#include "Game/UI/EndingPanel.h"

#include "Game/UI/DialoguePanel.h"
#include "Game/UI/GameUISystem.h"

#include <algorithm>

float EndingPanel::FadeTimer = 0.f;
bool EndingPanel::bShowTheEnd = false;
bool EndingPanel::bExitCalled = false;

void EndingPanel::Reset()
{
	FadeTimer = 0.f;
	bShowTheEnd = false;
	bExitCalled = false;
}

void EndingPanel::Tick(float DeltaTime)
{
	if (DialoguePanel::IsActive())
		return;

	bShowTheEnd = true;
	FadeTimer += std::max(0.0f, DeltaTime);
}

bool EndingPanel::ShouldShowTheEnd()
{
	return bShowTheEnd;
}

float EndingPanel::GetFadeAlpha()
{
	constexpr float FadeDuration = 2.0f;
	return std::clamp(FadeTimer / FadeDuration, 0.0f, 1.0f);
}

void EndingPanel::Render(EUIRenderMode Mode)
{
	(void)Mode;
}
