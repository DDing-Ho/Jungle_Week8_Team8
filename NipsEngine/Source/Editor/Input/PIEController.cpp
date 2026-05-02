#include "PIEController.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Engine/Input/InputSystem.h"
#include "Component/Physics/PhysicsHandleComponent.h"
#include "Object/Object.h"

#include <windows.h>

FPIEController::~FPIEController()
{
    DestroyPhysicsHandle();
}

void FPIEController::Tick(float InDeltaTime)
{
    DeltaTime = InDeltaTime;
    if (!Camera)
        return;
    if (!bTargetLocationInitialized)
    {
        TargetLocation = Camera->GetLocation();
        bTargetLocationInitialized = true;
    }
    constexpr float LocationLerpSpeed = 12.0f;
    const FVector   CurrentLocation = Camera->GetLocation();
    const float     LerpAlpha = MathUtil::Clamp(DeltaTime * LocationLerpSpeed, 0.0f, 1.0f);
    Camera->SetLocation(CurrentLocation + (TargetLocation - CurrentLocation) * LerpAlpha);

    if (PhysicsHandle)
    {
        PhysicsHandle->TickHandle(DeltaTime, Camera);
    }
}

void FPIEController::OnMouseMove(float DeltaX, float DeltaY)
{
    if (!Camera)
        return;

    // 마우스가 잠금 해제 상태(메뉴, 대화, 엔딩 등)이면 카메라 회전 차단
    if (!InputSystem::Get().IsMouseLocked())
        return;

    if (Camera->IsOrthographic())
    {
        // Pan: scale movement proportionally to current ortho zoom level
        const float   PanScale = Camera->GetOrthoHeight() * 0.002f;
        const FVector Right = Camera->GetEffectiveRight();
        const FVector Up = Camera->GetEffectiveUp();
        TargetLocation += Right * (-DeltaX * PanScale) + Up * (DeltaY * PanScale);
    }
    else
    {
        // Accumulate yaw/pitch and rebuild rotation quaternion
        const float RotationSpeed = 0.15f;
        Yaw += DeltaX * RotationSpeed;
        Pitch -= DeltaY * RotationSpeed;
        Pitch = MathUtil::Clamp(Pitch, -89.f, 89.f);
        UpdateCameraRotation();
    }
}

void FPIEController::OnLeftMouseClick(float X, float Y)
{
    // Does nothing for now
}

void FPIEController::OnLeftMouseDragEnd(float X, float Y)
{
    // Does nothing for now
}

void FPIEController::OnLeftMouseButtonUp(float X, float Y)
{
    // Does nothing for now
}

void FPIEController::OnRightMouseClick(float DeltaX, float DeltaY)
{
    // Does nothing for now
}

void FPIEController::OnLeftMouseDrag(float X, float Y)
{
    // Does nothing for now
}

void FPIEController::OnRightMouseDrag(float DeltaX, float DeltaY)
{
    // Does nothing for now
}

void FPIEController::OnMiddleMouseDrag(float DeltaX, float DeltaY)
{
    // Does nothing for now
}

void FPIEController::OnKeyPressed(int VK)
{
    switch (VK)
    {
    case VK_ESCAPE:
        if (OnRequestEndPIE)
            OnRequestEndPIE();
        break;
    case 'E':
        TogglePickup();
        break;
    }
}

void FPIEController::OnKeyDown(int VK)
{
    // WASD continuous movement + arrow key rotation (called every frame the key is held)
    if (!Camera)
        return;

    const float MoveSpeed = 10.f;
    FVector     Move = FVector(0, 0, 0);

    // Allow flying
    switch (VK)
    {
    case 'W': Move += Camera->GetForwardVector() *  MoveSpeed; break;
    case 'S': Move += Camera->GetForwardVector() * -MoveSpeed; break;
    case 'D': Move += Camera->GetRightVector()   *  MoveSpeed; break;
    case 'A': Move += Camera->GetRightVector()   * -MoveSpeed; break;
    }

    if (Move.X != 0.f || Move.Y != 0.f)
    {
        Move *= DeltaTime;
        TargetLocation += Move;
    }

    // Arrow key rotation
    constexpr float AngleVelocity = 60.f;
    bool            bRotationChanged = false;
    switch (VK)
    {
    case VK_LEFT:  Yaw   -= AngleVelocity * DeltaTime; bRotationChanged = true; break;
    case VK_RIGHT: Yaw   += AngleVelocity * DeltaTime; bRotationChanged = true; break;
    case VK_UP:    Pitch += AngleVelocity * DeltaTime; bRotationChanged = true; break;
    case VK_DOWN:  Pitch -= AngleVelocity * DeltaTime; bRotationChanged = true; break;
    }
    if (bRotationChanged)
    {
        Pitch = MathUtil::Clamp(Pitch, -89.f, 89.f);
        UpdateCameraRotation();
    }
}

void FPIEController::OnKeyReleased(int VK)
{
    // Nothing to do here for now
}

void FPIEController::OnWheelScrolled(float Notch)
{
    // Does nothing for now
}

void FPIEController::SetCamera(FViewportCamera* InCamera)
{
    if (!InCamera)
        return;
    Camera = InCamera;

    TargetLocation = Camera->GetLocation();
    bTargetLocationInitialized = true;

    // Initialize Yaw/Pitch from camera's current orientation so the first
    // mouse move does not snap the camera to a default forward direction.
    FVector Forward = Camera->GetForwardVector();
    Pitch = MathUtil::RadiansToDegrees(std::asin(MathUtil::Clamp(Forward.Z, -1.f, 1.f)));
    Yaw   = MathUtil::RadiansToDegrees(std::atan2(Forward.Y, Forward.X));
}

void FPIEController::SetCamera(FViewportCamera& InCamera) { SetCamera(&InCamera); }

void FPIEController::NullifyCamera()
{
    if (PhysicsHandle)
    {
        PhysicsHandle->Release();
    }
    Camera = nullptr;
}

void FPIEController::UpdateCameraRotation()
{
    if (!Camera)
        return;

    const float PitchRad = MathUtil::DegreesToRadians(Pitch);
    const float YawRad   = MathUtil::DegreesToRadians(Yaw);

    FVector Forward(
        std::cos(PitchRad) * std::cos(YawRad),
        std::cos(PitchRad) * std::sin(YawRad),
        std::sin(PitchRad));
    Forward = Forward.GetSafeNormal();

    FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
    if (Right.IsNearlyZero())
        return;

    FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

    FMatrix RotMat = FMatrix::Identity;
    RotMat.SetAxes(Forward, Right, Up);

    FQuat NewRotation(RotMat);
    NewRotation.Normalize();
    Camera->SetRotation(NewRotation);
}

void FPIEController::ResetTargetLocation()
{
    if (Camera)
        TargetLocation = Camera->GetLocation();
}

UPhysicsHandleComponent* FPIEController::GetPhysicsHandle()
{
    if (PhysicsHandle == nullptr)
    {
        PhysicsHandle = UObjectManager::Get().CreateObject<UPhysicsHandleComponent>();
        PhysicsHandle->SetTransient(true);
    }

    return PhysicsHandle;
}

void FPIEController::DestroyPhysicsHandle()
{
    if (PhysicsHandle)
    {
        PhysicsHandle->Release();
        UObjectManager::Get().DestroyObject(PhysicsHandle);
        PhysicsHandle = nullptr;
    }
}

void FPIEController::TogglePickup()
{
    UPhysicsHandleComponent* Handle = GetPhysicsHandle();
    if (Handle == nullptr)
    {
        return;
    }

    if (Handle->IsHolding())
    {
        Handle->Release();
        return;
    }

    Handle->TryGrab(World, Camera);
}
