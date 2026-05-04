#include "Game/Input/GamePlayerController.h"

#include "Component/CameraComponent.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/Physics/PhysicsHandleComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Logger.h"
#include "Engine/Runtime/SceneView.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Game/Systems/CleaningToolAnimator.h"
#include "Game/Systems/CleaningToolSystem.h"
#include "Game/Systems/GameContext.h"
#include "Game/Systems/ItemSystem.h"
#include "Game/UI/GameUISystem.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Matrix.h"
#include "Math/Utils.h"
#include "Object/Object.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Scripting/LuaScriptSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <windows.h>

namespace
{
	// 새 Action을 추가하려면:
	// 1. 여기에 이름 함수를 하나 더 만듭니다. 예: ActionInteract() -> "Interact"
	// 2. SetupDefaultInputMappings()에서 원하는 키를 AddActionMapping으로 연결합니다.
	// 3. OnKeyPressed/OnKeyReleased에서 IsActionKey로 처리합니다.
	const FName& ActionToggleInputCapture()
	{
		static const FName Name("ToggleInputCapture");
		return Name;
	}

	const FName& ActionTogglePause()
	{
		static const FName Name("TogglePause");
		return Name;
	}

	const FName& ActionPickup()
	{
		static const FName Name("Pickup");
		return Name;
	}

	// 새 Axis를 추가하려면:
	// 1. 여기에 이름 함수를 하나 더 만듭니다. 예: AxisMoveForward() -> "MoveForward"
	// 2. SetupDefaultInputMappings()에서 키와 Scale을 AddAxisMapping으로 연결합니다.
	// 3. ApplyInputAxes()에서 GetAxisValue로 값을 읽어 사용합니다.
	const FName& AxisMoveForward()
	{
		static const FName Name("MoveForward");
		return Name;
	}

	const FName& AxisMoveRight()
	{
		static const FName Name("MoveRight");
		return Name;
	}

	const FName& AxisLookYaw()
	{
		static const FName Name("LookYaw");
		return Name;
	}

	const FName& AxisLookPitch()
	{
		static const FName Name("LookPitch");
		return Name;
	}

	FVector ToForward(float PitchDegrees, float YawDegrees)
	{
		const float PitchRad = MathUtil::DegreesToRadians(PitchDegrees);
		const float YawRad = MathUtil::DegreesToRadians(YawDegrees);

		return FVector(std::cos(PitchRad) * std::cos(YawRad), std::cos(PitchRad) * std::sin(YawRad), std::sin(PitchRad)).GetSafeNormal();
	}

	FVector ToRight(float YawDegrees)
	{
		const float YawRad = MathUtil::DegreesToRadians(YawDegrees);
		return FVector(-std::sin(YawRad), std::cos(YawRad), 0.0f).GetSafeNormal();
	}

	FVector ToWorldOffset(const FVector& CameraForward, const FVector& CameraRight, const FVector& CameraUp, const FVector& CameraLocalOffset)
	{
		return CameraForward.GetSafeNormal() * CameraLocalOffset.X
			+ CameraRight.GetSafeNormal() * CameraLocalOffset.Y
			+ CameraUp.GetSafeNormal() * CameraLocalOffset.Z;
	}

	FVector ToWorldOffset(const FViewportCamera* Camera, const FVector& CameraLocalOffset)
	{
		if (!Camera)
		{
			return FVector::ZeroVector;
		}

		return ToWorldOffset(Camera->GetForwardVector(), Camera->GetRightVector(), Camera->GetUpVector(), CameraLocalOffset);
	}

	FQuat BuildCameraLocalHandleRotation(const FVector& CameraForward, const FVector& CameraRight, const FVector& CameraUp, const FVector& HandleCameraLocalDirection)
	{
		const FVector Forward = CameraForward.GetSafeNormal();
		const FVector HandleWorldDirection = ToWorldOffset(Forward, CameraRight, CameraUp, HandleCameraLocalDirection).GetSafeNormal();
		if (HandleWorldDirection.IsNearlyZero() || Forward.IsNearlyZero())
		{
			return FQuat::Identity;
		}

		FVector YAxis = FVector::CrossProduct(Forward, HandleWorldDirection).GetSafeNormal();
		if (YAxis.IsNearlyZero())
		{
			YAxis = CameraRight.GetSafeNormal();
		}

		const FVector ZAxis = FVector::CrossProduct(HandleWorldDirection, YAxis).GetSafeNormal();
		FMatrix RotationMatrix = FMatrix::Identity;
		RotationMatrix.SetAxes(HandleWorldDirection, YAxis, ZAxis);

		FQuat Rotation(RotationMatrix);
		Rotation.Normalize();
		return Rotation;
	}

	FQuat BuildCameraLocalHandleRotation(const FViewportCamera* Camera, const FVector& HandleCameraLocalDirection)
	{
		if (!Camera)
		{
			return FQuat::Identity;
		}

		return BuildCameraLocalHandleRotation(Camera->GetForwardVector(), Camera->GetRightVector(), Camera->GetUpVector(), HandleCameraLocalDirection);
	}

	FString NormalizeToolMatchKey(FString Value)
	{
		std::replace(Value.begin(), Value.end(), '\\', '/');
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char C)
		{
			return static_cast<char>(std::tolower(C));
		});
		return Value;
	}

	FString FindCleaningToolIdFromActor(const AActor* Actor, bool bLogResult = true)
	{
		if (!Actor)
		{
			return "";
		}

		const FString ScriptToolId = FLuaScriptSystem::Get().GetStringGameStateValue("CleaningTool:" + Actor->GetFName().ToString());
		if (!ScriptToolId.empty())
		{
			if (bLogResult)
			{
				UE_LOG("[CleaningTool] Actor=%s resolved by Lua toolId=%s", Actor->GetFName().ToString().c_str(), ScriptToolId.c_str());
			}
			return ScriptToolId;
		}

		const FString ActorName = NormalizeToolMatchKey(Actor->GetName());
		if (bLogResult)
		{
			UE_LOG("[CleaningTool] Actor=%s has no Lua tool id. Trying fallback match.", Actor->GetFName().ToString().c_str());
		}
		const TArray<FCleaningToolData>& ToolDataList = FCleaningToolSystem::Get().GetAllToolData();
		for (const FCleaningToolData& ToolData : ToolDataList)
		{
			if (ActorName == NormalizeToolMatchKey(ToolData.ToolId))
			{
				if (bLogResult)
				{
					UE_LOG("[CleaningTool] Actor=%s resolved by actor name toolId=%s", Actor->GetFName().ToString().c_str(), ToolData.ToolId.c_str());
				}
				return ToolData.ToolId;
			}
		}

		if (bLogResult)
		{
			UE_LOG("[CleaningTool] Actor=%s is not a registered cleaning tool.", Actor->GetFName().ToString().c_str());
		}
		return "";
	}

	FString FindItemIdFromActor(const AActor* Actor)
	{
		if (!Actor)
		{
			return "";
		}

		const FString ScriptItemId = FLuaScriptSystem::Get().GetStringGameStateValue("Item:" + Actor->GetFName().ToString());
		if (!ScriptItemId.empty())
		{
			return ScriptItemId;
		}

		const FString ActorName = NormalizeToolMatchKey(Actor->GetName());
		const TArray<FGameItemData>& Items = FItemSystem::Get().GetAllItemData();
		for (const FGameItemData& ItemData : Items)
		{
			if (ActorName == NormalizeToolMatchKey(ItemData.ItemId))
			{
				return ItemData.ItemId;
			}
		}

		return "";
	}
}

FGamePlayerController::FGamePlayerController()
{
	SetupDefaultInputMappings();
}

FGamePlayerController::~FGamePlayerController()
{
	DestroyPhysicsHandle();
}

void FGamePlayerController::Tick(float DeltaTime)
{
	IBaseGameController::Tick(DeltaTime);
	RefreshPawnComponents();
	CaptureInitialRigidBodyRotations();
	SyncFreeCameraAngles();
	ApplyInputAxes();
	UpdateHoveredPickableActor();
	FCleaningToolAnimator::Get().Tick(DeltaTime);
	if (UPhysicsHandleComponent* Handle = GetPhysicsHandle())
	{
		FVector CameraLocation;
		FVector CameraForward;
		FVector CameraRight;
		FVector CameraUp;
		if (!GetActiveCameraBasis(CameraLocation, CameraForward, CameraRight, CameraUp))
		{
			return;
		}

		const FString& CurrentToolId = GGameContext::Get().GetCurrentToolId();
		const FCleaningToolData* ToolData = CurrentToolId.empty() ? nullptr : FCleaningToolSystem::Get().FindToolData(CurrentToolId);
		const FVector ToolCameraLocalOffset = FCleaningToolAnimator::Get().GetHoldCameraLocalOffset()
			+ FCleaningToolAnimator::Get().GetCameraLocalOffset();
		const FVector ToolOffset = ToWorldOffset(CameraForward, CameraRight, CameraUp, ToolCameraLocalOffset);
		FQuat ToolRotation = FQuat::Identity;
		const FQuat* ToolRotationPtr = nullptr;
		if (ToolData && !ToolData->HandleCameraLocalDirection.IsNearlyZero())
		{
			ToolRotation = BuildCameraLocalHandleRotation(CameraForward, CameraRight, CameraUp, ToolData->HandleCameraLocalDirection);
			ToolRotationPtr = &ToolRotation;
		}
		Handle->TickHandle(DeltaTime, CameraLocation, CameraForward, ToolOffset, ToolRotationPtr, ToolData != nullptr);
	}
}

void FGamePlayerController::OnMouseMove(float DeltaX, float DeltaY)
{
	RotateActiveCamera(DeltaX, DeltaY);
}

void FGamePlayerController::OnMouseMoveAbsolute(float X, float Y)
{
	(void)X;
	(void)Y;
}

void FGamePlayerController::OnLeftMouseClick(float X, float Y)
{
	(void)X;
	(void)Y;
	TryBeginCleaningUse();
}

void FGamePlayerController::OnLeftMouseDrag(float X, float Y)
{
	(void)X;
	(void)Y;
	TryBeginCleaningUse();
}

void FGamePlayerController::OnLeftMouseDragEnd(float X, float Y)
{
	(void)X;
	(void)Y;
	EndCleaningUse();
}

void FGamePlayerController::OnLeftMouseButtonUp(float X, float Y)
{
	(void)X;
	(void)Y;
	EndCleaningUse();
}

void FGamePlayerController::OnRightMouseClick(float DeltaX, float DeltaY)
{
	(void)DeltaX;
	(void)DeltaY;
}

void FGamePlayerController::OnRightMouseDrag(float DeltaX, float DeltaY)
{
	RotateActiveCamera(DeltaX, DeltaY);
}

void FGamePlayerController::OnMiddleMouseDrag(float DeltaX, float DeltaY)
{
	(void)DeltaX;
	(void)DeltaY;
}

void FGamePlayerController::OnKeyPressed(int VK)
{
	if (InputMapping.IsActionKey(ActionToggleInputCapture(), VK) && !Camera && OnRequestToggleInputCapture)
	{
		OnRequestToggleInputCapture();
	}

	if (InputMapping.IsActionKey(ActionPickup(), VK))
	{
		TogglePickup();
	}
}

void FGamePlayerController::OnKeyDown(int VK)
{
	// 이동/회전처럼 누르고 있는 동안 계속 적용되는 입력은 Axis에서 처리합니다.
	// 그래서 개별 키 반복 이벤트인 OnKeyDown에서는 직접 움직이지 않습니다.
	(void)VK;
}

void FGamePlayerController::OnKeyReleased(int VK)
{
	if (InputMapping.IsActionKey(ActionTogglePause(), VK) && OnRequestTogglePause)
	{
		OnRequestTogglePause();
	}
}

void FGamePlayerController::OnWheelScrolled(float Notch)
{
	if (IsRuntimeWorld())
	{
		return;
	}

	MoveActiveCamera(ToForward(Camera ? Camera->GetPitchDegrees() : FreeCameraPitch,
		Camera ? Camera->GetYawDegrees() : FreeCameraYaw), Notch * MoveSpeed * 0.25f);
}

void FGamePlayerController::SetWorld(UWorld* InWorld)
{
	if (World == InWorld)
	{
		return;
	}

	World = InWorld;
	Player = nullptr;
	Camera = nullptr;
	CharacterMovement = nullptr;
	HoveredPickableActor = nullptr;
	InitialRigidBodyRotations.clear();
	bInitialRigidBodyRotationsCaptured = false;
	DestroyPhysicsHandle();
}

void FGamePlayerController::SetPlayer(AActor* InPlayer)
{
	if (Player == InPlayer)
	{
		return;
	}

	DestroyPhysicsHandle();
	CharacterMovement = nullptr;
	Player = InPlayer;
	RefreshPawnComponents();
}

void FGamePlayerController::SetCamera(UCameraComponent* InCamera)
{
	Camera = InCamera;
	if (Camera)
	{
		Camera->OnResize(static_cast<int32>(ViewportWidth), static_cast<int32>(ViewportHeight));
	}
}

void FGamePlayerController::SetFreeCamera(FViewportCamera* InCamera)
{
	FreeCamera = InCamera;
	bFreeCameraInitialized = false;
	SyncFreeCameraAngles();
}

AActor* FGamePlayerController::GetHeldNonCleaningToolActor() const
{
	if (PhysicsHandle == nullptr || !PhysicsHandle->IsHolding())
	{
		return nullptr;
	}

	if (!GGameContext::Get().GetCurrentToolId().empty())
	{
		return nullptr;
	}

	URigidBodyComponent* HeldBody = PhysicsHandle->GetHeldBody();
	return HeldBody ? HeldBody->GetOwner() : nullptr;
}

void FGamePlayerController::InitializeFreeCameraFromSnapshot(const FCameraSnapshot& Snapshot)
{
	if (!FreeCamera)
	{
		return;
	}

	FreeCamera->ClearCustomLookDir();
	FreeCamera->SetLocation(Snapshot.Location);
	FreeCamera->SetRotation(Snapshot.Rotation);
	FreeCamera->SetProjectionType(Snapshot.ProjectionType);
	FreeCamera->OnResize(Snapshot.Width, Snapshot.Height);
	FreeCamera->SetFOV(Snapshot.FOV);
	FreeCamera->SetNearPlane(Snapshot.NearPlane);
	FreeCamera->SetFarPlane(Snapshot.FarPlane);
	FreeCamera->SetOrthoHeight(Snapshot.OrthoHeight);

	bFreeCameraInitialized = false;
	SyncFreeCameraAngles();
}

void FGamePlayerController::BuildSceneView(FSceneView& OutView, const FViewportRect& ViewRect, EViewMode ViewMode) const
{
	if (Camera)
	{
		OutView.ViewMatrix = Camera->GetViewMatrix();
		OutView.ProjectionMatrix = Camera->GetProjectionMatrix();
		OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

		OutView.CameraPosition = Camera->GetWorldLocation();
		OutView.CameraForward = Camera->GetForwardVector();
		OutView.CameraRight = Camera->GetRightVector();
		OutView.CameraUp = Camera->GetUpVector();

		OutView.NearPlane = Camera->GetNearPlane();
		OutView.FarPlane = Camera->GetFarPlane();
		OutView.bOrthographic = Camera->IsOrthogonal();
		OutView.CameraOrthoHeight = Camera->GetOrthoWidth();
		OutView.CameraFrustum.UpdateFromCamera(OutView.ViewProjectionMatrix);
	}
	else if (FreeCamera)
	{
		OutView.ViewMatrix = FreeCamera->GetViewMatrix();
		OutView.ProjectionMatrix = FreeCamera->GetProjectionMatrix();
		OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

		OutView.CameraPosition = FreeCamera->GetLocation();
		OutView.CameraForward = FreeCamera->GetForwardVector();
		OutView.CameraRight = FreeCamera->GetRightVector();
		OutView.CameraUp = FreeCamera->GetUpVector();

		OutView.NearPlane = FreeCamera->GetNearPlane();
		OutView.FarPlane = FreeCamera->GetFarPlane();
		OutView.bOrthographic = FreeCamera->IsOrthographic();
		OutView.CameraOrthoHeight = FreeCamera->GetOrthoHeight();
		OutView.CameraFrustum = FreeCamera->GetFrustum();
	}

	OutView.ViewRect = ViewRect;
	OutView.ViewMode = ViewMode;
}

void FGamePlayerController::SetupDefaultInputMappings()
{
	InputMapping.Clear();

	// Action Mapping: 키 입력을 "한 번 발생하는 명령" 이름에 연결합니다.
	// 예: InputMapping.AddActionMapping(ActionInteract(), 'E');
	InputMapping.AddActionMapping(ActionToggleInputCapture(), VK_F4);
	InputMapping.AddActionMapping(ActionTogglePause(), 'P');
	InputMapping.AddActionMapping(ActionPickup(), 'E');

	// Axis Mapping: 여러 키를 하나의 연속 값으로 합칩니다.
	// 예를 들어 W는 MoveForward에 +1, S는 -1을 더합니다.
	// 키를 바꾸려면 문자 키나 VK_* 값을 바꾸고, 방향을 바꾸려면 Scale의 부호를 바꾸면 됩니다.
	// 예: 위쪽 화살표도 전진에 쓰려면 AddAxisMapping(AxisMoveForward(), VK_UP, 1.0f)를 추가합니다.
	InputMapping.AddAxisMapping(AxisMoveForward(), 'W', 1.0f);
	InputMapping.AddAxisMapping(AxisMoveForward(), 'S', -1.0f);
	InputMapping.AddAxisMapping(AxisMoveRight(), 'D', 1.0f);
	InputMapping.AddAxisMapping(AxisMoveRight(), 'A', -1.0f);

	InputMapping.AddAxisMapping(AxisLookYaw(), VK_RIGHT, 1.0f);
	InputMapping.AddAxisMapping(AxisLookYaw(), VK_LEFT, -1.0f);
	InputMapping.AddAxisMapping(AxisLookPitch(), VK_DOWN, 1.0f);
	InputMapping.AddAxisMapping(AxisLookPitch(), VK_UP, -1.0f);
}

void FGamePlayerController::ApplyInputAxes()
{
	if (!IsInputEnabled())
	{
		return;
	}

	const float MoveForwardValue = InputMapping.GetAxisValue(AxisMoveForward());
	const float MoveRightValue = InputMapping.GetAxisValue(AxisMoveRight());

	// GetAxisValue가 매핑된 키들을 보고 MoveForwardValue 같은 의미 값만 돌려줍니다.
	// Axis 값은 -1~+1 범위의 값으로 사용합니다.
	// 실제 월드 방향은 현재 카메라의 forward/right/up 벡터로 변환합니다.
	const float Yaw = Camera ? Camera->GetYawDegrees() : FreeCameraYaw;
	const float Pitch = Camera ? Camera->GetPitchDegrees() : FreeCameraPitch;
	const FVector Forward = IsRuntimeWorld() ? ToForward(0.0f, Yaw) : ToForward(Pitch, Yaw);
	const FVector Right = ToRight(Yaw);

	const FVector Direction = Forward * MoveForwardValue + Right * MoveRightValue;
	if (!Direction.IsNearlyZero())
	{
		if (IsRuntimeWorld())
		{
			if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
			{
				MoveComp->SetActive(true);
				MoveComp->SetComponentTickEnabled(true);
				MoveComp->AddInputVector(Direction.GetSafeNormal());
				static int32 InputLogCounter = 0;
				if ((InputLogCounter++ % 30) == 0)
				{
					UE_LOG("[PlayerMove] Controller input forward=%.2f right=%.2f dir=(%.2f, %.2f, %.2f) player=%s moveComp=%s",
						MoveForwardValue,
						MoveRightValue,
						Direction.GetSafeNormal().X,
						Direction.GetSafeNormal().Y,
						Direction.GetSafeNormal().Z,
						Player ? Player->GetFName().ToString().c_str() : "None",
						MoveComp->GetFName().ToString().c_str());
				}
			}
			else
			{
				UE_LOG("[PlayerMove] Controller has movement input but no CharacterMovementComponent. Falling back to direct move.");
				MoveActiveCamera(Direction.GetSafeNormal(), MoveSpeed * DeltaTime);
			}
		}
		else
		{
			MoveActiveCamera(Direction.GetSafeNormal(), MoveSpeed * DeltaTime);
		}
	}

	if (!MathUtil::IsNearlyZero(RotateSensitivity))
	{
		// 화살표 키도 Axis로 읽어서 매 프레임 일정한 속도로 회전시킵니다.
		const float LookYawValue = InputMapping.GetAxisValue(AxisLookYaw());
		const float LookPitchValue = InputMapping.GetAxisValue(AxisLookPitch());
		const float RotateScale = 60.0f * DeltaTime / RotateSensitivity;
		if (!MathUtil::IsNearlyZero(LookYawValue) || !MathUtil::IsNearlyZero(LookPitchValue))
		{
			RotateActiveCamera(LookYawValue * RotateScale, LookPitchValue * RotateScale);
		}
	}
}

bool FGamePlayerController::TryBeginCleaningUse()
{
	if (!IsInputEnabled())
	{
		UE_LOG("[CleaningTool] BeginUse blocked: input disabled.");
		return false;
	}

	if (!PhysicsHandle || !PhysicsHandle->IsHolding())
	{
		UE_LOG("[CleaningTool] BeginUse blocked: no held physics object.");
		return false;
	}

	const FString& CurrentToolId = GGameContext::Get().GetCurrentToolId();
	if (CurrentToolId.empty())
	{
		UE_LOG("[CleaningTool] BeginUse blocked: current tool id is empty.");
		return false;
	}

	const FCleaningToolData* ToolData = FCleaningToolSystem::Get().FindToolData(CurrentToolId);
	if (!ToolData)
	{
		UE_LOG("[CleaningTool] BeginUse blocked: tool data not found for toolId=%s.", CurrentToolId.c_str());
		return false;
	}

	bIsCleaningUseHeld = true;
	FCleaningToolAnimator::Get().BeginUse(*ToolData);
	UE_LOG("[CleaningTool] BeginUse started: toolId=%s amplitude=%.3f speed=%.3f.",
		CurrentToolId.c_str(),
		ToolData->UseBobAmplitude,
		ToolData->UseBobSpeed);
	return true;
}

void FGamePlayerController::EndCleaningUse()
{
	if (!bIsCleaningUseHeld)
	{
		return;
	}

	bIsCleaningUseHeld = false;
	FCleaningToolAnimator::Get().EndUse();
}

void FGamePlayerController::TogglePickup()
{
	if (!World || !IsInputEnabled())
	{
		return;
	}

	UPhysicsHandleComponent* Handle = GetPhysicsHandle();
	if (!Handle)
	{
		return;
	}

	if (Handle->IsHolding())
	{
		UE_LOG("[CleaningTool] Releasing held object. currentToolId=%s", GGameContext::Get().GetCurrentToolId().c_str());
		EndCleaningUse();
		FCleaningToolAnimator::Get().Reset();
		GGameContext::Get().SetCurrentTool("");
		Handle->Release();
		Handle->ResetHoldDistance();
		return;
	}

	FVector CameraLocation;
	FVector CameraForward;
	if (!GetActiveCameraFrame(CameraLocation, CameraForward))
	{
		return;
	}

	if (Handle->TryGrab(World, CameraLocation, CameraForward))
	{
		ResetHeldBodyRotationToInitial();
		bool bSelectedHeldTool = false;
		if (URigidBodyComponent* HeldBody = Handle->GetHeldBody())
		{
			if (AActor* HeldActor = HeldBody->GetOwner())
			{
				const FString HeldToolId = FindCleaningToolIdFromActor(HeldActor);
				bSelectedHeldTool = !HeldToolId.empty() && FCleaningToolSystem::Get().SelectTool(HeldToolId);
				if (bSelectedHeldTool)
				{
					if (const FCleaningToolData* ToolData = FCleaningToolSystem::Get().FindToolData(HeldToolId))
					{
						Handle->SetHoldDistance(ToolData->HoldDistance, false);
						FCleaningToolAnimator::Get().SetActiveTool(*ToolData);
					}
				}
				UE_LOG("[CleaningTool] Picked actor=%s resolvedToolId=%s selected=%d",
					HeldActor->GetFName().ToString().c_str(),
					HeldToolId.c_str(),
					bSelectedHeldTool ? 1 : 0);
			}
		}

		if (!bSelectedHeldTool)
		{
			UE_LOG("[CleaningTool] Picked object is not a cleaning tool. Clearing current tool.");
			GGameContext::Get().SetCurrentTool("");
			FCleaningToolAnimator::Get().Reset();
			Handle->ResetHoldDistance();
		}
	}
}

UPhysicsHandleComponent* FGamePlayerController::GetPhysicsHandle()
{
	RefreshPawnComponents();
	return PhysicsHandle;
}

UCharacterMovementComponent* FGamePlayerController::GetCharacterMovement()
{
	RefreshPawnComponents();
	return CharacterMovement;
}

void FGamePlayerController::DestroyPhysicsHandle()
{
	if (PhysicsHandle)
	{
		PhysicsHandle->Release();
		PhysicsHandle = nullptr;
	}
	HoveredPickableActor = nullptr;
}

void FGamePlayerController::RefreshPawnComponents()
{
	Camera = nullptr;
	PhysicsHandle = nullptr;
	CharacterMovement = nullptr;
	if (Player == nullptr)
	{
		return;
	}

	for (UActorComponent* Component : Player->GetComponents())
	{
		if (Camera == nullptr)
		{
			Camera = Cast<UCameraComponent>(Component);
			if (Camera != nullptr)
			{
				Camera->OnResize(static_cast<int32>(ViewportWidth), static_cast<int32>(ViewportHeight));
			}
		}

		if (PhysicsHandle == nullptr)
		{
			PhysicsHandle = Cast<UPhysicsHandleComponent>(Component);
		}

		if (CharacterMovement == nullptr)
		{
			CharacterMovement = Cast<UCharacterMovementComponent>(Component);
		}

		if (Camera != nullptr && PhysicsHandle != nullptr && CharacterMovement != nullptr)
		{
			break;
		}
	}
}

void FGamePlayerController::UpdateHoveredPickableActor()
{
	HoveredPickableActor = nullptr;
	GameUISystem::Get().SetInteractionHint(EInteractionHintType::None);

	if (World == nullptr || !IsInputEnabled())
	{
		return;
	}

	UPhysicsHandleComponent* Handle = GetPhysicsHandle();
	if (Handle == nullptr || Handle->IsHolding())
	{
		return;
	}

	FVector CameraLocation;
	FVector CameraForward;
	if (!GetActiveCameraFrame(CameraLocation, CameraForward))
	{
		return;
	}

	if (URigidBodyComponent* Body = Handle->FindPickableBody(World, CameraLocation, CameraForward))
	{
		HoveredPickableActor = Body->GetOwner();
		if (!FindCleaningToolIdFromActor(HoveredPickableActor, false).empty())
		{
			GameUISystem::Get().SetInteractionHint(EInteractionHintType::Clean);
		}
		else if (!FindItemIdFromActor(HoveredPickableActor).empty())
		{
			GameUISystem::Get().SetInteractionHint(EInteractionHintType::Inspect);
		}
	}
}

bool FGamePlayerController::GetActiveCameraFrame(FVector& OutLocation, FVector& OutForward) const
{
	FVector Right;
	FVector Up;
	return GetActiveCameraBasis(OutLocation, OutForward, Right, Up);
}

bool FGamePlayerController::GetActiveCameraBasis(FVector& OutLocation, FVector& OutForward, FVector& OutRight, FVector& OutUp) const
{
	if (Camera != nullptr)
	{
		OutLocation = Camera->GetWorldLocation();
		OutForward = Camera->GetForwardVector();
		OutRight = Camera->GetRightVector();
		OutUp = Camera->GetUpVector();
		return !OutForward.IsNearlyZero();
	}

	if (FreeCamera != nullptr)
	{
		OutLocation = FreeCamera->GetLocation();
		OutForward = FreeCamera->GetForwardVector();
		OutRight = FreeCamera->GetRightVector();
		OutUp = FreeCamera->GetUpVector();
		return !OutForward.IsNearlyZero();
	}

	return false;
}

void FGamePlayerController::CaptureInitialRigidBodyRotations()
{
	if (bInitialRigidBodyRotationsCaptured || !IsRuntimeWorld())
	{
		return;
	}

	InitialRigidBodyRotations.clear();
	for (AActor* Actor : World->GetActors())
	{
		if (Actor == nullptr)
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component);
			if (Body == nullptr)
			{
				continue;
			}

			if (USceneComponent* UpdatedComponent = Body->GetUpdatedComponent())
			{
				InitialRigidBodyRotations[Body] = UpdatedComponent->GetRelativeRotation();
			}
		}
	}

	bInitialRigidBodyRotationsCaptured = true;
}

void FGamePlayerController::ResetHeldBodyRotationToInitial()
{
	if (!IsRuntimeWorld() || PhysicsHandle == nullptr || !PhysicsHandle->IsHolding())
	{
		return;
	}

	URigidBodyComponent* Body = PhysicsHandle->GetHeldBody();
	if (Body == nullptr)
	{
		return;
	}

	USceneComponent* UpdatedComponent = Body->GetUpdatedComponent();
	if (UpdatedComponent == nullptr)
	{
		return;
	}

	auto It = InitialRigidBodyRotations.find(Body);
	if (It == InitialRigidBodyRotations.end())
	{
		It = InitialRigidBodyRotations.emplace(Body, UpdatedComponent->GetRelativeRotation()).first;
	}

	UpdatedComponent->SetRelativeRotation(It->second);
	Body->SetAngularVelocity(FVector::ZeroVector);
	FJoltPhysicsSystem::Get().SetBodyTransformFromComponent(Body);
}

bool FGamePlayerController::IsRuntimeWorld() const
{
	if (World == nullptr)
	{
		return false;
	}

	const EWorldType WorldType = World->GetWorldType();
	return WorldType == EWorldType::PIE || WorldType == EWorldType::Game;
}

void FGamePlayerController::RotateActiveCamera(float DeltaX, float DeltaY)
{
	if (Camera)
	{
		Camera->AddYawInput(DeltaX * RotateSensitivity);
		Camera->AddPitchInput(-DeltaY * RotateSensitivity);
		return;
	}

	if (!FreeCamera)
	{
		return;
	}

	SyncFreeCameraAngles();
	FreeCameraYaw += DeltaX * RotateSensitivity;
	FreeCameraPitch -= DeltaY * RotateSensitivity;
	FreeCameraPitch = MathUtil::Clamp(FreeCameraPitch, -89.0f, 89.0f);
	UpdateFreeCameraRotation();
}

void FGamePlayerController::MoveActiveCamera(const FVector& Direction, float Scale)
{
	if (Player)
	{
		Player->AddActorWorldOffset(Direction * Scale);
		return;
	}

	if (Camera)
	{
		Camera->SetWorldLocation(Camera->GetWorldLocation() + Direction * Scale);
		return;
	}

	if (FreeCamera)
	{
		FreeCamera->SetLocation(FreeCamera->GetLocation() + Direction * Scale);
	}
}

void FGamePlayerController::SyncFreeCameraAngles()
{
	if (!FreeCamera || bFreeCameraInitialized)
		return;

	const FVector Forward = FreeCamera->GetForwardVector().GetSafeNormal();
	FreeCameraPitch = MathUtil::RadiansToDegrees(std::asin(MathUtil::Clamp(Forward.Z, -1.0f, 1.0f)));
	FreeCameraYaw = MathUtil::RadiansToDegrees(std::atan2(Forward.Y, Forward.X));
	bFreeCameraInitialized = true;
}

void FGamePlayerController::UpdateFreeCameraRotation()
{
	if (!FreeCamera)
		return;

	const FVector Forward = ToForward(FreeCameraPitch, FreeCameraYaw);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	if (Right.IsNearlyZero())
		return;

	const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

	FMatrix RotationMatrix = FMatrix::Identity;
	RotationMatrix.SetAxes(Forward, Right, Up);

	FQuat Rotation(RotationMatrix);
	Rotation.Normalize();
	FreeCamera->SetRotation(Rotation);
}
