// Bike part 1: the bicycles (game/bike.cpp, the original's 0x005d8760..0x005db780) against the original executable's.
// cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the bikes' calls go through bk_calls.s /
// bk_calls.inc (gen_cmd_calls.py bk). The LS3DF math (quaternions, products, AngleTo, the inverse) is LS3DF.dll's on
// both sides; AngleBetween2D is the original's on both sides; a script's Advance is logged on both sides (the port's
// patched, the original's redirected); the road network's points are the world's, placed about the bikes.
//
// Each case builds the same world on each side (cmd_diff's), one or two bikes: frames under the primary sector (turned,
// tilted) with their wheels, handlebars, cranks and seat frame ("kolo1", "kolo2", "riditka", "kliky", "ram"; now and
// then one missing), riders' frames (a human's, with a script or none, or a frame without an actor), the road points
// about the bikes, makes the bikes on each side (the original's by its constructor 0x005d9460 in a block of 0x1D0, the
// port's by its own) and runs the same random sequence of operations: properties read, CopyFrom, SetFrame, starts,
// stops, restarts, Drive (to a road point or off; the nearest point and the path found or not), updates (the ride, the
// turns, the ground under the front wheel or not, the pedals), the saved game's calls (the file just written, made
// ones). After each operation the answer, every bike's state (every field, its volume, its path), the frames and every
// call made into the game and the engine are compared; the saves' bytes too. The creator makes and destroys one.
// MinGW; build_bk.bat; runs bk_diff.exe ("bk_diff: N checks, 0 failures"). Env: HD2_BK_CASES (default 1,500;
// HD2_BK_QUICK: 300), HD2_BK_SEED, HD2_BK_SHOW (1: the port's lines; 2: both), HD2_BK_ONLY (the case shown),
// HD2_BK_TRACE (a case's operations as they start).
#define HARNESS_NAME "bk_diff"
#define CMD_CALLS_INC "bk_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/bike.h"
#include "game/checkpoints.h"
#include "game/human_movement.h"
#include "script/script.h"
#undef protected
#undef private

// ---- the port's side of what the bikes call and the test provides ----
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
S_vector __stdcall S_vector::RotateByMatrix(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_vector *, float *, const S_matrix &)>(
        "?RotateByMatrix@S_vector@@QBG?AU1@ABUS_matrix@@@Z");
    alignas(16) float r[4] = {};
    f(this, r, m);
    return S_vector(r[0], r[1], r[2]);
}
float __stdcall S_vector::AngleTo(const S_vector &v) const
{
    static const auto f = Ls3dfFn<float(__stdcall *)(const S_vector *, const S_vector &)>("?AngleTo@S_vector@@QBGMABU1@@Z");
    return f(this, v);
}
// The original's _CIasin (its argument and answer in ST0): FPATAN of x over sqrt(1 - x * x), NaN past 1 without the
// CRT's errno (the mapped copy's CRT never started: its error path crashes). The bytes: FLD ST0; FMUL ST0, ST0; FLD1;
// FSUBRP ST1, ST0; FSQRT; FPATAN; RET.
asm(".globl _B_CIasin\n"
    "_B_CIasin:\n"
    ".byte 0xD9, 0xC0, 0xD8, 0xC8, 0xD9, 0xE8, 0xDE, 0xE1, 0xD9, 0xFA, 0xD9, 0xF3, 0xC3\n");
extern "C" void B_CIasin();
// AngleBetween2D: the original's on both sides.
float AngleBetween2D(const float *a, const float *b)
{
    return OriginalAt<float(__attribute__((fastcall)) *)(const float *, const float *)>(0x00788a10)(a, b);   // (ECX, EDX)
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
// The actors' creators (the bikes' registers itself before main: a table of the test's).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the bike's vtable holds but the test never reaches.
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
// A script's Advance (the port's own patched to this at the start; the original's redirected): logged.
void OT B_Advance(void *script) { g_log[g_side].push_back("C_Script::Advance(" + NameOf(reinterpret_cast<uint32_t>(script)) + ")"); }
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

// ---- the bikes ----
constexpr int MAX_BIKES = 2, RIDERS = 3, PARTS = 5;
const char *const PART_NAMES[PARTS] = {"kolo1", "kolo2", "riditka", "kliky", "ram"};
uint8_t *g_bike[2][MAX_BIKES];
uint8_t *g_bikeFrame[2][MAX_BIKES];
uint8_t *g_bikePart[2][MAX_BIKES][PARTS];
uint8_t *g_rider[2][RIDERS];
uint8_t *g_riderHuman[2][RIDERS];
int g_bikeCount;
uint8_t *g_shadow[2];                                   // (the first bike's, made when first asked in a case)
C_Bike *PortBike(int i) { return reinterpret_cast<C_Bike *>(g_bike[0][i]); }
constexpr uint32_t O_BK_CTOR = 0x005d9460, O_BK_DTOR = 0x005d9530, O_BK_UPDATE = 0x005d95c0, O_BK_SET_FRAME = 0x005d9570,
                   O_BK_READ = 0x005db640, O_BK_COPY = 0x005d9590, O_BK_START = 0x005db0e0, O_BK_STOP = 0x005db4c0,
                   O_BK_RESTART = 0x005d8880, O_BK_SAVE = 0x005d88b0, O_BK_LOAD = 0x005d8b90, O_BK_DRIVE = 0x005d8f60;
constexpr uint32_t O_BK_CREATOR = 0x008ac954, O_BK_CREATE = 0x005d87f0, O_BK_DESTROY = 0x005d8860;

void SetUpBikeAnswers()
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
        static const float boxes[][6] = {{-0.3f, 0.0f, -0.9f, 0.3f, 1.1f, 0.9f}, {-0.25f, -0.1f, -0.8f, 0.2f, 1.0f, 0.85f}};
        const float *b = boxes[NextFake() % 2];
        const float v[8] = {b[0], b[1], b[2], 0.0f, b[3], b[4], b[5], 0.0f};
        std::memcpy(box, v, sizeof v);
        return a[1];
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
    // The road network: the nearest point (one of the first four, or none), a path (0..3 points, or none found).
    g_answers["C_CheckpointNetwork::FindNearestPoint"] = [](const std::vector<uint32_t> &) {
        return NextFake() % 5 ? NextFake() % 4 : uint32_t(CHECKPOINT_NONE);
    };
    g_answers["C_CheckpointNetwork::FindPath"] = [](const std::vector<uint32_t> &a) {
        if (NextFake() % 5 == 0)
            return 0u;
        std::vector<uint16_t> found(NextFake() % 4);
        for (uint16_t &p : found)
            p = uint16_t(NextFake() % CHECKPOINT_NAME_COUNT);
        uint8_t *out = reinterpret_cast<uint8_t *>(a[3]);
        if (g_side) {
            uint8_t *buf = static_cast<uint8_t *>(O_New(2 * found.size() + 2));
            if (!found.empty())
                std::memcpy(buf, found.data(), 2 * found.size());
            *reinterpret_cast<uint8_t **>(out + 4) = buf;
            *reinterpret_cast<uint8_t **>(out + 8) = buf + 2 * found.size();
            *reinterpret_cast<uint8_t **>(out + 0xC) = buf + 2 * found.size();
        } else {
            reinterpret_cast<std::vector<uint16_t> *>(out)->assign(found.begin(), found.end());
        }
        return 1u;
    };
    // The shadows: a bike's (made by CreateShadow, the same each time asked) for most, none for others.
    g_answers["C_GEffectsModule::CreateShadow"] = [](const std::vector<uint32_t> &) { return 0u; };
    g_answers["C_GEffectsModule::GetShadow"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *frame = reinterpret_cast<I3D_frame *>(a[1]);
        const std::string name = frame && frame->m_name ? frame->m_name : "";
        if (name != "bike0")
            return 0u;
        if (!g_shadow[g_side]) {
            g_shadow[g_side] = NewFrame(FRAME_VISUAL, "shadow");
            AsFrame(g_shadow[g_side])->m_name = W().frames.back().name.c_str();
        }
        return Ptr(g_shadow[g_side]);
    };
    // The ground under the front wheel: hit within the ray now and then (its point filled), else missed.
    g_answers["CollisionRayTest"] = [](const std::vector<uint32_t> &a) {
        const uint32_t r = NextFake() % 4;
        if (r == 0)
            return 0u;
        const S_vector &from = *reinterpret_cast<const S_vector *>(a[0]);
        const S_vector &dir = *reinterpret_cast<const S_vector *>(a[1]);
        static const float parts[] = {0.0f, 0.5f, 0.75f, 1.0f};
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[2]))
            *hit = S_vector(from.x + dir.x * parts[r], from.y + dir.y * parts[r], from.z + dir.z * parts[r]);
        return 1u;
    };
}

// ---- the compared state ----
std::string V3(const float *v) { return VectorText(Ptr(v), 3); }
std::string V4(const float *v) { return VectorText(Ptr(v), 4); }
std::string Fl(float f) { return FloatText(f); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::string FrameText(const I3D_frame *f)
{
    if (!f)
        return "none";
    return F("%s flags %08x at ", f->m_name ? f->m_name : "", f->m_frameFlags) + VectorText(Ptr(&f->m_localMatrix.m[3][0]), 3) +
           " rot " + VectorText(Ptr(&f->m_rot), 4) + " parent " + NameOf(Ptr(f->m_parent));
}
// The volume (S_CollisionVolume: the same layout on both sides; its inverse matrix the bike's own).
std::string VolumeText(const S_CollisionVolume &v, const void *inv)
{
    return F("volume flags %x type %x frame %s actor %s matrix %s inv %s radius ", v.flags, v.type, NameOf(Ptr(v.frame)).c_str(),
             NameOf(Ptr(v.actor)).c_str(), NameOf(Ptr(v.matrix)).c_str(), v.invMatrix == inv ? "own" : NameOf(Ptr(v.invMatrix)).c_str()) +
           Fl(v.radius) + " centre " + V4(&v.center.x) + " box " + V3(&v.bbox.min.x) + " " + V3(&v.bbox.max.x);
}
std::vector<std::string> BikeState(int i)
{
    std::vector<std::string> out;
    const uint8_t *b = g_bike[g_side][i];
    if (g_side) {
        std::string path;
        const uint32_t first = At<uint32_t>(b, 0x194 + 4), last = At<uint32_t>(b, 0x194 + 8);
        for (uint32_t a = first; a && a < last; a += 2)
            path += F(" %u", unsigned(*reinterpret_cast<const uint16_t *>(a)));
        out.push_back(F("bike%d started %d top ", i, int(b[0xD0])) + Fl(At<float>(b, 0x120)) + F(" u124 %d driving %d speed ", At<int>(b, 0x124), int(b[0x190])) +
                      Fl(At<float>(b, 0x1A4)) + " pedals " + Fl(At<float>(b, 0x1B0)) +
                      F(" model %s rider %s parts %s %s %s %s %s path", NameOf(At<uint32_t>(b, 0x1A8)).c_str(), NameOf(At<uint32_t>(b, 0x1AC)).c_str(),
                        NameOf(At<uint32_t>(b, 0x1B4)).c_str(), NameOf(At<uint32_t>(b, 0x1B8)).c_str(), NameOf(At<uint32_t>(b, 0x1BC)).c_str(),
                        NameOf(At<uint32_t>(b, 0x1C0)).c_str(), NameOf(At<uint32_t>(b, 0x1C4)).c_str()) +
                      path);
        out.push_back(F("  sound chapter %d flags %x attach %s source %s pos ", At<int>(b, 0xE0 + offsetof(S_SoundRequest, chapter)), At<uint32_t>(b, 0xE0 + offsetof(S_SoundRequest, flags)),
                        NameOf(At<uint32_t>(b, 0xE0 + offsetof(S_SoundRequest, attachTo))).c_str(),
                        NameOf(At<uint32_t>(b, 0xE0 + offsetof(S_SoundRequest, source))).c_str()) +
                      V4(FP(b, 0xE0 + offsetof(S_SoundRequest, pos))));
        out.push_back("  model " + FrameText(reinterpret_cast<const I3D_frame *>(At<uint32_t>(b, 0x1A8))));
        out.push_back("  inv " + VectorText(Ptr(b + 0x90), 16));
        out.push_back("  " + VolumeText(*reinterpret_cast<const S_CollisionVolume *>(b + 0x130), b + 0x90));
        return out;
    }
    const C_Bike *p = PortBike(i);
    std::string path;
    for (uint16_t point : p->m_path)
        path += F(" %u", unsigned(point));
    out.push_back(F("bike%d started %d top ", i, int(p->m_started)) + Fl(p->m_topSpeed) + F(" u124 %d driving %d speed ", p->m_unknown124, int(p->m_driving)) +
                  Fl(p->m_speed) + " pedals " + Fl(p->m_pedalPhase) +
                  F(" model %s rider %s parts %s %s %s %s %s path", NameOf(Ptr(p->m_riderModel)).c_str(), NameOf(Ptr(p->m_rider)).c_str(),
                    NameOf(Ptr(p->m_frontWheel)).c_str(), NameOf(Ptr(p->m_rearWheel)).c_str(), NameOf(Ptr(p->m_handlebars)).c_str(),
                    NameOf(Ptr(p->m_body)).c_str(), NameOf(Ptr(p->m_cranks)).c_str()) +
                  path);
    out.push_back(F("  sound chapter %d flags %x attach %s source %s pos ", p->m_sound.chapter, p->m_sound.flags, NameOf(Ptr(p->m_sound.attachTo)).c_str(),
                    NameOf(Ptr(p->m_sound.source)).c_str()) +
                  V4(&p->m_sound.pos.x));
    out.push_back("  model " + FrameText(p->m_riderModel));
    out.push_back("  inv " + VectorText(Ptr(&p->m_invMatrix), 16));
    out.push_back("  " + VolumeText(p->m_volume, &p->m_invMatrix));
    return out;
}
std::vector<std::string> BikesState()
{
    std::vector<std::string> out;
    for (int i = 0; i < g_bikeCount; ++i)
        for (const std::string &s : BikeState(i))
            out.push_back(s);
    for (int i = 0; i < g_bikeCount; ++i) {
        out.push_back(F("frame%d ", i) + FrameText(AsFrame(g_bikeFrame[g_side][i])));
        for (int k = 0; k < PARTS; ++k)
            if (g_bikePart[g_side][i][k])
                out.push_back(F("  part%d ", k) + FrameText(AsFrame(g_bikePart[g_side][i][k])));
    }
    for (int k = 0; k < RIDERS; ++k) {
        std::string human;
        if (uint8_t *h = g_riderHuman[g_side][k])
            human = " human at " + V4(g_side ? FP(h, 0x30) : &reinterpret_cast<C_Actor *>(h)->m_position.x);
        out.push_back(F("rider%d ", k) + FrameText(AsFrame(g_rider[g_side][k])) + human);
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
    template <class T> void EntryOf(uint16_t id, const T &v)
    {
        Begin(id);
        Raw(&v, sizeof v);
        End();
    }
};
enum { OP_READ, OP_COPY, OP_SET_FRAME, OP_START, OP_STOP, OP_RESTART, OP_DRIVE, OP_UPDATE, OP_SAVE_LOAD, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"ReadProperties", "CopyFrom", "SetFrame", "Start", "Stop", "Restart", "Drive", "Update", "SaveLoad"};
struct S_BkOp {
    int kind = 0;
    int bike = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_BkBike {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f, tilt = 0.0f;
    bool parts[PARTS] = {true, true, true, true, true};
    int odd = 0;                                        // (1..4: its z axis tiny along x, zero, tiny along z, vertical)
};
struct S_BkCase {
    std::vector<S_BkBike> bikes;
    std::vector<S_BkOp> ops;
    int riderKinds[RIDERS] = {};                        // (0 a human with a script, 1 a human without, 2 no actor)
    float pointAt[CHECKPOINT_NAME_COUNT][3] = {};
    uint8_t pointRadius[CHECKPOINT_NAME_COUNT] = {};
};
int g_bkCases, g_opCounts[OP_KINDS];
const int FRAME_TIMES[] = {0, 16, 100, 33, 500, 1000, 2000, 50, 1, 250, 5000, 300};
S_BkCase RandomBkCase()
{
    S_BkCase c;
    const int bikes = P(0.75) ? 1 : 2;
    for (int i = 0; i < bikes; ++i) {
        S_BkBike l;
        l.x = float(I(-20, 20)) * 0.5f;
        l.y = float(I(-4, 4)) * 0.5f;
        l.z = float(I(-20, 20)) * 0.5f;
        l.heading = float(I(0, 628)) * 0.01f;
        l.tilt = P(0.2) ? float(I(-20, 20)) * 0.01f : 0.0f;
        for (bool &part : l.parts)
            part = P(0.97);
        l.odd = P(0.1) ? I(1, 4) : 0;
        c.bikes.push_back(l);
    }
    for (int &k : c.riderKinds)
        k = I(0, 2);
    // (The road points about the first bike: near, along, far; radii from none to 10 m.)
    for (int k = 0; k < CHECKPOINT_NAME_COUNT; ++k) {
        c.pointAt[k][0] = c.bikes[0].x + float(I(-40, 40)) * 0.25f;
        c.pointAt[k][1] = c.bikes[0].y;
        c.pointAt[k][2] = c.bikes[0].z + float(I(-40, 40)) * 0.25f;
        static const uint8_t radii[] = {0, 16, 48, 160, 80, 4};
        c.pointRadius[k] = radii[I(0, 5)];
    }
    const int count = P(0.1) ? I(40, 120) : I(4, 40);
    for (int i = 0; i < count; ++i) {
        S_BkOp op;
        const int r = I(0, 99);
        op.kind = r < 3 ? OP_READ : r < 5 ? OP_COPY : r < 7 ? OP_SET_FRAME : r < 11 ? OP_START : r < 14 ? OP_STOP : r < 16 ? OP_RESTART
                : r < 30 ? OP_DRIVE : r < 90 ? OP_UPDATE : OP_SAVE_LOAD;
        op.bike = I(0, bikes - 1);
        op.a = I(0, 1000);
        op.b = I(0, 1000);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    // Started first, mostly.
    for (int i = bikes; i-- > 0;) {
        if (P(0.9)) {
            S_BkOp start;
            start.kind = OP_START;
            start.bike = i;
            start.c = I(0, 3);
            c.ops.insert(c.ops.begin(), start);
        }
    }
    return c;
}

using T_BkInt = void(TC *)(void *, int);
using T_BkChunk = bool(TC *)(void *, void *);
using T_BkVoid = void(TC *)(void *, void *);
std::string Hex(const std::string &bytes)
{
    std::string hex;
    for (unsigned char ch : bytes)
        hex += F("%02x", ch);
    return hex;
}
uint8_t *Field8(int i, uint32_t off) { return reinterpret_cast<uint8_t *>(At<uint32_t>(g_bike[1][i], off)); }
// (Not ridden without its seat frame or handlebars, nor rolling without the handlebars or the front wheel: both sides read
// through null; the bikes in the game have all of them.)
bool Updatable(int i)
{
    const bool o = g_side != 0;
    const C_Bike *p = PortBike(i);
    const bool driving = o ? g_bike[1][i][0x190] != 0 : p->m_driving;
    const bool pathEmpty = o ? At<uint32_t>(g_bike[1][i], 0x198) == At<uint32_t>(g_bike[1][i], 0x19C) : p->m_path.empty();
    const bool body = o ? Field8(i, 0x1C0) != nullptr : p->m_body != nullptr;
    const bool bars = o ? Field8(i, 0x1BC) != nullptr : p->m_handlebars != nullptr;
    const bool wheel = o ? Field8(i, 0x1B4) != nullptr : p->m_frontWheel != nullptr;
    const float speed = o ? At<float>(g_bike[1][i], 0x1A4) : p->m_speed;
    if (driving && !pathEmpty && (!body || !bars))
        return false;
    if ((speed != 0.0f || (driving && !pathEmpty)) && (!bars || !wheel))
        return false;
    return true;
}
std::string RunBkOp(const S_BkOp &op, int index)
{
    const bool o = g_side != 0;
    const int li = op.bike < g_bikeCount ? op.bike : 0;
    uint8_t *b = g_bike[g_side][li];
    C_Bike *p = PortBike(li);
    switch (op.kind) {
    case OP_READ: {
        // The top speed and the unknown; now and then cut short.
        static const float speeds[] = {7.0f, 15.0f, 0.0f, 30.5f};
        S_Writer w;
        w.Begin(0xAE31);
        const float speed = speeds[op.a % 4];
        const int unknown = op.b % 7 - 2;
        w.Raw(&speed, 4);
        w.Raw(&unknown, 4);
        w.End();
        std::string bytes = w.out;
        if (op.d % 8 == 0)
            bytes.resize(size_t(6 + 4 * (op.d / 8 % 2)));
        const std::string name = F("bk_props%d.bin", index);
        g_overrides[Lower(name)] = bytes;
        C_chunk chunk;
        chunk.Open(name.c_str(), false);
        uint16_t id;
        chunk.Descend(id);
        const bool r = o ? OriginalAt<T_BkChunk>(O_BK_READ)(b, &chunk) : p->ReadProperties(chunk);
        chunk.Close();
        return F("ReadProperties %d -> %d ", li, int(r)) + Hex(bytes);
    }
    case OP_COPY: {
        const int from = (li + (op.a & 1)) % g_bikeCount;
        uint8_t *src = g_bike[g_side][from];
        if (o)
            OriginalAt<T_BkVoid>(O_BK_COPY)(b, src);
        else
            p->CopyFrom(reinterpret_cast<C_Actor *>(src));
        return F("CopyFrom %d %d", li, from);
    }
    case OP_SET_FRAME: {
        uint8_t *frame = op.a % 4 ? g_bikeFrame[g_side][li] : nullptr;
        const bool r = o ? OriginalAt<bool(TC *)(void *, void *)>(O_BK_SET_FRAME)(b, frame) : p->SetFrame(AsFrame(frame));
        return F("SetFrame %d %s -> %d", li, NameOf(Ptr(frame)).c_str(), int(r));
    }
    case OP_START: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_BkInt>(O_BK_START)(b, server);
        else
            p->StartMission(server);
        return F("StartMission %d %d", li, int(server));
    }
    case OP_STOP: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_BkInt>(O_BK_STOP)(b, server);
        else
            p->StopMission(server);
        return F("StopMission %d %d", li, int(server));
    }
    case OP_RESTART:
        if (o)
            OriginalAt<T_BkInt>(O_BK_RESTART)(b, op.c & 1);
        else
            p->RestartMission(op.c & 1);
        return F("RestartMission %d", li);
    case OP_DRIVE: {
        // To a road point (one of five) or off; a rider (one of three) or none.
        const int point = op.a % 4 == 0 ? int(CHECKPOINT_NONE) : op.a % CHECKPOINT_NAME_COUNT;
        uint8_t *rider = op.b % 5 == 0 ? nullptr : g_rider[g_side][op.b % RIDERS];
        const float speed = 3.5f;
        if (o)
            OriginalAt<void(TC *)(void *, int, float, void *)>(O_BK_DRIVE)(b, point, speed, rider);
        else
            p->Drive(point, speed, AsFrame(rider));
        return F("Drive %d %d %s", li, point, NameOf(Ptr(rider)).c_str());
    }
    case OP_UPDATE: {
        if (!Updatable(li))
            return "Update (a part missing)";
        const int t = FRAME_TIMES[op.a % 12];
        if (o)
            OriginalAt<T_BkInt>(O_BK_UPDATE)(b, t);
        else
            p->Update(t);
        return F("Update %d %d", li, t);
    }
    default: {
        // (Not a bike ridden without a rider, driven by a script with none: the original saves its name through null.)
        const bool driving = o ? b[0x190] != 0 : p->m_driving;
        const bool rider = o ? At<uint32_t>(b, 0x1AC) != 0 : p->m_rider != nullptr;
        if (driving && !rider)
            return "SaveGame (ridden without a rider)";
        const std::string file = F("bk_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_BkChunk>(O_BK_SAVE)(b, &save) : p->SaveGame(save);
        save.Ascend();
        save.Begin(0x3000);
        save.Write(int(7));
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        // (Loaded back now and then (into this one or the other, stopped first: a ridden bike's load mounts its rider
        // again): its file, or a made one: a rider by name (one, an unknown name), each entry now and then, a short path,
        // a foreign entry.)
        bool rl = false;
        const int load = op.c % 4;
        const int ii = (li + (op.d & 1)) % g_bikeCount;
        uint8_t *into = g_bike[g_side][ii];
        unsigned next = 0;
        int depth = 0;
        if (load) {
            std::string name = file;
            if (load >= 2) {
                S_Writer w;
                w.Begin(0x2000);
                w.Begin(uint16_t(op.a % 7 == 0 ? 1000 : 2000));
                if (op.a & 1) {
                    w.Begin(5000);
                    const std::string riderName = op.b % 4 == 0 ? std::string("nobody") : F("rider%d", op.b % RIDERS);
                    w.Raw(riderName.c_str(), riderName.size() + 1);
                    w.End();
                }
                if (op.a & 2) {
                    w.Begin(6000);
                    const int count = op.b % 4;
                    w.Raw(&count, 4);
                    // (Short: one point fewer than counted, the last read repeated. Not without any: the original
                    // pushes a stack word, the port 0.)
                    const int written = op.d % 5 == 0 && count >= 2 ? count - 1 : count;
                    for (int k = 0; k < written; ++k) {
                        const uint16_t point = uint16_t((op.d + k) % CHECKPOINT_NAME_COUNT);
                        w.Raw(&point, 2);
                    }
                    w.End();
                }
                if (op.a & 4)
                    w.EntryOf(3000, 1.25f);
                if (op.a & 8)
                    w.EntryOf(4000, 0.004f);
                if (op.b % 3 == 0) {
                    w.Begin(0x0009);
                    w.End();
                }
                w.End();
                w.End();
                w.EntryOf(0x3000, int(7));
                name = F("bk_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            // (Stopped and started first: the load is a fresh mission's.)
            if (o) {
                OriginalAt<T_BkInt>(O_BK_STOP)(into, 1);
                OriginalAt<T_BkInt>(O_BK_START)(into, 1);
            } else {
                reinterpret_cast<C_Bike *>(into)->StopMission(true);
                reinterpret_cast<C_Bike *>(into)->StartMission(true);
            }
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_BkChunk>(O_BK_LOAD)(into, &chunk) : reinterpret_cast<C_Bike *>(into)->LoadGame(chunk);
            depth = chunk.m_depth;
            chunk.Ascend();
            uint16_t after = 0;
            chunk.Descend(after);
            next = after;
            chunk.Close();
        }
        return F("SaveGame %d -> %d LoadGame %d into %d -> %d depth %d next %04x bytes ", li, int(rs), load, ii, int(rl), depth, next) +
               Hex(bytes);
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
            std::printf("bk_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_bkShow;
void RunBkCase(const S_BkCase &c)
{
    g_caseStart = GetTickCount();
    ++g_bkCases;
    g_holdFrees = true;
    g_current = F("case %d", g_bkCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_bikeCount = int(c.bikes.size());
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_texts[side].clear();
        g_shadow[side] = nullptr;
        BuildWorld(side, worldSeed);
        uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
        // The road points.
        for (int k = 0; k < CHECKPOINT_NAME_COUNT; ++k) {
            uint8_t *point = W().points[1] + 0x24 * k;
            *reinterpret_cast<S_vector *>(point) = S_vector(c.pointAt[k][0], c.pointAt[k][1], c.pointAt[k][2]);
            point[0x13] = c.pointRadius[k];
        }
        for (int i = 0; i < g_bikeCount; ++i) {
            const S_BkBike &lc = c.bikes[size_t(i)];
            uint8_t *frame = Child(sector, FRAME_MODEL, F("bike%d", i), nullptr);
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
            if (lc.odd) {
                static const float rows[4][3] = {{1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1e-5f}, {0.0f, 1.0f, 0.0f}};
                for (int k = 0; k < 3; ++k)
                    m.m[2][k] = rows[lc.odd - 1][k];
            }
            m.m[3][0] = lc.x;
            m.m[3][1] = lc.y;
            m.m[3][2] = lc.z;
            static const float partAt[PARTS][3] = {{0.0f, 0.35f, 0.6f}, {0.0f, 0.35f, -0.6f}, {0.0f, 1.0f, 0.5f}, {0.0f, 0.3f, 0.0f},
                                                   {0.0f, 0.8f, -0.1f}};
            for (int k = 0; k < PARTS; ++k) {
                uint8_t *part = nullptr;
                if (lc.parts[k]) {
                    part = Child(frame, k == 4 ? FRAME_DUMMY : FRAME_VISUAL, PART_NAMES[k], frame);
                    I3D_frame *f = AsFrame(part);
                    f->m_localMatrix.m[3][0] = partAt[k][0];
                    f->m_localMatrix.m[3][1] = partAt[k][1];
                    f->m_localMatrix.m[3][2] = partAt[k][2];
                    // (The handlebars turned a little.)
                    if (k == 2) {
                        f->m_localMatrix.m[0][0] = 0.98f;
                        f->m_localMatrix.m[0][2] = -0.2f;
                        f->m_localMatrix.m[2][0] = 0.2f;
                        f->m_localMatrix.m[2][2] = 0.98f;
                    }
                    f->m_rot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
                }
                g_bikePart[side][i][k] = part;
            }
            // (Its bound: a centre off the origin and a radius, the box's sphere after a move.)
            AsFrame(frame)->m_bound.center = S_vector(0.1f, 0.5f, 0.0f);
            AsFrame(frame)->m_bound.radius = 1.25f;
            g_bikeFrame[side][i] = frame;
        }
        // The riders' frames: humans' (with a script or none), or a frame without an actor.
        uint8_t *script = Block(0x400);
        Name(script, "script");
        for (int k = 0; k < RIDERS; ++k) {
            uint8_t *rider = Child(sector, FRAME_MODEL, F("rider%d", k), nullptr);
            AsFrame(rider)->m_localMatrix.m[3][0] = float(k) * 2.0f;
            g_riderHuman[side][k] = nullptr;
            if (c.riderKinds[k] < 2) {
                // (The k-th human, or the last.)
                uint8_t *human = nullptr;
                int left = k;
                for (const auto &a : W().actors)
                    if (a.type == ACTOR_HUMAN) {
                        human = a.bytes;
                        if (left-- == 0)
                            break;
                    }
                S_FrameData *data = reinterpret_cast<S_FrameData *>(Block(0x20));
                data->actor = reinterpret_cast<C_Actor *>(human);
                data->script = c.riderKinds[k] == 0 ? reinterpret_cast<C_Script *>(script) : nullptr;
                AsFrame(rider)->m_data = data;
                g_riderHuman[side][k] = human;
            }
            g_rider[side][k] = rider;
        }
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        for (int i = 0; i < g_bikeCount; ++i) {
            uint8_t *b;
            if (side) {
                b = Block(0x1D0 + 16);
                OriginalAt<T_Void>(O_BK_CTOR)(b);
                Mut<uint8_t *>(b, 0x20) = g_bikeFrame[1][i];
            } else {
                b = Block(sizeof(C_Bike) + 16);
                new (b) C_Bike();
                reinterpret_cast<C_Bike *>(b)->m_frame = AsFrame(g_bikeFrame[0][i]);
            }
            g_bike[side][i] = b;
            Name(b, F("bike %d", i));
        }
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_log[side].clear();
        for (const std::string &s : BikesState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_BK_TRACE") ? std::atoi(std::getenv("HD2_BK_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_bkCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_KIND_NAMES[c.ops[i].kind],
                            c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunBkOp(c.ops[i], int(i)));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            for (const std::string &s : BikesState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        for (int i = 0; i < g_bikeCount; ++i) {
            uint8_t *b = g_bike[side][i];
            if (side) {
                OriginalAt<T_BkInt>(O_BK_STOP)(b, 1);
                OriginalAt<T_Void>(O_BK_DTOR)(b);
            } else {
                PortBike(i)->StopMission(true);
                PortBike(i)->~C_Bike();
            }
        }
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(O_BK_CREATE)(OriginalAt<void *>(O_BK_CREATOR));
            Name(made, "created bike");
            out.push_back(F("created type %d", At<int>(made, 0x18)));
            OriginalAt<void(TC *)(void *, void *)>(O_BK_DESTROY)(OriginalAt<void *>(O_BK_CREATOR), made);
        } else {
            C_ActorCreator *creator = g_actorCreators[ACTOR_BIKE];
            C_Actor *made = creator->Create();
            Name(made, "created bike");
            out.push_back(F("created type %d", int(made->GetType())));
            creator->Destroy(made);
        }
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        for (int i = 0; i < MAX_BIKES; ++i)
            g_bike[side][i] = nullptr;
    }
    static const int only = std::getenv("HD2_BK_ONLY") ? std::atoi(std::getenv("HD2_BK_ONLY")) : 0;
    if (g_bkShow && (!only || only == g_bkCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_bkShow == 2)
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

void SetUpBike()
{
    SetUpCmd();
    SetUpBikeAnswers();
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
        {0x00416cf0, reinterpret_cast<const void *>(&B_Advance)},
        {0x007e66c0, reinterpret_cast<const void *>(&B_CIasin)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    {
        auto advance = &C_Script::Advance;
        void *at;
        std::memcpy(&at, &advance, sizeof at);
        PatchPort(at, reinterpret_cast<const void *>(&B_Advance));
    }
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("bk_diff: main");
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
        std::printf("bk_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_BK_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_BK_SHOW"))
        g_bkShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_BK_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_BK_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpBike();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunBkCase(RandomBkCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("bk_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_bkCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
