#include "Game/UI/StartMenuPanel.h"

#include "Engine/Input/InputRouter.h"
#include "Engine/Viewport/ViewportRect.h"
#include "ImGui/imgui.h"

#include <Windows.h>

// -------------------------------------------------------
// 타이틀 이름 - 게임 이름으로 교체
// -------------------------------------------------------
static constexpr const char* GameTitle = "GAME TITLE";

// -------------------------------------------------------
// 텍스트 버튼 헬퍼
//   커서 위치에 InvisibleButton 을 놓고,
//   호버 여부에 따라 텍스트 색을 바꿔서 그린다.
// -------------------------------------------------------
static bool TextButton(ImDrawList* Draw,
					   const char* Label,
					   float WinX, float WinY,   // 윈도우 스크린 좌표
					   float CursorX, float CursorY,  // SetCursorPos 에 넘길 값
					   float BtnW, float BtnH,
					   EUIRenderMode Mode)
{
	ImGui::SetCursorPos(ImVec2(CursorX, CursorY));
	ImGui::InvisibleButton(Label, ImVec2(BtnW, BtnH));

	const bool bHovered = (Mode == EUIRenderMode::Play) && ImGui::IsItemHovered();
	const bool bClicked = (Mode == EUIRenderMode::Play) && ImGui::IsItemClicked();

	const ImVec2 LabelSize = ImGui::CalcTextSize(Label);
	const ImVec2 TextPos   = ImVec2(
		WinX + CursorX + (BtnW  - LabelSize.x) * 0.5f,
		WinY + CursorY + (BtnH  - LabelSize.y) * 0.5f
	);
	const ImU32 Color = bHovered
		? IM_COL32(255, 220, 100, 255)   // 호버 - 골든
		: IM_COL32(190, 190, 190, 255);  // 기본 - 밝은 회색

	Draw->AddText(TextPos, Color, Label);

	return bClicked;
}

void StartMenuPanel::Render(EUIRenderMode Mode)
{
	ImGuiIO& IO = ImGui::GetIO();
	const FViewportRect& VR   = FInputRouter::GetGuiInputState().ViewportHostRect;
	const bool           bHasVR = (VR.Width > 0);

	const float VX = bHasVR ? static_cast<float>(VR.X)      : 0.f;
	const float VY = bHasVR ? static_cast<float>(VR.Y)      : 0.f;
	const float VW = bHasVR ? static_cast<float>(VR.Width)  : IO.DisplaySize.x;
	const float VH = bHasVR ? static_cast<float>(VR.Height) : IO.DisplaySize.y;

	// ── 전체 화면 윈도우 ────────────────────────────────────────
	ImGui::SetNextWindowPos(ImVec2(VX, VY));
	ImGui::SetNextWindowSize(ImVec2(VW, VH));
	ImGui::SetNextWindowBgAlpha(1.f);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.07f, 1.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));

	constexpr ImGuiWindowFlags Flags =
		ImGuiWindowFlags_NoDecoration   |
		ImGuiWindowFlags_NoMove         |
		ImGuiWindowFlags_NoSavedSettings;

	ImGui::Begin("##StartMenu", nullptr, Flags);

	ImDrawList* Draw = ImGui::GetWindowDrawList();

	// ── 타이틀 ─────────────────────────────────────────────────
	const float TitleY    = VH * 0.36f;
	const float LineW     = VW * 0.3f;
	const float LineY     = TitleY - 10.f;
	const float LineBotY  = TitleY + ImGui::GetFontSize() + 8.f;

	// 위 구분선
	Draw->AddLine(
		ImVec2(VX + (VW - LineW) * 0.5f, VY + LineY),
		ImVec2(VX + (VW + LineW) * 0.5f, VY + LineY),
		IM_COL32(130, 130, 160, 180)
	);

	// 타이틀 텍스트
	const ImVec2 TitleSize = ImGui::CalcTextSize(GameTitle);
	Draw->AddText(
		ImVec2(VX + (VW - TitleSize.x) * 0.5f, VY + TitleY),
		IM_COL32(255, 255, 255, 255),
		GameTitle
	);

	// 아래 구분선
	Draw->AddLine(
		ImVec2(VX + (VW - LineW) * 0.5f, VY + LineBotY),
		ImVec2(VX + (VW + LineW) * 0.5f, VY + LineBotY),
		IM_COL32(130, 130, 160, 180)
	);

	// ── 버튼 ────────────────────────────────────────────────────
	const float BtnW   = 100.f;
	const float BtnH   = ImGui::GetFontSize() + 6.f;
	const float BtnX   = (VW - BtnW) * 0.5f;   // 윈도우 내 커서 X

	const float StartY = VH * 0.50f;
	const float ExitY  = StartY + BtnH + 12.f;

	if (TextButton(Draw, "START", VX, VY, BtnX, StartY, BtnW, BtnH, Mode))
		GameUISystem::Get().SetState(EGameUIState::InGame);

	if (TextButton(Draw, "EXIT", VX, VY, BtnX, ExitY, BtnW, BtnH, Mode))
		PostQuitMessage(0);

	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}
