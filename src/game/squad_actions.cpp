// C_SquadController's actions (game/human_controller.h; original source 0x00528950..0x0052a3b0,
// 0x00534340..0x00538a60): the action a squad member carries out (walk to the place, turn,
// then the command), the squad commands of the command module that start them, the order
// queue's processing, and the looting of bodies.
#include "game/human_controller.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "game/actor.h"
#include "game/actors_module.h"
#include "game/ai_parts.h"
#include "game/checkpoints.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/soldier.h"
#include "game/squad_commander.h"
#include "game/squad_internal.h"
#include "game/squad_order.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// The members turn toward the action until within this cosine (about 18 degrees).
constexpr float TURNED_COS = 0.95f;

// Where the actor is (its frame).
S_vector4 ActorPos4(const C_Actor *actor)
{
    return WorldPos4Of(actor->GetFrame());
}

// The order's place: its actor's, else its position.
S_vector4 OrderPos4(const S_HumanOrder *order)
{
    return order->actor ? ActorPos4(order->actor) : order->pos;
}

// The weapon of a firearm (the item definition's component).
bool IsFirearm(const C_Item *item)
{
    return item->GetDef()->GetComponentKind() == ICK_FIREARM;
}

// A weapon carried like a rifle or a pistol (carry modes 1, 3, 4).
bool IsCarriedGun(int carryMode)
{
    return carryMode == ITEM_CARRY_SMG || carryMode == ITEM_CARRY_RIFLE || carryMode == ITEM_CARRY_LMG;
}

}  // namespace

// ------------------------------------------------------------------------- the action

void S_SquadAction::Set(C_Actor *target_, int type_, const S_vector4 &pos_, const S_vector4 &dir_, bool keepGoing_,
                        float radius_)
{
    type = type_;
    turned = false;
    finished = false;
    arrived = false;
    target = target_;
    pos = pos_;
    dir = dir_;
    delay = 0;
    targetId = -1;
    started = false;
    if (target)
        targetId = target->GetNetId();
    completed = false;
    keepGoing = keepGoing_;
    radius = radius_;
    skipTurn = false;
    noCommand = false;
    notifyModule = false;
    stance = 0;
    slot = 0;
    useDir = false;
}

void C_SquadController::ActOn(int type, C_Actor *actor, bool keepGoing, float radius, bool notify)
{
    m_action.Set(actor, type, ActorPos4(actor), m_human->GetDirection4(), keepGoing, radius);
    if (notify)
        NotifyIfIdle();
}

void C_SquadController::DropCarriedBody()
{
    if (!m_human->m_carried)
        return;
    C_HumanController::IssueCommand(MakeCommand(HUMAN_COMMAND_LEAVE_VEHICLE, m_action.targetId, 0));
    m_action.delay = 1000;
}

bool C_SquadController::ExecuteAction(int timeDelta)
{
    if (m_ordersHeld)
        return true;
    if (m_action.delay != 0) {
        m_action.delay -= timeDelta;
        if (m_action.delay > 0)
            return true;
        if (m_action.delay < 0)
            m_action.delay = 0;
    }
    if (m_action.finished) {
        // Nothing to do: the cover's stance, and holding, the way the member holds.
        if (m_coverStance != 0 && m_coverStance != (m_moveFlags & m_coverStance)) {
            SetStance(static_cast<int>(m_coverStance));
            m_coverStance = 0;
        }
        if (m_flags & FLAG_HOLDING) {
            if (m_alertTime == 0 && m_eventTime == 0) {
                m_facing->SetView(m_holdDir);
                m_facing->SetFacing(m_holdDir);
            }
            return true;
        }
        return false;
    }

    // Attacks and following go on while the target lives.
    bool attack = false;
    bool vehicleAttack = false;
    bool follow = false;
    switch (m_action.type) {
    case SQUAD_ACTION_ATTACK_VEHICLE:
        vehicleAttack = true;
        break;
    case SQUAD_ACTION_ATTACK:
    case SQUAD_ACTION_ATTACK_SILENTLY:
        attack = true;
        break;
    case SQUAD_ACTION_FOLLOW:
        follow = true;
        break;
    default:
        break;
    }

    // 1. The walk to the place starts.
    if (!m_action.started) {
        if (!m_action.arrived) {
            C_Actor *vehicle = m_human->m_vehicle;
            if (vehicle) {
                bool leave = false;
                if (vehicle->GetType() == ACTOR_CAR) {
                    if (m_human->m_seat != 0) {
                        // A passenger at the seat's weapon attacks from there.
                        if ((attack || vehicleAttack) && vehicle->HasSeatWeapon(m_human->m_seat))
                            return true;
                        leave = true;
                    }
                } else if (vehicle->GetType() == ACTOR_STATIC_WEAPON) {
                    if (attack || vehicleAttack)
                        return true;
                    leave = true;
                }
                if (leave)
                    C_HumanController::IssueCommand(MakeCommand(HUMAN_COMMAND_LEAVE_VEHICLE, 0, 0));
            }
            C_Actor *own = m_human->m_vehicle;
            const bool driver = own && own->GetType() == ACTOR_CAR && m_human->m_seat == 0;
            m_action.started = true;
            if (driver) {
                m_driver->SetVehicle(own);
                m_driver->DriveTo(m_action.pos, 1.0f);
                m_driver->SetPace(m_quiet ? MOVE_PACE_1 : MOVE_PACE_3);
            } else {
                LowerWeapon();
                m_move->SetPos(m_action.pos);
                m_flags &= ~FLAG_1;
                // The direction kept is away from the place. (Its w: a stale local in the
                // original.)
                const S_vector4 &own4 = m_human->GetPosition4();
                S_vector away(own4.x - m_action.pos.x, own4.y - m_action.pos.y, own4.z - m_action.pos.z);
                away.Normalize();
                m_holdDir.x = away.x;
                m_holdDir.y = away.y;
                m_holdDir.z = away.z;
            }
            SetStatusIcon(m_quiet ? 5 : 4);
            return true;
        }
    } else if (!m_action.arrived) {
        // 2. Walking (or driving): arrived at the target or within the radius.
        C_Actor *own = m_human->m_vehicle;
        if (!own || own->GetType() != ACTOR_CAR || m_human->m_seat != 0) {
            const S_vector &pos = m_human->GetFrame()->GetWorldPos();
            const float dx = m_action.pos.x - pos.x;
            const float dy = m_action.pos.y - pos.y;
            const float dz = m_action.pos.z - pos.z;
            const float distance2 = dy * dy + dz * dz + dx * dx;
            const float radius2 = m_action.radius * m_action.radius;
            if ((m_action.target && IsAtTarget(m_action.target)) || distance2 < radius2) {
                m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
                m_action.arrived = true;
            }
            // About to pick up a body: stand up.
            if (m_action.type == HUMAN_COMMAND_PICK_UP_BODY && distance2 < 16.0f && m_action.target
                && m_action.target->GetType() == ACTOR_HUMAN) {
                SetStance(MOVE_STANDING);
                return true;
            }
        }
        return true;
    }

    // 3. Turned toward the target: the action's end.
    if (m_action.turned) {
        if (m_action.completed)
            return true;
        if (m_action.stance != 0) {
            // (As the original: the stance is compared with the cover stance's flags.)
            if (m_action.stance != (m_coverStance & m_moveFlags))
                SetStance(static_cast<int>(m_action.stance));
            m_action.stance = 0;
        }
        if (m_flags & FLAG_THROW)
            m_flags &= ~FLAG_THROW;
        if (m_flags & FLAG_LAUNCHER)
            m_flags &= ~FLAG_LAUNCHER;
        const bool goesOn = attack || vehicleAttack || follow;
        if (goesOn) {
            if (IsActorDead(m_action.target)) {
                m_action.completed = true;
                m_action.finished = true;
            }
        } else if (!m_action.keepGoing) {
            m_action.finished = true;
            m_action.completed = true;
        } else {
            m_action.completed = true;
        }
        if (m_action.type == SQUAD_ACTION_BOUND && m_boundHops != 0) {
            // The next hop: a cover point 8 m on along the bound's direction (on the walking
            // network, within its point's radius).
            const S_vector4 from = WorldPos4Of(m_human->GetFrame());
            S_vector4 ahead;
            ahead.x = from.x + m_boundDir.x * 24.0f * 0.33333334f;
            ahead.y = m_boundDir.y * 24.0f * 0.33333334f + from.y;
            ahead.z = from.z + m_boundDir.z * 24.0f * 0.33333334f;
            ahead.w = 0.0f;                             // (a stale local in the original)
            S_vector4 hop = FindCoverPoint(ahead, m_boundDir, m_coverRadius, static_cast<uint32_t>(m_human->GetNetId()));
            C_Checkpoints *network = GetGame()->GetCheckpoints();
            const int point = network->FindNearest(XYZ(hop), 10.0f, -1.0f);
            bool hopped = false;
            if (point != CHECKPOINT_NONE) {
                const S_Checkpoint *checkpoint = network->Get(point);
                S_vector offset(hop.x - checkpoint->pos.x, 0.0f, hop.z - checkpoint->pos.z);
                const float radius = static_cast<float>(checkpoint->radius) * 0.0625f;
                // (The height of the hop is the offset's until it is the point's below.)
                hop.y = 0.0f;
                if (radius * radius < offset.z * offset.z + offset.x * offset.x) {
                    offset.Normalize();
                    hop.x = offset.x * radius + checkpoint->pos.x;
                    hop.y = offset.y * radius;
                    hop.z = offset.z * radius + checkpoint->pos.z;
                }
                // Far enough (2 m, in the xz plane) to be worth the hop.
                const float ex = from.x - hop.x;
                const float ey = -hop.y;
                const float ez = from.z - hop.z;
                hop.y = checkpoint->pos.y;
                if (!(ez * ez + ey * ey + ex * ex < 4.0f)) {
                    m_action.Set(nullptr, SQUAD_ACTION_BOUND, hop, m_action.dir, true, m_action.radius);
                    // The members go one after another.
                    const int alive = g_teamManager.GetPlayerTeam()->GetAliveCount();
                    const int others = alive - 2;
                    int delay = others == -1 ? 0 : (m_human->GetNetId() % (others + 1)) * 800;
                    if (m_boundHops != 3)
                        delay += others * 500;
                    m_action.delay = delay;
                    m_action.noCommand = true;
                    --m_boundHops;
                    if (delay != 0) {
                        m_facing->SetView(m_boundDir);
                        m_facing->SetFacing(m_boundDir);
                        if (m_alertTime < delay)
                            m_alertTime = delay;
                        CoverStance(true);
                        RaiseWeapon();
                    }
                    hopped = true;
                }
            }
            if (!hopped)
                m_boundHops = 0;
        }
        if (m_action.completed)
            FinishAction(m_action.type);
        return true;
    }

    // 4. Turning toward the target (or the direction).
    S_vector dir;
    if (m_action.useDir) {
        dir.Set(m_action.dir.x, 0.0f, m_action.dir.z);
        m_holdDir = m_action.dir;
    } else if (m_action.target) {
        const S_vector4 &to = m_action.target->GetPosition4();
        const S_vector4 &own = m_human->GetPosition4();
        dir.Set(to.x - own.x, 0.0f, to.z - own.z);
    } else {
        const S_vector4 &own = m_human->GetPosition4();
        dir.Set(m_action.pos.x - own.x, 0.0f, m_action.pos.z - own.z);
    }
    dir.Normalize();
    const S_vector4 dir4 = {dir.x, dir.y, dir.z, 0.0f};   // (w: a stale local in the original)
    m_facing->SetView(dir4);
    m_facing->SetFacing(dir4);
    const S_vector4 frameDir = FrameDir4(m_human->GetFrame());
    S_vector body(frameDir.x, 0.0f, frameDir.z);
    body.Normalize();
    const float dot = body.y * dir.y + dir.z * body.z + body.x * dir.x;
    const bool climbing = m_human->m_body.GetClimbState() != 0 && !(m_move->flags & S_AIMoveOrder::FLAG_ROUTE);
    if ((dot <= TURNED_COS || climbing) && !m_action.skipTurn)
        return true;
    m_action.turned = true;
    m_action.delay = 1500;
    if (m_flags & FLAG_LAUNCHER)
        return true;
    if (m_flags & FLAG_THROW) {
        // A grenade at the place (not within 6 m).
        const S_vector4 &own = m_human->GetPosition4();
        const float dx = m_action.pos.x - own.x;
        const float dy = m_action.pos.y - own.y;
        const float dz = m_action.pos.z - own.z;
        if (dx * dx + dy * dy + dz * dz <= 36.0f) {
            m_action.completed = true;
            SayReply(SENTENCE_CANNOT, true, 300, false);
            return true;
        }
        m_fireOrder->SetThrow(m_action.pos);
        m_action.delay = S_HumanBody::GetCarryAnimLength(ANIM_THROW, (~m_moveFlags >> 2) & 1, 0);
        return true;
    }

    // 5. The command. A vehicle's seat: the asked one, else the highest free one.
    int seat = 0;
    C_Actor *target = m_action.target;
    if (target
        && (target->GetType() == ACTOR_CAR || target->GetType() == ACTOR_SUBMARINE || target->GetType() == ACTOR_STATIC_WEAPON)
        && m_action.type == HUMAN_COMMAND_ENTER_VEHICLE) {
        if (target->IsSeatBlocked(m_action.slot)) {
            int count = target->GetSeatCount();
            bool found = false;
            while (count != 0) {
                --count;
                if (!target->IsSeatBlocked(count)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_action.keepGoing = false;
                SayReply(SENTENCE_CANNOT, true, 300, false);
                return true;
            }
            seat = count;
        } else {
            seat = m_action.slot;
        }
    }
    if (m_action.noCommand) {
        // Only the walk: done (an order's walk of the module tells it).
        if (m_action.type != SQUAD_ACTION_FOLLOW) {
            m_action.delay = 0;
            if (m_action.notifyModule) {
                GetSquadCommander()->OnOrderMoveDone();
                return true;
            }
        }
        return true;
    }
    ShowActionIcon(m_action.type);
    if (!IsActionNeeded()) {
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return true;
    }

    bool enterVehicle = false;
    switch (m_action.type) {
    case HUMAN_COMMAND_ENTER_VEHICLE:
        enterVehicle = true;
        break;
    case HUMAN_COMMAND_PICK_UP_BODY:
        if (target) {
            if (target->GetType() == ACTOR_ITEM) {
                m_human->RequestPickUpItem(target);
                return false;
            }
            if (target->GetType() == ACTOR_SUPPLY_BOX) {
                m_human->RequestTakeFromBox(target, m_action.slot);
                return false;
            }
            if (target->GetType() == ACTOR_HUMAN && !(m_moveFlags & MOVE_STANDING)) {
                SetStance(MOVE_STANDING);
                m_action.delay = 1500;
                PickUp(m_action.target, m_action.slot);
                return true;
            }
        }
        enterVehicle = true;
        break;
    case SQUAD_ACTION_SEARCH_BODY:
        if (SearchBodyItems(static_cast<C_Human *>(m_action.target))) {
            m_action.delay = 1000;
            return true;
        }
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return true;
    case SQUAD_ACTION_SWAP_UNIFORM: {
        // The uniforms change hands, then the helmets.
        C_Human *other = static_cast<C_Human *>(m_action.target);
        C_Item *theirs = other->GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem();
        C_Item *mine = m_human->GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem();
        const bool swapped = m_human->RequestTransferItem(mine, ITEM_CONTAINER_UNIFORM, other, theirs, ITEM_CONTAINER_UNIFORM);
        if (swapped) {
            m_human->PutOnUniform(m_human->GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem()->GetUid());
            other->PutOnUniform(other->GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem()->GetUid());
        }
        C_Item *theirHead = other->GetContainer(ITEM_CONTAINER_HEAD)->GetItem();
        C_Item *myHead = m_human->GetContainer(ITEM_CONTAINER_HEAD)->GetItem();
        if (theirHead) {
            const bool moved = myHead
                                   ? m_human->RequestTransferItem(myHead, ITEM_CONTAINER_HEAD, other, theirHead, ITEM_CONTAINER_HEAD)
                                   : m_human->RequestMoveItem(theirHead, other, ITEM_CONTAINER_HEAD, ITEM_CONTAINER_HEAD, 0);
            if (!moved) {
                SayReply(SENTENCE_CANNOT, true, 300, false);
                return true;
            }
        }
        if (swapped)
            return true;
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return true;
    }
    case SQUAD_ACTION_21: {
        const bool a = TakeItems(m_action.target, ITEM_BEHAVIOUR_RESPAWNING);
        const bool b = TakeItems(m_action.target, ITEM_BEHAVIOUR_KEY);
        const bool c = TakeItems(m_action.target, ITEM_BEHAVIOUR_MISSION_EXPLOSIVE);
        if (a || b || c)
            return true;
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return true;
    }
    default:
        break;
    }

    if (enterVehicle && target && target->GetType() == ACTOR_CAR && m_human->m_vehicle != target) {
        // Into a car: to the entry of the seat (or another one) first.
        C_Car *car = static_cast<C_Car *>(target);
        S_vector4 entry;
        S_vector4 entryDir;
        bool found = car->GetSeatEntry(m_action.slot, &entry, &entryDir);
        for (int i = 0; !found && i < car->GetEntryCount(); ++i) {
            if (i != m_action.slot && car->GetSeatEntry(i, &entry, &entryDir))
                found = true;
        }
        bool atEntry = false;
        if (found) {
            const float distance = (XYZ(entry) - m_human->GetFrame()->GetWorldPos()).Magnitude();
            if (distance > 4.5f) {
                if (distance <= 20.0f) {
                    m_action.delay = 1000;
                    EnterVehicle(car, m_action.slot + 8, entry);
                    return true;
                }
            } else {
                atEntry = true;
            }
        }
        if (!atEntry) {
            // No way in: the member waits at its first order's place (the queue cut to two)
            // and follows again.
            if (!m_orders.empty()) {
                QueueWatchOrder(OrderPos4(m_orders[0]), m_human->GetDirection4(), m_orders[0]);
                while (m_orders.size() > 2)
                    RemoveOrder(0, true);
            }
            DropAction();
            SetFollow(true, false);
            SayReply(SENTENCE_CANNOT, true, 300, false);
            SetStatusIcon(0x12);
            return true;
        }
        ShowActionIcon(seat + 8);
    }

    // The command; its interaction's length is the wait.
    m_action.delay = 1500;
    C_HumanController::IssueCommand(MakeCommand(m_action.type, m_action.targetId, seat));
    if (m_interaction != -1)
        m_action.delay = m_interactionTime;
    return true;
}

bool C_SquadController::IsActionNeeded()
{
    C_Actor *target = m_action.target;
    switch (m_action.type) {
    case HUMAN_COMMAND_LOCK_DOOR:
        return !static_cast<C_Door *>(target)->IsLocked();
    case HUMAN_COMMAND_UNLOCK_DOOR:
        return static_cast<C_Door *>(target)->IsLocked();
    case HUMAN_COMMAND_PLANT_EXPLOSIVE:
        return static_cast<C_ExplosivePlace *>(target)->GetState() == 0;
    case HUMAN_COMMAND_HEAL:
        return !IsActorDead(target);
    case HUMAN_COMMAND_CUT_WIRE:
        return !static_cast<C_WireCut *>(target)->IsCut();
    case HUMAN_COMMAND_TIE_UP: {
        bool surrendered = false;
        if (target->GetType() == ACTOR_HUMAN) {
            C_Human *human = static_cast<C_Human *>(target);
            surrendered = !human->IsUnconscious() && human->IsSurrendered();
        }
        return !IsActorDead(target) && surrendered;
    }
    case SQUAD_ACTION_ATTACK_VEHICLE:
        // (The car's side is asked and not used.)
        if (!m_human->m_vehicle && target->GetType() == ACTOR_CAR)
            static_cast<C_Car *>(target)->GetCrewSide();
        return false;
    case HUMAN_COMMAND_OPEN_DOOR: {
        const int state = static_cast<C_Door *>(target)->GetState();
        return state != 1 && state != 2;
    }
    case HUMAN_COMMAND_CLOSE_DOOR: {
        const int state = static_cast<C_Door *>(target)->GetState();
        return state != 0 && state != 3;
    }
    case HUMAN_COMMAND_DELIVER_DOCUMENTS:
        return static_cast<C_DocumentPlace *>(target)->GetState() == 0;
    default:
        return true;
    }
}

bool C_SquadController::IsAtTarget(C_Actor *actor)
{
    if (!actor)
        return false;
    if (actor->GetType() == ACTOR_HUMAN) {
        // Within 1.5 m of the other.
        const S_vector &own = m_human->GetFrame()->GetWorldPos();
        const S_vector &other = actor->GetFrame()->GetWorldPos();
        const float dx = other.x - own.x;
        const float dy = other.y - own.y;
        const float dz = other.z - own.z;
        return dx * dx + dy * dy + dz * dz < 2.25f;
    }
    I3D_frame *frame;
    if (actor->GetType() == ACTOR_CAR) {
        frame = actor->GetFrame()->FindChildFrame("body", 0xFFFF);
    } else if (actor->GetType() == ACTOR_DOOR) {
        // A door: the walking point the member is at is the door's.
        if (!m_currentCheckpoint)
            return false;
        const int index = m_checkpoints->GetIndex(m_currentCheckpoint);
        if (index >= m_checkpoints->GetCount() || index < 0 || static_cast<uint16_t>(index) == 0xFFFF)
            return false;
        return m_checkpoints->GetActor(static_cast<uint16_t>(index)) == actor;
    } else {
        frame = actor->GetFrame();
    }
    if (!frame)
        return false;
    // Within 1.5 m of the frame's box, in the frame's space (a dummy's own box, else the
    // hierarchy's).
    const S_vector own = m_human->GetFrame()->GetWorldPos();
    S_matrix inverse;
    inverse.Inverse(frame->GetMatrix());
    const S_vector local = own * inverse;
    I3D_bbox box;
    if (frame->GetType() == FRAME_DUMMY) {
        box = static_cast<I3D_dummy *>(frame)->GetBBox();
    } else {
        I3D_bound_volume bound;
        box = I3DGetDriver()->GetHierarchyBound(bound, frame)->bbox;
    }
    float distance2;
    if (box.min.x <= local.x)
        distance2 = local.x <= box.max.x ? 0.0f : (local.x - box.max.x) * (local.x - box.max.x);
    else
        distance2 = (local.x - box.min.x) * (local.x - box.min.x);
    if (local.y < box.min.y)
        distance2 = (local.y - box.min.y) * (local.y - box.min.y) + distance2;
    else if (box.max.y < local.y)
        distance2 = (local.y - box.max.y) * (local.y - box.max.y) + distance2;
    if (box.min.z <= local.z) {
        if (box.max.z < local.z)
            distance2 = (local.z - box.max.z) * (local.z - box.max.z) + distance2;
    } else {
        distance2 = (local.z - box.min.z) * (local.z - box.min.z) + distance2;
    }
    return distance2 < 2.25f;
}

void C_SquadController::ShowActionIcon(int type)
{
    // The mode icon of each command (temporary, 2 s).
    switch (type) {
    case 0: SetModeIcon(0xF); break;
    case 1: SetModeIcon(0x10); break;
    case 2:
    case 0x26: SetModeIcon(0x11); break;
    case 3: SetModeIcon(0x12); break;
    case 4: SetModeIcon(0x13); break;
    case 5: SetModeIcon(0x14); break;
    case 6:
    case 0x21:
    case 0x22: SetModeIcon(0x15); break;
    case 8: SetModeIcon(0x16); break;
    case 9: SetModeIcon(0x17); break;
    case 10: SetModeIcon(0x18); break;
    case 0xB: SetModeIcon(0x19); break;
    case 0xC: SetModeIcon(0x1A); break;
    case 0xD: SetModeIcon(0x1B); break;
    case 0xE: SetModeIcon(0x1C); break;
    case 0xF: SetModeIcon(0x1D); break;
    case 0x10: SetModeIcon(0x1E); break;
    case 0x12: SetModeIcon(0x20); break;
    case 0x13: SetModeIcon(0x21); break;
    case 0x15: SetModeIcon(0x22); break;
    case 0x16: SetModeIcon(0x23); break;
    case 0x17: SetModeIcon(0x24); break;
    case 0x18: SetModeIcon(0x25); break;
    case 0x1B: SetModeIcon(0x26); break;
    case 0x1C: SetModeIcon(0x27); break;
    default: break;
    }
}

// ------------------------------------------------------------------------- the order queue

bool C_SquadController::ProcessOrders(int timeDelta)
{
    if (m_orders.empty()) {
        m_orderStance = 0;
        m_orderPace = 0;
        return false;
    }
    if (m_restartOrder) {
        m_orders[0]->started = false;
        m_restartOrder = false;
    }
    S_HumanOrder *first = m_orders[0];
    StartOrder(timeDelta);
    // (A follow order empties the queue; the original goes on with the order just released,
    // still in the queue's first slot.)
    S_HumanOrder *order = m_orders.empty() ? first : m_orders[0];
    if (!order->started) {
        SetOrderTask(order);
        order->started = true;
    }
    // Fix: the original removes an order of the empty queue (and loops over the memory).
    if (order->done && !m_orders.empty()) {
        // The last order done: the member watches on from where it ended (a move order along
        // its direction, else the way the body faces).
        if (m_orders.size() == 1 && order->kind != HUMAN_ORDER_FOLLOW) {
            const S_vector4 dir = order->kind == HUMAN_ORDER_MOVE ? order->data : FrameDir4(m_human->GetFrame());
            QueueWatchOrder(WorldPos4Of(m_human->GetFrame()), dir, m_orders[0]);
        }
        RemoveOrder(0, false);
    }
    return true;
}

void C_SquadController::StartOrder(int timeDelta)
{
    S_HumanOrder *order = m_orders[0];
    const int kind = order->kind;
    if (order->started) {
        if (kind != HUMAN_ORDER_WATCH && (kind < HUMAN_ORDER_6 || kind > HUMAN_ORDER_7)) {
            // The action of the order; done when it is.
            if (ExecuteAction(timeDelta) && !m_action.completed)
                return;
            order->done = true;
            return;
        }
        // Watching (when calm).
        if (m_eventTime != 0 || m_alertTime != 0)
            return;
        m_facing->SetView(order->data);
        m_facing->SetFacing(order->data);
        m_holdDir = order->data;
        return;
    }
    m_restartOrder = false;
    bool setFlags = true;
    switch (kind) {
    case HUMAN_ORDER_MOVE:
    case HUMAN_ORDER_WATCH:
        m_action.finished = true;
        m_action.Set(nullptr, SQUAD_ACTION_ORDER_MOVE, OrderPos4(order), order->data, true, 0.5f);
        m_action.noCommand = true;
        m_action.turned = true;
        m_flags &= ~FLAG_1;
        SetStatusIcon(kind != HUMAN_ORDER_MOVE ? 11 : 10);
        break;
    case HUMAN_ORDER_ACT:
        GetSquadCommander()->ExecuteCommand(order->arg, order->actor, m_human, false, order->unknown70);
        break;
    case HUMAN_ORDER_FOLLOW:
        FollowSelected(true);
        setFlags = false;
        break;
    case HUMAN_ORDER_6:
    case HUMAN_ORDER_7:
        m_action.finished = true;
        SetStatusIcon(kind != HUMAN_ORDER_6 ? 0xE : 0xD);
        break;
    default:
        break;
    }
    if (!setFlags)
        return;
    // The order's stance, pace and AI mode (the defaults go into the order).
    if (!(order->flags & MOVE_STANCE_MASK))
        order->flags |= MOVE_STANDING;
    m_orderStance = order->flags & MOVE_STANCE_MASK;
    if (!(order->flags & MOVE_PACE_MASK))
        order->flags |= MOVE_PACE_4;
    m_orderPace = order->flags & MOVE_PACE_MASK;
    if (!(order->flags & MOVE_AI_MODE_MASK))
        order->flags |= MOVE_AI_DEFENSIVE;
    const uint32_t flags = order->flags;
    m_moveFlags = (m_moveFlags & 0xFFFE08FFu) | (flags & MOVE_AI_MODE_MASK);
    m_driver->SetPace(m_orderPace);
    SetModeIcon((flags & MOVE_AI_AGGRESSIVE) ? 0xB : (flags & MOVE_AI_DEFENSIVE) ? 0xA : 9);
    if (m_orderStance == (m_moveFlags & m_orderStance))
        return;
    SetStatusIcon((m_orderStance & MOVE_LYING) ? 0x10 : (m_orderStance & MOVE_CROUCHING) ? 0x11 : 0x12);
    SetStance(static_cast<int>(m_orderStance));
}

void C_SquadController::SetOrderTask(S_HumanOrder *order)
{
    ReleaseTask();
    m_flags &= ~FLAG_2;
    switch (order->kind) {
    case HUMAN_ORDER_MOVE: {
        const S_vector4 pos = OrderPos4(order);
        m_task = AllocTask7(pos, order->data);
        C_Actor *vehicle = m_human->m_vehicle;
        if (vehicle && vehicle->GetType() == ACTOR_CAR && m_human->m_seat == 0) {
            // The driver drives there, arriving toward the next move order.
            m_driver->SetVehicle(vehicle);
            size_t next = 1;
            while (next < m_orders.size() && m_orders[next]->kind != HUMAN_ORDER_MOVE)
                ++next;
            if (next < m_orders.size()) {
                m_driver->DriveTo(pos, OrderPos4(m_orders[next]), 1.0f);
                return;
            }
            m_driver->DriveTo(pos, 1.0f);
        }
        break;
    }
    case HUMAN_ORDER_WATCH:
        // (The task keeps the order's actor as a number.)
        m_task = AllocTask12(order->data, static_cast<int>(reinterpret_cast<intptr_t>(order->actor)));
        break;
    case HUMAN_ORDER_FOLLOW:
        m_task = AllocTask8(static_cast<int>(reinterpret_cast<intptr_t>(order->actor)));
        break;
    default:
        break;
    }
}

void C_SquadController::SetIdleTask(S_HumanOrder *order)
{
    ReleaseTask();
    m_idleCheckpoint = -1;
    const S_vector4 pos = WorldPos4Of(m_human->GetFrame());
    S_vector4 dir;
    if (!order) {
        dir = m_facing->view;
    } else {
        const S_vector4 to = OrderPos4(order);
        S_vector d(to.x - pos.x, to.y - pos.y, to.z - pos.z);
        d.Normalize();
        dir = {d.x, d.y, d.z, 0.0f};                    // (w: a stale local in the original)
    }
    m_task = AllocTask1(pos, static_cast<uint16_t>(m_idleCheckpoint), 15.0f, dir);
}

void C_SquadController::QueueWatchOrder(const S_vector4 &pos, const S_vector4 &dir, S_HumanOrder *model)
{
    uint32_t flags;
    int arg;
    if (!model) {
        flags = m_moveFlags;
        arg = -1;
    } else {
        if (!(model->flags & MOVE_AI_MODE_MASK))
            model->flags |= MOVE_AI_DEFENSIVE;
        if (!(model->flags & MOVE_STANCE_MASK))
            model->flags |= MOVE_STANDING;
        if (!(model->flags & MOVE_PACE_MASK))
            model->flags |= MOVE_PACE_4;
        arg = model->arg;
        flags = model->flags;
    }
    m_orders.push_back(AllocHumanOrder(m_human, HUMAN_ORDER_WATCH, arg, pos, dir, flags & MOVE_PACE_MASK,
                                       flags & MOVE_STANCE_MASK, flags & MOVE_AI_MODE_MASK, nullptr, 0));
}

void C_SquadController::EndOrderKind6()
{
    if (!m_orders.empty() && m_orders[0]->kind == HUMAN_ORDER_6)
        m_orders[0]->done = true;
}

void C_SquadController::EndOrderKind7()
{
    if (!m_orders.empty() && m_orders[0]->kind == HUMAN_ORDER_7)
        m_orders[0]->done = true;
}

void C_SquadController::HoldOrders(bool on)
{
    m_ordersHeld = on;
    if (m_orders.empty() || m_action.finished || m_action.arrived)
        return;
    if (!on)
        m_move->SetPos(m_action.pos);
    else if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE)
        m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
}

// ------------------------------------------------------------------------- following and holding

void C_SquadController::DropAction()
{
    if (!m_action.finished) {
        if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE)
            m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
        if (m_human->m_vehicle && m_human->m_seat == 0)
            m_driver->Stop();
        if (m_flags & FLAG_1)
            m_flags &= ~FLAG_1;
        // (As the original: the flags are replaced by the throw's / the launcher's bit when it
        // is set, probably meant to clear it; no action has type 0x200.)
        if (m_action.type == SQUAD_ACTION_THROW) {
            if (m_flags & FLAG_THROW)
                m_flags = FLAG_THROW;
        } else if (m_action.type == 0x200) {
            if (m_flags & FLAG_LAUNCHER)
                m_flags = FLAG_LAUNCHER;
        }
        m_action.finished = true;
        m_action.completed = true;
    }
    m_moveFlags &= ~MOVE_AI_MODE_MASK;
    FollowSelectedMember();
}

void C_SquadController::FollowSelected(bool fromOrders)
{
    C_Human *leader = g_teamManager.GetPlayerTeam()->GetSelectedMember();
    bool setLeader = true;
    if (!fromOrders) {
        if (!m_action.finished
            && (m_action.type == SQUAD_ACTION_BOUND || m_action.type == SQUAD_ACTION_MOVE_NEAR
                || m_action.type == SQUAD_ACTION_MOVE_TO))
            DropAction();
    } else if (m_orders.empty()) {
        setLeader = false;
    } else {
        CancelOrders(false);
        DropAction();
        UpdateModeIcon();
        SetStatusIcon(m_quiet ? 1 : 0);
        SetStatusIcon(0x13);
    }
    if (setLeader) {
        m_human->SetLeader(leader);
        // The member's slot in the formation is planned again.
        if (C_Human *newLeader = m_human->GetLeader()) {
            if (C_FormationShape *formation = newLeader->GetFormation())
                formation->InvalidateSlot(m_human);
        }
    }
    SetFollow(leader != nullptr, m_quiet);
    NotifyIfIdle();
}

void C_SquadController::SetFollow(bool follow, bool quiet)
{
    const bool wasFollowing = m_following;
    m_quiet = quiet;
    m_following = follow;
    if (!follow) {
        m_followDelay = 0;
        m_holdDir = Dir4(m_human->GetFacingDir());
        SetStatusIcon(m_quiet ? 3 : 2);
        return;
    }
    // Walks and attacks (and getting into a car without orders) end.
    if (!m_action.finished) {
        switch (m_action.type) {
        case HUMAN_COMMAND_ENTER_VEHICLE:
            if (m_orders.empty() && m_action.target
                && (m_action.target->GetType() == ACTOR_CAR || m_action.target->GetType() == ACTOR_STATIC_WEAPON))
                DropAction();
            break;
        case SQUAD_ACTION_ATTACK_VEHICLE:
        case SQUAD_ACTION_ATTACK:
        case SQUAD_ACTION_ATTACK_SILENTLY:
        case SQUAD_ACTION_BOUND:
        case SQUAD_ACTION_MOVE_TO:
        case SQUAD_ACTION_MOVE_NEAR:
            DropAction();
            break;
        default:
            break;
        }
    }
    SetStatusIcon(m_quiet ? 1 : 0);
    m_flags &= ~(FLAG_1 | FLAG_HOLDING);
    // Starting to follow again while idle, the member waits 4.5 s.
    if (m_action.finished && m_orders.empty() && !quiet && wasFollowing)
        m_followDelay = 4500;
    // Quiet: sneaking; else walking, running when 10 m or more behind.
    m_stateFlags = MOVE_PACE_3;
    if (C_Human *leader = m_human->GetLeader()) {
        if (m_quiet) {
            m_stateFlags = MOVE_PACE_1;
        } else {
            const S_vector4 &to = leader->GetPosition4();
            const S_vector4 &from = m_human->GetPosition4();
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float dz = to.z - from.z;
            if (100.0f <= dx * dx + dy * dy + dz * dz)
                m_stateFlags = MOVE_PACE_4;
        }
    }
    NotifyIfIdle();
}

void C_SquadController::UpdateFollowPace()
{
    if (!m_following)
        return;
    SetStatusIcon(m_quiet ? 1 : 0);
    m_flags &= ~(FLAG_1 | FLAG_HOLDING);
    C_Human *leader = m_human->GetLeader();
    if (!leader)
        return;
    if (m_quiet) {
        m_stateFlags = MOVE_PACE_1;
        return;
    }
    const S_vector4 &to = leader->GetPosition4();
    const S_vector4 &from = m_human->GetPosition4();
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float dz = to.z - from.z;
    m_stateFlags = dx * dx + dy * dy + dz * dz < 100.0f ? MOVE_PACE_3 : MOVE_PACE_4;
}

void C_SquadController::HoldPosition(bool quiet, bool notify)
{
    if (m_flags & FLAG_HOLDING)
        return;
    m_flags = (m_flags & ~FLAG_1) | FLAG_HOLDING;
    if (!m_action.finished
        && (m_action.type == SQUAD_ACTION_BOUND || m_action.type == SQUAD_ACTION_MOVE_NEAR
            || m_action.type == SQUAD_ACTION_MOVE_TO))
        DropAction();
    ChooseCoverFacing(&m_holdDir, &m_coverStance);
    if (m_following)
        SetFollow(false, quiet);
    SetStatusIcon(m_quiet ? 7 : 6);
    // (The cover's direction is replaced by the facing at once.)
    m_holdDir = Dir4(m_human->GetFacingDir());
    SetStance(static_cast<int>(m_coverStance));
    if (m_human->m_vehicle && m_human->m_seat == 0)
        m_driver->Stop();
    if (notify)
        NotifyIfIdle();
}

void C_SquadController::FinishAction(int type)
{
    m_action.finished = true;
    if (!m_orders.empty())
        return;
    switch (type) {
    case HUMAN_COMMAND_ENTER_VEHICLE:
        if (m_action.target
            && (m_action.target->GetType() == ACTOR_CAR || m_action.target->GetType() == ACTOR_STATIC_WEAPON
                || m_action.target->GetType() == ACTOR_SUBMARINE)) {
            SetFollow(false, m_quiet);
            break;
        }
        UpdateFollowPace();
        break;
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case HUMAN_COMMAND_USE_LADDER:
        SetFollow(false, m_quiet);
        break;
    case SQUAD_ACTION_FOLLOW:
    case SQUAD_ACTION_BOUND:
    case SQUAD_ACTION_MOVE_NEAR: {
        // At the end of a bound or a walk: the stance of the leader, or of the cover here.
        m_eventTime = 0;
        ReactToStimuli();
        m_holdDir = m_facing->facing;
        C_Human *leader = m_human->GetLeader();
        if (!leader)
            leader = g_teamManager.GetPlayerTeam()->GetSelectedMember();
        bool cover = false;
        if (leader) {
            const uint32_t stance = leader->GetStanceFlags();
            if ((stance & MOVE_LYING) && m_quietTime > 5000)
                m_coverStance = MOVE_LYING;
            else if (stance & MOVE_CROUCHING)
                m_coverStance = MOVE_CROUCHING;
            else if (m_currentCheckpoint)
                cover = true;
        } else if (m_currentCheckpoint) {
            cover = true;
        }
        if (cover) {
            const uint8_t bit = S_Checkpoint::GetDirectionBit(XYZ(m_holdDir));
            if (m_currentCheckpoint->cover4 & bit)
                m_coverStance = MOVE_STANDING;
            else if (m_currentCheckpoint->cover2 & bit)
                m_coverStance = MOVE_CROUCHING;
            else
                m_coverStance = MOVE_LYING;
        }
        SetStance(static_cast<int>(m_coverStance));
        HoldPosition(m_quiet, false);
        break;
    }
    case SQUAD_ACTION_MOVE_TO:
        HoldPosition(m_quiet, false);
        break;
    default:
        UpdateFollowPace();
        break;
    }
    GetGame()->Print("%s - Action finished!", m_human->GetLastName());
}

void C_SquadController::AdjustStanceForThreat()
{
    // An enemy in sight (or seen by a member near) keeps the member down.
    bool threat = !m_human->m_seenEnemies.empty();
    if (!threat)
        threat = g_teamManager.GetPlayerTeam()->GetEnemySeenNearby(m_human) != nullptr;
    const uint32_t flags = m_moveFlags;
    int stance = 0;
    if (flags & MOVE_LYING) {
        if (threat && (m_flags & (FLAG_2 | FLAG_HOLD_FIRE)) && !m_fireOrder->target
            && !(FireDirSquare(m_fireOrder->fireDir) >= 1e-16f))
            stance = MOVE_LYING;
        else
            stance = (m_alertTime > 999 || threat) ? MOVE_CROUCHING : MOVE_STANDING;
    } else if (flags & MOVE_CROUCHING) {
        if (!(m_alertTime > 999 || threat))
            stance = MOVE_STANDING;
    } else if (flags & MOVE_STANDING) {
        if (!(m_alertTime < 1001 && !threat))
            stance = MOVE_CROUCHING;
    }
    if (stance != 0)
        SetStance(stance);
    m_stateFlags = m_quiet ? MOVE_PACE_1 : MOVE_PACE_4;
}

void C_SquadController::Engage(bool on, const S_vector4 &pos)
{
    if (on) {
        SetCombatOrder(SQUAD_ORDER_MODE1);
        m_flags = (m_flags & ~FLAG_HOLD_FIRE) | FLAG_ENGAGED;
        m_engageTime = static_cast<int>(RandFloat() * 2000.0f + 8000.0f);
        m_alertTime = m_engageTime + 1000;
        m_engagePos = pos;
        m_facing->SetLookAround(false, 4000);
        LookAround(0);
        SetModeIcon(6);
        return;
    }
    m_flags &= ~FLAG_ENGAGED;
    m_fireOrder->SetTarget(nullptr);
    UpdateModeIcon();
}

void C_SquadController::SetCombatOrder(int order)
{
    bool firing = false;
    bool coverStance = true;
    switch (order) {
    case SQUAD_ORDER_MODE0:
        m_silentMode = false;
        m_combatMode = 0;
        Engage(false, m_human->GetPosition4());
        firing = true;
        break;
    case SQUAD_ORDER_MODE0_ALT:
        m_silentMode = true;
        m_combatMode = 0;
        Engage(false, m_human->GetPosition4());
        // Fix: the original reads the hands item's type without one.
        firing = m_handsItem && IsSilencedType(m_handsItem->GetDef()->GetId());
        break;
    case SQUAD_ORDER_MODE2:
    case SQUAD_ORDER_MODE2_ALT:
        m_flags |= FLAG_HOLD_FIRE;
        m_combatMode = 2;
        Engage(false, m_human->GetPosition4());
        firing = false;
        break;
    case SQUAD_ORDER_MODE1:
        m_silentMode = false;
        m_flags &= ~FLAG_HOLD_FIRE;
        m_combatMode = 1;
        SetModeIcon(4);
        firing = true;
        break;
    case SQUAD_ORDER_MODE1_ALT:
        m_flags &= ~FLAG_HOLD_FIRE;
        m_silentMode = true;
        m_combatMode = 1;
        SetModeIcon(5);
        if (!m_handsItem)
            coverStance = false;
        else
            firing = IsSilencedType(m_handsItem->GetDef()->GetId());
        break;
    default:
        coverStance = false;
        break;
    }
    if (coverStance)
        CoverStance(firing);
    // A weapon to suit the order (silent: a silenced one).
    if (!m_silentMode || !m_handsItem || !IsSilencedType(m_handsItem->GetDef()->GetId()))
        UpdateWeaponChoice();
}

// ------------------------------------------------------------------------- the squad commands

void C_SquadController::Act21(C_Actor *target)
{
    ActOn(SQUAD_ACTION_21, target, false, 1.5f, true);
}

void C_SquadController::DeliverDocuments(C_Actor *place)
{
    if (place->GetType() == ACTOR_DOCUMENT_PLACE) {
        C_DocumentPlace *documents = static_cast<C_DocumentPlace *>(place);
        if (m_human->FindItem(documents->GetItemId()) && documents->GetState() == 0) {
            ActOn(HUMAN_COMMAND_DELIVER_DOCUMENTS, place, false, 1.5f, true);
            return;
        }
    }
    SayReply(SENTENCE_CANNOT, true, 300, false);
}

void C_SquadController::Follow(C_Human *leader)
{
    m_human->SetLeader(leader);
    SetFollow(true, false);
    m_action.Set(leader, SQUAD_ACTION_FOLLOW, m_human->GetPosition4(), m_human->GetDirection4(), false, 0.5f);
    m_action.skipTurn = true;
    m_action.arrived = true;
    m_action.noCommand = true;
    NotifyIfIdle();
}

void C_SquadController::EnterVehicle(C_Actor *vehicle, int seat, const S_vector4 &pos)
{
    // (`seat` comes as its icon, the seat + 8.)
    bool checkSeat = false;
    if (vehicle->GetType() == ACTOR_CAR) {
        if (!static_cast<C_Car *>(vehicle)->CanEnter()) {
            SayReply(SENTENCE_CANNOT, true, 300, false);
            return;
        }
        checkSeat = true;
    } else if (vehicle->GetType() == ACTOR_STATIC_WEAPON) {
        checkSeat = true;
    }
    if (checkSeat && vehicle->GetSeatOccupant(seat - 8)) {
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return;
    }
    m_action.Set(vehicle, HUMAN_COMMAND_ENTER_VEHICLE, pos, m_human->GetDirection4(), true, 2.5f);
    m_action.slot = seat - 8;
    DropCarriedBody();
    NotifyIfIdle();
}

void C_SquadController::PickUp(C_Actor *actor, int item)
{
    // An item that fits, a body (not while carrying one), or a supply box; out of a vehicle
    // first.
    const int type = actor->GetType();
    bool ok = false;
    if (type == ACTOR_ITEM && g_itemsModule.CanPutItem(m_human, static_cast<C_ItemActor *>(actor)->GetItem()) > 0)
        ok = true;
    else if (type == ACTOR_HUMAN && !m_human->m_carried)
        ok = true;
    else if (type == ACTOR_SUPPLY_BOX)
        ok = true;
    if (!ok) {
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return;
    }
    ActOn(HUMAN_COMMAND_PICK_UP_BODY, actor, false, 1.0f, true);
    if (m_human->m_vehicle) {
        C_HumanController::IssueCommand(MakeCommand(HUMAN_COMMAND_LEAVE_VEHICLE, m_human->GetNetId(), m_human->m_seat));
        m_action.delay = 1000;
    }
    if (type == ACTOR_SUPPLY_BOX)
        m_action.slot = item;
}

void C_SquadController::LockDoor(C_Actor *door)
{
    ActOn(HUMAN_COMMAND_LOCK_DOOR, door, false, 1.5f, true);
}

void C_SquadController::UnlockDoor(C_Actor *door)
{
    ActOn(HUMAN_COMMAND_UNLOCK_DOOR, door, false, 1.5f, true);
}

void C_SquadController::OpenDoor(C_Actor *actor)
{
    if (actor->GetType() == ACTOR_DOOR) {
        // A locked door needs its key.
        C_Door *door = static_cast<C_Door *>(actor);
        if (!door->IsLocked() || door->CanPickLock() || m_human->FindItem(door->GetLockKey())) {
            ActOn(HUMAN_COMMAND_OPEN_DOOR, actor, false, 1.5f, true);
            return;
        }
    }
    SayReply(SENTENCE_CANNOT, true, 300, false);
}

void C_SquadController::CloseDoor(C_Actor *door)
{
    ActOn(HUMAN_COMMAND_CLOSE_DOOR, door, false, 1.5f, true);
}

void C_SquadController::UseActor(C_Actor *actor)
{
    // The distance to use it from (the vehicles: getting in goes on).
    float radius = 1.5f;
    bool keepGoing = false;
    switch (actor->GetType()) {
    case ACTOR_CAR:
    case ACTOR_LADDER:
    case ACTOR_STATIC_WEAPON:
        radius = 2.5f;
        keepGoing = true;
        break;
    case ACTOR_SUBMARINE:
        radius = 3.5f;
        keepGoing = true;
        break;
    default:
        break;
    }
    ActOn(HUMAN_COMMAND_ENTER_VEHICLE, actor, keepGoing, radius, true);
    DropCarriedBody();
}

void C_SquadController::DefuseMine(C_Actor *mine)
{
    ActOn(HUMAN_COMMAND_DEFUSE_MINE, mine, false, 1.5f, true);
    DropCarriedBody();
}

void C_SquadController::DisarmExplosive(C_Actor *place)
{
    ActOn(HUMAN_COMMAND_DISARM_EXPLOSIVE, place, false, 1.5f, true);
    DropCarriedBody();
}

void C_SquadController::SearchBody(C_Actor *body)
{
    if (static_cast<C_Human *>(body)->m_stats[STAT_HEALTH] < 1) {
        ActOn(SQUAD_ACTION_SEARCH_BODY, body, false, 1.5f, true);
        return;
    }
    SayReply(SENTENCE_CANNOT, true, 300, false);
}

void C_SquadController::PlantExplosive(C_Actor *place)
{
    // An explosive place needs the explosive carried; it (or another actor) must be empty.
    bool ok = true;
    int state;
    if (place->GetType() == ACTOR_EXPLOSIVE_PLACE) {
        C_ExplosivePlace *explosive = static_cast<C_ExplosivePlace *>(place);
        ok = m_human->FindItem(explosive->GetItemId()) != nullptr;
        state = explosive->GetState();
    } else {
        // (The original reads the same field of any actor.)
        state = static_cast<C_ExplosivePlace *>(place)->GetState();
    }
    if (ok && state == 0) {
        ActOn(HUMAN_COMMAND_PLANT_EXPLOSIVE, place, false, 1.5f, true);
        DropCarriedBody();
        return;
    }
    SayReply(SENTENCE_CANNOT, true, 300, false);
}

void C_SquadController::SwapUniform(C_Actor * /*human*/)
{
    // (The original's test is inverted: a human never passes, and another actor is asked
    // IsUnconscious as if it were a human. Fix: that is not done, so the command fails.)
    SayReply(SENTENCE_CANNOT, true, 300, false);
}

void C_SquadController::CutWire(C_Actor *actor)
{
    C_WireCut *wire = static_cast<C_WireCut *>(actor);
    if (wire->IsCut()) {
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return;
    }
    ActOn(HUMAN_COMMAND_CUT_WIRE, actor, false, 0.75f, true);
    const int first = wire->GetPoint(0);
    const int second = wire->GetPoint(1);
    if (m_currentCheckpoint && first != CHECKPOINT_NONE && second != CHECKPOINT_NONE) {
        // To the nearer side of the fence along the walking network.
        C_Checkpoints *network = GetGame()->GetCheckpoints();
        int from = network->GetIndex(m_currentCheckpoint);
        if (from >= network->GetCount() || from < 0)
            from = 0xFFFF;
        std::vector<uint16_t> path;
        float toFirst = -1.0f;
        float toSecond = -1.0f;
        if (network->FindPath(from, first, &path, 0))
            toFirst = network->GetPathLength(path);
        path.clear();
        from = network->GetIndex(m_currentCheckpoint);
        if (from >= network->GetCount() || from < 0)
            from = 0xFFFF;
        if (network->FindPath(from, second, &path, 0))
            toSecond = network->GetPathLength(path);
        int side;
        if (!(toFirst <= toSecond) || toFirst < 0.0f) {
            // (As the original: without a way to the first side the second is refused too
            // when it is the longer way, i.e. always.)
            if (toFirst < toSecond || toSecond < 0.0f) {
                SayReply(SENTENCE_CANNOT, true, 300, false);
                return;
            }
            side = second;
        } else {
            side = first;
        }
        const S_Checkpoint *point = network->Get(side);
        m_action.pos.x = point->pos.x;
        m_action.pos.y = point->pos.y;
        m_action.pos.z = point->pos.z;
        // (The w: a stale local in the original.)
    }
    if (m_human->m_carried) {
        C_HumanController::IssueCommand(MakeCommand(HUMAN_COMMAND_LEAVE_VEHICLE, m_action.targetId, 0));
        m_action.delay = 1000;
    }
}

void C_SquadController::PickLock(C_Actor *door)
{
    ActOn(HUMAN_COMMAND_PICK_LOCK, door, false, 1.5f, true);
    DropCarriedBody();
}

void C_SquadController::OrderLeaveVehicle(C_Actor *vehicle)
{
    if (!m_human->m_vehicle && !m_human->m_carried) {
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return;
    }
    ActOn(HUMAN_COMMAND_LEAVE_VEHICLE, vehicle, false, 1.5f, true);
    m_action.arrived = true;
    m_action.skipTurn = true;
}

void C_SquadController::Attack(C_Actor *target)
{
    if (IsActorDead(target)) {
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return;
    }
    ActOn(SQUAD_ACTION_ATTACK, target, false, 1.0f, true);
    m_flags &= ~FLAG_HOLD_FIRE;
    m_action.noCommand = true;
    SetStatusIcon(10);
    DropCarriedBody();
}

void C_SquadController::AttackSilently(C_Actor *target)
{
    if (IsActorDead(target)) {
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return;
    }
    // A silenced weapon into the hands.
    C_Item *hands = m_human->GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
    C_Item *silenced = m_human->FindItem(ITEM_SILENCED_16);
    if (!silenced)
        silenced = m_human->FindItem(ITEM_SILENCED_7);
    if (!silenced)
        silenced = m_human->FindItem(ITEM_SILENCED_104);
    if (!silenced) {
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return;
    }
    if (silenced != hands) {
        if (!hands)
            m_human->RequestMoveItem(silenced, m_human, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS, 0);
        else
            m_human->RequestTransferItem(hands, ITEM_CONTAINER_HANDS, m_human, silenced, ITEM_CONTAINER_LONG_GUN);
    }
    ActOn(SQUAD_ACTION_ATTACK_SILENTLY, target, false, 1.0f, true);
    m_flags &= ~FLAG_HOLD_FIRE;
    m_action.noCommand = true;
    SetCombatOrder(SQUAD_ORDER_MODE1_ALT);
    SetStatusIcon(10);
    DropCarriedBody();
}

void C_SquadController::AttackVehicle(C_Actor *target)
{
    C_Actor *vehicle = m_human->m_vehicle;
    bool go = false;
    if (target != vehicle) {
        if (!vehicle) {
            // On foot: a weapon for it into the hands.
            int container;
            if (C_Item *weapon = FindWeaponFor(target, &container)) {
                Equip(weapon, &container);
                go = true;
            }
        } else if (vehicle->HasSeatWeapon(m_human->m_seat)) {
            // From the seat's weapon; an armoured target needs a projectile weapon.
            go = !IsArmoured(target) || vehicle->GetSeatWeaponDef(m_human->m_seat)->GetComponentKind() == ICK_PROJECTILE_WEAPON;
        }
    }
    if (!go) {
        m_action.finished = true;
        m_action.completed = true;
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return;
    }
    ActOn(SQUAD_ACTION_ATTACK_VEHICLE, target, false, 5.0f, true);
    m_action.noCommand = true;
    m_flags &= ~FLAG_HOLD_FIRE;
    DropCarriedBody();
}

void C_SquadController::TieUp(C_Actor *actor)
{
    if (actor->GetType() == ACTOR_HUMAN && static_cast<C_Human *>(actor)->IsSurrendered()) {
        ActOn(HUMAN_COMMAND_TIE_UP, actor, false, 1.5f, true);
        DropCarriedBody();
        return;
    }
    SayReply(SENTENCE_CANNOT, true, 300, false);
}

void C_SquadController::MoveNear(const S_vector4 &pos, bool quiet)
{
    m_boundHops = 0;
    m_action.Set(nullptr, SQUAD_ACTION_MOVE_NEAR, pos, m_human->GetDirection4(), true, 2.5f);
    m_action.noCommand = true;
    m_coverRadius = 2.5f;
    m_quiet = quiet;
    m_action.delay = 200;
    SetStatusIcon(quiet ? 5 : 4);
    NotifyIfIdle();
    AdjustStanceForThreat();
}

void C_SquadController::OrderMove(const S_vector4 &pos, int delay, bool notifyModule)
{
    m_action.Set(nullptr, SQUAD_ACTION_ORDER_MOVE, pos, m_human->GetDirection4(), true, 0.5f);
    m_action.notifyModule = notifyModule;
    m_action.delay = delay;
    m_action.noCommand = true;
    SetStatusIcon(m_quiet ? 5 : 4);
    AdjustStanceForThreat();
}

void C_SquadController::ThrowGrenadeAt(C_Actor *target)
{
    if (!m_human->FindContainerOfBehaviour(ITEM_BEHAVIOUR_THROWN, nullptr)) {
        SayReply(SENTENCE_CANNOT, true, 300, false);
        return;
    }
    ThrowAt(ActorPos4(target));
    DropCarriedBody();
}

void C_SquadController::Heal(C_Actor *actor)
{
    if (static_cast<C_Human *>(actor)->m_stats[STAT_HEALTH] > 0 && m_human->GetMedkit()) {
        ActOn(HUMAN_COMMAND_HEAL, actor, false, 1.5f, true);
        DropCarriedBody();
        return;
    }
    SayReply(SENTENCE_CANNOT, true, 300, false);
}

void C_SquadController::ThrowAt(const S_vector4 &pos)
{
    // A grenade on the belt, and the place over 6 m away.
    if (m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN)) {
        const S_vector4 &own = m_human->GetPosition4();
        const float dx = own.x - pos.x;
        const float dy = own.y - pos.y;
        const float dz = own.z - pos.z;
        if (36.0f < dx * dx + dy * dy + dz * dz) {
            m_action.Set(nullptr, SQUAD_ACTION_THROW, pos, m_human->GetDirection4(), false, 1.5f);
            m_action.arrived = true;
            m_flags = (m_flags & ~FLAG_ENGAGED) | FLAG_THROW;
            DropCarriedBody();
            NotifyIfIdle();
            return;
        }
    }
    SayReply(SENTENCE_CANNOT, true, 300, false);
}

void C_SquadController::FireLauncherAt(const S_vector4 &pos)
{
    // A launcher in the hands or on the back, loaded or with its ammo carried.
    C_Item *hands = m_human->GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
    C_ItemContainer *longGun = m_human->GetContainer(ITEM_CONTAINER_LONG_GUN);
    C_Item *back = longGun ? longGun->GetItem() : nullptr;
    C_Item *launcher = nullptr;
    if (hands && hands->GetDef()->GetCarryMode() == ITEM_CARRY_LAUNCHER)
        launcher = hands;
    else if (back && back->GetDef()->GetCarryMode() == ITEM_CARRY_LAUNCHER)
        launcher = back;
    if (!launcher)
        return;
    const int ammoId = launcher->GetDef()->GetAmmoItemId();
    if (!(launcher->GetAmount() >= 0.0f) && m_human->CountItems(ammoId) <= 0)
        return;
    if (launcher != hands) {
        if (!hands)
            m_human->RequestMoveItem(launcher, m_human, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS, 0);
        else
            m_human->RequestTransferItem(hands, ITEM_CONTAINER_HANDS, m_human, launcher, ITEM_CONTAINER_LONG_GUN);
    }
    m_action.Set(nullptr, SQUAD_ACTION_ORDER_MOVE, pos, m_human->GetDirection4(), false, 2.5f);
    m_action.arrived = true;
    m_flags = (m_flags & ~FLAG_ENGAGED) | FLAG_LAUNCHER;
    DropCarriedBody();
    NotifyIfIdle();
}

void C_SquadController::MoveTo(const S_vector4 &pos, const S_vector4 &dir, bool quiet, uint32_t stance)
{
    const S_vector &own = m_human->GetFrame()->GetWorldPos();
    const float dx = pos.x - own.x;
    const float dy = pos.y - own.y;
    const float dz = pos.z - own.z;
    if (dy * dy + dx * dx + dz * dz < 0.04f) {
        // Already there.
        SayReply(quiet ? SENTENCE_CANNOT + 1 : SENTENCE_CANNOT, false, 300, false);
        return;
    }
    m_boundHops = 0;
    m_action.Set(nullptr, SQUAD_ACTION_MOVE_TO, pos, dir, true, 0.5f);
    m_coverRadius = 0.5f;
    m_action.noCommand = true;
    m_action.useDir = true;
    m_quiet = quiet;
    m_holdDir = dir;
    m_action.stance = stance;
    SetStatusIcon(quiet ? 9 : 8);
    NotifyIfIdle();
    AdjustStanceForThreat();
}

void C_SquadController::Bound(const S_vector4 &dir, bool quiet)
{
    m_boundHops = 3;
    m_boundDir = dir;
    m_action.finished = true;
    m_action.Set(nullptr, SQUAD_ACTION_BOUND, WorldPos4Of(m_human->GetFrame()), dir, true, 1.5f);
    m_action.turned = true;
    m_action.arrived = true;
    m_action.noCommand = true;
    m_quiet = quiet;
    m_coverRadius = 3.0f;
    SetStatusIcon(quiet ? 5 : 4);
    NotifyIfIdle();
    AdjustStanceForThreat();
}

// ------------------------------------------------------------------------- bodies and items

bool C_SquadController::LootItemsOfType(C_Human *body, int typeId)
{
    // Up to three items of the type, into wherever they fit.
    bool moved = false;
    int count = 0;
    for (;;) {
        int container;
        C_Item *item = body->FindItem(typeId, &container);
        if (!item)
            return moved;
        if (g_itemsModule.CanPutItem(m_human, item) < 1)
            return moved;
        int toContainer = ITEM_CONTAINER_COUNT;
        m_human->ServerMoveItem(item->GetUid(), body->GetNetId(), container, &toContainer, 0);
        ++count;
        moved = true;
        if (count > 2)
            return true;
    }
}

bool C_SquadController::LootAmmo(C_Human *body)
{
    // The ammo of the firearms in the hands, on the back and on the belt; and of what is in
    // the backpack that is not a firearm (as the original).
    bool moved = false;
    C_ItemContainer *hands = m_human->GetContainer(ITEM_CONTAINER_HANDS);
    if (!hands->IsEmpty()) {
        C_Item *item = hands->GetItem();
        if (IsFirearm(item))
            moved = LootItemsOfType(body, item->GetDef()->GetAmmoItemId());
    }
    C_ItemContainer *longGun = m_human->GetContainer(ITEM_CONTAINER_LONG_GUN);
    if (!longGun->IsEmpty()) {
        C_Item *item = longGun->GetItem();
        if (IsFirearm(item))
            moved = LootItemsOfType(body, item->GetDef()->GetAmmoItemId()) || moved;
    }
    C_ItemContainer *belt = m_human->GetContainer(ITEM_CONTAINER_BELT);
    for (int i = 0; i < belt->GetUsedSlotCount(); ++i) {
        const std::vector<C_Item *> &slot = belt->GetSlots()[i];
        for (size_t j = slot.size(); j-- > 0;) {
            if (IsFirearm(slot[j]))
                moved = LootItemsOfType(body, slot[j]->GetDef()->GetAmmoItemId()) || moved;
        }
    }
    if (C_ItemContainer *contents = m_human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)) {
        for (int i = 0; i < contents->GetUsedSlotCount(); ++i) {
            const std::vector<C_Item *> &slot = contents->GetSlots()[i];
            for (size_t j = slot.size(); j-- > 0;) {
                if (!IsFirearm(slot[j]))
                    moved = LootItemsOfType(body, slot[j]->GetDef()->GetAmmoItemId()) || moved;
            }
        }
    }
    return moved;
}

bool C_SquadController::LootWeapon(C_Human *body)
{
    // The body's gun (or one lying within 3 m) with the most ammo for it on the body, when the
    // member has little ammo for its own.
    if (!body)
        return false;
    static const int CONTAINERS[2] = {ITEM_CONTAINER_HANDS, ITEM_CONTAINER_LONG_GUN};
    int bodyAmmo[2] = {0, 0};
    C_Item *bodyGuns[2] = {nullptr, nullptr};
    int ownAmmo[2] = {0, 0};
    C_Item *ownGuns[2] = {nullptr, nullptr};
    C_Actor *lying = g_temporaryActors.FindItemNear(XYZ(body->GetPosition4()), 3.0f);
    int lyingAmmo = 0;
    if (lying) {
        C_Item *item = static_cast<C_ItemActor *>(lying)->GetItem();
        if (IsCarriedGun(item->GetDef()->GetCarryMode()))
            lyingAmmo = body->CountAmmo(item->GetDef()->GetAmmoItemId(), false);
        else
            lying = nullptr;
    }
    for (int k = 0; k < 2; ++k) {
        C_ItemContainer *container = body->GetContainer(CONTAINERS[k]);
        if (container->IsEmpty())
            continue;
        C_Item *item = container->GetItem();
        bodyGuns[k] = item;
        if (IsCarriedGun(item->GetDef()->GetCarryMode()))
            bodyAmmo[k] = body->CountAmmo(item->GetDef()->GetAmmoItemId(), false);
        else
            bodyGuns[k] = nullptr;
    }
    for (int k = 0; k < 2; ++k) {
        C_ItemContainer *container = m_human->GetContainer(CONTAINERS[k]);
        if (container->IsEmpty())
            continue;
        C_Item *item = container->GetItem();
        ownGuns[k] = item;
        if (IsCarriedGun(item->GetDef()->GetCarryMode()))
            ownAmmo[k] = m_human->CountAmmo(item->GetDef()->GetAmmoItemId(), false);
        else
            ownGuns[k] = nullptr;
    }
    // Which: 0 the body's hands, 1 its back, 2 the one lying there.
    int which = -1;
    if (!bodyGuns[0]) {
        if (bodyGuns[1] && bodyAmmo[0] <= bodyAmmo[1] && lyingAmmo <= bodyAmmo[1])
            which = 1;
    } else if (!(bodyAmmo[0] < bodyAmmo[1] || bodyAmmo[0] < lyingAmmo)) {
        which = 0;
    } else if (bodyGuns[1] && bodyAmmo[0] <= bodyAmmo[1] && lyingAmmo <= bodyAmmo[1]) {
        which = 1;
    }
    if (which < 0) {
        if (!lying || lyingAmmo < bodyAmmo[0] || lyingAmmo < bodyAmmo[1])
            return false;
        which = 2;
    }
    if (ownAmmo[0] >= 2 || ownAmmo[1] >= 2)
        return false;
    // The member's guns go to the ground.
    for (int k = 0; k < 2; ++k) {
        if (ownGuns[k])
            m_human->DropItem(ownGuns[k]->GetUid(), m_human->GetDirection());
    }
    int container = ITEM_CONTAINER_COUNT;
    int ammoId;
    bool taken;
    if (which == 2) {
        C_Item *item = static_cast<C_ItemActor *>(lying)->GetItem();
        ammoId = item->GetDef()->GetAmmoItemId();
        taken = m_human->PickUpItem(lying->GetNetId(), &container);
    } else {
        C_Item *item = bodyGuns[which];
        ammoId = item->GetDef()->GetAmmoItemId();
        taken = m_human->ServerMoveItem(item->GetUid(), body->GetNetId(), which != 0 ? ITEM_CONTAINER_LONG_GUN : ITEM_CONTAINER_HANDS,
                                        &container, 0);
    }
    LootItemsOfType(body, ammoId);
    return taken;
}

bool C_SquadController::SearchBodyItems(C_Human *body)
{
    if (!body)
        return false;
    const bool ammo = LootAmmo(body);
    const bool weapon = LootWeapon(body);
    const bool grenades = TakeItems(body, ITEM_BEHAVIOUR_THROWN);
    const bool medkits = TakeItems(body, ITEM_BEHAVIOUR_MEDKIT);
    return medkits || ammo || weapon || grenades;
}

bool C_SquadController::TakeItems(C_Actor *from, int behaviour)
{
    // Every item of the behaviour, onto the belt (false when a move fails).
    std::vector<C_Item *> items;
    std::vector<int> containers;
    bool taken = false;
    if (static_cast<C_Human *>(from)->CollectItems(behaviour, items, containers) && !items.empty()) {
        taken = true;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!m_human->RequestMoveItem(items[i], from, containers[i], ITEM_CONTAINER_BELT, 0))
                return false;
        }
    }
    return taken;
}

void C_SquadController::Equip(C_Item *item, int *slot)
{
    C_Item *hands = m_human->GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
    if (!item || item == hands)
        return;
    if (hands)
        m_human->RequestTransferItem(hands, ITEM_CONTAINER_HANDS, m_human, item, *slot);
    else
        m_human->RequestMoveItem(item, m_human, *slot, ITEM_CONTAINER_HANDS, 0);
}

C_Item *C_SquadController::FindWeaponFor(C_Actor *target, int *container)
{
    if (!target)
        return nullptr;
    // Armoured targets need a launcher.
    if (IsArmoured(target))
        return FindWeapon(ITEM_BEHAVIOUR_LAUNCHER, false, false, container);
    std::vector<uint32_t> prefs;
    BuildWeaponPrefs(target, prefs);
    return PickWeapon(prefs, container);
}
