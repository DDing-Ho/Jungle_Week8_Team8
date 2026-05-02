#include "GameEngine.h"

#include "Game/GameTypes.h"
#include "Game/Settings/GameSettings.h"
#include "Game/Viewport/GameViewportClient.h"
#include "Game/Render/GameRenderPipeline.h"
#include "Engine/Audio/AudioSystem.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Serialization/SceneSaveManager.h"
#include "Engine/Core/Paths.h"
#include "Engine/Settings/EngineSettings.h"
#include "Engine/GameFramework/World.h"

#include <Windows.h>

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
}

void UGameEngine::Init(FWindowsWindow* InWindow)
{
	UEngine::Init(InWindow);

	Game::RegisterGameTypes();

	SetRenderPipeline(std::make_unique<FGameRenderPipeline>(this, Renderer));

	LoadStartupScene();

	GameViewport = std::make_unique<FGameViewportClient>();
	GameViewport->Initialize(InWindow);
	GameViewport->SetWorld(GetWorld());
}

void UGameEngine::LoadStartupScene()
{
	const FString ScenePath = FPaths::ToString(FPaths::Combine(FPaths::SceneDir(), GameSettings::StartupSceneName));

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

	GameLog(("Startup scene loaded. Handle: " + Ctx.ContextHandle.ToString()).c_str());
}

void UGameEngine::Tick(float DeltaTime)
{
	InputSystem::Get().Tick();
	GameViewport->Tick(DeltaTime);
	WorldTick(DeltaTime);
	FAudioSystem::Get().Tick(DeltaTime);
	Render(DeltaTime);
}

void UGameEngine::Shutdown()
{
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
