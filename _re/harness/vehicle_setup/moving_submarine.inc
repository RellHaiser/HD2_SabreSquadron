// (moving_actors_test.inc, inside MovingActorsTest: the submarine.)

    // --- the submarine ---------------------------------------------------------------------------------------
    {
        C_ActorCreator *creator = g_actorCreators[ACTOR_SUBMARINE];
        MA(r.sub, creator && std::strcmp(creator->GetName(), "Submarine") == 0);
        g_textIds = true;
        const size_t subs0 = GetSubmarines().size();
        auto *a = static_cast<C_Submarine *>(creator->Create());
        auto *b = static_cast<C_Submarine *>(creator->Create());
        const auto seatsEmpty = [](const C_Submarine *s) {
            for (const auto &seat : s->m_seats)
                if (seat.blocked || seat.occupant || seat.frame || seat.flags)
                    return false;
            return true;
        };
        MA(r.sub, a->GetType() == ACTOR_SUBMARINE && std::strcmp(static_cast<const char *>(a->m_name), "text 7156") == 0 && a->m_maxWaterLevel == 100.0f &&
                      a->m_engineSound == -1 && a->m_diveSound == -1 && a->m_riseSound == -1 && a->m_surfaceSound == -1 &&
                      a->m_underwaterSound == -1 && a->m_speed == 0.0f && a->m_turn == 0.0f && a->m_climb == 0.0f &&
                      a->m_forward == 0.0f && a->m_side == 0.0f && a->m_up == 0.0f && a->m_bob == 0.0f && !a->m_camera &&
                      a->m_volumes.empty() && !a->m_volumesActive && !a->m_body && !a->m_propeller && seatsEmpty(a) &&
                      GetSubmarines().size() == subs0 + 2 && GetSubmarines()[subs0] == a && GetSubmarines()[subs0 + 1] == b &&
                      a->GetSeatCount() == 2 && a->GetSpeed() == 0.0f);
        // (Destroyed: out of the list, the last one in its place.)
        auto *c = static_cast<C_Submarine *>(creator->Create());
        creator->Destroy(a);
        MA(r.sub, GetSubmarines().size() == subs0 + 2 && GetSubmarines()[subs0] == c && GetSubmarines()[subs0 + 1] == b);
        creator->Destroy(c);
        MA(r.sub, GetSubmarines().size() == subs0 + 1 && GetSubmarines()[subs0] == b);
        a = b;

        // SetFrame: the base's (false without a frame); the body and its place, the seats from the last
        // ("SEAT01") down to the first missing, their flags from "SEAT<digits>" (added), the propeller.
        I3D_frame *f = AnFrame(FRAME_MODEL, "ma.sub", nullptr);
        const float heading = rnd(-3, 3);
        f->m_localMatrix.m[0][0] = std::cos(heading);
        f->m_localMatrix.m[0][2] = -std::sin(heading);
        f->m_localMatrix.m[2][0] = std::sin(heading);
        f->m_localMatrix.m[2][2] = std::cos(heading);
        f->m_localMatrix(3) = S_vector(rnd(-30, 30), rnd(-5, 0), rnd(-30, 30));
        I3D_frame *body = AnFrame(FRAME_VISUAL, "BODY", f);
        body->m_localMatrix(3) = S_vector(0.0f, rnd(0.2f, 0.6f), rnd(-0.5f, 0.5f));
        body->m_localMatrix.m[3][3] = 1.0f;
        I3D_frame *seat0 = AnFrame(FRAME_DUMMY, "SEAT00", body);
        seat0->m_localMatrix(3) = S_vector(rnd(-0.5f, 0.5f), 0.8f, rnd(1, 2));
        seat0->m_userProps = "SEAT13 ,nam5,name7";
        seat0->m_localMatrix.m[3][3] = 1.5f;   // (an exit's w)
        I3D_frame *seat1 = AnFrame(FRAME_DUMMY, "SEAT01", body);
        seat1->m_localMatrix(3) = S_vector(rnd(-0.5f, 0.5f), 1.1f, rnd(-2, -1));
        seat1->m_userProps = "SEAX2 name9";      // (not "SEAT": no flags)
        I3D_frame *camera3rd = AnFrame(FRAME_DUMMY, "camera3RDa", seat0);
        I3D_frame *cameraFpv = AnFrame(FRAME_DUMMY, "cameraFPVb", body);
        I3D_frame *propeller = AnFrame(FRAME_DUMMY, "sroub", f);
        propeller->m_localMatrix(3) = S_vector(0.0f, 0.2f, -3.0f);
        I3D_frame *hull = AnFrame(FRAME_DUMMY, "BODYCOLB00", f);
        std::vector<I3D_frame *> boxes;
        for (int k = 0; k < 4; ++k) {
            I3D_frame *box = k == 2 ? RbBoxFrame(hull, "hullmesh", -0.4f, 0.4f, -0.3f, 0.5f, -1.0f, 1.0f, S_vector(0, 0, 0))
                                    : AnFrame(FRAME_DUMMY, "hullbox" + std::to_string(k), hull);
            if (k != 2)
                AnSetBBox(box, S_vector(rnd(-1.2f, -0.6f), rnd(-0.8f, -0.2f), rnd(-2.0f, -1.0f)),
                          S_vector(rnd(0.6f, 1.2f), rnd(0.2f, 0.8f), rnd(1.0f, 2.0f)));
            box->m_localMatrix(3) = S_vector(0.0f, 0.0f, -3.0f + 2.0f * k);
            boxes.push_back(box);
        }
        {
            auto *s = static_cast<C_Submarine *>(creator->Create());
            I3D_frame *bare = AnFrame(FRAME_MODEL, "ma.bare", nullptr);
            I3D_frame *only0 = AnFrame(FRAME_DUMMY, "SEAT00", bare);
            only0->m_userProps = "SEAT1";
            s->m_seats[1].flags = 0x40;
            MA(r.sub, !s->SetFrame(nullptr) && !s->GetFrame() && s->SetFrame(bare) && s->GetFrame() == bare && !s->m_body &&
                          !s->m_propeller && !s->m_seats[1].frame && !s->m_seats[0].frame && s->m_seats[1].flags == 0x40 &&
                          s->m_bodyPos.x == 0.0f && s->m_bodyPos.w == 0.0f);
            creator->Destroy(s);
        }
        a->m_seats[0].flags = 0x100;
        MA(r.sub, a->SetFrame(f) && a->GetFrame() == f && a->m_body == body && a->m_propeller == propeller &&
                      a->m_bodyPos.x == body->m_localMatrix.m[3][0] && a->m_bodyPos.y == body->m_localMatrix.m[3][1] &&
                      a->m_bodyPos.z == body->m_localMatrix.m[3][2] && a->m_bodyPos.w == 1.0f && a->m_seats[0].frame == seat0 &&
                      a->m_seats[1].frame == seat1 && a->m_seats[0].flags == (0x100u | 0x2u | 0x8u) && a->m_seats[1].flags == 0u &&
                      a->GetSeatFlags(0) == 0x10Au && a->GetSeatFlags(1) == 0u && a->GetSeatFlags(2) == 0 &&
                      a->GetSeatFlags(-1) == 0 && a->GetSeatText(0) == 7 && a->GetSeatText(1) == 9 && a->GetSeatText(2) == -1 &&
                      a->GetSeatText(-1) == -1);
        a->m_seats[0].flags = 0x0A;

        // ReadProperties: chunk 0 the version (2) first, then 1 the maximum water level; a short level
        // chunk: the version chunk's id and size; another id entered (the rest read inside it).
        const auto chunkOf = [](uint16_t id, const std::vector<uint8_t> &payload) {
            S_SaBytes x;
            x.Head(id, static_cast<uint32_t>(6 + payload.size()));
            x.Put(payload.data(), payload.size());
            return x.b;
        };
        const auto valueOf = [](auto value) {
            std::vector<uint8_t> v(sizeof value);
            std::memcpy(v.data(), &value, sizeof value);
            return v;
        };
        const auto wrap = [](const std::vector<std::vector<uint8_t>> &chunks) {
            S_SaBytes x;
            size_t n = 6;
            for (const auto &k : chunks)
                n += k.size();
            x.Head(0x777, static_cast<uint32_t>(n));
            for (const auto &k : chunks)
                x.Put(k.data(), k.size());
            return x;
        };
        for (int k = 0; k < 7; ++k) {
            std::vector<uint8_t> inner = chunkOf(1, valueOf(-7.5f));
            const S_SaBytes in = k == 0   ? wrap({chunkOf(0, valueOf(2)), chunkOf(1, valueOf(12.5f))})
                                 : k == 1 ? wrap({chunkOf(0, valueOf(2)), chunkOf(1, {1, 2}), chunkOf(5, valueOf(3.0f))})
                                 : k == 2 ? wrap({chunkOf(0, valueOf(2)), chunkOf(9, inner), chunkOf(1, valueOf(8.0f))})
                                 : k == 3 ? wrap({chunkOf(0, valueOf(3)), chunkOf(1, valueOf(12.5f))})
                                 : k == 4 ? wrap({chunkOf(1, valueOf(2)), chunkOf(1, valueOf(12.5f))})
                                 : k == 5 ? wrap({})
                                          : wrap({chunkOf(0, {2, 0})});
            S_SaReader rd(in);
            a->m_maxWaterLevel = 100.0f;
            const bool ok = a->ReadProperties(rd.c);
            uint32_t bits = 10u << 16;
            float quirk;
            std::memcpy(&quirk, &bits, 4);
            const float e = k == 0 ? 12.5f : k == 1 ? quirk : k == 2 ? -7.5f : 100.0f;
            MA(r.subProps, ok == (k < 3) && SaSame(&a->m_maxWaterLevel, &e, 4));
        }
        {
            // (A level chunk longer than its value: left, the next one read.)
            std::vector<uint8_t> longLevel = valueOf(12.5f);
            longLevel.insert(longLevel.end(), {9, 9, 9, 9});
            const S_SaBytes in = wrap({chunkOf(0, valueOf(2)), chunkOf(1, longLevel), chunkOf(1, valueOf(3.0f))});
            S_SaReader rd(in);
            MA(r.subProps, a->ReadProperties(rd.c) && a->m_maxWaterLevel == 3.0f);
        }
        a->m_maxWaterLevel = rnd(-1.0f, 2.0f) + f->m_localMatrix.m[3][1];

        // --- StartMission (a server): the hull's boxes (flags 0x42, type 1, from BODYCOLB00's
        // children), the sounds paused (chapter 8: groups 7, 0, 1 on the boat, 2 and 3 at the
        // propeller), the pilot seat's camera keeper; else only the activation.
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
            const S_CollisionVolume &x = v->volume;
            const S_vector cw = center * world;
            const float rr = most * radius;
            return x.flags == flags && x.frame == fr && x.type == type && x.actor == actor && RbSameBox(x.bbox, box) &&
                   SaSame(&x.radius, &rr, 4) && RbSame(x.center, cw) && x.matrix == &fr->m_worldMatrix &&
                   x.invMatrix == &v->invMatrix && SaSame(&v->invMatrix, &inv, sizeof inv);
        };
        std::map<int, I3D_sound *> subSounds;
        for (int slot = 40; slot < 45; ++slot) {
            subSounds[slot] = static_cast<I3D_sound *>(MakeSound());
            g_itNames[subSounds[slot]] = "slot" + std::to_string(slot);
            F_SetOn(subSounds[slot], slot % 2 == 0);
        }
        g_maSlotSounds = subSounds;
        g_soundSlot = 40;
        g_soundSlotStep = 1;
        g_soundRequests.clear();
        g_keepersMade.clear();
        reset();
        a->m_seats[0].occupant = a;
        a->m_seats[1].occupant = a;
        a->m_bob = 2.0f;
        a->StartMission(false);
        MA(r.subStart, g_activations == std::vector<bool>{false} && a->m_volumes.empty() && g_soundRequests.empty() &&
                           !a->m_camera && a->m_bob == 2.0f);
        reset();
        a->StartMission(true);
        bool hullOk = a->m_volumes.size() == 4 && g_volumeLog.size() == 8;
        for (size_t k = 0; hullOk && k < 4; ++k)
            hullOk = frameVolumeIs(a->m_volumes[k], boxes[k], 0x42, 1, a) && g_volumeLog[2 * k].op == 'A' &&
                     g_volumeLog[2 * k].volume == &a->m_volumes[k]->volume && g_volumeLog[2 * k + 1].op == 'M';
        bool requestsOk = g_soundRequests.size() == 5;
        const int groups[5] = {7, 0, 1, 2, 3};
        for (size_t k = 0; requestsOk && k < 5; ++k) {
            const S_SoundRequest &q = g_soundRequests[k];
            requestsOk = q.flags == 0xD && q.chapter == 8 && q.group == groups[k] && q.attachTo == (k < 3 ? f : propeller) &&
                         !q.source && !q.fileName && q.volume == 1.0f;
        }
        // (Paused: SetActive(false) for the ones playing, 40, 42, 44.)
        const std::vector<std::pair<const I3D_frame *, char>> pauses = {
            {subSounds[40], 'X'}, {subSounds[42], 'X'}, {subSounds[44], 'X'}};
        MA(r.subStart, g_activations == std::vector<bool>{true} && hullOk && a->m_volumesActive && requestsOk &&
                           a->m_engineSound == 40 && a->m_diveSound == 41 && a->m_riseSound == 42 && a->m_surfaceSound == 43 &&
                           a->m_underwaterSound == 44 && g_soundLog == pauses && !a->m_seats[0].occupant &&
                           !a->m_seats[1].occupant && a->m_bob == 0.0f && a->m_camera && g_keepersMade.size() == 1 &&
                           Keeper(a->m_camera).owner == a && Keeper(a->m_camera).frame == camera3rd);
        g_soundSlotStep = 0;

        // The seats: blocked flags; an occupant linked into its seat (the pilot's camera back first),
        // at its origin facing +z; FindSeat from the last; LeaveSeat: out into the primary sector (its
        // world kept), the pilot's controls let go, unblocked, null returned (quirk kept).
        {
            MA(r.subSeats, !a->IsSeatBlocked(0) && a->SetSeatBlocked(1, true) && a->IsSeatBlocked(1) && !a->SetSeatBlocked(2, true) &&
                               !a->SetSeatBlocked(-1, true) && !a->IsSeatBlocked(2) && !a->IsSeatBlocked(-1));
            C_Human pilot, gunner;
            I3D_frame *pf = AnFrame(FRAME_MODEL, "ma.pilot", nullptr);
            I3D_frame *gf = AnFrame(FRAME_MODEL, "ma.gunner", nullptr);
            pilot.m_frame = pf;
            gunner.m_frame = gf;
            pf->m_localMatrix(3) = S_vector(5, 6, 7);
            g_restored.clear();
            reset();
            a->SetSeatOccupant(0, &pilot);
            const size_t restoredAfterPilot = g_restored.size();
            a->SetSeatOccupant(1, &gunner);
            a->SetSeatOccupant(2, &pilot);
            a->SetSeatOccupant(-1, &gunner);
            MA(r.subSeats, a->GetSeatOccupant(0) == &pilot && a->GetSeatOccupant(1) == &gunner && !a->GetSeatOccupant(2) &&
                               !a->GetSeatOccupant(-1) && pf->m_parent == seat0 && gf->m_parent == seat1 &&
                               RbSame(pf->GetPos(), S_vector(0, 0, 0)) && RbSame(gf->GetPos(), S_vector(0, 0, 0)) &&
                               g_restored == std::vector<const C_CameraFrameKeeper *>{a->m_camera} && restoredAfterPilot == 1 &&
                               SaLogIs({"link ma.pilot SEAT00 0", "update ma.pilot", "worlddir ma.pilot 0 0 1 0", "update ma.pilot",
                                        "link ma.gunner SEAT01 0", "update ma.gunner", "worlddir ma.gunner 0 0 1 0",
                                        "update ma.gunner"}) &&
                               a->FindSeat(&pilot) == 0 && a->FindSeat(&gunner) == 1 && a->FindSeat(nullptr) == -1 &&
                               a->FindSeat(a) == -1);
            // A frameless occupant: set, not linked; none: the seat emptied.
            C_Human ghost;
            reset();
            a->SetSeatOccupant(1, &ghost);
            MA(r.subSeats, a->GetSeatOccupant(1) == &ghost && g_itLog.empty());
            a->SetSeatOccupant(1, nullptr);
            MA(r.subSeats, !a->GetSeatOccupant(1) && a->FindSeat(nullptr) == 1);
            a->SetSeatOccupant(1, &gunner);
            // The seats' cameras: the seat's own ("camera3RD*" / "cameraFPV*", the occupant unlinked
            // meanwhile, then back), else its parent's.
            g_firstPerson = false;
            reset();
            I3D_frame *cam0 = a->GetSeatCamera(0);
            g_firstPerson = true;
            I3D_frame *cam1 = a->GetSeatCamera(1);
            I3D_frame *camFpv0 = a->GetSeatCamera(0);
            g_firstPerson = false;
            I3D_frame *cam3rd1 = a->GetSeatCamera(1);
            MA(r.subSeats, cam0 == camera3rd && cam1 == cameraFpv && camFpv0 == cameraFpv && cam3rd1 == camera3rd &&
                               !a->GetSeatCamera(2) && !a->GetSeatCamera(-1) && pf->m_parent == seat0 && gf->m_parent == seat1 &&
                               SaLogIs({"link ma.pilot 0 0", "link ma.pilot SEAT00 0", "update ma.pilot", "link ma.gunner 0 0",
                                        "link ma.gunner SEAT01 0", "update ma.gunner", "link ma.pilot 0 0",
                                        "link ma.pilot SEAT00 0", "update ma.pilot", "link ma.gunner 0 0",
                                        "link ma.gunner SEAT01 0", "update ma.gunner"}));
            // LeaveSeat.
            a->SetControls(0.5f, -0.25f, 0.75f);
            a->SetSeatBlocked(0, true);
            a->SetSeatBlocked(1, true);
            g_restored.clear();
            reset();
            const S_vector pw = pf->GetWorldPos();
            MA(r.subSeats, !a->LeaveSeat(1) && !a->GetSeatOccupant(1) && !a->IsSeatBlocked(1) && a->m_forward == 0.5f &&
                               a->m_side == -0.25f && a->m_up == 0.75f && g_restored.empty() &&
                               SaLogIs({"link ma.gunner sector 1"}));
            reset();
            MA(r.subSeats, !a->LeaveSeat(0) && !a->GetSeatOccupant(0) && !a->IsSeatBlocked(0) && a->m_forward == 0.0f &&
                               a->m_side == 0.0f && a->m_up == 0.0f && g_restored.size() == 1 &&
                               SaLogIs({"link ma.pilot sector 1"}) && RbSame(pf->GetWorldPos(), pw) && !a->LeaveSeat(2) &&
                               !a->LeaveSeat(-1));
            // (An empty seat: still unblocked; the pilot's: the camera back again.)
            a->SetSeatBlocked(0, true);
            g_restored.clear();
            reset();
            MA(r.subSeats, !a->LeaveSeat(0) && !a->IsSeatBlocked(0) && g_restored.size() == 1 && g_itLog.empty());
            MA(r.subSeats, !a->ExitSeat(0) && !a->ExitSeat(2) && !a->ExitSeat(-1));
        }

        // --- the exits: a 0.25 m sphere moved 1.35 m sideways (the boat's level x axis; then the other
        // side, then the other seat's sides, its left first) from the seat's height and depth on the
        // boat's middle, 1 m up; the ground below (0.45 m under where a sphere from 1.5 m above stops);
        // the occupant there facing the seat, in the primary sector; then out of the seat.
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
        // (The level ones: x and z, y 0 then; a zero vector's y becomes 1.)
        const auto levelNormalized = [](S_vector v, float sq) {
            if (!(std::fabs(sq - 1.0f) < 1e-08f)) {
                if (!(sq < 1e-08f)) {
                    const float k = 1.0f / std::sqrt(sq);
                    v = S_vector(v.x * k, 0.0f * k, v.z * k);
                } else if (v.x != 0.0f) {
                    v.x = v.x < 0.0f ? -1.0f : 1.0f;
                } else if (v.z != 0.0f) {
                    v.z = v.z < 0.0f ? -1.0f : 1.0f;
                } else {
                    v.y = 1.0f;
                }
            }
            return v;
        };
        const auto line = [](const char *format, auto... args) {
            char text[256];
            std::snprintf(text, sizeof text, format, args...);
            return std::string(text);
        };
        const auto exitOracle = [&](C_Submarine *s, int seat, bool other, std::vector<S_MaSphere> &spheres,
                                    const std::set<int> &hits, S_vector4 &pos, S_vector4 &dir) {
            I3D_frame *seatFrame = s->m_seats[seat].frame;
            const S_matrix seatWorld = seatFrame->GetMatrix();
            S_vector local(seatWorld.m[3][0], seatWorld.m[3][1], seatWorld.m[3][2]);
            S_matrix inv;
            inv.Inverse(s->GetFrame()->GetMatrix());
            local *= inv;
            const S_matrix world = s->GetFrame()->GetMatrix();
            S_vector side = levelNormalized(S_vector(world.m[0][0], 0.0f, world.m[0][2]),
                                            world.m[0][2] * world.m[0][2] + world.m[0][0] * world.m[0][0]);
            normalizeBy(side, (side.z * side.z + side.x * side.x) + side.y * side.y);
            const float reach = other ? -1.35f : 1.35f;
            const S_vector offset(side.x * reach, reach * side.y, side.z * reach);
            S_vector start(0.0f, local.y, local.z);
            start *= world;
            spheres.push_back({S_vector(start.x, start.y + 1.0f, start.z), offset, 0.25f, 0xFFFFFFFFu, 0u});
            if (hits.count(static_cast<int>(spheres.size()) - 1))
                return false;
            S_vector end(offset.x + start.x, start.y + offset.y, start.z + offset.z);
            spheres.push_back({S_vector(end.x, end.y + 1.5f, end.z), S_vector(0.0f, -3.5f, 0.0f), 0.25f, 0xFFFFFFFFu, 0u});
            if (hits.count(static_cast<int>(spheres.size()) - 1))
                end = S_vector(g_sphereHookHit.x, g_sphereHookHit.y - 0.45f, g_sphereHookHit.z);
            pos = {end.x, end.y, end.z, seatWorld.m[3][3]};
            const S_vector seatPos = seatFrame->GetWorldPos();
            const S_vector facing = levelNormalized(S_vector(seatPos.x - end.x, 0.0f, seatPos.z - end.z),
                                                    (seatPos.x - end.x) * (seatPos.x - end.x) +
                                                        (seatPos.z - end.z) * (seatPos.z - end.z));
            dir = {facing.x, facing.y, facing.z, 0.0f};
            return true;
        };
        const auto sameSpheres = [](const std::vector<S_MaSphere> &x, const std::vector<S_MaSphere> &y) {
            if (x.size() != y.size())
                return false;
            for (size_t i = 0; i < x.size(); ++i)
                if (!RbSame(x[i].from, y[i].from) || !RbSame(x[i].dir, y[i].dir) || !SaSame(&x[i].radius, &y[i].radius, 4) ||
                    x[i].types != y[i].types || x[i].flags != y[i].flags)
                    return false;
            return true;
        };
        {
            C_Human pilot, gunner;
            I3D_frame *pf = AnFrame(FRAME_MODEL, "ma.pilot2", nullptr);
            I3D_frame *gf = AnFrame(FRAME_MODEL, "ma.gunner2", nullptr);
            pilot.m_frame = pf;
            gunner.m_frame = gf;
            g_sphereHookSetsHit = true;
            S_CollisionVolume wall;
            g_maSphereHit = &wall;
            const std::vector<std::set<int>> cases = {{}, {1}, {0}, {0, 1}, {0, 1, 2}, {0, 1, 2, 3}, {0, 2}};
            for (size_t k = 0; k < cases.size() * 2; ++k) {
                const std::set<int> &hits = cases[k % cases.size()];
                const bool usesGunner = k >= cases.size();
                C_Human &who = usesGunner ? gunner : pilot;
                const int seat = usesGunner ? 1 : 0;
                a->SetSeatOccupant(seat, &who);
                a->SetSeatBlocked(seat, true);
                a->SetControls(0.25f, 0.5f, -0.5f);
                g_sphereHookHit = S_vector(rnd(-30, 30), rnd(-3, 1), rnd(-30, 30));
                // The oracle: this seat's right then left; the other seat's left then right.
                std::vector<S_MaSphere> spheres;
                S_vector4 pos{}, dir{};
                bool found = exitOracle(a, seat, false, spheres, hits, pos, dir) || exitOracle(a, seat, true, spheres, hits, pos, dir);
                for (int o = 0; !found && o < 2; ++o)
                    found = o != seat && (exitOracle(a, o, true, spheres, hits, pos, dir) || exitOracle(a, o, false, spheres, hits, pos, dir));
                const std::string n = ItName(who.GetFrame());
                std::vector<std::string> log;
                if (found)
                    log = {"link " + n + " sector 0", line("setpos %s %s", n.c_str(), ItV(S_vector(pos.x, pos.y, pos.z)).c_str()),
                           line("worlddir %s %s 0", n.c_str(), ItV(S_vector(dir.x, dir.y, dir.z)).c_str()), "update " + n,
                           "link " + n + " sector 1"};
                g_maSphereHitsAt = hits;
                g_restored.clear();
                reset();
                const bool exited = a->ExitSeat(seat);
                g_maSphereHitsAt.clear();
                MA(r.subExit, exited == found && sameSpheres(g_maSpheres, spheres) && SaLogIs(log) &&
                                  (!found || (SaSame(&who.m_position, &pos, sizeof pos) && SaSame(&who.m_direction, &dir, sizeof dir) &&
                                              !a->GetSeatOccupant(seat) && !a->IsSeatBlocked(seat) &&
                                              (seat == 1 || (a->m_forward == 0.0f && g_restored.size() == 1)))) &&
                                  (found || (a->GetSeatOccupant(seat) == &who && a->IsSeatBlocked(seat))));
                a->LeaveSeat(seat);
            }
            {
                // (The ground met right under the seat: facing a zero vector, its y 1.)
                const S_vector sp = seat0->GetWorldPos();
                g_sphereHookHit = S_vector(sp.x, sp.y - 2.0f, sp.z);
                a->SetSeatOccupant(0, &pilot);
                std::vector<S_MaSphere> spheres;
                S_vector4 pos{}, dir{};
                const std::set<int> hits = {1};
                const bool found = exitOracle(a, 0, false, spheres, hits, pos, dir);
                g_maSphereHitsAt = hits;
                reset();
                const bool exited = a->ExitSeat(0);
                g_maSphereHitsAt.clear();
                MA(r.subExit, found && exited && dir.y == 1.0f && SaSame(&pilot.m_direction, &dir, sizeof dir) &&
                                  sameSpheres(g_maSpheres, spheres) &&
                                  std::find(g_itLog.begin(), g_itLog.end(), "worlddir " + ItName(pf) + " 0 1 0 0") != g_itLog.end());
            }
            // PutOutside: none, or not in a seat: false.
            reset();
            MA(r.subExit, !a->PutOutside(nullptr) && !a->PutOutside(&pilot) && g_maSpheres.empty() && g_itLog.empty());
            // FindExit of no seat: false.
            S_vector4 p4, d4;
            MA(r.subExit, !a->FindExit(2, &p4, &d4, false) && !a->FindExit(-1, &p4, &d4, true) && g_maSpheres.empty());
            // A boat turned upright (its x axis vertical: the level axis a zero vector, y 1).
            const S_matrix fLocal = f->m_localMatrix;
            f->m_localMatrix.m[0][0] = 0.0f;
            f->m_localMatrix.m[0][1] = 1.0f;
            f->m_localMatrix.m[0][2] = 0.0f;
            f->m_localMatrix.m[1][0] = -1.0f;
            f->m_localMatrix.m[1][1] = 0.0f;
            f->m_localMatrix.m[1][2] = 0.0f;
            std::vector<S_MaSphere> spheres;
            S_vector4 pos{}, dir{};
            const bool e1 = exitOracle(a, 0, true, spheres, {}, pos, dir);
            reset();
            MA(r.subExit, a->FindExit(0, &p4, &d4, true) == e1 && sameSpheres(g_maSpheres, spheres) &&
                              SaSame(&p4, &pos, sizeof pos) && SaSame(&d4, &dir, sizeof dir));
            f->m_localMatrix = fLocal;
            g_sphereHookSetsHit = false;
        }

        // --- the movement (from the asm): Bob (a server: the body up and down and rolling with the
        // game's time), the controls, the move (a server; level along the heading, turned by the turn
        // times the speed, climbing up to the highest level), the hull's boxes swept (a hit turns it
        // away by the speed and puts it back; turning, it turns by the speed's size and tries again),
        // the frame, the boxes, the propeller, the sounds, the camera keeper.
        const auto isServer = [] { return (g_gameRole & GAME_ROLE_SERVER) != 0; };
        const auto heading4 = [&](I3D_frame *fr) {
            const S_matrix w = fr->GetMatrix();
            S_vector z(w.m[2][0], w.m[2][1], w.m[2][2]);
            normalizeBy(z, (z.y * z.y + z.z * z.z) + z.x * z.x);
            return S_vector4{z.x, z.y, z.z, w.m[2][3]};
        };
        struct S_SubExpect {
            std::vector<std::string> log;
            std::vector<S_MaSphere> spheres;
            std::vector<std::pair<char, const S_CollisionVolume *>> volumes;
            std::vector<std::pair<const I3D_frame *, char>> sounds;
            std::vector<std::pair<const I3D_frame *, float>> frequencies;
            float speed = 0, turn = 0, climb = 0, bob = 0;
            S_vector4 position{}, direction{};
            bool active = false;
            std::vector<S_vector> centers;
            std::vector<S_matrix> invs;
            S_quat propRot{};
            bool propTurned = false;
            S_matrix frameLocal{}, bodyLocal{};
        };
        const auto subUpdate = [&](C_Submarine *s, int ft, const std::set<int> &hits, bool tick) {
            S_SubExpect e;
            I3D_frame *fr = s->GetFrame();
            const S_matrix frameLocal0 = fr->m_localMatrix;
            const S_matrix bodyLocal0 = s->m_body ? s->m_body->m_localMatrix : S_matrix{};
            e.speed = s->m_speed;
            e.turn = s->m_turn;
            e.climb = s->m_climb;
            e.bob = s->m_bob;
            e.position = s->m_position;
            e.direction = s->m_direction;
            e.active = s->m_volumesActive;
            for (const S_FrameVolume *v : s->m_volumes) {
                e.centers.push_back(v->volume.center);
                e.invs.push_back(v->invMatrix);
            }
            const bool client = (g_gameRole & GAME_ROLE_CLIENT) != 0;
            if (tick && (client || !isServer())) {
                e.frameLocal = fr->m_localMatrix;
                return e;
            }
            // Bob.
            if (isServer() && s->m_body) {
                const uint32_t time = GetGame()->GetTime();
                const float bobAngle = static_cast<float>(static_cast<double>(time) * 0.002f);
                e.bob = static_cast<float>(std::cos(static_cast<double>(bobAngle)) * 0.05f);
                const float rollAngle = static_cast<float>(static_cast<double>(time) * 0.001f);
                const float roll = static_cast<float>(std::sin(static_cast<double>(rollAngle)) * 0.01884955660992227);
                e.log.push_back(line("worlddir %s 0 0 1 %.9g", ItName(s->m_body).c_str(), roll));
                s->m_body->m_localMatrix(3) = S_vector(s->m_bodyPos.x, e.bob + s->m_bodyPos.y, s->m_bodyPos.z);
                e.log.push_back("update " + ItName(s->m_body));
            }
            // The controls.
            const float time = static_cast<float>(static_cast<uint32_t>(ft)) * 0.001f;
            const auto controlled = [&](float &value, float control, float rate, float decay, float limit, bool clampAlways) {
                if (control != 0.0f) {
                    value = time * control * rate + value;
                    if (!clampAlways) {
                        if (value > limit)
                            value = limit;
                        if (value < -limit)
                            value = -limit;
                    }
                } else if (value > 0.0f) {
                    value = value - time * decay;
                    if (value < 0.0f)
                        value = 0.0f;
                } else if (value < 0.0f) {
                    value = time * decay + value;
                    if (!(value <= 0.0f))
                        value = 0.0f;
                }
            };
            // (Speed and climb: time * control + value, rate and decay 1, clamped when controlled.)
            {
                float v = e.speed;
                if (s->m_forward != 0.0f) {
                    v = time * s->m_forward + v;
                    if (v > 6.0f)
                        v = 6.0f;
                    if (v < -6.0f)
                        v = -6.0f;
                } else if (v > 0.0f) {
                    v = v - time;
                    if (v < 0.0f)
                        v = 0.0f;
                } else if (v < 0.0f) {
                    v = time + v;
                    if (!(v <= 0.0f))
                        v = 0.0f;
                }
                e.speed = v;
                float c = e.climb;
                if (s->m_up != 0.0f) {
                    c = time * s->m_up + c;
                    if (c > 1.0f)
                        c = 1.0f;
                    if (c < -1.0f)
                        c = -1.0f;
                } else if (c > 0.0f) {
                    c = c - time;
                    if (c < 0.0f)
                        c = 0.0f;
                } else if (c < 0.0f) {
                    c = time + c;
                    if (!(c <= 0.0f))
                        c = 0.0f;
                }
                e.climb = c;
                controlled(e.turn, s->m_side, 0.1f, 0.5f, 0.5f, true);
                float t = e.turn > 0.5f ? 0.5f : e.turn;
                if (t < -0.5f)
                    t = -0.5f;
                e.turn = t;
            }
            // The move.
            const auto setVolumes = [&](bool on) {
                if (e.active == on)
                    return;
                for (const S_FrameVolume *v : s->m_volumes) {
                    if (on) {
                        e.volumes.push_back({'A', &v->volume});
                        e.volumes.push_back({'M', &v->volume});
                    } else {
                        e.volumes.push_back({'R', &v->volume});
                    }
                }
                e.active = on;
            };
            const auto placeFrame = [&] {
                fr->m_localMatrix(3) = S_vector(e.position.x, e.position.y, e.position.z);
                e.log.push_back(line("setpos %s %s", ItName(fr).c_str(), ItV(S_vector(e.position.x, e.position.y, e.position.z)).c_str()));
                e.log.push_back(line("setdir %s %s 0", ItName(fr).c_str(), ItV(S_vector(e.direction.x, e.direction.y, e.direction.z)).c_str()));
                e.log.push_back("update " + ItName(fr));
            };
            const auto check = [&]() {
                const S_matrix w = fr->GetMatrix();
                const S_vector4 oldPos = {w.m[3][0], w.m[3][1], w.m[3][2], w.m[3][3]};
                const S_vector4 oldDir = heading4(fr);
                placeFrame();
                setVolumes(false);
                S_vector center(0.0f, 0.0f, 0.0f);
                for (size_t i = 1; i < s->m_volumes.size(); ++i) {
                    const S_FrameVolume *v = s->m_volumes[i];
                    I3D_frame *bf = v->volume.frame;
                    if (bf->GetType() == FRAME_DUMMY) {
                        const I3D_bbox &box = static_cast<I3D_dummy *>(bf)->GetBBox();
                        center = S_vector((box.min.x + box.max.x) * 0.5f, (box.max.y + box.min.y) * 0.5f,
                                          (box.max.z + box.min.z) * 0.5f);
                        center *= bf->GetMatrix();
                    }
                    const S_vector &from = v->volume.center;
                    S_vector move(center.x - from.x, center.y - from.y, center.z - from.z);
                    e.spheres.push_back({from, move, v->volume.radius, 2u, 0x10000u});
                    if (!hits.count(static_cast<int>(e.spheres.size()) - 1))
                        continue;
                    S_vector normal = g_sphereHookNormal;
                    normal.y = 0.0f;
                    move.y = 0.0f;
                    const float away = time * e.speed * 0.1f;
                    if (!(move.z * normal.x - normal.z * move.x > 0.0f))
                        e.turn = away + e.turn;
                    else
                        e.turn = e.turn - away;
                    e.position = oldPos;
                    e.direction = oldDir;
                    placeFrame();
                    setVolumes(true);
                    return false;
                }
                setVolumes(true);
                return true;
            };
            if (isServer() && (e.speed != 0.0f || e.climb != 0.0f)) {
                const S_matrix w = fr->GetMatrix();
                e.position = {w.m[3][0], w.m[3][1], w.m[3][2], w.m[3][3]};
                e.direction = heading4(fr);
                e.direction.y = 0.0f;
                S_vector h(e.direction.x, e.direction.y, e.direction.z);
                normalizeBy(h, (h.x * h.x + h.y * h.y) + h.z * h.z);
                e.direction = {h.x, h.y, h.z, e.direction.w};
                const float step = time * e.speed;
                e.position.x = step * e.direction.x + e.position.x;
                e.position.y = step * e.direction.y + e.position.y;
                e.position.z = step * e.direction.z + e.position.z;
                if (e.turn != 0.0f) {
                    const S_vector t = S_vector(e.direction.x, e.direction.y, e.direction.z)
                                           .RotateByMatrix(RotateYMatrix(time * e.turn * e.speed * 0.2f));
                    e.direction = {t.x, t.y, t.z, e.direction.w};
                }
                if (e.climb != 0.0f) {
                    e.position.y = time * e.climb + e.position.y;
                    if (e.position.y > s->m_maxWaterLevel)
                        e.position.y = s->m_maxWaterLevel;
                }
                if (!check() && e.turn != 0.0f) {
                    const float angle =
                        static_cast<float>(static_cast<double>(time * e.turn * std::fabs(e.speed)) * 0.20000000298023224);
                    const S_vector t = S_vector(e.direction.x, e.direction.y, e.direction.z).RotateByMatrix(RotateYMatrix(angle));
                    e.direction = {t.x, t.y, t.z, e.direction.w};
                    check();
                }
                placeFrame();
                // UpdateVolumes (from the last; a box that is not a dummy keeps the centre before it).
                S_vector center(0.0f, 0.0f, 0.0f);
                for (size_t i = s->m_volumes.size(); i-- > 0;) {
                    I3D_frame *bf = s->m_volumes[i]->volume.frame;
                    if (bf->GetType() == FRAME_DUMMY) {
                        const I3D_bbox &box = static_cast<I3D_dummy *>(bf)->GetBBox();
                        center = S_vector((box.min.x + box.max.x) * 0.5f, (box.max.y + box.min.y) * 0.5f,
                                          (box.max.z + box.min.z) * 0.5f);
                        center *= bf->GetMatrix();
                    }
                    e.invs[i].Inverse(bf->GetMatrix());
                    e.centers[i] = center;
                    e.volumes.push_back({'M', &s->m_volumes[i]->volume});
                }
                if (std::fabs(e.speed) > 0.01f && s->m_propeller) {
                    e.propRot = SaNormalized(s->m_propeller->GetRot().RotateByMatrix(RotateZMatrix(e.speed * -0.04f)));
                    e.propTurned = true;
                    e.log.push_back("update " + ItName(s->m_propeller));
                }
            }
            // The sounds.
            if (isServer()) {
                const auto activeOk = [&](int slot, bool on) {
                    if (slot == -1 || !g_maSlotSounds.count(slot) || !g_maSlotSounds[slot])
                        return false;
                    I3D_sound *snd = g_maSlotSounds[slot];
                    if (snd->IsOn() != on)
                        e.sounds.push_back({snd, 'X'});
                    return true;
                };
                const auto pitch = [&](int slot, float frequency) {
                    I3D_sound *snd = g_maSlotSounds[slot];
                    if (!(std::fabs(snd->GetFrequencyInline() - frequency) <= 0.05f)) {
                        e.sounds.push_back({snd, 'F'});
                        e.frequencies.push_back({snd, frequency});
                    }
                };
                if (std::fabs(e.speed) > 0.01f) {
                    if (activeOk(s->m_engineSound, true))
                        pitch(s->m_engineSound, (std::fabs(e.speed * 0.16666667f) + 1.0f) * 0.5f);
                    if (std::fabs(e.speed) > 2.4f) {
                        if (std::fabs(e.position.y - s->m_maxWaterLevel) > 1.0f) {
                            activeOk(s->m_surfaceSound, false);
                            if (activeOk(s->m_underwaterSound, true))
                                pitch(s->m_underwaterSound, std::fabs(e.speed * 0.16666667f) * 0.3f + 0.7f);
                        } else {
                            activeOk(s->m_surfaceSound, true);
                            activeOk(s->m_underwaterSound, false);
                        }
                    } else {
                        activeOk(s->m_surfaceSound, false);
                        activeOk(s->m_underwaterSound, false);
                    }
                } else {
                    activeOk(s->m_engineSound, false);
                    activeOk(s->m_surfaceSound, false);
                    activeOk(s->m_underwaterSound, false);
                }
                if (e.climb == 0.0f) {
                    activeOk(s->m_riseSound, false);
                    activeOk(s->m_diveSound, false);
                } else if (e.climb > 0.0f) {
                    activeOk(s->m_riseSound, true);
                    activeOk(s->m_diveSound, false);
                } else {
                    activeOk(s->m_riseSound, false);
                    activeOk(s->m_diveSound, true);
                }
            }
            e.frameLocal = fr->m_localMatrix;
            if (s->m_body)
                e.bodyLocal = s->m_body->m_localMatrix;
            fr->m_localMatrix = frameLocal0;
            if (s->m_body)
                s->m_body->m_localMatrix = bodyLocal0;
            return e;
        };
        const auto subUpdateOk = [&](C_Submarine *s, const S_SubExpect &e, int ft, bool tick) {
            bool ok = SaLogIs(e.log) && sameSpheres(g_maSpheres, e.spheres) && g_soundLog == e.sounds &&
                      SaSame(&s->m_speed, &e.speed, 4) && SaSame(&s->m_turn, &e.turn, 4) && SaSame(&s->m_climb, &e.climb, 4) &&
                      SaSame(&s->m_bob, &e.bob, 4) && SaSame(&s->m_position, &e.position, sizeof e.position) &&
                      SaSame(&s->m_direction, &e.direction, sizeof e.direction) && s->m_volumesActive == e.active &&
                      g_volumeLog.size() == e.volumes.size() &&
                      SaSame(&s->GetFrame()->m_localMatrix, &e.frameLocal, sizeof e.frameLocal);
            for (size_t i = 0; ok && i < e.volumes.size(); ++i)
                ok = g_volumeLog[i].op == e.volumes[i].first && g_volumeLog[i].volume == e.volumes[i].second;
            for (size_t i = 0; ok && i < s->m_volumes.size(); ++i)
                ok = RbSame(s->m_volumes[i]->volume.center, e.centers[i]) &&
                     SaSame(&s->m_volumes[i]->invMatrix, &e.invs[i], sizeof e.invs[i]);
            for (const auto &fq : e.frequencies)
                ok = ok && g_sounds[fq.first].frequency == fq.second;
            if (e.propTurned)
                ok = ok && SaSameQuat(s->m_propeller->m_rot, e.propRot);
            if (s->m_body && !e.log.empty())
                ok = ok && SaSame(&s->m_body->m_localMatrix, &e.bodyLocal, sizeof e.bodyLocal);
            const bool updates = !tick || (!(g_gameRole & GAME_ROLE_CLIENT) && isServer());
            ok = ok && g_keeperUpdates.size() == (updates && s->m_camera ? 1u : 0u) &&
                 (g_keeperUpdates.empty() || (g_keeperUpdates[0].first == s->m_camera && g_keeperUpdates[0].second == ft));
            return ok;
        };
        {
            // Slots 40..44 (on: 40, 42, 44); their pitches set near the first ones the engine asks.
            g_maSlotSounds = subSounds;
            *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(subSounds[40]) + 0x174) = 0.62f;
            *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(subSounds[44]) + 0x174) = 0.79f;
            g_sphereHookSetsHit = true;
            S_CollisionVolume wall;
            g_maSphereHit = &wall;
            const uint8_t roles[] = {GAME_ROLE_SERVER | GAME_ROLE_CLIENT, GAME_ROLE_SERVER, GAME_ROLE_CLIENT};
            int step = 0;
            for (uint8_t role : roles) {
                g_gameRole = role;
                // (forward, side, up, time, hits): speeding up, turning, climbing to the top, a box
                // meeting a wall (left and right of the move, while turning: the second sweep too), a
                // fast run under water and at the surface, letting go.
                // (special: 1 fast, 0.75 m below the top: at the surface; 2 fast, 2 m above the top:
                // under water (by the distance's size); 3 the engine's slot without a sound; 4 the boxes
                // out of the world already; 5 the boat's z axis straight up (its level heading a zero
                // vector: y 1); 6 its z axis nearly up (a level square between 1e-8 and 1e-2); 7 pitched
                // (its heading's y and w).)
                struct S_SubStep {
                    float forward, side, up;
                    int time;
                    std::set<int> hits;
                    int special = 0;
                };
                const std::vector<S_SubStep> steps = {
                    {1.0f, 0.0f, 0.0f, 500, {}},    {1.0f, 0.5f, 0.0f, 1000, {}},   {1.0f, -1.0f, 1.0f, 2000, {}},
                    {1.0f, 0.0f, 1.0f, 3000, {1}},  {1.0f, 1.0f, -1.0f, 700, {0}},  {1.0f, 1.0f, 0.0f, 600, {0, 3}},
                    {-1.0f, -0.5f, -1.0f, 900, {2}}, {0.0f, 0.0f, 0.0f, 400, {}},   {0.0f, 0.0f, 0.0f, 5000, {}},
                    {-1.0f, 0.0f, 0.0f, 3000, {}},  {0.0f, 0.0f, 0.0f, 16, {}},     {0.0f, 0.0f, 0.0f, 9000, {}},
                    {0.0f, 0.0f, 1.0f, 500, {}},    {0.0f, 0.0f, 1.0f, 500, {0}},   {0.0f, 0.0f, 0.0f, 9000, {}},
                    {-1.0f, 0.0f, 0.0f, 9000, {}},  {-1.0f, 0.5f, 0.0f, 700, {1}},  {0.0f, 0.0f, 0.0f, 9000, {}},
                    {1.0f, 0.0f, 0.0f, 300, {}, 1}, {1.0f, 0.0f, 0.0f, 300, {}, 2}, {1.0f, 0.0f, 0.0f, 300, {}, 3},
                    {1.0f, 0.3f, 0.0f, 300, {}, 4}, {1.0f, 0.0f, 0.0f, 300, {}, 5}, {1.0f, 0.0f, 0.0f, 300, {}, 6},
                    {1.0f, 0.0f, 0.0f, 300, {}, 7},
                    {1.0f, 0.2f, 0.0f, 9000, {}},   {1.0f, 0.0f, 1.0f, 9000, {}},   {0.0f, 0.0f, 0.0f, 0, {}}};
                F_SetOn(subSounds[41], true);
                for (const S_SubStep &st : steps) {
                    a->SetControls(st.forward, st.side, st.up);
                    GetGame()->m_time = static_cast<uint32_t>(1000 + 777 * step++);
                    const bool tick = st.special == 0 && step % 4 == 0;
                    const S_matrix fLocal0 = f->m_localMatrix;
                    const float level0 = a->m_maxWaterLevel;
                    const int engine0 = a->m_engineSound;
                    if (st.special == 1 || st.special == 2) {
                        a->m_speed = 4.0f;
                        a->m_climb = 0.0f;
                        a->m_maxWaterLevel = f->GetWorldPos().y + (st.special == 1 ? 0.75f : -2.0f);
                    } else if (st.special == 3) {
                        a->m_speed = 4.0f;
                        a->m_engineSound = 99;
                    } else if (st.special == 4) {
                        a->m_volumesActive = false;
                    } else if (st.special == 5) {
                        f->m_localMatrix(0) = S_vector(1.0f, 0.0f, 0.0f);
                        f->m_localMatrix(1) = S_vector(0.0f, 0.0f, -1.0f);
                        f->m_localMatrix(2) = S_vector(0.0f, 1.0f, 0.0f);
                    } else if (st.special == 6) {
                        f->m_localMatrix(0) = S_vector(1.0f, 0.0f, 0.0f);
                        f->m_localMatrix(1) = S_vector(0.0f, -0.04f, 0.9987f);
                        f->m_localMatrix(2) = S_vector(0.03f, 0.9987f, 0.04f);
                    } else if (st.special == 7) {
                        f->m_localMatrix(2) = S_vector(f->m_localMatrix.m[2][0], 0.2f, f->m_localMatrix.m[2][2]);
                        f->m_localMatrix.m[2][3] = 0.25f;
                    }
                    if (st.special != 0)
                        a->m_speed = a->m_speed == 0.0f ? 2.0f : a->m_speed;
                    g_sphereHookNormal = S_vector(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
                    g_maSphereHitsAt = st.hits;
                    const S_SubExpect e = subUpdate(a, st.time, st.hits, tick);
                    g_keeperUpdates.clear();
                    reset();
                    if (tick)
                        a->Tick(st.time);
                    else
                        a->Update(st.time);
                    g_maSphereHitsAt.clear();
                    MA(r.subMove, subUpdateOk(a, e, st.time, tick));
                    // (The frame's turn back; its place kept.)
                    if (st.special >= 5) {
                        const S_vector at = f->m_localMatrix(3);
                        f->m_localMatrix = fLocal0;
                        f->m_localMatrix(3) = at;
                    }
                    a->m_maxWaterLevel = level0;
                    a->m_engineSound = engine0;
                    if (st.special == 4)
                        a->m_volumesActive = true;          // (a pure client did not move them)
                }
            }
            g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
            g_sphereHookSetsHit = false;
        }

        // The saved game: the base's, then chunks 0x457 the speed, 0x8AE the turn, 0xD05 the climb.
        // LoadGame: the frame's place and heading taken, the boxes moved, the chunks (another id entered
        // and not left; a short chunk 0 here, an unset local in the original).
        {
            a->m_speed = 2.5f;
            a->m_turn = -0.25f;
            a->m_climb = 0.75f;
            S_SaBytes sv;
            sv.Head(0x777, 6 + 10 + 30).Head(0x3039, 10).Val(g_actorSaveTag).Head(0x457, 10).Val(2.5f).Head(0x8AE, 10).Val(-0.25f)
                .Head(0xD05, 10).Val(0.75f);
            bool saved = false;
            MA(r.subSave, SaWritten([&](C_chunk &k) { return a->SaveGame(k); }, &saved) == sv.b && saved);
            for (int k = 0; k < 3; ++k) {
                S_SaBytes ld;
                if (k == 0)
                    ld.Head(0x777, 6 + 10 + 30).Head(0x3039, 10).Val(5).Head(0xD05, 10).Val(-1.0f).Head(0x457, 10).Val(4.0f)
                        .Head(0x8AE, 10).Val(0.125f);
                else if (k == 1)
                    // (In the unknown chunk a turn chunk too short for its own header: 0.)
                    ld.Head(0x777, 6 + 10 + 8 + 10).Head(0x3039, 10).Val(5).Head(0x457, 8).Val(static_cast<uint16_t>(7))
                        .Head(0x99, 10).Head(0x8AE, 4);
                else
                    ld.Head(0x777, 6 + 10 + 16).Head(0x3039, 10).Val(5).Head(0x55, 16).Head(0xD05, 10).Val(0.5f);
                S_SaReader rd(ld);
                a->m_speed = 1.0f;
                a->m_turn = 1.0f;
                a->m_climb = 1.0f;
                a->m_position = {};
                const S_matrix w = f->GetMatrix();
                const S_vector4 pos = {w.m[3][0], w.m[3][1], w.m[3][2], w.m[3][3]};
                const S_vector4 dir = heading4(f);
                reset();
                const bool ok = a->LoadGame(rd.c);
                bool moved = g_volumeLog.size() == a->m_volumes.size();
                for (size_t i = 0; moved && i < g_volumeLog.size(); ++i)
                    moved = g_volumeLog[i].op == 'M' && g_volumeLog[i].volume == &a->m_volumes[a->m_volumes.size() - 1 - i]->volume;
                if (std::getenv("CAR_TRACE"))
                    std::printf("  sub load %d: ok %d pos %d dir %d moved %d (%zu) speed %g turn %g climb %g\n", k, ok,
                                SaSame(&a->m_position, &pos, sizeof pos), SaSame(&a->m_direction, &dir, sizeof dir), moved,
                                g_volumeLog.size(), a->m_speed, a->m_turn, a->m_climb);
                MA(r.subSave, ok && SaSame(&a->m_position, &pos, sizeof pos) && SaSame(&a->m_direction, &dir, sizeof dir) && moved &&
                                  (k == 0 ? a->m_speed == 4.0f && a->m_turn == 0.125f && a->m_climb == -1.0f
                                   : k == 1 ? a->m_speed == 0.0f && a->m_turn == 0.0f && a->m_climb == 1.0f
                                            : a->m_speed == 1.0f && a->m_turn == 1.0f && a->m_climb == 0.5f));
            }
            {
                // (A turn chunk longer than its value: left, the climb after it read.)
                S_SaBytes ld;
                ld.Head(0x777, 6 + 10 + 14 + 10).Head(0x3039, 10).Val(5).Head(0x8AE, 14).Val(0.375f).Val(0x01020304)
                    .Head(0xD05, 10).Val(-0.5f);
                S_SaReader rd(ld);
                a->m_turn = 1.0f;
                a->m_climb = 1.0f;
                MA(r.subSave, a->LoadGame(rd.c) && a->m_turn == 0.375f && a->m_climb == -0.5f);
            }
        }

        // StopMission: on a server only the server's side (a client-only call: the deactivation); the
        // body back, everything reset (the boxes out, the seats left, unblocked), the sounds stopped,
        // the camera keeper gone (restored). RestartMission: the slots forgotten; a client's side
        // stopped, a server's stopped and started, a client's started.
        {
            C_Human pilot, gunner;
            I3D_frame *pf = AnFrame(FRAME_MODEL, "ma.pilot3", nullptr);
            I3D_frame *gf = AnFrame(FRAME_MODEL, "ma.gunner3", nullptr);
            pilot.m_frame = pf;
            gunner.m_frame = gf;
            a->SetSeatOccupant(0, &pilot);
            a->SetSeatOccupant(1, &gunner);
            a->SetSeatBlocked(1, true);
            a->SetControls(1.0f, 1.0f, 1.0f);
            a->m_climb = 0.6f;
            body->m_localMatrix(3) = S_vector(9, 9, 9);
            g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
            reset();
            a->StopMission(false);
            MA(r.subStart, g_deactivations == std::vector<bool>{false} && g_itLog.empty() && a->m_camera && a->m_volumesActive);
            std::vector<const S_CollisionVolume *> volumes;
            for (size_t i = a->m_volumes.size(); i-- > 0;)
                volumes.push_back(&a->m_volumes[i]->volume);
            const C_CameraFrameKeeper *keeper = a->m_camera;
            g_restored.clear();
            g_keepersDeleted.clear();
            reset();
            a->StopMission(true);
            bool removed = g_volumeLog.size() == volumes.size();
            for (size_t i = 0; removed && i < volumes.size(); ++i)
                removed = g_volumeLog[i].op == 'R' && g_volumeLog[i].volume == volumes[i];
            MA(r.subStart, g_deactivations == std::vector<bool>{true} && removed && a->m_volumes.empty() && !a->m_volumesActive &&
                               !a->m_camera && g_keepersDeleted == std::vector<const C_CameraFrameKeeper *>{keeper} &&
                               g_restored.size() == 2 && !a->GetSeatOccupant(0) && !a->GetSeatOccupant(1) &&
                               !a->IsSeatBlocked(1) && a->m_speed == 0.0f && a->m_forward == 0.0f && a->m_climb == 0.0f &&
                               RbSame(body->GetPos(), S_vector(a->m_bodyPos.x, a->m_bodyPos.y, a->m_bodyPos.z)) &&
                               SaLogIs({"update BODY", "link ma.gunner3 sector 1", "link ma.pilot3 sector 1", "stopsound 40",
                                        "stopsound 41", "stopsound 42", "stopsound 43", "stopsound 44"}));
            // (A pure client's StopMission(false): the whole stop.)
            const int slots0[5] = {a->m_engineSound, a->m_diveSound, a->m_riseSound, a->m_surfaceSound, a->m_underwaterSound};
            a->StartMission(true);
            a->m_engineSound = slots0[0];
            a->m_diveSound = slots0[1];
            a->m_riseSound = slots0[2];
            a->m_surfaceSound = slots0[3];
            a->m_underwaterSound = slots0[4];
            g_gameRole = GAME_ROLE_CLIENT;
            reset();
            a->StopMission(false);
            MA(r.subStart, g_deactivations == std::vector<bool>{false} && a->m_volumes.empty() && !a->m_camera &&
                               !g_itLog.empty() && g_itLog[0] == "update BODY");
            g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
            // (A slot of -1 is not stopped.)
            a->m_diveSound = -1;
            reset();
            a->StopMission(true);
            MA(r.subStart, SaLogIs({"update BODY", "stopsound 40", "stopsound 42", "stopsound 43", "stopsound 44"}));
            for (uint8_t role : {static_cast<uint8_t>(GAME_ROLE_SERVER | GAME_ROLE_CLIENT), static_cast<uint8_t>(GAME_ROLE_SERVER),
                                 static_cast<uint8_t>(GAME_ROLE_CLIENT)}) {
                g_gameRole = role;
                g_soundSlot = 60;
                g_soundRequests.clear();
                reset();
                a->RestartMission(true);
                const bool server = (role & GAME_ROLE_SERVER) != 0, client = (role & GAME_ROLE_CLIENT) != 0;
                std::vector<bool> acts, deacts;
                if (client)
                    deacts.push_back(false);
                if (server) {
                    deacts.push_back(true);
                    acts.push_back(true);
                }
                if (client)
                    acts.push_back(false);
                bool none = true;
                for (const std::string &x : g_itLog)
                    none = none && x.compare(0, 9, "stopsound") != 0;
                MA(r.subStart, g_activations == acts && g_deactivations == deacts && none &&
                                   g_soundRequests.size() == (server ? 5u : 0u) &&
                                   a->m_engineSound == (server ? 60 : -1) && a->m_underwaterSound == (server ? 60 : -1));
                a->StopMission(true);
            }
            g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
        }
        // Bob without a body: nothing.
        {
            I3D_frame *body0 = a->m_body;
            a->m_body = nullptr;
            a->m_bob = 0.5f;
            reset();
            a->Bob(1234);
            MA(r.subMove, a->m_bob == 0.5f && g_itLog.empty());
            a->m_body = body0;
        }
        creator->Destroy(a);
        g_textIds = false;
        g_maSlotSounds.clear();
    }
