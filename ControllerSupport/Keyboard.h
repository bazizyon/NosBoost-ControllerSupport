#pragma once
#include "Game.h"

namespace ControllerSupport::Keyboard {
    bool Update(TLBSWidget* root, const XINPUT_GAMEPAD& pad, bool active);
    bool IsOpen();
    void Toggle();
    void SendChat(const std::wstring& text);
    void Pump(TLBSWidget* root);
    void Destroy();
}
