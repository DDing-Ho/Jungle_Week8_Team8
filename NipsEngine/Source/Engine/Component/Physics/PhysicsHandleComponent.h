#pragma once

#include "Component/ActorComponent.h"
#include "Core/CollisionTypes.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

class FViewportCamera;
class URigidBodyComponent;
class UWorld;

class UPhysicsHandleComponent : public UActorComponent
{
public:
	DECLARE_CLASS(UPhysicsHandleComponent, UActorComponent)

	UPhysicsHandleComponent();

	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	bool TryGrab(UWorld* World, const FViewportCamera* Camera);
	void Release();
	void TickHandle(float DeltaTime, const FViewportCamera* Camera, const FVector& TargetOffset = FVector::ZeroVector, const FQuat* TargetRotation = nullptr);

	bool IsHolding() const { return HeldBody != nullptr; }
	URigidBodyComponent* GetHeldBody() const { return HeldBody; }

	void SetTraceDistance(float InTraceDistance) { TraceDistance = InTraceDistance; }
	void SetHoldDistance(float InHoldDistance) { HoldDistance = InHoldDistance; }
	void ResetHoldDistance() { HoldDistance = DefaultHoldDistance; }

private:
	URigidBodyComponent* FindRigidBodyFromHit(const FHitResult& Hit) const;
	FVector GetHoldTarget(const FViewportCamera* Camera, const FVector& TargetOffset) const;
	void ClampEditableValues();

private:
	URigidBodyComponent* HeldBody = nullptr;
	UWorld* HeldWorld = nullptr;
	FVector HoldLocation = FVector::ZeroVector;
	FVector HoldVelocity = FVector::ZeroVector;
	FVector LastHoldLocation = FVector::ZeroVector;

	float TraceDistance = 5.0f;
	float DefaultHoldDistance = 4.0f;
	float HoldDistance = 4.0f;
	float SpringStrength = 70.0f;
	float Damping = 12.0f;
	float MaxHoldSpeed = 30.0f;
};
