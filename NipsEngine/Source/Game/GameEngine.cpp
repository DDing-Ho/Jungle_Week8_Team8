#include "GameEngine.h"

#include "Game/GameTypes.h"
#include "Game/Settings/GameSettings.h"
#include "Game/Systems/GameContext.h"
#include "Game/Systems/ItemSystem.h"
#include "Game/Viewport/GameViewportClient.h"
#include "Game/Render/GameRenderPipeline.h"
#include "Engine/Audio/AudioSystem.h"
#include "Engine/Input/InputRouter.h"
#include "Engine/Serialization/SceneSaveManager.h"
#include "Engine/Core/Paths.h"
#include "Engine/Settings/EngineSettings.h"
#include "Engine/GameFramework/World.h"
#include "Engine/Runtime/WindowsWindow.h"

#include "Game/UI/GameUISystem.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Render/Renderer/Renderer.h"

#include <Windows.h>
#include <commdlg.h>

DEFINE_CLASS(UGameEngine, UEngine)
REGISTER_FACTORY(UGameEngine)

UGameEngine::UGameEngine() = default;
UGameEngine::~UGameEngine() = default;

// GameEngine 전역 Logger
static void GameLog(const char* Msg)
{
	OutputDebugStringA("[GameEngine] ");
	OutputDebugStringA(Msg);
	OutputDebugStringA("\n");
}

namespace
{
	const FName GameWorldHandle("GameWorld");
	const FString GameWorldName = "GameWorld";

	void ApplyDefaultContext(FWorldContext& Context)
	{
		Context.WorldType = EWorldType::Game;
		Context.ContextHandle = GameWorldHandle;
		Context.ContextName = GameWorldName;

		if (Context.World)
		{
			Context.World->SetWorldType(EWorldType::Game);
			FWorldSpatialIndex::FMaintenancePolicy& Policy = Context.World->GetSpatialIndex().GetMaintenancePolicy();
			FEngineSettings::Get().ApplyToSpatialPolicy(Policy);
		}
	}

	// 디버그용 임시 함수: 추후 GameSettings에 저장된 Scene 파일을 불러오도록 변경
	FString OpenScene(HWND OwnerWindow)
	{
		WCHAR FileBuffer[MAX_PATH] = {};
		const std::wstring InitialDir = FPaths::SceneDir();

		OPENFILENAMEW Ofn{};
		Ofn.lStructSize = sizeof(Ofn);
		Ofn.hwndOwner = OwnerWindow;
		Ofn.lpstrFilter = L"Nips Scene (*.Scene)\0*.Scene\0All Files (*.*)\0*.*\0";
		Ofn.lpstrFile = FileBuffer;
		Ofn.nMaxFile = MAX_PATH;
		Ofn.lpstrInitialDir = InitialDir.c_str();
		Ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		Ofn.lpstrDefExt = L"Scene";

		if (!GetOpenFileNameW(&Ofn))
		{
			return "";
		}

		return FPaths::ToUtf8(FileBuffer);
	}
}

void UGameEngine::Init(FWindowsWindow* InWindow)
{
	UEngine::Init(InWindow);

	// DefaultRenderPipeline → GameRenderPipeline 교체 (UI 렌더 포함)
	SetRenderPipeline(std::make_unique<FGameRenderPipeline>(this, GetRenderer()));

	// GameUISystem 초기화 (ImGui 컨텍스트)
	GameUISystem::Get().Init(
		InWindow->GetHWND(),
		GetRenderer().GetFD3DDevice().GetDevice(),
		GetRenderer().GetFD3DDevice().GetDeviceContext()
	);

	Game::RegisterGameTypes();
	GGameContext::Get().Reset();
	FItemSystem::Get().ResetRuntimeState();

	LoadStartupScene();

	GameViewport = std::make_unique<FGameViewportClient>();
	GameViewport->Initialize(InWindow);
	GameViewport->SetWorld(GetWorld());
}

void UGameEngine::LoadStartupScene()
{
	FString ScenePath = OpenScene(Window ? Window->GetHWND() : nullptr);
	if (ScenePath.empty())
	{
		ScenePath = FPaths::ToString(FPaths::Combine(FPaths::SceneDir(), GameSettings::StartupSceneName));
	}

	FWorldContext Ctx;
	FSceneSaveManager::Load(ScenePath, Ctx, nullptr);

	if (!Ctx.World)
	{
		FWorldContext& DefaultCtx = CreateWorldContext(EWorldType::Game, GameWorldHandle, GameWorldName);
		ApplyDefaultContext(DefaultCtx);
		SetActiveWorld(DefaultCtx.ContextHandle);
		return;
	}

	ApplyDefaultContext(Ctx);
	WorldList.push_back(Ctx);
	SetActiveWorld(Ctx.ContextHandle);
}

void UGameEngine::Tick(float DeltaTime)
{
	FInputRouter::TickInputSystem();
	UpdateInputWorldType();
	GameViewport->Tick(DeltaTime);
	WorldTick(DeltaTime);
	FAudioSystem::Get().Tick(DeltaTime);
	Render(DeltaTime);
}

void UGameEngine::Shutdown()
{
	FItemSystem::Get().ResetRuntimeState();
	GGameContext::Get().Reset();
	GameUISystem::Get().Shutdown();
	GameViewport.reset();
	UEngine::Shutdown();
}

void UGameEngine::OnWindowResized(uint32 Width, uint32 Height)
{
	UEngine::OnWindowResized(Width, Height);

	if (GameViewport)
	{
		GameViewport->SetViewportSize(static_cast<float>(Width), static_cast<float>(Height));
	}
}
	
