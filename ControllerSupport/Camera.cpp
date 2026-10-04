#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"

namespace ControllerSupport {
    void ReadStick(const SHORT RawX, const SHORT RawY, const float Deadzone, float& OutX, float& OutY) {
        const float X = RawX;
        const float Y = RawY;
        const float Magnitude = std::sqrt(X * X + Y * Y);
        if (Magnitude <= Deadzone) {
            OutX = OutY = 0.0f;
            return;
        }
        const float Scaled = std::min((Magnitude - Deadzone) / (32767.0f - Deadzone), 1.0f);
        const float Curved = Scaled * Scaled;
        OutX = X / Magnitude * Curved;
        OutY = Y / Magnitude * Curved;
    }

    struct DamperState {
        double startTime;   // 0x08
        float startAngle;   // 0x10
        float currentAngle; // 0x14
        float distance;     // 0x18
        int8_t direction;   // 0x1C
        char pad_1D[3];
        float goal;         // 0x20
        bool hasNewGoal;    // 0x24
    };

    static_assert(offsetof(DamperState, hasNewGoal) == 0x24 - 0x08, "DamperState layout");

    void SetAngle(TLBSRotDamper* damper, float angle) {
        if (!damper) {
            return;
        }
        auto* State = reinterpret_cast<DamperState*>(reinterpret_cast<uintptr_t>(damper) + 0x08);
        const float Lower = damper->min;
        const float Upper = damper->max;
        if (Lower != 0.0f || Upper != 0.0f) {
            angle = std::clamp(angle, Lower, Upper);
        } else {
            angle = std::remainder(angle, 6.28318530718f);
        }
        State->startAngle = angle;
        State->currentAngle = angle;
        State->goal = angle;
        State->distance = 0.0f;
        State->direction = 0;
        State->hasNewGoal = false;
    }

    void RotateDirect(TLBSRotDamper* damper, float delta) {
        if (!damper) {
            return;
        }
        constexpr float Nudge = 0.0001f;
        const float Current = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(damper) + 0x14);
        SetAngle(damper, Current + delta - Nudge);
        RotateBy(damper, Nudge);
    }

    float CameraYaw() {
        TSceneManager* Scene = GetSceneManager();
        if (!Scene || !Scene->camera || !Scene->camera->VerticalRotDamper) {
            return 0.0f;
        }
        // The SDK has the two damper names swapped: VerticalRotDamper is the horizontal one.
        return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(Scene->camera->VerticalRotDamper) + 0x14);
    }

    void UpdateCamera(const float DeltaSeconds, const TLBSWidget* root) {
        float StickX, StickY;
        ReadStick(PadState.Gamepad.sThumbRX, PadState.Gamepad.sThumbRY, RightStickDeadzone, StickX, StickY);
        if (StickX == 0.0f && StickY == 0.0f) {
            return;
        }
        TSceneManager* Scene = GetSceneManager();
        const TLBSWidget* navi = FindNaviWidget(root);
        if (!Scene || !Scene->camera || !navi) {
            return;
        }
        auto* naviBytes = reinterpret_cast<uint8_t*>(const_cast<TLBSWidget*>(navi));
        if (naviBytes[0x56] != 0) {
            return;
        }
        naviBytes[0x28] = 0;
        auto* sceneFields = reinterpret_cast<uint8_t*>(Scene);
        *reinterpret_cast<uint32_t*>(sceneFields + 0x38) = *reinterpret_cast<uint32_t*>(sceneFields + 0x34);
        *reinterpret_cast<uint32_t*>(sceneFields + 0x34) = 0;
        const float Horizontal = StickX * HorizontalCameraSpeed * DeltaSeconds * (InvertHorizontalCamera ? -1.0f : 1.0f);
        const float Vertical = -StickY * VerticalCameraSpeed * DeltaSeconds * (InvertVerticalCamera ? -1.0f : 1.0f);
        const auto Rotate = DirectCamera ? RotateDirect : RotateBy;
        if (Horizontal != 0.0f) Rotate(Scene->camera->VerticalRotDamper, Horizontal);
        if (Vertical != 0.0f) Rotate(Scene->camera->HorizontalRotDamper, Vertical);
    }
}
