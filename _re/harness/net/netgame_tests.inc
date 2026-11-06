// (Part 10d-i.) C_NetGame: construction, the message streams and queues, the frame, votes, closing, the players.
// Included by netgame_test.cpp.

C_NetGame &Fresh()
{
    static C_NetGame *net;
    if (net) {
        net->m_players.clear();
        net->m_received.clear();
        net->m_outgoing.clear();
    } else {
        net = new C_NetGame;
        net->Init();
        net->m_players.clear();
    }
    g_log.clear();
    return *net;
}

S_NetPlayerState Player(int connection, const char *name)
{
    S_NetPlayerState p;
    p.connection = connection;
    std::strcpy(p.name, name);
    return p;
}

extern int64_t g_perfOffset;

// A human whose controller answers IsSpectating (vtable +0x64, thiscall) from its flag.
struct T_Controller {
    void **vtable;
    bool spectating;
};
bool __attribute__((thiscall)) FakeIsSpectating(T_Controller *self)
{
    return self->spectating;
}
C_Human *MakeHuman(bool spectating)
{
    static void *vtable[64];
    static T_Controller controllers[64];
    alignas(16) static unsigned char storage[64][sizeof(C_Human)];
    static int used;
    for (void *&slot : vtable)
        slot = reinterpret_cast<void *>(&FakeIsSpectating);
    T_Controller &controller = controllers[used];
    controller.vtable = vtable;
    controller.spectating = spectating;
    auto *human = reinterpret_cast<C_Human *>(storage[used++]);
    human->m_controller = reinterpret_cast<C_HumanController *>(&controller);
    return human;
}

int ResendTime(C_NetConnection *link)
{
    return static_cast<C_NetReliableChannel *>(link->m_channels[1])->m_resendTime;
}
float Lag(C_NetObject *object)
{
    return object->GetProperty(0)->GetTrack().GetLag();
}
bool Near(float a, float b)
{
    return std::fabs(a - b) < 1e-6f;
}
C_NetObject *LagObject(C_NetReplication &replication, int slot)
{
    C_NetObject *object = replication.FindObject(slot, true);
    object->AddProperty(0);
    return object;
}

void TestNetGameConstruction()
{
    g_gameRole = 0;
    g_log.clear();
    auto *net = new C_NetGame;
    CHECK(g_gameRole == GAME_ROLE_LOCAL && net->m_transport != nullptr && Logged("server entry reset"));
    CHECK(net->m_historyIndex == 0 && net->m_receivedHistory[49] == 0 && net->m_vote.kind == 0x42 && !net->m_vote.voted);
    // Init: the defaults, 32 player slots.
    net->m_sendPeriod = 7;
    g_netMaxInactivity = 1;
    g_gameRole = 0;
    net->Init();
    CHECK(g_gameRole == GAME_ROLE_LOCAL && net->m_sendPeriod == 50 && net->m_sendTime == 0 && net->m_unknown5060 &&
          net->m_locksMade && net->m_joinStatus == 0 && net->m_keySocket == static_cast<uintptr_t>(-1) &&
          g_netMaxPing == 0 && g_netMaxFreq == 50 && g_netMaxInactivity == 300000 &&
          net->m_packetCallback == TransportCallback && g_netReplication == nullptr);
    CHECK(net->m_players.size() == 32 && net->m_players[5].team == 2 && net->m_players[5].pendingTeam == 2 &&
          net->m_players[5].keyQuery == -1 && net->m_players[5].unknownB8 == 50 && !net->m_players[5].name[0] &&
          net->m_players[5].human == nullptr && net->m_players[5].stats[3] == 0);
    // The default player state.
    S_NetPlayerState p;
    CHECK(p.connection == 0 && p.team == 2 && p.pendingTeam == 2 && p.keyQuery == -1 && p.joinStage == 0 &&
          p.link == nullptr && p.highPings == 0 && p.livesUsed == 0);
    // Shut down: closed, the voice objects deleted, the replication deleted, the locks gone.
    net->m_voiceClient = new C_NetVoice(nullptr, nullptr);
    g_log.clear();
    g_testMp.unknown174 = 0;
    g_gameProfile.m_mpSettings = &g_testMp;
    net->Shutdown();
    CHECK(Logged("voice deleted") && Logged("delete replication") && net->m_voiceClient == nullptr && !net->m_locksMade &&
          g_gameRole == GAME_ROLE_LOCAL);
}

void TestStreams()
{
    C_NetGame &net = Fresh();
    // Reliable to all: the channel's stream; the type in 5 bits.
    g_gameRole = GAME_ROLE_SERVER;
    C_BitStream *s = net.CreateMessage(9, 1);
    auto *m = static_cast<C_NetMessage *>(s);
    CHECK(s == &net.m_messages[1] && m->m_channel == 1 && m->m_pending == 1 && m->m_target == 0 && !m->m_unreliable &&
          m->m_type == 9 && m->BitPosition() == 5u);
    m->ResetPosition();
    CHECK(m->ReadBits(5) == 9);
    m->ResetPosition();
    // Channels above 2: 2.
    s = net.CreateMessage(3, 7);
    CHECK(s == &net.m_messages[2] && static_cast<C_NetMessage *>(s)->m_channel == 2);
    net.m_messages[2].ResetPosition();
    // Unreliable: the server's own stream; a client's channel stream.
    s = net.CreateMessageEx(4, 0);
    CHECK(s == &net.m_unreliable && net.m_unreliable.m_unreliable);
    net.m_unreliable.ResetPosition();
    g_gameRole = GAME_ROLE_CLIENT;
    s = net.CreateMessageEx(4, 0);
    CHECK(s == &net.m_messages[0] && net.m_messages[0].m_unreliable);
    net.m_messages[0].ResetPosition();
    // A target: the direct stream (reliable or a client's).
    s = net.BeginMessage(77, 5, true, 0);
    CHECK(s == &net.m_direct && net.m_direct.m_target == 77);
    net.m_direct.ResetPosition();
    // Sent to a target: ended, the slot found; itself: queued as outgoing; others: sent (the client: to the
    // server's link).
    net.m_players.push_back(Player(11, "a"));
    net.m_players.push_back(Player(22, "b"));
    g_localPlayerSlot = 1;
    s = net.BeginMessage(22, 5, true, 0);
    s->WriteBits(0xAB, 8);
    net.SendNetMessage(s);
    CHECK(net.m_outgoing.size() == 1 && net.m_direct.BitPosition() == 0u && net.m_direct.m_pending == 0);
    {
        C_NetMessage back;
        back.Attach(net.m_outgoing.front().data, static_cast<uint32_t>(net.m_outgoing.front().size) * 8);
        CHECK(back.ReadBits(5) == 5 && back.ReadBits(8) == 0xAB && back.ReadBits(5) == NET_MESSAGE_END);
        CHECK(net.m_outgoing.front().size == 3 && net.m_outgoing.front().flags == 0);
    }
    C_NetSocket socket;
    C_NetConnection link(1, 0, &socket);
    net.m_serverLink = &link;
    g_localPlayerSlot = 0;
    s = net.BeginMessage(22, 6, true, 0);
    net.SendNetMessage(s);
    CHECK(link.m_channels[2]->m_outgoing.size() == 1 && link.m_channels[2]->m_outgoing.front()->m_kind == 2);
    // No target: nothing sent (the frame sends the stream).
    s = net.CreateMessage(8, 0);
    net.SendNetMessage(s);
    CHECK(static_cast<C_NetMessage *>(s)->m_pending == 0 && net.m_messages[0].BitPosition() == 5u);
    net.m_messages[0].ResetPosition();
    net.m_serverLink = nullptr;
    for (C_NetChannel *channel : link.m_channels)
        while (C_NetPacket *p = channel->PopOutgoing())
            p->Release();
}

void TestQueues()
{
    C_NetGame &net = Fresh();
    // The transport's callback: the server queues as received, a client as outgoing; the flags carry the kind.
    const uint8_t data[3] = {1, 2, 3};
    g_gameRole = GAME_ROLE_SERVER;
    g_netGame.m_queueLock.Init();
    TransportCallback(5, const_cast<uint8_t *>(data), 3, 2, 1);
    CHECK(g_netGame.m_received.size() == 1 && g_netGame.m_received.front().flags == (2 << 5 | 1) &&
          std::memcmp(g_netGame.m_received.front().data, data, 3) == 0 && g_netGame.m_received.front().data != data);
    g_gameRole = GAME_ROLE_CLIENT;
    TransportCallback(5, const_cast<uint8_t *>(data), 2, 1, 0);
    CHECK(g_netGame.m_outgoing.size() == 1 && g_netGame.m_outgoing.front().size == 2 &&
          g_netGame.m_outgoing.front().flags == 0x20);
    g_netGame.m_received.clear();
    g_netGame.m_outgoing.clear();
    // Received: each message dispatched until the end; a game command to the app (the client's handler for the
    // outgoing queue, the server's for the received one); the module manager's (DispatchMessage, CallUnknown28)
    // only with the mission loaded.
    auto *game = reinterpret_cast<C_game *>(g_gameStorage);
    g_game = game;
    game->m_missionLoaded = true;
    C_GrowingBitStream w;
    w.WriteBits(NET_MESSAGE_GAME, 5);
    w.WriteBits(1234, 32);
    w.WriteBits(0x55, 8);
    w.WriteBits(3, 5);
    w.WriteBits(0x66, 8);
    w.WriteBits(NET_MESSAGE_END, 5);
    w.WriteBits(4, 5);
    net.QueueOutgoing(w.Data(), w.ByteSize(), 0);
    net.ReceiveMessages(0, false);
    CHECK(Logged("client command 1234 85") && Logged("dispatch 3 102") && net.m_outgoing.empty() && g_log.size() == 2);
    g_log.clear();
    g_gameRole = GAME_ROLE_SERVER;
    g_testMp.unknown174 = 1;
    net.m_reporting = true;
    net.QueueReceived(w.Data(), w.ByteSize(), 0);
    net.ReceiveMessages(0, true);
    CHECK(Logged("qr2 think 0") && Logged("gcd think") && Logged("server command 1234 85") && Logged("call28 3 102") && net.m_received.empty());
    net.m_reporting = false;
    g_testMp.unknown174 = 0;
    // A client's received queue: not read.
    g_gameRole = GAME_ROLE_CLIENT;
    net.QueueReceived(w.Data(), w.ByteSize(), 0);
    g_log.clear();
    net.ReceiveMessages(0, true);
    CHECK(g_log.empty() && net.m_received.size() == 1);
    net.m_received.clear();
    // A module not handling it: the rest dropped; no mission: nothing handled.
    C_GrowingBitStream w2;
    w2.WriteBits(7, 5);
    w2.WriteBits(1, 8);
    w2.WriteBits(3, 5);
    w2.WriteBits(2, 8);
    g_gameRole = GAME_ROLE_SERVER;
    net.QueueReceived(w2.Data(), w2.ByteSize(), 0);
    g_log.clear();
    net.ReceiveMessages(0, true);
    CHECK(Logged("call28 7 1") && !Logged("call28 3 2"));
    game->m_missionLoaded = false;
    net.QueueOutgoing(w2.Data(), w2.ByteSize(), 0);
    g_log.clear();
    net.ReceiveMessages(0, false);
    CHECK(g_log.empty());
    // The replication's data (kind bit 0x20): read by the replication, from a client when bit 1 is set.
    C_NetReplication replication;
    g_netReplication = &replication;
    replication.m_enabled = true;
    replication.m_readEpoch = 1;
    C_GrowingBitStream w3;
    w3.WriteBits(1, 1);
    w3.WriteBits(0x1FF, 9);
    net.QueueReceived(w3.Data(), w3.ByteSize(), 0x22);
    net.ReceiveMessages(0, true);
    CHECK(replication.m_bitsRead == 10 && net.m_received.empty());
    // No game: dropped.
    g_game = nullptr;
    net.QueueReceived(w3.Data(), w3.ByteSize(), 0x20);
    net.ReceiveMessages(0, true);
    CHECK(replication.m_bitsRead == 10 && net.m_received.empty());
    g_game = game;
    g_netReplication = nullptr;
}

void TestSendToPlayers()
{
    C_NetGame &net = Fresh();
    C_NetSocket socket;
    C_NetConnection a(1, 0, &socket), b(2, 0, &socket);
    net.m_players.push_back(Player(10, "a"));
    net.m_players.push_back(Player(20, "b"));
    net.m_players.push_back(Player(30, "c"));
    net.m_players[0].link = &a;
    net.m_players[0].joinStage = 2;
    net.m_players[1].link = &b;
    net.m_players[1].joinStage = 1;
    g_gameRole = GAME_ROLE_SERVER;
    const uint8_t data[2] = {9, 8};
    // To one: its link when it is being sent the game; to all (-1).
    net.SendToPlayers(0, data, 2, 3 << 5 | 1);
    CHECK(a.m_channels[1]->m_outgoing.size() == 1 && a.m_channels[1]->m_outgoing.front()->m_kind == 3);
    net.SendToPlayers(1, data, 2, 0);
    CHECK(b.m_channels[0]->m_outgoing.empty());
    net.m_players[1].joinStage = 2;
    net.SendToPlayers(-1, data, 2, 0x42);
    CHECK(a.m_channels[2]->m_outgoing.size() == 1 && b.m_channels[2]->m_outgoing.size() == 1);
    // A client: to the server's link.
    g_gameRole = GAME_ROLE_CLIENT;
    net.SendToPlayers(5, data, 2, 0);
    net.m_serverLink = &b;
    net.SendToPlayers(5, data, 2, 0);
    CHECK(b.m_channels[0]->m_outgoing.size() == 1);
    net.m_serverLink = nullptr;
    for (C_NetConnection *c : {&a, &b})
        for (C_NetChannel *channel : c->m_channels)
            while (C_NetPacket *p = channel->PopOutgoing())
                p->Release();
}

void TestProcess()
{
    C_NetGame &net = Fresh();
    C_NetSocket socket;
    C_NetConnection a(1, 0, &socket), b(2, 0, &socket), self(3, 0, &socket);
    net.m_players.push_back(Player(10, "srv"));
    net.m_players.push_back(Player(20, "b"));
    net.m_players.push_back(Player(0, "nobody"));
    net.m_players.push_back(Player(40, "me"));
    for (int i = 0; i < 4; ++i)
        net.m_players[static_cast<size_t>(i)].joinStage = 2;
    net.m_players[0].link = &a;
    net.m_players[1].link = &b;
    net.m_players[2].link = &self;
    net.m_players[3].link = &self;
    g_localPlayerSlot = 3;
    g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    // Before the period: nothing.
    net.m_sendPeriod = 50;
    net.m_sendTime = 0;
    net.CreateMessage(4, 1)->WriteBits(0x77, 8);
    net.Process(49, false);
    CHECK(net.m_sendTime == 49 && net.m_messages[1].BitPosition() == 13u);
    // The period: the stream ended and sent to every connected player but itself (a server playing: queued for
    // itself too); its kind 2 (0x42).
    net.Process(1, false);
    CHECK(net.m_sendTime == 0 && net.m_messages[1].BitPosition() == 0u);
    CHECK(a.m_channels[2]->m_outgoing.size() == 1 && b.m_channels[2]->m_outgoing.size() == 1 &&
          self.m_channels[2]->m_outgoing.empty() && net.m_outgoing.size() == 1);
    CHECK(a.m_channels[2]->m_outgoing.front()->m_size == 3 && a.m_channels[2]->m_outgoing.front()->m_kind == 2);
    // Far behind: the time dropped.
    net.Process(170, false);
    CHECK(net.m_sendTime == 0);
    net.m_sendTime = 0;
    net.Process(120, false);
    CHECK(net.m_sendTime == 70);
    // A dedicated server (not playing): no queue for itself.
    net.m_outgoing.clear();
    g_gameRole = GAME_ROLE_SERVER;
    net.m_sendTime = 0;
    net.CreateMessage(4, 0)->WriteBits(1, 8);
    net.Process(50, false);
    CHECK(net.m_outgoing.empty() && a.m_channels[2]->m_outgoing.size() == 2);
    // The server's unreliable stream: queued as received for itself.
    net.CreateMessageEx(6, 0)->WriteBits(2, 8);
    net.Process(50, false);
    CHECK(net.m_received.size() == 1 && net.m_unreliable.BitPosition() == 0u);
    net.m_received.clear();
    // A client: to slot 0 only.
    g_gameRole = GAME_ROLE_CLIENT;
    net.m_serverLink = &b;
    net.CreateMessage(4, 0)->WriteBits(3, 8);
    net.Process(50, false);
    CHECK(b.m_channels[2]->m_outgoing.size() == 3);
    net.m_serverLink = nullptr;
    // Joining: the time counted (30 s: timed out).
    net.m_joinStatus = NET_JOIN_JOINING;
    net.m_joinTime = 29990;
    net.Process(50, false);
    CHECK(net.m_joinTime == 30040 && net.m_joinStatus == NET_JOIN_TIMED_OUT);
    // Local: nothing sent.
    g_gameRole = GAME_ROLE_LOCAL;
    net.CreateMessage(4, 0)->WriteBits(3, 8);
    net.Process(50, false);
    CHECK(b.m_channels[2]->m_outgoing.size() == 3 && net.m_messages[0].BitPosition() == 0u);
    // The replication: written and sent to the players (0x20), then the clients' (0x22) when they wrote.
    g_gameRole = GAME_ROLE_SERVER;
    C_NetReplication replication;
    static S_NetQuantization q;
    q.Setup({100, 100, 100, 0}, {-50, -50, -50, 0}, {0.1f, 0.1f, 0.1f, 0}, 100);
    replication.SetReadQuantizations(&q, &q, &q);
    replication.SetWriteQuantizations(&q, &q, &q);
    g_netReplication = &replication;
    net.Process(50, true);
    CHECK(a.m_channels[0]->m_outgoing.empty());
    C_NetObject *object = replication.FindObject(4, false);
    (void)object;
    replication.m_makeFactory = [](int) { return NetMessageFactory(nullptr); };
    replication.m_makeStateReader = [](int) { return NetStateHandler(nullptr); };
    replication.FindObject(4, true)->SetSendMask(NET_SEND_PROPERTIES);
    net.Process(50, true);
    CHECK(a.m_channels[0]->m_outgoing.size() == 1 && a.m_channels[0]->m_outgoing.front()->m_kind == 1 &&
          b.m_channels[0]->m_outgoing.size() == 1);
    g_netReplication = nullptr;
    for (C_NetConnection *c : {&a, &b, &self})
        for (C_NetChannel *channel : c->m_channels)
            while (C_NetPacket *p = channel->PopOutgoing())
                p->Release();
}

void TestJoinData()
{
    C_NetGame &net = Fresh();
    C_NetSocket socket;
    C_NetConnection a(1, 0, &socket);
    net.m_players.push_back(Player(10, "pat"));
    net.m_players.push_back(Player(20, ""));
    net.m_players[0].link = &a;
    net.m_players[1].link = &a;
    // Not before the port is known.
    net.SendJoinData();
    CHECK(g_log.empty());
    a.m_port = 5;
    net.SendJoinData();
    CHECK(g_log.size() == 4 && g_log[0] == "game 0 to 0 a 0 b 0 1" && g_log[1] == "game 0 to 10 a 0 b 0 0" &&
          g_log[2] == "game 0 to 0 a 0 b 0 1" && g_log[3].rfind("game 9 to 0", 0) == 0 &&
          net.m_players[0].joinStage == 1);
    g_log.clear();
    net.SendJoinData();
    CHECK(g_log.empty());
}

void TestSendMessages()
{
    C_NetGame &net = Fresh();
    g_gameProfile.m_mpSettings = &g_testMp;
    // A dedicated server: the console's commands executed.
    g_testMp.dedicated = true;
    g_consoleCommands = {"kick x", "map y"};
    g_gameRole = GAME_ROLE_SERVER;
    net.SendMessages(10);
    CHECK(Logged("execute kick x") && Logged("execute map y") && g_consoleCommands.empty());
    g_testMp.dedicated = false;
    // A client: the bandwidth every 100 ms (scaled every sixth).
    g_gameRole = GAME_ROLE_CLIENT;
    for (int i = 0; i < 7; ++i)
        g_bandwidth[i] = i + 1;
    net.SendMessages(99);
    CHECK(net.m_bandwidthLast[0] == 0);
    net.SendMessages(1);
    CHECK(net.m_bandwidthLast[0] == 1 && net.m_bandwidthTotal[3] == 4 && net.m_receivedHistory[net.m_historyIndex] == 6 &&
          net.m_sentHistory[net.m_historyIndex2] == 8);
    for (int i = 0; i < 5; ++i)
        net.SendMessages(100);
    // (The sixth: five sums scaled: the count by a fifth, the others doubled; then this one's added.)
    CHECK(net.m_bandwidthAverage[0] == 1 && net.m_bandwidthAverage[1] == 20 && net.m_bandwidthAverage[6] == 70 &&
          net.m_bandwidthTotal[0] == 1 && net.m_bandwidthTotal[6] == 7);
    // A failed figure (-2 or below 0): nothing.
    g_bandwidth[0] = -2;
    const int index = net.m_historyIndex;
    net.SendMessages(100);
    CHECK(net.m_historyIndex == index);
    g_bandwidth[0] = 1;
    g_gameRole = GAME_ROLE_LOCAL;
    net.SendMessages(100);
    CHECK(net.m_historyIndex == index);
}

void TestCommands()
{
    C_NetGame &net = Fresh();
    T_Server server;
    net.m_server = &server;
    net.m_players.push_back(Player(10, "host"));
    net.m_players.push_back(Player(20, "pat"));
    net.m_players.push_back(Player(30, "kim"));
    // Kick by name (slot 0 not looked at); by slot.
    CHECK(net.Command(NET_COMMAND_KICK_NAME, "kim", 0) && Logged("destroy client 30"));
    g_log.clear();
    CHECK(!net.Command(NET_COMMAND_KICK_NAME, "host", 0) && g_log.empty());
    CHECK(!net.Command(NET_COMMAND_KICK_NAME, "", 0) && !net.Command(NET_COMMAND_KICK_NAME, nullptr, 0));
    CHECK(net.Command(NET_COMMAND_KICK_SLOT, reinterpret_cast<const char *>(1), 0) && Logged("destroy client 20"));
    server.destroyResult = 1;
    CHECK(!net.Command(NET_COMMAND_KICK_SLOT, reinterpret_cast<const char *>(1), 0));
    server.destroyResult = 0;
    g_log.clear();
    CHECK(!net.Command(NET_COMMAND_KICK_SLOT, nullptr, 0) && !net.Command(NET_COMMAND_KICK_SLOT,
                                                                          reinterpret_cast<const char *>(3), 0) &&
          g_log.empty() && !net.Command(9, nullptr, 0));
    net.m_server = nullptr;
    // The bits of a range.
    uint32_t steps = 0;
    CHECK(BitsForRange(&steps, 1000.7f) == 10 && steps == 1000 && BitsForRange(&steps, 2.0f) == 1 &&
          BitsForRange(&steps, 3.0f) == 2 && BitsForRange(&steps, 4.0f) == 2 && BitsForRange(&steps, 5.0f) == 3 &&
          BitsForRange(&steps, 0.0f) == 1 && steps == 0);
}

void TestVotes()
{
    C_NetGame &net = Fresh();
    S_NetVote &vote = net.m_vote;
    vote = S_NetVote();
    // (The count of players is g_netGame's: four connected, one not.)
    g_netGame.m_players.clear();
    for (int i = 0; i < 5; ++i)
        g_netGame.m_players.push_back(Player(i == 2 ? 0 : 10 + i, "p"));
    // Asked by a player: told to everyone; not while one runs.
    vote.Start(2, 7);
    CHECK(vote.kind == 2 && vote.arg == 7 && vote.time == 0 && !vote.voted && vote.yes == 0 &&
          Logged("game 22 to 0 a 2 b 7 0"));
    g_log.clear();
    vote.Start(1, 1);
    CHECK(vote.kind == 2 && g_log.empty());
    // The yes: once.
    CHECK(vote.Vote() && vote.voted && Logged("post 11 0 0 0"));
    CHECK(!vote.Vote());
    // Counted by the server: over half the players (4: three) carries it.
    vote.CountYes();
    vote.CountYes();
    CHECK(vote.kind == 2 && vote.yes == 2);
    vote.CountYes();
    CHECK(Logged("execute map 7") && vote.kind == 0x42 && Logged("game 23 to 0 a 1 b 0 0"));
    g_log.clear();
    vote.CountYes();
    CHECK(g_log.empty());
    // Begun from the server's message: printed.
    vote.Begin(0, 12);
    CHECK(vote.kind == 0 && vote.arg == 12 && Logged("print 50 150 50 T22E2 12 (maxclients)"));
    // Timed out after 15 s (the server).
    g_gameRole = GAME_ROLE_SERVER;
    g_log.clear();
    net.UpdateVote(15000);
    CHECK(vote.time == 15000 && vote.kind == 0);
    net.UpdateVote(1);
    CHECK(vote.kind == 0x42 && Logged("game 23 to 0 a 0 b 0 0"));
    g_gameRole = GAME_ROLE_CLIENT;
    vote.kind = 1;
    vote.time = 0;
    net.UpdateVote(20000);
    CHECK(vote.kind == 1 && vote.time == 20000);
    vote.kind = 0x42;
    vote.time = 0;
    net.UpdateVote(20000);
    CHECK(vote.time == 0);
    PrintVoteResult(true);
    PrintVoteResult(false);
    CHECK(Logged("print 50 150 50 T22E3") && Logged("print 50 150 50 T22E4"));
    PostVote(3, 4);
    CHECK(Logged("post 10 3 4 0"));
    g_netGame.m_players.clear();
}

void TestClose()
{
    C_NetGame &net = Fresh();
    g_gameProfile.m_mpSettings = &g_testMp;
    // Not now: at the next frame.
    net.Close(false);
    CHECK(net.m_closePending && g_log.empty());
    T_Server server;
    T_Client client;
    net.m_server = &server;
    net.m_client = &client;
    g_graphFlags = 0x1234;
    net.m_unknown448 = 5;
    net.m_joinStatus = 3;
    net.m_players.push_back(Player(1, "x"));
    g_options.voiceChat = true;
    g_testMp.unknown174 = 1;
    g_gameRole = GAME_ROLE_SERVER;
    g_localPlayerSlot = 4;
    const uint8_t data[2] = {1, 2};
    net.QueueReceived(data, 2, 0);
    net.QueueOutgoing(data, 2, 0);
    C_NetReplication replication;
    replication.m_makeFactory = [](int) { return NetMessageFactory(nullptr); };
    replication.m_makeStateReader = [](int) { return NetStateHandler(nullptr); };
    replication.FindObject(1, true);
    g_netReplication = &replication;
    net.Close(true);
    CHECK(!net.m_closePending && Logged("firewall 0 0") && Logged("graph flags 4660") && Logged("stop voice client") &&
          Logged("stop voice server") && Logged("server cancel 0 32768") && Logged("server close 0") &&
          Logged("server release") && Logged("client cancel 0 32768") && Logged("client close 0") &&
          Logged("client release") && Logged("stop gamespy"));
    CHECK(net.m_server == nullptr && net.m_client == nullptr && net.m_joinStatus == 0 && g_gameRole == GAME_ROLE_LOCAL &&
          net.m_players.empty() && net.m_received.empty() && net.m_outgoing.empty() && net.m_unknown448 == 0 &&
          replication.m_objects.empty() && net.m_string502C == "session" && g_localPlayerSlot == 0 &&
          net.m_serverLink == nullptr);
    g_netReplication = nullptr;
    g_options.voiceChat = false;
    g_testMp.unknown174 = 0;
    g_log.clear();
    net.Close(true);
    CHECK(!Logged("stop voice client") && !Logged("stop gamespy") && !Logged("server close 0"));
}

void TestPlayers()
{
    C_NetGame &net = Fresh();
    net.m_players.push_back(Player(10, "host"));
    net.m_players.push_back(Player(20, "pat"));
    net.m_players.push_back(Player(0, ""));
    // Looked up.
    CHECK(net.FindPlayerByName("pat") == 1 && net.FindPlayerByName("nobody") == -1 && net.FindPlayerByName("") == 2);
    CHECK(net.FindPlayer(20) == 1 && net.FindPlayer(0) == -1 && net.FindPlayer(30) == -1);
    CHECK(std::strcmp(net.GetPlayerName(1), "pat") == 0 && net.GetPlayerName(3) == nullptr && net.GetPlayerName(-1) == nullptr);
    C_Human *human = MakeHuman(false);
    net.m_players[1].human = human;
    CHECK(net.GetPlayerHuman(1) == human && net.GetPlayerHuman(3) == nullptr && net.GetPlayerHuman(-1) == nullptr);
    CHECK(net.LockPlayers() == net.m_players.data());
    net.UnlockPlayers();
    CHECK(net.GetPlayerCount() == 3);
    // Scores.
    net.m_players[0].human = human;
    net.m_players[0].stats[3] = 5;
    net.m_players[1].stats[3] = 9;
    net.m_players[2].stats[3] = 50;
    net.m_players[2].human = human;
    CHECK(net.GetBestScore() == 9);
    g_gameRole = GAME_ROLE_SERVER;
    net.AddScore(1, 3);
    CHECK(net.m_players[1].stats[3] == 12 && net.m_players[1].stats[8] == 3 && Logged("rank 1") &&
          net.m_players[1].stats[5] == 1);
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    net.AddScore(0, -2);
    CHECK(net.m_players[0].stats[3] == 3 && g_log.empty());
    net.AddScore(7, 1);
    // Reset: the stats (and +0x7C) and the teams'.
    net.m_players[1].teamKills = 1;
    net.m_teamScores[1] = 3.0f;
    net.m_teamStats[2][8] = 4;
    net.m_players[1].team = 1;
    net.ResetScores();
    CHECK(net.m_players[1].stats[3] == 0 && net.m_players[1].stats[8] == 0 && net.m_players[1].teamKills == 0 &&
          net.m_teamScores[1] == 0.0f && net.m_teamStats[2][8] == 0 && net.m_players[1].human == human &&
          net.m_players[1].team == 1);
    // The player stats: the humans too, and the replication's objects.
    net.m_players[1].stats[2] = 4;
    C_NetReplication replication;
    replication.m_makeFactory = [](int) { return NetMessageFactory(nullptr); };
    replication.m_makeStateReader = [](int) { return NetStateHandler(nullptr); };
    replication.FindObject(1, true);
    g_netReplication = &replication;
    net.m_teamScores[0] = 2.0f;
    net.ResetPlayerStats();
    CHECK(net.m_players[1].human == nullptr && net.m_players[1].stats[2] == 0 && replication.m_objects.empty() &&
          net.m_teamScores[0] == 0.0f);
    g_netReplication = nullptr;
    // A client's table grown to a slot.
    g_gameRole = GAME_ROLE_CLIENT;
    net.SetPlayer(5, 55, "sam");
    CHECK(net.m_players.size() == 6 && net.m_players[5].connection == 55 && std::strcmp(net.m_players[5].name, "sam") == 0 &&
          net.m_players[4].name[0] == 0 && net.m_players[4].connection == 0 && net.m_players[4].team == 2);
    net.SetPlayer(0, 1, "h");
    CHECK(net.m_players.size() == 6 && net.m_players[0].connection == 1);
    g_gameRole = GAME_ROLE_SERVER;
    net.SetPlayer(8, 1, "z");
    CHECK(net.m_players.size() == 6);
    // Chat and voice.
    g_localPlayerSlot = 3;
    std::string longText(150, 'x');
    CHECK(net.SendChat(longText.c_str(), true) && Logged(("post 3 '" + std::string(99, 'x') + "' slot 3 team 1").c_str()));
    net.SetVoiceValue(5);
    CHECK(!Logged("voice config 32 40 1 5 -2 0 0 0"));
    C_NetVoice voice(nullptr, nullptr);
    net.m_voiceClient = &voice;
    net.SetVoiceValue(5);
    CHECK(Logged("voice config 32 40 1 5 -2 0 0 0"));
    net.m_voiceClient = nullptr;
}

void TestFrameBounds()
{
    // A frame's bound widens the scan's.
    alignas(16) static unsigned char storage[sizeof(I3D_frame)];
    auto *frame = reinterpret_cast<I3D_frame *>(storage);
    frame->m_frameFlags = FRMFLAGS_BOUND_VALID;
    frame->m_bound.bbox.min = S_vector(-1, -2, -3);
    frame->m_bound.bbox.max = S_vector(4, 5, 6);
    CHECK(ScanFrameBounds(frame, nullptr) == 0);
}

// ---- (added after the first mutation run) ----------------------------------------------------------------------------

void TestUpdateLags()
{
    C_NetGame &net = Fresh();
    g_gameRole = GAME_ROLE_LOCAL;
    net.UpdateLags(0);                                // (its timer made)
    g_perfOffset += 4000;
    C_NetSocket socket;
    T_Server server;
    net.m_server = &server;
    C_NetReplication replication;
    replication.m_makeFactory = [](int) { return NetMessageFactory(nullptr); };
    replication.m_makeStateReader = [](int) { return NetStateHandler(nullptr); };
    g_netReplication = &replication;
    const int now = static_cast<int>(GetPerfTimeMs());
    const DWORD ticks = timeGetTime();
    const char *names[8] = {"srv", "pat", "kim", "lee", "max", "", "a", "bo"};
    C_NetConnection *links[8] = {};
    C_NetConnection *transport[8] = {};
    for (int i = 0; i < 8; ++i) {
        net.m_players.push_back(Player(10 * (i + 1), names[i]));
        if (i != 6) {
            links[i] = new C_NetConnection(100 + i, 0, &socket);
            links[i]->m_lastReceive = now;
            net.m_players[static_cast<size_t>(i)].link = links[i];
        }
        if (i != 0)
            net.m_players[static_cast<size_t>(i)].human = MakeHuman(i == 2);
    }
    links[0]->m_lastReceive = now - 6000;            // timed out
    links[5]->m_lastReceive = now - 6000;
    links[7]->m_lastReceive = now - 4000;            // not yet
    const int roundTrips[8] = {0, 101, 80, 0, 300, 50, 100, 0};
    for (int i : {1, 2, 4, 5, 6}) {
        transport[i] = new C_NetConnection(i, 0, &socket);
        transport[i]->m_roundTrip = static_cast<uint32_t>(roundTrips[i]);
        net.m_transport->m_connections.push_back(transport[i]);
    }
    C_NetObject *objects[8] = {};
    for (int i : {1, 2, 5, 6})
        objects[i] = LagObject(replication, i);
    g_netMaxPing = 80;
    g_netMaxInactivity = 1000;
    net.m_players[1].highPings = 2;
    net.m_players[1].activityTime = ticks - 5000;
    net.m_players[2].activityTime = ticks - 5000;
    net.m_players[3].highPings = 5;
    net.m_players[4].highPings = 5;
    net.m_players[6].highPings = 1;
    net.m_players[6].activityTime = ticks;
    g_gameRole = GAME_ROLE_SERVER;
    net.UpdateLags(0);
    // Timed-out links dropped (named or not); the resend times from the round trips; the lags half the round trip
    // and 5 ms.
    CHECK(Logged("destroy client 10") && Logged("destroy client 60") && !Logged("destroy client 80") &&
          server.lastData == nullptr && server.lastSize == 0 && server.lastFlags == 0);
    CHECK(ResendTime(links[1]) == 301 && ResendTime(links[4]) == 500 && ResendTime(links[3]) == 200 &&
          ResendTime(links[2]) == 280);
    CHECK(Near(Lag(objects[1]), 0.055f) && Near(Lag(objects[2]), 0.045f) && Near(Lag(objects[6]), 0.055f) &&
          Near(Lag(objects[5]), 0.035f));
    // High pings counted (over the most) and kicked after three; the inactive kicked (not spectators).
    CHECK(net.m_players[1].highPings == 3 && net.m_players[2].highPings == 0 && net.m_players[6].highPings == 2);
    CHECK(Logged("game 17 to 0 a 2 b 1 0") && !Logged("game 17 to 0 a 2 b 3 0") && !Logged("game 17 to 0 a 2 b 4 0") && !Logged("game 17 to 0 a 2 b 6 0") && !Logged("game 17 to 0 a 2 b 2 0"));
    CHECK(Logged("game 17 to 0 a 3 b 1 0") && !Logged("game 17 to 0 a 3 b 2 0") && !Logged("game 17 to 0 a 3 b 6 0") && net.m_players[1].activityTime >= ticks &&
          net.m_players[2].activityTime == ticks - 5000);
    // No most ping, no most inactivity: not counted, not kicked (through the frame).
    g_netMaxPing = 0;
    g_netMaxInactivity = 0;
    net.m_players[1].activityTime = ticks - 5000;
    net.m_players[6].highPings = 0;
    g_log.clear();
    g_perfOffset += 4000;
    net.m_sendTime = 0;
    net.Process(50, false);
    CHECK(net.m_players[6].highPings == 0 && !Logged("game 17 to 0 a 3 b 1 0") && Logged("game 17 to 0 a 2 b 1 0"));
    // Not before the 3 s; not local, the dedicated (0x12), nor without the server's or the client's object.
    net.m_players[6].highPings = 0;
    g_netMaxPing = 1;
    g_log.clear();
    net.UpdateLags(0);
    for (uint32_t role : {static_cast<uint32_t>(GAME_ROLE_LOCAL), 0x12u, static_cast<uint32_t>(GAME_ROLE_CLIENT)}) {
        g_gameRole = role;
        g_perfOffset += 4000;
        net.UpdateLags(0);
    }
    g_gameRole = GAME_ROLE_SERVER;
    net.m_server = nullptr;
    g_perfOffset += 4000;
    net.UpdateLags(0);
    CHECK(net.m_players[6].highPings == 0 && g_log.empty());
    // A client: its server link's resend time; the lags from it plus the players' own (+0x14 stats[4]); its own
    // object the replication's local one.
    for (C_NetConnection *c : net.m_transport->m_connections)
        delete c;
    net.m_transport->m_connections.clear();
    net.m_players.clear();
    T_Client client;
    net.m_client = &client;
    C_NetReplication clientReplication;
    clientReplication.m_makeFactory = [](int) { return NetMessageFactory(nullptr); };
    clientReplication.m_makeStateReader = [](int) { return NetStateHandler(nullptr); };
    g_netReplication = &clientReplication;
    C_NetConnection serverLink(0, 0, &socket);
    serverLink.m_roundTrip = 201;
    net.m_serverLink = &serverLink;
    const char *clientNames[6] = {"srv", "me", "x", "", "y", "z"};
    for (int i = 0; i < 6; ++i) {
        net.m_players.push_back(Player(i + 1, clientNames[i]));
        if (i != 4)
            net.m_players[static_cast<size_t>(i)].human = MakeHuman(false);
    }
    net.m_players[1].stats[4] = 7;
    net.m_players[2].stats[4] = 3;
    C_NetObject *clientObjects[5] = {};
    for (int i : {0, 1, 2, 3, 4})
        clientObjects[i] = LagObject(clientReplication, i);
    clientReplication.GetLocalObject().AddProperty(0);
    g_localPlayerSlot = 1;
    g_gameRole = GAME_ROLE_CLIENT;
    g_perfOffset += 4000;
    net.UpdateLags(0);
    CHECK(ResendTime(&serverLink) == 401 && Near(Lag(clientObjects[0]), 0.105f) &&
          Near(Lag(&clientReplication.GetLocalObject()), 0.112f) && Near(Lag(clientObjects[1]), 0.035f) &&
          Near(Lag(clientObjects[2]), 0.108f) && Near(Lag(clientObjects[3]), 0.035f) && Near(Lag(clientObjects[4]), 0.035f));
    net.m_client = nullptr;
    net.m_serverLink = nullptr;
    g_netReplication = nullptr;
    net.m_players.clear();
    for (C_NetConnection *link : links)
        delete link;
    g_localPlayerSlot = 0;
}

// The level's scan (Command 3): the primary sector's frames' bounds, widened.
struct T_Sector {
    void **vtable;
    std::vector<I3D_frame *> frames;
};
void __stdcall FakeEnumFrames(T_Sector *self, I3D_ENUMPROC *callback, void *context, uint32_t typeMask, const char *)
{
    Log("enum %u", typeMask);
    for (I3D_frame *frame : self->frames)
        callback(frame, context);
}
I3D_frame *BoundFrame(S_vector low, S_vector high)
{
    alignas(16) static unsigned char storage[8][sizeof(I3D_frame)];
    static int used;
    auto *frame = reinterpret_cast<I3D_frame *>(storage[used++ % 8]);
    frame->m_frameFlags = FRMFLAGS_BOUND_VALID;
    frame->m_bound.bbox.min = low;
    frame->m_bound.bbox.max = high;
    return frame;
}

void TestLevelScan()
{
    C_NetGame &net = Fresh();
    static void *vtable[32];
    for (void *&slot : vtable)
        slot = reinterpret_cast<void *>(&FakeEnumFrames);
    T_Sector sector{vtable, {}};
    alignas(16) static unsigned char scene[0x400];
    *reinterpret_cast<T_Sector **>(scene + 0x1F8) = &sector;
    auto *game = reinterpret_cast<C_game *>(g_gameStorage);
    g_game = game;
    game->m_scene = reinterpret_cast<I3D_scene *>(scene);
    sector.frames = {BoundFrame(S_vector(-1, 5, -3), S_vector(4, 6, 2)), BoundFrame(S_vector(2, -2, -7), S_vector(9, 1, 3))};
    for (int i = 0; i < 3; ++i) {
        net.m_levelSteps[i] = 9;
        net.m_levelBits[i] = 9;
    }
    net.m_unknown505C = 7;
    for (int i = 0; i < 7; ++i)
        net.m_bandwidthTotal[i] = net.m_bandwidthAverage[i] = net.m_bandwidthLast[i] = 5;
    g_localPlayerSlot = 5;
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(!net.Command(NET_COMMAND_SCAN_LEVEL, nullptr, 0));
    CHECK(Logged("enum 1") && g_netReplication == nullptr);
    CHECK(Near(net.m_levelMin[0], -1.05f) && Near(net.m_levelMin[2], -7.05f) && Near(net.m_levelSize[1], 8.1f) &&
          Near(net.m_levelSize[2], 10.1f));
    bool zeroed = true;
    for (int i = 0; i < 7; ++i)
        zeroed = zeroed && net.m_bandwidthTotal[i] == 0 && net.m_bandwidthAverage[i] == 0 && net.m_bandwidthLast[i] == 0;
    CHECK(zeroed && net.m_unknown505C == 0 && g_localPlayerSlot == 5);
    CHECK(net.m_levelBits[0] == 1 && net.m_levelBits[1] == 1 && net.m_levelBits[2] == 1 && net.m_levelSteps[0] == 0 &&
          net.m_levelSteps[1] == 0 && net.m_levelSteps[2] == 0);
    // Again (the scan restarted): a smaller level; a local game: slot 0.
    sector.frames = {BoundFrame(S_vector(0, 0, 0), S_vector(1, 5, 8)), BoundFrame(S_vector(0, 0, 0), S_vector(2, 1, 1)),
                     BoundFrame(S_vector(0, 0, 0), S_vector(0, 6, 0))};
    g_gameRole = GAME_ROLE_LOCAL;
    g_log.clear();
    net.Command(NET_COMMAND_SCAN_LEVEL, nullptr, 0);
    CHECK(Near(net.m_levelMin[1], -0.05f) && Near(net.m_levelSize[0], 2.1f) && Near(net.m_levelSize[1], 6.1f) &&
          Near(net.m_levelSize[2], 8.1f) && g_localPlayerSlot == 0);
    game->m_scene = nullptr;
}

void TestResets()
{
    // Init after use: every field back to its default.
    g_log.clear();
    auto *net = new C_NetGame;
    net->m_unknown4EE0 = reinterpret_cast<C_NetUnknownObject *>(1);
    net->m_enumHandle = 5;
    net->m_sendTime = 9;
    net->m_unknown448 = 3;
    net->m_serverBrowser = reinterpret_cast<S_ServerBrowser *>(3);
    net->m_joinStatus = 4;
    net->m_reporting = true;
    net->m_keySocket = 5;
    g_netReplication = reinterpret_cast<C_NetReplication *>(1);
    g_netMaxPing = 9;
    g_netMaxFreq = 1;
    net->m_voiceServer = reinterpret_cast<C_NetVoice *>(1);
    net->m_voiceClient = reinterpret_cast<C_NetVoice *>(1);
    net->m_unknown8 = true;
    net->Init();
    CHECK(net->m_unknown4EE0 == nullptr && net->m_enumHandle == 0 && net->m_sendTime == 0 && net->m_unknown448 == 0 &&
          net->m_serverBrowser == nullptr && net->m_joinStatus == 0 && !net->m_reporting &&
          net->m_keySocket == static_cast<uintptr_t>(-1) && g_netReplication == nullptr && g_netMaxPing == 0 &&
          g_netMaxFreq == 50 && net->m_voiceServer == nullptr && net->m_voiceClient == nullptr && !net->m_unknown8);
    // Shut down: the voice server deleted, the +0x4EE0 object destroyed.
    struct T_Unknown : C_NetUnknownObject {
        void Unknown00() override {}
        void Unknown04() override {}
        void Unknown08() override {}
        void Destroy(bool free) override { Log("destroy unknown %d", free); }
    } unknown;
    net->m_unknown4EE0 = &unknown;
    net->m_voiceServer = new C_NetVoice(nullptr, nullptr);
    g_gameProfile.m_mpSettings = &g_testMp;
    g_log.clear();
    net->Shutdown();
    CHECK(Logged("firewall 0 0") && Logged("voice deleted") && net->m_voiceServer == nullptr &&
          Logged("destroy unknown 1") && net->m_unknown4EE0 == nullptr);
    // Closed: the LAN search's socket closed, the query thread's handle closed, the connections deleted, the
    // transport stopped, the streams sent, the queues' buffers given back.
    net->Init();
    WSADATA wsa;
    WSAStartup(0x202, &wsa);
    const SOCKET search = socket(AF_INET, SOCK_DGRAM, 0);
    net->m_keySocket = search;
    HANDLE thread = CreateEventA(nullptr, TRUE, TRUE, nullptr);
    g_netQueryThread = thread;
    C_NetConnection *staleLink = reinterpret_cast<C_NetConnection *>(8);
    net->m_serverLink = staleLink;
    net->m_reporting = true;
    C_NetSocket socket;
    net->m_transport->m_connections.push_back(new C_NetConnection(9, 0, &socket));
    net->m_transport->m_running = true;
    net->m_transport->m_stopped = CreateEventA(nullptr, TRUE, TRUE, nullptr);
    net->CreateMessage(4, 1)->WriteBits(1, 8);
    net->BeginMessage(5, 4, true, 0)->WriteBits(1, 8);
    const size_t buffers = g_netPool.m_buffers.items.size();
    const uint8_t data[2] = {1, 2};
    net->QueueReceived(data, 2, 0);
    net->QueueOutgoing(data, 2, 0);
    g_gameRole = GAME_ROLE_SERVER;
    net->Close(true);
    int type = 0;
    int length = sizeof type;
    CHECK(net->m_keySocket == static_cast<uintptr_t>(-1) &&
          getsockopt(search, SOL_SOCKET, SO_TYPE, reinterpret_cast<char *>(&type), &length) != 0);
    DWORD flags;
    CHECK(g_netQueryThread == nullptr && !GetHandleInformation(thread, &flags));
    CHECK(net->m_serverLink == nullptr && !net->m_reporting && net->m_transport->m_connections.empty() && !net->m_transport->m_running &&
          net->m_messages[1].BitPosition() == 0u && g_netPool.m_buffers.items.size() == buffers);
    // A received message's buffer given back too.
    C_GrowingBitStream w;
    w.WriteBits(NET_MESSAGE_END, 5);
    net->QueueOutgoing(w.Data(), w.ByteSize(), 0);
    net->ReceiveMessages(0, false);
    CHECK(g_netPool.m_buffers.items.size() == buffers);
    // The vote's state reset when one starts or begins.
    S_NetVote vote;
    vote.time = 5;
    vote.voted = true;
    vote.yes = 2;
    vote.Start(1, 1);
    CHECK(vote.time == 0 && !vote.voted && vote.yes == 0);
    vote.time = 5;
    vote.voted = true;
    vote.Begin(1, 1);
    CHECK(vote.time == 0 && !vote.voted);
}

void TestBoundaries()
{
    C_NetGame &net = Fresh();
    g_gameRole = GAME_ROLE_CLIENT;
    // The send time kept at twice the period; the join timed out only past 30 s.
    net.m_sendPeriod = 50;
    net.m_sendTime = 0;
    net.Process(150, false);
    CHECK(net.m_sendTime == 100);
    net.m_joinStatus = NET_JOIN_JOINING;
    net.m_joinTime = 29950;
    net.m_sendTime = 0;
    net.Process(50, false);
    CHECK(net.m_joinTime == 30000 && net.m_joinStatus == NET_JOIN_JOINING);
    net.m_joinTime = 29951;
    net.m_sendTime = 0;
    net.Process(50, false);
    CHECK(net.m_joinStatus == NET_JOIN_TIMED_OUT);
    net.m_joinStatus = 0;
    // A client with only the server's slot: sent to it; channel 3: the last.
    C_NetSocket socket;
    C_NetConnection link(1, 0, &socket);
    net.m_serverLink = &link;
    net.m_players.push_back(Player(1, "srv"));
    net.CreateMessage(4, 3)->WriteBits(1, 8);
    CHECK(net.m_messages[2].BitPosition() == 13u);
    net.Process(50, false);
    CHECK(link.m_channels[2]->m_outgoing.size() == 1);
    // The direct stream reset by the frame too.
    net.BeginMessage(9, 4, true, 0)->WriteBits(1, 8);
    net.Process(50, false);
    CHECK(net.m_direct.BitPosition() == 0u);
    // The server's own queues: flags 0; its unreliable stream ended.
    g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    net.m_players.clear();
    net.CreateMessage(4, 0)->WriteBits(1, 8);
    net.CreateMessageEx(6, 0)->WriteBits(2, 8);
    net.Process(50, false);
    CHECK(net.m_outgoing.size() == 1 && net.m_outgoing.front().flags == 0 && net.m_received.size() == 1 &&
          net.m_received.front().flags == 0);
    {
        C_NetMessage back;
        back.Attach(net.m_received.front().data, static_cast<uint32_t>(net.m_received.front().size) * 8);
        CHECK(back.ReadBits(5) == 6 && back.ReadBits(8) == 2 && back.ReadBits(5) == NET_MESSAGE_END);
    }
    net.m_received.clear();
    net.m_outgoing.clear();
    net.m_serverLink = nullptr;
    while (C_NetPacket *p = link.m_channels[2]->PopOutgoing())
        p->Release();
    // No mission: the rest not read (a game command after it not run).
    auto *game = reinterpret_cast<C_game *>(g_gameStorage);
    g_game = game;
    game->m_missionLoaded = false;
    C_GrowingBitStream w;
    w.WriteBits(3, 5);
    w.WriteBits(NET_MESSAGE_GAME, 5);
    w.WriteBits(1234, 32);
    w.WriteBits(0x55, 8);
    net.QueueOutgoing(w.Data(), w.ByteSize(), 0);
    g_log.clear();
    net.ReceiveMessages(0, false);
    CHECK(g_log.empty());
    // Join data: the stage 2 while it is sent; a port of 0 is known; a player past stage 0 skipped.
    net.m_players.clear();
    C_NetConnection a(1, 0, &socket);
    a.m_port = 0;
    net.m_players.push_back(Player(10, "pat"));
    net.m_players.push_back(Player(30, "kim"));
    net.m_players[0].link = &a;
    net.m_players[1].link = &a;
    net.m_players[1].joinStage = 1;
    g_watchedPlayer = &net.m_players[0];
    g_log.clear();
    net.SendJoinData();
    g_watchedPlayer = nullptr;
    CHECK(Logged("stage 2") && !Logged("stage 0") && net.m_players[0].joinStage == 1 && !Logged("game 0 to 30 a 0 b 0 0"));
    // Through the frame of a server.
    net.m_players[0].joinStage = 0;
    g_gameRole = GAME_ROLE_SERVER;
    g_gameProfile.m_mpSettings = &g_testMp;
    g_testMp.dedicated = false;
    net.m_sendTime = 0;
    net.SendMessages(20);
    CHECK(net.m_players[0].joinStage == 1 && net.m_sendTime == 20);
    net.m_players.clear();
    // The bandwidth: once per 100 ms; 0 figures taken, below 0 not; the history indexes wrap at 50.
    g_gameRole = GAME_ROLE_CLIENT;
    for (int i = 0; i < 7; ++i)
        g_bandwidth[i] = 1;
    for (int i = 0, start = net.m_historyIndex; i < 200 && net.m_historyIndex == start; ++i)
        net.SendMessages(1);                         // (to the moment it went: no time left over)
    const int index = net.m_historyIndex;
    net.SendMessages(99);
    net.SendMessages(1);
    CHECK(net.m_historyIndex == (index + 1) % 50);
    net.SendMessages(1);
    CHECK(net.m_historyIndex == (index + 1) % 50);
    g_bandwidth[0] = -1;
    net.SendMessages(100);
    CHECK(net.m_historyIndex == (index + 1) % 50);
    g_bandwidth[0] = 0;
    net.SendMessages(100);
    CHECK(net.m_historyIndex == (index + 2) % 50);
    for (int i = 0; i < 60; ++i)
        net.SendMessages(100);
    CHECK(net.m_historyIndex == (index + 62) % 50 && net.m_historyIndex2 == net.m_historyIndex &&
          net.m_receivedHistory[net.m_historyIndex] == 2 && net.m_sentHistory[net.m_historyIndex2] == 2);
    // The averages every sixth, again and again: 12 more (two averages).
    for (int i = 0; i < 7; ++i)
        g_bandwidth[i] = 3;
    for (int i = 0; i < 12; ++i)
        net.SendMessages(100);
    CHECK(net.m_bandwidthAverage[1] == 36 && net.m_bandwidthAverage[0] == 3);
    // Kicks by name from slot 1; the client destroyed with no data.
    T_Server server;
    net.m_server = &server;
    net.m_players.push_back(Player(10, "host"));
    net.m_players.push_back(Player(20, "pat"));
    g_log.clear();
    CHECK(net.Command(NET_COMMAND_KICK_NAME, "pat", 0) && Logged("destroy client 20") && server.lastData == nullptr &&
          server.lastSize == 0 && server.lastFlags == 0);
    net.m_server = nullptr;
    // The players: slot 0 looked up; the best score not below 0; stats[3] the score.
    net.m_players[0].human = MakeHuman(false);
    net.m_players[1].human = MakeHuman(false);
    net.m_players[0].stats[3] = -4;
    net.m_players[1].stats[3] = -2;
    net.m_players[1].stats[2] = 7;
    net.m_players[1].stats[4] = 8;
    CHECK(net.FindPlayerByName("host") == 0 && net.FindPlayer(10) == 0);
    CHECK(net.GetBestScore() == 0 && std::strcmp(net.GetPlayerName(0), "host") == 0 &&
          net.GetPlayerHuman(0) == net.m_players[0].human);
    net.m_players.push_back(Player(30, "q"));
    net.m_players[2].human = net.m_players[0].human;
    net.m_players[2].stats[3] = 4;
    CHECK(net.GetBestScore() == 4);
    net.m_players.pop_back();
    // Every team reset (both resets).
    for (int team = 0; team < 3; ++team) {
        net.m_teamScores[team] = 1.0f;
        net.m_teamStats[team][0] = 1;
    }
    net.ResetScores();
    CHECK(net.m_teamScores[2] == 0.0f && net.m_teamStats[0][0] == 0 && net.m_teamStats[2][0] == 0);
    for (int team = 0; team < 3; ++team) {
        net.m_teamScores[team] = 1.0f;
        net.m_teamStats[team][0] = 1;
    }
    net.ResetPlayerStats();
    CHECK(net.m_teamScores[2] == 0.0f && net.m_teamStats[0][0] == 0 && net.m_teamStats[2][0] == 0);
    // A client's table grown to the slot just past it.
    g_gameRole = GAME_ROLE_CLIENT;
    net.SetPlayer(2, 5, "sam");
    CHECK(net.m_players.size() == 3 && net.m_players[2].connection == 5);
    net.m_players.clear();
}

void RunNetGameTests()
{
    g_netGame.m_playersLock.Init();
    g_netGame.m_queueLock.Init();
    std::printf("> NetGameConstruction\n");
    TestNetGameConstruction();
    std::printf("> Streams\n");
    TestStreams();
    std::printf("> Queues\n");
    TestQueues();
    std::printf("> SendToPlayers\n");
    TestSendToPlayers();
    std::printf("> Process\n");
    TestProcess();
    std::printf("> JoinData\n");
    TestJoinData();
    std::printf("> SendMessages\n");
    TestSendMessages();
    std::printf("> Commands\n");
    TestCommands();
    std::printf("> Votes\n");
    TestVotes();
    std::printf("> Close\n");
    TestClose();
    std::printf("> Players\n");
    TestPlayers();
    std::printf("> FrameBounds\n");
    TestFrameBounds();
    std::printf("> UpdateLags\n");
    TestUpdateLags();
    std::printf("> LevelScan\n");
    TestLevelScan();
    std::printf("> Resets\n");
    TestResets();
    std::printf("> Boundaries\n");
    TestBoundaries();
}
