#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"

namespace ControllerSupport::Overlay {
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

    uint32_t GameTime() {
        if (!TimerGlobal) {
            const uint8_t TimerPattern[] = {0xA1, 0, 0, 0, 0, 0xC3, 0x8B, 0xC0, 0x53, 0x56, 0x57, 0x84, 0xD2, 0x74, 0x08};
            const uintptr_t match = FindPattern(TimerPattern, "x????xxxxxxxxxx", nullptr, 1);
            TimerGlobal = match ? *reinterpret_cast<uintptr_t*>(match) : 0;
            if (!TimerGlobal) return 0;
        }
        return *reinterpret_cast<uint32_t*>(TimerGlobal);
    }

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
                if (Bindings[layer][cell].kind == ItemKind) continue;
                if (!IsBarSkill(Bindings[layer][cell].action)) {
                    ShowEntryCooldown(slot, now);
                    continue;
                }
                const uintptr_t record = reinterpret_cast<uintptr_t>(slot.icon->image);
                const TNTTimeAniIcon* source = slot.source;
                if (!source || !IsClass(source, "TNTTimeAniIcon") || reinterpret_cast<uintptr_t>(source->image) != record) continue;
                TNTTimeAniIcon* icon = slot.icon;
                const auto* sourceFlags = reinterpret_cast<const uint8_t*>(&source->flags);
                auto* iconFlags = reinterpret_cast<uint8_t*>(&icon->flags);
                // startTick is really the last update tick, so bring the cooldown up to now.
                const uint32_t elapsed = source->elapsedMs + (now - source->startTick);
                const bool running = sourceFlags[3] != 0 && elapsed < source->cooldownMs;
                const uint32_t start = now - elapsed;
                const bool restarted = !slot.cooling || static_cast<int32_t>(start - slot.cooldownStart) > 100
                    || static_cast<int32_t>(slot.cooldownStart - start) > 100;
                if (running && restarted) {
                    slot.cooldownStart = start;
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
                } else if (!running && slot.cooling) {
                    StopCooldown(icon);
                    slot.cooling = false;
                    slot.cooldownStart = 0;
                }
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

    TNTTimeAniIcon* BarIcon(const TLBSWidget* root, const uint8_t action) {
        const bool pet = action <= PetSkill3;
        const bool linker = action >= Linker1;
        const TLBSWidget* bar = FindWidgetOfClass(root, linker ? "TNTLinkerSlotWidget" : pet ? "TNTPetSKillSlotWidget" : "TNTPartnerSlotWidget", 1);
        // The game hides these bars while there's nothing to use, their icons keep old skills.
        if (!bar || !bar->isVisible || !bar->childrenList || !bar->childrenList->list) return nullptr;
        int wanted = action - (linker ? Linker1 : pet ? PetSkill1 : PartnerSkill1);
        for (uint32_t i = 0; i < bar->childrenList->count; i++) {
            TLBSWidget* child = bar->childrenList->list[i];
            if (!IsClass(child, "TNTTimeAniIcon") || wanted-- != 0) continue;
            return !linker || child->isVisible ? reinterpret_cast<TNTTimeAniIcon*>(child) : nullptr;
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
    int16_t ItemId(const uintptr_t record) {
        const uintptr_t data = record ? *reinterpret_cast<const uintptr_t*>(record + 0x08) : 0;
        return data ? static_cast<int16_t>(*reinterpret_cast<const int32_t*>(data)) : 0;
    }

    TLBSWidget* InventoryWindow(const TLBSWidget* root) {
        return FindWidgetOfClass(root, "TNTCharacterInventoryInfoWidget", 1);
    }

    const uint8_t* InventoryIcon(const TLBSWidget* root) {
        const TLBSWidget* window = InventoryWindow(root);
        if (!window || !window->childrenList || !window->childrenList->list) return nullptr;
        for (uint32_t i = 0; i < window->childrenList->count; i++) {
            const TLBSWidget* child = window->childrenList->list[i];
            if (IsClass(child, "TNTIconWidget")) return reinterpret_cast<const uint8_t*>(child);
        }
        return nullptr;
    }

    const uint8_t* InventoryItem(const TLBSWidget* root, const int16_t tab, const int16_t slot) {
        const TLBSWidget* window = InventoryWindow(root);
        if (!window) return nullptr;
        const auto* bytes = reinterpret_cast<const uint8_t*>(window);
        for (uint32_t offset = 0x228; offset <= 0x230; offset += 4) {
            const auto* list = *reinterpret_cast<const uint8_t* const*>(bytes + offset);
            if (!IsClass(reinterpret_cast<const TLBSWidget*>(list), "TNTItemList")) continue;
            const auto* items = *reinterpret_cast<const uint8_t* const* const*>(list + 4);
            const int32_t count = *reinterpret_cast<const int32_t*>(list + 8);
            for (int32_t i = 0; items && i < count; i++) {
                const uint8_t* item = items[i];
                if (item && *reinterpret_cast<const int16_t*>(item + 6) == tab && *reinterpret_cast<const int16_t*>(item + 8) == slot) {
                    return ItemId(*reinterpret_cast<const uintptr_t*>(item)) ? item : nullptr;
                }
            }
        }
        return nullptr;
    }

    // Handlers come from the skill window or inventory, never from the game's hotbar.
    // While a recast stage is up it shares the base skill's hotbar slot record (+0x14), only when the base is on the game's bar.
    const uint8_t* ActiveStage(const TLBSWidget* root, const uint8_t* base) {
        const uintptr_t baseEntry = *reinterpret_cast<const uintptr_t*>(base);
        const uintptr_t link = baseEntry ? *reinterpret_cast<const uintptr_t*>(baseEntry + 0x14) : 0;
        if (!link) return base;
        const TLBSWidget* window = SkillWindow(root);
        const auto* bytes = reinterpret_cast<const uint8_t*>(window);
        for (uint32_t offset = 0x15C; window && offset <= 0x168; offset += 4) {
            const auto* list = *reinterpret_cast<const uint8_t* const*>(bytes + offset);
            if (!IsClass(reinterpret_cast<const TLBSWidget*>(list), "TNTItemList")) continue;
            const auto* items = *reinterpret_cast<const uint8_t* const* const*>(list + 4);
            const int32_t count = *reinterpret_cast<const int32_t*>(list + 8);
            for (int32_t i = 0; items && i < count; i++) {
                const uint8_t* item = items[i];
                const uintptr_t entry = item ? *reinterpret_cast<const uintptr_t*>(item) : 0;
                if (!entry || entry == baseEntry || *reinterpret_cast<const int16_t*>(item + 6) != 1) continue;
                if (*reinterpret_cast<const uint8_t*>(entry + 0x11) && *reinterpret_cast<const uintptr_t*>(entry + 0x14) == link) return item;
            }
        }
        return base;
    }

    bool IsRecastStage(const uintptr_t entry) {
        const uintptr_t data = entry ? *reinterpret_cast<const uintptr_t*>(entry + 0x08) : 0;
        return data && *reinterpret_cast<const int32_t*>(data + 0x68) == 999;
    }

    const uint8_t* LinkedRecast(const TLBSWidget* root, SlotView& slot) {
        if (!slot.recast) return nullptr;
        for (uint8_t action = Linker1; action <= Linker5; action++) {
            const TNTTimeAniIcon* icon = BarIcon(root, action);
            if (icon && reinterpret_cast<uintptr_t>(icon->image) == slot.recast) return reinterpret_cast<const uint8_t*>(icon);
        }
        slot.recast = 0;
        return nullptr;
    }

    // Skills that turn into a follow-up carry a type 68 effect, Meditate doesn't.
    bool HasFollowUp(const uintptr_t entry) {
        const uintptr_t data = entry ? *reinterpret_cast<const uintptr_t*>(entry + 0x08) : 0;
        for (int i = 0; data && i < 5; i++) {
            if (*reinterpret_cast<const int32_t*>(data + 0x1D8 + i * 0x14) == 68) return true;
        }
        return false;
    }

    // A recast stage showing up right after a slot was cast takes over that slot.
    void LinkRecasts(const TLBSWidget* root) {
        if (LastCastLayer < 0 || GetTickCount() - LastCastTick > 1500) return;
        SlotView& slot = Panels[LastCastLayer].slots[LastCastCell];
        if (!slot.icon || !HasFollowUp(reinterpret_cast<uintptr_t>(slot.icon->image))) return;
        for (uint8_t action = Linker1; action <= Linker5; action++) {
            const TNTTimeAniIcon* icon = BarIcon(root, action);
            const uintptr_t record = icon ? reinterpret_cast<uintptr_t>(icon->image) : 0;
            if (!record || record == slot.recast || !IsRecastStage(record)) continue;
            bool taken = false;
            for (const Panel& panel : Panels) {
                for (const SlotView& other : panel.slots) taken = taken || other.recast == record;
            }
            if (taken) continue;
            slot.recast = record;
            LastCastLayer = -1;
            return;
        }
    }

    const uint8_t* HotbarIcon(const TLBSWidget* root) {
        const TLBSWidget* bar = FindWidgetOfClass(root, "TNTQuickSlotWidget", 1);
        return bar ? *reinterpret_cast<const uint8_t* const*>(reinterpret_cast<const uint8_t*>(bar) + 0xCC) : nullptr;
    }

    bool ResolveSkill(const TLBSWidget* root, Binding& binding, SlotView& slot) {
        const bool isItem = binding.kind == ItemKind;
        const uint8_t* base = isItem ? InventoryItem(root, binding.tab, binding.index) : SkillItem(root, binding.tab, binding.index);
        const uint8_t* item = base && !isItem && binding.tab == 1 ? ActiveStage(root, base) : base;
        const uint8_t* linked = base && !isItem ? LinkedRecast(root, slot) : nullptr;
        if (linked) item = linked + 0xB0;
        // The skill window refuses recast stages while in an SP, the game's hotbar casts them.
        const auto* model = isItem ? InventoryIcon(root)
            : linked ? linked : item != base ? HotbarIcon(root) : reinterpret_cast<const uint8_t*>(SkillWindowIcon(root, 0));
        if (!item || !model) {
            slot.missing = true;
            return false;
        }
        slot.missing = false;
        uint8_t* fields = binding.fields + (0xB0 - BindingFirst);
        uint8_t* handlers = binding.fields + (0x98 - BindingFirst);
        auto* icon = reinterpret_cast<uint8_t*>(slot.icon);
        if (std::memcmp(fields, item, 0xBC - 0xB0) == 0 && std::memcmp(handlers, model + 0x98, 0xB0 - 0x98) == 0
            && icon && std::memcmp(icon + 0xB0, item, 0xBC - 0xB0) == 0 && std::memcmp(icon + 0x98, model + 0x98, 0xB0 - 0x98) == 0) {
            return true;
        }
        std::memcpy(fields, item, 0xBC - 0xB0);
        std::memcpy(handlers, model + 0x98, 0xB0 - 0x98);
        std::memcpy(binding.fields, model + BindingFirst, 0x74 - BindingFirst);
        if (icon) {
            std::memcpy(icon + 0xB0, item, 0xBC - 0xB0);
            std::memcpy(icon + 0x98, model + 0x98, 0xB0 - 0x98);
            std::memcpy(icon + BindingFirst, model + BindingFirst, 0x74 - BindingFirst);
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
}
