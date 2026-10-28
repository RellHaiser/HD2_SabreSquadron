// (seat_actors_test.inc, inside SeatActorsTest: the bike.)

    // --- the bike -------------------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_BIKE];
        SE(r.bike, creator && std::strcmp(creator->GetName(), "Bike") == 0);
        auto *a = static_cast<C_Bike *>(creator->Create());
        SE(r.bike, a->GetType() == ACTOR_BIKE && a->m_topSpeed == 7.0f && a->m_unknown124 == 0 && !a->m_started &&
                       !a->m_driving && a->m_path.empty() && a->m_speed == 0.0f && !a->m_riderModel && !a->m_rider &&
                       a->m_pedalPhase == 0.0f && !a->m_frontWheel && !a->m_body && a->m_sound.chapter == 0 &&
                       a->m_sound.volume == 1.0f && !a->SetFrame(nullptr) && !a->GetFrame());
        // The bike: the frame placed and turned; the wheels, the handlebars (turned off the frame's
        // heading), the cranks, the frame "ram"; a visual for its box.
        I3D_frame *f = AnFrame(FRAME_MODEL, "se.bike", nullptr);
        const float heading = rnd(-3, 3);
        f->m_localMatrix.m[0][0] = std::cos(heading);
        f->m_localMatrix.m[0][2] = -std::sin(heading);
        f->m_localMatrix.m[2][0] = std::sin(heading);
        f->m_localMatrix.m[2][2] = std::cos(heading);
        f->m_localMatrix(3) = S_vector(rnd(-20, 20), rnd(0, 2), rnd(-20, 20));
        I3D_frame *body = RbBoxFrame(f, "ram", -0.2f, 0.2f, 0.3f, 1.0f, -0.9f, 0.9f, S_vector(0.0f, 0.1f, 0.0f));
        body->m_name = Stub(body).name.c_str();
        I3D_frame *front = AnFrame(FRAME_DUMMY, "kolo1", f);
        front->m_localMatrix(3) = S_vector(0.0f, 0.35f, 0.8f);
        I3D_frame *rear = AnFrame(FRAME_DUMMY, "kolo2", f);
        rear->m_localMatrix(3) = S_vector(0.0f, 0.35f, -0.8f);
        I3D_frame *bars = AnFrame(FRAME_DUMMY, "riditka", body);
        const float barTurn = rnd(-0.6f, 0.6f);
        bars->m_localMatrix.m[0][0] = std::cos(barTurn);
        bars->m_localMatrix.m[0][2] = -std::sin(barTurn);
        bars->m_localMatrix.m[2][0] = std::sin(barTurn);
        bars->m_localMatrix.m[2][2] = std::cos(barTurn);
        bars->m_localMatrix(3) = S_vector(0.0f, 1.0f, 0.6f);
        I3D_frame *cranks = AnFrame(FRAME_DUMMY, "kliky", f);
        cranks->m_localMatrix(3) = S_vector(0.0f, 0.3f, 0.0f);
        // (Both pitched a little: the y parts of their directions count.)
        {
            const float p = rnd(0.05f, 0.2f);
            S_matrix pitch;
            pitch.Identity();
            pitch.m[1][1] = std::cos(p);
            pitch.m[1][2] = std::sin(p);
            pitch.m[2][1] = -std::sin(p);
            pitch.m[2][2] = std::cos(p);
            const S_vector bp = body->m_localMatrix(3);
            body->m_localMatrix = pitch;
            body->m_localMatrix(3) = bp;
            S_matrix yawed = bars->m_localMatrix;
            const S_vector hp = yawed(3);
            yawed(3) = S_vector(0, 0, 0);
            bars->m_localMatrix = pitch * yawed;
            bars->m_localMatrix(3) = hp;
        }
        SE(r.bike, a->SetFrame(f) && a->GetFrame() == f);

        // ReadProperties: the top speed read and put back (quirk), the next value raw.
        {
            S_SaBytes in;
            in.Head(0x777, 14).Val(12.5f).Val(31);
            S_SaReader rd(in);
            SE(r.bikeProps, a->ReadProperties(rd.c) && a->m_topSpeed == 7.0f && a->m_unknown124 == 31);
            S_SaBytes shortIn;
            shortIn.Head(0x777, 10).Val(3.0f);
            S_SaReader rd2(shortIn);
            SE(r.bikeProps, a->ReadProperties(rd2.c) && a->m_topSpeed == 7.0f && a->m_unknown124 == 31);
            auto *b = static_cast<C_Bike *>(creator->Create());
            b->m_topSpeed = 4.5f;
            b->m_unknown124 = 8;
            b->m_speed = 2.0f;
            a->CopyFrom(b);
            SE(r.bikeProps, a->m_topSpeed == 4.5f && a->m_unknown124 == 8 && a->m_speed == 0.0f);
            creator->Destroy(b);
        }
        a->m_topSpeed = rnd(6.0f, 25.0f);

        // --- StartMission: once, the box (flags 0x40, COLLISION_TYPE_PIKE), the sound request at the
        // frame, the parts, the shadow ------------------------------------------------------------------------
        const auto boxVolumeIs = [&](const S_CollisionVolume &x, const S_matrix &inv, I3D_frame *fr, uint32_t flags,
                                     uint32_t type, C_Actor *actor) {
            I3D_bbox tmp;
            const I3D_bbox box = *F_GetHierarchyBBox(nullptr, tmp, fr);
            S_vector center(0.0f, 0.0f, 0.0f);
            float radius = 0.0f;
            if (box.min.x <= box.max.x) {
                const S_vector half = (box.max - box.min) * 0.5f;
                center = box.min + half;
                radius = std::sqrt((half.z * half.z + half.y * half.y) + half.x * half.x);
            }
            const S_matrix world = fr->GetMatrix();
            S_matrix expectInv;
            expectInv.Inverse(world);
            const S_vector s = fr->m_parent ? world.GetScale() : fr->m_scale;
            const float yz = s.y <= s.z ? s.z : s.y;
            const float most = s.x <= yz ? yz : s.x;
            const S_vector cw = center * world;
            const float rr = most * radius;
            return x.flags == flags && x.frame == fr && x.type == type && x.actor == actor && RbSameBox(x.bbox, box) &&
                   SaSame(&x.radius, &rr, 4) && RbSame(x.center, cw) && x.matrix == &fr->m_worldMatrix &&
                   x.invMatrix == &inv && SaSame(&inv, &expectInv, sizeof inv);
        };
        reset();
        a->m_driving = true;
        a->m_speed = 3.0f;
        a->m_path = {4, 5};
        a->m_pedalPhase = 2.0f;
        a->m_sound.attachTo = f;
        a->m_sound.flags = 9;
        a->m_riderModel = reinterpret_cast<I3D_model *>(front);
        a->m_rider = rear;
        a->StartMission(true);
        const S_vector fw = f->GetWorldPos();
        SE(r.bikeStart, g_activations == std::vector<bool>{true} && a->m_started && !a->m_driving && a->m_speed == 0.0f &&
                            a->m_path.empty() && boxVolumeIs(a->m_volume, a->m_invMatrix, f, 0x40, COLLISION_TYPE_PIKE, a) &&
                            g_volumeLog.size() == 1 && g_volumeLog[0].op == 'A' && g_volumeLog[0].volume == &a->m_volume &&
                            a->m_sound.chapter == 1 && RbSame(a->m_sound.pos, fw) && a->m_sound.posW == f->GetMatrix().m[3][3] &&
                            !a->m_sound.attachTo && a->m_sound.flags == 0 && a->m_sound.source == a && a->m_frontWheel == front &&
                            a->m_rearWheel == rear && a->m_handlebars == bars && a->m_cranks == cranks && a->m_body == body &&
                            !a->m_riderModel && a->m_pedalPhase == 0.0f && !a->m_rider && SaLogIs({"createshadow se.bike"}));
        reset();
        a->StartMission(false);
        SE(r.bikeStart, g_activations == std::vector<bool>{false} && g_volumeLog.empty() && g_itLog.empty());

        // --- the road network: points along the way (radii 1..5 m), the first next to the bike -------------------
        alignas(16) static uint8_t checkpointsMemory[sizeof(C_Checkpoints)];
        std::memset(checkpointsMemory, 0, sizeof checkpointsMemory);
        auto &checkpoints = *reinterpret_cast<C_Checkpoints *>(checkpointsMemory);
        GetGame()->m_checkpoints = &checkpoints;
        C_CheckpointNetwork &roads = *checkpoints.GetRoadNetwork();
        static S_Checkpoint points[8];
        roads.m_points = points;
        roads.m_pointCount = 8;
        {
            S_vector p(fw.x, fw.y, fw.z + 6.0f);       // (the others 6 m on at least: out of reach)
            for (int k = 0; k < 8; ++k) {
                points[k].pos = p;
                points[k].radius = static_cast<uint8_t>(16 + 12 * k);
                p = S_vector(p.x + rnd(-6, 6), p.y + rnd(-0.5f, 0.5f), p.z + rnd(4, 9));
            }
            points[0].pos = S_vector(fw.x + 1.0f, fw.y, fw.z - 0.5f);
        }

        // --- Drive: from the point nearest the bike (5 m) along the path found to the target; a rider not yet
        // on mounts (its model copied onto "ram", the animations), its human suspended, the speed 0 -------------
        C_Human rider;
        I3D_frame *rf = AnFrame(FRAME_MODEL, "se.rider", nullptr);
        rider.m_frame = rf;
        rf->m_localMatrix(3) = S_vector(fw.x + 2.0f, fw.y, fw.z);
        alignas(16) static uint8_t scriptMemory[64];
        S_FrameData riderData = {};
        riderData.actor = &rider;
        riderData.script = reinterpret_cast<C_Script *>(scriptMemory);
        rf->m_data = &riderData;
        const auto mountLog = [](const std::string &rm) {
            return std::vector<std::string>{"createframe 9", "duplicate new se.rider", "setname new " + rm,
                                            "link " + rm + " se.bike 0", "update " + rm, "link " + rm + " ram 1",
                                            "update " + rm, "getanim bike_legs", "setanim " + rm + " bike_legs 0 1 2",
                                            "animrelease bike_legs", "getanim bike_ride", "setanim " + rm + " bike_ride 1 1 0",
                                            "animrelease bike_ride"};
        };
        const std::string rm = "se.rider_ridedupl";
        {
            // No point near: nothing; no path: nothing (the old path kept).
            const S_vector p0 = points[0].pos;
            points[0].pos = S_vector(p0.x + 50.0f, p0.y, p0.z);
            a->m_path = {7};
            reset();
            a->Drive(3, 9.0f, rf);
            SE(r.bikeDrive, a->m_path == std::vector<uint16_t>{7} && !a->m_driving && !a->m_riderModel &&
                                SaLogIs({"nearest " + ItV(fw) + " 5 -1"}));
            points[0].pos = p0;
            g_sePathFound = false;
            reset();
            a->Drive(3, 9.0f, rf);
            SE(r.bikeDrive, a->m_path == std::vector<uint16_t>{7} && !a->m_driving &&
                                SaLogIs({"nearest " + ItV(fw) + " 5 -1", "findpath 0 3 0"}));
            g_sePathFound = true;
        }
        {
            // The path and then the target; mounted, the human suspended.
            g_sePath = {1, 2};
            rider.suspends.clear();
            a->m_speed = 1.5f;
            reset();
            a->Drive(3, 9.0f, rf);
            std::vector<std::string> log = {"nearest " + ItV(fw) + " 5 -1", "findpath 0 3 0"};
            for (const std::string &x : mountLog(rm))
                log.push_back(x);
            I3D_frame *model = a->m_riderModel;
            SE(r.bikeDrive, a->m_path == std::vector<uint16_t>({1, 2, 3}) && a->m_driving && model && a->m_rider == rf &&
                                model->m_parent == body && ItName(model) == rm && a->m_speed == 0.0f &&
                                rider.suspends == std::vector<bool>{true} && SaLogIs(log) && SaSame(&model->m_rot.s, "\0\0\x80\x3f", 4));
            // Driven again (mounted): no mount, no suspension; the target being the start: the path only.
            a->m_speed = 1.5f;
            g_sePath = {4};
            reset();
            a->Drive(0, 1.0f, rf);
            SE(r.bikeDrive, a->m_path == std::vector<uint16_t>{4} && a->m_driving && a->m_riderModel == model &&
                                a->m_speed == 1.5f && rider.suspends.size() == 1 &&
                                SaLogIs({"nearest " + ItV(fw) + " 5 -1", "findpath 0 0 0"}));
            // An empty path to the start: not driving any more (still set: the flag only rises).
            a->m_driving = false;
            g_sePath = {};
            reset();
            a->Drive(0, 1.0f, nullptr);
            SE(r.bikeDrive, a->m_path.empty() && !a->m_driving);
            // Off: the human back where the bike is (its sector), the model released.
            a->m_driving = true;
            rider.suspends.clear();
            rider.m_position = {0, 0, 0, 0};
            reset();
            a->Drive(CHECKPOINT_NONE, 0.0f, rf);
            const S_matrix bw = f->GetMatrix();
            SE(r.bikeDrive, !a->m_riderModel && !a->m_driving && rider.suspends == std::vector<bool>{false} &&
                                SaSame(&rider.m_position, &bw.m[3][0], 16) &&
                                SaLogIs({"sector se.rider " + ItV(rf->GetWorldPos()), "link " + rm + " 0 0", "release " + rm}));
            // (Still a rider: RestartMission gets it off first.)
            SE(r.bikeDrive, a->m_rider == rf);
            // A frame without an actor: mounted, nothing suspended; the legs' animation missing: no rider.
            S_FrameData bare = {};
            rf->m_data = &bare;
            g_sePath = {2};
            reset();
            a->Drive(3, 1.0f, rf);
            SE(r.bikeDrive, a->m_riderModel && rider.suspends.size() == 1 && a->m_path == std::vector<uint16_t>({2, 3}));
            a->Drive(CHECKPOINT_NONE, 0.0f, rf);
            rf->m_data = &riderData;
            a->m_rider = nullptr;
            g_seAnimMissing = "bike_legs";
            reset();
            a->Drive(3, 1.0f, rf);
            SE(r.bikeDrive, a->m_riderModel && !a->m_rider &&
                                std::find(g_itLog.begin(), g_itLog.end(), "getanim bike_ride") == g_itLog.end());
            g_seAnimMissing.clear();
            a->Drive(CHECKPOINT_NONE, 0.0f, rf);
            a->m_rider = nullptr;
        }

        // --- Update (from the asm) ---------------------------------------------------------------------------
        struct S_SeRay {
            S_vector from, dir;
            bool hitPos, normal;
            uint32_t types, flags;
            int ignores, clears;
        };
        static std::vector<S_SeRay> rays;
        static int rayHitAt;                            // (-1: none, else that ray meets the ground)
        static S_vector rayHit;
        g_rayTestHook = [](const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                           uint32_t types, uint32_t flags) -> S_CollisionVolume * {
            rays.push_back({from, dir, hitPos != nullptr, hitNormal != nullptr, types, flags, g_ignores, g_ignoreClears});
            if (static_cast<int>(rays.size()) - 1 != rayHitAt)
                return nullptr;
            if (hitPos)
                *hitPos = rayHit;
            return &g_groundVolume;
        };
        struct S_BikeO {
            std::vector<std::string> log;
            std::vector<S_SeRay> rays;
            std::vector<uint16_t> path;
            bool driving;
            float speed, pedal;
            S_vector framePos;
            S_quat wheel, rearRot, crankRot;
            S_matrix inv;
            S_vector center;
            float centerW;
            bool moved;
        };
        const auto localDir = [](I3D_frame *fr) {
            if (!(fr->m_frameFlags & FRMFLAGS_LMATRIX_VALID)) {
                S_quat q = fr->m_rot;
                q.Normalize();
                S_matrix m = fr->m_localMatrix;
                m.SetRot3(q);
                return S_vector(m(2));
            }
            S_vector v = fr->m_localMatrix(2);
            SeNormalize(v, (v.y * v.y + v.z * v.z) + v.x * v.x);
            return v;
        };
        const auto worldDir = [](I3D_frame *fr) {
            S_vector v = fr->GetMatrix()(2);
            SeNormalize(v, (v.y * v.y + v.z * v.z) + v.x * v.x);
            return v;
        };
        const auto rotOf = [](I3D_frame *fr) {
            if (!(fr->m_frameFlags & FRMFLAGS_ROT_DIRTY)) {
                S_quat q;
                q.Make(fr->m_localMatrix);
                return q;
            }
            return fr->m_rot;
        };
        const auto bikeOracle = [&](C_Bike *b, int t, int hitAt, const S_vector &hit) {
            S_BikeO o;
            o.path = b->m_path;
            o.driving = b->m_driving;
            o.speed = b->m_speed;
            o.pedal = b->m_pedalPhase;
            o.moved = false;
            o.wheel = b->m_frontWheel->m_rot;
            o.rearRot = b->m_rearWheel->m_rot;
            o.crankRot = b->m_cranks->m_rot;
            I3D_frame *fr = b->GetFrame();
            o.framePos = fr->GetPos();
            const S_vector before = fr->GetPos();
            const uint32_t time = static_cast<uint32_t>(t);
            const std::string rmName = b->m_riderModel ? ItName(b->m_riderModel) : "";
            if (o.driving) {
                if (o.path.empty())
                    return o;
                const S_Checkpoint &next = points[o.path[0]];
                const float tx = next.pos.x, tz = next.pos.z;
                const float ax = fr->GetPos().x, az = fr->GetPos().z;
                const float dx = ax - tx, dz = az - tz;
                const float distance = std::sqrt(dz * dz + dx * dx);
                const float radius = static_cast<float>(next.radius) * 0.0625f;
                const float reach = radius < 3.0f ? 3.0f : radius;
                if (distance > reach) {
                    o.speed = static_cast<float>(time) * 0.001f + o.speed;
                    const float top = b->m_topSpeed * 0.28f;
                    if (o.speed > top)
                        o.speed = top;
                } else {
                    o.path.erase(o.path.begin());
                    if (o.path.empty()) {
                        o.driving = false;
                        if (b->m_rider && b->m_rider->m_data && b->m_rider->m_data->script)
                            o.log.push_back("advance");
                    }
                }
                S_vector d = localDir(fr);
                const float facing[2] = {d.x, d.z};
                const float toPoint[2] = {tx - ax, tz - az};
                float turn = AngleBetween2D(facing, toPoint);
                const double cone = std::asin(static_cast<double>(radius / distance));
                if (cone > std::fabs(static_cast<double>(turn)))
                    turn = 0.0f;
                const float ft = static_cast<float>(time);
                S_quat yaw;
                yaw.Make(S_vector(0.0f, 1.0f, 0.0f), ft * turn * 0.001f);
                SeNormalize(d, (d.y * d.y + d.x * d.x) + d.z * d.z);
                d = d.RotateByMatrix(yaw.RotationMatrix());
                const S_vector bodyWorld = worldDir(b->m_body);
                o.log.push_back("worlddir se.bike " + ItV(d) + " 0");
                o.log.push_back("update se.bike");
                o.log.push_back("setdir ram " + ItV(bodyWorld) + " 0");
                o.log.push_back("update ram");
                S_vector bd = localDir(b->m_body);
                bd.y = 0.0f;
                const S_vector hd = localDir(b->m_handlebars);
                const float angle = bd.AngleTo(hd);
                S_vector axis(hd.z * bd.y - bd.z * hd.y, bd.z * hd.x - hd.z * bd.x, hd.y * bd.x - hd.x * bd.y);
                SeNormalize(axis, (axis.z * axis.z + axis.y * axis.y) + axis.x * axis.x);
                float follow = ft * 0.002f * angle;
                if (angle < 0.05235988f)
                    follow *= 3.0f;
                S_quat q2;
                q2.Make(axis, -follow);
                const S_vector turned = bd.RotateByMatrix(q2.RotationMatrix());
                o.log.push_back("worlddir ram " + ItV(turned) + " 0");
                o.log.push_back("update ram");
                float steer = turned.x > 0.0f ? -angle : angle;
                if (steer > 0.43633232f)
                    steer = 0.43633232f;
                else if (steer < -0.43633232f)
                    steer = -0.43633232f;
                if (b->m_riderModel) {
                    const int frame = static_cast<int>(steer * 57.295776f + 25.0f);
                    o.log.push_back(SeLine("animtime %s 1 %d", rmName.c_str(), frame * 16));
                    o.log.push_back("tick " + rmName + " 0");
                    o.log.push_back("update " + rmName);
                }
            }
            if (o.speed == 0.0f)
                return o;
            o.moved = true;
            if (o.path.empty()) {
                o.speed = o.speed - static_cast<float>(time) * 0.001f;
                if (o.speed < 0.0f)
                    o.speed = 0.0f;
            }
            const float ft = static_cast<float>(time);
            const float distance = ft * o.speed * 0.001f;
            S_vector ahead = worldDir(b->m_handlebars);
            ahead.y = 0.0f;
            SeNormalize(ahead, ahead.z * ahead.z + ahead.x * ahead.x);
            const S_vector at = fr->GetWorldPos();
            const S_vector to(ahead.x * distance + at.x, ahead.y * distance + at.y, ahead.z * distance + at.z);
            o.log.push_back("setpos se.bike " + ItV(to));
            o.log.push_back("update se.bike");
            // (The wheel's world after the move: the frame has no parent.)
            S_matrix world = fr->m_localMatrix;
            world(3) = to;
            const S_vector wheel = b->m_frontWheel->m_localMatrix(3) * world;
            S_vector down(0.0f, -0.35f, 0.0f);
            S_vector ground(0.0f, 0.0f, 0.0f);
            const int ign = g_ignores + 1, clr = g_ignoreClears;
            o.rays.push_back({wheel, down, true, false, 0u, 0x800000u, ign, clr});
            if (hitAt == 0) {
                ground = hit;
            } else {
                down = S_vector(ft * 0.0f * 0.001f + 0.0f, -0.35f + ft * -2.0f * 0.001f, 0.0f + ft * 0.0f * 0.001f);
                o.rays.push_back({wheel, down, true, false, 0u, 0x800000u, ign, clr});
                ground = hitAt == 1 ? hit : S_vector(down.x + wheel.x, down.y + wheel.y, down.z + wheel.z);
            }
            const S_vector offset(to.x - wheel.x, to.y - wheel.y, to.z - wheel.z);
            o.framePos = S_vector(offset.x + ground.x, (ground.y + offset.y) + 0.35f, ground.z + offset.z);
            const S_vector mv(o.framePos.x - before.x, o.framePos.y - before.y, o.framePos.z - before.z);
            const S_vector d = localDir(fr);
            o.log.push_back("worlddir se.bike " + ItV(S_vector(d.x + (mv.x + mv.x), d.y + (mv.y + mv.y), d.z + (mv.z + mv.z))) + " 0");
            if (distance != 0.0f) {
                const float roll = distance * 3.3333333f;
                S_quat spin;
                spin.Make(S_vector(1.0f, 0.0f, 0.0f), -roll);
                o.wheel = rotOf(b->m_frontWheel).RotateByMatrix(spin.RotationMatrix());
                o.wheel.Normalize();
                o.rearRot = o.wheel;
                o.log.push_back("update kolo1");
                o.log.push_back("update kolo2");
                const float crank = roll * 0.5f;
                S_quat cs;
                cs.Make(S_vector(1.0f, 0.0f, 0.0f), -crank);
                o.crankRot = rotOf(b->m_cranks).RotateByMatrix(cs.RotationMatrix());
                o.crankRot.Normalize();
                o.log.push_back("update kliky");
                if (b->m_riderModel) {
                    const float phase = crank + o.pedal;
                    if (phase != 0.0f) {
                        const float frames = phase * 127.32395f;
                        const int whole = static_cast<int>(frames);
                        o.log.push_back(SeLine("advancestages %s %d 0 0 0 0 0 0 0", rmName.c_str(), whole));
                        o.log.push_back("update " + rmName);
                        o.pedal = (frames - static_cast<float>(whole)) * 0.007853982f;
                    }
                }
            }
            // (The inline SetPos dropped the bound.)
            o.log.push_back("bound se.bike");
            S_matrix fw2 = fr->m_localMatrix;
            fw2(3) = o.framePos;
            o.inv.Inverse(fw2);
            o.center = S_vector(0.25f, 0.5f, -0.75f);
            o.centerW = 1.5f;
            return o;
        };
        const auto bikeSame = [&](C_Bike *b, const S_BikeO &o) {
            bool ok = b->m_path == o.path && b->m_driving == o.driving && SaSame(&b->m_speed, &o.speed, 4) &&
                      SaSame(&b->m_pedalPhase, &o.pedal, 4) && RbSame(b->GetFrame()->GetPos(), o.framePos) && SaLogIs(o.log) &&
                      SaSameQuat(b->m_frontWheel->m_rot, o.wheel) && SaSameQuat(b->m_rearWheel->m_rot, o.rearRot) &&
                      SaSameQuat(b->m_cranks->m_rot, o.crankRot) && rays.size() == o.rays.size();
            for (size_t i = 0; ok && i < o.rays.size(); ++i)
                ok = RbSame(rays[i].from, o.rays[i].from) && RbSame(rays[i].dir, o.rays[i].dir) && rays[i].hitPos &&
                     !rays[i].normal && rays[i].types == o.rays[i].types && rays[i].flags == o.rays[i].flags &&
                     rays[i].ignores == o.rays[i].ignores && rays[i].clears == o.rays[i].clears;
            S_matrix inv;
            inv.Inverse(b->GetFrame()->GetMatrix());
            if (o.moved)
                ok = ok && SaSame(&b->m_invMatrix, &inv, sizeof inv) && RbSame(b->m_volume.center, o.center) &&
                     b->m_volume.centerW == o.centerW && g_volumeLog.size() == 1 && g_volumeLog[0].op == 'M' &&
                     g_volumeLog[0].volume == &b->m_volume && g_ignoreClears == o.rays[0].clears + 1;
            else
                ok = ok && g_volumeLog.empty();
            return ok;
        };
        // The ride: mounted, along the path (each point reached in turn: the last one advances the rider's
        // script), then coasting to a stop; the ground met by the first ray, the second or neither; the
        // steering's limits; the frame's bound now valid, now not.
        g_sePath = {1, 2};
        a->Drive(4, 1.0f, rf);
        for (int step = 0; step < 120 && (a->m_driving || a->m_speed != 0.0f); ++step) {
            const int t = static_cast<int>(rnd(10, 250));
            const int hitAt = step % 3;
            const S_vector wp = a->m_frontWheel->GetWorldPos();
            rayHitAt = hitAt;
            rayHit = S_vector(wp.x + rnd(-0.1f, 0.1f), wp.y - rnd(0.2f, 0.5f), wp.z + rnd(-0.1f, 0.1f));
            // (Now and then a point reached at once: the bike placed on it.)
            if (step % 9 == 4 && !a->m_path.empty()) {
                const S_vector p = points[a->m_path[0]].pos;
                f->m_localMatrix(3) = S_vector(p.x + 0.5f, f->m_localMatrix.m[3][1], p.z - 0.5f);
                f->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            }
            rays.clear();
            reset();
            g_ignores = g_ignoreClears = 0;
            const S_BikeO e = bikeOracle(a, t, hitAt, rayHit);
            a->Update(t);
            SE(r.bikeUpdate, bikeSame(a, e));
        }
        SE(r.bikeUpdate, !a->m_driving && a->m_speed == 0.0f);
        a->Drive(CHECKPOINT_NONE, 0.0f, rf);
        // Ridden without a path: nothing at all.
        a->m_driving = true;
        a->m_path.clear();
        rays.clear();
        reset();
        a->Update(50);
        SE(r.bikeUpdate, g_itLog.empty() && rays.empty() && g_volumeLog.empty());
        a->m_driving = false;
        // A wide point (radius over 3 m): reached within it; the turn within its angle not made.
        {
            a->m_path = {7};
            a->m_driving = true;
            a->m_speed = 1.0f;
            const S_vector p = points[7].pos;
            f->m_localMatrix(3) = S_vector(p.x + 4.0f, f->m_localMatrix.m[3][1], p.z + 4.5f);
            f->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            rayHitAt = -1;
            rays.clear();
            reset();
            g_ignores = g_ignoreClears = 0;
            const S_BikeO e = bikeOracle(a, 40, -1, S_vector(0, 0, 0));
            a->Update(40);
            SE(r.bikeUpdate, bikeSame(a, e) && !a->m_driving);
        }
        // Neither ridden nor moving: nothing.
        a->m_driving = false;
        a->m_speed = 0.0f;
        rays.clear();
        reset();
        a->Update(30);
        SE(r.bikeUpdate, g_itLog.empty() && rays.empty() && g_volumeLog.empty());
        // A narrow point (1 m: reached within 3 m) 3.5 m away, off in x and z: not reached; the handlebars
        // 3.7 degrees off, then 2 (under 3 degrees the frame follows them faster).
        for (const float yaw : {0.0645f, 0.035f}) {
            a->m_path = {0};
            a->m_driving = true;
            a->m_speed = 1.0f;
            const S_vector p = points[0].pos;
            f->m_localMatrix(3) = S_vector(p.x + 2.0f, f->m_localMatrix.m[3][1], p.z + 2.9f);
            f->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            const S_matrix barsLocal = bars->m_localMatrix;
            bars->m_localMatrix.Identity();
            bars->m_localMatrix.m[0][0] = std::cos(yaw);
            bars->m_localMatrix.m[0][2] = -std::sin(yaw);
            bars->m_localMatrix.m[2][0] = std::sin(yaw);
            bars->m_localMatrix.m[2][2] = std::cos(yaw);
            bars->m_localMatrix(3) = barsLocal(3);
            bars->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            rayHitAt = 0;
            rayHit = S_vector(p.x + 2.0f, p.y - 0.4f, p.z + 2.9f);
            rays.clear();
            reset();
            g_ignores = g_ignoreClears = 0;
            const S_BikeO e = bikeOracle(a, 40, 0, rayHit);
            a->Update(40);
            SE(r.bikeUpdate, bikeSame(a, e) && a->m_driving && a->m_path == std::vector<uint16_t>{0});
            bars->m_localMatrix = barsLocal;
            bars->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            a->m_path.clear();
            a->m_driving = false;
            a->m_speed = 0.0f;
        }
        g_rayTestHook = nullptr;

        // --- the saved game: ridden, chunk 2000 holding 5000 the rider's name, 6000 the path, 3000 the speed,
        // 4000 the pedals; else an empty 1000 ------------------------------------------------------------------
        {
            S_SaBytes base;
            base.Head(0x3039, 10).Val(g_actorSaveTag);
            a->m_driving = false;
            S_SaBytes sv;
            sv.Head(0x777, 6 + 10 + 6).Put(base.b.data(), base.b.size());
            sv.Head(1000, 6);
            bool saved = false;
            SE(r.bikeSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv.b && saved);
            a->m_driving = true;
            a->m_rider = rf;
            a->m_path = {3, 1, 6};
            a->m_speed = 1.25f;
            a->m_pedalPhase = -0.5f;
            S_SaBytes body;
            body.Head(5000, 6 + 9).Put("se.rider", 9);
            body.Head(6000, 6 + 4 + 6).Val(3).Val(static_cast<uint16_t>(3)).Val(static_cast<uint16_t>(1)).Val(static_cast<uint16_t>(6));
            body.Head(3000, 10).Val(1.25f).Head(4000, 10).Val(-0.5f);
            S_SaBytes sv2;
            sv2.Head(0x777, static_cast<uint32_t>(6 + 10 + 6 + body.b.size())).Put(base.b.data(), base.b.size());
            sv2.Head(2000, static_cast<uint32_t>(6 + body.b.size())).Put(body.b.data(), body.b.size());
            SE(r.bikeSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv2.b && saved);
            // (Fix: ridden without a rider, an empty name.)
            a->m_rider = nullptr;
            S_SaBytes body2;
            body2.Head(5000, 7).Put("", 1);
            body2.Put(body.b.data() + 15, body.b.size() - 15);
            S_SaBytes sv3;
            sv3.Head(0x777, static_cast<uint32_t>(6 + 10 + 6 + body2.b.size())).Put(base.b.data(), base.b.size());
            sv3.Head(2000, static_cast<uint32_t>(6 + body2.b.size())).Put(body2.b.data(), body2.b.size());
            SE(r.bikeSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv3.b && saved);
            // Loaded: the rider found by name, mounted and suspended (speed 0 first), the path added, the speed
            // and the pedals; a short path chunk repeats its last point; another id entered.
            a->m_driving = false;
            a->m_path.clear();
            a->m_speed = 9.0f;
            a->m_pedalPhase = 9.0f;
            g_maSceneFrames["se.rider"] = rf;
            rider.suspends.clear();
            const auto loadOf = [&](const std::vector<uint8_t> &inner, uint16_t id) {
                S_SaBytes in;
                in.Head(0x777, static_cast<uint32_t>(6 + 10 + 6 + inner.size())).Head(0x3039, 10).Val(66)
                    .Head(id, static_cast<uint32_t>(6 + inner.size()));
                in.Put(inner.data(), inner.size());
                return in;
            };
            {
                // (The speed chunk after the name: loaded over the suspension's 0.)
                S_SaBytes inner;
                inner.Put(body.b.data(), body.b.size());
                const S_SaBytes in = loadOf(inner.b, 2000);
                S_SaReader rd(in);
                g_actorLoads.clear();
                reset();
                SE(r.bikeSave, a->LoadGame(rd.c) && rd.c.m_depth == 1 && g_actorLoads == std::vector<int>{66} && a->m_driving && a->m_rider == rf &&
                                   a->m_riderModel && a->m_path == std::vector<uint16_t>({3, 1, 6}) && a->m_speed == 1.25f &&
                                   a->m_pedalPhase == -0.5f && rider.suspends == std::vector<bool>{true} &&
                                   g_itLog.size() > 1 && g_itLog[0] == "find se.rider ffff");
                a->Drive(CHECKPOINT_NONE, 0.0f, rf);
            }
            {
                S_SaBytes inner;
                inner.Head(6000, 6 + 4 + 3).Val(4).Val(static_cast<uint16_t>(9)).Val(static_cast<uint8_t>(1));
                inner.Head(5000, 6 + 8).Put("nothing", 8);
                inner.Head(55, 16).Head(3000, 10).Val(2.0f).Head(4000, 10).Val(1.0f);
                const S_SaBytes in = loadOf(inner.b, 2000);
                S_SaReader rd(in);
                a->m_path.clear();
                a->m_driving = false;
                a->m_speed = 9.0f;
                a->m_pedalPhase = 9.0f;
                SE(r.bikeSave, a->LoadGame(rd.c) && rd.c.m_depth == 2 && a->m_path == std::vector<uint16_t>({9, 9, 9, 9}) && !a->m_driving &&
                                   a->m_speed == 2.0f && a->m_pedalPhase == 9.0f);
            }
            {
                // The rider without a speed chunk: 0 (suspended); a path chunk with a count only (its
                // points 0), one without a count (none); the chunk left.
                S_SaBytes inner;
                inner.Head(5000, 6 + 9).Put("se.rider", 9);
                inner.Head(6000, 6 + 4).Val(2);
                inner.Head(6000, 6);
                inner.Head(4000, 10).Val(0.75f);
                const S_SaBytes in = loadOf(inner.b, 2000);
                S_SaReader rd(in);
                a->m_path.clear();
                a->m_speed = 9.0f;
                SE(r.bikeSave, a->LoadGame(rd.c) && rd.c.m_depth == 1 && a->m_speed == 0.0f &&
                                   a->m_path == std::vector<uint16_t>({0, 0}) && a->m_pedalPhase == 0.75f && a->m_driving);
                a->Drive(CHECKPOINT_NONE, 0.0f, rf);
                a->m_driving = false;
                a->m_path.clear();
            }
            {
                // Not ridden (1000): nothing read.
                S_SaBytes inner;
                inner.Head(3000, 10).Val(2.5f);
                const S_SaBytes in = loadOf(inner.b, 1000);
                S_SaReader rd(in);
                a->m_speed = 9.0f;
                SE(r.bikeSave, a->LoadGame(rd.c) && rd.c.m_depth == 1 && a->m_speed == 9.0f);
            }
            g_maSceneFrames.erase("se.rider");
        }

        // --- StopMission, RestartMission ------------------------------------------------------------------------
        {
            f->m_localMatrix(3) = fw;
            f->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            g_sePath = {1};
            a->Drive(3, 1.0f, rf);
            a->m_cranks->m_rot = S_quat(0.5f, 0.5f, 0.5f, 0.5f);
            I3D_frame *shadow = AnFrame(FRAME_DUMMY, "se.shadow", nullptr);
            g_seShadow = shadow;
            reset();
            a->StopMission(true);
            S_quat straight;
            straight.Make(IdentityMatrix());
            straight.Normalize();
            SE(r.bikeStop, g_deactivations == std::vector<bool>{true} && !a->m_started && !a->m_riderModel && !a->m_rider &&
                               a->m_path.empty() && g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R' &&
                               g_volumeLog[0].volume == &a->m_volume && SaSameQuat(a->m_cranks->m_rot, straight) &&
                               SaLogIs({"link " + rm + " 0 0", "release " + rm, "worlddir ram 0 0 1 0", "getshadow se.bike",
                                        "getshadow se.bike", "release se.shadow"}));
            g_seShadow = nullptr;
            reset();
            a->StopMission(false);
            SE(r.bikeStop, g_deactivations == std::vector<bool>{false} && g_itLog.empty() && g_volumeLog.empty());
            a->StartMission(true);
            f->m_localMatrix(3) = fw;
            f->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            g_sePath = {2};
            a->Drive(3, 1.0f, rf);
            rider.suspends.clear();
            reset();
            a->RestartMission(false);
            SE(r.bikeStop, rider.suspends == std::vector<bool>{false} && g_deactivations == std::vector<bool>{true} &&
                               g_activations == std::vector<bool>{true} && a->m_started && !a->m_riderModel && !a->m_driving &&
                               g_itLog.size() > 3 && g_itLog[0] == "sector se.rider " + ItV(rf->GetWorldPos()));
            reset();
            a->RestartMission(true);
            SE(r.bikeStop, g_deactivations == std::vector<bool>{true} && g_activations == std::vector<bool>{true} &&
                               rider.suspends.size() == 1);
            a->StopMission(true);
        }
        rf->m_data = nullptr;
        creator->Destroy(a);
    }
