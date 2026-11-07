// (Part of PropActorsTest: the supply box.)
{
    C_ActorCreator *creator = g_actorCreators[ACTOR_SUPPLY_BOX];
    PR(r.box, creator && std::string(creator->GetName()) == "SupplyBox");
    // The animations' lengths (the fake animation set's slot 0x08).
    static std::map<std::string, int> animEnds;
    animEnds = {{"#crateopen", 700}, {"#crateclosed", 1600}, {"#cratedrop", 900}, {"#cratefly", 3000}};
    struct S_AnimEnd {
        static int __stdcall End(S_SeAnim *self) { return animEnds[self->name]; }
    };
    g_seAnimVtable[0] = reinterpret_cast<void *>(&F_SeAnimRelease);
    g_seAnimVtable[2] = reinterpret_cast<void *>(&S_AnimEnd::End);

    // The crate: a model with its walls ("Steny02": a mesh, the feet among its vertices) and the
    // ammunition parts (Ammo07 missing).
    const float dropX = rnd(-20.0f, 20.0f), dropZ = rnd(-20.0f, 20.0f);
    I3D_frame *crate = NewFrame(FRAME_MODEL, "crate", nullptr);
    LinkUnder(crate, g_sector);
    crate->m_name = Stub(crate).name.c_str();
    crate->m_localMatrix(3) = S_vector(dropX, 0.0f, dropZ);
    crate->m_scale = S_vector(1.0f, 1.25f, 0.8f);
    I3D_frame *walls = RbBoxFrame(crate, "Steny02", -0.5f, 0.5f, 0.0f, 0.8f, -0.4f, 0.4f, S_vector(0, 0, 0));
    walls->m_name = Stub(walls).name.c_str();
    // (A low vertex past the first four: not taken; a high one: not a foot.)
    const std::vector<S_vector> vertices = {{0.0f, 0.7f, 0.0f}, {-0.45f, 0.05f, -0.35f}, {0.45f, 0.1f, 0.35f},
                                            {0.45f, 0.0f, -0.35f}, {0.0f, 0.5f, 0.0f}, {-0.45f, 0.02f, 0.35f},
                                            {0.0f, 0.01f, 0.0f}};
    PrMesh(walls, vertices);
    I3D_frame *ammo[14] = {};
    for (int i = 0; i < 14; ++i) {
        if (i == 7)
            continue;
        char name[16];
        std::snprintf(name, sizeof name, "Ammo%02d", i);
        ammo[i] = NewFrame(FRAME_VISUAL, name, crate);
        ammo[i]->m_name = Stub(ammo[i]).name.c_str();
    }
    auto *box = static_cast<C_SupplyBox *>(creator->Create());
    box->SetNetId(70);
    PR(r.box, box->GetType() == ACTOR_SUPPLY_BOX && box->GetMaterialId(walls) == 25 && box->m_axisItems.empty() &&
                  box->m_alliedItems.empty() && !box->m_morphPlayer);
    // A frame without walls: false (the parts are still looked for).
    {
        I3D_frame *bare = NewFrame(FRAME_MODEL, "bare", nullptr);
        auto *b = static_cast<C_SupplyBox *>(creator->Create());
        PR(r.box, !b->SetFrame(bare) && !b->m_walls && b->GetCharges() == 20);
        creator->Destroy(b);
    }
    // SetFrame: the drop point and rotation, the lists emptied, the defaults, the parts, the feet.
    box->m_axisItems = {1, 2};
    box->m_maxCarried = 9;
    reset();
    PR(r.box, box->SetFrame(crate));
    const S_matrix &world = crate->GetMatrix();
    S_quat dropRot;
    dropRot.Make(world);
    PR(r.box, box->m_axisItems.empty() && box->m_maxCarried == 5 && box->m_capacity == 20 && box->m_respawnTime == -1 &&
                  box->m_health == 6000 && box->GetCharges() == 20 && box->m_walls == walls && !box->m_ammo[7] &&
                  box->m_ammo[0] == ammo[0] && box->m_ammo[13] == ammo[13] && box->m_ammo[9] == ammo[9] &&
                  box->m_ammo[10] == ammo[10] && box->m_state == C_SupplyBox::SUPPLY_READY && box->m_stateTime == 0 &&
                  box->m_animStage == -1 && box->m_anim == 5 && box->m_textId == 0x1BFA && !box->m_planeFlying &&
                  box->m_dropPos.x == world.m[3][0] && box->m_dropPos.z == world.m[3][2] &&
                  SaSameQuat(box->m_dropRot, dropRot));
    PR(r.box, SaLogIs({"getlevel 0", "getvertices 0", "unlockvertices"}));
    // The feet (the asm's choice): the first four vertices below half the box's height, then ordered.
    {
        I3D_bbox hb;
        const I3D_bbox hbox = *I3DGetDriver()->GetHierarchyBBox(hb, crate);
        const float limit = (hbox.max.y - hbox.min.y) * 0.5f;
        S_vector4 f[4] = {};
        int n = 0;
        for (const S_vector &v : vertices)
            if (v.y < limit && n < 4)
                f[n++] = {v.x, v.y, v.z, 0.0f};
        const auto dist = [](const S_vector4 &a, const S_vector4 &b) {
            const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return PrLength((dz * dz + dy * dy) + dx * dx);
        };
        if (dist(f[0], f[2]) < dist(f[0], f[1]))
            std::swap(f[1], f[2]);
        else if (dist(f[0], f[3]) < dist(f[0], f[1]))
            std::swap(f[1], f[3]);
        const float d03 = dist(f[0], f[3]);
        const float ax = f[0].x - f[2].x, ay = f[0].y - f[2].y, az = f[0].z - f[2].z;
        if (PrLength((ax * ax + ay * ay) + az * az) > d03)
            std::swap(f[2], f[3]);
        for (int i = 0; i < 4; ++i)
            PR(r.boxFeet, box->m_feet[i].x == f[i].x && box->m_feet[i].y == f[i].y && box->m_feet[i].z == f[i].z);
        PR(r.boxFeet, n == 4);
    }

    // --- the properties: a version byte, the fields, ten items a list (-1 past the end), the text ---------
    {
        S_SaBytes in;
        in.Head(0x777, 6 + 1 + 20 + 80 + 4).Val(uint8_t(1)).Val(1).Val(1u).Val(3).Val(12).Val(4);
        const int axis[10] = {5, 6, -1, 7, -1, -1, -1, -1, -1, 8};
        const int allied[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, 9};
        for (int v : axis)
            in.Val(v);
        for (int v : allied)
            in.Val(v);
        in.Val(0x1C00);
        S_SaReader rd(in);
        box->m_axisItems = {2};
        PR(r.boxProps, box->ReadProperties(rd.c));
        PR(r.boxProps, box->HasItemLists() && box->m_shownFlags == 1 && box->GetMaxCarried() == 3 && box->m_capacity == 12 &&
                           box->m_respawnTime == 4 && box->m_textId == 0x1C00 &&
                           box->m_axisItems == std::vector<int>({2, 5, 6, 7, 8}) && box->m_alliedItems == std::vector<int>({9}) &&
                           box->GetItemCount(false) == 5 && box->GetItemList(false)[1] == 5 && box->GetItemCount(true) == 1 &&
                           box->GetItemList(true)[0] == 9);
        box->m_axisItems = {5, 6, 7, 8, 1, 2, 3, 4, 10, 11, 12};
        S_SaBytes e;
        e.Head(0x777, 6 + 1 + 20 + 80 + 4).Val(uint8_t(1)).Val(1).Val(1u).Val(3).Val(12).Val(4);
        for (int i = 0; i < 10; ++i)
            e.Val(box->m_axisItems[i]);
        e.Val(9);
        for (int i = 1; i < 10; ++i)
            e.Val(-1);
        e.Val(0x1C00);
        bool written = false;
        PR(r.boxProps, SaWritten([&](C_chunk &c) { return box->WriteProperties(c); }, &written) == e.b && written);
        box->m_axisItems = {5, 6, 7, 8};
        // Copied: the lists and counts, not the shown flags or the text.
        auto *copy = static_cast<C_SupplyBox *>(creator->Create());
        copy->m_shownFlags = 0;
        copy->m_alliedItems = {1, 1, 1};
        copy->CopyFrom(box);
        PR(r.boxProps, copy->m_axisItems == box->m_axisItems && copy->m_alliedItems == std::vector<int>({9}) &&
                           copy->m_hasItemLists == 1 && copy->m_maxCarried == 3 && copy->m_capacity == 12 &&
                           copy->m_respawnTime == 4 && copy->m_shownFlags == 0 && copy->m_textId == 0x1BFA);
        creator->Destroy(copy);
    }

    // --- the start: the models, the plane's dummy, the animations, the box, shown and opening -------------
    I3D_frame *letibedna = nullptr, *morph = nullptr;
    static S_VertexAnimation ammoboxAnim;
    ammoboxAnim.levelCount = 17;
    g_maAnimations["ammobox"] = &ammoboxAnim;
    static std::string fillName;
    struct S_Fill {
        static void Fill(I3D_model *object)
        {
            I3D_frame *model = reinterpret_cast<I3D_frame *>(object);
            const std::string &file = g_cacheLoads.back();
            if (file == "cratedummy") {
                I3D_frame *f = NewFrame(FRAME_DUMMY, "letibedna", model);
                f->m_name = Stub(f).name.c_str();
                g_itNames[model] = "dummy";
            } else if (file == "parachute") {
                I3D_frame *f = NewFrame(FRAME_VISUAL, "morph", model);
                f->m_name = Stub(f).name.c_str();
                *reinterpret_cast<void ***>(f) = g_prVisualVtable;
                g_itNames[model] = "chute";
            }
        }
    };
    g_cacheFill = &S_Fill::Fill;
    g_gameRole = GAME_ROLE_LOCAL;
    box->m_shownFlags = 1;
    reset();
    g_cacheLoads.clear();
    box->StartMission(true);
    letibedna = box->m_crateDummy ? box->m_crateDummy->FindChildFrame("letibedna", 0xFFFF) : nullptr;
    morph = box->m_parachute ? box->m_parachute->FindChildFrame("morph", 0xFFFF) : nullptr;
    if (box->m_plane)
        g_itNames[box->m_plane] = "plane";
    PR(r.boxStart, box->m_started && g_activations == std::vector<bool>{true} && crate->m_parent == g_sector &&
                       g_cacheLoads == std::vector<std::string>({"cratedummy", "parachute"}) && letibedna &&
                       box->m_flyingCrate == letibedna && morph && box->m_plane &&
                       box->m_plane->GetType() == FRAME_DUMMY && box->m_crateDummy->GetType() == FRAME_MODEL &&
                       std::string(box->m_name.c_str()) == "text 7162" && box->m_textId == 0x1BFA);
    PR(r.boxStart, box->m_anims[0] && reinterpret_cast<S_SeAnim *>(box->m_anims[0])->name == "#cratefly" &&
                       reinterpret_cast<S_SeAnim *>(box->m_anims[1])->name == "#crateopen" &&
                       reinterpret_cast<S_SeAnim *>(box->m_anims[2])->name == "#crateclosed" &&
                       reinterpret_cast<S_SeAnim *>(box->m_anims[3])->name == "#cratedrop" && !box->m_anims[4] && !box->m_anims[5]);
    // The morph player: on the parachute's morph (its own mesh), the animation's last frame, 130 ms.
    PR(r.boxStart, box->m_morphPlayer && box->m_morphPlayer->visual == morph && box->m_morphPlayer->animation == &ammoboxAnim &&
                       box->m_morphPlayer->maxLevel == 16 && box->m_morphPlayer->frameTime == 130 && box->m_morphPlayer->pingPong &&
                       box->m_morphPlayer->stopped && !box->m_morphPlayer->loop && box->m_morphPlayer->startFrame == 0 &&
                       std::count(g_itLog.begin(), g_itLog.end(), "dupmesh morph") == 1 &&
                       std::count(g_itLog.begin(), g_itLog.end(), "addplayer morph 250") == 1);
    // The box volume: the hierarchy's box under the frame's scale; the matrices without it; the
    // sphere from x and z (1.2 times).
    {
        I3D_bbox hb;
        I3D_bbox hbox = *I3DGetDriver()->GetHierarchyBBox(hb, crate);
        const S_vector s = crate->m_scale;
        hbox.max = S_vector(hbox.max.x * s.x, hbox.max.y * s.y, hbox.max.z * s.z);
        hbox.min = S_vector(hbox.min.x * s.x, hbox.min.y * s.y, hbox.min.z * s.z);
        const float hx = (hbox.max.x - hbox.min.x) * 0.5f, hz = (hbox.max.z - hbox.min.z) * 0.5f;
        const float radius = static_cast<float>(static_cast<double>(std::sqrt(hz * hz + hx * hx)) * 1.2);
        S_matrix m = crate->GetMatrix();
        for (int j = 0; j < 3; ++j) {
            m.m[0][j] = (1.0f / s.x) * m.m[0][j];
            m.m[1][j] = (1.0f / s.y) * m.m[1][j];
            m.m[2][j] = (1.0f / s.z) * m.m[2][j];
        }
        const S_CollisionVolume *v = box->m_volume;
        PR(r.boxStart, v && v->flags == COLLISION_SHAPE_BOX && v->type == COLLISION_TYPE_PIKE && v->actor == box &&
                           v->frame == crate && v->radius == radius && RbSameBox(v->bbox, hbox) &&
                           v->matrix == &box->m_boxMatrix && v->invMatrix == &box->m_boxInvMatrix &&
                           SaSame(&box->m_boxMatrix, &m, sizeof m) && RbSame(v->center, crate->GetMatrix()(3)));
        PR(r.boxStart, g_volumeLog.size() == 2 && g_volumeLog[0].op == 'A' && g_volumeLog[1].op == 'M');
    }
    // Shown (at once, at the drop point), then opening: the open animation and its sound.
    PR(r.boxStart, box->m_flags == C_SupplyBox::SUPPLY_SHOWN && box->m_state == C_SupplyBox::SUPPLY_OPENING &&
                       box->m_stateTime == 1000 && box->m_anim == 1 && box->m_animStage == 0 && g_soundRequests.size() == 1 &&
                       g_soundRequests[0].group == 10 && g_soundRequests[0].chapter == 1 &&
                       g_soundRequests[0].flags == SOUND_SILENT_TO_AI && RbSame(g_soundRequests[0].pos, crate->GetMatrix()(3)));
    PR(r.boxStart, std::count(g_itLog.begin(), g_itLog.end(), "fadein crate 0 1") == 1 &&
                       std::count(g_itLog.begin(), g_itLog.end(), "setanim crate #crateopen 0 1 1") == 1);
    // Started: nothing again.
    reset();
    box->StartMission(true);
    PR(r.boxStart, g_itLog.empty() && g_activations.empty());
    g_itNames[box->m_crateDummy] = "dummy";
    g_itNames[box->m_parachute] = "chute";

    // --- the ammunition parts: shown as far as the box is full; taking items ---------------------------------
    {
        box->m_capacity = 12;
        const auto partsAre = [&](int charges) {
            const float empty = 14.0f - (static_cast<float>(charges) / 12.0f) * 14.0f;
            bool ok = true;
            for (int i = 0; i < 14; ++i) {
                if (!ammo[i])
                    continue;
                const bool on = !(static_cast<float>(i) < empty) || (i >= 13 && !(empty >= 14.0f) && !(empty <= 13.0f));
                ok = ok && ammo[i]->IsOn() == on;
            }
            return ok;
        };
        box->m_state = C_SupplyBox::SUPPLY_READY;
        box->m_charges = 12;
        box->m_flags = C_SupplyBox::SUPPLY_SHOWN;
        for (int k = 11; k >= 1; --k) {
            box->UseCharge();
            PR(r.boxStates, box->GetCharges() == k && box->m_state == C_SupplyBox::SUPPLY_READY && partsAre(k));
        }
        // Only a ready, placed box gives charges.
        box->m_state = C_SupplyBox::SUPPLY_OPENING;
        box->UseCharge();
        box->m_state = C_SupplyBox::SUPPLY_READY;
        box->m_flags = 0;
        box->UseCharge();
        PR(r.boxStates, box->GetCharges() == 1 && !box->IsPlaced() && !box->IsBusy());
        box->m_flags = C_SupplyBox::SUPPLY_SHOWN;
        reset();
        box->UseCharge();
        PR(r.boxStates, box->GetCharges() == 0 && box->m_state == C_SupplyBox::SUPPLY_CLOSING && box->IsBusy() &&
                            box->m_stateTime == 1600 && partsAre(0) && g_soundRequests.size() == 1 &&
                            g_soundRequests[0].group == 11);
    }

    // --- the drop: closing -> hidden (respawn s) -> the plane 5 s before -> falling -> landed -> the
    // parachute fades -> opening -> ready ---------------------------------------------------------------------
    {
        box->m_respawnTime = 7;
        box->m_state = C_SupplyBox::SUPPLY_CLOSING;
        box->m_stateTime = 100;
        reset();
        box->Update(100);
        PR(r.boxStates, box->m_state == C_SupplyBox::SUPPLY_HIDDEN && box->m_stateTime == 7000 &&
                            box->m_flags == C_SupplyBox::SUPPLY_FLAG_4 &&
                            std::count(g_itLog.begin(), g_itLog.end(), "fadeout crate 1000 1") == 1 &&
                            std::count(g_itLog.begin(), g_itLog.end(), "on chute 0") == 1 && g_volumeLog.size() == 1 &&
                            g_volumeLog[0].op == 'R');
        // (Hidden: updated still, the timer on; the plane at 5 s before.)
        reset();
        box->Update(1999);
        PR(r.boxStates, box->m_stateTime == 5001 && !box->m_planeFlying && g_soundRequests.empty());
        reset();
        box->Update(1);
        S_vector dir = crate->GetMatrix()(2);
        PrNormalize(dir, (dir.y * dir.y + dir.z * dir.z) + dir.x * dir.x);
        const S_vector planeAt(box->m_dropPos.x - dir.x * 150.0f, (box->m_dropPos.y + 20.0f) - dir.y * 150.0f,
                               box->m_dropPos.z - dir.z * 150.0f);
        PR(r.boxStates, box->m_planeFlying && g_soundRequests.size() == 1 && g_soundRequests[0].attachTo == box->m_plane &&
                            g_soundRequests[0].group == 8 && g_soundRequests[0].flags == (SOUND_SILENT_TO_AI | SOUND_ANY_RANGE) &&
                            g_soundRequests[0].chapter == 1 && RbSame(g_soundRequests[0].pos, S_vector(0, 0, 0)) &&
                            std::count(g_itLog.begin(), g_itLog.end(), "setpos plane " + ItV(planeAt)) == 1);
        // The plane flies on along the crate's z (0.03 m a ms; already on the step that started it).
        S_vector pz = crate->GetMatrix()(2);
        PrNormalize(pz, (pz.x * pz.x + pz.y * pz.y) + pz.z * pz.z);
        const S_vector move1(pz.x * 1.0f * 0.03f, pz.y * 1.0f * 0.03f, pz.z * 1.0f * 0.03f);
        const S_vector after1(move1.x + planeAt.x, move1.y + planeAt.y, move1.z + planeAt.z);
        PR(r.boxStates, std::count(g_itLog.begin(), g_itLog.end(), "setpos plane " + ItV(after1)) == 1);
        reset();
        box->Update(40);
        const S_vector move(pz.x * 40.0f * 0.03f, pz.y * 40.0f * 0.03f, pz.z * 40.0f * 0.03f);
        PR(r.boxStates, std::count(g_itLog.begin(), g_itLog.end(),
                                   "setpos plane " + ItV(S_vector(move.x + after1.x, move.y + after1.y, move.z + after1.z))) == 1);
        // A disabled box is not updated.
        box->m_flags |= C_SupplyBox::SUPPLY_DISABLED;
        reset();
        box->Update(50);
        PR(r.boxStates, g_itLog.empty() && box->m_stateTime == 4960);
        box->m_flags &= ~C_SupplyBox::SUPPLY_DISABLED;
        // Tick: on a pure client nothing.
        g_gameRole = GAME_ROLE_CLIENT;
        box->Tick(50);
        PR(r.boxStates, box->m_stateTime == 4960);
        // (A local game has the client's bit too.)
        g_gameRole = GAME_ROLE_LOCAL;
        box->Tick(60);
        PR(r.boxStates, box->m_stateTime == 4960);
        g_gameRole = GAME_ROLE_SERVER;
        box->Tick(60);
        PR(r.boxStates, box->m_stateTime == 4900);
        g_gameRole = GAME_ROLE_LOCAL;
        // The drop: 85 m over the drop point (a local game), the cratedummy 3 m higher, the crate and the
        // parachute under "letibedna", the cratedummy's animation.
        box->m_stateTime = 10;
        reset();
        box->Update(10);
        PR(r.boxStates, box->m_state == C_SupplyBox::SUPPLY_FALLING && box->m_stateTime == -1 &&
                            crate->m_parent == letibedna && box->m_parachute->m_parent == letibedna && box->GetCharges() == 12 &&
                            box->m_health == 6000 && box->m_flags == C_SupplyBox::SUPPLY_SHOWN);
        PR(r.boxStates, std::count(g_itLog.begin(), g_itLog.end(),
                                   "setpos crate " + ItV(S_vector(box->m_dropPos.x, 85.0f + box->m_dropPos.y, box->m_dropPos.z))) == 1 &&
                            std::count(g_itLog.begin(), g_itLog.end(),
                                       "setpos dummy " + ItV(S_vector(box->m_dropPos.x, (85.0f + 3.0f) + box->m_dropPos.y,
                                                                      box->m_dropPos.z))) == 1 &&
                            std::count(g_itLog.begin(), g_itLog.end(), "setanim dummy #cratefly 0 1 2") == 1 &&
                            std::count(g_itLog.begin(), g_itLog.end(), "fadein chute 0 1") == 1);
        // (In a network game under the scene's top.)
        g_gameRole = GAME_ROLE_SERVER;
        g_scanMax[1] = 60.0f;
        box->m_state = C_SupplyBox::SUPPLY_HIDDEN;
        box->m_stateTime = 5;
        reset();
        box->Update(5);
        PR(r.boxStates, std::count(g_itLog.begin(), g_itLog.end(),
                                   "setpos crate " + ItV(S_vector(box->m_dropPos.x, (60.0f - 0.1f) + box->m_dropPos.y,
                                                                  box->m_dropPos.z))) == 1);
        g_scanMax[1] = 1000.0f;
        box->m_state = C_SupplyBox::SUPPLY_HIDDEN;
        box->m_stateTime = 5;
        reset();
        box->Update(5);
        PR(r.boxStates, std::count(g_itLog.begin(), g_itLog.end(),
                                   "setpos crate " + ItV(S_vector(box->m_dropPos.x, 85.0f + box->m_dropPos.y, box->m_dropPos.z))) == 1);
        // (Falling only from hidden: otherwise just the state.)
        box->m_state = C_SupplyBox::SUPPLY_READY;
        reset();
        box->SetState(C_SupplyBox::SUPPLY_FALLING);
        PR(r.boxStates, box->m_state == C_SupplyBox::SUPPLY_FALLING && g_itLog.empty());
    }

    // --- the fall: 5 m a second; the box swept, then rays from the feet and their middles and a sphere
    // from the middle; it stops 0.01 m over the ground and lands --------------------------------------------
    {
        g_flatGround = true;
        g_groundY = 0.0f;
        int steps = 0, sweeps = 0;
        float lastY = box->m_crateDummy->GetWorldPos().y;
        while (box->m_state == C_SupplyBox::SUPPLY_FALLING && steps < 2000) {
            const int t = 30 + steps % 50;
            const int boxTouches = g_boxTouches;
            reset();
            box->Update(t);
            ++steps;
            const float fall = static_cast<float>(static_cast<uint32_t>(t)) * 0.005f;
            const float y = box->m_crateDummy->GetWorldPos().y;
            if (g_boxTouches == boxTouches) {
                // No touch: the full fall, no rays.
                PR(r.boxFall, y == lastY - fall && g_prLines.empty());
            } else {
                ++sweeps;
                PR(r.boxFall, g_prLines.size() == 7 && g_prLines[0].color == 0xFF0000 && g_prLines[6].color == 0xFF00 &&
                                  g_prLines[0].to.y == -fall + g_prLines[0].from.y && y <= lastY);
            }
            lastY = y;
        }
        // Landed: the lowest foot 0.01 m over the ground (the rays' stop), the drop animation, the
        // parachute and the crate put back in the world, the crate turned back.
        PR(r.boxFall, box->m_state == C_SupplyBox::SUPPLY_LANDED && sweeps > 0 && box->m_stateTime == 2000 &&
                          box->m_anim == 3 && !box->m_planeFlying && crate->m_parent == nullptr &&
                          box->m_parachute->m_parent == nullptr && !box->m_morphPlayer->stopped &&
                          SaSameQuat(crate->m_rot, SaNormalized(box->m_dropRot)) &&
                          g_soundRequests.size() == 1 && g_soundRequests[0].group == 9);
        float lowest = 1e9f;
        for (const S_vector4 &f : box->m_feet)
            lowest = std::min(lowest, (S_vector(f.x, f.y, f.z) * walls->GetMatrix()).y);
        PR(r.boxFall, std::fabs(lowest - 0.01f) < 0.002f);
        // The parachute fades (1 s), the crate opens (its animation, 1 s at least), then it is ready.
        reset();
        box->Update(2000);
        PR(r.boxFall, box->m_state == C_SupplyBox::SUPPLY_PARACHUTE_FADING && box->m_stateTime == 1000 &&
                          std::count(g_itLog.begin(), g_itLog.end(), "fadeout chute 1000 1") == 1);
        reset();
        box->Update(1000);
        PR(r.boxFall, box->m_state == C_SupplyBox::SUPPLY_OPENING && box->m_stateTime == 1000 && box->m_morphPlayer->stopped &&
                          g_soundRequests.size() == 1 && g_soundRequests[0].group == 10);
        animEnds["#crateopen"] = 1700;
        box->SetState(C_SupplyBox::SUPPLY_OPENING);
        PR(r.boxFall, box->m_stateTime == 1700);
        reset();
        box->Update(1700);
        PR(r.boxFall, box->m_state == C_SupplyBox::SUPPLY_READY && box->m_stateTime == -1);
        animEnds["#crateopen"] = 700;
        // What it lands on: humans and vehicles under it are hit (-1, on the server) and keep it up.
        g_flatGround = false;
        static S_CollisionVolume crushed;
        static C_Human *victim;
        victim = new C_Human();
        crushed = S_CollisionVolume();
        crushed.flags = COLLISION_FLAG_DYNAMIC;
        crushed.type = 1;
        crushed.actor = victim;
        struct S_Crush {
            static S_CollisionVolume *Ray(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *,
                                          uint32_t, uint32_t)
            {
                if (hitPos)
                    *hitPos = S_vector(from.x, from.y + dir.y * 0.5f, from.z);
                return &crushed;
            }
        };
        g_rayTestHook = &S_Crush::Ray;
        g_flatGround = true;
        g_groundY = 0.0f;
        box->m_state = C_SupplyBox::SUPPLY_HIDDEN;
        box->SetState(C_SupplyBox::SUPPLY_FALLING);
        g_gameRole = GAME_ROLE_SERVER;
        const size_t hits = victim->hits.size();
        int crushSteps = 0;
        for (int step = 0; step < 800 && box->m_state == C_SupplyBox::SUPPLY_FALLING; ++step) {
            reset();
            box->Update(50);
            if (!g_prLines.empty())
                ++crushSteps;
        }
        PR(r.boxFall, box->m_state == C_SupplyBox::SUPPLY_FALLING && crushSteps > 0 && victim->hits.size() > hits &&
                          victim->hits.back().amount == -1.0f && victim->hits.back().type == HIT_FALL && !victim->hits.back().attacker &&
                          RbSame(victim->hits.back().pos, S_vector(0, 0, 0)) && RbSame(victim->hits.back().dir, S_vector(0, 0, 0)) &&
                          victim->hits.back().flags == 0);
        g_rayTestHook = nullptr;
        g_flatGround = false;
        box->m_state = C_SupplyBox::SUPPLY_READY;
    }

    // --- the network: enabled on a server (the state sent), the messages, a joining client's state ---------
    {
        g_gameRole = GAME_ROLE_SERVER;
        reset();
        box->SetEnabled(true);
        PR(r.boxNet, box->m_state == C_SupplyBox::SUPPLY_HIDDEN && box->m_stateTime == 3000 && g_streams.size() == 1 &&
                         g_streams[0]->id == 70 && g_streams[0]->sent && !PrBits(0).empty() && PrBits(0)[0] == std::make_pair(0u, 1));
        const std::vector<std::pair<uint32_t, int>> sent = PrBits(0);
        PR(r.boxNet, sent.size() == 1 + 9 + 6 + 1 && sent[1] == std::make_pair(box->m_flags, 6) &&
                         sent[2] == std::make_pair(uint32_t(box->GetCharges()), 32) && sent[3] == std::make_pair(6000u, 32) &&
                         sent[8] == std::make_pair(2u, 4) && sent[9] == std::make_pair(3000u, 32) && sent[10].second == -11 &&
                         sent[16] == std::make_pair(uint32_t(g_prAnimTime), 32));
        // (A local game sends nothing.)
        g_gameRole = GAME_ROLE_LOCAL;
        reset();
        box->SetEnabled(true);
        PR(r.boxNet, g_streams.empty() && box->m_stateTime == 3000);
        // Off: hidden at once and disabled.
        reset();
        box->SetEnabled(false);
        PR(r.boxNet, box->m_flags == C_SupplyBox::SUPPLY_DISABLED && std::count(g_itLog.begin(), g_itLog.end(), "fadeout crate 0 1") == 1);
        // Destroyed (kind 1): a disabled box is not.
        g_readBits = {{1, 1}};
        g_readPos = 0;
        box->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.boxNet, box->m_health == 6000 && box->m_state == C_SupplyBox::SUPPLY_HIDDEN);
        box->m_flags = C_SupplyBox::SUPPLY_SHOWN;
        g_readPos = 0;
        reset();
        box->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.boxNet, box->m_health == 0 && box->m_state == C_SupplyBox::SUPPLY_DESTROYED && box->m_stateTime == 1000 &&
                         box->m_flags == C_SupplyBox::SUPPLY_FLAG_4 && g_soundRequests.size() == 1 && g_soundRequests[0].group == 12 &&
                         std::count(g_itLog.begin(), g_itLog.end(), "fadeout crate 0 1") == 1);
        box->Damage(5);
        PR(r.boxNet, box->m_health == -5);
        box->m_health = 100;
        box->m_flags = 0;
        box->Damage(40);
        PR(r.boxNet, box->m_health == 60);
        box->Damage(60);
        PR(r.boxNet, box->m_health == 0 && box->m_state == C_SupplyBox::SUPPLY_DESTROYED);
        // The state (kind 0): applied.
        const auto stateBits = [](uint32_t flags, int charges, int state, int stateTime, const S_vector &pos, const S_vector &crate2,
                                  int animTime) {
            std::vector<std::pair<uint32_t, int>> b = {{flags, 6}, {uint32_t(charges), 32}, {1234u, 32}, {1u, 1}, {0u, 32},
                                                       {1u, 32}, {2u, 4}, {uint32_t(state), 4}, {uint32_t(stateTime), 32}};
            for (float v : {pos.x, pos.y, pos.z})
                b.emplace_back(PrFloatBits(v), -11);
            for (float v : {crate2.x, crate2.y, crate2.z})
                b.emplace_back(PrFloatBits(v), -11);
            b.emplace_back(uint32_t(animTime), 32);
            return b;
        };
        g_readBits = {{0, 1}};
        for (const auto &b : stateBits(1, 7, C_SupplyBox::SUPPLY_READY, 77, S_vector(1, 2, 3), S_vector(0, 0, 0), 55))
            g_readBits.push_back(b);
        g_readPos = 0;
        reset();
        box->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.boxNet, box->GetCharges() == 7 && box->m_health == 1234 && box->m_planeFlying && box->m_state == C_SupplyBox::SUPPLY_READY &&
                         box->m_stateTime == 77 && box->m_flags == 1 && box->m_anim == 1 &&
                         std::count(g_itLog.begin(), g_itLog.end(), "setpos crate 1 2 3") == 1 &&
                         std::count(g_itLog.begin(), g_itLog.end(), "tick crate 10000") == 1 &&
                         std::count(g_itLog.begin(), g_itLog.end(), "link chute 0 1") == 1);
        // The joining client's: the same bits without the kind; read back.
        g_readBits = stateBits(1, 3, C_SupplyBox::SUPPLY_LANDED, 400, S_vector(4, 5, 6), S_vector(0, 0, 0), 66);
        g_readPos = 0;
        reset();
        box->ReadState(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.boxNet, box->GetCharges() == 3 && box->m_state == C_SupplyBox::SUPPLY_LANDED && box->m_anim == 3 &&
                         std::count(g_itLog.begin(), g_itLog.end(), "tick crate 66") == 1 &&
                         std::count(g_itLog.begin(), g_itLog.end(), "setpos crate 4 5 6") == 1);
        g_readBits = stateBits(1, 3, C_SupplyBox::SUPPLY_FALLING, -1, S_vector(4, 50, 6), S_vector(0.5f, -1.0f, 0.25f), 0);
        g_readPos = 0;
        reset();
        box->ReadState(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.boxNet, box->m_state == C_SupplyBox::SUPPLY_FALLING && crate->m_parent == letibedna &&
                         std::count(g_itLog.begin(), g_itLog.end(), "setpos dummy 4 50 6") == 1 &&
                         std::count(g_itLog.begin(), g_itLog.end(), "setpos crate 0.5 -1 0.25") == 1);
        reset();
        box->WriteState(reinterpret_cast<C_BitStream *>(g_streams.emplace_back(new S_FakeStream{0, false, {}, false})));
        const std::vector<std::pair<uint32_t, int>> w = PrBits(0);
        const S_vector dummyAt = box->m_crateDummy->GetWorldPos(), crateAt = crate->GetWorldPos();
        PR(r.boxNet, w.size() == 16 && w[0] == std::make_pair(box->m_flags, 6) && w[7] == std::make_pair(3u, 4) &&
                         w[9].first == PrFloatBits(dummyAt.x) && w[10].first == PrFloatBits(dummyAt.y) &&
                         w[12].first == PrFloatBits(crateAt.x) && w[14].first == PrFloatBits(crateAt.z));
        box->m_state = C_SupplyBox::SUPPLY_READY;
    }

    // --- the saved game: the base's chunk, 0x202 the state (0x60 bytes); read back ---------------------------
    {
        g_gameRole = GAME_ROLE_LOCAL;
        box->SetState(C_SupplyBox::SUPPLY_LANDED);
        box->m_charges = 5;
        box->m_health = 4321;
        bool saved = false;
        const std::vector<uint8_t> bytes = SaWritten([&](C_chunk &c) { return box->SaveGame(c); }, &saved);
        PR(r.boxSave, saved && bytes.size() == 6 + 6 + 4 + 6 + 0x60 && bytes[12 + 4] == 0x02 && bytes[12 + 5] == 0x02);
        C_SupplyBox::S_State st;
        if (bytes.size() >= 6 + 6 + 4 + 6 + 0x60)
            std::memcpy(&st, bytes.data() + 6 + 6 + 4 + 6, sizeof st);
        PR(r.boxSave, st.charges == 5 && st.health == 4321 && st.state == C_SupplyBox::SUPPLY_LANDED && st.stateTime == 2000 &&
                          st.anim == 3 && st.animTime == g_prAnimTime && st.unknown24[0] == 0 && st.unknown54[2] == 0 &&
                          RbSame(S_vector(st.pos.x, st.pos.y, st.pos.z), crate->GetWorldPos()) && st.crate.x == 0.0f);
        box->m_charges = 9;
        box->m_state = C_SupplyBox::SUPPLY_READY;
        S_SaBytes in;
        in.Put(bytes.data(), bytes.size());
        S_SaReader rd(in);
        reset();
        PR(r.boxSave, box->LoadGame(rd.c) && box->GetCharges() == 5 && box->m_health == 4321 &&
                          box->m_state == C_SupplyBox::SUPPLY_LANDED);
        // (Other chunks are skipped.)
        S_SaBytes other;
        other.Head(0x777, 6 + 10 + 7 + 6 + 0x60).Head(0x3039, 10).Val(0).Head(0x55, 7).Val(uint8_t(1)).Head(0x202, 6 + 0x60);
        C_SupplyBox::S_State st2 = st;
        st2.charges = 2;
        other.Put(&st2, sizeof st2);
        S_SaReader rd2(other);
        PR(r.boxSave, box->LoadGame(rd2.c) && box->GetCharges() == 2);
    }

    // --- stop and restart ------------------------------------------------------------------------------------
    {
        reset();
        I3D_frame *dummy = box->m_crateDummy;
        box->RestartMission(true);
        PR(r.boxStop, box->m_started && box->m_state == C_SupplyBox::SUPPLY_OPENING && box->m_volume && box->m_crateDummy == dummy &&
                          std::count(g_itLog.begin(), g_itLog.end(), "on dummy 0") == 1 &&
                          std::count(g_itLog.begin(), g_itLog.end(), "link chute 0 1") == 1 && g_volumeLog.size() >= 2 &&
                          g_volumeLog[0].op == 'R');
        reset();
        box->StopMission(true);
        PR(r.boxStop, !box->m_started && !box->m_crateDummy && !box->m_parachute && !box->m_plane && !box->m_morphPlayer &&
                          !box->m_anims[0] && !box->m_volume && g_deactivations == std::vector<bool>{true} &&
                          std::count(g_itLog.begin(), g_itLog.end(), "removeplayer morph 0") == 1 &&
                          std::count(g_itLog.begin(), g_itLog.end(), "release dummy") == 1 &&
                          std::count(g_itLog.begin(), g_itLog.end(), "release chute") == 1 &&
                          std::count(g_itLog.begin(), g_itLog.end(), "animrelease #cratefly") == 1);
        reset();
        box->StopMission(true);
        PR(r.boxStop, g_itLog.empty() && g_deactivations.empty());
    }

    // --- the rest: the values, a new box, the feet's choices, short chunks, the animations' calls, the
    // drop's calls, the fall's tests, the states applied --------------------------------------------------------
    {
        using C = C_SupplyBox;
        PR(r.box, C::SUPPLY_READY == 0 && C::SUPPLY_CLOSING == 1 && C::SUPPLY_HIDDEN == 2 && C::SUPPLY_FALLING == 3 &&
                      C::SUPPLY_LANDED == 4 && C::SUPPLY_PARACHUTE_FADING == 5 && C::SUPPLY_OPENING == 6 &&
                      C::SUPPLY_DESTROYED == 7 && C::SUPPLY_SHOWN == 1 && C::SUPPLY_DISABLED == 2 && C::SUPPLY_FLAG_4 == 4 &&
                      C::SUPPLY_MOVE_TO_DROP == 8 && C::SUPPLY_AT_ONCE == 0x10);
        auto *fresh = static_cast<C_SupplyBox *>(creator->Create());
        PR(r.box, fresh->m_hasItemLists == 0 && fresh->m_shownFlags == 0 && fresh->m_flags == 0 && fresh->m_maxCarried == 5 &&
                      fresh->m_capacity == 20 && fresh->m_charges == 0 && fresh->m_respawnTime == -1 && fresh->m_health == 6000 &&
                      fresh->m_blendTime == 0 && fresh->m_animStage == -1 && fresh->m_anim == 5 && fresh->m_stateTime == 0 &&
                      fresh->m_state == C::SUPPLY_READY && !fresh->m_started);
        // A short properties chunk: only the items there.
        {
            S_SaBytes in;
            in.Head(0x777, 6 + 1 + 20 + 12).Val(uint8_t(1)).Val(0).Val(0u).Val(5).Val(20).Val(-1).Val(4).Val(5).Val(6);
            S_SaReader rd(in);
            fresh->ReadProperties(rd.c);
            PR(r.boxProps, fresh->m_axisItems == std::vector<int>({4, 5, 6}) && fresh->m_alliedItems.empty());
        }
        // SetFrame empties and defaults all of it.
        static S_CollisionVolume stale;
        fresh->m_alliedItems = {3};
        fresh->m_hasItemLists = 1;
        fresh->m_capacity = 3;
        fresh->m_respawnTime = 5;
        fresh->m_started = true;
        fresh->m_volume = &stale;
        I3D_frame *bare = NewFrame(FRAME_MODEL, "bare2", nullptr);
        fresh->SetFrame(bare);
        PR(r.box, fresh->m_axisItems.empty() && fresh->m_alliedItems.empty() && fresh->m_hasItemLists == 0 &&
                      fresh->m_capacity == 20 && fresh->m_respawnTime == -1 && !fresh->m_started && !fresh->m_volume);
        creator->Destroy(fresh);

        // The feet: other walls (their box from minY), other vertices; the oracle as above.
        const auto feetOf = [&](float minY, const std::vector<S_vector> &verts) {
            I3D_frame *c = NewFrame(FRAME_MODEL, "crate2", nullptr);
            c->m_name = Stub(c).name.c_str();
            I3D_frame *w = RbBoxFrame(c, "Steny02", -0.5f, 0.5f, minY, 0.8f, -0.4f, 0.4f, S_vector(0, 0, 0));
            w->m_name = Stub(w).name.c_str();
            PrMesh(w, verts);
            auto *b = static_cast<C_SupplyBox *>(creator->Create());
            b->SetFrame(c);
            I3D_bbox hb;
            const I3D_bbox hbox = *I3DGetDriver()->GetHierarchyBBox(hb, c);
            const float limit = (hbox.max.y - hbox.min.y) * 0.5f;
            S_vector4 f[4] = {};
            int n = 0;
            for (const S_vector &v : verts)
                if (v.y < limit && n < 4)
                    f[n++] = {v.x, v.y, v.z, 0.0f};
            const auto dist = [](const S_vector4 &a, const S_vector4 &b2) {
                const float dx = a.x - b2.x, dy = a.y - b2.y, dz = a.z - b2.z;
                return PrLength((dz * dz + dy * dy) + dx * dx);
            };
            if (dist(f[0], f[2]) < dist(f[0], f[1]))
                std::swap(f[1], f[2]);
            else if (dist(f[0], f[3]) < dist(f[0], f[1]))
                std::swap(f[1], f[3]);
            const float d03 = dist(f[0], f[3]);
            const float ax = f[0].x - f[2].x, ay = f[0].y - f[2].y, az = f[0].z - f[2].z;
            if (PrLength((ax * ax + ay * ay) + az * az) > d03)
                std::swap(f[2], f[3]);
            bool same = true;
            for (int i = 0; i < 4; ++i)
                same = same && b->m_feet[i].x == f[i].x && b->m_feet[i].y == f[i].y && b->m_feet[i].z == f[i].z;
            S_vector4 got[4];
            std::memcpy(got, b->m_feet, sizeof got);
            creator->Destroy(b);
            return std::make_pair(same, std::vector<S_vector4>(got, got + 4));
        };
        const auto feetAre = [&](float minY, const std::vector<S_vector> &verts, const std::vector<S_vector> &expect) {
            const auto got = feetOf(minY, verts);
            bool ok = got.first;
            for (size_t i = 0; ok && i < expect.size(); ++i)
                ok = got.second[i].x == expect[i].x && got.second[i].y == expect[i].y && got.second[i].z == expect[i].z;
            return ok;
        };
        // (The first vertex taken; one at exactly half the height not; a box from below 0.)
        PR(r.boxFeet, feetAre(0.0f, {{0.1f, 0.0f, 0.0f}, {0.0f, 0.4f, 0.0f}, {0.3f, 0.0f, 0.0f}, {0.1f, 0.0f, 0.7f},
                                     {0.4f, 0.0f, 0.6f}},
                              {{0.1f, 0.0f, 0.0f}, {0.3f, 0.0f, 0.0f}, {0.4f, 0.0f, 0.6f}, {0.1f, 0.0f, 0.7f}}));
        PR(r.boxFeet, feetAre(-0.2f, {{0.1f, 0.35f, 0.0f}, {0.3f, 0.0f, 0.0f}, {0.1f, 0.0f, 0.7f}, {0.4f, 0.0f, 0.6f}},
                              {{0.1f, 0.35f, 0.0f}}));
        // (Equal distances: no swap; a 0 length; a length of exactly 1; heights and both signs.)
        PR(r.boxFeet, feetOf(0.0f, {{0.0f, 0.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.5f}, {0.9f, 0.0f, 0.9f}}).first);
        PR(r.boxFeet, feetOf(0.0f, {{0.0f, 0.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, {0.6f, 0.0f, 0.6f}, {0.0f, 0.0f, 0.5f}}).first);
        PR(r.boxFeet, feetOf(0.0f, {{0.0f, 0.0f, 0.0f}, {0.3f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}}).first);
        PR(r.boxFeet, feetOf(0.0f, {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.2f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.1f}}).first);
        PR(r.boxFeet, feetOf(0.0f, {{0.2f, 0.1f, -0.3f}, {0.5f, 0.35f, 0.0f}, {0.6f, 0.0f, 0.1f}, {-0.3f, 0.3f, 0.3f}}).first);
        PR(r.boxFeet, feetOf(0.0f, {{-0.4f, 0.3f, 0.2f}, {0.1f, 0.0f, 0.3f}, {0.4f, 0.35f, -0.3f}, {-0.1f, 0.05f, -0.35f}}).first);
        PR(r.boxFeet, feetOf(0.0f, {{0.0f, 0.0f, 0.0f}, {0.1f, 0.0f, 0.0f}, {0.3f, 0.0f, 0.4f}, {0.5f, 0.0f, 0.0f}}).first);
        PR(r.boxFeet, feetOf(0.0f, {{0.3f, 0.1f, 0.2f}, {0.35f, 0.1f, 0.25f}, {-0.3f, 0.3f, -0.2f}, {0.4f, 0.0f, -0.35f}}).first);
        PR(r.boxFeet, feetAre(0.0f, {{0.0f, 0.0f, 0.0f}, {0.3f, 0.35f, 0.0f}, {0.4f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.38f}},
                              {{0.0f, 0.0f, 0.0f}, {0.4f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.38f}, {0.3f, 0.35f, 0.0f}}));

        // Started again (a local game): restarted with nothing shown (all of the defaults again).
        g_gameRole = GAME_ROLE_LOCAL;
        box->m_shownFlags = 1;
        box->StartMission(true);
        g_itNames[box->m_crateDummy] = "dummy";
        g_itNames[box->m_parachute] = "chute";
        g_itNames[box->m_plane] = "plane";
        box->m_shownFlags = 0;
        box->m_health = 1;
        box->m_blendTime = 5;
        box->m_animStage = 1;
        box->m_anim = 2;
        box->m_state = C::SUPPLY_LANDED;
        box->m_stateTime = 9;
        box->m_planeFlying = true;
        box->m_morphPlayer->stopped = false;
        box->m_morphPlayer->startFrame = 3;
        std::memset(&box->m_boxMatrix, 0, sizeof box->m_boxMatrix);
        std::memset(&box->m_boxInvMatrix, 0, sizeof box->m_boxInvMatrix);
        for (I3D_frame *a : ammo)
            if (a)
                a->SetOn(false);
        const S_vector oldScale = crate->m_scale;
        crate->m_scale = S_vector(1.5f, 1.25f, 0.8f);
        const size_t reactivations = g_obReactivations.size();
        reset();
        box->RestartMission(true);
        S_matrix m = crate->GetMatrix();
        for (int j = 0; j < 3; ++j) {
            m.m[0][j] = (1.0f / crate->m_scale.x) * m.m[0][j];
            m.m[1][j] = (1.0f / crate->m_scale.y) * m.m[1][j];
            m.m[2][j] = (1.0f / crate->m_scale.z) * m.m[2][j];
        }
        bool partsOn = true;
        for (I3D_frame *a : ammo)
            partsOn = partsOn && (!a || a->IsOn());
        PR(r.boxStop, box->m_health == 6000 && box->m_blendTime == 0 && box->m_animStage == -1 && box->m_anim == 5 &&
                          box->m_state == C::SUPPLY_READY && box->m_stateTime == 0 && !box->m_planeFlying &&
                          box->m_morphPlayer->stopped && box->m_morphPlayer->startFrame == 0 && partsOn &&
                          SaSame(&box->m_boxMatrix, &m, sizeof m) && g_obReactivations.size() == reactivations + 1);
        {
            S_matrix inv;
            inv.Inverse(m);
            PR(r.boxStop, SaSame(&box->m_boxInvMatrix, &inv, sizeof inv));
        }
        crate->m_scale = oldScale;
        PR(r.boxStop, SaLogIs({"on dummy 0", "link chute 0 1", "link crate 0 1", "setanim crate #crateclosed 1 1 1", "tick crate 0",
                               "setanim crate #crateopen 1 1 1", "tick crate 0", "stopanim crate 1", "animweight crate 1 1",
                               "animweight crate 0 0", "animactive crate 1", "tick crate 1", "findanim ammobox",
                               "removeplayer morph 0", "dupmesh morph", "animinit morph", "animreset morph 0",
                               "addplayer morph 250", "on Ammo00 1", "on Ammo01 1", "on Ammo02 1", "on Ammo03 1",
                               "on Ammo04 1", "on Ammo05 1", "on Ammo06 1", "on Ammo08 1", "on Ammo09 1", "on Ammo10 1",
                               "on Ammo11 1", "on Ammo12 1", "on Ammo13 1", "on chute 0", "fadeout crate 0 1", "on chute 0",
                               "fadeout crate 0 1"}));
        box->m_shownFlags = 1;
        const auto Vec4 = [](const S_vector4 &v) { return S_vector(v.x, v.y, v.z); };
        // (The log after a call, named for reading.)
        const auto logIs = [](const char *, const std::vector<std::string> &expect) { return SaLogIs(expect); };

        // Shown at the drop point: faded in, moved there, the box volume moved.
        reset();
        box->SetVisibility(C::SUPPLY_SHOWN | C::SUPPLY_MOVE_TO_DROP | C::SUPPLY_AT_ONCE);
        PR(r.boxStates, box->m_flags == C::SUPPLY_SHOWN && logIs("show", {"fadein crate 0 1", "setpos crate " + ItV(Vec4(box->m_dropPos)), "update crate",
                                                                             "sector crate " + ItV(Vec4(box->m_dropPos))}) && g_volumeLog.size() == 1);

        // The animations: from none on stage 0 at once; then the other stage blended in (350 ms), and back;
        // none for 0; the reset's on stage 0 when there was none; a stage of -1 is not blended.
        box->m_anim = 5;
        box->m_animStage = -1;
        box->m_blendTime = 7;
        reset();
        box->SetAnimation(1);
        PR(r.boxStates, box->m_animStage == 0 && box->m_anim == 1 && box->m_blendTime == 0 && logIs("anim1", {"setanim crate #crateopen 0 1 1"}));
        reset();
        box->SetAnimation(2);
        PR(r.boxStates, box->m_animStage == 1 && box->m_anim == 2 && box->m_blendTime == 350 && logIs("anim2", {"setanim crate #crateclosed 1 0 1"}));
        reset();
        box->SetAnimation(3);
        PR(r.boxStates, box->m_animStage == 0 && box->m_anim == 3 && box->m_blendTime == 350 && logIs("anim3", {"setanim crate #cratedrop 0 0 1"}));
        reset();
        box->SetAnimation(0);
        PR(r.boxStates, g_itLog.empty() && box->m_anim == 3);
        box->m_animStage = -1;
        reset();
        box->SetAnimation(5);
        PR(r.boxStates, box->m_animStage == 0 && box->m_anim == 5 && logIs("anim5", {"setanim crate #crateclosed 0 1 1", "tick crate 0", "setanim crate #crateopen 0 1 1", "tick crate 0", "stopanim crate 0"}));
        box->m_anim = 1;
        box->m_animStage = -1;
        reset();
        box->SetAnimation(2);
        PR(r.boxStates, box->m_animStage == 0 && box->m_blendTime == 0 && logIs("anim6", {"setanim crate #crateclosed 0 1 1"}));
        // The blend's weights in whole steps of 350 ms (a step below 0 clamps); none without a blend time.
        g_prAnimActive = true;
        box->m_animStage = 1;
        const int blends[5][2] = {{350, 100}, {800, 50}, {10, 410}, {0, 20}, {1, 1}};
        for (const auto &bt : blends) {
            box->m_blendTime = bt[0];
            reset();
            box->TickAnimation(bt[1]);
            std::vector<std::string> expect;
            if (bt[0] > 0) {
                const float left = std::max(0.0f, static_cast<float>((bt[0] - bt[1]) / 350));
                char a[64], b[64];
                std::snprintf(a, sizeof a, "animweight crate 1 %.9g", 1.0f - left);
                std::snprintf(b, sizeof b, "animweight crate 0 %.9g", left);
                expect = {a, b};
            }
            expect.push_back("animactive crate 1");
            expect.push_back("tick crate " + std::to_string(bt[1]));
            PR(r.boxStates, box->m_blendTime == (bt[0] > 0 ? bt[0] - bt[1] : bt[0]) && SaLogIs(expect));
        }
        // (No stage: not asked if active.)
        box->m_animStage = -1;
        g_prAnimActive = false;
        box->m_anim = 2;
        reset();
        box->TickAnimation(5);
        PR(r.boxStates, box->m_anim == 2 && logIs("nostage", {"tick crate 5"}));
        box->m_animStage = 0;
        box->TickAnimation(5);
        PR(r.boxStates, box->m_anim == 5);
        g_prAnimActive = true;

        // (No time: nothing.)
        box->m_state = C::SUPPLY_CLOSING;
        box->m_stateTime = 0;
        box->TickState(5);
        PR(r.boxStates, box->m_state == C::SUPPLY_CLOSING && box->m_stateTime == 0);
        // (The last part stays with anything left.)
        {
            const int capacity = box->m_capacity, charges = box->m_charges;
            box->m_capacity = 20;
            box->m_charges = 1;
            box->UpdateAmmoParts();
            PR(r.boxStates, ammo[13]->IsOn() && !ammo[12]->IsOn());
            box->m_capacity = capacity;
            box->m_charges = charges;
        }
        // The state's timer: 1 ms left is not yet, run out exactly it is; hidden without a respawn time:
        // no plane; respawn 0 s: none, 1 s: 1000 ms.
        box->m_state = C::SUPPLY_PARACHUTE_FADING;
        box->m_stateTime = 2;
        box->TickState(1);
        PR(r.boxStates, box->m_state == C::SUPPLY_PARACHUTE_FADING && box->m_stateTime == 1);
        box->TickState(1);
        PR(r.boxStates, box->m_state == C::SUPPLY_OPENING);
        box->m_respawnTime = -1;
        box->m_state = C::SUPPLY_CLOSING;
        box->m_stateTime = 5;
        box->m_planeFlying = false;
        reset();
        box->TickState(5);
        PR(r.boxStates, box->m_state == C::SUPPLY_HIDDEN && box->m_stateTime == -1 && !box->m_planeFlying && g_soundRequests.empty());
        box->m_respawnTime = 0;
        box->m_morphPlayer->stopped = false;
        box->SetState(C::SUPPLY_HIDDEN);
        PR(r.boxStates, box->m_stateTime == -1 && box->m_morphPlayer->stopped);
        box->m_respawnTime = 1;
        box->SetState(C::SUPPLY_HIDDEN);
        PR(r.boxStates, box->m_stateTime == 1000);
        box->m_anim = 1;
        reset();
        box->SetState(C::SUPPLY_CLOSING);
        PR(r.boxStates, box->m_anim == 2 && g_soundRequests.size() == 1 && !g_soundRequests[0].source &&
                            g_soundRequests[0].posW == crate->GetMatrix().m[3][3] && !g_soundRequests[0].attachTo);

        // The plane: 150 m behind along the crate's z normalised (summed y, z, x) as the inline copies
        // do (a tiny x or z goes to 1, none to up), then on along it (summed x, y, z).
        const S_vector rows[5] = {S_vector(1.2f, 0.0f, 1.6f), S_vector(1e-5f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, 3e-6f),
                                  S_vector(0.0f, 0.0f, 0.0f), S_vector(0.3f, 1.2f, 1.6f)};
        const float n5 = 1.0f / std::sqrt(4.09f);
        const S_vector norms[5] = {S_vector(0.6f, 0.0f, 0.8f), S_vector(1.0f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, 1.0f),
                                   S_vector(0.0f, 1.0f, 0.0f), S_vector(0.3f * n5, 1.2f * n5, 1.6f * n5)};
        const S_matrix savedLocal = crate->m_localMatrix;
        for (int k = 0; k < 5; ++k) {
            crate->m_localMatrix(2) = rows[k];
            crate->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            box->m_state = C::SUPPLY_HIDDEN;
            box->m_stateTime = 5002;
            box->m_planeFlying = false;
            box->m_dropPos = {3.0f, 2.0f, -4.0f, 1.0f};
            reset();
            box->TickState(2);
            S_vector dir = crate->GetMatrix()(2);
            PrNormalize(dir, (dir.y * dir.y + dir.z * dir.z) + dir.x * dir.x);
            const S_vector at(3.0f - dir.x * 150.0f, (2.0f + 20.0f) - dir.y * 150.0f, -4.0f - dir.z * 150.0f);
            PR(r.boxStates, box->m_planeFlying && RbSame(dir, norms[k]) && SaLogIs({"setpos plane " + ItV(at), "update plane"}) &&
                                g_soundRequests.size() == 1 && !g_soundRequests[0].source);
            box->m_flags = 0;
            box->m_stateTime = 4000;
            box->m_state = C::SUPPLY_HIDDEN;
            reset();
            box->Update(10);
            S_vector pz = crate->GetMatrix()(2);
            PrNormalize(pz, (pz.x * pz.x + pz.y * pz.y) + pz.z * pz.z);
            const S_vector next(pz.x * 10.0f * 0.03f + at.x, pz.y * 10.0f * 0.03f + at.y, pz.z * 10.0f * 0.03f + at.z);
            PR(r.boxStates, SaLogIs({"animweight crate 1 1", "animweight crate 0 0", "animactive crate 1", "tick crate 10",
                                     "setpos plane " + ItV(next), "update plane"}));
        }
        crate->m_localMatrix = savedLocal;
        crate->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        box->m_planeFlying = false;

        // The drop: the health and charges full, everything placed (2 m over: the drop point's height),
        // the reset animation first; on a server under a top at exactly 85 m 0.1 under it.
        box->m_dropPos = {3.0f, 2.0f, -4.0f, 1.0f};
        box->m_health = 10;
        box->m_charges = 1;
        crate->m_rot = S_quat(0.0f, 0.0f, 1.0f, 0.0f);
        crate->m_frameFlags = (crate->m_frameFlags & ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_LMATRIX_VALID)) | FRMFLAGS_ROT_DIRTY;
        box->m_anim = 1;
        box->m_state = C::SUPPLY_HIDDEN;
        reset();
        box->SetState(C::SUPPLY_FALLING);
        partsOn = true;
        for (I3D_frame *a : ammo)
            partsOn = partsOn && (!a || a->IsOn());
        PR(r.boxStates, box->m_health == 6000 && box->GetCharges() == box->m_capacity && box->m_anim == 5 && partsOn &&
                            SaSameQuat(crate->m_rot, SaNormalized(box->m_dropRot)) && logIs("drop", {"setanim crate #crateclosed 1 1 1", "tick crate 0", "setanim crate #crateopen 1 1 1", "tick crate 0", "stopanim crate 1", "setpos crate 3 87 -4", "update crate", "sector crate 3 87 -4", "on chute 1", "setpos chute 3 87 -4", "setdir chute 0 0 1 0", "update chute", "fadein chute 0 1", "on dummy 1", "setpos dummy 3 90 -4", "update dummy", "sector dummy 3 90 -4", "link chute letibedna 1", "link crate letibedna 1", "on Ammo00 1", "on Ammo01 1", "on Ammo02 1", "on Ammo03 1", "on Ammo04 1", "on Ammo05 1", "on Ammo06 1", "on Ammo08 1", "on Ammo09 1", "on Ammo10 1", "on Ammo11 1", "on Ammo12 1", "on Ammo13 1", "fadein crate 1000 1", "setanim dummy #cratefly 0 1 2"}));
        g_gameRole = GAME_ROLE_SERVER;
        g_scanMax[1] = 85.0f;
        box->m_state = C::SUPPLY_HIDDEN;
        reset();
        box->SetState(C::SUPPLY_FALLING);
        PR(r.boxStates, std::count(g_itLog.begin(), g_itLog.end(), "setpos crate " + ItV(S_vector(3.0f, (85.0f - 0.1f) + 2.0f, -4.0f))) == 1);
        g_scanMax[1] = 1000.0f;
        g_gameRole = GAME_ROLE_LOCAL;
        // Falling without a touch: the cratedummy's animation 1.5 times faster, the crate's, the move.
        box->m_flags = C::SUPPLY_SHOWN;
        reset();
        box->Update(10);
        PR(r.boxFall, logIs("fallstep", {"tick dummy 15", "animweight crate 1 1", "animweight crate 0 0", "animactive crate 1", "tick crate 10", "setpos dummy 3 89.8499985 -4", "update dummy", "sector dummy 3 89.8499985 -4"}) && g_volumeLog.size() == 1);
        // A ready box: only its animation.
        box->m_state = C::SUPPLY_READY;
        box->m_stateTime = -1;
        reset();
        box->Update(10);
        PR(r.boxStates, logIs("ready", {"animweight crate 1 1", "animweight crate 0 0", "animactive crate 1", "tick crate 10"}));
        // Landed: put away, the sectors, turned back; destroyed: put away, its (missing) animation.
        crate->m_rot = S_quat(0.0f, 0.0f, 1.0f, 0.0f);
        crate->m_frameFlags = (crate->m_frameFlags & ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_LMATRIX_VALID)) | FRMFLAGS_ROT_DIRTY;
        reset();
        box->SetState(C::SUPPLY_LANDED);
        PR(r.boxStates, SaSameQuat(crate->m_rot, SaNormalized(box->m_dropRot)) && logIs("landed", {"setanim crate #cratedrop 1 1 1", "on dummy 0", "link chute 0 1", "link crate 0 1", "sector chute 6 176.75 -8", "sector crate 6 176.75 -8"}));
        box->m_flags = C::SUPPLY_SHOWN;
        reset();
        box->SetState(C::SUPPLY_DESTROYED);
        PR(r.boxStates, box->m_anim == 4 && logIs("destroyed", {"on dummy 0", "link chute 0 1", "link crate 0 1", "setanim crate 0 0 0 1", "on chute 0", "fadeout crate 0 1"}));
        // Damage to 1 left: not destroyed.
        box->m_flags = 0;
        box->m_state = C::SUPPLY_READY;
        box->m_health = 101;
        box->Damage(100);
        PR(r.boxNet, box->m_health == 1 && box->m_state == C::SUPPLY_READY);
        // Enabled on a server: the message reliable.
        g_gameRole = GAME_ROLE_SERVER;
        reset();
        box->SetEnabled(true);
        PR(r.boxNet, g_streams.size() == 1 && g_streams[0]->reliable);
        g_gameRole = GAME_ROLE_LOCAL;

        // The state saved: all of the fields.
        box->m_planeFlying = true;
        box->m_blendTime = 33;
        box->m_animStage = 1;
        box->m_state = C::SUPPLY_READY;
        C::S_State st;
        st.charges = 77;
        box->GetState(st);
        PR(r.boxSave, st.planeFlying && st.blendTime == 33 && st.animStage == 1 && st.charges == box->GetCharges());
        // Written: the bits' counts.
        reset();
        box->WriteState(reinterpret_cast<C_BitStream *>(g_streams.emplace_back(new S_FakeStream{0, false, {}, false})));
        const int counts[16] = {6, 32, 32, 1, 32, 32, 4, 4, 32, -11, -11, -11, -11, -11, -11, 32};
        const std::vector<std::pair<uint32_t, int>> w = PrBits(0);
        bool countsOk = w.size() == 16;
        for (size_t i = 0; countsOk && i < 16; ++i)
            countsOk = w[i].second == counts[i];
        PR(r.boxNet, countsOk && w[3].first == 1u && w[4].first == 33u && w[5].first == 1u);
        box->m_planeFlying = false;

        // Applied: the morph reset, the parts as the charges were (then the new charges), the stage kept,
        // the reset animation first; each state's calls.
        const auto apply = [&](int state, int charges, const std::vector<std::string> &log) {
            C::S_State s;
            s.flags = C::SUPPLY_SHOWN;
            s.charges = charges;
            s.health = 500;
            s.animStage = 1;
            s.anim = 2;
            s.state = state;
            s.stateTime = 1234;
            s.pos = {1.0f, 2.0f, 3.0f, 1.0f};
            s.crate = {0.5f, -1.0f, 0.25f, 1.0f};
            s.animTime = 66;
            box->m_morphPlayer->stopped = false;
            box->m_blendTime = 0;
            reset();
            box->ApplyState(s);
            const bool drop = state == C::SUPPLY_FALLING;       // (the drop fills it up)
            return box->m_morphPlayer->stopped && box->GetCharges() == (drop ? box->m_capacity : charges) &&
                   box->m_health == (drop ? 6000 : 500) && box->m_stateTime == 1234 && SaLogIs(log);
        };
        box->m_charges = box->m_capacity;
        PR(r.boxNet, apply(C::SUPPLY_READY, 0, std::vector<std::string>({"animreset morph 16", "on dummy 0", "link chute 0 1", "on Ammo00 1", "on Ammo01 1", "on Ammo02 1", "on Ammo03 1", "on Ammo04 1", "on Ammo05 1", "on Ammo06 1", "on Ammo08 1", "on Ammo09 1", "on Ammo10 1", "on Ammo11 1", "on Ammo12 1", "on Ammo13 1", "setanim crate #crateclosed 1 1 1", "tick crate 0", "setanim crate #crateopen 1 1 1", "tick crate 0", "stopanim crate 1", "fadein crate 0 1", "setanim crate #crateopen 1 1 1", "animactive crate 1", "tick crate 10000", "setpos crate 1 2 3", "update crate", "sector crate 1 2 3"})) && box->m_animStage == 1 && box->m_blendTime == 0);
        PR(r.boxNet, apply(C::SUPPLY_CLOSING, 3, std::vector<std::string>({"animreset morph 16", "on dummy 0", "link chute 0 1", "on Ammo00 0", "on Ammo01 0", "on Ammo02 0", "on Ammo03 0", "on Ammo04 0", "on Ammo05 0", "on Ammo06 0", "on Ammo08 0", "on Ammo09 0", "on Ammo10 0", "on Ammo11 0", "on Ammo12 0", "on Ammo13 0", "setanim crate #crateclosed 1 1 1", "tick crate 0", "setanim crate #crateopen 1 1 1", "tick crate 0", "stopanim crate 1", "fadein crate 0 1", "setanim crate #crateclosed 1 1 1", "animactive crate 1", "tick crate 66", "setpos crate 1 2 3", "update crate", "sector crate 1 2 3"})));
        PR(r.boxNet, apply(C::SUPPLY_HIDDEN, 3, std::vector<std::string>({"animreset morph 16", "on dummy 0", "link chute 0 1", "on Ammo00 0", "on Ammo01 0", "on Ammo02 0", "on Ammo03 0", "on Ammo04 0", "on Ammo05 0", "on Ammo06 0", "on Ammo08 0", "on Ammo09 0", "on Ammo10 0", "on Ammo11 1", "on Ammo12 1", "on Ammo13 1", "setanim crate #crateclosed 1 1 1", "tick crate 0", "setanim crate #crateopen 1 1 1", "tick crate 0", "stopanim crate 1", "fadein crate 0 1", "setpos crate 1 2 3", "update crate", "sector crate 1 2 3"})));
        PR(r.boxNet, apply(C::SUPPLY_LANDED, 3, std::vector<std::string>({"animreset morph 16", "on dummy 0", "link chute 0 1", "on Ammo00 0", "on Ammo01 0", "on Ammo02 0", "on Ammo03 0", "on Ammo04 0", "on Ammo05 0", "on Ammo06 0", "on Ammo08 0", "on Ammo09 0", "on Ammo10 0", "on Ammo11 1", "on Ammo12 1", "on Ammo13 1", "setanim crate #crateclosed 1 1 1", "tick crate 0", "setanim crate #crateopen 1 1 1", "tick crate 0", "stopanim crate 1", "fadein crate 0 1", "setanim crate #cratedrop 1 1 1", "animactive crate 1", "tick crate 66", "setpos crate 1 2 3", "update crate", "sector crate 1 2 3"})));
        PR(r.boxNet, apply(C::SUPPLY_OPENING, 3, std::vector<std::string>({"animreset morph 16", "on dummy 0", "link chute 0 1", "on Ammo00 0", "on Ammo01 0", "on Ammo02 0", "on Ammo03 0", "on Ammo04 0", "on Ammo05 0", "on Ammo06 0", "on Ammo08 0", "on Ammo09 0", "on Ammo10 0", "on Ammo11 1", "on Ammo12 1", "on Ammo13 1", "setanim crate #crateclosed 1 1 1", "tick crate 0", "setanim crate #crateopen 1 1 1", "tick crate 0", "stopanim crate 1", "fadein crate 0 1", "setanim crate #crateopen 1 1 1", "animactive crate 1", "tick crate 66", "setpos crate 1 2 3", "update crate", "sector crate 1 2 3"})));
        PR(r.boxNet, apply(C::SUPPLY_FALLING, 3, std::vector<std::string>({"animreset morph 16", "on dummy 0", "link chute 0 1", "on Ammo00 0", "on Ammo01 0", "on Ammo02 0", "on Ammo03 0", "on Ammo04 0", "on Ammo05 0", "on Ammo06 0", "on Ammo08 0", "on Ammo09 0", "on Ammo10 0", "on Ammo11 1", "on Ammo12 1", "on Ammo13 1", "setanim crate #crateclosed 1 1 1", "tick crate 0", "setanim crate #crateopen 1 1 1", "tick crate 0", "stopanim crate 1", "setpos crate 3 87 -4", "update crate", "sector crate 3 87 -4", "on chute 1", "setpos chute 3 87 -4", "setdir chute 0 0 1 0", "update chute", "fadein chute 0 1", "on dummy 1", "setpos dummy 3 90 -4", "update dummy", "sector dummy 3 90 -4", "link chute letibedna 1", "link crate letibedna 1", "on Ammo00 1", "on Ammo01 1", "on Ammo02 1", "on Ammo03 1", "on Ammo04 1", "on Ammo05 1", "on Ammo06 1", "on Ammo08 1", "on Ammo09 1", "on Ammo10 1", "on Ammo11 1", "on Ammo12 1", "on Ammo13 1", "fadein crate 1000 1", "setanim dummy #cratefly 0 1 2", "setpos dummy 1 2 3", "update dummy", "sector dummy 1 2 3", "setpos crate 0.5 -1 0.25", "update crate"})));
        // (The box moved with the crate under the cratedummy.)
        PR(r.boxNet, box->m_boxMatrix.m[3][0] == crate->GetMatrix().m[3][0] && box->m_boxMatrix.m[3][1] == crate->GetMatrix().m[3][1]);
        PR(r.boxNet, apply(C::SUPPLY_DESTROYED, 3, std::vector<std::string>({"animreset morph 16", "on dummy 0", "link chute 0 1", "on Ammo00 1", "on Ammo01 1", "on Ammo02 1", "on Ammo03 1", "on Ammo04 1", "on Ammo05 1", "on Ammo06 1", "on Ammo08 1", "on Ammo09 1", "on Ammo10 1", "on Ammo11 1", "on Ammo12 1", "on Ammo13 1", "setanim crate #crateclosed 1 1 1", "tick crate 0", "setanim crate #crateopen 1 1 1", "tick crate 0", "stopanim crate 1", "fadein crate 0 1", "setpos crate 1 2 3", "update crate", "sector crate 2 4 6"})));

        // Loaded: the parts as the loaded charges; a short state chunk: zeros.
        box->m_flags = C::SUPPLY_SHOWN;
        box->m_state = C::SUPPLY_READY;
        box->m_charges = 0;
        box->UpdateAmmoParts();
        C::S_State full;
        box->GetState(full);
        full.charges = box->m_capacity;
        S_SaBytes in;
        in.Head(0x777, 10 + 6 + 0x60).Head(0x3039, 10).Val(0).Head(0x202, 6 + 0x60);
        in.Put(&full, sizeof full);
        S_SaReader rd(in);
        reset();
        box->LoadGame(rd.c);
        partsOn = true;
        for (I3D_frame *a : ammo)
            partsOn = partsOn && (!a || a->IsOn());
        PR(r.boxSave, partsOn && box->GetCharges() == box->m_capacity);
        S_SaBytes shortIn;
        shortIn.Head(0x777, 10 + 6 + 4).Head(0x3039, 10).Val(0).Head(0x202, 6 + 4).Val(uint32_t(C::SUPPLY_SHOWN));
        S_SaReader rd2(shortIn);
        box->m_health = 5;
        box->m_stateTime = 5;
        box->m_animStage = 1;
        box->m_planeFlying = true;
        box->LoadGame(rd2.c);
        // (Too short: nothing read, all zeros.)
        PR(r.boxSave, box->GetCharges() == 0 && box->m_health == 0 && box->m_stateTime == 0 && box->m_animStage == 0 &&
                          !box->m_planeFlying && box->m_state == C::SUPPLY_READY && box->m_flags == 0);
        // The fall onto ground 1.5 m up: each step the box swept (its move, up twice, the types), its volume
        // ignored and back; on a touch the six rays from the feet and their middles and the sphere from
        // the middle (half the first pair's distance) with their lines; landed 0.01 m over it.
        {
            box->m_flags = C::SUPPLY_SHOWN;
            box->m_state = C::SUPPLY_HIDDEN;
            box->SetState(C::SUPPLY_FALLING);
            g_flatGround = true;
            g_groundY = 1.5f;
            g_recordTests = true;
            bool sweepsOk = true, touched = false, touchOk = false, matrixOk = true;
            for (int step = 0; step < 2000 && box->m_state == C::SUPPLY_FALLING; ++step) {
                const int t = 50;
                const float fall = static_cast<float>(static_cast<uint32_t>(t)) * 0.005f;
                const S_matrix from = walls->GetMatrix();
                const int ignores = g_ignores, clears = g_ignoreClears, touches = g_boxTouches;
                reset();
                g_testRecords.clear();
                box->Update(t);
                const std::vector<S_TestRecord> rec = g_testRecords;
                sweepsOk = sweepsOk && g_ignores == ignores + 1 && g_ignoreClears == clears + 1 && g_ignoreArg == 0 && g_ignoreClearArg == 0 && !rec.empty() &&
                           rec[0].kind == 'b' && RbSame(rec[0].from, S_vector(0.0f, -fall, 0.0f)) && rec[0].radius == 0.0f &&
                           RbSame(rec[0].dir, S_vector(0, 1, 0)) && RbSame(rec[0].extra, S_vector(0, 1, 0)) &&
                           rec[0].types == 0x10002 && rec[0].flags == 0x30000;
                if (box->m_state == C::SUPPLY_FALLING && !g_volumeLog.empty())
                    matrixOk = matrixOk && box->m_boxMatrix.m[3][1] == crate->GetMatrix().m[3][1];
                if (g_boxTouches == touches || touched)
                    continue;
                touched = true;
                S_vector pts[7];
                for (int i = 0; i < 4; ++i)
                    pts[i] = S_vector(box->m_feet[i].x, box->m_feet[i].y, box->m_feet[i].z) * from;
                const S_vector m01((pts[1].x + pts[0].x) * 0.5f, (pts[1].y + pts[0].y) * 0.5f, (pts[1].z + pts[0].z) * 0.5f);
                const S_vector m23((pts[3].x + pts[2].x) * 0.5f, (pts[3].y + pts[2].y) * 0.5f, (pts[3].z + pts[2].z) * 0.5f);
                pts[4] = m01;
                pts[5] = m23;
                pts[6] = S_vector((m23.x + m01.x) * 0.5f, (m23.y + m01.y) * 0.5f, (m23.z + m01.z) * 0.5f);
                const float dx = pts[0].x - pts[1].x, dy = pts[0].y - pts[1].y, dz = pts[0].z - pts[1].z;
                const float radius = PrLength((dz * dz + dy * dy) + dx * dx) * 0.5f;
                touchOk = rec.size() == 8 && g_prLines.size() == 7;
                for (int i = 0; touchOk && i < 7; ++i) {
                    const S_TestRecord &q = rec[1 + i];
                    touchOk = RbSame(g_prLines[i].from, pts[i]) && RbSame(g_prLines[i].to, S_vector(pts[i].x, -fall + pts[i].y, pts[i].z)) &&
                              g_prLines[i].color == (i < 6 ? 0xFF0000u : 0xFF00u) && g_prLines[i].color2 == 0 &&
                              q.types == 0x10002 && q.flags == 0x30000;
                    if (i < 6)
                        touchOk = touchOk && q.kind == 'r' && RbSame(q.from, pts[i]) && RbSame(q.dir, S_vector(0.0f, -fall, 0.0f));
                    else
                        touchOk = touchOk && q.kind == 's' && q.radius == radius &&
                                  RbSame(q.from, S_vector(pts[6].x, radius + pts[6].y, pts[6].z)) &&
                                  RbSame(q.dir, S_vector(0.0f, -fall - radius, 0.0f));
                }
            }
            g_recordTests = false;
            float lowest = 1e9f;
            for (const S_vector4 &f : box->m_feet)
                lowest = std::min(lowest, (S_vector(f.x, f.y, f.z) * walls->GetMatrix()).y);
            PR(r.boxFall, box->m_state == C::SUPPLY_LANDED && sweepsOk && touched && touchOk && matrixOk &&
                              std::fabs(lowest - (1.5f + 0.01f)) < 1e-4f);
            g_flatGround = false;
            g_groundY = 0.0f;
        }

        // The stop: the reset animation, the plane released, the crate shown back, nothing kept.
        box->m_state = C::SUPPLY_READY;
        reset();
        box->StopMission(true);
        PR(r.boxStop, !box->m_flyingCrate && logIs("stop", {"animreset morph 16", "on dummy 0", "link chute 0 1", "link crate 0 1", "setanim crate #crateclosed 0 1 1", "tick crate 0", "setanim crate #crateopen 0 1 1", "tick crate 0", "stopanim crate 0", "animactive crate 0", "tick crate 1", "removeplayer morph 0", "release dummy", "release plane", "release chute", "animrelease #cratefly", "animrelease #crateopen", "animrelease #crateclosed", "animrelease #cratedrop", "fadein crate 0 1", "setpos crate 3 2 -4", "update crate", "sector crate 3 2 -4"}));
        // (A start: the frame linked back to its parent keeping its place; the animation reset with
        // nothing shown.)
        box->m_anim = 2;
        box->m_shownFlags = 0;
        reset();
        box->StartMission(true);
        PR(r.boxStart, box->m_anim == 5 && std::count(g_itLog.begin(), g_itLog.end(), "link crate " + (crate->m_parent ? ItName(crate->m_parent) : std::string("0")) + " 1") == 1);
        box->m_shownFlags = 1;
        g_itNames[box->m_crateDummy] = "dummy";
        g_itNames[box->m_parachute] = "chute";
        g_itNames[box->m_plane] = "plane";
    }
    g_cacheFill = nullptr;
    g_maAnimations.erase("ammobox");
    creator->Destroy(box);
}
