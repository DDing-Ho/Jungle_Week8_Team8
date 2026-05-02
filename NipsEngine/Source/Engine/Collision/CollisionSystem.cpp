#include "CollisionSystem.h"

#include "Collision/Collision.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Geometry/AABB.h"
#include "Math/Utils.h"
#include "UI/EditorConsoleWidget.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Spatial/WorldSpatialIndex.h"

namespace
{
	const char* GetCollisionTypeName(ECollisionType Type)
	{
		switch (Type)
		{
		case ECollisionType::Box:
			return "Box";
		case ECollisionType::Sphere:
			return "Sphere";
		case ECollisionType::Capsule:
			return "Capsule";
		default:
			return "None";
		}
	}

	FString GetActorLogName(const AActor* Actor)
	{
		return Actor ? Actor->GetName() : FString("None");
	}

	bool HasCollisionResponse(const UPrimitiveComponent* Component)
	{
		return Component != nullptr && (Component->IsGenerateOverlapEvents() || Component->IsBlockComponent());
	}

	FHitResult MakeReverseHit(const FHitResult& Hit, UPrimitiveComponent* HitComponent)
	{
		FHitResult Result = Hit;
		Result.HitComponent = HitComponent;
		Result.Normal = Hit.Normal * -1.0f;
		Result.bHit = (HitComponent != nullptr);
		return Result;
	}

	bool TryMakeAABBSeparation(const FAABB& A, const FAABB& B, FVector& OutNormal, float& OutDepth)
	{
		const float OverlapX = std::min(A.Max.X, B.Max.X) - std::max(A.Min.X, B.Min.X);
		const float OverlapY = std::min(A.Max.Y, B.Max.Y) - std::max(A.Min.Y, B.Min.Y);
		const float OverlapZ = std::min(A.Max.Z, B.Max.Z) - std::max(A.Min.Z, B.Min.Z);
		if (OverlapX <= 0.0f || OverlapY <= 0.0f || OverlapZ <= 0.0f)
		{
			return false;
		}

		const FVector Delta = B.GetCenter() - A.GetCenter();
		OutDepth = OverlapX;
		OutNormal = FVector((Delta.X >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);

		if (OverlapY < OutDepth)
		{
			OutDepth = OverlapY;
			OutNormal = FVector(0.0f, (Delta.Y >= 0.0f) ? 1.0f : -1.0f, 0.0f);
		}

		if (OverlapZ < OutDepth)
		{
			OutDepth = OverlapZ;
			OutNormal = FVector(0.0f, 0.0f, (Delta.Z >= 0.0f) ? 1.0f : -1.0f);
		}

		return true;
	}
}

void FCollisionSystem::UpdateWorldCollision(UWorld* World)
{
	if (World == nullptr)
	{
		return;
	}

	TArray<FCollisionCandidate> Candidates;
	GatherCandidates(World, Candidates);

	for (const FCollisionCandidate& Candidate : Candidates)
	{
		ProcessBroadCollision(World, Candidate);
	}

	ClearStaleCollisions(Candidates);
}

// 월드에서 충돌 처리가 필요한 ShapeComponent 후보를 수집합니다.
void FCollisionSystem::GatherCandidates(UWorld* World, TArray<FCollisionCandidate>& OutCandidates)
{
	OutCandidates.clear();
	if (World == nullptr)
	{
		return;
	}

	FWorldSpatialIndex& SpatialIndex = World->GetSpatialIndex();

	for (AActor* Actor : World->GetActors())
	{
		if (Actor == nullptr || !Actor->IsActive())
		{
			continue;
		}

		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (Primitive == nullptr || Cast<UShapeComponent>(Primitive) == nullptr)
			{
				continue;
			}

			if (Primitive->GetCollisionType() == ECollisionType::None || !HasCollisionResponse(Primitive))
			{
				continue;
			}

			const int32 ObjectIndex = SpatialIndex.FindObjectIndex(Primitive);
			if (ObjectIndex == FBVH::INDEX_NONE)
			{
				continue;
			}

			OutCandidates.push_back({ Actor, Primitive, ObjectIndex });
		}
	}
}

// BVH AABB Query를 통해 후보 하나와 충돌 가능성이 있는 Broad Phase 후보들을 찾습니다.
void FCollisionSystem::ProcessBroadCollision(UWorld* World, const FCollisionCandidate& Candidate)
{
	if (World == nullptr || Candidate.Component == nullptr)
	{
		return;
	}

	FWorldSpatialIndex& SpatialIndex = World->GetSpatialIndex();
	FWorldSpatialIndex::FPrimitiveAABBQueryScratch Scratch;
	TArray<UPrimitiveComponent*> BroadCandidates;
	SpatialIndex.AABBQueryPrimitives(Candidate.Component->GetWorldAABB(), BroadCandidates, Scratch);

	for (UPrimitiveComponent* OtherComponent : BroadCandidates)
	{
		if (OtherComponent == nullptr || OtherComponent == Candidate.Component)
		{
			continue;
		}

		const int32 OtherObjectIndex = SpatialIndex.FindObjectIndex(OtherComponent);
		if (OtherObjectIndex == FBVH::INDEX_NONE || Candidate.ObjectIndex >= OtherObjectIndex)
		{
			continue;
		}

		AActor* OtherActor = OtherComponent->GetOwner();
		if (OtherActor == nullptr || OtherActor == Candidate.Actor)
		{
			continue;
		}

		if (Cast<UShapeComponent>(OtherComponent) == nullptr || OtherComponent->GetCollisionType() == ECollisionType::None)
		{
			continue;
		}

		if (!HasCollisionResponse(OtherComponent))
		{
			continue;
		}

		ProcessNarrowCollision(Candidate, { OtherActor, OtherComponent, OtherObjectIndex });
	}
}

// Broad Phase를 통과한 두 ShapeComponent에 대해 실제 도형 기반 Narrow Phase 충돌을 판정합니다.
void FCollisionSystem::ProcessNarrowCollision(const FCollisionCandidate& A, const FCollisionCandidate& B)
{
	if (A.Component == nullptr || B.Component == nullptr || A.Actor == B.Actor)
	{
		return;
	}

	FHitResult Hit;
	const bool bIsOverlapping = FCollision::TestOverlap(A.Component, B.Component, &Hit);
	const FString ActorNameA = GetActorLogName(A.Actor);
	const FString ActorNameB = GetActorLogName(B.Actor);

	if (!bIsOverlapping)
	{
		return;
	}

	const bool bShouldBlock = A.Component->IsBlockComponent() || B.Component->IsBlockComponent();
	const bool bWasAOverlapping = A.Component->HasOverlapInfo(B.Actor, B.Component);
	const bool bWasBOverlapping = B.Component->HasOverlapInfo(A.Actor, A.Component);
	const bool bWasABlocking = A.Component->HasBlockingInfo(B.Actor, B.Component);
	const bool bWasBBlocking = B.Component->HasBlockingInfo(A.Actor, A.Component);

	if (bShouldBlock)
	{
		ProcessBlocking(A.Component, B.Component);

		if (bWasAOverlapping)
		{
			FOverlapResult EndOverlapInfo{ B.Actor, B.Component };
			A.Component->RemoveOverlapInfo(B.Actor, B.Component);
			A.Component->OnComponentEndOverlap.Broadcast(EndOverlapInfo);
		}

		if (bWasBOverlapping)
		{
			FOverlapResult EndOverlapInfo{ A.Actor, A.Component };
			B.Component->RemoveOverlapInfo(A.Actor, A.Component);
			B.Component->OnComponentEndOverlap.Broadcast(EndOverlapInfo);
		}

		if (!bWasABlocking)
		{
			A.Component->AddBlockingInfo(B.Actor, B.Component, Hit);
			A.Component->OnComponentHit.Broadcast(Hit);
			// UE_LOG("[Collision] Block Begin %s -> %s", ActorNameA.c_str(), ActorNameB.c_str());
		}

		if (!bWasBBlocking)
		{
			const FHitResult ReverseHit = MakeReverseHit(Hit, A.Component);
			B.Component->AddBlockingInfo(A.Actor, A.Component, ReverseHit);
			B.Component->OnComponentHit.Broadcast(ReverseHit);
			// UE_LOG("[Collision] Block Begin %s -> %s", ActorNameB.c_str(), ActorNameA.c_str());
		}

		return;
	}

	if (bWasABlocking)
	{
		A.Component->RemoveBlockingInfo(B.Actor, B.Component);
	}

	if (bWasBBlocking)
	{
		B.Component->RemoveBlockingInfo(A.Actor, A.Component);
	}

	if (A.Component->IsGenerateOverlapEvents() && !bWasAOverlapping)
	{
		FOverlapResult BeginOverlapInfo{ B.Actor, B.Component };
		A.Component->AddOverlapInfo(B.Actor, B.Component);
		A.Component->OnComponentBeginOverlap.Broadcast(BeginOverlapInfo);
	}

	if (B.Component->IsGenerateOverlapEvents() && !bWasBOverlapping)
	{
		FOverlapResult BeginOverlapInfo{ A.Actor, A.Component };
		B.Component->AddOverlapInfo(A.Actor, A.Component);
		B.Component->OnComponentBeginOverlap.Broadcast(BeginOverlapInfo);
	}
}

void FCollisionSystem::ProcessBlocking(UPrimitiveComponent* A, UPrimitiveComponent* B)
{
	if (A == nullptr || B == nullptr)
	{
		return;
	}

	FVector Normal;
	float Depth = 0.0f;
	if (!TryMakeAABBSeparation(A->GetWorldAABB(), B->GetWorldAABB(), Normal, Depth))
	{
		return;
	}

	constexpr float PushOutEpsilon = 0.1f;
	const float PushDistance = Depth + PushOutEpsilon;
	const bool bABlocks = A->IsBlockComponent();
	const bool bBBlocks = B->IsBlockComponent();

	if (bABlocks && bBBlocks)
	{
		if (AActor* OwnerA = A->GetOwner())
		{
			OwnerA->AddActorWorldOffset(Normal * (-PushDistance * 0.5f));
		}
		if (AActor* OwnerB = B->GetOwner())
		{
			OwnerB->AddActorWorldOffset(Normal * ( PushDistance * 0.5f));
		}
		return;
	}

	if (bABlocks)
	{
		if (AActor* OwnerB = B->GetOwner())
		{
			OwnerB->AddActorWorldOffset(Normal * PushDistance);
		}
		return;
	}

	if (bBBlocks)
	{
		if (AActor* OwnerA = A->GetOwner())
		{
			OwnerA->AddActorWorldOffset(Normal * -PushDistance);
		}
	}
}

void FCollisionSystem::ClearStaleCollisions(const TArray<FCollisionCandidate>& Candidates)
{
	for (const FCollisionCandidate& C : Candidates)
	{
		UPrimitiveComponent* Comp = C.Component;
		if (Comp == nullptr)
			continue;

		TArray<FOverlapResult> StaleOverlaps;
		for (const FOverlapResult& Info : Comp->GetOverlapInfos())
		{
			if (Info.OtherComp == nullptr || !FCollision::TestOverlap(Comp, Info.OtherComp))
				StaleOverlaps.push_back(Info);
		}
		for (const FOverlapResult& Stale : StaleOverlaps)
		{
			Comp->RemoveOverlapInfo(Stale.OtherActor, Stale.OtherComp);
			Comp->OnComponentEndOverlap.Broadcast(Stale);
			const FString ActorNameA = GetActorLogName(C.Actor);
			const FString ActorNameB = GetActorLogName(Stale.OtherActor);
		}

		TArray<FBlockingResult> StaleBlockings;
		for (const FBlockingResult& Info : Comp->GetBlockingInfos())
		{
			if (Info.OtherComp == nullptr || !FCollision::TestOverlap(Comp, Info.OtherComp))
				StaleBlockings.push_back(Info);
		}
		for (const FBlockingResult& Stale : StaleBlockings)
		{
			Comp->RemoveBlockingInfo(Stale.OtherActor, Stale.OtherComp);
			const FString ActorNameA = GetActorLogName(C.Actor);
			const FString ActorNameB = GetActorLogName(Stale.OtherActor);
		}
	}
}
