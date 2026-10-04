#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"
#include "Navigator.h"

namespace ControllerSupport {
    bool WindowVisible = false;

    constexpr ModClassRequirement Requirements[] = {
        {TLBSWidget::ClassName, TLBSWidget::Version, TLBSWidget::ExpectedSize},
        {TEWControlWidget::ClassName, TEWControlWidget::Version, TEWControlWidget::ExpectedSize},
        {TEWLabel::ClassName, TEWLabel::Version, TEWLabel::ExpectedSize},
        {TNTIconWidget::ClassName, TNTIconWidget::Version, TNTIconWidget::ExpectedSize},
        {TNTTimeAniIcon::ClassName, TNTTimeAniIcon::Version, TNTTimeAniIcon::ExpectedSize},
        {TEWGraphicButtonWidget::ClassName, TEWGraphicButtonWidget::Version, TEWGraphicButtonWidget::ExpectedSize},
        {TEWCustomPanelWidget::ClassName, TEWCustomPanelWidget::Version, TEWCustomPanelWidget::ExpectedSize},
    };

    bool GameFocused() {
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        return process == GetCurrentProcessId();
    }

    bool EditModeActive() {
        return Overlay::EditMode;
    }

    WORD PreviousButtons = 0;

    void RunAction(const uint8_t action, const TLBSWidget* root) {
        switch (action) {
            case Overlay::Attack: PressKey(VK_SPACE); PauseMovement(); break;
            case Overlay::ClearTarget: SelectTarget(nullptr); break;
            case Overlay::NextTarget: TargetNext(); break;
            case Overlay::PrevTarget: TargetPrevious(); break;
            case Overlay::BossTarget: TargetNextMarked(root); break;
            case Overlay::PickUp: PressKey('X'); PauseMovement(); break;
            case Overlay::Sit: PressKey('C'); PauseMovement(); break;
            case Overlay::Specialist: PressKey('G'); PauseMovement(); break;
            case Overlay::PartnerSpecialist: PressKey('H'); PauseMovement(); break;
            case Overlay::PetsFollow: PressKey('D'); PauseMovement(); break;
            case Overlay::PetsStop: PressKey('S'); PauseMovement(); break;
            case Overlay::Chat: PressKey(VK_RETURN); break;
            default: break;
        }
    }

    void UpdateButtons(const TLBSWidget* root) {
        const WORD buttons = PadState.Gamepad.wButtons;
        const WORD pressed = buttons & ~PreviousButtons;
        PreviousButtons = buttons;
        constexpr WORD CellButtons[8] = {XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT,
                                         XINPUT_GAMEPAD_DPAD_RIGHT, XINPUT_GAMEPAD_Y, XINPUT_GAMEPAD_A,
                                         XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_B};
        const int layer = Overlay::ActiveLayer(PadState.Gamepad);
        for (int cell = 0; cell < 8; cell++) {
            if (!(pressed & CellButtons[cell])) continue;
            const Overlay::Binding& binding = Overlay::Bindings[layer][cell];
            if (!binding.set) continue;
            if (!binding.action || Overlay::IsBarSkill(binding.action)) {
                if (Overlay::Cast(layer, cell)) PauseMovement();
                continue;
            }
            RunAction(binding.action, root);
        }
    }

    int MaxObservedStickValue = 26000;
    float MaxMoveDistance = 7.0f;
    std::chrono::steady_clock::time_point NextMoveAllowedAt{};
    std::mt19937 MoveRng{std::random_device{}()};

    std::chrono::milliseconds RollMoveCooldown() {
        std::uniform_int_distribution<int> Jitter(0, 60);
        return std::chrono::milliseconds(100 + Jitter(MoveRng));
    }
}

using namespace ControllerSupport;

// DLLExport so that runtime can find these exports via GetProcAdress
extern "C" {
    // Writes the size of "Requirements" onto OutCount and returns the array of requirements.
    // This is due to requirements just being returned as a raw pointer and the runtime not knowing how many elements to read.
    __declspec(dllexport) const ModClassRequirement* ModGetRequirements(size_t* OutCount) {
        *OutCount = std::size(Requirements);
        return Requirements;
    }

    __declspec(dllexport) void ModStartup(
        ImGuiContext* Context, const ImGuiMemAllocFunc AllocFunc, const ImGuiMemFreeFunc FreeFunc,
        void* AllocUserData, const ModHost* Host
    ) {
        ImGui::SetCurrentContext(Context);
        ImGui::SetAllocatorFunctions(AllocFunc, FreeFunc, AllocUserData);
        CachedHost = Host;
        Initialize();
        FindRotateBy();
        FindPetsFollow();
        FindStopAction();
        FindSelectById();

        FindWalkTargetGlobal();
        Safety::Verify(Host, {{"RotateBy", rotateByFunction}, {"MoveTo", moveFunction},
                              {"pet command", petsFollowFunction}, {"stop action", stopActionFunction},
                              {"select by id", selectByIdFunction}, {"cooldown sweep", Overlay::FindSetElapsed()},
                              {"cooldown length", Overlay::FindSetLength()}, {"cooldown running", Overlay::FindSetRunning()}});
        LoadXInput();
    }

    __declspec(dllexport) void ModShutdown() {
        RestoreBars();
        for (Overlay::Panel& panel : Overlay::Panels) Overlay::Destroy(panel);
        Overlay::DestroyPalette();
        if (Overlay::DragLabel) {
            Overlay::Detach(Overlay::DragLabel);
            Overlay::DragLabel = nullptr;
        }
        Overlay::AttachedRoot = nullptr;
        Navigator::Destroy();
    }

    __declspec(dllexport) void ModEarlyTick(const TLBSWidget* RootWidget, const TickContext tickContext) {
        const auto Now = std::chrono::steady_clock::now();
        const float DeltaSeconds = LastEarlyTick.time_since_epoch().count() == 0
            ? 0.0f
            : std::min(std::chrono::duration<float>(Now - LastEarlyTick).count(), 0.1f);
        LastEarlyTick = Now;

        if (PendingKey) {
            PressKey(PendingKey);
            PendingKey = 0;
        }

        PadState = {};
        PadResult = getState ? getState(0, &PadState) : ERROR_DEVICE_NOT_CONNECTED;
        // Another window or another client has the focus, the gamepad isn't ours then.
        if (!GameFocused()) {
            PadState = {};
            PadResult = ERROR_DEVICE_NOT_CONNECTED;
        }
        UpdateInputMode(RootWidget);
        if (tickContext.isPlayerLoaded) {
            Overlay::Update(const_cast<TLBSWidget*>(RootWidget), PadIsActive && !Navigator::Window, PadState.Gamepad,
                            tickContext.mouseX, tickContext.mouseY);
        }
        if (PadResult != ERROR_SUCCESS || !tickContext.isPlayerLoaded) {
            Navigator::Close(false);
            return;
        }
        if (PadIsActive && !Overlay::EditMode && Navigator::Update(const_cast<TLBSWidget*>(RootWidget), PadState.Gamepad)) {
            PreviousButtons = PadState.Gamepad.wButtons;
            return;
        }
        if (!PadIsActive || Overlay::EditMode) Navigator::Close(false);
        UpdateCamera(DeltaSeconds, RootWidget);
        UpdateButtons(RootWidget);

        const SHORT StickX = PadState.Gamepad.sThumbLX;
        const SHORT StickY = PadState.Gamepad.sThumbLY;
        const float LeftMagnitude = std::sqrt(static_cast<float>(StickX) * StickX + static_cast<float>(StickY) * StickY);
        MaxObservedStickValue = std::max({MaxObservedStickValue, std::abs(static_cast<int>(StickX)), std::abs(static_cast<int>(StickY))});

        if (LeftMagnitude > LeftStickDeadzone && Now >= NextMoveAllowedAt && Now >= MovementPausedUntil) {
            float MoveX = static_cast<float>(StickX) / MaxObservedStickValue;
            float MoveY = static_cast<float>(StickY) / MaxObservedStickValue;
            if (CameraRelativeMovement) {
                const float Angle = (CameraYaw() - ForwardYaw) * (FlipMovementRotation ? -1.0f : 1.0f);
                const float Cos = std::cos(Angle);
                const float Sin = std::sin(Angle);
                const float RotatedX = MoveX * Cos - MoveY * Sin;
                const float RotatedY = MoveX * Sin + MoveY * Cos;
                MoveX = RotatedX;
                MoveY = RotatedY;
            }
            const int DX = static_cast<int>(std::round(MoveX * MaxMoveDistance));
            const int DY = static_cast<int>(std::round(-MoveY * MaxMoveDistance));
            if (DX != 0 || DY != 0) {
                TSceneManager* Scene = GetSceneManager();
                if (!Scene || !Scene->mapPlayerObjPtr) {
                    return;
                }
                const auto& player = Scene->mapPlayerObjPtr;
                // Dead: the respawn box takes the click, so the game sends nothing, not even pet moves.
                if (reinterpret_cast<const uint8_t*>(player)[0xA9] == 4) {
                    return;
                }
                const int CurrentX = player->xPosition;
                const int CurrentY = player->yPosition;
                TLBSWidget* navi = FindNaviWidget(RootWidget);
                ClearWalkTarget();
                if (PlayerMayMove(navi)) {
                    StopAction();
                    MoveTo(CurrentX + DX, CurrentY + DY);
                }
                PetsFollow(navi, CurrentX + DX, CurrentY + DY);
                NextMoveAllowedAt = Now + RollMoveCooldown();
            }
        }
    }

    __declspec(dllexport) void ModTick(const TLBSWidget* RootWidget, const TickContext tickContext) {
        if (WindowVisible) {
            ImGui::Begin("Gamepad Debug");
            ImGui::Text("RotateBy: %s", rotateByFunction ? "found" : "NOT FOUND");
            ImGui::Text("Input: %s, walk target global: %s", PadIsActive ? "gamepad" : "keyboard/mouse",
                        walkTargetGlobal ? "found" : "NOT FOUND");
            ImGui::Text("Stop action: %s", stopActionFunction ? "found" : "NOT FOUND");
            ImGui::Text("Select by id: %s, cycle %u/%u", selectByIdFunction ? "found" : "NOT FOUND",
                        static_cast<unsigned>(TargetHistory.empty() ? 0 : HistoryIndex + 1),
                        static_cast<unsigned>(TargetHistory.size()));
            ImGui::Text("Pets follow: %s, TNaviWidget: %s", petsFollowFunction ? "found" : "NOT FOUND", naviWidget ? "found" : "not found");
            ImGui::SliderFloat("Camera speed", &HorizontalCameraSpeed, 0.5f, 6.0f);
            ImGui::SliderFloat("Vertical speed", &VerticalCameraSpeed, 0.2f, 3.0f);
            ImGui::Checkbox("Invert horizontal", &InvertHorizontalCamera);
            ImGui::Checkbox("Invert vertical", &InvertVerticalCamera);
            ImGui::Checkbox("Direct camera", &DirectCamera);
            ImGui::Checkbox("Edit gamepad bars (drag skills onto the slots)", &Overlay::EditMode);
            ImGui::SliderFloat("Stick pause after action (s)", &ActionMovementPause, 0.0f, 1.0f);
            if (ImGui::Button("Press Space (test)")) {
                PendingKey = VK_SPACE;
            }
            ImGui::SameLine();
            ImGui::Text("last sent to: %s", LastKeyWindowClass);
            ImGui::Separator();
            ImGui::Text("Camera yaw: %.3f  forward yaw: %.3f", CameraYaw(), ForwardYaw);
            ImGui::Checkbox("Camera-relative movement", &CameraRelativeMovement);
            if (ImGui::Button("Use current camera as forward")) {
                ForwardYaw = CameraYaw();
            }
            ImGui::Checkbox("Flip movement rotation", &FlipMovementRotation);
            ImGui::Separator();
            if (PadResult == ERROR_SUCCESS) {
                ImGui::Text("Connected");

                ImGui::Text("Left Stick");
                ImGui::Text("X: %d", PadState.Gamepad.sThumbLX);
                ImGui::Text("Y: %d", PadState.Gamepad.sThumbLY);

                ImGui::Separator();

                ImGui::Text("Right Stick");
                ImGui::Text("X: %d", PadState.Gamepad.sThumbRX);
                ImGui::Text("Y: %d", PadState.Gamepad.sThumbRY);

                ImGui::Separator();

                ImGui::Text("Triggers");
                ImGui::Text("LT: %u", PadState.Gamepad.bLeftTrigger);
                ImGui::Text("RT: %u", PadState.Gamepad.bRightTrigger);

                ImGui::Separator();

                ImGui::Text("Bumpers");
                ImGui::Text("LB: %s", PadState.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER ? "Pressed" : "Released");
                ImGui::Text("RB: %s", PadState.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER ? "Pressed" : "Released");

                ImGui::Separator();

                ImGui::Text("Buttons");

                ImGui::Text("A: %s",
                    (PadState.Gamepad.wButtons & XINPUT_GAMEPAD_A) ? "Pressed" : "Released");

                ImGui::Text("B: %s",
                    (PadState.Gamepad.wButtons & XINPUT_GAMEPAD_B) ? "Pressed" : "Released");

                ImGui::Text("X: %s",
                    (PadState.Gamepad.wButtons & XINPUT_GAMEPAD_X) ? "Pressed" : "Released");

                ImGui::Text("Y: %s",
                    (PadState.Gamepad.wButtons & XINPUT_GAMEPAD_Y) ? "Pressed" : "Released");
            } else {
                ImGui::Text("No controller connected");
            }
            ImGui::End();
        }
    }

    __declspec(dllexport) void ModToggleMainWindow() {
        WindowVisible = !WindowVisible;
    }
}
