#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"

class UPrimitiveComponent;
class URigidBodyComponent;
class UWorld;

class FJoltPhysicsSystem
{
public:
	static FJoltPhysicsSystem& Get();

	bool Initialize();
	void Shutdown();

	bool IsInitialized() const { return bInitialized; }
	bool IsCurrentWorld(const UWorld* World) const { return CurrentWorld == World; }
	bool IsBodyManaged(const URigidBodyComponent* Body) const;

	void RebuildWorld(UWorld* World);
	void Step(UWorld* World, float DeltaTime);

	void SetBodyKinematic(URigidBodyComponent* Body);
	void SetBodyDynamic(URigidBodyComponent* Body);
	void SetBodyTransformFromComponent(URigidBodyComponent* Body);
	void SetBodyLinearVelocity(URigidBodyComponent* Body, const FVector& Velocity);
	void AddBodyImpulse(URigidBodyComponent* Body, const FVector& Impulse);

private:
	FJoltPhysicsSystem() = default;
	~FJoltPhysicsSystem();

	FJoltPhysicsSystem(const FJoltPhysicsSystem&) = delete;
	FJoltPhysicsSystem& operator=(const FJoltPhysicsSystem&) = delete;

	void ClearWorld();
	void RegisterStaticBody(UPrimitiveComponent* ShapeComponent);
	void RegisterDynamicBody(URigidBodyComponent* Body);

private:
	struct FImpl;

	FImpl* Impl = nullptr;
	UWorld* CurrentWorld = nullptr;
	bool bInitialized = false;
};
