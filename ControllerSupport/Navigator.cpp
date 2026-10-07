#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Overlay.h"
#include "Navigator.h"

namespace ControllerSupport::Navigator {
    int FrameImage = 0;
    uint16_t FrameSize = 0;

    bool IsA(const TLBSWidget* widget, const char* name) {
        uintptr_t vmt = widget ? widget->vTable : 0;
        const size_t length = strlen(name);
        while (vmt) {
            const auto* className = *reinterpret_cast<const uint8_t**>(vmt - 0x2C);
            if (className && className[0] == length && memcmp(className + 1, name, length) == 0) return true;
            const uintptr_t parent = *reinterpret_cast<const uintptr_t*>(vmt - 0x24);
            vmt = parent ? *reinterpret_cast<const uintptr_t*>(parent) : 0;
        }
        return false;
    }

    bool Selectable(const TLBSWidget* widget) {
        return widget->isInteractable && widget->rect.right > widget->rect.left && widget->rect.bottom > widget->rect.top
            && (IsA(widget, "TEWCustomButtonWidget") || IsA(widget, "TEWRollOverButtonWidget") || IsA(widget, "TEWEditWidget")
                || IsA(widget, "TNTIconWidget"));
    }

    void Collect(TLBSWidget* widget, const int16_t x, const int16_t y, std::vector<Target>& out);

    void CollectWindow(TLBSWidget* window, std::vector<Target>& out) {
        if (Selectable(window)) out.push_back({window, window->rect});
        Collect(window, window->rect.left, window->rect.top, out);
    }

    // Lists draw their own rows: +0x72 rows shown, +0x86 row height, +0x98 the entries, +0xA0 the first one shown.
    // +0x76 is 0 on text-only lists. TEWListView rows (+0xC8 selected) are clicked through their window.
    void CollectRows(TLBSWidget* list, const Rect& rect, std::vector<Target>& out) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(list);
        if (!bytes[0x76] && !IsA(list, "TEWListView")) return;
        const uint16_t shown = *reinterpret_cast<const uint16_t*>(bytes + 0x72);
        const uint16_t height = *reinterpret_cast<const uint16_t*>(bytes + 0x86);
        const auto* entries = *reinterpret_cast<const uint8_t* const*>(bytes + 0x98);
        const int32_t first = *reinterpret_cast<const int32_t*>(bytes + 0xA0);
        if (!entries || !height || !IsA(reinterpret_cast<const TLBSWidget*>(entries), "TStringList")) return;
        const int32_t count = *reinterpret_cast<const int32_t*>(entries + 0x14);
        const int32_t rows = std::min<int32_t>(shown, count - std::clamp<int32_t>(first, 0, count));
        for (int32_t row = 0; row < rows; row++) {
            const auto top = static_cast<int16_t>(rect.top + row * height);
            if (top + height > rect.bottom + 2) break;
            out.push_back({list, {rect.left, top, rect.right, static_cast<int16_t>(top + height)}, row});
        }
    }

    void Collect(TLBSWidget* widget, const int16_t x, const int16_t y, std::vector<Target>& out) {
        if (!widget->childrenList || !widget->childrenList->list) return;
        for (uint32_t i = 0; i < widget->childrenList->count; i++) {
            TLBSWidget* child = widget->childrenList->list[i];
            if (!child || !child->isVisible) continue;
            const Rect rect{static_cast<int16_t>(x + child->rect.left), static_cast<int16_t>(y + child->rect.top),
                            static_cast<int16_t>(x + child->rect.right), static_cast<int16_t>(y + child->rect.bottom)};
            if (Selectable(child)) out.push_back({child, rect});
            if (IsA(child, "TEWStringListView")) CollectRows(child, rect, out);
            Collect(child, rect.left, rect.top, out);
        }
    }

    int32_t Entries(const TLBSWidget* list) {
        const auto* entries = *reinterpret_cast<const uint8_t* const*>(reinterpret_cast<const uint8_t*>(list) + 0x98);
        return entries ? *reinterpret_cast<const int32_t*>(entries + 0x14) : -1;
    }

    void MergeColumns(std::vector<Target>& targets) {
        std::vector<bool> drop(targets.size());
        for (size_t i = 0; i < targets.size(); i++) {
            Target& row = targets[i];
            if (row.row < 0 || drop[i]) continue;
            for (size_t j = i + 1; j < targets.size(); j++) {
                const Target& other = targets[j];
                if (other.row < 0 || drop[j] || other.widget->parent != row.widget->parent || other.rect.top != row.rect.top
                    || other.rect.bottom != row.rect.bottom || Entries(other.widget) != Entries(row.widget)) {
                    continue;
                }
                row.rect.left = std::min(row.rect.left, other.rect.left);
                row.rect.right = std::max(row.rect.right, other.rect.right);
                drop[j] = true;
            }
        }
        size_t kept = 0;
        for (size_t i = 0; i < targets.size(); i++) {
            if (!drop[i]) targets[kept++] = targets[i];
        }
        targets.resize(kept);
    }

    // A label or marker inside a button is a target of its own, keep only the button around it.
    void DropNested(std::vector<Target>& targets) {
        const auto area = [](const Rect& r) { return (r.right - r.left) * (r.bottom - r.top); };
        const auto inside = [](const Rect& inner, const Rect& outer) {
            return inner.left >= outer.left - 2 && inner.top >= outer.top - 2 && inner.right <= outer.right + 2
                && inner.bottom <= outer.bottom + 2;
        };
        const auto close = [&](const Rect& inner, const Rect& outer) {
            return inside(inner, outer) && area(outer) <= area(inner) * 4;
        };
        std::vector<bool> drop(targets.size());
        // List rows stay, a hover widget over a row comes and goes with the selection.
        for (size_t i = 0; i < targets.size(); i++) {
            if (targets[i].row >= 0) continue;
            for (size_t j = 0; j < targets.size() && !drop[i]; j++) {
                if (i == j || drop[j]) continue;
                const Rect& mine = targets[i].rect;
                const Rect& other = targets[j].rect;
                if (targets[j].row >= 0) drop[i] = close(mine, other) || close(other, mine);
                else if (close(mine, other)) drop[i] = area(mine) < area(other) || j < i;
            }
        }
        size_t kept = 0;
        for (size_t i = 0; i < targets.size(); i++) {
            if (!drop[i]) targets[kept++] = targets[i];
        }
        targets.resize(kept);
    }

    // Only a window that just appeared with the focus, not one already on screen that the focus falls back to.
    TLBSWidget* FocusedWindow(TLBSWidget* root) {
        TLBSWidget* focus = root->someChild;
        const bool valid = focus && focus->isVisible && focus != FindNaviWidget(root) && root->childrenList
            && root->childrenList->index_of(focus) >= 0;
        const bool appeared = valid && !ShownBefore.empty() && std::ranges::find(ShownBefore, focus) == ShownBefore.end();
        ShownBefore.clear();
        if (root->childrenList && root->childrenList->list) {
            for (uint32_t i = 0; i < root->childrenList->count; i++) {
                TLBSWidget* child = root->childrenList->list[i];
                if (child && child->isVisible) ShownBefore.push_back(child);
            }
        }
        if (!valid) return nullptr;
        return focus == Window || appeared ? focus : nullptr;
    }

    bool Ours(const TLBSWidget* widget) {
        for (const Overlay::Panel& panel : Overlay::Panels) {
            if (panel.container == widget) return true;
        }
        for (const TEWCustomPanelWidget* piece : Frame) {
            if (piece == widget) return true;
        }
        return widget == Overlay::PaletteContainer || widget == Overlay::EditBoard || widget == Overlay::DragLabel;
    }

    std::vector<TLBSWidget*> OpenWindows(TLBSWidget* root) {
        std::vector<TLBSWidget*> windows;
        if (!root->childrenList || !root->childrenList->list) return windows;
        // Everything listed before TNaviWidget is drawn under it and can't be seen.
        const int32_t navi = root->childrenList->index_of(FindNaviWidget(root));
        std::vector<Target> found;
        for (uint32_t i = navi < 0 ? 0 : navi + 1; i < root->childrenList->count; i++) {
            TLBSWidget* child = root->childrenList->list[i];
            if (!child || !child->isVisible || Ours(child) || (child->rect.left < 0 && child->rect.right <= 0)) continue;
            found.clear();
            CollectWindow(child, found);
            if (!found.empty()) windows.push_back(child);
        }
        std::ranges::stable_sort(windows, [](const TLBSWidget* a, const TLBSWidget* b) {
            const int ax = a->rect.left + a->rect.right, bx = b->rect.left + b->rect.right;
            return ax != bx ? ax < bx : a->rect.top + a->rect.bottom < b->rect.top + b->rect.bottom;
        });
        return windows;
    }

    POINT Centre(const Rect& rect) {
        return {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    }

    // The bottom row's left button, which is confirm in the game's message boxes.
    // Windows where the first thing to pick is at the top, not the confirm button.
    constexpr const char* StartAtTop[] = {"TCharacterSelectWidget"};

    const Target* Primary() {
        const Target* best = nullptr;
        for (const char* name : StartAtTop) {
            if (!Window || !IsA(Window, name)) continue;
            for (const Target& target : Targets) {
                if (!best || target.rect.top < best->rect.top - 4
                    || (std::abs(target.rect.top - best->rect.top) <= 4 && target.rect.left < best->rect.left)) {
                    best = &target;
                }
            }
            if (best) return best;
        }
        for (const Target& target : Targets) {
            if (target.row == 0 && (!best || target.rect.left < best->rect.left)) best = &target;
        }
        if (best) return best;
        for (const Target& target : Targets) {
            if (!IsA(target.widget, "TEWCustomButtonWidget")) continue;
            if (!best || target.rect.bottom > best->rect.bottom + 4
                || (std::abs(target.rect.bottom - best->rect.bottom) <= 4 && target.rect.left < best->rect.left)) {
                best = &target;
            }
        }
        return best ? best : Targets.empty() ? nullptr : &Targets.front();
    }

    const Target* Find(const TLBSWidget* widget) {
        for (const Target& target : Targets) {
            if (target.widget == widget && target.row == SelectedRow) return &target;
        }
        return nullptr;
    }

    const Target* Step(const Target& from, const float dx, const float dy) {
        const POINT origin = Centre(from.rect);
        for (const float cone : {1.75f, 1.0e9f}) {
            const Target* best = nullptr;
            float bestScore = 0.0f;
            for (const Target& target : Targets) {
                if (&target == &from) continue;
                const POINT centre = Centre(target.rect);
                const float vx = static_cast<float>(centre.x - origin.x), vy = static_cast<float>(centre.y - origin.y);
                const float along = vx * dx + vy * dy;
                const float across = std::abs(vx * dy - vy * dx);
                if (along <= 0.0f || across > along * cone) continue;
                const float score = along + 2.0f * across;
                if (!best || score < bestScore) {
                    best = &target;
                    bestScore = score;
                }
            }
            if (best) return best;
        }
        return nullptr;
    }

    void PostMouse(const UINT message, const WPARAM keys, const Rect& rect) {
        HWND window = FindGameWindow();
        if (!window) return;
        const POINT at = Centre(rect);
        PostMessageA(window, message, keys, MAKELPARAM(at.x, at.y));
    }

    TEWCustomPanelWidget* AddPiece(TLBSWidget* root, const AtlasFrame& frame) {
        auto* piece = Widget::Create<TEWCustomPanelWidget>(CachedHost);
        if (!piece) return nullptr;
        delete[] piece->imageData.atlasFrames;
        piece->imageData.imageName = FrameImage;
        piece->imageData.imageWidth = static_cast<int16_t>(FrameSize);
        piece->imageData.imageHeight = static_cast<int16_t>(FrameSize);
        piece->imageData.frameCount = 1;
        piece->imageData.atlasFrames = new AtlasFrame[1]{frame};
        piece->drawMode = 0;
        piece->isMoveable = false;
        piece->isInteractable = false;
        piece->isVisible = false;
        Overlay::Attach(root, piece);
        return piece;
    }

    // Eight pieces around the target, none over it, so posted clicks land on the target itself.
    void PlaceFrame(TLBSWidget* root, const Rect* around) {
        if (Pointing) around = nullptr;
        if (!FrameImage) {
            uint16_t height = 0;
            FrameImage = LoadUiImageResource("FRAME", FrameSize, height);
            if (!FrameImage) return;
        }
        constexpr int16_t C = 8;
        const int16_t S = static_cast<int16_t>(FrameSize);
        const AtlasFrame frames[8] = {{0, 0, C, C}, {C, 0, static_cast<int16_t>(S - 2 * C), C}, {static_cast<int16_t>(S - C), 0, C, C},
                                      {static_cast<int16_t>(S - C), C, C, static_cast<int16_t>(S - 2 * C)},
                                      {static_cast<int16_t>(S - C), static_cast<int16_t>(S - C), C, C},
                                      {C, static_cast<int16_t>(S - C), static_cast<int16_t>(S - 2 * C), C},
                                      {0, static_cast<int16_t>(S - C), C, C}, {0, C, C, static_cast<int16_t>(S - 2 * C)}};
        if (FrameRoot != root) Destroy();
        FrameRoot = root;
        for (int i = 0; i < 8; i++) {
            if (!Frame[i]) Frame[i] = AddPiece(root, frames[i]);
        }
        if (!around) {
            for (TEWCustomPanelWidget* piece : Frame) {
                if (piece && piece->isVisible) piece->isVisible = false;
            }
            FrameAround = {};
            return;
        }
        const Rect& r = *around;
        if (r.left != FrameAround.left || r.top != FrameAround.top || r.right != FrameAround.right || r.bottom != FrameAround.bottom) {
            FrameAround = r;
            const int16_t l = static_cast<int16_t>(r.left - C), t = static_cast<int16_t>(r.top - C);
            const int16_t rr = r.right, b = r.bottom;
            const Rect rects[8] = {{l, t, r.left, r.top}, {r.left, t, rr, r.top}, {rr, t, static_cast<int16_t>(rr + C), r.top},
                                   {rr, r.top, static_cast<int16_t>(rr + C), b}, {rr, b, static_cast<int16_t>(rr + C), static_cast<int16_t>(b + C)},
                                   {r.left, b, rr, static_cast<int16_t>(b + C)}, {l, b, r.left, static_cast<int16_t>(b + C)},
                                   {l, r.top, r.left, b}};
            for (int i = 0; i < 8; i++) {
                if (Frame[i]) Frame[i]->rect = rects[i];
            }
        }
        for (TEWCustomPanelWidget* piece : Frame) {
            if (!piece) continue;
            if (!piece->isVisible) piece->isVisible = true;
            if (root->childrenList->index_of(piece) < root->childrenList->index_of(Window)) piece->BubbleUp();
        }
    }

    void KeepOnTop(TLBSWidget* root, TLBSWidget* widget) {
        if (root->childrenList->index_of(widget) < root->childrenList->index_of(Window)) widget->BubbleUp();
    }

    // The real cursor, saved first so it can go back when UI mode ends.
    void MoveCursor(const Rect& rect) {
        HWND window = FindGameWindow();
        if (!window) return;
        if (!CursorSaved) CursorSaved = GetCursorPos(&SavedCursor) != 0;
        POINT at = Centre(rect);
        ClientToScreen(window, &at);
        SetCursorPos(at.x, at.y);
        LastCursor = at;
        if (Holding) PostMouse(WM_MOUSEMOVE, MK_LBUTTON, rect);
    }

    void RestoreCursor() {
        if (!CursorSaved) return;
        SetCursorPos(SavedCursor.x, SavedCursor.y);
        LastCursor = SavedCursor;
        CursorSaved = false;
    }

    struct Hint {
        const char* glyphs[2];
        const wchar_t* text;
        int16_t width;
    };
    constexpr Hint Hints[] = {
        {{"XBOX_DPAD_SMALL", "XBOX_STICK_L_SMALL"}, L"Move", 32}, {{"XBOX_STICK_R_SMALL", nullptr}, L"Cursor", 36},
        {{"XBOX_BUTTON_COLOR_A_SMALL", nullptr}, L"Left click", 51}, {{"XBOX_BUTTON_COLOR_B_SMALL", nullptr}, L"Escape", 40},
        {{"XBOX_BUTTON_COLOR_X_SMALL", nullptr}, L"Right click", 56}, {{"XBOX_BUTTON_COLOR_Y_SMALL", nullptr}, L"Enter", 32},
        {{"XBOX_LB_SMALL", "XBOX_RB_SMALL"}, L"Switch", 39}, {{"XBOX_BUTTON_MENU_SMALL", nullptr}, L"Keyboard", 54},
        {{"XBOX_BUTTON_VIEW_SMALL", nullptr}, L"Exit", 22},
    };
    Overlay::Image HintGlyphs[std::size(Hints)][2];

    TEWCustomPanelWidget* AddBoard(TLBSWidget* parent, const int16_t width, const int16_t height) {
        auto* board = Widget::Create<TEWCustomPanelWidget>(CachedHost);
        if (!board || !Overlay::SlotImage.id) return board;
        constexpr int16_t C = 10;
        const int16_t S = static_cast<int16_t>(Overlay::SlotImage.width);
        const int16_t M = static_cast<int16_t>(S - 2 * C);
        delete[] board->imageData.atlasFrames;
        board->imageData.imageName = Overlay::SlotImage.id;
        board->imageData.imageWidth = S;
        board->imageData.imageHeight = static_cast<int16_t>(Overlay::SlotImage.height);
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
        board->isInteractable = false;
        board->color = Color(215, 255, 255, 255);
        board->rect = {0, 0, width, height};
        Overlay::Attach(parent, board);
        return board;
    }

    void BuildLegend(TLBSWidget* root) {
        Overlay::LoadImages();
        constexpr int16_t Glyph = 28, Gap = 6, Space = 16, Pad = 12, Height = 40;
        int16_t width = Pad;
        int16_t labels[std::size(Hints)]{};
        for (size_t i = 0; i < std::size(Hints); i++) {
            for (int g = 0; g < 2; g++) {
                if (Hints[i].glyphs[g] && !HintGlyphs[i][g].id) HintGlyphs[i][g] = Overlay::Load(Hints[i].glyphs[g]);
                if (Hints[i].glyphs[g]) width = static_cast<int16_t>(width + Glyph);
            }
            labels[i] = Hints[i].width;
            width = static_cast<int16_t>(width + Gap + labels[i] + Space);
        }
        width = static_cast<int16_t>(width - Space + Pad);
        auto* legend = Widget::Create<TLBSWidget>(CachedHost);
        if (!legend) return;
        const int16_t left = static_cast<int16_t>((root->rect.right - root->rect.left - width) / 2);
        const int16_t top = static_cast<int16_t>(root->rect.bottom - root->rect.top - Height - 70);
        legend->rect = {left, top, static_cast<int16_t>(left + width), static_cast<int16_t>(top + Height)};
        legend->isVisible = false;
        Overlay::Attach(root, legend);
        AddBoard(legend, width, Height);
        int16_t x = Pad;
        for (size_t i = 0; i < std::size(Hints); i++) {
            for (int g = 0; g < 2; g++) {
                if (!Hints[i].glyphs[g]) continue;
                Overlay::AddImage(legend, HintGlyphs[i][g], x, static_cast<int16_t>((Height - Glyph) / 2));
                x = static_cast<int16_t>(x + Glyph);
            }
            x = static_cast<int16_t>(x + Gap);
            if (TEWLabel* label = Overlay::AddLabel(legend, x, 14, labels[i], 1, Hints[i].text)) label->isInteractable = false;
            x = static_cast<int16_t>(x + labels[i] + Space);
        }
        Legend = legend;
    }

    // The login and character screens have buttons where the in-game spot is, so the legend sits higher there.
    void ShowLegend(TLBSWidget* root, const bool shown, const bool preGame) {
        if (shown && !Legend) BuildLegend(root);
        if (!Legend) return;
        const auto height = static_cast<int16_t>(Legend->rect.bottom - Legend->rect.top);
        auto top = static_cast<int16_t>(root->rect.bottom - root->rect.top - height - (preGame ? 120 : 70));
        // Never over the window being used, like the NPC conversation at the bottom of the screen.
        if (Window && Window->rect.bottom > top && Window->rect.top < top + height && Window->rect.left < Legend->rect.right
            && Window->rect.right > Legend->rect.left && Window->rect.top - height - 8 >= 0) {
            top = static_cast<int16_t>(Window->rect.top - height - 8);
        }
        const auto width = static_cast<int16_t>(Legend->rect.right - Legend->rect.left);
        const auto left = static_cast<int16_t>((root->rect.right - root->rect.left - width) / 2);
        if (Legend->rect.left != left) {
            Legend->rect.left = left;
            Legend->rect.right = static_cast<int16_t>(left + width);
        }
        if (Legend->rect.top != top) {
            Legend->rect.top = top;
            Legend->rect.bottom = static_cast<int16_t>(top + height);
        }
        if (Legend->isVisible != shown) Legend->isVisible = shown;
        if (shown) KeepOnTop(root, Legend);
    }

    void Select(TLBSWidget* root, const Target* target) {
        Selected = target ? target->widget : nullptr;
        SelectedRow = target ? target->row : -1;
        SelectedRect = target ? target->rect : Rect{};
        if (target && Holding) MoveCursor(target->rect);
        else if (target) PostMouse(WM_MOUSEMOVE, 0, target->rect);
        PlaceFrame(root, target ? &target->rect : nullptr);
    }

    bool Update(TLBSWidget* root, const XINPUT_GAMEPAD& pad, const bool always) {
        const bool viewDown = (pad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
        const bool viewPressed = viewDown && !ViewWasDown;
        ViewWasDown = viewDown;
        TLBSWidget* window = FocusedWindow(root);
        if (viewPressed && (Manual || Window) && !always) {
            Close();
            PreviousButtons = pad.wButtons;
            return true;
        }
        if (!window && (viewPressed || Manual || always)) {
            const std::vector<TLBSWidget*> windows = OpenWindows(root);
            if (Manual && std::ranges::find(windows, Window) != windows.end()) {
                window = Window;
            } else if (!windows.empty()) {
                const auto focused = std::ranges::find(windows, root->someChild);
                window = focused != windows.end() ? *focused : windows.back();
            }
            Manual = window != nullptr;
        }
        if (!window) {
            Close();
            return false;
        }
        ShowLegend(root, true, always);
        Targets.clear();
        CollectWindow(window, Targets);
        MergeColumns(Targets);
        DropNested(Targets);
        if (window != Window) {
            Window = window;
            Holding = false;
            Select(root, Primary());
            PreviousButtons = pad.wButtons;
            WaitNeutral = true;
            return true;
        }
        const Target* current = Find(Selected);
        if (!current) {
            current = Primary();
            Select(root, current);
        } else if (current->rect.left != SelectedRect.left || current->rect.top != SelectedRect.top
                   || current->rect.right != SelectedRect.right || current->rect.bottom != SelectedRect.bottom) {
            Select(root, current);
        } else {
            PlaceFrame(root, &current->rect);
        }

        const WORD buttons = pad.wButtons;
        const WORD pressed = buttons & ~PreviousButtons;
        const WORD released = PreviousButtons & ~buttons;
        PreviousButtons = buttons;

        float dx = 0.0f, dy = 0.0f;
        if (buttons & XINPUT_GAMEPAD_DPAD_LEFT) dx = -1.0f;
        if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) dx = 1.0f;
        if (buttons & XINPUT_GAMEPAD_DPAD_UP) dy = -1.0f;
        if (buttons & XINPUT_GAMEPAD_DPAD_DOWN) dy = 1.0f;
        const float sx = pad.sThumbLX, sy = pad.sThumbLY;
        const float magnitude = std::sqrt(sx * sx + sy * sy);
        if (dx == 0.0f && dy == 0.0f && magnitude > LeftStickDeadzone * 2.0f) {
            dx = sx / magnitude;
            dy = -sy / magnitude;
        } else if (dx != 0.0f && dy != 0.0f) {
            dx *= 0.7071f;
            dy *= 0.7071f;
        }
        // The press that opened the window may still be held.
        if (WaitNeutral) {
            if (dx != 0.0f || dy != 0.0f) {
                dx = dy = 0.0f;
            } else {
                WaitNeutral = false;
            }
        }
        const DWORD tick = GetTickCount();
        const float seconds = PointerTick ? std::min(static_cast<float>(tick - PointerTick) / 1000.0f, 0.1f) : 0.0f;
        PointerTick = tick;
        float px, py;
        ReadStick(pad.sThumbRX, pad.sThumbRY, RightStickDeadzone, px, py);
        if (px != 0.0f || py != 0.0f) {
            if (!Pointing) {
                const POINT start = current ? Centre(current->rect) : POINT{(root->rect.right - root->rect.left) / 2, (root->rect.bottom - root->rect.top) / 2};
                PointerX = static_cast<float>(start.x);
                PointerY = static_cast<float>(start.y);
                Pointing = true;
                PlaceFrame(root, nullptr);
            }
            constexpr float Speed = 1100.0f;
            PointerX = std::clamp(PointerX + px * Speed * seconds, 0.0f, static_cast<float>(root->rect.right - root->rect.left - 1));
            PointerY = std::clamp(PointerY - py * Speed * seconds, 0.0f, static_cast<float>(root->rect.bottom - root->rect.top - 1));
        } else if (Pointing && (dx != 0.0f || dy != 0.0f)) {
            Pointing = false;
            if (current) PlaceFrame(root, &current->rect);
        }
        if (Pointing && (static_cast<LONG>(PointerX) != PointerShown.x || static_cast<LONG>(PointerY) != PointerShown.y)) {
            PointerShown = {static_cast<LONG>(PointerX), static_cast<LONG>(PointerY)};
            MoveCursor({static_cast<int16_t>(PointerX), static_cast<int16_t>(PointerY), static_cast<int16_t>(PointerX + 1),
                        static_cast<int16_t>(PointerY + 1)});
        }
        const Rect pointerRect{static_cast<int16_t>(PointerX), static_cast<int16_t>(PointerY),
                               static_cast<int16_t>(PointerX + 1), static_cast<int16_t>(PointerY + 1)};
        const Rect* aim = Pointing ? &pointerRect : current ? &current->rect : nullptr;
        if (Pointing) dx = dy = 0.0f;

        const bool fresh = (pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) != 0;
        if ((dx != 0.0f || dy != 0.0f) && current && (fresh || static_cast<int32_t>(GetTickCount() - NextRepeat) >= 0)) {
            if (const Target* next = Step(*current, dx, dy)) {
                Select(root, next);
                current = next;
            }
            NextRepeat = GetTickCount() + (fresh ? 350 : 160);
        } else if (dx == 0.0f && dy == 0.0f) {
            NextRepeat = 0;
        }

        // The game checks the real button between messages, so a tap sends down and up together.
        if (aim && (pressed & XINPUT_GAMEPAD_A)) {
            PressPending = true;
            PressTick = tick;
            PressRect = *aim;
        }
        if (PressPending && (buttons & XINPUT_GAMEPAD_A) && tick - PressTick >= 250) {
            PressPending = false;
            Holding = true;
            MoveCursor(PressRect);
            PostMouse(WM_LBUTTONDOWN, MK_LBUTTON, PressRect);
        }
        if (released & XINPUT_GAMEPAD_A) {
            if (PressPending) {
                const POINT at = Centre(PressRect), last = Centre(LastClickRect);
                const bool twice = LastClickTick && tick - LastClickTick <= GetDoubleClickTime()
                    && std::abs(at.x - last.x) <= 4 && std::abs(at.y - last.y) <= 4;
                HWND window = FindGameWindow();
                const bool doubleClicks = window && (GetClassLongA(window, GCL_STYLE) & CS_DBLCLKS) != 0;
                PostMouse(WM_MOUSEMOVE, 0, PressRect);
                PostMouse(twice && doubleClicks ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, MK_LBUTTON, PressRect);
                PostMouse(WM_LBUTTONUP, 0, PressRect);
                LastClickTick = twice ? 0 : tick;
                LastClickRect = PressRect;
                PressPending = false;
            } else if (Holding) {
                PostMouse(WM_LBUTTONUP, 0, aim ? *aim : PressRect);
                Holding = false;
                if (!Pointing) RestoreCursor();
            }
        }
        if (aim && (pressed & XINPUT_GAMEPAD_X)) {
            PostMouse(WM_MOUSEMOVE, 0, *aim);
            PostMouse(WM_RBUTTONDOWN, MK_RBUTTON, *aim);
            PostMouse(WM_RBUTTONUP, 0, *aim);
        }
        if (pressed & XINPUT_GAMEPAD_B) PressKey(VK_ESCAPE);
        if (pressed & XINPUT_GAMEPAD_Y) PressKey(VK_RETURN);
        if (pressed & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
            const std::vector<TLBSWidget*> windows = OpenWindows(root);
            if (windows.size() > 1) {
                const auto at = std::ranges::find(windows, Window);
                const ptrdiff_t index = at == windows.end() ? 0 : at - windows.begin();
                const ptrdiff_t step = (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1 : -1;
                Window = windows[(index + step + static_cast<ptrdiff_t>(windows.size())) % static_cast<ptrdiff_t>(windows.size())];
                Selected = nullptr;
                Manual = true;
            }
        }
        return true;
    }

    void Close(const bool restoreCursor) {
        if (Holding && Selected) PostMouse(WM_LBUTTONUP, 0, SelectedRect);
        Holding = false;
        Manual = false;
        Pointing = false;
        PointerTick = 0;
        PointerShown = {-1, -1};
        PressPending = false;
        if (Legend && Legend->isVisible) Legend->isVisible = false;
        if (restoreCursor) RestoreCursor();
        CursorSaved = false;
        Window = nullptr;
        Selected = nullptr;
        Targets.clear();
        for (TEWCustomPanelWidget* piece : Frame) {
            if (piece && piece->isVisible) piece->isVisible = false;
        }
        FrameAround = {};
    }

    void Destroy() {
        for (TEWCustomPanelWidget*& piece : Frame) {
            if (piece) Overlay::Detach(piece);
            piece = nullptr;
        }
        PointerShown = {-1, -1};
        if (Legend) Overlay::Detach(Legend);
        Legend = nullptr;
        FrameRoot = nullptr;
        FrameAround = {};
    }
}
