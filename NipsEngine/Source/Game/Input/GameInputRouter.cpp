#include "Game/Input/GameInputRouter.h"

void FGameInputRouter::Tick(float DeltaTime)
{
	PlayerController.Tick(DeltaTime);
}

void FGameInputRouter::RouteKeyboardInput(EKeyInputType Type, int VK)
{
	switch (Type)
	{
	case EKeyInputType::KeyPressed:
		PlayerController.OnKeyPressed(VK);
		break;
	case EKeyInputType::KeyDown:
		PlayerController.OnKeyDown(VK);
		break;
	case EKeyInputType::KeyReleased:
		PlayerController.OnKeyReleased(VK);
		break;
	default:
		break;
	}
}

void FGameInputRouter::RouteMouseInput(EMouseInputType Type, float DeltaX, float DeltaY)
{
	switch (Type)
	{
	case EMouseInputType::E_MouseMoved:
		PlayerController.OnMouseMove(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_MouseMovedAbsolute:
		PlayerController.OnMouseMoveAbsolute(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_LeftMouseClicked:
		PlayerController.OnLeftMouseClick(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_LeftMouseDragged:
		PlayerController.OnLeftMouseDrag(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_LeftMouseDragEnded:
		PlayerController.OnLeftMouseDragEnd(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_LeftMouseButtonUp:
		PlayerController.OnLeftMouseButtonUp(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_RightMouseClicked:
		PlayerController.OnRightMouseClick(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_RightMouseDragged:
		PlayerController.OnRightMouseDrag(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_MiddleMouseDragged:
		PlayerController.OnMiddleMouseDrag(DeltaX, DeltaY);
		break;
	case EMouseInputType::E_MouseWheelScrolled:
		PlayerController.OnWheelScrolled(DeltaX);
		break;
	default:
		break;
	}
}
