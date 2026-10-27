// (world_actors_test.inc, inside WorldActorsTest: the explosive place and the ladder.)

    // --- the explosive place ---------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_EXPLOSIVE_PLACE];
        WA(r.explosive, creator && std::strcmp(creator->GetName(), "Explosive place") == 0);
        C_ItTestDef chargeDef, otherDef;
        chargeDef.m_textId = 710;
        otherDef.m_textId = 711;
        g_obDefs[0x44] = &chargeDef;
        g_obDefs[0x48] = &otherDef;
        // ApplyState as the original's (0x0060d380): the frames' SetOn, the time left, the name, the
        // volume, the frame's Update.
        struct S_WaApply {
            std::vector<std::string> log;
            std::string name;
            bool added;
            char op;
            int timeLeft;
        };
        const auto applyOracle = [&](C_ExplosivePlace *a, int state) {
            S_WaApply e{{}, a->m_name.c_str(), a->m_volumeAdded, 0, a->m_timeLeft};
            if (state == a->m_state || !a->m_onFrame || !a->m_offFrame)
                return e;
            const std::string on = ItName(a->m_onFrame), off = ItName(a->m_offFrame), frame = ItName(a->m_frame);
            bool volume = false;
            switch (state) {
            case 0:
                e.log.push_back("on " + on + " 0");
                e.timeLeft = 0;
                if (!a->m_hidden) {
                    e.log.push_back("on " + off + " 1");
                    e.name = "text " + std::to_string(a->m_nameTextId);
                    volume = true;
                } else {
                    e.log.push_back("on " + off + " 0");
                    e.name = "Hidden, you should not see this text!!";
                }
                break;
            case 1: {
                e.log.push_back("on " + on + " 1");
                e.log.push_back("on " + off + " 0");
                e.timeLeft = a->m_delay;
                const auto it = g_obDefs.find(a->m_itemId);
                e.name = "text " + std::to_string(it == g_obDefs.end() || a->m_itemId < 0 || a->m_itemId >= 500
                                                      ? -1
                                                      : it->second->GetTextId());
                volume = true;
                break;
            }
            case 2:
            case 3:
                e.log.push_back("on " + on + " 0");
                e.log.push_back("on " + off + " 0");
                e.timeLeft = 0;
                e.name = state == 2 ? "Exploded, you should not see this text!!" : "Hidden, you should not see this text!!";
                break;
            case 4:
                e.timeLeft = 0;
                e.log.push_back("on " + on + " 1");
                e.log.push_back("on " + off + " 1");
                break;
            default:
                e.log.push_back("update " + frame);
                return e;
            }
            if (volume) {
                if (!e.added)
                    e.op = 'A';
                e.added = true;
            } else {
                if (e.added)
                    e.op = 'R';
                e.added = false;
            }
            e.log.push_back("update " + frame);
            return e;
        };
        const auto applied = [&](C_ExplosivePlace *a, const S_WaApply &e, int state) {
            return a->m_state == state && SaLogIs(e.log) && std::strcmp(a->m_name.c_str(), e.name.c_str()) == 0 &&
                   a->m_volumeAdded == e.added && a->m_timeLeft == e.timeLeft &&
                   (e.op ? g_volumeLog.size() == 1 && g_volumeLog[0].op == e.op && g_volumeLog[0].volume == &a->m_volume
                         : g_volumeLog.empty());
        };
        const auto apply = [&](C_ExplosivePlace *a, int state) {
            const S_WaApply e = applyOracle(a, state);
            reset();
            a->ApplyState(state);
            return applied(a, e, state);
        };
        C_Human planter;
        planter.m_netId = 21;
        C_Item *charge = ObItem(0x44, 300);
        for (int k = 0; k < 3; ++k) {
            I3D_frame *f = placed(FRAME_MODEL, "wa.expl", k != 1);
            boxFrame(f, "b", k);
            // (The frames' names are matched without case, of any type; the third place has no "on"
            // frame.)
            I3D_frame *on = k == 2 ? nullptr : AnFrame(k == 1 ? FRAME_DUMMY : FRAME_VISUAL, "explosive_on", f);
            I3D_frame *off = AnFrame(FRAME_DUMMY, k == 1 ? "EXPLOSIVE_OFF" : "explosive_off", f);
            auto *a = static_cast<C_ExplosivePlace *>(creator->Create());
            WA(r.explosive, a->GetType() == ACTOR_EXPLOSIVE_PLACE && a->GetState() == EXPLOSIVE_STOPPED && a->m_timed &&
                                a->m_nameTextId == 0x1BF9 && a->GetItemId() == 0x44 && a->m_delay == 0 && a->m_sound == -1 &&
                                a->m_effect == -1 && a->m_damage == 0 && a->m_range == 0 && !a->m_startPlaced &&
                                !a->m_hidden && !a->m_onFrame && !a->m_offFrame && a->m_planterId == -1 &&
                                a->GetTimeLeft() == 0 && !a->m_started && !a->m_volumeAdded);
            const int inits = g_actorInits;
            reset();
            WA(r.explosive, a->SetFrame(f) && a->GetFrame() == f && g_actorInits == inits + 1 && g_itLog.empty());
            a->m_delay = 1500 + 100 * k;
            a->m_startPlaced = k == 1;
            a->m_hidden = k == 0;
            a->m_planterId = 9;
            a->m_timeLeft = 8;
            g_gameRole = GAME_ROLE_SERVER;
            // StartMission: who placed it forgotten, the box volume (its centre's w the box's own
            // radius), the frames found, empty or placed.
            reset();
            a->m_onFrame = on;                          // (for the oracle: what StartMission finds)
            a->m_offFrame = off;
            a->m_timeLeft = 0;
            const S_WaApply start = applyOracle(a, k == 1 ? EXPLOSIVE_PLACED : EXPLOSIVE_EMPTY);
            a->m_onFrame = a->m_offFrame = nullptr;
            a->m_timeLeft = 8;
            reset();
            a->StartMission(true);
            WA(r.explosive, a->m_started && a->m_planterId == -1 && a->m_onFrame == on && a->m_offFrame == off &&
                                g_activations == std::vector<bool>{true} &&
                                applied(a, start, k == 1 ? EXPLOSIVE_PLACED : EXPLOSIVE_EMPTY));
            S_CollisionVolume v = a->m_volume;
            const S_SaVolume ev = SaVolumeOracle(f);
            I3D_bbox tmp;
            const I3D_bbox &box = *F_GetHierarchyBBox(nullptr, tmp, f);
            float boxRadius = 0.0f;
            if (box.min.x <= box.max.x) {
                const S_vector half = (box.max - box.min) * 0.5f;
                boxRadius = std::sqrt((half.z * half.z + half.y * half.y) + half.x * half.x);
            }
            WA(r.explosive, v.centerW == boxRadius);
            v.centerW = ev.centerW;
            WA(r.explosive, SaSameVolume(v, a->m_invMatrix, f, COLLISION_TYPE_LIGHT_OBJECT, a));
            // Started: only the activation.
            a->m_planterId = 9;
            a->m_timeLeft = 33;
            reset();
            a->StartMission(false);
            WA(r.explosive, g_activations == std::vector<bool>{false} && g_itLog.empty() && g_volumeLog.empty() &&
                                a->m_planterId == 9 && a->m_timeLeft == 33);
            a->m_timeLeft = a->m_state == EXPLOSIVE_PLACED ? a->m_delay : 0;
            if (k == 2) {
                // Without the "on" frame: the states only stored.
                for (int s = 0; s < 5; ++s)
                    WA(r.explosiveState, apply(a, s));
                // (Nor with only the "on" frame.)
                a->m_onFrame = off;
                a->m_offFrame = nullptr;
                for (int s = 0; s < 5; ++s)
                    WA(r.explosiveState, apply(a, s));
                creator->Destroy(a);
                continue;
            }

            // ApplyState from every state, hidden or not, the volume in or out.
            for (int from = 0; from < 5; ++from) {
                for (int to = 0; to < 6; ++to) {
                    for (int variant = 0; variant < 4; ++variant) {
                        a->m_state = from;
                        a->m_hidden = (variant & 1) != 0;
                        a->m_volumeAdded = (variant & 2) != 0;
                        a->m_timeLeft = 33;
                        a->m_itemId = variant == 3 ? 0x99 : variant == 2 ? 0x48 : 0x44;
                        a->m_name = "before";
                        WA(r.explosiveState, apply(a, to == 5 ? 7 : to));
                    }
                }
            }
            a->m_itemId = 0x44;
            a->m_hidden = false;
            WA(r.explosiveState, apply(a, EXPLOSIVE_EMPTY));

            // Place: only while empty; who placed it noted before the item is looked for; the item (of
            // the place's type) taken, placed, the clients told (kind 2, who, the uid).
            a->m_netId = 50 + k;
            const int before[] = {EXPLOSIVE_EMPTY, EXPLOSIVE_EMPTY, EXPLOSIVE_PLACED, EXPLOSIVE_EXPLODED,
                                  EXPLOSIVE_HIDDEN, EXPLOSIVE_STOPPED, EXPLOSIVE_EMPTY};
            for (int c = 0; c < 7; ++c) {
                C_Human h;
                h.m_netId = 21;
                if (c != 1)
                    h.items.emplace_back(0x44, charge);
                h.items.emplace_back(0x45, ObItem(0x45, 301));
                apply(a, before[c]);
                a->m_planterId = 5;
                if (c == 6)
                    a->m_itemId = 0x45;
                const bool places = c == 0 || c == 6;
                const int uid = c == 6 ? 301 : 300;
                const S_WaApply e = places ? applyOracle(a, EXPLOSIVE_PLACED) : S_WaApply{};
                reset();
                const bool placedOk = a->Place(&h);
                if (places)
                    WA(r.explosiveState, placedOk && applied(a, e, EXPLOSIVE_PLACED) && a->m_planterId == 21 &&
                                             h.removedItems == std::vector<std::pair<int, bool>>{{uid, true}} &&
                                             g_streams.size() == 1 && g_streams[0]->id == a->m_netId && g_streams[0]->sent &&
                                             g_streams[0]->bits == T_Bits{{2, 2}, {21, 10}, {static_cast<uint32_t>(uid), 16}});
                else
                    WA(r.explosiveState, !placedOk && h.removedItems.empty() && g_streams.empty() &&
                                             a->m_planterId == (c == 1 ? 21 : 5) && a->GetState() == before[c]);
                a->m_itemId = 0x44;
            }
            // Disarm: empty, the clients told (kind 1).
            {
                const S_WaApply e = applyOracle(a, EXPLOSIVE_EMPTY);
                reset();
                WA(r.explosiveState, a->Disarm(&planter) && applied(a, e, EXPLOSIVE_EMPTY) && g_streams.size() == 1 &&
                                         g_streams[0]->bits == T_Bits{{1, 2}} && g_streams[0]->sent);
            }
            // SetState: the clients told (kind 3, the state) unless the game is local; nothing when
            // it is the state already.
            const uint8_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT};
            for (uint8_t role : roles) {
                g_gameRole = role;
                for (int s = 0; s < 5; ++s) {
                    const S_WaApply e = applyOracle(a, s);
                    const bool same = s == a->GetState();
                    reset();
                    a->SetState(s);
                    WA(r.explosiveNet, applied(a, e, s) &&
                                           (same || role == GAME_ROLE_LOCAL ? g_streams.empty()
                                                                            : g_streams.size() == 1 &&
                                                                                  g_streams[0]->bits == T_Bits{{3, 2}, {static_cast<uint32_t>(s), 2}} &&
                                                                                  g_streams[0]->id == a->m_netId &&
                                                                                  g_streams[0]->sent));
                    reset();
                    a->SetState(s);
                    WA(r.explosiveNet, g_streams.empty() && g_itLog.empty() && a->GetState() == s);
                }
            }

            // The countdown: a timed placed charge counts down in Tick (the server's) and explodes
            // below 1 ms; a client counts in Update and stops at 0.
            const S_vector at = f->GetWorldPos();
            a->m_sound = 17;
            a->m_effect = 18;
            a->m_damage = 400;
            a->m_range = 7;
            const int planterIds[] = {-1, 21, 22};
            for (int p = 0; p < 3; ++p) {
                for (int timed = 0; timed < 2; ++timed) {
                    g_gameRole = GAME_ROLE_SERVER;
                    apply(a, EXPLOSIVE_EMPTY);
                    a->m_timed = timed != 0;
                    a->m_timeLeft = 500;
                    apply(a, EXPLOSIVE_PLACED);
                    a->m_timeLeft = 500;
                    a->m_planterId = planterIds[p];
                    g_obHumans[21] = &planter;
                    reset();
                    a->Update(100);
                    a->Tick(200);
                    WA(r.explosiveState, a->GetTimeLeft() == (timed ? 300 : 500) && g_itLog.empty() && g_streams.empty());
                    const S_WaApply e = applyOracle(a, EXPLOSIVE_EXPLODED);
                    reset();
                    a->Tick(300);
                    if (!timed) {
                        WA(r.explosiveState, a->GetTimeLeft() == 500 && g_itLog.empty() && g_streams.empty());
                    } else {
                        std::vector<std::string> log;
                        if (planterIds[p] >= 0)
                            log.push_back("gethuman " + std::to_string(planterIds[p]));
                        log.insert(log.end(), e.log.begin(), e.log.end());
                        S_WaApply full = e;
                        full.log = log;
                        const bool damage = planterIds[p] == 21;
                        WA(r.explosiveState,
                           applied(a, full, EXPLOSIVE_EXPLODED) && g_streams.size() == 1 && g_streams[0]->bits == T_Bits{{0, 2}} && g_streams[0]->sent &&
                               (damage ? g_explosions.size() == 1 && g_explosions[0].manager && g_explosions[0].source == &planter &&
                                             RbSame(g_explosions[0].pos, at) && g_explosions[0].range == 7.0f &&
                                             g_explosions[0].damage == 400.0f && g_explosions[0].sound == 17
                                       : g_explosions.empty()));
                    }
                    g_obHumans.clear();
                    // A client's count stops at 0; a server's Update does nothing.
                    const uint8_t updateRoles[] = {GAME_ROLE_CLIENT, GAME_ROLE_SERVER, GAME_ROLE_LOCAL};
                    for (uint8_t role : updateRoles) {
                        g_gameRole = role;
                        a->m_timeLeft = 250;
                        a->Update(100);
                        const int first = a->GetTimeLeft();
                        a->Update(200);
                        const bool counts = timed && !(role & GAME_ROLE_SERVER);
                        WA(r.explosiveState, first == (counts ? 150 : 250) && a->GetTimeLeft() == (counts ? 0 : 250));
                    }
                    a->m_timeLeft = 0;
                    a->Tick(10);
                    a->Update(10);
                    WA(r.explosiveState, a->GetTimeLeft() == 0);
                }
            }

            // Messages: 0 exploded (the effects), 1 disarmed and 2 placed (clients only), 3 a state.
            for (int role = 0; role < 2; ++role) {
                g_gameRole = role ? GAME_ROLE_SERVER : GAME_ROLE_CLIENT;
                // Exploded.
                apply(a, EXPLOSIVE_PLACED);
                S_WaApply e = applyOracle(a, EXPLOSIVE_EXPLODED);
                SaRead({{0, 2}});
                reset();
                a->OnMessage(nullptr);
                WA(r.explosiveNet, SaReadAll() && applied(a, e, EXPLOSIVE_EXPLODED) && g_explosionsShown.size() == 1 &&
                                       g_explosionsShown[0].manager && RbSame(g_explosionsShown[0].pos, at) &&
                                       RbSame(g_explosionsShown[0].dir, S_vector(0, 1, 0)) && g_explosionsShown[0].effect == 18 &&
                                       g_explosionsShown[0].material == -1 && g_explosionsShown[0].sound == 17);
                // Disarmed.
                apply(a, EXPLOSIVE_PLACED);
                e = role ? S_WaApply{{}, a->m_name.c_str(), a->m_volumeAdded, 0, a->m_timeLeft}
                         : applyOracle(a, EXPLOSIVE_EMPTY);
                SaRead({{1, 2}});
                reset();
                a->OnMessage(nullptr);
                WA(r.explosiveNet, SaReadAll() && applied(a, e, role ? EXPLOSIVE_PLACED : EXPLOSIVE_EMPTY));
                // Placed: who, the uid (16 bits, signed); the item taken from the human.
                for (int found = 0; found < 3; ++found) {
                    apply(a, EXPLOSIVE_EMPTY);
                    a->m_planterId = 4;
                    C_Human h;
                    h.items.emplace_back(0x44, ObItem(0x44, found == 2 ? -2 : 302));
                    if (found != 1)
                        g_obHumans[33] = &h;
                    const uint32_t uid = found == 2 ? 0xFFFEu : 302u;
                    const bool takes = !role && found != 1;
                    e = takes ? applyOracle(a, EXPLOSIVE_PLACED) : S_WaApply{{}, a->m_name.c_str(), a->m_volumeAdded, 0, a->m_timeLeft};
                    if (!role)
                        e.log.insert(e.log.begin(), "gethuman 33");
                    SaRead({{2, 2}, {33, 10}, {uid, 16}});
                    reset();
                    a->OnMessage(nullptr);
                    WA(r.explosiveNet, SaReadAll() && applied(a, e, takes ? EXPLOSIVE_PLACED : EXPLOSIVE_EMPTY) &&
                                           a->m_planterId == (role ? 4 : 33) &&
                                           h.removedItems == (takes ? std::vector<std::pair<int, bool>>{{found == 2 ? -2 : 302, true}}
                                                                    : std::vector<std::pair<int, bool>>{}));
                    // A uid it does not carry: nothing.
                    if (takes) {
                        apply(a, EXPLOSIVE_EMPTY);
                        SaRead({{2, 2}, {33, 10}, {999, 16}});
                        reset();
                        a->OnMessage(nullptr);
                        WA(r.explosiveNet, SaReadAll() && a->GetState() == EXPLOSIVE_EMPTY && h.removedItems.size() == 1);
                    }
                    g_obHumans.clear();
                }
                // A state.
                for (uint32_t s = 0; s < 4; ++s) {
                    apply(a, s == 0 ? EXPLOSIVE_HIDDEN : EXPLOSIVE_EMPTY);
                    e = applyOracle(a, static_cast<int>(s));
                    SaRead({{3, 2}, {s, 2}});
                    reset();
                    a->OnMessage(nullptr);
                    WA(r.explosiveNet, SaReadAll() && applied(a, e, static_cast<int>(s)));
                }
            }
            // WriteState / ReadState: the state in 2 bits.
            for (int s = 0; s < 5; ++s) {
                a->m_state = s;
                a->WriteState(newStream());
                WA(r.explosiveNet, lastStream()->bits == T_Bits{{static_cast<uint32_t>(s), 2}});
            }
            for (uint32_t s = 0; s < 4; ++s) {
                apply(a, s == 2 ? EXPLOSIVE_EMPTY : EXPLOSIVE_EXPLODED);
                const S_WaApply e = applyOracle(a, static_cast<int>(s));
                SaRead({{s, 2}});
                reset();
                a->ReadState(nullptr);
                WA(r.explosiveNet, SaReadAll() && applied(a, e, static_cast<int>(s)));
            }

            // Properties: 100 the name's text and the timer flag, 101 the item, 102 the delay, 103 the
            // sound, effect, damage, range, 104 placed at the start, 105 hidden while empty.
            a->m_nameTextId = 61;
            a->m_timed = k == 0;
            a->m_itemId = 62;
            a->m_delay = 63;
            a->m_sound = 64;
            a->m_effect = 65;
            a->m_damage = 66;
            a->m_range = 67;
            a->m_startPlaced = k == 1;
            a->m_hidden = k == 0;
            S_SaBytes props;
            props.Head(0x777, 73).Head(100, 11).Val(61).Val(static_cast<uint8_t>(k == 0)).Head(101, 10).Val(62)
                .Head(102, 10).Val(63).Head(103, 22).Val(64).Val(65).Val(66).Val(67).Head(104, 7)
                .Val(static_cast<uint8_t>(k == 1)).Head(105, 7).Val(static_cast<uint8_t>(k == 0));
            bool written = false;
            WA(r.explosiveSave, SaWritten([&](C_chunk &c) { return a->WriteProperties(c); }, &written) == props.b && written);
            auto *b = static_cast<C_ExplosivePlace *>(creator->Create());
            {
                S_SaReader rd(props);
                WA(r.explosiveSave, b->ReadProperties(rd.c) && b->m_nameTextId == 61 && b->m_timed == (k == 0) &&
                                        b->m_itemId == 62 && b->m_delay == 63 && b->m_sound == 64 && b->m_effect == 65 &&
                                        b->m_damage == 66 && b->m_range == 67 && b->m_startPlaced == (k == 1) &&
                                        b->m_hidden == (k == 0));
            }
            {
                // Another chunk is entered and not left: what it holds is read, what follows it not.
                S_SaBytes nested;
                nested.Head(0x777, 6 + 26 + 10).Head(9, 26).Head(102, 10).Val(81).Head(101, 10).Val(82).Head(102, 10).Val(83);
                S_SaReader rd(nested);
                WA(r.explosiveSave, b->ReadProperties(rd.c) && b->m_delay == 81 && b->m_itemId == 82);
            }
            // CopyFrom: the base's, the timer flag and the name's text.
            b->m_timed = k != 0;
            b->m_nameTextId = 91;
            b->m_itemId = 92;
            b->m_delay = 93;
            const int copies = g_actorCopies;
            a->CopyFrom(b);
            WA(r.explosiveSave, g_actorCopies == copies + 1 && a->m_timed == (k != 0) && a->m_nameTextId == 91 &&
                                    a->m_itemId == 62 && a->m_delay == 63);
            creator->Destroy(b);
            // The saved game: chunk 3000 with the state, the time left, who placed it; LoadGame enters
            // other chunks without leaving them and applies the state read.
            apply(a, EXPLOSIVE_PLACED);
            a->m_timeLeft = 1234;
            a->m_planterId = 45;
            S_SaBytes sv;
            sv.Head(0x777, 24).Head(3000, 18).Val(EXPLOSIVE_PLACED).Val(1234).Val(45);
            bool saved = false;
            WA(r.explosiveSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv.b && saved);
            {
                S_SaBytes ld;
                ld.Head(0x777, 6 + 6 + 18).Head(5, 24).Head(3000, 18).Val(EXPLOSIVE_EXPLODED).Val(77).Val(46);
                S_SaReader rd(ld);
                const S_WaApply e = applyOracle(a, EXPLOSIVE_EXPLODED);
                reset();
                const bool loaded = a->LoadGame(rd.c);
                S_WaApply full = e;
                full.timeLeft = 77;
                WA(r.explosiveSave, loaded && applied(a, full, EXPLOSIVE_EXPLODED) && a->m_planterId == 46 && rd.c.m_depth == 2);
            }
            {
                // A short chunk: the state only.
                S_SaBytes ld;
                ld.Head(0x777, 6 + 10).Head(3000, 10).Val(EXPLOSIVE_HIDDEN);
                S_SaReader rd(ld);
                a->m_timeLeft = 5;
                a->m_planterId = 6;
                WA(r.explosiveSave, a->LoadGame(rd.c) && a->GetState() == EXPLOSIVE_HIDDEN && a->m_timeLeft == 0 &&
                                        a->m_planterId == 6);
            }
            // GetMaterialId: the part's (-1 none), else the frame's, else 11.
            I3D_frame *part = AnFrame(FRAME_VISUAL, "wa.epart", f);
            const int partIds[] = {4, -1, -1, -2};
            const int frameIds[] = {6, 8, -1, 9};
            for (int i = 0; i < 4; ++i) {
                g_frameMaterials[part] = partIds[i];
                g_frameMaterials[f] = frameIds[i];
                WA(r.explosive, a->GetMaterialId(part) == (partIds[i] != -1 ? partIds[i] : frameIds[i] != -1 ? frameIds[i] : 11));
                WA(r.explosive, a->GetMaterialId(f) == (frameIds[i] != -1 ? frameIds[i] : 11));
            }
            g_frameMaterials.erase(part);
            g_frameMaterials.erase(f);
            // RestartMission: each side the game runs stopped and started.
            const uint8_t restartRoles[] = {GAME_ROLE_CLIENT, GAME_ROLE_SERVER, 3};
            for (uint8_t role : restartRoles) {
                g_gameRole = role;
                reset();
                a->RestartMission(true);
                const std::vector<bool> stops = role == GAME_ROLE_CLIENT   ? std::vector<bool>{false}
                                                : role == GAME_ROLE_SERVER ? std::vector<bool>{true}
                                                                           : std::vector<bool>{false, true};
                const std::vector<bool> starts = role == GAME_ROLE_CLIENT   ? std::vector<bool>{false}
                                                 : role == GAME_ROLE_SERVER ? std::vector<bool>{true}
                                                                            : std::vector<bool>{true, false};
                WA(r.explosive, g_deactivations == stops && g_activations == starts && a->m_started);
            }
            // StopMission: stopped (both frames shown, the volume out) once.
            {
                apply(a, EXPLOSIVE_EMPTY);
                const S_WaApply e = applyOracle(a, EXPLOSIVE_STOPPED);
                reset();
                a->StopMission(true);
                WA(r.explosive, !a->m_started && applied(a, e, EXPLOSIVE_STOPPED) && g_deactivations == std::vector<bool>{true});
                a->m_state = EXPLOSIVE_HIDDEN;
                reset();
                a->StopMission(false);
                WA(r.explosive, g_itLog.empty() && g_volumeLog.empty() && a->GetState() == EXPLOSIVE_HIDDEN &&
                                    g_deactivations == std::vector<bool>{false});
            }
            // Hit and slot 0x20 do nothing.
            reset();
            a->m_timeLeft = 7;
            a->Hit(HIT_RUN_OVER, 50.0f, at, S_vector(0, 1, 0), &planter, 0);
            a->Unknown20(nullptr);
            WA(r.explosive, g_itLog.empty() && g_streams.empty() && a->GetState() == EXPLOSIVE_HIDDEN && a->m_timeLeft == 7);
            creator->Destroy(a);
        }
        g_obDefs.erase(0x44);
        g_obDefs.erase(0x48);
    }

    // --- the ladder ---------------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_LADDER];
        WA(r.ladder, creator && std::strcmp(creator->GetName(), "Ladder") == 0);
        // S_vector::Normalize from a square summed the caller's way.
        const auto normalizeBy = [](S_vector &v, float square) {
            if (!(1e-08f <= std::fabs(square - 1.0f)))
                return;
            if (1e-08f <= square) {
                const float scale = 1.0f / std::sqrt(square);
                v = S_vector(v.x * scale, v.y * scale, v.z * scale);
            } else if (v.x != 0.0f) {
                v.x = 0.0f <= v.x ? 1.0f : -1.0f;
            } else if (v.z != 0.0f) {
                v.z = 0.0f <= v.z ? 1.0f : -1.0f;
            } else {
                v.y = 0.0f <= v.y ? 1.0f : -1.0f;
            }
        };
        // CanUse as the original's (0x005eb1d0, from the asm).
        const auto canUseOracle = [&](const C_Ladder *a, const S_vector4 &pos, const S_vector4 &dir, const S_vector &vel,
                                      bool lookingUp) {
            S_vector facing(dir.x, dir.y, dir.z);
            if (facing.y != 0.0f) {
                facing.y = 0.0f;
                normalizeBy(facing, facing.z * facing.z + facing.x * facing.x);
            }
            S_vector move = vel;
            if (move.y != 0.0f)
                move.y = 0.0f;
            const float sq = (move.z * move.z + move.x * move.x) + move.y * move.y;
            int32_t bits;
            std::memcpy(&bits, &sq, 4);
            float speed = 0.0f;
            if (bits >= 0x322bcc77) {
                speed = std::fabs(sq - 1.0f) < 1e-08f ? 1.0f : std::sqrt(sq);
                if (speed != 0.0f) {
                    const float k = 1.0f / speed;
                    move = S_vector(move.x * k, move.y * k, move.z * k);
                }
            }
            const S_vector4 &d = a->m_dir;
            const auto dot = [&](const S_vector &v) { return (v.z * d.z + v.y * d.y) + v.x * d.x; };
            const S_vector t(pos.x - a->m_top.x, pos.y - a->m_top.y, pos.z - a->m_top.z);
            const S_vector b(pos.x - a->m_bottom.x, pos.y - a->m_bottom.y, pos.z - a->m_bottom.z);
            const float tSq = (t.z * t.z + t.y * t.y) + t.x * t.x;
            const float bSq = (b.z * b.z + b.y * b.y) + b.x * b.x;
            if (bSq <= tSq || std::isnan(bSq) || std::isnan(tSq)) {
                if (!lookingUp)
                    return false;
                const float above = pos.y - a->m_bottom.y;
                if (!(above <= 0.5f || std::isnan(above)))
                    return false;
                S_vector up(a->m_top.x - pos.x, 0.0f, a->m_top.z - pos.z);
                normalizeBy(up, up.z * up.z + up.x * up.x);
                const float l1 = a->m_unknown180 ? 0.5f : 0.85f, l2 = a->m_unknown180 ? 0.5f : 0.95f;
                if (dot(up) < l1)
                    return false;
                if (dot(move) <= l2)
                    return false;
                return !(dot(facing) <= l2);
            }
            if (static_cast<double>(speed) < 0.001)
                return false;
            if (lookingUp)
                return false;
            if (pos.y - a->m_top.y < -0.3f)
                return false;
            S_vector back(b.x, 0.0f, b.z);
            normalizeBy(back, back.z * back.z + back.x * back.x);
            if (dot(back) < 0.8f)
                return false;
            if (dot(move) > -0.8f)
                return false;
            return !(static_cast<double>(std::fabs(dot(facing))) <= 0.8);
        };
        static const char *const props[] = {"x MatID31 other", "matidz", "matid", "matid-7 z", "MATID 0"};
        for (int k = 0; k < 5; ++k) {
            I3D_frame *f = placed(FRAME_MODEL, "wa.ladder", k % 2 == 0);
            // (The second without a visual: its own properties; the first with a visual named up*.)
            if (k != 1)
                boxFrame(f, "rungs", k);
            if (k == 0)
                AnFrame(FRAME_VISUAL, "upright", f);
            I3D_frame *up = k == 3 ? nullptr : AnFrame(FRAME_DUMMY, k == 2 ? "UP" : "up_top", f);
            I3D_frame *down = k == 4 ? nullptr : AnFrame(FRAME_DUMMY, "down1", f);
            if (up) {
                up->m_localMatrix.m[3][0] = rnd(-0.2f, 0.2f);
                up->m_localMatrix.m[3][1] = rnd(2.0f, 5.0f);
                up->m_localMatrix.m[3][2] = rnd(-0.6f, 0.2f);
            }
            if (down) {
                down->m_localMatrix.m[3][1] = rnd(-0.4f, 0.3f);
                down->m_localMatrix.m[3][2] = rnd(-0.2f, 0.2f);
            }
            I3D_frame *visual = F_FindChildFrame(f, "*", ENUMF_WILDMASK | FRAME_MASK_VISUAL);
            (visual ? visual : f)->m_userProps = props[k];
            auto *a = static_cast<C_Ladder *>(creator->Create());
            WA(r.ladder, a->GetType() == ACTOR_LADDER && std::strcmp(a->m_name.c_str(), "text 7154") == 0 && !a->m_started &&
                             !a->m_volume && !a->m_topVolume && a->GetClimbers().empty() && a->m_unknown170 == -1 &&
                             a->GetStepSound() == -1 && a->GetClimbSound() == -1 && a->m_unknown17C == 1 &&
                             !a->GetUnknown180());
            // SetFrame: false without the frame or either end (nothing else done).
            const int inits = g_actorInits;
            WA(r.ladder, !a->SetFrame(nullptr) && g_actorInits == inits);
            if (k >= 3) {
                WA(r.ladder, !a->SetFrame(f) && !a->GetFrame() && g_actorInits == inits);
                creator->Destroy(a);
                continue;
            }
            WA(r.ladder, a->SetFrame(f) && a->GetFrame() == f && g_actorInits == inits + 1);
            const S_matrix &tw = up->GetMatrix(), &bw = down->GetMatrix(), &w = f->GetMatrix();
            WA(r.ladder, a->GetTop().x == tw.m[3][0] && a->GetTop().y == tw.m[3][1] && a->GetTop().z == tw.m[3][2] &&
                             a->GetTop().w == tw.m[3][3] && a->GetBottom().x == bw.m[3][0] && a->GetBottom().y == bw.m[3][1] &&
                             a->GetBottom().z == bw.m[3][2] && a->GetBottom().w == bw.m[3][3]);
            S_vector axis = w(2);
            normalizeBy(axis, (axis.z * axis.z + axis.x * axis.x) + axis.y * axis.y);
            S_vector level(axis.x, 0.0f, axis.z);
            normalizeBy(level, (level.x * level.x + level.y * level.y) + level.z * level.z);
            WA(r.ladder, a->GetDir().x == level.x && a->GetDir().y == level.y && a->GetDir().z == level.z &&
                             a->GetDir().w == w.m[2][3]);
            const int materials[] = {31, 25, 25};
            WA(r.ladder, a->GetMaterialId(f) == materials[k] && a->GetMaterialId(nullptr) == materials[k]);

            // StartMission: relinked to its parent (the world transform kept) every time; the volumes
            // once until StopMission.
            const auto volumesOk = [&]() {
                I3D_bbox tmp;
                I3D_bbox box = *F_GetHierarchyBBox(nullptr, tmp, f);
                const S_vector s = f->m_scale;
                box.max = S_vector(box.max.x * s.x, box.max.y * s.y, box.max.z * s.z);
                box.min = S_vector(box.min.x * s.x, box.min.y * s.y, box.min.z * s.z);
                S_matrix m = f->GetMatrix();
                for (int c = 0; c < 3; ++c) {
                    m.m[0][c] = (1.0f / s.x) * m.m[0][c];
                    m.m[1][c] = (1.0f / s.y) * m.m[1][c];
                    m.m[2][c] = (1.0f / s.z) * m.m[2][c];
                }
                S_matrix inv;
                inv.Inverse(m);
                S_vector boxTop(0.0f, box.max.y, 0.0f);
                boxTop *= m;
                box.max.y = ((a->m_top.y - boxTop.y) + box.max.y) + 0.3f;
                const float radius = (box.max.y - box.min.y) + 0.3f;
                const S_matrix &world = f->GetMatrix();
                const auto volumeOk = [&](const S_CollisionVolume *v, const I3D_bbox &b) {
                    static const uint8_t zeros[0x0C] = {};
                    return v && v->flags == COLLISION_SHAPE_BOX && v->frame == f && v->type == 0x4000 && v->cell == nullptr &&
                           SaSame(&v->bbox, &b, sizeof b) && v->center.x == world.m[3][0] && v->center.y == world.m[3][1] &&
                           v->center.z == world.m[3][2] && v->centerW == world.m[3][3] && v->matrix == &a->m_matrix &&
                           v->invMatrix == &a->m_invMatrix && v->actor == a && v->radius == radius &&
                           v->capsuleRadius == 0.0f && SaSame(v->unknown54, zeros, sizeof zeros);
                };
                bool ok = SaSame(&a->m_matrix, &m, sizeof m) && SaSame(&a->m_invMatrix, &inv, sizeof inv) &&
                          volumeOk(a->m_volume, box);
                const S_vector topLocal = S_vector(a->m_top.x, a->m_top.y, a->m_top.z) * inv;
                box.max.y = topLocal.y;
                box.min.y = topLocal.y - 0.0075f;
                box.min.z = box.min.z - 0.1f;
                box.max.z = box.min.z + 0.25f;
                return ok && volumeOk(a->m_topVolume, box);
            };
            g_gameRole = GAME_ROLE_SERVER;
            const std::string link = "link " + ItName(f) + " " + ItName(f->GetParent()) + " 1";
            reset();
            a->StartMission(true);
            WA(r.ladderVolumes, a->m_started && SaLogIs({link}) && g_activations == std::vector<bool>{true} &&
                                    g_volumeLog.size() == 2 && g_volumeLog[0].op == 'A' && g_volumeLog[0].volume == a->m_volume &&
                                    g_volumeLog[1].op == 'A' && g_volumeLog[1].volume == a->m_topVolume && volumesOk());
            reset();
            a->StartMission(false);
            WA(r.ladderVolumes, SaLogIs({link}) && g_activations == std::vector<bool>{false} && g_volumeLog.empty());

            // CanUse, against the oracle around both ends (the facing, the velocity, looking up or
            // not, the byte at +0x180).
            int agree = 0, total = 0, yes = 0;
            for (int i = 0; i < 400; ++i) {
                const bool atTop = i % 2 == 0;
                const S_vector4 &end = atTop ? a->m_top : a->m_bottom;
                const S_vector4 pos = {end.x + rnd(-1.5f, 1.5f), end.y + rnd(-0.8f, 0.8f), end.z + rnd(-1.5f, 1.5f), 1.0f};
                S_vector4 dir = {a->m_dir.x * rnd(-1, 1) + rnd(-0.3f, 0.3f), i % 3 == 0 ? rnd(-0.5f, 0.5f) : 0.0f,
                                 a->m_dir.z * rnd(-1, 1) + rnd(-0.3f, 0.3f), 0.0f};
                if (i % 7 == 0)
                    dir = {a->m_dir.x, a->m_dir.y, a->m_dir.z, 0.0f};
                if (i % 11 == 0)
                    dir = {-a->m_dir.x, 0.25f, -a->m_dir.z, 0.0f};
                if (i % 13 == 0)
                    dir = {0.0f, 1e-5f, 0.0f, 0.0f};
                const float sign = atTop ? -1.0f : 1.0f;
                S_vector vel(a->m_dir.x * sign * rnd(0, 300) + rnd(-80, 80), rnd(-50, 50), a->m_dir.z * sign * rnd(0, 300) + rnd(-80, 80));
                if (i % 5 == 0)
                    vel = S_vector(0, i % 10 == 0 ? 0.0f : 3.0f, 0);
                if (i % 17 == 0)
                    vel = S_vector(1e-5f, 0, 0);
                const bool looking = (i / 2) % 2 == 0;
                a->m_unknown180 = (i / 4) % 3 == 0;
                const bool expect = canUseOracle(a, pos, dir, vel, looking);
                yes += expect;
                ++total;
                agree += a->CanUse(pos, dir, vel, looking) == expect && CanUseLadder(a, pos, dir, vel, looking) == expect;
            }
            // (And ones made to climb on: back from the bottom facing in and looking up, past the top
            // facing out and walking back; at a few distances; the ladder facing from its bottom to its
            // top for these, as the test's frames need not.)
            const S_vector4 frameDir = a->m_dir;
            S_vector span(a->m_top.x - a->m_bottom.x, 0.0f, a->m_top.z - a->m_bottom.z);
            if ((span.x * span.x + span.z * span.z) > 1e-4f) {
                span.Normalize();
                a->m_dir = {span.x, 0.0f, span.z, frameDir.w};
            }
            for (int i = 0; i < 24; ++i) {
                const bool atTop = i % 2 == 1;
                const float off = 0.1f * static_cast<float>(1 << (i / 4));
                const S_vector4 &d = a->m_dir;
                const S_vector4 &end = atTop ? a->m_top : a->m_bottom;
                const float sign = atTop ? 1.0f : -1.0f;
                const S_vector4 pos = {end.x + d.x * off * sign, end.y + (atTop ? 0.1f : 0.0f), end.z + d.z * off * sign, 1.0f};
                const S_vector4 dir = {d.x, 0.0f, d.z, 0.0f};
                const S_vector vel(-d.x * sign * 2.0f, 0.0f, -d.z * sign * 2.0f);
                a->m_unknown180 = (i / 2) % 2 == 1;
                const bool expect = canUseOracle(a, pos, dir, vel, !atTop);
                yes += expect;
                ++total;
                agree += a->CanUse(pos, dir, vel, !atTop) == expect && CanUseLadder(a, pos, dir, vel, !atTop) == expect;
            }
            a->m_unknown180 = false;
            a->m_dir = frameDir;
            WA(r.ladderUse, agree == total && yes > 0);
            // The limits exactly, and NaNs, on a ladder 1 m high facing +z: equally far from both
            // ends (the bottom's); the place, the walk and the facing at 0.5 with the byte at +0x180
            // (0.6, 0.8 is a unit vector in floats; the ladder's direction 0.625 long for these:
            // 0.8 * 0.625 is 0.5); a speed of 0.005; walking back at -0.8; a NaN height (the
            // bottom's, then above it); a speed whose square is just below 1e-8's bits (no speed: not
            // made a unit); facings up with level parts whose square is below 1e-8 (snapped: x's
            // sign, else z's), the ladder facing (0.6, 0, 0.8).
            {
                const S_vector4 top = a->m_top, bottom = a->m_bottom, dir = a->m_dir;
                a->m_top = {0.0f, 1.0f, 0.0f, 1.0f};
                a->m_bottom = {0.0f, 0.0f, 0.0f, 1.0f};
                const float nan = std::numeric_limits<float>::quiet_NaN();
                const S_vector4 in = {0.0f, 0.0f, 1.0f, 0.0f}, side = {0.6f, 0.0f, 0.8f, 0.0f};
                const S_vector walk(0.0f, 0.0f, 2.0f), sideWalk(0.6f, 0.0f, 0.8f), back(0.6f, 0.0f, -0.8f);
                const S_vector4 z1 = {0.0f, 0.0f, 1.0f, 0.0f}, z625 = {0.0f, 0.0f, 0.625f, 0.0f}, d68 = {0.6f, 0.0f, 0.8f, 0.0f};
                struct S_Case {
                    S_vector4 pos, dir;
                    S_vector vel;
                    S_vector4 ladderDir;
                    bool looking, flag180, expect;
                };
                const S_Case cases[] = {
                    {{0.0f, 0.5f, -0.4f, 1.0f}, in, walk, z1, true, false, true},              // equally far
                    {{-0.6f, 0.2f, -0.8f, 1.0f}, in, walk, z625, true, true, true},           // placed at 0.5
                    {{0.0f, 0.2f, -0.5f, 1.0f}, in, sideWalk, z625, true, true, false},       // walking at 0.5
                    {{0.0f, 0.2f, -0.5f, 1.0f}, side, walk, z625, true, true, false},         // facing at 0.5
                    {{0.0f, 1.0f, 0.3f, 1.0f}, in, S_vector(0.0f, 0.0f, -0.005f), z1, false, false, true},   // slow
                    {{0.0f, 1.0f, 0.3f, 1.0f}, in, back, z1, false, false, true},             // back at -0.8
                    {{0.0f, nan, -0.4f, 1.0f}, in, walk, z1, true, false, true},              // NaN height
                    {{0.0f, 0.2f, -0.5f, 1.0f}, in, S_vector(0.0f, 0.0f, 1e-4f), z1, true, true, false},     // no speed
                    {{-0.3f, 0.2f, -0.4f, 1.0f}, {1e-5f, 0.5f, -1e-5f, 0.0f}, sideWalk, d68, true, true, true},   // x's sign
                    {{-0.3f, 0.2f, -0.4f, 1.0f}, {-1e-5f, 0.5f, 1e-5f, 0.0f}, sideWalk, d68, true, true, false},
                    {{-0.3f, 0.2f, -0.4f, 1.0f}, {0.0f, 0.5f, 1e-5f, 0.0f}, sideWalk, d68, true, true, true},     // z's sign
                    {{-0.3f, 0.2f, -0.4f, 1.0f}, {0.0f, 0.5f, -1e-5f, 0.0f}, sideWalk, d68, true, true, false},
                };
                bool ok = true;
                for (const S_Case &c : cases) {
                    a->m_dir = c.ladderDir;
                    a->m_unknown180 = c.flag180;
                    ok = ok && a->CanUse(c.pos, c.dir, c.vel, c.looking) == c.expect &&
                         canUseOracle(a, c.pos, c.dir, c.vel, c.looking) == c.expect;
                }
                WA(r.ladderUse, ok);
                a->m_unknown180 = false;
                a->m_top = top;
                a->m_bottom = bottom;
                a->m_dir = dir;
            }
            // Exactly at its bottom, facing it, walking in, looking up; and the edges of the limits.
            {
                const S_vector4 pos = {a->m_bottom.x - a->m_dir.x * 0.4f, a->m_bottom.y + 0.5f, a->m_bottom.z - a->m_dir.z * 0.4f, 1.0f};
                const S_vector4 dir = {a->m_dir.x, 0.0f, a->m_dir.z, 0.0f};
                const S_vector vel(a->m_dir.x * 100.0f, 0.0f, a->m_dir.z * 100.0f);
                for (int flag = 0; flag < 2; ++flag) {
                    a->m_unknown180 = flag != 0;
                    for (int looking = 0; looking < 2; ++looking)
                        WA(r.ladderUse, a->CanUse(pos, dir, vel, looking != 0) == canUseOracle(a, pos, dir, vel, looking != 0));
                }
                a->m_unknown180 = false;
            }
            // The climbers: added at the end, the last one fills the gap; RestartMission clears them.
            C_Human h1, h2, h3, h4;
            a->AddClimber(&h1);
            AddLadderClimber(a, &h2);
            a->AddClimber(&h3);
            WA(r.ladderUse, a->GetClimbers() == std::vector<C_Human *>{&h1, &h2, &h3});
            a->RemoveClimber(&h1);
            WA(r.ladderUse, GetLadderClimbers(a) == std::vector<C_Human *>{&h3, &h2});
            RemoveLadderClimber(a, &h4);
            a->RemoveClimber(&h2);
            WA(r.ladderUse, a->GetClimbers() == std::vector<C_Human *>{&h3});
            a->AddClimber(&h2);
            reset();
            a->RestartMission(true);
            WA(r.ladderUse, a->GetClimbers().empty() && g_itLog.empty() && g_volumeLog.empty() && g_activations.empty());
            // The accessors the movement code uses.
            a->m_stepSound = 41;
            a->m_climbSound = 42;
            a->m_unknown180 = true;
            WA(r.ladder, &GetLadderTop(a) == &a->m_top && &GetLadderBottom(a) == &a->m_bottom && &GetLadderDir(a) == &a->m_dir &&
                             GetLadderStepSoundGroup(a) == 41 && GetLadderSoundGroup(a) == 42 && GetLadderFlag180(a));
            a->m_unknown180 = false;
            WA(r.ladder, !GetLadderFlag180(a));
            a->m_unknown180 = true;
            // Properties: version 3, the unknown at +0x170, the sounds, +0x17C, +0x180 (raw).
            a->m_unknown170 = 43;
            a->m_unknown17C = 44;
            S_SaBytes pr;
            pr.Head(0x777, 24).Val(static_cast<uint8_t>(3)).Val(43).Val(41).Val(42).Val(44).Val(static_cast<uint8_t>(1));
            bool written = false;
            WA(r.ladderProps, SaWritten([&](C_chunk &c) { return a->WriteProperties(c); }, &written) == pr.b && written);
            const uint8_t versions[] = {1, 2, 3, 0x83};
            for (uint8_t version : versions) {
                auto *b = static_cast<C_Ladder *>(creator->Create());
                S_SaBytes in;
                in.Head(0x777, 24).Val(version).Val(51).Val(52).Val(53).Val(54).Val(static_cast<uint8_t>(1));
                S_SaReader rd(in);
                const bool v2 = version == 2 || version == 3, v3 = version == 3;
                WA(r.ladderProps, b->ReadProperties(rd.c) && b->m_unknown170 == 51 && b->GetStepSound() == 52 &&
                                      b->GetClimbSound() == 53 && b->m_unknown17C == (v2 ? 54 : 1) && b->GetUnknown180() == v3);
                creator->Destroy(b);
            }
            // The saved game: nothing (not the base's chunk).
            S_SaBytes none;
            none.Head(0x777, 6);
            bool saved = false;
            WA(r.ladderProps, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == none.b && saved);
            S_SaBytes ld;
            ld.Head(0x777, 16).Head(0x3039, 10).Val(5);
            S_SaReader rd(ld);
            reset();
            WA(r.ladderProps, a->LoadGame(rd.c) && g_actorLoads.empty() && rd.c.RemainingSize() == 10);
            const int copies = g_actorCopies;
            a->CopyFrom(a);
            WA(r.ladder, g_actorCopies == copies + 1 && a->m_unknown170 == 43);
            // StopMission: the volumes out and let go, nobody on it; then only the deactivation.
            a->AddClimber(&h1);
            S_CollisionVolume *v1 = a->m_volume, *v2 = a->m_topVolume;
            reset();
            a->StopMission(true);
            WA(r.ladderVolumes, !a->m_started && !a->m_volume && !a->m_topVolume && a->GetClimbers().empty() &&
                                    g_volumeLog.size() == 2 && g_volumeLog[0].op == 'R' && g_volumeLog[0].volume == v1 &&
                                    g_volumeLog[1].op == 'R' && g_volumeLog[1].volume == v2 &&
                                    g_deactivations == std::vector<bool>{true});
            a->AddClimber(&h1);
            reset();
            a->StopMission(false);
            WA(r.ladderVolumes, g_volumeLog.empty() && g_deactivations == std::vector<bool>{false} && a->GetClimbers().size() == 1);
            // Destroyed while its mission runs: the volumes out (Fix).
            a->StartMission(true);
            v1 = a->m_volume;
            v2 = a->m_topVolume;
            reset();
            creator->Destroy(a);
            WA(r.ladderVolumes, g_volumeLog.size() == 2 && g_volumeLog[0].op == 'R' && g_volumeLog[0].volume == v1 &&
                                    g_volumeLog[1].op == 'R' && g_volumeLog[1].volume == v2);
        }
    }
