#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"

namespace ControllerSupport::Overlay {
    bool Cast(const int layer, const int cell) {
        Panel& panel = Panels[layer];
        SlotView& slot = panel.slots[cell];
        Binding& binding = Bindings[layer][cell];
        if (!binding.set || (binding.action && !(IsBarSkill(binding.action) && slot.barRecord)) || !slot.icon || !panel.shown) return false;
        if (!binding.action && !ResolveSkill(AttachedRoot, binding, slot)) return false;
        if (Casting.active) return true;
        HWND window = FindGameWindow();
        if (!window) return false;
        POINT cursor{};
        GetCursorPos(&cursor);
        ScreenToClient(window, &cursor);
        Casting = {window, MAKELPARAM((slot.rect.left + slot.rect.right) / 2, (slot.rect.top + slot.rect.bottom) / 2),
                   MAKELPARAM(cursor.x, cursor.y), layer, 0, true};
        return true;
    }

    void StepCast() {
        if (!Casting.active) return;
        const bool doubleClickMessages = (GetClassLongA(Casting.window, GCL_STYLE) & CS_DBLCLKS) != 0;
        switch (Casting.step++) {
            case 0:
                PostMessageA(Casting.window, WM_MOUSEMOVE, 0, Casting.at);
                break;
            case 1:
                PostMessageA(Casting.window, WM_LBUTTONDOWN, MK_LBUTTON, Casting.at);
                PostMessageA(Casting.window, WM_LBUTTONUP, 0, Casting.at);
                break;
            case 2:
                PostMessageA(Casting.window, doubleClickMessages ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, MK_LBUTTON, Casting.at);
                PostMessageA(Casting.window, WM_LBUTTONUP, 0, Casting.at);
                break;
            default:
                PostMessageA(Casting.window, WM_MOUSEMOVE, 0, Casting.back);
                Casting.active = false;
                break;
        }
    }
}
