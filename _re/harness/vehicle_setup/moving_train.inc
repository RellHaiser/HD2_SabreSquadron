// (moving_actors_test.inc, inside MovingActorsTest: the train.)

    // --- the train -------------------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_TRAIN];
        MA(r.train, creator && std::strcmp(creator->GetName(), "Train") == 0);
        auto *t = static_cast<C_Train *>(creator->Create());
        const auto noSounds = [&](const C_Train *a) {
            if (a->m_steam || a->m_whistleNear || a->m_whistleFar || a->m_brakeLoop || a->m_brake || a->m_jar)
                return false;
            for (const auto &set : a->m_chuffSets)
                for (I3D_sound *s : set.sounds)
                    if (s)
                        return false;
            for (const auto *w : a->m_wagons)
                if (w->wheelSound)
                    return false;
            return true;
        };
        MA(r.train, t->GetType() == ACTOR_TRAIN && t->m_crossheadHeight == -0.3f && t->m_chuffSet == -1 && !t->m_running &&
                        !t->m_started && t->m_wagons.empty() && t->m_wheels.empty() && t->m_path.empty() &&
                        t->m_lastMarked == -1 && t->m_firstMarked == -1 && noSounds(t) && !t->m_player.GetTrack() &&
                        t->m_player.GetKeyIndex() == 0 && t->m_player.m_tolerance == 0.5f &&
                        t->m_player.m_pathIndex == -1 && !t->m_player.m_active && t->m_player.m_time == 0 &&
                        !t->m_player.m_frame && t->m_speed == 0.0f && t->m_brakeFade == 0.0f &&
                        t->m_chuffSets[0].count == 0 && t->m_chuffSets[3].sounds[3] == nullptr);

        // SetFrame: a model only; it becomes the first wagon (its name), then the base's.
        I3D_frame *model = AnFrame(FRAME_MODEL, "ma.train", nullptr);
        model->m_localMatrix.m[3][0] = rnd(-20, 20);
        model->m_localMatrix.m[3][2] = rnd(-20, 20);
        I3D_frame *visual = AnFrame(FRAME_VISUAL, "ma.notmodel", nullptr);
        MA(r.train, !t->SetFrame(nullptr) && !t->SetFrame(visual) && t->m_wagons.empty() && !t->GetFrame());
        MA(r.train, t->SetFrame(model) && t->GetFrame() == model && t->m_wagons.size() == 1 &&
                        t->m_wagons[0]->name == "ma.train" && t->m_wagons[0]->length == 10.0f &&
                        t->m_wagons[0]->gap == 3.0f && !t->m_wagons[0]->frame && !t->m_wagons[0]->body &&
                        !t->m_wagons[0]->wheelSound && !t->m_wagons[0]->wheelSoundFrame && !t->m_wagons[0]->volume);
        MA(r.train, t->SetFrame(model) && t->m_wagons.size() == 2 && t->m_wagons[1]->name == "ma.train");
        {
            // (The whole name, not the short one.)
            auto *pt = static_cast<C_Train *>(creator->Create());
            I3D_frame *pref = AnFrame(FRAME_MODEL, "ma.prefix.train", nullptr);
            pref->m_nameOffset = 10;
            MA(r.train, pt->SetFrame(pref) && pt->m_wagons[0]->name == "ma.prefix.train");
            creator->Destroy(pt);
        }

        // ReadProperties: the wagons dropped; chunk 1 the version (2 or 3), 8 the name (dropped), 10 a
        // wagon, 0x14 its length, 0x1E its gap; another id entered and not left.
        const auto chunkOf = [](uint16_t id, const std::vector<uint8_t> &payload) {
            S_SaBytes b;
            b.Head(id, static_cast<uint32_t>(6 + payload.size()));
            b.Put(payload.data(), payload.size());
            return b.b;
        };
        const auto textOf = [](const std::string &s) {
            std::vector<uint8_t> v(s.begin(), s.end());
            v.push_back(0);
            return v;
        };
        const auto valueOf = [](auto value) {
            std::vector<uint8_t> v(sizeof value);
            std::memcpy(v.data(), &value, sizeof value);
            return v;
        };
        const auto wrap = [](const std::vector<std::vector<uint8_t>> &chunks) {
            S_SaBytes b;
            size_t n = 6;
            for (const auto &c : chunks)
                n += c.size();
            b.Head(0x777, static_cast<uint32_t>(n));
            for (const auto &c : chunks)
                b.Put(c.data(), c.size());
            return b;
        };
        const auto wagonIs = [](const C_Train *a, size_t i, const std::string &name, float length, float gap) {
            return i < a->m_wagons.size() && a->m_wagons[i]->name == name &&
                   SaSame(&a->m_wagons[i]->length, &length, 4) && SaSame(&a->m_wagons[i]->gap, &gap, 4) &&
                   !a->m_wagons[i]->frame;
        };
        const auto bitsOf = [](uint32_t bits) {
            float f;
            std::memcpy(&f, &bits, 4);
            return f;
        };
        {
            const S_SaBytes in = wrap({chunkOf(1, valueOf(3)), chunkOf(8, textOf("ma.trainname")),
                                       chunkOf(0x14, valueOf(7.5f)), chunkOf(10, textOf("ma.w0")),
                                       chunkOf(0x14, valueOf(12.5f)), chunkOf(0x1E, valueOf(2.25f)),
                                       chunkOf(10, textOf("ma.w1")), chunkOf(0x1E, valueOf(4.0f)),
                                       chunkOf(10, textOf("ma.w2")), chunkOf(0x14, valueOf(8.0f))});
            S_SaReader rd(in);
            MA(r.trainProps, t->ReadProperties(rd.c) && t->m_wagons.size() == 3 && wagonIs(t, 0, "ma.w0", 12.5f, 2.25f) &&
                                 wagonIs(t, 1, "ma.w1", 10.0f, 4.0f) && wagonIs(t, 2, "ma.w2", 8.0f, 3.0f) &&
                                 rd.c.RemainingSize() == 0);
        }
        {
            // Version 2; a name of 1200 characters keeps 999; short length (0) and gap chunks: the gap
            // keeps the version chunk's header bits, later the last gap read; an unknown chunk entered:
            // the wagon in it read, the reading ends with it.
            const std::string longName(1200, 'n');
            std::vector<uint8_t> unknown = chunkOf(10, textOf("ma.inner"));
            const std::vector<uint8_t> innerLength = chunkOf(0x14, valueOf(6.0f));
            unknown.insert(unknown.end(), innerLength.begin(), innerLength.end());
            const S_SaBytes in = wrap({chunkOf(1, valueOf(2)), chunkOf(10, textOf(longName)), chunkOf(0x1E, {1, 2}),
                                       chunkOf(0x14, {9, 9, 9}), chunkOf(10, textOf("ma.w1b")),
                                       chunkOf(0x1E, valueOf(5.0f)), chunkOf(10, textOf("ma.last")), chunkOf(0x1E, {7}),
                                       chunkOf(0x99, unknown), chunkOf(10, textOf("ma.after"))});
            S_SaReader rd(in);
            MA(r.trainProps, t->ReadProperties(rd.c) && t->m_wagons.size() == 4 &&
                                 wagonIs(t, 0, std::string(999, 'n'), 0.0f, bitsOf(10u << 16 | 1u)) &&
                                 wagonIs(t, 1, "ma.w1b", 10.0f, 5.0f) && wagonIs(t, 2, "ma.last", 10.0f, 5.0f) &&
                                 wagonIs(t, 3, "ma.inner", 6.0f, 3.0f));
        }
        for (int c = 0; c < 5; ++c) {
            // Version 4, 1 or a first chunk of another id: false (the wagons dropped all the same); an
            // empty chunk; a version chunk too short.
            S_SaBytes in = c == 0   ? wrap({chunkOf(1, valueOf(4)), chunkOf(10, textOf("x"))})
                           : c == 1 ? wrap({chunkOf(1, valueOf(1))})
                           : c == 2 ? wrap({chunkOf(2, valueOf(3)), chunkOf(10, textOf("x"))})
                           : c == 3 ? wrap({})
                                    : wrap({chunkOf(1, {3, 0}), chunkOf(10, textOf("x"))});
            S_SaReader rd(in);
            MA(r.trainProps, !t->ReadProperties(rd.c) && t->m_wagons.empty());
            t->SetFrame(model);
        }
        {
            const S_SaBytes in = wrap({chunkOf(1, valueOf(3)), chunkOf(10, textOf("ma.w0")), chunkOf(0x14, valueOf(12.5f)),
                                       chunkOf(0x1E, valueOf(2.25f)), chunkOf(10, textOf("ma.w1")),
                                       chunkOf(0x1E, valueOf(4.0f)), chunkOf(10, textOf("ma.w2")),
                                       chunkOf(0x14, valueOf(8.0f))});
            S_SaReader rd(in);
            t->ReadProperties(rd.c);
        }

        // --- StartMission: the wagons' frames, bodies, wheels and boxes; the sounds ------------------------
        // The frame volume as S_FrameVolume::Init makes it (0x005df710; the submarine's hull boxes too).
        const auto frameVolumeIs = [&](const S_FrameVolume *v, I3D_frame *fr, uint32_t flags, uint32_t type,
                                       C_Actor *actor) {
            if (!v)
                return false;
            I3D_bbox box;
            S_vector center(0.0f, 0.0f, 0.0f);
            float radius = 0.0f;
            if (fr->GetType() == FRAME_DUMMY) {
                box = static_cast<I3D_dummy *>(fr)->GetBBox();
                center = S_vector((box.max.x + box.min.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
                                  (box.max.z + box.min.z) * 0.5f);
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
            const S_matrix world = fr->GetMatrix();
            S_matrix inv;
            inv.Inverse(world);
            const S_vector s = fr->m_parent ? world.GetScale() : fr->m_scale;
            const float yz = s.y <= s.z ? s.z : s.y;
            const float most = s.x <= yz ? yz : s.x;
            const S_CollisionVolume &a = v->volume;
            const S_vector c = center * world;
            const float rr = most * radius;
            return a.flags == flags && a.frame == fr && a.type == type && a.actor == actor && RbSameBox(a.bbox, box) &&
                   SaSame(&a.radius, &rr, 4) && RbSame(a.center, c) && a.matrix == &fr->m_worldMatrix &&
                   a.invMatrix == &v->invMatrix && SaSame(&v->invMatrix, &inv, sizeof inv) && !a.cell &&
                   a.capsuleRadius == 0.0f;
        };
        // The wagons' frames: user frames in the scene, each with its "BODY" (the second's a dummy, its
        // box; the third's a visual with a mesh), wheels (right then left; the first wagon's under its
        // body, its left 01 a dummy, passed over), rods (some missing) and its box frame (the first's
        // "BODYCOLB00", the second's its body, the third's itself).
        struct S_MaWagon {
            I3D_frame *frame = nullptr, *body = nullptr, *hull = nullptr;
            std::vector<C_Train::S_Wheel> wheels;
        };
        int trainNo = 0;
        const auto boxDummy = [&](I3D_frame *parent, const std::string &name, const std::string &logName) {
            I3D_frame *d = AnFrame(FRAME_DUMMY, name, parent);
            g_itNames[d] = logName;
            AnSetBBox(d, S_vector(rnd(-1.5f, -0.5f), rnd(-0.5f, 0.0f), rnd(-6, -3)),
                      S_vector(rnd(0.5f, 1.5f), rnd(1.5f, 3.0f), rnd(3, 6)));
            d->m_localMatrix.m[3][1] = rnd(0.0f, 0.5f);
            return d;
        };
        const auto makeWagons = [&](const C_Train *a) {
            std::vector<S_MaWagon> made(a->m_wagons.size());
            const std::string tag = "t" + std::to_string(trainNo++) + ".";
            for (size_t i = 0; i < made.size(); ++i) {
                S_MaWagon &w = made[i];
                const std::string wn = tag + "w" + std::to_string(i);
                w.frame = AnFrame(FRAME_USER, a->m_wagons[i]->name, nullptr);
                g_itNames[w.frame] = wn;
                // (A rod's name straight under the frame, looked at first: the rods are the wheels'.)
                g_itNames[AnFrame(FRAME_DUMMY, "THR00", w.frame)] = wn + ".decoy";
                const float turn = rnd(-0.6f, 0.6f);
                RbTilt(w.frame, std::cos(turn), std::sin(turn), i == 1);
                if (i % 3 == 1) {                       // (scaled: the box's radius)
                    for (int row = 0; row < 3; ++row)
                        for (int col = 0; col < 3; ++col)
                            w.frame->m_localMatrix.m[row][col] *= 1.3f;
                }
                w.frame->m_localMatrix.m[3][0] = rnd(-30, 30);
                w.frame->m_localMatrix.m[3][1] = rnd(0, 2);
                w.frame->m_localMatrix.m[3][2] = rnd(-30, 30);
                if (i == 0)
                    w.frame->m_localMatrix.m[3][3] = 1.25f;   // (its saved w)
                w.frame->m_scale = S_vector(rnd(0.8f, 1.2f), rnd(0.8f, 1.2f), rnd(0.8f, 1.2f));
                if (i % 3 == 1) {
                    // (A visual named like the box frame: not a dummy, passed over.)
                    g_itNames[AnFrame(FRAME_VISUAL, "BODYCOLB00", w.frame)] = wn + ".notcolb";
                    w.body = boxDummy(w.frame, "BODY", wn + ".body");
                } else {
                    w.body = AnFrame(FRAME_VISUAL, "BODY", w.frame);
                    g_itNames[w.body] = wn + ".body";
                    RbBoxFrame(w.body, "BODYMESH", -1.2f, 1.1f, 0.0f, 2.5f, -5.0f, 5.5f, S_vector(0.0f, 0.1f, 0.0f));
                }
                w.body->m_localMatrix.m[3][1] = rnd(0.3f, 0.8f);
                w.body->m_localMatrix.m[3][2] = rnd(-0.3f, 0.3f);
                w.hull = i % 3 == 0 ? boxDummy(w.frame, "BODYCOLB00", wn + ".colb") : i % 3 == 1 ? w.body : w.frame;
                if (w.hull == w.frame) {                // (its bound for the box's moves: valid, no UpdateBound)
                    w.frame->m_frameFlags |= FRMFLAGS_BOUND_VALID;
                    w.frame->m_bound.bbox.min = S_vector(rnd(-1.4f, -1.0f), rnd(-0.2f, 0.0f), rnd(-6, -4));
                    w.frame->m_bound.bbox.max = S_vector(rnd(1.0f, 1.4f), rnd(2.0f, 3.0f), rnd(4, 6));
                }
                const int counts[3][2] = {{3, 2}, {1, 1}, {1, 0}};   // [wagon][side]: left, right
                for (int side = 1; side >= 0; --side) {
                    for (int n = 0; n < counts[i % 3][side]; ++n) {
                        char name[16];
                        std::snprintf(name, sizeof name, side ? "WHR%02d" : "WHL%02d", n);
                        const bool dummy = i % 3 == 0 && side == 0 && n == 1;
                        I3D_frame *wh = AnFrame(dummy ? FRAME_DUMMY : FRAME_VISUAL, name, i % 3 == 0 ? w.body : w.frame);
                        g_itNames[wh] = wn + "." + name;
                        RbTilt(wh, std::cos(rnd(0, 6)), std::sin(rnd(0, 6)), true);
                        wh->m_localMatrix.m[3][0] = side ? 0.8f : -0.8f;
                        wh->m_localMatrix.m[3][1] = 0.4f;
                        wh->m_localMatrix.m[3][2] = rnd(-4, 4);
                        if (dummy)
                            continue;
                        const float r0 = rnd(0.3f, 0.7f);
                        I3D_bbox &b = static_cast<I3D_visual *>(wh)->Field<I3D_bound_volume>(0x1A0).bbox;
                        b.min = S_vector(-0.1f, -r0 + rnd(-0.05f, 0.05f), -r0);
                        b.max = S_vector(0.1f, r0, r0);
                        static_cast<I3D_visual *>(wh)->VisualFlags() |= VISFLAGS_BOUND_VALID;
                        C_Train::S_Wheel e;
                        e.wheel = wh;
                        e.radius = (b.max.y - b.min.y) * 0.5f;
                        // (Rods: none on the second wagon's left wheel; the third's left has a connecting
                        // rod without its crosshead.)
                        const bool rods = !(i % 3 == 1 && side == 0);
                        if (rods) {
                            std::snprintf(name, sizeof name, side ? "THR%02d" : "THL%02d", n);
                            e.rod = AnFrame(FRAME_DUMMY, name, wh);
                            g_itNames[e.rod] = wn + "." + name;
                            e.rod->m_localMatrix.m[3][1] = 0.15f;
                            std::snprintf(name, sizeof name, side ? "DTHR%02d" : "DTHL%02d", n);
                            e.connectingRod = AnFrame(FRAME_DUMMY, name, wh);
                            g_itNames[e.connectingRod] = wn + "." + name;
                            RbTilt(e.connectingRod, std::cos(rnd(-0.3f, 0.3f)), std::sin(rnd(-0.3f, 0.3f)), true);
                            e.connectingRod->m_localMatrix.m[3][1] = 0.12f;
                            e.connectingRod->m_localMatrix.m[3][2] = rnd(-0.1f, 0.1f);
                            if (!(i % 3 == 2 && side == 0)) {
                                e.crosshead = AnFrame(FRAME_DUMMY, side ? "STHR00" : "STHL00", e.connectingRod);
                                g_itNames[e.crosshead] = wn + (side ? ".STHR00." : ".STHL00.") + std::to_string(n);
                                e.crosshead->m_localMatrix.m[3][1] = rnd(-0.05f, 0.05f);
                                e.crosshead->m_localMatrix.m[3][2] = rnd(1.2f, 1.8f);
                            }
                        }
                        w.wheels.push_back(e);
                    }
                }
            }
            return made;
        };
        const auto sceneOf = [&](const std::vector<S_MaWagon> &made, int missing) {
            g_maSceneFrames.clear();
            for (size_t i = 0; i < made.size(); ++i)
                if (static_cast<int>(i) != missing)
                    g_maSceneFrames[made[i].frame->m_name] = made[i].frame;
        };
        // The sounds StartMission loads, in turn.
        struct S_MaTrainSound {
            std::string file;
            I3D_frame *parent;
            S_vector pos;
            bool loop, on;
            float nearRange, farRange;
        };
        const auto trainSounds = [&](const C_Train *a, const std::vector<S_MaWagon> &made) {
            std::vector<S_MaTrainSound> e;
            const S_vector origin(0.0f, 0.0f, 0.0f), top(0.0f, 3.0f, 0.0f), bottom(0.0f, -1.0f, 0.0f);
            for (size_t i = made.size(); i-- > 0;) {
                const bool last = i == made.size() - 1;
                e.push_back({i == 0 ? "l_hheels.wav" : last ? "l_eheels.wav" : "l_rheels.wav", made[i].body, origin, true,
                             true, 10.0f, last ? 70.0f : 50.0f});
            }
            I3D_frame *f = a->GetFrame();
            e.push_back({"l_steam.wav", f, origin, true, true, 4.0f, 20.0f});
            e.push_back({"l_whisnr.wav", f, top, false, false, 32.0f, 54.0f});
            e.push_back({"l_whisfr.wav", f, top, false, false, 100.0f, 350.0f});
            e.push_back({"l_braklp.wav", f, bottom, true, false, 48.0f, 64.0f});
            e.push_back({"l_brake.wav", f, bottom, false, false, 32.0f, 48.0f});
            e.push_back({"l_jar.wav", f, bottom, false, false, 48.0f, 64.0f});
            for (int set = 4; set-- > 0;)
                for (int j = 4; j-- > 0;)
                    e.push_back({"l_sts" + std::to_string(set + 1) + static_cast<char>('a' + j) + ".wav", f, origin, false,
                                 false, 5.0f, 50.0f});
            return e;
        };
        // The train's sounds in StartMission's order (with their frames).
        const auto loadedSounds = [&](const C_Train *a) {
            std::vector<std::pair<I3D_sound *, S_SoundFrame *>> s;
            for (size_t i = a->m_wagons.size(); i-- > 0;)
                s.push_back({a->m_wagons[i]->wheelSound, a->m_wagons[i]->wheelSoundFrame});
            s.push_back({a->m_steam, a->m_steamFrame});
            s.push_back({a->m_whistleNear, a->m_whistleNearFrame});
            s.push_back({a->m_whistleFar, a->m_whistleFarFrame});
            s.push_back({a->m_brakeLoop, a->m_brakeLoopFrame});
            s.push_back({a->m_brake, a->m_brakeFrame});
            s.push_back({a->m_jar, a->m_jarFrame});
            for (int set = 4; set-- > 0;)
                for (int j = 4; j-- > 0;)
                    s.push_back({a->m_chuffSets[set].sounds[j], a->m_chuffSets[set].frames[j]});
            return s;
        };
        const auto soundsOk = [&](const C_Train *a, const std::vector<S_MaWagon> &made, int firstName,
                                  std::vector<std::string> *log) {
            const auto e = trainSounds(a, made);
            const auto s = loadedSounds(a);
            const size_t wheels = made.size();
            for (size_t k = 0; k < e.size(); ++k) {
                I3D_sound *snd = s[k].first;
                if (!snd || !g_sounds.count(snd))
                    return false;
                const S_FakeSound &f = g_sounds[snd];
                const std::string name = "snd" + std::to_string(firstName + static_cast<int>(k));
                const bool wheel = k < wheels;
                if (ItName(snd) != name || f.file != e[k].file || f.type != I3DSOUND_POINT || f.range[0] != e[k].nearRange ||
                    f.range[1] != e[k].farRange || f.range[2] != 0.5f || f.range[3] != 0.5f || f.loop != e[k].loop ||
                    snd->IsOn() != e[k].on || snd->m_parent != e[k].parent || !RbSame(snd->GetPos(), e[k].pos) ||
                    g_linkFlags[snd] != 0 ||
                    !s[k].second || s[k].second->sound != snd || s[k].second->trackSector ||
                    s[k].second->baseVolume != (wheel ? 0.0f : 1.0f) || f.volumes != (wheel ? 1 : 0) ||
                    (wheel && f.volume != 0.0f))
                    return false;
                if (log) {
                    log->push_back("createframe 4");
                    log->push_back("update " + name);
                    log->push_back("register " + name + " 0");
                    if (wheel) {
                        char text[96];
                        std::snprintf(text, sizeof text, "sectorfactor %s %.9g 0", name.c_str(), s[k].second->sectorFactor);
                        log->push_back("update " + name);
                        log->push_back(text);
                    }
                }
            }
            for (int set = 0; set < 4; ++set)
                if (a->m_chuffSets[set].minSpeed != static_cast<float>(set * 12) || a->m_chuffSets[set].count != 4 ||
                    a->m_chuffSets[set].next != 0)
                    return false;
            return a->m_chuffSet == -1;
        };
        const auto wheelsOf = [](const std::vector<S_MaWagon> &made) {
            std::vector<C_Train::S_Wheel> e;
            for (size_t i = made.size(); i-- > 0;)
                e.insert(e.end(), made[i].wheels.begin(), made[i].wheels.end());
            return e;
        };
        const auto sameWheels = [](const std::vector<C_Train::S_Wheel> &a, const std::vector<C_Train::S_Wheel> &b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].wheel != b[i].wheel || a[i].rod != b[i].rod || a[i].connectingRod != b[i].connectingRod ||
                    a[i].crosshead != b[i].crosshead || !SaSame(&a[i].radius, &b[i].radius, 4))
                    return false;
            return true;
        };
        std::vector<S_MaWagon> made = makeWagons(t);
        {
            // A wagon missing: the later ones (taken first) done, then it stops (not started, no sounds).
            sceneOf(made, 1);
            reset();
            t->StartMission(true);
            const std::string w2 = ItName(made[2].frame);
            MA(r.trainStart, !t->m_started && noSounds(t) && t->m_wagons[2]->frame == made[2].frame &&
                                 !t->m_wagons[1]->frame && !t->m_wagons[0]->frame && t->m_trainLength == 3.0f &&
                                 g_activations == std::vector<bool>{true} &&
                                 SaLogIs({"find ma.w2 80", "link " + w2 + " sector 1", "update " + w2, "find ma.w1 80"}) &&
                                 g_volumeLog.size() == 2 && g_volumeLog[0].op == 'A' && g_volumeLog[1].op == 'M' &&
                                 g_volumeLog[0].volume == &t->m_wagons[2]->volume->volume &&
                                 t->m_wheels.size() == made[2].wheels.size());
            // (Its box and wheels stay; a train made anew goes on.)
            creator->Destroy(t);
            auto *fresh = static_cast<C_Train *>(creator->Create());
            fresh->SetFrame(model);
            const S_SaBytes in = wrap({chunkOf(1, valueOf(3)), chunkOf(10, textOf("ma.w0")), chunkOf(0x14, valueOf(12.5f)),
                                       chunkOf(0x1E, valueOf(2.25f)), chunkOf(10, textOf("ma.w1")),
                                       chunkOf(0x1E, valueOf(4.0f)), chunkOf(10, textOf("ma.w2")),
                                       chunkOf(0x14, valueOf(8.0f))});
            S_SaReader rd(in);
            fresh->ReadProperties(rd.c);
            t = fresh;
        }
        for (int pass = 0; pass < 2; ++pass) {
            // The whole train (twice: started again after StopMission, from where it was put back).
            made = makeWagons(t);
            sceneOf(made, -1);
            g_maSoundOn = true;
            const int firstSound = g_maSoundsMade;
            // (What StartMission clears.)
            t->m_running = true;
            t->m_speed = 5.0f;
            t->m_jarTimer = 7;
            t->m_brakeTimer = 9;
            t->m_brakeFade = 0.5f;
            reset();
            t->StartMission(pass == 0);
            std::vector<std::string> log;
            float length = 0.0f;
            bool wagonsOk = true;
            for (size_t i = made.size(); i-- > 0;) {
                const std::string wn = ItName(made[i].frame);
                log.push_back("find " + t->m_wagons[i]->name + " 80");
                log.push_back("link " + wn + " sector 1");
                log.push_back("update " + wn);
                if (i != made.size() - 1)
                    length = t->m_wagons[i]->length + length;
                length = t->m_wagons[i]->gap + length;
                const C_Train::S_Wagon *w = t->m_wagons[i];
                const S_vector4 pos = {made[i].frame->m_localMatrix.m[3][0], made[i].frame->m_localMatrix.m[3][1],
                                       made[i].frame->m_localMatrix.m[3][2], made[i].frame->m_localMatrix.m[3][3]};
                wagonsOk = wagonsOk && w->frame == made[i].frame && w->body == made[i].body &&
                           made[i].frame->m_parent == g_sector && SaSame(&w->savedPos, &pos, sizeof pos) &&
                           SaSameQuat(w->savedRot, made[i].frame->m_rot) &&
                           frameVolumeIs(w->volume, made[i].hull, 0x40, 0x2000, t);
            }
            std::vector<std::string> soundLog;
            const bool sounds = soundsOk(t, made, firstSound, &soundLog);
            log.insert(log.end(), soundLog.begin(), soundLog.end());
            bool volumes = g_volumeLog.size() == 6;
            for (size_t k = 0; volumes && k < 3; ++k)
                volumes = g_volumeLog[2 * k].op == 'A' && g_volumeLog[2 * k + 1].op == 'M' &&
                          g_volumeLog[2 * k].volume == &t->m_wagons[2 - k]->volume->volume &&
                          g_volumeLog[2 * k + 1].volume == g_volumeLog[2 * k].volume;
            MA(r.trainStart, t->m_started && t->m_time == 1 && !t->m_running && t->m_speed == 0.0f &&
                                 t->m_brakeTimer == 0 && t->m_jarTimer == 0 && t->m_brakeFade == 0.0f &&
                                 SaSame(&t->m_trainLength, &length, 4) && wagonsOk && volumes &&
                                 sameWheels(t->m_wheels, wheelsOf(made)) && g_activations == std::vector<bool>{pass == 0});
            MA(r.trainSounds, sounds && SaLogIs(log));
            // Again: only the activation.
            reset();
            t->StartMission(false);
            MA(r.trainStart, g_activations == std::vector<bool>{false} && g_itLog.empty() && g_volumeLog.empty());

            // StopMission: the sounds off and released (the wheels', the others, the chuffs from the
            // last set), the wagons back (their place and rotation), their boxes out, the wheels and
            // the path gone.
            const auto sounds0 = loadedSounds(t);
            for (size_t i = 0; i < made.size(); ++i) {
                made[i].frame->m_localMatrix.m[3][0] += 5.0f;
                made[i].frame->m_rot = S_quat(rnd(0.5f, 1), rnd(-0.5f, 0.5f), rnd(-0.5f, 0.5f), rnd(-0.5f, 0.5f));
            }
            t->m_path.push_back(nullptr);
            t->m_lastMarked = 4;
            t->m_firstMarked = 2;
            t->m_chuffSet = 2;
            std::vector<S_FrameVolume *> volumesBefore;
            for (const auto *w : t->m_wagons)
                volumesBefore.push_back(w->volume);
            reset();
            t->StopMission(pass == 1);
            std::vector<std::string> stopLog;
            std::vector<std::pair<const I3D_frame *, char>> offs;
            for (const auto &s : sounds0) {
                stopLog.push_back("release " + ItName(s.first));
                offs.push_back({s.first, 'o'});
            }
            bool restored = true;
            for (size_t i = made.size(); i-- > 0;) {
                stopLog.push_back("update " + ItName(made[i].frame));
                const C_Train::S_Wagon *w = t->m_wagons[i];
                restored = restored && RbSame(made[i].frame->GetPos(), S_vector(w->savedPos.x, w->savedPos.y, w->savedPos.z)) &&
                           SaSameQuat(made[i].frame->m_rot, SaNormalized(w->savedRot)) && !w->volume &&
                           (made[i].frame->m_frameFlags & 0x40000008u) == 0x40000008u;
            }
            bool removed = g_volumeLog.size() == 3;
            for (size_t k = 0; removed && k < 3; ++k)
                removed = g_volumeLog[k].op == 'R' && g_volumeLog[k].volume == &volumesBefore[2 - k]->volume;
            MA(r.trainStart, !t->m_started && noSounds(t) && t->m_chuffSet == -1 && t->m_wheels.empty() && t->m_path.empty() &&
                                 t->m_lastMarked == -1 && t->m_firstMarked == -1 && restored && removed &&
                                 g_deactivations == std::vector<bool>{pass == 1});
            MA(r.trainSounds, SaLogIs(stopLog) && g_soundLog == offs);
            // Not started: only the deactivation.
            reset();
            t->StopMission(true);
            MA(r.trainStart, g_deactivations == std::vector<bool>{true} && g_itLog.empty() && g_volumeLog.empty());
        }

        // RestartMission: stopped and started (the server's side).
        {
            t->StartMission(true);
            reset();
            t->RestartMission(false);
            MA(r.trainStart, t->m_started && g_deactivations == std::vector<bool>{true} &&
                                 g_activations == std::vector<bool>{true} && !t->m_wheels.empty());
            t->StopMission(true);
        }
        // A wheel sound already there is kept (not loaded again).
        {
            auto *a = static_cast<C_Train *>(creator->Create());
            a->SetFrame(model);
            const S_SaBytes in = wrap({chunkOf(1, valueOf(3)), chunkOf(10, textOf("ma.w0")), chunkOf(10, textOf("ma.w1"))});
            S_SaReader rd(in);
            a->ReadProperties(rd.c);
            const std::vector<S_MaWagon> m = makeWagons(a);
            sceneOf(m, -1);
            I3D_sound *kept = static_cast<I3D_sound *>(MakeSound());
            a->m_wagons[1]->wheelSound = kept;
            reset();
            a->StartMission(true);
            int made = 0;
            for (const std::string &x : g_itLog)
                made += x == "createframe 4";
            MA(r.trainSounds, a->m_wagons[1]->wheelSound == kept && !a->m_wagons[1]->wheelSoundFrame &&
                                  a->m_wagons[0]->wheelSound && made == 23);
            a->m_wagons[1]->wheelSound = nullptr;
            a->StopMission(true);
            creator->Destroy(a);
        }

        // Without the sound driver: each sound made and released, none kept; CreateFrame failing after 5
        // frames: the first five loaded, then none (Fix: nothing released through null).
        for (int c = 0; c < 2; ++c) {
            auto *a = static_cast<C_Train *>(creator->Create());
            a->SetFrame(model);
            const S_SaBytes in = wrap({chunkOf(1, valueOf(3)), chunkOf(10, textOf("ma.w0")), chunkOf(10, textOf("ma.w1")),
                                       chunkOf(10, textOf("ma.w2"))});
            S_SaReader rd(in);
            a->ReadProperties(rd.c);
            const std::vector<S_MaWagon> m = makeWagons(a);
            sceneOf(m, -1);
            g_maSoundOn = c == 1;
            g_maFramesLeft = c == 1 ? 5 : -1;
            const int firstSound = g_maSoundsMade;
            reset();
            a->StartMission(true);
            g_maSoundOn = true;
            g_maFramesLeft = -1;
            std::vector<std::string> log;
            for (int k = 0; k < 25; ++k) {
                log.push_back("createframe 4");
                if (c == 0) {
                    log.push_back("release snd" + std::to_string(firstSound + k));
                } else if (k < 5) {
                    const std::string name = "snd" + std::to_string(firstSound + k);
                    log.push_back("update " + name);
                    log.push_back("register " + name + " 0");
                    if (k < 3) {
                        const S_SoundFrame *f = g_maSoundFrames.back();
                        for (const S_SoundFrame *g : g_maSoundFrames)
                            if (ItName(g->sound) == name)
                                f = g;
                        log.push_back("update " + name);
                        char text[96];
                        std::snprintf(text, sizeof text, "sectorfactor %s %.9g 0", name.c_str(), f->sectorFactor);
                        log.push_back(text);
                    }
                }
            }
            std::vector<std::string> actual;
            for (const std::string &s : g_itLog)
                if (s.compare(0, 6, "create") == 0 || s.compare(0, 7, "release") == 0 || s.compare(0, 4, "snd") == 0 ||
                    (s.compare(0, 7, "update ") == 0 && s.compare(7, 3, "snd") == 0) || s.compare(0, 8, "register") == 0 ||
                    s.compare(0, 6, "sector") == 0)
                    actual.push_back(s);
            const auto s = loadedSounds(a);
            bool loaded = true;
            for (size_t k = 0; k < s.size(); ++k)
                loaded = loaded && ((c == 1 && k < 5) ? s[k].first != nullptr && s[k].second != nullptr
                                                      : s[k].first == nullptr && s[k].second == nullptr);
            MA(r.trainSounds, a->m_started && loaded && actual == log && a->m_chuffSets[0].count == 0 &&
                                  a->m_chuffSets[3].count == 0 && a->m_chuffSet == -1);
            // (Stopped: only the loaded ones released.)
            reset();
            a->StopMission(true);
            int releases = 0;
            for (const std::string &x : g_itLog)
                releases += x.compare(0, 7, "release") == 0;
            MA(r.trainSounds, releases == (c == 1 ? 5 : 0) && noSounds(a));
            creator->Destroy(a);
        }

        // --- the track, the path, the movement ---------------------------------------------------------------
        // The inline normalisation (from the asm): the square as summed by the caller; a NaN component
        // counts as not 0 and not negative.
        const auto normalizeBy = [](S_vector &v, float sq) {
            if (std::fabs(sq - 1.0f) < 1e-08f)
                return;
            if (!(sq < 1e-08f)) {
                const float k = 1.0f / std::sqrt(sq);
                v = S_vector(v.x * k, v.y * k, v.z * k);
            } else if (v.x != 0.0f) {
                v.x = v.x < 0.0f ? -1.0f : 1.0f;
            } else if (v.z != 0.0f) {
                v.z = v.z < 0.0f ? -1.0f : 1.0f;
            } else {
                v.y = v.y < 0.0f ? -1.0f : 1.0f;
            }
        };
        const auto line = [](const char *format, auto... args) {
            char text[256];
            std::snprintf(text, sizeof text, format, args...);
            return std::string(text);
        };
        // A track of 24 keys 5 m apart (a slight curve; faster up to key 12, then slower), the
        // checkpoints near its keys in a chain (P3 also linked to a point beside it, P6 to one 37° off,
        // P9 to one behind), the model frame (the train's own: the crossings' place) near its start.
        alignas(16) static uint8_t networkMemory[sizeof(C_CheckpointNetwork)];
        std::memset(networkMemory, 0, sizeof networkMemory);
        auto &network = *reinterpret_cast<C_CheckpointNetwork *>(networkMemory);
        GetGame()->m_checkpoints = reinterpret_cast<C_Checkpoints *>(&network);
        constexpr int KEYS = 24;
        static S_Checkpoint points[KEYS + 4];
        static S_CheckpointLink links[KEYS + 4][4];
        network.m_points = points;
        network.m_pointCount = KEYS + 4;
        static int trackRuns = 0;                           // (a new track each model)
        const std::string trackName = "ma.track" + std::to_string(trackRuns++);
        g_trackModule.AddTrack(trackName.c_str());
        C_Track *track = g_trackModule.FindTrack(trackName.c_str());
        const S_vector start(rnd(-20, 20), rnd(0, 2), rnd(-20, 20));
        S_quat noTurn;
        noTurn.Identity();
        for (int k = 0; k < KEYS; ++k)
            track->AddKey(start + S_vector(5.0f * k, 0.1f * std::sin(k * 0.7f), 1.5f * std::sin(k * 0.3f)), noTurn,
                          k < 12 ? 4.0f + k : 16.0f - (k - 12) * 1.1f);
        const auto setPoint = [&](int i, const S_vector &pos, std::initializer_list<int> to) {
            points[i].pos = pos;
            points[i].flags = static_cast<uint16_t>(to.size());
            points[i].links = links[i];
            int n = 0;
            for (int target : to)
                links[i][n++] = {static_cast<uint16_t>(target), 10};
        };
        const auto keyPos = [&](int k) { return track->GetKey(k).pos; };
        for (int k = 0; k < KEYS; ++k) {
            const S_vector p = keyPos(k) + S_vector(0.3f, 0.1f, -0.2f);
            if (k == 0)
                setPoint(k, p, {1});
            else if (k == KEYS - 1)
                setPoint(k, p, {k - 1});
            else if (k == 3)
                setPoint(k, p, {2, KEYS, 4});
            else if (k == 6)
                setPoint(k, p, {5, 7, KEYS + 1});
            else if (k == 9)
                setPoint(k, p, {KEYS + 2, 8, 10});
            else
                setPoint(k, p, {k - 1, k + 1});
        }
        setPoint(KEYS, points[3].pos + S_vector(0.0f, 0.0f, 6.0f), {3});
        setPoint(KEYS + 1, points[6].pos + S_vector(4.0f, 0.0f, 3.0f), {6});
        setPoint(KEYS + 2, points[9].pos + S_vector(-5.0f, 0.0f, 0.5f), {9});
        setPoint(KEYS + 3, S_vector(1e6f, 1e6f, 1e6f), {});   // (used by the path cases)
        model->m_localMatrix.m[3][0] = keyPos(3).x;
        model->m_localMatrix.m[3][1] = keyPos(3).y + 1.0f;
        model->m_localMatrix.m[3][2] = keyPos(3).z + 0.5f;

        // The crossheads' shared state (train.cpp's) as the oracle follows it: every wheel update of
        // the test goes through expectMove.
        static float crossZ = 0.0f;
        static bool crossForward = false;
        struct S_MaMove {
            bool none = false, moved = false, cut = false;
            std::vector<std::string> log;
            std::vector<std::pair<const I3D_frame *, char>> sounds;
            float speed = 0.0f, fade = 0.0f;
            int set = -1, brakeTimer = 0, jarTimer = 0, key = 0, lastMarked = -1, firstMarked = -1;
            int next[4] = {};
            std::vector<uint16_t> flags;
            std::vector<std::pair<I3D_frame *, S_quat>> rots;
            std::vector<std::pair<I3D_frame *, S_matrix>> locals;
            std::vector<const C_Train::S_Wagon *> placed;
            float wheelVolume = 0.0f;
            bool loopSet = false;
            float loopVolume = 0.0f;
            float crossZ = 0.0f;
            bool crossForward = false;
        };
        const auto placeLog = [&](S_MaMove &e, I3D_frame *frame, I3D_frame *body, const S_vector &pos, const S_vector &dir,
                                  float phase, int time) {
            const float sway = phase == 0.0f ? 0.005f : 0.0085f;
            e.log.push_back(line("setdir %s %s 0", ItName(frame).c_str(), ItV(dir).c_str()));
            const float angle = static_cast<float>(static_cast<double>(time) * 0.0025f) + phase;
            const float roll = static_cast<float>(std::sin(static_cast<double>(angle))) * sway;
            if (body)
                e.log.push_back(line("setdir %s %s %.9g", ItName(body).c_str(), ItV(dir).c_str(), roll));
            e.log.push_back("update " + ItName(frame));
            // (Fix: without its body the frame goes to the place itself.)
            const S_vector fp = frame->GetWorldPos(), bp = body ? body->GetWorldPos() : fp;
            const S_vector np(fp.x - bp.x + pos.x, fp.y - bp.y + pos.y, fp.z - bp.z + pos.z);
            e.log.push_back(line("setpos %s %s", ItName(frame).c_str(), ItV(np).c_str()));
            e.log.push_back("update " + ItName(frame));
        };
        // UpdateWheels (from the asm): each wheel turned by distance / radius; its rod along its parent's
        // z axis; the connecting rod from its pin to the slide (the wheel's local matrix rebuilt from its
        // new rotation), a turn of the first one's crosshead a chuff; the crosshead along the parent's z.
        const auto wheelsExpect = [&](C_Train *a, float distance, S_MaMove &e) {
            bool checked = false;
            for (size_t i = a->m_wheels.size(); i-- > 0;) {
                const C_Train::S_Wheel &w = a->m_wheels[i];
                const S_quat rot = SaNormalized(w.wheel->GetRot().RotateByMatrix(RotateXMatrix(distance / w.radius)));
                e.rots.push_back({w.wheel, rot});
                e.log.push_back("update " + ItName(w.wheel));
                const S_matrix &parent = w.wheel->m_parent->GetMatrix();
                if (w.rod) {
                    S_vector z(parent.m[2][0], parent.m[2][1], parent.m[2][2]);
                    normalizeBy(z, (z.z * z.z + z.y * z.y) + z.x * z.x);
                    e.log.push_back(line("setdir %s %s 0", ItName(w.rod).c_str(), ItV(z).c_str()));
                }
                if (!w.connectingRod || !w.crosshead)
                    continue;
                const S_vector end = w.crosshead->GetPos();
                const float rodLength = RbLength((end.x * end.x + end.y * end.y) + end.z * end.z);
                S_matrix space = w.wheel->m_localMatrix;
                space.SetRot3(rot);
                e.locals.push_back({w.wheel, space});
                S_vector pin = w.connectingRod->GetPos();
                pin *= space;
                const float dy = pin.y - a->m_crossheadHeight;
                S_vector slide(pin.x, a->m_crossheadHeight, std::sqrt(rodLength * rodLength - dy * dy) + pin.z);
                if (!checked) {
                    bool chuff = false;
                    if (e.crossZ < slide.z && !e.crossForward) {
                        e.crossForward = true;
                        chuff = true;
                    } else if (e.crossZ > slide.z && e.crossForward) {
                        e.crossForward = false;
                        chuff = true;
                    }
                    if (chuff && a->m_chuffSet != -1 && a->m_chuffSets[a->m_chuffSet].count != 0) {
                        int &n = e.next[a->m_chuffSet];
                        if (++n >= a->m_chuffSets[a->m_chuffSet].count)
                            n = 0;
                        if (I3D_sound *s = a->m_chuffSets[a->m_chuffSet].sounds[n]) {
                            e.sounds.push_back({s, 'P'});
                            e.sounds.push_back({s, 'O'});
                        }
                    }
                    checked = true;
                    e.crossZ = slide.z;
                }
                S_matrix inv = space;
                inv.Inverse(S_matrix(space));
                slide *= inv;
                S_vector dir = slide - w.connectingRod->GetPos();
                normalizeBy(dir, (dir.z * dir.z + dir.y * dir.y) + dir.x * dir.x);
                S_vector up(0.0f, 1.0f, 0.0f);
                up *= inv;
                S_matrix rodLocal = w.connectingRod->m_localMatrix;
                rodLocal.SetDir3(dir, up);
                e.locals.push_back({w.connectingRod, rodLocal});
                e.log.push_back("update " + ItName(w.connectingRod));
                S_vector z(parent.m[2][0], parent.m[2][1], parent.m[2][2]);
                normalizeBy(z, (z.x * z.x + z.y * z.y) + z.z * z.z);
                e.log.push_back(line("setdir %s %s 0", ItName(w.crosshead).c_str(), ItV(z).c_str()));
                e.log.push_back("update " + ItName(w.crosshead));
            }
        };
        const auto expectMove = [&](C_Train *a, uint32_t time, bool force) {
            S_MaMove e;
            e.speed = a->m_speed;
            e.set = a->m_chuffSet;
            e.brakeTimer = a->m_brakeTimer;
            e.jarTimer = a->m_jarTimer;
            e.fade = a->m_brakeFade;
            e.key = a->m_player.GetKeyIndex();
            e.lastMarked = a->m_lastMarked;
            e.firstMarked = a->m_firstMarked;
            for (int k = 0; k < 4; ++k)
                e.next[k] = a->m_chuffSets[k].next;
            e.crossZ = crossZ;
            e.crossForward = crossForward;
            for (const S_Checkpoint *p : a->m_path)
                e.flags.push_back(p->flags);
            C_Train::S_Wagon *front = a->m_wagons.empty() ? nullptr : a->m_wagons[0];
            if (!a->m_started || !front || !front->frame || !front->body || !a->m_player.GetTrack() ||
                a->m_player.GetTrack()->NumKeys() < 2) {
                e.none = true;
                return e;
            }
            auto player = std::make_unique<C_TrackPlayer>(a->m_player);
            bool decelerating = false;
            const S_vector bodyPos = front->body->GetWorldPos();
            const S_vector next = player->AdvanceAlongKeys(bodyPos, static_cast<int>(time), &decelerating);
            e.key = player->GetKeyIndex();
            const S_vector d = next - bodyPos;
            const float distance = RbLength((d.x * d.x + d.z * d.z) + d.y * d.y);
            const float speed = time != 0 ? distance / (static_cast<float>(time) * 0.001f) * 3.6f : 0.0f;
            e.moved = distance > 0.001f || force;
            if (e.moved) {
                wheelsExpect(a, distance, e);
                int key = 0;
                S_vector rear = player->FindPointAtDistance(player->GetKeyIndex(), next, front->length, &key);
                S_vector dir = next - rear;
                normalizeBy(dir, (dir.x * dir.x + dir.z * dir.z) + dir.y * dir.y);
                placeLog(e, front->frame, front->body, next, dir, 0.0f, static_cast<int>(a->m_time));
                e.placed.push_back(front);
                for (size_t i = 1; i < a->m_wagons.size(); ++i) {
                    const S_vector coupling = player->FindPointAtDistance(key, rear, a->m_wagons[i - 1]->gap, &key);
                    const C_Train::S_Wagon *w = a->m_wagons[i];
                    if (!w->frame) {
                        e.cut = true;
                        return e;
                    }
                    rear = player->FindPointAtDistance(key, coupling, w->length, &key);
                    S_vector heading = coupling - rear;
                    normalizeBy(heading, (heading.z * heading.z + heading.y * heading.y) + heading.x * heading.x);
                    placeLog(e, w->frame, w->body, coupling, heading, static_cast<float>(static_cast<int>(i)) * 3.14159274f,
                             static_cast<int>(a->m_time));
                    e.placed.push_back(w);
                }
                int set = 0;
                for (int k = 4; k-- > 0;)
                    if (a->m_chuffSets[k].minSpeed < speed) {
                        set = k;
                        break;
                    }
                const auto sector = [&](I3D_sound *s, S_SoundFrame *f) {
                    e.log.push_back(line("sectorfactor %s %.9g %.9g", ItName(s).c_str(), f->sectorFactor, f->baseVolume));
                };
                if (decelerating && a->m_brakeLoop) {
                    if (!a->m_brakeLoop->IsOn()) {
                        e.loopSet = true;
                        e.loopVolume = 1.0f;
                        e.sounds.push_back({a->m_brakeLoop, 'V'});
                        sector(a->m_brakeLoop, a->m_brakeLoopFrame);
                        e.sounds.push_back({a->m_brakeLoop, 'O'});
                        e.fade = 0.0f;
                    }
                    e.brakeTimer -= static_cast<int>(time);
                    if (e.brakeTimer <= 0) {
                        e.brakeTimer += std::rand() % 2000 + 1500;
                        if (a->m_brake) {
                            e.sounds.push_back({a->m_brake, 'P'});
                            e.sounds.push_back({a->m_brake, 'O'});
                        }
                    }
                    e.jarTimer -= static_cast<int>(time);
                    if (e.jarTimer <= 0) {
                        e.jarTimer += std::rand() % 4000 + 6000;
                        if (a->m_jar) {
                            e.sounds.push_back({a->m_jar, 'P'});
                            e.sounds.push_back({a->m_jar, 'O'});
                        }
                    }
                } else if (a->m_brakeLoop && e.fade <= 1.0f) {
                    e.fade = static_cast<float>(time) * 0.0005f + e.fade;
                    if (e.fade > 1.0f) {
                        e.fade = 1.0f;
                        e.sounds.push_back({a->m_brakeLoop, 'o'});
                    } else {
                        e.loopSet = true;
                        e.loopVolume = 1.0f - e.fade;
                        e.sounds.push_back({a->m_brakeLoop, 'V'});
                        sector(a->m_brakeLoop, a->m_brakeLoopFrame);
                    }
                }
                e.set = set;
            }
            e.wheelVolume = speed >= 10.0f ? 0.9f : speed > 1.0f ? (speed - 1.0f) * 0.1f : 0.0f;
            for (size_t i = a->m_wagons.size(); i-- > 0;) {
                const C_Train::S_Wagon *w = a->m_wagons[i];
                if (!w->wheelSound)
                    continue;
                e.sounds.push_back({w->wheelSound, 'V'});
                e.log.push_back(line("sectorfactor %s %.9g %.9g", ItName(w->wheelSound).c_str(),
                                     w->wheelSoundFrame->sectorFactor, e.wheelVolume));
            }
            if (e.moved) {
                e.speed = speed;
                // UpdateCrossings: the points within 5 s ahead marked, those past the train unmarked.
                const S_vector at = a->GetFrame()->GetWorldPos();
                const float reach = speed * 1.38888896f;
                const float reachSquare = reach * reach;
                const size_t n = e.flags.size();
                for (size_t i = static_cast<uint32_t>(e.lastMarked) + 1; i < n; ++i) {
                    const S_vector v = at - a->m_path[i]->pos;
                    if (!((v.z * v.z + v.y * v.y) + v.x * v.x < reachSquare))
                        break;
                    e.flags[i] |= CHECKPOINT_FLAG_UNDER_CAR;
                    e.lastMarked = static_cast<int>(i);
                    if (e.firstMarked == -1)
                        e.firstMarked = static_cast<int>(i);
                }
                if (e.firstMarked != -1) {
                    const float lengthSquare = a->m_trainLength * a->m_trainLength;
                    for (size_t i = static_cast<uint32_t>(e.firstMarked); i < n; ++i) {
                        const S_vector v = at - a->m_path[i]->pos;
                        if (!((v.z * v.z + v.y * v.y) + v.x * v.x > lengthSquare))
                            break;
                        e.flags[i] = static_cast<uint16_t>(e.flags[i] & ~CHECKPOINT_FLAG_UNDER_CAR);
                        if (static_cast<size_t>(++e.firstMarked) >= n)
                            e.firstMarked = static_cast<int>(n);
                    }
                }
            }
            return e;
        };
        // After the move: the logs, the state, the wheels, the boxes (from the frames' final places),
        // the sweeps.
        const auto moveOk = [&](C_Train *a, const S_MaMove &e) {
            bool ok = SaLogIs(e.log) && g_soundLog == e.sounds && a->m_chuffSet == e.set && a->m_brakeTimer == e.brakeTimer &&
                      a->m_jarTimer == e.jarTimer && SaSame(&a->m_brakeFade, &e.fade, 4) && SaSame(&a->m_speed, &e.speed, 4) &&
                      a->m_player.GetKeyIndex() == e.key && a->m_lastMarked == e.lastMarked &&
                      a->m_firstMarked == e.firstMarked;
            for (int k = 0; k < 4; ++k)
                ok = ok && a->m_chuffSets[k].next == e.next[k];
            for (size_t i = 0; ok && i < e.flags.size(); ++i)
                ok = a->m_path[i]->flags == e.flags[i];
            for (const auto &r0 : e.rots)
                ok = ok && SaSameQuat(r0.first->m_rot, r0.second) && (r0.first->m_frameFlags & 0x40000008u) == 0x40000008u;
            for (const auto &l : e.locals)
                ok = ok && SaSame(&l.first->m_localMatrix, &l.second, 48) && RbSame(l.first->GetPos(), l.second(3)) &&
                     (l.first->m_frameFlags & FRMFLAGS_LMATRIX_VALID);
            ok = ok && g_volumeLog.size() == e.placed.size() && g_maSpheres.size() == e.placed.size();
            for (size_t i = 0; ok && i < e.placed.size(); ++i) {
                const S_FrameVolume *v = e.placed[i]->volume;
                I3D_frame *fr = v->volume.frame;
                const S_matrix world = fr->GetMatrix();
                S_matrix inv = world;
                inv.Inverse(S_matrix(world));
                const I3D_bbox box = fr->GetType() == FRAME_DUMMY ? static_cast<I3D_dummy *>(fr)->GetBBox() : fr->m_bound.bbox;
                const S_vector c((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.max.z + box.min.z) * 0.5f);
                const S_vector center = fr->GetType() == FRAME_DUMMY ? c * world : c;
                const S_vector front = S_vector(c.x, c.y, box.min.z) * world, back = S_vector(c.x, c.y, box.max.z) * world;
                const float size = (std::fabs(box.max.x - box.min.x) + std::fabs(box.max.y - box.min.y)) * 0.25f;
                const float radius = size * ((fr->m_scale.y + fr->m_scale.x) * 0.5f);
                const S_MaSphere &s = g_maSpheres[i];
                ok = g_volumeLog[i].op == 'M' && g_volumeLog[i].volume == &v->volume && SaSame(&v->invMatrix, &inv, sizeof inv) &&
                     RbSame(v->volume.center, center) && RbSame(s.from, front) && RbSame(s.dir, back - front) &&
                     SaSame(&s.radius, &radius, 4) && s.types == 0x2002 && s.flags == 0xFFFFFFFF;
            }
            if (e.loopSet)
                ok = ok && g_sounds[a->m_brakeLoop].volume == e.loopVolume;
            for (const C_Train::S_Wagon *w : a->m_wagons)
                if (w->wheelSound)
                    ok = ok && g_sounds[w->wheelSound].volume == e.wheelVolume && w->wheelSoundFrame->baseVolume == e.wheelVolume;
            return ok;
        };
        // A move with its oracle (the rand() calls replayed); the crossheads' state carried on.
        const auto checkedMove = [&](C_Train *a, uint32_t time, bool force) {
            const unsigned seedNow = static_cast<unsigned>(rnd(0, 30000));
            std::srand(seedNow);
            const S_MaMove e = expectMove(a, time, force);
            std::srand(seedNow);
            reset();
            a->Move(time, force);
            const bool ok = moveOk(a, e);
            crossZ = e.crossZ;
            crossForward = e.crossForward;
            return ok;
        };

        made = makeWagons(t);
        sceneOf(made, -1);
        t->StartMission(true);
        t->m_initialRot = S_quat(0.9f, 0.1f, -0.3f, 0.2f);
        {
            // Not running yet: Update does nothing.
            reset();
            const uint32_t time0 = t->m_time;
            t->Update(40);
            MA(r.trainMove, t->m_time == time0 && g_itLog.empty() && g_soundLog.empty());
        }
        // SetTrack (FollowTrack): the path, the first key past the train's length, the first wagon
        // there facing along its frame's local z (normalised when its local matrix is valid), then
        // Move(1000); its oracle from the state FollowTrack leaves before the move (made by hand, then
        // undone).
        const auto followCheck = [&](C_Track *trk, const char *name) {
            float length = 0.0f;
            for (size_t i = t->m_wagons.size(); i-- > 0;)
                length = length + t->m_wagons[i]->length + t->m_wagons[i]->gap;
            int key = 0;
            float covered = 0.0f;
            for (int k = 0; k < trk->NumKeys(); ++k) {
                if (k != 0) {
                    const S_vector v = trk->GetKey(k - 1).pos - trk->GetKey(k).pos;
                    covered = RbLength((v.z * v.z + v.y * v.y) + v.x * v.x) + covered;
                }
                if (covered > length) {
                    key = k;
                    break;
                }
            }
            I3D_frame *front = t->m_wagons[0]->frame;
            I3D_frame *frontBody = t->m_wagons[0]->body;
            S_vector dir(front->m_localMatrix.m[2][0], front->m_localMatrix.m[2][1], front->m_localMatrix.m[2][2]);
            normalizeBy(dir, (dir.y * dir.y + dir.z * dir.z) + dir.x * dir.x);
            S_MaMove place;
            const S_vector at = trk->GetKey(key).pos;
            placeLog(place, front, frontBody, at, dir, 0.0f, 0);
            const S_matrix frontLocal = front->m_localMatrix;
            const uint32_t frontFlags = front->m_frameFlags;
            const S_vector fp = front->GetWorldPos(), bp = frontBody->GetWorldPos();
            front->m_localMatrix(3) = S_vector(fp.x - bp.x + at.x, fp.y - bp.y + at.y, fp.z - bp.z + at.z);
            C_Track *const track0 = t->m_player.GetTrack();
            const int key0 = t->m_player.GetKeyIndex();
            t->m_player.SetTrack(trk);
            t->m_player.SetKeyIndex(key);
            t->BuildPath();
            const std::vector<S_Checkpoint *> path = t->m_path;
            const unsigned seedNow = static_cast<unsigned>(rnd(0, 30000));
            std::srand(seedNow);
            S_MaMove e = expectMove(t, 1000, false);
            front->m_localMatrix = frontLocal;
            front->m_frameFlags = frontFlags;
            t->m_player.SetTrack(track0);
            t->m_player.SetKeyIndex(key0);
            t->m_path.clear();
            e.log.insert(e.log.begin(), place.log.begin(), place.log.end());
            std::srand(seedNow);
            reset();
            t->m_running = false;
            t->SetTrack(true, name);
            const bool ok = t->m_running && t->m_player.GetTrack() == trk && t->m_path == path && moveOk(t, e);
            crossZ = e.crossZ;
            crossForward = e.crossForward;
            return ok;
        };
        {
            std::vector<S_Checkpoint *> path;
            for (int k = 0; k < KEYS; ++k)
                path.push_back(&points[k]);
            MA(r.trainMove, followCheck(track, trackName.c_str()));
            MA(r.trainPath, t->m_path == path && g_finds.size() == 1 && RbSame(g_finds[0].pos, keyPos(0)) &&
                                g_finds[0].maxDist == 5.0f && g_finds[0].minDist == -1.0f);
        }
        // Update while running: the time on, then the move (the brakes from key 12 on, where the keys
        // slow down; the end of the track: no move); forced moves of 0 ms.
        const auto checkedUpdate = [&](C_Train *a, int frameTime) {
            const uint32_t time0 = a->m_time;
            const unsigned seedNow = static_cast<unsigned>(rnd(0, 30000));
            a->m_time += static_cast<uint32_t>(frameTime);
            std::srand(seedNow);
            const S_MaMove e = expectMove(a, static_cast<uint32_t>(frameTime), false);
            a->m_time = time0;
            std::srand(seedNow);
            reset();
            a->Update(frameTime);
            const bool ok = moveOk(a, e) && a->m_time == time0 + static_cast<uint32_t>(frameTime);
            crossZ = e.crossZ;
            crossForward = e.crossForward;
            return ok;
        };
        bool sawBrake = false, sawFade = false, sawChuff = false, sawStill = false;
        const int times[] = {16, 40, 0, 100, 33, 250, 16, 500, 1000, 70, 1500, 900, 2000, 16, 2500, 3000, 40, 4000, 5000, 600};
        for (int pass = 0; pass < 3; ++pass) {
            for (int ft : times) {
                const bool loopOn = t->m_brakeLoop && t->m_brakeLoop->IsOn();
                MA(r.trainMove, checkedUpdate(t, ft));
                sawBrake = sawBrake || (!loopOn && t->m_brakeLoop->IsOn());
                sawFade = sawFade || (loopOn && !t->m_brakeLoop->IsOn());
                sawStill = sawStill || t->m_speed == 0.0f;
                for (const auto &s : g_soundLog)
                    sawChuff = sawChuff || (s.second == 'P' && s.first != t->m_brake && s.first != t->m_jar);
                if (ft == 16)
                    MA(r.trainMove, checkedMove(t, 0, true));
            }
            // Back to the start for the next pass (the chuff set kept).
            t->m_player.SetKeyIndex(0);
        }
        MA(r.trainMove, sawBrake && sawFade && sawChuff);
        (void)sawStill;

        // FollowTrack's facing: its frame's local z row scaled (normalised), tiny (a square below 1e-8:
        // the unit axis along x, else z, else y by its sign; above: scaled), zero; a track the train
        // only fills at its last key (placed there, no move: Move(1000) is not forced); SetTrack(true,
        // none) follows the same track again.
        {
            I3D_frame *front = t->m_wagons[0]->frame;
            const S_matrix local0 = front->m_localMatrix;
            const S_vector rows[] = {S_vector(0.0f, 0.0f, 2.5f), S_vector(1e-5f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, -1e-5f),
                                     S_vector(0.0f, 0.0f, 0.0f), S_vector(0.003f, 0.0f, 0.004f),
                                     S_vector(0.0f, -1e-5f, 0.0f)};
            for (const S_vector &row : rows) {
                front->m_localMatrix(2) = row;
                MA(r.trainPath, followCheck(track, trackName.c_str()));
                front->m_localMatrix = local0;
            }
            const std::string endName = trackName + ".end";
            if (!g_trackModule.FindTrack(endName.c_str())) {
                g_trackModule.AddTrack(endName.c_str());
                C_Track *et = g_trackModule.FindTrack(endName.c_str());
                et->AddKey(keyPos(0), noTurn, 5.0f);
                et->AddKey(keyPos(0) + S_vector(20.0f, 0.0f, 0.0f), noTurn, 5.0f);
                et->AddKey(keyPos(0) + S_vector(40.5f, 0.0f, 0.0f), noTurn, 5.0f);
            }
            C_Track *endTrack = g_trackModule.FindTrack(endName.c_str());
            MA(r.trainPath, followCheck(endTrack, endName.c_str()) && t->m_player.GetKeyIndex() == 2);
            MA(r.trainPath, followCheck(endTrack, nullptr));
            t->m_player.SetTrack(track);
            t->m_player.SetKeyIndex(3);
            t->BuildPath();
        }
        // More moves: a wagon without its wheel sound (the others' still set); the brake and jar timers
        // running out exactly (0: they go off); no brake loop; the actor frame's local matrix rebuilt from
        // its rotation (taken and dropped); slow tracks (10.4 and 11.7 km/h: the wheels' volume and the
        // chuff set).
        {
            I3D_sound *wheel1 = t->m_wagons[1]->wheelSound;
            t->m_wagons[1]->wheelSound = nullptr;
            MA(r.trainMove, checkedMove(t, 300, false));
            t->m_wagons[1]->wheelSound = wheel1;
            t->m_player.SetKeyIndex(14);
            t->m_brakeTimer = 40;
            t->m_jarTimer = 40;
            const int plays0 = g_sounds[t->m_brake].plays, jars0 = g_sounds[t->m_jar].plays;
            MA(r.trainMove, checkedMove(t, 40, false) && g_sounds[t->m_brake].plays == plays0 + 1 &&
                                g_sounds[t->m_jar].plays == jars0 + 1);
            I3D_sound *loop = t->m_brakeLoop;
            t->m_brakeLoop = nullptr;
            t->m_brakeFade = 0.25f;
            t->m_player.SetKeyIndex(2);
            MA(r.trainMove, checkedMove(t, 300, false) && t->m_brakeFade == 0.25f);
            t->m_brakeLoop = loop;
            I3D_frame *own = t->GetFrame();
            own->m_frameFlags &= ~FRMFLAGS_LMATRIX_VALID;
            own->m_rot = S_quat(0.8f, 0.3f, -0.2f, 0.1f);
            S_quat rot = own->m_rot;
            rot.Normalize();
            S_matrix rebuilt = own->m_localMatrix;
            rebuilt.SetRot3(rot);
            MA(r.trainMove, checkedMove(t, 300, true) && (own->m_frameFlags & FRMFLAGS_LMATRIX_VALID) &&
                                SaSame(&own->m_localMatrix, &rebuilt, sizeof rebuilt));
            for (float v : {2.9f, 3.25f}) {
                const std::string slowName = trackName + ".slow" + std::to_string(static_cast<int>(v * 100));
                if (!g_trackModule.FindTrack(slowName.c_str())) {
                    g_trackModule.AddTrack(slowName.c_str());
                    for (int k = 0; k < 6; ++k)
                        g_trackModule.FindTrack(slowName.c_str())->AddKey(keyPos(k), noTurn, v);
                }
                t->m_player.SetTrack(g_trackModule.FindTrack(slowName.c_str()));
                t->m_player.SetKeyIndex(0);
                for (int n = 0; n < 4; ++n)
                    MA(r.trainMove, checkedMove(t, 250, false));
                const float kmh = v * 3.6f;
                MA(r.trainMove, t->m_speed > kmh - 0.2f && t->m_speed < kmh + 0.2f);
                // (1 ms: a move of about 3 mm, above 0.001 m: moved.)
                MA(r.trainMove, checkedMove(t, 1, false) && t->m_speed > kmh - 0.5f);
            }
            t->m_player.SetTrack(track);
            t->m_player.SetKeyIndex(3);
        }
        // UpdateCrossings by itself: from the point after the last marked, those within 5 s at the speed
        // marked, up to the first out of reach; from the first marked, those farther than the train's
        // length unmarked, the first marked moving on (at most to the end).
        {
            const S_vector at = t->GetFrame()->GetWorldPos();
            static S_Checkpoint cross[5];
            const float far1[] = {1.0f, 50.0f, 2.0f, 3.0f, 60.0f};
            const std::vector<S_Checkpoint *> keep = t->m_path;
            t->m_path.clear();
            for (int i = 0; i < 5; ++i) {
                cross[i].pos = at + S_vector(far1[i] * 0.6f, 0.0f, far1[i] * 0.8f);
                cross[i].flags = 0;
                t->m_path.push_back(&cross[i]);
            }
            t->m_speed = 7.2f;                          // (10 m)
            t->m_lastMarked = -1;
            t->m_firstMarked = -1;
            t->UpdateCrossings();
            MA(r.trainPath, cross[0].flags == CHECKPOINT_FLAG_UNDER_CAR && cross[1].flags == 0 && cross[2].flags == 0 &&
                                t->m_lastMarked == 0 && t->m_firstMarked == 0);
            const float far2[] = {40.0f, 45.0f, 2.0f, 50.0f, 55.0f};
            for (int i = 0; i < 5; ++i) {
                cross[i].pos = at + S_vector(far2[i] * 0.6f, 0.0f, far2[i] * 0.8f);
                cross[i].flags = CHECKPOINT_FLAG_UNDER_CAR | 3;
            }
            t->m_lastMarked = 4;
            t->m_firstMarked = 0;
            t->UpdateCrossings();
            MA(r.trainPath, cross[0].flags == 3 && cross[1].flags == 3 && cross[2].flags == (CHECKPOINT_FLAG_UNDER_CAR | 3) &&
                                t->m_firstMarked == 2 && t->m_lastMarked == 4);
            cross[2].pos = at + S_vector(30.0f, 0.0f, 40.0f);
            t->UpdateCrossings();
            MA(r.trainPath, cross[2].flags == 3 && cross[4].flags == 3 && t->m_firstMarked == 5);
            t->m_path = keep;
            t->m_lastMarked = -1;
            t->m_firstMarked = -1;
        }

        // Running a human over: a sweep meeting a human's frame (flags 0xC0) hits it (5, -1); not
        // with other flags, without the frame, its data or an actor that is not a human.
        {
            C_Human human;
            I3D_frame *humanFrame = AnFrame(FRAME_MODEL, "ma.human", nullptr);
            S_FrameData data = {};
            data.actor = &human;
            humanFrame->SetData(&data);
            S_CollisionVolume hit;
            g_maSphereHit = &hit;
            C_Human other;                              // (a human by its class, another actor by its type)
            other.m_type = ACTOR_SUBMARINE;
            for (int c = 0; c < 7; ++c) {
                hit.flags = c == 1 ? 0x30u : c == 2 ? 0x80u : 0x40u;
                hit.frame = c == 3 ? nullptr : humanFrame;
                humanFrame->SetData(c == 4 ? nullptr : &data);
                data.actor = c == 5 ? static_cast<C_Actor *>(t) : c == 6 ? static_cast<C_Actor *>(&other) : &human;
                human.hits.clear();
                other.hits.clear();
                g_maSphereHitAt = c % 3;
                t->m_player.SetKeyIndex(2);
                MA(r.trainMove, checkedMove(t, 300, true));
                const bool hits = c == 0 || c == 2;
                MA(r.trainMove, other.hits.empty() && human.hits.size() == (hits ? 1u : 0u) &&
                                    (!hits || (human.hits[0].type == HIT_THROWN_OBJECT && human.hits[0].amount == -1.0f &&
                                               RbSame(human.hits[0].pos, S_vector(0, 0, 0)) &&
                                               RbSame(human.hits[0].dir, S_vector(0, 0, 0)) && !human.hits[0].attacker &&
                                               human.hits[0].flags == 0)));
            }
            g_maSphereHitAt = -1;
            humanFrame->SetData(nullptr);
        }
        // A wagon without its body (Fix: placed by its frame); a wagon without its frame: the move
        // stops there (quirk kept: nothing after it, not even the sounds and the checkpoints).
        {
            I3D_frame *body1 = t->m_wagons[1]->body;
            t->m_wagons[1]->body = nullptr;
            t->m_player.SetKeyIndex(1);
            MA(r.trainMove, checkedMove(t, 500, false));
            t->m_wagons[1]->body = body1;
            I3D_frame *frame2 = t->m_wagons[2]->frame;
            t->m_wagons[2]->frame = nullptr;
            const unsigned seedNow = static_cast<unsigned>(rnd(0, 30000));
            std::srand(seedNow);
            S_MaMove e = expectMove(t, 400, false);
            std::srand(seedNow);
            reset();
            const float speed0 = t->m_speed;
            const int set0 = t->m_chuffSet;
            t->Move(400, false);
            crossZ = e.crossZ;
            crossForward = e.crossForward;
            MA(r.trainMove, e.cut && SaLogIs(e.log) && t->m_speed == speed0 && t->m_chuffSet == set0 &&
                                g_volumeLog.size() == 2 && g_maSpheres.size() == 2);
            t->m_wagons[2]->frame = frame2;
        }
        // Nothing moves (Fix where the original reads through null or past the keys): not started, no
        // first wagon's frame or body, no track or one of a single key; no wagons.
        for (int c = 0; c < 6; ++c) {
            C_Train::S_Wagon *front = t->m_wagons[0];
            I3D_frame *frame0 = front->frame, *body0 = front->body;
            C_Track *track0 = t->m_player.GetTrack();
            const std::string single = trackName + ".1";
            if (c == 5 && !g_trackModule.FindTrack(single.c_str())) {
                g_trackModule.AddTrack(single.c_str());
                g_trackModule.FindTrack(single.c_str())->AddKey(keyPos(0), noTurn, 5.0f);
            }
            std::vector<C_Train::S_Wagon *> wagons0;
            if (c == 0)
                t->m_started = false;
            if (c == 1)
                front->frame = nullptr;
            if (c == 2)
                front->body = nullptr;
            if (c == 3)
                t->m_player.SetTrack(nullptr);
            if (c == 4)
                wagons0.swap(t->m_wagons);
            if (c == 5)
                t->m_player.SetTrack(g_trackModule.FindTrack(single.c_str()));
            reset();
            const float speed0 = t->m_speed;
            t->Move(300, true);
            MA(r.trainMove, g_itLog.empty() && g_soundLog.empty() && g_volumeLog.empty() && t->m_speed == speed0);
            t->m_started = true;
            front->frame = frame0;
            front->body = body0;
            t->m_player.SetTrack(track0);
            if (c == 4)
                wagons0.swap(t->m_wagons);
        }

        // Start: the near whistle when the camera is within 48 m of the train's frame, else the far
        // one; nothing without a scene, a camera or the whistle.
        {
            uint8_t *scene = reinterpret_cast<uint8_t *>(g_carScene);
            I3D_frame *const camera0 = *reinterpret_cast<I3D_frame **>(scene + 0x15C);
            I3D_frame *camera = AnFrame(FRAME_CAMERA, "ma.camera", nullptr);
            *reinterpret_cast<I3D_frame **>(scene + 0x15C) = camera;
            const S_vector at = model->GetWorldPos();
            const float distances[] = {10.0f, 47.99f, 48.0f, 48.01f, 300.0f};
            for (int c = 0; c < 8; ++c) {
                const float d = distances[c % 5];
                camera->m_localMatrix(3) = at + S_vector(d * 0.6f, 0.0f, d * 0.8f);
                I3D_sound *near0 = t->m_whistleNear;
                if (c == 7)
                    t->m_whistleNear = nullptr;
                if (c == 6)
                    *reinterpret_cast<I3D_frame **>(scene + 0x15C) = nullptr;
                if (c == 5)
                    GetGame()->m_scene = nullptr;
                const S_vector v = camera->GetWorldPos() - at;
                const bool inRange = RbLength((v.z * v.z + v.y * v.y) + v.x * v.x) <= 48.0f;
                reset();
                g_sounds[t->m_whistleFar].playAt = -1;
                t->Start();
                I3D_sound *s = inRange ? t->m_whistleNear : t->m_whistleFar;
                const bool plays = c < 5 || (c == 7 && !inRange);
                MA(r.trainSounds, plays ? g_soundLog == std::vector<std::pair<const I3D_frame *, char>>{{s, 'P'}, {s, 'O'}} &&
                                              g_sounds[s].playAt == 0
                                        : g_soundLog.empty());
                t->m_whistleNear = near0;
                GetGame()->m_scene = g_carScene;
                *reinterpret_cast<I3D_frame **>(scene + 0x15C) = camera;
            }
            *reinterpret_cast<I3D_frame **>(scene + 0x15C) = camera0;
        }

        // PlayChuff: the current set's next sound (from the one after the last, round its count); none
        // without a set or sounds; a missing sound in the set plays nothing (quirk kept: the count is of
        // the loaded ones).
        {
            t->m_chuffSets[2].count = 3;
            I3D_sound *gone = t->m_chuffSets[2].sounds[1];
            t->m_chuffSets[2].sounds[1] = nullptr;
            t->m_chuffSets[1].count = 0;
            for (int c = 0; c < 9; ++c) {
                const int set = c < 6 ? 2 : c == 6 ? -1 : c == 7 ? 1 : 3;
                t->m_chuffSet = set;
                int next = set >= 0 ? t->m_chuffSets[set].next : 0;
                std::vector<std::pair<const I3D_frame *, char>> e;
                if (set != -1 && t->m_chuffSets[set].count != 0) {
                    if (++next >= t->m_chuffSets[set].count)
                        next = 0;
                    if (I3D_sound *s = t->m_chuffSets[set].sounds[next])
                        e = {{s, 'P'}, {s, 'O'}};
                }
                reset();
                t->PlayChuff();
                MA(r.trainSounds, g_soundLog == e && (set < 0 || t->m_chuffSets[set].next == next));
            }
            t->m_chuffSets[2].sounds[1] = gone;
            t->m_chuffSets[2].count = 4;
            t->m_chuffSets[1].count = 4;
        }

        // The saved game: running, chunk 1000 (the track's name and its length with the end, the key,
        // the wagons' places: one without its frame 0, Fix); else 2000, empty.
        {
            const auto saveBytes = [&](C_Train *a, const std::string &name, bool running) {
                S_SaBytes body;
                if (running) {
                    body.Val(static_cast<uint16_t>(name.size() + 1));
                    body.Put(name.c_str(), name.size() + 1);
                    body.Val(a->m_player.GetKeyIndex()).Val(static_cast<int>(a->m_wagons.size()));
                    for (const C_Train::S_Wagon *w : a->m_wagons) {
                        const S_vector p = w->frame ? w->frame->GetWorldPos() : S_vector(0, 0, 0);
                        body.Put(&p, 12);
                    }
                }
                // (The base's chunk first: the harness's C_Actor::SaveGame writes 0x3039 with its tag.)
                S_SaBytes sv;
                sv.Head(0x777, static_cast<uint32_t>(22 + body.b.size())).Head(0x3039, 10).Val(g_actorSaveTag)
                    .Head(running ? 1000 : 2000, static_cast<uint32_t>(6 + body.b.size()));
                sv.Put(body.b.data(), body.b.size());
                return sv.b;
            };
            bool saved = false;
            t->m_player.SetKeyIndex(7);
            MA(r.trainSave, SaWritten([&](C_chunk &c) { return t->SaveGame(c); }, &saved) == saveBytes(t, trackName, true) && saved);
            I3D_frame *frame1 = t->m_wagons[1]->frame;
            t->m_wagons[1]->frame = nullptr;
            C_Track *track0 = t->m_player.GetTrack();
            t->m_player.SetTrack(nullptr);
            MA(r.trainSave, SaWritten([&](C_chunk &c) { return t->SaveGame(c); }, &saved) == saveBytes(t, "", true) && saved);
            t->m_wagons[1]->frame = frame1;
            t->m_player.SetTrack(track0);
            t->m_running = false;
            MA(r.trainSave, SaWritten([&](C_chunk &c) { return t->SaveGame(c); }, &saved) == saveBytes(t, "", false) && saved);

            // LoadGame: chunk 1000: the track, running, the key, the wagons placed (places past them
            // dropped, Fix), the path; a name past 127 characters: its rest skipped by reads of n - 1 down
            // to 0 bytes (quirk kept); another id: nothing.
            for (int c = 0; c < 4; ++c) {
                S_SaBytes body;
                std::string name = c == 2 ? std::string(145, 'r') : c == 3 ? "ma.nosuch" : trackName;
                body.Val(static_cast<uint16_t>(name.size() + 1));
                body.Put(name.c_str(), name.size() + 1);
                const int key = 5 + c;
                const int count = c == 1 ? 5 : 3;
                body.Val(key).Val(count);
                std::vector<S_vector> places;
                for (int i = 0; i < count; ++i) {
                    places.push_back(S_vector(rnd(-50, 50), rnd(0, 3), rnd(-50, 50)));
                    body.Put(&places.back(), 12);
                }
                S_SaBytes ld;
                const uint16_t id = c == 3 ? 2000 : 1000;
                ld.Head(0x777, static_cast<uint32_t>(22 + body.b.size())).Head(0x3039, 10).Val(77)
                    .Head(id, static_cast<uint32_t>(6 + body.b.size()));
                ld.Put(body.b.data(), body.b.size());
                S_SaReader rd(ld);
                t->m_running = false;
                t->m_player.SetTrack(nullptr);
                t->m_player.SetKeyIndex(1);
                t->m_path.clear();
                reset();
                const bool ok = t->LoadGame(rd.c);
                std::vector<std::string> log;
                if (c < 2)
                    for (int i = 0; i < 3; ++i)
                        log.push_back(line("setpos %s %s", ItName(made[i].frame).c_str(), ItV(places[i]).c_str()));
                std::vector<S_Checkpoint *> path;
                for (int k = 0; k < KEYS; ++k)
                    path.push_back(&points[k]);
                if (c < 2)
                    MA(r.trainSave, ok && t->m_running && t->m_player.GetTrack() == track && t->m_player.GetKeyIndex() == key &&
                                        SaLogIs(log) && t->m_path == path && rd.c.m_depth == 1);
                else if (c == 2)
                    // (145: 127 read, 18 left: reads of 17, 16, 15, 14 take the rest; the key is not read.)
                    MA(r.trainSave, ok && t->m_running && !t->m_player.GetTrack() && t->m_player.GetKeyIndex() == 1 &&
                                        g_itLog.empty() && t->m_path.empty() && rd.c.m_depth == 1);
                else
                    MA(r.trainSave, ok && !t->m_running && !t->m_player.GetTrack() && g_itLog.empty() && rd.c.m_depth == 1);
            }
            t->m_player.SetTrack(track);
            t->m_running = true;
        }
        // LoadGame's other cases: another id (77): nothing; a name of 144 characters: 127 kept (a track
        // of that name followed), the 18 left skipped by reads of 17 down to 0 bytes (153 bytes taken:
        // the key read after them); only the count left (no places: the wagons placed at 0).
        {
            const std::string longTrack(127, 'q');
            if (!g_trackModule.FindTrack(longTrack.c_str())) {
                g_trackModule.AddTrack(longTrack.c_str());
                g_trackModule.FindTrack(longTrack.c_str())->AddKey(keyPos(0), noTurn, 5.0f);
            }
            for (int c = 0; c < 3; ++c) {
                S_SaBytes body;
                if (c == 1) {
                    body.Val(static_cast<uint16_t>(145));
                    const std::string name = std::string(144, 'q');
                    body.Put(name.c_str(), 145);
                    const std::vector<uint8_t> filler(135, 0xA5);
                    body.Put(filler.data(), filler.size());
                    const S_vector p(1.0f, 2.0f, 3.0f);
                    body.Val(7).Val(1).Put(&p, 12);
                } else {
                    body.Val(static_cast<uint16_t>(trackName.size() + 1));
                    body.Put(trackName.c_str(), trackName.size() + 1);
                    body.Val(6).Val(2);
                }
                S_SaBytes ld;
                ld.Head(0x777, static_cast<uint32_t>(22 + body.b.size())).Head(0x3039, 10).Val(77)
                    .Head(c == 0 ? 77 : 1000, static_cast<uint32_t>(6 + body.b.size()));
                ld.Put(body.b.data(), body.b.size());
                S_SaReader rd(ld);
                t->m_running = false;
                t->m_player.SetTrack(nullptr);
                t->m_player.SetKeyIndex(1);
                reset();
                const bool ok = t->LoadGame(rd.c);
                if (c == 0)
                    MA(r.trainSave, ok && !t->m_running && !t->m_player.GetTrack() && g_itLog.empty());
                else if (c == 1)
                    MA(r.trainSave, ok && t->m_running && t->m_player.GetTrack() == g_trackModule.FindTrack(longTrack.c_str()) &&
                                        t->m_player.GetKeyIndex() == 7 &&
                                        SaLogIs({"setpos " + ItName(made[0].frame) + " 1 2 3"}));
                else
                    MA(r.trainSave, ok && t->m_running && t->m_player.GetKeyIndex() == 6 &&
                                        SaLogIs({"setpos " + ItName(made[0].frame) + " 0 0 0",
                                                 "setpos " + ItName(made[1].frame) + " 0 0 0"}));
            }
            t->m_player.SetTrack(track);
            t->m_running = true;
        }

        // The path's other cases: the first key without a point within 2 m (the next key's taken); the
        // first point's links: the last one looked at (link 0) wins, even behind it (quirk kept); no
        // track: no path; a track shorter than the train: not placed, not moved (quirk kept), running
        // (the path goes on along the checkpoints past the track's end).
        {
            points[0].pos = keyPos(0) + S_vector(1.8f, 0.9f, 0.5f);
            t->m_path.clear();
            t->m_player.SetTrack(track);
            reset();
            t->BuildPath();
            // (From P1 without a previous point its link 0, P0, is taken: back the way the train came.)
            MA(r.trainPath, g_finds.size() == 2 && RbSame(g_finds[1].pos, keyPos(1)) &&
                                t->m_path == std::vector<S_Checkpoint *>{&points[1], &points[0]} && t->m_lastMarked == -1 &&
                                t->m_firstMarked == -1);
            points[0].pos = keyPos(0) + S_vector(0.3f, 0.1f, -0.2f);
            setPoint(0, points[0].pos, {KEYS + 2, 1});
            setPoint(KEYS + 2, points[0].pos + S_vector(-5.0f, 0.0f, 0.5f), {0});
            reset();
            t->BuildPath();
            MA(r.trainPath, t->m_path == std::vector<S_Checkpoint *>{&points[0], &points[KEYS + 2]});
            setPoint(0, points[0].pos, {1, KEYS + 2});
            reset();
            t->BuildPath();
            MA(r.trainPath, t->m_path.size() == KEYS && t->m_path[1] == &points[1]);
            t->m_player.SetTrack(nullptr);
            t->BuildPath();
            MA(r.trainPath, t->m_path.empty());
            const std::string shortName = trackName + ".short";
            if (!g_trackModule.FindTrack(shortName.c_str())) {
                g_trackModule.AddTrack(shortName.c_str());
                for (int k = 0; k < 4; ++k)
                    g_trackModule.FindTrack(shortName.c_str())->AddKey(keyPos(k), noTurn, 6.0f);
            }
            t->m_running = false;
            reset();
            t->SetTrack(true, shortName.c_str());
            MA(r.trainPath, t->m_running && t->m_player.GetTrack() == g_trackModule.FindTrack(shortName.c_str()) &&
                                t->m_player.GetKeyIndex() == 0 && g_itLog.empty() && g_soundLog.empty() &&
                                t->m_path.size() == KEYS);
            // SetTrack(false): stopped, the track kept; (true, none): the same track followed again.
            t->SetTrack(false, trackName.c_str());
            MA(r.trainPath, !t->m_running && t->m_player.GetTrack() == g_trackModule.FindTrack(shortName.c_str()));
            setPoint(0, points[0].pos, {1});
            // A track far from every point: no path.
            const std::string farName = trackName + ".far";
            if (!g_trackModule.FindTrack(farName.c_str())) {
                g_trackModule.AddTrack(farName.c_str());
                for (int k = 0; k < 3; ++k)
                    g_trackModule.FindTrack(farName.c_str())->AddKey(keyPos(k) + S_vector(0.0f, 500.0f, 0.0f), noTurn, 5.0f);
            }
            t->m_player.SetTrack(g_trackModule.FindTrack(farName.c_str()));
            reset();
            t->BuildPath();
            MA(r.trainPath, t->m_path.empty() && g_finds.size() == 3);
            t->m_player.SetTrack(track);
            // P6's links in the order 5, R (37° off), 7: the straightest (7) still taken.
            setPoint(6, points[6].pos, {5, KEYS + 1, 7});
            t->BuildPath();
            MA(r.trainPath, t->m_path.size() == KEYS && t->m_path[7] == &points[7]);
            setPoint(6, points[6].pos, {5, 7, KEYS + 1});
            // P12's only way on 55° off (a dot of 0.57, above 0.5): taken, the path ends there.
            S_vector h = points[12].pos - points[11].pos;
            h = S_vector(h.x, 0.0f, h.z);
            h = h * (1.0f / std::sqrt(h.x * h.x + h.z * h.z));
            const S_vector side(-h.z, 0.0f, h.x);
            setPoint(KEYS + 3, points[12].pos + (h * 0.573576f + side * 0.819152f) * 5.0f, {12});
            setPoint(12, points[12].pos, {11, KEYS + 3});
            t->BuildPath();
            std::vector<S_Checkpoint *> bent;
            for (int k = 0; k <= 12; ++k)
                bent.push_back(&points[k]);
            bent.push_back(&points[KEYS + 3]);
            MA(r.trainPath, t->m_path == bent);
            setPoint(12, points[12].pos, {11, 13});
            setPoint(KEYS + 3, S_vector(1e6f, 1e6f, 1e6f), {});
        }
        t->StopMission(true);
        creator->Destroy(t);
    }
