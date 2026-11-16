// Part 38 of the human harness (HUMAN_SNIPER builds, build_sniper.bat; included by human_test.cpp after
// aicombat_test.inc, whose fakes and helpers it uses): the sniper AI (game/sniper_controller.cpp) against the
// original's, on part 33's AI controller harness and part 35's humans.
//
// The port's sniper (a test class over C_SniperController: ExecuteCommand logged) is the AI pair's port side (part 33's
// AiCall converts and compares its C_AIController part); the original's is its constructor's object (0x0056ee90) with a
// copy of the sniper's vtable (0x00811618: ExecuteCommand logged). The sniper's own fields (+0x140 .. +0x180, the same
// offsets on both sides; its two vectors MSVC6 ones in the original) are converted and compared around each call. The
// human's events (the sniper post, the stimuli: an MSVC6 deque at +0x48 in the original) are an object pair of their
// own.
//
// Faked on both sides and logged: UpdateMove (ai_movement.cpp, step 4's; the original's 0x0057a750), the human's
// DrawLongGun / DrawSidearm (0x005a67c0 / 0x005a6830), the sight module's TestNow (0x0046f280), the checkpoints'
// GetNeighbours (0x00520830), ReleaseHumanEvent (0x005add20). CoverAgainst (enemy_combat.cpp's, inline in the
// original) is a copy of the port's here, until the enemy's part links that file.
#include "game/enemy_internal.h"
#include "script/script.h"

namespace {
// ---- the private members: through explicit instantiation (the access check does not apply to it)
template <class Tag, typename Tag::type M> struct S_SnRob {
    friend typename Tag::type SnGet(Tag) { return M; }
};
#define SN_ROB(NAME, TYPE, MEMBER) \
    struct NAME { \
        using type = TYPE; \
        friend type SnGet(NAME); \
    }; \
    template struct S_SnRob<NAME, MEMBER>;
typedef C_SniperController S_Sn;
SN_ROB(SnUpdateTimers, void (S_Sn::*)(int), &S_Sn::UpdateTimers)
SN_ROB(SnDropHandsItem, void (S_Sn::*)(), &S_Sn::DropHandsItem)
SN_ROB(SnUpdateFiring, void (S_Sn::*)(int), &S_Sn::UpdateFiring)
SN_ROB(SnUpdateTask, S_AITask *(S_Sn::*)(int), &S_Sn::UpdateTask)
SN_ROB(SnCheckDone, void (S_Sn::*)(int), &S_Sn::CheckDone)
SN_ROB(SnRunTask, void (S_Sn::*)(int), &S_Sn::RunTask)
SN_ROB(SnStartTask, void (S_Sn::*)(), &S_Sn::StartTask)
SN_ROB(SnUpdateZombie, void (S_Sn::*)(int), &S_Sn::UpdateZombie)
SN_ROB(SnUpdateSniper, void (S_Sn::*)(int), &S_Sn::UpdateSniper)
SN_ROB(SnTestSightOfTarget, void (S_Sn::*)(), &S_Sn::TestSightOfTarget)
SN_ROB(SnTakeCoverStance, void (S_Sn::*)(const S_Checkpoint *), &S_Sn::TakeCoverStance)
SN_ROB(SnHasSomethingToDo, bool (S_Sn::*)(), &S_Sn::HasSomethingToDo)
SN_ROB(SnSeesEnemies, bool (S_Sn::*)(), &S_Sn::SeesEnemies)
SN_ROB(SnSeesTarget, bool (S_Sn::*)(), &S_Sn::SeesTarget)
SN_ROB(SnTakeStimulusSource, C_Actor *(S_Sn::*)(), &S_Sn::TakeStimulusSource)
SN_ROB(SnChooseStance, void (S_Sn::*)(const S_Checkpoint *), &S_Sn::ChooseStance)
SN_ROB(SnFindCoverPoint, int (S_Sn::*)(), &S_Sn::FindCoverPoint)
SN_ROB(SnFindFirePointHeard, int (S_Sn::*)(), &S_Sn::FindFirePointHeard)
SN_ROB(SnFindFirePointSeen, int (S_Sn::*)(), &S_Sn::FindFirePointSeen)
SN_ROB(SnHomePos, S_vector4 S_Sn::*, &S_Sn::m_homePos)
SN_ROB(SnStartStance, uint32_t S_Sn::*, &S_Sn::m_startStance)
SN_ROB(SnFlags, uint32_t S_Sn::*, &S_Sn::m_sniperFlags)
SN_ROB(SnTaskTime, int S_Sn::*, &S_Sn::m_taskTime)
SN_ROB(SnTarget, C_Human *S_Sn::*, &S_Sn::m_target)
SN_ROB(SnPoints, std::vector<uint16_t> S_Sn::*, &S_Sn::m_points)
SN_ROB(SnSniperPoints, std::vector<uint16_t> S_Sn::*, &S_Sn::m_sniperPoints)
SN_ROB(EvSniperPos, S_vector4 C_HumanEvents::*, &C_HumanEvents::m_sniperPos)
SN_ROB(EvSniperRadius, float C_HumanEvents::*, &C_HumanEvents::m_sniperRadius)
SN_ROB(EvStimuli, std::deque<S_HumanEvent *> C_HumanEvents::*, &C_HumanEvents::m_stimuli)
SN_ROB(ScOnAlarmDone, int C_Script::*, &C_Script::m_onAlarmDone)
SN_ROB(ItTypeId, int C_Item::*, &C_Item::m_typeId)
#undef SN_ROB

struct S_SniperScript {
    int move = 0;                                       // UpdateMove
    bool longGun = false;                               // DrawLongGun
    std::vector<uint16_t> neighbours;                   // GetNeighbours
} g_sn;
}  // namespace

// ---- the port's side of the fakes
int C_AIController::UpdateMove(int timeDelta)
{
    Log(this, "UpdateMove", {double(timeDelta)});
    return g_sn.move;
}
bool C_Human::DrawLongGun()
{
    Log(this, "DrawLongGun");
    return g_sn.longGun;
}
bool C_Human::DrawSidearm(int reason)
{
    Log(this, "DrawSidearm", {double(reason)});
    return true;
}
bool C_SightModule::TestNow(int observer, int target)
{
    Log(nullptr, "SightTestNow", {double(observer), double(target)});
    return true;
}
bool C_CheckpointNetwork::GetNeighbours(int from, float radius, std::vector<uint16_t> *out)
{
    Log(nullptr, "GetNeighbours", {double(from), radius});
    *out = g_sn.neighbours;
    return true;
}
void C_Human::ProcessEvents() { Log(this, "ProcessEvents"); }
void C_Human::StartScriptedController() { Log(this, "StartScriptedController"); }
void C_Human::RequestThrowItem(C_Item *item, int arg, const S_vector &dir)
{
    Log(this, ("RequestThrowItem " + NameOf(item)).c_str(), {double(arg), dir.x, dir.y, dir.z});
}
// (human_events.cpp's, copied: GetActor is the test's on both sides.)
C_Actor *IdToActor(int id)
{
    if (id < 0)
        return nullptr;
    return g_actorsModule.GetActor(id);
}
// (enemy_combat.cpp's, copied: that file is linked by the enemy's part.)
uint32_t CoverAgainst(const S_Checkpoint *cp, const S_vector &pos, uint32_t stances)
{
    const S_vector dir(pos.x - cp->pos.x, pos.y - cp->pos.y, pos.z - cp->pos.z);
    const uint8_t bit = S_Checkpoint::GetDirectionBit(dir);
    uint32_t result = 0;
    if ((stances & 1) && (cp->cover1 & bit))
        result = 1;
    if ((stances & 2) && (cp->cover2 & bit))
        result |= 2;
    if ((stances & 4) && (cp->cover4 & bit))
        result |= 4;
    return result;
}

namespace {
// The shared events the stimuli hold (their source ids).
const int SN_EVENTS = 8;
alignas(16) S_HumanEvent g_snEv[SN_EVENTS];
int SnEventIndex(const void *e)
{
    for (int k = 0; k < SN_EVENTS; ++k)
        if (e == &g_snEv[k])
            return k;
    return e ? 99 : -1;
}
}  // namespace
// (Silent: the original's sniper puts the stimulus it takes into the pool inline; the stimuli are compared.)
void ReleaseHumanEvent(S_HumanEvent *) {}

namespace {
// ---- the original's side
int AC OS_UpdateMove(void *self, int timeDelta)
{
    Log(self, "UpdateMove", {double(timeDelta)});
    return g_sn.move;
}
int AC OS_DrawLongGun(void *self)
{
    Log(self, "DrawLongGun");
    return g_sn.longGun;
}
int AC OS_DrawSidearm(void *self, int reason)
{
    Log(self, "DrawSidearm", {double(reason)});
    return 1;
}
int AC OS_TestNow(void *, int observer, int target)
{
    Log(nullptr, "SightTestNow", {double(observer), double(target)});
    return 1;
}
// (Its vector: allocator, first, last, end; the original's new is the test's malloc.)
int AC OS_GetNeighbours(void *, int from, float radius, uint32_t *out)
{
    Log(nullptr, "GetNeighbours", {double(from), radius});
    const size_t n = g_sn.neighbours.size();
    out[1] = out[2] = out[3] = 0;
    if (n) {
        auto *data = static_cast<uint16_t *>(std::malloc(n * 2));
        std::memcpy(data, g_sn.neighbours.data(), n * 2);
        out[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        out[2] = out[3] = out[1] + 2 * static_cast<uint32_t>(n);
    }
    return 1;
}
void OS_ReleaseHumanEvent(void *) {}
// The walking points (Get's answers on both sides: the original pushes a 16-bit index), part 33's at the network's
// stride.
S_Checkpoint *SnPointAt(int index) { return index < 8 ? reinterpret_cast<S_Checkpoint *>(g_aiPoints[index]) : nullptr; }
// The vtables each side calls through: the weapon component's (part 36's) and the definitions' slot 0x10 (the port's
// GetCarryMode, the original's GetBehaviour: parts 23 and 35 gave it one each).
void SnVtablesFor(bool orig)
{
    CompVtableFor(orig);
    g_defVt[0x10 / 4] = orig ? reinterpret_cast<void *>(DF_GetBehaviour) : reinterpret_cast<void *>(DF_GetCarryMode);
}
// The AI's control arrays: the port's (ai_controller.cpp) and the original's globals.
float *OrigAiAxes() { return OriginalAt<float *>(0x008ab058); }
float *OrigAiButtons() { return OriginalAt<float *>(0x008aaf58); }
void AC OS_ProcessEvents(void *self) { Log(self, "ProcessEvents"); }
void AC OS_StartScriptedController(void *self) { Log(self, "StartScriptedController"); }
void AC OS_RequestThrowItem(void *self, void *item, int arg, const S_vector *dir)
{
    Log(self, ("RequestThrowItem " + NameOf(item)).c_str(), {double(arg), dir->x, dir->y, dir->z});
}
// The actors by net id (GetActor's answers on both sides): the humans, part 23's actors; one id now and then gone.
int g_snGoneId = -1000;
C_Actor *SnActorById(int id, bool original)
{
    if (id == g_snGoneId)
        return nullptr;
    std::vector<C_Actor *> all = {g_h.human, PortOther()};
    for (int k = 0; k < ACTOR_KIND_COUNT; ++k)
        all.push_back(static_cast<C_Actor *>(PortActor(k)));
    for (C_Actor *a : all)
        if (a && a->m_netId == id)
            return original ? static_cast<C_Actor *>(ToOrig(a)) : a;
    return nullptr;
}
void *AC SN_GetActor(void *self, int index)
{
    Log(self, "GetActor", {double(index)});
    return SnActorById(index, true);
}
void *AC SN_GetActorById(void *self, int id)
{
    Log(self, "GetActorById", {double(id)});
    return SnActorById(id, true);
}
// The worn containers' IsLocked (part 21's shared vtable: the original's slot 0x48 and the port's).
bool g_snHandsLocked = false;
int AC SN_IsLocked(void *self)
{
    Log(self, "ContainerIsLocked");
    return g_snHandsLocked;
}

#include "msvc_containers.inc"

// ---- the pair
struct S_TestSniper : C_SniperController {
    bool ExecuteCommand(const S_HumanCommand &c) override
    {
        LogCommand(this, c);
        return true;
    }
};
S_TestSniper *g_psn;
uint8_t *g_osn;
void *g_osnVt[0x48];
C_HumanEvents g_snEvents;
alignas(16) uint8_t g_snOrigEvents[0x100];
alignas(16) uint8_t g_snScriptBytes[0x400];
S_FrameData g_snFrameData;
S_Sn *Sn() { return g_psn; }

// An MSVC6 vector of 16-bit points at `at` (allocator, first, last, end; its own malloc'd buffer).
void SnVectorPut(uint8_t *at, const std::vector<uint16_t> &items)
{
    uint32_t w[4] = {0, 0, 0, 0};
    if (!items.empty()) {
        const size_t spare = size_t(I(0, 2));
        auto *data = static_cast<uint16_t *>(std::malloc((items.size() + spare) * 2));
        std::memcpy(data, items.data(), items.size() * 2);
        w[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        w[2] = w[1] + 2 * static_cast<uint32_t>(items.size());
        w[3] = w[2] + 2 * static_cast<uint32_t>(spare);
    }
    std::memcpy(at, w, 16);
}
std::vector<uint16_t> SnVectorGet(const uint8_t *at)
{
    uint32_t w[4];
    std::memcpy(w, at, 16);
    if (!w[1])
        return {};
    const auto *first = reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(w[1]));
    return std::vector<uint16_t>(first, first + (w[2] - w[1]) / 2);
}

// The sniper's fields and the events in the original's layout.
void SniperToOrig()
{
    S_Sn *s = Sn();
    std::memcpy(g_osn + 0x140, &(s->*SnGet(SnHomePos())), 16);
    std::memcpy(g_osn + 0x150, &(s->*SnGet(SnStartStance())), 4);
    std::memcpy(g_osn + 0x154, &(s->*SnGet(SnFlags())), 4);
    std::memcpy(g_osn + 0x158, &(s->*SnGet(SnTaskTime())), 4);
    void *target = s->*SnGet(SnTarget()) ? ToOrig(s->*SnGet(SnTarget())) : nullptr;
    std::memcpy(g_osn + 0x15C, &target, 4);
    SnVectorPut(g_osn + 0x160, s->*SnGet(SnPoints()));
    SnVectorPut(g_osn + 0x170, s->*SnGet(SnSniperPoints()));
    C_HumanEvents &e = g_snEvents;
    std::memset(g_snOrigEvents, 0, sizeof g_snOrigEvents);
    std::memcpy(g_snOrigEvents, &(e.*SnGet(EvSniperPos())), 16);
    std::memcpy(g_snOrigEvents + 0x10, &(e.*SnGet(EvSniperRadius())), 4);
    std::vector<uint32_t> stimuli;
    for (S_HumanEvent *ev : e.*SnGet(EvStimuli()))
        stimuli.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ev)));
    MsvcDequePut(g_snOrigEvents + 0x48, stimuli);
    std::memcpy(OrigAiAxes(), g_aiAxes, sizeof g_aiAxes);
    std::memcpy(OrigAiButtons(), g_aiButtons, sizeof g_aiButtons);
}
std::string SniperDiffers()
{
    S_Sn *s = Sn();
    if (std::memcmp(g_osn + 0x140, &(s->*SnGet(SnHomePos())), 16) != 0)
        return "homePos";
    auto word = [&](size_t at, uint32_t port, const char *name) {
        uint32_t o;
        std::memcpy(&o, g_osn + at, 4);
        char text[96];
        std::snprintf(text, sizeof text, "%s: port %08x original %08x", name, port, o);
        return o == port ? std::string() : std::string(text);
    };
    std::string why = word(0x150, s->*SnGet(SnStartStance()), "startStance");
    if (why.empty())
        why = word(0x154, s->*SnGet(SnFlags()), "sniperFlags");
    if (why.empty())
        why = word(0x158, uint32_t(s->*SnGet(SnTaskTime())), "taskTime");
    if (!why.empty())
        return why;
    void *target = s->*SnGet(SnTarget()) ? ToOrig(s->*SnGet(SnTarget())) : nullptr, *otarget;
    std::memcpy(&otarget, g_osn + 0x15C, 4);
    if (target != otarget)
        return "target";
    if (SnVectorGet(g_osn + 0x160) != s->*SnGet(SnPoints()))
        return "points";
    if (SnVectorGet(g_osn + 0x170) != s->*SnGet(SnSniperPoints()))
        return "sniperPoints";
    std::vector<uint32_t> stimuli;
    for (S_HumanEvent *ev : g_snEvents.*SnGet(EvStimuli()))
        stimuli.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ev)));
    if (MsvcDequeGet(g_snOrigEvents + 0x48) != stimuli)
        return "stimuli";
    if (std::memcmp(OrigAiAxes(), g_aiAxes, sizeof g_aiAxes) != 0)
        return "aiAxes";
    if (std::memcmp(OrigAiButtons(), g_aiButtons, sizeof g_aiButtons) != 0)
        return "aiButtons";
    return "";
}
template <class P, class O> void SnCall(const char *what, P port, O original)
{
    SniperToOrig();
    AiCall(what,
           [&] {
               SnVtablesFor(false);
               return port();
           },
           [&](void *o) {
               SnVtablesFor(true);
               const int r = original(o);
               SnVtablesFor(false);
               // (The stale w words the original hands the facing (part 36's), and the look direction's, +0x2C: the
               // turn passes a stack word it does not set, the port 0.)
               FacingWFromPort();
               uint8_t *facing;
               std::memcpy(&facing, g_osn + 0x124, 4);
               std::memcpy(facing + 0x2C, reinterpret_cast<const uint8_t *>(g_psn->m_facing) + 0x2C, 4);
               return r;
           });
    const std::string why = SniperDiffers();
    CHECK(why.empty());
    static std::map<std::string, int> shown;
    if (!why.empty() && shown[what]++ < 3)
        std::printf("  %s: %s\n", what, why.c_str());
}
// A pointer as the original's (the port's mapped), for return values.
int SnWord(const void *p, bool orig)
{
    return static_cast<int>(reinterpret_cast<uintptr_t>(orig || !p ? p : ToOrig(p)));
}

void SetUpSniper()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0057a750, FAKE(OS_UpdateMove)},
        {0x005a67c0, FAKE(OS_DrawLongGun)},
        {0x005a6830, FAKE(OS_DrawSidearm)},
        {0x0046f280, FAKE(OS_TestNow)},
        {0x00520830, FAKE(OS_GetNeighbours)},
        {0x005add20, FAKE(OS_ReleaseHumanEvent)},
        {0x005a8480, FAKE(OS_ProcessEvents)},
        {0x005af680, FAKE(OS_StartScriptedController)},
        {0x005a2c50, FAKE(OS_RequestThrowItem)},
        {0x00475dd0, FAKE(SN_GetActor)},
        {0x00475d50, FAKE(SN_GetActorById)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    g_checkpointAt = SnPointAt;
    // (The definitions' slot the sides share: SnVtablesFor's.)
    if (VSlot(&C_ItemDef::GetCarryMode) != 0x10 / 4) {
        std::printf("sniper: the definitions' slots moved\n");
        std::exit(3);
    }
    g_actorByIndex = SnActorById;
    g_actorById = SnActorById;
    g_wornVt[0x48 / 4] = reinterpret_cast<void *>(SN_IsLocked);
    g_wornVt[VSlot(&C_ItemContainer::IsLocked)] = reinterpret_cast<void *>(SN_IsLocked);
    g_psn = new S_TestSniper;
    g_osn = static_cast<uint8_t *>(OC_AlignedNew(0x180, 16));
    typedef void(AC * T_Ctor)(void *, int);
    OriginalAt<T_Ctor>(0x0056ee90)(g_osn, HUMAN_CONTROLLER_SNIPER);
    std::memcpy(g_osnVt, OriginalAt<void *>(0x00811618), sizeof g_osnVt);
    g_osnVt[0xDC / 4] = reinterpret_cast<void *>(OA_ExecuteCommand);
    void *vt = g_osnVt;
    std::memcpy(g_osn, &vt, 4);
    g_names[g_psn] = g_names[g_osn] = "sniper";
    g_extraPointers.push_back({static_cast<C_AIController *>(g_psn), g_osn});
    const char *const parts[] = {"snFacing", "snFireOrder", "snMove", "snDriver", "snAlert"};
    void *const portParts[] = {g_psn->m_facing, g_psn->m_fireOrder, g_psn->m_move, g_psn->m_driver, g_psn->m_alert};
    for (int k = 0; k < 5; ++k) {
        void *op;
        std::memcpy(&op, g_osn + 0x124 + 4 * k, 4);
        g_names[portParts[k]] = g_names[op] = parts[k];
        g_extraPointers.push_back({portParts[k], op});
    }
    g_names[&g_snEvents] = g_names[g_snOrigEvents] = "events";
    g_extraPointers.push_back({&g_snEvents, g_snOrigEvents});
    for (int k = 0; k < SN_EVENTS; ++k)
        g_names[&g_snEv[k]] = "event" + std::to_string(k);
    g_names[g_snScriptBytes] = "script";
    g_names[g_aiAxes] = g_names[OrigAiAxes()] = "aiAxes";
    g_names[g_aiButtons] = g_names[OrigAiButtons()] = "aiButtons";
    g_extraPointers.push_back({g_aiAxes, OrigAiAxes()});
    g_extraPointers.push_back({g_aiButtons, OrigAiButtons()});
    // The AI pair is the sniper from here on.
    g_pa = reinterpret_cast<S_TestAi *>(static_cast<C_AIController *>(g_psn));
    g_oa = g_osn;
}

// The fighting's state (part 36's), then the sniper's: its fields, its task (type 3, its states), the post's points,
// the events' post and stimuli, the human's morale, the script's OnAlarmDone, the frame's data. `aiming`: the call may
// fire at the sniper's target (Tick): the target and the enemies in view the tested human only (its model has the
// bones GetAimPoint picks from; the copies have none).
void RandomSniper(bool aiming)
{
    RandomFireOrder(true);
    S_Sn *s = Sn();
    C_Human *h = g_h.human;
    C_Human *const humans[2] = {h, aiming ? h : PortOther()};
    if (aiming) {
        h->m_seenEnemies.clear();
        for (int k = P(0.4f) ? 0 : I(1, 2); k > 0; --k)
            h->m_seenEnemies.push_back(h);
    }
    // (The game's walking network: the original reads a missing one's ladder points; the port does not.)
    GetGame()->m_checkpoints = reinterpret_cast<C_Checkpoints *>(g_checkpointNet);
    void *net = g_checkpointNet;
    std::memcpy(g_origGame + 0x54, &net, 4);
    s->m_events = &g_snEvents;
    h->m_events = &g_snEvents;
    s->*SnGet(SnHomePos()) = AiDir();
    s->*SnGet(SnStartStance()) = uint32_t(I(0, 7));
    s->*SnGet(SnFlags()) = uint32_t(I(0, 15));
    s->*SnGet(SnTaskTime()) = AiInt();
    s->*SnGet(SnTarget()) = P(0.3f) ? nullptr : humans[I(0, 1)];
    std::vector<uint16_t> &points = s->*SnGet(SnPoints());
    std::vector<uint16_t> &sniperPoints = s->*SnGet(SnSniperPoints());
    points.clear();
    sniperPoints.clear();
    for (int k = P(0.2f) ? 0 : I(1, 6); k > 0; --k)
        points.push_back(uint16_t(I(0, 7)));
    for (uint16_t p : points)
        if (P(0.5f))
            sniperPoints.push_back(p);
    // The task: a sniper's (type 3) in one of its states, now and then none or another type.
    if (s->m_task)
        ReleaseTask(s->m_task);
    const float r = U(0, 1);
    if (r < 0.15f) {
        s->m_task = nullptr;
    } else if (r < 0.25f) {
        s->m_task = AllocTask(I(1, 12));
        RandomAiTask(s->m_task);
    } else {
        s->m_task = AllocTask(3);
        RandomAiTask(s->m_task);
        // (Idle and moving now and then more: the states' exits wait on the timer and the move order.)
        s->m_task->unknown08 = P(0.3f) ? (P(0.5f) ? S_Sn::SNIPER_IDLE : S_Sn::SNIPER_MOVING) : I(0, 6);
        s->m_task->unknown10 = P(0.3f) ? 0 : I(-100, 20000);
    }
    if (P(0.5f))
        s->m_move->flags |= S_AIMoveOrder::FLAG_DONE;
    // A network game's net object (the idle movement's send mask), the messages queued for the clients.
    if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL && P(0.8f))
        s->m_netObject = reinterpret_cast<C_NetObject *>(g_netObjectBytes);
    g_h.movement->m_unknown1AC = P(0.5f) ? 0 : I(1, 3);
    g_h.movement->m_unknown9C0 = P(0.5f) ? 0 : I(1, 900);
    if (P(0.5f))
        RandomAiQueue(g_pa->m_toClients, g_oa + 0x90);
    // The events: the post, the stimuli (shared events from the humans and others).
    C_HumanEvents &e = g_snEvents;
    e.*SnGet(EvSniperPos()) = {h->m_position.x + U(-10, 10), h->m_position.y, h->m_position.z + U(-10, 10), 1.0f};
    e.*SnGet(EvSniperRadius()) = U(0, 30);
    std::deque<S_HumanEvent *> &stimuli = e.*SnGet(EvStimuli());
    stimuli.clear();
    for (int k = P(0.45f) ? 0 : I(1, 4); k > 0; --k) {
        S_HumanEvent *ev = &g_snEv[I(0, SN_EVENTS - 1)];
        stimuli.push_back(ev);
    }
    for (S_HumanEvent &ev : g_snEv) {
        static const int types[] = {AI_EVENT_ENEMY_LOST, AI_EVENT_SOUND_0, AI_EVENT_SOUND_2, AI_EVENT_HIT_5, AI_EVENT_SHOT_AT,
                                    AI_EVENT_LOUD_SOUND, 1, 9};
        ev.type = types[I(0, 7)];
        ev.sourceId = P(0.2f) ? -1 : P(0.7f) ? humans[I(0, 1)]->GetNetId() : I(0, 50);
        ev.timeLeft = I(0, 30000);
    }
    // The morale (now and then at its edges).
    auto morale = [] { return P(0.2f) ? (P(0.5f) ? I(19, 21) : I(39, 41)) : P(0.2f) ? I(74, 76) : P(0.2f) ? I(89, 91) : I(0, 100); };
    h->m_stats[STAT_MORALE] = morale();
    h->m_initialStats[STAT_MORALE] = morale();
    // The script (OnAlarmDone or none) in the frame's data (the port's member and the original's +0xEC).
    const int onAlarmDone = P(0.5f) ? -1 : I(0, 50);
    reinterpret_cast<C_Script *>(g_snScriptBytes)->*SnGet(ScOnAlarmDone()) = onAlarmDone;
    std::memcpy(g_snScriptBytes + 0xEC, &onAlarmDone, 4);
    g_snFrameData = S_FrameData();
    g_snFrameData.actor = h;
    g_snFrameData.script = P(0.8f) ? reinterpret_cast<C_Script *>(g_snScriptBytes) : nullptr;
    g_snHandsLocked = P(0.2f);
    // The target gone now and then (GetActor no longer knows its id); the hands' weapons single-shot launchers now and
    // then (item type 0x26, +0x0C on both sides).
    C_Human *const target = s->*SnGet(SnTarget());
    g_snGoneId = target && P(0.2f) ? target->GetNetId() : -1000;
    for (int k = 0; k < 2; ++k) {
        const int type = P(0.3f) ? 0x26 : I(0, 0x40);
        reinterpret_cast<C_Item *>(g_weaponPort[k])->*SnGet(ItTypeId()) = type;
        std::memcpy(g_weaponOrig[k] + 0x0C, &type, 4);
    }
    g_sn.move = I(0, 3);
    g_sn.longGun = P(0.5f);
    g_sn.neighbours.clear();
    for (int k = I(0, 6); k > 0; --k)
        g_sn.neighbours.push_back(uint16_t(I(0, 7)));
    // The next task of type 3 of each side's pool alike (a new task keeps its pool object's +0x0C and +0x10 on both
    // sides: the pools' objects differ): one taken, filled alike, put back (both pools give back the last one put).
    {
        const int stale0C = AiInt(), stale10 = AiInt();
        S_AITask3 *t = AllocTask3(0, nullptr, S_vector4{0, 0, 0, 0});
        t->unknown0C = stale0C;
        t->unknown10 = stale10;
        ReleaseTask(t);
        uint8_t *o = OrigTask(3);
        std::memcpy(o + 0x0C, &stale0C, 4);
        std::memcpy(o + 0x10, &stale10, 4);
        typedef void(AC * T_Release)(void *, void *);
        OriginalAt<T_Release>(0x00582c50)(AiPools(), o);
    }
    // The walking network: part 33's points at its own stride (Get and GetIndex agree), about the human; the human at
    // one of them now and then.
    const uint16_t count = 8;
    void *table = g_aiPoints;
    std::memcpy(g_checkpointNet, &count, 2);
    std::memcpy(g_checkpointNet + 4, &table, 4);
    s->m_checkpoints = reinterpret_cast<C_Checkpoints *>(g_checkpointNet);
    const S_vector own = g_h.frame->GetWorldPos();
    for (int k = 0; k < 8; ++k) {
        auto *point = reinterpret_cast<S_Checkpoint *>(g_aiPoints[k]);
        point->pos = own + (P(0.2f) ? S_vector(0, 0, 0) : P(0.4f) ? S_vector(U(-1, 1), 0, U(-1, 1)) : S_vector(U(-12, 12), U(-1, 1), U(-12, 12)));
        point->type = uint8_t(P(0.4f) ? CHECKPOINT_TYPE_SNIPER : I(0, 12));
        point->radius = uint8_t(I(0, 64));
        point->flags = uint16_t(I(0, 0xFFFF));
        point->cover1 = uint8_t(I(0, 255));
        point->cover2 = uint8_t(I(0, 255));
        point->cover4 = uint8_t(I(0, 255));
    }
    s->m_currentCheckpoint = P(0.2f) ? nullptr : reinterpret_cast<S_Checkpoint *>(g_aiPoints[I(0, 7)]);
}

void TestSniperA()
{
    std::printf("sniper: life cycle, the task, its states, the helpers, saves\n");
    for (int i = 0; i < 20000 / QUICK; ++i) {
        const int which = I(0, 21);
        RandomSniper(which == 3);
        S_Sn *s = Sn();
        const int dt = I(0, 200);
        const S_Checkpoint *cp = P(0.15f) ? nullptr : reinterpret_cast<const S_Checkpoint *>(g_aiPoints[I(0, 7)]);
        switch (which) {
        case 0: {
            C_HumanController *previous = P(0.5f) ? nullptr : static_cast<C_HumanController *>(g_pc2);
            g_h.frame->SetData(&g_snFrameData);
            SnCall("Attach", [&] { s->Attach(g_h.human, previous); return 0; },
                   [&](void *) { OAi<void>(0x00559a50, static_cast<void *>(g_h.human ? ToOrig(g_h.human) : nullptr), previous ? ToOrig(previous) : nullptr); return 0; });
            break;
        }
        case 1:
            SnCall("Detach", [&] { s->Detach(); return 0; }, [&](void *) { OAi<void>(0x00559c20); return 0; });
            break;
        case 2:
            SnCall("Update", [&] { s->Update(dt); return 0; }, [&](void *) { OAi<void>(0x00559da0, dt); return 0; });
            break;
        case 3:
            SnCall("Tick", [&] { s->Tick(dt); return 0; }, [&](void *) { OAi<void>(0x00559de0, dt); return 0; });
            break;
        case 4:
            SnCall("UpdateTimers", [&] { (s->*SnGet(SnUpdateTimers()))(dt); return 0; }, [&](void *) { OAi<void>(0x00559c60, dt); return 0; });
            break;
        case 5:
            SnCall("DropHandsItem", [&] { (s->*SnGet(SnDropHandsItem()))(); return 0; }, [&](void *) { OAi<void>(0x00559cf0); return 0; });
            break;
        case 6:
            SnCall("UpdateFiring", [&] { (s->*SnGet(SnUpdateFiring()))(dt); return 0; }, [&](void *) { OAi<void>(0x00559d40, dt); return 0; });
            break;
        case 7: {
            S_HumanEvent *ev = &g_snEv[I(0, SN_EVENTS - 1)];
            SnCall("HandleEvent", [&] { return SnEventIndex(s->HandleEvent(ev)); },
                   [&](void *) { return SnEventIndex(OAi<void *>(0x00559f60, static_cast<void *>(ev))); });
            break;
        }
        case 8:
            SnCall("UpdateTask", [&] { return SnWord((s->*SnGet(SnUpdateTask()))(dt), false) != 0; },
                   [&](void *) { return OAi<void *>(0x00559eb0, dt) != nullptr; });
            break;
        case 9:
            SnCall("CheckDone", [&] { (s->*SnGet(SnCheckDone()))(dt); return 0; }, [&](void *) { OAi<void>(0x0055a0e0, dt); return 0; });
            break;
        case 10:
            SnCall("RunTask", [&] { (s->*SnGet(SnRunTask()))(dt); return 0; }, [&](void *) { OAi<void>(0x0055a100, dt); return 0; });
            break;
        case 11:
            SnCall("StartTask", [&] { (s->*SnGet(SnStartTask()))(); return 0; }, [&](void *) { OAi<void>(0x0055a130); return 0; });
            break;
        case 12:
            if (!s->m_task || s->m_task->type != 3)
                break;
            SnCall("UpdateZombie", [&] { (s->*SnGet(SnUpdateZombie()))(dt); return 0; }, [&](void *) { OAi<void>(0x0055a6e0, dt); return 0; });
            break;
        case 13:
            if (!s->m_task || s->m_task->type != 3)
                break;
            SnCall("UpdateSniper", [&] { (s->*SnGet(SnUpdateSniper()))(dt); return 0; }, [&](void *) { OAi<void>(0x0055ad50, dt); return 0; });
            break;
        case 14:
            SnCall("TestSightOfTarget", [&] { (s->*SnGet(SnTestSightOfTarget()))(); return 0; }, [&](void *) { OAi<void>(0x0055b6c0); return 0; });
            break;
        case 15:
            SnCall("TakeCoverStance", [&] { (s->*SnGet(SnTakeCoverStance()))(cp); return 0; },
                   [&](void *) { OAi<void>(0x0055b6f0, static_cast<const void *>(cp)); return 0; });
            break;
        case 16: {
            const int k = I(0, 2);
            static const uint32_t at[] = {0x0055b860, 0x0055b8a0, 0x0055b8d0};
            static const char *const names[] = {"HasSomethingToDo", "SeesEnemies", "SeesTarget"};
            SnCall(names[k],
                   [&] {
                       return int(k == 0 ? (s->*SnGet(SnHasSomethingToDo()))() : k == 1 ? (s->*SnGet(SnSeesEnemies()))() : (s->*SnGet(SnSeesTarget()))());
                   },
                   [&](void *) { return int(OAi<uint32_t>(at[k]) & 0xFF); });
            break;
        }
        case 17:
            SnCall("TakeStimulusSource", [&] { return SnWord((s->*SnGet(SnTakeStimulusSource()))(), false); },
                   [&](void *) { return SnWord(OAi<void *>(0x0055b920), true); });
            break;
        case 18:
            SnCall("ChooseStance", [&] { (s->*SnGet(SnChooseStance()))(cp); return 0; },
                   [&](void *) { OAi<void>(0x0055bc40, static_cast<const void *>(cp)); return 0; });
            break;
        case 19: {
            const int k = I(0, 2);
            static const uint32_t at[] = {0x0055bd40, 0x0055c0b0, 0x0055c450};
            static const char *const names[] = {"FindCoverPoint", "FindFirePointHeard", "FindFirePointSeen"};
            SnCall(names[k],
                   [&] {
                       return (k == 0 ? (s->*SnGet(SnFindCoverPoint()))() : k == 1 ? (s->*SnGet(SnFindFirePointHeard()))() : (s->*SnGet(SnFindFirePointSeen()))()) & 0xFFFF;
                   },
                   [&](void *) { return OAi<int>(at[k]) & 0xFFFF; });   // (a 16-bit point: the high word is stale)
            break;
        }
        case 20: {
            // (An interaction has its target, as part 33's: the original saves the target's id without a check.)
            static const int kinds[] = {0, 1, 2, 9, 11, KIND_HUMAN};
            if (P(0.6f)) {
                s->m_interaction = P(0.2f) ? HUMAN_COMMAND_HEAL_SELF : I(0, 0x26);
                s->m_interactionTarget = static_cast<C_Actor *>(PortActor(kinds[I(0, 5)]));
            } else {
                s->m_interaction = -1;
            }
            const std::string portFile = AiTempFile("_snport.sav"), origFile = AiTempFile("_snorig.sav");
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            auto save = [&](const std::string &file, bool orig) {
                C_chunk c;
                c.Open(file.c_str(), true);
                c.Begin(0x7777);
                if (orig)
                    OAi<void>(0x0055c820, &c);
                else
                    s->SaveGame(c);
                c.Ascend();
                c.Begin(0x0BD1);
                c.Ascend();
                return 0;
            };
            SnCall("SaveGame", [&] { return save(portFile, false); }, [&](void *) { return save(origFile, true); });
            const bool same = AiFileBytes(portFile) == AiFileBytes(origFile);
            CHECK(same);
            static int shown;
            if (!same && shown++ < 3)
                std::printf("  SaveGame: the files differ\n");
            RandomSniper(false);
            // (Every saved actor known: a task naming an unknown one keeps its pool object's, which differ per side.)
            g_snGoneId = -1000;
            auto load = [&](bool orig) {
                C_chunk c;
                c.Open(portFile.c_str(), false);
                uint16_t id;
                c.Descend(id);
                if (orig)
                    OAi<void>(0x0055cb50, &c);
                else
                    s->LoadGame(c);
                int depth;
                std::memcpy(&depth, reinterpret_cast<const uint8_t *>(&c) + 0x410, 4);
                const int at = c.GetPosition();
                c.Ascend();
                return depth * 0x10000 + (at & 0xFFFF) + AiNextChunk(c) * 0x100000;
            };
            SnCall("LoadGame", [&] { return load(false); }, [&](void *) { return load(true); });
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            break;
        }
        default: {
            const S_vector dir(U(-1, 1), U(-1, 1), U(-1, 1));
            SnCall("Unknown114", [&] { return int(s->Unknown114(&dir)); },
                   [&](void *) { return int(OAi<uint32_t>(0x005a4510, &dir) & 0xFF); });
            break;
        }
        }
    }
}

void TestSniper()
{
    SetUpSenses();
    SetUpSensesB();
    SetUpCombatAi();
    g_humanAnswers = AsAnswers;
    g_visiblePartsOf = AsPartsOf;
    SetUpFireOrder();
    SetUpSniper();
    TestSniperA();
    g_humanAnswers = nullptr;
    g_visiblePartsOf = nullptr;
}
}  // namespace
