#include "Editor/Viewport/EditorViewportClient.h"

#include "Editor/UI/EditorConsoleWidget.h"
#include "Editor/Settings/EditorSettings.h"
#include "Engine/Slate/SlateApplication.h"
#include "EditorEngine.h"

#include "GameFramework/World.h"
#include "Component/GizmoComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Object/Object.h"
#include "Object/ActorIterator.h"
#include "Editor/Selection/SelectionManager.h"
#include "Runtime/SceneView.h"
#include "Utility/EditorUIUtils.h"
#include "Math/Vector4.h"
#include "Slate/SWidget.h"
#include <algorithm>
#include <unordered_set>
#include "GameFramework/PrimitiveActors.h"
#include "Component/StaticMeshComponent.h"

void FEditorViewportClient::Initialize(FWindowsWindow* InWindow, UEditorEngine* InEditor)
{
	FViewportClient::Initialize(InWindow);
	Editor = InEditor;
	EditorWorldController.SetStartPIECallback([this]()
											  {
		if (Editor)
			Editor->StartPlaySession(); });
	PIEController.SetToggleInputCaptureCallback([this]()
												{ TogglePIEInputCapture(); });
	InputRouter.SetEditorWorldController(&EditorWorldController);
	InputRouter.SetPIEController(&PIEController);
	InputRouter.SetGamePlayerController(&GamePlayerController);
	InputRouter.SetWorldType(EWorldType::Editor);
}

void FEditorViewportClient::SetWorld(UWorld* InWorld)
{
	World = InWorld;
	EditorWorldController.SetWorld(InWorld);
	InputRouter.SetWorldType(InWorld ? InWorld->GetWorldType() : EWorldType::Editor);
}

void FEditorViewportClient::StartPIE(UWorld* InWorld)
{
	World = InWorld;
	if (APlayerStartActor* PlayerStart = InWorld ? InWorld->FindPlayerStart() : nullptr)
	{
		Camera.SetProjectionType(EViewportProjectionType::Perspective);
		Camera.ClearCustomLookDir();
		Camera.SetLocation(PlayerStart->GetActorLocation());
		Camera.SetRotation(FRotator::MakeFromEuler(PlayerStart->GetActorRotation()));
	}

	GamePlayerController.SetCamera(nullptr);
	GamePlayerController.SetFreeCamera(&Camera);
	if (bHasCameraSnapshot)
	{
		GamePlayerController.InitializeFreeCameraFromSnapshot(SavedCamera);
	}
	InputRouter.SetWorldType(EWorldType::PIE);
}

void FEditorViewportClient::EndPIE(UWorld* InWorld)
{
	World = InWorld;
	EditorWorldController.SetTargetLocation(Camera.GetLocation());
	EditorWorldController.SetWorld(InWorld);
	InputRouter.SetWorldType(EWorldType::Editor);
	EditorWorldController.ResetTargetLocation();
	GamePlayerController.SetCamera(nullptr);
	GamePlayerController.SetFreeCamera(nullptr);
	ClearEndPIECallback();
	FInputRouter::LockMouse(false);
	bControlLocked = false;
}

void FEditorViewportClient::SetSelectionManager(FSelectionManager* InSelectionManager)
{
	SelectionManager = InSelectionManager;
	EditorWorldController.SetSelectionManager(InSelectionManager);
}

void FEditorViewportClient::CreateCamera()
{
	bHasCamera = true;
	Camera = FViewportCamera();
	Camera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
	EditorWorldController.SetCamera(&Camera);
	GamePlayerController.SetFreeCamera(&Camera);
	EditorWorldController.ResetTargetLocation();
}

void FEditorViewportClient::DestroyCamera()
{
	bHasCamera = false;
	EditorWorldController.NullifyCamera();
	GamePlayerController.SetFreeCamera(nullptr);
}

void FEditorViewportClient::ResetCamera()
{
	if (!bHasCamera || !Settings)
		return;

	Camera.SetLocation(Settings->InitViewPos);

	const FVector Forward = (Settings->InitLookAt - Settings->InitViewPos).GetSafeNormal();
	if (!Forward.IsNearlyZero())
	{
		FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
		if (!Right.IsNearlyZero())
		{
			FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();
			FMatrix RotationMatrix = FMatrix::Identity;
			RotationMatrix.SetAxes(Forward, Right, Up);

			FQuat NewRotation(RotationMatrix);
			NewRotation.Normalize();
			Camera.SetRotation(NewRotation);
		}
	}
	EditorWorldController.ResetTargetLocation();
}

void FEditorViewportClient::SetViewportSize(float InWidth, float InHeight)
{
	FViewportClient::SetViewportSize(InWidth, InHeight);

	if (bHasCamera)
		Camera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
}

void FEditorViewportClient::Tick(float DeltaTime)
{
	if (State && !State->bHovered)
		return;

	if (bHasCamera && Settings)
	{
		FEditorWorldController& Controller = EditorWorldController;
		Controller.SetMoveSpeed(Settings->CameraSpeed);
		Controller.SetMoveSensitivity(Settings->CameraMoveSensitivity);
		Controller.SetRotateSensitivity(Settings->CameraRotateSensitivity);
		Controller.SetZoomSpeed(Settings->CameraZoomSpeed);
	}

	FInputRouteContext RouteContext;
	RouteContext.Window = Window;
	RouteContext.ViewportRect = Viewport ? Viewport->GetRect() : FViewportRect(0, 0, static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	RouteContext.bHovered = State ? State->bHovered : true;
	RouteContext.bControlLocked = bControlLocked;
	InputRouter.Tick(DeltaTime, RouteContext);

	TickInteraction(DeltaTime);
}

void FEditorViewportClient::BuildSceneView(FSceneView& OutView) const
{
	if (!bHasCamera)
		return;

	if (InputRouter.GetWorldType() == EWorldType::PIE)
	{
		const FViewportRect Rect = State && Viewport
									   ? Viewport->GetRect()
									   : FViewportRect(0, 0, static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
		GamePlayerController.BuildSceneView(OutView, Rect, State ? State->ViewMode : EViewMode::Lit);
		return;
	}

	OutView.ViewMatrix = Camera.GetViewMatrix();
	OutView.ProjectionMatrix = Camera.GetProjectionMatrix();
	OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

	OutView.CameraPosition = Camera.GetLocation();
	OutView.CameraForward = Camera.GetForwardVector();
	OutView.CameraRight = Camera.GetRightVector();
	OutView.CameraUp = Camera.GetUpVector();

	OutView.NearPlane = Camera.GetNearPlane();
	OutView.FarPlane = Camera.GetFarPlane();

	OutView.bOrthographic = Camera.IsOrthographic();

	OutView.CameraOrthoHeight = Camera.GetOrthoHeight();

	OutView.CameraFrustum = Camera.GetFrustum();

	if (State)
	{
		OutView.ViewRect = Viewport->GetRect();
		OutView.ViewMode = State->ViewMode;
	}
}

void FEditorViewportClient::ApplyCameraMode()
{
	// Orthographic views reset rotation so the existing value doesn't interfere with LookAt.
	Camera.SetRotation(FRotator(0.f, 0.f, 0.f));

	switch (ViewportType)
	{
	case EVT_Perspective:
		Camera.SetProjectionType(EViewportProjectionType::Perspective);
		Camera.ClearCustomLookDir();
		Camera.SetLocation(FVector(5.f, 3.f, 5.f));
		Camera.SetLookAt(FVector(0.f, 0.f, 0.f));
		break;

		// Orthographic views (X=Forward, Y=Right, Z=Up)

	case EVT_OrthoTop: // top-down (-Z), screen-up = +X
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(0.f, 0.f, 1000.f));
		Camera.SetCustomLookDir(FVector(0.f, 0.f, -1.f), FVector(1.f, 0.f, 0.f));
		break;

	case EVT_OrthoBottom: // bottom-up (+Z), screen-up = +X
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(0.f, 0.f, -1000.f));
		Camera.SetCustomLookDir(FVector(0.f, 0.f, 1.f), FVector(1.f, 0.f, 0.f));
		break;

	case EVT_OrthoFront: // front (-X->+X), screen-up = +Z
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(1000.f, 0.f, 0.f));
		Camera.SetCustomLookDir(FVector(-1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	case EVT_OrthoBack: // back (+X->-X), screen-up = +Z
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(-1000.f, 0.f, 0.f));
		Camera.SetCustomLookDir(FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	case EVT_OrthoLeft: // left (-Y->+Y), screen-up = +Z
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(0.f, -1000.f, 0.f));
		Camera.SetCustomLookDir(FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	case EVT_OrthoRight: // right (+Y->-Y), screen-up = +Z
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(0.f, 1000.f, 0.f));
		Camera.SetCustomLookDir(FVector(0.f, -1.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	default:
		break;
	}

	// Reset lerp target immediately so accumulated TargetLocation doesn't
	// move the camera on the next Tick after a mode switch.
	EditorWorldController.ResetTargetLocation();
}

bool FEditorViewportClient::IsActiveOperation() const
{
	return FInputRouter::GetRightDragging() || FInputRouter::GetMiddleDragging();
}

// ── Input tick sub-steps ──────────────────────────────────────────────────────

void FEditorViewportClient::TogglePIEInputCapture()
{
	if (!bControlLocked)
	{
		bControlLocked = true;
		FInputRouter::SetCursorVisibility(true);
		FInputRouter::LockMouse(false);
	}
	else
	{
		bControlLocked = false;
	}
}

// ── Interaction (gizmo scaling + box selection) ───────────────────────────────

void FEditorViewportClient::TickInteraction(float DeltaTime)
{
	(void)DeltaTime;

	if (!bHasCamera || !Gizmo)
		return;

	if (World && World->GetWorldType() == EWorldType::PIE)
		return;

	// Gizmo screen-space scaling must happen every frame.
	if (Camera.IsOrthographic())
		Gizmo->ApplyScreenSpaceScalingOrtho(Camera.GetOrthoHeight());
	else
		Gizmo->ApplyScreenSpaceScaling(Camera.GetLocation());

	if (!World || !SelectionManager)
		return;

	// ── Box selection (Ctrl+Alt+LMB drag) ────────────────────────────────────
	POINT MousePoint = FInputRouter::GetMousePos();

	if (bBoxSelecting)
	{
		const FGuiInputState& GuiState = FInputRouter::GetGuiInputState();
		if (!GuiState.IsInViewportHost(MousePoint.x, MousePoint.y))
		{
			bBoxSelecting = false;
			return;
		}
	}

	if (Window)
		MousePoint = Window->ScreenToClientPoint(MousePoint);
	const float VX = State ? static_cast<float>(Viewport->GetRect().X) : 0.f;
	const float VY = State ? static_cast<float>(Viewport->GetRect().Y) : 0.f;
	const float LocalX = static_cast<float>(MousePoint.x) - VX;
	const float LocalY = static_cast<float>(MousePoint.y) - VY;

	if (bBoxSelecting && (LocalX < 0.f || LocalY < 0.f || LocalX > WindowWidth || LocalY > WindowHeight))
	{
		bBoxSelecting = false;
		return;
	}

	const bool bCtrlDown = FInputRouter::GetKey(VK_CONTROL);
	const bool bAltDown = FInputRouter::GetKey(VK_MENU);

	if (FInputRouter::GetKeyDown(VK_LBUTTON) && bCtrlDown && bAltDown)
	{
		bBoxSelecting = true;
		BoxSelectStart = POINT{ static_cast<LONG>(LocalX), static_cast<LONG>(LocalY) };
		BoxSelectEnd = BoxSelectStart;
		return;
	}

	if (bBoxSelecting)
	{
		if (FInputRouter::GetLeftDragging())
			BoxSelectEnd = POINT{ static_cast<LONG>(LocalX), static_cast<LONG>(LocalY) };
		else if (FInputRouter::GetLeftDragEnd())
		{
			HandleBoxSelection();
			bBoxSelecting = false;
		}
		else if (FInputRouter::GetKeyUp(VK_LBUTTON))
			bBoxSelecting = false;
	}
}

bool FEditorViewportClient::TryProjectWorldToViewport(const FVector& WorldPos, float& OutViewportX, float& OutViewportY, float& OutDepth) const
{
	const FVector4 Clip = FMatrix::Identity.TransformVector4(FVector4(WorldPos, 1.0f), Camera.GetViewProjectionMatrix());
	if (MathUtil::IsNearlyZero(Clip.W))
		return false;

	const float InvW = 1.0f / Clip.W;
	const float NdcX = Clip.X * InvW;
	const float NdcY = Clip.Y * InvW;
	const float NdcZ = Clip.Z * InvW;
	if (NdcX < -1.0f || NdcX > 1.0f || NdcY < -1.0f || NdcY > 1.0f)
		return false;

	OutViewportX = (NdcX * 0.5f + 0.5f) * WindowWidth;
	OutViewportY = (1.0f - (NdcY * 0.5f + 0.5f)) * WindowHeight;
	OutDepth = NdcZ;
	return true;
}

void FEditorViewportClient::HandleBoxSelection()
{
	if (!SelectionManager || !World)
		return;

	const int32 MinX = std::min(BoxSelectStart.x, BoxSelectEnd.x);
	const int32 MinY = std::min(BoxSelectStart.y, BoxSelectEnd.y);
	const int32 MaxX = std::max(BoxSelectStart.x, BoxSelectEnd.x);
	const int32 MaxY = std::max(BoxSelectStart.y, BoxSelectEnd.y);
	const int32 Width = MaxX - MinX;
	const int32 Height = MaxY - MinY;

	if (Width < 2 || Height < 2)
		return;

	if (!FInputRouter::GetKey(VK_SHIFT))
		SelectionManager->ClearSelection();

	TArray<UPrimitiveComponent*> CandidatePrimitives;
	World->GetSpatialIndex().FrustumQueryPrimitives(Camera.GetFrustum(), CandidatePrimitives, FrustumQueryScratch);

	std::unordered_set<AActor*> SeenActors;
	SeenActors.reserve(CandidatePrimitives.size());

	for (UPrimitiveComponent* Primitive : CandidatePrimitives)
	{
		AActor* Actor = (Primitive != nullptr) ? Primitive->GetOwner() : nullptr;
		if (!Actor || !Actor->GetRootComponent())
			continue;

		if (!SeenActors.insert(Actor).second)
			continue;

		float ViewportX = 0.f, ViewportY = 0.f, Depth = 0.f;
		if (!TryProjectWorldToViewport(Actor->GetActorLocation(), ViewportX, ViewportY, Depth))
			continue;

		if (Depth < 0.f || Depth > 1.f)
			continue;

		const int32 Px = static_cast<int32>(ViewportX);
		const int32 Py = static_cast<int32>(ViewportY);
		if (Px >= MinX && Px <= MaxX && Py >= MinY && Py <= MaxY)
			SelectionManager->AddSelect(Actor);
	}
}

bool FEditorViewportClient::RequestActorPlacement(float X, float Y, float PopupX, float PopupY)
{
	if (!World || !bHasCamera)
		return false;

	FRay Ray = Camera.DeprojectScreenToWorld(X, Y, WindowWidth, WindowHeight);

	FHitResult BestHit{};
	bool bHasHit = false;
	float ClosestDist = FLT_MAX;

	FWorldSpatialIndex::FPrimitiveRayQueryScratch RayQueryScratch;
	TArray<UPrimitiveComponent*> CandidatePrimitives;
	TArray<float> CandidateTs;
	World->GetSpatialIndex().RayQueryPrimitives(Ray, CandidatePrimitives, CandidateTs, RayQueryScratch);

	for (int32 CandidateIndex = 0; CandidateIndex < static_cast<int32>(CandidatePrimitives.size()); ++CandidateIndex)
	{
		if (CandidateTs[CandidateIndex] > ClosestDist)
			break;

		UPrimitiveComponent* PrimitiveComp = CandidatePrimitives[CandidateIndex];
		if (!PrimitiveComp)
			continue;

		FHitResult HitResult{};
		if (PrimitiveComp->Raycast(Ray, HitResult) && HitResult.Distance < ClosestDist)
		{
			ClosestDist = HitResult.Distance;
			BestHit = HitResult;
			bHasHit = true;
		}
	}

	if (!bHasHit)
		return false;

	PendingActorPlacementLocation = BestHit.Location + BestHit.Normal;
	PendingActorPlacementPopupPos = { static_cast<LONG>(PopupX), static_cast<LONG>(PopupY) };
	bPendingActorPlacement = true;
	return true;
}

void FEditorViewportClient::FocusPrimarySelection()
{
	if (!SelectionManager || !bHasCamera)
		return;

	AActor* Primary = SelectionManager->GetPrimarySelection();
	if (!Primary)
		return;

	const FVector Target = Primary->GetActorLocation();

	if (Camera.IsOrthographic())
	{
		const FVector Forward = Camera.GetEffectiveForward().GetSafeNormal();
		float Distance = FVector::DotProduct(Camera.GetLocation() - Target, Forward);
		if (MathUtil::IsNearlyZero(Distance))
			Distance = 1000.f;
		Camera.SetLocation(Target + Forward * Distance);
	}
	else
	{
		const FVector Forward = Camera.GetForwardVector().GetSafeNormal();
		Camera.SetLocation(Target - Forward * 5.f);
		Camera.SetLookAt(Target);
	}

	EditorWorldController.ResetTargetLocation();
}

void FEditorViewportClient::DeleteSelectedActors()
{
	if (!SelectionManager)
		return;

	const TArray<AActor*> SelectedActors = SelectionManager->GetSelectedActors();
	for (AActor* Actor : SelectedActors)
	{
		if (!Actor)
			continue;
		if (UWorld* ActorWorld = Actor->GetFocusedWorld())
			ActorWorld->DestroyActor(Actor);
	}
	SelectionManager->ClearSelection();
	Editor->GetMainPanel().GetPropertyWidget().ResetSelection();
}

void FEditorViewportClient::SelectAllActors()
{
	if (!SelectionManager || !World)
		return;

	SelectionManager->ClearSelection();
	for (TActorIterator<AActor> Iter(World); Iter; ++Iter)
	{
		if (AActor* Actor = *Iter)
			SelectionManager->AddSelect(Actor);
	}
}

void FEditorViewportClient::SaveCameraSnapshot()
{
	const FViewportCamera* SnapshotSource = GetCamera();
	if (!SnapshotSource)
	{
		bHasCameraSnapshot = false;
		return;
	}

	SavedCamera.Location = SnapshotSource->GetLocation();
	SavedCamera.Rotation = SnapshotSource->GetRotation();
	SavedCamera.ProjectionType = SnapshotSource->GetProjectionType();
	SavedCamera.Width = SnapshotSource->GetWidth();
	SavedCamera.Height = SnapshotSource->GetHeight();
	SavedCamera.FOV = SnapshotSource->GetFOV();
	SavedCamera.NearPlane = SnapshotSource->GetNearPlane();
	SavedCamera.FarPlane = SnapshotSource->GetFarPlane();
	SavedCamera.OrthoHeight = SnapshotSource->GetOrthoHeight();
	bHasCameraSnapshot = true;
}

void FEditorViewportClient::RestoreCameraSnapshot()
{
	if (!bHasCamera || !bHasCameraSnapshot)
	{
		return;
	}

	Camera.ClearCustomLookDir();
	Camera.SetLocation(SavedCamera.Location);
	Camera.SetRotation(SavedCamera.Rotation);
	Camera.SetProjectionType(SavedCamera.ProjectionType);
	Camera.OnResize(SavedCamera.Width, SavedCamera.Height);
	Camera.SetFOV(SavedCamera.FOV);
	Camera.SetNearPlane(SavedCamera.NearPlane);
	Camera.SetFarPlane(SavedCamera.FarPlane);
	Camera.SetOrthoHeight(SavedCamera.OrthoHeight);
	EditorWorldController.ResetTargetLocation();
}
