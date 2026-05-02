#include "Component/Physics/PhysicsHandleComponent.h"

#include "Component/Physics/RigidBodyComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>

namespace
{
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
		HoldVelocity = FVector::ZeroVector;
		return;
	}

	HeldBody->SetHeldByPhysicsHandle(false);
	HeldBody->SetVelocity(HoldVelocity);
	HeldBody->PlayDropSound();
	HeldBody = nullptr;
	HoldVelocity = FVector::ZeroVector;
}

void UPhysicsHandleComponent::TickHandle(float DeltaTime, const FViewportCamera* Camera)
{
	if (DeltaTime <= 0.0f || HeldBody == nullptr || Camera == nullptr)
	{
		return;
	}

	if (!IsLiveObjectPointer(HeldBody))
	{
		HeldBody = nullptr;
		HoldVelocity = FVector::ZeroVector;
		return;
	}

	ClampEditableValues();

	HoldLocation = HeldBody->GetPhysicsLocation();
	LastHoldLocation = HoldLocation;

	const FVector Target = GetHoldTarget(Camera);
	const FVector ToTarget = Target - HoldLocation;
	const FVector Acceleration = ToTarget * SpringStrength - HoldVelocity * Damping;
	HoldVelocity += Acceleration * DeltaTime;

	if (MaxHoldSpeed > 0.0f && HoldVelocity.SizeSquared() > MaxHoldSpeed * MaxHoldSpeed)
	{
		HoldVelocity = HoldVelocity.GetSafeNormal() * MaxHoldSpeed;
	}

	LastHoldLocation = HoldLocation;
	HoldLocation += HoldVelocity * DeltaTime;
	HeldBody->SetPhysicsLocation(HoldLocation);
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

FVector UPhysicsHandleComponent::GetHoldTarget(const FViewportCamera* Camera) const
{
	return Camera->GetLocation() + Camera->GetForwardVector().GetSafeNormal() * HoldDistance;
}

void UPhysicsHandleComponent::ClampEditableValues()
{
	TraceDistance = std::max(0.1f, TraceDistance);
	HoldDistance = std::max(0.1f, HoldDistance);
	SpringStrength = std::max(0.0f, SpringStrength);
	Damping = std::max(0.0f, Damping);
	MaxHoldSpeed = std::max(0.0f, MaxHoldSpeed);
}
