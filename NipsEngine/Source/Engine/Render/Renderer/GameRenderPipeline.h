#pragma once

#include "IRenderPipeline.h"
#include "Render/Collector/RenderCollector.h"
#include "Render/Scene/RenderBus.h"

class UEngine;

// -------------------------------------------------------
// FGameRenderPipeline
//   - FDefaultRenderPipeline 과 동일한 3D 렌더를 수행하되
//     EndFrame(Present) 직전에 GameUISystem::Render() 를 삽입한다.
//   - DefaultRenderPipeline / UEngine 을 수정하지 않기 위해
//     UGameEngine::Init() 에서 파이프라인을 교체하는 방식으로 사용.
// -------------------------------------------------------
class FGameRenderPipeline : public IRenderPipeline
{
public:
    FGameRenderPipeline(UEngine* InEngine, FRenderer& InRenderer);
    ~FGameRenderPipeline() override;

    void Execute(float DeltaTime, FRenderer& Renderer) override;

private:
    UEngine*       Engine = nullptr;
    FRenderCollector Collector;
    FRenderBus       Bus;
};
