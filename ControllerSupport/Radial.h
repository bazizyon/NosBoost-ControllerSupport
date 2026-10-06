#pragma once
#include "Game.h"

namespace ControllerSupport::Radial {
    bool Update(TLBSWidget* root, XINPUT_GAMEPAD& pad, bool inGame);
    bool IsOpen();
    bool HidesBars();
    void Settle(XINPUT_GAMEPAD& pad);
    void StartSettle();
    void Hide(const TLBSWidget* root);
    void Destroy();
}
