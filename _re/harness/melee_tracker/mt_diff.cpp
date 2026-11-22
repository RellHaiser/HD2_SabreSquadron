// Human part 82a: the meleeTracker actor's sensing and per-frame code (game/melee_tracker.cpp: C_MeleeTracker, 0x5b0
// bytes in the original's layout): SetOptimized, TestSight, ResetAI, OnSightEvent, Update, Tick, TrackVelocity, HeadLook,
// SetLookBehaviour, UpdateLookBehaviour, HitDamage, Hit, UnknownD0, OnSoundHeard, HearPlayers, against the original's.
// MinGW; build_mt.bat; runs mt_diff.exe ("mt_diff: N checks, 0 failures"). melee_tracker.cpp is compiled into this file,
// melee_tracker_ai.cpp linked (SetController's variants; not called here).
// Each side has its tracker: the original's a block in its layout, the port's a C_MeleeTracker block (its C_Actor packs
// its vectors: the fields elsewhere, copied by the generated layout table). What the tracker reaches is faked on both
// sides with the same loggers: its movement's members (the movement is part 81's), its controller (a block in the
// original's layout, as the port's: its virtuals logged by slot, Disable), the humans (blocks in each side's layout: type,
// frame, position, health, role, their movement's locomotion's ScaleStanceVolumes, GetMoveSpeedClass), the sight
// module's targets (each side's vtable) and Remove(Observer|Target), the actors module's GetHuman, the sound module's
// RemoveListener, the controls' GetValues, the game's debug print, the frames (the interface kit's arena), the head's
// joint override set; RandFloat, the clocks (timeGetTime, QueryPerformanceCounter, MicrosecondsNow) and rand alike;
// AngleBetween2D, RotateAboutY and MeleeTrackerAnimClass the original's on both sides, LS3DF's math for both.
// Each scenario sets the world, then runs up to 12 operations; the logs (every call out, the results, the tracker's
// fields after each operation) compared.
// HD2_MT_ROUNDS: scenarios (default 60,000; HD2_MT_QUICK: 20,000); HD2_MT_DUMP=N: round N's logs; HD2_MT_TRACE: the
// operations as they run; the kit's HD2_INTERFACE_* switches.
#include <windows.h>
#include <mmsystem.h>

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
// (The port's clock: the test's; timeGetTime's declaration seen already.)
namespace H { DWORD FakeTimeGetTime(); }
// (The caches' members the port reaches: stubs, declared before their use.)
template <> HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *, I3D_animation_set **, T_CacheErrorProc, void *);
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *, const char *, uint32_t, void *, void *, int);
#define timeGetTime H::FakeTimeGetTime
#include "game/melee_tracker.cpp"
#undef timeGetTime
#include "game/actors_module.h"
#include "game/app.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_movement.h"
#include "game/melee_tracker_ai.h"
#include "game/sight.h"
#include "game/sound_module.h"
#undef private
#undef protected

#include "original_exe.h"

#define INTERFACE_FRAMES 32
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
#define TC __attribute__((thiscall))

// ---- the layout: the port's C_MeleeTracker fields and the original's offsets ----
struct S_LayoutField {
    uint32_t orig, port, size;
    const char *name;
    char kind;
};
#define LF(o, m) S_LayoutField{o, uint32_t(offsetof(C_MeleeTracker, m)), uint32_t(sizeof(C_MeleeTracker::m)), #m, 'x'}
const S_LayoutField FIELDS[] = {
    LF(0x4A0, m_stats), LF(0x4B0, m_startStats), LF(0x4C0, m_startPos), LF(0x4D0, m_startRot), LF(0x4E0, m_headOverrides),
    LF(0x4E4, m_headFrame), LF(0x4E8, m_headTurning), LF(0x4E9, m_lookActive), LF(0x4F0, m_lookPoint), LF(0x500, m_headYaw),
    LF(0x504, m_lookReturn), LF(0x505, m_lookReached), LF(0x508, m_lookSpeed), LF(0x50C, m_lookRateMode), LF(0x510, m_lookTarget),
    LF(0x520, m_lookState), LF(0x524, m_lookParam), LF(0x528, m_lookTimer), LF(0x52C, m_lookRepeats), LF(0x530, m_optimized),
    LF(0x534, m_ai), LF(0x538, m_538), LF(0x53C, m_sightObserverId), LF(0x540, m_sightId), LF(0x544, m_bodyFrame),
    LF(0x548, m_soundListener), LF(0x54C, m_54c), LF(0x550, m_playersBegin), LF(0x554, m_playersEnd), LF(0x558, m_playersCap),
    LF(0x55C, m_55c), LF(0x564, m_sightingsBegin), LF(0x568, m_sightingsEnd), LF(0x56C, m_sightingsCap), LF(0x570, m_570),
    LF(0x574, m_seenBegin), LF(0x578, m_seenEnd), LF(0x57C, m_seenCap), LF(0x580, m_velocity), LF(0x590, m_lastPos),
    LF(0x5A0, m_lastFrameTime),
};
uint32_t FieldOff(int side, const char *name)
{
    for (const S_LayoutField &f : FIELDS)
        if (std::strcmp(f.name, name) == 0)
            return side ? f.port : f.orig;
    std::printf("mt_diff: no field %s\n", name);
    std::exit(2);
}
// The C_Actor fields (the original's offsets; the port's by offsetof).
const uint32_t A_ID[2] = {0x10, uint32_t(offsetof(C_Actor, m_netId))}, A_TYPE[2] = {0x18, uint32_t(offsetof(C_Actor, m_type))},
               A_FRAME[2] = {0x20, uint32_t(offsetof(C_Actor, m_frame))}, A_POS[2] = {0x30, uint32_t(offsetof(C_Actor, m_position))};
const uint32_t MOVEMENT[2] = {0x90, uint32_t(offsetof(C_MeleeTracker, m_movement))};

// ---- the objects ----
alignas(16) uint8_t g_tracker[2][0x800];               // [0] the original's layout, [1] the port's C_MeleeTracker
void *g_trackerVt[2][0x80];
C_MeleeTracker &Mt() { return *reinterpret_cast<C_MeleeTracker *>(g_tracker[1]); }
uint8_t *Field(int side, const char *name) { return g_tracker[side] + FieldOff(side, name); }
template <class T> T Get(int side, const char *name) { return At<T>(g_tracker[side], FieldOff(side, name)); }
template <class T> void Set(int side, const char *name, T v) { Put(g_tracker[side], FieldOff(side, name), v); }
// The controllers (the original's layout on both: the port's class has it).
alignas(16) uint8_t g_ai[2][0x100];
void *g_aiVt[2][0x40];
static_assert(offsetof(C_MeleeTrackerController, m_mode) == 0x14, "controller mode");
static_assert(offsetof(C_MeleeTrackerController, m_flags) == 0x20, "controller orders");
static_assert(offsetof(C_MeleeTrackerController, m_actionA) == 0x60, "controller weights");
// The humans (and a non-human actor among them): each side's layout.
constexpr int HUMANS = 4;
alignas(16) uint8_t g_human[2][HUMANS][sizeof(C_Human) > 0x800 ? sizeof(C_Human) + 0x40 : 0x840];
alignas(16) uint8_t g_humanMv[2][HUMANS][sizeof(C_HumanMovement) > 0x1000 ? sizeof(C_HumanMovement) + 0x40 : 0x1040];
const uint32_t H_TYPE[2] = {0x18, uint32_t(offsetof(C_Human, m_type))}, H_FRAME[2] = {0x20, uint32_t(offsetof(C_Human, m_frame))},
               H_POS[2] = {0x30, uint32_t(offsetof(C_Human, m_position))},
               H_HEALTH[2] = {0xEC, uint32_t(offsetof(C_Human, m_stats) + 4 * STAT_HEALTH)},
               H_ROLE[2] = {0x19C, uint32_t(offsetof(C_Human, m_role))},
               H_MOVEMENT[2] = {0xA0, uint32_t(offsetof(C_Human, m_body) + offsetof(S_HumanBody, movement))},
               H_LOCO[2] = {0xAC0, uint32_t(offsetof(C_HumanMovement, m_locomotion))};
void *Human(int side, int i) { return g_human[side][i]; }
// The sight module's targets: each side's (the port's vtable has its destructor first).
constexpr int TARGETS = 5;
alignas(16) uint8_t g_target[2][TARGETS][0x40];
void *g_targetVt[2][0x10];
void *g_targetList[TARGETS];                             // the original's vector's data (begin at 0x0089b298)
// The joint override set and its item (COM slots: shared).
alignas(16) uint8_t g_overrideSet[0x40], g_overrideItem[0x40];
void *g_overrideSetVt[0x20], *g_overrideItemVt[0x20];
void *g_overrideItems[1] = {g_overrideItem};
// The sound module, the game.
uint8_t g_sound[0x10], g_origGame[0x100], g_portGame[0x2000];
void *g_soundVt[0x40];
// The camera module (the player's controller sets its view direction).
uint8_t g_cameraObj[0x10];
void *g_cameraVt[0x40];
// The controls' values.
float g_valuesHeld[0x40], g_valuesPressed[0x40];

// ---- the frames: 0 the tracker's model, 1 the head bone "f", 2 its parent, 3..6 the humans', 7..13 the hit zones'
// bones ("c" "f" "d" "h" "p" "t" "l": the movement's +0x3F0.. +0x400) ----
const char *const FRAME_NAMES[] = {"model", "head", "neck", "h0", "h1", "h2", "h3", "zc", "zf", "zd", "zh", "zp", "zt", "zl"};
constexpr int NFRAMES = 14, HUMAN_FRAME = 3, ZONE_FRAME = 7;
I3D_frame *Model() { return FrameAt(0); }

std::string LifeName(const void *p);
std::string PartName(const void *p)
{
    for (int side = 0; side < 2; ++side) {
        if (p == g_tracker[side])
            return "tracker";
        if (p == g_tracker[side] + MOVEMENT[side])
            return "movement";
        if (p == g_ai[side])
            return "ai";
        for (int i = 0; i < HUMANS; ++i) {
            if (p == g_human[side][i])
                return "human" + std::to_string(i);
            if (p == g_humanMv[side][i] + H_LOCO[side])
                return "loco" + std::to_string(i);
        }
        for (int i = 0; i < TARGETS; ++i)
            if (p == g_target[side][i])
                return "target" + std::to_string(i);
    }
    if (p == g_overrideSet)
        return "overrides";
    if (p == g_overrideItem)
        return "override0";
    if (p == g_valuesHeld)
        return "held";
    if (p == g_valuesPressed)
        return "pressed";
    const std::string life = LifeName(p);
    if (!life.empty())
        return life;
    const int f = FrameIndex(p);
    if (f >= 0 && f < NFRAMES)
        return FRAME_NAMES[f];
    return "";
}

// ---- the world ----
struct S_World {
    uint32_t role;
    int health, startHealth, sightRange, hearRange;
    S_vector4 pos;
    // the look state
    bool headTurning, lookActive, lookReturn, lookReached, optimized;
    S_vector4 lookPoint, lookTarget;
    float headYaw, lookSpeed;
    int lookRateMode, lookState, lookParam, lookTimer, lookRepeats, lastFrameTime, c55c, sightObserverId, sightId,
        soundListener;
    S_vector4 velocity, lastPos;
    bool overrides;                                     // a head override set (the frame setup made one)
    uint8_t movement374;                                // the movement's landing state (4: out)
    int movement364, seqA;
    // the controller
    bool hasAi;
    int aiMode;
    uint32_t aiOrders;
    int aiTarget;                                       // GetTarget's human (-1 none)
    // the humans
    uint32_t humanType[HUMANS];
    int humanHealth[HUMANS], humanRole[HUMANS], humanSpeed[HUMANS];
    S_vector4 humanPos[HUMANS];
    // the sight targets
    int targetActor[TARGETS];                           // a human (0..3) or the tracker (4)
    int targetKind[TARGETS];
    bool targetActive[TARGETS];
    // the lists
    std::vector<int> seen, players;                     // humans
    int playersExtra, seenExtra;                        // the players' and the seen list's spare capacity
    bool exact;                                         // whole-number positions: distances exactly a range
    struct S_Sight {
        S_vector4 pos;
        int type, source, loudness;
        uint32_t time;
    };
    std::vector<S_Sight> sightings;
    uint64_t now;
    DWORD ms;
    std::vector<uint8_t> frames0;
};
S_World g_w;
uint64_t g_now;
DWORD g_ms;
DWORD FakeTimeGetTime()
{
    Log("timeGetTime -> %lu", g_ms);
    return g_ms++;
}

// ---- the fakes (both sides' logic; O_: the original's entry points) ----
void LogSighting(const char *what, const S_MeleeTrackerSighting *s)
{
    Log("%s %s %s %s %d %s %u %d", what, Name(s).c_str(), VecText(S_vector(s->pos.x, s->pos.y, s->pos.z)).c_str(),
        FloatText(s->pos.w).c_str(), s->type, Name(s->source).c_str(), s->time, s->loudness);
}
// The movement's members.
void F_MvSetOptimized(void *self, int on) { Log("%s SetOptimized %d", Name(self).c_str(), on & 0xFF); }
void F_MvUpdateAnimation(void *self, int t) { Log("%s UpdateAnimation %d", Name(self).c_str(), t); }
void F_MvBuildSequence(void *self) { Log("%s BuildSequence", Name(self).c_str()); }
void F_MvStepAnimation(void *self, int t) { Log("%s StepAnimation %d", Name(self).c_str(), t); }
void F_MvUpdate(void *self, int t) { Log("%s Update %d", Name(self).c_str(), t); }
void F_MvSetMode(void *self, int mode, int priority) { Log("%s SetMode %d %d", Name(self).c_str(), mode, priority); }
void F_MvSetSubModeA(void *self, int v, int priority) { Log("%s SetSubModeA %d %d", Name(self).c_str(), v, priority); }
int F_MvGetSeqAnimA(void *self)
{
    Log("%s GetSeqAnimA -> %d", Name(self).c_str(), g_w.seqA);
    return g_w.seqA;
}
void TC O_MvSetOptimized(void *self, int on) { F_MvSetOptimized(self, on); }
void TC O_MvUpdateAnimation(void *self, int t) { F_MvUpdateAnimation(self, t); }
void TC O_MvBuildSequence(void *self) { F_MvBuildSequence(self); }
void TC O_MvStepAnimation(void *self, int t) { F_MvStepAnimation(self, t); }
void TC O_MvUpdate(void *self, int t) { F_MvUpdate(self, t); }
void TC O_MvSetMode(void *self, int mode, int priority) { F_MvSetMode(self, mode, priority); }
void TC O_MvSetSubModeA(void *self, int v, int priority) { F_MvSetSubModeA(self, v, priority); }
int TC O_MvGetSeqAnimA(void *self) { return F_MvGetSeqAnimA(self); }
// The controller's virtuals (each side's slots) and Disable.
void *AiTarget(int side) { return g_w.aiTarget >= 0 ? Human(side, g_w.aiTarget) : nullptr; }
void TC V_FrameUpdate(void *self, int t) { Log("%s FrameUpdate %d", Name(self).c_str(), t); }
void TC V_Tick(void *self, int t)
{
    Log("%s Tick %d weights %s %s", Name(self).c_str(), t, Name(At<void *>(self, 0x60)).c_str(), Name(At<void *>(self, 0x64)).c_str());
}
void TC V_TestSight(void *self, void *target, int *status, float *visibility)
{
    Log("%s TestSight %s", Name(self).c_str(), Name(target).c_str());
    *status = 3;
    *visibility = 0.5f;
}
void *TC V_GetTarget(void *self)
{
    void *t = AiTarget(g_side);
    Log("%s GetTarget -> %s", Name(self).c_str(), Name(t).c_str());
    return t;
}
void TC V_ReleaseScript(void *self) { Log("%s ReleaseScript", Name(self).c_str()); }
void TC V_OnBulletHit(void *self) { Log("%s OnBulletHit", Name(self).c_str()); }
void TC V_OnHeardSound(void *self, S_MeleeTrackerSighting *s) { LogSighting((Name(self) + " OnHeardSound").c_str(), s); }
void TC V_OnSpottedEnemy(void *self, void *target) { Log("%s OnSpottedEnemy %s", Name(self).c_str(), Name(target).c_str()); }
void TC V_OnLostEnemy(void *self, void *target) { Log("%s OnLostEnemy %s", Name(self).c_str(), Name(target).c_str()); }
void TC V_OnSeenHuman(void *self, int type, void *actor) { Log("%s OnSeenHuman %d %s", Name(self).c_str(), type, Name(actor).c_str()); }
void TC V_Other(void *self) { Log("%s other slot", Name(self).c_str()); }
void TC O_Disable(void *self) { Log("%s Disable", Name(self).c_str()); }
// The tracker's own Hit through its vtable (Tick's fall).
void TC V_TrackerHit(void *self, int type, float amount, const S_vector *pos, const S_vector *dir, void *attacker, int flags)
{
    Log("%s Hit %d %s %s %s %s %d", Name(self).c_str(), type, FloatText(amount).c_str(), VecText(*pos).c_str(),
        VecText(*dir).c_str(), Name(attacker).c_str(), flags);
}
// The humans.
int F_MoveSpeedClass(const void *human)
{
    int i = 0;
    while (i < HUMANS && human != Human(g_side, i))
        ++i;
    const int v = i < HUMANS ? g_w.humanSpeed[i] : -1;
    Log("%s GetMoveSpeedClass -> %d", Name(human).c_str(), v);
    return v;
}
int TC O_MoveSpeedClass(void *human) { return F_MoveSpeedClass(human); }
void TC O_ScaleStance(void *loco, int enlarged) { Log("%s ScaleStanceVolumes %d", Name(loco).c_str(), enlarged & 0xFF); }
// The sight module.
void TC O_RemoveObserver(void *, int id) { Log("sight RemoveObserver %d", id); }
void TC O_RemoveTarget(void *, int id) { Log("sight RemoveTarget %d", id); }
void *TC T_GetActor(void *self)
{
    for (int i = 0; i < TARGETS; ++i)
        if (self == g_target[g_side][i]) {
            const int a = g_w.targetActor[i];
            return a == 4 ? static_cast<void *>(g_tracker[g_side]) : Human(g_side, a);
        }
    return nullptr;
}
int TC T_GetKind(void *self)
{
    for (int i = 0; i < TARGETS; ++i)
        if (self == g_target[g_side][i])
            return g_w.targetKind[i];
    return -1;
}
// The actors module.
int g_nullSlot = -1;                                    // (part 82b's: a slot answered with none)
void *F_GetHuman(int id)
{
    void *h = id >= 0 && id < HUMANS && id != g_nullSlot ? Human(g_side, id) : nullptr;
    Log("actors GetHuman %d -> %s", id, Name(h).c_str());
    return h;
}
void *TC O_GetHuman(void *, int id) { return F_GetHuman(id); }
// The sound module, the game, the controls.
void TC O_RemoveListener(void *, int id) { Log("sound RemoveListener %d", id); }
void *__cdecl O_GetSoundModule() { return g_sound; }
void TC O_PrintText(void *, const char *text) { Log("Print %s", text); }
int TC O_GetValues(void *, float **values, int pressed)
{
    Log("controls GetValues %d", pressed & 0xFF);
    *values = (pressed & 0xFF) ? g_valuesPressed : g_valuesHeld;
    return 0;
}
// The clocks and RandFloat.
DWORD __stdcall O_TimeGetTime() { return FakeTimeGetTime(); }
BOOL __stdcall O_QPF(LARGE_INTEGER *f)
{
    f->QuadPart = 1000000;
    return TRUE;
}
BOOL __stdcall O_QPC(LARGE_INTEGER *c)
{
    Log("clock %llu", static_cast<unsigned long long>(g_now));
    c->QuadPart = static_cast<LONGLONG>(g_now);
    g_now += 977;
    return TRUE;
}
float F_RandFloat()
{
    const float r = static_cast<float>(std::rand() % 1000) * 0.001f;
    Log("RandFloat -> %s", FloatText(r).c_str());
    return r;
}
float __cdecl O_RandFloat() { return F_RandFloat(); }
// (Zeroed: the original's controllers leave fields unset (the player's and the remote's owner) that it then reads.)
void *__cdecl O_AlignedMalloc(size_t size, size_t align)
{
    void *p = _aligned_malloc(size, align);
    std::memset(p, 0, size);
    return p;
}
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }
// The head's joint override set (COM: the same on both sides).
void __stdcall OV_Clear(void *self) { Log("%s Clear", Name(self).c_str()); }
void __stdcall OV_SetDir(void *self, const S_vector &dir, float roll)
{
    Log("%s SetDir %s %s", Name(self).c_str(), VecText(dir).c_str(), FloatText(roll).c_str());
}
void __stdcall OV_Other(void *self) { Log("%s other slot", Name(self).c_str()); }
void __stdcall M_Tick(void *f, int time) { Log("%s Tick %d", Name(f).c_str(), time); }
void TC O_CamSetViewDir(void *, const S_vector4 *dir) { Log("camera SetViewDir %s", VecText(S_vector(dir->x, dir->y, dir->z)).c_str()); }
void *TC O_GetCameraModule(void *) { return g_cameraObj; }

}  // namespace H

// ---- the port's side of the fakes ----
void C_MeleeTrackerMovement::SetOptimized(bool on) { H::F_MvSetOptimized(this, on); }
void C_MeleeTrackerMovement::UpdateAnimation(int t) { H::F_MvUpdateAnimation(this, t); }
void C_MeleeTrackerMovement::BuildSequence() { H::F_MvBuildSequence(this); }
void C_MeleeTrackerMovement::StepAnimation(int t) { H::F_MvStepAnimation(this, t); }
void C_MeleeTrackerMovement::Update(int t) { H::F_MvUpdate(this, t); }
void C_MeleeTrackerMovement::SetMode(int mode, int priority) { H::F_MvSetMode(this, mode, priority); }
void C_MeleeTrackerMovement::SetSubModeA(int v, int priority) { H::F_MvSetSubModeA(this, v, priority); }
int C_MeleeTrackerMovement::GetSeqAnimA() { return H::F_MvGetSeqAnimA(this); }
// (melee_tracker_ai.cpp defines it: the tracker's calls go through the linker's --wrap to the logger.)
extern "C" void __attribute__((thiscall)) __wrap__ZN24C_MeleeTrackerController7DisableEv(void *self) { H::O_Disable(self); }
int C_Human::GetMoveSpeedClass() const { return H::F_MoveSpeedClass(this); }
void C_HumanLocomotion::ScaleStanceVolumes(bool enlarged) { H::O_ScaleStance(this, enlarged); }
bool C_SightModule::RemoveObserver(int id) { H::O_RemoveObserver(this, id); return true; }
bool C_SightModule::RemoveTarget(int id) { H::O_RemoveTarget(this, id); return true; }
C_SightTarget *C_SightModule::GetTarget(int id) const
{
    // (As the original's: past the list or a free slot, none.)
    if (id < 0 || id >= H::TARGETS || !H::g_w.targetActive[id])
        return nullptr;
    return reinterpret_cast<C_SightTarget *>(H::g_target[1][id]);
}
C_Actor *C_ActorsModule::GetHuman(int id) const { return static_cast<C_Actor *>(H::F_GetHuman(id)); }
C_SoundModule *GetSoundModule() { return reinterpret_cast<C_SoundModule *>(H::g_sound); }
bool C_SoundModule::RemoveListener(int id) { H::O_RemoveListener(this, id); return true; }
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_portGame); }
void C_game::Print(const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    H::Log("Print %s", text);
}
int C_Controls::GetValues(float **values, bool pressed) { return H::O_GetValues(this, values, pressed); }
uint64_t MicrosecondsNow()
{
    H::Log("clock %llu", static_cast<unsigned long long>(H::g_now));
    const uint64_t n = H::g_now;
    H::g_now += 977;
    return n;
}
extern "C" float RandFloat() { return H::F_RandFloat(); }
S_MeleeTrackerAnim g_meleeTrackerAnims[0x17];
float AngleBetween2D(const float *a, const float *b)
{
    using T = float(__attribute__((fastcall)) *)(const float *, const float *);
    return OriginalAt<T>(0x00788a10)(a, b);
}
void RotateAboutY(S_vector &v, float angle)
{
    using T = void(__attribute__((thiscall)) *)(S_vector *, float);
    OriginalAt<T>(0x007888f0)(&v, angle);
}
int MeleeTrackerAnimClass(int group)
{
    using T = int(__attribute__((fastcall)) *)(int);
    return OriginalAt<T>(0x005d86f0)(group);
}
bool __stdcall S_matrix::Inverse(const S_matrix &m)
{
    using T = bool(__stdcall *)(S_matrix *, const S_matrix &);
    static const T f = reinterpret_cast<T>(H::Ls3df("?Inverse@S_matrix@@QAG_NABU1@@Z"));
    return f(this, m);
}
S_vector &__stdcall S_vector::operator*=(const S_matrix &m)
{
    using T = S_vector &(__stdcall *)(S_vector *, const S_matrix &);
    static const T f = reinterpret_cast<T>(H::Ls3df("??XS_vector@@QAGAAU0@ABUS_matrix@@@Z"));
    return f(this, m);
}

#include "mt_stubs.inc"

namespace H {

// ---- the world ----
float Fp(std::initializer_list<float> l) { return l.begin()[I(0, int(l.size()) - 1)]; }
int Ip(std::initializer_list<int> l) { return l.begin()[I(0, int(l.size()) - 1)]; }
S_vector4 RandomPos() { return {F(-20, 20), F(-2, 3), F(-20, 20), P(0.8) ? 1.0f : F(-1, 1)}; }
void RandomWorld()
{
    g_w.role = Pick({7u, 1u, 2u, 3u});
    g_w.startHealth = P(0.3) ? 250 : I(1, 400);
    g_w.health = P(0.2) ? I(-5, 0) : P(0.2) ? I(1, g_w.startHealth / 8 + 1) : I(1, 400);
    g_w.sightRange = P(0.2) ? 0 : I(1, 40);
    g_w.hearRange = P(0.2) ? 0 : I(1, 60);
    g_w.pos = RandomPos();
    g_w.exact = P(0.3);
    if (g_w.exact)
        g_w.pos = {float(I(-10, 10)), float(I(-1, 2)), float(I(-10, 10)), 1.0f};
    g_w.headTurning = P(0.5);
    g_w.lookActive = P(0.6);
    g_w.lookReturn = P(0.3);
    g_w.lookReached = P(0.3);
    g_w.optimized = P(0.15);
    g_w.lookPoint = RandomPos();
    g_w.lookTarget = RandomPos();
    g_w.headYaw = P(0.3) ? 0.0f : F(-1.2f, 1.2f);
    g_w.lookSpeed = P(0.3) ? Fp({1.0f, 0.6f}) : F(0.1f, 1.6f);
    g_w.lookRateMode = P(0.8) ? I(0, 2) : I(-1, 4);
    g_w.lookState = Ip({0, 2, 4, 6, 8, 3, 0x18});
    g_w.lookParam = P(0.3) ? Ip({0, 1, -1}) : I(-200, 2000);
    g_w.lookTimer = P(0.3) ? Ip({0, -1, 1}) : I(-100, 5000);
    g_w.lookRepeats = P(0.7) ? I(0, 3) : I(-1, 6);
    if (P(0.3)) {
        // (The look-around's next point: state 8 with a few repeats, its timer run out, the point reached.)
        g_w.lookState = 8;
        g_w.lookActive = P(0.7);
        g_w.lookRepeats = I(1, 3);
        g_w.lookTimer = Ip({0, 1, 50});
        g_w.lookReached = P(0.5);
        g_w.lookParam = Ip({0, 10, 300});
    }
    g_w.lastFrameTime = P(0.2) ? 0 : I(1, 100);
    g_w.c55c = P(0.5) ? I(900, 1100) : I(-1000, 2000);
    g_w.sightObserverId = P(0.3) ? -1 : I(0, 9);
    g_w.sightId = P(0.3) ? -1 : P(0.2) ? 0 : I(1, 9);
    g_w.soundListener = P(0.3) ? -1 : I(0, 9);
    g_w.velocity = RandomPos();
    g_w.lastPos = P(0.3) ? g_w.pos : RandomPos();
    g_w.overrides = P(0.9);
    g_w.movement374 = uint8_t(P(0.1) ? 4 : I(0, 2));
    g_w.movement364 = P(0.3) ? I(7, 12) : I(0, 0x17);
    g_w.seqA = P(0.2) ? Ip({0x13, 0x14}) : I(0, 0x17);
    g_w.hasAi = P(0.95);
    g_w.aiMode = Ip({1, 4, 5, 7});
    g_w.aiOrders = P(0.2) ? 0x40000000u : U32() & 0x3FFFu;
    g_w.aiTarget = P(0.5) ? -1 : I(0, HUMANS - 1);
    for (int i = 0; i < HUMANS; ++i) {
        g_w.humanType[i] = P(0.85) ? ACTOR_HUMAN : Pick({uint32_t(ACTOR_MELEE_TRACKER), 5u});
        g_w.humanHealth[i] = P(0.2) ? I(-3, 0) : I(1, 100);
        // (A non-human actor in a player slot: the original reads a role at +0x19C regardless, the port skips it, its
        // fix; never the player's role here.)
        g_w.humanRole[i] = g_w.humanType[i] == ACTOR_HUMAN ? I(0, 6) : Ip({0, 2, 3});
        g_w.humanSpeed[i] = I(-1, 6);
        g_w.humanPos[i] = P(0.5) ? S_vector4{g_w.pos.x + F(-8, 8), g_w.pos.y + F(-1, 1), g_w.pos.z + F(-8, 8), F(-1, 1)} : RandomPos();
        if (g_w.exact && P(0.5))   // (exactly the sight or hearing range away, on it, a metre off)
            g_w.humanPos[i] = {g_w.pos.x + float(Ip({g_w.sightRange, g_w.hearRange, 0, 1, -1})), g_w.pos.y, g_w.pos.z, 1.0f};
    }
    for (int i = 0; i < TARGETS; ++i) {
        g_w.targetActor[i] = P(0.85) ? I(0, HUMANS - 1) : 4;
        g_w.targetKind[i] = P(0.8) ? 0 : 1;
        g_w.targetActive[i] = P(0.9);
    }
    g_w.seen.clear();
    for (int n = P(0.4) ? 0 : I(1, 5); n > 0; --n)
        g_w.seen.push_back(I(0, HUMANS - 1));
    g_w.players.clear();
    for (int n = P(0.5) ? 0 : I(1, 4); n > 0; --n)
        g_w.players.push_back(I(0, HUMANS - 1));
    g_w.playersExtra = P(0.3) ? I(1, 2) : 0;
    g_w.seenExtra = P(0.3) ? I(1, 2) : 0;
    g_w.sightings.clear();
    for (int n = P(0.4) ? 0 : I(1, 4); n > 0; --n)
        g_w.sightings.push_back({RandomPos(), I(0, 10), P(0.2) ? -1 : I(0, HUMANS - 1), I(0, 40), U32() & 0xFFFF});
    g_w.now = 1000000 + U32() % 100000;
    g_w.ms = 5000 + U32() % 1000;
    for (int i = 0; i < NFRAMES; ++i) {
        RandomFrame(i);
        I3D_frame *f = FrameAt(i);
        f->m_parent = i == 1 ? FrameAt(2) : i ? Model() : nullptr;
        f->m_worldMatrix.m[3][3] = 1.0f;
        static const char *const names[] = {"dog", "f", "e", "Smith", "Jones", "Brown", "Novak", "c", "f", "d", "h", "p", "t", "l"};
        f->m_name = names[i];
    }
    // The model's position the tracker's (mostly), now and then fallen out; its forward a unit, short or any.
    S_matrix &wm = Model()->m_worldMatrix;
    if (g_w.exact || P(0.6)) {
        wm.m[3][0] = g_w.pos.x;
        wm.m[3][1] = P(0.1) ? Fp({-500.0f, -501.0f, -499.9f}) : g_w.pos.y;
        wm.m[3][2] = g_w.pos.z;
    }
    // (Two hit-zone bones now and then at one place: the first of equally near ones wins.)
    if (P(0.1)) {
        const int a = I(0, 6), b = I(0, 6);
        std::memcpy(&FrameAt(ZONE_FRAME + b)->m_worldMatrix.m[3][0], &FrameAt(ZONE_FRAME + a)->m_worldMatrix.m[3][0], 12);
    }
    // (The model's and the head's forward now and then a unit or too short to normalise: the axis snaps.)
    for (int i = 0; i < 2; ++i)
        if (P(0.15)) {
            static const float rows[][3] = {{0, 0, 1}, {0.6f, 0, 0.8f}, {0, 1e-5f, 0}, {0, -1e-5f, 0}, {1e-5f, 0, 0},
                                            {-1e-5f, 0, 0}, {0, 0, -1e-5f}, {0, 0, 2e-5f}, {0, 0, 0}};
            const auto &r = rows[I(0, 8)];
            S_matrix &m = FrameAt(i)->m_worldMatrix;
            m.m[2][0] = r[0];
            m.m[2][1] = r[1];
            m.m[2][2] = r[2];
        }
    for (int i = 0; i < HUMANS; ++i) {
        S_matrix &hm = FrameAt(HUMAN_FRAME + i)->m_worldMatrix;
        hm.m[3][0] = g_w.humanPos[i].x;
        hm.m[3][1] = g_w.humanPos[i].y;
        hm.m[3][2] = g_w.humanPos[i].z;
    }
    g_w.frames0.assign(g_arena.frames[0], g_arena.frames[0] + NFRAMES * FRAME_BYTES);
}

// A pointer list on a side: the original's (operator new: the test's malloc) and the port's alike.
template <class T> void PutList(int side, const char *begin, const std::vector<T *> &items, size_t extra)
{
    T **b = nullptr, **e = nullptr, **c = nullptr;
    if (!items.empty() || extra) {
        b = static_cast<T **>(std::malloc((items.size() + extra) * sizeof(T *) + 4));
        for (size_t i = 0; i < items.size(); ++i)
            b[i] = items[i];
        e = b + items.size();
        c = e + extra;
    }
    uint8_t *f = Field(side, begin);
    Put(f, 0, b);
    Put(f, 4, e);
    Put(f, 8, c);
}
S_FrameData g_modelData[2];                             // the model's frame data: its actor the side's tracker
void SetWorld(int side)
{
    g_modelData[side] = S_FrameData();
    g_modelData[side].actor = reinterpret_cast<C_Actor *>(g_tracker[side]);
    uint8_t *t = g_tracker[side];
    std::memset(t, 0, sizeof g_tracker[side]);
    Put(t, 0, static_cast<void *>(g_trackerVt[side]));
    Put(t, A_ID[side], 120);
    Put(t, A_TYPE[side], uint32_t(ACTOR_MELEE_TRACKER));
    Put(t, A_FRAME[side], Model());
    Put(t, A_POS[side], g_w.pos);
    // (The movement's: the landing state, the animation id, the hit zones' bones.)
    uint8_t *mv = t + MOVEMENT[side];
    Put(mv, 0x374, int(g_w.movement374));
    Put(mv, 0x364, g_w.movement364);
    for (int i = 0; i < 7; ++i)
        Put(mv, 0x3E8 + 4 * i, FrameAt(ZONE_FRAME + i));
    std::swap(*reinterpret_cast<I3D_frame **>(mv + 0x3E8), *reinterpret_cast<I3D_frame **>(mv + 0x3F0));   // ("f", "d", "c")
    const S_MeleeTrackerStats stats = {g_w.health, 50, g_w.sightRange, g_w.hearRange};
    const S_MeleeTrackerStats start = {g_w.startHealth, 50, 20, 100};
    Set(side, "m_stats", stats);
    Set(side, "m_startStats", start);
    Set(side, "m_headOverrides", g_w.overrides ? static_cast<void *>(g_overrideSet) : nullptr);
    Set(side, "m_headFrame", FrameAt(1));
    Set(side, "m_headTurning", g_w.headTurning);
    Set(side, "m_lookActive", g_w.lookActive);
    Set(side, "m_lookPoint", g_w.lookPoint);
    Set(side, "m_headYaw", g_w.headYaw);
    Set(side, "m_lookReturn", g_w.lookReturn);
    Set(side, "m_lookReached", g_w.lookReached);
    Set(side, "m_lookSpeed", g_w.lookSpeed);
    Set(side, "m_lookRateMode", g_w.lookRateMode);
    Set(side, "m_lookTarget", g_w.lookTarget);
    Set(side, "m_lookState", g_w.lookState);
    Set(side, "m_lookParam", g_w.lookParam);
    Set(side, "m_lookTimer", g_w.lookTimer);
    Set(side, "m_lookRepeats", g_w.lookRepeats);
    Set(side, "m_optimized", g_w.optimized);
    Set(side, "m_ai", g_w.hasAi ? static_cast<void *>(g_ai[side]) : nullptr);
    Set(side, "m_sightObserverId", g_w.sightObserverId);
    Set(side, "m_sightId", g_w.sightId);
    Set(side, "m_bodyFrame", FrameAt(1));
    Set(side, "m_soundListener", g_w.soundListener);
    Set(side, "m_55c", g_w.c55c);
    Set(side, "m_velocity", g_w.velocity);
    Set(side, "m_lastPos", g_w.lastPos);
    Set(side, "m_lastFrameTime", g_w.lastFrameTime);
    std::vector<C_Actor *> seen;
    for (int i : g_w.seen)
        seen.push_back(static_cast<C_Actor *>(Human(side, i)));
    PutList(side, "m_seenBegin", seen, size_t(g_w.seenExtra));
    std::vector<C_Human *> players;
    for (int i : g_w.players)
        players.push_back(static_cast<C_Human *>(Human(side, i)));
    PutList(side, "m_playersBegin", players, size_t(g_w.playersExtra));
    std::vector<S_MeleeTrackerSighting *> sightings;
    for (const S_World::S_Sight &s : g_w.sightings) {
        auto *p = static_cast<S_MeleeTrackerSighting *>(std::malloc(sizeof(S_MeleeTrackerSighting)));
        p->pos = s.pos;
        p->type = s.type;
        p->source = s.source < 0 ? nullptr : static_cast<C_Actor *>(Human(side, s.source));
        p->time = s.time;
        p->loudness = s.loudness;
        sightings.push_back(p);
    }
    PutList(side, "m_sightingsBegin", sightings, 0);
    // The controller.
    std::memset(g_ai[side], 0, sizeof g_ai[side]);
    Put(g_ai[side], 0, static_cast<void *>(g_aiVt[side]));
    Put(g_ai[side], 0x14, g_w.aiMode);
    Put(g_ai[side], 0x20, g_w.aiOrders);
    // The humans.
    for (int i = 0; i < HUMANS; ++i) {
        uint8_t *h = g_human[side][i];
        std::memset(h, 0, sizeof g_human[side][i]);
        Put(h, H_TYPE[side], g_w.humanType[i]);
        Put(h, H_FRAME[side], FrameAt(HUMAN_FRAME + i));
        Put(h, H_POS[side], g_w.humanPos[i]);
        Put(h, H_HEALTH[side], g_w.humanHealth[i]);
        Put(h, H_ROLE[side], g_w.humanRole[i]);
        Put(h, H_MOVEMENT[side], static_cast<void *>(g_humanMv[side][i]));
    }
    // The sight targets.
    for (int i = 0; i < TARGETS; ++i) {
        uint8_t *g = g_target[side][i];
        std::memset(g, 0, sizeof g_target[side][i]);
        Put(g, 0, static_cast<void *>(g_targetVt[side]));
        Put(g, side ? uint32_t(offsetof(C_SightTarget, mask)) : 8u, g_w.targetActive[i] ? 0xFFu : 0u);
        if (!side)
            g_targetList[i] = g;
    }
    *OriginalAt<void **>(0x0089b298) = g_targetList;
    *OriginalAt<void **>(0x0089b29c) = g_targetList + TARGETS;
    g_gameRole = g_w.role;
    *OriginalAt<uint32_t *>(0x00982100) = g_w.role;
    std::memcpy(g_arena.frames, g_w.frames0.data(), g_w.frames0.size());
    Model()->m_data = &g_modelData[side];
    for (int i = 0; i < 0x40; ++i) {
        g_valuesHeld[i] = float(i);
        g_valuesPressed[i] = float(-i);
    }
    g_now = g_w.now;
    g_ms = g_w.ms;
    std::srand(4321);
}
void FreeLists(int side)
{
    for (const char *n : {"m_seenBegin", "m_playersBegin", "m_sightingsBegin"}) {
        // (The sightings themselves: from the pools or the test's malloc; left.)
        std::free(Get<void *>(side, n));
        Set<void *>(side, n, nullptr);
    }
}

bool g_lifeMasks;                                       // part 82b's scenarios: the stack words it leaves
// The tracker's fields after an operation (the pointers by name; the lists by their entries).
std::string Snap(int side)
{
    std::string s = "mt";
    for (const S_LayoutField &f : FIELDS) {
        const uint8_t *p = g_tracker[side] + (side ? f.port : f.orig);
        const std::string n = f.name;
        if (n == "m_seenBegin" || n == "m_playersBegin" || n == "m_sightingsBegin") {
            void **b = At<void **>(p, 0), **e = At<void **>(p, 4), **c = At<void **>(p, 8);
            s += " | " + n.substr(2, n.size() - 7) + " " + std::to_string(b ? e - b : -1) + "/" + std::to_string(b ? c - b : -1);
            for (void **q = b; q && q != e; ++q) {
                if (n == "m_sightingsBegin") {
                    const auto *g = static_cast<const S_MeleeTrackerSighting *>(*q);
                    s += " [" + VecText(S_vector(g->pos.x, g->pos.y, g->pos.z)) + " " + FloatText(g->pos.w) + " " + std::to_string(g->type) +
                         " " + Name(g->source) + " " + std::to_string(g->time) + " " + std::to_string(g->loudness) + "]";
                } else
                    s += " " + Name(*q);
            }
            continue;
        }
        if (n == "m_seenEnd" || n == "m_seenCap" || n == "m_playersEnd" || n == "m_playersCap" || n == "m_sightingsEnd" ||
            n == "m_sightingsCap")
            continue;
        if (f.size == 4 && (n == "m_headOverrides" || n == "m_headFrame" || n == "m_ai" || n == "m_bodyFrame")) {
            s += " | " + n + " " + Name(At<void *>(p, 0));
            continue;
        }
        // (The w of the look point and target: the original's are stack words where it sets them.)
        // (Part 82b's: the start position's w (LoadGame's stack copy) and the velocity's (SetupFrame's).)
        const bool w = n == "m_lookPoint" || n == "m_lookTarget" || (g_lifeMasks && (n == "m_startPos" || n == "m_velocity"));
        s += " | " + n;
        for (uint32_t k = 0; k < f.size; k += 4) {
            if (w && k == 12)
                continue;
            if (f.size - k >= 4)
                s += " " + Hex(At<uint32_t>(p, k));
            else
                s += " " + Hex(p[k]);
        }
    }
    s += " | ai " + Name(At<void *>(g_ai[side], 0x60)) + " " + Name(At<void *>(g_ai[side], 0x64));
    for (int i = 0; i < NFRAMES; ++i) {
        const I3D_frame *f = FrameAt(i);
        s += " | f" + std::to_string(i) + " " + Hex(f->m_frameFlags);
    }
    return s;
}

// ---- the operations ----
enum {
    OP_OPTIMIZED, OP_TEST_SIGHT, OP_RESET_AI, OP_SIGHT_EVENT, OP_UPDATE, OP_TICK, OP_VELOCITY, OP_HEAD_LOOK, OP_SET_LOOK,
    OP_UPDATE_LOOK, OP_HIT_DAMAGE, OP_HIT, OP_D0, OP_SOUND, OP_HEAR, W_ANSWERS, OPS
};
struct S_Op {
    int kind, a, b, t;
    float v[4];
    float amount;
    S_vector pos;
    int chapter;
    float intensity;
};
S_Op RandomOp()
{
    S_Op op{};
    // (The look-around's next point is rare by chance: its operation weighted up.)
    op.kind = P(0.12) ? int(OP_UPDATE_LOOK) : I(0, OPS - 1);
    op.a = P(0.3) ? Ip({0, 1, 2, 4, 6, 8, 0x18, 0x14, 0x1A, 0x19}) : I(-1, 0x20);
    op.b = P(0.3) ? Ip({0, 1, 2, 3, -1}) : I(-5, 6);
    op.t = P(0.2) ? Ip({0, 1, 10, 100, 1000}) : I(0, 300);
    const S_vector4 d = RandomPos();
    op.v[0] = d.x;
    op.v[1] = d.y;
    op.v[2] = d.z;
    op.v[3] = d.w;
    op.amount = P(0.15) ? -1.0f : P(0.1) ? 0.0f : F(0, 300);
    op.pos = S_vector(F(-3, 3), F(-1, 3), F(-3, 3));
    op.chapter = P(0.8) ? I(0, 10) : I(-1, 14);
    op.intensity = P(0.3) ? Fp({0.95f / 3.0f, 0.3166667f, 0.32f, 0.0f, 1.0f}) : F(0, 1);
    return op;
}
// (The port's virtuals called qualified: its block's vtable is the test's.)
void Run(int side, const S_Op &op)
{
    static const bool trace = std::getenv("HD2_MT_TRACE") != nullptr;
    if (trace)
        std::printf("round %d side %d op %d %d %d %d\n", g_round, side, op.kind, op.a, op.b, op.t);
    C_MeleeTracker &m = Mt();
    void *x = g_tracker[side];
    switch (op.kind) {
    case OP_OPTIMIZED:
        side ? m.SetOptimized(op.b & 1) : OriginalAt<void(TC *)(void *, int)>(0x005d5870)(x, op.b & 1);
        break;
    case OP_TEST_SIGHT: {
        if (!g_w.hasAi) {
            Log("skipped: no controller");   // (both read it unchecked)
            break;
        }
        int status = -7;
        float visibility = -7.0f;
        void *target = Human(side, op.b & 3);
        side ? m.TestSight(static_cast<C_Actor *>(target), &status, &visibility)
             : OriginalAt<void(TC *)(void *, void *, int *, float *)>(0x005d64c0)(x, target, &status, &visibility);
        Log("result %d %s", status, FloatText(visibility).c_str());
        break;
    }
    case OP_RESET_AI:
        if (!g_w.hasAi) {
            Log("skipped: no controller");   // (the original calls through it unchecked; the port's fix)
            break;
        }
        side ? m.ResetAI() : OriginalAt<void(TC *)(void *)>(0x0041d790)(x);
        break;
    case OP_SIGHT_EVENT: {
        const int index = op.a & 7;
        // (The original reads a target past the list or a free slot unchecked: the port's fix skips it.)
        if (g_w.hasAi && !(At<uint32_t>(g_ai[side], 0x20) & 0x40000000u) && (index >= TARGETS || !g_w.targetActive[index])) {
            Log("skipped: no such target");
            break;
        }
        side ? m.OnSightEvent(op.b & 1, index) : OriginalAt<void(TC *)(void *, int, int)>(0x005d5cb0)(x, op.b & 1, index);
        break;
    }
    case OP_UPDATE:
        if (!g_w.hasAi && !Get<bool>(side, "m_optimized") && g_w.movement374 != 4) {
            Log("skipped: no controller");   // (the original calls it unchecked)
            break;
        }
        if (!g_w.overrides && Get<bool>(side, "m_lookActive") && !Get<bool>(side, "m_optimized") && g_w.movement374 != 4) {
            Log("skipped: no head override");   // (its head look crashes on it: the port's fix)
            break;
        }
        side ? m.C_MeleeTracker::Update(op.t) : OriginalAt<void(TC *)(void *, int)>(0x005d5810)(x, op.t);
        break;
    case OP_TICK:
        if (!g_w.hasAi && !Get<bool>(side, "m_optimized") && g_w.movement374 != 4) {
            Log("skipped: no controller");
            break;
        }
        side ? m.C_MeleeTracker::Tick(op.t) : OriginalAt<void(TC *)(void *, int)>(0x005d5620)(x, op.t);
        break;
    case OP_VELOCITY:
        side ? m.TrackVelocity(op.t) : OriginalAt<void(TC *)(void *, int)>(0x005d64e0)(x, op.t);
        break;
    case OP_HEAD_LOOK:
        if (!g_w.overrides && Get<bool>(side, "m_lookActive")) {
            Log("skipped: no head override");   // (the original crashes on it: the port's fix)
            break;
        }
        side ? m.HeadLook(op.t) : OriginalAt<void(TC *)(void *, int)>(0x005d7a10)(x, op.t);
        break;
    case OP_SET_LOOK: {
        const S_vector4 target = {op.v[0], op.v[1], op.v[2], op.v[3]};
        side ? m.SetLookBehaviour(target, uint32_t(op.a), uint32_t(op.b))
             : OriginalAt<void(TC *)(void *, const S_vector4 *, uint32_t, uint32_t)>(0x005d8070)(x, &target, uint32_t(op.a), uint32_t(op.b));
        break;
    }
    case OP_UPDATE_LOOK:
        side ? m.UpdateLookBehaviour(op.t) : OriginalAt<void(TC *)(void *, int)>(0x005d82b0)(x, op.t);
        break;
    case OP_HIT_DAMAGE: {
        using T = float(TC *)(void *, const S_vector *, float);
        Log("result %s", FloatText(side ? m.HitDamage(op.pos, op.amount) : OriginalAt<T>(0x005d73b0)(x, &op.pos, op.amount)).c_str());
        break;
    }
    case OP_HIT: {
        if (!g_w.hasAi && IsServer() && Get<S_MeleeTrackerStats>(side, "m_stats").health > 0) {
            Log("skipped: no controller");
            break;
        }
        const S_vector dir(op.v[0], op.v[1], op.v[2]);
        void *attacker = op.b >= 0 && op.b < HUMANS ? Human(side, op.b) : nullptr;
        using T = void(TC *)(void *, int, float, const S_vector *, const S_vector *, void *, int);
        side ? m.C_MeleeTracker::Hit(static_cast<E_HitType>(op.a & 0xF), op.amount, op.pos, dir, static_cast<C_Actor *>(attacker), op.t)
             : OriginalAt<T>(0x005d7180)(x, op.a & 0xF, op.amount, &op.pos, &dir, attacker, op.t);
        break;
    }
    case OP_D0: {
        if (!g_w.hasAi && !IsServer()) {
            Log("skipped: no controller");
            break;
        }
        const int health = op.b * 13 + (op.a & 3) * 40;
        using T = void(TC *)(void *, int, int, int, int);
        side ? m.C_MeleeTracker::UnknownD0(health, 0, 0, 0) : OriginalAt<T>(0x005d7300)(x, health, 0, 0, 0);
        break;
    }
    case OP_SOUND: {
        if (!g_w.hasAi) {
            Log("skipped: no controller");
            break;
        }
        if ((op.a & 3) == 3) {
            Log("skipped: no listener");   // (the original reads its actor unchecked: the port's fix)
            break;
        }
        S_HeardSound sound = {};
        // (Now and then exactly the hearing range away along x.)
        sound.pos = (op.a & 8) ? S_vector(g_w.pos.x + float(g_w.hearRange), g_w.pos.y, g_w.pos.z) : (op.a & 4) ? S_vector(g_w.pos.x + op.pos.x * 4, g_w.pos.y, g_w.pos.z + op.pos.z * 4) : S_vector(op.v[0], op.v[1], op.v[2]);
        sound.posW = op.v[3];
        sound.listener = (op.a & 3) == 3 ? nullptr : Model();
        sound.intensity = op.intensity;
        sound.chapter = op.chapter;
        sound.source = op.b >= 0 && op.b < HUMANS ? static_cast<C_Actor *>(Human(side, op.b)) : nullptr;
        side ? C_MeleeTracker::OnSoundHeard(&sound) : OriginalAt<void(__stdcall *)(const S_HeardSound *)>(0x005d6000)(&sound);
        break;
    }
    case OP_HEAR:
        if (!g_w.hasAi) {
            Log("skipped: no controller");
            break;
        }
        side ? m.HearPlayers() : OriginalAt<void(TC *)(void *)>(0x005d7760)(x);
        break;
    default:
        break;
    }
}
// (P() inside Run would differ between the sides: OP_SOUND's position choice comes from the operation's own numbers.)

struct S_Run {
    std::vector<std::string> log;
};
S_Run g_runs[2];
void TestScenarios()
{
    RandomWorld();
    const S_World start = g_w;
    std::vector<S_Op> ops;
    for (int n = I(1, 12); n > 0; --n)
        ops.push_back(RandomOp());
    std::vector<S_World> answers;
    for (size_t i = 0; i < ops.size(); ++i) {
        RandomWorld();
        answers.push_back(g_w);
    }
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        g_w = start;
        g_log.clear();
        SetWorld(side);
        for (size_t i = 0; i < ops.size(); ++i) {
            const S_Op &op = ops[i];
            Log("-- %d %d %d %d", op.kind, op.a, op.b, op.t);
            if (op.kind == W_ANSWERS) {
                const S_World &a = answers[i];
                g_w.seqA = a.seqA;
                g_w.aiTarget = a.aiTarget;
                std::memcpy(g_w.humanSpeed, a.humanSpeed, sizeof a.humanSpeed);
            } else
                Run(side, op);
            g_log.push_back(Snap(side));
        }
        FreeLists(side);
        g_runs[side].log.swap(g_log);
    }
    static const int dump = std::getenv("HD2_MT_DUMP") ? std::atoi(std::getenv("HD2_MT_DUMP")) : -1;
    if (g_round == dump)
        for (size_t i = 0; i < std::max(g_runs[0].log.size(), g_runs[1].log.size()); ++i)
            std::printf("%s\n  %s\n", i < g_runs[0].log.size() ? g_runs[0].log[i].c_str() : "-", i < g_runs[1].log.size() ? g_runs[1].log[i].c_str() : "-");
    CompareLogs(g_runs[0].log, g_runs[1].log);
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

}  // namespace H
#include "mt_life.inc"
namespace H {

void SetUp()
{
    g_partName = PartName;
    g_frameVt[0x20 / 4] = reinterpret_cast<void *>(M_Tick);
    for (void *&s : g_cameraVt)
        s = reinterpret_cast<void *>(V_Other);
    g_cameraVt[0x48 / 4] = reinterpret_cast<void *>(O_CamSetViewDir);
    Put(g_cameraObj, 0, static_cast<void *>(g_cameraVt));
    for (int side = 0; side < 2; ++side) {
        for (void *&s : g_aiVt[side])
            s = reinterpret_cast<void *>(V_Other);
        for (void *&s : g_trackerVt[side])
            s = reinterpret_cast<void *>(V_Other);
        for (void *&s : g_targetVt[side])
            s = reinterpret_cast<void *>(V_Other);
    }
    const struct {
        int orig, port;
        void *fake;
    } aiSlots[] = {
        {0x14, Slot(&C_MeleeTrackerController::FrameUpdate), reinterpret_cast<void *>(V_FrameUpdate)},
        {0x18, Slot(&C_MeleeTrackerController::Tick), reinterpret_cast<void *>(V_Tick)},
        {0x1C, Slot(&C_MeleeTrackerController::TestSight), reinterpret_cast<void *>(V_TestSight)},
        {0x24, Slot(&C_MeleeTrackerController::GetTarget), reinterpret_cast<void *>(V_GetTarget)},
        {0x3C, Slot(&C_MeleeTrackerController::ReleaseScript), reinterpret_cast<void *>(V_ReleaseScript)},
        {0x44, Slot(&C_MeleeTrackerController::OnBulletHit), reinterpret_cast<void *>(V_OnBulletHit)},
        {0x48, Slot(&C_MeleeTrackerController::OnHeardSound), reinterpret_cast<void *>(V_OnHeardSound)},
        {0x4C, Slot(&C_MeleeTrackerController::OnSpottedEnemy), reinterpret_cast<void *>(V_OnSpottedEnemy)},
        {0x50, Slot(&C_MeleeTrackerController::OnLostEnemy), reinterpret_cast<void *>(V_OnLostEnemy)},
        {0x54, Slot(&C_MeleeTrackerController::OnSeenHuman), reinterpret_cast<void *>(V_OnSeenHuman)},
    };
    for (const auto &s : aiSlots) {
        g_aiVt[0][s.orig / 4] = s.fake;
        g_aiVt[1][s.port / 4] = s.fake;
    }
    g_trackerVt[0][0xD4 / 4] = reinterpret_cast<void *>(V_TrackerHit);
    g_trackerVt[1][Slot(&C_Actor::Hit) / 4] = reinterpret_cast<void *>(V_TrackerHit);
    g_targetVt[0][0] = reinterpret_cast<void *>(T_GetActor);
    g_targetVt[0][1] = reinterpret_cast<void *>(T_GetKind);
    g_targetVt[1][Slot(&C_SightTarget::GetActor) / 4] = reinterpret_cast<void *>(T_GetActor);
    g_targetVt[1][Slot(&C_SightTarget::GetKind) / 4] = reinterpret_cast<void *>(T_GetKind);
    for (void *&s : g_overrideSetVt)
        s = reinterpret_cast<void *>(OV_Other);
    for (void *&s : g_overrideItemVt)
        s = reinterpret_cast<void *>(OV_Other);
    g_overrideItemVt[0x08 / 4] = reinterpret_cast<void *>(OV_Clear);
    g_overrideItemVt[0x1C / 4] = reinterpret_cast<void *>(OV_SetDir);
    Put(g_overrideSet, 0, static_cast<void *>(g_overrideSetVt));
    Put(g_overrideSet, 8, static_cast<void *>(g_overrideItems));
    Put(g_overrideSet, 0x10, 1);
    Put(g_overrideItem, 0, static_cast<void *>(g_overrideItemVt));
    for (void *&s : g_soundVt)
        s = reinterpret_cast<void *>(V_Other);
    g_soundVt[0x40 / 4] = reinterpret_cast<void *>(O_RemoveListener);
    Put(g_sound, 0, static_cast<void *>(g_soundVt));
    // The copy protection's check passes (HitDamage cuts a cracked copy's damage).
    *OriginalAt<uint16_t *>(0x008cac8e) = 0;
    *OriginalAt<uint16_t *>(0x008cb38e) = 0;
    *OriginalAt<uint32_t *>(0x008ca840) = 0x1234;
    *OriginalAt<uint32_t *>(0x008caf40) = 0x1234;
    // The sighting pool's growth (its static initialiser did not run).
    *OriginalAt<int *>(0x008ac938 + 0x0C) = 16;
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x005c86e0, reinterpret_cast<const void *>(O_MvSetOptimized)},
        {0x005c18c0, reinterpret_cast<const void *>(O_MvUpdateAnimation)},
        {0x005c5030, reinterpret_cast<const void *>(O_MvBuildSequence)},
        {0x005c4cb0, reinterpret_cast<const void *>(O_MvStepAnimation)},
        {0x005c8b30, reinterpret_cast<const void *>(O_MvUpdate)},
        {0x005c8490, reinterpret_cast<const void *>(O_MvSetMode)},
        {0x005c8540, reinterpret_cast<const void *>(O_MvSetSubModeA)},
        {0x005c85a0, reinterpret_cast<const void *>(O_MvGetSeqAnimA)},
        {0x005ce6e0, reinterpret_cast<const void *>(O_Disable)},
        {0x005a5cc0, reinterpret_cast<const void *>(O_MoveSpeedClass)},
        {0x00770990, reinterpret_cast<const void *>(O_ScaleStance)},
        {0x0046fd50, reinterpret_cast<const void *>(O_RemoveObserver)},
        {0x0046fdf0, reinterpret_cast<const void *>(O_RemoveTarget)},
        {0x00475e00, reinterpret_cast<const void *>(O_GetHuman)},
        {0x004336c0, reinterpret_cast<const void *>(O_GetSoundModule)},
        {0x005d5c70, reinterpret_cast<const void *>(O_PrintText)},
        {0x006c7600, reinterpret_cast<const void *>(O_GetValues)},
        {0x007e5302, reinterpret_cast<const void *>(O_RandFloat)},
        {0x0069a620, reinterpret_cast<const void *>(O_GetCameraModule)},
        {0x007e53c2, reinterpret_cast<const void *>(O_AlignedMalloc)},   // (the sighting pool's growth)
        {0x007e559c, reinterpret_cast<const void *>(O_AlignedFree)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    // The clocks' import slots.
    const struct {
        uint32_t slot;
        const void *to;
    } imports[] = {
        {0x0080f3f8, reinterpret_cast<const void *>(O_TimeGetTime)},
        {0x0080f1c4, reinterpret_cast<const void *>(O_QPF)},
        {0x0080f1c8, reinterpret_cast<const void *>(O_QPC)},
    };
    for (const auto &i : imports) {
        void **slot = OriginalAt<void **>(i.slot);
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        *slot = const_cast<void *>(i.to);
        VirtualProtect(slot, 4, old, &old);
    }
    SetUpLife();
    g_log.clear();
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("mt_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_MT_QUICK") ? 20000 : 60000;
    if (const char *r = std::getenv("HD2_MT_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "MeleeTracker";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    // Part 82b's: the life cycle.
    int lifeRounds = std::getenv("HD2_MT_QUICK") ? 8000 : 25000;
    if (const char *r = std::getenv("HD2_MT_LIFE_ROUNDS"))
        lifeRounds = std::atoi(r);
    g_case = "MeleeTracker life";
    for (g_round = 0; g_round < lifeRounds; ++g_round)
        TestLife();
    std::printf("mt_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
