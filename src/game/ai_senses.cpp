// C_AIController's senses (original source 0x0056fc80..0x00575820): the sight tests of the
// sight module, the choice of the enemy to fire at, and what the human perceived turned into
// events of its AI (C_HumanEvents) or a look toward a noise.
#include "game/human_controller.h"

#include <cmath>

#include "game/ai_parts.h"
#include "game/controller_internal.h"
#include "game/controller_messages.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

constexpr int EVENT_TIME = 10000;                       // ms
constexpr int HIT_STIMULUS_TIME = 50000;                // ms (the attacker of a dead human)

// The sight test: the meleeTrackers' range, the humans' half field of view (degrees; the squad's is
// wider and they see 1.3 times as far), the widest a side of the view goes (radians).
constexpr float MELEE_TRACKER_SIGHT_RANGE = 100.0f;
constexpr float MELEE_TRACKER_HALF_FOV = 1.0471975803375244f;    // 60 degrees
constexpr float HUMAN_HALF_FOV = 50.0f;
constexpr float SQUAD_HALF_FOV = 60.0f;
constexpr float SQUAD_SIGHT_FACTOR = 1.3f;
constexpr float MAX_VIEW_SIDE = 1.4f;
constexpr float DEG_TO_RAD = 0.017453292f;
constexpr float TWO_PI = 6.2831855f;
constexpr float PI = 3.1415927f;
// TestSight's status (not read by the callers).
constexpr int SIGHT_TESTED = 99;
constexpr int SIGHT_SKIPPED = 500;
// Move flags that skip the sight test: 0x800 (MOVE_DEAD) and MOVE_DEAF.
constexpr uint32_t MOVE_NO_SIGHT = MOVE_DEAD | MOVE_DEAF;

// Distance in the way the SDK computes a magnitude (exact 0 and 1).
float Distance(const S_vector &a, const S_vector &b)
{
    return (a - b).Magnitude();
}

// A new event of the AI, its age 0 and 10 s to live.
S_HumanEvent *NewEvent(uint32_t type, const C_Actor *actor, const C_Actor *source)
{
    S_HumanEvent *event = AllocHumanEvent();
    event->type = type;
    event->age = 0;
    event->timeLeft = EVENT_TIME;
    event->actorId = ActorToId(actor);
    event->sourceId = ActorToId(source);
    return event;
}

void SetEventPos(S_HumanEvent *event, const I3D_frame *frame)
{
    event->pos = reinterpret_cast<const S_vector4 &>(frame->GetMatrix().m[3]);
    event->unknown30 = 0;
}

void ClearEventPos(S_HumanEvent *event)
{
    event->unknown30 = 0;
    event->pos.x = event->pos.y = event->pos.z = 0.0f;
}

// The direction the human looks for a sight test, in the xz plane: the seat weapon's aim in a
// vehicle that has one, else the body frame's z axis.
void SightDirection(const C_Human *human, float &x, float &z)
{
    C_Actor *vehicle = human->GetVehicle();
    if (vehicle && vehicle->HasSeatWeapon(human->GetVehicleSeat())) {
        const S_vector aim = vehicle->GetSeatWeaponAim(human->GetVehicleSeat());
        x = aim.x;
        z = aim.z;
        return;
    }
    const S_vector axis = S_vector(human->GetBodyFrame()->GetMatrix()(2)).Normalized();
    x = axis.x;
    z = axis.z;
}

// Whether `target` (xz) is inside the triangle the view spans: from `eye` `range` ahead along
// `dirX / dirZ`, `left` and `right` radians to the sides (the tangents scale the sideways
// reach; the original's own arithmetic, one side at a time).
bool InViewTriangle(float eyeX, float eyeZ, float dirX, float dirZ, float range, float left, float right,
                    float targetX, float targetZ)
{
    // The view direction normalized with the SDK's rules (y stays 0, or becomes 1 for a zero
    // vector), and its perpendicular to the right.
    S_vector forward(dirX, 0.0f, dirZ);
    forward.Normalize();
    const float perpX = forward.y * 0.0f - forward.z * -1.0f;
    const float perpZ = forward.x * -1.0f - forward.y * 0.0f;
    const float tanRight = std::tan(right);
    const float tanLeft = std::tan(left);
    const float aheadX = forward.x * range + eyeX;
    const float aheadZ = eyeZ + forward.z * range;
    const float rightX = aheadX + perpX * (tanRight * range);
    const float rightZ = aheadZ + perpZ * (tanRight * range);
    const float leftX = aheadX + perpX * (tanLeft * range);
    const float leftZ = aheadZ + perpZ * (tanLeft * range);
    return (targetZ - eyeZ) * (rightX - eyeX) - (targetX - eyeX) * (rightZ - eyeZ) >= 0.0f &&
           (targetZ - rightZ) * (leftX - rightX) - (targetX - rightX) * (leftZ - rightZ) >= 0.0f &&
           (eyeX - leftX) * (targetZ - leftZ) - (targetX - leftX) * (eyeZ - leftZ) >= 0.0f;
}

// What an AI controller fires at (the original reads +0xD0 of any controller; the player's
// and the remote controller's have other members there).
C_Actor *FireTargetOf(const C_HumanController *controller)
{
    switch (controller->GetType()) {
    case HUMAN_CONTROLLER_SQUAD:
    case HUMAN_CONTROLLER_ENEMY:
    case HUMAN_CONTROLLER_AI:
    case HUMAN_CONTROLLER_NEUTRAL:
    case HUMAN_CONTROLLER_SNIPER:
        return static_cast<const C_AIController *>(controller)->GetFireTarget();
    default:
        return nullptr;
    }
}

bool IsTargetable(const C_Human *enemy)
{
    return enemy->GetStat(STAT_HEALTH) > 0 && !enemy->IsUnconscious() && !enemy->IsSurrendered();
}

// The part of a target's score that does not depend on the shooter's position: being looked
// at or fired at, noise, stance.
float TargetAwarenessScore(const C_Human *enemy, const C_Human *self, float dirX, float dirY, float dirZ)
{
    float score = 0.0f;
    const C_HumanController *controller = enemy->GetController();
    if (controller->GetType() == HUMAN_CONTROLLER_PLAYER) {
        const S_vector &view = enemy->GetViewDir();
        score += (-dirX * view.x + -dirY * view.y + -dirZ * view.z > 0.9f) ? 50.0f : 30.0f;
    } else if (FireTargetOf(controller) == self) {
        score += 50.0f;
    }
    return score;
}

}  // namespace

// ------------------------------------------------------------------------- sight tests

void C_AIController::TestSightOfMeleeTracker(C_Actor *meleeTracker, int *status, float *visibility)
{
    // Only the squad looks out for meleeTrackers (and not when it is an enemy, a civilian or a meleeTracker).
    if (m_moveFlags & MOVE_NO_SIGHT) {
        *status = SIGHT_SKIPPED;
        *visibility = 0.0f;
        return;
    }
    if (m_type != HUMAN_CONTROLLER_SQUAD) {
        *status = SIGHT_SKIPPED;
        *visibility = 0.0f;
        return;
    }
    const bool skip = m_human->GetType() == ACTOR_HUMAN
                          ? m_human->GetRole() == HUMAN_ROLE_ENEMY || m_human->GetRole() == HUMAN_ROLE_NEUTRAL
                          : m_human->GetType() == ACTOR_MELEE_TRACKER;
    if (skip) {
        *status = SIGHT_SKIPPED;
        *visibility = 0.0f;
        return;
    }
    float range = MELEE_TRACKER_SIGHT_RANGE;
    *status = SIGHT_TESTED;
    const float viewDistance = GetCameraModule()->GetViewDistance();
    if (viewDistance < MELEE_TRACKER_SIGHT_RANGE)
        range = viewDistance;
    const I3D_frame *meleeTrackerFrame = static_cast<C_MeleeTracker *>(meleeTracker)->GetBodyFrame();
    const I3D_frame *eye = m_human->GetBodyFrame();
    const S_vector offset = eye->GetWorldPos() - meleeTrackerFrame->GetWorldPos();
    if (range * range < offset.Square()) {
        *visibility = 0.0f;
        return;
    }
    float dirX, dirZ;
    SightDirection(m_human, dirX, dirZ);
    const S_vector &eyePos = m_human->GetBodyFrame()->GetWorldPos();
    const S_vector &target = meleeTracker->GetFrame()->GetWorldPos();
    *visibility = InViewTriangle(eyePos.x, eyePos.z, dirX, dirZ, range, -MELEE_TRACKER_HALF_FOV, MELEE_TRACKER_HALF_FOV, target.x, target.z)
                      ? 100.0f
                      : 0.0f;
}

void C_AIController::TestSightOfHuman(C_Human *target, int *status, float *visibility)
{
    if (m_moveFlags & MOVE_NO_SIGHT) {
        *status = SIGHT_SKIPPED;
        *visibility = 0.0f;
        return;
    }
    *status = SIGHT_TESTED;
    float range = target->GetVisibility();
    float halfFov;
    if (m_type == HUMAN_CONTROLLER_SQUAD) {
        range *= SQUAD_SIGHT_FACTOR;
        halfFov = SQUAD_HALF_FOV;
    } else {
        halfFov = HUMAN_HALF_FOV;
    }
    const float viewDistance = GetCameraModule()->GetViewDistance();
    if (viewDistance < range)
        range = viewDistance;
    if (m_type != HUMAN_CONTROLLER_SQUAD) {
        const float sightRange = static_cast<float>(m_human->m_stats[STAT_SIGHT_RANGE]);
        if (sightRange < range)
            range = sightRange;
    }
    if (!(Distance(m_human->GetBodyFrame()->GetWorldPos(), target->GetBodyFrame()->GetWorldPos()) <= range)) {
        *visibility = 0.0f;
        return;
    }
    float dirX, dirZ;
    SightDirection(m_human, dirX, dirZ);
    // How far the view is turned from the body: the field of view widens toward that side.
    float bodyAngle = std::atan2(dirX, dirZ);
    float viewAngle = std::atan2(m_viewDir.x, m_viewDir.z);
    if (bodyAngle >= 0.0f) {
        if (bodyAngle > TWO_PI)
            bodyAngle -= TWO_PI;
    } else {
        bodyAngle += TWO_PI;
    }
    if (viewAngle >= 0.0f) {
        if (viewAngle > TWO_PI)
            viewAngle -= TWO_PI;
    } else {
        viewAngle += TWO_PI;
    }
    float turn = bodyAngle - viewAngle;
    if (turn > PI)
        turn -= TWO_PI;
    const float half = halfFov * DEG_TO_RAD;
    float right, left;
    if (turn <= 0.0f) {
        right = half;
        left = turn - half;
        if (left < -MAX_VIEW_SIDE)
            left = -MAX_VIEW_SIDE;
    } else {
        right = half + turn;
        if (right > MAX_VIEW_SIDE)
            right = MAX_VIEW_SIDE;
        left = -halfFov * DEG_TO_RAD;
    }
    const S_vector &eyePos = m_human->GetBodyFrame()->GetWorldPos();
    const S_vector &targetPos = target->GetFrame()->GetWorldPos();
    *visibility = InViewTriangle(eyePos.x, eyePos.z, dirX, dirZ, range, left, right, targetPos.x, targetPos.z)
                      ? 100.0f
                      : 0.0f;
}

// ------------------------------------------------------------------------- targets

C_Human *C_AIController::PickTargetFromSeat()
{
    // A seat weapon: the enemies by distance, being looked at or fired at, in its arc, noise
    // and stance.
    C_Actor *vehicle = m_human->m_vehicle;
    if (!vehicle)
        return nullptr;
    const int seat = m_human->m_seat;
    C_ItemDef *weapon = vehicle->GetSeatWeaponDef(seat);
    if (!weapon)
        return nullptr;
    const bool projectileWeapon = weapon->GetComponentKind() == ICK_PROJECTILE_WEAPON;
    const I3D_frame *frame = m_human->GetFrame();
    C_Human *best = nullptr;
    float bestScore = -100000.0f;
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    for (size_t i = enemies.size(); i-- > 0;) {
        C_Human *enemy = enemies[i];
        if (!IsTargetable(enemy))
            continue;
        // The player's squad does not fire at the crews of cars with +0x5E0 set, but with
        // projectile weapons.
        if (!projectileWeapon && m_human->m_role == HUMAN_ROLE_PLAYER) {
            C_Actor *enemyVehicle = enemy->GetVehicle();
            if (enemyVehicle && enemyVehicle->GetType() == ACTOR_CAR && static_cast<C_Car *>(enemyVehicle)->GetUnknown5E0())
                continue;
        }
        const S_vector &enemyPos = enemy->GetFrame()->GetWorldPos();
        S_vector offset = enemyPos - frame->GetWorldPos();
        const float square = offset.Square();
        float distance;
        if (square < 1e-8f) {
            distance = 0.0f;
        } else if (std::fabs(square - 1.0f) >= 1e-8f) {
            distance = std::sqrt(square);
            if (distance > 100.0f)
                distance = 100.0f;
        } else {
            distance = 1.0f;
        }
        float score = 0.0f;
        const C_HumanController *controller = enemy->GetController();
        bool watched;
        if (controller->GetType() == HUMAN_CONTROLLER_PLAYER) {
            offset.Normalize();
            const S_vector &view = enemy->GetViewDir();
            watched = 0.99f < -offset.x * view.x + -offset.y * view.y + -offset.z * view.z;
        } else {
            watched = FireTargetOf(controller) == m_human;
        }
        if (watched)
            score = 50.0f;
        if (vehicle->CanSeatWeaponAim(seat, const_cast<float *>(&enemyPos.x)))
            score += 20.0f;
        score += (100.0f - distance) * 0.25f;
        if (controller->GetQuietTime() < 400)
            score += 20.0f;
        const uint32_t flags = controller->GetMoveFlags();
        if (!(flags & (MOVE_LYING | MOVE_CROUCHING)))
            score += 10.0f;
        else if (flags & MOVE_CROUCHING)
            score += 5.0f;
        if (score > bestScore) {
            best = enemy;
            bestScore = score;
        }
    }
    return best;
}

C_Human *C_AIController::PickTarget(const S_vector *dir)
{
    // The best of the enemies in view (within 25 degrees of `dir` when given): near, seeing or
    // firing at the human, well visible, noisy, upright, in front; one other friends fire at
    // counts less.
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    if (enemies.empty())
        return nullptr;
    C_Actor *vehicle = m_human->m_vehicle;
    if (vehicle && vehicle->HasSeatWeapon(m_human->m_seat))
        return PickTargetFromSeat();
    if (enemies.size() == 1) {
        C_Human *enemy = enemies[0];
        return !enemy->IsUnconscious() && !enemy->IsSurrendered() && enemy->GetStat(STAT_HEALTH) > 0 ? enemy : nullptr;
    }
    const I3D_frame *frame = m_human->GetFrame();
    C_Human *best = nullptr;
    float bestScore = -100000.0f;
    for (size_t i = enemies.size(); i-- > 0;) {
        C_Human *enemy = enemies[i];
        if (!IsTargetable(enemy))
            continue;
        S_vector offset = enemy->GetFrame()->GetWorldPos() - frame->GetWorldPos();
        const float square = offset.Square();
        float distance;
        if (square < 1e-8f) {
            distance = 0.0f;
        } else if (std::fabs(square - 1.0f) >= 1e-8f) {
            distance = std::sqrt(square);
            if (distance > 100.0f)
                distance = 100.0f;
        } else {
            distance = 1.0f;
        }
        offset.Normalize();
        if (dir && !(0.9f <= offset.Dot(*dir)))
            continue;
        const float viewDot = offset.Dot(GetViewDir());
        float score = 100.0f - distance;
        score += TargetAwarenessScore(enemy, m_human, offset.x, offset.y, offset.z);
        uint32_t parts = 0;
        g_sightModule.GetVisibleParts(m_human->GetSightId(), enemy->GetSightTargetId(), &parts);
        if (parts & SIGHT_PART_BODY)
            score += 30.0f;
        if (parts & SIGHT_PART_LEGS)
            score += 10.0f;
        if (parts & SIGHT_PART_HEAD)
            score += 10.0f;
        if (parts & SIGHT_PART_LEFT_ARM)
            score += 5.0f;
        if (parts & SIGHT_PART_RIGHT_ARM)
            score += 5.0f;
        const C_HumanController *controller = enemy->GetController();
        if (controller->GetQuietTime() < 400)
            score += 20.0f;
        const uint32_t flags = controller->GetMoveFlags();
        if (!(flags & (MOVE_LYING | MOVE_CROUCHING)))
            score += 10.0f;
        else if (flags & MOVE_CROUCHING)
            score += 5.0f;
        if (viewDot > 0.95f)
            score += 5.0f;
        // Spread the fire over the enemies.
        const std::vector<C_Human *> &friends = m_human->m_seenFriends;
        for (size_t j = friends.size(); j-- > 0;) {
            C_Human *other = friends[j];
            if (other != m_human && other->GetController()->GetLastTarget() == enemy)
                score -= 20.0f;
        }
        if (score > bestScore) {
            bestScore = score;
            best = enemy;
        }
    }
    return best;
}

float C_AIController::GetDistanceTo(const C_Actor *actor) const
{
    if (!actor)
        return 65535.0f;
    return Distance(actor->GetFrame()->GetWorldPos(), m_human->GetFrame()->GetWorldPos());
}

float C_AIController::GetNearestEnemyDistance() const
{
    float nearest = 65535.0f;
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    for (size_t i = enemies.size(); i-- > 0;) {
        const float distance = Distance(enemies[i]->GetFrame()->GetWorldPos(), m_human->GetFrame()->GetWorldPos());
        if (distance < nearest)
            nearest = distance;
    }
    return nearest;
}

S_vector4 C_AIController::GetAimDirTo(const C_Actor *target) const
{
    // To the point a shooter aims at on a human, else to the actor's frame, from the eyes.
    const S_vector &eye = m_human->GetBodyFrame()->GetWorldPos();
    S_vector dir;
    if (target->GetType() == ACTOR_HUMAN)
        dir = static_cast<const C_Human *>(target)->GetAimPoint(m_human) - eye;
    else
        dir = target->GetFrame()->GetWorldPos() - eye;
    dir.Normalize();
    return ToVector4(dir, 0.0f);                        // (w: a stale local in the original)
}

S_vector4 C_AIController::GetDirToFrame(const I3D_frame *frame) const
{
    S_vector dir = frame->GetWorldPos() - m_human->GetBodyFrame()->GetWorldPos();
    dir.Normalize();
    return ToVector4(dir, 0.0f);
}

// ------------------------------------------------------------------------- perception

void C_AIController::OnKilledEnemy(C_Human *victim)
{
    S_HumanEvent *event = NewEvent(AI_EVENT_KILLED_ENEMY, nullptr, victim);
    SetEventPos(event, victim->GetFrame());
    m_events->Raise(event);
}

void C_AIController::OnEnemySeen(C_Human *human)
{
    S_HumanEvent *event = NewEvent(AI_EVENT_ENEMY_SEEN, nullptr, human);
    SetEventPos(event, human->GetFrame());
    m_events->Raise(event);
}

void C_AIController::OnEnemyLost(C_Human *human)
{
    S_HumanEvent *event = NewEvent(AI_EVENT_ENEMY_LOST, nullptr, human);
    SetEventPos(event, human->GetFrame());
    m_events->Raise(event);
}

void C_AIController::OnBodySeen(C_Human *human)
{
    S_HumanEvent *event = NewEvent(AI_EVENT_BODY_SEEN, human, nullptr);
    ClearEventPos(event);
    m_events->Raise(event);
}

void C_AIController::OnBodyHeard(C_Actor *source)
{
    S_HumanEvent *event = NewEvent(AI_EVENT_BODY_HEARD, source, nullptr);
    ClearEventPos(event);
    m_events->Raise(event);
}

void C_AIController::OnFriendHeard(C_Actor *source)
{
    // A friend calls: what that friend fires at, if anything. (The source is a human.)
    if (!source)
        return;
    C_Actor *target = static_cast<C_Human *>(source)->GetController()->GetLastTarget();
    S_HumanEvent *event;
    if (target) {
        event = NewEvent(AI_EVENT_FRIEND_HEARD, source, target);
        SetEventPos(event, target->GetFrame());
    } else {
        event = NewEvent(AI_EVENT_FRIEND_HEARD, source, nullptr);
        ClearEventPos(event);
    }
    m_events->Raise(event);
}

void C_AIController::OnLoudSound(C_Actor *source)
{
    if (!source || m_human->IsFriendOf(source))
        return;
    S_HumanEvent *event = NewEvent(AI_EVENT_LOUD_SOUND, nullptr, source);
    SetEventPos(event, source->GetFrame());
    m_events->Raise(event);
}

void C_AIController::OnShotAt(C_Actor *shooter, const S_vector & /*velocity*/)
{
    S_HumanEvent *event = NewEvent(AI_EVENT_SHOT_AT, nullptr, shooter);
    SetEventPos(event, shooter->GetFrame());
    m_events->Raise(event);
}

void C_AIController::OnSoundHeard(S_RememberedSound *sound)
{
    // By the sound's chapter; enemies, controller type 8 and the squad do not turn to it.
    const auto raise = [&](uint32_t type) {
        S_HumanEvent *event = NewEvent(type, nullptr, sound->source);
        event->pos = {sound->pos.x, sound->pos.y, sound->pos.z, sound->posW};
        event->unknown30 = 0;
        m_events->Raise(event);
    };
    const bool turns = m_type != HUMAN_CONTROLLER_ENEMY && m_type != HUMAN_CONTROLLER_SNIPER && m_type != HUMAN_CONTROLLER_SQUAD;
    switch (sound->chapter) {
    case 0:
        raise(AI_EVENT_SOUND_0);
        if (turns)
            TurnToSound2(sound);
        break;
    case 1:
    case 3:
        raise(AI_EVENT_SOUND_1);
        break;
    case 2:
        raise(AI_EVENT_SOUND_2);
        if (turns)
            TurnToSound(sound);
        break;
    case 6:
        raise(AI_EVENT_SOUND_6);
        break;
    case 8:
    case 9:
        if (sound->source && !m_human->IsFriendOf(sound->source)) {
            const S_vector4 pos = {sound->pos.x, sound->pos.y, sound->pos.z, sound->posW};
            S_HumanEvent *event = AllocHumanEvent();
            event->Init(nullptr, AI_EVENT_SOUND_8, sound->source, pos, 0);
            m_events->Raise(event);
        }
        break;
    default:
        break;
    }
}

void C_AIController::TurnToSound(const S_RememberedSound *sound)
{
    // Looks to the side of a noise in front (not of an enemy it saw), unless it already looks.
    TurnToward(sound, false, 2000);
}

void C_AIController::TurnToSound2(const S_RememberedSound *sound)
{
    TurnToward(sound, true, 1000);
}

void C_AIController::TurnToward(const S_RememberedSound *sound, bool unlessLooking, int pause)
{
    // (0x00573900 / 0x00573c50, the same code: the second one only while not looking around.)
    if (unlessLooking && m_lookAroundTime != 0)
        return;
    C_Actor *source = sound->source;
    if (source->GetType() == ACTOR_HUMAN && m_human->HasSeenEnemy(static_cast<C_Human *>(source)))
        return;
    S_vector toSound = sound->pos - m_human->GetBodyFrame()->GetWorldPos();
    toSound.Normalize();
    const S_matrix &m = m_human->GetFrame()->GetMatrix();
    const S_vector forward = S_vector(m(2)).Normalized();
    if (forward.Dot(toSound) < -0.4f)
        return;
    const float side = toSound.x * m.m[0][0] + toSound.y * m.m[0][1] + toSound.z * m.m[0][2];
    // To the right (5) or the left (6); straight ahead nothing.
    if (!(side <= 0.0f) || side < 0.0f) {
        LookAround(!(side <= 0.0f) ? 5 : 6);
        m_lookAroundTime = 1500;
    }
    m_unknown110 = pause;
}

void C_AIController::OnHit(int type, int zone, int health, C_Actor *attacker, const S_vector &pos, const S_vector &dir)
{
    // The clients hear of the hit (the position relative to the human).
    if (!IsLocalGame()) {
        S_MsgHit *message = new S_MsgHit;
        message->hitType = type;
        message->zone = zone;
        message->health = health;
        message->attacker = attacker ? attacker->GetNetId() : -1;
        const S_vector offset = pos - m_human->GetPosition();
        message->pos = ToVector4(offset, 0.0f);
        message->velocity = ToVector4(dir, 0.0f);
        SendToClients(message);
    }
    bool seatWeapon = false;
    if (C_Actor *vehicle = m_human->m_vehicle) {
        const int seat = m_human->m_seat;
        if ((vehicle->GetSeatFlags(seat) & 0x10) || vehicle->HasSeatWeapon(seat))
            seatWeapon = true;
    }
    if (m_type == HUMAN_CONTROLLER_ENEMY || m_type == HUMAN_CONTROLLER_AI || m_type == HUMAN_CONTROLLER_NEUTRAL ||
        m_type == HUMAN_CONTROLLER_SNIPER) {
        // A hit on zone 11 knocks off the headgear, one in 20 on zone 3 the weapon in the hands
        // (a little upward, 2..3 m/s along the shot).
        const auto knockAway = [&](int uid) {
            S_vector velocity = dir.Normalized();
            const float speed = RandFloat() + 2.0f;
            velocity.x *= speed;
            velocity.z *= speed;
            velocity.y = velocity.y * speed + 1.0f;
            m_human->DropItem(uid, velocity);
        };
        if (zone == 0xB) {
            // (Fix: the original calls through a missing head container.)
            C_ItemContainer *head = m_human->GetContainer(ITEM_CONTAINER_HEAD);
            if (C_Item *headgear = head ? head->GetItem() : nullptr)
                knockAway(headgear->GetUid());
        }
        if (!seatWeapon && m_type != HUMAN_CONTROLLER_AI && zone == 3 && std::rand() % 20 == 0) {
            UpdateHandsItem();
            if (m_handsItem) {
                const int carryMode = m_handsItem->GetDef()->GetCarryMode();
                if (carryMode == ITEM_CARRY_SMG || carryMode == ITEM_CARRY_RIFLE || carryMode == ITEM_CARRY_LMG) {
                    knockAway(m_handsItem->GetUid());
                    m_handsItem = nullptr;
                }
            }
        }
    }
    if (!attacker)
        return;
    if (m_human->GetStat(STAT_HEALTH) < 1) {
        // Dead: the attacker is remembered.
        S_HumanEvent *event = NewEvent(AI_EVENT_HIT, attacker, attacker);
        SetEventPos(event, attacker->GetFrame());
        m_events->AddStimulus(HIT_STIMULUS_TIME, event, false);
        return;
    }
    if (type == 5) {
        S_HumanEvent *event = NewEvent(AI_EVENT_HIT_5, nullptr, attacker);
        SetEventPos(event, attacker->GetFrame());
        m_events->Raise(event);
    }
}

S_HumanEvent *S_HumanEvent::Init(const C_Actor *actor, uint32_t eventType, const C_Actor *source, const S_vector4 &position,
                                 int value30)
{
    type = eventType;
    age = 0;
    timeLeft = EVENT_TIME;
    actorId = ActorToId(actor);
    sourceId = ActorToId(source);
    pos = position;
    unknown30 = value30;
    return this;
}

float C_AIController::GetTargetDistance2() const
{
    return Distance(m_fire.target->GetFrame()->GetWorldPos(), m_human->GetFrame()->GetWorldPos());
}

float C_AIController::GetViewDotTo(const C_Actor *target) const
{
    const S_vector4 dir = GetAimDirTo(target);
    return dir.x * m_facingDir.x + dir.y * m_facingDir.y + dir.z * m_facingDir.z;
}

void C_AIController::GetWeaponPos(S_vector4 *pos) const
{
    if (!m_handsItem) {
        pos->x = 0.0f;
        pos->y = 0.0f;
        pos->z = 0.0f;
        return;
    }
    I3D_frame *frame = m_handsItem->GetFrame(0);
    const E_ItemComponentKind kind = m_handsItem->GetDef()->GetComponentKind();
    if (kind == ICK_FIREARM || kind == ICK_PROJECTILE_WEAPON)
        frame = frame->FindChildFrame("shdum", FRAME_MASK_DUMMY);
    const S_matrix &m = frame->GetMatrix();
    pos->x = m.m[3][0];
    pos->y = m.m[3][1];
    pos->z = m.m[3][2];
    pos->w = m.m[3][3];
}
