#include "Component/Physics/RigidBodyComponent.h"

#include "Audio/AudioSystem.h"
#include "Component/Collision/BoxComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Logger.h"
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
	constexpr float AngularSleepSpeed = 0.5f;
	constexpr float GroundedAngularDamping = 18.0f;
	constexpr float RestingAngularStopDelay = 0.18f;
	constexpr float GravityAcceleration = 9.8f;
	constexpr float RadiansToDegrees = 57.29577951308232f;

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

	float ClampFloat(float Value, float MinValue, float MaxValue)
	{
		return std::max(MinValue, std::min(Value, MaxValue));
	}

	const char* GetObjectTypeName(const UObject* Object)
	{
		return Object != nullptr && Object->GetTypeInfo() != nullptr ? Object->GetTypeInfo()->name : "None";
	}

	FString GetObjectNameForLog(const UObject* Object)
	{
		return Object != nullptr ? Object->GetName() : "None";
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

	USceneComponent* CurrentUpdatedComponent = GetUpdatedComponent();
	CacheInitialRotationIfNeeded();
	const FString OwnerName = GetObjectNameForLog(Owner);
	const FString UpdatedName = GetObjectNameForLog(CurrentUpdatedComponent);
	UE_LOG("RigidBodyComponent: BeginPlay owner=%s updated=%s(%s)",
		OwnerName.c_str(),
		UpdatedName.c_str(),
		GetObjectTypeName(CurrentUpdatedComponent));
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
	Ar << "AngularVelocity" << AngularVelocity;
	Ar << "AngularDamping" << AngularDamping;
	Ar << "TipTorqueStrength" << TipTorqueStrength;
	Ar << "MaxAngularSpeed" << MaxAngularSpeed;
	Ar << "TipOverAngle" << TipOverAngle;
	Ar << "TippingSupportGraceTime" << TippingSupportGraceTime;
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
	OutProps.push_back({ "Angular Velocity", EPropertyType::Vec3, &AngularVelocity });
	OutProps.push_back({ "Angular Damping", EPropertyType::Float, &AngularDamping, 0.0f, 50.0f, 0.01f });
	OutProps.push_back({ "Torque Scale", EPropertyType::Float, &TipTorqueStrength, 0.01f, 10.0f, 0.01f });
	OutProps.push_back({ "Max Angular Speed", EPropertyType::Float, &MaxAngularSpeed, 0.0f, 720.0f, 1.0f });
	OutProps.push_back({ "Tipping Support Grace Time", EPropertyType::Float, &TippingSupportGraceTime, 0.0f, 2.0f, 0.01f });
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
		bGrounded = false;
		StableRestTime = 0.0f;
		Velocity = FVector::ZeroVector;
		AngularVelocity = FVector::ZeroVector;
		ClearTippingState();
		ResetRotationToInitial();
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
	StableRestTime = 0.0f;
	ClearTippingState();
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
	FSupportState Support;
	const bool bHasRestingSupport = FindSupportState(GroundSupportTolerance, Support);
	const bool bHasUnstableSupport = bHasRestingSupport && !Support.bStable;
	const bool bHasStableSupport = bHasRestingSupport && Support.bStable;
	if (bHasUnstableSupport)
	{
		StableRestTime = 0.0f;
		const FVector CandidateAxis = Support.Torque.GetSafeNormal();
		if (!bTipping && !CandidateAxis.IsNearlyZero())
		{
			bTipping = true;
			TippingPivotWorld = Support.PivotWorld;
			TippingAxisWorld = CandidateAxis;
			ConstrainAngularVelocityToAxis(TippingAxisWorld);
		}
		TippingTimeWithoutSupport = 0.0f;
	}
	else if (bTipping)
	{
		StableRestTime = 0.0f;
		TippingTimeWithoutSupport += DeltaTime;
		if (bHasRestingSupport && Support.bStable)
		{
			ClearTippingState();
		}
		else if (TippingTimeWithoutSupport > TippingSupportGraceTime)
		{
			ClearTippingState();
		}
	}
	else if (!bHasStableSupport)
	{
		StableRestTime = 0.0f;
	}

	bGrounded = bHasStableSupport;
	if (bGrounded && Velocity.Z < 0.0f)
	{
		Velocity.Z = 0.0f;
	}

	if (bGrounded && std::fabs(Support.SnapDeltaZ) > 0.0001f)
	{
		if (AActor* OwnerActor = Owner)
		{
			OwnerActor->AddActorWorldOffset(FVector(0.0f, 0.0f, Support.SnapDeltaZ));
		}
	}

	const bool bHasTipTorque = bHasUnstableSupport && bTipping && !TippingAxisWorld.IsNearlyZero() && ApplyTipTorque(Support, DeltaTime, &TippingAxisWorld);
	if (bTipping && !TippingAxisWorld.IsNearlyZero())
	{
		ConstrainAngularVelocityToAxis(TippingAxisWorld);
	}
	if (bGrounded && AngularVelocity.SizeSquared() > 0.0f)
	{
		const float ContactDampingScale = std::max(0.0f, 1.0f - GroundedAngularDamping * DeltaTime);
		AngularVelocity *= ContactDampingScale;
		if (AngularVelocity.SizeSquared() <= AngularSleepSpeed * AngularSleepSpeed)
		{
			AngularVelocity = FVector::ZeroVector;
			ClearTippingState();
		}
	}

	if (!bGrounded || AngularVelocity.SizeSquared() > 0.0f)
	{
		const FVector* AngularPivot = nullptr;
		if (bHasUnstableSupport && bTipping)
		{
			AngularPivot = &TippingPivotWorld;
		}
		ApplyAngularMotion(DeltaTime, bGrounded, AngularPivot);
	}

	const bool bCanStopRestingAngularMotion =
		bGrounded &&
		!bTipping &&
		Velocity.SizeSquared() <= SleepSpeed * SleepSpeed;
	if (bCanStopRestingAngularMotion)
	{
		StableRestTime += DeltaTime;
		if (StableRestTime >= RestingAngularStopDelay)
		{
			Velocity = FVector::ZeroVector;
			AngularVelocity = FVector::ZeroVector;
			ClearTippingState();
			StableRestTime = 0.0f;
		}
	}
	else
	{
		StableRestTime = 0.0f;
	}

	if (bGrounded &&
		Velocity.SizeSquared() <= SleepSpeed * SleepSpeed &&
		AngularVelocity.SizeSquared() <= AngularSleepSpeed * AngularSleepSpeed)
	{
		Velocity = FVector::ZeroVector;
		AngularVelocity = FVector::ZeroVector;
		ClearTippingState();
	}

	USceneComponent* CurrentUpdatedComponent = GetUpdatedComponent();
	DebugSupportLogTimer += DeltaTime;
	const bool bShouldLogSupportState =
		!bDebugSupportStateInitialized ||
		DebugPrevUpdatedComponent != CurrentUpdatedComponent ||
		bDebugPrevHasSupport != bHasRestingSupport ||
		bDebugPrevStable != Support.bStable ||
		bDebugPrevHasTipTorque != bHasTipTorque ||
		bDebugPrevTipping != bTipping ||
		DebugSupportLogTimer >= 1.0f;
	if (bShouldLogSupportState)
	{
		const FString OwnerName = GetObjectNameForLog(Owner);
		const FString UpdatedName = GetObjectNameForLog(CurrentUpdatedComponent);
		const FVector Rotation = CurrentUpdatedComponent ? CurrentUpdatedComponent->GetRelativeRotation() : FVector::ZeroVector;
		UE_LOG(
			"RigidBodyComponent: support owner=%s updated=%s(%s) hasSupport=%d stable=%d tipping=%d hasTorque=%d wasGrounded=%d pushed=%d groundContact=%d torqueScale=%.3f maxAngular=%.3f grace=%.3f rotation=(%.3f, %.3f, %.3f) center=(%.3f, %.3f, %.3f) pivot=(%.3f, %.3f, %.3f) tipAxis=(%.3f, %.3f, %.3f) torque=(%.3f, %.3f, %.3f) angularVelocity=(%.3f, %.3f, %.3f)",
			OwnerName.c_str(),
			UpdatedName.c_str(),
			GetObjectTypeName(CurrentUpdatedComponent),
			bHasRestingSupport ? 1 : 0,
			Support.bStable ? 1 : 0,
			bTipping ? 1 : 0,
			bHasTipTorque ? 1 : 0,
			bWasGrounded ? 1 : 0,
			bWasPushedOntoGround ? 1 : 0,
			bHasGroundContact ? 1 : 0,
			TipTorqueStrength,
			MaxAngularSpeed,
			TippingTimeWithoutSupport,
			Rotation.X,
			Rotation.Y,
			Rotation.Z,
			Support.CenterWorld.X,
			Support.CenterWorld.Y,
			Support.CenterWorld.Z,
			Support.PivotWorld.X,
			Support.PivotWorld.Y,
			Support.PivotWorld.Z,
			TippingAxisWorld.X,
			TippingAxisWorld.Y,
			TippingAxisWorld.Z,
			Support.Torque.X,
			Support.Torque.Y,
			Support.Torque.Z,
			AngularVelocity.X,
			AngularVelocity.Y,
			AngularVelocity.Z);

		bDebugSupportStateInitialized = true;
		DebugPrevUpdatedComponent = CurrentUpdatedComponent;
		bDebugPrevHasSupport = bHasRestingSupport;
		bDebugPrevStable = Support.bStable;
		bDebugPrevHasTipTorque = bHasTipTorque;
		bDebugPrevTipping = bTipping;
		DebugSupportLogTimer = 0.0f;
	}

	if (bHasUnstableSupport && Velocity.Z < 0.0f)
	{
		Velocity.Z = 0.0f;
	}

	if (bUseGravity && !bHasRestingSupport && !(bGrounded && Velocity.Z <= 0.0f))
	{
		Velocity += FVector(0.0f, 0.0f, -GravityAcceleration * GravityScale) * DeltaTime;
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

	if (bGrounded &&
		Velocity.SizeSquared() <= SleepSpeed * SleepSpeed &&
		AngularVelocity.SizeSquared() <= AngularSleepSpeed * AngularSleepSpeed)
	{
		Velocity = FVector::ZeroVector;
		AngularVelocity = FVector::ZeroVector;
		return;
	}

	if (USceneComponent* Scene = GetUpdatedComponent())
	{
		FVector FrameMove = Velocity * DeltaTime;
		if (bHasUnstableSupport)
		{
			FrameMove.Z = 0.0f;
		}
		if (!FrameMove.IsNearlyZero())
		{
			Scene->AddWorldOffset(FrameMove);
		}
	}

	if (Velocity.Z > SleepSpeed)
	{
		bGrounded = false;
		StableRestTime = 0.0f;
	}
}

void URigidBodyComponent::ClampEditableValues()
{
	Mass = std::max(0.01f, Mass);
	GravityScale = std::max(0.0f, GravityScale);
	LinearDamping = std::max(0.0f, LinearDamping);
	MaxSpeed = std::max(0.0f, MaxSpeed);
	SleepSpeed = std::max(0.0f, SleepSpeed);
	AngularDamping = std::max(0.0f, AngularDamping);
	if (TipTorqueStrength <= 0.0f || TipTorqueStrength > 50.0f)
	{
		TipTorqueStrength = 1.0f;
	}
	TipTorqueStrength = ClampFloat(TipTorqueStrength, 0.01f, 10.0f);
	MaxAngularSpeed = std::max(0.0f, MaxAngularSpeed);
	TipOverAngle = std::max(1.0f, TipOverAngle);
	TippingSupportGraceTime = std::max(0.0f, TippingSupportGraceTime);
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

float URigidBodyComponent::ComputeRotationalInertia(const FVector& Axis) const
{
	const FVector SafeAxis = Axis.GetSafeNormal();
	if (SafeAxis.IsNearlyZero())
	{
		return Mass;
	}

	FVector HalfExtent(0.5f, 0.5f, 0.5f);
	if (Owner != nullptr)
	{
		for (UPrimitiveComponent* Primitive : Owner->GetPrimitiveComponents())
		{
			if (UBoxComponent* Box = Cast<UBoxComponent>(Primitive))
			{
				const FVector Scale = Box->GetWorldScale();
				const FVector Extent = Box->GetBoxExtent();
				HalfExtent = FVector(
					std::fabs(Extent.X * Scale.X),
					std::fabs(Extent.Y * Scale.Y),
					std::fabs(Extent.Z * Scale.Z));
				break;
			}
		}
	}

	const FVector Size = HalfExtent * 2.0f;
	const float Ixx = Mass * (Size.Y * Size.Y + Size.Z * Size.Z) / 12.0f;
	const float Iyy = Mass * (Size.X * Size.X + Size.Z * Size.Z) / 12.0f;
	const float Izz = Mass * (Size.X * Size.X + Size.Y * Size.Y) / 12.0f;
	const float Inertia =
		SafeAxis.X * SafeAxis.X * Ixx +
		SafeAxis.Y * SafeAxis.Y * Iyy +
		SafeAxis.Z * SafeAxis.Z * Izz;
	return std::max(0.001f, Inertia);
}

bool URigidBodyComponent::ApplyTipTorque(const FSupportState& Support, float DeltaTime, const FVector* AxisWorld)
{
	if (DeltaTime <= 0.0f ||
		TipTorqueStrength <= 0.0f ||
		Support.Torque.SizeSquared() <= GroundSupportHorizontalSlop * GroundSupportHorizontalSlop)
	{
		return false;
	}

	const FVector Axis = AxisWorld != nullptr ? AxisWorld->GetSafeNormal() : Support.Torque.GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		return false;
	}

	float TorqueAlongAxis = FVector::DotProduct(Support.Torque, Axis);
	if (std::fabs(TorqueAlongAxis) <= GroundSupportHorizontalSlop)
	{
		return false;
	}

	const float Inertia = ComputeRotationalInertia(Axis);
	const FVector AngularAccelerationDeg = Axis * (TorqueAlongAxis * RadiansToDegrees * TipTorqueStrength / Inertia);
	AngularVelocity += AngularAccelerationDeg * DeltaTime;
	return true;
}

void URigidBodyComponent::ConstrainAngularVelocityToAxis(const FVector& AxisWorld)
{
	const FVector Axis = AxisWorld.GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		return;
	}

	const float SpeedAlongAxis = FVector::DotProduct(AngularVelocity, Axis);
	AngularVelocity = Axis * SpeedAlongAxis;
}

void URigidBodyComponent::ClearTippingState()
{
	bTipping = false;
	TippingTimeWithoutSupport = 0.0f;
	TippingPivotWorld = FVector::ZeroVector;
	TippingAxisWorld = FVector::ZeroVector;
}

void URigidBodyComponent::CacheInitialRotationIfNeeded()
{
	if (bHasInitialRelativeRotation)
	{
		return;
	}

	if (const USceneComponent* Scene = GetUpdatedComponent())
	{
		InitialRelativeRotation = Scene->GetRelativeRotation();
		bHasInitialRelativeRotation = true;
	}
}

void URigidBodyComponent::ResetRotationToInitial()
{
	CacheInitialRotationIfNeeded();

	if (USceneComponent* Scene = GetUpdatedComponent())
	{
		Scene->SetRelativeRotation(InitialRelativeRotation);
	}
}

void URigidBodyComponent::ApplyAngularMotion(float DeltaTime, bool bAllowSleep, const FVector* PivotWorld)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	if (AngularDamping > 0.0f)
	{
		const float DampingScale = std::max(0.0f, 1.0f - AngularDamping * DeltaTime);
		AngularVelocity *= DampingScale;
	}

	if (MaxAngularSpeed > 0.0f && AngularVelocity.SizeSquared() > MaxAngularSpeed * MaxAngularSpeed)
	{
		AngularVelocity = AngularVelocity.GetSafeNormal() * MaxAngularSpeed;
	}

	if (bAllowSleep && AngularVelocity.SizeSquared() <= AngularSleepSpeed * AngularSleepSpeed)
	{
		AngularVelocity = FVector::ZeroVector;
		return;
	}

	if (USceneComponent* Scene = GetUpdatedComponent())
	{
		const float AngularSpeed = AngularVelocity.Size();
		if (AngularSpeed <= 1.e-6f)
		{
			return;
		}

		FVector LocalPivot = FVector::ZeroVector;
		FVector PivotBefore = FVector::ZeroVector;
		const bool bUsePivot = PivotWorld != nullptr;
		if (bUsePivot)
		{
			PivotBefore = *PivotWorld;
			LocalPivot = Scene->GetWorldTransform().InverseTransformPosition(PivotBefore);
		}

		const FVector Axis = AngularVelocity.GetSafeNormal();
		const float DeltaAngleRad = AngularSpeed * DeltaTime * (3.14159265358979323846f / 180.0f);
		FQuat DeltaQuat(Axis, DeltaAngleRad);
		FQuat ResultQuat = DeltaQuat * Scene->GetRelativeQuat();
		ResultQuat.Normalize();
		Scene->SetRelativeRotationQuat(ResultQuat);

		if (bUsePivot)
		{
			const FVector PivotAfter = Scene->GetWorldTransform().TransformPosition(LocalPivot);
			Scene->AddWorldOffset(PivotBefore - PivotAfter);
		}
	}
}

bool URigidBodyComponent::FindSupportState(float Tolerance, FSupportState& OutSupport) const
{
	OutSupport = FSupportState{};
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
	int32 OwnerBlockCount = 0;
	int32 OtherBlockCount = 0;
	int32 XYOverlapCount = 0;
	float ClosestAbsDeltaZ = std::numeric_limits<float>::max();
	FAABB DebugOwnBounds;
	FAABB DebugOtherBounds;
	bool bHasDebugPair = false;

	for (UPrimitiveComponent* Primitive : Owner->GetPrimitiveComponents())
	{
		if (Primitive == nullptr || !Primitive->IsBlockComponent())
		{
			continue;
		}

		++OwnerBlockCount;
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

				++OtherBlockCount;
				const FAABB OtherBounds = OtherPrimitive->GetWorldAABB();
				const bool bOverlapsX =
					OwnBounds.Max.X > OtherBounds.Min.X + GroundSupportHorizontalSlop &&
					OwnBounds.Min.X < OtherBounds.Max.X - GroundSupportHorizontalSlop;
				const bool bOverlapsY =
					OwnBounds.Max.Y > OtherBounds.Min.Y + GroundSupportHorizontalSlop &&
					OwnBounds.Min.Y < OtherBounds.Max.Y - GroundSupportHorizontalSlop;
				if (!bOverlapsX || !bOverlapsY)
				{
					continue;
				}

				++XYOverlapCount;
				const float BottomZ = OwnBounds.Min.Z;
				const float SupportTopZ = OtherBounds.Max.Z;
				const float SnapDeltaZ = SupportTopZ - BottomZ;
				const float AbsDeltaZ = std::fabs(SnapDeltaZ);
				if (AbsDeltaZ < ClosestAbsDeltaZ)
				{
					ClosestAbsDeltaZ = AbsDeltaZ;
					DebugOwnBounds = OwnBounds;
					DebugOtherBounds = OtherBounds;
					bHasDebugPair = true;
				}
				if (AbsDeltaZ <= Tolerance && AbsDeltaZ < BestAbsDeltaZ)
				{
					const FVector OwnCenter = OwnBounds.GetCenter();
					const float SupportMinX = std::max(OwnBounds.Min.X, OtherBounds.Min.X);
					const float SupportMaxX = std::min(OwnBounds.Max.X, OtherBounds.Max.X);
					const float SupportMinY = std::max(OwnBounds.Min.Y, OtherBounds.Min.Y);
					const float SupportMaxY = std::min(OwnBounds.Max.Y, OtherBounds.Max.Y);
					const bool bCenterSupported =
						OwnCenter.X >= SupportMinX - GroundSupportHorizontalSlop &&
						OwnCenter.X <= SupportMaxX + GroundSupportHorizontalSlop &&
						OwnCenter.Y >= SupportMinY - GroundSupportHorizontalSlop &&
						OwnCenter.Y <= SupportMaxY + GroundSupportHorizontalSlop;

					const FVector PivotWorld(
						ClampFloat(OwnCenter.X, SupportMinX, SupportMaxX),
						ClampFloat(OwnCenter.Y, SupportMinY, SupportMaxY),
						SupportTopZ);
					const FVector Arm = OwnCenter - PivotWorld;
					const FVector GravityForce(0.0f, 0.0f, -GravityAcceleration * Mass * GravityScale);

					BestAbsDeltaZ = AbsDeltaZ;
					OutSupport.bHasSupport = true;
					OutSupport.bStable = bCenterSupported;
					OutSupport.SnapDeltaZ = SnapDeltaZ;
					OutSupport.PivotWorld = PivotWorld;
					OutSupport.CenterWorld = OwnCenter;
					OutSupport.Torque = bCenterSupported ? FVector::ZeroVector : FVector::CrossProduct(Arm, GravityForce);
					bFoundSupport = true;
				}
			}
		}
	}

	DebugSupportProbeLogTimer += 1.0f / 60.0f;
	if (!bFoundSupport && DebugSupportProbeLogTimer >= 1.0f)
	{
		const FString OwnerName = GetObjectNameForLog(Owner);
		const char* Reason = "ZTooFar";
		if (OwnerBlockCount == 0)
		{
			Reason = "NoOwnerBlock";
		}
		else if (OtherBlockCount == 0)
		{
			Reason = "NoOtherBlock";
		}
		else if (XYOverlapCount == 0)
		{
			Reason = "NoXYOverlap";
		}

		if (bHasDebugPair)
		{
			UE_LOG(
				"RigidBodyComponent: support probe failed owner=%s reason=%s ownerBlocks=%d otherBlocks=%d xyPairs=%d closestZ=%.3f tolerance=%.3f ownMin=(%.3f, %.3f, %.3f) ownMax=(%.3f, %.3f, %.3f) otherMin=(%.3f, %.3f, %.3f) otherMax=(%.3f, %.3f, %.3f)",
				OwnerName.c_str(),
				Reason,
				OwnerBlockCount,
				OtherBlockCount,
				XYOverlapCount,
				ClosestAbsDeltaZ,
				Tolerance,
				DebugOwnBounds.Min.X,
				DebugOwnBounds.Min.Y,
				DebugOwnBounds.Min.Z,
				DebugOwnBounds.Max.X,
				DebugOwnBounds.Max.Y,
				DebugOwnBounds.Max.Z,
				DebugOtherBounds.Min.X,
				DebugOtherBounds.Min.Y,
				DebugOtherBounds.Min.Z,
				DebugOtherBounds.Max.X,
				DebugOtherBounds.Max.Y,
				DebugOtherBounds.Max.Z);
		}
		else
		{
			UE_LOG(
				"RigidBodyComponent: support probe failed owner=%s reason=%s ownerBlocks=%d otherBlocks=%d xyPairs=%d",
				OwnerName.c_str(),
				Reason,
				OwnerBlockCount,
				OtherBlockCount,
				XYOverlapCount);
		}

		DebugSupportProbeLogTimer = 0.0f;
	}
	else if (bFoundSupport)
	{
		DebugSupportProbeLogTimer = 0.0f;
	}

	return bFoundSupport;
}
