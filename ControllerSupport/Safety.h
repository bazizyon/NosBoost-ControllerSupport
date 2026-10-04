#pragma once
#include "Game.h"

namespace ControllerSupport::Safety {
    inline std::string BlockedReason;

    bool IsCheckFunction(const uintptr_t function);
    bool CallsCheck(const uintptr_t function);
    void Verify(const ModHost* host, std::initializer_list<std::pair<const char*, uintptr_t>> functions);
}
