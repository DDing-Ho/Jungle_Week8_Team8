#pragma once

#include "Engine/UI/GameUISystem.h"

class EndingPanel
{
public:
    static void Render(EUIRenderMode Mode);
    static void Reset();

private:
    static float FadeTimer;
    static bool  bShowTheEnd;
    static bool  bExitCalled;
};
