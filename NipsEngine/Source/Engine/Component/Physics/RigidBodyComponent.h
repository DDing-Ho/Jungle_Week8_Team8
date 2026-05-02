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
	void AddImpulse(const FVector& Impulse);
	void NotifyBlockingPushOut(const FVector& PushDelta);

	FVector GetPhysicsLocation() const;
	void SetPhysicsLocation(const FVector& NewLocation);

	void PlayPickupSound() const;
	void PlayDropSound() const;

protected:
	void TickComponent(float DeltaTime) override;

private:
	void ClampEditableValues();
	void ApplyBlockingResponse();
	bool HasBlockingContact() const;
	bool HasGroundContact() const;
	bool HasRestingSupport(float Tolerance) const;

private:
	USceneComponent* UpdatedComponent = nullptr;
	FVector Velocity = FVector::ZeroVector;

	bool bSimulatePhysics = true;
	bool bUseGravity = true;
	bool bCanBePickedUp = true;
	bool bHeldByPhysicsHandle = false;
	bool bWasSimulatingBeforeHold = true;
	bool bGrounded = false;
	bool bGroundPushOutSinceLastTick = false;

	float Mass = 1.0f;
	float GravityScale = 1.0f;
	float LinearDamping = 1.5f;
	float MaxSpeed = 50.0f;
	float SleepSpeed = 0.03f;

	FString PickupSoundPath;
	FString DropSoundPath;
};
