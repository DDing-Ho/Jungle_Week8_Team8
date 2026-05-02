#pragma once

#include "Engine/Viewport/ViewportCamera.h"
#include "Engine/Viewport/ViewportClient.h"
#include "Game/Input/GameInputRouter.h"

class FWindowsWindow;
class UCameraComponent;
class UWorld;

class FGameViewportClient : public FViewportClient
{
public:
	~FGameViewportClient() override;

	void Initialize(FWindowsWindow* InWindow) override;
	void SetViewportSize(float InWidth, float InHeight) override;
	void Tick(float DeltaTime) override;
	void BuildSceneView(FSceneView& OutView) const override;

	void SetWorld(UWorld* InWorld);
	UWorld* GetFocusedWorld() const { return World; }

	void SetCamera(UCameraComponent* InCamera);
	UCameraComponent* GetCamera() const { return ActiveCamera; }

	FViewportCamera& GetFreeCamera() { return FreeCamera; }
	const FViewportCamera& GetFreeCamera() const { return FreeCamera; }

	FGameInputRouter& GetInputRouter() { return InputRouter; }
	const FGameInputRouter& GetInputRouter() const { return InputRouter; }

private:
	void TickKeyboardInput();
	void TickMouseInput();
	void UpdateControllerViewportDim();
	void UpdateCursorCapture();
	void HideMouseCursor();
	void ShowMouseCursor();
	void ConfineMouseCursorToWindow();
	void LockMouseCursor();
	void ReleaseMouseCursor();

	void ToggleInteractionMode();

private:
	UWorld* World = nullptr;
	UCameraComponent* ActiveCamera = nullptr;
	FViewportCamera FreeCamera;
	FGameInputRouter InputRouter;
	bool bInputActive = true;
	bool bCursorVisible = true;
	bool bCursorConfined = false;
};
