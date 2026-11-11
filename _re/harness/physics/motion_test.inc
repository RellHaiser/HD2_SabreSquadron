// Part 2 of the physics harness (included by physics_test.cpp): every object's step against the original
// executable's (..\collision\original_exe.h).
//
// One step at a time from the same state: the object and its frame are copied, the port's step runs, both are put
// back, the object is given the original's vtable (its own virtual calls inside go to the original's: Rotate,
// ApplyGravity) and the original's step runs on the same memory, against a copy of the port's collision world at
// the original's g_collisionWorld (its dynamic grid with MSVC6's vectors). Every field the step writes and the frame
// must agree; then the port's result is kept and the trajectory goes on (the module's SyncFrame between steps, the
// port's). The original never changes what is in the world (its allocator was never started): a step of the port's
// that adds or removes a volume is counted and its original not run; doors' and swinging objects' volumes are out of
// the world before a hit is compared.
namespace {

const uint32_t kOrigWorld = 0x008cb480;
struct S_OrigCode {
    uint32_t vtable, tick, impulse, hit;
};
S_OrigCode OrigCode(int type)
{
    switch (type) {
    case PHYS_SIMPLE: return {0x0080f5f0, 0x0040e160, 0x0040b030, 0x0040b790};
    case PHYS_SPHERE: return {0x0080f624, 0x0040edd0, 0x0040b030, 0x0040b790};
    case PHYS_CAN: return {0x0080f608, 0x0040e3b0, 0x0040eb10, 0x0040eb00};
    case PHYS_BOX: return {0x0080f5d8, 0x0040bee0, 0x0040b030, 0x0040b790};
    case PHYS_FALLING: return {0x0080f5b0, 0x0040b910, 0x0040b030, 0x0040b790};
    case PHYS_DOOR: return {0x0080f578, 0x004070c0, 0x004094d0, 0x00408780};
    default: return {0x0080f55c, 0x00405ca0, 0x004066a0, 0x00406390};
    }
}
typedef bool(__attribute__((thiscall)) * T_OrigTick)(void *, float);
typedef void(__attribute__((thiscall)) * T_OrigImpulse)(void *, const S_vector &, const S_vector &, float);
typedef bool(__attribute__((thiscall)) * T_OrigHit)(void *, int, const S_vector *, void *);

// The original's objects (the offsets in physics.h: its vectors 16-byte aligned) against the port's (12-byte
// vectors, packed): each field's place in both, by type. The original runs on a copy in its own layout.
struct S_Field {
    uint32_t orig;
    size_t port, size;
};
#define FIELD(cls, member, at) S_Field{at, offsetof(cls, member), sizeof(cls::member)}
std::vector<S_Field> Fields(int type)
{
    std::vector<S_Field> f = {
        FIELD(C_PhysObject, m_velocity, 0x10), FIELD(C_PhysObject, m_angularVelocity, 0x20), FIELD(C_PhysObject, m_pos, 0x30),
        FIELD(C_PhysObject, m_rot, 0x40), FIELD(C_PhysObject, m_invMatrix, 0x50), FIELD(C_PhysObject, m_matrix, 0x90),
        FIELD(C_PhysObject, m_volume, 0xD0), FIELD(C_PhysObject, m_mass, 0x130), FIELD(C_PhysObject, m_frame, 0x140),
        FIELD(C_PhysObject, m_unknown144, 0x144), FIELD(C_PhysObject, m_collideWithBoxes, 0x145), FIELD(C_PhysObject, m_materialId, 0x148),
        FIELD(C_PhysObject, m_callback, 0x14C), FIELD(C_PhysObject, m_handle, 0x150), FIELD(C_PhysObject, m_callbackContext, 0x154),
        FIELD(C_PhysObject, m_resting, 0x158), FIELD(C_PhysObject, m_type, 0x15C), FIELD(C_PhysObject, m_volumeActive, 0x160),
        FIELD(C_PhysObject, m_freezeVolume, 0x161), FIELD(C_PhysObject, m_state, 0x164),
    };
    const std::vector<S_Field> more =
        type == PHYS_CAN      ? std::vector<S_Field>{FIELD(C_PhysCan, m_radius, 0x170)}
        : type == PHYS_SPHERE ? std::vector<S_Field>{FIELD(C_PhysSphere, m_radius, 0x170)}
        : type == PHYS_FALLING
            ? std::vector<S_Field>{FIELD(C_PhysFallingObject, m_sliding, 0x170), FIELD(C_PhysFallingObject, m_groundChecked, 0x171),
                                   FIELD(C_PhysFallingObject, m_groundY, 0x174)}
        : type == PHYS_BOX
            ? std::vector<S_Field>{FIELD(C_PhysBox, m_axis1Start, 0x170), FIELD(C_PhysBox, m_axis1End, 0x180), FIELD(C_PhysBox, m_axis2Start, 0x190),
                                   FIELD(C_PhysBox, m_axis2End, 0x1A0), FIELD(C_PhysBox, m_contact, 0x1B0), FIELD(C_PhysBox, m_rollAxis, 0x1C0)}
        : type == PHYS_DOOR
            ? std::vector<S_Field>{FIELD(C_PhysDoor, m_center, 0x170), FIELD(C_PhysDoor, m_axis, 0x180), FIELD(C_PhysDoor, m_edge1, 0x190),
                                   FIELD(C_PhysDoor, m_edge2, 0x1A0), FIELD(C_PhysDoor, m_hitDir, 0x1B0), FIELD(C_PhysDoor, m_openRot1, 0x1C0),
                                   FIELD(C_PhysDoor, m_openRot2, 0x1D0), FIELD(C_PhysDoor, m_closedRot, 0x1E0), FIELD(C_PhysDoor, m_initialRot, 0x1F0),
                                   FIELD(C_PhysDoor, m_moving, 0x200), FIELD(C_PhysDoor, m_freeSwing, 0x201), FIELD(C_PhysDoor, m_rattling, 0x202),
                                   FIELD(C_PhysDoor, m_tree, 0x203), FIELD(C_PhysDoor, m_breakable, 0x204), FIELD(C_PhysDoor, m_breakModel, 0x205),
                                   FIELD(C_PhysDoor, m_phase, 0x20C), FIELD(C_PhysDoor, m_amplitude, 0x210), FIELD(C_PhysDoor, m_originalFrame, 0x214),
                                   FIELD(C_PhysDoor, m_brokenPart, 0x218), FIELD(C_PhysDoor, m_damping, 0x21C), FIELD(C_PhysDoor, m_speed, 0x220),
                                   FIELD(C_PhysDoor, m_openAngle, 0x224), FIELD(C_PhysDoor, m_angle, 0x228), FIELD(C_PhysDoor, m_swingState, 0x22C)}
        : type == PHYS_SWINGING
            ? std::vector<S_Field>{FIELD(C_PhysSwingingObject, m_swingRot1, 0x170), FIELD(C_PhysSwingingObject, m_swingRot2, 0x180),
                                   FIELD(C_PhysSwingingObject, m_restRot, 0x190), FIELD(C_PhysSwingingObject, m_matrix2, 0x1A0),
                                   FIELD(C_PhysSwingingObject, m_invMatrix2, 0x1E0), FIELD(C_PhysSwingingObject, m_swinging, 0x220),
                                   FIELD(C_PhysSwingingObject, m_phase, 0x224), FIELD(C_PhysSwingingObject, m_amplitude, 0x228),
                                   FIELD(C_PhysSwingingObject, m_volume2, 0x230), FIELD(C_PhysSwingingObject, m_parent, 0x290),
                                   FIELD(C_PhysSwingingObject, m_swingState, 0x294)}
            : std::vector<S_Field>{};
    f.insert(f.end(), more.begin(), more.end());
    return f;
}
#undef FIELD
// The original's copy of a port object (its vtable, the fields at their places), and the fields read back.
alignas(16) uint8_t g_origObject[0x300];
void ToOriginal(const uint8_t *port, int type)
{
    std::memset(g_origObject, 0, sizeof g_origObject);
    *reinterpret_cast<uint32_t *>(g_origObject) = OrigCode(type).vtable;
    for (const S_Field &f : Fields(type))
        std::memcpy(g_origObject + f.orig, port + f.port, f.size);
}
void FromOriginal(uint8_t *port, int type)
{
    for (const S_Field &f : Fields(type))
        std::memcpy(port + f.port, g_origObject + f.orig, f.size);
}

size_t ObjectSize(int type)
{
    switch (type) {
    case PHYS_SIMPLE: return sizeof(C_PhysSimpleObject);
    case PHYS_SPHERE: return sizeof(C_PhysSphere);
    case PHYS_CAN: return sizeof(C_PhysCan);
    case PHYS_BOX: return sizeof(C_PhysBox);
    case PHYS_FALLING: return sizeof(C_PhysFallingObject);
    case PHYS_DOOR: return offsetof(C_PhysDoor, m_breakVolumes);         // (the vector's own layout differs)
    default: return sizeof(C_PhysSwingingObject);
    }
}

// ---- the original's world: a copy of the port's, its dynamic grid with MSVC6's vectors (or none)
struct S_MsvcCell {
    int x, z;
    uint32_t allocator;
    S_CollisionVolume **first, **last, **end;
};
// The object's own volumes in the cells are given as the original's copy of them (it ignores its volume by address).
const C_PhysObject *g_diffObject;
template <class F> auto WithOriginalWorld(bool grid, F f) -> decltype(f())
{
    std::vector<S_MsvcCell> cells;
    std::vector<S_MsvcCell *> table;
    std::vector<std::vector<S_CollisionVolume *>> lists;
    S_CollisionGrid copy{};
    const S_CollisionGrid *g = grid ? g_collisionWorld.m_grid : nullptr;
    if (g) {
        copy = *g;
        const int n = g->cellsX * g->cellsZ;
        cells.resize(n);
        table.assign(n, nullptr);
        lists.resize(n);
        const S_CollisionVolume *own1 = g_diffObject ? &g_diffObject->m_volume : nullptr;
        const S_CollisionVolume *own2 = g_diffObject && g_diffObject->GetType() == PHYS_SWINGING
                                            ? &static_cast<const C_PhysSwingingObject *>(g_diffObject)->m_volume2 : nullptr;
        for (int i = 0; i < n; ++i)
            if (S_CollisionGridCell *c = g->cells[i]) {
                lists[i] = c->volumes;
                for (S_CollisionVolume *&v : lists[i])
                    if (own1 && v == own1)
                        v = reinterpret_cast<S_CollisionVolume *>(g_origObject + 0xD0);
                    else if (own2 && v == own2)
                        v = reinterpret_cast<S_CollisionVolume *>(g_origObject + 0x230);
                S_CollisionVolume **data = lists[i].data();
                cells[i] = {c->x, c->z, 0, data, data + lists[i].size(), data + lists[i].size()};
                table[i] = &cells[i];
            }
        copy.cells = reinterpret_cast<S_CollisionGridCell **>(table.data());
    }
    // Up to the grid the layouts agree; after it the original's vectors are 16 bytes (the port's 12): its free and
    // empty cell lists (+0x3F0, +0x400) empty, nothing ignored (+0x410..+0x42C), by its own offsets.
    auto *world = OriginalAt<uint8_t *>(kOrigWorld);
    std::memcpy(world, static_cast<const void *>(&g_collisionWorld), 0x3EC);
    *reinterpret_cast<const S_CollisionGrid **>(world + 0x3EC) = g ? &copy : nullptr;
    std::memset(world + 0x3F0, 0, 0x42C - 0x3F0);
    return f();
}

// ---- comparing two states of an object (byte copies) and its frame
struct S_State {
    alignas(16) uint8_t object[0x300];
    alignas(16) uint8_t frame[0x400];
    bool result = false;
    int callbacks = 0;
};
void Take(S_State &s, const C_PhysObject *o, size_t size)
{
    std::memcpy(s.object, static_cast<const void *>(o), size);
    std::memcpy(s.frame, o->GetFrame(), sizeof s.frame);
}
void Put(const S_State &s, C_PhysObject *o, size_t size)
{
    I3D_frame *frame = o->GetFrame();
    std::memcpy(static_cast<void *>(o), s.object, size);
    std::memcpy(static_cast<void *>(frame), s.frame, sizeof s.frame);
}
template <class T> const T &As(const S_State &s) { return *reinterpret_cast<const T *>(s.object); }
const I3D_frame &FrameOf(const S_State &s) { return *reinterpret_cast<const I3D_frame *>(s.frame); }

float g_tol = 2e-4f;
bool NearV(const S_vector &a, const S_vector &b) { return (a - b).Magnitude() <= g_tol * std::max(1.0f, a.Magnitude()); }
bool NearF(float a, float b) { return std::fabs(a - b) <= g_tol * std::max(1.0f, std::fabs(a)); }
bool NearQ(const S_quat &a, const S_quat &b)
{
    const bool same = NearF(a.s, b.s) && NearV(a.v, b.v);
    const bool negated = NearF(a.s, -b.s) && NearV(a.v, -b.v);
    return same || negated;
}
bool NearM(const S_matrix &a, const S_matrix &b)
{
    for (int i = 0; i < 4; ++i)
        if (!NearV(a(i), b(i)))
            return false;
    return true;
}
// SyncFrame's comparison: the volume's matrices and center too.
bool g_compareVolume;
// (ctor_test.inc: the original's world calls, logged.)
void PatchOriginal();
struct S_OrigCall {
    char what;
    const void *volume;
};
extern std::vector<S_OrigCall> g_origCalls;

// What a step writes, by type; the name of the first field that differs.
std::string Differs(int type, const S_State &p, const S_State &o)
{
    if (p.result != o.result)
        return "result";
    if (p.callbacks != o.callbacks)
        return "callbacks";
    const C_PhysObject &a = As<C_PhysObject>(p), &b = As<C_PhysObject>(o);
    if (!NearV(a.m_velocity, b.m_velocity))
        return "velocity";
    if (!NearV(a.m_angularVelocity, b.m_angularVelocity))
        return "angular velocity";
    if (!NearV(a.m_pos, b.m_pos))
        return "position";
    if (!NearQ(a.m_rot, b.m_rot))
        return "rotation";
    if (a.m_resting != b.m_resting || a.m_state != b.m_state || a.m_volumeActive != b.m_volumeActive || a.m_freezeVolume != b.m_freezeVolume)
        return "flags";
    switch (type) {
    case PHYS_SPHERE:
        if (!NearM(a.m_invMatrix, b.m_invMatrix))
            return "sphere's inverse matrix";
        if (!NearV(a.m_volume.center, b.m_volume.center) || !NearF(a.m_volume.centerW, b.m_volume.centerW))
            return "sphere's volume center";
        break;
    case PHYS_FALLING: {
        const auto &x = As<C_PhysFallingObject>(p), &y = As<C_PhysFallingObject>(o);
        if (x.m_groundChecked != y.m_groundChecked || (x.m_groundChecked && !NearF(x.m_groundY, y.m_groundY)))
            return "ground";
        break;
    }
    case PHYS_BOX: {
        const auto &x = As<C_PhysBox>(p), &y = As<C_PhysBox>(o);
        if (!NearV(x.m_contact, y.m_contact))
            return "box contact";
        if (!NearV(x.m_rollAxis, y.m_rollAxis))
            return "box roll axis";
        break;
    }
    case PHYS_DOOR: {
        const auto &x = As<C_PhysDoor>(p), &y = As<C_PhysDoor>(o);
        if (!NearF(x.m_phase, y.m_phase) || !NearF(x.m_amplitude, y.m_amplitude) || x.m_swingState != y.m_swingState || x.m_moving != y.m_moving ||
            x.m_rattling != y.m_rattling)
            return "door state";
        if (!NearQ(x.m_openRot1, y.m_openRot1) || !NearQ(x.m_openRot2, y.m_openRot2) || !NearQ(x.m_closedRot, y.m_closedRot))
            return "door rotations";
        if (!NearV(x.m_axis, y.m_axis) || !NearV(x.m_hitDir, y.m_hitDir) || !NearF(x.m_openAngle, y.m_openAngle) || !NearF(x.m_speed, y.m_speed) ||
            !NearF(x.m_damping, y.m_damping))
            return "door swing";
        break;
    }
    case PHYS_SWINGING: {
        const auto &x = As<C_PhysSwingingObject>(p), &y = As<C_PhysSwingingObject>(o);
        if (!NearF(x.m_phase, y.m_phase) || !NearF(x.m_amplitude, y.m_amplitude) || x.m_swingState != y.m_swingState || x.m_swinging != y.m_swinging)
            return "swing state";
        if (!NearQ(x.m_swingRot1, y.m_swingRot1) || !NearQ(x.m_swingRot2, y.m_swingRot2) || !NearQ(x.m_restRot, y.m_restRot))
            return "swing rotations";
        break;
    }
    }
    if (g_compareVolume) {
        if (!NearM(a.m_matrix, b.m_matrix) || !NearM(a.m_invMatrix, b.m_invMatrix))
            return "volume matrices";
        if (!NearV(a.m_volume.center, b.m_volume.center))
            return "volume center";
    }
    const I3D_frame &fa = FrameOf(p), &fb = FrameOf(o);
    if ((fa.m_frameFlags & ~0u) != (fb.m_frameFlags & ~0u))
        return "frame flags";
    if (!NearV(fa.m_localMatrix(3), fb.m_localMatrix(3)) || !NearQ(fa.m_rot, fb.m_rot))
        return "frame place";
    if ((fa.m_frameFlags & FRMFLAGS_WMATRIX_VALID) && !NearM(fa.m_worldMatrix, fb.m_worldMatrix))
        return "frame matrix";
    return "";
}

struct S_MotionStats {
    long steps = 0, agree = 0;
    std::map<std::string, long> differ;
};
std::map<std::string, S_MotionStats> g_motion;

// One call compared: `port` and `original` run on the same state; the port's result kept. `grid`: the original's
// world with the dynamic volumes. Returns the port's result.
template <class P, class O> bool DiffCall(const char *what, C_PhysObject *o, bool grid, P port, O original)
{
    S_MotionStats &st = g_motion[what];
    const int type = o->GetType();
    const size_t size = ObjectSize(type);
    static S_State before, p, q;
    Take(before, o, size);
    const S_CollisionGridCell *cell1 = o->m_volume.cell;
    const S_CollisionGridCell *cell2 = type == PHYS_SWINGING ? static_cast<C_PhysSwingingObject *>(o)->m_volume2.cell : nullptr;
    const int callbacks = g_callbackCalls;
    p.result = port();
    p.callbacks = g_callbackCalls - callbacks;
    Take(p, o, size);
    ++st.steps;
    // Whether each volume is in the world after the port's call.
    const bool in1 = o->m_volume.cell != nullptr;
    const bool in2 = type == PHYS_SWINGING && static_cast<C_PhysSwingingObject *>(o)->m_volume2.cell != nullptr;
    if (!OriginalExeLoaded())
        return p.result;
    Put(before, o, size);                                                // (the frame as it was)
    ToOriginal(before.object, type);
    g_callbackCalls = callbacks;
    g_origCalls.clear();
    g_diffObject = o;
    q.result = WithOriginalWorld(grid, [&] { return original(static_cast<void *>(g_origObject)); });
    g_diffObject = nullptr;
    // The original's: as before, then its Add / Remove calls (a Move files one whose cell exists: all here).
    bool orig1 = cell1 != nullptr, orig2 = cell2 != nullptr;
    for (const S_OrigCall &c : g_origCalls) {
        bool &in = c.volume == g_origObject + 0xD0 ? orig1 : orig2;
        if (c.what == 'R')
            in = false;
        else if (g_collisionWorld.m_grid)
            in = true;
    }
    const bool registrationDiffers = orig1 != in1 || (type == PHYS_SWINGING && orig2 != in2);
    q.callbacks = g_callbackCalls - callbacks;
    std::memcpy(q.object, before.object, size);
    FromOriginal(q.object, type);
    std::memcpy(q.frame, o->GetFrame(), sizeof q.frame);
    const std::string differs = registrationDiffers ? std::string("volumes in the world") : Differs(type, p, q);
    Put(p, o, size);
    g_callbackCalls = callbacks + p.callbacks;
    CHECK(differs.empty());
    if (differs.empty()) {
        ++st.agree;
    } else {
        ++st.differ[differs];
        static int shown;
        if (shown++ < 12) {
            const C_PhysObject &a = As<C_PhysObject>(p), &b = As<C_PhysObject>(q);
            std::printf("  %s differs (%s): port %d pos %g %g %g vel %g %g %g; original %d pos %g %g %g vel %g %g %g\n", what, differs.c_str(), p.result,
                        a.m_pos.x, a.m_pos.y, a.m_pos.z, a.m_velocity.x, a.m_velocity.y, a.m_velocity.z, q.result, b.m_pos.x, b.m_pos.y, b.m_pos.z,
                        b.m_velocity.x, b.m_velocity.y, b.m_velocity.z);
        }
    }
    return p.result;
}
bool DiffTick(const char *what, C_PhysObject *o, float dt, bool grid = true)
{
    const uint32_t tick = OrigCode(o->GetType()).tick;
    return DiffCall(what, o, grid, [&] { return o->Tick(dt); }, [&](void *self) { return OriginalAt<T_OrigTick>(tick)(self, dt); });
}
void DiffImpulse(const char *what, C_PhysObject *o, const S_vector &impulse, const S_vector &point, float factor)
{
    const uint32_t code = OrigCode(o->GetType()).impulse;
    DiffCall(what, o, true, [&] { o->ApplyImpulse(impulse, point, factor); return true; },
             [&](void *self) { OriginalAt<T_OrigImpulse>(code)(self, impulse, point, factor); return true; });
}
bool DiffHit(const char *what, C_PhysObject *o, int kind, const S_vector &dir)
{
    const uint32_t code = OrigCode(o->GetType()).hit;
    return DiffCall(what, o, true, [&] { return o->Hit(kind, &dir, nullptr); },
                    [&](void *self) { return OriginalAt<T_OrigHit>(code)(self, kind, &dir, nullptr); });
}
void DiffSync(const char *what, C_PhysObject *o)
{
    typedef void(__attribute__((thiscall)) * T_OrigSync)(void *);
    g_compareVolume = true;
    DiffCall(what, o, true, [&] { o->SyncFrame(); return true; },
             [&](void *self) { OriginalAt<T_OrigSync>(0x0040ab80)(self); return true; });
    g_compareVolume = false;
}

// ---- the objects' trajectories
std::mt19937 g_mrng(4711);
float U(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_mrng); }
S_vector RandomDir() { return S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized(); }
C_PhysObject *Make(int type, const S_vector &pos, const S_vector &half, float yaw, S_PhysicsObjectDesc *custom = nullptr)
{
    I3D_frame *frame = NewFrame(FRAME_VISUAL, "m", pos, YawQuat(yaw), Box(half), "", g_sector);
    S_PhysicsObjectDesc d = custom ? *custom : Desc(frame, type, true);
    d.frame = frame;
    d.type = type;
    d.callback = Callback;
    return ObjectOf(g_physicsModule.CreateObject(d));
}
void Drop(C_PhysObject *o)
{
    T_PhysicsObject h = o->GetHandle();
    g_physicsModule.DestroyObject(&h);
}
// Steps until it stops (the module's sync between them).
void Fly(const char *what, C_PhysObject *o, int steps, bool grid = true)
{
    for (int i = 0; i < steps; ++i) {
        const float dt = U(0.01f, 0.12f);
        const bool moving = DiffTick(what, o, dt, grid);
        DiffSync("sync", o);
        if (!moving)
            break;
    }
}

// Crates: cans resting on the floor; half of them given a switch's type (rays that exclude it pass).
std::vector<C_PhysObject *> Crates()
{
    std::vector<C_PhysObject *> crates;
    for (int i = 0; i < 12; ++i) {
        C_PhysObject *c = Make(PHYS_CAN, S_vector(U(10, 50), 1.3f, U(10, 50)), S_vector(0.3f, 0.3f, 0.3f), U(0, 6));
        c->m_velocity.Zero();
        if (i % 2)
            c->m_volume.type = COLLISION_TYPE_SWITCH;
        crates.push_back(c);
    }
    return crates;
}

void TestPoints()
{
    // Simple objects and debris: thrown at the floor, the wall, the block, the ramp, the crates; some collide with
    // dynamic volumes, some not; pushed too.
    std::vector<C_PhysObject *> crates = Crates();
    for (int i = 0; i < 400 / QUICK; ++i) {
        const int type = i % 2 ? PHYS_FALLING : PHYS_SIMPLE;
        const float size = i % 4 < 2 ? 0.1f : 0.4f;                       // (debris: small ones slide on the ground)
        C_PhysObject *o = Make(type, S_vector(U(4, 60), U(1.5f, 8), U(4, 60)), S_vector(size, size, size), U(0, 6));
        o->m_velocity = RandomDir() * U(0.5f, 12);
        if (U(0, 1) < 0.5f)                                              // (half of them thrown at a crate)
            o->m_velocity = (crates[static_cast<size_t>(U(0, 11.99f))]->m_pos - o->m_pos).Normalized() * U(3, 14);
        o->m_angularVelocity = RandomDir() * U(0, 8);
        o->m_collideWithBoxes = i % 3 == 0;
        if (i % 5 == 0)
            DiffImpulse(type == PHYS_SIMPLE ? "simple push" : "debris push", o, RandomDir() * U(0.5f, 5), o->m_pos + RandomDir() * 0.3f, U(0.2f, 1.5f));
        Fly(type == PHYS_SIMPLE ? "simple step" : "debris step", o, 60);
        Drop(o);
    }
    for (C_PhysObject *c : crates)
        Drop(c);
}

void TestCans()
{
    // Cans dropped, thrown and kicked: on the floor, against the wall and the block, onto each other and the crates.
    std::vector<C_PhysObject *> crates = Crates();
    for (int i = 0; i < 200 / QUICK; ++i) {
        S_PhysicsObjectDesc d = Desc(nullptr, PHYS_CAN, true);
        d.radius = i % 3 ? -1.0f : U(0.05f, 0.4f);
        C_PhysObject *o = Make(PHYS_CAN, S_vector(U(4, 60), U(1.3f, 6), U(4, 60)), S_vector(0.08f, 0.12f, 0.08f), U(0, 6), &d);
        o->m_collideWithBoxes = i % 2 == 0;
        if (i % 2)
            o->m_velocity = RandomDir() * U(0.5f, 10);
        if (U(0, 1) < 0.4f)
            o->m_velocity = (crates[static_cast<size_t>(U(0, 11.99f))]->m_pos - o->m_pos).Normalized() * U(2, 12);
        if (i % 3 == 0)
            DiffImpulse("can kick", o, RandomDir() * U(0.2f, 3), o->m_pos + RandomDir() * 0.1f, 1.0f);
        Fly("can step", o, 60);
        Drop(o);
    }
    for (C_PhysObject *c : crates)
        Drop(c);
}

void TestSpheres()
{
    // Balls (never made by the game): their steps move their own volume, so both run without the dynamic grid.
    for (int i = 0; i < 150 / QUICK; ++i) {
        S_PhysicsObjectDesc d = Desc(nullptr, PHYS_SPHERE, true);
        d.radius = i % 3 ? -1.0f : U(0.05f, 0.5f);
        C_PhysObject *o = Make(PHYS_SPHERE, S_vector(U(4, 60), U(1.5f, 6), U(4, 60)), S_vector(0.2f, 0.2f, 0.2f), U(0, 6), &d);
        g_collisionWorld.RemoveVolume(&o->m_volume);
        o->m_volumeActive = false;
        o->m_velocity = RandomDir() * U(0.5f, 10);
        S_CollisionGrid *grid = g_collisionWorld.m_grid;
        g_collisionWorld.m_grid = nullptr;
        Fly("sphere step", o, 40, false);
        g_collisionWorld.m_grid = grid;
        Drop(o);
    }
}

void TestBoxes()
{
    // Tumbling boxes (never made by the game): flying, tipping over an edge, rolling.
    for (int i = 0; i < 200 / QUICK; ++i) {
        const S_vector half(U(0.1f, 0.6f), U(0.1f, 0.6f), U(0.1f, 0.6f));
        C_PhysObject *o = Make(PHYS_BOX, S_vector(U(4, 60), U(1.8f, 5), U(4, 60)), half, U(0, 6));
        o->m_velocity = RandomDir() * U(0.2f, 8);
        o->m_angularVelocity = RandomDir() * U(0, 5);
        if (i % 4 == 0)
            DiffImpulse("box push", o, RandomDir() * U(0.5f, 4), o->m_pos + RandomDir() * 0.2f, U(0.2f, 1.5f));
        Fly("box step", o, 80);
        // At rest tipped over: woken again (a hit), it rolls round its long axis until that end meets something.
        if (o->m_state == BOX_ROLLING)
            Fly("box roll", o, 80);
        Drop(o);
    }
    // Spinning by the wall (x 40), hardly moving: the turn meets the wall before the move does.
    for (int i = 0; i < 120 / QUICK; ++i) {
        const S_vector half(U(0.3f, 0.8f), U(0.05f, 0.2f), U(0.05f, 0.2f));
        C_PhysObject *o = Make(PHYS_BOX, S_vector(40.0f - U(0.35f, 0.9f), U(1.3f, 3.5f), U(8, 56)), half, U(0, 6));
        o->m_velocity = RandomDir() * U(0.01f, i % 2 ? 0.15f : 3.0f);
        o->m_angularVelocity = S_vector(U(-1, 1), U(2, 9) * (i % 3 ? 1.0f : -1.0f), U(-1, 1));
        Fly("box spin", o, 40);
        if (o->m_state == BOX_ROLLING)
            Fly("box roll", o, 80);
        Drop(o);
    }
}

void TestDoors()
{
    // Doors kicked and pushed from either side, against walls and each other; free swinging ones tipping over.
    for (int i = 0; i < 300 / QUICK; ++i) {
        S_PhysicsObjectDesc d = Desc(nullptr, PHYS_DOOR, false);
        d.door.freeSwing = i % 4 == 3;
        d.door.angle = i % 3 == 0 ? 1.5707964f : U(0.2f, 1.2f);
        const float yaw = U(0, 6);
        S_vector at(U(6, 58), 2.0f, U(6, 58));
        if (i % 5 == 0)
            at = S_vector(40.0f - U(0.3f, 1.2f), 2.0f, U(8, 56));          // (by the wall: blocked)
        C_PhysObject *o = Make(PHYS_DOOR, at, S_vector(U(0.4f, 0.8f), 1.0f, 0.05f), yaw, &d);
        // (Some directions not of unit length: the hits normalize their axis.)
        const S_vector dir = S_vector(U(-1, 1), 0, U(-1, 1)).Normalized() * (i % 7 == 2 ? U(0.3f, 3) : 1.0f);
        const int kind = i % 3 == 1 ? PHYS_HIT_PUSH : PHYS_HIT_KICK;
        DiffHit(kind == PHYS_HIT_PUSH ? "door push" : "door kick", o, kind, dir);
        DiffHit("door hit again", o, kind, dir);                          // (moving: refused)
        Fly(kind == PHYS_HIT_PUSH ? "door rattle" : "door swing", o, 120);
        // Hit again once it has stopped (a rattled door takes it; an opened one stays open), the other way or not.
        const S_vector dir2 = S_vector(U(-1, 1), 0, U(-1, 1)).Normalized();
        const int kind2 = i % 2 ? PHYS_HIT_PUSH : PHYS_HIT_KICK;
        DiffHit("door hit later", o, kind2, dir2);
        Fly(kind2 == PHYS_HIT_PUSH ? "door rattle" : "door swing", o, 120);
        Drop(o);
    }
}

void TestSwinging()
{
    for (int i = 0; i < 200 / QUICK; ++i) {
        C_PhysObject *o = Make(PHYS_SWINGING, S_vector(U(6, 58), U(1.5f, 4), U(6, 58)), S_vector(0.2f, 0.4f, 0.2f), U(0, 6));
        auto *sw = static_cast<C_PhysSwingingObject *>(o);
        const S_vector dir = RandomDir();
        if (i % 2)
            DiffHit("swing hit", o, i % 4 == 1 ? PHYS_HIT_PUSH : PHYS_HIT_KICK, dir);
        else
            DiffImpulse("swing push", o, dir * U(0.5f, 3), o->m_pos, 1.0f);
        DiffHit("swing hit again", o, PHYS_HIT_KICK, dir);               // (swinging: refused)
        Fly("swing step", o, 200);
        if (o->m_volumeActive) {                                          // (settled: back in the world)
            CHECK(InWorld(o->m_volume) && InWorld(sw->m_volume2) && !sw->m_swinging);
            DiffHit("swing hit later", o, PHYS_HIT_KICK, RandomDir());     // (and swings again, as far as the first time)
            Fly("swing step", o, 200);
        }
        Drop(o);
    }
}

// Bounce alone (0x0040b360: the velocity and the normal by reference).
void TestBounce()
{
    typedef void(__attribute__((thiscall)) * T_OrigBounce)(void *, S_vector *, const S_vector *);
    C_PhysObject *o = Make(PHYS_SIMPLE, S_vector(30, 5, 30), S_vector(0.1f, 0.1f, 0.1f), 0);
    S_MotionStats &st = g_motion["bounce"];
    for (int i = 0; i < 2000 && OriginalExeLoaded(); ++i) {
        S_vector v = RandomDir() * U(0.0f, 10.0f), n = RandomDir();
        if (i % 7 == 0)
            v = v.Normalized();
        struct alignas(16) {
            S_vector v;
            float w;
        } ov = {v, 0}, on = {n, 0};
        S_vector pv = v;
        o->Bounce(pv, n);
        ToOriginal(reinterpret_cast<const uint8_t *>(o), PHYS_SIMPLE);
        OriginalAt<T_OrigBounce>(0x0040b360)(g_origObject, &ov.v, &on.v);
        ++st.steps;
        const bool ok = NearV(pv, ov.v);
        st.agree += ok;
        CHECK(ok);
        if (!ok && st.agree + 5 > st.steps)
            std::printf("  bounce v %g %g %g n %g %g %g: port %g %g %g, original %g %g %g\n", v.x, v.y, v.z, n.x, n.y, n.z, pv.x, pv.y, pv.z, ov.v.x, ov.v.y,
                        ov.v.z);
    }
    Drop(o);
}

void TestMotion()
{
    Fresh();
    PatchOriginal();
    TestBounce();
    g_gameRole = GAME_ROLE_LOCAL;
    if (!OriginalExeLoaded())
        std::printf("  (no original executable: the objects' steps run unchecked)\n");
    TestPoints();
    TestCans();
    TestSpheres();
    TestBoxes();
    TestDoors();
    TestSwinging();
    for (auto &[what, st] : g_motion) {
        std::printf("  %s: %ld, agree %ld", what.c_str(), st.steps, st.agree);
        for (auto &[field, n] : st.differ)
            std::printf(", %s differs %ld", field.c_str(), n);
        std::printf("\n");
    }
    Fresh();
}
}  // namespace
