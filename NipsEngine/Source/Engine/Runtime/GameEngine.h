#pragma once

#include "Engine/Runtime/Engine.h"

// -------------------------------------------------------
// UGameEngine
//   - 게임 빌드 전용 엔진 (WITHOUT_EDITOR 시 사용)
//   - UEngine 을 상속해 GameUISystem 초기화/해제를 추가하고
//     FDefaultRenderPipeline 대신 FGameRenderPipeline 을 사용한다.
// -------------------------------------------------------
class UGameEngine : public UEngine
{
public:
    DECLARE_CLASS(UGameEngine, UEngine)

    UGameEngine()  = default;
    ~UGameEngine() override = default;

    void Init(FWindowsWindow* InWindow) override;
    void Shutdown() override;
};
