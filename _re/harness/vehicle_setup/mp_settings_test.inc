// Part 9i-ii's session settings (game/mp_settings.cpp: S_MultiplayerSettings' methods), in the factory build, run by
// ControlsTest after HeroStatsTest (the deletes' hook MmDeleteHook). Prints "mpsettings:" with 1 per check met.

struct S_MpSettingsResults {
    bool msReset = true, msRules = true, msSetup = true, msApply = true, msRemove = true, msAdd = true, msClear = true,
         msSelect = true;
};

void MpSettingsTest(I3D_scene *)
{
    S_MpSettingsResults r;
    S_MultiplayerSettings *const s = g_mpSettings;
    const S_MultiplayerSettings old = *s;
    s->maps.clear();
    s->setup = nullptr;
    const auto names = [&]() {
        std::string out;
        for (const C_MpMap *map : s->maps)
            out += std::string(map->GetName()) + " ";
        return out;
    };
    {
        // Reset: no session, the password empty, its +0x21C "\1\2\3", no listing, not dedicated, the server entry reset,
        // no name, game type 0, port 11001, the rules' defaults, the first map current, the maps deleted.
        bool ok = true;
        s->sessionType = 2;
        s->password = "pw";
        s->adminPassword = "x";
        s->unknown178 = 5;
        s->unknown174 = 1;
        s->dedicated = true;
        auto *entry = reinterpret_cast<S_ServerEntry *>(s->unknown17C);
        entry->ping = 5;
        std::strcpy(s->sessionName, "Session");
        s->sessionName[0x167] = 'z';
        s->gameType = 3;
        s->port = 1;
        s->currentMap = 7;
        S_MpGameRules garbage;
        std::memset(&garbage, 0x55, sizeof garbage);
        s->SetRules(garbage);
        auto *a = new C_MpMap(C_str("a"), C_str("da")), *b = new C_MpMap(C_str("b"), C_str("db"));
        s->maps = {a, b};
        g_mmWatch = {a, b};
        g_mmDeleted.clear();
        g_deleteHook = &MmDeleteHook;
        s->Reset();
        g_deleteHook = nullptr;
        ok &= s->sessionType == 0 && s->password.IsEmpty() && std::strcmp(s->adminPassword.c_str(), "\x01\x02\x03") == 0 &&
              s->unknown178 == 0 && s->unknown174 == 0 && !s->dedicated && entry->ping == 9999 && entry->gameType == 4 &&
              s->sessionName[0] == 0 && s->sessionName[0x167] == 0 && s->gameType == 0 && s->port == 11001 &&
              s->currentMap == 0 && s->maps.empty() && g_mmDeleted.size() == 2;
        S_MpGameRules got;
        s->GetRules(got);
        const int want[22] = {32, 20, 0, 3, 30, 10, 1, 1, 1, 0, 1, 100, 1, 1, 1, 1, 0, 3, -1, 1, 0, 1};
        ok &= std::memcmp(&got, want, sizeof want) == 0;
        g_mmWatch.clear();
        g_mmDeleted.clear();
        WM(r.msReset, ok);
    }
    {
        // The rules into the session's fields in S_MpGameRules' order, and back.
        bool ok = true;
        int values[22];
        for (int i = 0; i < 22; ++i)
            values[i] = 100 + i;
        S_MpGameRules rules;
        std::memcpy(&rules, values, sizeof values);
        s->SetRules(rules);
        ok &= s->maxClients == 100 && s->pointLimit == 101 && s->roundLimit == 102 && s->roundCount == 103 && s->warmup == 104 &&
              s->respawnTime == 105 && s->allowRespawn == 106 && s->friendlyFire == 107 && s->autoTeamBalance == 108 &&
              s->thirdPersonView == 109 && s->spawnProtection == 110 && s->inverseDamage == 111 && s->fallingDamage == 112 &&
              s->unknown25C == 113 && s->unknown260 == 114 && s->allowVehicles == 115 && s->autoRestart == 116 &&
              s->coopDifficulty == 117 && s->coopLives == 118 && s->allowCrosshair == 119 && s->spawnOnStart == 120 &&
              s->teamLives == 121;
        S_MpGameRules back;
        s->GetRules(back);
        ok &= std::memcmp(&back, &rules, sizeof rules) == 0;
        WM(r.msRules, ok);
    }
    {
        // The set-up made (its map list read) and deleted.
        bool ok = true;
        const std::string list = "<MAP_LIST>\n<GAMESTYLE type=\"deathmatch\">\n<MAP name=\"Oasis\" dir=\"o_mp\">\n</MAP>\n"
                                 "</GAMESTYLE>\n</MAP_LIST>\n";
        g_files["mpmaplist.txt"].assign(list.begin(), list.end());
        s->MakeSetup();
        ok &= s->setup != nullptr && s->setup->loaded && s->setup->maps[0].maps.size() == 1 &&
              std::string(s->setup->maps[0].maps[0]->GetName()) == "Oasis";
        g_files.erase("mpmaplist.txt");
        C_MpGameSetup *made = s->setup;
        g_mmWatch = {made};
        g_mmDeleted.clear();
        g_deleteHook = &MmDeleteHook;
        s->DeleteSetup();
        g_deleteHook = nullptr;
        ok &= !s->setup && g_mmDeleted == std::vector<void *>({made});
        g_mmWatch.clear();
        g_mmDeleted.clear();
        WM(r.msSetup, ok);
    }
    // A set-up of the tests' (no map list read).
    C_MpGameSetup setup;
    for (auto &group : setup.maps) {
        for (C_MpMap *map : group.maps)
            delete map;
        group.maps.clear();
    }
    s->setup = &setup;
    {
        // The rules of the session's game type from the set-up.
        bool ok = true;
        setup.rules[2].maxClients = 12;
        setup.rules[2].teamLives = 7;
        setup.rules[1].maxClients = 99;
        s->gameType = 2;
        setup.gameType = 0;
        s->ApplySetupRules();
        ok &= setup.gameType == 2 && s->maxClients == 12 && s->teamLives == 7;
        WM(r.msApply, ok);
    }
    {
        // A map removed by name (any case; not the current one; none: false).
        bool ok = true;
        auto *a = new C_MpMap(C_str("Alpha"), C_str("da")), *b = new C_MpMap(C_str("Beta"), C_str("db")),
             *c = new C_MpMap(C_str("Gamma"), C_str("dc"));
        s->maps = {a, b, c};
        s->currentMap = 0;
        g_mmWatch = {a, b, c};
        g_mmDeleted.clear();
        g_deleteHook = &MmDeleteHook;
        ok &= s->RemoveMap("bETA") && names() == "Alpha Gamma " && g_mmDeleted == std::vector<void *>({b});
        ok &= !s->RemoveMap("alpha") && !s->RemoveMap("Delta") && names() == "Alpha Gamma ";
        s->currentMap = -1;
        ok &= s->RemoveMap("alpha") && names() == "Gamma ";
        s->currentMap = 1;
        ok &= s->RemoveMap("Gamma") && names() == "";
        g_deleteHook = nullptr;
        g_mmWatch.clear();
        g_mmDeleted.clear();
        WM(r.msRemove, ok);
    }
    {
        // A map added from the set-up's maps of the session's game type (the last of the name, copied); the set-up not
        // read, another game type, none of the name: false.
        bool ok = true;
        C_MpMap one(C_str("Oasis"), C_str("o1")), two(C_str("OASIS"), C_str("o2")), other(C_str("Bridge"), C_str("b1"));
        setup.maps[1].maps = {&one, &other, &two};
        setup.loaded = true;
        s->gameType = 1;
        setup.gameType = 3;
        ok &= s->AddMap("oasis") && s->maps.size() == 1 && s->maps[0] != &two &&
              std::string(s->maps[0]->GetDirectory()) == "o2" && setup.gameType == 1;
        ok &= s->AddMap("bridge") && names() == "OASIS Bridge ";
        ok &= !s->AddMap("Nowhere") && s->maps.size() == 2;
        // (Other types the set-up's last list: still false.)
        C_MpMap coop(C_str("Oasis"), C_str("c1")), zero(C_str("Zero"), C_str("z1"));
        setup.maps[3].maps = {&coop};
        s->gameType = 4;
        ok &= !s->AddMap("oasis");
        s->gameType = -1;
        ok &= !s->AddMap("oasis");
        // Game type 0; the list's first map.
        setup.maps[0].maps = {&zero};
        s->gameType = 0;
        ok &= s->AddMap("ZERO") && names() == "OASIS Bridge Zero " && setup.gameType == 0;
        setup.maps[0].maps.clear();
        setup.maps[3].maps.clear();
        ok &= s->maps.size() == 3;
        delete s->maps.back();
        s->maps.pop_back();
        s->gameType = 1;
        setup.loaded = false;
        ok &= !s->AddMap("oasis") && s->maps.size() == 2;
        setup.maps[1].maps.clear();
        WM(r.msAdd, ok);
    }
    {
        // Cleared: the maps deleted, none current, the set-up deleted.
        bool ok = true;
        auto *made = new C_MpGameSetup();
        s->setup = made;
        g_mmWatch = {s->maps[0], s->maps[1], made};
        g_mmDeleted.clear();
        g_deleteHook = &MmDeleteHook;
        s->Clear();
        g_deleteHook = nullptr;
        ok &= s->maps.empty() && s->currentMap == -1 && !s->setup && g_mmDeleted.size() == 3 && g_mmDeleted.back() == made;
        g_mmWatch.clear();
        g_mmDeleted.clear();
        WM(r.msClear, ok);
    }
    {
        // Selected: by name (any case; none: false, unchanged), or the next one round; the current one.
        bool ok = true;
        C_MpMap a(C_str("Alpha"), C_str("da")), b(C_str("Beta"), C_str("db")), c(C_str("Gamma"), C_str("dc"));
        s->maps = {&a, &b, &c};
        s->currentMap = 0;
        ok &= s->SelectMap("GAMMA") && s->currentMap == 2 && s->GetCurrentMap() == &c;
        ok &= s->SelectMap("alpha") && s->currentMap == 0 && s->SelectMap("Gamma") && s->currentMap == 2;
        ok &= !s->SelectMap("Delta") && s->currentMap == 2;
        ok &= s->SelectMap(nullptr) && s->currentMap == 0 && s->GetCurrentMap() == &a;
        ok &= s->SelectMap(nullptr) && s->currentMap == 1;
        s->currentMap = -1;
        ok &= s->SelectMap(nullptr) && s->currentMap == 0;
        s->maps.clear();
        s->currentMap = 4;
        ok &= s->SelectMap(nullptr) && s->currentMap == 0 && !s->SelectMap("Alpha");
        WM(r.msSelect, ok);
    }
    s->setup = nullptr;
    s->maps.clear();
    *s = old;
    g_gfLog.clear();
    std::printf("mpsettings: msReset %d msRules %d msSetup %d msApply %d msRemove %d msAdd %d msClear %d msSelect %d\n",
                r.msReset, r.msRules, r.msSetup, r.msApply, r.msRemove, r.msAdd, r.msClear, r.msSelect);
}

// (Part 9i-iii: the replication's tests, in the factory build: the strings of the fake streams, as item_test.inc's,
// which the factory build does not have.)
#ifndef ITEM_TEST
std::vector<std::string> g_itStrings;
void C_BitStream::WriteString(const char *text)
{
    g_itStrings.push_back(text);
    reinterpret_cast<S_FakeStream *>(this)->bits.emplace_back(static_cast<uint32_t>(g_itStrings.size() - 1), -6);
}
void C_BitStream::ReadString(char *buffer, int bufferSize)
{
    buffer[0] = 0;
    if (g_readPos >= g_readBits.size() || g_readBits[g_readPos].second != -6) {
        g_readMismatch = true;
        return;
    }
    const std::string &text = g_itStrings.at(g_readBits[g_readPos++].first);
    std::snprintf(buffer, static_cast<size_t>(bufferSize), "%s", text.c_str());
}
#endif

struct S_ReplResults {
    bool rpWriteRules = true, rpReadRules = true, rpWriteMap = true, rpReadMap = true, rpCurrentMap = true;
};

void ReplicationTest()
{
    S_ReplResults r;
    S_MultiplayerSettings *const s = g_mpSettings;
    const S_MultiplayerSettings old = *s;
    s->maps.clear();
    const auto bits = [](const S_FakeStream &stream) {
        std::string out;
        for (const auto &b : stream.bits)
            out += CmText("%x/%d ", b.first, b.second);
        return out;
    };
    const int oldFreq = g_netMaxFreq, oldPing = g_netMaxPing, oldInactivity = g_netMaxInactivity;
    {
        // The rules: the listing and the game type (8 bits), the 22 rules (16), the net limits (16; the inactivity in s).
        // (The limits start at 0.)
        bool ok = g_netMaxFreq == 0 && g_netMaxPing == 0 && g_netMaxInactivity == 0;
        s->unknown174 = 1;
        s->gameType = 3;
        int values[22];
        for (int i = 0; i < 22; ++i)
            values[i] = 200 + i;
        values[18] = -1;
        S_MpGameRules rules;
        std::memcpy(&rules, values, sizeof values);
        s->SetRules(rules);
        g_netMaxFreq = 25;
        g_netMaxPing = 600;
        g_netMaxInactivity = 30500;
        S_FakeStream stream{0, false, {}, false};
        s->WriteRules(reinterpret_cast<C_BitStream *>(&stream));
        std::string want = "1/8 3/8 ";
        for (int i = 0; i < 22; ++i)
            want += CmText("%x/16 ", static_cast<uint32_t>(values[i]));
        want += "19/16 258/16 1e/16 ";
        ok &= bits(stream) == want;
        if (!ok)
            std::printf("REPL rules %s\n", bits(stream).c_str());
        WM(r.rpWriteRules, ok);
    }
    {
        // Read back: 8 bits unsigned, the rules and limits sign-extended, the inactivity in ms.
        bool ok = true;
        g_readBits = {{0xFF, 8}, {2, 8}};
        for (int i = 0; i < 22; ++i)
            g_readBits.emplace_back(i == 3 ? 0xFFFE : 300 + i, 16);
        g_readBits.emplace_back(0xFFFF, 16);
        g_readBits.emplace_back(700, 16);
        g_readBits.emplace_back(45, 16);
        g_readPos = 0;
        g_readMismatch = false;
        s->ReadRules(nullptr);
        S_MpGameRules got;
        s->GetRules(got);
        int values[22];
        std::memcpy(values, &got, sizeof values);
        ok &= !g_readMismatch && g_readPos == g_readBits.size() && s->unknown174 == 255 && s->gameType == 2 &&
              values[0] == 300 && values[3] == -2 && values[21] == 321 && g_netMaxFreq == -1 && g_netMaxPing == 700 &&
              g_netMaxInactivity == 45000;
        WM(r.rpReadRules, ok);
    }
    // A map with objectives and items.
    C_MpMap map(C_str("Oasis"), C_str("oasis_mp"));
    map.aircraftAxis = 1;
    map.aircraftAllied = -1;
    map.balancedAxis = 3;
    map.balancedAllied = 4;
    map.frontline = 1;
    map.description = -5;
    map.presets = "p1;p2";
    map.objectives = {{11, 12, 13, 1, 1, 14}, {21, 22, -23, -1, 2, -1}};
    map.items = {{101, 2, 30}, {-2, 255, 1}};
    S_FakeStream written{0, false, {}, false};
    {
        // Written: the name, the directory (strings), five bytes, the description (16), the presets; the objectives
        // (8: their count; each: allied, axis, points 16, type 8, team 2, the VIP's uniform 16); the items (8; each: id
        // 16, count and ammo 8).
        bool ok = true;
        g_itStrings.clear();
        map.Write(reinterpret_cast<C_BitStream *>(&written));
        ok &= bits(written) == "0/-6 1/-6 1/8 ffffffff/8 3/8 4/8 1/8 fffffffb/16 2/-6 2/8 c/16 b/16 d/16 1/8 1/2 e/16 "
                               "16/16 15/16 ffffffe9/16 ffffffff/8 2/2 ffffffff/16 2/8 65/16 2/8 1e/8 fffffffe/16 ff/8 1/8 " &&
              g_itStrings == std::vector<std::string>({"Oasis", "oasis_mp", "p1;p2"});
        if (!ok)
            std::printf("REPL map %s\n", bits(written).c_str());
        WM(r.rpWriteMap, ok);
    }
    {
        // Read back (the stream's values as the real stream masks them): the bytes unsigned, the shorts signed.
        bool ok = true;
        g_readBits.clear();
        for (const auto &b : written.bits)
            g_readBits.emplace_back(b.second > 0 && b.second < 32 ? b.first & ((1u << b.second) - 1u) : b.first, b.second);
        g_readPos = 0;
        g_readMismatch = false;
        C_MpMap got(C_str("x"), C_str("y"));
        got.objectives = {{1, 1, 1, 1, 1, 1}};
        got.items = {{9, 9, 9}};
        got.Read(nullptr);
        ok &= !g_readMismatch && g_readPos == g_readBits.size() && std::string(got.GetName()) == "Oasis" &&
              std::string(got.GetDirectory()) == "oasis_mp" && got.aircraftAxis == 1 && got.aircraftAllied == 255 &&
              got.balancedAxis == 3 && got.balancedAllied == 4 && got.frontline == 1 && got.description == -5 &&
              std::string(got.GetPresets()) == "p1;p2" && got.objectives.size() == 2 && got.items.size() == 2;
        if (got.objectives.size() == 2 && got.items.size() == 2) {
            const S_MpMapObjective &o = got.objectives[1];
            ok &= got.objectives[0].axisText == 11 && got.objectives[0].alliedText == 12 && got.objectives[0].points == 13 &&
                  got.objectives[0].type == 1 && got.objectives[0].team == 1 && got.objectives[0].vipUniform == 14 &&
                  o.axisText == 21 && o.alliedText == 22 && o.points == -23 && o.type == 255 && o.team == 2 &&
                  o.vipUniform == -1 && got.items[0].itemId == 101 && got.items[0].count == 2 && got.items[0].ammo == 30 &&
                  got.items[1].itemId == -2 && got.items[1].count == 255 && got.items[1].ammo == 1;
        }
        WM(r.rpReadMap, ok);
    }
    {
        // The current map written; read: the old maps deleted, the one read the current.
        bool ok = true;
        C_MpMap other(C_str("Other"), C_str("o"));
        s->maps = {&other, &map};
        s->currentMap = 1;
        S_FakeStream stream{0, false, {}, false};
        g_itStrings.clear();
        s->WriteCurrentMap(reinterpret_cast<C_BitStream *>(&stream));
        ok &= bits(stream) == bits(written);
        auto *a = new C_MpMap(C_str("a"), C_str("a")), *b = new C_MpMap(C_str("b"), C_str("b"));
        s->maps = {a, b};
        s->currentMap = 1;
        g_mmWatch = {a, b};
        g_mmDeleted.clear();
        g_deleteHook = &MmDeleteHook;
        g_readBits.clear();
        for (const auto &x : written.bits)
            g_readBits.emplace_back(x.second > 0 && x.second < 32 ? x.first & ((1u << x.second) - 1u) : x.first, x.second);
        g_readPos = 0;
        g_readMismatch = false;
        s->ReadCurrentMap(nullptr);
        g_deleteHook = nullptr;
        ok &= g_mmDeleted == std::vector<void *>({a, b}) && s->maps.size() == 1 && s->currentMap == 0 &&
              std::string(s->maps[0]->GetName()) == "Oasis" && s->maps[0]->objectives.size() == 2 && !g_readMismatch;
        for (C_MpMap *m : s->maps)
            delete m;
        s->maps.clear();
        g_mmWatch.clear();
        g_mmDeleted.clear();
        WM(r.rpCurrentMap, ok);
    }
    g_readBits.clear();
    g_readPos = 0;
    g_itStrings.clear();
    g_netMaxFreq = oldFreq;
    g_netMaxPing = oldPing;
    g_netMaxInactivity = oldInactivity;
    *s = old;
    std::printf("replication: rpWriteRules %d rpReadRules %d rpWriteMap %d rpReadMap %d rpCurrentMap %d\n", r.rpWriteRules,
                r.rpReadRules, r.rpWriteMap, r.rpReadMap, r.rpCurrentMap);
}

// (Part 9i-iv.) The session's description file: written to a file of the test's, read back.
std::string ReadTextFile(const char *name)
{
    std::string text;
    if (FILE *f = std::fopen(name, "rt")) {
        char buffer[512];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0)
            text.append(buffer, n);
        std::fclose(f);
    }
    return text;
}

void DescriptionTest()
{
    bool ok = true;
    S_MultiplayerSettings *const s = g_mpSettings;
    const S_MultiplayerSettings old = *s;
    const int oldFreq = g_netMaxFreq, oldPing = g_netMaxPing, oldInactivity = g_netMaxInactivity;
    C_MpMap a(C_str("Oasis"), C_str("o")), b(C_str("Bridge"), C_str("b"));
    s->maps = {&a, &b};
    std::memset(s->sessionName, 0, sizeof s->sessionName);
    std::strcpy(s->sessionName, "My Game");
    s->gameType = 2;
    s->unknown174 = 1;
    s->dedicated = true;
    s->port = 11002;
    s->password = "secret";
    int values[22];
    for (int i = 0; i < 22; ++i)
        values[i] = i * 3 - 1;
    S_MpGameRules rules;
    std::memcpy(&rules, values, sizeof values);
    s->SetRules(rules);
    g_netMaxFreq = 20;
    g_netMaxPing = 500;
    g_netMaxInactivity = 61999;
    s->unknown178 = 4;
    const char *const file = "mpdesc_test.txt";
    s->WriteDescription(file);
    std::string want = "sessionname \"My Game\"\nstyle objectives\nmapname Oasis\nmapname Bridge\ndomain internet\n"
                       "dedicated 1\nport 11002\npassword \"secret\"\n";
    const char *const keys[13] = {"maxclients", "pointlimit", "roundlimit", "roundcount", "warmup", "respawntime",
                                  "allowrespawn", "friendlyfire", "autoteambalance", "3rdpersonview", "spawnprotection",
                                  "inversedamage", "fallingdmg"};
    for (int i = 0; i < 13; ++i)
        want += CmText("%s %d\n", keys[i], values[i]);
    want += CmText("maxfreq 20\nmaxping 500\nmaxinactivity 61\nallowvehicles %d\nautorestart %d\ncoopdifficulty %d\n"
                   "cooplives %d\nallowcrosshair %d\nspawnonstart %d\nteamlives %d\nvoicechat truespeech\nserver\n",
                   values[15], values[16], values[17], values[18], values[19], values[20], values[21]);
    const std::string got = ReadTextFile(file);
    ok &= got == want;
    if (!ok)
        std::printf("DESC got\n%s\nDESC want\n%s\n", got.c_str(), want.c_str());
    // A dedicated LAN server: no port; no codec (0) or past them: no voice line; other game types, domains: empty.
    s->unknown174 = 0;
    s->unknown178 = 0;
    s->gameType = 0;
    s->WriteDescription(file);
    std::string lan = ReadTextFile(file);
    ok &= lan.find("port ") == std::string::npos && lan.find("voicechat") == std::string::npos &&
          lan.find("style deathmatch\n") != std::string::npos && lan.find("domain local\n") != std::string::npos;
    s->dedicated = false;
    s->unknown178 = 8;
    s->gameType = 4;
    s->unknown174 = 2;
    s->WriteDescription(file);
    lan = ReadTextFile(file);
    ok &= lan.find("port 11002\n") != std::string::npos && lan.find("voicechat") == std::string::npos &&
          lan.find("style \n") != std::string::npos && lan.find("domain \n") != std::string::npos &&
          lan.find("dedicated 0\n") != std::string::npos;
    s->unknown178 = 1;
    s->gameType = 3;
    s->unknown174 = 0;
    s->WriteDescription(file);
    lan = ReadTextFile(file);
    ok &= lan.find("voicechat vr12\n") != std::string::npos && lan.find("style cooperative\n") != std::string::npos &&
          lan.find("port 11002\n") != std::string::npos;
    s->unknown178 = 7;
    s->gameType = -1;
    s->unknown174 = -1;
    s->WriteDescription(file);
    lan = ReadTextFile(file);
    ok &= lan.find("voicechat pcm\nserver\n") != std::string::npos && lan.find("style \n") != std::string::npos &&
          lan.find("domain \n") != std::string::npos;
    std::remove(file);
    // No file opened (a directory's name): nothing written.
    s->WriteDescription(".");
    s->maps.clear();
    *s = old;
    g_netMaxFreq = oldFreq;
    g_netMaxPing = oldPing;
    g_netMaxInactivity = oldInactivity;
    std::printf("description: dsWrite %d\n", ok);
}
