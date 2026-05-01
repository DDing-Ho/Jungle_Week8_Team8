#include "CollisionSystem.h"

#include "Collision/Collision.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Spatial/WorldSpatialIndex.h"

namespace
{
	bool HasCollisionResponse(const UPrimitiveComponent* Component)
	{
		return Component != nullptr && (Component->IsGenerateOverlapEvents() || Component->IsBlockComponent());
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
	if (!FCollision::TestOverlap(A.Component, B.Component, &Hit))
	{
		return;
	}
}
