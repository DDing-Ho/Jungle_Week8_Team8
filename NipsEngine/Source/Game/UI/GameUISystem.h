#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Engine/Input/IUIInputHandler.h"

struct HWND__;
struct ID3D11Device;
struct ID3D11DeviceContext;
class FRmlUiRenderInterfaceD3D11;
class FRmlUiSystemInterface;
class FRmlUiClickListener;

namespace Rml
{
	class Context;
	class ElementDocument;
}

// -------------------------------------------------------
// 게임 UI 상태 - 현재 어떤 화면을 표시할지 결정
// -------------------------------------------------------
enum class EGameUIState
{
    None,        // UI 없음
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

enum class EInteractionHintType
{
    None,
    Pickup,
    Clean,
    Inspect,
};

// -------------------------------------------------------
// GameUISystem
//   - 게임 UI 전체를 관리하는 싱글턴
//   - 에디터에서는 RenderPanelsOnly() 로 호출 (ImGui 프레임 불필요)
//   - 게임 빌드에서는 Render() 로 호출 (ImGui 프레임 직접 관리)
// -------------------------------------------------------
class GameUISystem : public IUIInputHandler
{
public:
    static GameUISystem& Get();
    ~GameUISystem();

    // 게임 빌드 전용 - RmlUi 초기화 / 해제
    void Init(HWND__* Hwnd, ID3D11Device* Device, ID3D11DeviceContext* Context);
    void Shutdown();

    // 게임 빌드 - 현재 렌더 타겟 위에 UI 렌더링 (FGameRenderPipeline 에서 호출)
    void Render(EUIRenderMode Mode);
    void RenderToCurrentTarget(EUIRenderMode Mode, int Width, int Height);

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
    void SetInteractionHint(EInteractionHintType Type);

    // 일시정지 메뉴
    static void TogglePauseMenuIfInGame();
    void SetPauseMenuOpen(bool bOpen);
    bool IsPauseMenuOpen() const { return bPauseMenuOpen; }
    bool WantsMouseCursor() const;

    // 게임 데이터 초기화 (Retry 시 호출)
    void ResetGameData();

    // getter (패널에서 사용)
    float         GetProgress()     const { return CleanProgress; }
    const char*   GetItemName()     const { return CurrentItemName.c_str(); }
    const char*   GetItemDesc()     const { return CurrentItemDesc.c_str(); }
    int           GetItemCount()    const { return ItemCount; }
    float         GetElapsedTime()  const { return ElapsedTime; }

    // 아이템 갯수 / 경과 시간 setter
    void SetItemCount(int Count);
    void SetElapsedTime(float Seconds);

    // -------------------------------------------------------
    // 대화창
    // -------------------------------------------------------
    void ShowDialogue(const char* Speaker, const char* Text);   // 즉시 표시 (큐 초기화)
    void QueueDialogue(const char* Speaker, const char* Text);  // 큐에 추가
    void HideDialogue();
    bool IsDialogueActive() const;

    // -------------------------------------------------------
    // PIE / 플레이 종료 콜백
    //   에디터: StartPlaySession 에서 StopPlaySession 바인딩
    //   게임 빌드: 콜백이 없으면 윈도우 종료 요청
    // -------------------------------------------------------
    void SetExitPlayCallback(std::function<void()> Callback);
    void RequestExitPlay();   // EndingPanel 에서 호출

    void SetStartGameCallback(std::function<void()> Callback);
    void RequestStartGame();

    bool OnUIMouseMove(float X, float Y) override;
    bool OnUIMouseButtonDown(int Button, float X, float Y) override;
    bool OnUIMouseButtonUp(int Button, float X, float Y) override;
    bool OnUIKeyDown(int VK) override;
    bool OnUIKeyUp(int VK) override;

private:
    GameUISystem() = default;

    void RenderCurrentPanel(EUIRenderMode Mode);
    void UpdateRmlUiDocument(EUIRenderMode Mode, int Width, int Height);
    bool CreateGameDocument();
    void BindRmlUiEvents();
    void SetElementVisible(const char* Id, bool bVisible);
    void SetElementText(const char* Id, const std::string& Text);
    void SetElementProperty(const char* Id, const char* Property, const std::string& Value);
    // 상태에 따라 커서/마우스 잠금을 자동으로 맞춤

    EGameUIState CurrentState        = EGameUIState::None;
    bool         bPauseMenuOpen     = false;

    bool bRmlUiInitialized = false;
    std::unique_ptr<FRmlUiSystemInterface> RmlSystemInterface;
    std::unique_ptr<FRmlUiRenderInterfaceD3D11> RmlRenderInterface;
    Rml::Context* RmlContext = nullptr;
    Rml::ElementDocument* RmlDocument = nullptr;
    ID3D11DeviceContext* D3DContext = nullptr;
    std::unique_ptr<FRmlUiClickListener> StartClickListener;
    std::unique_ptr<FRmlUiClickListener> RetryClickListener;
    std::unique_ptr<FRmlUiClickListener> ExitClickListener;
    double LastRmlUpdateTime = 0.0;

    // 게임 데이터
    float       CleanProgress    = 0.f;
    int         ItemCount        = 0;
    float       ElapsedTime      = 0.f;
    std::string CurrentItemName;
    std::string CurrentItemDesc;
    EInteractionHintType InteractionHintType = EInteractionHintType::None;

    std::function<void()> ExitPlayCallback;
    std::function<void()> StartGameCallback;
};
