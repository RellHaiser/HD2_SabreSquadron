// C_AIController's fighting (original source 0x00571680..0x00576ac0): carrying out the fire
// order the derived controllers give (bursts and pauses, reloading, throwing grenades), the
// scatter of its aim, turning to look, and the stance it is asked for.
#include "game/human_controller.h"

#include <cmath>

#include "game/ai_parts.h"
#include "game/checkpoints.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_body.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/profile.h"
#include "ls3d/i3d.h"

namespace {

// A walking point where a crouching AI rises to fire (S_Checkpoint::flags, inferred).
constexpr uint16_t CHECKPOINT_FLAG_RISE_TO_FIRE = 0x4000;
// Walking point types the stance follows (S_Checkpoint::type, inferred from the stances).
enum : uint8_t {
    CHECKPOINT_TYPE_4 = 4,                              // with a waypoint of type 4
    CHECKPOINT_TYPE_6 = 6,                              // with a waypoint of type 5
    CHECKPOINT_TYPE_CROUCH = 7,
    CHECKPOINT_TYPE_LIE = 8,
};
// A thrown grenade's eye height over the frame (standing, crouching) and half of g.
constexpr float THROW_HEIGHT_STANDING = 1.8f;
constexpr float THROW_HEIGHT_CROUCHING = 1.1f;
constexpr float HALF_G = 4.905f;


float Xyz4Square(const S_vector4 &v)
{
    return v.z * v.z + v.y * v.y + v.x * v.x;
}

// The weapon's attack time (C_WeaponItem +0x58). (Fix: the original reads it of any item in
// the hands; other items have none.)
uint32_t AttackTimeOf(const C_Item *item, uint32_t now)
{
    if (item->GetKind() == ITEM_KIND_WEAPON)
        return static_cast<const C_WeaponItem *>(item)->GetAttackTime();
    return now;
}

uint32_t ReadyTimeOf(const C_Item *item)
{
    if (item->GetKind() == ITEM_KIND_WEAPON)
        return static_cast<const C_WeaponItem *>(item)->GetReadyTime();
    return 0;
}

int ReloadTimeOf(const C_Item *item)
{
    return static_cast<C_WeaponComp *>(item->GetDef()->GetComponent())->GetReloadTimeMs();
}

// A grenade's or a launched projectile's flight: the direction to lob it at `target` from
// `from` at `speed` (half of g times the time of flight squared lifts the aim).
S_vector LobDirection(const S_vector &from, const S_vector &target, float speed)
{
    const float distance = (target - from).Magnitude();
    const float flightTime = distance * 1.1f / speed;
    S_vector flat(target.x - from.x, 0.0f, target.z - from.z);
    const float flatDistance = flat.Magnitude();
    flat.Normalize();
    const float angle = std::atan((flightTime * flightTime * HALF_G + (target.y - from.y)) * (1.0f / flatDistance));
    S_vector dir(flat.x * std::cos(angle), std::sin(angle), flat.z * std::cos(angle));
    dir.Normalize();
    return dir;
}

}  // namespace

// ------------------------------------------------------------------------- the aim

float C_AIController::GetAimSpread()
{
    // Worse with less shooting skill, standing (better lying), moving fast, after a noise of
    // its own, and with some weapons.
    float spread = 1.0f - static_cast<float>(m_human->m_stats[STAT_SHOOTING]) * 0.01f;
    spread = spread + spread;
    switch (m_human->GetStance()) {
    case 0:
        spread = RandFloat() * 0.3f + spread + 0.7f;
        break;
    case 1:
        spread = RandFloat() * 0.6f + spread + 0.3f;
        break;
    default:
        spread = RandFloat() * 0.4f + spread;
        break;
    }
    switch (m_human->GetMoveSpeedClass()) {
    case 0:
        spread = RandFloat() * 0.3f + spread + 0.2f;
        break;
    case 1:
        spread = RandFloat() * 0.2f + spread + 0.3f;
        break;
    case 2:
        spread = RandFloat() * 0.1f + spread + 0.4f;
        break;
    case 5:
        spread = RandFloat() * 0.3f + spread + 0.5f;
        break;
    default:
        spread = RandFloat() * 0.5f + spread;
        break;
    }
    if (m_quietTime < 2500)
        spread = RandFloat() * 0.6f + spread + 0.4f;
    switch (m_handsItem->GetDef()->GetCarryMode()) {
    case 1:
    case 4:
        return RandFloat() * 0.2f + spread + 0.3f;
    case 2:
        return RandFloat() * 0.4f + spread + 1.0f;
    default:
        return spread;
    }
}

bool C_AIController::AdjustAim(S_vector *dir)
{
    const C_Item *item = m_handsItem;
    C_ItemComponent *component = item && item->GetDef() ? item->GetDef()->GetComponent() : nullptr;
    if (!item || !m_fire.target || !component || component->GetKind() != ICK_PROJECTILE_WEAPON) {
        // Scattered sideways (the squad less; harder difficulties more).
        float spread = GetAimSpread();
        if (m_type == HUMAN_CONTROLLER_SQUAD) {
            *dir *= 100.0f;
            spread *= 0.4f;
        } else {
            const int difficulty = g_gameProfile.GetDifficulty();
            if (difficulty == 1)
                spread = spread + spread;
            else if (difficulty == 2)
                spread *= 1.5f;
            *dir *= 100.0f;
        }
        S_vector side(dir->y * 0.0f - dir->z, dir->z * 0.0f - dir->x * 0.0f, dir->x - dir->y * 0.0f);
        side.Normalize();
        float offset = (RandFloat() - 0.5f) * side.x * spread;
        dir->x = offset + offset + dir->x;
        offset = (RandFloat() - 0.5f) * side.y * spread;
        dir->y = offset + offset + dir->y;
        offset = (RandFloat() - 0.5f) * side.z * spread;
        dir->z = offset + offset + dir->z;
        dir->Normalize();
        return true;
    }
    // A projectile weapon lobs at the target, missing by up to 10 m with no shooting skill
    // (the squad aims true).
    const float speed = static_cast<C_ProjectileWeaponComp *>(component)->GetMuzzleVelocity();
    S_vector eye = m_human->GetFrame()->GetWorldPos();
    S_vector target = m_fire.target->GetFrame()->GetWorldPos();
    if (m_type != HUMAN_CONTROLLER_SQUAD) {
        const float error = 10.0f - static_cast<float>(m_human->m_stats[STAT_SHOOTING]) * 0.1f;
        const float r1 = RandFloat();
        const float r2 = RandFloat();
        const float r3 = RandFloat();
        const float z = (r3 - 0.5f) * error;
        target.x = ((r2 - 0.5f) + (r2 - 0.5f)) * error + target.x;
        target.y = ((r1 - 0.5f) + (r1 - 0.5f)) * error + target.y;
        target.z = target.z + z + z;
    }
    if (m_moveFlags & MOVE_STANDING)
        eye.y += THROW_HEIGHT_STANDING;
    else if (m_moveFlags & MOVE_CROUCHING)
        eye.y += THROW_HEIGHT_CROUCHING;
    else if (m_moveFlags & MOVE_LYING)
        return false;
    // (The original also writes the target frame's w after the direction.)
    *dir = LobDirection(eye, target, speed);
    return true;
}

void C_AIController::ThrowGrenade()
{
    // A grenade from the belt at m_fire.throwPos.
    C_Item *grenade = m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN);
    if (!grenade)
        return;
    const float speed = static_cast<C_ProjectileWeaponComp *>(grenade->GetDef()->GetComponent())->GetMuzzleVelocity();
    S_vector eye = m_human->GetFrame()->GetWorldPos();
    if (m_moveFlags & MOVE_STANDING)
        eye.y += THROW_HEIGHT_STANDING;
    else if (m_moveFlags & MOVE_CROUCHING)
        eye.y += THROW_HEIGHT_CROUCHING;
    else if (m_moveFlags & MOVE_LYING)
        return;
    // (The original zeroes the throw position's height in the controller while it computes
    // the flat distance; the height it uses is the one before.)
    const S_vector target(m_fire.throwPos.x, m_fire.throwPos.y, m_fire.throwPos.z);
    m_fire.throwPos.y = 0.0f;
    const S_vector dir = LobDirection(eye, target, speed);
    m_facing->SetView(ToVector4(dir, 0.0f));
    m_buttons[BUTTON_THROW] = 1.0f;
    const int length = m_human->m_body.GetCarryAnimLength(ANIM_THROW, (~m_moveFlags >> 2) & 1, 0);
    m_fire.pauseTime = length;
    m_fire.unknown0C = length + 100;
}

// ------------------------------------------------------------------------- the timers

void C_AIController::UpdateTimers(int timeDelta)
{
    const auto countDown = [timeDelta](int &timer) {
        if (timer > 0) {
            timer -= timeDelta;
            if (timer < 0)
                timer = 0;
        }
    };
    countDown(m_unknown120);
    countDown(m_fire.pauseTime);
    countDown(m_fire.unknown0C);
    countDown(m_lookAroundTime);
    countDown(m_unknown110);
    m_unknown10C += timeDelta;
    UpdateInteraction(timeDelta);
}

bool C_AIController::ExtendBurst(uint32_t time)
{
    if (static_cast<uint32_t>(m_fire.pauseTime) < time)
        m_fire.pauseTime = static_cast<int>(time);
    if (m_moveFlags & MOVE_FIRING)
        m_fire.burstTime += static_cast<int>(time);
    return true;
}

// ------------------------------------------------------------------------- looking

int C_AIController::TurnTowards(const S_vector4 &from, const S_vector4 &to)
{
    // The body turns toward `to`; on a ladder (work state 5) the view too, the height kept.
    S_vector4 dir = to;
    if (!m_human->m_body.IsWorkState5()) {
        reinterpret_cast<S_vector &>(dir) = S_vector(to.x - from.x, 0.0f, to.z - from.z).Normalized();
        return m_facing->SetFacing(dir);
    }
    reinterpret_cast<S_vector &>(dir) = (XYZ(to) - XYZ(from)).Normalized();
    const int result = m_facing->SetFacing(dir);
    m_facing->SetView(dir);
    return result;
}

int C_AIController::ApplyAlertState()
{
    // A new alert state sets the facing's / view's locks (1: the body stays; 2 / 4: the view
    // stays; others: neither), unless the move order is busy.
    if (!m_alert->changed)
        return 1;
    if (m_alert->Is1()) {
        m_alert->changed = false;
        m_facing->UnlockView();
        m_facing->LockFacing();
        return 2;
    }
    if (m_move->flags & S_AIMoveOrder::FLAG_BUSY)
        return 0;
    m_alert->changed = false;
    m_facing->UnlockFacing();
    if (m_alert->Is4() || m_alert->Is2())
        m_facing->LockView();
    else
        m_facing->UnlockView();
    return 2;
}

int C_AIController::UpdateLook(int timeDelta)
{
    S_AIFacing *facing = m_facing;
    if (!facing->lookAt) {
        if (Xyz4Square(facing->lookDir) <= 0.1f) {
            // Nothing to look at: now and then a look around.
            if (facing->TickLookTimer(timeDelta))
                LookAround(1);
        } else {
            const S_vector4 dir = facing->lookDir;
            if (!facing->IsViewLocked()) {
                facing->SetView(dir);
            } else {
                // The view may not turn: a look to the side the direction is on.
                float view[2] = {facing->view.x, facing->view.z};
                float target[2] = {dir.x, dir.z};
                const float viewLength = std::sqrt(view[1] * view[1] + view[0] * view[0]);
                if (viewLength > 1e-8f) {
                    view[0] *= 1.0f / viewLength;
                    view[1] = (1.0f / viewLength) * view[1];
                }
                const float targetLength = std::sqrt(target[0] * target[0] + target[1] * target[1]);
                if (targetLength > 1e-8f) {
                    target[0] *= 1.0f / targetLength;
                    target[1] = (1.0f / targetLength) * target[1];
                }
                LookAround(AngleBetween2D(view, target) >= 0.0f ? 6 : 5);
            }
            facing->lookDir.x = facing->lookDir.y = facing->lookDir.z = 0.0f;
        }
    } else {
        // A frame to look at: in front, or with the view locked, a look around at it.
        const S_vector &target = facing->lookAt->GetWorldPos();
        S_vector dir = target - m_human->GetBodyFrame()->GetWorldPos();
        dir.Normalize();
        if (-0.1f < dir.Dot(XYZ(facing->facing)) || facing->IsViewLocked()) {
            LookAroundAt(8, reinterpret_cast<const S_vector4 &>(facing->lookAt->GetMatrix().m[3]));
            facing->SetLookAt(nullptr);
        } else {
            facing->SetView(ToVector4(dir, 0.0f));
            facing->SetLookAt(nullptr);
        }
    }
    m_facingDir = facing->facing;
    m_viewDir = facing->view;
    return 0;
}

// ------------------------------------------------------------------------- firing

bool C_AIController::StartFiring()
{
    if (!m_handsItem)
        return false;
    if (!m_fire.target && !(m_fire.flags & S_AIFire::FLAG_DIRECTION))
        return false;
    if (!m_weaponRaised)
        m_buttons[BUTTON_FIRE] = 1.0f;
    // The burst: the order's length give or take its spread, by default 100 .. 400 ms.
    int time, spread;
    if (m_fireOrder->GetBurstTime(&time, &spread))
        m_fire.burstTime = static_cast<int>((RandFloat() - 0.5f) * static_cast<float>(spread) * 2.0f + static_cast<float>(time));
    else
        m_fire.burstTime = static_cast<int>(RandFloat() * 300.0f + 100.0f);
    if (m_fire.flags & S_AIFire::FLAG_DIRECTION)
        m_fire.unknown00 = false;
    const uint32_t old = m_moveFlags;
    m_moveFlags = old | MOVE_FIRING;
    if (old & MOVE_LOOKING_AROUND)
        LookAround(0);
    // Crouching behind cover at a point that allows it: rise to fire.
    if (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && (m_moveFlags & MOVE_CROUCHING) && m_currentCheckpoint &&
        (m_currentCheckpoint->flags & CHECKPOINT_FLAG_RISE_TO_FIRE)) {
        m_fire.pauseTime = 200;
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RISE_FROM_COVER));
        const S_vector4 zero = {};
        RequestAction(CONTROLLER_ACTION_RISE_FROM_COVER, &zero, &zero, 0.0f, -1, -1);
    }
    return true;
}

void C_AIController::StopFiring(uint32_t pause)
{
    // The pause before the next burst: the order's, else `pause`; at least until the weapon
    // is ready again.
    int time, spread;
    if (m_fireOrder->GetPauseTime(&time, &spread))
        pause = static_cast<uint32_t>(static_cast<int>((RandFloat() - 0.5f) * static_cast<float>(spread) * 2.0f + static_cast<float>(time)));
    const uint32_t old = m_moveFlags;
    m_moveFlags = old & ~MOVE_FIRING;
    if (old & MOVE_UNKNOWN_8000000) {
        // Back behind the cover.
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_BACK_TO_COVER));
        const S_vector4 zero = {};
        RequestAction(CONTROLLER_ACTION_BACK_TO_COVER, &zero, &zero, 0.0f, -1, -1);
    }
    m_fire.target = nullptr;
    m_fire.flags = 0;
    if (C_Item *item = m_handsItem) {
        const uint32_t now = GetGame()->GetMissionTime();
        const int itemBusy = static_cast<int>(item->GetTime() - now);
        const int weaponBusy = static_cast<int>(static_cast<float>(static_cast<int>(now - AttackTimeOf(item, now))) * -1.0f);
        if (itemBusy > 0 || weaponBusy > 0) {
            const int longest = itemBusy > weaponBusy ? itemBusy : weaponBusy;
            if (static_cast<uint32_t>(longest) > pause) {
                m_fire.pauseTime = longest;
                return;
            }
        }
    }
    m_fire.pauseTime = static_cast<int>(pause);
}

void C_AIController::PressReload()
{
    m_buttons[BUTTON_RELOAD] = 1.0f;
    m_fire.flags &= ~S_AIFire::FLAG_RELOAD;
}

void C_AIController::UpdateFiring(int timeDelta)
{
    m_fire.burstTime -= timeDelta;
    if (m_fire.burstTime < 0)
        m_fire.burstTime = 0;
    C_Item *weapon = m_handsItem;
    const bool reloading = weapon->GetKind() == ITEM_KIND_WEAPON && static_cast<C_WeaponItem *>(weapon)->IsReloadPending();
    if (weapon->GetAmount() != 0.0f && !reloading) {
        // A human target out of view is fired at only while the sight module still counts it
        // (1, defensive AI 3; never in zombie mode).
        C_Actor *target = m_fire.target;
        if (target && target->GetType() == ACTOR_HUMAN) {
            bool visible = false;
            int out = 0;
            const int count = g_sightModule.CanSee(m_human->GetSightId(), static_cast<C_Human *>(target)->GetSightTargetId(),
                                                   &out, &visible);
            if (!visible) {
                if (m_moveFlags & MOVE_AI_ZOMBIE)
                    return;
                if (m_moveFlags & MOVE_AI_DEFENSIVE) {
                    if (count < 3)
                        return;
                } else if (count < 1) {
                    return;
                }
            }
        }
        m_axes[AXIS_FIRE] = 1.0f;
        m_buttons[BUTTON_FIRE] = 1.0f;
        return;
    }
    // Empty (or reloading): fire on while the burst lasts (4 times in 5), else reload.
    if (m_fire.burstTime != 0 && RandFloat() <= 0.8f) {
        m_axes[AXIS_FIRE] = 1.0f;
        return;
    }
    m_fire.flags |= S_AIFire::FLAG_RELOAD;
}

uint32_t C_AIController::CheckFireOrder()
{
    C_Item *weapon = m_handsItem;
    if (!weapon) {
        if (m_moveFlags & MOVE_FIRING)
            StopFiring(400);
        return AI_FIRE_NO_WEAPON;
    }
    const uint32_t now = GetGame()->GetMissionTime();
    if (now < weapon->GetTime())
        return AI_FIRE_BUSY;
    if (static_cast<int>(now - AttackTimeOf(weapon, now)) < 0)
        return AI_FIRE_BUSY;
    if (now < ReadyTimeOf(weapon))
        return AI_FIRE_NOT_READY;
    if (m_fire.pauseTime > 0)
        return AI_FIRE_PAUSE;
    if ((m_moveFlags & MOVE_FIRING) && m_fire.burstTime == 0) {
        StopFiring(static_cast<uint32_t>(static_cast<int>(RandFloat() * 400.0f + 300.0f)));
        return AI_FIRE_PAUSE;
    }
    // The direction to fire in: the order's direction, or at the target (an empty weapon
    // without ammo left aims at the body frame of a human target).
    S_vector4 dir;
    C_Actor *target = m_fireOrder->target;
    if (!target) {
        dir = m_fireOrder->fireDir;
    } else if (weapon->GetAmount() == 0.0f && m_human->CountAmmo(weapon->GetDef()->GetAmmoItemId(), false) == 0) {
        if (target->GetType() != ACTOR_HUMAN)
            dir = GetDirToFrame(target->GetFrame());
        else
            dir = GetDirToFrame(static_cast<C_Human *>(target)->GetBodyFrame());
    } else {
        dir = GetAimDirTo(target);
    }
    if (m_facing->SetView(dir) == 1)
        return AI_FIRE_MUST_TURN;
    // A client waits until the weapon points along the view.
    if (IsClient() && (m_moveFlags & MOVE_FIRING)) {
        const S_vector4 view = m_facing->view;
        if (I3D_frame *frame = weapon->GetFrame(0)) {
            S_vector axis = S_vector(frame->GetMatrix()(2)).Normalized();
            axis.Normalize();
            if (view.x * axis.x + axis.y * view.y + axis.z * view.z < 0.95f)
                return AI_FIRE_NOT_AIMED;
        }
    }
    target = m_fireOrder->target;
    // (A friendly target skips the line-of-fire test.)
    if ((!target || !m_human->IsFriendOf(target)) && !Unknown114(&XYZ(m_facing->view)))
        return AI_FIRE_BLOCKED;
    target = m_fireOrder->target;
    if (target && target->GetType() == ACTOR_HUMAN && static_cast<C_Human *>(target)->GetStat(STAT_HEALTH) < 1) {
        if (m_moveFlags & MOVE_FIRING)
            StopFiring(400);
        return AI_FIRE_TARGET_DEAD;
    }
    return 0;
}

uint32_t C_AIController::CheckReload()
{
    if (!(m_fire.flags & S_AIFire::FLAG_RELOAD)) {
        if (m_fire.burstTime == 0) {
            StopFiring(static_cast<uint32_t>(static_cast<int>(RandFloat() * 400.0f + 300.0f)));
            return AI_FIRE_PAUSE;
        }
        return 0;
    }
    StopFiring(static_cast<uint32_t>(ReloadTimeOf(m_handsItem) + 100));
    // The squad does not reload without spare ammunition (the others always can).
    if (m_type == HUMAN_CONTROLLER_SQUAD &&
        m_human->CountAmmo(m_handsItem->GetDef()->GetAmmoItemId(), false) == 0)
        return AI_FIRE_NO_AMMO;
    PressReload();
    return AI_FIRE_RELOADING;
}

uint32_t C_AIController::UpdateFireOrder(int timeDelta)
{
    S_AIFireOrder *order = m_fireOrder;
    if (order->throwGrenade) {
        m_fire.throwPos = order->throwPos;
        ThrowGrenade();
        m_fireOrder->throwGrenade = false;
        return 0;
    }
    if (order->reload) {
        if (m_handsItem && m_human->CountAmmo(m_handsItem->GetDef()->GetAmmoItemId(), false) == 0) {
            m_fireOrder->result = AI_FIRE_NO_AMMO;
            return AI_FIRE_NO_AMMO;
        }
        PressReload();
        // (Fix: the original reads the reload time of a missing weapon.)
        if (m_handsItem)
            StopFiring(static_cast<uint32_t>(ReloadTimeOf(m_handsItem) + 100));
        m_fireOrder->reload = false;
        m_fireOrder->result = AI_FIRE_RELOADING;
        return AI_FIRE_RELOADING;
    }
    C_Actor *target = order->target;
    uint32_t result;
    if (m_moveFlags & MOVE_FIRING) {
        if (!target && Xyz4Square(order->fireDir) < 1e-16f) {
            StopFiring(100);
            m_fireOrder->result = AI_FIRE_NOTHING;
            return AI_FIRE_NOTHING;
        }
        result = CheckFireOrder();
        if (result & AI_FIRE_STOP_MASK) {
            StopFiring(100);
            m_fireOrder->result = result;
            return result;
        }
        if (!(result & AI_FIRE_WAIT_MASK)) {
            UpdateFiring(timeDelta);
            result = CheckReload();
            if (result & AI_FIRE_STOP_MASK)
                StopFiring(100);
            m_fireOrder->result = result;
            return result;
        }
        if (m_fireOrder->target != m_fire.target) {
            StopFiring(100);
            m_fireOrder->result = AI_FIRE_NOT_AIMED;
            return AI_FIRE_NOT_AIMED;
        }
        m_fireOrder->result = result;
        return result;
    }
    if (!target) {
        if (Xyz4Square(order->fireDir) < 1e-16f) {
            m_fireOrder->result = AI_FIRE_NOTHING;
            return AI_FIRE_NOTHING;
        }
        m_fire.flags = S_AIFire::FLAG_DIRECTION;
        m_fire.target = nullptr;
    } else {
        m_fire.target = target;
        m_fire.orderTarget = order->target;
        m_fire.flags = 0;
    }
    result = CheckFireOrder();
    if (result == 0) {
        StartFiring();
        result = AI_FIRE_STARTED;
    }
    m_fireOrder->result = result;
    return result;
}

// ------------------------------------------------------------------------- the stance

void C_AIController::UpdateCheckpointStance()
{
    // The walking point the human is at asks for a stance (crouch / lie points; at the points
    // of types 4 and 6 before their waypoints it stands up).
    const S_Checkpoint *point = m_currentCheckpoint;
    if (!point)
        return;
    if (!m_waypoints.empty() &&
        ((point->type == CHECKPOINT_TYPE_4 && m_waypoints[0]->type == 4) ||
         (point->type == CHECKPOINT_TYPE_6 && m_waypoints[0]->type == 5))) {
        const S_vector offset = point->pos - m_human->GetFrame()->GetWorldPos();
        const float square = offset.Square();
        if (square < 1e-8f || std::fabs(square - 1.0f) < 1e-8f || std::sqrt(square) < 1.5f) {
            if (m_moveFlags & MOVE_STANDING)
                return;
            if (m_moveFlags & MOVE_CROUCHING)
                m_buttons[BUTTON_CROUCH] = 1.0f;
            else if (m_moveFlags & MOVE_LYING)
                m_buttons[BUTTON_LIE] = 1.0f;
            return;
        }
    }
    if (!IsHumanWithin(&point->pos, static_cast<float>(point->radius) * 0.0625f))
        return;
    switch (point->type) {
    case CHECKPOINT_TYPE_CROUCH:
        if (!(m_moveFlags & MOVE_CROUCHING))
            m_buttons[BUTTON_CROUCH] = 1.0f;
        return;
    case CHECKPOINT_TYPE_LIE:
        if (!(m_moveFlags & MOVE_LYING))
            m_buttons[BUTTON_LIE] = 1.0f;
        return;
    default:
        if (!(m_stance & m_moveFlags))
            SetStance(static_cast<int>(m_stance));
        return;
    }
}

void C_AIController::SetStance(int stance)
{
    // Not while carrying a body, during a special move, or in animation state 0x5F.
    if (m_human->GetCarried())
        return;
    const C_HumanMovement *movement = m_human->GetMovement();
    if (movement->GetUnknownA30() > 0)
        return;
    if (movement->GetState() == 0x5F)
        return;
    if (m_type == HUMAN_CONTROLLER_SQUAD) {
        const char *name = stance == MOVE_LYING ? "E_BF_LIE"
                           : stance == MOVE_CROUCHING ? "E_BF_CROUCH"
                           : stance == MOVE_STANDING ? "E_BF_STAND"
                                                    : "E_BF_UNKNOW";
        GetGame()->Print("%s %s", m_human->GetShortName(), name);
    }
    const int pointType = m_currentCheckpoint ? m_currentCheckpoint->type : 0;
    const auto backToCover = [this]() {
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_BACK_TO_COVER));
        const S_vector4 zero = {};
        RequestAction(CONTROLLER_ACTION_BACK_TO_COVER, &zero, &zero, 0.0f, -1, -1);
    };
    if (stance == MOVE_STANDING) {
        if (m_moveFlags & MOVE_STANDING)
            return;
        m_stance = MOVE_STANDING;
        if (pointType == CHECKPOINT_TYPE_CROUCH || pointType == CHECKPOINT_TYPE_LIE)
            return;
        if (m_moveFlags & MOVE_UNKNOWN_8000000)
            backToCover();
        if (m_moveFlags & MOVE_CROUCHING)
            m_buttons[BUTTON_CROUCH] = 1.0f;
        else if (m_moveFlags & MOVE_LYING)
            m_buttons[BUTTON_LIE] = 1.0f;
    } else if (stance == MOVE_CROUCHING) {
        if (m_moveFlags & MOVE_CROUCHING)
            return;
        m_stance = MOVE_CROUCHING;
        if (pointType == CHECKPOINT_TYPE_LIE)
            return;
        m_buttons[BUTTON_CROUCH] = 1.0f;
    } else if (stance == MOVE_LYING) {
        if (m_moveFlags & MOVE_LYING)
            return;
        if (m_moveFlags & MOVE_UNKNOWN_8000000)
            backToCover();
        m_stance = MOVE_LYING;
        m_buttons[BUTTON_LIE] = 1.0f;
        // Lying down delays the next burst.
        if (m_fire.pauseTime < 300)
            m_fire.pauseTime = 300;
        return;
    } else {
        return;
    }
    // Getting up from lying delays the next burst.
    if (!(m_moveFlags & MOVE_LYING))
        return;
    if (m_fire.pauseTime < 300)
        m_fire.pauseTime = 300;
}
