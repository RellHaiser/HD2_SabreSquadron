// Vehicle physics part 1: the rigid bodies (game/rigid_body.cpp and rigid_body_step.cpp, the original's
// 0x004c7d90..0x004cff70) against the original executable's. cmd_diff.cpp's world, fakes and call logger are included
// (CMD_DIFF_NO_MAIN); the bodies' calls go through rb_calls.s / rb_calls.inc (gen_cmd_calls.py rb). The LS3DF math (the
// quaternions, the products, the inverse, AngleTo, the planes' line) is LS3DF.dll's on both sides.
//
// Each case builds the same world on each side (cmd_diff's) and one to three bodies on frames of the test's (at the
// root, turned; their object visuals' boxes of every proportion, nested now and then; the frame itself a visual now
// and then), makes the bodies on each side (the original's by its constructor 0x004c84a0 in a block of 0x600, the
// port's by its own) and runs the same random sequence of operations: SetFrame, Setup (boxes) and SetupWheel (discs)
// with their parameters, Start, Stop, Tick and Step (frames of every length), ApplyImpulse, AddImpulse, Wake, Nudge,
// SetState, SetOwner, ForgetActor (the body's and every body's), the owners' fields (the hit sound's material, the flat
// stop, the actors ignored, the step), saves and loads, GetHierarchyBox, the actors under the bodies moved. The ray
// tests are answered from the case's ground: a tilted plane, water above it now and then, an actor's platform (a car,
// an animation actor, a human) over part of it, a wall; their materials of every kind. The frames' engine calls are
// answered alike on both sides (SetWorldPosDir3 and SetPos set the local matrix, Update rebuilds it from the rotation
// when it is stale, UpdateBound sets the bound). The callback logs its contacts and refuses some. After each
// operation every body's fields (the original's block read through the port's class: the layout is the same), the
// frames and every call made into the game and the engine are compared.
// MinGW; build_rb.bat; runs rb_diff.exe ("rb_diff: N checks, 0 failures"). Env: HD2_RB_CASES (default 1,500;
// HD2_RB_QUICK: 300), HD2_RB_SEED, HD2_RB_SHOW (1: the port's lines; 2: both), HD2_RB_ONLY (the case shown),
// HD2_RB_TRACE (a case's operations as they start).
#define HARNESS_NAME "rb_diff"
#define CMD_CALLS_INC "rb_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/collision.h"
#include "game/rigid_body.h"
#undef protected
#undef private

static_assert(offsetof(C_RigidBody, m_state) == 0x10 && offsetof(C_RigidBody, m_frame) == 0x40 &&
                  offsetof(C_RigidBody, m_owner) == 0x88 && offsetof(C_RigidBody, m_matrix) == 0xF0 &&
                  offsetof(C_RigidBody, m_points) == 0x230 && offsetof(C_RigidBody, m_volume) == 0x410 &&
                  offsetof(C_RigidBody, m_pointPlanes) == 0x470 && offsetof(C_RigidBody, m_mode) == 0x560 &&
                  offsetof(C_RigidBody, m_actorContacts) == 0x5BC && offsetof(C_RigidBody, m_pointSurface) == 0x5CC &&
                  offsetof(C_RigidBody, m_ignoreActors) == 0x5FD,
              "the original's layout");

// ---- the port's side of what the bodies call and the test provides ----
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpenWrite(name); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }

// LS3DF.dll's math (the original's calls go there too; a struct answer's address after `this`).
template <class T> T Ls3dfFn(const char *name) { return reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df(name))); }
bool __stdcall S_matrix::Inverse(const S_matrix &src)
{
    static const auto f = Ls3dfFn<bool(__stdcall *)(S_matrix *, const S_matrix &)>("?Inverse@S_matrix@@QAG_NABU1@@Z");
    return f(this, src);
}
S_vector &__stdcall S_vector::operator*=(const S_matrix &m)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_vector *, const S_matrix &)>("??XS_vector@@QAGAAU0@ABUS_matrix@@@Z");
    f(this, m);
    return *this;
}
S_matrix __stdcall S_matrix::operator*(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_matrix *, S_matrix *, const S_matrix &)>("??DS_matrix@@QBG?AU0@ABU0@@Z");
    S_matrix r;
    f(this, &r, m);
    return r;
}
void __stdcall S_quat::Make(const S_vector &axis, float angle)
{
    static const auto f = Ls3dfFn<void(__stdcall *)(S_quat *, const S_vector &, float)>("?Make@S_quat@@QAGXABUS_vector@@M@Z");
    f(this, axis, angle);
}
S_matrix __stdcall S_quat::RotationMatrix() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, S_matrix *)>("?RotationMatrix@S_quat@@QBG?AUS_matrix@@XZ");
    S_matrix m;
    f(this, &m);
    return m;
}
float __stdcall S_vector::AngleTo(const S_vector &v) const
{
    static const auto f = Ls3dfFn<float(__stdcall *)(const S_vector *, const S_vector &)>("?AngleTo@S_vector@@QBGMABU1@@Z");
    return f(this, v);
}
bool __stdcall S_plane::IntersectionLine(const S_plane &other, S_vector &linePos, S_vector &lineDir) const
{
    static const auto f = Ls3dfFn<bool(__stdcall *)(const S_plane *, const S_plane &, S_vector &, S_vector &)>(
        "?IntersectionLine@S_plane@@QBG_NABU1@AAUS_vector@@1@Z");
    return f(this, other, linePos, lineDir);
}
// The port's allocations zeroed, as the original's (its operator new and _aligned_malloc zeroing ones here).
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void *operator new(size_t n, std::align_val_t a)
{
    void *p = _aligned_malloc(n ? n : 1, size_t(a));
    std::memset(p, 0, n ? n : 1);
    return p;
}
void operator delete(void *p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { _aligned_free(p); }
void *operator new[](size_t n, std::align_val_t a)
{
    void *p = _aligned_malloc(n ? n : 1, size_t(a));
    std::memset(p, 0, n ? n : 1);
    return p;
}

namespace H {

int __cdecl O_Atexit(void *) { return 0; }
void *__cdecl O_AlignedMalloc(size_t size, size_t align)
{
    void *p = _aligned_malloc(size ? size : 1, align);
    std::memset(p, 0, size ? size : 1);
    return p;
}
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }
uint32_t Ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
template <class T> T &Mut(void *p, uint32_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }
// (Denormals shown as "tiny": the original's x87 rounds them twice, the harness's SSE once; a body without a visual box
// has an inertia of ~1e-34 and spins of ~1e-40.)
std::string Fl(float f) { return f != 0.0f && std::fabs(f) < 1e-37f ? std::string("tiny") : FloatText(f); }
std::string VN(const void *p, int n)
{
    std::string s = "(";
    for (int k = 0; k < n; ++k)
        s += (k ? " " : "") + Fl(static_cast<const float *>(p)[k]);
    return s + ")";
}
std::string V3(const void *p) { return VN(p, 3); }
std::string V4(const void *p) { return VN(p, 4); }
std::string Hex(const std::string &bytes)
{
    std::string hex;
    for (unsigned char ch : bytes)
        hex += F("%02x", ch);
    return hex;
}

// ---- the original's functions ----
constexpr uint32_t O_RB_CTOR = 0x004c84a0, O_RB_DTOR = 0x004c8770, O_RB_SETUP = 0x004cd6e0, O_RB_SETUP_WHEEL = 0x004cd550,
                   O_RB_SET_OWNER = 0x004c8250, O_RB_SET_STATE = 0x004c8270, O_RB_ADD_IMPULSE = 0x004ce7a0,
                   O_RB_FORGET = 0x004ce8f0, O_FORGET_ACTOR = 0x004cff70, O_HIERARCHY_BOX = 0x004c7db0;
// The virtual slots (the original's vtable 0x00810700): SetFrame 0, ApplyImpulse 4, Wake 8, SaveGame 0xC, LoadGame 0x10,
// Nudge 0x14, Start 0x18, Stop 0x1C, Step 0x20, Tick 0x24.
template <class T> T Slot(uint8_t *b, int off) { return reinterpret_cast<T>((*reinterpret_cast<void ***>(b))[off / 4]); }

// ---- the case ----
constexpr int MAX_BODIES = 3, MAX_ACTORS = 6, STRIPES = 5, VOLUMES = 3 + STRIPES;
struct S_RbBox {
    float min[3], max[3];
    float at[3];                                        // (the visual's place in the frame)
};
struct S_RbBody {
    float pos[3] = {0, 2, 0};
    float rot[4] = {1, 0, 0, 0};
    bool frameVisual = false;                           // (the frame itself an object visual)
    std::vector<S_RbBox> boxes;                         // (its visuals; a nested one after the first now and then)
    bool nested = false;
    bool wheel = false;
    float velocity[3] = {};
    float weight = 10.0f, friction = 0.5f, radius = 0.3f, width = 0.2f, roll = 0.02f;
    bool moveCollision = false, restVolume = false;
    int callbackMode = 0;                               // 0 none, 1 accept, 2 refuse some, 3 answer 2
};
struct S_RbGround {
    float y = 0.0f, tiltX = 0.0f, tiltZ = 0.0f;
    int material = 1;
    bool water = false;
    float waterY = 0.5f;
    int waterMaterial = 0x1F;
    int platform = -1;                                  // (an actor's: its index)
    float platformY = 0.6f, platformMinX = -1.0f, platformMaxX = 1.0f;
    int platformMaterial = 2;
    bool wall = false;
    float wallX = 3.0f;
};
enum { OP_SET_FRAME, OP_SETUP, OP_START, OP_STOP, OP_TICK, OP_STEP, OP_APPLY_IMPULSE, OP_ADD_IMPULSE, OP_WAKE, OP_NUDGE,
       OP_SET_STATE, OP_SET_OWNER, OP_FORGET, OP_FIELDS, OP_SAVE_LOAD, OP_BOX, OP_ACTOR_MOVE, OP_PLATFORM, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"SetFrame", "Setup", "Start", "Stop", "Tick", "Step", "ApplyImpulse", "AddImpulse", "Wake",
                                     "Nudge", "SetState", "SetOwner", "ForgetActor", "Fields", "SaveLoad", "HierarchyBox",
                                     "ActorMove", "Platform"};
struct S_RbOp {
    int kind = 0;
    int body = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_RbCase {
    std::vector<S_RbBody> bodies;
    S_RbGround ground;
    int actorTypes[MAX_ACTORS] = {ACTOR_CAR, ACTOR_ANIMATION, ACTOR_HUMAN, ACTOR_CAR, ACTOR_DOOR, ACTOR_ANIMATION};
    std::vector<S_RbOp> ops;
};

// ---- the case's world on each side ----
uint8_t *g_body[2][MAX_BODIES];
uint8_t *g_bodyFrame[2][MAX_BODIES];
uint8_t *g_actor[2][MAX_ACTORS];
uint8_t *g_volume[2][VOLUMES];                          // ground, water, wall, the platform's stripes
int g_bodyCount;
S_RbGround g_ground;
bool g_platformOn = true;                               // (OP_PLATFORM takes it away and back)
int g_callbackMode[MAX_BODIES];
C_RigidBody *Body(int i) { return reinterpret_cast<C_RigidBody *>(g_body[g_side][i]); }

uint8_t *MakeFrame(int type, const std::string &name, uint8_t *parent)
{
    uint8_t *b = NewFrame(type, name);
    AsFrame(b)->m_name = W().frames.back().name.c_str();
    if (parent)
        LinkUnder(b, parent);
    return b;
}
void SetLocal(uint8_t *frame, const S_matrix &m)
{
    I3D_frame *f = AsFrame(frame);
    f->m_localMatrix = m;
    f->m_scale = S_vector(1.0f, 1.0f, 1.0f);
    f->m_frameFlags = (f->m_frameFlags & ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_ROT_DIRTY | FRMFLAGS_BOUND_VALID)) | FRMFLAGS_CHANGED |
                      FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
}
void SetBox(uint8_t *visual, const float *mn, const float *mx)
{
    I3D_bound_volume &b = *reinterpret_cast<I3D_bound_volume *>(visual + 0x1A0);
    b.bbox.min = S_vector(mn[0], mn[1], mn[2]);
    b.bbox.max = S_vector(mx[0], mx[1], mx[2]);
    b.bbox.minW = 0.0f;
    b.bbox.maxW = 0.0f;
    b.center = S_vector((mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f);
    b.radius = 1.0f;
    Mut<uint32_t>(visual, 0x1D0) |= VISFLAGS_BOUND_VALID;
}
void SetVisualType(uint8_t *frame, uint32_t type)
{
    for (S_FakeFrame &f : W().frames)
        if (f.bytes == frame)
            f.visualType = type;
}

void BuildRbWorld(const S_RbCase &c)
{
    static void **actorVtables[2];
    if (!actorVtables[g_side])
        actorVtables[g_side] = GameVtable("C_Actor", VirtualSlots("C_Actor::", g_side != 0));
    for (int i = 0; i < MAX_ACTORS; ++i) {
        uint8_t *a = Block(ActorBytes(ACTOR_CAR));
        *reinterpret_cast<void ***>(a) = actorVtables[g_side];
        Name(a, F("rb actor%d", i));
        const S_vector pos(float(i) - 1.0f, 0.5f, float(i) * 0.5f);
        if (g_side) {
            Mut<int>(a, 0x18) = c.actorTypes[i];
            std::memcpy(a + 0x30, &pos, 12);
        } else {
            reinterpret_cast<C_Actor *>(a)->m_type = E_ActorType(c.actorTypes[i]);
            reinterpret_cast<C_Actor *>(a)->SetPosition(pos);
        }
        g_actor[g_side][i] = a;
    }
    // The ground's volumes (their flags carry the material), the platform's five stripes (each an actor's).
    static const char *const names[] = {"ground", "water", "wall", "stripe0", "stripe1", "stripe2", "stripe3", "stripe4"};
    for (int i = 0; i < VOLUMES; ++i) {
        g_volume[g_side][i] = Block(0x60);
        Name(g_volume[g_side][i], names[i]);
    }
    S_CollisionVolume *v = reinterpret_cast<S_CollisionVolume *>(g_volume[g_side][0]);
    v->flags = uint32_t(c.ground.material) << 24;
    v = reinterpret_cast<S_CollisionVolume *>(g_volume[g_side][1]);
    v->flags = (uint32_t(c.ground.waterMaterial) << 24) | COLLISION_FLAG_WATER;
    v = reinterpret_cast<S_CollisionVolume *>(g_volume[g_side][2]);
    v->flags = uint32_t(0x55) << 24;                    // (paper: a contact that always counts)
    for (int k = 0; k < STRIPES; ++k) {
        v = reinterpret_cast<S_CollisionVolume *>(g_volume[g_side][3 + k]);
        v->flags = (uint32_t(c.ground.platformMaterial) << 24) | COLLISION_FLAG_DYNAMIC;
        v->actor = c.ground.platform >= 0 ? reinterpret_cast<C_Actor *>(g_actor[g_side][(c.ground.platform + k) % MAX_ACTORS]) : nullptr;
    }
    // The bodies' frames (at the root): turned, their visuals' boxes.
    g_bodyCount = int(c.bodies.size());
    for (int i = 0; i < g_bodyCount; ++i) {
        const S_RbBody &bc = c.bodies[size_t(i)];
        uint8_t *frame = MakeFrame(bc.frameVisual ? FRAME_VISUAL : FRAME_MODEL, F("body%d", i), nullptr);
        S_quat q;
        q.s = bc.rot[0];
        q.v = S_vector(bc.rot[1], bc.rot[2], bc.rot[3]);
        S_matrix m;
        m.Identity();
        m.SetRot3(q);
        m.m[3][0] = bc.pos[0];
        m.m[3][1] = bc.pos[1];
        m.m[3][2] = bc.pos[2];
        SetLocal(frame, m);
        AsFrame(frame)->m_rot = q;
        SetVisualType(frame, bc.frameVisual ? I3DVISUAL_OBJECT : 0);
        uint8_t *parent = frame;
        for (size_t k = 0; k < bc.boxes.size(); ++k) {
            const S_RbBox &box = bc.boxes[k];
            if (bc.frameVisual && k == 0) {
                SetBox(frame, box.min, box.max);
                continue;
            }
            uint8_t *visual = MakeFrame(FRAME_VISUAL, F("body%d box%d", i, int(k)), parent);
            SetVisualType(visual, k == 2 ? I3DVISUAL_LMAP : I3DVISUAL_OBJECT);   // (a third is not an object visual)
            S_matrix lm;
            lm.Identity();
            lm.m[3][0] = box.at[0];
            lm.m[3][1] = box.at[1];
            lm.m[3][2] = box.at[2];
            SetLocal(visual, lm);
            SetBox(visual, box.min, box.max);
            if (bc.nested)
                parent = visual;
        }
        g_bodyFrame[g_side][i] = frame;
    }
}

// ---- the answers ----
// The ray from `from` along `dir` (its length the reach) against the case's ground: the nearest of the tilted plane,
// the water (unless excluded), the platform (over its x range), the wall.
uint32_t RayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal, uint32_t excludeFlags)
{
    const S_RbGround &g = g_ground;
    struct S_Plane {
        S_vector n;
        float d;
        int volume;
    };
    std::vector<S_Plane> planes;
    {
        S_vector n(-g.tiltX, 1.0f, -g.tiltZ);
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        n = S_vector(n.x / len, n.y / len, n.z / len);
        planes.push_back({n, -(n.y * g.y), 0});
    }
    if (g.water && !(excludeFlags & COLLISION_FLAG_WATER))
        planes.push_back({S_vector(0.0f, 1.0f, 0.0f), -g.waterY, 1});
    if (g.platform >= 0 && g_platformOn)
        planes.push_back({S_vector(0.0f, 1.0f, 0.0f), -g.platformY, 3});
    if (g.wall)
        planes.push_back({S_vector(-1.0f, 0.0f, 0.0f), g.wallX, 2});
    float best = 2.0f;
    int which = -1;
    S_vector at, normal;
    for (const S_Plane &p : planes) {
        const float denominator = p.n.x * dir.x + p.n.y * dir.y + p.n.z * dir.z;
        if (!(denominator < 0.0f))
            continue;                                   // (only from the front)
        const float t = -((p.n.x * from.x + p.n.y * from.y + p.n.z * from.z) + p.d) / denominator;
        if (!(t >= 0.0f) || !(t <= 1.0f) || !(t < best))
            continue;
        const S_vector h(from.x + dir.x * t, from.y + dir.y * t, from.z + dir.z * t);
        int volume = p.volume;
        if (volume == 3) {
            if (h.x < g.platformMinX || h.x > g.platformMaxX)
                continue;
            // (Its stripe along x: each an actor's.)
            const float w = (g.platformMaxX - g.platformMinX) / float(STRIPES);
            int k = int((h.x - g.platformMinX) / w);
            volume += k < 0 ? 0 : k >= STRIPES ? STRIPES - 1 : k;
        }
        best = t;
        which = volume;
        at = h;
        normal = p.n;
    }
    if (which < 0)
        return 0u;
    *hitPos = at;
    *hitNormal = normal;
    return Ptr(g_volume[g_side][which]);
}
int g_callbackCount[2];
int __attribute__((fastcall)) BodyCallback(int event, int, void *context, const S_RigidBodyContact *contact)
{
    const int i = int(reinterpret_cast<uintptr_t>(context)) - 1;
    std::string line = F("callback body%d event %d", i, event);
    if (contact)
        line += " " + NameOf(Ptr(contact->volume)) + " " + V3(&contact->pos) + " " + V3(&contact->normal);
    g_log[g_side].push_back(line);
    const int mode = i >= 0 && i < MAX_BODIES ? g_callbackMode[i] : 1;
    ++g_callbackCount[g_side];
    if (mode == 2 && g_callbackCount[g_side] % 3 == 0)
        return RIGID_BODY_REFUSE;
    return mode == 3 ? 2 : 0;
}
void SetUpRbAnswers()
{
    g_answers["CollisionRayTest"] = [](const std::vector<uint32_t> &a) {
        const S_vector from = *reinterpret_cast<const S_vector *>(a[0]);
        const S_vector dir = *reinterpret_cast<const S_vector *>(a[1]);
        // (Its hit follows from the arguments the call's line shows, the same on both sides.)
        return RayTest(from, dir, reinterpret_cast<S_vector *>(a[2]), reinterpret_cast<S_vector *>(a[3]), a[5]);
    };
    // The frames: SetWorldPosDir3 and SetPos set the local matrix (the bodies' frames are at the root), Update
    // rebuilds it from the rotation when stale and leaves the world matrix to be made again, UpdateBound sets a bound.
    g_answers["I3D_frame::SetWorldPosDir3"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        const S_vector pos = *reinterpret_cast<const S_vector *>(a[1]);
        S_vector z = *reinterpret_cast<const S_vector *>(a[2]);
        const S_vector up = *reinterpret_cast<const S_vector *>(a[3]);
        auto unit = [](S_vector v) {
            const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return l > 0.0f ? S_vector(v.x / l, v.y / l, v.z / l) : S_vector(0.0f, 0.0f, 1.0f);
        };
        z = unit(z);
        const S_vector x = unit(S_vector(up.y * z.z - up.z * z.y, up.z * z.x - up.x * z.z, up.x * z.y - up.y * z.x));
        const S_vector y(z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x);
        S_matrix m;
        m.Identity();
        m(0) = x;
        m(1) = y;
        m(2) = z;
        m(3) = pos;
        f->m_localMatrix = m;
        f->m_frameFlags = (f->m_frameFlags & ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_ROT_DIRTY | FRMFLAGS_BOUND_VALID)) | FRMFLAGS_CHANGED |
                          FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
        return 0u;
    };
    g_answers["I3D_frame::SetPos"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_localMatrix(3) = *reinterpret_cast<const S_vector *>(a[1]);
        f->m_frameFlags = (f->m_frameFlags & ~(FRMFLAGS_WMATRIX_VALID | FRMFLAGS_BOUND_VALID)) | FRMFLAGS_CHANGED;
        return 0u;
    };
    g_answers["I3D_frame::Update"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        if (!(f->m_frameFlags & FRMFLAGS_LMATRIX_VALID)) {
            const S_vector pos = f->m_localMatrix(3);
            S_quat q = f->m_rot;
            f->m_localMatrix.Identity();
            f->m_localMatrix.SetRot3(q);
            f->m_localMatrix(3) = pos;
            f->m_frameFlags |= FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
        }
        f->m_frameFlags &= ~uint32_t(FRMFLAGS_WMATRIX_VALID);
        return 0u;
    };
    g_answers["I3D_frame::UpdateBound"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_bound.center = S_vector(0.1f, 0.2f, 0.3f);
        f->m_bound.radius = 1.5f;
        f->m_frameFlags |= FRMFLAGS_BOUND_VALID;
        return 0u;
    };
}

// ---- the compared state ----
std::string BodyState(int i)
{
    const C_RigidBody &b = *Body(i);
    std::string s = F("body%d state %d frame %s owner %s callback %s context %d restVol %d moveColl %d changed %d mode %d", i, b.m_state,
                      NameOf(Ptr(b.m_frame)).c_str(), NameOf(Ptr(b.m_owner)).c_str(), b.m_callback ? "set" : "null",
                      int(reinterpret_cast<uintptr_t>(b.m_context)), int(b.m_restVolume), int(b.m_moveCollision),
                      int(b.m_contactsChanged), b.m_mode);
    s += "\n  pos " + V3(&b.m_pos) + " dir " + V3(&b.m_dir) + " right " + V3(&b.m_right) + " up " + V3(&b.m_up) + " normal " + V3(&b.m_normal);
    s += "\n  centre " + V3(&b.m_centre) + F(" has %d", int(b.m_hasCentre)) + " world " + V3(&b.m_centreWorld) + " rest " + V3(&b.m_restPos);
    s += "\n  matrix " + VN(&b.m_matrix, 16) + " inv " + VN(&b.m_invMatrix, 16);
    s += "\n  rot " + VN(&b.m_rotation, 16) + " invRot " + VN(&b.m_invRotation, 16);
    // (Not gravity's w: the original's is unset, never read.)
    s += "\n  invInertia " + V3(&b.m_invInertia) + " velocity " + V3(&b.m_velocity) + " spin " + V3(&b.m_spin) + " gravity " + V3(&b.m_gravity);
    s += "\n  points";
    for (int k = 0; k < RIGID_BODY_POINTS; ++k)
        s += " " + V3(&b.m_points[k]);
    s += "\n  last";
    for (int k = 0; k < RIGID_BODY_POINTS; ++k)
        s += " " + V3(&b.m_lastPoints[k]);
    // (The disc's contact only while it touches: a ground ray that misses stores the original's unset stack there, the
    // port keeps the last; read only with a spring force.)
    s += "\n  contact " + (b.m_springForce > 0.0f ? V3(&b.m_contactPoint) : std::string("-")) + " hit " + V3(&b.m_hitPoint);
    const S_CollisionVolume &v = b.m_volume;
    s += F("\n  volume flags %x frame %s type %x box ", v.flags, NameOf(Ptr(v.frame)).c_str(), v.type) + V3(&v.bbox.min) + " " +
         V3(&v.bbox.max) + " centre " + V4(&v.center) + F(" matrix %s inv %s actor %s radius ", v.matrix == &b.m_matrix ? "own" : v.matrix ? "other" : "null",
                                                        v.invMatrix == &b.m_invMatrix ? "own" : v.invMatrix ? "other" : "null",
                                                        NameOf(Ptr(v.actor)).c_str()) +
         Fl(v.radius);
    s += "\n  planes";
    for (int k = 0; k < RIGID_BODY_POINTS; ++k)
        s += F(" %d:", int(b.m_pointHit[k])) + V3(&b.m_pointPlanes[k].normal) + "/" + Fl(b.m_pointPlanes[k].d) + "/" +
             NameOf(Ptr(b.m_pointActors[k])) + F("/%d/%d", int(b.m_pointSurface[k]), int(b.m_pointOnActor[k]));
    s += F("\n  still %u maxStep ", b.m_stillTime) + Fl(b.m_maxStep) + " mass " + Fl(b.m_mass) + " friction " + Fl(b.m_friction) +
         " bounce " + Fl(b.m_bounce) + " range " + Fl(b.m_contactRange) + " stiff " + Fl(b.m_stiffness) + " g " + Fl(b.m_g) + " u584 " +
         Fl(b.m_unknown584) + " skin " + Fl(b.m_skin) + " width " + Fl(b.m_width) + " radius " + Fl(b.m_radius) + " u594 " +
         Fl(b.m_unknown594) + " spring " + Fl(b.m_springForce) + " groundDist " + Fl(b.m_groundDistance) + " slow " + Fl(b.m_slowTime) +
         " restDelay " + Fl(b.m_restDelay) + " timeScale " + Fl(b.m_timeScale) +
         F(" u5ac %d hitMat %d hardest ", b.m_unknown5AC, b.m_hitMaterial) + Fl(b.m_hardestHit) +
         F(" u5b8 %d hitIdx %d u5f8 %d trailing %d nudged %d stopFlat %d ignoreActors %d", b.m_unknown5B8, int(b.m_hitPointIndex),
           int(b.m_unknown5F8), int(b.m_testTrailing), int(b.m_nudged), int(b.m_stopOnFlat), int(b.m_ignoreActors));
    s += F("\n  actors %d cap %d grow %d", b.m_actorContactCount, b.m_actorContactsCapacity, b.m_actorContactsGrowBy);
    for (int k = 0; k < b.m_actorContactCount; ++k) {
        const S_RigidBodyActorContact &a = b.m_actorContacts[k];
        s += " " + NameOf(Ptr(a.actor)) + " " + V3(&a.pos) + F(" %d %d", a.time, a.point);
    }
    return s;
}
std::vector<std::string> RbState()
{
    std::vector<std::string> out;
    for (int i = 0; i < g_bodyCount; ++i) {
        std::string s = BodyState(i);
        size_t at = 0;
        for (size_t nl; (nl = s.find('\n', at)) != std::string::npos; at = nl + 1)
            out.push_back(s.substr(at, nl - at));
        out.push_back(s.substr(at));
        const I3D_frame *f = AsFrame(g_bodyFrame[g_side][i]);
        out.push_back(F("  frame flags %08x local ", f->m_frameFlags) + VN(&f->m_localMatrix, 16));
    }
    return out;
}

// ---- the cases ----
int g_rbCases, g_opCounts[OP_KINDS];
float RF(int lo, int hi, float scale) { return float(I(lo, hi)) * scale; }
S_RbCase RandomRbCase()
{
    S_RbCase c;
    const int bodies = P(0.6) ? 1 : I(2, MAX_BODIES);
    for (int i = 0; i < bodies; ++i) {
        S_RbBody b;
        b.pos[0] = RF(-20, 20, 0.1f);
        b.pos[1] = P(0.2) ? RF(0, 5, 0.01f) : RF(2, 40, 0.1f);
        b.pos[2] = RF(-20, 20, 0.1f);
        // (Upright along y (no turn), z or x now and then: the topple's axes.)
        const int turn = I(0, 9);
        if (turn == 0) {
            b.rot[0] = 0.70710677f;
            b.rot[1] = 0.70710677f;
        } else if (turn == 1) {
            b.rot[0] = 0.70710677f;
            b.rot[3] = 0.70710677f;
        } else if (turn < 8) {
            float q[4] = {RF(-100, 100, 0.01f), RF(-100, 100, 0.01f), RF(-100, 100, 0.01f), RF(-100, 100, 0.01f)};
            const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (l > 0.01f)
                for (int k = 0; k < 4; ++k)
                    b.rot[k] = q[k] / l;
        }
        b.frameVisual = P(0.2);
        const int boxes = P(0.1) ? 0 : P(0.7) ? 1 : I(2, 3);
        static const float sizes[][3] = {{0.2f, 0.2f, 0.2f}, {0.5f, 0.1f, 0.3f}, {0.1f, 0.8f, 0.1f}, {1.0f, 0.3f, 0.3f},
                                         {0.05f, 0.05f, 0.6f}, {0.3f, 0.3f, 1.2f}, {0.4f, 0.02f, 0.4f}, {0.15f, 0.5f, 0.15f}};
        for (int k = 0; k < boxes; ++k) {
            S_RbBox box;
            const float *s = sizes[I(0, 7)];
            const float off[3] = {RF(-10, 10, 0.01f), RF(-10, 10, 0.01f), RF(-10, 10, 0.01f)};
            for (int a = 0; a < 3; ++a) {
                box.min[a] = -s[a] * 0.5f + off[a];
                box.max[a] = s[a] * 0.5f + off[a];
                box.at[a] = k ? RF(-5, 5, 0.1f) : 0.0f;
            }
            if (P(0.05))
                std::swap(box.min[0], box.max[0]);      // (an empty box)
            b.boxes.push_back(box);
        }
        b.nested = P(0.3);
        b.wheel = P(0.25);
        if (P(0.6)) {
            b.velocity[0] = RF(-50, 50, 0.1f);
            b.velocity[1] = RF(-50, 30, 0.1f);
            b.velocity[2] = RF(-50, 50, 0.1f);
        } else if (P(0.3)) {
            b.velocity[0] = 0.001f;                      // (nearly still)
        }
        static const float weights[] = {1.0f, 10.0f, 0.5f, 80.0f, 25.0f};
        b.weight = weights[I(0, 4)];
        b.friction = RF(0, 10, 0.1f);
        b.radius = RF(10, 50, 0.01f);
        b.width = RF(5, 30, 0.01f);
        b.roll = RF(0, 10, 0.01f);
        b.moveCollision = P(0.5);
        b.restVolume = P(0.5);
        b.callbackMode = I(0, 3);
        c.bodies.push_back(b);
    }
    S_RbGround &g = c.ground;
    g.y = RF(-10, 10, 0.1f);
    if (P(0.4)) {
        g.tiltX = RF(-50, 50, 0.01f);
        g.tiltZ = RF(-50, 50, 0.01f);
    }
    // (Paper 0x55; the waters 0x1F, 0x20, 0x46.)
    static const int materials[] = {1, 2, 5, 0x55, 0x43, 0x1F, 0x20, 0x46, 10};
    g.material = materials[I(0, 8)];
    g.water = P(0.25);
    g.waterY = g.y + RF(1, 15, 0.1f);
    g.waterMaterial = P(0.5) ? materials[I(5, 7)] : materials[I(0, 4)];
    g.platform = P(0.35) ? I(0, MAX_ACTORS - 1) : -1;
    g.platformY = g.y + RF(1, 10, 0.1f);
    g.platformMinX = RF(-30, 0, 0.1f);
    g.platformMaxX = g.platformMinX + RF(5, 40, 0.1f);
    g.platformMaterial = materials[I(0, 8)];
    g.wall = P(0.2);
    g.wallX = RF(5, 30, 0.1f);
    static const int types[] = {ACTOR_CAR, ACTOR_ANIMATION, ACTOR_HUMAN, ACTOR_DOOR};
    for (int &t : c.actorTypes)
        t = types[I(0, 3)];
    // The bodies made ready mostly: their frame set, set up, started; then the ops, ticks the most.
    const int count = P(0.1) ? I(40, 100) : I(10, 45);
    for (int i = 0; i < bodies; ++i) {
        if (P(0.95))
            c.ops.push_back({OP_SET_FRAME, i, 1, 0, 0, 0});
        if (P(0.95))
            c.ops.push_back({OP_SETUP, i, I(0, 1000), 0, 0, 0});
        if (P(0.9))
            c.ops.push_back({OP_START, i, 0, 0, 0, 0});
    }
    // Now and then a body left to settle (the longest frames, up to 3 s): resting, on the platform's actors, toppling.
    // (Half the time the first body a tall box standing upright, still, just above the ground; the actors under it
    // moving now and then.)
    if (P(0.25)) {
        if (P(0.5)) {
            // (Tall along y, z or x, turned so that axis stands up: each of the topple's ways.)
            S_RbBody &b = c.bodies[0];
            const int axis = I(0, 2);
            b.rot[0] = axis ? 0.70710677f : 1.0f;
            b.rot[1] = axis == 1 ? 0.70710677f : 0.0f;
            b.rot[2] = 0.0f;
            b.rot[3] = axis == 2 ? 0.70710677f : 0.0f;
            S_RbBox box{{-0.05f, -0.05f, -0.05f}, {0.05f, 0.05f, 0.05f}, {0.0f, 0.0f, 0.0f}};
            const int tall = axis == 0 ? 1 : axis == 1 ? 2 : 0;
            box.min[tall] = 0.0f;
            box.max[tall] = 0.8f;
            b.boxes.assign(1, box);
            // (Or a wide flat box lying across a narrow platform: on each stripe's actor, five at once.)
            const bool wide = P(0.3);
            if (wide) {
                b.rot[0] = 1.0f;
                b.rot[1] = b.rot[2] = b.rot[3] = 0.0f;
                b.boxes.assign(1, S_RbBox{{-0.6f, 0.0f, -0.2f}, {0.6f, 0.1f, 0.2f}, {0.0f, 0.0f, 0.0f}});
                c.ground.platform = I(0, MAX_ACTORS - 1);
                c.ground.platformMinX = -0.5f;
                c.ground.platformMaxX = 0.5f;
                c.ground.platformY = c.ground.y + 1.0f;
                c.ground.water = false;
            }
            b.frameVisual = false;
            b.wheel = false;
            b.velocity[0] = b.velocity[2] = 0.0f;
            b.velocity[1] = -0.2f;                      // (a still one would start at rest)
            b.pos[1] = (c.ground.platform >= 0 && (wide || P(0.5)) ? c.ground.platformY : c.ground.y) + 0.02f;
            b.pos[0] = c.ground.platform >= 0 ? (c.ground.platformMinX + c.ground.platformMaxX) * 0.5f : 0.0f;
            b.pos[2] = 0.0f;
        }
        const int settle = I(15, 36);
        for (int k = 0; k < settle; ++k) {
            c.ops.push_back({OP_TICK, I(0, bodies - 1), 5, 0, 0, 0});
            if (P(0.15))
                c.ops.push_back({OP_ACTOR_MOVE, 0, I(0, MAX_ACTORS - 1), 2, 0, 0});
        }
    }
    for (int k = 0; k < count; ++k) {
        S_RbOp op;
        const int r = I(0, 99);
        op.kind = r < 2 ? OP_SET_FRAME : r < 4 ? OP_SETUP : r < 6 ? OP_START : r < 8 ? OP_STOP : r < 55 ? OP_TICK : r < 60 ? OP_STEP
                : r < 66 ? OP_APPLY_IMPULSE : r < 71 ? OP_ADD_IMPULSE : r < 74 ? OP_WAKE : r < 76 ? OP_NUDGE : r < 79 ? OP_SET_STATE
                : r < 81 ? OP_SET_OWNER : r < 83 ? OP_FORGET : r < 88 ? OP_FIELDS : r < 92 ? OP_SAVE_LOAD : r < 94 ? OP_BOX
                : r < 98 ? OP_ACTOR_MOVE : OP_PLATFORM;
        op.body = I(0, bodies - 1);
        op.a = I(0, 100000);
        op.b = I(0, 100000);
        op.c = I(0, 100000);
        op.d = I(0, 100000);
        c.ops.push_back(op);
    }
    return c;
}

bool Started(int i) { return Body(i)->m_frame != nullptr; }

using T_RbVoid = void(TC *)(void *);
std::string RunRbOp(const S_RbCase &c, const S_RbOp &op, int index)
{
    const bool o = g_side != 0;
    const int i = op.body < g_bodyCount ? op.body : 0;
    uint8_t *b = g_body[g_side][i];
    C_RigidBody *p = Body(i);
    const S_RbBody &bc = c.bodies[size_t(i)];
    switch (op.kind) {
    case OP_SET_FRAME: {
        uint8_t *frame = g_bodyFrame[g_side][i];
        const bool r = o ? Slot<bool(TC *)(void *, void *)>(b, 0x00)(b, frame) : p->SetFrame(AsFrame(frame));
        return F("SetFrame %d -> %d", i, int(r));
    }
    case OP_SETUP: {
        // (Not without the frame: both read its box through null.)
        if (!Started(i))
            return "Setup (no frame)";
        if (bc.wheel) {
            S_WheelDebrisParams w;
            w.velocity = {bc.velocity[0], bc.velocity[1], bc.velocity[2], 0.0f};
            w.radius = bc.radius;
            w.width = bc.width;
            w.weight = bc.weight;
            w.unknown1C = bc.friction;
            w.unknown20 = bc.roll;
            w.callback = bc.callbackMode ? BodyCallback : nullptr;
            w.moveCollision = bc.moveCollision;
            if (o)
                OriginalAt<void(TC *)(void *, const S_WheelDebrisParams *)>(O_RB_SETUP_WHEEL)(b, &w);
            else
                p->SetupWheel(w);
            return F("SetupWheel %d", i);
        }
        S_RigidBodyParams r;
        r.velocity = {bc.velocity[0], bc.velocity[1], bc.velocity[2], 0.0f};
        r.weight = bc.weight;
        r.friction = bc.friction;
        r.callback = bc.callbackMode ? BodyCallback : nullptr;
        r.context = reinterpret_cast<void *>(uintptr_t(i + 1));
        r.moveCollision = bc.moveCollision;
        r.unknown41 = bc.restVolume;
        if (o)
            OriginalAt<void(TC *)(void *, const S_RigidBodyParams *)>(O_RB_SETUP)(b, &r);
        else
            p->Setup(r);
        return F("Setup %d", i);
    }
    case OP_START:
        if (!Started(i))
            return "Start (no frame)";
        if (o)
            Slot<T_RbVoid>(b, 0x18)(b);
        else
            p->Start();
        return F("Start %d", i);
    case OP_STOP:
        if (o)
            Slot<T_RbVoid>(b, 0x1C)(b);
        else
            p->Stop();
        return F("Stop %d", i);
    case OP_TICK:
    case OP_STEP: {
        if (!Started(i))
            return "Tick (no frame)";
        static const uint32_t times[] = {16, 33, 33, 50, 20, 83, 100, 250, 0, 1, 28, 29, 57, 1000};
        const uint32_t t = times[op.a % 14];
        // (A body set up of mass 0 divides by it: only set-up bodies step.)
        if (p->m_mass == 0.0f)
            return "Tick (not set up)";
        if (op.kind == OP_TICK) {
            if (o)
                Slot<void(TC *)(void *, uint32_t)>(b, 0x24)(b, t);
            else
                p->Tick(t);
        } else if (o)
            Slot<void(TC *)(void *, uint32_t)>(b, 0x20)(b, t);
        else
            p->Step(t);
        return F("%s %d %u", op.kind == OP_TICK ? "Tick" : "Step", i, t);
    }
    case OP_APPLY_IMPULSE:
    case OP_ADD_IMPULSE: {
        if (p->m_mass == 0.0f)
            return "Impulse (not set up)";
        const S_vector4 at = {p->m_centreWorld.x + float(op.b % 11 - 5) * 0.05f, p->m_centreWorld.y + float(op.c % 11 - 5) * 0.05f,
                              p->m_centreWorld.z + float(op.d % 11 - 5) * 0.05f, 1.0f};
        static const float sizes[] = {1.0f, 10.0f, 0.1f, 100.0f, 0.0f, 3.0f};
        const float s = sizes[op.a % 6];
        const S_vector impulse(float(op.b % 7 - 3) * s, float(op.c % 7 - 2) * s, float(op.d % 7 - 3) * s);
        if (op.kind == OP_ADD_IMPULSE) {
            if (o)
                OriginalAt<void(TC *)(void *, const S_vector *, const S_vector4 *)>(O_RB_ADD_IMPULSE)(b, &impulse, &at);
            else
                p->AddImpulse(impulse, at);
            return F("AddImpulse %d ", i) + V3(&impulse) + " " + V3(&at);
        }
        const bool r = o ? Slot<bool(TC *)(void *, int, const S_vector4 *, const S_vector *, int, int, int)>(b, 0x04)(b, 0, &at, &impulse, 0, 0, 0)
                         : p->ApplyImpulse(0, at, impulse, 0, 0, 0);
        return F("ApplyImpulse %d ", i) + V3(&impulse) + " " + V3(&at) + F(" -> %d", int(r));
    }
    case OP_WAKE: {
        const bool r = o ? Slot<bool(TC *)(void *, int, int, int, int, int, int, int, int)>(b, 0x08)(b, 0, 0, 0, 0, 0, 0, 0, 0)
                         : p->Wake(0, 0, 0, 0, 0, 0, 0, 0);
        return F("Wake %d -> %d", i, int(r));
    }
    case OP_NUDGE:
        if (o)
            Slot<T_RbVoid>(b, 0x14)(b);
        else
            p->Nudge();
        return F("Nudge %d", i);
    case OP_SET_STATE: {
        // (Not at rest without the frame with the rest volume: the box is the frame's.)
        const int state = op.a % 3;
        if (state != RIGID_BODY_MOVING && p->m_restVolume && !Started(i))
            return "SetState (no frame)";
        if (o)
            OriginalAt<void(TC *)(void *, int)>(O_RB_SET_STATE)(b, state);
        else
            p->SetState(state);
        return F("SetState %d %d", i, state);
    }
    case OP_SET_OWNER: {
        uint8_t *owner = op.a % 4 ? g_actor[g_side][op.a % MAX_ACTORS] : nullptr;
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(O_RB_SET_OWNER)(b, owner);
        else
            p->SetOwner(reinterpret_cast<C_Actor *>(owner));
        return F("SetOwner %d %s", i, NameOf(Ptr(owner)).c_str());
    }
    case OP_FORGET: {
        uint8_t *actor = op.a % 5 ? g_actor[g_side][op.a % MAX_ACTORS] : nullptr;
        if (op.b % 2) {
            if (o)
                OriginalAt<void(__attribute__((fastcall)) *)(void *)>(O_FORGET_ACTOR)(actor);   // (ECX)
            else
                ForgetActor(reinterpret_cast<C_Actor *>(actor));
            return F("ForgetActor (all) %s", NameOf(Ptr(actor)).c_str());
        }
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(O_RB_FORGET)(b, actor);
        else
            p->ForgetActor(reinterpret_cast<C_Actor *>(actor));
        return F("ForgetActor %d %s", i, NameOf(Ptr(actor)).c_str());
    }
    case OP_FIELDS: {
        // The owners' fields: the hit sound's material, the flat stop, the actors ignored, the step.
        static const float steps[] = {0.028f, 0.01f, 0.05f, 0.1f};
        p->m_hitMaterial = op.a % 4 == 0 ? -1 : op.a % 40;
        p->m_stopOnFlat = op.b % 3 == 0;
        p->m_ignoreActors = op.c % 4 == 0;
        p->m_maxStep = steps[op.d % 4];
        return F("Fields %d %d %d %d ", i, p->m_hitMaterial, int(p->m_stopOnFlat), int(p->m_ignoreActors)) + Fl(p->m_maxStep);
    }
    case OP_SAVE_LOAD: {
        if (!Started(i))
            return "SaveLoad (no frame)";
        const std::string file = F("rb_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? Slot<bool(TC *)(void *, void *)>(b, 0x0C)(b, &save) : p->SaveGame(save);
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        // (The contact point's bytes (+0x80..+0x8B of the record) only while the disc touches: see BodyState.)
        std::string shown = bytes;
        if (!(p->m_springForce > 0.0f) && shown.size() >= 6 + 0x8C)
            for (size_t k = 6 + 0x80; k < 6 + 0x8C; ++k)
                shown[k] = 0;
        // (Nor the float fields' denormals: see Fl.)
        for (size_t k = 0x08; k + 4 <= 0xA4; k += 4) {
            if ((k >= 0x94 && k < 0xA0) || shown.size() < 6 + k + 4)
                continue;
            uint32_t w;
            std::memcpy(&w, &shown[6 + k], 4);
            if ((w & 0x7F800000u) == 0 && (w & 0x007FFFFFu) != 0)
                std::memset(&shown[6 + k], 0x7E, 4);
        }
        // (Loaded back into this body or another started one; a version from another now and then.)
        bool rl = false;
        int into = op.b % g_bodyCount;
        if (!Started(into))
            into = i;
        if (op.c % 3) {
            std::string data = bytes;
            if (op.c % 7 == 0 && data.size() >= 10)
                data[6] = 4;                            // (another version)
            g_overrides[Lower(file + "l")] = data;
            C_chunk chunk;
            chunk.Open((file + "l").c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            uint8_t *ib = g_body[g_side][into];
            rl = o ? Slot<bool(TC *)(void *, void *)>(ib, 0x10)(ib, &chunk) : Body(into)->LoadGame(chunk);
            chunk.Close();
        }
        return F("SaveGame %d -> %d LoadGame %d -> %d bytes ", i, int(rs), into, int(rl)) + Hex(shown);
    }
    case OP_BOX: {
        uint8_t *frame = g_bodyFrame[g_side][i];
        const I3D_bbox *box = o ? OriginalAt<const I3D_bbox *(__attribute__((fastcall)) *)(void *)>(O_HIERARCHY_BOX)(frame)   // (ECX)
                                : &GetHierarchyBox(AsFrame(frame));
        return F("GetHierarchyBox %d ", i) + V3(&box->min) + " " + V3(&box->max);
    }
    case OP_PLATFORM:
        // The platform taken away (its actors' records age) or put back.
        g_platformOn = !g_platformOn;
        return F("Platform %d", int(g_platformOn));
    default: {
        // An actor moved (those under a body wake it), or the platform's.
        const int a = op.a % MAX_ACTORS;
        uint8_t *actor = g_actor[g_side][a];
        static const float moves[] = {0.0f, 0.001f, 0.01f, 0.5f};
        const float m = moves[op.b % 4];
        S_vector pos = g_side ? *reinterpret_cast<const S_vector *>(actor + 0x30) : reinterpret_cast<C_Actor *>(actor)->GetPosition();
        pos.x += m;
        if (g_side)
            std::memcpy(actor + 0x30, &pos, 12);
        else
            reinterpret_cast<C_Actor *>(actor)->SetPosition(pos);
        return F("ActorMove %d ", a) + Fl(m);
    }
    }
}

volatile DWORD g_caseStart;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("rb_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_rbShow;
void RunRbCase(const S_RbCase &c)
{
    g_caseStart = GetTickCount();
    ++g_rbCases;
    g_holdFrees = true;
    g_current = F("case %d", g_rbCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_ground = c.ground;
    for (int i = 0; i < int(c.bodies.size()); ++i)
        g_callbackMode[i] = c.bodies[size_t(i)].callbackMode;
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        BuildWorld(side, worldSeed);
        BuildRbWorld(c);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        g_callbackCount[side] = 0;
        g_platformOn = true;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        for (int i = 0; i < g_bodyCount; ++i) {
            uint8_t *b = Block(0x600 + 16);
            if (side)
                OriginalAt<T_RbVoid>(O_RB_CTOR)(b);
            else
                new (b) C_RigidBody();
            g_body[side][i] = b;
            Name(b, F("body %d", i));
            Name(b + 0x410, F("volume %d", i));
        }
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_log[side].clear();
        for (const std::string &s : RbState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_RB_TRACE") ? std::atoi(std::getenv("HD2_RB_TRACE")) : -1;
        for (size_t k = 0; k < c.ops.size(); ++k) {
            ++g_opCounts[c.ops[k].kind];
            if (trace == g_rbCases)
                std::printf("trace %s op %d kind %s body %d a %d b %d c %d d %d\n", SIDE_NAMES[side], int(k), OP_KIND_NAMES[c.ops[k].kind],
                            c.ops[k].body, c.ops[k].a, c.ops[k].b, c.ops[k].c, c.ops[k].d);
            out.push_back(RunRbOp(c, c.ops[k], int(k)));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            // (After the ticks and steps every third time, their calls show each step's place; the rest after each.)
            const bool ticking = c.ops[k].kind == OP_TICK || c.ops[k].kind == OP_STEP;
            if (!ticking || k % 3 == 0 || k + 1 == c.ops.size())
                for (const std::string &s : RbState())
                    out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        for (int i = 0; i < g_bodyCount; ++i) {
            uint8_t *b = g_body[side][i];
            if (side) {
                Slot<T_RbVoid>(b, 0x1C)(b);
                OriginalAt<T_RbVoid>(O_RB_DTOR)(b);
            } else {
                Body(i)->Stop();
                Body(i)->~C_RigidBody();
            }
        }
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        for (int i = 0; i < MAX_BODIES; ++i)
            g_body[side][i] = nullptr;
    }
    static const int only = std::getenv("HD2_RB_ONLY") ? std::atoi(std::getenv("HD2_RB_ONLY")) : 0;
    if (g_rbShow && (!only || only == g_rbCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_rbShow == 2)
            for (const std::string &s : r.orig)
                std::printf("  orig: %s\n", s.c_str());
    }
    Compare(g_current, r);
    ReleaseHeldFrees();
    if (!g_overrun.empty()) {
        ++failures;
        std::printf("FAIL %s: the original wrote past a block\n", g_overrun.c_str());
        g_overrun.clear();
    }
    FreeWorld(0);
    FreeWorld(1);
}

void SetUpRb()
{
    SetUpCmd();
    SetUpRbAnswers();
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x007e53aa, reinterpret_cast<const void *>(&O_dtaOpenWrite)},
        {0x007e5398, reinterpret_cast<const void *>(&O_dtaSeek)},
        {0x007e5392, reinterpret_cast<const void *>(&O_dtaWrite)},
        {0x007e562e, reinterpret_cast<const void *>(&O_Atexit)},
        {0x007e53c2, reinterpret_cast<const void *>(&O_AlignedMalloc)},
        {0x007e559c, reinterpret_cast<const void *>(&O_AlignedFree)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("rb_diff: main");
    std::fflush(stdout);
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("rb_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_RB_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_RB_SHOW"))
        g_rbShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_RB_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_RB_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpRb();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunRbCase(RandomRbCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("rb_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_rbCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
