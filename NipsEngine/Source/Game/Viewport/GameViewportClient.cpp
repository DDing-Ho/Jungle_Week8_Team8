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

FGameViewportClient::~FGameViewportClient()
{
	ReleaseMouseCursor();
	ShowMouseCursor();
}

// 디버그용 Free Camera와 PlayerController를 초기화합니다.
void FGameViewportClient::Initialize(FWindowsWindow* InWindow)
{
	FViewportClient::Initialize(InWindow);
	FreeCamera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
	FreeCamera.SetLocation(FVector(-5.0f, -5.0f, 3.0f));
	FreeCamera.SetLookAt(FVector::ZeroVector);
	InputRouter.GetPlayerController().SetFreeCamera(&FreeCamera);
	UpdateControllerViewportDim();
	UpdateCursorCapture();
}

// 현재 활성화된 카메라에 맞춰 뷰포트 크기를 적용합니다.
void FGameViewportClient::SetViewportSize(float InWidth, float InHeight)
{
	FViewportClient::SetViewportSize(InWidth, InHeight);
	FreeCamera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
	if (ActiveCamera)
	{
		ActiveCamera->OnResize(static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	}
	UpdateControllerViewportDim();
	UpdateCursorCapture();
}

void FGameViewportClient::Tick(float DeltaTime)
{
	UpdateCursorCapture();
	UpdateControllerViewportDim();
	InputRouter.Tick(DeltaTime);
	TickKeyboardInput();
	TickMouseInput();
}

// 카메라 활성화 여부에 따라 적절한 카메라를 선택하여 렌더러에 넘겨줄 FSceneView 구조체의 내용을 채웁니다.
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
		OutView.ViewMatrix = FreeCamera.GetViewMatrix();
		OutView.ProjectionMatrix = FreeCamera.GetProjectionMatrix();
		OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

		OutView.CameraPosition = FreeCamera.GetLocation();
		OutView.CameraForward = FreeCamera.GetForwardVector();
		OutView.CameraRight = FreeCamera.GetRightVector();
		OutView.CameraUp = FreeCamera.GetUpVector();

		OutView.NearPlane = FreeCamera.GetNearPlane();
		OutView.FarPlane = FreeCamera.GetFarPlane();
		OutView.bOrthographic = FreeCamera.IsOrthographic();
		OutView.CameraOrthoHeight = FreeCamera.GetOrthoHeight();
		OutView.CameraFrustum = FreeCamera.GetFrustum();
	}

	OutView.ViewRect = FViewportRect(0, 0, static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	OutView.ViewMode = EViewMode::Lit;
}

void FGameViewportClient::SetWorld(UWorld* InWorld)
{
	World = InWorld;
	if (World)
	{
		World->SetActiveCamera(&FreeCamera);
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

// 프레임당 키보드 입력을 처리합니다.
void FGameViewportClient::TickKeyboardInput()
{
	const InputSystem& IS = InputSystem::Get();
	
	// F4 키로 커서 숨김, 마우스/키보드 입력 처리 여부를 토글합니다.
	if (!ActiveCamera && IS.GetKeyDown(VK_F4))
		ToggleInteractionMode();

	if (IS.GetGuiInputState().bBlockViewportInput || (!ActiveCamera && !bInputActive))
		return;

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

// 프레임당 마우스 입력을 처리합니다.
void FGameViewportClient::TickMouseInput()
{
	const InputSystem& IS = InputSystem::Get();
	if (IS.GetGuiInputState().bBlockViewportInput || (!ActiveCamera && !bInputActive))
		return;

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

// PlayerController에 현재 뷰포트 크기를 적용합니다.
void FGameViewportClient::UpdateControllerViewportDim()
{
	InputRouter.GetPlayerController().SetViewportDim(0.0f, 0.0f, WindowWidth, WindowHeight);
}

// 윈도우 포커스 여부에 따라 마우스 커서를 숨길지, 보일지 결정합니다.
void FGameViewportClient::UpdateCursorCapture()
{
	if (!bInputActive || !Window || !Window->GetHWND())
	{
		ReleaseMouseCursor();
		ShowMouseCursor();
		return;
	}

	if (GetForegroundWindow() == Window->GetHWND())
	{
		HideMouseCursor();
		ConfineMouseCursorToWindow();
		LockMouseCursor();
	}
	else
	{
		ReleaseMouseCursor();
		ShowMouseCursor();
	}
}
 
void FGameViewportClient::HideMouseCursor()
{
	if (!bCursorVisible)
		return;

	while (ShowCursor(FALSE) >= 0);
	bCursorVisible = false;
}

void FGameViewportClient::ShowMouseCursor()
{
	if (bCursorVisible)
		return;

	while (ShowCursor(TRUE) < 0);
	bCursorVisible = true;
}

// 마우스 커서가 윈도우 밖으로 나가지 못하게 합니다.
void FGameViewportClient::ConfineMouseCursorToWindow()
{
	if (!Window || !Window->GetHWND())
		return;

	RECT ClientRect{};
	GetClientRect(Window->GetHWND(), &ClientRect);

	POINT LeftTop{ ClientRect.left, ClientRect.top };
	POINT RightBottom{ ClientRect.right, ClientRect.bottom };
	ClientToScreen(Window->GetHWND(), &LeftTop);
	ClientToScreen(Window->GetHWND(), &RightBottom);

	RECT ClipRect{ LeftTop.x, LeftTop.y, RightBottom.x, RightBottom.y };
	ClipCursor(&ClipRect);
	bCursorConfined = true;
}

// 마우스 커서가 화면 중앙을 벗어나지 못하도록 합니다.
void FGameViewportClient::LockMouseCursor()
{
	if (!Window || !Window->GetHWND())
	{
		return;
	}

	RECT ClientRect{};
	GetClientRect(Window->GetHWND(), &ClientRect);

	POINT Center{(ClientRect.left + ClientRect.right) / 2, (ClientRect.top + ClientRect.bottom) / 2};

	ClientToScreen(Window->GetHWND(), &Center);
	const float ScreenX = static_cast<float>(Center.x) - (ClientRect.right - ClientRect.left) * 0.5f;
	const float ScreenY = static_cast<float>(Center.y) - (ClientRect.bottom - ClientRect.top) * 0.5f;

	InputSystem::Get().LockMouse(true, ScreenX, ScreenY, static_cast<float>(ClientRect.right - ClientRect.left), static_cast<float>(ClientRect.bottom - ClientRect.top));
}

// 마우스 커서가 윈도우 밖으로 나갈 수 있도록 합니다.
void FGameViewportClient::ReleaseMouseCursor()
{
	if (!bCursorConfined)
		return;

	ClipCursor(nullptr);
	InputSystem::Get().LockMouse(false);
	bCursorConfined = false;
}

// 마우스 커서를 표시하고, 입력을 받을지 여부를 토글합니다.
void FGameViewportClient::ToggleInteractionMode()
{
	bInputActive = !bInputActive;
	if (!bInputActive)
	{
		ReleaseMouseCursor();
		ShowMouseCursor();
	}
}
