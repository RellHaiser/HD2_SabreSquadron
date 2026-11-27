// Fish part 1: the schools of fish (game/fish.cpp, the original's 0x005db780..0x005de650) against the original
// executable's. cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the schools' calls go
// through fs_calls.s / fs_calls.inc (gen_cmd_calls.py fs). The LS3DF math (the turns, the inverse, the vector by matrix
// products) is LS3DF.dll's on both sides; the model cache is real on both sides (game/resource_cache.cpp; the original's
// inline Get on its own, built here), its models' Open logged and answered ("ryba" with its "ryba" frame or without it);
// RandomFloat is the original's on both sides (over the test's rand).
//
// Each case builds the same world on each side (cmd_diff's) and one or two schools: frames under the primary sector
// (turned, tilted, now and then a vertical or tiny z axis), the camera near or far, makes the schools on each side (the
// original's by its constructor 0x005dbd80 in a block of 0xE0, the port's by its own) and runs the same random sequence
// of operations: properties read (the top speed, the radius, the count, cut short), CopyFrom, SetFrame, starts (the
// fish made: models with and without the frame, visuals refused), stops, restarts, ticks (speeding up, slowing down,
// the pause and the turn, the slides) and updates (the sway, the fade near the camera), the camera moved, the saved
// game's calls (the file just written, made ones). After each operation the answer, every school's state (every field,
// its fish), the frames and every call made into the game and the engine are compared; the saves' bytes too. The
// creator makes and destroys one.
// MinGW; build_fs.bat; runs fs_diff.exe ("fs_diff: N checks, 0 failures"). Env: HD2_FS_CASES (default 1,500;
// HD2_FS_QUICK: 300), HD2_FS_SEED, HD2_FS_SHOW (1: the port's lines; 2: both), HD2_FS_ONLY (the case shown),
// HD2_FS_TRACE (a case's operations as they start).
#define HARNESS_NAME "fs_diff"
#define CMD_CALLS_INC "fs_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/fish.h"
#include "game/resource_cache.h"
#undef protected
#undef private

// ---- the port's side of what the schools call and the test provides ----
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
S_matrix __stdcall RotateYMatrix(float angle)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_matrix *, float)>("?RotateYMatrix@@YG?AUS_matrix@@M@Z");
    S_matrix m;
    f(&m, angle);
    return m;
}
S_matrix __stdcall RotateZMatrix(float angle)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_matrix *, float)>("?RotateZMatrix@@YG?AUS_matrix@@M@Z");
    S_matrix m;
    f(&m, angle);
    return m;
}
S_vector __stdcall S_vector::RotateByMatrix(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_vector *, float *, const S_matrix &)>(
        "?RotateByMatrix@S_vector@@QBG?AU1@ABUS_matrix@@@Z");
    alignas(16) float r[4] = {};
    f(this, r, m);
    return S_vector(r[0], r[1], r[2]);
}
// RandomFloat: the original's on both sides (its rand the test's).
float RandomFloat(float max) { return OriginalAt<float(__stdcall *)(float)>(0x00475190)(max); }
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
// The actors' creators (the schools' registers itself before main: a table of the test's).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the school's vtable holds but the test never reaches.
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

// ---- the schools ----
constexpr int MAX_SCHOOLS = 2;
uint8_t *g_school[2][MAX_SCHOOLS];
uint8_t *g_schoolFrame[2][MAX_SCHOOLS];
int g_schoolCount;
uint8_t *g_camera[2];                                   // (the world's active camera, put back after "none")
C_Fish *PortSchool(int i) { return reinterpret_cast<C_Fish *>(g_school[0][i]); }
constexpr uint32_t O_FS_CTOR = 0x005dbd80, O_FS_DTOR = 0x005dbe20, O_FS_UPDATE = 0x005dcb10, O_FS_TICK = 0x005dbec0,
                   O_FS_SET_FRAME = 0x005dbe60, O_FS_READ = 0x005de300, O_FS_COPY = 0x005dbe80, O_FS_START = 0x005dd900,
                   O_FS_STOP = 0x005de260, O_FS_RESTART = 0x005db870, O_FS_SAVE = 0x005db890, O_FS_LOAD = 0x005dba50;
constexpr uint32_t O_FS_CREATOR = 0x008ac958, O_FS_CREATE = 0x005db7e0, O_FS_DESTROY = 0x005db850;
constexpr uint32_t O_MODEL_CACHE = 0x008aeda0;

// The case's answers: whether the model has its "ryba" frame, whether a visual is refused (from the n-th on).
bool g_rybaFrame = true;
int g_visualsLeft = 1000;

void SetUpFishAnswers()
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
    // The models: "ryba" opened (with its "ryba" frame, or without it, as the case says); others not found.
    g_answers["I3D_model::Open"] = [](const std::vector<uint32_t> &a) {
        const std::string file = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        if (file.find("ryba") == std::string::npos)
            return uint32_t(I3DERR_NOFILE);
        uint8_t *model = reinterpret_cast<uint8_t *>(a[0]);
        uint8_t *frame = Child(model, FRAME_VISUAL, g_rybaFrame ? "ryba" : "other", model);
        W().frames.back().visualType = 0x4f424a5f;
        (void)frame;
        return 0u;
    };
    g_answers["I3D_model::DuplicateModel"] = [](const std::vector<uint32_t> &a) {
        std::function<void(I3D_frame *, I3D_frame *)> copy = [&](I3D_frame *from, I3D_frame *to) {
            for (I3D_frame *c = from->m_firstChild; c; c = c->m_nextSibling) {
                uint8_t *b = NewFrame(c->m_type, c->m_name ? c->m_name : "");
                AsFrame(b)->m_name = W().frames.back().name.c_str();
                LinkUnder(b, reinterpret_cast<uint8_t *>(to));
                copy(c, AsFrame(b));
            }
        };
        copy(reinterpret_cast<I3D_frame *>(a[1]), reinterpret_cast<I3D_frame *>(a[0]));
        return 0u;
    };
    // The visuals made: a visual frame, or none once the case's count is used up.
    g_answers["I3D_driver::CreateVisual"] = [](const std::vector<uint32_t> &) {
        if (g_visualsLeft-- <= 0)
            return 0u;
        uint8_t *b = NewFrame(FRAME_VISUAL, F("fish%d", W().created++));
        AsFrame(b)->m_name = W().frames.back().name.c_str();
        return Ptr(b);
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
    // The slides: none mostly; else stopped part of the way and pushed aside, a normal facing back.
    g_answers["CollisionSlideSphere"] = [](const std::vector<uint32_t> &a) {
        const uint32_t r = NextFake() % 7;
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[0]);
        const S_vector &move = *reinterpret_cast<const S_vector *>(a[1]);
        S_vector &end = *reinterpret_cast<S_vector *>(a[3]);
        if (r < 4) {
            end = S_vector(pos.x + move.x, pos.y + move.y, pos.z + move.z);
            return 0u;
        }
        static const float parts[] = {0.0f, 0.5f, 1.0f};
        const float f = parts[r - 4];
        end = S_vector(pos.x + move.x * f + 0.125f, pos.y + move.y * f, pos.z + move.z * f - 0.0625f);
        if (S_vector *normal = reinterpret_cast<S_vector *>(a[4]))
            *normal = S_vector(-0.6f, 0.0f, 0.8f);
        return 1u;
    };
}

// ---- the compared state ----
std::string V3(const float *v) { return VectorText(Ptr(v), 3); }
std::string V4(const float *v) { return VectorText(Ptr(v), 4); }
std::string Fl(float f) { return FloatText(f); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::vector<uint32_t> OVec(const uint8_t *p, uint32_t off)
{
    std::vector<uint32_t> out;
    const uint32_t first = At<uint32_t>(p, off + 4), last = At<uint32_t>(p, off + 8);
    for (uint32_t a = first; a && a < last; a += 4)
        out.push_back(*reinterpret_cast<const uint32_t *>(a));
    return out;
}
std::string FrameText(const I3D_frame *f)
{
    if (!f)
        return "none";
    return F("%s flags %08x at ", f->m_name ? f->m_name : "", f->m_frameFlags) + VectorText(Ptr(&f->m_localMatrix.m[3][0]), 3) +
           " rot " + VectorText(Ptr(&f->m_rot), 4) + " scale " + V3(&f->m_scale.x) + " parent " + NameOf(Ptr(f->m_parent));
}
// A fish (0x30 bytes, the same layout on both sides; its place's w a stale local in the original: left out).
std::string FishText(uint32_t p)
{
    if (!p)
        return "null";
    const uint8_t *f = reinterpret_cast<const uint8_t *>(p);
    return "pos " + V3(FP(f, 0)) + " target " + V3(FP(f, 0x10)) + " phase " + Fl(At<float>(f, 0x24)) + " rate " +
           Fl(At<float>(f, 0x28)) + " frame " + FrameText(reinterpret_cast<const I3D_frame *>(At<uint32_t>(f, 0x20)));
}
std::vector<std::string> SchoolState(int i)
{
    std::vector<std::string> out;
    const uint8_t *b = g_school[g_side][i];
    std::vector<uint32_t> fish;
    if (g_side) {
        fish = OVec(b, 0xD0);
    } else {
        for (const C_Fish::S_FishMember *m : PortSchool(i)->m_fish)
            fish.push_back(Ptr(m));
    }
    if (g_side) {
        out.push_back(F("school%d dir ", i) + V3(FP(b, 0x90)) + " velocity " + V3(FP(b, 0xA0)) +
                      F(" state %d pause %d time %d top ", At<int>(b, 0xB0), At<int>(b, 0xB4), At<int>(b, 0xB8)) + Fl(At<float>(b, 0xBC)) +
                      " radius " + Fl(At<float>(b, 0xC0)) + F(" count %d started %d speed ", At<int>(b, 0xC4), int(b[0xC8])) +
                      Fl(At<float>(b, 0xCC)) + F(" fish %d", int(fish.size())));
    } else {
        const C_Fish *p = PortSchool(i);
        out.push_back(F("school%d dir ", i) + V3(&p->m_dir.x) + " velocity " + V3(&p->m_velocity.x) +
                      F(" state %d pause %d time %d top ", p->m_state, p->m_pause, p->m_time) + Fl(p->m_topSpeed) + " radius " +
                      Fl(p->m_radius) + F(" count %d started %d speed ", p->m_count, int(p->m_started)) + Fl(p->m_speed) +
                      F(" fish %d", int(fish.size())));
    }
    for (size_t k = 0; k < fish.size(); ++k)
        out.push_back(F("  fish%d ", int(k)) + FishText(fish[k]));
    return out;
}
std::vector<std::string> SchoolsState()
{
    std::vector<std::string> out;
    for (int i = 0; i < g_schoolCount; ++i)
        for (const std::string &s : SchoolState(i))
            out.push_back(s);
    for (int i = 0; i < g_schoolCount; ++i)
        out.push_back(F("frame%d ", i) + FrameText(AsFrame(g_schoolFrame[g_side][i])));
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
enum { OP_READ, OP_COPY, OP_SET_FRAME, OP_START, OP_STOP, OP_RESTART, OP_TICK, OP_UPDATE, OP_CAMERA, OP_SAVE_LOAD, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"ReadProperties", "CopyFrom", "SetFrame", "Start", "Stop", "Restart", "Tick", "Update", "Camera",
                                     "SaveLoad"};
struct S_FsOp {
    int kind = 0;
    int school = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_FsSchool {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f, tilt = 0.0f;
    int odd = 0;                                        // (1..3: its z axis vertical, tiny, zero)
};
struct S_FsCase {
    std::vector<S_FsSchool> schools;
    std::vector<S_FsOp> ops;
    bool rybaFrame = true;
    int visuals = 1000;
};
int g_fsCases, g_opCounts[OP_KINDS];
const int FRAME_TIMES[] = {0, 16, 100, 33, 500, 1000, 2000, 3500, 1, 250, 5000, 50};
S_FsCase RandomFsCase()
{
    S_FsCase c;
    const int schools = P(0.75) ? 1 : 2;
    for (int i = 0; i < schools; ++i) {
        S_FsSchool l;
        l.x = float(I(-20, 20)) * 0.5f;
        l.y = float(I(-10, 4)) * 0.5f;
        l.z = float(I(-20, 20)) * 0.5f;
        l.heading = float(I(0, 628)) * 0.01f;
        l.tilt = P(0.3) ? float(I(-40, 40)) * 0.01f : 0.0f;
        l.odd = P(0.1) ? I(1, 3) : 0;
        c.schools.push_back(l);
    }
    // (Not a model without its frame: the original calls through null, the port's fix makes no fish.)
    c.rybaFrame = true;
    c.visuals = P(0.1) ? I(0, 6) : 1000;
    const int count = P(0.1) ? I(40, 120) : I(4, 40);
    for (int i = 0; i < count; ++i) {
        S_FsOp op;
        const int r = I(0, 99);
        op.kind = r < 5 ? OP_READ : r < 7 ? OP_COPY : r < 9 ? OP_SET_FRAME : r < 14 ? OP_START : r < 18 ? OP_STOP : r < 20 ? OP_RESTART
                : r < 50 ? OP_TICK : r < 80 ? OP_UPDATE : r < 90 ? OP_CAMERA : OP_SAVE_LOAD;
        op.school = I(0, schools - 1);
        op.a = I(0, 1000);
        op.b = I(0, 1000);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    // Properties read first, mostly; then started.
    for (int i = schools; i-- > 0;) {
        if (P(0.9)) {
            S_FsOp start;
            start.kind = OP_START;
            start.school = i;
            start.c = I(0, 3);
            c.ops.insert(c.ops.begin(), start);
        }
        if (P(0.85)) {
            S_FsOp read;
            read.kind = OP_READ;
            read.school = i;
            read.a = I(0, 1000);
            read.b = I(0, 1000);
            read.c = I(0, 1000);
            read.d = I(0, 1000);
            c.ops.insert(c.ops.begin(), read);
        }
    }
    return c;
}

using T_FsInt = void(TC *)(void *, int);
using T_FsChunk = bool(TC *)(void *, void *);
using T_FsVoid = void(TC *)(void *, void *);
std::string Hex(const std::string &bytes)
{
    std::string hex;
    for (unsigned char ch : bytes)
        hex += F("%02x", ch);
    return hex;
}
bool SchoolHasFrame(int i)
{
    return g_side ? At<uint32_t>(g_school[1][i], 0x20) != 0 : PortSchool(i)->m_frame != nullptr;
}
uint8_t *Camera() { return *reinterpret_cast<uint8_t **>(W().scene + 0x15C); }
std::string RunFsOp(const S_FsOp &op, int index)
{
    const bool o = g_side != 0;
    const int li = op.school < g_schoolCount ? op.school : 0;
    uint8_t *b = g_school[g_side][li];
    C_Fish *p = PortSchool(li);
    switch (op.kind) {
    case OP_READ: {
        // The top speed, the radius, the count; now and then cut short.
        static const float speeds[] = {1.0f, 0.5f, 3.0f, 0.05f, 0.0f};
        static const float radii[] = {2.0f, 0.5f, 6.0f, 1.0f, 0.0f};
        static const int counts[] = {10, 3, 1, 0, 25, 7};
        S_Writer w;
        w.Begin(0xAE30);
        const float speed = speeds[op.a % 5], radius = radii[op.b % 5];
        const int count = counts[op.c % 6];
        w.Raw(&speed, 4);
        w.Raw(&radius, 4);
        w.Raw(&count, 4);
        w.End();
        std::string bytes = w.out;
        if (op.d % 8 == 0)
            bytes.resize(size_t(6 + 4 * (op.d / 8 % 3)));
        const std::string name = F("fs_props%d.bin", index);
        g_overrides[Lower(name)] = bytes;
        C_chunk chunk;
        chunk.Open(name.c_str(), false);
        uint16_t id;
        chunk.Descend(id);
        const bool r = o ? OriginalAt<T_FsChunk>(O_FS_READ)(b, &chunk) : p->ReadProperties(chunk);
        chunk.Close();
        return F("ReadProperties %d -> %d ", li, int(r)) + Hex(bytes);
    }
    case OP_COPY: {
        const int from = (li + (op.a & 1)) % g_schoolCount;
        uint8_t *src = op.b % 6 == 0 ? nullptr : g_school[g_side][from];
        if (o)
            OriginalAt<T_FsVoid>(O_FS_COPY)(b, src);
        else
            p->CopyFrom(reinterpret_cast<C_Actor *>(src));
        return F("CopyFrom %d %s", li, NameOf(Ptr(src)).c_str());
    }
    case OP_SET_FRAME: {
        // Its frame, or none (the base's SetFrame logged).
        uint8_t *frame = op.a % 4 ? g_schoolFrame[g_side][li] : nullptr;
        const bool r = o ? OriginalAt<bool(TC *)(void *, void *)>(O_FS_SET_FRAME)(b, frame) : p->SetFrame(AsFrame(frame));
        return F("SetFrame %d %s -> %d", li, NameOf(Ptr(frame)).c_str(), int(r));
    }
    case OP_START: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_FsInt>(O_FS_START)(b, server);
        else
            p->StartMission(server);
        return F("StartMission %d %d", li, int(server));
    }
    case OP_STOP: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_FsInt>(O_FS_STOP)(b, server);
        else
            p->StopMission(server);
        return F("StopMission %d %d", li, int(server));
    }
    case OP_RESTART:
        if (o)
            OriginalAt<T_FsInt>(O_FS_RESTART)(b, op.c & 1);
        else
            p->RestartMission(op.c & 1);
        return F("RestartMission %d", li);
    case OP_TICK: {
        const int t = FRAME_TIMES[op.a % 12];
        if (o)
            OriginalAt<T_FsInt>(O_FS_TICK)(b, t);
        else
            p->Tick(t);
        return F("Tick %d %d", li, t);
    }
    case OP_UPDATE: {
        const int t = FRAME_TIMES[op.a % 12];
        if (o)
            OriginalAt<T_FsInt>(O_FS_UPDATE)(b, t);
        else
            p->Update(t);
        return F("Update %d %d", li, t);
    }
    case OP_CAMERA: {
        // The camera near the school (within a metre of a fish now and then), at the edge of its range, far.
        static const float offsets[][3] = {{0.0f, 0.0f, 0.0f}, {0.3f, 0.2f, 0.1f}, {5.0f, 0.0f, 0.0f}, {0.0f, 16.0f, 0.0f},
                                           {0.0f, 0.0f, 17.5f}, {40.0f, 0.0f, 0.0f}, {1.0f, -0.5f, 0.5f}, {12.0f, 9.0f, 0.0f}};
        // (Now and then 1.1 m from the first fish, between the fade's square and 1.5; now and then none at all.)
        const int kind = op.a % 10;
        uint8_t **active = reinterpret_cast<uint8_t **>(W().scene + 0x15C);
        if (kind == 9) {
            *active = nullptr;
            return "Camera none";
        }
        *active = g_camera[g_side];
        S_vector at = AsFrame(g_schoolFrame[g_side][li])->GetWorldPos();
        float off[3] = {offsets[kind % 8][0], offsets[kind % 8][1], offsets[kind % 8][2]};
        if (kind == 8) {
            const uint8_t *b = g_school[g_side][li];
            uint32_t first = 0;
            if (o) {
                if (At<uint32_t>(b, 0xD4) != At<uint32_t>(b, 0xD8))
                    first = *reinterpret_cast<const uint32_t *>(At<uint32_t>(b, 0xD4));
            } else if (!PortSchool(li)->m_fish.empty()) {
                first = Ptr(PortSchool(li)->m_fish[0]);
            }
            if (first)
                at = AsFrame(reinterpret_cast<uint8_t *>(At<uint32_t>(reinterpret_cast<const uint8_t *>(first), 0x20)))->GetWorldPos();
            off[0] = 1.1f;
            off[1] = off[2] = 0.0f;
        }
        I3D_frame *camera = AsFrame(Camera());
        camera->m_localMatrix.m[3][0] = at.x + off[0];
        camera->m_localMatrix.m[3][1] = at.y + off[1];
        camera->m_localMatrix.m[3][2] = at.z + off[2];
        camera->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
        return F("Camera %d", kind);
    }
    default: {
        const std::string file = F("fs_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_FsChunk>(O_FS_SAVE)(b, &save) : p->SaveGame(save);
        save.Ascend();
        save.Begin(0x3000);
        save.Write(int(7));
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        // (Loaded back now and then (into this one or the other): its file, or a made one: each entry now and then, other
        // states, a foreign entry (entered, not left), a short speed.)
        bool rl = false;
        const int load = op.c % 4;
        uint8_t *into = g_school[g_side][(li + (op.d & 1)) % g_schoolCount];
        unsigned next = 0;
        int depth = 0;
        if (load) {
            std::string name = file;
            if (load >= 2) {
                S_Writer w;
                w.Begin(0x2000);
                w.Begin(9999);
                const float dir[3] = {0.6f, -0.2f, 0.8f}, velocity[3] = {0.5f, 0.0f, -1.5f};
                if (op.a & 1) {
                    w.Begin(1000);
                    w.Raw(dir, 12);
                    w.End();
                }
                if (op.a & 2)
                    w.EntryOf(2000, int(op.b % 4));
                if (op.a & 4)
                    w.EntryOf(3000, int(op.b % 3 == 0 ? -5 : 1500));
                if (op.a & 8)
                    w.EntryOf(4000, int(op.d % 5000));
                if (op.a & 16) {
                    // (A short one followed by a whole one: the short one's speed, the header's bits, is a
                    // denormal whose steps the original works out with the FPU's wider exponent.)
                    if (op.b % 5 == 0) {
                        w.Begin(5000);
                        w.Raw("\x01\x02", 2);
                        w.End();
                    }
                    w.EntryOf(5000, 0.75f);
                }
                if (op.a & 32) {
                    w.Begin(6000);
                    w.Raw(velocity, 12);
                    w.End();
                }
                if (load == 3 && op.b % 3 == 0) {
                    w.Begin(0x0009);
                    w.End();
                }
                w.End();
                w.End();
                // (An entry after: where the load leaves the reader.)
                w.EntryOf(0x3000, int(7));
                name = F("fs_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_FsChunk>(O_FS_LOAD)(into, &chunk) : reinterpret_cast<C_Fish *>(into)->LoadGame(chunk);
            // (The reader's depth and the next entry: the reader's Descend does not stop at the end of its parent.)
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
            std::printf("fs_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_fsShow;
void RunFsCase(const S_FsCase &c)
{
    g_caseStart = GetTickCount();
    ++g_fsCases;
    g_holdFrees = true;
    g_current = F("case %d", g_fsCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_schoolCount = int(c.schools.size());
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_texts[side].clear();
        BuildWorld(side, worldSeed);
        g_camera[side] = *reinterpret_cast<uint8_t **>(W().scene + 0x15C);
        g_rybaFrame = c.rybaFrame;
        g_visualsLeft = c.visuals;
        uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
        for (int i = 0; i < g_schoolCount; ++i) {
            const S_FsSchool &lc = c.schools[size_t(i)];
            uint8_t *frame = Child(sector, FRAME_DUMMY, F("school%d", i), nullptr);
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
                static const float rows[3][3] = {{0.0f, 1.0f, 0.0f}, {1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
                for (int k = 0; k < 3; ++k)
                    m.m[2][k] = rows[lc.odd - 1][k];
            }
            m.m[3][0] = lc.x;
            m.m[3][1] = lc.y;
            m.m[3][2] = lc.z;
            g_schoolFrame[side][i] = frame;
        }
        // (The camera at the first school mostly.)
        {
            I3D_frame *camera = AsFrame(Camera());
            const S_FsSchool &first = c.schools[0];
            camera->m_localMatrix.m[3][0] = first.x + 1.5f;
            camera->m_localMatrix.m[3][1] = first.y;
            camera->m_localMatrix.m[3][2] = first.z;
            camera->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
        }
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        for (int i = 0; i < g_schoolCount; ++i) {
            uint8_t *b;
            if (side) {
                b = Block(0xE0 + 16);
                OriginalAt<T_Void>(O_FS_CTOR)(b);
                Mut<uint8_t *>(b, 0x20) = g_schoolFrame[1][i];
            } else {
                b = Block(sizeof(C_Fish) + 16);
                new (b) C_Fish();
                reinterpret_cast<C_Fish *>(b)->m_frame = AsFrame(g_schoolFrame[0][i]);
            }
            g_school[side][i] = b;
            Name(b, F("school %d", i));
        }
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_log[side].clear();
        for (const std::string &s : SchoolsState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_FS_TRACE") ? std::atoi(std::getenv("HD2_FS_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_fsCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_KIND_NAMES[c.ops[i].kind],
                            c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunFsOp(c.ops[i], int(i)));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            for (const std::string &s : SchoolsState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        for (int i = 0; i < g_schoolCount; ++i) {
            uint8_t *b = g_school[side][i];
            if (side) {
                OriginalAt<T_FsInt>(O_FS_STOP)(b, 1);
                OriginalAt<T_Void>(O_FS_DTOR)(b);
            } else {
                PortSchool(i)->StopMission(true);
                PortSchool(i)->~C_Fish();
            }
        }
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(O_FS_CREATE)(OriginalAt<void *>(O_FS_CREATOR));
            Name(made, "created school");
            out.push_back(F("created type %d", At<int>(made, 0x18)));
            OriginalAt<void(TC *)(void *, void *)>(O_FS_DESTROY)(OriginalAt<void *>(O_FS_CREATOR), made);
        } else {
            C_ActorCreator *creator = g_actorCreators[ACTOR_FISH];
            C_Actor *made = creator->Create();
            Name(made, "created school");
            out.push_back(F("created type %d", int(made->GetType())));
            creator->Destroy(made);
        }
        // (The model cache emptied: its models are this world's frames.)
        ClearModelCache();
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        for (int i = 0; i < MAX_SCHOOLS; ++i)
            g_school[side][i] = nullptr;
    }
    static const int only = std::getenv("HD2_FS_ONLY") ? std::atoi(std::getenv("HD2_FS_ONLY")) : 0;
    if (g_fsShow && (!only || only == g_fsCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_fsShow == 2)
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
void SetUpFish()
{
    SetUpCmd();
    SetUpFishAnswers();
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
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
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
    std::puts("fs_diff: main");
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
        std::printf("fs_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_FS_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_FS_SHOW"))
        g_fsShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_FS_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_FS_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpFish();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunFsCase(RandomFsCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("fs_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_fsCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
