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
        return CharacterName + "." + std::to_string(Morph);
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
                else if (binding.set) value = "S," + std::to_string(binding.tab) + "," + std::to_string(binding.index);
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
                if (std::sscanf(value, "S,%d,%d", &tab, &index) != 2) {
                    ApplyBinding(Panels[layer].slots[cell], Bindings[layer][cell]);
                    continue;
                }
                const TNTTimeAniIcon* model = SkillWindowIcon(root, 0);
                const uint8_t* item = SkillItem(root, static_cast<int16_t>(tab), static_cast<int16_t>(index));
                if (!model || !item) {
                    anyMissing = true;
                    continue;
                }
                Binding& binding = Bindings[layer][cell];
                binding = {};
                binding.set = true;
                std::memcpy(binding.fields, reinterpret_cast<const uint8_t*>(model) + BindingFirst, 0xB0 - BindingFirst);
                std::memcpy(binding.fields + (0xB0 - BindingFirst), item, 0xBC - 0xB0);
                binding.tab = static_cast<int16_t>(tab);
                binding.index = static_cast<int16_t>(index);
                ApplyBinding(Panels[layer].slots[cell], binding);
            }
        }
        return !anyMissing;
    }

    void RequestLoad() {
        LoadPending = true;
        LoadDeadline = GetTickCount() + 10000;
    }

    void TrackCharacter() {
        TSceneManager* scene = GetSceneManager();
        const auto* player = scene ? reinterpret_cast<const uint8_t*>(scene->mapPlayerObjPtr) : nullptr;
        if (!player) return;
        const uint8_t morphByte = player[0x1B9];
        const int morph = morphByte == 0xFF ? 0 : morphByte;
        std::string name;
        if (const auto* text = *reinterpret_cast<const char* const*>(player + 0x1F8)) {
            const int32_t length = *reinterpret_cast<const int32_t*>(text - 4);
            if (length > 0 && length < 64) name.assign(text, length);
        }
        if (name.empty()) return;
        if (name != CharacterName || morph != Morph) {
            CharacterName = name;
            Morph = morph;
            RequestLoad();
        }
    }

    void UpdateLoad(const TLBSWidget* root) {
        if (!LoadPending || CharacterName.empty() || !Panels[Base].container) return;
        if (LoadBindings(root) || static_cast<int32_t>(GetTickCount() - LoadDeadline) > 0) LoadPending = false;
    }
}
