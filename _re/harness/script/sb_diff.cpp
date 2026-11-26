// Submarine part 1: the submarines (game/submarine.cpp, the original's 0x005de660..0x005e2070) against the original
// executable's. cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the submarine's calls go
// through sb_calls.s / sb_calls.inc (gen_cmd_calls.py sb). The LS3DF math is LS3DF.dll's on both sides; the pilot
// seat's camera keeper (the camera module's, tested with it) is logged on both sides; so are the game's texts.
//
// Each case builds the same world on each side (cmd_diff's, plus one or two submarines: models under the primary sector
// with their body, seats (user properties, camera dummies), propeller and hull boxes (dummies with boxes, visuals), some
// missing), makes the submarines on each side (the original's by its constructor 0x005df000 in a block of 0x130, the
// port's by its own), sets their frames, gives them properties, starts the mission and runs the same random sequence of
// operations in the case's role: updates and ticks (the game's time advancing), the controls, the seats' virtuals,
// FindExit, saves and loads, restarts, stops and starts. The sweeps and the ground answer by turns. After each operation
// the answer, every submarine's state (every field, its boxes, seats and sounds in each side's layout), the world's
// frames and every call made into the game and the engine are compared; a save's bytes too.
// MinGW; build_sb.bat; runs sb_diff.exe ("sb_diff: N checks, 0 failures"). Env: HD2_SB_CASES (default 1,500;
// HD2_SB_QUICK: 300), HD2_SB_SEED, HD2_SB_SHOW (1: the port's lines; 2: both), HD2_SB_DUMP, HD2_SB_TRACE.
#define HARNESS_NAME "sb_diff"
#define CMD_CALLS_INC "sb_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/camera_module.h"
#include "game/collision.h"
#include "game/frame_volume.h"
#include "game/game_texts.h"
#include "game/submarine.h"
#undef protected
#undef private

// ---- the port's side of what the submarine calls and the test provides ----
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
S_vector __stdcall S_vector::RotateByMatrix(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_vector *, float *, const S_matrix &)>(
        "?RotateByMatrix@S_vector@@QBG?AU1@ABUS_matrix@@@Z");
    alignas(16) float r[4] = {};
    f(this, r, m);
    return S_vector(r[0], r[1], r[2]);
}
S_vector &__stdcall S_vector::operator*=(const S_matrix &m)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_vector *, const S_matrix &)>("??XS_vector@@QAGAAU0@ABUS_matrix@@@Z");
    f(this, m);
    return *this;
}
S_quat __stdcall S_quat::RotateByMatrix(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, S_quat *, const S_matrix &)>(
        "?RotateByMatrix@S_quat@@QBG?AU1@ABUS_matrix@@@Z");
    S_quat r;
    f(this, &r, m);
    return r;
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
// The actors' creators (the submarines' registers itself before main: a table of the test's).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the submarine's vtable holds but the test never reaches.
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

// The pilot seat's camera keeper: logged on both sides (the original's constructor, Restore and Update redirected; its
// StopMission restores and frees the keeper inline, the port's destructor restores).
namespace H {
void KeeperLog(const void *k, const std::string &what) { g_log[g_side].push_back(NameOf(reinterpret_cast<uint32_t>(k)) + " " + what); }
void KeeperMade(void *k, const void *owner, const void *frame)
{
    static int count[2];
    Name(k, F("keeper %d", count[g_side]++));
    KeeperLog(k, "made(" + NameOf(reinterpret_cast<uint32_t>(owner)) + ", " + NameOf(reinterpret_cast<uint32_t>(frame)) + ")");
}
}  // namespace H
C_CameraFrameKeeper::C_CameraFrameKeeper(C_Actor *owner, I3D_frame *frame) : m_frame(frame), m_owner(owner)
{
    H::KeeperMade(this, owner, frame);
}
C_CameraFrameKeeper::~C_CameraFrameKeeper() { Restore(); }
bool C_CameraFrameKeeper::Restore()
{
    H::KeeperLog(this, "Restore");
    return m_frame != nullptr;
}
bool C_CameraFrameKeeper::Update(int time)
{
    H::KeeperLog(this, H::F("Update(%d)", time));
    return m_frame && m_owner;
}

namespace H {

#define OT __attribute__((thiscall))
void *OT O_KeeperCtor(uint8_t *k, void *owner, void *frame)
{
    *reinterpret_cast<void **>(k + 0x40) = frame;
    *reinterpret_cast<void **>(k + 0x44) = owner;
    KeeperMade(k, owner, frame);
    return k;
}
bool OT O_KeeperRestore(uint8_t *k)
{
    KeeperLog(k, "Restore");
    return *reinterpret_cast<void **>(k + 0x40) != nullptr;
}
bool OT O_KeeperUpdate(uint8_t *k, int time)
{
    KeeperLog(k, F("Update(%d)", time));
    return *reinterpret_cast<void **>(k + 0x40) && *reinterpret_cast<void **>(k + 0x44);
}
// The texts (the original's C_GameTexts::Get: its C_str by a hidden pointer).
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

// ---- the submarines ----
constexpr int MAX_SUBS = 2;
uint8_t *g_sub[2][MAX_SUBS];
uint8_t *g_subModel[2][MAX_SUBS];
int g_subCount;
C_Submarine *PortSub(int i) { return reinterpret_cast<C_Submarine *>(g_sub[0][i]); }
// A submarine's model (by the world's random numbers: the same on both sides).
void BuildSubmarine(uint8_t *model)
{
    auto place = [](uint8_t *f, float x, float y, float z) {
        S_matrix &m = AsFrame(f)->m_localMatrix;
        m.m[3][0] = x;
        m.m[3][1] = y;
        m.m[3][2] = z;
    };
    if (WI(0, 5))
        place(Child(model, FRAME_DUMMY, "BODY", model), 0.0f, float(WI(0, 4)) * 0.5f, 0.0f);
    static const char *const props[] = {"SEAT12,name17", "SEAT0 name4", "SEAT", "x name9", "", "SEAT3"};
    // (Both seats always: SetFrame looks for the second first and stops at a missing one; without the pilot's frame the
    // original's StartMission reads through null.)
    const int seats = 2;
    for (int s = 0; s < seats; ++s) {
        uint8_t *seat = Child(model, FRAME_DUMMY, F("SEAT%02d", s), model);
        AsFrame(seat)->m_userProps = Text(props[WI(0, 5)]);
        place(seat, float(WI(-2, 2)), 1.0f + float(WI(0, 2)), float(WI(-4, 4)));
        if (WI(0, 2))
            Child(seat, FRAME_DUMMY, F("camera3RD%d", s), model);
        if (WI(0, 2))
            Child(seat, FRAME_DUMMY, F("cameraFPV%d", s), model);
    }
    if (WI(0, 1))
        Child(model, FRAME_DUMMY, "camera3RDb", model);
    if (WI(0, 4))
        Child(model, FRAME_VISUAL, "sroub", model);
    if (WI(0, 5)) {
        uint8_t *hull = Child(model, FRAME_DUMMY, "BODYCOLB00", model);
        // (Child k - 1: LinkUnder puts each first. Boxes 1 (the sweep's first) and the last (UpdateVolumes' first) are
        // dummies: a visual box takes the centre found before it, a stack value in the original for the first.)
        const int count = WI(0, 4);
        for (int k = count; k > 0; --k) {
            const bool dummy = k == 2 || k == count || WI(0, 2) != 0;
            uint8_t *b = Child(hull, dummy ? FRAME_DUMMY : FRAME_VISUAL, F("colb%d", k), model);
            place(b, float(WI(-3, 3)), float(WI(-1, 1)), float(WI(-8, 8)));
            if (dummy) {
                float *box = reinterpret_cast<float *>(b + 0x120);
                const float s = float(WI(1, 4));
                const float v[8] = {-s, -0.5f * s, -2.0f * s, 0.0f, s + 1.0f, 0.5f * s, 2.0f * s, 0.0f};   // (off-centre in x)
                std::memcpy(box, v, sizeof v);
            }
        }
    }
}

std::map<uint32_t, uint8_t *> g_slotSounds[2];         // (each side's slots' sounds: the case's world's)
void SetUpSubmarineAnswers()
{
    // Frames by name under a frame: a trailing '*' (ENUMF_WILDMASK) matches a prefix.
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        const std::string want = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        const bool wild = !want.empty() && want.back() == '*';
        std::function<uint32_t(I3D_frame *)> find = [&](I3D_frame *p) -> uint32_t {
            for (I3D_frame *c = p->m_firstChild; c; c = c->m_nextSibling) {
                const std::string n = c->m_name ? c->m_name : "";
                if (wild ? n.compare(0, want.size() - 1, want, 0, want.size() - 1) == 0 : n == want)
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
    // The base's SetFrame: it took the frame (the test sets it first).
    g_answers["C_Actor::SetFrame"] = [](const std::vector<uint32_t> &) { return 1u; };
    // A frame's position (the submarines' moves): its local matrix's, its matrices made again.
    g_answers["I3D_frame::SetPos"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[1]);
        f->m_localMatrix.m[3][0] = pos.x;
        f->m_localMatrix.m[3][1] = pos.y;
        f->m_localMatrix.m[3][2] = pos.z;
        // (Its children's world matrices go stale with it, as the engine's.)
        std::function<void(I3D_frame *)> stale = [&](I3D_frame *x) {
            x->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
            for (I3D_frame *c = x->m_firstChild; c; c = c->m_nextSibling)
                stale(c);
        };
        stale(f);
        return 0u;
    };
    g_answers["I3D_frame::SetOn"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_frameFlags = (a[1] & 0xFF) ? (f->m_frameFlags | 1u) : (f->m_frameFlags & ~1u);
        return 0u;
    };
    // A hierarchy's box (the hull's visuals): filled and returned (min +0, max +0x10). (Not empty: the original's Init
    // leaves an empty one's centre unset, the port's fix 0.)
    g_answers["I3D_driver::GetHierarchyBBox"] = [](const std::vector<uint32_t> &a) {
        float *box = reinterpret_cast<float *>(a[1]);
        const float s = float(1 + NextFake() % 4);
        const float b[8] = {-2.0f * s, -0.5f * s, -3.0f * s, 0.0f, 2.0f * s, 0.75f * s, 3.0f * s, 0.0f};
        std::memcpy(box, b, sizeof b);
        return a[1];
    };
    // A sound slot's sound: a sound frame (its own per slot), or none for some slots (the same answer every time: the
    // frequency asks again unchecked).
    g_answers["C_SoundModule::GetSound"] = [](const std::vector<uint32_t> &a) {
        if (a[1] % 7 == 3)
            return 0u;
        uint8_t *&s = g_slotSounds[g_side][a[1]];
        if (!s)
            s = Child(nullptr, FRAME_SOUND, F("slot%d sound", int(a[1])), nullptr);
        if (std::getenv("HD2_SB_BITS"))
            g_log[g_side].push_back(F("(slot %d flags %08x)", int(a[1]), AsFrame(s)->m_frameFlags));
        return Ptr(s);
    };
    g_answers["I3D_sound::SetFrequency"] = [](const std::vector<uint32_t> &a) {
        Mut<uint32_t>(reinterpret_cast<void *>(a[0]), 0x174) = a[1];
        return 0u;
    };
    // The spheres: a hit now and then, part way; its normal across.
    g_answers["CollisionSphereTest"] = [](const std::vector<uint32_t> &a) {
        if (NextFake() % 3)
            return 0u;
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[0]);
        const S_vector &move = *reinterpret_cast<const S_vector *>(a[1]);
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[3]))
            *hit = S_vector(pos.x + move.x * 0.5f, pos.y + move.y * 0.5f, pos.z + move.z * 0.5f);
        if (S_vector *n = reinterpret_cast<S_vector *>(a[4])) {
            static const S_vector normals[] = {S_vector(1.0f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, -1.0f), S_vector(-0.6f, 0.0f, 0.8f),
                                               S_vector(0.0f, 1.0f, 0.0f)};
            *n = normals[NextFake() % 4];
        }
        static uint8_t volume[2][0x80];
        Name(volume[g_side], "sphere volume");
        return Ptr(volume[g_side]);
    };
}

// ---- the original's submarines ----
constexpr uint32_t O_SB_CTOR = 0x005df000, O_SB_DTOR = 0x005df2e0, O_SB_SET_FRAME = 0x005dee00, O_SB_READ = 0x005dfe50,
                   O_SB_START = 0x005df3d0, O_SB_STOP = 0x005dfca0, O_SB_RESTART = 0x005de7c0, O_SB_SAVE = 0x005de830,
                   O_SB_LOAD = 0x005dea30, O_SB_IS_BLOCKED = 0x005e0270, O_SB_SET_BLOCKED = 0x005e0290,
                   O_SB_SET_OCCUPANT = 0x005e0320, O_SB_GET_OCCUPANT = 0x005e0410, O_SB_FIND_SEAT = 0x005e0430,
                   O_SB_LEAVE = 0x005e0460, O_SB_EXIT = 0x005e04f0, O_SB_SEAT_COUNT = 0x005e0c90, O_SB_SEAT_FLAGS = 0x005e0ca0,
                   O_SB_SEAT_CAMERA = 0x005e0cc0, O_SB_SEAT_TEXT = 0x005e02c0, O_SB_FIND_EXIT = 0x005e0530,
                   O_SB_CONTROLS = 0x005e0dd0, O_SB_UPDATE = 0x005e0e20, O_SB_TICK = 0x005e0e00;
constexpr uint32_t O_G_SUBMARINES = 0x008ac9a8, O_SB_CREATOR = 0x008ac9b8;

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
std::string Names(const std::vector<uint32_t> &v)
{
    std::string s;
    for (uint32_t p : v)
        s += " " + NameOf(p);
    return s;
}
// A hull box (S_FrameVolume: the same layout on both sides).
std::string BoxText(uint32_t b)
{
    if (!b)
        return "box null";
    const S_FrameVolume &v = *reinterpret_cast<const S_FrameVolume *>(b);
    return F("box flags %x type %x frame %s actor %s radius ", v.volume.flags, v.volume.type, NameOf(Ptr(v.volume.frame)).c_str(),
             NameOf(Ptr(v.volume.actor)).c_str()) +
           Fl(v.volume.radius) + " centre " + V3(&v.volume.center.x) + " bbox " + V3(&v.volume.bbox.min.x) + " " +
           V3(&v.volume.bbox.max.x) + " inv " + VectorText(Ptr(&v.invMatrix), 16);
}
std::vector<std::string> SubState(int i)
{
    std::vector<std::string> out;
    uint8_t *b = g_sub[g_side][i];
    if (g_side) {
        // (The body's position's w without a body: a stack word in the original, never read.)
        out.push_back(F("sub%d pos ", i) + V4(FP(b, 0x30)) + " dir " + V4(FP(b, 0x40)) + " body pos " +
                      (At<uint32_t>(b, 0xB0) ? V4(FP(b, 0x90)) : V3(FP(b, 0x90))) +
                      F(" body %s propeller %s", NameOf(At<uint32_t>(b, 0xB0)).c_str(), NameOf(At<uint32_t>(b, 0xB4)).c_str()));
        for (int s = 0; s < C_Submarine::SEAT_COUNT; ++s) {
            const uint8_t *x = b + 0xB8 + 0x10 * s;
            out.push_back(F("  seat %d blocked %d occupant %s frame %s flags %x", s, int(x[0]), NameOf(At<uint32_t>(x, 4)).c_str(),
                            NameOf(At<uint32_t>(x, 8)).c_str(), At<uint32_t>(x, 0xC)));
        }
        out.push_back("  level " + Fl(At<float>(b, 0xD8)) +
                      F(" sounds %d %d %d %d %d bob ", At<int>(b, 0xDC), At<int>(b, 0xE0), At<int>(b, 0xE4), At<int>(b, 0xE8),
                        At<int>(b, 0xEC)) +
                      Fl(At<float>(b, 0xF0)) + " speed " + Fl(At<float>(b, 0xF4)) + " turn " + Fl(At<float>(b, 0xF8)) + " climb " +
                      Fl(At<float>(b, 0xFC)) + " controls " + V3(FP(b, 0x100)) +
                      F(" active %d camera %s", int(b[0x10C]), NameOf(At<uint32_t>(b, 0x110)).c_str()));
        for (uint32_t v : OVec(b, 0xA0))
            out.push_back("  " + BoxText(v));
        return out;
    }
    const C_Submarine *p = PortSub(i);
    out.push_back(F("sub%d pos ", i) + V4(&p->m_position.x) + " dir " + V4(&p->m_direction.x) + " body pos " +
                  (p->m_body ? V4(&p->m_bodyPos.x) : V3(&p->m_bodyPos.x)) +
                  F(" body %s propeller %s", NameOf(Ptr(p->m_body)).c_str(), NameOf(Ptr(p->m_propeller)).c_str()));
    for (int s = 0; s < C_Submarine::SEAT_COUNT; ++s) {
        const auto &x = p->m_seats[s];
        out.push_back(F("  seat %d blocked %d occupant %s frame %s flags %x", s, int(x.blocked), NameOf(Ptr(x.occupant)).c_str(),
                        NameOf(Ptr(x.frame)).c_str(), x.flags));
    }
    out.push_back("  level " + Fl(p->m_maxWaterLevel) +
                  F(" sounds %d %d %d %d %d bob ", p->m_engineSound, p->m_diveSound, p->m_riseSound, p->m_surfaceSound,
                    p->m_underwaterSound) +
                  Fl(p->m_bob) + " speed " + Fl(p->m_speed) + " turn " + Fl(p->m_turn) + " climb " + Fl(p->m_climb) + " controls " +
                  V3(&p->m_forward) + F(" active %d camera %s", int(p->m_volumesActive), NameOf(Ptr(p->m_camera)).c_str()));
    for (const S_FrameVolume *v : p->m_volumes)
        out.push_back("  " + BoxText(Ptr(v)));
    return out;
}
std::vector<std::string> SubsState()
{
    std::vector<std::string> out;
    for (int i = 0; i < g_subCount; ++i)
        for (const std::string &s : SubState(i))
            out.push_back(s);
    std::vector<uint32_t> list;
    if (g_side)
        list = OVec(OriginalAt<uint8_t *>(O_G_SUBMARINES), 0);
    else
        for (C_Actor *a : GetSubmarines())
            list.push_back(Ptr(a));
    out.push_back("list" + Names(list));
    // The models' and bodies' local places (the bobbing; restored at the stop), the humans' places (the exits).
    for (int i = 0; i < g_subCount; ++i) {
        const I3D_frame *body = reinterpret_cast<const I3D_frame *>(g_side ? At<uint32_t>(g_sub[1][i], 0xB0) : Ptr(PortSub(i)->m_body));
        out.push_back(F("model%d at ", i) + VectorText(Ptr(&AsFrame(g_subModel[g_side][i])->m_localMatrix.m[3][0]), 3) + " body at " +
                      (body ? VectorText(Ptr(&body->m_localMatrix.m[3][0]), 3) + " rot " + VectorText(Ptr(&body->m_rot), 4) : "-"));
    }
    std::string humans = "humans";
    for (int h : W().humans) {
        // (The original's at +0x30 and +0x40, the port's members; the w's: stack words in the original's exits.)
        const uint8_t *a = W().actors[size_t(h)].bytes;
        const C_Actor *x = reinterpret_cast<const C_Actor *>(a);
        humans += " " + VectorText(g_side ? Ptr(a + 0x30) : Ptr(&x->GetPosition4()), 3) + " " +
                  VectorText(g_side ? Ptr(a + 0x40) : Ptr(&x->GetDirection4()), 3);
    }
    out.push_back(humans);
    return out;
}
std::vector<std::string> SbFrameStates()
{
    std::vector<std::string> out;
    for (const S_FakeFrame &f : W().frames) {
        const I3D_frame *x = AsFrame(f.bytes);
        out.push_back(F("frame %s ref %d flags %08x pos ", NameOf(Ptr(f.bytes)).c_str(), int(x->m_refCount), x->m_frameFlags) +
                      VectorText(Ptr(&x->m_localMatrix.m[3][0]), 4) + " rot " + VectorText(Ptr(&x->m_rot), 4) + " parent " +
                      NameOf(Ptr(x->m_parent)) + " name " + (x->m_name ? x->m_name : ""));
    }
    return out;
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
enum { OP_UPDATE, OP_TICK, OP_CONTROLS, OP_SEAT, OP_FIND_EXIT, OP_SAVE_LOAD, OP_RESTART, OP_STOP_START, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"Update", "Tick", "Controls", "Seat", "FindExit", "SaveLoad", "Restart", "StopStart"};
struct S_SbOp {
    int kind = 0;
    int sub = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_SbSub {
    std::string props;
    bool readProps = false;
    bool started = true;
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    int odd = 0;                                        // (1..8: its z or x axis tiny, zero or vertical)
};
struct S_SbCase {
    std::vector<S_SbSub> subs;
    std::vector<S_SbOp> ops;
    uint32_t role = GAME_ROLE_LOCAL;
    int time = 0;
};
int g_sbCases, g_opCounts[OP_KINDS];
const int FRAME_TIMES[] = {0, 10, 16, 33, 100, 250, 1000, 3000};
S_SbCase RandomSbCase()
{
    S_SbCase c;
    const int subs = P(0.8) ? 1 : 2;
    for (int i = 0; i < subs; ++i) {
        S_SbSub s;
        s.readProps = P(0.7);
        if (s.readProps) {
            // The version first (2; another; a short one), then the level (or another entry), the level again.
            S_Writer w;
            w.Begin(0xAE24);
            const int v = I(0, 9);
            if (v == 0) {
                w.Begin(0);
                w.Raw("\x02\x00", 2);
                w.End();
            } else if (v == 1)
                w.EntryOf(1, 2);
            else
                w.EntryOf(0, v == 2 ? 3 : 2);
            for (int k = I(0, 3); k > 0; --k) {
                if (P(0.15)) {
                    w.Begin(1);
                    w.Raw("\x01\x00", 2);
                    w.End();
                } else if (P(0.2)) {
                    // (Another id, entered and not left: a level inside it is read. Not a bare one at the end: the
                    // original's header read fails there and it loops for ever.)
                    w.Begin(uint16_t(I(2, 9)));
                    w.EntryOf(1, 42.0f);
                    w.End();
                } else {
                    static const float levels[] = {0.0f, -5.0f, 3.5f, 100.0f, 20.0f};
                    w.EntryOf(1, levels[I(0, 4)]);
                }
            }
            w.End();
            s.props = w.out;
        }
        s.started = P(0.95);
        s.x = float(I(-50, 50));
        s.y = float(I(-20, 5));
        s.z = float(I(-50, 50));
        s.heading = float(I(0, 628)) * 0.01f;
        s.odd = P(0.15) ? I(1, 8) : 0;
        c.subs.push_back(s);
    }
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    c.time = I(0, 100000);
    const int count = P(0.1) ? I(60, 160) : I(4, 40);
    for (int i = 0; i < count; ++i) {
        S_SbOp op;
        const int r = I(0, 99);
        op.kind = r < 35 ? OP_UPDATE : r < 45 ? OP_TICK : r < 60 ? OP_CONTROLS : r < 80 ? OP_SEAT : r < 85 ? OP_FIND_EXIT
                : r < 92 ? OP_SAVE_LOAD : r < 95 ? OP_RESTART : OP_STOP_START;
        op.sub = I(0, subs - 1);
        op.a = I(0, 1000);
        op.b = I(-1, 2);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    return c;
}

void MakeSubs()
{
    for (int i = 0; i < g_subCount; ++i) {
        uint8_t *b;
        if (g_side) {
            b = Block(0x130);
            OriginalAt<T_Void>(O_SB_CTOR)(b);
            // (The sound slots as the port's start: the original's constructor leaves them as its allocation had them,
            // so a stop before the first start stops whatever slots those are.)
            for (uint32_t off = 0xDC; off <= 0xEC; off += 4)
                Mut<int>(b, off) = -1;
        } else {
            b = Block(sizeof(C_Submarine) + 16);
            new (b) C_Submarine();
        }
        g_sub[g_side][i] = b;
        Name(b, F("sub %d", i));
    }
}
void AdvanceTime(int t)
{
    if (g_side)
        Mut<uint32_t>(g_origGame, 0x2C) += uint32_t(t);
    else
        reinterpret_cast<C_game *>(g_portGameBuf)->m_time += uint32_t(t);
}
uint8_t *HumanActor(int n)
{
    const auto &humans = W().humans;
    return humans.empty() ? nullptr : W().actors[size_t(humans[size_t(n) % humans.size()])].bytes;
}
// Whether the seat has its frame (a submarine without seat frames: the original reads through null in the seats' calls
// that use it).
bool HasSeatFrame(int i, int seat)
{
    if (seat < 0 || seat >= C_Submarine::SEAT_COUNT)
        return true;
    return g_side ? At<uint32_t>(g_sub[1][i], 0xB8 + 0x10 * seat + 8) != 0 : PortSub(i)->m_seats[seat].frame != nullptr;
}

using T_SbInt = void(TC *)(void *, int);
using T_SbChunk = bool(TC *)(void *, void *);
std::string RunSbOp(const S_SbOp &op, int index)
{
    const bool o = g_side != 0;
    const int si = op.sub < g_subCount ? op.sub : 0;
    uint8_t *b = g_sub[g_side][si];
    C_Submarine *p = PortSub(si);
    const int t = FRAME_TIMES[op.a % 8];
    switch (op.kind) {
    case OP_UPDATE:
        AdvanceTime(t);
        if (o)
            OriginalAt<T_SbInt>(O_SB_UPDATE)(b, t);
        else
            p->Update(t);
        if (std::getenv("HD2_SB_BITS")) {
            const float *f = o ? FP(b, 0xF4) : &p->m_speed;
            const float *y = o ? FP(b, 0x34) : &p->m_position.y;
            const float *l = o ? FP(b, 0xD8) : &p->m_maxWaterLevel;
            uint32_t w[3];
            std::memcpy(&w[0], f, 4);
            std::memcpy(&w[1], y, 4);
            std::memcpy(&w[2], l, 4);
            std::printf("bits %s case %d speed %08x y %08x level %08x\n", SIDE_NAMES[g_side], g_sbCases, w[0], w[1], w[2]);
        }
        return F("Update %d %d", si, t);
    case OP_TICK:
        AdvanceTime(t);
        if (o)
            OriginalAt<T_SbInt>(O_SB_TICK)(b, t);
        else
            p->Tick(t);
        return F("Tick %d %d", si, t);
    case OP_CONTROLS: {
        static const float values[] = {0.0f, 1.0f, -1.0f, 0.5f, -0.25f, 3.0f};
        const float f = values[op.a % 6], s = values[op.c % 6], u = values[op.d % 6];
        if (o)
            OriginalAt<void(TC *)(void *, float, float, float)>(O_SB_CONTROLS)(b, f, s, u);
        else
            p->SetControls(f, s, u);
        return F("SetControls %d ", si) + Fl(f) + " " + Fl(s) + " " + Fl(u);
    }
    case OP_SEAT: {
        const int seat = op.b;
        const int which = op.c % 12;
        uint8_t *human = HumanActor(op.a);
        switch (which) {
        case 0: {
            const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(O_SB_IS_BLOCKED)(b, seat) : p->IsSeatBlocked(seat);
            return F("IsSeatBlocked %d -> %d", seat, int(r));
        }
        case 1: {
            const bool blocked = op.d & 1;
            const bool r = o ? OriginalAt<bool(TC *)(void *, int, int)>(O_SB_SET_BLOCKED)(b, seat, blocked)
                             : p->SetSeatBlocked(seat, blocked);
            return F("SetSeatBlocked %d %d -> %d", seat, int(blocked), int(r));
        }
        case 2:
        case 3: {
            uint8_t *who = which == 2 || op.d % 4 ? human : nullptr;
            if (o)
                OriginalAt<void(TC *)(void *, int, void *)>(O_SB_SET_OCCUPANT)(b, seat, who);
            else
                p->SetSeatOccupant(seat, reinterpret_cast<C_Actor *>(who));
            return F("SetSeatOccupant %d %s", seat, NameOf(Ptr(who)).c_str());
        }
        case 4: {
            const uint32_t r = o ? Ptr(OriginalAt<void *(TC *)(void *, int)>(O_SB_GET_OCCUPANT)(b, seat)) : Ptr(p->GetSeatOccupant(seat));
            return F("GetSeatOccupant %d -> %s", seat, NameOf(r).c_str());
        }
        case 5: {
            uint8_t *who = op.d % 3 ? human : nullptr;
            const int r = o ? OriginalAt<int(TC *)(void *, const void *)>(O_SB_FIND_SEAT)(b, who)
                            : p->FindSeat(reinterpret_cast<C_Actor *>(who));
            return F("FindSeat %s -> %d", NameOf(Ptr(who)).c_str(), r);
        }
        case 6: {
            const uint32_t r = o ? Ptr(OriginalAt<void *(TC *)(void *, int)>(O_SB_LEAVE)(b, seat)) : Ptr(p->LeaveSeat(seat));
            return F("LeaveSeat %d -> %s", seat, NameOf(r).c_str());
        }
        case 7: {
            // (The seats' frames: the exit reads them unchecked in both programs.)
            if (!HasSeatFrame(si, 0) || !HasSeatFrame(si, 1))
                return "ExitSeat (no seat frames)";
            // (Mostly with someone seated first: the exits' fallbacks, every way out blocked.)
            std::string seated;
            if (op.d % 3 && seat >= 0 && seat < C_Submarine::SEAT_COUNT && human) {
                if (o)
                    OriginalAt<void(TC *)(void *, int, void *)>(O_SB_SET_OCCUPANT)(b, seat, human);
                else
                    p->SetSeatOccupant(seat, reinterpret_cast<C_Actor *>(human));
                seated = " seated " + NameOf(Ptr(human));
            }
            const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(O_SB_EXIT)(b, seat) : p->ExitSeat(seat);
            return F("ExitSeat %d -> %d", seat, int(r)) + seated;
        }
        case 8: {
            const uint32_t r = o ? OriginalAt<uint32_t(TC *)(void *, int)>(O_SB_SEAT_FLAGS)(b, seat) : p->GetSeatFlags(seat);
            const int n = o ? OriginalAt<int(TC *)(void *)>(O_SB_SEAT_COUNT)(b) : p->GetSeatCount();
            return F("GetSeatFlags %d -> %x count %d", seat, r, n);
        }
        case 9: {
            if (!HasSeatFrame(si, seat))
                return "GetSeatCamera (no seat frame)";
            const uint32_t r = o ? Ptr(OriginalAt<void *(TC *)(void *, int)>(O_SB_SEAT_CAMERA)(b, seat)) : Ptr(p->GetSeatCamera(seat));
            return F("GetSeatCamera %d -> %s", seat, NameOf(r).c_str());
        }
        default: {
            if (!HasSeatFrame(si, seat))
                return "GetSeatText (no seat frame)";
            const int r = o ? OriginalAt<int(TC *)(void *, int)>(O_SB_SEAT_TEXT)(b, seat) : p->GetSeatText(seat);
            return F("GetSeatText %d -> %d", seat, r);
        }
        }
    }
    case OP_FIND_EXIT: {
        const int seat = op.b;
        if (!HasSeatFrame(si, seat))
            return "FindExit (no seat frame)";
        alignas(16) S_vector4 pos = {7.0f, 7.0f, 7.0f, 7.0f}, dir = {8.0f, 8.0f, 8.0f, 8.0f};
        const bool other = op.d & 1;
        const bool r = o ? OriginalAt<bool(TC *)(void *, int, S_vector4 *, S_vector4 *, int)>(O_SB_FIND_EXIT)(b, seat, &pos, &dir, other)
                         : p->FindExit(seat, &pos, &dir, other);
        // (The w's: the seat's or a stack word, a stack word in the original: x, y, z compared.)
        return F("FindExit %d %d -> %d ", seat, int(other), int(r)) + V3(&pos.x) + " " + V3(&dir.x);
    }
    case OP_SAVE_LOAD: {
        const std::string file = F("sb_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_SbChunk>(O_SB_SAVE)(b, &save) : p->SaveGame(save);
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
        // (Loaded back now and then (into this one or the other): its file, or a made one with an entry of another
        // kind (entered, not left) or a short one.)
        bool rl = false;
        const int load = op.c % 4;
        uint8_t *into = g_sub[g_side][(si + (op.d & 1)) % g_subCount];
        if (load) {
            std::string name = file;
            if (load >= 2) {
                S_Writer w;
                w.Begin(0x2000);
                // (The climb first now and then: what follows it is read too.)
                const bool climbFirst = load == 3 && (op.a & 1);
                if (climbFirst)
                    w.EntryOf(0xD05, 0.75f);
                w.EntryOf(0x457, float(op.a % 13 - 6));
                if (load == 2) {
                    // (Another id, entered and not left: the turn inside it is read, nothing after it.)
                    w.Begin(0x1234);
                    w.EntryOf(0x8AE, 0.25f);
                    w.End();
                }
                if (!climbFirst)
                    w.EntryOf(0xD05, -0.5f);
                w.End();
                name = F("sb_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_SbChunk>(O_SB_LOAD)(into, &chunk) : reinterpret_cast<C_Submarine *>(into)->LoadGame(chunk);
            chunk.Close();
        }
        return F("SaveGame %d -> %d LoadGame %d into %s -> %d bytes ", si, int(rs), load, NameOf(Ptr(into)).c_str(), int(rl)) + hex;
    }
    case OP_RESTART: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_SbInt>(O_SB_RESTART)(b, server);
        else
            p->RestartMission(server);
        return F("RestartMission %d %d", si, int(server));
    }
    default: {
        const bool server = op.c & 1;
        const int what = op.d % 3;                      // (0: stop and start; 1: stop; 2: start)
        if (what != 2) {
            if (o)
                OriginalAt<T_SbInt>(O_SB_STOP)(b, server);
            else
                p->StopMission(server);
        }
        if (what != 1) {
            if (o)
                OriginalAt<T_SbInt>(O_SB_START)(b, server);
            else
                p->StartMission(server);
        }
        return F("StopStart %d %d %d", si, what, int(server));
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
            std::printf("sb_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_sbShow;
void RunSbCase(const S_SbCase &c)
{
    g_caseStart = GetTickCount();
    ++g_sbCases;
    g_holdFrees = true;
    g_current = F("case %d", g_sbCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    for (size_t i = 0; i < c.subs.size(); ++i)
        g_overrides[Lower(F("sb_props_%d.bin", int(i)))] = c.subs[i].props;
    g_subCount = int(c.subs.size());
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_world[side].frames.reserve(65536);
        g_texts[side].clear();
        g_slotSounds[side].clear();
        BuildWorld(side, worldSeed);
        uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
        for (int i = 0; i < g_subCount; ++i) {
            const S_SbSub &sc = c.subs[size_t(i)];
            uint8_t *model = Child(sector, FRAME_MODEL, F("submarine%d", i), nullptr);
            BuildSubmarine(model);
            S_matrix &m = AsFrame(model)->m_localMatrix;
            m.Identity();
            m.m[0][0] = std::cos(sc.heading);
            m.m[0][2] = -std::sin(sc.heading);
            m.m[2][0] = std::sin(sc.heading);
            m.m[2][2] = std::cos(sc.heading);
            if (sc.odd) {
                static const float rows[8][3] = {{1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                                 {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1e-5f},
                                                 {0.0f, 0.0f, -1e-5f}, {0.0f, 0.0f, 1e-5f}};
                const int row = sc.odd <= 3 || sc.odd >= 7 ? 2 : 0;
                for (int k = 0; k < 3; ++k)
                    m.m[row][k] = rows[sc.odd - 1][k];
            }
            m.m[3][0] = sc.x;
            m.m[3][1] = sc.y;
            m.m[3][2] = sc.z;
            g_subModel[side][i] = model;
        }
        if (side) {
            Mut<uint32_t>(g_origGame, 0x2C) = uint32_t(c.time);
            uint8_t *list = OriginalAt<uint8_t *>(O_G_SUBMARINES);
            Mut<uint32_t>(list, 8) = Mut<uint32_t>(list, 4);
        } else {
            reinterpret_cast<C_game *>(g_portGameBuf)->m_time = uint32_t(c.time);
        }
        SetRole(c.role);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        static const int trace = std::getenv("HD2_SB_TRACE") ? std::atoi(std::getenv("HD2_SB_TRACE")) : -1;
        const auto step = [&](const char *what) {
            if (trace == g_sbCases)
                std::printf("trace %s %s\n", SIDE_NAMES[side], what);
        };
        step("make");
        MakeSubs();
        step("made");
        for (int i = 0; i < g_subCount; ++i) {
            const S_SbSub &sc = c.subs[size_t(i)];
            uint8_t *b = g_sub[side][i];
            C_Submarine *p = PortSub(i);
            // The frame (the base's SetFrame is logged: the frame set here first, as it would).
            if (side) {
                Mut<uint8_t *>(b, 0x20) = g_subModel[1][i];
                out.push_back(F("SetFrame %d -> %d", i, int(OriginalAt<bool(TC *)(void *, void *)>(O_SB_SET_FRAME)(b, g_subModel[1][i]))));
            } else {
                p->m_frame = AsFrame(g_subModel[0][i]);
                out.push_back(F("SetFrame %d -> %d", i, int(p->SetFrame(AsFrame(g_subModel[0][i])))));
            }
            step("properties");
            if (sc.readProps) {
                C_chunk props;
                props.Open(F("sb_props_%d.bin", i).c_str(), false);
                uint16_t id;
                props.Descend(id);
                const bool ok = side ? OriginalAt<T_SbChunk>(O_SB_READ)(b, &props) : p->ReadProperties(props);
                props.Close();
                out.push_back(F("ReadProperties %d -> %d", i, int(ok)));
            }
            step("start");
            if (sc.started) {
                const bool server = (c.role & GAME_ROLE_SERVER) != 0;
                if (side)
                    OriginalAt<T_SbInt>(O_SB_START)(b, server);
                else
                    p->StartMission(server);
            }
        }
        for (const std::string &s : SubsState())
            out.push_back(s);
        step("ops");
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_sbCases)
                std::printf("trace %s op %d kind %s\n", SIDE_NAMES[side], int(i), OP_KIND_NAMES[c.ops[i].kind]);
            out.push_back(RunSbOp(c.ops[i], int(i)));
            // (The operation's calls with it.)
            for (const std::string &s : g_log[side])
                out.push_back("  call " + MaskSoundW(s));
            g_log[side].clear();
            for (const std::string &s : SubsState())
                out.push_back("  " + s);
        }
        // The end: stopped and destroyed (every third case destroyed as it is: the destructor's own reset).
        const bool stopFirst = g_sbCases % 3 != 0;
        for (int i = 0; i < g_subCount; ++i) {
            uint8_t *b = g_sub[side][i];
            if (side) {
                if (stopFirst)
                    OriginalAt<T_SbInt>(O_SB_STOP)(b, 1);
                OriginalAt<T_Void>(O_SB_DTOR)(b);
            } else {
                if (stopFirst)
                    PortSub(i)->StopMission(true);
                PortSub(i)->~C_Submarine();
            }
        }
        // The creator: a submarine made and destroyed; the list's getter.
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(0x005de6c0)(OriginalAt<void *>(O_SB_CREATOR));
            Name(made, "created sub");
            out.push_back("created, list" + Names(OVec(OriginalAt<uint8_t *>(O_G_SUBMARINES), 0)) +
                          F(" getter %d", int(Ptr(OriginalAt<void *(*)()>(0x005de7b0)()) == OriginalAt<uint32_t>(O_G_SUBMARINES))));
            OriginalAt<void(TC *)(void *, void *)>(0x005de730)(OriginalAt<void *>(O_SB_CREATOR), made);
            out.push_back("destroyed, list" + Names(OVec(OriginalAt<uint8_t *>(O_G_SUBMARINES), 0)));
        } else {
            C_ActorCreator *creator = g_actorCreators[ACTOR_SUBMARINE];
            C_Actor *made = creator->Create();
            Name(made, "created sub");
            std::vector<uint32_t> list;
            for (C_Actor *a : GetSubmarines())
                list.push_back(Ptr(a));
            out.push_back("created, list" + Names(list) + " getter 1");
            creator->Destroy(made);
            list.clear();
            for (C_Actor *a : GetSubmarines())
                list.push_back(Ptr(a));
            out.push_back("destroyed, list" + Names(list));
        }
        g_case = "";
        for (const std::string &s : SbFrameStates())
            out.push_back(s);
        for (const std::string &s : g_log[side])
            out.push_back("call " + MaskSoundW(s));
        for (int i = 0; i < MAX_SUBS; ++i)
            g_sub[side][i] = nullptr;
    }
    if (g_sbShow) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_sbShow == 2)
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

void SetUpSubmarine()
{
    SetUpCmd();
    SetUpSubmarineAnswers();
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
        {0x00465f00, reinterpret_cast<const void *>(&O_KeeperCtor)},
        {0x00466010, reinterpret_cast<const void *>(&O_KeeperRestore)},
        {0x00466110, reinterpret_cast<const void *>(&O_KeeperUpdate)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    // The original's list: empty, its storage the original's heap.
    uint8_t *list = OriginalAt<uint8_t *>(O_G_SUBMARINES);
    uint8_t *store = static_cast<uint8_t *>(O_New(4 * 64));
    Mut<uint8_t *>(list, 4) = store;
    Mut<uint8_t *>(list, 8) = store;
    Mut<uint8_t *>(list, 0xC) = store + 4 * 64;
}

}  // namespace H

// (A call into data: the caller's return address, from the stack.)
LONG WINAPI CallIntoData(EXCEPTION_POINTERS *e)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->ExceptionInformation[1] == at) {
        const uint32_t *sp = reinterpret_cast<const uint32_t *>(e->ContextRecord->Esp);
        std::printf("call into data at %08x from %08x %08x %08x ecx %08x\n", unsigned(at), sp[0], sp[1], sp[2],
                    unsigned(e->ContextRecord->Ecx));
        std::fflush(stdout);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int main()
{
    using namespace H;
    if (std::getenv("HD2_SB_CALLS"))
        AddVectoredExceptionHandler(1, CallIntoData);
    std::puts("sb_diff: main");
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
        std::printf("sb_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_SB_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_SB_SHOW"))
        g_sbShow = std::atoi(s) ? std::atoi(s) : 1;
    g_dump = std::getenv("HD2_SB_DUMP") != nullptr;
    int cases = std::getenv("HD2_SB_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_SB_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpSubmarine();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunSbCase(RandomSbCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("sb_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_sbCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
