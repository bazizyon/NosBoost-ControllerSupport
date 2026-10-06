#pragma once
#include "ModContract.h"
#include "TEWLabel.h"
#include "TEWControlWidget.h"
#include "TEWGraphicButtonWidget.h"
#include "TEWCustomPanelWidget.h"
#include "WidgetKit.h"
#include "TNTIconWidget.h"
#include "TNTTimeAniIcon.h"
#include "TSceneManager.h"
#include "TMapObjBase.h"
#include <string>
#include <chrono>
#include <Xinput.h>
#include <format>
#include <Psapi.h>
#include <cmath>
#include <random>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace ControllerSupport {
    inline bool GameCallsAllowed = true;

    inline const ModHost* CachedHost = nullptr;
    inline TSceneManager* sceneManager = nullptr;

    inline uintptr_t moveFunction;
    inline uintptr_t rotateByFunction;
    inline uintptr_t petsFollowFunction;
    inline uintptr_t stopActionFunction;
    inline uintptr_t selectByIdFunction;
    inline uintptr_t talkFunction;
    inline uintptr_t talkOwnerGlobal;
    inline uintptr_t walkTargetGlobal;
    inline TLBSWidget* naviWidget = nullptr;

    using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    inline GetStateFn getState = nullptr;

    uintptr_t GetModuleBase(const char* moduleName);
    size_t GetModuleSize(const char* moduleName);
    uintptr_t FindPattern(const uint8_t* pattern, const char* mask,const char* moduleName, const ptrdiff_t resultOffset);
    void Initialize();
    void FindRotateBy();
    void FindPetsFollow();
    void FindStopAction();
    void FindSelectById();
    void FindTalk();
    void FindWalkTargetGlobal();
    void ClearWalkTarget();
    bool PlayerMayMove(const TLBSWidget* navi);
    bool IsClass(const TLBSWidget* widget, const char* name);
    TLBSWidget* FindWidgetOfClass(const TLBSWidget* parent, const char* name, const int depth);
    TLBSWidget* FindNaviWidget(const TLBSWidget* root);

    inline char LastKeyWindowClass[64] = "";

    HWND FindGameWindow();
    void PressKey(const UINT virtualKey);
    void LoadXInput();
    TSceneManager* GetSceneManager();
    uintptr_t GetPlayerObjManagerPtr();
    __declspec(noinline) void RotateBy(TLBSRotDamper* damper, float delta);
    __declspec(noinline) void PetsFollow(TLBSWidget* navi, int x, int y);
    __declspec(noinline) void StopAction();
    __declspec(noinline) void SelectTarget(const TMapObjBase* entity);
    __declspec(noinline) void TalkTo(const TMapObjBase* npc);
    void MoveTo(int x, int y);
    int LoadUiImageResource(const char* name, uint16_t& width, uint16_t& height);
}
