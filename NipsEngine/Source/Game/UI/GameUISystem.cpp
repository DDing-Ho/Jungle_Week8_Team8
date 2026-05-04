#include "Game/UI/GameUISystem.h"

#include "Game/UI/DialoguePanel.h"
#include "Game/UI/EndingPanel.h"
#include "Game/UI/HUDPanel.h"
#include "Game/UI/PauseMenuPanel.h"
#include "Game/UI/RmlUi/RmlUiDocumentsResource.h"
#include "Game/UI/RmlUi/RmlUiRenderInterfaceD3D11.h"
#include "Game/UI/RmlUi/RmlUiSystemInterface.h"
#include "Game/UI/StartMenuPanel.h"

#include "Render/Common/RenderTypes.h"

#include <Windows.h>
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
#include "RmlUi/Core/Event.h"
#include "RmlUi/Core/EventListener.h"
#include "RmlUi/Core/Input.h"
#include "RmlUi/Core/StringUtilities.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

class FRmlUiClickListener : public Rml::EventListener
{
public:
	explicit FRmlUiClickListener(std::function<void()> InCallback)
		: Callback(std::move(InCallback))
	{
	}

	void ProcessEvent(Rml::Event& Event) override
	{
		(void)Event;
		if (Callback)
			Callback();
	}

private:
	std::function<void()> Callback;
};

namespace
{
	Rml::Input::KeyIdentifier ToRmlKey(int VK)
	{
		if (VK >= '0' && VK <= '9')
			return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + (VK - '0'));
		if (VK >= 'A' && VK <= 'Z')
			return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + (VK - 'A'));
		if (VK >= VK_F1 && VK <= VK_F12)
			return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_F1 + (VK - VK_F1));

		switch (VK)
		{
		case VK_SPACE: return Rml::Input::KI_SPACE;
		case VK_ESCAPE: return Rml::Input::KI_ESCAPE;
		case VK_RETURN: return Rml::Input::KI_RETURN;
		case VK_TAB: return Rml::Input::KI_TAB;
		case VK_BACK: return Rml::Input::KI_BACK;
		case VK_LEFT: return Rml::Input::KI_LEFT;
		case VK_RIGHT: return Rml::Input::KI_RIGHT;
		case VK_UP: return Rml::Input::KI_UP;
		case VK_DOWN: return Rml::Input::KI_DOWN;
		case VK_HOME: return Rml::Input::KI_HOME;
		case VK_END: return Rml::Input::KI_END;
		case VK_PRIOR: return Rml::Input::KI_PRIOR;
		case VK_NEXT: return Rml::Input::KI_NEXT;
		case VK_INSERT: return Rml::Input::KI_INSERT;
		case VK_DELETE: return Rml::Input::KI_DELETE;
		case VK_CONTROL: return Rml::Input::KI_LCONTROL;
		case VK_SHIFT: return Rml::Input::KI_LSHIFT;
		case VK_MENU: return Rml::Input::KI_LMENU;
		default: return Rml::Input::KI_UNKNOWN;
		}
	}

	std::string FormatTime(float Seconds)
	{
		const int TotalSec = std::max(0, static_cast<int>(Seconds));
		char Buffer[32] = {};
		std::snprintf(Buffer, sizeof(Buffer), "%dm %02ds", TotalSec / 60, TotalSec % 60);
		return Buffer;
	}

	std::string FormatPercent(float Progress)
	{
		const int Percent = static_cast<int>(std::clamp(Progress, 0.0f, 1.0f) * 100.0f);
		return std::to_string(Percent) + "%";
	}

	std::string FormatPixels(float Value)
	{
		char Buffer[32] = {};
		std::snprintf(Buffer, sizeof(Buffer), "%.2fpx", Value);
		return Buffer;
	}

	std::string LoadTextResource(int ResourceId)
	{
		HMODULE Module = GetModuleHandleW(nullptr);
		HRSRC Resource = FindResourceW(Module, MAKEINTRESOURCEW(ResourceId), RT_RCDATA);
		if (!Resource)
			return {};

		HGLOBAL ResourceData = LoadResource(Module, Resource);
		if (!ResourceData)
			return {};

		const DWORD ResourceSize = SizeofResource(Module, Resource);
		const char* ResourceBytes = static_cast<const char*>(LockResource(ResourceData));
		if (!ResourceBytes || ResourceSize == 0)
			return {};

		return std::string(ResourceBytes, ResourceSize);
	}

	void ReplaceAll(std::string& Text, const std::string& Token, const std::string& Value)
	{
		size_t Position = 0;
		while ((Position = Text.find(Token, Position)) != std::string::npos)
		{
			Text.replace(Position, Token.length(), Value);
			Position += Value.length();
		}
	}
}

GameUISystem& GameUISystem::Get()
{
	static GameUISystem Instance;
	return Instance;
}

GameUISystem::~GameUISystem() = default;

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
	bRmlUiInitialized = true;

	Rml::LoadFontFace("C:/Windows/Fonts/malgun.ttf", true);

	RmlContext = Rml::CreateContext("GameUI", Rml::Vector2i(1280, 720));
	if (!RmlContext || !CreateGameDocument())
	{
		Shutdown();
		return;
	}

	LastRmlUpdateTime = RmlSystemInterface->GetElapsedTime();
}

void GameUISystem::Shutdown()
{
	if (RmlDocument)
	{
		if (Rml::Element* Element = RmlDocument->GetElementById("start-button"))
			Element->RemoveEventListener("click", StartClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("retry-button"))
			Element->RemoveEventListener("click", RetryClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("exit-button"))
			Element->RemoveEventListener("click", ExitClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("pause-exit-button"))
			Element->RemoveEventListener("click", ExitClickListener.get());
	}

	if (RmlDocument && RmlContext)
	{
		RmlContext->UnloadDocument(RmlDocument);
		RmlDocument = nullptr;
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

	StartClickListener.reset();
	RetryClickListener.reset();
	ExitClickListener.reset();

	RmlRenderInterface.reset();
	RmlSystemInterface.reset();
	D3DContext = nullptr;
	LastRmlUpdateTime = 0.0;
}

void GameUISystem::Render(EUIRenderMode Mode)
{
	if (!D3DContext)
		return;

	UINT NumViewports = 1;
	D3D11_VIEWPORT Viewport = {};
	D3DContext->RSGetViewports(&NumViewports, &Viewport);

	RenderToCurrentTarget(Mode, static_cast<int>(Viewport.Width), static_cast<int>(Viewport.Height));
}

void GameUISystem::RenderToCurrentTarget(EUIRenderMode Mode, int Width, int Height)
{
	if (!bRmlUiInitialized || !RmlContext || !RmlRenderInterface)
		return;

	if (Width <= 0 || Height <= 0)
		return;

	RmlRenderInterface->BeginFrame(Width, Height);
	RmlContext->SetDimensions(Rml::Vector2i(Width, Height));
	UpdateRmlUiDocument(Mode, Width, Height);
	RmlContext->Update();
	RmlContext->Render();

	if (D3DContext)
	{
		ID3D11ShaderResourceView* NullSRV = nullptr;
		D3DContext->PSSetShaderResources(0, 1, &NullSRV);
	}
}

void GameUISystem::RenderPanelsOnly(EUIRenderMode Mode)
{
	(void)Mode;
}

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

void GameUISystem::SetPauseMenuOpen(bool bOpen)
{
	if (bPauseMenuOpen == bOpen)
		return;

	bPauseMenuOpen = bOpen;
}

void GameUISystem::TogglePauseMenuIfInGame()
{
	GameUISystem& UI = GameUISystem::Get();
	if (UI.GetState() == EGameUIState::InGame)
		UI.SetPauseMenuOpen(!UI.IsPauseMenuOpen());
}

void GameUISystem::ResetGameData()
{
	CleanProgress = 0.f;
	ItemCount = 0;
	ElapsedTime = 0.f;
	CurrentItemName.clear();
	CurrentItemDesc.clear();
	InteractionHintType = EInteractionHintType::None;
}

void GameUISystem::SetProgress(float InProgress)
{
	CleanProgress = std::clamp(InProgress, 0.0f, 1.0f);
}

void GameUISystem::SetCurrentItem(const char* Name, const char* Desc)
{
	CurrentItemName = Name ? Name : "";
	CurrentItemDesc = Desc ? Desc : "";
}

void GameUISystem::SetInteractionHint(EInteractionHintType Type)
{
	InteractionHintType = Type;
}

void GameUISystem::SetItemCount(int Count)
{
	ItemCount = Count;
}

void GameUISystem::SetElapsedTime(float Seconds)
{
	ElapsedTime = Seconds;
}

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

void GameUISystem::SetExitPlayCallback(std::function<void()> Callback)
{
	ExitPlayCallback = std::move(Callback);
}

void GameUISystem::RequestExitPlay()
{
	if (ExitPlayCallback)
		ExitPlayCallback();
	else
		PostQuitMessage(0);
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

bool GameUISystem::OnUIMouseMove(float X, float Y)
{
	if (!bRmlUiInitialized || !RmlContext)
		return false;

	RmlContext->ProcessMouseMove(static_cast<int>(X), static_cast<int>(Y), 0);
	return WantsMouseCursor();
}

bool GameUISystem::OnUIMouseButtonDown(int Button, float X, float Y)
{
	if (!bRmlUiInitialized || !RmlContext)
		return false;

	RmlContext->ProcessMouseMove(static_cast<int>(X), static_cast<int>(Y), 0);
	RmlContext->ProcessMouseButtonDown(Button, 0);
	return WantsMouseCursor();
}

bool GameUISystem::OnUIMouseButtonUp(int Button, float X, float Y)
{
	if (!bRmlUiInitialized || !RmlContext)
		return false;

	RmlContext->ProcessMouseMove(static_cast<int>(X), static_cast<int>(Y), 0);
	RmlContext->ProcessMouseButtonUp(Button, 0);
	return WantsMouseCursor();
}

bool GameUISystem::OnUIKeyDown(int VK)
{
	if (!bRmlUiInitialized || !RmlContext)
		return false;

	const Rml::Input::KeyIdentifier Key = ToRmlKey(VK);
	if (Key != Rml::Input::KI_UNKNOWN)
		RmlContext->ProcessKeyDown(Key, 0);

	if (CurrentState == EGameUIState::StartMenu)
	{
		if (VK == VK_RETURN || VK == VK_SPACE)
		{
			RequestStartGame();
			return true;
		}

		if (VK == VK_ESCAPE)
		{
			RequestExitPlay();
			return true;
		}

		return true;
	}

	return false;
}

bool GameUISystem::OnUIKeyUp(int VK)
{
	if (!bRmlUiInitialized || !RmlContext)
		return false;

	const Rml::Input::KeyIdentifier Key = ToRmlKey(VK);
	if (Key != Rml::Input::KI_UNKNOWN)
		RmlContext->ProcessKeyUp(Key, 0);

	if (CurrentState == EGameUIState::StartMenu)
		return true;

	if (VK == VK_SPACE && DialoguePanel::AdvanceOrSkip())
		return true;

	return false;
}

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

void GameUISystem::UpdateRmlUiDocument(EUIRenderMode Mode, int Width, int Height)
{
	if (!RmlDocument)
		return;

	double Now = LastRmlUpdateTime;
	if (RmlSystemInterface)
		Now = RmlSystemInterface->GetElapsedTime();

	const float DeltaTime = LastRmlUpdateTime > 0.0 ? static_cast<float>(Now - LastRmlUpdateTime) : 0.0f;
	LastRmlUpdateTime = Now;

	DialoguePanel::Tick(DeltaTime, Mode);
	if (CurrentState == EGameUIState::Ending)
		EndingPanel::Tick(DeltaTime);

	RmlDocument->SetClass("is-preview", Mode == EUIRenderMode::Preview);

	const bool bShowStart = CurrentState == EGameUIState::StartMenu && Mode == EUIRenderMode::Play;
	const bool bShowHud = CurrentState == EGameUIState::InGame;
	const bool bShowPause = CurrentState == EGameUIState::InGame && bPauseMenuOpen;
	const bool bShowDialogue = DialoguePanel::IsActive() &&
		(CurrentState == EGameUIState::InGame || CurrentState == EGameUIState::Ending || CurrentState == EGameUIState::Prologue);
	const bool bShowEnding = CurrentState == EGameUIState::Ending;
	const bool bShowTheEnd = bShowEnding && EndingPanel::ShouldShowTheEnd();
	const bool bShowInteractionHint = bShowHud && !bShowPause && !bShowDialogue && InteractionHintType != EInteractionHintType::None;

	SetElementVisible("start-menu", bShowStart);
	SetElementVisible("hud-panel", bShowHud);
	SetElementVisible("item-status", bShowHud);
	SetElementVisible("crosshair-dot", bShowHud && !bShowPause);
	SetElementVisible("interaction-hint", bShowInteractionHint);
	SetElementVisible("pause-layer", bShowPause);
	SetElementVisible("dialogue-panel", bShowDialogue);
	SetElementVisible("ending-panel", bShowEnding);
	SetElementVisible("the-end", bShowTheEnd);

	constexpr float TitleBackgroundAspect = 2760.0f / 1504.0f;
	float TitleBackgroundWidth = static_cast<float>(Width);
	float TitleBackgroundHeight = static_cast<float>(Height);
	if (Width > 0 && Height > 0)
	{
		const float ViewAspect = static_cast<float>(Width) / static_cast<float>(Height);
		if (ViewAspect > TitleBackgroundAspect)
		{
			TitleBackgroundWidth = static_cast<float>(Width);
			TitleBackgroundHeight = TitleBackgroundWidth / TitleBackgroundAspect;
		}
		else
		{
			TitleBackgroundHeight = static_cast<float>(Height);
			TitleBackgroundWidth = TitleBackgroundHeight * TitleBackgroundAspect;
		}
	}

	SetElementProperty("title-background", "width", FormatPixels(TitleBackgroundWidth));
	SetElementProperty("title-background", "height", FormatPixels(TitleBackgroundHeight));
	SetElementProperty("title-background", "left", FormatPixels((static_cast<float>(Width) - TitleBackgroundWidth) * 0.5f));
	SetElementProperty("title-background", "top", FormatPixels((static_cast<float>(Height) - TitleBackgroundHeight) * 0.5f));

	const std::string ProgressText = FormatPercent(CleanProgress);
	SetElementText("progress-value", ProgressText);
	SetElementText("pause-progress", ProgressText);
	SetElementProperty("progress-fill", "width", ProgressText);

	SetElementText("item-count", std::to_string(ItemCount));
	SetElementText("pause-item-count", std::to_string(ItemCount));
	SetElementText("pause-time", FormatTime(ElapsedTime));
	SetElementText("current-item-name", CurrentItemName.empty() ? "No item" : CurrentItemName);
	// SetElementText("current-item-desc", CurrentItemDesc.empty() ? "Nothing selected" : CurrentItemDesc);

	switch (InteractionHintType)
	{
	case EInteractionHintType::Clean:
		SetElementText("interaction-hint-text", "[E] 청소하기");
		break;
	case EInteractionHintType::Inspect:
		SetElementText("interaction-hint-text", "[E] 살펴보기");
		break;
	default:
		SetElementText("interaction-hint-text", "");
		break;
	}

	SetElementText("dialogue-speaker", DialoguePanel::GetSpeaker());
	SetElementText("dialogue-text", DialoguePanel::GetVisibleText());
	SetElementVisible("dialogue-hint", DialoguePanel::IsTextComplete());

	const int Alpha = static_cast<int>(EndingPanel::GetFadeAlpha() * 255.0f);
	SetElementProperty("the-end", "color", "rgba(220, 210, 190, " + std::to_string(Alpha) + ")");
}

bool GameUISystem::CreateGameDocument()
{
	if (!RmlContext)
		return false;

	std::string DocumentRml = LoadTextResource(IDR_GAME_UI_RML);
	const std::string DocumentRcss = LoadTextResource(IDR_GAME_UI_RCSS);
	if (DocumentRml.empty() || DocumentRcss.empty())
		return false;

	ReplaceAll(DocumentRml, "{{GAME_UI_RCSS}}", DocumentRcss);

	RmlDocument = RmlContext->LoadDocumentFromMemory(DocumentRml, "GameUI");
	if (!RmlDocument)
		return false;

	RmlDocument->Show();
	BindRmlUiEvents();
	UpdateRmlUiDocument(EUIRenderMode::Play, 1280, 720);
	return true;
}

void GameUISystem::BindRmlUiEvents()
{
	if (!RmlDocument)
		return;

	StartClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		if (GameUISystem::Get().GetState() == EGameUIState::StartMenu)
			GameUISystem::Get().RequestStartGame();
	});

	RetryClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().ResetGameData();
		GameUISystem::Get().SetPauseMenuOpen(false);
	});

	ExitClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().RequestExitPlay();
	});

	if (Rml::Element* Element = RmlDocument->GetElementById("start-button"))
		Element->AddEventListener("click", StartClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("retry-button"))
		Element->AddEventListener("click", RetryClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("exit-button"))
		Element->AddEventListener("click", ExitClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("pause-exit-button"))
		Element->AddEventListener("click", ExitClickListener.get());
}

void GameUISystem::SetElementVisible(const char* Id, bool bVisible)
{
	SetElementProperty(Id, "display", bVisible ? "block" : "none");
}

void GameUISystem::SetElementText(const char* Id, const std::string& Text)
{
	if (!RmlDocument)
		return;

	if (Rml::Element* Element = RmlDocument->GetElementById(Id))
		Element->SetInnerRML(Rml::StringUtilities::EncodeRml(Text));
}

void GameUISystem::SetElementProperty(const char* Id, const char* Property, const std::string& Value)
{
	if (!RmlDocument)
		return;

	if (Rml::Element* Element = RmlDocument->GetElementById(Id))
		Element->SetProperty(Property, Value);
}
