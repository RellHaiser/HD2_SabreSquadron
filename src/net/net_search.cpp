// The network game's LAN search, its GameSpy reporting and the CD-key checks (part 10d-iii): GameSpy's browser
// finding servers for the main menu, the server's qr2 keys, the key socket's exchange (the server's hundred checks,
// the client's own). See net_game.h, net/gamespy.h.
#include <winsock2.h>

#include "net/net_game.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/controls.h"
#include "game/game.h"
#include "game/main_menu.h"
#include "game/mp_game_setup.h"
#include "game/pending.h"
#include "game/perf_timer.h"
#include "game/profile.h"
#include "net/gamespy.h"

char g_randomName[33];                               // 0x0097d048

namespace {

// The keys a browser asks each server for (12).
const uint8_t g_searchFields[12] = {QR2_HOSTNAME,   QR2_GAMEMODE, QR2_GAMETYPE,   QR2_MAPNAME,
                                     QR2_NUMPLAYERS, QR2_MAXPLAYERS, QR2_HOSTPORT,   QR2_ISDEDICATED,
                                     QR2_GAMEVER,    QR2_PASSWORD, QR2_VOICECHAT,  QR2_EXPANSION};

// The game's own keys registered with qr2 (the browser's and the reporting's).
void RegisterGameKeys()
{
    qr2_register_key(QR2_ISDEDICATED, "isdedicated");
    qr2_register_key(QR2_VOICECHAT, "voicechat");
    qr2_register_key(QR2_EXPANSION, "expansion");
}

// A key check's time-outs (ms).
constexpr int kKeyExpiry = 15000;
constexpr int kKeyResend = 200;

// The checksum the key packets carry: each byte (signed) as (b << 16 | b), summed.
int KeyChecksum(const char *data, int size)
{
    int sum = 0;
    for (int i = 0; i < size; ++i) {
        const int b = static_cast<signed char>(data[i]);
        sum += b << 16 | b;
    }
    return sum;
}

const char *PlayerName()
{
    return g_gameProfile.GetPlayerProfile()->GetProfileName().c_str();
}

}  // namespace

// ---- the LAN search ----------------------------------------------------------------------------------------------------

void OnBrowserCallback(S_ServerBrowser *, int reason, S_SbServer *server, void *)
{
    if (reason == 1)
        AddBrowserServer(server);
}

void AddBrowserServer(S_SbServer *server)
{
    auto *entry = new S_ServerEntry;
    entry->Reset();
    *reinterpret_cast<uint32_t *>(entry->name) = 0;
    // (Fix: the names copied within their fields; the original's strcpy took any length a server sent.)
    std::strncpy(entry->name, SBServerGetStringValue(server, "hostname", "(NO HOST)"), sizeof entry->name - 1);
    entry->name[sizeof entry->name - 1] = 0;
    std::strncpy(entry->map, SBServerGetStringValue(server, "mapname", "(NO MAP)"), sizeof entry->map - 1);
    entry->map[sizeof entry->map - 1] = 0;
    entry->players = static_cast<uint32_t>(SBServerGetIntValue(server, "numplayers", 0));
    entry->maxPlayers = static_cast<uint32_t>(SBServerGetIntValue(server, "maxplayers", 0));
    entry->port = SBServerGetIntValue(server, "hostport", 0);
    entry->ping = SBServerGetPing(server);
    entry->address = SBServerGetPublicInetAddress(server);
    char type[40];
    std::strncpy(type, SBServerGetStringValue(server, "gametype", "(NO TYPE)"), sizeof type - 1);
    type[sizeof type - 1] = 0;
    if (_stricmp(type, "occupation") == 0)
        entry->gameType = MP_GAME_OCCUPATION;
    else if (_stricmp(type, "objectives") == 0)
        entry->gameType = MP_GAME_OBJECTIVES;
    else if (_stricmp(type, "cooperative") == 0)
        entry->gameType = MP_GAME_COOPERATIVE;
    else
        entry->gameType = MP_GAME_DEATHMATCH;
    entry->unknown84 = -1;
    entry->password = SBServerGetIntValue(server, "password", 0) != 0;
    entry->dedicated = SBServerGetIntValue(server, "isdedicated", 0) != 0;
    entry->internet = true;
    entry->voice = SBServerGetIntValue(server, "voicechat", 0) != 0;
    entry->unknown94 = SBServerGetIntValue(server, "expansion", 1);
    std::sscanf(SBServerGetStringValue(server, "gamever", "1.1"), "%d.%d", &entry->versionMajor, &entry->versionMinor);
    C_MainMenu::OnServerFound(entry);
}

int C_NetGame::StartLanSearch()
{
    RegisterGameKeys();
    if (!m_serverBrowser)
        m_serverBrowser = ServerBrowserNew(kGameSpyName, kGameSpyName, kGameSpyKey, 1, 10, 1, OnBrowserCallback, nullptr);
    else
        ServerBrowserClear(m_serverBrowser);
    ServerBrowserUpdate(m_serverBrowser, 1, 1, g_searchFields, 12, nullptr);
    g_gameRole = 0x12;
    return 1;
}

void C_NetGame::PollSearch()
{
    if (m_serverBrowser)
        ServerBrowserThink(m_serverBrowser);
}

int C_NetGame::StartSearch()
{
    if (g_gameProfile.m_mpSettings->unknown174 == 1)
        StartLanSearch();
    else
        EnumHosts(nullptr, 0);
    return 0;
}

void C_NetGame::SearchServers(void(__fastcall *callback)(S_ServerEntry *server))
{
    Close(true);
    m_searchCallback = callback;
    g_netGame.StartSearch();
}

// ---- the server's reporting ------------------------------------------------------------------------------------------------

void ReportServerKey(int key, S_Qr2Buffer *buffer, void *)
{
    if (!GetGame())
        return;
    const S_MultiplayerSettings &settings = *g_gameProfile.m_mpSettings;
    switch (key) {
    case QR2_HOSTNAME:
        qr2_buffer_add(buffer, settings.sessionName);
        return;
    case QR2_GAMEVER: {
        char version[100];
        std::sprintf(version, "%d.%d", g_versionMajor, g_versionMinor);
        qr2_buffer_add(buffer, version);
        return;
    }
    case QR2_HOSTPORT:
        qr2_buffer_add_int(buffer, settings.port);
        return;
    case QR2_MAPNAME:
        qr2_buffer_add(buffer, g_gameProfile.m_mpSettings->GetCurrentMap()->GetName());
        return;
    case QR2_GAMETYPE:
        if (settings.gameType == MP_GAME_OCCUPATION)
            qr2_buffer_add(buffer, "Occupation");
        else if (settings.gameType == MP_GAME_OBJECTIVES)
            qr2_buffer_add(buffer, "Objectives");
        else if (settings.gameType == MP_GAME_COOPERATIVE)
            qr2_buffer_add(buffer, "Cooperative");
        else
            qr2_buffer_add(buffer, "Deathmatch");
        return;
    case QR2_NUMPLAYERS:
        qr2_buffer_add_int(buffer, g_netGame.CountPlayers());
        return;
    case QR2_MAXPLAYERS:
        qr2_buffer_add_int(buffer, settings.maxClients);
        return;
    case QR2_GAMEMODE:
        qr2_buffer_add_int(buffer, 0);
        return;
    case QR2_PASSWORD:
        qr2_buffer_add_int(buffer, settings.password.Size() != 0);
        return;
    case QR2_ISDEDICATED:
        qr2_buffer_add_int(buffer, settings.dedicated);
        return;
    case QR2_VOICECHAT:
        qr2_buffer_add_int(buffer, settings.unknown178 != 0);
        return;
    case QR2_EXPANSION:
        qr2_buffer_add_int(buffer, 2);
        return;
    default:
        qr2_buffer_add(buffer, "");
        return;
    }
}

void ReportPlayerKey(int key, int index, S_Qr2Buffer *buffer, void *)
{
    // The index-th named player of the 32 slots. (Fix: not past the table; the original read 32 whatever its size.)
    const S_NetPlayerState *players = g_netGame.LockPlayers();
    int named = 0;
    for (int slot = 0; slot < 32 && slot < g_netGame.GetPlayerCount(); ++slot) {
        const S_NetPlayerState &player = players[slot];
        if (!player.name[0])
            continue;
        if (named != index) {
            ++named;
            continue;
        }
        switch (key) {
        case QR2_PLAYER:
            qr2_buffer_add(buffer, player.name);
            break;
        case QR2_SCORE:
            qr2_buffer_add_int(buffer, player.stats[NET_STAT_SCORE]);
            break;
        case QR2_PING:
            qr2_buffer_add_int(buffer, player.stats[NET_STAT_PING]);
            break;
        case QR2_TEAM:
            qr2_buffer_add_int(buffer, player.team);
            break;
        case QR2_DEATHS:
            qr2_buffer_add_int(buffer, player.stats[NET_STAT_DEATHS]);
            break;
        default:
            qr2_buffer_add(buffer, "");
            break;
        }
        g_netGame.UnlockPlayers();
        return;
    }
    qr2_buffer_add(buffer, "");
    g_netGame.UnlockPlayers();
}

void ReportTeamKey(int key, int index, S_Qr2Buffer *buffer, void *)
{
    if (key == QR2_TEAM_NAME)
        qr2_buffer_add(buffer, index != 0 ? "allied" : "axis");
    else if (key == QR2_TEAM_SCORE)
        qr2_buffer_add_int(buffer, static_cast<int>(g_netGame.GetTeamScore(index)));
}

void ReportKeyList(int keyType, S_Qr2KeyBuffer *keys, void *)
{
    if (keyType == QR2_KEY_SERVER) {
        for (int key : {QR2_HOSTNAME, QR2_GAMEVER, QR2_HOSTPORT, QR2_MAPNAME, QR2_NUMPLAYERS, QR2_MAXPLAYERS,
                        QR2_GAMEMODE, QR2_GAMETYPE, QR2_ISDEDICATED, QR2_PASSWORD, QR2_VOICECHAT, QR2_EXPANSION})
            qr2_keybuffer_add(keys, key);
    } else if (keyType == QR2_KEY_PLAYER) {
        for (int key : {QR2_PLAYER, QR2_SCORE, QR2_DEATHS, QR2_PING, QR2_TEAM})
            qr2_keybuffer_add(keys, key);
    } else if (keyType == QR2_KEY_TEAM) {
        qr2_keybuffer_add(keys, QR2_TEAM_NAME);
        qr2_keybuffer_add(keys, QR2_TEAM_SCORE);
    }
}

int ReportCount(int keyType, void *)
{
    if (keyType == QR2_KEY_PLAYER)
        return g_netGame.CountPlayers();
    if (keyType == QR2_KEY_TEAM)
        return (g_gameProfile.m_mpSettings->gameType != MP_GAME_DEATHMATCH) + 1;
    return 0;
}

void ReportError(int, char *, void *) {}

void C_NetGame::StartReporting()
{
    RegisterGameKeys();
    qr2_init(nullptr, nullptr, g_gameProfile.m_mpSettings->port + 3, kGameSpyName, kGameSpyKey, 1, 0, ReportServerKey,
             ReportPlayerKey, ReportTeamKey, ReportKeyList, ReportCount, ReportError, nullptr);
    gcd_init(nullptr, kGameSpyGameId);
    OpenKeyServer();
    m_reporting = true;
}

void C_NetGame::AnswerQueries()
{
    if (!m_reporting)
        return;
    qr2_think(nullptr);
    m_playersLock.Enter();
    gcd_think();
    m_playersLock.Leave();
    ProcessKeyQueries();
}

// ---- the CD keys' socket ------------------------------------------------------------------------------------------------

void C_NetGame::OpenKeyServer()
{
    for (S_KeyQuery &query : m_keyQueries)
        query.state = KEY_FREE;
    WSADATA wsa;
    WSAStartup(0x202, &wsa);
    m_keySocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = htons(static_cast<u_short>(g_gameProfile.m_mpSettings->port + 2));
    bind(m_keySocket, reinterpret_cast<sockaddr *>(&local), sizeof local);
}

bool C_NetGame::ReceiveKeyPacket(char *data, int *size, uint32_t *address, uint32_t *port)
{
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(m_keySocket, &readable);
    fd_set writable, errors;
    FD_ZERO(&writable);
    FD_ZERO(&errors);
    timeval wait = {0, 0};
    // (A failed select still reads.)
    if (select(static_cast<int>(m_keySocket) + 1, &readable, &writable, &errors, &wait) >= 0 &&
        !FD_ISSET(m_keySocket, &readable))
        return false;
    sockaddr_in from = {};
    int fromSize = sizeof from;
    const int got = recvfrom(m_keySocket, data, *size, 0, reinterpret_cast<sockaddr *>(&from), &fromSize);
    if (got == 0 || got == -1)
        return false;
    const int length = got - 4;
    const int sum = KeyChecksum(data, length);
    for (int i = 0; i < length; ++i)
        data[i] = static_cast<char>(data[i] ^ 0xA0);
    int sent;
    std::memcpy(&sent, data + length, sizeof sent);
    if (sent != sum)
        return false;
    *address = from.sin_addr.s_addr;
    *port = ntohs(from.sin_port);
    *size = length;
    return true;
}

void C_NetGame::SendKeyPacket(const char *data, int size, uint32_t address, uint16_t port)
{
    char packet[512];
    std::memcpy(packet, data, static_cast<size_t>(size));
    for (int i = 0; i < size; ++i)
        packet[i] = static_cast<char>(packet[i] ^ 0xA0);
    const int sum = KeyChecksum(packet, size);
    std::memcpy(packet + size, &sum, sizeof sum);
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = address;
    sendto(m_keySocket, packet, size + 4, 0, reinterpret_cast<sockaddr *>(&to), sizeof to);
}

const char *RandomName(int length)
{
    if (length > 32)
        length = 32;
    g_randomName[length] = 0;
    for (; length != 0; --length)
        g_randomName[length - 1] = static_cast<char>(std::rand() % 26 + 'a');
    return g_randomName;
}

void OnKeyChecked(int, int localId, int authenticated, char *message, void *)
{
    S_KeyQuery &query = g_netGame.m_keyQueries[localId];
    if (authenticated)
        query.state = KEY_GOOD;
    else if (message[0] == 'C')
        query.state = KEY_IN_USE;
    else
        query.state = KEY_BAD;
}

int C_NetGame::FindFreeNameNumber(const char *name, int number)
{
    char candidate[1000];
    if (number < 2)
        std::strcpy(candidate, name);
    else
        std::sprintf(candidate, "%s%i", name, number);
    for (size_t i = m_players.size(); i != 0; --i) {
        if (_stricmp(candidate, m_players[i - 1].name) == 0)
            return FindFreeNameNumber(name, number + 1);
    }
    return number;
}

void C_NetGame::ProcessKeyQueries()
{
    const int now = static_cast<int>(GetPerfTimeMs());
    for (int i = 0; i < 100; ++i) {
        S_KeyQuery &query = m_keyQueries[i];
        if (query.expiry < now && query.state != KEY_GOOD && query.state != KEY_KEPT) {
            query.state = KEY_FREE;
            query.name[0] = 0;
            continue;
        }
        if (query.expiry + kKeyExpiry < now && query.state == KEY_GOOD) {
            // A good key: GameSpy told it is gone 15 s after.
            m_playersLock.Enter();
            gcd_disconnect_user(kGameSpyGameId, i);
            m_playersLock.Leave();
            query.state = KEY_FREE;
            query.name[0] = 0;
            continue;
        }
        if (now <= query.lastSend + kKeyResend)
            continue;
        query.lastSend = now;
        char packet[256];
        switch (query.state) {
        case KEY_CHALLENGED: {
            const int length = std::sprintf(packet, "c:%s", query.challenge);
            SendKeyPacket(packet, length + 1, query.address, static_cast<uint16_t>(query.port));
            break;
        }
        case KEY_GOOD:
        case KEY_BAD:
        case KEY_IN_USE:
            packet[0] = query.state == KEY_GOOD ? 'm' : query.state == KEY_BAD ? 'e' : 'u';
            SendKeyPacket(packet, 1, query.address, static_cast<uint16_t>(query.port));
            break;
        default:
            break;
        }
    }
    char packet[256];
    int size = sizeof packet;
    uint32_t address;
    uint32_t port;
    if (!ReceiveKeyPacket(packet, &size, &address, &port))
        return;
    char name[0x34] = {};
    // A player's name made unique as the join will (the players' table); (Fix: within the name's room).
    auto takeName = [&](const char *from) {
        std::strncpy(name, from, sizeof name - 1);
        name[sizeof name - 1] = 0;
        LockPlayers();
        const int number = FindFreeNameNumber(name, 1);
        UnlockPlayers();
        if (number > 1) {
            const C_str suffix = C_fstr("%i", number);
            std::strncat(name, suffix.c_str(), sizeof name - 1 - std::strlen(name));
        }
    };
    if (packet[0] == 'c') {
        // Asked: an existing check refreshed; else a new one challenged.
        takeName(packet + 2);
        for (S_KeyQuery &query : m_keyQueries) {
            if (std::strcmp(query.name, name) == 0 && query.address == address &&
                static_cast<uint32_t>(query.port) == port) {
                query.expiry = static_cast<int>(GetPerfTimeMs()) + kKeyExpiry;
                query.lastSend = static_cast<int>(GetPerfTimeMs());
                return;
            }
        }
        for (S_KeyQuery &query : m_keyQueries) {
            if (query.state != KEY_FREE)
                continue;
            query.state = KEY_CHALLENGED;
            std::strcpy(query.name, name);
            query.port = static_cast<int>(port);
            query.address = address;
            query.expiry = static_cast<int>(GetPerfTimeMs()) + kKeyExpiry;
            query.lastSend = static_cast<int>(GetPerfTimeMs());
            std::strcpy(query.challenge, RandomName(8));
            query.answered = 0;
            break;
        }
    }
    if (packet[0] == 'r') {
        // The response ("r:<response>:<name>"): handed to GameSpy for the challenged check of that name.
        const char *separator = std::strstr(packet + 3, ":");
        if (separator)
            takeName(separator + 1);
        for (int i = 0; i < 100; ++i) {
            S_KeyQuery &query = m_keyQueries[i];
            if (std::strcmp(query.name, name) == 0 && query.address == address && query.state == KEY_CHALLENGED) {
                query.answered = 1;
                query.state = KEY_CHECKING;
                m_playersLock.Enter();
                gcd_authenticate_user(kGameSpyGameId, i, address, query.challenge, packet + 2, OnKeyChecked,
                                      m_keyQueries);
                m_playersLock.Leave();
            }
        }
    }
}

void C_NetGame::StartKeyQuery(const S_ServerEntry *server)
{
    WSADATA wsa;
    WSAStartup(0x202, &wsa);
    m_keySocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    bind(m_keySocket, reinterpret_cast<sockaddr *>(&local), sizeof local);
    m_keyClient.state = KEY_FREE;
    m_keyClient.address = server->address;
    m_keyClient.state = KEY_ASKING;
    m_keyClient.port = server->port + 2;
    m_keyClient.expiry = static_cast<int>(GetPerfTimeMs()) + kKeyExpiry;
    m_keyClient.lastSend = static_cast<int>(GetPerfTimeMs());
}

bool C_NetGame::RunKeyQuery()
{
    for (;;) {
        const int now = static_cast<int>(GetPerfTimeMs());
        if (m_keyClient.expiry < now) {
            m_joinStatus = NET_JOIN_TIMED_OUT;
            return false;
        }
        if (m_keyClient.lastSend + kKeyResend < now) {
            m_keyClient.lastSend = now;
            char packet[256];
            int size = -1;
            if (m_keyClient.state == KEY_CHALLENGED) {
                const int length = std::sprintf(packet, "r:%s:", m_keyClient.response);
                std::strcat(packet, PlayerName());
                size = length + static_cast<int>(std::strlen(PlayerName())) + 1;
            } else if (m_keyClient.state == KEY_ASKING) {
                size = std::sprintf(packet, "c:%s", PlayerName()) + 1;
            }
            if (size > 0)
                SendKeyPacket(packet, size, m_keyClient.address, static_cast<uint16_t>(m_keyClient.port));
        }
        char packet[256];
        int size = sizeof packet;
        uint32_t address;
        uint32_t port;
        if (!ReceiveKeyPacket(packet, &size, &address, &port)) {
            Sleep(50);
            continue;
        }
        if (packet[0] == 'c') {
            gcd_compute_response(GetCDKey(), packet + 2, m_keyClient.response);
            m_keyClient.state = KEY_CHALLENGED;
        }
        if (packet[0] == 'm')
            return true;
        if (packet[0] == 'e') {
            m_joinStatus = NET_JOIN_BAD_CD_KEY;
            return false;
        }
        if (packet[0] == 'u') {
            m_joinStatus = NET_JOIN_KEY_IN_USE;
            return false;
        }
    }
}

void C_NetGame::DropKeyQuery(int index)
{
    if (index < 0 || index >= 100)
        return;
    m_keyQueries[index].state = KEY_FREE;
    m_playersLock.Enter();
    gcd_disconnect_user(kGameSpyGameId, index);
    m_playersLock.Leave();
}
