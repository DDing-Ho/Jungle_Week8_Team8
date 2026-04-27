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
    DirectionalShaderBinding.reset();
    ShaderBinding.reset();
    ShadowAtlasManager.Release();

	SpotShadowVSMSRV.Reset();
    SpotShadowVSMRTVs.clear();
    SpotShadowVSMTexture.Reset();

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
        Context->RenderTargets->DirectionalShadowSRV = nullptr;
        Context->RenderTargets->SpotShadowSRV = nullptr;
        Context->RenderTargets->SpotShadowCount = 0;

        Context->RenderTargets->ShadowVSMSRV = nullptr;
    }

    if (!EnsureDirectionalShadowResources(Context->Device, MAX_CASCADE_COUNT))
    {
        return false;
    }

    if (!EnsureSpotShadowResources(Context->Device))
    {
        return false;
    }

    return true;
}

bool FShadowPass::DrawCommand(const FRenderPassContext* Context)
{
    if (Context == nullptr || Context->RenderBus == nullptr || Context->DeviceContext == nullptr)
    {
        return false;
    }

    const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::Opaque);

    // ─────────────────── Directional Shadow ───────────────────
    const FDirectionalShadowConstants* DirShadow = Context->RenderBus->GetDirectionalShadow();
    if (DirShadow != nullptr && DirectionalShaderBinding)
    {
        // 이전 프레임에 shader stage에서 directional shadow array를 읽고 있었을 수 있으니
        // depth target으로 다시 쓰기 전에 SRV 바인딩을 끊어준다.
        ID3D11ShaderResourceView* NullDirectionalShadowSRV = nullptr;
        Context->DeviceContext->PSSetShaderResources(13, 1, &NullDirectionalShadowSRV);
        Context->DeviceContext->VSSetShaderResources(13, 1, &NullDirectionalShadowSRV);

        ID3D11DepthStencilView* AtlasDSV = ShadowAtlasManager.GetDirectionalAtlasDSV();
        if (AtlasDSV == nullptr)
        {
            return false;
        }
        
        /*D3D11_VIEWPORT DirShadowViewport = {};
        DirShadowViewport.Width = static_cast<float>(DirectionalShadowResolution);
        DirShadowViewport.Height = static_cast<float>(DirectionalShadowResolution);
        DirShadowViewport.MinDepth = 0.0f;
        DirShadowViewport.MaxDepth = 1.0f;*/

        /*Context->DeviceContext->RSSetViewports(1, &DirShadowViewport);*/
        Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11DepthStencilState* DSState =
            FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::Default, Context->Device);
        Context->DeviceContext->OMSetDepthStencilState(DSState, 0);
        Context->DeviceContext->OMSetRenderTargets(0, nullptr, AtlasDSV);
        Context->DeviceContext->ClearDepthStencilView(AtlasDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

        const TArray<FDirectionalAtlasSlotDesc>& CascadeSlots = FShadowAtlasManager::GetDirectionalCascadeSlots();
        const uint32 CascadeCount = static_cast<uint32>(CascadeSlots.size());
        
        for (uint32 CascadeIndex = 0; CascadeIndex < CascadeCount; ++CascadeIndex)
        {
            const FDirectionalAtlasSlotDesc& Slot = CascadeSlots[CascadeIndex];
            const D3D11_VIEWPORT DirShadowViewport = 
                MakeViewportFromAtlasRect(Slot.AtlasRect, static_cast<float>(FShadowAtlasManager::DirectionalAtlasResolution ));
            Context->DeviceContext->RSSetViewports(1, &DirShadowViewport);
            
            /*ID3D11DepthStencilView* CascadeDSV = DirectionalShadowDSVs[CascadeIndex].Get();
            Context->DeviceContext->OMSetRenderTargets(0, nullptr, CascadeDSV);
            Context->DeviceContext->ClearDepthStencilView(CascadeDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);*/

            DirectionalShaderBinding->SetMatrix4("LightViewProj", DirShadow->LightViewProj[CascadeIndex]);

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
                DirectionalShaderBinding->SetMatrix4("World", Cmd.PerObjectConstants.Model);
                DirectionalShaderBinding->Bind(Context->DeviceContext);

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
        }

        if (Context->RenderTargets != nullptr)
        {
            Context->RenderTargets->DirectionalShadowSRV = ShadowAtlasManager.GetDirectionalAtlasSRV();
        }
    }

    // ─────────────────── Spot Shadow ───────────────────
    if (!ShaderBinding)
    {
        return true;
    }

    const TArray<FSpotShadowConstants>& SpotShadows = Context->RenderBus->GetCastShadowSpotLights();
    if (SpotShadows.empty() || Commands.empty())
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
        
        ID3D11DepthStencilView* ShadowDSV = SpotShadowDSVs[SliceIndex].Get();
        ID3D11RenderTargetView** ShadowVSMRTV = SpotShadowVSMRTVs[SliceIndex].GetAddressOf();
        Context->DeviceContext->OMSetRenderTargets(1, ShadowVSMRTV, ShadowDSV);
        Context->DeviceContext->ClearDepthStencilView(ShadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

		float ClearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        Context->DeviceContext->ClearRenderTargetView(*ShadowVSMRTV, ClearColor);
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

		Context->RenderTargets->ShadowVSMSRV = SpotShadowVSMSRV.Get();
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

bool FShadowPass::EnsureDirectionalShadowResources(ID3D11Device* Device, uint32 CascadeCount)
{
    if (Device == nullptr || CascadeCount == 0)
    {
        return false;
    }

    if (!ShadowAtlasManager.InitializeDirectionalAtlas(Device))
    {
        UE_LOG("Failed to initialize directional shadow atlas");
        return false;
    }

    if (!DirectionalShaderBinding)
    {
        UShader* DirectionalShadowShader = FResourceManager::Get().GetShader("Shaders/Multipass/DirectionalShadowDepth.hlsl");
        if (DirectionalShadowShader == nullptr)
        {
            UE_LOG("Failed to find directional shadow depth shader");
            return false;
        }

        DirectionalShaderBinding  = DirectionalShadowShader->CreateBindingInstance(Device);
        if (!DirectionalShaderBinding )
        {
            UE_LOG("Failed to create directional shadow shader binding");
            return false;
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

    SpotShadowTexture = std::move(NewTexture);
    SpotShadowDSVs = std::move(NewDSVs);
    SpotShadowSRV = std::move(NewSRV);

	// VSM
    D3D11_TEXTURE2D_DESC VSMTextureDesc = {};
    VSMTextureDesc.Width = SpotShadowResolution;
    VSMTextureDesc.Height = SpotShadowResolution;
    VSMTextureDesc.MipLevels = 1;
    VSMTextureDesc.ArraySize = MaxSpotShadowCount;
    VSMTextureDesc.Format = DXGI_FORMAT_R32G32_FLOAT; // R=depth G=depth²
    VSMTextureDesc.SampleDesc.Count = 1;
    VSMTextureDesc.SampleDesc.Quality = 0;
    VSMTextureDesc.Usage = D3D11_USAGE_DEFAULT;
    VSMTextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    VSMTextureDesc.CPUAccessFlags = 0;
    VSMTextureDesc.MiscFlags = 0;

    TComPtr<ID3D11Texture2D> NewVSMTexture;
    if (FAILED(Device->CreateTexture2D(&VSMTextureDesc, nullptr, NewVSMTexture.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow VSM texture array");
        return false;
    }

    TArray<TComPtr<ID3D11RenderTargetView>> NewRTVs;
    NewRTVs.reserve(MaxSpotShadowCount);

    for (uint32 SliceIndex = 0; SliceIndex < MaxSpotShadowCount; ++SliceIndex)
    {
        D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
        RTVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
        RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        RTVDesc.Texture2DArray.MipSlice = 0;
        RTVDesc.Texture2DArray.FirstArraySlice = SliceIndex;
        RTVDesc.Texture2DArray.ArraySize = 1;

        TComPtr<ID3D11RenderTargetView> NewRTV;
        if (FAILED(Device->CreateRenderTargetView(NewVSMTexture.Get(), &RTVDesc, NewRTV.GetAddressOf())))
        {
            UE_LOG("Failed to create spot shadow render target view");
            return false;
        }

        NewRTVs.push_back(std::move(NewRTV));
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC VSMSRVDesc = {};
    VSMSRVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    VSMSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    VSMSRVDesc.Texture2DArray.MostDetailedMip = 0;
    VSMSRVDesc.Texture2DArray.MipLevels = 1;
    VSMSRVDesc.Texture2DArray.FirstArraySlice = 0;
    VSMSRVDesc.Texture2DArray.ArraySize = MaxSpotShadowCount;

    TComPtr<ID3D11ShaderResourceView> NewVSMSRV;
    if (FAILED(Device->CreateShaderResourceView(NewVSMTexture.Get(), &VSMSRVDesc, NewVSMSRV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow VSM shader resource view");
        return false;
    }

    SpotShadowVSMTexture = std::move(NewVSMTexture);
    SpotShadowVSMRTVs = std::move(NewRTVs);
    SpotShadowVSMSRV = std::move(NewVSMSRV);

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
