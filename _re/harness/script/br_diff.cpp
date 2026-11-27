// Barrel part 1: the barrels (game/barrel.cpp, the original's 0x00606250..0x0060a6f0) against the original executable's.
// cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the barrels' calls go through br_calls.s /
// br_calls.inc (gen_cmd_calls.py br). The LS3DF math (the inverse, the quaternions, the turn about y) is LS3DF.dll's on
// both sides; the model cache is real on both sides (game/resource_cache.cpp; the original's inline OpenInto on its
// own, built here), its models' Open logged and answered; a script's OnDeath is logged on both sides (the port's patched,
// the original's redirected); the frames' material is the original's GetFrameMaterial on both sides.
//
// Each case builds the same world on each side (cmd_diff's) and one or two barrels: frames under the primary sector
// (turned, tilted) with a surface ("dekl" or another name) and a body (now and then missing), frame data with a script
// or none, makes the barrels on each side (the original's by its constructor 0x00606380 in a block of 0x210, the port's
// by its own) and runs the same random sequence of operations in the case's role: properties read (made ones: every
// entry, any order, foreign ones), CopyFrom, starts, stops, restarts, updates and ticks (the tip, the slosh, the leak at
// their edges), hits (bullets, explosions and others into the body, the surface or elsewhere, amounts about the hit
// points), bumps (OnCollisionEx), the clients' messages, the saved game's calls (the file just written, made ones),
// GetMaterialId. After each operation the answer, every barrel's state (every field, its volume, its holes), the frames
// and every call made into the game and the engine (the volumes, the particles, the messages, the destroyed model) are
// compared; the saves' bytes too. The creator makes and destroys one.
// MinGW; build_br.bat; runs br_diff.exe ("br_diff: N checks, 0 failures"). Env: HD2_BR_CASES (default 1,500;
// HD2_BR_QUICK: 300), HD2_BR_SEED, HD2_BR_SHOW (1: the port's lines; 2: both), HD2_BR_ONLY (the case shown),
// HD2_BR_TRACE (a case's operations as they start).
#define HARNESS_NAME "br_diff"
#define CMD_CALLS_INC "br_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/barrel.h"
#include "game/collision.h"
#include "game/resource_cache.h"
#include "script/script.h"
#undef protected
#undef private

// ---- the port's side of what the barrels call and the test provides ----
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
S_matrix __stdcall RotateYMatrix(float angle)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_matrix *, float)>("?RotateYMatrix@@YG?AUS_matrix@@M@Z");
    S_matrix m;
    f(&m, angle);
    return m;
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
S_quat __stdcall S_quat::RotateByMatrix(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, S_quat *, const S_matrix &)>(
        "?RotateByMatrix@S_quat@@QBG?AU1@ABUS_matrix@@@Z");
    S_quat q;
    f(this, &q, m);
    return q;
}
S_quat __stdcall S_quat::Slerp(const S_quat &q, float t, bool shortestPath) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, S_quat *, const S_quat &, float, bool)>(
        "?Slerp@S_quat@@QBG?AU1@ABU1@M_N@Z");
    S_quat r;
    f(this, &r, q, t, shortestPath);
    return r;
}
S_vector __stdcall S_quat::GetDir() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, float *)>("?GetDir@S_quat@@QBG?AUS_vector@@XZ");
    alignas(16) float r[4] = {};
    f(this, r);
    return S_vector(r[0], r[1], r[2]);
}
void __stdcall S_quat::SetDir(const S_vector &dir, float roll)
{
    static const auto f = Ls3dfFn<void(__stdcall *)(S_quat *, const S_vector &, float)>("?SetDir@S_quat@@QAGXABUS_vector@@M@Z");
    f(this, dir, roll);
}
// A frame's material (the original's own function on both sides, lower-casing the user properties: not logged).
int GetFrameMaterial(const I3D_frame *frame)
{
    return OriginalAt<int(__attribute__((fastcall)) *)(const I3D_frame *)>(0x004511e0)(frame);   // (ECX the frame)
}
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
// The actors' creators (the barrels' registers itself before main: a table of the test's).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the barrel's vtable holds but the test never reaches.
void C_Actor::Init() {}
void C_Actor::Done() {}
int C_Actor::GetSeatAmmoCount(int) { return 0; }
void C_Actor::GetBoxCorners(S_vector4 *) {}

namespace H {

#define OT __attribute__((thiscall))
int __cdecl O_Atexit(void *) { return 0; }
void *__cdecl O_AlignedMalloc(size_t size, size_t align)
{
    void *p = _aligned_malloc(size ? size : 1, align);
    std::memset(p, 0, size ? size : 1);
    return p;
}
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }
// A script's OnDeath (the port's own patched to this at the start; the original's redirected): logged.
void OT B_OnDeath(void *script) { g_log[g_side].push_back("C_Script::OnDeath(" + NameOf(reinterpret_cast<uint32_t>(script)) + ")"); }
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

// ---- the barrels ----
constexpr int MAX_BARRELS = 2;
uint8_t *g_barrel[2][MAX_BARRELS];
uint8_t *g_barrelFrame[2][MAX_BARRELS];
uint8_t *g_barrelSurface[2][MAX_BARRELS];
uint8_t *g_barrelBody[2][MAX_BARRELS];
uint8_t *g_barrelScript[2];
int g_barrelCount;
C_Barrel *PortBarrel(int i) { return reinterpret_cast<C_Barrel *>(g_barrel[0][i]); }
constexpr uint32_t O_BR_CTOR = 0x00606380, O_BR_DTOR = 0x00606490, O_BR_UPDATE = 0x006071b0, O_BR_TICK = 0x006071c0,
                   O_BR_MESSAGE = 0x00609220, O_BR_READ = 0x0060a280, O_BR_COPY = 0x00606520, O_BR_START = 0x00606650,
                   O_BR_STOP = 0x00606bb0, O_BR_RESTART = 0x00606340, O_BR_SAVE = 0x00609a30, O_BR_LOAD = 0x00609c70,
                   O_BR_COLLISION = 0x00609510, O_BR_HIT = 0x00608e40, O_BR_MATERIAL = 0x006094f0;
constexpr uint32_t O_BR_CREATOR = 0x008aca24, O_BR_CREATE = 0x006062b0, O_BR_DESTROY = 0x00606320;
constexpr uint32_t O_MODEL_CACHE = 0x008aeda0;

// The messages' words (ReadBits answers them in turn on each side).
std::deque<uint32_t> g_bits[2];

void SetUpBarrelAnswers()
{
    g_answers["I3D_frame::SetOn"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_frameFlags = (a[1] & 0xFF) ? (f->m_frameFlags | 1u) : (f->m_frameFlags & ~1u);
        return 0u;
    };
    g_answers["I3D_frame::SetName"] = [](const std::vector<uint32_t> &a) {
        reinterpret_cast<I3D_frame *>(a[0])->m_name = Text(a[1] ? reinterpret_cast<const char *>(a[1]) : "");
        return 0u;
    };
    // Frames by name under a frame (any type).
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        const std::string want = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        std::function<uint32_t(I3D_frame *)> find = [&](I3D_frame *p) -> uint32_t {
            for (I3D_frame *c = p->m_firstChild; c; c = c->m_nextSibling) {
                if ((c->m_name ? c->m_name : "") == want)
                    return Ptr(c);
                if (uint32_t d = find(c))
                    return d;
            }
            return 0u;
        };
        return find(reinterpret_cast<I3D_frame *>(a[0]));
    };
    // A hierarchy's box: filled and returned (min +0, max +0x10), by turns (none empty).
    g_answers["I3D_driver::GetHierarchyBBox"] = [](const std::vector<uint32_t> &a) {
        float *box = reinterpret_cast<float *>(a[1]);
        static const float boxes[][6] = {{-0.3f, 0.0f, -0.3f, 0.3f, 1.0f, 0.3f}, {-0.25f, -0.5f, -0.2f, 0.25f, 0.9f, 0.3f},
                                         {-0.4f, 0.0f, -0.4f, 0.4f, 1.2f, 0.4f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
                                         {-0.3f, 0.0f, -0.5f, 0.3f, 1.0f, 0.2f}};
        const float *b = boxes[NextFake() % 5];
        const float v[8] = {b[0], b[1], b[2], 0.0f, b[3], b[4], b[5], 0.0f};
        std::memcpy(box, v, sizeof v);
        return a[1];
    };
    // The models: opened when the name says "ok" (anywhere in the path); a copy's frames copied.
    g_answers["I3D_model::Open"] = [](const std::vector<uint32_t> &a) {
        const std::string file = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        return file.find("ok") != std::string::npos ? 0u : uint32_t(I3DERR_NOFILE);
    };
    g_answers["I3D_model::DuplicateModel"] = [](const std::vector<uint32_t> &) { return 0u; };
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
        return 0u;
    };
    // The actors' messages: the world's bit stream; their words in turn.
    g_answers["CreateActorMessage"] = [](const std::vector<uint32_t> &) { return Ptr(W().bitstream); };
    g_answers["C_BitStream::ReadBits"] = [](const std::vector<uint32_t> &) {
        if (g_bits[g_side].empty())
            return 0u;
        const uint32_t w = g_bits[g_side].front();
        g_bits[g_side].pop_front();
        return w;
    };
    // The random floats (0..1): by turns, the edges among them.
    g_answers["RandFloat"] = [](const std::vector<uint32_t> &) {
        static const float values[] = {0.0f, 0.5f, 0.999f, 0.25f, 1.0f, 0.75f};
        uint32_t w;
        std::memcpy(&w, &values[NextFake() % 6], 4);
        return w;
    };
}

// ---- the compared state ----
std::string V3(const float *v) { return VectorText(Ptr(v), 3); }
std::string V4(const float *v) { return VectorText(Ptr(v), 4); }
std::string Fl(float f) { return FloatText(f); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::string RepText(uint32_t rep) { return rep ? std::string(reinterpret_cast<const char *>(rep + 8)) : std::string("(none)"); }
std::string StrText(const C_str &s) { return s.c_str() ? std::string(s.c_str()) : std::string("(none)"); }
// The volume (S_CollisionVolume: the same layout on both sides; its inverse matrix the barrel's own; its centre's w a
// stale word in the original: left out).
std::string VolumeText(const S_CollisionVolume &v, const void *inv)
{
    return F("volume flags %x type %x frame %s actor %s matrix %s inv %s radius ", v.flags, v.type, NameOf(Ptr(v.frame)).c_str(),
             NameOf(Ptr(v.actor)).c_str(), NameOf(Ptr(v.matrix)).c_str(), v.invMatrix == inv ? "own" : NameOf(Ptr(v.invMatrix)).c_str()) +
           Fl(v.radius) + " centre " + V3(&v.center.x) + " box " + V3(&v.bbox.min.x) + " " + V3(&v.bbox.max.x);
}
std::vector<uint32_t> OVec(const uint8_t *p, uint32_t off)
{
    std::vector<uint32_t> out;
    const uint32_t first = At<uint32_t>(p, off + 4), last = At<uint32_t>(p, off + 8);
    for (uint32_t a = first; a && a < last; a += 4)
        out.push_back(*reinterpret_cast<const uint32_t *>(a));
    return out;
}
std::vector<std::string> BarrelState(int i)
{
    std::vector<std::string> out;
    const uint8_t *b = g_barrel[g_side][i];
    if (g_side) {
        std::string holes = " holes";
        for (uint32_t h : OVec(b, 0x1F0))
            holes += " " + Fl(*reinterpret_cast<const float *>(h)) + F("/%d", *reinterpret_cast<const int *>(h + 4));
        out.push_back(F("barrel%d empty %d sloshing %d damped %d leak %d sloshTime %d surface %s %s body %s %s tipping %d back %d "
                        "tipTime %d destroyedModel %s explosion %d hp %d volumeOut %d destroyed %s exitHoles %d",
                        i, int(b[0xD8]), int(b[0xD9]), int(b[0xDA]), At<int>(b, 0xDC), At<int>(b, 0xE0), RepText(At<uint32_t>(b, 0xE4)).c_str(),
                        NameOf(At<uint32_t>(b, 0xE8)).c_str(), RepText(At<uint32_t>(b, 0x124)).c_str(), NameOf(At<uint32_t>(b, 0x120)).c_str(),
                        int(b[0x128]), int(b[0x129]), At<int>(b, 0x12C), RepText(At<uint32_t>(b, 0x130)).c_str(), At<int>(b, 0x134),
                        At<int>(b, 0x138), int(b[0x144]), NameOf(At<uint32_t>(b, 0x148)).c_str(), int(b[0x200])) +
                      holes);
        out.push_back("  levels " + Fl(At<float>(b, 0xC0)) + " " + Fl(At<float>(b, 0xC4)) + " " + Fl(At<float>(b, 0xC8)) + " rate " +
                      Fl(At<float>(b, 0xCC)) + " slosh " + Fl(At<float>(b, 0xD0)) + " " + Fl(At<float>(b, 0xD4)) + " health " +
                      Fl(At<float>(b, 0x13C)) + " explosionLevel " + Fl(At<float>(b, 0x140)) + " radius " + Fl(At<float>(b, 0x204)));
        out.push_back("  rots " + V4(FP(b, 0x90)) + " " + V4(FP(b, 0xA0)) + " " + V4(FP(b, 0xB0)) + " " + V4(FP(b, 0xF0)) + " " +
                      V4(FP(b, 0x100)) + " bodyPos " + V3(FP(b, 0x110)));
        out.push_back("  inv " + VectorText(Ptr(b + 0x150), 16));
        out.push_back("  " + VolumeText(*reinterpret_cast<const S_CollisionVolume *>(b + 0x190), b + 0x150));
        return out;
    }
    const C_Barrel *p = PortBarrel(i);
    std::string holes = " holes";
    for (const C_Barrel::S_Hole *h : p->m_holes)
        holes += " " + Fl(h->height) + F("/%d", h->particle);
    out.push_back(F("barrel%d empty %d sloshing %d damped %d leak %d sloshTime %d surface %s %s body %s %s tipping %d back %d "
                    "tipTime %d destroyedModel %s explosion %d hp %d volumeOut %d destroyed %s exitHoles %d",
                    i, int(p->m_empty), int(p->m_sloshing), int(p->m_sloshDamped), p->m_leakEffect, p->m_sloshTime,
                    StrText(p->m_surfaceName).c_str(), NameOf(Ptr(p->m_surface)).c_str(), StrText(p->m_bodyName).c_str(),
                    NameOf(Ptr(p->m_body)).c_str(), int(p->m_bodyTipping), int(p->m_bodyTippingBack), p->m_bodyTipTime,
                    StrText(p->m_destroyedModel).c_str(), p->m_explosionEffect, p->m_hitPoints, int(p->m_volumeOutWhenDestroyed),
                    NameOf(Ptr(p->m_destroyed)).c_str(), int(p->m_exitHoles)) +
                  holes);
    out.push_back("  levels " + Fl(p->m_emptyLevel) + " " + Fl(p->m_fullLevel) + " " + Fl(p->m_level) + " rate " + Fl(p->m_leakRate) +
                  " slosh " + Fl(p->m_sloshAmount) + " " + Fl(p->m_sloshPhase) + " health " + Fl(p->m_health) + " explosionLevel " +
                  Fl(p->m_explosionLevel) + " radius " + Fl(p->m_radius));
    out.push_back("  rots " + V4(reinterpret_cast<const float *>(&p->m_surfaceRot)) + " " + V4(reinterpret_cast<const float *>(&p->m_sloshFrom)) + " " + V4(reinterpret_cast<const float *>(&p->m_sloshTo)) + " " + V4(reinterpret_cast<const float *>(&p->m_bodyRot)) +
                  " " + V4(reinterpret_cast<const float *>(&p->m_bodyTipRot)) + " bodyPos " + V3(&p->m_bodyPos.x));
    out.push_back("  inv " + VectorText(Ptr(&p->m_invMatrix), 16));
    out.push_back("  " + VolumeText(p->m_volume, &p->m_invMatrix));
    return out;
}
std::string FrameText(const I3D_frame *f)
{
    if (!f)
        return "none";
    return F("%s flags %08x at ", f->m_name ? f->m_name : "", f->m_frameFlags) + VectorText(Ptr(&f->m_localMatrix.m[3][0]), 3) +
           " rot " + VectorText(Ptr(&f->m_rot), 4) + " parent " + NameOf(Ptr(f->m_parent));
}
std::vector<std::string> BarrelsState()
{
    std::vector<std::string> out;
    for (int i = 0; i < g_barrelCount; ++i)
        for (const std::string &s : BarrelState(i))
            out.push_back(s);
    for (int i = 0; i < g_barrelCount; ++i) {
        out.push_back(F("frame%d ", i) + FrameText(AsFrame(g_barrelFrame[g_side][i])));
        out.push_back(F("  surface%d ", i) + FrameText(AsFrame(g_barrelSurface[g_side][i])));
        out.push_back(F("  body%d ", i) + FrameText(AsFrame(g_barrelBody[g_side][i])));
        const uint32_t destroyed = g_side ? At<uint32_t>(g_barrel[1][i], 0x148) : Ptr(PortBarrel(i)->m_destroyed);
        if (destroyed)
            out.push_back(F("  destroyed%d ", i) + FrameText(reinterpret_cast<const I3D_frame *>(destroyed)));
    }
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
    void Str(const std::string &s) { out.append(s.c_str(), s.size() + 1); }
    template <class T> void EntryOf(uint16_t id, const T &v)
    {
        Begin(id);
        Raw(&v, sizeof v);
        End();
    }
};
enum { OP_READ, OP_COPY, OP_START, OP_STOP, OP_RESTART, OP_UPDATE, OP_TICK, OP_HIT, OP_BUMP, OP_MESSAGE, OP_SAVE_LOAD,
       OP_MATERIAL, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"ReadProperties", "CopyFrom", "Start", "Stop", "Restart", "Update", "Tick", "Hit", "Bump",
                                     "Message", "SaveLoad", "Material"};
struct S_BrOp {
    int kind = 0;
    int barrel = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_BrBarrel {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f, tilt = 0.0f;
    bool surface = true, body = true;
    int script = 0;                                     // (0 no frame data, 1 data without a script, 2 a script)
};
struct S_BrCase {
    std::vector<S_BrBarrel> barrels;
    std::vector<S_BrOp> ops;
    uint32_t role = GAME_ROLE_LOCAL;
};
int g_brCases, g_opCounts[OP_KINDS];
// (The tip's and the slosh's 400 ms at their edges, the leak's seconds.)
const int FRAME_TIMES[] = {0, 16, 100, 399, 400, 401, 800, 1000, 3000, 1, 250, 10000};
S_BrCase RandomBrCase()
{
    S_BrCase c;
    const int barrels = P(0.75) ? 1 : 2;
    for (int i = 0; i < barrels; ++i) {
        S_BrBarrel l;
        l.x = float(I(-40, 40)) * 0.5f;
        l.y = float(I(-4, 10)) * 0.5f;
        l.z = float(I(-40, 40)) * 0.5f;
        l.heading = float(I(0, 628)) * 0.01f;
        l.tilt = P(0.2) ? float(I(-30, 30)) * 0.01f : 0.0f;
        l.surface = P(0.9);
        l.body = P(0.9);
        l.script = I(0, 2);
        c.barrels.push_back(l);
    }
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    const int count = P(0.1) ? I(40, 120) : I(4, 40);
    for (int i = 0; i < count; ++i) {
        S_BrOp op;
        const int r = I(0, 99);
        op.kind = r < 6 ? OP_READ : r < 8 ? OP_COPY : r < 14 ? OP_START : r < 18 ? OP_STOP : r < 20 ? OP_RESTART : r < 36 ? OP_UPDATE
                : r < 48 ? OP_TICK : r < 72 ? OP_HIT : r < 80 ? OP_BUMP : r < 88 ? OP_MESSAGE : r < 96 ? OP_SAVE_LOAD : OP_MATERIAL;
        op.barrel = I(0, barrels - 1);
        op.a = I(0, 1000);
        op.b = I(0, 1000);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    // Properties read first, mostly; then started.
    for (int i = barrels; i-- > 0;) {
        if (P(0.9)) {
            S_BrOp start;
            start.kind = OP_START;
            start.barrel = i;
            start.c = I(0, 3);
            c.ops.insert(c.ops.begin(), start);
        }
        if (P(0.85)) {
            S_BrOp read;
            read.kind = OP_READ;
            read.barrel = i;
            read.a = I(0, 1000);
            read.b = I(0, 1000);
            read.c = I(0, 1000);
            read.d = I(0, 1000);
            c.ops.insert(c.ops.begin(), read);
        }
    }
    return c;
}

uint8_t *ActorOf(int type, int n)
{
    std::vector<uint8_t *> found;
    for (const auto &a : W().actors)
        if (a.type == type && (g_side ? *reinterpret_cast<uint8_t **>(a.bytes + 0x20) != nullptr
                                      : reinterpret_cast<C_Actor *>(a.bytes)->m_frame != nullptr))
            found.push_back(a.bytes);
    return found.empty() ? nullptr : found[size_t(n) % found.size()];
}

// (Not animated while sloshing without its surface or tipping without its body: both sides read through null. StopMission
// forgets the frames but not the slosh or the tip; only a stopped barrel or a bump's message to a barrel without a body
// gets there, and the game does not update a stopped actor.)
bool Animatable(int i)
{
    const uint8_t *b = g_barrel[g_side][i];
    const C_Barrel *p = PortBarrel(i);
    const bool sloshing = g_side ? b[0xD9] != 0 : p->m_sloshing;
    const bool surface = g_side ? At<uint32_t>(b, 0xE8) != 0 : p->m_surface != nullptr;
    const bool tipping = g_side ? b[0x128] != 0 : p->m_bodyTipping;
    const bool body = g_side ? At<uint32_t>(b, 0x120) != 0 : p->m_body != nullptr;
    return (!sloshing || surface) && (!tipping || body);
}
using T_BrInt = void(TC *)(void *, int);
using T_BrChunk = bool(TC *)(void *, void *);
using T_BrVoid = void(TC *)(void *, void *);
std::string Hex(const std::string &bytes)
{
    std::string hex;
    for (unsigned char ch : bytes)
        hex += F("%02x", ch);
    return hex;
}
void PushVector(const S_vector &v)
{
    for (float f : {v.x, v.y, v.z}) {
        uint32_t w;
        std::memcpy(&w, &f, 4);
        g_bits[g_side].push_back(w);
    }
}
// Places about the barrel (its frame's space turned into the world's): heights about its levels, sides and centre.
S_vector WorldPlace(int i, int n)
{
    static const float heights[] = {0.05f, 0.15f, 0.3f, 0.5f, 0.75f, 0.95f, 1.05f, 0.11f, 0.6f};
    static const float sides[][2] = {{0.3f, 0.0f}, {-0.3f, 0.0f}, {0.0f, 0.3f}, {0.2f, -0.2f}, {0.0f, 0.0f}, {1.0f, 1.0f}};
    const S_vector local(sides[n % 6][0], heights[(n / 6) % 9], sides[n % 6][1]);
    return local * AsFrame(g_barrelFrame[g_side][i])->GetMatrix();
}
std::string RunBrOp(const S_BrOp &op, int index)
{
    const bool o = g_side != 0;
    const int li = op.barrel < g_barrelCount ? op.barrel : 0;
    uint8_t *b = g_barrel[g_side][li];
    C_Barrel *p = PortBarrel(li);
    switch (op.kind) {
    case OP_READ: {
        // The outer entry (any id), the properties (each now and then) in any order, a foreign one now and then (entered,
        // not left: what is inside it read as properties).
        S_Writer w;
        w.Begin(uint16_t(op.d % 2 ? 0x0100 : 0x7777));
        std::vector<int> ids;
        for (int k = 0; k < 12; ++k)
            if ((op.a >> (k % 10)) & 1 || op.b % 4 == 0)
                ids.push_back(2000 + 1000 * k);
        for (size_t k = ids.size(); k > 1; --k)
            std::swap(ids[k - 1], ids[size_t(op.c + int(k)) % k]);
        // (Not a name past the 999 characters read: the original leaves its buffer unterminated, stack bytes after.)
        static const char *const surfaces[] = {"dekl", "surface", "", "missing"};
        static const char *const bodies[] = {"body", "telo", "", "missing"};
        static const char *const destroyedModels[] = {"barrel_ok.i3d", "barrel_bad.i3d", "", "plain_ok", "x.y_ok.4ds"};
        static const float fulls[] = {1.0f, 0.8f, 0.4f, 0.12f}, empties[] = {0.1f, 0.2f, 0.0f, 0.5f};
        static const float rates[] = {0.1f, 0.5f, 0.02f, 2.0f}, explosionLevels[] = {0.2f, 0.0f, 0.5f, 1.5f};
        static const int hps[] = {1000, 50, 0, 1, -5};
        const int foreignAt = op.d % 3 == 0 ? op.d % int(ids.size() + 1) : -1;
        for (size_t k = 0; k <= ids.size(); ++k) {
            if (int(k) == foreignAt) {
                w.Begin(uint16_t(op.d % 2 ? 0x0099 : 0x1234));
                if (op.d % 4 == 0)
                    w.EntryOf(uint16_t(9000), int(hps[op.b % 5]));
                w.End();
            }
            if (k == ids.size())
                break;
            const int id = ids[k];
            const int v = (op.b + int(k) * 7) % 1000;
            switch (id) {
            case 2000:
                w.Begin(2000);
                w.Str(surfaces[v % 4]);
                w.End();
                break;
            case 3000:
                w.EntryOf(3000, fulls[v % 4]);
                break;
            case 4000:
                w.EntryOf(4000, empties[v % 4]);
                break;
            case 5000:
                w.EntryOf(5000, rates[v % 4]);
                break;
            case 6000:
                w.EntryOf(6000, int(v % 3 == 0 ? -1 : 90 + v % 10));
                break;
            case 7000:
                w.Begin(7000);
                w.Str(bodies[v % 4]);
                w.End();
                break;
            case 8000:
                w.Begin(8000);
                w.Str(destroyedModels[v % 5]);
                w.End();
                break;
            case 9000:
                w.EntryOf(9000, int(hps[v % 5]));
                break;
            case 10000:
                w.EntryOf(10000, uint8_t(v % 2));
                break;
            case 11000:
                w.EntryOf(11000, int(v % 3 == 0 ? -1 : 20 + v % 7));
                break;
            case 12000:
                w.EntryOf(12000, explosionLevels[v % 4]);
                break;
            default:
                w.EntryOf(13000, uint8_t(v % 3 != 0));
                break;
            }
        }
        w.End();
        // (An entry after: where the properties leave the reader.)
        w.EntryOf(0x4444, int(3));
        const std::string name = F("br_props%d.bin", index);
        g_overrides[Lower(name)] = w.out;
        C_chunk chunk;
        chunk.Open(name.c_str(), false);
        const bool r = o ? OriginalAt<T_BrChunk>(O_BR_READ)(b, &chunk) : p->ReadProperties(chunk);
        // (The reader's depth and the next entry: the reader's Descend does not stop at the end of its parent.)
        const int depth = chunk.m_depth;
        uint16_t after = 0;
        chunk.Descend(after);
        chunk.Close();
        return F("ReadProperties %d -> %d depth %d next %04x ", li, int(r), depth, unsigned(after)) + Hex(w.out);
    }
    case OP_COPY: {
        const int from = (li + (op.a & 1)) % g_barrelCount;
        uint8_t *src = g_barrel[g_side][from];
        if (o)
            OriginalAt<T_BrVoid>(O_BR_COPY)(b, src);
        else
            p->CopyFrom(reinterpret_cast<C_Actor *>(src));
        return F("CopyFrom %d %d", li, from);
    }
    case OP_START: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_BrInt>(O_BR_START)(b, server);
        else
            p->StartMission(server);
        return F("StartMission %d %d", li, int(server));
    }
    case OP_STOP: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_BrInt>(O_BR_STOP)(b, server);
        else
            p->StopMission(server);
        return F("StopMission %d %d", li, int(server));
    }
    case OP_RESTART:
        if (o)
            OriginalAt<T_BrInt>(O_BR_RESTART)(b, op.c & 1);
        else
            p->RestartMission(op.c & 1);
        return F("RestartMission %d", li);
    case OP_UPDATE: {
        if (!Animatable(li))
            return "Update (sloshing or tipping without its frame)";
        const int t = FRAME_TIMES[op.a % 12];
        if (o)
            OriginalAt<T_BrInt>(O_BR_UPDATE)(b, t);
        else
            p->Update(t);
        return F("Update %d %d", li, t);
    }
    case OP_TICK: {
        if (!Animatable(li))
            return "Tick (sloshing or tipping without its frame)";
        const int t = FRAME_TIMES[op.a % 12];
        if (o)
            OriginalAt<T_BrInt>(O_BR_TICK)(b, t);
        else
            p->Tick(t);
        return F("Tick %d %d", li, t);
    }
    case OP_HIT: {
        // A bullet, an explosion or another hit; an amount about the hit points; into the surface, the body, elsewhere.
        static const int types[] = {HIT_BULLET, HIT_BULLET, HIT_EXPLOSION, 0, 3, HIT_BULLET};
        const int type = types[op.a % 6];
        static const float amounts[] = {0.0f, 10.0f, 49.0f, 50.0f, 51.0f, 1000.0f, 0.5f, 2000.0f};
        const float amount = amounts[(op.a / 6) % 8];
        const S_vector pos = WorldPlace(li, op.b);
        // (Tiny ones among them: Normalize's signs.)
        static const float dirs[][3] = {{0.0f, 0.0f, 1.0f}, {1.0f, -0.2f, 0.0f}, {-0.6f, 0.0f, 0.8f}, {0.0f, -1.0f, 0.0f},
                                        {0.3f, 0.1f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1e-5f, 0.0f, 0.0f}, {0.0f, 2e-5f, -1e-5f},
                                        {0.0f, 0.0f, 1e-5f}};
        const S_vector dir(dirs[op.c % 9][0], dirs[op.c % 9][1], dirs[op.c % 9][2]);
        const int where = op.d % 4;
        uint8_t *surface = g_barrelSurface[g_side][li];
        const int part = where == 0 && surface ? int(Ptr(surface)) : where == 1 ? int(Ptr(g_barrelBody[g_side][li])) : where == 2 ? 0 : 12345;
        uint8_t *attacker = ActorOf(ACTOR_HUMAN, op.d);
        // (Now and then a burst: the slosh piles up to its cap.)
        const int times = op.d % 25 == 0 ? 16 : 1;
        for (int k = 0; k < times; ++k) {
            if (o)
                OriginalAt<void(TC *)(void *, int, float, const S_vector *, const S_vector *, void *, int)>(O_BR_HIT)(b, type, amount,
                                                                                                             &pos, &dir, attacker, part);
            else
                p->Hit(E_HitType(type), amount, pos, dir, reinterpret_cast<C_Actor *>(attacker), part);
        }
        return F("Hit %d type %d x%d ", li, type, times) + Fl(amount) + " " + V3(&pos.x) + " " + V3(&dir.x) + F(" part %d", where);
    }
    case OP_BUMP: {
        uint8_t *other = ActorOf(op.a % 2 ? ACTOR_HUMAN : ACTOR_CAR, op.b);
        if (!other)
            return "OnCollisionEx (no actor)";
        const S_vector at(1.0f, 2.0f, 3.0f);
        const bool r = o ? OriginalAt<bool(TC *)(void *, void *, const S_vector *, int, int, int, int, int)>(O_BR_COLLISION)(b, other, &at, 0, 0,
                                                                                                                    0, 0, 0)
                         : p->OnCollisionEx(reinterpret_cast<C_Actor *>(other), &at, 0, 0, 0, 0, 0);
        return F("OnCollisionEx %d %s -> %d", li, NameOf(Ptr(other)).c_str(), int(r));
    }
    case OP_MESSAGE: {
        // The kind, then its words: a shot's place and direction, a bump's tip bit and vectors.
        const uint32_t kind = uint32_t(op.a % 5);
        g_bits[g_side].clear();
        g_bits[g_side].push_back(kind);
        const S_vector pos = WorldPlace(li, op.b);
        const S_vector dir(0.6f, -0.1f, 0.8f);
        if (kind == 0 || kind == 3) {
            PushVector(pos);
            PushVector(dir);
        } else if (kind == 1) {
            const bool tip = op.c & 1;
            g_bits[g_side].push_back(tip);
            if (tip)
                PushVector(S_vector(0.2f, 0.5f, -0.3f));
            PushVector(dir);
        }
        if (o)
            OriginalAt<T_BrVoid>(O_BR_MESSAGE)(b, W().bitstream);
        else
            p->OnMessage(reinterpret_cast<C_BitStream *>(W().bitstream));
        return F("OnMessage %d %u", li, kind);
    }
    case OP_SAVE_LOAD: {
        const std::string file = F("br_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_BrChunk>(O_BR_SAVE)(b, &save) : p->SaveGame(save);
        save.Ascend();
        save.Begin(0x3000);
        save.Write(int(7));
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        // (Loaded back now and then (into this one or the other): its file, or a made one: other flags, levels and
        // health (0 among them: destroyed on loading), a foreign entry, another outer id.)
        bool rl = false;
        const int load = op.c % 4;
        uint8_t *into = g_barrel[g_side][(li + (op.d & 1)) % g_barrelCount];
        unsigned next = 0;
        int depth = 0;
        if (load) {
            std::string name = file;
            if (load >= 2) {
                S_Writer w;
                w.Begin(0x2000);
                w.Begin(uint16_t(op.a % 9 == 0 ? 2 : 1));
                if (op.b % 3 == 0) {
                    w.Begin(0x0009);
                    w.End();
                }
                if (op.a & 1)
                    w.EntryOf(1000, uint8_t(op.b & 1));
                static const float levels[] = {1.0f, 0.5f, 0.1f, 0.05f, 0.9f};
                if (op.a & 2)
                    w.EntryOf(2000, levels[op.b % 5]);
                static const int healths[] = {0, 1000, 25, 0, -3};
                if (op.a & 4 || load == 3)
                    w.EntryOf(3000, healths[op.d % 5]);
                w.End();
                w.End();
                // (An entry after: where the load leaves the reader.)
                w.EntryOf(0x3000, int(7));
                name = F("br_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_BrChunk>(O_BR_LOAD)(into, &chunk) : reinterpret_cast<C_Barrel *>(into)->LoadGame(chunk);
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
    default: {
        uint8_t *frame = op.b % 3 == 0 ? g_barrelFrame[g_side][li] : op.b % 3 == 1 ? g_barrelSurface[g_side][li] : nullptr;
        const int r = o ? OriginalAt<int(TC *)(void *, void *)>(O_BR_MATERIAL)(b, frame) : p->GetMaterialId(AsFrame(frame));
        return F("GetMaterialId %d %s -> %d", li, NameOf(Ptr(frame)).c_str(), r);
    }
    }
}

void ClearModelCache()
{
    if (g_side)
        OriginalAt<T_Void>(0x006d77e0)(OriginalAt<void *>(O_MODEL_CACHE));
    else
        g_modelCache.Clear();
}

volatile DWORD g_caseStart;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("br_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_brShow;
void RunBrCase(const S_BrCase &c)
{
    g_caseStart = GetTickCount();
    ++g_brCases;
    g_holdFrees = true;
    g_current = F("case %d", g_brCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_barrelCount = int(c.barrels.size());
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_texts[side].clear();
        g_bits[side].clear();
        BuildWorld(side, worldSeed);
        uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
        g_barrelScript[side] = Block(0x400);
        Name(g_barrelScript[side], "script");
        for (int i = 0; i < g_barrelCount; ++i) {
            const S_BrBarrel &lc = c.barrels[size_t(i)];
            uint8_t *frame = Child(sector, FRAME_MODEL, F("barrel%d", i), nullptr);
            S_matrix &m = AsFrame(frame)->m_localMatrix;
            m.Identity();
            const float ch = std::cos(lc.heading), sh = std::sin(lc.heading), ct = std::cos(lc.tilt), st = std::sin(lc.tilt);
            m.m[0][0] = ch;
            m.m[0][2] = -sh;
            m.m[1][1] = ct;
            m.m[1][2] = st;
            m.m[2][0] = sh * ct;
            m.m[2][1] = -st;
            m.m[2][2] = ch * ct;
            m.m[3][0] = lc.x;
            m.m[3][1] = lc.y;
            m.m[3][2] = lc.z;
            if (lc.script) {
                S_FrameData *data = reinterpret_cast<S_FrameData *>(Block(0x20));
                data->script = lc.script == 2 ? reinterpret_cast<C_Script *>(g_barrelScript[side]) : nullptr;
                AsFrame(frame)->m_data = data;
            }
            g_barrelSurface[side][i] = lc.surface ? Child(frame, FRAME_VISUAL, "dekl", frame) : nullptr;
            if (g_barrelSurface[side][i])
                AsFrame(g_barrelSurface[side][i])->m_localMatrix.m[3][1] = 0.7f;
            uint8_t *body = lc.body ? Child(frame, FRAME_VISUAL, "body", frame) : nullptr;
            if (body) {
                AsFrame(body)->m_localMatrix.m[3][1] = 0.05f;
                AsFrame(body)->m_userProps = Text("matid 4");
            }
            g_barrelBody[side][i] = body;
            g_barrelFrame[side][i] = frame;
        }
        SetRole(c.role);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        for (int i = 0; i < g_barrelCount; ++i) {
            uint8_t *b;
            if (side) {
                b = Block(0x210 + 16);
                OriginalAt<T_Void>(O_BR_CTOR)(b);
                Mut<uint8_t *>(b, 0x20) = g_barrelFrame[1][i];
            } else {
                b = Block(sizeof(C_Barrel) + 16);
                new (b) C_Barrel();
                reinterpret_cast<C_Barrel *>(b)->m_frame = AsFrame(g_barrelFrame[0][i]);
            }
            // (The net id: odd and even barrels slosh opposite ways.)
            if (side)
                Mut<int>(b, 0x10) = i + int(randSeed % 3);
            else
                reinterpret_cast<C_Barrel *>(b)->m_netId = i + int(randSeed % 3);
            g_barrel[side][i] = b;
            Name(b, F("barrel %d", i));
        }
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_log[side].clear();
        for (const std::string &s : BarrelsState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_BR_TRACE") ? std::atoi(std::getenv("HD2_BR_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_brCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_KIND_NAMES[c.ops[i].kind],
                            c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunBrOp(c.ops[i], int(i)));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            for (const std::string &s : BarrelsState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        for (int i = 0; i < g_barrelCount; ++i) {
            uint8_t *b = g_barrel[side][i];
            if (side) {
                OriginalAt<T_BrInt>(O_BR_STOP)(b, 1);
                OriginalAt<T_Void>(O_BR_DTOR)(b);
            } else {
                PortBarrel(i)->StopMission(true);
                PortBarrel(i)->~C_Barrel();
            }
        }
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(O_BR_CREATE)(OriginalAt<void *>(O_BR_CREATOR));
            Name(made, "created barrel");
            out.push_back(F("created type %d", At<int>(made, 0x18)));
            OriginalAt<void(TC *)(void *, void *)>(O_BR_DESTROY)(OriginalAt<void *>(O_BR_CREATOR), made);
        } else {
            C_ActorCreator *creator = g_actorCreators[ACTOR_BARREL];
            C_Actor *made = creator->Create();
            Name(made, "created barrel");
            out.push_back(F("created type %d", int(made->GetType())));
            creator->Destroy(made);
        }
        // (The model cache emptied: its models are this world's frames.)
        ClearModelCache();
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        for (int i = 0; i < MAX_BARRELS; ++i)
            g_barrel[side][i] = nullptr;
    }
    static const int only = std::getenv("HD2_BR_ONLY") ? std::atoi(std::getenv("HD2_BR_ONLY")) : 0;
    if (g_brShow && (!only || only == g_brCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_brShow == 2)
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
void SetUpBarrel()
{
    SetUpCmd();
    SetUpBarrelAnswers();
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
        {0x0041d920, reinterpret_cast<const void *>(&B_OnDeath)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    {
        auto onDeath = &C_Script::OnDeath;
        void *at;
        std::memcpy(&at, &onDeath, sizeof at);
        PatchPort(at, reinterpret_cast<const void *>(&B_OnDeath));
    }
    // The model cache (both sides' real; the original's built here).
    if (*OriginalAt<uint32_t *>(0x0098bcc0) != 2) {
        InitializeCriticalSection(OriginalAt<CRITICAL_SECTION *>(0x0098bca8));
        *OriginalAt<uint32_t *>(0x0098bcc0) = 2;
    }
    OriginalAt<void (*)()>(0x006d5030)();
    *OriginalAt<uint32_t *>(O_MODEL_CACHE + 0x34) = 256;
    uint8_t *buf = static_cast<uint8_t *>(O_New(8 * 256));
    *OriginalAt<uint8_t **>(O_MODEL_CACHE + 0x18) = buf;
    *OriginalAt<uint8_t **>(O_MODEL_CACHE + 0x1C) = buf;
    *OriginalAt<uint8_t **>(O_MODEL_CACHE + 0x20) = buf + 8 * 256;
    reinterpret_cast<T_AddDir>((*OriginalAt<void ***>(O_MODEL_CACHE))[0x18 / 4])(OriginalAt<void *>(O_MODEL_CACHE), "models\\");
    g_modelCache.AddDir("models\\");
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("br_diff: main");
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
        std::printf("br_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_BR_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_BR_SHOW"))
        g_brShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_BR_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_BR_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpBarrel();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunBrCase(RandomBrCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("br_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_brCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
