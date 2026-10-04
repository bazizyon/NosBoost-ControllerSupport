#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"

namespace ControllerSupport {
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

    void FindRotateBy() {
        const BYTE ROTATE_BY_P[] = {
            0x55,
            0x8B, 0xEC,
            0x51,
            0xD9, 0x40, 0x60,
            0xD8, 0x40, 0x10,
            0x0F, 0xBE, 0x50, 0x1C
        };
        const auto ROTATE_BY_MASK = "xxxxxxxxxxxxxx";
        rotateByFunction = FindPattern(ROTATE_BY_P, ROTATE_BY_MASK, nullptr, 0);
    }

    void FindPetsFollow() {
        const BYTE PETS_P[] = {
            0x53,
            0x56,
            0x57,
            0x83, 0xC4, 0xD8,
            0x89, 0x54, 0x24, 0x04,
            0x89, 0x04, 0x24,
            0x8B, 0x04, 0x24,
            0x8B, 0x40, 0x40,
            0x8B, 0x40, 0x2C
        };
        const auto PETS_MASK = "xxxxxxxxxxxxxxxxxxxxxx";
        petsFollowFunction = FindPattern(PETS_P, PETS_MASK, nullptr, 0);
    }

    void FindStopAction() {
        const BYTE STOP_P[] = {
            0x80, 0x78, 0x14, 0x00,
            0x74, 0x1A,
            0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
            0x8B, 0x12,
            0x33, 0xC9,
            0x89, 0x4A, 0x48
        };
        const auto STOP_MASK = "xxxxxxxx????xxxxxxx";
        stopActionFunction = FindPattern(STOP_P, STOP_MASK, nullptr, 0);
    }

    void FindSelectById() {
        const BYTE SELECT_P[] = {
            0x8B, 0x50, 0x28,
            0x8A, 0x40, 0x2C,
            0xE8, 0x00, 0x00, 0x00, 0x00,
            0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
            0x8B, 0x12,
            0x92,
            0xE8
        };
        const auto SELECT_MASK = "xxxxxxx????xx????xxxx";
        selectByIdFunction = FindPattern(SELECT_P, SELECT_MASK, nullptr, 0);
    }

    void FindWalkTargetGlobal() {
        const BYTE WALK_TARGET_P[] = {
            0x33, 0xD2,
            0xA1, 0x00, 0x00, 0x00, 0x00,
            0xE8, 0x00, 0x00, 0x00, 0x00,
            0xA1, 0x00, 0x00, 0x00, 0x00,
            0xC6, 0x40, 0x30, 0x00,
            0x84, 0xDB
        };
        const auto WALK_TARGET_MASK = "xxx????x????x????xxxxxx";
        const uintptr_t match = FindPattern(WALK_TARGET_P, WALK_TARGET_MASK, nullptr, 0x03);
        walkTargetGlobal = match ? *reinterpret_cast<uintptr_t*>(match) : 0;
    }

    void ClearWalkTarget() {
        const uintptr_t plan = walkTargetGlobal ? *reinterpret_cast<uintptr_t*>(walkTargetGlobal) : 0;
        if (!plan) {
            return;
        }
        *reinterpret_cast<uintptr_t*>(plan + 0x34) = 0;
        *reinterpret_cast<uint8_t*>(plan + 0x30) = 0;
    }

    bool PlayerMayMove(const TLBSWidget* navi) {
        const uintptr_t state = navi ? *reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(navi) + 0x40) : 0;
        return state && *reinterpret_cast<const uint8_t*>(state + 0x38) != 0;
    }

    bool IsClass(const TLBSWidget* widget, const char* name) {
        if (!widget || !widget->vTable) {
            return false;
        }
        const auto* className = *reinterpret_cast<const uint8_t**>(widget->vTable - 0x2C);
        const size_t length = strlen(name);
        return className && className[0] == length && memcmp(className + 1, name, length) == 0;
    }

    TLBSWidget* FindWidgetOfClass(const TLBSWidget* parent, const char* name, const int depth) {
        if (!parent || !parent->childrenList || !parent->childrenList->list || depth <= 0) {
            return nullptr;
        }
        for (uint32_t i = 0; i < parent->childrenList->count; i++) {
            TLBSWidget* child = parent->childrenList->list[i];
            if (IsClass(child, name)) {
                return child;
            }
        }
        for (uint32_t i = 0; i < parent->childrenList->count; i++) {
            if (TLBSWidget* found = FindWidgetOfClass(parent->childrenList->list[i], name, depth - 1)) {
                return found;
            }
        }
        return nullptr;
    }

    TLBSWidget* FindNaviWidget(const TLBSWidget* root) {
        naviWidget = FindWidgetOfClass(root, "TNaviWidget", 3);
        return naviWidget;
    }

    HWND FindGameWindow() {
        HWND window = GetFocus();
        if (!window) {
            HWND foreground = GetForegroundWindow();
            DWORD pid = 0;
            GetWindowThreadProcessId(foreground, &pid);
            window = pid == GetCurrentProcessId() ? foreground : nullptr;
        }
        if (window) {
            GetClassNameA(window, LastKeyWindowClass, sizeof(LastKeyWindowClass));
        } else {
            strcpy_s(LastKeyWindowClass, "none");
        }
        return window;
    }

    void PressKey(const UINT virtualKey) {
        HWND window = FindGameWindow();
        if (!window) {
            return;
        }
        const UINT scanCode = MapVirtualKeyA(virtualKey, MAPVK_VK_TO_VSC);
        PostMessageA(window, WM_KEYDOWN, virtualKey, 1 | scanCode << 16);
        PostMessageA(window, WM_KEYUP, virtualKey, 1 | scanCode << 16 | 0xC0000000);
    }

    void LoadXInput() {
        const HMODULE XInputModule = LoadLibraryA("xinput9_1_0.dll");
        getState = XInputModule ? reinterpret_cast<GetStateFn>(GetProcAddress(XInputModule, "XInputGetState")) : nullptr;
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
        const uintptr_t global = *reinterpret_cast<uintptr_t*>(match);
        const uintptr_t holder = global ? *reinterpret_cast<uintptr_t*>(global) : 0;
        sceneManager = holder ? *reinterpret_cast<TSceneManager**>(holder) : nullptr;
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

    __declspec(noinline) void RotateBy(TLBSRotDamper* damper, float delta) {
        if (!GameCallsAllowed) return;
        if (!rotateByFunction || !damper) {
            return;
        }
        uintptr_t function = rotateByFunction;
        _asm {
            push delta
            mov eax, damper
            call function
        }
    }

    __declspec(noinline) void PetsFollow(TLBSWidget* navi, int x, int y) {
        if (!GameCallsAllowed) return;
        if (!petsFollowFunction || !navi) {
            return;
        }
        uint32_t mapPos = (y & 0xFFFF) << 16 | x & 0xFFFF;
        uintptr_t function = petsFollowFunction;
        _asm {
            mov edx, mapPos
            mov eax, navi
            call function
        }
    }

    __declspec(noinline) void StopAction() {
        if (!GameCallsAllowed) return;
        uintptr_t pointer = GetPlayerObjManagerPtr();
        if (!stopActionFunction || !pointer) {
            return;
        }
        uintptr_t function = stopActionFunction;
        _asm {
            mov eax, pointer
            call function
        }
    }

    struct EntityRef {
        char pad_00[0x28];
        uint32_t id;    // 0x28
        uint8_t type;   // 0x2C
        char pad_2D[3];
    };

    static_assert(offsetof(EntityRef, type) == 0x2C, "EntityRef layout");

    __declspec(noinline) void SelectTarget(const TMapObjBase* entity) {
        if (!GameCallsAllowed) return;
        if (!selectByIdFunction) {
            return;
        }
        EntityRef ref{};
        ref.id = entity ? entity->objectID : 0;
        ref.type = entity ? entity->objectType : 0;
        EntityRef* self = &ref;
        uintptr_t function = selectByIdFunction;
        _asm {
            mov eax, self
            call function
        }
    }

    void MoveTo(int x, int y) {
        if (!GameCallsAllowed) return;
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

    int LoadUiImageResource(const char* name, uint16_t& width, uint16_t& height) {
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&LoadUiImageResource), &self);
        HRSRC resource = FindResourceA(self, name, MAKEINTRESOURCEA(10));
        HGLOBAL loaded = resource ? LoadResource(self, resource) : nullptr;
        const void* data = loaded ? LockResource(loaded) : nullptr;
        if (!data || !CachedHost || !CachedHost->LoadUiImage) {
            return 0;
        }
        return CachedHost->LoadUiImage(data, SizeofResource(self, resource), &width, &height);
    }
}
