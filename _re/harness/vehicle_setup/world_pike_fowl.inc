// (world_actors_test.inc, inside WorldActorsTest: the pike and the fowl generator.)

    // --- the pike ------------------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_PIKE];
        WA(r.pike, creator && std::strcmp(creator->GetName(), "Pike") == 0);
        // The volume as the original's UpdateVolume (0x005b97b0, from the asm).
        struct S_WaPikeVolume {
            S_CollisionVolume v;
            S_matrix m, inv;
        };
        const auto volumeOracle = [&](C_Pike *a) {
            S_WaPikeVolume e;
            e.v = a->m_volume;
            I3D_frame *fr = a->m_volume.frame;
            I3D_bbox box;
            S_vector center(0.0f, 0.0f, 0.0f);
            float radius = 0.0f;
            if (fr->GetType() == FRAME_DUMMY) {
                box = static_cast<I3D_dummy *>(fr)->GetBBox();
                center = S_vector((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.min.z + box.max.z) * 0.5f);
                radius = (box.max.y - box.min.y) * 0.5f;
            } else {
                I3D_bbox tmp;
                box = *F_GetHierarchyBBox(nullptr, tmp, fr);
                if (box.min.x <= box.max.x) {
                    const S_vector half = (box.max - box.min) * 0.5f;
                    center = box.min + half;
                    radius = std::sqrt((half.z * half.z + half.y * half.y) + half.x * half.x);
                }
            }
            const S_vector s = fr->m_scale;
            box.max = S_vector(box.max.x * s.x, box.max.y * s.y, box.max.z * s.z);
            box.min = S_vector(box.min.x * s.x, box.min.y * s.y, box.min.z * s.z);
            e.m = fr->GetMatrix();
            for (int c = 0; c < 3; ++c) {
                e.m.m[0][c] = (1.0f / s.x) * e.m.m[0][c];
                e.m.m[1][c] = (1.0f / s.y) * e.m.m[1][c];
                e.m.m[2][c] = (1.0f / s.z) * e.m.m[2][c];
            }
            e.inv.Inverse(e.m);
            const S_vector ws = fr->GetWorldScale();
            const float yz = ws.y > ws.z ? ws.y : ws.z;
            const float most = ws.x > yz ? ws.x : yz;
            e.v.bbox = box;
            e.v.radius = most * radius;
            e.v.center = center * fr->GetMatrix();
            e.v.centerW = e.m.m[0][3];
            e.v.matrix = &a->m_matrix;
            e.v.invMatrix = &a->m_invMatrix;
            return e;
        };
        const auto volumeOk = [&](C_Pike *a, const S_WaPikeVolume &e) {
            return SaSame(&a->m_volume, &e.v, sizeof e.v) && SaSame(&a->m_matrix, &e.m, sizeof e.m) &&
                   SaSame(&a->m_invMatrix, &e.inv, sizeof e.inv);
        };
        // The turned rotation (q(axis, angle) after the activation's rotation; from the asm).
        const auto turnRot = [&](C_Pike *a) {
            S_quat q;
            q.Make(S_vector(a->m_axis.x, a->m_axis.y, a->m_axis.z), a->m_angle);
            const S_quat &p = a->m_initialRot;
            const S_vector c(q.v.y * p.v.z - q.v.z * p.v.y, q.v.z * p.v.x - p.v.z * q.v.x, q.v.x * p.v.y - q.v.y * p.v.x);
            S_quat rot;
            rot.v.x = (p.v.x * q.s + q.v.x * p.s) + c.x;
            rot.v.y = (p.v.y * q.s + q.v.y * p.s) + c.y;
            rot.v.z = (p.v.z * q.s + q.v.z * p.s) + c.z;
            rot.s = q.s * p.s - ((q.v.z * p.v.z + q.v.y * p.v.y) + q.v.x * p.v.x);
            rot.Normalize();
            return rot;
        };
        // The rays: the box's 12 edges, then two diagonals, 0.01 m inside it.
        const auto raysOracle = [&](I3D_frame *fr) {
            I3D_bbox tmp;
            const I3D_bbox b = *F_GetHierarchyBBox(nullptr, tmp, fr);
            const S_vector hi(b.max.x - 0.01f, b.max.y - 0.01f, b.max.z - 0.01f), lo(b.min.x + 0.01f, b.min.y + 0.01f, b.min.z + 0.01f);
            S_vector p[8];
            for (int i = 0; i < 8; ++i) {
                p[i] = S_vector(i < 4 ? hi.x : lo.x, i % 4 < 2 ? hi.y : lo.y, i % 2 ? hi.z : lo.z);
                p[i] *= fr->GetMatrix();
            }
            static const int edges[14][2] = {{2, 3}, {3, 7}, {7, 6}, {6, 2}, {0, 2}, {1, 3}, {5, 7},
                                             {4, 6}, {0, 1}, {1, 5}, {5, 4}, {4, 0}, {1, 6}, {2, 5}};
            std::vector<S_WaRay> rays;
            for (const auto &e : edges)
                rays.push_back({p[e[0]], S_vector(p[e[1]].x - p[e[0]].x, p[e[1]].y - p[e[0]].y, p[e[1]].z - p[e[0]].z), 2, 0,
                                true, true});
            return rays;
        };
        const auto raysOk = [&](const std::vector<S_WaRay> &e, size_t n) {
            if (g_waRays.size() != n)
                return false;
            for (size_t i = 0; i < n; ++i) {
                const S_WaRay &a = g_waRays[i], &b = e[i];
                if (!SaSame(&a.from, &b.from, 12) || !SaSame(&a.dir, &b.dir, 12) || a.types != 2 || a.flags != 0 || !a.hitPos ||
                    !a.hitNormal)
                    return false;
            }
            return true;
        };
        const auto sameRot = [](const S_quat &a, const S_quat &b) { return SaSame(&a, &b, sizeof a); };
        for (int k = 0; k < 3; ++k) {
            I3D_frame *f = placed(k == 2 ? FRAME_DUMMY : FRAME_MODEL, "wa.pike", k == 0);
            if (k == 2)
                AnSetBBox(f, S_vector(-0.2f, -0.1f, -2.5f), S_vector(0.3f, 0.6f, 0.4f));
            else
                boxFrame(f, "arm", k);
            auto *a = static_cast<C_Pike *>(creator->Create());
            WA(r.pike, a->GetType() == ACTOR_PIKE && a->GetState() == PIKE_DOWN && a->m_axis.x == 0.0f && a->m_axis.y == 1.0f &&
                           a->m_axis.z == 0.0f && a->m_maxAngle == 1.5707964f && a->m_speed == 1.5707964f &&
                           a->m_blockedTime == 0 && a->m_angle == 0.0f && a->m_loopSound == -1 && !a->m_started &&
                           a->m_lowSound == -1 && a->m_highSound == -1 && a->m_moveSound == -1 &&
                           a->m_sound.chapter == 0 && a->m_sound.volume == 1.0f);
            const int inits = g_actorInits;
            WA(r.pike, !a->SetFrame(nullptr) && g_actorInits == inits && a->SetFrame(f) && a->GetFrame() == f);
            const float axisLen = 1.0f / std::sqrt(3.0f);
            a->m_axis = {k == 1 ? 1.0f : axisLen, k == 1 ? 0.0f : axisLen, k == 1 ? 0.0f : -axisLen, 0.0f};
            a->m_initialRot = S_quat(rnd(0.5f, 1), rnd(-0.5f, 0.5f), rnd(-0.5f, 0.5f), rnd(-0.5f, 0.5f));
            a->m_initialRot.Normalize();
            a->m_maxAngle = 1.2f + 0.2f * k;
            a->m_speed = 0.8f + 0.3f * k;
            a->m_lowSound = k == 1 ? -1 : 31;
            a->m_highSound = 32;
            a->m_moveSound = k == 2 ? -1 : 33;
            a->m_netId = 70 + k;
            f->SetData(&scriptData);

            // StartMission: the rotation kept, a server raises one that was up, the volume (added
            // and moved), the sound's place.
            g_gameRole = k == 0 ? static_cast<uint8_t>(GAME_ROLE_SERVER) : k == 1 ? static_cast<uint8_t>(GAME_ROLE_CLIENT) : static_cast<uint8_t>(3);
            a->m_state = PIKE_UP;
            a->m_angle = 0.4f;
            a->m_loopSound = 9;
            const bool raises = k != 1;
            // (Before: Update leaves the volume alone without its frame; StartMission clears it.)
            const S_quat placedRot = f->GetRot();
            a->m_state = PIKE_RAISING;
            reset();
            a->Update(10);
            WA(r.pike, g_volumeLog.empty());
            a->m_state = PIKE_UP;
            a->m_angle = 0.4f;
            a->m_blockedTime = 0;
            f->SetRot(placedRot);
            a->m_volume.capsuleRadius = 3.0f;
            a->m_volume.unknown54[0] = 7;
            S_quat startRot = f->GetRot();
            reset();
            a->StartMission(k != 1);
            const S_WaPikeVolume ev = volumeOracle(a);
            const S_matrix &w = f->GetMatrix();
            WA(r.pike, a->m_started && a->m_angle == 0.0f && sameRot(a->m_startRot, startRot) &&
                           g_activations == std::vector<bool>{k != 1} && a->GetState() == (raises ? PIKE_RAISING : PIKE_UP) &&
                           g_uses == (raises ? std::vector<int>{PIKE_RAISING} : std::vector<int>{}) &&
                           (raises ? g_streams.size() == 1 && g_streams[0]->bits == T_Bits{{0, 2}} && g_streams[0]->id == a->m_netId
                                   : g_streams.empty()) &&
                           a->m_volume.frame == f && a->m_volume.actor == a && a->m_volume.flags == COLLISION_SHAPE_BOX &&
                           a->m_volume.capsuleRadius == 0.0f && a->m_volume.unknown54[0] == 0 &&
                           a->m_volume.type == 0x800 && volumeOk(a, ev) && g_volumeLog.size() == 2 &&
                           g_volumeLog[0].op == 'A' && g_volumeLog[1].op == 'M' && g_volumeLog[0].volume == &a->m_volume &&
                           a->m_loopSound == -1 && a->m_sound.chapter == SOUND_CHAPTER_OBJECT_MANIPULATION &&
                           RbSame(a->m_sound.pos, w(3)) && a->m_sound.posW == w.m[3][3] && a->m_sound.flags == 0 &&
                           !a->m_sound.attachTo && !a->m_sound.source);
            reset();
            a->StartMission(false);
            WA(r.pike, g_activations == std::vector<bool>{false} && g_volumeLog.empty() && g_uses.empty());

            // Turn: the frame turned; rays along the box; a hit (at ray `at`) turns it back.
            for (int at = -1; at < 15; at += (at < 2 ? 1 : 5)) {
                a->m_angle = rnd(0.0f, 1.0f);
                const S_quat before = f->GetRot();
                const S_quat expect = turnRot(a);
                const std::vector<S_WaRay> rays = raysOracle(f);
                g_waBlockAt = at;
                const int ignores = g_ignores, clears = g_ignoreClears;
                reset();
                const bool blocked = a->Turn(5);
                const bool hit = at >= 0 && at < 14;
                S_quat back = before;
                back.Normalize();
                WA(r.pikeTurn, blocked == hit && raysOk(rays, hit ? static_cast<size_t>(at + 1) : 14u) &&
                                   sameRot(f->m_rot, hit ? back : expect) && g_ignores == ignores + 1 &&
                                   g_ignoreClears == clears + 1 &&
                                   SaLogIs(hit ? std::vector<std::string>{"update " + ItName(f), "update " + ItName(f)}
                                               : std::vector<std::string>{"update " + ItName(f)}));
            }
            g_waBlockAt = -1;

            // Tick (a server without a client side) and Update (clients): the angle moves by
            // speed * time towards the end, stopping there; a blocked turn waits 0.2 s; Update's ends
            // play their sound and stop the loop.
            g_soundSlot = 5;
            for (int tick = 0; tick < 2; ++tick) {
                const uint8_t roles[] = {GAME_ROLE_SERVER, GAME_ROLE_CLIENT, 3};
                for (uint8_t role : roles) {
                    for (int s = 0; s < 4; ++s) {
                        // (c 4: the wait ends below 0; 5: it ends at 1; 6: an end reached exactly: 1 rad/s
                        // for 0.25 s to 1.25 or to 0.)
                        for (int c = 0; c < 7; ++c) {
                            const float maxAngle = a->m_maxAngle, speed = a->m_speed;
                            if (c == 6) {
                                a->m_maxAngle = 1.25f;
                                a->m_speed = 1.0f;
                            }
                            g_gameRole = role;
                            a->m_state = s;
                            a->m_angle = c == 0 ? 0.05f : c == 1 ? a->m_maxAngle - 0.05f : c == 6 ? (s == PIKE_LOWERING ? 0.25f : 1.0f) : 0.6f;
                            a->m_blockedTime = c == 2 ? 150 : c == 4 ? 50 : c == 5 ? 101 : 0;
                            a->m_loopSound = c == 3 ? -1 : 6;
                            const int dt = c == 6 ? 250 : c == 2 || c == 4 || c == 5 ? 100 : 180;
                            g_waBlockAt = c == 3 ? 0 : -1;
                            // The oracle (Tick 0x005b9340, Update 0x005b9470).
                            int state = s, blockedTime = a->m_blockedTime, loop = a->m_loopSound;
                            float angle = a->m_angle;
                            std::vector<std::string> log;
                            std::vector<int> played, stopped;
                            bool moves = false, waits = false;
                            if (tick) {
                                if (!(s == PIKE_DOWN || s == PIKE_UP || (role & GAME_ROLE_CLIENT))) {
                                    if (blockedTime != 0) {
                                        blockedTime -= dt;
                                        waits = blockedTime > 0;
                                        if (!waits)
                                            blockedTime = 0;
                                    }
                                    moves = !waits;
                                }
                            } else {
                                if (blockedTime != 0) {
                                    blockedTime -= dt;
                                    waits = blockedTime > 0;
                                    if (!waits)
                                        blockedTime = 0;
                                }
                                moves = !waits && (s == PIKE_RAISING || s == PIKE_LOWERING);
                            }
                            const float angle0 = angle;
                            if (moves) {
                                const float step = static_cast<float>(static_cast<uint32_t>(dt)) * 0.001f * a->m_speed;
                                bool arrived = false;
                                int sound = -1;
                                if (s == PIKE_RAISING) {
                                    angle = step + angle;
                                    if (a->m_maxAngle < angle) {
                                        state = PIKE_UP;
                                        angle = a->m_maxAngle;
                                        arrived = true;
                                        sound = a->m_highSound;
                                    }
                                } else if (s == PIKE_LOWERING) {
                                    angle = angle - step;
                                    if (angle < 0.0f) {
                                        state = PIKE_DOWN;
                                        angle = 0.0f;
                                        arrived = true;
                                        sound = a->m_lowSound;
                                    }
                                }
                                if (!tick && arrived) {
                                    if (sound != -1)
                                        played.push_back(sound);
                                    if (loop != -1) {
                                        log.push_back("stopsound " + std::to_string(loop));
                                        stopped.push_back(loop);
                                        loop = -1;
                                    }
                                }
                                log.push_back("update " + ItName(f));
                                if (c == 3) {
                                    log.push_back("update " + ItName(f));
                                    blockedTime = 200;
                                    angle = angle0;
                                }
                                if (!tick)
                                    log.push_back("bound " + ItName(f));
                            }
                            const int sounds = static_cast<int>(played.size());
                            reset();
                            if (tick)
                                a->Tick(dt);
                            else
                                a->Update(dt);
                            const S_WaPikeVolume e = volumeOracle(a);
                            bool soundsOk = static_cast<int>(g_soundRequests.size()) == sounds;
                            for (int i = 0; soundsOk && i < sounds; ++i)
                                soundsOk = soundIs(g_soundRequests[i], a->m_sound.pos, a->m_sound.posW, SOUND_SILENT_TO_AI,
                                                   SOUND_CHAPTER_OBJECT_MANIPULATION, played[i]);
                            WA(r.pikeTurn, a->GetState() == state && a->m_angle == angle && a->m_blockedTime == blockedTime &&
                                               a->m_loopSound == loop && SaLogIs(log) && g_waStops == stopped && soundsOk &&
                                               (moves ? g_volumeLog.size() == 1 && g_volumeLog[0].op == 'M' && volumeOk(a, e)
                                                      : g_volumeLog.empty()) &&
                                               (moves ? g_waRays.size() == (c == 3 ? 1u : 14u) : g_waRays.empty()));
                            a->m_maxAngle = maxAngle;
                            a->m_speed = speed;
                        }
                    }
                }
            }
            g_waBlockAt = -1;
            g_soundSlot = 0;

            // Messages: 0 raising, 1 lowering: the start sound (none: no loop either), the loop
            // unless one plays.
            for (uint32_t kind = 0; kind < 3; ++kind) {
                for (int playing = 0; playing < 2; ++playing) {
                    a->m_state = PIKE_UP;
                    a->m_loopSound = playing ? 4 : -1;
                    g_soundSlot = 8;
                    SaRead({{kind, 2}});
                    reset();
                    a->OnMessage(nullptr);
                    const int start = kind == 0 ? a->m_lowSound : a->m_highSound;
                    bool ok = SaReadAll();
                    if (kind == 2) {
                        ok = ok && a->GetState() == PIKE_UP && g_soundRequests.empty() && a->m_loopSound == (playing ? 4 : -1);
                    } else if (start == -1) {
                        ok = ok && g_soundRequests.empty() && a->m_loopSound == (playing ? 4 : -1);
                    } else {
                        const bool loops = a->m_moveSound != -1 && !playing;
                        ok = ok && g_soundRequests.size() == (loops ? 2u : 1u) &&
                             soundIs(g_soundRequests[0], a->m_sound.pos, a->m_sound.posW, SOUND_SILENT_TO_AI,
                                     SOUND_CHAPTER_OBJECT_MANIPULATION, start) &&
                             (!loops || soundIs(g_soundRequests[1], a->m_sound.pos, a->m_sound.posW, SOUND_SILENT_TO_AI | SOUND_LOOP,
                                                SOUND_CHAPTER_OBJECT_MANIPULATION, a->m_moveSound)) &&
                             a->m_loopSound == (loops ? 8 : playing ? 4 : -1) &&
                             a->m_sound.group == a->m_moveSound && a->m_sound.flags == (SOUND_SILENT_TO_AI | SOUND_LOOP);
                    }
                    if (kind < 2)
                        ok = ok && a->GetState() == (kind == 0 ? PIKE_RAISING : PIKE_LOWERING);
                    WA(r.pikeNet, ok);
                }
            }
            g_soundSlot = 0;
            // WriteState: raising or up 0, else 1; ReadState: 0 raises unless up, 1 lowers unless down.
            for (int s = 0; s < 4; ++s) {
                a->m_state = s;
                a->WriteState(newStream());
                WA(r.pikeNet, lastStream()->bits == T_Bits{{s == PIKE_UP || s == PIKE_RAISING ? 0u : 1u, 2}});
                for (uint32_t kind = 0; kind < 3; ++kind) {
                    a->m_state = s;
                    SaRead({{kind, 2}});
                    a->ReadState(nullptr);
                    const int e = kind == 0 ? (s == PIKE_UP ? s : PIKE_RAISING) : kind == 1 ? (s == PIKE_DOWN ? s : PIKE_LOWERING) : s;
                    WA(r.pikeNet, SaReadAll() && a->GetState() == e);
                }
            }
            // SetState: the frame's script told every state; 2 and 3 tell the clients.
            for (int s = 0; s < 5; ++s) {
                a->m_state = 9;
                reset();
                a->SetState(s);
                const bool moves = s == PIKE_RAISING || s == PIKE_LOWERING;
                WA(r.pikeNet, g_uses == std::vector<int>{s} && a->GetState() == (moves ? s : 9) &&
                                  (moves ? g_streams.size() == 1 && g_streams[0]->bits == T_Bits{{s == PIKE_RAISING ? 0u : 1u, 2}} &&
                                               g_streams[0]->sent
                                         : g_streams.empty()));
            }
            f->SetData(nullptr);
            reset();
            a->SetState(PIKE_LOWERING);
            WA(r.pikeNet, g_uses.empty() && a->GetState() == PIKE_LOWERING);

            // Properties: the state, the largest angle, the speed, the axis, the three sounds (raw).
            {
                S_SaBytes in;
                in.Head(0x777, 6 + 36).Val(PIKE_UP).Val(0.75f).Val(2.5f).Val(0.6f).Val(0.0f).Val(0.8f).Val(11).Val(12).Val(13);
                auto *b = static_cast<C_Pike *>(creator->Create());
                b->m_axis.w = 5.0f;
                S_SaReader rd(in);
                WA(r.pikeSave, b->ReadProperties(rd.c) && b->GetState() == PIKE_UP && b->m_maxAngle == 0.75f &&
                                   b->m_speed == 2.5f && b->m_axis.x == 0.6f && b->m_axis.y == 0.0f && b->m_axis.z == 0.8f &&
                                   b->m_axis.w == 5.0f && b->m_lowSound == 11 && b->m_highSound == 12 && b->m_moveSound == 13);
                // A short one: the axis not read (0 here), its 4 bytes left taken by the first sound.
                S_SaBytes shortIn;
                shortIn.Head(0x777, 6 + 16).Val(PIKE_DOWN).Val(1.0f).Val(2.0f).Val(0.5f);
                S_SaReader rs(shortIn);
                WA(r.pikeSave, b->ReadProperties(rs.c) && b->GetState() == PIKE_DOWN && b->m_axis.x == 0.0f &&
                                   b->m_axis.y == 0.0f && b->m_axis.z == 0.0f && b->m_lowSound == 0x3F000000 && b->m_highSound == 12);
                // CopyFrom: the base's and the properties (the axis's w too).
                b->m_axis = {0.1f, 0.2f, 0.3f, 0.4f};
                b->m_state = PIKE_LOWERING;
                b->m_angle = 0.9f;
                b->m_blockedTime = 7;
                const int copies = g_actorCopies;
                auto *c = static_cast<C_Pike *>(creator->Create());
                c->CopyFrom(b);
                WA(r.pikeSave, g_actorCopies == copies + 1 && c->GetState() == PIKE_LOWERING && c->m_maxAngle == 1.0f &&
                                   c->m_speed == 2.0f && SaSame(&c->m_axis, &b->m_axis, 16) && c->m_lowSound == 0x3F000000 &&
                                   c->m_highSound == 12 && c->m_moveSound == 13 && c->m_angle == 0.0f && c->m_blockedTime == 0);
                creator->Destroy(b);
                creator->Destroy(c);
            }
            // The saved game: chunk 1234 with the state, the wait, the angle; LoadGame then Update(0).
            {
                a->m_state = PIKE_UP;
                a->m_blockedTime = 120;
                a->m_angle = 0.33f;
                S_SaBytes sv;
                sv.Head(0x777, 24).Head(1234, 18).Val(PIKE_UP).Val(120).Val(0.33f);
                bool saved = false;
                WA(r.pikeSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv.b && saved);
                // (Any id; Update(0) of one waiting only counts the wait.)
                S_SaBytes ld;
                ld.Head(0x777, 24).Head(77, 18).Val(PIKE_LOWERING).Val(90).Val(0.25f);
                S_SaReader rd(ld);
                reset();
                WA(r.pikeSave, a->LoadGame(rd.c) && a->GetState() == PIKE_LOWERING && a->m_blockedTime == 90 &&
                                   a->m_angle == 0.25f && g_itLog.empty() && rd.c.m_depth == 1);
                // Not waiting: Update(0) turns it (and the volume).
                S_SaBytes mv;
                mv.Head(0x777, 24).Head(77, 18).Val(PIKE_RAISING).Val(0).Val(0.25f);
                S_SaReader rm(mv);
                reset();
                WA(r.pikeSave, a->LoadGame(rm.c) && a->GetState() == PIKE_RAISING && !g_itLog.empty() &&
                                   g_itLog.front() == "update " + ItName(f) && g_volumeLog.size() == 1);
                // A short chunk: its id and size as the state.
                S_SaBytes sh;
                sh.Head(0x777, 6 + 8).Head(1234, 8).Val(static_cast<uint16_t>(3));
                S_SaReader rs(sh);
                a->m_blockedTime = 0;
                WA(r.pikeSave, a->LoadGame(rs.c) && a->GetState() == static_cast<int>(1234u | (8u << 16)) && a->m_angle == 0.25f);
            }
            // StopMission: the rotation back, the volume out, the loop stopped; then only the
            // deactivation.
            a->m_loopSound = 0;                         // (slot 0 is a loop too)
            a->m_state = PIKE_DOWN;
            reset();
            a->StopMission(true);
            S_quat back = a->m_startRot;
            back.Normalize();
            WA(r.pike, !a->m_started && sameRot(f->m_rot, back) && SaLogIs({"update " + ItName(f), "stopsound 0"}) &&
                           g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R' && g_volumeLog[0].volume == &a->m_volume &&
                           a->m_loopSound == -1 && g_deactivations == std::vector<bool>{true});
            reset();
            a->StopMission(false);
            WA(r.pike, g_itLog.empty() && g_volumeLog.empty() && g_deactivations == std::vector<bool>{false});
            // RestartMission: stopped and started on the server side.
            g_gameRole = GAME_ROLE_CLIENT;
            reset();
            a->RestartMission(false);
            WA(r.pike, g_deactivations == std::vector<bool>{true} && g_activations == std::vector<bool>{true} && a->m_started);
            reset();
            a->m_angle = 0.25f;
            a->Hit(HIT_RUN_OVER, 1.0f, S_vector(0, 0, 0), S_vector(0, 1, 0), nullptr, 0);
            a->Unknown20(nullptr);
            WA(r.pike, g_itLog.empty() && g_streams.empty() && a->m_angle == 0.25f);
            creator->Destroy(a);
        }
        // A box flat in x (min.x == max.x): still its centre and radius.
        {
            I3D_frame *f = placed(FRAME_MODEL, "wa.flatpike", false);
            RbBoxFrame(f, "flat", 0.2f, 0.2f, -0.5f, 0.5f, -1.0f, 1.0f, S_vector(0.0f, 0.0f, 0.0f));
            auto *a = static_cast<C_Pike *>(creator->Create());
            a->SetFrame(f);
            reset();
            a->StartMission(true);
            WA(r.pike, volumeOk(a, volumeOracle(a)) && a->m_volume.radius > 0.0f);
            a->StopMission(true);
            creator->Destroy(a);
        }
    }

    // --- the fowl generator -------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_FOWL_GENERATOR];
        WA(r.fowl, creator && std::strcmp(creator->GetName(), "FowlGenerator") == 0);
        g_rayTestHook = &WaGroundRay;
        // The random numbers as RandomFloat gives them (a copy of its sequence).
        uint32_t mirror = 0;
        const auto nextRandom = [&](float max) {
            mirror = mirror * 1103515245u + 12345u;
            return static_cast<float>((mirror >> 8) & 0xFFFF) / 65536.0f * max;
        };
        const auto fmt = [](const char *format, double v) {
            char text[64];
            std::snprintf(text, sizeof text, format, v);
            return std::string(text);
        };
        // CreateBirds as the original's (0x005ba8b0): its log, the birds' places and names.
        struct S_WaBirds {
            std::vector<std::string> log;
            std::vector<S_vector> places;
            std::vector<std::string> names;
            std::string model;
        };
        const auto birdsOracle = [&](C_FowlGenerator *a) {
            S_WaBirds e;
            mirror = g_waRandom;
            if (a->m_mode == 0)
                return e;
            const float side = std::sqrt(static_cast<float>(a->m_count));
            const float radius = a->m_radius;
            const float spacing = (radius + radius) / side;
            const S_vector center = a->m_frame->GetWorldPos();
            e.log.push_back("createframe 9");
            if (g_obNoFrame)
                return e;
            e.model = a->m_model.c_str();
            if (e.model.rfind('.') != std::string::npos)
                e.model.erase(e.model.rfind('.'));
            if (g_cacheFailName == e.model) {
                e.log.push_back("release new");
                return e;
            }
            const int rows = static_cast<int>(side);
            for (int row = rows; row-- > 0;) {
                const float rowF = static_cast<float>(row);
                const float x = (rowF * spacing + center.x) - radius;
                const float jitter = spacing * 0.4f;
                for (int col = rows; col-- > 0;) {
                    S_vector pos(x, center.y, (static_cast<float>(col) * spacing + center.z) - radius);
                    e.log.push_back(fmt("random %.9g", jitter + jitter));
                    pos.x = (nextRandom(jitter + jitter) - jitter) + pos.x;
                    e.log.push_back(fmt("random %.9g", jitter + jitter));
                    pos.z = (nextRandom(jitter + jitter) - jitter) + pos.z;
                    e.log.push_back("createframe 9");
                    e.log.push_back("duplicate new new");
                    e.log.push_back("addframe new");
                    e.log.push_back("link new sector 0");
                    if (a->m_mode == 2) {
                        e.log.push_back("ray " + ItV(pos) + " " + ItV(S_vector(0, -100, 0)) + " 0 0");
                        if (!(pos.y < g_waGround))
                            pos.y = g_waGround;
                    }
                    e.log.push_back("on new 1");
                    e.log.push_back("random 1");
                    const float dx = nextRandom(1.0f);
                    e.log.push_back("random 1");
                    const float dz = nextRandom(1.0f);
                    e.log.push_back("worlddir new " + ItV(S_vector(dx, 0.0f, dz)) + " 0");
                    const double index = static_cast<float>(rowF * side);
                    uint64_t bits;
                    std::memcpy(&bits, &index, 8);
                    char name[64];
                    std::snprintf(name, sizeof name, "_bird_%d_%d", a->m_netId, static_cast<int>(static_cast<uint32_t>(bits)));
                    e.log.push_back(std::string("name ") + name);
                    e.log.push_back(std::string("update ") + name);
                    e.log.push_back(std::string("bird ") + name + " 1");
                    e.log.push_back(std::string("release ") + name);
                    e.places.push_back(pos);
                    e.names.push_back(name);
                }
            }
            e.log.push_back("release new");
            return e;
        };
        const auto birdsMade = [&](C_FowlGenerator *a, const S_WaBirds &e, size_t before, size_t framesBefore) {
            bool ok = SaLogIs(e.log) && a->m_birds.size() == before + e.places.size() &&
                      g_cacheLoads == (!e.model.empty() ? std::vector<std::string>{e.model} : std::vector<std::string>{});
            for (size_t i = 0; ok && i < e.places.size(); ++i) {
                const C_Bird *bird = a->m_birds[before + i];
                I3D_frame *model = g_obMadeFrames[framesBefore + 1 + i];
                ok = bird->GetModel() == model && bird->OwnsModel() && ItName(model) == e.names[i] &&
                     SaSame(&model->m_localMatrix.m[3][0], &e.places[i], 12) && model->m_refCount == 0;
            }
            return ok;
        };
        // A bird model of the mission at `pos`.
        const auto missionBird = [&](const std::string &name, const S_vector &pos) {
            I3D_frame *m = AnFrame(FRAME_MODEL, name, nullptr);
            m->m_localMatrix.m[3][0] = pos.x;
            m->m_localMatrix.m[3][1] = pos.y;
            m->m_localMatrix.m[3][2] = pos.z;
            return new C_Bird(m, false);
        };
        const S_vector4 camera0 = {};
        I3D_frame *camera = AnFrame(FRAME_DUMMY, "wa.camera", nullptr);
        I3D_camera **activeCamera = reinterpret_cast<I3D_camera **>(reinterpret_cast<uint8_t *>(g_carScene) + 0x15C);
        I3D_camera *const oldCamera = *activeCamera;
        *activeCamera = reinterpret_cast<I3D_camera *>(camera);
        (void)camera0;
        for (int k = 0; k < 4; ++k) {
            I3D_frame *f = placed(FRAME_DUMMY, "wa.fowl", k % 2 == 1);
            auto *a = static_cast<C_FowlGenerator *>(creator->Create());
            WA(r.fowl, a->GetType() == ACTOR_FOWL_GENERATOR && a->m_mode == 0 && a->m_listener == -1 && a->m_radius == 2.0f &&
                           a->m_triggerDistance == 5.0f && a->m_count == 0 && std::strcmp(a->m_model.c_str(), "ptacekletacek") == 0 &&
                           !a->m_flown && a->m_time == 0 && a->m_birds.empty());
            a->SetFrame(f);
            a->m_netId = 90 + k;
            const S_vector here = f->GetWorldPos();
            // Mode 0: the mission's birds within the radius taken over, the last filling each gap
            // (the length the inline way: exactly 1 near 1, 0 below 1e-8).
            if (k == 0) {
                g_birds.clear();
                a->m_radius = 3.0f;
                const S_vector offsets[] = {S_vector(1, 0, 0), S_vector(0, 3.5f, 0), S_vector(0, 0, -2.9f), S_vector(2.5f, 2, 0),
                                            S_vector(0, 0, 0), S_vector(0.5f, 0, 3.1f), S_vector(9, 0, 0), S_vector(0, 0, 9)};
                for (size_t i = 0; i < 8; ++i)
                    g_birds.push_back(missionBird("wa.mbird" + std::to_string(i), here + offsets[i]));
                std::vector<C_Bird *> mission = g_birds;
                g_gameRole = static_cast<uint8_t>(3);           // (a client side too: the flight is heard)
                std::srand(1234);
                const int check = std::rand() % 1000;
                std::srand(1234);
                a->m_time = 55;
                a->m_flown = true;
                reset();
                a->StartMission(true);
                // Taken from the back, the last filling each gap: 4, 2, 0 (three: no flock);
                // the others are farther.
                std::vector<C_Bird *> taken, left = mission;
                for (size_t i = left.size(); i-- > 0;) {
                    const S_vector d = here - left[i]->GetModel()->GetWorldPos();
                    const float sq = (d.z * d.z + d.y * d.y) + d.x * d.x;
                    int32_t bitsSq;
                    std::memcpy(&bitsSq, &sq, 4);
                    const float len = bitsSq < 0x322bcc77 ? 0.0f : std::fabs(sq - 1.0f) < 1e-8f ? 1.0f : std::sqrt(sq);
                    if (len <= a->m_radius) {
                        taken.push_back(left[i]);
                        left[i] = left.back();
                        left.pop_back();
                    }
                }
                WA(r.fowlBirds, a->m_birds == taken && g_birds == left && taken.size() >= 3 && a->m_checkTime == check &&
                                    a->m_time == 0 &&
                                    !a->m_flown && a->m_listener == 77 && SaLogIs({"addlistener " + ItName(f)}) &&
                                    g_waListenerFrame == f);
                // No bird near: nothing to set off (flown); a client, or a server's client side, does
                // not listen.
                auto *b = static_cast<C_FowlGenerator *>(creator->Create());
                I3D_frame *bf = placed(FRAME_DUMMY, "wa.fowl", false);
                b->SetFrame(bf);
                b->m_radius = 0.001f;
                g_gameRole = GAME_ROLE_CLIENT;
                reset();
                b->StartMission(false);
                WA(r.fowlBirds, b->m_birds.empty() && b->m_flown && b->m_listener == -1 && g_itLog.empty());
                g_gameRole = GAME_ROLE_SERVER;
                b->m_flown = false;
                reset();
                b->StartMission(false);
                WA(r.fowlBirds, !b->m_flown && g_itLog.empty());
                // A bird 1e-5 m off: a square below 1e-8 is no distance (taken at radius 0).
                b->m_radius = 0.0f;
                const std::vector<C_Bird *> others = g_birds;
                g_birds = {missionBird("wa.nearbird", bf->GetWorldPos() + S_vector(1e-5f, 0.0f, 0.0f))};
                C_Bird *const closeBird = g_birds[0];
                b->StartMission(true);
                WA(r.fowlBirds, b->m_birds == std::vector<C_Bird *>{closeBird} && g_birds.empty());
                b->m_birds.clear();
                delete closeBird;
                g_birds = others;
                creator->Destroy(b);
                g_gameRole = static_cast<uint8_t>(3);
            } else {
                // Modes 1 and 2: made (2 at StartMission, on the ground below; 1 when they fly up).
                a->m_mode = k == 3 ? 1 : 2;
                const int counts[] = {0, 7, 13, 4};   // (13: names whose low words show the index)
                a->m_count = counts[k];
                a->m_radius = 1.5f + k;
                const char *models[] = {"", "birds.4ds", "a.b.c", "ptacekletacek"};
                a->m_model = models[k];
                g_waGround = here.y - (k == 2 ? 0.5f : -9.0f);
                g_gameRole = k == 2 ? static_cast<uint8_t>(3) : static_cast<uint8_t>(GAME_ROLE_CLIENT);   // (a client side: StartMission(false) goes on)
                std::srand(99);
                const int check = std::rand() % 1000;
                std::srand(99);
                const S_WaBirds e = a->m_mode == 2 ? birdsOracle(a) : S_WaBirds{};
                const size_t frames = g_obMadeFrames.size();
                a->m_time = 55;
                a->m_flown = true;
                reset();
                a->StartMission(k == 2);
                S_WaBirds full = e;
                if (k == 2)
                    full.log.push_back("addlistener " + ItName(f));
                WA(r.fowlBirds, birdsMade(a, full, 0, frames) && a->m_checkTime == check && !a->m_flown && a->m_time == 0 &&
                                    a->m_listener == (k == 2 ? 77 : -1));
                // CreateBirds' failures: no frame, no model.
                if (k == 2) {
                    auto *b = static_cast<C_FowlGenerator *>(creator->Create());
                    b->SetFrame(f);
                    b->m_mode = 2;
                    b->m_count = 4;
                    b->m_model = "missing.4ds";
                    g_obNoFrame = true;
                    reset();
                    b->CreateBirds();
                    WA(r.fowlBirds, SaLogIs({"createframe 9"}) && b->m_birds.empty() && g_cacheLoads.empty());
                    g_obNoFrame = false;
                    // A bird's frame not made: that row ends (the next tries again), the model let go.
                    b->m_model = "missing.4ds";
                    g_obFramesLeft = 2;
                    reset();
                    b->CreateBirds();
                    WA(r.fowlBirds, b->m_birds.size() == 1 && !g_itLog.empty() && g_itLog.back() == "release new" &&
                                        std::count(g_itLog.begin(), g_itLog.end(), std::string("createframe 9")) == 4);
                    g_obFramesLeft = -1;
                    for (C_Bird *bird : b->m_birds)
                        delete bird;
                    b->m_birds.clear();
                    g_cacheFailName = "missing";
                    reset();
                    b->CreateBirds();
                    WA(r.fowlBirds, SaLogIs({"createframe 9", "release new"}) && b->m_birds.empty() &&
                                        g_cacheLoads == std::vector<std::string>{"missing"});
                    g_cacheFailName.clear();
                    b->m_mode = 0;
                    reset();
                    b->CreateBirds();
                    WA(r.fowlBirds, g_itLog.empty());
                    creator->Destroy(b);
                }
            }

            // Start: once; mode 1 makes the birds first; they all fly away (from the back); a client
            // hears them within a second (group 1 for more than 3); the clients told when asked.
            for (int time = 0; time < 3; ++time) {
                const size_t before = a->m_birds.size();
                a->m_flown = false;
                a->m_time = 5;
                if (time > 0 && a->m_mode == 1)
                    a->m_mode = 3;                      // (made once)
                const S_WaBirds e = a->m_mode == 1 ? birdsOracle(a) : S_WaBirds{};
                const size_t frames = g_obMadeFrames.size();
                reset();
                const int startTime = time == 0 ? 0 : time == 1 ? 999 : 1000;
                a->Start(time != 1, startTime);
                S_WaBirds full = e;
                for (size_t i = before + e.places.size(); i-- > 0;)
                    full.log.push_back("flyaway " + ItName(a->m_birds[i]->GetModel()));
                const bool heard = (g_gameRole & GAME_ROLE_CLIENT) && startTime < 1000;
                const S_matrix &w = f->GetMatrix();
                WA(r.fowlTrigger, birdsMade(a, full, before, frames) && a->m_flown && a->m_time == 0 &&
                                      (heard ? g_soundRequests.size() == 1 &&
                                                   soundIs(g_soundRequests[0], w(3), w.m[3][3], SOUND_SILENT_TO_AI,
                                                           SOUND_CHAPTER_ACTION, a->m_birds.size() > 3 ? 1 : 0)
                                             : g_soundRequests.empty()) &&
                                      (time != 1 ? g_streams.size() == 1 && g_streams[0]->bits.empty() && g_streams[0]->sent &&
                                                       g_streams[0]->id == a->m_netId
                                                 : g_streams.empty()));
                reset();
                a->Start(true, 0);
                WA(r.fowlTrigger, g_itLog.empty() && g_streams.empty() && g_soundRequests.empty());
            }
            if (a->m_mode == 3)
                a->m_mode = 1;
            // OnMessage: started without telling the clients.
            a->m_flown = false;
            reset();
            a->OnMessage(nullptr);
            WA(r.fowlTrigger, a->m_flown && g_streams.empty());
            // A server without a client side: not heard.
            const uint8_t role = g_gameRole;
            g_gameRole = GAME_ROLE_SERVER;
            a->m_flown = false;
            reset();
            a->Start(false, 0);
            WA(r.fowlTrigger, a->m_flown && g_soundRequests.empty());
            g_gameRole = role;

            // Update: each bird flies on (from the back), the ones gone deleted, the last filling the
            // gap; the time since they flew up (10 s more once none is left).
            {
                const std::vector<C_Bird *> birds = a->m_birds;
                if (birds.size() >= 2) {
                    g_waGoneBirds = {birds[0], birds[birds.size() - 1]};
                }
                std::vector<std::string> log;
                std::vector<C_Bird *> left = birds;
                for (size_t i = left.size(); i-- > 0;) {
                    log.push_back("fly " + ItName(left[i]->GetModel()) + " 40");
                    if (g_waGoneBirds.count(left[i])) {
                        log.push_back("delete bird " + ItName(left[i]->GetModel()));
                        left[i] = left.back();
                        left.pop_back();
                    }
                }
                a->m_time = 100;
                reset();
                a->Update(40);
                WA(r.fowlBirds, SaLogIs(log) && a->m_birds == left && a->m_time == (birds.empty() ? 100 : left.empty() ? 10140 : 140));
                g_waGoneBirds.clear();
                a->m_flown = false;
                a->m_time = 100;
                a->Update(40);
                WA(r.fowlBirds, a->m_time == 100);
                // The last one gone: 10 s more.
                const std::vector<C_Bird *> kept = a->m_birds;
                C_Bird *last = new C_Bird(AnFrame(FRAME_MODEL, "wa.lastbird", nullptr), true);
                a->m_birds = {last};
                g_waGoneBirds = {last};
                a->m_flown = true;
                a->m_time = 100;
                a->Update(40);
                WA(r.fowlBirds, a->m_birds.empty() && a->m_time == 10140);
                // None left: nothing more.
                a->m_time = 100;
                a->Update(40);
                WA(r.fowlBirds, a->m_time == 100);
                g_waGoneBirds.clear();
                a->m_birds = kept;
            }

            // Tick: once a second until they fly: a player within the distance, then the player's
            // other humans (actors past the 32 players' slots), then the camera.
            {
                std::vector<C_Human *> humans;
                const auto human = [&](const S_vector &pos, E_HumanRole role) {
                    auto *h = new C_Human();
                    h->role = role;
                    I3D_frame *hf = AnFrame(FRAME_DUMMY, "wa.human" + std::to_string(humans.size()), nullptr);
                    hf->m_localMatrix.m[3][0] = pos.x;
                    hf->m_localMatrix.m[3][1] = pos.y;
                    hf->m_localMatrix.m[3][2] = pos.z;
                    h->SetFrame(hf);
                    humans.push_back(h);
                    return h;
                };
                const float d = a->m_triggerDistance;
                for (int c = 0; c < 12; ++c) {
                    g_waActors.clear();
                    for (int i = 0; i < 40; ++i)
                        g_waActors.push_back(nullptr);
                    // c 0: none near; 1: player 0 near; 2: player 1 near after a gap (not seen); 3: a
                    // player human past the slots near; 4: a non-player one near; 5: the camera near;
                    // 6: flown already; 7: the time not up; 8: a player human in slot 32 near (the back ones
                    // reach down to it); 9: player 3 near (players 0..3 there); 10: a player human in slot
                    // 34 near (35 empty: not seen); 11: an actor in slot 36 near with the player role, not
                    // a human.
                    g_waActors[0] = human(here + S_vector(0, 0, d + 1.0f), HUMAN_ROLE_PLAYER);
                    g_waActors[1] = human(here + S_vector(c == 1 ? d - 0.5f : d + 2.0f, 0, 0), HUMAN_ROLE_PLAYER);
                    g_waActors[3] = human(here + S_vector(0, c == 2 || c == 9 ? 0.5f : 9.0f, 0), HUMAN_ROLE_PLAYER);
                    g_waActors[36] = human(here + S_vector(0, 0, c == 3 || c == 11 ? -(d - 0.1f) : -9.0f), HUMAN_ROLE_PLAYER);
                    if (c == 11)
                        g_waActors[36]->m_type = ACTOR_PIKE;
                    if (c == 10)
                        g_waActors[34] = human(here + S_vector(0, 0, 1.0f), HUMAN_ROLE_PLAYER);
                    g_waActors[37] = human(here + S_vector(c == 4 ? 1.0f : 20.0f, 0, 0), HUMAN_ROLE_ENEMY);
                    g_waActors[38] = human(here + S_vector(0, 0, 25.0f), HUMAN_ROLE_PLAYER);   // (the back ones up to a gap: 36..39)
                    g_waActors[39] = human(here + S_vector(30.0f, 0, 0), HUMAN_ROLE_PLAYER);
                    if (c != 2)
                        g_waActors[2] = human(here + S_vector(40.0f, 0, 0), HUMAN_ROLE_PLAYER);
                    if (c == 8) {
                        g_waActors[32] = human(here + S_vector(0, 0, d - 0.5f), HUMAN_ROLE_PLAYER);
                        for (int i = 33; i < 36; ++i)
                            g_waActors[static_cast<size_t>(i)] = human(here + S_vector(0, 0, 40.0f + i), HUMAN_ROLE_PLAYER);
                    }
                    camera->m_localMatrix.m[3][0] = here.x + (c == 5 ? d - 1.0f : 50.0f);
                    camera->m_localMatrix.m[3][1] = here.y;
                    camera->m_localMatrix.m[3][2] = here.z;
                    a->m_flown = c == 6;
                    a->m_checkTime = c == 7 ? 300 : 120;
                    reset();
                    a->Tick(250);
                    const bool starts = c == 1 || c == 3 || c == 5 || c == 8 || c == 9;
                    WA(r.fowlTrigger, a->m_flown == (starts || c == 6) && a->m_checkTime == (c == 7 ? 50 : c == 6 ? -130 : 870) &&
                                          g_streams.size() == (starts ? 1u : 0u));
                }
                // The countdown wraps by whole seconds.
                a->m_flown = false;
                for (C_Actor *&actor : g_waActors)
                    actor = nullptr;
                a->m_checkTime = -2500;
                a->Tick(10);
                WA(r.fowlTrigger, a->m_checkTime == 490 && !a->m_flown);
                // Due at 0; a second's wrap that ends at 0 goes on to 1000.
                a->m_checkTime = 250;
                a->Tick(250);
                WA(r.fowlTrigger, a->m_checkTime == 1000);
                a->m_checkTime = -1000;
                a->Tick(0);
                WA(r.fowlTrigger, a->m_checkTime == 1000);
                g_waActors.clear();
                for (C_Human *h : humans) {
                    h->m_type = ACTOR_HUMAN;
                    delete h;
                }
            }

            // The sound listener: footsteps or shots heard louder than 0.8 by its frame's actor.
            if (k == 0 && g_waListener) {
                S_FrameData data = {};
                data.actor = a;
                f->SetData(&data);
                const int chapters[] = {SOUND_CHAPTER_PUPPET_MOVEMENT, SOUND_CHAPTER_OBJECT_MANIPULATION, SOUND_CHAPTER_WEAPON_SHOOTING};
                for (int c = 0; c < 3; ++c) {
                    for (int loud = 0; loud < 2; ++loud) {
                        a->m_flown = false;
                        S_HeardSound heard = {};
                        heard.listener = f;
                        heard.chapter = chapters[c];
                        heard.intensity = loud ? 0.81f : 0.8f;
                        reset();
                        g_waListener(&heard);
                        WA(r.fowlTrigger, a->m_flown == (loud && c != 1) && g_streams.size() == (loud && c != 1 ? 1u : 0u));
                    }
                }
                S_HeardSound heard = {};
                heard.chapter = SOUND_CHAPTER_WEAPON_SHOOTING;
                heard.intensity = 1.0f;
                a->m_flown = false;
                g_waListener(&heard);
                heard.listener = f;
                data.actor = nullptr;
                g_waListener(&heard);
                f->SetData(nullptr);
                g_waListener(&heard);
                WA(r.fowlTrigger, !a->m_flown);
            }

            // Properties: chunk 0 the version (1) first; 1 the mode, 2 the radius, 3 the distance, 4
            // the count, 5 the model's name; other chunks are left.
            {
                auto *b = static_cast<C_FowlGenerator *>(creator->Create());
                S_SaBytes in;
                in.Head(0x777, 6 + 10 + 10 + 10 + 16 + 10 + 10 + 13).Head(0, 10).Val(1).Head(1, 10).Val(2).Head(2, 10).Val(4.5f)
                    .Head(9, 16).Head(3, 10).Val(9.0f).Head(3, 10).Val(6.5f);
                in.Head(5, 13).Put("pigeon\0", 7);
                in.Head(4, 10).Val(16);                 // (the count after the name)
                S_SaReader rd(in);
                WA(r.fowlSave, b->ReadProperties(rd.c) && b->m_mode == 2 && b->m_radius == 4.5f && b->m_triggerDistance == 6.5f &&
                                   b->m_count == 16 && std::strcmp(b->m_model.c_str(), "pigeon") == 0);
                S_SaBytes v2;
                v2.Head(0x777, 6 + 20).Head(0, 10).Val(2).Head(1, 10).Val(1);
                S_SaReader r2(v2);
                WA(r.fowlSave, !b->ReadProperties(r2.c) && b->m_mode == 2);
                S_SaBytes late;
                late.Head(0x777, 6 + 20).Head(1, 10).Val(1).Head(0, 10).Val(1);
                S_SaReader r3(late);
                WA(r.fowlSave, !b->ReadProperties(r3.c) && b->m_mode == 2);
                S_SaBytes none;
                none.Head(0x777, 6);
                S_SaReader r4(none);
                WA(r.fowlSave, !b->ReadProperties(r4.c));
                // A name without its terminator, a short count: the count's chunk read as 0.
                S_SaBytes raw;
                raw.Head(0x777, 6 + 10 + 8 + 9).Head(0, 10).Val(1).Head(4, 8).Val(static_cast<uint16_t>(5)).Head(5, 9).Put("abc", 3);
                S_SaReader r5(raw);
                WA(r.fowlSave, b->ReadProperties(r5.c) && b->m_count == 0 && std::strcmp(b->m_model.c_str(), "abc") == 0);
                const int copies = g_actorCopies;
                b->CopyFrom(a);
                WA(r.fowlSave, g_actorCopies == copies + 1 && b->m_count == 0);
                creator->Destroy(b);
            }
            // The saved game: chunk 0x10E1, flown and then the time since; LoadGame flies them up and
            // on by that time.
            {
                a->m_flown = true;
                a->m_time = 4321;
                S_SaBytes sv;
                sv.Head(0x777, 6 + 11).Head(0x10E1, 11).Val(static_cast<uint8_t>(1)).Val(4321);
                bool saved = false;
                WA(r.fowlSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv.b && saved);
                a->m_flown = false;
                S_SaBytes sv2;
                sv2.Head(0x777, 6 + 7).Head(0x10E1, 7).Val(static_cast<uint8_t>(0));
                WA(r.fowlSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv2.b && saved);
                if (a->m_mode == 1)
                    a->m_mode = 3;
                S_SaBytes ld;
                ld.Head(0x777, 6 + 11).Head(42, 11).Val(static_cast<uint8_t>(1)).Val(1500);
                S_SaReader rd(ld);
                std::vector<std::string> log;
                for (size_t i = a->m_birds.size(); i-- > 0;)
                    log.push_back("flyaway " + ItName(a->m_birds[i]->GetModel()));
                for (size_t i = a->m_birds.size(); i-- > 0;)
                    log.push_back("fly " + ItName(a->m_birds[i]->GetModel()) + " 1500");
                a->m_time = 7;
                reset();
                WA(r.fowlSave, a->LoadGame(rd.c) && a->m_flown && SaLogIs(log) && rd.c.m_depth == 1 && g_streams.size() == 1 &&
                                   a->m_time == (a->m_birds.empty() ? 0 : 1500) && g_soundRequests.empty());
                S_SaBytes ld0;
                ld0.Head(0x777, 6 + 7).Head(42, 7).Val(static_cast<uint8_t>(0));
                S_SaReader r0(ld0);
                reset();
                WA(r.fowlSave, a->LoadGame(r0.c) && !a->m_flown && g_itLog.empty() && rd.c.m_depth == 1);
                if (a->m_mode == 3)
                    a->m_mode = 1;
            }

            // StopMission: the birds deleted (from the back); the mission's ones back in their places
            // (their models out of the spare ones, shown); a server's listener let go (the index kept).
            {
                g_spareBirdModels.clear();
                I3D_frame *spare = AnFrame(FRAME_MODEL, "wa.spare", nullptr);
                g_spareBirdModels.push_back(spare);
                std::vector<std::string> log;
                std::vector<I3D_frame *> spares = {spare};
                for (size_t i = a->m_birds.size(); i-- > 0;) {
                    C_Bird *bird = a->m_birds[i];
                    log.push_back("delete bird " + ItName(bird->GetModel()));
                    if (!bird->OwnsModel()) {
                        log.push_back("on " + ItName(bird->GetModel()) + " 1");
                        log.push_back("update " + ItName(bird->GetModel()));
                    }
                }
                const bool server = k == 0;
                if (server && a->m_listener != -1)
                    log.push_back("removelistener " + std::to_string(a->m_listener));
                const int listener = a->m_listener;
                reset();
                a->StopMission(server);
                WA(r.fowlBirds, SaLogIs(log) && a->m_birds.empty() && g_spareBirdModels == spares && a->m_listener == listener);
                g_spareBirdModels.clear();
            }
            // RestartMission: stopped and started on the server side.
            g_gameRole = GAME_ROLE_CLIENT;
            a->m_listener = 5;
            reset();
            a->RestartMission(false);
            WA(r.fowl, a->m_listener == 77 && !g_itLog.empty() && g_itLog.front() == "removelistener 5" &&
                           g_itLog.back() == "addlistener " + ItName(f));
            a->StopMission(true);
            // No listener: none let go.
            a->m_listener = -1;
            reset();
            a->StopMission(true);
            WA(r.fowl, g_itLog.empty());
            creator->Destroy(a);
        }
        for (C_Bird *bird : g_birds)
            delete bird;
        g_birds.clear();
        g_spareBirdModels.clear();
        *activeCamera = oldCamera;
        g_rayTestHook = nullptr;
        g_waListener = nullptr;
    }
