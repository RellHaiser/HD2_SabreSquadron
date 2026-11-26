// Train part 1: the trains (game/train.cpp, the original's 0x005bbdb0..0x005c0ce0) against the original executable's.
// cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the train's calls go through tr_calls.s /
// tr_calls.inc (gen_cmd_calls.py tr). The track player is each side's own (game/track.cpp linked; the original's code
// on the original's tracks, made here by its own AddKey); the sound cache runs on both sides; the LS3DF math is
// LS3DF.dll's on both sides.
//
// Each case makes one or two tracks (keys along a bending line, their speeds; on each side in its own layout), a
// checkpoint network along them (points near some keys, linked on, branches), the train's wagons (models with their
// bodies, wheels on each side with rods, connecting rods and crossheads, hull boxes; some missing) and a train on each
// side (the original's by its constructor 0x005bcfb0 in a block of 0x8260, the port's by its own): SetFrame, the
// properties (the version, the wagons by name with their lengths and gaps, unknown entries), the mission started; then
// the same random operations: updates, SetTrack (a track, another, one not there, off), Start (the whistle), Move
// (forced or not), saves and loads (its own file, made ones), restarts, stops and starts. The sweeps answer nothing or a
// human now and then. After each operation the answer, the train's state (every field, its wagons, wheels, chuff sets,
// path and track player in each side's layout), the checkpoints' flags and the operation's calls are compared; then
// the world's frames.
// MinGW; build_tr.bat; runs tr_diff.exe ("tr_diff: N checks, 0 failures"). Env: HD2_TR_CASES (default 1,500;
// HD2_TR_QUICK: 300), HD2_TR_SEED, HD2_TR_SHOW (1: the port's lines; 2: both), HD2_TR_ONLY (one case shown), HD2_TR_DUMP.
#define HARNESS_NAME "tr_diff"
#define CMD_CALLS_INC "tr_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/checkpoints.h"
#include "game/collision.h"
#include "game/frame_volume.h"
#include "game/sound_module.h"
#include "game/track.h"
#include "game/train.h"
#include "ls3d/igraph.h"
#undef protected
#undef private

// ---- the port's side of what the train calls and the test provides ----
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
S_matrix __stdcall RotateXMatrix(float angle)
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(S_matrix *, float)>("?RotateXMatrix@@YG?AUS_matrix@@M@Z");
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
void __stdcall S_matrix::SetDir3(const S_vector &dir, const S_vector &up)
{
    static const auto f =
        Ls3dfFn<void(__stdcall *)(S_matrix *, const S_vector &, const S_vector &)>("?SetDir3@S_matrix@@QAGXABUS_vector@@0@Z");
    f(this, dir, up);
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
S_vector __stdcall S_quat::GetDir() const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, float *)>("?GetDir@S_quat@@QBG?AUS_vector@@XZ");
    alignas(16) float r[4] = {};
    f(this, r);
    return S_vector(r[0], r[1], r[2]);
}
S_quat __stdcall S_quat::Slerp(const S_quat &q, float t, bool shortest) const
{
    static const auto f =
        Ls3dfFn<void *(__stdcall *)(const S_quat *, S_quat *, const S_quat &, float, bool)>("?Slerp@S_quat@@QBG?AU1@ABU1@M_N@Z");
    S_quat r;
    f(this, &r, q, t, shortest);
    return r;
}
// (The test's points: nothing to free.)
S_Checkpoint::~S_Checkpoint() {}
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
// The actors' creators (the trains' registers itself before main: a table of the test's).
static C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
C_ActorCreator **g_actorCreators = g_creatorTable;
// The base's virtuals the train's vtable holds but the test never reaches.
void C_Actor::Init() {}
void C_Actor::Done() {}
void C_Actor::CopyFrom(const C_Actor *) {}
int C_Actor::GetSeatAmmoCount(int) { return 0; }
void C_Actor::GetBoxCorners(S_vector4 *) {}
// The module base (the track module's: its name and id; not logged: the port's global module is made before the logger).
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
void Place(uint8_t *f, float x, float y, float z)
{
    S_matrix &m = AsFrame(f)->m_localMatrix;
    m.m[3][0] = x;
    m.m[3][1] = y;
    m.m[3][2] = z;
}

// ---- the tracks (each side's: the port's in its track module, the original's blocks found by name) ----
struct S_TrKey {
    S_vector pos;
    float speed;
};
struct S_TrTrack {
    std::string name;
    std::vector<S_TrKey> keys;
};
std::vector<uint8_t *> g_origTracks;                    // (the original's tracks, 0x28 bytes each, the case's)
std::vector<std::string> g_trackNames;
// The original's FindTrack (0x00401ed0, thiscall on its track module): the case's original tracks by name (any case).
void *OT O_FindTrack(void *, const char *name, int *outIndex)
{
    for (size_t i = g_trackNames.size(); i-- > 0;)
        if (_stricmp(name, g_trackNames[i].c_str()) == 0) {
            if (outIndex)
                *outIndex = int(i);
            return g_origTracks[i];
        }
    if (outIndex)
        *outIndex = -1;
    return nullptr;
}
using T_AddKey = int(TC *)(void *, const S_vector *, const S_quat *, float);
void MakeTracks(const std::vector<S_TrTrack> &tracks)
{
    // The port's: its track module's list.
    for (C_Track *t : g_trackModule.m_tracks)
        delete t;
    g_trackModule.m_tracks.clear();
    g_origTracks.clear();
    g_trackNames.clear();
    const S_quat rot(1.0f, 0.0f, 0.0f, 0.0f);
    for (const S_TrTrack &t : tracks) {
        auto *p = new C_Track(t.name.c_str());
        for (const S_TrKey &k : t.keys)
            p->AddKey(k.pos, rot, k.speed);
        g_trackModule.m_tracks.push_back(p);
        if (std::getenv("HD2_TR_TRACE"))
            std::puts("trace port track");
        // The original's (0x28 bytes: its name an MSVC 6 string, its keys a vector the original's AddKey fills).
        uint8_t *o = static_cast<uint8_t *>(O_New(0x30));
        std::memset(o, 0, 0x30);
        char *text = static_cast<char *>(O_New(t.name.size() + 2));
        text[0] = 0;                                    // (the string's reference count)
        std::memcpy(text + 1, t.name.c_str(), t.name.size() + 1);
        Mut<char *>(o, 4) = text + 1;
        Mut<uint32_t>(o, 8) = uint32_t(t.name.size());
        Mut<uint32_t>(o, 0xC) = uint32_t(t.name.size());
        // (Its keys: an array, +0x10 the keys, +0x14 the room, +0x18 the count, +0x1C the room added at a time, 2 as
        // the original's AddTrack makes it.)
        Mut<uint32_t>(o, 0x1C) = 2;
        Mut<float>(o, 0x20) = 0.1f;
        Mut<float>(o, 0x24) = 0.5f;
        for (const S_TrKey &k : t.keys)
            OriginalAt<T_AddKey>(0x00402880)(o, &k.pos, &rot, k.speed);
        g_origTracks.push_back(o);
        g_trackNames.push_back(t.name);
    }
}

// ---- the checkpoints (the same points on both sides: the network's Get / FindNearest answered) ----
std::vector<S_Checkpoint> *g_points[2];
std::vector<std::vector<S_CheckpointLink>> g_links[2];
struct S_TrPoint {
    S_vector pos;
    std::vector<int> links;
};
void MakePoints(const std::vector<S_TrPoint> &points)
{
    for (int side = 0; side < 2; ++side) {
        delete g_points[side];
        g_points[side] = new std::vector<S_Checkpoint>(points.size());
        g_links[side].assign(points.size(), {});
        for (size_t i = 0; i < points.size(); ++i) {
            S_Checkpoint &c = (*g_points[side])[i];
            c.pos = points[i].pos;
            for (int l : points[i].links)
                g_links[side][i].push_back({uint16_t(l), uint16_t(10)});
            c.flags = uint16_t(points[i].links.size());
            c.links = g_links[side][i].empty() ? nullptr : g_links[side][i].data();
        }
    }
}
std::string PointsText()
{
    std::string s = "points";
    if (g_points[g_side])
        for (const S_Checkpoint &c : *g_points[g_side])
            s += F(" %x", c.flags);
    return s;
}

// ---- the wagons' models ----
// A wagon model (by the world's random numbers: the same on both sides).
void BuildWagon(uint8_t *model, bool engine)
{
    // (A body always: the original turns a missing one through null in PlaceWagon and Move.)
    {
        const bool dummy = WI(0, 2) != 0;
        uint8_t *body = Child(model, dummy ? FRAME_DUMMY : FRAME_VISUAL, "BODY", model);
        Place(body, 0.0f, 1.0f + float(WI(0, 2)) * 0.25f, float(WI(-1, 1)));
        if (dummy) {
            float *box = reinterpret_cast<float *>(body + 0x120);
            const float v[8] = {-1.5f, -1.0f, -5.0f, 0.0f, 1.5f + 0.5f, 2.0f, 5.0f, 0.0f};
            std::memcpy(box, v, sizeof v);
        }
    }
    if (WI(0, 3) == 0) {
        uint8_t *hull = Child(model, FRAME_DUMMY, "BODYCOLB00", model);
        float *box = reinterpret_cast<float *>(hull + 0x120);
        const float v[8] = {-1.0f, -0.5f, -4.0f, 0.0f, 1.25f, 1.5f, 4.5f, 0.0f};
        std::memcpy(box, v, sizeof v);
    }
    // The wheels on each side, numbered from 0 (a gap now and then: the rest are not found); the engine's with rods.
    for (int side = 0; side < 2; ++side) {
        const int count = WI(0, 3);
        for (int n = 0; n < count + (WI(0, 4) == 0 ? 1 : 0); ++n) {
            if (n == count)                             // (one past a gap)
                ++n;
            const bool visual = WI(0, 7) != 0;
            uint8_t *wheel = Child(model, visual ? FRAME_VISUAL : FRAME_DUMMY, F(side ? "WHR%02d" : "WHL%02d", n), model);
            Place(wheel, side ? 1.0f : -1.0f, 0.5f, float(n) * 2.0f - 2.0f);
            if (visual) {
                const float r = 0.4f + float(WI(0, 3)) * 0.2f;
                float *bound = reinterpret_cast<float *>(wheel + 0x1A0);
                bound[1] = -r;
                bound[5] = r;
                Mut<uint32_t>(wheel, 0x1D0) |= 0x00020000u;   // (VISFLAGS_BOUND_VALID)
            }
            if (!engine || WI(0, 3) == 0)
                continue;
            if (WI(0, 3))
                Child(wheel, FRAME_DUMMY, F(side ? "THR%02d" : "THL%02d", n), model);
            if (WI(0, 3)) {
                uint8_t *rod = Child(wheel, FRAME_DUMMY, F(side ? "DTHR%02d" : "DTHL%02d", n), model);
                Place(rod, 0.0f, 0.2f + float(WI(0, 2)) * 0.1f, 0.1f);
                // (Its crosshead always: the original's UpdateWheels reads a missing one through null.)
                {
                    uint8_t *cross = Child(rod, FRAME_DUMMY, side ? "STHR00" : "STHL00", model);
                    Place(cross, 0.0f, float(WI(-1, 1)) * 0.1f, 2.0f + float(WI(0, 2)) * 0.5f);
                }
            }
        }
    }
}

std::map<uint32_t, uint8_t *> g_soundFrames[2];          // (the registered sounds' frames: the case's)
void SetUpTrainAnswers()
{
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        const std::string want = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        std::function<uint32_t(I3D_frame *)> find = [&](I3D_frame *p) -> uint32_t {
            for (I3D_frame *c = p->m_firstChild; c; c = c->m_nextSibling) {
                if (c->m_name && want == c->m_name && (a[2] & (1u << (c->m_type - 1))))
                    return Ptr(c);
                if (uint32_t d = find(c))
                    return d;
            }
            return 0u;
        };
        return find(reinterpret_cast<I3D_frame *>(a[0]));
    };
    g_answers["I3D_scene::FindFrame"] = [](const std::vector<uint32_t> &a) {
        return Ptr(FindByName(reinterpret_cast<const char *>(a[1])));
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
        c->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
        return 0u;
    };
    g_answers["I3D_frame::SetOn"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_frameFlags = (a[1] & 0xFF) ? (f->m_frameFlags | 1u) : (f->m_frameFlags & ~1u);
        return 0u;
    };
    g_answers["I3D_frame::SetPos"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[1]);
        f->m_localMatrix.m[3][0] = pos.x;
        f->m_localMatrix.m[3][1] = pos.y;
        f->m_localMatrix.m[3][2] = pos.z;
        f->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
        return 0u;
    };
    g_answers["I3D_driver::CreateFrame"] = [](const std::vector<uint32_t> &a) {
        return Ptr(Child(nullptr, int(a[1]), F("new%d", W().created++), nullptr));
    };
    // A hierarchy's box (the hulls without a dummy).
    g_answers["I3D_driver::GetHierarchyBBox"] = [](const std::vector<uint32_t> &a) {
        float *box = reinterpret_cast<float *>(a[1]);
        const float b[8] = {-1.5f, -0.5f, -6.0f, 0.0f, 1.5f, 3.0f, 6.0f, 0.0f};
        std::memcpy(box, b, sizeof b);
        return a[1];
    };
    g_answers["I3D_frame::UpdateBound"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        f->m_bound.radius = float(2 + NextFake() % 5);
        const float b[8] = {-1.0f, -1.0f, -3.0f, 0.0f, 1.5f, 2.0f, 3.5f, 0.0f};
        std::memcpy(&f->m_bound.bbox, b, sizeof b);
        f->m_frameFlags |= FRMFLAGS_BOUND_VALID;
        return 0u;
    };
    g_answers["ISND_driver::IsInitialized"] = [](const std::vector<uint32_t> &) { return uint32_t(NextFake() % 6 != 0); };
    // The registered sounds' frames (S_SoundFrame, 0x14 bytes: the sound, the sector flag, the sector, the base volume,
    // the factor 1).
    g_answers["C_SoundModule::RegisterSoundFrame"] = [](const std::vector<uint32_t> &a) {
        uint8_t *&f = g_soundFrames[g_side][a[1]];
        f = Block(0x20);
        Mut<uint32_t>(f, 0) = a[1];
        f[4] = uint8_t(a[2]);
        Mut<float>(f, 0x0C) = 1.0f;
        Mut<float>(f, 0x10) = 1.0f;
        Name(f, "sound frame of " + NameOf(a[1]));
        return Ptr(f);
    };
    // The sweeps (HitHuman): nothing mostly; a volume, a human's frame on it now and then.
    g_answers["CollisionSphereTest"] = [](const std::vector<uint32_t> &a) {
        if (std::getenv("HD2_TR_PC"))
            std::printf("pc %s %08x\n", SIDE_NAMES[g_side], unsigned(_controlfp(0, 0)));
        if (NextFake() % 4)
            return 0u;
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[0]);
        const S_vector &move = *reinterpret_cast<const S_vector *>(a[1]);
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[3]))
            *hit = S_vector(pos.x + move.x * 0.5f, pos.y + move.y * 0.5f, pos.z + move.z * 0.5f);
        if (S_vector *n = reinterpret_cast<S_vector *>(a[4]))
            *n = S_vector(0.0f, 1.0f, 0.0f);
        static uint8_t volume[2][0x80];
        std::memset(volume[g_side], 0, 0x80);
        S_CollisionVolume *v = reinterpret_cast<S_CollisionVolume *>(volume[g_side]);
        v->flags = (NextFake() % 4) << 6;
        const auto &humans = W().humans;
        if (!humans.empty() && NextFake() % 2)
            v->frame = AsFrame(*reinterpret_cast<uint8_t **>(W().actors[humans[NextFake() % humans.size()]].bytes +
                                                            (g_side ? 0x20 : offsetof(C_Actor, m_frame))));
        else if (NextFake() % 2)
            v->frame = AsFrame(W().frames[NextFake() % W().frames.size()].bytes);
        Name(volume[g_side], "sweep volume");
        return Ptr(volume[g_side]);
    };
    // The checkpoints: the case's points by index; the nearest within the distance (its index), else none.
    g_answers["C_CheckpointNetwork::Get"] = [](const std::vector<uint32_t> &a) {
        // (Its index's low half: the original pushes a word whose upper half is a leftover.)
        std::vector<S_Checkpoint> *p = g_points[g_side];
        const uint32_t index = a[1] & 0xFFFF;
        return p && index < p->size() ? Ptr(&(*p)[index]) : 0u;
    };
    g_answers["C_CheckpointNetwork::FindNearest"] = [](const std::vector<uint32_t> &a) {
        std::vector<S_Checkpoint> *p = g_points[g_side];
        float x, y, z, maxDist;
        std::memcpy(&x, &a[1], 4);
        std::memcpy(&y, &a[2], 4);
        std::memcpy(&z, &a[3], 4);
        std::memcpy(&maxDist, &a[4], 4);
        int best = CHECKPOINT_NONE;
        float bestSquare = maxDist * maxDist;
        if (p)
            for (size_t i = 0; i < p->size(); ++i) {
                const S_vector &q = (*p)[i].pos;
                const float sq = (q.x - x) * (q.x - x) + (q.y - y) * (q.y - y) + (q.z - z) * (q.z - z);
                if (sq < bestSquare) {
                    bestSquare = sq;
                    best = int(i);
                }
            }
        return uint32_t(best);
    };
    g_answers["S_Checkpoint::GetLink"] = [](const std::vector<uint32_t> &a) {
        const S_Checkpoint *c = reinterpret_cast<const S_Checkpoint *>(a[0]);
        return int(a[1]) >= 0 && int(a[1]) < c->GetLinkCount() ? Ptr(&c->links[a[1]]) : 0u;
    };
}

// ---- the original's train ----
constexpr uint32_t O_TR_CTOR = 0x005bcfb0, O_TR_DTOR = 0x005bd0f0, O_TR_START_WHISTLE = 0x005bc970, O_TR_SET_TRACK = 0x0041d590,
                   O_TR_UPDATE = 0x005bcf80, O_TR_SET_FRAME = 0x005bd1a0, O_TR_READ = 0x005be360, O_TR_START = 0x005bd820,
                   O_TR_STOP = 0x005be1b0, O_TR_RESTART = 0x005befe0, O_TR_SAVE = 0x005bf000, O_TR_LOAD = 0x005bf250,
                   O_TR_MOVE = 0x005bff10;
constexpr uint32_t O_SOUND_CACHE = 0x008aed64;
uint8_t *g_train[2];
C_Train *PortTrain() { return reinterpret_cast<C_Train *>(g_train[0]); }

// ---- the compared state ----
std::string V3(const float *v) { return VectorText(Ptr(v), 3); }
std::string V4(const float *v) { return VectorText(Ptr(v), 4); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
std::string Fl(float f) { return FloatText(f); }
std::vector<uint32_t> OVec(const uint8_t *p, uint32_t off, uint32_t stride = 4)
{
    std::vector<uint32_t> out;
    const uint32_t first = At<uint32_t>(p, off + 4), last = At<uint32_t>(p, off + 8);
    for (uint32_t a = first; a && a < last; a += stride)
        out.push_back(*reinterpret_cast<const uint32_t *>(a));
    return out;
}
std::string BoxText(uint32_t b)
{
    if (!b)
        return "box null";
    const S_FrameVolume &v = *reinterpret_cast<const S_FrameVolume *>(b);
    return F("box flags %x type %x frame %s radius ", v.volume.flags, v.volume.type, NameOf(Ptr(v.volume.frame)).c_str()) +
           Fl(v.volume.radius) + " centre " + V3(&v.volume.center.x) + " inv " + VectorText(Ptr(&v.invMatrix), 16);
}
std::string WagonText(const float *savedPos, const float *savedRot, const std::string &name, uint32_t frame, uint32_t body,
                      float length, float gap, uint32_t sound, uint32_t soundFrame, uint32_t volume)
{
    return "wagon " + name + " saved " + V4(savedPos) + " " + V4(savedRot) +
           F(" frame %s body %s length ", NameOf(frame).c_str(), NameOf(body).c_str()) + Fl(length) + " gap " + Fl(gap) +
           F(" sound %s %s ", NameOf(sound).c_str(), NameOf(soundFrame).c_str()) + BoxText(volume);
}
std::string ChuffText(const uint8_t *c)
{
    std::string s = "chuffs";
    for (int i = 0; i < 4; ++i)
        s += " " + NameOf(At<uint32_t>(c, 4 * i)) + "/" + NameOf(At<uint32_t>(c, 0x10 + 4 * i));
    return s + " min " + Fl(At<float>(c, 0x20)) + F(" count %d next %d", At<int>(c, 0x24), At<int>(c, 0x28));
}
std::string WheelText(uint32_t wheel, uint32_t rod, uint32_t connecting, uint32_t cross, float radius)
{
    return F("wheel %s rod %s connecting %s crosshead %s radius ", NameOf(wheel).c_str(), NameOf(rod).c_str(),
             NameOf(connecting).c_str(), NameOf(cross).c_str()) +
           Fl(radius);
}
std::string RepText(uint32_t rep) { return rep ? std::string(reinterpret_cast<const char *>(rep)) : std::string(); }
std::vector<std::string> TrainState()
{
    std::vector<std::string> out;
    uint8_t *b = g_train[g_side];
    if (g_side) {
        std::string s = "sounds";
        for (uint32_t off = 0x90; off <= 0xBC; off += 4)
            s += " " + NameOf(At<uint32_t>(b, off));
        out.push_back(s + F(" brake %d jar %d fade ", At<int>(b, 0xC0), At<int>(b, 0xC4)) + Fl(At<float>(b, 0xC8)) + " length " +
                      Fl(At<float>(b, 0xCC)));
        for (int k = 0; k < 4; ++k)
            out.push_back("  " + ChuffText(b + 0xD0 + 0x2C * k));
        std::string path = "path";
        // (Its points held through 8-byte blocks, the point first.)
        for (uint32_t h : OVec(b, 0x180))
            path += " " + NameOf(At<uint32_t>(reinterpret_cast<void *>(h), 0));
        out.push_back(path + F(" marked %d %d chuff set %d running %d started %d", At<int>(b, 0x190), At<int>(b, 0x194),
                               At<int>(b, 0x198), int(b[0x19C]), int(b[0x1B0])));
        for (uint32_t w : OVec(b, 0x1A0)) {
            const uint8_t *x = reinterpret_cast<const uint8_t *>(w);
            out.push_back("  " + WagonText(FP(x, 0), FP(x, 0x10), RepText(At<uint32_t>(x, 0x24)), At<uint32_t>(x, 0x30),
                                           At<uint32_t>(x, 0x34), At<float>(x, 0x38), At<float>(x, 0x3C), At<uint32_t>(x, 0x40),
                                           At<uint32_t>(x, 0x44), At<uint32_t>(x, 0x48)));
        }
        const uint8_t *player = b + 0x1C0;
        uint32_t track = At<uint32_t>(player, 0x48);
        out.push_back(F("player track %s key %d", track ? RepText(At<uint32_t>(reinterpret_cast<void *>(track), 4)).c_str() : "null",
                        At<int>(player, 0x4C)));
        for (uint32_t w : OVec(b, 0x8230)) {
            const uint8_t *x = reinterpret_cast<const uint8_t *>(w);
            out.push_back("  " + WheelText(At<uint32_t>(x, 0), At<uint32_t>(x, 4), At<uint32_t>(x, 8), At<uint32_t>(x, 0xC),
                                           At<float>(x, 0x10)));
        }
        out.push_back("crosshead " + Fl(At<float>(b, 0x8240)) + F(" time %u speed ", At<uint32_t>(b, 0x8244)) +
                      Fl(At<float>(b, 0x8248)) + " pos " + V4(FP(b, 0x30)) + " dir " + V4(FP(b, 0x40)));
    } else {
        const C_Train *t = PortTrain();
        std::string s = "sounds";
        for (const void *p : {static_cast<const void *>(t->m_steam), static_cast<const void *>(t->m_whistleNear),
                              static_cast<const void *>(t->m_whistleFar), static_cast<const void *>(t->m_brakeLoop),
                              static_cast<const void *>(t->m_brake), static_cast<const void *>(t->m_jar),
                              static_cast<const void *>(t->m_steamFrame), static_cast<const void *>(t->m_whistleNearFrame),
                              static_cast<const void *>(t->m_whistleFarFrame), static_cast<const void *>(t->m_brakeFrame),
                              static_cast<const void *>(t->m_jarFrame), static_cast<const void *>(t->m_brakeLoopFrame)})
            s += " " + NameOf(Ptr(p));
        out.push_back(s + F(" brake %d jar %d fade ", t->m_brakeTimer, t->m_jarTimer) + Fl(t->m_brakeFade) + " length " +
                      Fl(t->m_trainLength));
        for (int k = 0; k < 4; ++k)
            out.push_back("  " + ChuffText(reinterpret_cast<const uint8_t *>(&t->m_chuffSets[k])));
        std::string path = "path";
        for (const S_Checkpoint *p : t->m_path)
            path += " " + NameOf(Ptr(p));
        out.push_back(path + F(" marked %d %d chuff set %d running %d started %d", t->m_lastMarked, t->m_firstMarked, t->m_chuffSet,
                               int(t->m_running), int(t->m_started)));
        for (const auto *w : t->m_wagons)
            out.push_back("  " + WagonText(&w->savedPos.x, reinterpret_cast<const float *>(&w->savedRot), w->name, Ptr(w->frame), Ptr(w->body), w->length, w->gap,
                                           Ptr(w->wheelSound), Ptr(w->wheelSoundFrame), Ptr(w->volume)));
        const C_Track *track = t->m_player.GetTrack();
        out.push_back(F("player track %s key %d", track ? track->GetName() : "null", t->m_player.GetKeyIndex()));
        for (const auto &w : t->m_wheels)
            out.push_back("  " + WheelText(Ptr(w.wheel), Ptr(w.rod), Ptr(w.connectingRod), Ptr(w.crosshead), w.radius));
        out.push_back("crosshead " + Fl(t->m_crossheadHeight) + F(" time %u speed ", t->m_time) + Fl(t->m_speed) + " pos " +
                      V4(&t->m_position.x) + " dir " + V4(&t->m_direction.x));
    }
    out.push_back(PointsText());
    // The wagons' frames' world matrices.
    for (const S_FakeFrame &f : W().frames)
        if (f.name.compare(0, 5, "wagon") == 0)
            out.push_back(f.name + " world " + VectorText(Ptr(&AsFrame(f.bytes)->GetMatrix()), 16));
    return out;
}
std::vector<std::string> TrFrameStates()
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
    void Text(uint16_t id, const std::string &s)
    {
        Begin(id);
        Raw(s.c_str(), s.size() + 1);
        End();
    }
    template <class T> void EntryOf(uint16_t id, const T &v)
    {
        Begin(id);
        Raw(&v, sizeof v);
        End();
    }
};
enum { OP_UPDATE, OP_SET_TRACK, OP_WHISTLE, OP_MOVE, OP_SAVE_LOAD, OP_RESTART, OP_STOP_START, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"Update", "SetTrack", "Whistle", "Move", "SaveLoad", "Restart", "StopStart"};
struct S_TrOp {
    int kind = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_TrCase {
    std::vector<S_TrTrack> tracks;
    std::vector<S_TrPoint> points;
    int wagons = 1;                                     // (the train's models: wagon0 the engine, then wagon1..)
    std::string props;
    bool readProps = false;
    bool started = true;
    bool dummyFrame = false;                            // (the train's frame not a model: SetFrame refuses it)
    float trainX = 0.0f, trainZ = 0.0f;
    std::vector<S_TrOp> ops;
    uint32_t role = GAME_ROLE_LOCAL;
};
int g_trCases, g_opCounts[OP_KINDS];
const int FRAME_TIMES[] = {0, 1, 16, 33, 100, 250, 1000, 3000, 7};
S_TrCase RandomTrCase()
{
    S_TrCase c;
    // The tracks: keys along a bending line, their speeds.
    for (int t = 0; t < (P(0.7) ? 1 : 2); ++t) {
        S_TrTrack track;
        track.name = F("track%d", t + 1);
        float x = float(I(-50, 50)), z = float(I(-50, 50)), heading = float(I(0, 628)) * 0.01f;
        // (A key at least: the original's FollowTrack reads the first of none.)
        const int keys = P(0.1) ? 1 : I(2, 14);
        // (Now and then exact steps: a square of exactly 1.5 between the first keys, the next 5 m on.)
        if (P(0.06)) {
            static const float exact[][3] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 0.5f}, {6.0f, 0.5f, 0.5f}, {12.0f, 0.5f, 0.5f},
                                             {20.0f, 0.5f, 3.0f}};
            for (const auto &e : exact)
                track.keys.push_back({S_vector(x + e[0], e[1], z + e[2]), float(I(1, 3)) * 5.0f});
            c.tracks.push_back(track);
            continue;
        }
        for (int k = 0; k < keys; ++k) {
            static const float speeds[] = {0.0f, 2.0f, 5.0f, 10.0f, 15.0f, 3.5f};
            track.keys.push_back({S_vector(x, float(I(0, 4)) * 0.5f, z), speeds[I(0, 5)]});
            // (A repeated key now and then, or one a hair away.)
            const float step = P(0.04) ? 0.0f : P(0.04) ? 1e-5f : float(I(2, 25));
            heading += float(I(-30, 30)) * 0.01f;
            x += std::cos(heading) * step;
            z += std::sin(heading) * step;
        }
        c.tracks.push_back(track);
    }
    // The checkpoints: near some keys of the first track (within 2 m, or 3 m: too far), linked on; branches.
    if (P(0.85) && !c.tracks[0].keys.empty()) {
        int previous = -1;
        for (size_t k = 0; k < c.tracks[0].keys.size(); ++k) {
            if (k && P(0.3))
                continue;
            const S_vector &kp = c.tracks[0].keys[k].pos;
            const float off = P(0.8) ? float(I(0, 10)) * 0.1f : 3.0f;
            c.points.push_back({S_vector(kp.x + off, kp.y, kp.z), {}});
            const int i = int(c.points.size()) - 1;
            if (previous >= 0) {
                c.points[size_t(previous)].links.push_back(i);
                c.points[size_t(i)].links.push_back(previous);
            }
            if (P(0.3)) {                               // a branch off it
                c.points.push_back({S_vector(kp.x + float(I(-10, 10)), kp.y, kp.z + float(I(-10, 10))), {}});
                const int br = int(c.points.size()) - 1;
                c.points[size_t(i)].links.push_back(br);
                c.points[size_t(br)].links.push_back(i);
            }
            previous = i;
        }
    }
    c.wagons = I(1, 4);
    c.readProps = P(0.8);
    if (c.readProps) {
        S_Writer w;
        w.Begin(0xAE24);
        const int v = I(0, 9);
        if (P(0.05))                                    // (not the version first: refused)
            w.Text(8, "the train");
        w.EntryOf(1, v == 0 ? 1 : v == 1 ? 2 : 3);
        if (P(0.3))
            w.Text(8, "the train");
        for (int i = 0; i < c.wagons + (P(0.1) ? 1 : 0); ++i) {
            // (Now and then a name of 1,000 characters, and a short gap entry first.)
            w.Text(10, P(0.03) ? std::string(1000, char('a' + i)) : F("wagon%d", i));
            if (P(0.05))
                w.EntryOf(0x1E, uint16_t(7));
            static const float lengths[] = {10.0f, 8.0f, 12.5f, 6.0f};
            if (P(0.8))
                w.EntryOf(0x14, lengths[I(0, 3)]);
            if (P(0.8))
                w.EntryOf(0x1E, float(I(0, 4)));
            if (P(0.05)) {                              // (another id, entered and not left: a wagon inside it)
                w.Begin(uint16_t(I(40, 50)));
                w.Text(10, "wagon9");
                w.End();
            }
        }
        w.End();
        c.props = w.out;
    }
    c.started = P(0.95);
    c.dummyFrame = P(0.03);
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    c.role = roles[I(0, 3)];
    const int count = P(0.1) ? I(60, 160) : I(4, 40);
    for (int i = 0; i < count; ++i) {
        S_TrOp op;
        const int r = I(0, 99);
        op.kind = r < 50 ? OP_UPDATE : r < 62 ? OP_SET_TRACK : r < 67 ? OP_WHISTLE : r < 80 ? OP_MOVE : r < 90 ? OP_SAVE_LOAD
                : r < 94 ? OP_RESTART : OP_STOP_START;
        op.a = I(0, 1000);
        op.b = I(0, 1000);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    return c;
}

// (HD2_TR_PROBE: the port's and the original's FindPointAtDistance on the case's first track, random inputs.)
void ProbeFindPoint()
{
    if (g_trackModule.m_tracks.empty() || g_trackModule.m_tracks[0]->NumKeys() < 2)
        return;
    C_TrackPlayer port;
    port.SetTrack(g_trackModule.m_tracks[0]);
    static uint8_t orig[0x8080];
    Mut<void *>(orig, 0x48) = g_origTracks[0];
    using T_Find = void *(TC *)(void *, S_vector *, int, const S_vector *, float, int *);
    const int keys = g_trackModule.m_tracks[0]->NumKeys();
    for (int n = 0; n < 50; ++n) {
        const int key = I(0, keys - 1);
        const S_vector &k = g_trackModule.m_tracks[0]->GetKey(key).pos;
        const S_vector center(k.x + float(I(-50, 50)) * 0.1f, k.y + float(I(-10, 10)) * 0.1f, k.z + float(I(-50, 50)) * 0.1f);
        const float radius = float(I(1, 200)) * 0.1f;
        int pk = -1, ok = -1;
        const S_vector pr = port.FindPointAtDistance(key, center, radius, &pk);
        S_vector orr;
        OriginalAt<T_Find>(0x00404c80)(orig, &orr, key, &center, radius, &ok);
        // TimeToKey, AccelerateTowardsKey, AdvanceAlongKeys.
        if (key >= 1) {
            const float pt = port.TimeToKey(center, key);
            const float ot = OriginalAt<float(TC *)(void *, const S_vector *, int)>(0x00402bd0)(orig, &center, key);
            if (std::memcmp(&pt, &ot, 4) != 0)
                std::printf("probe TimeToKey %d: port %.9g original %.9g\n", key, pt, ot);
        }
        if (key + 1 < keys) {
            const float time = float(I(0, 3000)) * 0.001f;
            const S_vector pa = port.AccelerateTowardsKey(center, key, time);
            S_vector oa;
            OriginalAt<void *(TC *)(void *, S_vector *, const S_vector *, int, float)>(0x004030d0)(orig, &oa, &center, key, time);
            if (std::memcmp(&pa, &oa, 12) != 0)
                std::printf("probe Accelerate %d: port %.9g %.9g %.9g original %.9g %.9g %.9g\n", key, pa.x, pa.y, pa.z, oa.x, oa.y,
                            oa.z);
        }
        {
            port.SetKeyIndex(key);
            Mut<int>(orig, 0x4C) = key;
            const int ms = I(0, 3000);
            bool pd = false, od = false;
            const S_vector pv = port.AdvanceAlongKeys(center, ms, &pd);
            S_vector ov;
            OriginalAt<void *(TC *)(void *, S_vector *, const S_vector *, int, bool *)>(0x00402a70)(orig, &ov, &center, ms, &od);
            if (std::memcmp(&pv, &ov, 12) != 0 || pd != od || port.GetKeyIndex() != Mut<int>(orig, 0x4C))
                std::printf("probe Advance %d %d: port %.9g %.9g %.9g %d %d original %.9g %.9g %.9g %d %d\n", key, ms, pv.x, pv.y, pv.z,
                            int(pd), port.GetKeyIndex(), ov.x, ov.y, ov.z, int(od), Mut<int>(orig, 0x4C));
        }
        if (std::memcmp(&pr, &orr, 12) != 0 || pk != ok)
            std::printf("probe key %d radius %g: port %.9g %.9g %.9g (%d) original %.9g %.9g %.9g (%d)\n", key, radius, pr.x, pr.y,
                        pr.z, pk, orr.x, orr.y, orr.z, ok);
    }
}
using T_TrInt = void(TC *)(void *, int);
using T_TrChunk = bool(TC *)(void *, void *);
std::string RunTrOp(const S_TrOp &op, int index)
{
    const bool o = g_side != 0;
    uint8_t *b = g_train[g_side];
    C_Train *p = PortTrain();
    const int t = FRAME_TIMES[op.a % 9];
    // (The first wagon without its frame (none found: StartMission stops at a missing wagon, from the last): the
    // original's FollowTrack and Move read it through null; no track following, updates or moves then.)
    bool framed;
    if (o) {
        const std::vector<uint32_t> wagons = OVec(b, 0x1A0);
        framed = !wagons.empty() && At<uint32_t>(reinterpret_cast<void *>(wagons[0]), 0x30);
    } else
        framed = !p->m_wagons.empty() && p->m_wagons[0]->frame;
    if (!framed && (op.kind == OP_UPDATE || op.kind == OP_SET_TRACK || op.kind == OP_MOVE))
        return F("%s (the first wagon without its frame)", OP_KIND_NAMES[op.kind]);
    // (Nor a move without a track of two keys: the original's Move reads through null or past the keys.)
    int keys = -1;
    if (o) {
        if (const uint32_t track = At<uint32_t>(b, 0x1C0 + 0x48))
            keys = At<int>(reinterpret_cast<void *>(track), 0x18);
    } else if (const C_Track *track = p->m_player.GetTrack())
        keys = track->NumKeys();
    const bool running = o ? b[0x19C] != 0 : p->m_running;
    const bool started = o ? b[0x1B0] != 0 : p->m_started;
    if (keys < 2 && started && (op.kind == OP_MOVE || (op.kind == OP_UPDATE && running)))
        return F("%s (no track of two keys)", OP_KIND_NAMES[op.kind]);
    switch (op.kind) {
    case OP_UPDATE:
        if (o)
            OriginalAt<T_TrInt>(O_TR_UPDATE)(b, t);
        else
            p->Update(t);
        return F("Update %d", t);
    case OP_SET_TRACK: {
        static const char *const names[] = {"track1", "track2", "TRACK1", "nowhere"};
        const bool on = op.c % 5 != 0;
        const char *name = names[op.b % 4];
        // (With on, a track that is there: the original's FollowTrack places the train on a missing one through null.)
        if (on && g_trackModule.FindTrack(name) == nullptr)
            name = names[2];
        if (o)
            OriginalAt<void(TC *)(void *, int, const char *)>(O_TR_SET_TRACK)(b, on, name);
        else
            p->SetTrack(on, name);
        return F("SetTrack %d %s", int(on), name);
    }
    case OP_WHISTLE:
        if (o)
            OriginalAt<T_Void>(O_TR_START_WHISTLE)(b);
        else
            p->Start();
        return "Start";
    case OP_MOVE: {
        const bool force = op.c & 1;
        if (o)
            OriginalAt<void(TC *)(void *, uint32_t, int)>(O_TR_MOVE)(b, uint32_t(t), force);
        else
            p->Move(uint32_t(t), force);
        return F("Move %d %d", t, int(force));
    }
    case OP_SAVE_LOAD: {
        // (Not running without a track: the original's save reads its name through null.)
        const bool running = o ? b[0x19C] != 0 : p->m_running;
        const bool track = o ? At<uint32_t>(b, 0x1C0 + 0x48) != 0 : p->m_player.GetTrack() != nullptr;
        if (running && !track)
            return "SaveGame (running without a track)";
        // (Nor running with a wagon without its frame: the original reads its place through null.)
        bool frameless = false;
        if (o) {
            for (uint32_t w : OVec(b, 0x1A0))
                frameless |= At<uint32_t>(reinterpret_cast<void *>(w), 0x30) == 0;
        } else
            for (const auto *w : p->m_wagons)
                frameless |= w->frame == nullptr;
        if (running && frameless)
            return "SaveGame (running with a wagon without its frame)";
        const std::string file = F("tr_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_TrChunk>(O_TR_SAVE)(b, &save) : p->SaveGame(save);
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        std::string hex;
        for (unsigned char ch : bytes)
            hex += F("%02x", ch);
        // (A wagon past the track's reach: its place NaN in both programs, the x87's positive, the SSE's negative.)
        for (size_t at = 0; (at = hex.find("0000c0ff", at)) != std::string::npos; at += 8)
            hex.replace(at, 8, "0000c07f");
        bool rl = false;
        const int load = op.c % 4;
        if (load) {
            std::string name = file;
            if (load >= 2) {
                // A made file: running on a track (or one not there), a key, the wagons' places (fewer or more).
                S_Writer w;
                w.Begin(0x2000);
                if (load == 2) {
                    w.Begin(2000);
                    w.End();
                } else {
                    w.Begin(1000);
                    // (A name of length 1, its NUL only, now and then.)
                    const std::string track = op.d % 3 ? "track1" : op.d % 2 ? "nowhere" : "";
                    const uint16_t length = track.empty() ? uint16_t(op.d % 4 == 0 ? 1 : 0) : uint16_t(track.size() + 1);
                    w.Raw(&length, 2);
                    w.Raw(track.c_str(), length);
                    const int key = op.b % 6;
                    w.Raw(&key, 4);
                    if (op.d % 4) {
                        // (Not past the wagons with frames: the original reads past them, or through null.)
                        int framed = 0;
                        if (o) {
                            for (uint32_t w : OVec(b, 0x1A0))
                                if (At<uint32_t>(reinterpret_cast<void *>(w), 0x30))
                                    ++framed;
                                else
                                    break;
                        } else {
                            for (const auto *w : p->m_wagons)
                                if (w->frame)
                                    ++framed;
                                else
                                    break;
                        }
                        const int count = framed ? op.a % (framed + 1) : 0;
                        w.Raw(&count, 4);
                        for (int i = 0; i < count; ++i) {
                            const float pos[3] = {float(i) * 3.0f, 0.5f, float(op.b % 20)};
                            w.Raw(pos, 12);
                        }
                    }
                    w.End();
                }
                w.End();
                name = F("tr_made%d.sav", index);
                g_overrides[Lower(name)] = w.out;
            }
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_TrChunk>(O_TR_LOAD)(b, &chunk) : p->LoadGame(chunk);
            chunk.Close();
        }
        return F("SaveGame -> %d LoadGame %d -> %d bytes ", int(rs), load, int(rl)) + hex;
    }
    case OP_RESTART: {
        const bool server = op.c & 1;
        if (o)
            OriginalAt<T_TrInt>(O_TR_RESTART)(b, server);
        else
            p->RestartMission(server);
        return F("RestartMission %d", int(server));
    }
    default: {
        const bool server = op.c & 1;
        const int what = op.d % 3;
        if (what != 2) {
            if (o)
                OriginalAt<T_TrInt>(O_TR_STOP)(b, server);
            else
                p->StopMission(server);
        }
        if (what != 1) {
            if (o)
                OriginalAt<T_TrInt>(O_TR_START)(b, server);
            else
                p->StartMission(server);
        }
        return F("StopStart %d %d", what, int(server));
    }
    }
}

void ClearTrCaches()
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
            std::printf("tr_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_trShow;
void RunTrCase(const S_TrCase &c)
{
    g_caseStart = GetTickCount();
    ++g_trCases;
    g_holdFrees = true;
    g_current = F("case %d", g_trCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_overrides[Lower("tr_props.bin")] = c.props;
    static const bool trace = std::getenv("HD2_TR_TRACE") != nullptr;
    if (trace)
        std::puts("trace tracks");
    MakeTracks(c.tracks);
    if (trace)
        std::puts("trace points");
    MakePoints(c.points);
    if (std::getenv("HD2_TR_PROBE"))
        ProbeFindPoint();
    if (trace)
        std::puts("trace sides");
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_world[side].frames.reserve(65536);
        g_texts[side].clear();
        g_soundFrames[side].clear();
        BuildWorld(side, worldSeed);
        g_sndDriver[side] = Block(0x40);
        *reinterpret_cast<void ***>(g_sndDriver[side]) = EngineVtable("ISND_driver");
        Name(g_sndDriver[side], "sound driver");
        for (size_t i = 0; i < c.points.size(); ++i)
            Name(&(*g_points[side])[i], F("point %d", int(i)));
        // The wagons' models (in the scene, not the sector yet; the first the train's frame).
        std::vector<uint8_t *> models;
        for (int i = 0; i < c.wagons + 1; ++i) {
            uint8_t *m = Child(nullptr, i == 0 && c.dummyFrame ? FRAME_DUMMY : FRAME_MODEL, F("wagon%d", i), nullptr);
            BuildWagon(m, i == 0);
            Place(m, c.trainX + float(i) * 12.0f, 0.0f, c.trainZ);
            models.push_back(m);
        }
        SetRole(c.role);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        // The train: made, its frame, its properties, started.
        uint8_t *b;
        if (side) {
            b = Block(0x8260);
            OriginalAt<T_Void>(O_TR_CTOR)(b);
            // (The marks as the port's start: the original's constructor leaves them; ClearPath sets them before use.)
            Mut<int>(b, 0x190) = -1;
            Mut<int>(b, 0x194) = -1;
            Mut<int>(b, 0x198) = -1;                    // (the chuff set: SetSounds sets it before use)
        } else {
            b = Block(sizeof(C_Train) + 16);
            new (b) C_Train();
        }
        g_train[side] = b;
        Name(b, "train");
        if (side) {
            Mut<uint8_t *>(b, 0x20) = models[0];
            out.push_back(F("SetFrame -> %d", int(OriginalAt<bool(TC *)(void *, void *)>(O_TR_SET_FRAME)(b, models[0]))));
        } else {
            PortTrain()->m_frame = AsFrame(models[0]);
            out.push_back(F("SetFrame -> %d", int(PortTrain()->SetFrame(AsFrame(models[0])))));
        }
        if (c.readProps) {
            C_chunk props;
            props.Open("tr_props.bin", false);
            uint16_t id;
            props.Descend(id);
            const bool ok = side ? OriginalAt<T_TrChunk>(O_TR_READ)(b, &props) : PortTrain()->ReadProperties(props);
            props.Close();
            out.push_back(F("ReadProperties -> %d", int(ok)));
        }
        if (c.started) {
            const bool server = (c.role & GAME_ROLE_SERVER) != 0;
            if (side)
                OriginalAt<T_TrInt>(O_TR_START)(b, server);
            else
                PortTrain()->StartMission(server);
        }
        for (const std::string &s : TrainState())
            out.push_back(s);
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            out.push_back(RunTrOp(c.ops[i], int(i)));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + MaskSoundW(s));
            g_log[side].clear();
            for (const std::string &s : TrainState())
                out.push_back("  " + s);
        }
        if (side) {
            OriginalAt<T_TrInt>(O_TR_STOP)(b, 1);
            OriginalAt<T_Void>(O_TR_DTOR)(b);
        } else {
            PortTrain()->StopMission(true);
            PortTrain()->~C_Train();
        }
        // The creator: a train made and destroyed (the original's at 0x008ac5ac).
        if (side) {
            void *made = OriginalAt<void *(TC *)(void *)>(0x005bbde0)(OriginalAt<void *>(0x008ac5ac));
            Name(made, "created train");
            out.push_back("created");
            OriginalAt<void(TC *)(void *, void *)>(0x005bbe50)(OriginalAt<void *>(0x008ac5ac), made);
        } else {
            C_Actor *made = g_actorCreators[ACTOR_TRAIN]->Create();
            Name(made, "created train");
            out.push_back("created");
            g_actorCreators[ACTOR_TRAIN]->Destroy(made);
        }
        g_case = "";
        for (const std::string &s : TrFrameStates())
            out.push_back(s);
        for (const std::string &s : g_log[side])
            out.push_back("call " + MaskSoundW(s));
        ClearTrCaches();
        g_train[side] = nullptr;
    }
    static const int only = std::getenv("HD2_TR_ONLY") ? std::atoi(std::getenv("HD2_TR_ONLY")) : 0;
    if (g_trShow && (!only || only == g_trCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_trShow == 2)
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
void SetUpTrain()
{
    SetUpCmd();
    SetUpTrainAnswers();
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
        {0x00401ed0, reinterpret_cast<const void *>(&O_FindTrack)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
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
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("tr_diff: main");
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
        std::printf("tr_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_TR_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_TR_SHOW"))
        g_trShow = std::atoi(s) ? std::atoi(s) : 1;
    g_dump = std::getenv("HD2_TR_DUMP") != nullptr;
    int cases = std::getenv("HD2_TR_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_TR_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpTrain();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunTrCase(RandomTrCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("tr_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_trCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
