#include "Game/UI/PauseMenuPanel.h"

#include "Engine/Input/InputRouter.h"
#include "Engine/Viewport/ViewportRect.h"
#include "ImGui/imgui.h"

#include <cstdio>
#include <Windows.h>

void PauseMenuPanel::Render(EUIRenderMode Mode)
{
	ImGuiIO& IO = ImGui::GetIO();

	const FViewportRect& VR = FInputRouter::GetGuiInputState().ViewportHostRect;
	const float VX = (VR.Width > 0) ? static_cast<float>(VR.X) : 0.f;
	const float VY = (VR.Width > 0) ? static_cast<float>(VR.Y) : 0.f;
	const float VW = (VR.Width > 0) ? static_cast<float>(VR.Width)  : IO.DisplaySize.x;
	const float VH = (VR.Width > 0) ? static_cast<float>(VR.Height) : IO.DisplaySize.y;

	constexpr ImGuiWindowFlags OverlayFlags =
		ImGuiWindowFlags_NoDecoration   |
		ImGuiWindowFlags_NoNav          |
		ImGuiWindowFlags_NoMove         |
		ImGuiWindowFlags_NoInputs       |
		ImGuiWindowFlags_NoSavedSettings|
		ImGuiWindowFlags_NoBringToFrontOnFocus;

	// ── 1. 반투명 오버레이 (ImGui 윈도우로 — DrawList 쓰면 메뉴 위에 덮임) ──
	ImGui::SetNextWindowPos(ImVec2(VX, VY));
	ImGui::SetNextWindowSize(ImVec2(VW, VH));
	ImGui::SetNextWindowBgAlpha(0.55f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 1.f));
	ImGui::Begin("##PauseOverlay", nullptr, OverlayFlags);
	ImGui::End();
	ImGui::PopStyleColor();

	// ── 2. 메뉴 패널 (오버레이보다 나중에 Begin → 위에 그려짐) ──────────
	const float PanelW = 300.f;
	const float PanelH = 290.f;
	const float PanelX = VX + (VW - PanelW) * 0.5f;
	const float PanelY = VY + (VH - PanelH) * 0.5f;

	ImGui::SetNextWindowPos(ImVec2(PanelX, PanelY));
	ImGui::SetNextWindowSize(ImVec2(PanelW, PanelH));
	ImGui::SetNextWindowFocus();  // 에디터 패널보다 앞으로 이동

	constexpr ImGuiWindowFlags MenuFlags =
		ImGuiWindowFlags_NoDecoration   |
		ImGuiWindowFlags_NoMove         |
		ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 0.97f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(24.f, 20.f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(8.f, 10.f));

	if (ImGui::Begin("##PauseMenu", nullptr, MenuFlags))
	{
		// [MENU] 타이틀 중앙 정렬
		const char* Title  = "[MENU]";
		const float TitleW = ImGui::CalcTextSize(Title).x;
		ImGui::SetCursorPosX((PanelW - TitleW) * 0.5f);
		ImGui::TextUnformatted(Title);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// 통계
		ImGui::Text("발견한 아이템  :  %d개",    GameUISystem::Get().GetItemCount());

		const int TotalSec = static_cast<int>(GameUISystem::Get().GetElapsedTime());
		ImGui::Text("실행 시간      :  %d분 %02d초", TotalSec / 60, TotalSec % 60);

		ImGui::Text("청결도         :  %d%%",
			static_cast<int>(GameUISystem::Get().GetProgress() * 100.f));

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// 버튼
		const float BtnW = PanelW - 48.f;

		if (Mode == EUIRenderMode::Preview)
			ImGui::BeginDisabled();

		if (ImGui::Button("RETRY", ImVec2(BtnW, 38.f)))
		{
			GameUISystem::Get().ResetGameData();
			GameUISystem::Get().SetPauseMenuOpen(false);
		}

		ImGui::Spacing();

		if (ImGui::Button("EXIT", ImVec2(BtnW, 38.f)))
		{
			PostQuitMessage(0);
		}

		if (Mode == EUIRenderMode::Preview)
			ImGui::EndDisabled();
	}
	ImGui::End();

	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor();
}
