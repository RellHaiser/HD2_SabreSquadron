// Track part 1: the track module and its spline players (game/track.cpp, the original's 0x00401040..0x00404fff) against
// the original executable's. cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the module's
// calls go through tk_calls.s / tk_calls.inc (gen_cmd_calls.py tk). The original's module is its global (0x00867a64:
// its own code reads it), built once by its constructor 0x00401060; the port's is its global. The tracks come from the
// game's missions' tracks.dat (H:\HD2_Unpacked, HD2_UNPACKED) or from files made for the case; the LS3DF math (Slerp,
// the quaternion's RotateByMatrix, the vector's product by a matrix) is LS3DF.dll's on both sides.
//
// Each case builds the same world on each side (cmd_diff's) and runs the same random operations on the module:
// LoadMissionData (a game mission's tracks, a made file: odd tensions, repeated and near keys, keys of speed 0, an empty
// track, unknown entries, cut-short headers; a mission without the file), FreeMissionData, AddTrack, FindTrack,
// CreatePlayer (on a world frame, a frame already following, none; in a parent's space or not), RemovePlayer, updates
// (the mission running or not), StopMission, RestartMission, saves and loads (the frames found by name), and on a player
// GetOffsetFromStart, AdvanceAlongKeys and BuildPath. After each operation the answer, its calls and the module (every
// track's name, values and keys; every player's fields and its path's points) are compared; at the end the frames.
// MinGW; build_tk.bat; runs tk_diff.exe ("tk_diff: N checks, 0 failures"). Env: HD2_TK_CASES (default 1,500;
// HD2_TK_QUICK: 300), HD2_TK_SEED, HD2_TK_SHOW (1: the port's lines; 2: both), HD2_TK_ONLY, HD2_TK_TRACE.
#define HARNESS_NAME "tk_diff"
#define CMD_CALLS_INC "tk_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>
#include <memory>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/track.h"
#undef protected
#undef private

// ---- the port's side of what the module calls and the test provides ----
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpenWrite(name); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
// The port's allocations zeroed, as the original's (its new redirected to a zeroing one).
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
// The module base (its name and id; not logged: the port's global module is made before the logger is).
C_Module::C_Module(int id, const char *name) : m_name(name), m_id(id) {}
C_Module::~C_Module() {}

// LS3DF.dll's math (the original's calls go there too; a struct answer's address after `this`).
template <class T> T Ls3dfFn(const char *name) { return reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df(name))); }
S_quat __stdcall S_quat::Slerp(const S_quat &q, float t, bool shortest) const
{
    static const auto f =
        Ls3dfFn<void *(__stdcall *)(const S_quat *, S_quat *, const S_quat &, float, bool)>("?Slerp@S_quat@@QBG?AU1@ABU1@M_N@Z");
    S_quat r;
    f(this, &r, q, t, shortest);
    return r;
}
S_quat __stdcall S_quat::RotateByMatrix(const S_matrix &m) const
{
    static const auto f = Ls3dfFn<void *(__stdcall *)(const S_quat *, S_quat *, const S_matrix &)>(
        "?RotateByMatrix@S_quat@@QBG?AU1@ABUS_matrix@@@Z");
    S_quat r;
    f(this, &r, m);
    return r;
}

namespace H {

uint32_t Ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
template <class T> T &Mut(void *p, uint32_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }
const float *FP(const void *p, uint32_t off) { return reinterpret_cast<const float *>(static_cast<const uint8_t *>(p) + off); }
// Floats as text, a NaN's sign left out (the x87's and SSE's differ).
std::string Fl(float f) { return std::isnan(f) ? std::string("nan") : FloatText(f); }
std::string VN(const float *v, int n)
{
    std::string s = "(";
    for (int i = 0; i < n; ++i)
        s += (i ? " " : "") + Fl(v[i]);
    return s + ")";
}
std::string NanText(std::string s)
{
    for (size_t at; (at = s.find("-nan")) != std::string::npos;)
        s.erase(at, 1);
    return s;
}
int __cdecl O_Atexit(void *) { return 0; }
void *__cdecl O_AlignedMalloc(size_t size, size_t align)
{
    void *p = _aligned_malloc(size ? size : 1, align);
    std::memset(p, 0, size ? size : 1);
    return p;
}
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }
int __cdecl O_Sprintf(char *out, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = std::vsprintf(out, format, args);
    va_end(args);
    return n;
}
std::deque<std::string> g_texts[2];                     // (names: stable storage)
const char *Text(const std::string &s)
{
    g_texts[g_side].push_back(s);
    return g_texts[g_side].back().c_str();
}

void Stale(I3D_frame *f)
{
    f->m_frameFlags &= ~uint32_t(FRMFLAGS_WMATRIX_VALID);
    for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling)
        Stale(c);
}

constexpr uint32_t O_TRACK_MODULE = 0x00867a64;
constexpr uint32_t O_TK_CTOR = 0x00401060, O_TK_UPDATE = 0x00401fd0, O_TK_LOAD_DATA = 0x00402020, O_TK_FREE_DATA = 0x00402030,
                   O_TK_STOP = 0x00401f40, O_TK_RESTART = 0x00401140, O_TK_SAVE = 0x00401160, O_TK_LOAD = 0x00401440,
                   O_TK_ADD = 0x00401d40, O_TK_FIND = 0x00401ed0, O_TK_CREATE = 0x00401b20, O_TK_REMOVE = 0x00401a90,
                   O_TP_OFFSET = 0x004029f0, O_TP_ADVANCE_KEYS = 0x00402a70, O_TP_BUILD_PATH = 0x004042e0;
uint8_t *OrigModule() { return OriginalAt<uint8_t *>(O_TRACK_MODULE); }

// ---- each side's tracks and players ----
std::vector<uint8_t *> OVec(const uint8_t *p, uint32_t off)
{
    std::vector<uint8_t *> out;
    const uint32_t first = At<uint32_t>(p, off + 4), last = At<uint32_t>(p, off + 8);
    for (uint32_t a = first; a && a < last; a += 4)
        out.push_back(*reinterpret_cast<uint8_t **>(a));
    return out;
}
std::vector<uint8_t *> Tracks()
{
    if (g_side)
        return OVec(OrigModule(), 0x0C);
    std::vector<uint8_t *> out;
    for (C_Track *t : g_trackModule.m_tracks)
        out.push_back(reinterpret_cast<uint8_t *>(t));
    return out;
}
std::vector<uint8_t *> Players()
{
    if (g_side)
        return OVec(OrigModule(), 0x1C);
    std::vector<uint8_t *> out;
    for (C_TrackPlayer *p : g_trackModule.m_players)
        out.push_back(reinterpret_cast<uint8_t *>(p));
    return out;
}
// A track's keys (the original's: an array at +0x10, its count at +0x18, 0x30 bytes each).
int KeyCount(const uint8_t *t)
{
    return g_side ? At<int>(t, 0x18) : reinterpret_cast<const C_Track *>(t)->NumKeys();
}
std::string TrackName(const uint8_t *t)
{
    if (g_side) {
        const char *s = reinterpret_cast<const char *>(At<uint32_t>(t, 4));
        return s ? s : "";
    }
    return reinterpret_cast<const C_Track *>(t)->GetName();
}
uint8_t *PlayerTrack(const uint8_t *p) { return reinterpret_cast<uint8_t *>(g_side ? At<uint32_t>(p, 0x48) : Ptr(reinterpret_cast<const C_TrackPlayer *>(p)->m_track)); }
uint32_t PlayerFrame(const uint8_t *p) { return g_side ? At<uint32_t>(p, 0x40) : Ptr(reinterpret_cast<const C_TrackPlayer *>(p)->m_frame); }
int PlayerKey(const uint8_t *p) { return g_side ? At<int>(p, 0x4C) : reinterpret_cast<const C_TrackPlayer *>(p)->m_keyIndex; }
int PlayerPathIndex(const uint8_t *p) { return g_side ? At<int>(p, 0x8060) : reinterpret_cast<const C_TrackPlayer *>(p)->m_pathIndex; }

// Whether an update of `ms` takes the player to the end of its last path with time left: the original then walks that
// path again and, with time still left, builds the path of the last key, reading the key after the end (garbage; now
// and then a crash, the cursor running on); the port's fix stops it there (PortPastEndCheck). Found by running the
// original's Advance steps on a copy of the player in the port's code.
bool UpdatePassesEnd(const uint8_t *p, int ms)
{
    const uint8_t *t = PlayerTrack(p);
    const int count = KeyCount(t);
    const int key = PlayerKey(p);
    if (key + 2 >= count)
        return false;                                       // (the update returns at once)
    C_Track track("copy");
    for (int k = 0; k < count; ++k) {
        if (g_side) {
            const uint8_t *x = reinterpret_cast<const uint8_t *>(At<uint32_t>(t, 0x10)) + 0x30 * k;
            S_quat rot;
            std::memcpy(static_cast<void *>(&rot), x + 0x10, 16);
            track.AddKey(*reinterpret_cast<const S_vector *>(x), rot, At<float>(x, 0x20));
        } else {
            const S_TrackKey &k0 = reinterpret_cast<const C_Track *>(t)->GetKey(k);
            track.AddKey(k0.pos, k0.rot, k0.speed);
        }
    }
    track.m_tension = g_side ? At<float>(t, 0x20) : reinterpret_cast<const C_Track *>(t)->m_tension;
    std::unique_ptr<C_TrackPlayer> copy(new C_TrackPlayer(&track, nullptr, nullptr));
    if (g_side) {
        copy->m_curPos = *reinterpret_cast<const S_vector *>(p + 0x10);
        copy->m_tolerance = At<float>(p, 0x54);
        for (int i = 0; i < 1024; ++i) {
            copy->m_path[i].pos = *reinterpret_cast<const S_vector *>(p + 0x60 + 0x20 * i);
            copy->m_path[i].speed = At<float>(p, 0x60 + 0x20 * uint32_t(i) + 0x10);
        }
    } else {
        const C_TrackPlayer *x = reinterpret_cast<const C_TrackPlayer *>(p);
        copy->m_curPos = x->m_curPos;
        copy->m_tolerance = x->m_tolerance;
        std::memcpy(static_cast<void *>(copy->m_path), x->m_path, sizeof x->m_path);
    }
    copy->m_keyIndex = key;
    copy->m_pathIndex = PlayerPathIndex(p);
    // (Advance's steps, stopping before the build of a key past the end.)
    S_vector pos = copy->m_curPos;
    if (copy->m_pathIndex == -1) {
        copy->BuildPath(0);
        copy->m_pathIndex = 0;
    }
    float remaining = copy->MoveAlongPath(pos, ms * 0.001f);
    while (remaining != 0.0f) {
        // (Reaching the key before the last with time left: the original walks the previous path again, then reads past
        // the keys; the port's fix stops there.)
        if (++copy->m_keyIndex >= count - 2)
            return true;
        copy->BuildPath(copy->m_keyIndex);
        copy->m_pathIndex = 0;
        remaining = copy->MoveAlongPath(pos, remaining);
    }
    return false;
}

std::vector<std::string> ModuleState()
{
    std::vector<std::string> out;
    for (uint8_t *t : Tracks()) {
        std::string s = "track " + TrackName(t);
        if (g_side) {
            s += " tension " + Fl(At<float>(t, 0x20)) + " u24 " + Fl(At<float>(t, 0x24));
            const uint8_t *keys = reinterpret_cast<const uint8_t *>(At<uint32_t>(t, 0x10));
            for (int k = 0; k < KeyCount(t); ++k) {
                const uint8_t *x = keys + 0x30 * k;
                s += " {" + VN(FP(x, 0), 3) + " " + VN(FP(x, 0x10), 4) + " " + Fl(At<float>(x, 0x20)) + "}";
            }
        } else {
            const C_Track *x = reinterpret_cast<const C_Track *>(t);
            s += " tension " + Fl(x->m_tension) + " u24 " + Fl(x->m_unknown24);
            for (int k = 0; k < x->NumKeys(); ++k) {
                const S_TrackKey &key = x->GetKey(k);
                s += " {" + VN(&key.pos.x, 3) + " " + VN(reinterpret_cast<const float *>(&key.rot), 4) + " " + Fl(key.speed) + "}";
            }
        }
        out.push_back(s);
    }
    for (uint8_t *p : Players()) {
        std::string s;
        const int pathIndex = PlayerPathIndex(p);
        if (g_side)
            s = "player start " + VN(FP(p, 0), 3) + " cur " + VN(FP(p, 0x10), 3) + " rot " + VN(FP(p, 0x20), 4) + " restore " +
                VN(FP(p, 0x30), 3) +
                F(" frame %s parent %s track %s key %d u50 %d tolerance ", NameOf(At<uint32_t>(p, 0x40)).c_str(),
                  NameOf(At<uint32_t>(p, 0x44)).c_str(), TrackName(PlayerTrack(p)).c_str(), At<int>(p, 0x4C), At<int>(p, 0x50)) +
                Fl(At<float>(p, 0x54)) + F(" active %d time %d path %d", int(p[0x58]), At<int>(p, 0x5C), pathIndex);
        else {
            const C_TrackPlayer *x = reinterpret_cast<const C_TrackPlayer *>(p);
            s = "player start " + VN(&x->m_startPos.x, 3) + " cur " + VN(&x->m_curPos.x, 3) + " rot " +
                VN(reinterpret_cast<const float *>(&x->m_curRot), 4) + " restore " + VN(&x->m_frameRestorePos.x, 3) +
                F(" frame %s parent %s track %s key %d u50 %d tolerance ", NameOf(Ptr(x->m_frame)).c_str(),
                  NameOf(Ptr(x->m_parent)).c_str(), x->m_track ? x->m_track->GetName() : "", x->m_keyIndex, x->m_unknown50) +
                Fl(x->m_tolerance) + F(" active %d time %d path %d", int(x->m_active), x->m_time, pathIndex);
        }
        out.push_back(s);
        // The path's points up to the end mark (the original's 0x20 bytes each: the point, its speed at +0x10).
        std::string path = "  path";
        for (int i = 0; i < 1024; ++i) {
            float speed;
            const float *pos;
            if (g_side) {
                pos = FP(p, 0x60 + 0x20 * uint32_t(i));
                speed = At<float>(p, 0x60 + 0x20 * uint32_t(i) + 0x10);
            } else {
                const auto &pt = reinterpret_cast<const C_TrackPlayer *>(p)->m_path[i];
                pos = &pt.pos.x;
                speed = pt.speed;
            }
            if (i > 0 && speed == 0.0f && pos[0] == 0.0f && pos[1] == 0.0f && pos[2] == 0.0f)
                break;
            path += " " + VN(pos, 3) + ":" + Fl(speed);
            if (speed < 0.0f)
                break;
        }
        out.push_back(path);
    }
    return out;
}
std::vector<std::string> TkFrameStates()
{
    std::vector<std::string> out;
    for (const S_FakeFrame &f : W().frames) {
        const I3D_frame *x = AsFrame(f.bytes);
        out.push_back(F("frame %s flags %08x pos ", NameOf(Ptr(f.bytes)).c_str(), x->m_frameFlags) +
                      VN(&x->m_localMatrix.m[3][0], 3) + " rot " + VN(reinterpret_cast<const float *>(&x->m_rot), 4));
    }
    return out;
}

void SetUpTrackAnswers()
{
    // A frame's position: its local matrix's, its and its children's world matrices stale.
    g_answers["I3D_frame::SetPos"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        const S_vector &pos = *reinterpret_cast<const S_vector *>(a[1]);
        f->m_localMatrix.m[3][0] = pos.x;
        f->m_localMatrix.m[3][1] = pos.y;
        f->m_localMatrix.m[3][2] = pos.z;
        f->m_frameFlags &= ~uint32_t(FRMFLAGS_SETPOS_CLEAR);
        Stale(f);
        return 0u;
    };
    g_answers["I3D_frame::Update"] = [](const std::vector<uint32_t> &a) {
        Stale(reinterpret_cast<I3D_frame *>(a[0]));
        return 0u;
    };
}

// ---- the made tracks.dat ----
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
std::string MadeTracks(uint32_t seed)
{
    uint32_t s = seed;
    auto next = [&](int n) {
        s = s * 1664525u + 1013904223u;
        return int((s >> 8) % uint32_t(n));
    };
    S_Writer w;
    w.Begin(0x2426);
    const int tracks = 1 + next(3);
    for (int t = 0; t < tracks; ++t) {
        w.Begin(0x2454);
        if (next(8)) {
            w.Begin(0x2486);
            const std::string name = t == 2 ? "Track1" : F("track%d", t + 1);   // (one named as another, in other case)
            w.Raw(name.c_str(), name.size() + 1);
            w.End();
        }
        static const float tensions[] = {0.1f, 0.5f, 0.0f, 1.0f, -0.3f};
        if (next(2))
            w.EntryOf(0x249A, tensions[next(5)]);
        if (next(3) == 0)
            w.EntryOf(0x2490, 0.25f);
        const int keys = next(10) == 0 ? 0 : 1 + next(7);
        float x = float(next(20)), y = 0.0f, z = float(next(20));
        for (int k = 0; k < keys; ++k) {
            w.Begin(0x24B8);
            const int r = next(10);
            // Along a bending line; now and then a hair away. (Not the same place again, nor a speed of 0: the paths'
            // speeds turn NaN and both programs' moves loop on.)
            if (r != 1) {
                x += float(next(40) - 10);
                z += float(next(40) - 5);
                y += float(next(5) - 2) * 0.5f;
            } else if (r == 1) {
                x += 1e-4f;
            }
            const float pos[3] = {x, y, z};
            // (The place and the rotation always: a missing one leaves the original's local unset, the port's fix a
            // default. The speed now and then left out: 1 in both.)
            w.EntryOf(0x251C, pos);
            static const float speeds[] = {5.0f, 10.0f, 1.0f, 0.5f, 20.0f, 2.5f};
            if (next(8))
                w.EntryOf(0x2580, speeds[next(6)]);
            // (Now and then negated: the slerp between keys then takes the long way round.)
            const float sign = next(4) == 0 ? -1.0f : 1.0f;
            const float q[4] = {sign * float(next(10)) * 0.1f, 0.0f, sign * float(next(10)) * 0.1f, sign * float(next(3)) * 0.1f};
            w.EntryOf(0x25E4, q);
            w.End();
        }
        w.End();
    }
    // (No unknown entries: the original does not leave them, it reads the next header inside one; the port's fix
    // skips them.)
    w.End();
    return w.out;
}

// ---- one case ----
enum {
    OP_LOAD_DATA, OP_FREE_DATA, OP_ADD, OP_FIND, OP_CREATE, OP_REMOVE, OP_UPDATE, OP_STOP, OP_RESTART, OP_SAVE_LOAD, OP_OFFSET,
    OP_ADVANCE_KEYS, OP_BUILD_PATH, OP_KINDS
};
const char *const OP_NAMES[] = {"LoadMissionData", "FreeMissionData", "AddTrack", "FindTrack", "CreatePlayer", "RemovePlayer",
                                "Update", "StopMission", "RestartMission", "SaveLoad", "GetOffsetFromStart", "AdvanceAlongKeys",
                                "BuildPath"};
struct S_TkOp {
    int kind = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_TkCase {
    std::string mission;
    uint32_t madeSeed = 0;
    std::vector<S_TkOp> ops;
};
std::vector<std::string> g_trackMissions;
int g_tkCases, g_opCounts[OP_KINDS];
S_TkCase RandomTkCase()
{
    S_TkCase c;
    const int m = I(0, 9);
    c.mission = m < 5 && !g_trackMissions.empty() ? g_trackMissions[size_t(I(0, int(g_trackMissions.size()) - 1))]
              : m < 9 ? "MADE" : "NOTRACKS";
    c.madeSeed = U32();
    const int count = P(0.15) ? I(80, 250) : I(5, 60);
    for (int i = 0; i < count; ++i) {
        S_TkOp op;
        const int r = I(0, 99);
        op.kind = r < 4 ? OP_LOAD_DATA : r < 6 ? OP_FREE_DATA : r < 9 ? OP_ADD : r < 13 ? OP_FIND : r < 25 ? OP_CREATE
                : r < 28 ? OP_REMOVE : r < 68 ? OP_UPDATE : r < 70 ? OP_STOP : r < 72 ? OP_RESTART : r < 78 ? OP_SAVE_LOAD
                : r < 84 ? OP_OFFSET : r < 92 ? OP_ADVANCE_KEYS : OP_BUILD_PATH;
        op.a = I(0, 1000);
        op.b = I(-1, 6);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    // The tracks loaded first, mostly.
    if (P(0.9)) {
        S_TkOp op;
        op.kind = OP_LOAD_DATA;
        op.a = 0;
        c.ops.insert(c.ops.begin(), op);
    }
    return c;
}

uint8_t *WorldFrame(int n)
{
    const auto &frames = W().frames;
    return frames.empty() ? nullptr : frames[size_t(n) % frames.size()].bytes;
}
const char *TrackNameOf(int n)
{
    static const char *const names[] = {"track1", "TRACK2", "track3", "nowhere", "Track1"};
    const auto tracks = Tracks();
    if (n % 3 && !tracks.empty())
        return Text(TrackName(tracks[size_t(n) % tracks.size()]));
    return names[n % 5];
}

using T_TkBool = void(TC *)(void *, int);
std::string g_previousSave[2];
// The players each save file holds: track, key, path cursor.
struct S_SavedPlayer {
    std::string track;
    int key, pathIndex;
};
std::map<std::string, std::vector<S_SavedPlayer>> g_savedPlayers[2];
// A made save (the module's chunk: a count, then players' chunks of fields in any order): the world's frames and the
// tracks there by name; fields left out, a second track in one chunk (the fields after the first reset), a count one
// short, names long (998 characters: a longer one the original leaves unterminated) and unknown. (A built path only
// where the key has a next one: both programs read past the keys.)
std::string MadeSave(const S_TkOp &op, std::vector<S_SavedPlayer> &saved)
{
    uint32_t s = uint32_t(op.a * 7919 + op.b * 104729 + op.d * 31);
    auto next = [&](int n) {
        s = s * 1664525u + 1013904223u;
        return int((s >> 8) % uint32_t(n));
    };
    auto text = [](S_Writer &w, uint16_t id, const std::string &t) {
        w.Begin(id);
        w.Raw(t.c_str(), t.size() + 1);
        w.End();
    };
    S_Writer w;
    w.Begin(0x2000);
    const int players = next(4);
    // (Not more than there are: past the last a failed Descend sends the original back to the last field's case, with
    // whatever follows, the port's loop ends.)
    const int count = players - (players && next(5) == 0 ? 1 : 0);
    w.Raw(&count, 4);
    for (int i = 0; i < players; ++i) {
        w.Begin(0x029A);
        for (int part = next(4) == 0 ? 2 : 1; part > 0; --part) {
            const char *track = TrackNameOf(next(15));
            int keys = -1;
            for (uint8_t *t : Tracks())
                if (_stricmp(TrackName(t).c_str(), track) == 0)
                    keys = KeyCount(t);
            if (keys >= 0 && keys < 2)
                track = "nowhere";
            // (A field left out: 0, as after a track.)
            const bool hasKey = next(5) != 0, hasPath = next(5) != 0;
            const int pathIndex = hasPath ? (next(3) == 0 ? -1 : next(4)) : 0;
            const int key = !hasKey || keys < 2 ? 0 : next(pathIndex == -1 ? keys : keys - 1);
            auto frameName = [&](int n) {
                const I3D_frame *f = AsFrame(WorldFrame(n));
                return std::string(f && f->m_name ? f->m_name : "unknown frame");
            };
            const int frame = next(64);
            if (next(6)) {
                const int r = next(12);
                text(w, 1000, r == 0 ? std::string(998, 'f') : r == 1 ? std::string(1, 'f') : r == 2 ? "unknown frame" : frameName(frame));
            }
            if (next(3) == 0)
                text(w, 2000, frameName(frame + 1 + next(8)));
            if (next(5)) {
                const float pos[3] = {float(next(40) - 20), float(next(6)), float(next(40) - 20)};
                w.Begin(3000);
                w.Raw(pos, 12);
                w.End();
            }
            if (next(8) == 0)
                text(w, 5000, "unknown");                   // (an unknown field: left)
            if (hasKey)
                w.EntryOf(uint16_t(3500), key);
            if (hasPath)
                w.EntryOf(uint16_t(3750), pathIndex);
            text(w, 4000, track);
            saved.push_back({track, key, pathIndex});
        }
        w.End();
    }
    w.End();
    w.Begin(0x3000);
    w.Raw("\7\0\0\0", 4);
    w.End();
    return w.out;
}
std::string RunTkOp(const S_TkOp &op, int index)
{
    const bool o = g_side != 0;
    uint8_t *g = OrigModule();
    C_TrackModule &m = g_trackModule;
    const auto players = Players();
    switch (op.kind) {
    case OP_LOAD_DATA:
    case OP_FREE_DATA: {
        const bool server = op.a % 5 == 4;
        // (Not with players on the tracks freed: both programs leave them on freed tracks, read on the next update.)
        if (!players.empty() && !server)
            return F("%s (players on the tracks)", OP_NAMES[op.kind]);
        if (o)
            OriginalAt<T_TkBool>(op.kind == OP_LOAD_DATA ? O_TK_LOAD_DATA : O_TK_FREE_DATA)(g, server);
        else if (op.kind == OP_LOAD_DATA)
            m.LoadMissionData(server);
        else
            m.FreeMissionData(server);
        return F("%s %d", OP_NAMES[op.kind], int(server));
    }
    case OP_ADD: {
        const char *name = TrackNameOf(op.a);
        const bool r = o ? OriginalAt<bool(TC *)(void *, const char *)>(O_TK_ADD)(g, name) : m.AddTrack(name);
        return F("AddTrack %s -> %d", name, int(r));
    }
    case OP_FIND: {
        const char *name = TrackNameOf(op.a);
        int index = 77;
        const bool outs = op.d % 3 != 0;
        uint8_t *r = static_cast<uint8_t *>(o ? OriginalAt<void *(TC *)(void *, const char *, int *)>(O_TK_FIND)(g, name, outs ? &index : nullptr)
                                              : static_cast<void *>(m.FindTrack(name, outs ? &index : nullptr)));
        return F("FindTrack %s -> %s %d", name, r ? TrackName(r).c_str() : "null", index);
    }
    case OP_CREATE: {
        const char *name = TrackNameOf(op.a);
        // (A track of keys: the player's constructor reads the first unchecked in both programs.)
        bool keyed = false, found = false;
        for (uint8_t *t : Tracks())
            if (_stricmp(TrackName(t).c_str(), name) == 0) {
                if (!found)
                    keyed = KeyCount(t) > 0;
                found = true;
            }
        // (The module looks from the end: the last one of the name.)
        {
            const auto tracks = Tracks();
            for (size_t i = tracks.size(); i-- > 0;)
                if (_stricmp(TrackName(tracks[i]).c_str(), name) == 0) {
                    keyed = KeyCount(tracks[i]) > 0;
                    break;
                }
        }
        if (found && !keyed)
            return "CreatePlayer (a track without keys)";
        uint8_t *frame = op.c % 7 ? WorldFrame(op.c % 6) : nullptr;
        uint8_t *parent = op.d % 3 == 0 ? WorldFrame(op.d) : nullptr;
        if (parent == frame)
            parent = nullptr;
        uint8_t *r;
        if (o) {
            r = static_cast<uint8_t *>(OriginalAt<void *(TC *)(void *, const char *, void *, void *)>(O_TK_CREATE)(g, name, frame, parent));
            // (The original places the frame at the player's unset start, the port's fix at the first key: the
            // original's frame given the port's place.)
            if (r && frame) {
                I3D_frame *f = AsFrame(frame);
                const S_vector &first = *reinterpret_cast<const S_vector *>(At<uint32_t>(PlayerTrack(r), 0x10));
                f->m_localMatrix.m[3][0] = first.x;
                f->m_localMatrix.m[3][1] = first.y;
                f->m_localMatrix.m[3][2] = first.z;
                Stale(f);
            }
        } else
            r = reinterpret_cast<uint8_t *>(m.CreatePlayer(name, AsFrame(frame), AsFrame(parent)));
        if (r)
            Name(r, F("player %d", index));
        // (The constructor's SetPos: the unset start in the original, the first key in the port: left out.)
        for (std::string &line : g_log[g_side])
            if (line.rfind("I3D_frame::SetPos(", 0) == 0 && line.find(NameOf(Ptr(frame))) != std::string::npos) {
                line = "I3D_frame::SetPos(" + NameOf(Ptr(frame)) + ", -)";
                break;
            }
        return F("CreatePlayer %s %s %s -> %s", name, NameOf(Ptr(frame)).c_str(), NameOf(Ptr(parent)).c_str(),
                 r ? "a player" : "null");
    }
    case OP_REMOVE: {
        uint8_t *p = players.empty() || op.c % 6 == 0 ? reinterpret_cast<uint8_t *>(Block(16)) : players[size_t(op.a) % players.size()];
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(O_TK_REMOVE)(g, p);
        else
            m.RemovePlayer(reinterpret_cast<C_TrackPlayer *>(p));
        return F("RemovePlayer %s", NameOf(Ptr(p)).c_str());
    }
    case OP_UPDATE: {
        static const int times[] = {0, 16, 33, 100, 250, 1000, 3000, 1, 7, 500};
        const int t = times[op.a % 10];
        // (Not past a track's end: see UpdatePassesEnd.)
        for (uint8_t *p : players)
            if (UpdatePassesEnd(p, t))
                return F("Update %d (a player past its track's end)", t);
        static const int states[] = {1, 1, 5, 6, 0, 2};
        SetGameField(4, uint32_t(states[op.c % 6]));
        if (o)
            OriginalAt<T_TkBool>(O_TK_UPDATE)(g, t);
        else
            m.Update(t);
        return F("Update %d state %d", t, states[op.c % 6]);
    }
    case OP_STOP:
    case OP_RESTART: {
        const bool server = op.a % 4 == 3;
        if (o)
            OriginalAt<T_TkBool>(op.kind == OP_STOP ? O_TK_STOP : O_TK_RESTART)(g, server);
        else if (op.kind == OP_STOP)
            m.StopMission(server);
        else
            m.RestartMission(server);
        return F("%s %d", OP_NAMES[op.kind], int(server));
    }
    case OP_SAVE_LOAD: {
        // (Not with a player without a frame: both programs save its name through null.)
        for (uint8_t *p : players)
            if (!PlayerFrame(p))
                return "SaveLoad (a player without a frame)";
        const std::string file = F("tk_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<bool(TC *)(void *, void *)>(O_TK_SAVE)(g, &save) : m.SaveGame(save);
        save.Ascend();
        save.Begin(0x3000);
        save.Write(int(7));
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        std::string hex;
        for (unsigned char ch : bytes)
            hex += F("%02x", ch);
        // (Loaded now and then: this file or the case's previous one; the players made again by name.)
        std::vector<S_SavedPlayer> &saved = g_savedPlayers[g_side][file];
        saved.clear();
        for (uint8_t *p : players)
            saved.push_back({TrackName(PlayerTrack(p)), PlayerKey(p), PlayerPathIndex(p)});
        const int load = op.c % 3;
        bool rl = false;
        unsigned next = 0;
        const std::string previous = g_previousSave[g_side];
        g_previousSave[g_side] = file;
        if (load) {
            std::string name = load == 2 && !previous.empty() ? previous : file;
            // (A made file a third of the times otherwise.)
            if (load == 2 && op.d % 3 == 0) {
                name = F("tk_made%d.sav", index);
                std::vector<S_SavedPlayer> &made = g_savedPlayers[g_side][name];
                made.clear();
                g_overrides[Lower(name)] = MadeSave(op, made);
            }
            // (Not a player whose path is built past its track's keys, nor one on a track without keys (a track of the
            // name with fewer keys now): both programs read the key after the end, the constructor the first.)
            for (const S_SavedPlayer &s : g_savedPlayers[g_side][name]) {
                const uint8_t *t = nullptr;
                for (uint8_t *x : Tracks())
                    if (_stricmp(TrackName(x).c_str(), s.track.c_str()) == 0)
                        t = x;
                if (t && (KeyCount(t) == 0 || (s.pathIndex != -1 && s.key + 2 != KeyCount(t) && s.key + 1 >= KeyCount(t))))
                    return hex + F(" SaveGame -> %d (a load past a track's keys)", int(rs));
            }
            C_chunk chunk;
            chunk.Open(name.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<bool(TC *)(void *, void *)>(O_TK_LOAD)(g, &chunk) : m.LoadGame(chunk);
            // (Where the load left the reader: the save's chunk left, the next one's id.)
            chunk.Ascend();
            uint16_t after = 0;
            chunk.Descend(after);
            next = after;
            chunk.Close();
            // (The loaded players: those on the frames the load placed (addresses are reused).)
            std::vector<std::string> &log = g_log[g_side];
            for (uint8_t *p : Players()) {
                const std::string head = "I3D_frame::SetPos(" + NameOf(PlayerFrame(p)) + ", ";
                bool placed = false;
                for (const std::string &line : log)
                    placed = placed || line.rfind(head, 0) == 0;
                if (!placed && g_names[g_side].count(Ptr(p)))
                    continue;
                Name(p, F("loaded player %d", index));
            }
            // (A loaded player's constructor's SetPos, at the unset start in the original, the first key in the
            // port, is followed by the load's own: left out.)
            for (size_t i = 0; i < log.size(); ++i) {
                if (log[i].rfind("I3D_frame::SetPos(", 0) != 0)
                    continue;
                const std::string head = log[i].substr(0, log[i].find(", ") + 2);
                for (size_t j = i + 1; j < log.size(); ++j)
                    if (log[j].rfind(head, 0) == 0) {
                        log[i] = head + "-)";
                        break;
                    }
            }
        }
        return F("SaveGame -> %d LoadGame %d -> %d next %04x bytes ", int(rs), load, int(rl), next) + hex;
    }
    default: {
        if (players.empty())
            return F("%s (no player)", OP_NAMES[op.kind]);
        uint8_t *p = players[size_t(op.a) % players.size()];
        const int keys = KeyCount(PlayerTrack(p));
        if (op.kind == OP_OFFSET) {
            alignas(16) float offset[4] = {7, 7, 7, 7}, rot[4] = {8, 8, 8, 8};
            const bool r = o ? OriginalAt<bool(TC *)(void *, float *, float *)>(O_TP_OFFSET)(p, offset, rot)
                             : reinterpret_cast<C_TrackPlayer *>(p)->GetOffsetFromStart(*reinterpret_cast<S_vector *>(offset),
                                                                                        *reinterpret_cast<S_quat *>(rot));
            return F("GetOffsetFromStart -> %d ", int(r)) + VN(offset, 3) + " " + VN(rot, 4);
        }
        if (op.kind == OP_ADVANCE_KEYS) {
            // (The key-based movement from a place near the player's key; the time in ms.)
            alignas(16) float from[4] = {0, 0, 0, 0};
            const int key = PlayerKey(p);
            if (key + 1 < keys) {
                const uint8_t *t = PlayerTrack(p);
                const float *k0 = g_side ? FP(reinterpret_cast<void *>(At<uint32_t>(t, 0x10)), 0x30 * uint32_t(key))
                                         : &reinterpret_cast<const C_Track *>(t)->GetKey(key).pos.x;
                for (int i = 0; i < 3; ++i)
                    from[i] = k0[i] + float(op.d % 5) * 0.25f;
            }
            static const int times[] = {0, 100, 1000, 5000, 20000, 16};
            const int ms = times[op.c % 6];
            bool decelerating = op.d % 2 != 0;
            const bool outs = op.d % 3 != 0;
            alignas(16) float r[4] = {};
            if (o)
                OriginalAt<void *(TC *)(void *, float *, const float *, int, bool *)>(O_TP_ADVANCE_KEYS)(p, r, from, ms,
                                                                                                        outs ? &decelerating : nullptr);
            else {
                const S_vector v = reinterpret_cast<C_TrackPlayer *>(p)->AdvanceAlongKeys(*reinterpret_cast<const S_vector *>(from), ms,
                                                                                          outs ? &decelerating : nullptr);
                std::memcpy(r, &v, 12);
            }
            return F("AdvanceAlongKeys %d -> ", ms) + VN(r, 3) + F(" %d", int(decelerating));
        }
        // BuildPath of a key with a next one.
        if (keys < 2)
            return "BuildPath (fewer than two keys)";
        const int key = op.c % (keys - 1);
        if (o)
            OriginalAt<T_TkBool>(O_TP_BUILD_PATH)(p, key);
        else
            reinterpret_cast<C_TrackPlayer *>(p)->BuildPath(key);
        return F("BuildPath %d", key);
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
            std::printf("tk_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_tkShow;
void ClearModule()
{
    if (g_side) {
        // (Its players and tracks freed by its own StopMission and FreeMissionData.)
        OriginalAt<T_TkBool>(O_TK_STOP)(OrigModule(), 0);
        OriginalAt<T_TkBool>(O_TK_FREE_DATA)(OrigModule(), 0);
    } else {
        g_trackModule.StopMission(false);
        g_trackModule.FreeMissionData(false);
    }
}
void RunTkCase(const S_TkCase &c)
{
    g_caseStart = GetTickCount();
    ++g_tkCases;
    g_holdFrees = true;
    g_current = F("case %d (%s)", g_tkCases, c.mission.c_str());
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_overrides[Lower("missions\\MADE\\tracks.dat")] = MadeTracks(c.madeSeed);
    SetMission(c.mission);
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_world[side].frames.reserve(65536);
        g_texts[side].clear();
        g_previousSave[side].clear();
        g_savedPlayers[side].clear();
        BuildWorld(side, worldSeed);
        SetGameField(4, 1);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        static const int trace = std::getenv("HD2_TK_TRACE") ? std::atoi(std::getenv("HD2_TK_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_tkCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_NAMES[c.ops[i].kind],
                            c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(NanText(RunTkOp(c.ops[i], int(i))));
            if (trace == g_tkCases) {
                std::printf("trace   -> %s\n", out.back().substr(0, 160).c_str());
                std::fflush(stdout);
            }
            for (const std::string &s : g_log[side])
                out.push_back("  call " + NanText(s));
            g_log[side].clear();
            for (const std::string &s : ModuleState())
                out.push_back("  " + NanText(s));
        }
        g_case = "";
        ClearModule();
        for (const std::string &s : g_log[side])
            out.push_back("call " + NanText(s));
        g_log[side].clear();
        for (const std::string &s : TkFrameStates())
            out.push_back(NanText(s));
    }
    static const int only = std::getenv("HD2_TK_ONLY") ? std::atoi(std::getenv("HD2_TK_ONLY")) : 0;
    if (g_tkShow && (!only || only == g_tkCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_tkShow == 2)
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

void SetUpTrack()
{
    SetUpCmd();
    SetUpTrackAnswers();
    RedirectOriginal(0x007e562e, reinterpret_cast<const void *>(&O_Atexit));
    // The original's chunks seek and write through these.
    RedirectOriginal(0x007e53aa, reinterpret_cast<const void *>(&O_dtaOpenWrite));
    RedirectOriginal(0x007e5398, reinterpret_cast<const void *>(&O_dtaSeek));
    RedirectOriginal(0x007e5392, reinterpret_cast<const void *>(&O_dtaWrite));
    RedirectOriginal(0x007e53c2, reinterpret_cast<const void *>(&O_AlignedMalloc));
    RedirectOriginal(0x007e559c, reinterpret_cast<const void *>(&O_AlignedFree));
    RedirectOriginal(0x007e6334, reinterpret_cast<const void *>(&O_Sprintf));
    // The original's module (its static initialiser never ran).
    OriginalAt<void(TC *)(void *, int, const char *)>(O_TK_CTOR)(OrigModule(), MODULE_TRACK, "Track module");
    // The game's missions with tracks.
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((g_data + "\\Missions\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string dummy;
            const std::string name = fd.cFileName;
            if (name != "." && name != ".." && FileText("missions\\" + name + "\\tracks.dat", dummy))
                g_trackMissions.push_back(name);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::printf("tk_diff: %d missions with tracks\n", int(g_trackMissions.size()));
}

// The port's fixes of the original's reads past a track's keys (the cases leave them out: the original reads garbage
// there), checked on the port alone: updates past the end stop at the key before the last; a path from a key without a
// next one is refused; no player on a track without keys. (Build with -D_GLIBCXX_ASSERTIONS to have a read past the
// keys abort.)
void PortPastEndCheck()
{
    g_side = 0;
    C_TrackModule &m = g_trackModule;
    m.AddTrack("past end");
    C_Track *track = m.FindTrack("past end");
    S_quat rot;
    rot.Identity();
    for (int k = 0; k < 4; ++k)
        track->AddKey(S_vector(float(k) * 3.0f, 0.0f, float(k % 2)), rot, 5.0f);
    m.AddTrack("no keys");
    C_TrackPlayer *player = m.CreatePlayer("past end", nullptr, nullptr);
    bool ok = player != nullptr && m.CreatePlayer("no keys", nullptr, nullptr) == nullptr;
    for (int i = 0; ok && i < 4; ++i)
        player->Update(100000);
    const S_vector end = track->GetKey(2).pos;
    ok = ok && player->GetKeyIndex() == 2 && player->m_curPos.x == end.x && player->m_curPos.y == end.y &&
         player->m_curPos.z == end.z;
    if (ok) {
        player->BuildPath(3);
        player->BuildPath(7);
        player->BuildPath(-1);
        player->Update(100000);
        ok = player->GetKeyIndex() == 2;
    }
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL the port past a track's end: key %d\n", player ? player->GetKeyIndex() : -9);
    }
    if (player)
        m.RemovePlayer(player);
    m.FreeTracks();
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("tk_diff: main");
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
        std::printf("tk_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *d = std::getenv("HD2_UNPACKED"))
        g_data = d;
    if (const char *s = std::getenv("HD2_TK_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_TK_SHOW"))
        g_tkShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_TK_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_TK_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpTrack();
    PortPastEndCheck();
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunTkCase(RandomTkCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_NAMES[i], g_opCounts[i]);
    std::printf("tk_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_tkCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
