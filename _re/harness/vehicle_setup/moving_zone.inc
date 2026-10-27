// (moving_actors_test.inc, inside MovingActorsTest: the multiplayer zone.)

    // --- the multiplayer zone --------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_MP_ZONE];
        MA(r.zone, creator && std::strcmp(creator->GetName(), "MPZone") == 0);
        auto *z = static_cast<C_MPZone *>(creator->Create());
        const auto noNames = [](const C_MPZone *x) {
            for (const auto &n : x->m_flagNames)
                for (char ch : n)
                    if (ch)
                        return false;
            return true;
        };
        MA(r.zone, z->GetType() == ACTOR_MP_ZONE && z->m_number == 0 && z->m_shape == 0 && z->m_team == 2 && z->m_startTeam == 2 &&
                       z->m_textId == -1 && z->m_objective == -1 && z->m_captureTime == 0 && z->m_bothWays &&
                       !z->m_resetPending && noNames(z) && !z->m_flagAnim && z->m_newTeam == 2 && z->m_checkTime == 0 &&
                       z->m_progress == 0 && !z->m_localInside && !z->m_progressShown && z->m_present[0] == 0 &&
                       z->m_present[2] == 0 && z->m_spawnIndex == 0 && !z->m_flags[0] && !z->m_flags[2] && !z->m_spawnFrame &&
                       z->m_captureState == 0 && !z->m_started && z->GetNumber() == 0 && z->GetTeam() == 2 &&
                       z->GetObjective() == -1 && !z->IsRunning());
        I3D_frame *zf = AnFrame(FRAME_DUMMY, "ma.zone", nullptr);
        AnSetBBox(zf, S_vector(-4.0f, -1.0f, -3.0f), S_vector(5.0f, 3.0f, 6.0f));
        const float zt = rnd(-0.5f, 0.5f);
        RbTilt(zf, std::cos(zt), std::sin(zt));
        zf->m_localMatrix(3) = S_vector(rnd(-20, 20), rnd(0, 2), rnd(-20, 20));
        MA(r.zone, !z->SetFrame(nullptr) && !z->GetFrame() && z->SetFrame(zf) && z->GetFrame() == zf);

        // The properties: version 0x8001, the number, shape, team, text, the flags' names, objective,
        // capture time and the both-ways byte; older ones start with the number (no both-ways byte).
        {
            z->m_number = 3;
            z->m_shape = 1;
            z->m_team = 0;
            z->m_textId = 77;
            std::strcpy(z->m_flagNames[0], "ma.flagA");
            std::strcpy(z->m_flagNames[1], "ma.flagB");
            std::strcpy(z->m_flagNames[2], "ma.flagN");
            z->m_objective = -1;
            z->m_captureTime = 20;
            z->m_bothWays = false;
            S_SaBytes w;
            w.Head(0x777, 6 + 20 + 48 + 9).Val(0x8001).Val(3).Val(1).Val(0).Val(77);
            w.Put(z->m_flagNames, 48);
            w.Val(-1).Val(20).Val(false);
            bool written = false;
            MA(r.zoneProps, SaWritten([&](C_chunk &k) { return z->WriteProperties(k); }, &written) == w.b && written);
            for (int k = 0; k < 6; ++k) {
                S_SaBytes in;
                char names[3][16] = {"fA", "fB", "fN"};
                if (k == 0) {
                    in.Head(0x777, 6 + 20 + 48 + 9).Val(0x8001).Val(5).Val(0).Val(1).Val(12);
                    in.Put(names, 48);
                    in.Val(4).Val(30).Val(true);
                } else if (k == 1) {                    // (old: the number first, no both-ways byte)
                    in.Head(0x777, 6 + 16 + 48 + 8).Val(6).Val(1).Val(2).Val(13);
                    in.Put(names, 48);
                    in.Val(-1).Val(40);
                } else if (k == 2) {                    // (short: the rest left)
                    in.Head(0x777, 6 + 12).Val(0x8001).Val(7).Val(0);
                } else if (k == 4) {                    // (0x8000: still an old one)
                    in.Head(0x777, 6 + 16 + 48 + 8).Val(0x8000).Val(1).Val(0).Val(14);
                    in.Put(names, 48);
                    in.Val(-1).Val(50);
                } else if (k == 5) {                    // (old with a byte after it: not read)
                    in.Head(0x777, 6 + 16 + 48 + 9).Val(6).Val(1).Val(2).Val(13);
                    in.Put(names, 48);
                    in.Val(-1).Val(40).Val(false);
                } else {                                // (empty: read as versioned, nothing read)
                    in.Head(0x777, 6);
                }
                S_SaReader rd(in);
                C_MPZone *y = static_cast<C_MPZone *>(creator->Create());
                y->m_bothWays = k != 1;
                y->m_number = 9;
                y->m_team = 1;
                y->m_newTeam = 0;
                y->m_startTeam = 0;
                const bool ok = y->ReadProperties(rd.c);
                bool same = ok && y->m_newTeam == y->m_team && y->m_startTeam == y->m_team;
                if (k == 0)
                    same = same && y->m_number == 5 && y->m_shape == 0 && y->m_team == 1 && y->m_textId == 12 &&
                           std::strcmp(y->m_flagNames[2], "fN") == 0 && y->m_objective == 4 && y->m_captureTime == 30 && y->m_bothWays;
                else if (k == 1)
                    same = same && y->m_number == 6 && y->m_shape == 1 && y->m_team == 2 && y->m_textId == 13 &&
                           std::strcmp(y->m_flagNames[0], "fA") == 0 && y->m_objective == -1 && y->m_captureTime == 40 &&
                           !y->m_bothWays;
                else if (k == 2)
                    same = same && y->m_number == 7 && y->m_shape == 0 && y->m_team == 1 && y->m_textId == -1 && y->m_bothWays;
                else if (k == 4)
                    same = same && y->m_number == 0x8000 && y->m_shape == 1 && y->m_team == 0 && y->m_textId == 14 &&
                           y->m_captureTime == 50 && y->m_bothWays;
                else if (k == 5)
                    same = same && y->m_number == 6 && y->m_captureTime == 40 && y->m_bothWays;
                else
                    same = same && y->m_number == 9 && y->m_team == 1 && y->m_bothWays;
                MA(r.zoneProps, same);
                // CopyFrom: the properties (not the starting team, the state).
                C_MPZone *x = static_cast<C_MPZone *>(creator->Create());
                x->m_startTeam = 1;
                x->m_newTeam = 0;
                const int copies = g_actorCopies;
                x->CopyFrom(y);
                MA(r.zoneProps, g_actorCopies == copies + 1 && x->m_number == y->m_number && x->m_shape == y->m_shape && x->m_team == y->m_team &&
                                    x->m_textId == y->m_textId && x->m_objective == y->m_objective &&
                                    x->m_captureTime == y->m_captureTime && x->m_bothWays == y->m_bothWays &&
                                    SaSame(x->m_flagNames, y->m_flagNames, 48) && x->m_startTeam == 1 && x->m_newTeam == 0);
                creator->Destroy(x);
                creator->Destroy(y);
            }
        }

        // --- the model (from the asm): the state the zone should reach, its log and messages -----------
        struct S_MaZone {
            int team, newTeam, startTeam, objective, number, captureTime, checkTime, progress, captureState, spawnIndex,
                textId;
            bool bothWays, resetPending, localInside, progressShown;
            int present[3];
            S_VertexAnimPlayer anim;
            std::vector<std::string> log;
            std::vector<std::vector<std::pair<uint32_t, int>>> streams;
        };
        const auto modelOf = [](const C_MPZone *x) {
            S_MaZone m;
            m.team = x->m_team;
            m.newTeam = x->m_newTeam;
            m.startTeam = x->m_startTeam;
            m.objective = x->m_objective;
            m.number = x->m_number;
            m.captureTime = x->m_captureTime;
            m.checkTime = x->m_checkTime;
            m.progress = x->m_progress;
            m.captureState = x->m_captureState;
            m.spawnIndex = x->m_spawnIndex;
            m.textId = x->m_textId;
            m.bothWays = x->m_bothWays;
            m.resetPending = x->m_resetPending;
            m.localInside = x->m_localInside;
            m.progressShown = x->m_progressShown;
            for (int i = 0; i < 3; ++i)
                m.present[i] = x->m_present[i];
            if (x->m_flagAnim)
                m.anim = *x->m_flagAnim;
            return m;
        };
        const auto zoneIs = [&](const C_MPZone *x, const S_MaZone &m) {
            bool ok = x->m_team == m.team && x->m_newTeam == m.newTeam && x->m_startTeam == m.startTeam &&
                      x->m_checkTime == m.checkTime && x->m_progress == m.progress && x->m_captureState == m.captureState &&
                      x->m_spawnIndex == m.spawnIndex && x->m_resetPending == m.resetPending &&
                      x->m_localInside == m.localInside && x->m_progressShown == m.progressShown &&
                      x->m_present[0] == m.present[0] && x->m_present[1] == m.present[1] && x->m_present[2] == m.present[2] &&
                      SaLogIs(m.log) && g_streams.size() == m.streams.size();
            for (size_t i = 0; ok && i < m.streams.size(); ++i)
                ok = g_streams[i]->id == x->m_netId && g_streams[i]->bits == m.streams[i] && g_streams[i]->sent && !g_streams[i]->ex;
            if (ok && x->m_flagAnim) {
                const S_VertexAnimPlayer &p = *x->m_flagAnim, &q = m.anim;
                ok = p.frame == q.frame && p.visual == q.visual && p.unknown08 == q.unknown08 && p.frameTime == q.frameTime &&
                     p.unknown1C == q.unknown1C && p.unknown1E == q.unknown1E && p.unknown1F == q.unknown1F &&
                     p.animation == q.animation;
            }
            return ok;
        };
        const auto isServer = [] { return (g_gameRole & GAME_ROLE_SERVER) != 0; };
        const auto isClient = [] { return (g_gameRole & GAME_ROLE_CLIENT) != 0; };
        const auto isLocal = [] { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; };
        const auto line = [](const char *format, auto... args) {
            char text[256];
            std::snprintf(text, sizeof text, format, args...);
            return std::string(text);
        };
        S_VertexAnimation flagAnimation = {};
        flagAnimation.frameCount = 12;
        const auto send = [&](S_MaZone &m, uint32_t kind) {
            std::vector<std::pair<uint32_t, int>> bits = {{kind, 2}};
            if (kind == 2)
                bits.push_back({static_cast<uint32_t>(m.team), 2});
            m.streams.push_back(bits);
        };
        const auto applyTeam = [&](C_MPZone *x, S_MaZone &m) {
            if (m.team == m.newTeam)
                return;
            if (isClient() && m.objective == -1 && (!x->m_spawnFrame || g_mpSettingsValue.gameType != MP_GAME_OBJECTIVES) && !g_coop)
                m.log.push_back(line("icon %d %d", m.number, m.newTeam));
            for (int i = 0; i < 3; ++i) {
                I3D_frame *flag = x->m_flags[i];
                if (!flag)
                    continue;
                m.log.push_back(line("on %s %d", ItName(flag).c_str(), m.newTeam == i));
                m.log.push_back("update " + ItName(flag));
                if (m.newTeam != i)
                    continue;
                m.log.push_back("findanim vlajka");
                I3D_frame *visual = Find(flag, "vlajka", ENUMF_WILDMASK | FRAME_MASK_VISUAL);
                S_VertexAnimation *anim = g_maAnimations.count("vlajka") ? g_maAnimations["vlajka"] : nullptr;
                if (visual && anim) {
                    m.log.push_back(line("animreset %s %d", ItName(m.anim.visual).c_str(), m.anim.frame));
                    m.log.push_back(line("removeplayer %s 0", ItName(m.anim.visual).c_str()));
                    m.log.push_back("animinit " + ItName(visual));
                    m.anim.visual = visual;
                    m.anim.animation = anim;
                    m.log.push_back(line("addplayer %s %d", ItName(visual).c_str(), m.anim.frameTime));
                    m.anim.unknown1C = false;
                    m.anim.frame = anim->frameCount - 1;
                    m.anim.frameTime = 210;
                    m.anim.unknown08 = 0;
                    m.anim.unknown1F = true;
                    m.anim.unknown1E = false;
                }
            }
            m.team = m.newTeam;
        };
        const auto sphereOf = [](const I3D_bbox &box, S_vector &center, float &radius) {
            center = S_vector((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.min.z + box.max.z) * 0.5f);
            const S_vector d(box.min.x - center.x, box.min.y - center.y, box.min.z - center.z);
            radius = RbLength((d.z * d.z + d.y * d.y) + d.x * d.x);
        };
        const auto contains = [&](C_MPZone *x, const S_vector &p, const I3D_bbox &box, const S_vector &c, float radius) {
            if (x->m_shape == 0)
                return p.x > box.min.x && p.x < box.max.x && p.y > box.min.y && p.y < box.max.y && p.z > box.min.z && p.z < box.max.z;
            if (x->m_shape != 1)
                return false;
            const S_vector d(p.x - c.x, p.y - c.y, p.z - c.z);
            return RbLength((d.z * d.z + d.y * d.y) + d.x * d.x) < radius;
        };
        const auto countPresence = [&](C_MPZone *x, S_MaZone &m) {
            const I3D_bbox box = static_cast<I3D_dummy *>(x->GetFrame())->GetBBox();
            S_matrix inv;
            inv.Inverse(x->GetFrame()->GetMatrix());
            S_vector c;
            float radius;
            sphereOf(box, c, radius);
            m.present[0] = m.present[1] = m.present[2] = 0;
            m.log.push_back("lock");
            for (int slot = 0; slot < g_maPlayerCount; ++slot) {
                C_Human *h = g_obPlayers[slot].human;
                if (!g_obPlayers[slot].IsConnected() || !h || m.present[h->GetTeam(false)] != 0 || !h->IsAlive() || !h->IsInPlay())
                    continue;
                if (contains(x, h->GetFrame()->GetWorldPos() * inv, box, c, radius))
                    ++m.present[h->GetTeam(false)];
            }
            C_Human *local = isClient() && g_obPlayers[g_localPlayerSlot].IsConnected() ? g_obPlayers[g_localPlayerSlot].human : nullptr;
            if (local) {
                m.localInside = contains(x, local->GetPosition() * inv, box, c, radius);
                if (!m.bothWays && g_maLocalTeam == m.startTeam)
                    m.localInside = false;
            } else {
                m.localInside = false;
            }
            m.log.push_back("unlock");
        };
        const auto award = [&](C_MPZone *x, S_MaZone &m, int points) {
            const I3D_bbox box = static_cast<I3D_dummy *>(x->GetFrame())->GetBBox();
            S_matrix inv;
            inv.Inverse(x->GetFrame()->GetMatrix());
            S_vector c;
            float radius;
            sphereOf(box, c, radius);
            m.log.push_back("lock");
            for (int slot = 0; slot < g_maPlayerCount; ++slot) {
                C_Human *h = g_obPlayers[slot].human;
                if (!g_obPlayers[slot].IsConnected() || !h || !h->IsAlive() || !h->IsInPlay())
                    continue;
                if (contains(x, h->GetFrame()->GetWorldPos() * inv, box, c, radius))
                    m.log.push_back(line("score %d %d", slot, points));
            }
            m.log.push_back("unlock");
        };
        alignas(16) static uint8_t missionMemory[sizeof(C_MpMissionDef)];
        std::memset(missionMemory, 0, sizeof missionMemory);
        auto *mission = reinterpret_cast<C_MpMissionDef *>(missionMemory);
        const auto canTake = [&](C_MPZone *x, const S_MaZone &m, int team) {
            if (mission->m_unknown60 != 1)
                return true;
            int held = 0;
            for (size_t i = 32; i < g_waActors.size(); ++i) {
                if (!g_waActors[i] || g_waActors[i]->GetType() != ACTOR_MP_ZONE)
                    continue;
                const auto *o = static_cast<const C_MPZone *>(g_waActors[i]);
                const int oTeam = o == x ? m.team : o->m_team;
                const int oNumber = o->m_number;
                if (oTeam == team)
                    ++held;
                if (team == 0) {
                    if (oNumber == m.number - 1 && oTeam == team)
                        return true;
                } else if (team == 1) {
                    if (oNumber == m.number + 1 && oTeam == team)
                        return true;
                }
            }
            return held == 0 && m.team == 2;
        };
        const auto checkCapture = [&](C_MPZone *x, S_MaZone &m) {
            if (m.team != m.newTeam || g_coop)
                return;
            countPresence(x, m);
            if (m.captureState < 0)
                return;
            if (m.captureState <= 1) {
                if (GetGame()->m_missionTime < static_cast<uint32_t>((g_mpSettingsValue.warmup + 5) * 1000))
                    return;
                if (m.present[m.team] != 0 && m.bothWays)
                    return;
                if (!m.bothWays && m.present[m.startTeam == 0 ? 1 : 0] == 0)
                    return;
                if (m.objective != -1 && m.team != m.startTeam)
                    return;
                if (g_mpSettingsValue.gameType == MP_GAME_OBJECTIVES && m.objective == -1)
                    return;
                const int axis = m.present[0], allies = m.present[1];
                if (allies == axis) {
                    if (m.bothWays)
                        return;
                    if (axis == 0 && allies == 0)
                        return;
                }
                if (!canTake(x, m, axis == 0 ? 1 : 0))
                    return;
                m.progress = 0;
                m.captureState = 2;
                send(m, 0);
                return;
            }
            if (m.captureState != 2)
                return;
            if (m.present[m.team] != 0 && m.bothWays) {
                m.captureState = 0;
                send(m, 1);
                return;
            }
            const int axis = m.present[0], allies = m.present[1];
            const bool stop = axis == 0 ? allies == 0 : (allies != 0 && m.bothWays);
            if (!stop && canTake(x, m, axis == 0 ? 1 : 0))
                return;
            m.captureState = 1;
            send(m, 1);
        };
        const auto tick = [&](C_MPZone *x, S_MaZone &m, int dt) {
            if (isLocal())
                return;
            m.checkTime += dt;
            if (m.checkTime > 1000) {
                checkCapture(x, m);
                m.checkTime = 0;
            }
            if (m.objective == -1 && x->m_flags[0])
                m.log.push_back(line("teamscore %d %.9g", m.team, static_cast<float>(static_cast<uint32_t>(dt)) * 1.25e-05f));
            if (m.captureState != 2)
                return;
            m.progress += dt;
            if (m.objective == -1 && m.progress > m.captureTime * 500 && m.team != 2) {
                if (!canTake(x, m, m.present[0] == 0 ? 1 : 0) || (m.present[0] == 0 && m.present[1] == 0)) {
                    m.progress = 0;
                    m.captureState = 1;
                    send(m, 1);
                    return;
                }
                m.progress = 0;
                m.newTeam = 2;
                applyTeam(x, m);
                send(m, 2);
                send(m, 0);
                award(x, m, 2);
            }
            const int scale = m.bothWays && m.objective == -1 ? 500 : 1000;
            if (m.progress <= m.captureTime * scale)
                return;
            if (m.team != 2 && m.bothWays && m.objective == -1)
                return;
            countPresence(x, m);
            const int axis = m.present[0], allies = m.present[1];
            if (!canTake(x, m, axis == 0 ? 1 : 0) || (axis == 0 && allies == 0)) {
                m.progress = 0;
                m.captureState = 1;
                send(m, 1);
                return;
            }
            m.newTeam = m.objective == -1 ? (axis == 0 ? 1 : 0) : (m.team != 1 ? 1 : 0);
            m.captureState = 0;
            m.progress = 0;
            if (m.objective == -1) {
                applyTeam(x, m);
                award(x, m, 5);
                send(m, 2);
                return;
            }
            if (m.newTeam != m.startTeam) {
                m.team = m.newTeam;
                m.log.push_back(line("objective %d 1", m.objective));
            }
            send(m, 2);
        };
        const auto update = [&](C_MPZone *x, S_MaZone &m, int ft, int &zoneSet) {
            if (isLocal())
                return;
            if (m.resetPending) {
                m.resetPending = false;
                m.team = -1;
                applyTeam(x, m);
            }
            if (m.captureState != 2) {
                if (m.progressShown && m.captureTime != 0 && m.team != 2)
                    m.progressShown = false;
                return;
            }
            if (!isServer()) {
                m.progress += ft;
                m.checkTime += ft;
                if (m.checkTime <= 1000)
                    return;
                m.checkTime = 0;
                m.log.push_back("lock");
                C_Human *local = g_obPlayers[g_localPlayerSlot].human;
                m.log.push_back("unlock");
                if (!local || local->GetHealth() < 1) {
                    m.localInside = false;
                } else {
                    const I3D_bbox box = static_cast<I3D_dummy *>(x->GetFrame())->GetBBox();
                    S_matrix inv;
                    inv.Inverse(x->GetFrame()->GetMatrix());
                    S_vector c;
                    float radius;
                    sphereOf(box, c, radius);
                    m.localInside = contains(x, local->GetPosition() * inv, box, c, radius);
                    if (!m.bothWays && g_maLocalTeam == m.startTeam)
                        m.localInside = false;
                }
            }
            if (m.localInside && !m.progressShown && m.captureTime != 0) {
                if (g_localHuman)
                    zoneSet = x->m_netId;
                m.progressShown = true;
            }
        };

        // --- the scene: three flags (user frames; the axis and allied ones with a "vlajka" visual), the
        // spawn frame "<name>spawn" with four points; the players; the other zones.
        const auto flagFrame = [&](const char *name, bool visual) {
            I3D_frame *fl = AnFrame(FRAME_USER, name, nullptr);
            if (visual)
                g_itNames[AnFrame(FRAME_VISUAL, "vlajka", fl)] = std::string(name) + ".vlajka";
            g_maSceneFrames[name] = fl;
            return fl;
        };
        g_maSceneFrames.clear();
        I3D_frame *flagA = flagFrame("ma.flagA", true), *flagB = flagFrame("ma.flagB", true), *flagN = flagFrame("ma.flagN", false);
        I3D_frame *spawn = AnFrame(FRAME_DUMMY, "ma.zonespawn", nullptr);
        g_maSceneFrames["ma.zonespawn"] = spawn;
        std::vector<I3D_frame *> spawnPoints;
        for (int k = 0; k < 4; ++k) {
            I3D_frame *p = AnFrame(FRAME_DUMMY, "sp" + std::to_string(k), spawn);
            RbTilt(p, std::cos(rnd(0, 6)), std::sin(rnd(0, 6)), k % 2 == 1);
            p->m_localMatrix(3) = S_vector(rnd(-10, 10), rnd(0, 2), rnd(-10, 10));
            p->m_localMatrix.m[3][3] = 1.25f;          // (the places' and directions' w)
            p->m_localMatrix.m[2][3] = 0.5f;
            spawnPoints.push_back(p);
        }
        g_maAnimations["vlajka"] = &flagAnimation;
        std::strcpy(z->m_flagNames[0], "ma.flagA");
        std::strcpy(z->m_flagNames[1], "ma.flagB");
        std::strcpy(z->m_flagNames[2], "ma.flagN");
        alignas(16) static uint8_t interfaceMemory[64];
        C_Interface *const oldInterface = GetGame()->m_interface;
        C_MissionDef *const oldMission = GetGame()->m_mission;
        GetGame()->m_interface = reinterpret_cast<C_Interface *>(interfaceMemory);
        GetGame()->m_mission = mission;
        const bool oldReliable = g_reliableActorStreams;
        g_reliableActorStreams = true;
        const int oldGameType = g_mpSettingsValue.gameType;
        z->m_netId = 230;

        // StartMission: once (the flags found and held, the animation player made, the spawn frame);
        // a client's side: the team shown again.
        {
            const int refsA = flagA->m_refCount, refsS = spawn->m_refCount;
            z->m_team = 1;
            z->m_newTeam = 1;
            z->m_progressShown = true;
            g_gameRole = GAME_ROLE_SERVER;
            reset();
            z->StartMission(true);
            MA(r.zoneStart, z->m_started && !z->m_progressShown && z->m_flags[0] == flagA && z->m_flags[1] == flagB && z->m_flags[2] == flagN &&
                                flagA->m_refCount == refsA + 1 && z->m_spawnFrame == spawn && spawn->m_refCount == refsS + 1 &&
                                z->m_flagAnim && z->m_flagAnim->frameTime == 250 && !z->m_flagAnim->visual && z->m_team == 1 &&
                                g_activations == std::vector<bool>{true} &&
                                SaLogIs({"find ma.flagA 80", "find ma.flagB 80", "find ma.flagN 80", "find ma.zonespawn 20"}));
            S_VertexAnimPlayer *anim0 = z->m_flagAnim;
            g_gameRole = GAME_ROLE_CLIENT;
            S_MaZone m = modelOf(z);
            m.team = -1;
            m.resetPending = false;
            applyTeam(z, m);
            z->m_resetPending = true;
            reset();
            z->StartMission(false);
            MA(r.zoneStart, zoneIs(z, m) && z->m_flagAnim == anim0 && z->m_team == 1 && g_activations == std::vector<bool>{false} &&
                                flagA->m_refCount == refsA + 1);
        }

        // The state machine against the model: random players (connected, alive, in play, in the
        // zone or not, their teams), zones before and after it, the mission time, the game type, the
        // roles; Tick, Update, the messages, the script's team, a restart.
        std::vector<C_Human *> humans;
        for (int i = 0; i < 8; ++i) {
            humans.push_back(new C_Human());
            humans[i]->m_frame = AnFrame(FRAME_MODEL, "ma.player" + std::to_string(i), nullptr);
            g_obPlayers[i].human = humans[i];
        }
        std::vector<C_MPZone *> others;
        for (int i = 0; i < 3; ++i) {
            others.push_back(static_cast<C_MPZone *>(creator->Create()));
            others[i]->m_number = 2 + 2 * i;   // 2, 4, 6 (this one is 3)
        }
        const std::vector<C_Actor *> actors0 = g_waActors;
        g_waActors.assign(32, nullptr);
        g_waActors.push_back(others[0]);
        g_waActors.push_back(nullptr);
        g_waActors.push_back(z);
        g_waActors.push_back(humans[0]);
        g_waActors.push_back(others[1]);
        g_waActors.push_back(others[2]);
        const I3D_bbox box = static_cast<I3D_dummy *>(zf)->GetBBox();
        // (Inside the box; far out; out of the box only along x but in its sphere; only above it; only
        // past its far z; just inside its sphere: kinds 0..5.)
        const auto placeAt = [&](int kind) {
            const S_vector c((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.min.z + box.max.z) * 0.5f);
            S_vector local;
            if (kind == 0)
                local = S_vector(rnd(box.min.x + 0.2f, box.max.x - 0.2f), rnd(box.min.y + 0.2f, box.max.y - 0.2f),
                                 rnd(box.min.z + 0.2f, box.max.z - 0.2f));
            else if (kind == 1)
                local = S_vector(rnd(box.max.x + 2.5f, box.max.x + 6.0f), rnd(-1, 3), rnd(-3, 6));
            else if (kind == 2)
                local = S_vector(rnd(box.max.x + 0.2f, box.max.x + 1.5f), c.y, c.z);
            else if (kind == 3)
                local = S_vector(c.x, rnd(box.max.y + 0.3f, box.max.y + 0.9f), c.z);
            else if (kind == 4)
                local = S_vector(c.x, c.y, rnd(box.max.z + 0.2f, box.max.z + 1.0f));
            else                                        // (just inside the sphere along x)
                local = S_vector(c.x + 6.6f, c.y, c.z);
            return local * zf->GetMatrix();
        };
        const auto placeIn = [&](bool inside) { return placeAt(inside ? 0 : 1); };
        int zoneSetSeen = 0;
        for (int step = 0; step < 600; ++step) {
            // The world.
            const int roleKind = static_cast<int>(rnd(0, 3.99f));
            g_gameRole = static_cast<uint8_t>(roleKind == 0 ? GAME_ROLE_SERVER : roleKind == 1 ? GAME_ROLE_CLIENT
                                                                    : roleKind == 2 ? GAME_ROLE_SERVER | GAME_ROLE_CLIENT
                                                                                    : GAME_ROLE_LOCAL);
            g_maPlayerCount = static_cast<int>(rnd(0, 8.99f));
            g_localPlayerSlot = static_cast<int>(rnd(0, 7.99f));
            g_maLocalTeam = static_cast<int>(rnd(0, 2.99f));
            g_coop = rnd(0, 1) < 0.1f;
            g_mpSettingsValue.gameType = rnd(0, 1) < 0.2f ? MP_GAME_OBJECTIVES : MP_GAME_OCCUPATION;
            g_mpSettingsValue.warmup = static_cast<int>(rnd(0, 3));
            GetGame()->m_missionTime = static_cast<uint32_t>(rnd(0, 12000));
            mission->m_unknown60 = rnd(0, 1) < 0.5f ? 1 : 0;
            g_localHuman = rnd(0, 1) < 0.8f ? humans[g_localPlayerSlot] : nullptr;
            for (int i = 0; i < 8; ++i) {
                C_Human *h = humans[i];
                std::strcpy(g_obPlayers[i].name, rnd(0, 1) < 0.85f ? "p" : "");
                h->team = static_cast<int>(rnd(0, 2.99f));
                h->health = rnd(0, 1) < 0.15f ? 0 : 500;
                h->inPlay = rnd(0, 1) < 0.9f;
                h->m_frame->m_localMatrix(3) = placeAt(static_cast<int>(rnd(0, 5.99f)));
                const S_vector p = placeAt(static_cast<int>(rnd(0, 5.99f)));
                h->m_position = {p.x, p.y, p.z, 1.0f};
                h->mpZone = -1;
            }
            for (C_MPZone *o : others)
                o->m_team = static_cast<int>(rnd(0, 2.99f));
            if (step % 50 == 0) {
                z->m_shape = (step / 50) % 3;           // (2: no zone)
                z->m_bothWays = (step / 50) % 3 != 1;
                z->m_objective = (step / 50) % 4 == 3 ? 5 : -1;
                z->m_captureTime = (step / 50) % 5 == 4 ? 0 : 2 + (step / 50) % 3;
                z->m_startTeam = (step / 50) % 3;
            }
            if (rnd(0, 1) < 0.1f)
                z->m_flags[0] = rnd(0, 1) < 0.5f ? flagA : nullptr;
            // (The state as the calls may find it: shown or not, a team pending, the spawn points used,
            // no spawn frame, no flag animation, the player's fields changed.)
            z->m_progressShown = rnd(0, 1) < 0.3f;
            if (rnd(0, 1) < 0.15f)
                z->m_newTeam = static_cast<int>(rnd(0, 2.99f));
            z->m_spawnIndex = static_cast<int>(rnd(0, 3.99f));
            const bool noSpawn = rnd(0, 1) < 0.2f;
            if (noSpawn)
                z->m_spawnFrame = nullptr;
            const bool noAnimation = rnd(0, 1) < 0.15f;
            if (noAnimation)
                g_maAnimations.erase("vlajka");
            if (rnd(0, 1) < 0.2f) {
                z->m_flagAnim->unknown08 = 5;
                z->m_flagAnim->unknown1F = false;
            }
            // The call.
            S_MaZone m = modelOf(z);
            const int what = static_cast<int>(rnd(0, 9.99f));
            int zoneSet = -1;
            std::vector<std::pair<uint32_t, int>> in;
            reset();
            if (what <= 3) {
                const int dt = rnd(0, 1) < 0.5f ? 250 * static_cast<int>(rnd(0, 4.99f)) : static_cast<int>(rnd(0, 2500));
                tick(z, m, dt);
                z->Tick(dt);
            } else if (what <= 6) {
                const int ft = rnd(0, 1) < 0.5f ? 250 * static_cast<int>(rnd(0, 4.99f)) : static_cast<int>(rnd(0, 1500));
                update(z, m, ft, zoneSet);
                z->Update(ft);
            } else if (what == 7) {
                // OnMessage: 0 a capture starts (a pure client checks at the next Update), 1 it stops,
                // 2 a new team (shown unless it is an objective's or a spawn area of the objectives game).
                const uint32_t kind = static_cast<uint32_t>(rnd(0, 3.99f));
                const uint32_t team = static_cast<uint32_t>(rnd(0, 2.99f));
                in = {{kind, 2}};
                if (kind == 2)
                    in.push_back({team, 2});
                if (kind == 0) {
                    m.captureState = 2;
                    m.progress = 0;
                    if (!isServer())
                        m.checkTime = 10000;
                } else if (kind == 1) {
                    m.captureState = 0;
                    if (m.progressShown && m.captureTime != 0)
                        m.progressShown = false;
                } else if (kind == 2) {
                    m.captureState = 0;
                    if (m.team != static_cast<int>(team)) {
                        m.newTeam = static_cast<int>(team);
                        if (m.objective != -1 || (z->m_spawnFrame && g_mpSettingsValue.gameType == MP_GAME_OBJECTIVES))
                            m.team = static_cast<int>(team);
                        else
                            applyTeam(z, m);
                    }
                }
                SaRead(in);
                z->OnMessage(nullptr);
            } else if (what == 8) {
                // The script: the team shown and sent.
                const int team = static_cast<int>(rnd(0, 2.99f));
                m.newTeam = team;
                applyTeam(z, m);
                send(m, 2);
                z->EnableForSide(team);
            } else {
                // Restart: the starting team shown again, the capture stopped, the spawn points reset.
                m.newTeam = m.startTeam;
                m.progressShown = false;
                m.resetPending = true;
                m.captureState = 1;
                m.progress = 0;
                m.spawnIndex = 0;
                m.team = -1;
                applyTeam(z, m);
                z->RestartMission(rnd(0, 1) < 0.5f);
            }
            const bool zoneOk = zoneSet == -1 ? (!g_localHuman || g_localHuman->mpZone == -1)
                                              : g_localHuman && g_localHuman->mpZone == zoneSet;
            zoneSetSeen += zoneSet != -1;
            if (noSpawn)
                z->m_spawnFrame = spawn;
            if (noAnimation)
                g_maAnimations["vlajka"] = &flagAnimation;
            MA(r.zoneCapture, zoneIs(z, m) && zoneOk && (what != 7 || SaReadAll()));
            if (std::getenv("CAR_TRACE") && !zoneIs(z, m))
                std::printf("  zone step %d what %d role %d\n", step, what, g_gameRole);
        }
        {
            // (Always: a pure client's local player inside a running capture: shown, its zone set.)
            g_gameRole = GAME_ROLE_CLIENT;
            z->m_captureState = 2;
            z->m_captureTime = 5;
            z->m_shape = 0;
            z->m_progressShown = false;
            z->m_checkTime = 0;
            z->m_bothWays = true;
            z->m_resetPending = false;
            z->m_team = z->m_newTeam;
            g_localPlayerSlot = 1;
            humans[1]->health = 500;
            const S_vector p = placeIn(true);
            humans[1]->m_position = {p.x, p.y, p.z, 1.0f};
            g_localHuman = humans[1];
            humans[1]->mpZone = -1;
            S_MaZone m = modelOf(z);
            int zoneSet = -1;
            update(z, m, 1100, zoneSet);
            reset();
            z->Update(1100);
            MA(r.zoneCapture, zoneIs(z, m) && zoneSet == z->m_netId && humans[1]->mpZone == z->m_netId && z->m_progressShown);
        }
        (void)zoneSetSeen;
        g_waActors = actors0;

        // GetProgress: the time left (a neutral zone's half less), the total, the text; none unless shown.
        {
            int time = 1, total = 2, text = 3;
            z->m_progressShown = false;
            MA(r.zoneState, !z->GetProgress(&time, &total, &text) && time == 1 && total == 2 && text == 3);
            z->m_progressShown = true;
            z->m_captureTime = 12;
            z->m_progress = 2500;
            z->m_textId = 41;
            z->m_team = 2;
            MA(r.zoneState, z->GetProgress(&time, &total, &text) && total == 12000 && time == 12000 - 2500 - 6000 && text == 41);
            z->m_team = 0;
            z->m_localInside = false;
            MA(r.zoneState, z->GetProgress(&time, &total, &text) && total == 12000 && time == 12000 - 2500 && z->IsRunning());
        }
        // The state's team: written in 2 bits; read: shown.
        {
            z->m_team = 1;
            C_BitStream *msg = reinterpret_cast<C_BitStream *>(new S_FakeStream{0, false, {}, false});
            z->WriteState(msg);
            MA(r.zoneState, reinterpret_cast<S_FakeStream *>(msg)->bits == std::vector<std::pair<uint32_t, int>>{{1, 2}});
            delete reinterpret_cast<S_FakeStream *>(msg);
            g_gameRole = GAME_ROLE_CLIENT;
            S_MaZone m = modelOf(z);
            m.newTeam = 2;
            m.team = -1;
            applyTeam(z, m);
            SaRead({{2, 2}});
            reset();
            z->ReadState(nullptr);
            MA(r.zoneState, zoneIs(z, m) && SaReadAll());
            // (The same team: shown again all the same.)
            S_MaZone m2 = modelOf(z);
            m2.newTeam = 2;
            m2.team = -1;
            applyTeam(z, m2);
            SaRead({{2, 2}});
            reset();
            z->ReadState(nullptr);
            MA(r.zoneState, zoneIs(z, m2) && SaReadAll() && !g_itLog.empty());
        }
        // The spawn points (the real PickSpawnPosition): each point's world place and its z axis; going
        // round with `cycle`, else false past the last; a held zone turns neutral when its points are used
        // up (told); up to 32 tries for one not blocked (unless the objectives game).
        {
            const auto pointOf = [&](int index, S_vector4 &pos, S_vector4 &dir) {
                const S_matrix w = spawnPoints[static_cast<size_t>(index)]->GetMatrix();
                pos = {w.m[3][0], w.m[3][1], w.m[3][2], w.m[3][3]};
                S_vector zz(w.m[2][0], w.m[2][1], w.m[2][2]);
                const float sq = (zz.y * zz.y + zz.x * zz.x) + zz.z * zz.z;
                if (!(std::fabs(sq - 1.0f) < 1e-08f)) {
                    if (!(sq < 1e-08f)) {
                        const float k = 1.0f / std::sqrt(sq);
                        zz = S_vector(zz.x * k, zz.y * k, zz.z * k);
                    } else if (zz.x != 0.0f) {
                        zz.x = zz.x < 0.0f ? -1.0f : 1.0f;
                    } else if (zz.z != 0.0f) {
                        zz.z = zz.z < 0.0f ? -1.0f : 1.0f;
                    } else {
                        zz.y = zz.y < 0.0f ? -1.0f : 1.0f;
                    }
                }
                dir = {zz.x, zz.y, zz.z, w.m[2][3]};
            };
            g_gameRole = GAME_ROLE_SERVER;
            for (int c = 0; c < 7; ++c) {
                const bool cycle = c % 2 == 0;
                z->m_spawnIndex = c < 2 ? 0 : 2;
                z->m_team = c == 4 || c == 5 ? 2 : 1;   // (5: neutral already at the end: not told)
                g_obBlocked = c == 4 ? 40 : c;
                g_mpSettingsValue.gameType = c == 3 ? MP_GAME_OBJECTIVES : MP_GAME_OCCUPATION;
                // The oracle.
                S_MaZone m = modelOf(z);
                int blocked = g_obBlocked;
                int tries = 0;
                bool result = true;
                S_vector4 pos{}, dir{};
                do {
                    if (!cycle && m.spawnIndex == 4) {
                        result = false;
                        break;
                    }
                    pointOf(m.spawnIndex, pos, dir);
                    ++m.spawnIndex;
                    if (cycle)
                        m.spawnIndex = static_cast<int>(static_cast<uint32_t>(m.spawnIndex) % 4u);
                    if (m.team != 2 && m.spawnIndex == 4) {
                        m.team = 2;
                        send(m, 2);
                    }
                    ++tries;
                    m.log.push_back("blocked " + ItV(S_vector(pos.x, pos.y, pos.z)));
                } while (blocked-- > 0 && tries < 32 && g_mpSettingsValue.gameType != MP_GAME_OBJECTIVES);
                if (result)
                    result = tries < 32;
                S_vector4 p4{}, d4{};
                reset();
                const bool got = MaPickSpawnPosition(z, &p4, &d4, cycle);
                MA(r.zoneSpawn, got == result && zoneIs(z, m) && (tries == 0 || (SaSame(&p4, &pos, sizeof pos) && SaSame(&d4, &dir, sizeof dir))));
            }
            g_obBlocked = 0;
            z->ResetSpawnIndex();
            MA(r.zoneSpawn, z->m_spawnIndex == 0);
        }

        // StopMission: the flags let go, the player removed and deleted, the spawn frame let go; once.
        {
            z->m_flags[0] = flagA;
            const int refsA = flagA->m_refCount, refsS = spawn->m_refCount;
            const std::string shown = ItName(z->m_flagAnim->visual);
            reset();
            z->StopMission(true);
            MA(r.zoneStart, !z->m_started && !z->m_flags[0] && !z->m_flags[1] && !z->m_flags[2] && !z->m_spawnFrame &&
                                !z->m_flagAnim && flagA->m_refCount == refsA - 1 && spawn->m_refCount == refsS - 1 &&
                                SaLogIs({"release ma.flagA", "release ma.flagB", "release ma.flagN", "removeplayer " + shown + " 0",
                                         "release ma.zonespawn"}));
            reset();
            z->StopMission(false);
            MA(r.zoneStart, g_itLog.empty() && g_deactivations.empty());
            // (Without its flags or spawn frame.)
            g_maSceneFrames.clear();
            g_gameRole = GAME_ROLE_SERVER;
            reset();
            z->StartMission(true);
            MA(r.zoneStart, z->m_started && !z->m_flags[0] && !z->m_spawnFrame && z->m_flagAnim);
            reset();
            z->StopMission(true);
            MA(r.zoneStart, SaLogIs({"removeplayer 0 0"}) && !z->m_flagAnim);
        }
        // SaveGame / LoadGame: nothing (false).
        {
            bool saved = true;
            MA(r.zoneState, SaWritten([&](C_chunk &k) { return z->SaveGame(k); }, &saved) == std::vector<uint8_t>{0x77, 0x07, 6, 0, 0, 0} &&
                                !saved);
            S_SaBytes in;
            in.Head(0x777, 10).Val(5);
            S_SaReader rd(in);
            MA(r.zoneState, !z->LoadGame(rd.c));
        }

        g_mpSettingsValue.gameType = oldGameType;
        g_reliableActorStreams = oldReliable;
        GetGame()->m_interface = oldInterface;
        GetGame()->m_mission = oldMission;
        g_coop = false;
        g_maAnimations.clear();
        g_maSceneFrames.clear();
        for (int i = 0; i < 8; ++i) {
            g_obPlayers[i].human = nullptr;
            g_obPlayers[i].name[0] = 0;
            delete humans[i];
        }
        for (C_MPZone *o : others)
            creator->Destroy(o);
        creator->Destroy(z);
        g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
        GetGame()->m_missionTime = 0;
        g_localHuman = nullptr;
    }
