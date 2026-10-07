#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"
#include "Navigator.h"
#include "Radial.h"
#include "Clients.h"
#include "Keyboard.h"
#include "imgui_internal.h"

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

    // The gamepad drives ImGui only while one of its windows has the focus, clicking the game takes it back.
    bool ModsMenuOpen = false;
    bool ImGuiFocused = false;
    bool ImGuiNavSet = false;
    bool ImGuiViewWasDown = false;
    bool DropImGuiFocus = false;

    void UpdateImGuiFocus() {
        const ImGuiWindow* mods = ImGui::FindWindowByName("Mods");
        const bool open = mods && mods->WasActive;
        if (open && !ModsMenuOpen) ImGui::SetWindowFocus("Mods");
        ModsMenuOpen = open;
        if (DropImGuiFocus) {
            ImGui::SetWindowFocus(nullptr);
            DropImGuiFocus = false;
        }
        ImGuiFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
        ImGuiIO& io = ImGui::GetIO();
        if (ImGuiFocused && !(io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad)) {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
            ImGuiNavSet = true;
        } else if (!ImGuiFocused && ImGuiNavSet) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
            ImGuiNavSet = false;
        }
    }

    bool GameFocused() {
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        return process == GetCurrentProcessId();
    }

    bool EditModeActive() {
        return Overlay::EditMode;
    }

    WORD PreviousButtons = 0;

    // Like clicking an NPC that's already in reach, nothing happens if none is. Reach is under 4 cells in a straight line.
    constexpr int TalkReachSquared = 13;

    void TalkToNearestNpc() {
        TSceneManager* scene = GetSceneManager();
        if (!scene || !scene->mapPlayerObjPtr) return;
        const TMapPlayerObj* player = scene->mapPlayerObjPtr;
        TMapObjBase* npc = NearestNpc(scene, player);
        if (!npc) return;
        const int dx = npc->xPosition - player->xPosition, dy = npc->yPosition - player->yPosition;
        if (dx * dx + dy * dy > TalkReachSquared) return;
        SelectTarget(npc);
        TalkTo(npc);
    }

    char Commands[2][128]{};
    std::string CommandsFor;

    void LoadCommands() {
        if (Overlay::CharacterName == CommandsFor) return;
        CommandsFor = Overlay::CharacterName;
        const std::string section = "Commands." + CommandsFor, path = Overlay::IniPath();
        for (int i = 0; i < 2; i++) {
            GetPrivateProfileStringA(section.c_str(), ("C" + std::to_string(i + 1)).c_str(), "", Commands[i], sizeof(Commands[i]), path.c_str());
        }
    }

    void SaveCommand(const int i) {
        if (CommandsFor.empty()) return;
        WritePrivateProfileStringA(("Commands." + CommandsFor).c_str(), ("C" + std::to_string(i + 1)).c_str(), Commands[i],
                                   Overlay::IniPath().c_str());
    }

    void SendCommand(const int i) {
        LoadCommands();
        wchar_t text[128]{};
        MultiByteToWideChar(CP_UTF8, 0, Commands[i], -1, text, 127);
        Keyboard::SendChat(text);
    }

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
            case Overlay::ModsMenu: PressKey(VK_F9); break;
            case Overlay::TalkToNpc: TalkToNearestNpc(); break;
            case Overlay::ChatCommand1: SendCommand(0); break;
            case Overlay::ChatCommand2: SendCommand(1); break;
            default: break;
        }
    }

    void UpdateButtons(const TLBSWidget* root) {
        const WORD buttons = PadState.Gamepad.wButtons;
        const WORD pressed = buttons & ~PreviousButtons;
        PreviousButtons = buttons;
        const int layer = Overlay::ActiveLayer(PadState.Gamepad);
        for (int cell = 0; cell < 8; cell++) {
            if (!(pressed & Overlay::CellButtons[cell])) continue;
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

    bool OldMovement = false;
    bool StopOnRelease = true;
    float Lookahead = 7.0f;
    float FineTilt = 0.25f;
    bool WasWalking = false;
    int TargetX = 0, TargetY = 0;
    float WalkDirX = 0.0f, WalkDirY = 0.0f;
    std::chrono::steady_clock::time_point LastWalkSent{};
    float DrawnX = 0.0f, DrawnY = 0.0f, DrawnStepX = 0.0f, DrawnStepY = 0.0f;

    // The game's own click-to-walk steps (the guarded walk does these too), dead players excluded.
    void WalkTo(const TLBSWidget* root, const int x, const int y) {
        TSceneManager* Scene = GetSceneManager();
        if (!Scene || !Scene->mapPlayerObjPtr) return;
        // Dead: the respawn box takes the click, so the game sends nothing, not even pet moves.
        if (reinterpret_cast<const uint8_t*>(Scene->mapPlayerObjPtr)[0xA9] == 4) return;
        TLBSWidget* navi = FindNaviWidget(root);
        ClearWalkTarget();
        if (PlayerMayMove(navi)) {
            StopAction();
            MoveTo(x, y);
        }
        PetsFollow(navi, x, y);
    }

    // Sends a new target only when the direction turns or the old one is nearly reached, and stops where the stick is let go.
    void SmoothWalk(const TLBSWidget* root, const float moveX, const float moveY, const float tilt) {
        TSceneManager* Scene = GetSceneManager();
        if (!Scene || !Scene->mapPlayerObjPtr) return;
        const int x = Scene->mapPlayerObjPtr->xPosition, y = Scene->mapPlayerObjPtr->yPosition;
        const auto now = std::chrono::steady_clock::now();
        // The cell field trails the drawn character. The drawn position (+0x58, +0x60) is the cell centre in half cells.
        const auto* player = reinterpret_cast<const uint8_t*>(Scene->mapPlayerObjPtr);
        const float drawnX = *reinterpret_cast<const float*>(player + 0x58) * 2.0f - 0.5f;
        const float drawnY = *reinterpret_cast<const float*>(player + 0x60) * 2.0f - 0.5f;
        if (std::abs(drawnX - DrawnX) > 0.001f || std::abs(drawnY - DrawnY) > 0.001f) {
            DrawnStepX = drawnX - DrawnX;
            DrawnStepY = drawnY - DrawnY;
        }
        DrawnX = drawnX;
        DrawnY = drawnY;
        if (tilt <= 0.0f) {
            // Stop on the first cell the character is walking into, never back on the one it's leaving.
            if (WasWalking && StopOnRelease) {
                const auto next = [](const float at, const float step) {
                    return static_cast<int>(step > 0.001f ? std::ceil(at) : step < -0.001f ? std::floor(at) : std::round(at));
                };
                const int stopX = next(drawnX, DrawnStepX), stopY = next(drawnY, DrawnStepY);
                WalkTo(root, std::abs(stopX - x) <= 1 ? stopX : x, std::abs(stopY - y) <= 1 ? stopY : y);
            }
            WasWalking = false;
            return;
        }
        const float length = std::sqrt(moveX * moveX + moveY * moveY);
        const float dirX = moveX / length, dirY = -moveY / length;
        const float distance = tilt < FineTilt ? 1.0f : Lookahead;
        const int targetX = x + static_cast<int>(std::round(dirX * distance));
        const int targetY = y + static_cast<int>(std::round(dirY * distance));
        if (targetX == x && targetY == y) return;
        const bool turned = dirX * WalkDirX + dirY * WalkDirY < 0.94f;
        const bool arriving = std::abs(TargetX - x) <= 1 && std::abs(TargetY - y) <= 1;
        if (WasWalking && !turned && !arriving) return;
        if (now - LastWalkSent < std::chrono::milliseconds(100)) return;
        WalkTo(root, targetX, targetY);
        WasWalking = true;
        TargetX = targetX;
        TargetY = targetY;
        WalkDirX = dirX;
        WalkDirY = dirY;
        LastWalkSent = now;
    }
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
        FindTalk();

        FindWalkTargetGlobal();
        Safety::Verify(Host, {{"RotateBy", rotateByFunction}, {"MoveTo", moveFunction},
                              {"pet command", petsFollowFunction}, {"stop action", stopActionFunction},
                              {"select by id", selectByIdFunction}, {"npc talk", talkFunction}, {"cooldown sweep", Overlay::FindSetElapsed()},
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
        Radial::Destroy();
        Keyboard::Destroy();
        Clients::Withdraw();
        Overlay::DestroyHint();
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

        if (GameFocused()) Keyboard::Pump(const_cast<TLBSWidget*>(RootWidget));
        PadState = {};
        PadResult = getState ? getState(0, &PadState) : ERROR_DEVICE_NOT_CONNECTED;
        // Another window or another client has the focus, the gamepad isn't ours then.
        static bool wasFocused = false;
        const bool focused = GameFocused();
        if (!focused) {
            PadState = {};
            PadResult = ERROR_DEVICE_NOT_CONNECTED;
        }
        // Coming from another client the sticks may still be tilted from picking it.
        if (focused && !wasFocused) Radial::StartSettle();
        wasFocused = focused;
        UpdateInputMode(RootWidget);
        const TSceneManager* scene = GetSceneManager();
        const bool inGame = tickContext.isPlayerLoaded && scene && scene->mapPlayerObjPtr;
        if (inGame) {
            Overlay::Update(const_cast<TLBSWidget*>(RootWidget), PadIsActive && !Navigator::Window && !Radial::HidesBars() && !ImGuiFocused && !Keyboard::IsOpen(), PadState.Gamepad,
                            tickContext.mouseX, tickContext.mouseY);
        } else {
            Overlay::HideAll(const_cast<TLBSWidget*>(RootWidget));
        }
        Clients::Publish(inGame ? Overlay::CharacterName : std::string());
        if (PadResult != ERROR_SUCCESS) Radial::Hide(RootWidget);
        if (PadResult != ERROR_SUCCESS) {
            Navigator::Close(false);
            return;
        }
        if (Keyboard::Update(const_cast<TLBSWidget*>(RootWidget), PadState.Gamepad, PadIsActive)) {
            PreviousButtons = PadState.Gamepad.wButtons;
            return;
        }
        // View closes the mods menu, or hands a focused mod window back to the game.
        if (ImGuiFocused && PadIsActive) {
            const bool viewDown = (PadState.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
            if (viewDown && !ImGuiViewWasDown) {
                if (ModsMenuOpen) PressKey(VK_F9);
                DropImGuiFocus = true;
            }
            ImGuiViewWasDown = viewDown;
            PreviousButtons = PadState.Gamepad.wButtons;
            Navigator::Close(false);
            return;
        }
        ImGuiViewWasDown = (PadState.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
        XINPUT_GAMEPAD pad = PadState.Gamepad;
        if (PadIsActive && !Overlay::EditMode && Radial::Update(const_cast<TLBSWidget*>(RootWidget), pad, inGame)) {
            PreviousButtons = PadState.Gamepad.wButtons;
            return;
        }
        Radial::Settle(pad);
        PadState.Gamepad.sThumbLX = pad.sThumbLX;
        PadState.Gamepad.sThumbLY = pad.sThumbLY;
        PadState.Gamepad.sThumbRX = pad.sThumbRX;
        PadState.Gamepad.sThumbRY = pad.sThumbRY;
        // Login, server and character selection have nothing but UI, so UI mode is always on there.
        static bool wasInGame = false;
        if (!inGame) {
            wasInGame = false;
            if (PadIsActive) Navigator::Update(const_cast<TLBSWidget*>(RootWidget), pad, true);
            else Navigator::Close(false);
            return;
        }
        if (!wasInGame) {
            wasInGame = true;
            Navigator::Close(false);
        }
        if (PadIsActive && !Overlay::EditMode && Navigator::Update(const_cast<TLBSWidget*>(RootWidget), pad)) {
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

        if (!OldMovement) {
            float MoveX = static_cast<float>(StickX) / MaxObservedStickValue;
            float MoveY = static_cast<float>(StickY) / MaxObservedStickValue;
            if (CameraRelativeMovement) {
                const float Angle = (CameraYaw() - ForwardYaw) * (FlipMovementRotation ? -1.0f : 1.0f);
                const float RotatedX = MoveX * std::cos(Angle) - MoveY * std::sin(Angle);
                const float RotatedY = MoveX * std::sin(Angle) + MoveY * std::cos(Angle);
                MoveX = RotatedX;
                MoveY = RotatedY;
            }
            // A skill was just cast, a stop here would cancel it.
            if (Now < MovementPausedUntil) {
                WasWalking = false;
                return;
            }
            const bool Held = LeftMagnitude > LeftStickDeadzone;
            const float Tilt = Held ? std::min((LeftMagnitude - LeftStickDeadzone) / (MaxObservedStickValue - LeftStickDeadzone), 1.0f) : 0.0f;
            SmoothWalk(RootWidget, MoveX, MoveY, Tilt);
            return;
        }
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
                WalkTo(RootWidget, Scene->mapPlayerObjPtr->xPosition + DX, Scene->mapPlayerObjPtr->yPosition + DY);
                NextMoveAllowedAt = Now + RollMoveCooldown();
            }
        }
    }

    __declspec(dllexport) void ModTick(const TLBSWidget* RootWidget, const TickContext tickContext) {
        UpdateImGuiFocus();
        if (WindowVisible) {
            ImGui::Begin("Gamepad");
            ImGui::Checkbox("Edit gamepad bars (drag skills onto the slots)", &Overlay::EditMode);
            LoadCommands();
            for (int i = 0; i < 2; i++) {
                if (ImGui::InputText(i ? "Chat command 2" : "Chat command 1", Commands[i], sizeof(Commands[i]))) SaveCommand(i);
            }
            ImGui::End();
        }
    }

    __declspec(dllexport) void ModToggleMainWindow() {
        WindowVisible = !WindowVisible;
    }
}
