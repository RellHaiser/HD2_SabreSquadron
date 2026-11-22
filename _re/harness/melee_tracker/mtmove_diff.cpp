// Human parts 81a and 81b: the meleeTracker's movement (game/melee_tracker_movement.cpp: C_MeleeTrackerMovement, the tracker's
// field at +0x90, 0x410 bytes in the original's layout), its life cycle, modes, animation sequences, turning, sounds,
// network position, ground check, bite and saved games (81a), Update, Move and their collision helpers (81b), against
// the original's (0x005c0d10 .. 0x005c8b30, 0x005d09b0 .. 0x005d1f50). MinGW; build_mtmove.bat; runs mtmove_diff.exe ("mtmove_diff: N
// checks, 0 failures"). melee_tracker_movement.cpp is compiled into this file, the mixer (part 80's) linked; each side
// runs its own mixer.
// Each side has its movement (the same start bytes but for the tracker's pointer and the volumes'), its tracker (the
// port's C_MeleeTracker layout, the original's offsets: the frame, position, direction, stats, controller), its
// controller (the mode at +0x14; GetTarget and DistanceToTarget answered by its vtable, the port's slots its own) and
// its target. Shared: the frames (the interface kit's arena: the tracker's model with its stages (as part 80's), the
// collision dummies, "anim2", the bones), the animation table (0x008ac5c0), the animation sets, the materials. Faked on
// both sides with the same loggers: the collision world (AddVolume, RemoveVolume, MoveVolume; the volumes named by
// their slot; SetIgnoredVolume(s)), CollisionSphereTest (a scripted answer; 81b's operations: the stream below),
// CollisionRayTest and the body sweep MoveSpheresStepping (one scripted answer stream per scenario, taken in call order;
// the sweep's request logged), the debug trajectory line, the tracker's Hit, the scene's GetSector (two sectors), the sound module (Play: the whole request; Stop), the game
// (GetCameraModule, SetViewDir, Print, the scene's UpdateFrameSector), the player's MouseLook, the player team's
// AddSeenMeleeTracker, the target's Hit, the graph's mouse; AngleBetween2D and RotateAboutY are the original's on both
// sides. _aligned_malloc / _aligned_free and rand are the test's CRT's on both sides. The original's Move reads stack
// words it never wrote: its stack is zeroed first (Update's call of it patched to the zeroing wrapper), and after a Move
// the w words that can carry them (the tracker's position, the net positions', the facing's...) are not compared.
// Each scenario sets the world, then runs up to 14 operations; the logs (every call out, the results, the movement's
// bytes (the pointers by name), its volumes, the tracker's position and direction, the model's stages after each
// operation) compared.
// HD2_MTMOVE_ROUNDS: scenarios (default 27,000; HD2_MTMOVE_QUICK: 10,000); the kit's HD2_INTERFACE_* switches.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <malloc.h>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/resource_cache.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
// (The port's volume frees logged as the original's are: its _aligned_free through the test's.)
namespace H { void PortAlignedFree(void *p); }
#define _aligned_free H::PortAlignedFree
#include "game/melee_tracker_movement.cpp"
#undef _aligned_free
#include "game/camera_module.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/melee_tracker_ai.h"
#include "game/shots.h"
#include "game/sound_module.h"
#include "game/team.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

#include "original_exe.h"

#include "../interface/interface_kit.inc"

namespace H {

int g_side;
template <class T> void Put(void *b, uint32_t o, T v) { std::memcpy(static_cast<uint8_t *>(b) + o, &v, sizeof v); }
template <class T> T At(const void *b, uint32_t o)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(b) + o, sizeof v);
    return v;
}
template <class M> int Slot(M m)   // a port virtual's byte offset in its vtable (g++: the member pointer holds 1 + it)
{
    struct {
        uintptr_t p;
        ptrdiff_t adjust;
    } r;
    static_assert(sizeof r == sizeof m, "member pointer");
    std::memcpy(&r, &m, sizeof r);
    return int(r.p - 1);
}
std::string Hex(uint32_t w)
{
    char t[12];
    std::snprintf(t, sizeof t, "%x", w);
    return t;
}

// ---- the objects ----
alignas(16) C_MeleeTrackerMovement g_mv[2];
C_MeleeTrackerMovement &Mv() { return g_mv[g_side]; }
alignas(16) uint8_t g_tracker[2][0x800];                // [0] the original's layout, [1] the port's C_MeleeTracker
alignas(16) uint8_t g_ai[2][0x100];                     // the controllers (the original's layout on both)
alignas(16) uint8_t g_target[2][0x800];                 // the target actors
void *g_aiVt[2][0x40], *g_targetVt[2][0x80], *g_trackerVt[2][0x80];
// The volumes the collision queries answer with (their flags and types the scenario's).
constexpr int HITVOLS = 4;
alignas(16) S_CollisionVolume g_hitVol[HITVOLS];
alignas(16) uint8_t g_origGame[0x100], g_portGame[0x2000];
uint8_t g_camera[0x10], g_team[0x10], g_sound[0x10], g_player[0x10];
void *g_cameraVt[0x40], *g_soundVt[0x40];
// The port's C_MeleeTracker fields' offsets.
const uint32_t P_FRAME = offsetof(C_MeleeTracker, m_frame), P_POS = offsetof(C_MeleeTracker, m_position),
               P_DIR = offsetof(C_MeleeTracker, m_direction), P_STATS = offsetof(C_MeleeTracker, m_stats),
               P_AI = offsetof(C_MeleeTracker, m_ai);
const uint32_t O_FRAME = 0x20, O_POS = 0x30, O_DIR = 0x40, O_STATS = 0x4A0, O_AI = 0x534;
uint32_t TrackerOff(int side, uint32_t orig, uint32_t port) { return side ? port : orig; }
void *Tracker(int side) { return g_tracker[side]; }

// ---- the frames (the arena): 0 the tracker's model, 1..3 the collision dummies, 4 "anim2", 5..12 the bones, 13 and
// 14 two sectors (Update's) ----
const char *const FRAME_NAMES[] = {"model", "col dummy1", "col dummy2", "col dummy", "anim2", "d", "w", "f", "c", "h",
                                   "p", "t", "l", "sector1", "sector2"};
constexpr int NFRAMES = 15, SECTOR1 = 13;
I3D_frame *Model() { return FrameAt(0); }

// ---- the model's stages and the animation sets (part 80's) ----
constexpr int STAGES = 8;
struct S_Stages {
    int anim[STAGES];
    float weight[STAGES];
    int time[STAGES];
    int flags[STAGES];
    int active[STAGES];
};
S_Stages g_st;
constexpr int SETS = 6;
struct S_FakeSet {
    void **vt;
    int refCount;
    int playMode;
    const char *fileName;
    int endTime;
};
S_FakeSet g_sets[SETS];
void *g_setVt[16];
const char *const SET_NAMES[SETS] = {"anims\\dog\\walk.i3d", "anims\\dog\\run.i3d", "bite.i3d", "anims\\dog\\sit.i3d",
                                     "anims\\idle.i3d", nullptr};
const int SET_ENDS[SETS] = {900, 640, 1, 2400, 333, 1700};
int SetIndex(const void *p)
{
    for (int i = 0; i < SETS; ++i)
        if (p == &g_sets[i])
            return i;
    return p ? -2 : -1;
}
I3D_animation_set *SetAt(int i) { return i < 0 ? nullptr : reinterpret_cast<I3D_animation_set *>(&g_sets[i]); }

// The names: frames by their role, the side's objects alike.
std::string PartName(const void *p)
{
    const int s = SetIndex(p);
    if (s >= 0)
        return "set" + std::to_string(s);
    for (int i = 0; i < HITVOLS; ++i)
        if (p == &g_hitVol[i])
            return "hit" + std::to_string(i);
    for (int side = 0; side < 2; ++side) {
        if (p == &g_mv[side])
            return "movement";
        if (p == g_tracker[side])
            return "tracker";
        if (p == g_ai[side])
            return "ai";
        if (p == g_target[side])
            return "target";
        for (int i = 0; i < 2; ++i)
            if (p && p == g_mv[side].m_volumeA[i])
                return "volA" + std::to_string(i);
        for (int i = 0; i < 3; ++i)
            if (p && p == g_mv[side].m_volumeB[i])
                return "volB" + std::to_string(i);
    }
    if (p == g_camera)
        return "camera";
    if (p == g_team)
        return "playerTeam";
    if (p == g_sound)
        return "soundModule";
    const int f = FrameIndex(p);
    if (f >= 0 && f < NFRAMES)
        return FRAME_NAMES[f];
    return "";
}

uint32_t __stdcall S_Release(void *self)
{
    Log("%s Release", Name(self).c_str());
    return uint32_t(--static_cast<S_FakeSet *>(self)->refCount);
}
int __stdcall S_EndTime(void *self) { return static_cast<S_FakeSet *>(self)->endTime; }
void __stdcall S_Other(void *self) { Log("%s set other slot", Name(self).c_str()); }
bool StageOk(int stage)
{
    if (stage >= 0 && stage < STAGES)
        return true;
    Log("bad stage %d", stage);
    return false;
}
void __stdcall M_Tick(void *f, int time) { Log("%s Tick %d", Name(f).c_str(), time); }
void __stdcall M_SetCallback(void *f, void *cb, uint32_t messages)
{
    const bool hook = cb == reinterpret_cast<void *>(&C_MeleeTrackerAnimMixer::NoteHook) || cb == OriginalAt<void *>(0x005c7960);
    Log("%s SetCallback %s %x", Name(f).c_str(), cb ? hook ? "hook" : "other" : "null", messages);
    static_cast<I3D_frame *>(f)->m_callback = hook ? &C_MeleeTrackerAnimMixer::NoteHook : reinterpret_cast<I3D_CALLBACK *>(cb);
}
void __stdcall M_SetAnimation(void *f, void *anim, int stage, float weight, int flags)
{
    Log("%s SetAnimation %s %d %s %d", Name(f).c_str(), Name(anim).c_str(), stage, FloatText(weight).c_str(), flags);
    if (!StageOk(stage))
        return;
    g_st.anim[stage] = SetIndex(anim);
    g_st.weight[stage] = weight;
    g_st.time[stage] = 0;
    g_st.flags[stage] = flags;
    g_st.active[stage] = anim != nullptr;
}
void *__stdcall M_GetAnimation(void *, int stage) { return StageOk(stage) ? SetAt(g_st.anim[stage]) : nullptr; }
void __stdcall M_StopAnimation(void *f, int stage)
{
    Log("%s StopAnimation %d", Name(f).c_str(), stage);
    if (StageOk(stage))
        g_st.active[stage] = 0;
}
void __stdcall M_SetAnimWeight(void *f, int stage, float weight)
{
    Log("%s SetAnimWeight %d %s", Name(f).c_str(), stage, FloatText(weight).c_str());
    if (StageOk(stage))
        g_st.weight[stage] = weight;
}
float __stdcall M_GetAnimWeight(void *, int stage) { return StageOk(stage) ? g_st.weight[stage] : 0.0f; }
void __stdcall M_SetAnimTime(void *f, int stage, int time)
{
    Log("%s SetAnimTime %d %d", Name(f).c_str(), stage, time);
    if (StageOk(stage))
        g_st.time[stage] = time;
}
int __stdcall M_GetAnimTime(void *, int stage) { return StageOk(stage) ? g_st.time[stage] : 0; }
bool __stdcall M_IsAnimActive(void *, int stage) { return StageOk(stage) && g_st.active[stage] != 0; }
void __stdcall M_AdvanceStages(void *f, const int *times)
{
    std::string t;
    for (int i = 0; i < STAGES; ++i) {
        t += " " + std::to_string(times[i]);
        if (g_st.anim[i] >= 0)
            g_st.time[i] += times[i];
    }
    Log("%s AdvanceStages%s", Name(f).c_str(), t.c_str());
}

// ---- the world's answers ----
// The collision queries' answers: a scripted stream each scenario, taken in call order on both sides.
constexpr int ANSWERS = 64;
struct S_Answer {
    bool hit;
    int vol;                                            // g_hitVol
    float frac;                                         // where along the ray / the move it stops
    S_vector normal;
    int count;                                          // MoveSpheres: the contacts
    float posW;                                         // MoveSpheres: the end's w
    bool bend;                                          // MoveSpheres: changes the move it was given
    float lift;                                         // MoveSpheres: ends this much higher (a step climbed)
};
struct S_World {
    alignas(16) uint8_t mv[sizeof(C_MeleeTrackerMovement)];
    bool volumes;                                       // the movement's volumes allocated
    int volumeGap;                                      // one slot left empty (-1: none)
    S_Stages stages;
    bool frames[NFRAMES];                               // FindChildFrame finds it
    bool dummy[NFRAMES];                                // of the dummy type
    S_vector4 pos, dir;
    int health, biteDamage;
    bool hasAi;
    int aiMode;
    bool hasTarget;
    float targetDistance;
    S_MeleeTrackerAnim anims[0x17];
    int playMode[SETS];
    int cooldown;
    uint32_t role;
    bool sphereHit;
    int material[0x60];                                 // the footstep groups (the materials' +0x24 word)
    uint32_t matWord[0x60];                             // their +0x20 word
    std::vector<uint8_t> frames0;                       // the arena's frames (made once: both sides the same)
    uint32_t matCount;                                  // g_materialCount
    uint32_t hitFlags[HITVOLS], hitType[HITVOLS];
    S_Answer answers[ANSWERS];
    float iterations;                                   // g_moveIterations
};
S_World g_w;
int g_answerAt;
bool g_queryStream;                                     // the sphere test answers from the stream (81b's operations)
const S_Answer &NextAnswer() { return g_w.answers[g_answerAt++ % ANSWERS]; }
int g_soundHandle;
std::vector<uint8_t> g_materialTable;

I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    if (f != Model())
        return nullptr;
    for (int i = 1; i < NFRAMES; ++i)
        if (std::strcmp(name, FRAME_NAMES[i]) == 0)
            return g_w.frames[i] ? FrameAt(i) : nullptr;
    return nullptr;
}

// ---- the fakes (both sides' logic; O_: the original's entry points) ----
std::string VolumeText(const S_CollisionVolume *v)
{
    if (!v)
        return "null";
    return Name(v) + " flags " + Hex(v->flags) + " frame " + Name(v->frame) + " type " + Hex(v->type) + " actor " +
           Name(v->actor) + " c " + VecText(v->center) + " r " + FloatText(v->radius);
}
void F_AddVolume(S_CollisionVolume *v) { Log("AddVolume %s", VolumeText(v).c_str()); }
// (A null volume not logged: the original's SetMode(5) removes its slots unchecked, the port's skips empty ones.)
void F_RemoveVolume(S_CollisionVolume *v)
{
    if (v)
        Log("RemoveVolume %s", VolumeText(v).c_str());
}
void F_MoveVolume(S_CollisionVolume *v) { Log("MoveVolume %s", VolumeText(v).c_str()); }
S_CollisionVolume *F_SphereTest(const S_vector &from, const S_vector &dir, float radius, S_vector *hit, S_vector *normal,
                                uint32_t types, uint32_t flags)
{
    Log("SphereTest %s %s %s %x %x", VecText(from).c_str(), VecText(dir).c_str(), FloatText(radius).c_str(), types, flags);
    if (g_queryStream) {
        const S_Answer &a = NextAnswer();
        Log("-> %s %s", a.hit ? Name(&g_hitVol[a.vol]).c_str() : "none", FloatText(a.frac).c_str());
        if (!a.hit)
            return nullptr;
        if (hit)
            *hit = S_vector(from.x + dir.x * a.frac, from.y + dir.y * a.frac, from.z + dir.z * a.frac);
        if (normal)
            *normal = a.normal;
        return &g_hitVol[a.vol];
    }
    if (!g_w.sphereHit)
        return nullptr;
    if (hit)
        *hit = S_vector(from.x, from.y - 0.4f, from.z);
    if (normal)
        *normal = S_vector(0.0f, 1.0f, 0.0f);
    return reinterpret_cast<S_CollisionVolume *>(g_camera);   // (any volume: only its being there is read)
}
S_CollisionVolume *F_RayTest(const S_vector &from, const S_vector &dir, S_vector *hit, S_vector *normal, uint32_t types,
                             uint32_t flags)
{
    const S_Answer &a = NextAnswer();
    Log("RayTest %s %s %x %x -> %s %s", VecText(from).c_str(), VecText(dir).c_str(), types, flags,
        a.hit ? Name(&g_hitVol[a.vol]).c_str() : "none", FloatText(a.frac).c_str());
    if (!a.hit)
        return nullptr;
    if (hit)
        *hit = S_vector(from.x + dir.x * a.frac, from.y + dir.y * a.frac, from.z + dir.z * a.frac);
    if (normal)
        *normal = a.normal;
    return &g_hitVol[a.vol];
}
// The body sweep: its request logged (the spheres' and the move's w are stack words in the original's), then a
// scripted end and contacts.
S_CollisionVolume *F_MoveSpheres(S_MoveContacts &c, float iterations)
{
    std::string s = "MoveSpheresStepping it " + FloatText(iterations) + " n " + std::to_string(c.sphereCount);
    for (uint32_t i = 0; i < c.sphereCount && i < MOVE_CONTACTS_MAX; ++i)
        s += " | " + FloatText(c.radii[i]) + " " + VecText(S_vector(c.spheres[i].x, c.spheres[i].y, c.spheres[i].z));
    s += " | move " + VecText(S_vector(c.move.x, c.move.y, c.move.z)) + " ex " + Hex(c.excludeFlags) + " " +
         Hex(c.excludeTypes) + " | pos " + VecText(S_vector(c.pos.x, c.pos.y, c.pos.z)) + " n0 " +
         VecText(S_vector(c.normals[0].x, c.normals[0].y, c.normals[0].z)) + " count " + std::to_string(c.count);
    const S_Answer &a = NextAnswer();
    s += " -> " + (a.hit ? Name(&g_hitVol[a.vol]) : std::string("none")) + " " + std::to_string(a.count) + " " +
         FloatText(a.frac) + (a.bend ? " bend" : "") + (a.lift != 0.0f ? " lift " + FloatText(a.lift) : std::string());
    Log("%s", s.c_str());
    c.count = a.count;
    for (int k = 0; k < a.count; ++k) {
        const S_vector n = k ? NextAnswer().normal : a.normal;
        c.normals[k] = {n.x, n.y, n.z, 0.0f};
        c.volumes[k] = &g_hitVol[(a.vol + k) % HITVOLS];
        c.sphere[k] = k & 1;
    }
    c.pos = {c.spheres[0].x + c.move.x * a.frac, c.spheres[0].y + (a.lift != 0.0f ? a.lift : c.move.y * a.frac),
             c.spheres[0].z + c.move.z * a.frac, a.posW};
    if (a.bend)                                         // (Move reads its own copy of the move, not this one)
        c.move.y = c.move.y * 0.5f - 0.25f;
    return a.hit ? &g_hitVol[a.vol] : nullptr;
}
void F_IgnoreVolumes(S_CollisionVolume **v, uint32_t count, int unknown)
{
    const int at = int(reinterpret_cast<uint8_t *>(v) - reinterpret_cast<uint8_t *>(&Mv()));
    Log("SetIgnoredVolumes +%x %s %s %u %d", at, Name(v[0]).c_str(), Name(v[1]).c_str(), count, unknown);
}
void F_IgnoreVolume(S_CollisionVolume *v, int unknown) { Log("SetIgnoredVolume %s %d", Name(v).c_str(), unknown); }
void F_DebugLine(const float *from, const float *to, uint32_t color, uint32_t color2)
{
    // (The end's w: a stack word in the original's.)
    Log("DebugLine %s %s %s %x %x", VecText(S_vector(from[0], from[1], from[2])).c_str(), FloatText(from[3]).c_str(),
        VecText(S_vector(to[0], to[1], to[2])).c_str(), color, color2);
}
std::string RequestText(const S_SoundRequest &r)
{
    // (The w only for the footsteps (chapter 0): the mouth's sounds leave a stack word there.)
    return VecText(r.pos) + " " + (r.chapter == 0 ? FloatText(r.posW) : std::string("-")) + " flags " + Hex(r.flags) + " attach " + Name(r.attachTo) + " source " +
           Name(r.source) + " chapter " + std::to_string(r.chapter) + " group " + std::to_string(r.group) + " file " +
           (r.fileName ? r.fileName : "null") + " " + FloatText(r.nearRange) + " " + FloatText(r.farRange) + " ch " +
           std::to_string(r.channel) + " " + FloatText(r.nearScale) + " " + FloatText(r.farScale) + " " + FloatText(r.volume);
}
int F_Play(const S_SoundRequest &r)
{
    Log("SoundPlay %s -> %d", RequestText(r).c_str(), g_soundHandle);
    return g_soundHandle++;
}
void F_Stop(int *slot)
{
    const int at = int(reinterpret_cast<uint8_t *>(slot) - reinterpret_cast<uint8_t *>(&Mv()));
    Log("SoundStop +%x %d", at, *slot);
    *slot = -1;
}
#define TC __attribute__((thiscall))
void TC O_AddVolume(void *, S_CollisionVolume *v) { F_AddVolume(v); }
void TC O_RemoveVolume(void *, S_CollisionVolume *v) { F_RemoveVolume(v); }
void TC O_MoveVolume(void *, S_CollisionVolume *v) { F_MoveVolume(v); }
S_CollisionVolume *TC O_SphereTest(void *, const S_vector *from, const S_vector *dir, float radius, S_vector *hit, S_vector *normal,
                                   uint32_t types, uint32_t flags)
{
    return F_SphereTest(*from, *dir, radius, hit, normal, types, flags);
}
S_CollisionVolume *TC O_RayTest(void *, const S_vector *from, const S_vector *dir, S_vector *hit, S_vector *normal, uint32_t types,
                                uint32_t flags)
{
    return F_RayTest(*from, *dir, hit, normal, types, flags);
}
S_CollisionVolume *TC O_MoveSpheres(void *, S_MoveContacts *c) { return F_MoveSpheres(*c, *OriginalAt<float *>(0x008441e4)); }
void TC O_IgnoreVolumes(void *, S_CollisionVolume **v, uint32_t count, int unknown) { F_IgnoreVolumes(v, count, unknown); }
void TC O_IgnoreVolume(void *, S_CollisionVolume *v, int unknown) { F_IgnoreVolume(v, unknown); }
void __attribute__((fastcall)) O_DebugLine(const float *from, const float *to, uint32_t color, uint32_t color2)
{
    F_DebugLine(from, to, color, color2);
}
// The original's Move with its stack zeroed first: it reads stack words it never wrote (the slope bundle's normal,
// the spheres' w); the port takes them as 0 (the normal: the slope-slide's "moving" path off).
__attribute__((noinline)) void ZeroStack()
{
    uint8_t buf[0x2000];
    std::memset(buf, 0, sizeof buf);
    asm volatile("" : : "r"(buf) : "memory");
}
void TC O_MoveZeroed(void *self, int timeDelta)
{
    ZeroStack();
    OriginalAt<void(TC *)(void *, int)>(0x005c3420)(self, timeDelta);
}
void *__cdecl O_GetSoundModule() { return g_sound; }
int TC O_Play(void *, const S_SoundRequest *r) { return F_Play(*r); }
void TC O_Stop(void *, int *slot) { F_Stop(slot); }
void *__cdecl O_GetGame() { return g_origGame; }
void *TC O_GetCameraModule(void *) { return g_camera; }
void TC O_SetViewDir(void *, const S_vector4 *dir) { Log("SetViewDir %s", VecText(S_vector(dir->x, dir->y, dir->z)).c_str()); }
void __cdecl O_Print(void *, const char *format, ...) { Log("Print %s", format); }
void TC O_MouseLook(void *self, int dx, int dy) { Log("%s MouseLook %d %d", Name(self).c_str(), dx, dy); }
void *TC O_GetPlayerTeam(void *) { return g_team; }
void TC O_AddSeen(void *self, void *tracker) { Log("%s AddSeenMeleeTracker %s", Name(self).c_str(), Name(tracker).c_str()); }
void *__cdecl O_AlignedMalloc(size_t size, size_t align) { return _aligned_malloc(size, align); }
void __cdecl O_AlignedFree(void *p)
{
    if (p)   // (death frees its empty volume slots too, in the original: a no-op)
        Log("free %s", Name(p).c_str());
    _aligned_free(p);
}
void PortAlignedFree(void *p)
{
    if (p)   // (death frees its empty volume slots too, in the original: a no-op)
        Log("free %s", Name(p).c_str());
    _aligned_free(p);
}
// The controller's and the target's answers (each side's vtable).
C_Actor *TC V_GetTarget(void *) { return g_w.hasTarget ? reinterpret_cast<C_Actor *>(g_target[g_side]) : nullptr; }
float TC V_Distance(void *) { return g_w.targetDistance; }
void TC V_Hit(void *self, int type, float amount, const S_vector *pos, const S_vector *dir, C_Actor *attacker, int flags)
{
    Log("%s Hit %d %s %s %s %s %d", Name(self).c_str(), type, FloatText(amount).c_str(), VecText(*pos).c_str(), VecText(*dir).c_str(),
        Name(attacker).c_str(), flags);
}
void TC V_Other(void *self) { Log("%s other slot", Name(self).c_str()); }
// The scene's UpdateFrameSector (0xEC) and GetSector (0xE0).
void __stdcall SC_UpdateFrameSector(void *, void *frame, const S_vector &pos)
{
    Log("scene UpdateFrameSector %s %s", Name(frame).c_str(), VecText(pos).c_str());
}
void *__stdcall SC_GetSector(void *, const S_vector &pos)
{
    // (The two sectors split by x; none far out in z.)
    void *s = std::fabs(pos.z) > 4.0f ? nullptr : FrameAt(pos.x < 0.0f ? SECTOR1 : SECTOR1 + 1);
    Log("scene GetSector %s -> %s", VecText(pos).c_str(), Name(s).c_str());
    return s;
}
int g_mouse[2];
// The animation cache's Get (both sides; part 80's): a fake set by its file name.
HRESULT CacheGet(const char *name, I3D_animation_set **out)
{
    Log("cache Get %s", name);
    *out = nullptr;
    for (int i = 0; i < SETS; ++i) {
        const char *file = SET_NAMES[i];
        if (!file)
            continue;
        const char *slash = std::strrchr(file, '\\');
        if (std::strcmp(slash ? slash + 1 : file, name) == 0 || std::strcmp(file, name) == 0) {
            ++g_sets[i].refCount;
            *out = SetAt(i);
            return I3D_OK;
        }
    }
    return static_cast<HRESULT>(0x80004005);
}
HRESULT TC O_CacheGet(void *, const char *name, I3D_animation_set **out, void *, void *) { return CacheGet(name, out); }
int __stdcall G_MouseX(void *) { return g_mouse[0]; }
int __stdcall G_MouseY(void *) { return g_mouse[1]; }

}  // namespace H

// ---- the port's side of the fakes ----
template <> HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *name, I3D_animation_set **out, T_CacheErrorProc, void *)
{
    return H::CacheGet(name, out);
}
float __stdcall MixerBlendProgress(float elapsed, float step, float duration)
{
    using T = float(__stdcall *)(float, float, float);
    return OriginalAt<T>(0x00772900)(elapsed, step, duration);
}
float AngleBetween2D(const float *a, const float *b)
{
    using T = float(__attribute__((fastcall)) *)(const float *, const float *);   // (a in ECX, b in EDX)
    return OriginalAt<T>(0x00788a10)(a, b);
}
void RotateAboutY(S_vector &v, float angle)
{
    using T = void(__attribute__((thiscall)) *)(S_vector *, float);
    OriginalAt<T>(0x007888f0)(&v, angle);
}
void C_CollisionWorld::AddVolume(S_CollisionVolume *v) { H::F_AddVolume(v); }
void C_CollisionWorld::RemoveVolume(S_CollisionVolume *v) { H::F_RemoveVolume(v); }
void C_CollisionWorld::MoveVolume(S_CollisionVolume *v) { H::F_MoveVolume(v); }
void C_CollisionWorld::SetIgnoredVolume(S_CollisionVolume *v, int unknown) { H::F_IgnoreVolume(v, unknown); }
void C_CollisionWorld::SetIgnoredVolumes(S_CollisionVolume **v, uint32_t count, int unknown) { H::F_IgnoreVolumes(v, count, unknown); }
S_CollisionVolume *C_CollisionWorld::MoveSpheresStepping(S_MoveContacts &c) const { return H::F_MoveSpheres(c, g_moveIterations); }
S_CollisionVolume *CollisionSphereTest(const S_vector &from, const S_vector &dir, float radius, S_vector *hit, S_vector *normal,
                                       uint32_t types, uint32_t flags)
{
    return H::F_SphereTest(from, dir, radius, hit, normal, types, flags);
}
S_CollisionVolume *CollisionRayTest(const S_vector &from, const S_vector &dir, S_vector *hit, S_vector *normal, uint32_t types,
                                    uint32_t flags)
{
    return H::F_RayTest(from, dir, hit, normal, types, flags);
}
void DrawDebugLine(const float *from, const float *to, uint32_t color, uint32_t color2) { H::F_DebugLine(from, to, color, color2); }
C_SoundModule *GetSoundModule() { return reinterpret_cast<C_SoundModule *>(H::g_sound); }
int C_SoundModule::Play(const S_SoundRequest &r) { return H::F_Play(r); }
void C_SoundModule::Stop(int *slot) { H::F_Stop(slot); }
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_portGame); }
C_CameraModule *C_game::GetCameraModule() { return reinterpret_cast<C_CameraModule *>(H::g_camera); }
void C_CameraModule::SetViewDir(const S_vector4 &dir) { H::Log("SetViewDir %s", H::VecText(S_vector(dir.x, dir.y, dir.z)).c_str()); }
void C_game::Print(const char *format, ...) { H::Log("Print %s", format); }
void C_MeleeTrackerPlayerControl::MouseLook(int dx, int dy) { H::Log("%s MouseLook %d %d", H::Name(this).c_str(), dx, dy); }
void C_Team::AddSeenMeleeTracker(C_MeleeTracker *tracker)
{
    H::Log("%s AddSeenMeleeTracker %s", reinterpret_cast<void *>(this) == g_teamManager.GetPlayerTeam() ? "playerTeam" : "?",
           H::Name(tracker).c_str());
}
float __stdcall S_vector::AngleTo(const S_vector &v) const
{
    using T = float(__stdcall *)(const S_vector *, const S_vector &);
    static const T f = reinterpret_cast<T>(H::Ls3df("?AngleTo@S_vector@@QBGMABU1@@Z"));
    return f(this, v);
}
S_vector &__stdcall S_vector::operator*=(const S_matrix &m)
{
    using T = S_vector &(__stdcall *)(S_vector *, const S_matrix &);
    static const T f = reinterpret_cast<T>(H::Ls3df("??XS_vector@@QAGAAU0@ABUS_matrix@@@Z"));
    return f(this, m);
}
void RotateAboutX(S_vector4 &v, float angle)
{
    using T = void(__attribute__((thiscall)) *)(S_vector4 *, float);
    OriginalAt<T>(0x00788950)(&v, angle);
}
S_matrix __stdcall RotateYMatrix(float angle)
{
    using T = S_matrix(__stdcall *)(float);
    static const T f = reinterpret_cast<T>(H::Ls3df("?RotateYMatrix@@YG?AUS_matrix@@M@Z"));
    return f(angle);
}

namespace H {

// ---- the original's entry points ----
using T_V = void(TC *)(void *);
using T_VI = void(TC *)(void *, int);
using T_VII = void(TC *)(void *, int, int);
using T_VP = void(TC *)(void *, const void *);
using T_VPP = void(TC *)(void *, const void *, const void *);
using T_I = int(TC *)(void *);
using T_BII = bool(TC *)(void *, int, int);
using T_PP = void *(TC *)(void *);
using T_RetBP = bool(TC *)(void *, void *);
using T_OneShot = int(TC *)(void *, int);
int __attribute__((fastcall)) (*const AnimClassOrig)(int) = nullptr;

// ---- the chunks and files (part 80's) ----
std::map<std::string, std::string> g_files;
struct S_OpenFile {
    std::string name;
    size_t at = 0;
};
std::map<int, S_OpenFile> g_openFiles;
int g_nextFile = 1;
bool g_fileLog = true;
int FileOpen(const char *name, bool write)
{
    if (!write && !g_files.count(name)) {
        if (g_fileLog)
            Log("dta open %s -> -1", name);
        return -1;
    }
    if (write)
        g_files[name].clear();
    g_openFiles[g_nextFile] = S_OpenFile{name, 0};
    if (g_fileLog)
        Log("dta open %s %d -> %d", name, write, g_nextFile);
    return g_nextFile++;
}
int FileSeek(int handle, int offset, int origin)
{
    if (!g_openFiles.count(handle))
        return -1;
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    o.at = origin == 0 ? size_t(offset) : origin == 1 ? o.at + offset : f.size() + offset;
    return int(o.at);
}
int FileRead(int handle, void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    const int got = o.at >= f.size() ? 0 : std::max(0, std::min(int(f.size() - o.at), size));
    if (got)
        std::memcpy(data, f.data() + o.at, size_t(got));
    o.at += size_t(got);
    return got;
}
int FileWrite(int handle, const void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    std::string &f = g_files[o.name];
    if (o.at + size_t(size) > f.size())
        f.resize(o.at + size_t(size));
    std::memcpy(&f[o.at], data, size_t(size));
    o.at += size_t(size);
    return size;
}
bool FileClose(int handle)
{
    if (g_fileLog)
        Log("dta close %d", handle);
    return g_openFiles.erase(handle) != 0;
}
int __stdcall O_dtaOpen(const char *name, bool) { return FileOpen(name, false); }
int __stdcall O_dtaOpenWrite(const char *name, int) { return FileOpen(name, true); }
int __stdcall O_dtaSeek(int h, int offset, int origin) { return FileSeek(h, offset, origin); }
int __stdcall O_dtaRead(int h, void *data, int size) { return FileRead(h, data, size); }
int __stdcall O_dtaWrite(int h, const void *data, int size) { return FileWrite(h, data, size); }
bool __stdcall O_dtaClose(int h) { return FileClose(h); }
}  // namespace H
int RW_DATA_CALL dtaOpen(const char *name, bool) { return H::FileOpen(name, false); }
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpen(name, true); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaRead(int h, void *data, int size) { return H::FileRead(h, data, size); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
bool RW_DATA_CALL dtaClose(int h) { return H::FileClose(h); }

namespace H {
using T_ChunkOpen = bool(TC *)(void *, const char *, bool);
using T_ChunkClose = bool(TC *)(void *);
using T_ChunkBegin = bool(TC *)(void *, uint16_t);
using T_ChunkWrite = bool(TC *)(void *, const void *, int);
using T_ChunkAscend = bool(TC *)(void *, uint32_t *);
bool CBegin(void *c, uint16_t id) { return g_side ? static_cast<C_chunk *>(c)->Begin(id) : OriginalAt<T_ChunkBegin>(0x00405820)(c, id); }
bool CWrite(void *c, const void *d, int n) { return g_side ? static_cast<C_chunk *>(c)->Write(d, n) : OriginalAt<T_ChunkWrite>(0x004059b0)(c, d, n); }
bool CAscend(void *c) { return g_side ? static_cast<C_chunk *>(c)->Ascend() : OriginalAt<T_ChunkAscend>(0x00405890)(c, nullptr); }
int CPos(const void *c) { return At<bool>(c, 0) ? FileSeek(At<int>(c, 4), 0, 1) : -1; }
int CDepth(const void *c) { return At<int>(c, 0x410); }
struct S_SideChunk {
    alignas(16) uint8_t orig[0x500] = {};
    C_chunk port;
    void *Ptr(int side) { return side ? static_cast<void *>(&port) : static_cast<void *>(orig); }
};
bool ChunkOpen(int side, S_SideChunk &c, const char *name, bool write)
{
    return side ? c.port.Open(name, write) : OriginalAt<T_ChunkOpen>(0x00405620)(c.orig, name, write);
}
void ChunkClose(int side, S_SideChunk &c) { side ? c.port.Close() : OriginalAt<T_ChunkClose>(0x00405750)(c.orig); }

// ---- the world ----
float Fp(std::initializer_list<float> l) { return l.begin()[I(0, int(l.size()) - 1)]; }
int Ip(std::initializer_list<int> l) { return l.begin()[I(0, int(l.size()) - 1)]; }
S_vector4 RandomDir()
{
    S_vector4 v{F(-1, 1), P(0.7) ? 0.0f : F(-0.3f, 0.3f), F(-1, 1), F(-1, 1)};
    if (P(0.1))
        v = {Fp({0.0f, 1.0f, -1.0f}), 0.0f, Fp({0.0f, 1.0f, -1.0f}), 0.0f};
    return v;
}
void RandomSeq(S_MeleeTrackerAnimSequence &s)
{
    for (int &a : s.anims)
        a = P(0.1) ? 0x17 : I(0, 0x16);
    s.animA = P(0.3) ? 0x17 : I(0, 0x16);
    s.animB = I(0, 2);
    s.animC = P(0.8) ? I(0, 1) : Ip({7, 2});
    // (No 0x17 after a 0x17, 5 or 6: StepAnimation's advance reads it as an index, one past the table in both.)
    for (int i = 3; i > 0; --i)
        if ((s.anims[i] == 0x17 || s.anims[i] == 5 || s.anims[i] == 6) && s.anims[i - 1] == 0x17)
            s.anims[i - 1] = I(0, 4);
    s.readIndex = P(0.3) ? -1 : I(0, 3);
    s.endIndex = P(0.3) ? -1 : I(0, 4);
}
void RandomMixer(C_MeleeTrackerAnimMixer &x)
{
    x.m_model = reinterpret_cast<I3D_model *>(Model());
    for (S_MeleeTrackerMixerTrack &t : x.m_tracks) {
        t.current = P(0.3) ? -1 : I(0, 1);
        t.time = P(0.1) ? -1 : I(0, 2500);
        t.hold = P(0.7) ? 0 : I(-1, 900);
        t.eventCount = I(0, 3);
        for (int &e : t.events)
            e = I(0, 3);
        t.length = I(0, 2500);
        t.reversed = uint8_t(P(0.2));
    }
    for (S_MeleeTrackerMixerStage &s : x.m_stages) {
        s.weight = P(0.3) ? 1.0f : F(0, 1);
        s.blendTime = P(0.2) ? 0.0f : F(1, 500);
        s.blendElapsed = P(0.3) ? MELEE_TRACKER_MIXER_NO_BLEND : F(0, 600);
        s.crossfade = uint8_t(P(0.5));
        s.flags = P(0.7) ? 0 : MELEE_TRACKER_MIXER_STAGE_FADE_OUT;
        s.rate = P(0.6) ? 1.0f : F(0.2f, 2.0f);
        s.finished = uint8_t(P(0.3));
        s.tag = I(-1, 3);
    }
    x.m_speedPrevious = F(0, 6);
    x.m_speedCurrent = F(0, 6);
    x.m_addMoveDuration = P(0.5) ? MELEE_TRACKER_MOVE_NO_TIME : I(1, 600);
    x.m_addMoveTime = I(0, 600);
    x.m_slideDuration = P(0.5) ? MELEE_TRACKER_MOVE_NO_TIME : I(1, 800);
    x.m_slideTime = I(0, 500);
    x.m_slideStepTime = I(0, 79);
    x.m_stageFinished = uint8_t(P(0.3));
    // The last move (Move's root motion): still, tiny (1e-16 squared), straight down, or any.
    const float w = F(-1, 1);
    if (P(0.25))
        x.m_move = {0.0f, 0.0f, 0.0f, w};
    else if (P(0.1))
        x.m_move = {Fp({0.0f, 5e-9f, 1e-8f}), Fp({0.0f, -7e-9f}), Fp({0.0f, 5e-9f, -1.2e-8f}), w};
    else if (P(0.15))
        x.m_move = {0.0f, F(-0.5f, 0.2f), 0.0f, w};
    else
        x.m_move = {F(-0.4f, 0.4f), P(0.4) ? 0.0f : F(-0.4f, 0.3f), F(-0.4f, 0.4f), w};
}
// A contact's or a ray's surface normal: up, at the floor tests' edges, with a short horizontal part, or any.
S_vector RandomNormal()
{
    if (P(0.25))
        return S_vector(0.0f, 1.0f, 0.0f);
    if (P(0.25)) {
        const float y = Fp({0.707f, std::nextafter(0.707f, 1.0f), 0.5f, 0.95f, 0.55f, 0.0f, -0.3f});
        const float h = std::sqrt(std::max(0.0f, 1.0f - y * y)), a = F(0, 6.2831853f);
        return S_vector(h * std::cos(a), y, h * std::sin(a));
    }
    if (P(0.1))
        return S_vector(Fp({0.0f, 1e-5f, -2e-5f}), F(-1, 1), Fp({0.0f, -1e-5f, 3e-5f}));
    const S_vector n(F(-1, 1), F(-1, 1), F(-1, 1));
    const float l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    return l > 1e-3f ? S_vector(n.x / l, n.y / l, n.z / l) : S_vector(0.0f, 1.0f, 0.0f);
}
void SetFrames();
void RandomWorld()
{
    C_MeleeTrackerMovement &m = *reinterpret_cast<C_MeleeTrackerMovement *>(g_w.mv);
    std::memset(g_w.mv, 0, sizeof g_w.mv);
    m.m_vtable = reinterpret_cast<void *>(0x008121c4);
    RandomMixer(m.m_mixer);
    m.m_mixer.m_vtable = reinterpret_cast<void *>(0x008121d0);
    // (Always there: the original's Init, PlayAnimSound and UpdateAnimation write through it unchecked.)
    m.m_animFrame = FrameAt(4);
    m.m_168 = P(0.5) ? nullptr : reinterpret_cast<void *>(uintptr_t(U32() & 0xFFFF));
    m.m_moveDir = RandomDir();
    m.m_facing = RandomDir();
    for (int i = 0; i < 3; ++i) {
        m.m_colFrame[i] = P(0.85) ? FrameAt(1 + i) : nullptr;
        m.m_spherePos[i] = {F(-5, 5), F(-1, 2), F(-5, 5), F(-1, 1)};
        m.m_sphereRadius[i] = F(0.05f, 0.4f);
    }
    m.m_material = P(0.2) ? -1 : I(0, 0x5F);
    m.m_1f4 = P(0.5) ? uint32_t(I(0, 300)) : P(0.3) ? uint32_t(I(9000, 12000)) : U32();
    m.m_1f8 = P(0.4) ? -10000.1f : F(-3, 3);
    m.m_1fc = P(0.2) ? Fp({0.0f, 0.9f, 1.0f}) : F(0, 1);
    m.m_200 = F(0, 2);
    m.m_210 = RandomDir();
    m.m_220 = RandomDir();
    m.m_noCollision = P(0.3);
    m.m_turned = uint8_t(P(0.5));
    m.m_moved = uint8_t(P(0.5));
    m.m_234 = U32() & 0xFF;
    m.m_238 = U32() & 0xFF;
    m.m_23c = U32() & 0xFF;
    m.m_240 = P(0.5) ? 0 : I(-1, 6);
    m.m_speed = P(0.4) ? 1.0f : F(0, 2);
    m.m_speedTarget = P(0.3) ? m.m_speed : P(0.4) ? 1.0f : F(0, 2);
    m.m_airSteps = P(0.5) ? 0 : I(1, 8);
    m.m_250 = P(0.3) ? 0.0f : P(0.15) ? Fp({9.81f, 19.62f}) : F(0, 25);   // (the fall speed)
    m.m_254 = P(0.3) ? 0.0f : F(0, 85);                 // (its 85 ms step phase)
    m.m_258 = P(0.4) ? -10000.1f : F(-2, 2);
    m.m_25c = P(0.5) ? MELEE_TRACKER_MOVE_NO_TIME : P(0.3) ? I(0, 1) : I(0, 900);
    m.m_260 = P(0.5);
    m.m_264 = U32() & 0xFF;
    m.m_bitePending = P(0.3);
    m.m_mode = P(0.1) ? 0x17 : I(0, 6);
    m.m_subModeA = I(0, 2);
    m.m_subModeB = P(0.8) ? I(0, 1) : 7;
    m.m_modePriority = I(-1, 3);
    m.m_subModeAPriority = I(-1, 3);
    m.m_subModeBPriority = I(-1, 3);
    RandomSeq(m.m_currentSeq);
    // (The queue's entries filled, as the pushes leave them: StepAnimation reads an entry's anims[readIndex] as a
    // table index, readIndex -1 an unrelated field.)
    for (S_MeleeTrackerAnimSequence &s : m.m_queue) {
        RandomSeq(s);
        s.readIndex = I(0, 3);
        s.endIndex = I(0, s.readIndex);
    }
    m.m_35c = P(0.4) ? -1 : I(0, 4);
    m.m_360 = m.m_35c == -1 ? (P(0.5) ? -1 : I(0, 4)) : (P(0.4) ? m.m_35c : I(0, 4));
    if (P(0.3)) {
        // (BuildSequence's sub-mode A transitions and its mode 2 choice: the sequences' animation of the mode's
        // class, their sub-mode not 2.)
        m.m_mode = I(0, 2);
        const auto pick = [&] { return m.m_mode == 0 ? Ip({0, 2}) : m.m_mode == 1 ? Ip({1, 3, 4}) : Ip({5, 6}); };
        m.m_currentSeq.animA = pick();
        m.m_currentSeq.animB = I(0, 1);
        for (S_MeleeTrackerAnimSequence &s : m.m_queue) {
            s.animA = pick();
            s.animB = I(0, 1);
        }
    }
    m.m_364 = P(0.2) ? 0x17 : P(0.3) ? Ip({5, 6}) : I(0, 0x16);
    m.m_368 = P(0.3) ? 0x17 : I(0, 0x16);
    m.m_36c = uint8_t(P(0.4));
    m.m_370 = P(0.5) ? 0 : P(0.2) ? Ip({1, -1}) : I(-100, 600);
    m.m_374 = P(0.7) ? 0 : I(1, 2);
    m.m_378 = P(0.5) ? MELEE_TRACKER_MOVE_NO_TIME : P(0.2) ? I(-5, 1) : I(0, 5000);
    for (int &h : m.m_soundHandle)
        h = P(0.5) ? -1 : I(0, 50);
    m.m_388 = P(0.3) ? 0 : I(0, 300);
    m.m_38c = P(0.4) ? -1 : P(0.2) ? 0 : I(1, 0xA6);
    m.m_prevNetPos = {F(-5, 5), F(-1, 2), F(-5, 5), 1.0f};
    m.m_prevFramePos = {F(-5, 5), F(-1, 2), F(-5, 5), 1.0f};
    m.m_netPos = {F(-5, 5), F(-1, 2), F(-5, 5), 1.0f};
    m.m_3c0 = uint8_t(P(0.4));
    m.m_netVelocity = {F(-3, 3), F(-1, 1), F(-3, 3), F(-1, 1)};
    m.m_3e0 = P(0.5) ? 0 : I(1, 3);
    m.m_3e4 = uint8_t(P(0.3));
    m.m_3e8 = FrameAt(7);
    m.m_bodyFrame = FrameAt(Ip({5, 6}));                // (these two always: CheckGround reads them unchecked)
    m.m_3f0 = FrameAt(8);
    m.m_3f4 = FrameAt(9);
    m.m_3f8 = FrameAt(10);
    m.m_3fc = FrameAt(11);
    m.m_400 = FrameAt(12);
    m.m_groundFrame = FrameAt(Ip({6, 5}));             // (Init finds its own: "d" 5, "w" 6)
    g_w.volumes = P(0.85);
    g_w.volumeGap = P(0.85) ? -1 : I(0, 4);             // (Move's and its helpers' tests of the first slot)
    for (int i = 0; i < STAGES; ++i) {
        g_w.stages.anim[i] = P(0.3) ? -1 : I(0, SETS - 1);
        g_w.stages.weight[i] = P(0.2) ? 0.0f : F(0, 1);
        g_w.stages.time[i] = I(0, 2500);
        g_w.stages.flags[i] = I(0, 3);
        g_w.stages.active[i] = P(0.8);
    }
    for (int i = 1; i < NFRAMES; ++i) {
        g_w.frames[i] = i == 4 || i == 5 || i == 6 || P(0.9);
        g_w.dummy[i] = i <= 3 ? P(0.9) : P(0.1);
    }
    g_w.pos = {F(-5, 5), F(-1, 2), F(-5, 5), 1.0f};
    g_w.dir = RandomDir();
    if (P(0.1)) {                                       // (a short horizontal part: HandleAnimEvents' snaps)
        g_w.dir = {Fp({0.0f, 1e-5f, -1e-5f}), Fp({0.3f, -0.5f}), Fp({0.0f, 2e-5f, -2e-5f}), 0.0f};
        if (P(0.6)) {                                   // (the lie-down slide that reads them: 0.8 times x, z)
            m.m_364 = 5;
            m.m_currentSeq.animB = 1;
            for (S_MeleeTrackerAnimSequence &s : m.m_queue)
                s.animB = 1;
            S_MeleeTrackerMixerTrack &t = m.m_mixer.m_tracks[I(0, 1)];   // (a lie-down note to read)
            t.eventCount = I(1, 3);
            t.events[0] = 1;
        }
    }
    if (P(0.15)) {
        // (The facing a hair off the direction: TurnToFacing's 1e-4 angle test.)
        const float a = Fp({5e-5f, -5e-5f, 1.2e-4f, -1.2e-4f, 3e-4f, 0.0f});
        m.m_facing = {g_w.dir.x * std::cos(a) + g_w.dir.z * std::sin(a), g_w.dir.y,
                      g_w.dir.z * std::cos(a) - g_w.dir.x * std::sin(a), F(-1, 1)};
    }
    if (P(0.1)) {
        // (A speed step exactly a time step's seconds: UpdateAnimation's <=.)
        m.m_speed = 0.0f;
        m.m_speedTarget = (P(0.5) ? 1.0f : -1.0f) * (static_cast<float>(Ip({1, 10, 100})) * 0.001f);
    }
    if (P(0.15)) {
        // (Whole-number net positions: a net step exactly 1 long, SafeLength's unit case.)
        m.m_prevNetPos = {float(I(-5, 5)), float(I(-1, 2)), float(I(-5, 5)), 1.0f};
        m.m_prevFramePos = P(0.5) ? m.m_prevNetPos : S_vector4{float(I(-5, 5)), float(I(-1, 2)), float(I(-5, 5)), 1.0f};
    }
    g_w.health = P(0.2) ? I(-10, 0) : I(1, 250);
    g_w.biteDamage = I(0, 80);
    g_w.hasAi = true;                                   // (the original reads the controller unchecked)
    g_w.aiMode = Ip({1, 4, 5, 7, 0});
    g_w.hasTarget = P(0.6);
    g_w.targetDistance = P(0.1) ? 3.4028235e+38f : P(0.1) ? Fp({1.8f, 2.1f, 1.65f}) : F(0, 5);
    for (S_MeleeTrackerAnim &a : g_w.anims) {
        a.anim = P(0.15) ? nullptr : SetAt(I(0, SETS - 1));
        a.blendTime = P(0.2) ? 0 : I(0, 600);
        a.speed = P(0.3) ? 0.0f : F(0, 6);
    }
    for (int &p : g_w.playMode)
        p = I(1, 2);
    g_w.cooldown = P(0.7) ? 0 : I(1, 3);
    g_w.role = Pick({7u, 1u, 2u});
    g_w.sphereHit = P(0.5);
    g_w.matCount = P(0.8) ? 0x60 : uint32_t(I(0x20, 0x5F));
    for (int i = 0; i < HITVOLS; ++i) {
        g_w.hitFlags[i] = (uint32_t(I(0, 0x5F)) << 24) | (P(0.5) ? 0x40u : 0u) | (U32() & 0x3Fu);
        g_w.hitType[i] = (P(0.5) ? 0x100u : 0u) | (U32() & 0x80002u);
    }
    for (S_Answer &a : g_w.answers) {
        a.hit = P(0.6);
        a.vol = I(0, HITVOLS - 1);
        a.frac = P(0.3) ? Fp({0.0f, 0.5f, 1.0f}) : F(0, 1);
        a.normal = RandomNormal();
        a.count = a.hit ? I(1, 3) : P(0.15) ? I(1, 2) : 0;
        a.posW = F(-1, 1);
        a.bend = P(0.1);
        a.lift = P(0.2) ? Fp({0.2f, 0.25f, 0.5f, 0.8f, 1.5f}) : 0.0f;
    }
    g_w.iterations = P(0.5) ? 10.0f : F(1, 10);
    for (int i = 0; i < 0x60; ++i) {
        g_w.material[i] = P(0.3) ? -1 : I(0, 40);
        g_w.matWord[i] = P(0.3) ? uint32_t(-1) : U32() & 0x3F;
    }
    SetFrames();
    g_w.frames0.assign(g_arena.frames[0], g_arena.frames[0] + NFRAMES * FRAME_BYTES);
}
void SetFrames()
{
    for (int i = 0; i < NFRAMES; ++i) {
        RandomFrame(i);
        I3D_frame *f = FrameAt(i);
        f->m_type = g_w.dummy[i] ? FRAME_DUMMY : FRAME_VISUAL;
        f->m_parent = i ? Model() : nullptr;
        // (A world matrix's last column as the engine keeps it (row 3's w the footstep's), a dummy's box: the
        // body spheres' radius.)
        f->m_worldMatrix.m[3][3] = 1.0f;
        const float lo = F(-0.4f, 0.0f), hi = F(0.0f, 0.4f);
        const S_vector4 boxMin = {lo, F(-0.4f, 0.0f), F(-0.4f, 0.0f), 0.0f}, boxMax = {hi, F(0.0f, 0.4f), F(0.0f, 0.4f), 0.0f};
        std::memcpy(g_arena.frames[i] + 0x120, &boxMin, sizeof boxMin);
        std::memcpy(g_arena.frames[i] + 0x130, &boxMax, sizeof boxMax);
        if (i >= SECTOR1)
            f->m_type = FRAME_SECTOR;
    }
    // The model: in a sector or not; its world position at the tracker's (Update's 1e-4 test), its forward unit,
    // nearly vertical or short (Move's pitch test).
    I3D_frame *model = Model();
    model->m_parent = P(0.6) ? FrameAt(SECTOR1 + I(0, 1)) : P(0.5) ? nullptr : FrameAt(5);
    S_matrix &wm = model->m_worldMatrix;
    if (P(0.35)) {
        const float tiny = P(0.5) ? 0.0f : Fp({0.005f, 0.01f, 0.02f});
        wm.m[3][0] = g_w.pos.x + tiny;
        wm.m[3][1] = g_w.pos.y;
        wm.m[3][2] = g_w.pos.z;
    }
    if (P(0.3))
        wm.m[3][3] = F(-1, 1);
    if (P(0.4)) {
        static const float rows[][3] = {{0, 0, 1}, {0.6f, 0, 0.8f}, {0, 0.005f, 1}, {0.6f, 0.02f, 0.8f}, {0, 1e-5f, 0},
                                        {0, -1e-5f, 0}, {1e-5f, 0, 0}, {0, 0, -1e-5f}, {0, 0, 0}, {0, 2, 0}};
        const auto &r = rows[I(0, 9)];
        wm.m[2][0] = r[0];
        wm.m[2][1] = r[1];
        wm.m[2][2] = r[2];
    }
}
void PutTracker(int side)
{
    uint8_t *t = g_tracker[side];
    std::memset(t, 0, sizeof g_tracker[side]);
    Put(t, 0, static_cast<void *>(g_trackerVt[side]));   // (Update's Hit)
    Put(t, TrackerOff(side, O_FRAME, P_FRAME), Model());
    Put(t, TrackerOff(side, O_POS, P_POS), g_w.pos);
    Put(t, TrackerOff(side, O_DIR, P_DIR), g_w.dir);
    Put(t, TrackerOff(side, O_STATS, P_STATS), g_w.health);
    Put(t, TrackerOff(side, O_STATS, P_STATS) + 4, g_w.biteDamage);
    Put(t, TrackerOff(side, O_AI, P_AI), g_w.hasAi ? static_cast<void *>(g_ai[side]) : nullptr);
    std::memset(g_ai[side], 0, sizeof g_ai[side]);
    Put(g_ai[side], 0, static_cast<void *>(g_aiVt[side]));
    Put(g_ai[side], 0x14, g_w.aiMode);
    std::memset(g_target[side], 0, sizeof g_target[side]);
    Put(g_target[side], 0, static_cast<void *>(g_targetVt[side]));
}
S_CollisionVolume *NewVolume()
{
    void *p = _aligned_malloc(sizeof(S_CollisionVolume), 16);
    std::memset(p, 0, sizeof(S_CollisionVolume));
    return static_cast<S_CollisionVolume *>(p);
}
void FreeVolumes(int side)
{
    for (S_CollisionVolume *&v : g_mv[side].m_volumeA)
        if (v)
            _aligned_free(v), v = nullptr;
    for (S_CollisionVolume *&v : g_mv[side].m_volumeB)
        if (v)
            _aligned_free(v), v = nullptr;
}
void SetWorld(int side)
{
    std::memcpy(static_cast<void *>(&g_mv[side]), g_w.mv, sizeof g_w.mv);
    PutTracker(side);
    C_MeleeTrackerMovement &m = g_mv[side];
    m.m_meleeTracker = reinterpret_cast<C_MeleeTracker *>(g_tracker[side]);
    if (g_w.volumes) {
        uint32_t seed = 77;
        for (int i = 0; i < 5; ++i) {
            S_CollisionVolume *v = NewVolume();
            v->flags = i < 2 ? 0x42 : 0x42000042;
            v->frame = Model();
            v->type = i < 2 ? 0x100001 : 0x100002;
            v->actor = reinterpret_cast<C_Actor *>(g_tracker[side]);
            v->center = S_vector(float(seed % 7), float(i), 0.5f);
            v->radius = 0.1f * float(i + 1);
            seed = seed * 31 + 7;
            (i < 2 ? m.m_volumeA[i] : m.m_volumeB[i - 2]) = v;
            if (i == g_w.volumeGap) {
                _aligned_free(v);
                (i < 2 ? m.m_volumeA[i] : m.m_volumeB[i - 2]) = nullptr;
            }
        }
    }
    g_st = g_w.stages;
    for (int i = 0; i < SETS; ++i) {
        g_sets[i].vt = g_setVt;
        g_sets[i].refCount = 1;
        g_sets[i].playMode = g_w.playMode[i];
        g_sets[i].fileName = SET_NAMES[i];
        g_sets[i].endTime = SET_ENDS[i];
    }
    std::memcpy(g_meleeTrackerAnims, g_w.anims, sizeof g_w.anims);
    std::memcpy(OriginalAt<void *>(0x008ac5c0), g_w.anims, sizeof g_w.anims);
    g_meleeTrackerBarkTurnCooldown = g_w.cooldown;
    *OriginalAt<int *>(0x008ac5b8) = g_w.cooldown;
    g_gameRole = g_w.role;
    *OriginalAt<uint32_t *>(0x00982100) = g_w.role;
    g_materialTable.assign(0x60 * sizeof(S_MaterialDef), 0);
    for (int i = 0; i < 0x60; ++i) {
        Put(g_materialTable.data(), uint32_t(i * sizeof(S_MaterialDef) + 0x20), g_w.matWord[i]);
        Put(g_materialTable.data(), uint32_t(i * sizeof(S_MaterialDef) + 0x24), g_w.material[i]);
    }
    g_materials = reinterpret_cast<S_MaterialDef *>(g_materialTable.data());
    *OriginalAt<void **>(0x0097b9b4) = g_materialTable.data();
    std::memcpy(g_arena.frames, g_w.frames0.data(), g_w.frames0.size());
    g_soundHandle = 100;
    std::srand(1234);
    g_materialCount = g_w.matCount;
    *OriginalAt<uint32_t *>(0x0097b9b8) = g_w.matCount;
    g_moveIterations = g_w.iterations;
    *OriginalAt<float *>(0x008441e4) = g_w.iterations;
    for (int i = 0; i < HITVOLS; ++i) {
        g_hitVol[i] = S_CollisionVolume();
        g_hitVol[i].flags = g_w.hitFlags[i];
        g_hitVol[i].type = g_w.hitType[i];
    }
    g_answerAt = 0;
    g_queryStream = false;
}

// The movement, its volumes, the tracker's position and direction, the model's stages and sets after an operation.
// (The original's stack words left out: the mixer's move w, the net velocity's w.)
bool g_loaded;
bool g_moveRan;                                         // a Move ran: the position's w may be a stack word's
std::string Snap(int side)
{
    C_MeleeTrackerMovement &m = g_mv[side];
    uint8_t b[sizeof(C_MeleeTrackerMovement)];
    std::memcpy(b, static_cast<void *>(&m), sizeof b);
    std::memset(b + 0x160, 0, 4);                       // the tracker (each side's)
    std::memset(b + 0x1DC, 0, 5 * 4);                   // the volumes (each side's: compared below)
    std::memset(b + 0x10 + 0xFC, 0, 4);                 // the mixer's move w
    std::memset(b + 0x3DC, 0, 4);                       // the net velocity's w (SetNetPosition stores a stack word)
    for (uint32_t at : {0x1ACu, 0x1BCu, 0x1CCu, 0x17Cu, 0x21Cu, 0x22Cu})   // the spheres' w (UpdateSpheres), Init's w
        std::memset(b + at, 0, 4);
    if (g_moveRan)   // the w words copied from the tracker's position or direction (a stack word's after Move): the
                     // net positions', the facing's, the move direction's, the mixer's add-move and slide
        for (uint32_t at : {0x39Cu, 0x3ACu, 0x3BCu, 0x18Cu, 0x17Cu, 0x11Cu, 0x13Cu, 0x14Cu})
            std::memset(b + at, 0, 4);
    if (g_loaded)                                       // the loaded vectors' w (LoadGame's stack copies)
        for (uint32_t at : {0x17Cu, 0x18Cu, 0x21Cu, 0x22Cu, 0x39Cu, 0x3ACu, 0x3BCu, 0x10Cu + 0x10, 0x12Cu + 0x10, 0x13Cu + 0x10})
            std::memset(b + at, 0, 4);
    std::string s = "mv";
    for (size_t i = 0; i < sizeof b; i += 4)
        s += " " + Hex(At<uint32_t>(b, uint32_t(i)));
    s += " | tracker " + Name(m.m_meleeTracker);
    for (S_CollisionVolume *v : m.m_volumeA)
        s += " | " + VolumeText(v);
    for (S_CollisionVolume *v : m.m_volumeB)
        s += " | " + VolumeText(v);
    const uint8_t *t = g_tracker[side];
    S_vector4 pos = At<S_vector4>(t, TrackerOff(side, O_POS, P_POS));
    if (g_moveRan)
        pos.w = 0.0f;
    S_vector4 dir = At<S_vector4>(t, TrackerOff(side, O_DIR, P_DIR));
    dir.w = 0.0f;                                       // (TurnToFacing stores a stack word there)
    s += " | pos " + VecText(S_vector(pos.x, pos.y, pos.z)) + " " + FloatText(pos.w) + " dir " + VecText(S_vector(dir.x, dir.y, dir.z)) +
         " " + FloatText(dir.w);
    s += " | stages";
    for (int i = 0; i < STAGES; ++i)
        s += " " + std::to_string(g_st.anim[i]) + " " + FloatText(g_st.weight[i]) + " " + std::to_string(g_st.time[i]) + " " +
             std::to_string(g_st.flags[i]) + " " + std::to_string(g_st.active[i]);
    s += " | sets";
    for (const S_FakeSet &set : g_sets)
        s += " " + std::to_string(set.refCount) + "/" + std::to_string(set.playMode);
    s += " | cooldown " + std::to_string(side ? g_meleeTrackerBarkTurnCooldown : *OriginalAt<int *>(0x008ac5b8));
    s += " | iterations " + FloatText(side ? g_moveIterations : *OriginalAt<float *>(0x008441e4));
    for (int i = 0; i < NFRAMES; ++i) {
        const I3D_frame *f = FrameAt(i);
        s += " | f" + std::to_string(i) + " " + Hex(f->m_frameFlags) + " " + VecText(f->m_localMatrix(3));
    }
    return s;
}

// ---- the operations ----
enum {
    OP_BIND, OP_INIT, OP_RESET, OP_FREE, OP_OPTIMIZED, OP_OPT_CONTROLLER, OP_SET_MODE, OP_SUB_A, OP_SUB_B, OP_SEQ_A,
    OP_SEQ_B, OP_SEQ_C, OP_PUSH, OP_POP_FRONT, OP_POP_BACK, OP_NET_POS, OP_CLEAR_NET, OP_WORLD_DIR, OP_FACING, OP_TURN_TO,
    OP_TURN_DIR, OP_UPDATE_ANIM, OP_EVENTS, OP_APPLY_NET, OP_CHECK_GROUND, OP_BITE, OP_SAVE, OP_LOAD, OP_LOAD_OWN,
    OP_SPHERES, OP_STEP, OP_BUILD, OP_ATTACK, OP_SOUND, OP_BARK, OP_CLASS, OP_ONE_SHOT, OP_LUNGE,
    // (81b's)
    OP_UPDATE, OP_MOVE, OP_CONTACT, OP_ALIGN, OP_SLOPE, OP_GROUNDED, OP_VOLMAT, OP_SLIDE_STEP, OP_GRAVITY, W_ANSWERS, OPS
};
struct S_Op {
    int kind, a, b, t;
    float v[4];
    int mouse[2];
    // (SlopeSlide's and IsGrounded's inputs)
    S_MoveContacts c;
    float normal[4], ground[4], move[4];
    bool moving;
    int hitVol;
};
// SlopeSlide's and IsGrounded's inputs: contacts as a body sweep leaves them, the surface normal, the ground
// sphere and the move near the slope tests' edges.
void RandomContacts(S_Op &op)
{
    S_MoveContacts &c = op.c;
    c = S_MoveContacts();
    c.count = P(0.2) ? 0 : I(1, 3);
    for (int k = 0; k < c.count; ++k) {
        const S_vector n = RandomNormal();
        c.normals[k] = {P(0.03) ? INFINITY : n.x, n.y, n.z, F(-1, 1)};
        c.volumes[k] = &g_hitVol[I(0, HITVOLS - 1)];
        c.sphere[k] = I(0, 1);
    }
    c.sphereCount = uint32_t(I(1, 2));
    const S_vector base(F(-3, 3), F(-1, 2), F(-3, 3));
    c.spheres[0] = {base.x, base.y + (P(0.8) ? F(-1, 1) : Fp({1.0f, -1.0f, 1.5f})), base.z, F(-1, 1)};
    c.pos = {base.x + F(-0.3f, 0.3f), base.y, base.z + F(-0.3f, 0.3f), F(-1, 1)};
    c.move = {P(0.1) ? Fp({0.0f, 1e-5f}) : F(-0.3f, 0.3f), F(-0.3f, 0.1f), P(0.1) ? 0.0f : F(-0.3f, 0.3f), F(-1, 1)};
    const S_vector n = RandomNormal();
    op.normal[0] = n.x;
    op.normal[1] = n.y;
    op.normal[2] = n.z;
    op.normal[3] = F(-1, 1);
    op.ground[0] = c.pos.x + F(-0.2f, 0.2f);
    op.ground[1] = c.pos.y - (P(0.7) ? F(0, 0.3f) : F(-0.3f, 0.3f));
    op.ground[2] = c.pos.z + F(-0.2f, 0.2f);
    op.ground[3] = F(-1, 1);
    op.move[0] = P(0.2) ? Fp({0.0f, 1e-5f}) : F(-0.2f, 0.2f);
    op.move[1] = F(-0.2f, 0.0f);
    op.move[2] = P(0.2) ? 0.0f : F(-0.2f, 0.2f);
    op.move[3] = F(-1, 1);
    op.moving = P(0.75);
    op.hitVol = P(0.85) ? I(0, HITVOLS - 1) : -1;
    if (P(0.5)) {
        // A slope it slides on: hit and moving, the normal tilted along the move, the end above the ground by about
        // 0.2 m/s, the move's horizontal speed about 1 m/s (the vertical / horizontal test).
        op.b |= 1;
        op.moving = true;
        op.hitVol = I(0, HITVOLS - 1);
        const float seconds = float(std::max(op.t, 1)) * 0.001f;
        const float a = F(0, 6.2831853f), speed = P(0.2) ? 0.0f : F(0.2f, 1.6f) * seconds;
        op.move[0] = std::cos(a) * speed;
        op.move[2] = std::sin(a) * speed;
        c.move = {op.move[0] * Fp({1.0f, -1.0f, 0.3f}), F(-0.1f, 0.0f), op.move[2], F(-1, 1)};
        const float ny = Fp({0.55f, 0.6f, 0.8f, 0.95f, 0.98f, 0.3f, 0.5f, 0.7f, -0.1f}), h = std::sqrt(1.0f - ny * ny);
        const float b = P(0.7) ? a : F(0, 6.2831853f);
        op.normal[0] = std::cos(b) * h;
        op.normal[1] = ny;
        op.normal[2] = std::sin(b) * h;
        c.spheres[0].y = c.pos.y + F(-0.5f, 0.5f);
        op.ground[1] = c.pos.y - F(0.05f, 2.0f) * 0.2f * seconds;
    }
}
S_Op RandomOp()
{
    S_Op op{};
    op.kind = P(0.3) ? I(OP_UPDATE, OP_GRAVITY) : I(0, OPS - 1);
    op.a = P(0.2) ? Ip({-1, 0, 5, 6, 7, 0x17, 0x7FFFFFFF}) : I(-1, 0x18);
    op.b = P(0.3) ? Ip({0, 1, -1, 3}) : I(-5, 300);
    op.t = P(0.2) ? Ip({0, 1, 10, 100}) : I(0, 300);    // (a time step: the original reads some unsigned)
    const S_vector4 d = P(0.5) ? RandomDir() : S_vector4{F(-5, 5), F(-1, 2), F(-5, 5), 1.0f};
    op.v[0] = d.x;
    op.v[1] = d.y;
    op.v[2] = d.z;
    op.v[3] = d.w;
    // (Now and then near the model's frame: its forward (SetWorldDir's per-axis tests), its position (a net step of
    // almost nothing: the direction's short cases); the arena holds this scenario's frames.)
    const S_matrix &fm = Model()->m_worldMatrix;
    if ((op.kind == OP_WORLD_DIR || op.kind == OP_NET_POS || op.kind == OP_CLEAR_NET) && P(0.3)) {
        const int row = op.kind == OP_WORLD_DIR ? 2 : 3;
        S_vector f(fm.m[row][0], fm.m[row][1], fm.m[row][2]);
        if (row == 2)
            NormalizeDir(f);
        const float tiny = Fp({0.0f, 1e-5f, -1e-5f, 0.0005f, 0.002f});
        op.v[0] = f.x + (P(0.5) ? tiny : 0.0f);
        op.v[1] = f.y + (P(0.3) ? tiny : 0.0f);
        op.v[2] = f.z + (P(0.5) ? tiny : 0.0f);
    }
    if ((op.kind == OP_NET_POS || op.kind == OP_CLEAR_NET) && P(0.15)) {
        // (Within a hair of the last net position: a step tiny but not 0, the interpolation's steps beyond 2^31.)
        const S_vector4 &prev = reinterpret_cast<const C_MeleeTrackerMovement *>(g_w.mv)->m_prevNetPos;
        op.v[0] = prev.x + Fp({1e-6f, -2e-6f, 0.0f});
        op.v[1] = prev.y;
        op.v[2] = prev.z + Fp({0.0f, 1e-6f});
    }
    const C_MeleeTrackerMovement &wm = *reinterpret_cast<const C_MeleeTrackerMovement *>(g_w.mv);
    if ((op.kind == OP_NET_POS || op.kind == OP_CLEAR_NET) && P(0.15)) {
        // (One axis step on from the last net position: whole numbers give a step exactly 1 long.)
        op.v[0] = wm.m_prevNetPos.x;
        op.v[1] = wm.m_prevNetPos.y;
        op.v[2] = wm.m_prevNetPos.z;
        op.v[I(0, 2)] += P(0.5) ? 1.0f : -1.0f;
    }
    if (op.kind == OP_FACING && P(0.4)) {
        // (The facing itself, or with one component changed: SetFacing's per-component tests, its w.)
        op.v[0] = wm.m_facing.x;
        op.v[1] = wm.m_facing.y;
        op.v[2] = wm.m_facing.z;
        if (P(0.5))
            op.v[I(0, 2)] = F(-1, 1);
    }
    if (op.kind == OP_TURN_DIR && P(0.3)) {
        // (A hair off the tracker's direction: TurnDir's 1e-4 angle test.)
        const float a = Fp({5e-5f, -5e-5f, 1.2e-4f, -1.2e-4f, 3e-4f, 0.0f});
        op.v[0] = g_w.dir.x * std::cos(a) + g_w.dir.z * std::sin(a);
        op.v[1] = g_w.dir.y;
        op.v[2] = g_w.dir.z * std::cos(a) - g_w.dir.x * std::sin(a);
    }
    op.mouse[0] = I(-30, 30);
    op.mouse[1] = I(-30, 30);
    if ((op.kind == OP_SLIDE_STEP || op.kind == OP_GRAVITY) && P(0.4))
        op.t = Ip({85, 86, 170, 171, 255, 1000, 100000, -5});   // (the 85 ms steps' edges)
    if (op.kind == OP_SLOPE || op.kind == OP_GROUNDED)
        RandomContacts(op);
    return op;
}
std::string g_ownSave[2];
std::string MakeLoadFile();
// Whether StepAnimation's animation id stays inside the table (0..0x16). The original indexes it unchecked, the
// port too: a sequence the game's BuildSequence makes can lead it out (a 0x17 read after a skipped one, an entry
// pushed without an animation: anims[-1]); both then read past the table, different memory on each side.
bool StepInTable(const C_MeleeTrackerMovement &m)
{
    S_MeleeTrackerAnimSequence seq = m.m_currentSeq;
    int read = m.m_35c;
    const auto empty = [](const S_MeleeTrackerAnimSequence &q) { return q.readIndex == -1 || q.readIndex < q.endIndex; };
    if (empty(seq) && read == -1)
        return true;
    if (empty(seq)) {
        if (read < 0 || read > 4)
            return false;
        seq = m.m_queue[read];
        read = read == m.m_360 ? -1 : (read + 1 > 4 ? 0 : read + 1);
    }
    if (seq.readIndex < 0 || seq.readIndex > 3)
        return false;
    int id = seq.anims[seq.readIndex];
    if (id == 0x17 || ((id == 5 || id == 6) && !g_w.hasTarget)) {
        if (seq.readIndex != -1 && seq.readIndex >= seq.endIndex)
            --seq.readIndex;
        if (empty(seq))
            return read == -1 || (read >= 0 && read <= 4);
        if (seq.readIndex < 0 || seq.readIndex > 3)
            return false;
        id = seq.anims[seq.readIndex];
    }
    return id >= 0 && id < 0x17;
}
void Run(int side, const S_Op &op)
{
    C_MeleeTrackerMovement &m = Mv();
    void *x = &g_mv[side];
    float v[4] = {op.v[0], op.v[1], op.v[2], op.v[3]};
    switch (op.kind) {
    case OP_BIND:
        // (A fresh bind: the old volumes go first, as a tracker's StopMission does.)
        FreeVolumes(side);
        m.m_meleeTracker = nullptr;                     // (Bind stores its owner)
        side ? m.Bind(reinterpret_cast<I3D_model *>(Model()), reinterpret_cast<C_MeleeTracker *>(g_tracker[side]))
             : OriginalAt<T_VPP>(0x005c8370)(x, Model(), g_tracker[side]);
        break;
    case OP_INIT:
        FreeVolumes(side);
        side ? m.Init() : OriginalAt<T_V>(0x005c7d30)(x);
        break;
    case OP_RESET:
        side ? m.Reset() : OriginalAt<T_V>(0x005d1f50)(x);
        break;
    case OP_FREE:
        side ? m.FreeVolumesAndSounds() : OriginalAt<T_V>(0x005c83a0)(x);
        break;
    case OP_OPTIMIZED:
        if (!m.m_volumeA[0] || !m.m_volumeA[1] || !m.m_volumeB[0] || !m.m_volumeB[1] || !m.m_volumeB[2]) {
            Log("skipped: a volume is gone");   // (both call through the volumes unchecked)
            break;
        }
        side ? m.SetOptimized(op.b & 1) : OriginalAt<T_VI>(0x005c86e0)(x, op.b & 1);
        break;
    case OP_OPT_CONTROLLER:
        side ? m.SetOptimizedController(op.b & 1) : OriginalAt<T_VI>(0x005c86c0)(x, op.b & 1);
        break;
    case OP_SET_MODE:
        // (Death empties the queue until its read index meets the write index: one out of 0..4 (QueuePopBack's quirk, a
        // loaded one) never does, in both.)
        if (op.a == 5 && m.m_35c != -1 && (m.m_360 < 0 || m.m_360 > 4) && m.m_374 == 0) {
            Log("skipped: the queue would never empty");
            break;
        }
        side ? m.SetMode(op.a, op.b) : OriginalAt<T_VII>(0x005c8490)(x, op.a, op.b);
        break;
    case OP_SUB_A:
        side ? m.SetSubModeA(op.a, op.b) : OriginalAt<T_VII>(0x005c8540)(x, op.a, op.b);
        break;
    case OP_SUB_B:
        side ? m.SetSubModeB(op.a, op.b) : OriginalAt<T_VII>(0x005c8570)(x, op.a, op.b);
        break;
    case OP_SEQ_A:
        Log("result %d", side ? m.GetSeqAnimA() : OriginalAt<T_I>(0x005c85a0)(x));
        break;
    case OP_SEQ_B:
        Log("result %d", side ? m.GetSeqAnimB() : OriginalAt<T_I>(0x005c8600)(x));
        break;
    case OP_SEQ_C:
        Log("result %d", side ? m.GetSeqAnimC() : OriginalAt<T_I>(0x005c8660)(x));
        break;
    case OP_PUSH: {
        void *slot = side ? static_cast<void *>(m.QueuePushSlot()) : OriginalAt<T_PP>(0x005c4b90)(x);
        Log("slot +%x", unsigned(static_cast<uint8_t *>(slot) - static_cast<uint8_t *>(x)));
        break;
    }
    case OP_POP_FRONT:
        side ? m.QueuePopFront() : OriginalAt<T_V>(0x005c4c30)(x);
        break;
    case OP_POP_BACK:
        side ? m.QueuePopBack() : OriginalAt<T_V>(0x005c4c70)(x);
        break;
    case OP_NET_POS:
        side ? m.SetNetPosition(v) : OriginalAt<T_VP>(0x005c0f50)(x, v);
        break;
    case OP_CLEAR_NET:
        side ? m.ClearNetBit(v) : OriginalAt<T_VP>(0x005c0d10)(x, v);
        break;
    case OP_WORLD_DIR:
        side ? m.SetWorldDir(v) : OriginalAt<T_VP>(0x005c0d60)(x, v);
        break;
    case OP_FACING:
        side ? m.SetFacing(v) : OriginalAt<T_VP>(0x005c1e60)(x, v);
        break;
    case OP_TURN_TO:
        Log("result %d", int(side ? m.TurnToFacing(op.t, 1) : OriginalAt<T_BII>(0x005c1b80)(x, op.t, 1)));
        break;
    case OP_TURN_DIR:
        side ? m.TurnDir(v) : OriginalAt<T_VP>(0x005c89c0)(x, v);
        Log("dir %s", VecText(S_vector(v[0], v[1], v[2])).c_str());   // (its w: a stack word in the original's)
        break;
    case OP_UPDATE_ANIM:
        g_mouse[0] = op.mouse[0];
        g_mouse[1] = op.mouse[1];
        side ? m.UpdateAnimation(op.t) : OriginalAt<T_VI>(0x005c18c0)(x, op.t);
        break;
    case OP_EVENTS:
        side ? m.HandleAnimEvents() : OriginalAt<T_V>(0x005c1600)(x);
        break;
    case OP_APPLY_NET:
        side ? m.ApplyNetInterp(op.t) : OriginalAt<T_VI>(0x005c1430)(x, op.t);
        break;
    case OP_CHECK_GROUND:
        side ? m.CheckGround() : OriginalAt<T_V>(0x005c88b0)(x);
        break;
    case OP_BITE:
        side ? m.Bite() : OriginalAt<T_V>(0x005c49a0)(x);
        break;
    case OP_SPHERES:
        side ? m.UpdateSpheres() : OriginalAt<T_V>(0x005c1dc0)(x);
        break;
    case OP_STEP:
        if (!StepInTable(m)) {
            Log("skipped: the animation id would leave the table");
            break;
        }
        side ? m.StepAnimation(op.b) : OriginalAt<T_VI>(0x005c4cb0)(x, op.b);
        break;
    case OP_BUILD:
        side ? m.BuildSequence() : OriginalAt<T_V>(0x005c5030)(x);
        break;
    case OP_ATTACK:
        side ? m.CheckAttackSequence() : OriginalAt<T_V>(0x005c5a90)(x);
        break;
    case OP_SOUND:
        side ? m.PlayAnimSound(op.a) : OriginalAt<T_VI>(0x005c5bf0)(x, op.a);
        break;
    case OP_BARK:
        // (The original's takes the tracker in ECX, its movement at +0x90: the side's movement copied in and out.)
        if (side)
            m.BarkTurn();
        else {
            std::memcpy(g_tracker[0] + 0x90, x, sizeof(C_MeleeTrackerMovement));
            OriginalAt<T_V>(0x005c6c70)(g_tracker[0]);
            std::memcpy(x, g_tracker[0] + 0x90, sizeof(C_MeleeTrackerMovement));
        }
        break;
    case OP_CLASS: {
        using T = int(__attribute__((fastcall)) *)(int);
        Log("result %d", side ? MeleeTrackerAnimClass(op.a) : OriginalAt<T>(0x005d86f0)(op.a));
        break;
    }
    case OP_ONE_SHOT:
        Log("result %d", side ? MeleeTrackerAnimOneShot(op.a) : OriginalAt<T_OneShot>(0x005c8880)(x, op.a));
        break;
    case OP_LUNGE: {
        using T = float(TC *)(void *);
        Log("result %s", FloatText(side ? m.GetLungeRange() : OriginalAt<T>(0x005c8b10)(x)).c_str());
        break;
    }
    case OP_UPDATE:
        // (The original's Move, called from its Update, with its stack zeroed first: the call patched.)
        g_moveRan = g_queryStream = true;
        g_mouse[0] = op.mouse[0];
        g_mouse[1] = op.mouse[1];
        if (side)
            m.Update(op.t);
        else {
            ZeroStack();
            OriginalAt<T_VI>(0x005c8b30)(x, op.t);
        }
        g_queryStream = false;
        break;
    case OP_MOVE:
        g_moveRan = g_queryStream = true;
        side ? m.Move(op.t) : O_MoveZeroed(x, op.t);
        g_queryStream = false;
        break;
    case OP_CONTACT: {
        g_moveRan = g_queryStream = true;
        S_CollisionVolume *hit = op.a & 1 ? &g_hitVol[(op.a >> 1) & 3] : nullptr;
        const int grounded = op.b & 1;
        float move[4] = {v[0] * 0.2f, v[1] * 0.2f, v[2] * 0.2f, v[3]};
        if ((op.a & 0x18) == 0)
            move[0] = move[1] = move[2] = 0.0f;
        using T = void(TC *)(void *, void *, int, int, float *);
        if (side)
            m.ContactResponse(hit, char(grounded), op.t, move);
        else {
            ZeroStack();
            OriginalAt<T>(0x005c2260)(x, hit, grounded, op.t, move);
        }
        g_queryStream = false;
        break;
    }
    case OP_ALIGN:
        g_queryStream = true;
        if (side)
            m.AlignToSlope(op.t);
        else {
            ZeroStack();
            OriginalAt<T_VI>(0x005c26d0)(x, op.t);
        }
        g_queryStream = false;
        break;
    case OP_SLOPE: {
        // (The original's takes the normal, the grounded and moving bytes and the hit in one bundle.)
        S_MoveContacts c = op.c;
        S_CollisionVolume *hit = op.hitVol >= 0 ? &g_hitVol[op.hitVol] : nullptr;
        if (op.b & 1) {                                 // (the slide's preconditions: not falling, no slope blend yet)
            m.m_1f8 = -10000.1f;
            m.m_250 = 0.0f;
            if (m.m_364 > 4)
                m.m_364 = op.a & 3;
        }
        if (side)
            m.SlopeSlide(&c, op.normal, hit, op.moving, op.ground, op.move, op.t);
        else {
            struct {
                float normal[4];
                uint8_t grounded, moving, pad[2];
                S_CollisionVolume *hit;
            } bundle = {{op.normal[0], op.normal[1], op.normal[2], op.normal[3]}, 1, uint8_t(op.moving), {0, 0}, hit};
            using T = void(TC *)(void *, S_MoveContacts *, void *, const float *, const float *, int);
            ZeroStack();
            OriginalAt<T>(0x005c43d0)(x, &c, &bundle, op.ground, op.move, op.t);
        }
        Log("pos %s", VecText(S_vector(c.pos.x, c.pos.y, c.pos.z)).c_str());   // (its w: a stack word on some paths)
        break;
    }
    case OP_GROUNDED: {
        using T = bool(__attribute__((fastcall)) *)(const void *);
        Log("result %d", int(side ? IsGrounded(op.c) : OriginalAt<T>(0x005c6380)(&op.c)));
        break;
    }
    case OP_VOLMAT: {
        const S_CollisionVolume *vol = op.a & 4 ? nullptr : &g_hitVol[op.a & 3];
        using T = int(__attribute__((fastcall)) *)(const void *);
        Log("result %d", side ? VolumeMaterial(vol) : OriginalAt<T>(0x005c6360)(vol));
        break;
    }
    case OP_SLIDE_STEP: {
        // (From the operation's numbers: both sides the same.)
        static const float edges[] = {0.0f, 85.0f, 84.9f};
        float velocity = v[0] * 5.0f, phase = (op.a & 3) == 3 ? v[1] * 60.0f : edges[op.a & 3];
        using T = void(__attribute__((fastcall)) *)(int, float *, float *);
        side ? MeleeTrackerSlideStep(op.t, &velocity, &phase) : OriginalAt<T>(0x005c6250)(op.t, &velocity, &phase);
        Log("result %s %s", FloatText(velocity).c_str(), FloatText(phase).c_str());
        break;
    }
    case OP_GRAVITY: {
        static const float edges[] = {0.0f, 85.0f, 84.9f};
        const float velocity = v[0] * 5.0f, phase = (op.a & 3) == 3 ? v[1] * 60.0f : edges[op.a & 3];
        using T = float(__attribute__((fastcall)) *)(int, float, float);
        Log("result %s", FloatText(side ? MeleeTrackerGravityDistance(op.t, velocity, phase)
                                        : OriginalAt<T>(0x005c62c0)(op.t, velocity, phase)).c_str());
        break;
    }
    case OP_SAVE:
    case OP_LOAD:
    case OP_LOAD_OWN: {
        const bool save = op.kind == OP_SAVE;
        const char *name = save ? "save.sav" : "load.sav";
        if (op.kind == OP_LOAD_OWN) {
            if (g_ownSave[side].empty())
                break;
            g_files[name] = g_ownSave[side];
        }
        S_SideChunk c;
        const bool open = op.b != 3;
        if (open)
            ChunkOpen(side, c, name, save);
        bool result;
        if (save)
            result = side ? m.SaveGame(c.port) : OriginalAt<T_RetBP>(0x005d09b0)(x, c.orig);
        else {
            result = side ? m.LoadGame(c.port) : OriginalAt<T_RetBP>(0x005d11a0)(x, c.orig);
            g_loaded = true;
        }
        Log("result %d", int(result));
        if (open) {
            if (save) {
                CBegin(c.Ptr(side), 0x7777);
                const uint32_t next = 0x12345678;
                CWrite(c.Ptr(side), &next, 4);
                CAscend(c.Ptr(side));
            } else
                Log("left at %d depth %d", CPos(c.Ptr(side)), CDepth(c.Ptr(side)));
            ChunkClose(side, c);
        }
        if (save && open) {
            const std::string &f = g_files[name];
            std::string hex;
            char t[4];
            for (unsigned char ch : f) {
                std::snprintf(t, sizeof t, "%02x", ch);
                hex += t;
            }
            Log("file %d %s", int(f.size()), hex.c_str());
            g_ownSave[side] = f;
        }
        break;
    }
    default:
        break;
    }
}

// A load file: the movement's chunk (0x2F59) with its sub-chunks in any order, then the mixer's (0x49C1).
std::string MakeLoadFile()
{
    const bool keep = g_fileLog;
    g_fileLog = false;
    const std::map<std::string, std::string> kept = g_files;
    C_chunk c;
    c.Open("make.sav", true);
    c.Begin(P(0.05) ? uint16_t(0x2F5A) : uint16_t(0x2F59));
    std::vector<int> ids;
    for (int id = 0; id <= 0x30; ++id)
        if (P(0.8))
            ids.push_back(id);
    for (int n = I(0, 2); n > 0; --n)
        ids.push_back(Ip({0x31, 0x40, 0x2F59}));
    for (int n = I(0, 2); n > 0 && !ids.empty(); --n)
        ids.push_back(ids[size_t(I(0, int(ids.size()) - 1))]);
    std::shuffle(ids.begin(), ids.end(), g_rng);
    const auto fl = [&](float lo, float hi) { return Bits(F(lo, hi)); };
    const auto in = [&](int lo, int hi) { return uint32_t(I(lo, hi)); };
    for (const int id : ids) {
        std::vector<uint32_t> w;
        switch (id) {
        case 0x1E: {
            S_MeleeTrackerAnimSequence s;
            RandomSeq(s);
            w.resize(sizeof s / 4);
            std::memcpy(w.data(), &s, sizeof s);
            break;
        }
        case 0x1F: {
            w.resize(0xB4 / 4);
            for (int k = 0; k < 5; ++k) {
                S_MeleeTrackerAnimSequence s;
                RandomSeq(s);
                std::memcpy(&w[size_t(k * 9)], &s, sizeof s);
            }
            break;
        }
        case 1: case 2: case 8: case 9: case 0x29: case 0x2A: case 0x2B: case 0x2D:
            w = {fl(-3, 3), fl(-3, 3), fl(-3, 3)};
            break;
        case 5: case 6: case 7: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: case 0x30:
            w = {fl(-3, 3)};
            break;
        case 3:
            w = {in(-1, 0x5F)};
            break;
        case 0x18: case 0x22: case 0x23:
            w = {in(0, 0x17)};
            break;
        case 0x20: case 0x21:
            w = {in(-1, 4)};
            break;
        case 0x14: case 0x26:
            w = {P(0.4) ? uint32_t(MELEE_TRACKER_MOVE_NO_TIME) : in(0, 5000)};
            break;
        default:
            w = {in(-1, 300)};
            break;
        }
        if (P(0.04))
            w.push_back(U32());
        c.Begin(uint16_t(id));
        c.Write(w.data(), int(w.size() * 4));
        c.Ascend();
    }
    c.Ascend();
    // The mixer's chunk (part 80's: a few of its sub-chunks).
    if (P(0.85)) {
        c.Begin(P(0.05) ? uint16_t(0x49C2) : uint16_t(0x49C1));
        const uint32_t f1 = Bits(F(0, 1)), t = in(0, 600);
        c.Begin(2);
        c.Write(&f1, 4);
        c.Ascend();
        c.Begin(7);
        c.Write(&t, 4);
        c.Ascend();
        c.Ascend();
    }
    c.Begin(0x7777);
    const uint32_t next = 0x12345678;
    c.Write(&next, 4);
    c.Ascend();
    c.Close();
    std::string f = g_files["make.sav"];
    if (P(0.02))
        f.clear();
    g_files = kept;
    g_fileLog = keep;
    return f;
}

struct S_Run {
    std::vector<std::string> log;
};
S_Run g_runs[2];
void TestScenarios()
{
    RandomWorld();
    const S_World start = g_w;
    std::vector<S_Op> ops;
    for (int n = I(1, 14); n > 0; --n)
        ops.push_back(RandomOp());
    std::vector<std::string> files;
    std::vector<S_World> answers;
    for (const S_Op &op : ops) {
        files.push_back(op.kind == OP_LOAD ? MakeLoadFile() : std::string());
        RandomWorld();
        answers.push_back(g_w);
    }
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        g_w = start;
        g_log.clear();
        g_files.clear();
        g_openFiles.clear();
        g_nextFile = 1;
        g_ownSave[side].clear();
        g_loaded = false;
        g_moveRan = false;
        SetWorld(side);
        for (size_t i = 0; i < ops.size(); ++i) {
            const S_Op &op = ops[i];
            Log("-- %d %d %d %s %s %s %s", op.kind, op.a, op.b, FloatText(op.v[0]).c_str(), FloatText(op.v[1]).c_str(),
                FloatText(op.v[2]).c_str(), FloatText(op.v[3]).c_str());
            if (op.kind == W_ANSWERS) {
                // (The answers change: the controller, the target, the sphere test, the frames found.)
                const S_World &a = answers[i];
                g_w.hasTarget = a.hasTarget;
                g_w.targetDistance = a.targetDistance;
                g_w.sphereHit = a.sphereHit;
                g_w.aiMode = a.aiMode;
                Put(g_ai[side], 0x14, g_w.aiMode);
                std::memcpy(g_w.frames, a.frames, sizeof a.frames);
                g_w.health = a.health;
                Put(g_tracker[side], TrackerOff(side, O_STATS, P_STATS), g_w.health);
            } else {
                if (op.kind == OP_LOAD)
                    g_files["load.sav"] = files[i];
                Run(side, op);
            }
            g_log.push_back(Snap(side));
        }
        FreeVolumes(side);
        g_runs[side].log.swap(g_log);
    }
    CompareLogs(g_runs[0].log, g_runs[1].log);
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

void SetUp()
{
    g_partName = PartName;
    g_findChild = FindChild;
    for (void *&v : g_setVt)
        v = reinterpret_cast<void *>(S_Other);
    g_setVt[0x00 / 4] = reinterpret_cast<void *>(S_Release);
    g_setVt[0x08 / 4] = reinterpret_cast<void *>(S_EndTime);
    g_frameVt[0x20 / 4] = reinterpret_cast<void *>(M_Tick);
    g_frameVt[0x28 / 4] = reinterpret_cast<void *>(M_SetCallback);
    g_frameVt[0x84 / 4] = reinterpret_cast<void *>(M_SetAnimation);
    g_frameVt[0x88 / 4] = reinterpret_cast<void *>(M_GetAnimation);
    g_frameVt[0x8C / 4] = reinterpret_cast<void *>(M_StopAnimation);
    g_frameVt[0x94 / 4] = reinterpret_cast<void *>(M_SetAnimWeight);
    g_frameVt[0x98 / 4] = reinterpret_cast<void *>(M_GetAnimWeight);
    g_frameVt[0x9C / 4] = reinterpret_cast<void *>(M_SetAnimTime);
    g_frameVt[0xA0 / 4] = reinterpret_cast<void *>(M_GetAnimTime);
    g_frameVt[0xAC / 4] = reinterpret_cast<void *>(M_IsAnimActive);
    g_frameVt[0xB0 / 4] = reinterpret_cast<void *>(M_AdvanceStages);
    g_sceneVt[0xE0 / 4] = reinterpret_cast<void *>(SC_GetSector);
    g_sceneVt[0xEC / 4] = reinterpret_cast<void *>(SC_UpdateFrameSector);
    g_graphVt[0xEC / 4] = reinterpret_cast<void *>(G_MouseX);
    g_graphVt[0xF0 / 4] = reinterpret_cast<void *>(G_MouseY);
    // The controllers' and the targets' vtables (the original's slots; the port's own).
    for (int side = 0; side < 2; ++side) {
        for (void *&s : g_aiVt[side])
            s = reinterpret_cast<void *>(V_Other);
        for (void *&s : g_targetVt[side])
            s = reinterpret_cast<void *>(V_Other);
    }
    g_aiVt[0][0x24 / 4] = reinterpret_cast<void *>(V_GetTarget);
    g_aiVt[0][0x28 / 4] = reinterpret_cast<void *>(V_Distance);
    g_aiVt[1][Slot(&C_MeleeTrackerController::GetTarget) / 4] = reinterpret_cast<void *>(V_GetTarget);
    g_aiVt[1][Slot(&C_MeleeTrackerController::DistanceToTarget) / 4] = reinterpret_cast<void *>(V_Distance);
    g_targetVt[0][0xD4 / 4] = reinterpret_cast<void *>(V_Hit);
    g_targetVt[1][Slot(&C_Actor::Hit) / 4] = reinterpret_cast<void *>(V_Hit);
    for (int side = 0; side < 2; ++side)
        for (void *&s : g_trackerVt[side])
            s = reinterpret_cast<void *>(V_Other);
    g_trackerVt[0][0xD4 / 4] = reinterpret_cast<void *>(V_Hit);
    g_trackerVt[1][Slot(&C_Actor::Hit) / 4] = reinterpret_cast<void *>(V_Hit);
    for (void *&s : g_cameraVt)
        s = reinterpret_cast<void *>(V_Other);
    g_cameraVt[0x48 / 4] = reinterpret_cast<void *>(O_SetViewDir);
    Put(g_camera, 0, static_cast<void *>(g_cameraVt));
    for (void *&s : g_soundVt)
        s = reinterpret_cast<void *>(V_Other);
    g_soundVt[0x54 / 4] = reinterpret_cast<void *>(O_Play);
    g_soundVt[0x5C / 4] = reinterpret_cast<void *>(O_Stop);
    Put(g_sound, 0, static_cast<void *>(g_soundVt));
    Put(g_origGame, 0x18, static_cast<void *>(&g_scene));
    reinterpret_cast<C_game *>(g_portGame)->m_scene = reinterpret_cast<I3D_scene *>(&g_scene);
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x0041fc10, reinterpret_cast<const void *>(O_CacheGet)},
        {0x006ef7b0, reinterpret_cast<const void *>(O_AddVolume)},
        {0x006ef9b0, reinterpret_cast<const void *>(O_RemoveVolume)},
        {0x006efbc0, reinterpret_cast<const void *>(O_MoveVolume)},
        {0x00718900, reinterpret_cast<const void *>(O_SphereTest)},
        {0x00718770, reinterpret_cast<const void *>(O_RayTest)},
        {0x0071bec0, reinterpret_cast<const void *>(O_MoveSpheres)},
        {0x006efee0, reinterpret_cast<const void *>(O_IgnoreVolumes)},
        {0x006efe40, reinterpret_cast<const void *>(O_IgnoreVolume)},
        {0x0071fad0, reinterpret_cast<const void *>(O_DebugLine)},
        {0x004336c0, reinterpret_cast<const void *>(O_GetSoundModule)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x0069a620, reinterpret_cast<const void *>(O_GetCameraModule)},
        {0x0069c2b0, reinterpret_cast<const void *>(O_Print)},
        {0x005cf1b0, reinterpret_cast<const void *>(O_MouseLook)},
        {0x00461410, reinterpret_cast<const void *>(O_GetPlayerTeam)},
        {0x0045d4a0, reinterpret_cast<const void *>(O_AddSeen)},
        {0x007e53c2, reinterpret_cast<const void *>(O_AlignedMalloc)},
        {0x007e559c, reinterpret_cast<const void *>(O_AlignedFree)},
        {0x007e53a4, reinterpret_cast<const void *>(O_dtaOpen)},
        {0x007e53aa, reinterpret_cast<const void *>(O_dtaOpenWrite)},
        {0x007e53b0, reinterpret_cast<const void *>(O_dtaClose)},
        {0x007e5392, reinterpret_cast<const void *>(O_dtaWrite)},
        {0x007e5398, reinterpret_cast<const void *>(O_dtaSeek)},
        {0x007e539e, reinterpret_cast<const void *>(O_dtaRead)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    // Update's call of Move: to the stack-zeroing one.
    uint8_t *call = OriginalAt<uint8_t *>(0x005c8b82);
    if (call[0] != 0xE8) {
        std::printf("mtmove_diff: Update's call of Move not found\n");
        std::exit(2);
    }
    DWORD old;
    VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old);
    const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(&O_MoveZeroed) - (reinterpret_cast<uintptr_t>(call) + 5));
    std::memcpy(call + 1, &rel, 4);
    VirtualProtect(call, 5, old, &old);
    g_log.clear();
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("mtmove_diff", exitCode))
        return exitCode;
    SetUp();
    ++checks;   // (the bark-turn cooldown's starting value, before any scenario sets it)
    CHECK(*OriginalAt<int *>(0x008ac5b8) == g_meleeTrackerBarkTurnCooldown, "bark cooldown %d / %d",
          *OriginalAt<int *>(0x008ac5b8), g_meleeTrackerBarkTurnCooldown);
    int rounds = std::getenv("HD2_MTMOVE_QUICK") ? 10000 : 27000;
    if (const char *r = std::getenv("HD2_MTMOVE_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "MeleeTrackerMovement";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    std::printf("mtmove_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
