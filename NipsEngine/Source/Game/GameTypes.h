#pragma once

namespace Game
{
    // 게임 전용 액터/컴포넌트 클래스를 FObjectFactory에 등록합니다.
    // UGameEngine::Init에서 씬 역직렬화 전에 호출됩니다.
    void RegisterGameTypes();
}
