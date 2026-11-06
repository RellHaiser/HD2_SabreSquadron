// Windows XP's firewall (part 10d-vii): the Internet Connection Firewall through HNetCfg's NetSharingManager. A server
// or a client opens UDP ports port .. port + 3 on every firewalled connection (port mappings "HD2 port <n>"; a
// thread clicks away the small dialogs the change may raise) and removes them when the session ends; the main menu
// asks whether the firewall is on. See net_game.h.
#include <winsock2.h>
#include <windows.h>
#include <netcon.h>
#include <objbase.h>
#include <oleauto.h>

#include "net/dplay8.h"
#include "net/net_game.h"

#include <cstdio>

#include "game/game_app.h"
#include "game/pending.h"

namespace {

// The firewall's API (the original's copies at 0x008451dc .. 0x008451fc).
const CLSID CLSID_NetSharingManager_ = {0x5c63c1ad, 0x3956, 0x4ff8, {0x84, 0x86, 0x40, 0x03, 0x47, 0x58, 0x31, 0x5b}};
const IID IID_INetSharingManager_ = {0xc08956b7, 0x1cd3, 0x11d1, {0xb1, 0xc5, 0x00, 0x80, 0x5f, 0xc1, 0x27, 0x0e}};
const IID IID_INetConnection_ = {0xc08956a1, 0x1cd3, 0x11d1, {0xb1, 0xc5, 0x00, 0x80, 0x5f, 0xc1, 0x27, 0x0e}};
const IID IID_INetSharingPortMapping_ = {0xc08956b1, 0x1cd3, 0x11d1, {0xb1, 0xc5, 0x00, 0x80, 0x5f, 0xc1, 0x27, 0x0e}};
const IID IID_IEnumVARIANT_ = {0x00020404, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// The connections' characteristics a firewall shows in (NCCF_FIREWALLED and 0x100); the media of the shared access
// hosts (not mapped).
constexpr DWORD FIREWALL_CHARACTERISTICS = 0x500;
constexpr NETCON_MEDIATYPE MEDIA_SHARED_ACCESS_HOST_LAN = static_cast<NETCON_MEDIATYPE>(8);
constexpr NETCON_MEDIATYPE MEDIA_SHARED_ACCESS_HOST_RAS = static_cast<NETCON_MEDIATYPE>(9);
constexpr UCHAR PROTOCOL_UDP = 0x11;
// The dialogs the watcher clicks: shorter than this (px).
constexpr LONG SMALL_DIALOG_HEIGHT = 250;

// The firewall's state (0x0097cc00): the ports opened from, open, the watcher running.
struct S_FirewallState {
    int16_t port;
    bool open;
    bool watching;
};
S_FirewallState g_firewall;
// The small window the watcher found (0x0097d000).
HWND g_firewallDialog;

INetSharingManager *CreateSharingManager()
{
    CoInitialize(nullptr);
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                         EOAC_NONE, nullptr);
    INetSharingManager *manager = nullptr;
    NetCreateInstance(CLSID_NetSharingManager_, CLSCTX_ALL, IID_INetSharingManager_, reinterpret_cast<void **>(&manager));
    return manager;
}

BOOL __stdcall ClickChild(HWND window, LPARAM)                  // 0x00732050
{
    PostMessageA(g_firewallDialog, WM_COMMAND, IDOK, reinterpret_cast<LPARAM>(window));
    return TRUE;
}

BOOL __stdcall FindSmallWindow(HWND window, LPARAM)             // 0x00732080
{
    RECT rect;
    GetWindowRect(window, &rect);
    if (rect.bottom - rect.top < SMALL_DIALOG_HEIGHT)
        g_firewallDialog = window;
    return TRUE;
}

// While the ports are opened: the thread's small windows (the firewall's questions) answered OK every 200 ms.
DWORD __stdcall WatchDialogs(void *thread)                      // 0x007320c0
{
    const auto id = static_cast<DWORD>(reinterpret_cast<uintptr_t>(thread));
    while (g_firewall.watching) {
        EnumThreadWindows(id, FindSmallWindow, static_cast<LPARAM>(id));
        if (g_firewallDialog)
            EnumChildWindows(g_firewallDialog, ClickChild, static_cast<LPARAM>(id));
        Sleep(200);
    }
    return 0;
}

// The first item of `collection` (an object with _NewEnum at +0x1C) as an IEnumVARIANT; null for none.
IEnumVARIANT *EnumerateItems(IUnknown *collection)
{
    // (INetSharingEveryConnectionCollection / INetSharingPortMappingCollection: get__NewEnum.)
    IUnknown *items = nullptr;
    reinterpret_cast<INetSharingEveryConnectionCollection *>(collection)->get__NewEnum(&items);
    IEnumVARIANT *variants = nullptr;
    if (items) {
        items->QueryInterface(IID_IEnumVARIANT_, reinterpret_cast<void **>(&variants));
        items->Release();
    }
    return variants;
}

// UDP port `port` mapped on a connection's configuration when sharing or the firewall is on ("HD2 port <n>",
// enabled). S_OK when neither is.
HRESULT AddPortMapping(INetSharingConfiguration *config, int16_t port)   // 0x00732470 (__fastcall)
{
    VARIANT_BOOL sharing = VARIANT_FALSE;
    VARIANT_BOOL firewall = VARIANT_FALSE;
    config->get_SharingEnabled(&sharing);
    config->get_InternetFirewallEnabled(&firewall);
    if (!sharing && !firewall)
        return S_OK;
    char name[0x34];
    std::sprintf(name, "HD2 port %d", static_cast<int>(port));
    WCHAR wide[0x32];
    MultiByteToWideChar(CP_ACP, 0, name, -1, wide, 0x32);
    BSTR text = SysAllocString(wide);
    INetSharingPortMapping *mapping = nullptr;
    HRESULT hr = config->AddPortMapping(text, PROTOCOL_UDP, static_cast<USHORT>(port), static_cast<USHORT>(port), 0,
                                        const_cast<BSTR>(L"*"), ICSTT_IPADDRESS, &mapping);
    SysFreeString(text);
    if (mapping) {
        hr = mapping->Enable();
        mapping->Release();
    }
    return hr;
}

// Every connection looked at: `blocked` set by each one (firewalled or not; the last one decides); with a port, a
// firewalled one's configuration (not a shared access host's) maps port .. port + 3. The last result.
HRESULT MapConnections(INetSharingManager *manager, int16_t port, bool *blocked)   // 0x00732550 (__fastcall)
{
    INetSharingEveryConnectionCollection *connections = nullptr;
    HRESULT hr = manager->get_EnumEveryConnection(&connections);
    if (!connections)
        return hr;
    IUnknown *items = nullptr;
    connections->get__NewEnum(&items);
    IEnumVARIANT *variants = nullptr;
    if (items) {
        hr = items->QueryInterface(IID_IEnumVARIANT_, reinterpret_cast<void **>(&variants));
        items->Release();
    }
    if (variants) {
        VARIANT item;
        VariantInit(&item);
        while (variants->Next(1, &item, nullptr) == S_OK) {
            INetConnection *connection = nullptr;
            if (item.vt == VT_UNKNOWN)
                item.punkVal->QueryInterface(IID_INetConnection_, reinterpret_cast<void **>(&connection));
            if (connection) {
                INetConnectionProps *props = nullptr;
                manager->get_NetConnectionProps(connection, &props);
                if (props) {
                    DWORD characteristics = 0;
                    props->get_Characteristics(&characteristics);
                    if (!(characteristics & FIREWALL_CHARACTERISTICS)) {
                        *blocked = false;
                    } else if (port == 0) {
                        *blocked = true;
                    } else {
                        NETCON_MEDIATYPE media = NCM_NONE;
                        props->get_MediaType(&media);
                        if (media != MEDIA_SHARED_ACCESS_HOST_LAN && media != MEDIA_SHARED_ACCESS_HOST_RAS) {
                            INetSharingConfiguration *config = nullptr;
                            hr = manager->get_INetSharingConfigurationForINetConnection(connection, &config);
                            if (config) {
                                AddPortMapping(config, port);
                                AddPortMapping(config, static_cast<int16_t>(port + 1));
                                AddPortMapping(config, static_cast<int16_t>(port + 2));
                                hr = AddPortMapping(config, static_cast<int16_t>(port + 3));
                                config->Release();
                            }
                        }
                    }
                    props->Release();
                }
                connection->Release();
            }
            VariantClear(&item);
        }
        variants->Release();
    }
    connections->Release();
    return hr;
}

// The first firewalled connection's (not a shared access host's) mapping of `protocol` and external `port` deleted.
// E_FAIL without such a connection.
HRESULT RemovePortMapping(INetSharingManager *manager, UCHAR protocol, int port)   // 0x00732120 (__fastcall + 1)
{
    INetSharingEveryConnectionCollection *connections = nullptr;
    manager->get_EnumEveryConnection(&connections);
    INetConnection *connection = nullptr;
    if (connections) {
        IEnumVARIANT *variants = EnumerateItems(connections);
        if (variants) {
            VARIANT item;
            VariantInit(&item);
            bool found = false;
            while (!found && variants->Next(1, &item, nullptr) == S_OK) {
                if (item.vt == VT_UNKNOWN) {
                    item.punkVal->QueryInterface(IID_INetConnection_, reinterpret_cast<void **>(&connection));
                    if (connection) {
                        INetConnectionProps *props = nullptr;
                        manager->get_NetConnectionProps(connection, &props);
                        if (props) {
                            DWORD characteristics = 0;
                            props->get_Characteristics(&characteristics);
                            if (characteristics & FIREWALL_CHARACTERISTICS) {
                                NETCON_MEDIATYPE media = NCM_NONE;
                                props->get_MediaType(&media);
                                found = media != MEDIA_SHARED_ACCESS_HOST_LAN && media != MEDIA_SHARED_ACCESS_HOST_RAS;
                            }
                            props->Release();
                        }
                        if (!found) {
                            connection->Release();
                            connection = nullptr;
                        }
                    }
                }
                VariantClear(&item);
            }
            variants->Release();
        }
        connections->Release();
    }
    if (!connection)
        return E_FAIL;
    INetSharingConfiguration *config = nullptr;
    HRESULT hr = manager->get_INetSharingConfigurationForINetConnection(connection, &config);
    connection->Release();
    if (!config)
        return hr;
    INetSharingPortMappingCollection *mappings = nullptr;
    hr = config->get_EnumPortMappings(ICSSC_DEFAULT, &mappings);
    if (mappings) {
        INetSharingPortMapping *mapping = nullptr;
        IUnknown *items = nullptr;
        hr = mappings->get__NewEnum(&items);
        IEnumVARIANT *variants = nullptr;
        if (items) {
            hr = items->QueryInterface(IID_IEnumVARIANT_, reinterpret_cast<void **>(&variants));
            items->Release();
        }
        if (variants) {
            VARIANT item;
            VariantInit(&item);
            bool found = false;
            while (!found && variants->Next(1, &item, nullptr) == S_OK) {
                if (item.vt == VT_DISPATCH) {
                    item.pdispVal->QueryInterface(IID_INetSharingPortMapping_, reinterpret_cast<void **>(&mapping));
                    if (mapping) {
                        INetSharingPortMappingProps *props = nullptr;
                        hr = mapping->get_Properties(&props);
                        if (props) {
                            UCHAR itsProtocol = 0;
                            props->get_IPProtocol(&itsProtocol);
                            long external = 0;
                            props->get_ExternalPort(&external);
                            found = itsProtocol == protocol && external == static_cast<int16_t>(port);
                            props->Release();
                        }
                        if (!found) {
                            mapping->Release();
                            mapping = nullptr;
                        }
                    }
                }
                VariantClear(&item);
            }
            variants->Release();
        }
        if (mapping) {
            hr = mapping->Delete();
            mapping->Release();
        }
        mappings->Release();
    }
    config->Release();
    return hr;
}

}  // namespace

void C_NetGame::ConfigureFirewall(int port, bool open)
{
    if (!NetFirewallAvailable() || g_firewall.open == open)
        return;
    INetSharingManager *manager = CreateSharingManager();
    if (!manager)
        return;
    if (open) {
        if (!(g_gameRole & GAME_ROLE_CLIENT))
            GetGameApp()->PrintMessage("Firewall settings have been modified!");
        g_firewall.watching = true;
        DWORD thread;
        CreateThread(nullptr, 0, WatchDialogs, reinterpret_cast<void *>(static_cast<uintptr_t>(GetCurrentThreadId())), 0,
                     &thread);
        g_firewall.port = static_cast<int16_t>(port);
        bool blocked;
        MapConnections(manager, static_cast<int16_t>(port), &blocked);
        g_firewall.watching = false;
        g_firewall.open = true;
    } else {
        for (int i = 0; i < 4; ++i)
            RemovePortMapping(manager, PROTOCOL_UDP, g_firewall.port + i);
        g_firewall.open = false;
    }
    manager->Release();
}

bool C_NetGame::FirewallBlocks(uint16_t)
{
    if (!NetFirewallAvailable())
        return false;
    INetSharingManager *manager = CreateSharingManager();
    bool blocked = false;
    if (manager) {
        MapConnections(manager, 0, &blocked);
        manager->Release();
    }
    return blocked;
}
