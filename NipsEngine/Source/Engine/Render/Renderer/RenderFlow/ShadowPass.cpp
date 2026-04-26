#include "ShadowPass.h"

#include "Core/ResourceManager.h"
#include "UI/EditorConsoleWidget.h"

bool FShadowPass::Initialize()
{
	return true;
}

bool FShadowRenderPass::Release()
{
    ShaderBinding.reset();
    SpotShadowSRV.Reset();
    SpotShadowDSVs.clear();
    SpotShadowTexture.Reset();

	return true;
}

bool FShadowRenderPass::Begin(const FRenderPassContext* Context)
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

bool FShadowRenderPass::DrawCommand(const FRenderPassContext* Context)
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

    D3D11_VIEWPORT ShadowViewport = {};
    ShadowViewport.TopLeftX = 0.0f;
    ShadowViewport.TopLeftY = 0.0f;
    ShadowViewport.Width = static_cast<float>(SpotShadowResolution);
    ShadowViewport.Height = static_cast<float>(SpotShadowResolution);
    ShadowViewport.MinDepth = 0.0f;
    ShadowViewport.MaxDepth = 1.0f;

    Context->DeviceContext->RSSetViewports(1, &ShadowViewport);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11DepthStencilState* DepthStencilState =
        FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::Default, Context->Device);
    Context->DeviceContext->OMSetDepthStencilState(DepthStencilState, 0);

    uint32 SliceIndex = 0;
    for (const FSpotShadowConstants& SpotShadow : SpotShadows)
    {
        if (SliceIndex >= MaxSpotShadowCount || SliceIndex >= SpotShadowDSVs.size())
        {
            break;
        }

        ID3D11DepthStencilView* ShadowDSV = SpotShadowDSVs[SliceIndex].Get();
        Context->DeviceContext->OMSetRenderTargets(0, nullptr, ShadowDSV);
        Context->DeviceContext->ClearDepthStencilView(ShadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

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

        ++SliceIndex;
    }

    if (Context->RenderTargets != nullptr)
    {
        uint32 RenderedSpotShadowCount = static_cast<uint32>(SpotShadows.size());
        if (RenderedSpotShadowCount > MaxSpotShadowCount)
        {
            RenderedSpotShadowCount = MaxSpotShadowCount;
        }
        if (RenderedSpotShadowCount > SpotShadowDSVs.size())
        {
            RenderedSpotShadowCount = static_cast<uint32>(SpotShadowDSVs.size());
        }

        Context->RenderTargets->SpotShadowSRV = SpotShadowSRV.Get();
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

    if (SpotShadowTexture && SpotShadowSRV && SpotShadowDSVs.size() == MaxSpotShadowCount)
    {
        return true;
    }

    D3D11_TEXTURE2D_DESC TextureDesc = {};
    TextureDesc.Width = SpotShadowResolution;
    TextureDesc.Height = SpotShadowResolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = MaxSpotShadowCount;
    TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    TextureDesc.CPUAccessFlags = 0;
    TextureDesc.MiscFlags = 0;

    TComPtr<ID3D11Texture2D> NewTexture;
    if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, NewTexture.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow texture array");
        return false;
    }

    TArray<TComPtr<ID3D11DepthStencilView>> NewDSVs;
    NewDSVs.reserve(MaxSpotShadowCount);

    for (uint32 SliceIndex = 0; SliceIndex < MaxSpotShadowCount; ++SliceIndex)
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
        DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
        DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        DSVDesc.Flags = 0;
        DSVDesc.Texture2DArray.MipSlice = 0;
        DSVDesc.Texture2DArray.FirstArraySlice = SliceIndex;
        DSVDesc.Texture2DArray.ArraySize = 1;

        TComPtr<ID3D11DepthStencilView> NewDSV;
        if (FAILED(Device->CreateDepthStencilView(NewTexture.Get(), &DSVDesc, NewDSV.GetAddressOf())))
        {
            UE_LOG("Failed to create spot shadow depth stencil view");
            return false;
        }

        NewDSVs.push_back(std::move(NewDSV));
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    SRVDesc.Texture2DArray.MostDetailedMip = 0;
    SRVDesc.Texture2DArray.MipLevels = 1;
    SRVDesc.Texture2DArray.FirstArraySlice = 0;
    SRVDesc.Texture2DArray.ArraySize = MaxSpotShadowCount;

    TComPtr<ID3D11ShaderResourceView> NewSRV;
    if (FAILED(Device->CreateShaderResourceView(NewTexture.Get(), &SRVDesc, NewSRV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow shader resource view");
        return false;
    }

    SpotShadowTexture = std::move(NewTexture);
    SpotShadowDSVs = std::move(NewDSVs);
    SpotShadowSRV = std::move(NewSRV);

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
