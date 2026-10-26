// The work of a human (C_HumanWork, S_HumanBody::work; original source 0x0077e1e0..0x00780c70):
// what the body is asked to do. The setters record the requests (refused while the human's
// state blocks them, unless forced); Update passes them on to the movement's next animation
// state and parts and gives the movement a copy of the record (C_HumanMovement::m_request).
#include "game/first_person_view.h"
#include "game/human_body.h"

#include <cmath>

#include "common/chunk.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_anim_tables.h"
#include "game/human_controller.h"
#include "game/human_internal.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

constexpr int ANIM_STAND = 0x2D;
constexpr int CARRY_ANIM_NONE = 0x5B;
constexpr int ACTIVITY_NONE = 0x4B;

// ApplyHit: where the hit came from, relative to the model (S_MoveRequest::hitFlags above the
// zone bits 0x3F; sides inferred from the model's x axis).
enum : uint32_t {
    WORK_HIT_ZONES = 0x3F,
    WORK_HIT_FROM_FRONT = 0x40,
    WORK_HIT_FROM_BEHIND = 0x80,
    WORK_HIT_FROM_LEFT = 0x100,
    WORK_HIT_FROM_RIGHT = 0x200,
};

enum : uint16_t {
    CHUNK_WORK = 0x309,
    CHUNK_WORK_REQUEST = 0x00,                          // holds the record's chunk 0x308
    CHUNK_WORK_GROUP = 0x01,
    CHUNK_WORK_FORCED = 0x02,
    CHUNK_WORK_MODE = 0x03,
    CHUNK_WORK_STANCE_TIMER = 0x04,
    CHUNK_WORK_BLOCKED_TIMER = 0x05,
    CHUNK_WORK_TIMER_STANCE = 0x06,
    CHUNK_WORK_REFUSED = 0x07,
    CHUNK_WORK_BODY_FLAGS = 0x08,
    CHUNK_WORK_BLOCKED = 0x09,
    CHUNK_WORK_BLOCKED2 = 0x0A,
};

bool InRange(int value, int low, int high) { return value >= low && value <= high; }

// The dying animations.
bool IsDeathAnim(int anim) { return InRange(anim, 0x71, 0x9A); }

// The reaction (activity) to a hit of a zone from a side (ACTIVITY_NONE: none).
int GetHitActivity(uint32_t hit)
{
    const bool front = (hit & WORK_HIT_FROM_FRONT) != 0;
    const bool behind = (hit & WORK_HIT_FROM_BEHIND) != 0;
    switch (hit & WORK_HIT_ZONES) {
    case 0x01:
        return front ? 0x1E : behind ? 0x1F : ACTIVITY_NONE;
    case 0x02:
        return front ? 0x20 : behind ? 0x22 : ACTIVITY_NONE;
    case 0x04:
        return front ? 0x24 : behind ? 0x26 : ACTIVITY_NONE;
    case 0x08:
        if (hit & WORK_HIT_FROM_LEFT)
            return 0x32;
        if (hit & WORK_HIT_FROM_RIGHT)
            return 0x33;
        return front ? 0x30 : behind ? 0x31 : ACTIVITY_NONE;
    case 0x10:
        return front ? 0x28 : behind ? 0x2A : ACTIVITY_NONE;
    case 0x20:
        return front ? 0x2C : behind ? 0x2E : ACTIVITY_NONE;
    default:
        return ACTIVITY_NONE;
    }
}

}  // namespace

// ------------------------------------------------------------------ setting up

C_HumanWork::C_HumanWork()
{
    m_owner = nullptr;
    SetGroup(1);
}

void C_HumanWork::SetMovement(C_HumanMovement *movement)
{
    m_movement = movement;
    m_movementBase = movement;
    m_owner = &movement->m_owner;
}

void C_HumanWork::SetGroup(int group)
{
    m_request.Reset();
    m_stanceTimer = MOVE_NO_TIME;
    m_group = group;
    m_forced = false;
    m_mode = 0;
    m_blockedTimer = 0;
    m_blocked2 = 0;
    m_blocked = 0;
    m_request.changed = 0;
    m_refused = 0;
    m_bodyFlags = 0;
    m_playCount = 0;
    m_flagsD8 = 0;
}

void C_HumanWork::Reset()
{
    SetGroup(m_group);
}

// ------------------------------------------------------------------ the update

void C_HumanWork::Update(int time)
{
    C_HumanMovement *const base = m_movementBase;
    S_HumanAnimState &next = base->m_nextAnimState;
    int carry = CARRY_ANIM_NONE;
    const bool standing = InRange(base->m_animState.anim.anim, 0x29, 0x41);
    m_playCount = 0;
    if ((*m_owner)->IsLocalPlayer())
        m_movement->m_locomotion.SetTurnTarget(m_request.facingDir, time);

    if (IsDeathAnim(m_request.anim)) {
        if (m_movement->m_specialMoves.Die(&next)) {
            m_request.anim = next.anim.anim;
            goto done;
        }
    } else if (base->m_unknown1AC != 1 || IsDeathAnim(base->m_animState.anim.anim)) {
        goto done;
    }

    {
        // The animation asked for, unless it only switches between standing animations that
        // need no new start.
        const int anim = m_request.anim;
        const int current = base->m_animState.anim.anim;
        const bool special = anim == 0x2C || anim == 0x2B || current == 0x2C || current == 0x2B || anim == 0x36 ||
                             anim == 0x3E || anim == 0x41 || current == 0x36 || current == 0x3E || current == 0x41;
        if (anim != HUMAN_ANIM_NONE && (!InRange(anim, 0x29, 0x41) || !standing || (anim != current && special)) &&
            (IsLocalGame() || next.anim.anim > 3)) {
            next.anim.anim = anim;
            next.anim.Normalize();
        }
    }
    if (m_request.activity != ACTIVITY_NONE)
        next.activity = m_request.activity;
    if (next.anim.anim == 0x12) {
        if (!base->m_onGround || base->m_animState.anim.anim == 0x18 || base->m_unknownA30 != MOVE_NO_TIME ||
            m_request.stance == HUMAN_STANCE_CROUCH) {
            next.anim.anim = HUMAN_ANIM_NONE;
            next.anim.Normalize();
        } else if (!standing) {
            next.anim.anim = 0x18;
            next.anim.Normalize();
        }
    }

    {
        // Animations the special moves take over (ladders, obstacles; inferred).
        const int anim = next.anim.anim;
        bool clear = false;
        if (anim == 0x6A) {
            if ((IsLocalGame() || (*m_owner)->IsLocalPlayer() || (*m_owner)->IsServerSimulated()) &&
                base->m_useState == MOVE_NO_TIME) {
                if (!InRange(base->m_animState.anim.anim, 8, 0x0E)) {
                    next.anim.anim = 10;
                    next.anim.Normalize();
                    clear = m_mode == 5 || !m_movement->m_specialMoves.TryClimbOver(&next);
                } else {
                    clear = true;
                }
            }
        } else if (!InRange(anim, 8, 0x0E)) {
            const int current = base->m_animState.anim.anim;
            if ((anim == 0x22 || anim == 0x23) && m_mode != 2 && m_mode != 3 && current != 0x9F && current != 0x9E &&
                current != 0x9B && current != 0x6F && current != 0x22)
                m_movement->m_specialMoves.StartSitting(&next);
        } else if (m_mode == 5) {
            clear = true;
        } else if (anim != 0x0E && anim != 8) {
            clear = !m_movement->m_specialMoves.TryClimbOver(&next);
        }
        if (clear) {
            m_request.anim = HUMAN_ANIM_NONE;
            next.anim.anim = HUMAN_ANIM_NONE;
            next.anim.Normalize();
        }
    }

    // A new gait or stance restarts the animation (standing for a new stance).
    if (m_request.gait != base->m_request.gait && m_request.anim == HUMAN_ANIM_NONE)
        m_request.anim = base->m_animState.anim.anim;
    if (m_request.stance != base->m_request.stance) {
        if (m_request.anim == HUMAN_ANIM_NONE) {
            m_request.anim = ANIM_STAND;
            next.anim.anim = ANIM_STAND;
            next.anim.Normalize();
        }
        bool wait = false;
        if (!IsLocalGame() && !(*m_owner)->IsLocalPlayer() && !(*m_owner)->IsServerSimulated()) {
            // A remote human finishes the start of a climb first (inferred).
            if ((InRange(next.anim.anim, 8, 0x0E) || InRange(base->m_animState.anim.anim, 8, 0x0E)) &&
                base->m_mixer.GetTrackProgress(0) <= 0.4f && base->m_mixer.GetTrackProgress(0) >= 0.0f)
                wait = true;
        }
        if (!wait)
            m_movement->m_locomotion.ChangeStance();
    }

    if (!IsServer() && !base->m_owner->IsLocalPlayer() && m_request.action == 1) {
        // A remote human takes its weapon: the model in the hands.
        C_Human *owner = base->m_owner;
        m_request.unknown5C = 1;
        if (!owner->GetEquipment()->GetModel(EQUIPMENT_PLACE_HANDS) && owner->GetHandsItem()) {
            C_Item *item = owner->GetHandsItem();
            owner->GetEquipment()->SetModel(item->GetFrame(0), EQUIPMENT_PLACE_HANDS, item->GetUid(), MOVE_NO_TIME,
                                            item->GetTypeId());
            owner->GetEquipment()->OnAnimationEvent(EQUIPMENT_EVENT_TAKE);
        }
        if (m_request.carryMode == 0 && owner->GetHandsItem() &&
            owner->GetHandsItem()->GetDef()->GetRecordType() == ITEM_ANIMATED)
            m_request.carryMode = owner->GetHandsItem()->GetDef()->GetCarryMode();
        const int carryMode = owner->GetWork()->m_request.carryMode;
        if (carryMode != 2 && carryMode != 6)
            owner->GetEquipment()->OnAnimationEvent(EQUIPMENT_EVENT_0);
    }

    // The carry animation: from the carry mode of the hands item and the actions.
    {
        const int carryMode = m_request.carryMode;
        if (carryMode == 0 && base->m_request.carryMode == 0) {
            // Empty hands: the actions that need none.
            switch (m_request.action) {
            case 0:
                break;
            case 9:
                if (base->m_unknownA2C == MOVE_NO_TIME) {
                    next.anim.anim = 0x9C;
                    next.anim.Normalize();
                } else if ((base->m_unknownA2C & ~8) == 1) {
                    next.anim.anim = (base->m_unknownA2C & 8) ? 0xA1 : 0x9D;
                    next.anim.Normalize();
                }
                m_request.action = 0;
                break;
            case 10:
                if (base->m_unknownA30 == MOVE_NO_TIME) {
                    next.anim.anim = 0x5E;
                    next.anim.Normalize();
                } else if (base->m_unknownA30 == 1) {
                    if (m_movement->m_locomotion.m_waterSurface == MOVE_NO_VALUE &&
                        !InRange(base->m_animState.anim.anim, 0xA2, 0xA5)) {
                        next.anim.anim = 0x5F;
                        next.anim.Normalize();
                    } else {
                        // In water (or at 0xA2..0xA5) the carried body is dropped at once.
                        I3D_frame *body = (*m_owner)->GetEquipment()->GetModel(EQUIPMENT_PLACE_CARRIED);
                        C_Actor *actor = body && body->GetData() ? body->GetData()->actor : nullptr;
                        base->OnDropBody(static_cast<C_Human *>(actor));
                    }
                }
                m_request.action = 0;
                break;
            case 0x0C:
                if (base->m_unknownA2C == MOVE_NO_TIME) {
                    next.anim.anim = 0xA0;
                    next.anim.Normalize();
                } else if ((base->m_unknownA2C & ~8) == 1) {
                    next.anim.anim = 0xA1;
                    next.anim.Normalize();
                }
                m_request.action = 0;
                break;
            case 0x0D:
                if (m_request.stance != HUMAN_STANCE_PRONE || base->m_animState.anim.anim < HUMAN_ANIM_FIRST_6X)
                    next.carryAnim = 0x4F;
                m_request.action = 0;
                break;
            case 0x0E:
                if (m_request.stance != HUMAN_STANCE_PRONE || base->m_animState.anim.anim < HUMAN_ANIM_FIRST_6X)
                    next.carryAnim = 0x5A;
                m_request.action = 0;
                break;
            default:
                break;
            }
        } else {
            if (base->m_request.action == m_request.action)
                m_request.action = 0;
            const int action = m_request.action;
            // 0: set, 1: unless 0x5C is asked for, 2: (not set).
            int set = 1;
            if (action == 0 || (carryMode != base->m_request.carryMode && base->m_request.carryMode == 0 && carryMode != 0)) {
                set = 0;
                if (carryMode == 100 && base->m_unknownA30 == MOVE_NO_TIME) {
                    carry = 0x5C;
                    m_request.carryMode = 0;
                } else if (base->m_unknownA2C == MOVE_NO_TIME && base->m_unknownA30 == MOVE_NO_TIME &&
                           !((*m_owner)->GetEquipment()->GetFlags() & EQUIPMENT_HANDS_PUT_AWAY)) {
                    const int previous = base->m_request.carryMode;
                    if (carryMode == previous || (carryMode != 0 && previous != 0)) {
                        if (carryMode == 6 || carryMode == 100 || base->m_animState.carryAnim == 0x27 ||
                            (m_request.stance == HUMAN_STANCE_PRONE && !standing))
                            set = 1;
                        else if (m_request.unknown5C == 0)
                            carry = (*m_owner)->GetRole() == HUMAN_ROLE_PLAYER ? 0x1E : 0x1F;
                        else
                            carry = 0x20;
                    } else if (carryMode != 0) {
                        carry = 0x23;
                    } else {
                        carry = 0x22;
                        base->m_unknown220 = previous;
                    }
                } else if (base->m_unknownA2C == 1) {
                    carry = 0x57;
                    m_request.carryMode = base->m_request.carryMode;
                } else if (base->m_unknownA30 == 1) {
                    carry = 0x58;
                    m_request.carryMode = base->m_request.carryMode;
                } else if (base->m_unknownA2C & 8) {
                    carry = 0x5C;
                    m_request.carryMode = base->m_request.carryMode;
                } else {
                    set = 1;
                }
            } else {
                // The actions with the hands item; some start a standing animation.
                bool stand = false;
                switch (action) {
                case 1:
                    carry = 0x27;
                    set = 0;
                    break;
                case 2:
                    next.activity = 0x4A;
                    carry = 0x27;
                    set = 0;
                    break;
                case 3: {
                    if (m_request.stance == HUMAN_STANCE_PRONE && !standing)
                        break;
                    const int kind = (*m_owner)->GetEquipment()->GetHandsProjectileKind();
                    carry = kind == 2 || (kind != 3 && kind != 4) ? 0x50 : 0x51;
                    if (m_request.anim == HUMAN_ANIM_NONE || m_request.anim == ANIM_STAND)
                        m_request.anim = 0x36;
                    set = 0;
                    break;
                }
                case 5:
                    carry = 0x25;
                    set = 0;
                    break;
                case 6:
                case 7:
                case 0x0D:
                case 0x0E:
                    if (m_request.stance != HUMAN_STANCE_PRONE || base->m_animState.anim.anim < HUMAN_ANIM_FIRST_6X) {
                        carry = action == 6 ? 0x4C : action == 7 ? 0x4D : action == 0x0D ? 0x4F : 0x5A;
                        stand = true;
                    }
                    break;
                case 8:
                    carry = 0x46;
                    stand = true;
                    break;
                case 10:
                    if (base->m_unknownA30 == MOVE_NO_TIME) {
                        next.anim.anim = 0x5E;
                        next.anim.Normalize();
                    } else if (base->m_unknownA30 == 1) {
                        next.anim.anim = 0x5F;
                        next.anim.Normalize();
                    }
                    m_request.action = 0;
                    break;
                case 0x0B:
                    carry = 0x47;
                    stand = true;
                    break;
                default:
                    break;
                }
                if (stand) {
                    if (m_request.anim == HUMAN_ANIM_NONE || m_request.anim == ANIM_STAND)
                        m_request.anim = 0x36;
                    set = 0;
                }
            }
            if (set == 0 || next.carryAnim != 0x5C)
                next.carryAnim = carry;
        }
    }

    {
        const int action = m_request.action;
        if ((action < 0 || action > 2) && base->m_animState.activity == 0x4A)
            next.activity = 0x4C;
    }
    // The reaction to a hit.
    if (m_mode != 5 && (base->m_interaction == 0 || base->m_interaction == 1) && m_request.hitFlags != 0 &&
        (m_request.hitFlags & WORK_HIT_ZONES)) {
        const int activity = GetHitActivity(m_request.hitFlags);
        m_movement->SetMorph(2, 0);
        const int current = m_request.activity;
        m_request.hitFlags = 0;
        if ((current == ACTIVITY_NONE || current == 0x4C || current == base->m_animState.activity) &&
            activity != ACTIVITY_NONE && base->m_unknown9AC == MOVE_NO_TIME)
            next.activity = activity;
    }
    next.anim.stance = m_request.stance;
    next.anim.Normalize();
    next.carryVariant = m_request.stance;
    next.activityVariant = m_request.stance;
    next.anim.sub = m_request.gait;
    m_movement->m_specialMoves.AdjustLean(&next);
    if (m_request.interaction != 0) {
        m_movement->m_idle.Interact(m_request.interactTarget, m_request.interaction, m_request.interactArg);
        m_request.interactTarget = nullptr;
        m_request.interaction = 0;
    }

done:
    if (m_mode == 4)
        m_bodyFlags |= 0x40;
    base->m_request = m_request;
}

void C_HumanWork::SetStanceTimer(int time, int stance, uint32_t /*blocked*/)
{
    if (time != MOVE_NO_TIME) {
        m_stanceTimer = time;
        m_timerStance = stance;
        return;
    }
    m_forced = true;
    SetStance(m_timerStance, true);
    m_forced = false;
    m_refused |= WORK_REQUEST_STANCE;
    m_stanceTimer = MOVE_NO_TIME;
    m_blockedTimer = 0;
}

void C_HumanWork::UpdateStanceTimer(int time)
{
    if (m_stanceTimer == MOVE_NO_TIME)
        return;
    m_stanceTimer -= time;
    if (m_stanceTimer < 0)
        SetStanceTimer(MOVE_NO_TIME, m_timerStance, m_blockedTimer);
}

// ------------------------------------------------------------------ the requests

S_vector4 C_HumanWork::GetViewDir() const
{
    return m_movementBase->m_unknown940;
}

void C_HumanWork::Play(int animation, bool on)
{
    (*m_owner)->IsLocalPlayer();                        // (asked and not used)
    if (((m_blockedTimer | m_blocked | m_blocked2 | m_flagsD8) & WORK_REQUEST_ANIM ||
         m_movement->m_locomotion.m_waterTransition < 0) &&
        !m_forced) {
        m_refused |= WORK_REQUEST_ANIM;
        return;
    }
    if (IsDeathAnim(m_request.anim))
        return;
    bool keep = false;
    switch (m_mode) {
    case 1: {
        // On a ladder / at a window: the moves become climbing up or down.
        const bool forward = animation == 0xBE || animation == 0xC6;
        const bool back = animation == 0xBF || animation == 0xC7;
        if (!forward && !back) {
            const int current = m_movementBase->m_animState.anim.anim;
            if (!InRange(animation, 0x6A, 0x6C) && animation != 8 && animation != 0x0E && !IsDeathAnim(animation) &&
                (animation != 0x12 || !InRange(current, 0x6A, 0x6C)))
                animation = HUMAN_ANIM_NONE;
            (*m_owner)->SetMoveFlags(0);
            keep = true;
        } else if (!(*m_owner)->IsLocalPlayer()) {
            if (forward)
                animation = 0x6A;
            else
                animation = (m_request.gait == 2) + 0x6B;
            keep = true;
        } else if (back) {
            animation = HUMAN_ANIM_NONE;
            keep = true;
        } else if (GetViewDir().y > 0.17f) {
            animation = 0x6A;
            keep = true;
        } else if (GetViewDir().y < -0.2f) {
            animation = (m_request.gait == 2) + 0x6B;
            keep = true;
        }
        break;
    }
    case 2:
        if (animation == 0x1D || animation == 0x25) {
            Interact(nullptr, INTERACT_FLAG_BODY | INTERACT_STAND_UP, 0, true);
            animation = HUMAN_ANIM_NONE;
            keep = true;
        } else if (animation == 0x26) {
            Interact(nullptr, INTERACT_FLAG_BODY | INTERACT_STAND_UP_FAST, 0, true);
            animation = HUMAN_ANIM_NONE;
            keep = true;
        } else {
            keep = IsDeathAnim(animation);
        }
        break;
    case 3:
        if (animation == 0x25 || animation == 0x1D || animation == 0x26) {
            animation = 0x25;
            keep = true;
        } else {
            keep = IsDeathAnim(animation);
        }
        break;
    case 4:
        keep = IsDeathAnim(animation);
        break;
    case 5:
        // Swimming: the moves become the swimming animations (with diving gear 0x5C / 0x5D).
        keep = true;
        if (IsDeathAnim(animation))
            break;
        if (animation == 0xBE || animation == 0xC6 || animation == 0xBF || animation == 0xC7) {
            m_movement->m_locomotion.m_swimBack = animation == 0xBF || animation == 0xC7;
            m_movement->m_locomotion.UpdateVelocity(true);
            animation = (*m_owner)->IsWearingDivingGear() ? 0x5C : 0x5A;
            (*m_owner)->SetMoveFlags(m_movement->m_locomotion.m_swimBack ? MOVE_BACK : MOVE_FORWARD);
        } else if (!InRange(animation, 0x2D, 0x41)) {
            if (animation != 0x59 && animation != 0x68 && animation != 0x69 && animation != 0x5B && animation != 0x6A) {
                m_movement->m_locomotion.m_swimBack = false;
                return;
            }
        } else {
            m_movement->m_locomotion.m_swimBack = false;
            if (animation == ANIM_STAND) {
                animation = (*m_owner)->IsWearingDivingGear() ? 0x5D : 0x5B;
                (*m_owner)->SetMoveFlags(0);
            }
        }
        break;
    default:
        keep = true;
        break;
    }
    if (!keep)
        animation = HUMAN_ANIM_NONE;
    if ((m_movementBase->m_animState.anim.anim != 0x11 || animation < HUMAN_ANIM_FIRST_6X) &&
        (animation != 0x1D || m_request.anim == 0x22) && m_request.anim != animation) {
        ++m_playCount;
        m_request.anim = animation;
        if (on)
            m_request.changed |= WORK_REQUEST_ANIM;
    }
}

void C_HumanWork::PlayWorkAnimation(int animation, bool on)
{
    if (IsBlocked(WORK_REQUEST_ACTIVITY)) {
        m_refused |= WORK_REQUEST_ACTIVITY;
        return;
    }
    if (m_request.activity != animation) {
        m_request.activity = animation;
        if (on)
            m_request.changed |= WORK_REQUEST_ACTIVITY;
    }
}

void C_HumanWork::SetStance(int stance, bool on)
{
    const C_HumanMovement *base = m_movementBase;
    const bool allowed = !((m_blockedTimer | m_blocked | m_blocked2) & WORK_REQUEST_STANCE) && m_mode != 5 &&
                         base->m_unknownA2C == MOVE_NO_TIME && base->m_unknownA30 == MOVE_NO_TIME &&
                         m_movement->m_locomotion.m_waterTransition >= 0 && !InRange(base->m_animState.anim.anim, 0xA2, 0xA5);
    if (!allowed && !m_forced) {
        m_refused |= WORK_REQUEST_STANCE;
        return;
    }
    if (m_request.stance != stance) {
        m_request.stance = stance;
        if (on)
            m_request.changed |= WORK_REQUEST_STANCE;
    }
    if (stance == HUMAN_STANCE_STAND)
        (*m_owner)->SetMoveFlags(MOVE_STANDING);
    else if (stance == HUMAN_STANCE_CROUCH)
        (*m_owner)->SetMoveFlags(MOVE_CROUCHING);
    else if (stance == HUMAN_STANCE_PRONE)
        (*m_owner)->SetMoveFlags(MOVE_LYING);
}

void C_HumanWork::SetGait(int gait, bool on)
{
    if (IsBlocked(WORK_REQUEST_GAIT)) {
        m_refused |= WORK_REQUEST_GAIT;
        return;
    }
    if (m_request.gait == gait)
        return;
    m_request.gait = gait;
    if (on)
        m_request.changed |= WORK_REQUEST_GAIT;
    // The controller's pace.
    switch (gait) {
    case 0:
        (*m_owner)->SetMoveFlags(MOVE_PACE_1);
        break;
    case 5:
        (*m_owner)->SetMoveFlags(MOVE_PACE_MARCH);
        break;
    case 1:
        (*m_owner)->SetMoveFlags(MOVE_PACE_3);
        break;
    case 2:
        (*m_owner)->SetMoveFlags(MOVE_PACE_4);
        break;
    default:
        (*m_owner)->SetMoveFlags(MOVE_PACE_2);
        break;
    }
}

void C_HumanWork::SetLookAt(const S_vector4 &target, bool on)
{
    if (IsBlocked(WORK_REQUEST_LOOK_AT)) {
        m_refused |= WORK_REQUEST_LOOK_AT;
        return;
    }
    S_vector4 &current = m_request.unknown10;
    if (current.x != target.x || current.y != target.y || current.z != target.z) {
        current = target;
        if (on)
            m_request.changed |= WORK_REQUEST_LOOK_AT;
    }
}

void C_HumanWork::SetCarryMode(int carryMode, bool notify)
{
    const int hands = m_movementBase->m_handsCarryState;
    const bool allowed = !((m_blocked | m_blocked2) & WORK_REQUEST_CARRY_MODE) &&
                         (((hands == 0 || hands == 3 || hands == 4) && m_movement->m_animPlayer.m_carryDelay == MOVE_NO_TIME) ||
                          carryMode == 100);
    if (!allowed && !m_forced) {
        m_refused |= WORK_REQUEST_CARRY_MODE;
        return;
    }
    if (m_request.carryMode != carryMode) {
        m_request.carryMode = carryMode;
        if (notify)
            m_request.changed |= WORK_REQUEST_CARRY_MODE;
    }
}

void C_HumanWork::DoAction(int action, bool on)
{
    if (IsBlocked(WORK_REQUEST_ACTION)) {
        m_refused |= WORK_REQUEST_ACTION;
        return;
    }
    if (m_request.action != action) {
        m_request.action = action;
        if (on)
            m_request.changed |= WORK_REQUEST_ACTION;
    }
}

void C_HumanWork::SetUnknown5C(bool value, bool on)
{
    // (Not forced past the blocks, unlike the other requests.)
    if ((m_blocked | m_blocked2) & WORK_REQUEST_UNKNOWN5C) {
        m_refused |= WORK_REQUEST_UNKNOWN5C;
        return;
    }
    if (m_request.unknown5C == static_cast<int>(value))
        return;
    m_request.unknown5C = value;
    if (IsClient() && g_teamManager.GetPlayerTeam()->GetActivePlayer() == *m_owner)
        GetGame()->GetFirstPersonView()->SetPose(value ? FPV_POSE_RAISED : FPV_POSE_LOWERED);
    if (on)
        m_request.changed |= WORK_REQUEST_UNKNOWN5C;
}

void C_HumanWork::SetFacingDir(const S_vector &dir, bool /*on*/)
{
    if (IsBlocked(WORK_REQUEST_FACING)) {
        m_refused |= WORK_REQUEST_FACING;
        return;
    }
    // Only where the human is simulated.
    if (!IsLocalGame() && !m_forced && !IsServer() && !(*m_owner)->IsLocalPlayer())
        return;
    const S_vector flat = S_vector(dir.x, 0.0f, dir.z).Normalized();
    S_vector4 &moveDir = m_movementBase->m_unknown240;
    S_vector4 &facing = m_request.facingDir;
    if (std::fabs(moveDir.x - flat.x) + std::fabs(moveDir.z - flat.z) > 0.01f &&
        std::fabs(facing.x - flat.x) + std::fabs(facing.z - flat.z) > 0.01f) {
        moveDir.x = moveDir.y = moveDir.z = MOVE_NO_VALUE;
        // Fix: w is left (the original copies the 4 bytes after `dir`).
        facing.x = flat.x;
        facing.y = flat.y;
        facing.z = flat.z;
        // (Marked changed whatever `on` says.)
        m_request.changed |= WORK_REQUEST_FACING;
    }
}

void C_HumanWork::SetViewDir(const S_vector &dir)
{
    if (IsBlocked(WORK_REQUEST_VIEW_DIR)) {
        m_refused |= WORK_REQUEST_VIEW_DIR;
        return;
    }
    m_movement->m_weaponPose->SetViewDir(dir);
    // Fix: w is left (the original copies the 4 bytes after `dir`).
    S_vector4 &view = m_movementBase->m_unknown940;
    view.x = dir.x;
    view.y = dir.y;
    view.z = dir.z;
}

void C_HumanWork::SetWorkingFrame(I3D_frame *frame, bool on)
{
    if (IsBlocked(WORK_REQUEST_WORKING_FRAME)) {
        m_refused |= WORK_REQUEST_WORKING_FRAME;
        return;
    }
    if (m_request.workingFrame != frame) {
        m_request.workingFrame = frame;
        if (on)
            m_request.changed |= WORK_REQUEST_WORKING_FRAME;
    }
}

void C_HumanWork::SetWorkingFrame2(I3D_frame *frame, bool on)
{
    if (IsBlocked(WORK_REQUEST_WORKING_FRAME)) {
        m_refused |= WORK_REQUEST_WORKING_FRAME;
        return;
    }
    if (m_request.workingFrame != frame) {
        m_request.workingFrame = frame;
        frame->AddRef();
        if (on)
            m_request.changed |= WORK_REQUEST_WORKING_FRAME;
    }
}

void C_HumanWork::ApplyHit(uint32_t flags, const S_vector &pos, const S_vector &force)
{
    if (IsBlocked(WORK_REQUEST_HIT)) {
        m_refused |= WORK_REQUEST_HIT;
        return;
    }
    C_HumanWeaponPose *pose = m_movementBase->m_weaponPose2;
    if (m_movementBase->m_animState.anim.anim == 0x62) {
        pose->OnHit(flags, pos, force);
        return;
    }
    // The side the hit came from.
    m_request.hitFlags = flags;
    const S_vector from = (-force).Normalized();
    const S_matrix &m = (*m_owner)->GetFrame()->GetMatrix();
    const float ahead = S_vector(m.m[2][0], m.m[2][1], m.m[2][2]).Normalized().Dot(from);
    if (ahead > 0.707f)
        m_request.hitFlags |= WORK_HIT_FROM_FRONT;
    else if (ahead < -0.707f)
        m_request.hitFlags |= WORK_HIT_FROM_BEHIND;
    else if (from.Dot(S_vector(m.m[0][0], m.m[0][1], m.m[0][2])) <= 0.0f)
        m_request.hitFlags |= WORK_HIT_FROM_LEFT;
    else
        m_request.hitFlags |= WORK_HIT_FROM_RIGHT;
    // Fix: the pads after the vectors are left (the original copies the 4 bytes after them).
    m_request.hitPos = pos;
    m_request.hitForce = force;
    pose->OnHit(m_request.hitFlags & WORK_HIT_ZONES, m_request.hitPos, m_request.hitForce);
}

void C_HumanWork::Interact(C_Actor *target, uint32_t interaction, int arg, bool on)
{
    const bool allowed = !((m_blocked | m_blocked2) & WORK_REQUEST_INTERACT) &&
                         !InRange(m_movementBase->m_animState.anim.anim, 8, 0x0E);
    if (!allowed && !m_forced) {
        m_refused |= WORK_REQUEST_INTERACT;
        return;
    }
    if (m_request.interaction != interaction) {
        m_request.interaction = interaction;
        m_request.interactTarget = target;
        m_request.interactArg = arg;
        if (on)
            m_request.changed |= WORK_REQUEST_INTERACT;
    }
}

void C_HumanWork::SetLook(uint32_t flags, const S_vector4 *at)
{
    if (IsBlocked(WORK_REQUEST_LOOK)) {
        m_refused |= WORK_REQUEST_LOOK;
        return;
    }
    m_movementBase->m_weaponPose2->SetLook(flags, at);
}

// ------------------------------------------------------------------ save games

bool C_HumanWork::SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_WORK))
        return false;
    chunk.Begin(CHUNK_WORK_REQUEST);
    m_request.SaveGame(chunk);
    chunk.Ascend();
    chunk.WriteChunk(CHUNK_WORK_GROUP, m_group);
    chunk.WriteChunk(CHUNK_WORK_FORCED, static_cast<int>(m_forced));
    chunk.WriteChunk(CHUNK_WORK_MODE, m_mode);
    chunk.WriteChunk(CHUNK_WORK_STANCE_TIMER, m_stanceTimer);
    chunk.WriteChunk(CHUNK_WORK_BLOCKED_TIMER, m_blockedTimer);
    chunk.WriteChunk(CHUNK_WORK_TIMER_STANCE, m_timerStance);
    chunk.WriteChunk(CHUNK_WORK_REFUSED, m_refused);
    chunk.WriteChunk(CHUNK_WORK_BODY_FLAGS, m_bodyFlags);
    chunk.WriteChunk(CHUNK_WORK_BLOCKED, m_blocked);
    chunk.WriteChunk(CHUNK_WORK_BLOCKED2, m_blocked2);
    chunk.Ascend();
    return true;
}

bool C_HumanWork::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != CHUNK_WORK) {
        chunk.Ascend();
        return false;
    }
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        int value = 0;
        switch (id) {
        case CHUNK_WORK_REQUEST:
            m_request.LoadGame(chunk);
            break;
        case CHUNK_WORK_GROUP:
            chunk.Read(m_group);
            break;
        case CHUNK_WORK_FORCED:
            chunk.Read(value);
            m_forced = value != 0;
            break;
        case CHUNK_WORK_MODE:
            chunk.Read(m_mode);
            break;
        case CHUNK_WORK_STANCE_TIMER:
            chunk.Read(m_stanceTimer);
            break;
        case CHUNK_WORK_BLOCKED_TIMER:
            chunk.Read(m_blockedTimer);
            break;
        case CHUNK_WORK_TIMER_STANCE:
            chunk.Read(m_timerStance);
            break;
        case CHUNK_WORK_REFUSED:
            chunk.Read(m_refused);
            break;
        case CHUNK_WORK_BODY_FLAGS:
            chunk.Read(m_bodyFlags);
            break;
        case CHUNK_WORK_BLOCKED:
            chunk.Read(m_blocked);
            break;
        case CHUNK_WORK_BLOCKED2:
            chunk.Read(m_blocked2);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}
