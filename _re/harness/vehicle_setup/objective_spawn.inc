// (objective_actors_test.inc, inside ObjectiveActorsTest: the spawn point.)

    // --- the spawn point -------------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_SPAWN_POINT];
        OB(r.spawn, creator && std::strcmp(creator->GetName(), "SpawnPoint") == 0);
        // The game and the engine the spawns use (restored at the end): the driver's frames, the
        // teams' AddMember, an empty model fill, the players and their slots' use.
        void *const oldCreateFrame = g_driverVtable[0x54 / 4];
        g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_ObCreateFrame);
        for (void *&slot : g_obTeamVtable)
            slot = reinterpret_cast<void *>(&F_ObAddMember);
        void **const oldPlayerTeam = *reinterpret_cast<void ***>(g_teamManager.GetPlayerTeam());
        void **const oldCoopTeam = *reinterpret_cast<void ***>(g_teamManager.GetCoopTeam());
        *reinterpret_cast<void ***>(g_teamManager.GetPlayerTeam()) = g_obTeamVtable;
        *reinterpret_cast<void ***>(g_teamManager.GetCoopTeam()) = g_obTeamVtable;
        void (*const oldFill)(I3D_model *) = g_cacheFill;
        g_cacheFill = [](I3D_model *) {};
        const int oldLocalSlot = g_localPlayerSlot;
        const S_MultiplayerSettings oldMp = g_mpSettingsValue;
        const int oldRound = GetGame()->m_roundState, oldMode = GetGame()->m_mode;
        for (int i = 0; i < 8; ++i) {
            g_obPlayers[i] = S_NetPlayerState{};
            g_obPlayers[i].connection = 100 + i;
            std::snprintf(g_obPlayers[i].name, sizeof g_obPlayers[i].name, "player%d", i);
        }
        for (int i = 0; i < 32; ++i)
            g_obSlotUse[i] = static_cast<int8_t>(i % 5 - 1);
        const auto spReset = [&] {
            reset();
            g_cacheLoads.clear();
            g_obSpawnPick = 0;
        };
        const auto v3 = [](const S_vector4 &v) { return S_vector(v.x, v.y, v.z); };

        // The uniforms: one with a dotted model name, one without a dot, a coat (not a uniform).
        char dotted[] = "uniforms/ge.unif.4ds", plain[] = "PLAIN", coat[] = "coat.4ds";
        C_ItTestDef uniformDef, plainDef, coatDef;
        uniformDef.behaviour = ITEM_BEHAVIOUR_UNIFORM;
        uniformDef.m_modelName = dotted;
        plainDef.behaviour = ITEM_BEHAVIOUR_UNIFORM;
        plainDef.m_modelName = plain;
        coatDef.behaviour = 3;
        coatDef.m_modelName = coat;
        g_obDefs[0x60] = &uniformDef;
        g_obDefs[0x61] = &plainDef;
        g_obDefs[0x62] = &coatDef;
        // The model (CreateSoldier 0x00612a60): "NA01", or a uniform's model name copied with the part
        // from its last dot replaced by "." (0x0083972c).
        const auto modelOf = [](int id) -> std::string {
            C_ItemDef *def = id != -1 && id > -1 && id < 500 && g_obDefs.count(id) ? g_obDefs[id] : nullptr;
            if (!def || def->GetBehaviour() != 0xF)
                return "NA01";
            char buffer[256];
            std::strcpy(buffer, def->GetModelName());
            if (char *dot = std::strrchr(buffer, '.'))
                std::strcpy(dot, ".");
            return buffer;
        };
        // The frame's z axis as the original's inline normalisation (Spawn 0x00611c90).
        const auto axisOf = [](const S_vector &v) {
            const float sq = (v.y * v.y + v.z * v.z) + v.x * v.x;
            if (!(1e-8f <= std::fabs(sq - 1.0f)))
                return v;
            if (1e-8f <= sq) {
                const float k = 1.0f / std::sqrt(sq);
                return S_vector(v.x * k, v.y * k, v.z * k);
            }
            S_vector o = v;
            if (v.x != 0.0f)
                o.x = 0.0f <= v.x ? 1.0f : -1.0f;
            else if (v.z != 0.0f)
                o.z = 0.0f <= v.z ? 1.0f : -1.0f;
            else
                o.y = 0.0f <= v.y ? 1.0f : -1.0f;
            return o;
        };
        // S_vector::NormalizedGame: tiny vectors snapped to their first non-zero axis of x, z, y (the
        // others kept), near-unit ones kept, the rest scaled with the sum taken as (y² + z²) + x².
        {
            const S_vector cases[][2] = {
                {S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, 1.0f, 0.0f)},
                {S_vector(0.0f, -1e-5f, 0.0f), S_vector(0.0f, -1.0f, 0.0f)},
                {S_vector(2e-5f, -3e-5f, 1e-5f), S_vector(1.0f, -3e-5f, 1e-5f)},
                {S_vector(-2e-5f, 3e-5f, 0.0f), S_vector(-1.0f, 3e-5f, 0.0f)},
                {S_vector(0.0f, 3e-5f, -2e-5f), S_vector(0.0f, 3e-5f, -1.0f)},
                {S_vector(0.0f, -3e-5f, 2e-5f), S_vector(0.0f, -3e-5f, 1.0f)},
                {S_vector(1.0f, 0.0f, 0.0f), S_vector(1.0f, 0.0f, 0.0f)},
                {S_vector(0.0f, 1.0f, 1e-5f), S_vector(0.0f, 1.0f, 1e-5f)},
                {S_vector(3.0f, 0.0f, 4.0f), S_vector(3.0f * 0.2f, 0.0f, 4.0f * 0.2f)},
            };
            for (const auto &c : cases) {
                const S_vector n = c[0].NormalizedGame();
                OB(r.spawn, n.x == c[1].x && n.y == c[1].y && n.z == c[1].z);
            }
            for (int i = 0; i < 200; ++i) {
                const S_vector v(rnd(-3, 3), rnd(-3, 3), rnd(-3, 3));
                OB(r.spawn, RbSame(v.NormalizedGame(), axisOf(v)));
            }
        }

        // The defaults, the mission, the saved game (none), CopyFrom (the base's), RestartMission
        // (used and spawned: the frame's script told).
        {
            auto *a = static_cast<C_SpawnPoint *>(creator->Create());
            OB(r.spawn, a->GetType() == ACTOR_SPAWN_POINT && a->GetTeam() == 0 && a->GetKind() == 0 && a->m_unknown98 == 0 &&
                            a->IsEnabled() && a->m_npc == 0 && a->m_uniform == 0 && a->GetSpawnedActor() == nullptr);
            I3D_frame *f = placed(FRAME_DUMMY, "ob.spoint", true);
            const int inits = g_actorInits;
            reset();
            OB(r.spawn, a->SetFrame(f) && a->GetFrame() == f && g_actorInits == inits + 1 && g_itLog.empty());
            for (int server = 0; server < 2; ++server) {
                a->m_free = false;
                reset();
                a->StartMission(server != 0);
                OB(r.spawn, a->IsEnabled() && g_activations == std::vector<bool>{server != 0} && g_deactivations.empty());
                a->m_free = false;
                reset();
                a->StopMission(server != 0);
                OB(r.spawn, a->IsEnabled() && g_deactivations == std::vector<bool>{server != 0} && g_activations.empty());
            }
            bool saved = false;
            S_SaBytes empty;
            empty.Head(0x777, 6);
            reset();
            OB(r.spawn, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == empty.b && saved);
            S_SaBytes ld;
            ld.Head(0x777, 16).Head(0x3039, 10).Val(5);
            S_SaReader rd(ld);
            OB(r.spawn, a->LoadGame(rd.c) && g_actorLoads.empty() && rd.c.RemainingSize() == 10);
            auto *b = static_cast<C_SpawnPoint *>(creator->Create());
            b->m_team = 2;
            b->m_kind = 1;
            b->m_unknown98 = 4;
            b->m_npc = 1;
            b->m_uniform = 0x60;
            const int copies = g_actorCopies;
            a->CopyFrom(b);
            OB(r.spawn, g_actorCopies == copies + 1 && a->m_team == 0 && a->m_kind == 0 && a->m_unknown98 == 0 && a->m_npc == 0 &&
                            a->m_uniform == 0);
            creator->Destroy(b);
            C_Human spawned;
            S_FrameData noScript = {};
            for (int k = 0; k < 6; ++k) {
                a->m_free = k == 0;
                a->m_spawned = k == 1 ? nullptr : &spawned;
                f->SetData(k == 3 ? nullptr : k == 4 ? &noScript : &scriptData);
                reset();
                a->RestartMission(k % 2 == 0);
                OB(r.spawn, g_uses == (k == 2 || k == 5 ? std::vector<int>{0} : std::vector<int>{}) && g_itLog.empty() &&
                                g_activations.empty() && g_deactivations.empty());
            }
            f->SetData(nullptr);
            creator->Destroy(a);
        }

        // CreateSoldier's calls as the original makes them (fail: 1 no frame, 2 no model, 3 no actor);
        // whether it makes a human.
        const auto soldierLog = [&](std::vector<std::string> &log, int actorSlot, const S_vector &dir, bool mp, int fail,
                                    bool scene, bool coopAdd) {
            log.push_back("createframe 9");
            if (fail == 1)
                return false;
            if (fail == 2) {
                log.push_back("release new");
                return false;
            }
            if (scene)
                log.push_back("link new sector 0");
            log.push_back("worlddir new " + ItV(dir) + " 0");
            char name[32];
            std::snprintf(name, sizeof name, "_PLAYER_%2d", actorSlot);
            log.push_back(std::string("name ") + name);
            log.push_back(std::string("update ") + name);
            log.push_back("createactor " + std::to_string(ACTOR_HUMAN) + " " + name + " 1 " + std::to_string(actorSlot));
            log.push_back(std::string("release ") + name);
            if (fail == 3)
                return false;
            if (mp) {
                log.push_back("lock");
                if (coopAdd)
                    log.push_back("addmember coop");
                log.push_back("unlock");
            }
            return true;
        };
        // SetupSpawned's calls once it has its human (0x006123a0).
        const auto setupLog = [&](std::vector<std::string> &log, int slot, bool npc, bool server, bool local, int controller,
                                  bool localHuman, bool member, bool warmup, bool voice) {
            if (!local && slot == g_localPlayerSlot && !warmup)
                log.push_back("cameramode 1");
            if (slot == g_localPlayerSlot && !npc) {
                log.push_back("specialmode 0 1 0");
                log.push_back("specialmode 2 1 0");
                if (!member)
                    log.push_back("addmember player");
            }
            if (!local && controller == HUMAN_CONTROLLER_PLAYER) {
                if (localHuman)
                    log.push_back("selectmember 1");
                log.push_back("playerspawned " + std::to_string(slot) + " 1");
            }
            if (!local && !server && voice)
                log.push_back("voiceteam");
        };
        // The spawn message (Spawn): kind 0, the slots, the place, the direction, the slot's use, the
        // player's name and connection, the soldier state, the stats in multiplayer.
        const auto spawnBits = [&](int slot, int actorSlot, const S_vector &pos, const S_vector &dir, size_t str,
                                   const S_SoldierState &st, const int *stats) {
            T_Bits b = {{0u, 2}, {static_cast<uint32_t>(slot), 8}, {static_cast<uint32_t>(actorSlot), 8}};
            for (float v : {pos.x, pos.y, pos.z})
                b.emplace_back(ItF(v), -11);
            for (float v : {dir.x, dir.y, dir.z})
                b.emplace_back(ItF(v), -2);
            b.emplace_back(static_cast<uint32_t>(g_obSlotUse[actorSlot]), 8);
            b.emplace_back(static_cast<uint32_t>(str), -6);
            b.emplace_back(static_cast<uint32_t>(g_obPlayers[slot].connection), 32);
            b.emplace_back(static_cast<uint32_t>(st.profileIndex), -12);
            b.emplace_back(static_cast<uint32_t>(st.uniform.typeId), -12);
            if (stats) {
                b.emplace_back(static_cast<uint32_t>(stats[0]), 10);
                for (int i : {2, 1, 3, 4, 5, 6, 7, 8, 9, 11, 10})
                    b.emplace_back(static_cast<uint32_t>(stats[i]), 7);
            }
            return b;
        };
        const auto placedAt = [&](const C_Human *h, const S_vector &pos, bool scene) {
            I3D_frame *hf = h->GetFrame();
            return hf && !g_obMadeFrames.empty() && hf == g_obMadeFrames.back() && hf->m_localMatrix(3) == pos &&
                   hf->m_parent == (scene ? g_sector : nullptr) &&
                   (hf->m_frameFlags & (FRMFLAGS_SETPOS_CLEAR | FRMFLAGS_CHANGED)) == FRMFLAGS_CHANGED;
        };

        // --- Spawn --------------------------------------------------------------------------------------
        {
            // Random spawn points (their places), a zone (id 200).
            std::vector<C_SpawnPoint *> points;
            for (int i = 0; i < 3; ++i) {
                points.push_back(static_cast<C_SpawnPoint *>(creator->Create()));
                points.back()->m_position = {rnd(-40, 40), rnd(0, 6), rnd(-40, 40), rnd(0, 2)};
                points.back()->m_direction = {rnd(-1, 1), rnd(-1, 1), rnd(-1, 1), rnd(0, 2)};
            }
            g_obSpawnPoints.assign(points.begin(), points.end());
            auto *zone = static_cast<C_SpawnPoint *>(creator->Create());
            g_actorsById[200] = zone;
            g_obZone = zone;
            struct S_SpCase {
                uint8_t role;
                int type, slot, freeSlot, stateUniform, pointUniform;
                bool npc, loading, script, scene;
                int fail, controller;
                bool localSlot, warmup, voice, coop, zoneOk, respawn, localHuman;
                int blocked;
            };
            const uint8_t L = GAME_ROLE_LOCAL, S = GAME_ROLE_SERVER, H = GAME_ROLE_SERVER | GAME_ROLE_CLIENT, C = GAME_ROLE_CLIENT;
            const int P = HUMAN_CONTROLLER_PLAYER, R = HUMAN_CONTROLLER_REMOTE, E = HUMAN_CONTROLLER_ENEMY;
            const S_SpCase cases[] = {
                // role type slot free stU ptU npc load script scene fail ctrl lslot warm voice coop zone resp lhum blocked
                {L, 0, 0, 3, -1, 0x60, false, false, true, true, 0, P, true, false, false, false, true, false, true, 0},
                {L, 0, 1, 12, 0x61, 0x62, true, true, false, true, 0, P, true, false, true, true, true, false, true, 0},
                {L, 0, 2, 5, -1, 0x61, false, true, true, true, 0, E, true, true, false, false, true, false, false, 0},
                {L, 0, 0, 3, 0x60, 0, false, false, true, true, 1, P, false, false, false, false, true, false, false, 0},
                {L, 0, 0, 3, -1, 0x61, false, false, true, true, 2, P, false, false, false, false, true, false, false, 0},
                {L, 0, 0, 4, -1, 0x63, false, false, true, false, 3, P, false, false, false, false, true, false, false, 0},
                {L, 3, 3, 9, 0x62, 0x60, false, false, false, true, 0, P, false, false, true, true, false, false, true, 5},
                {S, 0, 2, 0, -1, 0x60, false, false, true, true, 0, P, true, false, true, true, true, false, true, 2},
                {S, 0, 3, 0, 0x61, 0x60, false, false, false, true, 0, R, true, true, false, false, true, false, false, 40},
                {H, 4, 4, 0, 0x62, 0, false, true, true, true, 0, P, true, false, true, false, true, false, true, 0},
                {S, 1, 5, 0, -1, 0x61, false, false, true, true, 0, E, false, false, false, false, true, true, false, 0},
                {S, 2, 6, 0, -1, 0x60, false, false, true, true, 0, P, true, false, true, true, false, false, false, 0},
                {S, 3, 7, 0, 0x60, 0, true, true, false, true, 0, P, true, false, false, true, true, false, true, 0},
                {H, 3, 1, 0, -1, 0x62, false, true, true, true, 0, R, false, false, true, true, true, true, false, 0},
                {C, 0, 0, 3, -1, 0x60, false, false, true, true, 0, P, true, false, true, true, true, false, true, 0},
                {S, 0, 1, 0, -1, 0x60, false, false, true, true, 3, P, true, false, true, true, true, false, true, 1},
                {H, 0, 2, 0, -1, 0x62, false, false, true, false, 0, R, false, false, true, false, true, false, false, 1},
                {H, 2, 3, 0, 0x61, 0x60, true, false, false, true, 0, P, true, true, false, false, true, true, true, 0},
            };
            int index = 0;
            for (const S_SpCase &c : cases) {
                I3D_frame *f = placed(FRAME_DUMMY, "ob.spawn", index % 2 == 0);
                f->m_localMatrix.m[2][3] = rnd(-1, 1);
                auto *a = static_cast<C_SpawnPoint *>(creator->Create());
                a->SetFrame(f);
                a->m_netId = 40 + index;
                a->m_team = 1;
                a->m_uniform = c.pointUniform;
                a->m_npc = c.npc ? 2 : 0;
                f->SetData(c.script ? &scriptData : nullptr);
                const bool local = c.role == L;
                g_gameRole = c.role;
                g_mpSettingsValue.gameType = c.type;
                g_mpSettingsValue.allowRespawn = c.respawn ? 3 : 0;
                g_obFreeSlot = c.freeSlot;
                const int uniformAfter = c.stateUniform == -1 ? c.pointUniform : c.stateUniform;
                const std::string model = modelOf(local ? c.pointUniform : uniformAfter);
                g_obNoFrame = c.fail == 1;
                g_cacheFailName = c.fail == 2 ? model : "";
                g_obNoHuman = c.fail == 3;
                g_obControllerType = c.controller;
                g_obLocalHuman = c.localHuman;
                g_obMakeMember = false;
                g_playerTeam.clear();
                g_localPlayerSlot = c.localSlot ? c.slot : 31;
                GetGame()->m_roundState = c.warmup ? ROUND_WARMUP : ROUND_PLAYING;
                GetGame()->m_mode = c.loading ? GAME_MODE_LOADING : GAME_MODE_NORMAL;
                GetGame()->m_scene = c.scene ? g_carScene : nullptr;
                g_obDetails.voiceChat = c.voice;
                g_coop = c.coop;
                g_obBlocked = c.blocked;
                g_obZoneOk = c.zoneOk;
                g_obZonePos = {rnd(-50, 50), rnd(0, 5), rnd(-50, 50), rnd(0, 2)};
                g_obZoneDir = {rnd(-1, 1), rnd(-1, 1), rnd(-1, 1), rnd(0, 2)};
                S_SquadSoldier st;
                st.uniform.uid = 77;
                st.uniform.typeId = static_cast<int16_t>(c.stateUniform);
                st.uniform.present = 1;
                st.team = 2;
                st.spawnZone = 200;
                st.spectating = index % 2 == 1;
                st.vip = index % 3 == 0;
                st.profileIndex = 10 + index;
                int stats[STAT_COUNT];
                for (int i = 0; i < STAT_COUNT; ++i)
                    stats[i] = 3 * i + index;
                const int *statsArg = local ? nullptr : stats;
                g_obPlayers[c.slot].human = nullptr;
                g_obPlayers[c.slot].activityTime = 0;
                spReset();
                const size_t str = g_itStrings.size(), humans = g_obMadeHumans.size();
                const uint32_t t0 = timeGetTime();
                a->Spawn(c.slot, &st, local ? nullptr : stats);
                const uint32_t t1 = timeGetTime();

                // The oracle (0x00611c90).
                std::vector<std::string> log;
                bool reached = false, made = false, sent = false;
                const S_matrix &w = f->GetMatrix();
                S_vector pos = w(3), dir = axisOf(w(2));
                int actorSlot = c.slot;
                if (c.role & GAME_ROLE_SERVER) {
                    if (local) {
                        log.push_back("freeslot");
                        actorSlot = c.freeSlot;
                    }
                    reached = true;
                    if (!local) {
                        if (c.type == 1 || c.type == 2 || c.type == 3) {
                            log.push_back(std::string("pickspawn ") + (c.respawn ? "1" : "0") + " zone");
                            if (!c.zoneOk) {
                                log.push_back("gamemsg 15 0 " + std::to_string(c.slot) + " 1 0");
                                reached = false;
                            } else {
                                pos = v3(g_obZonePos);
                                dir = v3(g_obZoneDir);
                                if (c.type == 1) {
                                    log.push_back("facezone " + ItV(pos) + " 2");
                                    dir.x = -dir.x;
                                }
                            }
                        } else {
                            int left = c.blocked;
                            for (int t = 0; t < 31; ++t) {
                                const C_Actor *p = points[static_cast<size_t>(t) % points.size()];
                                log.push_back(std::string("randompoint 1 ") + (c.type == 0 ? "0" : "2"));
                                pos = v3(p->GetPosition4());
                                dir = v3(p->GetDirection4());
                                log.push_back("blocked " + ItV(pos));
                                if (!(left-- > 0))
                                    break;
                            }
                        }
                    }
                }
                if (reached) {
                    if (c.stateUniform == -1)
                        log.push_back("putitem 2 -1 " + std::to_string(c.pointUniform) + " 1");
                    log.push_back("splitstacks");
                    made = soldierLog(log, actorSlot, dir, !local, c.fail, c.scene, !local && c.coop);
                    if (made) {
                        if (local)
                            log.push_back("setplayer " + std::to_string(c.slot) + " 0 name");
                        if (!local && c.localSlot && !c.warmup)
                            log.push_back("cameramode 1");
                        if (!local && c.voice)
                            log.push_back("voiceteam");
                        if (c.loading) {
                            log.push_back("lock");
                            if (c.role & GAME_ROLE_CLIENT)
                                log.push_back("setplayer " + std::to_string(c.slot) + " " +
                                              std::to_string(g_obPlayers[c.slot].connection) + " " + g_obPlayers[c.slot].name);
                            setupLog(log, c.slot, c.npc, true, local, c.controller, c.localHuman, false, c.warmup, c.voice);
                            log.push_back("unlock");
                        } else {
                            log.push_back("lock");
                            log.push_back("unlock");
                            log.push_back("writestate 0");
                            sent = true;
                        }
                    }
                }
                C_Human *h = g_obMadeHumans.size() > humans ? g_obMadeHumans.back() : nullptr;
                OB(r.spawn, SaLogIs(log));
                OB(r.spawn, (h != nullptr) == made && a->GetSpawnedActor() == h && a->IsEnabled() == !local);
                OB(r.spawn, g_cacheLoads == (reached && c.fail != 1 ? std::vector<std::string>{model} : std::vector<std::string>{}));
                OB(r.spawn, st.uniform.typeId == (reached ? uniformAfter : c.stateUniform) &&
                                st.uniform.uid == (reached && c.stateUniform == -1 ? -1 : 77));
                OB(r.spawn, g_uses == (made && c.script ? std::vector<int>{0} : std::vector<int>{}));
                if (sent) {
                    S_FakeStream *s = lastStream();
                    OB(r.spawn, g_streams.size() == 1 && s->id == a->m_netId && s->sent &&
                                    s->bits == spawnBits(c.slot, actorSlot, pos, dir, str, st, statsArg) &&
                                    g_itStrings.size() == str + 1 && g_itStrings[str] == g_obPlayers[c.slot].name);
                } else {
                    OB(r.spawn, g_streams.empty());
                }
                if (h) {
                    const bool player = !local && c.controller == P;
                    const bool told = player || (!local && c.loading && c.controller == R);
                    OB(r.spawn, placedAt(h, pos, c.scene) && h->setups.size() == 1 && h->setups[0].state == &st &&
                                    h->setups[0].role == (c.npc ? HUMAN_ROLE_NPC_TEAM : HUMAN_ROLE_PLAYER) &&
                                    h->setups[0].group == 1 && h->setups[0].stats == statsArg && h->setups[0].playerSlot == c.slot);
                    OB(r.spawn, h->startMissions == (c.role & GAME_ROLE_CLIENT ? std::vector<bool>{true, false} : std::vector<bool>{true}));
                    OB(r.spawn, h->playerTeams == std::vector<std::pair<int, bool>>(player ? (c.loading ? 2 : 1) : 0, {2, false}) &&
                                    h->controller->spectating == (told && st.spectating) && h->controller->vip == (told && st.vip));
                    OB(r.spawn, local ? g_obPlayers[c.slot].human == nullptr
                                      : g_obPlayers[c.slot].human == h && g_obPlayers[c.slot].activityTime >= t0 &&
                                            g_obPlayers[c.slot].activityTime <= t1);
                }
                f->SetData(nullptr);
                creator->Destroy(a);
                ++index;
            }
            g_actorsById.erase(200);
            g_obZone = nullptr;
            creator->Destroy(zone);
            for (C_SpawnPoint *p : points)
                creator->Destroy(p);
            g_obSpawnPoints.clear();
        }

        // --- SetupSpawned: the human made (a client), found by its id (a server), the spawned one
        // (single player) ---------------------------------------------------------------------------
        {
            I3D_frame *f = placed(FRAME_DUMMY, "ob.setup", false);
            auto *a = static_cast<C_SpawnPoint *>(creator->Create());
            // Without a frame: nothing.
            g_gameRole = GAME_ROLE_CLIENT;
            GetGame()->m_scene = g_carScene;
            spReset();
            S_SquadSoldier st0;
            const S_vector4 p0 = {1.0f, 2.0f, 3.0f, 1.0f}, d0 = {0.0f, 0.0f, 1.0f, 0.0f};
            a->SetupSpawned(0, 3, p0, d0, &st0, nullptr);
            OB(r.spawnSetup, g_itLog.empty() && g_cacheLoads.empty());
            a->SetFrame(f);
            for (int m = 0; m < 192; ++m) {
                const uint8_t roles[] = {GAME_ROLE_CLIENT, GAME_ROLE_SERVER, GAME_ROLE_SERVER | GAME_ROLE_CLIENT, GAME_ROLE_LOCAL};
                const int controllers[] = {HUMAN_CONTROLLER_PLAYER, HUMAN_CONTROLLER_REMOTE, HUMAN_CONTROLLER_ENEMY};
                const uint8_t role = roles[m % 4];
                const int ctrl = controllers[(m / 4) % 3];
                const int bits = m / 12;
                const bool localSlot = bits & 1, npc = bits & 2, member = bits & 4, voice = bits & 8;
                const bool warmup = m % 5 == 0, localHuman = m % 3 != 1, none = m % 7 == 6;
                const bool server = role & GAME_ROLE_SERVER, local = role == GAME_ROLE_LOCAL;
                const int slot = m % 6, actorSlot = 8 + m % 20;
                a->m_npc = npc ? 1 : 0;
                g_gameRole = role;
                g_localPlayerSlot = localSlot ? slot : 7;
                GetGame()->m_roundState = warmup ? ROUND_WARMUP : ROUND_PLAYING;
                g_obDetails.voiceChat = voice;
                g_coop = m % 2 == 1;                    // (a client never adds to the co-op team)
                S_SquadSoldier st;
                st.team = 1 + m % 3;
                st.spectating = m % 2 == 0;
                st.vip = m % 4 < 2;
                st.uniform.typeId = static_cast<int16_t>(m % 8 == 0 ? 0x60 : m % 8 == 1 ? 0x61 : -1);
                int stats[STAT_COUNT];
                for (int i = 0; i < STAT_COUNT; ++i)
                    stats[i] = m + i;
                const int *statsArg = m % 3 == 2 ? nullptr : stats;
                const S_vector4 pos = {rnd(-20, 20), rnd(0, 3), rnd(-20, 20), 1.0f};
                const S_vector4 dir = {rnd(-1, 1), 0.0f, rnd(-1, 1), 0.0f};
                C_Human found;
                C_PlayerController pc;
                C_RemoteController rc;
                found.controllerType = ctrl;
                found.controller = ctrl == HUMAN_CONTROLLER_PLAYER ? static_cast<C_HumanController *>(&pc) : &rc;
                found.localPlayer = localHuman;
                g_obControllerType = ctrl;
                g_obLocalHuman = localHuman;
                g_obMakeMember = member;
                g_obNoHuman = none;
                g_obNoFrame = false;
                g_cacheFailName.clear();
                g_playerTeam.clear();
                if (member)
                    g_playerTeam.insert(&found);
                g_actorsById.erase(actorSlot);
                if (server && !local && !none)
                    g_actorsById[actorSlot] = &found;
                a->m_spawned = local && !none ? &found : nullptr;
                g_obPlayers[slot].human = nullptr;
                spReset();
                const size_t humans = g_obMadeHumans.size();
                a->SetupSpawned(slot, actorSlot, pos, dir, &st, statsArg);

                std::vector<std::string> log;
                bool have = !none;
                if (!server)
                    have = soldierLog(log, actorSlot, v3(dir), true, none ? 3 : 0, true, false);
                const C_Human *h = !server ? (g_obMadeHumans.size() > humans ? g_obMadeHumans.back() : nullptr)
                                           : have ? &found : nullptr;
                if (have)
                    setupLog(log, slot, npc, server, local, ctrl, localHuman, member, warmup, voice);
                OB(r.spawnSetup, SaLogIs(log) && (h != nullptr) == have);
                if (h) {
                    const bool player = !local && ctrl == HUMAN_CONTROLLER_PLAYER;
                    const bool told = !local && (ctrl == HUMAN_CONTROLLER_PLAYER || ctrl == HUMAN_CONTROLLER_REMOTE);
                    OB(r.spawnSetup, h->startMissions == (server ? std::vector<bool>{} : std::vector<bool>{false}));
                    OB(r.spawnSetup, h->playerTeams == std::vector<std::pair<int, bool>>(player ? 1 : 0, {st.team, false}) &&
                                         h->controller->spectating == (told && st.spectating) &&
                                         h->controller->vip == (told && st.vip));
                    if (!server)
                        OB(r.spawnSetup, placedAt(h, v3(pos), true) && h->setups.size() == 1 && h->setups[0].state == &st &&
                                             h->setups[0].role == (npc ? HUMAN_ROLE_NPC_TEAM : HUMAN_ROLE_PLAYER) &&
                                             h->setups[0].group == 1 && h->setups[0].stats == statsArg &&
                                             h->setups[0].playerSlot == slot &&
                                             g_cacheLoads == std::vector<std::string>{modelOf(st.uniform.typeId)} &&
                                             g_obPlayers[slot].human == h);
                    else
                        OB(r.spawnSetup, h->setups.empty() && g_cacheLoads.empty() && g_obPlayers[slot].human == nullptr);
                }
                g_actorsById.erase(actorSlot);
            }
            a->m_spawned = nullptr;
            creator->Destroy(a);
        }

        // --- OnMessage: a client makes the soldier from the message ------------------------------------
        {
            I3D_frame *f = placed(FRAME_DUMMY, "ob.msg", true);
            auto *a = static_cast<C_SpawnPoint *>(creator->Create());
            a->SetFrame(f);
            // Not a client, no stream: nothing read; another kind: only the kind.
            g_gameRole = GAME_ROLE_SERVER;
            SaRead({{0u, 2}, {1u, 8}});
            spReset();
            a->OnMessage(newStream());
            OB(r.spawnNet, g_readPos == 0 && g_itLog.empty());
            g_gameRole = GAME_ROLE_CLIENT;
            a->OnMessage(nullptr);
            OB(r.spawnNet, g_readPos == 0 && g_itLog.empty());
            for (uint32_t kind = 1; kind < 4; ++kind) {
                SaRead({{kind, 2}, {1u, 8}});
                a->OnMessage(newStream());
                OB(r.spawnNet, g_readPos == 1 && !g_readMismatch && g_itLog.empty());
            }
            for (int m = 0; m < 24; ++m) {
                const uint8_t roles[] = {GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT, GAME_ROLE_LOCAL};
                const uint8_t role = roles[m % 3];
                const bool server = role & GAME_ROLE_SERVER, local = role == GAME_ROLE_LOCAL;
                const int slot = m % 8, actorSlot = 4 + m;
                const uint32_t use = m % 4 == 3 ? 0xFFu : static_cast<uint32_t>(m % 4);
                const int connection = 1000 + m;
                g_itStrings.push_back("net" + std::to_string(m));
                const auto str = static_cast<uint32_t>(g_itStrings.size() - 1);
                const S_vector pos(rnd(-30, 30), rnd(0, 4), rnd(-30, 30)), dir(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
                const int profile = 20 + m, uniform = m % 4 == 0 ? 0x61 : m % 4 == 1 ? 0x62 : m % 4 == 2 ? 0x60 : -1;
                int stats[STAT_COUNT];
                for (int i = 0; i < STAT_COUNT; ++i)
                    stats[i] = (m * 7 + i * 3) % 128;
                stats[0] = 500 + m;
                T_Bits b = {{0u, 2}, {static_cast<uint32_t>(slot), 8}, {static_cast<uint32_t>(actorSlot), 8}};
                for (float v : {pos.x, pos.y, pos.z})
                    b.emplace_back(ItF(v), -11);
                for (float v : {dir.x, dir.y, dir.z})
                    b.emplace_back(ItF(v), -2);
                b.emplace_back(use, 8);
                b.emplace_back(str, -6);
                b.emplace_back(static_cast<uint32_t>(connection), 32);
                b.emplace_back(static_cast<uint32_t>(profile), -12);
                b.emplace_back(static_cast<uint32_t>(uniform), -12);
                if (!local) {
                    b.emplace_back(static_cast<uint32_t>(stats[0]), 10);
                    for (int i : {2, 1, 3, 4, 5, 6, 7, 8, 9, 11, 10})
                        b.emplace_back(static_cast<uint32_t>(stats[i]), 7);
                }
                const bool npc = m % 5 == 0, voice = m % 3 == 0, localHuman = m % 4 < 2;
                const int ctrl = m % 2 ? HUMAN_CONTROLLER_PLAYER : HUMAN_CONTROLLER_REMOTE;
                a->m_npc = npc ? 1 : 0;
                g_gameRole = role;
                g_localPlayerSlot = m % 2 ? slot : 9;
                GetGame()->m_roundState = ROUND_PLAYING;
                g_obDetails.voiceChat = voice;
                g_coop = m % 4 < 2;
                g_obControllerType = ctrl;
                g_obLocalHuman = localHuman;
                g_obMakeMember = false;
                g_obNoHuman = false;
                g_playerTeam.clear();
                C_Human found;
                C_PlayerController pc;
                found.controllerType = HUMAN_CONTROLLER_PLAYER;
                found.controller = &pc;
                g_actorsById.erase(actorSlot);
                if (server && !local)
                    g_actorsById[actorSlot] = &found;
                a->m_spawned = local ? &found : nullptr;
                g_obPlayers[slot].human = nullptr;
                SaRead(b);
                spReset();
                const size_t humans = g_obMadeHumans.size();
                a->OnMessage(newStream());

                std::vector<std::string> log = {"readstring 255"};
                if (!server)
                    log.push_back("slotuse " + std::to_string(actorSlot) + " " + std::to_string(static_cast<int8_t>(use) - 1));
                log.push_back("readstate 0");
                log.push_back("setplayer " + std::to_string(slot) + " " + std::to_string(connection) + " net" + std::to_string(m));
                if (!server)
                    soldierLog(log, actorSlot, dir, true, 0, true, false);
                const int hCtrl = server ? HUMAN_CONTROLLER_PLAYER : ctrl;
                setupLog(log, slot, npc, server, local, hCtrl, server ? false : localHuman, false, false, voice);
                const C_Human *h = server ? &found : g_obMadeHumans.size() > humans ? g_obMadeHumans.back() : nullptr;
                OB(r.spawnNet, SaReadAll() && SaLogIs(log) && h);
                if (h) {
                    const bool player = !local && hCtrl == HUMAN_CONTROLLER_PLAYER;
                    OB(r.spawnNet, h->playerTeams == std::vector<std::pair<int, bool>>(player ? 1 : 0, {-1, false}) &&
                                       !h->controller->spectating && !h->controller->vip &&
                                       h->startMissions == (server ? std::vector<bool>{} : std::vector<bool>{false}));
                }
                if (h && !server && h->setups.size() == 1) {
                    const C_Human::S_Setup &s = h->setups[0];
                    S_SoldierProgress zero;
                    std::memset(static_cast<void *>(&zero), 0, sizeof zero);
                    OB(r.spawnNet, h->setups.size() == 1 && s.role == (npc ? HUMAN_ROLE_NPC_TEAM : HUMAN_ROLE_PLAYER) && s.group == 1 &&
                                       s.playerSlot == slot && s.stats != nullptr &&
                                       s.statValues == std::vector<int>(stats, stats + STAT_COUNT));
                    const S_SoldierState &c = s.copy;
                    OB(r.spawnNet, c.profileIndex == profile && c.uniform.typeId == uniform && c.team == -1 && c.spawnZone == 0x3FF &&
                                       !c.spectating && !c.vip && c.deployed && c.speechSet == -1 && c.unknown34 == -1 &&
                                       c.unknown38 == -1 && c.health == 0 && SaSame(&c.progress, &zero, sizeof zero) &&
                                       c.belt.empty() && c.backpackContents.empty() && c.head.typeId == -1 && c.hands.typeId == -1);
                    OB(r.spawnNet, placedAt(h, pos, true) && g_cacheLoads == std::vector<std::string>{modelOf(uniform)} &&
                                       g_obPlayers[slot].human == h);
                } else {
                    OB(r.spawnNet, server && g_cacheLoads.empty() && (!h || h->setups.empty()));
                }
                g_actorsById.erase(actorSlot);
            }
            a->m_spawned = nullptr;
            creator->Destroy(a);
        }

        // --- ReadProperties: the version (3) first, then the fields in any order --------------------------
        {
            auto *a = static_cast<C_SpawnPoint *>(creator->Create());
            const auto props = [&](const S_SaBytes &in) {
                S_SaReader rd(in);
                return a->ReadProperties(rd.c);
            };
            const auto fields = [&](int team, int kind, int uniform, int unknown98, int npc) {
                return a->m_team == team && a->m_kind == kind && a->m_uniform == uniform && a->m_unknown98 == unknown98 &&
                       a->m_npc == npc && a->IsEnabled() && a->GetSpawnedActor() == nullptr;
            };
            S_SaBytes all;
            all.Head(0x777, 6 + 6 * 10).Head(0, 10).Val(3).Head(1, 10).Val(2).Head(2, 10).Val(1).Head(3, 10).Val(0x60)
                .Head(4, 10).Val(77).Head(5, 10).Val(1);
            OB(r.spawnProps, props(all) && fields(2, 1, 0x60, 77, 1));
            S_SaBytes order;
            order.Head(0x777, 6 + 14 + 4 * 10).Head(0, 14).Val(3).Val(99).Head(5, 10).Val(0).Head(2, 10).Val(2).Head(1, 10)
                .Val(4).Head(2, 10).Val(0);
            OB(r.spawnProps, props(order) && fields(4, 0, 0x60, 77, 0));
            S_SaBytes v2;
            v2.Head(0x777, 6 + 20).Head(0, 10).Val(2).Head(1, 10).Val(9);
            OB(r.spawnProps, !props(v2) && fields(4, 0, 0x60, 77, 0));
            S_SaBytes late;
            late.Head(0x777, 6 + 20).Head(1, 10).Val(9).Head(0, 10).Val(3);
            OB(r.spawnProps, !props(late) && fields(4, 0, 0x60, 77, 0));
            S_SaBytes none;
            none.Head(0x777, 6);
            OB(r.spawnProps, !props(none) && fields(4, 0, 0x60, 77, 0));
            S_SaBytes shortVersion;
            shortVersion.Head(0x777, 6 + 8 + 10).Head(0, 8).Val(static_cast<uint16_t>(3)).Head(1, 10).Val(9);
            OB(r.spawnProps, !props(shortVersion) && fields(4, 0, 0x60, 77, 0));
            // A short team chunk: the version's 3.
            S_SaBytes shortTeam;
            shortTeam.Head(0x777, 6 + 10 + 8 + 10).Head(0, 10).Val(3).Head(1, 8).Val(static_cast<uint16_t>(5)).Head(3, 10).Val(0x61);
            OB(r.spawnProps, props(shortTeam) && fields(3, 0, 0x61, 77, 0));
            // Quirk kept: another chunk is entered and not left (its children are read, what follows it not).
            S_SaBytes nested;
            nested.Head(0x777, 6 + 10 + 26 + 10).Head(0, 10).Val(3).Head(9, 26).Head(1, 10).Val(6).Head(4, 10).Val(5)
                .Head(2, 10).Val(1);
            OB(r.spawnProps, props(nested) && fields(6, 0, 0x61, 5, 0));
            creator->Destroy(a);
        }

        g_obDefs.erase(0x60);
        g_obDefs.erase(0x61);
        g_obDefs.erase(0x62);
        uniformDef.m_modelName = plainDef.m_modelName = coatDef.m_modelName = nullptr;
        for (C_Human *h : g_obMadeHumans) {
            for (auto it = g_actorsById.begin(); it != g_actorsById.end();)
                it = it->second == h ? g_actorsById.erase(it) : std::next(it);
            delete h;
        }
        g_obMadeHumans.clear();
        for (C_HumanController *c : g_obControllers)
            delete c;
        g_obControllers.clear();
        g_obMadeFrames.clear();
        g_playerTeam.clear();
        g_cacheFailName.clear();
        g_obNoFrame = g_obNoHuman = g_obMakeMember = g_obLocalHuman = false;
        g_obControllerType = HUMAN_CONTROLLER_ENEMY;
        g_driverVtable[0x54 / 4] = oldCreateFrame;
        *reinterpret_cast<void ***>(g_teamManager.GetPlayerTeam()) = oldPlayerTeam;
        *reinterpret_cast<void ***>(g_teamManager.GetCoopTeam()) = oldCoopTeam;
        g_cacheFill = oldFill;
        g_localPlayerSlot = oldLocalSlot;
        g_mpSettingsValue = oldMp;
        g_coop = false;
        g_obDetails.voiceChat = false;
        GetGame()->m_roundState = oldRound;
        GetGame()->m_mode = oldMode;
        GetGame()->m_scene = g_carScene;
    }
