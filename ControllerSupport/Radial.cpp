#include "Game.h"
#include "Overlay.h"
#include "Navigator.h"
#include "Radial.h"
#include "Clients.h"
#include "Keyboard.h"

namespace ControllerSupport::Radial {
    // The game's interface atlas, the task bar icons have their hover look one row below.
    constexpr int32_t Atlas = 0x5F000030;
    constexpr DWORD HoldTime = 300;
    constexpr float IconScale = 1.6f;

    enum Where { TaskBar, TaskBarText, RootButton, MiniMap, Key, Motion };

    struct Entry {
        const wchar_t* name;
        const wchar_t* label;
        Where where;
        AtlasFrame frame;
        AtlasFrame hover;
        int value;
    };

    constexpr Entry Entries[] = {
        {L"Inventory", L"", TaskBar, {60, 0, 30, 30}, {60, 30, 30, 30}, 0},
        {L"Skills", L"", TaskBar, {30, 0, 30, 30}, {30, 30, 30, 30}, 0},
        {L"Character", L"", TaskBar, {0, 0, 30, 30}, {0, 30, 30, 30}, 0},
        {L"Quests", L"", TaskBar, {120, 0, 30, 30}, {120, 30, 30, 30}, 0},
        {L"Map", L"", MiniMap, {404, 93, 31, 18}, {436, 93, 31, 18}, 0},
        {L"Friends", L"", TaskBar, {180, 0, 30, 30}, {180, 30, 30, 30}, 0},
        {L"Family", L"Family", Key, {}, {}, 'J'},
        {L"Settings", L"Settings", TaskBarText, {}, {}, 0},
        {L"NosMall", L"", RootButton, {90, 0, 30, 30}, {90, 30, 30, 30}, 0},
        {L"NosBazaar", L"", Motion, {}, {}, 12},
        {L"Raid List", L"", Motion, {}, {}, 11},
        {L"Time Circle", L"", Motion, {}, {}, 10},
        {L"Celestial Spire Catacombs", L"", Motion, {}, {}, 13},
        {L"Mini-land", L"", TaskBar, {150, 0, 30, 30}, {150, 30, 30, 30}, 0},
        {L"Server selection", L"Server", TaskBarText, {}, {}, 1},
    };
    constexpr int Count = static_cast<int>(std::size(Entries));
    constexpr int ServerSelection = Count - 1;
    constexpr AtlasFrame MenuButton{210, 0, 30, 30};

    struct Item {
        std::wstring name;
        std::wstring label;
        AtlasFrame frame{};
        AtlasFrame hover{};
        bool marked = false;
        int16_t motion = 0;
    };

    // One ring of choices, opened by holding its button.
    struct Ring {
        WORD button;
        TLBSWidget* menu = nullptr;
        TLBSWidget* root = nullptr;
        std::vector<Item> items;
        std::vector<TEWCustomPanelWidget*> icons;
        std::vector<TEWCustomPanelWidget*> boards;
        std::vector<TNTTimeAniIcon*> motions;
        TEWCustomPanelWidget* nameBoard = nullptr;
        TEWCustomPanelWidget* highlight = nullptr;
        TEWLabel* name = nullptr;
        int16_t size = 0;
        int page = 0;
        int first = 0;
        int count = 0;
        WORD previous = 0;
        int16_t builtWidth = 0;
        int16_t builtHeight = 0;
        bool held = false;
        DWORD downTick = 0;
        int choice = -1;
        int shown = -2;
        DWORD lastTilt = 0;
    };

    Ring GameRing{XINPUT_GAMEPAD_BACK};
    Ring ClientRing{XINPUT_GAMEPAD_START};
    std::vector<Clients::Client> ClientList;

    // Sticks still tilted when a radial closes or the client gets focus are ignored until let go, a second at most.
    bool SettleLeft = false;
    bool SettleRight = false;
    DWORD SettleUntil = 0;

    struct MotionClick {
        bool active = false;
        int step = 0;
        LPARAM at = 0;
        LPARAM back = 0;
    };
    MotionClick Using;

    bool CloseTaskBar = false;
    int CloseTicks = 0;
    int Confirm = -1;
    DWORD BarsHiddenUntil = 0;
    DWORD ConfirmUntil = 0;

    bool SameFrame(const TLBSWidget* widget, const AtlasFrame& frame) {
        const auto* control = reinterpret_cast<const TEWControlWidget*>(widget);
        if (control->imageData.imageName != Atlas || control->imageData.frameCount < 1 || !control->imageData.atlasFrames) return false;
        const AtlasFrame& own = control->imageData.atlasFrames[0];
        return own.topLeftX == frame.topLeftX && own.topLeftY == frame.topLeftY && own.width == frame.width;
    }

    TLBSWidget* FindButton(const TLBSWidget* parent, const AtlasFrame& frame) {
        if (!parent || !parent->childrenList || !parent->childrenList->list) return nullptr;
        for (uint32_t i = 0; i < parent->childrenList->count; i++) {
            TLBSWidget* child = parent->childrenList->list[i];
            if (child && child->isVisible && Navigator::IsA(child, "TEWCustomButtonWidget") && SameFrame(child, frame)) return child;
        }
        return nullptr;
    }

    TLBSWidget* TextButton(const TLBSWidget* taskBar, int index) {
        if (!taskBar->childrenList || !taskBar->childrenList->list) return nullptr;
        for (uint32_t i = 0; i < taskBar->childrenList->count; i++) {
            TLBSWidget* child = taskBar->childrenList->list[i];
            if (!child || !child->isVisible || !IsClass(child, "TEWGraphicButtonWidget")) continue;
            if (index-- == 0) return child;
        }
        return nullptr;
    }

    void ClickAt(const int x, const int y) {
        HWND window = FindGameWindow();
        if (!window) return;
        PostMessageA(window, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        PostMessageA(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        PostMessageA(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    }

    void Click(const TLBSWidget* parent, const TLBSWidget* button) {
        if (!button) return;
        ClickAt((parent ? parent->rect.left : 0) + (button->rect.left + button->rect.right) / 2,
                (parent ? parent->rect.top : 0) + (button->rect.top + button->rect.bottom) / 2);
    }

    TLBSWidget* YesButton(const TLBSWidget* box) {
        if (!box->childrenList || !box->childrenList->list) return nullptr;
        TLBSWidget* yes = nullptr;
        for (uint32_t i = 0; i < box->childrenList->count; i++) {
            TLBSWidget* child = box->childrenList->list[i];
            if (child && child->isVisible && IsClass(child, "TEWGraphicButtonWidget") && (!yes || child->rect.left < yes->rect.left)) yes = child;
        }
        return yes;
    }

    // The game handles all posted clicks before it draws again, so the menu and the question
    // are opened and answered without being seen.
    void Open(TLBSWidget* root, const int index) {
        const Entry& entry = Entries[index];
        if (entry.where == Key) {
            PressKey(static_cast<UINT>(entry.value));
            return;
        }
        if (entry.where == Motion) return;
        if (entry.where == RootButton || entry.where == MiniMap) {
            TLBSWidget* parent = entry.where == MiniMap ? FindWidgetOfClass(root, "TNTMiniMapWidget", 1) : nullptr;
            Click(parent, FindButton(parent ? parent : root, entry.frame));
            return;
        }
        TLBSWidget* taskBar = FindWidgetOfClass(root, "TNTTaskBarWidget", 1);
        if (!taskBar) return;
        TLBSWidget* button = entry.where == TaskBarText ? TextButton(taskBar, entry.value) : FindButton(taskBar, entry.frame);
        if (!button) return;
        TLBSWidget* box = FindWidgetOfClass(root, "TNTMessageBoxWidget", 1);
        if (index == ServerSelection && (!box || box->isVisible)) return;
        if (!taskBar->isVisible) {
            Click(nullptr, FindButton(root, MenuButton));
            CloseTaskBar = true;
            CloseTicks = 2;
        }
        Click(taskBar, button);
        if (index != ServerSelection) return;
        // Placed where it showed last, or centred before it ever showed.
        const TLBSWidget* yes = YesButton(box);
        if (!yes) return;
        const int width = box->rect.right - box->rect.left, height = box->rect.bottom - box->rect.top;
        const int left = box->rect.left ? box->rect.left : (root->rect.right - root->rect.left - width) / 2;
        const int top = box->rect.left ? box->rect.top : (root->rect.bottom - root->rect.top - height) / 2;
        ClickAt(left + (yes->rect.left + yes->rect.right) / 2, top + (yes->rect.top + yes->rect.bottom) / 2);
        Confirm = index;
        ConfirmUntil = GetTickCount() + 1500;
    }

    void HideTaskBar(const TLBSWidget* root) {
        TLBSWidget* taskBar = FindWidgetOfClass(root, "TNTTaskBarWidget", 1);
        if (taskBar && taskBar->isVisible) taskBar->isVisible = false;
    }

    void FollowUp(TLBSWidget* root) {
        if (CloseTaskBar && (Confirm >= 0 || --CloseTicks <= 0)) {
            CloseTaskBar = false;
            const TLBSWidget* taskBar = FindWidgetOfClass(root, "TNTTaskBarWidget", 1);
            // Server selection leaves the game, the menu shouldn't be seen on the way out.
            if (Confirm >= 0) HideTaskBar(root);
            else if (taskBar && taskBar->isVisible) Click(nullptr, FindButton(root, MenuButton));
        }
        if (Confirm < 0) return;
        if (static_cast<int32_t>(GetTickCount() - ConfirmUntil) > 0) {
            Confirm = -1;
            return;
        }
        // Missed the spot, answer it where it is.
        TLBSWidget* box = FindWidgetOfClass(root, "TNTMessageBoxWidget", 1);
        if (!box || !box->isVisible) return;
        Click(box, YesButton(box));
        Confirm = -1;
    }

    // A nine-slice with only the middle piece stretches the frame to the widget's size.
    TEWCustomPanelWidget* AddScaled(TLBSWidget* parent, const AtlasFrame& frame, const int16_t x, const int16_t y,
                                    const int16_t width, const int16_t height) {
        auto* panel = Widget::Create<TEWCustomPanelWidget>(CachedHost);
        if (!panel) return nullptr;
        delete[] panel->imageData.atlasFrames;
        panel->imageData.imageName = Atlas;
        panel->imageData.imageWidth = 512;
        panel->imageData.imageHeight = 512;
        panel->imageData.frameCount = 9;
        panel->imageData.atlasFrames = new AtlasFrame[9]{frame};
        panel->nineSliceInfo = {static_cast<uint16_t>(width), static_cast<uint16_t>(height), static_cast<uint16_t>(width),
                                static_cast<uint16_t>(height), 0, 0, 0, 0};
        panel->sliceCount = 1;
        panel->drawMode = 5;
        panel->isMoveable = false;
        panel->isInteractable = false;
        panel->rect = {x, y, static_cast<int16_t>(x + width), static_cast<int16_t>(y + height)};
        Overlay::Attach(parent, panel);
        return panel;
    }

    void Destroy(Ring& ring) {
        if (ring.menu) Overlay::Detach(ring.menu);
        ring.menu = nullptr;
        ring.root = nullptr;
        ring.name = nullptr;
        ring.nameBoard = nullptr;
        ring.highlight = nullptr;
        ring.icons.clear();
        ring.boards.clear();
        ring.motions.clear();
    }

    Color BoardColor(const Ring& ring, const int index) {
        if (index == ring.choice) return Color(255, 255, 200, 60);
        if (index < ring.count && ring.items[ring.first + index].marked) return Color(90, 255, 255, 255);
        return Color(215, 255, 255, 255);
    }

    constexpr int PageSize = 8;

    int Pages(const Ring& ring) {
        return std::max(1, (static_cast<int>(ring.items.size()) + PageSize - 1) / PageSize);
    }

    void BuildPage(Ring& ring, TLBSWidget* root) {
        Destroy(ring);
        Overlay::LoadImages();
        const int pages = Pages(ring);
        ring.page = std::clamp(ring.page, 0, pages - 1);
        ring.first = ring.page * PageSize;
        ring.count = std::min(PageSize, static_cast<int>(ring.items.size()) - ring.first);
        const int count = ring.count;
        const int spacing = pages > 1 ? PageSize : count;
        constexpr int16_t Radius = 170, Slot = 64;
        constexpr int16_t PageHeight = 30;
        ring.size = static_cast<int16_t>(2 * Radius + Slot + 32 + 2 * (PageHeight + 10));
        auto* menu = Widget::Create<TLBSWidget>(CachedHost);
        if (!menu) return;
        const auto left = static_cast<int16_t>((root->rect.right - root->rect.left - ring.size) / 2);
        const auto top = static_cast<int16_t>((root->rect.bottom - root->rect.top - ring.size) / 2);
        menu->rect = {left, top, static_cast<int16_t>(left + ring.size), static_cast<int16_t>(top + ring.size)};
        menu->isVisible = false;
        Overlay::Attach(root, menu);
        ring.icons.assign(spacing, nullptr);
        ring.boards.assign(spacing, nullptr);
        ring.motions.assign(spacing, nullptr);
        for (int i = 0; i < spacing; i++) {
            constexpr int16_t width = Slot, height = Slot;
            const float angle = 6.2831853f * static_cast<float>(i) / spacing;
            const auto x = static_cast<int16_t>(ring.size / 2 + std::sin(angle) * Radius - width / 2);
            const auto y = static_cast<int16_t>(ring.size / 2 - std::cos(angle) * Radius - height / 2);
            if (TEWCustomPanelWidget* board = Navigator::AddBoard(menu, width, height)) {
                board->rect = {x, y, static_cast<int16_t>(x + width), static_cast<int16_t>(y + height)};
                ring.boards[i] = board;
            }
            if (i >= count) continue;
            const Item& item = ring.items[ring.first + i];
            const bool text = !item.frame.width && !item.motion;
            if (text) {
                if (TEWLabel* label = Overlay::AddLabel(menu, static_cast<int16_t>(x - 16), static_cast<int16_t>(y + height / 2 - 8),
                                                        static_cast<int16_t>(width + 32), 3, item.label.c_str())) {
                    label->isInteractable = false;
                }
                continue;
            }
            if (item.motion) {
                auto* icon = Widget::Create<TNTTimeAniIcon>(CachedHost);
                if (!icon) continue;
                Overlay::InitCooldownFields(icon, root);
                constexpr int16_t Inset = 8;
                icon->rect = {static_cast<int16_t>(x + Inset), static_cast<int16_t>(y + Inset),
                              static_cast<int16_t>(x + Slot - Inset), static_cast<int16_t>(y + Slot - Inset)};
                Overlay::Attach(menu, icon);
                if (Overlay::ShowMotion(icon, root, item.motion)) static_cast<TLBSWidget*>(icon)->isInteractable = true;
                ring.motions[i] = icon;
                continue;
            }
            const auto iconWidth = static_cast<int16_t>(item.frame.width * IconScale);
            const auto iconHeight = static_cast<int16_t>(item.frame.height * IconScale);
            ring.icons[i] = AddScaled(menu, item.frame, static_cast<int16_t>(x + (Slot - iconWidth) / 2),
                                      static_cast<int16_t>(y + (Slot - iconHeight) / 2), iconWidth, iconHeight);
        }
        for (int i = 0; i < spacing; i++) {
            if (ring.boards[i]) ring.boards[i]->color = BoardColor(ring, i);
        }
        constexpr int16_t NameWidth = 160, NameHeight = 34;
        if ((ring.nameBoard = Navigator::AddBoard(menu, NameWidth, NameHeight))) {
            const auto nameLeft = static_cast<int16_t>(ring.size / 2 - NameWidth / 2);
            const auto nameTop = static_cast<int16_t>(ring.size / 2 - NameHeight / 2);
            ring.nameBoard->rect = {nameLeft, nameTop, static_cast<int16_t>(nameLeft + NameWidth), static_cast<int16_t>(nameTop + NameHeight)};
            ring.nameBoard->isVisible = false;
        }
        ring.name = Overlay::AddLabel(menu, static_cast<int16_t>(ring.size / 2 - NameWidth / 2), static_cast<int16_t>(ring.size / 2 - 8),
                                      NameWidth, 3, L"");
        if (ring.name) ring.name->isInteractable = false;
        if (pages > 1) {
            constexpr int16_t PageWidth = 70;
            const auto pageLeft = static_cast<int16_t>(ring.size / 2 - PageWidth / 2);
            const auto pageTop = static_cast<int16_t>(ring.size / 2 + Radius + Slot / 2 + 10);
            if (TEWCustomPanelWidget* board = Navigator::AddBoard(menu, PageWidth, PageHeight)) {
                board->rect = {pageLeft, pageTop, static_cast<int16_t>(pageLeft + PageWidth), static_cast<int16_t>(pageTop + PageHeight)};
            }
            const std::wstring text = std::to_wstring(ring.page + 1) + L" / " + std::to_wstring(pages);
            if (TEWLabel* label = Overlay::AddLabel(menu, pageLeft, static_cast<int16_t>(pageTop + PageHeight / 2 - 8), PageWidth, 3,
                                                    text.c_str())) {
                label->isInteractable = false;
            }
            static const Overlay::Image Lb = Overlay::Load("XBOX_LB_SMALL"), Rb = Overlay::Load("XBOX_RB_SMALL");
            const auto glyphTop = static_cast<int16_t>(pageTop + (PageHeight - Lb.height) / 2);
            Overlay::AddImage(menu, Lb, static_cast<int16_t>(pageLeft - Lb.width - 4), glyphTop);
            Overlay::AddImage(menu, Rb, static_cast<int16_t>(pageLeft + PageWidth + 4), glyphTop);
        }
        ring.highlight = Overlay::AddFrame(menu);
        ring.menu = menu;
        ring.root = root;
        ring.builtWidth = static_cast<int16_t>(root->rect.right - root->rect.left);
        ring.builtHeight = static_cast<int16_t>(root->rect.bottom - root->rect.top);
        ring.shown = -2;
    }

    void Build(Ring& ring, TLBSWidget* root, std::vector<Item> items) {
        ring.items = std::move(items);
        BuildPage(ring, root);
    }

    void PlaceHighlight(Ring& ring) {
        if (!ring.highlight) return;
        const TEWCustomPanelWidget* board = ring.choice >= 0 ? ring.boards[ring.choice] : nullptr;
        if (!board) {
            ring.highlight->isVisible = false;
            return;
        }
        constexpr int16_t Out = 6;
        Overlay::PlaceFrame(ring.highlight, {static_cast<int16_t>(board->rect.left - Out), static_cast<int16_t>(board->rect.top - Out),
                                             static_cast<int16_t>(board->rect.right + Out), static_cast<int16_t>(board->rect.bottom + Out)});
        ring.highlight->isVisible = true;
        ring.highlight->BubbleUp();
    }

    void ShowChoice(Ring& ring) {
        if (ring.shown == ring.choice) return;
        for (int i = 0; i < static_cast<int>(ring.boards.size()); i++) {
            if (i != ring.choice && i != ring.shown) continue;
            if (i < ring.count && ring.icons[i]) {
                const Item& item = ring.items[ring.first + i];
                ring.icons[i]->imageData.atlasFrames[0] = i == ring.choice ? item.hover : item.frame;
            }
            if (ring.boards[i]) ring.boards[i]->color = BoardColor(ring, i);
        }
        const bool named = ring.choice >= 0 && ring.choice < ring.count;
        if (ring.name) ring.name->SetText(named ? ring.items[ring.first + ring.choice].name.c_str() : L"");
        if (ring.nameBoard && ring.nameBoard->isVisible != named) ring.nameBoard->isVisible = named;
        PlaceHighlight(ring);
        ring.shown = ring.choice;
    }

    bool IsOpen(const Ring& ring) {
        return ring.menu && ring.menu->isVisible;
    }

    void Close(Ring& ring) {
        if (IsOpen(ring)) ring.menu->isVisible = false;
    }

    std::vector<Item> GameItems() {
        std::vector<Item> items;
        for (const Entry& entry : Entries) {
            Item item{entry.name, entry.label, entry.frame, entry.hover};
            if (entry.where == Motion) item.motion = static_cast<int16_t>(entry.value);
            items.push_back(std::move(item));
        }
        return items;
    }

    std::vector<Item> ClientItems() {
        ClientList = Clients::List();
        std::vector<Item> items;
        for (const Clients::Client& client : ClientList) {
            Item item;
            item.label = client.name.empty() ? L"Not in game" : client.name;
            item.name = client.pid == GetCurrentProcessId() ? item.label + L" (here)" : item.label;
            item.marked = client.pid == GetCurrentProcessId();
            items.push_back(std::move(item));
        }
        return items;
    }

    enum class Step { Idle, Busy, Tapped, Picked };

    Step Run(Ring& ring, TLBSWidget* root, const WORD buttons, const XINPUT_GAMEPAD& pad, int& picked) {
        const bool down = (buttons & ring.button) != 0;
        const DWORD now = GetTickCount();
        const bool open = IsOpen(ring);
        if (down && !ring.held) {
            ring.held = true;
            ring.downTick = now;
        }
        if (!down && ring.held) {
            ring.held = false;
            if (!open) return Step::Tapped;
            picked = ring.choice >= 0 && ring.choice < ring.count && now - ring.lastTilt < 200 ? ring.first + ring.choice : -1;
            Close(ring);
            return Step::Picked;
        }
        if (!ring.held || (!open && now - ring.downTick < HoldTime)) return Step::Idle;
        if (!open) {
            if (&ring == &ClientRing) Build(ring, root, ClientItems());
            else if (!ring.menu || ring.root != root || ring.builtWidth != root->rect.right - root->rect.left
                     || ring.builtHeight != root->rect.bottom - root->rect.top) {
                Build(ring, root, GameItems());
            }
            if (!ring.menu) return Step::Idle;
            ring.choice = -1;
            ring.menu->isVisible = true;
            ring.menu->BubbleUp();
            ring.previous = buttons;
        }
        const WORD pressed = buttons & ~ring.previous;
        ring.previous = buttons;
        const int turn = (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER ? 1 : 0) - (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER ? 1 : 0);
        if (turn && Pages(ring) > 1) {
            ring.page = (ring.page + turn + Pages(ring)) % Pages(ring);
            BuildPage(ring, root);
            if (!ring.menu) return Step::Idle;
            ring.choice = -1;
            ring.menu->isVisible = true;
            ring.menu->BubbleUp();
        }
        // Either stick, the button can be held with either thumb.
        float x = pad.sThumbLX / 32767.0f, y = pad.sThumbLY / 32767.0f;
        const float rx = pad.sThumbRX / 32767.0f, ry = pad.sThumbRY / 32767.0f;
        if (rx * rx + ry * ry > x * x + y * y) {
            x = rx;
            y = ry;
        }
        const int spacing = Pages(ring) > 1 ? PageSize : ring.count;
        if (ring.count && x * x + y * y > 0.25f) {
            float angle = std::atan2(x, y);
            if (angle < 0) angle += 6.2831853f;
            const int slot = static_cast<int>(std::lround(angle / 6.2831853f * spacing)) % spacing;
            ring.choice = slot;
            ring.lastTilt = now;
        } else if (now - ring.lastTilt >= 200) {
            ring.choice = -1;
        }
        ShowChoice(ring);
        return Step::Busy;
    }

    void StartMotion(const int index) {
        const int local = index - GameRing.first;
        if (local < 0 || local >= GameRing.count || !GameRing.motions[local] || !GameRing.menu) return;
        HWND window = FindGameWindow();
        if (!window) return;
        const Rect& r = GameRing.motions[local]->rect;
        POINT cursor{};
        GetCursorPos(&cursor);
        ScreenToClient(window, &cursor);
        Using = {true, 0, MAKELPARAM(GameRing.menu->rect.left + (r.left + r.right) / 2, GameRing.menu->rect.top + (r.top + r.bottom) / 2),
                 MAKELPARAM(cursor.x, cursor.y)};
        GameRing.menu->isVisible = true;
    }

    void StepMotion() {
        if (!Using.active) return;
        HWND window = FindGameWindow();
        const bool doubleClicks = window && (GetClassLongA(window, GCL_STYLE) & CS_DBLCLKS) != 0;
        switch (window ? Using.step++ : 3) {
            case 0:
                PostMessageA(window, WM_MOUSEMOVE, 0, Using.at);
                break;
            case 1:
                PostMessageA(window, WM_LBUTTONDOWN, MK_LBUTTON, Using.at);
                PostMessageA(window, WM_LBUTTONUP, 0, Using.at);
                break;
            case 2:
                PostMessageA(window, doubleClicks ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, MK_LBUTTON, Using.at);
                PostMessageA(window, WM_LBUTTONUP, 0, Using.at);
                break;
            default:
                if (window) PostMessageA(window, WM_MOUSEMOVE, 0, Using.back);
                Using.active = false;
                Close(GameRing);
                break;
        }
    }

    bool IsOpen() {
        return IsOpen(GameRing) || IsOpen(ClientRing) || Using.active;
    }

    // Until the window it opened shows, or the bar would flash in between.
    bool HidesBars() {
        return IsOpen() || static_cast<int32_t>(GetTickCount() - BarsHiddenUntil) < 0;
    }

    void Settle(XINPUT_GAMEPAD& pad) {
        if (!SettleLeft && !SettleRight) return;
        const auto resting = [](const SHORT x, const SHORT y) { return x * x + y * y < 8000 * 8000; };
        if (static_cast<int32_t>(GetTickCount() - SettleUntil) > 0) SettleLeft = SettleRight = false;
        if (SettleLeft && resting(pad.sThumbLX, pad.sThumbLY)) SettleLeft = false;
        if (SettleRight && resting(pad.sThumbRX, pad.sThumbRY)) SettleRight = false;
        if (SettleLeft) pad.sThumbLX = pad.sThumbLY = 0;
        if (SettleRight) pad.sThumbRX = pad.sThumbRY = 0;
    }

    void StartSettle() {
        SettleLeft = SettleRight = true;
        SettleUntil = GetTickCount() + 1000;
    }

    bool Update(TLBSWidget* root, XINPUT_GAMEPAD& pad, const bool inGame) {
        const WORD buttons = pad.wButtons;
        pad.wButtons &= ~(XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START);
        int picked = -1;
        if (inGame) {
            FollowUp(root);
            if (Using.active) {
                StepMotion();
                return true;
            }
        } else {
            Using.active = false;
            // Server selection leaves the game, the menu shouldn't be seen on the way out.
            if (CloseTaskBar) HideTaskBar(root);
            CloseTaskBar = false;
            Confirm = -1;
            GameRing.held = false;
            Close(GameRing);
            pad.wButtons |= buttons & XINPUT_GAMEPAD_BACK;
        }
        // One ring at a time, the other button waits.
        if (inGame && !ClientRing.held) {
            switch (Run(GameRing, root, buttons, pad, picked)) {
            case Step::Tapped:
                // A short press stays the UI mode toggle.
                pad.wButtons |= XINPUT_GAMEPAD_BACK;
                return false;
            case Step::Picked:
                if (picked >= 0 && Entries[picked].where == Motion) {
                    StartMotion(picked);
                } else if (picked >= 0) {
                    Open(root, picked);
                    BarsHiddenUntil = GetTickCount() + 500;
                }
                StartSettle();
                return true;
            case Step::Busy:
                return true;
            case Step::Idle:
                break;
            }
        }
        if (GameRing.held) return false;
        switch (Run(ClientRing, root, buttons, pad, picked)) {
        case Step::Picked:
            if (picked >= 0 && picked < static_cast<int>(ClientList.size()) && ClientList[picked].pid != GetCurrentProcessId()) {
                Clients::SwitchTo(ClientList[picked]);
            }
            StartSettle();
            return true;
        case Step::Busy:
            return true;
        case Step::Tapped:
            Keyboard::Toggle();
            return true;
        default:
            return false;
        }
    }

    void Hide(const TLBSWidget* root) {
        GameRing.held = ClientRing.held = false;
        if (CloseTaskBar && root) HideTaskBar(root);
        CloseTaskBar = false;
        Confirm = -1;
        Close(GameRing);
        Close(ClientRing);
    }

    void Destroy() {
        Destroy(GameRing);
        Destroy(ClientRing);
    }
}
