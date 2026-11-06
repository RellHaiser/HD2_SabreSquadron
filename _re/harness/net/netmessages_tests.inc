// (Part 10d-v.) The network game's messages (net/net_messages.cpp): the server's DirectPlay messages, the game
// messages C_GameApp queues and sends, a soldier's state in a message. The messages' build only (build_messages.bat:
// -DNET_SESSION_TEST -DNET_MESSAGES_TEST; the fake SendGameMessage / PostCommand left out); included by
// netgame_test.cpp after the session's tests (their fakes: T_SessionServer, T_Address, g_cjk, ...).

// ---- the fakes ------------------------------------------------------------------------------------------------------

// The game's other writers: a marker byte each.
void S_MultiplayerSettings::WriteCurrentMap(C_BitStream *stream) { stream->WriteBits(0xC1, 8); }
void S_MultiplayerSettings::WriteRules(C_BitStream *stream) { stream->WriteBits(0xC2, 8); }
namespace ModuleManager {
void BroadcastWriteState(C_BitStream *stream) { stream->WriteBits(0xC3, 8); }
}  // namespace ModuleManager
void C_game::WriteObjectiveStates(C_BitStream *stream) const { stream->WriteBits(0xC4, 8); }
void C_TemporaryActorsList::WriteState(C_BitStream *stream) { stream->WriteBits(0xC5, 8); }
void C_game::OnPlayerLeft(int connection) { Log("left %d", connection); }
// A frame's world matrix: its local one.
void I3D_frame::UpdateWMatrixProc()
{
    Log("update matrix");
    m_worldMatrix = m_localMatrix;
    m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
}
// A human's soldier state: the defaults it was handed logged, then its own (by its net id).
void C_Human::GetSoldierState(S_SoldierState &state) const
{
    Log("soldier defaults %d %d %d %d %d %d %d %d %d %d %d %d", state.profileIndex, state.unknown34, state.unknown38,
        state.team, state.spawnZone, state.deployed, state.speechSet, state.spectating, state.vip, state.health,
        state.head.uid, state.head.typeId);
    state.name = C_fstr("S%d", m_netId);
    state.team = m_netId & 1;
    state.health = 100 + m_netId;
    state.head = {static_cast<int16_t>(m_netId), 2, 3};
}

// DirectPlay's server: GetClientInfo answers the name g_infoName (its first call: the size, g_infoSizeResult).
long g_infoSizeResult, g_infoResult;
const wchar_t *g_infoName = L"Bob";
struct T_MessageServer : T_SessionServer {
    HRESULT STDMETHODCALLTYPE GetClientInfo(DPNID id, void *info, DWORD *size, DWORD flags) override
    {
        if (!info) {
            Log("client info %u null %u %u", id, *size, flags);
            *size = 0x40;
            return g_infoSizeResult;
        }
        auto *i = static_cast<DPN_PLAYER_INFO *>(info);
        Log("client info %u %u %u %u %d", id, i->size, *size, flags, i->infoFlags);
        i->name = g_infoName;
        return g_infoResult;
    }
};
T_MessageServer g_messageServer;

// The game messages' quantisation (the senders'): 16 bits over -1000..1000.
S_NetQuantization g_quantization;
void SetupQuantization()
{
    for (int i = 0; i < 3; ++i) {
        g_quantization.deadZone[i] = 0.0f;
        g_quantization.min[i] = g_quantization.offset[i] = -1000.0f;
        g_quantization.range[i] = g_quantization.range40[i] = 2000.0f;
        g_quantization.bits[i] = 16;
        g_quantization.scale[i] = 65535.0f / 2000.0f;
    }
}

// The global network game ready (SendGameMessage, PostCommand and parts of HandleServerMessage use it): no players,
// nothing queued or written, sends never due, the server faked.
C_NetGame &MessageNet(uint32_t role)
{
    static bool ready;
    if (!ready) {
        g_netGame.m_playersLock.Init();
        g_netGame.m_queueLock.Init();
        InitializeCriticalSection(&GetGameApp()->m_commandLock);
        SetupQuantization();
        ready = true;
    }
    g_netGame.m_players.clear();
    g_netGame.m_received.clear();
    g_netGame.m_outgoing.clear();
    g_netGame.m_sendPeriod = 1 << 30;
    g_netGame.m_sendTime = 0;
    for (C_NetMessage &msg : g_netGame.m_messages) {
        msg.ResetPosition();
        msg.m_sender = msg.m_recipient = reinterpret_cast<S_NetPlayer *>(&g_quantization);
    }
    for (C_NetMessage *msg : {&g_netGame.m_direct, &g_netGame.m_unreliable}) {
        msg->ResetPosition();
        msg->m_sender = msg->m_recipient = reinterpret_cast<S_NetPlayer *>(&g_quantization);
    }
    g_netGame.m_server = &g_messageServer;
    g_netGame.m_transport->m_connections.clear();
    g_gameRole = role;
    g_localPlayerSlot = 0;
    g_gameProfile.m_mpSettings = &g_testMp;
    g_log.clear();
    return g_netGame;
}

// A stream's bits read back (a copy).
struct T_Reader {
    explicit T_Reader(const C_BitStream &stream) : bits(stream.BitPosition())
    {
        words.assign(stream.Data(), stream.Data() + (bits + 31) / 32);
        words.push_back(0);
        msg.Attach(words.data(), bits);
        msg.m_sender = msg.m_recipient = reinterpret_cast<S_NetPlayer *>(&g_quantization);
    }
    T_Reader(const void *data, int size) : bits(static_cast<uint32_t>(size) * 8)
    {
        words.resize(static_cast<size_t>(size) / 4 + 2);
        std::memcpy(words.data(), data, static_cast<size_t>(size));
        msg.Attach(words.data(), bits);
        msg.m_sender = msg.m_recipient = reinterpret_cast<S_NetPlayer *>(&g_quantization);
    }
    uint32_t Bits(int n) { return msg.ReadBits(n); }
    int Int() { return static_cast<int>(Bits(32)); }
    float Float()
    {
        const uint32_t u = Bits(32);
        float f;
        std::memcpy(&f, &u, sizeof f);
        return f;
    }
    std::string Str()
    {
        char text[256];
        msg.ReadString(text, 0xFF);
        return text;
    }
    // A game message's head: its type (0x14) and id.
    bool Game(int id) { return Bits(5) == NET_MESSAGE_GAME && Int() == id; }
    uint32_t Left() const { return bits - msg.BitPosition(); }

    uint32_t bits;
    std::vector<uint32_t> words;
    C_NetMessage msg;
};

// A chat line as the game queues it (0x110 bytes: the text, +0x104, +0x108, +0x10C).
char *ChatLine(const char *text, int a, int b, uint8_t c)
{
    auto *line = static_cast<char *>(operator new(0x110));
    std::memset(line, 0, 0x110);
    std::strcpy(line, text);
    std::memcpy(line + 0x104, &a, 4);
    std::memcpy(line + 0x108, &b, 4);
    line[0x10C] = static_cast<char>(c);
    return line;
}

// A frame at `pos` facing `dir` (its world matrix valid unless `stale`: then the local one is it).
I3D_frame *MessageFrame(S_vector pos, S_vector dir, bool stale)
{
    alignas(16) static unsigned char storage[8][sizeof(I3D_frame)];
    static int used;
    auto *frame = reinterpret_cast<I3D_frame *>(storage[used++ % 8]);
    std::memset(static_cast<void *>(frame), 0, sizeof(I3D_frame));
    S_matrix &m = stale ? frame->m_localMatrix : frame->m_worldMatrix;
    m(3) = pos;
    m(2) = dir;
    frame->m_frameFlags = stale ? 0 : FRMFLAGS_WMATRIX_VALID;
    return frame;
}
C_Human *MessageHuman(int netId, S_vector pos, S_vector dir, bool stale = false)
{
    C_Human *human = MakeHuman(false);
    human->m_netId = netId;
    human->m_frame = MessageFrame(pos, dir, stale);
    return human;
}

// A human whose uniform container (4) holds an item of `team` (none: -1).
struct T_Container {
    void **vtable;
    C_Item *item;
};
C_Item *__attribute__((thiscall)) FakeGetItem(T_Container *self) { return self->item; }
void GiveUniform(C_Human *human, int team)
{
    static void *vtable[32];
    static T_Container containers[8];
    static C_ItemContainer *lists[8][5];
    alignas(16) static unsigned char items[8][sizeof(C_Item)];
    alignas(16) static unsigned char defs[8][sizeof(C_ItemDef)];
    static int used;
    for (void *&slot : vtable)
        slot = reinterpret_cast<void *>(&FakeGetItem);
    const int i = used++ % 8;
    auto *item = reinterpret_cast<C_Item *>(items[i]);
    auto *def = reinterpret_cast<C_ItemDef *>(defs[i]);
    def->m_team = team;
    item->m_def = def;
    containers[i] = {vtable, team == -1 ? nullptr : item};
    lists[i][4] = reinterpret_cast<C_ItemContainer *>(&containers[i]);
    human->m_containers = lists[i];
}

// ---- a soldier's state ----------------------------------------------------------------------------------------------

S_SoldierState SampleState()
{
    S_SoldierState s;
    s.name = "Joe";
    s.team = 1;
    s.profileIndex = 12;
    s.spawnZone = 700;
    s.spectating = true;
    s.vip = false;
    s.health = 600;
    s.head = {1, 2, 3};
    s.uniform = {-4, 5, 6};
    s.backpack = {7, -8, 9};
    s.longGun = {10, 11, 12};
    s.hands = {13, 14, 15};
    s.belt = {{20, 21, 22}, {23, 24, 25}};
    s.backpackContents = {{30, 31, 32}};
    s.load.capacity = 40;
    s.load.weight = 1.5f;
    s.load.backpackMaxWeight = 2.5f;
    s.load.backpackMaxVolume = 7;
    s.load.backpackWeight = 3.25f;
    s.load.backpackVolume = 5;
    s.load.beltWeight = 0.75f;
    s.load.beltVolume = 9;
    return s;
}
bool SameItem(const S_SoldierItem &a, const S_SoldierItem &b)
{
    return a.uid == b.uid && a.typeId == b.typeId && a.present == b.present;
}
bool SameItems(const S_SoldierState &a, const S_SoldierState &b)
{
    if (a.belt.size() != b.belt.size() || a.backpackContents.size() != b.backpackContents.size())
        return false;
    for (size_t i = 0; i < a.belt.size(); ++i)
        if (!SameItem(a.belt[i], b.belt[i]))
            return false;
    for (size_t i = 0; i < a.backpackContents.size(); ++i)
        if (!SameItem(a.backpackContents[i], b.backpackContents[i]))
            return false;
    return SameItem(a.head, b.head) && SameItem(a.uniform, b.uniform) && SameItem(a.backpack, b.backpack) &&
           SameItem(a.longGun, b.longGun) && SameItem(a.hands, b.hands);
}
bool SameLoad(const S_SoldierLoad &a, const S_SoldierLoad &b) { return std::memcmp(&a, &b, sizeof a) == 0; }

void TestSoldierState()
{
    // The whole state: written and read back; -1 (the profile, the spawn zone) sent as all bits set, stored so.
    S_SoldierState s = SampleState();
    C_NetMessage out;
    NetWriteSoldierState(&out, s, false);
    CHECK(s.profileIndex == 12 && s.spawnZone == 700);
    C_NetMessage name;
    name.WriteString("Joe");
    CHECK(out.BitPosition() == name.BitPosition() + 1 + 8 + 10 + 1 + 1 + 10 + 5 * 40 + 8 + 2 * 40 + 8 + 40 + 256);
    T_Reader r(out);
    CHECK(r.Str() == "Joe" && r.Bits(1) == 1 && r.Bits(8) == 12 && r.Bits(10) == 700 && r.Bits(1) == 1 &&
          r.Bits(1) == 0 && r.Bits(10) == 600);
    CHECK(r.Bits(16) == 1 && r.Bits(16) == 2 && r.Bits(8) == 3);         // the head: uid, type, present
    CHECK(r.Bits(16) == 0xFFFC && r.Bits(16) == 5 && r.Bits(8) == 6);    // the uniform
    S_SoldierState back;
    T_Reader again(out);
    NetReadSoldierState(&again.msg, back, false);
    CHECK(again.Left() == 0);
    CHECK(back.name == "Joe" && back.team == 1 && back.profileIndex == 12 && back.spawnZone == 700 &&
          back.spectating && !back.vip && back.health == 600);
    CHECK(SameItems(back, s) && SameLoad(back.load, s.load));

    // Vip, the other team, -1s (written back in place as 0xFF / 0x3FF).
    s.team = 0;
    s.vip = true;
    s.spectating = false;
    s.profileIndex = -1;
    s.spawnZone = -1;
    C_NetMessage out2;
    NetWriteSoldierState(&out2, s, false);
    CHECK(s.profileIndex == 0xFF && s.spawnZone == 0x3FF);
    S_SoldierState back2;
    T_Reader r2(out2);
    NetReadSoldierState(&r2.msg, back2, false);
    CHECK(back2.team == 0 && back2.vip && !back2.spectating && back2.profileIndex == 0xFF && back2.spawnZone == 0x3FF);

    // The items only: 336 bits; the rest untouched; the lists appended to.
    S_SoldierState t = SampleState();
    t.profileIndex = -1;
    C_NetMessage items;
    NetWriteSoldierState(&items, t, true);
    CHECK(items.BitPosition() == 5 * 40 + 8 + 2 * 40 + 8 + 40 && t.profileIndex == -1);
    S_SoldierState u;
    u.name = "Kept";
    u.team = 3;
    u.health = 9;
    u.load.capacity = 77;
    u.belt = {{90, 91, 92}};
    T_Reader r3(items);
    NetReadSoldierState(&r3.msg, u, true);
    CHECK(r3.Left() == 0 && u.name == "Kept" && u.team == 3 && u.health == 9 && u.load.capacity == 77);
    CHECK(u.belt.size() == 3 && SameItem(u.belt[0], {90, 91, 92}) && SameItem(u.belt[2], {23, 24, 25}));
    CHECK(u.backpackContents.size() == 1 && SameItem(u.backpackContents[0], {30, 31, 32}) && SameItem(u.head, t.head));
    // A negative type read back as a short.
    CHECK(u.uniform.uid == -4 && u.backpack.typeId == -8);
}

// ---- the game messages' streams -------------------------------------------------------------------------------------

void TestBeginGameMessage()
{
    C_NetGame &net = MessageNet(GAME_ROLE_CLIENT);
    net.m_messages[0].m_channel = 2;
    net.m_messages[0].m_target = 9;
    C_BitStream *s = net.BeginGameMessage(0, 9, true);
    CHECK(s == &net.m_messages[0] && net.m_messages[0].m_channel == 0 && net.m_messages[0].m_pending == 1 &&
          net.m_messages[0].m_target == 0 && !net.m_messages[0].m_unreliable &&
          net.m_messages[0].m_type == NET_MESSAGE_GAME);
    T_Reader r(*s);
    CHECK(r.bits == 37 && r.Game(9));
    // The server's unreliable ones on their own stream; a target's: the direct one.
    g_gameRole = GAME_ROLE_SERVER;
    s = net.BeginGameMessage(0, 4, false);
    CHECK(s == &net.m_unreliable && net.m_unreliable.m_unreliable && net.m_unreliable.m_pending == 1);
    s = net.BeginGameMessage(5, 4, true);
    CHECK(s == &net.m_direct && net.m_direct.m_target == 5 && !net.m_direct.m_unreliable);
    g_gameRole = GAME_ROLE_CLIENT;
    net.m_direct.ResetPosition();
    s = net.BeginGameMessage(6, 4, false);
    CHECK(s == &net.m_direct && net.m_direct.m_target == 6 && net.m_direct.m_unreliable);
    // A command: unreliable, to everyone; a reply: reliable, to its target.
    net.m_messages[0].ResetPosition();
    s = net.BeginCommand(0x123);
    CHECK(s == &net.m_messages[0] && net.m_messages[0].m_unreliable && net.m_messages[0].m_target == 0);
    T_Reader r2(*s);
    CHECK(r2.Game(0x123));
    net.m_direct.ResetPosition();
    s = net.BeginReply(7, 0x45);
    CHECK(s == &net.m_direct && !net.m_direct.m_unreliable && net.m_direct.m_target == 7);
    T_Reader r3(*s);
    CHECK(r3.Game(0x45));
}

// ---- SendGameMessage ------------------------------------------------------------------------------------------------

// The messages queued so far sent (a reliable call of `type`): what everyone's stream holds.
T_Reader Flush(int type, int to, intptr_t a, int b)
{
    g_netGame.m_messages[0].ResetPosition();
    GetGameApp()->SendGameMessage(type, to, a, b, true);
    return T_Reader(g_netGame.m_messages[0]);
}

void TestSendGameMessageQueue()
{
    C_NetGame &net = MessageNet(GAME_ROLE_CLIENT);
    // Not reliable: queued (in their order); reliable: the queue sent (the call itself not).
    GetGameApp()->SendGameMessage(0xD, 0, 2, 0, false);
    GetGameApp()->SendGameMessage(0x17, 0, 1, 0, false);
    CHECK(net.m_messages[0].BitPosition() == 0);
    T_Reader r = Flush(0xD, 0, 3, 0);
    CHECK(r.Game(0xD) && r.Bits(2) == 2 && r.Game(0x17) && r.Bits(1) == 1 && r.Left() == 0);
    // The queue empty: nothing.
    T_Reader none = Flush(0xD, 0, 3, 0);
    CHECK(none.bits == 0);
    // A target's: the direct stream, sent (the local player's: queued as outgoing, the end added).
    net.m_players.push_back(Player(55, "me"));
    GetGameApp()->SendGameMessage(0xD, 55, 1, 0, false);
    Flush(0, 0, 0, 0);
    CHECK(net.m_outgoing.size() == 1 && net.m_direct.BitPosition() == 0);
    if (net.m_outgoing.size() == 1) {
        T_Reader out(net.m_outgoing.front().data, net.m_outgoing.front().size);
        CHECK(out.Game(0xD) && out.Bits(2) == 1 && out.Bits(5) == NET_MESSAGE_END);
    }
    // Each message processed first (its time: 1000 ms).
    net.m_sendTime = 0;
    GetGameApp()->SendGameMessage(0x17, 0, 0, 0, false);
    GetGameApp()->SendGameMessage(0x17, 0, 0, 0, false);
    Flush(0, 0, 0, 0);
    CHECK(net.m_sendTime == 2000);
    net.m_sendTime = 0;
}

void TestSendGameMessageSimple()
{
    MessageNet(GAME_ROLE_CLIENT);
    struct {
        int type;
        intptr_t a;
        int b;
        int bitsA, bitsB;
    } cases[] = {{0x13, 0x12345678, 0, 32, 0}, {0xA, 21, 9, 5, 4}, {0xD, 3, 0, 2, 0}, {0xE, 30, 2, 5, 2},
                 {0xF, 17, 1, 5, 1}, {0x11, 0x7FFFFFF1, 0x44, 32, 32}, {0x16, 100, 1000, 7, 10}, {0x17, 1, 0, 1, 0}};
    for (const auto &c : cases) {
        GetGameApp()->SendGameMessage(c.type, 0, c.a, c.b, false);
        T_Reader r = Flush(0, 0, 0, 0);
        CHECK(r.Game(c.type) && r.Bits(c.bitsA) == static_cast<uint32_t>(c.a));
        if (c.bitsB)
            CHECK(r.Bits(c.bitsB) == static_cast<uint32_t>(c.b));
        CHECK(r.Left() == 0);
    }
    // The writers' markers: the map (6), the temporary actors (7, 0x14), the scores (8), the rules (0x12).
    for (int type : {6, 7, 0x14, 0x12}) {
        GetGameApp()->SendGameMessage(type, 0, 0, 0, false);
        T_Reader r = Flush(0, 0, 0, 0);
        CHECK(r.Game(type) && r.Bits(8) == (type == 6 ? 0xC1u : type == 0x12 ? 0xC2u : 0xC5u) && r.Left() == 0);
    }
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER | GAME_ROLE_CLIENT);
    net.m_players.push_back(Player(5, "a"));
    net.m_players[0].human = MakeHuman(false);
    net.m_players[0].stats[NET_STAT_SCORE] = 321;
    GetGameApp()->SendGameMessage(8, 0, 0, 0, false);
    T_Reader scores = Flush(0, 0, 0, 0);
    C_NetMessage expected;
    net.WriteScores(&expected);
    CHECK(scores.Game(8) && expected.BitPosition() > 6 && scores.Left() == expected.BitPosition() &&
          scores.Bits(6) == 0 && scores.Bits(11) == 321);
}

void TestSendGameMessageJoin()
{
    // 0: the session's ids, the rules, the map, the named players (name, team, rank); a deathmatch: the uniform's
    // side (none or not the allies': 0).
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    GetGameApp()->m_randomId = 0xABCDEF01;
    net.m_unknown448 = 0x1234;
    g_testMp.gameType = MP_GAME_OBJECTIVES;
    net.m_players.push_back(Player(5, "Ann"));
    net.m_players.push_back(Player(0, ""));
    net.m_players.push_back(Player(6, "Ben"));
    net.m_players[0].team = 2;
    net.m_players[0].stats[NET_STAT_RANK] = 17;
    net.m_players[2].team = 1;
    net.m_players[2].stats[NET_STAT_RANK] = 3;
    net.m_players[2].human = MessageHuman(1, S_vector(0, 0, 0), S_vector(0, 0, 1));
    GiveUniform(net.m_players[2].human, SIDE_ALLIES);
    GetGameApp()->SendGameMessage(0, 0, 0, 0, false);
    T_Reader r = Flush(0, 0, 0, 0);
    CHECK(r.Game(0) && r.Bits(32) == 0xABCDEF01 && r.Int() == 0x1234 && r.Bits(8) == 0xC2 && r.Bits(8) == 0xC1);
    CHECK(r.Bits(8) == 2 && r.Str() == "Ann" && r.Bits(2) == 2 && r.Bits(5) == 17);
    CHECK(r.Str() == "Ben" && r.Bits(2) == 1 && r.Bits(5) == 3 && r.Left() == 0);

    g_testMp.gameType = MP_GAME_DEATHMATCH;
    net.m_players[0].human = MessageHuman(2, S_vector(0, 0, 0), S_vector(0, 0, 1));
    GiveUniform(net.m_players[0].human, 1);         // the axis'
    GiveUniform(net.m_players[2].human, SIDE_ALLIES);
    net.m_players[2].team = 0;
    GetGameApp()->SendGameMessage(0, 0, 0, 0, false);
    T_Reader dm = Flush(0, 0, 0, 0);
    CHECK(dm.Game(0) && dm.Int() != 0 && dm.Int() == 0x1234 && dm.Bits(8) == 0xC2 && dm.Bits(8) == 0xC1 &&
          dm.Bits(8) == 2);
    CHECK(dm.Str() == "Ann" && dm.Bits(2) == 0 && dm.Bits(5) == 17 && dm.Str() == "Ben" && dm.Bits(2) == 1);
    // No uniform: 0; no human: its team.
    GiveUniform(net.m_players[2].human, -1);
    net.m_players[0].human = nullptr;
    GetGameApp()->SendGameMessage(0, 0, 0, 0, false);
    T_Reader none = Flush(0, 0, 0, 0);
    CHECK(none.Game(0) && none.Bits(32) && none.Bits(32) == 0x1234 && none.Bits(24) && true);
    CHECK(none.Str() == "Ann" && none.Bits(2) == 2 && none.Bits(5) == 17 && none.Str() == "Ben" && none.Bits(2) == 0);
    g_testMp.gameType = MP_GAME_OBJECTIVES;
}

void TestSendGameMessagePlayers()
{
    alignas(16) static unsigned char gameStorage[sizeof(C_game)];
    g_game = reinterpret_cast<C_game *>(gameStorage);
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    net.m_players.push_back(Player(5, "Ann"));
    net.m_players.push_back(Player(6, "Ben"));
    // 2: the player left (told: the name and text 0x22CB; the game too), its id.
    GetGameApp()->SendGameMessage(2, 0, 6, 0, false);
    T_Reader left = Flush(0, 0, 0, 0);
    CHECK(left.Game(2) && left.Int() == 6 && left.Left() == 0);
    CHECK(Logged("app print Ben T22CB") && Logged("left 6"));
    // An unknown player: not told, still the game's and the id.
    g_log.clear();
    GetGameApp()->SendGameMessage(2, 0, 99, 0, false);
    T_Reader unknown = Flush(0, 0, 0, 0);
    CHECK(unknown.Game(2) && unknown.Int() == 99 && Logged("left 99") && g_log.size() == 1);
    // 9: a player joined (told: text 0x22CA).
    char name[] = "Cat";
    GetGameApp()->SendGameMessage(9, 0, reinterpret_cast<intptr_t>(name), 0, false);
    T_Reader joined = Flush(0, 0, 0, 0);
    CHECK(joined.Game(9) && joined.Str() == "Cat" && joined.Left() == 0 && Logged("app print Cat T22CA"));
    // 4: a chat line (freed).
    GetGameApp()->SendGameMessage(4, 0, reinterpret_cast<intptr_t>(ChatLine("hi", 7, -2, 0x81)), 0, false);
    T_Reader chat = Flush(0, 0, 0, 0);
    CHECK(chat.Game(4) && chat.Int() == 7 && chat.Int() == -2 && chat.Bits(1) == 1 && chat.Str() == "hi" &&
          chat.Left() == 0);
    // 0xC: the named players' slots (6 bits) and teams (2 bits); 0x21 ends.
    net.m_players.insert(net.m_players.begin() + 1, Player(0, ""));
    net.m_players[0].team = 3;
    net.m_players[2].team = 1;
    GetGameApp()->SendGameMessage(0xC, 0, 0, 0, false);
    T_Reader teams = Flush(0, 0, 0, 0);
    CHECK(teams.Game(0xC) && teams.Bits(6) == 0 && teams.Bits(2) == 3 && teams.Bits(6) == 2 && teams.Bits(2) == 1 &&
          teams.Bits(6) == 0x21 && teams.Left() == 0);
    g_game = nullptr;
}

void TestSendGameMessagePlaces()
{
    // 5: the players' count and places (the last first): the position, the z axis normalised (its length 1 within
    // 1e-8: as it is; zero: one axis' sign).
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    net.m_players.push_back(Player(5, "a"));
    net.m_players.push_back(Player(6, "b"));
    net.m_players.push_back(Player(7, "c"));
    net.m_players[0].human = MessageHuman(1, S_vector(1, 2, 3), S_vector(0, 3, 4));
    net.m_players[1].human = MessageHuman(2, S_vector(-4, 5, -6), S_vector(0, 0, 0), true);
    net.m_players[2].human = MessageHuman(3, S_vector(7, 8, 9), S_vector(0.6f, 0, 0.8f));
    GetGameApp()->SendGameMessage(5, 0, 0, 0, false);
    T_Reader r = Flush(0, 0, 0, 0);
    CHECK(r.Game(5) && r.Int() == 3);
    CHECK(r.Float() == 7 && r.Float() == 8 && r.Float() == 9);
    const float x = r.Float(), y = r.Float(), z = r.Float();
    const float sq = (0.8f * 0.8f + 0.0f) + 0.6f * 0.6f;
    const float k = 1.0f / std::sqrt(sq);
    CHECK(std::fabs(sq - 1.0f) >= 1e-8f ? (x == 0.6f * k && y == 0.0f && z == k * 0.8f) : (x == 0.6f && z == 0.8f));
    // The stale frame: updated; the zero direction: y's sign (+1).
    CHECK(r.Float() == -4 && r.Float() == 5 && r.Float() == -6 && Logged("update matrix"));
    CHECK(r.Float() == 0 && r.Float() == 1 && r.Float() == 0);
    CHECK(r.Float() == 1 && r.Float() == 2 && r.Float() == 3);
    const float k5 = 1.0f / std::sqrt((16.0f + 9.0f) + 0.0f);
    CHECK(r.Float() == 0 && r.Float() == 3 * k5 && r.Float() == k5 * 4 && r.Left() == 0);
    // No players: nothing after the head.
    net.m_players.clear();
    GetGameApp()->SendGameMessage(5, 0, 0, 0, false);
    T_Reader none = Flush(0, 0, 0, 0);
    CHECK(none.Game(5) && none.Left() == 0);
}

void TestNormalisedSigns()
{
    // The zero directions: x's sign first, then z's, then y's (others kept).
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    const S_vector dirs[] = {S_vector(1e-5f, 0, 0), S_vector(-1e-5f, 0, 0), S_vector(0, 0, 1e-5f),
                             S_vector(0, 0, -1e-5f), S_vector(0, -1e-5f, 0), S_vector(-1e-5f, 0, 1e-5f),
                             S_vector(0, 1e-5f, -1e-5f)};
    const S_vector expected[] = {S_vector(1, 0, 0), S_vector(-1, 0, 0), S_vector(0, 0, 1), S_vector(0, 0, -1),
                                 S_vector(0, -1, 0), S_vector(-1, 0, 1e-5f), S_vector(0, 1e-5f, -1)};
    for (int i = 0; i < 7; ++i) {
        net.m_players.assign(1, Player(5, "a"));
        net.m_players[0].human = MessageHuman(1, S_vector(0, 0, 0), dirs[i]);
        GetGameApp()->SendGameMessage(5, 0, 0, 0, false);
        T_Reader r = Flush(0, 0, 0, 0);
        CHECK(r.Game(5) && r.Int() == 1 && r.Bits(32) == 0 && r.Bits(32) == 0 && r.Bits(32) == 0);
        const float x = r.Float(), y = r.Float(), z = r.Float();
        CHECK(x == expected[i].x && y == expected[i].y && z == expected[i].z);
        if (!(x == expected[i].x && y == expected[i].y && z == expected[i].z))
            std::printf("  dir %d: %g %g %g\n", i, x, y, z);
    }
    // A long one: scaled by 1 / sqrt.
    net.m_players.assign(1, Player(5, "a"));
    net.m_players[0].human = MessageHuman(1, S_vector(0, 0, 0), S_vector(2, -3, 6));
    GetGameApp()->SendGameMessage(5, 0, 0, 0, false);
    T_Reader r = Flush(0, 0, 0, 0);
    CHECK(r.Game(5) && r.Int() == 1 && r.Bits(32) == 0 && r.Bits(32) == 0 && r.Bits(32) == 0);
    const float k = 1.0f / std::sqrt((36.0f + 9.0f) + 4.0f);
    CHECK(r.Float() == 2 * k && r.Float() == -3 * k && r.Float() == k * 6);
}

void TestSendGameMessageSpawn()
{
    // 3: a spawn: the slot's teams set, the slot, the position (quantised, mask 7), the direction, the soldier
    // state, the stats (10 bits, then 7 bits in the order 2 1 3 .. 9 11 10); the block freed.
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    net.m_players.push_back(Player(5, "a"));
    net.m_players.push_back(Player(6, "b"));
    auto *spawn = static_cast<float *>(_aligned_malloc(0x30, 16));
    spawn[0] = 10.5f;
    spawn[1] = -20.25f;
    spawn[2] = 300.0f;
    spawn[4] = 0.0f;
    spawn[5] = 0.0f;
    spawn[6] = 1.0f;
    auto *state = new S_SoldierState(SampleState());
    state->team = 1;
    auto *stats = static_cast<int *>(operator new(12 * sizeof(int)));
    for (int i = 0; i < 12; ++i)
        stats[i] = 10 * i + 1;
    reinterpret_cast<intptr_t *>(spawn)[8] = reinterpret_cast<intptr_t>(state);
    reinterpret_cast<intptr_t *>(spawn)[9] = reinterpret_cast<intptr_t>(stats);
    reinterpret_cast<int *>(spawn)[10] = 1;
    GetGameApp()->SendGameMessage(3, 0, reinterpret_cast<intptr_t>(spawn), 0, false);
    T_Reader r = Flush(0, 0, 0, 0);
    const S_NetPlayerState untouched;
    CHECK(net.m_players[1].team == 1 && net.m_players[1].pendingTeam == 1 && net.m_players[0].team == untouched.team &&
          net.m_players[0].pendingTeam == untouched.pendingTeam);
    CHECK(r.Game(3) && r.Int() == 1);
    float pos[3];
    r.msg.ReadQuantized(pos, &g_quantization, 7);
    CHECK(std::fabs(pos[0] - 10.5f) < 0.05f && std::fabs(pos[1] + 20.25f) < 0.05f && std::fabs(pos[2] - 300) < 0.05f);
    S_vector dir;
    r.msg.ReadDirection3D(dir, -1);
    CHECK(std::fabs(dir.z - 1.0f) < 0.01f);
    S_SoldierState back;
    NetReadSoldierState(&r.msg, back, false);
    CHECK(back.name == "Joe" && back.team == 1 && back.health == 600 && SameItems(back, SampleState()));
    CHECK(r.Bits(10) == 1);
    for (int i : {2, 1, 3, 4, 5, 6, 7, 8, 9, 11, 10})
        CHECK(r.Bits(7) == static_cast<uint32_t>(10 * i + 1));
    CHECK(r.Left() == 0);
}

void TestSendGameMessageFullState()
{
    // 0x15 with nothing: a 0 bit.
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    alignas(16) static unsigned char gameStorage[sizeof(C_game)];
    g_game = reinterpret_cast<C_game *>(gameStorage);
    GetGameApp()->SendGameMessage(0x15, 0, 0, 0, false);
    T_Reader empty = Flush(0, 0, 0, 0);
    CHECK(empty.Game(0x15) && empty.Bits(1) == 0 && empty.Left() == 0);
    // Else: 1, the count, each player (connection, name, human id or -1, stats; with a human its place and soldier
    // state, made from the defaults), the modules', the objectives; the target's join stage 2; the flag set.
    net.m_unknown8 = false;
    net.m_players.push_back(Player(5, "Ann"));
    net.m_players.push_back(Player(6, "Ben"));
    net.m_players[0].stats[NET_STAT_SCORE] = 12;
    net.m_players[1].human = MessageHuman(9, S_vector(1, 2, 3), S_vector(3, 0, 4));
    g_localPlayerSlot = 1;                          // (the target local: its message queued as outgoing)
    GetGameApp()->SendGameMessage(0x15, 6, 1, 0, false);
    Flush(0, 0, 0, 0);
    g_localPlayerSlot = 0;
    CHECK(net.m_players[1].joinStage == 2 && net.m_players[0].joinStage == 0 && net.m_unknown8);
    CHECK(Logged("soldier defaults -1 -1 -1 -1 1023 1 -1 0 0 0 -1 -1"));
    CHECK(net.m_outgoing.size() == 1);
    // (The same to everyone, read back.)
    net.m_unknown8 = false;
    GetGameApp()->SendGameMessage(0x15, 0, 1, 0, false);
    T_Reader r = Flush(0, 0, 0, 0);
    CHECK(r.Game(0x15) && r.Bits(1) == 1 && r.Bits(8) == 2);
    CHECK(r.Int() == 5 && r.Str() == "Ann" && r.Int() == -1);
    S_NetStats stats;
    stats.Read(&r.msg);
    CHECK(stats[NET_STAT_SCORE] == 12);
    CHECK(r.Int() == 6 && r.Str() == "Ben" && r.Int() == 9);
    stats.Read(&r.msg);
    CHECK(r.Float() == 1 && r.Float() == 2 && r.Float() == 3);
    const float x = r.Float(), y = r.Float(), z = r.Float();
    const float k = 1.0f / std::sqrt((9.0f + 0.0f) + 16.0f);
    CHECK(x == 3 * k && y == 0 && z == k * 4);
    S_SoldierState back;
    NetReadSoldierState(&r.msg, back, false);
    CHECK(back.name == "S9" && back.team == 1 && back.health == 109 && back.profileIndex == 0xFF &&
          back.spawnZone == 0x3FF && back.head.uid == 9 && back.uniform.uid == -1 && back.belt.empty());
    CHECK(r.Bits(8) == 0xC3 && r.Bits(8) == 0xC4 && r.Left() == 0);
    // An unknown target: no stage set.
    net.m_players[1].joinStage = 0;
    GetGameApp()->SendGameMessage(0x15, 77, 1, 0, false);
    Flush(0, 0, 0, 0);
    CHECK(net.m_players[1].joinStage == 0 && net.m_players[0].joinStage == 0);
    g_game = nullptr;
}

// ---- PostCommand ----------------------------------------------------------------------------------------------------

T_Reader FlushCommands()
{
    g_netGame.m_messages[0].ResetPosition();
    GetGameApp()->PostCommand(0, 0, 0, true);
    return T_Reader(g_netGame.m_messages[0]);
}

void TestPostCommand()
{
    C_NetGame &net = MessageNet(GAME_ROLE_CLIENT);
    g_localPlayerSlot = 3;
    // Not flushed: queued; flushed: the queue sent (not the call's own), in order, unreliable.
    GetGameApp()->PostCommand(2, 0, 0, false);
    GetGameApp()->PostCommand(4, 0x55667788, 0, false);
    CHECK(net.m_messages[0].BitPosition() == 0);
    T_Reader r = FlushCommands();
    CHECK(net.m_messages[0].m_unreliable);
    CHECK(r.Game(2) && r.Int() == 3 && r.Game(4) && r.Int() == 0x55667788 && r.Left() == 0);
    T_Reader none = FlushCommands();
    CHECK(none.bits == 0);
    // 5: the slot and a; 10: a in 7 bits, b in 10.
    GetGameApp()->PostCommand(5, 44, 0, false);
    GetGameApp()->PostCommand(10, 99, 1000, false);
    T_Reader r2 = FlushCommands();
    CHECK(r2.Game(5) && r2.Int() == 3 && r2.Int() == 44 && r2.Game(10) && r2.Bits(7) == 99 && r2.Bits(10) == 1000 &&
          r2.Left() == 0);
    // 3: a chat line; 6 / 7: the slot (5 bits) and a text; 8: a text (each freed).
    GetGameApp()->PostCommand(3, reinterpret_cast<intptr_t>(ChatLine("yo", -1, 4, 0)), 0, false);
    char *t6 = static_cast<char *>(operator new(8));
    std::strcpy(t6, "six");
    char *t7 = static_cast<char *>(operator new(8));
    std::strcpy(t7, "seven");
    char *t8 = static_cast<char *>(operator new(8));
    std::strcpy(t8, "eight");
    GetGameApp()->PostCommand(6, reinterpret_cast<intptr_t>(t6), 0, false);
    GetGameApp()->PostCommand(7, reinterpret_cast<intptr_t>(t7), 0, false);
    GetGameApp()->PostCommand(8, reinterpret_cast<intptr_t>(t8), 0, false);
    T_Reader r3 = FlushCommands();
    CHECK(r3.Game(3) && r3.Int() == -1 && r3.Int() == 4 && r3.Bits(1) == 0 && r3.Str() == "yo");
    CHECK(r3.Game(6) && r3.Bits(5) == 3 && r3.Str() == "six" && r3.Game(7) && r3.Bits(5) == 3 && r3.Str() == "seven");
    CHECK(r3.Game(8) && r3.Str() == "eight" && r3.Left() == 0);
    // 9: the slot, the actors from 32 counted (16 bits).
    g_actorsModule.m_actors.assign(40, nullptr);
    C_Actor *actor = reinterpret_cast<C_Actor *>(MakeHuman(false));
    g_actors[31] = actor;
    g_actors[32] = actor;
    g_actors[35] = actor;
    g_actors[39] = actor;
    g_actors[40] = actor;
    GetGameApp()->PostCommand(9, 0, 0, false);
    T_Reader r4 = FlushCommands();
    CHECK(r4.Game(9) && r4.Bits(5) == 3 && r4.Bits(16) == 3 && r4.Left() == 0);
    g_actors.clear();
    g_actorsModule.m_actors.clear();
    g_localPlayerSlot = 0;
}

void TestPostCommandSoldier()
{
    // 0 / 1: the slot and the squad's first soldier (none: an empty state; Fix: the original reads null).
    MessageNet(GAME_ROLE_CLIENT);
    g_localPlayerSlot = 2;
    alignas(16) static unsigned char squadStorage[sizeof(S_MissionSquad)];
    auto *squad = reinterpret_cast<S_MissionSquad *>(squadStorage);
    g_gameProfile.m_missionSquad = squad;
    GetGameApp()->PostCommand(1, 0, 0, false);
    T_Reader none = FlushCommands();
    S_SoldierState empty;
    CHECK(none.Game(1) && none.Int() == 2);
    NetReadSoldierState(&none.msg, empty, false);
    CHECK(empty.name == "" && empty.belt.empty() && none.Left() == 0);
    S_SquadSoldier first, second;
    static_cast<S_SoldierState &>(first) = SampleState();
    first.profileIndex = -1;
    second.name = "Two";
    squad->states.push_back(&first);
    squad->states.push_back(&second);
    for (int command : {0, 1}) {
        GetGameApp()->PostCommand(command, 0, 0, false);
        T_Reader r = FlushCommands();
        S_SoldierState back;
        CHECK(r.Game(command) && r.Int() == 2);
        NetReadSoldierState(&r.msg, back, false);
        CHECK(back.name == "Joe" && SameItems(back, SampleState()) && back.profileIndex == 0xFF && r.Left() == 0);
    }
    CHECK(first.profileIndex == 0xFF);
    squad->states.clear();
    g_gameProfile.m_missionSquad = nullptr;
    g_localPlayerSlot = 0;
}

// ---- HandleServerMessage --------------------------------------------------------------------------------------------

long Server(uint32_t type, void *message)
{
    return C_NetGame::HandleServerMessage(&g_netGame, nullptr, type, message);
}

void TestServerCreatePlayer()
{
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    g_infoSizeResult = static_cast<long>(0x80158100);
    g_infoResult = 0;
    g_infoName = L"Bob";
    net.m_players.push_back(Player(5, "Bob"));
    S_NetPlayerState pending = Player(-1, "");
    pending.address = 0x0A000002;
    pending.admin = true;
    pending.activityTime = 0;
    pending.stats[2] = 7;
    pending.stats[8] = 9;
    net.m_players.push_back(pending);
    DWORD create[3] = {12, 77, 1};
    CHECK(Server(0xFFFF0007, create) == 0);
    CHECK(Logged("client info 77 null 0 0") && Logged("client info 77 24 64 0 0"));
    const S_NetPlayerState &p = net.m_players[1];
    CHECK(p.connection == 77 && std::strcmp(p.name, "Bob2") == 0 && !p.admin && p.activityTime != 0);
    CHECK(p.stats[2] == 0 && p.stats[8] == 0);
    CHECK(p.link && p.link->m_id == 1 && p.link->m_address == 0x0A000002 &&
          p.link->m_socket == &net.m_transport->m_socket);
    CHECK(net.m_transport->m_connections.size() == 1 && net.m_transport->m_connections[0] == p.link);

    // The name: ANSI, or UTF-8 with the unicode font or CJK characters; unique.
    g_infoName = L"é";
    CHECK(Server(0xFFFF0007, create) == 0 && std::strcmp(net.m_players[1].name, "\xe9") == 0);
    g_gameProfile.m_unicodeFont = true;
    CHECK(Server(0xFFFF0007, create) == 0 && std::strcmp(net.m_players[1].name, "\xc3\xa9") == 0);
    g_gameProfile.m_unicodeFont = false;
    // (The slot's own name counts: the same again gets a number.)
    g_cjk = true;
    CHECK(Server(0xFFFF0007, create) == 0 && std::strcmp(net.m_players[1].name, "\xc3\xa9" "2") == 0);
    g_cjk = false;
    g_infoName = L"Solo";
    CHECK(Server(0xFFFF0007, create) == 0 && std::strcmp(net.m_players[1].name, "Solo") == 0);

    // The size's call succeeding is as good.
    g_infoSizeResult = 0;
    CHECK(Server(0xFFFF0007, create) == 0 && net.m_players[1].connection == 77);
    // The second call failing: its error kept (text "(2)"); the slot untouched.
    net.m_players[1].connection = -1;
    g_infoResult = static_cast<long>(0x80150001);
    net.m_lastError = 0;
    CHECK(Server(0xFFFF0007, create) == static_cast<long>(0x80150001) &&
          net.m_lastError == static_cast<long>(0x80150001) &&
          std::strcmp(net.m_lastErrorText, "DPserver->GetClientInfo failed (2)") == 0 &&
          net.m_players[1].connection == -1);
    // The first failing (not too small): "(1)".
    g_infoResult = 0;
    g_infoSizeResult = static_cast<long>(0x80150002);
    CHECK(Server(0xFFFF0007, create) == static_cast<long>(0x80150002) &&
          net.m_lastError == static_cast<long>(0x80150002) &&
          std::strcmp(net.m_lastErrorText, "DPserver->GetClientInfo failed (1)") == 0);
    // Not a client (the server's own): a playing server's slot appended with its id; a dedicated one's not.
    g_infoSizeResult = static_cast<long>(0x80158420);
    net.m_lastError = 0;
    const size_t before = net.m_players.size();
    CHECK(Server(0xFFFF0007, create) == 0 && net.m_players.size() == before && net.m_lastError == 0);
    g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    CHECK(Server(0xFFFF0007, create) == 0 && net.m_players.size() == before + 1 &&
          net.m_players.back().connection == 77 &&
          net.m_players.back().name[0] == 0 && net.m_players.back().keyQuery == -1 && net.m_lastError == 0);
    g_infoSizeResult = g_infoResult = 0;
    g_infoName = L"Bob";
}

void TestServerDestroyPlayer()
{
    // A player gone: its link removed from the transport and deleted, its key check dropped; told (queued: 2).
    alignas(16) static unsigned char gameStorage[sizeof(C_game)];
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    net.m_players.push_back(Player(5, "Ann"));
    net.m_players.push_back(Player(6, "Ben"));
    net.m_players[1].link = new C_NetConnection(1, 0, &net.m_transport->m_socket);
    net.m_transport->Add(net.m_players[1].link);
    net.m_players[1].keyQuery = 4;
    net.m_keyQueries[4].state = KEY_KEPT;
    DWORD destroy[2] = {8, 6};
    g_game = nullptr;
    CHECK(Server(0xFFFF0009, destroy) == 0);
    CHECK(net.m_players[1].link == nullptr && net.m_transport->m_connections.empty() &&
          net.m_players[1].keyQuery == -1 &&
          net.m_keyQueries[4].state == KEY_FREE && Logged("gcd drop 750 4"));
    // (No game: not told.)
    T_Reader none = Flush(0x17, 0, 0, 0);
    CHECK(none.bits == 0);
    // With a game: told; a stranger: not; the slot kept (its name).
    g_game = reinterpret_cast<C_game *>(gameStorage);
    net.m_players[1].keyQuery = -1;
    CHECK(Server(0xFFFF0009, destroy) == 0 && std::strcmp(net.m_players[1].name, "Ben") == 0);
    DWORD stranger[2] = {8, 66};
    CHECK(Server(0xFFFF0009, stranger) == 0);
    T_Reader told = Flush(0, 0, 0, 0);
    CHECK(told.Game(2) && told.Int() == 6 && told.Left() == 0 && Logged("left 6") && !Logged("left 66"));
    g_game = nullptr;
}

void TestServerEnumQuery()
{
    // A query's answer (a mission running past 99 ms): the request (2 bits), the map, the version, the rules, the
    // players, the password, the port (GameSpy's listing: the settings', else 31567), the voice, 2.
    alignas(16) static unsigned char gameStorage[sizeof(C_game)];
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    C_MpMap map(C_str("Arnhem"), C_str("dir"));
    g_currentMap = &map;
    g_testMp.gameType = MP_GAME_OCCUPATION;
    g_testMp.dedicated = true;
    g_testMp.maxClients = 33;
    g_testMp.password = C_str("pw");
    g_testMp.unknown174 = 1;
    g_testMp.port = 11001;
    g_testMp.unknown178 = 3;
    net.m_players.push_back(Player(5, "a"));
    net.m_players.push_back(Player(6, "b"));
    int request = 0;
    struct {
        DWORD size;
        void *sender, *device;
        const int *request;
        DWORD requestSize, maxResponse;
        void *response;
        DWORD responseSize;
        void *context;
    } query = {0x24, nullptr, nullptr, &request, 4, 1000, nullptr, 0, nullptr};
    g_game = nullptr;
    CHECK(Server(0xFFFF000A, &query) == -1 && query.response == nullptr);
    g_game = reinterpret_cast<C_game *>(gameStorage);
    g_game->m_missionTime = 99;
    CHECK(Server(0xFFFF000A, &query) == -1 && query.response == nullptr);
    g_game->m_missionTime = 100;
    CHECK(Server(0xFFFF000A, &query) == 0 && query.response != nullptr);
    {
        T_Reader r(query.response, static_cast<int>(query.responseSize));
        CHECK(r.Bits(2) == 0 && r.Str() == "Arnhem" && r.Bits(8) == 2 && r.Bits(8) == 1 &&
              r.Bits(3) == MP_GAME_OCCUPATION && r.Bits(1) == 1 && r.Bits(6) == 33 &&
              r.Bits(6) == static_cast<uint32_t>(net.CountPlayers()) && r.Bits(1) == 1 && r.Bits(16) == 11001 &&
              r.Bits(1) == 1 && r.Bits(2) == 2);
        const uint32_t used = r.msg.BitPosition();
        CHECK(query.responseSize == (used + 7) / 8 + 1);
    }
    operator delete(query.response);
    // No listing, no password, no voice; a status query (1): the status after it.
    g_testMp.unknown174 = 0;
    g_testMp.password = C_str();
    g_testMp.unknown178 = 0;
    g_testMp.dedicated = false;
    request = 1;
    CHECK(Server(0xFFFF000A, &query) == 0);
    {
        T_Reader r(query.response, static_cast<int>(query.responseSize));
        CHECK(r.Bits(2) == 1 && r.Str() == "Arnhem" && r.Bits(16) == 0x0102 && r.Bits(3) == MP_GAME_OCCUPATION &&
              r.Bits(1) == 0 && r.Bits(12) && r.Bits(1) == 0 && r.Bits(16) == 31567 && r.Bits(1) == 0 &&
              r.Bits(2) == 2);
        S_ServerStatus status, expected;
        status.Read(&r.msg);
        net.FillServerStatus(&expected);
        CHECK(status.players.size() == expected.players.size() && status.players.size() == 2 &&
              status.maxClients == expected.maxClients);
        C_NetMessage written;
        expected.Write(&written);
        CHECK(query.responseSize == (r.msg.BitPosition() + 7) / 8 + 1 &&
              r.msg.BitPosition() == 2 + 7 * 8 + 16 + 3 + 1 + 12 + 1 + 16 + 1 + 2 + written.BitPosition());
    }
    operator delete(query.response);
    g_game->m_missionTime = 0;
    g_game = nullptr;
    g_currentMap = nullptr;
    g_testMp.maxClients = 0;
    g_testMp.gameType = MP_GAME_OBJECTIVES;
}

struct T_Connect {
    DWORD size = 0x20;
    const S_JoinRequest *request;
    DWORD requestSize = sizeof(S_JoinRequest);
    uint8_t *reply = nullptr;
    DWORD replySize = 0;
    void *replyContext = nullptr;
    void *playerContext = reinterpret_cast<void *>(0x77);
    IDirectPlay8Address *player;
};
S_JoinRequest JoinRequest(const char *password, const char *name)
{
    S_JoinRequest request{};
    std::strcpy(request.password, password);
    std::strcpy(request.name, name);
    request.versionMajor = g_versionMajor;
    request.versionMinor = g_versionMinor;
    return request;
}
// The reply's byte (freed), -1: none.
int Reply(T_Connect &c)
{
    if (!c.reply || c.replySize != 1)
        return -1;
    const int value = *c.reply;
    operator delete(c.reply);
    c.reply = nullptr;
    return value;
}

void TestServerIndicateConnect()
{
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    g_testMp.maxClients = 4;
    g_testMp.password = C_str();
    g_testMp.unknown174 = 0;
    net.m_transport->m_session = 0;
    T_Address *address = new T_Address;
    g_addressHost = L"10.1.2.3";
    S_JoinRequest join = JoinRequest("", "Dan");
    T_Connect c;
    c.request = &join;
    c.player = address;
    // A slot: the first free one (no connection, not left) reset; the address; the reply its slot (and the session
    // bit); no key check.
    net.m_players.push_back(Player(5, "a"));
    net.m_players.push_back(Player(0, ""));
    net.m_players[1].left = true;
    net.m_players.push_back(Player(0, ""));
    net.m_players[2].admin = true;
    net.m_players[2].keyQuery = 7;
    net.m_players.push_back(Player(0, ""));
    CHECK(Server(0xFFFF000E, &c) == 0 && Reply(c) == 2 && c.playerContext == reinterpret_cast<void *>(2));
    CHECK(net.m_players[2].connection == -1 && !net.m_players[2].admin && net.m_players[2].keyQuery == -1 &&
          net.m_players[2].address == 0x0302010A && net.m_players[1].connection == 0 && net.m_players.size() == 4);
    // The session's bit at 0x80; none free: appended.
    net.m_transport->m_session = 1;
    net.m_players[3].connection = 9;
    c.playerContext = nullptr;
    CHECK(Server(0xFFFF000E, &c) == 0 && Reply(c) == (0x80 | 4) && c.playerContext == reinterpret_cast<void *>(4) &&
          net.m_players.size() == 5 && net.m_players[4].connection == -1);
    net.m_transport->m_session = 0;
    // Full: the room (counted players) or 32 slots: 3.
    g_testMp.maxClients = net.CountPlayers();
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == 3);
    g_testMp.maxClients = 64;
    while (net.m_players.size() < 32)
        net.m_players.push_back(Player(100 + static_cast<int>(net.m_players.size()), "x"));
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == 3 && net.m_players.size() == 32);
    net.m_players.resize(5);
    // The version, the request's size, its field +0x1C: 7.
    join.versionMinor = g_versionMinor + 1;
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == 7);
    join.versionMinor = g_versionMinor;
    join.versionMajor = g_versionMajor + 1;
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == 7);
    join.versionMajor = g_versionMajor;
    join.unknown1C = 1;
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == 7);
    join.unknown1C = 2;
    c.requestSize = sizeof(S_JoinRequest) - 1;
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == 7);
    c.requestSize = sizeof(S_JoinRequest);
    c.request = nullptr;
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == 7);
    c.request = &join;
    // The password: 4 (none set: any).
    g_testMp.password = C_str("secret");
    std::strcpy(join.password, "Secret");
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == 4);
    std::strcpy(join.password, "secret");
    net.m_players.resize(4);
    CHECK(Server(0xFFFF000E, &c) == 0 && Reply(c) == 4);
    g_testMp.password = C_str();
    address->Release();
}

void TestServerKeyChecks()
{
    // GameSpy's listing: a check of that address and name (the name made unique): good -> kept (the slot's);
    // in use -> 6; none -> 5; the slot cleared when refused.
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    g_testMp.maxClients = 10;
    g_testMp.password = C_str();
    g_testMp.unknown174 = 1;
    net.m_transport->m_session = 0;
    T_Address *address = new T_Address;
    g_addressHost = L"10.1.2.3";
    S_JoinRequest join = JoinRequest("", "Eve");
    T_Connect c;
    c.request = &join;
    c.player = address;
    net.m_players.push_back(Player(5, "Eve"));
    for (S_KeyQuery &q : net.m_keyQueries)
        q = S_KeyQuery();
    // None: 5.
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == 5 && net.m_players.size() == 2 &&
          net.m_players[1].connection == 0 && net.m_players[1].name[0] == 0);
    // Another address's, another name's, then a pending one of this name; then the good one: kept.
    S_KeyQuery &other = net.m_keyQueries[3];
    other.address = 0x0402010A;
    std::strcpy(other.name, "Eve2");
    other.state = KEY_GOOD;
    S_KeyQuery &wrongName = net.m_keyQueries[5];
    wrongName.address = 0x0302010A;
    std::strcpy(wrongName.name, "Eve");
    wrongName.state = KEY_GOOD;
    S_KeyQuery &pending = net.m_keyQueries[8];
    pending.address = 0x0302010A;
    std::strcpy(pending.name, "Eve2");
    pending.state = KEY_ASKING;
    S_KeyQuery &good = net.m_keyQueries[40];
    good.address = 0x0302010A;
    std::strcpy(good.name, "Eve2");
    good.state = KEY_GOOD;
    CHECK(Server(0xFFFF000E, &c) == 0 && Reply(c) == 1 && c.playerContext == reinterpret_cast<void *>(1));
    CHECK(good.state == KEY_KEPT && net.m_players[1].keyQuery == 40 && pending.state == KEY_ASKING &&
          other.state == KEY_GOOD && wrongName.state == KEY_GOOD && net.m_players[1].connection == -1);
    CHECK(std::strcmp(join.name, "Eve") == 0);
    // Kept already (not good): 5; in use: 6 (the first of that name decides when in use).
    net.m_players.resize(1);
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == 5);
    good.state = KEY_IN_USE;
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == 6 && net.m_players[1].connection == 0 &&
          net.m_players[1].name[0] == 0);
    // The last check (99) counts.
    good.state = KEY_FREE;
    S_KeyQuery &last = net.m_keyQueries[99];
    last.address = 0x0302010A;
    std::strcpy(last.name, "Eve2");
    last.state = KEY_GOOD;
    CHECK(Server(0xFFFF000E, &c) == 0 && Reply(c) == 1 && net.m_players[1].keyQuery == 99);
    // A name not taken: as sent.
    net.m_players[0].name[0] = 'X';
    net.m_players.resize(1);
    S_KeyQuery &plain = net.m_keyQueries[0];
    plain.address = 0x0302010A;
    std::strcpy(plain.name, "Eve");
    plain.state = KEY_GOOD;
    CHECK(Server(0xFFFF000E, &c) == 0 && Reply(c) == 1 && net.m_players[1].keyQuery == 0);
    g_testMp.unknown174 = 0;
    for (S_KeyQuery &q : net.m_keyQueries)
        q = S_KeyQuery();
    address->Release();
}

void TestServerOthers()
{
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    net.m_players.push_back(Player(5, "a"));
    net.m_players.push_back(Player(-1, "b"));
    net.m_players[1].link = new C_NetConnection(1, 0, &net.m_transport->m_socket);
    net.m_transport->Add(net.m_players[1].link);
    // An aborted connection: the slot cleared, its link removed (and deleted).
    DWORD aborted[2] = {8, 1};
    CHECK(Server(0xFFFF000F, aborted) == 0 && net.m_players[1].connection == 0 && net.m_players[1].name[0] == 0 &&
          net.m_transport->m_connections.empty());
    net.m_players[1].link = nullptr;
    CHECK(Server(0xFFFF000F, aborted) == 0);
    // Data: queued as received from the slot (0x80); none: nothing.
    uint8_t data[3] = {1, 2, 3};
    struct {
        DWORD size;
        DPNID sender;
        void *context;
        void *data;
        DWORD dataSize;
    } receive = {0x20, 9, reinterpret_cast<void *>(3), data, 3};
    CHECK(Server(0xFFFF0011, &receive) == 0 && net.m_received.size() == 1 && net.m_received.front().flags == 0x83 &&
          net.m_received.front().size == 3 && std::memcmp(net.m_received.front().data, data, 3) == 0);
    receive.dataSize = 0;
    CHECK(Server(0xFFFF0011, &receive) == 0 && net.m_received.size() == 1);
    // A buffer returned: freed.
    DPNMSG_RETURN_BUFFER returned = {sizeof returned, 0, operator new(16), nullptr};
    CHECK(Server(0xFFFF0013, &returned) == 0);
    // The server's info: the local player's connection (a server).
    g_localPlayerSlot = 1;
    DWORD info[2] = {8, 0x4321};
    CHECK(Server(0xFFFF0015, info) == 0 && net.m_players[1].connection == 0x4321 && net.m_players[0].connection == 5);
    g_gameRole = GAME_ROLE_CLIENT;
    info[1] = 0x99;
    CHECK(Server(0xFFFF0015, info) == 0 && net.m_players[1].connection == 0x4321);
    // Others: 0.
    CHECK(Server(0xFFFF0001, info) == 0);
    g_localPlayerSlot = 0;
}

// ---- (added after the first mutation run) ---------------------------------------------------------------------------

void TestMessagesEdges()
{
    // One-letter names count as named (the join data's count and list, the teams).
    C_NetGame &net = MessageNet(GAME_ROLE_SERVER);
    g_testMp.gameType = MP_GAME_OBJECTIVES;
    net.m_players.push_back(Player(5, "A"));
    net.m_players.push_back(Player(6, "B"));
    net.m_players[1].team = 1;
    GetGameApp()->SendGameMessage(0, 0, 0, 0, false);
    T_Reader join = Flush(0, 0, 0, 0);
    CHECK(join.Game(0) && join.Bits(32) == GetGameApp()->m_randomId &&
          join.Bits(32) == static_cast<uint32_t>(net.m_unknown448));
    const auto defaultTeam = static_cast<uint32_t>(S_NetPlayerState().team);
    CHECK(join.Bits(16) && join.Bits(8) == 2 && join.Str() == "A" && join.Bits(2) == defaultTeam && join.Bits(5) == 0 &&
          join.Str() == "B");
    GetGameApp()->SendGameMessage(0xC, 0, 0, 0, false);
    T_Reader teams = Flush(0, 0, 0, 0);
    CHECK(teams.Game(0xC) && teams.Bits(6) == 0 && teams.Bits(2) == defaultTeam && teams.Bits(6) == 1 &&
          teams.Bits(2) == 1 && teams.Bits(6) == 0x21);
    // Sent: no longer pending (the game messages', the commands').
    CHECK(net.m_messages[0].m_pending == 0);
    GetGameApp()->PostCommand(2, 0, 0, false);
    net.m_unreliable.m_pending = 1;
    FlushCommands();                                // (a server's commands: the unreliable stream)
    CHECK(net.m_unreliable.m_pending == 0 && net.m_unreliable.BitPosition() == 5 + 32 + 32);
    net.m_messages[0].m_pending = 1;
    GetGameApp()->SendGameMessage(0x17, 0, 0, 0, false);
    Flush(0, 0, 0, 0);
    CHECK(net.m_messages[0].m_pending == 0);

    // A direction of length squared 1.5 (exactly): normalised; a tiny one (1e-4): normalised or signed.
    net.m_players.assign(1, Player(5, "a"));
    net.m_players[0].human = MessageHuman(1, S_vector(0, 0, 0), S_vector(1, 0.5f, 0.5f));
    GetGameApp()->SendGameMessage(5, 0, 0, 0, false);
    T_Reader r = Flush(0, 0, 0, 0);
    CHECK(r.Game(5) && r.Int() == 1 && r.Bits(32) == 0 && r.Bits(32) == 0 && r.Bits(32) == 0);
    const float k = 1.0f / std::sqrt(1.5f);
    CHECK(r.Float() == 1 * k && r.Float() == 0.5f * k && r.Float() == k * 0.5f);
    net.m_players[0].human = MessageHuman(1, S_vector(0, 0, 0), S_vector(1e-4f, 0, 0));
    GetGameApp()->SendGameMessage(5, 0, 0, 0, false);
    T_Reader tiny = Flush(0, 0, 0, 0);
    CHECK(tiny.Game(5) && tiny.Int() == 1 && tiny.Bits(32) == 0 && tiny.Bits(32) == 0 && tiny.Bits(32) == 0);
    const float sq = (0.0f + 0.0f) + 1e-4f * 1e-4f;
    CHECK(tiny.Float() == (sq >= 1e-8f ? 1e-4f * (1.0f / std::sqrt(sq)) : 1.0f));

    // A player without a human: zeros (Fix, part 10d-vi).
    net.m_players.assign(2, Player(5, "a"));
    net.m_players[0].human = MessageHuman(1, S_vector(4, 5, 6), S_vector(0, 0, 1));
    GetGameApp()->SendGameMessage(5, 0, 0, 0, false);
    T_Reader none = Flush(0, 0, 0, 0);
    CHECK(none.Game(5) && none.Int() == 2);
    for (int i = 0; i < 6; ++i)
        CHECK(none.Bits(32) == 0);
    CHECK(none.Float() == 4 && none.Float() == 5 && none.Float() == 6);

    // The first player gone (slot 0).
    net.m_players.assign(2, Player(5, "a"));
    net.m_players[1].connection = 6;
    net.m_players[0].link = new C_NetConnection(0, 0, &net.m_transport->m_socket);
    net.m_transport->Add(net.m_players[0].link);
    net.m_players[0].keyQuery = 2;
    DWORD destroy[2] = {8, 5};
    CHECK(Server(0xFFFF0009, destroy) == 0 && net.m_players[0].link == nullptr && net.m_players[0].keyQuery == -1 &&
          net.m_transport->m_connections.empty());

    // A one-letter password; the first slot free; the session bit with a key check.
    g_testMp.maxClients = 10;
    g_testMp.password = C_str("x");
    g_testMp.unknown174 = 0;
    T_Address *address = new T_Address;
    g_addressHost = L"10.1.2.3";
    S_JoinRequest join2 = JoinRequest("y", "Fay");
    T_Connect c;
    c.request = &join2;
    c.player = address;
    net.m_players.assign(1, Player(0, ""));
    net.m_players.push_back(Player(5, "a"));
    CHECK(Server(0xFFFF000E, &c) == -1 && Reply(c) == NET_JOIN_BAD_PASSWORD);
    std::strcpy(join2.password, "x");
    CHECK(Server(0xFFFF000E, &c) == 0 && Reply(c) == 0 && net.m_players[0].connection == -1 &&
          net.m_players.size() == 2);
    g_testMp.password = C_str();
    g_testMp.unknown174 = 1;
    net.m_transport->m_session = 1;
    net.m_players.resize(1);
    S_KeyQuery &good = net.m_keyQueries[1];
    good.address = 0x0302010A;
    std::strcpy(good.name, "Fay");
    good.state = KEY_GOOD;
    CHECK(Server(0xFFFF000E, &c) == 0 && Reply(c) == (0x80 | 1) && net.m_players[1].keyQuery == 1);
    net.m_transport->m_session = 0;
    g_testMp.unknown174 = 0;
    good = S_KeyQuery();
    address->Release();
}

void RunNetMessagesTests()
{
    TestSoldierState();
    TestBeginGameMessage();
    TestSendGameMessageQueue();
    TestSendGameMessageSimple();
    TestSendGameMessageJoin();
    TestSendGameMessagePlayers();
    TestSendGameMessagePlaces();
    TestNormalisedSigns();
    TestSendGameMessageSpawn();
    TestSendGameMessageFullState();
    TestPostCommand();
    TestPostCommandSoldier();
    TestServerCreatePlayer();
    TestServerDestroyPlayer();
    TestServerEnumQuery();
    TestServerIndicateConnect();
    TestServerKeyChecks();
    TestServerOthers();
    TestMessagesEdges();
}
