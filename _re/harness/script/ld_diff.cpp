// Ladder part 1: the ladders (game/ladder.cpp, the original's 0x005ea300..0x005ebde0) against the original executable's.
// cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the ladders' calls go through
// ld_calls.s / ld_calls.inc (gen_cmd_calls.py ld). The LS3DF math (the inverse, the vector by matrix products) is
// LS3DF.dll's on both sides; the game's texts are logged on both sides.
//
// Each case builds the same world on each side (cmd_diff's) and one or two ladders: frames under the primary sector
// (turned, tilted, scaled, now and then with a tiny, zero or vertical z axis) with their "up*" and "down*" dummies (now
// and then missing) and a visual with user properties ("matid" in any case, with and without a number), makes the
// ladders on each side (the original's by its constructor 0x005ea3d0 in a block of 0x190, the port's by its own) and
// runs the same random sequence of operations: SetFrame (its frame, none), ReadProperties (versions 0..4, short ones),
// WriteProperties, starts (the volumes, once), stops, restarts, CanUse (places about both ends, exactly at them, facings,
// speeds and looking up), climbers added and removed, the saved game's calls, GetMaterialId. After each operation the
// answer, every ladder's state (every field, its two volumes in each side's layout, its climbers), the frames and every
// call made into the game and the engine are compared; the properties' bytes too. The creator makes and destroys one.
// MinGW; build_ld.bat; runs ld_diff.exe ("ld_diff: N checks, 0 failures"). Env: HD2_LD_CASES (default 1,500;
// HD2_LD_QUICK: 300), HD2_LD_SEED, HD2_LD_SHOW (1: the port's lines; 2: both), HD2_LD_ONLY (the case shown),
// HD2_LD_TRACE (a case's operations as they start).
#define HARNESS_NAME "ld_diff"
#define CMD_CALLS_INC "ld_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/collision.h"
#include "game/game_texts.h"
#include "game/ladder.h"
#undef protected
#undef private

// ---- the port's side of what the ladders call and the test provides ----
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
// The actors' creators (the ladders' registers itself before main: a table of the test's).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the ladder's vtable holds but the test never reaches.
void C_Actor::Init() {}
void C_Actor::Done() {}
void C_Actor::CopyFrom(const C_Actor *) {}
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
// A frame's local matrix set whole (rows scaled), as I3D_frame::SetLocalMatrix: the rotation taken from it.
void SetLocal(uint8_t *frame, const S_matrix &m, const S_vector &scale)
{
    I3D_frame *f = AsFrame(frame);
    f->m_localMatrix = m;
    f->m_scale = scale;
    f->m_frameFlags = (f->m_frameFlags & ~0x28u) | FRMFLAGS_CHANGED | FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
}

void SetUpLadderAnswers()
{
    g_answers["C_Actor::SetFrame"] = [](const std::vector<uint32_t> &) { return 1u; };
    // Frames by name under a frame, of the mask's types: a trailing '*' (ENUMF_WILDMASK) matches a prefix ("*": any).
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        const std::string want = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        const bool wild = (a[2] & ENUMF_WILDMASK) && !want.empty() && want.back() == '*';
        const uint32_t types = a[2] & 0xFFFF;
        std::function<uint32_t(I3D_frame *)> find = [&](I3D_frame *p) -> uint32_t {
            for (I3D_frame *c = p->m_firstChild; c; c = c->m_nextSibling) {
                const std::string n = c->m_name ? c->m_name : "";
                const bool named = wild ? n.compare(0, want.size() - 1, want, 0, want.size() - 1) == 0 : n == want;
                if (named && c->m_type && (types & (1u << (c->m_type - 1))))
                    return Ptr(c);
                if (uint32_t d = find(c))
                    return d;
            }
            return 0u;
        };
        return find(reinterpret_cast<I3D_frame *>(a[0]));
    };
    // A hierarchy's box: filled and returned (min +0, max +0x10), by turns.
    g_answers["I3D_driver::GetHierarchyBBox"] = [](const std::vector<uint32_t> &a) {
        float *box = reinterpret_cast<float *>(a[1]);
        static const float boxes[][6] = {{-0.3f, 0.0f, -0.1f, 0.3f, 4.0f, 0.1f}, {-0.25f, -0.5f, -0.2f, 0.25f, 2.5f, 0.05f},
                                         {-0.4f, 0.0f, 0.0f, 0.4f, 6.0f, 0.3f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
        const float *b = boxes[NextFake() % 4];
        const float v[8] = {b[0], b[1], b[2], 0.0f, b[3], b[4], b[5], 0.0f};
        std::memcpy(box, v, sizeof v);
        return a[1];
    };
}

// ---- the ladders ----
constexpr int MAX_LADDERS = 2;
uint8_t *g_ladder[2][MAX_LADDERS];
uint8_t *g_ladderFrame[2][MAX_LADDERS];
int g_ladderCount;
C_Ladder *PortLadder(int i) { return reinterpret_cast<C_Ladder *>(g_ladder[0][i]); }
constexpr uint32_t O_LD_CTOR = 0x005ea3d0, O_LD_DTOR = 0x005ea590, O_LD_SET_FRAME = 0x005ea5d0, O_LD_WRITE = 0x005ebaf0,
                   O_LD_READ = 0x005ebbd0, O_LD_START = 0x005ead20, O_LD_STOP = 0x005eb120, O_LD_CAN_USE = 0x005eb1d0,
                   O_LD_ADD = 0x005eb8c0, O_LD_REMOVE = 0x005eba70, O_LD_RESTART = 0x005ea4d0, O_LD_SAVE = 0x005ea500,
                   O_LD_LOAD = 0x005ea510, O_LD_MATERIAL = 0x005ea560;
constexpr uint32_t O_LD_CREATOR = 0x008ac9d8, O_LD_CREATE = 0x005ea330, O_LD_DESTROY = 0x005ea3b0;

// ---- the compared state ----
std::string V3(const float *v) { return VectorText(Ptr(v), 3); }
std::string V4(const float *v) { return VectorText(Ptr(v), 4); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::string Fl(float f) { return FloatText(f); }
std::vector<uint32_t> OVec(const uint8_t *p, uint32_t off)
{
    std::vector<uint32_t> out;
    const uint32_t first = At<uint32_t>(p, off + 4), last = At<uint32_t>(p, off + 8);
    for (uint32_t a = first; a && a < last; a += 4)
        out.push_back(*reinterpret_cast<const uint32_t *>(a));
    return out;
}
// A volume (S_CollisionVolume: the same layout on both sides; its matrices the ladder's own).
std::string VolumeText(uint32_t p, const void *matrix, const void *inv)
{
    if (!p)
        return "volume none";
    const S_CollisionVolume &v = *reinterpret_cast<const S_CollisionVolume *>(p);
    return F("volume flags %x type %x frame %s actor %s matrix %s inv %s radius ", v.flags, v.type, NameOf(Ptr(v.frame)).c_str(),
             NameOf(Ptr(v.actor)).c_str(), v.matrix == matrix ? "own" : NameOf(Ptr(v.matrix)).c_str(),
             v.invMatrix == inv ? "own" : NameOf(Ptr(v.invMatrix)).c_str()) +
           Fl(v.radius) + " centre " + V4(&v.center.x) + " box " + V3(&v.bbox.min.x) + " " + V3(&v.bbox.max.x);
}
std::vector<std::string> LadderState(int i)
{
    std::vector<std::string> out;
    const uint8_t *b = g_ladder[g_side][i];
    std::string climbers = " climbers";
    if (g_side) {
        for (uint32_t c : OVec(b, 0x160))
            climbers += " " + NameOf(c);
        out.push_back(F("ladder%d started %d top ", i, int(b[0x90])) + V4(FP(b, 0xA0)) + " bottom " + V4(FP(b, 0xB0)) + " dir " +
                      V4(FP(b, 0xD0)) +
                      F(" u170 %d step %d climb %d u17c %d u180 %d material %d", At<int>(b, 0x170), At<int>(b, 0x174), At<int>(b, 0x178),
                        At<int>(b, 0x17C), int(b[0x180]), At<int>(b, 0x184)) +
                      climbers);
        out.push_back("  matrix " + VectorText(Ptr(b + 0xE0), 16) + " inv " + VectorText(Ptr(b + 0x120), 16));
        out.push_back("  " + VolumeText(At<uint32_t>(b, 0xC0), b + 0xE0, b + 0x120));
        out.push_back("  " + VolumeText(At<uint32_t>(b, 0xC4), b + 0xE0, b + 0x120));
        return out;
    }
    const C_Ladder *p = PortLadder(i);
    for (const C_Human *c : p->m_climbers)
        climbers += " " + NameOf(Ptr(c));
    out.push_back(F("ladder%d started %d top ", i, int(p->m_started)) + V4(&p->m_top.x) + " bottom " + V4(&p->m_bottom.x) + " dir " +
                  V4(&p->m_dir.x) +
                  F(" u170 %d step %d climb %d u17c %d u180 %d material %d", p->m_unknown170, p->m_stepSound, p->m_climbSound,
                    p->m_unknown17C, int(p->m_unknown180), p->m_materialId) +
                  climbers);
    out.push_back("  matrix " + VectorText(Ptr(&p->m_matrix), 16) + " inv " + VectorText(Ptr(&p->m_invMatrix), 16));
    out.push_back("  " + VolumeText(Ptr(p->m_volume), &p->m_matrix, &p->m_invMatrix));
    out.push_back("  " + VolumeText(Ptr(p->m_topVolume), &p->m_matrix, &p->m_invMatrix));
    return out;
}
std::vector<std::string> LaddersState()
{
    std::vector<std::string> out;
    for (int i = 0; i < g_ladderCount; ++i)
        for (const std::string &s : LadderState(i))
            out.push_back(s);
    for (int i = 0; i < g_ladderCount; ++i) {
        const I3D_frame *f = AsFrame(g_ladderFrame[g_side][i]);
        out.push_back(F("frame%d flags %08x parent %s at ", i, f->m_frameFlags, NameOf(Ptr(f->m_parent)).c_str()) +
                      VectorText(Ptr(&f->m_localMatrix.m[3][0]), 3));
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
};
enum { OP_SET_FRAME, OP_READ, OP_WRITE, OP_START, OP_STOP, OP_RESTART, OP_CAN_USE, OP_CLIMBER, OP_SAVE_LOAD, OP_MATERIAL, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"SetFrame", "ReadProperties", "WriteProperties", "Start", "Stop", "Restart", "CanUse",
                                     "Climber", "SaveLoad", "Material"};
struct S_LdOp {
    int kind = 0;
    int ladder = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_LdLadder {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f, tilt = 0.0f, height = 3.0f;
    S_vector scale = S_vector(1.0f, 1.0f, 1.0f);
    int odd = 0;                                        // (1..5: its z axis tiny, zero or vertical)
    bool up = true, down = true, visual = true;
    std::string props;
};
struct S_LdCase {
    std::vector<S_LdLadder> ladders;
    std::vector<S_LdOp> ops;
};
int g_ldCases, g_opCounts[OP_KINDS];
S_LdCase RandomLdCase()
{
    S_LdCase c;
    const int ladders = P(0.75) ? 1 : 2;
    for (int i = 0; i < ladders; ++i) {
        S_LdLadder l;
        l.x = float(I(-40, 40)) * 0.5f;
        l.y = float(I(-4, 10)) * 0.5f;
        l.z = float(I(-40, 40)) * 0.5f;
        l.heading = float(I(0, 628)) * 0.01f;
        l.tilt = P(0.3) ? float(I(-40, 40)) * 0.01f : 0.0f;
        l.height = float(I(2, 16)) * 0.5f;
        if (P(0.3)) {
            static const float scales[] = {1.0f, 2.0f, 0.5f, 1.25f};
            l.scale = S_vector(scales[I(0, 3)], scales[I(0, 3)], scales[I(0, 3)]);
        }
        l.odd = P(0.1) ? I(1, 5) : 0;
        l.up = P(0.93);
        l.down = P(0.93);
        l.visual = P(0.9);
        static const char *const props[] = {"", "matid 7", "MATID12 x", "matid", "matid -3", "foo matid=5", "x MatId 40 y",
                                            "matid 2147483647", "nothing here"};
        l.props = props[I(0, 8)];
        c.ladders.push_back(l);
    }
    const int count = P(0.1) ? I(40, 120) : I(4, 40);
    for (int i = 0; i < count; ++i) {
        S_LdOp op;
        const int r = I(0, 99);
        op.kind = r < 6 ? OP_SET_FRAME : r < 14 ? OP_READ : r < 18 ? OP_WRITE : r < 28 ? OP_START : r < 34 ? OP_STOP
                : r < 38 ? OP_RESTART : r < 80 ? OP_CAN_USE : r < 92 ? OP_CLIMBER : r < 96 ? OP_SAVE_LOAD : OP_MATERIAL;
        op.ladder = I(0, ladders - 1);
        op.a = I(0, 1000);
        op.b = I(0, 1000);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    // The frame set first, mostly; then started.
    for (int i = ladders; i-- > 0;) {
        if (P(0.85)) {
            S_LdOp start;
            start.kind = OP_START;
            start.ladder = i;
            start.c = I(0, 1);
            c.ops.insert(c.ops.begin(), start);
        }
        if (P(0.95)) {
            S_LdOp set;
            set.kind = OP_SET_FRAME;
            set.ladder = i;
            set.a = 1;
            c.ops.insert(c.ops.begin(), set);
        }
    }
    return c;
}

uint8_t *HumanActor(int n)
{
    const auto &humans = W().humans;
    return humans.empty() ? nullptr : W().actors[size_t(humans[size_t(n) % humans.size()])].bytes;
}
bool LadderHasFrame(int i)
{
    return g_side ? At<uint32_t>(g_ladder[1][i], 0x20) != 0 : PortLadder(i)->m_frame != nullptr;
}

using T_LdInt = void(TC *)(void *, int);
using T_LdChunk = bool(TC *)(void *, void *);
std::string RunLdOp(const S_LdOp &op, int index)
{
    const bool o = g_side != 0;
    const int li = op.ladder < g_ladderCount ? op.ladder : 0;
    uint8_t *b = g_ladder[g_side][li];
    C_Ladder *p = PortLadder(li);
    switch (op.kind) {
    case OP_SET_FRAME: {
        // (Its frame, or none.)
        uint8_t *frame = op.a % 8 ? g_ladderFrame[g_side][li] : nullptr;
        if (frame) {
            if (o)
                Mut<uint8_t *>(b, 0x20) = frame;
            else
                p->m_frame = AsFrame(frame);
        }
        const bool r = o ? OriginalAt<bool(TC *)(void *, void *)>(O_LD_SET_FRAME)(b, frame) : p->SetFrame(AsFrame(frame));
        return F("SetFrame %d %s -> %d", li, NameOf(Ptr(frame)).c_str(), int(r));
    }
    case OP_READ: {
        // Version (0..4) as a byte, the unknown, the sounds, the version's extras; now and then cut short.
        S_Writer w;
        w.Begin(0xAE24);
        const int8_t version = int8_t(op.a % 5);
        w.Raw(&version, 1);
        const int values[] = {op.b % 7 - 1, op.c % 40 - 1, op.d % 40 - 1, op.a % 3};
        w.Raw(values, 16);
        const uint8_t flag = uint8_t(op.b & 1);
        w.Raw(&flag, 1);
        w.End();
        std::string bytes = w.out;
        // (Not cut before the version: the original reads it as a stack word, the port's fix 0.)
        if (op.c % 6 == 0)
            bytes.resize(size_t(6 + 1 + op.d % 18));
        const std::string name = F("ld_props%d.bin", index);
        g_overrides[Lower(name)] = bytes;
        C_chunk chunk;
        chunk.Open(name.c_str(), false);
        uint16_t id;
        chunk.Descend(id);
        const bool r = o ? OriginalAt<T_LdChunk>(O_LD_READ)(b, &chunk) : p->ReadProperties(chunk);
        chunk.Close();
        return F("ReadProperties %d version %d -> %d", li, int(version), int(r));
    }
    case OP_WRITE: {
        const std::string file = F("ld_write%d.bin", index);
        C_chunk chunk;
        chunk.Open(file.c_str(), true);
        chunk.Begin(0xAE24);
        const bool r = o ? OriginalAt<T_LdChunk>(O_LD_WRITE)(b, &chunk) : p->WriteProperties(chunk);
        chunk.Ascend();
        chunk.Close();
        std::string bytes;
        FileText(file, bytes);
        std::string hex;
        for (unsigned char ch : bytes)
            hex += F("%02x", ch);
        return F("WriteProperties %d -> %d ", li, int(r)) + hex;
    }
    case OP_START: {
        // (Not without its frame: the original takes its parent through null.)
        if (!LadderHasFrame(li))
            return "StartMission (no frame)";
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_LdInt>(O_LD_START)(b, server);
        else
            p->StartMission(server);
        return F("StartMission %d %d", li, int(server));
    }
    case OP_STOP: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_LdInt>(O_LD_STOP)(b, server);
        else
            p->StopMission(server);
        return F("StopMission %d %d", li, int(server));
    }
    case OP_RESTART:
        if (o)
            OriginalAt<T_LdInt>(O_LD_RESTART)(b, op.c & 1);
        else
            p->RestartMission(op.c & 1);
        return F("RestartMission %d", li);
    case OP_CAN_USE: {
        // A place about one end (exactly at it now and then), a facing, a speed, looking up or not.
        const float *top = o ? FP(b, 0xA0) : &p->m_top.x;
        const float *bottom = o ? FP(b, 0xB0) : &p->m_bottom.x;
        const float *dir = o ? FP(b, 0xD0) : &p->m_dir.x;
        const float *end = op.a & 1 ? top : bottom;
        static const float offsets[] = {0.0f, 0.25f, -0.25f, 0.5f, -0.6f, 1.0f, 0.1f, -0.3f, 0.49f};
        alignas(16) S_vector4 pos = {end[0] + offsets[op.b % 9] * (op.c & 1 ? 1.0f : 0.0f), end[1] + offsets[(op.b / 9) % 9],
                                     end[2] + offsets[(op.b / 81) % 9], 1.0f};
        const float dx = dir[0], dz = dir[2];
        // (Now and then along the ladder's way instead: behind or before the end, above or below it.)
        if ((op.a / 2) % 2) {
            static const float along[] = {-1.0f, -0.5f, -0.25f, 0.3f, 0.5f, 1.0f, 0.0f, -0.7f, 0.6f};
            static const float heights[] = {0.0f, 0.25f, 0.6f, -0.2f, -0.4f, 0.45f, 0.1f, -0.25f, 0.7f};
            const float t = along[op.b % 9];
            pos = {end[0] + dx * t, end[1] + heights[(op.b / 9) % 9], end[2] + dz * t, 1.0f};
        }
        // (Facings: the ladder's own way, against it, across, tilted, and odd ones.)
        static const float facings[][3] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
                                           {0.6f, 0.3f, 0.8f}, {0.0f, 1.0f, 0.0f}, {1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
                                           {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
        const int fi = op.c % 10;
        alignas(16) S_vector4 face = {facings[fi][0], facings[fi][1], facings[fi][2], 0.0f};
        if (fi == 0)
            face = {dx, 0.0f, dz, 0.0f};
        else if (fi == 1)
            face = {-dx, 0.25f, -dz, 0.0f};
        else if (fi == 8)
            face = {dx * 0.9f, 0.5f, dz * 0.9f, 0.0f};          // (raised: its level part shorter than 1)
        else if (fi == 9)
            face = {-dx * 0.7f, 0.5f, -dz * 0.7f, 0.0f};
        static const float speeds[] = {0.0f, 1.0f, 300.0f, -200.0f, 0.0005f, 50.0f, 0.0012f, 1e-4f};
        const float s = speeds[op.d % 8];
        const int vi = (op.d / 8) % 5;
        const S_vector velocity = vi == 0 ? S_vector(dx * s, 0.0f, dz * s) : vi == 1 ? S_vector(-dx * s, 5.0f, -dz * s)
                                  : vi == 2 ? S_vector(s, 0.0f, 0.0f) : vi == 3 ? S_vector(0.0f, s, 0.0f)
                                  : (op.d / 80) & 1 ? S_vector(dx * s, 1.0f, dz * s) : S_vector(-dx * s, 1.0f, -dz * s);
        const bool up = (op.d / 40) & 1;
        const bool r = o ? OriginalAt<bool(TC *)(void *, const S_vector4 *, const S_vector4 *, const S_vector *, int)>(O_LD_CAN_USE)(
                               b, &pos, &face, &velocity, up)
                         : p->CanUse(pos, face, velocity, up);
        return F("CanUse %d ", li) + V3(&pos.x) + " " + V3(&face.x) + " " + V3(&velocity.x) + F(" %d -> %d", int(up), int(r));
    }
    case OP_CLIMBER: {
        uint8_t *human = HumanActor(op.b);
        const bool add = op.a & 1;
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(add ? O_LD_ADD : O_LD_REMOVE)(b, human);
        else if (add)
            p->AddClimber(reinterpret_cast<C_Human *>(human));
        else
            p->RemoveClimber(reinterpret_cast<C_Human *>(human));
        return F("%s %d %s", add ? "AddClimber" : "RemoveClimber", li, NameOf(Ptr(human)).c_str());
    }
    case OP_SAVE_LOAD: {
        C_chunk chunk;
        const bool rs = o ? OriginalAt<T_LdChunk>(O_LD_SAVE)(b, &chunk) : p->SaveGame(chunk);
        const bool rl = o ? OriginalAt<T_LdChunk>(O_LD_LOAD)(b, &chunk) : p->LoadGame(chunk);
        return F("SaveGame %d -> %d LoadGame -> %d", li, int(rs), int(rl));
    }
    default: {
        const int r = o ? OriginalAt<int(TC *)(void *, void *)>(O_LD_MATERIAL)(b, nullptr) : p->GetMaterialId(nullptr);
        return F("GetMaterialId %d -> %d", li, r);
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
            std::printf("ld_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_ldShow;
void RunLdCase(const S_LdCase &c)
{
    g_caseStart = GetTickCount();
    ++g_ldCases;
    g_holdFrees = true;
    g_current = F("case %d", g_ldCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_ladderCount = int(c.ladders.size());
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_texts[side].clear();
        BuildWorld(side, worldSeed);
        uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
        for (int i = 0; i < g_ladderCount; ++i) {
            const S_LdLadder &lc = c.ladders[size_t(i)];
            // The ladder's frame under a parent dummy (StartMission relinks it to its parent).
            uint8_t *parent = Child(sector, FRAME_DUMMY, F("ladder parent%d", i), nullptr);
            uint8_t *frame = Child(parent, FRAME_MODEL, F("ladder%d", i), nullptr);
            S_matrix m;
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
                static const float rows[5][3] = {{1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1e-5f},
                                                 {0.0f, 0.0f, 1e-5f}};
                for (int k = 0; k < 3; ++k)
                    m.m[2][k] = rows[lc.odd - 1][k];
            }
            for (int k = 0; k < 3; ++k) {
                m.m[0][k] *= lc.scale.x;
                m.m[1][k] *= lc.scale.y;
                m.m[2][k] *= lc.scale.z;
            }
            m.m[3][0] = lc.x;
            m.m[3][1] = lc.y;
            m.m[3][2] = lc.z;
            SetLocal(frame, m, lc.scale);
            if (lc.visual) {
                uint8_t *visual = Child(frame, FRAME_VISUAL, "ladder mesh", frame);
                AsFrame(visual)->m_userProps = Text(lc.props);
            } else {
                AsFrame(frame)->m_userProps = Text(lc.props);
            }
            if (lc.down) {
                uint8_t *down = Child(frame, FRAME_DUMMY, "down01", frame);
                AsFrame(down)->m_localMatrix.m[3][2] = 0.3f;
            }
            if (lc.up) {
                uint8_t *up = Child(frame, FRAME_DUMMY, "up_top", frame);
                AsFrame(up)->m_localMatrix.m[3][1] = lc.height;
                AsFrame(up)->m_localMatrix.m[3][2] = 0.1f;
            }
            g_ladderFrame[side][i] = frame;
        }
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        for (int i = 0; i < g_ladderCount; ++i) {
            uint8_t *b;
            if (side) {
                b = Block(0x190 + 16);
                OriginalAt<T_Void>(O_LD_CTOR)(b);
            } else {
                b = Block(sizeof(C_Ladder) + 16);
                new (b) C_Ladder();
            }
            g_ladder[side][i] = b;
            Name(b, F("ladder %d", i));
        }
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_log[side].clear();
        for (const std::string &s : LaddersState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_LD_TRACE") ? std::atoi(std::getenv("HD2_LD_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_ldCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_KIND_NAMES[c.ops[i].kind],
                            c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunLdOp(c.ops[i], int(i)));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            for (const std::string &s : LaddersState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed.
        for (int i = 0; i < g_ladderCount; ++i) {
            uint8_t *b = g_ladder[side][i];
            // (Stopped first: the original's destructor leaves a started ladder's volumes in the world, the port's
            // fix frees them.)
            if (side) {
                OriginalAt<T_LdInt>(O_LD_STOP)(b, 1);
                OriginalAt<T_Void>(O_LD_DTOR)(b);
            } else {
                PortLadder(i)->StopMission(true);
                PortLadder(i)->~C_Ladder();
            }
        }
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(O_LD_CREATE)(OriginalAt<void *>(O_LD_CREATOR));
            Name(made, "created ladder");
            out.push_back(F("created type %d", At<int>(made, 0x18)));
            OriginalAt<void(TC *)(void *, void *)>(O_LD_DESTROY)(OriginalAt<void *>(O_LD_CREATOR), made);
        } else {
            C_ActorCreator *creator = g_actorCreators[ACTOR_LADDER];
            C_Actor *made = creator->Create();
            Name(made, "created ladder");
            out.push_back(F("created type %d", int(made->GetType())));
            creator->Destroy(made);
        }
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        for (int i = 0; i < MAX_LADDERS; ++i)
            g_ladder[side][i] = nullptr;
    }
    static const int only = std::getenv("HD2_LD_ONLY") ? std::atoi(std::getenv("HD2_LD_ONLY")) : 0;
    if (g_ldShow && (!only || only == g_ldCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_ldShow == 2)
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

void SetUpLadder()
{
    SetUpCmd();
    SetUpLadderAnswers();
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
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("ld_diff: main");
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
        std::printf("ld_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_LD_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_LD_SHOW"))
        g_ldShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_LD_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_LD_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpLadder();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunLdCase(RandomLdCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("ld_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_ldCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
