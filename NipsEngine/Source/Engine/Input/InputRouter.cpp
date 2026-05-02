#include "Engine/Input/InputRouter.h"

#include "Engine/Input/IInputController.h"

#include <windows.h>

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

void FInputRouter::RouteKeyboardInput(EKeyInputType Type, int VK)
{
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
