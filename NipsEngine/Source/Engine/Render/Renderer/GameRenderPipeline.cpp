#include "GameRenderPipeline.h"

#include "Renderer.h"
#include "Engine/Runtime/Engine.h"
#include "Engine/UI/GameUISystem.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "GameFramework/World.h"

FGameRenderPipeline::FGameRenderPipeline(UEngine* InEngine, FRenderer& InRenderer)
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
    // ── 1. 렌더버스 초기화 ──────────────────────────────────
    Bus.Clear();

    // ── 2. 월드·카메라 수집 (DefaultRenderPipeline 과 동일) ──
    UWorld* World  = Engine->GetWorld();
    FViewportCamera* Camera = World ? World->GetActiveCamera() : nullptr;

    if (Camera)
    {
        FShowFlags ShowFlags;
        EViewMode  ViewMode = EViewMode::Lit;

        Bus.SetViewProjection(Camera->GetViewMatrix(), Camera->GetProjectionMatrix());
        Bus.SetCameraPlane(Camera->GetNearPlane(), Camera->GetFarPlane());
        Bus.SetRenderSettings(ViewMode, ShowFlags);
        Bus.SetFXAAEnabled(true);

        Renderer.GetEditorLineBatcher().Clear();
        Collector.SetLineBatcher(&Renderer.GetEditorLineBatcher());

        const FFrustum& ViewFrustum = Camera->GetFrustum();
        Collector.CollectWorld(World, ShowFlags, ViewMode, Bus, &ViewFrustum);
    }

    // ── 3. 3D 씬 렌더 ──────────────────────────────────────
    Renderer.PrepareBatchers(Bus);
    Renderer.BeginFrame();
    Renderer.Render(Bus);

    // ── 4. 게임 UI 오버레이 (Present 전) ────────────────────
    GameUISystem::Get().Render(EUIRenderMode::Play);

    // ── 5. Present ──────────────────────────────────────────
    Renderer.EndFrame();
}
