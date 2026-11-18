// Part 38 of the human harness (HUMAN_SNIPER builds, build_sniper.bat; included by human_test.cpp after
// aicombat_test.inc, whose fakes and helpers it uses): the sniper AI (game/sniper_controller.cpp) against the
// original's, on the derived AI controllers' common harness (derived_ai.inc: part 33's AI pair, the fakes, the events).
//
// The port's sniper (a test class over C_SniperController: ExecuteCommand logged) is the AI pair's port side; the
// original's is the AI constructor's object with a copy of the sniper's vtable (0x00811618). The sniper's own fields
// (+0x140 .. +0x180, the same offsets on both sides; its two vectors MSVC6 ones in the original) are converted and
// compared around each call.
#include "derived_ai.inc"

namespace {
typedef C_SniperController S_Sn;
DA_ROB(SnUpdateTimers, void (S_Sn::*)(int), &S_Sn::UpdateTimers)
DA_ROB(SnDropHandsItem, void (S_Sn::*)(), &S_Sn::DropHandsItem)
DA_ROB(SnUpdateFiring, void (S_Sn::*)(int), &S_Sn::UpdateFiring)
DA_ROB(SnUpdateTask, S_AITask *(S_Sn::*)(int), &S_Sn::UpdateTask)
DA_ROB(SnCheckDone, void (S_Sn::*)(int), &S_Sn::CheckDone)
DA_ROB(SnRunTask, void (S_Sn::*)(int), &S_Sn::RunTask)
DA_ROB(SnStartTask, void (S_Sn::*)(), &S_Sn::StartTask)
DA_ROB(SnUpdateZombie, void (S_Sn::*)(int), &S_Sn::UpdateZombie)
DA_ROB(SnUpdateSniper, void (S_Sn::*)(int), &S_Sn::UpdateSniper)
DA_ROB(SnTestSightOfTarget, void (S_Sn::*)(), &S_Sn::TestSightOfTarget)
DA_ROB(SnTakeCoverStance, void (S_Sn::*)(const S_Checkpoint *), &S_Sn::TakeCoverStance)
DA_ROB(SnHasSomethingToDo, bool (S_Sn::*)(), &S_Sn::HasSomethingToDo)
DA_ROB(SnSeesEnemies, bool (S_Sn::*)(), &S_Sn::SeesEnemies)
DA_ROB(SnSeesTarget, bool (S_Sn::*)(), &S_Sn::SeesTarget)
DA_ROB(SnTakeStimulusSource, C_Actor *(S_Sn::*)(), &S_Sn::TakeStimulusSource)
DA_ROB(SnChooseStance, void (S_Sn::*)(const S_Checkpoint *), &S_Sn::ChooseStance)
DA_ROB(SnFindCoverPoint, int (S_Sn::*)(), &S_Sn::FindCoverPoint)
DA_ROB(SnFindFirePointHeard, int (S_Sn::*)(), &S_Sn::FindFirePointHeard)
DA_ROB(SnFindFirePointSeen, int (S_Sn::*)(), &S_Sn::FindFirePointSeen)
DA_ROB(SnHomePos, S_vector4 S_Sn::*, &S_Sn::m_homePos)
DA_ROB(SnStartStance, uint32_t S_Sn::*, &S_Sn::m_startStance)
DA_ROB(SnFlags, uint32_t S_Sn::*, &S_Sn::m_sniperFlags)
DA_ROB(SnTaskTime, int S_Sn::*, &S_Sn::m_taskTime)
DA_ROB(SnTarget, C_Human *S_Sn::*, &S_Sn::m_target)
DA_ROB(SnPoints, std::vector<uint16_t> S_Sn::*, &S_Sn::m_points)
DA_ROB(SnSniperPoints, std::vector<uint16_t> S_Sn::*, &S_Sn::m_sniperPoints)

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

// The sniper's fields in the original's layout.
void SniperToOrig()
{
    S_Sn *s = Sn();
    std::memcpy(g_osn + 0x140, &(s->*DaGet(SnHomePos())), 16);
    std::memcpy(g_osn + 0x150, &(s->*DaGet(SnStartStance())), 4);
    std::memcpy(g_osn + 0x154, &(s->*DaGet(SnFlags())), 4);
    std::memcpy(g_osn + 0x158, &(s->*DaGet(SnTaskTime())), 4);
    void *target = s->*DaGet(SnTarget()) ? ToOrig(s->*DaGet(SnTarget())) : nullptr;
    std::memcpy(g_osn + 0x15C, &target, 4);
    SnVectorPut(g_osn + 0x160, s->*DaGet(SnPoints()));
    SnVectorPut(g_osn + 0x170, s->*DaGet(SnSniperPoints()));
}
std::string SniperDiffers()
{
    S_Sn *s = Sn();
    if (std::memcmp(g_osn + 0x140, &(s->*DaGet(SnHomePos())), 16) != 0)
        return "homePos";
    auto word = [&](size_t at, uint32_t port, const char *name) {
        uint32_t o;
        std::memcpy(&o, g_osn + at, 4);
        char text[96];
        std::snprintf(text, sizeof text, "%s: port %08x original %08x", name, port, o);
        return o == port ? std::string() : std::string(text);
    };
    std::string why = word(0x150, s->*DaGet(SnStartStance()), "startStance");
    if (why.empty())
        why = word(0x154, s->*DaGet(SnFlags()), "sniperFlags");
    if (why.empty())
        why = word(0x158, uint32_t(s->*DaGet(SnTaskTime())), "taskTime");
    if (!why.empty())
        return why;
    void *target = s->*DaGet(SnTarget()) ? ToOrig(s->*DaGet(SnTarget())) : nullptr, *otarget;
    std::memcpy(&otarget, g_osn + 0x15C, 4);
    if (target != otarget)
        return "target";
    if (SnVectorGet(g_osn + 0x160) != s->*DaGet(SnPoints()))
        return "points";
    if (SnVectorGet(g_osn + 0x170) != s->*DaGet(SnSniperPoints()))
        return "sniperPoints";
    return "";
}
// (The look direction's w, the facing's +0x2C: the turn passes a stack word it does not set, the port 0.)
template <class P, class O> void SnCall(const char *what, P port, O original)
{
    DaCall(what, SniperToOrig, SniperDiffers,
           [] {
               uint8_t *facing;
               std::memcpy(&facing, g_osn + 0x124, 4);
               std::memcpy(facing + 0x2C, reinterpret_cast<const uint8_t *>(g_psn->m_facing) + 0x2C, 4);
           },
           port, original);
}

void SetUpSniper()
{
    SetUpDerivedAi();
    g_psn = new S_TestSniper;
    g_osn = DaOrigController(g_psn, 0x180, HUMAN_CONTROLLER_SNIPER, 0x00811618, g_osnVt, "sniper");
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
    s->m_events = &g_daEvents;
    h->m_events = &g_daEvents;
    s->*DaGet(SnHomePos()) = AiDir();
    s->*DaGet(SnStartStance()) = uint32_t(I(0, 7));
    s->*DaGet(SnFlags()) = uint32_t(I(0, 15));
    s->*DaGet(SnTaskTime()) = AiInt();
    s->*DaGet(SnTarget()) = P(0.3f) ? nullptr : humans[I(0, 1)];
    std::vector<uint16_t> &points = s->*DaGet(SnPoints());
    std::vector<uint16_t> &sniperPoints = s->*DaGet(SnSniperPoints());
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
    C_HumanEvents &e = g_daEvents;
    e.*DaGet(EvSniperPos()) = {h->m_position.x + U(-10, 10), h->m_position.y, h->m_position.z + U(-10, 10), 1.0f};
    e.*DaGet(EvSniperRadius()) = U(0, 30);
    std::deque<S_HumanEvent *> &stimuli = e.*DaGet(EvStimuli());
    stimuli.clear();
    for (int k = P(0.45f) ? 0 : I(1, 4); k > 0; --k) {
        S_HumanEvent *ev = &g_daEv[I(0, DA_EVENTS - 1)];
        stimuli.push_back(ev);
    }
    for (S_HumanEvent &ev : g_daEv) {
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
    reinterpret_cast<C_Script *>(g_daScriptBytes)->*DaGet(ScOnAlarmDone()) = onAlarmDone;
    std::memcpy(g_daScriptBytes + 0xEC, &onAlarmDone, 4);
    g_daFrameData = S_FrameData();
    g_daFrameData.actor = h;
    g_daFrameData.script = P(0.8f) ? reinterpret_cast<C_Script *>(g_daScriptBytes) : nullptr;
    g_daHandsLocked = P(0.2f);
    // The target gone now and then (GetActor no longer knows its id); the hands' weapons single-shot launchers now and
    // then (item type 0x26, +0x0C on both sides).
    C_Human *const target = s->*DaGet(SnTarget());
    g_daGoneId = target && P(0.2f) ? target->GetNetId() : -1000;
    for (int k = 0; k < 2; ++k) {
        const int type = P(0.3f) ? 0x26 : I(0, 0x40);
        reinterpret_cast<C_Item *>(g_weaponPort[k])->*DaGet(ItTypeId()) = type;
        std::memcpy(g_weaponOrig[k] + 0x0C, &type, 4);
    }
    g_da.move = I(0, 3);
    g_da.longGun = P(0.5f);
    g_da.neighbours.clear();
    for (int k = I(0, 6); k > 0; --k)
        g_da.neighbours.push_back(uint16_t(I(0, 7)));
    DaPrimeTaskPool(3);
    DaRandomPoints(s);
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
            g_h.frame->SetData(&g_daFrameData);
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
            SnCall("UpdateTimers", [&] { (s->*DaGet(SnUpdateTimers()))(dt); return 0; }, [&](void *) { OAi<void>(0x00559c60, dt); return 0; });
            break;
        case 5:
            SnCall("DropHandsItem", [&] { (s->*DaGet(SnDropHandsItem()))(); return 0; }, [&](void *) { OAi<void>(0x00559cf0); return 0; });
            break;
        case 6:
            SnCall("UpdateFiring", [&] { (s->*DaGet(SnUpdateFiring()))(dt); return 0; }, [&](void *) { OAi<void>(0x00559d40, dt); return 0; });
            break;
        case 7: {
            S_HumanEvent *ev = &g_daEv[I(0, DA_EVENTS - 1)];
            SnCall("HandleEvent", [&] { return DaEventIndex(s->HandleEvent(ev)); },
                   [&](void *) { return DaEventIndex(OAi<void *>(0x00559f60, static_cast<void *>(ev))); });
            break;
        }
        case 8:
            SnCall("UpdateTask", [&] { return DaWord((s->*DaGet(SnUpdateTask()))(dt), false) != 0; },
                   [&](void *) { return OAi<void *>(0x00559eb0, dt) != nullptr; });
            break;
        case 9:
            SnCall("CheckDone", [&] { (s->*DaGet(SnCheckDone()))(dt); return 0; }, [&](void *) { OAi<void>(0x0055a0e0, dt); return 0; });
            break;
        case 10:
            SnCall("RunTask", [&] { (s->*DaGet(SnRunTask()))(dt); return 0; }, [&](void *) { OAi<void>(0x0055a100, dt); return 0; });
            break;
        case 11:
            SnCall("StartTask", [&] { (s->*DaGet(SnStartTask()))(); return 0; }, [&](void *) { OAi<void>(0x0055a130); return 0; });
            break;
        case 12:
            if (!s->m_task || s->m_task->type != 3)
                break;
            SnCall("UpdateZombie", [&] { (s->*DaGet(SnUpdateZombie()))(dt); return 0; }, [&](void *) { OAi<void>(0x0055a6e0, dt); return 0; });
            break;
        case 13:
            if (!s->m_task || s->m_task->type != 3)
                break;
            SnCall("UpdateSniper", [&] { (s->*DaGet(SnUpdateSniper()))(dt); return 0; }, [&](void *) { OAi<void>(0x0055ad50, dt); return 0; });
            break;
        case 14:
            SnCall("TestSightOfTarget", [&] { (s->*DaGet(SnTestSightOfTarget()))(); return 0; }, [&](void *) { OAi<void>(0x0055b6c0); return 0; });
            break;
        case 15:
            SnCall("TakeCoverStance", [&] { (s->*DaGet(SnTakeCoverStance()))(cp); return 0; },
                   [&](void *) { OAi<void>(0x0055b6f0, static_cast<const void *>(cp)); return 0; });
            break;
        case 16: {
            const int k = I(0, 2);
            static const uint32_t at[] = {0x0055b860, 0x0055b8a0, 0x0055b8d0};
            static const char *const names[] = {"HasSomethingToDo", "SeesEnemies", "SeesTarget"};
            SnCall(names[k],
                   [&] {
                       return int(k == 0 ? (s->*DaGet(SnHasSomethingToDo()))() : k == 1 ? (s->*DaGet(SnSeesEnemies()))() : (s->*DaGet(SnSeesTarget()))());
                   },
                   [&](void *) { return int(OAi<uint32_t>(at[k]) & 0xFF); });
            break;
        }
        case 17:
            SnCall("TakeStimulusSource", [&] { return DaWord((s->*DaGet(SnTakeStimulusSource()))(), false); },
                   [&](void *) { return DaWord(OAi<void *>(0x0055b920), true); });
            break;
        case 18:
            SnCall("ChooseStance", [&] { (s->*DaGet(SnChooseStance()))(cp); return 0; },
                   [&](void *) { OAi<void>(0x0055bc40, static_cast<const void *>(cp)); return 0; });
            break;
        case 19: {
            const int k = I(0, 2);
            static const uint32_t at[] = {0x0055bd40, 0x0055c0b0, 0x0055c450};
            static const char *const names[] = {"FindCoverPoint", "FindFirePointHeard", "FindFirePointSeen"};
            SnCall(names[k],
                   [&] {
                       return (k == 0 ? (s->*DaGet(SnFindCoverPoint()))() : k == 1 ? (s->*DaGet(SnFindFirePointHeard()))() : (s->*DaGet(SnFindFirePointSeen()))()) & 0xFFFF;
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
            if (!same && shown++ < 3) {
                const std::vector<uint8_t> pb = AiFileBytes(portFile), ob = AiFileBytes(origFile);
                size_t at = 0;
                while (at < pb.size() && at < ob.size() && pb[at] == ob[at])
                    ++at;
                std::printf("  SaveGame: the files differ (%u / %u bytes) at %u:", unsigned(pb.size()), unsigned(ob.size()), unsigned(at));
                for (size_t k = at & ~size_t(3); k < at + 12 && k < pb.size() && k < ob.size(); ++k)
                    std::printf(" %02x/%02x", pb[k], ob[k]);
                std::printf(" (interaction %d)\n", s->m_interaction);
            }
            RandomSniper(false);
            // (Every saved actor known: a task naming an unknown one keeps its pool object's, which differ per side.)
            g_daGoneId = -1000;
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
