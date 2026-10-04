#pragma once
#include "Game.h"

namespace ControllerSupport {
    constexpr float LeftStickDeadzone = 7849.0f;
    constexpr float RightStickDeadzone = 8689.0f;

    inline float HorizontalCameraSpeed = 2.5f;
    inline float VerticalCameraSpeed = 0.8f;
    inline bool InvertHorizontalCamera = false;
    inline bool InvertVerticalCamera = false;

    inline bool CameraRelativeMovement = true;
    inline float ForwardYaw = 0.0f;
    inline bool FlipMovementRotation = true;

    inline bool DirectCamera = true;

    void ReadStick(const SHORT RawX, const SHORT RawY, const float Deadzone, float& OutX, float& OutY);
    void SetAngle(TLBSRotDamper* damper, float angle);
    void RotateDirect(TLBSRotDamper* damper, float delta);
    float CameraYaw();
    void UpdateCamera(const float DeltaSeconds, const TLBSWidget* root);
}
