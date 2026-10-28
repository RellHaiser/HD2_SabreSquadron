// (Part of PropActorsTest: the door.)
{
    C_ActorCreator *creator = g_actorCreators[ACTOR_DOOR];
    PR(r.door, creator && std::string(creator->GetName()) == "Door");

    // The world: a sector (its portals) holding the door; the door's handles, "dhandle" twice (the
    // last child's subtree wins) and "D2Handle".
    I3D_frame *sector = MakeFrame(g_frameVtable, FRAME_SECTOR, false);
    g_itNames[sector] = "dsector";
    LinkUnder(sector, g_sector);
    const float doorX = rnd(-5.0f, 5.0f), doorZ = rnd(-5.0f, 5.0f);
    I3D_frame *frame = RbBoxFrame(sector, "door", 0.0f, 1.0f, 0.0f, 2.0f, -0.05f, 0.05f, S_vector(doorX, 0.0f, doorZ));
    frame->m_name = Stub(frame).name.c_str();
    I3D_frame *handleA = RbBoxFrame(frame, "DHandleA", 0.8f, 0.9f, 0.9f, 1.0f, 0.05f, 0.1f, S_vector(0, 0, 0));
    handleA->m_name = Stub(handleA).name.c_str();
    I3D_frame *knob = NewFrame(FRAME_DUMMY, "knob", frame);
    knob->m_name = Stub(knob).name.c_str();
    I3D_frame *handleC = NewFrame(FRAME_DUMMY, "Door.dhandleC", knob);
    handleC->m_name = Stub(handleC).name.c_str();
    handleC->m_nameOffset = 5;                          // (short name "dhandleC")
    I3D_frame *handle2 = NewFrame(FRAME_DUMMY, "xD2Handle", frame);
    handle2->m_name = Stub(handle2).name.c_str();

    auto *door = static_cast<C_Door *>(creator->Create());
    PR(r.door, door->GetType() == ACTOR_DOOR && door->m_axis.x == 0.0f && door->m_axis.y == -1.0f &&
                   door->m_axis.z == 0.0f && door->m_openAngle == 1.5707964f && door->m_speed == 1.5707964f &&
                   door->GetLockKey() == -1 && door->CanPickLock() && !door->IsLocked() && door->m_hasPortals &&
                   door->m_autoClose && door->m_unknown99 && door->m_textId == 0x1BF1 && !door->GetUnknown118() &&
                   door->GetState() == DOOR_CLOSED && !door->GetPortal() && !door->GetOtherPortal() &&
                   door->m_kind == DOOR_HINGED && door->m_openDelay == 0);
    for (int sound : door->m_sounds)
        PR(r.door, sound == -1);
    door->SetFrame(frame);
    door->SetNetId(40 + static_cast<int>(g_frames.size() % 7));
    // The materials: the part's, else the frame's (for another part), else 11.
    g_frameMaterials.clear();
    PR(r.door, door->GetMaterialId(handleA) == 11 && door->GetMaterialId(frame) == 11);
    g_frameMaterials[frame] = 6;
    PR(r.door, door->GetMaterialId(handleA) == 6 && door->GetMaterialId(frame) == 6);
    g_frameMaterials[handleA] = 4;
    PR(r.door, door->GetMaterialId(handleA) == 4);
    g_frameMaterials.erase(frame);
    PR(r.door, door->GetMaterialId(frame) == 11 && door->GetMaterialId(knob) == 11);
    g_frameMaterials.clear();

    // --- the properties: the fields, then the chunks (an unknown one entered: its chunks read) -----------
    const float openAngle = rnd(0.5f, 2.0f), speed = rnd(0.8f, 3.0f);
    const S_vector axis(0.0f, rnd(0.5f, 1.0f) > 0.75f ? 1.0f : -1.0f, 0.0f);
    int sounds[9];
    for (int i = 0; i < 9; ++i)
        sounds[i] = 100 + i * 3 + static_cast<int>(g_frames.size() % 3);
    const auto props = [&](int key, int motion, bool withName, bool portals) {
        S_SaBytes b;
        const std::string name = "door2";
        S_SaBytes body;
        body.Val(0).Val(motion).Val(openAngle).Val(speed).Val(axis).Val(key).Val(uint8_t(0)).Val(uint8_t(0x5A))
            .Val(uint32_t(0x12345678)).Val(uint8_t(1));
        body.Head(0x65, 6 + 36);
        for (int s : sounds)
            body.Val(s);
        body.Head(0x66, 7).Val(uint8_t(portals));
        if (withName) {
            body.Head(0x67, 6 + static_cast<uint32_t>(name.size()) + 1);
            body.Put(name.c_str(), name.size() + 1);
        }
        body.Head(0x68, 7).Val(uint8_t(0));
        body.Head(0x69, 7).Val(uint8_t(0));
        body.Head(0x6A, 18).Val(S_vector(0.6f, 0.0f, 0.8f));
        body.Head(0x6D, 11).Val(0x2A0B).Val(uint8_t(1));
        // (An unknown chunk holding the shot-through and pickable chunks: read through it; then the
        // reading stops at its end, so nothing after it is read.)
        body.Head(0x70, 6 + 7 + 7).Head(0x6B, 7).Val(uint8_t(1)).Head(0x6C, 7).Val(uint8_t(0));
        body.Head(0x6D, 11).Val(0x1111).Val(uint8_t(0));
        b.Head(0x777, 6 + static_cast<uint32_t>(body.b.size()));
        b.Put(body.b.data(), body.b.size());
        return b;
    };
    {
        S_SaReader rd(props(0, DOOR_CLOSED, true, true));
        reset();
        PR(r.doorProps, door->ReadProperties(rd.c));
        PR(r.doorProps, door->m_key == -1 && door->m_motion == DOOR_CLOSED && door->m_openAngle == openAngle &&
                            door->m_speed == speed && door->m_axis.y == axis.y && !door->m_locked &&
                            door->m_unknown23C && door->m_hasPortals && !door->m_autoClose && !door->m_unknown99 &&
                            door->m_facing.x == 0.6f && door->m_facing.z == 0.8f && door->m_facing.w == 0.0f &&
                            door->m_shootThrough && !door->m_pickable && door->m_textId == 0x2A0B &&
                            door->m_unknown118 && std::string(door->m_otherHalfName.c_str()) == "door2");
        for (int i = 0; i < 9; ++i)
            PR(r.doorProps, door->m_sounds[i] == sounds[i]);
        S_SaReader keyed(props(7, DOOR_OPEN, false, false));
        door->ReadProperties(keyed.c);
        PR(r.doorProps, door->m_key == 7 && door->m_motion == DOOR_OPEN && !door->m_hasPortals);
        door->m_hasPortals = true;
        door->m_key = -1;
        door->m_motion = DOOR_CLOSED;
    }
    // Written back: the fields (the two skipped ones as zeros), then the chunks; the facing from the
    // frame's world z, turned to where the quarter turn moves the bound's centre (the local centre: no
    // move, so up).
    {
        frame->m_localMatrix.m[2][0] = 0.0f;
        frame->m_localMatrix.m[2][1] = rnd(-0.6f, 0.6f);
        frame->m_localMatrix.m[2][2] = 1.0f;
        frame->m_frameFlags &= ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_ROT_DIRTY);
        frame->m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
        const S_quat rotBefore = frame->GetRot();
        S_vector dir = frame->GetMatrix()(2);
        PrNormalize(dir, (dir.y * dir.y + dir.z * dir.z) + dir.x * dir.x);
        if (dir.y < 0.0f)
            dir = S_vector(-dir.x, -dir.y, -dir.z);
        S_SaBytes e;
        e.Val(door->m_kind).Val(door->m_motion).Val(door->m_openAngle).Val(door->m_speed).Val(door->m_axis.x)
            .Val(door->m_axis.y).Val(door->m_axis.z).Val(door->m_key).Val(uint8_t(door->m_locked)).Val(uint8_t(0))
            .Val(0u).Val(uint8_t(door->m_unknown23C));
        e.Head(0x65, 42);
        for (int s : door->m_sounds)
            e.Val(s);
        e.Head(0x66, 7).Val(uint8_t(1));
        e.Head(0x67, 12);
        e.Put("door2", 6);
        e.Head(0x68, 7).Val(uint8_t(0)).Head(0x69, 7).Val(uint8_t(0)).Head(0x6A, 18).Val(dir);
        e.Head(0x6B, 7).Val(uint8_t(1)).Head(0x6C, 7).Val(uint8_t(0)).Head(0x6D, 11).Val(0x2A0B).Val(uint8_t(1));
        S_SaBytes all;
        all.Head(0x777, 6 + static_cast<uint32_t>(e.b.size()));
        all.Put(e.b.data(), e.b.size());
        reset();
        bool written = false;
        PR(r.doorProps, SaWritten([&](C_chunk &c) { return door->WriteProperties(c); }, &written) == all.b && written);
        PR(r.doorProps, SaSameQuat(frame->m_rot, SaNormalized(rotBefore)) && SaSameQuat(door->m_startRot, rotBefore) &&
                            door->m_facing.x == dir.x && door->m_facing.y == dir.y && door->m_facing.z == dir.z);
        frame->m_localMatrix.m[2][1] = 0.0f;
        frame->m_frameFlags &= ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_ROT_DIRTY);
        door->m_facing = {0.0f, 0.0f, 1.0f, 0.0f};
    }

    // --- the portals: the nearest within 10 m and its radius, of the sector's own or (none) incoming ones --
    I3D_portal *pFar = PrPortal("pfar", S_vector(0.25f, 0.5f + 9.0f, -0.75f), 20.0f);
    I3D_portal *pOut = PrPortal("pout", S_vector(0.25f + 1.0f, 0.5f, -0.75f), 0.5f);
    I3D_portal *pNear = PrPortal("pnear", S_vector(0.25f, 0.5f, -0.75f - 2.0f), 2.0f);
    I3D_portal *pTie = PrPortal("ptie", S_vector(0.25f + 2.0f, 0.5f, -0.75f), 2.0f);
    I3D_portal *pBack = PrPortal("pback", S_vector(0, 0, 0), 1.0f);
    PrSetOpposite(pNear, pBack);
    PrSetOpposite(pTie, nullptr);
    std::vector<I3D_portal *> own = {pFar, pOut, pNear, pTie};
    std::vector<I3D_portal *> incoming = {pFar, pOut};
    std::vector<I3D_portal *> two = {pFar, pNear}, none, outOnly = {pOut};
    PrSetPortals(sector, &own, &incoming);
    {
        reset();
        door->FindPortals();
        // (The pTie at the same distance comes later and wins.)
        PR(r.doorPortals, door->GetPortal() == pTie && door->GetOtherPortal() == nullptr);
        PrSetPortals(sector, &two, &incoming);
        door->FindPortals();
        PR(r.doorPortals, door->GetPortal() == pNear && door->GetOtherPortal() == pBack);
        // No own portals: the incoming ones (here only the pFar one, 9 m and within its 20 m).
        PrSetPortals(sector, &none, &incoming);
        door->FindPortals();
        PR(r.doorPortals, door->GetPortal() == pFar);
        // Nothing pNear enough: the last ones kept.
        PrSetPortals(sector, &outOnly, &none);
        door->FindPortals();
        PR(r.doorPortals, door->GetPortal() == pFar);
        // Not a portal door: none.
        door->m_hasPortals = false;
        door->FindPortals();
        PR(r.doorPortals, !door->GetPortal() && !door->GetOtherPortal());
        door->m_hasPortals = true;
        PrSetPortals(sector, &two, &incoming);
        // Opened / closed: both told.
        door->FindPortals();
        reset();
        door->SetPortalsClosed(true);
        door->SetPortalsClosed(false);
        PR(r.doorPortals, SaLogIs({"portal pnear 0", "portal pback 0", "portal pnear 1", "portal pback 1"}));
    }

    // --- the start (server): the other half, the name, the handles, the box, the sound; an open door
    // opens again ------------------------------------------------------------------------------------------
    I3D_frame *frame2 = RbBoxFrame(sector, "door2", 0.0f, 1.0f, 0.0f, 2.0f, -0.05f, 0.05f, S_vector(doorX + 1.0f, 0.0f, doorZ));
    frame2->m_name = Stub(frame2).name.c_str();
    auto *door2 = static_cast<C_Door *>(creator->Create());
    door2->SetFrame(frame2);
    door2->SetNetId(60);
    g_maSceneFrames["door2"] = frame2;
    static S_FrameData door2Data, doorData;
    static int doorScript;
    door2Data = {};
    door2Data.actor = door2;
    frame2->m_data = &door2Data;
    doorData = {};
    doorData.actor = door;
    doorData.script = reinterpret_cast<C_Script *>(&doorScript);
    frame->m_data = &doorData;
    {
        g_gameRole = GAME_ROLE_SERVER;
        door->m_motion = DOOR_OPEN;
        door->m_openDelay = 300;
        door->m_autoClose = true;
        door->m_shootThrough = true;
        reset();
        door->StartMission(true);
        PR(r.doorStart, door->m_started && door->m_otherHalf == door2 && door->m_handle == handleC &&
                            door->m_handle2 == handle2 && std::string(door->m_name.c_str()) == "text 10763" &&
                            door->m_movingSound == -1 && door->m_handleRising &&
                            door->m_initialMotion == DOOR_OPEN && !door->m_initialLocked);
        // (Reopened: the message, the script, the delay; the closing time cleared after it.)
        PR(r.doorStart, door->m_motion == DOOR_OPENING && door->m_delay == 300 && door->m_autoCloseTime == 0 &&
                            door->m_handleMoving && g_streams.size() == 1 && g_streams[0]->id == door->GetNetId() &&
                            PrBits(0) == std::vector<std::pair<uint32_t, int>>({{0, 3}}) && g_streams[0]->sent);
        const S_matrix &world = frame->GetMatrix();
        PR(r.doorStart, door->m_startPos.x == world.m[3][0] && door->m_startPos.y == world.m[3][1] &&
                            door->m_startPos.z == world.m[3][2] && SaSameQuat(door->m_startRot, frame->m_rot));
        I3D_bbox hb;
        const I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hb, frame);
        PR(r.doorStart, g_volumeLog.size() == 1 && g_volumeLog[0].op == 'A' && g_volumeLog[0].volume == &door->m_volume &&
                            door->m_volume.flags == (COLLISION_SHAPE_BOX | COLLISION_VOLUME_SHOOT_THROUGH) &&
                            door->m_volume.type == COLLISION_TYPE_ACTOR_DOOR && door->m_volume.actor == door &&
                            door->m_volume.frame == frame && RbSame(door->m_volume.bbox.min, box.min) &&
                            RbSame(door->m_volume.bbox.max, box.max) && door->m_volume.invMatrix == &door->m_invMatrix);
        PR(r.doorStart, door->m_sound.pos.x == world.m[3][0] && door->m_sound.pos.z == world.m[3][2] &&
                            door->m_sound.flags == SOUND_SILENT_TO_AI && door->m_sound.chapter == 1 &&
                            !door->m_sound.attachTo && !door->m_sound.source);
        // Started already: nothing again (a server's call).
        reset();
        door->StartMission(true);
        PR(r.doorStart, g_streams.empty() && g_volumeLog.empty() && g_activations.size() == 1);
        // A client's call on a portal door: the portals, the frame under the dynamic root, the start
        // placement again; a closed one shuts them.
        g_gameRole = GAME_ROLE_CLIENT;
        I3D_frame *root = NewFrame(FRAME_DUMMY, "droot", nullptr);
        *reinterpret_cast<I3D_frame **>(sceneBase + 0x200) = root;
        door->m_motion = DOOR_CLOSED;
        reset();
        door->StartMission(false);
        PR(r.doorStart, door->GetPortal() == pNear && frame->m_parent == root &&
                            SaLogIs({"link door droot 1", "update door", "portal pnear 0", "portal pback 0"}));
        // Not a portal door: linked pBack where it was (the actor's initial parent).
        door->m_hasPortals = false;
        reset();
        door->StartMission(false);
        PR(r.doorStart, g_itLog.size() >= 1 && g_itLog[0].compare(0, 10, "link door ") == 0);
        door->m_hasPortals = true;
        LinkUnder(frame, sector);
        frame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
    }
    g_gameRole = GAME_ROLE_SERVER;

    // --- the states: opening (locked: only the rattle unless forced), closing; the other half too ---------
    {
        door->m_motion = DOOR_CLOSED;
        door->m_otherHalf = door2;
        door2->m_started = true;
        door->m_started = true;
        door->m_locked = true;
        door->m_openDelay = 250;
        reset();
        door->SetState(DOOR_ACTION_OPEN, true, false, 0);
        PR(r.doorState, door->m_motion == DOOR_CLOSED && door2->m_motion == DOOR_OPENING && g_streams.size() == 2 &&
                            g_streams[0]->id == 60 && PrBits(0) == std::vector<std::pair<uint32_t, int>>({{0, 3}}) &&
                            PrBits(1) == std::vector<std::pair<uint32_t, int>>({{2, 3}}));
        reset();
        door->SetState(DOOR_ACTION_OPEN, false, false, 1);
        PR(r.doorState, door->m_motion == DOOR_OPENING && door->m_delay == 250 && door->m_autoCloseTime == 0 &&
                            door->m_handleMoving && PrBits(1) == std::vector<std::pair<uint32_t, int>>({{0, 3}}));
        // (A linked call does not go pBack to the other half.)
        reset();
        door->SetState(DOOR_ACTION_CLOSE, true, true, 0);
        PR(r.doorState, door->m_motion == DOOR_CLOSING && g_streams.size() == 1 &&
                            PrBits(0) == std::vector<std::pair<uint32_t, int>>({{1, 3}}));
        // Other actions: only the script.
        reset();
        door->m_otherHalf = nullptr;
        door->SetState(5, true, false, 0);
        PR(r.doorState, g_streams.empty() && door->m_motion == DOOR_CLOSING);
        // Not closing by itself (the door's flag off).
        door->m_autoClose = false;
        door->m_locked = false;
        door->SetState(DOOR_ACTION_OPEN, true, false, 0);
        PR(r.doorState, door->m_autoCloseTime == 0);
        door->m_autoClose = true;
        // The lock: a moving door does not lock; the wrong key rattles (5); the key locks (4), unlocks (3).
        door->m_key = 9;
        door->m_otherHalf = door2;
        door2->m_locked = false;
        reset();
        PR(r.doorState, !door->Lock(9) && g_streams.empty());
        door->m_motion = DOOR_CLOSED;
        PR(r.doorState, !door->Lock(8) && !door->m_locked && PrBits(0) == std::vector<std::pair<uint32_t, int>>({{5, 3}}));
        reset();
        PR(r.doorState, door->Lock(9) && door->m_locked && door2->m_locked &&
                            PrBits(0) == std::vector<std::pair<uint32_t, int>>({{4, 3}}));
        reset();
        PR(r.doorState, door->Lock(1) && g_streams.empty());
        PR(r.doorState, !door->Unlock(3) && door->m_locked && PrBits(0) == std::vector<std::pair<uint32_t, int>>({{5, 3}}));
        reset();
        PR(r.doorState, door->Unlock(9) && !door->m_locked && !door2->m_locked &&
                            PrBits(0) == std::vector<std::pair<uint32_t, int>>({{3, 3}}));
        reset();
        PR(r.doorState, door->Unlock(2) && g_streams.empty() && door->GetState() == DOOR_CLOSED);
        // Picking: only closed, locked and pickable.
        door->m_locked = door2->m_locked = true;
        door->m_pickable = false;
        reset();
        door->PickLock();
        PR(r.doorState, door->m_locked && g_streams.empty() && door->GetState() == DOOR_STATE_LOCKED);
        door->m_pickable = true;
        door->m_motion = DOOR_OPENING;
        door->PickLock();
        PR(r.doorState, door->m_locked && g_streams.empty());
        door->m_motion = DOOR_CLOSED;
        door->PickLock();
        PR(r.doorState, !door->m_locked && !door2->m_locked && PrBits(0) == std::vector<std::pair<uint32_t, int>>({{3, 3}}));
        door->m_locked = true;
        door->m_otherHalf = nullptr;
        door->PickLock();
        PR(r.doorState, !door->m_locked);
        door->m_key = -1;
        door->m_otherHalf = nullptr;
    }

    // --- the swing: the handles, the delay, the motion, the sounds, the box's rays (closing) ---------------
    // The oracle (from the asm): a hinged door turns from its start rotation about the axis by the
    // position; closing, its hierarchy box shrunk by 0.01 is tested along its 12 edges and the
    // diagonals 1-6, 2-5.
    I3D_frame *moving = MakeSound();
    g_itNames[moving] = "dsound";
    g_maSlotSounds[77] = reinterpret_cast<I3D_sound *>(moving);
    {
        const auto quatProduct = [](const S_quat &a, const S_quat &b) {
            const S_vector c(a.v.y * b.v.z - a.v.z * b.v.y, a.v.z * b.v.x - a.v.x * b.v.z, a.v.x * b.v.y - a.v.y * b.v.x);
            S_quat q;
            q.v = S_vector((b.v.x * a.s + a.v.x * b.s) + c.x, (b.v.y * a.s + a.v.y * b.s) + c.y,
                           (b.v.z * a.s + a.v.z * b.s) + c.z);
            q.s = a.s * b.s - ((a.v.z * b.v.z + a.v.y * b.v.y) + a.v.x * b.v.x);
            return q;
        };
        const auto corners = [&](float shrinkXZ, float shrinkY, S_vector out[8]) {
            I3D_bbox hb;
            I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hb, frame);
            box.max = S_vector(box.max.x - shrinkXZ, box.max.y - shrinkY, box.max.z - shrinkXZ);
            box.min = S_vector(box.min.x + shrinkXZ, box.min.y + shrinkY, box.min.z + shrinkXZ);
            for (int c = 0; c < 8; ++c)
                out[c] = S_vector(c & 4 ? box.min.x : box.max.x, c & 2 ? box.min.y : box.max.y,
                                  c & 1 ? box.max.z : box.min.z) * frame->GetMatrix();
        };
        const int edges[12][2] = {{2, 3}, {3, 7}, {7, 6}, {6, 2}, {0, 2}, {1, 3}, {5, 7}, {4, 6}, {0, 1}, {1, 5}, {5, 4}, {4, 0}};
        const auto raysAre = [&](const S_vector c[8], int count) {
            bool ok = static_cast<int>(g_waRays.size()) == count;
            for (int i = 0; ok && i < count; ++i) {
                const S_vector from = i < 12 ? c[edges[i][0]] : i == 12 ? c[1] : c[2];
                const S_vector to = i < 12 ? c[edges[i][1]] : i == 12 ? c[6] : c[5];
                ok = RbSame(g_waRays[i].from, from) && RbSame(g_waRays[i].dir, to - from) && g_waRays[i].types == 2 &&
                     g_waRays[i].flags == 0 && g_waRays[i].hitPos && g_waRays[i].hitNormal;
            }
            return ok;
        };
        door->m_kind = DOOR_HINGED;
        door->m_axis = {0.0f, 1.0f, 0.0f, 0.0f};
        door->m_openAngle = 1.2f;
        door->m_speed = 1.5f;
        door->m_sounds[2] = 31;                         // the moving sound
        door->m_sounds[1] = 32;                         // shut
        door->m_handle = handleA;
        door->m_handle2 = nullptr;
        door->m_startRot = frame->GetRot();
        door->m_opened = 0.0f;
        door->m_motion = DOOR_OPENING;
        door->m_delay = 120;
        door->m_handleMoving = true;
        door->m_handleRising = true;
        door->m_handleAngle = 0.0f;
        door->m_autoCloseTime = 0;
        door->m_movingSound = 77;
        door->m_movingSoundPaused = false;
        door->m_otherHalf = nullptr;
        // The handle rises 0.005 rad a ms to 45 degrees, falls pBack, stops; the delay holds the door.
        reset();
        door->Update(100);
        PR(r.doorMove, door->m_handleAngle == 0.5f && door->m_handleRising && door->m_delay == 20 &&
                           door->m_opened == 0.0f && g_waRays.empty() && g_volumeLog.empty());
        PR(r.doorMove, g_itLog.size() == 2 && g_itLog[0].compare(0, 20, "worlddir DHandleA 0 ") == 0 &&
                           g_itLog[0].substr(g_itLog[0].size() - 4) == " 0.5" && g_itLog[1] == "update DHandleA");
        reset();
        door->Update(100);                              // (past 45 degrees: clamped, falling)
        PR(r.doorMove, door->m_handleAngle == 0.785398185f && !door->m_handleRising && door->m_delay == 0 &&
                           door->m_opened == static_cast<float>(100u) * 0.001f * 1.5f);
        reset();
        door->Update(100);
        PR(r.doorMove, door->m_handleAngle == -0.5f * 1.0f + 0.785398185f);
        reset();
        door->Update(100);                              // (below 0: stops at 0)
        PR(r.doorMove, door->m_handleAngle == 0.0f && !door->m_handleMoving);
        // Opening on to the open angle: the moving sound off; no rays while opening.
        float expect = door->m_opened;
        for (int step = 0; step < 40 && door->m_motion == DOOR_OPENING; ++step) {
            const int t = static_cast<int>(rnd(10.0f, 90.0f));
            reset();
            door->Update(t);
            expect = static_cast<float>(static_cast<uint32_t>(t)) * 0.001f * 1.5f + expect;
            const bool done = expect > 1.2f;
            if (done)
                expect = 1.2f;
            S_quat turn;
            turn.Make(S_vector(0.0f, 1.0f, 0.0f), expect);
            // (Opened: the box is tested, the motion no longer opening.)
            PR(r.doorMove, door->m_opened == expect && door->m_motion == (done ? DOOR_OPEN : DOOR_OPENING) &&
                               g_waRays.size() == (done ? 14u : 0u) && SaSameQuat(frame->m_rot, SaNormalized(quatProduct(turn, door->m_startRot))) &&
                               g_volumeLog.size() == 1 &&
                               door->m_volume.type == (done ? COLLISION_TYPE_ACTOR_DOOR : COLLISION_TYPE_ACTOR_DOOR | 2) &&
                               g_soundLog == (done ? std::vector<std::pair<const I3D_frame *, char>>{{moving, 'o'}}
                                                   : std::vector<std::pair<const I3D_frame *, char>>{}));
        }
        PR(r.doorMove, door->m_motion == DOOR_OPEN);
        // Open: nothing moves.
        reset();
        door->Update(50);
        PR(r.doorMove, g_itLog.empty() && g_volumeLog.empty());
        // Closing: the rays, a ray that meets something blocks (the turn and the position pBack, the
        // sound paused at its place, 0.2 s), then it goes on (the sound resumed where it was).
        g_sounds[moving].position = 345;
        door->m_motion = DOOR_CLOSING;
        reset();
        door->Update(40);
        expect = expect - static_cast<float>(40u) * 0.001f * 1.5f;
        S_vector c[8];
        corners(0.01f, 0.01f, c);
        PR(r.doorMove, door->m_opened == expect && raysAre(c, 14) && door->m_volume.type == COLLISION_TYPE_ACTOR_DOOR);
        const S_quat before = frame->m_rot;
        g_waBlockAt = 5;
        reset();
        door->Update(40);
        PR(r.doorMove, door->m_opened == expect && door->m_delay == 200 && door->m_movingSoundPaused &&
                           door->m_movingSoundPos == 345 && g_waRays.size() == 6 && SaSameQuat(frame->m_rot, SaNormalized(before)) &&
                           g_soundLog == std::vector<std::pair<const I3D_frame *, char>>({{moving, 'o'}}) &&
                           g_volumeLog.size() == 1);
        g_waBlockAt = 12;                               // (the first diagonal blocks too)
        reset();
        door->Update(250);
        PR(r.doorMove, door->m_delay == 200 && g_waRays.size() == 13);
        g_waBlockAt = 13;
        door->m_delay = 0;
        reset();
        door->Update(10);
        PR(r.doorMove, door->m_delay == 200 && g_waRays.size() == 14);
        g_waBlockAt = -1;
        door->m_delay = 0;
        reset();
        door->Update(30);
        expect = expect - static_cast<float>(30u) * 0.001f * 1.5f;
        PR(r.doorMove, !door->m_movingSoundPaused && door->m_opened == expect && g_sounds[moving].playAt == 345 &&
                           g_soundLog == std::vector<std::pair<const I3D_frame *, char>>({{moving, 'P'}, {moving, 'O'}}));
        // Shut: the sound off, the shut sound, the portals closed (the other half shut too).
        door->m_otherHalf = door2;
        door2->m_motion = DOOR_OPEN;
        for (int step = 0; step < 60 && door->m_motion == DOOR_CLOSING; ++step) {
            reset();
            door->Update(static_cast<int>(rnd(20.0f, 120.0f)));
        }
        PR(r.doorMove, door->m_motion == DOOR_CLOSED && door->m_opened == 0.0f && g_soundRequests.size() == 1 &&
                           g_soundRequests[0].group == 32 && !std::count(g_itLog.begin(), g_itLog.end(), "portal pnear 0"));
        // (With the other half shut: the portals shut.)
        door2->m_motion = DOOR_CLOSED;
        door->m_motion = DOOR_CLOSING;
        door->m_opened = 0.001f;
        reset();
        door->Update(30);
        PR(r.doorMove, std::count(g_itLog.begin(), g_itLog.end(), "portal pnear 0") == 1 && g_soundRequests.size() == 1);
        // A door turning about x is never tested; one about -x neither.
        door->m_axis = {1.0f, 0.0f, 0.0f, 0.0f};
        door->m_motion = DOOR_CLOSING;
        door->m_opened = 0.5f;
        reset();
        door->Update(10);
        PR(r.doorMove, g_waRays.empty());
        door->m_axis = {-1.0f, 0.0f, 0.0f, 0.0f};
        reset();
        door->Update(10);
        PR(r.doorMove, g_waRays.empty() && door->m_volume.type == COLLISION_TYPE_ACTOR_DOOR);
        door->m_motion = DOOR_OPENING;
        reset();
        door->Update(10);
        PR(r.doorMove, door->m_volume.type == COLLISION_TYPE_ACTOR_DOOR);
        door->m_axis = {1.0f, 0.0f, 0.5f, 0.0f};
        reset();
        door->Update(10);
        PR(r.doorMove, door->m_volume.type == (COLLISION_TYPE_ACTOR_DOOR | 2));
        door->m_axis = {0.0f, 1.0f, 0.0f, 0.0f};
        door->m_otherHalf = nullptr;
        // Closing by itself (single player): waits while the player is within 10 m or the door was seen
        // in the last 10 frames (2 s more), else closes.
        g_gameRole = GAME_ROLE_LOCAL;
        door->m_motion = DOOR_OPEN;
        door->m_autoCloseTime = 100;
        C_Human *player = new C_Human();
        I3D_frame *playerFrame = NewFrame(FRAME_DUMMY, "dplayer", nullptr);
        player->m_frame = playerFrame;
        g_prPlayer = player;
        door->SetPosition(S_vector(doorX, 0.0f, doorZ));
        playerFrame->m_localMatrix(3) = S_vector(doorX + 9.9f, 0.0f, doorZ);
        playerFrame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        *reinterpret_cast<int *>(sceneBase + 0x11C) = 500;
        *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(frame) + 0x11C) = 400;
        reset();
        door->Update(100);
        PR(r.doorMove, door->m_autoCloseTime == 2000 && door->m_motion == DOOR_OPEN);
        playerFrame->m_localMatrix(3) = S_vector(doorX + 10.0f, 0.0f, doorZ);
        playerFrame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(frame) + 0x11C) = 491;
        door->m_autoCloseTime = 30;
        door->Update(30);
        PR(r.doorMove, door->m_autoCloseTime == 2000 && door->m_motion == DOOR_OPEN);
        *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(frame) + 0x11C) = 490;
        door->m_autoCloseTime = 30;
        reset();
        door->Update(40);
        PR(r.doorMove, door->m_autoCloseTime == 0 && door->m_motion == DOOR_CLOSING && PrBits(0) == std::vector<std::pair<uint32_t, int>>({{1, 3}}));
        // No player: closes; a server only closes; a closing door's time just runs out.
        g_prPlayer = nullptr;
        door->m_motion = DOOR_OPEN;
        door->m_autoCloseTime = 10;
        door->Update(10);
        PR(r.doorMove, door->m_motion == DOOR_CLOSING);
        g_gameRole = GAME_ROLE_SERVER;
        g_prPlayer = player;
        playerFrame->m_localMatrix(3) = S_vector(doorX, 0.0f, doorZ);
        playerFrame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        door->m_motion = DOOR_OPEN;
        door->m_autoCloseTime = 10;
        door->Update(10);
        PR(r.doorMove, door->m_motion == DOOR_CLOSING);
        door->m_autoCloseTime = 10;
        door->Update(30);
        PR(r.doorMove, door->m_autoCloseTime == 0);
        g_prPlayer = nullptr;
        // Tick: on a pure client nothing; else the same.
        door->m_motion = DOOR_CLOSING;
        door->m_opened = 0.6f;
        g_gameRole = GAME_ROLE_CLIENT;
        door->Tick(100);
        PR(r.doorMove, door->m_opened == 0.6f);
        g_gameRole = GAME_ROLE_SERVER;
        door->Tick(100);
        PR(r.doorMove, door->m_opened == 0.6f - static_cast<float>(100u) * 0.001f * 1.5f);
        door->m_opened = 0.0f;
        door->m_motion = DOOR_CLOSED;
        door->m_movingSound = -1;
    }

    // --- the slide: along the frame's rotation turned three quarters about the axis, from the start ------
    {
        door->m_kind = DOOR_SLIDING;
        door->m_axis = {0.0f, 1.0f, 0.0f, 0.0f};
        door->m_openAngle = 0.9f;
        door->m_speed = 0.7f;
        door->m_startPos = {doorX, 0.0f, doorZ, 1.0f};
        door->m_motion = DOOR_OPENING;
        door->m_opened = 0.0f;
        door->m_delay = 0;
        door->m_handleMoving = false;
        PR(r.doorSlide, door->GetSide(S_vector4{0, 0, 0, 1}) == 2);
        float expect = 0.0f;
        for (int step = 0; step < 100 && door->m_motion == DOOR_OPENING; ++step) {
            const int t = static_cast<int>(rnd(20.0f, 120.0f));
            reset();
            door->Update(t);
            expect = static_cast<float>(static_cast<uint32_t>(t)) * 0.001f * 0.7f + expect;
            const bool done = expect > 0.9f;
            if (done)
                expect = 0.9f;
            S_quat turn;
            turn.Make(S_vector(0.0f, 1.0f, 0.0f), 4.71238899f);
            S_quat rot = frame->m_rot;
            const S_vector c(rot.v.y * turn.v.z - rot.v.z * turn.v.y, rot.v.z * turn.v.x - rot.v.x * turn.v.z,
                             rot.v.x * turn.v.y - rot.v.y * turn.v.x);
            S_quat q;
            q.v = S_vector((turn.v.x * rot.s + rot.v.x * turn.s) + c.x, (turn.v.y * rot.s + rot.v.y * turn.s) + c.y,
                           (turn.v.z * rot.s + rot.v.z * turn.s) + c.z);
            q.s = rot.s * turn.s - ((rot.v.z * turn.v.z + rot.v.y * turn.v.y) + rot.v.x * turn.v.x);
            const S_vector d = q.GetDir();
            const S_vector pos(d.x * expect + doorX, d.y * expect + 0.0f, d.z * expect + doorZ);
            PR(r.doorSlide, door->m_opened == expect && g_itLog.size() >= 1 && g_itLog[0] == "setpos door " + ItV(pos) &&
                                g_waRays.size() == (done ? 14u : 0u) && door->m_volume.type == COLLISION_TYPE_ACTOR_DOOR);
        }
        PR(r.doorSlide, door->m_motion == DOOR_OPEN);
        // Closing: tested (the box shrunk 0.001 in x and z, 0.01 in y); blocked: the position pBack, but
        // the frame stays where it slid.
        door->m_motion = DOOR_CLOSING;
        g_waBlockAt = 13;
        reset();
        door->Update(50);
        I3D_bbox hb;
        I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hb, frame);
        box.max = S_vector(box.max.x - 0.001f, box.max.y - 0.01f, box.max.z - 0.001f);
        box.min = S_vector(box.min.x + 0.001f, box.min.y + 0.01f, box.min.z + 0.001f);
        S_vector c[8];
        for (int k = 0; k < 8; ++k)
            c[k] = S_vector(k & 4 ? box.min.x : box.max.x, k & 2 ? box.min.y : box.max.y, k & 1 ? box.max.z : box.min.z) *
                   frame->GetMatrix();
        PR(r.doorSlide, door->m_opened == 0.9f && door->m_delay == 200 && g_waRays.size() == 14 &&
                            RbSame(g_waRays[13].from, c[2]) && RbSame(g_waRays[13].dir, c[5] - c[2]) &&
                            RbSame(g_waRays[0].from, c[2]) && RbSame(g_waRays[0].dir, c[3] - c[2]) &&
                            RbSame(g_waRays[12].from, c[1]) && RbSame(g_waRays[12].dir, c[6] - c[1]) &&
                            g_itLog[0] != "setpos door " + ItV(S_vector(doorX, 0.0f, doorZ)));
        g_waBlockAt = -1;
        door->m_delay = 0;
        door->m_motion = DOOR_CLOSED;
        door->m_opened = 0.0f;
        door->m_kind = DOOR_HINGED;
        frame->m_localMatrix(3) = S_vector(doorX, 0.0f, doorZ);
        frame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
    }
    // The side: in front of the facing 1, behind 0 (a point on the plane: 1).
    {
        door->m_facing = {0.0f, 0.0f, 1.0f, 0.0f};
        PR(r.doorSlide, door->GetSide(S_vector4{doorX, 1.0f, doorZ + 0.5f, 1}) == 1 &&
                            door->GetSide(S_vector4{doorX, 1.0f, doorZ - 0.5f, 1}) == 0 &&
                            door->GetSide(S_vector4{doorX + 3.0f, 1.0f, doorZ, 1}) == 1);
        door->m_facing = {0.6f, 0.0f, 0.8f, 0.0f};
        PR(r.doorSlide, door->GetSide(S_vector4{doorX + 0.5f, 0.0f, doorZ - 0.3f, 1}) == 1 &&
                            door->GetSide(S_vector4{doorX - 0.5f, 0.0f, doorZ + 0.3f, 1}) == 0 &&
                            door->GetSide(S_vector4{doorX, 7.0f, doorZ, 1}) == 1);
    }

    // --- the messages: their sounds and states; a joining client's state ---------------------------------
    {
        const auto message = [&](uint32_t kind) {
            g_readBits = {{kind, 3}};
            g_readPos = 0;
            door->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        };
        for (int i = 0; i < 9; ++i)
            door->m_sounds[i] = 200 + i;
        g_soundSlot = 78;
        g_maSlotSounds[78] = reinterpret_cast<I3D_sound *>(moving);
        door->m_movingSound = -1;
        door->m_motion = DOOR_CLOSED;
        door->m_handleMoving = door->m_handleRising = false;
        reset();
        message(0);
        PR(r.doorNet, g_soundRequests.size() == 2 && g_soundRequests[0].group == 200 && g_soundRequests[1].group == 202 &&
                          !(g_soundRequests[0].flags & SOUND_KEEP_SLOT) && (g_soundRequests[1].flags & SOUND_KEEP_SLOT) &&
                          !(door->m_sound.flags & SOUND_KEEP_SLOT) && door->m_movingSound == 78 &&
                          door->m_motion == DOOR_OPENING && door->m_handleMoving && door->m_handleRising &&
                          SaLogIs({"portal pnear 1", "portal pback 1"}));
        // Again: the sound on, not played again.
        door->m_movingSoundPaused = true;
        reset();
        message(1);
        PR(r.doorNet, g_soundRequests.empty() && door->m_motion == DOOR_CLOSING && !door->m_movingSoundPaused &&
                          g_soundLog == std::vector<std::pair<const I3D_frame *, char>>({{moving, 'O'}}));
        // Without a moving sound: none kept.
        door->m_sounds[2] = -1;
        door->m_sounds[0] = -1;
        reset();
        message(0);
        PR(r.doorNet, g_soundRequests.empty() && door->m_movingSound == -1);
        message(1);
        PR(r.doorNet, g_soundRequests.empty() && door->m_movingSound == -1 && door->m_motion == DOOR_CLOSING);
        door->m_sounds[2] = 202;
        reset();
        message(1);
        PR(r.doorNet, g_soundRequests.size() == 1 && g_soundRequests[0].group == 202 && door->m_movingSound == 78);
        // The lock's: 2 locked, 3 unlocked (and the other half), 4 locked (only this one), 5 the wrong key.
        door->m_otherHalf = door2;
        door->m_locked = door2->m_locked = true;
        reset();
        message(2);
        message(3);
        PR(r.doorNet, !door->m_locked && !door2->m_locked);
        message(4);
        message(5);
        message(6);
        PR(r.doorNet, door->m_locked && !door2->m_locked && g_soundRequests.size() == 4 && g_soundRequests[0].group == 208 &&
                          g_soundRequests[1].group == 204 && g_soundRequests[2].group == 203 && g_soundRequests[3].group == 205);
        door->m_sounds[8] = door->m_sounds[4] = door->m_sounds[3] = door->m_sounds[5] = -1;
        reset();
        message(2);
        message(3);
        message(4);
        message(5);
        PR(r.doorNet, g_soundRequests.empty());
        // The state: locked and open / opening -> 0, closed / closing -> 1.
        const int motions[4] = {DOOR_CLOSED, DOOR_OPEN, DOOR_OPENING, DOOR_CLOSING};
        for (int m : motions) {
            door->m_motion = m;
            door->m_locked = m == DOOR_OPEN;
            reset();
            door->WriteState(reinterpret_cast<C_BitStream *>(g_streams.emplace_back(new S_FakeStream{0, false, {}, false})));
            const uint32_t kind = m == DOOR_OPEN || m == DOOR_OPENING ? 0 : 1;
            PR(r.doorNet, PrBits(0) == std::vector<std::pair<uint32_t, int>>({{m == DOOR_OPEN ? 1u : 0u, 1}, {kind, 3}}));
        }
        door->m_motion = 7;
        reset();
        door->WriteState(reinterpret_cast<C_BitStream *>(g_streams.emplace_back(new S_FakeStream{0, false, {}, false})));
        PR(r.doorNet, PrBits(0) == std::vector<std::pair<uint32_t, int>>({{0u, 1}}));
        g_readBits = {{1, 1}, {0, 3}};
        g_readPos = 0;
        door->m_handleMoving = door->m_handleRising = false;
        reset();
        door->ReadState(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.doorNet, door->m_locked && door->m_motion == DOOR_OPENING && door->m_handleMoving && door->m_handleRising &&
                          SaLogIs({"portal pnear 1", "portal pback 1"}) && g_soundRequests.empty());
        g_readBits = {{0, 1}, {1, 3}};
        g_readPos = 0;
        reset();
        door->ReadState(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.doorNet, !door->m_locked && door->m_motion == DOOR_CLOSING && g_itLog.empty());
        g_readBits = {{0, 1}, {5, 3}};
        g_readPos = 0;
        door->m_motion = DOOR_OPEN;
        door->ReadState(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.doorNet, door->m_motion == DOOR_OPEN);
        door->m_otherHalf = nullptr;
        door->m_locked = false;
        door->m_movingSound = -1;
    }

    // --- the saved game: chunk 3000 (it reports false); read pBack (an unknown chunk is entered) -----------
    {
        frame->m_localMatrix(3) = S_vector(doorX + 0.25f, 0.5f, doorZ - 0.5f);
        frame->m_rot = S_quat(0.8f, 0.0f, 0.6f, 0.0f);
        frame->m_frameFlags = (frame->m_frameFlags & ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_LMATRIX_VALID)) | FRMFLAGS_ROT_DIRTY;
        door->m_opened = 0.375f;
        door->m_locked = true;
        door->m_motion = DOOR_OPENING;
        door->m_delay = 77;
        const S_matrix &world = frame->GetMatrix();
        S_SaBytes e;
        e.Head(0x777, 6 + 6 + 41).Head(3000, 6 + 41).Val(0.375f).Val(uint8_t(1)).Val(DOOR_OPENING).Val(77)
            .Val(world.m[3][0]).Val(world.m[3][1]).Val(world.m[3][2]).Val(0.8f).Val(0.0f).Val(0.6f).Val(0.0f);
        bool saved = true;
        PR(r.doorSave, SaWritten([&](C_chunk &c) { return door->SaveGame(c); }, &saved) == e.b && !saved);
        door->m_opened = 0.0f;
        door->m_locked = false;
        door->m_motion = DOOR_CLOSED;
        door->m_delay = 0;
        S_SaBytes in;
        in.Head(0x777, 6 + 6 + 6 + 41).Head(0x55, 6 + 6 + 41).Head(3000, 6 + 41).Val(0.25f).Val(uint8_t(1))
            .Val(DOOR_CLOSING).Val(66).Val(S_vector(1.5f, 2.5f, 3.5f)).Val(0.0f).Val(1.0f).Val(0.0f).Val(0.0f);
        S_SaReader rd(in);
        reset();
        PR(r.doorSave, door->LoadGame(rd.c));
        PR(r.doorSave, door->m_opened == 0.25f && door->m_locked && door->m_motion == DOOR_CLOSING && door->m_delay == 66 &&
                           RbSame(frame->m_localMatrix(3), S_vector(1.5f, 2.5f, 3.5f)) && frame->m_rot.v.x == 1.0f &&
                           g_volumeLog.size() == 1 && g_volumeLog[0].op == 'M' &&
                           SaLogIs({"setpos door 1.5 2.5 3.5", "update door", "bound door", "portal pnear 1", "portal pback 1"}));
        // Without the chunk: where it is; closed: the portals shut.
        door->m_motion = DOOR_CLOSED;
        S_SaBytes empty;
        empty.Head(0x777, 6 + 7).Head(0x10, 7).Val(uint8_t(0));
        S_SaReader rd2(empty);
        reset();
        PR(r.doorSave, door->LoadGame(rd2.c) && SaLogIs({"setpos door 1.5 2.5 3.5", "update door", "bound door", "portal pnear 0", "portal pback 0"}));
    }

    // --- stop and restart: the portals opened, the box out, the motion pBack; restarted from the start ----
    {
        door->m_otherHalf = door2;
        door->m_motion = DOOR_OPENING;
        door->m_initialMotion = DOOR_OPEN;
        door->m_unknown94 = 5;
        reset();
        door->StopMission(true);
        PR(r.doorStop, !door->m_started && !door->m_otherHalf && door->m_motion == DOOR_OPEN && door->m_unknown94 == 0 &&
                           g_volumeLog.size() == 1 && g_volumeLog[0].op == 'R' &&
                           SaLogIs({"portal pnear 1", "portal pback 1"}) && g_deactivations == std::vector<bool>{true});
        door->m_startPos = {doorX, 0.0f, doorZ, 1.0f};
        door->m_startRot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
        door->m_initialLocked = true;
        door->m_initialMotion = DOOR_CLOSED;
        door->m_movingSound = 78;
        door->m_movingSoundPaused = true;
        door->m_movingSoundPos = 9;
        door->m_handleAngle = 0.3f;
        door->m_handleMoving = true;
        door->m_handleRising = false;
        door->m_opened = 0.4f;
        door->m_delay = 30;
        reset();
        door->RestartMission(true);
        PR(r.doorStop, door->m_locked && door->m_motion == DOOR_CLOSED && door->m_movingSound == -1 &&
                           !door->m_movingSoundPaused && door->m_movingSoundPos == 0 && door->m_handleAngle == 0.0f &&
                           !door->m_handleMoving && door->m_handleRising && door->m_opened == 0.0f && door->m_delay == 0 &&
                           g_streams.empty() && g_volumeLog.size() == 1 && g_volumeLog[0].op == 'M' &&
                           SaLogIs({"setpos door " + ItV(S_vector(doorX, 0.0f, doorZ)), "update door", "bound door", "portal pnear 0",
                                    "portal pback 0"}));
        // An open door opens again on the server.
        door->m_initialMotion = DOOR_OPEN;
        door->m_initialLocked = false;
        door->m_otherHalf = nullptr;
        reset();
        door->RestartMission(true);
        PR(r.doorStop, door->m_motion == DOOR_OPENING && PrBits(0) == std::vector<std::pair<uint32_t, int>>({{0, 3}}));
        g_gameRole = GAME_ROLE_CLIENT;
        reset();
        door->RestartMission(false);
        PR(r.doorStop, door->m_motion == DOOR_OPEN && g_streams.empty());
        g_gameRole = GAME_ROLE_SERVER;
    }

    // --- the rest: the values, a new door, the copy, the edge cases of the swing, the portals, the
    // start's fields, the facing's normalising, the chunks after others --------------------------------------
    {
        // (The scripts, saved games and messages use the numbers.)
        PR(r.door, DOOR_ACTION_OPEN == 2 && DOOR_ACTION_CLOSE == 3 && DOOR_CLOSED == 0 && DOOR_OPEN == 1 &&
                       DOOR_OPENING == 2 && DOOR_CLOSING == 3 && DOOR_STATE_LOCKED == 4 && DOOR_HINGED == 0 &&
                       DOOR_SLIDING == 1);
        auto *door3 = static_cast<C_Door *>(creator->Create());
        PR(r.door, door3->m_autoCloseTime == 0 && door3->m_movingSound == -1 && door3->m_movingSoundPos == 0 &&
                       door3->m_handleRising && !door3->m_handleMoving && door3->m_handleAngle == 0.0f &&
                       door3->m_opened == 0.0f && door3->m_delay == 0 && !door3->m_started && !door3->m_movingSoundPaused);
        // The copy: the properties and sounds (not how far open), its portals found.
        I3D_frame *frame3 = RbBoxFrame(sector, "door3", 0.0f, 1.0f, 0.0f, 2.0f, -0.05f, 0.05f, S_vector(doorX, 0.0f, doorZ + 1.0f));
        frame3->m_name = Stub(frame3).name.c_str();
        door3->SetFrame(frame3);
        PrSetPortals(sector, &two, &incoming);
        door->m_kind = DOOR_SLIDING;
        door->m_motion = DOOR_OPEN;
        door->m_openAngle = 0.7f;
        door->m_speed = 2.5f;
        door->m_key = 12;
        door->m_locked = true;
        door->m_pickable = false;
        door->m_axis = {1.0f, 0.0f, 0.0f, 0.0f};
        door->m_openDelay = 77;
        door->m_unknown23C = true;
        door->m_hasPortals = true;
        door->m_opened = 0.9f;
        for (int i = 0; i < 9; ++i)
            door->m_sounds[i] = 300 + i;
        door3->m_hasPortals = false;
        door3->CopyFrom(door);
        PR(r.door, door3->m_kind == DOOR_SLIDING && door3->m_motion == DOOR_OPEN && door3->m_openAngle == 0.7f &&
                       door3->m_speed == 2.5f && door3->m_key == 12 && door3->m_locked && !door3->m_pickable &&
                       door3->m_axis.x == 1.0f && door3->m_axis.y == 0.0f && door3->m_openDelay == 77 &&
                       door3->m_unknown23C && door3->m_hasPortals && door3->GetPortal() == pNear &&
                       door3->GetOtherPortal() == pBack && door3->m_opened == 0.0f);
        for (int i = 0; i < 9; ++i)
            PR(r.door, door3->m_sounds[i] == 300 + i);
        creator->Destroy(door3);
        door->m_kind = DOOR_HINGED;
        door->m_motion = DOOR_CLOSED;
        door->m_key = -1;
        door->m_locked = false;
        door->m_pickable = true;
        door->m_axis = {0.0f, 1.0f, 0.0f, 0.0f};
        door->m_opened = 0.0f;
        door->m_openAngle = 1.2f;
        door->m_speed = 1.5f;
        for (int i = 0; i < 9; ++i)
            door->m_sounds[i] = -1;

        // The portals: a farther one later loses; none beyond 10 m; a length below 1e-8 is 0, 1 is 1.
        I3D_portal *pTen = PrPortal("pten", S_vector(0.25f, 0.5f + 10.5f, -0.75f), 20.0f);
        I3D_portal *pAt = PrPortal("pat", S_vector(0.25f, 0.5f, -0.75f), 0.3f);
        I3D_portal *pOne = PrPortal("pone", S_vector(0.25f, 0.5f + 1.0f, -0.75f), 1.2f);
        std::vector<I3D_portal *> nearFirst = {pNear, pFar}, ten = {pTen}, at = {pAt}, one = {pOne};
        PrSetPortals(sector, &nearFirst, &none);
        door->FindPortals();
        PR(r.doorPortals, door->GetPortal() == pNear);
        door->m_portal = nullptr;
        PrSetPortals(sector, &ten, &none);
        door->FindPortals();
        PR(r.doorPortals, door->GetPortal() == nullptr);
        PrSetPortals(sector, &at, &none);
        door->FindPortals();
        PR(r.doorPortals, door->GetPortal() == pAt && door->GetOtherPortal() == nullptr);
        door->m_portal = nullptr;
        PrSetPortals(sector, &one, &none);
        door->FindPortals();
        PR(r.doorPortals, door->GetPortal() == pOne);
        // (Not a portal door: both cleared, nothing searched.)
        PrSetPortals(sector, &two, &incoming);
        door->FindPortals();
        door->m_hasPortals = false;
        door->FindPortals();
        PR(r.doorPortals, !door->GetPortal() && !door->GetOtherPortal());
        door->m_hasPortals = true;
        door->FindPortals();

        // Opening: the closing time 15 s after the delay, the portals opened, the message reliable; the
        // other half's call is linked (it does not call back).
        door->m_autoClose = true;
        door->m_openDelay = 40;
        door->m_otherHalf = door2;
        door2->m_otherHalf = door;
        door->m_started = door2->m_started = true;
        door2->m_locked = false;
        reset();
        door->SetState(DOOR_ACTION_OPEN, true, false, 0);
        PR(r.doorState, door->m_autoCloseTime == 15040 && door->m_motion == DOOR_OPENING &&
                            door2->m_motion == DOOR_OPENING && door2->m_autoCloseTime == 15000 &&
                            std::count(g_itLog.begin(), g_itLog.end(), "portal pnear 1") == 1 &&
                            g_streams.size() == 2 && g_streams[0]->reliable && g_streams[1]->reliable);
        door2->m_otherHalf = nullptr;
        door->m_otherHalf = nullptr;
        door->m_openDelay = 0;
        door->m_autoCloseTime = 0;
        door->m_delay = 0;

        // The side: the frame's position subtracted.
        frame->m_localMatrix(3) = S_vector(3.0f, 0.0f, 0.0f);
        frame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        door->m_facing = {1.0f, 0.0f, 0.0f, 0.0f};
        PR(r.doorSlide, door->GetSide(S_vector4{2.9f, 0.0f, 0.0f, 1}) == 0 && door->GetSide(S_vector4{3.1f, 0.0f, 0.0f, 1}) == 1);
        door->m_facing = {0.0f, 1.0f, 0.0f, 0.0f};
        PR(r.doorSlide, door->GetSide(S_vector4{3.0f, 0.5f, 0.0f, 1}) == 1 && door->GetSide(S_vector4{3.0f, -0.5f, 0.0f, 1}) == 0);
        frame->m_localMatrix(3) = S_vector(doorX, 0.0f, doorZ);
        frame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;

        // The handles: exactly 45 degrees still rising, exactly 0 still falling; both turned.
        door->m_handle = handleA;
        door->m_handle2 = handle2;
        door->m_handleMoving = true;
        door->m_handleRising = true;
        door->m_handleAngle = 0.785398185f;
        door->m_motion = DOOR_CLOSED;
        reset();
        door->Update(0);
        PR(r.doorMove, door->m_handleRising && door->m_handleAngle == 0.785398185f &&
                           std::count(g_itLog.begin(), g_itLog.end(), "update xD2Handle") == 1 &&
                           std::count(g_itLog.begin(), g_itLog.end(), "update DHandleA") == 1);
        door->m_handleRising = false;
        door->m_handleAngle = 0.0f;
        door->Update(0);
        PR(r.doorMove, door->m_handleMoving && door->m_handleAngle == 0.0f);
        door->m_handleMoving = false;
        door->m_handle = door->m_handle2 = nullptr;

        // Closing by itself: 1 ms left is not yet; the player's height counts (x, y, z summed z, y, x).
        door->m_motion = DOOR_OPEN;
        door->m_autoCloseTime = 31;
        door->Update(30);
        PR(r.doorMove, door->m_autoCloseTime == 1 && door->m_motion == DOOR_OPEN);
        g_gameRole = GAME_ROLE_LOCAL;
        C_Human *player = new C_Human();
        I3D_frame *playerFrame = NewFrame(FRAME_DUMMY, "dplayer2", nullptr);
        player->m_frame = playerFrame;
        g_prPlayer = player;
        door->SetPosition(S_vector(doorX, 5.0f, doorZ));
        *reinterpret_cast<int *>(sceneBase + 0x11C) = 500;
        *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(frame) + 0x11C) = 490;
        playerFrame->m_localMatrix(3) = S_vector(doorX, 14.9f, doorZ);
        playerFrame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        door->m_autoCloseTime = 10;
        door->Update(10);
        PR(r.doorMove, door->m_autoCloseTime == 2000 && door->m_motion == DOOR_OPEN);
        playerFrame->m_localMatrix(3) = S_vector(doorX, 15.5f, doorZ);
        playerFrame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        door->m_autoCloseTime = 10;
        door->Update(10);
        PR(r.doorMove, door->m_motion != DOOR_OPEN && door->m_autoCloseTime == 0);   // (shut at once)
        g_prPlayer = nullptr;
        g_gameRole = GAME_ROLE_SERVER;

        // The delay: 1 ms holds a still door; run out exactly it moves; 1 ms left holds.
        door->m_motion = DOOR_CLOSING;
        door->m_opened = 0.5f;
        door->m_delay = 1;
        reset();
        door->Update(0);
        PR(r.doorMove, g_volumeLog.empty() && door->m_delay == 1);
        door->m_delay = 50;
        reset();
        door->Update(50);
        PR(r.doorMove, door->m_delay == 0 && door->m_opened == 0.5f - static_cast<float>(50u) * 0.001f * 1.5f &&
                           g_volumeLog.size() == 1);
        door->m_delay = 51;
        reset();
        door->Update(50);
        PR(r.doorMove, door->m_delay == 1 && g_volumeLog.empty());
        door->m_delay = 0;
        // Exactly open still opening; exactly shut still closing.
        door->m_motion = DOOR_OPENING;
        door->m_opened = 1.2f;
        door->Update(0);
        PR(r.doorMove, door->m_motion == DOOR_OPENING);
        door->m_motion = DOOR_CLOSING;
        door->m_opened = 0.0f;
        door->Update(0);
        PR(r.doorMove, door->m_motion == DOOR_CLOSING);

        // The moving sound in slot 0: off when open, off when shut, paused when blocked, resumed.
        g_maSlotSounds[0] = reinterpret_cast<I3D_sound *>(moving);
        using SoundLog = std::vector<std::pair<const I3D_frame *, char>>;
        door->m_movingSound = 0;
        door->m_movingSoundPaused = false;
        door->m_motion = DOOR_OPENING;
        door->m_opened = 1.19f;
        reset();
        door->Update(100);
        PR(r.doorMove, door->m_motion == DOOR_OPEN && g_soundLog == SoundLog({{moving, 'o'}}) &&
                           std::count(g_itLog.begin(), g_itLog.end(), "update door") == 1);
        door->m_motion = DOOR_CLOSING;
        door->m_opened = 0.01f;
        reset();
        door->Update(100);
        PR(r.doorMove, door->m_motion == DOOR_CLOSED && g_soundLog == SoundLog({{moving, 'o'}}));
        door->m_motion = DOOR_CLOSING;
        door->m_opened = 0.6f;
        g_sounds[moving].position = 123;
        g_waBlockAt = 0;
        reset();
        door->Update(10);
        PR(r.doorMove, door->m_movingSoundPaused && door->m_movingSoundPos == 123 && g_soundLog == SoundLog({{moving, 'o'}}) &&
                           std::count(g_itLog.begin(), g_itLog.end(), "update door") == 2);
        g_waBlockAt = -1;
        door->m_delay = 0;
        reset();
        door->Update(10);
        PR(r.doorMove, !door->m_movingSoundPaused && g_soundLog == SoundLog({{moving, 'P'}, {moving, 'O'}}));
        // (Without a moving sound (-1) none of it is asked for.)
        I3D_frame *moving2 = MakeSound();
        g_itNames[moving2] = "dsound2";
        g_maSlotSounds[-1] = reinterpret_cast<I3D_sound *>(moving2);
        door->m_movingSound = -1;
        door->m_motion = DOOR_OPENING;
        door->m_opened = 1.19f;
        reset();
        door->Update(100);
        bool none = g_soundLog.empty();
        door->m_motion = DOOR_CLOSING;
        door->m_opened = 0.01f;
        reset();
        door->Update(100);
        none = none && g_soundLog.empty();
        door->m_motion = DOOR_CLOSING;
        door->m_opened = 0.6f;
        g_waBlockAt = 0;
        reset();
        door->Update(10);
        none = none && g_soundLog.empty() && door->m_delay == 200;
        g_waBlockAt = -1;
        door->m_delay = 0;
        door->m_movingSoundPaused = true;
        reset();
        door->Update(10);
        PR(r.doorMove, none && g_soundLog.empty());
        g_maSlotSounds.erase(-1);
        // (Started afresh: not paused.)
        door->m_sounds[2] = 202;
        door->m_movingSoundPaused = true;
        g_soundSlot = 78;
        g_readBits = {{1, 3}};
        g_readPos = 0;
        door->OnMessage(reinterpret_cast<C_BitStream *>(&g_readBits));
        PR(r.doorNet, door->m_movingSound == 78 && !door->m_movingSoundPaused);
        door->m_sounds[2] = -1;
        door->m_motion = DOOR_CLOSED;
        door->m_movingSound = -1;
        g_maSlotSounds.erase(0);

        // The volume: the inverse matrix, the bound's centre and radius; not passable turning about +x.
        door->m_volume.center = S_vector(9.0f, 9.0f, 9.0f);
        door->m_volume.centerW = 9.0f;
        door->m_motion = DOOR_OPENING;
        door->m_opened = 0.2f;
        door->m_axis = {1.0f, 0.0f, 0.0f, 0.0f};
        std::memset(&door->m_invMatrix, 0, sizeof door->m_invMatrix);
        reset();
        door->Update(10);
        S_matrix inverse;
        inverse.Inverse(frame->GetMatrix());
        const I3D_bound_volume &bound = frame->GetBound();
        PR(r.doorMove, door->m_volume.type == COLLISION_TYPE_ACTOR_DOOR && RbSame(door->m_volume.center, bound.center) &&
                           door->m_volume.centerW == bound.radius && door->m_invMatrix.m[3][0] == inverse.m[3][0] &&
                           door->m_invMatrix.m[3][2] == inverse.m[3][2] && door->m_invMatrix.m[0][0] == inverse.m[0][0]);
        // The turn: the axis's turn after the start rotation (any axis, any start).
        const auto product = [](const S_quat &a, const S_quat &b) {
            const S_vector c(a.v.y * b.v.z - a.v.z * b.v.y, a.v.z * b.v.x - a.v.x * b.v.z, a.v.x * b.v.y - a.v.y * b.v.x);
            S_quat q;
            q.v = S_vector((b.v.x * a.s + a.v.x * b.s) + c.x, (b.v.y * a.s + a.v.y * b.s) + c.y,
                           (b.v.z * a.s + a.v.z * b.s) + c.z);
            q.s = a.s * b.s - ((a.v.z * b.v.z + a.v.y * b.v.y) + a.v.x * b.v.x);
            return q;
        };
        door->m_axis = {0.48f, 0.6f, 0.64f, 0.0f};
        door->m_startRot = S_quat(0.5f, 0.5f, -0.5f, 0.5f);
        door->m_opened = 0.7f;
        door->TurnTo();
        S_quat turn;
        turn.Make(S_vector(0.48f, 0.6f, 0.64f), 0.7f);
        PR(r.doorMove, SaSameQuat(frame->m_rot, SaNormalized(product(turn, door->m_startRot))));
        // A slide updates the frame once.
        door->m_kind = DOOR_SLIDING;
        door->m_axis = {0.0f, 1.0f, 0.0f, 0.0f};
        door->m_startPos = {doorX, 0.0f, doorZ, 1.0f};
        reset();
        door->Update(10);
        PR(r.doorSlide, std::count(g_itLog.begin(), g_itLog.end(), "update door") == 1);
        door->m_kind = DOOR_HINGED;
        door->m_motion = DOOR_CLOSED;
        door->m_opened = 0.0f;
        door->m_startRot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
        door->RestartMission(true);

        // The start's fields (not started: all set again; the portals forgotten on a server).
        door->StopMission(true);
        door->m_portal = pNear;
        door->m_otherPortal = pBack;
        door->m_movingSound = 5;
        door->m_handleRising = false;
        door->m_handleMoving = true;
        door->m_opened = 0.3f;
        door->m_initialLocked = true;
        door->m_motion = DOOR_CLOSED;
        door->m_sound.posW = 7.0f;
        door->m_sound.attachTo = frame;
        door->m_sound.source = door;
        reset();
        door->StartMission(true);
        PR(r.doorStart, !door->GetPortal() && !door->GetOtherPortal() && door->m_movingSound == -1 &&
                            door->m_handleRising && !door->m_handleMoving && door->m_opened == 0.0f &&
                            !door->m_initialLocked && door->m_sound.posW == frame->GetMatrix().m[3][3] &&
                            !door->m_sound.attachTo && !door->m_sound.source);
        // A started client's call takes the start placement again under the dynamic root.
        g_gameRole = GAME_ROLE_CLIENT;
        door->m_startPos = {9.0f, 9.0f, 9.0f, 9.0f};
        door->m_startRot = S_quat(0.0f, 1.0f, 0.0f, 0.0f);
        reset();
        door->StartMission(false);
        const S_matrix &world = frame->GetMatrix();
        PR(r.doorStart, door->m_startPos.x == world.m[3][0] && door->m_startPos.y == world.m[3][1] &&
                            door->m_startPos.z == world.m[3][2] && door->m_startPos.w == world.m[3][3] &&
                            SaSameQuat(door->m_startRot, frame->m_rot));
        // Not a portal door: linked back with the matrix kept (1).
        door->m_hasPortals = false;
        reset();
        door->StartMission(false);
        PR(r.doorStart, !g_itLog.empty() && g_itLog[0].size() > 2 && g_itLog[0].substr(g_itLog[0].size() - 2) == " 1");
        door->m_hasPortals = true;
        LinkUnder(frame, sector);
        frame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        g_gameRole = GAME_ROLE_SERVER;

        // The facing normalised as the inline copies do: a tiny x or z goes to 1 (a 0 dot: not turned).
        for (int k = 0; k < 2; ++k) {
            frame->m_localMatrix.m[2][0] = k == 0 ? 1e-5f : 0.0f;
            frame->m_localMatrix.m[2][1] = 0.0f;
            frame->m_localMatrix.m[2][2] = k == 0 ? 0.0f : 3e-6f;
            frame->m_frameFlags &= ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_ROT_DIRTY);
            frame->m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
            door->m_facing.w = 7.0f;
            reset();
            SaWritten([&](C_chunk &c) { return door->WriteProperties(c); }, nullptr);
            PR(r.doorProps, door->m_facing.x == (k == 0 ? 1.0f : 0.0f) && door->m_facing.y == 0.0f &&
                                door->m_facing.z == (k == 0 ? 0.0f : 1.0f) && door->m_facing.w == 0.0f &&
                                std::count(g_itLog.begin(), g_itLog.end(), "update door") == 2);
        }
        frame->m_localMatrix.m[2][0] = 0.0f;
        frame->m_localMatrix.m[2][1] = 0.0f;
        frame->m_localMatrix.m[2][2] = 1.0f;
        frame->m_frameFlags &= ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_ROT_DIRTY);

        // A chunk after the pickable one is read; of two saved states the second wins.
        S_SaBytes body;
        body.Val(0).Val(DOOR_CLOSED).Val(1.2f).Val(1.5f).Val(S_vector(0.0f, 1.0f, 0.0f)).Val(0).Val(uint8_t(0))
            .Val(uint8_t(0)).Val(uint32_t(0)).Val(uint8_t(0));
        body.Head(0x6C, 7).Val(uint8_t(0)).Head(0x6D, 11).Val(0x3333).Val(uint8_t(0));
        S_SaBytes props;
        props.Head(0x777, 6 + static_cast<uint32_t>(body.b.size()));
        props.Put(body.b.data(), body.b.size());
        S_SaReader rd(props);
        door->ReadProperties(rd.c);
        PR(r.doorProps, !door->m_pickable && door->m_textId == 0x3333);
        door->m_pickable = true;
        S_SaBytes in;
        in.Head(0x777, 6 + 2 * (6 + 41)).Head(3000, 6 + 41).Val(0.25f).Val(uint8_t(0)).Val(DOOR_CLOSING).Val(66)
            .Val(S_vector(doorX, 0.0f, doorZ)).Val(1.0f).Val(0.0f).Val(0.0f).Val(0.0f)
            .Head(3000, 6 + 41).Val(0.5f).Val(uint8_t(0)).Val(DOOR_CLOSED).Val(0)
            .Val(S_vector(doorX, 0.0f, doorZ)).Val(1.0f).Val(0.0f).Val(0.0f).Val(0.0f);
        S_SaReader rd2(in);
        door->LoadGame(rd2.c);
        PR(r.doorSave, door->m_opened == 0.5f && door->m_motion == DOOR_CLOSED && door->m_delay == 0);
        door->m_opened = 0.0f;
    }
    creator->Destroy(door);
    creator->Destroy(door2);
    g_maSceneFrames.clear();
    g_maSlotSounds.clear();
}
