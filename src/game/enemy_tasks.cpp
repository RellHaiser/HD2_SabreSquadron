// C_EnemyController's guard, search and morale broken tasks (game/human_controller.h;
// original source 0x00548890..0x00549ae0, 0x0054b730..0x0054b8f0, 0x00553cc0..0x00553ce0,
// 0x00553eb0..0x00554e20, 0x00557540, 0x00557d10, 0x00557f70).
#include "game/human_controller.h"

#include <cstdlib>
#include <vector>

#include "game/actor.h"
#include "game/checkpoints.h"
#include "game/controller_internal.h"
#include "game/enemy_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "game/pending.h"
#include "ls3d/i3d.h"

namespace {

// The paces the tasks set (S_AIMoveOrder::unknown54).
constexpr uint32_t PACE_WALK = MOVE_PACE_4 | MOVE_FORWARD;         // 0x4008
constexpr uint32_t PACE_RUN = MOVE_PACE_MARCH | MOVE_FORWARD;      // 0x10008
constexpr uint32_t PACE_ALERT = MOVE_PACE_3 | MOVE_FORWARD;        // 0x2008 weapon ready
constexpr uint32_t PACE_SNEAK = MOVE_PACE_1 | MOVE_FORWARD;        // 0x1008
constexpr uint32_t PACE_LADDER_DOWN = MOVE_PACE_4 | 0x10;          // 0x4010

// ms a searching human waits doing nothing (15..25 s).
int IdleTime()
{
    return static_cast<int>(Rand01() * 10000.0f + 15000.0f);
}

// A bell-shaped random number 0..1: four quarter rand()s summed.
float RandBell()
{
    int sum = std::rand() >> 2;
    sum += std::rand() >> 2;
    sum += std::rand() >> 2;
    sum += std::rand() >> 2;
    return static_cast<float>(sum) * (1.0f / 32767.0f);
}

// The direction from `from` to `to` (w: a stale local in the original).
S_vector4 DirFromTo(const S_vector &from, const S_vector &to)
{
    return {to.x - from.x, to.y - from.y, to.z - from.z, 0.0f};
}

}  // namespace

// ------------------------------------------------------------------------- small helpers

bool C_EnemyController::IsNotClimbing()
{
    return m_human->m_body.GetClimbState() == 0;
}

bool C_EnemyController::IsOutdoors()
{
    return m_human->m_body.GetSector() == GetGame()->GetScene()->GetPrimarySector();
}

uint32_t C_EnemyController::RunPace()
{
    // Indoors the human walks.
    return IsOutdoors() ? PACE_RUN : PACE_WALK;
}

bool C_EnemyController::IsLightWeapon()
{
    if (!m_handsItem)
        return true;
    switch (BehaviourOf(m_handsItem)) {
    case ITEM_BEHAVIOUR_MELEE:
    case ITEM_BEHAVIOUR_SMG:
    case 7:
    case ITEM_BEHAVIOUR_THROWN:
        return true;
    default:
        return false;
    }
}

void C_EnemyController::WalkCarefully()
{
    // A heavy weapon is lowered to walk.
    if (m_weaponRaised && !IsLightWeapon())
        LowerWeapon();
    m_alert->Set(1);
    m_move->unknown54 = PACE_WALK;
    LookAround(0);
}

void C_EnemyController::RunForCover()
{
    LowerWeapon();
    SetEnemyStance(MOVE_STANDING);
    m_alert->Set(1);
    m_move->unknown54 = PACE_RUN;
    LookAround(0);
}

bool C_EnemyController::LookTowardIfAside(const S_vector4 &pos)
{
    // The body's direction against the direction to `pos`, both in the xz plane.
    const S_matrix &m = m_human->GetBodyFrame()->GetMatrix();
    S_vector body(m.m[2][0], m.m[2][1], m.m[2][2]);
    NormalizeBy(body, body.y * body.y + body.z * body.z + body.x * body.x);
    S_vector ahead(body.x, 0.0f, body.z);
    NormalizeBy(ahead, ahead.x * ahead.x + ahead.z * ahead.z);
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    S_vector to(pos.x - own.x, 0.0f, pos.z - own.z);
    NormalizeBy(to, to.z * to.z + to.x * to.x);
    if (to.z * ahead.z + to.x * ahead.x + to.y * ahead.y >= 0.76f)
        return true;
    LookAroundAt(7, pos);
    const S_vector4 dir = {to.x, to.y, to.z, 0.0f};     // (w: a stale local in the original)
    m_facing->SetFacing(dir);
    m_facing->SetView(dir);
    return false;
}

void C_EnemyController::LookAroundToward(const S_vector4 &pos)
{
    // Looks around on the side `pos` is on (5 left, 6 right of the body), for 4..10 s.
    const S_matrix &m = m_human->GetBodyFrame()->GetMatrix();
    S_vector body(m.m[2][0], m.m[2][1], m.m[2][2]);
    NormalizeBy(body, body.y * body.y + body.z * body.z + body.x * body.x);
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    LookAround(body.z * (pos.x - own.x) - (pos.z - own.z) * body.x >= 0.0f ? 5 : 6);
    m_lookAroundTime = static_cast<int>(RandFloat() * 6000.0f + 4000.0f);
}

int C_EnemyController::NearestPoint(const S_vector4 &pos, const std::vector<uint16_t> &points, float *distance2)
{
    if (points.empty())
        return CHECKPOINT_NONE;
    // (`distance2` gets the squared distance of the last point looked at, not the nearest.)
    int best = CHECKPOINT_NONE;
    float bestDistance2 = 100000.0f;
    float last = -1.0f;
    for (size_t i = points.size(); i-- > 0;) {
        const S_Checkpoint *point = m_checkpoints->Get(points[i]);
        const float dx = pos.x - point->pos.x;
        const float dy = pos.y - point->pos.y;
        const float dz = pos.z - point->pos.z;
        last = dz * dz + dy * dy + dx * dx;
        if (last < bestDistance2) {
            best = points[i];
            bestDistance2 = last;
        }
    }
    *distance2 = last;
    return best;
}

// ------------------------------------------------------------------------- guard

void C_EnemyController::StartGuard()
{
    ReleaseTask();
    m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
    if (m_homeCheckpoint < 0)
        return;
    const S_vector4 &dir = Dir4(GetViewDir());
    S_AITask1 *task = AllocTask1(m_homePos, static_cast<uint16_t>(m_homeCheckpoint), m_homeRadius, dir);
    m_task = task;
    m_taskTime = 0;
    task->unknown08 = GUARD_PICK_TARGET;
}

void C_EnemyController::GuardEvent(S_HumanEvent *event)
{
    switch (event->type) {
    case AI_EVENT_FRIEND_HEARD:
        // A friend heard: the human looks at him unless enemies are in view.
        if (!SeesEnemies() && IdToActor(event->sourceId)
            && !m_human->HasSeenEnemy(static_cast<C_Human *>(IdToActor(event->sourceId))))
            m_facing->SetLookAt(static_cast<C_Human *>(IdToActor(event->sourceId))->GetBodyFrame());
        break;
    case AI_EVENT_ENEMY_SEEN:
        // An enemy shielded in a vehicle, or the human idle: picks a target again.
        if (IdToActor(event->sourceId)) {
            const int state = m_task->unknown08;
            if (IsShieldedInVehicle(static_cast<C_Human *>(IdToActor(event->sourceId))) || state == STATE_HIDING
                || state == GUARD_LOOK)
                m_task->unknown08 = GUARD_PICK_TARGET;
        }
        break;
    case AI_EVENT_SOUND_0:
    case AI_EVENT_SOUND_2:
        if (IdToActor(event->sourceId) && !m_human->HasSeenEnemy(static_cast<C_Human *>(IdToActor(event->sourceId))))
            m_facing->SetLookAt(static_cast<C_Human *>(IdToActor(event->sourceId))->GetBodyFrame());
        break;
    case AI_EVENT_SHOT_AT:
    case AI_EVENT_LOUD_SOUND:
    case AI_EVENT_HIT_5:
        // Shot at: looks at the shooter (one in a vehicle only while hiding from it).
        if (!IdToActor(event->sourceId))
            break;
        if (IsShieldedInVehicle(static_cast<C_Human *>(IdToActor(event->sourceId)))
            && m_task->unknown08 != STATE_TO_HIDING)
            break;
        if (!m_human->HasSeenEnemy(static_cast<C_Human *>(IdToActor(event->sourceId))))
            m_facing->SetLookAt(static_cast<C_Human *>(IdToActor(event->sourceId))->GetBodyFrame());
        if (IsShieldedInVehicle(static_cast<C_Human *>(IdToActor(event->sourceId)))
            || m_task->unknown08 == STATE_HIDING || m_task->unknown08 == GUARD_LOOK)
            m_task->unknown08 = GUARD_PICK_TARGET;
        break;
    default:
        break;
    }
}

void C_EnemyController::UpdateGuard(int timeDelta)
{
    S_AITask *task = m_task;
    if (task->unknown08 == GUARD_LOOK) {
        // No enemy in view: searches once the wait is over.
        if (!SeesEnemies()) {
            if (task->unknown10 == 0)
                StartSearch();
            return;
        }
        task->unknown08 = GUARD_PICK_TARGET;
    }
    if (m_task->unknown08 == GUARD_PICK_TARGET) {
        LookAround(0);
        m_target = PickTarget(nullptr);
        if (!m_target) {
            task->unknown10 = 200;
            task->unknown08 = GUARD_LOOK;
        } else if (C_Actor *gun = FindGunToUse(timeDelta)) {
            // A static gun at the post: mans it.
            const int point = m_checkpoints->FindActorPoint(gun);
            if (point != CHECKPOINT_NONE) {
                m_gun = gun;
                m_task->unknown08 = GUARD_TO_GUN;
                m_move->unknown54 = PACE_WALK;
                m_move->SetCheckpoint(static_cast<int16_t>(point));
            }
        } else if (!IsShieldedInVehicle(m_target)) {
            task->unknown08 = GUARD_TO_COVER;
        } else if (HasLauncher()) {
            // A target in a vehicle: the launcher.
            m_task->unknown10 = 1000;
            m_task->unknown08 = STATE_LAUNCHER;
        } else if (C_Human *other = NearestTargetOnFoot()) {
            // No launcher: an enemy on foot instead, else cover from the vehicle. (The
            // original calls an empty function first, 0x00559650.)
            m_target = other;
            task->unknown08 = GUARD_TO_COVER;
        } else if (TakeCoverStance(m_currentCheckpoint, m_target)) {
            m_task->unknown10 = 1000;
            m_task->unknown08 = STATE_HIDING;
        } else {
            m_task->unknown08 = STATE_HIDE_FROM_VEHICLE;
        }
    }
    if (m_task->unknown08 == GUARD_TO_GUN && (m_move->flags & S_AIMoveOrder::FLAG_DONE)) {
        // At the gun: into its seat if it is free (or its gunner is dead). (Fix: a gun gone
        // from a saved game was read through null.)
        C_Actor *gun = m_gun;
        if (gun) {
            if (!gun->IsSeatBlocked(0)
                || (gun->GetSeatOccupant(0) && static_cast<C_Human *>(gun->GetSeatOccupant(0))->GetHealth() < 1)) {
                S_HumanCommand command;
                command.category = HUMAN_COMMAND_VEHICLE;
                command.type = HUMAN_COMMAND_ENTER_VEHICLE;
                command.param1 = m_gun->GetNetId();
                command.param2 = 0;
                IssueCommand(command);
                return;
            }
        }
        task->unknown08 = GUARD_HOLD;
    }
    if (m_task->unknown08 == GUARD_TO_COVER) {
        // To cover away from the target, else holds here.
        if (!MoveToCombatPoint(PickCoverAwayFrom(m_target)))
            m_task->unknown08 = GUARD_HOLD;
        return;
    }
    if (task->unknown08 == GUARD_MOVE_FIRING) {
        if (m_move->flags & S_AIMoveOrder::FLAG_DONE)
            task->unknown08 = GUARD_HOLD;
        else if (m_target && m_target->GetHealth() > 0 && SeesEnemy(m_target))
            Attack(timeDelta);
    }
    if (task->unknown08 == GUARD_MOVE && (m_move->flags & S_AIMoveOrder::FLAG_DONE))
        task->unknown08 = GUARD_HOLD;
    if (m_task->unknown08 == GUARD_HOLD) {
        // Fires from here for 3 s (an enemy within 10 m) or 6 s.
        task->unknown10 = GetNearestEnemyDistance() < 10.0f ? 3000 : 6000;
        ChooseCombatStance();
        m_alert->Set(2);
        task->unknown08 = GUARD_FIRE;
        LookAround(0);
    }
    if (m_task->unknown08 == GUARD_FIRE) {
        if (!m_target || m_target->GetHealth() < 1 || !SeesEnemy(m_target) || m_task->unknown10 == 0) {
            m_fireOrder->SetTarget(nullptr);
            m_task->unknown08 = GUARD_PICK_TARGET;
            m_alert->Set(0);
        } else {
            Attack(timeDelta);
        }
    }

    switch (m_task->unknown08) {
    case STATE_LAUNCHER:
        if (TakeLauncher()) {
            // In range: fires from here; else from a point in range.
            if (CompareRange() == 0) {
                m_alert->Set(2);
                m_task->unknown08 = STATE_LAUNCHER_FIRE;
                task->unknown10 = 4000;
                return;
            }
            const int point = FindLauncherPoint(m_target);
            if (point == CHECKPOINT_NONE) {
                m_alert->Set(2);
                m_task->unknown08 = STATE_LAUNCHER_FIRE;
                task->unknown10 = 4000;
                return;
            }
            SetEnemyStance(MOVE_STANDING);
            m_alert->Set(1);
            m_move->SetCheckpoint(static_cast<int16_t>(point));
            m_move->unknown54 = PACE_WALK;
            m_task->unknown08 = STATE_TO_LAUNCHER_POINT;
            return;
        }
        if (!m_target || m_target->GetHealth() < 1 || m_task->unknown10 == 0) {
            m_task->unknown08 = GUARD_PICK_TARGET;
            m_alert->Set(0);
        }
        break;
    case STATE_TO_LAUNCHER_POINT:
        if (!m_target) {
            m_task->unknown08 = GUARD_PICK_TARGET;
            return;
        }
        if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE) {
            // On the way, every second: given up within 8 m of the target.
            if (m_task->unknown10 != 0)
                return;
            if (GetDistanceTo(m_target) < 8.0f)
                m_task->unknown08 = GUARD_PICK_TARGET;
            task->unknown10 = 1000;
            return;
        }
        if (CompareRange() != 0) {
            m_task->unknown08 = GUARD_PICK_TARGET;
            return;
        }
        m_alert->Set(2);
        task->unknown08 = STATE_LAUNCHER_FIRE;
        task->unknown10 = 3000;
        return;
    case STATE_LAUNCHER_FIRE:
        // Fires the launcher while the target is in view (for the countdown).
        if (m_target && m_target->GetHealth() > 0 && SeesEnemy(m_target) && m_handsItem && m_task->unknown10 != 0
            && HasLauncherInHands()) {
            Attack(timeDelta);
            break;
        }
        m_fireOrder->SetTarget(nullptr);
        m_task->unknown08 = GUARD_PICK_TARGET;
        m_alert->Set(0);
        break;
    default:
        break;
    }
    if (m_task->unknown08 == STATE_ATTACK_DONE)
        m_task->unknown08 = GUARD_PICK_TARGET;

    switch (m_task->unknown08) {
    case STATE_HIDE_FROM_VEHICLE:
        // Hides from the vehicle beside it (out of its guns' reach), else runs.
        if (m_target && SeesEnemies()) {
            const int point = FindPointBesideVehicle(m_target);
            if (point == CHECKPOINT_NONE) {
                LowerWeapon();
                m_alert->Set(1);
                m_move->unknown54 = RunPace();
                m_move->SetFlee(3, 20);
                m_task->unknown08 = STATE_HIDING;
                m_task->unknown10 = 1000;
                return;
            }
            RunForCover();
            m_move->unknown54 = RunPace();
            m_move->SetCheckpoint(static_cast<int16_t>(point));
            task->unknown08 = STATE_TO_HIDING;
            m_task->unknown10 = 1500;
            return;
        }
        break;
    case STATE_TO_HIDING:
        if (!m_target)
            break;
        if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE) {
            // Every second: walks once the enemies are 20 m away, runs (outdoors) while
            // nearer; within 10 m of the target picks again.
            if (m_task->unknown10 != 0)
                return;
            if (GetDistanceTo(m_target) < 10.0f) {
                m_task->unknown08 = GUARD_PICK_TARGET;
                task->unknown10 = 1000;
                return;
            }
            if (GetNearestEnemyDistance() < 20.0f) {
                if (IsOutdoors() && !(m_moveFlags & MOVE_PACE_MARCH))
                    m_move->unknown54 = PACE_RUN;
            } else if (m_moveFlags & MOVE_PACE_MARCH) {
                m_move->unknown54 = PACE_WALK;
            }
            task->unknown10 = 1000;
            return;
        }
        if (TakeCoverStance(m_currentCheckpoint, m_target)) {
            task->unknown10 = 2000;
            m_task->unknown08 = STATE_HIDING;
            return;
        }
        break;
    case STATE_HIDING:
        if (m_task->unknown10 != 0)
            return;
        break;
    default:
        return;
    }
    m_task->unknown08 = GUARD_PICK_TARGET;
}

// ------------------------------------------------------------------------- search

void C_EnemyController::SearchEvent(S_HumanEvent *event)
{
    if (!IdToActor(event->sourceId))
        return;
    // The first event with a source: searches for it.
    if (!m_event && (event->type & HUMAN_EVENT_PRIORITY_MASK) != 0) {
        m_event = CopyEvent(*event);
        m_task->unknown08 = SEARCH_TURN;
        return;
    }
    if (IsSameSource(event) && (event->type & HUMAN_EVENT_PRIORITY_MASK) < 0x40000000) {
        SearchSameSource(event);
        return;
    }
    switch (event->type) {
    case AI_EVENT_SOUND_0:
    case AI_EVENT_SOUND_2:
    case AI_EVENT_SOUND_1:
    case AI_EVENT_SOUND_8:
        SearchStrongerEvent(event);
        break;
    case AI_EVENT_ENEMY_SEEN:
        if (IsNotClimbing()) {
            if (m_moveFlags & MOVE_AI_AGGRESSIVE)
                StartCombat();
            else
                StartGuard();
        }
        break;
    case AI_EVENT_FRIEND_HEARD:
        SearchFriendHeard(event);
        break;
    case AI_EVENT_SHOT_AT:
    case AI_EVENT_LOUD_SOUND:
    case AI_EVENT_HIT_5:
        SetSearchEvent(event);
        break;
    default:
        break;
    }
}

bool C_EnemyController::IsSameSource(const S_HumanEvent *event)
{
    if (!m_event || !IdToActor(m_event->sourceId))
        return false;
    return IdToActor(m_event->sourceId) == IdToActor(event->sourceId);
}

void C_EnemyController::SearchSameSource(S_HumanEvent *event)
{
    if (m_task->unknown08 != SEARCH_WALK) {
        SetSearchEvent(event);
        return;
    }
    // Walking to it: the new place behind the human turns it round; one more than 10 m from
    // the old place is walked to anew.
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    const S_vector before(m_event->pos.x - own.x, m_event->pos.y - own.y, m_event->pos.z - own.z);
    const S_vector now(event->pos.x - own.x, event->pos.y - own.y, event->pos.z - own.z);
    const S_vector moved = before - now;
    ReleaseHumanEvent(m_event);
    m_event = CopyEvent(*event);
    if (now.z * before.z + now.y * before.y + now.x * before.x < 0.0f) {
        m_task->unknown08 = SEARCH_TURN;
        return;
    }
    if (100.0f < moved.z * moved.z + moved.y * moved.y + moved.x * moved.x)
        m_task->unknown08 = SEARCH_TO_POINT;
}

void C_EnemyController::SetSearchEvent(S_HumanEvent *event)
{
    ReleaseHumanEvent(m_event);
    m_event = CopyEvent(*event);
    const int state = m_task->unknown08;
    if (state != SEARCH_TURN && state != SEARCH_START && state != SEARCH_GO && m_human->m_body.GetClimbState() == 0)
        m_task->unknown08 = SEARCH_TURN;
}

void C_EnemyController::SearchStrongerEvent(S_HumanEvent *event)
{
    // A sound of a higher priority, or one while not walking: searches for it; one 20 m away
    // while walking: only looks toward it.
    if ((m_event->type & HUMAN_EVENT_PRIORITY_MASK) < (event->type & HUMAN_EVENT_PRIORITY_MASK)
        || m_task->unknown08 != SEARCH_WALK) {
        SetSearchEvent(event);
        return;
    }
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    const float dx = event->pos.x - own.x;
    const float dy = event->pos.y - own.y;
    const float dz = event->pos.z - own.z;
    if (20.0f <= MagnitudeBy(dz * dz + dy * dy + dx * dx)) {
        LookAroundToward(event->pos);
        return;
    }
    SetSearchEvent(event);
}

void C_EnemyController::SearchFriendHeard(S_HumanEvent *event)
{
    const int state = m_task->unknown08;
    if (state == SEARCH_TURN || state == SEARCH_START || m_human->m_body.GetClimbState() != 0)
        return;
    if (m_task->unknown08 == SEARCH_WALK)
        LookAroundToward(event->pos);
    else
        m_task->unknown08 = SEARCH_TURN;
}

void C_EnemyController::UpdateSearch(int /*timeDelta*/)
{
    // An enemy in view (off a ladder): fights.
    if (SeesEnemies() && IsNotClimbing()) {
        if (m_moveFlags & MOVE_AI_AGGRESSIVE)
            StartCombat();
        else
            StartGuard();
        return;
    }
    S_AITask *task = m_task;
    S_HumanEvent *event = m_event;
    switch (task->unknown08) {
    case SEARCH_IDLE:
        // Nothing to search for: looks around, then goes home.
        if (event) {
            ReleaseHumanEvent(event);
            m_event = nullptr;
        }
        if (!(m_moveFlags & MOVE_LOOKING_AROUND))
            LookAround(3);
        if (m_task->unknown10 != 0)
            return;
        LowerWeapon();
        SetEnemyStance(MOVE_STANDING);
        m_alert->Set(1);
        m_move->unknown54 = PACE_ALERT;
        m_move->SetPos(m_homePos);
        m_task->unknown08 = SEARCH_HOME;
        return;
    case SEARCH_OFF_LADDER: {
        // Down the ladder (to its lower end) first.
        const int from = m_move->ladderFrom;
        const int to = m_move->ladderTo;
        const S_Checkpoint *a = m_checkpoints->Get(from);
        const S_Checkpoint *b = m_checkpoints->Get(to);
        if (!a || !b) {
            task->unknown08 = SEARCH_IDLE;
        } else {
            m_move->SetCheckpoint(static_cast<int16_t>(b->pos.y <= a->pos.y ? to : from));
            m_move->unknown54 = PACE_LADDER_DOWN;
            task->unknown08 = SEARCH_CLIMBING;
        }
        break;
    }
    case SEARCH_CLIMBING:
        if (m_moveFlags & MOVE_ON_LADDER)
            return;
        if (!event || !IdToActor(event->sourceId)) {
            task->unknown10 = IdleTime();
            task->unknown08 = SEARCH_IDLE;
        } else {
            task->unknown08 = SEARCH_TURN;
        }
        break;
    case SEARCH_HOME:
        if (!(m_move->flags & S_AIMoveOrder::FLAG_DONE))
            return;
        m_alert->Set(0);
        task->unknown08 = SEARCH_WAIT;
        task->unknown10 = static_cast<int>(Rand01() * 3000.0f + 2000.0f);
        return;
    case SEARCH_WAIT:
        // Home again: back to the script after 2..5 s.
        if (task->unknown10 != 0 || (m_enemyFlags & (FLAG_DONE | FLAG_NO_ALARM_DONE)))
            return;
        m_enemyFlags |= FLAG_DONE;
        return;
    default:
        break;
    }

    // The source gone: off the ladder, else waits.
    if (!event || !IdToActor(event->sourceId)) {
        if (m_human->m_body.GetClimbState() == 0) {
            const int wait = IdleTime();
            task->unknown08 = SEARCH_IDLE;
            task->unknown10 = wait;
        } else {
            task->unknown08 = SEARCH_OFF_LADDER;
        }
        return;
    }

    if (task->unknown08 == SEARCH_TURN) {
        SetAlertState(0);
        if (m_human->m_body.GetClimbState() != 0) {
            task->unknown08 = SEARCH_OFF_LADDER;
            return;
        }
        if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE)
            m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
        // Turns toward the event (a sound of chapter 6: its place first, else its source);
        // with the source ahead (within 30 m: looked for at once) it looks around there.
        bool ahead = true;
        if (event->type != AI_EVENT_SOUND_6 || !LookTowardIfAside(event->pos)) {
            const I3D_frame *source = IdToActor(event->sourceId)->GetFrame();
            if (LookTowardIfAside(WorldPos4Of(source))) {
                const S_vector &own = m_human->GetFrame()->GetWorldPos();
                const S_vector &pos = source->GetWorldPos();
                const float dx = pos.x - own.x;
                const float dy = pos.y - own.y;
                const float dz = pos.z - own.z;
                if (dx * dx + dz * dz + dy * dy < 900.0f)
                    TestSightNow(static_cast<C_Human *>(IdToActor(event->sourceId)));
            } else {
                ahead = false;
            }
        }
        if (ahead) {
            task->unknown08 = SEARCH_LOOK;
            task->unknown0C = 0;
            LookAround(0);
            if (!m_weaponRaised)
                RaiseWeapon();
        }
    }
    if (task->unknown08 == SEARCH_START) {
        // 20 m away outdoors: runs to cover toward the event first.
        const S_vector &own = m_human->GetFrame()->GetWorldPos();
        const float dx = event->pos.x - own.x;
        const float dy = event->pos.y - own.y;
        const float dz = event->pos.z - own.z;
        if (IsOutdoors() && !(dx * dx + dz * dz + dy * dy < 400.0f)) {
            const int point = FindCoverToward(event->pos);
            if (point != CHECKPOINT_NONE) {
                RunForCover();
                m_move->SetCheckpoint(static_cast<int16_t>(point));
                task->unknown08 = SEARCH_GO;
                return;
            }
        }
        SetEnemyStance(MOVE_CROUCHING);
        task->unknown08 = SEARCH_TURN;
        return;
    }
    if (task->unknown08 == SEARCH_GO && (m_move->flags & S_AIMoveOrder::FLAG_DONE)) {
        // In cover: faces the source.
        RaiseWeapon();
        SetAlertState(2);
        const S_vector4 dir = DirFromTo(m_human->GetFrame()->GetWorldPos(), IdToActor(event->sourceId)->GetFrame()->GetWorldPos());
        m_facing->SetView(dir);
        m_facing->SetFacing(dir);
        task->unknown0C = 0;
        task->unknown08 = SEARCH_LOOK;
        LookAround(0);
    }
    if (task->unknown08 == SEARCH_LOOK) {
        // Looks 0.2..0.8 s (aggressive) or 0.5..2.5 s.
        if (!m_weaponRaised)
            RaiseWeapon();
        if (((m_moveFlags & MOVE_AI_AGGRESSIVE) && Rand01() * 600.0f + 200.0f < static_cast<float>(task->unknown0C))
            || Rand01() * 2000.0f + 500.0f < static_cast<float>(task->unknown0C))
            task->unknown08 = SEARCH_TO_POINT;
    }
    if (task->unknown08 == SEARCH_TO_POINT) {
        const int point = FindSearchPoint();
        if (point == CHECKPOINT_NONE) {
            SetEnemyStance(MOVE_CROUCHING);
            task->unknown08 = SEARCH_IDLE;
            task->unknown10 = IdleTime();
            return;
        }
        // To a point near the event: crouching (not aggressive); aggressive, runs from far
        // outdoors (60..80 m), comes near (20..40 m) ready to fire, else walks.
        const S_vector &own = m_human->GetFrame()->GetWorldPos();
        const float dx = event->pos.x - own.x;
        const float dy = event->pos.y - own.y;
        const float dz = event->pos.z - own.z;
        const float distance = MagnitudeBy(dx * dx + dz * dz + dy * dy);
        if (!(m_moveFlags & MOVE_AI_AGGRESSIVE)) {
            WalkCarefully();
            SetAlertState(1);
            SetEnemyStance(MOVE_CROUCHING);
        } else if (IsOutdoors() && Rand01() * 20.0f + 60.0f < distance) {
            RunForCover();
        } else if ((Rand01() + 1.0f) * 20.0f < distance) {
            SetEnemyStance(MOVE_STANDING);
            WalkCarefully();
        } else {
            m_alert->Set(1);
            m_move->unknown54 = PACE_ALERT;
            LookAround(4);
        }
        m_move->SetCheckpoint(static_cast<int16_t>(point));
        task->unknown08 = SEARCH_WALK;
        return;
    }
    if (task->unknown08 == SEARCH_WALK) {
        if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE) {
            // Nearing the event: running, at 1..2 m (squared 100..200), walking at 0..1.5 m
            // (squared 0..150, both around a bell curve), the human slows down ready to fire;
            // within 5..7 m (squared) it sneaks crouching.
            const S_vector &own = m_human->GetFrame()->GetWorldPos();
            const float dx = event->pos.x - own.x;
            const float dy = event->pos.y - own.y;
            const float dz = event->pos.z - own.z;
            const float distance2 = dx * dx + dz * dz + dy * dy;
            const uint32_t pace = m_move->unknown54;
            if (((pace & MOVE_PACE_MARCH) && (RandBell() - 0.5f) * 100.0f + 150.0f > distance2)
                || ((pace & MOVE_PACE_4) && distance2 < ((RandBell() - 0.5f) + 1.0f) * 100.0f)) {
                LookAround(4);
                m_move->unknown54 = PACE_ALERT;
                if (!m_weaponRaised)
                    RaiseWeapon();
            } else if (!(pace & MOVE_PACE_1) && distance2 < Rand01() * 24.0f + 25.0f) {
                SetEnemyStance(MOVE_CROUCHING);
                LookAround(4);
                m_move->unknown54 = PACE_SNEAK;
            }
            if (distance2 < 36.0f) {
                // At the event: looks around it; a source that is a body seen ends the walk.
                if (m_lookAroundTime == 0) {
                    LookAroundAt(7, event->pos);
                    m_lookAroundTime = static_cast<int>((Rand01() + 1.0f) * 1500.0f);
                } else if (IdToActor(event->sourceId)) {
                    const std::vector<C_Human *> &bodies = m_human->m_seenBodies;
                    for (size_t i = bodies.size(); i-- > 0;) {
                        if (IdToActor(event->sourceId) == bodies[i]) {
                            m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
                            break;
                        }
                    }
                }
            }
        }
        if (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE) || (m_move->flags & S_AIMoveOrder::FLAG_DONE)) {
            // There: aims at the event's place for 1.5..3.5 s.
            RaiseWeapon();
            SetAlertState(2);
            const S_vector4 dir = DirFromTo(m_human->GetBodyFrame()->GetWorldPos(), XYZ(event->pos));
            m_facing->SetView(dir);
            m_facing->SetFacing(dir);
            LookAround(4);
            task->unknown08 = SEARCH_AIM;
            task->unknown10 = static_cast<int>(Rand01() * 2000.0f + 1500.0f);
        }
    }
    if (task->unknown08 == SEARCH_AIM) {
        // The source dead, or not aggressive: on to the strongest stimulus, else waits.
        if (static_cast<C_Human *>(IdToActor(event->sourceId))->GetHealth() < 1 || !(m_moveFlags & MOVE_AI_AGGRESSIVE)) {
            S_HumanEvent *strongest = m_events->GetStrongestStimulus(m_human->GetFrame()->GetWorldPos());
            if (!strongest || SameEvent(*strongest, *m_event) || !IdToActor(strongest->sourceId)) {
                SetEnemyStance(MOVE_CROUCHING);
                task->unknown08 = SEARCH_IDLE;
                task->unknown10 = IdleTime();
            } else {
                ReleaseHumanEvent(m_event);
                m_event = CopyEvent(*strongest);
                task->unknown08 = SEARCH_LOOK;
                task->unknown0C = 0;
            }
        } else if (task->unknown10 == 0) {
            task->unknown08 = SEARCH_AROUND;
        }
    }
    if (task->unknown08 == SEARCH_AROUND) {
        const S_vector &own = m_human->GetFrame()->GetWorldPos();
        const S_vector &pos = IdToActor(event->sourceId)->GetFrame()->GetWorldPos();
        const float dx = pos.x - own.x;
        const float dy = pos.y - own.y;
        const float dz = pos.z - own.z;
        if (dz * dz + dy * dy + dx * dx < 36.0f) {
            // The source within 6 m: sneaks up to it.
            SetEnemyStance(MOVE_CROUCHING);
            m_alert->Set(4);
            m_move->unknown54 = PACE_SNEAK;
            m_move->SetPos(WorldPos4Of(IdToActor(event->sourceId)->GetFrame()));
            LookAround(0);
            task->unknown08 = SEARCH_TO_SOURCE;
        } else {
            // Searches the points around, on the far side of the event.
            const int point = FindSearchAround();
            if (point == CHECKPOINT_NONE) {
                SetEnemyStance(MOVE_CROUCHING);
                task->unknown08 = SEARCH_IDLE;
                task->unknown10 = IdleTime();
            } else {
                SetEnemyStance(MOVE_STANDING);
                m_alert->Set(1);
                m_move->unknown54 = PACE_ALERT;
                m_move->SetCheckpoint(static_cast<int16_t>(point));
                LookAround(3);
                task->unknown08 = SEARCH_AROUND_WALK;
            }
        }
    }
    if (task->unknown08 == SEARCH_AROUND_WALK && (m_move->flags & S_AIMoveOrder::FLAG_DONE)) {
        // On to another point (60 %), or gives up.
        if (60.0f <= Rand01() * 100.0f) {
            SetEnemyStance(MOVE_CROUCHING);
            task->unknown08 = SEARCH_IDLE;
            task->unknown10 = IdleTime();
        } else {
            task->unknown08 = SEARCH_AROUND;
        }
    }
    if (task->unknown08 == SEARCH_TO_SOURCE) {
        if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE)
            m_facing->SetView(DirFromTo(m_human->GetFrame()->GetWorldPos(), IdToActor(event->sourceId)->GetFrame()->GetWorldPos()));
        if (m_move->flags & S_AIMoveOrder::FLAG_DONE) {
            // There: searches around (80 %), or gives up.
            if (Rand01() * 100.0f < 80.0f) {
                task->unknown08 = SEARCH_AROUND;
                return;
            }
            SetEnemyStance(MOVE_CROUCHING);
            task->unknown08 = SEARCH_IDLE;
            task->unknown10 = IdleTime();
        }
    }
}

int C_EnemyController::FindSearchPoint()
{
    const S_HumanEvent *event = m_event;
    if (!(m_moveFlags & MOVE_AI_AGGRESSIVE)) {
        // The post's point nearest to the event (defensive: any, zombie: a cover point).
        float distance2 = 0.0f;
        return NearestPoint(event->pos, (m_moveFlags & MOVE_AI_DEFENSIVE) ? m_homePoints : m_homeCover, &distance2);
    }
    // Aggressive: a point within 5 m of the event, on the human's side of it.
    const int nearest = m_checkpoints->FindNearestPoint(XYZ(event->pos), 5.0f, -1.0f);
    if (nearest == CHECKPOINT_NONE)
        return CHECKPOINT_NONE;
    std::vector<uint16_t> points;
    m_checkpoints->GetNeighbours(nearest, 5.0f, &points);
    if (points.empty())
        return nearest;
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    const S_vector away(own.x - event->pos.x, own.y - event->pos.y, own.z - event->pos.z);
    for (size_t i = points.size(); i-- > 0;) {
        const S_Checkpoint *point = m_checkpoints->Get(points[i]);
        if (!point)
            continue;
        const S_vector d(point->pos.x - event->pos.x, point->pos.y - event->pos.y, point->pos.z - event->pos.z);
        if (d.z * away.z + d.y * away.y + away.x * d.x < 0.0f) {
            points[i] = points.back();
            points.pop_back();
        }
    }
    if (points.empty())
        return nearest;
    if (points.size() == 1)
        return points[0];
    return points[static_cast<unsigned>(std::rand()) % points.size()];
}

int C_EnemyController::FindSearchAround()
{
    // A point reachable within 20 m, at least 10 m away and not toward the human from the
    // event (more than 60 degrees off).
    const S_HumanEvent *event = m_event;
    std::vector<uint16_t> points;
    m_checkpoints->GetReachable(m_currentCheckpoint, 20.0f, &points);
    if (points.empty())
        return CHECKPOINT_NONE;
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    S_vector away(own.x - event->pos.x, own.y - event->pos.y, own.z - event->pos.z);
    NormalizeBy(away, away.z * away.z + away.y * away.y + away.x * away.x);
    for (size_t i = points.size(); i-- > 0;) {
        const S_Checkpoint *point = m_checkpoints->Get(points[i]);
        if (!point)
            continue;
        const S_vector d(point->pos.x - own.x, point->pos.y - own.y, point->pos.z - own.z);
        if (!(d.z * d.z + d.y * d.y + d.x * d.x < 100.0f)) {
            S_vector dir(point->pos.x - event->pos.x, point->pos.y - event->pos.y, point->pos.z - event->pos.z);
            NormalizeBy(dir, dir.y * dir.y + dir.z * dir.z + dir.x * dir.x);
            if (!(dir.z * away.z + dir.y * away.y + dir.x * away.x > 0.5f))
                continue;
        }
        points[i] = points.back();
        points.pop_back();
    }
    if (points.empty())
        return CHECKPOINT_NONE;
    if (points.size() == 1)
        return points[0];
    return points[static_cast<unsigned>(std::rand()) % points.size()];
}

int C_EnemyController::FindCoverToward(const S_vector4 &pos)
{
    if (pos.x * pos.x + pos.y * pos.y + pos.z * pos.z < 1e-16f)
        return CHECKPOINT_NONE;
    // A cover point reachable within 10 m against `pos`, not farther from it than the human.
    // (The original then drops the points within 45 degrees of the line to `pos` from the
    // list, which it does not use again.)
    std::vector<uint16_t> points;
    m_checkpoints->GetReachable(m_currentCheckpoint, 10.0f, &points);
    if (points.empty())
        return CHECKPOINT_NONE;
    return m_checkpoints->PickCoverPoint(m_human->GetFrame()->GetWorldPos(), XYZ(pos), &points,
                                         COVER_PICK_NOT_FARTHER | COVER_PICK_DROP_REJECTED | 0x4000 | 0x100
                                             | COVER_PICK_FROM_SELF);
}

// ------------------------------------------------------------------------- morale broken

void C_EnemyController::StartBroken()
{
    ReleaseTask();
    m_task = AllocTask5(BROKEN_START);
    m_taskTime = 0;
    if (m_moveFlags & MOVE_LYING)
        SetEnemyStance(MOVE_STANDING);
}

void C_EnemyController::BrokenEvent(S_HumanEvent *event)
{
    switch (event->type) {
    case AI_EVENT_SOUND_0:
    case AI_EVENT_SOUND_2:
    case AI_EVENT_SOUND_1:
    case AI_EVENT_SOUND_6:
    case AI_EVENT_SOUND_8:
        // Looks at the source (not while surrendering with enemies in view).
        if (!IdToActor(event->sourceId) || m_human->HasSeenEnemy(static_cast<C_Human *>(IdToActor(event->sourceId))))
            return;
        if ((m_task->unknown08 & BROKEN_SURRENDER_KIND) && SeesEnemies())
            return;
        m_facing->SetLookAt(static_cast<C_Human *>(IdToActor(event->sourceId))->GetBodyFrame());
        return;
    case AI_EVENT_SHOT_AT:
    case AI_EVENT_LOUD_SOUND:
    case AI_EVENT_HIT_5:
        break;
    default:
        return;
    }
    // Shot at (not while fleeing): surrendering to a shooter in view, the human fights back
    // with a weapon at hand, else flees.
    if ((m_task->unknown08 & BROKEN_FLEE_KIND) || !IdToActor(event->sourceId))
        return;
    if (m_human->HasSeenEnemy(static_cast<C_Human *>(IdToActor(event->sourceId)))) {
        if (!(m_task->unknown08 & BROKEN_SURRENDER_KIND))
            return;
        if (!m_handsItem && !m_human->FindItemByRecordType(1)) {
            m_fireOrder->SetTarget(nullptr);
            m_task->unknown08 = BROKEN_FLEE;
            return;
        }
        m_task->unknown08 = BROKEN_FIGHT;
    } else if (!SeesEnemies()) {
        m_facing->SetLookAt(static_cast<C_Human *>(IdToActor(event->sourceId))->GetBodyFrame());
    }
}

void C_EnemyController::UpdateBroken(int timeDelta)
{
    S_AITask *task = m_task;
    if (task->unknown08 == BROKEN_CALM && SeesEnemies())
        task->unknown08 = BROKEN_START;
    if (task->unknown08 == BROKEN_START) {
        // Enemies in view: a steady human with a weapon fights, a zombie or one with an enemy
        // within 15 m surrenders, the others flee.
        if (!SeesEnemies()) {
            m_alert->Set(0);
            task->unknown08 = BROKEN_CALM;
        } else if (m_handsItem && (m_enemyFlags & FLAG_STEADY)) {
            task->unknown08 = BROKEN_FIGHT;
        } else if ((m_moveFlags & MOVE_AI_ZOMBIE) || GetNearestEnemyDistance() < 15.0f) {
            m_alert->Set(0);
            m_fireOrder->SetTarget(nullptr);
            task->unknown08 = BROKEN_SURRENDER;
        } else {
            m_alert->Set(0);
            m_fireOrder->SetTarget(nullptr);
            task->unknown08 = BROKEN_FLEE;
        }
    }

    switch (task->unknown08) {
    case BROKEN_SURRENDER:
        m_surrenderTime = -1;
        if (!SeesEnemies()) {
            task->unknown08 = BROKEN_START;
            return;
        }
        // Drops the weapon and faces the nearest enemy, standing.
        m_alert->Set(0);
        m_fireOrder->SetTarget(nullptr);
        m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
        SetEnemyStance(MOVE_STANDING);
        DropHandsItem();
        LookAtNearestEnemy();
        m_task->unknown08 = BROKEN_SURRENDERING;
        task->unknown10 = 300;
        break;
    case BROKEN_SURRENDERING:
        if (task->unknown10 != 0)
            break;
        if (m_moveFlags & MOVE_STANDING) {
            // Hands up (network games: the clients are told), for 30 s at least.
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, WORK_ANIMATION_SURRENDER));
            if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL)
                SendMessage23(true);
            m_task->unknown08 = BROKEN_SURRENDERED;
            task->unknown10 = 30000;
            m_surrenderTime = GetGame()->GetMissionTime();
            break;
        }
        SetEnemyStance(MOVE_STANDING);
        task->unknown10 = 300;
        break;
    case BROKEN_SURRENDERED:
        // Keeps facing the nearest enemy (unless one is within 5 m).
        if (!SeesEnemies() || !(GetNearestEnemyDistance() > 5.0f))
            return;
        LookAtNearestEnemy();
        return;
    default:
        break;
    }

    switch (task->unknown08) {
    case BROKEN_FLEE:
        m_surrenderTime = -1;
        if (!SeesEnemies()) {
            task->unknown08 = BROKEN_START;
            return;
        }
        // Away from the enemies (40 m), weapon down.
        m_alert->Set(1);
        LowerWeapon();
        m_move->unknown54 = RunPace();
        m_move->SetFlee(2, 40);
        m_task->unknown08 = BROKEN_FLEEING;
        break;
    case BROKEN_FLEEING:
        // No way away: surrenders.
        m_task->unknown08 = (m_move->flags & S_AIMoveOrder::FLAG_ROUTE) ? BROKEN_FLEE_WALK : BROKEN_SURRENDER;
        break;
    case BROKEN_FLEE_WALK:
        if (m_move->flags & S_AIMoveOrder::FLAG_DONE) {
            m_alert->Set(0);
            m_task->unknown08 = BROKEN_FLED;
            task->unknown10 = 6000;
        } else if (GetNearestEnemyDistance() < 20.0f) {
            // Runs (outdoors) while an enemy is within 20 m, walks again beyond.
            if (IsOutdoors() && !(m_moveFlags & MOVE_PACE_MARCH))
                m_move->unknown54 = PACE_RUN;
        } else if (m_moveFlags & MOVE_PACE_MARCH) {
            m_move->unknown54 = PACE_WALK;
        }
        break;
    case BROKEN_FLED:
        // Calm after 6 s with no enemy within 20 m; an enemy nearer: flees again.
        if (GetNearestEnemyDistance() < 20.0f) {
            m_task->unknown08 = BROKEN_FLEE;
        } else if (task->unknown10 == 0) {
            m_alert->Set(0);
            m_task->unknown08 = BROKEN_CALM;
        }
        break;
    default:
        break;
    }

    switch (task->unknown08) {
    case BROKEN_FIGHT:
        // Fights on: weapon drawn, stands up from the activity (network games: the clients
        // are told).
        m_surrenderTime = -1;
        if (!m_handsItem)
            DrawWeapon();
        SetEnemyStance(MOVE_STANDING);
        m_alert->Set(2);
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, WORK_ANIMATION_END_ACTIVITY));
        if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL)
            SendMessage23(false);
        m_task->unknown08 = BROKEN_FIGHT_PICK;
        return;
    case BROKEN_FIGHT_PICK:
        if (!m_handsItem) {
            m_task->unknown08 = BROKEN_START;
            return;
        }
        m_target = PickTarget(nullptr);
        if (!m_target) {
            m_fireOrder->SetTarget(nullptr);
            m_task->unknown08 = BROKEN_FIGHT_WAIT;
            task->unknown10 = 6000;
            return;
        }
        // A target farther than 10 m: walks toward it. 6 s at it.
        if (GetDistanceTo(m_target) > 10.0f) {
            SetEnemyStance(MOVE_STANDING);
            m_move->SetPos(WorldPos4Of(m_target->GetFrame()));
            m_move->unknown54 = PACE_WALK;
        }
        task->unknown10 = 6000;
        m_task->unknown08 = BROKEN_FIGHT_ON;
        return;
    case BROKEN_FIGHT_ON:
        if (!m_handsItem) {
            m_task->unknown08 = BROKEN_START;
            return;
        }
        // An enemy within 10 m stops the walk.
        if ((m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && GetNearestEnemyDistance() < 10.0f)
            m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
        if (m_target && m_target->GetHealth() > 0 && task->unknown10 != 0 && SeesEnemy(m_target)) {
            Attack(timeDelta);
            return;
        }
        m_task->unknown08 = BROKEN_FIGHT_PICK;
        return;
    case BROKEN_FIGHT_WAIT:
        // Nobody to fire at: calm after 6 s without enemies; reloads meanwhile.
        if (SeesEnemies())
            m_task->unknown08 = BROKEN_FIGHT_PICK;
        else if (task->unknown10 == 0) {
            m_alert->Set(0);
            m_task->unknown08 = BROKEN_CALM;
        }
        if (NeedsReload())
            PressReload();
        return;
    default:
        return;
    }
}
