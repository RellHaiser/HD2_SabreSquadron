// Net part 11a (ng_diff.cpp): the world, the state compared, the calls, main.
namespace {

// ---- the world ----
int g_slots;
alignas(16) uint8_t g_mp[0x400];
uint32_t g_pBuf[6][4096], g_oBuf[6][4096];
const int O_STREAMS[6] = {0x208, 0x244, 0x280, 0x2BC, 0x2F8, 0x1A0};
C_NetMessage *PStream(int k)
{
    C_NetGame &net = PNet();
    return k < 3 ? &net.m_messages[k] : k == 3 ? &net.m_unreliable : k == 4 ? &net.m_direct : &net.m_replicationMessage;
}
void *OStream(int k) { return ONet() + O_STREAMS[k]; }
int PStreamIndex(const void *p)
{
    for (int k = 0; k < 6; ++k)
        if (p == PStream(k))
            return k;
    return -1;
}
int OStreamIndex(const void *p)
{
    for (int k = 0; k < 6; ++k)
        if (p == OStream(k))
            return k;
    return -1;
}
uint32_t OPosition(void *o) { return OF<uint32_t>(o, 8) * 32 + OF<uint32_t>(o, 0xC); }
void ResetStream(int k)
{
    std::memset(g_pBuf[k], 0, sizeof g_pBuf[k]);
    std::memset(g_oBuf[k], 0, sizeof g_oBuf[k]);
    C_NetMessage *p = PStream(k);
    p->Attach(g_pBuf[k], 4096 * 32);
    p->ResetPosition();
    void *o = OStream(k);
    OF<uint32_t *>(o, 4) = g_oBuf[k];
    OF<uint32_t>(o, 8) = 0;
    OF<uint32_t>(o, 0xC) = 0;
    OF<uint32_t>(o, 0x10) = 4096 * 32;
}
std::string StreamBits(const uint32_t *words, uint32_t bits)
{
    std::string s = N(bits) + ":";
    for (uint32_t w = 0; w < (bits + 31) / 32; ++w)
        s += Hex(w == bits / 32 && (bits & 31) ? words[w] & ((1u << (bits & 31)) - 1) : words[w]);
    return s;
}

// The queues (the port's std::list, the original's MSVC6 list: +4 the head, +8 the size; a node: next, prev, the value).
void OListClear(int at)
{
    uint8_t *head = OF<uint8_t *>(ONet(), at + 4);
    for (uint8_t *n = OF<uint8_t *>(head, 0); n != head;) {
        uint8_t *next = OF<uint8_t *>(n, 0);
        std::free(OF<void *>(n, 0x14));
        ODelete(n);
        n = next;
    }
    OF<void *>(head, 0) = head;
    OF<void *>(head, 4) = head;
    OF<int>(ONet(), at + 8) = 0;
}
void OListPush(int at, int time, uint32_t flags, int size, void *data)
{
    uint8_t *head = OF<uint8_t *>(ONet(), at + 4);
    uint8_t *node = static_cast<uint8_t *>(ONew(0x18));
    uint8_t *last = OF<uint8_t *>(head, 4);
    OF<void *>(node, 0) = head;
    OF<void *>(node, 4) = last;
    OF<int>(node, 8) = time;
    OF<uint32_t>(node, 0xC) = flags;
    OF<int>(node, 0x10) = size;
    OF<void *>(node, 0x14) = data;
    OF<void *>(last, 0) = node;
    OF<void *>(head, 4) = node;
    ++OF<int>(ONet(), at + 8);
}
std::string OListText(int at)
{
    std::string s = N(OF<int>(ONet(), at + 8)) + "[";
    uint8_t *head = OF<uint8_t *>(ONet(), at + 4);
    for (uint8_t *n = OF<uint8_t *>(head, 0); n != head; n = OF<uint8_t *>(n, 0))
        s += N(OF<int>(n, 8)) + "," + Hex(OF<uint32_t>(n, 0xC)) + "," + Bytes(OF<void *>(n, 0x14), OF<int>(n, 0x10)) + " ";
    return s + "]";
}
std::string PListText(const std::list<S_NetQueued> &l)
{
    std::string s = N(static_cast<long long>(l.size())) + "[";
    for (const S_NetQueued &q : l)
        s += N(q.time) + "," + Hex(q.flags) + "," + Bytes(q.data, q.size) + " ";
    return s + "]";
}
void ClearQueues()
{
    C_NetGame &net = PNet();
    for (std::list<S_NetQueued> *l : {&net.m_received, &net.m_outgoing}) {
        for (const S_NetQueued &q : *l)
            std::free(q.data);
        l->clear();
    }
    OListClear(0x1F0);
    OListClear(0x1FC);
}

// A message's bits: game commands (type 0x14, an id, a byte), module messages (their type, a byte), the end (0x13).
std::vector<uint8_t> MakeMessageBytes()
{
    uint32_t words[64] = {};
    C_BitStream s(words, 64 * 32);
    if (P(0.15)) {
        // (32 module messages: 13 words exactly.)
        for (int k = 0; k < 32; ++k) {
            s.WriteBits(static_cast<uint32_t>(I(0, 18)), 5);
            s.WriteBits(static_cast<uint32_t>(I(0, 255)), 8);
        }
        return std::vector<uint8_t>(reinterpret_cast<uint8_t *>(words), reinterpret_cast<uint8_t *>(words) + 52);
    }
    for (int k = I(0, 4); k > 0; --k) {
        const int kind = I(0, 4);
        if (kind == 0) {
            s.WriteBits(0x14, 5);
            s.WriteBits(static_cast<uint32_t>(I(0, 30)), 32);
            s.WriteBits(static_cast<uint32_t>(I(0, 255)), 8);
        } else if (kind == 4 && P(0.5)) {
            s.WriteBits(0x13, 5);
        } else {
            s.WriteBits(static_cast<uint32_t>(I(0, 20)), 5);
            s.WriteBits(static_cast<uint32_t>(I(0, 255)), 8);
        }
    }
    if (P(0.7))
        s.WriteBits(0x13, 5);
    const int bytes = static_cast<int>((s.BitPosition() + 7) / 8);
    std::vector<uint8_t> out(reinterpret_cast<uint8_t *>(words), reinterpret_cast<uint8_t *>(words) + std::max(bytes, 1));
    return out;
}

// The humans (each side's; their controllers' IsSpectating).
void SetHumans()
{
    for (int i = 0; i < HUMANS; ++i) {
        g_script.spectating[i] = P(0.3);
        C_Human *ph = PHuman(i);
        ph->m_controller = reinterpret_cast<C_HumanController *>(g_pControllers[i]);
        OF<void *>(g_oHumans[i], 0xB0) = g_oControllers[i];
    }
}

void SetWorld()
{
    C_NetGame &net = PNet();
    static const uint32_t roles[] = {7, 1, 2, 3, 0, 5, 0x12, 3};
    g_gameRole = roles[I(0, 7)];
    G<uint32_t>(O_ROLE) = g_gameRole;
    g_slots = I(1, 10);
    g_localPlayerSlot = I(0, g_slots - 1);
    G<int>(O_LOCAL_SLOT) = g_localPlayerSlot;
    g_script.game = P(0.9);
    g_script.missionLoaded = P(0.8);
    reinterpret_cast<C_game *>(g_pGame)->m_missionLoaded = g_script.missionLoaded;
    OF<uint8_t>(g_oGame, 0x14) = g_script.missionLoaded;
    g_script.now = static_cast<uint32_t>(I(0, 1000000));
    g_script.clockNow = I(0, 1000000);
    g_script.perfNow = I(0, 1000000);
    g_script.timerFires = P(0.6);
    g_script.playerCount = I(0, 12);
    g_script.dispatch = P(0.7);
    for (int &f : g_script.figures)
        f = P(0.1) ? I(-3, -1) : I(0, 3000);
    g_script.replicationWrote = P(0.5);
    g_script.objects.clear();
    for (int i = -1; i < 12; ++i)
        g_script.objects[i] = P(0.7);
    g_script.linkById = P(0.8) ? I(0, LINKS - 1) : -1;
    g_script.destroyResult = P(0.8) ? 0 : 1;
    g_script.voiceChat = P(0.5);
    net.m_unknown8 = P(0.5);
    OF<uint8_t>(ONet(), 8) = net.m_unknown8;
    g_options.voiceChat = g_script.voiceChat;
    SetHumans();

    // The rules (dedicated: the console's commands run; the listing: the queries answered, GameSpy stopped).
    std::memset(g_mp, 0, sizeof g_mp);
    S_MultiplayerSettings &mp = *reinterpret_cast<S_MultiplayerSettings *>(g_mp);
    mp.dedicated = P(0.3);
    mp.unknown174 = P(0.5) ? 1 : 0;

    // The connections' fields.
    for (int i = 0; i < LINKS; ++i) {
        g_linkPort[i] = P(0.2) ? -1 : I(1000, 9000);
        g_linkRoundTrip[i] = P(0.1) ? I(500, 3000) : I(0, 400);
        g_linkLastReceive[i] = static_cast<int>(g_script.perfNow) - (P(0.1) ? I(5000, 5001) : P(0.2) ? I(5001, 9000) : I(0, 5000));
        auto *pl = reinterpret_cast<C_NetConnection *>(g_pLinks[i]);
        pl->m_port = g_linkPort[i];
        pl->m_roundTrip = static_cast<uint32_t>(g_linkRoundTrip[i]);
        pl->m_lastReceive = g_linkLastReceive[i];
        OF<int>(g_oLinks[i], 0x10) = g_linkPort[i];
        OF<uint32_t>(g_oLinks[i], 0x4C) = static_cast<uint32_t>(g_linkRoundTrip[i]);
        OF<int>(g_oLinks[i], 0x40) = g_linkLastReceive[i];
    }
    net.m_serverLink = P(0.8) ? reinterpret_cast<C_NetConnection *>(g_pLinks[LINKS - 1]) : nullptr;
    OF<void *>(ONet(), 0x334) = net.m_serverLink ? g_oLinks[LINKS - 1] : nullptr;

    // The players.
    net.m_players.assign(static_cast<size_t>(g_slots), S_NetPlayerState());
    // (The original's vector: its own block, as its code grows it.)
    ODelete(OPlayers());
    uint8_t *op = static_cast<uint8_t *>(ONew(static_cast<size_t>(g_slots) * 0xC4));
    OF<uint8_t *>(ONet(), 0x1E0) = op;
    OF<uint8_t *>(ONet(), 0x1E4) = op + g_slots * 0xC4;
    OF<uint8_t *>(ONet(), 0x1E8) = op + g_slots * 0xC4;
    for (int s = 0; s < g_slots; ++s) {
        S_NetPlayerState &p = net.m_players[static_cast<size_t>(s)];
        uint8_t *raw = reinterpret_cast<uint8_t *>(&p);
        for (int k = 0; k < 0xC4; ++k)
            raw[k] = static_cast<uint8_t>(I(0, 255));
        p.connection = P(0.25) ? 0 : I(1, 60);
        std::memset(p.name, 0, sizeof p.name);
        if (P(0.8))
            std::snprintf(p.name, sizeof p.name, "P%d", s);
        else if (P(0.5)) {
            std::snprintf(p.name, sizeof p.name, "xP%d", s);
            p.name[0] = 0;
        }
        const int h = P(0.8) ? I(0, HUMANS - 1) : -1;
        p.human = PHuman(h);
        p.link = P(0.8) ? reinterpret_cast<C_NetConnection *>(g_pLinks[s]) : nullptr;
        p.joinStage = I(0, 3);
        p.highPings = I(0, 3);
        p.activityTime = g_script.now - static_cast<uint32_t>(P(0.1) ? I(299999, 300000) : P(0.3) ? I(300000, 400000) : I(0, 299999));
        for (int &v : p.stats.values)
            v = I(-20, 100);
        p.voiceBuffer = nullptr;
        uint8_t *o = op + s * 0xC4;
        std::memcpy(o, raw, 0xC4);
        OF<void *>(o, 0x3C) = h < 0 ? nullptr : g_oHumans[h];
        OF<void *>(o, 0x6C) = p.link ? g_oLinks[s] : nullptr;
    }

    // The net game's fields.
    auto set = [&](auto &field, int at, auto value) {
        field = value;
        std::memcpy(ONet() + at, &field, sizeof field);
    };
    set(net.m_sendPeriod, 0x5054, P(0.5) ? 50 : I(1, 200));
    set(net.m_sendTime, 0x5058, I(-10, 300));
    set(net.m_joinStatus, 0x5044, P(0.4) ? NET_JOIN_JOINING : I(0, 9));
    set(net.m_joinTime, 0x5048, P(0.3) ? I(29000, 31000) : I(0, 30000));
    set(net.m_unknown448, 0x448, I(0, 100));
    set(net.m_vote.kind, 0x4D0, P(0.4) ? 0x42 : I(0, 3));
    set(net.m_vote.voted, 0x4D4, P(0.5));
    set(net.m_vote.time, 0x4D8, P(0.1) ? I(15000, 15001) : P(0.3) ? I(14000, 16000) : I(0, 15000));
    set(net.m_vote.yes, 0x4DC, I(0, 6));
    set(net.m_vote.arg, 0x4E0, I(0, 50));
    set(net.m_keySocket, 0x4D3C, static_cast<uintptr_t>(P(0.5) ? -1 : I(3, 300)));
    set(net.m_enumHandle, 0x503C, static_cast<uint32_t>(P(0.5) ? 0 : I(1, 99)));
    set(net.m_reporting, 0x4DF8, P(0.5));
    set(net.m_unknown505C, 0x505C, P(0.5) ? 0 : I(1, 99));
    {
        // The LAN browser (a pointer: its presence), the transport's callback, the session's text.
        static uint8_t pBrowser[4], oBrowser[4];
        const bool browser = P(0.5);
        net.m_serverBrowser = browser ? reinterpret_cast<S_ServerBrowser *>(pBrowser) : nullptr;
        OF<void *>(ONet(), 0x4E4) = browser ? oBrowser : nullptr;
        const bool callback = P(0.5);
        net.m_transport->m_callback = callback ? TransportCallback : nullptr;
        OF<uint32_t>(ONet(), 0x414) = callback ? O_TRANSPORT_CALLBACK : 0;
        const std::string text = P(0.5) ? "" : "S" + N(I(0, 99));
        net.m_string502C = text;
        OSetString(0x502C, text);
    }
    for (int t = 0; t < 3; ++t) {
        net.m_teamScores[t] = static_cast<float>(I(-5, 50));
        OF<float>(ONet(), 0x44C + 4 * t) = net.m_teamScores[t];
        for (int k = 0; k < 9; ++k) {
            net.m_teamStats[t].values[k] = I(-20, 100);
            OF<int>(ONet(), 0x464 + 0x24 * t + 4 * k) = net.m_teamStats[t].values[k];
        }
    }
    for (int i = 0; i < 7; ++i) {
        set(net.m_bandwidthTotal[i], 0x4FB4 + 4 * i, I(0, 9000));
        set(net.m_bandwidthAverage[i], 0x4FD0 + 4 * i, I(0, 9000));
        set(net.m_bandwidthLast[i], 0x4FEC + 4 * i, I(0, 9000));
    }
    // DirectPlay, the voice, the replication.
    const bool server = P(0.7), client = P(0.6);
    set(net.m_server, 0, server ? reinterpret_cast<IDirectPlay8Server *>(&g_serverObject) : nullptr);
    set(net.m_client, 4, client ? reinterpret_cast<IDirectPlay8Client *>(&g_clientObject) : nullptr);
    const bool voiceClient = P(0.5), voiceServer = P(0.5);
    net.m_voiceClient = voiceClient ? reinterpret_cast<C_NetVoice *>(g_pVoices[0]) : nullptr;
    OF<void *>(ONet(), 0xC) = voiceClient ? g_oVoices[0] : nullptr;
    net.m_voiceServer = voiceServer ? reinterpret_cast<C_NetVoice *>(g_pVoices[1]) : nullptr;
    OF<void *>(ONet(), 0x10) = voiceServer ? g_oVoices[1] : nullptr;
    const bool unknown = P(0.3);
    net.m_unknown4EE0 = unknown ? reinterpret_cast<C_NetUnknownObject *>(g_pUnknown) : nullptr;
    OF<void *>(ONet(), 0x4EE0) = unknown ? g_oUnknown : nullptr;
    const bool replication = P(0.85);
    g_netReplication = replication ? reinterpret_cast<C_NetReplication *>(g_pReplication) : nullptr;
    G<void *>(O_REPLICATION) = replication ? g_oReplication : nullptr;
    const int objects = I(0, 2);
    auto *pr = reinterpret_cast<C_NetReplication *>(g_pReplication);
    pr->m_objects.assign(static_cast<size_t>(objects), reinterpret_cast<C_NetObject *>(g_pObjects[0]));
    static void *oObjects[4];
    OF<void **>(g_oReplication, 0x20) = objects ? oObjects : nullptr;
    OF<void **>(g_oReplication, 0x24) = objects ? oObjects + objects : nullptr;
    // The query thread, the limits.
    g_netQueryThread = P(0.3) ? g_queryThread : nullptr;
    G<void *>(O_QUERY_THREAD) = g_netQueryThread;
    g_netMaxPing = P(0.5) ? 0 : I(100, 600);
    G<int>(O_MAX_PING) = g_netMaxPing;
    g_netMaxInactivity = P(0.3) ? 0 : 300000;
    G<int>(O_MAX_INACTIVITY) = g_netMaxInactivity;
    g_graphFlags = static_cast<unsigned>(I(0, 0xFFFF));
    G<unsigned>(O_GRAPH_FLAGS) = g_graphFlags;
    // The console's commands.
    g_script.console.clear();
    for (int k = I(0, 2); k > 0; --k)
        g_script.console.push_back("kick P" + N(I(0, 9)));
    // The scene's frames.
    g_script.bounds.clear();
    for (int k = I(0, 5); k > 0; --k)
        for (int c = 0; c < 6; ++c)
            g_script.bounds.push_back(c < 3 ? R(-500.0f, 0.0f) : R(0.0f, 500.0f));
    ClearQueues();
    for (int k = 0; k < 6; ++k)
        ResetStream(k);
}

// Slots a call appends from a state constructed on the stack (Init: all; SetPlayer: those it adds): the bytes the
// original's constructor leaves as the stack had them are not compared in that call, then the port's take the
// original's.
int g_freshFrom = 1 << 30;
bool Indeterminate(int k)
{
    return (k >= 4 && k < 8) || (k >= 0x78 && k < 0x7C) || (k >= 0x80 && k < 0xA1) || k == 0xA3 || (k >= 0xA8 && k < 0xAC) ||
           (k >= 0xC0 && k < 0xC4);
}
void CarryFreshSlots()
{
    for (int i = g_freshFrom; i < OPlayerCount() && i < static_cast<int>(PNet().m_players.size()); ++i) {
        auto *a = reinterpret_cast<uint8_t *>(&PNet().m_players[static_cast<size_t>(i)]);
        const uint8_t *b = OPlayers() + i * 0xC4;
        for (int k = 0; k < 0xC4; ++k)
            if (Indeterminate(k))
                a[k] = b[k];
    }
    g_freshFrom = 1 << 30;
}

// The state compared.
void CompareState()
{
    C_NetGame &net = PNet();
    std::string s;
    if (OPlayerCount() != static_cast<int>(net.m_players.size()))
        s += "the players' count " + N(OPlayerCount()) + " / " + N(static_cast<long long>(net.m_players.size())) + "; ";
    for (int i = 0; i < OPlayerCount() && i < static_cast<int>(net.m_players.size()) && s.empty(); ++i) {
        const uint8_t *a = reinterpret_cast<const uint8_t *>(&net.m_players[static_cast<size_t>(i)]);
        uint8_t *b = OPlayers() + i * 0xC4;
        if (std::strcmp(reinterpret_cast<const char *>(a + 8), reinterpret_cast<const char *>(b + 8)) != 0)
            s += "slot " + N(i) + " name; ";
        for (int k = 0; k < 0xC4; ++k) {
            if ((k >= 0x3C && k < 0x40) || (k >= 0x6C && k < 0x70) || (k >= 8 && k < 0x3C) || (i >= g_freshFrom && Indeterminate(k)))
                continue;
            if (a[k] != b[k]) {
                s += "slot " + N(i) + " byte +" + N(k) + " " + N(b[k]) + " / " + N(a[k]) + "; ";
                break;
            }
        }
        if (PHumanIndex(net.m_players[static_cast<size_t>(i)].human) != OHumanIndex(OF<void *>(b, 0x3C)))
            s += "slot " + N(i) + " human; ";
        if (PLinkIndex(net.m_players[static_cast<size_t>(i)].link) != OLinkIndex(OF<void *>(b, 0x6C)))
            s += "slot " + N(i) + " link; ";
    }
    auto field = [&](const char *name, const void *p, int at, size_t size) {
        if (std::memcmp(p, ONet() + at, size) != 0)
            s += std::string(name) + "; ";
    };
    field("the send period", &net.m_sendPeriod, 0x5054, 4);
    field("the send time", &net.m_sendTime, 0x5058, 4);
    field("the join status", &net.m_joinStatus, 0x5044, 4);
    field("the join time", &net.m_joinTime, 0x5048, 4);
    field("+448", &net.m_unknown448, 0x448, 4);
    field("+505C", &net.m_unknown505C, 0x505C, 4);
    field("+5060", &net.m_unknown5060, 0x5060, 1);
    field("+8", &net.m_unknown8, 8, 1);
    field("the close pending", &net.m_closePending, 0x5070, 1);
    field("reporting", &net.m_reporting, 0x4DF8, 1);
    field("the key socket", &net.m_keySocket, 0x4D3C, 4);
    field("the locks made", &net.m_locksMade, 0x5018, 1);
    field("the enum handle", &net.m_enumHandle, 0x503C, 4);
    field("the vote", &net.m_vote.kind, 0x4D0, 4);
    field("the vote's voted", &net.m_vote.voted, 0x4D4, 1);
    field("the vote's time", &net.m_vote.time, 0x4D8, 4);
    field("the vote's yes", &net.m_vote.yes, 0x4DC, 4);
    field("the vote's argument", &net.m_vote.arg, 0x4E0, 4);
    field("the team scores", net.m_teamScores, 0x44C, 12);
    field("the team stats", net.m_teamStats, 0x464, 0x24 * 3);
    field("the level's min", net.m_levelMin, 0x4EA0, 12);   // (w: the original's stack's)
    field("the level's size", net.m_levelSize, 0x4EB0, 12);
    if ((net.m_serverBrowser != nullptr) != (OF<void *>(ONet(), 0x4E4) != nullptr))
        s += "the browser; ";
    if ((net.m_transport->m_callback == TransportCallback) != (OF<uint32_t>(ONet(), 0x414) == O_TRANSPORT_CALLBACK) ||
        (net.m_transport->m_callback == nullptr) != (OF<uint32_t>(ONet(), 0x414) == 0))
        s += "the transport's callback; ";
    if (net.m_string502C != OGetString(0x502C))
        s += "+502C \"" + OGetString(0x502C) + "\" / \"" + net.m_string502C + "\"; ";
    field("the level's bits", net.m_levelBits, 0x4EC0, 12);
    field("the level's steps", net.m_levelSteps, 0x4ECC, 12);
    field("the bandwidth total", net.m_bandwidthTotal, 0x4FB4, 28);
    field("the bandwidth average", net.m_bandwidthAverage, 0x4FD0, 28);
    field("the bandwidth last", net.m_bandwidthLast, 0x4FEC, 28);
    field("the received history", net.m_receivedHistory, 0x4EE4, 100);
    field("the sent history", net.m_sentHistory, 0x4F48, 100);
    field("the history index", &net.m_historyIndex, 0x4FAC, 4);
    field("the history index 2", &net.m_historyIndex2, 0x4FB0, 4);
    if ((net.m_server != nullptr) != (OF<void *>(ONet(), 0) != nullptr) || (net.m_client != nullptr) != (OF<void *>(ONet(), 4) != nullptr))
        s += "DirectPlay's objects; ";
    if ((net.m_voiceClient != nullptr) != (OF<void *>(ONet(), 0xC) != nullptr) ||
        (net.m_voiceServer != nullptr) != (OF<void *>(ONet(), 0x10) != nullptr))
        s += "the voices; ";
    if ((net.m_unknown4EE0 != nullptr) != (OF<void *>(ONet(), 0x4EE0) != nullptr))
        s += "+4EE0; ";
    if (PLinkIndex(net.m_serverLink) != OLinkIndex(OF<void *>(ONet(), 0x334)))
        s += "the server link; ";
    if ((g_netReplication != nullptr) != (G<void *>(O_REPLICATION) != nullptr))
        s += "the replication; ";
    if (g_gameRole != G<uint32_t>(O_ROLE) || g_localPlayerSlot != G<int>(O_LOCAL_SLOT))
        s += "the role / local slot " + N(G<uint32_t>(O_ROLE)) + " " + N(G<int>(O_LOCAL_SLOT)) + " / " + N(g_gameRole) + " " + N(g_localPlayerSlot) + "; ";
    if (g_netMaxPing != G<int>(O_MAX_PING) || g_netMaxFreq != G<int>(O_MAX_FREQ) || g_netMaxInactivity != G<int>(O_MAX_INACTIVITY))
        s += "the limits; ";
    if (std::memcmp(g_scanMin, reinterpret_cast<void *>(static_cast<uintptr_t>(O_SCAN_MIN)), 12) != 0 ||
        std::memcmp(g_scanMax, reinterpret_cast<void *>(static_cast<uintptr_t>(O_SCAN_MAX)), 12) != 0)
        s += "the scan's bounds; ";
    if ((g_netQueryThread != nullptr) != (G<void *>(O_QUERY_THREAD) != nullptr))
        s += "the query thread; ";
    const std::string pr = PListText(net.m_received), orr = OListText(0x1F0), po = PListText(net.m_outgoing), oo = OListText(0x1FC);
    if (pr != orr)
        s += "received " + orr.substr(0, 300) + " / " + pr.substr(0, 300) + "; ";
    if (po != oo)
        s += "outgoing " + oo.substr(0, 300) + " / " + po.substr(0, 300) + "; ";
    for (int k = 0; k < 6; ++k) {
        C_NetMessage *p = PStream(k);
        void *o = OStream(k);
        if (p->BitPosition() != OPosition(o) || p->m_unreliable != (OF<uint8_t>(o, 0x18) != 0) || p->m_pending != OF<int>(o, 0x1C) ||
            p->m_channel != OF<int>(o, 0x20) || p->m_target != OF<int>(o, 0x24) || p->m_type != OF<uint32_t>(o, 0x28) ||
            std::memcmp(g_pBuf[k], g_oBuf[k], (p->BitPosition() + 31) / 32 * 4) != 0)
            s += "stream " + N(k) + " (" + StreamBits(g_oBuf[k], OPosition(o)) + " / " + StreamBits(g_pBuf[k], p->BitPosition()) + "); ";
    }
    Check(s.empty(), s);
}

template <class A, class B> void Run(const std::string &what, A port, B orig)
{
    g_line = what;
    g_trace += " " + what;
    g_log[0].clear();
    g_log[1].clear();
    g_faking = true;
    g_side = 0;
    const std::vector<std::string> console = g_script.console;   // (each side pops its own)
    const long long x = port();
    g_side = 1;
    g_script.console = console;
    const long long y = orig();
    g_side = 0;
    g_faking = false;
    Check(x == y, "the result " + N(y) + " / " + N(x));
    CompareLogs();
    CompareState();
    CarryFreshSlots();
}

#define O_CALL(addr, type) reinterpret_cast<type>(static_cast<uintptr_t>(addr))
using FT0 = void(FAST *)(void *);

void Call()
{
    C_NetGame &net = PNet();
    void *on = ONet();
    const int op = I(0, 35);
    switch (op) {
    case 0: {
        const int kind = I(0, 3), arg = I(0, 99);
        Run("Vote Start " + N(kind) + " " + N(arg), [&] { net.m_vote.Start(kind, arg); return 0; },
            [&] { O_CALL(0x0072dd70, void(THISCALL *)(void *, int, int))(static_cast<uint8_t *>(on) + 0x4D0, kind, arg); return 0; });
        break;
    }
    case 1: {
        if (!g_script.game)
            return;
        const int kind = I(0, 3), arg = I(0, 99);
        Run("Vote Begin " + N(kind) + " " + N(arg), [&] { net.m_vote.Begin(kind, arg); return 0; },
            [&] { O_CALL(0x0072ddb0, void(THISCALL *)(void *, int, int))(static_cast<uint8_t *>(on) + 0x4D0, kind, arg); return 0; });
        break;
    }
    case 2:
        Run("Vote", [&] { return net.m_vote.Vote(); },
            [&] { return O_CALL(0x0072dfb0, bool(FAST *)(void *))(static_cast<uint8_t *>(on) + 0x4D0); });
        break;
    case 3:
        if (net.m_vote.kind != 0x42 && (net.m_vote.kind < 0 || net.m_vote.kind > 3))
            return;
        Run("CountYes", [&] { net.m_vote.CountYes(); return 0; },
            [&] { O_CALL(0x0072dfe0, FT0)(static_cast<uint8_t *>(on) + 0x4D0); return 0; });
        break;
    case 4: {
        if (!g_script.game)
            return;
        const bool passed = P(0.5);
        Run("PrintVoteResult " + N(passed), [&] { PrintVoteResult(passed); return 0; },
            [&] { O_CALL(0x0072de90, void(__stdcall *)(bool))(passed); return 0; });
        const int kind = I(0, 3), arg = I(0, 99);
        Run("PostVote", [&] { PostVote(kind, arg); return 0; }, [&] { O_CALL(0x0072dd50, void(__stdcall *)(int, int))(kind, arg); return 0; });
        break;
    }
    case 5: {
        const int delta = P(0.15) ? 15000 + I(0, 1) - net.m_vote.time : P(0.3) ? I(10000, 20000) : I(0, 1000);
        Run("UpdateVote " + N(delta), [&] { net.UpdateVote(delta); return 0; },
            [&] { O_CALL(0x0072e0a0, void(THISCALL *)(void *, int))(static_cast<uint8_t *>(on) + 0x4D0, delta); return 0; });
        break;
    }
    case 6:
    case 7: {
        // A stream begun and written to.
        const int kind = I(0, 2), target = P(0.5) ? 0 : I(1, 60), type = I(0, 31), channel = I(0, 4);
        const bool reliable = P(0.5);
        Run("Begin " + N(kind) + " " + N(target) + " " + N(type) + " " + N(reliable) + " " + N(channel),
            [&] {
                C_BitStream *m = kind == 0 ? net.CreateMessage(type, channel) : kind == 1 ? net.CreateMessageEx(type, channel)
                                                                                        : net.BeginMessage(target, type, reliable, channel);
                return static_cast<long long>(PStreamIndex(m));
            },
            [&] {
                void *m = kind == 0 ? O_CALL(0x0072c280, void *(THISCALL *)(void *, int, int))(on, type, channel)
                          : kind == 1 ? O_CALL(0x0072c2a0, void *(THISCALL *)(void *, int, int))(on, type, channel)
                                      : O_CALL(0x0072c2c0, void *(THISCALL *)(void *, int, int, bool, int))(on, target, type, reliable, channel);
                return static_cast<long long>(OStreamIndex(m));
            });
        const int k = I(0, 4);
        const uint32_t v = static_cast<uint32_t>(rng());
        PStream(k)->WriteBits(v, 13);
        reinterpret_cast<void(THISCALL *)(void *, uint32_t, int)>(0x0073d320)(OStream(k), v, 13);
        break;
    }
    case 8: {
        const int k = I(0, 4);
        Run("SendNetMessage " + N(k), [&] { net.SendNetMessage(PStream(k)); return 0; },
            [&] { O_CALL(0x0072c360, void(THISCALL *)(void *, void *))(on, OStream(k)); return 0; });
        break;
    }
    case 9: {
        const std::vector<uint8_t> bytes = MakeMessageBytes();
        const uint32_t flags = static_cast<uint32_t>(I(0, 0xFF));
        const int which = I(0, 2);
        Run("Queue " + N(which) + " " + Hex(flags),
            [&] {
                if (which == 0)
                    net.QueueReceived(bytes.data(), static_cast<int>(bytes.size()), flags);
                else if (which == 1)
                    net.QueueOutgoing(bytes.data(), static_cast<int>(bytes.size()), flags);
                else
                    TransportCallback(I(0, 3), const_cast<uint8_t *>(bytes.data()), static_cast<int>(bytes.size()), static_cast<int>(flags >> 5),
                                      flags & 0x1F);
                return 0;
            },
            [&] {
                if (which == 0)
                    O_CALL(0x0072c540, void(THISCALL *)(void *, const void *, int, uint32_t))(on, bytes.data(), static_cast<int>(bytes.size()), flags);
                else if (which == 1)
                    O_CALL(0x0072c600, void(THISCALL *)(void *, const void *, int, uint32_t))(on, bytes.data(), static_cast<int>(bytes.size()), flags);
                else
                    O_CALL(0x0072c730, void(FAST *)(int, void *, int, int, uint32_t))(0, const_cast<uint8_t *>(bytes.data()),
                                                                                  static_cast<int>(bytes.size()), static_cast<int>(flags >> 5),
                                                                                  flags & 0x1F);
                return 0;
            });
        break;
    }
    case 10: {
        const std::vector<uint8_t> bytes = MakeMessageBytes();
        const int slot = P(0.3) ? -1 : I(0, g_slots);
        const uint32_t flags = static_cast<uint32_t>(I(0, 0xFF));
        Run("SendToPlayers " + N(slot) + " " + Hex(flags), [&] { net.SendToPlayers(slot, bytes.data(), static_cast<int>(bytes.size()), flags); return 0; },
            [&] {
                O_CALL(0x0072d4b0, void(THISCALL *)(void *, int, const void *, int, uint32_t))(on, slot, bytes.data(), static_cast<int>(bytes.size()),
                                                                                                flags);
                return 0;
            });
        break;
    }
    case 11:
    case 12: {
        // The queues read (received before the update, outgoing after): messages, the replication's data.
        for (int k = I(0, 4); k > 0; --k) {
            const std::vector<uint8_t> bytes = MakeMessageBytes();
            const uint32_t flags = P(0.3) ? 0x20 | static_cast<uint32_t>(I(0, 3)) : 0;
            const bool received = P(0.5);
            void *pd = std::calloc(bytes.size() + 4, 1), *od = std::calloc(bytes.size() + 4, 1);
            std::memcpy(pd, bytes.data(), bytes.size());
            std::memcpy(od, bytes.data(), bytes.size());
            const int time = I(0, 99);
            (received ? net.m_received : net.m_outgoing).push_back({time, flags, static_cast<int>(bytes.size()), pd});
            OListPush(received ? 0x1F0 : 0x1FC, time, flags, static_cast<int>(bytes.size()), od);
        }
        const bool before = P(0.5);
        Run("ReceiveMessages " + N(before), [&] { net.ReceiveMessages(0, before); return 0; },
            [&] { O_CALL(0x0072c780, void(THISCALL *)(void *, int, bool))(on, 0, before); return 0; });
        break;
    }
    case 13: {
        uint32_t words[64] = {};
        C_BitStream s(words, 64 * 32);
        const std::vector<uint8_t> bytes = MakeMessageBytes();
        std::memcpy(words, bytes.data(), bytes.size());
        alignas(16) uint8_t pms[sizeof(C_NetMessage)];
        C_NetMessage *pm = new (pms) C_NetMessage();
        uint32_t pw[64], ow[64];
        std::memcpy(pw, words, sizeof pw);
        std::memcpy(ow, words, sizeof ow);
        pm->Attach(pw, 64 * 32);
        alignas(16) uint8_t oms[0x40];
        O_CALL(O_MSG_CTOR, void *(THISCALL *)(void *))(oms);
        OF<uint32_t *>(oms, 4) = ow;
        OF<uint32_t>(oms, 0x10) = 64 * 32;
        const bool toClient = P(0.5);
        Run("DispatchNetMessage " + N(toClient), [&] { return C_NetGame::DispatchNetMessage(pm, toClient); },
            [&] { return O_CALL(0x0072c480, bool(__stdcall *)(void *, bool))(oms, toClient); });
        pm->Attach(nullptr, 0);
        pm->~C_NetMessage();
        OF<uint32_t *>(oms, 4) = nullptr;
        break;
    }
    case 14:
    case 15:
    case 16: {
        // Some streams with data first.
        for (int k = 0; k < 4; ++k)
            if (P(0.4)) {
                const uint32_t v = static_cast<uint32_t>(rng());
                PStream(k)->WriteBits(v, 11);
                reinterpret_cast<void(THISCALL *)(void *, uint32_t, int)>(0x0073d320)(OStream(k), v, 11);
            }
        const int delta = P(0.2) ? I(100, 300) : I(0, 80);
        const bool replicate = P(0.6);
        Run("Process " + N(delta) + " " + N(replicate), [&] { net.Process(delta, replicate); return 0; },
            [&] { O_CALL(0x0072c9a0, void(THISCALL *)(void *, int, bool))(on, delta, replicate); return 0; });
        break;
    }
    case 17:
        Run("UpdateLags", [&] { net.UpdateLags(0); return 0; }, [&] { O_CALL(0x0072cde0, void(THISCALL *)(void *, int))(on, 0); return 0; });
        break;
    case 18:
        Run("SendJoinData", [&] { net.SendJoinData(); return 0; }, [&] { O_CALL(0x0072d120, FT0)(on); return 0; });
        break;
    case 19:
    case 20: {
        const int delta = I(0, 150);
        Run("SendMessages " + N(delta), [&] { net.SendMessages(delta); return 0; },
            [&] { O_CALL(0x0072d240, void(THISCALL *)(void *, int))(on, delta); return 0; });
        break;
    }
    case 21: {
        // A kick by name (a player's, another), by slot, the level scan.
        const int command = I(0, 3);
        char name[0x40];
        std::snprintf(name, sizeof name, "P%d", I(0, 11));
        if (P(0.1))
            name[0] = 0;
        const char *arg = command == 0 ? (P(0.1) ? nullptr : name) : command == 1 ? reinterpret_cast<const char *>(static_cast<intptr_t>(I(-1, g_slots + 1))) : nullptr;
        if (command == 3 && !g_script.game)
            return;
        if (command <= 1 && !net.m_server)
            return;   // (both kicks call the server unchecked: the dedicated server's console has one)
        Run("Command " + N(command), [&] { return net.Command(command, arg, 0); },
            [&] { return O_CALL(0x0072d680, bool(THISCALL *)(void *, int, const char *, int))(on, command, arg, 0); });
        break;
    }
    case 35: {
        // The constructor (a second net game each side: the fields it sets).
        static uint8_t *oFresh;
        static C_NetGame *pFresh;
        if (!oFresh) {
            oFresh = static_cast<uint8_t *>(ONew(0x5080));
            pFresh = static_cast<C_NetGame *>(operator new(sizeof(C_NetGame)));
        }
        std::memset(oFresh, 0x6B, 0x5080);
        g_gameRole = 3;
        G<uint32_t>(O_ROLE) = 3;
        Run("constructor", [&] { new (pFresh) C_NetGame(); return 0; }, [&] { O_CALL(0x0072e1d0, void *(FAST *)(void *))(oFresh); return 0; });
        std::string d;
        if (std::memcmp(pFresh->m_bandwidthTotal, oFresh + 0x4FB4, 3 * 28) != 0)
            d += "the bandwidth; ";
        if (std::memcmp(pFresh->m_receivedHistory, oFresh + 0x4EE4, 2 * 100) != 0 || pFresh->m_historyIndex != OF<int>(oFresh, 0x4FAC) ||
            pFresh->m_historyIndex2 != OF<int>(oFresh, 0x4FB0))
            d += "the history; ";
        if (std::memcmp(pFresh->m_serverEntry, oFresh + 0x4E00, 0x9C) != 0)
            d += "the server entry; ";
        Check(d.empty(), "the constructed " + d);
        break;
    }
    case 22: {
        // Metres: whole, hundredths (the centimetres' rounding), any.
        const int kind = I(0, 3);
        const float range = kind == 0 ? static_cast<float>(I(0, 3000)) : kind == 1 ? static_cast<float>(I(0, 300000)) / 100.0f
                            : kind == 2 ? static_cast<float>(1 << I(0, 20)) / 100.0f : R(0.0f, 70000.0f);
        uint32_t steps[2] = {0xDEAD, 0xBEEF};
        Run("BitsForRange " + F(range), [&] { return BitsForRange(&steps[0], range); },
            [&] { return O_CALL(0x0072dcd0, int(THISCALL *)(uint32_t *, float))(&steps[1], range); });
        Check(steps[0] == steps[1], "the steps " + N(steps[1]) + " / " + N(steps[0]));
        break;
    }
    case 23:
        Run("ResetScores", [&] { net.ResetScores(); return 0; }, [&] { O_CALL(0x0072ebb0, FT0)(on); return 0; });
        break;
    case 24:
        Run("ResetPlayerStats", [&] { net.ResetPlayerStats(); return 0; }, [&] { O_CALL(0x0072ec60, FT0)(on); return 0; });
        break;
    case 25: {
        const int slot = P(0.7) ? I(0, g_slots - 1) : I(g_slots, g_slots + 4), connection = I(0, 60);
        char name[0x34];
        std::snprintf(name, sizeof name, "New%d", I(0, 9));
        g_freshFrom = static_cast<int>(net.m_players.size());
        Run("SetPlayer " + N(slot), [&] { net.SetPlayer(slot, connection, name); return 0; },
            [&] { O_CALL(0x0072ed10, void(THISCALL *)(void *, int, int, const char *))(on, slot, connection, name); return 0; });
        break;
    }
    case 26: {
        const int value = I(-5000, 5000);
        Run("SetVoiceValue " + N(value), [&] { net.SetVoiceValue(value); return 0; },
            [&] { O_CALL(0x0072edf0, void(THISCALL *)(void *, int))(on, value); return 0; });
        break;
    }
    case 27: {
        std::string text = P(0.2) ? std::string(static_cast<size_t>(I(95, 120)), 'z') : "hello there";
        const bool team = P(0.5);
        Run("SendChat", [&] { return net.SendChat(text.c_str(), team); },
            [&] { return O_CALL(0x0072ee60, bool(THISCALL *)(void *, const char *, bool))(on, text.c_str(), team); });
        break;
    }
    case 28: {
        char name[0x40];
        std::snprintf(name, sizeof name, "P%d", I(0, 11));
        if (P(0.1))
            name[0] = 0;
        Run("FindPlayerByName " + Str(name), [&] { return net.FindPlayerByName(name); },
            [&] { return O_CALL(0x0072eeb0, int(THISCALL *)(void *, const char *))(on, name); });
        const int slot = I(-2, g_slots + 2);
        Run("GetPlayerName " + N(slot),
            [&] {
                const char *n = net.GetPlayerName(slot);
                return n ? static_cast<long long>(n - reinterpret_cast<const char *>(net.m_players.data())) : -1LL;
            },
            [&] {
                const char *n = O_CALL(0x0072efa0, const char *(THISCALL *)(void *, int))(on, slot);
                return n ? static_cast<long long>(n - reinterpret_cast<const char *>(OPlayers())) : -1LL;
            });
        Run("GetPlayerHuman " + N(slot), [&] { return static_cast<long long>(PHumanIndex(net.GetPlayerHuman(slot))); },
            [&] { return static_cast<long long>(OHumanIndex(O_CALL(0x0072eff0, void *(THISCALL *)(void *, int))(on, slot))); });
        const int connection = P(0.7) && g_slots ? net.m_players[static_cast<size_t>(I(0, g_slots - 1))].connection : I(0, 60);
        Run("FindPlayer " + N(connection), [&] { return net.FindPlayer(connection); },
            [&] { return O_CALL(0x0072f060, int(THISCALL *)(void *, int))(on, connection); });
        break;
    }
    case 29: {
        Run("GetBestScore", [&] { return net.GetBestScore(); }, [&] { return O_CALL(0x0072f110, int(FAST *)(void *))(on); });
        const int slot = I(-1, g_slots + 1), points = I(-10, 10);
        Run("AddScore " + N(slot) + " " + N(points), [&] { net.AddScore(slot, points); return 0; },
            [&] { O_CALL(0x0072f1a0, void(THISCALL *)(void *, int, int))(on, slot, points); return 0; });
        break;
    }
    case 30:
        Run("LockPlayers",
            [&] {
                const long long r = reinterpret_cast<uint8_t *>(net.LockPlayers()) == reinterpret_cast<uint8_t *>(net.m_players.data());
                net.UnlockPlayers();
                return r;
            },
            [&] {
                const long long r = O_CALL(0x0072f040, uint8_t *(THISCALL *)(void *))(on) == ONet() + 0x1DC;
                O_CALL(0x0072f100, void(THISCALL *)(void *))(on);
                return r;
            });
        break;
    case 31:
    case 32: {
        const bool now = P(0.8);
        Run("Close " + N(now), [&] { net.Close(now); return 0; },
            [&] { O_CALL(0x0072e8b0, void(THISCALL *)(void *, bool))(on, now); return 0; });
        break;
    }
    case 33:
        Run("Shutdown", [&] { net.Shutdown(); return 0; }, [&] { O_CALL(0x0072e830, FT0)(on); return 0; });
        break;
    default:
        g_freshFrom = 0;
        Run("Init", [&] { net.Init(); return 0; }, [&] { O_CALL(0x0072e570, FT0)(on); return 0; });
        break;
    }
}

void Round()
{
    g_trace.clear();
    SetWorld();
    CompareState();
    for (int k = I(1, 10); k > 0; --k)
        Call();
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p (the test's 0x20000000 base: %08x) in round %d, %s, %s's side\n   (ops:%s)\n",
                e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress) -
                                      reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)) + 0x20000000u),
                g_round, g_line.c_str(), g_side ? "the original" : "the port", g_trace.c_str());
    std::printf("  ebx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx\n", e->ContextRecord->Ebx, e->ContextRecord->Esi, e->ContextRecord->Edi,
                e->ContextRecord->Ebp, e->ContextRecord->Esp);
    std::fflush(stdout);
    ExitProcess(3);
}
LONG WINAPI FirstChance(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        OnCrash(e);
    return EXCEPTION_CONTINUE_SEARCH;
}

template <class M> int VSlot(M member)
{
    uint32_t raw;
    std::memcpy(&raw, &member, 4);
    return (raw & 1) ? static_cast<int>((raw - 1) / 4) : -1;
}

// The original's import slot of a system function (the import table scanned for its real address).
int RedirectImport(const void *real, const void *to)
{
    if (!real)
        return 0;
    const auto *base = reinterpret_cast<const uint8_t *>(0x400000);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int n = 0;
    for (auto *imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); imp->Name; ++imp)
        for (auto *t = reinterpret_cast<IMAGE_THUNK_DATA32 *>(const_cast<uint8_t *>(base) + imp->FirstThunk); t->u1.Function; ++t)
            if (t->u1.Function == static_cast<DWORD>(reinterpret_cast<uintptr_t>(real))) {
                t->u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(to));
                ++n;
            }
    return n;
}
bool RedirectApi(const char *dll, const char *name, const void *to)
{
    HMODULE m = GetModuleHandleA(dll);
    if (!m)
        m = LoadLibraryA(dll);
    const int n = m ? RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, name)), to) : 0;
    if (!n)
        std::printf("the original does not import %s\n", name);
    return n > 0;
}

}   // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_NG_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("ng_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);

    // The shared objects' vtables.
    g_serverVtable[0x14 / 4] = reinterpret_cast<void *>(&FakeServerCancel);
    g_serverVtable[0x5C / 4] = reinterpret_cast<void *>(&FakeServerClose);
    g_serverVtable[0x60 / 4] = reinterpret_cast<void *>(&FakeDestroyClient);
    g_serverVtable[2] = reinterpret_cast<void *>(&FakeServerRelease);
    g_clientVtable[0x18 / 4] = reinterpret_cast<void *>(&FakeClientCancel);
    g_clientVtable[0x38 / 4] = reinterpret_cast<void *>(&FakeClientClose);
    g_clientVtable[2] = reinterpret_cast<void *>(&FakeClientRelease);
    g_graphVtable[0x08 / 4] = reinterpret_cast<void *>(&FakeSetFlags);
    g_sectorVtable[0x44 / 4] = reinterpret_cast<void *>(&FakeEnumFrames);
    OF<void *>(g_scene, 0x1F8) = &g_sectorObject;
    reinterpret_cast<C_game *>(g_pGame)->m_scene = reinterpret_cast<I3D_scene *>(g_scene);
    OF<void *>(g_oGame, 0x18) = g_scene;
    // The port's: the replication's vtable, the controllers', the voices', the unknown object's.
    const int write = VSlot(&C_NetReplication::Write), read = VSlot(&C_NetReplication::Read), spect = VSlot(&C_HumanController::IsSpectating);
    if (write < 0 || read < 0 || spect < 0) {
        std::printf("ng_diff: a virtual's slot not found\n");
        return 1;
    }
    g_pReplicationVtable[write] = reinterpret_cast<void *>(&PRepWrite);
    g_pReplicationVtable[read] = reinterpret_cast<void *>(&PRepRead);
    *reinterpret_cast<void **>(g_pReplication) = g_pReplicationVtable;
    new (&reinterpret_cast<C_NetReplication *>(g_pReplication)->m_objects) std::vector<C_NetObject *>();
    g_pControllerVtable[spect] = reinterpret_cast<void *>(&PIsSpectating);
    for (int i = 0; i < HUMANS; ++i) {
        *reinterpret_cast<void **>(g_pControllers[i]) = g_pControllerVtable;
        OF<void *>(g_oControllers[i], 0) = g_oControllerVtable;
    }
    g_oControllerVtable[0x64 / 4] = reinterpret_cast<void *>(&OIsSpectating);
    g_pUnknownVtable[0x0C / 4] = reinterpret_cast<void *>(&PDestroy);
    *reinterpret_cast<void **>(g_pUnknown) = g_pUnknownVtable;
    g_oUnknownVtable[0x0C / 4] = reinterpret_cast<void *>(&ODestroy);
    OF<void *>(g_oUnknown, 0) = g_oUnknownVtable;
    // (The port's voices: C_NetVoice's own vtable: its destructor is the test's.)
    for (auto &v : g_pVoices)
        new (v) C_NetVoice(nullptr, nullptr);
    g_oVoiceVtable[0] = reinterpret_cast<void *>(&OVoiceDelete);
    for (auto &v : g_oVoices)
        OF<void *>(v, 0) = g_oVoiceVtable;
    g_oReplicationVtable[0x14 / 4] = reinterpret_cast<void *>(&ORepWrite);
    g_oReplicationVtable[0x18 / 4] = reinterpret_cast<void *>(&ORepRead);
    OF<void *>(g_oReplication, 0) = g_oReplicationVtable;
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_mpSettings = reinterpret_cast<S_MultiplayerSettings *>(g_mp);

    // The original's.
    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e884a, reinterpret_cast<const void *>(ORealloc));
    RedirectOriginal(0x007e42d9, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e4375, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e562e, reinterpret_cast<const void *>(OAtexit));
    RedirectOriginal(0x007e85af, reinterpret_cast<const void *>(FakeClock));
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x00746ca0, reinterpret_cast<const void *>(OTransportStop)},
        {0x00746cf0, reinterpret_cast<const void *>(OTransportDeleteConnections)},
        {0x00746d60, reinterpret_cast<const void *>(OFindById)},
        {0x00746e40, reinterpret_cast<const void *>(OTransportLock)},
        {0x00746e60, reinterpret_cast<const void *>(OTransportUnlock)},
        {0x00747260, reinterpret_cast<const void *>(OFlushAll)},
        {0x00746300, reinterpret_cast<const void *>(OSend)},
        {0x00746ac0, reinterpret_cast<const void *>(OSetResendTime)},
        {0x00749a70, reinterpret_cast<const void *>(OAllocBuffer)},
        {0x00749b20, reinterpret_cast<const void *>(OFreeBuffer)},
        {0x00748dc0, reinterpret_cast<const void *>(OGetPerfTimeMs)},
        {0x00748da0, reinterpret_cast<const void *>(OInitPerfTime)},
        {0x0073ee40, reinterpret_cast<const void *>(OPerfTimerCtor)},
        {0x0073ee60, reinterpret_cast<const void *>(OPerfTimerFire)},
        {0x0073f9c0, reinterpret_cast<const void *>(OFindObject)},
        {0x0073f970, reinterpret_cast<const void *>(ODeleteObjects)},
        {0x0073ed80, reinterpret_cast<const void *>(OObjectSetLag)},
        {0x006dcd10, reinterpret_cast<const void *>(OGetGameApp)},
        {0x0072a480, reinterpret_cast<const void *>(OSendGameMessage)},
        {0x0072b590, reinterpret_cast<const void *>(OPostCommand)},
        {0x00728820, reinterpret_cast<const void *>(OHandleClientCommand)},
        {0x00727bd0, reinterpret_cast<const void *>(OHandleServerCommand)},
        {0x006987d0, reinterpret_cast<const void *>(OGetGame)},
        {0x0069c240, reinterpret_cast<const void *>(OPrintMessage)},
        {0x0048dc70, reinterpret_cast<const void *>(ODispatchMessage)},
        {0x0048dc50, reinterpret_cast<const void *>(OCallUnknown28)},
        {0x0069f4b0, reinterpret_cast<const void *>(OGetConsole)},
        {0x00744c10, reinterpret_cast<const void *>(OPopCommand)},
        {0x0069f520, reinterpret_cast<const void *>(OGetCommandProcessor)},
        {0x00742850, reinterpret_cast<const void *>(OExecute)},
        {0x006e5380, reinterpret_cast<const void *>(OTextsGet)},
        {0x00730410, reinterpret_cast<const void *>(OCountPlayers)},
        {0x0072f6c0, reinterpret_cast<const void *>(OKickPlayer)},
        {0x00730130, reinterpret_cast<const void *>(OSendPlayerScore)},
        {0x0072f8d0, reinterpret_cast<const void *>(OSetLevelBounds)},
        {0x00724d30, reinterpret_cast<const void *>(OTakeBandwidth)},
        {0x00723dc0, reinterpret_cast<const void *>(OAnswerQueries)},
        {0x00732730, reinterpret_cast<const void *>(OConfigureFirewall)},
        {0x00724820, reinterpret_cast<const void *>(OStopVoiceClient)},
        {0x00724720, reinterpret_cast<const void *>(OStopVoiceServer)},
        {0x007235f0, reinterpret_cast<const void *>(ODeleteReplication)},
        {0x00738760, reinterpret_cast<const void *>(OStopGameSpyQueries)},
        {0x007252b0, reinterpret_cast<const void *>(OServerEntryReset)},
        {0x007252c0, reinterpret_cast<const void *>(OServerEntryReset)},
        {0x007425e0, reinterpret_cast<const void *>(OSetClientConfig)},
        {0x006b6a60, reinterpret_cast<const void *>(OGetDetailSettings)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    RedirectApi("kernel32.dll", "EnterCriticalSection", reinterpret_cast<const void *>(&FakeEnterCriticalSection));
    RedirectApi("kernel32.dll", "LeaveCriticalSection", reinterpret_cast<const void *>(&FakeLeaveCriticalSection));
    RedirectApi("kernel32.dll", "InitializeCriticalSection", reinterpret_cast<const void *>(&FakeInitializeCriticalSection));
    RedirectApi("kernel32.dll", "DeleteCriticalSection", reinterpret_cast<const void *>(&FakeDeleteCriticalSection));
    RedirectApi("kernel32.dll", "WaitForSingleObject", reinterpret_cast<const void *>(&FakeWaitForSingleObject));
    RedirectApi("kernel32.dll", "CloseHandle", reinterpret_cast<const void *>(&FakeCloseHandle));
    RedirectApi("winmm.dll", "timeGetTime", reinterpret_cast<const void *>(&FakeTimeGetTime));
    RedirectApi("ws2_32.dll", "closesocket", reinterpret_cast<const void *>(&FakeClosesocket)) ||
        RedirectApi("wsock32.dll", "closesocket", reinterpret_cast<const void *>(&FakeClosesocket));
    G<void *>(O_PROFILE + 0x24) = g_mp;
    G<void *>(0x0098c0fc) = g_oProcessor;
    std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(O_VOTABLE)), g_votableCommands, sizeof g_votableCommands);
    // (IGraph: the original's import of GetIGraph.)
    RedirectApi("LS3DF.dll", "GetIGraph", reinterpret_cast<const void *>(&OGetIGraph));
    // The original's net game: its constructor and Init (statics never run), the watched locks.
    g_watched[0][0] = reinterpret_cast<CRITICAL_SECTION *>(&g_netGame.m_playersLock);
    g_watched[0][1] = reinterpret_cast<CRITICAL_SECTION *>(&g_netGame.m_queueLock);
    g_watched[1][0] = reinterpret_cast<CRITICAL_SECTION *>(ONet() + 0x418);
    g_watched[1][1] = reinterpret_cast<CRITICAL_SECTION *>(ONet() + 0x430);
    O_CALL(0x0072e1d0, void *(FAST *)(void *))(ONet());
    O_CALL(0x0072e570, FT0)(ONet());
    PNet().Init();
    for (int k = 0; k < 6; ++k)
        ResetStream(k);

    int rounds = std::getenv("HD2_NG_QUICK") ? 5000 : 20000;
    if (const char *r = std::getenv("HD2_NG_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_NG_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("ng_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
