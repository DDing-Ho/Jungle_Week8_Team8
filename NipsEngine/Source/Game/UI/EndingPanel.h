#pragma once

#include "Game/UI/GameUISystem.h"

class EndingPanel
{
public:
    static void Render(EUIRenderMode Mode);
    static void Tick(float DeltaTime);
    static void Reset();
    static bool ShouldShowTheEnd();
    static float GetFadeAlpha();

private:
    static float FadeTimer;
    static bool  bShowTheEnd;
    static bool  bExitCalled;
};
