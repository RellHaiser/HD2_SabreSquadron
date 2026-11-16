// Part 37 of the human harness (HUMAN_MTAI builds, build_mtai.bat; included by human_test.cpp after aicombat_test.inc,
// whose fakes and helpers it uses): the meleeTrackers' controllers (game/melee_tracker_ai.cpp) against the original's.
//
// The controllers' layouts are the original's (the header's static_asserts), so each side's controller is the same
// bytes with its own vtable; a meleeTracker on each side likewise (C_MeleeTracker's layout is the original's too): the
// fields the controllers read are set alike, the movement's and the tracker's members they call are logged fakes.
#include "game/melee_tracker_ai.h"
#include "game/melee_tracker_movement.h"
#include "script/script.h"

// ---- what the controllers call, faked on both sides (the port's members here, the original's redirected below)
struct S_MeleeTrackerScript {
    int seqA = 0, seqC = 0;                             // the movement's GetSeqAnimA / GetSeqAnimC
    bool camera78 = false;                              // the camera module's GetUnknown78
    std::vector<uint16_t> path;                         // FindMeleeTrackerPath's checkpoints
    bool pathFound = true;
    int animClass = 0;                                  // MeleeTrackerAnimClass
    uint64_t now = 0;                                   // MicrosecondsNow
    bool route = true;                                  // RouteAroundCars
} g_mt;

void C_MeleeTrackerMovement::SetMode(int mode, int priority) { Log(this, "MvSetMode", {double(mode), double(priority)}); }
void C_MeleeTrackerMovement::SetSubModeA(int value, int priority) { Log(this, "MvSetSubModeA", {double(value), double(priority)}); }
void C_MeleeTrackerMovement::SetSubModeB(int value, int priority) { Log(this, "MvSetSubModeB", {double(value), double(priority)}); }
void C_MeleeTrackerMovement::SetFacing(const float *dir) { Log(this, "MvSetFacing", {dir[0], dir[1], dir[2]}); }
void C_MeleeTrackerMovement::SetOptimizedController(bool on) { Log(this, "MvSetOptimizedController", {double(on)}); }
void C_MeleeTrackerMovement::CheckAttackSequence() { Log(this, "MvCheckAttackSequence"); }
int C_MeleeTrackerMovement::GetSeqAnimA()
{
    Log(this, "MvGetSeqAnimA");
    return g_mt.seqA;
}
int C_MeleeTrackerMovement::GetSeqAnimC()
{
    Log(this, "MvGetSeqAnimC");
    return g_mt.seqC;
}
void C_MeleeTracker::SetOptimized(bool on) { Log(this, "MtSetOptimized", {double(on)}); }
void C_MeleeTracker::SetController(int mode) { Log(this, "MtSetController", {double(mode)}); }
void C_Script::Advance() { Log(this, "ScriptAdvance"); }
void C_Script::OnAlarm() { Log(this, "ScriptOnAlarm"); }
bool C_CameraModule::GetUnknown78()
{
    Log(nullptr, "CameraGetUnknown78");
    return g_mt.camera78;
}
void C_CameraModule::SetViewDir(const S_vector4 &dir) { Log(nullptr, "CameraSetViewDir", {dir.x, dir.y, dir.z}); }
bool C_CheckpointNetwork::FindMeleeTrackerPath(int from, int to, std::vector<uint16_t> *path)
{
    Log(this, "FindMeleeTrackerPath", {double(from), double(to)});
    *path = g_mt.path;
    return g_mt.pathFound;
}
void C_Human::RaiseEvent(S_HumanEvent *event) { LogEvent(this, "HumanRaiseEvent", reinterpret_cast<const uint8_t *>(event)); }
int MeleeTrackerAnimClass(int group)
{
    Log(nullptr, "MeleeTrackerAnimClass", {double(group)});
    return g_mt.animClass;
}
void MeleeTrackerBarkTurn(C_MeleeTracker *meleeTracker) { Log(meleeTracker, "MeleeTrackerBarkTurn"); }
uint64_t MicrosecondsNow() { return g_mt.now; }
void ReleaseMeleeTrackerSighting(S_MeleeTrackerSighting *sighting) { Log(sighting, "ReleaseSighting"); }
bool __fastcall WriteControlState(C_NetMessage *, const float *pos, const float *velocity, const float *dir, const void *,
                                  uint32_t bits)
{
    Log(nullptr, "WriteControlState", {pos[0], pos[1], pos[2], velocity[0], velocity[1], velocity[2], dir[0], dir[1], dir[2], double(bits)});
    return true;
}
bool __fastcall ReadControlState(C_NetMessage *, float *, float *, float *, void *, uint32_t *) { AiUnexpected(__PRETTY_FUNCTION__); }
bool C_MeleeTrackerAI::RouteAroundCars()
{
    Log(this, "RouteAroundCars");
    return g_mt.route;
}

namespace {
#define MT __attribute__((thiscall))
// ---- the original's side of the fakes (the same logs)
void MT OM_SetMode(void *self, int mode, int priority) { Log(self, "MvSetMode", {double(mode), double(priority)}); }
void MT OM_SetSubModeA(void *self, int value, int priority) { Log(self, "MvSetSubModeA", {double(value), double(priority)}); }
void MT OM_SetSubModeB(void *self, int value, int priority) { Log(self, "MvSetSubModeB", {double(value), double(priority)}); }
void MT OM_SetFacing(void *self, const float *dir) { Log(self, "MvSetFacing", {dir[0], dir[1], dir[2]}); }
void MT OM_SetOptimizedController(void *self, int on) { Log(self, "MvSetOptimizedController", {double(on & 0xFF)}); }
void MT OM_CheckAttackSequence(void *self) { Log(self, "MvCheckAttackSequence"); }
int MT OM_GetSeqAnimA(void *self)
{
    Log(self, "MvGetSeqAnimA");
    return g_mt.seqA;
}
int MT OM_GetSeqAnimC(void *self)
{
    Log(self, "MvGetSeqAnimC");
    return g_mt.seqC;
}
void MT OM_SetOptimized(void *self, int on) { Log(self, "MtSetOptimized", {double(on & 0xFF)}); }
void MT OM_SetController(void *self, int mode) { Log(self, "MtSetController", {double(mode)}); }
void MT OM_ScriptAdvance(void *self) { Log(self, "ScriptAdvance"); }
void MT OM_ScriptOnAlarm(void *self) { Log(self, "ScriptOnAlarm"); }
int MT OM_CameraGetUnknown78(void *)
{
    Log(nullptr, "CameraGetUnknown78");
    return g_mt.camera78;
}
void MT OM_CameraSetViewDir(void *, const S_vector4 *dir) { Log(nullptr, "CameraSetViewDir", {dir->x, dir->y, dir->z}); }
void MT OM_HumanRaiseEvent(void *self, uint8_t *event) { LogEvent(self, "HumanRaiseEvent", event); }
int __cdecl OM_AnimClass(int group)
{
    Log(nullptr, "MeleeTrackerAnimClass", {double(group)});
    return g_mt.animClass;
}
void MT OM_BarkTurn(void *meleeTracker) { Log(meleeTracker, "MeleeTrackerBarkTurn"); }   // (the tracker in ECX)
int MT OM_RouteAroundCars(void *self)
{
    Log(self, "RouteAroundCars");
    return g_mt.route;
}
void *__cdecl OM_New(size_t size) { return std::malloc(size); }
void __cdecl OM_Delete(void *p) { std::free(p); }

// ---- the meleeTracker: a block per side (the port's C_Actor packs its vectors 4-aligned: its fields sit elsewhere than
// the original's, whose offsets the header names), the fields the controllers read set alike (the original's: id +0x10,
// type +0x18, frame +0x20, position +0x30, direction +0x40, the movement +0x90 (its own layout the same on both sides),
// the stats +0x4A0) and the ones they write compared after each call; its frame part 35b's.
alignas(16) uint8_t g_mtPortBytes[sizeof(C_MeleeTracker) + 0x40], g_mtOrigBytes[0x600];
C_MeleeTracker *PortMt() { return reinterpret_cast<C_MeleeTracker *>(g_mtPortBytes); }
C_Actor *PortMtActor() { return reinterpret_cast<C_Actor *>(g_mtPortBytes); }
uint8_t *PortMtMovement() { return reinterpret_cast<uint8_t *>(&PortMt()->GetMovement()); }
constexpr size_t MV_SPEED_TARGET = 0x248, MV_AIR_STEPS = 0x24C;   // (within the movement)
void MtTrackerToOrig()
{
    const C_Actor *a = PortMtActor();
    std::memcpy(g_mtOrigBytes + 0x10, &a->m_netId, 4);
    std::memcpy(g_mtOrigBytes + 0x18, &a->m_type, 4);
    std::memcpy(g_mtOrigBytes + 0x20, &a->m_frame, 4);
    std::memcpy(g_mtOrigBytes + 0x30, &a->m_position, 16);
    std::memcpy(g_mtOrigBytes + 0x40, &a->m_direction, 16);
    std::memcpy(g_mtOrigBytes + 0x4A0, &PortMt()->GetStats(), 16);
    std::memcpy(g_mtOrigBytes + 0x90 + MV_SPEED_TARGET, PortMtMovement() + MV_SPEED_TARGET, 8);
}
std::string MtTrackerDiffers()
{
    if (std::memcmp(g_mtOrigBytes + 0x4A0, &PortMt()->GetStats(), 16) != 0)
        return "tracker stats";
    if (std::memcmp(g_mtOrigBytes + 0x90 + MV_SPEED_TARGET, PortMtMovement() + MV_SPEED_TARGET, 4) != 0)
        return "tracker speed target";
    return "";
}
alignas(16) uint8_t g_mtScriptBytes[0x100];             // the tracker's script (its Advance / OnAlarm logged)
S_FrameData g_mtFrameData;

// ---- the controllers: the port's (test classes over the variants) and the original's (its constructor, the variant's
// vtable: SetController's 0x00812250 scripted, 0x008122c8 automatic).
struct S_MtScripted : C_MeleeTrackerScriptedAI {
};
struct S_MtAuto : C_MeleeTrackerAutoAI {
};
S_MtScripted *g_pMtS;
S_MtAuto *g_pMtA, *g_pMtSrc;
uint8_t *g_oMtS, *g_oMtA, *g_oMtSrc;
// The pair under test.
C_MeleeTrackerAI *g_pMt;
uint8_t *g_oMt;
size_t g_mtSize;
// The behaviour weights each side's Update points at (the port's statics, the original's 0x008ac820 / 0x008ac720).
float *g_mtPortActionA, *g_mtPortActionB;
float *OrigActionA() { return OriginalAt<float *>(0x008ac820); }
float *OrigActionB() { return OriginalAt<float *>(0x008ac720); }
alignas(16) float g_mtTestActionA[0x40], g_mtTestActionB[0x40];

// A waypoint of the original's pool (its AllocMeleeTrackerWaypoint, 0x005ce4c0: the pool's Get inline; the caller copies
// the port's waypoint over it).
uint8_t *OrigMtWaypoint()
{
    typedef void *(MT * T_Alloc)(void *, int, const S_vector4 *, int, int, int);
    static const S_vector4 zero = {0, 0, 0, 0};
    return static_cast<uint8_t *>(OriginalAt<T_Alloc>(0x005ce4c0)(OriginalAt<void *>(0x008ac920), 0, &zero, 0, 0, 0));
}

// The port's controller in the original's layout: the same bytes; the actors mapped; the weights' arrays the original's
// (their contents copied); the path an MSVC6 vector of the original pool's waypoints.
void MtToOrig(const C_MeleeTrackerAI *pc, uint8_t *oc, size_t size)
{
    const auto *pb = reinterpret_cast<const uint8_t *>(pc);
    std::memcpy(oc + 4, pb + 4, size - 4);
    auto mapPointer = [&](size_t at) {
        void *p;
        std::memcpy(&p, pb + at, 4);
        p = p ? ToOrig(p) : nullptr;
        std::memcpy(oc + at, &p, 4);
    };
    for (size_t at : {size_t(0x10), size_t(0x18), size_t(0x88), size_t(0x8C)})
        mapPointer(at);
    for (int k = 0; k < 2; ++k) {
        float *p = k ? pc->m_actionB : pc->m_actionA;
        float *o = nullptr;
        if (p) {
            o = k ? OrigActionB() : OrigActionA();
            std::memcpy(o, p, 0x40 * 4);
        }
        std::memcpy(oc + (k ? 0x64 : 0x60), &o, 4);
    }
    uint32_t v[3] = {0, 0, 0};
    const size_t count = pc->m_pathBegin ? size_t(pc->m_pathEnd - pc->m_pathBegin) : 0;
    if (pc->m_pathBegin) {
        auto **data = static_cast<uint8_t **>(std::malloc(std::max<size_t>(1, size_t(pc->m_pathCap - pc->m_pathBegin)) * 4));
        for (size_t k = 0; k < count; ++k) {
            data[k] = OrigMtWaypoint();
            std::memcpy(data[k], pc->m_pathBegin[k], sizeof(S_AIWaypoint));
        }
        v[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        v[1] = v[0] + 4 * static_cast<uint32_t>(count);
        v[2] = v[0] + 4 * static_cast<uint32_t>(pc->m_pathCap - pc->m_pathBegin);
    }
    std::memcpy(oc + 0x78, v, 12);
}
// After a call: the two alike (the vtables apart; the actors, the weights and the path as above).
std::string MtDiffers(const C_MeleeTrackerAI *pc, const uint8_t *oc, size_t size)
{
    const auto *pb = reinterpret_cast<const uint8_t *>(pc);
    for (size_t at = 4; at < size; at += 4) {
        if (at >= 0x78 && at < 0x84)
            continue;
        uint32_t a, b;
        std::memcpy(&a, pb + at, 4);
        std::memcpy(&b, oc + at, 4);
        if (at == 0x10 || at == 0x18 || at == 0x88 || at == 0x8C) {
            void *p = reinterpret_cast<void *>(static_cast<uintptr_t>(a));
            a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p ? ToOrig(p) : nullptr));
        } else if (at == 0x60 || at == 0x64) {
            const float *p = reinterpret_cast<const float *>(static_cast<uintptr_t>(a));
            const float *o = reinterpret_cast<const float *>(static_cast<uintptr_t>(b));
            if ((p != nullptr) != (o != nullptr))
                return "weights " + std::to_string(at) + " (null on one side)";
            if (p && std::memcmp(p, o, 0x40 * 4) != 0)
                return "weights " + std::to_string(at);
            continue;
        }
        if (a != b)
            return "controller+" + std::to_string(at);
    }
    uint32_t v[3];
    std::memcpy(v, oc + 0x78, 12);
    const size_t count = v[0] ? (v[1] - v[0]) / 4 : 0;
    const size_t portCount = pc->m_pathBegin ? size_t(pc->m_pathEnd - pc->m_pathBegin) : 0;
    if (count != portCount)
        return "path: " + std::to_string(portCount) + " / " + std::to_string(count);
    for (size_t k = 0; k < count; ++k)
        if (std::memcmp(reinterpret_cast<uint8_t *const *>(static_cast<uintptr_t>(v[0]))[k], pc->m_pathBegin[k], sizeof(S_AIWaypoint)) != 0)
            return "path waypoint " + std::to_string(k);
    return "";
}

// A call on the pair under test (and the source controller, converted too): each side's, compared.
template <class P, class O> void MtCall(const char *what, P port, O original)
{
    MtTrackerToOrig();
    MtToOrig(g_pMt, g_oMt, g_mtSize);
    MtToOrig(g_pMtSrc, g_oMtSrc, 0xC0);
    DiffCall(what, port,
             [&](void *o) {
                 const int r = original(o);
                 // (The w words the original fills from stack words: the move accumulator's (Apply), the facing's
                 // (SetMoveToward), the two target positions' (ClearAttack, SetOwnerActor): the port's.)
                 for (uint8_t *oc : {g_oMt, g_oMtSrc}) {
                     const auto *pc = reinterpret_cast<const uint8_t *>(oc == g_oMt ? static_cast<C_MeleeTrackerAI *>(g_pMt) : g_pMtSrc);
                     for (size_t at : {size_t(0x3C), size_t(0x4C), size_t(0x5C), size_t(0x9C), size_t(0xAC)})
                         std::memcpy(oc + at, pc + at, 4);
                 }
                 return r;
             });
    std::string why = MtDiffers(g_pMt, g_oMt, g_mtSize);
    if (why.empty())
        why = MtDiffers(g_pMtSrc, g_oMtSrc, 0xC0);
    if (why.empty())
        why = MtTrackerDiffers();
    CHECK(why.empty());
    static std::map<std::string, int> shown;
    if (!why.empty() && shown[what]++ < 3)
        std::printf("  %s: %s\n", what, why.c_str());
}
// The original's member at `at` on its controller, and its virtual at a vtable offset.
template <class R, class... A> R OMt(uint32_t at, A... a)
{
    using F = R(MT *)(void *, A...);
    return OriginalAt<F>(at)(static_cast<void *>(g_oMt), a...);
}
template <class R, class... A> R OMtV(size_t slot, A... a)
{
    using F = R(MT *)(void *, A...);
    void **vt = *reinterpret_cast<void ***>(g_oMt);
    return reinterpret_cast<F>(vt[slot / 4])(static_cast<void *>(g_oMt), a...);
}

void SetUpMeleeTrackerAi()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x005c8490, FAKE(OM_SetMode)},
        {0x005c8540, FAKE(OM_SetSubModeA)},
        {0x005c8570, FAKE(OM_SetSubModeB)},
        {0x005c1e60, FAKE(OM_SetFacing)},
        {0x005c86c0, FAKE(OM_SetOptimizedController)},
        {0x005c5a90, FAKE(OM_CheckAttackSequence)},
        {0x005c85a0, FAKE(OM_GetSeqAnimA)},
        {0x005c8660, FAKE(OM_GetSeqAnimC)},
        {0x005d5870, FAKE(OM_SetOptimized)},
        {0x005d5890, FAKE(OM_SetController)},
        {0x00416cf0, FAKE(OM_ScriptAdvance)},
        {0x0041da20, FAKE(OM_ScriptOnAlarm)},
        {0x005a8b30, FAKE(OM_HumanRaiseEvent)},
        {0x005d86f0, FAKE(OM_AnimClass)},
        {0x005c6c70, FAKE(OM_BarkTurn)},
        {0x005ca5a0, FAKE(OM_RouteAroundCars)},
        {0x007e566f, FAKE(OM_New)},
        {0x007e2120, FAKE(OM_Delete)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    g_camModuleVt[0x78 / 4] = reinterpret_cast<void *>(OM_CameraGetUnknown78);
    g_camModuleVt[0x48 / 4] = reinterpret_cast<void *>(OM_CameraSetViewDir);
    // The waypoint pool's growth (its static initialiser did not run), both of its pools.
    *OriginalAt<int *>(0x008ac920 + 0x0C) = 16;
    *OriginalAt<int *>(0x008ac920 + 0x18 + 0x0C) = 16;
    // The trackers.
    std::memset(g_mtPortBytes, 0, sizeof g_mtPortBytes);
    std::memset(g_mtOrigBytes, 0, sizeof g_mtOrigBytes);
    C_Actor *a = PortMtActor();
    a->m_type = ACTOR_MELEE_TRACKER;
    a->m_frame = g_asMeleeFrame;
    a->m_netId = 120;
    g_names[g_mtPortBytes] = g_names[g_mtOrigBytes] = "mt";
    g_names[PortMtMovement()] = g_names[g_mtOrigBytes + 0x90] = "mtMovement";
    g_names[g_mtScriptBytes] = "mtScript";
    g_extraPointers.push_back({g_mtPortBytes, g_mtOrigBytes});
    // The controllers.
    g_pMtS = new S_MtScripted;
    g_pMtA = new S_MtAuto;
    g_pMtSrc = new S_MtAuto;
    typedef void(MT * T_Ctor)(void *, int);
    const struct {
        uint8_t **o;
        size_t size;
        int mode;
        uint32_t vtable;
    } orig[] = {{&g_oMtS, 0xD0, 5, 0x00812250}, {&g_oMtA, 0xC0, 4, 0x008122c8}, {&g_oMtSrc, 0xC0, 4, 0x008122c8}};
    for (const auto &o : orig) {
        *o.o = static_cast<uint8_t *>(OC_AlignedNew(o.size, 16));
        std::memset(*o.o, 0, o.size);
        OriginalAt<T_Ctor>(0x005cb190)(*o.o, o.mode);
        const uint32_t vt = o.vtable;
        std::memcpy(*o.o, &vt, 4);
    }
    g_names[g_pMtS] = g_names[g_oMtS] = "mtScripted";
    g_names[g_pMtA] = g_names[g_oMtA] = "mtAuto";
    g_names[g_pMtSrc] = g_names[g_oMtSrc] = "mtSource";
    g_extraPointers.push_back({g_pMtS, g_oMtS});
    g_extraPointers.push_back({g_pMtA, g_oMtA});
    g_extraPointers.push_back({g_pMtSrc, g_oMtSrc});
}

// A float of either sign, now and then 0 or 1.
float MtValue() { return P(0.1f) ? 0.0f : P(0.1f) ? 1.0f : U(-1, 1); }
// A position about the tracker.
S_vector4 MtPos() { return {U(-30, 30), U(-2, 2), U(-30, 30), 1.0f}; }

// The waypoints back to the port's pool, the path array freed (the original's are left: its pool keeps them).
void ClearPortPath(C_MeleeTrackerAI *c)
{
    for (S_AIWaypoint **w = c->m_pathBegin; w && w != c->m_pathEnd; ++w)
        ReleaseMeleeTrackerWaypoint(*w);
    ::operator delete(c->m_pathBegin);
    c->m_pathBegin = c->m_pathEnd = c->m_pathCap = nullptr;
}

// A controller's state at random: the base's fields, the AI's, a path now and then.
void RandomMtController(C_MeleeTrackerAI *c, bool withPath)
{
    C_Human *const humans[3] = {g_h.human, PortOther(), PortThird()};
    c->m_meleeTracker = PortMt();   // (always: both sides read it unchecked)
    c->m_owner = P(0.4f) ? nullptr : P(0.7f) ? static_cast<C_Actor *>(humans[I(0, 2)]) : static_cast<C_Actor *>(PortActor(I(0, ACTOR_KIND_COUNT - 1)));
    c->m_alarm = P(0.5f) ? 0 : I(1, 0x40);
    c->m_flags = (uint32_t(std::rand()) << 17) ^ uint32_t(std::rand());
    c->m_savedPos = MtPos();
    c->m_facing = {MtValue(), MtValue(), MtValue(), MtValue()};
    c->m_050 = MtPos();
    c->m_actionA = P(0.5f) ? nullptr : g_mtTestActionA;
    c->m_actionB = P(0.5f) ? nullptr : g_mtTestActionB;
    c->m_068 = I(0, 50);
    c->m_checkpoints = reinterpret_cast<C_CheckpointNetwork *>(g_checkpointNet);
    c->m_target = P(0.4f) ? nullptr : static_cast<C_Actor *>(humans[I(0, 2)]);
    c->m_orderedTarget = P(0.5f) ? nullptr : static_cast<C_Actor *>(humans[I(0, 2)]);
    c->m_090 = P(0.3f) ? S_vector4{-10000, -10000, -10000, 0} : MtPos();
    c->m_0a0 = P(0.3f) ? S_vector4{-10000, -10000, -10000, 0} : MtPos();
    // (The timers now and then exactly 0 or 1: their tests' edges.)
    auto timer = [] { return P(0.15f) ? 0 : P(0.15f) ? 1 : I(-500, 2000); };
    c->m_0b0 = timer();
    c->m_0b4 = I(-500, 500);
    c->m_changed = uint8_t(P(0.5f));
    c->m_0bc = timer();
    c->m_084 = timer();
    ClearPortPath(c);
    if (withPath && P(0.6f)) {
        const int n = I(1, 5);
        c->m_pathBegin = static_cast<S_AIWaypoint **>(::operator new(sizeof(void *) * (n + I(0, 2))));
        c->m_pathEnd = c->m_pathBegin;
        for (int k = 0; k < n; ++k)
            *c->m_pathEnd++ = AllocMeleeTrackerWaypoint(I(0, 300), MtPos(), uint16_t(I(0, 0xFFFF)), uint8_t(I(0, 20)), uint8_t(I(0, 64)));
        c->m_pathCap = c->m_pathEnd + I(0, 2);
    }
}

// The tracker's own state: where it is, facing, its stats, its frame's data (a script now and then).
void RandomMt()
{
    C_Actor *a = PortMtActor();
    a->m_position = MtPos();
    a->m_direction = {MtValue(), 0, MtValue(), 0};
    a->m_frame = P(0.95f) ? g_asMeleeFrame : g_otherFrame;
    S_MeleeTrackerStats &s = PortMt()->GetStats();
    s = {I(-10, 250), I(0, 80), I(0, 100), I(0, 200)};
    const float speedTarget = U(0, 2), airSteps = P(0.7f) ? 0.0f : U(0, 3);
    std::memcpy(PortMtMovement() + MV_SPEED_TARGET, &speedTarget, 4);
    std::memcpy(PortMtMovement() + MV_AIR_STEPS, &airSteps, 4);
    g_asMeleeFrame->SetPosInline(S_vector(a->m_position.x, a->m_position.y, a->m_position.z));
    UpdateTree(g_asMeleeFrame);
    g_mtFrameData = S_FrameData();
    g_mtFrameData.script = P(0.8f) ? reinterpret_cast<C_Script *>(g_mtScriptBytes) : nullptr;
    g_asMeleeFrame->SetData(P(0.9f) ? &g_mtFrameData : nullptr);
}

// The pair under test: the scripted or the automatic controller.
void PickMtPair(bool scripted)
{
    g_pMt = scripted ? static_cast<C_MeleeTrackerAI *>(g_pMtS) : static_cast<C_MeleeTrackerAI *>(g_pMtA);
    g_oMt = scripted ? g_oMtS : g_oMtA;
    g_mtSize = scripted ? 0xD0 : 0xC0;
}

void RandomMtAll()
{
    RandomSensesB();
    RandomMt();
    // The humans' positions and directions (owners, targets): the copies' in both layouts (the original's +0x30, +0x40;
    // the tested human's through ToOriginal).
    C_Human *const humans[3] = {g_h.human, PortOther(), PortThird()};
    uint8_t *const origs[3] = {nullptr, g_origOther, g_origThird};
    for (int k = 0; k < 3; ++k) {
        humans[k]->m_position = {U(-30, 30), U(-2, 2), U(-30, 30), 1.0f};
        const float yaw = U(0, 6.2831f);
        humans[k]->m_direction = {std::sin(yaw), 0, std::cos(yaw), 0};
        if (origs[k]) {
            std::memcpy(origs[k] + 0x30, &humans[k]->m_position, 16);
            std::memcpy(origs[k] + 0x40, &humans[k]->m_direction, 16);
        }
    }
    const bool scripted = P(0.6f);
    PickMtPair(scripted);
    RandomMtController(g_pMt, true);
    RandomMtController(g_pMtSrc, false);
    g_pMtSrc->m_owner = nullptr;   // (an owner runs FindPath: part 37c)
    if (scripted) {
        g_pMtS->m_script = P(0.85f) ? reinterpret_cast<C_Script *>(g_mtScriptBytes) : nullptr;
        g_pMtS->m_barkTimer = P(0.3f) ? -1 : I(0, 25000);
    }
    for (float &w : g_mtTestActionA)
        w = P(0.7f) ? 0.0f : 1.0f;
    for (float &w : g_mtTestActionB)
        w = P(0.7f) ? 0.0f : 1.0f;
    g_mt.seqA = I(0, 3);
    g_mt.seqC = I(0, 3);
}

// ---- part 37a: the life cycle, the script commands, the timers and the small members
void TestMtA()
{
    std::printf("melee tracker ai: life cycle, commands, timers\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomMtAll();
        C_MeleeTrackerAI *c = g_pMt;
        const bool scripted = g_mtSize == 0xD0;
        const int which = I(0, 21);
        switch (which) {
        case 0:
            c->m_meleeTracker = nullptr;   // (Attach sets it)
            MtCall("Attach", [&] { return int(c->Attach(PortMt())); }, [&](void *) { return int(OMtV<uint32_t>(0x00, static_cast<void *>(g_mtOrigBytes)) & 0xFF); });
            break;
        case 1:
            MtCall("Reset", [&] { c->Reset(); return 0; }, [&](void *) { OMtV<void>(0x04); return 0; });
            break;
        case 2:
            MtCall("Detach", [&] { return int(c->Detach()); }, [&](void *) { return int(OMtV<uint32_t>(0x0C) & 0xFF); });
            break;
        case 3:
            MtCall("ResetState", [&] { c->ResetState(g_pMtSrc); return 0; },
                   [&](void *) { OMtV<void>(0x10, static_cast<void *>(g_oMtSrc)); return 0; });
            break;
        case 4: {
            const int value = P(0.3f) ? 0 : I(1, 0x40);
            MtCall("SetAlarm", [&] { c->SetAlarm(value); return 0; }, [&](void *) { OMtV<void>(0x38, value); return 0; });
            break;
        }
        case 5: {
            // (An owner frame without a human: SetOwnerActor's null case; a human's runs FindPath: part 37c.)
            I3D_frame *frame = P(0.3f) ? nullptr : P(0.5f) ? g_targetFrame : g_asThirdFrame;
            MtCall("SetOwner", [&] { c->SetOwner(frame); return 0; }, [&](void *) { OMt<void>(0x005cf9b0, frame); return 0; });
            break;
        }
        case 6:
            MtCall("TakeChanged", [&] { return int(c->TakeChanged()); }, [&](void *) { return int(OMtV<uint32_t>(0x2C) & 0xFF); });
            break;
        case 7:
            MtCall("Disable", [&] { c->Disable(); return 0; }, [&](void *) { OMt<void>(0x005ce6e0); return 0; });
            break;
        case 8:
            if (!c->m_actionA)
                c->m_actionA = g_mtTestActionA;
            if (!c->m_actionB)
                c->m_actionB = g_mtTestActionB;
            MtCall("Apply", [&] { c->Apply(); return 0; }, [&](void *) { OMt<void>(0x005cec60); return 0; });
            break;
        case 9: {
            static const uint32_t modes[] = {0x005cf820, 0x005cf830, 0x005cf840, 0x005cf850, 0x005cf890, 0x005cf8a0};
            const int k = I(0, 5);
            MtCall("SetMode",
                   [&] {
                       switch (k) {
                       case 0: c->SetModeRun(); break;
                       case 1: c->SetModeWalk(); break;
                       case 2: c->SetModeLie(); break;
                       case 3: c->SetModeStand(); break;
                       case 4: c->SetAggressive(); break;
                       default: c->SetDefensive(); break;
                       }
                       return k;
                   },
                   [&](void *) { OMt<void>(modes[k]); return k; });
            break;
        }
        case 10: {
            const bool on = P(0.5f);
            if (P(0.5f))
                MtCall("Suspend", [&] { c->Suspend(on); return 0; }, [&](void *) { OMt<void>(0x005cf930, int(on)); return 0; });
            else
                MtCall("SetOptimized", [&] { c->SetOptimized(on); return 0; }, [&](void *) { OMt<void>(0x005cf950, int(on)); return 0; });
            break;
        }
        case 11: {
            const int range = I(-5, 300);
            if (P(0.5f))
                MtCall("SetSightRange", [&] { c->SetSightRange(range); return 0; }, [&](void *) { OMt<void>(0x005cf980, range); return 0; });
            else
                MtCall("SetHearRange", [&] { c->SetHearRange(range); return 0; }, [&](void *) { OMt<void>(0x005cf990, range); return 0; });
            break;
        }
        case 12: {
            const int dt = P(0.1f) ? 0 : I(1, 600);
            MtCall("UpdateTimers", [&] { c->UpdateTimers(dt); return 0; }, [&](void *) { OMt<void>(0x005cdf90, dt); return 0; });
            break;
        }
        case 13: {
            if (!c->m_actionA)
                c->m_actionA = g_mtTestActionA;
            if (!c->m_actionB)
                c->m_actionB = g_mtTestActionB;
            const int dt = I(0, 600);
            MtCall("SubUpdate", [&] { c->SubUpdate(dt); return 0; }, [&](void *) { OMtV<void>(0x6C, dt); return 0; });
            break;
        }
        case 14:
            // (Now and then exactly 1 m from the target: the length's exact case.)
            if (c->m_target && P(0.2f)) {
                g_asMeleeFrame->SetPosInline(c->m_target->GetFrame()->GetWorldPos() + S_vector(0, 0, 1));
                UpdateTree(g_asMeleeFrame);
            }
            if (P(0.5f))
                MtCall("DistanceToTarget", [&] { return FloatBitsOf(c->DistanceToTarget()); },
                       [&](void *) {
                           typedef float(MT * T)(void *);
                           return FloatBitsOf(reinterpret_cast<T>((*reinterpret_cast<void ***>(g_oMt))[0x28 / 4])(g_oMt));
                       });
            else
                MtCall("DistanceToOwnerSpot", [&] { return FloatBitsOf(c->DistanceToOwnerSpot()); },
                       [&](void *) {
                           typedef float(MT * T)(void *);
                           return FloatBitsOf(reinterpret_cast<T>((*reinterpret_cast<void ***>(g_oMt))[0x70 / 4])(g_oMt));
                       });
            break;
        case 15: {
            float po[4] = {9, 9, 9, 9}, oo[4] = {9, 9, 9, 9};
            MtCall("OwnerPosition", [&] { c->OwnerPosition(po); return VectorHash(S_vector(po[0], po[1], po[2])) + FloatBitsOf(po[3]); },
                   [&](void *) { OMt<void>(0x005cd690, oo); return VectorHash(S_vector(oo[0], oo[1], oo[2])) + FloatBitsOf(oo[3]); });
            break;
        }
        case 16:
            MtCall("ClearAttack", [&] { c->ClearAttack(); return 0; }, [&](void *) { OMt<void>(0x005cd120); return 0; });
            break;
        case 17: {
            // (Now and then the same point, or a hair away along x or z only: the normalisation's snaps.)
            const S_vector4 a = MtPos();
            S_vector4 b = P(0.7f) ? MtPos() : a;
            if (P(0.2f))
                (P(0.5f) ? b.x : b.z) += P(0.5f) ? 1e-5f : -1e-5f;
            const float from[4] = {a.x, a.y, a.z, a.w}, to[4] = {b.x, b.y, b.z, b.w};
            MtCall("SetMoveToward", [&] { c->SetMoveToward(from, to); return 0; }, [&](void *) { OMt<void>(0x005cc5f0, from, to); return 0; });
            break;
        }
        case 18: {
            // The events: a sound heard (loud or not), an enemy spotted, a human seen.
            static S_MeleeTrackerSighting sighting;
            sighting.pos = MtPos();
            sighting.type = I(0, 3);
            sighting.loudness = I(0, 20);
            sighting.source = nullptr;
            const int k = I(0, 2);
            C_Human *const humans[3] = {g_h.human, PortOther(), PortThird()};
            C_Actor *actor = P(0.2f) ? nullptr : P(0.7f) ? static_cast<C_Actor *>(humans[I(0, 2)]) : static_cast<C_Actor *>(PortActor(I(0, ACTOR_KIND_COUNT - 1)));
            void *oActor = actor ? ToOrig(actor) : nullptr;
            if (k == 0)
                MtCall("OnHeardSound", [&] { c->OnHeardSound(&sighting); return 0; }, [&](void *) { OMtV<void>(0x48, &sighting); return 0; });
            else if (k == 1)
                MtCall("OnSpottedEnemy", [&] { c->OnSpottedEnemy(actor); return 0; }, [&](void *) { OMtV<void>(0x4C, oActor); return 0; });
            else {
                // (The automatic variant's, the base AI's, reads a missing actor's type when ordered to: the port checks.)
                if (!actor && !scripted && (c->m_flags & 0x200))
                    actor = g_h.human, oActor = o_human;
                MtCall("OnSeenHuman", [&] { c->OnSeenHuman(I(0, 3), actor); return 0; }, [&](void *) { OMtV<void>(0x54, 0, oActor); return 0; });
            }
            break;
        }
        case 20: {
            // The base's and the AI's own members the variants override (called directly: the original's addresses).
            C_Human *const humans[3] = {g_h.human, PortOther(), PortThird()};
            C_Actor *actor = P(0.3f) ? nullptr : static_cast<C_Actor *>(humans[I(0, 2)]);
            void *oActor = actor ? ToOrig(actor) : nullptr;
            static S_MeleeTrackerSighting sighting;
            sighting.loudness = I(5, 15);
            switch (I(0, 4)) {
            case 0:
                MtCall("BaseDetach", [&] { return int(c->C_MeleeTrackerController::Detach()); },
                       [&](void *) { return int(OMt<uint32_t>(0x005cb230) & 0xFF); });
                break;
            case 1:
                MtCall("BaseSetOwnerActor", [&] { c->C_MeleeTrackerController::SetOwnerActor(actor); return 0; },
                       [&](void *) { OMt<void>(0x005cb2a0, oActor); return 0; });
                break;
            case 2:
                MtCall("BaseOnHeardSound", [&] { c->C_MeleeTrackerAI::OnHeardSound(&sighting); return 0; },
                       [&](void *) { OMt<void>(0x005cddd0, &sighting); return 0; });
                break;
            case 3:
                MtCall("BaseOnSpottedEnemy", [&] { c->C_MeleeTrackerAI::OnSpottedEnemy(actor); return 0; },
                       [&](void *) { OMt<void>(0x005cde20, oActor); return 0; });
                break;
            default:
                MtCall("BaseReset", [&] { c->C_MeleeTrackerAI::Reset(); return 0; }, [&](void *) { OMt<void>(0x005cb0d0); return 0; });
                break;
            }
            break;
        }
        case 19:
            // The scripted variant's script: released (the tracker gets the automatic controller) or set.
            if (scripted && P(0.5f))
                MtCall("ReleaseScript", [&] { c->ReleaseScript(); return 0; }, [&](void *) { OMtV<void>(0x3C); return 0; });
            else if (scripted) {
                C_Script *script = P(0.3f) ? nullptr : reinterpret_cast<C_Script *>(g_mtScriptBytes);
                MtCall("SetScript", [&] { c->SetScript(script); return 0; }, [&](void *) { OMtV<void>(0x40, static_cast<void *>(script)); return 0; });
            }
            break;
        default:
            if (scripted) {
                const bool on = P(0.6f);
                // (Switched off, the original returns its argument's byte over stale ones: the result compared on.)
                MtCall("SetBarking", [&] { return on ? g_pMtS->SetBarking(on) : (g_pMtS->SetBarking(on), 0); },
                       [&](void *) { return on ? OMt<int>(0x005cfbb0, int(on)) : (OMt<int>(0x005cfbb0, int(on)), 0); });
            }
            break;
        }
    }
}

void TestMeleeTrackerAi()
{
    SetUpSenses();
    SetUpSensesB();
    SetUpCombatAi();
    SetUpFireOrder();
    SetUpMeleeTrackerAi();
    TestMtA();
}
}  // namespace
