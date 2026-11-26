// Air attack part 1: the strafing fighter planes (game/air_attack.cpp, the original's 0x005e2c00..0x005ea2f0) against the
// original executable's. cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the planes' calls
// go through aa_calls.s / aa_calls.inc (gen_cmd_calls.py aa). The sound cache runs on both sides (game/resource_cache.cpp
// linked; the original's constructed by its static initialiser's constructor); the LS3DF math is LS3DF.dll's on both
// sides; RandomFloat and AngleBetween2D are the original's own on both sides (the original's rand redirected to the
// CRT's); a script's OnDeath is logged on both sides.
//
// Each case builds the same world on each side (cmd_diff's, plus one to three planes: models under the primary sector
// with their blades, wheels, muzzle flashes and propeller, some missing), makes the planes on each side (the original's
// by its constructor 0x005e3c90 in a block of 0x200, the port's by its own), gives them the same properties, starts the
// mission and runs the same random sequence of operations: updates and ticks (the game's time advancing), hits,
// SetTarget, saves and loads (the planes' own files and made ones), restarts, stops, FindAirAttackFor,
// PauseAirAttackSounds, RayTestPastBorders, WriteProperties. The rays answer by turns: nothing, a border, sand, a
// minefield, another material; the target's frame or a plane's volume now and then. After each operation the answer,
// every plane's state (every field and list in each side's layout), the world's frames and every call made into the
// game and the engine are compared; a save's bytes too.
// MinGW; build_aa.bat; runs aa_diff.exe ("aa_diff: N checks, 0 failures"). Env: HD2_AA_CASES (default 1,500;
// HD2_AA_QUICK: 300), HD2_AA_SEED, HD2_AA_SHOW (1: the port's lines; 2: both), HD2_AA_DUMP, HD2_AA_TRACE (a case's
// operations printed as they run).
#define HARNESS_NAME "aa_diff"
#define CMD_CALLS_INC "aa_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/air_attack.h"
#include "game/collision.h"
#include "game/resource_cache.h"
#include "game/shots.h"
#include "game/sight.h"
#include "ls3d/igraph.h"
#include "script/script.h"
#undef protected
#undef private

// ---- the port's side of what the planes call and the test provides ----
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
S_matrix __stdcall RotateYMatrix(float angle)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_matrix *, float)>("?RotateYMatrix@@YG?AUS_matrix@@M@Z");
    S_matrix r;
    f(&r, angle);
    return r;
}
S_matrix __stdcall RotateZMatrix(float angle)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_matrix *, float)>("?RotateZMatrix@@YG?AUS_matrix@@M@Z");
    S_matrix r;
    f(&r, angle);
    return r;
}
bool __stdcall S_matrix::Inverse(const S_matrix &src)
{
    static const auto f = Ls3dfFn<bool(__stdcall *)(S_matrix *, const S_matrix &)>("?Inverse@S_matrix@@QAG_NABU1@@Z");
    return f(this, src);
}
S_vector __stdcall S_matrix::GetScale() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_matrix *, float *)>("?GetScale@S_matrix@@QBG?AUS_vector@@XZ");
    alignas(16) float r[4] = {};
    f(this, r);
    return S_vector(r[0], r[1], r[2]);
}
void __stdcall S_matrix::SetDir3(const S_vector &dir)
{
    static const auto f = Ls3dfFn<void(__stdcall *)(S_matrix *, const S_vector &)>("?SetDir3@S_matrix@@QAGXABUS_vector@@@Z");
    f(this, dir);
}
S_vector __stdcall S_vector::RotateByMatrix(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_vector *, float *, const S_matrix &)>(
        "?RotateByMatrix@S_vector@@QBG?AU1@ABUS_matrix@@@Z");
    alignas(16) float r[4] = {};
    f(this, r, m);
    return S_vector(r[0], r[1], r[2]);
}
S_quat __stdcall S_quat::RotateByMatrix(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, S_quat *, const S_matrix &)>(
        "?RotateByMatrix@S_quat@@QBG?AU1@ABUS_matrix@@@Z");
    S_quat r;
    f(this, &r, m);
    return r;
}
// The original's own on both sides (its rand: the CRT's, cmd_diff's redirect).
float RandomFloat(float max) { return OriginalAt<float(__stdcall *)(float)>(0x00475190)(max); }
float AngleBetween2D(const float *a, const float *b)
{
    return OriginalAt<float(__fastcall *)(const float *, const float *)>(0x00788a10)(a, b);
}
// The port's allocations zeroed, as the original's (its _aligned_malloc redirected to a zeroing one): a plane the
// creator makes has its base's fields as the (logged) base constructor leaves them on both sides.
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
// The actors' creators (the planes' registers itself before main: a table of the test's, set up as the program loads).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the plane's vtable holds but the test never reaches.
void C_Actor::Init() {}
void C_Actor::Done() {}
void C_Actor::CopyFrom(const C_Actor *) {}
int C_Actor::GetSeatAmmoCount(int) { return 0; }
void C_Actor::GetBoxCorners(S_vector4 *) {}

// The sound driver (ISndGetDriver: LS3DF's; the original's import thunk redirected): each side's world's.
namespace H {
uint8_t *g_sndDriver[2];
void *__cdecl O_ISndGetDriver() { return g_sndDriver[1]; }
}  // namespace H
extern "C" ISND_driver *ISndGetDriver() { return reinterpret_cast<ISND_driver *>(H::g_sndDriver[H::g_side]); }

namespace H {

#define OT __attribute__((thiscall))
// A script's OnDeath (the port's own patched to this at the start; the original's redirected): logged.
void OT A_OnDeath(void *script) { g_log[g_side].push_back("C_Script::OnDeath(" + NameOf(reinterpret_cast<uint32_t>(script)) + ")"); }
// A human's sector (the original's 0x005ac4c0 on its body, +0x98): logged, the primary one now and then.
void *SectorOf(const void *actor)
{
    g_log[g_side].push_back("GetSector(" + NameOf(reinterpret_cast<uint32_t>(actor)) + ")");
    return NextFake() % 2 ? *reinterpret_cast<uint8_t **>(W().scene + 0x1F8) : nullptr;
}
void *OT O_GetSector(const uint8_t *body) { return SectorOf(body - 0x98); }
int __cdecl O_Atexit(void *) { return 0; }
}  // namespace H
I3D_sector *C_Actor::GetSector() const { return static_cast<I3D_sector *>(H::SectorOf(this)); }
namespace H {
void *__cdecl O_AlignedMalloc(size_t size, size_t align)
{
    void *p = _aligned_malloc(size ? size : 1, align);
    std::memset(p, 0, size ? size : 1);
    return p;
}
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }
// A port function's entry patched to jump to `to` (its code made writable).
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
std::deque<std::string> g_texts[2];                     // (names: stable storage)
const char *Text(const std::string &s)
{
    g_texts[g_side].push_back(s);
    return g_texts[g_side].back().c_str();
}
// A world frame under `parent` (its name, owner model).
uint8_t *Child(uint8_t *parent, int type, const std::string &name, uint8_t *owner, uint32_t visual = I3DVISUAL_OBJECT)
{
    uint8_t *b = NewFrame(type, name);
    S_FakeFrame &f = W().frames.back();
    if (type == FRAME_VISUAL)
        f.visualType = visual;
    I3D_frame *x = AsFrame(b);
    x->m_name = f.name.c_str();
    x->m_ownerModel = reinterpret_cast<I3D_frame *>(owner);
    x->m_frameFlags |= 1;                               // (shown)
    if (parent)
        LinkUnder(b, parent);
    return b;
}

// ---- the planes ----
constexpr int MAX_PLANES = 3;
uint8_t *g_plane[2][MAX_PLANES];                        // (the side's planes: the original's blocks, the port's objects)
uint8_t *g_planeModel[2][MAX_PLANES];
int g_planeCount;
C_AirAttack *PortPlane(int i) { return reinterpret_cast<C_AirAttack *>(g_plane[0][i]); }
// A plane's model (by the world's random numbers: the same on both sides).
void BuildPlane(uint8_t *model)
{
    const int left = WI(0, 3), right = WI(0, 3);
    // (A flash always: a plane without one divides by 0 in the original's Fire.)
    for (const char *n : {"listy", "carriage_l00", "carriage_r00", "bilboard", "bluur"})
        if (WI(0, 4) || (std::strcmp(n, "bilboard") == 0 && !left && !right))
            Child(model, FRAME_VISUAL, n, model, std::strncmp(n, "bilboard", 8) == 0 ? I3DVISUAL_BILLBOARD : I3DVISUAL_OBJECT);
    for (int i = 0; i < std::max(left, right); ++i) {
        if (i < left)
            Child(model, FRAME_VISUAL, F("bilboard_l0%d", i), model, I3DVISUAL_BILLBOARD);
        if (i < right)
            Child(model, FRAME_VISUAL, F("bilboard_r0%d", i), model, I3DVISUAL_BILLBOARD);
    }
    if (WI(0, 1))
        Child(model, FRAME_DUMMY, "BODY", model);
}

// The rays' volumes (a few in turn: a caller reads the one it got before the next ray).
alignas(16) uint8_t g_rayVolumes[2][4][0x80];
int g_rayNext[2];
uint8_t *g_target[2];                                   // (the case's last target frame: the rays see it now and then)
S_vector g_lastHit[2];                                  // (the last ray's end: the camera put there now and then)
bool g_cameraNear;                                      // (the case's cameraNear)
const int MATERIALS[] = {MATERIAL_STOP, MATERIAL_PASS, 42, 42, 42, MATERIAL_MINEFIELD, 3, 0};
uint8_t *HumanActor(int n)
{
    const auto &humans = W().humans;
    return humans.empty() ? nullptr : W().actors[size_t(humans[size_t(n) % humans.size()])].bytes;
}
S_CollisionVolume *RayVolume(uint32_t r)
{
    uint8_t *v = g_rayVolumes[g_side][g_rayNext[g_side]++ % 4];
    std::memset(v, 0, 0x80);
    S_CollisionVolume *x = reinterpret_cast<S_CollisionVolume *>(v);
    x->flags = uint32_t(MATERIALS[r % 8]) << 24 | (NextFake() % 4) << 6;
    const uint32_t who = NextFake() % 6;
    x->frame = who % 2 == 0 && g_target[g_side] ? AsFrame(g_target[g_side]) : nullptr;
    if (who == 1 && g_planeCount)
        x->actor = reinterpret_cast<C_Actor *>(g_plane[g_side][NextFake() % uint32_t(g_planeCount)]);
    else if (who == 2)
        x->actor = reinterpret_cast<C_Actor *>(HumanActor(int(NextFake() % 7)));
    Name(v, F("ray volume %d", g_rayNext[g_side] % 4));
    return x;
}

void SetUpAirAttackAnswers()
{
    // Frames by name under a frame.
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        const std::string want = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        std::function<uint32_t(I3D_frame *)> find = [&](I3D_frame *p) -> uint32_t {
            for (I3D_frame *c = p->m_firstChild; c; c = c->m_nextSibling) {
                if (c->m_name && want == c->m_name)
                    return Ptr(c);
                if (uint32_t d = find(c))
                    return d;
            }
            return 0u;
        };
        return find(reinterpret_cast<I3D_frame *>(a[0]));
    };
    g_answers["I3D_frame::LinkTo"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *c = reinterpret_cast<I3D_frame *>(a[0]);
        for (I3D_frame *p = reinterpret_cast<I3D_frame *>(a[1]); p; p = p->m_parent)
            if (p == c)
                return 0x80000001u;
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
        return 0u;
    };
    g_answers["I3D_frame::SetOn"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_frameFlags = (a[1] & 0xFF) ? (f->m_frameFlags | 1u) : (f->m_frameFlags & ~1u);
        return 0u;
    };
    // A frame's bound (the planes' boxes' w in Update): a radius of 2..6 m, made valid.
    g_answers["I3D_frame::UpdateBound"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_bound.radius = float(2 + NextFake() % 5);
        f->m_frameFlags |= FRMFLAGS_BOUND_VALID;
        return 0u;
    };
    // A frame's position (the planes' moves): its local matrix's, its matrices made again.
    g_answers["I3D_frame::SetPos"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[1]);
        f->m_localMatrix.m[3][0] = pos.x;
        f->m_localMatrix.m[3][1] = pos.y;
        f->m_localMatrix.m[3][2] = pos.z;
        f->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
        return 0u;
    };
    g_answers["I3D_frame::AddRef"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(++reinterpret_cast<I3D_frame *>(a[0])->m_refCount);
    };
    g_answers["I3D_driver::CreateFrame"] = [](const std::vector<uint32_t> &a) {
        return Ptr(Child(nullptr, int(a[1]), F("new%d", W().created++), nullptr));
    };
    // The driver's frames by name (a loaded target's), the scene's.
    g_answers["I3D_driver::FindFrame"] = [](const std::vector<uint32_t> &a) {
        return NextFake() % 4 ? Ptr(FindByName(reinterpret_cast<const char *>(a[2]))) : 0u;
    };
    g_answers["I3D_scene::FindFrame"] = [](const std::vector<uint32_t> &a) {
        return Ptr(FindByName(reinterpret_cast<const char *>(a[1])));
    };
    // A hierarchy's box (the planes' volumes): filled and returned (min +0, max +0x10).
    g_answers["I3D_driver::GetHierarchyBBox"] = [](const std::vector<uint32_t> &a) {
        float *box = reinterpret_cast<float *>(a[1]);
        const float s = float(1 + NextFake() % 4);
        // (Centred in x: StartMission's centre w is the local centre's x in the port, a stack word in the original.)
        const float b[8] = {-2.0f * s, -0.5f * s, -3.0f * s, 0.0f, 2.0f * s, 0.75f * s, 3.0f * s, 0.0f};
        std::memcpy(box, b, sizeof b);
        return a[1];
    };
    // The sound driver: mostly there. A sound's volume kept (+0x16C: GetVolumeInline reads it).
    g_answers["ISND_driver::IsInitialized"] = [](const std::vector<uint32_t> &) { return uint32_t(NextFake() % 6 != 0); };
    g_answers["I3D_sound::SetVolume"] = [](const std::vector<uint32_t> &a) {
        Mut<uint32_t>(reinterpret_cast<void *>(a[0]), 0x16C) = a[1];
        return 0u;
    };
    // The rays: nothing, or a volume (a border, sand, a minefield, another material) half way, its normal level, up,
    // slanted or along the ray.
    g_answers["CollisionRayTest"] = [](const std::vector<uint32_t> &a) {
        const uint32_t r = NextFake() % 10;
        if (r >= 8)
            return 0u;
        const S_vector &from = *reinterpret_cast<const S_vector *>(a[0]);
        const S_vector &dir = *reinterpret_cast<const S_vector *>(a[1]);
        static const float parts[] = {0.5f, 0.1f, 0.9f, 0.3f};
        const float k = parts[NextFake() % 4];
        const S_vector at(from.x + dir.x * k, from.y + dir.y * k, from.z + dir.z * k);
        g_lastHit[g_side] = at;
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[2]))
            *hit = at;
        if (S_vector *n = reinterpret_cast<S_vector *>(a[3])) {
            static const S_vector normals[] = {S_vector(0.0f, 1.0f, 0.0f), S_vector(0.0f, 0.5f, 0.866f),
                                               S_vector(1.0f, 0.0f, 0.0f), S_vector(0.6f, 0.8f, 0.0f)};
            // (Or against the ray; or across it in y and z: its z term's sign matters to the borders' test.)
            const uint32_t m = NextFake() % 6;
            if (m < 4)
                *n = normals[m];
            else if (m == 4 || (dir.y == 0.0f && dir.z == 0.0f))
                *n = S_vector(-dir.x, -dir.y, -dir.z);
            else {
                const float l = std::sqrt(dir.y * dir.y + dir.z * dir.z);
                *n = S_vector(0.0f, dir.z / l, -dir.y / l);
            }
        }
        return Ptr(RayVolume(r));
    };
    g_answers["C_CollisionWorld::RayTestDynamic"] = [](const std::vector<uint32_t> &a) {
        const uint32_t r = NextFake() % 4;
        if (r == 0)
            return 0u;
        const S_vector &from = *reinterpret_cast<const S_vector *>(a[1]);
        const S_vector &dir = *reinterpret_cast<const S_vector *>(a[2]);
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[3]))
            *hit = S_vector(from.x + dir.x * 0.5f, from.y + dir.y * 0.5f, from.z + dir.z * 0.5f);
        if (S_vector *n = reinterpret_cast<S_vector *>(a[4]))
            *n = S_vector(0.0f, 1.0f, 0.0f);
        return Ptr(RayVolume(r));
    };
    // The camera module's position (2: the module's own): near the last ray's end now and then.
    g_answers["C_CameraModule::GetCameraPos"] = [](const std::vector<uint32_t> &a) {
        float *pos = reinterpret_cast<float *>(a[2]);
        const S_vector &h = g_lastHit[g_side];
        if (NextFake() % 2) {
            pos[0] = h.x + 3.0f;
            pos[1] = h.y - 2.0f;
            pos[2] = h.z + 1.0f;
        } else {
            pos[0] = FakeFloat();
            pos[1] = FakeFloat();
            pos[2] = FakeFloat();
        }
        pos[3] = 1.0f;
        return 1u;
    };
}

// ---- the original's planes ----
constexpr uint32_t O_AA_CTOR = 0x005e3c90, O_AA_DTOR = 0x005e3f10, O_AA_SET_FRAME = 0x005e4070, O_AA_START = 0x005e4090,
                   O_AA_STOP = 0x005e56d0, O_AA_RESTART = 0x005e3090, O_AA_UPDATE = 0x005e5960, O_AA_TICK = 0x005e71e0,
                   O_AA_WRITE = 0x005e8e10, O_AA_READ = 0x005e9010, O_AA_SAVE = 0x005e30b0, O_AA_LOAD = 0x005e3420,
                   O_AA_HIT = 0x005e3b80, O_AA_SET_TARGET = 0x005e7570, O_AA_FIND_FOR = 0x005e8b70,
                   O_AA_PAUSE = 0x005e8d50, O_AA_RAY = 0x005e2cc0;
constexpr uint32_t O_G_AIR_ATTACKS = 0x008ac9c4, O_AA_CREATOR = 0x008ac9d4, O_SIGHT_MODULE = 0x0089b288;
constexpr uint32_t O_SOUND_CACHE = 0x008aed64;

// ---- the compared state: the planes' fields in each side's layout ----
// (Numbers under 1e-30 as 0: the x87 rounds a denormal twice (24 bits, then on the store), the SSE once, and the
// products of one differ after it; a plane flying straight up makes them.)
std::string VN(const float *v, int n)
{
    float w[4];
    for (int i = 0; i < n; ++i)
        w[i] = std::fabs(v[i]) < 1e-30f ? 0.0f : v[i];
    return VectorText(Ptr(w), n);
}
std::string TinyZero(const std::string &line)
{
    std::string out;
    size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        const bool start = (std::isdigit(static_cast<unsigned char>(c)) || c == '-') && (i == 0 || std::strchr(" ({,", line[i - 1]));
        if (start) {
            size_t j = i;
            while (j < line.size() && std::strchr("0123456789.-+e", line[j]))
                ++j;
            const std::string token = line.substr(i, j - i);
            char *end = nullptr;
            const double v = std::strtod(token.c_str(), &end);
            if (end && *end == '\0' && token.find('e') != std::string::npos && v != 0.0 && std::fabs(v) < 1e-30)
                out += "0";
            else
                out += token;
            i = j;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}
std::string V3(const float *v) { return VN(v, 3); }
std::string V4(const float *v) { return VN(v, 4); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::string Fl(float f) { return FloatText(f); }
// An MSVC 6 vector of pointers at `off` (first +4, last +8).
std::vector<uint32_t> OVec(const uint8_t *p, uint32_t off)
{
    std::vector<uint32_t> out;
    const uint32_t first = At<uint32_t>(p, off + 4), last = At<uint32_t>(p, off + 8);
    for (uint32_t a = first; a && a < last; a += 4)
        out.push_back(*reinterpret_cast<const uint32_t *>(a));
    return out;
}
template <class T> std::vector<uint32_t> PVec(const std::vector<T *> &v)
{
    std::vector<uint32_t> out;
    for (T *p : v)
        out.push_back(Ptr(p));
    return out;
}
std::string Names(const std::vector<uint32_t> &v)
{
    std::string s;
    for (uint32_t p : v)
        s += " " + NameOf(p);
    return s;
}
std::string VolumeText(const uint8_t *v)
{
    const S_CollisionVolume &x = *reinterpret_cast<const S_CollisionVolume *>(v);
    return F("volume flags %x type %x frame %s actor %s radius ", x.flags, x.type, NameOf(Ptr(x.frame)).c_str(),
             NameOf(Ptr(x.actor)).c_str()) +
           Fl(x.radius) + " centre " + V3(&x.center.x) + " " + Fl(x.centerW) + " box " + V3(&x.bbox.min.x) + " " +
           V3(&x.bbox.max.x);
}
std::vector<std::string> PlaneState(int i)
{
    std::vector<std::string> out;
    const bool orig = g_side != 0;
    uint8_t *b = g_plane[g_side][i];
    // (The fields in the original's offsets: the port's class has the same; read each side by its own.)
    if (orig) {
        out.push_back(F("plane%d pos ", i) + V4(FP(b, 0x30)) + " dir " + V4(FP(b, 0x40)) + " inv " + VectorText(Ptr(b + 0x90), 16));
        out.push_back("  " + VolumeText(b + 0xD0));
        out.push_back(F("  state %d started %d idle %d sight %d rolling %d gone %d turn ", At<int>(b, 0x130), int(b[0x134]),
                        int(b[0x135]), int(b[0x136]), int(b[0x137]), int(b[0x138])) +
                      Fl(At<float>(b, 0x13C)) + " climb " + Fl(At<float>(b, 0x140)) + " roll " + Fl(At<float>(b, 0x144)) +
                      " speed " + Fl(At<float>(b, 0x148)) + " clearance " + Fl(At<float>(b, 0x14C)) + " ceiling " +
                      Fl(At<float>(b, 0x150)) + " health " + Fl(At<float>(b, 0x154)) + " hp " + Fl(At<float>(b, 0x158)) +
                      " damage " + Fl(At<float>(b, 0x15C)));
        std::string s = F("  target %s propeller %s motor %s gun %s guns", NameOf(At<uint32_t>(b, 0x160)).c_str(),
                          NameOf(At<uint32_t>(b, 0x164)).c_str(), NameOf(At<uint32_t>(b, 0x168)).c_str(),
                          NameOf(At<uint32_t>(b, 0x16C)).c_str());
        for (int k = 0; k < 5; ++k)
            s += " " + NameOf(At<uint32_t>(b, 0x170 + 4 * k));
        s += " motors";
        for (int k = 0; k < 4; ++k)
            s += " " + NameOf(At<uint32_t>(b, 0x184 + 4 * k));
        out.push_back(s + F(" fire %d dust %d smoke %d", At<int>(b, 0x194), At<int>(b, 0x198), At<int>(b, 0x19C)));
        for (uint32_t x : OVec(b, 0x1A0))
            out.push_back("  bullet " + V3(FP(reinterpret_cast<void *>(x), 0)) + " shot " +
                          V3(FP(reinterpret_cast<void *>(x), 0x10)) + F(" time %d", At<int>(reinterpret_cast<void *>(x), 0x20)));
        std::string fl = "  flashes";
        for (uint32_t x : OVec(b, 0x1B0))
            fl += " " + NameOf(At<uint32_t>(reinterpret_cast<void *>(x), 0)) + F(" %d", At<int>(reinterpret_cast<void *>(x), 4));
        out.push_back(fl);
        out.push_back("  impacts" + Names(OVec(b, 0x1C0)) + " whizzes" + Names(OVec(b, 0x1D0)));
        for (uint32_t x : OVec(b, 0x1E0))
            out.push_back("  dust " + NameOf(At<uint32_t>(reinterpret_cast<void *>(x), 0)) + " dir " +
                          V4(FP(reinterpret_cast<void *>(x), 0x10)) + F(" particle %d time %d", At<int>(reinterpret_cast<void *>(x), 0x20),
                                                                      At<int>(reinterpret_cast<void *>(x), 0x24)));
        return out;
    }
    const C_AirAttack *p = PortPlane(i);
    out.push_back(F("plane%d pos ", i) + V4(&p->m_position.x) + " dir " + V4(&p->m_direction.x) + " inv " +
                  VectorText(Ptr(&p->m_invMatrix), 16));
    out.push_back("  " + VolumeText(reinterpret_cast<const uint8_t *>(&p->m_volume)));
    out.push_back(F("  state %d started %d idle %d sight %d rolling %d gone %d turn ", p->m_state, int(p->m_started),
                    int(p->m_idle), int(p->m_targetInSight), int(p->m_rolling), int(p->m_gone)) +
                  Fl(p->m_turnRate) + " climb " + Fl(p->m_climbRate) + " roll " + Fl(p->m_roll) + " speed " + Fl(p->m_speed) +
                  " clearance " + Fl(p->m_clearance) + " ceiling " + Fl(p->m_ceiling) + " health " + Fl(p->m_health) +
                  " hp " + Fl(p->m_hitPoints) + " damage " + Fl(p->m_damage));
    std::string s = F("  target %s propeller %s motor %s gun %s guns", NameOf(Ptr(p->m_target)).c_str(),
                      NameOf(Ptr(p->m_propeller)).c_str(), NameOf(Ptr(p->m_motorSound)).c_str(),
                      NameOf(Ptr(p->m_gunSound)).c_str());
    for (I3D_sound *x : p->m_gunSounds)
        s += " " + NameOf(Ptr(x));
    s += " motors";
    for (I3D_sound *x : p->m_motorSounds)
        s += " " + NameOf(Ptr(x));
    out.push_back(s + F(" fire %d dust %d smoke %d", p->m_fireTime, p->m_dustTime, p->m_smoke));
    // (The bullets' w: never read; the original's are a stack leftover and 1, the port's 0.)
    for (const auto *x : p->m_bullets)
        out.push_back("  bullet " + V3(&x->pos.x) + " shot " + V3(&x->shot.x) + F(" time %d", x->time));
    std::string fl = "  flashes";
    for (const auto *x : p->m_flashes)
        fl += " " + NameOf(Ptr(x->frame)) + F(" %d", x->time);
    out.push_back(fl);
    out.push_back("  impacts" + Names(PVec(p->m_impactSounds)) + " whizzes" + Names(PVec(p->m_whizzSounds)));
    for (const auto *x : p->m_dusts)
        out.push_back("  dust " + NameOf(Ptr(x->frame)) + " dir " + V4(&x->dir.x) + F(" particle %d time %d", x->particle, x->time));
    return out;
}
std::vector<std::string> PlanesState()
{
    std::vector<std::string> out;
    for (int i = 0; i < g_planeCount; ++i)
        for (const std::string &s : PlaneState(i))
            out.push_back(s);
    std::string list = "list";
    if (g_side)
        list += Names(OVec(OriginalAt<uint8_t *>(O_G_AIR_ATTACKS), 0));
    else
        list += Names(PVec(g_airAttacks));
    out.push_back(list);
    return out;
}
std::vector<std::string> AaFrameStates()
{
    std::vector<std::string> out;
    for (const S_FakeFrame &f : W().frames) {
        const I3D_frame *x = AsFrame(f.bytes);
        out.push_back(F("frame %s ref %d flags %08x pos ", NameOf(Ptr(f.bytes)).c_str(), int(x->m_refCount), x->m_frameFlags) +
                      VectorText(Ptr(&x->m_localMatrix.m[3][0]), 3) + " rot " + VectorText(Ptr(&x->m_rot), 4) + " scale " +
                      VectorText(Ptr(&x->m_scale), 3) + " parent " + NameOf(Ptr(x->m_parent)) + " name " +
                      (x->m_name ? x->m_name : ""));
    }
    return out;
}

// The smoke's matrix (C_ParticlesModule::Create): its w column is the original's stack (words 3, 7, 11, 15; the port's 0, 1).
std::string MaskSmokeMatrix(const std::string &line)
{
    if (line.compare(0, 26, "C_ParticlesModule::Create(") != 0)
        return line;
    const size_t open = line.find('('), m = line.find('(', open + 1), close = line.find(')', m);
    if (m == std::string::npos || close == std::string::npos)
        return line;
    std::string out = line.substr(0, m + 1);
    size_t at = m + 1;
    for (int word = 0; at < close; ++word) {
        size_t end = line.find_first_of(" )", at);
        out += (word == 3 || word == 7 || word == 11 || word == 15) ? std::string("-") : line.substr(at, end - at);
        out += line[end];
        at = end + 1;
    }
    return out + line.substr(close + 1);
}

// ---- one case ----
// The chunk writer (the properties, the made saves).
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
enum {
    OP_UPDATE, OP_TICK, OP_HIT, OP_SET_TARGET, OP_SAVE_LOAD, OP_RESTART, OP_STOP_START, OP_FIND, OP_PAUSE, OP_RAY,
    OP_WRITE_PROPS, OP_SET_FRAME, OP_KINDS
};
const char *const OP_KIND_NAMES[] = {"Update", "Tick", "Hit", "SetTarget", "SaveLoad", "Restart", "StopStart", "Find",
                                     "Pause", "Ray", "WriteProps", "SetFrame"};
struct S_AaOp {
    int kind = 0;
    int plane = 0;
    int a = 0, b = 0, c = 0, d = 0;
    float f = 0.0f;
};
struct S_AaPlane {
    std::string props;                                  // (a properties chunk, or none)
    bool readProps = false;
    bool serverStart = false;                           // StartMission(true) first
    bool started = true;
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    int oddHeading = 0;                                 // (1..4: the frame's z axis tiny or zero)
    int script = 0;                                     // (0: no frame data; 1: data without a script; 2: a script)
};
struct S_AaCase {
    std::vector<S_AaPlane> planes;
    std::vector<S_AaOp> ops;
    uint32_t role = GAME_ROLE_LOCAL;
    bool cutscene = false;
    int sightDelay = 0;
    bool cameraNear = false;                            // (the active camera over the first plane: its dust)
};
int g_aaCases, g_opCounts[OP_KINDS];
// (Odd ones too: the timers land on 1.)
const int FRAME_TIMES[] = {0, 10, 16, 33, 100, 250, 1000, 3000, 7000, 1, 19, 2999};
S_AaCase RandomAaCase()
{
    S_AaCase c;
    const int planes = P(0.7) ? 1 : I(2, MAX_PLANES);
    for (int i = 0; i < planes; ++i) {
        S_AaPlane p;
        p.readProps = P(0.6);
        if (p.readProps) {
            S_Writer w;
            w.Begin(0xAE24);
            for (int k = I(0, 4); k > 0; --k) {
                static const uint16_t ids[] = {1, 2, 4, 7};
                static const float values[] = {100.0f, 0.5f, 1000.0f, 30.0f, 0.0f, 250.0f};
                // (No short entries: the original takes an uninitialised local, the port keeps the value.)
                w.EntryOf(ids[I(0, 3)], values[I(0, 5)]);
            }
            w.End();
            p.props = w.out;
        }
        p.serverStart = P(0.2);
        p.started = P(0.95);
        p.x = float(I(-300, 300));
        p.y = float(I(5, 150));
        p.z = float(I(-300, 300));
        p.heading = float(I(0, 628)) * 0.01f;
        p.oddHeading = P(0.1) ? I(1, 4) : 0;
        p.script = I(0, 2);
        c.planes.push_back(p);
    }
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    c.cutscene = P(0.1);
    static const int delays[] = {0, 0, 0, 1000, 2000, 2001, 5000};
    c.sightDelay = delays[I(0, 6)];
    c.cameraNear = P(0.4);
    const int count = P(0.1) ? I(80, 200) : I(4, 50);
    for (int i = 0; i < count; ++i) {
        S_AaOp op;
        const int r = I(0, 99);
        op.kind = r < 40 ? OP_UPDATE : r < 50 ? OP_TICK : r < 58 ? OP_HIT : r < 68 ? OP_SET_TARGET : r < 78 ? OP_SAVE_LOAD
                : r < 80 ? OP_RESTART : r < 82 ? OP_STOP_START : r < 86 ? OP_FIND : r < 89 ? OP_PAUSE : r < 96 ? OP_RAY
                : r < 98 ? OP_WRITE_PROPS : OP_SET_FRAME;
        op.plane = I(0, planes - 1);
        op.a = I(0, 1000);
        op.b = I(-1, 5);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        static const float amounts[] = {0.0f, 1.0f, 5.0f, 50.0f, 1000.0f, 30.0f, 70.0f};
        op.f = amounts[I(0, 6)];
        c.ops.push_back(op);
    }
    return c;
}

// The side's planes made (the original's by its constructor), their frames the world's models.
void MakePlanes(const S_AaCase &c)
{
    for (int i = 0; i < g_planeCount; ++i) {
        uint8_t *b;
        if (g_side) {
            b = Block(0x200);
            OriginalAt<T_Void>(O_AA_CTOR)(b);
            Mut<uint8_t *>(b, 0x20) = g_planeModel[1][i];
            // (The smoke's id as the port's member starts: the original's constructor leaves it, StartMission sets it.)
            Mut<int>(b, 0x19C) = -1;
        } else {
            b = Block(sizeof(C_AirAttack) + 16);
            C_AirAttack *p = new (b) C_AirAttack();
            p->m_frame = AsFrame(g_planeModel[0][i]);
        }
        g_plane[g_side][i] = b;
        Name(b, F("plane %d", i));
        // Its frame's data: the plane, a script now and then.
        if (c.planes[size_t(i)].script) {
            S_FrameData *data = reinterpret_cast<S_FrameData *>(Block(0x20));
            data->actor = reinterpret_cast<C_Actor *>(b);
            if (c.planes[size_t(i)].script == 2) {
                uint8_t *script = Block(0x40);
                Name(script, F("script of plane %d", i));
                data->script = reinterpret_cast<C_Script *>(script);
            }
            AsFrame(g_planeModel[g_side][i])->m_data = data;
        }
    }
}
void AdvanceTime(int t)
{
    if (g_side)
        Mut<uint32_t>(g_origGame, 0x2C) += uint32_t(t);
    else
        reinterpret_cast<C_game *>(g_portGameBuf)->m_time += uint32_t(t);
}
uint8_t *WorldFrame(int n)
{
    const auto &frames = W().frames;
    return frames.empty() ? nullptr : frames[size_t(n) % frames.size()].bytes;
}

using T_AaInt = void(TC *)(void *, int);
using T_AaChunk = bool(TC *)(void *, void *);
using T_Ray = void *(__fastcall *)(const S_vector4 *, const S_vector4 *, S_vector4 *, S_vector4 *, uint32_t, uint32_t);
std::string RunAaOp(const S_AaOp &op, int index)
{
    const bool o = g_side != 0;
    const int pi = op.plane < g_planeCount ? op.plane : 0;
    uint8_t *b = g_plane[g_side][pi];
    C_AirAttack *p = PortPlane(pi);
    const int t = FRAME_TIMES[op.a % 12];
    switch (op.kind) {
    case OP_UPDATE:
        AdvanceTime(t);
        // (Not a stopped plane's, as the game: without its flashes the original's Fire divides by 0.)
        if (!(o ? b[0x134] != 0 : p->m_started))
            return F("Update %d (stopped)", pi);
        // (Nor one lost in NaNs (a direction straight up divides by 0 in both programs' levelling): the sign of a NaN
        // the x87 makes and the SSE's differ, so do the bit tests on them.)
        if (std::isnan(o ? At<float>(b, 0x30) : p->m_position.x))
            return F("Update %d (NaN)", pi);
        if (o)
            OriginalAt<T_AaInt>(O_AA_UPDATE)(b, t);
        else
            p->Update(t);
        return F("Update %d %d", pi, t);
    case OP_TICK:
        if (o)
            OriginalAt<T_AaInt>(O_AA_TICK)(b, t);
        else
            p->Tick(t);
        return F("Tick %d %d", pi, t);
    case OP_HIT: {
        uint8_t *attacker = HumanActor(op.a);
        const S_vector pos(1.0f, 2.0f, 3.0f), dir(0.0f, 0.0f, 1.0f);
        if (o)
            OriginalAt<void(TC *)(void *, int, float, const S_vector *, const S_vector *, void *, int)>(O_AA_HIT)(
                b, op.b & 3, op.f, &pos, &dir, attacker, op.c);
        else
            p->Hit(E_HitType(op.b & 3), op.f, pos, dir, reinterpret_cast<C_Actor *>(attacker), op.c);
        return F("Hit %d ", pi) + Fl(op.f);
    }
    case OP_SET_TARGET: {
        // A human's frame mostly; another frame; none.
        const int k = op.c % 8;
        uint8_t *target = nullptr;
        if (k < 5) {
            if (uint8_t *h = HumanActor(op.a))
                target = reinterpret_cast<uint8_t *>(g_side ? At<uint32_t>(h, 0x20) : Ptr(reinterpret_cast<C_Actor *>(h)->GetFrame()));
        } else if (k < 7)
            target = WorldFrame(op.a);
        if (target)
            g_target[g_side] = target;
        const bool stop = op.d % 5 == 0;
        if (o)
            OriginalAt<void(TC *)(void *, int, void *)>(O_AA_SET_TARGET)(b, stop, target);
        else
            p->SetTarget(stop, AsFrame(target));
        return F("SetTarget %d %d %s", pi, int(stop), NameOf(Ptr(target)).c_str());
    }
    case OP_SAVE_LOAD: {
        // (In an entry of its own, as the actors module saves it; loaded from inside one.)
        const std::string file = F("aa_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_AaChunk>(O_AA_SAVE)(b, &save) : p->SaveGame(save);
        save.Ascend();
        // (An entry after it, as the actors module writes the next actor's: one the save left open would take it in.)
        save.Begin(0x3000);
        save.Write(int(7));
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        std::string hex;
        for (unsigned char ch : bytes)
            hex += F("%02x", ch);
        // (Loaded back now and then, into this plane or another: its bullets grow. Or a made file: every entry with
        // made values, an unknown entry, a target's name.)
        bool rl = false;
        const int load = op.c % 4;
        uint8_t *into = g_plane[g_side][(pi + op.b + 1) % g_planeCount];
        if (load) {
            std::string name = file;
            if (load >= 2) {
                S_Writer w;
                w.Begin(0x2000);
                // (Approaching (1) only with a target that is there: both programs read the target unchecked then.)
                const bool nowhere = load == 2 || op.d % 3 == 0;
                static const int states[] = {0, 2, 3, 0}, statesThere[] = {0, 1, 2, 3, 1, 1};
                w.EntryOf(1000, nowhere ? states[op.d % 4] : statesThere[op.d % 6]);
                for (uint16_t id : {1500, 2000, 2500, 3000})
                    if (op.d & (id / 500))
                        w.EntryOf(id, uint8_t((op.a >> (id / 500)) & 1));
                static const float values[] = {0.0f, 3.5f, 100.0f, 30.0f, -2.0f, 400.0f};
                for (uint16_t id : {3500, 4000, 4500, 5000, 5500})
                    if (op.a & (id / 500))
                        w.EntryOf(id, values[(op.d + id) % 6]);
                w.EntryOf(6000, op.d % 50 - 10);
                w.EntryOf(8000, op.a % 5 == 4 ? 0 : op.a % 4000);   // (no dust wait when placed far: see below)
                if (load == 3) {
                    // A target by name (a world frame's, or one nowhere), the bullets.
                    const I3D_frame *f = AsFrame(WorldFrame(op.a));
                    const std::string target = !nowhere && f && f->m_name ? f->m_name : "nowhere";
                    w.Begin(7000);
                    w.Raw(target.c_str(), target.size() + 1);
                    w.End();
                    w.Begin(6500);
                    const int n = op.b + 1;
                    w.Raw(&n, 4);
                    for (int k = 0; k < n; ++k) {
                        const int time = k * 40 - 20;
                        const float v[6] = {float(k), 1.0f, 2.0f, 10.0f + float(k), 0.0f, -5.0f};
                        w.Raw(&time, 4);
                        w.Raw(v, sizeof v);
                    }
                    w.End();
                } else {
                    // (An unknown entry, last: entered and not left.)
                    w.EntryOf(0x1234, 5);
                }
                w.End();
                name = F("aa_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_AaChunk>(O_AA_LOAD)(into, &chunk) : reinterpret_cast<C_AirAttack *>(into)->LoadGame(chunk);
            chunk.Close();
            // The position the base's LoadGame (logged here) restores: near the plane's target now and then (its run's
            // end: in sight, within the guns' range, over it).
            const I3D_frame *target = AsFrame(reinterpret_cast<uint8_t *>(o ? At<uint32_t>(into, 0x160)
                                                                           : Ptr(reinterpret_cast<C_AirAttack *>(into)->m_target)));
            if (target && op.a % 3) {
                const S_vector &at = const_cast<I3D_frame *>(target)->GetWorldPos();
                static const float offsets[] = {150.0f, 30.0f, -60.0f, 10.0f, 600.0f};
                const S_vector4 pos = {at.x + offsets[op.a % 5], at.y + 20.0f + float(op.d % 3) * 10.0f,
                                       at.z + offsets[(op.a / 5) % 4], 1.0f};
                if (o)
                    Mut<S_vector4>(into, 0x30) = pos;
                else
                    reinterpret_cast<C_AirAttack *>(into)->m_position = pos;
                // (The camera with it in a case that has it near, or placed far: the dust.)
                if (g_cameraNear || op.a % 5 == 4)
                    if (uint8_t *camera = *reinterpret_cast<uint8_t **>(W().scene + 0x15C)) {
                        S_matrix &m = AsFrame(camera)->m_localMatrix;
                        m.m[3][0] = pos.x + 10.0f;
                        m.m[3][1] = 2.0f;
                        m.m[3][2] = pos.z - 20.0f;
                        AsFrame(camera)->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
                    }
            }
        }
        return F("SaveGame %d -> %d LoadGame %d into %s -> %d bytes ", pi, int(rs), load, NameOf(Ptr(into)).c_str(), int(rl)) + hex;
    }
    case OP_RESTART: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_AaInt>(O_AA_RESTART)(b, server);
        else
            p->RestartMission(server);
        return F("RestartMission %d %d", pi, int(server));
    }
    case OP_STOP_START: {
        const bool server = op.c & 1;
        const int what = op.d % 3;                      // (0: stop and start; 1: stop; 2: start)
        if (what != 2) {
            if (o) {
                OriginalAt<T_AaInt>(O_AA_STOP)(b, server);
                // (The port's fix in the original's copy: its playing motor and gun sounds are released, their
                // pointers left; an update or a pause calls through them.)
                Mut<uint32_t>(b, 0x168) = 0;
                Mut<uint32_t>(b, 0x16C) = 0;
            } else
                p->StopMission(server);
        }
        if (what != 1) {
            if (o)
                OriginalAt<T_AaInt>(O_AA_START)(b, server);
            else
                p->StartMission(server);
        }
        return F("StopStart %d %d %d", pi, what, int(server));
    }
    case OP_FIND: {
        uint8_t *human = HumanActor(op.a);
        if (!human)
            return "FindAirAttackFor (none)";
        const uint32_t r = o ? Ptr(OriginalAt<void *(__fastcall *)(void *)>(O_AA_FIND_FOR)(human))
                             : Ptr(FindAirAttackFor(reinterpret_cast<C_Human *>(human)));
        return F("FindAirAttackFor %s -> %s", NameOf(Ptr(human)).c_str(), NameOf(r).c_str());
    }
    case OP_PAUSE: {
        const bool paused = op.c & 1;
        if (o)
            OriginalAt<void(__fastcall *)(int)>(O_AA_PAUSE)(paused);
        else
            PauseAirAttackSounds(paused);
        return F("PauseAirAttackSounds %d", int(paused));
    }
    case OP_RAY: {
        alignas(16) S_vector4 from = {float(op.a % 200 - 100), float(op.b * 10), float(op.c % 200 - 100), 1.0f};
        alignas(16) S_vector4 dir = {float(op.d % 50 - 25), float(op.a % 7 - 50), float(op.c % 9 - 4), 0.0f};
        if (op.d % 7 == 0)
            dir = {0.0f, 0.0f, 0.0f, 0.0f};
        alignas(16) S_vector4 pos = {7.0f, 7.0f, 7.0f, 7.0f}, normal = {8.0f, 8.0f, 8.0f, 8.0f};
        const bool outs = op.c % 3 != 0;
        const uint32_t types = op.d & 1 ? 0xFFFFFFFFu : 0u, flags = op.d & 2 ? 0x820000u : 0u;
        const uint32_t r = o ? Ptr(OriginalAt<T_Ray>(O_AA_RAY)(&from, &dir, outs ? &pos : nullptr, outs ? &normal : nullptr, types, flags))
                             : Ptr(RayTestPastBorders(from, dir, outs ? &pos : nullptr, outs ? &normal : nullptr, types, flags));
        return "RayTestPastBorders -> " + NameOf(r) + " " + V4(&pos.x) + " " + V4(&normal.x);
    }
    case OP_SET_FRAME: {
        // (Its frame again: the properties' defaults back.)
        const bool r = o ? OriginalAt<bool(TC *)(void *, void *)>(O_AA_SET_FRAME)(b, g_planeModel[1][pi])
                         : p->SetFrame(AsFrame(g_planeModel[0][pi]));
        return F("SetFrame %d -> %d", pi, int(r));
    }
    default: {
        const std::string file = F("aa_props%d.bin", index);
        C_chunk chunk;
        chunk.Open(file.c_str(), true);
        const bool r = o ? OriginalAt<T_AaChunk>(O_AA_WRITE)(b, &chunk) : p->WriteProperties(chunk);
        chunk.Close();
        std::string bytes;
        FileText(file, bytes);
        std::string hex;
        for (unsigned char ch : bytes)
            hex += F("%02x", ch);
        return F("WriteProperties %d -> %d ", pi, int(r)) + hex;
    }
    }
}

void ClearAaCaches()
{
    if (g_side)
        OriginalAt<T_Void>(0x006d87a0)(OriginalAt<void *>(O_SOUND_CACHE));
    else
        g_soundCache.Clear();
}

volatile DWORD g_caseStart;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("aa_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_aaShow;
void RunAaCase(const S_AaCase &c)
{
    g_caseStart = GetTickCount();
    ++g_aaCases;
    g_holdFrees = true;
    g_current = F("case %d", g_aaCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    for (size_t i = 0; i < c.planes.size(); ++i)
        g_overrides[Lower(F("aa_props_%d.bin", int(i)))] = c.planes[i].props;
    g_planeCount = int(c.planes.size());
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_world[side].frames.reserve(65536);
        g_texts[side].clear();
        g_target[side] = nullptr;
        g_lastHit[side] = S_vector(0.0f, 0.0f, 0.0f);
        g_rayNext[side] = 0;
        BuildWorld(side, worldSeed);
        g_sndDriver[side] = Block(0x40);
        *reinterpret_cast<void ***>(g_sndDriver[side]) = EngineVtable("ISND_driver");
        Name(g_sndDriver[side], "sound driver");
        // The planes' models under the primary sector, placed and headed.
        uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
        for (int i = 0; i < g_planeCount; ++i) {
            const S_AaPlane &pc = c.planes[size_t(i)];
            uint8_t *model = Child(sector, FRAME_MODEL, F("plane%d", i), nullptr);
            BuildPlane(model);
            S_matrix &m = AsFrame(model)->m_localMatrix;
            m.Identity();
            m.m[0][0] = std::cos(pc.heading);
            m.m[0][2] = -std::sin(pc.heading);
            m.m[2][0] = std::sin(pc.heading);
            m.m[2][2] = std::cos(pc.heading);
            if (pc.oddHeading) {
                static const float rows[4][3] = {{1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1e-5f}, {0.0f, 0.0f, 0.0f},
                                                 {0.0f, 0.0f, 1e-5f}};
                for (int k = 0; k < 3; ++k)
                    m.m[2][k] = rows[pc.oddHeading - 1][k];
            }
            m.m[3][0] = pc.x;
            m.m[3][1] = pc.y;
            m.m[3][2] = pc.z;
            g_planeModel[side][i] = model;
        }
        g_cameraNear = c.cameraNear;
        if (c.cameraNear)
            if (uint8_t *camera = *reinterpret_cast<uint8_t **>(W().scene + 0x15C)) {
                S_matrix &m = AsFrame(camera)->m_localMatrix;
                m.m[3][0] = c.planes[0].x + 10.0f;
                m.m[3][1] = 2.0f;
                m.m[3][2] = c.planes[0].z - 20.0f;
            }
        // The game: the cutscene, the sight module's delay, the role.
        if (side) {
            *OriginalAt<uint8_t *>(O_SCRIPT_MODULE + 0x824) = c.cutscene;
            *OriginalAt<int *>(O_SIGHT_MODULE + 0x34) = c.sightDelay;
            uint8_t *list = OriginalAt<uint8_t *>(O_G_AIR_ATTACKS);
            Mut<uint32_t>(list, 8) = Mut<uint32_t>(list, 4);     // (emptied: the last made ones' blocks are gone)
        } else {
            g_scriptModule.m_cutsceneRunning = c.cutscene;
            g_sightModule.m_startDelay = c.sightDelay;
            g_airAttacks.clear();
        }
        SetRole(c.role);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        MakePlanes(c);
        for (int i = 0; i < g_planeCount; ++i) {
            const S_AaPlane &pc = c.planes[size_t(i)];
            uint8_t *b = g_plane[side][i];
            C_AirAttack *p = PortPlane(i);
            // SetFrame first (the property defaults), as the actors module does.
            if (side)
                OriginalAt<bool(TC *)(void *, void *)>(O_AA_SET_FRAME)(b, g_planeModel[1][i]);
            else
                p->SetFrame(AsFrame(g_planeModel[0][i]));
            if (pc.readProps) {
                C_chunk props;
                props.Open(F("aa_props_%d.bin", i).c_str(), false);
                uint16_t id;
                props.Descend(id);
                const bool ok = side ? OriginalAt<T_AaChunk>(O_AA_READ)(b, &props) : p->ReadProperties(props);
                props.Close();
                out.push_back(F("ReadProperties %d -> %d", i, int(ok)));
            }
            if (pc.serverStart) {
                if (side)
                    OriginalAt<T_AaInt>(O_AA_START)(b, 1);
                else
                    p->StartMission(true);
            }
            if (pc.started) {
                if (side)
                    OriginalAt<T_AaInt>(O_AA_START)(b, 0);
                else
                    p->StartMission(false);
            }
        }
        for (const std::string &s : PlanesState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_AA_TRACE") ? std::atoi(std::getenv("HD2_AA_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_aaCases)
                std::printf("trace %s op %d kind %s plane %d a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i),
                            OP_KIND_NAMES[c.ops[i].kind], c.ops[i].plane, c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunAaOp(c.ops[i], int(i)));
            if (trace == g_aaCases) {
                std::printf("trace   -> %s\n", out.back().substr(0, 160).c_str());
                std::fflush(stdout);
            }
            for (const std::string &s : PlanesState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        for (int i = 0; i < g_planeCount; ++i) {
            uint8_t *b = g_plane[side][i];
            if (side) {
                OriginalAt<T_AaInt>(O_AA_STOP)(b, 0);
                OriginalAt<T_Void>(O_AA_DTOR)(b);
            } else {
                PortPlane(i)->StopMission(false);
                PortPlane(i)->~C_AirAttack();
            }
        }
        {
            std::string list = "after: list";
            if (side)
                list += Names(OVec(OriginalAt<uint8_t *>(O_G_AIR_ATTACKS), 0));
            else
                list += Names(PVec(g_airAttacks));
            out.push_back(list);
        }
        // The creator: a plane made and destroyed (the original's creator at 0x008ac9d4, its Create and Destroy).
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(0x005e2c30)(OriginalAt<void *>(O_AA_CREATOR));
            Name(made, "created plane");
            out.push_back("created, list" + Names(OVec(OriginalAt<uint8_t *>(O_G_AIR_ATTACKS), 0)));
            OriginalAt<void(TC *)(void *, void *)>(0x005e2ca0)(OriginalAt<void *>(O_AA_CREATOR), made);
            out.push_back("destroyed, list" + Names(OVec(OriginalAt<uint8_t *>(O_G_AIR_ATTACKS), 0)));
        } else {
            C_Actor *made = g_airAttackCreator.Create();
            Name(made, "created plane");
            out.push_back("created, list" + Names(PVec(g_airAttacks)));
            g_airAttackCreator.Destroy(made);
            out.push_back("destroyed, list" + Names(PVec(g_airAttacks)));
        }
        g_case = "";
        for (const std::string &s : AaFrameStates())
            out.push_back(s);
        for (const std::string &s : g_log[side])
            out.push_back("call " + TinyZero(MaskSmokeMatrix(s)));
        ClearAaCaches();
        for (int i = 0; i < MAX_PLANES; ++i)
            g_plane[side][i] = nullptr;
    }
    static const int only = std::getenv("HD2_AA_ONLY") ? std::atoi(std::getenv("HD2_AA_ONLY")) : 0;
    if (g_aaShow && (!only || only == g_aaCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_aaShow == 2)
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

using T_AddDir = void(TC *)(void *, const char *);
void SetUpAirAttack()
{
    SetUpCmd();
    SetUpAirAttackAnswers();
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
        {0x007e531a, reinterpret_cast<const void *>(&O_ISndGetDriver)},
        {0x0041d920, reinterpret_cast<const void *>(&A_OnDeath)},
        {0x005ac4c0, reinterpret_cast<const void *>(&O_GetSector)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    {
        auto onDeath = &C_Script::OnDeath;
        void *at;
        std::memcpy(&at, &onDeath, sizeof at);
        PatchPort(at, reinterpret_cast<const void *>(&A_OnDeath));
    }
    // The sound cache (both sides' real; the original's built here).
    if (*OriginalAt<uint32_t *>(0x0098bcc0) != 2) {
        InitializeCriticalSection(OriginalAt<CRITICAL_SECTION *>(0x0098bca8));
        *OriginalAt<uint32_t *>(0x0098bcc0) = 2;
    }
    OriginalAt<void (*)()>(0x006d5370)();
    *OriginalAt<uint32_t *>(O_SOUND_CACHE + 0x34) = 256;
    uint8_t *buf = static_cast<uint8_t *>(O_New(8 * 256));
    *OriginalAt<uint8_t **>(O_SOUND_CACHE + 0x18) = buf;
    *OriginalAt<uint8_t **>(O_SOUND_CACHE + 0x1C) = buf;
    *OriginalAt<uint8_t **>(O_SOUND_CACHE + 0x20) = buf + 8 * 256;
    reinterpret_cast<T_AddDir>((*OriginalAt<void ***>(O_SOUND_CACHE))[0x18 / 4])(OriginalAt<void *>(O_SOUND_CACHE), "sounds\\");
    g_soundCache.AddDir("sounds\\");
    // The original's list of planes: empty, its storage the original's heap.
    uint8_t *list = OriginalAt<uint8_t *>(O_G_AIR_ATTACKS);
    uint8_t *store = static_cast<uint8_t *>(O_New(4 * 64));
    Mut<uint8_t *>(list, 4) = store;
    Mut<uint8_t *>(list, 8) = store;
    Mut<uint8_t *>(list, 0xC) = store + 4 * 64;
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("aa_diff: main");
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
        std::printf("aa_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_AA_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_AA_SHOW"))
        g_aaShow = std::atoi(s) ? std::atoi(s) : 1;
    g_dump = std::getenv("HD2_AA_DUMP") != nullptr;
    int cases = std::getenv("HD2_AA_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_AA_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpAirAttack();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunAaCase(RandomAaCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("aa_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_aaCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
