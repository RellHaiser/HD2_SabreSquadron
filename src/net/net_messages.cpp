// The network game's messages (part 10d-v): the server's DirectPlay messages (players joining and leaving, the
// hosts' queries, the connections asked for, data), the game messages C_GameApp queues and sends (SendGameMessage,
// PostCommand), a soldier's state in a message. See net_game.h, game/game_app.h.
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>

#include "net/dplay8.h"
#include "net/net_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <list>

#include "game/actors_module.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_texts.h"
#include "game/human.h"
#include "game/items.h"
#include "game/item_defs.h"
#include "game/module.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/soldier.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "net/net_channel.h"
#include "net/net_transport.h"

namespace {

// DirectPlay's server messages (their DPNMSG_* fields read).
constexpr DWORD DPN_MSGID_CREATE_PLAYER = 0xFFFF0007;
constexpr DWORD DPN_MSGID_DESTROY_PLAYER = 0xFFFF0009;
constexpr DWORD DPN_MSGID_ENUM_HOSTS_QUERY = 0xFFFF000A;
constexpr DWORD DPN_MSGID_INDICATE_CONNECT = 0xFFFF000E;
constexpr DWORD DPN_MSGID_INDICATED_CONNECT_ABORTED = 0xFFFF000F;
constexpr DWORD DPN_MSGID_SERVER_INFO = 0xFFFF0015;
constexpr HRESULT DPNERR_BUFFERTOOSMALL_ = static_cast<HRESULT>(0x80158100);
constexpr HRESULT DPNERR_INVALIDPLAYER_ = static_cast<HRESULT>(0x80158420);

struct S_CreatePlayer {
    DWORD size;
    DPNID player;                                   // +0x04
    void *context;                                  // +0x08 the slot INDICATE_CONNECT gave
};
struct S_DestroyPlayer {
    DWORD size;
    DPNID player;                                   // +0x04
};
struct S_EnumQuery {
    DWORD size;
    IDirectPlay8Address *sender;
    IDirectPlay8Address *device;
    const int *request;                             // +0x0C the 4 bytes EnumHosts sent
    DWORD requestSize;
    DWORD maxResponse;
    void *response;                                 // +0x18
    DWORD responseSize;                             // +0x1C
    void *responseContext;
};
struct S_IndicateConnect {
    DWORD size;
    const S_JoinRequest *request;                   // +0x04
    DWORD requestSize;                              // +0x08
    uint8_t *reply;                                 // +0x0C
    DWORD replySize;                                // +0x10
    void *replyContext;
    void *playerContext;                            // +0x18 the slot (CREATE_PLAYER's)
    IDirectPlay8Address *player;                    // +0x1C
};
struct S_ConnectAborted {
    DWORD size;
    void *playerContext;                            // +0x04
};
struct S_ServerReceive {
    DWORD size;
    DPNID sender;
    void *playerContext;                            // +0x08 the slot
    void *data;                                     // +0x0C
    DWORD dataSize;                                 // +0x10
};
struct S_ServerInfo {
    DWORD size;
    DPNID server;                                   // +0x04
};

// `name` with the lowest free number (2 and up) appended (the players locked by the caller or not, as the callers).
void AppendFreeNumber(C_NetGame &net, char *name, int number)
{
    if (number > 1)
        std::strcat(name, C_fstr("%i", number).c_str());
    (void)net;
}

// A player's slot freed (no connection, no name).
void ClearSlot(S_NetPlayerState &player)
{
    player.connection = 0;
    player.name[0] = 0;
}

// The direction a frame's z axis gives, normalised as the game's inline copies (the sum's order given).
S_vector Unit(float x, float y, float z, bool zFirst)
{
    const float sq = zFirst ? (z * z + y * y) + x * x : (x * x + y * y) + z * z;
    if (std::fabs(sq - 1.0f) < 1e-8f)
        return S_vector(x, y, z);
    if (sq >= 1e-8f) {
        const float k = 1.0f / std::sqrt(sq);
        return S_vector(x * k, y * k, k * z);
    }
    if (x != 0.0f)
        return S_vector(x >= 0.0f ? 1.0f : -1.0f, y, z);
    if (z != 0.0f)
        return S_vector(x, y, z >= 0.0f ? 1.0f : -1.0f);
    return S_vector(x, y >= 0.0f ? 1.0f : -1.0f, z);
}

void WriteFloat(C_BitStream *stream, float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    stream->WriteBits(bits, 32);
}

// A human's place and facing (its frame's world position, its z axis).
void WritePlace(C_BitStream *stream, const C_Human *human, bool zFirst)
{
    const S_matrix &m = human->GetFrame()->GetMatrix();
    WriteFloat(stream, m(3).x);
    WriteFloat(stream, m(3).y);
    WriteFloat(stream, m(3).z);
    const S_vector dir = Unit(m(2).x, m(2).y, m(2).z, zFirst);
    WriteFloat(stream, dir.x);
    WriteFloat(stream, dir.y);
    WriteFloat(stream, dir.z);
}

// The messages SendGameMessage / PostCommand queue (the original's lists at 0x0097ca00 / 0x0097c9f0).
struct S_QueuedGameMessage {
    int type;
    intptr_t a;
    int b;
    int to;
};
struct S_QueuedCommand {
    intptr_t a;
    intptr_t b;
    int command;
};
std::list<S_QueuedGameMessage> g_gameMessages;
std::list<S_QueuedCommand> g_commands;

}  // namespace

// ---- a soldier's state ----------------------------------------------------------------------------------------------

namespace {

void WriteItem(C_BitStream *stream, const S_SoldierItem &item)       // 0x00727820
{
    stream->WriteBits(static_cast<uint32_t>(item.uid), 16);
    stream->WriteBits(static_cast<uint32_t>(item.typeId), 16);
    stream->WriteBits(static_cast<uint32_t>(item.present), 8);
}

void ReadItem(C_BitStream *stream, S_SoldierItem &item)               // 0x007277f0
{
    item.uid = static_cast<int16_t>(stream->ReadBits(16));
    item.typeId = static_cast<int16_t>(stream->ReadBits(16));
    item.present = static_cast<int16_t>(stream->ReadBits(8));
}

}  // namespace

void NetWriteSoldierState(C_NetMessage *msg, S_SoldierState &state, bool itemsOnly)
{
    if (!itemsOnly) {
        msg->WriteString(state.name.c_str());
        msg->WriteBits(static_cast<uint32_t>(state.team), 1);
        // (-1 goes as all bits set: stored so in the state.)
        if (state.profileIndex == -1)
            state.profileIndex = 0xFF;
        msg->WriteBits(static_cast<uint32_t>(state.profileIndex), 8);
        if (state.spawnZone == -1)
            state.spawnZone = 0x3FF;
        msg->WriteBits(static_cast<uint32_t>(state.spawnZone), 10);
        msg->WriteBits(state.spectating, 1);
        msg->WriteBits(state.vip, 1);
        msg->WriteBits(static_cast<uint32_t>(state.health), 10);
    }
    WriteItem(msg, state.head);
    WriteItem(msg, state.uniform);
    WriteItem(msg, state.backpack);
    WriteItem(msg, state.longGun);
    WriteItem(msg, state.hands);
    msg->WriteBits(static_cast<uint32_t>(state.belt.size()), 8);
    for (const S_SoldierItem &item : state.belt)
        WriteItem(msg, item);
    msg->WriteBits(static_cast<uint32_t>(state.backpackContents.size()), 8);
    for (const S_SoldierItem &item : state.backpackContents)
        WriteItem(msg, item);
    if (!itemsOnly) {
        // The load: 256 bits.
        uint32_t words[8];
        std::memcpy(words, &state.load, sizeof words);
        for (uint32_t word : words)
            msg->WriteBits(word, 32);
    }
}

void NetReadSoldierState(C_NetMessage *msg, S_SoldierState &state, bool itemsOnly)
{
    if (!itemsOnly) {
        char name[256];
        msg->ReadString(name, 0xFF);
        state.name = name;
        state.team = static_cast<int>(msg->ReadBits(1));
        state.profileIndex = static_cast<int>(msg->ReadBits(8));
        state.spawnZone = static_cast<int>(msg->ReadBits(10));
        state.spectating = msg->ReadBits(1) != 0;
        state.vip = msg->ReadBits(1) != 0;
        state.health = static_cast<int>(msg->ReadBits(10));
    }
    ReadItem(msg, state.head);
    ReadItem(msg, state.uniform);
    ReadItem(msg, state.backpack);
    ReadItem(msg, state.longGun);
    ReadItem(msg, state.hands);
    for (std::vector<S_SoldierItem> *list : {&state.belt, &state.backpackContents}) {
        const auto count = static_cast<int>(msg->ReadBits(8));
        for (int i = 0; i < count; ++i) {
            S_SoldierItem item;
            ReadItem(msg, item);
            list->push_back(item);
        }
    }
    if (!itemsOnly) {
        uint32_t words[8];
        for (uint32_t &word : words)
            word = msg->ReadBits(32);
        std::memcpy(&state.load, words, sizeof words);
    }
}

// ---- the game messages' streams -------------------------------------------------------------------------------------

C_BitStream *C_NetGame::BeginGameMessage(int to, int id, bool reliable)
{
    C_NetMessage *msg;
    if (!reliable && (g_gameRole & GAME_ROLE_SERVER))
        msg = &m_unreliable;
    else
        msg = to ? &m_direct : &m_messages[0];
    msg->m_channel = 0;
    msg->m_pending = 1;
    msg->m_target = to;
    msg->m_unreliable = !reliable;
    msg->WriteBits(NET_MESSAGE_GAME, 5);
    msg->m_type = NET_MESSAGE_GAME;
    msg->WriteBits(static_cast<uint32_t>(id), 32);
    return msg;
}

C_BitStream *C_NetGame::BeginCommand(int id)
{
    return BeginGameMessage(0, id, false);
}

C_BitStream *C_NetGame::BeginReply(int to, int id)
{
    return BeginGameMessage(to, id, true);
}

// ---- the server's DirectPlay messages -------------------------------------------------------------------------------

long __stdcall C_NetGame::HandleServerMessage(C_NetGame *net, void *, uint32_t type, void *message)
{
    switch (type) {
    case DPN_MSGID_CREATE_PLAYER: {
        // A player connected: its slot (INDICATE_CONNECT's) given its DirectPlay id and name (made unique), its link.
        const auto *create = static_cast<const S_CreatePlayer *>(message);
        DWORD size = 0;
        long hr = net->m_server->GetClientInfo(create->player, nullptr, &size, 0);
        if (hr >= 0 || hr == DPNERR_BUFFERTOOSMALL_) {
            auto *info = static_cast<DPN_PLAYER_INFO *>(operator new(size));
            std::memset(info, 0, size);
            info->size = sizeof(DPN_PLAYER_INFO);
            hr = net->m_server->GetClientInfo(create->player, info, &size, 0);
            if (hr < 0) {
                net->m_lastError = hr;
                net->m_lastErrorText = "DPserver->GetClientInfo failed (2)";
                return hr;
            }
            const auto slot = static_cast<size_t>(reinterpret_cast<intptr_t>(create->context));
            net->m_playersLock.Enter();
            S_NetPlayerState &player = net->m_players[slot];
            player.connection = static_cast<int>(create->player);
            char name[0x34];
            if (g_gameProfile.UsesUnicodeFont() || HasCjkCharacters(reinterpret_cast<const uint16_t *>(info->name)))
                NetWideToUtf8(name, info->name, 0x32);
            else
                NetWideToAnsi(name, info->name, 0x32);
            const int number = net->FindFreeNameNumber(name, 1);
            std::strcpy(player.name, name);
            AppendFreeNumber(*net, player.name, number);
            operator delete(info);
            player.activityTime = timeGetTime();
            player.admin = false;
            player.link = new C_NetConnection(static_cast<int>(slot), player.address, &net->m_transport->m_socket);
            player.stats = S_NetStats();
            net->m_playersLock.Leave();
            net->m_transport->Add(player.link);
            return 0;
        }
        if (hr != DPNERR_INVALIDPLAYER_) {
            net->m_lastError = hr;
            net->m_lastErrorText = "DPserver->GetClientInfo failed (1)";
            return hr;
        }
        // The server's own player (a playing server): a slot of its own at the end.
        if (g_gameRole & GAME_ROLE_CLIENT) {
            S_NetPlayerState own;
            own.connection = static_cast<int>(create->player);
            net->m_playersLock.Enter();
            net->m_players.push_back(own);
            net->m_playersLock.Leave();
            return 0;
        }
        break;
    }
    case DPN_MSGID_DESTROY_PLAYER: {
        // A player gone: its link, its key check; told to the others.
        const DPNID id = static_cast<const S_DestroyPlayer *>(message)->player;
        net->m_playersLock.Enter();
        size_t slot = 0;
        for (; slot < net->m_players.size(); ++slot) {
            if (static_cast<DPNID>(net->m_players[slot].connection) == id)
                break;
        }
        if (slot < net->m_players.size()) {
            S_NetPlayerState &player = net->m_players[slot];
            if (player.link) {
                // (g_netGame's transport, as the original's absolute address.)
                g_netGame.m_transport->Remove(player.link);
                delete player.link;
                player.link = nullptr;
            }
            g_netGame.DropKeyQuery(player.keyQuery);
            player.keyQuery = -1;
        }
        net->m_playersLock.Leave();
        if (GetGame() && slot != net->m_players.size()) {
            GetGameApp()->SendGameMessage(2, 0, static_cast<intptr_t>(id), 0, false);
            return 0;
        }
        break;
    }
    case DPN_MSGID_ENUM_HOSTS_QUERY: {
        // A search's, ping's or status query's answer (a mission running): the request, the map, the version, the
        // rules; a status query's: the status.
        auto *query = static_cast<S_EnumQuery *>(message);
        if (!GetGame() || GetGame()->GetMissionTime() <= 99)
            return -1;
        void *buffer = operator new(1000);
        C_GrowingBitStream stream;
        stream.Attach(buffer, 0x1F40);
        const int request = *query->request;
        stream.WriteBits(static_cast<uint32_t>(request), 2);
        stream.WriteString(g_gameProfile.m_mpSettings->GetCurrentMap()->GetName());
        stream.WriteBits(static_cast<uint32_t>(g_versionMinor), 8);
        stream.WriteBits(static_cast<uint32_t>(g_versionMajor), 8);
        const S_MultiplayerSettings &settings = *g_gameProfile.m_mpSettings;
        stream.WriteBits(static_cast<uint32_t>(settings.gameType), 3);
        stream.WriteBits(settings.dedicated, 1);
        stream.WriteBits(static_cast<uint32_t>(settings.maxClients), 6);
        stream.WriteBits(static_cast<uint32_t>(net->CountPlayers()), 6);
        stream.WriteBits(settings.password.Size() != 0, 1);
        stream.WriteBits(static_cast<uint32_t>(settings.unknown174 == 1 ? settings.port : 0x7B4F), 16);
        stream.WriteBits(settings.unknown178 != 0, 1);
        stream.WriteBits(2, 2);
        if (request == 1) {
            S_ServerStatus status;
            net->FillServerStatus(&status);
            status.Write(&stream);
        }
        query->response = buffer;
        query->responseSize = static_cast<DWORD>(stream.ByteSize() + 1);
        return 0;
    }
    case DPN_MSGID_INDICATE_CONNECT: {
        // A connection asked for: the version, the room, the password; a slot (a free one reused, else one more);
        // GameSpy's listing: the CD key checked. The reply's byte: the refusal, or the slot (the transport's session
        // bit at 0x80).
        auto *connect = static_cast<S_IndicateConnect *>(message);
        const S_JoinRequest *request = connect->request;
        auto *reply = static_cast<uint8_t *>(operator new(1));
        connect->reply = reply;
        connect->replySize = 1;
        const S_MultiplayerSettings &settings = *g_gameProfile.m_mpSettings;
        if (!request || connect->requestSize != sizeof(S_JoinRequest) || request->versionMajor != g_versionMajor ||
            request->versionMinor != g_versionMinor || request->unknown1C == 1) {
            *reply = NET_JOIN_BAD_VERSION;
            return -1;
        }
        if (settings.maxClients <= net->CountPlayers()) {
            *reply = NET_JOIN_FULL;
            return -1;
        }
        net->m_playersLock.Enter();
        if (settings.password.Size() != 0 && std::strcmp(settings.password.c_str(), request->password) != 0) {
            net->m_playersLock.Leave();
            *reply = NET_JOIN_BAD_PASSWORD;
            return -1;
        }
        S_NetPlayerState fresh;
        fresh.connection = -1;
        size_t slot = 0;
        for (; slot < net->m_players.size(); ++slot) {
            if (net->m_players[slot].connection == 0 && !net->m_players[slot].left)
                break;
        }
        if (slot < net->m_players.size()) {
            net->m_players[slot] = fresh;
        } else {
            if (net->m_players.size() == 32) {
                net->m_playersLock.Leave();
                *reply = NET_JOIN_FULL;
                return -1;
            }
            slot = net->m_players.size();
            net->m_players.push_back(fresh);
        }
        S_NetPlayerState &player = net->m_players[slot];
        player.address = GetHostAddress(connect->player);
        if (settings.unknown174 != 1) {
            player.keyQuery = -1;
            net->m_playersLock.Leave();
            connect->playerContext = reinterpret_cast<void *>(static_cast<intptr_t>(slot));
            *reply = static_cast<uint8_t>((net->m_transport->m_session & 0xFF) << 7 | slot);
            return 0;
        }
        // The CD key: a good check of that name from that address (taken: 7, the slot's), in use: 6, none: 5.
        char name[0x34 + 16];
        std::strcpy(name, request->name);
        net->LockPlayers();
        const int number = net->FindFreeNameNumber(name, 1);
        net->UnlockPlayers();
        AppendFreeNumber(*net, name, number);
        for (int i = 0; i < 100; ++i) {
            S_KeyQuery &query = net->m_keyQueries[i];
            if (query.address != player.address || std::strcmp(query.name, name) != 0)
                continue;
            bool good = false;
            if (query.state == KEY_GOOD) {
                query.state = KEY_KEPT;
                player.keyQuery = i;
                good = true;
            }
            if (query.state == KEY_IN_USE) {
                ClearSlot(player);
                *reply = NET_JOIN_KEY_IN_USE;
                net->m_playersLock.Leave();
                return -1;
            }
            if (good) {
                net->m_playersLock.Leave();
                connect->playerContext = reinterpret_cast<void *>(static_cast<intptr_t>(slot));
                *reply = static_cast<uint8_t>((net->m_transport->m_session & 0xFF) << 7 | slot);
                return 0;
            }
            // (Not checked yet or refused: the next one looked at.)
        }
        ClearSlot(player);
        *reply = NET_JOIN_BAD_CD_KEY;
        net->m_playersLock.Leave();
        return -1;
    }
    case DPN_MSGID_INDICATED_CONNECT_ABORTED: {
        net->m_playersLock.Enter();
        const auto slot = static_cast<size_t>(
            reinterpret_cast<intptr_t>(static_cast<const S_ConnectAborted *>(message)->playerContext));
        S_NetPlayerState &player = net->m_players[slot];
        ClearSlot(player);
        if (player.link) {
            net->m_transport->Remove(player.link);
            delete player.link;
        }
        net->m_playersLock.Leave();
        return 0;
    }
    case DPN_MSGID_RECEIVE: {
        const auto *receive = static_cast<const S_ServerReceive *>(message);
        if (receive->dataSize != 0)
            net->QueueReceived(receive->data, static_cast<int>(receive->dataSize),
                               static_cast<uint32_t>(reinterpret_cast<uintptr_t>(receive->playerContext)) | 0x80);
        break;
    }
    case DPN_MSGID_RETURN_BUFFER:
        operator delete(static_cast<const DPNMSG_RETURN_BUFFER *>(message)->buffer);
        return 0;
    case DPN_MSGID_SERVER_INFO:
        if (g_gameRole & GAME_ROLE_SERVER) {
            net->m_players[static_cast<size_t>(g_localPlayerSlot)].connection =
                static_cast<int>(static_cast<const S_ServerInfo *>(message)->server);
            return 0;
        }
        break;
    default:
        break;
    }
    return 0;
}

// ---- the game messages ----------------------------------------------------------------------------------------------

void C_GameApp::SendGameMessage(int type, int to, intptr_t a, int b, bool reliable)
{
    if (!reliable) {
        // Queued (locked unless a local game) until a reliable call sends them.
        if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL)
            EnterCriticalSection(&m_commandLock);
        g_gameMessages.push_back({type, a, b, to});
        if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL)
            LeaveCriticalSection(&m_commandLock);
        return;
    }
    while (!g_gameMessages.empty()) {
        const S_QueuedGameMessage queued = g_gameMessages.front();
        g_netGame.Process(1000, false);
        auto *msg = static_cast<C_NetMessage *>(g_netGame.BeginReply(queued.to, queued.type));
        const intptr_t value = queued.a;
        switch (queued.type) {
        case 0: {
            // The join data: the session's ids, the rules, the map, the players (their names, teams, ranks).
            msg->WriteBits(GetGameApp()->m_randomId, 32);
            msg->WriteBits(static_cast<uint32_t>(g_netGame.m_unknown448), 32);
            g_gameProfile.m_mpSettings->WriteRules(msg);
            g_gameProfile.m_mpSettings->WriteCurrentMap(msg);
            const S_NetPlayerState *players = g_netGame.LockPlayers();
            const int count = g_netGame.GetPlayerCount();
            int named = 0;
            for (int i = 0; i < count; ++i)
                named += players[i].name[0] != 0;
            msg->WriteBits(static_cast<uint32_t>(named), 8);
            for (int i = 0; i < count; ++i) {
                const S_NetPlayerState &player = players[i];
                if (!player.name[0])
                    continue;
                msg->WriteString(player.name);
                int team = player.team;
                if (g_gameProfile.m_mpSettings->gameType == MP_GAME_DEATHMATCH && player.human) {
                    const C_Item *uniform = player.human->GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem();
                    team = (!uniform || uniform->GetDef()->GetTeam() != SIDE_ALLIES) ? 0 : 1;
                }
                msg->WriteBits(static_cast<uint32_t>(team), 2);
                msg->WriteBits(static_cast<uint32_t>(player.stats[NET_STAT_RANK]), 5);
            }
            g_netGame.UnlockPlayers();
            break;
        }
        case 2: {
            // A player left (told locally too).
            const int slot = g_netGame.FindPlayer(static_cast<int>(value));
            if (g_netGame.GetPlayerName(slot)) {
                const C_str text = g_texts.Get(0x22CB);
                const C_fstr line("%s %s", g_netGame.GetPlayerName(g_netGame.FindPlayer(static_cast<int>(value))),
                                  text.c_str());
                GetGameApp()->PrintMessage(line.c_str());
            }
            GetGame()->OnPlayerLeft(static_cast<int>(value));
            msg->WriteBits(static_cast<uint32_t>(value), 32);
            break;
        }
        case 0x13:
            msg->WriteBits(static_cast<uint32_t>(value), 32);
            break;
        case 3: {
            // A player spawned: its slot, place, facing, soldier state, stats (the block freed).
            auto *spawn = reinterpret_cast<float *>(value);
            auto *state = reinterpret_cast<S_SoldierState *>(reinterpret_cast<intptr_t *>(spawn)[8]);
            auto *stats = reinterpret_cast<int *>(reinterpret_cast<intptr_t *>(spawn)[9]);
            const int slot = reinterpret_cast<int *>(spawn)[10];
            g_netGame.SetPlayerTeam(slot, state->team, false);
            g_netGame.SetPlayerTeam(slot, state->team, true);
            msg->WriteBits(static_cast<uint32_t>(slot), 32);
            msg->WriteQuantized(spawn, msg->GetSender(-1), 7);
            msg->WriteDirection3D(*reinterpret_cast<const S_vector *>(spawn + 4), -1);
            NetWriteSoldierState(msg, *state, false);
            msg->WriteBits(static_cast<uint32_t>(stats[0]), 10);
            for (int index : {2, 1, 3, 4, 5, 6, 7, 8, 9, 11, 10})
                msg->WriteBits(static_cast<uint32_t>(stats[index]), 7);
            delete state;                           // (Fix: destroyed; the original only frees it)
            operator delete(stats);
            _aligned_free(spawn);
            break;
        }
        case 4: {
            // A chat line.
            const auto *line = reinterpret_cast<const char *>(value);
            msg->WriteBits(*reinterpret_cast<const uint32_t *>(line + 0x104), 32);
            msg->WriteBits(*reinterpret_cast<const uint32_t *>(line + 0x108), 32);
            msg->WriteBits(static_cast<uint8_t>(line[0x10C]), 1);
            msg->WriteString(line);
            operator delete(const_cast<char *>(line));
            break;
        }
        case 5: {
            // The players' places (the last first).
            const S_NetPlayerState *players = g_netGame.LockPlayers();
            const int count = g_netGame.GetPlayerCount();
            if (count != 0) {
                msg->WriteBits(static_cast<uint32_t>(count), 32);
                for (int i = count - 1; i >= 0; --i)
                    WritePlace(msg, players[i].human, true);
            }
            g_netGame.UnlockPlayers();
            break;
        }
        case 6:
            g_gameProfile.m_mpSettings->WriteCurrentMap(msg);
            break;
        case 7:
        case 0x14:
            g_temporaryActors.WriteState(msg);
            break;
        case 8:
            g_netGame.WriteScores(msg);
            break;
        case 9: {
            // A player joined (told locally too).
            const auto *name = reinterpret_cast<const char *>(value);
            msg->WriteString(name);
            const C_str text = g_texts.Get(0x22CA);
            const C_fstr line("%s %s", name, text.c_str());
            GetGameApp()->PrintMessage(line.c_str());
            break;
        }
        case 10:
            msg->WriteBits(static_cast<uint32_t>(value), 5);
            msg->WriteBits(static_cast<uint32_t>(queued.b), 4);
            break;
        case 0xC: {
            // The teams: each named slot (6 bits) and its team (2 bits); 0x21 ends.
            const S_NetPlayerState *players = g_netGame.LockPlayers();
            for (int i = 0; i < g_netGame.GetPlayerCount(); ++i) {
                if (!players[i].name[0])
                    continue;
                msg->WriteBits(static_cast<uint32_t>(i), 6);
                msg->WriteBits(static_cast<uint32_t>(players[i].team), 2);
            }
            msg->WriteBits(0x21, 6);
            g_netGame.UnlockPlayers();
            break;
        }
        case 0xD:
            msg->WriteBits(static_cast<uint32_t>(value), 2);
            break;
        case 0xE:
            msg->WriteBits(static_cast<uint32_t>(value), 5);
            msg->WriteBits(static_cast<uint32_t>(queued.b), 2);
            break;
        case 0xF:
            msg->WriteBits(static_cast<uint32_t>(value), 5);
            msg->WriteBits(static_cast<uint32_t>(queued.b), 1);
            break;
        case 0x11:
            msg->WriteBits(static_cast<uint32_t>(value), 32);
            msg->WriteBits(static_cast<uint32_t>(queued.b), 32);
            break;
        case 0x12:
            g_gameProfile.m_mpSettings->WriteRules(msg);
            break;
        case 0x15:
            // The whole game for a joining player: every player (its id, name, human, stats, place, soldier),
            // the modules' states, the objectives; the player's join done.
            if (!value) {
                msg->WriteBits(0, 1);
            } else {
                msg->WriteBits(1, 1);
                const S_NetPlayerState *players = g_netGame.LockPlayers();
                const int count = g_netGame.GetPlayerCount();
                msg->WriteBits(static_cast<uint32_t>(count), 8);
                for (int i = 0; i < count; ++i) {
                    S_NetPlayerState &player = const_cast<S_NetPlayerState &>(players[i]);
                    msg->WriteBits(static_cast<uint32_t>(player.connection), 32);
                    msg->WriteString(player.name);
                    const int human = player.human ? player.human->GetNetId() : -1;
                    msg->WriteBits(static_cast<uint32_t>(human), 32);
                    player.stats.Write(msg);
                    if (human == -1)
                        continue;
                    WritePlace(msg, player.human, false);
                    S_SoldierState state;
                    state.profileIndex = -1;
                    state.unknown34 = -1;
                    state.unknown38 = -1;
                    state.team = -1;
                    state.spawnZone = 0x3FF;
                    state.deployed = true;
                    state.speechSet = -1;
                    player.human->GetSoldierState(state);
                    NetWriteSoldierState(msg, state, false);
                }
                g_netGame.UnlockPlayers();
                ModuleManager::BroadcastWriteState(msg);
                GetGame()->WriteObjectiveStates(msg);
                const int slot = g_netGame.FindPlayer(queued.to);
                if (slot != -1) {
                    S_NetPlayerState *all = g_netGame.LockPlayers();
                    all[slot].joinStage = 2;
                    g_netGame.UnlockPlayers();
                }
                g_netGame.m_unknown8 = true;
            }
            break;
        case 0x16:
            msg->WriteBits(static_cast<uint32_t>(value), 7);
            msg->WriteBits(static_cast<uint32_t>(queued.b), 10);
            break;
        case 0x17:
            msg->WriteBits(static_cast<uint32_t>(value), 1);
            break;
        default:
            break;
        }
        g_netGame.SendNetMessage(msg);
        g_gameMessages.pop_front();
    }
}

void C_GameApp::PostCommand(int command, intptr_t a, intptr_t b, bool flush)
{
    if (!flush) {
        if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL)
            EnterCriticalSection(&m_commandLock);
        g_commands.push_back({a, b, command});
        if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL)
            LeaveCriticalSection(&m_commandLock);
        return;
    }
    while (!g_commands.empty()) {
        const S_QueuedCommand queued = g_commands.front();
        C_BitStream *msg = g_netGame.BeginCommand(queued.command);
        auto *text = reinterpret_cast<char *>(queued.a);
        const auto local = static_cast<uint32_t>(g_localPlayerSlot);
        switch (queued.command) {
        case 0:
        case 1: {
            // The player's soldier (the squad's first; Fix: none sends an empty state, the original read null).
            msg->WriteBits(local, 32);
            const std::vector<S_SquadSoldier *> &soldiers = g_gameProfile.m_missionSquad->states;
            if (soldiers.empty()) {
                S_SoldierState none;
                NetWriteSoldierState(static_cast<C_NetMessage *>(msg), none, false);
            } else {
                NetWriteSoldierState(static_cast<C_NetMessage *>(msg), *soldiers[0], false);
            }
            break;
        }
        case 2:
            msg->WriteBits(local, 32);
            break;
        case 3:
            // A chat line.
            msg->WriteBits(*reinterpret_cast<const uint32_t *>(text + 0x104), 32);
            msg->WriteBits(*reinterpret_cast<const uint32_t *>(text + 0x108), 32);
            msg->WriteBits(static_cast<uint8_t>(text[0x10C]), 1);
            msg->WriteString(text);
            operator delete(text);
            break;
        case 4:
            msg->WriteBits(static_cast<uint32_t>(queued.a), 32);
            break;
        case 5:
            msg->WriteBits(local, 32);
            msg->WriteBits(static_cast<uint32_t>(queued.a), 32);
            break;
        case 6:
        case 7:
            msg->WriteBits(local, 5);
            msg->WriteString(text);
            operator delete(text);
            break;
        case 8:
            msg->WriteString(text);
            operator delete(text);
            break;
        case 9: {
            // The actors from 32 counted.
            msg->WriteBits(local, 5);
            int actors = 0;
            for (int i = 32; i < g_actorsModule.GetActorCount(); ++i)
                actors += g_actorsModule.GetActor(i) != nullptr;
            msg->WriteBits(static_cast<uint32_t>(actors), 16);
            break;
        }
        case 10:
            msg->WriteBits(static_cast<uint32_t>(queued.a), 7);
            msg->WriteBits(static_cast<uint32_t>(queued.b), 10);
            break;
        default:
            break;
        }
        g_netGame.SendNetMessage(msg);
        g_commands.pop_front();
    }
}
