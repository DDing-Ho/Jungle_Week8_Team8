#include "Game/Input/GamePlayerController.h"

#include "Component/CameraComponent.h"
#include "Engine/Runtime/SceneView.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "GameFramework/AActor.h"
#include "Math/Matrix.h"
#include "Math/Utils.h"

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

	const FName& AxisMoveUp()
	{
		static const FName Name("MoveUp");
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
}

FGamePlayerController::FGamePlayerController()
{
	SetupDefaultInputMappings();
}

void FGamePlayerController::Tick(float DeltaTime)
{
	IBaseGameController::Tick(DeltaTime);
	SyncFreeCameraAngles();
	ApplyInputAxes();
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
}

void FGamePlayerController::OnLeftMouseDrag(float X, float Y)
{
	(void)X;
	(void)Y;
}

void FGamePlayerController::OnLeftMouseDragEnd(float X, float Y)
{
	(void)X;
	(void)Y;
}

void FGamePlayerController::OnLeftMouseButtonUp(float X, float Y)
{
	(void)X;
	(void)Y;
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
	MoveActiveCamera(ToForward(Camera ? Camera->GetPitchDegrees() : FreeCameraPitch,
		Camera ? Camera->GetYawDegrees() : FreeCameraYaw), Notch * MoveSpeed * 0.25f);
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

	// Axis Mapping: 여러 키를 하나의 연속 값으로 합칩니다.
	// 예를 들어 W는 MoveForward에 +1, S는 -1을 더합니다.
	// 키를 바꾸려면 문자 키나 VK_* 값을 바꾸고, 방향을 바꾸려면 Scale의 부호를 바꾸면 됩니다.
	// 예: 위쪽 화살표도 전진에 쓰려면 AddAxisMapping(AxisMoveForward(), VK_UP, 1.0f)를 추가합니다.
	InputMapping.AddAxisMapping(AxisMoveForward(), 'W', 1.0f);
	InputMapping.AddAxisMapping(AxisMoveForward(), 'S', -1.0f);
	InputMapping.AddAxisMapping(AxisMoveRight(), 'D', 1.0f);
	InputMapping.AddAxisMapping(AxisMoveRight(), 'A', -1.0f);
	InputMapping.AddAxisMapping(AxisMoveUp(), 'E', 1.0f);
	InputMapping.AddAxisMapping(AxisMoveUp(), 'Q', -1.0f);

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
	const float MoveUpValue = InputMapping.GetAxisValue(AxisMoveUp());

	// GetAxisValue가 매핑된 키들을 보고 MoveForwardValue 같은 의미 값만 돌려줍니다.
	// Axis 값은 -1~+1 범위의 값으로 사용합니다.
	// 실제 월드 방향은 현재 카메라의 forward/right/up 벡터로 변환합니다.
	const float Yaw = Camera ? Camera->GetYawDegrees() : FreeCameraYaw;
	const float Pitch = Camera ? Camera->GetPitchDegrees() : FreeCameraPitch;
	const FVector Forward = ToForward(Pitch, Yaw);
	const FVector Right = ToRight(Yaw);
	const FVector Up = FVector::UpVector;

	const FVector Direction = Forward * MoveForwardValue + Right * MoveRightValue + Up * MoveUpValue;
	if (!Direction.IsNearlyZero())
	{
		MoveActiveCamera(Direction.GetSafeNormal(), MoveSpeed * DeltaTime);
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
