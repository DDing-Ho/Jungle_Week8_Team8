#include "Component/Movement/CharacterMovementComponent.h"

#include "Component/Physics/RigidBodyComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Logger.h"
#include "GameFramework/AActor.h"
#include "Object/ObjectFactory.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float GravityAcceleration = 9.8f;
}

DEFINE_CLASS(UCharacterMovementComponent, UMovementComponent)
REGISTER_FACTORY(UCharacterMovementComponent)

UCharacterMovementComponent::UCharacterMovementComponent()
{
	bCanEverTick = true;
	Velocity = FVector::ZeroVector;
}

void UCharacterMovementComponent::BeginPlay()
{
	UMovementComponent::BeginPlay();
	RefreshUpdatedReferences();
	ClampEditableValues();
}

void UCharacterMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);
	uint32 RigidBodyUUID = RigidBody ? RigidBody->GetUUID() : 0;
	Ar << "RigidBodyUUID" << RigidBodyUUID;
	Ar << "MaxWalkSpeed" << MaxWalkSpeed;
	Ar << "Acceleration" << Acceleration;
	Ar << "BrakingDeceleration" << BrakingDeceleration;
	Ar << "GravityScale" << GravityScale;
	Ar << "MaxFallSpeed" << MaxFallSpeed;
	Ar << "GroundStickVelocity" << GroundStickVelocity;
	Ar << "GroundProbeDistance" << GroundProbeDistance;

	if (Ar.IsLoading())
	{
		ClampEditableValues();
	}
}

void UCharacterMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UMovementComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Velocity", EPropertyType::Vec3, &Velocity });
	OutProps.push_back({ "Max Walk Speed", EPropertyType::Float, &MaxWalkSpeed, 0.0f, 20.0f, 0.1f });
	OutProps.push_back({ "Acceleration", EPropertyType::Float, &Acceleration, 0.0f, 100.0f, 0.5f });
	OutProps.push_back({ "Braking Deceleration", EPropertyType::Float, &BrakingDeceleration, 0.0f, 100.0f, 0.5f });
	OutProps.push_back({ "Gravity Scale", EPropertyType::Float, &GravityScale, 0.0f, 10.0f, 0.05f });
	OutProps.push_back({ "Max Fall Speed", EPropertyType::Float, &MaxFallSpeed, 0.0f, 100.0f, 0.5f });
	OutProps.push_back({ "Ground Stick Velocity", EPropertyType::Float, &GroundStickVelocity, -10.0f, 0.0f, 0.05f });
	OutProps.push_back({ "Ground Probe Distance", EPropertyType::Float, &GroundProbeDistance, 0.0f, 1.0f, 0.01f });
}

void UCharacterMovementComponent::TickComponent(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		ConsumeInputVector();
		return;
	}

	RefreshUpdatedReferences();
	if (UpdatedComponent == nullptr)
	{
		ConsumeInputVector();
		return;
	}

	ClampEditableValues();

	FVector Input = ConsumeInputVector();
	Input.Z = 0.0f;
	if (Input.SizeSquared() > 1.0f)
	{
		Input = Input.GetSafeNormal();
	}

	const FVector DesiredHorizontalVelocity = Input * MaxWalkSpeed;
	const float HorizontalStep = (Input.IsNearlyZero() ? BrakingDeceleration : Acceleration) * DeltaTime;
	Velocity.X = MoveToward(Velocity.X, DesiredHorizontalVelocity.X, HorizontalStep);
	Velocity.Y = MoveToward(Velocity.Y, DesiredHorizontalVelocity.Y, HorizontalStep);

	if (bGrounded)
	{
		Velocity.Z = 0.0f;
	}
	else
	{
		Velocity.Z -= GravityAcceleration * GravityScale * DeltaTime;
		Velocity.Z = std::max(Velocity.Z, -MaxFallSpeed);
	}

	const FVector StartLocation = UpdatedComponent->GetWorldLocation();
	FVector TargetLocation = StartLocation + Velocity * DeltaTime;
	const FVector RequestedDelta = TargetLocation - StartLocation;

	bool bMovedByCharacter = false;
	if (RigidBody != nullptr && FJoltPhysicsSystem::Get().IsInitialized())
	{
		FVector ResolvedLocation = TargetLocation;
		FVector ResolvedVelocity = Velocity;
		bool bResolvedGrounded = false;
		bMovedByCharacter = FJoltPhysicsSystem::Get().MoveCharacter(
			RigidBody,
			Velocity,
			DeltaTime,
			GroundProbeDistance,
			ResolvedLocation,
			ResolvedVelocity,
			bResolvedGrounded);
		if (bMovedByCharacter)
		{
			TargetLocation = ResolvedLocation;
			Velocity = ResolvedVelocity;
			bGrounded = bResolvedGrounded;
		}
	}

	UpdatedComponent->SetWorldLocation(TargetLocation);

	const FVector ActualDelta = TargetLocation - StartLocation;
	if (!bMovedByCharacter)
	{
		const bool bHitGround = RequestedDelta.Z < -0.0001f && ActualDelta.Z > RequestedDelta.Z + 0.001f;
		bGrounded = bHitGround;
	}
	if (bGrounded && Velocity.Z < 0.0f)
	{
		Velocity.Z = 0.0f;
	}

	if (RequestedDelta.X != 0.0f && std::fabs(ActualDelta.X) < std::fabs(RequestedDelta.X) * 0.5f)
	{
		Velocity.X = 0.0f;
	}
	if (RequestedDelta.Y != 0.0f && std::fabs(ActualDelta.Y) < std::fabs(RequestedDelta.Y) * 0.5f)
	{
		Velocity.Y = 0.0f;
	}
}

void UCharacterMovementComponent::RefreshUpdatedReferences()
{
	if (Owner == nullptr)
	{
		UpdatedComponent = nullptr;
		RigidBody = nullptr;
		return;
	}

	if (UpdatedComponent == nullptr)
	{
		UpdatedComponent = Owner->GetRootComponent();
	}

	if (RigidBody == nullptr)
	{
		for (UActorComponent* Component : Owner->GetComponents())
		{
			if (URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component))
			{
				RigidBody = Body;
				break;
			}
		}
	}
}

void UCharacterMovementComponent::ClampEditableValues()
{
	MaxWalkSpeed = std::max(0.0f, MaxWalkSpeed);
	Acceleration = std::max(0.0f, Acceleration);
	BrakingDeceleration = std::max(0.0f, BrakingDeceleration);
	GravityScale = std::max(0.0f, GravityScale);
	MaxFallSpeed = std::max(0.0f, MaxFallSpeed);
	GroundStickVelocity = std::min(0.0f, GroundStickVelocity);
	GroundProbeDistance = std::max(0.0f, GroundProbeDistance);
}

float UCharacterMovementComponent::MoveToward(float Current, float Target, float MaxDelta) const
{
	if (std::fabs(Target - Current) <= MaxDelta)
	{
		return Target;
	}

	return Current + (Target > Current ? MaxDelta : -MaxDelta);
}
