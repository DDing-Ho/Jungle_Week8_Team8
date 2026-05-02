#pragma once

#include "Engine/Render/Renderer/IRenderPipeline.h"
#include "Render/Collector/RenderCollector.h"
#include "Render/Scene/RenderBus.h"

class UGameEngine;

class FGameRenderPipeline : public IRenderPipeline
{
public:
    FGameRenderPipeline(UGameEngine* InEngine, FRenderer& InRenderer);
    ~FGameRenderPipeline() override;

    void Execute(float DeltaTime, FRenderer& Renderer) override;

private:
    UGameEngine* Engine = nullptr;
    FRenderCollector Collector;
    FRenderBus Bus;
};
