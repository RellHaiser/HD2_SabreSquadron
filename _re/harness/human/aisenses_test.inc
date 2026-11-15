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
float C_CameraModule::GetViewDistance() { AiUnexpected(__PRETTY_FUNCTION__); }   // (part 35b)

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
};
S_SensesAi *SensesAi() { return static_cast<S_SensesAi *>(g_pa); }

// The human's lists in the original's layout (the ToOriginal hook): enemies +0x264, friends +0x274.
void (*g_asBeforeLists)() = nullptr;
std::vector<void *> g_asOrigLists[2];
void ListsToOriginal()
{
    if (g_asBeforeLists)
        g_asBeforeLists();
    C_Human *h = g_h.human;
    const std::vector<C_Human *> *lists[2] = {&h->m_seenEnemies, &h->m_seenFriends};
    for (int l = 0; l < 2; ++l) {
        g_asOrigLists[l].clear();
        for (C_Human *p : *lists[l])
            g_asOrigLists[l].push_back(ToOrig(p));
        uint32_t v[4] = {0, 0, 0, 0};
        if (!g_asOrigLists[l].empty()) {
            v[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_asOrigLists[l].data()));
            v[2] = v[3] = v[1] + 4 * static_cast<uint32_t>(g_asOrigLists[l].size());
        }
        std::memcpy(o_human + (l ? 0x274 : 0x264), v, 16);
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

void TestAiSenses()
{
    SetUpSenses();
    TestSensesA();
}
}  // namespace
