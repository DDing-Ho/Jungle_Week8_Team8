#include "Engine/UI/GameUISystem.h"

#include "Engine/UI/StartMenuPanel.h"
#include "Engine/UI/HUDPanel.h"
#include "Engine/UI/DialoguePanel.h"
#include "Engine/UI/PauseMenuPanel.h"
#include "Engine/UI/EndingPanel.h"
#include "Engine/Input/InputSystem.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

// -------------------------------------------------------
// 싱글턴
// -------------------------------------------------------
GameUISystem& GameUISystem::Get()
{
    static GameUISystem Instance;
    return Instance;
}

// -------------------------------------------------------
// 게임 빌드 전용 초기화
// -------------------------------------------------------
void GameUISystem::Init(HWND__* Hwnd, ID3D11Device* Device, ID3D11DeviceContext* Context)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& IO = ImGui::GetIO();
    IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplWin32_Init(static_cast<void*>(Hwnd));
    ImGui_ImplDX11_Init(Device, Context);

    bOwnsImGui = true;
}

void GameUISystem::Shutdown()
{
    if (bOwnsImGui)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        bOwnsImGui = false;
    }
}

// -------------------------------------------------------
// 게임 빌드 - 전체 ImGui 프레임
// -------------------------------------------------------
void GameUISystem::Render(EUIRenderMode Mode)
{
    if (bOwnsImGui)
    {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    RenderCurrentPanel(Mode);

    if (bOwnsImGui)
    {
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
}

// -------------------------------------------------------
// 에디터 - 패널만 (ImGui 프레임은 EditorMainPanel 소유)
// -------------------------------------------------------
void GameUISystem::RenderPanelsOnly(EUIRenderMode Mode)
{
    RenderCurrentPanel(Mode);
}

// -------------------------------------------------------
// 상태 전환
// -------------------------------------------------------
void GameUISystem::SetState(EGameUIState NewState)
{
    if (NewState == EGameUIState::Ending)
        EndingPanel::Reset();

    CurrentState = NewState;
    SetPauseMenuOpen(false);      // 일시정지 해제 (내부에서 커서 복원)
    ApplyCursorForState(NewState); // 새 상태에 맞는 커서 적용
}

void GameUISystem::ApplyCursorForState(EGameUIState State)
{
    switch (State)
    {
    case EGameUIState::StartMenu:
    case EGameUIState::Prologue:
    case EGameUIState::Ending:
        // 메뉴/컷씬 구간 - 커서 표시, 마우스 잠금 해제
        InputSystem::Get().LockMouse(false);
        InputSystem::Get().SetCursorVisibility(true);
        break;

    case EGameUIState::InGame:
    {
        // 게임 플레이 구간 - 커서 숨김, PIE면 뷰포트 재잠금
        InputSystem::Get().SetCursorVisibility(false);
        const FViewportRect& VR = InputSystem::Get().GetGuiInputState().ViewportHostRect;
        if (VR.Width > 0)
            InputSystem::Get().LockMouse(
                true,
                static_cast<float>(VR.X), static_cast<float>(VR.Y),
                static_cast<float>(VR.Width), static_cast<float>(VR.Height));
        break;
    }
    }
}

// -------------------------------------------------------
// 일시정지 메뉴
// -------------------------------------------------------
void GameUISystem::SetPauseMenuOpen(bool bOpen)
{
    if (bPauseMenuOpen == bOpen) return;
    bPauseMenuOpen = bOpen;

    if (bOpen)
    {
        // 메뉴 진입 - 마우스 언락 + 커서 표시
        InputSystem::Get().LockMouse(false);
        InputSystem::Get().SetCursorVisibility(true);
    }
    else
    {
        // 메뉴 종료 - 커서 숨김 + PIE/게임 모드면 뷰포트 재잠금
        InputSystem::Get().SetCursorVisibility(false);
        const FViewportRect& VR = InputSystem::Get().GetGuiInputState().ViewportHostRect;
        if (VR.Width > 0)
        {
            InputSystem::Get().LockMouse(
                true,
                static_cast<float>(VR.X), static_cast<float>(VR.Y),
                static_cast<float>(VR.Width), static_cast<float>(VR.Height));
        }
    }
}

// -------------------------------------------------------
// 게임 데이터 초기화 (Retry)
// -------------------------------------------------------
void GameUISystem::ResetGameData()
{
    CleanProgress  = 0.f;
    ItemCount      = 0;
    ElapsedTime    = 0.f;
    CurrentItemName.clear();
    CurrentItemDesc.clear();
}

// -------------------------------------------------------
// 데이터 setter
// -------------------------------------------------------
void GameUISystem::SetProgress(float InProgress)
{
    CleanProgress = InProgress;
}

void GameUISystem::SetCurrentItem(const char* Name, const char* Desc)
{
    CurrentItemName = Name ? Name : "";
    CurrentItemDesc = Desc ? Desc : "";
}

void GameUISystem::SetItemCount(int Count)
{
    ItemCount = Count;
}

void GameUISystem::SetElapsedTime(float Seconds)
{
    ElapsedTime = Seconds;
}

// -------------------------------------------------------
// 대화창
// -------------------------------------------------------
void GameUISystem::ShowDialogue(const char* Speaker, const char* Text)
{
    DialoguePanel::Show(Speaker, Text);
}

void GameUISystem::QueueDialogue(const char* Speaker, const char* Text)
{
    DialoguePanel::Enqueue(Speaker, Text);
}

void GameUISystem::HideDialogue()
{
    DialoguePanel::Hide();
}

bool GameUISystem::IsDialogueActive() const
{
    return DialoguePanel::IsActive();
}

// -------------------------------------------------------
// PIE / 플레이 종료
// -------------------------------------------------------
void GameUISystem::SetExitPlayCallback(std::function<void()> Callback)
{
    ExitPlayCallback = std::move(Callback);
}

void GameUISystem::RequestExitPlay()
{
    if (ExitPlayCallback)
        ExitPlayCallback();
    else
        SetState(EGameUIState::StartMenu);  // 게임 빌드: 시작화면으로 복귀
}

// -------------------------------------------------------
// 현재 상태에 맞는 패널 디스패치
// -------------------------------------------------------
void GameUISystem::RenderCurrentPanel(EUIRenderMode Mode)
{
    // 첫 렌더: 초기 상태에 맞는 커서 적용 (PIE 진입 시 커서가 숨겨진 상태를 보정)
    if (bFirstRender && Mode == EUIRenderMode::Play)
    {
        bFirstRender = false;
        ApplyCursorForState(CurrentState);
    }

    // InGame 일 때 P 키로 일시정지 토글 (Play 모드에서만)
    if (Mode == EUIRenderMode::Play && CurrentState == EGameUIState::InGame)
    {
        if (InputSystem::Get().GetKeyUp(0x50))  // P
            SetPauseMenuOpen(!bPauseMenuOpen);
    }

    switch (CurrentState)
    {
    case EGameUIState::StartMenu:
        if (Mode == EUIRenderMode::Play)
            StartMenuPanel::Render(Mode);
        break;

    case EGameUIState::Prologue:
        // ProloguePanel::Render(Mode);
        break;

    case EGameUIState::InGame:
        HUDPanel::Render(Mode);
        DialoguePanel::Render(Mode);
        if (bPauseMenuOpen)
            PauseMenuPanel::Render(Mode);
        break;

    case EGameUIState::Ending:
        if (Mode == EUIRenderMode::Play)
        {
            EndingPanel::Render(Mode);
            DialoguePanel::Render(Mode);
        }
        break;
    }

    // InGame 중 대화 활성 여부가 바뀔 때만 마우스 잠금 상태를 갱신
    if (Mode == EUIRenderMode::Play && CurrentState == EGameUIState::InGame && !bPauseMenuOpen)
    {
        const bool bDialogueNow = DialoguePanel::IsActive();
        if (bDialogueNow != bPrevDialogueActive)
        {
            bPrevDialogueActive = bDialogueNow;
            if (bDialogueNow)
            {
                InputSystem::Get().LockMouse(false);
                InputSystem::Get().SetCursorVisibility(true);
            }
            else
            {
                ApplyCursorForState(EGameUIState::InGame);
            }
        }
    }
}
