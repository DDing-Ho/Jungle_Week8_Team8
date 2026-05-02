#include "Engine/UI/HUDPanel.h"

#include "Engine/Input/InputSystem.h"
#include "Engine/Runtime/ViewportRect.h"
#include "ImGui/imgui.h"

#include <cstdio>

void HUDPanel::Render(EUIRenderMode Mode)
{
    ImGuiIO& IO = ImGui::GetIO();

    // 에디터: ViewportHostRect(뷰포트 패널 영역) 기준
    // 게임 빌드: 뷰포트가 전체 화면이므로 (0, 0, DisplaySize) 로 fallback
    const FViewportRect& VR = InputSystem::Get().GetGuiInputState().ViewportHostRect;
    const float VX = (VR.Width > 0) ? static_cast<float>(VR.X) : 0.f;
    const float VY = (VR.Width > 0) ? static_cast<float>(VR.Y) : 0.f;
    const float VW = (VR.Width > 0) ? static_cast<float>(VR.Width)  : IO.DisplaySize.x;

    const float BarW = VW * 0.4f;
    const float BarH = 28.f;
    const float PosX = VX + (VW - BarW) * 0.5f;
    const float PosY = VY + 14.f;
    const float Rounding = 6.f;

    const float Progress = (Mode == EUIRenderMode::Play)
        ? GameUISystem::Get().GetProgress()
        : 0.6f;

    char Overlay[8];
    snprintf(Overlay, sizeof(Overlay), "%d%%", static_cast<int>(Progress * 100.f));

    // GetForegroundDrawList: DockSpace·에디터 패널 위에 항상 최상단으로 그림
    ImDrawList* Draw = ImGui::GetForegroundDrawList();

    const ImVec2 BgMin (PosX,                       PosY);
    const ImVec2 BgMax (PosX + BarW,                PosY + BarH);
    const ImVec2 FillMax(PosX + BarW * Progress,    PosY + BarH);

    // 배경
    Draw->AddRectFilled(BgMin, BgMax, IM_COL32(26, 26, 26, 210), Rounding);
    // 채워진 부분 (하늘색)
    if (Progress > 0.f)
        Draw->AddRectFilled(BgMin, FillMax, IM_COL32(64, 191, 255, 255), Rounding);
    // 테두리
    Draw->AddRect(BgMin, BgMax, IM_COL32(80, 80, 80, 180), Rounding);

    // 퍼센트 텍스트 중앙 정렬
    const ImVec2 TextSize = ImGui::CalcTextSize(Overlay);
    Draw->AddText(
        ImVec2(PosX + (BarW  - TextSize.x) * 0.5f,
               PosY + (BarH - TextSize.y) * 0.5f),
        IM_COL32(255, 255, 255, 255),
        Overlay
    );
}
