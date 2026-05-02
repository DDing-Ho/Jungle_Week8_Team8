#include "Engine/Input/InputRouter.h"

#include "Engine/Input/IInputController.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Math/Utils.h"

#include <windows.h>

namespace
{
	static constexpr int InputKeys[] = {
		'W', 'A', 'S', 'D', 'Q', 'E',
		VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN,
		VK_SPACE, VK_ESCAPE, VK_F4,
		VK_DELETE, 'X', 'G',
	};
}

void FInputRouter::Tick(float DeltaTime)
{
	if (WorldType == EWorldType::PIE)
	{
		if (PIEController)
			PIEController->Tick(DeltaTime);
		if (GamePlayerController)
			GamePlayerController->Tick(DeltaTime);
		return;
	}

	if (IInputController* Controller = GetActiveController())
	{
		Controller->Tick(DeltaTime);
	}
}

void FInputRouter::Tick(float DeltaTime, const FInputRouteContext& Context)
{
	if (!Context.bHovered)
	{
		return;
	}

	SetViewportDim(static_cast<float>(Context.ViewportRect.X), static_cast<float>(Context.ViewportRect.Y), static_cast<float>(Context.ViewportRect.Width), static_cast<float>(Context.ViewportRect.Height));

	UpdateControllerModifiers();
	TickCursorCapture(Context);
	Tick(DeltaTime);
	TickKeyboardInput(Context);
	TickMouseInput(Context);
}

void FInputRouter::RouteKeyboardInput(EKeyInputType Type, int VK)
{
	UpdateControllerModifiers();

	if (WorldType == EWorldType::PIE)
	{
		if (Type == EKeyInputType::KeyPressed && IsPIESpecialKey(VK))
		{
			if (PIEController)
				PIEController->OnKeyPressed(VK);
			return;
		}

		if (GamePlayerController)
		{
			switch (Type)
			{
			case EKeyInputType::KeyPressed:
				GamePlayerController->OnKeyPressed(VK);
				break;
			case EKeyInputType::KeyDown:
				GamePlayerController->OnKeyDown(VK);
				break;
			case EKeyInputType::KeyReleased:
				GamePlayerController->OnKeyReleased(VK);
				break;
			default:
				break;
			}
		}
		return;
	}

	if (IInputController* Controller = GetActiveController())
	{
		switch (Type)
		{
		case EKeyInputType::KeyPressed:
			Controller->OnKeyPressed(VK);
			break;
		case EKeyInputType::KeyDown:
			Controller->OnKeyDown(VK);
			break;
		case EKeyInputType::KeyReleased:
			Controller->OnKeyReleased(VK);
			break;
		default:
			break;
		}
	}
}

void FInputRouter::RouteMouseInput(EMouseInputType Type, float DeltaX, float DeltaY)
{
	UpdateControllerModifiers();

	IInputController* Controller = GetActiveController();
	if (!Controller)
		return;

	switch (Type)
	{
	case EMouseInputType::E_MouseMoved:
		Controller->OnMouseMove(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_MouseMovedAbsolute:
		Controller->OnMouseMoveAbsolute(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_LeftMouseClicked:
		Controller->OnLeftMouseClick(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_LeftMouseDragEnded:
		Controller->OnLeftMouseDragEnd(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_LeftMouseButtonUp:
		Controller->OnLeftMouseButtonUp(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_RightMouseClicked:
		Controller->OnRightMouseClick(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_LeftMouseDragged:
		Controller->OnLeftMouseDrag(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_RightMouseDragged:
		Controller->OnRightMouseDrag(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_MiddleMouseDragged:
		Controller->OnMiddleMouseDrag(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_MouseWheelScrolled:
		Controller->OnWheelScrolled(DeltaX);
		break;
	default:
		break;
	}
}

void FInputRouter::SetViewportDim(float X, float Y, float Width, float Height)
{
	if (EditorWorldController)
		EditorWorldController->SetViewportDim(X, Y, Width, Height);
	if (PIEController)
		PIEController->SetViewportDim(X, Y, Width, Height);
	if (GamePlayerController)
		GamePlayerController->SetViewportDim(X, Y, Width, Height);
}

POINT FInputRouter::GetMousePos()
{
	return InputSystem::Get().GetMousePos();
}

bool FInputRouter::GetKey(int VK)
{
	return InputSystem::Get().GetKey(VK);
}

bool FInputRouter::GetKeyDown(int VK)
{
	return InputSystem::Get().GetKeyDown(VK);
}

bool FInputRouter::GetKeyUp(int VK)
{
	return InputSystem::Get().GetKeyUp(VK);
}

bool FInputRouter::GetRightDragging()
{
	return InputSystem::Get().GetRightDragging();
}

bool FInputRouter::GetMiddleDragging()
{
	return InputSystem::Get().GetMiddleDragging();
}

bool FInputRouter::GetLeftDragging()
{
	return InputSystem::Get().GetLeftDragging();
}

bool FInputRouter::GetLeftDragEnd()
{
	return InputSystem::Get().GetLeftDragEnd();
}

int FInputRouter::MouseDeltaX()
{
	return InputSystem::Get().MouseDeltaX();
}

int FInputRouter::MouseDeltaY()
{
	return InputSystem::Get().MouseDeltaY();
}

bool FInputRouter::MouseMoved()
{
	return InputSystem::Get().MouseMoved();
}

float FInputRouter::GetScrollNotches()
{
	return InputSystem::Get().GetScrollNotches();
}

FGuiInputState& FInputRouter::GetGuiInputState()
{
	return InputSystem::Get().GetGuiInputState();
}

void FInputRouter::SetCursorVisibility(bool bVisible)
{
	InputSystem::Get().SetCursorVisibility(bVisible);
}

void FInputRouter::LockMouse(bool bLock, float X, float Y, float Width, float Height)
{
	InputSystem::Get().LockMouse(bLock, X, Y, Width, Height);
}

void FInputRouter::TickInputSystem()
{
	InputSystem::Get().Tick();
}

void FInputRouter::SetOwnerWindow(HWND HWnd)
{
	InputSystem::Get().SetOwnerWindow(HWnd);
}

void FInputRouter::AddScrollDelta(int Delta)
{
	InputSystem::Get().AddScrollDelta(Delta);
}

void FInputRouter::UpdateControllerModifiers()
{
	const InputSystem& IS = InputSystem::Get();
	const bool bCtrl = IS.GetKey(VK_CONTROL);
	const bool bAlt = IS.GetKey(VK_MENU);
	const bool bShift = IS.GetKey(VK_SHIFT);

	if (EditorWorldController)
		EditorWorldController->SetInputModifiers(bCtrl, bAlt, bShift);
	if (PIEController)
		PIEController->SetInputModifiers(bCtrl, bAlt, bShift);
	if (GamePlayerController)
		GamePlayerController->SetInputModifiers(bCtrl, bAlt, bShift);
}

void FInputRouter::TickCursorCapture(const FInputRouteContext& Context)
{
	InputSystem& IS = InputSystem::Get();

	if (WorldType == EWorldType::Editor)
	{
		if (IS.GetGuiInputState().bBlockViewportInput)
		{
			return;
		}

		const bool bDragBegin = (IS.GetKeyDown(VK_RBUTTON) && !IS.GetKey(VK_CONTROL)) || IS.GetKeyDown(VK_MBUTTON);
		const bool bDragEnd = IS.GetKeyUp(VK_RBUTTON) || IS.GetKeyUp(VK_MBUTTON);

		if (bDragBegin)
		{
			IS.SetCursorVisibility(false);
			LockCursorToContextViewport(Context);
		}
		else if (bDragEnd)
		{
			IS.SetCursorVisibility(true);
			IS.LockMouse(false);
		}
		return;
	}

	if (WorldType == EWorldType::PIE)
	{
		if (Context.bControlLocked)
		{
			IS.SetCursorVisibility(true);
			IS.LockMouse(false);
		}
		else
		{
			IS.SetCursorVisibility(false);
			LockCursorToContextViewport(Context);
		}
		return;
	}

	if (WorldType == EWorldType::Game)
	{
		if (!Context.bInputActive || !Context.Window || !Context.Window->GetHWND())
		{
			IS.SetCursorVisibility(true);
			IS.LockMouse(false);
			return;
		}

		if (GetForegroundWindow() == Context.Window->GetHWND())
		{
			IS.SetCursorVisibility(false);
			LockCursorToContextViewport(Context);
		}
		else
		{
			IS.SetCursorVisibility(true);
			IS.LockMouse(false);
		}
	}
}

void FInputRouter::TickKeyboardInput(const FInputRouteContext& Context)
{
	const InputSystem& IS = InputSystem::Get();
	if (IS.GetGuiInputState().bBlockViewportInput || IS.GetGuiInputState().bUsingKeyboard)
	{
		return;
	}

	if (WorldType == EWorldType::Game && !Context.bHasActiveCamera && IS.GetKeyDown(VK_F4))
	{
		RouteKeyboardInput(EKeyInputType::KeyPressed, VK_F4);
		return;
	}

	if (WorldType == EWorldType::Game && !Context.bHasActiveCamera && !Context.bInputActive)
	{
		return;
	}

	if (Context.bControlLocked)
	{
		return;
	}

	for (int VK : InputKeys)
	{
		if (WorldType == EWorldType::PIE && VK == VK_ESCAPE)
		{
			if (IS.GetKeyDown(VK))
				RouteKeyboardInput(EKeyInputType::KeyPressed, VK);
			continue;
		}

		if (IS.GetKeyDown(VK))
			RouteKeyboardInput(EKeyInputType::KeyPressed, VK);
		if (IS.GetKey(VK))
			RouteKeyboardInput(EKeyInputType::KeyDown, VK);
		if (IS.GetKeyUp(VK))
			RouteKeyboardInput(EKeyInputType::KeyReleased, VK);
	}
}

void FInputRouter::TickMouseInput(const FInputRouteContext& Context)
{
	const InputSystem& IS = InputSystem::Get();
	if (IS.GetGuiInputState().bBlockViewportInput)
		return;

	if (WorldType == EWorldType::Game && !Context.bHasActiveCamera && !Context.bInputActive)
		return;

	if (Context.bControlLocked)
		return;

	POINT MousePoint = IS.GetMousePos();
	if (Context.Window)
	{
		MousePoint = Context.Window->ScreenToClientPoint(MousePoint);
	}

	const float LocalX = static_cast<float>(MousePoint.x) - static_cast<float>(Context.ViewportRect.X);
	const float LocalY = static_cast<float>(MousePoint.y) - static_cast<float>(Context.ViewportRect.Y);
	const float DeltaX = static_cast<float>(IS.MouseDeltaX());
	const float DeltaY = static_cast<float>(IS.MouseDeltaY());

	if (IS.MouseMoved())
	{
		RouteMouseInput(EMouseInputType::E_MouseMoved, DeltaX, DeltaY);
		RouteMouseInput(EMouseInputType::E_MouseMovedAbsolute, LocalX, LocalY);
	}

	if (IS.GetKeyDown(VK_RBUTTON))
		RouteMouseInput(EMouseInputType::E_RightMouseClicked, LocalX, LocalY);
	if (IS.GetRightDragging())
		RouteMouseInput(EMouseInputType::E_RightMouseDragged, DeltaX, DeltaY);
	if (IS.GetMiddleDragging())
		RouteMouseInput(EMouseInputType::E_MiddleMouseDragged, DeltaX, DeltaY);
	if (!MathUtil::IsNearlyZero(IS.GetScrollNotches()))
		RouteMouseInput(EMouseInputType::E_MouseWheelScrolled, IS.GetScrollNotches(), 0.0f);

	if (IS.GetKeyDown(VK_LBUTTON))
		RouteMouseInput(EMouseInputType::E_LeftMouseClicked, LocalX, LocalY);
	if (IS.GetLeftDragging())
		RouteMouseInput(EMouseInputType::E_LeftMouseDragged, LocalX, LocalY);
	if (IS.GetLeftDragEnd())
		RouteMouseInput(EMouseInputType::E_LeftMouseDragEnded, LocalX, LocalY);
	if (IS.GetKeyUp(VK_LBUTTON) && !IS.GetLeftDragEnd())
		RouteMouseInput(EMouseInputType::E_LeftMouseButtonUp, LocalX, LocalY);
}

void FInputRouter::LockCursorToContextViewport(const FInputRouteContext& Context)
{
	if (!Context.Window || !Context.Window->GetHWND())
		return;

	POINT Origin = { Context.ViewportRect.X, Context.ViewportRect.Y };
	::ClientToScreen(Context.Window->GetHWND(), &Origin);
	InputSystem::Get().LockMouse(
		true,
		static_cast<float>(Origin.x),
		static_cast<float>(Origin.y),
		static_cast<float>(Context.ViewportRect.Width),
		static_cast<float>(Context.ViewportRect.Height));
}

IInputController* FInputRouter::GetActiveController() const
{
	switch (WorldType)
	{
	case EWorldType::Editor:
		return EditorWorldController;
	case EWorldType::PIE:
	case EWorldType::Game:
		return GamePlayerController;
	default:
		return nullptr;
	}
}

bool FInputRouter::IsPIESpecialKey(int VK) const
{
	return VK == VK_ESCAPE || VK == VK_F4;
}
