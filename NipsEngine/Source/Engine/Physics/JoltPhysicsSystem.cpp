#include "Physics/JoltPhysicsSystem.h"

#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/CapsuleComponent.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/Collision/SphereComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Logger.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <thread>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace
{
	namespace ObjectLayers
	{
		constexpr JPH::ObjectLayer NonMoving = 0;
		constexpr JPH::ObjectLayer Moving = 1;
		constexpr JPH::ObjectLayer Count = 2;
	}

	namespace BroadPhaseLayers
	{
		const JPH::BroadPhaseLayer NonMoving(0);
		const JPH::BroadPhaseLayer Moving(1);
		constexpr JPH::uint Count = 2;
	}

	class FBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
	{
	public:
		JPH::uint GetNumBroadPhaseLayers() const override
		{
			return BroadPhaseLayers::Count;
		}

		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer InLayer) const override
		{
			switch (InLayer)
			{
			case ObjectLayers::Moving:
				return BroadPhaseLayers::Moving;
			case ObjectLayers::NonMoving:
			default:
				return BroadPhaseLayers::NonMoving;
			}
		}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer InLayer) const override
		{
			if (InLayer == BroadPhaseLayers::Moving)
			{
				return "Moving";
			}
			if (InLayer == BroadPhaseLayers::NonMoving)
			{
				return "NonMoving";
			}
			return "Invalid";
		}
#endif
	};

	class FObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer InLayer, JPH::BroadPhaseLayer InBroadPhaseLayer) const override
		{
			switch (InLayer)
			{
			case ObjectLayers::Moving:
				return true;
			case ObjectLayers::NonMoving:
				return InBroadPhaseLayer == BroadPhaseLayers::Moving;
			default:
				return false;
			}
		}
	};

	class FObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer InLayerA, JPH::ObjectLayer InLayerB) const override
		{
			if (InLayerA == ObjectLayers::NonMoving && InLayerB == ObjectLayers::NonMoving)
			{
				return false;
			}
			return true;
		}
	};

	JPH::Vec3 ToJoltVector(const FVector& Vector)
	{
		return JPH::Vec3(Vector.X, Vector.Y, Vector.Z);
	}

	JPH::RVec3 ToJoltPosition(const FVector& Vector)
	{
		return JPH::RVec3(Vector.X, Vector.Y, Vector.Z);
	}

	FVector ToEngineVector(const JPH::Vec3& Vector)
	{
		return FVector(Vector.GetX(), Vector.GetY(), Vector.GetZ());
	}

	JPH::Quat ToJoltQuat(const FQuat& Quat)
	{
		return JPH::Quat(Quat.X, Quat.Y, Quat.Z, Quat.W).Normalized();
	}

	FQuat GetWorldQuat(const USceneComponent* Scene)
	{
		return Scene != nullptr ? Scene->GetWorldTransform().GetRotation() : FQuat::Identity;
	}

	FQuat ToEngineQuat(const JPH::Quat& Quat)
	{
		return FQuat(Quat.GetX(), Quat.GetY(), Quat.GetZ(), Quat.GetW()).GetNormalized();
	}

	float SafePositive(float Value, float Fallback)
	{
		return Value > 0.001f ? Value : Fallback;
	}

	UShapeComponent* FindFirstBlockingShape(AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}

		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			UShapeComponent* Shape = Cast<UShapeComponent>(Primitive);
			if (Shape != nullptr && Shape->IsBlockComponent())
			{
				return Shape;
			}
		}

		return nullptr;
	}

	URigidBodyComponent* FindRigidBody(AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component);
			if (Body != nullptr)
			{
				return Body;
			}
		}

		return nullptr;
	}

	JPH::ShapeRefC CreateJoltShape(UPrimitiveComponent* Primitive)
	{
		if (UBoxComponent* Box = Cast<UBoxComponent>(Primitive))
		{
			const FVector Extent = Box->GetBoxExtent();
			return new JPH::BoxShape(
				JPH::Vec3(
					SafePositive(std::fabs(Extent.X), 0.05f),
					SafePositive(std::fabs(Extent.Y), 0.05f),
					SafePositive(std::fabs(Extent.Z), 0.05f)),
				0.01f);
		}

		if (USphereComponent* Sphere = Cast<USphereComponent>(Primitive))
		{
			return new JPH::SphereShape(SafePositive(std::fabs(Sphere->GetSphereRadius()), 0.05f));
		}

		if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Primitive))
		{
			const float Radius = SafePositive(std::fabs(Capsule->GetCapsuleRadius()), 0.05f);
			const float HalfCylinderHeight = std::max(0.01f, std::fabs(Capsule->GetCapsuleHalfHeight()) - Radius);
			return new JPH::CapsuleShape(HalfCylinderHeight, Radius);
		}

		return nullptr;
	}

	JPH::BodyID MakeBodyID(uint32 RawBodyID)
	{
		return JPH::BodyID(RawBodyID);
	}

	void JoltTrace(const char* Format, ...)
	{
		if (Format == nullptr)
		{
			return;
		}

		char Buffer[2048] = {};
		va_list Args;
		va_start(Args, Format);
		vsnprintf(Buffer, sizeof(Buffer), Format, Args);
		va_end(Args);

		UE_LOG("[Jolt] %s", Buffer);
	}

#ifdef JPH_ENABLE_ASSERTS
	bool JoltAssertFailed(const char* Expression, const char* Message, const char* File, JPH::uint Line)
	{
		UE_LOG("[Jolt Assert] %s | %s (%s:%u)",
			Expression != nullptr ? Expression : "",
			Message != nullptr ? Message : "",
			File != nullptr ? File : "",
			static_cast<unsigned int>(Line));
		return false;
	}
#endif
}

struct FJoltPhysicsSystem::FImpl
{
	JPH::PhysicsSystem PhysicsSystem;
	FBroadPhaseLayerInterface BroadPhaseLayerInterface;
	FObjectVsBroadPhaseLayerFilter ObjectVsBroadPhaseLayerFilter;
	FObjectLayerPairFilter ObjectLayerPairFilter;

	JPH::TempAllocatorImpl* TempAllocator = nullptr;
	JPH::JobSystemThreadPool* JobSystem = nullptr;

	std::vector<JPH::BodyID> BodyIDs;
	std::unordered_map<URigidBodyComponent*, JPH::BodyID> DynamicBodies;
};

FJoltPhysicsSystem& FJoltPhysicsSystem::Get()
{
	static FJoltPhysicsSystem Instance;
	return Instance;
}

FJoltPhysicsSystem::~FJoltPhysicsSystem()
{
	Shutdown();
}

bool FJoltPhysicsSystem::Initialize()
{
	if (bInitialized)
	{
		return true;
	}

	JPH::Trace = JoltTrace;
#ifdef JPH_ENABLE_ASSERTS
	JPH::AssertFailed = JoltAssertFailed;
#endif

	JPH::RegisterDefaultAllocator();
	if (JPH::Factory::sInstance == nullptr)
	{
		JPH::Factory::sInstance = new JPH::Factory();
	}
	JPH::RegisterTypes();

	Impl = new FImpl();
	Impl->TempAllocator = new JPH::TempAllocatorImpl(16 * 1024 * 1024);
	const int ThreadCount = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
	Impl->JobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, ThreadCount);

	constexpr JPH::uint MaxBodies = 4096;
	constexpr JPH::uint NumBodyMutexes = 0;
	constexpr JPH::uint MaxBodyPairs = 8192;
	constexpr JPH::uint MaxContactConstraints = 8192;
	Impl->PhysicsSystem.Init(
		MaxBodies,
		NumBodyMutexes,
		MaxBodyPairs,
		MaxContactConstraints,
		Impl->BroadPhaseLayerInterface,
		Impl->ObjectVsBroadPhaseLayerFilter,
		Impl->ObjectLayerPairFilter);
	Impl->PhysicsSystem.SetGravity(JPH::Vec3(0.0f, 0.0f, -9.8f));

	bInitialized = true;
	UE_LOG("JoltPhysicsSystem: initialized.");
	return true;
}

void FJoltPhysicsSystem::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	ClearWorld();

	delete Impl->JobSystem;
	Impl->JobSystem = nullptr;
	delete Impl->TempAllocator;
	Impl->TempAllocator = nullptr;
	delete Impl;
	Impl = nullptr;

	JPH::UnregisterTypes();
	delete JPH::Factory::sInstance;
	JPH::Factory::sInstance = nullptr;

	CurrentWorld = nullptr;
	bInitialized = false;
	UE_LOG("JoltPhysicsSystem: shutdown.");
}

bool FJoltPhysicsSystem::IsBodyManaged(const URigidBodyComponent* Body) const
{
	if (!bInitialized || Body == nullptr)
	{
		return false;
	}

	return Impl->DynamicBodies.find(const_cast<URigidBodyComponent*>(Body)) != Impl->DynamicBodies.end();
}

void FJoltPhysicsSystem::ClearWorld()
{
	if (Impl == nullptr)
	{
		return;
	}

	JPH::BodyInterface& BodyInterface = Impl->PhysicsSystem.GetBodyInterface();
	for (JPH::BodyID BodyID : Impl->BodyIDs)
	{
		if (!BodyID.IsInvalid())
		{
			BodyInterface.RemoveBody(BodyID);
			BodyInterface.DestroyBody(BodyID);
		}
	}

	for (auto& Pair : Impl->DynamicBodies)
	{
		if (Pair.first != nullptr)
		{
			Pair.first->ClearJoltBodyHandle();
		}
	}

	Impl->BodyIDs.clear();
	Impl->DynamicBodies.clear();
}

void FJoltPhysicsSystem::RebuildWorld(UWorld* World)
{
	if (World == nullptr || !Initialize())
	{
		return;
	}

	ClearWorld();
	CurrentWorld = World;

	for (AActor* Actor : World->GetActors())
	{
		if (Actor == nullptr || !Actor->IsActive())
		{
			continue;
		}

		URigidBodyComponent* Body = FindRigidBody(Actor);
		if (Body != nullptr && Body->IsSimulatingPhysics() && FindFirstBlockingShape(Actor) != nullptr)
		{
			RegisterDynamicBody(Body);
			continue;
		}

		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (Cast<UShapeComponent>(Primitive) != nullptr && Primitive->IsBlockComponent())
			{
				RegisterStaticBody(Primitive);
			}
		}
	}

	Impl->PhysicsSystem.OptimizeBroadPhase();
}

void FJoltPhysicsSystem::RegisterStaticBody(UPrimitiveComponent* ShapeComponent)
{
	if (Impl == nullptr || ShapeComponent == nullptr)
	{
		return;
	}

	JPH::ShapeRefC Shape = CreateJoltShape(ShapeComponent);
	if (Shape == nullptr)
	{
		return;
	}

	JPH::BodyCreationSettings Settings(
		Shape,
		ToJoltPosition(ShapeComponent->GetWorldLocation()),
		ToJoltQuat(GetWorldQuat(ShapeComponent)),
		JPH::EMotionType::Static,
		ObjectLayers::NonMoving);
	Settings.mFriction = 0.8f;
	Settings.mRestitution = 0.05f;

	JPH::BodyID BodyID = Impl->PhysicsSystem.GetBodyInterface().CreateAndAddBody(Settings, JPH::EActivation::DontActivate);
	if (!BodyID.IsInvalid())
	{
		Impl->BodyIDs.push_back(BodyID);
	}
}

void FJoltPhysicsSystem::RegisterDynamicBody(URigidBodyComponent* Body)
{
	if (Impl == nullptr || Body == nullptr || Body->GetOwner() == nullptr)
	{
		return;
	}

	UShapeComponent* ShapeComponent = FindFirstBlockingShape(Body->GetOwner());
	USceneComponent* UpdatedComponent = Body->GetUpdatedComponent();
	if (ShapeComponent == nullptr || UpdatedComponent == nullptr)
	{
		return;
	}

	JPH::ShapeRefC Shape = CreateJoltShape(ShapeComponent);
	if (Shape == nullptr)
	{
		return;
	}

	const JPH::EMotionType MotionType = Body->IsHeldByPhysicsHandle() ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
	JPH::BodyCreationSettings Settings(
		Shape,
		ToJoltPosition(UpdatedComponent->GetWorldLocation()),
		ToJoltQuat(GetWorldQuat(UpdatedComponent)),
		MotionType,
		ObjectLayers::Moving);
	Settings.mFriction = 0.85f;
	Settings.mRestitution = 0.05f;
	Settings.mLinearDamping = Body->GetLinearDamping();
	Settings.mAngularDamping = Body->GetAngularDamping();
	Settings.mMaxLinearVelocity = Body->GetMaxSpeed() > 0.0f ? Body->GetMaxSpeed() : 500.0f;
	Settings.mMaxAngularVelocity = Body->GetMaxAngularSpeed() > 0.0f ? Body->GetMaxAngularSpeed() * (3.1415926535f / 180.0f) : 60.0f;
	Settings.mGravityFactor = Body->IsGravityEnabled() ? Body->GetGravityScale() : 0.0f;
	Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
	Settings.mMassPropertiesOverride.mMass = Body->GetMass();

	JPH::BodyID BodyID = Impl->PhysicsSystem.GetBodyInterface().CreateAndAddBody(Settings, JPH::EActivation::Activate);
	if (BodyID.IsInvalid())
	{
		return;
	}

	Body->SetJoltBodyHandle(BodyID.GetIndexAndSequenceNumber());
	Impl->BodyIDs.push_back(BodyID);
	Impl->DynamicBodies[Body] = BodyID;
}

void FJoltPhysicsSystem::Step(UWorld* World, float DeltaTime)
{
	if (!bInitialized || Impl == nullptr || World == nullptr || World != CurrentWorld || DeltaTime <= 0.0f)
	{
		return;
	}

	const float ClampedDeltaTime = std::min(DeltaTime, 1.0f / 15.0f);
	const int CollisionSteps = std::max(1, static_cast<int>(std::ceil(ClampedDeltaTime / (1.0f / 60.0f))));
	Impl->PhysicsSystem.Update(ClampedDeltaTime, CollisionSteps, Impl->TempAllocator, Impl->JobSystem);

	JPH::BodyInterface& BodyInterface = Impl->PhysicsSystem.GetBodyInterface();
	for (auto& Pair : Impl->DynamicBodies)
	{
		URigidBodyComponent* Body = Pair.first;
		if (Body == nullptr)
		{
			continue;
		}

		USceneComponent* UpdatedComponent = Body->GetUpdatedComponent();
		if (UpdatedComponent == nullptr)
		{
			continue;
		}

		JPH::RVec3 Position;
		JPH::Quat Rotation;
		BodyInterface.GetPositionAndRotation(Pair.second, Position, Rotation);
		UpdatedComponent->SetWorldLocation(ToEngineVector(Position));
		UpdatedComponent->SetRelativeRotationQuat(ToEngineQuat(Rotation));

		JPH::Vec3 LinearVelocity;
		JPH::Vec3 AngularVelocity;
		BodyInterface.GetLinearAndAngularVelocity(Pair.second, LinearVelocity, AngularVelocity);
		Body->SetVelocity(ToEngineVector(LinearVelocity));
		Body->SetAngularVelocity(ToEngineVector(AngularVelocity));
	}
}

void FJoltPhysicsSystem::SetBodyKinematic(URigidBodyComponent* Body)
{
	if (!IsBodyManaged(Body))
	{
		return;
	}

	JPH::BodyInterface& BodyInterface = Impl->PhysicsSystem.GetBodyInterface();
	BodyInterface.SetMotionType(Impl->DynamicBodies[Body], JPH::EMotionType::Kinematic, JPH::EActivation::Activate);
	BodyInterface.SetLinearVelocity(Impl->DynamicBodies[Body], JPH::Vec3::sZero());
}

void FJoltPhysicsSystem::SetBodyDynamic(URigidBodyComponent* Body)
{
	if (!IsBodyManaged(Body))
	{
		return;
	}

	JPH::BodyInterface& BodyInterface = Impl->PhysicsSystem.GetBodyInterface();
	BodyInterface.SetMotionType(Impl->DynamicBodies[Body], JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
}

void FJoltPhysicsSystem::SetBodyTransformFromComponent(URigidBodyComponent* Body)
{
	if (!IsBodyManaged(Body))
	{
		return;
	}

	USceneComponent* UpdatedComponent = Body->GetUpdatedComponent();
	if (UpdatedComponent == nullptr)
	{
		return;
	}

	Impl->PhysicsSystem.GetBodyInterface().SetPositionAndRotation(
		Impl->DynamicBodies[Body],
		ToJoltPosition(UpdatedComponent->GetWorldLocation()),
		ToJoltQuat(GetWorldQuat(UpdatedComponent)),
		JPH::EActivation::Activate);
}

void FJoltPhysicsSystem::SetBodyLinearVelocity(URigidBodyComponent* Body, const FVector& Velocity)
{
	if (!IsBodyManaged(Body) || Body->IsHeldByPhysicsHandle())
	{
		return;
	}

	Impl->PhysicsSystem.GetBodyInterface().SetLinearVelocity(Impl->DynamicBodies[Body], ToJoltVector(Velocity));
}

void FJoltPhysicsSystem::AddBodyImpulse(URigidBodyComponent* Body, const FVector& Impulse)
{
	if (!IsBodyManaged(Body))
	{
		return;
	}

	Impl->PhysicsSystem.GetBodyInterface().AddImpulse(Impl->DynamicBodies[Body], ToJoltVector(Impulse));
}
