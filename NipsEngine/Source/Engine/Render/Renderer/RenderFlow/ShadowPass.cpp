#include "ShadowPass.h"

#include "Core/ResourceManager.h"
#include "UI/EditorConsoleWidget.h"

namespace
{
    // AtlasRect(0~1 정규화 UV)를 실제 D3D viewport 픽셀 좌표로 바꿉니다.
    D3D11_VIEWPORT MakeViewportFromAtlasRect(const FVector4& AtlasRect, float AtlasResolution)
    {
        D3D11_VIEWPORT Viewport = {};
        Viewport.TopLeftX = AtlasRect.X * AtlasResolution;
        Viewport.TopLeftY = AtlasRect.Y * AtlasResolution;
        Viewport.Width    = AtlasRect.Z * AtlasResolution;
        Viewport.Height   = AtlasRect.W * AtlasResolution;
        Viewport.MinDepth = 0.0f;
        Viewport.MaxDepth = 1.0f;
        return Viewport;
    }
}

bool FShadowPass::Initialize()
{
	return true;
}

bool FShadowPass::Release()
{
    ShaderBinding.reset();
    ShadowAtlasManager.Release();
    /*SpotShadowSRV.Reset();
    SpotShadowDSVs.clear();
    SpotShadowTexture.Reset();*/

	return true;
}

bool FShadowPass::Begin(const FRenderPassContext* Context)
{
    OutSRV = PrevPassSRV;
    OutRTV = PrevPassRTV;

    if (Context == nullptr)
    {
        return false;
    }

    if (Context->RenderTargets != nullptr)
    {
        Context->RenderTargets->SpotShadowSRV = nullptr;
        Context->RenderTargets->SpotShadowCount = 0;
    }

	if (!EnsureSpotShadowResources(Context->Device))
	{
        return false;
	}
	
    return true;
}

bool FShadowPass::DrawCommand(const FRenderPassContext* Context)
{
    if (Context == nullptr || Context->RenderBus == nullptr || Context->DeviceContext == nullptr || !ShaderBinding)
    {
        return false;
    }

	// TODO : Directional


	// TODO : Spot
    const TArray<FSpotShadowConstants>& SpotShadows = Context->RenderBus->GetCastShadowSpotLights();
    if (SpotShadows.empty())
    {
        return true;
    }

    const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::Opaque);
    if (Commands.empty())
    {
        return true;
    }
    
    // 이전 프레임에 픽셀 셰이더에서 이 shadow atlas를 읽고 있었을 수 있으니,
    // depth를 다시 쓰기 전에 SRV 바인딩 끊어주기
    ID3D11ShaderResourceView* NullShadowSRV = nullptr;
    Context->DeviceContext->PSSetShaderResources(12, 1, &NullShadowSRV);
    
    // spot shadow atlas를 depth 타겟으로 바라보는 핸들
    ID3D11DepthStencilView* AtlasDSV = ShadowAtlasManager.GetSpotAtlasDSV();
    if (AtlasDSV == nullptr)
    {
        return false;
    }
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    ID3D11DepthStencilState* DepthStencilState =
        FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::Default, Context->Device);
    Context->DeviceContext->OMSetDepthStencilState(DepthStencilState, 0);
    Context->DeviceContext->OMSetRenderTargets(0, nullptr, AtlasDSV);
    
    // 매 프레임 atlas 전체를 초기화하고, 이번 프레임의 visible spot shadow들을 다시 채우기
    Context->DeviceContext->ClearDepthStencilView(AtlasDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

    // 실제로 atlas에 그린 spot shadow 개수를 기록
    uint32 RenderedSpotShadowCount = 0;

    for (const FSpotShadowConstants& SpotShadow : SpotShadows)
    {
        const D3D11_VIEWPORT ShadowViewport =
            MakeViewportFromAtlasRect(SpotShadow.AtlasRect, static_cast<float>(FShadowAtlasManager::SpotAtlasResolution));
        Context->DeviceContext->RSSetViewports(1, &ShadowViewport);
        
        ShaderBinding->SetMatrix4("LightViewProj", SpotShadow.LightViewProj);
        ShaderBinding->SetFloat("ShadowResolution", SpotShadow.ShadowResolution);
        ShaderBinding->SetFloat("ShadowBias", SpotShadow.ShadowBias);
        
        for (const FRenderCommand& Cmd : Commands)
        {
            if (Cmd.Type == ERenderCommandType::PostProcessOutline)
            {
                continue;
            }

            if (Cmd.MeshBuffer == nullptr || !Cmd.MeshBuffer->IsValid())
            {
                continue;
            }

            ID3D11Buffer* VertexBuffer = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();
            if (VertexBuffer == nullptr)
            {
                continue;
            }

            const uint32 VertexCount = Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount();
            const uint32 Stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
            if (VertexCount == 0 || Stride == 0)
            {
                continue;
            }

            uint32 Offset = 0;
            ShaderBinding->SetMatrix4("World", Cmd.PerObjectConstants.Model);
            ShaderBinding->Bind(Context->DeviceContext);

            Context->DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);

            ID3D11Buffer* IndexBuffer = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
            if (IndexBuffer != nullptr)
            {
                Context->DeviceContext->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
                Context->DeviceContext->DrawIndexed(Cmd.SectionIndexCount, Cmd.SectionIndexStart, 0);
            }
            else
            {
                Context->DeviceContext->Draw(VertexCount, 0);
            }
        }

        ++RenderedSpotShadowCount;
    }
    
    if (Context->RenderTargets != nullptr)
    {
        Context->RenderTargets->SpotShadowSRV = ShadowAtlasManager.GetSpotAtlasSRV();
        Context->RenderTargets->SpotShadowCount = RenderedSpotShadowCount;
    }

	return true;
}

bool FShadowPass::End(const FRenderPassContext* Context)
{
    if (Context != nullptr && Context->DeviceContext != nullptr)
    {
        Context->DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

        if (Context->RenderTargets != nullptr)
        {
            D3D11_VIEWPORT Viewport = {};
            Viewport.TopLeftX = 0.0f;
            Viewport.TopLeftY = 0.0f;
            Viewport.Width = Context->RenderTargets->Width;
            Viewport.Height = Context->RenderTargets->Height;
            Viewport.MinDepth = 0.0f;
            Viewport.MaxDepth = 1.0f;
            Context->DeviceContext->RSSetViewports(1, &Viewport);
        }
    }

	return true;
}

bool FShadowPass::EnsureSpotShadowResources(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }

    if (!ShadowAtlasManager.Initialize(Device))
    {
        UE_LOG("Failed to initialize spot shadow atlas manager");
        return false;
    }

    if (!ShaderBinding)
    {
        UShader* SpotShadowShader = FResourceManager::Get().GetShader("Shaders/Multipass/SpotShadowDepth.hlsl");
        if (SpotShadowShader == nullptr)
        {
            UE_LOG("Failed to find spot shadow depth shader");
            return false;
        }

        ShaderBinding = SpotShadowShader->CreateBindingInstance(Device);
        if (!ShaderBinding)
        {
            UE_LOG("Failed to create spot shadow shader binding");
            return false;
        }
    }

    return true;
}
