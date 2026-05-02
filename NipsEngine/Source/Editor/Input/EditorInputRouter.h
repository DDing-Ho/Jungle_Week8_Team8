#pragma once
#include "Engine/Input/InputTypes.h"
#include "Editor/Input/EditorWorldController.h"
#include "Editor/Input/PIEController.h"

enum class EActiveEditorController
{
    EditorWorldController,
    PIEController,
    NilController,
};

class FEditorInputRouter
{
  public:
    FEditorInputRouter() = default;
    ~FEditorInputRouter() = default;

    void Tick(float DeltaTime);
    void RouteKeyboardInput(EKeyInputType Type, int VK);
    void RouteMouseInput(EMouseInputType Type, float DeltaX, float DeltaY);

    EActiveEditorController GetActiveController() { return ActiveEditorControllerState; }
    void                    SetActiveController(EActiveEditorController);
    void                    SetViewportDim(float X, float Y, float Width, float Height);

    FEditorWorldController& GetEditorWorldController() { return EditorWorldController; }
    FPIEController&         GetPIEController() { return PIEController; }

  private:
    EActiveEditorController ActiveEditorControllerState = EActiveEditorController::EditorWorldController;
    IBaseEditorController*  ActiveController = nullptr;
    FEditorWorldController  EditorWorldController;
    FPIEController          PIEController;
};
