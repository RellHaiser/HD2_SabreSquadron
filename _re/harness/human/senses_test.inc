// Part 24 of the human harness (HUMAN_SENSES builds, build_senses.bat; included by human_test.cpp after
// control_test.inc): the human's sight and hearing (game/human_senses.cpp) and the events its AI is told about
// (game/human_events.cpp) against the original's, on part 22's harness (its fakes of these left out: this build links
// the real ones).
//
// The containers the original keeps in MSVC6 form are rebuilt for it before each of its calls and read back after:
// the human's seen lists and remembered sounds (vectors), the events' two deques (blocks of 1024 pointers through a
// map; the first element anywhere in its block) and two vectors. The events themselves are shared (the same layout on
// both sides): their bytes are put back before the original's call and compared after it. The event pools are each
// side's own: what a call released is read from them afterwards (the port's drained down to a sentinel it was given
// first, the original's list emptied first and read).
//
// The modules the code talks to are fakes, their calls logged on both sides: the sight module's targets, CanSee and
// states, the sound module's listeners, the controller's virtuals, the body, the actors module, the team, the
// scripted controller, the remembered-sound pool, timeGetTime; IsFriendOf, IsDisguisedEnemy and UpdateDisguise
// (part 21's, tested there) are scripted (the port's through the linker's --wrap).
//
// Part 24a: the seen lists, sight events, sounds, listening, proximity, the event queue, freezing (human_senses.cpp).
// 24b: the events (human_events.cpp).
#include "game/human_internal.h"
#include "game/melee_tracker.h"
#include "game/sight.h"
#include "game/sound_table.h"
#include "game/world.h"

namespace {

// ---- what the fakes answer
struct S_SensesScript {
    uint32_t time = 100000;                             // timeGetTime
    bool visible = false;                               // CanSee's visible
    bool writeVisible = true;                           // ... written or not
    int canSee = 0;
    uint32_t seatFlags = 0;                             // the vehicle's GetSeatFlags
    int handled = 0;                                    // HandleEvent: 0 the event, 1 none, 2 another one
    bool friendOf[8] = {};                              // IsFriendOf by actor (ActorIndex)
    bool disguised[8] = {};                             // IsDisguisedEnemy
} g_sn;

// ---- the actors: three other humans, a car, a meleeTracker (each side its own), the test human (index 5)
const int OTHERS = 3, ACTOR_CAR_INDEX = 3, ACTOR_MELEE_INDEX = 4, ACTOR_SELF_INDEX = 5;
alignas(16) uint8_t g_otherPort[OTHERS][sizeof(C_Human) + 0x100], g_otherOrig[OTHERS][0x1000];
alignas(16) uint8_t g_carPort[0x400], g_carOrig[0x400], g_meleePort[sizeof(C_MeleeTracker) + 0x100], g_meleeOrig[0x800];
alignas(16) uint8_t g_otherCtlOrig[0x100];             // the other humans' controller (the original's reads its flags)
void *g_carPortVt[128], *g_carOrigVt[128];
I3D_frame *g_actorFrame[5];
C_Human *OtherPort(int k) { return reinterpret_cast<C_Human *>(g_otherPort[k]); }
void *ActorPort(int k)
{
    return k < OTHERS ? static_cast<void *>(g_otherPort[k]) : k == ACTOR_CAR_INDEX ? static_cast<void *>(g_carPort)
         : k == ACTOR_MELEE_INDEX ? static_cast<void *>(g_meleePort) : static_cast<void *>(g_h.human);
}
void *ActorOrig(int k)
{
    return k < OTHERS ? static_cast<void *>(g_otherOrig[k]) : k == ACTOR_CAR_INDEX ? static_cast<void *>(g_carOrig)
         : k == ACTOR_MELEE_INDEX ? static_cast<void *>(g_meleeOrig) : static_cast<void *>(o_human);
}
// 0..5 for either side's actor, -1 for anything else.
int ActorIndex(const void *p)
{
    for (int k = 0; k <= ACTOR_SELF_INDEX; ++k)
        if (p && (p == ActorPort(k) || p == ActorOrig(k)))
            return k;
    return -1;
}
int ActorNetId(int k) { return k == ACTOR_SELF_INDEX ? 7 : 20 + k; }
// The actors module's answers by id (each side its own actor).
C_Actor *SensesActorById(int id, bool original)
{
    for (int k = 0; k <= ACTOR_SELF_INDEX; ++k)
        if (id == ActorNetId(k))
            return static_cast<C_Actor *>(original ? ActorOrig(k) : ActorPort(k));
    return nullptr;
}

// ---- the sight module's targets (each side its own objects and vtable; the same kinds and actors)
const int TARGETS = 8;
alignas(16) uint8_t g_targetPort[TARGETS][0x40], g_targetOrig[TARGETS][0x40];
void *g_targetPortVt[16], *g_targetOrigVt[16];
int g_targetKind[TARGETS], g_targetActor[TARGETS];      // kind 0 human, 1 meleeTracker, 2 something else
uint32_t g_targetMask[TARGETS];
std::vector<C_SightTarget *> g_portTargets;
void *g_origTargets[TARGETS];
int TargetIndex(const void *self, bool &original)
{
    for (int k = 0; k < TARGETS; ++k) {
        if (self == g_targetPort[k] || self == g_targetOrig[k]) {
            original = self == g_targetOrig[k];
            return k;
        }
    }
    std::printf("unknown sight target\n");
    std::exit(3);
}
void *CT_ TG_GetActor(void *self)
{
    bool original;
    const int k = TargetIndex(self, original);
    Log(self, "TargetGetActor");
    return original ? ActorOrig(g_targetActor[k]) : ActorPort(g_targetActor[k]);
}
int CT_ TG_GetKind(void *self)
{
    bool original;
    const int k = TargetIndex(self, original);
    Log(self, "TargetGetKind");
    return g_targetKind[k];
}

// ---- the remembered sounds (each side its own objects: their source is an actor of that side)
const int SOUNDS = 12;
alignas(16) uint8_t g_soundPort[SOUNDS][0x20], g_soundOrig[SOUNDS][0x20];
int g_soundNext = 0;                                    // the pool's next one (AllocRememberedSound)
S_RememberedSound *SoundPort(int k) { return reinterpret_cast<S_RememberedSound *>(g_soundPort[k]); }
void *MapSoundSource(const void *source)
{
    const int k = ActorIndex(source);
    return k >= 0 ? ActorOrig(k) : const_cast<void *>(source);
}
void LogNewSound(const char *what, int chapter, const S_vector4 *pos, const void *source, uint32_t time)
{
    Log(nullptr, (std::string(what) + " " + NameOf(source)).c_str(), {double(chapter), pos->x, pos->y, pos->z, pos->w, double(time)});
}
void *CT_ SN_AllocSound(void *, int chapter, const S_vector4 *pos, void *source, uint32_t time)
{
    LogNewSound("AllocRememberedSound", chapter, pos, source, time);
    uint8_t *s = g_soundOrig[g_soundNext++ % SOUNDS];
    std::memcpy(s, pos, 16);
    std::memcpy(s + 0x10, &chapter, 4);
    std::memcpy(s + 0x14, &source, 4);
    std::memcpy(s + 0x18, &time, 4);
    s[0x1C] = 0;
    return s;
}

// ---- each side's events object (the port's real one, the original's MSVC6 layout), the shared events, the sentinel
alignas(16) uint8_t g_portEventsBytes[sizeof(C_HumanEvents) + 0x40], g_origEvents[0x100];
C_HumanEvents *g_portEvents;
const int EVENTS = 16;
alignas(16) S_HumanEvent g_ev[EVENTS + 1];             // (the last: HandleEvent's other event)
alignas(16) S_HumanEvent g_sentinel;
S_HumanEvent *OtherEvent() { return &g_ev[EVENTS]; }

// ---- MSVC6 containers in the original's copies
// A vector (allocator word, first, last, end) at `at` from `items` (a buffer of its own: the original may grow and
// free it).
void MsvcVectorPut(uint8_t *at, const std::vector<uint32_t> &items, size_t spare = 0)
{
    uint32_t *buffer = nullptr;
    if (!items.empty() || spare) {
        buffer = static_cast<uint32_t *>(std::malloc((items.size() + spare) * 4));
        for (size_t i = 0; i < items.size(); ++i)
            buffer[i] = items[i];
    }
    void **w = reinterpret_cast<void **>(at);
    w[0] = nullptr;
    w[1] = buffer;
    w[2] = buffer + items.size();
    w[3] = buffer + items.size() + spare;
}
std::vector<uint32_t> MsvcVectorGet(const uint8_t *at)
{
    const auto *w = reinterpret_cast<uint32_t *const *>(at);
    if (!w[1])
        return {};
    return std::vector<uint32_t>(w[1], w[2]);
}
// A deque of pointers (0x30 bytes: allocator; the first iterator: block, block end, element, map cell; the last
// iterator; map; map size; size): blocks of 1024. Empty: all zero (a new deque). The first element anywhere in its
// block, sometimes near its end; the last iterator never at a block's end (the original's invariant: a push that fills
// a block adds the next one).
const int DEQUE_BLOCK = 1024;
void MsvcDequePut(uint8_t *at, const std::vector<uint32_t> &items)
{
    std::memset(at, 0, 0x30);
    if (items.empty())
        return;
    const int start = P(0.3f) ? DEQUE_BLOCK - I(1, 4) : I(0, DEQUE_BLOCK - 1);
    const size_t end = start + items.size();
    const size_t blocks = end / DEQUE_BLOCK + 1;
    const size_t first = size_t(I(0, 2));
    const size_t mapSize = first + blocks + size_t(I(0, 2));
    auto **map = static_cast<uint32_t **>(std::calloc(mapSize, 4));
    for (size_t b = 0; b < blocks; ++b)
        map[first + b] = static_cast<uint32_t *>(std::malloc(DEQUE_BLOCK * 4));
    for (size_t i = 0; i < items.size(); ++i) {
        const size_t pos = start + i;
        map[first + pos / DEQUE_BLOCK][pos % DEQUE_BLOCK] = items[i];
    }
    uint32_t *w = reinterpret_cast<uint32_t *>(at);
    const auto put = [&](int word, size_t pos) {
        uint32_t *block = map[first + pos / DEQUE_BLOCK];
        w[word] = uint32_t(uintptr_t(block));
        w[word + 1] = uint32_t(uintptr_t(block + DEQUE_BLOCK));
        w[word + 2] = uint32_t(uintptr_t(block + pos % DEQUE_BLOCK));
        w[word + 3] = uint32_t(uintptr_t(&map[first + pos / DEQUE_BLOCK]));
    };
    put(1, size_t(start));
    put(5, end);
    w[9] = uint32_t(uintptr_t(map));
    w[10] = uint32_t(mapSize);
    w[11] = uint32_t(items.size());
}
std::vector<uint32_t> MsvcDequeGet(const uint8_t *at)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(at);
    std::vector<uint32_t> items;
    const uint32_t size = w[11];
    if (!size)
        return items;
    const auto *cur = reinterpret_cast<const uint32_t *>(uintptr_t(w[3]));
    const auto *blockEnd = reinterpret_cast<const uint32_t *>(uintptr_t(w[2]));
    auto *const *cell = reinterpret_cast<uint32_t *const *>(uintptr_t(w[4]));
    for (uint32_t i = 0; i < size; ++i) {
        if (cur == blockEnd) {
            ++cell;
            cur = *cell;
            blockEnd = cur + DEQUE_BLOCK;
        }
        items.push_back(*cur++);
    }
    // (The last iterator where the size says.)
    if (cur == blockEnd) {
        ++cell;
        cur = *cell;
    }
    if (uint32_t(uintptr_t(cur)) != w[7])
        items.push_back(0xDEADDEAD);
    return items;
}
uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }
// A shared event's index (-1 none, 99 another).
int EventIndex(const S_HumanEvent *e)
{
    if (!e)
        return -1;
    for (int k = 0; k <= EVENTS; ++k)
        if (e == &g_ev[k])
            return k;
    return 99;
}

// ---- the port's side: the fakes (the original's below)
}  // namespace

// The sight module (game/sight.cpp is not linked): its targets as the port's GetTarget looks them up; CanSee scripted.
C_SightTarget *C_SightModule::GetTarget(int id) const
{
    if (id < 0 || static_cast<size_t>(id) >= g_portTargets.size())
        return nullptr;
    C_SightTarget *target = g_portTargets[id];
    return target && target->mask != 0 ? target : nullptr;
}
int C_SightModule::CanSee(int observer, int target, int *before, bool *visible)
{
    Log(nullptr, "CanSee", {double(observer), double(target), double(before != nullptr), double(visible != nullptr)});
    // (Not always written: what the caller starts it with shows.)
    if (visible && g_sn.writeVisible)
        *visible = g_sn.visible;
    return g_sn.canSee;
}
void C_ActorsModule::SetActorActive(C_Actor *actor, bool active)
{
    Log(this, ("SetActorActive " + NameOf(actor)).c_str(), {double(active)});
}
void S_HumanBody::Freeze(bool on) { Log(nullptr, "BodyFreeze", {double(on)}); }
void C_Team::AddSeenMeleeTracker(C_MeleeTracker *meleeTracker) { Log(this, ("AddSeenMeleeTracker " + NameOf(meleeTracker)).c_str()); }
void C_ScriptedController::Suspend(bool on) { Log(this, "ScSuspend", {double(on)}); }
S_RememberedSound *AllocRememberedSound(int chapter, const S_vector4 &pos, C_Actor *source, uint32_t time)
{
    LogNewSound("AllocRememberedSound", chapter, &pos, source, time);
    S_RememberedSound *s = SoundPort(g_soundNext++ % SOUNDS);
    s->pos = S_vector(pos.x, pos.y, pos.z);
    s->posW = pos.w;
    s->chapter = chapter;
    s->source = source;
    s->time = time;
    s->refreshed = false;
    return s;
}
// timeGetTime (the port's import slot, this build links no winmm).
extern "C" DWORD __stdcall FakeTimeGetTime()
{
    Log(nullptr, "timeGetTime");
    return g_sn.time;
}
extern "C" {
void *g_impTimeGetTime asm("__imp__timeGetTime@0") = reinterpret_cast<void *>(FakeTimeGetTime);
// Part 21's, scripted (the linker's --wrap: human_senses.cpp's calls come here).
bool CT_ __wrap__ZNK7C_Human10IsFriendOfEPK7C_Actor(const C_Human *self, const C_Actor *other)
{
    Log(self, ("IsFriendOf " + NameOf(other)).c_str());
    const int k = ActorIndex(other);
    return k >= 0 && g_sn.friendOf[k];
}
bool CT_ __wrap__ZNK7C_Human16IsDisguisedEnemyEPKS_(const C_Human *self, const C_Human *other)
{
    Log(self, ("IsDisguisedEnemy " + NameOf(other)).c_str());
    const int k = ActorIndex(other);
    return k >= 0 && g_sn.disguised[k];
}
void CT_ __wrap__ZN7C_Human14UpdateDisguiseEf(C_Human *self, float amount) { Log(self, "UpdateDisguise", {amount}); }
}

namespace {

// ---- the original's side
uint32_t CT_ SN_IsFriendOf(void *self, void *other)
{
    Log(self, ("IsFriendOf " + NameOf(other)).c_str());
    const int k = ActorIndex(other);
    return k >= 0 && g_sn.friendOf[k];
}
uint32_t CT_ SN_IsDisguisedEnemy(void *self, void *other)
{
    Log(self, ("IsDisguisedEnemy " + NameOf(other)).c_str());
    const int k = ActorIndex(other);
    return k >= 0 && g_sn.disguised[k];
}
void CT_ SN_UpdateDisguise(void *self, float amount) { Log(self, "UpdateDisguise", {amount}); }
int CT_ SN_CanSee(void *, int observer, int target, int *before, bool *visible)
{
    Log(nullptr, "CanSee", {double(observer), double(target), double(before != nullptr), double(visible != nullptr)});
    // (Not always written: what the caller starts it with shows.)
    if (visible && g_sn.writeVisible)
        *visible = g_sn.visible;
    return g_sn.canSee;
}
void CT_ SN_SetActorActive(void *self, void *actor, int active)
{
    Log(self, ("SetActorActive " + NameOf(actor)).c_str(), {double(B(active))});
}
void CT_ SN_BodyFreeze(void *, int on) { Log(nullptr, "BodyFreeze", {double(B(on))}); }
void CT_ SN_AddSeenMeleeTracker(void *self, void *meleeTracker) { Log(self, ("AddSeenMeleeTracker " + NameOf(meleeTracker)).c_str()); }
void CT_ SN_ScSuspend(void *self, int on) { Log(self, "ScSuspend", {double(B(on))}); }
DWORD __stdcall SN_TimeGetTime()
{
    Log(nullptr, "timeGetTime");
    return g_sn.time;
}
void *CT_ SN_GetActor(void *self, int index)
{
    Log(self, "GetActor", {double(index)});
    return SensesActorById(index, true);
}
void *CT_ SN_GetActorById(void *self, int id)
{
    Log(self, "GetActorById", {double(id)});
    return SensesActorById(id, true);
}
C_Actor *SensesActorByIndex(int index, bool original) { return SensesActorById(index, original); }

// The controller's virtuals the senses call (both sides).
#define SV_ACTOR(name)                                                                                                 \
    void CT_ SV_##name(void *self, void *actor) { Log(self, (#name " " + NameOf(actor)).c_str()); }
SV_ACTOR(OnEnemySeen)
SV_ACTOR(OnEnemyLost)
SV_ACTOR(OnBodySeen)
SV_ACTOR(OnBodyHeard)
SV_ACTOR(OnFriendHeard)
SV_ACTOR(OnLoudSound)
void CT_ SV_OnActorNear(void *self, void *actor, float distance) { Log(self, ("OnActorNear " + NameOf(actor)).c_str(), {distance}); }
void CT_ SV_OnActorGone(void *self, void *actor, float distance) { Log(self, ("OnActorGone " + NameOf(actor)).c_str(), {distance}); }
void CT_ SV_OnSoundHeard(void *self, const uint8_t *sound)
{
    float f[4];
    std::memcpy(f, sound, 16);
    int chapter;
    uint32_t time;
    void *source;
    std::memcpy(&chapter, sound + 0x10, 4);
    std::memcpy(&source, sound + 0x14, 4);
    std::memcpy(&time, sound + 0x18, 4);
    Log(self, ("OnSoundHeard " + NameOf(sound) + " " + NameOf(source)).c_str(),
        {f[0], f[1], f[2], f[3], double(chapter), double(time), double(sound[0x1C])});
}
void CT_ SV_Freeze(void *self, int on) { Log(self, "ControllerFreeze", {double(B(on))}); }
S_HumanEvent *CT_ SV_HandleEvent(void *self, S_HumanEvent *event)
{
    Log(self, ("HandleEvent " + NameOf(event)).c_str());
    return g_sn.handled == 0 ? event : g_sn.handled == 1 ? nullptr : OtherEvent();
}
// The car's GetSeatFlags (each side's vtable).
uint32_t CT_ SV_GetSeatFlags(void *self, int seat)
{
    Log(self, "GetSeatFlags", {double(seat)});
    return g_sn.seatFlags;
}

template <class R, class... A> R OCall(uint32_t at, void *self, A... a)
{
    typedef R(CT_ * T)(void *, A...);
    return OriginalAt<T>(at)(self, a...);
}

const uint32_t O_SOUND_POOL_HEAD = 0x008ab1a4, O_SOUND_POOL_COUNT = 0x008ab1b0;
const uint32_t O_EVENT_POOL_HEAD = 0x008ac434, O_EVENT_POOL_COUNT = 0x008ac440;
const uint32_t O_TIME_GET_TIME_SLOT = 0x0080f3f8;
const uint32_t O_SIGHT_TARGETS = 0x0089b294;           // the sight module's target vector (allocator, first, last, end)

void SetUpSenses()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x005a0ab0, FAKE(SN_IsFriendOf)},
        {0x005a0a50, FAKE(SN_IsDisguisedEnemy)},
        {0x005a60d0, FAKE(SN_UpdateDisguise)},
        {0x0046f320, FAKE(SN_CanSee)},
        {0x00475ec0, FAKE(SN_SetActorActive)},
        {0x005ac170, FAKE(SN_BodyFreeze)},
        {0x0045d4a0, FAKE(SN_AddSeenMeleeTracker)},
        {0x0058faa0, FAKE(SN_ScSuspend)},
        {0x00582740, FAKE(SN_AllocSound)},
        {0x00475dd0, FAKE(SN_GetActor)},
        {0x00475d50, FAKE(SN_GetActorById)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // The functions tested: the real ones on both sides (earlier parts redirected some).
    for (uint32_t at : {0x005a4840u, 0x005a4a10u, 0x005a4be0u, 0x005a4da0u, 0x005a4f60u, 0x005a4fc0u, 0x005a5020u,
                        0x005a5e50u, 0x005a5250u, 0x005a50a0u, 0x005a5380u, 0x005a5430u, 0x005a7f50u, 0x005a7fa0u,
                        0x005a83f0u, 0x005a8480u, 0x005a8b30u, 0x005a5d50u, 0x005af570u, 0x005ae450u, 0x005ad040u,
                        0x005ad390u, 0x005ad700u, 0x005ae640u, 0x005ae810u, 0x005ae230u, 0x005ae890u, 0x005ae950u,
                        0x005adac0u, 0x005adc50u, 0x005adee0u, 0x005ae310u, 0x005ae270u, 0x005ae610u, 0x005aca70u,
                        0x005accd0u, 0x005add20u, 0x005aca30u, 0x005aca50u, 0x005a5fe0u})
        RestoreOriginal(at);
    {
        DWORD old;
        VirtualProtect(OriginalAt<void *>(O_TIME_GET_TIME_SLOT), 4, PAGE_READWRITE, &old);
        *OriginalAt<void **>(O_TIME_GET_TIME_SLOT) = reinterpret_cast<void *>(SN_TimeGetTime);
    }
    // (The original's event pool as its static initialiser leaves it: blocks of 50.)
    *OriginalAt<int *>(0x008ac43c) = 50;
    g_actorById = SensesActorById;
    g_actorByIndex = SensesActorByIndex;
    g_names[&g_actorsModule] = g_names[OriginalAt<void *>(0x0089b4b0)] = "actors";

    // The controller's slots (part 22's wrappers' controllers).
    void **pv = g_cPortVt, **ov = g_cOrigVt;
    SetSlot(pv, ov, &C_HumanController::OnActorNear, 0x3C, FAKE(SV_OnActorNear));
    SetSlot(pv, ov, &C_HumanController::OnActorGone, 0x40, FAKE(SV_OnActorGone));
    SetSlot(pv, ov, &C_HumanController::OnEnemySeen, 0x98, FAKE(SV_OnEnemySeen));
    SetSlot(pv, ov, &C_HumanController::OnEnemyLost, 0x9C, FAKE(SV_OnEnemyLost));
    SetSlot(pv, ov, &C_HumanController::OnBodySeen, 0xA0, FAKE(SV_OnBodySeen));
    SetSlot(pv, ov, &C_HumanController::OnBodyHeard, 0xA8, FAKE(SV_OnBodyHeard));
    SetSlot(pv, ov, &C_HumanController::OnFriendHeard, 0xAC, FAKE(SV_OnFriendHeard));
    SetSlot(pv, ov, &C_HumanController::OnLoudSound, 0xB0, FAKE(SV_OnLoudSound));
    SetSlot(pv, ov, &C_HumanController::OnSoundHeard, 0xB4, FAKE(SV_OnSoundHeard));
    SetSlot(pv, ov, &C_HumanController::Freeze, 0xF0, FAKE(SV_Freeze));
    SetSlot(pv, ov, &C_HumanController::HandleEvent, 0xF4, FAKE(SV_HandleEvent));

    // The actors.
    for (int i = 0; i < 128; ++i)
        g_carPortVt[i] = g_carOrigVt[i] = reinterpret_cast<void *>(CT_Unexpected);
    g_carPortVt[VSlot(&C_Actor::GetSeatFlags)] = reinterpret_cast<void *>(SV_GetSeatFlags);
    g_carOrigVt[0x8C / 4] = reinterpret_cast<void *>(SV_GetSeatFlags);
    S_quat none;
    none.Identity();
    for (int k = 0; k < 5; ++k) {
        char name[16];
        std::snprintf(name, sizeof name, "actor%d", k);
        g_actorFrame[k] = NewFrame(FRAME_DUMMY, name, S_vector(20, 1, 20), none, Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_sector);
        g_names[ActorPort(k)] = g_names[ActorOrig(k)] = k < OTHERS ? "other" + std::to_string(k) : k == ACTOR_CAR_INDEX ? "car" : "meleeTracker";
        g_extraPointers.push_back({ActorPort(k), ActorOrig(k)});
    }
    for (uint8_t *b : {g_carPort, g_meleePort})
        *reinterpret_cast<void ***>(b) = g_carPortVt;
    for (uint8_t *b : {g_carOrig, g_meleeOrig})
        *reinterpret_cast<void ***>(b) = g_carOrigVt;
    reinterpret_cast<C_Actor *>(g_carPort)->m_type = ACTOR_CAR;
    const int car = ACTOR_CAR;
    std::memcpy(g_carOrig + 0x18, &car, 4);
    reinterpret_cast<C_Actor *>(g_meleePort)->m_type = ACTOR_MELEE_TRACKER;
    const int melee = ACTOR_MELEE_TRACKER;
    std::memcpy(g_meleeOrig + 0x18, &melee, 4);
    for (int k = 0; k < 5; ++k) {
        C_Actor *a = static_cast<C_Actor *>(ActorPort(k));
        a->SetNetId(ActorNetId(k));
        a->m_frame = g_actorFrame[k];
        const int id = ActorNetId(k);
        std::memcpy(static_cast<uint8_t *>(ActorOrig(k)) + 0x10, &id, 4);
        std::memcpy(static_cast<uint8_t *>(ActorOrig(k)) + 0x20, &g_actorFrame[k], 4);
    }
    for (int k = 0; k < OTHERS; ++k) {
        OtherPort(k)->m_type = ACTOR_HUMAN;
        const int human = ACTOR_HUMAN;
        std::memcpy(g_otherOrig[k] + 0x18, &human, 4);
        // (Their controller: no move flags, so the original's GetMoveSpeedClass of a footstep's source asks nothing
        // more: the port does not make that call, whose answer the original drops.)
        void *ctl = g_otherCtlOrig;
        std::memcpy(g_otherOrig[k] + 0xB0, &ctl, 4);
    }

    // The sight targets.
    for (int i = 0; i < 16; ++i)
        g_targetPortVt[i] = g_targetOrigVt[i] = reinterpret_cast<void *>(CT_Unexpected);
    g_targetPortVt[VSlot(&C_SightTarget::GetActor)] = reinterpret_cast<void *>(TG_GetActor);
    g_targetPortVt[VSlot(&C_SightTarget::GetKind)] = reinterpret_cast<void *>(TG_GetKind);
    g_targetOrigVt[0] = reinterpret_cast<void *>(TG_GetActor);
    g_targetOrigVt[1] = reinterpret_cast<void *>(TG_GetKind);
    for (int k = 0; k < TARGETS; ++k) {
        *reinterpret_cast<void ***>(g_targetPort[k]) = g_targetPortVt;
        *reinterpret_cast<void ***>(g_targetOrig[k]) = g_targetOrigVt;
        g_portTargets.push_back(reinterpret_cast<C_SightTarget *>(g_targetPort[k]));
        g_origTargets[k] = g_targetOrig[k];
        g_names[g_targetPort[k]] = g_names[g_targetOrig[k]] = "target" + std::to_string(k);
    }
    void **targets = OriginalAt<void **>(O_SIGHT_TARGETS);
    targets[0] = nullptr;
    targets[1] = g_origTargets;
    targets[2] = g_origTargets + TARGETS;
    targets[3] = g_origTargets + TARGETS;

    // The sounds, the events.
    for (int k = 0; k < SOUNDS; ++k)
        g_names[g_soundPort[k]] = g_names[g_soundOrig[k]] = "sound" + std::to_string(k);
    g_portEvents = new (g_portEventsBytes) C_HumanEvents();
    g_names[g_portEvents] = g_names[g_origEvents] = "events";
    g_extraPointers.push_back({g_portEvents, g_origEvents});
    for (int k = 0; k <= EVENTS; ++k)
        g_names[&g_ev[k]] = "event" + std::to_string(k);
    g_names[&g_sentinel] = "sentinel";
}

// ---- the random state
// The port's human's lists and sounds before a call (the original's built from them).
struct S_SensesBefore {
    std::vector<C_Human *> lists[4];                    // enemies, friends, bodies, traitors
    std::vector<S_RememberedSound *> sounds;
    std::vector<std::vector<uint8_t>> soundBytes;       // all the port's sounds
    S_HumanEvent events[EVENTS + 1];
    // The events object.
    std::vector<S_HumanEvent *> pending, stimuli;
    std::vector<int> nearby, unknown;
    S_vector4 sniperPos;
    float sniperRadius;
    bool sniper;
    int soundNext;
} g_before;
const uint32_t kListOffsets[4] = {0x264, 0x274, 0x284, 0x294};
std::vector<C_Human *> &PortList(int l)
{
    C_Human *h = g_h.human;
    return l == 0 ? h->m_seenEnemies : l == 1 ? h->m_seenFriends : l == 2 ? h->m_seenBodies : h->m_seenTraitors;
}

void RandomEvent(S_HumanEvent &e)
{
    std::memset(static_cast<void *>(&e), 0, sizeof e);
    static const uint32_t types[] = {HUMAN_EVENT_HIT, HUMAN_EVENT_MELEE_TRACKER_ALERT, 0x20000003, 0x40000001, 0x30000016, 0x00000016};
    e.type = P(0.5f) ? types[I(0, 5)] : uint32_t(I(0, 4)) << 28 | uint32_t(I(0, 0x20));
    e.timeLeft = P(0.3f) ? I(-2, 2) : I(-100, 12000);
    e.age = I(0, 20000);
    e.actorId = P(0.4f) ? -1 : P(0.8f) ? ActorNetId(I(0, 5)) : I(-3, 40);
    e.sourceId = P(0.4f) ? -1 : P(0.7f) ? ActorNetId(I(0, 5)) : P(0.4f) ? 0 : I(-3, 40);
    for (uint8_t &b : e.unknown14)
        b = uint8_t(I(0, 255));
    e.pos = {U(0, 40), U(-2, 4), U(0, 40), P(0.5f) ? 1.0f : U(-5, 5)};
    e.unknown30 = I(-5, 99);
    for (uint8_t &b : e.unknown34)
        b = uint8_t(I(0, 255));
}
// Some of the shared events, distinct (or, rarely, one twice).
std::vector<S_HumanEvent *> SomeEvents(int most, std::vector<int> &used)
{
    std::vector<S_HumanEvent *> out;
    const int n = P(0.2f) ? 0 : I(1, most);
    for (int i = 0; i < n; ++i) {
        int k = I(0, EVENTS - 1);
        for (int tries = 0; tries < 40 && std::find(used.begin(), used.end(), k) != used.end(); ++tries)
            k = I(0, EVENTS - 1);
        if (std::find(used.begin(), used.end(), k) != used.end())
            break;
        used.push_back(k);
        out.push_back(&g_ev[k]);
    }
    return out;
}
std::vector<int> SomeIds(int most)
{
    std::vector<int> out;
    const int n = P(0.2f) ? 0 : I(1, most);
    for (int i = 0; i < n; ++i)
        out.push_back(P(0.8f) ? ActorNetId(I(0, 5)) : P(0.5f) ? -1 : I(0, 40));
    return out;
}
void RandomEvents()
{
    for (int k = 0; k <= EVENTS; ++k)
        RandomEvent(g_ev[k]);
    C_HumanEvents *ev = g_portEvents;
    std::vector<int> used;
    const std::vector<S_HumanEvent *> pending = SomeEvents(5, used), stimuli = SomeEvents(7, used);
    ev->m_pending.assign(pending.begin(), pending.end());
    ev->m_stimuli.assign(stimuli.begin(), stimuli.end());
    ev->m_nearbyActors = SomeIds(5);
    ev->m_unknownActors = SomeIds(3);
    ev->m_sniperPos = {U(0, 40), U(-2, 4), U(0, 40), U(-2, 2)};
    ev->m_sniperRadius = P(0.3f) ? 0.0f : U(0, 30);
    ev->m_sniper = P(0.5f);
}

void RandomSenses()
{
    RandomControl();
    C_Human *h = g_h.human;
    // The controller: each type (the AI's is the scripted controller), deaf or not, quiet a while or not.
    auto *pc = reinterpret_cast<C_HumanController *>(g_cPort);
    pc->m_moveFlags = uint32_t(I(0, 0xFFFF)) | (P(0.3f) ? uint32_t(MOVE_DEAF) : 0u);
    std::memcpy(g_cOrig + 0x50, &pc->m_moveFlags, 4);
    pc->m_quietTime = P(0.3f) ? 250 + I(0, 1) : I(0, 2000);
    std::memcpy(g_cOrig + 0xB0, &pc->m_quietTime, 4);
    h->m_stats[STAT_HEALTH] = P(0.15f) ? I(-1, 0) : I(1, 100);
    h->m_stats[STAT_HEARING_RANGE] = P(0.2f) ? 0 : I(1, 60);
    h->m_sightId = P(0.2f) ? -1 : I(0, 30);
    h->m_sightTargetId = P(0.2f) ? -1 : I(0, 30);
    h->m_soundListener = P(0.3f) ? -1 : I(0, 30);
    h->m_role = P(0.4f) ? HUMAN_ROLE_PLAYER : static_cast<E_HumanRole>(I(0, 6));
    // (A car, or the meleeTracker standing for a vehicle that is not a car: both with the car's vtable.)
    h->m_vehicle = P(0.3f) ? static_cast<C_Actor *>(ActorPort(ACTOR_CAR_INDEX))
                 : P(0.15f) ? static_cast<C_Actor *>(ActorPort(ACTOR_MELEE_INDEX)) : nullptr;
    h->m_seat = I(0, 5);
    h->m_events = P(0.1f) ? nullptr : g_portEvents;
    // The other humans: alive or dead (at the edges), their sight targets.
    for (int k = 0; k < OTHERS; ++k) {
        C_Human *o = OtherPort(k);
        o->m_stats[STAT_HEALTH] = P(0.3f) ? I(-1, 1) : I(-50, 100);
        o->m_sightTargetId = P(0.1f) ? -1 : I(0, TARGETS - 1);
        std::memcpy(g_otherOrig[k] + 0xEC, &o->m_stats[STAT_HEALTH], 4);
        std::memcpy(g_otherOrig[k] + 0x25C, &o->m_sightTargetId, 4);
    }
    reinterpret_cast<C_MeleeTracker *>(g_meleePort)->GetStats().health = P(0.3f) ? I(-1, 1) : I(-50, 100);
    std::memcpy(g_meleeOrig + 0x4A0, &reinterpret_cast<C_MeleeTracker *>(g_meleePort)->GetStats().health, 4);
    for (int k = 0; k < 5; ++k)
        g_actorFrame[k]->SetPosInline(S_vector(U(15, 25), U(0, 2), U(15, 25)));
    for (int k = 0; k < 8; ++k) {
        g_sn.friendOf[k] = P(0.5f);
        g_sn.disguised[k] = P(0.3f);
    }
    // The sight targets: humans (the others, the human itself), the meleeTracker, something else; free slots.
    for (int k = 0; k < TARGETS; ++k) {
        g_targetKind[k] = P(0.65f) ? 0 : P(0.7f) ? 1 : I(2, 3);
        g_targetActor[k] = g_targetKind[k] == 0 ? (P(0.15f) ? ACTOR_SELF_INDEX : I(0, OTHERS - 1))
                         : g_targetKind[k] == 1 ? ACTOR_MELEE_INDEX : ACTOR_CAR_INDEX;
        g_targetMask[k] = P(0.15f) ? 0u : uint32_t(I(1, 0xFF));
        std::memcpy(g_targetPort[k] + 8, &g_targetMask[k], 4);
        std::memcpy(g_targetOrig[k] + 8, &g_targetMask[k], 4);
    }
    // The lists: the others and the human, sometimes one twice.
    const auto someHumans = [&] {
        std::vector<C_Human *> out;
        const int n = P(0.2f) ? 0 : I(1, 4);
        for (int i = 0; i < n; ++i) {
            const int k = P(0.1f) ? ACTOR_SELF_INDEX : I(0, OTHERS - 1);
            C_Human *p = static_cast<C_Human *>(ActorPort(k));
            if (P(0.85f) && std::find(out.begin(), out.end(), p) != out.end())
                continue;
            out.push_back(p);
        }
        return out;
    };
    for (int l = 0; l < 4; ++l) {
        std::vector<C_Human *> fresh = someHumans();
        // (A new vector each time: the old one's header may name a buffer the last call freed.)
        new (&PortList(l)) std::vector<C_Human *>(fresh);
        g_before.lists[l] = fresh;
    }
    // The remembered sounds: some of the pool's (their sources the actors or none; their times about 3 s ago).
    g_sn.time = uint32_t(I(10000, 200000));
    g_soundNext = I(0, SOUNDS - 1);
    std::vector<S_RememberedSound *> sounds;
    for (int k = 0; k < SOUNDS; ++k) {
        S_RememberedSound *s = SoundPort(k);
        s->pos = S_vector(U(10, 30), U(-1, 3), U(10, 30));
        s->posW = P(0.5f) ? 1.0f : U(-2, 2);
        s->chapter = I(0, 14);
        s->source = P(0.2f) ? nullptr : static_cast<C_Actor *>(ActorPort(I(0, 5)));
        s->time = g_sn.time - uint32_t(P(0.3f) ? 3000 + I(-1, 1) : I(-100, 6000));
        s->refreshed = P(0.5f);
        if (k != g_soundNext && P(0.4f))
            sounds.push_back(s);
    }
    new (&h->m_heardSounds) std::vector<S_RememberedSound *>(sounds);
    g_before.sounds = sounds;
    g_before.soundBytes.clear();
    for (int k = 0; k < SOUNDS; ++k)
        g_before.soundBytes.emplace_back(g_soundPort[k], g_soundPort[k] + 0x20);
    g_before.soundNext = g_soundNext;
    RandomEvents();
    g_sn.visible = P(0.5f);
    g_sn.writeVisible = P(0.7f);
    g_sn.canSee = I(-1, 2);
    g_sn.seatFlags = P(0.4f) ? SEAT_FLAG_HIDDEN : uint32_t(I(0, 0xFF));
    g_sn.handled = P(0.7f) ? 0 : I(1, 2);
    g_script.activeIsOwner = P(0.5f);
}

// The port's events object before a call (and the events' bytes).
void NoteEventsBefore()
{
    const C_HumanEvents *ev = g_portEvents;
    g_before.pending.assign(ev->m_pending.begin(), ev->m_pending.end());
    g_before.stimuli.assign(ev->m_stimuli.begin(), ev->m_stimuli.end());
    g_before.nearby = ev->m_nearbyActors;
    g_before.unknown = ev->m_unknownActors;
    g_before.sniperPos = ev->m_sniperPos;
    g_before.sniperRadius = ev->m_sniperRadius;
    g_before.sniper = ev->m_sniper;
    std::memcpy(static_cast<void *>(g_before.events), g_ev, sizeof g_before.events);
}
// ... into the original's events object (and the events' bytes back).
void EventsToOriginal()
{
    std::memcpy(static_cast<void *>(g_ev), g_before.events, sizeof g_before.events);
    std::memset(g_origEvents, 0, sizeof g_origEvents);
    std::memcpy(g_origEvents, &g_before.sniperPos, 16);
    std::memcpy(g_origEvents + 0x10, &g_before.sniperRadius, 4);
    g_origEvents[0x14] = g_before.sniper;
    const auto words = [](const std::vector<S_HumanEvent *> &v) {
        std::vector<uint32_t> out;
        for (S_HumanEvent *e : v)
            out.push_back(Word(e));
        return out;
    };
    MsvcDequePut(g_origEvents + 0x18, words(g_before.pending));
    MsvcDequePut(g_origEvents + 0x48, words(g_before.stimuli));
    MsvcVectorPut(g_origEvents + 0x78, std::vector<uint32_t>(g_before.nearby.begin(), g_before.nearby.end()), size_t(I(0, 2)));
    MsvcVectorPut(g_origEvents + 0x88, std::vector<uint32_t>(g_before.unknown.begin(), g_before.unknown.end()), size_t(I(0, 2)));
    // The pool's list emptied (what the call releases is read from it).
    *OriginalAt<void **>(O_EVENT_POOL_HEAD) = nullptr;
    *OriginalAt<int *>(O_EVENT_POOL_COUNT) = 0;
}
// The human's lists and sounds into the original's copy (after ToOriginal).
void SensesToOriginal()
{
    for (int l = 0; l < 4; ++l) {
        std::vector<uint32_t> words;
        for (C_Human *p : g_before.lists[l])
            words.push_back(Word(ActorOrig(ActorIndex(p))));
        MsvcVectorPut(o_human + kListOffsets[l], words, size_t(I(0, 2)));
    }
    std::vector<uint32_t> sounds;
    for (S_RememberedSound *s : g_before.sounds)
        sounds.push_back(Word(g_soundOrig[reinterpret_cast<uint8_t(*)[0x20]>(s) - g_soundPort]));
    MsvcVectorPut(o_human + 0x2A8, sounds, size_t(I(0, 2)));
    for (int k = 0; k < SOUNDS; ++k) {
        std::memcpy(g_soundOrig[k], g_before.soundBytes[k].data(), 0x20);
        void *source;
        std::memcpy(&source, g_soundOrig[k] + 0x14, 4);
        source = MapSoundSource(source);
        std::memcpy(g_soundOrig[k] + 0x14, &source, 4);
    }
    g_soundNext = g_before.soundNext;
    *OriginalAt<void **>(O_SOUND_POOL_HEAD) = nullptr;
    *OriginalAt<int *>(O_SOUND_POOL_COUNT) = 0;
}

// ---- the state after a call, described alike for both sides (names, numbers)
std::string Hex(const void *p, size_t n)
{
    std::string s;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof b, "%02x", static_cast<const uint8_t *>(p)[i]);
        s += b;
    }
    return s;
}
std::string SoundText(const uint8_t *s)
{
    void *source;
    std::memcpy(&source, s + 0x14, 4);
    return Hex(s, 0x14) + "/" + NameOf(source) + "/" + Hex(s + 0x18, 5);
}
std::string ListText(const std::vector<uint32_t> &words)
{
    std::string s;
    for (uint32_t w : words)
        s += NameOf(reinterpret_cast<const void *>(uintptr_t(w))) + " ";
    return s;
}
std::string IdsText(const std::vector<uint32_t> &ids)
{
    std::string s;
    for (uint32_t id : ids)
        s += std::to_string(int(id)) + " ";
    return s;
}
template <class T> std::vector<uint32_t> Words(const T &c)
{
    std::vector<uint32_t> out;
    for (const auto &x : c)
        out.push_back(uint32_t(uintptr_t(x)));
    return out;
}
// The released (most recent first): the port's drained down to the sentinel.
std::vector<uint32_t> PortReleasedEvents()
{
    std::vector<uint32_t> out;
    for (int i = 0; i < 1000; ++i) {
        S_HumanEvent *e = AllocHumanEvent();
        if (e == &g_sentinel)
            return out;
        out.push_back(Word(e));
    }
    out.push_back(0xBADBAD);
    return out;
}
std::vector<uint32_t> OrigReleased(uint32_t head)
{
    std::vector<uint32_t> out;
    for (auto *node = *OriginalAt<uint32_t **>(head); node && out.size() < 1000; node = reinterpret_cast<uint32_t *>(uintptr_t(node[1])))
        out.push_back(node[0]);
    return out;
}
std::string EventBytes()
{
    std::string s;
    for (int k = 0; k <= EVENTS; ++k)
        s += Hex(&g_ev[k], 0x40) + " ";
    return s;
}
std::string PortSensesText(bool events)
{
    C_Human *h = g_h.human;
    std::string s;
    for (int l = 0; l < 4; ++l)
        s += "list" + std::to_string(l) + ": " + ListText(Words(PortList(l))) + "\n";
    s += "sounds: " + ListText(Words(h->m_heardSounds)) + "\n";
    for (int k = 0; k < SOUNDS; ++k)
        s += SoundText(g_soundPort[k]) + " ";
    s += "\nfreed sounds: (logged)\n";
    if (events) {
        const C_HumanEvents *ev = g_portEvents;
        s += "pending: " + ListText(Words(ev->m_pending)) + "\nstimuli: " + ListText(Words(ev->m_stimuli)) + "\nnearby: " +
             IdsText(Words(ev->m_nearbyActors)) + "\nunknown: " + IdsText(Words(ev->m_unknownActors)) + "\nsniper: " +
             Hex(&ev->m_sniperPos, 16) + " " + Hex(&ev->m_sniperRadius, 4) + " " + std::to_string(ev->m_sniper) +
             "\nreleased: " + ListText(PortReleasedEvents()) + "\nevents: " + EventBytes() + "\n";
    }
    return s;
}
std::string OrigSensesText(bool events)
{
    std::string s;
    for (int l = 0; l < 4; ++l)
        s += "list" + std::to_string(l) + ": " + ListText(MsvcVectorGet(o_human + kListOffsets[l])) + "\n";
    s += "sounds: " + ListText(MsvcVectorGet(o_human + 0x2A8)) + "\n";
    for (int k = 0; k < SOUNDS; ++k)
        s += SoundText(g_soundOrig[k]) + " ";
    s += "\nfreed sounds: (logged)\n";
    if (events) {
        s += "pending: " + ListText(MsvcDequeGet(g_origEvents + 0x18)) + "\nstimuli: " + ListText(MsvcDequeGet(g_origEvents + 0x48)) +
             "\nnearby: " + IdsText(MsvcVectorGet(g_origEvents + 0x78)) + "\nunknown: " + IdsText(MsvcVectorGet(g_origEvents + 0x88)) +
             "\nsniper: " + Hex(g_origEvents, 16) + " " + Hex(g_origEvents + 0x10, 4) + " " + std::to_string(g_origEvents[0x14]) +
             "\nreleased: " + ListText(OrigReleased(O_EVENT_POOL_HEAD)) + "\nevents: " + EventBytes() + "\n";
    }
    return s;
}
// The original's freed sounds (its inline free list) logged as the port's FreeRememberedSound calls are, in the order
// it freed them.
void LogOrigFreedSounds()
{
    std::vector<uint32_t> freed = OrigReleased(O_SOUND_POOL_HEAD);
    for (size_t i = freed.size(); i-- > 0;)
        Log(nullptr, "FreeRememberedSound");
}

// The frame's on flag and SetOn count during a call.
// (With the world matrix's position row as it is: not updated by reading it.)
std::string FrameOnText(int setOn0)
{
    return std::to_string((g_h.frame->IsOn() ? 1 : 0) + 2 * (Fake(g_h.frame)->setOn - setOn0)) + " " +
           Hex(&g_h.frame->m_worldMatrix.m[3][0], 12);
}

// A call of both sides with the senses' state compared after it (`events`: the events object and the pools too).
std::string g_portText, g_origText;
template <class P, class O> void SensesCall(const char *what, bool events, P port, O original)
{
    NoteEventsBefore();
    DiffCall(what,
             [&] {
                 if (events)
                     ReleaseHumanEvent(&g_sentinel);
                 const int setOn0 = Fake(g_h.frame)->setOn;
                 const int r = port();
                 g_portText = PortSensesText(events) + "frame " + FrameOnText(setOn0);
                 Fake(g_h.frame)->setOn = setOn0;
                 return r;
             },
             [&](void *x) {
                 SensesToOriginal();
                 EventsToOriginal();
                 const int setOn0 = Fake(g_h.frame)->setOn;
                 const int r = original(x);
                 LogOrigFreedSounds();
                 g_origText = OrigSensesText(events) + "frame " + FrameOnText(setOn0);
                 Fake(g_h.frame)->setOn = setOn0;
                 return r;
             });
    CHECK(g_portText == g_origText);
    static int shown;
    if (g_portText != g_origText && shown++ < 10)
        std::printf("  %s: state differs:\n--- port\n%s\n--- original\n%s\n", what, g_portText.c_str(), g_origText.c_str());
}

// ---- part 24a: human_senses.cpp

void TestSensesLists()
{
    C_Human *h = g_h.human;
    std::printf("senses: seen lists, sight events\n");
    for (int i = 0; i < 15000 / QUICK; ++i) {
        RandomSenses();
        const int k = P(0.15f) ? ACTOR_SELF_INDEX : I(0, OTHERS - 1);
        C_Human *port = static_cast<C_Human *>(ActorPort(k));
        void *orig = ActorOrig(k);
        switch (i % 12) {
        case 0:
            SensesCall("AddSeenBody", false, [&] { h->AddSeenBody(port); return 0; },
                       [&](void *) { OCall<void>(0x005a4840, o_human, orig); return 0; });
            break;
        case 1:
            SensesCall("AddSeenEnemy", false, [&] { h->AddSeenEnemy(port); return 0; },
                       [&](void *) { OCall<void>(0x005a4a10, o_human, orig); return 0; });
            break;
        case 2:
            SensesCall("AddSeenFriend", false, [&] { h->AddSeenFriend(port); return 0; },
                       [&](void *) { OCall<void>(0x005a4be0, o_human, orig); return 0; });
            break;
        case 3:
            SensesCall("AddSeenTraitor", false, [&] { h->AddSeenTraitor(port); return 0; },
                       [&](void *) { OCall<void>(0x005a4da0, o_human, orig); return 0; });
            break;
        case 4:
            SensesCall("RemoveSeenTraitor", false, [&] { h->RemoveSeenTraitor(port); return 0; },
                       [&](void *) { OCall<void>(0x005a4f60, o_human, orig); return 0; });
            break;
        case 5:
            SensesCall("RemoveSeenFriend", false, [&] { h->RemoveSeenFriend(port); return 0; },
                       [&](void *) { OCall<void>(0x005a4fc0, o_human, orig); return 0; });
            break;
        case 6:
            SensesCall("RemoveSeenEnemy", false, [&] { h->RemoveSeenEnemy(port); return 0; },
                       [&](void *) { OCall<void>(0x005a5020, o_human, orig); return 0; });
            break;
        case 7:
            SensesCall("HasSeenEnemy", false, [&] { return int(h->HasSeenEnemy(port)); },
                       [&](void *) { return int(OCall<uint32_t>(0x005a5e50, o_human, orig) & 0xFF); });
            break;
        case 8:
        case 9:
        case 10: {
            // A target that is registered (the original uses one that is not: the port checks); lost or seen; no
            // controller now and then.
            int target = I(0, TARGETS - 1);
            for (int tries = 0; tries < 20 && !g_targetMask[target]; ++tries)
                target = I(0, TARGETS - 1);
            if (!g_targetMask[target])
                break;
            const int lost = P(0.5f) ? I(0, 1) : I(-1, 3);
            if (P(0.05f))
                h->m_controller = nullptr;
            SensesCall("OnSightEvent", false, [&] { h->OnSightEvent(lost, target); return 0; },
                       [&](void *) { OCall<void>(0x005a5250, o_human, lost, target); return 0; });
            break;
        }
        default: {
            // (The lists' humans' sight targets registered ones: OnSightEvent goes through a dropped one's, which the
            // original uses unchecked.)
            int registered = 0;
            while (!g_targetMask[registered])
                ++registered;
            for (int l = 0; l < 4; ++l) {
                for (C_Human *p : PortList(l)) {
                    if (p->m_sightTargetId >= 0 && p->m_sightTargetId < TARGETS && g_targetMask[p->m_sightTargetId])
                        continue;
                    p->m_sightTargetId = P(0.5f) ? registered : g_targetMask[TARGETS - 1] ? TARGETS - 1 : registered;
                    const int k = ActorIndex(p);
                    if (k < OTHERS)
                        std::memcpy(g_otherOrig[k] + 0x25C, &p->m_sightTargetId, 4);
                }
            }
            SensesCall("UpdateSeenHumans", false, [&] { h->UpdateSeenHumans(); return 0; },
                       [&](void *) { OCall<void>(0x005a50a0, o_human); return 0; });
            break;
        }
        }
    }
}

// A sound heard: about the human (at the edges of its hearing range, at it, 1 m away), each chapter, each source.
struct S_TestHeard {
    S_HeardSound port, orig;
};
S_FrameData g_portFrameData, g_origFrameData, g_nullFrameData;
I3D_frame *g_plainFrame, *g_nullDataFrame;
void TestSensesSounds()
{
    C_Human *h = g_h.human;
    std::printf("senses: sounds\n");
    S_quat none;
    none.Identity();
    g_plainFrame = NewFrame(FRAME_DUMMY, "plain", S_vector(1, 1, 1), none, Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_sector);
    g_nullDataFrame = NewFrame(FRAME_DUMMY, "nullData", S_vector(1, 1, 1), none, Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_sector);
    g_nullDataFrame->m_data = &g_nullFrameData;
    g_portFrameData.actor = h;
    g_origFrameData.actor = reinterpret_cast<C_Actor *>(o_human);
    for (int i = 0; i < 21000 / QUICK; ++i) {
        RandomSenses();
        g_h.frame->m_data = &g_portFrameData;
        if (i % 6 == 5) {
            SensesCall("ForgetOldSounds", false, [&] { h->ForgetOldSounds(); return 0; },
                       [&](void *) { OCall<void>(0x005a5380, o_human); return 0; });
            continue;
        }
        S_TestHeard heard;
        const S_vector at = g_h.frame->GetWorldPos();
        S_vector pos;
        const int range = h->m_stats[STAT_HEARING_RANGE];
        switch (I(0, 4)) {
        case 0:
            pos = at;
            break;
        case 1:
            pos = at + S_vector(1, 0, 0);
            break;
        case 2:
            pos = at + S_vector(0, 0, float(range + I(-1, 1)));
            break;
        default:
            pos = at + S_vector(U(-50, 50), U(-5, 5), U(-50, 50));
            break;
        }
        const int chapter = P(0.05f) ? I(14, 16) : I(0, 13);
        int source = P(0.15f) ? -1 : I(0, 5);
        // (A speech's source is a human: the original reads its health; a footstep's not null: it reads its type.)
        if (chapter == SOUND_CHAPTER_SPEECH && (source < 0 || source == ACTOR_CAR_INDEX || source == ACTOR_MELEE_INDEX))
            source = I(0, OTHERS - 1);
        if (chapter == SOUND_CHAPTER_PUPPET_MOVEMENT && source < 0)
            source = I(0, 5);
        for (S_HeardSound *s : {&heard.port, &heard.orig}) {
            s->pos = pos;
            s->posW = P(0.5f) ? 1.0f : 0.0f;
            s->intensity = P(0.2f) ? 0.95f : U(0, 1);
            s->chapter = chapter;
        }
        heard.orig.posW = heard.port.posW;
        heard.orig.intensity = heard.port.intensity;
        heard.port.source = source < 0 ? nullptr : static_cast<C_Actor *>(ActorPort(source));
        heard.orig.source = source < 0 ? nullptr : static_cast<C_Actor *>(ActorOrig(source));
        // The listener: the human's frame, or none, or a frame without data or without an actor.
        const int listener = P(0.9f) ? 0 : I(1, 3);
        heard.port.listener = heard.orig.listener = listener == 0 ? g_h.frame : listener == 1 ? nullptr : listener == 2 ? g_plainFrame : g_nullDataFrame;
        // A remembered one from the same source and chapter now and then.
        if (P(0.3f) && !h->m_heardSounds.empty()) {
            S_RememberedSound *r = h->m_heardSounds[size_t(I(0, int(h->m_heardSounds.size()) - 1))];
            r->source = heard.port.source;
            r->chapter = chapter;
            const size_t k = reinterpret_cast<uint8_t(*)[0x20]>(r) - g_soundPort;
            std::memcpy(g_before.soundBytes[k].data(), r, 0x20);
        }
        SensesCall("OnSoundHeard", false, [&] { C_Human::OnSoundHeard(&heard.port); return 0; },
                   [&](void *) {
                       g_h.frame->m_data = &g_origFrameData;
                       typedef void(__stdcall * T)(const S_HeardSound *);
                       OriginalAt<T>(0x005a5430)(&heard.orig);
                       return 0;
                   });
    }
    g_h.frame->m_data = nullptr;
}

void TestSensesEvents()
{
    C_Human *h = g_h.human;
    std::printf("senses: listening, proximity, the event queue, freezing\n");
    for (int i = 0; i < 18000 / QUICK; ++i) {
        RandomSenses();
        switch (i % 8) {
        case 0:
            SensesCall("StopListening", false, [&] { h->StopListening(); return 0; },
                       [&](void *) { OCall<void>(0x005a7f50, o_human); return 0; });
            break;
        case 1:
            SensesCall("StartListening", false, [&] { h->StartListening(); return 0; },
                       [&](void *) { OCall<void>(0x005a7fa0, o_human); return 0; });
            break;
        case 2: {
            const int k = P(0.1f) ? -1 : I(0, 5);
            void *port = k < 0 ? nullptr : ActorPort(k), *orig = k < 0 ? nullptr : ActorOrig(k);
            const float distance = U(0, 30);
            const bool ended = P(0.5f), unknown = P(0.3f);
            if (P(0.1f))
                h->m_controller = nullptr;
            SensesCall("OnProximity", true,
                       [&] { h->OnProximity(static_cast<C_Actor *>(port), distance, ended, unknown); return 0; },
                       [&](void *) { OCall<void>(0x005a83f0, o_human, orig, distance, int(ended), int(unknown)); return 0; });
            break;
        }
        case 3:
            // (The original reads the queue of a missing events object: the port alike; not given.)
            if (!h->m_events)
                h->m_events = g_portEvents;
            SensesCall("ProcessEvents", true, [&] { h->ProcessEvents(); return 0; },
                       [&](void *) { OCall<void>(0x005a8480, o_human); return 0; });
            break;
        case 4: {
            S_HumanEvent *e = &g_ev[I(0, EVENTS - 1)];
            SensesCall("RaiseEvent", true, [&] { h->RaiseEvent(e); return 0; },
                       [&](void *) { OCall<void>(0x005a8b30, o_human, e); return 0; });
            break;
        }
        case 5:
        case 6: {
            const bool on = P(0.5f);
            if (P(0.1f))
                h->m_controller = nullptr;
            // (The frame moved without its world matrix updated: Freeze's Update shows.)
            g_h.frame->SetPosInline(S_vector(U(18, 22), U(0, 2), U(18, 22)));
            SensesCall("Freeze", false, [&] { h->Freeze(on); return 0; },
                       [&](void *) { OCall<void>(0x005a5d50, o_human, int(on)); return 0; });
            break;
        }
        default: {
            const bool on = P(0.5f);
            if (P(0.4f))
                SetControllerType(HUMAN_CONTROLLER_AI);
            SensesCall("Suspend", false, [&] { h->Suspend(on); return 0; },
                       [&](void *) { OCall<void>(0x005af570, o_human, int(on)); return 0; });
            break;
        }
        }
    }
}

// ---- part 24b: human_events.cpp

void TestEventsQueue()
{
    C_HumanEvents *ev = g_portEvents;
    std::printf("events: queue, stimuli, nearby actors\n");
    for (int i = 0; i < 24000 / QUICK; ++i) {
        RandomSenses();
        switch (i % 14) {
        case 0:
            SensesCall("EventsReset", true, [&] { ev->Reset(); return 0; },
                       [&](void *) { OCall<void>(0x005ae450, g_origEvents); return 0; });
            break;
        case 1: {
            const int delta = P(0.3f) ? I(0, 2) : I(0, 8000);
            SensesCall("EventsTick", true, [&] { ev->Tick(delta); return 0; },
                       [&](void *) { OCall<void>(0x005ad700, g_origEvents, delta); return 0; });
            break;
        }
        case 2:
        case 3: {
            const int k = P(0.1f) ? -1 : I(0, 5);
            const float distance = U(0, 30);
            void *port = k < 0 ? nullptr : ActorPort(k), *orig = k < 0 ? nullptr : ActorOrig(k);
            if (i % 14 == 2)
                SensesCall("AddNearbyActor", true, [&] { return int(ev->AddNearbyActor(static_cast<C_Actor *>(port), distance)); },
                           [&](void *) { return int(OCall<uint32_t>(0x005ae640, g_origEvents, orig, distance) & 0xFF); });
            else
                SensesCall("RemoveNearbyActor", true, [&] { return int(ev->RemoveNearbyActor(static_cast<C_Actor *>(port), distance)); },
                           [&](void *) { return int(OCall<uint32_t>(0x005ae810, g_origEvents, orig, distance) & 0xFF); });
            break;
        }
        case 4: {
            const int index = I(-2, int(ev->m_nearbyActors.size()) + 1);
            SensesCall("GetNearbyActor", true, [&] { return ActorIndex(ev->GetNearbyActor(index)); },
                       [&](void *) { return ActorIndex(OCall<void *>(0x005ae230, g_origEvents, index)); });
            break;
        }
        case 5: {
            S_vector pos = P(0.4f) ? g_actorFrame[I(0, 4)]->GetWorldPos() + S_vector(U(-2, 2), U(-1, 1), U(-2, 2))
                                   : S_vector(U(10, 30), U(-1, 3), U(10, 30));
            if (P(0.1f))
                pos = g_actorFrame[I(0, 4)]->GetWorldPos() + S_vector(2, 0, 0);
            SensesCall("IsAwayFromNearbyActors", true, [&] { return int(ev->IsAwayFromNearbyActors(pos)); },
                       [&](void *) { return int(OCall<uint32_t>(0x005ae890, g_origEvents, &pos) & 0xFF); });
            break;
        }
        case 6: {
            const int k = P(0.1f) ? -1 : I(0, 5);
            void *port = k < 0 ? nullptr : ActorPort(k), *orig = k < 0 ? nullptr : ActorOrig(k);
            SensesCall("AddUnknownActor", true, [&] { ev->AddUnknownActor(static_cast<C_Actor *>(port)); return 0; },
                       [&](void *) { OCall<void>(0x005ae950, g_origEvents, orig); return 0; });
            break;
        }
        case 7: {
            S_HumanEvent *e = &g_ev[I(0, EVENTS - 1)];
            SensesCall("EventsRaise", true, [&] { ev->Raise(e); return 0; },
                       [&](void *) { OCall<void>(0x005adac0, g_origEvents, e); return 0; });
            break;
        }
        case 8:
            SensesCall("PopFront", true, [&] { return EventIndex(ev->PopFront()); },
                       [&](void *) { return EventIndex(OCall<S_HumanEvent *>(0x005adc50, g_origEvents)); });
            break;
        case 9:
        case 10: {
            // A new event (not one the object holds), about an actor tracked already or not.
            S_HumanEvent *e = OtherEvent();
            if (P(0.5f) && !ev->m_stimuli.empty()) {
                const S_HumanEvent *tracked = ev->m_stimuli[size_t(I(0, int(ev->m_stimuli.size()) - 1))];
                if (P(0.5f))
                    e->actorId = tracked->actorId;
                else
                    e->sourceId = tracked->sourceId;
            }
            int time = P(0.3f) ? I(-1, 1) : I(0, 12000);
            // (Now and then the time a tracked one has left.)
            if (P(0.2f) && !ev->m_stimuli.empty())
                time = ev->m_stimuli[size_t(I(0, int(ev->m_stimuli.size()) - 1))]->timeLeft;
            const bool keep = P(0.5f);
            SensesCall("AddStimulus", true, [&] { ev->AddStimulus(time, e, keep); return 0; },
                       [&](void *) { OCall<void>(0x005adee0, g_origEvents, time, e, int(keep)); return 0; });
            break;
        }
        case 11: {
            S_vector pos(U(0, 40), U(-2, 4), U(0, 40));
            if (P(0.2f) && !ev->m_stimuli.empty()) {
                const S_HumanEvent *e = ev->m_stimuli[size_t(I(0, int(ev->m_stimuli.size()) - 1))];
                pos = S_vector(e->pos.x, e->pos.y, e->pos.z);
            }
            SensesCall("GetStrongestStimulus", true, [&] { return EventIndex(ev->GetStrongestStimulus(pos)); },
                       [&](void *) { return EventIndex(OCall<S_HumanEvent *>(0x005ae310, g_origEvents, &pos)); });
            break;
        }
        case 12:
            SensesCall("GetAttacker", true, [&] { return ActorIndex(ev->GetAttacker()); },
                       [&](void *) { return ActorIndex(OCall<void *>(0x005ae270, g_origEvents)); });
            break;
        default: {
            const bool on = P(0.5f);
            const float radius = U(0, 40);
            // (A world matrix row: w 1.)
            const S_vector4 pos = {U(0, 40), U(-2, 4), U(0, 40), 1.0f};
            const S_vector posV(pos.x, pos.y, pos.z);
            SensesCall("SetSniper", true, [&] { ev->SetSniper(on, radius, posV); return 0; },
                       [&](void *) { OCall<void>(0x005ae610, g_origEvents, int(on), radius, &pos); return 0; });
            break;
        }
        }
    }
}

void TestEventsIds()
{
    std::printf("events: actor ids\n");
    for (int i = 0; i < 4000 / QUICK; ++i) {
        RandomSenses();
        if (i % 2 == 0) {
            const int k = P(0.15f) ? -1 : I(0, 5);
            void *port = k < 0 ? nullptr : ActorPort(k), *orig = k < 0 ? nullptr : ActorOrig(k);
            SensesCall("ActorToId", false, [&] { return ActorToId(static_cast<C_Actor *>(port)); },
                       [&](void *) {
                           typedef int(__stdcall * T)(void *);
                           return OriginalAt<T>(0x005aca30)(orig);
                       });
        } else {
            const int id = P(0.6f) ? ActorNetId(I(0, 5)) : I(-3, 40);
            SensesCall("IdToActor", false, [&] { return ActorIndex(IdToActor(id)); },
                       [&](void *) {
                           typedef void *(__stdcall * T)(int);
                           return ActorIndex(OriginalAt<T>(0x005aca50)(id));
                       });
        }
    }
}

// The saved games: each side's file byte for byte; loading the port's file, or one of random chunks (each kind,
// unknown ones, short ones), on both sides (the loaded events compared by what LoadGame sets: each side's pool hands
// out its own objects).
void WriteRandomEventChunk(C_chunk &c)
{
    static const uint16_t kinds[] = {500, 0x1F5, 0x1F6, 0x1F7, 0x1F8, 0x1F9, 0x123};
    const uint16_t kind = kinds[I(0, 6)];
    c.Begin(kind);
    // (The actor and source chunks short half the time: their reads fail and leave what the chunk's case set.)
    const bool id = kind == 0x1F5 || kind == 0x1F6;
    const int words = id ? (P(0.5f) ? 0 : 1) : P(0.15f) ? I(0, 8) : 9;
    for (int w = 0; w < words; ++w) {
        const float f = U(-30, 30);
        int v = I(-5, 20000);
        if (P(0.5f))
            std::memcpy(&v, &f, 4);
        c.Write(&v, 4);
    }
    // (A short tail now and then.)
    if (P(0.1f)) {
        const uint8_t b = uint8_t(I(0, 255));
        c.Write(&b, 1);
    }
    c.Ascend();
}
void WriteRandomEventsFile(const std::string &file)
{
    C_chunk c;
    c.Open(file.c_str(), true);
    c.Begin(0x7777);
    const int chunks = I(0, 8);
    for (int i = 0; i < chunks; ++i) {
        static const uint16_t kinds[] = {600, 0x259, 0x25A, 0x25B, 0x25C, 0x25D, 0x321};
        const uint16_t kind = kinds[I(0, 6)];
        c.Begin(kind);
        if (kind == 0x259 || kind == 0x25A) {
            const int events = I(0, 3);
            for (int e = 0; e < events; ++e)
                WriteRandomEventChunk(c);
        } else {
            const int bytes = P(0.2f) ? I(0, 6) : kind == 600 ? 17 : 4;
            for (int b = 0; b < bytes; ++b) {
                const uint8_t v = uint8_t(I(0, 255));
                c.Write(&v, 1);
            }
        }
        c.Ascend();
    }
    c.Ascend();
}
// The loaded state: the events' contents (what LoadGame sets) instead of their identities.
std::string LoadedEventText(const S_HumanEvent *e)
{
    return Hex(&e->type, 0x14) + "," + Hex(&e->pos, 12) + "," + Hex(&e->unknown30, 4);
}
std::string LoadedText(const std::vector<uint32_t> &pending, const std::vector<uint32_t> &stimuli, const std::vector<uint32_t> &nearby,
                       const std::vector<uint32_t> &unknown, const void *sniper)
{
    std::string s = "pending: ";
    for (uint32_t w : pending)
        s += LoadedEventText(reinterpret_cast<const S_HumanEvent *>(uintptr_t(w))) + " ";
    s += "\nstimuli: ";
    for (uint32_t w : stimuli)
        s += LoadedEventText(reinterpret_cast<const S_HumanEvent *>(uintptr_t(w))) + " ";
    return s + "\nnearby: " + IdsText(nearby) + "\nunknown: " + IdsText(unknown) + "\nsniper: " + Hex(sniper, 16) + " " +
           Hex(static_cast<const uint8_t *>(sniper) + 0x10, 4) + " " + std::to_string(static_cast<const uint8_t *>(sniper)[0x14]);
}
// One event loaded on its own (S_HumanEvent::LoadGame: its result, which C_HumanEvents' loading drops, and what it
// sets) from random chunks, into the same event bytes on both sides.
void TestEventLoad()
{
    std::printf("events: one event loaded\n");
    const std::string file = TempFile("event_one.sav");
    int same = 0;
    for (int i = 0; i < 3000 / QUICK; ++i) {
        std::remove(file.c_str());
        {
            C_chunk c;
            c.Open(file.c_str(), true);
            c.Begin(0x7777);
            const int chunks = I(0, 4);
            for (int k = 0; k < chunks; ++k)
                WriteRandomEventChunk(c);
            c.Ascend();
        }
        S_HumanEvent start;
        RandomEvent(start);
        std::string text[2];
        int result[2];
        for (int side = 0; side < 2; ++side) {
            S_HumanEvent &e = g_ev[0];
            e = start;
            C_chunk c;
            c.Open(file.c_str(), false);
            uint16_t outer = 0;
            c.Descend(outer);
            result[side] = side == 0 ? int(e.LoadGame(c)) : int(OCall<uint32_t>(0x005accd0, &e, &c) & 0xFF);
            text[side] = LoadedEventText(&e);
        }
        CHECK(result[0] == result[1]);
        CHECK(text[0] == text[1]);
        same += result[0] == result[1] && text[0] == text[1];
        static int shown;
        if ((result[0] != result[1] || text[0] != text[1]) && shown++ < 10)
            std::printf("  EventLoad differs: port %d %s, original %d %s\n", result[0], text[0].c_str(), result[1], text[1].c_str());
    }
    g_stats["EventLoad"].calls += 3000 / QUICK;
    g_stats["EventLoad"].agree += same;
}

void TestEventsSaves()
{
    C_HumanEvents *ev = g_portEvents;
    std::printf("events: saved games\n");
    const std::string portFile = TempFile("events_port.sav"), origFile = TempFile("events_orig.sav");
    for (int i = 0; i < 2100 / QUICK; ++i) {
        RandomSenses();
        if (i % 3 == 0) {
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            SensesCall("EventsSave", true,
                       [&] {
                           C_chunk c;
                           c.Open(portFile.c_str(), true);
                           c.Begin(0x7777);
                           const int r = ev->SaveGame(c);
                           c.Ascend();
                           return r;
                       },
                       [&](void *) {
                           C_chunk c;
                           c.Open(origFile.c_str(), true);
                           c.Begin(0x7777);
                           const int r = OCall<uint32_t>(0x005ad040, g_origEvents, &c) & 0xFF;
                           c.Ascend();
                           return r;
                       });
            CHECK(FileBytes(portFile) == FileBytes(origFile));
            continue;
        }
        if (i % 3 == 2) {
            std::remove(portFile.c_str());
            WriteRandomEventsFile(portFile);
        }
        // (Saved by the last pass or written at random; loaded on both sides.)
        RandomSenses();
        NoteEventsBefore();
        std::string portText, origText;
        int portResult = 0, origResult = 0;
        {
            // (The events Reset releases and the loading does not take back are drained from the pool after it, down
            // to a sentinel: left there, a later load would hand one out while it is in use. A loading that took them
            // all takes the sentinel too.)
            ReleaseHumanEvent(&g_sentinel);
            C_chunk c;
            c.Open(portFile.c_str(), false);
            uint16_t outer = 0;
            c.Descend(outer);
            portResult = ev->LoadGame(c);
            portText = LoadedText(Words(ev->m_pending), Words(ev->m_stimuli), Words(ev->m_nearbyActors), Words(ev->m_unknownActors),
                                  &ev->m_sniperPos);
            const auto has = [&](const std::deque<S_HumanEvent *> &d) { return std::find(d.begin(), d.end(), &g_sentinel) != d.end(); };
            if (!has(ev->m_pending) && !has(ev->m_stimuli))
                PortReleasedEvents();
        }
        {
            EventsToOriginal();
            C_chunk c;
            c.Open(portFile.c_str(), false);
            uint16_t outer = 0;
            c.Descend(outer);
            origResult = OCall<uint32_t>(0x005ad390, g_origEvents, &c) & 0xFF;
            origText = LoadedText(MsvcDequeGet(g_origEvents + 0x18), MsvcDequeGet(g_origEvents + 0x48), MsvcVectorGet(g_origEvents + 0x78),
                                  MsvcVectorGet(g_origEvents + 0x88), g_origEvents);
        }
        // (The port's pool keeps what it handed out; the events it released and took back are the shared ones.)
        CHECK(portResult == origResult);
        CHECK(portText == origText);
        static int shown;
        if (portText != origText && shown++ < 10)
            std::printf("  EventsLoad differs:\n--- port\n%s\n--- original\n%s\n", portText.c_str(), origText.c_str());
        ++g_stats["EventsLoad"].calls;
        if (portText == origText)
            ++g_stats["EventsLoad"].agree;
    }
}

void TestSenses()
{
    SetUpCombat();
    SetUpMotion();
    SetUpDeaths();
    SetUpControl();
    SetUpSenses();
    // SENSES_ONLY: one part's tests alone (24a, 24b), for its mutation list.
    const char *only = std::getenv("SENSES_ONLY");
    const auto run = [&](const char *part) { return !only || std::strcmp(only, part) == 0; };
    if (run("24a")) {
        TestSensesLists();
        TestSensesSounds();
        TestSensesEvents();
    }
    if (run("24b")) {
        TestEventsQueue();
        TestEventsIds();
        TestEventsSaves();
        TestEventLoad();
    }
}

}  // namespace
