// The network game (part 10d-i): C_NetGame's construction, its message streams and queues, the frame's sending and
// receiving, the votes, closing, the player table's basics. See net_game.h.
#include <winsock2.h>
#include <mmsystem.h>

#include "net/net_game.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "game/console.h"
#include "game/controls.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_texts.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/module.h"
#include "game/pending.h"
#include "game/perf_timer.h"
#include "game/profile.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/dplay8.h"
#include "net/net_channel.h"
#include "net/net_object.h"
#include "net/net_transport.h"

C_NetGame g_netGame;

namespace {

// The message types' width: floor(log2(20) / log2(2)) + 1 (the original works it out each time).
constexpr int kTypeBits = 5;

// The players a stream goes to: the server's connected players but itself; a client's server (slot 0).
bool SendsTo(const C_NetGame &net, size_t slot)
{
    const uint32_t role = g_gameRole;
    return (net.m_players[slot].connection != 0 || !(role & GAME_ROLE_SERVER)) &&
           (static_cast<int>(slot) != g_localPlayerSlot || !(role & GAME_ROLE_SERVER)) &&
           (slot == 0 || (role & GAME_ROLE_SERVER));
}

// The level scan's bounds (Command 3).
float g_scanMin[3];                                  // 0x0097ca30
float g_scanMax[3];                                  // 0x0097ca40

}  // namespace

// ---- the lock --------------------------------------------------------------------------------------------------------

static_assert(sizeof(CRITICAL_SECTION) <= 40, "C_NetLock storage");

void C_NetLock::Init()
{
    InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(m_storage));
}

void C_NetLock::Delete()
{
    DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(m_storage));
}

void C_NetLock::Enter()
{
    EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(m_storage));
}

void C_NetLock::Leave()
{
    LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(m_storage));
}

// ---- the player state and the vote ----------------------------------------------------------------------------------

S_NetPlayerState::S_NetPlayerState()
{
    team = 2;
    pendingTeam = 2;
    unknown70 = -1;
    unknownB4[4] = 50;                               // (+0xB8 = 50)
}

S_NetVote::S_NetVote() = default;

void S_NetVote::Start(int kind_, int arg_)
{
    if (kind != 0x42)
        return;
    kind = kind_;
    time = 0;
    voted = false;
    yes = 0;
    arg = arg_;
    GetGameApp()->SendGameMessage(0x16, 0, kind_, arg_, false);
}

void S_NetVote::Begin(int kind_, int arg_)
{
    kind = kind_;
    arg = arg_;
    time = 0;
    voted = false;
    const C_str text = g_texts.Get(0x22E2);
    GetGame()->PrintMessage(S_vector(50.0f, 150.0f, 50.0f), "%s %d (%s)", text.c_str(), arg_,
                            g_votableCommands[kind_]);
}

bool S_NetVote::Vote()
{
    if (voted)
        return false;
    voted = true;
    GetGameApp()->PostCommand(0xB, 0, 0, false);
    return true;
}

void S_NetVote::CountYes()
{
    if (kind == 0x42)
        return;
    const int count = ++yes;
    if (static_cast<float>(g_netGame.CountPlayers()) * 0.5f < static_cast<float>(count)) {
        char command[100];
        std::sprintf(command, "%s %d", g_votableCommands[kind], arg);
        GetCommandProcessor()->Execute(command);
        End(1);
    }
}

void S_NetVote::End(uint32_t passed)
{
    kind = 0x42;
    GetGameApp()->SendGameMessage(0x17, 0, passed & 0xFF, 0, false);
}

void PrintVoteResult(bool passed)
{
    const C_str text = g_texts.Get(passed ? 0x22E3 : 0x22E4);
    GetGame()->PrintMessage(S_vector(50.0f, 150.0f, 50.0f), "%s", text.c_str());
}

void PostVote(int kind, int arg)
{
    GetGameApp()->PostCommand(10, kind, arg, false);
}

// ---- construction --------------------------------------------------------------------------------------------------

C_NetGame::C_NetGame()
{
    m_transport = new C_NetTransport;
    reinterpret_cast<S_ServerEntry *>(m_unknown4E00)->Reset();
    std::memset(m_bandwidthTotal, 0, sizeof m_bandwidthTotal);
    std::memset(m_bandwidthAverage, 0, sizeof m_bandwidthAverage);
    std::memset(m_bandwidthLast, 0, sizeof m_bandwidthLast);
    g_gameRole = GAME_ROLE_LOCAL;
    for (int i = 0; i < 50; ++i) {
        m_sentHistory[i] = 0;
        m_receivedHistory[i] = 0;
    }
    m_historyIndex2 = 0;
    m_historyIndex = 0;
    InitPerfTime();
}

C_NetGame::~C_NetGame()
{
    // (The queues' datagrams are not given back: as the original.)
    delete m_transport;
}

void C_NetGame::Init()
{
    m_unknown4EE0 = nullptr;
    m_unknown503C = 0;
    g_gameRole = GAME_ROLE_LOCAL;
    m_sendPeriod = 50;
    m_sendTime = 0;
    m_unknown5060 = true;
    m_unknown448 = 0;
    m_serverBrowser = nullptr;
    m_playersLock.Init();
    m_queueLock.Init();
    m_locksMade = true;
    m_joinStatus = 0;
    m_reporting = false;
    m_keySocket = static_cast<uintptr_t>(-1);
    m_replication = nullptr;
    m_maxPing = 0;
    m_unknown5068 = 50;
    m_maxInactivity = 300000;
    m_voiceServer = nullptr;
    m_voiceClient = nullptr;
    m_unknown8 = false;
    m_packetCallback = TransportCallback;
    // 32 player slots.
    for (int i = 0; i < 32; ++i)
        m_players.push_back(S_NetPlayerState());
}

void C_NetGame::Shutdown()
{
    Close(true);
    if (m_voiceClient) {
        delete m_voiceClient;
        m_voiceClient = nullptr;
    }
    if (m_voiceServer) {
        delete m_voiceServer;
        m_voiceServer = nullptr;
    }
    DeleteReplication();
    if (m_locksMade) {
        m_playersLock.Delete();
        m_queueLock.Delete();
        m_locksMade = false;
    }
    g_gameRole = GAME_ROLE_LOCAL;
    if (m_unknown4EE0) {
        m_unknown4EE0->Destroy(true);
        m_unknown4EE0 = nullptr;
    }
}

void C_NetGame::Close(bool now)
{
    if (!now) {
        m_closePending = true;
        return;
    }
    StopReporting(nullptr, false);
    GetIGraph()->SetFlags(m_graphFlags);
    Process(1000, false);
    m_closePending = false;
    m_unknown448 = 0;
    if (g_gameProfile.GetDetailSettings()->voiceChat) {
        StopVoiceClient();
        StopVoiceServer();
    }
    if (m_keySocket != static_cast<uintptr_t>(-1)) {
        closesocket(m_keySocket);
        m_keySocket = static_cast<uintptr_t>(-1);
    }
    if (g_netQueryThread) {
        WaitForSingleObject(g_netQueryThread, 0);
        CloseHandle(g_netQueryThread);
        g_netQueryThread = nullptr;
    }
    if (m_server) {
        m_server->CancelAsyncOperation(0, DPNCANCEL_ALL_OPERATIONS);
        m_server->Close(0);
        if (m_server) {
            m_server->Release();
            m_server = nullptr;
        }
    }
    if (m_client) {
        m_client->CancelAsyncOperation(0, DPNCANCEL_ALL_OPERATIONS);
        m_client->Close(0);
        if (m_client) {
            m_client->Release();
            m_client = nullptr;
        }
    }
    m_reporting = false;
    m_joinStatus = 0;
    g_gameRole = GAME_ROLE_LOCAL;
    if (m_replication)
        m_replication->DeleteObjects();
    m_transport->DeleteConnections();
    m_serverLink = nullptr;
    m_transport->Stop();
    m_playersLock.Enter();
    m_players.clear();
    m_playersLock.Leave();
    m_string502C = g_netDefaultSessionText;
    m_queueLock.Enter();
    while (!m_received.empty()) {
        const S_NetQueued queued = m_received.front();
        m_received.pop_front();
        g_netPool.FreeBuffer(queued.data, queued.size);
    }
    while (!m_outgoing.empty()) {
        const S_NetQueued queued = m_outgoing.front();
        m_outgoing.pop_front();
        g_netPool.FreeBuffer(queued.data, queued.size);
    }
    m_queueLock.Leave();
    if (g_gameProfile.m_mpSettings->unknown174 == 1)
        StopGameSpyQueries(nullptr);
    g_localPlayerSlot = 0;
}

// ---- the message streams -------------------------------------------------------------------------------------------

C_BitStream *C_NetGame::CreateMessage(int type, int channel)
{
    return BeginMessage(0, type, true, channel);
}

C_BitStream *C_NetGame::CreateMessageEx(int type, int channel)
{
    return BeginMessage(0, type, false, channel);
}

C_BitStream *C_NetGame::BeginMessage(int target, int type, bool reliable, int channel)
{
    if (channel > 2)
        channel = 2;
    C_NetMessage *msg;
    if (!reliable && (g_gameRole & GAME_ROLE_SERVER))
        msg = &m_unreliable;
    else if (target == 0)
        msg = &m_messages[channel];
    else
        msg = &m_direct;
    msg->m_channel = channel;
    msg->m_pending = 1;
    msg->m_target = target;
    msg->m_unreliable = !reliable;
    msg->WriteBits(static_cast<uint32_t>(type), kTypeBits);
    msg->m_type = static_cast<uint32_t>(type);
    return msg;
}

void C_NetGame::SendNetMessage(C_BitStream *stream)
{
    auto *msg = static_cast<C_NetMessage *>(stream);
    msg->m_pending = 0;
    if (msg->m_target == 0)
        return;
    msg->WriteBits(NET_MESSAGE_END, kTypeBits);
    const int slot = FindPlayer(msg->m_target);
    if (slot == g_localPlayerSlot)
        QueueOutgoing(msg->Data(), msg->ByteSize(), 0);
    else
        SendToPlayers(slot, msg->Data(), msg->ByteSize(), 0x42);
    msg->ResetPosition();
}

void C_NetGame::QueueReceived(const void *data, int size, uint32_t flags)
{
    void *copy = g_netPool.AllocBuffer(static_cast<size_t>(size));
    const int time = static_cast<int>(clock());
    std::memcpy(copy, data, static_cast<size_t>(size));
    m_queueLock.Enter();
    m_received.push_back({time, flags, size, copy});
    m_queueLock.Leave();
}

void C_NetGame::QueueOutgoing(const void *data, int size, uint32_t flags)
{
    void *copy = g_netPool.AllocBuffer(static_cast<size_t>(size));
    const int time = static_cast<int>(clock());
    std::memcpy(copy, data, static_cast<size_t>(size));
    m_queueLock.Enter();
    m_outgoing.push_back({time, flags, size, copy});
    m_queueLock.Leave();
}

void __fastcall TransportCallback(int, void *data, int size, int kind, uint32_t flags)
{
    if (g_gameRole & GAME_ROLE_SERVER)
        g_netGame.QueueReceived(data, size, static_cast<uint32_t>(kind) << 5 | flags);
    else
        g_netGame.QueueOutgoing(data, size, static_cast<uint32_t>(kind) << 5 | flags);
}

void C_NetGame::SendToPlayers(int slot, const void *data, int size, uint32_t flags)
{
    if (g_gameRole & GAME_ROLE_SERVER) {
        m_playersLock.Enter();
        for (size_t i = 0; i < m_players.size(); ++i) {
            if (slot == -1 || static_cast<int>(i) == slot) {
                C_NetConnection *link = m_players[i].link;
                if (link && m_players[i].joinStage == 2)
                    link->Send(data, size, static_cast<int>(flags) >> 5, static_cast<int>(flags & 3));
            }
        }
        m_playersLock.Leave();
        return;
    }
    if (m_serverLink)
        m_serverLink->Send(data, size, static_cast<int>(flags) >> 5, static_cast<int>(flags & 3));
}

bool C_NetGame::DispatchNetMessage(C_BitStream *stream, bool toClient)
{
    const uint32_t type = stream->ReadBits(kTypeBits);
    if (type == NET_MESSAGE_END)
        return false;
    if (type == NET_MESSAGE_GAME) {
        const int id = static_cast<int>(stream->ReadBits(32));
        if (toClient)
            GetGameApp()->HandleClientCommand(id, stream);
        else
            GetGameApp()->HandleServerCommand(id, stream);
        return true;
    }
    if (!GetGame() || !GetGame()->IsMissionLoaded())
        return false;
    if (toClient)
        return ModuleManager::DispatchMessage(static_cast<int>(type), stream);
    return ModuleManager::CallUnknown28(static_cast<int>(type), stream);
}

void C_NetGame::ReceiveMessages(int, bool beforeUpdate)
{
    std::list<S_NetQueued> *queue;
    if (!beforeUpdate) {
        queue = &m_outgoing;
    } else {
        if (!(g_gameRole & GAME_ROLE_SERVER))
            return;
        if (g_gameProfile.m_mpSettings->unknown174 == 1)
            AnswerQueries();
        queue = &m_received;
    }
    for (;;) {
        m_queueLock.Enter();
        if (queue->empty()) {
            m_queueLock.Leave();
            return;
        }
        const S_NetQueued queued = queue->front();
        queue->pop_front();
        m_queueLock.Leave();
        if (!(queued.flags & 0x20)) {
            C_NetMessage msg;
            msg.Attach(queued.data, static_cast<uint32_t>(queued.size) * 8);
            while (msg.BitPosition() < msg.CapacityBits()) {
                if (!DispatchNetMessage(&msg, !beforeUpdate))
                    break;
            }
        } else if (GetGame() && m_replication && queued.data && queued.size) {
            // The replication's data (the kind's bit 1: from a client).
            C_NetMessage msg;
            msg.Attach(queued.data, static_cast<uint32_t>(queued.size) * 8);
            m_replication->Read(&msg, 1, (queued.flags >> 1) & 1);
        }
        g_netPool.FreeBuffer(queued.data, queued.size);
    }
}

// ---- the frame -----------------------------------------------------------------------------------------------------

void C_NetGame::Process(int timeDelta, bool replicate)
{
    const int period = m_sendPeriod;
    m_sendTime += timeDelta;
    if (m_sendTime < period)
        return;
    m_sendTime -= period;
    if (period * 2 < m_sendTime)
        m_sendTime = 0;
    if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL && m_joinStatus == NET_JOIN_JOINING) {
        m_joinTime += timeDelta;
        if (m_joinTime > 30000)
            m_joinStatus = NET_JOIN_TIMED_OUT;
        m_transport->FlushAll(true);
    }
    UpdateLags(timeDelta);
    for (C_NetMessage &msg : m_messages) {
        if (msg.BitPosition() == 0)
            continue;
        msg.WriteBits(NET_MESSAGE_END, kTypeBits);
        if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL) {
            for (size_t i = 0; i < m_players.size(); ++i) {
                if (SendsTo(*this, i))
                    SendToPlayers(static_cast<int>(i), msg.Data(), msg.ByteSize(), 0x42);
            }
        }
        if ((g_gameRole & GAME_ROLE_SERVER) && (g_gameRole & GAME_ROLE_CLIENT))
            QueueOutgoing(msg.Data(), msg.ByteSize(), 0);
        msg.ResetPosition();
    }
    if (m_unreliable.BitPosition() != 0) {
        m_unreliable.WriteBits(NET_MESSAGE_END, kTypeBits);
        QueueReceived(m_unreliable.Data(), m_unreliable.ByteSize(), 0);
        m_unreliable.ResetPosition();
    }
    m_direct.ResetPosition();
    if ((g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL)
        return;
    if (replicate && m_replication && m_replication->HasObjects()) {
        // The replication's data (written for player 0) to each player: the server's (0x20), then the clients'
        // messages (0x22) when there were any.
        m_replicationMessage.ResetPosition();
        m_replication->Write(&m_replicationMessage, 0, false);
        for (size_t i = 0; i < m_players.size(); ++i) {
            if (SendsTo(*this, i))
                SendToPlayers(static_cast<int>(i), m_replicationMessage.Data(), m_replicationMessage.ByteSize(), 0x20);
        }
        m_replicationMessage.ResetPosition();
        if (m_replication->Write(&m_replicationMessage, 0, true)) {
            for (size_t i = 0; i < m_players.size(); ++i) {
                if (SendsTo(*this, i))
                    SendToPlayers(static_cast<int>(i), m_replicationMessage.Data(), m_replicationMessage.ByteSize(),
                                  0x22);
            }
        }
    }
    if (m_server || m_client)
        m_transport->FlushAll(false);
}

void C_NetGame::UpdateLags(int)
{
    static S_PerfTimer timer(3000);                  // 0x0097ca14
    timer.running = true;
    if (!timer.Fire() || g_gameRole == GAME_ROLE_LOCAL || g_gameRole == 0x12)
        return;
    if (!(g_gameRole & GAME_ROLE_SERVER) && !m_client)
        return;
    if ((g_gameRole & GAME_ROLE_SERVER) && !m_server)
        return;
    LockPlayers();
    if (!(g_gameRole & GAME_ROLE_SERVER)) {
        m_transport->FindById(g_localPlayerSlot);
        if (C_NetConnection *link = m_serverLink) {
            link->SetResendTime(static_cast<int>(link->m_roundTrip) + 200);
            for (size_t i = 0; i < m_players.size(); ++i) {
                const S_NetPlayerState &player = m_players[i];
                if (!player.human || !player.name[0])
                    continue;
                C_NetObject *object = static_cast<int>(i) == g_localPlayerSlot
                                          ? &m_replication->GetLocalObject()
                                          : m_replication->FindObject(static_cast<int>(i), false);
                if (object)
                    object->SetLag(static_cast<float>(static_cast<int>(m_serverLink->m_roundTrip >> 1) + 5 +
                                                      player.stats[4]) * 0.001f);
            }
        }
    } else {
        m_transport->Lock();
        const DWORD now = timeGetTime();
        for (size_t i = 0; i < m_players.size(); ++i) {
            S_NetPlayerState &player = m_players[i];
            if (player.link && static_cast<int>(GetPerfTimeMs()) - player.link->m_lastReceive > 5000)
                m_server->DestroyClient(static_cast<DPNID>(player.connection), nullptr, 0, 0);
            if (!player.human || !player.name[0])
                continue;
            C_NetConnection *link = m_transport->FindById(static_cast<int>(i));
            if (!link)
                continue;
            if (player.link)
                player.link->SetResendTime(static_cast<int>(link->m_roundTrip) + 200);
            C_NetObject *object = m_replication->FindObject(static_cast<int>(i), false);
            if (!object)
                continue;
            object->SetLag(static_cast<float>(static_cast<int>(link->m_roundTrip >> 1) + 5) * 0.001f);
            if (m_maxPing != 0 && m_maxPing < static_cast<int>(link->m_roundTrip))
                ++player.highPings;
            if (player.highPings > 2)
                KickPlayer(static_cast<int>(i), 2);
            if (m_maxInactivity != 0 && m_maxInactivity <= static_cast<int>(now - player.activityTime) && player.human &&
                !player.human->GetController()->IsSpectating()) {
                player.activityTime = now;
                KickPlayer(static_cast<int>(i), 3);
            }
        }
        m_transport->Unlock();
    }
    UnlockPlayers();
}

void C_NetGame::SendJoinData()
{
    m_playersLock.Enter();
    for (size_t i = 0; i < m_players.size(); ++i) {
        S_NetPlayerState &player = m_players[i];
        if (player.name[0] && player.link && player.link->m_port != -1 && player.joinStage == 0) {
            player.joinStage = 2;
            GetGameApp()->SendGameMessage(0, 0, 0, 0, true);
            GetGameApp()->SendGameMessage(0, player.connection, 0, 0, false);
            GetGameApp()->SendGameMessage(0, 0, 0, 0, true);
            GetGameApp()->SendGameMessage(9, 0, reinterpret_cast<intptr_t>(player.name), 0, false);
            m_players[i].joinStage = 1;
        }
    }
    m_playersLock.Leave();
}

void C_NetGame::SendMessages(int timeDelta)
{
    static int bandwidthTime;                        // 0x0097ca10
    static int bandwidthCount;                       // 0x0097ca0c
    if (g_gameRole & GAME_ROLE_SERVER)
        SendJoinData();
    Process(timeDelta, true);
    if (g_gameProfile.m_mpSettings->dedicated) {
        for (char *command = GetConsole()->PopCommand(); command; command = GetConsole()->PopCommand())
            GetCommandProcessor()->Execute(command);
    }
    if (g_gameRole == GAME_ROLE_LOCAL || g_gameRole == 0x12 || !(g_gameRole & GAME_ROLE_CLIENT))
        return;
    bandwidthTime += timeDelta;
    if (bandwidthTime < 100)
        return;
    bandwidthTime -= 100;
    int figures[7];
    TakeBandwidth(figures);
    if (figures[0] == -2 || figures[0] < 0)
        return;
    std::memcpy(m_bandwidthLast, figures, sizeof figures);
    if (bandwidthCount++ == 5) {
        // Every sixth: the sums scaled (the count by a fifth, the rest doubled) and kept as the average.
        bandwidthCount = 0;
        m_bandwidthTotal[0] /= 5;
        for (int i = 1; i < 7; ++i)
            m_bandwidthTotal[i] *= 2;
        std::memcpy(m_bandwidthAverage, m_bandwidthTotal, sizeof m_bandwidthTotal);
        std::memset(m_bandwidthTotal, 0, sizeof m_bandwidthTotal);
    }
    for (int i = 0; i < 7; ++i)
        m_bandwidthTotal[i] += figures[i];
    m_historyIndex = (m_historyIndex + 1) % 50;
    m_historyIndex2 = (m_historyIndex2 + 1) % 50;
    m_receivedHistory[m_historyIndex] = static_cast<int16_t>(figures[3] + figures[1]);
    m_sentHistory[m_historyIndex2] = static_cast<int16_t>(figures[4] + figures[2]);
}

// ---- commands and votes --------------------------------------------------------------------------------------------

bool C_NetGame::Command(int command, const char *arg, int)
{
    if (command == NET_COMMAND_KICK_NAME) {
        if (!arg || !*arg)
            return false;
        LockPlayers();
        for (size_t i = 1; i < m_players.size(); ++i) {
            if (std::strcmp(arg, m_players[i].name) == 0) {
                UnlockPlayers();
                Command(NET_COMMAND_KICK_SLOT, reinterpret_cast<const char *>(i), 0);
                return true;
            }
        }
    } else if (command == NET_COMMAND_KICK_SLOT) {
        LockPlayers();
        const auto slot = reinterpret_cast<intptr_t>(arg);
        if (slot > 0 && static_cast<size_t>(slot) < m_players.size()) {
            const int connection = m_players[static_cast<size_t>(slot)].connection;
            UnlockPlayers();
            return m_server->DestroyClient(static_cast<DPNID>(connection), nullptr, 0, 0) == 0;
        }
    } else if (command == NET_COMMAND_SCAN_LEVEL) {
        for (int i = 0; i < 3; ++i) {
            g_scanMin[i] = 1e16f;
            g_scanMax[i] = -1e16f;
        }
        I3D_sector *sector = GetGame()->GetScene()->GetPrimarySector();
        sector->EnumFrames(ScanFrameBounds, nullptr, 1, 0);
        for (int i = 0; i < 3; ++i)
            m_levelMin[i] = g_scanMin[i] - 0.05f;
        for (int i = 0; i < 3; ++i)
            m_levelSize[i] = (g_scanMax[i] - g_scanMin[i]) + 0.1f;
        g_netGame.SetLevelBounds(m_levelMin, m_levelSize);
        for (int i = 0; i < 3; ++i)
            m_levelBits[i] = BitsForRange(&m_levelSteps[i], 0.0f);
        m_unknown505C = 0;
        if ((g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL)
            g_localPlayerSlot = 0;
        std::memset(m_bandwidthTotal, 0, sizeof m_bandwidthTotal);
        std::memset(m_bandwidthAverage, 0, sizeof m_bandwidthAverage);
        std::memset(m_bandwidthLast, 0, sizeof m_bandwidthLast);
        return false;
    } else {
        return false;
    }
    UnlockPlayers();
    return false;
}

uint32_t __stdcall ScanFrameBounds(I3D_frame *frame, void *)
{
    const I3D_bbox &box = frame->GetBound().bbox;
    const S_vector &low = box.min, &high = box.max;
    if (low.x < g_scanMin[0])
        g_scanMin[0] = low.x;
    if (low.y < g_scanMin[1])
        g_scanMin[1] = low.y;
    if (low.z < g_scanMin[2])
        g_scanMin[2] = low.z;
    if (g_scanMax[0] < high.x)
        g_scanMax[0] = high.x;
    if (g_scanMax[1] < high.y)
        g_scanMax[1] = high.y;
    if (g_scanMax[2] < high.z)
        g_scanMax[2] = high.z;
    return 0;
}

int BitsForRange(uint32_t *steps, float range)
{
    const auto n = static_cast<uint32_t>(static_cast<int64_t>(range));
    *steps = n;
    uint32_t span = 2;
    int bits = 1;
    while (span < n) {
        span *= 2;
        ++bits;
    }
    return bits;
}

void C_NetGame::UpdateVote(int timeDelta)
{
    if (m_vote.kind == 0x42)
        return;
    m_vote.time += timeDelta;
    if ((g_gameRole & GAME_ROLE_SERVER) && m_vote.time > 15000)
        m_vote.End(0);
}

// ---- the players ---------------------------------------------------------------------------------------------------

void C_NetGame::ResetScores()
{
    m_playersLock.Enter();
    for (S_NetPlayerState &player : m_players) {
        std::memset(player.stats, 0, sizeof player.stats);
        player.teamKills = 0;
    }
    m_playersLock.Leave();
    for (int team = 0; team < 3; ++team) {
        std::memset(m_teamStats[team], 0, sizeof m_teamStats[team]);
        m_teamScores[team] = 0.0f;
    }
}

void C_NetGame::ResetPlayerStats()
{
    m_playersLock.Enter();
    for (S_NetPlayerState &player : m_players) {
        player.human = nullptr;
        std::memset(player.stats, 0, sizeof player.stats);
    }
    m_playersLock.Leave();
    if (m_replication)
        m_replication->DeleteObjects();
    for (int team = 0; team < 3; ++team) {
        std::memset(m_teamStats[team], 0, sizeof m_teamStats[team]);
        m_teamScores[team] = 0.0f;
    }
}

void C_NetGame::SetPlayer(int slot, int connection, const char *name)
{
    if (!(g_gameRole & GAME_ROLE_CLIENT))
        return;
    LockPlayers();
    if (m_players.size() <= static_cast<size_t>(slot)) {
        S_NetPlayerState empty;
        empty.connection = 0;
        empty.name[0] = 0;
        while (m_players.size() <= static_cast<size_t>(slot))
            m_players.push_back(empty);
    }
    m_players[static_cast<size_t>(slot)].connection = connection;
    std::strcpy(m_players[static_cast<size_t>(slot)].name, name);
    UnlockPlayers();
}

void C_NetGame::SetVoiceValue(int value)
{
    if (!m_voiceClient)
        return;
    uint32_t config[8] = {};
    config[0] = 0x20;
    config[1] = 0x28;
    config[2] = 1;
    config[3] = static_cast<uint32_t>(value);
    config[4] = 0xFFFFFFFE;
    m_voiceClient->SetClientConfig(config);
}

bool C_NetGame::SendChat(const char *text, bool team)
{
    auto *line = static_cast<char *>(operator new(0x110));
    std::strncpy(line, text, 100);
    line[99] = 0;
    *reinterpret_cast<int *>(line + 0x104) = g_localPlayerSlot;
    line[0x10C] = team;
    GetGameApp()->PostCommand(3, reinterpret_cast<intptr_t>(line), 0, false);
    return true;
}

int C_NetGame::FindPlayerByName(const char *name)
{
    m_playersLock.Enter();
    for (size_t i = 0; i < m_players.size(); ++i) {
        if (std::strcmp(m_players[i].name, name) == 0) {
            m_playersLock.Leave();
            return static_cast<int>(i);
        }
    }
    m_playersLock.Leave();
    return -1;
}

const char *C_NetGame::GetPlayerName(int slot)
{
    if (slot >= 0 && static_cast<size_t>(slot) < m_players.size())
        return m_players[static_cast<size_t>(slot)].name;
    return nullptr;
}

C_Human *C_NetGame::GetPlayerHuman(int slot)
{
    if (slot >= 0 && static_cast<size_t>(slot) < m_players.size())
        return m_players[static_cast<size_t>(slot)].human;
    return nullptr;
}

S_NetPlayerState *C_NetGame::LockPlayers()
{
    m_playersLock.Enter();
    return m_players.data();
}

int C_NetGame::FindPlayer(int connection)
{
    m_playersLock.Enter();
    for (size_t i = 0; i < m_players.size(); ++i) {
        if (m_players[i].name[0] && m_players[i].connection == connection) {
            m_playersLock.Leave();
            return static_cast<int>(i);
        }
    }
    m_playersLock.Leave();
    return -1;
}

void C_NetGame::UnlockPlayers()
{
    m_playersLock.Leave();
}

int C_NetGame::GetBestScore()
{
    m_playersLock.Enter();
    int best = 0;
    for (const S_NetPlayerState &player : m_players) {
        if (player.name[0] && player.human && best < player.stats[3])
            best = player.stats[3];
    }
    m_playersLock.Leave();
    return best;
}

void C_NetGame::AddScore(int slot, int points)
{
    m_playersLock.Enter();
    if (static_cast<size_t>(slot) < m_players.size()) {
        int *stats = m_players[static_cast<size_t>(slot)].stats;
        stats[3] = stats[3] + points;
        stats[8] += points;
        if (g_gameRole & GAME_ROLE_SERVER)
            SendPlayerScore(slot, false);
    }
    m_playersLock.Leave();
}
