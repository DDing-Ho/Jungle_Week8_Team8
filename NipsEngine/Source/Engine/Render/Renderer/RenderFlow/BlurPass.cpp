#include "BlurPass.h"

bool FBlurPass::Initialize()
{
    return true;
}

bool FBlurPass::Release()
{
    ShadowBlurTexture.Reset();
    ShadowBlurSRV.Reset();
    ShadowBlurUAV.Reset();

	ShadowBlurTempTexture.Reset();
    ShadowBlurTempSRV.Reset();
    ShadowBlurTempUAV.Reset();

    return true;
}

bool FBlurPass::Begin(const FRenderPassContext* Context)
{
    return true;
}

bool FBlurPass::DrawCommand(const FRenderPassContext* Context)
{
    return true;
}

bool FBlurPass::End(const FRenderPassContext* Context)
{
    return true;
}
