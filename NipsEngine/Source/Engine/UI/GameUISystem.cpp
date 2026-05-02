#include "Engine/UI/GameUISystem.h"

#include "Engine/UI/HUDPanel.h"

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
    CurrentState = NewState;
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

// -------------------------------------------------------
// 현재 상태에 맞는 패널 디스패치
// TODO: 각 패널을 구현한 뒤 아래 주석을 해제하고 호출하세요.
// -------------------------------------------------------
void GameUISystem::RenderCurrentPanel(EUIRenderMode Mode)
{
    switch (CurrentState)
    {
    case EGameUIState::StartMenu:
        // StartMenuPanel::Render(Mode);
        break;

    case EGameUIState::Prologue:
        // ProloguePanel::Render(Mode);
        break;

    case EGameUIState::InGame:
        HUDPanel::Render(Mode);
        break;

    case EGameUIState::Ending:
        // EndingPanel::Render(Mode);
        break;
    }
}
