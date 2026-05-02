#pragma once

#include "Component/ActorComponent.h"
#include "Math/Vector.h"

class USceneComponent;

class URigidBodyComponent : public UActorComponent
{
public:
	DECLARE_CLASS(URigidBodyComponent, UActorComponent)

	URigidBodyComponent();

	void BeginPlay() override;
	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	void SetUpdatedComponent(USceneComponent* InComponent) { UpdatedComponent = InComponent; }
	USceneComponent* GetUpdatedComponent() const;

	void SetSimulatePhysics(bool bInSimulate) { bSimulatePhysics = bInSimulate; }
	bool IsSimulatingPhysics() const { return bSimulatePhysics; }

	void SetHeldByPhysicsHandle(bool bHeld);
	bool IsHeldByPhysicsHandle() const { return bHeldByPhysicsHandle; }

	bool CanBePickedUp() const { return bCanBePickedUp; }
	void SetCanBePickedUp(bool bInCanBePickedUp) { bCanBePickedUp = bInCanBePickedUp; }

	const FVector& GetVelocity() const { return Velocity; }
	void SetVelocity(const FVector& InVelocity)
	{
		Velocity = InVelocity;
		if (Velocity.Z > 0.0f)
		{
			bGrounded = false;
		}
	}
	const FVector& GetAngularVelocity() const { return AngularVelocity; }
	void SetAngularVelocity(const FVector& InAngularVelocity) { AngularVelocity = InAngularVelocity; }
	void AddImpulse(const FVector& Impulse);
	void NotifyBlockingPushOut(const FVector& PushDelta);

	FVector GetPhysicsLocation() const;
	void SetPhysicsLocation(const FVector& NewLocation);

	void PlayPickupSound() const;
	void PlayDropSound() const;

protected:
	void TickComponent(float DeltaTime) override;

private:
	struct FSupportState
	{
		bool bHasSupport = false;
		bool bStable = true;
		float SnapDeltaZ = 0.0f;
		FVector PivotWorld = FVector::ZeroVector;
		FVector CenterWorld = FVector::ZeroVector;
		FVector Torque = FVector::ZeroVector;
	};

	void ClampEditableValues();
	void ApplyBlockingResponse();
	bool ApplyTipTorque(const FSupportState& Support, float DeltaTime, const FVector* AxisWorld = nullptr);
	void ConstrainAngularVelocityToAxis(const FVector& AxisWorld);
	void ClearTippingState();
	void CacheInitialRotationIfNeeded();
	void ResetRotationToInitial();
	void ApplyAngularMotion(float DeltaTime, bool bAllowSleep, const FVector* PivotWorld = nullptr);
	float ComputeRotationalInertia(const FVector& Axis) const;
	bool HasBlockingContact() const;
	bool HasGroundContact() const;
	bool FindSupportState(float Tolerance, FSupportState& OutSupport) const;

private:
	USceneComponent* UpdatedComponent = nullptr;
	FVector Velocity = FVector::ZeroVector;
	FVector AngularVelocity = FVector::ZeroVector;

	bool bSimulatePhysics = true;
	bool bUseGravity = true;
	bool bCanBePickedUp = true;
	bool bHeldByPhysicsHandle = false;
	bool bWasSimulatingBeforeHold = true;
	bool bGrounded = false;
	bool bGroundPushOutSinceLastTick = false;
	bool bTipping = false;
	bool bHasInitialRelativeRotation = false;
	float StableRestTime = 0.0f;
	float TippingTimeWithoutSupport = 0.0f;
	FVector InitialRelativeRotation = FVector::ZeroVector;
	FVector TippingPivotWorld = FVector::ZeroVector;
	FVector TippingAxisWorld = FVector::ZeroVector;
	bool bDebugSupportStateInitialized = false;
	bool bDebugPrevHasSupport = false;
	bool bDebugPrevStable = true;
	bool bDebugPrevHasTipTorque = false;
	bool bDebugPrevTipping = false;
	float DebugSupportLogTimer = 0.0f;
	mutable float DebugSupportProbeLogTimer = 0.0f;
	USceneComponent* DebugPrevUpdatedComponent = nullptr;

	float Mass = 1.0f;
	float GravityScale = 1.0f;
	float LinearDamping = 1.5f;
	float MaxSpeed = 50.0f;
	float SleepSpeed = 0.03f;
	float AngularDamping = 1.5f;
	float TipTorqueStrength = 1.0f;
	float MaxAngularSpeed = 180.0f;
	float TipOverAngle = 12.0f;
	float TippingSupportGraceTime = 0.25f;

	FString PickupSoundPath;
	FString DropSoundPath;
};
