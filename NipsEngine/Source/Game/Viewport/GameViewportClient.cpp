#include "Game/Viewport/GameViewportClient.h"

#include "Component/CameraComponent.h"
#include "Engine/GameFramework/World.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Runtime/SceneView.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Math/Utils.h"

#include <cmath>
#include <windows.h>

namespace
{
	static constexpr int GameInputKeys[] = {
		'W', 'A', 'S', 'D', 'Q', 'E',
		VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_SPACE, VK_ESCAPE,
	};

	FVector MakeForwardFromCamera(const UCameraComponent& Camera)
	{
		const float PitchRad = MathUtil::DegreesToRadians(Camera.GetPitchDegrees());
		const float YawRad = MathUtil::DegreesToRadians(Camera.GetYawDegrees());

		return FVector(std::cos(PitchRad) * std::cos(YawRad), std::cos(PitchRad) * std::sin(YawRad), std::sin(PitchRad)).GetSafeNormal();
	}

	FVector MakeRightFromCamera(const UCameraComponent& Camera)
	{
		const float YawRad = MathUtil::DegreesToRadians(Camera.GetYawDegrees());
		return FVector(-std::sin(YawRad), std::cos(YawRad), 0.0f).GetSafeNormal();
	}
} // namespace

void FGameViewportClient::Initialize(FWindowsWindow* InWindow)
{
	FViewportClient::Initialize(InWindow);
	DebugCamera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
	DebugCamera.SetLocation(FVector(-5.0f, -5.0f, 3.0f));
	DebugCamera.SetLookAt(FVector::ZeroVector);
	InputRouter.GetPlayerController().SetDebugCamera(&DebugCamera);
	UpdateControllerViewportDim();
}

void FGameViewportClient::SetViewportSize(float InWidth, float InHeight)
{
	FViewportClient::SetViewportSize(InWidth, InHeight);
	DebugCamera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
	if (ActiveCamera)
	{
		ActiveCamera->OnResize(static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	}
	UpdateControllerViewportDim();
}

void FGameViewportClient::Tick(float DeltaTime)
{
	UpdateControllerViewportDim();
	InputRouter.Tick(DeltaTime);
	TickKeyboardInput();
	TickMouseInput();
}

void FGameViewportClient::BuildSceneView(FSceneView& OutView) const
{
	if (ActiveCamera)
	{
		OutView.ViewMatrix = ActiveCamera->GetViewMatrix();
		OutView.ProjectionMatrix = ActiveCamera->GetProjectionMatrix();
		OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

		OutView.CameraPosition = ActiveCamera->GetWorldLocation();
		OutView.CameraForward = MakeForwardFromCamera(*ActiveCamera);
		OutView.CameraRight = MakeRightFromCamera(*ActiveCamera);
		OutView.CameraUp = FVector::UpVector;

		OutView.NearPlane = ActiveCamera->GetNearPlane();
		OutView.FarPlane = ActiveCamera->GetFarPlane();
		OutView.bOrthographic = ActiveCamera->IsOrthogonal();
		OutView.CameraOrthoHeight = ActiveCamera->GetOrthoWidth();
		OutView.CameraFrustum.UpdateFromCamera(OutView.ViewProjectionMatrix);
	}
	else
	{
		OutView.ViewMatrix = DebugCamera.GetViewMatrix();
		OutView.ProjectionMatrix = DebugCamera.GetProjectionMatrix();
		OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

		OutView.CameraPosition = DebugCamera.GetLocation();
		OutView.CameraForward = DebugCamera.GetForwardVector();
		OutView.CameraRight = DebugCamera.GetRightVector();
		OutView.CameraUp = DebugCamera.GetUpVector();

		OutView.NearPlane = DebugCamera.GetNearPlane();
		OutView.FarPlane = DebugCamera.GetFarPlane();
		OutView.bOrthographic = DebugCamera.IsOrthographic();
		OutView.CameraOrthoHeight = DebugCamera.GetOrthoHeight();
		OutView.CameraFrustum = DebugCamera.GetFrustum();
	}

	OutView.ViewRect = FViewportRect(0, 0, static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	OutView.ViewMode = EViewMode::Lit;
}

void FGameViewportClient::SetWorld(UWorld* InWorld)
{
	World = InWorld;
	if (World)
	{
		World->SetActiveCamera(&DebugCamera);
	}
}

void FGameViewportClient::SetCamera(UCameraComponent* InCamera)
{
	ActiveCamera = InCamera;
	InputRouter.GetPlayerController().SetCamera(InCamera);
	if (ActiveCamera)
	{
		ActiveCamera->OnResize(static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	}
}

void FGameViewportClient::TickKeyboardInput()
{
	const InputSystem& IS = InputSystem::Get();
	if (IS.GetGuiInputState().bBlockViewportInput || IS.GetGuiInputState().bUsingKeyboard)
	{
		return;
	}

	for (int VK : GameInputKeys)
	{
		if (IS.GetKeyDown(VK))
		{
			InputRouter.RouteKeyboardInput(EKeyInputType::KeyPressed, VK);
		}
		if (IS.GetKey(VK))
		{
			InputRouter.RouteKeyboardInput(EKeyInputType::KeyDown, VK);
		}
		if (IS.GetKeyUp(VK))
		{
			InputRouter.RouteKeyboardInput(EKeyInputType::KeyReleased, VK);
		}
	}
}

void FGameViewportClient::TickMouseInput()
{
	const InputSystem& IS = InputSystem::Get();
	if (IS.GetGuiInputState().bBlockViewportInput)
	{
		return;
	}

	POINT MousePoint = IS.GetMousePos();
	if (Window)
	{
		MousePoint = Window->ScreenToClientPoint(MousePoint);
	}

	const float LocalX = static_cast<float>(MousePoint.x);
	const float LocalY = static_cast<float>(MousePoint.y);
	const float DeltaX = static_cast<float>(IS.MouseDeltaX());
	const float DeltaY = static_cast<float>(IS.MouseDeltaY());

	if (IS.MouseMoved())
	{
		InputRouter.RouteMouseInput(EMouseInputType::E_MouseMoved, DeltaX, DeltaY);
		InputRouter.RouteMouseInput(EMouseInputType::E_MouseMovedAbsolute, LocalX, LocalY);
	}

	if (IS.GetKeyDown(VK_RBUTTON))
	{
		InputRouter.RouteMouseInput(EMouseInputType::E_RightMouseClicked, LocalX, LocalY);
	}
	if (IS.GetRightDragging())
	{
		InputRouter.RouteMouseInput(EMouseInputType::E_RightMouseDragged, DeltaX, DeltaY);
	}
	if (IS.GetMiddleDragging())
	{
		InputRouter.RouteMouseInput(EMouseInputType::E_MiddleMouseDragged, DeltaX, DeltaY);
	}
	if (!MathUtil::IsNearlyZero(IS.GetScrollNotches()))
	{
		InputRouter.RouteMouseInput(EMouseInputType::E_MouseWheelScrolled, IS.GetScrollNotches(), 0.0f);
	}

	if (IS.GetKeyDown(VK_LBUTTON))
	{
		InputRouter.RouteMouseInput(EMouseInputType::E_LeftMouseClicked, LocalX, LocalY);
	}
	if (IS.GetLeftDragging())
	{
		InputRouter.RouteMouseInput(EMouseInputType::E_LeftMouseDragged, LocalX, LocalY);
	}
	if (IS.GetLeftDragEnd())
	{
		InputRouter.RouteMouseInput(EMouseInputType::E_LeftMouseDragEnded, LocalX, LocalY);
	}
	if (IS.GetKeyUp(VK_LBUTTON) && !IS.GetLeftDragEnd())
	{
		InputRouter.RouteMouseInput(EMouseInputType::E_LeftMouseButtonUp, LocalX, LocalY);
	}
}

void FGameViewportClient::UpdateControllerViewportDim()
{
	InputRouter.GetPlayerController().SetViewportDim(0.0f, 0.0f, WindowWidth, WindowHeight);
}
