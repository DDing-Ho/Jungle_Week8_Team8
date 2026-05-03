#include "Component/Physics/PhysicsHandleComponent.h"

#include "Component/Collision/ShapeComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Engine/Geometry/AABB.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>

namespace
{
	constexpr float HoldCollisionSkin = 0.005f;

	bool IsLiveObjectPointer(const UObject* Object)
	{
		if (Object == nullptr)
		{
			return false;
		}

		for (const UObject* LiveObject : GUObjectArray)
		{
			if (LiveObject == Object)
			{
				return true;
			}
		}

		return false;
	}

	bool IsBlockingShape(UPrimitiveComponent* Primitive)
	{
		return Primitive != nullptr && Primitive->IsBlockComponent() && Cast<UShapeComponent>(Primitive) != nullptr;
	}

	bool IntersectsAABB(const FAABB& A, const FAABB& B)
	{
		return A.Min.X < B.Max.X && A.Max.X > B.Min.X &&
			A.Min.Y < B.Max.Y && A.Max.Y > B.Min.Y &&
			A.Min.Z < B.Max.Z && A.Max.Z > B.Min.Z;
	}

	bool OverlapsExceptAxis(const FAABB& A, const FAABB& B, int32 ExcludedAxis)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Axis == ExcludedAxis)
			{
				continue;
			}

			if (A.Min[Axis] >= B.Max[Axis] || A.Max[Axis] <= B.Min[Axis])
			{
				return false;
			}
		}

		return true;
	}

	float GetAxisOverlap(const FAABB& A, const FAABB& B, int32 Axis)
	{
		return std::min(A.Max[Axis], B.Max[Axis]) - std::max(A.Min[Axis], B.Min[Axis]);
	}

	FAABB ShiftAABB(const FAABB& Bounds, const FVector& Delta)
	{
		return FAABB(Bounds.Min + Delta, Bounds.Max + Delta);
	}

	void GatherBlockingShapes(AActor* Actor, TArray<UPrimitiveComponent*>& OutShapes)
	{
		if (Actor == nullptr)
		{
			return;
		}

		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (IsBlockingShape(Primitive))
			{
				OutShapes.push_back(Primitive);
			}
		}
	}

	float ResolveAxisMove(
		UWorld* World,
		const TArray<UPrimitiveComponent*>& HeldShapes,
		AActor* HeldOwner,
		const FVector& DeltaBeforeAxis,
		int32 Axis,
		float AxisDelta)
	{
		if (World == nullptr || HeldOwner == nullptr || HeldShapes.empty() || AxisDelta == 0.0f)
		{
			return AxisDelta;
		}

		float ResolvedDelta = AxisDelta;
		const bool bMovingPositive = AxisDelta > 0.0f;
		for (UPrimitiveComponent* HeldShape : HeldShapes)
		{
			if (HeldShape == nullptr)
			{
				continue;
			}

			const FAABB CurrentBounds = ShiftAABB(HeldShape->GetWorldAABB(), DeltaBeforeAxis);
			FVector CandidateDelta = DeltaBeforeAxis;
			CandidateDelta[Axis] += ResolvedDelta;
			const FAABB CandidateBounds = ShiftAABB(HeldShape->GetWorldAABB(), CandidateDelta);

			for (AActor* OtherActor : World->GetActors())
			{
				if (OtherActor == nullptr || OtherActor == HeldOwner || !OtherActor->IsActive())
				{
					continue;
				}

				for (UPrimitiveComponent* OtherPrimitive : OtherActor->GetPrimitiveComponents())
				{
					if (!IsBlockingShape(OtherPrimitive))
					{
						continue;
					}

					const FAABB OtherBounds = OtherPrimitive->GetWorldAABB();
					const bool bAlreadyOverlapping = IntersectsAABB(CurrentBounds, OtherBounds);
					const bool bCandidateOverlapping = IntersectsAABB(CandidateBounds, OtherBounds);
					if (bAlreadyOverlapping)
					{
						const float CurrentAxisOverlap = GetAxisOverlap(CurrentBounds, OtherBounds, Axis);
						const float CandidateAxisOverlap = GetAxisOverlap(CandidateBounds, OtherBounds, Axis);
						if (CandidateAxisOverlap > CurrentAxisOverlap + HoldCollisionSkin)
						{
							ResolvedDelta = 0.0f;
							return 0.0f;
						}
						continue;
					}

					const bool bSweptIntoBounds = OverlapsExceptAxis(CandidateBounds, OtherBounds, Axis) &&
						((bMovingPositive && CurrentBounds.Max[Axis] <= OtherBounds.Min[Axis] && CandidateBounds.Max[Axis] > OtherBounds.Min[Axis]) ||
						(!bMovingPositive && CurrentBounds.Min[Axis] >= OtherBounds.Max[Axis] && CandidateBounds.Min[Axis] < OtherBounds.Max[Axis]));
					if (!bCandidateOverlapping && !bSweptIntoBounds)
					{
						continue;
					}

					if (bMovingPositive)
					{
						const float Allowed = OtherBounds.Min[Axis] - CurrentBounds.Max[Axis] - HoldCollisionSkin;
						ResolvedDelta = std::min(ResolvedDelta, std::max(0.0f, Allowed));
					}
					else
					{
						const float Allowed = OtherBounds.Max[Axis] - CurrentBounds.Min[Axis] + HoldCollisionSkin;
						ResolvedDelta = std::max(ResolvedDelta, std::min(0.0f, Allowed));
					}

					if (ResolvedDelta == 0.0f)
					{
						return 0.0f;
					}
				}
			}
		}

		return ResolvedDelta;
	}

	FVector ResolveHeldBodyMovement(UWorld* World, URigidBodyComponent* Body, const FVector& DesiredDelta)
	{
		if (World == nullptr || Body == nullptr || Body->GetOwner() == nullptr || DesiredDelta.IsNearlyZero())
		{
			return DesiredDelta;
		}

		TArray<UPrimitiveComponent*> HeldShapes;
		GatherBlockingShapes(Body->GetOwner(), HeldShapes);
		if (HeldShapes.empty())
		{
			return DesiredDelta;
		}

		FVector ResolvedDelta = FVector::ZeroVector;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float AxisDelta = ResolveAxisMove(World, HeldShapes, Body->GetOwner(), ResolvedDelta, Axis, DesiredDelta[Axis]);
			ResolvedDelta[Axis] += AxisDelta;
		}

		return ResolvedDelta;
	}
}

DEFINE_CLASS(UPhysicsHandleComponent, UActorComponent)
REGISTER_FACTORY(UPhysicsHandleComponent)

UPhysicsHandleComponent::UPhysicsHandleComponent()
{
	bCanEverTick = true;
}

void UPhysicsHandleComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);
	Ar << "TraceDistance" << TraceDistance;
	Ar << "DefaultHoldDistance" << DefaultHoldDistance;
	Ar << "HoldDistance" << HoldDistance;
	Ar << "SpringStrength" << SpringStrength;
	Ar << "Damping" << Damping;
	Ar << "MaxHoldSpeed" << MaxHoldSpeed;

	if (Ar.IsLoading())
	{
		ClampEditableValues();
	}
}

void UPhysicsHandleComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UActorComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Trace Distance", EPropertyType::Float, &TraceDistance, 0.1f, 100.0f, 0.1f });
	OutProps.push_back({ "Default Hold Distance", EPropertyType::Float, &DefaultHoldDistance, 0.1f, 20.0f, 0.1f });
	OutProps.push_back({ "Hold Distance", EPropertyType::Float, &HoldDistance, 0.1f, 20.0f, 0.1f });
	OutProps.push_back({ "Spring Strength", EPropertyType::Float, &SpringStrength, 0.0f, 1000.0f, 1.0f });
	OutProps.push_back({ "Damping", EPropertyType::Float, &Damping, 0.0f, 1000.0f, 0.1f });
	OutProps.push_back({ "Max Hold Speed", EPropertyType::Float, &MaxHoldSpeed, 0.0f, 1000.0f, 0.1f });
}

void UPhysicsHandleComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);
	ClampEditableValues();
}

bool UPhysicsHandleComponent::TryGrab(UWorld* World, const FViewportCamera* Camera)
{
	if (World == nullptr || Camera == nullptr || HeldBody != nullptr)
	{
		return false;
	}

	ClampEditableValues();

	FHitResult Hit;
	const FRay Ray(Camera->GetLocation(), Camera->GetForwardVector().GetSafeNormal());
	if (!World->LineTraceSingle(Ray, TraceDistance, Hit))
	{
		return false;
	}

	URigidBodyComponent* Body = FindRigidBodyFromHit(Hit);
	if (!IsLiveObjectPointer(Body) || !Body->CanBePickedUp())
	{
		return false;
	}

	HeldBody = Body;
	HeldWorld = World;
	HoldLocation = Body->GetPhysicsLocation();
	LastHoldLocation = HoldLocation;
	HoldVelocity = FVector::ZeroVector;
	Body->SetHeldByPhysicsHandle(true);
	Body->SetVelocity(FVector::ZeroVector);
	Body->PlayPickupSound();
	return true;
}

void UPhysicsHandleComponent::Release()
{
	if (HeldBody == nullptr)
	{
		return;
	}

	if (!IsLiveObjectPointer(HeldBody))
	{
		HeldBody = nullptr;
		HeldWorld = nullptr;
		HoldVelocity = FVector::ZeroVector;
		return;
	}

	HeldBody->SetHeldByPhysicsHandle(false);
	HeldBody->SetVelocity(HoldVelocity);
	HeldBody->PlayDropSound();
	HeldBody = nullptr;
	HeldWorld = nullptr;
	HoldVelocity = FVector::ZeroVector;
}

void UPhysicsHandleComponent::TickHandle(float DeltaTime, const FViewportCamera* Camera, const FVector& TargetOffset, const FQuat* TargetRotation)
{
	if (DeltaTime <= 0.0f || HeldBody == nullptr || Camera == nullptr)
	{
		return;
	}

	if (!IsLiveObjectPointer(HeldBody))
	{
		HeldBody = nullptr;
		HeldWorld = nullptr;
		HoldVelocity = FVector::ZeroVector;
		return;
	}

	ClampEditableValues();

	HoldLocation = HeldBody->GetPhysicsLocation();
	LastHoldLocation = HoldLocation;

	const FVector Target = GetHoldTarget(Camera, TargetOffset);
	const FVector ToTarget = Target - HoldLocation;
	const FVector Acceleration = ToTarget * SpringStrength - HoldVelocity * Damping;
	HoldVelocity += Acceleration * DeltaTime;

	if (MaxHoldSpeed > 0.0f && HoldVelocity.SizeSquared() > MaxHoldSpeed * MaxHoldSpeed)
	{
		HoldVelocity = HoldVelocity.GetSafeNormal() * MaxHoldSpeed;
	}

	LastHoldLocation = HoldLocation;
	const FVector DesiredDelta = HoldVelocity * DeltaTime;
	const FVector ResolvedDelta = ResolveHeldBodyMovement(HeldWorld, HeldBody, DesiredDelta);
	HoldLocation += ResolvedDelta;
	HoldVelocity = DeltaTime > 0.0f ? ResolvedDelta / DeltaTime : FVector::ZeroVector;
	HeldBody->SetPhysicsLocation(HoldLocation);
	HeldBody->SetVelocity(HoldVelocity);
	if (TargetRotation)
	{
		HeldBody->SetPhysicsRotation(*TargetRotation);
	}
	HeldBody->SetVelocity(DeltaTime > 0.0f ? (HoldLocation - LastHoldLocation) / DeltaTime : FVector::ZeroVector);
}

URigidBodyComponent* UPhysicsHandleComponent::FindRigidBodyFromHit(const FHitResult& Hit) const
{
	const UPrimitiveComponent* HitComponent = Hit.HitComponent;
	AActor* HitActor = HitComponent ? HitComponent->GetOwner() : nullptr;
	if (HitActor == nullptr)
	{
		return nullptr;
	}

	for (UActorComponent* Component : HitActor->GetComponents())
	{
		if (URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component))
		{
			if (IsLiveObjectPointer(Body))
			{
				return Body;
			}
		}
	}

	return nullptr;
}

FVector UPhysicsHandleComponent::GetHoldTarget(const FViewportCamera* Camera, const FVector& TargetOffset) const
{
	return Camera->GetLocation() + Camera->GetForwardVector().GetSafeNormal() * HoldDistance + TargetOffset;
}

void UPhysicsHandleComponent::ClampEditableValues()
{
	TraceDistance = std::max(0.1f, TraceDistance);
	DefaultHoldDistance = std::max(0.1f, DefaultHoldDistance);
	HoldDistance = std::max(0.1f, HoldDistance);
	SpringStrength = std::max(0.0f, SpringStrength);
	Damping = std::max(0.0f, Damping);
	MaxHoldSpeed = std::max(0.0f, MaxHoldSpeed);
}
