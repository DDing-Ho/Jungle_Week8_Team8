#include "Component/Physics/RigidBodyComponent.h"

#include "Audio/AudioSystem.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	constexpr float GroundSupportTolerance = 0.05f;
	constexpr float GroundSupportHorizontalSlop = 0.001f;

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

DEFINE_CLASS(URigidBodyComponent, UActorComponent)
REGISTER_FACTORY(URigidBodyComponent)

URigidBodyComponent::URigidBodyComponent()
{
	bCanEverTick = true;
}

void URigidBodyComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	ClampEditableValues();

	if (UpdatedComponent == nullptr && Owner != nullptr)
	{
		UpdatedComponent = Owner->GetRootComponent();
	}
}

void URigidBodyComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);
	uint32 UpdatedComponentUUID = UpdatedComponent ? UpdatedComponent->GetUUID() : 0;
	Ar << "UpdatedComponentUUID" << UpdatedComponentUUID;
	Ar << "Velocity" << Velocity;
	Ar << "SimulatePhysics" << bSimulatePhysics;
	Ar << "UseGravity" << bUseGravity;
	Ar << "CanBePickedUp" << bCanBePickedUp;
	Ar << "Mass" << Mass;
	Ar << "GravityScale" << GravityScale;
	Ar << "LinearDamping" << LinearDamping;
	Ar << "MaxSpeed" << MaxSpeed;
	Ar << "SleepSpeed" << SleepSpeed;
	Ar << "PickupSoundPath" << PickupSoundPath;
	Ar << "DropSoundPath" << DropSoundPath;

	if (Ar.IsLoading())
	{
		ClampEditableValues();
	}
}

void URigidBodyComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UActorComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Updated Component", EPropertyType::SceneComponentRef, &UpdatedComponent });
	OutProps.push_back({ "Velocity", EPropertyType::Vec3, &Velocity });
	OutProps.push_back({ "Simulate Physics", EPropertyType::Bool, &bSimulatePhysics });
	OutProps.push_back({ "Use Gravity", EPropertyType::Bool, &bUseGravity });
	OutProps.push_back({ "Can Be Picked Up", EPropertyType::Bool, &bCanBePickedUp });
	OutProps.push_back({ "Mass", EPropertyType::Float, &Mass, 0.01f, 1000.0f, 0.1f });
	OutProps.push_back({ "Gravity Scale", EPropertyType::Float, &GravityScale, 0.0f, 10.0f, 0.01f });
	OutProps.push_back({ "Linear Damping", EPropertyType::Float, &LinearDamping, 0.0f, 50.0f, 0.01f });
	OutProps.push_back({ "Max Speed", EPropertyType::Float, &MaxSpeed, 0.0f, 1000.0f, 0.1f });
	OutProps.push_back({ "Sleep Speed", EPropertyType::Float, &SleepSpeed, 0.0f, 10.0f, 0.01f });
	OutProps.push_back({ "Pickup Sound", EPropertyType::String, &PickupSoundPath });
	OutProps.push_back({ "Drop Sound", EPropertyType::String, &DropSoundPath });
}

void URigidBodyComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);
	ClampEditableValues();
}

USceneComponent* URigidBodyComponent::GetUpdatedComponent() const
{
	if (IsLiveObjectPointer(UpdatedComponent))
	{
		return UpdatedComponent;
	}

	if (IsLiveObjectPointer(Owner))
	{
		USceneComponent* Root = Owner->GetRootComponent();
		return IsLiveObjectPointer(Root) ? Root : nullptr;
	}

	return nullptr;
}

void URigidBodyComponent::SetHeldByPhysicsHandle(bool bHeld)
{
	if (bHeldByPhysicsHandle == bHeld)
	{
		return;
	}

	bHeldByPhysicsHandle = bHeld;
	if (bHeld)
	{
		bWasSimulatingBeforeHold = bSimulatePhysics;
		bSimulatePhysics = false;
	}
	else
	{
		bSimulatePhysics = bWasSimulatingBeforeHold;
	}
}

void URigidBodyComponent::AddImpulse(const FVector& Impulse)
{
	ClampEditableValues();
	Velocity += Impulse / Mass;
	bGrounded = false;
	bGroundPushOutSinceLastTick = false;
}

void URigidBodyComponent::NotifyBlockingPushOut(const FVector& PushDelta)
{
	const FVector PushNormal = PushDelta.GetSafeNormal();
	if (PushNormal.IsNearlyZero())
	{
		return;
	}

	if (PushDelta.Z > 0.0f)
	{
		bGrounded = true;
		bGroundPushOutSinceLastTick = true;
		if (Velocity.Z < 0.0f)
		{
			Velocity.Z = 0.0f;
		}
	}

	const float IntoSurfaceSpeed = FVector::DotProduct(Velocity, PushNormal);
	if (IntoSurfaceSpeed < 0.0f)
	{
		Velocity -= PushNormal * IntoSurfaceSpeed;
	}
}

FVector URigidBodyComponent::GetPhysicsLocation() const
{
	if (const USceneComponent* Scene = GetUpdatedComponent())
	{
		return Scene->GetWorldLocation();
	}

	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

void URigidBodyComponent::SetPhysicsLocation(const FVector& NewLocation)
{
	if (USceneComponent* Scene = GetUpdatedComponent())
	{
		Scene->SetWorldLocation(NewLocation);
		return;
	}

	if (Owner != nullptr)
	{
		Owner->SetActorLocation(NewLocation);
	}
}

void URigidBodyComponent::PlayPickupSound() const
{
	if (!PickupSoundPath.empty())
	{
		FAudioSystem::Get().PlayAtLocation(PickupSoundPath, GetPhysicsLocation(), 1.0f, false, 0.5f, 8.0f);
	}
}

void URigidBodyComponent::PlayDropSound() const
{
	if (!DropSoundPath.empty())
	{
		FAudioSystem::Get().PlayAtLocation(DropSoundPath, GetPhysicsLocation(), 1.0f, false, 0.5f, 8.0f);
	}
}

void URigidBodyComponent::TickComponent(float DeltaTime)
{
	if (DeltaTime <= 0.0f || !bSimulatePhysics || bHeldByPhysicsHandle)
	{
		return;
	}

	ClampEditableValues();
	ApplyBlockingResponse();

	const bool bWasGrounded = bGrounded;
	const bool bWasPushedOntoGround = bGroundPushOutSinceLastTick;
	bGroundPushOutSinceLastTick = false;
	const bool bHasGroundContact = HasGroundContact();
	const bool bCanKeepRestingSupport = bWasGrounded || bWasPushedOntoGround || bHasGroundContact;
	float RestingSnapDeltaZ = 0.0f;
	const bool bHasRestingSupport = bCanKeepRestingSupport && FindRestingSupport(GroundSupportTolerance, RestingSnapDeltaZ);
	bGrounded = bHasRestingSupport;
	if (bGrounded && Velocity.Z < 0.0f)
	{
		Velocity.Z = 0.0f;
	}

	if (bGrounded && std::fabs(RestingSnapDeltaZ) > 0.0001f)
	{
		if (AActor* OwnerActor = Owner)
		{
			OwnerActor->AddActorWorldOffset(FVector(0.0f, 0.0f, RestingSnapDeltaZ));
		}
	}

	if (bUseGravity && !(bGrounded && Velocity.Z <= 0.0f))
	{
		Velocity += FVector(0.0f, 0.0f, -9.8f * GravityScale) * DeltaTime;
	}

	if (LinearDamping > 0.0f)
	{
		const float DampingScale = std::max(0.0f, 1.0f - LinearDamping * DeltaTime);
		Velocity *= DampingScale;
	}

	if (MaxSpeed > 0.0f && Velocity.SizeSquared() > MaxSpeed * MaxSpeed)
	{
		Velocity = Velocity.GetSafeNormal() * MaxSpeed;
	}

	if (bGrounded && Velocity.SizeSquared() <= SleepSpeed * SleepSpeed)
	{
		Velocity = FVector::ZeroVector;
		return;
	}

	if (USceneComponent* Scene = GetUpdatedComponent())
	{
		Scene->AddWorldOffset(Velocity * DeltaTime);
	}

	if (Velocity.Z > SleepSpeed)
	{
		bGrounded = false;
	}
}

void URigidBodyComponent::ClampEditableValues()
{
	Mass = std::max(0.01f, Mass);
	GravityScale = std::max(0.0f, GravityScale);
	LinearDamping = std::max(0.0f, LinearDamping);
	MaxSpeed = std::max(0.0f, MaxSpeed);
	SleepSpeed = std::max(0.0f, SleepSpeed);
}

void URigidBodyComponent::ApplyBlockingResponse()
{
	if (Owner == nullptr)
	{
		return;
	}

	for (UPrimitiveComponent* Primitive : Owner->GetPrimitiveComponents())
	{
		if (Primitive == nullptr)
		{
			continue;
		}

		for (const FBlockingResult& Blocking : Primitive->GetBlockingInfos())
		{
			if (Blocking.OtherComp == nullptr)
			{
				continue;
			}

			FVector Normal = Primitive->GetWorldAABB().GetCenter() - Blocking.OtherComp->GetWorldAABB().GetCenter();
			if (Normal.IsNearlyZero())
			{
				Normal = Blocking.Hit.Normal;
				if (Normal.IsNearlyZero())
				{
					continue;
				}
			}

			Normal = Normal.GetSafeNormal();
			const float IntoSurfaceSpeed = FVector::DotProduct(Velocity, Normal);
			if (IntoSurfaceSpeed < 0.0f)
			{
				Velocity -= Normal * IntoSurfaceSpeed;
			}

			if (Normal.Z > 0.5f && Velocity.Z < 0.0f)
			{
				Velocity.Z = 0.0f;
			}
		}
	}
}

bool URigidBodyComponent::HasBlockingContact() const
{
	if (Owner == nullptr)
	{
		return false;
	}

	for (UPrimitiveComponent* Primitive : Owner->GetPrimitiveComponents())
	{
		if (Primitive != nullptr && !Primitive->GetBlockingInfos().empty())
		{
			return true;
		}
	}

	return false;
}

bool URigidBodyComponent::HasGroundContact() const
{
	if (Owner == nullptr)
	{
		return false;
	}

	for (UPrimitiveComponent* Primitive : Owner->GetPrimitiveComponents())
	{
		if (Primitive == nullptr)
		{
			continue;
		}

		for (const FBlockingResult& Blocking : Primitive->GetBlockingInfos())
		{
			if (Blocking.OtherComp == nullptr)
			{
				continue;
			}

			const FVector Normal = (Primitive->GetWorldAABB().GetCenter() - Blocking.OtherComp->GetWorldAABB().GetCenter()).GetSafeNormal();
			if (Normal.Z > 0.5f)
			{
				return true;
			}
		}
	}

	return false;
}

bool URigidBodyComponent::FindRestingSupport(float Tolerance, float& OutSnapDeltaZ) const
{
	OutSnapDeltaZ = 0.0f;
	if (Owner == nullptr)
	{
		return false;
	}

	UWorld* World = Owner->GetFocusedWorld();
	if (World == nullptr)
	{
		return false;
	}

	bool bFoundSupport = false;
	float BestAbsDeltaZ = std::numeric_limits<float>::max();

	for (UPrimitiveComponent* Primitive : Owner->GetPrimitiveComponents())
	{
		if (Primitive == nullptr || !Primitive->IsBlockComponent())
		{
			continue;
		}

		const FAABB OwnBounds = Primitive->GetWorldAABB();
		for (AActor* OtherActor : World->GetActors())
		{
			if (OtherActor == nullptr || OtherActor == Owner || !OtherActor->IsActive())
			{
				continue;
			}

			for (UPrimitiveComponent* OtherPrimitive : OtherActor->GetPrimitiveComponents())
			{
				if (OtherPrimitive == nullptr || !OtherPrimitive->IsBlockComponent())
				{
					continue;
				}

				const FAABB OtherBounds = OtherPrimitive->GetWorldAABB();
				const FVector OwnCenter = OwnBounds.GetCenter();
				const bool bCenterSupportedX =
					OwnCenter.X >= OtherBounds.Min.X - GroundSupportHorizontalSlop &&
					OwnCenter.X <= OtherBounds.Max.X + GroundSupportHorizontalSlop;
				const bool bCenterSupportedY =
					OwnCenter.Y >= OtherBounds.Min.Y - GroundSupportHorizontalSlop &&
					OwnCenter.Y <= OtherBounds.Max.Y + GroundSupportHorizontalSlop;
				if (!bCenterSupportedX || !bCenterSupportedY)
				{
					continue;
				}

				const float BottomZ = OwnBounds.Min.Z;
				const float SupportTopZ = OtherBounds.Max.Z;
				const float SnapDeltaZ = SupportTopZ - BottomZ;
				const float AbsDeltaZ = std::fabs(SnapDeltaZ);
				if (AbsDeltaZ <= Tolerance && AbsDeltaZ < BestAbsDeltaZ)
				{
					BestAbsDeltaZ = AbsDeltaZ;
					OutSnapDeltaZ = SnapDeltaZ;
					bFoundSupport = true;
				}
			}
		}
	}

	return bFoundSupport;
}
