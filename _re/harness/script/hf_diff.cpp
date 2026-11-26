// Heavy fire support part 1: the air attacks and artillery strikes the radios call in (game/heavy_fire_support.cpp, the
// original's 0x00470500..0x004736d0) against the original executable's. cmd_diff.cpp's world, fakes and call logger are
// included (CMD_DIFF_NO_MAIN); the module's calls go through hf_calls.s / hf_calls.inc (gen_cmd_calls.py hf). The model
// and sound caches run on both sides (game/resource_cache.cpp linked; the original's constructed by their static
// initialisers' constructors).
//
// Each case makes a table (tables\airsupport.tbl: up to 16 kinds, each its model first, some values, unknown entries),
// makes the module on each side (the original's by its constructor 0x00471fc0 in a block of 0x1C0, the port's by its
// own: both read the table), starts the mission and runs the same random sequence of operations in the case's role:
// air attack and artillery requests (teams 0 and 1, kinds in the table and past it, 1..3 planes), updates, ticks,
// messages from the server (air attacks and artillery with made contents: kinds in the table, callers by id), starts and
// stops. After each operation the answer, the module's state (the kinds, the busy flags, every strike's fields and
// planes in each side's layout), the world's frames and every call made into the game and the engine (the explosions'
// and the sound requests' contents) are compared.
// MinGW; build_hf.bat; runs hf_diff.exe ("hf_diff: N checks, 0 failures"). Env: HD2_HF_CASES (default 1,500;
// HD2_HF_QUICK: 300), HD2_HF_SEED, HD2_HF_SHOW (1: the port's lines; 2: both), HD2_HF_DUMP.
#define HARNESS_NAME "hf_diff"
#define CMD_CALLS_INC "hf_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/heavy_fire_support.h"
#include "game/resource_cache.h"
#include "ls3d/igraph.h"
#undef protected
#undef private

// ---- the port's side of what the module calls and the test provides ----
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

extern C_HeavyFireSupport g_heavyFireSupport;
// The module base (its name and id, as the original's 0x0048d950; not logged: the port's global module is made before
// the logger is).
C_Module::C_Module(int id, const char *name) : m_name(name), m_id(id) {}
C_Module::~C_Module() {}

// The sound driver (ISndGetDriver: LS3DF's; the original's import thunk redirected): each side's world's.
namespace H {
uint8_t *g_sndDriver[2];
void *__cdecl O_ISndGetDriver() { return g_sndDriver[1]; }
}  // namespace H
extern "C" ISND_driver *ISndGetDriver() { return reinterpret_cast<ISND_driver *>(H::g_sndDriver[H::g_side]); }

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

// The messages' words (ReadBits answers them in turn on each side).
std::deque<uint32_t> g_bits[2];

void SetUpHeavyAnswers()
{
    g_answers["I3D_model::Open"] = [](const std::vector<uint32_t> &a) {
        const std::string file = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        uint8_t *model = reinterpret_cast<uint8_t *>(a[0]);
        Child(model, FRAME_VISUAL, file + " body", model);
        Mut<const char *>(model, 0x134) = Text(file);
        return file.find("missing") != std::string::npos ? uint32_t(I3DERR_NOFILE) : 0u;
    };
    g_answers["I3D_model::DuplicateModel"] = [](const std::vector<uint32_t> &a) {
        Mut<const char *>(reinterpret_cast<void *>(a[0]), 0x134) = Mut<const char *>(reinterpret_cast<void *>(a[1]), 0x134);
        return 0u;
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
    g_answers["I3D_frame::SetName"] = [](const std::vector<uint32_t> &a) {
        reinterpret_cast<I3D_frame *>(a[0])->m_name = Text(a[1] ? reinterpret_cast<const char *>(a[1]) : "");
        return 0u;
    };
    // A frame's bound (the planes' spans): a radius of 2..6 m, made valid.
    g_answers["I3D_frame::UpdateBound"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_bound.radius = float(2 + NextFake() % 5);
        f->m_frameFlags |= FRMFLAGS_BOUND_VALID;
        return 0u;
    };
    g_answers["I3D_driver::CreateFrame"] = [](const std::vector<uint32_t> &a) {
        return Ptr(Child(nullptr, int(a[1]), F("new%d", W().created++), nullptr));
    };
    g_answers["ISND_driver::IsInitialized"] = [](const std::vector<uint32_t> &) { return uint32_t(NextFake() % 6 != 0); };
    // The ground below: mostly there.
    g_answers["CollisionRayTest"] = [](const std::vector<uint32_t> &a) {
        if (NextFake() % 5 == 0)
            return 0u;
        const S_vector &from = *reinterpret_cast<const S_vector *>(a[0]);
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[2]))
            *hit = S_vector(from.x, from.y - float(10 + NextFake() % 400), from.z);
        static uint8_t volume[2][0x80];
        Name(volume[g_side], "ground");
        return Ptr(volume[g_side]);
    };
    // The messages: their words in turn (0 past the end).
    g_answers["C_BitStream::ReadBits"] = [](const std::vector<uint32_t> &) {
        if (g_bits[g_side].empty())
            return 0u;
        const uint32_t w = g_bits[g_side].front();
        g_bits[g_side].pop_front();
        return w;
    };
    g_answers["C_NetGame::CreateMessage"] = [](const std::vector<uint32_t> &) { return Ptr(W().bitstream); };
    // A sound request's file (the radio's: its name as text; the logger shows the pointer). The slot as the default.
    g_answers["C_SoundModule::Play"] = [](const std::vector<uint32_t> &a) {
        const char *file = *reinterpret_cast<const char *const *>(a[1] + 0x24);
        if (file)
            g_log[g_side].push_back(F("sound file %s", file));
        return uint32_t(FakeInt());
    };
    // A playing slot's sound (the planes' engines): a sound frame, or none.
    g_answers["C_SoundModule::GetSound"] = [](const std::vector<uint32_t> &) {
        return NextFake() % 4 ? Ptr(Child(nullptr, FRAME_SOUND, F("slot sound %d", W().created++), nullptr)) : 0u;
    };
}

// ---- the original's module ----
constexpr uint32_t O_HF_CTOR = 0x00471fc0, O_HF_REQUEST_AIR = 0x00472e50, O_HF_REQUEST_ARTILLERY = 0x00472ce0,
                   O_HF_TICK = 0x00472c60, O_HF_UPDATE = 0x00472510, O_HF_START = 0x00472250, O_HF_STOP = 0x00472270,
                   O_HF_MESSAGE = 0x00472590;
constexpr uint32_t O_MODEL_CACHE = 0x008aeda0, O_SOUND_CACHE = 0x008aed64;
uint8_t *g_module[2];
C_HeavyFireSupport *PortModule() { return reinterpret_cast<C_HeavyFireSupport *>(g_module[0]); }

// ---- the compared state ----
std::string V4(const float *v) { return VectorText(Ptr(v), 4); }
std::string V3(const float *v) { return VectorText(Ptr(v), 3); }
std::string Fl(float f) { return FloatText(f); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::string RepText(uint32_t rep) { return rep ? std::string(reinterpret_cast<const char *>(rep + 8)) : std::string(); }
std::vector<uint32_t> OVec(const uint8_t *p, uint32_t off)
{
    std::vector<uint32_t> out;
    const uint32_t first = At<uint32_t>(p, off + 4), last = At<uint32_t>(p, off + 8);
    for (uint32_t a = first; a && a < last; a += 4)
        out.push_back(*reinterpret_cast<const uint32_t *>(a));
    return out;
}
// A strike's fields (the original's at their offsets, the port's members).
struct S_AirFields {
    const float *pos, *target, *dir;
    float speed, range, damage;
    long long time;
    int timer, left, bombs, team, speechTimer;
    uint32_t caller, speech;
    std::vector<uint32_t> planes;
};
std::string AirText(const S_AirFields &a)
{
    // (The target's w: the original's request leaves it a stack word; never read.)
    std::string t = "air pos " + V3(a.pos) + " target " + V3(a.target) + " dir " + V3(a.dir) + " speed " + Fl(a.speed) +
                    F(" time %lld timer %d left %d bombs %d range ", a.time, a.timer, a.left, a.bombs) + Fl(a.range) +
                    " damage " + Fl(a.damage) +
                    F(" team %d caller %s speech %d %s planes", a.team, NameOf(a.caller).c_str(), a.speechTimer,
                      NameOf(a.speech).c_str());
    // (A plane: its offset +0, its model +0x10, its sound slot +0x14 on both sides.)
    for (uint32_t p : a.planes)
        t += " [" + V3(FP(reinterpret_cast<void *>(p), 0)) + " " + NameOf(At<uint32_t>(reinterpret_cast<void *>(p), 0x10)) +
             F(" %d]", At<int>(reinterpret_cast<void *>(p), 0x14));
    return t;
}
std::string AirTextOriginal(const uint8_t *s)
{
    return AirText({FP(s, 0), FP(s, 0x10), FP(s, 0x20), At<float>(s, 0x30), At<float>(s, 0x5C), At<float>(s, 0x60),
                    At<long long>(s, 0x48), At<int>(s, 0x50), At<int>(s, 0x54), At<int>(s, 0x58), At<int>(s, 0x64),
                    At<int>(s, 0x6C), At<uint32_t>(s, 0x68), At<uint32_t>(s, 0x70), OVec(s, 0x34)});
}
std::string AirTextPort(const C_AirStrike *s)
{
    std::vector<uint32_t> planes;
    for (const auto *p : s->m_planes)
        planes.push_back(Ptr(p));
    return AirText({&s->m_pos.x, &s->m_target.x, &s->m_dir.x, s->m_speed, s->m_range, s->m_damage, (long long)s->m_time,
                    s->m_blastTimer, s->m_blastsLeft, s->m_bombsPerPlane, s->m_team, s->m_speechTimer, Ptr(s->m_caller),
                    Ptr(s->m_speech), planes});
}
std::string ArtilleryText(const float *target, long long time, int timer, int left, int count, float range, float damage,
                          int team, uint32_t caller, int speech)
{
    // (The target's w: a stack word in the original's request, never read.)
    return "artillery target " + V3(target) + F(" time %lld timer %d left %d count %d range ", time, timer, left, count) +
           Fl(range) + " damage " + Fl(damage) + F(" team %d caller %s speech %d", team, NameOf(caller).c_str(), speech);
}
std::vector<std::string> ModuleState(bool kinds)
{
    std::vector<std::string> out;
    const uint8_t *m = g_module[g_side];
    const bool orig = g_side != 0;
    // (The kinds: the original's at +0x10, 0x18 bytes each; the port's members. The flags: the original's +0x1B0.)
    if (kinds)
        for (int k = 0; k < C_HeavyFireSupport::KIND_COUNT; ++k) {
            const uint8_t *x = orig ? m + 0x10 + 0x18 * k : reinterpret_cast<const uint8_t *>(&PortModule()->m_kinds[k]);
            const std::string model = orig ? RepText(At<uint32_t>(x, 0)) : PortModule()->m_kinds[k].model.c_str();
            out.push_back(F("kind %d %s speed ", k, model.c_str()) + Fl(At<float>(x, 4)) +
                          F(" sound %d bombs %d damage ", At<int>(x, 8), At<int>(x, 0xC)) + Fl(At<float>(x, 0x10)) + " range " +
                          Fl(At<float>(x, 0x14)));
        }
    const uint8_t *busy = orig ? m + 0x1B0 : reinterpret_cast<const uint8_t *>(PortModule()->m_busy);
    out.push_back(F("busy %d %d %d %d", int(busy[0]), int(busy[1]), int(busy[2]), int(busy[3])));
    if (orig) {
        for (uint32_t s : OVec(m, 0x190))
            out.push_back(AirTextOriginal(reinterpret_cast<const uint8_t *>(s)));
        for (uint32_t a : OVec(m, 0x1A0)) {
            const uint8_t *s = reinterpret_cast<const uint8_t *>(a);
            out.push_back(ArtilleryText(FP(s, 0), At<long long>(s, 0x10), At<int>(s, 0x18), At<int>(s, 0x1C), At<int>(s, 0x20),
                                        At<float>(s, 0x24), At<float>(s, 0x28), At<int>(s, 0x2C), At<uint32_t>(s, 0x30),
                                        At<int>(s, 0x34)));
        }
    } else {
        for (const C_AirStrike *s : PortModule()->m_airStrikes)
            out.push_back(AirTextPort(s));
        for (const C_ArtilleryStrike *s : PortModule()->m_artillery)
            out.push_back(ArtilleryText(&s->m_target.x, (long long)s->m_time, s->m_blastTimer, s->m_blastsLeft, s->m_blastCount,
                                        s->m_range, s->m_damage, s->m_team, Ptr(s->m_caller), s->m_speechTimer));
    }
    return out;
}
std::vector<std::string> HfFrameStates()
{
    std::vector<std::string> out;
    for (const S_FakeFrame &f : W().frames) {
        const I3D_frame *x = AsFrame(f.bytes);
        out.push_back(F("frame %s ref %d flags %08x pos ", NameOf(Ptr(f.bytes)).c_str(), int(x->m_refCount), x->m_frameFlags) +
                      VectorText(Ptr(&x->m_localMatrix.m[3][0]), 3) + " rot " + VectorText(Ptr(&x->m_rot), 4) + " parent " +
                      NameOf(Ptr(x->m_parent)) + " name " + (x->m_name ? x->m_name : ""));
    }
    return out;
}

// An explosion's posW (+0x0C): the original's is a stack word (the port's 0).
std::string MaskExplosionW(const std::string &line)
{
    if (line.compare(0, 20, "S_Explosion::Execute") != 0)
        return line;
    const size_t open = line.find('{');
    if (open == std::string::npos)
        return line;
    size_t at = open + 1;
    for (int word = 0; word < 3 && at != std::string::npos; ++word)
        at = line.find(' ', at) + 1;
    const size_t end = line.find(' ', at);
    return at == 0 || end == std::string::npos ? line : line.substr(0, at) + "-" + line.substr(end);
}

// A sound request's position's w (+0x0C): the original's is a stack word at every call (the port's 0).
std::string MaskSoundW(const std::string &line)
{
    if (line.compare(0, 20, "C_SoundModule::Play(") != 0)
        return line;
    const size_t open = line.find('{'), close = line.find('}', open);
    if (open == std::string::npos || close == std::string::npos)
        return line;
    const size_t last = line.rfind(' ', close);
    return line.substr(0, last + 1) + "-" + line.substr(close);
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
enum { OP_AIR, OP_ARTILLERY, OP_UPDATE, OP_TICK, OP_MESSAGE, OP_START, OP_STOP, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"Air", "Artillery", "Update", "Tick", "Message", "Start", "Stop"};
struct S_HfOp {
    int kind = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_HfCase {
    std::string table;                                  // (none: the defaults)
    int kinds = 0;
    uint32_t role = GAME_ROLE_LOCAL;
    std::vector<S_HfOp> ops;
};
int g_hfCases, g_opCounts[OP_KINDS];
// (Odd ones too: the timers land on 1 and one past their ends.)
const int FRAME_TIMES[] = {0, 16, 100, 250, 1000, 3000, 7000, 7000, 15000, 1, 7, 999, 6999, 14999};
S_HfCase RandomHfCase()
{
    S_HfCase c;
    if (P(0.9)) {
        S_Writer w;
        w.Begin(0x100);
        c.kinds = I(0, 16);
        size_t lastEntry = 0;                           // (the last value entry's start)
        for (int k = 0; k < c.kinds; ++k) {
            w.Begin(uint16_t(0x200 + k));
            static const char *const models[] = {"spitfire", "bf109", "stuka", "missing_plane", "p51"};
            const std::string model = models[I(0, 4)];
            w.Begin(1000);
            w.Raw(model.c_str(), model.size() + 1);
            w.End();
            for (int v = I(0, 6); v > 0; --v) {
                lastEntry = w.out.size();
                static const uint16_t ids[] = {2000, 3000, 4000, 5000, 6000, 7000};
                const uint16_t id = ids[I(0, 5)];
                if (id == 3000 || id == 4000)
                    w.EntryOf(id, I(0, 6));
                else {
                    static const float values[] = {30.0f, 80.0f, 5.0f, 1000.0f, 25.0f, 0.0f};
                    w.EntryOf(id, values[I(0, 5)]);
                }
            }
            w.End();
        }
        w.End();
        c.table = w.out;
        // (Cut short now and then in the last value entry's header: it does not read. A cut in a value reads its
        // bytes there in the original, nothing in the port.)
        if (lastEntry && P(0.1))
            c.table.resize(lastEntry + size_t(I(1, 5)));
    }
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    const int count = P(0.1) ? I(60, 150) : I(4, 40);
    for (int i = 0; i < count; ++i) {
        S_HfOp op;
        const int r = I(0, 99);
        op.kind = r < 15 ? OP_AIR : r < 27 ? OP_ARTILLERY : r < 65 ? OP_UPDATE : r < 80 ? OP_TICK : r < 92 ? OP_MESSAGE
                : r < 96 ? OP_START : OP_STOP;
        op.a = I(0, 1000);
        op.b = I(0, 1000);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    return c;
}

uint8_t *HumanActor(int n)
{
    const auto &humans = W().humans;
    return humans.empty() ? nullptr : W().actors[size_t(humans[size_t(n) % humans.size()])].bytes;
}
// (The C_Module part's virtuals take the module + 4 in the original: Update, Tick, StartMission, StopMission,
// OnMessage; the requests the module itself.)
using T_HfInt = void(TC *)(void *, int);
std::string RunHfOp(const S_HfOp &op, int kinds)
{
    const bool o = g_side != 0;
    uint8_t *m = g_module[g_side];
    C_HeavyFireSupport *p = PortModule();
    const int t = FRAME_TIMES[op.a % 14];
    switch (op.kind) {
    case OP_AIR: {
        // (A caller always: without one the original reads its id through null. Teams 0 and 1: past them the original
        // writes past its flags.)
        S_AirAttackRequest r = {};
        r.x = float(op.a % 400 - 200);
        r.z = float(op.b % 400 - 200);
        static const float dirs[][2] = {{1.0f, 0.0f}, {0.6f, 0.8f}, {0.0f, -2.0f}, {0.0f, 0.0f}, {-0.3f, 0.1f}};
        r.dirX = dirs[op.c % 5][0];
        r.dirZ = dirs[op.c % 5][1];
        r.kind = op.d % 3 == 0 ? op.d % 20 - 2 : (kinds ? op.d % kinds : 0);
        r.planes = 1 + op.b % 3;
        r.height = float(op.c % 100);
        r.caller = reinterpret_cast<C_Human *>(HumanActor(op.a));
        r.team = op.c & 1;
        if (!r.caller)
            return "RequestAirAttack (no caller)";
        const int result = o ? OriginalAt<int(TC *)(void *, const S_AirAttackRequest *)>(O_HF_REQUEST_AIR)(m, &r)
                             : p->RequestAirAttack(r);
        return F("RequestAirAttack kind %d planes %d team %d -> %d", r.kind, r.planes, r.team, result);
    }
    case OP_ARTILLERY: {
        S_ArtilleryRequest r = {};
        r.x = float(op.a % 400 - 200);
        r.z = float(op.b % 400 - 200);
        r.caller = reinterpret_cast<C_Human *>(HumanActor(op.b));
        r.kind = op.d % 4;
        r.team = op.c & 1;
        if (!r.caller)
            return "RequestArtillery (no caller)";
        const int result = o ? OriginalAt<int(TC *)(void *, const S_ArtilleryRequest *)>(O_HF_REQUEST_ARTILLERY)(m, &r)
                             : p->RequestArtillery(r);
        return F("RequestArtillery team %d -> %d", r.team, result);
    }
    case OP_UPDATE:
        if (o)
            OriginalAt<T_HfInt>(O_HF_UPDATE)(m + 4, t);
        else
            p->Update(t);
        return F("Update %d", t);
    case OP_TICK:
        if (o)
            OriginalAt<T_HfInt>(O_HF_TICK)(m + 4, t);
        else
            p->Tick(t);
        return F("Tick %d", t);
    case OP_MESSAGE: {
        // An air attack (a kind in the table: past it the original reads past the table) or artillery; the caller none,
        // an actor's id (a human's or another's; not past the list: the original reads the type through null).
        g_bits[g_side].clear();
        const bool air = op.a & 1;
        const auto f = [](float v) {
            uint32_t w;
            std::memcpy(&w, &v, 4);
            return w;
        };
        std::vector<uint32_t> ids;
        for (size_t k = 0; k < W().list.size(); ++k)
            if (W().list[k])
                ids.push_back(uint32_t(k));
        const uint32_t caller = op.d % 3 == 0 || ids.empty() ? 0xFFFFFFFFu : ids[size_t(op.d) % ids.size()];
        g_bits[g_side].push_back(air ? 0u : 1u);
        g_bits[g_side].push_back(f(float(op.b % 400 - 200)));
        g_bits[g_side].push_back(f(float(op.c % 100)));
        g_bits[g_side].push_back(f(float(op.c % 400 - 200)));
        if (air) {
            g_bits[g_side].push_back(f(float(op.a % 5 - 2)));
            g_bits[g_side].push_back(f(0.0f));
            g_bits[g_side].push_back(f(float(op.b % 3 - 1)));
            g_bits[g_side].push_back(kinds ? uint32_t(op.c % kinds) : 0u);
            g_bits[g_side].push_back(uint32_t(1 + op.b % 3));   // (1..3: without planes the original's Update reads the first)
        }
        g_bits[g_side].push_back(caller);
        if (air && !kinds)
            return "OnMessage (no kinds)";
        const bool r = o ? OriginalAt<bool(TC *)(void *, void *)>(O_HF_MESSAGE)(m + 4, W().bitstream)
                         : p->OnMessage(reinterpret_cast<C_BitStream *>(W().bitstream));
        return F("OnMessage %s caller %d -> %d", air ? "air" : "artillery", int(caller), int(r));
    }
    case OP_START:
        if (o)
            OriginalAt<T_HfInt>(O_HF_START)(m + 4, op.b & 1);
        else
            p->StartMission(op.b & 1);
        return "StartMission";
    default:
        if (o)
            OriginalAt<T_HfInt>(O_HF_STOP)(m + 4, op.b & 1);
        else
            p->StopMission(op.b & 1);
        return "StopMission";
    }
}

void ClearHfCaches()
{
    if (g_side) {
        OriginalAt<T_Void>(0x006d77e0)(OriginalAt<void *>(O_MODEL_CACHE));
        OriginalAt<T_Void>(0x006d87a0)(OriginalAt<void *>(O_SOUND_CACHE));
    } else {
        g_modelCache.Clear();
        g_soundCache.Clear();
    }
}

volatile DWORD g_caseStart;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("hf_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_hfShow;
void RunHfCase(const S_HfCase &c)
{
    g_caseStart = GetTickCount();
    ++g_hfCases;
    g_holdFrees = true;
    g_current = F("case %d", g_hfCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    if (c.table.empty())
        g_overrides.erase(Lower("tables\\airsupport.tbl"));
    else
        g_overrides[Lower("tables\\airsupport.tbl")] = c.table;
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_texts[side].clear();
        g_bits[side].clear();
        BuildWorld(side, worldSeed);
        g_sndDriver[side] = Block(0x40);
        *reinterpret_cast<void ***>(g_sndDriver[side]) = EngineVtable("ISND_driver");
        Name(g_sndDriver[side], "sound driver");
        SetRole(c.role);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        // The module (it reads the table), started.
        uint8_t *m = Block(0x1C0);
        if (side)
            OriginalAt<void(TC *)(void *, int, const char *)>(O_HF_CTOR)(m, MODULE_HEAVY_FIRE_SUPPORT, "HeavyFireSupportModule");
        else
            new (m) C_HeavyFireSupport(MODULE_HEAVY_FIRE_SUPPORT, "HeavyFireSupportModule");
        g_module[side] = m;
        Name(m, "module");
        if (side)
            OriginalAt<T_HfInt>(O_HF_START)(m + 4, 1);
        else
            PortModule()->StartMission(true);
        for (const std::string &s : ModuleState(true))
            out.push_back(s);
        // The interface's getter: the global module.
        out.push_back(side ? F("GetHeavyFireSupport %d", int(Ptr(OriginalAt<void *(*)()>(0x00471fb0)()) == 0x0089b2c0))
                           : F("GetHeavyFireSupport %d", int(GetHeavyFireSupport() == &g_heavyFireSupport)));
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            out.push_back(RunHfOp(c.ops[i], c.kinds));
            for (const std::string &s : ModuleState(false))
                out.push_back("  " + s);
        }
        // The end: stopped.
        if (side)
            OriginalAt<T_HfInt>(O_HF_STOP)(m + 4, 0);
        else
            PortModule()->StopMission(false);
        g_case = "";
        for (const std::string &s : HfFrameStates())
            out.push_back(s);
        for (const std::string &s : g_log[side])
            out.push_back("call " + MaskSoundW(MaskExplosionW(s)));
        ClearHfCaches();
    }
    if (g_hfShow) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_hfShow == 2)
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
void SetUpHeavy()
{
    SetUpCmd();
    SetUpHeavyAnswers();
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
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    // The caches (both sides' real; the original's built here).
    if (*OriginalAt<uint32_t *>(0x0098bcc0) != 2) {
        InitializeCriticalSection(OriginalAt<CRITICAL_SECTION *>(0x0098bca8));
        *OriginalAt<uint32_t *>(0x0098bcc0) = 2;
    }
    OriginalAt<void (*)()>(0x006d5030)();
    OriginalAt<void (*)()>(0x006d5370)();
    for (uint32_t cache : {O_MODEL_CACHE, O_SOUND_CACHE}) {
        *OriginalAt<uint32_t *>(cache + 0x34) = 256;
        uint8_t *buf = static_cast<uint8_t *>(O_New(8 * 256));
        *OriginalAt<uint8_t **>(cache + 0x18) = buf;
        *OriginalAt<uint8_t **>(cache + 0x1C) = buf;
        *OriginalAt<uint8_t **>(cache + 0x20) = buf + 8 * 256;
    }
    const struct {
        uint32_t cache;
        const char *dir;
    } dirs[] = {{O_MODEL_CACHE, "models\\"}, {O_SOUND_CACHE, "sounds\\"}};
    for (const auto &d : dirs)
        reinterpret_cast<T_AddDir>((*OriginalAt<void ***>(d.cache))[0x18 / 4])(OriginalAt<void *>(d.cache), d.dir);
    g_modelCache.AddDir("models\\");
    g_soundCache.AddDir("sounds\\");
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("hf_diff: main");
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
        std::printf("hf_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_HF_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_HF_SHOW"))
        g_hfShow = std::atoi(s) ? std::atoi(s) : 1;
    g_dump = std::getenv("HD2_HF_DUMP") != nullptr;
    int cases = std::getenv("HD2_HF_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_HF_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpHeavy();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunHfCase(RandomHfCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("hf_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_hfCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
