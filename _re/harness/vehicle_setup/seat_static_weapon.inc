// (seat_actors_test.inc, inside SeatActorsTest: the static weapon.)

    // --- the static weapon ------------------------------------------------------------------------------------
    {
        struct S_SeComp : public C_ItemComponent {
            explicit S_SeComp(E_ItemComponentKind kind) : C_ItemComponent(kind) {}
            void Save(int *, void *) override {}
            void Load(int, const void *) override {}
        };
        C_ActorCreator *creator = g_actorCreators[ACTOR_STATIC_WEAPON];
        SE(r.sw, creator && std::strcmp(creator->GetName(), "StaticWeapon") == 0);
        g_weaponEvents.clear();
        auto *a = static_cast<C_StaticWeapon *>(creator->Create());
        SE(r.sw, a->GetType() == ACTOR_STATIC_WEAPON && a->GetState() == 3 && !a->m_started && a->m_seats.empty() &&
                     !a->m_volumeAdded && !a->m_destroyed && !a->m_destroyedShown && !a->m_storeChanged && !a->m_netObject &&
                     a->GetHitPoints() == 100 && a->GetSeatCount() == 0 && !a->SetFrame(nullptr) &&
                     g_weaponEvents == std::vector<std::string>{"make"});
        C_FakeDef def(41);
        S_SeComp gunComp(ICK_FIREARM), launcherComp(ICK_PROJECTILE_WEAPON);
        def.m_component = &gunComp;
        g_weaponDef = &def;
        C_FakeStore store;
        store.count = 6;
        C_ItemContainer *containers[1] = {&store};
        a->m_containers = containers;
        a->SetNetId(77);

        // The gun: placed, turned, scaled (its own scale: no parent); "SEAT00" (flags 1, 4; text 12;
        // first person only), "SEAT01" (flag 2; text 3), no "SEAT02" ("SEAT03" not found); the cameras;
        // the parts: the barrel (with its sight), the shield, a part flagged 0x1000 (left out of the box).
        I3D_frame *f = AnFrame(FRAME_MODEL, "se.gun", nullptr);
        const float heading = rnd(-3, 3);
        f->m_localMatrix.m[0][0] = std::cos(heading);
        f->m_localMatrix.m[0][2] = -std::sin(heading);
        f->m_localMatrix.m[2][0] = std::sin(heading);
        f->m_localMatrix.m[2][2] = std::cos(heading);
        f->m_localMatrix(3) = S_vector(rnd(-20, 20), rnd(0, 2), rnd(-20, 20));
        f->m_scale = S_vector(1.0f, rnd(1.0f, 1.5f), rnd(0.5f, 1.0f));
        static const char *const gunFile = "gun.4ds";
        *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(f) + 0x134) = gunFile;
        I3D_frame *seat0 = AnFrame(FRAME_DUMMY, "SEAT00", f);
        seat0->m_localMatrix(3) = S_vector(0.0f, 0.8f, -0.9f);
        seat0->m_localMatrix.m[2][2] = 0.8f;            // (a seat's z axis not unit: normalized)
        seat0->m_localMatrix.m[2][0] = 0.2f;
        seat0->m_localMatrix.m[2][3] = 1.75f;           // (the facing's w)
        seat0->m_userProps = "SEAT14 name12 NO3RD";
        I3D_frame *seat1 = AnFrame(FRAME_DUMMY, "SEAT01", f);
        seat1->m_localMatrix(3) = S_vector(0.5f, 0.6f, -1.1f);
        seat1->m_userProps = "XSEAT2,name3";
        AnFrame(FRAME_DUMMY, "SEAT03", f);
        I3D_frame *cam3 = AnFrame(FRAME_DUMMY, "camera3RDa", seat1);
        I3D_frame *camFpv = AnFrame(FRAME_DUMMY, "cameraFPVb", f);
        I3D_frame *barrel = RbBoxFrame(f, "barrel", -0.1f, 0.1f, 0.9f, 1.1f, 0.0f, 1.6f, S_vector(0.0f, 0.1f, 0.2f));
        barrel->m_name = Stub(barrel).name.c_str();
        I3D_frame *sight = RbBoxFrame(barrel, "sight", -0.05f, 0.05f, 1.1f, 1.3f, 0.3f, 0.5f, S_vector(0.0f, 0.0f, 0.4f));
        // (The sight turned on the barrel: every corner counts; a flat plate far out; the gun's own box.)
        {
            const float c1 = std::cos(0.7f), s1 = std::sin(0.7f), c2 = std::cos(0.5f), s2 = std::sin(0.5f);
            const float rows[3][3] = {{c1, s1 * c2, s1 * s2}, {-s1, c1 * c2, c1 * s2}, {0.0f, -s2, c2}};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    sight->m_localMatrix.m[i][j] = rows[i][j];
        }
        I3D_frame *plate = RbBoxFrame(f, "plate", 0.3f, 0.3f, 2.0f, 2.4f, -0.2f, 0.2f, S_vector(0.0f, 0.0f, 0.0f));
        plate->m_name = Stub(plate).name.c_str();
        {
            std::vector<float> &v = Stub(f).vertices;
            for (int c = 0; c < 8; ++c) {
                const float q[8] = {c & 1 ? 0.4f : -0.4f, c & 2 ? 0.5f : 0.0f, c & 4 ? -1.3f : -1.6f, 0, 0, 0, 0, 0};
                v.insert(v.end(), q, q + 8);
            }
        }
        sight->m_name = Stub(sight).name.c_str();
        I3D_frame *shield = RbBoxFrame(f, "shield", -0.7f, 0.7f, 0.2f, 1.2f, 0.4f, 0.5f, S_vector(0.0f, 0.0f, 0.0f));
        shield->m_name = Stub(shield).name.c_str();
        I3D_frame *hidden = RbBoxFrame(f, "hiddenpart", -9.0f, 9.0f, -9.0f, 9.0f, -9.0f, 9.0f, S_vector(0, 0, 0));
        hidden->m_name = Stub(hidden).name.c_str();
        F_SeLocalBound(hidden);
        static_cast<I3D_visual *>(hidden)->VisualFlags() |= VISFLAGS_1000;
        SE(r.sw, a->SetFrame(f) && a->GetFrame() == f);
        {
            // (No properties: nothing read either way.)
            S_SaBytes in;
            in.Head(0x777, 10).Val(5);
            S_SaReader rd(in);
            bool wrote = false;
            SE(r.sw, a->ReadProperties(rd.c) && rd.c.RemainingSize() == 4 &&
                         SaWritten([&](C_chunk &c) { return a->WriteProperties(c); }, &wrote) ==
                             std::vector<uint8_t>({0x77, 0x07, 6, 0, 0, 0}) && wrote);
        }

        // The weapon's box (from the asm): the frame's own object visual's bound, its descendants' (those
        // flagged 0x1000 left out) through their local matrices.
        std::function<void(I3D_frame *, const S_matrix &, I3D_bbox &)> addBox = [&](I3D_frame *fr, const S_matrix &parent,
                                                                                    I3D_bbox &box) {
            const S_matrix m = fr->GetLocalMatrix() * parent;
            if (fr->GetVisualType() == I3DVISUAL_OBJECT && !(static_cast<I3D_visual *>(fr)->VisualFlags() & VISFLAGS_1000)) {
                const I3D_bbox &b = static_cast<I3D_visual *>(fr)->GetLocalBound().bbox;
                if (b.min.x <= b.max.x)
                    for (int c = 0; c < 8; ++c)
                        box.Expand(S_vector(c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y, c & 4 ? b.max.z : b.min.z) * m);
            }
            for (I3D_frame *child = fr->m_firstChild; child; child = child->m_nextSibling)
                addBox(child, m, box);
        };
        const auto weaponBox = [&](I3D_frame *fr) {
            I3D_bbox box = I3D_bbox::Empty();
            if (fr->GetVisualType() == I3DVISUAL_OBJECT)
                box = static_cast<I3D_visual *>(fr)->GetLocalBound().bbox;
            for (I3D_frame *child = fr->m_firstChild; child; child = child->m_nextSibling)
                addBox(child, IdentityMatrix(), box);
            return box;
        };
        const auto sphereOf = [](const I3D_bbox &box, S_vector &center) {
            if (!(box.min.x <= box.max.x))
                return 0.0f;
            const S_vector half = (box.max - box.min) * 0.5f;
            center = box.min + half;
            return std::sqrt((half.z * half.z + half.y * half.y) + half.x * half.x);
        };
        const auto volumeIs = [&](C_StaticWeapon *w, bool scaled) {
            const I3D_bbox box = weaponBox(w->GetFrame());
            S_vector center(0.0f, 0.0f, 0.0f);
            float radius = sphereOf(box, center);
            if (scaled) {
                const S_vector s = w->GetFrame()->m_scale;
                const float yz = s.y <= s.z ? s.z : s.y;
                radius = (s.x <= yz ? yz : s.x) * radius;
            }
            const S_matrix world = w->GetFrame()->GetMatrix();
            S_matrix inv;
            inv.Inverse(world);
            const S_vector cw = center * world;
            const S_CollisionVolume &x = w->m_volume;
            return x.flags == 0x40 && x.frame == w->GetFrame() && x.type == COLLISION_TYPE_PLACE && x.actor == w &&
                   RbSameBox(x.bbox, box) && SaSame(&x.radius, &radius, 4) && RbSame(x.center, cw) &&
                   x.matrix == &w->GetFrame()->m_worldMatrix && x.invMatrix == &w->m_invMatrix &&
                   SaSame(&w->m_invMatrix, &inv, sizeof inv);
        };

        // The walking points (the way out: the one at the weapon).
        alignas(16) static uint8_t checkpointsMemory[sizeof(C_Checkpoints)];
        std::memset(checkpointsMemory, 0, sizeof checkpointsMemory);
        auto &walk = *reinterpret_cast<C_Checkpoints *>(checkpointsMemory);
        static S_Checkpoint walkPoints[3];
        walk.m_points = walkPoints;
        walk.m_pointCount = 3;
        const S_vector gw = f->GetWorldPos();
        for (int k = 0; k < 3; ++k)
            walkPoints[k].pos = S_vector(gw.x + rnd(-3, 3), gw.y, gw.z + rnd(-3, 3));
        GetGame()->m_checkpoints = &walk;
        g_textIds = true;
        Fake(&a->m_weapon).text = 4711;

        // --- StartMission (a network server): the net object (unknown3C 1, the send mask 2, unknown40 off,
        // the seat weapon's message factory; the replication's C4 0 / C8 1), the seats, the seat weapon (on
        // the frame, seat 0; its sender, the store's rounds), the name, the box, the way out (the walking
        // point), the destroyed model ("C" and the model's file), usable ---------------------------------------
        g_gameRole = GAME_ROLE_SERVER;
        g_netFinds.clear();
        g_netC4.clear();
        g_netC8.clear();
        g_fakeNet = S_FakeNet();
        g_cacheLoads.clear();
        g_seActorPoint = 2;
        reset();
        a->StartMission(true);
        S_FakeWeapon &fw = Fake(&a->m_weapon);
        SE(r.swStart, g_activations == std::vector<bool>{true} && a->m_started && a->m_storeChanged &&
                          a->m_netObject == reinterpret_cast<C_NetObject *>(&g_fakeNet) &&
                          g_netFinds == std::vector<std::pair<int, bool>>{{77, true}} && g_fakeNet.unknown3C == 1 &&
                          g_fakeNet.sendMask == 2 && g_fakeNet.unknown40 == 0 && g_fakeNet.factory == &MakeSeatWeaponMessage &&
                          g_fakeNet.factoryContext == a && g_netC4 == std::vector<int>{0} && g_netC8 == std::vector<int>{1});
        SE(r.swStart, a->m_seats.size() == 2 && a->m_seats[0].frame == seat0 && a->m_seats[1].frame == seat1 &&
                          !a->m_seats[0].occupant && !a->m_seats[0].blocked && fw.inits == 1 && fw.car == static_cast<void *>(a) &&
                          fw.frame == f && fw.seatFrame == f && fw.seat == 0 && fw.sender == &SeatWeaponSendMessage &&
                          fw.senderContext == a && fw.refillBelow == 6 && store.counted == std::vector<int>{41} &&
                          AnString(a->m_name) == "text 4711");
        SE(r.swStart, volumeIs(a, true) && a->m_volumeAdded && g_volumeLog.size() == 1 && g_volumeLog[0].op == 'A' &&
                          g_volumeLog[0].volume == &a->m_volume && a->m_exit.x == walkPoints[2].pos.x &&
                          a->m_exit.y == walkPoints[2].pos.y && a->m_exit.z == walkPoints[2].pos.z && a->m_destroyed &&
                          g_cacheLoads == std::vector<std::string>{"Cgun.4ds"} && a->GetState() == 1 &&
                          SaLogIs({"actorpoint", "createframe 9", "on se.gun 1"}));
        reset();
        a->StartMission(false);
        SE(r.swStart, g_activations == std::vector<bool>{false} && g_itLog.empty() && fw.inits == 1);
        {
            // A local game: no net object, no sender; no walking point: half a metre behind; no destroyed
            // model (released); no item: nothing counted (fix).
            auto *b = static_cast<C_StaticWeapon *>(creator->Create());
            I3D_frame *bf = AnFrame(FRAME_MODEL, "se.gun2", nullptr);
            bf->m_localMatrix(3) = S_vector(3.0f, 1.0f, -2.0f);
            bf->m_localMatrix.m[2][0] = 0.5f;
            *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(bf) + 0x134) = gunFile;
            b->SetFrame(bf);
            b->m_containers = containers;
            g_gameRole = GAME_ROLE_LOCAL;
            g_seActorPoint = CHECKPOINT_NONE;
            g_cacheFailName = "Cgun.4ds";
            g_netFinds.clear();
            store.counted.clear();
            reset();
            b->StartMission(true);
            const S_matrix bw = bf->GetMatrix();
            // (A local game counts as a server: the store marked to be sent, never sent.)
            SE(r.swStart, b->m_started && b->m_storeChanged && !b->m_netObject && g_netFinds.empty() &&
                              !Fake(&b->m_weapon).sender && store.counted.empty() && !b->m_destroyed && b->m_seats.empty() &&
                              b->m_exit.x == bw.m[2][0] * -0.5f + bw.m[3][0] && b->m_exit.y == bw.m[2][1] * -0.5f + bw.m[3][1] &&
                              b->m_exit.z == bw.m[2][2] * -0.5f + bw.m[3][2] && volumeIs(b, true) &&
                              SaLogIs({"actorpoint", "createframe 9", "release new", "on se.gun2 1"}));
            g_cacheFailName.clear();
            // A network client: the store not to be sent; no item: nothing counted (fix).
            auto *c = static_cast<C_StaticWeapon *>(creator->Create());
            I3D_frame *cf = AnFrame(FRAME_MODEL, "se.gun3", nullptr);
            *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(cf) + 0x134) = gunFile;
            c->SetFrame(cf);
            c->m_containers = containers;
            g_gameRole = GAME_ROLE_CLIENT;
            g_weaponDef = nullptr;
            g_netC4.clear();
            c->StartMission(true);
            SE(r.swStart, !c->m_storeChanged && c->m_netObject && Fake(&c->m_weapon).sender == &SeatWeaponSendMessage &&
                              store.counted.empty() && g_netC4.empty());
            g_weaponDef = &def;
            b->StopMission(true);
            c->StopMission(true);
            creator->Destroy(b);
            creator->Destroy(c);
            g_gameRole = GAME_ROLE_SERVER;
        }

        // --- the seats ------------------------------------------------------------------------------------------
        C_Human gunner, loader;
        I3D_frame *gf = AnFrame(FRAME_MODEL, "se.gunner", nullptr);
        I3D_frame *lf = AnFrame(FRAME_MODEL, "se.loader", nullptr);
        gunner.m_frame = gf;
        loader.m_frame = lf;
        gf->m_localMatrix(3) = S_vector(4.0f, 0.5f, -3.0f);
        lf->m_localMatrix(3) = S_vector(-2.0f, 0.5f, 5.0f);
        {
            SE(r.swSeats, a->GetSeatFlags(0) == ((1u << 1) | (1u << 4)) && a->GetSeatFlags(1) == (1u << 2) &&
                              a->GetSeatFlags(2) == 0 && a->GetSeatFlags(-1) == 0 && a->GetSeatText(0) == 12 &&
                              a->GetSeatText(1) == 3 && a->GetSeatText(2) == -1 && a->GetSeatText(-1) == -1 &&
                              a->IsSeatFirstPersonOnly(0) && !a->IsSeatFirstPersonOnly(1) && !a->IsSeatFirstPersonOnly(2) &&
                              !a->IsSeatFirstPersonOnly(-1) && a->HasSeatWeapon(0) && !a->HasSeatWeapon(1));
            SE(r.swSeats, !a->IsSeatBlocked(0) && a->SetSeatBlocked(1, true) && a->IsSeatBlocked(1) && a->IsSeatBlocked(2) &&
                              a->IsSeatBlocked(-1) && !a->SetSeatBlocked(2, true) && !a->SetSeatBlocked(-1, false));
            // Into seat 0: linked at its origin, facing +z; the actor's place and direction the seat's
            // (normalized, its w), the view and the facing; the seat weapon's gunner; the box moved; the
            // server sends the rounds.
            fw.rounds = 23;
            fw.gunners = 0;
            g_queued.clear();
            g_heldMessages.clear();
            reset();
            a->SetSeatOccupant(0, &gunner);
            const S_matrix sw0 = seat0->GetMatrix();
            S_vector sd = sw0(2);
            SeNormalize(sd, (sd.y * sd.y + sd.z * sd.z) + sd.x * sd.x);
            const S_vector4 facing = {sd.x, sd.y, sd.z, sw0.m[2][3]};
            SE(r.swSeats, a->GetSeatOccupant(0) == &gunner && gf->m_parent == seat0 && RbSame(gf->GetPos(), S_vector(0, 0, 0)) &&
                              SaSame(&gunner.m_position, &sw0.m[3][0], 16) && SaSame(&gunner.m_direction, &facing, 16) &&
                              SaSame(&gunner.viewDir, &facing, 16) && SaSame(&gunner.facingDir, &facing, 16) &&
                              fw.gunner == &gunner && fw.gunners == 1 && volumeIs(a, false) && g_volumeLog.size() == 1 &&
                              g_volumeLog[0].op == 'M' && g_queued.size() == 1 && g_queued[0].type == SEAT_WEAPON_MSG_ROUNDS &&
                              g_queued[0].rounds == 23 && g_queued[0].seat == 0 && g_queued[0].sender == 6 &&
                              g_queued[0].reliable && g_queued[0].fromClient &&
                              SaLogIs({"link se.gunner SEAT00 0", "worlddir se.gunner 0 0 1 0", "update se.gunner",
                                       "update se.gun", "update se.gun"}));
            // Taken: nothing; out of range: nothing; the loader into seat 1 (a client: no rounds sent); the
            // frame's script told.
            reset();
            a->SetSeatOccupant(0, &loader);
            a->SetSeatOccupant(2, &loader);
            a->SetSeatOccupant(-1, &loader);
            SE(r.swSeats, a->GetSeatOccupant(0) == &gunner && g_itLog.empty() && fw.gunners == 1);
            g_gameRole = GAME_ROLE_CLIENT;
            g_queued.clear();
            alignas(16) static uint8_t scriptMemory[64];
            S_FrameData gunData = {};
            gunData.script = reinterpret_cast<C_Script *>(scriptMemory);
            f->m_data = &gunData;
            g_uses.clear();
            reset();
            a->SetSeatOccupant(1, &loader);
            f->m_data = nullptr;
            SE(r.swSeats, a->GetSeatOccupant(1) == &loader && lf->m_parent == seat1 && fw.gunner == &loader && g_queued.empty() &&
                              g_uses == std::vector<int>{1} && RbSame(lf->GetPos(), S_vector(0, 0, 0)) &&
                              a->FindSeat(&gunner) == 0 && a->FindSeat(&loader) == 1 && a->FindSeat(nullptr) == -1 &&
                              !a->GetSeatOccupant(2) && !a->GetSeatOccupant(-1));
            g_gameRole = GAME_ROLE_SERVER;
            // The cameras: the occupant out meanwhile; seat 0 first person only: "cameraFPV*" (under the seat,
            // else its parent); seat 1: "camera3RD*" unless the game is in first person.
            g_firstPerson = false;
            reset();
            I3D_frame *c0 = a->GetSeatCamera(0);
            I3D_frame *c1 = a->GetSeatCamera(1);
            g_firstPerson = true;
            I3D_frame *c1f = a->GetSeatCamera(1);
            g_firstPerson = false;
            SE(r.swSeats, c0 == camFpv && c1 == cam3 && c1f == camFpv && !a->GetSeatCamera(2) && !a->GetSeatCamera(-1) &&
                              gf->m_parent == seat0 && lf->m_parent == seat1 &&
                              SaLogIs({"link se.gunner 0 0", "link se.gunner SEAT00 0", "update se.gunner", "link se.loader 0 0",
                                       "link se.loader SEAT01 0", "update se.loader", "link se.loader 0 0",
                                       "link se.loader SEAT01 0", "update se.loader"}));
            // The seat weapon's: the controls (manned seats only; fix: out of range none), the rest whatever
            // the seat.
            float axes[4] = {}, buttons[4] = {};
            fw.controls = 0;
            a->ApplySeatControls(0, axes, buttons);
            a->ApplySeatControls(5, axes, buttons);
            a->ApplySeatControls(-1, axes, buttons);
            fw.aim = S_vector(0.1f, 0.2f, 0.3f);
            fw.origin = S_vector(1, 2, 3);
            fw.limits[0] = 0.5f, fw.limits[1] = -0.25f, fw.limits[2] = -1.0f, fw.limits[3] = 1.5f;
            fw.text = 99;
            fw.aimable = true;
            fw.c8 = true;
            float up = 0, down = 0, left = 0, right = 0;
            a->GetSeatWeaponLimits(1, &up, &down, &left, &right);
            float aimAt[3] = {4, 5, 6};
            int x8 = 0, y8 = 0, z8 = 0;
            fw.views = 0;
            a->ShowSeatView(1, true);
            SE(r.swSeats, fw.controls == 1 && fw.axes == axes && fw.buttons == buttons && a->GetSeatWeaponRounds(1) == 23 &&
                              RbSame(a->GetSeatWeaponAim(1), fw.aim) && RbSame(a->GetSeatWeaponOrigin(1), fw.origin) &&
                              a->UnknownAC(1) == 0.0f && a->GetSeatWeaponText(1) == 99 && a->GetSeatWeaponDef(1) == &def &&
                              up == 0.5f && down == -0.25f && left == -1.0f && right == 1.5f && a->CanSeatWeaponAim(1, aimAt) &&
                              fw.aimPos == aimAt && a->UnknownC8(1, &x8, &y8, &z8) && fw.c8Args[0] == &x8 && fw.c8Args[2] == &z8 &&
                              fw.view && fw.views == 1);
            // LeaveSeat: out into the primary sector (its world kept), the seat free and unblocked, no gunner,
            // the box moved; an empty seat: unblocked too, null.
            a->SetSeatBlocked(1, true);
            fw.gunners = 0;
            const S_vector lw = lf->GetWorldPos();
            reset();
            C_Actor *left1 = a->LeaveSeat(1);
            SE(r.swSeats, left1 == &loader && !a->GetSeatOccupant(1) && !a->IsSeatBlocked(1) && lf->m_parent == g_sector &&
                              RbSame(lf->GetWorldPos(), lw) && !fw.gunner && fw.gunners == 1 && g_volumeLog.size() == 1 &&
                              SaLogIs({"link se.loader sector 1", "update se.gun"}));
            a->SetSeatBlocked(1, true);
            reset();
            SE(r.swSeats, !a->LeaveSeat(1) && !a->IsSeatBlocked(1) && !a->LeaveSeat(2) && !a->LeaveSeat(-1) &&
                              SaLogIs({"update se.gun"}));
            // A null occupant into a free seat: the gunner none.
            fw.gunner = &gunner;
            a->SetSeatOccupant(1, nullptr);
            SE(r.swSeats, !a->GetSeatOccupant(1) && !fw.gunner);
            fw.gunner = &gunner;
        }

        // --- ExitSeat: to the way out (the walking point) or, without one, a metre behind; facing along the
        // weapon; the seat free (its blocking kept), no gunner, the box moved ------------------------------------
        {
            a->SetSeatBlocked(0, true);
            reset();
            const S_vector gfw = [&] {
                S_vector d = f->GetMatrix()(2);
                SeNormalize(d, (d.y * d.y + d.z * d.z) + d.x * d.x);
                return d;
            }();
            const S_vector exit0(a->m_exit.x, a->m_exit.y, a->m_exit.z);
            SE(r.swExit, a->ExitSeat(0) && !a->GetSeatOccupant(0) && a->IsSeatBlocked(0) && !fw.gunner && gf->m_parent == g_sector &&
                             SaLogIs({"link se.gunner sector 0", "setpos se.gunner " + ItV(exit0), "worlddir se.gunner " + ItV(gfw) + " 0",
                                      "update se.gunner", "update se.gun"}));
            SE(r.swExit, !a->ExitSeat(0) && !a->ExitSeat(2) && !a->ExitSeat(-1));
            a->SetSeatOccupant(0, &gunner);
            a->m_exit = {0.0f, 0.0f, 0.0f, 0.0f};
            const S_matrix w = f->GetMatrix();
            const S_vector back(w.m[2][0] * -1.0f + w.m[3][0], w.m[2][1] * -1.0f + w.m[3][1], w.m[2][2] * -1.0f + w.m[3][2]);
            reset();
            SE(r.swExit, a->ExitSeat(0) && SaLogIs({"link se.gunner sector 0", "setpos se.gunner " + ItV(back),
                                                    "worlddir se.gunner " + ItV(gfw) + " 0", "update se.gunner",
                                                    "update se.gun"}));
            // (A tiny way out, its square under 1e-16: behind too.)
            a->SetSeatOccupant(0, &gunner);
            a->m_exit = {1e-9f, 0.0f, -1e-9f, 0.0f};
            reset();
            SE(r.swExit, a->ExitSeat(0) && g_itLog.size() > 1 && g_itLog[1] == "setpos se.gunner " + ItV(back));
            // (Just over 1e-16, by its y: the way out.)
            a->SetSeatOccupant(0, &gunner);
            a->m_exit = {0.0f, 2e-8f, 0.0f, 0.0f};
            reset();
            SE(r.swExit, a->ExitSeat(0) && g_itLog.size() > 1 && g_itLog[1] == "setpos se.gunner " + ItV(S_vector(0.0f, 2e-8f, 0.0f)));
            a->m_exit = {walkPoints[2].pos.x, walkPoints[2].pos.y, walkPoints[2].pos.z, 0.0f};
        }

        // --- Hit: passed on to the gunner in seat 0 (none: nothing); a bullet or melee hit missing his zones
        // alarms him (alive, hostile to the attacker) along the weapon's direction; the rest his hit in its zone
        // (0 for other kinds), the direction normalized; the frame hit -----------------------------------------
        {
            C_Human attacker;
            I3D_frame *af = AnFrame(FRAME_MODEL, "se.attacker", nullptr);
            attacker.m_frame = af;
            a->m_direction = {0.25f, 0.0f, -0.75f, 0.0f};
            gunner.hits.clear();
            reset();
            a->Hit(HIT_BULLET, 30.0f, S_vector(1, 2, 3), S_vector(0, 0, 2), &attacker, 5);
            SE(r.swHit, gunner.hits.empty() && g_itLog.empty());
            a->SetSeatOccupant(0, &gunner);
            const S_vector pos(rnd(-5, 5), rnd(0, 2), rnd(-5, 5));
            const S_vector dir(rnd(-3, 3), rnd(-1, 1), rnd(-3, 3));
            S_vector nd = dir;
            SeNormalize(nd, (nd.y * nd.y + nd.z * nd.z) + nd.x * nd.x);
            const std::vector<std::pair<E_HitType, int>> cases = {{HIT_BULLET, 3}, {HIT_MELEE, 0}, {HIT_EXPLOSION, 7},
                                                                   {HIT_FALL, 0xFF}, {HIT_BULLET, 0xFF}, {HIT_MELEE, 0xFF}};
            for (const auto &k : cases) {
                g_seZone = k.second;
                gunner.hits.clear();
                gunner.shotsAt.clear();
                gunner.health = 800;
                gunner.hostileTo = {&attacker};
                const bool zoned = k.first == HIT_BULLET || k.first == HIT_MELEE;
                const bool missed = zoned && k.second == 0xFF;
                reset();
                a->Hit(k.first, 12.5f, pos, dir, &attacker, 99);
                std::vector<std::string> log;
                if (zoned)
                    log.push_back("hitzone se.gunner " + ItV(pos) + " " + ItV(dir));
                if (!missed)
                    log.push_back("framehit se.gunner");
                const int zone = zoned ? k.second : 0;
                SE(r.swHit, SaLogIs(log) && gunner.hits.size() == (missed ? 0u : 1u) &&
                                (missed || (gunner.hits[0].type == k.first && gunner.hits[0].amount == 12.5f &&
                                            RbSame(gunner.hits[0].pos, pos) && RbSame(gunner.hits[0].dir, nd) &&
                                            gunner.hits[0].attacker == &attacker && gunner.hits[0].flags == zone)) &&
                                gunner.shotsAt.size() == (missed ? 1u : 0u) &&
                                (!missed || (gunner.shotsAt[0].first == &attacker &&
                                             RbSame(gunner.shotsAt[0].second, S_vector(0.25f, 0.0f, -0.75f)))));
            }
            // (Missed, a dead or friendly gunner: nothing.)
            g_seZone = 0xFF;
            gunner.shotsAt.clear();
            gunner.health = 0;
            a->Hit(HIT_BULLET, 1.0f, pos, dir, &attacker, 0);
            gunner.health = 800;
            gunner.hostileTo.clear();
            a->Hit(HIT_MELEE, 1.0f, pos, dir, &attacker, 0);
            SE(r.swHit, gunner.shotsAt.empty() && gunner.hits.empty());
        }

        // --- the states: SetState applies a new one (shown / hidden, the destroyed model's meshes and turns
        // on the parts, the box) and tells the clients; destroyed, the seats' occupants are killed ----------------
        {
            // The models' parts: the destroyed model's "barrel" and "shield" with meshes and turns, the
            // reloaded model's likewise (named apart).
            static int fills;
            fills = 0;
            static I3D_frame *fillTarget;
            g_cacheFill = [](I3D_model *object) {
                const std::string tag = fills++ == 0 ? "dead" : "live";
                auto *model = reinterpret_cast<I3D_frame *>(object);
                const char *const names[3] = {"barrel", "sight", "shield"};
                for (int n = 0; n < 3; ++n) {
                    const char *name = names[n];
                    I3D_frame *part = MakeFrame(g_frameVtable, FRAME_VISUAL, false);
                    g_itNames[part] = tag + std::string(".") + name;
                    part->m_name = name;
                    LinkUnder(part, model);
                    static uint8_t meshes[6][8];
                    uint8_t *mesh = meshes[(tag == "dead" ? 0 : 3) + n];
                    *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(part) + 0x1E0) = mesh;
                    g_seMeshNames[mesh] = tag + "mesh." + name;
                    part->m_rot = S_quat(0.5f, name[0] == 'b' ? 0.5f : -0.5f, 0.5f, 0.5f);
                    part->m_frameFlags |= FRMFLAGS_ROT_DIRTY;
                }
                fillTarget = model;
            };
            // (A fresh StartMission for the filled destroyed model.)
            a->StopMission(true);
            reset();
            a->StartMission(true);
            I3D_frame *dead = fillTarget;
            SE(r.swState, a->m_destroyed == reinterpret_cast<I3D_model *>(dead) && a->m_seats.size() == 2);
            a->SetSeatOccupant(0, &gunner);
            a->SetSeatOccupant(1, &loader);
            // Hidden: off, the box out; told (2 bits the kind, 2 the state).
            reset();
            a->SetState(0);
            SE(r.swState, a->GetState() == 0 && !a->m_volumeAdded && SaLogIs({"on se.gun 0"}) && g_volumeLog.size() == 1 &&
                              g_volumeLog[0].op == 'R' && g_streams.size() == 1 && g_streams[0]->id == 77 && g_streams[0]->sent &&
                              g_streams[0]->bits == std::vector<std::pair<uint32_t, int>>({{1, 2}, {0, 2}}));
            reset();
            a->SetState(0);
            SE(r.swState, g_itLog.empty() && g_streams.empty());
            // Destroyed: the occupants out, every visual takes the dead part's mesh and turn, back in; their
            // hits (an explosion's, -1, at the weapon, up).
            gunner.hits.clear();
            loader.hits.clear();
            a->m_position = {rnd(-5, 5), rnd(0, 2), rnd(-5, 5), 1.0f};
            reset();
            a->SetState(2);
            const S_quat deadBarrel = SaNormalized(S_quat(0.5f, 0.5f, 0.5f, 0.5f));
            const S_quat deadShield = SaNormalized(S_quat(0.5f, -0.5f, 0.5f, 0.5f));
            const S_vector at(a->m_position.x, a->m_position.y, a->m_position.z);
            SE(r.swState, a->GetState() == 2 && a->m_destroyedShown && !a->m_volumeAdded && SaSameQuat(barrel->m_rot, deadBarrel) &&
                              SaSameQuat(shield->m_rot, deadShield) && SaSameQuat(sight->m_rot, deadShield) &&
                              SaLogIs({"link se.loader 0 1", "link se.gunner 0 1", "setmesh barrel deadmesh.barrel 1",
                                       "update barrel", "setmesh sight deadmesh.sight 1", "update sight",
                                       "setmesh shield deadmesh.shield 1", "update shield", "update se.gun",
                                       "link se.loader SEAT01 1", "update se.loader", "link se.gunner SEAT00 1",
                                       "update se.gunner"}) &&
                              loader.hits.size() == 1 && gunner.hits.size() == 1 && loader.hits[0].type == HIT_EXPLOSION &&
                              loader.hits[0].amount == -1.0f && RbSame(loader.hits[0].pos, at) &&
                              RbSame(loader.hits[0].dir, S_vector(0, 1, 0)) && !loader.hits[0].attacker && loader.hits[0].flags == 0 &&
                              g_hitOrder.size() >= 2 && g_hitOrder[g_hitOrder.size() - 2] == &loader && g_hitOrder.back() == &gunner &&
                              g_streams.size() == 1 && g_streams[0]->bits == std::vector<std::pair<uint32_t, int>>({{1, 2}, {2, 2}}));
            // Off: the parts from the model again (a new one, loaded: kept), the box out.
            g_cacheLoads.clear();
            reset();
            a->SetState(3);
            SE(r.swState, a->GetState() == 3 && !a->m_destroyedShown && g_cacheLoads == std::vector<std::string>{"gun.4ds"} &&
                              SaLogIs({"createframe 9", "link se.loader 0 1", "link se.gunner 0 1",
                                       "setmesh barrel livemesh.barrel 1", "update barrel", "setmesh sight livemesh.sight 1",
                                       "update sight", "setmesh shield livemesh.shield 1",
                                       "update shield", "update se.gun", "link se.loader SEAT01 1", "update se.loader",
                                       "link se.gunner SEAT00 1", "update se.gunner"}));
            // Usable: shown, the box in; a local game tells nobody.
            g_gameRole = GAME_ROLE_LOCAL;
            reset();
            a->SetState(1);
            SE(r.swState, a->GetState() == 1 && a->m_volumeAdded && SaLogIs({"on se.gun 1"}) && g_volumeLog.size() == 1 &&
                              g_volumeLog[0].op == 'A' && g_streams.empty());
            // (An unknown state: kept as it is, nothing done; the model failing to load: intact stays.)
            reset();
            a->SetState(7);
            a->SetState(2);
            g_cacheFailName = "gun.4ds";
            a->SetState(3);
            SE(r.swState, a->GetState() == 3 && a->m_destroyedShown &&
                              std::count(g_itLog.begin(), g_itLog.end(), std::string("release new")) == 1);
            g_cacheFailName.clear();
            a->SetState(1);
            a->SetState(3);
            // (No destroyed model: the state only.)
            I3D_model *keep = a->m_destroyed;
            a->m_destroyed = nullptr;
            reset();
            a->SetState(2);
            SE(r.swState, a->GetState() == 2 && !a->m_destroyedShown && g_itLog.empty());
            a->m_destroyed = keep;
            a->SetState(1);
            g_cacheFill = nullptr;
            g_gameRole = GAME_ROLE_SERVER;
            gunner.hits.clear();
            loader.hits.clear();
        }

        // --- the network: OnMessage (kind 0 the store, added unless a server; 1 the state), WriteState /
        // ReadState (the state, the store), the seat weapon's messages ------------------------------------------
        {
            C_FakeItem i1, i2, i3;
            i1.m_uid = 0x101, i1.m_typeId = 7;
            i2.m_uid = 0x202, i2.m_typeId = 9;
            i3.m_uid = 0x303, i3.m_typeId = 11;
            store.slots = {{&i1, &i2}, {&i3}};
            reset();
            C_BitStream *out = CreateActorMessage(1, false);
            a->WriteState(out);
            SE(r.swNet, g_streams[0]->bits == std::vector<std::pair<uint32_t, int>>(
                                                 {{1, 2}, {0x101, 16}, {7, 8}, {0x202, 16}, {9, 8}, {0x303, 16}, {11, 8}, {0xFFFF, 16}}));
            store.slots.clear();
            // ReadState: the state applied, the items made with their uids and added.
            g_readBits = {{0, 2}, {0x44, 16}, {5, 8}, {0x55, 16}, {6, 8}, {0xFFFF, 16}};
            g_readPos = 0;
            g_readMismatch = false;
            g_created.clear();
            store.added.clear();
            a->ReadState(nullptr);
            SE(r.swNet, !g_readMismatch && g_readPos == g_readBits.size() && a->GetState() == 0 && g_created.size() == 2 &&
                            g_created[0].typeId == 5 && g_created[0].uid == 0x44 && g_created[1].typeId == 6 &&
                            g_created[1].uid == 0x55 && g_created[0].container == 0 && !g_created[0].owner && store.added.size() == 2 &&
                            store.addIndices == std::vector<int>({-1, -1}));
            // OnMessage: the store (a server reads it only), the state; other kinds nothing.
            for (int role : {GAME_ROLE_SERVER, GAME_ROLE_CLIENT}) {
                g_gameRole = static_cast<uint8_t>(role);
                g_readBits = {{0, 2}, {0x66, 16}, {8, 8}, {0xFFFF, 16}};
                g_readPos = 0;
                g_created.clear();
                a->OnMessage(nullptr);
                SE(r.swNet, g_readPos == 4 && g_created.size() == (role == GAME_ROLE_SERVER ? 0u : 1u));
            }
            g_readBits = {{1, 2}, {1, 2}};
            g_readPos = 0;
            a->OnMessage(nullptr);
            g_readBits = {{2, 2}};
            g_readPos = 0;
            a->OnMessage(nullptr);
            SE(r.swNet, a->GetState() == 1 && g_readPos == 1);
            g_gameRole = GAME_ROLE_SERVER;
            g_readBits.clear();
            g_readPos = 0;

            // The seat weapon's messages (not a local game, Update; not a pure client, Tick): shots and reloads of
            // a living gunner (not a client's own player's), fired / reloaded by the server and sent on, shown by
            // a client; the rounds; ammunition into the store.
            a->SetSeatOccupant(0, &gunner);
            const auto shot = [](int seat, int barrel) {
                auto *m = new C_SeatWeaponShotMessage;
                m->seat = seat;
                m->barrel = barrel;
                m->pos = {1, 2, 3, 1};
                m->dir = {0, 0, 1, 0};
                m->fromClient = true;
                m->unknown24 = 1;                       // (held by the queue it came from)
                return m;
            };
            const auto reload = [](int seat) {
                auto *m = new C_SeatWeaponReloadMessage;
                m->seat = seat;
                m->fromClient = true;
                m->unknown24 = 1;
                return m;
            };
            auto *rounds = new C_SeatWeaponRoundsMessage;
            rounds->rounds = 17;
            rounds->unknown24 = 1;
            auto *ammo = new C_SeatWeaponAmmoMessage;
            ammo->typeId = 41;
            ammo->uid = 0x77;
            ammo->unknown24 = 1;
            store.count = 100;                  // (no refill meanwhile)
            a->LeaveSeat(1);
            fw.shots.clear();
            fw.playShots = fw.reloads = fw.showReloads = 0;
            fw.roundsSet.clear();
            fw.fires = true;
            g_queued.clear();
            g_heldMessages.clear();
            g_created.clear();
            store.added.clear();
            C_SeatWeaponShotMessage *s0 = shot(0, 2);
            C_SeatWeaponShotMessage *s1 = shot(1, 1);         // (no one in seat 1: dropped)
            C_SeatWeaponReloadMessage *r0 = reload(0);
            g_fakeNet.incoming = {s0, s1, r0, rounds, ammo};
            const int reads0 = g_messageReads;
            a->Tick(0);
            SE(r.swNet, fw.shots.size() == 1 && fw.shots[0].barrel == 2 && fw.shots[0].unused == 1000 &&
                            SaSame(&fw.shots[0].pos, &s0->pos, 16) && fw.reloads == 1 && fw.reloadStore == &store &&
                            fw.roundsSet == std::vector<int>{17} && g_created.size() == 1 && g_created[0].typeId == 41 &&
                            g_created[0].uid == 0x77 && g_created[0].container == 0 && store.added.size() == 1 && g_queued.size() == 2 &&
                            g_queued[0].type == SEAT_WEAPON_MSG_SHOT && !g_queued[0].fromClient && g_queued[0].reliable &&
                            g_queued[1].type == SEAT_WEAPON_MSG_RELOAD && !g_queued[1].fromClient && g_messageReads == reads0 + 1 &&
                            g_heldMessages.size() == 2 && g_heldMessages[0] == s0 && g_heldMessages[1] == r0);
            delete s0;
            delete r0;
            g_heldMessages.clear();
            // A client (Update): shown, not sent; its own player's shots dropped; a dead gunner's dropped; a refused
            // shot not sent.
            g_gameRole = GAME_ROLE_CLIENT;
            C_Human *const oldSelected = g_selectedMember;
            g_selectedMember = &loader;
            g_queued.clear();
            g_heldMessages.clear();
            fw.playShots = fw.showReloads = 0;
            fw.updates.clear();
            g_fakeNet.incoming = {shot(0, 3), reload(0)};
            a->Update(15);
            SE(r.swNet, fw.playShots == 1 && fw.playShotBarrel == 3 && fw.showReloads == 1 && fw.showReloadStore == &store &&
                            g_queued.empty() && fw.updates == std::vector<int>{15});
            // (The client's own player at the gun: its shots and reloads come back to it, dropped.)
            g_selectedMember = &gunner;
            g_fakeNet.incoming = {shot(0, 2), reload(0)};
            a->Update(15);
            SE(r.swNet, fw.playShots == 1 && fw.showReloads == 1);
            g_selectedMember = oldSelected;
            gunner.health = 0;
            g_fakeNet.incoming = {shot(0, 1), reload(0)};
            a->Update(15);
            SE(r.swNet, fw.playShots == 1 && fw.showReloads == 1);
            gunner.health = 800;
            g_gameRole = GAME_ROLE_SERVER;
            fw.fires = false;
            fw.shots.clear();
            g_fakeNet.incoming = {shot(0, 0)};
            a->Tick(0);
            SE(r.swNet, fw.shots.size() == 1 && g_queued.empty());
            fw.fires = true;
            // A local game reads none; a client's Tick none.
            g_gameRole = GAME_ROLE_LOCAL;
            const int reads1 = g_messageReads;
            a->Update(5);
            a->Tick(5);
            g_gameRole = GAME_ROLE_CLIENT;
            a->Tick(5);
            SE(r.swNet, g_messageReads == reads1);
            g_gameRole = GAME_ROLE_SERVER;
        }

        // --- Tick (a network server): the store sent once changed, the ammunition refilled (30 s, a launcher's
        // 15 s, while the store has no more than at the start, not in co-op); Update: manned, the model's animation
        // and the box -------------------------------------------------------------------------------------------
        {
            a->m_storeChanged = true;
            fw.ticks.clear();
            fw.refillTime = 1000;
            fw.refillBelow = 6;
            store.count = 6;
            store.added.clear();
            g_created.clear();
            g_queued.clear();
            GetGame()->m_missionTime = 1000 + 30000;
            reset();
            a->Tick(25);
            SE(r.swTick, fw.ticks == std::vector<int>{25} && !a->m_storeChanged && g_streams.size() == 1 &&
                             g_streams[0]->id == 77 && g_streams[0]->sent && g_streams[0]->bits.size() == 2 &&
                             g_streams[0]->bits[0] == std::make_pair(0u, 2) && g_created.empty() && fw.refillTime == 1000);
            GetGame()->m_missionTime = 1000 + 30001;
            reset();
            a->Tick(25);
            SE(r.swTick, g_streams.empty() && g_created.size() == 1 && g_created[0].typeId == 41 && g_created[0].uid == -1 &&
                             g_created[0].container == 0 && !g_created[0].owner && g_queued.size() == 1 && g_queued[0].reliable &&
                             store.added.size() == 1 && store.addIndices.back() == -1 && g_queued.size() == 1 &&
                             g_queued[0].type == SEAT_WEAPON_MSG_AMMO && g_queued[0].rounds == 41 &&
                             g_queued[0].seat == g_created[0].item->GetUid() && g_queued[0].fromClient && g_queued[0].sender == 6 &&
                             fw.refillTime == 1000 + 30001);
            // More in the store, co-op, too early: none; a launcher every 15 s.
            store.count = 7;
            GetGame()->m_missionTime += 40000;
            g_created.clear();
            a->Tick(1);
            store.count = 6;
            g_coop = true;
            a->Tick(1);
            g_coop = false;
            SE(r.swTick, g_created.empty());
            def.m_component = &launcherComp;
            fw.refillTime = GetGame()->m_missionTime - 15000;
            a->Tick(1);
            fw.refillTime = GetGame()->m_missionTime - 15001;
            a->Tick(1);
            def.m_component = nullptr;          // (no component: a gun's 30 s)
            fw.refillTime = GetGame()->m_missionTime - 30000;
            a->Tick(1);
            SE(r.swTick, g_created.size() == 1);
            def.m_component = &gunComp;
            // (Fix: no item, no refill.)
            Fake(&a->m_weapon).def = nullptr;
            fw.refillTime = 0;
            a->Tick(1);
            Fake(&a->m_weapon).def = &def;
            SE(r.swTick, g_created.size() == 1);
            // A client: the store not sent.
            g_gameRole = GAME_ROLE_CLIENT;
            a->m_storeChanged = true;
            reset();
            a->Tick(1);
            SE(r.swTick, a->m_storeChanged && g_streams.empty());
            g_gameRole = GAME_ROLE_SERVER;
            a->m_storeChanged = false;
            // Update manned: the frame's animation ticked, the box; unmanned: the seat weapon only; no seats
            // (fix): likewise.
            // (The frame moved meanwhile: the box's inverse follows.)
            f->m_localMatrix.m[3][0] += 0.5f;
            f->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            reset();
            fw.updates.clear();
            a->Update(33);
            const bool mannedOk = SaLogIs({"tick se.gun 33", "update se.gun"}) && g_volumeLog.size() == 1 &&
                                  g_volumeLog[0].op == 'M' && volumeIs(a, false);
            a->LeaveSeat(0);
            reset();
            a->Update(33);
            const bool emptyOk = g_itLog.empty() && g_volumeLog.empty();
            std::vector<C_StaticWeapon::S_Seat> seats = a->m_seats;
            a->m_seats.clear();
            reset();
            a->Update(33);
            SE(r.swTick, mannedOk && emptyOk && g_itLog.empty() && fw.updates == std::vector<int>({33, 33, 33}));
            a->m_seats = seats;
            // (The box out: UpdateVolume nothing.)
            a->SetSeatOccupant(0, &gunner);
            a->SetState(0);
            reset();
            a->Update(33);
            SE(r.swTick, SaLogIs({"tick se.gun 33"}) && g_volumeLog.empty());
            a->SetState(1);
        }

        // --- the saved game: 1000 the state, 0x3EA the store, 0x3E9 the seat weapon (no base chunk) ------------
        {
            fw.saveTag = 0x1234;
            S_SaBytes sv;
            sv.Head(0x777, 6 + 10 + 10 + 10).Head(1000, 10).Val(1).Head(0x3EA, 10).Val(0x5151).Head(0x3E9, 10).Val(0x1234);
            bool saved = false;
            SE(r.swSave, SaWritten([&](C_chunk &c) { return a->SaveGame(c); }, &saved) == sv.b && saved);
            // Loaded: the state applied last, the store, the seat weapon; other ids left; none: off.
            S_SaBytes in;
            in.Head(0x777, 6 + 10 + 10 + 12 + 10).Head(0x3E9, 10).Val(0x99).Head(0x3EA, 10).Val(0x88).Head(55, 12)
                .Val(static_cast<uint16_t>(1000)).Val(2).Head(1000, 10).Val(0);
            S_SaReader rd(in);
            fw.loadedTags.clear();
            g_seItemLoads.clear();
            reset();
            SE(r.swSave, a->LoadGame(rd.c) && a->GetState() == 0 && fw.loadedTags == std::vector<int>{0x99} &&
                             g_seItemLoads == std::vector<int>{0x88} && SaLogIs({"on se.gun 0"}));
            S_SaBytes empty;
            empty.Head(0x777, 6);
            S_SaReader rd2(empty);
            SE(r.swSave, a->LoadGame(rd2.c) && a->GetState() == 3);
            a->SetState(1);
        }

        // --- GetBoxCorners: the weapon's box's corners (x max first, then y max, z min / max) in the world ------
        {
            S_vector4 corners[10];                      // (two more: none written past the eight)
            for (S_vector4 &c : corners)
                c = {7, 7, 7, 7};
            reset();
            a->GetBoxCorners(corners);
            const I3D_bbox box = weaponBox(f);
            const S_matrix world = f->GetMatrix();
            bool ok = SaLogIs({"update se.gun"});
            int n = 0;
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j)
                    for (int k = 0; k < 2; ++k) {
                        const S_vector p = S_vector(i == 0 ? box.max.x : box.min.x, j == 0 ? box.max.y : box.min.y,
                                                    k == 0 ? box.min.z : box.max.z) * world;
                        ok = ok && RbSame(S_vector(corners[n].x, corners[n].y, corners[n].z), p) && corners[n].w == 7.0f;
                        ++n;
                    }
            SE(r.swBox, ok && corners[8].x == 7.0f && corners[8].y == 7.0f && corners[8].z == 7.0f && corners[9].x == 7.0f &&
                            corners[9].z == 7.0f);
            // (The box's sphere: an empty box's centre 0, radius 0 (fix).)
            I3D_frame *bare = AnFrame(FRAME_DUMMY, "se.bare", nullptr);
            F_SeLocalBound(bare);
            auto *b = static_cast<C_StaticWeapon *>(creator->Create());
            b->SetFrame(bare);
            b->m_containers = containers;
            g_gameRole = GAME_ROLE_LOCAL;
            b->StartMission(true);
            SE(r.swBox, b->m_volume.radius == 0.0f && RbSame(b->m_volume.center, S_vector(0, 0, 0) * bare->GetMatrix()));
            b->m_volume.center = S_vector(9, 9, 9);
            b->UpdateVolume();
            SE(r.swBox, b->m_volume.radius == 0.0f && RbSame(b->m_volume.center, S_vector(0, 0, 0) * bare->GetMatrix()));
            b->StopMission(true);
            creator->Destroy(b);
            // (A box flat in x: still a box, its sphere's radius from the other two.)
            I3D_frame *flat = RbBoxFrame(nullptr, "se.flat", 0.25f, 0.25f, -0.5f, 0.5f, -1.0f, 2.0f, S_vector(1, 0, 1));
            flat->m_name = Stub(flat).name.c_str();
            b = static_cast<C_StaticWeapon *>(creator->Create());
            b->SetFrame(flat);
            b->m_containers = containers;
            b->StartMission(true);
            SE(r.swBox, volumeIs(b, true) && b->m_volume.radius > 1.0f);
            b->StopMission(true);
            creator->Destroy(b);
            // (One box turned so that its corner 0 alone makes the least x, its corner 1 the least z.)
            I3D_frame *turnedRoot = AnFrame(FRAME_DUMMY, "se.turnedroot", nullptr);
            F_SeLocalBound(turnedRoot);
            I3D_frame *turned = RbBoxFrame(turnedRoot, "turned", -0.3f, 0.5f, -0.2f, 0.6f, -0.4f, 0.1f, S_vector(0, 0, 0));
            turned->m_name = Stub(turned).name.c_str();
            {
                const float ca = std::cos(0.1f), sa = std::sin(0.1f), cb = std::cos(0.3f), sb = std::sin(0.3f),
                            cc = std::cos(0.9f), sc = std::sin(0.9f);
                const float rz[3][3] = {{ca, sa, 0}, {-sa, ca, 0}, {0, 0, 1}};
                const float rx[3][3] = {{1, 0, 0}, {0, cb, sb}, {0, -sb, cb}};
                const float ry[3][3] = {{cc, 0, -sc}, {0, 1, 0}, {sc, 0, cc}};
                float zx[3][3];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        zx[i][j] = rz[i][0] * rx[0][j] + rz[i][1] * rx[1][j] + rz[i][2] * rx[2][j];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        turned->m_localMatrix.m[i][j] = zx[i][0] * ry[0][j] + zx[i][1] * ry[1][j] + zx[i][2] * ry[2][j];
            }
            b = static_cast<C_StaticWeapon *>(creator->Create());
            b->SetFrame(turnedRoot);
            b->m_containers = containers;
            b->StartMission(true);
            SE(r.swBox, volumeIs(b, true) && b->m_volume.radius > 0.5f);
            b->StopMission(true);
            creator->Destroy(b);
            g_gameRole = GAME_ROLE_SERVER;
        }

        // --- StopMission: the seats forgotten, the seat weapon stopped; started: off; RestartMission: everyone
        // out, off, the seat weapon restarted, the store to be sent (network), usable on the server ---------------
        {
            a->SetSeatOccupant(1, &loader);
            fw.restarts = 0;
            reset();
            a->RestartMission(false);
            SE(r.swStart, !a->GetSeatOccupant(0) && !a->GetSeatOccupant(1) && a->GetState() == 3 && fw.restarts == 1 &&
                              a->m_storeChanged && lf->m_parent == g_sector);
            reset();
            a->RestartMission(true);
            SE(r.swStart, a->GetState() == 1 && fw.restarts == 2);
            const int stops = fw.stops;
            reset();
            a->StopMission(false);
            SE(r.swStart, g_deactivations == std::vector<bool>{false} && a->m_seats.empty() && fw.stops == stops + 1 &&
                              !a->m_started && a->GetState() == 3);
            reset();
            a->StopMission(true);
            SE(r.swStart, fw.stops == stops + 2 && g_volumeLog.empty());
        }
        a->m_containers = nullptr;
        creator->Destroy(a);
        g_weaponDef = nullptr;
        GetGame()->m_checkpoints = nullptr;
    }
