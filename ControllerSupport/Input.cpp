#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"

namespace ControllerSupport {
    void PauseMovement() {
        MovementPausedUntil = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(static_cast<int>(ActionMovementPause * 1000.0f));
    }

    bool KeyboardOrMouseUsed() {
        POINT cursor{};
        bool used = false;
        if (GetCursorPos(&cursor)) {
            used = cursor.x != LastCursor.x || cursor.y != LastCursor.y;
            LastCursor = cursor;
        }
        for (int key = VK_LBUTTON; key <= VK_OEM_CLEAR && !used; key++) {
            if (key == VK_CANCEL) {
                continue;
            }
            used = (GetAsyncKeyState(key) & 0x8000) != 0;
        }
        return used;
    }

    void RestoreBars() {
        for (const MovedBar& bar : MovedBars) {
            const int16_t width = bar.widget->rect.right - bar.widget->rect.left;
            const int16_t height = bar.widget->rect.bottom - bar.widget->rect.top;
            bar.widget->rect = {bar.x, bar.y, static_cast<int16_t>(bar.x + width), static_cast<int16_t>(bar.y + height)};
        }
        MovedBars.clear();
    }

    void UpdateBars(const TLBSWidget* root) {
        if (!PadIsActive && !EditModeActive()) {
            RestoreBars();
            return;
        }
        std::erase_if(MovedBars, [root](const MovedBar& bar) {
            for (const char* name : HiddenBarClasses) {
                if (FindWidgetOfClass(root, name, 2) == bar.widget) {
                    return false;
                }
            }
            return true;
        });
        for (const char* name : HiddenBarClasses) {
            TLBSWidget* bar = FindWidgetOfClass(root, name, 2);
            if (!bar || std::ranges::any_of(MovedBars, [bar](const MovedBar& moved) { return moved.widget == bar; })) {
                continue;
            }
            MovedBars.push_back({bar, bar->rect.left, bar->rect.top});
            const int16_t width = bar->rect.right - bar->rect.left;
            const int16_t height = bar->rect.bottom - bar->rect.top;
            bar->rect = {OffScreen, OffScreen, static_cast<int16_t>(OffScreen + width), static_cast<int16_t>(OffScreen + height)};
        }
    }

    void UpdateInputMode(const TLBSWidget* root) {
        if (PadResult != ERROR_SUCCESS) {
            LastPadInputTick = 0;
        }
        const XINPUT_GAMEPAD& pad = PadState.Gamepad;
        const auto pastDeadzone = [](const SHORT x, const SHORT y, const float deadzone) {
            return std::sqrt(static_cast<float>(x) * x + static_cast<float>(y) * y) > deadzone;
        };
        if (pad.wButtons || pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD
            || pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD
            || pastDeadzone(pad.sThumbLX, pad.sThumbLY, LeftStickDeadzone)
            || pastDeadzone(pad.sThumbRX, pad.sThumbRY, RightStickDeadzone)) {
            LastPadInputTick = GetTickCount();
        }
        if (KeyboardOrMouseUsed()) {
            LastKeyboardMouseTick = GetTickCount();
        }
        PadIsActive = LastPadInputTick && static_cast<int>(LastPadInputTick - LastKeyboardMouseTick) >= 0;

        UpdateBars(root);
    }
}
