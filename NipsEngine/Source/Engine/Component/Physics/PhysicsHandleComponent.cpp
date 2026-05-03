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
#include <limits>

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

	FVector ComputeAABBPushOut(const FAABB& MovingBounds, const FAABB& StaticBounds)
	{
		if (!IntersectsAABB(MovingBounds, StaticBounds))
		{
			return FVector::ZeroVector;
		}

		const FVector MovingCenter = MovingBounds.GetCenter();
		const FVector StaticCenter = StaticBounds.GetCenter();
		float BestOverlap = std::numeric_limits<float>::max();
		int32 BestAxis = -1;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Overlap = GetAxisOverlap(MovingBounds, StaticBounds, Axis);
			if (Overlap > 0.0f && Overlap < BestOverlap)
			{
				BestOverlap = Overlap;
				BestAxis = Axis;
			}
		}

		if (BestAxis < 0 || BestOverlap == std::numeric_limits<float>::max())
		{
			return FVector::ZeroVector;
		}

		FVector Push = FVector::ZeroVector;
		const float Direction = MovingCenter[BestAxis] >= StaticCenter[BestAxis] ? 1.0f : -1.0f;
		Push[BestAxis] = Direction * (BestOverlap + HoldCollisionSkin);
		return Push;
	}

	FVector ResolveHeldBodyPenetration(UWorld* World, URigidBodyComponent* Body)
	{
		if (World == nullptr || Body == nullptr || Body->GetOwner() == nullptr)
		{
			return FVector::ZeroVector;
		}

		TArray<UPrimitiveComponent*> HeldShapes;
		GatherBlockingShapes(Body->GetOwner(), HeldShapes);
		if (HeldShapes.empty())
		{
			return FVector::ZeroVector;
		}

		FVector TotalPush = FVector::ZeroVector;
		for (int32 Iteration = 0; Iteration < 4; ++Iteration)
		{
			FVector IterationPush = FVector::ZeroVector;
			for (UPrimitiveComponent* HeldShape : HeldShapes)
			{
				if (HeldShape == nullptr)
				{
					continue;
				}

				const FAABB HeldBounds = ShiftAABB(HeldShape->GetWorldAABB(), TotalPush);
				for (AActor* OtherActor : World->GetActors())
				{
					if (OtherActor == nullptr || OtherActor == Body->GetOwner() || !OtherActor->IsActive())
					{
						continue;
					}

					for (UPrimitiveComponent* OtherPrimitive : OtherActor->GetPrimitiveComponents())
					{
						if (!IsBlockingShape(OtherPrimitive))
						{
							continue;
						}

						const FVector Push = ComputeAABBPushOut(ShiftAABB(HeldBounds, IterationPush), OtherPrimitive->GetWorldAABB());
						if (!Push.IsNearlyZero())
						{
							IterationPush += Push;
						}
					}
				}
			}

			if (IterationPush.IsNearlyZero())
			{
				break;
			}

			TotalPush += IterationPush;
		}

		return TotalPush;
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
	Ar << "HoldDistance" << HoldDistance;
	Ar << "SizeDistanceScale" << SizeDistanceScale;
	Ar << "MaxSizeDistanceOffset" << MaxSizeDistanceOffset;
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
	OutProps.push_back({ "Hold Distance", EPropertyType::Float, &HoldDistance, 0.1f, 20.0f, 0.1f });
	OutProps.push_back({ "Size Distance Scale", EPropertyType::Float, &SizeDistanceScale, 0.0f, 5.0f, 0.05f });
	OutProps.push_back({ "Max Size Distance Offset", EPropertyType::Float, &MaxSizeDistanceOffset, 0.0f, 20.0f, 0.1f });
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
	if (Camera == nullptr)
	{
		return false;
	}

	return TryGrab(World, Camera->GetLocation(), Camera->GetForwardVector());
}

bool UPhysicsHandleComponent::TryGrab(UWorld* World, const FVector& CameraLocation, const FVector& CameraForward)
{
	if (World == nullptr || HeldBody != nullptr)
	{
		return false;
	}

	ClampEditableValues();

	FHitResult Hit;
	const FVector Forward = CameraForward.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return false;
	}

	const FRay Ray(CameraLocation, Forward);
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
	HoldDistanceOffset = ComputeHoldDistanceOffset(Body, CameraLocation, Forward);
	CurrentHoldDistance = HoldDistance + HoldDistanceOffset;
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
		HoldDistanceOffset = 0.0f;
		CurrentHoldDistance = 0.0f;
		return;
	}

	HeldBody->SetHeldByPhysicsHandle(false);
	HeldBody->SetVelocity(HoldVelocity);
	HeldBody->PlayDropSound();
	HeldBody = nullptr;
	HeldWorld = nullptr;
	HoldVelocity = FVector::ZeroVector;
	HoldDistanceOffset = 0.0f;
	CurrentHoldDistance = 0.0f;
}

void UPhysicsHandleComponent::TickHandle(float DeltaTime, const FViewportCamera* Camera)
{
	if (Camera == nullptr)
	{
		return;
	}

	TickHandle(DeltaTime, Camera->GetLocation(), Camera->GetForwardVector());
}

void UPhysicsHandleComponent::TickHandle(float DeltaTime, const FVector& CameraLocation, const FVector& CameraForward)
{
	if (DeltaTime <= 0.0f || HeldBody == nullptr)
	{
		return;
	}

	if (!IsLiveObjectPointer(HeldBody))
	{
		HeldBody = nullptr;
		HeldWorld = nullptr;
		HoldVelocity = FVector::ZeroVector;
		HoldDistanceOffset = 0.0f;
		CurrentHoldDistance = 0.0f;
		return;
	}

	ClampEditableValues();

	HoldLocation = HeldBody->GetPhysicsLocation();
	LastHoldLocation = HoldLocation;

	const FVector Target = GetHoldTarget(CameraLocation, CameraForward);
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
	const FVector PenetrationPush = ResolveHeldBodyPenetration(HeldWorld, HeldBody);
	if (!PenetrationPush.IsNearlyZero())
	{
		const FVector PushNormal = PenetrationPush.GetSafeNormal();
		const float IntoSurfaceSpeed = FVector::DotProduct(HoldVelocity, PushNormal);
		if (IntoSurfaceSpeed < 0.0f)
		{
			HoldVelocity -= PushNormal * IntoSurfaceSpeed;
		}

		HoldLocation += PenetrationPush;
		HeldBody->SetPhysicsLocation(HoldLocation);
	}
	HeldBody->SetVelocity(HoldVelocity);
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

FVector UPhysicsHandleComponent::GetHoldTarget(const FVector& CameraLocation, const FVector& CameraForward) const
{
	const float TargetDistance = CurrentHoldDistance > 0.0f ? CurrentHoldDistance : (HoldDistance + HoldDistanceOffset);
	return CameraLocation + CameraForward.GetSafeNormal() * TargetDistance;
}

float UPhysicsHandleComponent::ComputeHoldDistanceOffset(URigidBodyComponent* Body, const FVector& CameraLocation, const FVector& CameraForward) const
{
	(void)CameraLocation;
	if (Body == nullptr || Body->GetOwner() == nullptr || SizeDistanceScale <= 0.0f || MaxSizeDistanceOffset <= 0.0f)
	{
		return 0.0f;
	}

	TArray<UPrimitiveComponent*> BlockingShapes;
	GatherBlockingShapes(Body->GetOwner(), BlockingShapes);
	if (BlockingShapes.empty())
	{
		return 0.0f;
	}

	const FVector BodyLocation = Body->GetPhysicsLocation();
	const FVector Forward = CameraForward.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return 0.0f;
	}

	float RadiusTowardCamera = 0.0f;
	for (UPrimitiveComponent* Shape : BlockingShapes)
	{
		if (Shape == nullptr)
		{
			continue;
		}

		const FAABB Bounds = Shape->GetWorldAABB();
		const FVector Corners[8] = {
			FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z),
			FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.Z),
			FVector(Bounds.Min.X, Bounds.Max.Y, Bounds.Min.Z),
			FVector(Bounds.Min.X, Bounds.Max.Y, Bounds.Max.Z),
			FVector(Bounds.Max.X, Bounds.Min.Y, Bounds.Min.Z),
			FVector(Bounds.Max.X, Bounds.Min.Y, Bounds.Max.Z),
			FVector(Bounds.Max.X, Bounds.Max.Y, Bounds.Min.Z),
			FVector(Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z),
		};

		for (const FVector& Corner : Corners)
		{
			const float Projection = FVector::DotProduct(Corner - BodyLocation, Forward);
			RadiusTowardCamera = std::max(RadiusTowardCamera, -Projection);
		}
	}

	return std::min(RadiusTowardCamera * SizeDistanceScale, MaxSizeDistanceOffset);
}

void UPhysicsHandleComponent::ClampEditableValues()
{
	TraceDistance = std::max(0.1f, TraceDistance);
	HoldDistance = std::max(0.1f, HoldDistance);
	SizeDistanceScale = std::max(0.0f, SizeDistanceScale);
	MaxSizeDistanceOffset = std::max(0.0f, MaxSizeDistanceOffset);
	SpringStrength = std::max(0.0f, SpringStrength);
	Damping = std::max(0.0f, Damping);
	MaxHoldSpeed = std::max(0.0f, MaxHoldSpeed);
}
