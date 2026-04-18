#include "FXAARenderPass.h"
#include "Core/ResourceManager.h"
#include "Render/Scene/RenderBus.h"

bool FFXAARenderPass::Initialize()
{
    return true;
}

bool FFXAARenderPass::Release()
{
    ShaderBinding.reset();
    return true;
}

bool FFXAARenderPass::Begin(const FRenderPassContext* Context)
{
    const FRenderTargetSet* RenderTargets = Context->RenderTargets;
    ID3D11RenderTargetView* RTVs[1] = { RenderTargets->SceneFXAARTV };
    Context->DeviceContext->OMSetRenderTargets(ARRAYSIZE(RTVs), RTVs, nullptr);

    OutSRV = RenderTargets->SceneFXAASRV;
    OutRTV = RenderTargets->SceneFXAARTV;

    UShader* FXAAShader = FResourceManager::Get().GetShader("Shaders/Multipass/FXAAPass.hlsl");
    if (!FXAAShader)
    {
        return false;
    }

    if (!ShaderBinding || ShaderBinding->GetShader() != FXAAShader)
    {
        ShaderBinding = FXAAShader->CreateBindingInstance(Context->Device);
    }

    if (!ShaderBinding)
    {
        return false;
    }

    ShaderBinding->ApplyFrameParameters(*Context->RenderBus);
    ShaderBinding->SetSRV("FinalSceneColor", PrevPassSRV);
    ShaderBinding->SetVector2(
        "InvResolution",
        FVector2(
            (Context->RenderTargets->Width > 0.0f) ? (1.0f / Context->RenderTargets->Width) : 0.0f,
            (Context->RenderTargets->Height > 0.0f) ? (1.0f / Context->RenderTargets->Height) : 0.0f));
    ShaderBinding->SetUInt("Enabled", Context->RenderBus->GetFXAAEnabled() ? 1u : 0u);
    ShaderBinding->SetAllSamplers(FResourceManager::Get().GetOrCreateSamplerState(ESamplerType::EST_Linear));

    Context->DeviceContext->IASetInputLayout(nullptr);
    Context->DeviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    Context->DeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    return true;
}

bool FFXAARenderPass::DrawCommand(const FRenderPassContext* Context)
{
    if (!ShaderBinding)
    {
        return false;
    }

    ShaderBinding->Bind(Context->DeviceContext);
    Context->DeviceContext->Draw(3, 0);
    return true;
}

bool FFXAARenderPass::End(const FRenderPassContext* Context)
{
    ID3D11ShaderResourceView* NullSRVs[] = { nullptr };
    Context->DeviceContext->PSSetShaderResources(0, 1, NullSRVs);
    return true;
}
