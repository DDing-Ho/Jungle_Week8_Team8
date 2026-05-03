#include "Game/UI/GameUISystem.h"

#include "Game/UI/StartMenuPanel.h"
#include "Game/UI/HUDPanel.h"
#include "Game/UI/DialoguePanel.h"
#include "Game/UI/PauseMenuPanel.h"
#include "Game/UI/EndingPanel.h"

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
	SetPauseMenuOpen(false);
}

bool GameUISystem::WantsMouseCursor() const
{
	return CurrentState == EGameUIState::StartMenu ||
		   CurrentState == EGameUIState::Prologue ||
		   CurrentState == EGameUIState::Ending ||
		   bPauseMenuOpen ||
		   DialoguePanel::IsActive();
}

// -------------------------------------------------------
// 일시정지 메뉴
// -------------------------------------------------------
void GameUISystem::SetPauseMenuOpen(bool bOpen)
{
	if (bPauseMenuOpen == bOpen) return;
	bPauseMenuOpen = bOpen;
}

void GameUISystem::TogglePauseMenuIfInGame()
{
	GameUISystem& UI = GameUISystem::Get();
	if (UI.GetState() == EGameUIState::InGame)
	{
		UI.SetPauseMenuOpen(!UI.IsPauseMenuOpen());
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
}
