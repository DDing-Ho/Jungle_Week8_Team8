#include "Game/UI/DialoguePanel.h"

#include "Engine/Input/InputRouter.h"
#include "Engine/Viewport/ViewportRect.h"
#include "ImGui/imgui.h"

// -------------------------------------------------------
// Static 멤버 정의
// -------------------------------------------------------
std::queue<FDialogueEntry> DialoguePanel::PendingQueue;
std::string                DialoguePanel::CurrentSpeaker;
std::string                DialoguePanel::CurrentFullText;
int                        DialoguePanel::VisibleChars  = 0;
float                      DialoguePanel::TypeTimer     = 0.f;
bool                       DialoguePanel::bActive       = false;

// -------------------------------------------------------
// UTF-8 헬퍼 - 한글(3바이트)을 한 글자로 카운트
// -------------------------------------------------------
int DialoguePanel::TotalCharCount(const std::string& Str)
{
	int i = 0, Count = 0;
	while (i < static_cast<int>(Str.size()))
	{
		const unsigned char C = static_cast<unsigned char>(Str[i]);
		if      (C < 0x80) i += 1;
		else if (C < 0xE0) i += 2;
		else if (C < 0xF0) i += 3;
		else               i += 4;
		++Count;
	}
	return Count;
}

int DialoguePanel::CharCountToByteOffset(const std::string& Str, int CharCount)
{
	int i = 0, Chars = 0;
	while (i < static_cast<int>(Str.size()) && Chars < CharCount)
	{
		const unsigned char C = static_cast<unsigned char>(Str[i]);
		if      (C < 0x80) i += 1;
		else if (C < 0xE0) i += 2;
		else if (C < 0xF0) i += 3;
		else               i += 4;
		++Chars;
	}
	return i;
}

// -------------------------------------------------------
// 내부 헬퍼
// -------------------------------------------------------
void DialoguePanel::StartEntry(const FDialogueEntry& Entry)
{
	CurrentSpeaker  = Entry.Speaker;
	CurrentFullText = Entry.Text;
	VisibleChars    = 0;
	TypeTimer       = 0.f;
	bActive         = true;
}

void DialoguePanel::AdvanceQueue()
{
	if (!PendingQueue.empty())
	{
		FDialogueEntry Next = PendingQueue.front();
		PendingQueue.pop();
		StartEntry(Next);
	}
	else
	{
		bActive = false;
	}
}

// -------------------------------------------------------
// Public API
// -------------------------------------------------------
void DialoguePanel::Show(const char* Speaker, const char* Text)
{
	while (!PendingQueue.empty()) PendingQueue.pop();
	StartEntry({ Speaker ? Speaker : "", Text ? Text : "" });
}

void DialoguePanel::Enqueue(const char* Speaker, const char* Text)
{
	FDialogueEntry Entry{ Speaker ? Speaker : "", Text ? Text : "" };
	if (!bActive)
		StartEntry(Entry);
	else
		PendingQueue.push(Entry);
}

void DialoguePanel::Hide()
{
	while (!PendingQueue.empty()) PendingQueue.pop();
	bActive = false;
}

bool DialoguePanel::IsActive()
{
	return bActive;
}

// -------------------------------------------------------
// Render
// -------------------------------------------------------
void DialoguePanel::Render(EUIRenderMode Mode)
{
	if (!bActive) return;

	// --- 타이핑 효과 업데이트 ---
	const float DeltaTime  = ImGui::GetIO().DeltaTime;
	const int   TotalChars = TotalCharCount(CurrentFullText);

	if (VisibleChars < TotalChars)
	{
		TypeTimer += DeltaTime;
		while (TypeTimer >= CharInterval && VisibleChars < TotalChars)
		{
			TypeTimer -= CharInterval;
			++VisibleChars;
		}
	}

	// --- SPACE 키 처리 (Play 모드에서만) ---
	if (Mode == EUIRenderMode::Play && FInputRouter::GetKeyUp(0x20))  // VK_SPACE
	{
		if (VisibleChars < TotalChars)
		{
			// 타이핑 스킵 - 전체 즉시 표시
			VisibleChars = TotalChars;
			TypeTimer    = 0.f;
		}
		else
		{
			AdvanceQueue();
			return;
		}
	}

	// --- 레이아웃 계산 ---
	ImGuiIO& IO = ImGui::GetIO();
	const FViewportRect& VR   = FInputRouter::GetGuiInputState().ViewportHostRect;
	const bool           bHasVR = (VR.Width > 0);

	const float VX = bHasVR ? static_cast<float>(VR.X)      : 0.f;
	const float VY = bHasVR ? static_cast<float>(VR.Y)      : 0.f;
	const float VW = bHasVR ? static_cast<float>(VR.Width)  : IO.DisplaySize.x;
	const float VH = bHasVR ? static_cast<float>(VR.Height) : IO.DisplaySize.y;

	const float PanelW   = VW * 0.8f;
	const float PanelH   = 110.f;
	const float PanelX   = VX + (VW - PanelW) * 0.5f;
	const float PanelY   = VY + VH - PanelH - 20.f;
	const float Padding  = 14.f;
	const float Rounding = 8.f;

	ImDrawList* Draw = ImGui::GetForegroundDrawList();

	// 배경
	Draw->AddRectFilled(
		ImVec2(PanelX,          PanelY),
		ImVec2(PanelX + PanelW, PanelY + PanelH),
		IM_COL32(15, 15, 20, 220), Rounding
	);
	// 테두리
	Draw->AddRect(
		ImVec2(PanelX,          PanelY),
		ImVec2(PanelX + PanelW, PanelY + PanelH),
		IM_COL32(100, 100, 120, 180), Rounding
	);

	// 화자 이름 (황금색)
	if (!CurrentSpeaker.empty())
	{
		Draw->AddText(
			ImVec2(PanelX + Padding, PanelY + Padding),
			IM_COL32(255, 210, 100, 255),
			CurrentSpeaker.c_str()
		);
	}

	// 대사 텍스트 (타이핑 효과 + 줄바꿈)
	const int         ByteOffset = CharCountToByteOffset(CurrentFullText, VisibleChars);
	const char* const TextBegin  = CurrentFullText.c_str();
	const char* const TextEnd    = TextBegin + ByteOffset;
	const float       WrapWidth  = PanelW - Padding * 2.f;

	Draw->AddText(
		nullptr, 0.f,
		ImVec2(PanelX + Padding, PanelY + Padding + 24.f),
		IM_COL32(230, 230, 230, 255),
		TextBegin, TextEnd, WrapWidth
	);

	// 진행 힌트 (타이핑 완료 시에만 표시)
	if (VisibleChars >= TotalChars)
	{
		const char*    Hint     = "[SPACE] >";
		const ImVec2   HintSize = ImGui::CalcTextSize(Hint);
		Draw->AddText(
			ImVec2(PanelX + PanelW - HintSize.x - Padding,
				   PanelY + PanelH - HintSize.y - Padding),
			IM_COL32(160, 160, 160, 200),
			Hint
		);
	}
}
