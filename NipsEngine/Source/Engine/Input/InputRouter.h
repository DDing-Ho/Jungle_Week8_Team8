#pragma once

#include "Engine/GameFramework/WorldContext.h"
#include "Engine/Input/InputTypes.h"

class IInputController;

class FInputRouter
{
public:
	void SetWorldType(EWorldType InWorldType) { WorldType = InWorldType; }
	EWorldType GetWorldType() const { return WorldType; }

	void SetEditorWorldController(IInputController* InController) { EditorWorldController = InController; }
	void SetPIEController(IInputController* InController) { PIEController = InController; }
	void SetGamePlayerController(IInputController* InController) { GamePlayerController = InController; }

	void Tick(float DeltaTime);
	void RouteKeyboardInput(EKeyInputType Type, int VK);
	void RouteMouseInput(EMouseInputType Type, float DeltaX, float DeltaY);
	void SetViewportDim(float X, float Y, float Width, float Height);

private:
	IInputController* GetActiveController() const;
	bool IsPIESpecialKey(int VK) const;

private:
	EWorldType WorldType = EWorldType::Editor;
	IInputController* EditorWorldController = nullptr;
	IInputController* PIEController = nullptr;
	IInputController* GamePlayerController = nullptr;
};
