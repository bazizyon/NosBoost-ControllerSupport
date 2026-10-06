#pragma once
#include "Game.h"

namespace ControllerSupport::Navigator {
    struct Target {
        TLBSWidget* widget;
        Rect rect;
    };

    inline std::vector<Target> Targets;
    inline TLBSWidget* Window = nullptr;
    inline TLBSWidget* Selected = nullptr;
    inline Rect SelectedRect{};
    inline TEWCustomPanelWidget* Frame[8]{};
    inline TLBSWidget* FrameRoot = nullptr;
    inline Rect FrameAround{};
    inline WORD PreviousButtons = 0;
    inline DWORD NextRepeat = 0;
    inline bool Holding = false;
    inline bool WaitNeutral = false;
    inline bool Manual = false;
    inline bool ViewWasDown = false;
    inline bool Pointing = false;
    inline float PointerX = 0.0f;
    inline float PointerY = 0.0f;
    inline POINT PointerShown{-1, -1};
    inline DWORD PointerTick = 0;
    inline POINT SavedCursor{};
    inline bool CursorSaved = false;
    inline DWORD LastClickTick = 0;
    inline Rect LastClickRect{};
    inline bool PressPending = false;
    inline DWORD PressTick = 0;
    inline Rect PressRect{};
    inline TLBSWidget* Legend = nullptr;
    inline std::vector<TLBSWidget*> ShownBefore;

    bool Update(TLBSWidget* root, const XINPUT_GAMEPAD& pad, bool always = false);
    void Close(bool restoreCursor = true);
    void Destroy();
    TEWCustomPanelWidget* AddBoard(TLBSWidget* parent, int16_t width, int16_t height);
}
