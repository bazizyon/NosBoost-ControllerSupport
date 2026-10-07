#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"

namespace ControllerSupport::Overlay {
    Image Load(const char* name) {
        Image image;
        image.id = LoadUiImageResource(name, image.width, image.height);
        return image;
    }

    bool IsBarSkill(const uint8_t action) {
        return action >= PetSkill1 && action <= Linker5;
    }

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
        LayerGlyphs[LTRB][0] = LayerGlyphs[LT][0];
        LayerGlyphs[LTRB][1] = LayerGlyphs[RB][0];
        LayerGlyphs[LBRB][0] = LayerGlyphs[LB][0];
        LayerGlyphs[LBRB][1] = LayerGlyphs[RB][0];
        LayerGlyphs[LBRT][0] = LayerGlyphs[LB][0];
        LayerGlyphs[LBRT][1] = LayerGlyphs[RT][0];
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

    TEWCustomPanelWidget* AddFrame(TLBSWidget* parent) {
        static Image image;
        if (!image.id) image = Load("FRAME");
        if (!image.id) return nullptr;
        auto* frame = Widget::Create<TEWCustomPanelWidget>(CachedHost);
        if (!frame) return nullptr;
        constexpr int16_t C = 8;
        const auto S = static_cast<int16_t>(image.width);
        const auto M = static_cast<int16_t>(S - 2 * C);
        delete[] frame->imageData.atlasFrames;
        frame->imageData.imageName = image.id;
        frame->imageData.imageWidth = S;
        frame->imageData.imageHeight = static_cast<int16_t>(image.height);
        frame->imageData.frameCount = 9;
        frame->imageData.atlasFrames = new AtlasFrame[9]{
            {C, C, M, M}, {0, 0, C, C}, {C, 0, M, C}, {static_cast<int16_t>(S - C), 0, C, C},
            {static_cast<int16_t>(S - C), C, C, M}, {static_cast<int16_t>(S - C), static_cast<int16_t>(S - C), C, C},
            {C, static_cast<int16_t>(S - C), M, C}, {0, static_cast<int16_t>(S - C), C, C}, {0, C, C, M},
        };
        frame->sliceCount = 1;
        frame->drawMode = 5;
        frame->isMoveable = false;
        frame->isInteractable = false;
        frame->isVisible = false;
        Attach(parent, frame);
        return frame;
    }

    void PlaceFrame(TEWCustomPanelWidget* frame, const Rect& rect) {
        if (!frame) return;
        const auto middleWidth = static_cast<uint16_t>(rect.right - rect.left - 16);
        const auto middleHeight = static_cast<uint16_t>(rect.bottom - rect.top - 16);
        frame->nineSliceInfo = {middleWidth, middleHeight, static_cast<uint16_t>(8 + middleWidth),
                                static_cast<uint16_t>(8 + middleHeight), 8, 8, 8, 8};
        frame->rect = rect;
    }

    // Action names are up to three lines split on '\n', kept centred on where a single line would sit.
    constexpr int16_t LineHeight = 12;

    int LineCount(const wchar_t* text) {
        int count = 1;
        for (const wchar_t* c = text; *c; c++) count += *c == L'\n';
        return std::min(count, 3);
    }

    void SetLines(TEWLabel* const (&lines)[3], const wchar_t* text, const int16_t top) {
        const int count = LineCount(text);
        const wchar_t* start = text;
        for (int i = 0; i < 3; i++) {
            const wchar_t* end = start;
            while (*end && *end != L'\n') end++;
            const std::wstring line = i < count ? std::wstring(start, end) : std::wstring();
            if (*end) start = end + 1;
            else start = end;
            if (!lines[i]) continue;
            lines[i]->SetText(line.c_str());
            const auto y = static_cast<int16_t>(top - (count - 1) * LineHeight / 2 + i * LineHeight + TextNudge);
            lines[i]->rect.bottom = static_cast<int16_t>(y + (lines[i]->rect.bottom - lines[i]->rect.top));
            lines[i]->rect.top = y;
        }
    }

    void AddLines(TLBSWidget* parent, const int16_t x, const int16_t top, const int16_t width, const wchar_t* text) {
        TEWLabel* lines[3]{};
        for (int i = 0; i < LineCount(text); i++) lines[i] = AddLabel(parent, x, top, width, 3, L"");
        SetLines(lines, text, top);
    }

    std::wstring OneLine(const wchar_t* text) {
        std::wstring line(text);
        std::ranges::replace(line, L'\n', L' ');
        return line;
    }

    TEWControlWidget* AddSprite(TLBSWidget* parent, const int image, const AtlasFrame& frame, const int16_t x,
                                const int16_t y, const int16_t imageWidth, const int16_t imageHeight) {
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

    void ApplyBinding(SlotView& slot, const Binding& binding) {
        const ActionLook& look = Actions[binding.set ? binding.action : NoAction];
        SetLines(slot.text, look.text, slot.textTop);
        if (slot.caption) slot.caption->SetText(look.caption);
        if (slot.actionIcon && look.sprite) PlacePicture(slot.actionIcon, *look.sprite, -GlyphOffset, -GlyphOffset, look.caption[0]);
        slot.motion = 0;
        slot.barRecord = 0;
        slot.recast = 0;
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
        const int16_t left = static_cast<int16_t>(centreX - CrossGap / 2 - Step - SlotSize / 2 + GlyphOffset - PanelSpill);
        const int16_t top = static_cast<int16_t>(centreY - Step - SlotSize / 2 + GlyphOffset - PanelSpill);
        const int16_t right = static_cast<int16_t>(centreX + CrossGap / 2 + Step + SlotSize / 2 + PanelSpill);
        const int16_t bottom = static_cast<int16_t>(centreY + Step + SlotSize / 2 + PanelSpill);
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
            slot.textTop = static_cast<int16_t>(In + SlotSize / 2 - 2);
            for (TEWLabel*& line : slot.text) line = AddLabel(group, In - 8, slot.textTop, SlotSize + 16, 3, L"");
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
        for (SlotView& slot : panel.slots) {
            slot.glowBase = {static_cast<int16_t>(slot.rect.left - left), static_cast<int16_t>(slot.rect.top - top),
                             static_cast<int16_t>(slot.rect.right - left), static_cast<int16_t>(slot.rect.bottom - top)};
            slot.glow = AddFrame(container);
            slot.ripple = AddFrame(container);
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
        for (TEWLabel* line : slot.text) SetShown(line, action && !pictured && !motion);
    }

    void Show(Panel& panel, const bool shown, const Binding (&bindings)[8]) {
        for (int i = 0; i < 8; i++) ShowSlot(panel.slots[i], bindings[i]);
        if (panel.shown == shown || !panel.container) return;
        panel.shown = shown;
        panel.container->isVisible = shown;
    }

    void ShowFrame(TEWCustomPanelWidget* frame, const Rect& base, const int out, const int alpha, int16_t& shown) {
        if (!frame) return;
        const auto key = static_cast<int16_t>(alpha <= 0 ? 0 : out << 8 | alpha);
        if (key == shown) return;
        shown = key;
        SetShown(frame, alpha > 0);
        if (alpha <= 0) return;
        PlaceFrame(frame, {static_cast<int16_t>(base.left - out), static_cast<int16_t>(base.top - out),
                           static_cast<int16_t>(base.right + out), static_cast<int16_t>(base.bottom + out)});
        frame->color = Color(static_cast<uint8_t>(alpha), 255, 255, 255);
    }

    void UpdateGlow(const int layer, const XINPUT_GAMEPAD& pad) {
        constexpr float FadeIn = 70.0f, FadeOut = 220.0f;
        constexpr DWORD RippleTime = 320;
        static DWORD last = GetTickCount();
        const DWORD now = GetTickCount();
        const auto elapsed = static_cast<float>(std::min<DWORD>(now - last, 100));
        last = now;
        for (int l = 0; l < LayerCount; l++) {
            for (int i = 0; i < 8; i++) {
                SlotView& slot = Panels[l].slots[i];
                const bool held = l == layer && (pad.wButtons & CellButtons[i]);
                if (held && !slot.held) slot.pressTick = now;
                slot.held = held;
                slot.glowLevel = held ? std::min(1.0f, slot.glowLevel + elapsed / FadeIn) : std::max(0.0f, slot.glowLevel - elapsed / FadeOut);
                if (l != layer) slot.glowLevel = 0.0f;
                ShowFrame(slot.glow, slot.glowBase, 4, static_cast<int>(slot.glowLevel * 15) * 17, slot.glowShown);
                const DWORD age = now - slot.pressTick;
                if (l == layer && slot.pressTick && age < RippleTime) {
                    const float p = static_cast<float>(age) / RippleTime;
                    const float eased = 1.0f - (1.0f - p) * (1.0f - p);
                    ShowFrame(slot.ripple, slot.glowBase, 4 + static_cast<int>(eased * 14), static_cast<int>((1.0f - p) * 15) * 15,
                              slot.rippleShown);
                } else {
                    slot.pressTick = 0;
                    ShowFrame(slot.ripple, slot.glowBase, 0, 0, slot.rippleShown);
                }
            }
        }
    }

    void Destroy(Panel& panel) {
        if (panel.container) Detach(panel.container);
        panel = {};
    }
}
