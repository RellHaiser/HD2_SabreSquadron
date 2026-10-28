// (seat_actors_test.inc, inside SeatActorsTest: the fish.)

    // --- the fish -------------------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_FISH];
        SE(r.fish, creator && std::strcmp(creator->GetName(), "Fish") == 0);
        auto *a = static_cast<C_Fish *>(creator->Create());
        SE(r.fish, a->GetType() == ACTOR_FISH && a->m_topSpeed == 1.0f && a->m_radius == 2.0f && a->m_count == 10 &&
                       !a->m_started && a->m_speed == 0.0f && a->m_state == 0 && a->m_pause == 0 && a->m_time == 0 &&
                       a->m_fish.empty() && !a->SetFrame(nullptr) && !a->GetFrame());
        I3D_frame *school = AnFrame(FRAME_DUMMY, "se.school", nullptr);
        const float heading = rnd(-3, 3);
        school->m_localMatrix.m[0][0] = std::cos(heading);
        school->m_localMatrix.m[0][2] = -std::sin(heading);
        school->m_localMatrix.m[2][0] = std::sin(heading);
        school->m_localMatrix.m[2][2] = std::cos(heading);
        school->m_localMatrix.m[2][1] = rnd(-0.3f, 0.3f);   // (a tilted heading: made level at the start)
        school->m_localMatrix(3) = S_vector(rnd(-20, 20), rnd(-3, 0), rnd(-20, 20));
        SE(r.fish, a->SetFrame(school) && a->GetFrame() == school);

        // ReadProperties: the top speed, the radius, the count, raw (a short chunk leaves the rest).
        {
            S_SaBytes in;
            in.Head(0x777, 18).Val(2.5f).Val(3.25f).Val(7);
            S_SaReader rd(in);
            SE(r.fishProps, a->ReadProperties(rd.c) && a->m_topSpeed == 2.5f && a->m_radius == 3.25f && a->m_count == 7);
            S_SaBytes shortIn;
            shortIn.Head(0x777, 12).Val(1.75f).Val(static_cast<uint16_t>(9));
            S_SaReader rd2(shortIn);
            SE(r.fishProps, a->ReadProperties(rd2.c) && a->m_topSpeed == 1.75f && a->m_radius == 3.25f && a->m_count == 7);
            // CopyFrom: null nothing; else the base's, the top speed, the count, the radius.
            auto *b = static_cast<C_Fish *>(creator->Create());
            b->m_topSpeed = 9.0f;
            b->m_radius = 8.0f;
            b->m_count = 4;
            b->m_speed = 5.0f;
            b->m_state = 2;
            a->CopyFrom(nullptr);
            SE(r.fishProps, a->m_topSpeed == 1.75f && a->m_radius == 3.25f && a->m_count == 7);
            a->CopyFrom(b);
            SE(r.fishProps, a->m_topSpeed == 9.0f && a->m_radius == 8.0f && a->m_count == 4 && a->m_speed == 0.0f &&
                                a->m_state == 0);
            creator->Destroy(b);
        }
        a->m_topSpeed = rnd(0.6f, 2.0f);
        a->m_radius = rnd(1.0f, 3.0f);
        a->m_count = 10;                                  // (two places swap at a turn)

        // --- StartMission ---------------------------------------------------------------------------------------
        I3D_frame *model = AnFrame(FRAME_MODEL, "se.rybamodel", nullptr);
        I3D_frame *proto = AnFrame(FRAME_VISUAL, "ryba", model);
        g_seModel = model;
        // No model: only the activation.
        g_seModelFail = true;
        reset();
        a->StartMission(true);
        SE(r.fishStart, g_activations == std::vector<bool>{true} && !a->m_started && a->m_fish.empty() &&
                            SaLogIs({"getmodel ryba"}));
        g_seModelFail = false;
        // The visuals running out after two: two fish kept, not started, the model released.
        const int modelRefs = model->m_refCount;
        g_seVisualsLeft = 2;
        reset();
        a->StartMission(false);
        SE(r.fishStart, g_activations == std::vector<bool>{false} && !a->m_started && a->m_fish.size() == 2 &&
                            model->m_refCount == modelRefs && g_itLog.back() == "release se.rybamodel" &&
                            g_itLog[g_itLog.size() - 2] == "createvisual 5f4a424f");
        g_seVisualsLeft = -1;
        a->StopMission(true);
        a->m_started = false;
        for (C_Fish::S_FishMember *m : a->m_fish)
            delete m;
        a->m_fish.clear();
        // A model without "ryba" (fix: no fish, started).
        {
            auto *b = static_cast<C_Fish *>(creator->Create());
            I3D_frame *bf = AnFrame(FRAME_DUMMY, "se.school2", nullptr);
            b->SetFrame(bf);
            I3D_frame *bare = AnFrame(FRAME_MODEL, "se.bare", nullptr);
            g_seModel = bare;
            reset();
            std::srand(77);
            b->StartMission(true);
            SE(r.fishStart, b->m_started && b->m_fish.empty() && b->m_state == 2 && b->m_speed == 0.5f &&
                                SaLogIs({"getmodel ryba", "release se.bare"}));
            creator->Destroy(b);
            g_seModel = model;
        }
        // The fish: each a copy of "ryba" linked to the frame at a random place (x, y within half the
        // radius, z within the radius; drawn z first), its sway's phase and rate, scaled (z by 1..1.5,
        // y and x by 1..1.25; drawn z first), facing +z; then the school's speed 0.5, its level
        // direction, a pause of 1..2 s.
        struct S_FishO {
            S_vector pos, target;
            float phase, rate;
            I3D_frame *frame;
        };
        std::vector<S_FishO> fo;
        std::vector<std::string> log = {"getmodel ryba"};
        S_SeRandom rand0{g_waRandom};
        const float rad = a->m_radius;
        const int firstFish = g_seVisualsMade;
        for (int k = 0; k < a->m_count; ++k) {
            const std::string n = "fish" + std::to_string(firstFish + k);
            log.push_back("createvisual 5f4a424f");
            log.push_back("duplicate " + n + " ryba");
            const float z = rand0.Next(rad + rad) - rad;
            const float y = rand0.Next(rad) - rad * 0.5f;
            const float x = rand0.Next(rad) - rad * 0.5f;
            const float phase = rand0.Next(6.2831855f);
            const float rate = rand0.Next(0.5f) + 0.5f;
            for (float m : {rad + rad, rad, rad, 6.2831855f, 0.5f})
                log.push_back(SeLine("random %.9g", m));
            log.push_back("link " + n + " se.school 0");
            const float sz = rand0.Next(0.5f) + 1.0f;
            const float sy = rand0.Next(0.25f) + 1.0f;
            const float sx = rand0.Next(0.25f) + 1.0f;
            for (float m : {0.5f, 0.25f, 0.25f})
                log.push_back(SeLine("random %.9g", m));
            log.push_back("worlddir " + n + " 0 0 1 0");
            log.push_back("update " + n);
            fo.push_back({S_vector(x, y, z), S_vector(x, y, z), phase, rate, nullptr});
            (void)sx, (void)sy, (void)sz;
        }
        log.push_back("release se.rybamodel");
        // (The scales checked on the frames below; the rand0 pass is re-run for them.)
        S_SeRandom rand1{g_waRandom};
        std::vector<S_vector> scales;
        for (int k = 0; k < a->m_count; ++k) {
            for (int j = 0; j < 5; ++j)
                rand1.Next(1.0f);
            const float sz = rand1.Next(0.5f) + 1.0f;
            const float sy = rand1.Next(0.25f) + 1.0f;
            const float sx = rand1.Next(0.25f) + 1.0f;
            scales.emplace_back(sx, sy, sz);
        }
        S_vector dir0 = school->GetMatrix()(2);
        SeNormalize(dir0, (dir0.y * dir0.y + dir0.z * dir0.z) + dir0.x * dir0.x);
        dir0.y = 0.0f;
        SeNormalize(dir0, dir0.z * dir0.z + dir0.x * dir0.x);
        std::srand(4242);
        const int pause0 = std::rand() % 1000 + 1000;
        std::srand(4242);
        a->m_velocity = {9.0f, 9.0f, 9.0f, 9.0f};
        a->m_time = 55;
        reset();
        a->StartMission(true);
        bool fishOk = a->m_fish.size() == fo.size();
        for (size_t k = 0; fishOk && k < fo.size(); ++k) {
            const C_Fish::S_FishMember *m = a->m_fish[k];
            fishOk = RbSame(S_vector(m->pos.x, m->pos.y, m->pos.z), fo[k].pos) &&
                     RbSame(S_vector(m->target.x, m->target.y, m->target.z), fo[k].target) && m->phase == fo[k].phase &&
                     m->rate == fo[k].rate && m->frame && m->frame->m_parent == school &&
                     RbSame(m->frame->GetPos(), fo[k].pos) && RbSame(m->frame->m_scale, scales[k]) &&
                     ItName(m->frame) == "fish" + std::to_string(firstFish + static_cast<int>(k));
            fo[k].frame = m->frame;
        }
        SE(r.fishStart, g_activations == std::vector<bool>{true} && fishOk && a->m_started && a->m_speed == 0.5f &&
                            a->m_state == 2 && a->m_pause == pause0 && a->m_time == 0 &&
                            RbSame(S_vector(a->m_dir.x, a->m_dir.y, a->m_dir.z), dir0) &&
                            RbSame(S_vector(a->m_velocity.x, a->m_velocity.y, a->m_velocity.z), S_vector(0, 0, 0)) &&
                            SaLogIs(log) && model->m_refCount == modelRefs);
        // Started: only the activation.
        reset();
        a->StartMission(false);
        SE(r.fishStart, g_activations == std::vector<bool>{false} && g_itLog.empty() && a->m_fish.size() == fo.size());

        // --- the oracle of Tick and Update (from the asm) ---------------------------------------------------------
        I3D_frame *camera = AnFrame(FRAME_DUMMY, "se.camera", nullptr);
        setCamera(camera);
        struct S_SchoolO {
            S_vector dir, velocity;
            int state, pause, time;
            float speed;
            std::vector<S_vector> pos, target;
            std::vector<std::string> log;
            std::vector<S_SeSlide> slides;
            bool on;
            S_vector framePos;
            std::vector<S_vector> fishLocal;
        };
        const auto capture = [&](C_Fish *f) {
            S_SchoolO o;
            o.dir = S_vector(f->m_dir.x, f->m_dir.y, f->m_dir.z);
            o.velocity = S_vector(f->m_velocity.x, f->m_velocity.y, f->m_velocity.z);
            o.state = f->m_state;
            o.pause = f->m_pause;
            o.time = f->m_time;
            o.speed = f->m_speed;
            for (const C_Fish::S_FishMember *m : f->m_fish) {
                o.pos.emplace_back(m->pos.x, m->pos.y, m->pos.z);
                o.target.emplace_back(m->target.x, m->target.y, m->target.z);
                o.fishLocal.push_back(m->frame->GetPos());
            }
            o.on = f->GetFrame()->IsOn();
            o.framePos = f->GetFrame()->GetPos();
            return o;
        };
        const auto lengthOf = [](float sq) {
            int32_t bits;
            std::memcpy(&bits, &sq, 4);
            if (bits < 0x322bcc77)
                return 0.0f;
            return std::fabs(sq - 1.0f) < 1e-08f ? 1.0f : std::sqrt(sq);
        };
        // Visible: the camera within the radius + 15 m (the frame shown / hidden as it changes).
        const auto visible = [&](C_Fish *f, S_SchoolO &o) {
            const S_vector p = f->GetFrame()->GetWorldPos();
            const S_vector c = camera->GetWorldPos();
            const S_vector d(c.x - p.x, c.y - p.y, c.z - p.z);
            const float range = f->m_radius + 15.0f;
            if (range * range < (d.z * d.z + d.x * d.x) + d.y * d.y) {
                if (o.on)
                    o.log.push_back("on se.school 0");
                o.on = false;
                return false;
            }
            if (!o.on)
                o.log.push_back("on se.school 1");
            o.on = true;
            return true;
        };
        const auto tickOracle = [&](C_Fish *f, int t) {
            S_SchoolO o = capture(f);
            if (!visible(f, o))
                return o;
            const uint32_t time = static_cast<uint32_t>(t);
            o.time += t;
            const size_t count = o.pos.size();
            if (o.state == 0) {
                o.speed = static_cast<float>(time) * 0.007f + o.speed;
                if (o.speed > f->m_topSpeed) {
                    o.state = 1;
                    o.speed = f->m_topSpeed;
                    for (size_t i = count; i-- > 0;)
                        o.target[i] = S_vector(o.target[i].x * 1.1111112f, o.target[i].y * 1.1111112f, o.target[i].z * 0.9f);
                }
            } else if (o.state == 1) {
                const float slow = f->m_topSpeed * 0.1f;
                o.speed = o.speed - ((o.speed - slow) + 0.1f) * static_cast<float>(t) * 0.001f;
                if (o.speed < slow) {
                    o.speed = slow;
                    o.state = 2;
                    o.pause = std::rand() % 2000 + 3000;
                }
            } else if (o.state == 2) {
                o.pause -= t;
                if (o.pause < 0) {
                    o.state = 0;
                    S_SeRandom rr{g_waRandom};
                    const float yaw = rr.Next(4.712389f) - 2.3561945f;
                    const float pitch = rr.Next(0.94247788f) - 0.47123894f;
                    o.log.push_back(SeLine("random %.9g", 4.712389f));
                    o.log.push_back(SeLine("random %.9g", 0.94247788f));
                    o.dir = o.dir.RotateByMatrix(RotateYMatrix(yaw)).RotateByMatrix(RotateZMatrix(pitch));
                    SeNormalize(o.dir, (o.dir.x * o.dir.x + o.dir.y * o.dir.y) + o.dir.z * o.dir.z);
                    const int swapped = static_cast<int>(static_cast<float>(count) * 0.2f);
                    for (size_t i = count; i-- > 0;) {
                        o.target[i] = S_vector(o.target[i].x * 0.9f, o.target[i].y * 0.9f, o.target[i].z * 1.1111112f);
                        if (static_cast<int>(i) < swapped)
                            std::swap(o.target[i], o.target[count - i - 1]);
                    }
                }
            }
            const float ft = static_cast<float>(time);
            const float distance = ft * o.speed * 0.001f;
            S_vector pos = f->GetFrame()->GetWorldPos();
            const S_vector move(distance * o.dir.x, distance * o.dir.y, distance * o.dir.z);
            const float level = ft * 0.001f;
            if (o.dir.y > 0.0f) {
                o.dir.y -= level;
                if (o.dir.y < 0.0f)
                    o.dir.y = 0.0f;
            } else if (o.dir.y < 0.0f) {
                o.dir.y = level + o.dir.y;
                if (o.dir.y > 0.0f)
                    o.dir.y = 0.0f;
            }
            SeNormalize(o.dir, (o.dir.x * o.dir.x + o.dir.y * o.dir.y) + o.dir.z * o.dir.z);
            o.slides.push_back({pos, move, f->m_radius, 0xFFFFFFFFu, 0u, true});
            if (g_seSlide) {
                const S_vector slid(g_seSlideEnd.x - pos.x, g_seSlideEnd.y - pos.y, g_seSlideEnd.z - pos.z);
                o.dir = S_vector((g_seSlideNormal.x * 0.5f + slid.x) + o.dir.x, (g_seSlideNormal.y * 0.5f + slid.y) + o.dir.y,
                                 (g_seSlideNormal.z * 0.5f + slid.z) + o.dir.z);
                SeNormalize(o.dir, (o.dir.x * o.dir.x + o.dir.y * o.dir.y) + o.dir.z * o.dir.z);
                pos = g_seSlideEnd;
            } else {
                pos = S_vector(pos.x + move.x, pos.y + move.y, pos.z + move.z);
            }
            const S_vector from = f->GetFrame()->GetWorldPos();
            S_vector delta(pos.x - from.x, pos.y - from.y, pos.z - from.z);
            const float sq = (delta.x * delta.x + delta.z * delta.z) + delta.y * delta.y;
            if (lengthOf(sq) > distance) {
                SeNormalize(delta, sq);
                pos = S_vector(delta.x * distance + from.x, delta.y * distance + from.y, delta.z * distance + from.z);
            }
            o.velocity = S_vector((pos.x - from.x) * 10.0f, (pos.y - from.y) * 10.0f, (pos.z - from.z) * 10.0f);
            return o;
        };
        const auto updateOracle = [&](C_Fish *f, int t) {
            S_SchoolO o = capture(f);
            if (!visible(f, o))
                return o;
            I3D_frame *fr = f->GetFrame();
            const float time = static_cast<float>(static_cast<uint32_t>(t));
            const float seconds = time * 0.001f;
            S_vector step(seconds * o.velocity.x, seconds * o.velocity.y, seconds * o.velocity.z);
            const S_vector from = fr->GetWorldPos();
            const S_vector to(step.x + from.x, step.y + from.y, step.z + from.z);
            const float sq = (step.x * step.x + step.z * step.z) + step.y * step.y;
            bool turned = false;
            if (sq != 0.0f) {
                S_vector move = step;
                SeNormalize(move, sq);
                const float k = seconds * 0.06f;
                move = S_vector(move.x * k, move.y * k, move.z * k);
                S_vector fwd = fr->GetMatrix()(2);
                SeNormalize(fwd, (fwd.y * fwd.y + fwd.z * fwd.z) + fwd.x * fwd.x);
                S_vector d(fwd.x + move.x, fwd.y + move.y, fwd.z + move.z);
                SeNormalize(d, (d.x * d.x + d.y * d.y) + d.z * d.z);
                o.log.push_back("setdir se.school " + ItV(d) + " 0");
                turned = true;
            }
            o.log.push_back("setpos se.school " + ItV(to));
            o.log.push_back("update se.school");
            // (The frame's world after the move: its parent is none.)
            S_matrix world = fr->m_localMatrix;
            world(3) = to;
            S_vector offset = step;
            if (turned) {
                S_matrix inv;
                inv.Inverse(world);
                SeNormalize(offset, (offset.x * offset.x + offset.z * offset.z) + offset.y * offset.y);
                const float ahead = seconds + seconds;
                offset = S_vector(offset.x * ahead + to.x, offset.y * ahead + to.y, offset.z * ahead + to.z);
                offset *= inv;
            }
            o.framePos = to;
            const S_vector cam = camera->GetWorldPos();
            for (size_t i = o.pos.size(); i-- > 0;) {
                S_vector delta(o.target[i].x - o.pos[i].x, o.target[i].y - o.pos[i].y, o.target[i].z - o.pos[i].z);
                const float dsq = (delta.x * delta.x + delta.z * delta.z) + delta.y * delta.y;
                const float length = lengthOf(dsq);
                if (length != 0.0f) {
                    const float most = time * 0.00015f;
                    const float by = most < length ? most : length;
                    SeNormalize(delta, dsq);
                    o.pos[i] = S_vector(delta.x * by + o.pos[i].x, delta.y * by + o.pos[i].y, by * delta.z + o.pos[i].z);
                }
                I3D_frame *ff = f->m_fish[i]->frame;
                S_vector local;
                if (!(ff->m_frameFlags & FRMFLAGS_LMATRIX_VALID)) {
                    S_quat q = ff->m_rot;
                    q.Normalize();
                    S_matrix m = ff->m_localMatrix;
                    m.SetRot3(q);
                    local = m(2);
                } else {
                    local = ff->m_localMatrix(2);
                    SeNormalize(local, (local.y * local.y + local.z * local.z) + local.x * local.x);
                }
                S_vector d(local.x + offset.x, local.y + offset.y, local.z + offset.z);
                SeNormalize(d, (d.y * d.y + d.z * d.z) + d.x * d.x);
                o.log.push_back("worlddir " + ItName(ff) + " " + ItV(d) + " 0");
                const float sway = static_cast<float>(f->m_time) * f->m_fish[i]->rate * 0.0001f;
                const float ph = f->m_fish[i]->phase;
                const float x = static_cast<float>(std::sin(static_cast<double>(0.0001f * sway + ph)) * 0.25) + o.pos[i].x;
                const float z = static_cast<float>(std::cos(static_cast<double>(7e-05f * sway + ph)) * static_cast<double>(0.35f)) +
                                o.pos[i].z;
                const float y = static_cast<float>(std::cos(static_cast<double>(sway * 0.00012f + ph)) * static_cast<double>(0.45f)) +
                                o.pos[i].y;
                o.fishLocal[i] = S_vector(x, y, z);
                const S_vector at = o.fishLocal[i] * world;
                const S_vector dc(at.x - cam.x, at.y - cam.y, at.z - cam.z);
                const float near2 = (dc.z * dc.z + dc.y * dc.y) + dc.x * dc.x;
                o.log.push_back(SeLine("alpha %s %.9g", ItName(ff).c_str(), near2 < 1.0f ? near2 : 1.0f));
            }
            return o;
        };
        const auto same = [&](C_Fish *f, const S_SchoolO &o, bool update) {
            bool ok = f->m_state == o.state && f->m_pause == o.pause && f->m_time == o.time && SaSame(&f->m_speed, &o.speed, 4) &&
                      RbSame(S_vector(f->m_dir.x, f->m_dir.y, f->m_dir.z), o.dir) &&
                      RbSame(S_vector(f->m_velocity.x, f->m_velocity.y, f->m_velocity.z), o.velocity) &&
                      f->GetFrame()->IsOn() == o.on && RbSame(f->GetFrame()->GetPos(), o.framePos) && SaLogIs(o.log);
            for (size_t i = 0; ok && i < o.pos.size(); ++i)
                ok = RbSame(S_vector(f->m_fish[i]->pos.x, f->m_fish[i]->pos.y, f->m_fish[i]->pos.z), o.pos[i]) &&
                     RbSame(S_vector(f->m_fish[i]->target.x, f->m_fish[i]->target.y, f->m_fish[i]->target.z), o.target[i]) &&
                     RbSame(f->m_fish[i]->frame->GetPos(), o.fishLocal[i]);
            if (!update) {
                ok = ok && g_seSlides.size() == o.slides.size();
                for (size_t i = 0; ok && i < o.slides.size(); ++i)
                    ok = RbSame(g_seSlides[i].from, o.slides[i].from) && RbSame(g_seSlides[i].move, o.slides[i].move) &&
                         g_seSlides[i].radius == o.slides[i].radius && g_seSlides[i].types == o.slides[i].types &&
                         g_seSlides[i].flags == o.slides[i].flags && g_seSlides[i].normal;
            }
            return ok;
        };
        // Still (the velocity 0 after the start): the frame placed, not turned.
        {
            const S_vector sp = school->GetWorldPos();
            camera->m_localMatrix(3) = S_vector(sp.x + 1.0f, sp.y, sp.z);
            camera->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            const S_SchoolO e = updateOracle(a, 40);
            reset();
            a->Update(40);
            SE(r.fishUpdate, same(a, e, true));
            SE(r.fishUpdate, std::none_of(g_itLog.begin(), g_itLog.end(), [](const std::string &x) { return x.compare(0, 6, "setdir") == 0; }) &&
                                 std::any_of(g_itLog.begin(), g_itLog.end(), [](const std::string &x) { return x.compare(0, 6, "setpos") == 0; }));
        }
        // The runs: the camera near (now and then out of range), the steps' times, slides now and then;
        // the states driven through (a long pause shortened, the top speed met).
        for (int step = 0; step < 160; ++step) {
            const S_vector sp = school->GetWorldPos();
            const bool outOfRange = step % 23 == 17;
            const bool atEdge = step % 23 == 9;         // (15.2 m: within the radius + 15)
            camera->m_localMatrix(3) = outOfRange ? S_vector(sp.x + a->m_radius + 15.5f, sp.y, sp.z)
                                       : atEdge   ? S_vector(sp.x + 15.2f, sp.y, sp.z)
                                                  : S_vector(sp.x + rnd(-5, 5), sp.y + rnd(-2, 2), sp.z + rnd(-5, 5));
            camera->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            if (step % 7 == 3)
                a->m_pause = static_cast<int>(rnd(0, 150));
            g_seSlide = step % 5 == 2;
            g_seSlideEnd = S_vector(sp.x + rnd(-0.3f, 0.3f), sp.y + rnd(-0.3f, 0.3f), sp.z + rnd(-0.3f, 0.3f));
            g_seSlideNormal = S_vector(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
            const int t = step % 11 == 0 ? 100 : static_cast<int>(rnd(1, 120));
            const bool tick = step % 3 != 2;
            std::srand(1000 + step);
            const S_SchoolO e = tick ? tickOracle(a, t) : updateOracle(a, t);
            std::srand(1000 + step);
            reset();
            if (tick)
                a->Tick(t);
            else
                a->Update(t);
            if (tick)
                SE(r.fishTick, same(a, e, false));
            else
                SE(r.fishUpdate, same(a, e, true));
        }
        g_seSlide = false;
        // (Without a camera: neither runs.)
        setCamera(nullptr);
        {
            const S_SchoolO before = capture(a);
            reset();
            a->Tick(50);
            a->Update(50);
            SE(r.fishTick, a->m_time == before.time && g_itLog.empty() && g_seSlides.empty());
        }
        setCamera(camera);

        // --- the saved game: chunk 9999 holding 1000 the direction, 2000 the state, 3000 the pause, 4000 the
        // time, 5000 the speed, 6000 the velocity ---------------------------------------------------------------
        {
            a->m_dir = {0.25f, -0.5f, 0.75f, 9.0f};
            a->m_velocity = {1.5f, 2.5f, -3.5f, 9.0f};
            a->m_state = 1;
            a->m_pause = 1234;
            a->m_time = 98765;
            a->m_speed = 0.625f;
            S_SaBytes body;
            body.Head(1000, 18).Val(0.25f).Val(-0.5f).Val(0.75f).Head(2000, 10).Val(1).Head(3000, 10).Val(1234)
                .Head(4000, 10).Val(98765).Head(5000, 10).Val(0.625f).Head(6000, 18).Val(1.5f).Val(2.5f).Val(-3.5f);
            S_SaBytes sv;
            sv.Head(0x777, static_cast<uint32_t>(6 + 10 + 6 + body.b.size())).Head(0x3039, 10).Val(g_actorSaveTag)
                .Head(9999, static_cast<uint32_t>(6 + body.b.size()));
            sv.Put(body.b.data(), body.b.size());
            bool saved = false;
            SE(r.fishSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv.b && saved);
            // Loaded back (the chunk's id not checked); a short speed chunk leaves the chunk's first header
            // bytes; another id is entered, the rest read inside it.
            const auto loadOf = [&](const std::vector<uint8_t> &inner, uint16_t outerId) {
                S_SaBytes in;
                in.Head(0x777, static_cast<uint32_t>(6 + 10 + 6 + inner.size())).Head(0x3039, 10).Val(77)
                    .Head(outerId, static_cast<uint32_t>(6 + inner.size()));
                in.Put(inner.data(), inner.size());
                return in;
            };
            const auto setTo = [&](C_Fish *f) {
                f->m_dir = {0, 0, 0, 0};
                f->m_velocity = {0, 0, 0, 0};
                f->m_state = 9;
                f->m_pause = 9;
                f->m_time = 9;
                f->m_speed = 9;
            };
            {
                const S_SaBytes in = loadOf(body.b, 4321);
                S_SaReader rd(in);
                setTo(a);
                g_actorLoads.clear();
                SE(r.fishSave, a->LoadGame(rd.c) && rd.c.m_depth == 1 && g_actorLoads == std::vector<int>{77} && a->m_state == 1 &&
                                   a->m_pause == 1234 && a->m_time == 98765 && a->m_speed == 0.625f &&
                                   RbSame(S_vector(a->m_dir.x, a->m_dir.y, a->m_dir.z), S_vector(0.25f, -0.5f, 0.75f)) &&
                                   RbSame(S_vector(a->m_velocity.x, a->m_velocity.y, a->m_velocity.z), S_vector(1.5f, 2.5f, -3.5f)));
            }
            {
                S_SaBytes inner;
                inner.Head(5000, 8).Val(static_cast<uint16_t>(7)).Head(77, 16).Head(2000, 10).Val(2).Head(4000, 10).Val(5);
                const S_SaBytes in = loadOf(inner.b, 9999);
                S_SaReader rd(in);
                setTo(a);
                uint32_t bits = 9999u | (static_cast<uint32_t>(6 + inner.b.size()) << 16);
                float quirk;
                std::memcpy(&quirk, &bits, 4);
                SE(r.fishSave, a->LoadGame(rd.c) && rd.c.m_depth == 2 && SaSame(&a->m_speed, &quirk, 4) && a->m_state == 2 && a->m_time == 9 &&
                                   a->m_pause == 9);
            }
            {
                // (Short direction and velocity chunks: 0 read, the w kept; the outer chunk left.)
                S_SaBytes inner;
                inner.Head(1000, 10).Val(1.5f).Head(6000, 8).Val(static_cast<uint16_t>(3));
                const S_SaBytes in = loadOf(inner.b, 9999);
                S_SaReader rd(in);
                setTo(a);
                a->m_dir.w = 4.0f;
                a->m_velocity = {7, 7, 7, 4};
                SE(r.fishSave, a->LoadGame(rd.c) && rd.c.m_depth == 1 && a->m_dir.x == 0.0f && a->m_dir.y == 0.0f &&
                                   a->m_dir.z == 0.0f && a->m_dir.w == 4.0f && a->m_velocity.x == 0.0f && a->m_velocity.z == 0.0f &&
                                   a->m_velocity.w == 4.0f);
            }
        }

        // --- StopMission: the fish released, the frame shown; not started: only the deactivation ---------------
        {
            a->GetFrame()->m_frameFlags &= ~1u;
            std::vector<std::string> stopLog = {"on se.school 1"};
            for (size_t i = a->m_fish.size(); i-- > 0;)
                stopLog.push_back("release " + ItName(a->m_fish[i]->frame));
            reset();
            a->StopMission(false);
            SE(r.fishStop, g_deactivations == std::vector<bool>{false} && !a->m_started && a->m_fish.empty() &&
                               SaLogIs(stopLog));
            reset();
            a->StopMission(true);
            SE(r.fishStop, g_deactivations == std::vector<bool>{true} && g_itLog.empty());
            // RestartMission: stopped and started on the server side whatever it is given.
            reset();
            std::srand(5);
            a->RestartMission(false);
            SE(r.fishStop, g_deactivations == std::vector<bool>{true} && g_activations == std::vector<bool>{true} &&
                               a->m_started && a->m_fish.size() == static_cast<size_t>(a->m_count));
            a->StopMission(true);
        }
        creator->Destroy(a);
        (void)proto;
    }
