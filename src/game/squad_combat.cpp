// C_SquadController's fighting (game/human_controller.h; original source 0x00525260..0x00525d90,
// 0x005276e0..0x00528950, 0x0052a5d0..0x0052d460, 0x0052e7c0, 0x0052f160..0x00530d40,
// 0x005318c0, 0x005325c0..0x00532ba0, 0x00536a30, 0x00537b60..0x00538980, 0x00538a60..0x00538b00):
// what a squad member fires at and with which weapon, the bursts, stopping to fire, cover,
// the vehicles' guns, the hits it takes, the events and the stimuli it reacts to.
#include "game/human_controller.h"

#include <cmath>
#include <cstdlib>
#include <deque>
#include <vector>

#include "game/actor.h"
#include "game/actors_module.h"
#include "game/air_attack.h"
#include "game/ai_parts.h"
#include "game/checkpoints.h"
#include "game/collision.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/soldier.h"
#include "game/squad_commander.h"
#include "game/squad_internal.h"
#include "game/squad_order.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// The cosine of 80 degrees: the squad fires within 10 degrees of the horizontal.
constexpr float LEVEL_FIRE_SIN = 0.173648f;
// The bursts and pauses by the weapon's carry mode (1..4; 0x0083c160): the burst far / near,
// the squared distances of near and far, the pause near / far, their squared distances.
struct S_BurstTable {
    int burstFar, burstNear, burstNear2, burstFar2;
    int pauseNear, pauseFar, pauseNear2, pauseFar2;
};
constexpr S_BurstTable BURST_TABLES[4] = {
    {95, 380, 25, 2500, 480, 1000, 1, 2500},
    {50, 380, 25, 625, 480, 1000, 1, 900},
    {93, 380, 25, 4900, 480, 2000, 100, 10000},
    {380, 580, 100, 10000, 1200, 2000, 1, 10000},
};
// The weapon preferences (BuildWeaponPrefs / PickWeapon bits).
enum : uint32_t {
    PREF_SIDEARM = 0x001,                               // behaviour 2
    PREF_SMG = 0x002,                                     // behaviour 3
    PREF_RIFLE = 0x004,                                     // behaviour 12, then 4
    PREF_LMG = 0x008,                                     // behaviour 5
    PREF_LAUNCHER = 0x010,                              // behaviour 6
    PREF_GRENADE = 0x020,                               // behaviour 9
    PREF_RIFLE_SCOPED = 0x040,
    PREF_RIFLE_SILENCED = 0x080,
    PREF_SMG_SILENCED = 0x100,
    PREF_SIDEARM_SILENCED = 0x200,
    PREF_ALL = 0xFFF,
};
// The compass of the cover masks (0x008aad90, made by 0x00525160): bit i of a mask is
// compass[i], 5 m out (N, NE, E, SE, S, SW, W, NW).
const S_vector4 g_compass[8] = {
    {0.0f, 0.0f, 5.0f, 0.0f},  {5.0f, 0.0f, 5.0f, 0.0f},   {5.0f, 0.0f, 0.0f, 0.0f},  {5.0f, 0.0f, -5.0f, 0.0f},
    {0.0f, 0.0f, -5.0f, 0.0f}, {-5.0f, 0.0f, -5.0f, 0.0f}, {-5.0f, 0.0f, 0.0f, 0.0f}, {-5.0f, 0.0f, 5.0f, 0.0f},
};

// A random number in [-1, 1] (rand() / 32767 twice, less one).
float RandSigned()
{
    float r = static_cast<float>(std::rand()) * (1.0f / 32767.0f);
    return r + r - 1.0f;
}

// The direction of the compass bits in `mask`, opposite the middle one (0x00525700
// __fastcall; false for an empty mask). (The original also measures the way from the
// selected member to `pos` and sorts out nothing with it, and draws a random number.)
bool PickCoverDir(uint32_t mask, S_vector4 &dir, const S_vector4 &pos)
{
    if (mask == 0)
        return false;
    std::vector<const S_vector4 *> dirs;
    for (int i = 0; i < 8; ++i) {
        if (mask & (1u << i))
            dirs.push_back(&g_compass[i]);
    }
    (void)pos;
    std::rand();
    const S_vector4 &pick = *dirs[dirs.size() / 2];
    S_vector d(pick.x * -1.0f, pick.y * -1.0f, pick.z * -1.0f);
    d.Normalize();
    dir = {d.x, d.y, d.z, pick.w};
    return true;
}

}  // namespace

bool IsArmoured(const C_Actor *target)
{
    if (!target)
        return false;
    return (target->GetType() == ACTOR_CAR && static_cast<const C_Car *>(target)->GetUnknown5E0() != 0)
           || (target->GetType() == ACTOR_ANIMATION && static_cast<const C_AnimationActor *>(target)->GetArmour() == ANIMATION_ARMOUR_LIGHT);
}

bool IsActorDead(const C_Actor *actor)
{
    switch (actor->GetType()) {
    case ACTOR_HUMAN:
        return static_cast<const C_Human *>(actor)->GetHealth() < 1;
    case ACTOR_CAR:
        return static_cast<const C_Car *>(actor)->IsDestroyed();
    case ACTOR_ANIMATION:
        return static_cast<const C_AnimationActor *>(actor)->IsStopped();
    case ACTOR_MELEE_TRACKER:
        return !static_cast<const C_MeleeTracker *>(actor)->IsAlive();
    default:
        return false;
    }
}

bool CanSeeVehicle(C_Actor *actor, const C_Human *human)
{
    if (actor->GetType() != ACTOR_CAR && actor->GetType() != ACTOR_ANIMATION)
        return false;
    const S_vector &body = human->GetBodyFrame()->GetWorldPos();
    const S_vector from = actor->GetFrame()->GetBound().center;
    const S_vector dir = body - from;
    if (!(dir.Magnitude() <= 300.0f))
        return false;
    S_CollisionVolume *hit = g_collisionWorld.RayTest(from, dir, nullptr, nullptr, 0, 0);
    if (!hit)
        return true;
    // In the way only the actor itself or the human's vehicle.
    return (hit->flags & 0x40) && hit->actor && (hit->actor == actor || hit->actor == human->GetVehicle());
}

S_vector4 FindCoverPoint(const S_vector4 &pos, const S_vector4 &dir, float radius, uint32_t seed)
{
    S_vector d = XYZ(dir);
    d.Normalize();
    // The cover points within `radius` that cover a stance (of 1 or 2) against `dir`.
    C_Checkpoints *network = GetGame()->GetCheckpoints();
    std::vector<uint16_t> points;
    std::vector<const S_Checkpoint *> covers;
    if (network->FindCoverWithin(XYZ(pos), radius, &points)) {
        for (uint16_t index : points) {
            const S_Checkpoint *point = network->Get(index);
            if (point->GetCoverStances(S_Checkpoint::GetDirectionBit(d), 3))
                covers.push_back(point);
        }
    }
    if (covers.empty())
        return pos;
    // One of them (by the seed), the offset turned by the seed within its radius.
    const S_Checkpoint *cover = covers[seed % covers.size()];
    S_vector offset = d.RotateByMatrix(RotateYMatrix(static_cast<float>(static_cast<int>(seed))));
    offset.Normalize();
    offset *= static_cast<float>(cover->radius) * 0.0625f;
    // (w: a stale local in the original.)
    return {cover->pos.x + offset.x, offset.y + cover->pos.y, offset.z + cover->pos.z, pos.w};
}

// ------------------------------------------------------------------------- cover

void C_SquadController::ChooseCoverFacing(S_vector4 *dir, uint32_t *stance)
{
    // Away from the cover the walking point gives standing (else crouching).
    S_vector4 facing = Dir4(GetViewDir());
    const S_vector4 &pos = m_human->GetPosition4();
    bool standing = false;
    if (m_currentCheckpoint) {
        if (PickCoverDir(m_currentCheckpoint->GetCoverMask(MOVE_STANDING), facing, pos))
            standing = true;
        else
            PickCoverDir(m_currentCheckpoint->GetCoverMask(MOVE_CROUCHING), facing, pos);
    }
    *stance = standing ? MOVE_STANDING : MOVE_CROUCHING;
    *dir = facing;
    // Following a leader that lies quiet: lying.
    C_Human *leader = m_human->GetLeader();
    if (m_following && leader && (leader->GetStanceFlags() & MOVE_LYING) && m_quietTime > 5000)
        *stance = MOVE_LYING;
}

void C_SquadController::CoverStance(bool firing)
{
    if (!m_axes || !m_buttons)
        return;
    UpdateHandsItem();
    if (!m_currentCheckpoint || !m_handsItem)
        return;
    const uint8_t bit = S_Checkpoint::GetDirectionBit(GetViewDir());
    const S_Checkpoint *point = m_currentCheckpoint;
    const bool lyingCover = (bit & point->cover1) != 0;
    uint8_t cover = lyingCover ? 1 : 0;
    if (point->cover2 & bit)
        cover = 2;
    if (!firing) {
        // Down behind the cover.
        if (point->cover4 & bit)
            cover |= 4;
        if ((cover & 6) && !(m_moveFlags & MOVE_CROUCHING)) {
            SetStance(MOVE_CROUCHING);
            return;
        }
        if (lyingCover && !(m_moveFlags & MOVE_LYING))
            SetStance(MOVE_LYING);
    } else {
        // Up over the cover to fire.
        if ((cover & 2) && !(m_moveFlags & MOVE_STANDING))
            m_orderStance = MOVE_STANDING;
        if (lyingCover && !(m_moveFlags & MOVE_CROUCHING))
            SetStance(MOVE_CROUCHING);
    }
}

// ------------------------------------------------------------------------- events and stimuli

S_HumanEvent *C_SquadController::HandleEvent(S_HumanEvent *event)
{
    switch (event->type) {
    case AI_EVENT_ENEMY_SEEN:
        m_alertTime = 16000;
        // (The source is looked up and not used.)
        if (IdToActor(event->sourceId))
            IdToActor(event->sourceId);
        return event;
    case AI_EVENT_BODY_SEEN:
    case AI_EVENT_BODY_HEARD:
        m_alertTime = 10000;
        return event;
    case AI_EVENT_ENEMY_LOST:
        m_alertTime = 10000;
        m_events->AddStimulus(3000, event, false);
        return nullptr;
    case AI_EVENT_KILLED_ENEMY:
        m_alertTime = 5000;
        event->actorId = -1;
        event->sourceId = -1;
        m_events->AddStimulus(2000, event, false);
        return nullptr;
    case AI_EVENT_SHOT_AT:
        if (m_combatMode == 0)
            SetCombatOrder(SQUAD_ORDER_MODE1);
        m_alertTime = 15000;
        m_events->AddStimulus(1000, event, false);
        return nullptr;
    case AI_EVENT_SOUND_0:
    case AI_EVENT_SOUND_2: {
        if (event->type == AI_EVENT_SOUND_0) {
            m_alertTime = 5000;
        } else {
            // An enemy aiming at a member within 30 m: the squad fights.
            C_Actor *source = IdToActor(event->sourceId);
            if (source && source->GetType() == ACTOR_HUMAN) {
                C_Human *enemy = static_cast<C_Human *>(source);
                if (enemy->GetHealth() > 0 && enemy->GetController()) {
                    if (C_Actor *target = enemy->GetController()->GetLastTarget()) {
                        const S_vector d = target->GetFrame()->GetWorldPos() - m_human->GetFrame()->GetWorldPos();
                        if (d.x * d.x + d.y * d.y + d.z * d.z < 900.0f
                            && g_teamManager.GetPlayerTeam()->IsMember(static_cast<C_Human *>(target)) && m_combatMode == 0)
                            SetCombatOrder(SQUAD_ORDER_MODE1);
                    }
                }
            }
            m_alertTime = 10000;
        }
        // Tracked while its source is not seen.
        C_Actor *source = IdToActor(event->sourceId);
        if (source && !m_human->HasSeenEnemy(static_cast<C_Human *>(source))) {
            m_events->AddStimulus(10000, event, false);
            return nullptr;
        }
        return event;
    }
    case AI_EVENT_LOUD_SOUND: {
        if (m_combatMode == 0)
            SetCombatOrder(SQUAD_ORDER_MODE1);
        C_Actor *source = IdToActor(event->sourceId);
        if (source && !m_human->HasSeenEnemy(static_cast<C_Human *>(source))) {
            m_events->AddStimulus(10000, event, false);
            m_alertTime = 15000;
            return nullptr;
        }
        return event;
    }
    case AI_EVENT_HIT_5: {
        // Hit by an enemy: the holding members fight.
        C_Actor *source = IdToActor(event->sourceId);
        if (source && source->GetType() == ACTOR_HUMAN && static_cast<C_Human *>(source)->IsHostileTo(m_human))
            g_teamManager.GetPlayerTeam()->ReleaseHoldingMembers(m_human);
        if (m_combatMode == 0 || m_combatMode == 2)
            SetCombatOrder(SQUAD_ORDER_MODE1);
        SayReply(SENTENCE_ENEMY_HIT, true, 2000, false);
        m_alertTime = 15000;
        m_events->AddStimulus(6000, event, false);
        return nullptr;
    }
    default:
        return event;
    }
}

bool C_SquadController::FindStimuli(int type, std::vector<int> &indexes, bool filter)
{
    // The tracked stimuli of `type`, the last first; `filter`: not those caused by the crew of
    // an armoured car.
    indexes.clear();
    std::deque<S_HumanEvent *> &stimuli = m_human->m_events->GetStimuli();
    for (size_t i = stimuli.size(); i-- > 0;) {
        const S_HumanEvent *event = stimuli[i];
        if (static_cast<int>(event->type) != type)
            continue;
        if (filter && event->sourceId >= 0) {
            C_Actor *source = g_actorsModule.GetActor(event->sourceId);
            if (source && source->GetType() == ACTOR_HUMAN) {
                C_Actor *vehicle = static_cast<C_Human *>(source)->GetVehicle();
                if (vehicle && vehicle->GetType() == ACTOR_CAR && static_cast<C_Car *>(vehicle)->GetUnknown5E0())
                    continue;
            }
        }
        indexes.push_back(static_cast<int>(i));
    }
    return !indexes.empty();
}

bool C_SquadController::ReactToStimuli()
{
    // Not while checking an event already, nor zombie-like with orders.
    if (m_eventTime != 0 || (GetOrderCount() != 0 && (m_moveFlags & MOVE_AI_ZOMBIE)))
        return false;
    I3D_frame *lookAt = nullptr;
    // With a launcher even the armoured cars' crews count.
    bool launcher = HasProjectileSeatWeapon();
    if (!launcher) {
        int container;
        launcher = FindWeapon(ITEM_BEHAVIOUR_LAUNCHER, false, false, &container) != nullptr;
    }
    std::deque<S_HumanEvent *> &stimuli = m_human->m_events->GetStimuli();
    std::vector<int> found;
    bool looking = false;
    if (!stimuli.empty()) {
        // The strongest kind tracked.
        const bool filter = !launcher;
        if (!FindStimuli(AI_EVENT_HIT_5, found, filter) && !FindStimuli(AI_EVENT_LOUD_SOUND, found, filter)
            && !FindStimuli(AI_EVENT_SHOT_AT, found, filter) && !FindStimuli(AI_EVENT_SOUND_0, found, filter))
            FindStimuli(AI_EVENT_SOUND_2, found, filter);
        // Sorted by their sources' distance, the farthest first.
        const S_vector4 &own = m_human->GetPosition4();
        for (size_t i = found.size(); i-- > 0;) {
            C_Actor *source = IdToActor(stimuli[found[i]]->sourceId);
            if (!source)
                continue;
            const S_vector4 &a = source->GetPosition4();
            float best = (own.z - a.z) * (own.z - a.z) + (own.y - a.y) * (own.y - a.y) + (own.x - a.x) * (own.x - a.x);
            if (i == 0)
                break;
            for (size_t j = i; j-- > 0;) {
                C_Actor *other = IdToActor(stimuli[found[j]]->sourceId);
                if (!other)
                    continue;
                const S_vector4 &b = other->GetPosition4();
                const float d = (own.z - b.z) * (own.z - b.z) + (own.y - b.y) * (own.y - b.y) + (own.x - b.x) * (own.x - b.x);
                if (d < best) {
                    std::swap(found[j], found[i]);
                    best = d;
                }
            }
        }
        // The first source still in the fight is looked at (the stimulus goes; an enemy killed
        // is not looked for).
        for (size_t k = 0; k < found.size(); ++k) {
            const int index = found[k];
            C_Human *source = static_cast<C_Human *>(IdToActor(stimuli[index]->sourceId));
            if (!source || source->IsUnconscious() || source->IsSurrendered())
                continue;
            if (stimuli[index]->type == AI_EVENT_KILLED_ENEMY)
                continue;
            ReleaseHumanEvent(stimuli[index]);
            stimuli.erase(stimuli.begin() + index);
            lookAt = source->GetBodyFrame();
            looking = lookAt != nullptr;
            break;
        }
    }
    if (!looking) {
        // An enemy a member near sees and this one does not, else one this one sees.
        C_Human *enemy = g_teamManager.GetPlayerTeam()->GetEnemyUnseenBy(m_human, !launcher);
        if (!enemy || !(lookAt = enemy->GetBodyFrame())) {
            std::vector<C_Human *> candidates;
            const std::vector<C_Human *> &seen = m_human->m_seenEnemies;
            for (size_t i = seen.size(); i-- > 0;) {
                C_Actor *vehicle = seen[i]->GetVehicle();
                if (launcher || !vehicle || vehicle->GetType() != ACTOR_CAR || !static_cast<C_Car *>(vehicle)->GetUnknown5E0())
                    candidates.push_back(seen[i]);
            }
            if (!candidates.empty())
                lookAt = candidates[static_cast<uint32_t>(std::rand()) % candidates.size()]->GetBodyFrame();
            if (!lookAt)
                return false;
        }
    }
    if ((m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && m_alert->Is1()) {
        // Walking: only the head turns.
        m_facing->SetLookAt(lookAt);
        m_eventTime = static_cast<int>(RandFloat() * 1000.0f + 2500.0f);
        GetGame()->Print("%s : Checking event by head for 3 s.", m_human->GetLastName());
        return true;
    }
    const S_vector4 &own = m_human->GetPosition4();
    const S_vector &to = lookAt->GetWorldPos();
    S_vector dir(to.x - own.x, 0.0f, to.z - own.z);
    dir.Normalize();
    const S_vector4 dir4 = {dir.x, dir.y, dir.z, 0.0f};   // (w: a stale local in the original)
    m_facing->SetFacing(dir4);
    m_facing->SetView(dir4);
    GetGame()->Print("%s : Checking event and turning body for 3.5 s.", m_human->GetLastName());
    m_eventTime = static_cast<int>(RandFloat() * 1000.0f + 3000.0f);
    return true;
}

// ------------------------------------------------------------------------- hits

void C_SquadController::TakeHit(int type, int zone, int health, C_Actor *attacker, const S_vector &pos,
                                const S_vector &dir, float force)
{
    if (attacker) {
        C_AIController::OnHit(type, zone, health, attacker, pos, dir);
        // Fix: the original reads a human's role of any attacker.
        if (attacker->GetType() == ACTOR_HUMAN) {
            C_Human *human = static_cast<C_Human *>(attacker);
            // Shot too often by the local player: the squad turns on the player.
            if (human->m_role == HUMAN_ROLE_PLAYER && type == HIT_BULLET && human->IsLocalPlayer()) {
                m_friendlyFire += force;
                if (m_friendlyFire > 400.0f) {
                    m_friendlyFire = 0.0f;
                    GetSquadCommander()->OnFriendlyFire(attacker);
                }
            }
            // Hit by an enemy while firing at nothing: the event check ends.
            if (human->m_role == HUMAN_ROLE_ENEMY && !m_fireOrder->target && FireDirSquare(m_fireOrder->fireDir) < 1e-16f)
                m_eventTime = 0;
        }
    }
    if (zone == 0xB) {
        // A hit on the head knocks the helmet off.
        if (C_Item *helmet = m_human->GetContainer(ITEM_CONTAINER_HEAD)->GetItem()) {
            S_vector velocity(dir.x, dir.y + 1.0f, dir.z);
            velocity.Normalize();
            velocity *= RandFloat() + 2.0f;
            m_human->DropItem(helmet->GetUid(), velocity);
        }
    } else if (zone == 3) {
        // A hit on the arm now and then drops the gun.
        if (std::rand() % 20 == 0) {
            UpdateHandsItem();
            if (m_handsItem) {
                const int carryMode = m_handsItem->GetDef()->GetCarryMode();
                if (carryMode == 1 || (carryMode > 2 && carryMode < 5)) {
                    m_human->DropItem(m_handsItem->GetUid(), m_human->GetViewDir());
                    SayReply(SENTENCE_WEAPON_DROPPED, false, 500, false);
                }
            }
        }
    }
}

// ------------------------------------------------------------------------- the combat update

void C_SquadController::UpdateCombat(int timeDelta, bool ordersDone)
{
    C_Actor *squadTarget = GetSquadCommander()->GetSquadTarget();
    bool attack = false;
    bool vehicleAttack = false;
    if (m_eventTime != 0) {
        m_eventTime -= timeDelta;
        if (m_eventTime < 0)
            m_eventTime = 0;
    }
    if (m_weaponCheckTime != 0) {
        m_weaponCheckTime -= timeDelta;
        if (m_weaponCheckTime < 0)
            m_weaponCheckTime = 0;
    }
    if (m_alertTime != 0) {
        m_alertTime -= timeDelta;
        if (m_alertTime < 0)
            m_alertTime = 0;
    }
    if (m_alertTime == 0 && m_unknownAC)
        LowerWeapon();

    if (C_Actor *meleeTracker = FindMeleeTrackerTarget(7.0f)) {
        AimAt(meleeTracker);
    } else if (!m_action.finished && m_action.type == SQUAD_ACTION_ATTACK_VEHICLE) {
        if (!IsActorDead(m_action.target)) {
            AimAt(m_action.target);
        } else {
            m_action.arrived = true;
            m_action.completed = true;
        }
    } else if (!(m_flags & FLAG_ENGAGED)) {
        C_Actor *target = squadTarget;
        bool choose = false;
        if (!(m_flags & FLAG_2) || FindEnemyTargeting(true)) {
            // The squad's target, else (not holding fire) the member's own.
            if (!squadTarget && !(m_flags & FLAG_HOLD_FIRE))
                target = PickCombatTarget(nullptr);
            choose = true;
        } else if (squadTarget) {
            choose = true;
        } else {
            m_fireOrder->SetTarget(nullptr);
        }
        if (choose) {
            bool clear = false;
            if (!m_action.finished) {
                const int type = m_action.type;
                if (type == SQUAD_ACTION_ATTACK || type == SQUAD_ACTION_ATTACK_SILENTLY || type == SQUAD_ACTION_ATTACK_VEHICLE) {
                    if (type == SQUAD_ACTION_ATTACK_VEHICLE)
                        vehicleAttack = true;
                    else
                        attack = true;
                    if (IsActorDead(m_action.target)) {
                        m_action.arrived = true;
                        m_action.completed = true;
                    }
                } else if (type == SQUAD_ACTION_MOVE_TO) {
                    clear = true;
                }
            }
            // Walking orders: the AI mode decides (zombie: nothing; defensive: only at who aims
            // at the member).
            if (!clear && target && !attack && !vehicleAttack && GetOrderCount() != 0) {
                if (m_moveFlags & MOVE_AI_ZOMBIE)
                    target = nullptr;
                if ((m_moveFlags & MOVE_AI_DEFENSIVE) && !FindEnemyTargeting(true))
                    clear = true;
            }
            if (clear)
                target = nullptr;
            // Idle members of modes 0 and 2 do not fire on their own; silent ones need a
            // silenced weapon.
            if ((ordersDone || m_orders.empty()) && (m_combatMode == 2 || m_combatMode == 0) && !vehicleAttack && !attack)
                target = nullptr;
            if (m_silentMode && m_handsItem && !IsSilencedType(m_handsItem->GetDef()->GetId()))
                target = nullptr;
            if (!(m_flags & FLAG_LAUNCHER)) {
                m_fireOrder->SetTarget(target);
            } else {
                // A launcher at the action's place.
                const S_vector &own = m_human->GetFrame()->GetWorldPos();
                m_fireOrder->SetFireDir({m_action.pos.x - own.x, m_action.pos.y - own.y, m_action.pos.z - own.z, 0.0f});
            }
            SetBurstTimes(target);
        }
        if (!target && m_eventTime == 0) {
            // Nothing to fire at: the planes, every second; and the cover's facing.
            if (m_airCheckTime == 0) {
                m_airTarget = (m_flags & FLAG_HOLD_FIRE) ? nullptr : FindAirTarget(&m_airTargetFrame);
                m_airCheckTime = 1000;
            } else {
                m_airCheckTime -= timeDelta;
                if (m_airCheckTime < 0)
                    m_airCheckTime = 0;
            }
            if ((m_flags & FLAG_2) && m_alertTime == 0) {
                S_vector4 dir;
                uint32_t stance;
                ChooseCoverFacing(&dir, &stance);
                if (stance == MOVE_LYING)
                    stance = MOVE_CROUCHING;
                C_Human *leader = m_human->GetLeader();
                if (!leader
                    || (stance <= (leader->GetController()->GetMoveFlags() & MOVE_STANCE_MASK)
                        && stance != (m_moveFlags & MOVE_STANCE_MASK)))
                    SetStance(static_cast<int>(stance));
                dir.y = 0.0f;
                m_facing->SetView(dir);
            }
        }
    } else if (m_engageTime == 0) {
        Engage(false, m_human->GetPosition4());
    } else {
        // Engaged: at what is around the engage point (1 m lower), else at the point.
        const S_vector4 &own = m_human->GetPosition4();
        S_vector d(m_engagePos.x - own.x, (m_engagePos.y - own.y) - 1.0f, m_engagePos.z - own.z);
        d.Normalize();
        if (C_Human *target = PickCombatTarget(&d))
            m_fireOrder->SetTarget(target);
        else
            m_fireOrder->SetFireDir({d.x, d.y, d.z, own.w});
    }

    // Not at the surrendered and the unconscious (except an attack's own target).
    if (C_Actor *target = m_fireOrder->target) {
        C_Human *human = static_cast<C_Human *>(target);
        if ((human->IsSurrendered() || human->IsUnconscious()) && (!attack || target != m_action.target))
            m_fireOrder->SetTarget(nullptr);
    }
    // Nothing else: a meleeTracker within 30 m.
    if (!m_fireOrder->target && FireDirSquare(m_fireOrder->fireDir) < 1e-16f) {
        C_Actor *meleeTracker = FindMeleeTrackerTarget(30.0f);
        if (meleeTracker && meleeTracker->GetType() == ACTOR_MELEE_TRACKER)
            AimAt(meleeTracker);
    }
    // The crew of an armoured car only with a launcher.
    if (C_Actor *target = m_fireOrder->target) {
        if (target->GetType() == ACTOR_HUMAN) {
            C_Actor *vehicle = static_cast<C_Human *>(target)->GetVehicle();
            if (vehicle && vehicle->GetType() == ACTOR_CAR && static_cast<C_Car *>(vehicle)->GetUnknown5E0() && m_handsItem
                && BehaviourOf(m_handsItem) != ITEM_BEHAVIOUR_LAUNCHER)
                m_fireOrder->SetTarget(nullptr);
        }
    }
    // The weapon for the target (not at a vehicle's gun).
    if (WeaponCheckDue()) {
        C_Actor *vehicle = m_human->m_vehicle;
        if (!vehicle || m_human->m_seat == 0 || !vehicle->HasSeatWeapon(m_human->m_seat))
            UpdateWeaponChoice();
    }

    C_Actor *previous = m_fireOrder->target;
    bool lyingKept = false;
    if (m_handsItem && !m_human->m_vehicle) {
        const int behaviour = BehaviourOf(m_handsItem);
        const bool onRoute = (m_move->flags & S_AIMoveOrder::FLAG_ROUTE) != 0;
        bool proneOnly = behaviour == ITEM_BEHAVIOUR_LMG;   // (fires only lying)
        if (m_fireOrder->target || FireDirSquare(m_fireOrder->fireDir) >= 1e-16f) {
            if (onRoute && !m_move->unknown58) {
                // Walking: now and then the member stops to fire.
                if (m_fireMoveTime < 0) {
                    m_fireMoveTime = 1;
                } else {
                    m_fireMoveTime -= timeDelta;
                    if (m_fireMoveTime < 1 && m_handsItem && m_handsItem->GetDef()) {
                        const int b = BehaviourOf(m_handsItem);
                        if (b < 0 || b > 1) {
                            m_move->unknown58 = true;
                            int stopMin, stopSpread, fireMin, fireSpread;
                            GetBurstDefaults(&stopMin, &stopSpread, &fireMin, &fireSpread);
                            m_fireMoveTime = -(std::rand() % stopSpread + stopMin);
                        } else {
                            m_fireMoveTime = -1;
                        }
                    }
                }
            } else if (m_fireMoveTime > 0) {
                m_fireMoveTime = 0;
            } else {
                m_fireMoveTime += timeDelta;
                if (m_fireMoveTime >= 0) {
                    // Walking on.
                    m_move->unknown58 = false;
                    int stopMin, stopSpread, fireMin, fireSpread;
                    GetBurstDefaults(&stopMin, &stopSpread, &fireMin, &fireSpread);
                    m_fireMoveTime = std::rand() % fireSpread + fireMin;
                    if (m_handsItem && m_handsItem->GetDef()) {
                        const int b = BehaviourOf(m_handsItem);
                        if (b >= 0 && b < 2)
                            m_fireMoveTime = 0;
                    }
                    if (!ordersDone && onRoute && m_orderStance != 0 && (m_moveFlags & MOVE_STANCE_MASK) != m_orderStance)
                        SetStance(static_cast<int>(m_orderStance));
                }
                // Standing at a walking point: down to fire when the line is clear.
                if ((m_moveFlags & MOVE_STANCE_MASK) >= 3 && m_currentCheckpoint && behaviour != 5) {
                    if (HasClearLine())
                        SetStance(MOVE_CROUCHING);
                }
            }
        } else {
            // Nothing to fire at: walking on, in the order's stance (not while carrying a body).
            if (m_move->unknown58) {
                m_fireMoveTime = static_cast<int>((RandFloat() + 1.0f) * 1000.0f);
                m_move->unknown58 = false;
            }
            if (!ordersDone && m_orderStance != 0 && (m_moveFlags & MOVE_STANCE_MASK) != m_orderStance
                && (m_action.finished || m_action.type != HUMAN_COMMAND_PICK_UP_BODY || !m_action.target
                    || m_action.target->GetType() != ACTOR_HUMAN))
                SetStance(static_cast<int>(m_orderStance));
            proneOnly = false;
        }
        if (proneOnly && (m_fireOrder->target || FireDirSquare(m_fireOrder->fireDir) >= 1e-16f)) {
            // A light machine gun: lying down to fire, stopped.
            const bool canFire = m_lyingToFire || CanFireAt();
            if ((!onRoute || m_move->unknown58) && canFire) {
                if ((m_moveFlags & MOVE_STANCE_MASK) != MOVE_LYING) {
                    m_fireOrder->SetTarget(nullptr);
                    SetStance(MOVE_LYING);
                }
                m_lyingToFire = true;
            } else {
                m_fireOrder->SetTarget(nullptr);
            }
            lyingKept = true;
        }
    }
    if (!lyingKept)
        m_lyingToFire = false;
    if ((m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && !m_move->unknown58)
        m_lyingToFire = false;
    // A rifle is not fired hurrying along.
    if (!m_human->m_vehicle && m_handsItem && m_handsItem->GetDef()->GetCarryMode() == 3) {
        const uint32_t pace = m_move->unknown54 & MOVE_PACE_MASK;
        if ((m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && !m_move->unknown58 && pace > MOVE_PACE_1 && pace != MOVE_PACE_2) {
            m_fireOrder->SetTarget(nullptr);
            if (m_unknownAC)
                LowerWeapon();
        }
    }
    // The target lost: looking where it was.
    if (previous && !m_fireOrder->target) {
        const S_vector4 &own = m_human->GetPosition4();
        const S_vector4 &was = previous->GetPosition4();
        m_facing->SetView({was.x - own.x, 0.0f, was.z - own.z, 0.0f});   // (w: a stale local in the original)
    }
    // An empty weapon without ammo: nothing to fire.
    if (!m_human->m_vehicle && m_handsItem && m_handsItem->GetMaxAmount() != -1.0f && m_handsItem->GetAmount() == 0.0f) {
        if (!m_human->FindItem(m_handsItem->GetDef()->GetAmmoItemId()))
            m_fireOrder->SetTarget(nullptr);
    }
    // Melee weapons strike within 1.2 m.
    if (!m_human->m_vehicle && m_handsItem && m_handsItem->GetDef() && m_fireOrder->target) {
        const int b = BehaviourOf(m_handsItem);
        if (b >= 0 && b < 2) {
            const S_vector4 &own = m_human->GetPosition4();
            const S_vector4 &to = m_fireOrder->target->GetPosition4();
            const S_vector d(to.x - own.x, to.y - own.y, to.z - own.z);
            if (d.Magnitude() < 1.2f)
                Melee(m_fireOrder->target);
        }
    }
    if (m_fireOrder->target || FireDirSquare(m_fireOrder->fireDir) >= 1e-16f)
        m_alertTime = 5000;
}

C_Actor *C_SquadController::FindAirTarget(I3D_frame **frame)
{
    if (C_Actor *plane = FindAirAttackFor(m_human))
        return plane;
    return FindJunkersTargetFor(m_human, frame);
}

void C_SquadController::SetBurstTimes(C_Actor *target)
{
    // The bursts longer and the pauses shorter the nearer the target (by the weapon's carry
    // mode); -1: the defaults.
    int burst = -1;
    int burstSpread = -1;
    int pause = -1;
    int pauseSpread = -1;
    if (target) {
        UpdateHandsItem();
        if (m_handsItem) {
            const int carryMode = m_handsItem->GetDef()->GetCarryMode();
            if (carryMode >= 1 && carryMode <= 4) {
                const S_BurstTable &t = BURST_TABLES[carryMode - 1];
                if (t.burstFar != -1) {
                    const S_vector d = m_human->GetFrame()->GetWorldPos() - target->GetFrame()->GetWorldPos();
                    const float distance2 = d.z * d.z + d.x * d.x + d.y * d.y;
                    if (distance2 <= static_cast<float>(t.burstNear2)) {
                        burst = t.burstNear;
                    } else if (static_cast<float>(t.burstFar2) <= distance2) {
                        burst = t.burstFar;
                    } else {
                        const float x = (distance2 - static_cast<float>(t.burstNear2)) / static_cast<float>(t.burstFar2 - t.burstNear2);
                        burst = static_cast<int>((1.0f - x) * static_cast<float>(t.burstNear - t.burstFar) + static_cast<float>(t.burstFar));
                    }
                    burstSpread = static_cast<int>(static_cast<float>(burst) * 0.1f);
                    if (distance2 <= static_cast<float>(t.pauseNear2)) {
                        pause = t.pauseNear;
                    } else if (static_cast<float>(t.pauseFar2) <= distance2) {
                        pause = t.pauseFar;
                    } else {
                        const float x = (distance2 - static_cast<float>(t.pauseNear2)) / static_cast<float>(t.pauseFar2 - t.pauseNear2);
                        pause = static_cast<int>(x * static_cast<float>(t.pauseFar - t.pauseNear) + static_cast<float>(t.pauseNear));
                    }
                    pauseSpread = static_cast<int>(static_cast<float>(pause) * 0.1f);
                }
            }
        }
    }
    m_fireOrder->SetBurstTime(burst, burstSpread);
    m_fireOrder->SetPauseTime(pause, pauseSpread);
}

void C_SquadController::GetBurstDefaults(int *stopMin, int *stopSpread, int *fireMin, int *fireSpread)
{
    // How long the member stops to fire and walks on (by the weapon's behaviour).
    if (m_handsItem) {
        switch (BehaviourOf(m_handsItem)) {
        case ITEM_BEHAVIOUR_SMG:
        case ITEM_BEHAVIOUR_ASSAULT:
            *stopMin = 1000;
            *stopSpread = 1000;
            *fireMin = 1000;
            *fireSpread = 1500;
            return;
        case ITEM_BEHAVIOUR_RIFLE:
            *stopMin = 2500;
            *stopSpread = 500;
            *fireMin = 1500;
            *fireSpread = 500;
            return;
        case ITEM_BEHAVIOUR_LMG:
            *stopMin = 4500;
            *stopSpread = 2000;
            *fireMin = 2000;
            *fireSpread = 2000;
            return;
        default:
            break;
        }
    }
    *stopMin = 1500;
    *stopSpread = 1000;
    *fireMin = 1500;
    *fireSpread = 1000;
}

bool C_SquadController::CanFireAt()
{
    // Level enough (10 degrees), outdoors, a gun that fires straight, and the line clear.
    C_Actor *target = m_fireOrder->target;
    if (!target && FireDirSquare(m_fireOrder->fireDir) < 1e-16f)
        return false;
    S_vector from = m_human->GetFrame()->GetWorldPos();
    S_vector d;
    if (!target)
        d = XYZ(m_fireOrder->fireDir);
    else
        d = target->GetFrame()->GetWorldPos() - from;
    const float length = S_vector(d.x, d.y, d.z).Magnitude();
    if (std::fabs(d.y / length) > LEVEL_FIRE_SIN)
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
    if (m_human->m_body.GetSector() != GetGame()->GetScene()->GetPrimarySector())
        return false;
    S_vector xz(d.x, 0.0f, d.z);
    xz.Normalize();
    from.x = xz.x * 0.5f + from.x;
    from.y = from.y + 0.15f + xz.y * 0.5f;
    from.z = xz.z * 0.5f + from.z;
    if (!target)
        return true;
    const S_vector to = static_cast<C_Human *>(target)->GetBodyFrame()->GetWorldPos() - from;
    return g_collisionWorld.RayTest(from, to, nullptr, nullptr, 3, 0) == nullptr;
}

bool C_SquadController::HasClearLine()
{
    C_Actor *target = m_fireOrder->target;
    if (!target)
        return false;
    S_vector from = m_human->GetFrame()->GetWorldPos();
    S_vector d = target->GetFrame()->GetWorldPos() - from;
    const float length = d.Magnitude();
    if (std::fabs(d.y / length) > LEVEL_FIRE_SIN)
        return false;
    switch (BehaviourOf(m_handsItem)) {
    case ITEM_BEHAVIOUR_MELEE:
    case ITEM_BEHAVIOUR_LAUNCHER:
    case 7:
    case ITEM_BEHAVIOUR_THROWN:
        return false;
    default:
        break;
    }
    if (m_human->m_body.GetSector() != GetGame()->GetScene()->GetPrimarySector())
        return false;
    // From the crouching eyes (0.75 m up, half a metre on) to the body.
    d.y = 0.0f;
    d.Normalize();
    d *= 0.5f;
    from.x = d.x + from.x;
    from.y = from.y + 0.75f + d.y;
    from.z = from.z + d.z;
    S_vector to;
    if (target->GetType() == ACTOR_HUMAN) {
        to = static_cast<C_Human *>(target)->GetBodyFrame()->GetWorldPos();
    } else {
        to = target->GetFrame()->GetWorldPos();
        to.y += 0.5f;
    }
    S_vector hit;
    return g_collisionWorld.RayTest(from, to - from, &hit, nullptr, 3, 0) == nullptr;
}

// ------------------------------------------------------------------------- targets

C_Actor *C_SquadController::FindNearMeleeTracker(float range)
{
    // A meleeTracker the team saw within `range`, near (4 m) or in sight; dead ones go off the list.
    std::vector<C_MeleeTracker *> &meleeTrackers = g_teamManager.GetPlayerTeam()->GetSeenMeleeTrackers();
    for (size_t i = meleeTrackers.size(); i-- > 0;) {
        C_MeleeTracker *meleeTracker = meleeTrackers[i];
        const S_vector4 &to = meleeTracker->GetPosition4();
        const S_vector4 &own = m_human->GetPosition4();
        const float dx = to.x - own.x;
        const float dy = to.y - own.y;
        const float dz = to.z - own.z;
        const float distance2 = dx * dx + dy * dy + dz * dz;
        if (!(distance2 < range * range))
            continue;
        if (!meleeTracker->IsAlive()) {
            meleeTrackers[i] = meleeTrackers.back();
            meleeTrackers.pop_back();
        } else if (distance2 < 16.0f || g_sightModule.CanSee(m_human->GetSightId(), meleeTracker->GetSightId(), nullptr, nullptr) != 0) {
            return meleeTrackers[i];
        }
    }
    return nullptr;
}

C_Actor *C_SquadController::FindMeleeTrackerTarget(float range)
{
    if (!m_action.finished) {
        const int type = m_action.type;
        C_Actor *target = m_action.target;
        if (type == SQUAD_ACTION_ATTACK || type == SQUAD_ACTION_ATTACK_SILENTLY) {
            if (target && target->GetType() == ACTOR_MELEE_TRACKER)
                return target;
        } else if (type == SQUAD_ACTION_ATTACK_VEHICLE && target && m_handsItem) {
            // A vehicle: in the weapon's range (by its carry mode) and in sight.
            const S_vector4 &own = m_human->GetPosition4();
            const S_vector4 &to = target->GetPosition4();
            const float dx = own.x - to.x;
            const float dy = own.y - to.y;
            const float dz = own.z - to.z;
            const float distance2 = dx * dx + dy * dy + dz * dz;
            bool inRange = false;
            switch (m_handsItem->GetDef()->GetCarryMode()) {
            case 1:
            case 4:
                inRange = !(distance2 >= 400.0f);
                break;
            case 2:
                inRange = !(distance2 >= 100.0f);
                break;
            case 3:
                if (distance2 < 2500.0f)
                    inRange = true;
                inRange = inRange || !(distance2 >= 1.44f);
                break;
            case 6:
                inRange = !(distance2 >= 1.44f);
                break;
            default:
                inRange = !(distance2 >= 900.0f);
                break;
            }
            const bool sees = CanSeeVehicle(target, m_human);
            if (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE)) {
                // Out of range at the walk's end: walk to it again.
                if (!inRange && !IsActorDead(target) && m_action.slot == 0) {
                    const S_vector &at = target->GetFrame()->GetWorldPos();
                    const float mx = at.x - m_move->pos.x;
                    const float my = at.y - m_move->pos.y;
                    const float mz = at.z - m_move->pos.z;
                    if (mx * mx + my * my + mz * mz < 0.04f) {
                        m_action.completed = true;
                        ActOn(m_action.type, target, false, m_action.radius, false);
                        m_action.noCommand = true;
                    }
                }
            } else if (inRange) {
                if (!sees)
                    return nullptr;
                m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
                m_action.arrived = true;
            }
            return sees ? target : nullptr;
        }
    }
    return FindNearMeleeTracker(range);
}

void C_SquadController::AimAt(C_Actor *target)
{
    const I3D_frame *frame;
    switch (target->GetType()) {
    case ACTOR_CAR:
        frame = target->GetFrame()->FindChildFrame("body", 0xFFFF);
        break;
    case ACTOR_STATIC_WEAPON:
    case ACTOR_ANIMATION:
        frame = target->GetFrame();
        break;
    case ACTOR_MELEE_TRACKER:
        frame = target->GetFrame()->FindChildFrame("c", 0xFFFF);
        break;
    default:
        return;
    }
    const S_vector d = frame->GetWorldPos() - m_human->GetBodyFrame()->GetWorldPos();
    const S_vector4 dir = {d.x, d.y, d.z, 0.0f};        // (w: a stale local in the original)
    // A grenade in the hands is thrown (at the action's place, beyond 6 m).
    C_Item *hands = m_human->GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
    if (hands && BehaviourOf(hands) == ITEM_BEHAVIOUR_THROWN) {
        const S_vector4 &own = m_human->GetPosition4();
        const float dx = m_action.pos.x - own.x;
        const float dy = m_action.pos.y - own.y;
        const float dz = m_action.pos.z - own.z;
        if (36.0f < dx * dx + dy * dy + dz * dz) {
            m_fireOrder->SetThrow(m_action.pos);
            m_action.delay = S_HumanBody::GetCarryAnimLength(ANIM_THROW, (~m_moveFlags >> 2) & 1, 0);
            return;
        }
        SayReply(SENTENCE_CANNOT, true, 300, false);
        m_action.completed = true;
        return;
    }
    m_facing->SetView(dir);
    m_fireOrder->SetFireDir(dir);
}

C_Human *C_SquadController::PickCombatTarget(const S_vector *dir)
{
    UpdateHandsItem();
    if (!m_handsItem)
        return nullptr;
    C_Human *target = PickTarget(dir);
    if (!m_action.finished && (m_action.type == SQUAD_ACTION_ATTACK || m_action.type == SQUAD_ACTION_ATTACK_SILENTLY)) {
        // The attack's target: walked to until in the weapon's range, then preferred unless
        // another is much nearer.
        C_Human *attacked = static_cast<C_Human *>(m_action.target);
        const bool sees = m_human->HasSeenEnemy(attacked);
        const S_vector4 &own = m_human->GetPosition4();
        const S_vector4 &to = attacked->GetPosition4();
        const float dx = own.x - to.x;
        const float dy = own.y - to.y;
        const float dz = own.z - to.z;
        const float distance2 = dx * dx + dy * dy + dz * dz;
        float range2;
        switch (BehaviourOf(m_handsItem)) {
        case ITEM_BEHAVIOUR_MELEE:
        case 1: range2 = 1.44f; break;
        case ITEM_BEHAVIOUR_SIDEARM: range2 = 225.0f; break;
        case ITEM_BEHAVIOUR_SMG: range2 = 1600.0f; break;
        case ITEM_BEHAVIOUR_RIFLE:
        case ITEM_BEHAVIOUR_ASSAULT: range2 = 22500.0f; break;
        case ITEM_BEHAVIOUR_LMG: range2 = 4900.0f; break;
        case ITEM_BEHAVIOUR_LAUNCHER: range2 = 2500.0f; break;
        case ITEM_BEHAVIOUR_THROWN: range2 = 400.0f; break;
        default: range2 = 625.0f; break;
        }
        const bool inRange = !(distance2 >= range2);
        if (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE)) {
            if (!inRange && !IsActorDead(attacked) && m_action.slot == 0) {
                const S_vector &at = attacked->GetFrame()->GetWorldPos();
                const float mx = at.x - m_move->pos.x;
                const float my = at.y - m_move->pos.y;
                const float mz = at.z - m_move->pos.z;
                if (mx * mx + my * my + mz * mz < 0.04f) {
                    m_action.completed = true;
                    ActOn(m_action.type, attacked, false, 1.0f, false);
                    m_action.noCommand = true;
                }
            }
        } else if (inRange) {
            if (!sees)
                return target;
            m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
            m_action.arrived = true;
        }
        if (sees) {
            if (!target)
                return attacked;
            const S_vector d = target->GetFrame()->GetWorldPos() - m_human->GetFrame()->GetWorldPos();
            if (std::sqrt(distance2) * 0.4f < d.Magnitude())
                return attacked;
        }
    }
    return target;
}

C_Human *C_SquadController::FindEnemyTargeting(bool friendOnly)
{
    // A seen enemy aiming at the member (`friendOnly`: at any friend of it).
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    for (size_t i = enemies.size(); i-- > 0;) {
        C_Actor *target = enemies[i]->GetController()->GetLastTarget();
        if (!friendOnly) {
            if (target == m_human)
                return enemies[i];
        } else if (target && static_cast<C_Human *>(target)->IsFriendOf(m_human)) {
            return enemies[i];
        }
    }
    return nullptr;
}

bool C_SquadController::Unknown114(const S_vector *dir)
{
    // No living member of the team (in front) within 1.15 times its body spheres of the line
    // 200 m along `dir` from the body.
    const S_vector segment = *dir * 200.0f;
    S_vector forward = *dir;
    forward.Normalize();
    const S_vector from = m_human->GetBodyFrame()->GetWorldPos();
    C_Team *team = g_teamManager.GetPlayerTeam();
    for (int i = 0; i < team->GetMemberCount(); ++i) {
        C_Human *member = team->GetMember(i);
        if (member->GetHealth() <= 0 || member == m_human)
            continue;
        const S_vector &pos = member->GetFrame()->GetWorldPos();
        S_vector toward(pos.x - from.x, 0.0f, pos.z - from.z);
        toward.Normalize();
        if (!(0.0f < toward.x * forward.x + forward.y * toward.y + forward.z * toward.z))
            continue;
        int count = 0;
        const S_vector4 *spheres = reinterpret_cast<const S_vector4 *>(member->m_body.GetUnknown6E0(&count));
        if (count == 0)
            continue;
        const float length2 = segment.x * segment.x + segment.y * segment.y + segment.z * segment.z;
        for (int k = count; k-- > 0;) {
            const S_vector4 &sphere = spheres[k];
            float distance;
            if (length2 >= 1e-8f) {
                const float ox = from.x - sphere.x;
                const float oy = from.y - sphere.y;
                const float oz = from.z - sphere.z;
                const float t = (ox * segment.x + oy * segment.y + oz * segment.z) / length2;
                const S_vector off(ox - segment.x * t, oy - segment.y * t, oz - segment.z * t);
                distance = off.Magnitude();
            } else {
                distance = -1e16f;
            }
            if (distance < sphere.w * 1.15f)
                return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------------- weapons

bool C_SquadController::WeaponCheckDue()
{
    if (m_weaponCheckTime != 0)
        return false;
    m_weaponCheckTime = m_human->m_seenEnemies.empty() ? 5000 : 400;
    return true;
}

void C_SquadController::BuildWeaponPrefs(C_Actor *target, std::vector<uint32_t> &prefs)
{
    // The weapons to try by their kind, best first, by the target, its distance and whether
    // the member is outdoors.
    prefs.clear();
    uint32_t mask = PREF_ALL;
    auto add = [&](uint32_t bit) {
        if (mask & bit)
            prefs.push_back(bit);
        mask &= ~bit;
    };
    auto addAll = [&](std::initializer_list<uint32_t> bits) {
        for (uint32_t bit : bits)
            add(bit);
    };
    if (IsArmoured(target)) {
        prefs.push_back(PREF_LAUNCHER);
        mask = PREF_ALL & ~PREF_LAUNCHER;
    }
    if (m_silentMode)
        addAll({PREF_SMG_SILENCED, PREF_RIFLE_SILENCED, PREF_SIDEARM_SILENCED});
    if (m_human->m_body.GetSector() != GetGame()->GetScene()->GetPrimarySector()) {
        // Indoors.
        addAll({PREF_SMG_SILENCED, PREF_SIDEARM_SILENCED, PREF_RIFLE_SILENCED, PREF_SMG, PREF_RIFLE, PREF_SIDEARM, PREF_RIFLE_SCOPED, PREF_LMG,
                PREF_LAUNCHER, PREF_GRENADE});
        return;
    }
    if (!target) {
        addAll({PREF_RIFLE_SCOPED, PREF_LMG, PREF_RIFLE_SILENCED, PREF_SMG_SILENCED, PREF_SIDEARM_SILENCED, PREF_RIFLE, PREF_SMG, PREF_SIDEARM,
                PREF_LAUNCHER, PREF_GRENADE});
        return;
    }
    const S_vector d = m_human->GetFrame()->GetWorldPos() - target->GetFrame()->GetWorldPos();
    const float distance2 = d.y * d.y + d.x * d.x + d.z * d.z;
    const bool human = target->GetType() == ACTOR_HUMAN;
    if (1600.0f < distance2) {
        if (!human)
            addAll({PREF_LMG, PREF_RIFLE, PREF_LAUNCHER, PREF_RIFLE_SCOPED, PREF_RIFLE_SILENCED, PREF_SMG, PREF_SMG_SILENCED, PREF_SIDEARM_SILENCED,
                    PREF_SIDEARM, PREF_GRENADE});
        else
            addAll({PREF_RIFLE_SCOPED, PREF_LMG, PREF_RIFLE, PREF_RIFLE_SILENCED, PREF_SMG_SILENCED, PREF_SMG, PREF_LAUNCHER, PREF_SIDEARM_SILENCED,
                    PREF_SIDEARM, PREF_GRENADE});
    } else if (distance2 <= 400.0f) {
        if (human)
            addAll({PREF_SIDEARM_SILENCED, PREF_SMG_SILENCED, PREF_RIFLE_SILENCED, PREF_SMG, PREF_SIDEARM, PREF_RIFLE, PREF_RIFLE_SCOPED, PREF_LMG,
                    PREF_LAUNCHER, PREF_GRENADE});
        else
            addAll({PREF_SMG, PREF_RIFLE, PREF_LMG, PREF_SMG_SILENCED, PREF_RIFLE_SILENCED, PREF_RIFLE_SCOPED, PREF_SIDEARM_SILENCED, PREF_SIDEARM,
                    PREF_LAUNCHER, PREF_GRENADE});
    } else {
        if (!human)
            addAll({PREF_LMG, PREF_SMG, PREF_SMG_SILENCED, PREF_RIFLE, PREF_RIFLE_SILENCED, PREF_RIFLE_SCOPED, PREF_LAUNCHER, PREF_GRENADE,
                    PREF_SIDEARM_SILENCED, PREF_SIDEARM});
        else
            addAll({PREF_SMG_SILENCED, PREF_SIDEARM_SILENCED, PREF_RIFLE_SILENCED, PREF_SMG, PREF_RIFLE, PREF_RIFLE_SCOPED, PREF_LMG, PREF_LAUNCHER,
                    PREF_SIDEARM, PREF_GRENADE});
    }
}

C_Item *C_SquadController::FindWeapon(int behaviour, bool scoped, bool silenced, int *container)
{
    // The item of the behaviour (scoped and silenced as asked) that has rounds or ammo
    // carried: in the hands, on the back, then (not the long guns) on the belt and in the
    // backpack.
    *container = ITEM_CONTAINER_COUNT;
    auto matches = [&](C_Item *item) {
        C_ItemDef *def = item->GetDef();
        if (def->GetBehaviour() != behaviour)
            return false;
        bool isScoped = false;
        if (def->GetBehaviour() == 4) {
            const C_ItemComponent *secondary = def->GetSecondaryComponent();
            isScoped = secondary && secondary->GetKind() == ICK_SCOPE && static_cast<const C_ScopeComp *>(secondary)->GetAimMode() < 4;
        }
        if (scoped != isScoped)
            return false;
        if (silenced != IsSilencedType(def->GetId()))
            return false;
        return item->GetAmount() != 0.0f || m_human->CountAmmo(def->GetAmmoItemId(), true) != 0;
    };
    C_ItemContainer *hands = m_human->GetContainer(ITEM_CONTAINER_HANDS);
    if (!hands->IsEmpty() && matches(hands->GetItem())) {
        *container = ITEM_CONTAINER_HANDS;
        return hands->GetItem();
    }
    C_ItemContainer *longGun = m_human->GetContainer(ITEM_CONTAINER_LONG_GUN);
    if (!longGun->IsEmpty() && matches(longGun->GetItem())) {
        *container = ITEM_CONTAINER_LONG_GUN;
        return longGun->GetItem();
    }
    if (behaviour == ITEM_BEHAVIOUR_SMG || behaviour == ITEM_BEHAVIOUR_RIFLE || behaviour == ITEM_BEHAVIOUR_LMG || behaviour == ITEM_BEHAVIOUR_LAUNCHER || behaviour == ITEM_BEHAVIOUR_ASSAULT)
        return nullptr;
    C_ItemContainer *belt = m_human->GetContainer(ITEM_CONTAINER_BELT);
    for (int i = 0; i < belt->GetUsedSlotCount(); ++i) {
        const std::vector<C_Item *> &slot = belt->GetSlots()[i];
        for (size_t j = slot.size(); j-- > 0;) {
            if (matches(slot[j])) {
                *container = ITEM_CONTAINER_BELT;
                return slot[j];
            }
        }
    }
    if (C_ItemContainer *contents = m_human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)) {
        for (int i = 0; i < contents->GetUsedSlotCount(); ++i) {
            const std::vector<C_Item *> &slot = contents->GetSlots()[i];
            for (size_t j = slot.size(); j-- > 0;) {
                if (matches(slot[j])) {
                    *container = ITEM_CONTAINER_BACKPACK_CONTENTS;
                    return slot[j];
                }
            }
        }
    }
    return nullptr;
}

C_Item *C_SquadController::PickWeapon(const std::vector<uint32_t> &prefs, int *container)
{
    for (uint32_t pref : prefs) {
        int behaviour;
        bool scoped = false;
        bool silenced = false;
        switch (pref) {
        case PREF_GRENADE: behaviour = ITEM_BEHAVIOUR_THROWN; break;
        case PREF_SIDEARM: behaviour = ITEM_BEHAVIOUR_SIDEARM; break;
        case PREF_SMG: behaviour = ITEM_BEHAVIOUR_SMG; break;
        case PREF_RIFLE:
            if (C_Item *item = FindWeapon(ITEM_BEHAVIOUR_ASSAULT, false, false, container))
                return item;
            behaviour = ITEM_BEHAVIOUR_RIFLE;
            break;
        case PREF_LMG: behaviour = ITEM_BEHAVIOUR_LMG; break;
        case PREF_LAUNCHER: behaviour = ITEM_BEHAVIOUR_LAUNCHER; break;
        case PREF_SMG_SILENCED: behaviour = ITEM_BEHAVIOUR_SMG; silenced = true; break;
        case PREF_RIFLE_SCOPED: behaviour = ITEM_BEHAVIOUR_RIFLE; scoped = true; break;
        case PREF_RIFLE_SILENCED: behaviour = ITEM_BEHAVIOUR_RIFLE; silenced = true; break;
        case PREF_SIDEARM_SILENCED: behaviour = ITEM_BEHAVIOUR_SIDEARM; silenced = true; break;
        default: continue;
        }
        if (C_Item *item = FindWeapon(behaviour, scoped, silenced, container))
            return item;
    }
    return nullptr;
}

void C_SquadController::ChooseWeapon(C_Actor *target)
{
    if (m_interactionTime >= 1 && m_interaction != -1)
        return;
    UpdateHandsItem();
    if (IsWeaponSuitable(target, m_handsItem))
        return;
    std::vector<uint32_t> prefs;
    BuildWeaponPrefs(target, prefs);
    int container;
    C_Item *item = PickWeapon(prefs, &container);
    if (item && item != m_handsItem) {
        // (The original's format has a second %s it passes nothing for.)
        GetGame()->Print("%s: Switching to %s", m_human->GetLastName(), "");
        m_weaponCheckTime = 4000;
        Equip(item, &container);
    }
}

bool C_SquadController::IsWeaponSuitable(C_Actor *target, C_Item *item)
{
    if (!item)
        return false;
    if (m_silentMode && !IsSilencedType(item->GetDef()->GetId()))
        return false;
    if (item->GetAmount() == 0.0f && m_human->CountAmmo(item->GetDef()->GetAmmoItemId(), true) == 0)
        return false;
    const int carryMode = item->GetDef()->GetCarryMode();
    if (IsArmoured(target))
        return carryMode == 5;
    float distance2 = 0.0f;
    if (target) {
        const S_vector d = m_human->GetFrame()->GetWorldPos() - target->GetFrame()->GetWorldPos();
        distance2 = d.x * d.x + d.y * d.y + d.z * d.z;
    }
    switch (carryMode) {
    case 1:
        return distance2 <= 1600.0f;
    case 2:
        if (!target && !IsSilencedType(item->GetDef()->GetId()))
            return false;
        return !(distance2 >= 100.0f);
    case 3:
    case 4:
        return true;
    case 5:
        if (!target)
            return true;
        if (distance2 <= 100.0f)
            return false;
        return !(distance2 >= 2500.0f);
    case 6:
        if (distance2 <= 225.0f)
            return false;
        return !(distance2 >= 1600.0f);
    default:
        return false;
    }
}

bool C_SquadController::UpdateWeaponChoice()
{
    if (m_interactionTime > 0 && m_interaction != -1)
        return false;
    // The fire target (its vehicle when it rides one), else the attack's.
    C_Actor *target = m_fireOrder->target;
    if (target && target->GetType() == ACTOR_HUMAN && static_cast<C_Human *>(target)->GetVehicle())
        target = static_cast<C_Human *>(target)->GetVehicle();
    if (!m_action.finished
        && (m_action.type == SQUAD_ACTION_ATTACK || m_action.type == SQUAD_ACTION_ATTACK_SILENTLY
            || m_action.type == SQUAD_ACTION_ATTACK_VEHICLE)
        && !target && m_action.target)
        target = m_action.target;
    ChooseWeapon(target);
    return true;
}

bool C_SquadController::ReloadFromBelt()
{
    // The ammo of the weapon in the hands from the backpack to the belt.
    C_Item *hands = m_human->GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
    if (!hands || hands->GetMaxAmount() == -1.0f)
        return false;
    C_Item *ammo = m_human->FindItem(hands->GetDef()->GetAmmoItemId());
    if (!ammo)
        return false;
    m_human->RequestMoveItem(ammo, m_human, ITEM_CONTAINER_BACKPACK_CONTENTS, ITEM_CONTAINER_BELT, 0);
    return true;
}

void C_SquadController::Melee(C_Actor *target)
{
    if (m_moveFlags & MOVE_LYING)
        return;
    C_Item *hands = m_handsItem;
    const uint32_t now = GetGame()->GetMissionTime();
    if (now < hands->GetTime())
        return;
    C_WeaponItem *weapon = static_cast<C_WeaponItem *>(hands);
    if (static_cast<int>(now - weapon->GetAttackTime()) < 0)
        return;
    const S_vector4 facing = Dir4(m_human->GetFacingDir());
    bool backstab = false;
    if (target->GetType() == ACTOR_HUMAN) {
        // Behind the other (facing the same way): the knife from behind.
        const S_vector4 other = FrameDir4(target->GetFrame());
        if (0.8f < facing.x * other.x + other.y * facing.y + other.z * facing.z) {
            m_human->IssueCommand(MakeCommand(HUMAN_COMMAND_BACKSTAB, target->GetNetId(), 0));
            backstab = true;
        }
    }
    if (!backstab) {
        // A strike (the body's action 8).
        S_BodyCommand command = MakeBodyCommand(BODY_COMMAND_ACTION, 8);
        const S_vector4 data = {0.0f, 0.0f, 0.0f, facing.w};
        std::memcpy(command.data, &data, sizeof(data));
        m_human->ExecuteBodyCommand(command);
    }
    const bool quiet = (g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL;
    weapon->PlayShotSound(m_human, m_handsItem->GetFrame(0), quiet);
    const float delay = static_cast<C_WeaponComp *>(m_handsItem->GetDef()->GetComponent())->GetAttackDelay();
    weapon->SetAttackTime(GetGame()->GetMissionTime() + static_cast<int>(delay));
    m_human->SetDisguise(-10.0f);
}

// ------------------------------------------------------------------------- vehicles

bool C_SquadController::HasSeatWeapon()
{
    C_Actor *vehicle = m_human->m_vehicle;
    return vehicle && vehicle->HasSeatWeapon(m_human->m_seat);
}

bool C_SquadController::HasProjectileSeatWeapon()
{
    C_Actor *vehicle = m_human->m_vehicle;
    if (!vehicle)
        return false;
    C_ItemDef *weapon = vehicle->GetSeatWeaponDef(m_human->m_seat);
    return weapon && weapon->GetComponentKind() == ICK_PROJECTILE_WEAPON;
}

bool C_SquadController::VehicleCombat(int timeDelta)
{
    LookAround(m_human->m_seenEnemies.empty() ? 3 : 0);
    UpdateGunner(timeDelta);
    if (!m_fireOrder->target && FireDirSquare(m_fireOrder->fireDir) < 1e-16f)
        return true;
    S_vector4 dir;
    if (!m_fireOrder->target)
        dir = m_fireOrder->fireDir;
    else
        GetVehicleAimDir(&dir, m_fireOrder->target);
    S_vector d = XYZ(dir);
    d.Normalize();
    dir = {d.x, d.y, d.z, dir.w};
    m_facing->SetView(dir);
    m_facing->SetFacing(dir);
    C_Actor *vehicle = m_human->m_vehicle;
    const int seat = m_human->m_seat;
    float cone = 0.99f;
    if (vehicle->GetType() == ACTOR_JUNKERS88) {
        if (static_cast<C_Junkers88 *>(vehicle)->GetHealth() == 0.0f)
            return true;
        cone = 0.9f;
    }
    // No rounds: the reload axis.
    if (vehicle->GetSeatWeaponRounds(seat) == 0) {
        m_axes[AXIS_RELOAD] = 1.0f;
        return true;
    }
    // The gun pointing at it: fire.
    const S_vector aim = vehicle->GetSeatWeaponAim(seat);
    const float dot = d.x * aim.x + d.z * aim.z + d.y * aim.y;
    if (!(dot <= cone)) {
        m_axes[AXIS_FIRE] = 1.0f;
        m_buttons[BUTTON_FIRE] = 1.0f;
    }
    return true;
}

void C_SquadController::GetVehicleAimDir(S_vector4 *dir, C_Actor *target)
{
    // Where the seat's gun aims at the target (led and scattered), from the gun.
    C_Actor *vehicle = m_human->m_vehicle;
    const int seat = m_human->m_seat;
    S_vector4 point = WorldPos4Of(target->GetFrame());
    const S_vector origin = vehicle->GetSeatWeaponOrigin(seat);
    switch (target->GetType()) {
    case ACTOR_HUMAN: {
        C_Human *human = static_cast<C_Human *>(target);
        // The body (lower unless lying), led by an eighth of a second, scattered 0.1 m.
        if (!(human->GetController()->GetMoveFlags() & MOVE_LYING)) {
            const S_vector &body = human->GetBodyFrame()->GetWorldPos();
            point.x = body.x;
            point.y = body.y - 0.3f;
            point.z = body.z;
        }
        const S_vector &velocity = human->GetVelocity();
        point.x = velocity.x * 0.125f + point.x;
        point.y = velocity.y * 0.125f + point.y;
        point.z = point.z + velocity.z * 0.125f;
        const float r1 = RandSigned();
        const float r2 = RandSigned();
        const float r3 = RandSigned();
        point.x = r3 * 0.1f + point.x;
        point.y = r2 * 0.1f + point.y;
        point.z = point.z + r1 * 0.1f;
        break;
    }
    case ACTOR_CAR:
    case ACTOR_ANIMATION: {
        // The bound's centre, raised by half a percent of the distance.
        const S_vector center = target->GetFrame()->GetBound().center;
        point.x = center.x;
        point.y = center.y;
        point.z = center.z;
        const float distance = (center - origin).Magnitude();
        point.y = distance * 0.005f + point.y;
        break;
    }
    case ACTOR_MELEE_TRACKER: {
        const S_vector &body = target->GetFrame()->FindChildFrame("c", 0xFFFF)->GetWorldPos();
        point = {body.x, body.y, body.z, point.w};
        break;
    }
    case ACTOR_JUNKERS88: {
        // 10 m back along the plane the air target search gave, scattered half a metre.
        const S_vector4 planeDir = FrameDir4(m_airTargetFrame);
        const S_vector back(planeDir.x * 10.0f, planeDir.y * 10.0f, planeDir.z * 10.0f);
        const S_vector &plane = m_airTargetFrame->GetWorldPos();
        point.x = plane.x - back.x;
        point.y = plane.y - back.y;
        point.z = plane.z - back.z;
        const float a = RandFloat() - 0.5f;
        const float b = RandFloat() - 0.5f;
        const float c = RandFloat() - 0.5f;
        point.x = c + point.x;
        point.y = b + point.y;
        point.z = point.z + a;
        break;
    }
    case ACTOR_AIR_ATTACK: {
        // Led by 0.35 s of the plane's speed, scattered half a metre.
        const float speed = static_cast<C_AirAttack *>(target)->GetSpeed();
        const S_vector4 planeDir = FrameDir4(target->GetFrame());
        const S_vector lead(planeDir.x * speed * 0.35f, planeDir.y * speed * 0.35f, planeDir.z * speed * 0.35f);
        point.x = lead.x + point.x;
        point.y = lead.y + point.y;
        point.z = point.z + lead.z;
        const float a = RandFloat() - 0.5f;
        const float b = RandFloat() - 0.5f;
        const float c = RandFloat() - 0.5f;
        point.x = c + point.x;
        point.y = b + point.y;
        point.z = point.z + a;
        break;
    }
    default:
        break;
    }
    S_vector d = XYZ(point) - vehicle->GetSeatWeaponOrigin(seat);
    d.Normalize();
    *dir = {d.x, d.y, d.z, 0.0f};                       // (w: a stale local in the original)
}

void C_SquadController::AimSeatWeapon(C_Actor *target)
{
    S_vector4 dir;
    GetVehicleAimDir(&dir, target);
    if (target->GetType() == ACTOR_HUMAN)
        m_fireOrder->SetTarget(target);
    else
        m_fireOrder->SetFireDir(dir);
    m_facing->SetView(dir);
    m_facing->SetFacing(dir);
    // A cannon fires long, slow bursts.
    C_ItemDef *weapon = m_human->m_vehicle->GetSeatWeaponDef(m_human->m_seat);
    if (weapon->GetComponentKind() == ICK_PROJECTILE_WEAPON) {
        m_fireOrder->SetBurstTime(2000, 1000);
        m_fireOrder->SetPauseTime(2000, 2000);
        return;
    }
    m_fireOrder->SetBurstTime(1500, 1000);
    m_fireOrder->SetPauseTime(300, 400);
}

void C_SquadController::UpdateGunner(int timeDelta)
{
    if (m_eventTime != 0) {
        m_eventTime -= timeDelta;
        if (m_eventTime < 0)
            m_eventTime = 0;
    }
    if (m_weaponCheckTime != 0) {
        m_weaponCheckTime -= timeDelta;
        if (m_weaponCheckTime < 0)
            m_weaponCheckTime = 0;
    }
    if (m_alertTime != 0) {
        m_alertTime -= timeDelta;
        if (m_alertTime < 0)
            m_alertTime = 0;
    }
    if (m_human->m_vehicle->GetType() == ACTOR_JUNKERS88) {
        // The Junkers' gunner: at the planes.
        if (m_airCheckTime == 0) {
            m_airTarget = (m_flags & FLAG_HOLD_FIRE) ? nullptr : FindAirTarget(&m_airTargetFrame);
            m_airCheckTime = 1000;
        } else {
            m_airCheckTime -= timeDelta;
            if (m_airCheckTime < 0)
                m_airCheckTime = 0;
        }
        if (m_airTarget)
            AimSeatWeapon(m_airTarget);
        return;
    }
    if (m_flags & FLAG_2)
        return;
    if (!m_action.finished) {
        const int type = m_action.type;
        if (type == SQUAD_ACTION_ATTACK || type == SQUAD_ACTION_ATTACK_SILENTLY) {
            C_Human *target = static_cast<C_Human *>(m_action.target);
            if (target->GetHealth() < 1) {
                m_action.completed = true;
                m_action.noCommand = true;
                m_action.finished = true;
            } else if (m_human->HasSeenEnemy(target)) {
                m_action.turned = true;
                m_gunnerLooksAhead = false;
                AimSeatWeapon(target);
                return;
            } else {
                m_action.turned = false;
            }
        } else if (type == SQUAD_ACTION_ATTACK_VEHICLE) {
            C_Actor *target = m_action.target;
            if (!IsActorDead(target)) {
                const bool sees = CanSeeVehicle(target, m_human);
                m_action.turned = false;
                if (sees && target) {
                    m_gunnerLooksAhead = false;
                    AimSeatWeapon(target);
                    return;
                }
            } else {
                m_action.finished = true;
                m_action.completed = true;
                m_action.noCommand = true;
            }
        }
    }
    if (m_flags & FLAG_ENGAGED) {
        if (m_engageTime == 0) {
            Engage(false, m_human->GetPosition4());
            m_gunnerLooksAhead = false;
            return;
        }
        const S_vector4 &own = m_human->GetPosition4();
        S_vector d(m_engagePos.x - own.x, (m_engagePos.y - own.y) - 1.0f, m_engagePos.z - own.z);
        d.Normalize();
        if (C_Human *target = PickCombatTarget(&d))
            m_fireOrder->SetTarget(target);
        else
            m_fireOrder->SetFireDir({d.x, d.y, d.z, own.w});
        m_gunnerLooksAhead = false;
        return;
    }
    // A target: the member's, the squad's, a meleeTracker within 20 m.
    C_Actor *target = PickTarget(nullptr);
    if (!target)
        target = GetSquadCommander()->GetSquadTarget();
    if (!target)
        target = FindNearMeleeTracker(20.0f);
    if (target
        && (target->GetType() != ACTOR_HUMAN
            || (!static_cast<C_Human *>(target)->IsSurrendered() && !static_cast<C_Human *>(target)->IsUnconscious()))) {
        bool fire;
        if (m_orders.empty())
            fire = !(m_flags & (FLAG_2 | FLAG_HOLD_FIRE)) && m_combatMode != 2 && m_combatMode != 0;
        else if (m_moveFlags & MOVE_AI_ZOMBIE)
            fire = false;
        else
            fire = !(m_moveFlags & MOVE_AI_DEFENSIVE) || FindEnemyTargeting(true);
        if (fire) {
            m_gunnerLooksAhead = false;
            m_alertTime = 5000;
            AimSeatWeapon(target);
            return;
        }
    }
    m_fireOrder->SetTarget(nullptr);
    if (ReactToStimuli()) {
        m_gunnerLooksAhead = false;
        return;
    }
    if (m_eventTime != 0)
        return;
    // Nothing: now and then a look round, else ahead.
    S_vector4 look;
    if (std::rand() % 3 == 0) {
        S_matrix m;
        m.Identity();
        m.SetDir(S_vector(0.0f, 1.0f, 0.0f), RandFloat() * 6.2831855f);
        look = {m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3]};
        m_eventTime = static_cast<int>(RandFloat() * 2000.0f + 8000.0f);
        m_gunnerLooksAhead = false;
    } else {
        look = FrameDir4(m_human->GetFrame());
        m_holdDir = look;
        m_eventTime = static_cast<int>(RandFloat() * 2000.0f + 10000.0f);
        m_gunnerLooksAhead = true;
    }
    m_facing->SetView(look);
    m_facing->SetFacing(look);
}

// ------------------------------------------------------------------------- the alert preset

void C_SquadController::UpdateAlertPreset()
{
    // How the AI behaves (S_AIAlertState: 1 moving first, 2 firing first, 4 keeping the
    // direction; names from the original's debug prints).
    const int kind = m_orders.empty() ? 0 : m_orders[0]->kind;
    UpdateHandsItem();
    float slot2 = 0.0f;
    if (m_following && m_action.finished) {
        C_Human *leader = m_human->GetLeader();
        if (leader) {
            if (C_FormationShape *formation = leader->GetFormation()) {
                S_vector4 slot = {};                    // (a stale local in the original without a slot)
                formation->GetSlotTarget(m_human, &slot, nullptr);
                const S_vector &pos = m_human->GetFrame()->GetWorldPos();
                const float dx = pos.x - slot.x;
                float dy = pos.y - slot.y;
                const float dz = pos.z - slot.z;
                if (std::fabs(dy) < 5.0f)
                    dy = 0.0f;
                slot2 = dy * dy + dx * dx + dz * dz;
            }
        }
    }
    int preset;
    if (!m_handsItem || m_human->m_body.IsWorkState5()) {
        preset = 1;
    } else if (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE)) {
        preset = 4;
    } else if (AtSpecialPoint()
               || (!m_action.finished && m_action.type != SQUAD_ACTION_BOUND && m_action.type != SQUAD_ACTION_MOVE_NEAR
                   && m_action.type != SQUAD_ACTION_THROW && m_action.type != SQUAD_ACTION_FOLLOW)
               || (m_following && (m_followDelay != 0 || 49.0f < slot2))) {
        preset = 1;
    } else {
        bool move = false;
        if (kind == HUMAN_ORDER_MOVE) {
            if (m_moveFlags & MOVE_AI_ZOMBIE) {
                move = true;
            } else {
                S_HumanOrder *order = m_orders[0];
                if (!(order->flags & MOVE_PACE_MASK))
                    order->flags |= MOVE_PACE_4;
                if (order->flags & MOVE_PACE_MARCH)
                    move = true;
            }
        }
        if (move)
            preset = 1;
        else if (m_flags & (FLAG_1 | FLAG_THROW | FLAG_ENGAGED | FLAG_HOLDING | FLAG_LAUNCHER))
            preset = 4;
        else if (!m_fireOrder->target && FireDirSquare(m_fireOrder->fireDir) < 1e-16f && m_eventTime == 0)
            preset = 1;
        else
            preset = 4;
    }
    if (preset == m_alert->Get())
        return;
    const char *format = nullptr;
    switch (preset) {
    case 0: format = "%s - Set preset none!"; break;
    case 1: format = "%s - Set preset prior move!"; break;
    case 2: format = "%s - Set preset prior fire!"; break;
    case 4: format = "%s - Set preset prior keep dir!"; break;
    default: break;
    }
    if (format)
        GetGame()->Print(format, m_human->GetLastName());
    m_alert->Set(preset);
}
