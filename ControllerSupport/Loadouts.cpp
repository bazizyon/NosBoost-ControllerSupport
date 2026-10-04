#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"

namespace ControllerSupport::Overlay {
    std::string IniPath() {
        char exe[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string folder(exe);
        return folder.substr(0, folder.find_last_of("\\/") + 1) + "mods\\ControllerSupport.ini";
    }

    std::string Section() {
        return CharacterName + "." + std::to_string(LoadoutSkill);
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
                else if (binding.set) value = (binding.kind == ItemKind ? "I," : "S,") + std::to_string(binding.tab) + "," + std::to_string(binding.index);
                WritePrivateProfileStringA(section.c_str(), key.c_str(), value.c_str(), path.c_str());
            }
        }
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
        if (GetPrivateProfileSectionA(section.c_str(), probe, sizeof(probe), path.c_str()) == 0 && Morph >= 0) {
            // Old saves were per morph, carry that one over once.
            const std::string old = CharacterName + "." + std::to_string(Morph);
            static char keys[8192];
            if (GetPrivateProfileSectionA(old.c_str(), keys, sizeof(keys), path.c_str()) > 0) {
                WritePrivateProfileSectionA(section.c_str(), keys, path.c_str());
            }
        }
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
                uint8_t bindingKind = SkillKind;
                if (std::sscanf(value, "I,%d,%d", &tab, &index) == 2) {
                    bindingKind = ItemKind;
                } else if (std::sscanf(value, "S,%d,%d", &tab, &index) != 2) {
                    ApplyBinding(Panels[layer].slots[cell], Bindings[layer][cell]);
                    continue;
                }
                Binding& binding = Bindings[layer][cell];
                binding = {};
                binding.kind = bindingKind;
                binding.tab = static_cast<int16_t>(tab);
                binding.index = static_cast<int16_t>(index);
                SlotView& slot = Panels[layer].slots[cell];
                if (!ResolveSkill(root, binding, slot)) {
                    binding = {};
                    anyMissing = true;
                    continue;
                }
                binding.set = true;
                ApplyBinding(slot, binding);
            }
        }
        return !anyMissing;
    }

    void RequestLoad() {
        LoadPending = true;
        LoadDeadline = GetTickCount() + 10000;
    }

    int FirstSkill(const TLBSWidget* root) {
        const uint8_t* item = SkillItem(root, 1, 0);
        const uintptr_t entry = item ? *reinterpret_cast<const uintptr_t*>(item) : 0;
        const uintptr_t data = entry ? *reinterpret_cast<const uintptr_t*>(entry + 0x08) : 0;
        return data ? *reinterpret_cast<const int32_t*>(data) : 0;
    }

    // Keyed on the first skill: a vehicle changes the morph, not the skills.
    void TrackCharacter(const TLBSWidget* root) {
        TSceneManager* scene = GetSceneManager();
        const auto* player = scene ? reinterpret_cast<const uint8_t*>(scene->mapPlayerObjPtr) : nullptr;
        if (!player) return;
        const int specialist = FirstSkill(root);
        if (!specialist) return;
        const uint16_t look = *reinterpret_cast<const uint16_t*>(player + 0x1B8);
        const int morph = look == 0xFFFF ? 0 : (look & 0xFF) == 0 ? look >> 8 : -1;
        std::string name;
        if (const auto* text = *reinterpret_cast<const char* const*>(player + 0x1F8)) {
            const int32_t length = *reinterpret_cast<const int32_t*>(text - 4);
            if (length > 0 && length < 64) name.assign(text, length);
        }
        if (name.empty()) return;
        if (name != CharacterName || specialist != LoadoutSkill) {
            CharacterName = name;
            LoadoutSkill = specialist;
            Morph = morph;
            RequestLoad();
        }
    }

    void UpdateLoad(const TLBSWidget* root) {
        if (!LoadPending || CharacterName.empty() || !Panels[Base].container) return;
        if (LoadBindings(root) || static_cast<int32_t>(GetTickCount() - LoadDeadline) > 0) LoadPending = false;
    }
}
