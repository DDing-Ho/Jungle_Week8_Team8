#pragma once

#include "Component/ActorComponent.h"
#include "Core/CollisionTypes.h"
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
	bool TryGrab(UWorld* World, const FVector& CameraLocation, const FVector& CameraForward);
	void Release();
	void TickHandle(float DeltaTime, const FViewportCamera* Camera);
	void TickHandle(float DeltaTime, const FVector& CameraLocation, const FVector& CameraForward);

	bool IsHolding() const { return HeldBody != nullptr; }
	URigidBodyComponent* GetHeldBody() const { return HeldBody; }

	void SetTraceDistance(float InTraceDistance) { TraceDistance = InTraceDistance; }
	void SetHoldDistance(float InHoldDistance) { HoldDistance = InHoldDistance; }

private:
	URigidBodyComponent* FindRigidBodyFromHit(const FHitResult& Hit) const;
	FVector GetHoldTarget(const FVector& CameraLocation, const FVector& CameraForward) const;
	float ComputeHoldDistanceOffset(URigidBodyComponent* Body, const FVector& CameraLocation, const FVector& CameraForward) const;
	void ClampEditableValues();

private:
	URigidBodyComponent* HeldBody = nullptr;
	UWorld* HeldWorld = nullptr;
	FVector HoldLocation = FVector::ZeroVector;
	FVector HoldVelocity = FVector::ZeroVector;
	FVector LastHoldLocation = FVector::ZeroVector;
	float HoldDistanceOffset = 0.0f;
	float CurrentHoldDistance = 0.0f;

	float TraceDistance = 5.0f;
	float HoldDistance = 1.5f;
	float SizeDistanceScale = 2.0f;
	float MaxSizeDistanceOffset = 5.0f;
	float SpringStrength = 150.0f;
	float Damping = 20.0f;
	float MaxHoldSpeed = 150.0f;
};
