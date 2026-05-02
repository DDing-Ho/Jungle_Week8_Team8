#include "Game/Input/GamePlayerController.h"

#include "Component/CameraComponent.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Math/Matrix.h"
#include "Math/Utils.h"

#include <cmath>
#include <windows.h>

namespace
{
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

void FGamePlayerController::Tick(float DeltaTime)
{
	IBaseGameController::Tick(DeltaTime);
	SyncFreeCameraAngles();
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
	(void)VK;
}

void FGamePlayerController::OnKeyDown(int VK)
{
	FVector Direction = FVector::ZeroVector;

	const float Yaw = Camera ? Camera->GetYawDegrees() : FreeCameraYaw;
	const float Pitch = Camera ? Camera->GetPitchDegrees() : FreeCameraPitch;
	const FVector Forward = ToForward(Pitch, Yaw);
	const FVector Right = ToRight(Yaw);
	const FVector Up = FVector::UpVector;

	switch (VK)
	{
	case 'W': Direction += Forward; break;
	case 'S': Direction -= Forward; break;
	case 'D': Direction += Right; break;
	case 'A': Direction -= Right; break;
	case 'E': Direction += Up; break;
	case 'Q': Direction -= Up; break;
	case VK_LEFT:
		RotateActiveCamera(-60.0f * DeltaTime / RotateSensitivity, 0.0f);
		return;
	case VK_RIGHT:
		RotateActiveCamera(60.0f * DeltaTime / RotateSensitivity, 0.0f);
		return;
	case VK_UP:
		RotateActiveCamera(0.0f, -60.0f * DeltaTime / RotateSensitivity);
		return;
	case VK_DOWN:
		RotateActiveCamera(0.0f, 60.0f * DeltaTime / RotateSensitivity);
		return;
	default:
		return;
	}

	if (!Direction.IsNearlyZero())
	{
		MoveActiveCamera(Direction.GetSafeNormal(), MoveSpeed * DeltaTime);
	}
}

void FGamePlayerController::OnKeyReleased(int VK)
{
	(void)VK;
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
