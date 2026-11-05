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
        s->unknown21C = "x";
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
        ok &= s->sessionType == 0 && s->password.IsEmpty() && std::strcmp(s->unknown21C.c_str(), "\x01\x02\x03") == 0 &&
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
