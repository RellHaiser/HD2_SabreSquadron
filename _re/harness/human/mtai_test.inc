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
void ReleaseMeleeTrackerSighting(S_MeleeTrackerSighting *) {}   // (silent: the original puts it in its pool inline)
// The control state a message carries (ReadControlState's answer on both sides; part 37c).
struct S_MtNetState {
    float pos[4], velocity[4], dir[4];
    uint8_t unused[0x10];
    uint32_t bits;
} g_mtNet;
void ReadMtNetState(float *pos, float *velocity, float *dir, void *unused, uint32_t *bits)
{
    Log(nullptr, "ReadControlState");
    std::memcpy(pos, g_mtNet.pos, 16);
    std::memcpy(velocity, g_mtNet.velocity, 16);
    std::memcpy(dir, g_mtNet.dir, 16);
    std::memcpy(unused, g_mtNet.unused, 0x10);
    *bits = g_mtNet.bits;
}
// (The velocity and the unused bytes not logged: the original's player sends stack words there, the port zeros, its fix.)
void LogMtControlState(const float *pos, const float *dir, uint32_t bits)
{
    Log(nullptr, "WriteControlState", {pos[0], pos[1], pos[2], pos[3], dir[0], dir[1], dir[2], dir[3], double(bits)});
}
bool __fastcall WriteControlState(C_NetMessage *, const float *pos, const float *, const float *dir, const void *, uint32_t bits)
{
    LogMtControlState(pos, dir, bits);
    return true;
}
bool __fastcall ReadControlState(C_NetMessage *, float *pos, float *velocity, float *dir, void *unused, uint32_t *bits)
{
    ReadMtNetState(pos, velocity, dir, unused, bits);
    return true;
}
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
int __fastcall OM_AnimClass(int group)   // (the group in ECX)
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
int __fastcall OM_WriteControlState(void *, const float *pos, const float *, const float *dir, const void *, uint32_t bits)
{
    LogMtControlState(pos, dir, bits);
    return 1;
}
int __fastcall OM_ReadControlState(void *, float *pos, float *velocity, float *dir, void *unused, uint32_t *bits)
{
    ReadMtNetState(pos, velocity, dir, unused, bits);
    return 1;
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
// (The test's arrays have their own copies on the original's side: two controllers may hold them; the port's statics
// are the original's globals.)
alignas(16) float g_mtOrigTestActionA[0x40], g_mtOrigTestActionB[0x40];

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
            if (p == g_mtTestActionA || p == g_mtTestActionB)
                o = p == g_mtTestActionA ? g_mtOrigTestActionA : g_mtOrigTestActionB;
            else
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
    const size_t cap = v[0] ? (v[2] - v[0]) / 4 : 0;
    const size_t portCap = pc->m_pathBegin ? size_t(pc->m_pathCap - pc->m_pathBegin) : 0;
    if (cap != portCap)
        return "path capacity: " + std::to_string(portCap) + " / " + std::to_string(cap);
    // (Each waypoint but the two w words: the original makes them from the checkpoint's three floats, the w a stale
    // stack word; the port's 1, its documented fix.)
    for (size_t k = 0; k < count; ++k) {
        const auto *o = reinterpret_cast<const uint32_t *>(reinterpret_cast<uint8_t *const *>(static_cast<uintptr_t>(v[0]))[k]);
        const auto *q = reinterpret_cast<const uint32_t *>(pc->m_pathBegin[k]);
        for (size_t w = 0; w < sizeof(S_AIWaypoint) / 4; ++w)
            if (w != 3 && w != 7 && w != 11 && (o[w] ^ q[w]) & (w == 9 ? 0x00FFFFFFu : 0xFFFFFFFFu))   // (+0x27, +0x2C: padding)
                return "path waypoint " + std::to_string(k) + " word " + std::to_string(w);
    }
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
        const int n = I(1, 5), spare = I(0, 2);
        c->m_pathBegin = static_cast<S_AIWaypoint **>(::operator new(sizeof(void *) * (n + spare)));
        c->m_pathEnd = c->m_pathBegin;
        for (int k = 0; k < n; ++k)
            *c->m_pathEnd++ = AllocMeleeTrackerWaypoint(I(0, 300), MtPos(), uint16_t(I(0, 0xFFFF)), uint8_t(I(0, 20)), uint8_t(I(0, 64)));
        c->m_pathCap = c->m_pathEnd + spare;
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

// ---- part 37b: the update and its stages (the sightings culled, the target decided and found, the move decided, the
// path found, smoothed and followed, the heel and reach checks), the attack and move orders, the sight test
//
// The tracker's lists are each side's: the humans seen (the original's copies mapped) and the sightings (a copy per
// side, the source mapped; the original puts the ones it culls into its pool, the port's release is silent: the lists
// are compared). The original's clock is QueryPerformanceCounter / Frequency through its import slots (0x0080f1c8 /
// 0x0080f1c4): the counter is the test's microseconds at a frequency of 1e6, the port's MicrosecondsNow the same.
// FindMeleeTrackerPath hands each side the test's checkpoints (the original's an MSVC6 vector: its new).
int MT OM_FindMeleeTrackerPath(void *self, int from, int to, uint32_t *vec)
{
    Log(self, "FindMeleeTrackerPath", {double(from), double(to)});
    if (!g_mt.path.empty()) {
        auto *data = static_cast<uint16_t *>(std::malloc(g_mt.path.size() * 2));
        std::memcpy(data, g_mt.path.data(), g_mt.path.size() * 2);
        vec[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        vec[2] = vec[3] = vec[1] + 2 * static_cast<uint32_t>(g_mt.path.size());
    }
    return g_mt.pathFound;
}
int __stdcall OM_QueryFrequency(int64_t *frequency)
{
    *frequency = 1000000;
    return 1;
}
int __stdcall OM_QueryCounter(int64_t *counter)
{
    *counter = static_cast<int64_t>(g_mt.now);
    return 1;
}

// The lists of each side.
constexpr int MT_LIST = 6;
C_Actor *g_mtSeenPort[MT_LIST];
void *g_mtSeenOrig[MT_LIST];
S_MeleeTrackerSighting g_mtSightingsPort[MT_LIST], g_mtSightingsOrig[MT_LIST];
S_MeleeTrackerSighting *g_mtSightPort[MT_LIST];
void *g_mtSightOrig[MT_LIST];
int g_mtSeenCount = 0, g_mtSightCount = 0;

void MtListsToOrig()
{
    C_MeleeTracker *t = PortMt();
    const size_t seen = t->m_seenBegin ? size_t(t->m_seenEnd - t->m_seenBegin) : 0;
    for (size_t k = 0; k < seen; ++k)
        g_mtSeenOrig[k] = t->m_seenBegin[k] ? ToOrig(t->m_seenBegin[k]) : nullptr;
    const size_t sight = t->m_sightingsBegin ? size_t(t->m_sightingsEnd - t->m_sightingsBegin) : 0;
    for (size_t k = 0; k < sight; ++k) {
        const size_t index = size_t(t->m_sightingsBegin[k] - g_mtSightingsPort);
        g_mtSightingsOrig[index] = *t->m_sightingsBegin[k];
        void *source = g_mtSightingsOrig[index].source ? ToOrig(g_mtSightingsOrig[index].source) : nullptr;
        std::memcpy(&g_mtSightingsOrig[index].source, &source, 4);
        g_mtSightOrig[k] = &g_mtSightingsOrig[index];
    }
    uint32_t v[3] = {0, 0, 0};
    if (t->m_seenBegin) {
        v[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_mtSeenOrig));
        v[1] = v[0] + 4 * static_cast<uint32_t>(seen);
        v[2] = v[0] + 4 * MT_LIST;
    }
    std::memcpy(g_mtOrigBytes + 0x574, v, 12);
    v[0] = v[1] = v[2] = 0;
    if (t->m_sightingsBegin) {
        v[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_mtSightOrig));
        v[1] = v[0] + 4 * static_cast<uint32_t>(sight);
        v[2] = v[0] + 4 * MT_LIST;
    }
    std::memcpy(g_mtOrigBytes + 0x564, v, 12);
    std::memcpy(g_mtOrigBytes + 0x580, &t->m_velocity, 16);
    std::memcpy(g_mtOrigBytes + 0x590, &t->m_lastPos, 16);
    std::memcpy(g_mtOrigBytes + 0x5A0, &t->m_lastFrameTime, 4);
    std::memcpy(g_mtOrigBytes + 0x544, &t->m_bodyFrame, 4);
    std::memcpy(g_mtOrigBytes + 0x90 + 0x364, PortMtMovement() + 0x364, 4);
    void *ai = t->m_ai ? ToOrig(t->m_ai) : nullptr;
    std::memcpy(g_mtOrigBytes + 0x534, &ai, 4);
}
// After a call: the lists alike (the sightings by which one, in order).
std::string MtListsDiffer()
{
    C_MeleeTracker *t = PortMt();
    uint32_t v[3];
    std::memcpy(v, g_mtOrigBytes + 0x564, 12);
    const size_t sight = t->m_sightingsBegin ? size_t(t->m_sightingsEnd - t->m_sightingsBegin) : 0;
    const size_t origSight = v[0] ? (v[1] - v[0]) / 4 : 0;
    if (sight != origSight)
        return "sightings: " + std::to_string(sight) + " / " + std::to_string(origSight);
    for (size_t k = 0; k < sight; ++k) {
        const auto *o = reinterpret_cast<const S_MeleeTrackerSighting *const *>(static_cast<uintptr_t>(v[0]))[k];
        const S_MeleeTrackerSighting *p = t->m_sightingsBegin[k];
        if (int(p - g_mtSightingsPort) != int(o - g_mtSightingsOrig))
            return "sighting " + std::to_string(k);
    }
    return "";
}

void SetUpMeleeTrackerB()
{
    RedirectOriginal(0x00522470, FAKE(OM_FindMeleeTrackerPath));
    *OriginalAt<void **>(0x0080f1c4) = reinterpret_cast<void *>(OM_QueryFrequency);
    *OriginalAt<void **>(0x0080f1c8) = reinterpret_cast<void *>(OM_QueryCounter);
    PortMt()->m_bodyFrame = g_asMeleeBody;
    for (int k = 0; k < MT_LIST; ++k)
        g_names[&g_mtSightingsPort[k]] = g_names[&g_mtSightingsOrig[k]] = "sighting" + std::to_string(k);
}

// A frame carrying a human (Attack's and SetOwner's): the frames are shared, so its data names each side's human during
// that side's call.
S_FrameData g_mtHumanFrameData;
C_Human *g_mtFrameHuman = nullptr;
I3D_frame *MtHumanFrame(C_Human *human)
{
    g_mtFrameHuman = human;
    g_mtHumanFrameData = S_FrameData();
    g_mtHumanFrameData.actor = human;
    g_asThirdFrame->SetData(&g_mtHumanFrameData);
    return g_asThirdFrame;
}
void MtFrameDataFor(bool orig)
{
    if (g_mtFrameHuman)
        g_mtHumanFrameData.actor = orig ? static_cast<C_Actor *>(ToOrig(g_mtFrameHuman)) : g_mtFrameHuman;
}

// The fight's state: the humans' velocities and health, the tracker's motion, its lists, the checkpoints and the answers.
void RandomMtB()
{
    RandomMtAll();
    for (S_HumanAnswers &a : g_asAnswers) {
        a.body = true;
        a.stance = I(0, 3);
        static const int classes[] = {-1, 0, 1, 2, 3, 5};
        a.speedClass = classes[I(0, 5)];
    }
    C_Human *const humans[3] = {g_h.human, PortOther(), PortThird()};
    uint8_t *const origs[3] = {nullptr, g_origOther, g_origThird};
    for (int k = 0; k < 3; ++k) {
        humans[k]->m_velocity = P(0.4f) ? S_vector4{0, 0, 0, 0} : S_vector4{U(-5, 5), U(-1, 1), U(-5, 5), 0};
        humans[k]->m_stats[STAT_HEALTH] = P(0.15f) ? I(-5, 0) : I(1, 800);
        if (origs[k]) {
            std::memcpy(origs[k] + 0x1B0, &humans[k]->m_velocity, 16);
            std::memcpy(origs[k] + 0xEC, &humans[k]->m_stats[STAT_HEALTH], 4);
        }
    }
    // The targets now and then close to the tracker (bites, lunges).
    const S_vector4 &own = PortMtActor()->m_position;
    for (int k = 0; k < 3; ++k)
        if (P(0.4f)) {
            humans[k]->m_position = {own.x + U(-5, 5), own.y + U(-0.5f, 0.5f), own.z + U(-5, 5), 1.0f};
            if (origs[k])
                std::memcpy(origs[k] + 0x30, &humans[k]->m_position, 16);
        }
    C_MeleeTracker *t = PortMt();
    t->m_velocity = P(0.3f) ? S_vector4{0, 0, 0, 0} : S_vector4{U(-4, 4), U(-0.5f, 0.5f), U(-4, 4), 0};
    t->m_lastPos = {own.x + U(-1, 1), own.y, own.z + U(-1, 1), 1.0f};
    t->m_lastFrameTime = I(0, 200);
    const int animGroup = I(0, 0x40);
    std::memcpy(PortMtMovement() + 0x364, &animGroup, 4);
    t->m_ai = P(0.2f) ? nullptr : static_cast<C_MeleeTrackerController *>(g_pMtSrc);
    // The tracker turned (its body frame too: the sight test's eye), now and then tilted; its direction with a height
    // now and then.
    S_quat turn;
    turn.Make(P(0.2f) ? S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized() : S_vector(0, 1, 0), U(0, 6.2831f));
    g_asMeleeFrame->SetRot(turn);
    UpdateTree(g_asMeleeFrame);
    {
        const float yaw = U(0, 6.2831f);
        PortMtActor()->m_direction = {std::sin(yaw), P(0.2f) ? U(-0.3f, 0.3f) : 0.0f, std::cos(yaw), 0};
    }
    // The humans near: the owner's spot about the tracker; targets in front of the eye within its sight range.
    const S_matrix &eye = g_asMeleeBody->GetMatrix();
    S_vector look(eye.m[2][0], 0, eye.m[2][2]);
    look.Normalize();
    const S_vector side(look.z, 0, -look.x);
    const float range = float(t->GetStats().sightRange);
    for (int k = 0; k < 3; ++k) {
        const float r = U(0, 1);
        S_vector4 pos = humans[k]->m_position;
        if (r < 0.3f) {
            pos = {own.x + U(-4, 4), own.y + U(-0.3f, 0.3f), own.z + U(-4, 4), 1.0f};
        } else if (r < 0.6f) {
            const float ahead = U(0, 1.3f) * range;
            const S_vector at = S_vector(eye.m[3][0], eye.m[3][1], eye.m[3][2]) + look * ahead + side * (U(-0.9f, 0.9f) * ahead * 0.84f);
            pos = {at.x, own.y, at.z, 1.0f};
        }
        humans[k]->m_position = pos;
        if (origs[k])
            std::memcpy(origs[k] + 0x30, &humans[k]->m_position, 16);
    }
    // (The copied humans' frames where they are: the sight test reads them.)
    for (int k = 1; k < 3; ++k) {
        I3D_frame *f = humans[k]->GetFrame();
        f->SetPosInline(S_vector(humans[k]->m_position.x, humans[k]->m_position.y, humans[k]->m_position.z));
        UpdateTree(f);
    }
    // The lists.
    g_mt.now = uint64_t(I(0, 1000000)) * 100 + 10000000;
    g_mtSeenCount = P(0.3f) ? 0 : I(1, 3);
    // (Humans only, and loud sightings from humans: the original scores them as humans unchecked, the port skips the
    // others, its fix.)
    for (int k = 0; k < g_mtSeenCount; ++k)
        g_mtSeenPort[k] = humans[I(0, 2)];
    g_mtSightCount = P(0.3f) ? 0 : I(1, 4);
    for (int k = 0; k < g_mtSightCount; ++k) {
        S_MeleeTrackerSighting &s = g_mtSightingsPort[k];
        s.pos = MtPos();
        s.type = I(0, 3);
        s.loudness = I(0, 30);
        s.source = s.loudness <= 19 && P(0.3f) ? (P(0.5f) ? nullptr : static_cast<C_Actor *>(PortActor(I(0, ACTOR_KIND_COUNT - 1))))
                                               : static_cast<C_Actor *>(humans[I(0, 2)]);
        const uint32_t nowLow = uint32_t(g_mt.now);
        s.time = P(0.1f) ? nowLow + 2500000u : P(0.4f) ? nowLow + 2500000u + uint32_t(I(1, 2000000)) : nowLow + uint32_t(I(-2000000, 2500000));
        g_mtSightPort[k] = &s;
    }
    t->m_seenBegin = P(0.1f) ? nullptr : g_mtSeenPort;
    t->m_seenEnd = t->m_seenBegin ? g_mtSeenPort + g_mtSeenCount : nullptr;
    t->m_seenCap = t->m_seenBegin ? g_mtSeenPort + MT_LIST : nullptr;
    t->m_sightingsBegin = P(0.1f) ? nullptr : g_mtSightPort;
    t->m_sightingsEnd = t->m_sightingsBegin ? g_mtSightPort + g_mtSightCount : nullptr;
    t->m_sightingsCap = t->m_sightingsBegin ? g_mtSightPort + MT_LIST : nullptr;
    // The checkpoints near the tracker, the path search's answer, the nearest point's.
    for (auto &bytes : g_checkpointBytes) {
        auto *point = reinterpret_cast<S_Checkpoint *>(bytes);
        point->pos = S_vector(own.x + U(-15, 15), own.y + U(-0.5f, 0.5f), own.z + U(-15, 15));
        point->radius = uint8_t(I(0, 64));
        point->flags = uint16_t(I(0, 0xFFFF));
        point->type = uint8_t(I(0, 12));
    }
    // (A checkpoint right by the tracker now and then: the path's first point dropped.)
    int nearPoint = -1;
    if (P(0.3f)) {
        nearPoint = I(0, 7);
        auto *point = reinterpret_cast<S_Checkpoint *>(g_checkpointBytes[nearPoint]);
        point->pos = S_vector(own.x + U(-0.8f, 0.8f), own.y, own.z + U(-0.8f, 0.8f));
        point->radius = uint8_t(I(8, 40));
    }
    g_mt.path.clear();
    if (nearPoint >= 0 && P(0.7f))
        g_mt.path.push_back(uint16_t(nearPoint));
    // (Never the same checkpoint twice in a row, as a search's path: a repeated point makes the smoothing divide 0 by 0.)
    for (int k = P(0.15f) ? 0 : I(1, 5); k > 0; --k) {
        uint16_t cp = uint16_t(I(0, 7));
        if (!g_mt.path.empty() && cp == g_mt.path.back())
            cp = uint16_t((cp + 1) % 8);
        g_mt.path.push_back(cp);
    }
    g_mt.pathFound = P(0.85f);
    // (A search that succeeds gives at least the start: from == to is one point.)
    if (g_mt.pathFound && g_mt.path.empty())
        g_mt.path.push_back(uint16_t(I(0, 7)));
    g_mt.route = P(0.85f);
    g_cs.nearest = P(0.1f) ? -1 : I(0, 7);
    g_mt.animClass = I(0, 2);
    // The controllers' waypoints about the tracker.
    for (S_AIWaypoint **w = g_pMt->m_pathBegin; w && w != g_pMt->m_pathEnd; ++w)
        (*w)->pos = (*w)->pos2 = {own.x + U(-3, 3), own.y, own.z + U(-3, 3), 1.0f};
    // The orders that steer the stages; following (0x10) exactly when there is an owner, as SetOwnerActor keeps them.
    if (P(0.5f))
        g_pMt->m_flags = (g_pMt->m_flags & ~uint32_t(0x800 | MELEE_TRACKER_ORDER_SUSPEND)) | (P(0.5f) ? 0x20000u : 0u) |
                         (P(0.5f) ? 0x4u : 0u);
    g_pMt->m_flags = g_pMt->m_owner && P(0.9f) ? g_pMt->m_flags | 0x10u : g_pMt->m_flags & ~0x10u;
}

// A call with the lists each side's (converted before, compared after).
template <class P, class O> void MtCallB(const char *what, P port, O original)
{
    MtListsToOrig();
    MtCall(what, port, original);
    const std::string why = MtListsDiffer();
    CHECK(why.empty());
    static std::map<std::string, int> shown;
    if (!why.empty() && shown[what]++ < 3)
        std::printf("  %s: %s\n", what, why.c_str());
}

void TestMtB()
{
    std::printf("melee tracker ai: the update, targets, paths\n");
    C_Human *const humans[3] = {g_h.human, PortOther(), PortThird()};
    for (int i = 0; i < 30000 / QUICK; ++i) {
        RandomMtB();
        C_MeleeTrackerAI *c = g_pMt;
        const bool scripted = g_mtSize == 0xD0;
        const S_vector4 own = PortMtActor()->m_position;
        auto randomPoint = [&](float *p) {
            const S_vector4 v = P(0.5f) ? S_vector4{own.x + U(-4, 4), own.y, own.z + U(-4, 4), 1.0f} : MtPos();
            p[0] = v.x, p[1] = v.y, p[2] = v.z, p[3] = v.w;
        };
        int which = I(0, 23);   // (17 .. 20: the sight test; 21 .. 23: FollowPath again)
        if (which > 20)
            which = 4;
        switch (which) {
        case 0: {
            if (scripted)
                g_pMtS->m_barkTimer = -1;   // (the idle bark: part 37d)
            const int dt = I(0, 200);
            if (g_mtPortActionA && P(0.5f))
                for (int k = 0; k < 0x40; ++k) {
                    g_mtPortActionA[k] = P(0.5f) ? 1.0f : 0.0f;
                    g_mtPortActionB[k] = P(0.5f) ? 1.0f : 0.0f;
                }
            MtCallB("Tick",
                    [&] {
                        c->Tick(dt);
                        if (c->m_actionA != g_mtTestActionA && c->m_actionA) {
                            g_mtPortActionA = c->m_actionA;
                            g_mtPortActionB = c->m_actionB;
                        }
                        return 0;
                    },
                    [&](void *) { OMtV<void>(0x18, dt); return 0; });
            break;
        }
        case 1:
            MtCallB("CullSightings", [&] { c->CullSightings(0); return 0; }, [&](void *) { OMt<void>(0x005cdff0, 0); return 0; });
            break;
        case 2: {
            if (!c->m_actionB)
                c->m_actionB = g_mtTestActionB;
            const int dt = I(0, 2000);
            if (P(0.3f))
                c->m_0b4 = P(0.5f) ? (P(0.5f) ? 1200 : 1201) - dt : P(0.5f) ? 100000 : 99999 - I(0, 5);
            if (P(0.5f)) {
                C_Human *target = humans[I(0, 2)];
                c->m_target = target;
                c->m_flags |= 0x20000u;
                target->m_stats[STAT_HEALTH] = I(1, 800);
                target->m_vehicle = nullptr;
                const float yaw = U(0, 6.2831f), d = U(0, 4.5f);
                target->m_position = {own.x + std::sin(yaw) * d, own.y, own.z + std::cos(yaw) * d, 1.0f};
                if (P(0.5f))
                    target->m_velocity = {0, 0, 0, 0};
                uint8_t *const origs[3] = {nullptr, g_origOther, g_origThird};
                const int k = target == humans[0] ? 0 : target == humans[1] ? 1 : 2;
                if (origs[k]) {
                    std::memcpy(origs[k] + 0x30, &target->m_position, 16);
                    std::memcpy(origs[k] + 0x1B0, &target->m_velocity, 16);
                    std::memcpy(origs[k] + 0xEC, &target->m_stats[STAT_HEALTH], 4);
                    std::memset(origs[k] + 0x160, 0, 4);
                }
            }
            MtCallB("DecideTarget", [&] { c->DecideTarget(dt); return 0; }, [&](void *) { OMt<void>(0x005cd1f0, dt); return 0; });
            break;
        }
        case 3:
            if (P(0.6f)) {
                C_Human *owner = humans[I(0, 2)];
                c->m_owner = owner;
                c->m_flags = (c->m_flags & ~0x20000u) | (P(0.9f) ? 0x10u : 0u);
                float spot[4] = {};
                c->OwnerPosition(spot);
                const S_vector want(own.x + U(-2.5f, 2.5f), own.y, own.z + U(-2.5f, 2.5f));
                owner->m_position.x += want.x - spot[0];
                owner->m_position.z += want.z - spot[2];
                const int k = owner == humans[0] ? 0 : owner == humans[1] ? 1 : 2;
                uint8_t *const origs[3] = {nullptr, g_origOther, g_origThird};
                if (origs[k])
                    std::memcpy(origs[k] + 0x30, &owner->m_position, 16);
                if (P(0.6f))
                    ClearPortPath(c);
            }
            MtCallB("DecideMove", [&] { c->DecideMove(0); return 0; }, [&](void *) { OMt<void>(0x005cd890, 0); return 0; });
            break;
        case 4:
            if (c->m_pathBegin && c->m_pathEnd != c->m_pathBegin && P(0.8f)) {
                const S_vector &at = g_asMeleeFrame->GetWorldPos();
                S_AIWaypoint *next = c->m_pathBegin[0];
                next->pos = {at.x + U(-1.2f, 1.2f), at.y, at.z + U(-1.2f, 1.2f), 1.0f};
                C_MeleeTracker *t = PortMt();
                t->m_lastPos = {at.x + U(-0.5f, 0.5f), at.y, at.z + U(-0.5f, 0.5f), 1.0f};
                t->m_velocity = {(next->pos.x - t->m_lastPos.x) * U(0, 4), 0, (next->pos.z - t->m_lastPos.z) * U(0, 4), 0};
                if (P(0.8f)) {
                    // (The next waypoint 0.3 .. 1 m ahead; the motion passing it 0 .. 0.6 m to the side.)
                    const float yaw = U(0, 6.2831f);
                    const S_vector dir(std::sin(yaw), 0, std::cos(yaw)), perp(dir.z, 0, -dir.x);
                    next->pos = {at.x + dir.x * U(0.3f, 1.0f), at.y, at.z + dir.z * U(0.3f, 1.0f), 1.0f};
                    t->m_lastFrameTime = I(1, 200);
                    const float speed = U(1, 6), seconds = float(t->m_lastFrameTime) * 0.001f;
                    t->m_velocity = {dir.x * speed, 0, dir.z * speed, 0};
                    const S_vector back = dir * (speed * seconds * U(0, 1.5f));
                    const S_vector offset = perp * U(-0.6f, 0.6f);
                    t->m_lastPos = {next->pos.x - back.x + offset.x, at.y, next->pos.z - back.z + offset.z, 1.0f};
                }
                c->m_flags = (c->m_flags & ~0x10000u) | 0x4u;
                if (P(0.6f))
                    c->m_flags &= ~0x20010u;
            }
            MtCallB("FollowPath", [&] { c->FollowPath(0); return 0; }, [&](void *) { OMt<void>(0x005cbf00, 0); return 0; });
            break;
        case 5:
            if (P(0.4f)) {
                // (The last leg of a scripted MoveTo: one waypoint near, moving, the script's order.)
                PickMtPair(true);
                c = g_pMt;
                RandomMtController(c, false);
                g_pMtS->m_script = reinterpret_cast<C_Script *>(g_mtScriptBytes);
                c->m_pathBegin = static_cast<S_AIWaypoint **>(::operator new(sizeof(void *)));
                c->m_pathEnd = c->m_pathCap = c->m_pathBegin + 1;
                const S_vector &at = g_asMeleeFrame->GetWorldPos();
                const S_vector4 wp = {at.x + U(-3, 3), at.y, at.z + U(-3, 3), 1.0f};
                c->m_pathBegin[0] = AllocMeleeTrackerWaypoint(I(0, 7), wp, 0, 0, uint8_t(I(0, 40)));
                c->m_pathBegin[0]->pos2 = {at.x + U(-2, 2), at.y, at.z + U(-2, 2), 1.0f};
                c->m_flags = (c->m_flags & ~0x20010u) | (P(0.7f) ? MELEE_TRACKER_ORDER_MOVETO : 0u) | MELEE_TRACKER_ORDER_ACTIVE |
                             (P(0.5f) ? 0x4u : 0x8u);
                c->m_owner = nullptr;
            }
            MtCallB("AdvanceWaypoint", [&] { return int(c->AdvanceWaypoint()); }, [&](void *) { return int(OMt<uint32_t>(0x005cc420) & 0xFF); });
            break;
        case 6:
            // (Always with an owner and following, as AdvanceWaypoint calls it: without one the original reads its spot
            // unset.)
            if (!c->m_owner)
                c->m_owner = humans[I(0, 2)];
            c->m_flags |= 0x10u;
            if (P(0.5f)) {
                float spot[4] = {};
                c->OwnerPosition(spot);
                c->m_0a0 = {spot[0] + U(-0.4f, 0.4f), spot[1] + U(-0.3f, 0.3f), spot[2] + U(-0.4f, 0.4f), spot[3]};
            }
            MtCallB("CheckHeel", [&] { return int(c->CheckHeel(1)); }, [&](void *) { return int(OMt<uint32_t>(0x005ccec0, 1) & 0xFF); });
            break;
        case 7: {
            float point[4];
            randomPoint(point);
            if (P(0.1f))
                point[0] = point[1] = point[2] = 0.0f;
            MtCallB("FacePoint", [&] { c->FacePoint(1, point); return 0; }, [&](void *) { OMt<void>(0x005cb480, 1, point); return 0; });
            break;
        }
        case 8: {
            const bool force = P(0.5f);
            MtCallB("FindTarget", [&] { c->FindTarget(force); return 0; }, [&](void *) { OMt<void>(0x005cc8d0, int(force)); return 0; });
            break;
        }
        case 9: {
            // (The original reads any candidate as a human, a missing one too: the port skips them. Humans here.)
            C_Actor *candidate = humans[I(0, 2)];
            void *oCandidate = ToOrig(candidate);
            const float start = P(0.3f) ? -10000.0f : U(999000, 1000001);
            float pScore = start, oScore = start;
            C_Actor *pBest = nullptr;
            void *oBest = nullptr;
            MtCallB("ScoreTarget",
                    [&] {
                        c->ScoreTarget(candidate, &pScore, &pBest);
                        return FloatBitsOf(pScore) ^ int(pBest ? reinterpret_cast<uintptr_t>(ToOrig(pBest)) : 0);
                    },
                    [&](void *) {
                        OMt<void>(0x005cc7a0, oCandidate, &oScore, &oBest);
                        return FloatBitsOf(oScore) ^ int(reinterpret_cast<uintptr_t>(oBest));
                    });
            break;
        }
        case 10:
            if (c->m_target && P(0.5f)) {
                const S_vector4 &theirs = c->m_target->GetPosition4();
                c->m_090 = {theirs.x + U(-1, 1), theirs.y + U(-0.3f, 0.3f), theirs.z + U(-1, 1), theirs.w};
            }
            MtCallB("CheckReached", [&] { return int(c->CheckReached(1)); }, [&](void *) { return int(OMt<uint32_t>(0x005cccc0, 1) & 0xFF); });
            break;
        case 11:
            MtCallB("BeginAttack", [&] { c->BeginAttack(); return 0; }, [&](void *) { OMt<void>(0x005cd0a0); return 0; });
            break;
        case 12: {
            I3D_frame *const frames[4] = {nullptr, g_h.frame, g_targetFrame, g_otherFrame};
            I3D_frame *frame = P(0.4f) ? MtHumanFrame(humans[I(0, 2)]) : frames[I(0, 3)];
            MtCallB("Attack", [&] { MtFrameDataFor(false); c->Attack(frame); return 0; },
                    [&](void *) { MtFrameDataFor(true); OMt<void>(0x005cf7c0, frame); MtFrameDataFor(false); return 0; });
            g_mtFrameHuman = nullptr;
            break;
        }
        case 13: {
            // (The scripted controller with its script: the original advances whatever +0xC0 holds, a missing script
            // or past the automatic variant; the port's fix checks.)
            PickMtPair(true);
            c = g_pMt;
            RandomMtController(c, true);
            g_pMtS->m_script = reinterpret_cast<C_Script *>(g_mtScriptBytes);
            const uint16_t checkpoint = uint16_t(P(0.1f) ? 0xFFFF : I(0, 7));
            MtCallB("MoveTo", [&] { c->MoveTo(checkpoint); return 0; }, [&](void *) { OMt<void>(0x005cf700, int(checkpoint)); return 0; });
            break;
        }
        case 14: {
            C_Actor *owner = P(0.2f) ? nullptr : static_cast<C_Actor *>(humans[I(0, 2)]);
            void *oOwner = owner ? ToOrig(owner) : nullptr;
            if (P(0.3f)) {
                // (Through SetOwner, the script command: the owner's frame.)
                I3D_frame *frame = MtHumanFrame(humans[I(0, 2)]);
                MtCallB("SetOwner", [&] { MtFrameDataFor(false); c->SetOwner(frame); return 0; },
                        [&](void *) { MtFrameDataFor(true); OMt<void>(0x005cf9b0, frame); MtFrameDataFor(false); return 0; });
                g_mtFrameHuman = nullptr;
            } else {
                MtCallB("SetOwnerActor", [&] { c->SetOwnerActor(owner); return 0; }, [&](void *) { OMtV<void>(0x30, oOwner); return 0; });
            }
            break;
        }
        case 15: {
            float from[4], to[4];
            randomPoint(from);
            randomPoint(to);
            MtCallB("FindPath", [&] { return int(c->FindPath(from, to)); }, [&](void *) { return int(OMt<uint32_t>(0x005cb500, from, to) & 0xFF); });
            break;
        }
        case 16:
            if (P(0.5f))
                MtCallB("ClearPath", [&] { c->ClearPath(); return 0; }, [&](void *) { OMt<void>(0x005cbe60); return 0; });
            else
                MtCallB("ResetPath", [&] { return int(c->ResetPath()); }, [&](void *) { return int(OMt<uint32_t>(0x005cb410) & 0xFF); });
            break;
        default: {
            // The sight test: a human, a part 23 actor, the tracker itself (with its controller: the original assumes one).
            C_Actor *target = P(0.6f) ? static_cast<C_Actor *>(humans[I(0, 2)]) : P(0.5f) ? static_cast<C_Actor *>(PortActor(I(0, ACTOR_KIND_COUNT - 1))) : PortMtActor();
            if (target == PortMtActor() && !PortMt()->m_ai)
                PortMt()->m_ai = g_pMtSrc;
            void *oTarget = ToOrig(target);
            int ps = 7, os = 7;
            float pv = 9, ov = 9;
            MtCallB("TestSight",
                    [&] {
                        c->TestSight(target, &ps, &pv);
                        return ps * 31 ^ FloatBitsOf(pv);
                    },
                    [&](void *) {
                        OMtV<void>(0x1C, oTarget, &os, &ov);
                        return os * 31 ^ FloatBitsOf(ov);
                    });
            break;
        }
        }
    }
}

// ---- part 37c: the scripted tracker's idle bark, the saved games, the remote and the player's controllers, the
// destructors and the full AI's own slots.
//
// The remote (vtable 0x008121e0, 0x270 bytes) and the player's control (0x00812340, 0x4B0 bytes): each side's copy the
// same bytes (the tracker and the owner mapped; the weights' pointers into the copy's own buffers rebased, the test
// arrays the original's copies). The idle bark's sound comes from the tracker frame's "d" child (the mouth); _rand is
// the same rand() on both sides (part 33's), seeded alike. The saved games: each side's into a file (the port's C_chunk
// on both sides), the files compared; the loads from those and from hand-made files; the actors by id the humans and
// part 34's actors (GetActor: each side's own).
C_MeleeTrackerRemote *g_pMtR;
C_MeleeTrackerPlayerControl *g_pMtP;
uint8_t *g_oMtR, *g_oMtP;
C_MeleeTrackerController *g_pCtl;                       // the pair under test
uint8_t *g_oCtl;
size_t g_ctlSize;
I3D_frame *g_mtMouth;

void CtlToOrig(const C_MeleeTrackerController *pc, uint8_t *oc, size_t size)
{
    const auto *pb = reinterpret_cast<const uint8_t *>(pc);
    std::memcpy(oc + 4, pb + 4, size - 4);
    for (size_t at : {size_t(0x10), size_t(0x18)}) {
        void *p;
        std::memcpy(&p, pb + at, 4);
        p = p ? ToOrig(p) : nullptr;
        std::memcpy(oc + at, &p, 4);
    }
    for (int k = 0; k < 2; ++k) {
        float *p = k ? pc->m_actionB : pc->m_actionA;
        const auto *pp = reinterpret_cast<const uint8_t *>(p);
        uint8_t *o = nullptr;
        if (pp >= pb && pp < pb + size) {
            o = oc + (pp - pb);
        } else if (p) {
            float *copy = p == g_mtTestActionA ? g_mtOrigTestActionA : g_mtOrigTestActionB;
            std::memcpy(copy, p, 0x40 * 4);
            o = reinterpret_cast<uint8_t *>(copy);
        }
        std::memcpy(oc + (k ? 0x64 : 0x60), &o, 4);
    }
}
std::string CtlDiffers(const C_MeleeTrackerController *pc, const uint8_t *oc, size_t size)
{
    const auto *pb = reinterpret_cast<const uint8_t *>(pc);
    for (size_t at = 4; at < size; at += 4) {
        uint32_t a, b;
        std::memcpy(&a, pb + at, 4);
        std::memcpy(&b, oc + at, 4);
        if (at == 0x10 || at == 0x18) {
            void *p = reinterpret_cast<void *>(static_cast<uintptr_t>(a));
            a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p ? ToOrig(p) : nullptr));
        } else if (at == 0x60 || at == 0x64) {
            const auto *p = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(a));
            const auto *o = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(b));
            const bool pOwn = p >= pb && p < pb + size, oOwn = o >= oc && o < oc + size;
            if (pOwn || oOwn) {
                if (!pOwn || !oOwn || p - pb != o - oc)
                    return "weights " + std::to_string(at) + " (where)";
                continue;
            }
            if ((p != nullptr) != (o != nullptr))
                return "weights " + std::to_string(at) + " (null on one side)";
            if (p && std::memcmp(p, o, 0x40 * 4) != 0)
                return "weights " + std::to_string(at);
            continue;
        }
        if (a != b)
            return "controller+" + std::to_string(at);
    }
    return "";
}
// A call on the remote / player pair (the source controller converted too). `staleW`: the w words the original fills
// from stack words (Apply's accumulator; a load's three vectors): the port's.
template <class P, class O> void CtlCall(const char *what, bool staleW, P port, O original)
{
    MtTrackerToOrig();
    MtToOrig(g_pMtSrc, g_oMtSrc, 0xC0);
    CtlToOrig(g_pCtl, g_oCtl, g_ctlSize);
    DiffCall(what, port,
             [&](void *o) {
                 const int r = original(o);
                 const auto *pc = reinterpret_cast<const uint8_t *>(g_pCtl);
                 std::memcpy(g_oCtl + 0x3C, pc + 0x3C, 4);
                 if (staleW) {
                     std::memcpy(g_oCtl + 0x4C, pc + 0x4C, 4);
                     std::memcpy(g_oCtl + 0x5C, pc + 0x5C, 4);
                 }
                 return r;
             });
    std::string why = CtlDiffers(g_pCtl, g_oCtl, g_ctlSize);
    if (why.empty())
        why = MtDiffers(g_pMtSrc, g_oMtSrc, 0xC0);
    if (why.empty())
        why = MtTrackerDiffers();
    CHECK(why.empty());
    static std::map<std::string, int> shown;
    if (!why.empty() && shown[what]++ < 3)
        std::printf("  %s: %s\n", what, why.c_str());
}
template <class R, class... A> R OMtCtl(uint32_t at, A... a)
{
    using F = R(MT *)(void *, A...);
    return OriginalAt<F>(at)(static_cast<void *>(g_oCtl), a...);
}
template <class R, class... A> R OMtCtlV(size_t slot, A... a)
{
    using F = R(MT *)(void *, A...);
    void **vt = *reinterpret_cast<void ***>(g_oCtl);
    return reinterpret_cast<F>(vt[slot / 4])(static_cast<void *>(g_oCtl), a...);
}

// The actors by id (GetActor's answers: the humans, part 34's actors, the tracker).
C_Actor *MtActorById(int id, bool original)
{
    std::vector<C_Actor *> all = {g_h.human, PortOther(), PortThird(), PortMtActor()};
    for (int k = 0; k < ACTOR_KIND_COUNT; ++k)
        all.push_back(static_cast<C_Actor *>(PortActor(k)));
    for (C_Actor *a : all)
        if (a && a->m_netId == id)
            return original ? static_cast<C_Actor *>(ToOrig(a)) : a;
    return nullptr;
}
int MtActorId()
{
    if (P(0.2f))
        return P(0.5f) ? -1 : I(0, 1000);
    const C_Actor *a = P(0.5f) ? static_cast<C_Actor *>(P(0.5f) ? g_h.human : PortOther()) : static_cast<C_Actor *>(PortActor(I(0, ACTOR_KIND_COUNT - 1)));
    return a->m_netId;
}
void *MT OM_GetActorMt(void *self, int index)
{
    Log(self, "GetActor", {double(index)});
    return MtActorById(index, true);
}

void SetUpMeleeTrackerC()
{
    RedirectOriginal(0x005ac5f0, FAKE(OM_WriteControlState));
    RedirectOriginal(0x005ac660, FAKE(OM_ReadControlState));
    RedirectOriginal(0x00475570, FAKE(OS_CreateActorMessageEx));
    RedirectOriginal(0x004755b0, FAKE(OS_SendActorMessage));
    RedirectOriginal(0x00475dd0, FAKE(OM_GetActorMt));
    g_actorByIndex = MtActorById;
    S_quat none;
    none.Identity();
    g_mtMouth = NewFrame(FRAME_DUMMY, "d", S_vector(0, 0.1f, 0.35f), none, Box(S_vector(0.05f, 0.05f, 0.05f)), "", g_asMeleeBody);
    g_pMtR = new C_MeleeTrackerRemote;
    g_pMtP = new C_MeleeTrackerPlayerControl;
    g_oMtR = static_cast<uint8_t *>(OC_AlignedNew(0x270, 16));
    g_oMtP = static_cast<uint8_t *>(OC_AlignedNew(0x4B0, 16));
    const uint32_t vtR = 0x008121e0, vtP = 0x00812340;
    std::memcpy(g_oMtR, &vtR, 4);
    std::memcpy(g_oMtP, &vtP, 4);
    g_names[g_pMtR] = g_names[g_oMtR] = "mtRemote";
    g_names[g_pMtP] = g_names[g_oMtP] = "mtPlayer";
    g_extraPointers.push_back({g_pMtR, g_oMtR});
    g_extraPointers.push_back({g_pMtP, g_oMtP});
}

// A remote's / player's state at random: the root's fields, the buffers, the weights' pointers (none, the copy's own
// buffers, the test arrays).
void RandomMtCtl(bool player)
{
    g_pCtl = player ? static_cast<C_MeleeTrackerController *>(g_pMtP) : static_cast<C_MeleeTrackerController *>(g_pMtR);
    g_oCtl = player ? g_oMtP : g_oMtR;
    g_ctlSize = player ? 0x4B0 : 0x270;
    C_MeleeTrackerController *c = g_pCtl;
    C_Human *const humans[3] = {g_h.human, PortOther(), PortThird()};
    c->m_meleeTracker = PortMt();
    c->m_owner = P(0.4f) ? nullptr : P(0.7f) ? static_cast<C_Actor *>(humans[I(0, 2)]) : static_cast<C_Actor *>(PortActor(I(0, ACTOR_KIND_COUNT - 1)));
    c->m_alarm = P(0.5f) ? 0 : I(1, 0x40);
    c->m_flags = (uint32_t(std::rand()) << 17) ^ uint32_t(std::rand());
    c->m_savedPos = MtPos();
    c->m_facing = {MtValue(), MtValue(), MtValue(), MtValue()};
    c->m_050 = MtPos();
    c->m_068 = I(0, 50);
    auto *b = reinterpret_cast<uint8_t *>(c);
    for (size_t at = 0x70; at < g_ctlSize; at += 4) {
        const float v = MtValue();
        std::memcpy(b + at, &v, 4);
    }
    auto pick = [&](size_t own, float *test) {
        const float r = U(0, 1);
        return r < 0.25f ? nullptr : r < 0.65f ? reinterpret_cast<float *>(b + own) : test;
    };
    c->m_actionA = pick(0x70, g_mtTestActionA);
    c->m_actionB = pick(0x170, g_mtTestActionB);
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT};
    g_gameRole = roles[I(0, 2)];
    for (float &v : g_mtNet.pos)
        v = U(-30, 30);
    for (float &v : g_mtNet.velocity)
        v = MtValue();
    for (float &v : g_mtNet.dir)
        v = MtValue();
    for (uint8_t &v : g_mtNet.unused)
        v = uint8_t(I(0, 255));
    g_mtNet.bits = (uint32_t(std::rand()) << 17) ^ uint32_t(std::rand());
    g_mt.camera78 = P(0.15f);
}

// The hand-made chunks: the root's (0x22b8), the full AI's (0x2249), the scripted's bark timer (0x2054); their
// sub-chunks a random subset in a random order, now and then an unknown one; now and then another chunk id.
template <class T> void MtShuffled(std::vector<T> &v)
{
    for (size_t k = v.size(); k > 1; --k)
        std::swap(v[k - 1], v[size_t(I(0, int(k) - 1))]);
    v.resize(size_t(I(0, int(v.size()))));
}
void MtVector3(C_chunk &c, uint16_t id)
{
    const float v[3] = {U(-30, 30), U(-30, 30), U(-30, 30)};
    c.WriteChunk(id, v, 0xc);
}
void HandMadeRootChunk(C_chunk &c)
{
    c.Begin(P(0.1f) ? uint16_t(I(1, 0x2000)) : uint16_t(0x22b8));
    std::vector<int> subs = {0, 1, 2, 3, 4, 5, 6};
    MtShuffled(subs);
    for (int sub : subs) {
        switch (sub) {
        case 0: c.WriteChunk(0, MtActorId()); break;
        case 1: c.WriteChunk(1, I(0, 0x40)); break;
        case 2: c.WriteChunk(2, (uint32_t(std::rand()) << 17) ^ uint32_t(std::rand())); break;
        case 3: case 4: case 5: MtVector3(c, uint16_t(sub)); break;
        default: c.WriteChunk(uint16_t(I(6, 40)), I(0, 1000)); break;
        }
    }
    c.Ascend();
}
void HandMadeAiChunk(C_chunk &c)
{
    c.Begin(P(0.1f) ? uint16_t(I(1, 0x2000)) : uint16_t(0x2249));
    std::vector<int> subs = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    MtShuffled(subs);
    for (int sub : subs) {
        switch (sub) {
        case 0: case 5: case 6: case 8: c.WriteChunk(uint16_t(sub), I(-500, 2000)); break;
        case 1: case 2: c.WriteChunk(uint16_t(sub), MtActorId()); break;
        case 3: case 4: MtVector3(c, uint16_t(sub)); break;
        case 7: c.WriteChunk(7, P(0.5f) ? 0 : I(1, 3)); break;
        case 9: {
            // (Known checkpoints: both sides read the checkpoint unchecked.)
            c.Begin(9);
            for (int k = 0, n = I(0, 6); k < n; ++k)
                c.WriteChunk(uint16_t(k), I(0, 7));
            c.Ascend();
            break;
        }
        default: c.WriteChunk(uint16_t(I(10, 40)), I(0, 1000)); break;
        }
    }
    c.Ascend();
}
void HandMadeBarkChunk(C_chunk &c)
{
    c.Begin(P(0.1f) ? uint16_t(I(1, 0x2000)) : uint16_t(0x2054));
    if (P(0.8f))
        c.WriteChunk(0, I(-1, 30000));
    if (P(0.2f))
        c.WriteChunk(uint16_t(I(1, 9)), I(0, 1000));
    c.Ascend();
}

// One save and load of the pair under test (`ai`: the AI pair, else the remote / player). `kind`: the variant's slots,
// the full AI's own (0x005d25b0 / 0x005d28b0), the root's (0x005d2000 / 0x005d21d0).
void MtSaveLoad(bool ai, int kind)
{
    const std::string portFile = AiTempFile("_mtport.sav"), origFile = AiTempFile("_mtorig.sav");
    std::remove(portFile.c_str());
    std::remove(origFile.c_str());
    C_MeleeTrackerAI *a = g_pMt;
    C_MeleeTrackerController *c = ai ? static_cast<C_MeleeTrackerController *>(g_pMt) : g_pCtl;
    auto portSave = [&](C_chunk &ch) -> bool {
        if (kind == 0)
            return c->SaveGame(ch);
        if (kind == 1)
            return a->C_MeleeTrackerAI::SaveGame(ch);
        return c->C_MeleeTrackerController::SaveGame(ch);
    };
    auto origSave = [&](C_chunk &ch) -> uint32_t {
        if (!ai)
            return OMtCtlV<uint32_t>(0x64, &ch);
        if (kind == 0)
            return OMtV<uint32_t>(0x64, &ch);
        return OMt<uint32_t>(kind == 1 ? 0x005d25b0 : 0x005d2000, &ch);
    };
    // (Now and then into a closed chunk: nothing written.)
    const bool closedSave = P(0.03f);
    auto save = [&](const std::string &file, bool orig) {
        C_chunk ch;
        if (closedSave)
            return orig ? int(origSave(ch) & 0xFF) : int(portSave(ch));
        ch.Open(file.c_str(), true);
        ch.Begin(0x7777);
        const int r = orig ? int(origSave(ch) & 0xFF) : int(portSave(ch));
        ch.Ascend();
        ch.Begin(0x0BD1);
        ch.Ascend();
        return r;
    };
    if (ai)
        MtCallB("SaveGame", [&] { return save(portFile, false); }, [&](void *) { return save(origFile, true); });
    else
        CtlCall("SaveGame", false, [&] { return save(portFile, false); }, [&](void *) { return save(origFile, true); });
    const bool same = AiFileBytes(portFile) == AiFileBytes(origFile);
    CHECK(same);
    static int shown;
    if (!same && shown++ < 3)
        std::printf("  SaveGame (kind %d): the files differ\n", kind);
    // The load: the saved file, or a hand-made one (now and then a closed chunk).
    if (P(0.5f)) {
        C_chunk ch;
        ch.Open(portFile.c_str(), true);
        ch.Begin(0x7777);
        if (kind == 1)
            HandMadeAiChunk(ch);
        else
            HandMadeRootChunk(ch);
        if (kind == 0 && ai && g_mtSize == 0xD0 && P(0.8f))
            HandMadeBarkChunk(ch);
        ch.Ascend();
        ch.Begin(0x0BD1);
        ch.Ascend();
    }
    const bool closed = P(0.03f);
    // A new state to load over.
    if (ai) {
        RandomMtController(g_pMt, true);
        if (g_mtSize == 0xD0)
            g_pMtS->m_barkTimer = P(0.3f) ? -1 : I(0, 25000);
    } else {
        RandomMtCtl(g_pCtl == g_pMtP);
    }
    int nextPort = 0, nextOrig = 0;
    auto load = [&](bool orig) {
        C_chunk ch;
        if (!closed) {
            ch.Open(portFile.c_str(), false);
            uint16_t id;
            ch.Descend(id);
        }
        int r;
        if (orig) {
            if (!ai)
                r = int(OMtCtlV<uint32_t>(0x68, &ch) & 0xFF);
            else if (kind == 0)
                r = int(OMtV<uint32_t>(0x68, &ch) & 0xFF);
            else
                r = int(OMt<uint32_t>(kind == 1 ? 0x005d28b0 : 0x005d21d0, &ch) & 0xFF);
        } else {
            r = int(kind == 0 ? c->LoadGame(ch) : kind == 1 ? a->C_MeleeTrackerAI::LoadGame(ch) : c->C_MeleeTrackerController::LoadGame(ch));
        }
        if (closed)
            return r;
        // (Where the load left the chunk: its depth (+0x410) and position, then the next chunk after its own.)
        int depth;
        std::memcpy(&depth, reinterpret_cast<const uint8_t *>(&ch) + 0x410, 4);
        const int at = ch.GetPosition();
        ch.Ascend();
        (orig ? nextOrig : nextPort) = AiNextChunk(ch);
        return r * 0x400000 + depth * 0x10000 + (at & 0xFFFF);
    };
    static const char *const loads[] = {"LoadGame (auto)", "LoadGame (scripted)", "LoadGame (AI's own)", "LoadGame (root's)"};
    const char *what = loads[kind == 0 ? (ai && g_mtSize == 0xD0 ? 1 : 0) : kind + 1];
    if (ai)
        MtCallB(what, [&] { return load(false); }, [&](void *) { return load(true); });
    else
        CtlCall("LoadGame (remote, player)", true, [&] { return load(false); }, [&](void *) { return load(true); });
    CHECK(nextPort == nextOrig);
    static int shownNext;
    if (nextPort != nextOrig && shownNext++ < 3)
        std::printf("  %s: the next chunk %d / %d\n", what, nextPort, nextOrig);
    std::remove(portFile.c_str());
    std::remove(origFile.c_str());
}

void TestMtC()
{
    std::printf("melee tracker ai: the idle bark, saves, the remote and the player's control\n");
    for (int i = 0; i < 14000 / QUICK; ++i) {
        RandomMtB();
        C_MeleeTrackerAI *c = g_pMt;
        const bool scripted = g_mtSize == 0xD0;
        PortMtActor()->m_frame = g_asMeleeFrame;
        const int which = I(0, 21);
        if (which <= 1) {
            // The scripted tracker's Tick with its idle bark due now and then (the AI pair otherwise: its own Tick).
            const int dt = I(0, 200);
            if (scripted)
                g_pMtS->m_barkTimer = P(0.2f) ? -1 : I(0, 2 * dt + 5);
            const unsigned seed = unsigned(I(1, 1 << 30));
            MtCallB(scripted ? "Tick (bark)" : "Tick (auto)",
                    [&] {
                        std::srand(seed);
                        c->Tick(dt);
                        if (c->m_actionA != g_mtTestActionA && c->m_actionA) {
                            g_mtPortActionA = c->m_actionA;
                            g_mtPortActionB = c->m_actionB;
                        }
                        return 0;
                    },
                    [&](void *) {
                        std::srand(seed);
                        OMtV<void>(0x18, dt);
                        return 0;
                    });
            std::srand(seed + 1);
        } else if (which <= 2) {
            // (The path's checkpoints known ones: a load reads them unchecked.)
            for (S_AIWaypoint **w = c->m_pathBegin; w && w != c->m_pathEnd; ++w)
                (*w)->checkpoint = I(0, 7);
            MtSaveLoad(true, I(0, 2));
        } else if (which <= 5) {
            // The full AI's own slots (its variants override them): Reset, Detach, Tick.
            const int k = I(0, 2), dt = I(0, 200);
            if (k == 0)
                MtCallB("AI Reset", [&] { c->C_MeleeTrackerAI::Reset(); return 0; }, [&](void *) { OMt<void>(0x005cb0d0); return 0; });
            else if (k == 1)
                MtCallB("AI Detach", [&] { return int(c->C_MeleeTrackerAI::Detach()); },
                        [&](void *) { return int(OMt<uint32_t>(0x005cb410) & 0xFF); });
            else
                MtCallB("AI Tick",
                        [&] {
                            c->C_MeleeTrackerAI::Tick(dt);
                            if (c->m_actionA != g_mtTestActionA && c->m_actionA) {
                                g_mtPortActionA = c->m_actionA;
                                g_mtPortActionB = c->m_actionB;
                            }
                            return 0;
                        },
                        [&](void *) { OMt<void>(0x005ce190, dt); return 0; });
        } else {
            const bool player = which >= 13;
            RandomMtCtl(player);
            C_MeleeTrackerController *p = g_pCtl;
            switch (I(0, player ? 10 : 7)) {
            case 0:
                p->m_meleeTracker = nullptr;
                CtlCall("Ctl Attach", false, [&] { return int(p->Attach(PortMt())); },
                        [&](void *) { return int(OMtCtlV<uint32_t>(0x00, static_cast<void *>(g_mtOrigBytes)) & 0xFF); });
                break;
            case 1:
                CtlCall("Ctl Detach", false, [&] { return int(p->Detach()); }, [&](void *) { return int(OMtCtlV<uint32_t>(0x0C) & 0xFF); });
                break;
            case 2:
                CtlCall("Ctl Reset", false, [&] { p->Reset(); return 0; }, [&](void *) { OMtCtlV<void>(0x04); return 0; });
                break;
            case 3: {
                const int ms = I(0, 100);
                CtlCall("Ctl FrameUpdate", false, [&] { p->FrameUpdate(ms); return 0; }, [&](void *) { OMtCtlV<void>(0x14, ms); return 0; });
                break;
            }
            case 4: {
                const int dt = I(0, 200);
                CtlCall("Ctl Tick", false, [&] { p->Tick(dt); return 0; }, [&](void *) { OMtCtlV<void>(0x18, dt); return 0; });
                break;
            }
            case 5: {
                // (The remote moves the tracker's frame: where it is after each side's call, compared.)
                I3D_frame *frame = PortMt()->GetFrame();
                S_vector portAt, origAt;
                CtlCall("Ctl OnNetState", false,
                        [&] {
                            p->OnNetState(nullptr);
                            portAt = frame->GetPos();
                            return 0;
                        },
                        [&](void *) {
                            frame->SetPosInline(S_vector(-77, -77, -77));
                            OMtCtlV<void>(0x58, static_cast<void *>(nullptr));
                            origAt = frame->GetPos();
                            return 0;
                        });
                if (player)
                    break;   // (the player's keeps the state: the frame does not move)
                const bool same = std::memcmp(&portAt, &origAt, 12) == 0;
                CHECK(same);
                static int shownAt;
                if (!same && shownAt++ < 3)
                    std::printf("  OnNetState: the frame's position\n");
                break;
            }
            case 6:
                // (Now and then: the files are slow to make.)
                if (P(0.3f))
                    MtSaveLoad(false, 0);
                break;
            case 7:
                // The root's Attach (no variant keeps it): the remote's copy.
                p->m_meleeTracker = nullptr;
                CtlCall("Root Attach", false, [&] { return int(p->C_MeleeTrackerController::Attach(PortMt())); },
                        [&](void *) { return int(OMtCtl<uint32_t>(0x005ce630, static_cast<void *>(g_mtOrigBytes)) & 0xFF); });
                break;
            case 8:
                CtlCall("Player OnNetControl", false, [&] { p->OnNetControl(nullptr); return 0; },
                        [&](void *) { OMtCtlV<void>(0x5C, static_cast<void *>(nullptr)); return 0; });
                break;
            case 9:
                CtlCall("Player ResetState", false, [&] { p->ResetState(g_pMtSrc); return 0; },
                        [&](void *) { OMtCtlV<void>(0x10, static_cast<void *>(g_oMtSrc)); return 0; });
                break;
            default: {
                // (Now and then far: the pitch's limits.)
                auto small = [] { return P(0.5f) ? 0 : P(0.5f) ? 1 : -1; };
                const int dx = P(0.35f) ? small() : I(-300, 300), dy = P(0.35f) ? small() : P(0.3f) ? I(-600, 600) : I(-100, 100);
                CtlCall("Player MouseLook", false, [&] { g_pMtP->MouseLook(dx, dy); return 0; },
                        [&](void *) { OMtCtl<void>(0x005cf1b0, dx, dy); return 0; });
                break;
            }
            }
        }
    }
}

// The destructors: each variant's on a copy (the port's in place, the original's deleting destructor without its delete
// flag); the AI's path array freed (its waypoints first released, as Detach does). Nothing is logged: both run, the
// logs (none) compared.
void TestMtDestructors()
{
    std::printf("melee tracker ai: destructors\n");
    for (int i = 0; i < 400 / QUICK; ++i) {
        RandomMtB();
        const int kind = I(0, 3);
        alignas(16) static uint8_t portBytes[0x4B0], origBytes[0x4B0];
        std::memset(portBytes, 0, sizeof portBytes);
        std::memset(origBytes, 0, sizeof origBytes);
        static const uint32_t vtables[] = {0x00812250, 0x008122c8, 0x008121e0, 0x00812340};
        static const size_t sizes[] = {0xD0, 0xC0, 0x270, 0x4B0};
        C_MeleeTrackerController *p;
        if (kind == 0)
            p = ::new (portBytes) S_MtScripted;
        else if (kind == 1)
            p = ::new (portBytes) S_MtAuto;
        else if (kind == 2)
            p = ::new (portBytes) C_MeleeTrackerRemote;
        else
            p = ::new (portBytes) C_MeleeTrackerPlayerControl;
        if (kind < 2) {
            auto *ai = static_cast<C_MeleeTrackerAI *>(p);
            RandomMtController(ai, true);
            for (S_AIWaypoint **w = ai->m_pathBegin; w && w != ai->m_pathEnd; ++w)
                ReleaseMeleeTrackerWaypoint(*w);
            ai->m_pathEnd = ai->m_pathBegin;
            MtToOrig(ai, origBytes, sizes[kind]);
        }
        std::memcpy(origBytes, &vtables[kind], 4);
        DiffCall("Destructor",
                 [&] {
                     p->~C_MeleeTrackerController();
                     return 0;
                 },
                 [&](void *) {
                     typedef void *(MT * T_Dtor)(void *, int);
                     void **vt = *reinterpret_cast<void ***>(origBytes);
                     reinterpret_cast<T_Dtor>(vt[0x08 / 4])(origBytes, 0);
                     return 0;
                 });
        // (The object's bytes are not compared afterwards: the compiler drops the port destructor's stores into the dying
        // object.)
    }
}

void TestMeleeTrackerAi()
{
    SetUpSenses();
    SetUpSensesB();
    SetUpCombatAi();
    SetUpFireOrder();
    SetUpMeleeTrackerAi();
    SetUpMeleeTrackerB();
    g_humanAnswers = AsAnswers;
    g_visiblePartsOf = AsPartsOf;
    TestMtA();
    TestMtB();
    SetUpMeleeTrackerC();
    TestMtC();
    TestMtDestructors();
    g_humanAnswers = nullptr;
    g_visiblePartsOf = nullptr;
}
}  // namespace
