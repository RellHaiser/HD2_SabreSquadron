// Door part 1: the doors (game/door.cpp, the original's 0x005ff150..0x00602a10) against the original executable's.
// cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the doors' calls go through dr_calls.s /
// dr_calls.inc (gen_cmd_calls.py dr). The LS3DF math (quaternions, GetDir, the inverse, the products) is LS3DF.dll's on
// both sides; the frames' material is the original's GetFrameMaterialId on both sides; a script's OnUse is logged on
// both sides (the port's patched, the original's redirected).
//
// Each case builds the same world on each side (cmd_diff's) and one or two doors (two: now and then each the other's
// half): frames in the primary sector (turned) with a mesh and handles ("DHandle", "x_d2handle"; now and then missing),
// frame data naming the door (and a script or none), the sector's portals (its own or incoming ones, near and far, each
// with its opposite or none), makes the doors on each side (the original's by its constructor 0x005ff210 in a block of
// 0x270, the port's by its own) and runs the same random sequence of operations in the case's role: properties read
// (made ones: hinged and sliding, axes, keys, every chunk, foreign ones) and written, CopyFrom, starts (server and
// client sides), stops, restarts, SetState (open, close, others; forced, closing by itself, linked), Unlock, Lock,
// PickLock, GetSide, ticks and updates (the delay, the handles, the swing and the slide, blocked by dynamic volumes,
// closing by itself near the player or not), the clients' messages, the network state, the saved game's calls (the
// file just written, made ones), GetMaterialId, the player moved. After each operation the answer, every door's state
// (every field, its volume), the frames and every call made into the game and the engine are compared; the properties'
// and the saves' bytes too. The creator makes and destroys one.
// MinGW; build_dr.bat; runs dr_diff.exe ("dr_diff: N checks, 0 failures"). Env: HD2_DR_CASES (default 1,500;
// HD2_DR_QUICK: 300), HD2_DR_SEED, HD2_DR_SHOW (1: the port's lines; 2: both), HD2_DR_ONLY (the case shown),
// HD2_DR_TRACE (a case's operations as they start).
#define HARNESS_NAME "dr_diff"
#define CMD_CALLS_INC "dr_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/door.h"
#include "game/game_texts.h"
#include "script/script.h"
#undef protected
#undef private

// ---- the port's side of what the doors call and the test provides ----
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpenWrite(name); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
namespace H {
void LogDebugText(const char *format, va_list args)
{
    char text[2048];
    std::vsnprintf(text, sizeof text, format, args);
    if (*g_case)
        g_log[g_side].push_back(std::string("dbgPrintf ") + text);
}
void __cdecl O_dbgPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogDebugText(format, args);
    va_end(args);
}
int __cdecl O_Sprintf(char *out, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = std::vsprintf(out, format, args);
    va_end(args);
    return n;
}
}  // namespace H
extern "C" void dbgPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    H::LogDebugText(format, args);
    va_end(args);
}

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
S_vector __stdcall S_matrix::GetScale() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_matrix *, float *)>("?GetScale@S_matrix@@QBG?AUS_vector@@XZ");
    alignas(16) float r[4] = {};
    f(this, r);
    return S_vector(r[0], r[1], r[2]);
}
void __stdcall S_quat::Make(const S_vector &axis, float angle)
{
    static const auto f = Ls3dfFn<void(__stdcall *)(S_quat *, const S_vector &, float)>("?Make@S_quat@@QAGXABUS_vector@@M@Z");
    f(this, axis, angle);
}
S_vector __stdcall S_quat::GetDir() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, float *)>("?GetDir@S_quat@@QBG?AUS_vector@@XZ");
    alignas(16) float r[4] = {};
    f(this, r);
    return S_vector(r[0], r[1], r[2]);
}
// A frame's "matid" (the original's own function on both sides: not logged).
int GetFrameMaterialId(const I3D_frame *frame) { return OriginalAt<int(__stdcall *)(const I3D_frame *)>(0x00611090)(frame); }
// The port's allocations zeroed, as the original's (its _aligned_malloc redirected to a zeroing one).
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
// The actors' creators (the doors' registers itself before main: a table of the test's).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the door's vtable holds but the test never reaches.
void C_Actor::Init() {}
void C_Actor::Done() {}
int C_Actor::GetSeatAmmoCount(int) { return 0; }
void C_Actor::GetBoxCorners(S_vector4 *) {}

namespace H {
std::string TextOf(int id) { return F("T%X", id); }
}  // namespace H
C_str C_GameTexts::Get(int id) const
{
    H::g_log[H::g_side].push_back(H::F("texts %d", id));
    return C_str(H::TextOf(id).c_str());
}

namespace H {

#define OT __attribute__((thiscall))
void *OT O_TextsGet(void *, void *ret, int id)
{
    g_log[g_side].push_back(F("texts %d", id));
    *reinterpret_cast<uint32_t *>(ret) = 0;
    OriginalAt<T_Fstr>(0x00405100)(ret, "%s", TextOf(id).c_str());
    return ret;
}
int __cdecl O_Atexit(void *) { return 0; }
void *__cdecl O_AlignedMalloc(size_t size, size_t align)
{
    void *p = _aligned_malloc(size ? size : 1, align);
    std::memset(p, 0, size ? size : 1);
    return p;
}
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }
// A script's OnUse (the port's own patched to this at the start; the original's redirected): logged.
void OT B_OnUse(void *script, int action)
{
    g_log[g_side].push_back(F("C_Script::OnUse(%s, %d)", NameOf(reinterpret_cast<uint32_t>(script)).c_str(), action));
}
void PatchPort(void *at, const void *to)
{
    DWORD old;
    VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
    uint8_t *p = static_cast<uint8_t *>(at);
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (reinterpret_cast<uintptr_t>(p) + 5));
    p[0] = 0xE9;
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
}

uint32_t Ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
template <class T> T &Mut(void *p, uint32_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }
std::deque<std::string> g_texts[2];
const char *Text(const std::string &s)
{
    g_texts[g_side].push_back(s);
    return g_texts[g_side].back().c_str();
}
uint8_t *Child(uint8_t *parent, int type, const std::string &name, uint8_t *owner)
{
    uint8_t *b = NewFrame(type, name);
    S_FakeFrame &f = W().frames.back();
    I3D_frame *x = AsFrame(b);
    x->m_name = f.name.c_str();
    x->m_ownerModel = reinterpret_cast<I3D_frame *>(owner);
    x->m_frameFlags |= 1;
    if (parent)
        LinkUnder(b, parent);
    return b;
}

// ---- the doors ----
constexpr int MAX_DOORS = 2, MAX_PORTALS = 4;
uint8_t *g_door[2][MAX_DOORS];
uint8_t *g_doorFrame[2][MAX_DOORS];
uint8_t *g_portal[2][MAX_PORTALS];
uint8_t *g_sound[2];
int g_doorCount;
C_Door *PortDoor(int i) { return reinterpret_cast<C_Door *>(g_door[0][i]); }
constexpr uint32_t O_DR_CTOR = 0x005ff210, O_DR_DTOR = 0x005ff3a0, O_DR_SET_STATE = 0x005ff4b0, O_DR_UNLOCK = 0x005ff5f0,
                   O_DR_LOCK = 0x005ff650, O_DR_PICK = 0x005ff6c0, O_DR_SIDE = 0x00602320, O_DR_UPDATE = 0x00600940,
                   O_DR_TICK = 0x00600920, O_DR_MESSAGE = 0x00601f90, O_DR_WRITE = 0x006013b0, O_DR_READ = 0x00601ac0,
                   O_DR_COPY = 0x005ff3e0, O_DR_START = 0x00600cc0, O_DR_STOP = 0x00601350, O_DR_RESTART = 0x00602400,
                   O_DR_SAVE = 0x00602500, O_DR_LOAD = 0x00602700, O_DR_WRITE_STATE = 0x006021e0, O_DR_READ_STATE = 0x00602290,
                   O_DR_MATERIAL = 0x006023b0;
constexpr uint32_t O_DR_CREATOR = 0x008ac9f8, O_DR_CREATE = 0x005ff180, O_DR_DESTROY = 0x005ff1f0;

// The messages' words (ReadBits answers them in turn on each side); whether the dynamic volumes block (per ray, by turns).
std::deque<uint32_t> g_bits[2];
int g_blockEvery = 0;
uint8_t *g_player[2];

void SetUpDoorAnswers()
{
    g_answers["I3D_frame::SetOn"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_frameFlags = (a[1] & 0xFF) ? (f->m_frameFlags | 1u) : (f->m_frameFlags & ~1u);
        return 0u;
    };
    // A frame's position: its local matrix's, its matrices made again (its children's too).
    g_answers["I3D_frame::SetPos"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[1]);
        f->m_localMatrix.m[3][0] = pos.x;
        f->m_localMatrix.m[3][1] = pos.y;
        f->m_localMatrix.m[3][2] = pos.z;
        std::function<void(I3D_frame *)> stale = [&](I3D_frame *x) {
            x->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
            for (I3D_frame *c = x->m_firstChild; c; c = c->m_nextSibling)
                stale(c);
        };
        stale(f);
        return 0u;
    };
    g_answers["I3D_frame::LinkTo"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *c = reinterpret_cast<I3D_frame *>(a[0]);
        if (c->m_parent) {
            I3D_frame **link = &c->m_parent->m_firstChild;
            while (*link && *link != c)
                link = &(*link)->m_nextSibling;
            if (*link)
                *link = c->m_nextSibling;
        }
        c->m_parent = nullptr;
        c->m_nextSibling = nullptr;
        if (I3D_frame *p = reinterpret_cast<I3D_frame *>(a[1])) {
            c->m_parent = p;
            c->m_nextSibling = p->m_firstChild;
            p->m_firstChild = c;
        }
        c->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
        return 0u;
    };
    // A hierarchy's box: filled and returned (min +0, max +0x10), by turns (none empty).
    g_answers["I3D_driver::GetHierarchyBBox"] = [](const std::vector<uint32_t> &a) {
        float *box = reinterpret_cast<float *>(a[1]);
        static const float boxes[][6] = {{0.0f, 0.0f, -0.05f, 1.0f, 2.0f, 0.05f}, {-0.5f, 0.0f, -0.1f, 0.5f, 2.2f, 0.1f},
                                         {0.0f, -0.1f, -0.08f, 0.9f, 1.9f, 0.08f}};
        const float *b = boxes[NextFake() % 3];
        const float v[8] = {b[0], b[1], b[2], 0.0f, b[3], b[4], b[5], 0.0f};
        std::memcpy(box, v, sizeof v);
        return a[1];
    };
    // The dynamic volumes: a ray blocked now and then (the case's rate; 0 never).
    g_answers["C_CollisionWorld::RayTestDynamic"] = [](const std::vector<uint32_t> &a) {
        if (!g_blockEvery || NextFake() % uint32_t(g_blockEvery))
            return 0u;
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[3]))
            *hit = *reinterpret_cast<const S_vector *>(a[1]);
        return Ptr(W().actors.empty() ? nullptr : W().actors[0].bytes);
    };
    // The sounds: a slot for most, -1 now and then; the slot's sound. (Not a kept slot without one: the original's
    // Animate calls through it unchecked, the port's checks; the moving sound's slot is kept, its sound there.)
    g_answers["C_SoundModule::Play"] = [](const std::vector<uint32_t> &) {
        const uint32_t r = NextFake() % 6;
        return r == 5 ? uint32_t(-1) : r;
    };
    g_answers["C_SoundModule::GetSound"] = [](const std::vector<uint32_t> &) { return Ptr(g_sound[g_side]); };
    g_answers["I3D_sound::GetPosition"] = [](const std::vector<uint32_t> &) { return 1234u; };
    // The player: the world's first human, or none now and then (the case's).
    g_answers["C_Team::GetActivePlayer"] = [](const std::vector<uint32_t> &) { return Ptr(g_player[g_side]); };
    // The messages: the world's bit stream; their words in turn.
    g_answers["CreateActorMessage"] = [](const std::vector<uint32_t> &) { return Ptr(W().bitstream); };
    g_answers["C_BitStream::ReadBits"] = [](const std::vector<uint32_t> &) {
        if (g_bits[g_side].empty())
            return 0u;
        const uint32_t w = g_bits[g_side].front();
        g_bits[g_side].pop_front();
        return w;
    };
    // A sector by position: the primary one.
    // A frame's bound: the door mesh's centre (0.5, 1, 0 in the frame) turned by the frame's rotation (q v q*), radius
    // 1.1, made valid.
    g_answers["I3D_frame::UpdateBound"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        const float s = f->m_rot.s, qx = f->m_rot.v.x, qy = f->m_rot.v.y, qz = f->m_rot.v.z;
        const float vx = 0.5f, vy = 1.0f, vz = 0.0f;
        // t = 2 (q.v x v); v' = v + s t + q.v x t
        const float tx = 2.0f * (qy * vz - qz * vy), ty = 2.0f * (qz * vx - qx * vz), tz = 2.0f * (qx * vy - qy * vx);
        f->m_bound.center = S_vector(vx + s * tx + (qy * tz - qz * ty), vy + s * ty + (qz * tx - qx * tz), vz + s * tz + (qx * ty - qy * tx));
        f->m_bound.radius = 1.1f;
        f->m_frameFlags |= FRMFLAGS_BOUND_VALID;
        return 0u;
    };
    g_answers["I3D_scene::GetSector"] = [](const std::vector<uint32_t> &) {
        return Ptr(*reinterpret_cast<uint8_t **>(W().scene + 0x1F8));
    };
}

// ---- the compared state ----
std::string V3(const float *v) { return VectorText(Ptr(v), 3); }
std::string V4(const float *v) { return VectorText(Ptr(v), 4); }
std::string Fl(float f) { return FloatText(f); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::string RepText(uint32_t rep) { return rep ? std::string(reinterpret_cast<const char *>(rep + 8)) : std::string(); }
std::string FrameText(const I3D_frame *f)
{
    if (!f)
        return "none";
    return F("%s flags %08x at ", f->m_name ? f->m_name : "", f->m_frameFlags) + VectorText(Ptr(&f->m_localMatrix.m[3][0]), 3) +
           " rot " + VectorText(Ptr(&f->m_rot), 4) + " parent " + NameOf(Ptr(f->m_parent));
}
// The volume (S_CollisionVolume: the same layout on both sides; its inverse matrix the door's own).
std::string VolumeText(const S_CollisionVolume &v, const void *inv)
{
    return F("volume flags %x type %x frame %s actor %s matrix %s inv %s radius ", v.flags, v.type, NameOf(Ptr(v.frame)).c_str(),
             NameOf(Ptr(v.actor)).c_str(), NameOf(Ptr(v.matrix)).c_str(), v.invMatrix == inv ? "own" : NameOf(Ptr(v.invMatrix)).c_str()) +
           Fl(v.radius) + " centre " + V4(&v.center.x) + " box " + V3(&v.bbox.min.x) + " " + V3(&v.bbox.max.x);
}
// (The fields at the same offsets on both sides: the port's header gives the original's.)
std::vector<std::string> DoorState(int i)
{
    std::vector<std::string> out;
    const uint8_t *b = g_door[g_side][i];
    const C_Door *p = PortDoor(i);
    const bool o = g_side != 0;
#define FIELD(T, member, off) (o ? At<T>(b, off) : T(p->member))
    std::string sounds;
    for (int k = 0; k < 9; ++k)
        sounds += F(" %d", o ? At<int>(b, 0x240 + 4 * k) : p->m_sounds[k]);
    out.push_back(F("door%d started %d u94 %d autoClose %d u99 %d closeTime %d portals %d movingSound %d pos %d paused %d portal %s %s "
                    "handles %s %s moving %d rising %d text %x u118 %d other %s otherName %s initial %d %d shoot %d",
                    i, int(FIELD(uint8_t, m_started, 0x90)), FIELD(int, m_unknown94, 0x94), int(FIELD(uint8_t, m_autoClose, 0x98)),
                    int(FIELD(uint8_t, m_unknown99, 0x99)), FIELD(int, m_autoCloseTime, 0x9C), int(FIELD(uint8_t, m_hasPortals, 0xA0)),
                    FIELD(int, m_movingSound, 0xF0), FIELD(int, m_movingSoundPos, 0xF4), int(FIELD(uint8_t, m_movingSoundPaused, 0xF8)),
                    NameOf(o ? At<uint32_t>(b, 0xFC) : Ptr(p->m_portal)).c_str(), NameOf(o ? At<uint32_t>(b, 0x100) : Ptr(p->m_otherPortal)).c_str(),
                    NameOf(o ? At<uint32_t>(b, 0x104) : Ptr(p->m_handle)).c_str(), NameOf(o ? At<uint32_t>(b, 0x108) : Ptr(p->m_handle2)).c_str(),
                    int(FIELD(uint8_t, m_handleMoving, 0x10C)), int(FIELD(uint8_t, m_handleRising, 0x10D)), FIELD(int, m_textId, 0x114),
                    int(FIELD(uint8_t, m_unknown118, 0x118)), NameOf(o ? At<uint32_t>(b, 0x11C) : Ptr(p->m_otherHalf)).c_str(),
                    o ? RepText(At<uint32_t>(b, 0x120)).c_str() : (p->m_otherHalfName.c_str() ? p->m_otherHalfName.c_str() : ""),
                    FIELD(int, m_initialMotion, 0x124), int(FIELD(uint8_t, m_initialLocked, 0x128)), int(FIELD(uint8_t, m_shootThrough, 0x190))));
    out.push_back(F("  kind %d motion %d key %d locked %d pickable %d delay %d u23c %d openDelay %d sounds%s handle ", FIELD(int, m_kind, 0x220),
                    FIELD(int, m_motion, 0x224), FIELD(int, m_key, 0x230), int(FIELD(uint8_t, m_locked, 0x234)), int(FIELD(uint8_t, m_pickable, 0x235)),
                    FIELD(int, m_delay, 0x268), int(FIELD(uint8_t, m_unknown23C, 0x23C)), FIELD(int, m_openDelay, 0x238), sounds.c_str()) +
                  Fl(FIELD(float, m_handleAngle, 0x110)) + " open " + Fl(FIELD(float, m_openAngle, 0x228)) + " speed " + Fl(FIELD(float, m_speed, 0x22C)) +
                  " opened " + Fl(FIELD(float, m_opened, 0x264)) + " name " + (o ? RepText(At<uint32_t>(b, 0x80)) : std::string(p->m_name.c_str() ? p->m_name.c_str() : "")));
    // (The axis's w is not set by the original: left out.)
    out.push_back("  startPos " + V4(o ? FP(b, 0x1E0) : &p->m_startPos.x) + " startRot " + V4(o ? FP(b, 0x1F0) : reinterpret_cast<const float *>(&p->m_startRot)) +
                  " facing " + V3(o ? FP(b, 0x200) : &p->m_facing.x) + " axis " + V3(o ? FP(b, 0x210) : &p->m_axis.x));
    const uint32_t sound = o ? Ptr(b + 0xB0) : Ptr(&p->m_sound);
    out.push_back(F("  sound chapter %d group %d flags %x attach %s source %s pos ", At<int>(reinterpret_cast<const void *>(sound), 0x1C),
                    At<int>(reinterpret_cast<const void *>(sound), 0x20), At<uint32_t>(reinterpret_cast<const void *>(sound), 0x10),
                    NameOf(At<uint32_t>(reinterpret_cast<const void *>(sound), 0x14)).c_str(),
                    NameOf(At<uint32_t>(reinterpret_cast<const void *>(sound), 0x18)).c_str()) +
                  V4(reinterpret_cast<const float *>(sound)));
    out.push_back("  inv " + VectorText(o ? Ptr(b + 0x1A0) : Ptr(&p->m_invMatrix), 16));
    out.push_back("  " + VolumeText(o ? *reinterpret_cast<const S_CollisionVolume *>(b + 0x130) : p->m_volume, o ? static_cast<const void *>(b + 0x1A0) : static_cast<const void *>(&p->m_invMatrix)));
#undef FIELD
    return out;
}
std::vector<std::string> DoorsState()
{
    std::vector<std::string> out;
    for (int i = 0; i < g_doorCount; ++i)
        for (const std::string &s : DoorState(i))
            out.push_back(s);
    for (int i = 0; i < g_doorCount; ++i) {
        const I3D_frame *f = AsFrame(g_doorFrame[g_side][i]);
        out.push_back(F("frame%d ", i) + FrameText(f));
        for (const I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling)
            out.push_back("  " + FrameText(c));
    }
    for (int k = 0; k < MAX_PORTALS; ++k)
        if (g_portal[g_side][k])
            out.push_back(F("portal%d flags %08x", k, At<uint32_t>(g_portal[g_side][k], 0x1F0)));
    return out;
}

// ---- one case ----
struct S_Writer {
    std::string out;
    std::vector<size_t> open;
    void Begin(uint16_t id)
    {
        open.push_back(out.size());
        out.append(reinterpret_cast<const char *>(&id), 2);
        out.append(4, '\0');
    }
    void End()
    {
        const size_t at = open.back();
        open.pop_back();
        const uint32_t size = uint32_t(out.size() - at);
        std::memcpy(&out[at + 2], &size, 4);
    }
    void Raw(const void *p, size_t n) { out.append(static_cast<const char *>(p), n); }
    template <class T> void EntryOf(uint16_t id, const T &v)
    {
        Begin(id);
        Raw(&v, sizeof v);
        End();
    }
};
enum { OP_READ, OP_WRITE, OP_COPY, OP_START, OP_STOP, OP_RESTART, OP_SET_STATE, OP_UNLOCK, OP_LOCK, OP_PICK, OP_SIDE, OP_TICK,
       OP_UPDATE, OP_MESSAGE, OP_NET_STATE, OP_SAVE_LOAD, OP_MATERIAL, OP_PLAYER, OP_AUTO_CLOSE, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"ReadProperties", "WriteProperties", "CopyFrom", "Start", "Stop", "Restart", "SetState", "Unlock",
                                     "Lock", "PickLock", "GetSide", "Tick", "Update", "Message", "NetState", "SaveLoad", "Material",
                                     "Player", "AutoClose"};
struct S_DrOp {
    int kind = 0;
    int door = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_DrDoor {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    bool handle = true, handle2 = true, script = false;
    int handleOdd = 0;                                  // (1, 2: the second handle's z axis tiny along x, along z)
};
struct S_DrCase {
    std::vector<S_DrDoor> doors;
    std::vector<S_DrOp> ops;
    uint32_t role = GAME_ROLE_LOCAL;
    bool halves = false;
    int portals = 0, incoming = 0;
    float portalAt[MAX_PORTALS][4] = {};                // (x, y, z, radius)
    int blockEvery = 0;
    bool player = true;
};
int g_drCases, g_opCounts[OP_KINDS];
const int FRAME_TIMES[] = {0, 16, 100, 33, 500, 1000, 2000, 199, 200, 201, 15000, 300};
S_DrCase RandomDrCase()
{
    S_DrCase c;
    const int doors = P(0.6) ? 1 : 2;
    for (int i = 0; i < doors; ++i) {
        S_DrDoor l;
        l.x = float(I(-20, 20)) * 0.5f + float(i);
        l.y = float(I(-4, 4)) * 0.5f;
        l.z = float(I(-20, 20)) * 0.5f;
        l.heading = float(I(0, 628)) * 0.01f;
        l.handle = P(0.8);
        l.handle2 = P(0.5);
        l.script = P(0.5);
        l.handleOdd = P(0.25) ? I(1, 3) : 0;
        c.doors.push_back(l);
    }
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    c.halves = doors == 2 && P(0.6);
    // (The sector's portals: its own, or incoming ones only; about the first door, some within reach.)
    c.portals = P(0.7) ? I(0, MAX_PORTALS) : 0;
    c.incoming = c.portals ? 0 : I(0, 2);
    for (int k = 0; k < MAX_PORTALS; ++k) {
        c.portalAt[k][0] = float(I(-30, 30)) * 0.5f;
        c.portalAt[k][1] = float(I(-4, 8)) * 0.5f;
        c.portalAt[k][2] = float(I(-30, 30)) * 0.5f;
        static const float radii[] = {2.0f, 30.0f, 0.5f, 12.0f};
        c.portalAt[k][3] = radii[I(0, 3)];
    }
    c.blockEvery = P(0.5) ? 0 : I(2, 30);
    c.player = P(0.8);
    const int count = P(0.1) ? I(40, 120) : I(4, 40);
    for (int i = 0; i < count; ++i) {
        S_DrOp op;
        const int r = I(0, 99);
        op.kind = r < 5 ? OP_READ : r < 7 ? OP_WRITE : r < 9 ? OP_COPY : r < 13 ? OP_START : r < 15 ? OP_STOP : r < 17 ? OP_RESTART
                : r < 30 ? OP_SET_STATE : r < 33 ? OP_UNLOCK : r < 36 ? OP_LOCK : r < 38 ? OP_PICK : r < 40 ? OP_SIDE : r < 55 ? OP_TICK
                : r < 72 ? OP_UPDATE : r < 80 ? OP_MESSAGE : r < 84 ? OP_NET_STATE : r < 92 ? OP_SAVE_LOAD : r < 94 ? OP_MATERIAL : r < 96 ? OP_PLAYER : OP_AUTO_CLOSE;
        op.door = I(0, doors - 1);
        op.a = I(0, 1000);
        op.b = I(0, 1000);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    // Properties read first, mostly; then started.
    for (int i = doors; i-- > 0;) {
        if (P(0.9)) {
            S_DrOp start;
            start.kind = OP_START;
            start.door = i;
            start.c = I(0, 3);
            c.ops.insert(c.ops.begin(), start);
        }
    }
    for (int i = doors; i-- > 0;) {
        if (P(0.85)) {
            S_DrOp read;
            read.kind = OP_READ;
            read.door = i;
            read.a = I(0, 1000);
            read.b = I(0, 1000);
            read.c = I(0, 1000);
            read.d = I(0, 1000);
            c.ops.insert(c.ops.begin(), read);
        }
    }
    return c;
}

using T_DrInt = void(TC *)(void *, int);
using T_DrChunk = bool(TC *)(void *, void *);
using T_DrVoid = void(TC *)(void *, void *);
std::string Hex(const std::string &bytes)
{
    std::string hex;
    for (unsigned char ch : bytes)
        hex += F("%02x", ch);
    return hex;
}
std::string RunDrOp(const S_DrOp &op, int index, const S_DrCase &c)
{
    const bool o = g_side != 0;
    const int li = op.door < g_doorCount ? op.door : 0;
    uint8_t *b = g_door[g_side][li];
    C_Door *p = PortDoor(li);
    switch (op.kind) {
    case OP_READ: {
        // The fields, then the chunks (each now and then, any order, a foreign one now and then).
        S_Writer w;
        w.Begin(0xAE40);
        const int kind = op.a % 4 == 0 ? 1 : 0;
        static const int motions[] = {0, 0, 1, 2, 3, 0};
        const int motion = motions[op.b % 6];
        static const float angles[] = {1.5707964f, 1.0f, -1.5707964f, 0.8f, 2.0f};
        static const float speeds[] = {1.5707964f, 3.0f, 0.5f, 10.0f};
        // (Axes along x only in part among them: the tests for a turn about x.)
        static const float axes[][3] = {{0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                                        {-1.0f, 0.5f, 0.0f}, {0.5f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.5f}};
        static const int keys[] = {-1, 0, 0x2A, 0x2B};
        const float angle = angles[op.c % 5], speed = speeds[op.d % 4];
        const float *axis = axes[(op.a / 4) % 8];
        const int key = keys[(op.b / 6) % 4];
        const uint8_t locked = uint8_t((op.c / 5) % 3 == 0);
        w.Raw(&kind, 4);
        w.Raw(&motion, 4);
        w.Raw(&angle, 4);
        w.Raw(&speed, 4);
        w.Raw(axis, 12);
        w.Raw(&key, 4);
        w.Raw(&locked, 1);
        const uint8_t skipped[5] = {0, 1, 2, 3, 4};
        w.Raw(skipped, 5);
        const uint8_t u23c = uint8_t(op.d & 1);
        w.Raw(&u23c, 1);
        std::vector<int> ids;
        for (int id = 0x65; id <= 0x6D; ++id)
            if ((op.a >> (id - 0x65)) & 1 || op.b % 4 == 0)
                ids.push_back(id);
        for (size_t k = ids.size(); k > 1; --k)
            std::swap(ids[k - 1], ids[size_t(op.c + int(k)) % k]);
        const int foreignAt = op.d % 3 == 0 ? op.d % int(ids.size() + 1) : -1;
        for (size_t k = 0; k <= ids.size(); ++k) {
            if (int(k) == foreignAt) {
                w.Begin(uint16_t(op.d % 2 ? 0x0099 : 0x1234));
                if (op.d % 4 == 0)
                    w.EntryOf(0x68, uint8_t(0));
                w.End();
            }
            if (k == ids.size())
                break;
            const int id = ids[k];
            const int v = (op.b + int(k) * 7) % 1000;
            w.Begin(uint16_t(id));
            switch (id) {
            case 0x65:
                for (int s = 0; s < 9; ++s) {
                    const int sound = (v + s) % 4 == 0 ? -1 : 10 + s + v % 5;
                    w.Raw(&sound, 4);
                }
                break;
            case 0x66: {
                const uint8_t f = uint8_t(v % 3 != 0);
                w.Raw(&f, 1);
                break;
            }
            case 0x67: {
                const std::string name = c.halves ? F("gate%d", 1 - li) : v % 2 ? "nowhere" : "";
                w.Raw(name.c_str(), name.size() + 1);
                break;
            }
            case 0x68:
            case 0x69:
            case 0x6B:
            case 0x6C: {
                const uint8_t f = uint8_t(v % 2);
                w.Raw(&f, 1);
                break;
            }
            case 0x6A: {
                const float facing[3] = {0.0f, v % 3 == 0 ? 0.5f : 0.0f, v % 2 ? 1.0f : -1.0f};
                w.Raw(facing, 12);
                break;
            }
            default: {
                const int text = 0x1BF1 + v % 3;
                const uint8_t f = uint8_t(v % 2);
                w.Raw(&text, 4);
                w.Raw(&f, 1);
                break;
            }
            }
            w.End();
        }
        w.End();
        const std::string name = F("dr_props%d.bin", index);
        g_overrides[Lower(name)] = w.out;
        C_chunk chunk;
        chunk.Open(name.c_str(), false);
        uint16_t id;
        chunk.Descend(id);
        const bool r = o ? OriginalAt<T_DrChunk>(O_DR_READ)(b, &chunk) : p->ReadProperties(chunk);
        const int depth = chunk.m_depth;
        chunk.Close();
        return F("ReadProperties %d -> %d depth %d ", li, int(r), depth) + Hex(w.out);
    }
    case OP_WRITE: {
        const std::string file = F("dr_write%d.bin", index);
        C_chunk chunk;
        chunk.Open(file.c_str(), true);
        chunk.Begin(0xAE40);
        const bool r = o ? OriginalAt<T_DrChunk>(O_DR_WRITE)(b, &chunk) : p->WriteProperties(chunk);
        chunk.Ascend();
        chunk.Begin(0x5555);
        chunk.Write(int(1));
        chunk.Ascend();
        chunk.Close();
        std::string bytes;
        FileText(file, bytes);
        // (The skipped field's last three bytes: stack leftovers in the original, zeros in the port: masked.)
        if (bytes.size() > 6 + 38)
            for (int k = 0; k < 3; ++k)
                bytes[size_t(6 + 35 + k)] = '\0';
        return F("WriteProperties %d -> %d ", li, int(r)) + Hex(bytes);
    }
    case OP_COPY: {
        const int from = (li + (op.a & 1)) % g_doorCount;
        uint8_t *src = g_door[g_side][from];
        if (o)
            OriginalAt<T_DrVoid>(O_DR_COPY)(b, src);
        else
            p->CopyFrom(reinterpret_cast<C_Actor *>(src));
        return F("CopyFrom %d %d", li, from);
    }
    case OP_START: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_DrInt>(O_DR_START)(b, server);
        else
            p->StartMission(server);
        return F("StartMission %d %d", li, int(server));
    }
    case OP_STOP: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_DrInt>(O_DR_STOP)(b, server);
        else
            p->StopMission(server);
        return F("StopMission %d %d", li, int(server));
    }
    case OP_RESTART:
        if (o)
            OriginalAt<T_DrInt>(O_DR_RESTART)(b, op.c & 1);
        else
            p->RestartMission(op.c & 1);
        return F("RestartMission %d", li);
    case OP_SET_STATE: {
        static const int actions[] = {DOOR_ACTION_OPEN, DOOR_ACTION_CLOSE, DOOR_ACTION_OPEN, DOOR_ACTION_CLOSE, 1, 0};
        const int action = actions[op.a % 6];
        const bool autoClose = op.b & 1, linked = (op.b / 2) % 4 == 0;
        const int force = op.c % 3 == 0 ? 0x101 : op.c % 3 == 1 ? 0x100 : 0;
        if (o)
            OriginalAt<void(TC *)(void *, int, int, int, int)>(O_DR_SET_STATE)(b, action, autoClose, linked, force);
        else
            p->SetState(action, autoClose, linked, force);
        return F("SetState %d %d %d %d %x", li, action, int(autoClose), int(linked), force);
    }
    case OP_UNLOCK:
    case OP_LOCK: {
        static const int keys[] = {-1, 0x2A, 0x2B, 0};
        const int key = keys[op.a % 4];
        const bool unlock = op.kind == OP_UNLOCK;
        const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(unlock ? O_DR_UNLOCK : O_DR_LOCK)(b, key)
                         : unlock ? p->Unlock(key) : p->Lock(key);
        return F("%s %d %d -> %d", unlock ? "Unlock" : "Lock", li, key, int(r));
    }
    case OP_PICK:
        if (o)
            OriginalAt<T_Void>(O_DR_PICK)(b);
        else
            p->PickLock();
        return F("PickLock %d", li);
    case OP_SIDE: {
        const S_vector &at = AsFrame(g_doorFrame[g_side][li])->GetWorldPos();
        alignas(16) S_vector4 pos = {at.x + float(op.a % 5) - 2.0f, at.y + float(op.c % 5) - 2.0f, at.z + float(op.b % 5) - 2.0f, 1.0f};
        const int r = o ? OriginalAt<int(TC *)(void *, const S_vector4 *)>(O_DR_SIDE)(b, &pos) : p->GetSide(pos);
        return F("GetSide %d -> %d", li, r);
    }
    case OP_TICK:
    case OP_UPDATE: {
        const int t = FRAME_TIMES[op.a % 12];
        const bool tick = op.kind == OP_TICK;
        if (o)
            OriginalAt<T_DrInt>(tick ? O_DR_TICK : O_DR_UPDATE)(b, t);
        else if (tick)
            p->Tick(t);
        else
            p->Update(t);
        return F("%s %d %d", tick ? "Tick" : "Update", li, t);
    }
    case OP_MESSAGE: {
        const uint32_t kind = uint32_t(op.a % 7);
        g_bits[g_side].clear();
        g_bits[g_side].push_back(kind);
        if (o)
            OriginalAt<T_DrVoid>(O_DR_MESSAGE)(b, W().bitstream);
        else
            p->OnMessage(reinterpret_cast<C_BitStream *>(W().bitstream));
        return F("OnMessage %d %u", li, kind);
    }
    case OP_NET_STATE: {
        if (op.a & 1) {
            if (o)
                OriginalAt<T_DrVoid>(O_DR_WRITE_STATE)(b, W().bitstream);
            else
                p->WriteState(reinterpret_cast<C_BitStream *>(W().bitstream));
            return F("WriteState %d", li);
        }
        g_bits[g_side].clear();
        g_bits[g_side].push_back(uint32_t(op.b % 2));
        g_bits[g_side].push_back(uint32_t(op.c % 4));
        if (o)
            OriginalAt<T_DrVoid>(O_DR_READ_STATE)(b, W().bitstream);
        else
            p->ReadState(reinterpret_cast<C_BitStream *>(W().bitstream));
        return F("ReadState %d", li);
    }
    case OP_SAVE_LOAD: {
        const std::string file = F("dr_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_DrChunk>(O_DR_SAVE)(b, &save) : p->SaveGame(save);
        save.Ascend();
        save.Begin(0x3000);
        save.Write(int(7));
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        // (Loaded back now and then (into this one or the other): its file, or a made one: other states, a foreign
        // entry, two states. Not one without the state: the original places the frame at stack leftovers.)
        bool rl = false;
        const int load = op.c % 4;
        uint8_t *into = g_door[g_side][(li + (op.d & 1)) % g_doorCount];
        unsigned next = 0;
        int depth = 0;
        if (load) {
            std::string name = file;
            if (load >= 2) {
                S_Writer w;
                w.Begin(0x2000);
                // (A foreign entry holds the states when there is one: entered, not left.)
                const bool foreign = op.b % 3 == 0;
                if (foreign)
                    w.Begin(0x0009);
                const int rounds = op.a % 7 == 0 ? 2 : 1;
                for (int k = 0; k < rounds; ++k) {
                    w.Begin(3000);
                    const float opened = float((op.a + k) % 4) * 0.4f;
                    const uint8_t locked = uint8_t(op.b & 1);
                    const int motion = (op.d + k) % 4, delay = op.b % 3 * 100;
                    const float pos[3] = {float(op.a % 10), 1.0f, float(op.d % 10)};
                    const float rot[4] = {0.9238795f, 0.0f, 0.3826834f, 0.0f};
                    w.Raw(&opened, 4);
                    w.Raw(&locked, 1);
                    w.Raw(&motion, 4);
                    w.Raw(&delay, 4);
                    w.Raw(pos, 12);
                    w.Raw(rot, 16);
                    w.End();
                }
                if (foreign)
                    w.End();
                w.End();
                w.EntryOf(0x3000, int(7));
                name = F("dr_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_DrChunk>(O_DR_LOAD)(into, &chunk) : reinterpret_cast<C_Door *>(into)->LoadGame(chunk);
            depth = chunk.m_depth;
            chunk.Ascend();
            uint16_t after = 0;
            chunk.Descend(after);
            next = after;
            chunk.Close();
        }
        return F("SaveGame %d -> %d LoadGame %d into %s -> %d depth %d next %04x bytes ", li, int(rs), load,
                 NameOf(Ptr(into)).c_str(), int(rl), depth, next) +
               Hex(bytes);
    }
    case OP_MATERIAL: {
        uint8_t *frame = op.b % 3 == 0 ? g_doorFrame[g_side][li] : op.b % 3 == 1 ? reinterpret_cast<uint8_t *>(AsFrame(g_doorFrame[g_side][li])->m_firstChild)
                                                                     : nullptr;
        const int r = o ? OriginalAt<int(TC *)(void *, void *)>(O_DR_MATERIAL)(b, frame) : p->GetMaterialId(AsFrame(frame));
        return F("GetMaterialId %d %s -> %d", li, NameOf(Ptr(frame)).c_str(), r);
    }
    case OP_AUTO_CLOSE: {
        // Opened to close by itself (forced), the player placed, the clock run past the 15 s (Update: every role).
        S_DrOp place = op;
        place.kind = OP_PLAYER;
        RunDrOp(place, index, c);
        if (o)
            OriginalAt<void(TC *)(void *, int, int, int, int)>(O_DR_SET_STATE)(b, DOOR_ACTION_OPEN, 1, 1, 0x101);
        else
            p->SetState(DOOR_ACTION_OPEN, true, true, 0x101);
        for (int step : {3000, 12500, 100, 2000, 300}) {
            if (o)
                OriginalAt<T_DrInt>(O_DR_UPDATE)(b, step);
            else
                p->Update(step);
        }
        return F("AutoClose %d %d %d", li, op.a % 3, op.b % 20);
    }
    default: {
        // The player near the door, far, or none; the scene's stamp against the frame's (seen lately or not).
        if (!g_player[g_side])
            return "Player (none)";
        I3D_frame *frame = AsFrame(*reinterpret_cast<uint8_t **>(g_player[g_side] + (o ? 0x20 : offsetof(C_Actor, m_frame))));
        if (frame) {
            // (In the world: the frame's local position moved by what its parent adds.)
            const S_vector at = AsFrame(g_doorFrame[g_side][li])->GetWorldPos();
            const float d = op.a % 3 == 0 ? 3.0f : op.a % 3 == 1 ? 9.9f : 40.0f;
            const float dy = op.c % 2 ? 1.5f : 0.0f;
            const S_vector target(at.x + d, at.y + dy, at.z + (op.c % 3 == 0 ? 2.0f : 0.0f));
            for (int pass = 0; pass < 2; ++pass) {
                const S_vector now = frame->GetWorldPos();
                frame->m_localMatrix.m[3][0] += target.x - now.x;
                frame->m_localMatrix.m[3][1] += target.y - now.y;
                frame->m_localMatrix.m[3][2] += target.z - now.z;
                std::function<void(I3D_frame *)> stale = [&](I3D_frame *x) {
                    x->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
                    for (I3D_frame *c = x->m_firstChild; c; c = c->m_nextSibling)
                        stale(c);
                };
                stale(frame);
            }
        }
        Mut<int>(W().scene, 0x11C) = 100 + op.b % 20;
        Mut<int>(g_doorFrame[g_side][li], 0x11C) = 100;
        return F("Player %d %d", op.a % 3, op.b % 20);
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
            std::printf("dr_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_drShow;
void RunDrCase(const S_DrCase &c)
{
    g_caseStart = GetTickCount();
    ++g_drCases;
    g_holdFrees = true;
    g_current = F("case %d", g_drCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_doorCount = int(c.doors.size());
    g_blockEvery = c.blockEvery;
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_texts[side].clear();
        g_bits[side].clear();
        BuildWorld(side, worldSeed);
        uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
        // The sector's portals (each the next one's opposite, the last none), its own or incoming.
        const int portals = c.portals ? c.portals : c.incoming;
        uint8_t **list = reinterpret_cast<uint8_t **>(Block(4 * MAX_PORTALS + 4));
        for (int k = 0; k < MAX_PORTALS; ++k) {
            g_portal[side][k] = nullptr;
            if (k >= portals)
                continue;
            uint8_t *portal = Block(0x200);
            *reinterpret_cast<void ***>(portal) = EngineVtable("I3D_portal");
            Name(portal, F("portal%d", k));
            Mut<S_vector>(portal, 0x180) = S_vector(c.doors[0].x + c.portalAt[k][0], c.doors[0].y + c.portalAt[k][1],
                                                    c.doors[0].z + c.portalAt[k][2]);
            Mut<float>(portal, 0x190) = c.portalAt[k][3];
            g_portal[side][k] = portal;
            list[k] = portal;
        }
        for (int k = 0; k + 1 < portals; ++k)
            Mut<uint8_t *>(g_portal[side][k], 0x19C) = g_portal[side][k + 1];
        Mut<uint8_t **>(sector, c.portals ? 0x24C : 0x25C) = list;
        Mut<int>(sector, c.portals ? 0x254 : 0x264) = portals;
        Mut<int>(sector, c.portals ? 0x264 : 0x254) = 0;
        g_sound[side] = NewFrame(FRAME_SOUND, "moving sound");
        // (The scene's render stamp well past the frames': not seen lately, the player's distance decides.)
        Mut<int>(W().scene, 0x11C) = 1000;
        g_player[side] = nullptr;
        if (c.player)
            for (const auto &a : W().actors)
                if (a.type == ACTOR_HUMAN) {
                    g_player[side] = a.bytes;
                    break;
                }
        uint8_t *script = Block(0x400);
        Name(script, "script");
        for (int i = 0; i < g_doorCount; ++i) {
            const S_DrDoor &lc = c.doors[size_t(i)];
            uint8_t *frame = Child(sector, FRAME_MODEL, F("gate%d", i), nullptr);
            S_matrix &m = AsFrame(frame)->m_localMatrix;
            m.Identity();
            const float ch = std::cos(lc.heading), sh = std::sin(lc.heading);
            m.m[0][0] = ch;
            m.m[0][2] = -sh;
            m.m[2][0] = sh;
            m.m[2][2] = ch;
            m.m[3][0] = lc.x;
            m.m[3][1] = lc.y;
            m.m[3][2] = lc.z;
            AsFrame(frame)->m_userProps = Text(i ? "matid 8" : "");
            uint8_t *mesh = Child(frame, FRAME_VISUAL, "door mesh", frame);
            AsFrame(mesh)->m_localMatrix.m[3][0] = 0.5f;
            AsFrame(mesh)->m_userProps = Text("matid 5");
            if (lc.handle) {
                uint8_t *handle = Child(mesh, FRAME_VISUAL, F("gate%d.DHandle", i), frame);
                AsFrame(handle)->m_localMatrix.m[3][0] = 0.4f;
            }
            if (lc.handle2) {
                uint8_t *handle2 = Child(frame, FRAME_VISUAL, "x_d2handle_b", frame);
                if (lc.handleOdd) {
                    I3D_frame *h = AsFrame(handle2);
                    h->m_localMatrix.m[2][0] = lc.handleOdd == 1 ? 1e-5f : 0.0f;
                    h->m_localMatrix.m[2][1] = 0.0f;
                    h->m_localMatrix.m[2][2] = lc.handleOdd == 2 ? -1e-5f : lc.handleOdd == 3 ? 1e-5f : 0.0f;
                    h->m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
                }
            }
            g_doorFrame[side][i] = frame;
        }
        SetRole(c.role);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        for (int i = 0; i < g_doorCount; ++i) {
            uint8_t *b;
            if (side) {
                b = Block(0x270 + 16);
                OriginalAt<T_Void>(O_DR_CTOR)(b);
                // (The original's constructor leaves the moving sound's slot and the handles' way unset, the port's -1
                // and rising: StartMission sets both before they are read.)
                Mut<int>(b, 0xF0) = -1;
                b[0x10D] = 1;
                Mut<uint8_t *>(b, 0x20) = g_doorFrame[1][i];
            } else {
                b = Block(sizeof(C_Door) + 16);
                new (b) C_Door();
                reinterpret_cast<C_Door *>(b)->m_frame = AsFrame(g_doorFrame[0][i]);
            }
            g_door[side][i] = b;
            Name(b, F("door %d", i));
            // (The actor's position: its frame's, as the actors module sets it; the closing by itself measures from it.)
            const S_vector at(c.doors[size_t(i)].x, c.doors[size_t(i)].y, c.doors[size_t(i)].z);
            if (side)
                Mut<S_vector4>(b, 0x30) = {at.x, at.y, at.z, 1.0f};
            else
                reinterpret_cast<C_Door *>(b)->m_position = {at.x, at.y, at.z, 1.0f};
            // The frame's data: the door, a script or none (the other half is found by it).
            S_FrameData *data = reinterpret_cast<S_FrameData *>(Block(0x20));
            data->actor = reinterpret_cast<C_Actor *>(b);
            data->script = c.doors[size_t(i)].script ? reinterpret_cast<C_Script *>(script) : nullptr;
            AsFrame(g_doorFrame[side][i])->m_data = data;
        }
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_log[side].clear();
        for (const std::string &s : DoorsState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_DR_TRACE") ? std::atoi(std::getenv("HD2_DR_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_drCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_KIND_NAMES[c.ops[i].kind],
                            c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunDrOp(c.ops[i], int(i), c));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            for (const std::string &s : DoorsState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        for (int i = 0; i < g_doorCount; ++i) {
            uint8_t *b = g_door[side][i];
            if (side) {
                OriginalAt<T_DrInt>(O_DR_STOP)(b, 1);
                OriginalAt<T_Void>(O_DR_DTOR)(b);
            } else {
                PortDoor(i)->StopMission(true);
                PortDoor(i)->~C_Door();
            }
        }
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(O_DR_CREATE)(OriginalAt<void *>(O_DR_CREATOR));
            Name(made, "created door");
            out.push_back(F("created type %d", At<int>(made, 0x18)));
            OriginalAt<void(TC *)(void *, void *)>(O_DR_DESTROY)(OriginalAt<void *>(O_DR_CREATOR), made);
        } else {
            C_ActorCreator *creator = g_actorCreators[ACTOR_DOOR];
            C_Actor *made = creator->Create();
            Name(made, "created door");
            out.push_back(F("created type %d", int(made->GetType())));
            creator->Destroy(made);
        }
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        for (int i = 0; i < MAX_DOORS; ++i)
            g_door[side][i] = nullptr;
    }
    static const int only = std::getenv("HD2_DR_ONLY") ? std::atoi(std::getenv("HD2_DR_ONLY")) : 0;
    if (g_drShow && (!only || only == g_drCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_drShow == 2)
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

void SetUpDoor()
{
    SetUpCmd();
    SetUpDoorAnswers();
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x007e52d8, reinterpret_cast<const void *>(&O_dbgPrintf)},
        {0x007e6334, reinterpret_cast<const void *>(&O_Sprintf)},
        {0x007e53aa, reinterpret_cast<const void *>(&O_dtaOpenWrite)},
        {0x007e5398, reinterpret_cast<const void *>(&O_dtaSeek)},
        {0x007e5392, reinterpret_cast<const void *>(&O_dtaWrite)},
        {0x007e562e, reinterpret_cast<const void *>(&O_Atexit)},
        {0x007e53c2, reinterpret_cast<const void *>(&O_AlignedMalloc)},
        {0x007e559c, reinterpret_cast<const void *>(&O_AlignedFree)},
        {0x006e5380, reinterpret_cast<const void *>(&O_TextsGet)},
        {0x0041d960, reinterpret_cast<const void *>(&B_OnUse)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    {
        auto onUse = &C_Script::OnUse;
        void *at;
        std::memcpy(&at, &onUse, sizeof at);
        PatchPort(at, reinterpret_cast<const void *>(&B_OnUse));
    }
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("dr_diff: main");
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
        std::printf("dr_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_DR_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_DR_SHOW"))
        g_drShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_DR_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_DR_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpDoor();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunDrCase(RandomDrCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("dr_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_drCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
