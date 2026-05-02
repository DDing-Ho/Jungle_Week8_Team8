#include "GameEngine.h"

#include "Game/GameTypes.h"
#include "Game/Config/GameConfig.h"
#include "Engine/Serialization/SceneSaveManager.h"
#include "Core/Paths.h"
#include "Settings/EngineSettings.h"
#include "GameFramework/World.h"

#include <Windows.h>

DEFINE_CLASS(UGameEngine, UEngine)

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

	void NormalizeGameWorldContext(FWorldContext& Context)
	{
		Context.WorldType = EWorldType::Game;
		Context.ContextHandle = GameWorldHandle;
		Context.ContextName = GameWorldName;

		if (Context.World)
		{
			Context.World->SetWorldType(EWorldType::Game);
			FEngineSettings::Get().ApplyToSpatialPolicy(Context.World->GetSpatialIndex().GetMaintenancePolicy());
		}
	}
}

void UGameEngine::Init(FWindowsWindow* InWindow)
{
    UEngine::Init(InWindow);

    Game::RegisterGameTypes();

    LoadStartupScene();
}

void UGameEngine::LoadStartupScene()
{
    const FString ScenePath = FPaths::ToString(
        FPaths::Combine(FPaths::SceneDir(), GameConfig::StartupSceneName));

    GameLog(("Loading startup scene: " + ScenePath).c_str());

    FWorldContext Ctx;
    FSceneSaveManager::Load(ScenePath, Ctx, nullptr);

    if (!Ctx.World)
    {
        GameLog(("Failed to load startup scene. Path: " + ScenePath).c_str());
		FWorldContext& FallbackCtx = CreateWorldContext(EWorldType::Game, GameWorldHandle, GameWorldName);
		NormalizeGameWorldContext(FallbackCtx);
		SetActiveWorld(FallbackCtx.ContextHandle);
		GameLog("Created fallback empty game world.");
        return;
    }

	NormalizeGameWorldContext(Ctx);

    WorldList.push_back(Ctx);
    SetActiveWorld(Ctx.ContextHandle);

    GameLog(("Startup scene loaded. Handle: " + Ctx.ContextHandle.ToString()).c_str());
}

void UGameEngine::Tick(float DeltaTime)
{
    UEngine::Tick(DeltaTime);
}

void UGameEngine::Shutdown()
{
    UEngine::Shutdown();
}

void UGameEngine::OnWindowResized(uint32 Width, uint32 Height)
{
    UEngine::OnWindowResized(Width, Height);
}
