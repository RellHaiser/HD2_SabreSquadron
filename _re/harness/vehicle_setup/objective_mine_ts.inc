// (objective_actors_test.inc, inside ObjectiveActorsTest: the mine and the TS explosion.)

    // --- the mine ----------------------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_MINE];
        OB(r.mine, creator && std::strcmp(creator->GetName(), "Mine") == 0);
        // The mine items: 0x46 (humans only), 0x47 (cars only), 0x50 (another, no component).
        C_MineComp humansComp, carsComp;
        humansComp.m_damage = 300.0f;
        humansComp.m_shotSound = 12;
        humansComp.m_effect = 7;
        humansComp.m_explosionRadius = 3.5f;
        carsComp.m_damage = 2000.0f;
        carsComp.m_shotSound = 13;
        carsComp.m_effect = 8;
        carsComp.m_explosionRadius = 6.0f;
        C_ItTestDef humansDef, carsDef, otherDef;
        humansDef.m_textId = 600;
        humansDef.m_component = &humansComp;
        carsDef.m_textId = 601;
        carsDef.m_component = &carsComp;
        otherDef.m_textId = 602;
        g_obDefs[0x46] = &humansDef;
        g_obDefs[0x47] = &carsDef;
        g_obDefs[0x50] = &otherDef;
        // The frame's z axis normalised as the original's inline code does it.
        const auto axisOf = [](const S_vector &v) {
            const float sq = (v.y * v.y + v.z * v.z) + v.x * v.x;
            if (std::fabs(sq - 1.0f) < 1e-8f)
                return v;
            const float k = 1.0f / std::sqrt(sq);
            return S_vector(v.x * k, v.y * k, v.z * k);
        };
        C_Human owner, walker, driver;
        owner.m_netId = 5;
        walker.m_netId = 6;
        alignas(16) static uint8_t carMemory[sizeof(C_Car)];
        std::memset(carMemory, 0, sizeof carMemory);
        auto *car = reinterpret_cast<C_Car *>(carMemory);
        car->m_type = ACTOR_CAR;
        car->m_driver = &driver;
        const int itemIds[] = {0x46, 0x47, 0x50, 0x99};
        for (int k = 0; k < 4; ++k) {
            const int itemId = itemIds[k];
            I3D_frame *f = placed(FRAME_VISUAL, "ob.mine", k % 2 == 0);
            f->m_localMatrix.m[2][3] = rnd(-1, 1);       // (the direction's w)
            auto *a = static_cast<C_Mine *>(creator->Create());
            reset();
            OB(r.mine, a->SetFrame(f) && a->GetFrame() == f && SaLogIs({"name Actor_mine"}));
            g_gameRole = GAME_ROLE_SERVER;
            // Setup: sunk along the z axis (0x46 0.07 m, 0x47 0.08 m), shown, the item's name.
            const S_vector axis = axisOf(f->GetMatrix()(2));
            const S_vector at = f->GetWorldPos();
            a->m_armTime = 55;                          // (Setup starts the countdown anew)
            a->m_armed = true;
            reset();
            a->Setup(k == 1 ? nullptr : &owner, itemId, -3 - k);
            std::vector<std::string> log;
            if (itemId == 0x46 || itemId == 0x47) {
                const float d = itemId == 0x46 ? 0.07f : 0.08f;
                log.push_back("setpos " + ItName(f) + " " + ItV(S_vector(at.x - axis.x * d, at.y - axis.y * d, at.z - axis.z * d)));
            }
            log.push_back("on " + ItName(f) + " 1");
            log.push_back("update " + ItName(f));
            log.push_back("sector " + ItName(f) + " " + ItV(f->GetWorldPos()));
            log.push_back("update " + ItName(f));
            const std::string name = itemId == 0x99 ? "text -1" : "text " + std::to_string(600 + (itemId == 0x47) + 2 * (itemId == 0x50));
            OB(r.mine, SaLogIs(log) && a->GetItemId() == itemId && a->GetOwner() == (k == 1 ? nullptr : &owner) &&
                           a->GetMaterialId(f) == -3 - k && a->m_armTime == 0 && !a->m_armed &&
                           std::strcmp(*a->GetName(), name.c_str()) == 0);
            C_chunk none;
            OB(r.mine, !a->WriteProperties(none) && !a->ReadProperties(none));
            const int copies = g_actorCopies;
            a->CopyFrom(a);
            OB(r.mine, g_actorCopies == copies);
            // StartMission: not in the world (yet), activated.
            a->m_volumeAdded = true;
            reset();
            a->StartMission(true);
            OB(r.mine, !a->m_volumeAdded && g_volumeLog.empty() && g_activations == std::vector<bool>{true});

            // The countdown: Tick on a server without a client side, Update on clients; armed at 2 s.
            const uint8_t roles[] = {GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_LOCAL};
            const uint8_t role = roles[k % 3];
            g_gameRole = role;
            const bool byTick = role == GAME_ROLE_SERVER;
            a->m_position.w = 9.0f;
            // (A volume left from before: Arm makes it anew.)
            std::memset(static_cast<void *>(&a->m_volume), 0x5A, sizeof a->m_volume);
            reset();
            const int steps[] = {700, 1000, 299, 1};
            int time = 0;
            for (int step : steps) {
                if (byTick) {
                    a->Update(5000);
                    a->Tick(step);
                } else {
                    a->Tick(5000);
                    a->Update(step);
                }
                time += step;
                OB(r.mineArm, a->m_armTime == (time >= 2000 ? -1 : time) && a->m_armed == (time >= 2000));
            }
            const S_matrix &w = f->GetMatrix();
            const S_vector dir = axisOf(w(2));
            const S_vector4 pos = {w.m[3][0], w.m[3][1] + 0.2f, w.m[3][2], 9.0f};
            OB(r.mineArm, SaSame(&a->m_position, &pos, 16) && RbSame(S_vector(a->m_direction.x, a->m_direction.y, a->m_direction.z), dir) &&
                              a->m_direction.w == w.m[2][3] && a->m_volumeAdded && g_volumeLog.size() == 1 &&
                              g_volumeLog[0].op == 'A' && g_volumeLog[0].volume == &a->m_volume);
            const S_CollisionVolume &v = a->m_volume;
            OB(r.mineArm, v.flags == COLLISION_SHAPE_SPHERE && v.frame == f && v.type == COLLISION_TYPE_MINE && v.actor == a &&
                              SaSame(&v.center, &pos, 12) && v.centerW == 9.0f &&
                              v.radius == (itemId == 0x47 ? 0.4f : 0.15f) && v.cell == nullptr && v.matrix == nullptr);
            a->Tick(100000);
            a->Update(100000);
            OB(r.mineArm, a->m_armTime == -1);

            // OnSteppedOn: armed; not the layer; humans don't set off 0x47, cars not 0x46. The culprit: the
            // layer, else the human, else the car's driver; the clients told; the explosion 0.2 m above the
            // sphere; a server without a client side removes it.
            C_Actor *const actors[] = {&owner, &walker, reinterpret_cast<C_Actor *>(car)};
            for (C_Actor *actor : actors) {
                for (int armed = 0; armed < 2; ++armed) {
                    a->m_armed = armed != 0;
                    a->m_volumeAdded = true;
                    reset();
                    a->OnSteppedOn(actor, -1);
                    const bool isHuman = actor != reinterpret_cast<C_Actor *>(car);
                    const bool off = armed && actor != a->GetOwner() && !(isHuman && itemId == 0x47) && !(!isHuman && itemId == 0x46);
                    if (!off) {
                        OB(r.mineStep, a->m_armed == (armed != 0) && g_volumeLog.empty() && g_streams.empty() && g_explosions.empty());
                        continue;
                    }
                    C_Actor *culprit = a->GetOwner() ? static_cast<C_Actor *>(a->GetOwner()) : isHuman ? actor : &driver;
                    const C_MineComp *comp = itemId == 0x46 ? &humansComp : itemId == 0x47 ? &carsComp : nullptr;
                    const S_vector at2(a->m_position.x, a->m_position.y + 0.2f, a->m_position.z);
                    bool exploded = comp ? g_explosions.size() == 1 && g_explosions[0].source == culprit &&
                                               RbSame(g_explosions[0].pos, at2) && g_explosions[0].range == comp->m_explosionRadius &&
                                               g_explosions[0].damage == comp->m_damage && g_explosions[0].sound == comp->m_shotSound
                                         : g_explosions.empty();
                    OB(r.mineStep, !a->m_armed && !a->m_volumeAdded && g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R' &&
                                       g_streams.size() == 1 && g_streams[0]->bits == T_Bits{{0, 2}} && g_streams[0]->sent &&
                                       g_itTemps.size() == 1 && g_itTemps[0].reliable && exploded &&
                                       g_itDestroyed.size() == (role == GAME_ROLE_SERVER ? 1u : 0u));
                }
            }
            // Not in the world: nothing to take out.
            a->m_armed = true;
            a->m_volumeAdded = false;
            reset();
            a->OnSteppedOn(&walker, -1);
            OB(r.mineStep, g_volumeLog.empty() && (itemId == 0x47 || !a->m_armed));
            // Defuse: the clients told (1), a server without a client side removes it.
            a->m_armed = true;
            reset();
            a->Defuse();
            OB(r.mineStep, !a->m_armed && g_streams.size() == 1 && g_streams[0]->bits == T_Bits{{1, 2}} && g_streams[0]->sent &&
                               g_itTemps.size() == 1 && g_itTemps[0].reliable && g_streams[0]->id == a->m_netId &&
                               g_itDestroyed.size() == (role == GAME_ROLE_SERVER ? 1u : 0u));
            // OnMessage: 0 the explosion's effects (then removed), 1 removed, others nothing.
            for (uint32_t kind = 0; kind < 4; ++kind) {
                a->m_armed = true;
                SaRead({{kind, 2}});
                reset();
                a->OnMessage(nullptr);
                const C_MineComp *comp = itemId == 0x46 ? &humansComp : itemId == 0x47 ? &carsComp : nullptr;
                bool shown = kind != 0 || !comp ? g_explosionsShown.empty()
                                                : g_explosionsShown.size() == 1 &&
                                                      RbSame(g_explosionsShown[0].pos, S_vector(a->m_position.x, a->m_position.y, a->m_position.z)) &&
                                                      RbSame(g_explosionsShown[0].dir, S_vector(a->m_direction.x, a->m_direction.y, a->m_direction.z)) &&
                                                      g_explosionsShown[0].effect == comp->m_effect && g_explosionsShown[0].material == -1 &&
                                                      g_explosionsShown[0].sound == comp->m_shotSound;
                OB(r.mineStep, SaReadAll() && shown && g_itDestroyed.size() == (kind < 2 ? 1u : 0u) && a->m_armed == (kind >= 2));
            }
            // StopMission: out of the world when in.
            for (int in = 0; in < 2; ++in) {
                a->m_volumeAdded = in != 0;
                reset();
                a->StopMission(false);
                OB(r.mine, !a->m_volumeAdded && g_volumeLog.size() == static_cast<size_t>(in) &&
                               g_deactivations == std::vector<bool>{false});
            }

            // The saved game: chunk 1 the place, the direction, the countdown and the flags; chunk 2 the
            // layer's id (with one). LoadGame: placed, armed again; false.
            a->m_armTime = 1234;
            a->m_armed = k % 2 == 0;
            a->m_volumeAdded = true;
            S_SaBytes e;
            const S_vector wp = f->GetWorldPos();
            const S_vector wd = axisOf(f->GetMatrix()(2));
            e.Head(0x777, 6 + 42 + (a->GetOwner() ? 10 : 0)).Head(1, 42).Val(wp).Val(wd).Val(1234).Val(a->m_armed ? 1 : 0).Val(1);
            if (a->GetOwner())
                e.Head(2, 10).Val(5);
            bool saved = false;
            OB(r.mineSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == e.b && saved);
            const S_vector lp(rnd(-5, 5), rnd(0, 2), rnd(-5, 5)), ld(0.6f, 0.0f, 0.8f);
            for (int armedLoad = 0; armedLoad < 2; ++armedLoad) {
                S_SaBytes in;
                // (Another chunk first: entered and not left, the state and the layer read inside it.)
                in.Head(0x777, 6 + 6 + 42 + 10).Head(7, 6 + 42 + 10).Head(1, 42).Val(lp).Val(ld).Val(800).Val(armedLoad).Val(0);
                in.Head(2, 10).Val(6);
                g_obHumans[6] = &walker;
                S_SaReader rd(in);
                a->m_owner = nullptr;
                reset();
                const bool ok = a->LoadGame(rd.c);
                std::vector<std::string> llog = {"setpos " + ItName(f) + " " + ItV(lp), "setdir " + ItName(f) + " " + ItV(ld) + " 0",
                                                 "gethuman 6"};
                OB(r.mineSave, !ok && SaLogIs(llog) && a->m_armTime == (armedLoad ? -1 : 800) && a->m_armed == (armedLoad != 0) &&
                                   a->m_volumeAdded == (armedLoad != 0) && a->GetOwner() == &walker &&
                                   g_volumeLog.size() == static_cast<size_t>(armedLoad));
                g_obHumans.clear();
            }
            // A short chunk: the flags and the layer's id share the last value read.
            {
                S_SaBytes in;
                in.Head(0x777, 6 + 38 + 8).Head(1, 38).Val(lp).Val(ld).Val(55).Val(1).Head(2, 8).Val(static_cast<uint16_t>(0));
                g_obHumans[1] = &owner;
                S_SaReader rd(in);
                a->m_volumeAdded = false;
                reset();
                a->LoadGame(rd.c);
                OB(r.mineSave, a->m_armTime == -1 && a->m_armed && a->GetOwner() == &owner);
                g_obHumans.clear();
            }
            creator->Destroy(a);
        }
        g_obDefs.clear();
    }

    // --- the TS explosion ------------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_TS_EXPLOSION];
        OB(r.ts, creator && std::strcmp(creator->GetName(), "Explosion [TS]") == 0);
        // The decal's log (the projector's frames are not named: "?").
        const auto scorchLog = [&](I3D_frame *f, bool hasTexture) {
            const S_vector at = f->GetWorldPos();
            const S_vector pos(at.x, at.y + 0.5f, at.z);
            return std::vector<std::string>{"texture ? " + std::to_string(hasTexture), "projmode ? 0", "on ? 1", "addframe ?",
                                            "setpos ? " + ItV(pos), "setdir ? 0 -1 0 0", "projb4 ? 5", "proja4 ? 1",
                                            "getsector " + ItV(pos), "link ? sector 0", "update ?", "projmode ? 3"};
        };
        const auto particlesOk = [&](I3D_frame *f, const std::vector<int> &effects) {
            if (g_rbSplashes.size() != effects.size())
                return false;
            for (size_t i = 0; i < effects.size(); ++i) {
                const S_RbSplash &s = g_rbSplashes[i];
                if (s.effect != effects[i] || !RbSame(s.pos, f->GetWorldPos()) || !RbSame(s.dir, S_vector(0, 1, 0)) ||
                    s.parent != nullptr || !s.faceCamera || s.transient)
                    return false;
            }
            return true;
        };
        for (int k = 0; k < 3; ++k) {
            I3D_frame *f = placed(FRAME_DUMMY, "ob.ts", k != 1);
            auto *a = static_cast<C_TSExplosion *>(creator->Create());
            OB(r.ts, a->m_countdownSound == -1 && a->m_delay == -1 && a->m_explosionSound == -1 && a->m_particle == -1 &&
                         a->m_particle2 == -1);
            a->m_flags = 5;
            a->m_delay = 7;
            a->m_clientTime = 8;
            a->m_countdownSound = 9;
            a->m_explosionSound = 10;
            a->m_range = 11;
            a->m_damage = 12;
            a->m_particle = 13;
            a->m_particle2 = 14;
            a->m_joinParticles = 15;
            a->m_serverTime = 16;
            a->m_scorch = reinterpret_cast<decltype(a->m_scorch)>(f);   // (not used: cleared)
            OB(r.ts, a->SetFrame(f) && a->GetFrame() == f && a->m_flags == 0 && a->m_delay == -1 && a->m_clientTime == 0 &&
                         a->m_serverTime == 0 &&
                         a->m_countdownSound == -1 && a->m_explosionSound == 10 && a->m_range == 0.0f && a->m_damage == 0.0f &&
                         a->m_particle == -1 && a->m_particle2 == -1 && a->m_joinParticles == 0 && !a->m_scorch);
            a->m_netId = 60 + k;
            a->m_delay = k == 2 ? 0 : 500;
            a->m_countdownSound = k == 1 ? -1 : 21;
            a->m_explosionSound = k == 0 ? -1 : 22;
            a->m_range = 8.0f;
            a->m_damage = 900.0f;
            a->m_particle = k == 1 ? -1 : 31;
            a->m_particle2 = k == 2 ? -1 : 32;
            a->m_joinParticles = k;
            // StartMission: the client side loads the decal's texture (not activated when it fails).
            a->m_flags = 3;
            a->m_serverTime = 4;
            a->m_clientTime = 5;
            a->m_texture = reinterpret_cast<decltype(a->m_texture)>(f);   // (not used: cleared)
            reset();
            a->StartMission(true);
            OB(r.ts, g_itLog.empty() && g_activations == std::vector<bool>{true} && a->m_flags == 0 && a->m_serverTime == 0 &&
                         a->m_clientTime == 0 &&
                         !a->m_texture);
            g_igraphVtable[0x3C / 4] = reinterpret_cast<void *>(&F_ObLoadTextureFail);
            reset();
            a->StartMission(false);
            OB(r.ts, SaLogIs({"loadtexture x_expl00.tga 402"}) && g_activations.empty() && !a->m_texture);
            g_igraphVtable[0x3C / 4] = reinterpret_cast<void *>(&F_ObLoadTexture);
            reset();
            a->StartMission(false);
            OB(r.ts, SaLogIs({"loadtexture x_expl00.tga 402 0"}) && g_activations == std::vector<bool>{false} && a->m_texture);

            // The server: Trigger tells the clients (an empty message) and counts the delay down; then the
            // area damage 1 m above the frame.
            g_gameRole = GAME_ROLE_SERVER;
            a->m_serverTime = 123;
            reset();
            a->Trigger(1);
            // (The states by the original's numbers: 1 counting, 2 a client's countdown, 4 exploded, 8
            // the effects made.)
            const S_matrix &w = f->GetMatrix();
            const auto exploded = [&] {
                return g_obExplosions.size() == 1 && RbSame(g_obExplosions[0].pos, S_vector(w.m[3][0], w.m[3][1] + 1.0f, w.m[3][2])) &&
                       g_obExplosions[0].posW == w.m[3][3] && g_obExplosions[0].range == 8.0f && g_obExplosions[0].damage == 900.0f &&
                       !g_obExplosions[0].unknown18 && g_obExplosions[0].source == nullptr;
            };
            OB(r.tsStep, g_streams.size() == 1 && g_streams[0]->bits.empty() && g_streams[0]->sent && g_streams[0]->id == 60 + k &&
                             a->m_serverTime == 0 &&
                             (k == 2 ? exploded() && a->GetState() == 4
                                     : g_obExplosions.empty() && a->GetState() == 1));
            if (k != 2) {
                a->Update(1000);
                a->Tick(300);
                OB(r.tsStep, g_obExplosions.empty() && a->m_serverTime == 300 && a->m_clientTime == 0);
                a->Tick(199);
                OB(r.tsStep, g_obExplosions.empty());
                a->Tick(1);
                OB(r.tsStep, exploded() && a->GetState() == 4 && a->m_serverTime == 500);
                a->Tick(1000);
                OB(r.tsStep, g_obExplosions.size() == 1 && a->m_serverTime == 500);
            }
            // A client: the message starts the countdown (its sound at once), then the particles, the
            // explosion's sound and the decal.
            g_gameRole = GAME_ROLE_CLIENT;
            a->m_flags = 0;
            a->m_clientTime = 77;
            // (A decal made before stays, unreferenced, beside the new one: quirk kept.)
            a->m_scorch = reinterpret_cast<decltype(a->m_scorch)>(MakeFrame(g_projectorVtable, FRAME_VISUAL, true));
            reset();
            a->OnMessage(nullptr);
            const S_vector soundAt(w.m[3][0], w.m[3][1] + 1.0f, w.m[3][2]);
            std::vector<std::string> effectsLog;
            std::vector<int> effects;
            if (a->m_particle != -1)
                effects.push_back(a->m_particle);
            if (a->m_particle2 != -1)
                effects.push_back(a->m_particle2);
            const auto effectsOk = [&](size_t firstSound) {
                return particlesOk(f, effects) &&
                       g_soundRequests.size() == firstSound + (a->m_explosionSound != -1 ? 1u : 0u) &&
                       (a->m_explosionSound == -1 ||
                        sameSound(g_soundRequests[firstSound], soundAt, 0.0f, SOUND_CHAPTER_EXPLOSION, a->m_explosionSound)) &&
                       SaLogIs(scorchLog(f, true)) && a->m_scorch &&
                       a->m_scorch->m_scale.x == 2.5f && a->m_scorch->m_scale.y == 2.5f && a->m_scorch->m_scale.z == 2.5f &&
                       (a->m_scorch->m_frameFlags & 0x40000008) == 0x40000008 && !(a->m_scorch->m_frameFlags & 0x134) &&
                       a->GetState() == 8;
            };
            const size_t countdownSounds = a->m_countdownSound != -1 ? 1 : 0;
            const bool countdownOk = countdownSounds == 0 ||
                                     sameSound(g_soundRequests[0], soundAt, 0.0f, SOUND_CHAPTER_EXPLOSION, a->m_countdownSound);
            if (k == 2) {
                OB(r.tsStep, countdownOk && effectsOk(countdownSounds) && a->m_clientTime == 0);
            } else {
                OB(r.tsStep, countdownOk && g_soundRequests.size() == countdownSounds && g_rbSplashes.empty() && g_itLog.empty() &&
                                 a->GetState() == 2 && a->m_clientTime == 0);
                reset();
                a->Tick(1000);
                a->Update(250);
                OB(r.tsStep, g_soundRequests.empty() && g_rbSplashes.empty() && a->m_clientTime == 250 && a->m_serverTime == 500);
                a->Update(249);
                OB(r.tsStep, g_soundRequests.empty() && g_rbSplashes.empty());
                a->Update(1);
                OB(r.tsStep, effectsOk(0) && a->m_clientTime == 500);
                reset();
                a->Update(100);
                OB(r.tsStep, g_soundRequests.empty() && g_itLog.empty() && a->m_clientTime == 500);
            }
            // WriteState: one bit; ReadState: the decal (and with "particles on join" the particles).
            for (uint32_t flags = 0; flags < 3; ++flags) {
                a->m_flags = flags;
                a->WriteState(newStream());
                OB(r.tsStep, lastStream()->bits == T_Bits{{flags != 0 ? 1u : 0u, 1}});
            }
            for (uint32_t bit = 0; bit < 2; ++bit) {
                SaRead({{bit, 1}});
                reset();
                a->ReadState(nullptr);
                std::vector<int> joined;
                if (bit && a->m_joinParticles) {
                    if (a->m_particle != -1)
                        joined.push_back(a->m_particle);
                    if (a->m_particle2 != -1)
                        joined.push_back(a->m_particle2);
                }
                OB(r.tsStep, SaReadAll() && particlesOk(f, joined) &&
                                 (bit ? SaLogIs(scorchLog(f, true)) : g_itLog.empty()));
            }
            // RestartMission: the decal out of the scene and let go, the times and the state cleared,
            // reactivated.
            a->m_flags = 9;
            a->m_clientTime = 3;
            a->m_serverTime = 4;
            reset();
            a->RestartMission(true);
            OB(r.ts, !a->m_scorch && a->m_flags == 0 && a->m_clientTime == 0 && a->m_serverTime == 0 &&
                         SaLogIs({"link ? 0 0", "removeframe ?", "release ?"}) && g_obReactivations == std::vector<bool>{true});
            // StopMission: the decal (none now) and the texture let go.
            a->MakeScorch();
            S_Image *texture = reinterpret_cast<S_Image *>(a->m_texture);
            const int refs = texture->refs;
            reset();
            a->StopMission(false);
            OB(r.ts, !a->m_scorch && !a->m_texture && texture->refs == refs - 1 &&
                         SaLogIs({"link ? 0 0", "removeframe ?", "release ?"}) && g_deactivations == std::vector<bool>{false});
            reset();
            a->StopMission(true);
            OB(r.ts, g_itLog.empty() && g_deactivations == std::vector<bool>{true});

            // CopyFrom: the properties, only from a TS explosion (not the base's).
            auto *b = static_cast<C_TSExplosion *>(creator->Create());
            const int copies = g_actorCopies;
            b->CopyFrom(a);
            OB(r.ts, g_actorCopies == copies && b->m_countdownSound == a->m_countdownSound && b->m_delay == a->m_delay &&
                         b->m_explosionSound == a->m_explosionSound && b->m_range == a->m_range && b->m_damage == a->m_damage &&
                         b->m_particle == a->m_particle && b->m_particle2 == a->m_particle2 &&
                         b->m_joinParticles == a->m_joinParticles && b->m_flags == 0);
            C_Radio other;
            b->m_delay = 4444;
            b->CopyFrom(&other);
            OB(r.ts, b->m_delay == 4444);
            creator->Destroy(b);

            // Properties: raw, a version byte first.
            bool written = false;
            S_SaBytes e;
            e.Head(0x777, 6 + 33).Val(static_cast<uint8_t>(1)).Val(a->m_countdownSound).Val(a->m_delay).Val(a->m_explosionSound)
                .Val(a->m_range).Val(a->m_damage).Val(a->m_particle).Val(a->m_particle2).Val(a->m_joinParticles);
            OB(r.tsSave, SaWritten([&](C_chunk &c) { return a->WriteProperties(c); }, &written) == e.b && written);
            S_SaBytes in;
            in.Head(0x777, 6 + 33).Val(static_cast<uint8_t>(2)).Val(41).Val(42).Val(43).Val(4.5f).Val(5.5f).Val(46).Val(47).Val(48);
            S_SaReader rd(in);
            OB(r.tsSave, a->ReadProperties(rd.c) && a->m_countdownSound == 41 && a->m_delay == 42 && a->m_explosionSound == 43 &&
                             a->m_range == 4.5f && a->m_damage == 5.5f && a->m_particle == 46 && a->m_particle2 == 47 &&
                             a->m_joinParticles == 48);
            S_SaBytes shortIn;
            shortIn.Head(0x777, 6 + 9).Val(static_cast<uint8_t>(1)).Val(51).Val(52);
            S_SaReader rs(shortIn);
            OB(r.tsSave, a->ReadProperties(rs.c) && a->m_countdownSound == 51 && a->m_delay == 52 && a->m_explosionSound == 43);

            // The saved game: chunk 0xBE { 0 the state, 1 a client's time, 2 the server's }; LoadGame
            // leaves other ids, makes the decal when the effects were made; another first chunk: false.
            a->m_flags = TS_EXPLOSION_SCORCHED | TS_EXPLOSION_EXPLODED;
            a->m_clientTime = 111;
            a->m_serverTime = 222;
            S_SaBytes sv;
            sv.Head(0x777, 6 + 36).Head(0xBE, 36).Head(0, 10).Val(12u).Head(1, 10).Val(111).Head(2, 10).Val(222);
            bool saved = false;
            OB(r.tsSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv.b && saved);
            for (int scorched = 0; scorched < 2; ++scorched) {
                S_SaBytes ld;
                ld.Head(0x777, 6 + 52).Head(0xBE, 52).Head(9, 16).Head(0, 10).Val(77).Head(2, 10).Val(333)
                    .Head(0, 10).Val(scorched ? 8u : 1u).Head(1, 10).Val(444);
                S_SaReader rl(ld);
                a->m_scorch = nullptr;
                reset();
                OB(r.tsSave, a->LoadGame(rl.c) && a->m_flags == (scorched ? 8u : 1u) && a->m_serverTime == 333 &&
                                 a->m_clientTime == 444 && rl.c.m_depth == 1 &&
                                 (scorched ? SaLogIs(scorchLog(f, false)) : g_itLog.empty()));   // (StopMission let the texture go)
            }
            S_SaBytes otherFirst;
            otherFirst.Head(0x777, 6 + 16 + 6).Head(0xBF, 16).Head(0, 10).Val(8u).Head(0xBE, 6);
            S_SaReader ro(otherFirst);
            a->m_flags = 1;
            OB(r.tsSave, !a->LoadGame(ro.c) && a->m_flags == 1 && ro.c.m_depth == 1);
            a->m_scorch = nullptr;
            creator->Destroy(a);
        }
    }
