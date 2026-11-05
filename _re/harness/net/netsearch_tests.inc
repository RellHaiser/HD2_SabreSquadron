// (Part 10d-iii.) The LAN search, the GameSpy reporting and the CD-key checks (net/net_search.cpp). Included by
// netgame_test.cpp after netplayers_tests.inc.

// The tests' UDP ports: a band of 100 of the process's own (the mutation runs go in parallel).
int TestPort(int offset)
{
    static const int base = 20000 + static_cast<int>(GetCurrentProcessId() % 400) * 100;
    return base + offset;
}

C_PlayerProfile *TestPlayerProfile(const char *name)
{
    alignas(16) static unsigned char storage[0x1000];
    auto *profile = reinterpret_cast<C_PlayerProfile *>(storage);
    profile->m_name = C_str(name);
    return profile;
}

void TestBrowser()
{
    C_NetGame &net = Fresh();
    g_gameProfile.m_mpSettings = &g_testMp;
    // The LAN browser: made once (cleared after), asked for twelve keys; the role 0x12.
    g_gameRole = GAME_ROLE_LOCAL;
    CHECK(net.StartLanSearch() == 1 && g_gameRole == 0x12u);
    CHECK(Logged("qr2 key 51 isdedicated") && Logged("qr2 key 52 voicechat") && Logged("qr2 key 53 expansion") &&
          Logged("sb new hd2 hd2 sK8pQ9 1 10 1 1 0") && Logged("sb update 1 1 1 1 11 6 5 8 10 4 51 3 19 52 53 0") &&
          !Logged("sb clear 1") && net.m_serverBrowser == g_browser);
    g_log.clear();
    net.StartLanSearch();
    CHECK(Logged("sb clear 1") && !Logged("sb new hd2 hd2 sK8pQ9 1 10 1 1 0"));
    // Polled while there is one.
    g_log.clear();
    net.PollSearch();
    net.m_serverBrowser = nullptr;
    net.PollSearch();
    CHECK(g_log.size() == 1 && g_log[0] == "sb think 1");
    // The search: GameSpy's (listing 1) or DirectPlay's.
    g_testMp.unknown174 = 1;
    g_log.clear();
    CHECK(net.StartSearch() == 0 && Logged("sb new hd2 hd2 sK8pQ9 1 10 1 1 0") && !Logged("enum hosts 0"));
    g_testMp.unknown174 = 0;
    g_log.clear();
    CHECK(net.StartSearch() == 0 && Logged("enum hosts 0") && !Logged("sb clear 1"));
    // SearchServers: the session closed first, the callback kept (g_netGame searches).
    g_log.clear();
    auto callback = [](S_ServerEntry *) {};
    void(__fastcall *fastCallback)(S_ServerEntry *) = reinterpret_cast<void(__fastcall *)(S_ServerEntry *)>(+callback);
    net.SearchServers(fastCallback);
    CHECK(net.m_searchCallback == fastCallback && Logged("stop reporting") && Logged("enum hosts 0"));
    net.m_serverBrowser = nullptr;
    g_gameRole = GAME_ROLE_LOCAL;
}

void TestBrowserServers()
{
    // A server the browser found: an entry for the main menu (reason 1 only).
    S_SbServer server;
    server.values = {{"hostname", "Hosted"},   {"mapname", "Map1"},      {"numplayers", "5"}, {"maxplayers", "12"},
                     {"hostport", "11001"},    {"gametype", "Objectives"}, {"password", "1"}, {"isdedicated", "1"},
                     {"voicechat", "0"},       {"expansion", "2"},       {"gamever", "1.3"}};
    server.ping = 77;
    server.address = 0x0100007F;
    g_foundServers.clear();
    OnBrowserCallback(g_browser, 0, &server, nullptr);
    CHECK(g_foundServers.empty());
    OnBrowserCallback(g_browser, 1, &server, nullptr);
    CHECK(g_foundServers.size() == 1);
    S_ServerEntry *entry = g_foundServers[0];
    CHECK(std::strcmp(entry->name, "Hosted") == 0 && std::strcmp(entry->map, "Map1") == 0 && entry->players == 5 &&
          entry->maxPlayers == 12 && entry->port == 11001 && entry->gameType == MP_GAME_OBJECTIVES && entry->ping == 77 &&
          entry->address == 0x0100007Fu && entry->password && entry->dedicated && !entry->voice && entry->internet &&
          entry->unknown84 == -1 && entry->unknown94 == 2 && entry->versionMajor == 1 && entry->versionMinor == 3);
    // The defaults; the game types (any case); a long name kept within its field.
    S_SbServer bare;
    bare.values = {{"gametype", "OCCUPATION"}, {"voicechat", "1"}, {"hostname", std::string(80, 'h')}};
    AddBrowserServer(&bare);
    entry = g_foundServers[1];
    CHECK(std::strlen(entry->name) == sizeof entry->name - 1 && std::strcmp(entry->map, "(NO MAP)") == 0 &&
          entry->gameType == MP_GAME_OCCUPATION && entry->voice && !entry->password && !entry->dedicated &&
          entry->unknown94 == 1 && entry->versionMajor == 1 && entry->versionMinor == 1 && entry->players == 0 &&
          entry->port == 0);
    S_SbServer coop, other, none;
    coop.values = {{"gametype", "cooperative"}};
    other.values = {{"gametype", "deathmatch"}};
    AddBrowserServer(&coop);
    AddBrowserServer(&other);
    AddBrowserServer(&none);
    S_SbServer longMap;
    longMap.values = {{"mapname", std::string(90, 'm')}, {"gametype", std::string(60, 'g')}};
    g_log.clear();
    AddBrowserServer(&longMap);
    CHECK(Logged("server entry reset") && std::strlen(g_foundServers[5]->map) == sizeof g_foundServers[5]->map - 1 &&
          g_foundServers[5]->gameType == MP_GAME_DEATHMATCH && g_foundServers[5]->name[0] == '(' &&
          !g_foundServers[5]->voice && !g_foundServers[5]->password && !g_foundServers[5]->dedicated &&
          g_foundServers[5]->maxPlayers == 0 && g_foundServers[5]->ping == 0 && g_foundServers[5]->listed);
    CHECK(g_foundServers[2]->gameType == MP_GAME_COOPERATIVE && g_foundServers[3]->gameType == MP_GAME_DEATHMATCH &&
          g_foundServers[4]->gameType == MP_GAME_DEATHMATCH && std::strcmp(g_foundServers[4]->name, "(NO HOST)") == 0);
    for (S_ServerEntry *found : g_foundServers)
        delete found;
    g_foundServers.clear();
}

std::vector<std::string> ServerKey(int key)
{
    S_Qr2Buffer buffer;
    ReportServerKey(key, &buffer, nullptr);
    return buffer.added;
}

void TestReporting()
{
    C_NetGame &net = Fresh();
    S_MultiplayerSettings &mp = g_testMp;
    std::strcpy(mp.sessionName, "Session");
    mp.port = TestPort(0);
    mp.gameType = MP_GAME_OBJECTIVES;
    mp.maxClients = 16;
    mp.dedicated = true;
    mp.unknown178 = 3;
    mp.password = C_str("pw");
    C_MpMap map(C_str("Arnhem"), C_str("dir"));
    g_currentMap = &map;
    auto *game = reinterpret_cast<C_game *>(g_gameStorage);
    g_game = game;
    g_netGame.m_players.clear();
    Named(g_netGame, {"a", "", "c"});
    g_netGame.m_players[1].connection = 0;
    // The server's keys (nothing without a game).
    using V = std::vector<std::string>;
    CHECK((ServerKey(QR2_HOSTNAME) == V{"Session"} && ServerKey(QR2_GAMEVER) == V{"1.2"} &&
          ServerKey(QR2_HOSTPORT) == V{"#" + std::to_string(TestPort(0))} && ServerKey(QR2_MAPNAME) == V{"Arnhem"} &&
          ServerKey(QR2_GAMETYPE) == V{"Objectives"} && ServerKey(QR2_NUMPLAYERS) == V{"#2"} &&
          ServerKey(QR2_MAXPLAYERS) == V{"#16"} && ServerKey(QR2_GAMEMODE) == V{"#0"} &&
          ServerKey(QR2_PASSWORD) == V{"#1"} && ServerKey(QR2_ISDEDICATED) == V{"#1"} &&
          ServerKey(QR2_VOICECHAT) == V{"#1"} && ServerKey(QR2_EXPANSION) == V{"#2"} && ServerKey(2) == V{""} &&
          ServerKey(7) == V{""} && ServerKey(QR2_SKILL) == V{""}));
    mp.gameType = MP_GAME_OCCUPATION;
    V occupation = ServerKey(QR2_GAMETYPE);
    mp.gameType = MP_GAME_COOPERATIVE;
    V cooperative = ServerKey(QR2_GAMETYPE);
    mp.gameType = MP_GAME_DEATHMATCH;
    V deathmatch = ServerKey(QR2_GAMETYPE);
    CHECK((occupation == V{"Occupation"} && cooperative == V{"Cooperative"} && deathmatch == V{"Deathmatch"}));
    mp.password = C_str();
    mp.dedicated = false;
    mp.unknown178 = 0;
    CHECK((ServerKey(QR2_PASSWORD) == V{"#0"} && ServerKey(QR2_ISDEDICATED) == V{"#0"} && ServerKey(QR2_VOICECHAT) == V{"#0"}));
    C_MpMap unnamed{C_str(), C_str()};
    g_currentMap = &unnamed;
    CHECK((ServerKey(QR2_MAPNAME) == V{""}));
    g_game = nullptr;
    CHECK(ServerKey(QR2_HOSTNAME).empty());
    g_game = game;
    // The players' keys: the index-th named player.
    g_netGame.m_players[2].stats[3] = 7;
    g_netGame.m_players[2].stats[4] = 30;
    g_netGame.m_players[2].stats[2] = 4;
    g_netGame.m_players[2].team = 1;
    auto playerKey = [](int key, int index) {
        S_Qr2Buffer buffer;
        ReportPlayerKey(key, index, &buffer, nullptr);
        return buffer.added;
    };
    CHECK((playerKey(QR2_PLAYER, 0) == V{"a"} && playerKey(QR2_PLAYER, 1) == V{"c"} && playerKey(QR2_SCORE, 1) == V{"#7"} &&
          playerKey(QR2_PING, 1) == V{"#30"} && playerKey(QR2_TEAM, 1) == V{"#1"} && playerKey(QR2_DEATHS, 1) == V{"#4"} &&
          playerKey(QR2_SKILL, 1) == V{""} && playerKey(QR2_PLAYER, 2) == V{""} && playerKey(QR2_TEAM, 0) == V{"#2"}));
    // The teams': the names, the scores.
    g_netGame.m_teamScores[0] = 4.9f;
    g_netGame.m_teamScores[1] = 2.0f;
    auto teamKey = [](int key, int index) {
        S_Qr2Buffer buffer;
        ReportTeamKey(key, index, &buffer, nullptr);
        return buffer.added;
    };
    CHECK((teamKey(QR2_TEAM_NAME, 0) == V{"axis"} && teamKey(QR2_TEAM_NAME, 1) == V{"allied"} &&
          teamKey(QR2_TEAM_SCORE, 0) == V{"#4"} && teamKey(QR2_TEAM_SCORE, 1) == V{"#2"} && teamKey(QR2_PLAYER, 0).empty()));
    // The keys of each kind; the counts.
    S_Qr2KeyBuffer serverKeys, playerKeys, teamKeys, noKeys;
    ReportKeyList(QR2_KEY_SERVER, &serverKeys, nullptr);
    ReportKeyList(QR2_KEY_PLAYER, &playerKeys, nullptr);
    ReportKeyList(QR2_KEY_TEAM, &teamKeys, nullptr);
    ReportKeyList(3, &noKeys, nullptr);
    CHECK((serverKeys.keys == std::vector<int>{1, 3, 4, 5, 8, 10, 11, 6, 0x33, 0x13, 0x34, 0x35} &&
          playerKeys.keys == std::vector<int>{0x15, 0x16, 0x1A, 0x18, 0x19} && teamKeys.keys == std::vector<int>{0x1C, 0x1D} &&
          noKeys.keys.empty()));
    mp.gameType = MP_GAME_OBJECTIVES;
    CHECK(ReportCount(QR2_KEY_PLAYER, nullptr) == 2 && ReportCount(QR2_KEY_TEAM, nullptr) == 2 &&
          ReportCount(QR2_KEY_SERVER, nullptr) == 0);
    mp.gameType = MP_GAME_DEATHMATCH;
    CHECK(ReportCount(QR2_KEY_TEAM, nullptr) == 1);
    ReportError(1, nullptr, nullptr);
    // Started: the keys, qr2 on the port + 3, gcd (0x2EE), the key socket on the port + 2.
    g_log.clear();
    net.StartReporting();
    char init[64];
    std::snprintf(init, sizeof init, "qr2 init 0 0 %d hd2 sK8pQ9 1 0 0", TestPort(3));
    CHECK(Logged("qr2 key 51 isdedicated") && Logged(init) && Logged("gcd init 0 750") &&
          net.m_reporting && net.m_keySocket != static_cast<uintptr_t>(-1) && g_serverKey == ReportServerKey &&
          g_playerKey == ReportPlayerKey && g_teamKey == ReportTeamKey && g_keyList == ReportKeyList &&
          g_count == ReportCount && g_addError == ReportError);
    sockaddr_in bound = {};
    int boundSize = sizeof bound;
    getsockname(net.m_keySocket, reinterpret_cast<sockaddr *>(&bound), &boundSize);
    CHECK(ntohs(bound.sin_port) == TestPort(2));
    // The queries answered (while reporting): qr2, gcd (the players locked), the key checks.
    net.m_keyQueries[9].state = KEY_CHALLENGED;
    net.m_keyQueries[9].expiry = 1;
    g_log.clear();
    net.AnswerQueries();
    CHECK(Logged("qr2 think 0") && Logged("gcd think") && net.m_keyQueries[9].state == KEY_FREE);
    net.m_reporting = false;
    g_log.clear();
    net.AnswerQueries();
    CHECK(g_log.empty());
    closesocket(net.m_keySocket);
    net.m_keySocket = static_cast<uintptr_t>(-1);
    g_netGame.m_players.clear();
    g_currentMap = nullptr;
}

// A raw UDP socket on 127.0.0.1 (`port` 0: any) for the key packets' other side.
SOCKET RawSocket(uint16_t port)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    local.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(s, reinterpret_cast<sockaddr *>(&local), sizeof local);
    return s;
}
// A packet on `s` within 300 ms, decoded (XOR 0xA0; its checksum checked): its data, else "".
std::string RawReceive(SOCKET s)
{
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(s, &readable);
    timeval wait = {0, 300000};
    if (select(0, &readable, nullptr, nullptr, &wait) <= 0)
        return "";
    char data[600];
    const int got = recv(s, data, sizeof data, 0);
    if (got < 4)
        return "";
    int sum = 0;
    for (int i = 0; i < got - 4; ++i) {
        const int b = static_cast<signed char>(data[i]);
        sum += b << 16 | b;
    }
    int sent;
    std::memcpy(&sent, data + got - 4, 4);
    if (sent != sum)
        return "bad checksum";
    std::string text;
    for (int i = 0; i < got - 4; ++i)
        text += static_cast<char>(data[i] ^ 0xA0);
    return text;
}
void RawSend(SOCKET s, uint16_t port, const std::string &text, bool corrupt = false)
{
    char data[600];
    int sum = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        data[i] = static_cast<char>(text[i] ^ 0xA0);
        const int b = static_cast<signed char>(data[i]);
        sum += b << 16 | b;
    }
    if (corrupt)
        ++sum;
    std::memcpy(data + text.size(), &sum, 4);
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = inet_addr("127.0.0.1");
    sendto(s, data, static_cast<int>(text.size()) + 4, 0, reinterpret_cast<sockaddr *>(&to), sizeof to);
}
uint16_t SocketPort(uintptr_t s)
{
    sockaddr_in bound = {};
    int size = sizeof bound;
    getsockname(s, reinterpret_cast<sockaddr *>(&bound), &size);
    return ntohs(bound.sin_port);
}

void TestKeyPackets()
{
    C_NetGame &net = Fresh();
    g_testMp.port = TestPort(10);
    net.m_keyQueries[42].state = KEY_GOOD;
    net.OpenKeyServer();
    CHECK(SocketPort(net.m_keySocket) == TestPort(12) && net.m_keyQueries[42].state == KEY_FREE);
    const uint32_t loopback = inet_addr("127.0.0.1");
    SOCKET peer = RawSocket(0);
    const uint16_t peerPort = SocketPort(peer);
    // Sent: XORed, the checksum after (a byte over 0x7F counted signed).
    net.SendKeyPacket("c:\xE9t\x01", 5, loopback, peerPort);
    CHECK(RawReceive(peer) == std::string("c:\xE9t\x01", 5));
    // Received: decoded; the sender's address and port; the length without the checksum.
    char data[256];
    int size = sizeof data;
    uint32_t address = 0, port = 0;
    CHECK(!net.ReceiveKeyPacket(data, &size, &address, &port));
    RawSend(peer, TestPort(12), std::string("r:x\xF0", 4));
    Sleep(20);
    CHECK(net.ReceiveKeyPacket(data, &size, &address, &port) && size == 4 &&
          std::string(data, 4) == std::string("r:x\xF0", 4) && address == loopback && port == peerPort);
    // A wrong checksum: refused.
    RawSend(peer, TestPort(12), "c:pat", true);
    Sleep(20);
    size = sizeof data;
    CHECK(!net.ReceiveKeyPacket(data, &size, &address, &port) && size == 256);
    // Random names: lowercase, 32 at most.
    std::srand(5);
    const std::string eight = RandomName(8);
    bool lower = eight.size() == 8;
    for (char c : eight)
        lower = lower && c >= 'a' && c <= 'z';
    CHECK(lower && std::strlen(RandomName(40)) == 32 && std::strlen(RandomName(33)) == 32 && std::strlen(RandomName(32)) == 32 && std::strlen(RandomName(0)) == 0 && RandomName(3) == g_randomName);
    // Unique names: the first free number (case ignored; the number's suffix from 2).
    net.m_players.clear();
    Named(net, {"pat", "PAT2", "kim"});
    CHECK(net.FindFreeNameNumber("pat", 1) == 3 && net.FindFreeNameNumber("bob", 1) == 1 &&
          net.FindFreeNameNumber("kim", 2) == 2 && net.FindFreeNameNumber("Kim", 0) == 2);
    // GameSpy's verdicts.
    OnKeyChecked(750, 3, 1, nullptr, nullptr);
    char used[] = "CD Key in use";
    char bad[] = "Invalid";
    OnKeyChecked(750, 4, 0, used, nullptr);
    OnKeyChecked(750, 5, 0, bad, nullptr);
    CHECK(g_netGame.m_keyQueries[3].state == KEY_GOOD && g_netGame.m_keyQueries[4].state == KEY_IN_USE &&
          g_netGame.m_keyQueries[5].state == KEY_BAD);
    // A check dropped (GameSpy told; only 0..99).
    net.m_keyQueries[7].state = KEY_GOOD;
    g_log.clear();
    net.DropKeyQuery(7);
    net.DropKeyQuery(-1);
    net.DropKeyQuery(100);
    CHECK(net.m_keyQueries[7].state == KEY_FREE && g_log.size() == 1 && g_log[0] == "gcd drop 750 7");
    net.m_keyQueries[0].state = KEY_GOOD;
    net.DropKeyQuery(0);
    CHECK(net.m_keyQueries[0].state == KEY_FREE && Logged("gcd drop 750 0"));
    closesocket(peer);
    closesocket(net.m_keySocket);
    net.m_keySocket = static_cast<uintptr_t>(-1);
    net.m_players.clear();
}

void TestKeyServer()
{
    C_NetGame &net = Fresh();
    g_testMp.port = TestPort(20);
    net.OpenKeyServer();
    const uint32_t loopback = inet_addr("127.0.0.1");
    SOCKET peer = RawSocket(0);
    const uint16_t peerPort = SocketPort(peer);
    const int now = static_cast<int>(GetPerfTimeMs());
    auto setQuery = [&](int i, int state, int expiry, int lastSend) {
        S_KeyQuery &query = net.m_keyQueries[i];
        query.state = state;
        query.expiry = expiry;
        query.lastSend = lastSend;
        query.address = loopback;
        query.port = peerPort;
        std::strcpy(query.name, "n");
        std::strcpy(query.challenge, "chal");
    };
    // The due ones sent (challenged: "c:<challenge>"; the verdicts m, e, u), each once per 200 ms.
    setQuery(0, KEY_CHALLENGED, now + 5000, now - 300);
    net.ProcessKeyQueries();
    CHECK(RawReceive(peer) == std::string("c:chal", 7) && net.m_keyQueries[0].lastSend >= now);
    net.ProcessKeyQueries();
    CHECK(RawReceive(peer).empty());
    for (int state : {KEY_GOOD, KEY_BAD, KEY_IN_USE, KEY_CHECKING}) {
        setQuery(0, state, now + 5000, static_cast<int>(GetPerfTimeMs()) - 300);
        net.ProcessKeyQueries();
        const std::string got = RawReceive(peer);
        CHECK(got == (state == KEY_GOOD ? "m" : state == KEY_BAD ? "e" : state == KEY_IN_USE ? "u" : ""));
    }
    setQuery(0, KEY_GOOD, now + 5000, static_cast<int>(GetPerfTimeMs()) - 150);
    net.ProcessKeyQueries();
    CHECK(RawReceive(peer).empty());
    // Expired: cleared (not a good one: 15 s more, then GameSpy told; nor a kept one).
    setQuery(1, KEY_CHALLENGED, now - 10, now);
    setQuery(2, KEY_GOOD, now - 10, now);
    setQuery(3, KEY_GOOD, now - 16000, now);
    setQuery(4, KEY_KEPT, now - 16000, now);
    g_log.clear();
    net.ProcessKeyQueries();
    CHECK(net.m_keyQueries[1].lastSend == now && net.m_keyQueries[3].lastSend == now);
    CHECK(net.m_keyQueries[1].state == KEY_FREE && net.m_keyQueries[1].name[0] == 0 &&
          net.m_keyQueries[2].state == KEY_GOOD && net.m_keyQueries[3].state == KEY_FREE &&
          net.m_keyQueries[3].name[0] == 0 && net.m_keyQueries[4].state == KEY_KEPT && Logged("gcd drop 750 3") &&
          g_log.size() == 1);
    for (S_KeyQuery &query : net.m_keyQueries) {
        query = S_KeyQuery();
        query.answered = 1;
    }
    // A player asking: challenged in the first free check (its name made unique: a player "pat" is there).
    net.m_players.clear();
    Named(net, {"pat"});
    net.m_keyQueries[0].state = KEY_GOOD;
    net.m_keyQueries[0].expiry = now + 5000;
    net.m_keyQueries[0].lastSend = now;
    std::srand(7);
    RawSend(peer, TestPort(22), std::string("c:pat", 6));
    Sleep(20);
    net.ProcessKeyQueries();
    const S_KeyQuery &asked = net.m_keyQueries[1];
    CHECK(asked.state == KEY_CHALLENGED && std::strcmp(asked.name, "pat2") == 0 && asked.address == loopback &&
          asked.port == peerPort && std::strlen(asked.challenge) == 8 && asked.answered == 0 &&
          asked.expiry > now + 14000 && asked.lastSend >= now);
    // Asking again: refreshed, not another.
    const_cast<S_KeyQuery &>(asked).expiry = static_cast<int>(GetPerfTimeMs()) + 1000;
    const_cast<S_KeyQuery &>(asked).lastSend = 5;
    RawSend(peer, TestPort(22), std::string("c:pat", 6));
    Sleep(20);
    net.ProcessKeyQueries();
    CHECK(asked.expiry > now + 14000 && asked.lastSend >= now && net.m_keyQueries[2].state == KEY_FREE);
    // A name no player has: kept as it is; a long one cut to its room.
    RawSend(peer, TestPort(22), std::string("c:bob", 6));
    Sleep(20);
    net.ProcessKeyQueries();
    CHECK(net.m_keyQueries[2].state == KEY_CHALLENGED && std::strcmp(net.m_keyQueries[2].name, "bob") == 0);
    RawSend(peer, TestPort(22), "c:" + std::string(70, 'l') + std::string(1, '\0'));
    Sleep(20);
    net.ProcessKeyQueries();
    CHECK(std::strlen(net.m_keyQueries[3].name) == 0x33);
    // A one-letter response.
    g_log.clear();
    RawSend(peer, TestPort(22), std::string("r:R:bob", 8));
    Sleep(20);
    net.ProcessKeyQueries();
    CHECK(net.m_keyQueries[2].state == KEY_CHECKING && net.m_keyQueries[2].answered == 1);
    g_netGame.m_keyQueries[2].state = KEY_FREE;
    // The response: handed to GameSpy (the challenge, the response with the name), answered and checking; GameSpy's
    // verdict set at once here.
    g_gcdAuthenticated = 1;
    g_log.clear();
    RawSend(peer, TestPort(22), std::string("r:RESP:pat", 11));
    Sleep(20);
    net.ProcessKeyQueries();
    char line[128];
    std::snprintf(line, sizeof line, "gcd check 750 1 %08x %s RESP:pat", loopback, asked.challenge);
    CHECK(Logged(line) && asked.answered == 1);
    CHECK(g_netGame.m_keyQueries[1].state == KEY_GOOD);
    g_netGame.m_keyQueries[1].state = KEY_FREE;
    // Not challenged, another name, no separator: nothing.
    g_log.clear();
    RawSend(peer, TestPort(22), std::string("r:RESP:pat", 11));
    RawSend(peer, TestPort(22), std::string("r:RESP:bob", 11));
    RawSend(peer, TestPort(22), std::string("r:RESP", 7));
    for (int i = 0; i < 3; ++i) {
        Sleep(20);
        net.ProcessKeyQueries();
    }
    CHECK(g_log.empty());
    // The table full: nothing.
    for (S_KeyQuery &query : net.m_keyQueries) {
        query.state = KEY_KEPT;
        query.lastSend = now + 100000;
    }
    RawSend(peer, TestPort(22), std::string("c:zed", 6));
    Sleep(20);
    net.ProcessKeyQueries();
    bool none = true;
    for (const S_KeyQuery &query : net.m_keyQueries)
        none = none && std::strcmp(query.name, "zed") != 0;
    CHECK(none);
    for (S_KeyQuery &query : net.m_keyQueries)
        query = S_KeyQuery();
    closesocket(peer);
    closesocket(net.m_keySocket);
    net.m_keySocket = static_cast<uintptr_t>(-1);
    net.m_players.clear();
}

// The server's side run on a thread of its own while the client waits (RunKeyQuery).
struct S_KeyServerThread {
    C_NetGame *server;
    volatile bool stop;
};
DWORD WINAPI KeyServerLoop(void *context)
{
    auto *thread = static_cast<S_KeyServerThread *>(context);
    while (!thread->stop) {
        thread->server->ProcessKeyQueries();
        Sleep(5);
    }
    return 0;
}

int RunExchange(C_NetGame &client, uint16_t port)
{
    // (The server is g_netGame: GameSpy's verdict goes to its checks.)
    C_NetGame *server = &g_netGame;
    for (S_KeyQuery &query : server->m_keyQueries)
        query = S_KeyQuery();
    g_testMp.port = port;
    server->OpenKeyServer();
    S_ServerEntry entry;
    entry.address = inet_addr("127.0.0.1");
    entry.port = port;
    client.m_joinStatus = 0;
    client.StartKeyQuery(&entry);
    S_KeyServerThread thread = {server, false};
    HANDLE handle = CreateThread(nullptr, 0, KeyServerLoop, &thread, 0, nullptr);
    const bool good = client.RunKeyQuery();
    thread.stop = true;
    WaitForSingleObject(handle, INFINITE);
    CloseHandle(handle);
    closesocket(server->m_keySocket);
    closesocket(client.m_keySocket);
    return good ? 1 : client.m_joinStatus * 10;
}

void TestKeyExchange()
{
    C_NetGame &client = Fresh();
    g_gameProfile.m_player = TestPlayerProfile("pat");
    // A good key: "m"; the client's response from its CD key and the challenge.
    g_gcdAuthenticated = 1;
    g_log.clear();
    CHECK(RunExchange(client, TestPort(30)) == 1);
    CHECK(std::strncmp(client.m_keyClient.response, "KEY+", 4) == 0 && std::strlen(client.m_keyClient.response) == 12 &&
          client.m_keyClient.state == KEY_CHALLENGED && client.m_keyClient.port == TestPort(32));
    bool checked = false;
    for (const std::string &line : g_log)
        checked = checked || (line.rfind("gcd check 750 0 ", 0) == 0 &&
                              line.find(std::string(client.m_keyClient.response) + ":pat") != std::string::npos);
    CHECK(checked);
    // Bad: the join status 5; in use: 6.
    g_gcdAuthenticated = 0;
    std::strcpy(g_gcdMessage, "Bad key");
    CHECK(RunExchange(client, TestPort(40)) == 50);
    std::strcpy(g_gcdMessage, "CD in use");
    CHECK(RunExchange(client, TestPort(50)) == 60);
    g_gcdAuthenticated = 1;
    g_gcdMessage[0] = 0;
    // No answer by the expiry: timed out (8).
    S_ServerEntry nowhere;
    nowhere.address = inet_addr("127.0.0.1");
    nowhere.port = TestPort(60);
    client.StartKeyQuery(&nowhere);
    CHECK(client.m_keyClient.state == KEY_ASKING && client.m_keyClient.address == inet_addr("127.0.0.1") &&
          client.m_keyClient.lastSend >= static_cast<int>(GetPerfTimeMs()) - 100 &&
          client.m_keyClient.expiry > static_cast<int>(GetPerfTimeMs()) + 14000);
    client.m_keyClient.expiry = static_cast<int>(GetPerfTimeMs()) - 1;
    CHECK(!client.RunKeyQuery() && client.m_joinStatus == NET_JOIN_TIMED_OUT);
    closesocket(client.m_keySocket);
    client.m_keySocket = static_cast<uintptr_t>(-1);
    client.m_joinStatus = 0;
    g_gameProfile.m_player = nullptr;
}

void RunNetSearchTests()
{
    std::printf("> Browser\n");
    TestBrowser();
    std::printf("> BrowserServers\n");
    TestBrowserServers();
    std::printf("> Reporting\n");
    TestReporting();
    std::printf("> KeyPackets\n");
    TestKeyPackets();
    std::printf("> KeyServer\n");
    TestKeyServer();
    std::printf("> KeyExchange\n");
    TestKeyExchange();
}
