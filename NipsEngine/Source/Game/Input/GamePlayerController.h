#pragma once

#include "Game/Input/BaseGameController.h"
#include "Math/Vector.h"
#include "Render/Common/ViewTypes.h"

#include <functional>

class AActor;
struct FSceneView;
class FViewportCamera;
struct FViewportRect;
struct FCameraSnapshot;
class UCameraComponent;

class FGamePlayerController : public IBaseGameController
{
public:
	FGamePlayerController() = default;
	~FGamePlayerController() override = default;

	void Tick(float DeltaTime) override;

	void OnMouseMove(float DeltaX, float DeltaY) override;
	void OnMouseMoveAbsolute(float X, float Y) override;
	void OnLeftMouseClick(float X, float Y) override;
	void OnLeftMouseDrag(float X, float Y) override;
	void OnLeftMouseDragEnd(float X, float Y) override;
	void OnLeftMouseButtonUp(float X, float Y) override;
	void OnRightMouseClick(float DeltaX, float DeltaY) override;
	void OnRightMouseDrag(float DeltaX, float DeltaY) override;
	void OnMiddleMouseDrag(float DeltaX, float DeltaY) override;
	void OnKeyPressed(int VK) override;
	void OnKeyDown(int VK) override;
	void OnKeyReleased(int VK) override;
	void OnWheelScrolled(float Notch) override;

	void SetPlayer(AActor* InPlayer) { Player = InPlayer; }
	AActor* GetPlayer() const { return Player; }

	void SetCamera(UCameraComponent* InCamera);
	UCameraComponent* GetCamera() const { return Camera; }

	void SetFreeCamera(FViewportCamera* InCamera);
	FViewportCamera* GetFreeCamera() const { return FreeCamera; }
	void InitializeFreeCameraFromSnapshot(const FCameraSnapshot& Snapshot);

	void SetMoveSpeed(float InMoveSpeed) { MoveSpeed = InMoveSpeed; }
	void SetRotateSensitivity(float InSensitivity) { RotateSensitivity = InSensitivity; }
	void SetToggleInputCaptureCallback(std::function<void()> Callback) { OnRequestToggleInputCapture = std::move(Callback); }
	void ClearToggleInputCaptureCallback() { OnRequestToggleInputCapture = nullptr; }

	void BuildSceneView(FSceneView& OutView, const FViewportRect& ViewRect, EViewMode ViewMode) const;

private:
	void RotateActiveCamera(float DeltaX, float DeltaY);
	void MoveActiveCamera(const FVector& Direction, float Scale);
	void SyncFreeCameraAngles();
	void UpdateFreeCameraRotation();

private:
	AActor* Player = nullptr;
	UCameraComponent* Camera = nullptr;
	FViewportCamera* FreeCamera = nullptr;

	float MoveSpeed = 10.0f;
	float RotateSensitivity = 0.15f;
	float FreeCameraYaw = 0.0f;
	float FreeCameraPitch = 0.0f;
	bool bFreeCameraInitialized = false;
	std::function<void()> OnRequestToggleInputCapture;
};
