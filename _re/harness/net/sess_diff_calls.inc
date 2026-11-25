// Net part 11b (sess_diff.cpp): the world, the state compared, the calls, main.
namespace {

// ---- the world ----
int g_slots;
alignas(16) uint8_t g_mp[0x400];
alignas(16) uint8_t g_playerProfile[0x40];   // (both sides': the name's C_str at +0)
uint32_t g_pBuf[6][4096], g_oBuf[6][4096];
const int O_STREAMS[6] = {0x208, 0x244, 0x280, 0x2BC, 0x2F8, 0x1A0};
C_NetMessage *PStream(int k)
{
    C_NetGame &net = PNet();
    return k < 3 ? &net.m_messages[k] : k == 3 ? &net.m_unreliable : k == 4 ? &net.m_direct : &net.m_replicationMessage;
}
void *OStream(int k) { return ONet() + O_STREAMS[k]; }
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

// The original's MSVC6 strings (+4 the characters, a reference count before them; +8 the length; +0xC the room).
std::string OGetString(int at)
{
    const char *p = OF<const char *>(ONet(), at + 4);
    return p ? std::string(p, OF<size_t>(ONet(), at + 8)) : std::string();
}
void OSetString(int at, const std::string &text)
{
    char *&p = OF<char *>(ONet(), at + 4);
    if (p && p[-1] == 0)
        ODelete(p - 1);
    p = nullptr;
    OF<size_t>(ONet(), at + 8) = 0;
    OF<size_t>(ONet(), at + 0xC) = 0;
    if (text.empty())
        return;
    auto *buffer = static_cast<char *>(ONew(text.size() + 2));
    p = buffer + 1;
    std::memcpy(p, text.c_str(), text.size() + 1);
    OF<size_t>(ONet(), at + 8) = text.size();
    OF<size_t>(ONet(), at + 0xC) = text.size();
}

// A DirectPlay result: mostly success, else a failure or a positive success.
long HR() { return P(0.85) ? 0 : P(0.6) ? static_cast<long>(0x80004005) : 1; }
std::string RandomName(const char *prefix)
{
    const int kind = I(0, 9);
    if (kind == 0)
        return "";
    if (kind == 1)
        return std::string(static_cast<size_t>(I(40, 80)), 'n');
    if (kind == 2)
        return std::string(prefix) + "\xC3\xA9\xE4\xB8\xAD";   // (UTF-8: an accent, a CJK character)
    return prefix + N(I(0, 999));
}
// An entry of a server (the same bytes on both sides).
void RandomEntry(S_ServerEntry *e)
{
    auto *b = reinterpret_cast<uint8_t *>(e);
    for (size_t i = 0; i < sizeof *e; ++i)
        b[i] = static_cast<uint8_t>(I(0, 255));
    std::snprintf(e->name, sizeof e->name, "Srv%d", I(0, 99));
    std::snprintf(e->map, sizeof e->map, "Map%d", I(0, 99));
    e->address = P(0.8) ? static_cast<uint32_t>(I(0, 0x7FFFFFFF)) * 2u + static_cast<uint32_t>(I(0, 1)) : 0;
    e->port = P(0.2) ? I(-5, 70000) : I(1000, 9000);
    e->voice = P(0.5);
}

void SetWorld()
{
    C_NetGame &net = PNet();
    static const uint32_t roles[] = {7, 1, 2, 3, 0, 5, 0x12};
    g_gameRole = roles[I(0, 6)];
    G<uint32_t>(O_ROLE) = g_gameRole;
    g_slots = I(1, 10);
    g_localPlayerSlot = I(0, g_slots - 1);
    G<int>(O_LOCAL_SLOT) = g_localPlayerSlot;
    g_script.game = P(0.6);
    g_script.now = static_cast<uint32_t>(I(0, 1000000));
    g_script.perfNow = I(0, 1000000);
    g_script.linkById = P(0.8) ? I(0, LINKS - 1) : -1;
    g_script.voiceChat = P(0.5);
    g_options.voiceChat = g_script.voiceChat;
    g_script.createServer = HR();
    g_script.createClient = HR();
    g_script.createAddress = 0;   // (CreateAddresses uses its addresses unchecked: only Host's may fail)
    g_script.serverInit = HR();
    g_script.enumProviders = HR();
    g_script.setSP = HR();
    g_script.host = HR();
    g_script.clientInit = HR();
    static const long enumResults[] = {0, 0x0015800E, static_cast<long>(0x80158040), static_cast<long>(0x80158320), static_cast<long>(0x80004005), 1};
    g_script.enumHosts = enumResults[I(0, 5)];
    g_script.setClientInfo = HR();
    static const long connectResults[] = {0, static_cast<long>(0x8000000A), static_cast<long>(0x80004005), 0x0015800E};
    g_script.connect = connectResults[I(0, 3)];
    g_script.noDevices = P(0.1);
    g_script.failWithObject = P(0.3);
    {
        const int kind = I(0, 4);
        wchar_t text[64];
        if (kind == 0)
            std::swprintf(text, 64, L"%d.%d.%d.%d", I(0, 255), I(0, 255), I(0, 255), I(0, 255));
        else if (kind == 1)
            std::swprintf(text, 64, L"%d.%d", I(0, 255), I(0, 255));
        else if (kind == 2)
            std::swprintf(text, 64, L"host%d", I(0, 9));
        else
            std::swprintf(text, 64, L"%u.%u.%u.%u", static_cast<unsigned>(I(0, 999)), static_cast<unsigned>(I(0, 999)), I(0, 255), I(0, 255));
        g_script.hostname = text;
    }
    for (int i = 0; i < HUMANS; ++i) {
        g_script.teams[i] = I(0, 2);
        g_script.spectating[i] = P(0.3);
        PHuman(i)->m_controller = reinterpret_cast<C_HumanController *>(g_pControllers[i]);
        OF<void *>(g_oHumans[i], 0xB0) = g_oControllers[i];
    }
    // (The static objects a call may have destroyed: made again.)
    new (g_pReplication) C_NetReplication();
    for (auto &l : g_pLinks)
        new (l) C_NetConnection(0, 0, nullptr);
    for (auto &v : g_pVoices)
        new (v) C_NetVoice(nullptr, nullptr);
    {
        auto *r = reinterpret_cast<C_NetReplication *>(g_pReplication);
        r->m_bitsRead = I(0, 99999);
        r->m_bitsWritten = I(0, 99999);
        OF<int>(g_oReplication, 0xCC) = r->m_bitsRead;
        OF<int>(g_oReplication, 0xD0) = r->m_bitsWritten;
    }
    GetGameApp()->m_state = reinterpret_cast<C_GameState *>(g_pState);
    OF<void *>(g_oApp, 0x14) = g_oState;
    static const int keyStatuses[] = {-1, 0, NET_JOIN_TIMED_OUT, NET_JOIN_BAD_CD_KEY, NET_JOIN_JOINING, NET_JOIN_KEY_IN_USE};
    g_script.keyStatus = keyStatuses[I(0, 5)];
    g_script.createProcess = P(0.7);
    g_script.cjk = P(0.3);
    g_script.voiceBuffer = P(0.7);
    for (uint32_t &v : g_script.info)
        v = static_cast<uint32_t>(I(0, 100000));
    g_script.counters[0] = I(0, 9999);
    g_script.counters[1] = I(0, 9999);
    static const int actorTypes[] = {-1, ACTOR_HUMAN, ACTOR_CAR, ACTOR_DOOR, ACTOR_MINE};
    g_script.actorType = actorTypes[I(0, 4)];
    g_addressNext = 0;

    // The rules, the profile, the versions.
    std::memset(g_mp, 0, sizeof g_mp);
    S_MultiplayerSettings &mp = *reinterpret_cast<S_MultiplayerSettings *>(g_mp);
    mp.dedicated = P(0.3);
    mp.unknown174 = P(0.5) ? 1 : I(0, 2);
    mp.unknown178 = P(0.5) ? 0 : I(0, 9);
    mp.port = P(0.2) ? 0 : I(1000, 40000);
    static C_str passwords[3] = {C_str(""), C_str("pw"), C_str("a-long-password-of-more-than-twenty")};
    new (&mp.password) C_str(passwords[I(0, 2)]);
    std::memset(g_playerProfile, 0, sizeof g_playerProfile);
    // (45 characters at most: Connect's original has room for 46 wide ones on its stack.)
    new (g_playerProfile) C_str(RandomName("Pl").substr(0, 45).c_str());
    const bool unicode = P(0.3);
    reinterpret_cast<uint8_t *>(&g_gameProfile)[0x30] = unicode;
    G<uint8_t>(O_PROFILE + 0x30) = unicode;
    g_versionMajor = P(0.8) ? 1 : I(0, 3);
    g_versionMinor = I(0, 30);
    G<int>(O_VERSION_MAJOR) = g_versionMajor;
    G<int>(O_VERSION_MINOR) = g_versionMinor;
    g_graphFlags = static_cast<unsigned>(I(0, 0xFFFF));
    G<unsigned>(O_GRAPH_FLAGS) = g_graphFlags;

    // The connections' fields.
    for (int i = 0; i < LINKS; ++i) {
        g_linkPort[i] = P(0.2) ? -1 : I(1000, 9000);
        g_linkRoundTrip[i] = I(0, 400);
        g_linkLastReceive[i] = static_cast<int>(g_script.perfNow) - I(0, 5000);
        auto *pl = reinterpret_cast<C_NetConnection *>(g_pLinks[i]);
        pl->m_port = g_linkPort[i];
        pl->m_roundTrip = static_cast<uint32_t>(g_linkRoundTrip[i]);
        pl->m_lastReceive = g_linkLastReceive[i];
        pl->m_id = I(0, 40);
        OF<int>(g_oLinks[i], 0x10) = g_linkPort[i];
        OF<uint32_t>(g_oLinks[i], 0x4C) = static_cast<uint32_t>(g_linkRoundTrip[i]);
        OF<int>(g_oLinks[i], 0x40) = g_linkLastReceive[i];
        OF<int>(g_oLinks[i], 4) = pl->m_id;
    }
    net.m_serverLink = P(0.8) ? reinterpret_cast<C_NetConnection *>(g_pLinks[LINKS - 1]) : nullptr;
    OF<void *>(ONet(), 0x334) = net.m_serverLink ? g_oLinks[LINKS - 1] : nullptr;

    // The players.
    net.m_players.assign(static_cast<size_t>(g_slots), S_NetPlayerState());
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
        const int h = P(0.8) ? I(0, HUMANS - 1) : -1;
        p.human = PHuman(h);
        p.link = P(0.8) ? reinterpret_cast<C_NetConnection *>(g_pLinks[s]) : nullptr;
        p.joinStage = 1;
        p.highPings = 0;
        p.activityTime = g_script.now;
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
    set(net.m_joinStatus, 0x5044, I(0, 9));
    set(net.m_joinTime, 0x5048, I(0, 30000));
    set(net.m_unknown448, 0x448, I(0, 100));
    set(net.m_unknown8, 8, P(0.5));
    set(net.m_unknown338, 0x338, I(0, 1000));
    set(net.m_enumHandle, 0x503C, static_cast<uint32_t>(I(0, 99)));
    set(net.m_lastError, 0x504C, static_cast<long>(I(0, 9)));
    net.m_lastErrorText = nullptr;
    OF<void *>(ONet(), 0x5050) = nullptr;
    set(net.m_keySocket, 0x4D3C, static_cast<uintptr_t>(-1));
    set(net.m_vote.kind, 0x4D0, 0x42);
    set(net.m_reporting, 0x4DF8, false);
    {
        const bool search = P(0.8), status = P(0.8);
        net.m_searchCallback = search ? SearchCallback : nullptr;
        OF<void *>(ONet(), 0x4EC) = search ? reinterpret_cast<void *>(SearchCallback) : nullptr;
        net.m_statusCallback = status ? StatusCallback : nullptr;
        OF<void *>(ONet(), 0x4F0) = status ? reinterpret_cast<void *>(StatusCallback) : nullptr;
    }
    RandomEntry(reinterpret_cast<S_ServerEntry *>(net.m_serverEntry));
    std::memcpy(ONet() + 0x4E00, net.m_serverEntry, 0x9C);
    {
        const std::string text = P(0.5) ? "" : "S" + N(I(0, 99));
        net.m_string501C = text;
        OSetString(0x501C, text);
    }
    for (int i = 0; i < 7; ++i)
        set(net.m_bandwidthLast[i], 0x4FEC + 4 * i, I(0, 9000));
    // DirectPlay, the voice, the replication.
    const bool server = P(0.7), client = P(0.6);
    set(net.m_server, 0, server ? reinterpret_cast<IDirectPlay8Server *>(&g_serverObject) : nullptr);
    set(net.m_client, 4, client ? reinterpret_cast<IDirectPlay8Client *>(&g_clientObject) : nullptr);
    const bool voiceClient = P(0.6), voiceServer = P(0.5), voiceObject = P(0.9);
    auto *pv = reinterpret_cast<C_NetVoice *>(g_pVoices[0]);
    pv->m_client = voiceObject ? reinterpret_cast<IDirectPlayVoiceClient *>(&g_voiceClientObject) : nullptr;
    OF<void *>(g_oVoices[0], 4) = voiceObject ? &g_voiceClientObject : nullptr;
    net.m_voiceClient = voiceClient ? pv : nullptr;
    OF<void *>(ONet(), 0xC) = voiceClient ? g_oVoices[0] : nullptr;
    net.m_voiceServer = voiceServer ? reinterpret_cast<C_NetVoice *>(g_pVoices[1]) : nullptr;
    OF<void *>(ONet(), 0x10) = voiceServer ? g_oVoices[1] : nullptr;
    net.m_unknown4EE0 = nullptr;
    OF<void *>(ONet(), 0x4EE0) = nullptr;
    const bool replication = P(0.7);
    g_netReplication = replication ? reinterpret_cast<C_NetReplication *>(g_pReplication) : nullptr;
    G<void *>(O_REPLICATION) = replication ? g_oReplication : nullptr;
    g_netServerSide = P(0.5);
    G<uint8_t>(O_SERVER_SIDE) = g_netServerSide;
    // The status a query read (the rules' bytes; no players).
    net.m_status.players.clear();
    OF<uint8_t *>(ONet(), 0x4F4 + 8) = OF<uint8_t *>(ONet(), 0x4F4 + 4);
    for (int k = 0x10; k < 0x68; k += 4) {
        const int v = I(-3, 3000);
        std::memcpy(reinterpret_cast<uint8_t *>(&net.m_status) + k, &v, 4);
        OF<int>(ONet(), 0x4F4 + k) = v;
    }
    g_netQueryThread = nullptr;
    G<void *>(O_QUERY_THREAD) = nullptr;
    ClearQueues();
    for (int k = 0; k < 6; ++k)
        ResetStream(k);
}

// Slots a call appends from a state constructed on the stack: not compared in that call, then carried.
int g_freshFrom = 1 << 30;
bool Indeterminate(int k)
{
    return (k >= 4 && k < 8) || (k >= 0x78 && k < 0x7C) || (k >= 0x80 && k < 0xA1) || k == 0xA3 || (k >= 0xA8 && k < 0xAC) ||
           (k >= 0xC0 && k < 0xC4);
}

std::string OErrorText() { return OF<const char *>(ONet(), 0x5050) ? Str(OF<const char *>(ONet(), 0x5050)) : "null"; }
std::string ONameOfFactory(uint32_t f)
{
    return f == 0x0072d590 ? "factory" : f == 0x0072d5c0 ? "reader" : f ? "?" : "null";
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
    field("the join status", &net.m_joinStatus, 0x5044, 4);
    field("the join time", &net.m_joinTime, 0x5048, 4);
    field("+448", &net.m_unknown448, 0x448, 4);
    field("+8", &net.m_unknown8, 8, 1);
    field("+338", &net.m_unknown338, 0x338, 4);
    field("the enum handle", &net.m_enumHandle, 0x503C, 4);
    field("the last error", &net.m_lastError, 0x504C, 4);
    field("the server entry", net.m_serverEntry, 0x4E00, 0x9C);
    // (The vectors' w: the original's stack's.)
    for (int k = 0; k < 0x180; ++k)
        if ((k % 0x80 >= 0x50 || k % 16 < 12) && reinterpret_cast<const uint8_t *>(net.m_quantizations)[k] != ONet()[0x20 + k]) {
            s += "the quantisations at +" + Hex(static_cast<uint32_t>(0x20 + k)) + "; ";
            break;
        }
    field("the status's rules", reinterpret_cast<const uint8_t *>(&net.m_status) + 0x10, 0x4F4 + 0x10, 0x58);
    field("the close pending", &net.m_closePending, 0x5070, 1);
    field("reporting", &net.m_reporting, 0x4DF8, 1);
    const std::string pe = net.m_lastErrorText ? Str(net.m_lastErrorText) : "null";
    if (pe != OErrorText())
        s += "the error's text " + OErrorText() + " / " + pe + "; ";
    if (net.m_string501C != OGetString(0x501C))
        s += "+501C \"" + OGetString(0x501C) + "\" / \"" + net.m_string501C + "\"; ";
    if (reinterpret_cast<void *>(net.m_searchCallback) != OF<void *>(ONet(), 0x4EC) ||
        reinterpret_cast<void *>(net.m_statusCallback) != OF<void *>(ONet(), 0x4F0))
        s += "the callbacks; ";
    {
        const int saved = g_side;
        g_side = 0;
        const std::string ps = StatusText(&net.m_status);
        g_side = 1;
        const std::string os = StatusText(ONet() + 0x4F4);
        g_side = saved;
        if (ps != os)
            s += "the status " + os.substr(0, 200) + " / " + ps.substr(0, 200) + "; ";
    }
    if (Who(net.m_server) != Who(OF<void *>(ONet(), 0)) || Who(net.m_client) != Who(OF<void *>(ONet(), 4)))
        s += "DirectPlay's objects " + Who(OF<void *>(ONet(), 0)) + " " + Who(OF<void *>(ONet(), 4)) + " / " + Who(net.m_server) + " " +
             Who(net.m_client) + "; ";
    if (PVoiceName(net.m_voiceClient) != OVoiceName(OF<void *>(ONet(), 0xC)) || PVoiceName(net.m_voiceServer) != OVoiceName(OF<void *>(ONet(), 0x10)))
        s += "the voices " + OVoiceName(OF<void *>(ONet(), 0xC)) + " " + OVoiceName(OF<void *>(ONet(), 0x10)) + " / " + PVoiceName(net.m_voiceClient) +
             " " + PVoiceName(net.m_voiceServer) + "; ";
    const int pl = PLinkIndex(net.m_serverLink), ol = OLinkIndex(OF<void *>(ONet(), 0x334));
    if (pl != ol)
        s += "the server link " + N(ol) + " / " + N(pl) + "; ";
    else if (pl >= 0 && (net.m_serverLink->m_id != OF<int>(OF<void *>(ONet(), 0x334), 4) ||
                         (pl == 99 && net.m_serverLink->m_port != OF<int>(OF<void *>(ONet(), 0x334), 0x10))))
        s += "the server link's id / port " + N(OF<int>(OF<void *>(ONet(), 0x334), 4)) + " " + N(OF<int>(OF<void *>(ONet(), 0x334), 0x10)) + " / " +
             N(net.m_serverLink->m_id) + " " + N(net.m_serverLink->m_port) + "; ";
    void *orep = G<void *>(O_REPLICATION);
    if (PReplicationName(g_netReplication) != OReplicationName(orep))
        s += "the replication " + OReplicationName(orep) + " / " + PReplicationName(g_netReplication) + "; ";
    else if (g_netReplication && g_netReplication != reinterpret_cast<C_NetReplication *>(g_pReplication)) {
        const bool pf = g_netReplication->m_makeFactory != nullptr, pr = g_netReplication->m_makeStateReader != nullptr;
        if (g_netReplication->m_field2 != OF<int>(orep, 0xC0) || g_netReplication->m_local.m_id != OF<int>(orep, 0x4C) ||
            (pf ? "factory" : "null") != ONameOfFactory(OF<uint32_t>(orep, 0xE4)) || (pr ? "reader" : "null") != ONameOfFactory(OF<uint32_t>(orep, 0xE0)))
            s += "the replication's fields; ";
    }
    if (g_netServerSide != (G<uint8_t>(O_SERVER_SIDE) != 0))
        s += "the server side; ";
    if (g_gameRole != G<uint32_t>(O_ROLE) || g_localPlayerSlot != G<int>(O_LOCAL_SLOT))
        s += "the role / local slot " + N(G<uint32_t>(O_ROLE)) + " " + N(G<int>(O_LOCAL_SLOT)) + " / " + N(g_gameRole) + " " + N(g_localPlayerSlot) + "; ";
    const std::string pr = PListText(net.m_received), orr = OListText(0x1F0), po = PListText(net.m_outgoing), oo = OListText(0x1FC);
    if (pr != orr)
        s += "received " + orr.substr(0, 300) + " / " + pr.substr(0, 300) + "; ";
    if (po != oo)
        s += "outgoing " + oo.substr(0, 300) + " / " + po.substr(0, 300) + "; ";
    for (int k = 0; k < 6; ++k) {
        C_NetMessage *p = PStream(k);
        void *o = OStream(k);
        if (p->BitPosition() != OPosition(o) || std::memcmp(g_pBuf[k], g_oBuf[k], (p->BitPosition() + 31) / 32 * 4) != 0)
            s += "stream " + N(k) + "; ";
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
    const int addressNext = g_addressNext;   // (each side's addresses from the same one)
    const long long x = port();
    g_side = 1;
    g_addressNext = addressNext;
    const long long y = orig();
    g_side = 0;
    g_faking = false;
    Check(x == y, "the result " + N(y) + " / " + N(x));
    CompareLogs();
    CompareState();
    g_freshFrom = 1 << 30;
}

#define O_CALL(addr, type) reinterpret_cast<type>(static_cast<uintptr_t>(addr))
using FT0 = void(FAST *)(void *);

// A text in each encoding the conversions take: ASCII, Latin-1 (the ANSI page), UTF-8, wide.
std::string RandomNarrow()
{
    static const char *texts[] = {"", "abc", "Caf\xE9", "\xC3\xA9t\xC3\xA9", "\xE4\xB8\xAD\xE6\x96\x87", "a.b.c", "0123456789012345678901234567890123456789"};
    return texts[I(0, 6)];
}
std::wstring RandomWide()
{
    static const wchar_t *texts[] = {L"", L"abc", L"Café", L"中文", L"x€y", L"0123456789012345678901234567890123456789"};
    return texts[I(0, 5)];
}

// A host's answer's data (old or newer layout, the kind 0 search, 1 status, other).
std::vector<uint8_t> MakeResponse(int kind, bool newer)
{
    uint32_t words[256] = {};
    C_BitStream s(words, 256 * 32);
    s.WriteBits(static_cast<uint32_t>(kind), 2);
    char map[24];
    std::snprintf(map, sizeof map, "map%d", I(0, 99));
    s.WriteString(map);
    if (!newer) {
        s.WriteBits(static_cast<uint32_t>(I(0, 3)), 2);
        s.WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
        s.WriteBits(static_cast<uint32_t>(I(0, 63)), 6);
        s.WriteBits(static_cast<uint32_t>(I(0, 63)), 6);
        s.WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
        // (A status follows only data the reader stays aligned with: this layout's version, 1.0 .. 1.20.)
        const bool old = kind == 1 || P(0.7);
        s.WriteBits(static_cast<uint32_t>(old ? I(0, 20) : I(21, 255)), 8);
        s.WriteBits(static_cast<uint32_t>(old ? 1 : I(0, 255)), 8);
        s.WriteBits(static_cast<uint32_t>(I(0, 65535)), 16);
        s.WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
    } else {
        s.WriteBits(static_cast<uint32_t>(I(0, 255)), 8);
        s.WriteBits(static_cast<uint32_t>(I(0, 255)), 8);
        s.WriteBits(static_cast<uint32_t>(I(0, 7)), 3);
        s.WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
        s.WriteBits(static_cast<uint32_t>(I(0, 63)), 6);
        s.WriteBits(static_cast<uint32_t>(I(0, 63)), 6);
        s.WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
        s.WriteBits(static_cast<uint32_t>(I(0, 65535)), 16);
        s.WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
        s.WriteBits(static_cast<uint32_t>(I(0, 3)), 2);
    }
    if (kind == 1) {
        // A status: players (their stats, team, name), the rules, the scores.
        const int players = I(0, 3);
        s.WriteBits(static_cast<uint32_t>(players), 32);
        for (int i = 0; i < players; ++i) {
            for (int k = 1; k <= 5; ++k)
                s.WriteBits(static_cast<uint32_t>(I(0, 255)), 8);
            s.WriteBits(static_cast<uint32_t>(I(0, 2)), 32);
            char name[24];
            std::snprintf(name, sizeof name, "pl%d", I(0, 99));
            s.WriteString(name);
        }
        static const int ruleBits[] = {8, 16, 16, 16, 16, 16, 1, 1, 1, 1, 1, 1, 1, 8, 16, 1, 1, 1, 16, 10, 10};
        for (int b : ruleBits)
            s.WriteBits(static_cast<uint32_t>(I(0, (1 << b) - 1)), b);
    }
    // (Random bits after the data: a field read too long reads them.)
    s.WriteBits(static_cast<uint32_t>(I(0, 0xFFFF)), 16);
    const int bytes = static_cast<int>((s.BitPosition() + 7) / 8);
    return std::vector<uint8_t>(reinterpret_cast<uint8_t *>(words), reinterpret_cast<uint8_t *>(words) + bytes);
}

// The original's raw status: its players' vector (+4 first, +8 last, +0xC end; 0x5C each), the rules, the scores.
void OFillStatus(uint8_t *o, const S_ServerStatus &p)
{
    const size_t n = p.players.size();
    auto *first = static_cast<uint8_t *>(ONew(n * 0x5C + 1));
    std::memcpy(first, p.players.data(), n * 0x5C);
    OF<uint8_t *>(o, 4) = first;
    OF<uint8_t *>(o, 8) = first + n * 0x5C;
    OF<uint8_t *>(o, 0xC) = first + n * 0x5C;
    std::memcpy(o + 0x10, reinterpret_cast<const uint8_t *>(&p) + 0x10, 0x58);
}

void Call()
{
    C_NetGame &net = PNet();
    void *on = ONet();
    const int op = I(0, 33);
    switch (op) {
    case 0:
    case 1: {
        // Narrow to wide (ANSI, UTF-8): a null side, the length -1 or given.
        const bool utf8 = op == 1;
        const std::string text = RandomNarrow();
        const bool nullOut = P(0.05), nullText = P(0.05);
        const unsigned length = P(0.5) ? static_cast<unsigned>(-1) : static_cast<unsigned>(I(1, 40));
        wchar_t out[2][64];
        std::wmemset(out[0], 0x7F7F, 64);
        std::wmemset(out[1], 0x7F7F, 64);
        Run(std::string(utf8 ? "NetUtf8ToWide" : "NetAnsiToWide") + " " + N(static_cast<int>(length)),
            [&] {
                (utf8 ? NetUtf8ToWide : NetAnsiToWide)(nullOut ? nullptr : out[0], nullText ? nullptr : text.c_str(), length);
                return 0;
            },
            [&] {
                O_CALL(utf8 ? 0x00724b80 : 0x00724ad0, void(__stdcall *)(wchar_t *, const char *, unsigned))(nullOut ? nullptr : out[1],
                                                                                                         nullText ? nullptr : text.c_str(), length);
                return 0;
            });
        Check(std::wmemcmp(out[0], out[1], 64) == 0, "the wide text " + WStr(out[1]) + " / " + WStr(out[0]));
        break;
    }
    case 2:
    case 3: {
        // Wide to narrow.
        const bool utf8 = op == 3;
        const std::wstring text = RandomWide();
        const bool nullOut = P(0.05), nullText = P(0.05);
        const int length = P(0.5) ? -1 : I(1, 40);
        char out[2][128];
        std::memset(out, 0x7F, sizeof out);
        Run(std::string(utf8 ? "NetWideToUtf8" : "NetWideToAnsi") + " " + N(length),
            [&] {
                (utf8 ? NetWideToUtf8 : NetWideToAnsi)(nullOut ? nullptr : out[0], nullText ? nullptr : text.c_str(), length);
                return 0;
            },
            [&] {
                O_CALL(utf8 ? 0x00724bd0 : 0x00724b30, void(__stdcall *)(char *, const wchar_t *, int))(nullOut ? nullptr : out[1],
                                                                                                     nullText ? nullptr : text.c_str(), length);
                return 0;
            });
        Check(std::memcmp(out[0], out[1], 128) == 0, "the narrow text " + Bytes(out[1], 48) + " / " + Bytes(out[0], 48));
        break;
    }
    case 4: {
        void *address = &g_addressObjects[I(0, ADDRESSES - 1)];
        Run("GetHostAddress", [&] { return static_cast<long long>(GetHostAddress(static_cast<IDirectPlay8Address *>(address))); },
            [&] { return static_cast<long long>(O_CALL(0x00725190, uint32_t(__stdcall *)(void *))(address)); });
        break;
    }
    case 5:
    case 6:
    case 7: {
        // Hosting: the session's name, TCP/IP or another provider, the password.
        S_NetSessionDesc desc;
        std::memset(&desc, 0, sizeof desc);
        // (51 characters at most: the original's wide name has room for 52 before its "Creating" line, which overwrites
        // the rest before Host.)
        std::snprintf(desc.name, 52, "%s", RandomName("Game").c_str());
        g_script.createAddress = P(0.9) ? 0 : static_cast<long>(0x80004005);
        if (P(0.8))
            std::memcpy(desc.serviceProvider, &CLSID_NetDP8SP_TCPIP, 16);
        else
            for (uint32_t &w : desc.serviceProvider)
                w = static_cast<uint32_t>(I(0, 0x7FFFFFFF));
        std::snprintf(desc.password, sizeof desc.password, "%s", P(0.5) ? "" : "secret");
        const bool firewall = P(0.5);
        // (Host writes the local player's name into slot 0 after Close emptied the players: its buffer.)
        Run("Host " + N(firewall), [&] { return static_cast<long long>(net.Host(desc, firewall)); },
            [&] { return static_cast<long long>(O_CALL(0x00722b60, int(THISCALL *)(void *, const S_NetSessionDesc *, bool))(on, &desc, firewall)); });
        g_script.createAddress = 0;
        if (net.m_players.capacity() && OPlayers())
            Check(std::strncmp(net.m_players.data()->name, reinterpret_cast<const char *>(OPlayers() + 8), 0x34) == 0 &&
                      reinterpret_cast<const uint8_t *>(net.m_players.data())[0xA2] == OPlayers()[0xA2],
                  "slot 0's name and left flag " + Str(reinterpret_cast<const char *>(OPlayers() + 8)) + " " + N(OPlayers()[0xA2]) + " / " +
                      Str(net.m_players.data()->name) + " " + N(reinterpret_cast<const uint8_t *>(net.m_players.data())[0xA2]));
        break;
    }
    case 8: {
        const bool firewall = P(0.5);
        Run("HostDedicated " + N(firewall), [&] { return static_cast<long long>(net.HostDedicated(firewall)); },
            [&] { return static_cast<long long>(O_CALL(0x00722a80, bool(THISCALL *)(void *, bool))(on, firewall)); });
        break;
    }
    case 9:
        Run("CreateClient", [&] { return static_cast<long long>(net.CreateClient()); },
            [&] { return static_cast<long long>(O_CALL(0x00723e00, long(FAST *)(void *))(on)); });
        break;
    case 10: {
        static S_ServerEntry server;
        RandomEntry(&server);
        const bool none = P(0.3);
        void *devices[2] = {}, *hosts[2] = {};
        Run("CreateAddresses " + N(none),
            [&] {
                net.CreateAddresses(none ? nullptr : &server, reinterpret_cast<IDirectPlay8Address **>(&devices[0]),
                                    reinterpret_cast<IDirectPlay8Address **>(&hosts[0]));
                return 0;
            },
            [&] {
                O_CALL(0x00723700, void(THISCALL *)(void *, const S_ServerEntry *, void **, void **))(on, none ? nullptr : &server, &devices[1], &hosts[1]);
                return 0;
            });
        Check(Who(devices[0]) == Who(devices[1]) && Who(hosts[0]) == Who(hosts[1]), "the addresses made");
        break;
    }
    case 11:
    case 12:
    case 13:
    case 14: {
        // The enumeration: a server (searched, pinged, queried) or the LAN.
        static S_ServerEntry server;
        RandomEntry(&server);
        const bool none = P(0.3);
        S_ServerEntry *target = none ? nullptr : &server;
        if (op == 11) {
            const int request = I(0, 2);
            Run("EnumHosts " + N(none) + " " + N(request), [&] { return static_cast<long long>(net.EnumHosts(target, request)); },
                [&] { return static_cast<long long>(O_CALL(0x00723e80, int(THISCALL *)(void *, S_ServerEntry *, int))(on, target, request)); });
        } else if (op == 12) {
            Run("PingServer " + N(none), [&] { net.PingServer(SearchCallback, target); return 0; },
                [&] { O_CALL(0x00724070, void(THISCALL *)(void *, void *, S_ServerEntry *))(on, reinterpret_cast<void *>(SearchCallback), target); return 0; });
        } else if (op == 13) {
            Run("QueryServerStatus " + N(none), [&] { return static_cast<long long>(net.QueryServerStatus(StatusCallback, target)); },
                [&] {
                    return static_cast<long long>(
                        O_CALL(0x007240d0, int(THISCALL *)(void *, void *, S_ServerEntry *))(on, reinterpret_cast<void *>(StatusCallback), target));
                });
        } else {
            static S_ServerEntry servers[3];
            std::vector<S_ServerEntry *> list;
            for (int k = I(0, 3); k > 0; --k) {
                RandomEntry(&servers[list.size()]);
                list.push_back(&servers[list.size()]);
            }
            // (The original's vector: +4 first, +8 last.)
            void *ov[4] = {nullptr, list.empty() ? nullptr : list.data(), list.empty() ? nullptr : list.data() + list.size(),
                           list.empty() ? nullptr : list.data() + list.size()};
            Run("PingServers " + N(static_cast<long long>(list.size())), [&] { net.PingServers(SearchCallback, list); return 0; },
                [&] { O_CALL(0x00724090, void(THISCALL *)(void *, void *, void *))(on, reinterpret_cast<void *>(SearchCallback), ov); return 0; });
        }
        break;
    }
    case 15:
    case 16: {
        static S_ServerEntry server[2];
        RandomEntry(&server[0]);
        if (!net.m_client) {
            // (CreateClient's failure not checked: the client's object used.)
            g_script.createClient = 0;
            g_script.clientInit = 0;
        }
        server[1] = server[0];
        Run("Connect", [&] { return static_cast<long long>(net.Connect(&server[0])); },
            [&] { return static_cast<long long>(O_CALL(0x007240f0, int(THISCALL *)(void *, S_ServerEntry *))(on, &server[1])); });
        Check(std::memcmp(&server[0], &server[1], sizeof server[0]) == 0, "the server entry");
        break;
    }
    case 17:
        Run("OnConnected", [&] { net.OnConnected(); return 0; }, [&] { O_CALL(0x007244e0, FT0)(on); return 0; });
        break;
    case 18:
    case 19:
    case 20:
    case 21: {
        // The client's DirectPlay messages through its handler (and the server's handler: passed on).
        static const uint32_t types[] = {DPN_MSGID_CONNECT_COMPLETE, DPN_MSGID_ENUM_HOSTS_RESPONSE, DPN_MSGID_ENUM_HOSTS_RESPONSE, DPN_MSGID_RECEIVE,
                                         DPN_MSGID_RETURN_BUFFER, DPN_MSGID_TERMINATE_SESSION, 0xFFFF0001, 0x12345};
        const uint32_t type = types[I(0, 7)];
        const bool serverHandler = P(0.1);
        void *context = P(0.5) ? &net : nullptr;
        uint8_t message[2][0x40] = {};
        void *buffers[2] = {};
        std::vector<uint8_t> data;
        static uint8_t reply[4];
        DPN_APPLICATION_DESC application;
        static S_ServerEntry pinged[2];
        if (type == DPN_MSGID_CONNECT_COMPLETE) {
            auto *m = reinterpret_cast<DPNMSG_CONNECT_COMPLETE *>(message[0]);
            m->result = P(0.6) ? 0 : static_cast<HRESULT>(0x80158410);
            if (m->result == 0 && !net.m_serverLink && g_gameRole != GAME_ROLE_LOCAL)
                return;   // (accepted: the link the client made, unchecked)
            reply[0] = static_cast<uint8_t>(I(0, 255));
            m->replyData = reply;
            m->replySize = P(0.3) ? 0 : 4;
        } else if (type == DPN_MSGID_ENUM_HOSTS_RESPONSE) {
            // (The callbacks the enumeration set, called unchecked.)
            net.m_searchCallback = SearchCallback;
            net.m_statusCallback = StatusCallback;
            OF<void *>(on, 0x4EC) = reinterpret_cast<void *>(SearchCallback);
            OF<void *>(on, 0x4F0) = reinterpret_cast<void *>(StatusCallback);
            const int kind = P(0.8) ? I(0, 1) : I(2, 3);
            data = MakeResponse(kind, kind != 1 && P(0.5));
            static wchar_t name[40];
            const std::wstring w = RandomWide();
            std::swprintf(name, 40, L"%ls%ls", w.c_str(), P(0.1) ? L"DEMO" : L"");
            application.sessionName = name;
            auto *m = reinterpret_cast<DPNMSG_ENUM_HOSTS_RESPONSE *>(message[0]);
            m->sender = static_cast<IDirectPlay8Address *>(static_cast<void *>(&g_addressObjects[I(0, ADDRESSES - 1)]));
            m->application = &application;
            m->responseData = data.data();
            m->responseSize = static_cast<DWORD>(data.size());
            m->roundTripLatency = static_cast<DWORD>(I(0, 999));
            if (P(0.5)) {
                RandomEntry(&pinged[0]);
                pinged[1] = pinged[0];
                m->context = &pinged[0];
            }
        } else if (type == DPN_MSGID_RECEIVE) {
            data.resize(static_cast<size_t>(P(0.2) ? 0 : I(1, 40)));
            for (uint8_t &b : data)
                b = static_cast<uint8_t>(I(0, 255));
            auto *m = reinterpret_cast<DPNMSG_RECEIVE *>(message[0]);
            m->data = data.data();
            m->dataSize = static_cast<DWORD>(data.size());
        } else if (type == DPN_MSGID_RETURN_BUFFER) {
            buffers[0] = operator new(16);
            buffers[1] = ONew(16);
            reinterpret_cast<DPNMSG_RETURN_BUFFER *>(message[0])->buffer = buffers[0];
        } else if (type == DPN_MSGID_TERMINATE_SESSION) {
            // (The app's state: none or one.)
            const bool state = P(0.7);
            GetGameApp()->m_state = state ? reinterpret_cast<C_GameState *>(g_pState) : nullptr;
            OF<void *>(g_oApp, 0x14) = state ? g_oState : nullptr;
        }
        std::memcpy(message[1], message[0], sizeof message[0]);
        if (type == DPN_MSGID_RETURN_BUFFER)
            reinterpret_cast<DPNMSG_RETURN_BUFFER *>(message[1])->buffer = buffers[1];
        if (type == DPN_MSGID_ENUM_HOSTS_RESPONSE && reinterpret_cast<DPNMSG_ENUM_HOSTS_RESPONSE *>(message[0])->context)
            reinterpret_cast<DPNMSG_ENUM_HOSTS_RESPONSE *>(message[1])->context = &pinged[1];
        g_testEntries[0] = &pinged[0];
        g_testEntries[1] = &pinged[1];
        Run(std::string(serverHandler ? "ServerMessageHandler " : "ClientMessageHandler ") + Hex(type),
            [&] { return static_cast<long long>((serverHandler ? ServerMessageHandler : ClientMessageHandler)(context, type, message[0])); },
            [&] {
                return static_cast<long long>(O_CALL(serverHandler ? 0x00726440 : 0x00722520, long(__stdcall *)(void *, unsigned long, void *))(
                    context ? on : nullptr, type, message[1]));
            });
        if (type == DPN_MSGID_ENUM_HOSTS_RESPONSE && reinterpret_cast<DPNMSG_ENUM_HOSTS_RESPONSE *>(message[0])->context)
            Check(std::memcmp(&pinged[0], &pinged[1], sizeof pinged[0]) == 0, "the pinged entry " + ServerEntryText(&pinged[1]) + " / " + ServerEntryText(&pinged[0]));
        g_testEntries[0] = g_testEntries[1] = nullptr;
        GetGameApp()->m_state = reinterpret_cast<C_GameState *>(g_pState);   // (HostDedicated's, unchecked)
        OF<void *>(g_oApp, 0x14) = g_oState;
        break;
    }
    case 22: {
        const bool server = P(0.5);
        Run(std::string(server ? "VoiceServerHandler" : "VoiceClientHandler"),
            [&] { return static_cast<long long>((server ? VoiceServerHandler : VoiceClientHandler)(nullptr, 5, nullptr)); },
            [&] { return static_cast<long long>(O_CALL(server ? 0x00726430 : 0x00722510, long(__stdcall *)(void *, unsigned long, void *))(nullptr, 5, nullptr)); });
        break;
    }
    case 23:
        Run("MakeReplication", [&] { net.MakeReplication(); return 0; }, [&] { O_CALL(0x007233d0, FT0)(on); return 0; });
        break;
    case 24:
        Run("DeleteReplication", [&] { net.DeleteReplication(); return 0; }, [&] { O_CALL(0x007235f0, FT0)(on); return 0; });
        break;
    case 25:
        Run("StartVoiceServer", [&] { net.StartVoiceServer(); return 0; }, [&] { O_CALL(0x00724520, FT0)(on); return 0; });
        break;
    case 26: {
        const bool onServer = P(0.5);
        Run("StartVoiceClient " + N(onServer), [&] { net.StartVoiceClient(onServer); return 0; },
            [&] { O_CALL(0x00724740, void(THISCALL *)(void *, bool))(on, onServer); return 0; });
        break;
    }
    case 27:
        if (P(0.5))
            Run("StopVoiceServer", [&] { net.StopVoiceServer(); return 0; }, [&] { O_CALL(0x00724720, FT0)(on); return 0; });
        else
            Run("StopVoiceClient", [&] { net.StopVoiceClient(); return 0; }, [&] { O_CALL(0x00724820, FT0)(on); return 0; });
        break;
    case 28: {
        // (A voice client with its DirectPlay Voice object.)
        if (!net.m_voiceClient || !net.m_voiceClient->m_client || net.m_players.empty())
            return;
        const int slot = I(0, static_cast<int>(net.m_players.size()) - 1);
        Run("MakeVoiceBuffer " + N(slot), [&] { net.MakeVoiceBuffer(slot); return 0; },
            [&] { O_CALL(0x00724840, void(THISCALL *)(void *, int))(on, slot); return 0; });
        break;
    }
    case 29:
        Run("VisitPlayers", [&] { net.VisitPlayers(); return 0; }, [&] { O_CALL(0x007248f0, FT0)(on); return 0; });
        break;
    case 30:
        if (net.m_voiceClient && !net.m_voiceClient->m_client)
            return;   // (a voice client has its object)
        if (!GetGame() || g_localPlayerSlot < 0 || g_localPlayerSlot >= static_cast<int>(net.m_players.size()))
            return;   // (in a game, the local player's slot taken)
        Run("UpdateVoiceTeam", [&] { net.UpdateVoiceTeam(); return 0; }, [&] { O_CALL(0x00724940, FT0)(on); return 0; });
        break;
    case 31: {
        if (!(g_gameRole & GAME_ROLE_SERVER) && !net.m_client)
            return;   // (a client has its object)
        if ((g_gameRole & GAME_ROLE_SERVER) && !net.m_server && net.m_players.size() >= 2)
            return;   // (a server has its object)
        int figures[2][7];
        std::memset(figures, 0x5C, sizeof figures);
        Run("TakeBandwidth", [&] { net.TakeBandwidth(figures[0]); return 0; },
            [&] { O_CALL(0x00724d30, void(THISCALL *)(void *, int *))(on, figures[1]); return 0; });
        Check(std::memcmp(figures[0], figures[1], sizeof figures[0]) == 0,
              "the figures " + Bytes(figures[1], 28) + " / " + Bytes(figures[0], 28));
        break;
    }
    case 32: {
        // A status written: the players, the rules, the scores.
        S_ServerStatus p;
        p.unknown00 = 0;
        for (int k = I(0, 3); k > 0; --k) {
            S_StatusPlayer pl;
            std::memset(&pl, 0, sizeof pl);
            for (int i = 0; i < 9; ++i)
                reinterpret_cast<int *>(&pl)[i] = I(0, 255);
            pl.team = I(-1, 3);
            std::snprintf(pl.name, sizeof pl.name, "%s", RandomName("pl").substr(0, 0x30).c_str());
            p.players.push_back(pl);
        }
        for (int k = 0x10; k < 0x68; k += 4) {
            const int v = P(0.2) ? I(-70000, 70000) : I(0, 300);
            std::memcpy(reinterpret_cast<uint8_t *>(&p) + k, &v, 4);
        }
        alignas(16) uint8_t o[0x68] = {};
        OFillStatus(o, p);
        uint32_t words[2][256] = {};
        C_BitStream ps(words[0], 256 * 32);
        alignas(16) uint8_t os[0x40] = {};
        O_CALL(0x0073d740, void *(THISCALL *)(void *))(os);   // (the original's message: its vtable)
        OF<uint32_t *>(os, 4) = words[1];
        OF<uint32_t>(os, 8) = 0;
        OF<uint32_t>(os, 0xC) = 0;
        OF<uint32_t>(os, 0x10) = 256 * 32;
        const int which = I(0, 2);
        if (which == 0)
            Run("Status Write", [&] { p.Write(&ps); return 0; }, [&] { O_CALL(0x007263c0, void(THISCALL *)(void *, void *))(o, os); return 0; });
        else if (which == 1)
            Run("Status WriteRules", [&] { p.WriteRules(&ps); return 0; },
                [&] { O_CALL(0x00726120, void(THISCALL *)(void *, void *))(o + 0x10, os); return 0; });
        else if (!p.players.empty())
            Run("StatusPlayer Write", [&] { p.players[0].Write(&ps); return 0; },
                [&] { O_CALL(0x00726260, void(THISCALL *)(void *, void *))(OF<uint8_t *>(o, 4), os); return 0; });
        Check(ps.BitPosition() == OPosition(os) && std::memcmp(words[0], words[1], sizeof words[0]) == 0,
              "the bits " + N(OPosition(os)) + " / " + N(ps.BitPosition()));
        ODelete(OF<uint8_t *>(o, 4));
        break;
    }
    case 33: {
        // The replication's factories (made by MakeReplication: each side's, called with the object's id).
        if (!g_netReplication || g_netReplication == reinterpret_cast<C_NetReplication *>(g_pReplication)) {
            // (One made first on both sides, not compared.)
            net.MakeReplication();
            O_CALL(0x007233d0, FT0)(on);
            g_log[0].clear();
            g_log[1].clear();
        }
        const int id = P(0.3) ? I(31, 33) : P(0.5) ? I(0, 31) : I(32, 99);
        const bool reader = P(0.5);
        auto name = [](const void *f, bool orig) -> long long {
            const struct {
                const void *port;
                uint32_t orig;
            } known[] = {{reinterpret_cast<const void *>(&NetCreateMessage), 0x0053b760}, {reinterpret_cast<const void *>(&MakeSeatWeaponMessage), 0x004c14b0},
                         {reinterpret_cast<const void *>(&CarNetReadState), 0x004d0610}, {reinterpret_cast<const void *>(&NetReadControlBits), 0x0053b970}};
            for (int i = 0; i < 4; ++i)
                if (f == (orig ? reinterpret_cast<const void *>(static_cast<uintptr_t>(known[i].orig)) : known[i].port))
                    return i;
            return f ? 99 : -1;
        };
        void *orep = G<void *>(O_REPLICATION);
        Run(std::string(reader ? "StateReader " : "Factory ") + N(id),
            [&] {
                return name(reader ? reinterpret_cast<const void *>(g_netReplication->m_makeStateReader(id))
                                   : reinterpret_cast<const void *>(g_netReplication->m_makeFactory(id)),
                            false);
            },
            [&] { return name(O_CALL(OF<uint32_t>(orep, reader ? 0xE0 : 0xE4), void *(FAST *)(int))(id), true); });
        break;
    }
    }
}

void Round()
{
    g_trace.clear();
    SetWorld();
    CompareState();
    for (int k = I(1, 8); k > 0; --k)
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
    if (std::getenv("HD2_SESS_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("sess_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);

    // The shared objects' vtables.
    for (void **v : {g_serverVtable, g_clientVtable, g_addressVtable})
        v[2] = reinterpret_cast<void *>(&FakeRelease);
    g_serverVtable[0x0C / 4] = reinterpret_cast<void *>(&FakeServerInitialize);
    g_serverVtable[0x10 / 4] = reinterpret_cast<void *>(&FakeEnumServiceProviders);
    g_serverVtable[0x14 / 4] = reinterpret_cast<void *>(&FakeServerCancel);
    g_serverVtable[0x34 / 4] = reinterpret_cast<void *>(&FakeHost);
    g_serverVtable[0x5C / 4] = reinterpret_cast<void *>(&FakeServerClose);
    g_serverVtable[0x60 / 4] = reinterpret_cast<void *>(&FakeDestroyClient);
    g_serverVtable[0x74 / 4] = reinterpret_cast<void *>(&FakeServerSetCaps);
    g_serverVtable[0x80 / 4] = reinterpret_cast<void *>(&FakeServerConnectionInfo);
    g_clientVtable[0x0C / 4] = reinterpret_cast<void *>(&FakeClientInitialize);
    g_clientVtable[0x14 / 4] = reinterpret_cast<void *>(&FakeEnumHosts);
    g_clientVtable[0x18 / 4] = reinterpret_cast<void *>(&FakeClientCancel);
    g_clientVtable[0x1C / 4] = reinterpret_cast<void *>(&FakeConnect);
    g_clientVtable[0x2C / 4] = reinterpret_cast<void *>(&FakeSetClientInfo);
    g_clientVtable[0x38 / 4] = reinterpret_cast<void *>(&FakeClientClose);
    g_clientVtable[0x44 / 4] = reinterpret_cast<void *>(&FakeClientSetCaps);
    g_clientVtable[0x50 / 4] = reinterpret_cast<void *>(&FakeClientConnectionInfo);
    g_addressVtable[0x34 / 4] = reinterpret_cast<void *>(&FakeSetSP);
    g_addressVtable[0x40 / 4] = reinterpret_cast<void *>(&FakeGetComponentByName);
    g_addressVtable[0x48 / 4] = reinterpret_cast<void *>(&FakeAddComponent);
    g_addressVtable[0x50 / 4] = reinterpret_cast<void *>(&FakeSetDevice);
    for (void *&a : g_addressObjects)
        a = g_addressVtable;
    g_voiceClientVtable[0x2C / 4] = reinterpret_cast<void *>(&FakeSetTransmitTargets);
    g_voiceClientVtable[0x34 / 4] = reinterpret_cast<void *>(&FakeCreate3DSoundBuffer);
    g_bufferVtable[0x40 / 4] = reinterpret_cast<void *>(&FakeSetMaxDistance);
    g_bufferVtable[0x44 / 4] = reinterpret_cast<void *>(&FakeSetMinDistance);
    g_graphVtable[0x08 / 4] = reinterpret_cast<void *>(&FakeSetFlags);
    g_graphVtable[0x48 / 4] = reinterpret_cast<void *>(&FakeGetHWND);
    g_graphVtable[0xDC / 4] = reinterpret_cast<void *>(&FakeInitMouse);
    // The port's: the static replication (its class's own vtable: the test's virtuals), the pool's connections, the
    // voices, the app's state, the unknown object, the profile.
    new (g_pReplication) C_NetReplication();
    for (auto &l : g_pLinks)
        new (l) C_NetConnection(0, 0, nullptr);
    for (auto &v : g_pVoices)
        new (v) C_NetVoice(nullptr, nullptr);
    const int pending = VSlot(&C_GameState::SetPendingState);
    if (pending < 0) {
        std::printf("sess_diff: a virtual's slot not found\n");
        return 1;
    }
    g_pStateVtable[pending] = reinterpret_cast<void *>(&PSetPendingState);
    *reinterpret_cast<void **>(g_pState) = g_pStateVtable;
    g_pUnknownVtable[0x0C / 4] = reinterpret_cast<void *>(&PDestroy);
    *reinterpret_cast<void **>(g_pUnknown) = g_pUnknownVtable;
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_mpSettings = reinterpret_cast<S_MultiplayerSettings *>(g_mp);
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_player = reinterpret_cast<C_PlayerProfile *>(g_playerProfile);
    // The original's.
    g_oVoiceVtable[0] = reinterpret_cast<void *>(&OVoiceDelete);
    for (auto &v : g_oVoices)
        OF<void *>(v, 0) = g_oVoiceVtable;
    g_oReplicationVtable[0] = reinterpret_cast<void *>(&OSetReadQuantizations);
    g_oReplicationVtable[1] = reinterpret_cast<void *>(&OSetWriteQuantizations);
    g_oReplicationVtable[4] = reinterpret_cast<void *>(&OReplicationDelete);
    g_oReplicationVtable[5] = reinterpret_cast<void *>(&ORepWrite);
    g_oReplicationVtable[6] = reinterpret_cast<void *>(&ORepRead);
    const int spect = VSlot(&C_HumanController::IsSpectating);
    if (spect < 0) {
        std::printf("sess_diff: IsSpectating's slot not found\n");
        return 1;
    }
    g_pControllerVtable[spect] = reinterpret_cast<void *>(&PIsSpectating);
    for (int i = 0; i < HUMANS; ++i) {
        *reinterpret_cast<void **>(g_pControllers[i]) = g_pControllerVtable;
        OF<void *>(g_oControllers[i], 0) = g_oControllerVtable;
    }
    g_oControllerVtable[0x64 / 4] = reinterpret_cast<void *>(&OIsSpectating);
    OF<void *>(g_oReplication, 0) = g_oReplicationVtable;
    g_oLinkVtable[0] = reinterpret_cast<void *>(&OConnectionDelete);
    for (auto &l : g_oLinks)
        OF<void *>(l, 0) = g_oLinkVtable;
    g_oStateVtable[0x2C / 4] = reinterpret_cast<void *>(&OSetPendingState);
    OF<void *>(g_oState, 0) = g_oStateVtable;
    g_oUnknownVtable[0x0C / 4] = reinterpret_cast<void *>(&ODestroy);
    OF<void *>(g_oUnknown, 0) = g_oUnknownVtable;

    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e884a, reinterpret_cast<const void *>(ORealloc));
    RedirectOriginal(0x007e42d9, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e4375, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e562e, reinterpret_cast<const void *>(OAtexit));
    RedirectOriginal(0x007e85af, reinterpret_cast<const void *>(FakeClock));
    RedirectOriginal(0x007e53c2, reinterpret_cast<const void *>(OAlignedMalloc));
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x00746ca0, reinterpret_cast<const void *>(OTransportStop)},
        {0x00746cf0, reinterpret_cast<const void *>(OTransportDeleteConnections)},
        {0x00746c10, reinterpret_cast<const void *>(OTransportStart)},
        {0x00746fb0, reinterpret_cast<const void *>(OTransportAdd)},
        {0x00747180, reinterpret_cast<const void *>(OTransportRemove)},
        {0x00746d60, reinterpret_cast<const void *>(OFindById)},
        {0x00746e40, reinterpret_cast<const void *>(OTransportLock)},
        {0x00746e60, reinterpret_cast<const void *>(OTransportUnlock)},
        {0x00747260, reinterpret_cast<const void *>(OFlushAll)},
        {0x007486e0, reinterpret_cast<const void *>(OTakeCounters)},
        {0x00746180, reinterpret_cast<const void *>(OConnectionCtor)},
        {0x00746300, reinterpret_cast<const void *>(OSend)},
        {0x00746ac0, reinterpret_cast<const void *>(OSetResendTime)},
        {0x00749a70, reinterpret_cast<const void *>(OAllocBuffer)},
        {0x00749b20, reinterpret_cast<const void *>(OFreeBuffer)},
        {0x00748dc0, reinterpret_cast<const void *>(OGetPerfTimeMs)},
        {0x00748da0, reinterpret_cast<const void *>(OInitPerfTime)},
        {0x0073ee40, reinterpret_cast<const void *>(OPerfTimerCtor)},
        {0x0073ee60, reinterpret_cast<const void *>(OPerfTimerFire)},
        {0x0073f020, reinterpret_cast<const void *>(OReplicationCtor)},
        {0x0073ed40, reinterpret_cast<const void *>(OSetPropertyField2)},
        {0x0073fa30, reinterpret_cast<const void *>(OReplicationSetLag)},
        {0x0073f9c0, reinterpret_cast<const void *>(OFindObject)},
        {0x0073f970, reinterpret_cast<const void *>(ODeleteObjects)},
        {0x0073ed80, reinterpret_cast<const void *>(OObjectSetLag)},
        {0x00475dd0, reinterpret_cast<const void *>(OGetActor)},
        {0x006dcd10, reinterpret_cast<const void *>(OGetGameApp)},
        {0x0072a480, reinterpret_cast<const void *>(OSendGameMessage)},
        {0x0072b590, reinterpret_cast<const void *>(OPostCommand)},
        {0x006c4e80, reinterpret_cast<const void *>(OAppPrintMessage)},
        {0x00728820, reinterpret_cast<const void *>(OHandleClientCommand)},
        {0x00727bd0, reinterpret_cast<const void *>(OHandleServerCommand)},
        {0x006987d0, reinterpret_cast<const void *>(OGetGame)},
        {0x0069c240, reinterpret_cast<const void *>(OPrintMessage)},
        {0x0069c2b0, reinterpret_cast<const void *>(OPrint)},
        {0x0048dc70, reinterpret_cast<const void *>(ODispatchMessage)},
        {0x0048dc50, reinterpret_cast<const void *>(OCallUnknown28)},
        {0x006e5380, reinterpret_cast<const void *>(OTextsGet)},
        {0x00730410, reinterpret_cast<const void *>(OCountPlayers)},
        {0x0072f6c0, reinterpret_cast<const void *>(OKickPlayer)},
        {0x00730130, reinterpret_cast<const void *>(OSendPlayerScore)},
        {0x0072f8d0, reinterpret_cast<const void *>(OSetLevelBounds)},
        {0x00723dc0, reinterpret_cast<const void *>(OAnswerQueries)},
        {0x00732730, reinterpret_cast<const void *>(OConfigureFirewall)},
        {0x00723d20, reinterpret_cast<const void *>(OStartReporting)},
        {0x007316f0, reinterpret_cast<const void *>(OStartKeyQuery)},
        {0x007317a0, reinterpret_cast<const void *>(ORunKeyQuery)},
        {0x00726470, reinterpret_cast<const void *>(OHandleServerMessage)},
        {0x00738760, reinterpret_cast<const void *>(OStopGameSpyQueries)},
        {0x007252b0, reinterpret_cast<const void *>(OServerEntryCtor)},
        {0x007252c0, reinterpret_cast<const void *>(OServerEntryCtor)},
        {0x0064da80, reinterpret_cast<const void *>(OHasCjkCharacters)},
        {0x00749ed0, reinterpret_cast<const void *>(OStatsWrite)},
        {0x00749f40, reinterpret_cast<const void *>(OStatsRead)},
        {0x006b6a60, reinterpret_cast<const void *>(OGetDetailSettings)},
        {0x006ad200, reinterpret_cast<const void *>(OWriteDescription)},
        {0x00742190, reinterpret_cast<const void *>(OVoiceCtor)},
        {0x007421f0, reinterpret_cast<const void *>(OVoiceInit)},
        {0x007425e0, reinterpret_cast<const void *>(OSetClientConfig)},
        {0x005a8820, reinterpret_cast<const void *>(OGetTeam)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    RedirectApi("kernel32.dll", "EnterCriticalSection", reinterpret_cast<const void *>(&FakeEnterCriticalSection));
    RedirectApi("kernel32.dll", "LeaveCriticalSection", reinterpret_cast<const void *>(&FakeLeaveCriticalSection));
    RedirectApi("kernel32.dll", "InitializeCriticalSection", reinterpret_cast<const void *>(&FakeInitializeCriticalSection));
    RedirectApi("kernel32.dll", "DeleteCriticalSection", reinterpret_cast<const void *>(&FakeDeleteCriticalSection));
    RedirectApi("kernel32.dll", "WaitForSingleObject", reinterpret_cast<const void *>(&FakeWaitForSingleObject));
    RedirectApi("kernel32.dll", "CloseHandle", reinterpret_cast<const void *>(&FakeCloseHandle));
    RedirectApi("kernel32.dll", "GetStartupInfoA", reinterpret_cast<const void *>(&FakeGetStartupInfoA));
    RedirectApi("kernel32.dll", "CreateProcessA", reinterpret_cast<const void *>(&FakeCreateProcessA));
    RedirectApi("winmm.dll", "timeGetTime", reinterpret_cast<const void *>(&FakeTimeGetTime));
    RedirectApi("user32.dll", "ShowWindow", reinterpret_cast<const void *>(&FakeShowWindow));
    RedirectApi("ole32.dll", "CoInitialize", reinterpret_cast<const void *>(&FakeCoInitialize));
    RedirectApi("ole32.dll", "CoInitializeEx", reinterpret_cast<const void *>(&FakeCoInitializeEx));
    RedirectApi("ole32.dll", "CoCreateInstance", reinterpret_cast<const void *>(&FakeCoCreateInstance));
    RedirectApi("ws2_32.dll", "closesocket", reinterpret_cast<const void *>(&FakeClosesocket)) ||
        RedirectApi("wsock32.dll", "closesocket", reinterpret_cast<const void *>(&FakeClosesocket));
    G<void *>(O_PROFILE + 0x24) = g_mp;
    G<void *>(O_PROFILE + 0x20) = g_playerProfile;
    std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(O_VOTABLE)), g_votableCommands, sizeof g_votableCommands);
    RedirectApi("LS3DF.dll", "GetIGraph", reinterpret_cast<const void *>(&OGetIGraph));
    // The original's net game: its constructor and Init (statics never run), the watched locks.
    g_watched[0][0] = reinterpret_cast<CRITICAL_SECTION *>(&g_netGame.m_playersLock);
    g_watched[0][1] = reinterpret_cast<CRITICAL_SECTION *>(&g_netGame.m_queueLock);
    g_watched[1][0] = reinterpret_cast<CRITICAL_SECTION *>(ONet() + 0x418);
    g_watched[1][1] = reinterpret_cast<CRITICAL_SECTION *>(ONet() + 0x430);
    O_CALL(0x0072e1d0, void *(FAST *)(void *))(ONet());
    O_CALL(0x0072e570, FT0)(ONet());
    PNet().Init();
    GetGameApp()->m_state = reinterpret_cast<C_GameState *>(g_pState);
    OF<void *>(g_oApp, 0x14) = g_oState;

    int rounds = std::getenv("HD2_SESS_QUICK") ? 5000 : 20000;
    if (const char *r = std::getenv("HD2_SESS_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_SESS_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("sess_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
