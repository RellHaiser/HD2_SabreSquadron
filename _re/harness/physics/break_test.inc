// Part 3 of the physics harness (included by physics_test.cpp): fences breaking and trees falling (C_PhysDoor's Break,
// AddBreakVolumes, KnockOverTree, HideBrokenPart, the destructor of a broken fence). The original's versions load
// models through its own cache, so they cannot run here: the expectations follow its code (0x00407bd0, 0x00406740,
// 0x00407f60 region, 0x004079f0), the reference counts included (Release is the fake's slot 0x00, AddRef inline).
namespace {

int Refs(const I3D_frame *frame) { return frame->m_refCount; }
bool QuatNear(const S_quat &a, const S_quat &b) { return (a.v - b.v).Magnitude() + std::fabs(a.s - b.s) < 1e-6f; }
bool IsOn(const I3D_frame *frame) { return (frame->m_frameFlags & 1u) != 0; }
T_PhysicsObject HandleOf(const I3D_frame *frame)
{
    const S_FrameData *data = const_cast<I3D_frame *>(frame)->GetData();
    return data ? data->physicsObject : nullptr;
}
int Count(const std::vector<I3D_frame *> &list, const I3D_frame *frame)
{
    return static_cast<int>(std::count(list.begin(), list.end(), frame));
}
S_vector RowOf(const S_matrix &m, int row) { return S_vector(m.m[row][0], m.m[row][1], m.m[row][2]); }
// How many of `list` the dynamic grid's cells still hold (pointers compared only: the volumes may be freed).
int RegisteredOf(const S_CollisionVolume *const *list, int n)
{
    const S_CollisionGrid *grid = g_collisionWorld.m_grid;
    int found = 0;
    for (int c = 0; grid && c < grid->cellsX * grid->cellsZ; ++c)
        if (const S_CollisionGridCell *cell = grid->cells[c])
            for (const S_CollisionVolume *v : cell->volumes)
                for (int i = 0; i < n; ++i)
                    found += v == list[i];
    return found;
}

// The break model "plotdira" as the levels' fences have it: the fence with the hole ("plot", a mesh under it), the
// piece that falls out ("dira"), the collision boxes VOLB00 (a dummy) and VOLB01 (a scaled visual) - `volumes` of
// them - all under the model. `withPiece` / `withChild` leave parts out.
struct S_BreakModel {
    I3D_frame *model = nullptr, *plot = nullptr, *dira = nullptr, *mesh = nullptr;
    std::vector<I3D_frame *> volb;
};
S_BreakModel g_lastBreakModel;
void DefineBreakModel(const std::string &file, int volumes, bool withPiece = true, bool withChild = true, bool withFence = true)
{
    g_modelFiles[file] = [=](I3D_frame *model) {
        S_BreakModel m;
        m.model = model;
        if (withFence)
            m.plot = NewFrame(FRAME_VISUAL, "plot", S_vector(0, 0.5f, 0), Identity(), Box(S_vector(1.0f, 0.5f, 0.05f)), "", model);
        if (withFence && withChild)
            m.mesh = NewFrame(FRAME_VISUAL, "plotmesh", S_vector(0.2f, 0, 0), Identity(), Box(S_vector(0.3f, 0.3f, 0.05f)), "", m.plot);
        if (withPiece)
            m.dira = NewFrame(FRAME_VISUAL, "dira", S_vector(0.4f, 0.6f, 0), YawQuat(0.3f), Box(S_vector(0.3f, 0.3f, 0.05f)), "", model);
        for (int i = 0; i < volumes; ++i) {
            const std::string name = "VOLB0" + std::to_string(i);
            const S_vector at(-0.8f + 0.5f * i, 0.4f, 0.1f * i);
            I3D_frame *v;
            if (i % 2 == 0) {
                v = NewFrame(FRAME_DUMMY, name, at, YawQuat(0.2f * i), Box(S_vector(1, 1, 1)), "", model);
                I3D_bbox box;
                box.min = S_vector(-0.2f, -0.4f - 0.1f * i, -0.05f);
                box.max = S_vector(0.3f, 0.5f, 0.05f + 0.01f * i);
                reinterpret_cast<I3D_dummy *>(v)->Field<I3D_bbox>(0x120) = box;
            } else {
                v = NewFrame(FRAME_VISUAL, name, at, YawQuat(-0.1f * i), Box(S_vector(0.25f, 0.45f, 0.06f)), "", model);
            }
            v->SetScale(S_vector(1.0f + 0.5f * i, 2.0f - 0.25f * i, 1.5f));
            v->m_frameFlags |= FRMFLAGS_ROT_DIRTY;
            UpdateTree(v);
            m.volb.push_back(v);
        }
        g_lastBreakModel = m;
    };
}

// A breakable fence at `pos` turned by `yaw`, its break model `model`.
T_PhysicsObject Fence(const S_vector &pos, float yaw, const char *model, I3D_frame **frameOut)
{
    I3D_frame *frame = NewFrame(FRAME_VISUAL, "fence", pos, YawQuat(yaw), Box(S_vector(1.0f, 0.5f, 0.05f)), "", g_sector);
    S_PhysicsObjectDesc d = Desc(frame, PHYS_DOOR);
    d.door.breakable = true;
    d.door.breakModel = model;
    d.door.angle = 1.5707964f;
    *frameOut = frame;
    return g_physicsModule.CreateObject(d);
}

void TestBreakOne(int volumes, bool withScene, bool withChild)
{
    Fresh();
    g_sceneAdded.clear();
    g_created.clear();
    g_opened.clear();
    if (withScene)
        GetGame()->m_scene = reinterpret_cast<I3D_scene *>(g_sceneBytes);
    DefineBreakModel("plotdira", volumes, true, withChild);
    I3D_frame *frame;
    const float yaw = 0.4f + 0.3f * volumes;
    T_PhysicsObject h = Fence(S_vector(10, 1, 12), yaw, "plotdira", &frame);
    auto *door = static_cast<C_PhysDoor *>(ObjectOf(h));
    CHECK(door && door->m_breakable);
    CHECK(g_breakables.size() == 1 && g_breakables[0].frame == frame);
    CHECK((door->m_volume.flags & COLLISION_VOLUME_SHOOT_THROUGH) != 0 && InWorld(door->m_volume));
    CHECK(Refs(frame) == 2);                                           // (the level's, the object's)
    const S_quat initialRot = frame->GetRot();
    const S_matrix doorWorld = frame->GetMatrix();

    const S_vector dir = S_vector(0.3f, 0, -1).Normalized();
    CHECK(g_physicsModule.Hit(h, PHYS_HIT_BREAK, &dir, nullptr));
    door = static_cast<C_PhysDoor *>(ObjectOf(h));
    CHECK(door && !door->m_breakable);
    CHECK(g_opened.size() == 1 && g_opened[0] == "plotdira");
    const S_BreakModel m = g_lastBreakModel;
    CHECK(m.model && Refs(m.model) == 0);                              // (released)
    CHECK(!InWorld(door->m_volume) && !door->m_volumeActive);

    // The model was placed on the fence (its z axis the fence's), the parts moved out of it beside the fence.
    CHECK(Near(m.model->GetWorldPos(), RowOf(doorWorld, 3)));
    CHECK(Near(RowOf(m.model->GetMatrix(), 2), RowOf(doorWorld, 2).Normalized(), 1e-3f));
    CHECK(m.dira->m_parent == g_sector && m.plot->m_parent == g_sector);
    CHECK(Fake(m.dira)->removedFromModel == 1 && Fake(m.plot)->removedFromModel == 1);
    if (withChild) {
        CHECK(Fake(m.mesh)->removedFromModel == 1);
        CHECK(m.mesh->m_parent == m.plot);                              // (still under the fence: not relinked)
    }
    // (Worlds kept through the relink: the piece where the model put it.)
    CHECK(Near(m.dira->GetWorldPos(), S_vector(0.4f, 0.6f, 0) * m.model->GetMatrix(), 1e-4f));
    if (withScene) {
        CHECK(g_sceneAdded.size() == (withChild ? 3u : 2u));
        CHECK(g_sceneAdded.size() >= 2 && g_sceneAdded[0] == m.dira && g_sceneAdded[1] == m.plot);
        CHECK(!withChild || (g_sceneAdded.size() == 3 && g_sceneAdded[2] == m.mesh));
    } else {
        CHECK(g_sceneAdded.empty());
    }

    // This object is the fence with the hole now; the old frame hidden, kept for the destructor.
    CHECK(door->m_frame == m.plot && door->m_originalFrame == frame);
    CHECK(!IsOn(frame) && HandleOf(frame) == g_noPhysicsObject && HandleOf(m.plot) == h);
    CHECK(Refs(frame) == 1);
    CHECK(Refs(m.plot) == (withScene ? 2 : 3));
    CHECK(!withChild || Refs(m.mesh) == (withScene ? 1 : 2));     // (released only with a scene)
    {
        const S_quat q = m.plot->GetRot();
        CHECK(QuatNear(door->m_closedRot, q));
    }

    // The collision boxes: out of the model, beside the fence, in the world as doors, in the boxes' unscaled space.
    CHECK(static_cast<int>(door->m_breakVolumes.size()) == volumes);
    for (int i = 0; i < volumes && i < static_cast<int>(door->m_breakVolumes.size()); ++i) {
        I3D_frame *v = m.volb[i];
        const S_BreakVolume *b = door->m_breakVolumes[i];
        CHECK(b->volume.frame == v && v->m_parent == g_sector && Fake(v)->removedFromModel == 1);
        CHECK(InWorld(b->volume) && b->volume.type == COLLISION_TYPE_DOOR && b->volume.flags == COLLISION_SHAPE_BOX);
        CHECK(HandleOf(v) == h && Refs(v) == 2);
        CHECK(b->volume.matrix == &b->matrix && b->volume.invMatrix == &b->invMatrix);
        const S_vector scale = v->GetScale();
        const S_matrix &w = v->GetMatrix();
        I3D_bbox box;
        if (v->GetType() == FRAME_DUMMY)
            box = reinterpret_cast<I3D_dummy *>(v)->GetBBox();
        else
            box = v->m_bound.bbox;
        const S_vector center = (box.min + box.max) * 0.5f;
        CHECK(Near(b->volume.bbox.min, S_vector(box.min.x * scale.x, box.min.y * scale.y, box.min.z * scale.z)));
        CHECK(Near(b->volume.bbox.max, S_vector(box.max.x * scale.x, box.max.y * scale.y, box.max.z * scale.z)));
        for (int r = 0; r < 3; ++r) {
            const float s = r == 0 ? scale.x : r == 1 ? scale.y : scale.z;
            CHECK(Near(RowOf(b->matrix, r), RowOf(w, r) * (1.0f / s), 1e-4f));
        }
        CHECK(Near(RowOf(b->matrix, 3), RowOf(w, 3)));
        S_matrix product = b->matrix * b->invMatrix;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK(std::fabs(product.m[r][c] - (r == c ? 1.0f : 0.0f)) < 1e-4f);
        CHECK(Near(S_vector(b->volume.center.x, b->volume.center.y, b->volume.center.z), center * w, 1e-4f));
        const S_vector ws = v->GetWorldScale();
        const float radius = v->GetType() == FRAME_DUMMY ? (box.max.y - box.min.y) * 0.5f
                                                          : (box.max - box.min).Magnitude() * 0.5f;
        CHECK(std::fabs(b->volume.radius - std::max(ws.x, std::max(ws.y, ws.z)) * radius) < 1e-4f);
    }

    // The piece flies off: a door kicked in the hit direction, free of the scene's references.
    T_PhysicsObject piece = HandleOf(m.dira);
    auto *flying = static_cast<C_PhysDoor *>(ObjectOf(piece));
    CHECK(flying && flying->m_frame == m.dira && piece != h);
    CHECK(flying && flying->m_moving && !flying->m_rattling && Near(flying->m_hitDir, dir));
    CHECK(flying && (flying->m_volume.flags & COLLISION_VOLUME_SHOOT_THROUGH) != 0);
    CHECK(piece && piece->active);
    CHECK(door->m_brokenPart == m.dira);
    CHECK(Refs(m.dira) == (withScene ? 1 : 2));
    CHECK(g_physicsModule.m_doors.size() == 2);

    // Broken once only.
    CHECK(g_physicsModule.Hit(h, PHYS_HIT_BREAK, &dir, nullptr));
    CHECK(g_opened.size() == 1 && door->m_frame == m.plot);

    door->HideBrokenPart();
    CHECK(!IsOn(m.dira));
    CHECK(Fake(m.dira)->setOn >= 1);

    // Destroyed: the boxes leave the world, the old fence comes back as it was.
    T_PhysicsObject hd = h;
    const S_CollisionVolume *volumesOf[8] = {};
    for (int i = 0; i < volumes && i < 8; ++i)
        volumesOf[i] = &door->m_breakVolumes[i]->volume;
    std::vector<I3D_frame *> volb = m.volb;
    CHECK(g_physicsModule.DestroyObject(&hd));
    CHECK(IsOn(frame));
    const S_quat q = frame->GetRot();
    CHECK(QuatNear(q, initialRot));
    CHECK(Refs(frame) == 1);
    CHECK(Refs(m.plot) == (withScene ? 0 : 1));
    for (I3D_frame *v : volb)
        CHECK(Refs(v) == 1);
    CHECK(RegisteredOf(volumesOf, volumes < 8 ? volumes : 8) == 0);
    CHECK(ListsConsistent());
    g_physicsModule.DestroyObject(&piece);
    CHECK(Refs(m.dira) == (withScene ? 0 : 1));
    GetGame()->m_scene = nullptr;
}

void TestBreakFailures()
{
    // No model file: the volume is gone, nothing else changes.
    Fresh();
    g_opened.clear();
    I3D_frame *frame;
    T_PhysicsObject h = Fence(S_vector(30, 1, 30), 0.0f, "nofile", &frame);
    const S_vector dir(0, 0, 1);
    const size_t created = g_created.size();
    CHECK(g_physicsModule.Hit(h, PHYS_HIT_BREAK, &dir, nullptr));
    auto *door = static_cast<C_PhysDoor *>(ObjectOf(h));
    CHECK(g_opened.size() == 1 && g_opened[0] == "nofile");
    CHECK(g_created.size() == created + 1 && Refs(g_created.back()) == 0);
    CHECK(door->m_frame == frame && IsOn(frame) && !InWorld(door->m_volume) && !door->m_breakable);
    CHECK(door->m_originalFrame == nullptr && door->m_brokenPart == nullptr && door->m_breakVolumes.empty());
    CHECK(g_physicsModule.m_doors.size() == 1);

    // A model without the piece, or without the fence: released, nothing taken from it.
    for (int which = 0; which < 2; ++which) {
        Fresh();
        DefineBreakModel("partial", 2, which == 1, true, which == 0);
        h = Fence(S_vector(30, 1, 30), 0.0f, "partial", &frame);
        CHECK(g_physicsModule.Hit(h, PHYS_HIT_BREAK, &dir, nullptr));
        door = static_cast<C_PhysDoor *>(ObjectOf(h));
        const S_BreakModel m = g_lastBreakModel;
        CHECK(Refs(m.model) == 0 && door->m_frame == frame && door->m_breakVolumes.empty());
        CHECK(!InWorld(door->m_volume) && g_physicsModule.m_doors.size() == 1);
        CHECK(!m.plot || Fake(m.plot)->removedFromModel == 0);
        CHECK(!m.dira || Fake(m.dira)->removedFromModel == 0);
    }

    // A fence that is not breakable ignores the break; other hits do not break it.
    Fresh();
    g_opened.clear();
    frame = NewFrame(FRAME_VISUAL, "fence", S_vector(30, 1, 30), Identity(), Box(S_vector(1.0f, 0.5f, 0.05f)), "", g_sector);
    S_PhysicsObjectDesc d = Desc(frame, PHYS_DOOR);
    d.door.breakModel = "plotdira";
    h = g_physicsModule.CreateObject(d);
    CHECK(g_breakables.empty());
    CHECK(g_physicsModule.Hit(h, PHYS_HIT_BREAK, &dir, nullptr));
    door = static_cast<C_PhysDoor *>(ObjectOf(h));
    CHECK(g_opened.empty() && InWorld(door->m_volume) && !door->m_moving);
    Fresh();
    DefineBreakModel("plotdira", 1);
    g_opened.clear();
    h = Fence(S_vector(30, 1, 30), 0.0f, "plotdira", &frame);
    CHECK(g_physicsModule.Hit(h, PHYS_HIT_KICK, &dir, nullptr));
    door = static_cast<C_PhysDoor *>(ObjectOf(h));
    CHECK(g_opened.empty() && door->m_breakable && door->m_moving);
    door->HideBrokenPart();                                             // (nothing broken: nothing to hide)
    Fresh();
}

// ---- trees: the damaged model takes the owner model's place, its part falls over
struct S_Tree {
    I3D_frame *owner, *part, *leaf;
    T_PhysicsObject handle;
};
S_Tree Tree(const char *file, const S_vector &pos, int ownerType = FRAME_MODEL)
{
    S_Tree t;
    t.owner = NewFrame(static_cast<E_I3D_FRAME_TYPE>(ownerType), "oak01", pos, YawQuat(0.7f), Box(S_vector(2, 4, 2)), "", g_sector);
    static std::string fileName;
    fileName = file;
    reinterpret_cast<I3D_model *>(t.owner)->Field<const char *>(0x134) = fileName.c_str();
    t.owner->SetScale(S_vector(1.2f, 1.2f, 1.2f));
    t.owner->m_frameFlags |= FRMFLAGS_ROT_DIRTY;
    t.part = NewFrame(FRAME_DUMMY, "trunk", S_vector(0, 0, 0), Identity(), Box(S_vector(0.5f, 3, 0.5f)), "", t.owner);
    t.leaf = NewFrame(FRAME_VISUAL, "trunkmesh", S_vector(0, 1.5f, 0), Identity(), Box(S_vector(0.5f, 1.5f, 0.5f)), "", t.part);
    t.leaf->m_ownerModel = t.owner;
    UpdateTree(t.owner);
    S_PhysicsObjectDesc d = Desc(t.leaf, PHYS_DOOR);
    d.door.tree = true;
    d.door.freeSwing = true;
    d.door.angle = 1.5707964f;
    t.handle = g_physicsModule.CreateObject(d);
    return t;
}
I3D_frame *g_damagedPart;
void DefineDamaged(const std::string &file, bool withPart)
{
    g_modelFiles[file] = [=](I3D_frame *model) {
        g_damagedPart = nullptr;
        if (withPart) {
            I3D_frame *p = NewFrame(FRAME_DUMMY, "trunkmesh", S_vector(0, 0, 0), Identity(), Box(S_vector(0.5f, 3, 0.5f)), "", model);
            g_damagedPart = NewFrame(FRAME_VISUAL, "fallen", S_vector(0, 1.5f, 0), Identity(), Box(S_vector(0.5f, 1.5f, 0.5f)), "", p);
            NewFrame(FRAME_VISUAL, "second", S_vector(0, 2.5f, 0), Identity(), Box(S_vector(0.2f, 0.2f, 0.2f)), "", p);
        }
    };
}

void TestTrees()
{
    const S_vector dir = S_vector(1, 0, 0.5f).Normalized();
    for (int withPart = 0; withPart < 2; ++withPart) {
        Fresh();
        GetGame()->m_scene = reinterpret_cast<I3D_scene *>(g_sceneBytes);
        g_sceneAdded.clear();
        g_sceneRemoved.clear();
        g_opened.clear();
        DefineDamaged("doak", withPart == 1);
        S_Tree t = Tree("maps\\trees\\oak.i3d", S_vector(12, 1, 30));
        const size_t created = g_created.size();
        const size_t doors = g_physicsModule.m_doors.size();
        T_PhysicsObject h = t.handle;
        // Walked into: nothing.
        CHECK(g_physicsModule.Hit(h, PHYS_HIT_PUSH, &dir, nullptr));
        CHECK(!static_cast<C_PhysDoor *>(ObjectOf(h))->m_moving && g_opened.empty());
        // Kicked: swapped for "d" + the file's name.
        const S_vector ownerPos = t.owner->GetPos();
        const S_quat ownerRot = t.owner->GetRot();
        CHECK(g_physicsModule.Hit(h, PHYS_HIT_KICK, &dir, nullptr));
        CHECK(g_opened.size() == 1 && g_opened[0] == "doak");
        CHECK(g_created.size() == created + 1);
        I3D_frame *damaged = g_created.back();
        CHECK(damaged->m_parent == g_sector && Near(damaged->GetPos(), ownerPos));
        const S_quat dq = damaged->GetRot();
        CHECK(QuatNear(dq, ownerRot));
        CHECK(Near(damaged->GetScale(), S_vector(1.2f, 1.2f, 1.2f)));
        CHECK(std::strcmp(damaged->GetShortName(), "oak01") == 0 && IsOn(damaged));
        CHECK(t.owner->m_parent == nullptr);
        CHECK(Count(g_sceneRemoved, t.owner) == 1 && Count(g_sceneAdded, damaged) == 1);
        CHECK(Refs(t.owner) == 0 && Refs(damaged) == 0);
        // This object is gone; the damaged part fell over in the hit direction.
        CHECK(h->index == -1 && HandleOf(t.leaf) == g_noPhysicsObject);
        if (withPart) {
            T_PhysicsObject fallen = HandleOf(g_damagedPart);
            auto *f = static_cast<C_PhysDoor *>(ObjectOf(fallen));
            CHECK(f && f->m_frame == g_damagedPart && f->m_moving && f->m_freeSwing && fallen->active);
            CHECK(f && Near(f->m_hitDir, dir) && Near(f->m_axis, S_vector(-dir.z, 0, dir.x).Normalized()));
            CHECK(f && (f->m_volume.flags & COLLISION_VOLUME_SHOOT_THROUGH) != 0);
            CHECK(g_physicsModule.m_doors.size() == doors + 1);
        } else {
            CHECK(g_physicsModule.m_doors.size() == doors);
            CHECK(g_physicsModule.m_doors.empty() || g_physicsModule.m_doors.back() == nullptr);
        }
        CHECK(ListsConsistent());
    }

    // The damaged model missing: not swapped, the tree falls itself.
    Fresh();
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(g_sceneBytes);
    g_sceneRemoved.clear();
    g_opened.clear();
    S_Tree t = Tree("C:\\x\\pine.4ds", S_vector(12, 1, 30));
    CHECK(g_physicsModule.Hit(t.handle, PHYS_HIT_KICK, &dir, nullptr));
    CHECK(g_opened.size() == 1 && g_opened[0] == "dpine");
    CHECK(Refs(g_created.back()) == 0);
    auto *door = static_cast<C_PhysDoor *>(ObjectOf(t.handle));
    CHECK(door && door->m_moving && door->m_frame == t.leaf && t.owner->m_parent == g_sector && g_sceneRemoved.empty());
    CHECK(door && Near(door->m_axis, S_vector(-dir.z, 0, dir.x).Normalized()));

    // The owner not a model: not swapped, nothing opened.
    Fresh();
    g_opened.clear();
    t = Tree("oak", S_vector(12, 1, 30), FRAME_DUMMY);
    CHECK(g_physicsModule.Hit(t.handle, PHYS_HIT_KICK, &dir, nullptr));
    CHECK(g_opened.empty() && static_cast<C_PhysDoor *>(ObjectOf(t.handle))->m_moving);

    // No owner at all.
    Fresh();
    g_opened.clear();
    t = Tree("oak", S_vector(12, 1, 30));
    t.leaf->m_ownerModel = nullptr;
    CHECK(g_physicsModule.Hit(t.handle, PHYS_HIT_KICK, &dir, nullptr));
    CHECK(g_opened.empty() && static_cast<C_PhysDoor *>(ObjectOf(t.handle))->m_moving);

    // No scene: the swap fails half way (the original's behaviour: the damaged model linked, the owner unlinked).
    Fresh();
    g_opened.clear();
    DefineDamaged("doak", true);
    t = Tree("oak.i3d", S_vector(12, 1, 30));
    CHECK(!g_physicsModule.Hit(t.handle, PHYS_HIT_KICK, &dir, nullptr));
    CHECK(g_opened.size() == 1 && t.owner->m_parent == nullptr && g_created.back()->m_parent == g_sector);
    door = static_cast<C_PhysDoor *>(ObjectOf(t.handle));
    CHECK(door && door->m_frame == t.leaf);
    Fresh();
}

// A world just loaded has no dynamic cells: MoveVolume files a volume only into a cell that exists, so here only
// AddVolume puts one in.
void ReloadWorld()
{
    g_collisionWorld.Unload(false);
    LoadGround();
}
void TestFreshWorld()
{
    Fresh();
    ReloadWorld();
    for (int type = PHYS_SIMPLE; type <= PHYS_SWINGING; ++type) {
        I3D_frame *f = Prop("fresh" + std::to_string(type), S_vector(6.0f + 8.0f * type, 3, 50), S_vector(0.3f, 0.5f, 0.2f));
        C_PhysObject *o = ObjectOf(g_physicsModule.CreateObject(Desc(f, type, true)));
        const bool volume = type != PHYS_SIMPLE && type != PHYS_FALLING;
        CHECK(o && InWorld(o->m_volume) == volume);
        if (o && type == PHYS_SWINGING)
            CHECK(InWorld(static_cast<C_PhysSwingingObject *>(o)->m_volume2));
    }

    // Back in the world after being out of it.
    Fresh();
    ReloadWorld();
    T_PhysicsObject h = g_physicsModule.CreateObject(Desc(Prop("again", S_vector(30, 3, 40), S_vector(0.2f, 0.2f, 0.2f)), PHYS_CAN, true));
    C_PhysObject *can = ObjectOf(h);
    can->SetVolumeActive(false);
    ReloadWorld();
    can->SetVolumeActive(true);
    CHECK(InWorld(can->m_volume));

    // A swinging object settled after a hit; its frame relinked to its parent when it is deleted.
    Fresh();
    ReloadWorld();
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(g_sceneBytes);
    I3D_frame *hook = NewFrame(FRAME_DUMMY, "hook", S_vector(20, 4, 44), YawQuat(0.5f), Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_sector);
    I3D_frame *lamp = NewFrame(FRAME_VISUAL, "lamp", S_vector(0, -0.5f, 0), Identity(), Box(S_vector(0.2f, 0.3f, 0.2f)), "", hook);
    const S_vector lampAt = lamp->GetWorldPos();
    h = g_physicsModule.CreateObject(Desc(lamp, PHYS_SWINGING, false));
    auto *sw = static_cast<C_PhysSwingingObject *>(ObjectOf(h));
    CHECK(lamp->m_parent == g_sector && Near(lamp->GetWorldPos(), lampAt));
    const S_vector dir(1, 0, 0);
    CHECK(g_physicsModule.Hit(h, PHYS_HIT_KICK, &dir, nullptr));
    CHECK(!InWorld(sw->m_volume) && !InWorld(sw->m_volume2));
    ReloadWorld();
    for (int i = 0; i < 400 && sw->m_swinging; ++i)
        g_physicsModule.Update(50);
    CHECK(!sw->m_swinging && InWorld(sw->m_volume) && InWorld(sw->m_volume2));
    g_physicsModule.DestroyObject(&h);
    CHECK(lamp->m_parent == hook && Near(lamp->GetWorldPos(), lampAt, 1e-3f));
    GetGame()->m_scene = nullptr;

    // A fence broken where nothing has been: its collision boxes go in.
    Fresh();
    DefineBreakModel("plotdira", 3);
    I3D_frame *frame;
    h = Fence(S_vector(50, 1, 10), 0.3f, "plotdira", &frame);
    ObjectOf(h)->SetVolumeActive(false);
    ReloadWorld();
    const S_vector hitDir(0, 0, 1);
    g_physicsModule.Hit(h, PHYS_HIT_BREAK, &hitDir, nullptr);
    auto *door = static_cast<C_PhysDoor *>(ObjectOf(h));
    CHECK(door->m_breakVolumes.size() == 3);
    for (const S_BreakVolume *b : door->m_breakVolumes)
        CHECK(InWorld(b->volume));
    Fresh();
    ReloadWorld();
}

// A fence kicked open, then broken: deleted, its first frame comes back closed.
void TestKickedThenBroken()
{
    Fresh();
    DefineBreakModel("plotdira", 1);
    I3D_frame *frame;
    T_PhysicsObject h = Fence(S_vector(14, 1, 52), 0.7f, "plotdira", &frame);
    const S_quat initial = frame->GetRot();
    const S_vector dir(0.2f, 0, 1);
    g_physicsModule.Hit(h, PHYS_HIT_KICK, &dir, nullptr);
    for (int i = 0; i < 6; ++i)
        g_physicsModule.Update(100);
    CHECK(!QuatNear(frame->GetRot(), initial));
    g_physicsModule.Hit(h, PHYS_HIT_BREAK, &dir, nullptr);
    CHECK(static_cast<C_PhysDoor *>(ObjectOf(h))->m_originalFrame == frame);
    g_physicsModule.DestroyObject(&h);
    CHECK(QuatNear(frame->GetRot(), initial) && IsOn(frame));
    Fresh();
}

void TestBreaking()
{
    TestKickedThenBroken();
    TestBreakOne(0, true, true);
    TestBreakOne(1, true, false);
    TestBreakOne(3, true, true);
    TestBreakOne(2, false, true);
    TestBreakOne(4, false, false);
    TestBreakFailures();
    TestTrees();
    TestFreshWorld();
    g_modelFiles.clear();
}
}  // namespace
