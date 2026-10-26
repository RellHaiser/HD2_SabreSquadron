// C_EnemyController's gunner task (the seat weapon of a car or a static gun) and the static
// guns of the guard's post (game/human_controller.h; original source 0x0054cbb0..0x0054f910).
#include "game/human_controller.h"

#include <cstdlib>
#include <deque>
#include <vector>

#include "game/actor.h"
#include "game/collision.h"
#include "game/controller_internal.h"
#include "game/enemy_internal.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/pending.h"
#include "game/soldier.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

S_vector4 Dir4Of(const S_vector &dir)
{
    return {dir.x, dir.y, dir.z, 0.0f};                 // (w: a stale local in the original)
}

// The enemies in view as the gun's targets: the AI's sight test also passes those it hears
// (or reports) in a way the original keeps a count of; a defensive gunner fires at those it
// counts 3 for, an aggressive one at 1 (C_SightModule::CanSee).
bool MayFireAt(uint32_t moveFlags, int seen, bool inView)
{
    if (inView)
        return true;
    if (moveFlags & MOVE_AI_ZOMBIE)
        return false;
    if ((moveFlags & MOVE_AI_DEFENSIVE) && seen < 3)
        return false;
    if ((moveFlags & MOVE_AI_AGGRESSIVE) && seen < 1)
        return false;
    return true;
}

}  // namespace

S_vector4 GetLeadPoint(const C_Human *human)
{
    // Standing or crouching: the body 0.3 m lower; lying: the human's position.
    S_vector4 point;
    const uint32_t flags = human->GetController()->GetMoveFlags();
    if (flags & (MOVE_STANDING | MOVE_CROUCHING)) {
        const S_vector &body = human->GetBodyFrame()->GetWorldPos();
        point = {body.x, body.y - 0.3f, body.z, 0.0f};  // (w: a stale local in the original)
    } else {
        point = WorldPos4Of(human->GetFrame());
    }
    const S_vector &velocity = human->GetVelocity();
    point.x += velocity.x * 0.125f;
    point.y += velocity.y * 0.125f;
    point.z += velocity.z * 0.125f;
    return point;
}

// ------------------------------------------------------------------------- the gunner task

void C_EnemyController::GunnerEvent(S_HumanEvent *event)
{
    switch (event->type) {
    case AI_EVENT_BODY_SEEN:
    case AI_EVENT_BODY_HEARD: {
        // A friend's body: watching, the gun turns to his killer.
        if ((m_enemyFlags & FLAG_CANNON) || m_task->unknown08 != GUNNER_WATCH)
            return;
        C_Actor *body = IdToActor(event->actorId);
        if (!body || !m_human->IsFriendOf(body))
            return;
        C_Actor *attacker = static_cast<C_Human *>(body)->m_events->GetAttacker();
        // (Fix: the original tested the killer's health and reach before it tested that he
        // is a human.)
        if (!attacker || attacker->GetType() != ACTOR_HUMAN)
            return;
        C_Human *killer = static_cast<C_Human *>(attacker);
        if (killer->GetHealth() < 1 || IsOutOfGunReach(killer))
            return;
        m_target = killer;
        m_task->unknown10 = static_cast<int>(Rand01() * 2000.0f + 1500.0f);
        m_task->unknown08 = GUNNER_FIRE;
        m_targetChanged = true;
        return;
    }
    case AI_EVENT_FRIEND_HEARD:
    case AI_EVENT_SOUND_2:
    case AI_EVENT_SOUND_1:
    case AI_EVENT_SOUND_6:
    case AI_EVENT_SOUND_8: {
        // Something heard while watching: looks there; a human enemy in reach is the target.
        if ((m_enemyFlags & FLAG_CANNON) || m_task->unknown08 != GUNNER_WATCH)
            return;
        C_Actor *source = IdToActor(event->sourceId);
        if (!source || m_human->IsFriendOf(source))
            return;
        // (Fix: the original read the health of any actor; one not a human counts as alive.)
        if (source->GetType() == ACTOR_HUMAN && static_cast<C_Human *>(source)->GetHealth() < 1)
            return;
        const S_vector4 pos = {event->pos.x, event->pos.y + 1.0f, event->pos.z, 0.0f};   // (w: a stale local)
        LookAroundAt(7, pos);
        if (source->GetType() != ACTOR_HUMAN || IsOutOfGunReach(static_cast<C_Human *>(source)))
            return;
        m_target = static_cast<C_Human *>(source);
        m_task->unknown10 = static_cast<int>(Rand01() * 2500.0f + 1000.0f);
        m_task->unknown08 = GUNNER_FIRE;
        m_targetChanged = true;
        return;
    }
    case AI_EVENT_SOUND_0:
    case AI_EVENT_SHOT_AT:
    case AI_EVENT_LOUD_SOUND:
    case AI_EVENT_HIT_5: {
        // Shot at by an enemy not in view: watching, picks a target; at a static gun, one
        // within 8 m or out of the gun's reach drives the gunner off it.
        C_Actor *source = IdToActor(event->sourceId);
        if (!m_human->GetVehicle() || !source || m_human->IsFriendOf(source) || source->GetType() != ACTOR_HUMAN
            || static_cast<C_Human *>(source)->GetHealth() < 1
            || m_human->HasSeenEnemy(static_cast<C_Human *>(source)))
            return;
        // (The original also tests a function that returns true, 0x0054d560.)
        if (!IsAtStaticWeapon()
            || (!(GetDistanceTo(source) < 8.0f) && !IsOutOfGunReach(static_cast<C_Human *>(source)))) {
            if (m_task->unknown08 == GUNNER_WATCH)
                m_task->unknown08 = GUNNER_PICK;
            return;
        }
        m_task->unknown08 = GUNNER_LEAVE;
        return;
    }
    default:
        return;
    }
}

void C_EnemyController::UpdateGunner(int /*timeDelta*/)
{
    if (m_task->unknown08 == GUNNER_START) {
        // Alert; a seat weapon of component kind 2 is a cannon.
        SetAlertState(2);
        m_task->unknown08 = GUNNER_PICK;
        C_Actor *vehicle = m_human->GetVehicle();
        if (vehicle) {
            C_ItemDef *weapon = vehicle->GetSeatWeaponDef(m_human->GetVehicleSeat());
            if (weapon && weapon->GetComponentKind() == ICK_PROJECTILE_WEAPON)
                m_enemyFlags |= FLAG_CANNON;
        }
    }
    if (m_task->unknown08 == GUNNER_WATCH) {
        // Nobody in view: looks around (a cannon sweeps), until the wait is over.
        if (!(m_moveFlags & MOVE_LOOKING_AROUND))
            LookAround(3);
        if (SeesEnemies()) {
            m_task->unknown08 = GUNNER_PICK;
        } else if (m_task->unknown10 == 0) {
            LookAround(0);
            m_task->unknown08 = GUNNER_DONE;
        } else if (m_enemyFlags & FLAG_CANNON) {
            SweepGun();
        }
    }
    if (m_task->unknown08 == GUNNER_PICK) {
        m_target = PickGunnerTarget();
        if (!m_target) {
            if (SeesEnemies() && (m_enemyFlags & FLAG_CANNON)) {
                m_task->unknown10 = GetGunnerDelay();
                m_task->unknown08 = GUNNER_PAUSE;
            } else {
                // Watches for 15..25 s.
                m_facing->SetLookAround(true, 4000);
                m_task->unknown10 = static_cast<int>(Rand01() * 10000.0f + 15000.0f);
                m_task->unknown08 = GUNNER_WATCH;
            }
        } else if (IsOutOfGunReach(m_target)) {
            m_target = nullptr;
            m_task->unknown10 = GetGunnerDelay();
            m_task->unknown08 = GUNNER_PAUSE;
        } else {
            // Turns the gun to it for 0.5..3.5 s, firing.
            m_task->unknown10 = static_cast<int>(Rand01() * 3000.0f + 500.0f);
            m_task->unknown08 = GUNNER_FIRE;
            m_targetChanged = true;
        }
    }
    if (m_task->unknown08 == GUNNER_FIRE) {
        if (IsGunnerThreatened()) {
            m_task->unknown08 = GUNNER_LEAVE;
        } else if (!m_target || (m_target->GetHealth() > 0 && IsOutOfGunReach(m_target))) {
            m_task->unknown08 = GUNNER_PICK;
        } else if (IsGunEmpty()) {
            m_axes[AXIS_RELOAD] = 1.0f;
            m_task->unknown10 = GetGunnerDelay();
            m_task->unknown08 = GUNNER_PAUSE;
        } else if (m_task->unknown10 == 0) {
            // The burst is over: a pause.
            m_task->unknown10 = GetGunnerDelay();
            m_task->unknown08 = GUNNER_PAUSE;
        } else if (m_enemyFlags & FLAG_CANNON) {
            AimAndFireCannon();
        } else {
            AimAndFireGun();
        }
    }
    if (m_task->unknown08 == GUNNER_PAUSE) {
        // Between bursts: keeps aiming (a cannon sweeps), then picks a target again.
        if (IsGunnerThreatened())
            m_task->unknown08 = GUNNER_LEAVE;
        else if (m_task->unknown10 == 0)
            m_task->unknown08 = GUNNER_PICK;
        else if (m_enemyFlags & FLAG_CANNON)
            SweepGun();
        else if (SeesEnemy(m_target))
            AimGun();
    }
    switch (m_task->unknown08) {
    case GUNNER_DONE:
        // Nothing more to fire at: one who was at the gun from the start goes back to the
        // script there, the others leave it.
        LookAround(0);
        if (m_enemyFlags & FLAG_GUNNER) {
            m_enemyFlags |= FLAG_DONE;
            return;
        }
        LeaveGun();
        return;
    case GUNNER_GIVE_UP:
        m_target = nullptr;
        LookAround(0);
        SetAlertState(0);
        return;
    case GUNNER_LEAVE:
        m_target = nullptr;
        LookAround(0);
        SetAlertState(0);
        LeaveGun();
        return;
    default:
        return;
    }
}

C_Human *C_EnemyController::PickGunnerTarget()
{
    // The best enemy in view (a cannon: only those), else the source of a fresh stimulus not
    // in view the gun can point at, if it is nearer (stimuli count 1.4 times as far).
    C_Actor *vehicle = m_human->GetVehicle();
    if (!vehicle)
        return nullptr;
    if (m_enemyFlags & FLAG_CANNON)
        return PickSeenTarget();
    C_Actor *best = PickSeenTarget();
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    float best2 = 1e6f;
    if (best) {
        const S_vector &pos = best->GetFrame()->GetWorldPos();
        const float dx = pos.x - own.x;
        const float dy = pos.y - own.y;
        const float dz = pos.z - own.z;
        best2 = dz * dz + dy * dy + dx * dx;
    }
    std::deque<S_HumanEvent *> &stimuli = m_events->GetStimuli();
    for (size_t i = 0; i < stimuli.size(); ++i) {
        S_HumanEvent *event = stimuli[i];
        if (event->age * 2 > event->timeLeft)
            continue;
        C_Actor *source = IdToActor(event->sourceId);
        if (!source)
            continue;
        if (source->GetType() == ACTOR_HUMAN && static_cast<C_Human *>(source)->GetHealth() < 1)
            continue;
        if (m_human->HasSeenEnemy(static_cast<C_Human *>(source)))
            continue;
        if (!vehicle->CanSeatWeaponAim(m_human->GetVehicleSeat(), &event->pos.x))
            continue;
        const float dx = event->pos.x - own.x;
        const float dy = event->pos.y - own.y;
        const float dz = event->pos.z - own.z;
        // (Fix: a stimulus with no time left divided by 0; age / timeLeft is 0 for the others
        // the test above lets through.)
        const int fraction = event->timeLeft != 0 ? event->age / event->timeLeft : 0;
        const float distance2 = (static_cast<float>(fraction) + 1.4f) * (dz * dz + dy * dy + dx * dx);
        if (distance2 <= best2) {
            best = source;
            best2 = distance2;
        }
    }
    return static_cast<C_Human *>(best);
}

C_Human *C_EnemyController::PickSeenTarget()
{
    // The enemy in view who scores most (a cannon: only those it has a free line of fire
    // to): facing the gunner 50, in the gun's reach 20, nearness (100 m: 0, here 25), noisy
    // 20, standing 10 or crouching 5.
    C_Actor *vehicle = m_human->GetVehicle();
    const int seat = m_human->GetVehicleSeat();
    C_Human *best = nullptr;
    float bestScore = -100000.0f;
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    for (size_t i = enemies.size(); i-- > 0;) {
        C_Human *enemy = enemies[i];
        if (enemy->GetHealth() < 1 || enemy->IsUnconscious() || enemy->IsSurrendered())
            continue;
        S_vector4 body = WorldPos4Of(enemy->GetBodyFrame());
        if ((m_enemyFlags & FLAG_CANNON) && !IsLineOfFireFree(body))
            continue;
        const float dx = body.x - own.x;
        const float dy = body.y - own.y;
        const float dz = body.z - own.z;
        const float square = dz * dz + dy * dy + dx * dx;
        float distance;
        if (square < 1e-8f) {
            distance = 0.0f;
        } else if (std::fabs(square - 1.0f) < 1e-8f) {
            distance = 1.0f;
        } else {
            distance = std::sqrt(square);
            if (distance > 100.0f)
                distance = 100.0f;
        }
        float score = 0.0f;
        if (enemy->GetController()->IsFacing(m_human))
            score = 50.0f;
        if (vehicle->CanSeatWeaponAim(seat, &body.x))
            score += 20.0f;
        score = (100.0f - distance) * 0.25f + score;
        if (enemy->GetController()->GetQuietTime() < 400)
            score += 20.0f;
        const uint32_t flags = enemy->GetController()->GetMoveFlags();
        if (!(flags & (MOVE_LYING | MOVE_CROUCHING)))
            score += 10.0f;
        else if (flags & MOVE_CROUCHING)
            score += 5.0f;
        if (bestScore < score) {
            bestScore = score;
            best = enemy;
        }
    }
    return best;
}

bool C_EnemyController::IsThreatToGunner(C_Human *human)
{
    // Within 8 m, or within 20 m out of the gun's reach.
    const float distance = GetDistanceTo(human);
    if (distance < 8.0f)
        return true;
    return distance < 20.0f && IsOutOfGunReach(human);
}

bool C_EnemyController::IsGunnerThreatened()
{
    // Only at a static gun. (The original also tests a function that returns true,
    // 0x0054d560.)
    if (!IsAtStaticWeapon())
        return false;
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    for (size_t i = enemies.size(); i-- > 0;) {
        if (IsThreatToGunner(enemies[i]))
            return true;
    }
    return false;
}

int C_EnemyController::GetGunnerDelay()
{
    // ms between bursts: 35..100 a metre to the nearest enemy (none in view: 0.5 s).
    if (!SeesEnemies())
        return 500;
    const float half = GetNearestEnemyDistance() * 0.5f;
    return static_cast<int>((Rand01() * 130.0f + 70.0f) * half);
}

S_vector C_EnemyController::GetGunAimDir(bool scatter)
{
    // From the gun to where the target will be (led by an eighth of a second): standing or
    // crouching its body 0.3 m lower, lying its body (its position when rand() gives 0);
    // `scatter`: off by up to 6..10 % of the distance (a cannon 4..8 %), less with the
    // gunner's shooting skill.
    C_Actor *vehicle = m_human->GetVehicle();
    const S_vector origin = vehicle->GetSeatWeaponOrigin(m_human->GetVehicleSeat());
    const C_Human *target = m_target;
    S_vector point;
    const uint32_t flags = target->GetController()->GetMoveFlags();
    if (flags & (MOVE_STANDING | MOVE_CROUCHING)) {
        const S_vector &body = target->GetBodyFrame()->GetWorldPos();
        point = S_vector(body.x, body.y - 0.3f, body.z);
    } else if (Rand01() == 0.0f) {
        point = target->GetFrame()->GetWorldPos();
    } else {
        point = target->GetBodyFrame()->GetWorldPos();
    }
    const S_vector &velocity = target->GetVelocity();
    point.x += velocity.x * 0.125f;
    point.y += velocity.y * 0.125f;
    point.z += velocity.z * 0.125f;
    if (scatter) {
        const float dx = point.x - origin.x;
        const float dy = point.y - origin.y;
        const float dz = point.z - origin.z;
        const float distance = MagnitudeBy(dz * dz + dy * dy + dx * dx);
        const float miss = 1.0f - static_cast<float>(m_human->GetStat(STAT_SHOOTING)) * 0.01f;
        const float r1 = Rand01();
        const float r2 = Rand01();
        const float r3 = Rand01();
        if (!(m_enemyFlags & FLAG_CANNON)) {
            const float k = (miss * 0.04f + 0.06f) * distance;
            point.x = k * ((r3 + r3) - 1.0f) + point.x;
            point.y = point.y + (r2 * 1.2f - 1.0f) * k;
            point.z = point.z + ((r1 + r1) - 1.0f) * k;
        } else {
            const float k = (miss * 0.04f + 0.04f) * distance;
            point.x = k * ((r3 + r3) - 1.0f) + point.x;
            point.y = point.y + (r2 - 0.3f) * k;
            point.z = point.z + k * ((r1 + r1) - 1.0f);
        }
    }
    return point - origin;
}

void C_EnemyController::LeaveGun()
{
    S_HumanCommand command;
    command.category = HUMAN_COMMAND_VEHICLE;
    command.type = HUMAN_COMMAND_LEAVE_VEHICLE;
    IssueCommand(command);
}

bool C_EnemyController::IsOutOfGunReach(C_Human *target)
{
    // The seat weapon cannot point at the target, nor half a metre above it.
    C_Actor *vehicle = m_human->GetVehicle();
    // Fix: the original read no vehicle (an event raised as the human left it).
    if (!vehicle)
        return true;
    S_vector4 point = GetLeadPoint(target);
    if (vehicle->CanSeatWeaponAim(m_human->GetVehicleSeat(), &point.x))
        return false;
    point.y += 0.5f;
    return !vehicle->CanSeatWeaponAim(m_human->GetVehicleSeat(), &point.x);
}

bool C_EnemyController::IsAtStaticWeapon()
{
    return m_human->GetVehicle() && m_human->GetVehicle()->GetType() == ACTOR_STATIC_WEAPON;
}

void C_EnemyController::AimGun()
{
    if (!m_target)
        return;
    S_vector dir = GetGunAimDir(false);
    NormalizeBy(dir, dir.y * dir.y + dir.z * dir.z + dir.x * dir.x);
    SetViewDir(Dir4Of(dir));
}

void C_EnemyController::AimAndFireGun()
{
    // Turns the gun to the target; pointing at it (10 degrees) while alert, fires (not at
    // a new target in the tick it turned to it), slightly off by the skill.
    if (!m_target)
        return;
    C_Actor *vehicle = m_human->GetVehicle();
    const int seat = m_human->GetVehicleSeat();
    S_vector dir = GetGunAimDir(false);
    S_vector aim = vehicle->GetSeatWeaponAim(seat);
    NormalizeBy(aim, aim.y * aim.y + aim.z * aim.z + aim.x * aim.x);
    NormalizeBy(dir, dir.z * dir.z + dir.y * dir.y + dir.x * dir.x);
    SetViewDir(Dir4Of(dir));
    if (m_alertTime <= 0)
        return;
    if (vehicle->GetSeatWeaponRounds(seat) == 0) {
        m_axes[AXIS_RELOAD] = 1.0f;
        return;
    }
    if (!(aim.z * dir.z + aim.y * dir.y + dir.x * aim.x > 0.985f))
        return;
    int unused = 0;
    bool inView = false;
    const int seen = g_sightModule.CanSee(m_human->GetSightId(), m_target->GetSightTargetId(), &unused, &inView);
    if (!MayFireAt(m_moveFlags, seen, inView))
        return;
    S_vector shot = GetGunAimDir(true);
    NormalizeBy(shot, shot.z * shot.z + shot.y * shot.y + shot.x * shot.x);
    SetViewDir(Dir4Of(shot));
    if (m_targetChanged) {
        m_targetChanged = false;
        return;
    }
    m_axes[AXIS_FIRE] = 1.0f;
    m_buttons[BUTTON_FIRE] = 1.0f;
}

void C_EnemyController::AimAndFireCannon()
{
    // As the gun, turned in the xz plane only to 1 degree.
    if (!m_target)
        return;
    C_Actor *vehicle = m_human->GetVehicle();
    const int seat = m_human->GetVehicleSeat();
    S_vector dir = GetGunAimDir(false);
    S_vector aim = vehicle->GetSeatWeaponAim(seat);
    NormalizeBy(aim, aim.y * aim.y + aim.z * aim.z + aim.x * aim.x);
    NormalizeBy(dir, dir.y * dir.y + dir.z * dir.z + dir.x * dir.x);
    SetViewDir(Dir4Of(dir));
    if (vehicle->GetSeatWeaponRounds(seat) == 0) {
        m_axes[AXIS_RELOAD] = 1.0f;
        return;
    }
    if (m_alertTime <= 0)
        return;
    S_vector flatDir(dir.x, 0.0f, dir.z);
    NormalizeBy(flatDir, dir.x * dir.x + dir.z * dir.z);
    S_vector flatAim(aim.x, 0.0f, aim.z);
    NormalizeBy(flatAim, aim.x * aim.x + aim.z * aim.z);
    if (!(flatAim.y * flatDir.y + flatAim.x * flatDir.x + flatAim.z * flatDir.z >= 0.9998f))
        return;
    int unused = 0;
    bool inView = false;
    const int seen = g_sightModule.CanSee(m_human->GetSightId(), m_target->GetSightTargetId(), &unused, &inView);
    if (!MayFireAt(m_moveFlags, seen, inView))
        return;
    S_vector shot = GetGunAimDir(true);
    NormalizeBy(shot, shot.y * shot.y + shot.z * shot.z + shot.x * shot.x);
    SetViewDir(Dir4Of(shot));
    m_targetChanged = false;
    m_axes[AXIS_FIRE] = 1.0f;
    m_buttons[BUTTON_FIRE] = 1.0f;
}

bool C_EnemyController::IsGunEmpty()
{
    C_Actor *vehicle = m_human->GetVehicle();
    return vehicle && vehicle->GetSeatWeaponRounds(m_human->GetVehicleSeat()) == 0;
}

void C_EnemyController::SweepGun()
{
    // Turns the gun toward the likeliest enemy: those in view and the sources of fresh
    // stimuli not in view, by how near they are and how little the gun has to turn.
    C_Actor *vehicle = m_human->GetVehicle();
    if (!vehicle)
        return;
    const int seat = m_human->GetVehicleSeat();
    S_vector aim = vehicle->GetSeatWeaponAim(seat);
    const S_vector origin = vehicle->GetSeatWeaponOrigin(seat);
    NormalizeBy(aim, aim.y * aim.y + aim.z * aim.z + aim.x * aim.x);
    float best = -1e8f;
    S_vector bestPos;
    auto consider = [&](const S_vector &pos) {
        S_vector d(pos.x - origin.x, pos.y - origin.y, pos.z - origin.z);
        const float square = d.z * d.z + d.y * d.y + d.x * d.x;
        const float distance = MagnitudeBy(square);
        NormalizeBy(d, square);
        const float score = (100.0f - distance) * 0.5f + (aim.z * d.z + aim.y * d.y + d.x * aim.x) * 5.0f;
        if (best < score) {
            best = score;
            bestPos = pos;
        }
    };
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    for (size_t i = enemies.size(); i-- > 0;)
        consider(enemies[i]->GetFrame()->GetWorldPos());
    std::deque<S_HumanEvent *> &stimuli = m_events->GetStimuli();
    for (size_t i = 0; i < stimuli.size(); ++i) {
        const S_HumanEvent *event = stimuli[i];
        if (event->age * 2 > event->timeLeft)
            continue;
        C_Actor *source = IdToActor(event->sourceId);
        if (!source || source->GetType() != ACTOR_HUMAN || static_cast<C_Human *>(source)->GetHealth() < 1
            || m_human->HasSeenEnemy(static_cast<C_Human *>(source)))
            continue;
        consider(XYZ(event->pos));
    }
    if (!(best > -100000.0f))
        return;
    S_vector dir = bestPos - origin;
    NormalizeBy(dir, dir.z * dir.z + dir.y * dir.y + dir.x * dir.x);
    SetViewDir(Dir4Of(dir));
}

bool C_EnemyController::IsLineOfFireFree(const S_vector4 &pos)
{
    // Nothing of an animation actor nor a car without an enemy aboard in the way. (The ray
    // starts at `pos` and goes on away from the gun, as in the original.)
    C_Actor *vehicle = m_human->GetVehicle();
    const S_vector origin = vehicle->GetSeatWeaponOrigin(m_human->GetVehicleSeat());
    const S_vector dir(pos.x - origin.x, pos.y - origin.y, pos.z - origin.z);
    m_human->m_body.RemoveCollision(false);
    S_CollisionVolume *hit = g_collisionWorld.RayTestDynamic(XYZ(pos), dir, nullptr, nullptr, 0, 0);
    m_human->m_body.RestoreCollision(false);
    if (hit && hit->actor) {
        if (hit->actor->GetType() == ACTOR_ANIMATION)
            return false;
        if (hit->actor->GetType() == ACTOR_CAR && !static_cast<C_Car *>(hit->actor)->HasEnemyAboard(m_human))
            return false;
    }
    return true;
}

// ------------------------------------------------------------------------- the post's guns

C_Actor *C_EnemyController::FindGunToUse(int /*timeDelta*/)
{
    // A static gun of the post with nobody alive at it that is of use; several: the nearest.
    // (The time set here for the next check is not read.)
    m_gunCheckTime = static_cast<int>((Rand01() + 1.0f) * 2000.0f);
    if (m_guns.empty())
        return nullptr;
    std::vector<C_Actor *> free;
    for (size_t i = m_guns.size(); i-- > 0;) {
        C_Actor *gun = m_guns[i];
        if ((!gun->GetSeatOccupant(0) || static_cast<C_Human *>(gun->GetSeatOccupant(0))->GetHealth() < 1)
            && !IsGunUseless(gun))
            free.push_back(gun);
    }
    if (free.empty())
        return nullptr;
    if (free.size() == 1)
        return free[0];
    return NearestGun(free);
}

bool C_EnemyController::IsGunUseless(C_Actor *gun)
{
    // Within 6 m of it: useless with an enemy within 12 m, or one within 30 m it cannot point
    // at. 6..10 m: of use when every enemy in view is 30 m away or more and it can point at
    // one. Farther: useless.
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    const S_vector origin = gun->GetSeatWeaponOrigin(0);
    const float dx = origin.x - own.x;
    const float dy = origin.y - own.y;
    const float dz = origin.z - own.z;
    const float distance2 = dz * dz + dx * dx + dy * dy;
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    if (distance2 < 36.0f) {
        if (GetNearestEnemyDistance() < 12.0f)
            return true;
        for (size_t i = enemies.size(); i-- > 0;) {
            if (!(GetDistanceTo(enemies[i]) < 30.0f))
                continue;
            S_vector4 point = GetLeadPoint(enemies[i]);
            if (!gun->CanSeatWeaponAim(0, &point.x))
                return true;
        }
        return false;
    }
    if (!(distance2 < 100.0f) || GetNearestEnemyDistance() < 30.0f)
        return true;
    for (size_t i = enemies.size(); i-- > 0;) {
        S_vector4 point = GetLeadPoint(enemies[i]);
        if (gun->CanSeatWeaponAim(0, &point.x))
            return false;
    }
    return true;
}

C_Actor *C_EnemyController::NearestGun(const std::vector<C_Actor *> &guns)
{
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    C_Actor *best = nullptr;
    float best2 = 10000.0f;
    for (size_t i = guns.size(); i-- > 0;) {
        const S_vector origin = guns[i]->GetSeatWeaponOrigin(0);
        const float dx = origin.x - own.x;
        const float dy = origin.y - own.y;
        const float dz = origin.z - own.z;
        const float distance2 = dz * dz + dy * dy + dx * dx;
        if (distance2 < best2) {
            best = guns[i];
            best2 = distance2;
        }
    }
    return best;
}
