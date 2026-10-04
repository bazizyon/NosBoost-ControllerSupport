#include "ModContract.h"
#include "TEWLabel.h"
#include "TEWControlWidget.h"
#include "TEWGraphicButtonWidget.h"
#include "TEWCustomPanelWidget.h"
#include "WidgetKit.h"
#include "TNTIconWidget.h"
#include "TNTTimeAniIcon.h"
#include <string>
#include <chrono>
#include <Xinput.h>
#include <format>
#include <Psapi.h>
#include "TSceneManager.h"
#include "TMapObjBase.h"
#include <cmath>
#include <random>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace {
    bool GameCallsAllowed = true;
    bool WindowVisible = false;
    const ModHost* CachedHost = nullptr;
    TSceneManager* sceneManager = nullptr;

    constexpr ModClassRequirement Requirements[] = {
        {TLBSWidget::ClassName, TLBSWidget::Version, TLBSWidget::ExpectedSize},
        {TEWControlWidget::ClassName, TEWControlWidget::Version, TEWControlWidget::ExpectedSize},
        {TEWLabel::ClassName, TEWLabel::Version, TEWLabel::ExpectedSize},
        {TNTIconWidget::ClassName, TNTIconWidget::Version, TNTIconWidget::ExpectedSize},
        {TNTTimeAniIcon::ClassName, TNTTimeAniIcon::Version, TNTTimeAniIcon::ExpectedSize},
        {TEWGraphicButtonWidget::ClassName, TEWGraphicButtonWidget::Version, TEWGraphicButtonWidget::ExpectedSize},
        {TEWCustomPanelWidget::ClassName, TEWCustomPanelWidget::Version, TEWCustomPanelWidget::ExpectedSize},
    };

    uintptr_t moveFunction;
    uintptr_t rotateByFunction;
    uintptr_t petsFollowFunction;
    uintptr_t stopActionFunction;
    uintptr_t selectByIdFunction;
    uintptr_t walkTargetGlobal;
    TLBSWidget* naviWidget = nullptr;

    using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    GetStateFn getState = nullptr;
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

    char LastKeyWindowClass[64] = "";

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

    constexpr float LeftStickDeadzone = 7849.0f;
    constexpr float RightStickDeadzone = 8689.0f;

    float HorizontalCameraSpeed = 2.5f;
    float VerticalCameraSpeed = 0.8f;
    bool InvertHorizontalCamera = false;
    bool InvertVerticalCamera = false;

    bool CameraRelativeMovement = true;
    float ForwardYaw = 0.0f;
    bool FlipMovementRotation = true;

    bool DirectCamera = true;

    UINT PendingKey = 0;

    XINPUT_STATE PadState{};
    DWORD PadResult = ERROR_DEVICE_NOT_CONNECTED;
    std::chrono::steady_clock::time_point LastEarlyTick{};

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

    struct MapObjList {
        uintptr_t vTable;
        TMapObjBase** items;
        uint32_t count;
    };

    MapObjList* MonsterList(TSceneManager* scene) {
        return *reinterpret_cast<MapObjList**>(reinterpret_cast<uintptr_t>(scene) + 0x10);
    }

    bool IsTargetable(const TMapObjBase* entity) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(entity);
        return entity && bytes[0x33] != 0 && bytes[0xA9] != 4;
    }

    constexpr float TargetRange = 22.0f;

    std::vector<uint32_t> TargetHistory;
    size_t HistoryIndex = 0;

    TMapObjBase* FindMonster(TSceneManager* scene, const uint32_t id) {
        MapObjList* list = MonsterList(scene);
        if (!list || !list->items) {
            return nullptr;
        }
        for (uint32_t i = 0; i < list->count; i++) {
            TMapObjBase* entity = list->items[i];
            if (entity && entity->objectID == id) {
                return IsTargetable(entity) ? entity : nullptr;
            }
        }
        return nullptr;
    }

    void PruneHistory(TSceneManager* scene) {
        for (size_t i = TargetHistory.size(); i-- > 0;) {
            if (FindMonster(scene, TargetHistory[i])) {
                continue;
            }
            TargetHistory.erase(TargetHistory.begin() + i);
            if (HistoryIndex >= i && HistoryIndex > 0) {
                HistoryIndex--;
            }
        }
    }

    TMapObjBase* NearestNewMonster(TSceneManager* scene, const TMapPlayerObj* player) {
        MapObjList* list = MonsterList(scene);
        if (!list || !list->items) {
            return nullptr;
        }
        TMapObjBase* best = nullptr;
        float bestDistance = TargetRange;
        for (uint32_t i = 0; i < list->count; i++) {
            TMapObjBase* entity = list->items[i];
            if (!IsTargetable(entity)
                || std::ranges::find(TargetHistory, entity->objectID) != TargetHistory.end()) {
                continue;
            }
            const float dx = static_cast<float>(entity->xPosition) - player->xPosition;
            const float dy = static_cast<float>(entity->yPosition) - player->yPosition;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= bestDistance) {
                best = entity;
                bestDistance = distance;
            }
        }
        return best;
    }

    struct MarkWidget : TLBSWidget {
        uintptr_t button;   // 0x24
        uint32_t entityId;  // 0x28
        uint8_t entityType; // 0x2C
        char pad_2D[3];
    };
    static_assert(offsetof(MarkWidget, entityType) == 0x2C, "MarkWidget layout");

    std::vector<uint32_t> MarkedHistory;

    TMapObjBase* NearestNewMarked(TSceneManager* scene, const TMapPlayerObj* player) {
        const TLBSWidget* navi = naviWidget;
        if (!navi || !navi->childrenList || !navi->childrenList->list) {
            return nullptr;
        }
        TMapObjBase* best = nullptr;
        float bestDistance = TargetRange;
        for (uint32_t i = 0; i < navi->childrenList->count; i++) {
            auto* mark = reinterpret_cast<MarkWidget*>(navi->childrenList->list[i]);
            if (!mark || !mark->entityId || !IsClass(mark, "TMarkWidget")
                || std::ranges::find(MarkedHistory, mark->entityId) != MarkedHistory.end()) {
                continue;
            }
            TMapObjBase* entity = FindMonster(scene, mark->entityId);
            if (!entity) {
                continue;
            }
            const float dx = static_cast<float>(entity->xPosition) - player->xPosition;
            const float dy = static_cast<float>(entity->yPosition) - player->yPosition;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= bestDistance) {
                best = entity;
                bestDistance = distance;
            }
        }
        return best;
    }

    void TargetNextMarked(const TLBSWidget* root) {
        TSceneManager* scene = GetSceneManager();
        if (!scene || !scene->mapPlayerObjPtr || !FindNaviWidget(root)) {
            return;
        }
        std::erase_if(MarkedHistory, [scene](const uint32_t id) { return !FindMonster(scene, id); });
        TMapObjBase* next = NearestNewMarked(scene, scene->mapPlayerObjPtr);
        if (!next && !MarkedHistory.empty()) {
            const uint32_t current = MarkedHistory.back();
            MarkedHistory.assign(1, current);
            next = NearestNewMarked(scene, scene->mapPlayerObjPtr);
            MarkedHistory.clear();
        }
        if (!next) {
            return;
        }
        MarkedHistory.push_back(next->objectID);
        SelectTarget(next);
    }

    void TargetNext() {
        TSceneManager* scene = GetSceneManager();
        if (!scene || !scene->mapPlayerObjPtr) {
            return;
        }
        PruneHistory(scene);
        if (HistoryIndex + 1 < TargetHistory.size()) {
            HistoryIndex++;
            SelectTarget(FindMonster(scene, TargetHistory[HistoryIndex]));
            return;
        }
        TMapObjBase* next = NearestNewMonster(scene, scene->mapPlayerObjPtr);
        if (!next && !TargetHistory.empty()) {
            const uint32_t current = TargetHistory[HistoryIndex];
            TargetHistory.assign(1, current);
            next = NearestNewMonster(scene, scene->mapPlayerObjPtr);
            TargetHistory.clear();
        }
        if (!next) {
            return;
        }
        TargetHistory.push_back(next->objectID);
        HistoryIndex = TargetHistory.size() - 1;
        SelectTarget(next);
    }

    void TargetPrevious() {
        TSceneManager* scene = GetSceneManager();
        if (!scene) {
            return;
        }
        PruneHistory(scene);
        if (HistoryIndex == 0 || TargetHistory.empty()) {
            return;
        }
        HistoryIndex--;
        SelectTarget(FindMonster(scene, TargetHistory[HistoryIndex]));
    }

    float ActionMovementPause = 0.2f;
    std::chrono::steady_clock::time_point MovementPausedUntil{};

    void PauseMovement() {
        MovementPausedUntil = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(static_cast<int>(ActionMovementPause * 1000.0f));
    }


    bool PadIsActive = false;
    DWORD LastPadInputTick = 0;
    DWORD LastKeyboardMouseTick = 0;
    POINT LastCursor{};

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
    constexpr const char* HiddenBarClasses[] = {"TNTQuickSlotWidget", "TNTPartnerSlotWidget", "TNTPetSKillSlotWidget"};
    constexpr int16_t OffScreen = -20000;

    struct MovedBar {
        TLBSWidget* widget;
        int16_t x;
        int16_t y;
    };
    std::vector<MovedBar> MovedBars;

    void RestoreBars() {
        for (const MovedBar& bar : MovedBars) {
            const int16_t width = bar.widget->rect.right - bar.widget->rect.left;
            const int16_t height = bar.widget->rect.bottom - bar.widget->rect.top;
            bar.widget->rect = {bar.x, bar.y, static_cast<int16_t>(bar.x + width), static_cast<int16_t>(bar.y + height)};
        }
        MovedBars.clear();
    }

    bool EditModeActive();

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

    namespace Overlay {
        struct Sprite {
            int image;
            int16_t imageSize;
            AtlasFrame frame;
            int16_t width;
            int16_t height;
        };
        constexpr Sprite AttackSprite{1593835568, 512, {161, 91, 30, 30}, 34, 34};
        constexpr Sprite ClearSprite{1593835782, 256, {204, 2, 30, 29}, 32, 31};
        constexpr Sprite PrevSprite{1593835585, 512, {476, 50, 11, 17}, 18, 28};
        constexpr Sprite NextSprite{1593835585, 512, {496, 50, 11, 17}, 18, 28};
        constexpr Sprite BossSprite{1593835574, 512, {432, 23, 56, 56}, 36, 36};
        constexpr Sprite SpecialistSprite{1593835617, 512, {307, 53, 30, 30}, 34, 34};
        constexpr Sprite PartnerSprite{1593835617, 512, {444, 53, 28, 29}, 32, 33};
        constexpr Sprite PetFollowSprite{1593835569, 512, {368, 24, 27, 25}, 27, 25};
        constexpr Sprite PetStaySprite{1593835569, 512, {394, 24, 25, 25}, 25, 25};
        constexpr Sprite ChatSprite{1593835576, 512, {426, 0, 23, 20}, 30, 26};
        constexpr int16_t RestMotion = 1;
        constexpr int16_t PickUpMotion = 2;
        constexpr int16_t SlotSize = 56;
        constexpr int16_t Step = 62;
        constexpr int16_t CrossGap = 2 * Step + 150;
        constexpr int16_t BottomMargin = 112;
        constexpr int16_t EditRaise = 5;
        constexpr int16_t GlyphOffset = -8;
        constexpr int16_t TextNudge = -1;

        struct Image {
            int id = 0;
            uint16_t width = 0;
            uint16_t height = 0;
        };

        Image Load(const char* name) {
            Image image;
            image.id = LoadUiImageResource(name, image.width, image.height);
            return image;
        }

        struct Cell {
            int16_t dx;
            int16_t dy;
            const char* glyph;
        };
        constexpr Cell Cells[] = {
            {-CrossGap / 2, -Step, "XBOX_DPAD_UP_SMALL"},
            {-CrossGap / 2, Step, "XBOX_DPAD_DOWN_SMALL"},
            {-CrossGap / 2 - Step, 0, "XBOX_DPAD_LEFT_SMALL"},
            {-CrossGap / 2 + Step, 0, "XBOX_DPAD_RIGHT_SMALL"},
            {CrossGap / 2, -Step, "XBOX_BUTTON_COLOR_Y_SMALL"},
            {CrossGap / 2, Step, "XBOX_BUTTON_COLOR_A_SMALL"},
            {CrossGap / 2 - Step, 0, "XBOX_BUTTON_COLOR_X_SMALL"},
            {CrossGap / 2 + Step, 0, "XBOX_BUTTON_COLOR_B_SMALL"},
        };

        enum Action : uint8_t {
            NoAction, Attack, ClearTarget, NextTarget, PrevTarget, BossTarget, PickUp, Sit, Specialist,
            PartnerSpecialist, PetsFollow, PetsStop, Chat, PetSkill1, PetSkill2, PetSkill3, PartnerSkill1, PartnerSkill2,
            PartnerSkill3, ActionCount
        };
        struct ActionLook {
            const wchar_t* text;
            const Sprite* sprite;
            int16_t motion;
            const wchar_t* caption;
        };
        constexpr ActionLook Actions[ActionCount] = {
            {L"", nullptr, 0, L""},
            {L"Attack", &AttackSprite, 0, L""},
            {L"Clear", &ClearSprite, 0, L""},
            {L"Next", &NextSprite, 0, L""},
            {L"Prev", &PrevSprite, 0, L""},
            {L"Boss", &BossSprite, 0, L""},
            {L"Pick up", nullptr, PickUpMotion, L""},
            {L"Sit", nullptr, RestMotion, L""},
            {L"SP", &SpecialistSprite, 0, L""},
            {L"Partner", &PartnerSprite, 0, L"Partner"},
            {L"Pets", &PetFollowSprite, 0, L""},
            {L"Stay", &PetStaySprite, 0, L""},
            {L"Chat", &ChatSprite, 0, L""},
            {L"Pet 1", nullptr, 0, L""},
            {L"Pet 2", nullptr, 0, L""},
            {L"Pet 3", nullptr, 0, L""},
            {L"Partner 1", nullptr, 0, L""},
            {L"Partner 2", nullptr, 0, L""},
            {L"Partner 3", nullptr, 0, L""},
        };

        bool IsBarSkill(const uint8_t action) {
            return action >= PetSkill1 && action <= PartnerSkill3;
        }

        struct SlotView {
            Rect rect{};
            TEWLabel* text = nullptr;
            TEWLabel* caption = nullptr;
            TEWCustomPanelWidget* actionIcon = nullptr;
            TNTTimeAniIcon* icon = nullptr;
            TNTTimeAniIcon* source = nullptr;
            int16_t motion = 0;
            uintptr_t barRecord = 0;
            uint32_t cooldownStart = 0;
            bool missing = false;
            bool cooling = false;
        };

        struct Panel {
            TLBSWidget* container = nullptr;
            SlotView slots[8];
            bool shown = false;
        };

        constexpr uint32_t BindingFirst = 0x70;
        constexpr uint32_t BindingSize = 0xBC - 0x70;
        struct Binding {
            bool set = false;
            uint8_t action = NoAction;
            uint8_t fields[BindingSize]{};
            int16_t tab = 0;
            int16_t index = 0;
        };
        enum Layer { Base, RB, LT, RT, LB, LTRT, LayerCount };
        Binding Bindings[LayerCount][8];

        bool EditMode = false;
        bool AppliedEditMode = false;

        Image SlotImage;
        Image CellGlyphs[8];
        Image LayerGlyphs[LayerCount][2];
        bool ImagesLoaded = false;

        TLBSWidget* AttachedRoot = nullptr;
        Panel Panels[LayerCount];

        void LoadImages() {
            if (ImagesLoaded) return;
            ImagesLoaded = true;
            SlotImage = Load("SLOT");
            for (int i = 0; i < 8; i++) CellGlyphs[i] = Load(Cells[i].glyph);
            LayerGlyphs[RB][0] = Load("XBOX_RB");
            LayerGlyphs[LT][0] = Load("XBOX_LT");
            LayerGlyphs[RT][0] = Load("XBOX_RT");
            LayerGlyphs[LB][0] = Load("XBOX_LB");
            LayerGlyphs[LTRT][0] = LayerGlyphs[LT][0];
            LayerGlyphs[LTRT][1] = LayerGlyphs[RT][0];
        }

        void Attach(TLBSWidget* parent, TLBSWidget* child) {
            child->parent = parent;
            parent->childrenList->push_back(child);
        }

        void Detach(TLBSWidget* widget) {
            if (TLBSWidget* parent = widget->parent; parent && parent->childrenList
                && parent->childrenList->index_of(widget) >= 0) {
                parent->childrenList->remove(widget);
            }
        }

        TEWLabel* AddLabel(TLBSWidget* parent, const int16_t x, const int16_t textY, const int16_t width,
                           const uint8_t alignment, const wchar_t* text) {
            TEWLabel* label = Widget::Create<TEWLabel>(CachedHost);
            if (!label) return nullptr;
            label->rect = {x, static_cast<int16_t>(textY + TextNudge), static_cast<int16_t>(x + width),
                           static_cast<int16_t>(textY + TextNudge + 30)};
            label->textAlignment = alignment;
            label->pxPerLine = width;
            label->SetText(text);
            Attach(parent, label);
            return label;
        }

        TEWControlWidget* AddSprite(TLBSWidget* parent, const int image, const AtlasFrame& frame, const int16_t x,
                                    const int16_t y, const int16_t imageWidth = 512, const int16_t imageHeight = 512) {
            auto* sprite = Widget::Create<TEWControlWidget>(CachedHost);
            if (!sprite) return nullptr;
            delete[] sprite->imageData.atlasFrames;
            sprite->imageData.imageName = image;
            sprite->imageData.imageWidth = imageWidth;
            sprite->imageData.imageHeight = imageHeight;
            sprite->imageData.frameCount = 1;
            sprite->imageData.atlasFrames = new AtlasFrame[1]{frame};
            sprite->rect = {x, y, static_cast<int16_t>(x + frame.width), static_cast<int16_t>(y + frame.height)};
            Attach(parent, sprite);
            return sprite;
        }

        TEWControlWidget* AddImage(TLBSWidget* parent, const Image& image, const int16_t x, const int16_t y) {
            if (!image.id) return nullptr;
            return AddSprite(parent, image.id, AtlasFrame{0, 0, static_cast<int16_t>(image.width), static_cast<int16_t>(image.height)},
                             x, y, static_cast<int16_t>(image.width), static_cast<int16_t>(image.height));
        }

        void PlacePicture(TEWCustomPanelWidget* picture, const Sprite& sprite, const int16_t left, const int16_t top,
                          const bool captioned) {
            picture->imageData.imageName = sprite.image;
            picture->imageData.imageWidth = sprite.imageSize;
            picture->imageData.imageHeight = sprite.imageSize;
            picture->imageData.atlasFrames[0] = sprite.frame;
            const int16_t x = static_cast<int16_t>(left + (SlotSize - sprite.width) / 2);
            const int16_t y = static_cast<int16_t>(top + (SlotSize - sprite.height) / 2 + (captioned ? -4 : 4));
            picture->rect = {x, y, static_cast<int16_t>(x + sprite.width), static_cast<int16_t>(y + sprite.height)};
        }

        TEWCustomPanelWidget* AddPicture(TLBSWidget* parent, const Sprite& sprite, const int16_t left, const int16_t top,
                                         const bool captioned) {
            auto* picture = Widget::Create<TEWCustomPanelWidget>(CachedHost);
            if (!picture) return nullptr;
            delete[] picture->imageData.atlasFrames;
            picture->imageData.frameCount = 1;
            picture->imageData.atlasFrames = new AtlasFrame[1]{sprite.frame};
            picture->drawMode = 0;
            picture->isMoveable = false;
            picture->isInteractable = false;
            PlacePicture(picture, sprite, left, top, captioned);
            Attach(parent, picture);
            return picture;
        }

        void StopCooldown(TNTTimeAniIcon* icon);
        void SaveBindings();

        void ApplyBinding(SlotView& slot, const Binding& binding) {
            const ActionLook& look = Actions[binding.set ? binding.action : NoAction];
            if (slot.text) slot.text->SetText(look.text);
            if (slot.caption) slot.caption->SetText(look.caption);
            if (slot.actionIcon && look.sprite) PlacePicture(slot.actionIcon, *look.sprite, -GlyphOffset, -GlyphOffset, look.caption[0]);
            slot.motion = 0;
            slot.barRecord = 0;
            if (!slot.icon || !binding.set || binding.action) return;
            static_cast<TLBSWidget*>(slot.icon)->isInteractable = true;
            auto* bytes = reinterpret_cast<uint8_t*>(slot.icon);
            // Never copy 0x78..0x97, it's mouse state from the drag and makes hovering start a drag.
            std::memcpy(bytes + 0x70, binding.fields + (0x70 - BindingFirst), 0x78 - 0x70);
            std::memcpy(bytes + 0x98, binding.fields + (0x98 - BindingFirst), 0xBC - 0x98);
            *reinterpret_cast<TNTTimeAniIcon**>(bytes + 0x74) = slot.icon;
            slot.source = nullptr;
            slot.icon->resized = true;
            StopCooldown(slot.icon);
            slot.cooling = false;
        }

        void InitCooldownFields(TNTTimeAniIcon* icon, const TLBSWidget* root);

        void __cdecl OnSlotClick(void* argument);

        struct SlotRef {
            int layer;
            int cell;
        };
        SlotRef SlotRefs[LayerCount][8];

        TEWGraphicButtonWidget* AddSquareButton(TLBSWidget* parent, const int16_t x, const int16_t y,
                                                const WidgetKit::ClickFn onClick, void* argument) {
            if (!SlotImage.id) return nullptr;
            auto* button = Widget::Create<TEWGraphicButtonWidget>(CachedHost);
            if (!button) return nullptr;
            const AtlasFrame whole{0, 0, static_cast<int16_t>(SlotImage.width), static_cast<int16_t>(SlotImage.height)};
            delete[] button->imageData.atlasFrames;
            button->imageData.imageName = SlotImage.id;
            button->imageData.imageWidth = static_cast<int16_t>(SlotImage.width);
            button->imageData.imageHeight = static_cast<int16_t>(SlotImage.height);
            button->imageData.frameCount = 3;
            button->imageData.atlasFrames = new AtlasFrame[3]{whole, whole, whole};
            button->drawMode = 0;
            button->rect = {x, y, static_cast<int16_t>(x + SlotSize), static_cast<int16_t>(y + SlotSize)};
            WidgetKit::SetOnClick(button, onClick, argument);
            Attach(parent, button);
            return button;
        }

        void AddClickAbsorber(TLBSWidget* container) {
            const int16_t width = static_cast<int16_t>(container->rect.right - container->rect.left);
            const int16_t height = static_cast<int16_t>(container->rect.bottom - container->rect.top);
            TEWGraphicButtonWidget* backing = AddSquareButton(container, 0, 0, [](void*) {}, nullptr);
            if (!backing) return;
            backing->rect = {0, 0, width, height};
            backing->color = Color(170, 20, 20, 20);
        }

        void Build(Panel& panel, TLBSWidget* root, const Image (&title)[2], const int layer, const int16_t lift,
                   const int16_t shift, const Binding (&bindings)[8]) {
            const int16_t centreX = static_cast<int16_t>((root->rect.right - root->rect.left) / 2 + shift);
            const int16_t centreY = static_cast<int16_t>(root->rect.bottom - root->rect.top - BottomMargin - lift);
            const int16_t left = static_cast<int16_t>(centreX - CrossGap / 2 - Step - SlotSize / 2 + GlyphOffset);
            const int16_t top = static_cast<int16_t>(centreY - Step - SlotSize / 2 + GlyphOffset);
            const int16_t right = static_cast<int16_t>(centreX + CrossGap / 2 + Step + SlotSize / 2);
            const int16_t bottom = static_cast<int16_t>(centreY + Step + SlotSize / 2);
            auto* container = Widget::Create<TLBSWidget>(CachedHost);
            if (!container) return;
            container->rect = {left, top, right, bottom};
            container->isVisible = false;
            Attach(root, container);
            panel.container = container;

            for (int i = 0; i < 8; i++) {
                const int16_t x = static_cast<int16_t>(centreX + Cells[i].dx - SlotSize / 2);
                const int16_t y = static_cast<int16_t>(centreY + Cells[i].dy - SlotSize / 2);
                SlotView& slot = panel.slots[i];
                slot = {};
                slot.rect = {x, y, static_cast<int16_t>(x + SlotSize), static_cast<int16_t>(y + SlotSize)};
                auto* group = Widget::Create<TLBSWidget>(CachedHost);
                if (!group) continue;
                const int16_t gx = static_cast<int16_t>(x + GlyphOffset - left);
                const int16_t gy = static_cast<int16_t>(y + GlyphOffset - top);
                group->rect = {gx, gy, static_cast<int16_t>(gx + SlotSize - GlyphOffset), static_cast<int16_t>(gy + SlotSize - GlyphOffset)};
                Attach(container, group);
                constexpr int16_t In = -GlyphOffset;
                SlotRefs[layer][i] = {layer, i};
                AddSquareButton(group, In, In, &OnSlotClick, &SlotRefs[layer][i]);
                slot.actionIcon = AddPicture(group, PetFollowSprite, In, In, false);
                if (slot.actionIcon) slot.actionIcon->isVisible = false;
                slot.text = AddLabel(group, In - 8, In + SlotSize / 2 - 2, SlotSize + 16, 3, L"");
                if (auto* icon = Widget::Create<TNTTimeAniIcon>(CachedHost)) {
                    InitCooldownFields(icon, root);
                    constexpr int16_t IconInset = 4;
                    icon->rect = {static_cast<int16_t>(In + IconInset), static_cast<int16_t>(In + IconInset),
                                  static_cast<int16_t>(In + SlotSize - IconInset), static_cast<int16_t>(In + SlotSize - IconInset)};
                    static_cast<TLBSWidget*>(icon)->isVisible = false;
                    Attach(group, icon);
                    slot.icon = icon;
                }
                slot.caption = AddLabel(group, In - 8, In + SlotSize - 22, SlotSize + 16, 3, L"");
                if (slot.caption) slot.caption->isVisible = false;
                ApplyBinding(slot, bindings[i]);
                AddImage(group, CellGlyphs[i], 0, 0);
            }
            if (title[1].id) {
                constexpr int16_t Half = 40;
                AddImage(container, title[0], static_cast<int16_t>(centreX - title[0].width / 2 - left),
                         static_cast<int16_t>(centreY - Half - title[0].height / 2 - top));
                AddImage(container, title[1], static_cast<int16_t>(centreX - title[1].width / 2 - left),
                         static_cast<int16_t>(centreY + Half - title[1].height / 2 - top));
                AddLabel(container, static_cast<int16_t>(centreX - 20 - left), static_cast<int16_t>(centreY - 8 - top), 40, 3, L"+");
            } else if (title[0].id) {
                AddImage(container, title[0], static_cast<int16_t>(centreX - title[0].width / 2 - left),
                         static_cast<int16_t>(centreY - title[0].height / 2 - top));
            }
        }

        void SetShown(TLBSWidget* widget, const bool shown) {
            if (widget && widget->isVisible != shown) widget->isVisible = shown;
        }

        void ShowSlot(SlotView& slot, const Binding& binding) {
            const bool skill = binding.set && !binding.action && !slot.missing;
            const bool action = binding.set && binding.action;
            const ActionLook& look = Actions[action ? binding.action : NoAction];
            const bool motion = action && ((look.motion && slot.motion == look.motion) || (IsBarSkill(binding.action) && slot.barRecord));
            const bool pictured = action && look.sprite;
            SetShown(slot.icon, skill || motion);
            SetShown(slot.actionIcon, pictured);
            SetShown(slot.caption, action && look.caption[0]);
            SetShown(slot.text, action && !pictured && !motion);
        }

        void Show(Panel& panel, const bool shown, const Binding (&bindings)[8]) {
            for (int i = 0; i < 8; i++) ShowSlot(panel.slots[i], bindings[i]);
            if (panel.shown == shown || !panel.container) return;
            panel.shown = shown;
            panel.container->isVisible = shown;
        }

        void Destroy(Panel& panel) {
            if (panel.container) Detach(panel.container);
            panel = {};
        }

        TLBSWidget* SkillWindow(const TLBSWidget* root) {
            return FindWidgetOfClass(root, "TNTNewCharacterSkillInfoWidget", 1);
        }

        TNTTimeAniIcon* SkillWindowIcon(const TLBSWidget* root, const uintptr_t record) {
            const TLBSWidget* window = SkillWindow(root);
            if (!window || !window->childrenList || !window->childrenList->list) return nullptr;
            for (uint32_t i = 0; i < window->childrenList->count; i++) {
                TLBSWidget* child = window->childrenList->list[i];
                if (!IsClass(child, "TNTTimeAniIcon")) continue;
                auto* icon = reinterpret_cast<TNTTimeAniIcon*>(child);
                if (!record || reinterpret_cast<uintptr_t>(icon->image) == record) return icon;
            }
            return nullptr;
        }

        void InitCooldownFields(TNTTimeAniIcon* icon, const TLBSWidget* root) {
            if (const TNTTimeAniIcon* model = SkillWindowIcon(root, 0)) {
                std::memcpy(icon->pad_D4, model->pad_D4, sizeof(icon->pad_D4));
                icon->overlayColor = model->overlayColor;
            }
            if (TEWLabel* label = Widget::Create<TEWLabel>(CachedHost)) {
                label->rect = {0, 17, 48, 47};
                label->textAlignment = 3;
                label->pxPerLine = 48;
                label->SetText(L"");
                Attach(icon, label);
                icon->textLabel = reinterpret_cast<uintptr_t>(label);
            }
        }

        uintptr_t TimerGlobal = 0;

        uint32_t GameTime() {
            if (!TimerGlobal) {
                const uint8_t TimerPattern[] = {0xA1, 0, 0, 0, 0, 0xC3, 0x8B, 0xC0, 0x53, 0x56, 0x57, 0x84, 0xD2, 0x74, 0x08};
                const uintptr_t match = FindPattern(TimerPattern, "x????xxxxxxxxxx", nullptr, 1);
                TimerGlobal = match ? *reinterpret_cast<uintptr_t*>(match) : 0;
                if (!TimerGlobal) return 0;
            }
            return *reinterpret_cast<uint32_t*>(TimerGlobal);
        }

        uintptr_t SetElapsedFn = 0;

        uintptr_t FindSetElapsed() {
            if (!SetElapsedFn) {
                const uint8_t SweepPattern[] = {0x53, 0x56, 0x83, 0xC4, 0xF8, 0x8B, 0xD8, 0xBE, 0, 0, 0, 0,
                                                0x8B, 0xC2, 0x89, 0x83, 0x0C, 0x01, 0x00, 0x00};
                SetElapsedFn = FindPattern(SweepPattern, "xxxxxxxx????xxxxxxxx", nullptr, 0);
            }
            return SetElapsedFn;
        }

        __declspec(noinline) void SetSweep(TNTTimeAniIcon* icon, int elapsed) {
            if (!GameCallsAllowed || !FindSetElapsed()) return;
            uintptr_t function = SetElapsedFn;
            _asm {
                mov eax, icon
                mov edx, elapsed
                call function
            }
        }

        uintptr_t SetLengthFn = 0;
        uintptr_t SetRunningFn = 0;

        uintptr_t FindSetLength() {
            if (!SetLengthFn) {
                const uint8_t Pattern[] = {0x51, 0x83, 0xFA, 0x01, 0x7C, 0, 0x8B, 0xCA, 0x89, 0x88, 0x14, 0x01, 0x00, 0x00,
                                           0x3B, 0x88, 0x0C, 0x01, 0x00, 0x00};
                SetLengthFn = FindPattern(Pattern, "xxxxx?xxxxxxxxxxxxxx", nullptr, 0);
            }
            return SetLengthFn;
        }

        uintptr_t FindSetRunning() {
            if (!SetRunningFn) {
                const uint8_t Pattern[] = {0x53, 0x8B, 0xD8, 0x8B, 0xC2, 0x88, 0x83, 0x1F, 0x01, 0x00, 0x00, 0x84, 0xC0, 0x74, 0, 0xE8};
                SetRunningFn = FindPattern(Pattern, "xxxxxxxxxxxxxx?x", nullptr, 0);
            }
            return SetRunningFn;
        }

        __declspec(noinline) void SetLength(TNTTimeAniIcon* icon, int milliseconds) {
            if (!GameCallsAllowed || !FindSetLength()) return;
            uintptr_t function = SetLengthFn;
            _asm {
                mov eax, icon
                mov edx, milliseconds
                call function
            }
        }

        __declspec(noinline) void SetRunning(TNTTimeAniIcon* icon, int running) {
            if (!GameCallsAllowed || !FindSetRunning()) return;
            uintptr_t function = SetRunningFn;
            _asm {
                mov eax, icon
                mov edx, running
                call function
            }
        }

        void StopCooldown(TNTTimeAniIcon* icon) {
            icon->elapsedMs = 0;
            icon->unknown_110 = 0;
            icon->cooldownMs = 100;
            icon->startTick = 0;
            icon->flags = 0x00000001;
            SetSweep(icon, 0);
            std::memset(&icon->color, 0xFF, sizeof(icon->color));
            if (icon->textLabel) {
                auto* label = reinterpret_cast<TLBSWidget*>(icon->textLabel);
                if (label->isVisible) label->isVisible = false;
            }
        }

        // The skill's entry holds the cooldown: +0x20 length in 100 ms, +0x24 start tick.
        void ShowEntryCooldown(SlotView& slot, const uint32_t now) {
            TNTTimeAniIcon* icon = slot.icon;
            const auto* entry = reinterpret_cast<const uint8_t*>(icon->image);
            const uint32_t length = *reinterpret_cast<const uint32_t*>(entry + 0x20) * 100;
            const uint32_t start = *reinterpret_cast<const uint32_t*>(entry + 0x24);
            const bool running = start > 0 && now - start < length;
            if (running && (!slot.cooling || start != slot.cooldownStart)) {
                SetLength(icon, static_cast<int>(length));
                SetRunning(icon, 1);
                SetSweep(icon, static_cast<int>(now - start));
                reinterpret_cast<uint8_t*>(&icon->flags)[1] = 1;
                auto* color = reinterpret_cast<uint8_t*>(&icon->color);
                color[0] = 0x4F;
                color[1] = 0x4F;
                color[2] = 0xCD;
                if (icon->textLabel) {
                    auto* label = reinterpret_cast<TLBSWidget*>(icon->textLabel);
                    if (!label->isVisible) label->isVisible = true;
                }
                slot.cooling = true;
                slot.cooldownStart = start;
            } else if (!running && slot.cooling) {
                StopCooldown(icon);
                slot.cooling = false;
                slot.cooldownStart = 0;
            }
        }

        void SyncCooldowns(const TLBSWidget* root) {
            const uint32_t now = GameTime();
            if (!now) return;
            for (int layer = 0; layer < LayerCount; layer++) {
                Panel* panel = &Panels[layer];
                for (int cell = 0; cell < 8; cell++) {
                    SlotView& slot = panel->slots[cell];
                    if (!slot.icon || !slot.icon->image || !slot.icon->isVisible) continue;
                    if (!IsBarSkill(Bindings[layer][cell].action)) {
                        ShowEntryCooldown(slot, now);
                        continue;
                    }
                    const uintptr_t record = reinterpret_cast<uintptr_t>(slot.icon->image);
                    if (!slot.source || !IsClass(slot.source, "TNTTimeAniIcon")
                        || reinterpret_cast<uintptr_t>(slot.source->image) != record) {
                        slot.source = SkillWindowIcon(root, record);
                    }
                    const TNTTimeAniIcon* source = slot.source;
                    if (!source) continue;
                    TNTTimeAniIcon* icon = slot.icon;
                    const auto* sourceFlags = reinterpret_cast<const uint8_t*>(&source->flags);
                    auto* iconFlags = reinterpret_cast<uint8_t*>(&icon->flags);
                    // startTick is really the last update tick, so bring the cooldown up to now.
                    const uint32_t elapsed = source->elapsedMs + (now - source->startTick);
                    const bool running = sourceFlags[3] != 0 && elapsed < source->cooldownMs;
                    if (running) {
                        icon->cooldownMs = source->cooldownMs;
                        icon->elapsedMs = elapsed;
                        icon->startTick = now;
                        iconFlags[0] = sourceFlags[0];
                        iconFlags[1] = 1;
                        iconFlags[2] = sourceFlags[2];
                        iconFlags[3] = 1;
                        icon->color = source->color;
                        if (icon->textLabel) {
                            auto* label = reinterpret_cast<TLBSWidget*>(icon->textLabel);
                            if (!label->isVisible) label->isVisible = true;
                        }
                        slot.cooling = true;
                    } else if (slot.cooling) {
                        StopCooldown(icon);
                        slot.cooling = false;
                    }
                }
            }
        }

        std::string CharacterName;
        int Morph = 0;
        bool LoadPending = false;
        uint32_t LoadDeadline = 0;

        std::string IniPath() {
            char exe[MAX_PATH]{};
            GetModuleFileNameA(nullptr, exe, MAX_PATH);
            std::string folder(exe);
            return folder.substr(0, folder.find_last_of("\\/") + 1) + "mods\\ControllerSupport.ini";
        }

        std::string Section() {
            return CharacterName + "." + std::to_string(Morph);
        }

        void SaveBindings() {
            if (CharacterName.empty()) return;
            const std::string path = IniPath();
            const std::string section = Section();
            for (int layer = 0; layer < LayerCount; layer++) {
                for (int cell = 0; cell < 8; cell++) {
                    const Binding& binding = Bindings[layer][cell];
                    const std::string key = "L" + std::to_string(layer) + "C" + std::to_string(cell);
                    std::string value = "-";
                    if (binding.set && binding.action) value = "A," + std::to_string(binding.action);
                    else if (binding.set) value = "S," + std::to_string(binding.tab) + "," + std::to_string(binding.index);
                    WritePrivateProfileStringA(section.c_str(), key.c_str(), value.c_str(), path.c_str());
                }
            }
        }

        const uint8_t* SkillItem(const TLBSWidget* root, const int16_t tab, const int16_t index) {
            const TLBSWidget* window = SkillWindow(root);
            if (!window) return nullptr;
            const auto* bytes = reinterpret_cast<const uint8_t*>(window);
            for (uint32_t offset = 0x15C; offset <= 0x168; offset += 4) {
                const auto* list = *reinterpret_cast<const uint8_t* const*>(bytes + offset);
                if (!IsClass(reinterpret_cast<const TLBSWidget*>(list), "TNTItemList")) continue;
                const auto* items = *reinterpret_cast<const uint8_t* const* const*>(list + 4);
                const int32_t count = *reinterpret_cast<const int32_t*>(list + 8);
                for (int32_t i = 0; items && i < count; i++) {
                    const uint8_t* item = items[i];
                    if (item && *reinterpret_cast<const uintptr_t*>(item) && *reinterpret_cast<const int16_t*>(item + 6) == tab
                        && *reinterpret_cast<const int16_t*>(item + 8) == index) {
                        return item;
                    }
                }
            }
            return nullptr;
        }

        const uint8_t* MotionItem(const TLBSWidget* root, const int16_t index) {
            return SkillItem(root, 3, index);
        }

        bool ShowMotion(TNTTimeAniIcon* icon, const TLBSWidget* root, const int16_t index) {
            const TNTTimeAniIcon* model = SkillWindowIcon(root, 0);
            const uint8_t* item = MotionItem(root, index);
            if (!model || !item) return false;
            auto* bytes = reinterpret_cast<uint8_t*>(icon);
            const auto* source = reinterpret_cast<const uint8_t*>(model);
            std::memcpy(bytes + 0x70, source + 0x70, 0x78 - 0x70);
            std::memcpy(bytes + 0x98, source + 0x98, 0xB0 - 0x98);
            std::memcpy(bytes + 0xB0, item, 0xBC - 0xB0);
            *reinterpret_cast<TNTTimeAniIcon**>(bytes + 0x74) = icon;
            icon->resized = true;
            static_cast<TLBSWidget*>(icon)->isInteractable = false;
            StopCooldown(icon);
            return true;
        }

        void ClearBindings() {
            for (int layer = 0; layer < LayerCount; layer++) {
                for (int cell = 0; cell < 8; cell++) {
                    Bindings[layer][cell] = {};
                    Panels[layer].slots[cell].source = nullptr;
                    Panels[layer].slots[cell].cooling = false;
                    ApplyBinding(Panels[layer].slots[cell], Bindings[layer][cell]);
                }
            }
        }

        constexpr uint8_t DefaultActions[LayerCount][8] = {
            {NoAction, NoAction, PrevTarget, NextTarget, BossTarget, Attack, NoAction, ClearTarget},
            {PetsFollow, PetsStop, Chat, NoAction, PartnerSpecialist, PickUp, Specialist, Sit},
        };

        void ApplyDefaults() {
            for (int layer = 0; layer < LayerCount; layer++) {
                for (int cell = 0; cell < 8; cell++) {
                    if (!DefaultActions[layer][cell]) continue;
                    Binding& binding = Bindings[layer][cell];
                    binding = {};
                    binding.set = true;
                    binding.action = DefaultActions[layer][cell];
                    ApplyBinding(Panels[layer].slots[cell], binding);
                }
            }
        }

        bool LoadBindings(const TLBSWidget* root) {
            if (!SkillWindowIcon(root, 0)) return false;
            ClearBindings();
            const std::string path = IniPath();
            const std::string section = Section();
            char probe[8]{};
            if (GetPrivateProfileSectionA(section.c_str(), probe, sizeof(probe), path.c_str()) == 0) {
                ApplyDefaults();
                for (int layer = 0; layer < LayerCount; layer++) {
                    for (int cell = 0; cell < 8; cell++) ApplyBinding(Panels[layer].slots[cell], Bindings[layer][cell]);
                }
                return true;
            }
            bool anyMissing = false;
            for (int layer = 0; layer < LayerCount; layer++) {
                for (int cell = 0; cell < 8; cell++) {
                    char value[32]{};
                    const std::string key = "L" + std::to_string(layer) + "C" + std::to_string(cell);
                    GetPrivateProfileStringA(section.c_str(), key.c_str(), "", value, sizeof(value), path.c_str());
                    int kind = 0, tab = 0, index = 0;
                    if (!value[0] && DefaultActions[layer][cell]) {
                        Binding& binding = Bindings[layer][cell];
                        binding.set = true;
                        binding.action = DefaultActions[layer][cell];
                        ApplyBinding(Panels[layer].slots[cell], binding);
                        continue;
                    }
                    if (std::sscanf(value, "A,%d", &kind) == 1 && kind > NoAction && kind < ActionCount) {
                        Binding& binding = Bindings[layer][cell];
                        binding.set = true;
                        binding.action = static_cast<uint8_t>(kind);
                        ApplyBinding(Panels[layer].slots[cell], binding);
                        continue;
                    }
                    if (std::sscanf(value, "S,%d,%d", &tab, &index) != 2) {
                        ApplyBinding(Panels[layer].slots[cell], Bindings[layer][cell]);
                        continue;
                    }
                    const TNTTimeAniIcon* model = SkillWindowIcon(root, 0);
                    const uint8_t* item = SkillItem(root, static_cast<int16_t>(tab), static_cast<int16_t>(index));
                    if (!model || !item) {
                        anyMissing = true;
                        continue;
                    }
                    Binding& binding = Bindings[layer][cell];
                    binding = {};
                    binding.set = true;
                    std::memcpy(binding.fields, reinterpret_cast<const uint8_t*>(model) + BindingFirst, 0xB0 - BindingFirst);
                    std::memcpy(binding.fields + (0xB0 - BindingFirst), item, 0xBC - 0xB0);
                    binding.tab = static_cast<int16_t>(tab);
                    binding.index = static_cast<int16_t>(index);
                    ApplyBinding(Panels[layer].slots[cell], binding);
                }
            }
            return !anyMissing;
        }

        void RequestLoad() {
            LoadPending = true;
            LoadDeadline = GetTickCount() + 10000;
        }

        void TrackCharacter() {
            TSceneManager* scene = GetSceneManager();
            const auto* player = scene ? reinterpret_cast<const uint8_t*>(scene->mapPlayerObjPtr) : nullptr;
            if (!player) return;
            const uint8_t morphByte = player[0x1B9];
            const int morph = morphByte == 0xFF ? 0 : morphByte;
            std::string name;
            if (const auto* text = *reinterpret_cast<const char* const*>(player + 0x1F8)) {
                const int32_t length = *reinterpret_cast<const int32_t*>(text - 4);
                if (length > 0 && length < 64) name.assign(text, length);
            }
            if (name.empty()) return;
            if (name != CharacterName || morph != Morph) {
                CharacterName = name;
                Morph = morph;
                RequestLoad();
            }
        }

        void UpdateLoad(const TLBSWidget* root) {
            if (!LoadPending || CharacterName.empty() || !Panels[Base].container) return;
            if (LoadBindings(root) || static_cast<int32_t>(GetTickCount() - LoadDeadline) > 0) LoadPending = false;
        }

        bool WasDragging = false;
        uint8_t DragFields[BindingSize]{};

        bool Contains(const Rect& rect, const int32_t x, const int32_t y) {
            return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
        }

        void WatchDrag(const TLBSWidget* root, const int32_t mouseX, const int32_t mouseY) {
            TLBSWidget* drag = FindWidgetOfClass(root, "TNTDragIconWidget", 1);
            const bool dragging = drag && drag->isVisible;
            if (dragging) {
                std::memcpy(DragFields, reinterpret_cast<uint8_t*>(drag) + BindingFirst, BindingSize);
                const auto* source = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(drag) + 0xD8);
                if (source && IsClass(reinterpret_cast<const TLBSWidget*>(source), "TNTTimeAniIcon")) {
                    std::memcpy(DragFields, source + BindingFirst, 0xA8 - BindingFirst);
                }
            } else if (WasDragging) {
                for (int layer = 0; layer < LayerCount; layer++) {
                    for (int i = 0; i < 8; i++) {
                        if (!Contains(Panels[layer].slots[i].rect, mouseX, mouseY)) continue;
                        Binding& binding = Bindings[layer][i];
                        binding = {};
                        binding.set = true;
                        std::memcpy(binding.fields, DragFields, BindingSize);
                        binding.tab = *reinterpret_cast<int16_t*>(DragFields + (0xB6 - BindingFirst));
                        binding.index = *reinterpret_cast<int16_t*>(DragFields + (0xB8 - BindingFirst));
                        ApplyBinding(Panels[layer].slots[i], binding);
                        SaveBindings();
                    }
                }
            }
            WasDragging = dragging;
        }

        struct PendingCast {
            HWND window = nullptr;
            LPARAM at = 0;
            LPARAM back = 0;
            int layer = 0;
            int step = 0;
            bool active = false;
        };
        PendingCast Casting;

        // Skills are used with a posted double-click because the skill window's handler is guarded.
        bool ResolveSkill(const TLBSWidget* root, Binding& binding, SlotView& slot);

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

        uint8_t SelectedAction = NoAction;
        TLBSWidget* PaletteContainer = nullptr;
        TEWGraphicButtonWidget* PaletteTiles[ActionCount]{};
        Rect PaletteRects[ActionCount]{};
        uint8_t PaletteRefs[ActionCount]{};
        TNTTimeAniIcon* PaletteMotions[ActionCount]{};
        bool PaletteMotionShown[ActionCount]{};
        uintptr_t PaletteBarRecords[ActionCount]{};
        TEWLabel* PaletteTexts[ActionCount]{};

        void HighlightSelection() {
            for (int id = 1; id < ActionCount; id++) {
                if (!PaletteTiles[id]) continue;
                const bool selected = id == SelectedAction;
                PaletteTiles[id]->color = selected ? Color(255, 255, 220, 120) : Color(255, 255, 255, 255);
            }
        }

        void __cdecl OnPaletteClick(void* argument) {
            const uint8_t id = *static_cast<uint8_t*>(argument);
            SelectedAction = SelectedAction == id ? NoAction : id;
            HighlightSelection();
        }

        void PlaceAction(int layer, int cell, uint8_t action);

        void __cdecl OnSlotClick(void* argument) {
            if (!EditMode || !SelectedAction) return;
            const auto* ref = static_cast<SlotRef*>(argument);
            PlaceAction(ref->layer, ref->cell, SelectedAction);
        }

        void BuildPalette(TLBSWidget* root, const int16_t panelsRight) {
            constexpr int Rows = ActionCount / 2;
            const int16_t height = static_cast<int16_t>(Rows * Step + 30);
            const int16_t left = static_cast<int16_t>(panelsRight + 30);
            const int16_t top = static_cast<int16_t>(root->rect.bottom - root->rect.top - BottomMargin - Step - SlotSize / 2 - height + SlotSize + 2 * Step - EditRaise);
            auto* container = Widget::Create<TLBSWidget>(CachedHost);
            if (!container) return;
            container->rect = {left, top, static_cast<int16_t>(left + 2 * Step), static_cast<int16_t>(top + height)};
            Attach(root, container);
            PaletteContainer = container;
            AddLabel(container, 0, 0, 2 * Step, 3, L"Actions");
            for (int id = 1; id < ActionCount; id++) {
                const int16_t x = static_cast<int16_t>(((id - 1) % 2) * Step);
                const int16_t y = static_cast<int16_t>(24 + ((id - 1) / 2) * Step);
                PaletteRefs[id] = static_cast<uint8_t>(id);
                PaletteTiles[id] = AddSquareButton(container, x, y, &OnPaletteClick, &PaletteRefs[id]);
                PaletteRects[id] = {static_cast<int16_t>(left + x), static_cast<int16_t>(top + y),
                                    static_cast<int16_t>(left + x + SlotSize), static_cast<int16_t>(top + y + SlotSize)};
                const ActionLook& look = Actions[id];
                PaletteMotions[id] = nullptr;
                PaletteMotionShown[id] = false;
                if (look.sprite) {
                    AddPicture(container, *look.sprite, x, y, look.caption[0]);
                } else if (look.motion || IsBarSkill(static_cast<uint8_t>(id))) {
                    PaletteBarRecords[id] = 0;
                    if (IsBarSkill(static_cast<uint8_t>(id))) {
                        PaletteTexts[id] = AddLabel(container, static_cast<int16_t>(x - 8), static_cast<int16_t>(y + SlotSize / 2 - 6),
                                                    SlotSize + 16, 3, look.text);
                    }
                    if (auto* icon = Widget::Create<TNTTimeAniIcon>(CachedHost)) {
                        InitCooldownFields(icon, root);
                        constexpr int16_t IconInset = 4;
                        icon->rect = {static_cast<int16_t>(x + IconInset), static_cast<int16_t>(y + IconInset),
                                      static_cast<int16_t>(x + SlotSize - IconInset), static_cast<int16_t>(y + SlotSize - IconInset)};
                        static_cast<TLBSWidget*>(icon)->isVisible = false;
                        Attach(container, icon);
                        PaletteMotions[id] = icon;
                    }
                } else {
                    AddLabel(container, static_cast<int16_t>(x - 8), static_cast<int16_t>(y + SlotSize / 2 - 6), SlotSize + 16, 3, look.text);
                }
                if (look.caption[0]) {
                    AddLabel(container, static_cast<int16_t>(x - 8), static_cast<int16_t>(y + SlotSize - 22), SlotSize + 16, 3, look.caption);
                }
            }
            HighlightSelection();
        }

        TLBSWidget* EditBoard = nullptr;

        void BuildEditBoard(TLBSWidget* root) {
            Rect area = Panels[Base].container ? Panels[Base].container->rect : Rect{};
            auto grow = [&area](const TLBSWidget* widget) {
                if (!widget) return;
                area.left = std::min(area.left, widget->rect.left);
                area.top = std::min(area.top, widget->rect.top);
                area.right = std::max(area.right, widget->rect.right);
                area.bottom = std::max(area.bottom, widget->rect.bottom);
            };
            for (const Panel& panel : Panels) grow(panel.container);
            grow(PaletteContainer);
            constexpr int16_t Margin = 12;
            auto* board = Widget::Create<TEWCustomPanelWidget>(CachedHost);
            if (!board || !SlotImage.id) return;
            const int16_t width = static_cast<int16_t>(area.right - area.left + 2 * Margin);
            const int16_t height = static_cast<int16_t>(area.bottom - area.top + 2 * Margin);
            board->rect = {static_cast<int16_t>(area.left - Margin), static_cast<int16_t>(area.top - Margin),
                           static_cast<int16_t>(area.left - Margin + width), static_cast<int16_t>(area.top - Margin + height)};
            constexpr int16_t C = 10;
            const int16_t S = static_cast<int16_t>(SlotImage.width);
            const int16_t M = static_cast<int16_t>(S - 2 * C);
            delete[] board->imageData.atlasFrames;
            board->imageData.imageName = SlotImage.id;
            board->imageData.imageWidth = S;
            board->imageData.imageHeight = static_cast<int16_t>(SlotImage.height);
            board->imageData.frameCount = 9;
            board->imageData.atlasFrames = new AtlasFrame[9]{
                {C, C, M, M}, {0, 0, C, C}, {C, 0, M, C}, {static_cast<int16_t>(S - C), 0, C, C},
                {static_cast<int16_t>(S - C), C, C, M}, {static_cast<int16_t>(S - C), static_cast<int16_t>(S - C), C, C},
                {C, static_cast<int16_t>(S - C), M, C}, {0, static_cast<int16_t>(S - C), C, C}, {0, C, C, M},
            };
            const uint16_t middleWidth = static_cast<uint16_t>(width - 2 * C);
            const uint16_t middleHeight = static_cast<uint16_t>(height - 2 * C);
            board->nineSliceInfo = {middleWidth, middleHeight, static_cast<uint16_t>(C + middleWidth),
                                    static_cast<uint16_t>(C + middleHeight), C, C, C, C};
            board->sliceCount = 1;
            board->drawMode = 5;
            board->isMoveable = false;
            board->color = Color(220, 255, 255, 255);
            Attach(root, board);
            EditBoard = board;
            for (Panel& panel : Panels) {
                if (panel.container) panel.container->BubbleUp();
            }
            if (PaletteContainer) PaletteContainer->BubbleUp();
        }

        void DestroyPalette() {
            if (EditBoard) Detach(EditBoard);
            EditBoard = nullptr;
            if (PaletteContainer) Detach(PaletteContainer);
            PaletteContainer = nullptr;
            for (auto& tile : PaletteTiles) tile = nullptr;
            for (auto& icon : PaletteMotions) icon = nullptr;
            for (auto& text : PaletteTexts) text = nullptr;
            SelectedAction = NoAction;
        }

        uint8_t DraggedAction = NoAction;
        TEWLabel* DragLabel = nullptr;
        bool LeftWasDown = false;
        int32_t DragStartX = 0, DragStartY = 0;

        void PlaceAction(const int layer, const int cell, const uint8_t action) {
            Binding& binding = Bindings[layer][cell];
            binding = {};
            binding.set = true;
            binding.action = action;
            Panels[layer].slots[cell].source = nullptr;
            Panels[layer].slots[cell].cooling = false;
            ApplyBinding(Panels[layer].slots[cell], binding);
            SaveBindings();
        }

        void WatchPaletteDrag(TLBSWidget* root, const int32_t mouseX, const int32_t mouseY) {
            const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (down && !LeftWasDown) {
                for (int id = 1; id < ActionCount; id++) {
                    if (PaletteTiles[id] && Contains(PaletteRects[id], mouseX, mouseY)) {
                        DraggedAction = static_cast<uint8_t>(id);
                        DragStartX = mouseX;
                        DragStartY = mouseY;
                    }
                }
            }
            const bool moved = DraggedAction && (std::abs(mouseX - DragStartX) > 4 || std::abs(mouseY - DragStartY) > 4);
            if (down && moved) {
                if (!DragLabel && (DragLabel = AddLabel(root, 0, 0, 80, 3, Actions[DraggedAction].text))) {
                    DragLabel->textColor = Color(255, 255, 220, 120);
                }
                if (DragLabel) {
                    const int16_t x = static_cast<int16_t>(mouseX - 40), y = static_cast<int16_t>(mouseY - 20);
                    if (DragLabel->rect.left != x || DragLabel->rect.top != y) DragLabel->rect = {x, y, static_cast<int16_t>(x + 80), static_cast<int16_t>(y + 30)};
                }
            }
            if (!down && LeftWasDown && DraggedAction) {
                if (moved) {
                    for (int layer = 0; layer < LayerCount; layer++) {
                        for (int cell = 0; cell < 8; cell++) {
                            if (Contains(Panels[layer].slots[cell].rect, mouseX, mouseY)) PlaceAction(layer, cell, DraggedAction);
                        }
                    }
                }
                DraggedAction = NoAction;
                if (DragLabel) {
                    Detach(DragLabel);
                    DragLabel = nullptr;
                }
            }
            LeftWasDown = down;
        }

        bool RightWasDown = false;

        void WatchRightClick(const int32_t mouseX, const int32_t mouseY) {
            const bool down = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            if (down && !RightWasDown) {
                for (int layer = 0; layer < LayerCount; layer++) {
                    for (int cell = 0; cell < 8; cell++) {
                        if (!Contains(Panels[layer].slots[cell].rect, mouseX, mouseY)) continue;
                        Bindings[layer][cell] = {};
                        Panels[layer].slots[cell].source = nullptr;
                        Panels[layer].slots[cell].cooling = false;
                        ApplyBinding(Panels[layer].slots[cell], Bindings[layer][cell]);
                        SaveBindings();
                    }
                }
            }
            RightWasDown = down;
        }

        constexpr int16_t EditColumn = (CrossGap + 2 * Step + SlotSize + 40) / 2;
        constexpr int16_t EditRow = 2 * Step + SlotSize + 40;
        constexpr int16_t EditShift[LayerCount] = {-EditColumn, EditColumn, -EditColumn, EditColumn, -EditColumn, EditColumn};
        constexpr int16_t EditLift[LayerCount] = {EditRaise, EditRaise, EditRaise + EditRow, EditRaise + EditRow,
                                                  EditRaise + 2 * EditRow, EditRaise + 2 * EditRow};


        TNTTimeAniIcon* BarIcon(const TLBSWidget* root, const uint8_t action) {
            const bool pet = action <= PetSkill3;
            const TLBSWidget* bar = FindWidgetOfClass(root, pet ? "TNTPetSKillSlotWidget" : "TNTPartnerSlotWidget", 1);
            if (!bar || !bar->childrenList || !bar->childrenList->list) return nullptr;
            int wanted = action - (pet ? PetSkill1 : PartnerSkill1);
            for (uint32_t i = 0; i < bar->childrenList->count; i++) {
                TLBSWidget* child = bar->childrenList->list[i];
                if (IsClass(child, "TNTTimeAniIcon") && wanted-- == 0) return reinterpret_cast<TNTTimeAniIcon*>(child);
            }
            return nullptr;
        }

        uintptr_t CopyBarSkill(TNTTimeAniIcon* icon, const TNTTimeAniIcon* source, const bool interactable) {
            const uintptr_t record = source ? reinterpret_cast<uintptr_t>(source->image) : 0;
            if (!record) return 0;
            auto* bytes = reinterpret_cast<uint8_t*>(icon);
            const auto* from = reinterpret_cast<const uint8_t*>(source);
            std::memcpy(bytes + 0x70, from + 0x70, 0x78 - 0x70);
            std::memcpy(bytes + 0x98, from + 0x98, 0xBC - 0x98);
            *reinterpret_cast<TNTTimeAniIcon**>(bytes + 0x74) = icon;
            icon->resized = true;
            static_cast<TLBSWidget*>(icon)->isInteractable = interactable;
            StopCooldown(icon);
            return record;
        }

        // The game rebuilds skill entries when the list changes, so find them again by tab and id.
        bool ResolveSkill(const TLBSWidget* root, Binding& binding, SlotView& slot) {
            const uint8_t* item = SkillItem(root, binding.tab, binding.index);
            if (!item) {
                slot.missing = true;
                return false;
            }
            slot.missing = false;
            uint8_t* fields = binding.fields + (0xB0 - BindingFirst);
            if (std::memcmp(fields, item, 0xBC - 0xB0) == 0
                && slot.icon && std::memcmp(reinterpret_cast<uint8_t*>(slot.icon) + 0xB0, item, 0xBC - 0xB0) == 0) {
                return true;
            }
            std::memcpy(fields, item, 0xBC - 0xB0);
            if (slot.icon) {
                std::memcpy(reinterpret_cast<uint8_t*>(slot.icon) + 0xB0, item, 0xBC - 0xB0);
                StopCooldown(slot.icon);
            }
            slot.source = nullptr;
            slot.cooling = false;
            slot.cooldownStart = 0;
            return true;
        }

        void ResolveSkills(const TLBSWidget* root) {
            for (int layer = 0; layer < LayerCount; layer++) {
                for (int cell = 0; cell < 8; cell++) {
                    Binding& binding = Bindings[layer][cell];
                    if (binding.set && !binding.action) ResolveSkill(root, binding, Panels[layer].slots[cell]);
                }
            }
        }

        void ShowBarSkills(const TLBSWidget* root) {
            for (int layer = 0; layer < LayerCount; layer++) {
                for (int cell = 0; cell < 8; cell++) {
                    const Binding& binding = Bindings[layer][cell];
                    SlotView& slot = Panels[layer].slots[cell];
                    if (!binding.set || !IsBarSkill(binding.action) || !slot.icon) continue;
                    TNTTimeAniIcon* source = BarIcon(root, binding.action);
                    const uintptr_t record = source ? reinterpret_cast<uintptr_t>(source->image) : 0;
                    if (record == slot.barRecord) continue;
                    slot.barRecord = CopyBarSkill(slot.icon, source, true);
                    slot.source = slot.barRecord ? source : nullptr;
                    slot.cooling = false;
                }
            }
            for (int id = PetSkill1; id <= PartnerSkill3; id++) {
                TNTTimeAniIcon* icon = PaletteMotions[id];
                if (!icon) continue;
                TNTTimeAniIcon* source = BarIcon(root, static_cast<uint8_t>(id));
                const uintptr_t record = source ? reinterpret_cast<uintptr_t>(source->image) : 0;
                if (record == PaletteBarRecords[id]) continue;
                PaletteBarRecords[id] = CopyBarSkill(icon, source, false);
                static_cast<TLBSWidget*>(icon)->isVisible = PaletteBarRecords[id] != 0;
                if (PaletteTexts[id]) PaletteTexts[id]->isVisible = PaletteBarRecords[id] == 0;
            }
        }

        void ShowMotions(const TLBSWidget* root) {
            for (int layer = 0; layer < LayerCount; layer++) {
                for (int cell = 0; cell < 8; cell++) {
                    const Binding& binding = Bindings[layer][cell];
                    SlotView& slot = Panels[layer].slots[cell];
                    const int16_t motion = binding.set && binding.action ? Actions[binding.action].motion : 0;
                    if (motion && slot.icon && slot.motion != motion && ShowMotion(slot.icon, root, motion)) slot.motion = motion;
                }
            }
            for (int id = 1; id < ActionCount; id++) {
                if (!PaletteMotions[id] || !Actions[id].motion || PaletteMotionShown[id] || !ShowMotion(PaletteMotions[id], root, Actions[id].motion)) continue;
                PaletteMotionShown[id] = true;
                static_cast<TLBSWidget*>(PaletteMotions[id])->isVisible = true;
            }
        }

        int ActiveLayer(const XINPUT_GAMEPAD& pad) {
            const bool lt = pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
            const bool rt = pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
            if (lt && rt) return LTRT;
            if (lt) return LT;
            if (rt) return RT;
            if (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) return LB;
            if (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) return RB;
            return Base;
        }

        void Update(TLBSWidget* root, const bool padActive, const XINPUT_GAMEPAD& pad, const int32_t mouseX, const int32_t mouseY) {
            if (!root || !root->childrenList) return;
            if (root != AttachedRoot || EditMode != AppliedEditMode) {
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
                    BuildPalette(root, Panels[RB].container->rect.right);
                    BuildEditBoard(root);
                }
            }
            TrackCharacter();
            UpdateLoad(root);
            StepCast();
            ResolveSkills(root);
            ShowMotions(root);
            ShowBarSkills(root);
            SyncCooldowns(root);
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
    namespace Safety {
        std::string BlockedReason;

        bool IsCheckFunction(const uintptr_t function) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(function);
            for (int i = 0; i < 0x100; i++) {
                if (bytes[i] == 0xB8 && bytes[i + 1] == 0x1E && bytes[i + 2] == 0 && bytes[i + 3] == 0 && bytes[i + 4] == 0
                    && bytes[i + 5] == 0xE8 && bytes[i + 10] == 0xFF && bytes[i + 11] == 0x04 && bytes[i + 12] == 0x85) {
                    return true;
                }
            }
            return false;
        }

        bool CallsCheck(const uintptr_t function) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(function);
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
            const size_t imageSize = GetModuleSize(nullptr);
            for (int i = 0; i < 0x1000; i++) {
                const uint8_t op = bytes[i];
                if ((op == 0xC3 || (op == 0xC2 && bytes[i + 2] == 0)) ) {
                    const uint8_t next = bytes[i + (op == 0xC3 ? 1 : 3)];
                    if (next == 0x8B || next == 0x90 || next == 0x00) break;
                }
                if (op != 0xE8) continue;
                const uintptr_t target = function + i + 5 + *reinterpret_cast<const int32_t*>(bytes + i + 1);
                if (target >= base && target < base + imageSize && IsCheckFunction(target)) return true;
            }
            return false;
        }

        void Verify(const ModHost* host, std::initializer_list<std::pair<const char*, uintptr_t>> functions) {
            for (const auto& [name, address] : functions) {
                if (address && CallsCheck(address)) {
                    GameCallsAllowed = false;
                    BlockedReason += std::string(BlockedReason.empty() ? "" : ", ") + name;
                }
            }
            if (!GameCallsAllowed) {
                BlockedReason = "Game function now guarded: " + BlockedReason + ". ControllerSupport makes no game calls until it's updated.";
                host->ReportStatus(ModHealthLevel::Broken, BlockedReason.c_str());
            }
        }
    }
}

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
        UpdateInputMode(RootWidget);
        if (tickContext.isPlayerLoaded) {
            Overlay::Update(const_cast<TLBSWidget*>(RootWidget), PadIsActive, PadState.Gamepad,
                            tickContext.mouseX, tickContext.mouseY);
        }
        if (PadResult != ERROR_SUCCESS || !tickContext.isPlayerLoaded) {
            return;
        }
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
