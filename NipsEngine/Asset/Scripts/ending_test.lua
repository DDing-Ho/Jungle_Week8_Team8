-- ending_test.lua
-- 테스트용: F5 키를 누르면 엔딩 대화 시퀀스를 트리거한다.
-- 액터에 붙여두고 Play 모드에서 F5를 눌러 확인.

local KEY_F5 = 0x74
local triggered = false

function BeginPlay(owner)
    Log("[EndingTest] 준비 완료 - F5 키로 엔딩 대화를 테스트합니다.")
end

function Tick(owner, deltaTime)
    if triggered then return end

    if GetKeyDown(KEY_F5) then
        triggered = true
        StartCoroutine(TriggerEnding)
    end
end

function TriggerEnding()
    Log("[EndingTest] 엔딩 시퀀스 시작")

    -- Ending 상태로 전환 (어두운 배경 표시)
    SetUIState("Ending")

    -- 대화 큐 등록
    QueueDialogue("내레이터", "오랜 싸움이 끝났다.")
    QueueDialogue("내레이터", "폐허가 된 도시 위로, 새벽빛이 스며들었다.")
    QueueDialogue("주인공",   "...이제 쉴 수 있는 건가.")
    QueueDialogue("내레이터", "그리고 세계는 다시, 천천히 숨을 쉬기 시작했다.")

    -- 모든 대화가 끝날 때까지 대기
    while IsDialogueActive() do
        wait(0.1)
    end

    Log("[EndingTest] 대화 종료 - THE END 표시 중")
end
