#include "BlurPass.h"

#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "UI/EditorConsoleWidget.h"
#include "ShadowPass.h"

bool FBlurPass::Initialize()
{
    return true;
}

bool FBlurPass::Release()
{
    ShadowVSMInputSRV.Reset();

    ShadowBlurTempTexture.Reset();
    ShadowBlurTempSRV.Reset();
    ShadowBlurTempUAV.Reset();

	ShadowBlurFinalTexture.Reset();
    ShadowBlurFinalSRV.Reset();
    ShadowBlurFinalUAV.Reset();

	ComputeShader.Reset();
    ConstantBuffer.Reset();

    return true;
}

bool FBlurPass::Begin(const FRenderPassContext* Context)
{
	 OutSRV = PrevPassSRV;
	 OutRTV = PrevPassRTV;

	 if (Context == nullptr)
	 {
		 return false;
	 }
	 if (Context->RenderTargets != nullptr)
	 {
         ShadowVSMInputSRV = Context->RenderTargets->SpotShadowVSMSRV;
	 }

     if (!EnsureComputeShader(Context->Device))
     {
         return false;
     }
     if (!EnsureConstantBuffer(Context->Device))
     {
         return false;
     }
	 if (!EnsureShadowBlurResources(Context->Device))
	 {
		 return false;
     }

    return true;
}

bool FBlurPass::DrawCommand(const FRenderPassContext* Context)
{
    if (!ComputeShader || !ConstantBuffer || !ShadowVSMInputSRV)
        return false;

    ID3D11DeviceContext* DC = Context->DeviceContext;

    const uint32 GroupX = (SpotShadowResolution + 7) / 8;
    const uint32 GroupY = (SpotShadowResolution + 7) / 8;

    ID3D11ShaderResourceView* NullSRV = nullptr;
    ID3D11UnorderedAccessView* NullUAV = nullptr;
    ID3D11Buffer* NullCB = nullptr;

    DC->CSSetShader(ComputeShader.Get(), nullptr, 0);

    // ----------------------------------------------------------------
    // Pass 1 : Horizontal Blur
    //   Input  : ShadowVSMInputSRV  (t14)
    //   Output : ShadowBlurTempUAV  (u0)
    // ----------------------------------------------------------------
    UpdateConstantBuffer(DC, 0);

    ID3D11Buffer* CB = ConstantBuffer.Get();
    ID3D11ShaderResourceView* InSRV = ShadowVSMInputSRV.Get();
    ID3D11UnorderedAccessView* OutUAV = ShadowBlurTempUAV.Get();

    DC->CSSetConstantBuffers(10, 1, &CB);
    DC->CSSetShaderResources(14, 1, &InSRV);
    DC->CSSetUnorderedAccessViews(0, 1, &OutUAV, nullptr);

    DC->Dispatch(GroupX, GroupY, MaxSpotShadowCount);

    DC->CSSetUnorderedAccessViews(0, 1, &NullUAV, nullptr);
    DC->CSSetShaderResources(14, 1, &NullSRV);

    // ----------------------------------------------------------------
    // Pass 2 : Vertical Blur
    //   Input  : ShadowBlurTempSRV   (t14)
    //   Output : ShadowBlurFinalUAV  (u0)
    // ----------------------------------------------------------------
    UpdateConstantBuffer(DC, 1);

    ID3D11ShaderResourceView* TempSRV = ShadowBlurTempSRV.Get();
    ID3D11UnorderedAccessView* FinalUAV = ShadowBlurFinalUAV.Get();

    DC->CSSetConstantBuffers(10, 1, &CB);
    DC->CSSetShaderResources(14, 1, &TempSRV);
    DC->CSSetUnorderedAccessViews(0, 1, &FinalUAV, nullptr);

    DC->Dispatch(GroupX, GroupY, MaxSpotShadowCount);

    // 언바인딩
    DC->CSSetUnorderedAccessViews(0, 1, &NullUAV, nullptr);
    DC->CSSetShaderResources(14, 1, &NullSRV);
    DC->CSSetConstantBuffers(10, 1, &NullCB);
    DC->CSSetShader(nullptr, nullptr, 0);

    // ----------------------------------------------------------------
    // Opaque Pass로 넘길 SRV 교체
    // ----------------------------------------------------------------
    if (Context->RenderTargets != nullptr)
    {
        Context->RenderTargets->SpotShadowVSMSRV = ShadowBlurFinalSRV.Get();
    }

    return true;
}

bool FBlurPass::End(const FRenderPassContext* Context)
{
    return true;
}

bool FBlurPass::EnsureComputeShader(ID3D11Device* Device)
{
    if (ComputeShader)
    {
        return true;
    }

    TComPtr<ID3DBlob> CSBlob;
    TComPtr<ID3DBlob> ErrorBlob;
    const HRESULT CompileResult = D3DCompileFromFile(
        FPaths::ToWide("Shaders/Multipass/ShadowBlurCS.hlsl").c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "mainCS",
        "cs_5_0",
        0,
        0,
        CSBlob.GetAddressOf(),
        ErrorBlob.GetAddressOf());

    if (FAILED(CompileResult))
    {
        if (ErrorBlob)
        {
            UE_LOG("ShadowBlurPass CS Compile Error: %s", static_cast<const char*>(ErrorBlob->GetBufferPointer()));
        }
        else
        {
            UE_LOG("Failed to compile ShadowBlurPass.hlsl");
        }
        return false;
    }

    const HRESULT CreateResult = Device->CreateComputeShader(
		CSBlob->GetBufferPointer(), CSBlob->GetBufferSize(), nullptr, ComputeShader.GetAddressOf());

    if (FAILED(CreateResult))
    {
        UE_LOG("Failed to create ShadowBlurPass compute shader");
        return false;
    }

	return true;
}

bool FBlurPass::EnsureConstantBuffer(ID3D11Device* Device)
{
    if (ConstantBuffer)
    {
        return true;
    }

    D3D11_BUFFER_DESC CBDesc = {};
    CBDesc.ByteWidth = sizeof(FShadowBlurConstants);
    CBDesc.Usage = D3D11_USAGE_DYNAMIC;
    CBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    CBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    return SUCCEEDED(Device->CreateBuffer(&CBDesc, nullptr, ConstantBuffer.GetAddressOf()));
}

bool FBlurPass::EnsureShadowBlurResources(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }

	if (ShadowBlurTempTexture && ShadowBlurTempSRV && ShadowBlurTempUAV)
    {
        return true;
    }

    if (ShadowBlurFinalTexture && ShadowBlurFinalSRV && ShadowBlurFinalUAV)
    {
        return true;
    }

	D3D11_TEXTURE2D_DESC TexDesc = {};
    TexDesc.Width = SpotShadowResolution;
    TexDesc.Height = SpotShadowResolution;
    TexDesc.MipLevels = 1;
    TexDesc.ArraySize = MaxSpotShadowCount;
    TexDesc.Format = DXGI_FORMAT_R32G32_FLOAT; // R=depth, G=depth²
    TexDesc.SampleDesc.Count = 1;
    TexDesc.SampleDesc.Quality = 0;
    TexDesc.Usage = D3D11_USAGE_DEFAULT;
    TexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    TexDesc.CPUAccessFlags = 0;
    TexDesc.MiscFlags = 0;

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    SRVDesc.Texture2DArray.MostDetailedMip = 0;
    SRVDesc.Texture2DArray.MipLevels = 1;
    SRVDesc.Texture2DArray.FirstArraySlice = 0;
    SRVDesc.Texture2DArray.ArraySize = MaxSpotShadowCount;

	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    UAVDesc.Texture2DArray.MipSlice = 0;
    UAVDesc.Texture2DArray.FirstArraySlice = 0;
    UAVDesc.Texture2DArray.ArraySize = MaxSpotShadowCount;

	TComPtr<ID3D11Texture2D> NewBlurTempTexture;
    if (FAILED(Device->CreateTexture2D(&TexDesc, nullptr, NewBlurTempTexture.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur texture array");
        return false;
    }

    TComPtr<ID3D11ShaderResourceView> NewBlurTempSRV;
    if (FAILED(Device->CreateShaderResourceView(NewBlurTempTexture.Get(), &SRVDesc, NewBlurTempSRV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur shader resource view");
        return false;
    }

	TComPtr<ID3D11UnorderedAccessView> NewBlurTempUAV;
    if (FAILED(Device->CreateUnorderedAccessView(NewBlurTempTexture.Get(), &UAVDesc, NewBlurTempUAV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow unordered access view");
        return false;
    }

	ShadowBlurTempTexture = std::move(NewBlurTempTexture);
    ShadowBlurTempSRV = std::move(NewBlurTempSRV);
    ShadowBlurTempUAV = std::move(NewBlurTempUAV);

	TComPtr<ID3D11Texture2D> NewBlurFinalTexture;
    if (FAILED(Device->CreateTexture2D(&TexDesc, nullptr, NewBlurFinalTexture.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur texture array");
        return false;
    }

    TComPtr<ID3D11ShaderResourceView> NewBlurFinalSRV;
    if (FAILED(Device->CreateShaderResourceView(NewBlurFinalTexture.Get(), &SRVDesc, NewBlurFinalSRV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur shader resource view");
        return false;
    }

	TComPtr<ID3D11UnorderedAccessView> NewBlurFinalUAV;
    if (FAILED(Device->CreateUnorderedAccessView(NewBlurFinalTexture.Get(), &UAVDesc, NewBlurFinalUAV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow unordered access view");
        return false;
    }

	ShadowBlurFinalTexture = std::move(NewBlurFinalTexture);
    ShadowBlurFinalSRV = std::move(NewBlurFinalSRV);
    ShadowBlurFinalUAV = std::move(NewBlurFinalUAV);

	return true;
}

void FBlurPass::UpdateConstantBuffer(ID3D11DeviceContext* DeviceContext, uint32 BlurDirection)
{
    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (FAILED(DeviceContext->Map(ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
        return;

    FShadowBlurConstants* CB = static_cast<FShadowBlurConstants*>(Mapped.pData);
    CB->BlurDirection = BlurDirection;
    CB->SliceCount = MaxSpotShadowCount;
    CB->Pad0 = 0;
    CB->Pad1 = 0;

    DeviceContext->Unmap(ConstantBuffer.Get(), 0);
}
