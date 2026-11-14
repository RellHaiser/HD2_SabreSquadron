// The network game's DirectPlay session (part 10d-iv): hosting, the dedicated server's process, the client's object
// and addresses, the hosts' enumeration (searches, pings, status queries), connecting, the client's DirectPlay
// messages, the replication made, the voice chat, the bandwidth, a server status in a message. See net_game.h.
#define _WINSOCK_DEPRECATED_NO_WARNINGS             // (inet_ntoa, as the original)
#include <winsock2.h>
#include <windows.h>

#include "net/dplay8.h"
#include "net/net_autorestart.h"
#include "net/net_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/actor.h"
#include "game/actors_module.h"
#include "game/console.h"
#include "game/controller_net.h"
#include "game/car.h"
#include "game/controls.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_state.h"
#include "game/human.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/seat_weapon.h"
#include "game/sound_module.h"
#include "game/text_box.h"
#include "ls3d/igraph.h"
#include "net/net_channel.h"
#include "net/net_object.h"
#include "net/net_socket.h"
#include "net/net_track.h"
#include "net/net_transport.h"

namespace {

// The port the game uses without GameSpy's listing.
constexpr int kDefaultPort = 0x7B4F;
// DirectPlay's codes and flags the session uses.
constexpr HRESULT DPNSUCCESS_PENDING = 0x0015800E;
constexpr HRESULT DPNERR_ENUMQUERYTOOLARGE_ = static_cast<HRESULT>(0x80158040);
constexpr HRESULT DPNERR_USERCANCEL_ = static_cast<HRESULT>(0x80158320);
constexpr HRESULT E_PENDING_ = static_cast<HRESULT>(0x8000000A);
constexpr DWORD DPNA_DATATYPE_STRING = 1;
constexpr DWORD DPNA_DATATYPE_DWORD = 2;
constexpr DWORD DPNSESSION_CLIENT_SERVER = 1;
constexpr DWORD DPNSESSION_NODPNSVR = 0x40;
constexpr DWORD DPNINFO_NAME = 1;
constexpr DWORD DPNSETCLIENTINFO_SYNC = 0x80000000;

S_ServerEntry &Entry(uint8_t *storage)
{
    return *reinterpret_cast<S_ServerEntry *>(storage);
}

// The text set as the last failure's (with its result).
long Fail(C_NetGame &net, long hr, const char *text)
{
    net.m_lastError = hr;
    net.m_lastErrorText = text;
    return hr;
}

// The caps both sides set: 2 s to connect, retried for ever, kept alive after 4 s.
DPN_CAPS SessionCaps()
{
    DPN_CAPS caps;
    caps.size = sizeof caps;
    caps.flags = 0;
    caps.connectTimeout = 2000;
    caps.connectRetries = static_cast<DWORD>(-1);
    caps.timeoutUntilKeepAlive = 4000;
    return caps;
}

// The player's name as DirectPlay's wide text (the Unicode font: from UTF-8).
void WideName(wchar_t *out, const char *name)
{
    if (g_gameProfile.UsesUnicodeFont())
        NetUtf8ToWide(out, name, static_cast<unsigned>(-1));
    else
        NetAnsiToWide(out, name, static_cast<unsigned>(-1));
}

// The replication's factories by the object's id: a car's (the seat weapons' messages, the car's state); a human's
// or a player's (the controllers'); none for the other actors.
NetMessageFactory MakeObjectFactory(int id)             // 0x0072d590 (DS 0x00786eb0)
{
    if (id >= 32) {
        C_Actor *actor = g_actorsModule.GetActor(id);
        if (actor && actor->GetType() != ACTOR_HUMAN) {
            if (actor->GetType() != ACTOR_CAR)
                return nullptr;
            return MakeSeatWeaponMessage;
        }
    }
    return NetCreateMessage;
}
NetStateHandler MakeObjectStateReader(int id)           // 0x0072d5c0 (DS 0x00786ef0)
{
    if (id >= 32) {
        C_Actor *actor = g_actorsModule.GetActor(id);
        if (actor && actor->GetType() != ACTOR_HUMAN) {
            if (actor->GetType() != ACTOR_CAR)
                return nullptr;
            return CarNetReadState;
        }
    }
    return NetReadControlBits;
}

}  // namespace

// ---- text ----------------------------------------------------------------------------------------------------------

void NetAnsiToWide(wchar_t *out, const char *text, unsigned length)
{
    if (!out || !text)
        return;
    if (length == static_cast<unsigned>(-1))
        length = static_cast<unsigned>(std::strlen(text)) + 1;
    MultiByteToWideChar(CP_ACP, 0, text, -1, out, static_cast<int>(length - 1));
    out[length - 1] = 0;
}

void NetWideToAnsi(char *out, const wchar_t *text, int length)
{
    if (!out || !text)
        return;
    if (length == -1)
        length = static_cast<int>(wcslen(text)) + 1;
    WideCharToMultiByte(CP_ACP, 0, text, -1, out, length - 1, nullptr, nullptr);
    out[length - 1] = 0;
}

void NetUtf8ToWide(wchar_t *out, const char *text, unsigned length)
{
    if (!out || !text)
        return;
    if (length == static_cast<unsigned>(-1))
        length = static_cast<unsigned>(std::strlen(text)) + 1;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out, static_cast<int>(length - 1));
    out[length - 1] = 0;
}

void NetWideToUtf8(char *out, const wchar_t *text, int length)
{
    if (!out || !text)
        return;
    if (length == -1)
        length = static_cast<int>(wcslen(text)) + 1;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out, length - 1, nullptr, nullptr);
    out[length - 1] = 0;
}

bool DirectPlayMissing()
{
    return false;
}

uint32_t GetHostAddress(IDirectPlay8Address *address)
{
    wchar_t name[0x32] = {};
    DWORD size = 0x32;
    DWORD type = 0;
    address->GetComponentByName(L"hostname", name, &size, &type);
    char text[72];
    NetWideToAnsi(text, name, -1);
    unsigned a = 0, b = 0, c = 0, d = 0;
    std::sscanf(text, "%u.%u.%u.%u", &a, &b, &c, &d);
    return ((d << 8 | c) << 8 | b) << 8 | a;
}

// ---- hosting -------------------------------------------------------------------------------------------------------

int C_NetGame::Host(const S_NetSessionDesc &desc, bool firewall)
{
    Close(true);
#ifdef HD2_DEDICATED
    GetSoundModule()->SetBackgroundMusic(-1, 0);   // (the server's is empty)
    // (DS 0x007798a4) The session's settings in a file, for a server started again; `autorestart`: the watchdog told.
    char file[52];
    std::sprintf(file, "XDServer_%5d.txt", g_gameProfile.m_mpSettings->port);
    g_gameProfile.m_mpSettings->WriteDescription(file);
    if (g_gameProfile.m_mpSettings->autoRestart) {
        StartAutoRestartWatchdog();
        Sleep(100);
        WatchSession(g_gameProfile.m_mpSettings->sessionName);
    }
#endif
    int port = kDefaultPort;
    if (g_gameProfile.m_mpSettings->unknown174 == 1)
        port = g_gameProfile.m_mpSettings->port;
    S_ServerEntry &entry = Entry(m_serverEntry);
    std::strcpy(entry.name, desc.name);
    entry.address = 0;
    entry.port = g_gameProfile.m_mpSettings->port;
    m_unknown448 = 0;
    m_unknown8 = false;
    CoInitialize(nullptr);
    IDirectPlay8Address *address = nullptr;
    if (DirectPlayMissing())
        return -1;
    GetIGraph()->SetFlags(g_graphFlags & ~0x20u);
    HRESULT hr = NetCreateInstance(CLSID_NetDirectPlay8Server, CLSCTX_INPROC_SERVER, IID_NetDirectPlay8Server,
                                   reinterpret_cast<void **>(&m_server));
    if (hr < 0) {
        if (m_server) {
            m_server->Release();
            m_server = nullptr;
        }
        return Fail(*this, hr, "DirectX 8 not installed. Can't create server");
    }
    hr = m_server->Initialize(nullptr, ServerMessageHandler, 0);
    if (hr < 0) {
        m_server->Release();
        m_server = nullptr;
        return Fail(*this, hr, "Can't initialize server");
    }
    // The service provider's first device.
    const auto *provider = reinterpret_cast<const GUID *>(desc.serviceProvider);
    DWORD size = 0;
    DWORD count = 0;
    m_server->EnumServiceProviders(provider, nullptr, nullptr, &size, &count, 0);
    auto *devices = static_cast<uint8_t *>(operator new(size));
    hr = m_server->EnumServiceProviders(provider, nullptr, devices, &size, &count, 0);
    if (!devices || hr < 0) {
        // (Fix: the buffer freed; the original leaked it.)
        operator delete(devices);
        m_server->CancelAsyncOperation(0, DPNCANCEL_ALL_OPERATIONS);
        m_server->Close(0);
        m_server->Release();
        m_server = nullptr;
        return Fail(*this, hr, "Enumeration of devices failed.");
    }
    GUID device;
    std::memcpy(&device, devices + 4, sizeof device);
    operator delete(devices);
    hr = NetCreateInstance(CLSID_NetDirectPlay8Address, CLSCTX_ALL, IID_NetDirectPlay8Address,
                           reinterpret_cast<void **>(&address));
    if (hr < 0) {
        if (address)
            address->Release();
        m_server->Release();
        m_server = nullptr;
        return Fail(*this, hr, "Can't create address object");
    }
    hr = address->SetSP(provider);
    if (hr < 0) {
        address->Release();
        m_server->Release();
        m_server = nullptr;
        return Fail(*this, hr, "Can't set self address");
    }
    address->SetDevice(&device);
    if (std::memcmp(provider, &CLSID_NetDP8SP_TCPIP, sizeof(GUID)) == 0 && port != 0) {
        const DWORD value = static_cast<DWORD>(port);
        address->AddComponent(L"port", &value, sizeof value, DPNA_DATATYPE_DWORD);
    }
    wchar_t name[200];
    WideName(name, desc.name);
    DPN_APPLICATION_DESC application;
    // (Fix: without DPNSVR. On current Windows, with DirectPlay's address family forced to IPv4, Host fails with
    // 0x80158185 and nothing is bound; clients always name the port, so they don't need it. The original: only the first.)
    application.flags = DPNSESSION_CLIENT_SERVER | DPNSESSION_NODPNSVR;
    application.application = g_netApplicationGuid;
    application.sessionName = name;
    DPN_CAPS caps = SessionCaps();
    m_server->SetCaps(&caps, 0);
    char line[1000];
    std::sprintf(line, "Creating H&D2 Server v%d.%02d", g_versionMajor, g_versionMinor);
    GetGameApp()->PrintMessage(line);
    g_gameRole = g_gameProfile.m_mpSettings->dedicated ? GAME_ROLE_SERVER : GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    hr = m_server->Host(&application, reinterpret_cast<void **>(&address), 1, nullptr, nullptr, nullptr, 0);
    if (hr < 0) {
        if (address)
            address->Release();
        m_server->Release();
        m_server = nullptr;
        g_gameRole = GAME_ROLE_LOCAL;
        return Fail(*this, hr, "Server::Host failed");
    }
    if (firewall)
        ConfigureFirewall(port, true);
    m_string501C.assign(desc.password);
    g_localPlayerSlot = g_gameProfile.m_mpSettings->dedicated ? -1 : 0;
    if (!g_gameProfile.m_mpSettings->dedicated) {
        m_playersLock.Enter();
        std::strncpy(m_players[0].name, g_gameProfile.GetPlayerProfile()->GetProfileName().c_str(), 0x32);
        m_players[0].left = true;
        m_playersLock.Leave();
    } else {
        // A dedicated server: nothing shown.
#ifndef HD2_DEDICATED   // no window or input in LS3DRF.dll
        GetIGraph()->InitMouse(0);
        ShowWindow(GetIGraph()->GetHWND(), SW_HIDE);
#endif
    }
    if (address)
        address->Release();
    m_joinStatus = NET_JOIN_CONNECTED;
    if (g_gameProfile.m_mpSettings->unknown178) {
        StartVoiceServer();
        if ((g_gameRole & GAME_ROLE_CLIENT) && g_gameProfile.GetDetailSettings()->voiceChat)
            StartVoiceClient(true);
    }
    if (g_gameProfile.m_mpSettings->unknown174 == 1)
        StartReporting();
    MakeReplication();
    m_transport->Start(port + 1);
    return hr;
}

bool C_NetGame::HostDedicated(bool firewall)
{
    int port = kDefaultPort;
    if (g_gameProfile.m_mpSettings->unknown174 == 1)
        port = g_gameProfile.m_mpSettings->port;
    if (firewall)
        ConfigureFirewall(port, true);
    char file[52];
    std::sprintf(file, "XDServer_%5d.txt", g_gameProfile.m_mpSettings->port);
    g_gameProfile.m_mpSettings->WriteDescription(file);
    char command[256];
    std::sprintf(command, "hd2ds_sabresquadron.exe -cmd -exec %s", file);
    STARTUPINFOA startup;
    GetStartupInfoA(&startup);
    PROCESS_INFORMATION process;
    if (!CreateProcessA("hd2ds_sabresquadron.exe", command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                        &process))
        return false;
    GetGameApp()->m_state->SetPendingState(4);
    return true;
}

// ---- the client's side ----------------------------------------------------------------------------------------------

long C_NetGame::CreateClient()
{
    if (m_client)
        return 0;
    HRESULT hr = NetCreateInstance(CLSID_NetDirectPlay8Client, CLSCTX_INPROC_SERVER, IID_NetDirectPlay8Client,
                                   reinterpret_cast<void **>(&m_client));
    if (hr < 0)
        return Fail(*this, hr, "DirectX 8 not installed. Can't create client");
    hr = m_client->Initialize(nullptr, ClientMessageHandler, 0);
    if (hr < 0) {
        if (m_client) {
            m_client->Release();
            m_client = nullptr;
        }
        return Fail(*this, hr, "Client::Initialize failed");
    }
    return 0;
}

void C_NetGame::CreateAddresses(const S_ServerEntry *server, IDirectPlay8Address **device, IDirectPlay8Address **host)
{
    NetCreateInstance(CLSID_NetDirectPlay8Address, CLSCTX_ALL, IID_NetDirectPlay8Address, reinterpret_cast<void **>(device));
    (*device)->SetSP(&CLSID_NetDP8SP_TCPIP);
    NetCreateInstance(CLSID_NetDirectPlay8Address, CLSCTX_ALL, IID_NetDirectPlay8Address, reinterpret_cast<void **>(host));
    (*host)->SetSP(&CLSID_NetDP8SP_TCPIP);
    if (server) {
        in_addr address;
        address.s_addr = server->address;
        wchar_t name[20];
        NetAnsiToWide(name, inet_ntoa(address), static_cast<unsigned>(-1));
        (*host)->AddComponent(L"hostname", name, static_cast<DWORD>(wcslen(name) * 2 + 2), DPNA_DATATYPE_STRING);
    }
    const DWORD port = static_cast<DWORD>(server ? server->port : kDefaultPort);
    (*host)->AddComponent(L"port", &port, sizeof port, DPNA_DATATYPE_DWORD);
}

int C_NetGame::EnumHosts(S_ServerEntry *server, int request)
{
    IDirectPlay8Address *device = nullptr;
    IDirectPlay8Address *host = nullptr;
    if (DirectPlayMissing())
        return -1;
    CoInitializeEx(nullptr, 0);
    long hr = CreateClient();
    if (hr < 0)
        return Fail(*this, hr, "Client::Create failed");
    CreateAddresses(server, &device, &host);
    DPN_APPLICATION_DESC application;
    application.application = g_netApplicationGuid;
    // A server: asked once (1 s); the LAN: until cancelled.
    hr = m_client->EnumHosts(&application, host, device, &request, sizeof request, server ? 1 : static_cast<DWORD>(-1), 0,
                             server ? 1000 : static_cast<DWORD>(-1), server, &m_enumHandle, 0);
    if (device) {
        device->Release();
        device = nullptr;
    }
    if (host) {
        host->Release();
        host = nullptr;
    }
    if (hr == 0 || hr == DPNSUCCESS_PENDING || hr == DPNERR_ENUMQUERYTOOLARGE_ || hr == DPNERR_USERCANCEL_) {
        g_gameRole = 0x12;
        return 0;
    }
    m_client->CancelAsyncOperation(0, DPNCANCEL_ALL_OPERATIONS);
    m_client->Close(0);
    if (m_client) {
        m_client->Release();
        m_client = nullptr;
    }
    return Fail(*this, hr, "Client::EnumHosts failed");
}

void C_NetGame::PingServer(void(__fastcall *callback)(S_ServerEntry *server), S_ServerEntry *server)
{
    m_searchCallback = callback;
    EnumHosts(server, 0);
}

void C_NetGame::PingServers(void(__fastcall *callback)(S_ServerEntry *server), std::vector<S_ServerEntry *> &servers)
{
    m_searchCallback = callback;
    for (size_t i = 0; i < servers.size(); ++i)
        EnumHosts(servers[i], 2);
}

int C_NetGame::QueryServerStatus(void(__fastcall *callback)(S_ServerStatus *status), S_ServerEntry *server)
{
    m_statusCallback = callback;
    return EnumHosts(server, 1);
}

int C_NetGame::Connect(S_ServerEntry *server)
{
    if (DirectPlayMissing())
        return -1;
    GetIGraph()->SetFlags(g_graphFlags & ~0x20u);
    std::memcpy(m_serverEntry, server, sizeof(S_ServerEntry));
    if (g_gameProfile.m_mpSettings->unknown174 == 1) {
        // GameSpy's listing: the CD key checked with the server first.
        m_joinStatus = 0;
        StartKeyQuery(server);
        RunKeyQuery();
        if (m_joinStatus == NET_JOIN_TIMED_OUT || m_joinStatus == NET_JOIN_BAD_CD_KEY)
            return 0;
    }
    if (g_gameProfile.m_mpSettings->unknown174 != 1)
        server->port = kDefaultPort;
    ConfigureFirewall(static_cast<int16_t>(server->port), true);
    const int port = server->port;
    m_transport->Start(-1);
    m_unknown338 = static_cast<int>(server->address);
    m_serverLink = new C_NetConnection(0, static_cast<uint32_t>(m_unknown338), &m_transport->m_socket);
    m_serverLink->m_port = port + 1;
    m_transport->Add(m_serverLink);
    m_transport->FlushAll(true);
    CreateClient();
    m_client->CancelAsyncOperation(0, DPNCANCEL_ALL_OPERATIONS);
    m_unknown448 = 0;
    m_unknown8 = false;
    m_joinTime = 0;
    const char *playerName = g_gameProfile.GetPlayerProfile()->GetProfileName().c_str();
    wchar_t name[112];
    WideName(name, playerName);
    DPN_PLAYER_INFO info = {};
    info.size = sizeof info;
    info.infoFlags = DPNINFO_NAME;
    info.name = name;
    HRESULT hr = m_client->SetClientInfo(&info, nullptr, nullptr, DPNSETCLIENTINFO_SYNC);
    if (hr < 0)
        return Fail(*this, hr, "SetClientInfo failed");
    DPN_APPLICATION_DESC application;
    application.application = g_netApplicationGuid;
    g_gameRole = GAME_ROLE_CLIENT;
    IDirectPlay8Address *device = nullptr;
    IDirectPlay8Address *host = nullptr;
    CreateAddresses(server, &device, &host);
    S_JoinRequest request;
    std::strncpy(request.password, g_gameProfile.m_mpSettings->password.c_str(), sizeof request.password);
    request.versionMajor = g_versionMajor;
    request.versionMinor = g_versionMinor;
    request.unknown1C = 2;
    // (Fix: the name within its room; the original's strcpy took any length.)
    std::strncpy(request.name, playerName, sizeof request.name - 1);
    request.name[sizeof request.name - 1] = 0;
    DPN_CAPS caps = SessionCaps();
    m_client->SetCaps(&caps, 0);
    DPNHANDLE handle = 0;
    hr = m_client->Connect(&application, host, device, nullptr, nullptr, &request, sizeof request, nullptr, &handle, 0);
    m_joinStatus = NET_JOIN_JOINING;
    if (device) {
        device->Release();
        device = nullptr;
    }
    if (host)
        host->Release();
    if (hr != E_PENDING_ && hr < 0) {
        g_gameRole = GAME_ROLE_LOCAL;
        Fail(*this, hr, "Connect failed");
    }
    return hr;
}

void C_NetGame::OnConnected()
{
    m_joinStatus = NET_JOIN_CONNECTED;
    if (Entry(m_serverEntry).voice && g_gameProfile.GetDetailSettings()->voiceChat)
        StartVoiceClient(false);
    MakeReplication();
}

long __stdcall ClientMessageHandler(void *context, unsigned long type, void *message)
{
    if (g_gameRole == GAME_ROLE_LOCAL)
        return 0;
    return C_NetGame::HandleClientMessage(&g_netGame, context, type, message);
}

long __stdcall ServerMessageHandler(void *context, unsigned long type, void *message)
{
    if (!(g_gameRole & GAME_ROLE_SERVER))
        return 0;
    return C_NetGame::HandleServerMessage(&g_netGame, context, type, message);
}

long __stdcall VoiceClientHandler(void *, unsigned long, void *)
{
    return 0;
}

long __stdcall VoiceServerHandler(void *, unsigned long, void *)
{
    return 0;
}

long __stdcall C_NetGame::HandleClientMessage(C_NetGame *net, void *, uint32_t type, void *message)
{
    switch (type) {
    case DPN_MSGID_CONNECT_COMPLETE: {
        // Accepted: the slot the server gave in the reply; refused: the reason (its join status).
        const auto *complete = static_cast<const DPNMSG_CONNECT_COMPLETE *>(message);
        if (complete->result == 0) {
            g_localPlayerSlot = complete->replyData[0] & 0x7F;
            net->m_serverLink->m_id = g_localPlayerSlot;
        } else if (complete->replySize != 0) {
            net->m_joinStatus = complete->replyData[0];
        }
        break;
    }
    case DPN_MSGID_ENUM_HOSTS_RESPONSE: {
        // A host's answer (not in a game): the server's entry (the one pinged, or a new one) from the answer's data;
        // a search's or a ping's to the search callback, a status query's to the status callback.
        if (GetGame())
            break;
        const auto *response = static_cast<const DPNMSG_ENUM_HOSTS_RESPONSE *>(message);
        auto *entry = static_cast<S_ServerEntry *>(response->context);
        if (!entry) {
            entry = new S_ServerEntry;
            entry->Reset();
            *reinterpret_cast<uint32_t *>(entry->name) = 0;
        }
        C_GrowingBitStream data;
        data.Attach(response->responseData, response->responseSize * 8);
        entry->ping = response->roundTripLatency;
        const wchar_t *name = response->application->sessionName;
        if (HasCjkCharacters(reinterpret_cast<const uint16_t *>(name)))
            NetWideToUtf8(entry->name, name, 0x32);
        else
            NetWideToAnsi(entry->name, name, 0x32);
        entry->address = GetHostAddress(response->sender);
        const uint32_t kind = data.ReadBits(2);
        data.ReadString(entry->map, 0x32);
        const uint32_t afterMap = data.BitPosition();
        entry->gameType = static_cast<int>(data.ReadBits(2));
        entry->dedicated = data.ReadBits(1) != 0;
        entry->maxPlayers = data.ReadBits(6);
        entry->players = data.ReadBits(6);
        entry->password = data.ReadBits(1) != 0;
        entry->versionMinor = static_cast<int>(data.ReadBits(8));
        entry->versionMajor = static_cast<int>(data.ReadBits(8));
        entry->port = static_cast<int16_t>(data.ReadBits(16));
        entry->internet = false;
        entry->voice = data.ReadBits(1) != 0;
        entry->unknown94 = 1;
        if (entry->versionMajor != 1 || entry->versionMinor > 20) {
            // A newer server's layout: the version first, a 3-bit game type, the expansion.
            data.SetBitPosition(afterMap);
            entry->versionMinor = static_cast<int>(data.ReadBits(8));
            entry->versionMajor = static_cast<int>(data.ReadBits(8));
            entry->gameType = static_cast<int>(data.ReadBits(3));
            entry->dedicated = data.ReadBits(1) != 0;
            entry->maxPlayers = data.ReadBits(6);
            entry->players = data.ReadBits(6);
            entry->password = data.ReadBits(1) != 0;
            entry->port = static_cast<int16_t>(data.ReadBits(16));
            entry->internet = false;
            entry->voice = data.ReadBits(1) != 0;
            if (std::strstr(entry->name, "DEMO"))
                entry->unknown94 = 2;
            else
                entry->unknown94 = static_cast<int>(data.ReadBits(2));
        }
        if (kind == 0) {
            net->m_searchCallback(entry);
        } else if (kind == 1) {
            net->m_status.Read(&data);
            net->m_statusCallback(&net->m_status);
            return 1;
        }
        break;
    }
    case DPN_MSGID_RECEIVE: {
        const auto *receive = static_cast<const DPNMSG_RECEIVE *>(message);
        if (receive->dataSize != 0)
            net->QueueOutgoing(receive->data, static_cast<int>(receive->dataSize), 0x80);
        break;
    }
    case DPN_MSGID_RETURN_BUFFER:
        operator delete(static_cast<const DPNMSG_RETURN_BUFFER *>(message)->buffer);
        break;
    case DPN_MSGID_TERMINATE_SESSION:
        // The session ended: the server's link gone, back to the menus.
        if (net->m_serverLink) {
            net->m_transport->Remove(net->m_serverLink);
            delete net->m_serverLink;
            net->m_serverLink = nullptr;
        }
        net->m_joinStatus = 0;
        if (GetGameApp()->m_state)
            GetGameApp()->m_state->SetPendingState(0xB);
        break;
    default:
        break;
    }
    return 0;
}

// ---- the replication ---------------------------------------------------------------------------------------------------

void C_NetGame::MakeReplication()
{
    DeleteReplication();
    g_netReplication = new C_NetReplication;
    g_netReplication->SetFactories(MakeObjectStateReader, MakeObjectFactory);
    // The positions (3 km from -1 km, 1 cm), the deltas (0.6 from -0.3; 5 mm, 7 mm up), the rates (15 from -7.5,
    // 0.1).
    S_NetQuantization *value = &m_quantizations[0];
    S_NetQuantization *rate = &m_quantizations[1];
    S_NetQuantization *delta = &m_quantizations[2];
    value->Setup({3000.0f, 3000.0f, 3000.0f, 0.0f}, {-1000.0f, -1000.0f, -1000.0f, 0.0f}, {0.01f, 0.01f, 0.01f, 0.0f}, 100);
    delta->Setup({0.6f, 0.6f, 0.6f, 0.0f}, {-0.3f, -0.3f, -0.3f, 0.0f}, {0.005f, 0.007f, 0.005f, 0.0f}, 100);
    rate->Setup({15.0f, 15.0f, 15.0f, 0.0f}, {-7.5f, -7.5f, -7.5f, 0.0f}, {0.1f, 0.1f, 0.1f, 0.0f}, 100);
    g_netReplication->SetReadQuantizations(value, delta, rate);
    g_netReplication->SetWriteQuantizations(value, delta, rate);
    g_netReplication->SetField2(0x3B);
    g_netReplication->GetLocalObject().SetPropertyField2(0x3B);
    g_netReplication->GetLocalObject().SetId(g_localPlayerSlot);
    g_netReplication->SetLag(0.035f);
    g_netServerSide = (g_gameRole & GAME_ROLE_SERVER) != 0;
}

void C_NetGame::DeleteReplication()
{
    if (g_netReplication) {
        delete g_netReplication;
        g_netReplication = nullptr;
    }
}

// ---- the voice chat -------------------------------------------------------------------------------------------------------

void C_NetGame::StartVoiceServer()
{
    const int codec = g_gameProfile.m_mpSettings->unknown178;
    if (!codec)
        return;
    m_voiceServer = new C_NetVoice(nullptr, VoiceServerHandler);
    const GUID &guid = codec >= 1 && codec <= 7 ? g_netVoiceCodecs[codec] : g_netVoiceCodecs[0];
#ifndef HD2_DEDICATED
    m_voiceServer->Init(GetIGraph()->GetHWND(), true, false, m_server, 3, &guid, nullptr, 0);
#else   // the server's window: its console
    m_voiceServer->Init(GetConsole()->GetWindow(), true, false, m_server, 3, &guid, nullptr, 0);
#endif
}

void C_NetGame::StartVoiceClient(bool onServer)
{
    m_voiceClient = new C_NetVoice(VoiceClientHandler, nullptr);
    uint32_t config[8] = {};
    config[0] = 0x20;
    config[1] = 0x28;
    config[2] = 1;
    config[3] = 0;
    config[4] = 0xFFFFFFFE;
    IUnknown *session = onServer ? static_cast<IUnknown *>(m_server) : static_cast<IUnknown *>(m_client);
#ifndef HD2_DEDICATED   // no window in LS3DRF.dll
    m_voiceClient->Init(GetIGraph()->GetHWND(), false, true, session, 3, nullptr, config, 0);
#else
    m_voiceClient->Init(nullptr, false, true, session, 3, nullptr, config, 0);
#endif
}

void C_NetGame::StopVoiceServer()
{
    if (m_voiceServer) {
        delete m_voiceServer;
        m_voiceServer = nullptr;
    }
}

void C_NetGame::StopVoiceClient()
{
    if (m_voiceClient) {
        delete m_voiceClient;
        m_voiceClient = nullptr;
    }
}

void C_NetGame::MakeVoiceBuffer(int slot)
{
    S_NetPlayerState &player = m_players[static_cast<size_t>(slot)];
    m_voiceClient->m_client->Create3DSoundBuffer(static_cast<DWORD>(player.connection), nullptr, 0, 0, &player.voiceBuffer);
    if (auto *buffer = static_cast<IDirectSound3DBufferLite *>(player.voiceBuffer)) {
        buffer->SetMinDistance(5.0f, 0);
        buffer->SetMaxDistance(20.0f, 0);
    }
}

void C_NetGame::VisitPlayers()
{
    m_playersLock.Enter();
    for (size_t i = 0; i < m_players.size(); ++i) {
    }
    m_playersLock.Leave();
}

void C_NetGame::UpdateVoiceTeam()
{
    if (!Entry(m_serverEntry).voice && !(g_gameRole & GAME_ROLE_SERVER))
        return;
    C_Human *local = m_players[static_cast<size_t>(g_localPlayerSlot)].human;
    if (!local || !m_voiceClient)
        return;
    // The local player's team's players hear it (16 at most; Fix: the original wrote past its 16).
    DWORD targets[16];
    int count = 0;
    const int team = local->GetTeam(false);
    GetGame()->Print("Set Transmition team: %d ------------------\n", team);
    m_playersLock.Enter();
    for (const S_NetPlayerState &player : m_players) {
        if (player.connection == 0 || !player.human || player.human->GetTeam(false) != team || count >= 16)
            continue;
        targets[count++] = static_cast<DWORD>(player.connection);
        GetGame()->Print("%s\n", player.name);
    }
    m_playersLock.Leave();
    GetGame()->Print("------------------\n");
    m_voiceClient->m_client->SetTransmitTargets(count ? targets : nullptr, static_cast<DWORD>(count), 0);
}

// ---- the bandwidth ------------------------------------------------------------------------------------------------------

void C_NetGame::TakeBandwidth(int *figures)
{
    // The ping, DirectPlay's bytes sent and received since the last time, the transport socket's, the replication's.
    static int lastSent;                             // 0x0097c8a4
    static int lastReceived;                         // 0x0097c8a8
    int result[7] = {};
    DPN_CONNECTION_INFO info;
    info.size = sizeof info;
    if (!(g_gameRole & GAME_ROLE_SERVER)) {
        m_client->GetConnectionInfo(&info, 0);
        result[0] = m_serverLink ? static_cast<int>(m_serverLink->m_roundTrip) : 0;
        const int sent = static_cast<int>(info.bytesSentGuaranteed + info.bytesSentNonGuaranteed);
        const int received = static_cast<int>(info.bytesReceivedGuaranteed + info.bytesReceivedNonGuaranteed);
        result[1] = sent - lastSent;
        lastSent = sent;
        result[2] = received - lastReceived;
        lastReceived = received;
    } else {
        m_playersLock.Enter();
        m_transport->Lock();
        if (m_players.size() < 2) {
            result[0] = -1;
        } else {
            // (The last connected player's figures.)
            int sent = 0;
            int received = 0;
            for (size_t id = 1; id < m_players.size(); ++id) {
                if (m_players[id].connection == 0)
                    continue;
                C_NetConnection *link = m_transport->FindById(static_cast<int>(id));
                m_server->GetConnectionInfo(static_cast<DPNID>(m_players[id].connection), &info, 0);
                result[0] = link ? static_cast<int>(link->m_roundTrip) : static_cast<int>(info.roundTripLatency);
                sent = static_cast<int>(info.bytesSentGuaranteed + info.bytesSentNonGuaranteed);
                received = static_cast<int>(info.bytesReceivedGuaranteed + info.bytesReceivedNonGuaranteed);
            }
            result[1] = sent - lastSent;
            result[2] = received - lastReceived;
            lastReceived = received;
            lastSent = sent;
        }
        m_transport->Unlock();
        m_playersLock.Leave();
    }
    if (g_netReplication)
        g_netReplication->TakeByteCounts(&result[5], &result[6]);
    m_transport->m_socket.TakeCounters(&result[4], &result[3]);
    std::memcpy(figures, result, sizeof result);
}

// ---- a server's status in a message --------------------------------------------------------------------------------------

namespace {

int Signed16(uint32_t value)
{
    return static_cast<int16_t>(value);
}

}  // namespace

void S_StatusPlayer::Write(C_BitStream *stream)
{
    reinterpret_cast<S_NetStats *>(this)->Write(stream);
    stream->WriteBits(static_cast<uint32_t>(team), 32);
    stream->WriteString(name);
}

void S_StatusPlayer::Read(C_BitStream *stream)
{
    reinterpret_cast<S_NetStats *>(this)->Read(stream);
    team = static_cast<int>(stream->ReadBits(32));
    stream->ReadString(name, 0x32);
}

void S_ServerStatus::WriteRules(C_BitStream *stream)
{
    stream->WriteBits(static_cast<uint32_t>(maxClients), 8);
    stream->WriteBits(static_cast<uint32_t>(fragLimit), 16);
    stream->WriteBits(static_cast<uint32_t>(roundLimit), 16);
    stream->WriteBits(static_cast<uint32_t>(roundCount), 16);
    stream->WriteBits(static_cast<uint32_t>(preparationTime), 16);
    stream->WriteBits(static_cast<uint32_t>(respawnTime), 16);
    stream->WriteBits(static_cast<uint32_t>(allowRespawn), 1);
    stream->WriteBits(static_cast<uint32_t>(friendlyFire), 1);
    stream->WriteBits(static_cast<uint32_t>(autoTeamBalance), 1);
    stream->WriteBits(static_cast<uint32_t>(thirdPersonView), 1);
    stream->WriteBits(static_cast<uint32_t>(spawnProtectionTime), 1);
    stream->WriteBits(static_cast<uint32_t>(fallingDamage), 1);
    stream->WriteBits(static_cast<uint32_t>(allowVehicles), 1);
    stream->WriteBits(static_cast<uint32_t>(difficulty), 8);
    stream->WriteBits(static_cast<uint32_t>(lives), 16);
    stream->WriteBits(static_cast<uint32_t>(allowCrosshair), 1);
    stream->WriteBits(static_cast<uint32_t>(spawnOnStart), 1);
    stream->WriteBits(static_cast<uint32_t>(teamLives), 1);
    stream->WriteBits(static_cast<uint32_t>(inverseDamage), 16);
}

void S_ServerStatus::ReadRules(C_BitStream *stream)
{
    maxClients = static_cast<int>(stream->ReadBits(8));
    fragLimit = Signed16(stream->ReadBits(16));
    roundLimit = Signed16(stream->ReadBits(16));
    roundCount = Signed16(stream->ReadBits(16));
    preparationTime = Signed16(stream->ReadBits(16));
    respawnTime = Signed16(stream->ReadBits(16));
    allowRespawn = static_cast<int>(stream->ReadBits(1));
    friendlyFire = static_cast<int>(stream->ReadBits(1));
    autoTeamBalance = static_cast<int>(stream->ReadBits(1));
    thirdPersonView = static_cast<int>(stream->ReadBits(1));
    spawnProtectionTime = static_cast<int>(stream->ReadBits(1));
    fallingDamage = static_cast<int>(stream->ReadBits(1));
    allowVehicles = static_cast<int>(stream->ReadBits(1));
    difficulty = static_cast<int>(stream->ReadBits(8));
    lives = Signed16(stream->ReadBits(16));
    allowCrosshair = static_cast<int>(stream->ReadBits(1));
    spawnOnStart = static_cast<int>(stream->ReadBits(1));
    teamLives = static_cast<int>(stream->ReadBits(1));
    inverseDamage = Signed16(stream->ReadBits(16));
}

void S_ServerStatus::Write(C_BitStream *stream)
{
    stream->WriteBits(static_cast<uint32_t>(players.size()), 32);
    for (S_StatusPlayer &player : players)
        player.Write(stream);
    WriteRules(stream);
    stream->WriteBits(static_cast<uint32_t>(axisScore), 10);
    stream->WriteBits(static_cast<uint32_t>(alliedScore), 10);
}

void S_ServerStatus::Read(C_BitStream *stream)
{
    players.clear();
    const auto count = static_cast<int>(stream->ReadBits(32));
    for (int i = 0; i < count; ++i) {
        players.push_back(S_StatusPlayer());
        players.back().Read(stream);
    }
    ReadRules(stream);
    axisScore = static_cast<int>(stream->ReadBits(10));
    alliedScore = static_cast<int>(stream->ReadBits(10));
}
