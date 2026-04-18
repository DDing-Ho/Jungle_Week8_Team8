#include "Material.h"
#include "Core/ResourceManager.h"

DEFINE_CLASS(UMaterialInterface, UObject)
DEFINE_CLASS(UMaterial, UMaterialInterface)
DEFINE_CLASS(UMaterialInstance, UMaterialInterface)

namespace
{
	void ApplyMaterialParam(FShaderBindingInstance& Binding, const FString& Name, const FMaterialParamValue& ParamValue)
	{
		switch (ParamValue.Type)
		{
		case EMaterialParamType::Bool:
			Binding.SetBool(Name, std::get<bool>(ParamValue.Value));
			break;
		case EMaterialParamType::Int:
			Binding.SetInt(Name, std::get<int32>(ParamValue.Value));
			break;
		case EMaterialParamType::UInt:
			Binding.SetUInt(Name, std::get<uint32>(ParamValue.Value));
			break;
		case EMaterialParamType::Float:
			Binding.SetFloat(Name, std::get<float>(ParamValue.Value));
			break;
		case EMaterialParamType::Vector2:
			Binding.SetVector2(Name, std::get<FVector2>(ParamValue.Value));
			break;
		case EMaterialParamType::Vector3:
			Binding.SetVector3(Name, std::get<FVector>(ParamValue.Value));
			break;
		case EMaterialParamType::Vector4:
			Binding.SetVector4(Name, std::get<FVector4>(ParamValue.Value));
			break;
		case EMaterialParamType::Matrix4:
			Binding.SetMatrix4(Name, std::get<FMatrix>(ParamValue.Value));
			break;
		case EMaterialParamType::Texture:
			Binding.SetTexture(Name, std::get<UTexture*>(ParamValue.Value));
			break;
		default:
			break;
		}
	}
}

ID3D11SamplerState* UMaterial::ApplyRenderStates(ID3D11DeviceContext* Context) const
{
	if (!Context)
	{
		return nullptr;
	}

	ID3D11DepthStencilState* DSState = FResourceManager::Get().GetOrCreateDepthStencilState(DepthStencilType);
	ID3D11BlendState* BlendState = FResourceManager::Get().GetOrCreateBlendState(BlendType);
	ID3D11RasterizerState* RasterizerState = FResourceManager::Get().GetOrCreateRasterizerState(RasterizerType);
	ID3D11SamplerState* Sampler = FResourceManager::Get().GetOrCreateSamplerState(SamplerType);

	Context->OMSetDepthStencilState(DSState, 0);
	Context->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);
	Context->RSSetState(RasterizerState);

	return Sampler;
}

void UMaterial::EnsureShaderBinding(ID3D11Device* Device) const
{
	if (!Shader)
	{
		ShaderBinding.reset();
		return;
	}

	if (ShaderBinding && ShaderBinding->GetShader() == Shader)
	{
		return;
	}

	ShaderBinding = Shader->CreateBindingInstance(Device);
}

void UMaterial::ApplyParams(FShaderBindingInstance& Binding, const TMap<FString, FMaterialParamValue>& Params) const
{
	for (const auto& [Name, ParamValue] : Params)
	{
		ApplyMaterialParam(Binding, Name, ParamValue);
	}
}

void UMaterial::Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus, const FPerObjectConstants* PerObject) const
{
	if (!Context || !Shader)
	{
		return;
	}

	EnsureShaderBinding(FResourceManager::Get().GetCachedDevice());
	if (!ShaderBinding)
	{
		return;
	}

	ID3D11SamplerState* Sampler = ApplyRenderStates(Context);
	ShaderBinding->SetAllSamplers(Sampler);

	if (RenderBus)
	{
		ShaderBinding->ApplyFrameParameters(*RenderBus);
	}

	if (PerObject)
	{
		ShaderBinding->ApplyPerObjectParameters(*PerObject);
	}

	ApplyParams(*ShaderBinding, MaterialParams);
	ShaderBinding->Bind(Context);
}

void UMaterialInstance::Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus, const FPerObjectConstants* PerObject) const
{
	if (!Context || !Parent || !Parent->Shader)
	{
		return;
	}

	TMap<FString, FMaterialParamValue> CombinedParams;
	Parent->GatherAllParams(CombinedParams);
	for (const auto& [Name, Value] : OverridedParams)
	{
		CombinedParams[Name] = Value;
	}

	if (!ShaderBinding || ShaderBinding->GetShader() != Parent->Shader)
	{
		ShaderBinding = Parent->Shader->CreateBindingInstance(FResourceManager::Get().GetCachedDevice());
	}

	if (!ShaderBinding)
	{
		return;
	}

	ID3D11SamplerState* Sampler = Parent->ApplyRenderStates(Context);
	ShaderBinding->SetAllSamplers(Sampler);

	if (RenderBus)
	{
		ShaderBinding->ApplyFrameParameters(*RenderBus);
	}

	if (PerObject)
	{
		ShaderBinding->ApplyPerObjectParameters(*PerObject);
	}

	Parent->ApplyParams(*ShaderBinding, CombinedParams);
	ShaderBinding->Bind(Context);
}
