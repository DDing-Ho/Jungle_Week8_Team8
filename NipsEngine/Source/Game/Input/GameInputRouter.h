#pragma once

#include "Engine/Input/InputTypes.h"
#include "Game/Input/GamePlayerController.h"

class FGameInputRouter
{
public:
	FGameInputRouter() = default;
	~FGameInputRouter() = default;

	void Tick(float DeltaTime);
	void RouteKeyboardInput(EKeyInputType Type, int VK);
	void RouteMouseInput(EMouseInputType Type, float DeltaX, float DeltaY);

	FGamePlayerController& GetPlayerController() { return PlayerController; }
	const FGamePlayerController& GetPlayerController() const { return PlayerController; }

private:
	FGamePlayerController PlayerController;
};
