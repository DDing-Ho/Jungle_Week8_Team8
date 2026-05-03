#include "Game/UI/GameUISystem.h"

#include "Game/UI/StartMenuPanel.h"
#include "Game/UI/HUDPanel.h"
#include "Game/UI/DialoguePanel.h"
#include "Game/UI/PauseMenuPanel.h"
#include "Game/UI/EndingPanel.h"
#include "Game/UI/RmlUi/RmlUiRenderInterfaceD3D11.h"
#include "Game/UI/RmlUi/RmlUiSystemInterface.h"

#include "Render/Common/RenderTypes.h"

#ifdef GetFirstChild
#undef GetFirstChild
#endif
#ifdef GetNextSibling
#undef GetNextSibling
#endif

#include "RmlUi/Core.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

#include <algorithm>

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
	(void)Hwnd;

	if (bRmlUiInitialized)
		return;

	D3DContext = Context;
	RmlSystemInterface = std::make_unique<FRmlUiSystemInterface>();
	RmlRenderInterface = std::make_unique<FRmlUiRenderInterfaceD3D11>();
	if (!RmlRenderInterface->Initialize(Device, Context))
	{
		RmlRenderInterface.reset();
		RmlSystemInterface.reset();
		D3DContext = nullptr;
		return;
	}

	Rml::SetSystemInterface(RmlSystemInterface.get());
	Rml::SetRenderInterface(RmlRenderInterface.get());
	if (!Rml::Initialise())
	{
		RmlRenderInterface.reset();
		RmlSystemInterface.reset();
		D3DContext = nullptr;
		return;
	}

	Rml::LoadFontFace("C:/Windows/Fonts/malgun.ttf", true);

	RmlContext = Rml::CreateContext("GameUI", Rml::Vector2i(1280, 720));
	if (!RmlContext || !CreateTestDocument())
	{
		Shutdown();
		return;
	}

	bRmlUiInitialized = true;
}

void GameUISystem::Shutdown()
{
	if (RmlDocument && RmlContext)
	{
		RmlContext->UnloadDocument(RmlDocument);
		RmlDocument = nullptr;
		RmlContext->Update();
	}

	if (RmlContext)
	{
		Rml::RemoveContext("GameUI");
		RmlContext = nullptr;
	}

	if (bRmlUiInitialized)
	{
		Rml::Shutdown();
		bRmlUiInitialized = false;
	}

	RmlRenderInterface.reset();
	RmlSystemInterface.reset();
	D3DContext = nullptr;
}

// -------------------------------------------------------
// 게임 빌드 - 현재 렌더 타겟에 RmlUi 렌더
// -------------------------------------------------------
void GameUISystem::Render(EUIRenderMode Mode)
{
	if (!D3DContext)
		return;

	UINT NumViewports = 1;
	D3D11_VIEWPORT Viewport = {};
	D3DContext->RSGetViewports(&NumViewports, &Viewport);

	RenderToCurrentTarget(
		Mode,
		static_cast<int>(Viewport.Width),
		static_cast<int>(Viewport.Height));
}

void GameUISystem::RenderToCurrentTarget(EUIRenderMode Mode, int Width, int Height)
{
	if (!bRmlUiInitialized || !RmlContext || !RmlRenderInterface)
		return;

	if (Width <= 0 || Height <= 0)
		return;

	RmlRenderInterface->BeginFrame(Width, Height);
	RmlContext->SetDimensions(Rml::Vector2i(Width, Height));
	UpdateRmlUiDocument(Mode);
	RmlContext->Update();
	RmlContext->Render();

	if (D3DContext)
	{
		ID3D11ShaderResourceView* NullSRV = nullptr;
		D3DContext->PSSetShaderResources(0, 1, &NullSRV);
	}
}

// -------------------------------------------------------
// 에디터 - 패널만 (ImGui 프레임은 EditorMainPanel 소유)
// -------------------------------------------------------
void GameUISystem::RenderPanelsOnly(EUIRenderMode Mode)
{
	(void)Mode;
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
	CleanProgress = std::clamp(InProgress, 0.0f, 1.0f);
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

void GameUISystem::SetStartGameCallback(std::function<void()> Callback)
{
	StartGameCallback = std::move(Callback);
}

void GameUISystem::RequestStartGame()
{
	if (StartGameCallback)
		StartGameCallback();
	else
		SetState(EGameUIState::InGame);
}

// -------------------------------------------------------
// 현재 상태에 맞는 패널 디스패치
// -------------------------------------------------------
void GameUISystem::RenderCurrentPanel(EUIRenderMode Mode)
{
	switch (CurrentState)
	{
	case EGameUIState::None:
		break;

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

void GameUISystem::UpdateRmlUiDocument(EUIRenderMode Mode)
{
	if (!RmlDocument)
		return;

	RmlDocument->SetClass("is-preview", Mode == EUIRenderMode::Preview);

	if (Rml::Element* Progress = RmlDocument->GetElementById("progress-value"))
	{
		const int Percent = static_cast<int>(CleanProgress * 100.0f);
		Progress->SetInnerRML(std::to_string(Percent) + "%");
	}

	if (Rml::Element* Items = RmlDocument->GetElementById("item-count"))
	{
		Items->SetInnerRML(std::to_string(ItemCount));
	}
}

bool GameUISystem::CreateTestDocument()
{
	if (!RmlContext)
		return false;

	static const char* DocumentRml = R"(
<rml>
<head>
	<title>Game UI</title>
	<style>
		body {
			width: 100%;
			height: 100%;
			font-family: "Malgun Gothic", "malgun";
			color: #ffffff;
		}
		#panel {
			position: absolute;
			left: 32px;
			top: 32px;
			width: 280px;
			padding: 18px;
			background-color: rgba(12, 18, 28, 210);
			border: 1px #7fb7ff;
		}
		#title {
			font-size: 24px;
			margin-bottom: 12px;
			color: #9bd2ff;
		}
		.row {
			font-size: 16px;
			margin-top: 6px;
		}
		.value {
			color: #ffe08a;
		}
	</style>
</head>
<body>
	<div id="panel">
		<div id="title">RmlUi Test Window</div>
		<div class="row">Render target: current D3D11 target</div>
		<div class="row">Progress: <span id="progress-value" class="value">0%</span></div>
		<div class="row">Items: <span id="item-count" class="value">0</span></div>
	</div>
</body>
</rml>
)";

	RmlDocument = RmlContext->LoadDocumentFromMemory(DocumentRml, "GameUITest");
	if (!RmlDocument)
		return false;

	RmlDocument->Show();
	return true;
}
