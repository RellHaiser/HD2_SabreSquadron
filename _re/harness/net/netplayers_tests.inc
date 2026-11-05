// (Part 10d-ii.) C_NetGame's players, teams and scores (net/net_players.cpp). Included by netgame_test.cpp after
// netgame_tests.inc (its Fresh, Player, MakeHuman).

void Named(C_NetGame &net, std::initializer_list<const char *> names)
{
    int connection = 10;
    for (const char *name : names) {
        net.m_players.push_back(Player(connection, name));
        connection += 10;
    }
}

void TestNetStats()
{
    // The score in 11 bits (signed past 0x400), kills, deaths, the ping (clamped in place) in 10, the rank in 5.
    S_NetStats stats{{0, 5, 7, -3, 2000, 4, 0, 0, 0}};
    C_GrowingBitStream w;
    stats.Write(&w);
    CHECK(stats[4] == 0x3FF && w.BitPosition() == 46u);
    S_NetStats back;
    w.ResetPosition();
    back.Read(&w);
    CHECK(back[1] == 5 && back[2] == 7 && back[3] == -3 && back[4] == 0x3FF && back[5] == 4 && back[0] == 0 &&
          back[6] == 0);
    S_NetStats edges[3] = {{{0, 0, 0, 0x400, -1, 0, 0, 0, 0}}, {{0, 0, 0, 0x401, 0x3FF, 31, 0, 0, 0}},
                           {{0, 1, 2, -1, 0, 0, 0, 0, 0}}};
    C_GrowingBitStream e;
    for (S_NetStats &s : edges)
        s.Write(&e);
    CHECK(edges[0][4] == 0x3FF && edges[1][4] == 0x3FF && edges[2][4] == 0);
    e.ResetPosition();
    S_NetStats r[3];
    for (S_NetStats &s : r)
        s.Read(&e);
    CHECK(r[0][3] == 0x400 && r[1][3] == 0x401 - 0x800 && r[1][5] == 31 && r[2][3] == -1 && r[2][1] == 1 && r[2][2] == 2);
}

void TestKills()
{
    C_NetGame &net = Fresh();
    g_gameProfile.m_mpSettings = &g_testMp;
    g_testMp.gameType = MP_GAME_OBJECTIVES;
    g_cooperative = false;
    g_gameRole = GAME_ROLE_SERVER;
    g_localPlayerSlot = 5;
    Named(net, {"a", "b", "c", "d"});
    net.m_players[0].team = 0;
    net.m_players[1].team = 1;
    net.m_players[2].team = 0;
    net.m_players[3].team = 1;
    for (int i = 0; i < 3; ++i)
        net.m_players[static_cast<size_t>(i)].human = MakeHuman(false);
    g_controllerTypes[net.m_players[2].human] = HUMAN_CONTROLLER_REMOTE;
    std::memset(net.m_teamStats, 0, sizeof net.m_teamStats);
    std::memset(net.m_teamScores, 0, sizeof net.m_teamScores);
    // A kill: the victim's deaths, the killer's kills and score, the teams'; objectives: the killer's team a point.
    net.OnPlayerKilled(1, 0);
    const int *a = net.m_players[0].stats;
    const int *b = net.m_players[1].stats;
    CHECK(b[2] == 1 && b[7] == 1 && b[3] == 0 && net.m_teamStats[1][2] == 1 && net.m_teamStats[1][7] == 1);
    CHECK(a[1] == 1 && a[6] == 1 && a[3] == 1 && a[8] == 1 && a[2] == 0 && net.m_teamStats[0][1] == 1 &&
          net.m_teamStats[0][6] == 1 && net.m_teamStats[0][3] == 1 && net.m_teamStats[0][8] == 1 &&
          net.m_teamScores[0] == 1.0f && net.m_teamScores[1] == 0.0f);
    // A suicide: a point off.
    net.OnPlayerKilled(1, 1);
    CHECK(b[2] == 2 && b[3] == -1 && b[8] == -1 && b[1] == 0);
    // The ranks sent again (the victim's after a suicide; the killer's and the victim's after a kill).
    net.m_players[1].stats[5] = 3;
    g_log.clear();
    net.OnPlayerKilled(1, 1);
    CHECK(Logged("rank 0") && net.m_players[1].stats[5] == 0);
    net.m_players[0].stats[5] = 4;
    net.m_players[1].stats[5] = 5;
    g_log.clear();
    net.OnPlayerKilled(1, 0);
    CHECK(net.m_players[0].stats[5] == 0 && net.m_players[1].stats[5] == 0 && g_log.size() == 2);
    net.m_players[1].stats[2] = 2;
    net.m_players[1].stats[7] = 2;
    net.m_players[1].stats[3] = -1;
    net.m_players[1].stats[8] = -1;
    net.m_players[0].stats[1] = 1;
    net.m_players[0].stats[6] = 1;
    net.m_players[0].stats[3] = 1;
    net.m_players[0].stats[8] = 1;
    net.m_teamStats[1][2] = 2;
    net.m_teamStats[1][7] = 2;
    net.m_teamStats[0][1] = 1;
    net.m_teamStats[0][6] = 1;
    net.m_teamStats[0][3] = 1;
    net.m_teamStats[0][8] = 1;
    net.m_teamScores[0] = 1.0f;
    // A killer not a player's own controller (remote), none, out of range: only the death.
    net.OnPlayerKilled(1, 2);
    net.OnPlayerKilled(1, -1);
    net.OnPlayerKilled(1, 4);
    CHECK(b[2] == 5 && net.m_players[2].stats[1] == 0 && net.m_teamStats[1][2] == 5);
    // Team kills: 10 off; the third kicks (not the server's own player).
    g_controllerTypes.erase(net.m_players[2].human);
    net.m_players[2].stats[3] = 30;
    g_log.clear();
    net.OnPlayerKilled(0, 2);
    net.OnPlayerKilled(0, 2);
    const int *c = net.m_players[2].stats;
    CHECK(c[3] == 10 && c[8] == -20 && net.m_players[2].teamKills == 2 && net.m_teamStats[0][3] == -19 &&
          net.m_teamStats[0][8] == -19 && c[1] == 0 && !Logged("game 17 to 0 a 1 b 2 0"));
    net.OnPlayerKilled(0, 2);
    CHECK(net.m_players[2].teamKills == 3 && Logged("game 17 to 0 a 1 b 2 0"));
    g_localPlayerSlot = 2;
    g_log.clear();
    net.OnPlayerKilled(0, 2);
    CHECK(!Logged("game 17 to 0 a 1 b 2 0"));
    g_gameRole = GAME_ROLE_CLIENT;
    g_localPlayerSlot = 5;
    net.OnPlayerKilled(0, 2);
    CHECK(!Logged("game 17 to 0 a 1 b 2 0") && net.m_players[2].teamKills == 5);
    // A deathmatch: any kill; occupation: no team point.
    g_gameRole = GAME_ROLE_SERVER;
    g_testMp.gameType = MP_GAME_DEATHMATCH;
    net.OnPlayerKilled(0, 2);
    CHECK(c[1] == 1 && net.m_players[2].teamKills == 5);
    g_testMp.gameType = MP_GAME_OCCUPATION;
    const float axisScore = net.m_teamScores[0];
    net.OnPlayerKilled(1, 0);
    CHECK(net.m_teamScores[0] == axisScore && a[1] == 2);
    // Not a player: in co-op its killer scores (an ally or neutral: 10 off); a client counts too.
    auto *enemy = reinterpret_cast<C_Human *>(MakeHuman(false));
    enemy->m_role = HUMAN_ROLE_ENEMY;
    auto *neutral = reinterpret_cast<C_Human *>(MakeHuman(false));
    neutral->m_role = HUMAN_ROLE_NEUTRAL;
    auto *ally = reinterpret_cast<C_Human *>(MakeHuman(false));
    ally->m_role = HUMAN_ROLE_NPC_TEAM;
    auto *enemyNpc = reinterpret_cast<C_Human *>(MakeHuman(false));
    enemyNpc->m_role = HUMAN_ROLE_NPC_TEAM_5;
    g_actors[40] = enemy;
    g_actors[41] = neutral;
    g_actors[42] = ally;
    g_actors[43] = enemyNpc;
    net.OnPlayerKilled(40, 0);
    CHECK(a[1] == 2);
    g_cooperative = true;
    const int score = a[3];
    net.OnPlayerKilled(40, 0);
    CHECK(a[1] == 3 && a[6] == 3 && a[3] == score + 1 && a[8] == score + 1);
    net.OnPlayerKilled(44, 0);                       // (no actor)
    CHECK(a[1] == 4);
    for (int npc : {41, 42, 43})
        net.OnPlayerKilled(npc, 0);
    CHECK(a[1] == 4 && a[3] == score - 28 && a[8] == score - 28);
    g_actors[4] = enemy;
    const int kills = a[1];
    net.OnPlayerKilled(4, 0);
    CHECK(a[1] == kills + 1);
    a = net.m_players[0].stats;
    net.m_players[0].stats[1] = kills;
    net.OnPlayerKilled(-1, 0);
    net.OnPlayerKilled(40, -1);
    net.OnPlayerKilled(40, 4);
    CHECK(a[1] == 5);
    g_cooperative = false;
    g_actors.clear();
}

void TestRanks()
{
    C_NetGame &net = Fresh();
    g_gameRole = GAME_ROLE_SERVER;
    Named(net, {"a", "b", ""});
    net.m_players[0].human = MakeHuman(false);
    net.m_players[2].human = MakeHuman(false);
    // The rank: the thresholds (10 .. 80) reached; sent by the player's controller when new.
    const int scores[][2] = {{9, 0}, {10, 1}, {19, 1}, {20, 2}, {69, 6}, {70, 7}, {80, 7}, {500, 7}, {-5, 0}};
    bool ranks = true;
    for (const auto &score : scores) {
        net.m_players[0].stats[3] = score[0];
        net.m_players[0].stats[5] = 9;
        net.SendPlayerScore(0, false);
        ranks = ranks && net.m_players[0].stats[5] == score[1];
    }
    CHECK(ranks);
    g_log.clear();
    net.SendPlayerScore(0, true);
    CHECK(g_log.empty());
    net.m_players[0].stats[3] = 35;
    net.SendPlayerScore(0, true);
    CHECK(g_log.size() == 1 && g_log[0] == "rank 3");
    // No human, no name, a client: nothing.
    net.m_players[1].stats[3] = 50;
    net.m_players[2].stats[3] = 50;
    net.SendScores();
    CHECK(net.m_players[1].stats[5] == 0 && net.m_players[2].stats[5] == 0);
    net.m_players[0].stats[3] = 45;
    g_gameRole = GAME_ROLE_CLIENT;
    net.SendPlayerScore(0, false);
    CHECK(net.m_players[0].stats[5] == 3);
    g_gameRole = GAME_ROLE_SERVER;
    net.SendScores();
    CHECK(net.m_players[0].stats[5] == 4);
    // Its change printed: the local player's (down: red, 0x22C4; up: green, 0x22C5), the others' with the name.
    g_testMp.gameType = MP_GAME_OBJECTIVES;
    g_localPlayerSlot = 0;
    g_log.clear();
    net.SetPlayerRank(2, 0);
    CHECK(net.m_players[0].stats[5] == 2 && Logged("print 255 0 0 T22C4 T279E"));
    net.SetPlayerRank(2, 0);
    CHECK(Logged("print 0 255 0 T22C5 T279E"));
    net.m_players[1].team = 1;
    net.SetPlayerRank(3, 1);
    net.SetPlayerRank(1, 1);
    CHECK(Logged("print 0 255 0 b T22C7 T2795") && Logged("print 255 0 0 b T22C6 T2793"));
    // A deathmatch: the uniform's side (allied: the allies' names; none or another: the others').
    g_testMp.gameType = MP_GAME_DEATHMATCH;
    alignas(16) static unsigned char defStorage[sizeof(C_ItemDef)];
    alignas(16) static unsigned char itemStorage[sizeof(C_Item)];
    auto *def = reinterpret_cast<C_ItemDef *>(defStorage);
    auto *item = reinterpret_cast<C_Item *>(itemStorage);
    item->m_def = def;
    struct T_Uniform {
        void **vtable;
        C_Item *item;
    };
    static void *uniformVtable[32];
    static T_Uniform uniform;
    struct Getter {
        static C_Item *__attribute__((thiscall)) GetItem(T_Uniform *self) { return self->item; }
    };
    for (void *&slot : uniformVtable)
        slot = reinterpret_cast<void *>(&Getter::GetItem);
    uniform.vtable = uniformVtable;
    uniform.item = item;
    static C_ItemContainer *containers[7];
    containers[ITEM_CONTAINER_UNIFORM] = reinterpret_cast<C_ItemContainer *>(&uniform);
    net.m_players[0].human->m_containers = containers;
    def->m_team = SIDE_ALLIES;
    net.m_players[0].team = 0;
    CHECK(std::strcmp(net.m_players[0].GetRankName().c_str(), "T2794") == 0);
    def->m_team = SIDE_AXIS;
    CHECK(std::strcmp(net.m_players[0].GetRankName().c_str(), "T279E") == 0);
    uniform.item = nullptr;
    net.m_players[0].team = 1;
    CHECK(std::strcmp(net.m_players[0].GetRankName().c_str(), "T279E") == 0);
    net.m_players[0].human->m_containers = nullptr;
    // Not a deathmatch: the player's team.
    g_testMp.gameType = MP_GAME_OCCUPATION;
    CHECK(std::strcmp(net.m_players[0].GetRankName().c_str(), "T2794") == 0);
    net.m_players[0].human = nullptr;
    g_testMp.gameType = MP_GAME_DEATHMATCH;
    CHECK(std::strcmp(net.m_players[0].GetRankName().c_str(), "T2794") == 0);
    // The stats' accessors.
    CHECK(net.GetPlayerStats(1) == net.m_players[1].stats && net.GetLocalPlayerStats() == net.m_players[0].stats);
    g_localPlayerSlot = 0;
}

void TestKicks()
{
    C_NetGame &net = Fresh();
    Named(net, {"a", "b"});
    net.m_players.push_back(Player(0, "c"));
    g_gameRole = GAME_ROLE_SERVER;
    // Told to everyone (the server) and printed with the reason's text; none for a free connection.
    const char *texts[4] = {"app print T22AB", "app print T22AA", "app print T22AD", "app print T22AC"};
    bool printed = true;
    for (int reason = 0; reason < 4; ++reason) {
        g_log.clear();
        net.KickPlayer(1, reason);
        char line[64];
        std::snprintf(line, sizeof line, "game 17 to 0 a %d b 1 0", reason);
        printed = printed && Logged(line) && Logged(texts[reason]) && g_log.size() == 2;
    }
    CHECK(printed);
    g_log.clear();
    net.KickPlayer(0, 7);
    CHECK(Logged("game 17 to 0 a 7 b 0 0") && Logged("app print a kicked because unknow(new?) reason"));
    g_log.clear();
    net.KickPlayer(2, 1);
    CHECK(g_log.empty());
    g_gameRole = GAME_ROLE_CLIENT;
    net.KickPlayer(1, 1);
    CHECK(g_log.size() == 1 && g_log[0] == "app print T22AA");
    // By name.
    g_gameRole = GAME_ROLE_SERVER;
    g_log.clear();
    net.KickPlayerByName("b", 3);
    CHECK(Logged("game 17 to 0 a 3 b 1 0"));
    g_log.clear();
    net.KickPlayerByName("a", 2);
    CHECK(Logged("game 17 to 0 a 2 b 0 0"));
    g_log.clear();
    net.KickPlayerByName("z", 2);
    CHECK(g_log.empty());
}

void TestScoresMessage()
{
    C_NetGame &net = Fresh();
    T_Server server;
    net.m_server = &server;
    g_cooperative = true;
    g_testMp.teamLives = 0;
    Named(net, {"host", "pat", "", "kim", "l", "sam"});
    net.m_players[0].human = MakeHuman(false);
    net.m_players[1].human = MakeHuman(false);
    net.m_players[4].human = MakeHuman(false);
    int livesUsed[6] = {-1, 2, 127, 0, 200, 5};
    for (int i = 0; i < 6; ++i)
        net.m_players[static_cast<size_t>(i)].livesUsed = livesUsed[i];
    int sent[9] = {0, 4, 2, -7, 0, 3, 0, 0, 0};
    std::memcpy(net.m_players[1].stats, sent, sizeof sent);
    net.m_players[0].stats[3] = 11;
    net.m_players[0].stats[4] = 99;
    net.m_players[4].stats[3] = 22;
    auto *game = reinterpret_cast<C_game *>(g_gameStorage);
    g_game = game;
    game->m_roundTime = 5000;
    game->m_missionTime = 9000;
    game->m_teamLivesUsed = 3;
    net.m_teamStats[0][3] = 5;
    net.m_teamStats[1][1] = 6;
    net.m_teamScores[0] = 12.7f;
    net.m_teamScores[1] = 3.0f;
    net.m_teamWins[0] = 2;
    net.m_teamWins[1] = 1;
    // A client: nothing written.
    C_GrowingBitStream none;
    g_gameRole = GAME_ROLE_CLIENT;
    net.WriteScores(&none);
    CHECK(none.BitPosition() == 0u);
    // The server playing: its own ping 0; the others' half DirectPlay's round trip (their ids x 10).
    g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    C_GrowingBitStream w;
    net.WriteScores(&w);
    CHECK(net.m_players[0].stats[4] == 0 && net.m_players[1].stats[4] == 100 && net.m_players[3].stats[4] == 200);
    // (3 x (6 + 46) + 6, the times 64, six lives x 7, the teams' 2 x 46 + 2 x 10 + 2 x 8.)
    CHECK(w.BitPosition() == 396u);
    // Read by a client: the players with a human there take the stats; the times, the lives, the teams'.
    auto *client = new C_NetGame;
    client->Init();
    for (int i = 0; i < 6; ++i) {
        std::strcpy(client->m_players[static_cast<size_t>(i)].name, "x");
        client->m_players[static_cast<size_t>(i)].human = MakeHuman(false);
    }
    client->m_players[4].human = nullptr;
    client->m_players[4].stats[3] = 77;
    client->m_teamWins[2] = 9;
    w.ResetPosition();
    g_gameRole = GAME_ROLE_CLIENT;
    game->m_missionTime = 0;
    game->m_time = 0;
    game->m_teamLivesUsed = 9;
    g_log.clear();
    client->ReadScores(&w);
    const int *p1 = client->m_players[1].stats;
    CHECK(p1[1] == 4 && p1[2] == 2 && p1[3] == -7 && p1[4] == 100 && p1[5] == 3 && client->m_players[0].stats[3] == 11 &&
          client->m_players[0].stats[4] == 0 && client->m_players[4].stats[3] == 77 && client->m_players[3].stats[3] == 0);
    CHECK(game->m_missionTime == 9000 && game->m_time == 9000 && Logged("round time 5000") && game->m_teamLivesUsed == 0);
    // (-1 goes as 0x7E; 0x7F comes back as -1; over 0x7F: 0x7E.)
    CHECK(client->m_players[0].livesUsed == 126 && client->m_players[1].livesUsed == 2 &&
          client->m_players[2].livesUsed == -1 && client->m_players[4].livesUsed == 126 &&
          client->m_players[5].livesUsed == 5 && client->m_players[6].livesUsed == 0);
    CHECK(client->m_teamStats[0][3] == 5 && client->m_teamStats[1][1] == 6 && client->m_teamScores[0] == 12.0f &&
          client->m_teamScores[1] == 3.0f && client->m_teamWins[0] == 2 && client->m_teamWins[1] == 1 &&
          client->m_teamWins[2] == 9 && w.BitPosition() == w.ByteSize() * 8u - (w.ByteSize() * 8u - w.BitPosition()));
    // Shared lives: the team's count (16 bits, signed).
    g_testMp.teamLives = 1;
    game->m_teamLivesUsed = -2;
    game->m_missionTime = 9000;
    g_gameRole = GAME_ROLE_SERVER;
    C_GrowingBitStream t;
    net.WriteScores(&t);
    t.ResetPosition();
    g_gameRole = GAME_ROLE_CLIENT;
    game->m_teamLivesUsed = 0;
    client->m_players[1].livesUsed = 33;
    client->ReadScores(&t);
    CHECK(game->m_teamLivesUsed == -2 && client->m_players[1].livesUsed == 33);
    // Not co-op: no lives; the server reads into nothing; no mission time: the game left alone.
    g_cooperative = false;
    g_gameRole = GAME_ROLE_SERVER;
    C_GrowingBitStream u;
    net.m_players[1].stats[3] = 40;
    net.WriteScores(&u);
    u.ResetPosition();
    game->m_teamLivesUsed = 4;
    game->m_missionTime = 1;
    client->ReadScores(&u);
    CHECK(client->m_players[1].stats[3] == -7 && game->m_missionTime == 1 && client->m_teamWins[0] == 2);
    game->m_missionTime = 0;
    C_GrowingBitStream v;
    net.WriteScores(&v);
    v.ResetPosition();
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    client->ReadScores(&v);
    CHECK(client->m_players[1].stats[3] == 40 && !Logged("round time 5000") && game->m_teamLivesUsed == 4);
    // No game: zeros sent (the shared lives too).
    g_game = nullptr;
    g_gameRole = GAME_ROLE_SERVER;
    g_cooperative = true;
    g_testMp.teamLives = 1;
    C_GrowingBitStream z;
    net.WriteScores(&z);
    z.ResetPosition();
    while (z.ReadBits(6) != 0x21) {
        z.ReadBits(23);
        z.ReadBits(23);
    }
    CHECK(z.ReadBits(32) == 0 && z.ReadBits(32) == 0 && z.ReadBits(16) == 0);
    g_cooperative = false;
    g_game = game;
    net.m_server = nullptr;
    g_testMp.teamLives = 0;
}

void TestTeams()
{
    C_NetGame &net = Fresh();
    g_gameRole = GAME_ROLE_SERVER;
    Named(net, {"a", "b", "c", "d", "", "f"});
    net.m_players[1].connection = 0;
    const int teams[6] = {0, 1, 0, 1, 0, 2};
    const int pending[6] = {1, 1, 0, 0, 1, 0};
    for (int i = 0; i < 6; ++i) {
        net.m_players[static_cast<size_t>(i)].team = teams[i];
        net.m_players[static_cast<size_t>(i)].pendingTeam = pending[i];
    }
    net.m_players[0].human = MakeHuman(false);
    net.m_players[3].human = MakeHuman(false);
    net.m_players[4].human = MakeHuman(false);
    // Counted: the named (the free slot 4 not), by team or the next team, with a human or not.
    int axis = -1, allies = -1;
    net.CountTeams(&axis, &allies, false, false);
    CHECK(axis == 2 && allies == 2);
    net.CountTeams(&axis, &allies, true, false);
    CHECK(axis == 3 && allies == 2);
    net.CountTeams(&axis, &allies, false, true);
    CHECK(axis == 1 && allies == 1);
    net.CountTeams(&axis, &allies, true, true);
    CHECK(axis == 1 && allies == 1);
    net.CountTeamPlayers(&axis, &allies);
    CHECK(axis == 1 && allies == 1);
    CHECK(net.CountPlayers() == 4);
    // Balanced: within 1.5 of the share.
    CHECK(net.TeamsBalanced(1, 1, false) && net.TeamsBalanced(1, 1, true) && net.TeamsBalanced(1, 2, false) &&
          !net.TeamsBalanced(2, 1, false) && net.TeamsBalanced(7, 4, false) && !net.TeamsBalanced(9, 5, false) &&
          net.TeamsBalanced(5, 4, true) && !net.TeamsBalanced(1, 4, true));
    // The teams, the next ones.
    CHECK(net.GetPlayerTeam(1, false) == 1 && net.GetPlayerTeam(0, true) == 1 && net.GetPlayerTeam(6, false) == 2 &&
          net.GetPlayerTeam(-1, true) == 2);
    net.SetPlayerTeam(2, 1, true);
    net.SetPlayerTeam(2, 2, false);
    net.SetPlayerTeam(6, 1, false);
    CHECK(net.m_players[2].pendingTeam == 1 && net.m_players[2].team == 2 && net.m_players.size() == 6);
    net.ApplyPendingTeams();
    CHECK(net.m_players[0].team == 1 && net.m_players[2].team == 1 && net.m_players[3].team == 0 &&
          net.m_players[4].team == 0 && net.m_players[5].team == 0);
    // The local player's.
    g_localPlayerSlot = 3;
    CHECK(net.GetLocalTeam() == 0 && net.GetLocalHuman() == net.m_players[3].human);
    g_localPlayerSlot = 6;
    CHECK(net.GetLocalHuman() == nullptr);
    g_localPlayerSlot = 0;
    // Spectating: no human; a player's (or remote) controller that watches; not for a free slot or past the end.
    CHECK(net.IsPlayerSpectating(1) && !net.IsPlayerSpectating(0) && !net.IsPlayerSpectating(4) &&
          !net.IsPlayerSpectating(6) && !net.IsPlayerSpectating(-1));
    reinterpret_cast<T_Controller *>(net.m_players[0].human->m_controller)->spectating = true;
    CHECK(net.IsPlayerSpectating(0));
    g_controllerTypes[net.m_players[0].human] = HUMAN_CONTROLLER_REMOTE;
    CHECK(net.IsPlayerSpectating(0));
    g_controllerTypes[net.m_players[0].human] = HUMAN_CONTROLLER_AI;
    CHECK(!net.IsPlayerSpectating(0));
    g_controllerTypes.clear();
    // A team alive: a named player of it with a living human not watching.
    net.m_players[3].human->m_stats[STAT_HEALTH] = 10;
    CHECK(net.IsTeamAlive(0) && !net.IsTeamAlive(1) && !net.IsTeamAlive(2));
    net.m_players[3].human->m_stats[STAT_HEALTH] = 0;
    net.m_players[0].human->m_stats[STAT_HEALTH] = 5;
    CHECK(!net.IsTeamAlive(0) && !net.IsTeamAlive(1));
    reinterpret_cast<T_Controller *>(net.m_players[0].human->m_controller)->spectating = false;
    CHECK(net.IsTeamAlive(1));
    C_HumanController *controller = net.m_players[0].human->m_controller;
    net.m_players[0].human->m_controller = nullptr;
    CHECK(!net.IsTeamAlive(1));
    net.m_players[0].human->m_controller = controller;
    net.m_players[4].human->m_stats[STAT_HEALTH] = 5;
    CHECK(!net.IsTeamAlive(0));
    // The team scores (the server's).
    net.m_teamScores[0] = 0.0f;
    net.m_teamScores[1] = 1.5f;
    net.AddTeamScore(1, 2.0f);
    g_gameRole = GAME_ROLE_CLIENT;
    net.AddTeamScore(1, 2.0f);
    CHECK(net.m_teamScores[1] == 3.5f);
    // Printed (nothing in the release build): each player with a human, the kills by team.
    net.m_players[0].stats[1] = 3;
    net.m_players[3].stats[1] = 4;
    g_humanTeams[net.m_players[0].human] = 1;
    g_log.clear();
    net.PrintTeams();
    CHECK(Logged("game print               a 1\n") && Logged("game print               d 0\n") &&
          Logged("colored 200 Kills  Axis:  4 Allied:  3\n") && Logged("colored 200 Score  Axis:  0 Allied:  4\n") &&
          g_log.size() == 4);
    g_humanTeams.clear();
}

void TestBalance()
{
    C_NetGame &net = Fresh();
    // Team 0 against the share of the players (truncated): moved from the last slot while over one off.
    auto setup = [&](std::initializer_list<int> teams) {
        net.m_players.clear();
        int i = 0;
        for (int team : teams) {
            net.m_players.push_back(Player(10 + i, "p"));
            net.m_players.back().team = team;
            net.m_players.back().pendingTeam = 7;
            ++i;
        }
    };
    setup({1, 1, 1, 1, 1, 1});
    net.BalanceTeams(0, 50);
    CHECK(net.m_players[5].team == 0 && net.m_players[5].pendingTeam == 0 && net.m_players[4].team == 0 &&
          net.m_players[3].team == 0 && net.m_players[2].team == 1 && net.m_players[2].pendingTeam == 7);
    setup({0, 0, 0, 0, 0, 1});
    net.BalanceTeams(99, 50);
    CHECK(net.m_players[4].team == 1 && net.m_players[4].pendingTeam == 1 && net.m_players[3].team == 1 &&
          net.m_players[2].team == 0 && net.m_players[5].team == 1);
    // One off: left.
    setup({1, 1, 1, 0, 0});
    net.BalanceTeams(0, 50);
    CHECK(net.m_players[2].team == 1 && net.m_players[2].pendingTeam == 7);
    setup({0, 0, 1, 1});
    net.BalanceTeams(0, 75);
    CHECK(net.m_players[3].pendingTeam == 7);
    setup({0, 0, 0, 1});
    net.BalanceTeams(0, 25);
    CHECK(net.m_players[2].team == 1 && net.m_players[1].team == 1 && net.m_players[0].team == 0 &&
          net.m_players[3].team == 1);
    setup({0, 0, 1, 1});
    net.BalanceTeams(0, 50);
    CHECK(net.m_players[3].pendingTeam == 7 && net.m_players[1].pendingTeam == 7 && net.m_players[1].team == 0);
    setup({0, 0, 0});
    net.BalanceTeams(0, 0);
    CHECK(net.m_players[0].team == 1 && net.m_players[1].team == 1 && net.m_players[2].team == 1 &&
          net.m_players[0].pendingTeam == 1);
    setup({1, 1, 1});
    net.BalanceTeams(0, 100);
    CHECK(net.m_players[0].team == 0 && net.m_players[2].team == 0);
    // (0.99 x 3 truncates to 2.)
    setup({1, 1, 1});
    net.BalanceTeams(0, 99);
    CHECK(net.m_players[2].team == 0 && net.m_players[1].team == 0 && net.m_players[0].team == 1);
    // The free slots not counted nor moved.
    setup({1, 1, 1, 1});
    net.m_players[3].name[0] = 0;
    net.m_players[2].connection = 0;
    net.BalanceTeams(0, 100);
    CHECK(net.m_players[3].team == 1 && net.m_players[2].team == 0 && net.m_players[1].team == 0 &&
          net.m_players[0].team == 1);
}

void TestAdminsAndLives()
{
    g_netGame.m_players.clear();
    Named(g_netGame, {"a", "b", ""});
    g_netGame.m_players[1].admin = true;
    g_netGame.m_players[2].admin = true;
    CHECK(!IsPlayerAdmin(0) && IsPlayerAdmin(1) && !IsPlayerAdmin(2));
    // Respawns: not co-op: the rule; co-op: the lives (each player's or the team's; -1: no limit).
    g_testMp.allowRespawn = 1;
    g_cooperative = false;
    CHECK(CanPlayerRespawn(0));
    g_testMp.allowRespawn = 0;
    CHECK(!CanPlayerRespawn(0));
    g_cooperative = true;
    g_testMp.teamLives = 0;
    g_testMp.coopLives = 3;
    g_netGame.m_players[0].livesUsed = 2;
    g_netGame.m_players[1].livesUsed = 3;
    CHECK(CanPlayerRespawn(0) && !CanPlayerRespawn(1));
    g_testMp.coopLives = -1;
    CHECK(CanPlayerRespawn(1));
    g_testMp.teamLives = 1;
    g_testMp.coopLives = 5;
    auto *game = reinterpret_cast<C_game *>(g_gameStorage);
    g_game = game;
    game->m_teamLivesUsed = 4;
    CHECK(CanPlayerRespawn(1));
    game->m_teamLivesUsed = 5;
    CHECK(!CanPlayerRespawn(0));
    g_testMp.coopLives = -1;
    CHECK(CanPlayerRespawn(0));
    g_cooperative = false;
    g_testMp.teamLives = 0;
    g_netGame.m_players.clear();
}

void TestServerStatus()
{
    C_NetGame &net = Fresh();
    Named(net, {"a", "", "c"});
    net.m_players[0].human = MakeHuman(false);
    g_humanTeams[net.m_players[0].human] = 1;
    for (int i = 0; i < 9; ++i)
        net.m_players[0].stats[i] = i + 1;
    net.m_players[2].stats[3] = 9;
    S_MultiplayerSettings &mp = g_testMp;
    int *fields[] = {&mp.maxClients, &mp.pointLimit, &mp.roundLimit, &mp.roundCount, &mp.warmup, &mp.respawnTime,
                     &mp.allowRespawn, &mp.friendlyFire, &mp.autoTeamBalance, &mp.thirdPersonView, &mp.spawnProtection,
                     &mp.fallingDamage, &mp.unknown25C, &mp.allowVehicles, &mp.coopDifficulty, &mp.coopLives,
                     &mp.allowCrosshair, &mp.spawnOnStart, &mp.teamLives, &mp.inverseDamage};
    int value = 100;
    for (int *field : fields)
        *field = value++;
    net.m_teamScores[0] = 7.9f;
    net.m_teamScores[1] = 2.2f;
    S_ServerStatus status;
    status.players.resize(5);
    net.FillServerStatus(&status);
    CHECK(status.players.size() == 2 && std::strcmp(status.players[0].name, "a") == 0 && status.players[0].kills == 2 &&
          status.players[0].points == 4 && status.players[0].team == 1 && status.players[0].unknown00 == 1 &&
          reinterpret_cast<int *>(&status.players[0])[8] == 9 && std::strcmp(status.players[1].name, "c") == 0 &&
          status.players[1].team == 2 && status.players[1].points == 9);
    const int *got[] = {&status.maxClients, &status.fragLimit, &status.roundLimit, &status.roundCount,
                        &status.preparationTime, &status.respawnTime, &status.allowRespawn, &status.friendlyFire,
                        &status.autoTeamBalance, &status.thirdPersonView, &status.spawnProtectionTime,
                        &status.fallingDamage, &status.unknown40, &status.allowVehicles, &status.difficulty,
                        &status.lives, &status.allowCrosshair, &status.spawnOnStart, &status.teamLives,
                        &status.inverseDamage};
    bool same = true;
    value = 100;
    for (const int *field : got)
        same = same && *field == value++;
    CHECK(same && status.axisScore == 7 && status.alliedScore == 2);
    g_humanTeams.clear();
    for (int *field : fields)
        *field = 0;
}

void TestSessionAndQueues()
{
    C_NetGame &net = Fresh();
    C_NetSocket socket;
    C_NetConnection a(1, 0, &socket), b(2, 0, &socket), server(3, 0, &socket);
    Named(net, {"a", "b", ""});
    net.m_players[0].link = &a;
    net.m_players[0].joinStage = 2;
    net.m_players[1].link = &b;
    net.m_players[1].joinStage = 1;
    net.m_players[2].link = &b;
    net.m_players[2].joinStage = 2;
    // The server: each joined player's bit flipped, its connection's following.
    g_gameRole = GAME_ROLE_SERVER;
    net.FlipSessionParity();
    CHECK(net.m_players[0].session == 1 && a.m_session == 1 && net.m_players[1].session == 0 && b.m_session == 0 &&
          net.m_players[2].session == 0);
    net.FlipSessionParity();
    CHECK(net.m_players[0].session == 0 && a.m_session == 0);
    // A client: its own, the server's link following.
    g_gameRole = GAME_ROLE_CLIENT;
    g_localPlayerSlot = 1;
    net.m_serverLink = &server;
    net.FlipSessionParity();
    CHECK(net.m_players[1].session == 1 && server.m_session == 1 && net.m_players[0].session == 0);
    net.m_serverLink = nullptr;
    g_localPlayerSlot = 0;
    // The replication reset: the queue of the role emptied (its buffers given back); `all`: the objects' messages.
    const size_t buffers = g_netPool.m_buffers.items.size();
    const uint8_t data[2] = {1, 2};
    net.QueueReceived(data, 2, 0);
    net.QueueOutgoing(data, 2, 0);
    net.QueueOutgoing(data, 2, 0);
    net.ResetReplication(false);
    CHECK(net.m_outgoing.empty() && net.m_received.size() == 1);
    g_gameRole = GAME_ROLE_SERVER;
    C_NetReplication replication;
    replication.m_makeFactory = [](int) { return NetMessageFactory(nullptr); };
    replication.m_makeStateReader = [](int) { return NetStateHandler(nullptr); };
    C_NetObject *object = replication.FindObject(1, true);
    struct T_Msg : C_NetObjectMessage {
        T_Msg() : C_NetObjectMessage(1) {}
        void Write(C_BitStream *) override {}
        void Read(C_BitStream *) override {}
    };
    auto *kept = new T_Msg;
    kept->refCount = 5;
    object->m_outgoing.push_back(kept);
    object->m_received.push_back(new T_Msg);
    net.m_replication = &replication;
    net.ResetReplication(true);
    CHECK(net.m_received.empty() && object->m_outgoing.empty() && object->m_received.empty() &&
          g_netPool.m_buffers.items.size() == buffers);
    net.m_replication = nullptr;
    // The connection: lost when the transport heard nothing for over 600 ms.
    net.m_joinStatus = NET_JOIN_JOINING;
    net.m_transport->m_lastReceive = static_cast<int>(GetPerfTimeMs());
    CHECK(net.GetConnectionState() == NET_JOIN_JOINING);
    net.m_transport->m_lastReceive = static_cast<int>(GetPerfTimeMs()) - 590;
    CHECK(net.GetConnectionState() == NET_JOIN_JOINING);
    net.m_transport->m_lastReceive = static_cast<int>(GetPerfTimeMs()) - 601;
    CHECK(net.GetConnectionState() == NET_CONNECTION_LOST);
    net.m_joinStatus = 0;
}

bool SameQuantization(const S_NetQuantization &a, const S_NetQuantization &b)
{
    return std::memcmp(&a, &b, 0x5C) == 0 && std::memcmp(a.scale, b.scale, sizeof a.scale) == 0 &&
           a.unknown70 == b.unknown70;
}

void TestLevelQuantization()
{
    C_NetGame &net = Fresh();
    const float min[4] = {-10.0f, -2.0f, -30.0f, 0.0f};
    const float size[4] = {20.0f, 4.0f, 60.0f, 0.0f};
    // No replication: nothing.
    std::memset(static_cast<void *>(&net.m_quantizations[0]), 0x5A, sizeof net.m_quantizations[0]);
    net.SetLevelBounds(min, size);
    CHECK(reinterpret_cast<unsigned char *>(&net.m_quantizations[0])[0] == 0x5A);
    // The first quantisation set up (steps 0.01, 0.0013, 0.01; 100) and handed to the replication (reading and
    // writing), the table's entry 0 too; the replication enabled.
    C_NetReplication replication;
    net.m_replication = &replication;
    replication.m_enabled = false;
    net.SetLevelBounds(min, size);
    S_NetQuantization expected;
    const S_vector4 span = {20.0f, 4.0f, 60.0f, 0.0f}, from = {-10.0f, -2.0f, -30.0f, 0.0f},
                    step = {0.01f, 0.0013f, 0.01f, 0.0f};
    expected.Setup(span, from, step, 100);
    CHECK(SameQuantization(net.m_quantizations[0], expected) &&
          SameQuantization(g_netQuantizations.m_entries[0], expected) && expected.bits[1] == 12);
    CHECK(replication.m_read[0] == &net.m_quantizations[0] && replication.m_write[0] == &net.m_quantizations[0] &&
          replication.m_enabled);
    net.m_replication = nullptr;
    // The table: ten at first (the defaults in 0 .. 3); doubled once when too small.
    C_NetQuantizationTable table;
    S_NetQuantization angle;
    const S_vector4 angleSpan = {6.29f, 6.29f, 6.29f, 0.0f}, angleFrom = {-3.145f, -3.145f, -3.145f, 0.0f},
                    spinStep = {0.2f, 0.2f, 0.2f, 0.0f};
    angle.Setup(angleSpan, angleFrom, spinStep, 100);
    CHECK(table.m_entries.size() == 10 && SameQuantization(table.m_entries[3], angle));
    const S_vector4 big = {3000.0f, 3000.0f, 3000.0f, 0.0f}, low = {-1500.0f, -1500.0f, -1500.0f, 0.0f},
                    fine = {0.01f, 0.01f, 0.01f, 0.0f};
    S_NetQuantization position;
    position.Setup(big, low, fine, 100);
    CHECK(SameQuantization(table.m_entries[0], position));
    S_NetQuantization turn, rate;
    const S_vector4 turnStep = {0.025f, 0.025f, 0.025f, 0.0f}, rateSpan = {2.0f, 2.0f, 2.0f, 0.0f},
                    rateFrom = {-2.0f, -2.0f, -2.0f, 0.0f}, rateStep = {0.02f, 0.02f, 0.02f, 0.0f};
    turn.Setup(angleSpan, angleFrom, turnStep, 100);
    rate.Setup(rateSpan, rateFrom, rateStep, 100);
    CHECK(SameQuantization(table.m_entries[1], turn) && SameQuantization(table.m_entries[2], rate) &&
          !SameQuantization(table.m_entries[1], angle));
    C_NetQuantizationTable edge;
    edge.Set(10, &span.x, &from.x, &step.x);
    CHECK(edge.m_entries.size() == 20 && SameQuantization(edge.m_entries[10], expected));
    table.Set(12, &span.x, &from.x, &step.x);
    CHECK(table.m_entries.size() == 20 && SameQuantization(table.m_entries[12], expected));
    table.Set(10, &span.x, &from.x, &step.x);
    CHECK(table.m_entries.size() == 20);
}

void RunNetPlayersTests()
{
    std::printf("> NetStats\n");
    TestNetStats();
    std::printf("> Kills\n");
    TestKills();
    std::printf("> Ranks\n");
    TestRanks();
    std::printf("> Kicks\n");
    TestKicks();
    std::printf("> ScoresMessage\n");
    TestScoresMessage();
    std::printf("> Teams\n");
    TestTeams();
    std::printf("> Balance\n");
    TestBalance();
    std::printf("> AdminsAndLives\n");
    TestAdminsAndLives();
    std::printf("> ServerStatus\n");
    TestServerStatus();
    std::printf("> SessionAndQueues\n");
    TestSessionAndQueues();
    std::printf("> LevelQuantization\n");
    TestLevelQuantization();
}
