#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"

namespace ControllerSupport::Overlay {
    int ActiveLayer(const XINPUT_GAMEPAD& pad) {
        const bool lt = pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        const bool rt = pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        if (lt && rt) return LTRT;
        const bool lb = (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
        const bool rb = (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        if (lt && rb) return LTRB;
        if (lb && rt) return LBRT;
        if (lb && rb) return LBRB;
        if (lt) return LT;
        if (rt) return RT;
        if (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) return LB;
        if (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) return RB;
        return Base;
    }

    void Update(TLBSWidget* root, const bool padActive, const XINPUT_GAMEPAD& pad, const int32_t mouseX, const int32_t mouseY) {
        if (!root || !root->childrenList) return;
        if (root != AttachedRoot || EditMode != AppliedEditMode) {
            DestroyHint();
            LoadImages();
            DestroyPalette();
            if (root == AttachedRoot) {
                for (Panel& panel : Panels) Destroy(panel);
            } else {
                for (Panel& panel : Panels) panel = {};
                for (auto& layer : Bindings) for (Binding& binding : layer) binding = {};
                RequestLoad();
            }
            AttachedRoot = root;
            AppliedEditMode = EditMode;
            for (int layer = 0; layer < LayerCount; layer++) {
                Build(Panels[layer], root, LayerGlyphs[layer], layer, EditMode ? EditLift[layer] : 0,
                      EditMode ? EditShift[layer] : 0, Bindings[layer]);
            }
            if (EditMode && Panels[RB].container) {
                int16_t right = 0;
                for (const Panel& panel : Panels) {
                    if (panel.container) right = std::max(right, panel.container->rect.right);
                }
                BuildPalette(root, right);
                BuildEditBoard(root);
            }
        }
        TrackCharacter(root);
        UpdateLoad(root);
        StepCast();
        LinkRecasts(root);
        ResolveSkills(root);
        ShowMotions(root);
        ShowBarSkills(root);
        SyncCooldowns(root);
        UpdateHint(root, padActive && !EditMode);
        if (EditMode) {
            WatchDrag(root, mouseX, mouseY);
            WatchPaletteDrag(root, mouseX, mouseY);
            WatchRightClick(mouseX, mouseY);
            for (int layer = 0; layer < LayerCount; layer++) Show(Panels[layer], true, Bindings[layer]);
            return;
        }
        int shown = padActive ? ActiveLayer(pad) : -1;
        if (Casting.active) shown = Casting.layer;
        for (int layer = 0; layer < LayerCount; layer++) Show(Panels[layer], layer == shown, Bindings[layer]);
    }

    void HideAll(TLBSWidget* root) {
        if (!AttachedRoot) return;
        UpdateHint(root, false);
        for (Panel& panel : Panels) {
            if (!panel.shown || !panel.container) continue;
            panel.shown = false;
            panel.container->isVisible = false;
        }
    }
}
