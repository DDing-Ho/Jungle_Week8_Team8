#include "Game/UI/GameUISystem.h"

#include "Game/UI/DialoguePanel.h"
#include "Game/UI/EndingPanel.h"
#include "Game/UI/HUDPanel.h"
#include "Game/UI/PauseMenuPanel.h"
#include "Game/UI/RmlUi/RmlUiDocumentsResource.h"
#include "Game/UI/RmlUi/RmlUiRenderInterfaceD3D11.h"
#include "Game/UI/RmlUi/RmlUiSystemInterface.h"
#include "Game/UI/StartMenuPanel.h"

#include "Audio/AudioSystem.h"
#include "Core/Paths.h"
#include "Game/Systems/EndingSystem.h"
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
#include <cmath>
#include <cstdio>
#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
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

	std::string FormatValuePercent(float Value)
	{
		const int Percent = static_cast<int>(std::round(std::clamp(Value, 0.0f, 3.0f) * 100.0f));
		return std::to_string(Percent) + "%";
	}

	std::string FormatSensitivity(float Value)
	{
		char Buffer[32] = {};
		std::snprintf(Buffer, sizeof(Buffer), "%.2f", std::clamp(Value, 0.2f, 3.0f));
		return Buffer;
	}

	std::string FormatCssPercent(float Value)
	{
		char Buffer[32] = {};
		std::snprintf(Buffer, sizeof(Buffer), "%.2f%%", std::clamp(Value, 0.0f, 1.0f) * 100.0f);
		return Buffer;
	}

	std::string FormatPixels(float Value)
	{
		char Buffer[32] = {};
		std::snprintf(Buffer, sizeof(Buffer), "%.2fpx", Value);
		return Buffer;
	}

	std::string FormatOpacity(float Value)
	{
		char Buffer[32] = {};
		std::snprintf(Buffer, sizeof(Buffer), "%.3f", std::clamp(Value, 0.0f, 1.0f));
		return Buffer;
	}

	std::string FormatAlphaColor(float Red, float Green, float Blue, float Alpha)
	{
		char Buffer[64] = {};
		std::snprintf(
			Buffer,
			sizeof(Buffer),
			"rgba(%d, %d, %d, %d)",
			static_cast<int>(std::clamp(Red, 0.0f, 255.0f)),
			static_cast<int>(std::clamp(Green, 0.0f, 255.0f)),
			static_cast<int>(std::clamp(Blue, 0.0f, 255.0f)),
			static_cast<int>(std::clamp(Alpha, 0.0f, 1.0f) * 255.0f));
		return Buffer;
	}

	EEndingType EndingTypeFromId(const FString& EndingId)
	{
		if (EndingId == "Ending_Good")
			return EEndingType::Good;
		if (EndingId == "Ending_Bad")
			return EEndingType::Bad;
		return EEndingType::Normal;
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

	constexpr const char* TitleButtonIds[] =
	{
		"start-button",
		"settings-button",
		"credits-button",
		"exit-button",
	};

	constexpr size_t TitleButtonCount = sizeof(TitleButtonIds) / sizeof(TitleButtonIds[0]);

	constexpr float SettingsPanelWidth = 480.0f;
	constexpr float SettingsPanelHeight = 326.0f;
	constexpr float SettingsRowLeft = 44.0f;
	constexpr float SettingsSliderLeft = 202.0f;
	constexpr float SettingsSliderTop = 17.0f;
	constexpr float SettingsSliderWidth = 132.0f;
	constexpr float SettingsSliderHitHeight = 28.0f;
	constexpr float MouseSensitivityMin = 0.2f;
	constexpr float MouseSensitivityMax = 3.0f;
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

	LoadSettings();

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

	Rml::LoadFontFace(FPaths::ToAbsoluteString(L"Asset/Font/NEXONLv1GothicRegular.ttf"), true);
	Rml::LoadFontFace(FPaths::ToAbsoluteString(L"Asset/Font/NEXONLv1GothicBold.ttf"), false);
	Rml::LoadFontFace(FPaths::ToAbsoluteString(L"Asset/Font/NEXONLv1GothicLight.ttf"), false);

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
		if (Rml::Element* Element = RmlDocument->GetElementById("settings-button"))
			Element->RemoveEventListener("click", SettingsOpenClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("pause-settings-button"))
			Element->RemoveEventListener("click", SettingsOpenClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("settings-close-button"))
			Element->RemoveEventListener("click", SettingsCloseClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("credits-button"))
			Element->RemoveEventListener("click", CreditsOpenClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("credits-close-button"))
			Element->RemoveEventListener("click", CreditsCloseClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("pause-title-button"))
			Element->RemoveEventListener("click", PauseTitleClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("save-score-button"))
			Element->RemoveEventListener("click", SaveScoreClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("exit-to-main-button"))
			Element->RemoveEventListener("click", ExitToMainClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("debug-menu-close"))
			Element->RemoveEventListener("click", DebugMenuCloseClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("debug-jump-bad"))
			Element->RemoveEventListener("click", DebugJumpBadClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("debug-jump-normal"))
			Element->RemoveEventListener("click", DebugJumpNormalClickListener.get());
		if (Rml::Element* Element = RmlDocument->GetElementById("debug-jump-good"))
			Element->RemoveEventListener("click", DebugJumpGoodClickListener.get());
		for (size_t Index = 0; Index < TitleButtonHoverEnterListeners.size() && Index < TitleButtonCount; ++Index)
		{
			if (Rml::Element* Element = RmlDocument->GetElementById(TitleButtonIds[Index]))
				Element->RemoveEventListener("mouseover", TitleButtonHoverEnterListeners[Index].get());
		}
		for (size_t Index = 0; Index < TitleButtonHoverLeaveListeners.size() && Index < TitleButtonCount; ++Index)
		{
			if (Rml::Element* Element = RmlDocument->GetElementById(TitleButtonIds[Index]))
				Element->RemoveEventListener("mouseout", TitleButtonHoverLeaveListeners[Index].get());
		}
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
	SettingsOpenClickListener.reset();
	SettingsCloseClickListener.reset();
	CreditsOpenClickListener.reset();
	CreditsCloseClickListener.reset();
	PauseTitleClickListener.reset();
	SaveScoreClickListener.reset();
	ExitToMainClickListener.reset();
	DebugMenuCloseClickListener.reset();
	DebugJumpBadClickListener.reset();
	DebugJumpNormalClickListener.reset();
	DebugJumpGoodClickListener.reset();
	TitleButtonHoverEnterListeners.clear();
	TitleButtonHoverLeaveListeners.clear();

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
	{
		if (CurrentEndingType == EEndingType::None)
			CurrentEndingType = EndingTypeFromId(FEndingSystem::Get().EvaluateEnding().EndingId);
		EndingPanel::Reset();
	}
	if (NewState == EGameUIState::StartMenu && CurrentState != EGameUIState::StartMenu)
		ResetTitleIntro();

	CurrentState = NewState;
	SetPauseMenuOpen(false);
	bSettingsOpen = false;
	bCreditsOpen = false;
	EndSettingsSliderDrag();
}

bool GameUISystem::WantsMouseCursor() const
{
	return CurrentState == EGameUIState::StartMenu ||
		   CurrentState == EGameUIState::Prologue ||
		   CurrentState == EGameUIState::Ending ||
		   bSettingsOpen ||
		   bCreditsOpen ||
		   bDebugMenuOpen ||
		   bItemInspectOpen ||
		   bPauseMenuOpen ||
		   DialoguePanel::IsActive();
}

bool GameUISystem::WantsCustomCursor() const
{
	return WantsMouseCursor();
}

void GameUISystem::SetPauseMenuOpen(bool bOpen)
{
	if (bPauseMenuOpen == bOpen)
		return;

	bPauseMenuOpen = bOpen;
	if (bPauseMenuOpen)
	{
		bSettingsOpen = false;
		bCreditsOpen = false;
		EndSettingsSliderDrag();
	}
	else
	{
		EndSettingsSliderDrag();
	}
}

void GameUISystem::SetMouseSensitivityChangedCallback(std::function<void(float)> Callback)
{
	MouseSensitivityChangedCallback = std::move(Callback);
	ApplySettings();
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
	CurrentEndingType = EEndingType::None;
	bSettingsOpen = false;
	bCreditsOpen = false;
	HideItemInspect();
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

void GameUISystem::ShowItemInspect(const char* Name, const char* Desc, const char* IconPath)
{
	InspectItemName = Name ? Name : "";
	InspectItemDesc = Desc ? Desc : "";
	InspectItemIconPath = IconPath ? IconPath : "";
	bItemInspectOpen = true;
}

void GameUISystem::HideItemInspect()
{
	bItemInspectOpen = false;
	InspectItemName.clear();
	InspectItemDesc.clear();
	InspectItemIconPath.clear();
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

void GameUISystem::SetExitToTitleCallback(std::function<void()> Callback)
{
	ExitToTitleCallback = std::move(Callback);
}

void GameUISystem::RequestExitPlay()
{
	if (ExitPlayCallback)
		ExitPlayCallback();
	else
		PostQuitMessage(0);
}

void GameUISystem::RequestExitToTitle()
{
	bPauseMenuOpen = false;
	bSettingsOpen = false;
	bCreditsOpen = false;
	EndSettingsSliderDrag();
	if (ExitToTitleCallback)
	{
		ExitToTitleCallback();
	}
	else
	{
		SetState(EGameUIState::StartMenu);
		ResetGameData();
	}
}

void GameUISystem::RequestSaveScore()
{
	const std::wstring SavesDir = FPaths::Combine(FPaths::RootDir(), L"Saves");
	FPaths::CreateDir(SavesDir);

	const std::wstring ScorePath = FPaths::Combine(SavesDir, L"Scores.txt");
	std::ofstream File(FPaths::ToUtf8(ScorePath), std::ios::app);
	if (!File.is_open())
		return;

	SYSTEMTIME st;
	GetLocalTime(&st);

	char DateBuf[64];
	std::snprintf(DateBuf, sizeof(DateBuf), "%04d-%02d-%02d %02d:%02d:%02d",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	File << "[" << DateBuf << "] "
		 << "Progress: " << static_cast<int>(CleanProgress * 100.0f) << "%, "
		 << "Time: " << FormatTime(ElapsedTime) << ", "
		 << "Items: " << ItemCount << "\n";
	
	File.close();
}

void GameUISystem::SetStartGameCallback(std::function<void()> Callback)
{
	StartGameCallback = std::move(Callback);
}

void GameUISystem::RequestStartGame()
{
	if (bStartGameTransitionActive)
		return;

	if (CurrentState == EGameUIState::StartMenu)
	{
		bStartGameTransitionActive = true;
		StartGameTransitionElapsed = 0.0f;
		bStartGameTransitionReady = false;
		return;
	}

	if (StartGameCallback)
		StartGameCallback();
	else
		SetState(EGameUIState::InGame);
}

void GameUISystem::OpenSettings()
{
	bSettingsOpen = true;
	bCreditsOpen = false;
}

void GameUISystem::OpenCredits()
{
	bCreditsOpen = true;
	bSettingsOpen = false;
}

void GameUISystem::CloseCredits()
{
	bCreditsOpen = false;
}

void GameUISystem::OpenDebugMenu()
{
	bDebugMenuOpen = true;
	bSettingsOpen = false;
	bCreditsOpen = false;
	bPauseMenuOpen = false;
}

void GameUISystem::CloseDebugMenu()
{
	bDebugMenuOpen = false;
}

void GameUISystem::CloseSettings()
{
	bSettingsOpen = false;
	EndSettingsSliderDrag();
	SaveSettings();
}

void GameUISystem::ApplySettings()
{
	if (MouseSensitivityChangedCallback)
	{
		MouseSensitivityChangedCallback(MouseSensitivityScale);
	}

	FAudioSystem::Get().SetBusVolume(EAudioBus::Music, BgmVolume);
	FAudioSystem::Get().SetBusVolume(EAudioBus::SFX, SfxVolume);
}

void GameUISystem::LoadSettings()
{
	const std::wstring SettingsPath = FPaths::Combine(FPaths::SettingsDir(), L"Game.ini");
	std::ifstream File(FPaths::ToUtf8(SettingsPath));
	if (!File.is_open())
		return;

	std::string Line;
	while (std::getline(File, Line))
	{
		std::istringstream Is(Line);
		std::string Key;
		if (std::getline(Is, Key, '='))
		{
			std::string Value;
			if (std::getline(Is, Value))
			{
				if (Key == "MouseSensitivity") MouseSensitivityScale = std::stof(Value);
				else if (Key == "BgmVolume") BgmVolume = std::stof(Value);
				else if (Key == "SfxVolume") SfxVolume = std::stof(Value);
			}
		}
	}

	ApplySettings();
}

void GameUISystem::SaveSettings()
{
	const std::wstring SettingsDir = FPaths::SettingsDir();
	FPaths::CreateDir(SettingsDir);

	const std::wstring SettingsPath = FPaths::Combine(SettingsDir, L"Game.ini");
	std::ofstream File(FPaths::ToUtf8(SettingsPath));
	if (!File.is_open())
		return;

	File << "MouseSensitivity=" << MouseSensitivityScale << "\n";
	File << "BgmVolume=" << BgmVolume << "\n";
	File << "SfxVolume=" << SfxVolume << "\n";
}

void GameUISystem::UpdateSettingsElements()
{
	SetElementText("settings-mouse-value", FormatSensitivity(MouseSensitivityScale));
	SetElementText("settings-bgm-value", FormatValuePercent(BgmVolume));
	SetElementText("settings-sfx-value", FormatValuePercent(SfxVolume));

	const float MouseNormalized = GetSettingsSliderNormalized(ESettingsSlider::MouseSensitivity);
	const float BgmNormalized = GetSettingsSliderNormalized(ESettingsSlider::Bgm);
	const float SfxNormalized = GetSettingsSliderNormalized(ESettingsSlider::Sfx);

	SetElementProperty("settings-mouse-fill", "width", FormatCssPercent(MouseNormalized));
	SetElementProperty("settings-mouse-knob", "left", FormatCssPercent(MouseNormalized));
	SetElementProperty("settings-bgm-fill", "width", FormatCssPercent(BgmNormalized));
	SetElementProperty("settings-bgm-knob", "left", FormatCssPercent(BgmNormalized));
	SetElementProperty("settings-sfx-fill", "width", FormatCssPercent(SfxNormalized));
	SetElementProperty("settings-sfx-knob", "left", FormatCssPercent(SfxNormalized));
}

bool GameUISystem::TryBeginSettingsSliderDrag(float X, float Y)
{
	if (!bSettingsOpen)
		return false;

	const ESettingsSlider Sliders[] = { ESettingsSlider::MouseSensitivity, ESettingsSlider::Bgm, ESettingsSlider::Sfx };
	for (ESettingsSlider Slider : Sliders)
	{
		float Left = 0.0f;
		float Top = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
		if (!GetSettingsSliderRect(Slider, Left, Top, Width, Height))
			continue;

		if (X >= Left && X <= Left + Width && Y >= Top && Y <= Top + Height)
		{
			ActiveSettingsSlider = Slider;
			UpdateSettingsSliderDrag(X, Y);
			return true;
		}
	}

	return false;
}

void GameUISystem::UpdateSettingsSliderDrag(float X, float Y)
{
	(void)Y;
	if (ActiveSettingsSlider == ESettingsSlider::None)
		return;

	float Left = 0.0f;
	float Top = 0.0f;
	float Width = 0.0f;
	float Height = 0.0f;
	if (!GetSettingsSliderRect(ActiveSettingsSlider, Left, Top, Width, Height) || Width <= 0.0f)
		return;

	const float Normalized = std::clamp((X - Left) / Width, 0.0f, 1.0f);
	SetSettingsSliderNormalized(ActiveSettingsSlider, Normalized);
}

void GameUISystem::EndSettingsSliderDrag()
{
	ActiveSettingsSlider = ESettingsSlider::None;
}

bool GameUISystem::GetSettingsSliderRect(ESettingsSlider Slider, float& Left, float& Top, float& Width, float& Height) const
{
	float RowTop = 0.0f;
	switch (Slider)
	{
	case ESettingsSlider::MouseSensitivity:
		RowTop = 88.0f;
		break;
	case ESettingsSlider::Bgm:
		RowTop = 142.0f;
		break;
	case ESettingsSlider::Sfx:
		RowTop = 196.0f;
		break;
	default:
		return false;
	}

	const float PanelLeft = static_cast<float>(LastUiWidth) * 0.5f - SettingsPanelWidth * 0.5f;
	const float PanelTop = static_cast<float>(LastUiHeight) * 0.5f - SettingsPanelHeight * 0.5f;
	Left = PanelLeft + SettingsRowLeft + SettingsSliderLeft;
	Top = PanelTop + RowTop + SettingsSliderTop - (SettingsSliderHitHeight - 8.0f) * 0.5f;
	Width = SettingsSliderWidth;
	Height = SettingsSliderHitHeight;
	return true;
}

float GameUISystem::GetSettingsSliderNormalized(ESettingsSlider Slider) const
{
	switch (Slider)
	{
	case ESettingsSlider::MouseSensitivity:
		return (std::clamp(MouseSensitivityScale, MouseSensitivityMin, MouseSensitivityMax) - MouseSensitivityMin) /
			(MouseSensitivityMax - MouseSensitivityMin);
	case ESettingsSlider::Bgm:
		return std::clamp(BgmVolume, 0.0f, 1.0f);
	case ESettingsSlider::Sfx:
		return std::clamp(SfxVolume, 0.0f, 1.0f);
	default:
		return 0.0f;
	}
}

void GameUISystem::SetSettingsSliderNormalized(ESettingsSlider Slider, float Normalized)
{
	Normalized = std::clamp(Normalized, 0.0f, 1.0f);
	switch (Slider)
	{
	case ESettingsSlider::MouseSensitivity:
		MouseSensitivityScale = MouseSensitivityMin + Normalized * (MouseSensitivityMax - MouseSensitivityMin);
		break;
	case ESettingsSlider::Bgm:
		BgmVolume = Normalized;
		break;
	case ESettingsSlider::Sfx:
		SfxVolume = Normalized;
		break;
	default:
		return;
	}

	ApplySettings();
	UpdateSettingsElements();
}

bool GameUISystem::OnUIMouseMove(float X, float Y)
{
	if (!bRmlUiInitialized || !RmlContext)
		return false;

	CustomCursorX = X;
	CustomCursorY = Y;
	RmlContext->ProcessMouseMove(static_cast<int>(X), static_cast<int>(Y), 0);
	if (ActiveSettingsSlider != ESettingsSlider::None)
	{
		UpdateSettingsSliderDrag(X, Y);
		return true;
	}
	return WantsMouseCursor();
}

bool GameUISystem::OnUIMouseButtonDown(int Button, float X, float Y)
{
	if (!bRmlUiInitialized || !RmlContext)
		return false;

	RmlContext->ProcessMouseMove(static_cast<int>(X), static_cast<int>(Y), 0);
	RmlContext->ProcessMouseButtonDown(Button, 0);
	if (Button == 0 && TryBeginSettingsSliderDrag(X, Y))
		return true;
	return WantsMouseCursor();
}

bool GameUISystem::OnUIMouseButtonUp(int Button, float X, float Y)
{
	if (!bRmlUiInitialized || !RmlContext)
		return false;

	RmlContext->ProcessMouseMove(static_cast<int>(X), static_cast<int>(Y), 0);
	RmlContext->ProcessMouseButtonUp(Button, 0);
	if (Button == 0 && ActiveSettingsSlider != ESettingsSlider::None)
	{
		EndSettingsSliderDrag();
		return true;
	}
	if (Button == 0 && CurrentState == EGameUIState::Ending && DialoguePanel::AdvanceOrSkip())
		return true;
	return WantsMouseCursor();
}

bool GameUISystem::OnUIKeyDown(int VK)
{
	if (VK == VK_OEM_3 && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) // Debug: Ctrl + Backtick key (`)
	{
		if (bDebugMenuOpen)
			CloseDebugMenu();
		else
			OpenDebugMenu();
		return true;
	}

	if (!bRmlUiInitialized || !RmlContext)
		return false;

	const Rml::Input::KeyIdentifier Key = ToRmlKey(VK);
	if (Key != Rml::Input::KI_UNKNOWN)
		RmlContext->ProcessKeyDown(Key, 0);

	if (bSettingsOpen)
	{
		if (VK == VK_ESCAPE)
		{
			CloseSettings();
		}
		return true;
	}

	if (bCreditsOpen)
	{
		if (VK == VK_ESCAPE)
		{
			CloseCredits();
		}
		return true;
	}

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

	if (bItemInspectOpen)
	{
		if (VK == VK_ESCAPE || VK == 'Q')
			HideItemInspect();
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

	if (bSettingsOpen)
		return true;

	if (bCreditsOpen)
		return true;

	if (CurrentState == EGameUIState::StartMenu)
		return true;

	if (bItemInspectOpen)
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

	LastUiWidth = Width;
	LastUiHeight = Height;

	double Now = LastRmlUpdateTime;
	if (RmlSystemInterface)
		Now = RmlSystemInterface->GetElapsedTime();

	const float DeltaTime = LastRmlUpdateTime > 0.0 ? static_cast<float>(Now - LastRmlUpdateTime) : 0.0f;
	LastRmlUpdateTime = Now;

	DialoguePanel::Tick(DeltaTime, Mode);
	if (CurrentState == EGameUIState::Ending)
		EndingPanel::Tick(DeltaTime);

	if (RmlRenderInterface)
		RmlRenderInterface->SetFlashFactor(0.0f);

	TickTitleTransitions(DeltaTime);
	if (CurrentState == EGameUIState::InGame && !bPauseMenuOpen && Mode == EUIRenderMode::Play)
	{
		ElapsedTime += std::max(0.0f, DeltaTime);
	}

	float MaxTime = 300.0f; // 5분
	float RemainingTime = std::max(0.0f, MaxTime - ElapsedTime);

	if (RemainingTime <= 0.0f && CurrentState == EGameUIState::InGame && Mode == EUIRenderMode::Play)
	{
		SetEndingType(EEndingType::Bad);
		SetState(EGameUIState::Ending);
	}

	RmlDocument->SetClass("is-preview", Mode == EUIRenderMode::Preview);

	if (Rml::Element* TimerElement = RmlDocument->GetElementById("elapsed-time-value"))
	{
		TimerElement->SetClass("critical", RemainingTime <= 60.0f);
	}

	const bool bShowStart = CurrentState == EGameUIState::StartMenu && Mode == EUIRenderMode::Play;
	const bool bShowHud = CurrentState == EGameUIState::InGame;
	const bool bShowPause = CurrentState == EGameUIState::InGame && bPauseMenuOpen;
	const bool bShowSettings = bSettingsOpen && (bShowStart || bShowPause);
	const bool bShowCredits = bCreditsOpen && bShowStart;
	const bool bShowDialogue = DialoguePanel::IsActive() &&
		(CurrentState == EGameUIState::InGame || CurrentState == EGameUIState::Ending || CurrentState == EGameUIState::Prologue);
	const bool bShowEnding = CurrentState == EGameUIState::Ending;
	const bool bShowTheEnd = bShowEnding && EndingPanel::ShouldShowTheEnd();
	const bool bShowEndingVisual = bShowEnding && !bShowTheEnd;
	const bool bShowEndingButtons = bShowTheEnd && EndingPanel::GetFadeAlpha() >= 0.8f;
	const bool bShowItemInspect = bShowHud && bItemInspectOpen;
	const bool bShowInteractionHint = bShowHud && !bShowPause && !bShowDialogue && !bShowItemInspect && InteractionHintType != EInteractionHintType::None;

	SetElementVisible("start-menu", bShowStart);
	SetElementVisible("settings-layer", bShowSettings);
	SetElementVisible("credits-layer", bShowCredits);
	SetElementVisible("hud-panel", bShowHud);
	SetElementVisible("elapsed-time-panel", bShowHud);
	SetElementVisible("item-status", bShowHud);
	SetElementVisible("crosshair-dot", bShowHud && !bShowPause);
	SetElementVisible("interaction-hint", bShowInteractionHint);
	SetElementVisible("pause-layer", bShowPause);
	SetElementVisible("item-inspect-panel", bShowItemInspect);
	SetElementVisible("debug-menu-layer", bDebugMenuOpen);
	SetElementVisible("dialogue-panel", bShowDialogue);
	SetElementVisible("ending-panel", bShowEnding);
	SetElementVisible("ending-visual-frame", bShowEndingVisual);
	SetElementVisible("the-end", bShowTheEnd);
	SetElementVisible("ending-buttons", bShowEndingButtons);
	UpdateTitleTransitionElements();

	const bool bShowCustomCursor = WantsCustomCursor();
	SetElementVisible("game-cursor", bShowCustomCursor);
	SetElementProperty("game-cursor", "left", FormatPixels(CustomCursorX));
	SetElementProperty("game-cursor", "top", FormatPixels(CustomCursorY));
	SetElementAttribute("game-cursor", "src", bTitleButtonHovered ? "Asset/Texture/CursorHovered.png" : "Asset/Texture/CursorDefault.png");

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
	const std::string RemainingTimeText = FormatTime(RemainingTime);
	SetElementText("elapsed-time-value", RemainingTimeText);
	SetElementText("pause-time", RemainingTimeText);
	SetElementText("current-item-name", CurrentItemName.empty() ? "No item" : CurrentItemName);
	UpdateSettingsElements();
	// SetElementText("current-item-desc", CurrentItemDesc.empty() ? "Nothing selected" : CurrentItemDesc);

	const bool bShowInspectHint = InteractionHintType == EInteractionHintType::DropWithInspect;
	SetElementProperty("interaction-secondary-key", "display", bShowInspectHint ? "inline-block" : "none");
	SetElementProperty("interaction-secondary-text", "display", bShowInspectHint ? "inline-block" : "none");
	switch (InteractionHintType)
	{
	case EInteractionHintType::Pickup:
		SetElementText("interaction-secondary-key-label", "Q");
		SetElementText("interaction-secondary-text", "살펴보기");
		SetElementText("interaction-key-label", "E");
		SetElementText("interaction-hint-text", "들기");
		break;
	case EInteractionHintType::Keep:
		SetElementText("interaction-secondary-key-label", "");
		SetElementText("interaction-secondary-text", "");
		SetElementText("interaction-key-label", "E");
		SetElementText("interaction-hint-text", "보관하기");
		break;
	case EInteractionHintType::Discard:
		SetElementText("interaction-secondary-key-label", "");
		SetElementText("interaction-secondary-text", "");
		SetElementText("interaction-key-label", "E");
		SetElementText("interaction-hint-text", "버리기");
		break;
	case EInteractionHintType::Drop:
	case EInteractionHintType::DropWithInspect:
		SetElementText("interaction-secondary-key-label", "Q");
		SetElementText("interaction-secondary-text", "살펴보기");
		SetElementText("interaction-key-label", "E");
		SetElementText("interaction-hint-text", "놓기");
		break;
	default:
		SetElementText("interaction-secondary-key-label", "");
		SetElementText("interaction-secondary-text", "");
		SetElementText("interaction-key-label", "");
		SetElementText("interaction-hint-text", "");
		break;
	}

	SetElementText("item-inspect-title", InspectItemName.empty() ? "Item" : InspectItemName);
	SetElementText("item-inspect-desc", InspectItemDesc);
	SetElementVisible("item-inspect-image", !InspectItemIconPath.empty());
	SetElementVisible("item-inspect-image-placeholder", InspectItemIconPath.empty());
	if (!InspectItemIconPath.empty())
		SetElementAttribute("item-inspect-image", "src", InspectItemIconPath);

	SetElementText("dialogue-speaker", DialoguePanel::GetSpeaker());
	SetElementText("dialogue-text", DialoguePanel::GetVisibleText());
	SetElementText("dialogue-hint", CurrentState == EGameUIState::Ending ? "[CLICK] >" : "[SPACE] >");
	SetElementVisible("dialogue-hint", DialoguePanel::IsTextComplete());

	SetElementAttribute("ending-visual-image", "src", EndingPanel::GetImagePath());

	const int Alpha = static_cast<int>(EndingPanel::GetFadeAlpha() * 255.0f);
	SetElementProperty("the-end", "color", "rgba(220, 210, 190, " + std::to_string(Alpha) + ")");

	if (bStartGameTransitionReady)
		FinishStartGameTransition();
}

void GameUISystem::ResetTitleIntro()
{
	TitleIntroElapsed = 0.0f;
	bTitleButtonHovered = false;
	bStartGameTransitionActive = false;
	StartGameTransitionElapsed = 0.0f;
	bStartGameTransitionReady = false;
}

void GameUISystem::TickTitleTransitions(float DeltaTime)
{
	if (CurrentState == EGameUIState::StartMenu)
		TitleIntroElapsed += std::max(0.0f, DeltaTime);

	if (!bStartGameTransitionActive)
		return;

	constexpr float TransitionDuration = 1.5f;
	StartGameTransitionElapsed += std::max(0.0f, DeltaTime);
	if (StartGameTransitionElapsed >= TransitionDuration)
		bStartGameTransitionReady = true;
}

void GameUISystem::UpdateTitleTransitionElements()
{
	const bool bInStartMenu = CurrentState == EGameUIState::StartMenu;

	constexpr float IntroFadeInDuration = 1.0f;
	constexpr float IntroHoldDuration = 3.0f;
	constexpr float IntroFadeOutDuration = 1.0f;
	constexpr float IntroFadeOutStart = IntroFadeInDuration + IntroHoldDuration;
	constexpr float IntroTotalDuration = IntroFadeOutStart + IntroFadeOutDuration;

	float IntroIconAlpha = 1.0f;
	if (TitleIntroElapsed < IntroFadeInDuration)
	{
		IntroIconAlpha = TitleIntroElapsed / IntroFadeInDuration;
	}
	else if (TitleIntroElapsed >= IntroFadeOutStart)
	{
		IntroIconAlpha = 1.0f - ((TitleIntroElapsed - IntroFadeOutStart) / IntroFadeOutDuration);
	}
	IntroIconAlpha = bInStartMenu ? std::clamp(IntroIconAlpha, 0.0f, 1.0f) : 0.0f;

	const float IntroLayerAlpha = bInStartMenu && TitleIntroElapsed >= IntroFadeOutStart ? IntroIconAlpha : (bInStartMenu ? 1.0f : 0.0f);
	const bool bShowIntro = bInStartMenu && TitleIntroElapsed < IntroTotalDuration && !bStartGameTransitionActive;

	float IntroIconBlink = 0.0f;
	constexpr float BlinkStartTime = 1.5f;
	constexpr float BlinkDuration = 0.8f;
	if (TitleIntroElapsed >= BlinkStartTime && TitleIntroElapsed < BlinkStartTime + BlinkDuration)
	{
		float t = (TitleIntroElapsed - BlinkStartTime) / BlinkDuration;
		IntroIconBlink = std::sin(t * 3.14159265f);
	}

	if (RmlRenderInterface)
	{
		RmlRenderInterface->SetFlashFactor(bShowIntro ? IntroIconBlink : 0.0f);
	}

	constexpr float StartFadeDuration = 1.0f;
	const float StartFadeAlpha = bStartGameTransitionActive ? std::clamp(StartGameTransitionElapsed / StartFadeDuration, 0.0f, 1.0f) : 0.0f;

	constexpr float TitleFlickerSpeed = 7.0f;
	constexpr float TitleFlickerStrength = 0.08f;
	const float TitleFlickerWave =
		0.5f +
		0.32f * std::sin(TitleIntroElapsed * TitleFlickerSpeed) +
		0.18f * std::sin(TitleIntroElapsed * 17.0f + 0.9f);
	const float TitleOpacity = bInStartMenu && !bShowIntro
		? 1.0f - (TitleFlickerStrength * std::clamp(TitleFlickerWave, 0.0f, 1.0f))
		: 1.0f;

	SetElementProperty("game-title", "opacity", FormatOpacity(TitleOpacity));
	SetElementVisible("title-intro-layer", bShowIntro);
	SetElementProperty("title-intro-layer", "background-color", FormatAlphaColor(0.0f, 0.0f, 0.0f, IntroLayerAlpha));
	SetElementProperty("title-intro-icon", "opacity", FormatOpacity(IntroIconAlpha));
	
	SetElementVisible("screen-fade-layer", bStartGameTransitionActive);
	SetElementProperty("screen-fade-layer", "background-color", FormatAlphaColor(0.0f, 0.0f, 0.0f, StartFadeAlpha));
}

void GameUISystem::FinishStartGameTransition()
{
	bStartGameTransitionReady = false;

	if (StartGameCallback)
		StartGameCallback();
	else
		SetState(EGameUIState::InGame);

	bStartGameTransitionActive = false;
	StartGameTransitionElapsed = 0.0f;
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
		GameUISystem::Get().SetPauseMenuOpen(false);
	});

	ExitClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().RequestExitPlay();
	});

	SettingsOpenClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().OpenSettings();
	});

	SettingsCloseClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().CloseSettings();
	});

	CreditsOpenClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().OpenCredits();
	});

	CreditsCloseClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().CloseCredits();
	});

	PauseTitleClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().RequestExitToTitle();
	});

	SaveScoreClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().RequestSaveScore();
	});

	ExitToMainClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().RequestExitToTitle();
	});

	DebugMenuCloseClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().CloseDebugMenu();
	});

	DebugJumpBadClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().CloseDebugMenu();
		GameUISystem::Get().SetEndingType(EEndingType::Bad);
		GameUISystem::Get().SetState(EGameUIState::Ending);
	});

	DebugJumpNormalClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().CloseDebugMenu();
		GameUISystem::Get().SetEndingType(EEndingType::Normal);
		GameUISystem::Get().SetState(EGameUIState::Ending);
	});

	DebugJumpGoodClickListener = std::make_unique<FRmlUiClickListener>([]()
	{
		GameUISystem::Get().CloseDebugMenu();
		GameUISystem::Get().SetEndingType(EEndingType::Good);
		GameUISystem::Get().SetState(EGameUIState::Ending);
	});

	if (Rml::Element* Element = RmlDocument->GetElementById("start-button"))
		Element->AddEventListener("click", StartClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("retry-button"))
		Element->AddEventListener("click", RetryClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("exit-button"))
		Element->AddEventListener("click", ExitClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("pause-exit-button"))
		Element->AddEventListener("click", ExitClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("settings-button"))
		Element->AddEventListener("click", SettingsOpenClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("pause-settings-button"))
		Element->AddEventListener("click", SettingsOpenClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("settings-close-button"))
		Element->AddEventListener("click", SettingsCloseClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("credits-button"))
		Element->AddEventListener("click", CreditsOpenClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("credits-close-button"))
		Element->AddEventListener("click", CreditsCloseClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("pause-title-button"))
		Element->AddEventListener("click", PauseTitleClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("save-score-button"))
		Element->AddEventListener("click", SaveScoreClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("exit-to-main-button"))
		Element->AddEventListener("click", ExitToMainClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("debug-menu-close"))
		Element->AddEventListener("click", DebugMenuCloseClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("debug-jump-bad"))
		Element->AddEventListener("click", DebugJumpBadClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("debug-jump-normal"))
		Element->AddEventListener("click", DebugJumpNormalClickListener.get());
	if (Rml::Element* Element = RmlDocument->GetElementById("debug-jump-good"))
		Element->AddEventListener("click", DebugJumpGoodClickListener.get());

	for (const char* ButtonId : TitleButtonIds)
	{
		TitleButtonHoverEnterListeners.emplace_back(std::make_unique<FRmlUiClickListener>([ButtonId]()
		{
			GameUISystem& UI = GameUISystem::Get();
			UI.bTitleButtonHovered = true;
			UI.SetElementProperty(ButtonId, "opacity", "0.82");
		}));

		TitleButtonHoverLeaveListeners.emplace_back(std::make_unique<FRmlUiClickListener>([ButtonId]()
		{
			GameUISystem& UI = GameUISystem::Get();
			UI.bTitleButtonHovered = false;
			UI.SetElementProperty(ButtonId, "opacity", "1.0");
		}));

		if (Rml::Element* Element = RmlDocument->GetElementById(ButtonId))
		{
			Element->AddEventListener("mouseover", TitleButtonHoverEnterListeners.back().get());
			Element->AddEventListener("mouseout", TitleButtonHoverLeaveListeners.back().get());
		}
	}
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

void GameUISystem::SetElementAttribute(const char* Id, const char* Attribute, const std::string& Value)
{
	if (!RmlDocument)
		return;

	if (Rml::Element* Element = RmlDocument->GetElementById(Id))
		Element->SetAttribute(Attribute, Value);
}
