#include "Game/UI/EndingPanel.h"
#include "Game/UI/DialoguePanel.h"
#include "Game/UI/GameUISystem.h"
#include "Engine/Input/InputRouter.h"
#include "Engine/Viewport/ViewportRect.h"
#include "ImGui/imgui.h"

float EndingPanel::FadeTimer   = 0.f;
bool  EndingPanel::bShowTheEnd = false;
bool  EndingPanel::bExitCalled = false;

void EndingPanel::Reset()
{
	FadeTimer   = 0.f;
	bShowTheEnd = false;
	bExitCalled = false;
}

void EndingPanel::Render(EUIRenderMode Mode)
{
	ImGuiIO& IO = ImGui::GetIO();
	const FViewportRect& VR    = FInputRouter::GetGuiInputState().ViewportHostRect;
	const bool           bHasVR = (VR.Width > 0);

	const float VX = bHasVR ? static_cast<float>(VR.X)     : 0.f;
	const float VY = bHasVR ? static_cast<float>(VR.Y)     : 0.f;
	const float VW = bHasVR ? static_cast<float>(VR.Width)  : IO.DisplaySize.x;
	const float VH = bHasVR ? static_cast<float>(VR.Height) : IO.DisplaySize.y;

	const float DeltaTime = IO.DeltaTime;

	// 대화가 끝난 뒤 "THE END" 페이드인 → PIE 자동 종료
	if (!DialoguePanel::IsActive())
	{
		bShowTheEnd = true;
		FadeTimer  += DeltaTime;

		// 페이드 완료(2초) + 여운(1.5초) 후 PIE 종료 요청
		// 게임 빌드에서는 콜백 미등록이므로 아무 일도 일어나지 않음
		constexpr float ExitAfter = 3.5f;
		if (!bExitCalled && FadeTimer >= ExitAfter)
		{
			bExitCalled = true;
			GameUISystem::Get().RequestExitPlay();
		}
	}

	// 전체 화면 어두운 배경
	ImDrawList* Draw = ImGui::GetForegroundDrawList();
	Draw->AddRectFilled(
		ImVec2(VX, VY),
		ImVec2(VX + VW, VY + VH),
		IM_COL32(8, 8, 12, 230)
	);

	// 대화가 끝난 후 "THE END" 텍스트 페이드인
	if (bShowTheEnd)
	{
		constexpr float FadeDuration = 2.0f;
		const float     Alpha        = FadeTimer < FadeDuration
										   ? (FadeTimer / FadeDuration)
										   : 1.0f;
		const ImU32     TextColor    = IM_COL32(
			220, 210, 190,
			static_cast<int>(Alpha * 255)
		);

		const char*  TheEnd   = "THE END";
		const ImVec2 TextSize = ImGui::CalcTextSize(TheEnd);
		Draw->AddText(
			ImVec2(VX + (VW - TextSize.x) * 0.5f,
				   VY + (VH - TextSize.y) * 0.5f),
			TextColor,
			TheEnd
		);
	}
}
