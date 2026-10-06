#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"

namespace ControllerSupport::Overlay {
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
            const uint8_t kind = DragFields[0xB4 - BindingFirst];
            for (int layer = 0; layer < LayerCount && (kind == SkillKind || kind == ItemKind); layer++) {
                for (int i = 0; i < 8; i++) {
                    if (Fixed(layer) || !Contains(Panels[layer].slots[i].rect, mouseX, mouseY)) continue;
                    Binding& binding = Bindings[layer][i];
                    binding = {};
                    binding.set = true;
                    std::memcpy(binding.fields, DragFields, BindingSize);
                    binding.kind = kind;
                    binding.tab = *reinterpret_cast<int16_t*>(DragFields + (0xB6 - BindingFirst));
                    binding.index = *reinterpret_cast<int16_t*>(DragFields + (0xB8 - BindingFirst));
                    ApplyBinding(Panels[layer].slots[i], binding);
                    SaveBindings();
                }
            }
        }
        WasDragging = dragging;
    }

    void HighlightSelection() {
        for (int id = 1; id < ActionCount; id++) {
            if (!InPalette(id)) continue;
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

    void __cdecl OnSlotClick(void* argument) {
        if (!EditMode || !SelectedAction) return;
        const auto* ref = static_cast<SlotRef*>(argument);
        PlaceAction(ref->layer, ref->cell, SelectedAction);
    }

    void BuildPalette(TLBSWidget* root, const int16_t panelsRight) {
        constexpr int Rows = (PaletteCount + 1) / 2;
        const int16_t height = static_cast<int16_t>(Rows * Step + 30);
        const int16_t left = static_cast<int16_t>(panelsRight + 30);
        const int16_t top = static_cast<int16_t>(root->rect.bottom - root->rect.top - BottomMargin - Step - SlotSize / 2 - height + SlotSize + 2 * Step - EditRaise);
        auto* container = Widget::Create<TLBSWidget>(CachedHost);
        if (!container) return;
        container->rect = {left, top, static_cast<int16_t>(left + 2 * Step), static_cast<int16_t>(top + height)};
        Attach(root, container);
        PaletteContainer = container;
        AddLabel(container, 0, 0, 2 * Step, 3, L"Actions");
        int placed = 0;
        for (int id = 1; id < ActionCount; id++) {
            if (!InPalette(id)) continue;
            const int16_t x = static_cast<int16_t>((placed % 2) * Step);
            const int16_t y = static_cast<int16_t>(24 + (placed / 2) * Step);
            placed++;
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
                AddLines(container, static_cast<int16_t>(x - 8), static_cast<int16_t>(y + SlotSize / 2 - 6), SlotSize + 16, look.text);
            }
            if (look.caption[0]) {
                AddLabel(container, static_cast<int16_t>(x - 8), static_cast<int16_t>(y + SlotSize - 22), SlotSize + 16, 3, look.caption);
            }
        }
        HighlightSelection();
    }

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

    void PlaceAction(const int layer, const int cell, const uint8_t action) {
        if (Fixed(layer)) return;
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
            if (!InPalette(id)) continue;
                if (PaletteTiles[id] && Contains(PaletteRects[id], mouseX, mouseY)) {
                    DraggedAction = static_cast<uint8_t>(id);
                    DragStartX = mouseX;
                    DragStartY = mouseY;
                }
            }
        }
        const bool moved = DraggedAction && (std::abs(mouseX - DragStartX) > 4 || std::abs(mouseY - DragStartY) > 4);
        if (down && moved) {
            if (!DragLabel && (DragLabel = AddLabel(root, 0, 0, 80, 3, OneLine(Actions[DraggedAction].text).c_str()))) {
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

    void WatchRightClick(const int32_t mouseX, const int32_t mouseY) {
        const bool down = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (down && !RightWasDown) {
            for (int layer = 0; layer < LayerCount; layer++) {
                for (int cell = 0; cell < 8; cell++) {
                    if (Fixed(layer) || !Contains(Panels[layer].slots[cell].rect, mouseX, mouseY)) continue;
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
}
