#include "Game.h"
#include "Overlay.h"
#include "Navigator.h"
#include "Keyboard.h"
#include <deque>

namespace ControllerSupport::Keyboard {
    constexpr int Columns = 10;
    constexpr int Rows = 4;
    constexpr const wchar_t* Pages[][Rows] = {
        {L"1234567890", L"qwertyuiop", L"asdfghjkl'", L"zxcvbnm,.?"},
        {L"1234567890", L"QWERTYUIOP", L"ASDFGHJKL\"", L"ZXCVBNM;:!"},
        {L"!@#$%^&*()", L"-_=+[]{}\\|", L"/<>~`;:'\"?", L"äöüßéèçñ,."},
    };
    constexpr int PageCount = static_cast<int>(std::size(Pages));
    constexpr int16_t Key = 40, Gap = 4, Pad = 10, HintHeight = 26;

    TLBSWidget* Board = nullptr;
    TLBSWidget* BoardRoot = nullptr;
    TEWLabel* Labels[Rows][Columns]{};
    TEWCustomPanelWidget* Frame = nullptr;
    int Page = 0;
    int ShownPage = -1;
    int Row = 1;
    int Column = 0;
    int ShownRow = -1;
    int ShownColumn = -1;
    TLBSWidget* Editing = nullptr;
    TLBSWidget* ClosedFor = nullptr;
    DWORD NextCheck = 0;
    WORD Previous = 0;
    DWORD NextRepeat = 0;
    bool Moving = false;
    bool Forced = false;
    bool HoldView = false;

    TLBSWidget* FindEditing(TLBSWidget* widget) {
        if (!widget || !widget->isVisible) return nullptr;
        // +0x1A is set on the field being typed in.
        if (reinterpret_cast<const uint8_t*>(widget)[0x1A] && Navigator::IsA(widget, "TEWEditWidget")) return widget;
        if (!widget->childrenList || !widget->childrenList->list) return nullptr;
        for (uint32_t i = 0; i < widget->childrenList->count; i++) {
            if (TLBSWidget* found = FindEditing(widget->childrenList->list[i])) return found;
        }
        return nullptr;
    }

    void Destroy() {
        if (Board) Overlay::Detach(Board);
        Board = nullptr;
        BoardRoot = nullptr;
        Frame = nullptr;
        for (auto& row : Labels) for (TEWLabel*& label : row) label = nullptr;
        ShownPage = ShownRow = ShownColumn = -1;
    }

    void Build(TLBSWidget* root) {
        Destroy();
        Overlay::LoadImages();
        const auto width = static_cast<int16_t>(2 * Pad + Columns * Key + (Columns - 1) * Gap);
        const auto height = static_cast<int16_t>(2 * Pad + Rows * Key + (Rows - 1) * Gap + HintHeight);
        auto* board = Widget::Create<TLBSWidget>(CachedHost);
        if (!board) return;
        const auto left = static_cast<int16_t>((root->rect.right - root->rect.left - width) / 2);
        const auto top = static_cast<int16_t>(root->rect.bottom - root->rect.top - height - 140);
        board->rect = {left, top, static_cast<int16_t>(left + width), static_cast<int16_t>(top + height)};
        board->isVisible = false;
        Overlay::Attach(root, board);
        Navigator::AddBoard(board, width, height);
        for (int r = 0; r < Rows; r++) {
            for (int c = 0; c < Columns; c++) {
                const auto x = static_cast<int16_t>(Pad + c * (Key + Gap));
                const auto y = static_cast<int16_t>(Pad + r * (Key + Gap));
                if (TEWCustomPanelWidget* key = Navigator::AddBoard(board, Key, Key)) {
                    key->rect = {x, y, static_cast<int16_t>(x + Key), static_cast<int16_t>(y + Key)};
                }
                Labels[r][c] = Overlay::AddLabel(board, x, static_cast<int16_t>(y + Key / 2 - 8), Key, 3, L"");
                if (Labels[r][c]) Labels[r][c]->isInteractable = false;
            }
        }
        if (TEWLabel* hint = Overlay::AddLabel(board, Pad, static_cast<int16_t>(height - Pad - HintHeight + 8),
                                               static_cast<int16_t>(width - 2 * Pad), 3,
                                               L"A type   B delete   X space   Y enter   LB/RB letters   View close")) {
            hint->isInteractable = false;
        }
        Frame = Overlay::AddFrame(board);
        Board = board;
        BoardRoot = root;
    }

    void Show() {
        if (ShownPage != Page) {
            ShownPage = Page;
            for (int r = 0; r < Rows; r++) {
                for (int c = 0; c < Columns; c++) {
                    if (!Labels[r][c]) continue;
                    const wchar_t text[2] = {Pages[Page][r][c], 0};
                    Labels[r][c]->SetText(text);
                }
            }
        }
        if (ShownRow == Row && ShownColumn == Column) return;
        ShownRow = Row;
        ShownColumn = Column;
        const auto x = static_cast<int16_t>(Pad + Column * (Key + Gap));
        const auto y = static_cast<int16_t>(Pad + Row * (Key + Gap));
        Overlay::PlaceFrame(Frame, {static_cast<int16_t>(x - 4), static_cast<int16_t>(y - 4), static_cast<int16_t>(x + Key + 4),
                                    static_cast<int16_t>(y + Key + 4)});
        if (Frame) Frame->isVisible = true;
    }

    std::deque<std::vector<INPUT>> Steps;

    INPUT KeyInput(const WORD vk, const bool up) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        return input;
    }

    void Tap(const WORD key, const bool shift) {
        if (shift) Steps.push_back({KeyInput(VK_SHIFT, false)});
        Steps.push_back({KeyInput(key, false), KeyInput(key, true)});
        if (shift) Steps.push_back({KeyInput(VK_SHIFT, true)});
    }

    void Type(const wchar_t character) {
        const SHORT scan = VkKeyScanW(character);
        if (scan == -1 || (scan & 0x600)) return;
        Tap(static_cast<WORD>(scan & 0xFF), (scan & 0x100) != 0);
    }

    std::wstring Command;
    DWORD CommandUntil = 0;
    DWORD CloseAt = 0;
    DWORD QuietUntil = 0;

    void Pump(TLBSWidget* root) {
        if (!Command.empty() && Steps.empty()) {
            TLBSWidget* chat = FindWidgetOfClass(root, "TNTChatEditWidget", 1);
            if (auto* field = reinterpret_cast<TEWEditWidget*>(chat ? FindEditing(chat) : nullptr)) {
                field->SetText(Command.c_str());
                field->caretIndex = static_cast<int32_t>(Command.size());
                Tap(VK_RETURN, false);
                Command.clear();
                CloseAt = GetTickCount() + 250;
            } else if (static_cast<int32_t>(GetTickCount() - CommandUntil) > 0) {
                Command.clear();
            }
        }
        if (CloseAt && Steps.empty() && static_cast<int32_t>(GetTickCount() - CloseAt) >= 0) {
            CloseAt = 0;
            TLBSWidget* chat = FindWidgetOfClass(root, "TNTChatEditWidget", 1);
            if (chat && FindEditing(chat)) Tap(VK_ESCAPE, false);
            QuietUntil = GetTickCount() + 300;
        }
        if (Steps.empty()) return;
        std::vector<INPUT>& step = Steps.front();
        if (!step.empty()) SendInput(static_cast<UINT>(step.size()), step.data(), sizeof(INPUT));
        Steps.pop_front();
    }

    void SetOpen(TLBSWidget* root, bool open);

    void SendChat(const std::wstring& text) {
        if (text.empty() || !Command.empty()) return;
        if (IsOpen()) {
            Forced = false;
            SetOpen(BoardRoot, false);
        }
        Command = text;
        CommandUntil = GetTickCount() + 1000;
        Tap(VK_RETURN, false);
    }

    void SetOpen(TLBSWidget* root, const bool open) {
        if (open && (!Board || BoardRoot != root)) Build(root);
        if (!Board || Board->isVisible == open) return;
        Board->isVisible = open;
        if (open) Board->BubbleUp();
    }

    bool IsOpen() {
        return Board && Board->isVisible;
    }

    void Toggle() {
        if (IsOpen()) {
            Forced = false;
            ClosedFor = Editing;
        } else {
            Forced = true;
            ClosedFor = nullptr;
        }
    }

    bool Update(TLBSWidget* root, const XINPUT_GAMEPAD& pad, const bool active) {
        const DWORD now = GetTickCount();
        if (static_cast<int32_t>(now - NextCheck) >= 0) {
            NextCheck = now + 100;
            Editing = FindEditing(root);
            if (Editing != ClosedFor) ClosedFor = nullptr;
        }
        if (!active) Forced = false;
        const bool busy = !Command.empty() || CloseAt || static_cast<int32_t>(now - QuietUntil) < 0;
        const bool open = active && !busy && (Forced || (Editing && Editing != ClosedFor));
        SetOpen(root, open);
        const WORD buttons = pad.wButtons;
        const WORD pressed = buttons & ~Previous;
        Previous = buttons;
        if (HoldView && (buttons & XINPUT_GAMEPAD_BACK)) return true;
        HoldView = false;
        if (!open) return false;
        Show();

        if (pressed & XINPUT_GAMEPAD_BACK) {
            HoldView = true;
            Forced = false;
            ClosedFor = Editing;
            SetOpen(root, false);
            return true;
        }
        if (pressed & XINPUT_GAMEPAD_A) Type(Pages[Page][Row][Column]);
        if (pressed & XINPUT_GAMEPAD_X) Type(L' ');
        if (pressed & XINPUT_GAMEPAD_B) Tap(VK_BACK, false);
        if (pressed & XINPUT_GAMEPAD_Y) Tap(VK_RETURN, false);
        if (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) Page = (Page + 1) % PageCount;
        if (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER) Page = (Page + PageCount - 1) % PageCount;

        int dx = (buttons & XINPUT_GAMEPAD_DPAD_RIGHT ? 1 : 0) - (buttons & XINPUT_GAMEPAD_DPAD_LEFT ? 1 : 0);
        int dy = (buttons & XINPUT_GAMEPAD_DPAD_DOWN ? 1 : 0) - (buttons & XINPUT_GAMEPAD_DPAD_UP ? 1 : 0);
        constexpr SHORT Tilt = 16000;
        if (!dx && !dy) {
            dx = pad.sThumbLX > Tilt ? 1 : pad.sThumbLX < -Tilt ? -1 : 0;
            dy = pad.sThumbLY > Tilt ? -1 : pad.sThumbLY < -Tilt ? 1 : 0;
        }
        if (!dx && !dy) {
            Moving = false;
            return true;
        }
        if (!Moving || static_cast<int32_t>(now - NextRepeat) >= 0) {
            NextRepeat = now + (Moving ? 110 : 300);
            Moving = true;
            Column = (Column + dx + Columns) % Columns;
            Row = (Row + dy + Rows) % Rows;
        }
        return true;
    }
}
