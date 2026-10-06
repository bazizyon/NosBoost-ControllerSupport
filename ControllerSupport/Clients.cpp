#include "Clients.h"

namespace ControllerSupport::Clients {
    // Every running client keeps its own row here, the others read it to know who is open.
    struct Row {
        volatile LONG pid;
        DWORD window;
        DWORD tick;
        wchar_t name[32];
    };

    struct Table {
        Row rows[16];
    };

    constexpr DWORD Stale = 5000;

    HANDLE Mapping = nullptr;
    Table* Shared = nullptr;
    HWND OwnWindow = nullptr;
    DWORD LastPublish = 0;

    Table* Open() {
        if (Shared) return Shared;
        Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Table), L"Local\\NosBoostGamepadClients");
        if (!Mapping) return nullptr;
        Shared = static_cast<Table*>(MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Table)));
        return Shared;
    }

    BOOL CALLBACK FindOwn(HWND window, LPARAM) {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) || !GetWindowTextLengthW(window)) return TRUE;
        OwnWindow = window;
        return FALSE;
    }

    bool Fresh(const Row& row, const DWORD now) {
        return row.pid && now - row.tick < Stale;
    }

    // The Gameforge account name, shown until a character is in game.
    std::string AccountName() {
        const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleA("psw_tnt.dll"));
        if (!module) return {};
        const auto* text = reinterpret_cast<const char*>(module + 0x385EB);
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(text, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return {};
        std::string name;
        for (int i = 0; i < 31 && text[i]; i++) {
            if (static_cast<unsigned char>(text[i]) < 0x20) return {};
            name.push_back(text[i]);
        }
        return name;
    }

    void Publish(const std::string& characterName) {
        const DWORD now = GetTickCount();
        if (now - LastPublish < 500) return;
        LastPublish = now;
        Table* table = Open();
        if (!table) return;
        if (!OwnWindow || !IsWindow(OwnWindow)) {
            OwnWindow = nullptr;
            EnumWindows(FindOwn, 0);
        }
        const auto me = static_cast<LONG>(GetCurrentProcessId());
        Row* own = nullptr;
        for (Row& row : table->rows) {
            if (row.pid == me) own = &row;
        }
        for (Row& row : table->rows) {
            if (own) break;
            const LONG seen = row.pid;
            if (Fresh(row, now) || InterlockedCompareExchange(&row.pid, me, seen) != seen) continue;
            own = &row;
        }
        if (!own) return;
        own->window = static_cast<DWORD>(reinterpret_cast<uintptr_t>(OwnWindow));
        wchar_t name[32]{};
        const std::string shown = characterName.empty() ? AccountName() : characterName;
        if (!shown.empty()) MultiByteToWideChar(CP_ACP, 0, shown.c_str(), static_cast<int>(shown.size()), name, 31);
        std::memcpy(own->name, name, sizeof(name));
        own->tick = now;
    }

    std::vector<Client> List() {
        std::vector<Client> clients;
        Table* table = Open();
        if (!table) return clients;
        const DWORD now = GetTickCount();
        for (const Row& row : table->rows) {
            if (!Fresh(row, now) || !row.window) continue;
            Client client{static_cast<DWORD>(row.pid), reinterpret_cast<HWND>(static_cast<uintptr_t>(row.window)), {}};
            client.name.assign(row.name, wcsnlen(row.name, 32));
            clients.push_back(std::move(client));
        }
        std::ranges::sort(clients, [](const Client& a, const Client& b) { return a.pid < b.pid; });
        return clients;
    }

    void SwitchTo(const Client& client) {
        if (!IsWindow(client.window)) return;
        if (IsIconic(client.window)) ShowWindow(client.window, SW_RESTORE);
        SetForegroundWindow(client.window);
    }

    void Withdraw() {
        if (!Shared) return;
        for (Row& row : Shared->rows) {
            if (row.pid == static_cast<LONG>(GetCurrentProcessId())) row.pid = 0;
        }
        UnmapViewOfFile(Shared);
        CloseHandle(Mapping);
        Shared = nullptr;
        Mapping = nullptr;
    }
}
