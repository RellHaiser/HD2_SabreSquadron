#include "script/actor_scripts.h"

#include <cstdlib>
#include <vector>

#include "game/actor.h"
#include "game/game.h"
#include "game/pending.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

constexpr float KMH_TO_MS = 0.28f;                  // the original's km/h -> m/s factor

// Activities (C_Human::PlayActivity) used by the HUMAN_ACTIVITY_* commands.
enum E_HumanActivity {
    ACTIVITY_INVESTIGATE = 0x15,
    ACTIVITY_CARD_1 = 0x19,
    ACTIVITY_CARD = 0x1A,
    ACTIVITY_CUBES = 0x1B,
    ACTIVITY_DRINK_BOTTLE = 0x1C,
    ACTIVITY_DRINK_BOTTLE_AT = 0x1D,
    ACTIVITY_COLD = 0x35,
    ACTIVITY_HOT = 0x36,
    ACTIVITY_CHECK_WEAPON = 0x37,
    ACTIVITY_HAT = 0x38,
    ACTIVITY_SHOES = 0x39,
    ACTIVITY_SALUTE = 0x3A,
    ACTIVITY_SALUTE_1 = 0x3B,
    ACTIVITY_SALUTE_2 = 0x3C,
    ACTIVITY_BINOCULARS_END = 0x3D,
    ACTIVITY_POINT = 0x3E,
    ACTIVITY_BINOCULARS = 0x41,
    ACTIVITY_EAT = 0x42,
    ACTIVITY_PHONE = 0x43,
    ACTIVITY_TELEGRAPH = 0x44,
    ACTIVITY_READ = 0x45,
    ACTIVITY_STOP = 0x4C,
};

// AI modes (C_Human::SetAIMode).
enum : int {
    AI_MODE_AGGRESSIVE = 0x100,
    AI_MODE_DEFENSIVE = 0x200,
    AI_MODE_ZOMBIE = 0x400,
};

C_TeamBase *GetPlayerTeamForMode()
{
    if (g_gameProfile.IsCooperativeGame())
        return g_teamManager.GetCoopTeam();
    return g_teamManager.GetPlayerTeam();
}

// Nearest active player (by squared distance) to `ref`.
C_Human *FindNearestPlayer(I3D_frame *ref)
{
    C_TeamBase *team = GetPlayerTeamForMode();
    C_Human *nearest = nullptr;
    float best = 3.402823466e38f;
    for (int i = team->GetMemberCount(); i-- > 0;) {
        C_Human *player = team->GetMember(i);
        if (!player->IsAlive() || player->IsSpectating() || !player->IsInPlay() || !player->GetFrame() || !ref)
            continue;
        const float d2 = (player->GetFrame()->GetWorldPos() - ref->GetWorldPos()).Square();
        if (d2 < best) {
            best = d2;
            nearest = player;
        }
    }
    return nearest;
}

} // namespace

// ---------------------------------------------------------------------------
// C_HumanScript

C_HumanScript::C_HumanScript(I3D_frame *frame, C_Human *human)
    : C_Script(frame, false), m_human(human)
{
    m_actorFlag = false;
}

bool C_HumanScript::ExecuteCommand(const S_ScriptCommand &cmd)
{
    if (C_Script::ExecuteCommand(cmd))
        return true;

    C_Human *human = m_human;
    if (!human) {
        Advance();
        return true;
    }

    auto number = [&](int i) { return NumberVar(cmd.Arg(i)); };
    auto boolArg = [&](int i) { return NumberVar(cmd.Arg(i)) != 0; };
    auto frame = [&](int i) { return FrameVar(cmd.Arg(i)); };
    auto string = [&](int i) -> const char * { return StringVar(cmd.Arg(i)); };
    // Starts an activity and waits for it.
    auto activity = [&](int id, I3D_frame *target = nullptr) {
        m_delay = target ? human->PlayActivity(id, target) : human->PlayActivity(id);
        return true;
    };

    switch (cmd.opcode) {
    case OP_HUMAN_CreateItemInStore:
        human->CreateItemInStore(number(0), number(1));
        break;

    case OP_HUMAN_SetAlarm:
        human->SetAlarm(boolArg(0));
        break;

    case OP_HUMAN_Move:
    case OP_HUMAN_AttackMove: {
        const C_str name = StringVar(cmd.Arg(0));
        const int checkpoint = human->FindCheckpoint(name);
        if (checkpoint == -1) {
            // "(MO) E_IT_MOVE : checkpoint '%s'not found!!!" (not reported)
            Advance();
        } else if (cmd.opcode == OP_HUMAN_Move) {
            human->MoveTo(checkpoint, m_ip);
        } else {
            human->AttackMoveTo(checkpoint, m_ip);
        }
        return true;
    }

    case OP_HUMAN_Attack:
    case OP_HUMAN_IllFire: {
        // IllFire only pretends to shoot.
        const bool fake = cmd.opcode == OP_HUMAN_IllFire;
        I3D_frame *target = frame(0);
        const int time = number(1);
        if (C_Actor *actor = GetFrameActor(target))
            human->AttackActor(actor, time, fake, m_ip);
        else
            human->AttackFrame(target, time, fake, m_ip);
        return true;
    }

    case OP_HUMAN_Reload:
        human->Reload();
        break;

    case OP_HUMAN_Stop:
        human->StopAction();
        break;

    case OP_HUMAN_Drive: {
        C_Actor *vehicle = human->GetVehicle();
        if (!vehicle || vehicle->GetType() != ACTOR_CAR)
            break;
        const C_str name = StringVar(cmd.Arg(0));
        const int checkpoint = GetGame()->GetCheckpoints()->GetRoadNetwork()->Find(name);
        const float speed = number(1) * KMH_TO_MS;
        if (checkpoint == CHECKPOINT_NONE) {
            // Fix: the original stops the car through any controller (the player's has no
            // driver there).
            if (C_AIController *ai = AsAIController(human->GetController()))
                ai->Drive(CHECKPOINT_NONE, speed, -1);
            Advance();
        } else if (human->GetControllerType() == HUMAN_CONTROLLER_AI) {
            static_cast<C_AIController *>(human->GetController())->Drive(checkpoint, speed, m_ip);
        }
        return true;
    }

    case OP_HUMAN_DriveNearPlayer: {
        // Drive to a random road point within 20 m of the nearest player.
        C_Actor *vehicle = human->GetVehicle();
        if (!vehicle || vehicle->GetType() != ACTOR_CAR || human->GetVehicleSeat() != 0)
            break;
        C_Human *player = FindNearestPlayer(m_frame);
        if (!player)
            break;
        C_CheckpointNetwork *roads = GetGame()->GetCheckpoints()->GetRoadNetwork();
        const int start = roads->FindNearest(player->GetPosition(), 20.0f, -1.0f);
        std::vector<uint16_t> points;
        if (!roads || start == CHECKPOINT_NONE || !roads->GetNeighbours(start, 20.0f, &points))
            break;
        for (size_t i = points.size(); i-- > 0;) {
            const S_Checkpoint *cp = roads->Get(points[i]);
            if (cp && (cp->flags & CHECKPOINT_FLAG_NO_STOP)) {
                points[i] = points.back();
                points.pop_back();
            }
        }
        const int target = points.empty() ? CHECKPOINT_NONE : points[std::rand() % points.size()];
        const float speed = number(0) * KMH_TO_MS;
        if (target == CHECKPOINT_NONE) {
            if (C_AIController *ai = AsAIController(human->GetController()))   // (Fix: as above)
                ai->Drive(CHECKPOINT_NONE, speed, -1);
            break;
        }
        if (human->GetControllerType() == HUMAN_CONTROLLER_AI)
            static_cast<C_AIController *>(human->GetController())->Drive(target, speed, m_ip);
        return true;
    }

    case OP_HUMAN_Investigate:
        return activity(ACTIVITY_INVESTIGATE, frame(0));

    case OP_HUMAN_SetIgnoreCircumventionFlag:
        if (human->GetControllerType() == HUMAN_CONTROLLER_AI)
            static_cast<C_AIController *>(human->GetController())->SetIgnoreCircumvention(boolArg(0));
        break;

    case OP_HUMAN_Suspend: {
        const bool suspend = boolArg(0);
        if (suspend && !m_actorFlag) {
            // Suspend two seconds later: wait, then run this command again.
            m_delay = 2000;
            --m_ip;
            m_running = true;
            m_actorFlag = true;
            return true;
        }
        human->Suspend(suspend);
        Advance();
        m_actorFlag = false;
        return true;
    }

    case OP_HUMAN_MoveToFrame:
    case OP_HUMAN_MoveFromFrame: {
        I3D_frame *target = frame(0);
        const float distance = static_cast<float>(number(1));
        if (!target)
            break;
        S_vector goal;
        if (cmd.opcode == OP_HUMAN_MoveToFrame) {
            // `distance` metres from the frame, on the human's side.
            const S_vector dir = (human->GetFrame()->GetWorldPos() - target->GetWorldPos()).Normalized();
            goal = target->GetWorldPos() + dir * distance;
        } else {
            // `distance` metres further away from the frame.
            const S_vector dir = (human->GetFrame()->GetWorldPos() - target->GetWorldPos()).Normalized();
            goal = human->GetPosition() + dir * distance;
        }
        const int checkpoint = GetGame()->GetCheckpoints()->FindNearest(goal, 5.0f, -1.0f);
        if (checkpoint == CHECKPOINT_NONE)
            break;
        human->MoveTo(checkpoint, m_ip);
        return true;
    }

    case OP_HUMAN_MoveRandomRadius: {
        const float radius = static_cast<float>(number(0));
        std::vector<uint16_t> points;
        const S_Checkpoint *current = human->GetController()->GetCurrentCheckpoint();
        if (!GetGame()->GetCheckpoints()->GetReachable(current, radius, &points) || points.empty()) {
            Advance();
            return true;
        }
        human->MoveTo(points[std::rand() % points.size()], m_ip);
        return true;
    }

    case OP_HUMAN_BoardVehicle: {
        // Board (or with an empty name / off: leave) a car, static weapon or Junkers88.
        if (static_cast<unsigned>(cmd.Arg(0)) >= m_strings.size())
            break;
        const C_str name = StringVar(cmd.Arg(0));
        const int seat = number(2);
        S_HumanCommand command;
        command.category = HUMAN_COMMAND_VEHICLE;
        if (name.IsEmpty() || !boolArg(1)) {
            command.type = HUMAN_COMMAND_LEAVE_VEHICLE;
        } else {
            I3D_frame *vehicleFrame = GetGame()->GetScene()->FindFrame(name, 0xFFFF);
            C_Actor *vehicle = GetFrameActor(vehicleFrame);
            if (!vehicle || (vehicle->GetType() != ACTOR_CAR && vehicle->GetType() != ACTOR_STATIC_WEAPON &&
                             vehicle->GetType() != ACTOR_JUNKERS88))
                break;
            command.type = HUMAN_COMMAND_ENTER_VEHICLE;
            command.param1 = vehicle->GetNetId();
            command.param2 = seat;
        }
        human->IssueCommand(command);
        break;
    }

    case OP_HUMAN_SetAnim:
        human->SetAnimation(string(0), number(1), number(2), boolArg(3));
        break;

    case OP_HUMAN_DressUniform:
        GetGEffects()->DressUniform(human, number(0));
        break;

    case OP_HUMAN_SetWorkingFrame:
        human->GetWork()->SetWorkingFrame(frame(0), true);
        break;

    case OP_HUMAN_TurnAt:
        human->TurnAt(frame(0));
        break;

    case OP_HUMAN_SetOptimized:
        human->SetAIOptimized(number(0));
        break;

    case OP_HUMAN_LookAround: {
        int type = number(0);
        type = type < 0 ? 0 : (type > 6 ? 6 : type);
        human->LookAround(type);
        break;
    }

    case OP_HUMAN_MoveToAlarm:
        human->MoveToAlarm(m_ip);
        return true;

    case OP_HUMAN_SetWaitForPlayer: {
        const float stop = static_cast<float>(number(1));
        const float resume = static_cast<float>(number(2));
        human->SetWaitForPlayer(boolArg(0), stop * stop, resume * resume, boolArg(3));
        break;
    }

    case OP_HUMAN_TurnAtNearestPlayer: {
        C_Human *player = FindNearestPlayer(m_frame);
        if (player)
            human->TurnAt(player->GetFrame());
        break;
    }

    case OP_HUMAN_SetSniper:
        human->SetSniper(boolArg(0), static_cast<float>(number(1)));
        break;

    case OP_HUMAN_Bike: {
        // Ride the named bike to a road point (empty: just get on).
        const C_str bikeName = StringVar(cmd.Arg(0));
        const C_str pointName = StringVar(cmd.Arg(1));
        const float speed = static_cast<float>(number(2));
        I3D_frame *bikeFrame = GetGame()->GetScene()->FindFrame(bikeName, 0xFFFF);
        C_Bike *bike = GetFrameActorAs<C_Bike>(bikeFrame, ACTOR_BIKE);
        if (!bike)
            return true;
        if (pointName.IsEmpty()) {
            bike->Drive(CHECKPOINT_NONE, speed, human->GetFrame());
            Advance();
            return true;
        }
        const int checkpoint = GetGame()->GetCheckpoints()->GetRoadNetwork()->Find(pointName);
        if (checkpoint != CHECKPOINT_NONE)
            bike->Drive(checkpoint, speed, human->GetFrame());
        return true;
    }

    // ------------------------------------------------------------ move modes
    case OP_HUMAN_SETMODE_Run: human->SetMoveModeRun(); break;
    case OP_HUMAN_SETMODE_Walk: human->SetMoveModeWalk(); break;
    case OP_HUMAN_SETMODE_Silence: human->SetMoveModeSilence(); break;
    case OP_HUMAN_SETMODE_Guard: human->SetMoveModeGuard(); break;
    case OP_HUMAN_SETMODE_Lie: human->SetMoveModeLie(); break;
    case OP_HUMAN_SETMODE_Crouch: human->SetMoveModeCrouch(); break;
    case OP_HUMAN_SETMODE_Stand: human->SetMoveModeStand(); break;
    case OP_HUMAN_SETMODE_StandFast: human->SetMoveModeStandFast(); break;

    case OP_HUMAN_SETAIMODE_Aggressive: human->SetAIMode(AI_MODE_AGGRESSIVE); break;
    case OP_HUMAN_SETAIMODE_Defensive: human->SetAIMode(AI_MODE_DEFENSIVE); break;
    case OP_HUMAN_SETAIMODE_Zombie: human->SetAIMode(AI_MODE_ZOMBIE); break;

    case OP_FRAME_SETAIMODE_Aggressive:
    case OP_FRAME_SETAIMODE_Defensive:
    case OP_FRAME_SETAIMODE_Zombie:
        if (C_Human *target = GetFrameActorAs<C_Human>(frame(0), ACTOR_HUMAN)) {
            const int mode = cmd.opcode == OP_FRAME_SETAIMODE_Aggressive ? AI_MODE_AGGRESSIVE
                           : cmd.opcode == OP_FRAME_SETAIMODE_Defensive  ? AI_MODE_DEFENSIVE
                                                                         : AI_MODE_ZOMBIE;
            target->SetControllerAIMode(mode);
        }
        break;

    case OP_HUMAN_SETSIGHTRANGE:
        human->SetSightRange(number(0));
        break;

    case OP_HUMAN_SETHEARRANGE:
        human->SetHearRange(number(0));
        break;

    // ---------------------------------------------------------------- weapons
    case OP_HUMAN_WeaponOnArm:
        if (boolArg(0))
            human->TakeWeapon();
        else
            human->HolsterWeapon();
        break;

    case OP_HUMAN_PickObject:
        human->PickObject(frame(0), number(1) == 1);
        break;

    case OP_HUMAN_WeaponUp:
        if (boolArg(0))
            human->RaiseWeapon();
        else
            human->LowerWeapon();
        break;

    case OP_HUMAN_DropObject:
        human->DropObject();
        break;

    // ------------------------------------------------------------- activities
    case OP_HUMAN_ACTIVITY_Card: {
        const int v = number(0);
        return activity(v == 1 ? ACTIVITY_CARD_1 : v == 0 ? ACTIVITY_STOP : ACTIVITY_CARD);
    }
    case OP_HUMAN_ACTIVITY_Cubes: return activity(ACTIVITY_CUBES);
    case OP_HUMAN_ACTIVITY_CheckWeapon: return activity(ACTIVITY_CHECK_WEAPON);
    case OP_HUMAN_ACTIVITY_Shoes: return activity(ACTIVITY_SHOES);
    case OP_HUMAN_ACTIVITY_Hat: return activity(ACTIVITY_HAT);
    case OP_HUMAN_ACTIVITY_Binoculars:
        // Looping: the script continues immediately.
        m_delay = 0;
        human->PlayActivity(ACTIVITY_BINOCULARS);
        break;
    case OP_HUMAN_ACTIVITY_Cold: return activity(ACTIVITY_COLD);
    case OP_HUMAN_ACTIVITY_Hot: return activity(ACTIVITY_HOT);
    case OP_HUMAN_ACTIVITY_Telegraph:
        return activity(boolArg(0) ? ACTIVITY_TELEGRAPH : ACTIVITY_STOP);
    case OP_HUMAN_ACTIVITY_Phone:
        return boolArg(0) ? activity(ACTIVITY_PHONE, frame(1)) : activity(ACTIVITY_STOP);
    case OP_HUMAN_ACTIVITY_Read:
        return boolArg(0) ? activity(ACTIVITY_READ, frame(1)) : activity(ACTIVITY_STOP);
    case OP_HUMAN_ACTIVITY_DrinkBottle:
        return frame(0) ? activity(ACTIVITY_DRINK_BOTTLE_AT, frame(0)) : activity(ACTIVITY_DRINK_BOTTLE);
    case OP_HUMAN_ACTIVITY_Eat:
        if (!boolArg(0))
            return activity(ACTIVITY_STOP);
        return activity(ACTIVITY_EAT, frame(1));
    case OP_HUMAN_ACTIVITY_Turk:
        human->ActivityTurk();
        break;
    case OP_HUMAN_ACTIVITY_Salute: {
        const int v = number(0);
        return activity(v == 1 ? ACTIVITY_SALUTE_1 : v == 2 ? ACTIVITY_SALUTE_2 : ACTIVITY_SALUTE);
    }
    case OP_HUMAN_ACTIVITY_Sit:
        human->Sit(frame(0));
        break;
    case OP_HUMAN_ACTIVITY_Point:
        return activity(ACTIVITY_POINT, frame(0));
    case OP_HUMAN_ACTIVITY_BinocularsEnd:
        m_delay = 0;
        human->PlayActivity(ACTIVITY_BINOCULARS_END);
        break;

    case OP_HUMAN_SETEVENTS:
        if (boolArg(0))
            human->EnableEvents();
        else
            human->DisableEvents();
        break;

    default:
        // Not implemented in this build (e.g. HUMAN_LookAt, HUMAN_Put, several
        // activities): the script just moves on.
        break;
    }

    Advance();
    return true;
}

// ---------------------------------------------------------------------------
// C_MeleeTrackerScript

C_MeleeTrackerScript::C_MeleeTrackerScript(I3D_frame *frame, C_MeleeTracker *meleeTracker)
    : C_Script(frame, false), m_meleeTracker(meleeTracker)
{
    m_actorFlag = false;
}

bool C_MeleeTrackerScript::ExecuteCommand(const S_ScriptCommand &cmd)
{
    if (C_Script::ExecuteCommand(cmd))
        return true;

    // The commands are the full AI's (the original calls them on whatever controller the tracker
    // has; fix: they are skipped while it has none or a remote / player one).
    C_MeleeTrackerAI *ai = m_meleeTracker->GetFullAI();
    auto number = [&](int i) { return NumberVar(cmd.Arg(i)); };
    auto boolArg = [&](int i) { return NumberVar(cmd.Arg(i)) != 0; };

    switch (ai ? cmd.opcode : -1) {
    case OP_MELEE_TRACKER_SetOptimized:
        ai->SetOptimized(boolArg(0));
        break;
    case OP_MELEE_TRACKER_Suspend: {
        const bool suspend = boolArg(0);
        if (suspend && !m_actorFlag) {
            // Suspend two seconds later (see HUMAN_Suspend).
            m_delay = 2000;
            --m_ip;
            m_running = true;
            m_actorFlag = true;
            return true;
        }
        ai->Suspend(suspend);
        m_actorFlag = false;
        break;
    }
    case OP_MELEE_TRACKER_SetRangeSight: ai->SetSightRange(number(0)); break;
    case OP_MELEE_TRACKER_SetRangeHear: ai->SetHearRange(number(0)); break;
    case OP_MELEE_TRACKER_ClearAlarm: ai->SetAlarm(0); break;   // (the controller's vtable +0x38)
    case OP_MELEE_TRACKER_SetOwner: ai->SetOwner(FrameVar(cmd.Arg(0))); break;
    case OP_MELEE_TRACKER_Move:
        ai->MoveTo(GetGame()->GetCheckpoints()->Find(StringVar(cmd.Arg(0))));
        break;
    case OP_MELEE_TRACKER_Attack: ai->Attack(FrameVar(cmd.Arg(0))); break;
    case OP_MELEE_TRACKER_SETMODE_Run: ai->SetModeRun(); break;
    case OP_MELEE_TRACKER_SETMODE_Walk: ai->SetModeWalk(); break;
    case OP_MELEE_TRACKER_SETMODE_Stand: ai->SetModeStand(); break;
    case OP_MELEE_TRACKER_SETMODE_Lie: ai->SetModeLie(); break;
    case OP_MELEE_TRACKER_HafHaf:
        // (The scripted controller's; the original assumes it.)
        if (C_MeleeTrackerScriptedAI *scripted = m_meleeTracker->GetScriptedAI())
            scripted->SetBarking(boolArg(0));
        break;
    case OP_MELEE_TRACKER_SETAIMODE_Aggressive: ai->SetAggressive(); break;
    case OP_MELEE_TRACKER_SETAIMODE_Defensive: ai->SetDefensive(); break;
    default: break;
    }

    // Move and Attack continue when the meleeTracker has finished (OnActionDone).
    if (cmd.opcode != OP_MELEE_TRACKER_Move && cmd.opcode != OP_MELEE_TRACKER_Attack)
        Advance();
    return true;
}
