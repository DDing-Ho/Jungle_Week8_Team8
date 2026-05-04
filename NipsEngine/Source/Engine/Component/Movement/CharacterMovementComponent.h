#pragma once

#include "Component/Movement/MovementComponent.h"
#include "Math/Vector.h"

class URigidBodyComponent;

class UCharacterMovementComponent : public UMovementComponent
{
public:
	DECLARE_CLASS(UCharacterMovementComponent, UMovementComponent)

	UCharacterMovementComponent();

	void BeginPlay() override;
	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

	float GetMaxSpeed() const override { return MaxWalkSpeed; }

	bool IsGrounded() const { return bGrounded; }
	void SetRigidBody(URigidBodyComponent* InRigidBody) { RigidBody = InRigidBody; }

protected:
	void TickComponent(float DeltaTime) override;

private:
	void RefreshUpdatedReferences();
	void ClampEditableValues();
	float MoveToward(float Current, float Target, float MaxDelta) const;

private:
	URigidBodyComponent* RigidBody = nullptr;

	float MaxWalkSpeed = 3.5f;
	float Acceleration = 18.0f;
	float BrakingDeceleration = 24.0f;
	float GravityScale = 1.0f;
	float MaxFallSpeed = 18.0f;
	float GroundStickVelocity = -0.5f;
	bool bGrounded = false;
};
