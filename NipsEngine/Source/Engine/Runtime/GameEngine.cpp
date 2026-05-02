#include "Engine/Runtime/GameEngine.h"

#include "Engine/UI/GameUISystem.h"
#include "Engine/Render/Renderer/GameRenderPipeline.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Render/Renderer/Renderer.h"

DEFINE_CLASS(UGameEngine, UEngine)

void UGameEngine::Init(FWindowsWindow* InWindow)
{
    // 1. 기본 엔진 초기화 (FDefaultRenderPipeline 이 세팅됨)
    UEngine::Init(InWindow);

    // 2. DefaultRenderPipeline 을 GameRenderPipeline 으로 교체
    //    (GameRenderPipeline 이 3D 렌더 + UI 렌더 + Present 를 모두 처리)
    SetRenderPipeline(std::make_unique<FGameRenderPipeline>(this, GetRenderer()));

    // 3. GameUISystem 초기화 (ImGui 컨텍스트 생성)
    GameUISystem::Get().Init(
        InWindow->GetHWND(),
        GetRenderer().GetFD3DDevice().GetDevice(),
        GetRenderer().GetFD3DDevice().GetDeviceContext()
    );
}

void UGameEngine::Shutdown()
{
    GameUISystem::Get().Shutdown();
    UEngine::Shutdown();
}
