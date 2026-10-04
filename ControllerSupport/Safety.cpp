#include "Game.h"
#include "Camera.h"
#include "Input.h"
#include "Targeting.h"
#include "Safety.h"
#include "Overlay.h"

namespace ControllerSupport::Safety {
    bool IsCheckFunction(const uintptr_t function) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(function);
        for (int i = 0; i < 0x100; i++) {
            if (bytes[i] == 0xB8 && bytes[i + 1] == 0x1E && bytes[i + 2] == 0 && bytes[i + 3] == 0 && bytes[i + 4] == 0
                && bytes[i + 5] == 0xE8 && bytes[i + 10] == 0xFF && bytes[i + 11] == 0x04 && bytes[i + 12] == 0x85) {
                return true;
            }
        }
        return false;
    }

    bool CallsCheck(const uintptr_t function) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(function);
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        const size_t imageSize = GetModuleSize(nullptr);
        for (int i = 0; i < 0x1000; i++) {
            const uint8_t op = bytes[i];
            if ((op == 0xC3 || (op == 0xC2 && bytes[i + 2] == 0)) ) {
                const uint8_t next = bytes[i + (op == 0xC3 ? 1 : 3)];
                if (next == 0x8B || next == 0x90 || next == 0x00) break;
            }
            if (op != 0xE8) continue;
            const uintptr_t target = function + i + 5 + *reinterpret_cast<const int32_t*>(bytes + i + 1);
            if (target >= base && target < base + imageSize && IsCheckFunction(target)) return true;
        }
        return false;
    }

    void Verify(const ModHost* host, std::initializer_list<std::pair<const char*, uintptr_t>> functions) {
        for (const auto& [name, address] : functions) {
            if (address && CallsCheck(address)) {
                GameCallsAllowed = false;
                BlockedReason += std::string(BlockedReason.empty() ? "" : ", ") + name;
            }
        }
        if (!GameCallsAllowed) {
            BlockedReason = "Game function now guarded: " + BlockedReason + ". ControllerSupport makes no game calls until it's updated.";
            host->ReportStatus(ModHealthLevel::Broken, BlockedReason.c_str());
        }
    }
}
