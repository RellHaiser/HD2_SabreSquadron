// Part 35 of the human harness (HUMAN_AISENSES builds, build_aisenses.bat; included by human_test.cpp after
// aictl_test.inc): the AI controllers' senses (game/ai_senses.cpp) against the original's, on part 33's AI controllers
// (part 23's human, controllers, actors, weapons; part 21's other human).
//
// The events the senses make are logged by their fields where they are raised or tracked (C_HumanEvents::Raise and
// AddStimulus faked alike on both sides; the original's event pool, 0x008ac430, given its growth: its static
// initialiser did not run). The human's seen enemies and friends are MSVC6 vectors in the original's human (+0x264,
// +0x274), written from the port's lists on every copy (the ToOriginal hook). The sounds heard are each side's copy of
// the same remembered sound (the source mapped). The definitions' GetCarryMode and the actors' GetSeatFlags join part
// 23's fakes.
//
// Part 35a: the events of what the human perceived, OnHit, the turns toward sounds, the distances and directions.
// Part 35b: the sight tests and the targets.

#include "game/melee_tracker.h"

// ---- the events (the port's: C_HumanEvents' Raise and AddStimulus, the pool's allocation; ActorToId as
// human_events.cpp's)
namespace {
void LogEvent(const void *events, const char *what, const uint8_t *e, std::initializer_list<double> more = {})
{
    // (The fields both sides set: type, time left, age, the ids, +0x30, the position's x, y, z.)
    std::vector<double> nums;
    for (size_t at : {size_t(0), size_t(4), size_t(8), size_t(0xC), size_t(0x10), size_t(0x30)}) {
        int32_t w;
        std::memcpy(&w, e + at, 4);
        nums.push_back(w);
    }
    float pos[3];
    std::memcpy(pos, e + 0x20, 12);
    nums.insert(nums.end(), {pos[0], pos[1], pos[2]});
    nums.insert(nums.end(), more.begin(), more.end());
    S_Call c{NameOf(events) + "." + what, nums};
    g_calls.push_back(c);
}
}  // namespace
void C_HumanEvents::Raise(S_HumanEvent *event) { LogEvent(this, "Raise", reinterpret_cast<const uint8_t *>(event)); }
void C_HumanEvents::AddStimulus(int time, S_HumanEvent *event, bool keep)
{
    LogEvent(this, "AddStimulus", reinterpret_cast<const uint8_t *>(event), {double(time), double(keep)});
}
S_HumanEvent *AllocHumanEvent()
{
    void *p = _aligned_malloc(sizeof(S_HumanEvent), 16);
    // (Stale bytes, as a pool's: the senses set every field the events are logged by; as floats about 51,000.)
    std::memset(p, 0x47, sizeof(S_HumanEvent));
    return static_cast<S_HumanEvent *>(p);
}
int ActorToId(const C_Actor *actor) { return actor ? actor->GetNetId() : -1; }
float g_asViewDistance = 100.0f;
#ifndef HUMAN_PLAYERCTL   // (part 41's build: playerctl_test.inc's)
float C_CameraModule::GetViewDistance()
{
    Log(nullptr, "GetViewDistance");
    return g_asViewDistance;
}
#endif

namespace {
void AC OE_Raise(void *self, uint8_t *e) { LogEvent(self, "Raise", e); }
void AC OE_AddStimulus(void *self, int time, uint8_t *e, int keep) { LogEvent(self, "AddStimulus", e, {double(time), double(keep & 0xFF)}); }

// The definitions' carry mode (part 23's shared definition vtable: the port's slot, the original's 0x0C).
int g_asCarryMode = 0;
int CT_ DF_GetCarryMode(void *self)
{
    Log(self, "DefGetCarryMode");
    return g_asCarryMode;
}
// The actors' seat flags (part 23's actors: the port's slot, the original's 0x8C).
uint32_t g_asSeatFlags = 0;
uint32_t CT_ AC_GetSeatFlags(void *self, int seat)
{
    Log(self, "GetSeatFlags", {double(seat)});
    return g_asSeatFlags;
}

// Access to the AI controller's protected members.
struct S_SensesAi : S_TestAi {
    using C_AIController::CanSeeHeadOf;
    using C_AIController::FaceTowards;
    using C_AIController::GetAimDirTo;
    using C_AIController::GetDirToFrame;
    using C_AIController::GetDistanceTo;
    using C_AIController::GetNearestEnemy;
    using C_AIController::GetNearestEnemyDistance;
    using C_AIController::GetTargetDistance2;
    using C_AIController::GetViewDotTo;
    using C_AIController::GetWeaponPos;
    using C_AIController::PickTarget;
    using C_AIController::PickTargetFromSeat;
};
S_SensesAi *SensesAi() { return static_cast<S_SensesAi *>(g_pa); }

// The human's lists in the original's layout (the ToOriginal hook): enemies +0x264, friends +0x274, bodies +0x284 (part
// 43's).
void (*g_asBeforeLists)() = nullptr;
std::vector<void *> g_asOrigLists[3];
void ListsToOriginal()
{
    if (g_asBeforeLists)
        g_asBeforeLists();
    C_Human *h = g_h.human;
    const std::vector<C_Human *> *lists[3] = {&h->m_seenEnemies, &h->m_seenFriends, &h->m_seenBodies};
    static const size_t at[3] = {0x264, 0x274, 0x284};
    for (int l = 0; l < 3; ++l) {
        g_asOrigLists[l].clear();
        for (C_Human *p : *lists[l])
            g_asOrigLists[l].push_back(ToOrig(p));
        uint32_t v[4] = {0, 0, 0, 0};
        if (!g_asOrigLists[l].empty()) {
            v[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_asOrigLists[l].data()));
            v[2] = v[3] = v[1] + 4 * static_cast<uint32_t>(g_asOrigLists[l].size());
        }
        std::memcpy(o_human + at[l], v, 16);
    }
}

// The humans and actors the senses meet: the other human, the human itself, part 23's actors.
C_Actor *AsActor()
{
    const int k = I(0, 3);
    if (k == 0)
        return reinterpret_cast<C_Actor *>(g_portOther);
    if (k == 1)
        return g_h.human;
    return static_cast<C_Actor *>(PortActor(I(0, ACTOR_KIND_COUNT - 1)));
}
void *OrigOf(const void *p) { return p ? ToOrig(p) : nullptr; }

void RandomSenses()
{
    RandomAi();
    C_Human *h = g_h.human;
    // The lists: none, the other human, the human itself.
    h->m_seenEnemies.clear();
    h->m_seenFriends.clear();
    if (P(0.6f))
        h->m_seenEnemies.push_back(PortOther());
    if (P(0.2f))
        h->m_seenEnemies.push_back(h);
    if (P(0.5f))
        h->m_seenFriends.push_back(PortOther());
    // The other human's controller: a base one (no target) or the AI (its fire order's target).
    if (P(0.5f)) {
        PortOther()->m_controller = g_pa;
        std::memcpy(g_origOther + 0xB0, &g_oa, 4);
    } else {
        PortOther()->m_controller = g_pc2;
        std::memcpy(g_origOther + 0xB0, &g_oc2, 4);
    }
    g_asCarryMode = I(0, 8);
    g_asSeatFlags = P(0.5f) ? 0u : uint32_t(I(0, 0x3F));
    g_pa->m_lookAroundTime = P(0.5f) ? 0 : I(1, 3000);
    g_gameRole = P(0.5f) ? GAME_ROLE_LOCAL : P(0.5f) ? GAME_ROLE_SERVER : GAME_ROLE_CLIENT;
}

// A float's bits, for the results.
int FloatBitsOf(float f)
{
    int w;
    std::memcpy(&w, &f, 4);
    return w;
}

void SetUpSenses()
{
    SetUpAi();
    SetUpAiNetwork();
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x005adac0, FAKE(OE_Raise)},
        {0x005adee0, FAKE(OE_AddStimulus)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    *OriginalAt<int *>(0x008ac430 + 0x0C) = 16;
    g_defVt[VSlot(&C_ItemDef::GetCarryMode)] = reinterpret_cast<void *>(DF_GetCarryMode);
    g_defVt[0x0C / 4] = reinterpret_cast<void *>(DF_GetCarryMode);
    g_actPortVt[VSlot(&C_Actor::GetSeatFlags)] = reinterpret_cast<void *>(AC_GetSeatFlags);
    g_actOrigVt[0x8C / 4] = reinterpret_cast<void *>(AC_GetSeatFlags);
    g_asBeforeLists = g_toOriginalExtra;
    g_toOriginalExtra = ListsToOriginal;
}

void TestSensesA()
{
    std::printf("ai senses: events, hits, sounds, distances\n");
    C_Human *h = g_h.human;
    for (int i = 0; i < 20000 / QUICK; ++i) {
        RandomSenses();
        // (More sounds and hits: their rarer paths.)
        const int which = P(0.25f) ? (P(0.5f) ? 8 : 9) : I(0, 19);
        C_Actor *actor = AsActor();
        C_Human *human = P(0.6f) ? PortOther() : h;
        // (The aim tests: part 23's actors or the human itself; the other human is a copy whose bones GetAimPoint
        // cannot pick from.)
        if ((which == 12 || which == 15) && actor == reinterpret_cast<C_Actor *>(g_portOther))
            actor = h;
        // (Network games: the sounds' sources are humans, IsFriendOf takes them as humans.)
        if ((which == 6 || which == 8) && (g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL)
            actor = human;
        void *oActor = OrigOf(actor), *oHuman = OrigOf(human);
        switch (which) {
        case 0:
            AiCall("OnKilledEnemy", [&] { g_pa->OnKilledEnemy(human); return 0; }, [&](void *) { OAi<void>(0x00573fb0, oHuman); return 0; });
            break;
        case 1:
            AiCall("OnEnemySeen", [&] { g_pa->OnEnemySeen(human); return 0; }, [&](void *) { OAi<void>(0x00574070, oHuman); return 0; });
            break;
        case 2:
            AiCall("OnEnemyLost", [&] { g_pa->OnEnemyLost(human); return 0; }, [&](void *) { OAi<void>(0x00574130, oHuman); return 0; });
            break;
        case 3:
            AiCall("OnBodySeen", [&] { g_pa->OnBodySeen(human); return 0; }, [&](void *) { OAi<void>(0x005741f0, oHuman); return 0; });
            break;
        case 4:
            AiCall("OnBodyHeard", [&] { g_pa->OnBodyHeard(actor); return 0; }, [&](void *) { OAi<void>(0x00574280, oActor); return 0; });
            break;
        case 5: {
            // (The source: none or a human, whose controller's last target is called for.)
            C_Actor *source = P(0.2f) ? nullptr : static_cast<C_Actor *>(human);
            void *oSource = OrigOf(source);
            AiCall("OnFriendHeard", [&] { g_pa->OnFriendHeard(source); return 0; }, [&](void *) { OAi<void>(0x00574310, oSource); return 0; });
            break;
        }
        case 6: {
            C_Actor *source = P(0.2f) ? nullptr : actor;
            void *oSource = OrigOf(source);
            AiCall("OnLoudSound", [&] { g_pa->OnLoudSound(source); return 0; }, [&](void *) { OAi<void>(0x00574480, oSource); return 0; });
            break;
        }
        case 7: {
            const S_vector v(U(-5, 5), U(-5, 5), U(-5, 5));
            AiCall("OnShotAt", [&] { g_pa->OnShotAt(actor, v); return 0; }, [&](void *) { OAi<void>(0x00574cb0, oActor, &v); return 0; });
            break;
        }
        case 8: {
            // A sound of each chapter; the AI's type decides whether it turns (enemies, snipers and the squad do not).
            alignas(16) S_RememberedSound ps;
            // (Around the human's body, every side; now and then anywhere.)
            ps.pos = P(0.2f) ? S_vector(U(-30, 30), U(-2, 4), U(-30, 30))
                             : h->GetBodyFrame()->GetWorldPos() + S_vector(U(-15, 15), U(-2, 2), U(-15, 15));
            ps.posW = P(0.5f) ? 1.0f : U(-2, 2);
            ps.chapter = I(0, 10);
            ps.source = P(0.1f) && ps.chapter >= 8 ? nullptr : static_cast<C_Actor *>(P(0.5f) ? static_cast<C_Actor *>(human) : actor);
            ps.time = uint32_t(I(0, 100000));
            ps.refreshed = P(0.5f);
            alignas(16) uint8_t os[sizeof ps];
            std::memcpy(os, &ps, sizeof ps);
            void *oSource = OrigOf(ps.source);
            std::memcpy(os + offsetof(S_RememberedSound, source), &oSource, 4);
            g_pa->m_type = static_cast<E_HumanController>(CONTROLLER_TYPES[I(0, 6)]);
            if (P(0.5f))
                g_pa->m_lookAroundTime = 0;
            AiCall("OnSoundHeard", [&] { g_pa->OnSoundHeard(&ps); return 0; }, [&](void *) { OAi<void>(0x00574d70, os); return 0; });
            break;
        }
        case 9: {
            // (In a seat with flags or a seat weapon now and then; the head's and the hands' items: part 21's and 23's.)
            const int type = I(0, 6), zone = P(0.3f) ? 0xB : P(0.5f) ? 3 : I(0, 12), health = I(-5, 100);
            C_Actor *attacker = P(0.2f) ? nullptr : actor;
            void *oAttacker = OrigOf(attacker);
            const S_vector pos(U(-20, 20), U(-2, 3), U(-20, 20));
            const S_vector dir(U(-1, 1), U(-1, 1), U(-1, 1));
            h->m_vehicle = P(0.7f) ? nullptr : static_cast<C_Actor *>(PortActor(I(3, 6)));
            h->m_stats[STAT_HEALTH] = P(0.3f) ? I(-5, 0) : P(0.3f) ? 1 : I(1, 100);
            h->m_position = {U(-20, 20), U(-2, 3), U(-20, 20), 1.0f};
            // (The hands' weapons knocked away: an SMG, a rifle, a light machine gun.)
            static const int knocked[] = {ITEM_CARRY_SMG, ITEM_CARRY_RIFLE, ITEM_CARRY_LMG};
            if (P(0.6f))
                g_asCarryMode = knocked[I(0, 2)];
            g_pa->m_type = static_cast<E_HumanController>(CONTROLLER_TYPES[I(0, 6)]);
            AiCall("OnHit", [&] { g_pa->OnHit(type, zone, health, attacker, pos, dir); return 0; },
                   [&](void *) { OAi<void>(0x00574560, type, zone, health, oAttacker, &pos, &dir); return 0; });
            break;
        }
        case 10: {
            C_Actor *target = P(0.1f) ? nullptr : actor;
            void *oTarget = OrigOf(target);
            AiCall("GetDistanceTo", [&] { return FloatBitsOf(SensesAi()->GetDistanceTo(target)); },
                   [&](void *) {
                       typedef float(AC * T)(void *, const void *);
                       return FloatBitsOf(OriginalAt<T>(0x00573240)(g_oa, oTarget));
                   });
            break;
        }
        case 11:
            AiCall("GetNearestEnemyDistance", [&] { return FloatBitsOf(SensesAi()->GetNearestEnemyDistance()); },
                   [&](void *) {
                       typedef float(AC * T)(void *);
                       return FloatBitsOf(OriginalAt<T>(0x005733e0)(g_oa));
                   });
            break;
        case 12:
            AiCall("GetAimDirTo",
                   [&] {
                       const S_vector4 d = SensesAi()->GetAimDirTo(actor);
                       return VectorHash(S_vector(d.x, d.y, d.z));
                   },
                   [&](void *) {
                       S_vector4 out;
                       typedef void(AC * T)(void *, S_vector4 *, const void *);
                       OriginalAt<T>(0x00575450)(g_oa, &out, oActor);
                       return VectorHash(S_vector(out.x, out.y, out.z));
                   });
            break;
        case 13: {
            const I3D_frame *frame = actor->GetFrame();
            AiCall("GetDirToFrame",
                   [&] {
                       const S_vector4 d = SensesAi()->GetDirToFrame(frame);
                       return VectorHash(S_vector(d.x, d.y, d.z));
                   },
                   [&](void *) {
                       S_vector4 out;
                       typedef void(AC * T)(void *, S_vector4 *, const void *);
                       OriginalAt<T>(0x00575670)(g_oa, &out, frame);
                       return VectorHash(S_vector(out.x, out.y, out.z));
                   });
            break;
        }
        case 14:
            g_pa->m_fire.target = actor;
            AiCall("GetTargetDistance2", [&] { return FloatBitsOf(SensesAi()->GetTargetDistance2()); },
                   [&](void *) {
                       typedef float(AC * T)(void *);
                       return FloatBitsOf(OriginalAt<T>(0x00575350)(g_oa));
                   });
            break;
        case 15:
            AiCall("GetViewDotTo", [&] { return FloatBitsOf(SensesAi()->GetViewDotTo(actor)); },
                   [&](void *) {
                       typedef float(AC * T)(void *, const void *);
                       return FloatBitsOf(OriginalAt<T>(0x00575410)(g_oa, oActor));
                   });
            break;
        case 16: {
            S_vector4 pp = {9, 9, 9, 9}, op = {9, 9, 9, 9};
            // (A component of every kind in the hands item's definition, +0x48 on both sides, its kind +8; the
            // weapons' frame the gun model with its "shdum" child, or the camera for the others.)
            alignas(16) static uint8_t component[0x20];
            uint8_t *def = nullptr;
            if (P(0.8f)) {
                const int k = I(0, 15);
                g_pa->m_handsItem = reinterpret_cast<C_Item *>(g_wornItems[k]);
                std::memcpy(&def, g_wornItems[k] + 0x04, 4);
                const int kind = I(0, 4);
                std::memcpy(component + 8, &kind, 4);
                uint8_t *c = P(0.2f) ? nullptr : component;
                std::memcpy(def + 0x48, &c, 4);
                const bool weapon = c && (kind == ICK_FIREARM || kind == ICK_PROJECTILE_WEAPON);
                g_cs.itemFrame = (weapon || P(0.5f)) ? g_shdumParent : g_cameraFrame;
                g_shdumParent->SetPos(S_vector(U(-30, 30), U(0, 5), U(-30, 30)));
            } else {
                g_pa->m_handsItem = nullptr;
            }
            AiCall("GetWeaponPos",
                   [&] {
                       SensesAi()->GetWeaponPos(&pp);
                       return VectorHash(S_vector(pp.x, pp.y, pp.z)) + 7 * pp.w;
                   },
                   [&](void *) {
                       OAi<void>(0x005715f0, &op);
                       return VectorHash(S_vector(op.x, op.y, op.z)) + 7 * op.w;
                   });
            if (def)
                std::memset(def + 0x48, 0, 4);
            break;
        }
        case 17: {
            const S_vector4 from = AiDir(), to = AiDir();
            // (The original writes the facing's w from a stack temp: the port's kept there.)
            AiCall("FaceTowards", [&] { SensesAi()->FaceTowards(from, to); return 0; },
                   [&](void *) {
                       OAi<void>(0x00570ac0, &from, &to);
                       std::memcpy(g_oa + 0x2C, &g_pa->m_facingDir.w, 4);
                       return 0;
                   });
            break;
        }
        case 18: {
            // (The human itself: the other human's copy has no body frame.)
            C_Human *target = P(0.1f) ? nullptr : h;
            void *oTarget = OrigOf(target);
            AiCall("CanSeeHeadOf", [&] { return int(SensesAi()->CanSeeHeadOf(target)); },
                   [&](void *) { return int(OAi<uint32_t>(0x00575150, oTarget) & 0xFF); });
            break;
        }
        default: {
            // (A route: the enemy's direction against the first waypoint's.)
            if (!h->m_seenEnemies.empty() && g_pa->m_waypoints.empty())
                g_pa->m_waypoints.push_back(AllocWaypoint(I(-1, 300), AiDir(), 0, 0, 1.0f));
            float pd = 0, pdot = 0, od = 0, odot = 0;
            AiCall("GetNearestEnemy",
                   [&] {
                       SensesAi()->GetNearestEnemy(&pd, &pdot);
                       return FloatBitsOf(pd) ^ (FloatBitsOf(pdot) * 3);
                   },
                   [&](void *) {
                       OAi<void>(0x00573500, &od, &odot);
                       return FloatBitsOf(od) ^ (FloatBitsOf(odot) * 3);
                   });
            break;
        }
        }
    }
}

// ---- part 35b: the sight tests (TestSightOfHuman, TestSightOfMeleeTracker) and the targets (PickTarget,
// PickTargetFromSeat)
//
// Three humans: the tested one, part 21's other and a third copy of it, each on its own frame (also its body frame:
// the original's +0x260) around the tested human, with its own health, controller (the AI's or the second base
// controller as the player's or a remote), vehicle, sight target id, and its own answers to the shared fakes
// (unconscious, surrendered, view direction, the sight module's visible parts by target id). A car (its +0x5E0 flag
// past part 23's actors) carries the seat weapons: HasSeatWeapon, GetSeatWeaponAim, GetSeatWeaponDef (a definition
// with a component of every kind) and CanSeatWeaponAim logged on both sides (the original's 0x98, 0xA4 with its
// hidden return pointer, 0xB4, 0xBC). The camera module's view distance: the port's member, the original's slot 0x58
// of self_test's fake module. A MeleeTracker: its frame and body frame (+0x544 in both).
alignas(16) uint8_t g_portThird[sizeof(C_Human) + 0x100], g_origThird[0x1000];
alignas(16) uint8_t g_asCarPort[sizeof(C_Car) + 0x100], g_asCarOrig[0xA00];   // (part 46: the car's speed +0x958)
size_t g_asCarFlagAt = 0;                               // the port's C_Car::GetUnknown5E0 field (private: found)
alignas(16) uint8_t g_asMeleePort[sizeof(C_MeleeTracker) + 0x100], g_asMeleeOrig[0x800];
alignas(16) uint8_t g_asSeatComponent[0x20];
void *g_asCarPortVt[200], *g_asCarOrigVt[200];
I3D_frame *g_asThirdFrame, *g_asMeleeFrame, *g_asMeleeBody, *g_asBodyFrame;
C_Human *PortThird() { return reinterpret_cast<C_Human *>(g_portThird); }

float AC OC_GetViewDistance(void *)
{
    Log(nullptr, "GetViewDistance");
    return g_asViewDistance;
}

// The seat weapon's answers.
struct S_AsSeat {
    bool hasWeapon = false;
    S_vector aim;
    bool def = true;
    bool canAim = false;
    float aimSplit = 0.0f;                              // (the answer flips at this x: per enemy)
} g_asSeat;
struct S_AsSeatFake {
    virtual bool HasWeapon(int seat);
    virtual S_vector Aim(int seat);
    virtual C_ItemDef *Def(int seat);
    virtual bool CanAim(int seat, float *pos);
};
bool S_AsSeatFake::HasWeapon(int seat)
{
    Log(this, "HasSeatWeapon", {double(seat)});
    return g_asSeat.hasWeapon;
}
S_vector S_AsSeatFake::Aim(int seat)
{
    Log(this, "GetSeatWeaponAim", {double(seat)});
    return g_asSeat.aim;
}
C_ItemDef *S_AsSeatFake::Def(int seat)
{
    Log(this, "GetSeatWeaponDef", {double(seat)});
    return g_asSeat.def ? reinterpret_cast<C_ItemDef *>(g_wornDefs[0]) : nullptr;
}
bool S_AsSeatFake::CanAim(int seat, float *pos)
{
    Log(this, "CanSeatWeaponAim", {double(seat), pos[0], pos[1], pos[2]});
    return g_asSeat.canAim != (pos[0] > g_asSeat.aimSplit);
}
int AC OS_HasSeatWeapon(void *self, int seat)
{
    Log(self, "HasSeatWeapon", {double(seat)});
    return g_asSeat.hasWeapon;
}
float *AC OS_GetSeatWeaponAim(void *self, float *out, int seat)
{
    Log(self, "GetSeatWeaponAim", {double(seat)});
    out[0] = g_asSeat.aim.x;
    out[1] = g_asSeat.aim.y;
    out[2] = g_asSeat.aim.z;
    return out;
}
void *AC OS_GetSeatWeaponDef(void *self, int seat)
{
    Log(self, "GetSeatWeaponDef", {double(seat)});
    return g_asSeat.def ? static_cast<void *>(g_wornDefs[0]) : nullptr;
}
int AC OS_CanSeatWeaponAim(void *self, int seat, float *pos)
{
    Log(self, "CanSeatWeaponAim", {double(seat), pos[0], pos[1], pos[2]});
    return g_asSeat.canAim != (pos[0] > g_asSeat.aimSplit);
}

// The humans' own answers (index 0 the tested human, 1 the other, 2 the third), the visible parts by target id.
S_HumanAnswers g_asAnswers[3];
uint32_t g_asParts[8];
const S_HumanAnswers *AsAnswers(const void *p)
{
    if (p == g_h.human || p == o_human)
        return &g_asAnswers[0];
    if (p == g_portOther || p == g_origOther)
        return &g_asAnswers[1];
    if (p == g_portThird || p == g_origThird)
        return &g_asAnswers[2];
    return nullptr;
}
uint32_t AsPartsOf(int target) { return target >= 0 && target < 8 ? g_asParts[target] : 0u; }

void SetUpSensesB()
{
    S_quat none;
    none.Identity();
    g_asThirdFrame = NewFrame(FRAME_DUMMY, "thirdModel", S_vector(10, 1, 10), none, Box(S_vector(0.3f, 0.9f, 0.3f)), "", g_sector);
    g_asMeleeFrame = NewFrame(FRAME_DUMMY, "meleeModel", S_vector(5, 0, 5), none, Box(S_vector(0.3f, 0.3f, 0.6f)), "", g_sector);
    g_asMeleeBody = NewFrame(FRAME_DUMMY, "meleeBody", S_vector(0, 0.5f, 0), none, Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_asMeleeFrame);
    g_asBodyFrame = NewFrame(FRAME_DUMMY, "testedBody", S_vector(0, 1.4f, 0), none, Box(S_vector(0.1f, 0.1f, 0.1f)), "", g_sector);
    g_names[g_portThird] = g_names[g_origThird] = "third";
    g_extraPointers.push_back({g_portThird, g_origThird});
    // The car: part 23's actor vtables, the seat weapon's slots the fakes.
    std::memcpy(g_asCarPortVt, g_actPortVt, sizeof g_asCarPortVt);
    std::memcpy(g_asCarOrigVt, g_actOrigVt, sizeof g_asCarOrigVt);
    S_AsSeatFake fake;
    void **fakeVt = *reinterpret_cast<void ***>(&fake);
    g_asCarPortVt[VSlot(&C_Actor::HasSeatWeapon)] = fakeVt[0];
    g_asCarPortVt[VSlot(&C_Actor::GetSeatWeaponAim)] = fakeVt[1];
    g_asCarPortVt[VSlot(&C_Actor::GetSeatWeaponDef)] = fakeVt[2];
    g_asCarPortVt[VSlot(&C_Actor::CanSeatWeaponAim)] = fakeVt[3];
    g_asCarOrigVt[0x98 / 4] = reinterpret_cast<void *>(OS_HasSeatWeapon);
    g_asCarOrigVt[0xA4 / 4] = reinterpret_cast<void *>(OS_GetSeatWeaponAim);
    g_asCarOrigVt[0xB4 / 4] = reinterpret_cast<void *>(OS_GetSeatWeaponDef);
    g_asCarOrigVt[0xBC / 4] = reinterpret_cast<void *>(OS_CanSeatWeaponAim);
    for (size_t at = 4; at + 4 <= sizeof(C_Car) && !g_asCarFlagAt; at += 4) {
        std::memset(g_asCarPort, 0, sizeof g_asCarPort);
        g_asCarPort[at] = 1;
        if (reinterpret_cast<C_Car *>(g_asCarPort)->GetUnknown5E0() == 1)
            g_asCarFlagAt = at;
    }
    CHECK(g_asCarFlagAt != 0);
    std::memset(g_asCarPort, 0, sizeof g_asCarPort);
    *reinterpret_cast<void ***>(g_asCarPort) = g_asCarPortVt;
    *reinterpret_cast<void ***>(g_asCarOrig) = g_asCarOrigVt;
    reinterpret_cast<C_Actor *>(g_asCarPort)->m_frame = g_targetFrame;
    std::memcpy(g_asCarOrig + 0x20, &g_targetFrame, 4);
    g_names[g_asCarPort] = g_names[g_asCarOrig] = "seatCar";
    g_extraPointers.push_back({g_asCarPort, g_asCarOrig});
    // The MeleeTracker.
    auto *melee = reinterpret_cast<C_MeleeTracker *>(g_asMeleePort);
    reinterpret_cast<C_Actor *>(melee)->m_type = ACTOR_MELEE_TRACKER;
    reinterpret_cast<C_Actor *>(melee)->m_frame = g_asMeleeFrame;
    melee->m_bodyFrame = g_asMeleeBody;
    const int meleeType = ACTOR_MELEE_TRACKER;
    std::memcpy(g_asMeleeOrig + 0x18, &meleeType, 4);
    std::memcpy(g_asMeleeOrig + 0x20, &g_asMeleeFrame, 4);
    std::memcpy(g_asMeleeOrig + 0x544, &g_asMeleeBody, 4);
    g_names[g_asMeleePort] = g_names[g_asMeleeOrig] = "melee";
    g_extraPointers.push_back({g_asMeleePort, g_asMeleeOrig});
    g_camModuleVt[0x58 / 4] = reinterpret_cast<void *>(OC_GetViewDistance);
}

// A human of the three placed around the tested one (both layouts: frame +0x20, controller +0xB0, health +0xEC,
// vehicle +0x160, seat +0x164, sight target id +0x25C, body frame +0x260).
void PlaceHuman(C_Human *p, uint8_t *o, I3D_frame *frame, int sightTarget)
{
    const S_vector &at = g_h.frame->GetWorldPos();
    const float spread = P(0.2f) ? 120.0f : 40.0f;
    // (Now and then much higher or lower, or exactly 1 m above: the distance's exact case.)
    frame->SetPosInline(P(0.05f) ? at + S_vector(0, 1, 0)
                                 : at + S_vector(U(-spread, spread), P(0.3f) ? U(-10, 10) : U(-1, 2), U(-spread, spread)));
    UpdateTree(frame);
    p->m_frame = frame;
    p->m_bodyFrame = frame;
    std::memcpy(o + 0x20, &frame, 4);
    std::memcpy(o + 0x260, &frame, 4);
    p->m_stats[STAT_HEALTH] = P(0.15f) ? I(-5, 0) : P(0.1f) ? 1 : I(1, 800);
    std::memcpy(o + 0xEC, &p->m_stats[STAT_HEALTH], 4);
    // (The AI's controller, or the second base one as a player's: the original reads +0xD0 of any other type's, past
    // the base's 0xC0 bytes; the port's FireTargetOf answers none for them.)
    const bool ai = P(0.5f);
    p->m_controller = ai ? static_cast<C_HumanController *>(g_pa) : g_pc2;
    void *oc = ai ? static_cast<void *>(g_oa) : static_cast<void *>(g_oc2);
    std::memcpy(o + 0xB0, &oc, 4);
    p->m_vehicle = P(0.3f) ? reinterpret_cast<C_Actor *>(g_asCarPort) : nullptr;
    void *ov = p->m_vehicle ? static_cast<void *>(g_asCarOrig) : nullptr;
    std::memcpy(o + 0x160, &ov, 4);
    p->m_seat = I(0, 3);
    std::memcpy(o + 0x164, &p->m_seat, 4);
    p->m_sightTargetId = sightTarget;
    std::memcpy(o + 0x25C, &sightTarget, 4);
}

void RandomSensesB()
{
    RandomSenses();
    C_Human *h = g_h.human;
    // The third human: a copy of the other (each side's), then each placed.
    std::memcpy(g_portThird, g_portOther, sizeof(C_Human));
    std::memcpy(g_origThird, g_origOther, sizeof g_origThird);
    PlaceHuman(PortOther(), g_origOther, g_otherFrame, 1);
    PlaceHuman(PortThird(), g_origThird, g_asThirdFrame, 2);
    h->m_sightTargetId = 3;
    h->m_sightId = I(0, 5);
    // The tested human's own: its body frame its frame (the sight tests measure from it), its vehicle, seat, role,
    // sight range.
    // (Its own body frame where the human is, turned: now and then tilted, or not turned at all, the body's direction
    // then exactly +z.)
    S_quat turn;
    if (P(0.1f))
        turn.Identity();
    else if (P(0.3f))
        turn.Make(S_vector(U(-1, 1), U(-1, 1), U(-1, 1)).Normalized(), U(0, 6.2831f));
    else
        turn.Make(S_vector(0, 1, 0), U(0, 6.2831f));
    g_asBodyFrame->SetPosInline(g_h.frame->GetWorldPos() + S_vector(0, 1.4f, 0));
    g_asBodyFrame->SetRot(turn);
    UpdateTree(g_asBodyFrame);
    h->m_bodyFrame = g_asBodyFrame;
    h->m_vehicle = P(0.4f) ? reinterpret_cast<C_Actor *>(g_asCarPort) : nullptr;
    h->m_seat = I(0, 3);
    h->m_role = static_cast<E_HumanRole>(I(0, 5));
    h->m_stats[STAT_SIGHT_RANGE] = P(0.3f) ? I(0, 30) : I(30, 200);
    // The lists: the three humans, some twice.
    h->m_seenEnemies.clear();
    h->m_seenFriends.clear();
    C_Human *const humans[3] = {h, PortOther(), PortThird()};
    for (int k = P(0.15f) ? 0 : P(0.2f) ? 1 : I(2, 5); k > 0; --k)
        h->m_seenEnemies.push_back(humans[P(0.15f) ? 0 : I(1, 2)]);
    for (int k = I(0, 3); k > 0; --k)
        h->m_seenFriends.push_back(humans[I(0, 2)]);
    // The controllers: the AI's type any (enemies with it are fired at by its orders), the base one the player's; the
    // AI's fire order's target one of the humans.
    // (An AI controller has an AI type: FireTargetOf answers none for the others, where the original reads +0xD0.)
    static const E_HumanController aiTypes[] = {HUMAN_CONTROLLER_SQUAD, HUMAN_CONTROLLER_ENEMY, HUMAN_CONTROLLER_AI,
                                                HUMAN_CONTROLLER_NEUTRAL, HUMAN_CONTROLLER_SNIPER};
    g_pa->m_type = aiTypes[I(0, 4)];
    g_pc2->m_type = HUMAN_CONTROLLER_PLAYER;
    for (C_HumanController *c : {static_cast<C_HumanController *>(g_pa), g_pc2}) {
        c->m_quietTime = P(0.5f) ? I(0, 399) : I(400, 20000);
        // (Dead or deaf: no sight.)
        c->m_moveFlags = (c->m_moveFlags & ~uint32_t(MOVE_LYING | MOVE_CROUCHING | MOVE_DEAD | MOVE_DEAF)) |
                         (P(0.3f) ? MOVE_LYING : P(0.4f) ? MOVE_CROUCHING : 0u) | (P(0.05f) ? MOVE_DEAD : 0u) |
                         (P(0.05f) ? MOVE_DEAF : 0u);
    }
    g_pa->m_fire.target = P(0.3f) ? nullptr : P(0.5f) ? static_cast<C_Actor *>(h) : static_cast<C_Actor *>(humans[I(1, 2)]);
    g_pa->m_fire.orderTarget = P(0.3f) ? nullptr : static_cast<C_Actor *>(humans[I(0, 2)]);
    // (The view now and then exactly along an axis: the angles exactly 0, pi or a half.)
    static const S_vector axes[] = {S_vector(0, 0, 1), S_vector(0, 0, -1), S_vector(1, 0, 0), S_vector(-1, 0, 0)};
    g_pa->m_viewDir = ToVector4(P(0.1f) ? axes[I(0, 3)] : S_vector(U(-1, 1), U(-0.3f, 0.3f), U(-1, 1)).Normalized(), 0.0f);
    // The answers.
    for (S_HumanAnswers &a : g_asAnswers) {
        a.unconscious = P(0.1f);
        a.surrendered = P(0.1f);
        a.viewDir = S_vector(U(-1, 1), U(-0.3f, 0.3f), U(-1, 1)).Normalized();
    }
    // (The players' views at the tested human now and then: being watched, nearly or exactly.)
    for (int k = 1; k < 3; ++k)
        if (P(0.4f)) {
            const S_vector to = g_h.frame->GetWorldPos() - humans[k]->GetFrame()->GetWorldPos();
            g_asAnswers[k].viewDir = (to.Normalized() + (P(0.5f) ? S_vector(U(-0.1f, 0.1f), U(-0.1f, 0.1f), U(-0.1f, 0.1f)) : S_vector(0, 0, 0))).Normalized();
        }
    for (uint32_t &parts : g_asParts)
        parts = P(0.2f) ? 0u : uint32_t(I(1, 0x1F)) | (P(0.3f) ? 0x20u : 0u);   // (never only other bits: the original's GetAimPoint divides by 0)
    // The car and the seat weapon.
    auto *car = reinterpret_cast<C_Actor *>(g_asCarPort);
    const int type = P(0.7f) ? ACTOR_CAR : ACTOR_STATIC_WEAPON;
    car->m_type = static_cast<E_ActorType>(type);
    std::memcpy(g_asCarOrig + 0x18, &type, 4);
    const int flag = P(0.5f) ? 0 : I(1, 2);
    std::memcpy(g_asCarPort + g_asCarFlagAt, &flag, 4);
    std::memcpy(g_asCarOrig + 0x5E0, &flag, 4);
    g_asSeat.hasWeapon = P(0.6f);
    static const S_vector aimAxes[] = {S_vector(0, 0, 1), S_vector(0, 0, -1), S_vector(1, 0, 0), S_vector(-1, 0, 0)};
    g_asSeat.aim = P(0.15f) ? aimAxes[I(0, 3)] : S_vector(U(-1, 1), U(-0.3f, 0.3f), U(-1, 1)).Normalized();
    g_asSeat.def = P(0.85f);
    g_asSeat.canAim = P(0.5f);
    g_asSeat.aimSplit = g_h.frame->GetWorldPos().x + U(-20, 20);
    const int kind = P(0.4f) ? ICK_PROJECTILE_WEAPON : I(0, 4);
    std::memcpy(g_asSeatComponent + 8, &kind, 4);
    uint8_t *component = g_asSeatComponent;
    std::memcpy(g_wornDefs[0] + 0x48, &component, 4);
    g_asViewDistance = P(0.3f) ? U(5, 60) : U(60, 300);
    // The MeleeTracker around the human.
    const S_vector &at = g_h.frame->GetWorldPos();
    const float spread = P(0.2f) ? 120.0f : 40.0f;
    g_asMeleeFrame->SetPosInline(at + S_vector(U(-spread, spread), U(-1, 1), U(-spread, spread)));
    UpdateTree(g_asMeleeFrame);
}

void TestSensesB()
{
    std::printf("ai senses: sight tests, targets\n");
    C_Human *h = g_h.human;
    g_humanAnswers = AsAnswers;
    g_visiblePartsOf = AsPartsOf;
    // (The outcomes, printed: the sight tests' tested and visible counts, the targets picked by index.)
    int sight[2][3] = {}, picked[2][4] = {};
    for (int i = 0; i < 20000 / QUICK; ++i) {
        RandomSensesB();
        const int which = I(0, 3);
        auto index = [&](const void *p) { return p == nullptr ? 0 : AsAnswers(p) ? int(AsAnswers(p) - g_asAnswers) + 1 : 9; };
        switch (which) {
        case 0: {
            C_Human *const humans[3] = {h, PortOther(), PortThird()};
            C_Human *target = humans[P(0.1f) ? 0 : I(1, 2)];
            void *oTarget = ToOrig(target);
            int ps = 7, os = 7;
            float pv = 9.0f, ov = 9.0f;
            AiCall("TestSightOfHuman",
                   [&] {
                       g_pa->TestSightOfHuman(target, &ps, &pv);
                       ++sight[0][ps == 99 ? /* tested */ (pv > 0.0f ? 2 : 1) : 0];
                       return ps * 31 ^ FloatBitsOf(pv);
                   },
                   [&](void *) {
                       OAi<void>(0x00570280, oTarget, &os, &ov);
                       return os * 31 ^ FloatBitsOf(ov);
                   });
            break;
        }
        case 1: {
            int ps = 7, os = 7;
            float pv = 9.0f, ov = 9.0f;
            // (The controlled actor a MeleeTracker or another kind now and then: the type only, set back after.)
            h->m_type = P(0.1f) ? ACTOR_MELEE_TRACKER : P(0.05f) ? ACTOR_CAR : ACTOR_HUMAN;
            // (Mostly the squad's AI: only the squad looks out for MeleeTrackers.)
            if (P(0.7f))
                g_pa->m_type = HUMAN_CONTROLLER_SQUAD;
            AiCall("TestSightOfMeleeTracker",
                   [&] {
                       g_pa->TestSightOfMeleeTracker(reinterpret_cast<C_Actor *>(g_asMeleePort), &ps, &pv);
                       ++sight[1][ps == 99 ? /* tested */ (pv > 0.0f ? 2 : 1) : 0];
                       return ps * 31 ^ FloatBitsOf(pv);
                   },
                   [&](void *) {
                       OAi<void>(0x0056fc80, static_cast<void *>(g_asMeleeOrig), &os, &ov);
                       return os * 31 ^ FloatBitsOf(ov);
                   });
            h->m_type = ACTOR_HUMAN;
            break;
        }
        case 2: {
            // (A direction at one of the humans now and then, nearly or exactly: within the 25 degrees.)
            S_vector dir = S_vector(U(-1, 1), U(-0.3f, 0.3f), U(-1, 1)).Normalized();
            if (P(0.6f)) {
                C_Human *const humans[3] = {h, PortOther(), PortThird()};
                const S_vector to = humans[I(1, 2)]->GetFrame()->GetWorldPos() - h->GetFrame()->GetWorldPos();
                dir = (to.Normalized() + (P(0.5f) ? S_vector(U(-0.3f, 0.3f), U(-0.1f, 0.1f), U(-0.3f, 0.3f)) : S_vector(0, 0, 0))).Normalized();
            }
            const bool withDir = P(0.4f);
            AiCall("PickTarget",
                   [&] {
                       const int r = index(SensesAi()->PickTarget(withDir ? &dir : nullptr));
                       ++picked[0][r & 3];
                       return r;
                   },
                   [&](void *) { return index(OAi<void *>(0x005710b0, withDir ? &dir : nullptr)); });
            break;
        }
        default:
            // (Mostly in the car.)
            if (P(0.8f))
                h->m_vehicle = reinterpret_cast<C_Actor *>(g_asCarPort);
            AiCall("PickTargetFromSeat",
                   [&] {
                       const int r = index(SensesAi()->PickTargetFromSeat());
                       ++picked[1][r & 3];
                       return r;
                   },
                   [&](void *) { return index(OAi<void *>(0x00570ca0)); });
            break;
        }
    }
    g_humanAnswers = nullptr;
    g_visiblePartsOf = nullptr;
    std::memset(g_wornDefs[0] + 0x48, 0, 4);
    std::printf("  sight of humans: %d skipped, %d not visible, %d visible; of the MeleeTracker: %d, %d, %d\n", sight[0][0],
                sight[0][1], sight[0][2], sight[1][0], sight[1][1], sight[1][2]);
    std::printf("  picked (none, the human, the other, the third): %d %d %d %d; from the seat: %d %d %d %d\n", picked[0][0],
                picked[0][1], picked[0][2], picked[0][3], picked[1][0], picked[1][1], picked[1][2], picked[1][3]);
}

void TestAiSenses()
{
    SetUpSenses();
    SetUpSensesB();
    TestSensesA();
    TestSensesB();
}
}  // namespace
