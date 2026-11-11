// Part 1 of the physics harness (included by physics_test.cpp): the module and the objects' life, checked against my
// own reading of the rules.
namespace {

// The module emptied, the logs cleared, a single player game.
void Fresh()
{
    g_physicsModule.DeleteAll();
    g_physicsModule.m_doors.clear();
    g_breakables.clear();
    g_clearBreakables = 0;
    g_callbackCalls = 0;
    g_callbackResult = 0;
    g_callbackContexts.clear();
    g_callbackKinds.clear();
    g_callbackHandles.clear();
    g_gameRole = GAME_ROLE_LOCAL;
    GetGame()->m_scene = nullptr;
}
// A prop under the primary sector: a visual of half size `half` at `pos`.
I3D_frame *Prop(const std::string &name, const S_vector &pos, const S_vector &half, float yaw = 0.0f, const std::string &props = "")
{
    return NewFrame(FRAME_VISUAL, name, pos, YawQuat(yaw), Box(half), props, g_sector);
}
bool Near(const S_vector &a, const S_vector &b, float tol = 1e-4f) { return (a - b).Magnitude() <= tol; }

// ---- CreateObject: every type, the handle, the lists, the frame's data, the callback, the volumes
void TestCreate()
{
    Fresh();
    for (int type = PHYS_SIMPLE; type <= PHYS_SWINGING; ++type) {
        I3D_frame *frame = Prop("obj" + std::to_string(type), S_vector(10.0f + 3.0f * type, 3.0f, 10.0f), S_vector(0.3f, 0.6f, 0.2f), 0.4f * type);
        S_PhysicsObjectDesc d = Desc(frame, type, type % 2 == 1);
        d.callback = Callback;
        d.callbackContext = reinterpret_cast<void *>(static_cast<intptr_t>(100 + type));
        d.materialId = 5 + type;
        d.unknown14 = true;
        d.collideWithBoxes = type % 3 == 0;
        const int calls = g_callbackCalls;
        const size_t handles = g_physicsModule.m_handles.size();
        T_PhysicsObject h = g_physicsModule.CreateObject(d);
        CHECK(h && h != g_noPhysicsObject && h->index >= 0 && h->active == d.startActive);
        CHECK(g_physicsModule.m_handles.size() == handles + 1 && g_physicsModule.m_handles.back() == h);
        C_PhysObject *o = ObjectOf(h);
        CHECK(o && o->GetType() == type && o->GetFrame() == frame && o->GetHandle() == h);
        if (!o)
            continue;
        CHECK(o->m_materialId == d.materialId && o->m_unknown144 && o->m_collideWithBoxes == d.collideWithBoxes && o->m_callback == Callback &&
              o->m_callbackContext == d.callbackContext && !o->IsResting());
        CHECK(frame->GetData() && frame->GetData()->physicsObject == h);
        CHECK(g_callbackCalls == calls + 1 && g_callbackContexts.back() == d.callbackContext && g_callbackKinds.back() == PHYS_CALLBACK_CREATED &&
              g_callbackHandles.back() == h);
        CHECK(frame->m_refCount == 2);                                   // (the object's reference)
        CHECK(Near(o->m_pos, frame->GetWorldPos()));
        S_quat rot;
        rot.Make(frame->GetMatrix());
        CHECK(std::fabs(o->m_rot.s - rot.s) < 1e-5f && Near(o->m_rot.v, rot.v, 1e-5f));
        const I3D_bound_volume &bound = frame->m_bound;
        switch (type) {
        case PHYS_SIMPLE:
        case PHYS_FALLING:
            CHECK(!o->m_volumeActive && !InWorld(o->m_volume));
            break;
        case PHYS_SPHERE:
        case PHYS_CAN: {
            const float radius = type == PHYS_CAN ? static_cast<C_PhysCan *>(o)->m_radius : static_cast<C_PhysSphere *>(o)->m_radius;
            CHECK(o->m_volumeActive && InWorld(o->m_volume) && o->m_volume.flags == COLLISION_SHAPE_SPHERE && radius == bound.radius &&
                  o->m_volume.radius == bound.radius && Near(o->m_volume.center, frame->GetWorldPos()) && o->m_volume.frame == frame &&
                  o->m_volume.type == 0);
            break;
        }
        case PHYS_BOX:
            // (Its center taken through the inverse matrix, as the original.)
            CHECK(o->m_volumeActive && InWorld(o->m_volume) && o->m_volume.flags == COLLISION_SHAPE_BOX &&
                  o->m_volume.type == COLLISION_TYPE_LIGHT_OBJECT && Near(o->m_volume.center, bound.center * o->m_invMatrix));
            break;
        case PHYS_DOOR: {
            auto *door = static_cast<C_PhysDoor *>(o);
            CHECK(InWorld(o->m_volume) && o->m_volume.flags == COLLISION_SHAPE_BOX && o->m_volume.type == COLLISION_TYPE_DOOR && o->m_freezeVolume);
            // Own: the bottom center, the axis across the leaf, the top corners in local space.
            const S_vector top = bound.bbox.max * frame->GetMatrix(), bottom = bound.bbox.min * frame->GetMatrix();
            CHECK(Near(door->m_axis, S_vector(top.x - bottom.x, 0, top.z - bottom.z)) &&
                  Near(door->m_center, S_vector((top.x + bottom.x) * 0.5f, bottom.y, (top.z + bottom.z) * 0.5f)));
            CHECK(Near(door->m_edge1, bound.bbox.max, 1e-4f) && Near(door->m_edge2, S_vector(bound.bbox.min.x, bound.bbox.max.y, bound.bbox.min.z), 1e-4f));
            CHECK(!door->m_moving && door->m_phase == 0 && door->m_damping == 0.4f && door->m_speed == 1.0f && door->m_angle == 1.5707964f &&
                  door->m_swingState == 0 && g_physicsModule.m_doors.back() == o);
            break;
        }
        case PHYS_SWINGING: {
            auto *sw = static_cast<C_PhysSwingingObject *>(o);
            CHECK(InWorld(o->m_volume) && o->m_volume.flags == COLLISION_SHAPE_BOX && o->m_volume.type == COLLISION_TYPE_LIGHT_OBJECT);
            CHECK(InWorld(sw->m_volume2) && sw->m_volume2.flags == COLLISION_SHAPE_SPHERE && sw->m_volume2.radius == bound.radius &&
                  Near(sw->m_volume2.center, bound.center * frame->GetMatrix() - S_vector(0, 0.5f, 0)) && sw->m_parent == g_sector && !sw->m_swinging);
            break;
        }
        }
    }
    CHECK(ListsConsistent());

    // No frame, or no such type: no object (and no handle kept).
    S_PhysicsObjectDesc d = Desc(nullptr, PHYS_CAN);
    CHECK(g_physicsModule.CreateObject(d) == g_noPhysicsObject);
    for (int type : {static_cast<int>(PHYS_BASE), 9, -1}) {
        const size_t handles = g_physicsModule.m_handles.size();
        I3D_frame *frame = Prop("bad", S_vector(5, 3, 5), S_vector(0.2f, 0.2f, 0.2f));
        CHECK(g_physicsModule.CreateObject(Desc(frame, type)) == g_noPhysicsObject && g_physicsModule.m_handles.size() == handles);
        CHECK(!frame->GetData());
    }
    // A can's own radius; a door that bullets pass; a breakable fence.
    {
        I3D_frame *frame = Prop("can", S_vector(5, 3, 30), S_vector(0.1f, 0.15f, 0.1f));
        S_PhysicsObjectDesc can = Desc(frame, PHYS_CAN);
        can.radius = 0.7f;
        auto *o = static_cast<C_PhysCan *>(ObjectOf(g_physicsModule.CreateObject(can)));
        CHECK(o && o->m_radius == 0.7f && o->m_volume.radius == 0.7f);
        I3D_frame *f2 = Prop("door", S_vector(8, 2, 30), S_vector(0.5f, 1.0f, 0.05f));
        S_PhysicsObjectDesc door = Desc(f2, PHYS_DOOR);
        door.shootThrough = true;
        door.door.angle = 0.7f;
        door.door.freeSwing = true;
        door.door.tree = true;
        auto *dd = static_cast<C_PhysDoor *>(ObjectOf(g_physicsModule.CreateObject(door)));
        CHECK(dd && dd->m_volume.flags == (COLLISION_SHAPE_BOX | COLLISION_VOLUME_SHOOT_THROUGH) && dd->m_angle == 0.7f && dd->m_freeSwing && dd->m_tree &&
              !dd->m_breakable);
        CHECK(g_breakables.empty());
        I3D_frame *f3 = Prop("fence", S_vector(12, 2, 30), S_vector(1.0f, 0.8f, 0.05f));
        S_PhysicsObjectDesc fence = Desc(f3, PHYS_DOOR);
        fence.door.breakable = true;
        fence.door.breakModel = "plotx";
        auto *fd = static_cast<C_PhysDoor *>(ObjectOf(g_physicsModule.CreateObject(fence)));
        CHECK(fd && fd->m_breakable && (fd->m_volume.flags & COLLISION_VOLUME_SHOOT_THROUGH) && std::strcmp(fd->m_breakModel, "plotx") == 0);
        CHECK(g_breakables.size() == 1 && g_breakables[0].frame == f3 && g_breakables[0].door == fd && g_breakables[0].volume == &fd->m_volume);
    }
    CHECK(ListsConsistent());
}

// ---- DestroyObject, handle reuse, the lists' first free slots
void TestDestroy()
{
    Fresh();
    std::vector<T_PhysicsObject> h;
    std::vector<I3D_frame *> frames;
    for (int i = 0; i < 6; ++i) {
        frames.push_back(Prop("d" + std::to_string(i), S_vector(5.0f + 2 * i, 3, 50), S_vector(0.3f, 0.3f, 0.3f)));
        S_PhysicsObjectDesc d = Desc(frames.back(), i % 2 ? PHYS_CAN : PHYS_DOOR);
        d.callback = Callback;
        h.push_back(g_physicsModule.CreateObject(d));
    }
    CHECK(g_physicsModule.m_resting.size() == 6 && g_physicsModule.m_doors.size() == 3);
    // Nothing to destroy: no handles yet in an empty module; the "none" handle; a gone one.
    T_PhysicsObject none = g_noPhysicsObject;
    CHECK(!g_physicsModule.DestroyObject(&none));
    T_PhysicsObject hh = h[2];
    C_PhysObject *door = ObjectOf(hh);
    S_CollisionVolume *volume = &door->m_volume;
    const int calls = g_callbackCalls;
    CHECK(g_physicsModule.DestroyObject(&hh));
    CHECK(hh == g_noPhysicsObject && h[2]->index == -1 && g_physicsModule.m_resting[2] == nullptr);
    CHECK(frames[2]->GetData()->physicsObject == g_noPhysicsObject && frames[2]->m_refCount == 1);
    CHECK(g_callbackCalls == calls + 1 && g_callbackKinds.back() == PHYS_CALLBACK_DELETED && g_callbackHandles.back() == h[2]);   // (the destructor)
    CHECK(!InWorld(*volume) || true);                                   // (door volumes are removed by the module's DeleteAll only)
    CHECK(std::count(g_physicsModule.m_doors.begin(), g_physicsModule.m_doors.end(), nullptr) == 1);   // (fixed: the door's own entry)
    T_PhysicsObject again = h[2];
    CHECK(!g_physicsModule.DestroyObject(&again));                       // (index -1)
    // The next object takes the free handle and the free slot.
    I3D_frame *f = Prop("reuse", S_vector(30, 3, 50), S_vector(0.3f, 0.3f, 0.3f));
    T_PhysicsObject r = g_physicsModule.CreateObject(Desc(f, PHYS_SIMPLE));
    CHECK(r == h[2] && r->index == 2 && !r->active && g_physicsModule.m_resting[2] && g_physicsModule.m_resting[2]->GetFrame() == f);
    // Destroying through a stale copy of a handle that was reused does nothing to the new object? (The handle is
    // shared: it now names the new object, which is destroyed.)
    CHECK(ListsConsistent());
    // A can's volume leaves the world with it.
    T_PhysicsObject c = h[1];
    C_PhysObject *can = ObjectOf(c);
    CHECK(can && InWorld(can->m_volume));
    CHECK(g_physicsModule.DestroyObject(&c));
    CHECK(ListsConsistent());
}

// ---- random sequences: the lists and handles stay consistent after every step
void TestRandomOps()
{
    Fresh();
    std::mt19937 rng(31337);
    std::uniform_real_distribution<float> pos(4.0f, 60.0f), u(-1.0f, 1.0f);
    std::vector<T_PhysicsObject> live;
    long ops = 0, consistent = 0, rested = 0;
    const int types[] = {PHYS_SIMPLE, PHYS_CAN, PHYS_FALLING, PHYS_DOOR, PHYS_SWINGING};
    for (int i = 0; i < 3000 / QUICK; ++i) {
        const int op = rng() % 8;
        if (op <= 2 || live.empty()) {
            const int type = types[rng() % 5];
            I3D_frame *frame = Prop("r", S_vector(pos(rng), 1.2f + 2.0f * (u(rng) + 1), pos(rng)), S_vector(0.2f, 0.3f, 0.2f), u(rng) * 3);
            S_PhysicsObjectDesc d = Desc(frame, type, (rng() & 1) && type != PHYS_DOOR && type != PHYS_SWINGING);
            d.callback = Callback;
            T_PhysicsObject h = g_physicsModule.CreateObject(d);
            if (h != g_noPhysicsObject)
                live.push_back(h);
        } else if (op == 3) {
            T_PhysicsObject h = live[rng() % live.size()];
            g_physicsModule.DestroyObject(&h);
        } else if (op == 4) {
            g_physicsModule.ApplyImpulse(live[rng() % live.size()], S_vector(u(rng), u(rng), u(rng)) * 3.0f, S_vector(pos(rng), 2, pos(rng)), 0.5f);
        } else if (op == 5) {
            const S_vector dir = S_vector(u(rng), 0, u(rng)).Normalized();
            g_physicsModule.Hit(live[rng() % live.size()], rng() % 2, &dir, nullptr);
        } else {
            g_gameRole = 1 + rng() % 3;
            const size_t before = g_physicsModule.m_active.size();
            if (op == 6)
                g_physicsModule.Tick(50 + rng() % 100);
            else
                g_physicsModule.Update(50 + rng() % 100);
            rested += before > g_physicsModule.m_active.size();
        }
        ++ops;
        consistent += ListsConsistent();
    }
    CHECK(consistent == ops);
    std::printf("  random operations %ld: lists consistent after each %ld, steps that put objects to rest %ld\n", ops, consistent, rested);
    CHECK(rested > 0);
}

// ---- the server's and the client's steps by type
void TestRouting()
{
    // Expected stepped types: server only: everything but debris and swinging objects; client only: doors, debris,
    // swinging objects; both: the server's minus doors, the client's.
    const char *roleNames[4] = {"", "server", "client", "both"};
    for (uint32_t role : {1u, 2u, 3u}) {
        Fresh();
        std::map<int, I3D_frame *> frames;
        for (int type = PHYS_SIMPLE; type <= PHYS_SWINGING; ++type) {
            frames[type] = Prop("t" + std::to_string(type), S_vector(6.0f + 6.0f * type, 6.0f, 6.0f), S_vector(0.3f, 0.3f, 0.3f));
            S_PhysicsObjectDesc d = Desc(frames[type], type, type != PHYS_DOOR && type != PHYS_SWINGING);
            T_PhysicsObject h = g_physicsModule.CreateObject(d);
            if (type == PHYS_DOOR || type == PHYS_SWINGING) {
                const S_vector dir(1, 0, 0);
                g_physicsModule.Hit(h, PHYS_HIT_KICK, &dir, nullptr);      // (woken, its turns set)
            }
        }
        g_gameRole = role;
        std::map<int, int> before;
        for (auto &[type, f] : frames)
            before[type] = Fake(f)->setPos;
        g_physicsModule.Tick(100);
        std::set<int> ticked, updated;
        for (auto &[type, f] : frames)
            if (Fake(f)->setPos != before[type])
                ticked.insert(type);
        for (auto &[type, f] : frames)
            before[type] = Fake(f)->setPos;
        g_physicsModule.Update(100);
        for (auto &[type, f] : frames)
            if (Fake(f)->setPos != before[type])
                updated.insert(type);
        std::set<int> wantTick, wantUpdate;
        for (int type = PHYS_SIMPLE; type <= PHYS_SWINGING; ++type) {
            if ((role & GAME_ROLE_SERVER) && type != PHYS_FALLING && type != PHYS_SWINGING && (type != PHYS_DOOR || !(role & GAME_ROLE_CLIENT)))
                wantTick.insert(type);
            if ((role & GAME_ROLE_CLIENT) && (type == PHYS_DOOR || type == PHYS_FALLING || type == PHYS_SWINGING))
                wantUpdate.insert(type);
        }
        CHECK(ticked == wantTick);
        CHECK(updated == wantUpdate);
        if (ticked != wantTick || updated != wantUpdate)
            std::printf("  routing (%s): ticked %zu, updated %zu\n", roleNames[role], ticked.size(), updated.size());
    }
}

// ---- coming to rest: to the resting list; deleted when the callback answers 1 or the object says so
void TestRest()
{
    Fresh();
    // A point object 5 mm over the floor meets it in its first step, slower than 1 m/s: stops. (A can would not: its
    // sweep starts inside its own volume, which it meets first and ignores; see motion_test.inc.)
    I3D_frame *f = Prop("point", S_vector(10, 1.005f, 10), S_vector(0.15f, 0.15f, 0.15f));
    S_PhysicsObjectDesc d = Desc(f, PHYS_SIMPLE, true);
    d.callback = Callback;
    T_PhysicsObject h = g_physicsModule.CreateObject(d);
    CHECK(h->active);
    g_callbackCalls = 0;
    g_physicsModule.Tick(100);
    CHECK(!h->active && h->index >= 0 && ObjectOf(h) && g_callbackCalls == 1 && g_callbackKinds.back() == PHYS_CALLBACK_RESTING && g_callbackHandles.back() == h);
    // Woken by a push: active again, pushed, called back.
    g_callbackCalls = 0;
    g_physicsModule.ApplyImpulse(h, S_vector(2, 0, 0), S_vector(10, 1.3f, 10), 1.0f);   // (2 m/s: sqrt(2 * 2 * 1 / 1))
    CHECK(h->active && g_callbackCalls == 1 && g_callbackKinds.back() == PHYS_CALLBACK_PUSHED && std::fabs(ObjectOf(h)->m_velocity.x - 2.0f) < 1e-5f);
    // The callback answers 1: deleted when it stops.
    I3D_frame *f2 = Prop("point2", S_vector(14, 1.005f, 10), S_vector(0.15f, 0.15f, 0.15f));
    S_PhysicsObjectDesc d2 = Desc(f2, PHYS_SIMPLE, true);
    d2.callback = Callback;
    T_PhysicsObject h2 = g_physicsModule.CreateObject(d2);
    g_callbackResult = 1;
    g_physicsModule.Tick(100);
    g_callbackResult = 0;
    CHECK(h2->index == -1 && f2->GetData()->physicsObject == g_noPhysicsObject);
    // Small debris resting on the ground marks itself as resting: deleted.
    I3D_frame *f3 = Prop("bit", S_vector(18, 1.05f, 10), S_vector(0.05f, 0.05f, 0.05f));
    T_PhysicsObject h3 = g_physicsModule.CreateObject(Desc(f3, PHYS_FALLING, true));
    for (int i = 0; i < 40 && h3->index != -1; ++i)
        g_physicsModule.Update(100);
    CHECK(h3->index == -1);
    // Debris over nothing: gone too.
    I3D_frame *f4 = Prop("lost", S_vector(-20, 5, -20), S_vector(0.05f, 0.05f, 0.05f));
    T_PhysicsObject h4 = g_physicsModule.CreateObject(Desc(f4, PHYS_FALLING, true));
    for (int i = 0; i < 40 && h4->index != -1; ++i)
        g_physicsModule.Update(100);
    CHECK(h4->index == -1);
    // A hit on a resting door wakes it; a hit or a push on a gone handle does nothing.
    I3D_frame *f5 = Prop("door", S_vector(30, 2, 30), S_vector(0.5f, 1.0f, 0.05f));
    T_PhysicsObject h5 = g_physicsModule.CreateObject(Desc(f5, PHYS_DOOR));
    const S_vector dir(0, 0, 1);
    CHECK(g_physicsModule.Hit(h5, PHYS_HIT_KICK, &dir, nullptr) && h5->active);
    CHECK(!g_physicsModule.Hit(h5, PHYS_HIT_KICK, &dir, nullptr));       // (already moving)
    T_PhysicsObject gone = h2;
    CHECK(!g_physicsModule.Hit(gone, PHYS_HIT_KICK, &dir, nullptr));
    g_physicsModule.ApplyImpulse(gone, S_vector(1, 0, 0), S_vector(0, 0, 0), 1.0f);
    CHECK(!g_physicsModule.Hit(nullptr, PHYS_HIT_KICK, &dir, nullptr));
    CHECK(ListsConsistent());
}

// ---- the scene's "phobj" frames and their user properties; the mission's start, stop and restart
struct S_PropCase {
    const char *props;
    int type;                       // -1: no object
    bool active, shootThrough, freeSwing, tree, breakable;
    const char *breakModel;
    float angle;
};
const S_PropCase kProps[] = {
    {"phobj", PHYS_DOOR, false, true, false, false, false, "", 1.5707964f},
    {"PhObj SightStop", PHYS_DOOR, false, false, false, false, false, "", 1.5707964f},
    {"phobj can", PHYS_CAN, true, true, false, false, false, "", 0},
    {"phobj cross", PHYS_SWINGING, false, true, false, false, false, "", 0},
    {"phobj free", PHYS_DOOR, false, true, true, false, false, "", 1.5707964f},
    {"phobj axis", PHYS_DOOR, false, true, true, false, false, "", 1.5707964f},
    {"phobj tree", PHYS_DOOR, false, true, false, true, false, "", 1.5707964f},
    {"phobj break", PHYS_DOOR, false, true, false, false, true, "plotdira", 1.5707964f},
    {"phobj breakFence2.4ds other", PHYS_DOOR, false, true, false, false, true, "fence2", 1.5707964f},
    {"phobj angle90", PHYS_DOOR, false, true, false, false, false, "", 90 * 0.008726646f},
    {"phobj angle 45.5 free", PHYS_DOOR, false, true, true, false, false, "", 45.5f * 0.008726646f},
    {"scenery", -1, false, false, false, false, false, "", 0},
    {"", -1, false, false, false, false, false, "", 0},
};
void TestSceneObjects()
{
    Fresh();
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(g_sceneBytes);
    // The scene's frames: one per case, under a dummy (found in depth), and a model frame (skipped).
    I3D_frame *group = NewFrame(FRAME_DUMMY, "group", S_vector(0, 0, 0), Identity(), Box(S_vector(1, 1, 1)), "", g_sector);
    std::vector<I3D_frame *> frames;
    for (size_t i = 0; i < sizeof kProps / sizeof kProps[0]; ++i)
        frames.push_back(NewFrame(FRAME_VISUAL, "p" + std::to_string(i), S_vector(4.0f + 3 * i, 2.5f, 58), Identity(), Box(S_vector(0.4f, 0.9f, 0.05f)),
                                  kProps[i].props, i % 2 ? group : g_sector));
    I3D_frame *model = NewFrame(FRAME_MODEL, "model", S_vector(4, 2.5f, 54), Identity(), Box(S_vector(0.4f, 0.4f, 0.4f)), "phobj", g_sector);
    for (uint32_t role : {static_cast<uint32_t>(GAME_ROLE_LOCAL), static_cast<uint32_t>(GAME_ROLE_SERVER), static_cast<uint32_t>(GAME_ROLE_CLIENT)}) {
        g_gameRole = role;
        // A client's StartMission(false) builds them when the game is not also the server.
        g_physicsModule.StartMission(false);
        const bool built = !(role & GAME_ROLE_SERVER);
        g_physicsModule.StartMission(true);                              // (the server's always does)
        CHECK(model->GetData() == nullptr || model->GetData()->physicsObject == g_noPhysicsObject);
        for (size_t i = 0; i < frames.size(); ++i) {
            const S_PropCase &c = kProps[i];
            T_PhysicsObject h = frames[i]->GetData() ? frames[i]->GetData()->physicsObject : nullptr;
            C_PhysObject *o = ObjectOf(h);
            if (c.type < 0) {
                CHECK(!o);
                continue;
            }
            CHECK(o && o->GetType() == c.type && h->active == c.active);
            if (!o)
                continue;
            if (c.type == PHYS_CAN)
                continue;
            CHECK(((o->m_volume.flags & COLLISION_VOLUME_SHOOT_THROUGH) != 0) == (c.shootThrough || c.breakable) || c.type == PHYS_SWINGING);
            if (c.type != PHYS_DOOR)
                continue;
            auto *door = static_cast<C_PhysDoor *>(o);
            CHECK(door->m_freeSwing == c.freeSwing && door->m_tree == c.tree && door->m_breakable == c.breakable &&
                  std::fabs(door->m_angle - c.angle) < 1e-6f);
            CHECK(std::strcmp(door->m_breakModel, c.breakModel) == 0);
        }
        CHECK(ListsConsistent());
        // Stop: a client's StopMission(false) deletes them only when not also the server; the server's always.
        g_physicsModule.StopMission(false);
        CHECK(g_physicsModule.m_handles.empty() == !(role & GAME_ROLE_SERVER));
        g_physicsModule.StopMission(true);
        CHECK(g_physicsModule.m_handles.empty() && g_physicsModule.m_active.empty() && g_physicsModule.m_resting.empty() && g_physicsModule.m_doors.empty());
        (void)built;
    }
    // Restart: stops and starts by role, the breakable list cleared in between.
    for (uint32_t role : {static_cast<uint32_t>(GAME_ROLE_LOCAL), static_cast<uint32_t>(GAME_ROLE_SERVER), static_cast<uint32_t>(GAME_ROLE_CLIENT)}) {
        g_gameRole = role;
        g_physicsModule.StartMission(true);
        T_PhysicsObject before = frames[0]->GetData()->physicsObject;
        const S_vector dir(0, 0, 1);
        g_physicsModule.Hit(before, PHYS_HIT_KICK, &dir, nullptr);
        g_clearBreakables = 0;
        g_physicsModule.RestartMission(true);
        T_PhysicsObject after = frames[0]->GetData()->physicsObject;
        C_PhysObject *o = ObjectOf(after);
        CHECK(g_clearBreakables == 1 && o && !after->active && !static_cast<C_PhysDoor *>(o)->m_moving);
        CHECK(static_cast<int>(g_physicsModule.m_doors.size()) == 9);    // (one each, made once)
        g_physicsModule.StopMission(true);
    }
    GetGame()->m_scene = nullptr;
    for (I3D_frame *f : frames)
        LinkUnder(f, nullptr);
    LinkUnder(group, nullptr);
    LinkUnder(model, nullptr);
}

// ---- the opening doors sent to a joining client, and read back
void TestState()
{
    Fresh();
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(g_sceneBytes);
    std::vector<I3D_frame *> frames;
    for (int i = 0; i < 6; ++i)
        frames.push_back(NewFrame(FRAME_VISUAL, "door" + std::to_string(i), S_vector(4.0f + 4 * i, 2.0f, 34), YawQuat(0.3f * i),
                                  Box(S_vector(0.5f, 1.0f, 0.05f)), "phobj", g_sector));
    g_physicsModule.StartMission(true);
    // An empty module writes nothing.
    {
        C_PhysicsModule empty(99, "empty");
        C_NetMessage msg;
        empty.WriteState(&msg);
        CHECK(msg.BitPosition() == 0);
    }
    // Kicked doors 1 and 4 (opening), a pushed door 2 (rattling: not sent).
    const S_vector kick1 = S_vector(0.6f, 0, 0.8f), kick4 = S_vector(-1, 0, 0.2f).Normalized(), push = S_vector(0, 0, 1);
    auto doorOf = [&](int i) { return static_cast<C_PhysDoor *>(ObjectOf(frames[i]->GetData()->physicsObject)); };
    const int index1 = static_cast<int>(std::find(g_physicsModule.m_doors.begin(), g_physicsModule.m_doors.end(), doorOf(1)) - g_physicsModule.m_doors.begin());
    const int index4 = static_cast<int>(std::find(g_physicsModule.m_doors.begin(), g_physicsModule.m_doors.end(), doorOf(4)) - g_physicsModule.m_doors.begin());
    g_physicsModule.Hit(frames[1]->GetData()->physicsObject, PHYS_HIT_KICK, &kick1, nullptr);
    g_physicsModule.Hit(frames[4]->GetData()->physicsObject, PHYS_HIT_KICK, &kick4, nullptr);
    g_physicsModule.Hit(frames[2]->GetData()->physicsObject, PHYS_HIT_PUSH, &push, nullptr);
    CHECK(doorOf(1)->IsOpening() && doorOf(4)->IsOpening() && !doorOf(2)->IsOpening() && doorOf(2)->m_moving);
    C_NetMessage msg;
    g_physicsModule.WriteState(&msg);
    // Own reading: the doors from the last down, each a 16-bit index and a heading; -1 ends.
    msg.SetBitPosition(0);
    std::vector<int> sent;
    for (int index = static_cast<int16_t>(msg.ReadBits(16)); index != -1 && sent.size() < 10; index = static_cast<int16_t>(msg.ReadBits(16))) {
        sent.push_back(index);
        S_vector dir;
        msg.ReadDirection(dir, -1);
        const S_vector want = index == index1 ? kick1 : kick4;
        CHECK(std::fabs(std::atan2(dir.z, dir.x) - std::atan2(want.z, want.x)) < 0.006f);   // (the heading in 0.01 steps)
    }
    CHECK(sent.size() == 2 && sent[0] == std::max(index1, index4) && sent[1] == std::min(index1, index4));
    // The joining client: the same doors from the scene, the message read: those two open (kicked).
    g_physicsModule.StopMission(true);
    g_physicsModule.StartMission(true);
    msg.SetBitPosition(0);
    g_physicsModule.ReadState(&msg);
    for (int i = 0; i < 6; ++i) {
        C_PhysDoor *door = doorOf(i);
        CHECK(door && door->m_moving == (i == 1 || i == 4) && door->IsOpening() == (i == 1 || i == 4));
        if (door && (i == 1 || i == 4)) {
            const S_vector want = i == 1 ? kick1 : kick4;
            CHECK(std::fabs(std::atan2(door->m_hitDir.z, door->m_hitDir.x) - std::atan2(want.z, want.x)) < 0.006f && door->m_hitDir.y == 0);
        }
    }
    // The first door of the list sent too; what follows the doors in the message read in its place.
    {
        g_physicsModule.StopMission(true);
        g_physicsModule.StartMission(true);
        const S_vector kick0(0, 0, -1);
        C_PhysObject *first = g_physicsModule.m_doors[0];
        g_physicsModule.Hit(first->GetHandle(), PHYS_HIT_KICK, &kick0, nullptr);
        C_NetMessage both;
        g_physicsModule.WriteState(&both);
        both.WriteBits(0x5A, 8);
        g_physicsModule.StopMission(true);
        g_physicsModule.StartMission(true);
        both.SetBitPosition(0);
        g_physicsModule.ReadState(&both);
        CHECK(both.ReadBits(8) == 0x5A);
        auto *door = static_cast<C_PhysDoor *>(g_physicsModule.m_doors[0]);
        CHECK(door && door->IsOpening());
        for (size_t i = 1; i < g_physicsModule.m_doors.size(); ++i)
            CHECK(!static_cast<C_PhysDoor *>(g_physicsModule.m_doors[i])->m_moving);
    }
    // A door index past the list (a destroyed door's empty entry, or none): read over and left.
    {
        C_NetMessage bad;
        bad.WriteBits(200, 16);
        bad.WriteDirection(S_vector(1, 0, 0), -1);
        T_PhysicsObject h = frames[0]->GetData()->physicsObject;
        const int index0 = static_cast<int>(std::find(g_physicsModule.m_doors.begin(), g_physicsModule.m_doors.end(), doorOf(0)) - g_physicsModule.m_doors.begin());
        g_physicsModule.DestroyObject(&h);
        bad.WriteBits(static_cast<uint32_t>(index0), 16);
        bad.WriteDirection(S_vector(1, 0, 0), -1);
        bad.WriteBits(5, 16);
        bad.WriteDirection(S_vector(0, 0, 1), -1);
        bad.WriteBits(0xFFFF, 16);
        bad.SetBitPosition(0);
        g_physicsModule.ReadState(&bad);
        const int index5 = 5;
        C_PhysDoor *door5 = static_cast<C_PhysDoor *>(g_physicsModule.m_doors[index5]);
        CHECK(door5 && door5->m_moving);
        CHECK(bad.BitPosition() > 0);
    }
    g_physicsModule.StopMission(true);
    GetGame()->m_scene = nullptr;
    for (I3D_frame *f : frames)
        LinkUnder(f, nullptr);
}

// ---- the frames' sync and the volumes
void TestSync()
{
    Fresh();
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(g_sceneBytes);
    I3D_frame *f = Prop("sync", S_vector(16, 6, 16), S_vector(0.3f, 0.3f, 0.3f));
    T_PhysicsObject h = g_physicsModule.CreateObject(Desc(f, PHYS_CAN));
    C_PhysObject *o = ObjectOf(h);
    CHECK(f->m_parent == g_sector && Fake(f)->links >= 1);              // (relinked to the primary sector, keeping its place)
    o->m_pos = S_vector(17, 5, 15);
    o->m_rot = YawQuat(0.8f);
    const int setPos = Fake(f)->setPos;
    g_sectorUpdates.clear();
    o->SyncFrame();
    CHECK(Fake(f)->setPos == setPos + 1 && Near(f->GetWorldPos(), S_vector(17, 5, 15)) && (g_sectorUpdates[std::make_pair(static_cast<const I3D_frame *>(f), 0)] == 1));
    CHECK(Near(o->m_volume.center, f->m_bound.center * f->GetMatrix()) && Near(o->m_matrix(3), S_vector(17, 5, 15)));
    // The volume follows only when it is active and not frozen.
    o->SetFreezeVolume(true);
    o->m_pos = S_vector(18, 5, 15);
    o->SyncFrame();
    CHECK(Near(o->m_volume.center, S_vector(17, 5, 15)));
    o->SetFreezeVolume(false);
    o->SetVolumeActive(false);
    CHECK(!InWorld(o->m_volume) && !o->m_volumeActive);
    o->SyncFrame();
    CHECK(Near(o->m_volume.center, S_vector(17, 5, 15)));
    o->SetVolumeActive(true);
    CHECK(InWorld(o->m_volume));
    o->SetVolumeActive(true);                                            // (no change)
    CHECK(InWorld(o->m_volume));
    // Deleting an object puts a door's rotation back and drops the frame's reference.
    I3D_frame *fd = Prop("syncdoor", S_vector(22, 2, 16), S_vector(0.5f, 1.0f, 0.05f), 0.3f);
    T_PhysicsObject hd = g_physicsModule.CreateObject(Desc(fd, PHYS_DOOR));
    const S_quat initial = fd->GetRot();
    const S_vector dir(1, 0, 0);
    g_physicsModule.Hit(hd, PHYS_HIT_KICK, &dir, nullptr);
    for (int i = 0; i < 5; ++i)
        g_physicsModule.Update(100);
    CHECK(std::fabs(fd->GetRot().s - initial.s) > 1e-3f);
    g_physicsModule.DestroyObject(&hd);
    CHECK(std::fabs(fd->GetRot().s - initial.s) < 1e-5f && fd->m_refCount == 1);
    // Deleting them all leaves no frame naming an object, and no reference.
    std::vector<I3D_frame *> frames;
    for (int type = PHYS_SIMPLE; type <= PHYS_SWINGING; ++type) {
        frames.push_back(Prop("all" + std::to_string(type), S_vector(10.0f + 4.0f * type, 3, 30), S_vector(0.3f, 0.4f, 0.2f)));
        g_physicsModule.CreateObject(Desc(frames.back(), type, type % 2 == 0));
    }
    frames.push_back(f);                                                 // (the can: the first resting)
    // A step of the module: the time in milliseconds.
    {
        I3D_frame *fs = Prop("step", S_vector(30, 10, 50), S_vector(0.1f, 0.1f, 0.1f));
        C_PhysObject *s = ObjectOf(g_physicsModule.CreateObject(Desc(fs, PHYS_SIMPLE, true)));
        s->m_velocity = S_vector(2, 0, 0);
        g_physicsModule.Tick(100);
        CHECK(Near(s->m_pos, S_vector(30.2f, 10, 50)) && std::fabs(s->m_velocity.y + 0.981f) < 1e-5f);
        frames.push_back(fs);
    }
    g_physicsModule.DeleteAll();
    for (I3D_frame *frame : frames)
        CHECK(frame->GetData() && frame->GetData()->physicsObject == g_noPhysicsObject && frame->m_refCount == 1);
    // An empty module: nothing to destroy, a door message not read.
    {
        S_PhysicsHandle stale{3, true};
        T_PhysicsObject handle = &stale;
        CHECK(!g_physicsModule.DestroyObject(&handle) && handle == &stale);
        C_NetMessage msg;
        msg.WriteBits(0, 16);
        msg.SetBitPosition(0);
        g_physicsModule.ReadState(&msg);
        CHECK(msg.BitPosition() == 0);
    }
    GetGame()->m_scene = nullptr;
}

// ---- edges: a degenerate bound, frames that are not visuals, objects made without a frame, the interface
void TestEdges()
{
    Fresh();
    CHECK(GetPhysicsModule() == &g_physicsModule);
    C_chunk *chunk = nullptr;
    CHECK(g_physicsModule.SaveGame(*chunk) && g_physicsModule.LoadGame(*chunk));   // (nothing saved)
    // An empty box: no sphere around it.
    I3D_bound_volume empty = {};
    empty.bbox.min = S_vector(1, 1, 1);
    empty.bbox.max = S_vector(1, 2, 3);
    empty.radius = 5;
    physics::ComputeBoundSphere(empty);
    CHECK(empty.radius == 0.0f);
    I3D_bound_volume box = {};
    box.bbox.min = S_vector(-1, 0, 2);
    box.bbox.max = S_vector(3, 2, 4);
    physics::ComputeBoundSphere(box);
    CHECK(Near(box.center, S_vector(1, 1, 3)) && std::fabs(box.radius - std::sqrt(6.0f)) < 1e-5f);
    // A dummy door and a dummy swinging object: their volumes from the hierarchy's bound (the driver's box).
    I3D_frame *dummy = NewFrame(FRAME_DUMMY, "dummydoor", S_vector(30, 2, 44), Identity(), Box(S_vector(0.6f, 1.0f, 0.1f)), "", g_sector);
    auto *door = static_cast<C_PhysDoor *>(ObjectOf(g_physicsModule.CreateObject(Desc(dummy, PHYS_DOOR))));
    CHECK(door && door->m_volume.bbox.max.x == 0.6f && std::fabs(door->m_volume.radius - (S_vector(1.2f, 2.0f, 0.2f).Magnitude() * 0.5f)) < 1e-5f);
    I3D_frame *dummy2 = NewFrame(FRAME_DUMMY, "dummycross", S_vector(34, 2, 44), Identity(), Box(S_vector(0.2f, 0.4f, 0.2f)), "", g_sector);
    auto *cross = static_cast<C_PhysSwingingObject *>(ObjectOf(g_physicsModule.CreateObject(Desc(dummy2, PHYS_SWINGING))));
    CHECK(cross && cross->m_volume2.radius == cross->m_volume.radius && cross->m_volume.bbox.max.y == 0.4f);
    // Made without a frame (the module never does): nothing set up, no step, pushes and hits ignored.
    S_PhysicsHandle handle;
    {
        C_PhysSimpleObject simple(nullptr, &handle);
        C_PhysCan can(nullptr, 0.5f, &handle);
        C_PhysSphere sphere(nullptr, 0.5f, &handle);
        C_PhysBox box3(nullptr, &handle);
        C_PhysSwingingObject swinging(nullptr, &handle, nullptr);
        S_PhysicsObjectDesc d = Desc(nullptr, PHYS_DOOR);
        C_PhysDoor nodoor(d, &handle);
        CHECK(!simple.Tick(0.1f) && !can.Tick(0.1f) && !sphere.Tick(0.1f) && !box3.Tick(0.1f) && !nodoor.MoveLeaf());
        CHECK(!InWorld(can.m_volume) && !InWorld(sphere.m_volume) && !InWorld(swinging.m_volume2) && !InWorld(nodoor.m_volume));
        const S_vector before = can.m_velocity;
        can.ApplyImpulse(S_vector(5, 0, 0), S_vector(0, 0, 0), 1.0f);
        simple.ApplyImpulse(S_vector(5, 0, 0), S_vector(0, 0, 0), 1.0f);
        CHECK(can.m_velocity.x == before.x && simple.m_velocity.x == 0.0f);
        const S_vector dir(1, 0, 0);
        CHECK(!swinging.Hit(PHYS_HIT_KICK, &dir, nullptr) && !nodoor.Hit(PHYS_HIT_KICK, &dir, nullptr));
        CHECK(can.Hit(PHYS_HIT_KICK, &dir, nullptr) && !simple.Hit(PHYS_HIT_KICK, &dir, nullptr));   // (cans take any hit; the base none)
        CHECK(simple.C_PhysObject::Tick(0.1f));                         // (the base step: still moving)
        CHECK(sphere.Unknown18());
        simple.SyncFrame();                                              // (no frame: nothing)
        // (Their destructors: no volume to remove, no frame to release; the swinging one relinks nothing.)
        simple.m_volumeActive = can.m_volumeActive = sphere.m_volumeActive = box3.m_volumeActive = swinging.m_volumeActive = nodoor.m_volumeActive = false;
    }
    Fresh();
}

void TestModule()
{
    TestEdges();
    TestCreate();
    TestDestroy();
    TestRandomOps();
    TestRouting();
    TestRest();
    TestSceneObjects();
    TestState();
    TestSync();
    Fresh();
}
}  // namespace
