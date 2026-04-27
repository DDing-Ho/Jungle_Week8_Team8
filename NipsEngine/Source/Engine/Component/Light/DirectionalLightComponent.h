#pragma once
#include "LightComponent.h"

class UDirectionalLightComponent : public ULightComponent
{
public:
    DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)

    static constexpr const char* BillboardTexturePath = "Asset/Texture/Icons/S_LightDirectional.PNG";

    UDirectionalLightComponent();
    ~UDirectionalLightComponent() override = default;

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void Serialize(FArchive& Ar) override;
    void PostDuplicate(UObject* Original) override;

	// ──────────── Cascade Shadow Map ────────────
	float GetShadowDistance() const { return ShadowDistance; }
	float GetCascadeSplitWeight() const { return CascadeSplitWeight;  }

private:
	float ShadowDistance = 3000.0f; // 빛마다 Cascade Split을 다르게 조정할 수 있으므로 static constexpr로 선언하지 않는다.
	float CascadeSplitWeight = 1.0f; // 0.0f면 선형 분할하며, 1.0f면 로그 분할(가까울수록 좁게, 멀수록 크게)한다.
};
