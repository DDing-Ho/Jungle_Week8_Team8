#include "Game/Render/GameRenderPipeline.h"

#include "Game/GameEngine.h"
#include "Game/Viewport/GameViewportClient.h"
#include "Render/Renderer/Renderer.h"
#include "GameFramework/World.h"
#include "Runtime/SceneView.h"
#include "Core/Logging/Stats.h"
#include "Core/Logging/GPUProfiler.h"

FGameRenderPipeline::FGameRenderPipeline(UGameEngine* InEngine, FRenderer& InRenderer)
    : Engine(InEngine)
{
    Collector.Initialize(InRenderer.GetFD3DDevice().GetDevice());
}

FGameRenderPipeline::~FGameRenderPipeline()
{
    Collector.Release();
}

void FGameRenderPipeline::Execute(float DeltaTime, FRenderer& Renderer)
{
#if STATS
    FStatManager::Get().TakeSnapshot();
    FGPUProfiler::Get().TakeSnapshot();
#endif

    UWorld* World = Engine->GetWorld();
	FGameViewportClient* Viewport = Engine->GetGameViewport();
    
	if (!World || !Viewport)
		return;

    // SceneView 구성 — 카메라 행렬, 프러스텀, 뷰포트 크기
    FSceneView SceneView;
    Viewport->BuildSceneView(SceneView);

    const FViewportRect& Rect = SceneView.ViewRect;
    if (Rect.Width <= 0 || Rect.Height <= 0)
        return;

    // RenderBus 구성 (ShowFlags/Settings 기본값 사용)
    Bus.Clear();
    Bus.SetViewProjection(SceneView.ViewMatrix, SceneView.ProjectionMatrix);
    Bus.SetCameraPlane(SceneView.NearPlane, SceneView.FarPlane);

    FShowFlags ShowFlags;
    Bus.SetRenderSettings(SceneView.ViewMode, ShowFlags);
    Bus.SetFXAAEnabled(true);
    Bus.SetViewportSize(FVector2(static_cast<float>(Rect.Width), static_cast<float>(Rect.Height)));

    // 씬 수집 (기즈모/선택/그리드/에디터 오버레이 생략)
    Renderer.GetEditorLineBatcher().Clear();
    Collector.SetLineBatcher(&Renderer.GetEditorLineBatcher());
    Collector.CollectWorld(World, ShowFlags, SceneView.ViewMode, Bus, &SceneView.CameraFrustum);
    
    Renderer.PrepareBatchers(Bus);
    Renderer.BeginFrame();
    Renderer.Render(Bus);
    Renderer.EndFrame();
}
