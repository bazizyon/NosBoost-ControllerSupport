#include "ModContract.h"
#include "TEWLabel.h"
#include <chrono>
#include <Xinput.h>
#include <format>
#include <Psapi.h>
#include "TSceneManager.h"
#include <cmath>
#include <random>
#include <algorithm>

// An example mod that
// receives level packets,
// tracks the time since start-up
// displays an ImGUI window with information.
namespace {
    bool WindowVisible = false;
    const ModHost* CachedHost = nullptr;
    TSceneManager* sceneManager = nullptr;

    // There is no need to handwrite the requirements, they can be linked to the SDK straight.
    constexpr const ModClassRequirement* Requirements = nullptr;

    uintptr_t moveFunction;
    uintptr_t GetModuleBase(const char* moduleName) {
        HMODULE moduleHandle = GetModuleHandleA(moduleName);
        return reinterpret_cast<uintptr_t>(moduleHandle);
    }

    size_t GetModuleSize(const char* moduleName) {
        MODULEINFO info{};
        const HMODULE moduleHandle = GetModuleHandleA(moduleName);
        if (!moduleHandle) return 0;
        K32GetModuleInformation(GetCurrentProcess(),moduleHandle,&info,sizeof(info));
        return static_cast<size_t>(info.SizeOfImage);
    }
    uintptr_t FindPattern(const uint8_t* pattern, const char* mask,const char* moduleName, const ptrdiff_t resultOffset) {
        const uintptr_t moduleBase = GetModuleBase(moduleName);
        const size_t moduleSize = GetModuleSize(moduleName);
        const size_t maskLength = strlen(mask);

        if (!moduleBase || maskLength == 0 || moduleSize < maskLength ) {
            return 0;
        }

        const size_t scanLimit = moduleSize - maskLength;

        for (size_t i = 0; i <= scanLimit; i++) {
            bool found = true;

            for (size_t j = 0; j < maskLength; ++j) {
                const uint8_t currentByte = *reinterpret_cast<uint8_t*>(moduleBase + i + j);
                if (mask[j] != '?' && pattern[j] != currentByte) {
                    found = false;
                    break;
                }
            }
            if (found) {
                return moduleBase + i + resultOffset;
            }
        }
        return 0;
    }
    void Initialize() {
        const BYTE MOVE_FUNCTION_P[] = {
            0x6A, 0x01,
            0x33, 0xC9,
            0x8B, 0x55, 0xFC,
            0xA1, 0x00, 0x00, 0x00, 0x00,
            0xE8, 0x00, 0x00, 0x00, 0x00
        };
        const auto MOVE_FUNCTION_MASK = "xxxxxxxx????x????";
        const uintptr_t moveFuncAddr = FindPattern(MOVE_FUNCTION_P, MOVE_FUNCTION_MASK, nullptr, 0x0C);
        if (!moveFuncAddr) {
            return;
        }
        const INT32 rel = *reinterpret_cast<INT32*>(moveFuncAddr + 1);
        moveFunction = moveFuncAddr + 5 + rel;
    }
    TSceneManager* GetSceneManager() {
        if (sceneManager) {
            return sceneManager;
        }
        uintptr_t match = 0;
        const BYTE SM_PATTERN[] = {
            0xA1,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x58, 0x00, 0x0F, 0x84, 0xBF
        };
        const auto SM_MASK = "x????????xxxxx";
        match = FindPattern(SM_PATTERN, SM_MASK, nullptr, 0x01);
        if (!match) {
            return nullptr;
        }
        sceneManager = ***reinterpret_cast<TSceneManager****>(match);
        return sceneManager;
    }
    uintptr_t GetPlayerObjManagerPtr() {
        uintptr_t match = 0;
        const BYTE POM_PATTERN[] = {
            0x50,
            0x6A, 0x00,
            0x6A, 0x00,
            0x6A, 0x00,
            0x6A, 0x00,
            0x6A, 0x00,
            0x6A, 0x00,
            0x33, 0xD2,
            0x33, 0xC0,
            0xE8, 0x00, 0x00, 0x00, 0x00,
            0x8B, 0xD0,
            0xA1, 0x00, 0x00, 0x00, 0x00
        };
        const auto POM_MASK = "xx?x?x?x?x?x?xxxxx????xxx????";
        match = FindPattern(POM_PATTERN, POM_MASK, nullptr, 0x19);
        if (!match) {
            return 0;
        }

        const uintptr_t p1 = *reinterpret_cast<uintptr_t *>(match);
        if (!p1) {
            return 0;
        }

        const uintptr_t p2 = *reinterpret_cast<uintptr_t *>(p1);
        if (!p2) {
            return 0;
        }

        const uintptr_t p3 = *reinterpret_cast<uintptr_t *>(p2);
        if (!p3) {
            return 0;
        }

        return p3;
    }
    void MoveTo(int x, int y) {
        if (!moveFunction) {
            return;
        }
        uint32_t mapPos = (y & 0xFF) << 16 | x & 0xFF;
        uintptr_t pointer = GetPlayerObjManagerPtr();
        if (!pointer) {
            return;
        }
        _asm {
            push 1
            xor ecx, ecx
            mov edx, mapPos
            mov eax, pointer
            call moveFunction
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

// DLLExport so that runtime can find these exports via GetProcAdress
extern "C" {
    // Writes the size of "Requirements" onto OutCount and returns the array of requirements.
    // This is due to requirements just being returned as a raw pointer and the runtime not knowing how many elements to read.
    __declspec(dllexport) const ModClassRequirement* ModGetRequirements(size_t* OutCount) {
        *OutCount = 0;
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
    }

    __declspec(dllexport) void ModShutdown() {

    }

    __declspec(dllexport) void ModTick(const TLBSWidget* RootWidget, const TickContext tickContext) {
        XINPUT_STATE state{};
        const HMODULE XInputModule = LoadLibraryA("xinput9_1_0.dll");
        using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
        const GetStateFn getState = XInputModule
            ? reinterpret_cast<GetStateFn>(GetProcAddress(XInputModule, "XInputGetState"))
            : nullptr;
        const DWORD result = getState ? getState(0, &state) : ERROR_DEVICE_NOT_CONNECTED;

        if (WindowVisible) {
            ImGui::Begin("Gamepad Debug");
            if (result == ERROR_SUCCESS) {
                ImGui::Text("Connected");

                ImGui::Text("Left Stick");
                ImGui::Text("X: %d", state.Gamepad.sThumbLX);
                ImGui::Text("Y: %d", state.Gamepad.sThumbLY);

                ImGui::Separator();

                ImGui::Text("Right Stick");
                ImGui::Text("X: %d", state.Gamepad.sThumbRX);
                ImGui::Text("Y: %d", state.Gamepad.sThumbRY);

                ImGui::Separator();

                ImGui::Text("Triggers");
                ImGui::Text("LT: %u", state.Gamepad.bLeftTrigger);
                ImGui::Text("RT: %u", state.Gamepad.bRightTrigger);

                ImGui::Separator();

                ImGui::Text("Bumpers");
                ImGui::Text("LB: %s", state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER ? "Pressed" : "Released");
                ImGui::Text("RB: %s", state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER ? "Pressed" : "Released");

                ImGui::Separator();

                ImGui::Text("Buttons");

                ImGui::Text("A: %s",
                    (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) ? "Pressed" : "Released");

                ImGui::Text("B: %s",
                    (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) ? "Pressed" : "Released");

                ImGui::Text("X: %s",
                    (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) ? "Pressed" : "Released");

                ImGui::Text("Y: %s",
                    (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) ? "Pressed" : "Released");
            } else {
                ImGui::Text("No controller connected");
            }
            ImGui::End();
        }
        if (result == ERROR_SUCCESS && tickContext.isPlayerLoaded) {
            const SHORT StickX = state.Gamepad.sThumbLX;
            const SHORT StickY = state.Gamepad.sThumbLY;
            MaxObservedStickValue = std::max({MaxObservedStickValue, std::abs(static_cast<int>(StickX)), std::abs(static_cast<int>(StickY))});

            const auto Now = std::chrono::steady_clock::now();
            if ((StickX != 0 || StickY != 0) && Now >= NextMoveAllowedAt) {
                const int DX = static_cast<int>(std::round((static_cast<float>(StickX) / MaxObservedStickValue) * MaxMoveDistance));
                const int DY = static_cast<int>(std::round((-static_cast<float>(StickY) / MaxObservedStickValue) * MaxMoveDistance));
                if (DX != 0 || DY != 0) {
                    const auto& player = GetSceneManager()->mapPlayerObjPtr;
                    const int CurrentX = player->xPosition;
                    const int CurrentY = player->yPosition;
                    MoveTo(CurrentX + DX, CurrentY + DY);
                    NextMoveAllowedAt = Now + RollMoveCooldown();
                }
            }
        }
    }

    __declspec(dllexport) void ModToggleMainWindow() {
        WindowVisible = !WindowVisible;
    }
}
