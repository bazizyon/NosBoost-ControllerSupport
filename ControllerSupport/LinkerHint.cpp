#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Overlay.h"
#include "Navigator.h"

namespace ControllerSupport::Overlay {
    TLBSWidget* Hint = nullptr;
    std::vector<uintptr_t> HintShown;
    Image HintLayerGlyphs[LayerCount][2];

    void LoadHintGlyphs() {
        if (HintLayerGlyphs[RB][0].id) return;
        HintLayerGlyphs[RB][0] = Load("XBOX_RB_SMALL");
        HintLayerGlyphs[LT][0] = Load("XBOX_LT_SMALL");
        HintLayerGlyphs[RT][0] = Load("XBOX_RT_SMALL");
        HintLayerGlyphs[LB][0] = Load("XBOX_LB_SMALL");
        HintLayerGlyphs[LTRT][0] = HintLayerGlyphs[LT][0];
        HintLayerGlyphs[LTRT][1] = HintLayerGlyphs[RT][0];
        HintLayerGlyphs[LTRB][0] = HintLayerGlyphs[LT][0];
        HintLayerGlyphs[LTRB][1] = HintLayerGlyphs[RB][0];
        HintLayerGlyphs[LBRB][0] = HintLayerGlyphs[LB][0];
        HintLayerGlyphs[LBRB][1] = HintLayerGlyphs[RB][0];
        HintLayerGlyphs[LBRT][0] = HintLayerGlyphs[LB][0];
        HintLayerGlyphs[LBRT][1] = HintLayerGlyphs[RT][0];
    }

    void DestroyHint() {
        if (Hint) Detach(Hint);
        Hint = nullptr;
        HintShown.clear();
    }

    std::vector<uintptr_t> HintState(const TLBSWidget* root) {
        std::vector<uintptr_t> state;
        for (uint8_t action = Linker1; action <= Linker5; action++) {
            const TNTTimeAniIcon* icon = BarIcon(root, action);
            const uintptr_t record = icon ? reinterpret_cast<uintptr_t>(icon->image) : 0;
            if (!record) continue;
            bool attached = false;
            for (int layer = 0; layer < LayerCount && !attached; layer++) {
                for (int cell = 0; cell < 8 && !attached; cell++) {
                    if (Panels[layer].slots[cell].recast != record) continue;
                    attached = true;
                    state.push_back(record);
                    state.push_back(static_cast<uintptr_t>(action << 16 | layer << 8 | cell));
                }
            }
            if (attached) continue;
            for (int layer = 0; layer < LayerCount; layer++) {
                for (int cell = 0; cell < 8; cell++) {
                    if (Bindings[layer][cell].set && Bindings[layer][cell].action == action) {
                        state.push_back(record);
                        state.push_back(static_cast<uintptr_t>(action << 16 | layer << 8 | cell));
                    }
                }
            }
        }
        return state;
    }

    void BuildHint(TLBSWidget* root, const std::vector<uintptr_t>& state) {
        DestroyHint();
        HintShown = state;
        if (state.empty()) return;
        LoadHintGlyphs();
        constexpr int16_t Row = 44, Icon = 40, Glyph = 28, Pad = 8;
        const auto rows = static_cast<int16_t>(state.size() / 2);
        int glyphs = 1;
        for (size_t i = 1; i < state.size(); i += 2) {
            const int layer = static_cast<int>(state[i] >> 8 & 0xFF);
            glyphs = std::max(glyphs, 1 + (HintLayerGlyphs[layer][0].id ? 1 : 0) + (HintLayerGlyphs[layer][1].id ? 1 : 0));
        }
        const auto width = static_cast<int16_t>(Pad + Icon + 6 + glyphs * Glyph + Pad);
        const int16_t height = static_cast<int16_t>(Pad + rows * Row + Pad - 4);
        const int16_t centreX = static_cast<int16_t>((root->rect.right - root->rect.left) / 2);
        const int16_t left = static_cast<int16_t>(centreX + CrossGap / 2 + Step + SlotSize / 2 + 40);
        const int16_t bottom = static_cast<int16_t>(root->rect.bottom - root->rect.top - BottomMargin + Step + SlotSize / 2);
        auto* hint = Widget::Create<TLBSWidget>(CachedHost);
        if (!hint) return;
        hint->rect = {left, static_cast<int16_t>(bottom - height), static_cast<int16_t>(left + width), bottom};
        Attach(root, hint);
        Navigator::AddBoard(hint, width, height);
        for (int16_t row = 0; row < rows; row++) {
            const auto action = static_cast<uint8_t>(state[row * 2 + 1] >> 16);
            const int layer = static_cast<int>(state[row * 2 + 1] >> 8 & 0xFF);
            const int cell = static_cast<int>(state[row * 2 + 1] & 0xFF);
            const auto y = static_cast<int16_t>(Pad + row * Row);
            if (auto* icon = Widget::Create<TNTTimeAniIcon>(CachedHost)) {
                InitCooldownFields(icon, root);
                icon->rect = {Pad, y, static_cast<int16_t>(Pad + Icon), static_cast<int16_t>(y + Icon)};
                Attach(hint, icon);
                CopyBarSkill(icon, BarIcon(root, action), false);
            }
            auto x = static_cast<int16_t>(Pad + Icon + 6);
            const auto glyphY = static_cast<int16_t>(y + (Icon - Glyph) / 2);
            for (const Image& glyph : HintLayerGlyphs[layer]) {
                if (!glyph.id) continue;
                AddImage(hint, glyph, x, glyphY);
                x = static_cast<int16_t>(x + Glyph);
            }
            AddImage(hint, CellGlyphs[cell], x, glyphY);
        }
        Hint = hint;
    }

    void UpdateHint(TLBSWidget* root, const bool shown) {
        const std::vector<uintptr_t> state = shown ? HintState(root) : std::vector<uintptr_t>{};
        if (state != HintShown) BuildHint(root, state);
    }
}
