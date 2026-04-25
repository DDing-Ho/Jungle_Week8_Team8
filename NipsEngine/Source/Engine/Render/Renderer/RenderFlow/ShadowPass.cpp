#include "ShadowPass.h"
#include "SceneLightBinding.h"
#include "Engine/Runtime/Engine.h"
#include "Component/Light/LightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Render/Renderer/RenderTarget/DepthStencilResource.h"

bool FShadowPass::Initialize()
{
    // Device는 어떻게 가져오는지 모르니 GEngine 패턴 가정
    ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();

    // ── Constant Buffer 생성 ──────────────────────────────
    D3D11_BUFFER_DESC CBDesc = {};
    CBDesc.ByteWidth = sizeof(FShadowConstants) + (16 - sizeof(FShadowConstants) % 16) % 16; // 16byte 정렬
    CBDesc.Usage = D3D11_USAGE_DYNAMIC;
    CBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    CBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT HR = Device->CreateBuffer(&CBDesc, nullptr, ShadowConstantBuffer.GetAddressOf());
    if (FAILED(HR))
        return false;

    // ── Vertex Shader 생성 ───────────────────────────────
    // ShadowDepth.hlsl 컴파일
    TComPtr<ID3DBlob> VSBlob;
    TComPtr<ID3DBlob> ErrorBlob;
    HR = D3DCompileFromFile(
        L"Shaders/ShadowDepth.hlsl", // 경로는 프로젝트에 맞게 수정
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
        0,
        VSBlob.GetAddressOf(),
        ErrorBlob.GetAddressOf());
    if (FAILED(HR))
    {
        if (ErrorBlob)
            OutputDebugStringA((char*)ErrorBlob->GetBufferPointer());
        return false;
    }

    HR = Device->CreateVertexShader(
        VSBlob->GetBufferPointer(),
        VSBlob->GetBufferSize(),
        nullptr,
        ShadowVS.GetAddressOf());
    if (FAILED(HR))
        return false;

    // ── Input Layout 생성 (Position만) ───────────────────
    D3D11_INPUT_ELEMENT_DESC LayoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    HR = Device->CreateInputLayout(
        LayoutDesc,
        ARRAYSIZE(LayoutDesc),
        VSBlob->GetBufferPointer(),
        VSBlob->GetBufferSize(),
        ShadowInputLayout.GetAddressOf());
    if (FAILED(HR))
        return false;

    return true;
}

bool FShadowPass::Release()
{
	return true;
}

bool FShadowPass::Begin(const FRenderPassContext* Context)
{
    return true;
}

bool FShadowPass::DrawCommand(const FRenderPassContext* Context)
{
    const FRenderBus* RenderBus = Context->RenderBus;
    const TArray<FRenderCommand>& Commands = RenderBus->GetCommands(ERenderPass::Opaque);

    if (Commands.empty())
        return true;
    
	const TArray<FLightSlot>& LightSlots = GEngine->GetWorld()->GetWorldLightSlots();
    const TArray<FRenderLight>& SceneLights = RenderBus->GetLights();

    for (const FLightSlot& LightSlot : LightSlots)
    {
        if (!LightSlot.LightData->IsA<UDirectionalLightComponent>())
		{
            continue;
        }

        UDirectionalLightComponent* DirLight = Cast<UDirectionalLightComponent>(LightSlot.LightData);
        FDepthStencilResource DSR = DirLight->GetDepthStencilResource();

		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        Context->DeviceContext->PSSetShaderResources(11, 1, nullSRV);
		Context->DeviceContext->OMSetRenderTargets(0, nullptr, DSR.DSV.Get());

        Context->DeviceContext->ClearDepthStencilView(
            DSR.DSV.Get(),
            D3D11_CLEAR_DEPTH,
            1.0f, 0
		);

		FMatrix LightViewProj = GetDirectionalLightViewProj(DirLight);
        DirLight->ViewProjectionMatrix = LightViewProj;

		Context->DeviceContext->VSSetShader(ShadowVS.Get(), nullptr, 0);
        Context->DeviceContext->PSSetShader(nullptr, nullptr, 0);

        // Light면 모든 Primitive에 대하여 Model -> World -> Light 변환 후 ShadowMap 생성
		for (const FRenderCommand& Cmd : Commands)
		{
            if (Cmd.Type != ERenderCommandType::StaticMesh)
                continue;

			FShadowConstants ShadowCB;
            ShadowCB.Model = Cmd.PerObjectConstants.Model;
            ShadowCB.LightViewProj = LightViewProj;
            UpdateConstantBuffer(Context, ShadowCB);

            Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Context->DeviceContext->IASetInputLayout(ShadowInputLayout.Get());

			if (Cmd.MeshBuffer == nullptr || !Cmd.MeshBuffer->IsValid())
            {
                return false;
            }

            uint32 offset = 0;
            ID3D11Buffer* vertexBuffer = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();
            if (vertexBuffer == nullptr)
            {
                return false;
            }

            uint32 vertexCount = Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount();
            uint32 stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
            if (vertexCount == 0 || stride == 0)
            {
                return false;
            }

            Context->DeviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

            ID3D11Buffer* indexBuffer = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
            if (indexBuffer != nullptr)
            {
                uint32 indexStart = Cmd.SectionIndexStart;
                uint32 indexCount = Cmd.SectionIndexCount;
                Context->DeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
                Context->DeviceContext->DrawIndexed(indexCount, indexStart, 0);
            }
            else
            {
                Context->DeviceContext->Draw(vertexCount, 0);
            }

		}
	}

	return true;
}

bool FShadowPass::End(const FRenderPassContext* Context)
{
    Context->DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	return true;
}

FMatrix FShadowPass::GetDirectionalLightViewProj(UDirectionalLightComponent* DirLight)
{
    const FVector Forward = -DirLight->GetForwardVector().GetSafeNormal();
    const FVector ActualUp = DirLight->GetUpVector().GetSafeNormal();
    FMatrix LightView = FMatrix::MakeViewLookAtLH(DirLight->GetWorldLocation(), DirLight->GetWorldLocation() + Forward, ActualUp);

	//FVector LightPos = -Forward * 100.0f; // Scene 중심에서 빛 반대 방향으로 충분히 뒤로

 //   FMatrix LightView = FMatrix::MakeViewLookAtLH(
 //       LightPos,
 //       LightPos + Forward,
 //       ActualUp);

	float AspectRatio = 16.0f / 9.0f;
    float OrthoHeight = 10.0f;
    FMatrix LightProj = FMatrix::MakeOrthographicLH
    (
        OrthoHeight * AspectRatio, // Width  - Scene 크기에 맞게
        OrthoHeight, // Height
        0.1f,  // Near
        2000.0f // Far
    );

    return LightView * LightProj;
}

void FShadowPass::UpdateConstantBuffer(const FRenderPassContext* Context, FShadowConstants ShadowCB)
{
    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    HRESULT HR = Context->DeviceContext->Map(
        ShadowConstantBuffer.Get(),
        0,
        D3D11_MAP_WRITE_DISCARD,
        0,
        &Mapped);
    if (FAILED(HR))
        return;

    memcpy(Mapped.pData, &ShadowCB, sizeof(FShadowConstants));
    Context->DeviceContext->Unmap(ShadowConstantBuffer.Get(), 0);

    // b8 슬롯에 바인딩
    ID3D11Buffer* CB = ShadowConstantBuffer.Get();
    Context->DeviceContext->VSSetConstantBuffers(8, 1, &CB);
}
