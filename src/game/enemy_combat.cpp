// C_EnemyController's combat task and the points it (and the guard) fights from
// (game/human_controller.h; original source 0x0054ab70..0x0054b730, 0x00551db0..0x00553d10,
// 0x00554e80..0x00555ca0, 0x005572f0, 0x005579e0).
#include "game/human_controller.h"

#include <cstdlib>
#include <vector>

#include "game/actor.h"
#include "game/checkpoints.h"
#include "game/collision.h"
#include "game/controller_internal.h"
#include "game/enemy_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "ls3d/i3d.h"

namespace {

// The paces the tasks set (S_AIMoveOrder::unknown54).
constexpr uint32_t PACE_WALK = MOVE_PACE_4 | MOVE_FORWARD;         // 0x4008
constexpr uint32_t PACE_RUN = MOVE_PACE_MARCH | MOVE_FORWARD;      // 0x10008
constexpr uint32_t PACE_ALERT = MOVE_PACE_3 | MOVE_FORWARD;        // 0x2008 weapon ready

// PickCoverPoint's flags of the AI's searches (COVER_PICK_*): 0x100 the point covers lying,
// 0x4000 it does not cover standing; 2 is not identified.
constexpr uint32_t COVER_NEAR_LOW = 0xC0004101;      // from the human, not farther from the enemy
constexpr uint32_t COVER_NEAR_LOW_2 = 0xC0004102;
constexpr uint32_t COVER_FAR_LOW = 0x40004104;       // the farthest
constexpr uint32_t COVER_FAR_LYING = 0x40000104;
constexpr uint32_t COVER_LOW = 0x40004101;

// Removes element `index` of the first `size` of `points` as the original did (the last one
// moved into its place, then dropped). The elements past `size` stay readable, as they did in
// the original's vector memory, which FindApproachPoint reads again.
void SwapRemove(std::vector<uint16_t> &points, size_t &size, size_t index)
{
    // Fix: the original's list could run below empty.
    if (size == 0)
        return;
    if (index + 1 < size)
        points[index] = points[size - 1];
    --size;
}

// The same on a whole vector.
void SwapRemove(std::vector<uint16_t> &points, size_t index)
{
    points[index] = points.back();
    points.pop_back();
}

}  // namespace

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

// ------------------------------------------------------------------------- the combat task

void C_EnemyController::CombatEvent(S_HumanEvent *event)
{
    switch (event->type) {
    case AI_EVENT_SOUND_0:
    case AI_EVENT_SOUND_2:
    case AI_EVENT_SHOT_AT:
    case AI_EVENT_LOUD_SOUND:
    case AI_EVENT_HIT_5:
        // Looks at a source not in view.
        if (IdToActor(event->sourceId) && !m_human->HasSeenEnemy(static_cast<C_Human *>(IdToActor(event->sourceId))))
            m_facing->SetLookAt(static_cast<C_Human *>(IdToActor(event->sourceId))->GetBodyFrame());
        break;
    default:
        break;
    }
}

void C_EnemyController::UpdateCombat(int timeDelta)
{
    // The time spent off a route (in cover).
    if (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE))
        m_coverTime += timeDelta;
    S_AITask *task = m_task;
    if (task->unknown08 == COMBAT_LOOK) {
        // No enemy in view: searches once the wait is over.
        if (!SeesEnemies()) {
            if (task->unknown10 == 0)
                StartSearch();
            return;
        }
        task->unknown08 = COMBAT_PICK_TARGET;
    }
    if (task->unknown08 == COMBAT_PICK_TARGET) {
        LookAround(0);
        m_target = PickTarget(nullptr);
        if (!m_target) {
            task->unknown10 = 200;
            task->unknown08 = COMBAT_LOOK;
        } else if (!IsShieldedInVehicle(m_target)) {
            task->unknown08 = COMBAT_MOVE;
        } else if (HasLauncher()) {
            // A target in a vehicle: the launcher.
            m_task->unknown10 = 1000;
            m_task->unknown08 = STATE_LAUNCHER;
        } else if (C_Human *other = NearestTargetOnFoot()) {
            // No launcher: an enemy on foot instead, else cover from the vehicle.
            m_target = other;
            task->unknown08 = COMBAT_MOVE;
        } else if (TakeCoverStance(m_currentCheckpoint, m_target)) {
            m_task->unknown10 = 1000;
            m_task->unknown08 = STATE_HIDING;
        } else {
            m_task->unknown08 = STATE_HIDE_FROM_VEHICLE;
        }
    }
    if (task->unknown08 == COMBAT_MOVE) {
        // Stays (in cover, with friends), else to a point by the weapon's range: walking
        // ready to fire when in range with a weapon fired walking, else running.
        if (ShouldStay()) {
            ChooseCombatStance();
            task->unknown08 = COMBAT_FIRE;
            return;
        }
        const int point = FindCombatPoint();
        if (point == CHECKPOINT_NONE) {
            if (CompareRange() > 0) {
                // Out of range, nowhere to go: straight at the target.
                LowerWeapon();
                m_alert->Set(1);
                m_move->SetPos(WorldPos4Of(m_target->GetFrame()));
                m_move->unknown54 = RunPace();
                LookAround(0);
                SetEnemyStance(MOVE_STANDING);
                task->unknown08 = COMBAT_APPROACH;
                task->unknown0C = 0;
                task->unknown10 = 1000;
                return;
            }
            ChooseCombatStance();
            m_alert->Set(2);
            task->unknown08 = COMBAT_FIRE;
            task->unknown10 = 3000;
            return;
        }
        if (IsInWeaponRange() && CanFireMoving()) {
            m_alert->Set(2);
            m_move->SetCheckpoint(static_cast<int16_t>(point));
            m_move->unknown54 = PACE_WALK;
            LookAround(0);
            SetEnemyStance(MOVE_CROUCHING);
            task->unknown08 = COMBAT_TO_RANGE;
            return;
        }
        LowerWeapon();
        m_alert->Set(1);
        m_move->SetCheckpoint(static_cast<int16_t>(point));
        m_move->unknown54 = RunPace();
        LookAround(0);
        SetEnemyStance(MOVE_STANDING);
        task->unknown08 = COMBAT_TO_COVER;
        return;
    }
    if (task->unknown08 == COMBAT_APPROACH) {
        if (!m_target) {
            m_fireOrder->SetTarget(nullptr);
            m_task->unknown08 = COMBAT_PICK_TARGET;
            m_alert->Set(0);
        }
        if ((m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && task->unknown10 == 0) {
            // Every second on the way: within 10 m beyond the least distance, fires; in range,
            // to cover in range; still out of range, on toward the target, firing after 4..7 s.
            const float distance = GetDistanceTo(m_target);
            if (GetMinDistance() + 10.0f > distance) {
                m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
                m_alert->Set(2);
                task->unknown08 = COMBAT_FIRE;
                task->unknown10 = 3000;
            } else if (CompareRange() != 0) {
                task->unknown10 = 2000;
            } else {
                const int point = FindCoverInRange();
                if (point != CHECKPOINT_NONE) {
                    if (IsInWeaponRange() && CanFireMoving()) {
                        m_alert->Set(2);
                        m_move->SetCheckpoint(static_cast<int16_t>(point));
                        m_move->unknown54 = PACE_WALK;
                        LookAround(0);
                        SetEnemyStance(MOVE_CROUCHING);
                        task->unknown08 = COMBAT_TO_RANGE;
                        return;
                    }
                    LowerWeapon();
                    m_alert->Set(1);
                    m_move->SetCheckpoint(static_cast<int16_t>(point));
                    m_move->unknown54 = RunPace();
                    LookAround(0);
                    SetEnemyStance(MOVE_STANDING);
                    task->unknown08 = COMBAT_TO_COVER;
                    return;
                }
                if (m_target == PickTarget(nullptr)) {
                    m_move->SetPos(WorldPos4Of(m_target->GetFrame()));
                    m_move->unknown54 = RunPace();
                }
                const float elapsed = static_cast<float>(task->unknown0C);
                if (Rand01() * 3000.0f + 4000.0f <= elapsed) {
                    m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
                    m_alert->Set(2);
                    task->unknown08 = COMBAT_FIRE;
                    // (The 1..4 s set here are replaced by 1 s right after.)
                    task->unknown10 = static_cast<int>(Rand01() * 3000.0f + 1000.0f);
                    ChooseCombatStance();
                }
                task->unknown10 = 1000;
            }
        }
        if (m_move->flags & S_AIMoveOrder::FLAG_DONE) {
            m_alert->Set(2);
            task->unknown08 = COMBAT_FIRE;
            task->unknown10 = 3000;
        }
    }
    if (task->unknown08 == COMBAT_TO_RANGE) {
        // Walking ready to fire: fires on the way.
        if (m_move->flags & S_AIMoveOrder::FLAG_DONE) {
            m_alert->Set(2);
            task->unknown08 = COMBAT_FIRE;
            task->unknown10 = 3000;
        }
        if (m_target && m_target->GetHealth() > 0 && SeesEnemy(m_target))
            Attack(timeDelta);
    }
    if (task->unknown08 == COMBAT_TO_COVER && (m_move->flags & S_AIMoveOrder::FLAG_DONE)) {
        m_alert->Set(2);
        task->unknown08 = COMBAT_FIRE;
        task->unknown10 = 3000;
    }
    if (task->unknown08 == COMBAT_FIRE) {
        // Fires for the countdown, then picks a target again.
        if (m_target && m_target->GetHealth() > 0 && SeesEnemy(m_target)) {
            Attack(timeDelta);
        } else {
            m_fireOrder->SetTarget(nullptr);
            m_task->unknown08 = COMBAT_PICK_TARGET;
            m_alert->Set(0);
        }
        if (m_task->unknown10 == 0) {
            m_task->unknown08 = COMBAT_PICK_TARGET;
            m_alert->Set(0);
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
            if (point != CHECKPOINT_NONE) {
                SetEnemyStance(MOVE_STANDING);
                m_alert->Set(1);
                m_move->SetCheckpoint(static_cast<int16_t>(point));
                m_move->unknown54 = PACE_WALK;
                m_task->unknown08 = STATE_TO_LAUNCHER_POINT;
                return;
            }
            m_alert->Set(2);
            m_task->unknown08 = STATE_LAUNCHER_FIRE;
            task->unknown10 = 4000;
            return;
        }
        if (!m_target || m_target->GetHealth() < 1 || m_task->unknown10 == 0) {
            m_task->unknown08 = COMBAT_PICK_TARGET;
            m_alert->Set(0);
        }
        break;
    case STATE_TO_LAUNCHER_POINT:
        if (!m_target) {
            m_task->unknown08 = COMBAT_PICK_TARGET;
            return;
        }
        if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE) {
            // On the way, every second: given up within 8 m of the target.
            if (m_task->unknown10 != 0)
                return;
            if (GetDistanceTo(m_target) < 8.0f)
                m_task->unknown08 = COMBAT_PICK_TARGET;
            task->unknown10 = 1000;
            return;
        }
        if (CompareRange() == 0) {
            m_alert->Set(2);
            task->unknown08 = STATE_LAUNCHER_FIRE;
            task->unknown10 = 3000;
            return;
        }
        m_task->unknown08 = COMBAT_PICK_TARGET;
        return;
    case STATE_LAUNCHER_FIRE:
        // Fires the launcher while the target is in view (for the countdown).
        if (m_target && m_target->GetHealth() > 0 && SeesEnemy(m_target) && m_handsItem && m_task->unknown10 != 0
            && HasLauncherInHands()) {
            Attack(timeDelta);
            break;
        }
        m_fireOrder->SetTarget(nullptr);
        m_task->unknown08 = COMBAT_PICK_TARGET;
        m_alert->Set(0);
        break;
    default:
        break;
    }
    if (m_task->unknown08 == STATE_ATTACK_DONE)
        m_task->unknown08 = COMBAT_PICK_TARGET;

    switch (m_task->unknown08) {
    case STATE_HIDE_FROM_VEHICLE:
        // Hides from the vehicle beside it (out of its guns' reach), else runs away.
        if (m_target && SeesEnemies()) {
            const int point = FindPointBesideVehicle(m_target);
            if (point != CHECKPOINT_NONE) {
                RunForCover();
                m_move->unknown54 = RunPace();
                m_move->SetCheckpoint(static_cast<int16_t>(point));
                task->unknown08 = STATE_TO_HIDING;
                m_task->unknown10 = 1500;
                return;
            }
            LowerWeapon();
            m_alert->Set(1);
            m_move->unknown54 = RunPace();
            m_move->SetFlee(6, 40);
            m_task->unknown08 = STATE_HIDING;
            m_task->unknown10 = 1000;
            return;
        }
        break;
    case STATE_TO_HIDING:
        if (!m_target)
            break;
        if (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE)) {
            if (TakeCoverStance(m_currentCheckpoint, m_target)) {
                task->unknown10 = 2000;
                m_task->unknown08 = STATE_HIDING;
                return;
            }
            break;
        }
        // Every second: walks once the enemies are 20 m away, runs (outdoors) while nearer;
        // within 10 m of the target picks again.
        if (m_task->unknown10 != 0)
            return;
        if (GetDistanceTo(m_target) < 10.0f) {
            m_task->unknown08 = COMBAT_PICK_TARGET;
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
    case STATE_HIDING:
        if (m_task->unknown10 != 0)
            return;
        break;
    default:
        return;
    }
    m_task->unknown08 = COMBAT_PICK_TARGET;
}

// ------------------------------------------------------------------------- the weapon's range

int C_EnemyController::CompareRange()
{
    const float distance = GetDistanceTo(m_target);
    float nearest;
    float farthest;
    GetWeaponRange(&nearest, &farthest);
    if (distance < nearest)
        return -1;
    if (farthest < distance)
        return 1;
    return 0;
}

int C_EnemyController::FindCombatPoint()
{
    // Too near: back; too far: nearer; in range: cover against the target.
    const float distance = GetDistanceTo(m_target);
    float nearest;
    float farthest;
    GetWeaponRange(&nearest, &farthest);
    if (distance < nearest)
        return FindRetreatPoint();
    if (farthest < distance)
        return FindApproachPoint(m_currentCheckpoint, m_target);
    return FindFlankPoint(WorldPos4Of(m_target->GetFrame()));
}

void C_EnemyController::GetWeaponRange(float *nearest, float *farthest)
{
    // The distances (m) to fight at by the weapon in the hands (no weapon: 20..60).
    if (!m_handsItem) {
        *nearest = 20.0f;
        *farthest = 60.0f;
        return;
    }
    switch (BehaviourOf(m_handsItem)) {
    case ITEM_BEHAVIOUR_SIDEARM: *nearest = 5.0f; *farthest = 20.0f; return;
    case ITEM_BEHAVIOUR_SMG: *nearest = 6.0f; *farthest = 30.0f; return;
    case ITEM_BEHAVIOUR_RIFLE: *nearest = 10.0f; *farthest = 60.0f; return;
    case ITEM_BEHAVIOUR_LMG: *nearest = 15.0f; *farthest = 70.0f; return;
    case ITEM_BEHAVIOUR_LAUNCHER: *nearest = 8.0f; *farthest = 35.0f; return;
    case 7: *nearest = 5.0f; *farthest = 15.0f; return;
    case ITEM_BEHAVIOUR_THROWN: *nearest = 12.0f; *farthest = 45.0f; return;
    case ITEM_BEHAVIOUR_ASSAULT: *nearest = 10.0f; *farthest = 70.0f; return;
    default: *nearest = 0.0f; *farthest = 2.0f; return;
    }
}

float C_EnemyController::GetMinDistance()
{
    // How near the enemies may come (no weapon: 20 m).
    if (!m_handsItem)
        return 20.0f;
    switch (BehaviourOf(m_handsItem)) {
    case ITEM_BEHAVIOUR_LAUNCHER: return 4.0f;
    case 7: return 3.0f;
    case ITEM_BEHAVIOUR_THROWN: return 12.0f;
    case ITEM_BEHAVIOUR_ASSAULT: return 5.0f;
    default: return 0.0f;
    }
}

float C_EnemyController::GetMaxDistance()
{
    // How far the weapon reaches.
    if (m_handsItem) {
        switch (BehaviourOf(m_handsItem)) {
        case ITEM_BEHAVIOUR_MELEE: return 2.0f;
        case ITEM_BEHAVIOUR_SIDEARM: return 30.0f;
        case ITEM_BEHAVIOUR_SMG: return 70.0f;
        case ITEM_BEHAVIOUR_RIFLE: return 250.0f;
        case ITEM_BEHAVIOUR_LMG:
        case ITEM_BEHAVIOUR_ASSAULT: return 300.0f;
        case ITEM_BEHAVIOUR_LAUNCHER: return 50.0f;
        case 7: return 25.0f;
        case ITEM_BEHAVIOUR_THROWN: return 45.0f;
        default: break;
        }
    }
    return 0.0f;
}

bool C_EnemyController::IsInWeaponRange()
{
    if (!m_handsItem || !m_target)
        return false;
    const float distance = GetDistanceTo(m_target);
    const float farthest = GetMaxDistance();
    const float nearest = GetMinDistance();
    return nearest <= distance && distance <= farthest;
}

bool C_EnemyController::CanFireMoving()
{
    // Fix: the original read the behaviour of no item.
    if (!m_handsItem)
        return false;
    switch (BehaviourOf(m_handsItem)) {
    case ITEM_BEHAVIOUR_MELEE:
    case ITEM_BEHAVIOUR_SIDEARM:
    case ITEM_BEHAVIOUR_SMG:
    case 7:
    case ITEM_BEHAVIOUR_THROWN:
        return true;
    default:
        return false;
    }
}

bool C_EnemyController::HasLineTo(const S_vector4 &from)
{
    // Nothing between `from` and the target's body.
    const S_vector &to = m_target->GetBodyFrame()->GetWorldPos();
    const S_vector dir(to.x - from.x, to.y - from.y, to.z - from.z);
    return !g_collisionWorld.RayTest(XYZ(from), dir, nullptr, nullptr, 3, 0);
}

bool C_EnemyController::CanFireLying()
{
    // The target 20 m away or more and within 10 degrees of level, a rifle or a machine gun,
    // nobody within 2 m, outdoors, and a clear line from the ground half a metre ahead.
    const S_vector4 own = WorldPos4Of(m_human->GetFrame());
    const S_vector &target = m_target->GetFrame()->GetWorldPos();
    const float dx = target.x - own.x;
    const float dy = target.y - own.y;
    const float dz = target.z - own.z;
    const float square = dy * dy + dz * dz + dx * dx;
    if (square < 1e-8f || std::fabs(square - 1.0f) < 1e-8f || std::sqrt(square) < 20.0f)
        return false;
    const float magnitude = MagnitudeBy(square);
    const float sine = dy / magnitude < 0.0f ? -(dy / magnitude) : dy / magnitude;
    if (sine > 0.173648f)
        return false;
    if (!m_handsItem)
        return false;
    switch (BehaviourOf(m_handsItem)) {
    case ITEM_BEHAVIOUR_MELEE:
    case ITEM_BEHAVIOUR_SIDEARM:
    case ITEM_BEHAVIOUR_LAUNCHER:
    case 7:
    case ITEM_BEHAVIOUR_THROWN:
        return false;
    default:
        break;
    }
    if (!m_events->IsAwayFromNearbyActors(XYZ(own)) || !IsOutdoors())
        return false;
    S_vector ahead(dx, 0.0f, dz);
    NormalizeBy(ahead, dz * dz + dx * dx);
    const S_vector4 from = {ahead.x * 0.5f + own.x, own.y + 0.15f + ahead.y * 0.5f, ahead.z * 0.5f + own.z, own.w};
    return HasLineTo(from);
}

// ------------------------------------------------------------------------- stances

bool C_EnemyController::TakeCoverStance(const S_Checkpoint *cp, C_Human *target)
{
    if (!cp)
        return false;
    if (!target || !target->GetVehicle()) {
        // Standing in cover from every enemy in view. (The original then tests standing once
        // more, meant for crouching (it would crouch then), which cannot hold here.)
        const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
        for (size_t i = enemies.size(); i-- > 0;) {
            const S_vector dir = enemies[i]->GetFrame()->GetWorldPos() - cp->pos;
            if (!cp->GetCoverStances(S_Checkpoint::GetDirectionBit(dir), 4))
                return false;
        }
        return true;
    }
    // A target in a vehicle: standing in cover from it; 10 m from the vehicle or more,
    // crouching, else lying.
    const S_vector &pos = target->GetFrame()->GetWorldPos();
    if (cp->GetCoverStances(S_Checkpoint::GetDirectionBit(pos - cp->pos), 4))
        return true;
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    const S_vector &vehicle = target->GetVehicle()->GetFrame()->GetWorldPos();
    const S_vector d(own.x - vehicle.x, own.y - (vehicle.y + 1.0f), own.z - vehicle.z);
    if (d.y * d.y + d.z * d.z + d.x * d.x < 100.0f)
        return false;
    if (cp->GetCoverStances(S_Checkpoint::GetDirectionBit(pos - cp->pos), 2)) {
        SetEnemyStance(MOVE_CROUCHING);
        return true;
    }
    if (cp->GetCoverStances(S_Checkpoint::GetDirectionBit(pos - cp->pos), 1)) {
        SetEnemyStance(MOVE_LYING);
        return true;
    }
    return false;
}

void C_EnemyController::ChooseCombatStance()
{
    // The lowest stance the point leaves a line of fire to the target from (lying only where
    // it can fire lying).
    const S_Checkpoint *cp = m_currentCheckpoint;
    if (!cp || !m_target)
        return;
    const uint8_t bit = S_Checkpoint::GetDirectionBit(m_target->GetFrame()->GetWorldPos() - cp->pos);
    if (!cp->GetCoverStances(bit, 1) && CanFireLying()) {
        SetEnemyStance(MOVE_LYING);
        return;
    }
    if (!cp->GetCoverStances(bit, 2)) {
        SetEnemyStance(MOVE_CROUCHING);
        return;
    }
    if (!(cp->cover4 & bit))
        SetEnemyStance(MOVE_STANDING);
}

bool C_EnemyController::ShouldStay()
{
    if (!m_target || !m_currentCheckpoint)
        return false;
    // Enemies in view for less than 2 s: stays (with friends near, unless rand() gives 0).
    if (m_enemySeenTime < 2000) {
        if (HasFriendNearby()) {
            const float r = Rand01();
            if (r + r == 0.0f)
                return false;
        }
        return true;
    }
    // In cover lying from the target: stays up to 7 s.
    const S_Checkpoint *cp = m_currentCheckpoint;
    if (cp->cover1 & S_Checkpoint::GetDirectionBit(m_target->GetFrame()->GetWorldPos() - cp->pos)) {
        if (m_coverTime < 7001)
            return true;
        m_coverTime = 0;
    }
    return false;
}

bool C_EnemyController::HasFriendNearby()
{
    for (int i = 0; i < m_events->GetNearbyActorCount(); ++i) {
        C_Actor *actor = m_events->GetNearbyActor(i);
        if (actor && actor->GetType() == ACTOR_HUMAN && static_cast<C_Human *>(actor)->IsFriendOf(m_human))
            return true;
    }
    return false;
}

bool C_EnemyController::MoveToCombatPoint(int checkpoint)
{
    // The guard to `checkpoint`: an enemy nearer than the least distance, standing ready to
    // fire; out of reach, walking; in range, crouching and firing with a weapon fired walking;
    // else running.
    if (checkpoint == CHECKPOINT_NONE)
        return false;
    const float nearest = GetMinDistance();
    if (GetNearestEnemyDistance() < nearest) {
        SetEnemyStance(MOVE_STANDING);
        m_alert->Set(2);
        m_move->SetCheckpoint(static_cast<int16_t>(checkpoint));
        m_move->unknown54 = PACE_ALERT;
        m_task->unknown08 = GUARD_MOVE_FIRING;
        return true;
    }
    const float farthest = GetMaxDistance();
    if (farthest < GetNearestEnemyDistance()) {
        LowerWeapon();
        SetEnemyStance(MOVE_STANDING);
        m_alert->Set(1);
        m_move->SetCheckpoint(static_cast<int16_t>(checkpoint));
        m_move->unknown54 = PACE_WALK;
        m_task->unknown08 = GUARD_MOVE;
        return true;
    }
    if (IsInWeaponRange() && CanFireMoving()) {
        SetEnemyStance(MOVE_CROUCHING);
        m_alert->Set(2);
        m_move->SetCheckpoint(static_cast<int16_t>(checkpoint));
        m_move->unknown54 = PACE_WALK;
        m_task->unknown08 = GUARD_MOVE_FIRING;
        return true;
    }
    if (!CanFireMoving())
        LowerWeapon();
    SetEnemyStance(MOVE_STANDING);
    m_alert->Set(1);
    m_move->SetCheckpoint(static_cast<int16_t>(checkpoint));
    m_move->unknown54 = RunPace();
    m_task->unknown08 = GUARD_MOVE;
    return true;
}

// ------------------------------------------------------------------------- points to fight from

int C_EnemyController::FindRetreatPoint()
{
    // Back from a target too near: the point reachable within 20 m, away from the target (not
    // straight away), with the most cover from the enemies in view: crouching 3, lying 2.
    if (GetDistanceTo(m_target) < 12.0f && !CanFireMoving())
        return CHECKPOINT_NONE;
    if (!m_checkpoints || !m_currentCheckpoint)
        return CHECKPOINT_NONE;
    std::vector<uint16_t> points;
    m_checkpoints->GetReachableCopy(m_currentCheckpoint, 20.0f, &points);
    if (points.empty())
        return CHECKPOINT_NONE;
    const S_vector4 own = WorldPos4Of(m_human->GetFrame());
    const S_vector &target = m_target->GetFrame()->GetWorldPos();
    S_vector toward(target.x - own.x, target.y - own.y, target.z - own.z);
    NormalizeBy(toward, toward.x * toward.x + toward.y * toward.y + toward.z * toward.z);
    for (size_t i = points.size(); i-- > 0;) {
        const S_Checkpoint *point = m_checkpoints->Get(points[i]);
        S_vector d(point->pos.x - own.x, point->pos.y - own.y, point->pos.z - own.z);
        bool keep = false;
        if (d.x * toward.x + d.y * toward.y + d.z * toward.z < 0.0f) {
            NormalizeBy(d, d.x * d.x + d.y * d.y + d.z * d.z);
            keep = !(toward.x * d.x + toward.z * d.z + toward.y * d.y < -0.96f);
        }
        if (!keep)
            SwapRemove(points, i);
    }
    if (points.empty())
        return CHECKPOINT_NONE;
    std::vector<int> scores(points.size(), 0);
    for (size_t i = points.size(); i-- > 0;) {
        const S_Checkpoint *point = m_checkpoints->Get(points[i]);
        if (point == m_currentCheckpoint)
            continue;
        const S_vector d = point->pos - m_human->GetFrame()->GetWorldPos();
        const float distance2 = d.x * d.x + d.y * d.y + d.z * d.z;
        const S_vector fromTarget = point->pos - m_target->GetFrame()->GetWorldPos();
        const float weight = (distance2 > 1.0f ? 5.0f / distance2 : 5.0f) + fromTarget.Square();
        const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
        for (size_t k = enemies.size(); k-- > 0;) {
            const uint32_t cover = CoverAgainst(point, enemies[k]->GetFrame()->GetWorldPos(), 7);
            // (Standing cover scores nothing: the original multiplies the standing bit by the
            // weight and only tests the product for 0.)
            int score = 0;
            if (static_cast<float>(cover & 4) * weight == 0.0f)
                score = (cover & 2) ? 3 : static_cast<int>(cover & 1) * 2;
            scores[i] += score;
        }
    }
    int best = CHECKPOINT_NONE;
    int bestScore = -1;
    for (size_t i = scores.size(); i-- > 0;) {
        if (bestScore < scores[i]) {
            best = points[i];
            bestScore = scores[i];
        }
    }
    return best;
}

int C_EnemyController::FindApproachPoint(const S_Checkpoint *from, C_Human *target)
{
    // Toward a target too far: a cover point reachable within 15..25 m, nearer the target than
    // the human (by its radius) but 6 m from it at least, and nearer the human than the
    // target is.
    if (!target)
        return CHECKPOINT_NONE;
    const int r = std::rand();
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    const S_vector &pos = target->GetFrame()->GetWorldPos();
    const S_vector away(own.x - pos.x, own.y - pos.y, own.z - pos.z);
    const float distance = MagnitudeBy(away.z * away.z + away.y * away.y + away.x * away.x);
    std::vector<uint16_t> points;
    if (m_checkpoints->GetReachableCover(from, static_cast<float>(r) * (1.0f / 32767.0f) * 10.0f + 15.0f, &points)) {
        if (points.empty())
            return CHECKPOINT_NONE;
        // (Only its filtering of the points is used.)
        m_checkpoints->PickCoverPoint(own, pos, &points, COVER_NEAR_LOW_2);
        // (A point too far from the target that also fails the other test drops a second
        // one: the original goes on to the second test after removing it.)
        size_t size = points.size();
        for (size_t i = points.size(); i-- > 0;) {
            const S_Checkpoint *point = m_checkpoints->Get(points[i]);
            if (!point)
                continue;
            const S_vector a(own.x - point->pos.x, own.y - point->pos.y, own.z - point->pos.z);
            const float fromHuman = MagnitudeBy(a.z * a.z + a.y * a.y + a.x * a.x);
            const S_vector b(pos.x - point->pos.x, pos.y - point->pos.y, pos.z - point->pos.z);
            const float fromTarget = MagnitudeBy(b.z * b.z + b.y * b.y + b.x * b.x) - static_cast<float>(point->radius) * 0.0625f;
            if (fromTarget > distance)
                SwapRemove(points, size, i);
            if (fromTarget < 6.0f)
                SwapRemove(points, size, i);
            else if (fromHuman > distance)
                SwapRemove(points, size, i);
        }
        points.resize(size);
    }
    if (points.empty())
        return CHECKPOINT_NONE;
    if (points.size() == 1)
        return points[0];
    return points[static_cast<unsigned>(std::rand()) % points.size()];
}

int C_EnemyController::FindFlankPoint(const S_vector4 &pos)
{
    // In range: a cover point against `pos` (the post's points; aggressive: those reachable
    // within 15 m) where the human can fire lying (standing exposed).
    if (pos.z * pos.z + pos.y * pos.y + pos.x * pos.x < 1e-16f)
        return CHECKPOINT_NONE;
    std::vector<uint16_t> points;
    if (!(m_moveFlags & MOVE_AI_AGGRESSIVE))
        points = m_homePoints;
    else
        m_checkpoints->GetReachable(m_currentCheckpoint, 15.0f, &points);
    if (points.empty())
        return CHECKPOINT_NONE;
    // (The original then measures, for a point other than the current one, the angle at the
    // human between `pos` and it (0x00556610), and drops the result.)
    return m_checkpoints->PickCoverPoint(m_human->GetFrame()->GetWorldPos(), XYZ(pos), &points, COVER_LOW);
}

int C_EnemyController::FindCoverInRange()
{
    // On the way to the target: a cover point reachable within 10 m, toward the target, with
    // the target within the weapon's range from it.
    if (GetDistanceTo(m_target) < 12.0f && !CanFireMoving())
        return CHECKPOINT_NONE;
    if (!m_checkpoints || !m_currentCheckpoint)
        return CHECKPOINT_NONE;
    std::vector<uint16_t> points;
    m_checkpoints->GetReachableCopy(m_currentCheckpoint, 10.0f, &points);
    if (points.empty())
        return CHECKPOINT_NONE;
    const S_vector own = m_human->GetFrame()->GetWorldPos();
    const S_vector target = m_target->GetFrame()->GetWorldPos();
    S_vector toward(target.x - own.x, target.y - own.y, target.z - own.z);
    NormalizeBy(toward, toward.x * toward.x + toward.y * toward.y + toward.z * toward.z);
    const float farthest = GetMaxDistance();
    const float nearest = GetMinDistance();
    for (size_t i = points.size(); i-- > 0;) {
        const S_Checkpoint *point = m_checkpoints->Get(points[i]);
        const S_vector d(point->pos.x - own.x, point->pos.y - own.y, point->pos.z - own.z);
        bool keep = false;
        if (!(d.x * toward.x + d.y * toward.y + d.z * toward.z < 0.0f)) {
            const S_vector t(target.x - point->pos.x, target.y - point->pos.y, target.z - point->pos.z);
            const float distance = MagnitudeBy(t.z * t.z + t.y * t.y + t.x * t.x);
            keep = !(distance < nearest || farthest < distance);
        }
        if (!keep)
            SwapRemove(points, i);
    }
    if (points.empty())
        return CHECKPOINT_NONE;
    return m_checkpoints->PickCoverPoint(own, target, &points, COVER_NEAR_LOW);
}

int C_EnemyController::FindLauncherPoint(C_Human *target)
{
    // To fire a launcher at a vehicle from: a cover point 15..40 m from it, on the human's
    // side, out of its front (45 degrees) within 30 m (the post's cover points; aggressive:
    // the points reachable within 15 m).
    if (!target || !target->GetVehicle())
        return CHECKPOINT_NONE;
    C_Actor *vehicle = target->GetVehicle();
    std::vector<uint16_t> points;
    if (!(m_moveFlags & MOVE_AI_AGGRESSIVE))
        points = m_homeCover;
    else
        m_checkpoints->GetReachable(m_currentCheckpoint, 15.0f, &points);
    const S_vector4 own = WorldPos4Of(m_human->GetFrame());
    const S_vector &vehiclePos = vehicle->GetFrame()->GetWorldPos();
    const S_vector center(vehiclePos.x, vehiclePos.y + 1.0f, vehiclePos.z);
    const S_matrix &m = vehicle->GetFrame()->GetMatrix();
    S_vector front(m.m[2][0], m.m[2][1], m.m[2][2]);
    NormalizeBy(front, front.x * front.x + front.y * front.y + front.z * front.z);
    front = S_vector(front.x * 10.0f, front.y * 10.0f, front.z * 10.0f);
    const int picked = m_checkpoints->PickCoverPoint(XYZ(own), center, &points, COVER_FAR_LOW);
    if (points.empty())
        return picked;
    front.y = 0.0f;
    NormalizeBy(front, front.x * front.x + front.z * front.z);
    for (size_t i = points.size(); i-- > 0;) {
        const S_Checkpoint *point = m_checkpoints->Get(points[i]);
        if (!point)
            continue;
        const S_vector d(point->pos.x - center.x, point->pos.y - center.y, point->pos.z - center.z);
        const float distance2 = d.x * d.x + d.y * d.y + d.z * d.z;
        bool remove;
        if (distance2 < 225.0f || distance2 > 1600.0f) {
            remove = true;
        } else {
            S_vector flat(d.x, 0.0f, d.z);
            NormalizeBy(flat, flat.z * flat.z + flat.x * flat.x);
            const float ahead = flat.z * front.z + flat.x * front.x + flat.y * front.y;
            if (ahead > 0.707f && distance2 < 900.0f) {
                remove = true;
            } else {
                S_vector away(own.x - center.x, 0.0f, own.z - center.z);
                NormalizeBy(away, away.x * away.x + away.z * away.z);
                remove = away.x * flat.x + away.y * flat.y + away.z * flat.z < 0.0f;
            }
        }
        if (remove)
            SwapRemove(points, i);
    }
    if (points.empty())
        return CHECKPOINT_NONE;
    return points[static_cast<unsigned>(std::rand()) % points.size()];
}

int C_EnemyController::FindPointBesideVehicle(C_Human *target)
{
    // To hide from a vehicle's guns: a cover point (lying) 5 m from the vehicle at least, not
    // in its front within 31 m nor ahead near it, not on its far side, and not beside its
    // front on the human's side (the post's cover points; aggressive: those reachable within
    // 25 m).
    if (!target || !target->GetVehicle())
        return CHECKPOINT_NONE;
    C_Actor *vehicle = target->GetVehicle();
    std::vector<uint16_t> points;
    if (!(m_moveFlags & MOVE_AI_AGGRESSIVE))
        points = m_homeCover;
    else
        m_checkpoints->GetReachable(m_currentCheckpoint, 25.0f, &points);
    if (points.empty())
        return CHECKPOINT_NONE;
    const S_vector4 own = WorldPos4Of(m_human->GetFrame());
    const S_vector &vehiclePos = vehicle->GetFrame()->GetWorldPos();
    const S_vector center(vehiclePos.x, vehiclePos.y + 1.0f, vehiclePos.z);
    const S_matrix &m = vehicle->GetFrame()->GetMatrix();
    S_vector front(m.m[2][0], m.m[2][1], m.m[2][2]);
    NormalizeBy(front, front.y * front.y + front.x * front.x + front.z * front.z);
    front = S_vector(front.x * 10.0f, front.y * 10.0f, front.z * 10.0f);
    const int picked = m_checkpoints->PickCoverPoint(XYZ(own), center, &points, COVER_FAR_LYING);
    if (points.empty())
        return picked;
    front.y = 0.0f;
    NormalizeBy(front, front.x * front.x + front.z * front.z);
    for (size_t i = points.size(); i-- > 0;) {
        const S_Checkpoint *point = m_checkpoints->Get(points[i]);
        if (!point)
            continue;
        const S_vector d(point->pos.x - center.x, point->pos.y - center.y, point->pos.z - center.z);
        const float distance2 = d.x * d.x + d.y * d.y + d.z * d.z;
        S_vector flat(d.x, 0.0f, d.z);
        NormalizeBy(flat, flat.x * flat.x + flat.z * flat.z);
        const float ahead = flat.z * front.z + flat.x * front.x + flat.y * front.y;
        bool remove;
        if (ahead > 0.707f && distance2 < 1000.0f) {
            remove = true;
        } else if (ahead > 0.0f && distance2 < ahead * 100.0f + 25.0f) {
            remove = true;
        } else if (distance2 < 25.0f) {
            remove = true;
        } else {
            S_vector away(own.x - center.x, 0.0f, own.z - center.z);
            NormalizeBy(away, away.x * away.x + away.z * away.z);
            const float side = away.x * flat.x + away.y * flat.y + away.z * flat.z;
            remove = false;
            if (side < -0.5f) {
                remove = true;
            } else if (side > 0.0f && ahead > 0.0f) {
                // Both ahead on the same side of the vehicle's line.
                const float pointSide = flat.z * -front.x + front.z * flat.x + flat.y * 0.0f;
                const float humanSide = -front.x * away.z + front.z * away.x + away.y * 0.0f;
                remove = pointSide * humanSide > 0.0f;
            }
        }
        if (remove)
            SwapRemove(points, i);
    }
    if (points.empty())
        return CHECKPOINT_NONE;
    return points[static_cast<unsigned>(std::rand()) % points.size()];
}

int C_EnemyController::PickCoverAwayFrom(C_Human *target)
{
    // The guard: one of the post's cover points against `target`, 6 m from it at least
    // (none: nothing to pick, or no cover at the post, which also set the state).
    if (m_task->type != 1)
        return CHECKPOINT_NONE;
    if (!target) {
        m_task->unknown08 = GUARD_LOOK;
        return CHECKPOINT_NONE;
    }
    if (m_homeCover.empty()) {
        m_task->unknown08 = GUARD_FIRE;
        return CHECKPOINT_NONE;
    }
    std::vector<uint16_t> points = m_homeCover;
    const S_vector &pos = target->GetFrame()->GetWorldPos();
    // (Only its filtering of the points is used.)
    m_checkpoints->PickCoverPoint(m_human->GetFrame()->GetWorldPos(), pos, &points, COVER_NEAR_LOW_2);
    for (size_t i = points.size(); i-- > 0;) {
        const S_Checkpoint *point = m_checkpoints->Get(points[i]);
        if (!point)
            continue;
        const float dx = pos.x - point->pos.x;
        const float dy = pos.y - point->pos.y;
        const float dz = pos.z - point->pos.z;
        if (MagnitudeBy(dx * dx + dy * dy + dz * dz) - static_cast<float>(point->radius) * 0.0625f < 6.0f)
            points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
    }
    if (points.empty())
        return CHECKPOINT_NONE;
    return points[static_cast<unsigned>(std::rand()) % points.size()];
}
