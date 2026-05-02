#pragma once

#include <string>

struct HWND__;
struct ID3D11Device;
struct ID3D11DeviceContext;

// -------------------------------------------------------
// 게임 UI 상태 - 현재 어떤 화면을 표시할지 결정
// -------------------------------------------------------
enum class EGameUIState
{
    StartMenu,   // 시작 화면
    Prologue,    // 프롤로그
    InGame,      // 게임 중 (HUD)
    Ending,      // 엔딩
};

// -------------------------------------------------------
// 렌더 모드
//   Preview : 에디터 편집 모드 - 더미 데이터, 버튼 동작 안 함
//   Play    : PIE 또는 게임 빌드 - 실제 데이터, 버튼 동작
// -------------------------------------------------------
enum class EUIRenderMode
{
    Preview,
    Play,
};

// -------------------------------------------------------
// GameUISystem
//   - 게임 UI 전체를 관리하는 싱글턴
//   - 에디터에서는 RenderPanelsOnly() 로 호출 (ImGui 프레임 불필요)
//   - 게임 빌드에서는 Render() 로 호출 (ImGui 프레임 직접 관리)
// -------------------------------------------------------
class GameUISystem
{
public:
    static GameUISystem& Get();

    // 게임 빌드 전용 - ImGui 초기화 / 해제
    void Init(HWND__* Hwnd, ID3D11Device* Device, ID3D11DeviceContext* Context);
    void Shutdown();

    // 게임 빌드 - 전체 ImGui 프레임 처리 (FGameRenderPipeline 에서 호출)
    void Render(EUIRenderMode Mode);

    // 에디터 - 패널만 그림, ImGui 프레임은 EditorMainPanel 것을 사용
    void RenderPanelsOnly(EUIRenderMode Mode);

    // 상태 전환
    void SetState(EGameUIState NewState);
    EGameUIState GetState() const { return CurrentState; }

    // -------------------------------------------------------
    // 게임 로직에서 호출하는 데이터 setter
    // -------------------------------------------------------
    void SetProgress(float InProgress);             // 0.0 ~ 1.0
    void SetCurrentItem(const char* Name, const char* Desc);

    // getter (패널에서 사용)
    float         GetProgress()     const { return CleanProgress; }
    const char*   GetItemName()     const { return CurrentItemName.c_str(); }
    const char*   GetItemDesc()     const { return CurrentItemDesc.c_str(); }

private:
    GameUISystem() = default;

    // 현재 상태에 맞는 패널을 그린다
    void RenderCurrentPanel(EUIRenderMode Mode);

    EGameUIState CurrentState = EGameUIState::InGame;

    // ImGui 소유권 (게임 빌드에서만 true)
    bool bOwnsImGui = false;

    // 게임 데이터
    float       CleanProgress    = 0.f;
    std::string CurrentItemName;
    std::string CurrentItemDesc;
};
