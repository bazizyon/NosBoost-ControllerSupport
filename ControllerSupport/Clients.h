#pragma once
#include "Game.h"

namespace ControllerSupport::Clients {
    struct Client {
        DWORD pid;
        HWND window;
        std::wstring name;
    };

    void Publish(const std::string& characterName);
    std::vector<Client> List();
    void SwitchTo(const Client& client);
    void Withdraw();
}
