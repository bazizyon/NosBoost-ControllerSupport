#pragma once
#include "Game.h"

namespace ControllerSupport {
    inline UINT PendingKey = 0;

    inline XINPUT_STATE PadState{};
    inline DWORD PadResult = ERROR_DEVICE_NOT_CONNECTED;
    inline std::chrono::steady_clock::time_point LastEarlyTick{};

    inline float ActionMovementPause = 0.2f;
    inline std::chrono::steady_clock::time_point MovementPausedUntil{};

    void PauseMovement();

    inline bool PadIsActive = false;
    inline DWORD LastPadInputTick = 0;
    inline DWORD LastKeyboardMouseTick = 0;
    inline POINT LastCursor{};

    bool KeyboardOrMouseUsed();

    constexpr const char* HiddenBarClasses[] = {"TNTQuickSlotWidget", "TNTPartnerSlotWidget", "TNTPetSKillSlotWidget"};
    constexpr int16_t OffScreen = -20000;

    struct MovedBar {
        TLBSWidget* widget;
        int16_t x;
        int16_t y;
    };
    inline std::vector<MovedBar> MovedBars;

    void RestoreBars();
    void UpdateBars(const TLBSWidget* root);
    void UpdateInputMode(const TLBSWidget* root);

    bool EditModeActive();
}
