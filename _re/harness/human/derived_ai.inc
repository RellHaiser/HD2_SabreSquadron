// The derived AI controllers' common harness (parts 38-: the sniper, the scripted controller, ...; included by their
// test files, each its own build, after aicombat_test.inc, whose fakes and helpers it uses).
//
// Each test's port controller is the AI pair's port side (part 33's AiCall converts and compares its C_AIController
// part); the original's is its AI constructor's object (0x0056ee90) with a copy of its vtable. Here: the private members
// through explicit instantiation, the fakes both sides call, the events pair (the human's C_HumanEvents and the
// original's block: the sniper post, the pending events and the stimuli as MSVC6 deques, the nearby actors), the AI's
// control arrays, the walking points, the per-side vtables, and the call wrapper.
//
// Faked on both sides and logged: UpdateMove (ai_movement.cpp, step 4's; the original's 0x0057a750), the human's
// DrawLongGun / DrawSidearm (0x005a67c0 / 0x005a6830), ProcessEvents (0x005a8480), StartScriptedController
// (0x005af680), RequestThrowItem (0x005a2c50), the sight module's TestNow (0x0046f280), the checkpoints' GetNeighbours
// (0x00520830), GetActor / GetActorById (0x00475dd0 / 0x00475d50: the humans and part 23's actors by net id), the worn
// containers' IsLocked. ReleaseHumanEvent (0x005add20) is silent (the originals put events into their pool inline).
// IdToActor (human_events.cpp's) and CoverAgainst (enemy_combat.cpp's, inline in the original) are copies of the port's
// here until their files are linked.
#include "game/enemy_internal.h"
#include "script/script.h"

namespace {
// ---- the private members: through explicit instantiation (the access check does not apply to it)
template <class Tag, typename Tag::type M> struct S_DaRob {
    friend typename Tag::type DaGet(Tag) { return M; }
};
#define DA_ROB(NAME, TYPE, MEMBER) \
    struct NAME { \
        using type = TYPE; \
        friend type DaGet(NAME); \
    }; \
    template struct S_DaRob<NAME, MEMBER>;
DA_ROB(EvSniperPos, S_vector4 C_HumanEvents::*, &C_HumanEvents::m_sniperPos)
DA_ROB(EvSniperRadius, float C_HumanEvents::*, &C_HumanEvents::m_sniperRadius)
DA_ROB(EvPending, std::deque<S_HumanEvent *> C_HumanEvents::*, &C_HumanEvents::m_pending)
DA_ROB(EvStimuli, std::deque<S_HumanEvent *> C_HumanEvents::*, &C_HumanEvents::m_stimuli)
DA_ROB(EvNearby, std::vector<int> C_HumanEvents::*, &C_HumanEvents::m_nearbyActors)
DA_ROB(ScOnAlarmDone, int C_Script::*, &C_Script::m_onAlarmDone)
DA_ROB(ItTypeId, int C_Item::*, &C_Item::m_typeId)

struct S_DaScript {
    int move = 0;                                       // UpdateMove
    bool longGun = false;                               // DrawLongGun
    std::vector<uint16_t> neighbours;                   // GetNeighbours
    bool routeEnds = false;                             // UpdateMove ends the route (the move order's FLAG_ROUTE, +0x50)
} g_da;
bool g_daAppend = false;                                // (part 43: the network's answers appended, as the real ones)
}  // namespace

// ---- the port's side of the fakes
#ifndef HUMAN_AIMOVE   // (part 46 links game/ai_movement.cpp's)
int C_AIController::UpdateMove(int timeDelta)
{
    Log(this, "UpdateMove", {double(timeDelta)});
    if (g_da.routeEnds)
        m_move->flags &= ~S_AIMoveOrder::FLAG_ROUTE;
    return g_da.move;
}
#endif
bool C_Human::DrawLongGun()
{
    Log(this, "DrawLongGun");
    return g_da.longGun;
}
bool C_Human::DrawSidearm(int reason)
{
    Log(this, "DrawSidearm", {double(reason)});
    return true;
}
// (The sight tests' answer: part 45's OnFriendlyFire asks for both.)
bool g_daSight = true;
bool C_SightModule::TestNow(int observer, int target)
{
    Log(nullptr, "SightTestNow", {double(observer), double(target)});
    return g_daSight;
}
bool C_CheckpointNetwork::GetNeighbours(int from, float radius, std::vector<uint16_t> *out)
{
    Log(nullptr, "GetNeighbours", {double(from & 0xFFFF), radius});   // (a 16-bit point, as the original pushes it)
    if (g_daAppend)
        out->insert(out->end(), g_da.neighbours.begin(), g_da.neighbours.end());
    else
        *out = g_da.neighbours;
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
#ifndef HUMAN_ENEMY
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
#endif

namespace {
// The shared events the event lists hold.
const int DA_EVENTS = 8;
alignas(16) S_HumanEvent g_daEv[DA_EVENTS];
int DaEventIndex(const void *e)
{
    for (int k = 0; k < DA_EVENTS; ++k)
        if (e == &g_daEv[k])
            return k;
    return e ? 99 : -1;
}
}  // namespace
// (Silent: the originals put the events they drop into the pool inline; the lists are compared.)
void ReleaseHumanEvent(S_HumanEvent *) {}

namespace {
// ---- the original's side
#ifndef HUMAN_AIMOVE
int AC DA_UpdateMove(void *self, int timeDelta)
{
    Log(self, "UpdateMove", {double(timeDelta)});
    if (g_da.routeEnds) {
        uint8_t *move;
        std::memcpy(&move, static_cast<uint8_t *>(self) + 0x12C, 4);
        uint32_t flags;
        std::memcpy(&flags, move + 0x50, 4);
        flags &= ~uint32_t(S_AIMoveOrder::FLAG_ROUTE);
        std::memcpy(move + 0x50, &flags, 4);
    }
    return g_da.move;
}
#endif
int AC DA_DrawLongGun(void *self)
{
    Log(self, "DrawLongGun");
    return g_da.longGun;
}
int AC DA_DrawSidearm(void *self, int reason)
{
    Log(self, "DrawSidearm", {double(reason)});
    return 1;
}
int AC DA_TestNow(void *, int observer, int target)
{
    Log(nullptr, "SightTestNow", {double(observer), double(target)});
    return g_daSight;
}
// (Its vector: allocator, first, last, end; the original's new is the test's malloc.)
int AC DA_GetNeighbours(void *, int from, float radius, uint32_t *out)
{
    from &= 0xFFFF;   // (a 16-bit point: the original's push leaves the high word stale)
    Log(nullptr, "GetNeighbours", {double(from), radius});
    // (Part 43: appended to what the vector holds, as the network's flood fill does.)
    std::vector<uint16_t> all;
    if (g_daAppend && out[1]) {
        const auto *first = reinterpret_cast<const uint16_t *>(static_cast<uintptr_t>(out[1]));
        all.assign(first, first + (out[2] - out[1]) / 2);
    }
    all.insert(all.end(), g_da.neighbours.begin(), g_da.neighbours.end());
    const size_t n = all.size();
    out[1] = out[2] = out[3] = 0;
    if (n) {
        auto *data = static_cast<uint16_t *>(std::malloc(n * 2));
        std::memcpy(data, all.data(), n * 2);
        out[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        out[2] = out[3] = out[1] + 2 * static_cast<uint32_t>(n);
    }
    return 1;
}
void __stdcall DA_ReleaseHumanEvent(void *) {}   // (__stdcall: RET 4)
void AC DA_ProcessEvents(void *self) { Log(self, "ProcessEvents"); }
void AC DA_StartScriptedController(void *self) { Log(self, "StartScriptedController"); }
void AC DA_RequestThrowItem(void *self, void *item, int arg, const S_vector *dir)
{
    Log(self, ("RequestThrowItem " + NameOf(item)).c_str(), {double(arg), dir->x, dir->y, dir->z});
}
// The walking points (Get's answers on both sides: the original pushes a 16-bit index), part 33's at the network's
// stride.
S_Checkpoint *DaPointAt(int index) { return index < 8 ? reinterpret_cast<S_Checkpoint *>(g_aiPoints[index]) : nullptr; }
// The vtables each side calls through: the weapon component's (part 36's) and the definitions' slot 0x10 (the port's
// GetCarryMode, the original's GetBehaviour: parts 23 and 35 gave it one each).
void DaVtablesFor(bool orig)
{
    CompVtableFor(orig);
    g_defVt[0x10 / 4] = orig ? reinterpret_cast<void *>(DF_GetBehaviour) : reinterpret_cast<void *>(DF_GetCarryMode);
}
// The AI's control arrays: the port's (ai_controller.cpp) and the original's globals.
float *OrigAiAxes() { return OriginalAt<float *>(0x008ab058); }
float *OrigAiButtons() { return OriginalAt<float *>(0x008aaf58); }
// The actors by net id (GetActor's answers on both sides): the humans, part 23's actors; one id now and then gone.
int g_daGoneId = -1000;
C_Actor *DaActorById(int id, bool original)
{
    if (id == g_daGoneId)
        return nullptr;
    std::vector<C_Actor *> all = {g_h.human, PortOther()};
    for (int k = 0; k < ACTOR_KIND_COUNT; ++k)
        all.push_back(static_cast<C_Actor *>(PortActor(k)));
    for (C_Actor *a : all)
        if (a && a->m_netId == id)
            return original ? static_cast<C_Actor *>(ToOrig(a)) : a;
    return nullptr;
}
void *AC DA_GetActor(void *self, int index)
{
    if (!LookedUpBefore(index))
        Log(self, "GetActor", {double(index)});
    return DaActorById(index, true);
}
void *AC DA_GetActorById(void *self, int id)
{
    Log(self, "GetActorById", {double(id)});
    return DaActorById(id, true);
}
// The worn containers' IsLocked (part 21's shared vtable: the original's slot 0x48 and the port's).
bool g_daHandsLocked = false;
int AC DA_IsLocked(void *self)
{
    Log(self, "ContainerIsLocked");
    return g_daHandsLocked;
}

#include "msvc_containers.inc"

// ---- the events pair, the script, the frame's data
C_HumanEvents g_daEvents;
alignas(16) uint8_t g_daOrigEvents[0x100];
alignas(16) uint8_t g_daScriptBytes[0x400];
S_FrameData g_daFrameData;
std::vector<uint32_t> DaWords(const std::deque<S_HumanEvent *> &events)
{
    std::vector<uint32_t> words;
    for (S_HumanEvent *ev : events)
        words.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ev)));
    return words;
}
// The events (the post, the pending events, the stimuli, the nearby actors' ids) and the control arrays in the
// original's layout.
void DaEventsToOrig()
{
    C_HumanEvents &e = g_daEvents;
    std::memset(g_daOrigEvents, 0, sizeof g_daOrigEvents);
    std::memcpy(g_daOrigEvents, &(e.*DaGet(EvSniperPos())), 16);
    std::memcpy(g_daOrigEvents + 0x10, &(e.*DaGet(EvSniperRadius())), 4);
    MsvcDequePut(g_daOrigEvents + 0x18, DaWords(e.*DaGet(EvPending())));
    MsvcDequePut(g_daOrigEvents + 0x48, DaWords(e.*DaGet(EvStimuli())));
    const std::vector<int> &nearby = e.*DaGet(EvNearby());
    MsvcVectorPut(g_daOrigEvents + 0x78, std::vector<uint32_t>(nearby.begin(), nearby.end()));
    std::memcpy(OrigAiAxes(), g_aiAxes, sizeof g_aiAxes);
    std::memcpy(OrigAiButtons(), g_aiButtons, sizeof g_aiButtons);
}
std::string DaEventsDiffer()
{
    C_HumanEvents &e = g_daEvents;
    if (MsvcDequeGet(g_daOrigEvents + 0x18) != DaWords(e.*DaGet(EvPending())))
        return "pending events";
    if (MsvcDequeGet(g_daOrigEvents + 0x48) != DaWords(e.*DaGet(EvStimuli())))
        return "stimuli";
    if (std::memcmp(OrigAiAxes(), g_aiAxes, sizeof g_aiAxes) != 0)
        return "aiAxes";
    if (std::memcmp(OrigAiButtons(), g_aiButtons, sizeof g_aiButtons) != 0)
        return "aiButtons";
    return "";
}
// A call on the pair: the test's fields to the original's before (`toOrig`), the vtables each side's, the stale w words
// the original hands the facing taken from the port (part 36's), `after` on the original's side (more of them), the
// test's fields compared after (`differs`).
template <class T, class D, class A, class P, class O>
void DaCall(const char *what, T toOrig, D differs, A after, P port, O original)
{
    toOrig();
    DaEventsToOrig();
    AiCall(what,
           [&] {
               DaVtablesFor(false);
               return port();
           },
           [&](void *o) {
               DaVtablesFor(true);
               const int r = original(o);
               DaVtablesFor(false);
               FacingWFromPort();
               after();
               return r;
           });
    std::string why = differs();
    if (why.empty())
        why = DaEventsDiffer();
    CHECK(why.empty());
    static std::map<std::string, int> shown;
    if (!why.empty() && shown[what]++ < 3)
        std::printf("  %s: %s\n", what, why.c_str());
}
// A pointer as the original's (the port's mapped), for return values.
int DaWord(const void *p, bool orig)
{
    return static_cast<int>(reinterpret_cast<uintptr_t>(orig || !p ? p : ToOrig(p)));
}

// The original's controller for `type` with a copy of `vtable` (ExecuteCommand logged), named and mapped with its parts.
uint8_t *DaOrigController(C_AIController *port, size_t size, int type, uint32_t vtable, void **vtCopy, const char *name)
{
    auto *o = static_cast<uint8_t *>(OC_AlignedNew(size, 16));
    typedef void(AC * T_Ctor)(void *, int);
    OriginalAt<T_Ctor>(0x0056ee90)(o, type);
    std::memcpy(vtCopy, OriginalAt<void *>(vtable), 0x48 * sizeof(void *));
    vtCopy[0xDC / 4] = reinterpret_cast<void *>(OA_ExecuteCommand);
    std::memcpy(o, &vtCopy, 4);
    g_names[port] = g_names[o] = name;
    g_extraPointers.push_back({port, o});
    const char *const parts[] = {"Facing", "FireOrder", "Move", "Driver", "Alert"};
    void *const portParts[] = {port->m_facing, port->m_fireOrder, port->m_move, port->m_driver, port->m_alert};
    for (int k = 0; k < 5; ++k) {
        void *op;
        std::memcpy(&op, o + 0x124 + 4 * k, 4);
        g_names[portParts[k]] = g_names[op] = std::string(name) + parts[k];
        g_extraPointers.push_back({portParts[k], op});
    }
    // The AI pair is this one from here on.
    g_pa = reinterpret_cast<S_TestAi *>(port);
    g_oa = o;
    return o;
}

void SetUpDerivedAi()
{
    const std::pair<uint32_t, const void *> redirects[] = {
#ifndef HUMAN_AIMOVE
        {0x0057a750, FAKE(DA_UpdateMove)},
#endif
        {0x005a67c0, FAKE(DA_DrawLongGun)},
        {0x005a6830, FAKE(DA_DrawSidearm)},
        {0x0046f280, FAKE(DA_TestNow)},
        {0x00520830, FAKE(DA_GetNeighbours)},
        {0x005add20, FAKE(DA_ReleaseHumanEvent)},
        {0x005a8480, FAKE(DA_ProcessEvents)},
        {0x005af680, FAKE(DA_StartScriptedController)},
        {0x005a2c50, FAKE(DA_RequestThrowItem)},
        {0x00475dd0, FAKE(DA_GetActor)},
        {0x00475d50, FAKE(DA_GetActorById)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    g_checkpointAt = DaPointAt;
    // (The definitions' slot the sides share: DaVtablesFor's.)
    if (VSlot(&C_ItemDef::GetCarryMode) != 0x10 / 4) {
        std::printf("derived ai: the definitions' slots moved\n");
        std::exit(3);
    }
    g_actorByIndex = DaActorById;
    g_actorById = DaActorById;
    g_wornVt[0x48 / 4] = reinterpret_cast<void *>(DA_IsLocked);
    g_wornVt[VSlot(&C_ItemContainer::IsLocked)] = reinterpret_cast<void *>(DA_IsLocked);
    g_names[&g_daEvents] = g_names[g_daOrigEvents] = "events";
    g_extraPointers.push_back({&g_daEvents, g_daOrigEvents});
    for (int k = 0; k < DA_EVENTS; ++k)
        g_names[&g_daEv[k]] = "event" + std::to_string(k);
    g_names[g_daScriptBytes] = "script";
    g_names[g_aiAxes] = g_names[OrigAiAxes()] = "aiAxes";
    g_names[g_aiButtons] = g_names[OrigAiButtons()] = "aiButtons";
    g_extraPointers.push_back({g_aiAxes, OrigAiAxes()});
    g_extraPointers.push_back({g_aiButtons, OrigAiButtons()});
}

// The next task of `type` of each side's pool alike (a new task keeps its pool object's +0x0C and +0x10 on both sides:
// the pools' objects differ): one taken, filled alike, put back (both pools give back the last one put).
void DaPrimeTaskPool(int type)
{
    const int stale0C = AiInt(), stale10 = AiInt();
    S_AITask *t = AllocTask(type);
    t->unknown0C = stale0C;
    t->unknown10 = stale10;
    ReleaseTask(t);
    uint8_t *o = OrigTask(type);
    std::memcpy(o + 0x0C, &stale0C, 4);
    std::memcpy(o + 0x10, &stale10, 4);
    typedef void(AC * T_Release)(void *, void *);
    OriginalAt<T_Release>(0x00582c50)(AiPools(), o);
}

// The walking network: part 33's points at its own stride (Get and GetIndex agree), about the human; the game's and
// the controller's network (the original's game's +0x54).
void DaRandomPoints(C_AIController *c)
{
    const uint16_t count = 8;
    void *table = g_aiPoints;
    std::memcpy(g_checkpointNet, &count, 2);
    std::memcpy(g_checkpointNet + 4, &table, 4);
    c->m_checkpoints = reinterpret_cast<C_Checkpoints *>(g_checkpointNet);
    GetGame()->m_checkpoints = reinterpret_cast<C_Checkpoints *>(g_checkpointNet);
    void *net = g_checkpointNet;
    std::memcpy(g_origGame + 0x54, &net, 4);
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
    c->m_currentCheckpoint = P(0.2f) ? nullptr : reinterpret_cast<S_Checkpoint *>(g_aiPoints[I(0, 7)]);
}
}  // namespace
