// The weapons of C_PlayerController (original source 0x00542b80..0x005433b0,
// 0x00544d20..0x00545b90): firing with the spread and the recoil, reloading, the empty click,
// the knife (from behind: a backstab), grenades thrown with a charge (from the hands or the
// belt), mines and car charges, the sights, the scopes and the binoculars, raising and lowering
// the weapon.
#include "game/first_person_view.h"
#include "game/human_controller.h"

#include <cstdint>

#include "game/collision.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/player_internal.h"
#include "game/profile.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// A throw: charged for 1 s at most (its strength the share of it, at most 0.85 crouching and
// 0.7 lying); the hands are busy for a while after, the next throw waits 2.5 s.
constexpr int MAX_CHARGE_TIME = 1000;
constexpr float MAX_STRENGTH_CROUCHING = 0.85f;         // 0x3F59999A
constexpr float MAX_STRENGTH_LYING = 0.7f;              // 0x3F333333
constexpr int HANDS_LOCK_BELT_THROW = 1000;
constexpr int HANDS_LOCK_THROW = 1500;
constexpr int THROW_DELAY = 2500;

// The knife: from behind (the victim's facing within about 37 degrees of the strike) on a
// standing human, a backstab.
constexpr float BACKSTAB_COS = 0.8f;

// Firing without the HUD: the camera's ray finds the target within 500 m; a target nearer
// than 3 m keeps the camera's direction.
constexpr float AIM_RANGE = 500.0f;
constexpr float AIM_MIN_SQUARE = 9.0f;
constexpr uint32_t AIM_RAY_EXCLUDE = 0x8001;

// The click of an empty weapon repeats after 200..300 ms while the trigger is held.
constexpr float EMPTY_CLICK_DELAY = 200.0f;
constexpr float EMPTY_CLICK_RANDOM = 100.0f;

// The binoculars' key; the view's jitter when they are put away.
constexpr int BUTTON_BINOCULARS = 0xF;
constexpr float BINOCULARS_JITTER = 0.01f;

// The recoil's scale per weapon behaviour, the scope's aim modes that keep it (the reticles
// 0..3 of behaviour 4), the recoil's share of the view.
constexpr int SCOPE_AIM_MODE_IRON_SIGHTS = 5;
constexpr int SCOPE_RETICLE_MODES = 4;
constexpr float RECOIL_VIEW_SCALE = 0.2f;
constexpr int DIFFICULTY_NO_RECOIL_CHEAT = 4;           // the "heal" cheat's recoil stays on this difficulty
constexpr int DIFFICULTY_EASY = 1;
constexpr int DIFFICULTY_MEDIUM = 2;

// The moving flags that lower a raised weapon of behaviour 5 (anything but lying still).
constexpr uint32_t MOVE_NOT_LYING_STILL = 0xFE;
// Behaviour 6 (launchers) wants a still human, not lying.
constexpr uint32_t MOVE_LAUNCHER_BLOCK = 0xF9;

// The weapon behaviours with a firearm's controls (the pistols and 4 single shots: the held
// fire key does not repeat; 6 the launchers).
bool IsFirearm(int behaviour)
{
    switch (behaviour) {
    case ITEM_BEHAVIOUR_SIDEARM:
    case 3:
    case 4:
    case 5:
    case ITEM_BEHAVIOUR_LAUNCHER:
    case 7:
    case 12:
        return true;
    default:
        return false;
    }
}

// A throw's strength from the charge.
float ThrowStrength(int chargeTime, uint32_t moveFlags)
{
    float strength = static_cast<float>(chargeTime) * 0.001f;
    if (moveFlags & MOVE_CROUCHING) {
        if (strength > MAX_STRENGTH_CROUCHING)
            strength = MAX_STRENGTH_CROUCHING;
    } else if ((moveFlags & MOVE_LYING) && strength > MAX_STRENGTH_LYING) {
        strength = MAX_STRENGTH_LYING;
    }
    return strength;
}

// The hands are busy until `time`. (Fix: the original calls through a missing container.)
void LockHands(C_Human *human, uint32_t time)
{
    if (C_ItemContainer *hands = human->GetContainer(ITEM_CONTAINER_HANDS))
        hands->SetLockTime(time);
}

// The weapon's attack delay of its component (ms).
int AttackDelayOf(const C_Item *item)
{
    return static_cast<int>(static_cast<const C_WeaponComp *>(item->GetDef()->GetComponent())->GetAttackDelay());
}

const S_vector4 ZERO = {0.0f, 0.0f, 0.0f, 0.0f};

}  // namespace

// ------------------------------------------------------------------------- the basis of the spread

void BuildBasis(const S_vector &dir, S_vector &side, S_vector &up)
{
    S_vector across(dir.y * 0.0f - dir.z, dir.z * 0.0f - dir.x * 0.0f, dir.x - dir.y * 0.0f);
    NormalizeBy(across, (across.x * across.x + across.y * across.y) + across.z * across.z);
    side = across.Cross(dir);
    NormalizeBy(side, (side.x * side.x + side.y * side.y) + side.z * side.z);
    up = side.Cross(dir);
    NormalizeBy(up, (up.x * up.x + up.y * up.y) + up.z * up.z);
}

// ------------------------------------------------------------------------- the controls

void C_PlayerController::WeaponControls(int timeDelta)
{
    m_quietTime += timeDelta;
    if (!m_buttons || !m_axes)
        return;
    // Network games: the spawn protection keeps the weapons quiet.
    if (!IsLocalGame() && m_human->m_aliveTime < g_mpSettings->spawnProtection * 1000)
        return;
    if (m_viewMode == VIEW_BINOCULARS)
        return;
    // In a vehicle: not the driver, nor at a seat weapon.
    if (C_Actor *vehicle = m_human->m_vehicle) {
        if (m_human->m_seat == 0)
            return;
        if (vehicle->HasSeatWeapon(m_human->m_seat))
            return;
    }
    if (!GetGame()->GetFirstPersonView()->CanUseWeapon())
        return;
    if (m_human->m_body.GetEquipmentFlag1())
        return;
    UpdateHandsItem();
    const uint32_t now = GetGame()->GetMissionTime();

    // A grenade from the belt: charged while the key is held.
    if (m_buttons[BUTTON_THROW] != 0.0f && !m_charging) {
        if ((m_moveFlags & MOVE_LYING) && (m_moveFlags & MOVE_MOVING_MASK))
            return;
        if (m_viewMode != VIEW_NORMAL)
            return;
        if (m_handsItem) {
            // (Fix: the original reads a weapon's field of any item.)
            if (m_handsItem->GetKind() == ITEM_KIND_WEAPON && now < static_cast<C_WeaponItem *>(m_handsItem)->GetReadyTime())
                return;
            if (now < m_handsItem->GetTime())
                return;
            if (BehaviourOf(m_handsItem) == ITEM_BEHAVIOUR_THROWN)
                return;
        }
        if (m_throwDelay != 0)
            return;
        C_Item *grenade = m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN);
        if (!grenade)
            return;
        m_charging = true;
        m_chargeTime = 0;
        m_chargeAction = BODY_ACTION_THROW_BELT_GRENADE;
        m_human->SetActionProgress(MAX_CHARGE_TIME, grenade->GetDef()->GetTextId());
        return;
    }
    if (UpdateCharge(timeDelta))
        return;
    if (!m_handsItem || m_charging)
        return;

    const int behaviour = BehaviourOf(m_handsItem);
    switch (behaviour) {
    case ITEM_BEHAVIOUR_MELEE:
        if (m_buttons[BUTTON_FIRE] != 0.0f)
            MeleeStrike();
        return;
    case ITEM_BEHAVIOUR_THROWN:
        // A grenade in the hands: the throw (or the second one) is charged while held.
        if ((m_moveFlags & MOVE_LYING) && (m_moveFlags & MOVE_MOVING_MASK))
            return;
        if (m_throwDelay != 0)
            return;
        if (m_buttons[BUTTON_FIRE] != 0.0f) {
            m_chargeAction = BODY_ACTION_THROW;
        } else if (m_buttons[BUTTON_7] != 0.0f) {
            m_chargeAction = BODY_ACTION_THROW_2;
        } else {
            return;
        }
        m_charging = true;
        m_chargeTime = 0;
        m_human->SetActionProgress(MAX_CHARGE_TIME, m_handsItem->GetDef()->GetTextId());
        return;
    case ITEM_BEHAVIOUR_MINE:
        // Lays the mine (the human's own id with the command).
        if (m_buttons[BUTTON_FIRE] != 0.0f && m_interaction == -1) {
            S_HumanCommand command;
            command.category = HUMAN_COMMAND_VEHICLE;
            command.type = HUMAN_COMMAND_LAY_MINE;
            command.param1 = m_human->GetNetId();
            command.param2 = 0;
            IssueCommand(command);
        }
        return;
    case ITEM_BEHAVIOUR_STICKY_CHARGE: {
        // A charge on the car looked at.
        if (m_buttons[BUTTON_FIRE] == 0.0f || m_interaction != -1)
            return;
        S_vector hitPos;
        S_vector hitNormal;
        const S_CollisionVolume *volume = g_collisionWorld.RayTest(m_human->GetBodyFrame()->GetWorldPos(),
                                                                   XYZ(m_viewDir), &hitPos, &hitNormal, 0, 0);
        if (!volume || !(volume->flags & 0xC0))
            return;
        C_Actor *car = GetFrameActor(volume->frame);
        if (!car || car->GetType() != ACTOR_CAR || static_cast<C_Car *>(car)->IsDestroyed())
            return;
        S_HumanCommand command;
        command.category = HUMAN_COMMAND_VEHICLE;
        command.type = HUMAN_COMMAND_PLACE_CAR_CHARGE;
        command.param1 = car->GetNetId();
        command.param2 = 0;
        IssueCommand(command);
        return;
    }
    default:
        if (!IsFirearm(behaviour))
            return;
        break;
    }

    // --- the firearms
    C_WeaponItem *weapon = static_cast<C_WeaponItem *>(m_handsItem);
    if (behaviour == ITEM_BEHAVIOUR_LAUNCHER && (m_moveFlags & MOVE_MOVING_MASK)) {
        // A launcher does nothing on the move.
        m_buttons[BUTTON_RELOAD] = 0.0f;
        m_buttons[BUTTON_FIRE] = 0.0f;
    }
    if (behaviour == ITEM_BEHAVIOUR_SIDEARM || behaviour == 4 || behaviour == ITEM_BEHAVIOUR_LAUNCHER)
        m_axes[AXIS_FIRE] = 0.0f;                       // no automatic fire
    if (m_buttons[BUTTON_FIRE] == 0.0f && m_axes[AXIS_FIRE] == 0.0f) {
        // The reload key, when the human is free for it and the weapon not full.
        if (m_buttons[BUTTON_RELOAD] == 0.0f)
            return;
        if (behaviour == 5 && (m_moveFlags & MOVE_NOT_LYING_STILL))
            return;
        if (m_human->m_body.GetClimbState() != 0 || m_human->m_body.IsWorkState5() || m_human->m_carried)
            return;
        if ((m_moveFlags & MOVE_LYING) && (m_moveFlags & MOVE_MOVING_MASK))
            return;
        const float amount = weapon->GetAmount();
        if (weapon->GetMaxAmount() == amount)
            return;
        int ammoUid = -1;
        if (!weapon->Reload(m_human, ITEM_CONTAINER_BELT, &ammoUid, false))
            return;
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RELOAD));
        RequestAction(CONTROLLER_ACTION_RELOAD, &ZERO, &ZERO, 0.0f, ammoUid, static_cast<int>(weapon->GetLoadedRounds()));
        return;
    }
    if (!m_unknownAC)
        return;
    if (weapon->GetAmount() == 0.0f || weapon->IsReloadPending()) {
        // Empty: the trigger reloads, else it clicks.
        int ammoUid = -1;
        if (m_buttons[BUTTON_FIRE] != 0.0f && weapon->Reload(m_human, ITEM_CONTAINER_BELT, &ammoUid, false)) {
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RELOAD));
            weapon->ClearReloadPending();
            RequestAction(CONTROLLER_ACTION_RELOAD, &ZERO, &ZERO, 0.0f, ammoUid,
                          static_cast<int>(weapon->GetLoadedRounds()));
            return;
        }
        if (m_buttons[BUTTON_FIRE] == 0.0f) {
            const float elapsed = static_cast<float>(static_cast<int>(now - weapon->GetAttackTime()));
            if (!(RandFloat() * EMPTY_CLICK_RANDOM + EMPTY_CLICK_DELAY <= elapsed))
                return;
        }
        weapon->PlayEmptySound(m_human);
        weapon->SetAttackTime(GetGame()->GetMissionTime());
        GetGame()->GetFirstPersonView()->OnInventoryEvent(FPV_EVENT_JAMMED, 0);
        RequestAction(CONTROLLER_ACTION_EMPTY, &ZERO, &ZERO, 0.0f, -1, -1);
        return;
    }
    if (static_cast<int>(now - weapon->GetAttackTime()) < 0)
        return;
    // From where the player aims: the HUD's aim, or the weapon's muzzle at what the camera
    // looks at.
    S_vector4 origin;
    S_vector4 dir = ZERO;
    C_game *game = GetGame();
    if (game->IsFirstPersonView()) {
        origin = game->GetFirstPersonView()->GetAimOrigin();
        dir = game->GetFirstPersonView()->GetAimDir();
    } else {
        origin = WorldPos4Of(ShotFrameOf(weapon));
        C_CameraModule *camera = game->GetCameraModule();
        S_vector4 cameraPos;
        camera->GetCameraPos(0, &cameraPos);
        camera->GetCameraDir(1, &dir);
        m_human->m_body.RemoveCollision(false);
        const S_vector ray(dir.x * AIM_RANGE, dir.y * AIM_RANGE, dir.z * AIM_RANGE);
        S_vector hit;
        const S_CollisionVolume *volume = g_collisionWorld.RayTest(XYZ(cameraPos), ray, &hit, nullptr, AIM_RAY_EXCLUDE, 0);
        m_human->m_body.RestoreCollision(false);
        if (volume) {
            S_vector toTarget(hit.x - origin.x, hit.y - origin.y, hit.z - origin.z);
            const float square = (toTarget.z * toTarget.z + toTarget.y * toTarget.y) + toTarget.x * toTarget.x;
            if (AIM_MIN_SQUARE < square) {
                NormalizeBy(toTarget, square);
                dir = ToVector4(toTarget, dir.w);
            }
        }
    }
    ApplySpread(reinterpret_cast<S_vector &>(dir));
    weapon->Fire(m_human, XYZ(origin), XYZ(dir), 0);
    UpdateHandsItem();
    if (!m_handsItem)
        return;
    ApplyRecoil(m_viewDir);
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_FIRE));
    GetGame()->GetFirstPersonView()->OnInventoryEvent(m_handsItem->GetAmount() == 0.0f ? FPV_EVENT_FIRE_LAST : FPV_EVENT_FIRE, 0);
    static_cast<C_WeaponItem *>(m_handsItem)->SetAttackTime(GetGame()->GetMissionTime() + AttackDelayOf(m_handsItem));
    RequestAction(CONTROLLER_ACTION_FIRE, &origin, &dir, 0.0f, -1, -1);
    m_quietTime = 0;
    // Dropped: a copy protection check: in single player, a failed check now and then
    // (rand() < 163.835) made the player drop the weapon (thrown along the view).
}

void C_PlayerController::MeleeStrike()
{
    if (!m_unknownAC || (m_moveFlags & MOVE_LYING))
        return;
    C_Item *item = m_handsItem;
    if (!item || item->GetKind() != ITEM_KIND_WEAPON)   // (Fix: the original reads a missing item)
        return;
    C_WeaponItem *knife = static_cast<C_WeaponItem *>(item);
    if (GetGame()->GetMissionTime() < knife->GetTime())
        return;
    if (static_cast<int>(GetGame()->GetMissionTime() - knife->GetAttackTime()) < 0)
        return;
    bool backstab = false;
    if (IsLocalGame()) {
        // Single player: a standing human struck from behind is stabbed in the back.
        C_game *game = GetGame();
        const S_vector4 dir = game->GetFirstPersonView()->GetAimDir();
        const S_vector4 origin =
            game->IsFirstPersonView() ? game->GetFirstPersonView()->GetAimOrigin() : WorldPos4Of(m_human->GetBodyFrame());
        m_human->m_body.RemoveCollision(false);
        const S_CollisionVolume *volume = g_collisionWorld.RayTestDynamic(XYZ(origin), XYZ(dir), nullptr, nullptr, 0, 0);
        m_human->m_body.RestoreCollision(false);
        C_Actor *actor = volume ? volume->actor : nullptr;
        if (actor && actor->GetType() == ACTOR_HUMAN) {
            C_Human *victim = static_cast<C_Human *>(actor);
            if (victim->GetStance() == 0) {
                const S_vector &facing = victim->GetFacingDir();
                if ((dir.z * facing.z + dir.y * facing.y) + dir.x * facing.x > BACKSTAB_COS) {
                    m_human->SetDisguise(DISGUISE_LOSS_ACTION);
                    S_HumanCommand command;
                    command.category = HUMAN_COMMAND_VEHICLE;
                    command.type = HUMAN_COMMAND_BACKSTAB;
                    command.param1 = victim->GetNetId();
                    command.param2 = 0;                 // (a stale local in the original)
                    m_human->IssueCommand(command);
                    backstab = true;
                }
            }
        }
    }
    if (!backstab)
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_STAB));
    knife->PlayShotSound(m_human, knife->GetFrame(0), !IsLocalGame());
    knife->SetAttackTime(GetGame()->GetMissionTime() + AttackDelayOf(knife));
}

bool C_PlayerController::UpdateCharge(int timeDelta)
{
    if (!m_charging)
        return false;
    m_chargeTime += timeDelta;
    if (static_cast<float>(m_chargeTime) > static_cast<float>(MAX_CHARGE_TIME)) {
        m_chargeTime = MAX_CHARGE_TIME;
        m_human->SetActionProgress(0, 0);
    }
    if (m_human->m_body.GetEquipmentFlag1()) {
        m_charging = false;
        m_chargeTime = 0;
        m_human->SetActionProgress(0, 0);
        return true;
    }
    const uint32_t now = GetGame()->GetMissionTime();
    switch (m_chargeAction) {
    case BODY_ACTION_THROW_BELT_GRENADE: {
        if (m_axes[BUTTON_THROW] != 0.0f)
            return true;                                // still charging
        // Released: the belt's grenade goes.
        if (C_Item *grenade = m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN)) {
            const float strength = ThrowStrength(m_chargeTime, m_moveFlags);
            static_cast<C_WeaponItem *>(grenade)->SetThrowStrength(strength);
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, m_chargeAction, FrameArg(grenade->GetFrame(0))));
            RequestAction(CONTROLLER_ACTION_START_BELT_THROW, &ZERO, &ZERO, strength, -1, -1);
            GetGame()->GetFirstPersonView()->OnInventoryEvent(FPV_EVENT_BELT_GRENADE, 0);
            LockHands(m_human, now + HANDS_LOCK_BELT_THROW);
        }
        break;
    }
    case BODY_ACTION_THROW:
    case BODY_ACTION_THROW_2: {
        const int key = m_chargeAction == BODY_ACTION_THROW ? static_cast<int>(AXIS_FIRE) : static_cast<int>(BUTTON_7);
        if (m_axes[key] != 0.0f)
            return true;                                // still charging
        // Released: the grenade in the hands goes.
        if (m_handsItem && BehaviourOf(m_handsItem) == ITEM_BEHAVIOUR_THROWN) {
            const float strength = ThrowStrength(m_chargeTime, m_moveFlags);
            static_cast<C_WeaponItem *>(m_handsItem)->SetThrowStrength(strength);
            m_human->ExecuteBodyCommand(
                MakeBodyCommand(BODY_COMMAND_ACTION, m_chargeAction, FrameArg(m_handsItem->GetFrame(0))));
            const int action =
                m_chargeAction == BODY_ACTION_THROW ? CONTROLLER_ACTION_START_THROW : CONTROLLER_ACTION_START_THROW_2;
            RequestAction(action, &ZERO, &ZERO, strength, -1, -1);
            LockHands(m_human, now + HANDS_LOCK_THROW);
        }
        break;
    }
    default:
        return true;
    }
    m_charging = false;
    m_chargeTime = 0;
    m_human->SetActionProgress(0, 0);
    m_throwDelay = THROW_DELAY;
    return true;
}

// ------------------------------------------------------------------------- spread and recoil

void C_PlayerController::ApplySpread(S_vector &dir)
{
    // The weapon's dispersion scatters the shot in a random direction across it.
    const float spread = static_cast<C_WeaponComp *>(m_handsItem->GetDef()->GetComponent())->GetAimedDispersion();
    // Dropped: a copy protection check: in single player past mission 8, a failed check set
    // the spread to 50.
    if (spread == 0.0f)
        return;
    S_vector side;
    S_vector up;
    BuildBasis(dir, side, up);
    float r = RandFloat();
    const float k1 = (r - 0.5f) + (r - 0.5f);
    up *= k1;
    r = RandFloat();
    const float k2 = (r - 0.5f) + (r - 0.5f);
    side.y *= k2;
    side.z *= k2;
    up.x = side.x * k2 + up.x;
    up.y += side.y;
    up.z += side.z;
    NormalizeBy(up, (up.z * up.z + up.y * up.y) + up.x * up.x);
    const float scale = spread * 0.01f * RandFloat();
    dir.x = up.x * scale + dir.x;
    dir.y = up.y * scale + dir.y;
    dir.z = up.z * scale + dir.z;
    NormalizeBy(dir, (dir.z * dir.z + dir.y * dir.y) + dir.x * dir.x);
}

void C_PlayerController::ApplyRecoil(S_vector4 &view)
{
    // Single player: the "heal" console command turns it off (but on difficulty 4).
    if (IsLocalGame() && GetCheatValue(CHEAT_SNIPER) != 0 && g_gameProfile.GetDifficulty() != DIFFICULTY_NO_RECOIL_CHEAT)
        return;
    float recoil = GetAimBonus();
    if (recoil == 0.0f)
        return;
    C_ItemDef *def = m_handsItem->GetDef();
    switch (def->GetBehaviour()) {
    case ITEM_BEHAVIOUR_SIDEARM:
        recoil *= 0.8f;
        break;
    case 3:
        recoil *= 0.3f;
        break;
    case 4: {
        // Through a reticle scope it stays.
        const C_ItemComponent *scope = def->GetSecondaryComponent();
        if (scope && scope->GetKind() == ICK_SCOPE &&
            static_cast<const C_ScopeComp *>(scope)->GetAimMode() < SCOPE_RETICLE_MODES)
            break;
        recoil *= 0.4f;
        break;
    }
    case 5:
        recoil *= 0.4f;
        break;
    case 12:
        recoil *= 0.5f;
        break;
    default:
        break;
    }
    // Aimed, half of it; tired, up to twice as much.
    if (IsClient() && GetGame()->GetFirstPersonView()->IsAimViewOn())
        recoil *= 0.5f;
    recoil = ((1.0f - static_cast<float>(m_human->GetStaminaPercent()) * 0.01f) + 1.0f) * recoil;
    if (IsLocalGame()) {
        const int difficulty = g_gameProfile.GetDifficulty();
        if (difficulty == DIFFICULTY_EASY)
            recoil *= 0.3f;
        else if (difficulty == DIFFICULTY_MEDIUM)
            recoil *= 0.8f;
    }
    view.x = ((RandFloat() - 0.5f) * recoil) * RECOIL_VIEW_SCALE + view.x;
    view.y = ((RandFloat() * recoil) * RECOIL_VIEW_SCALE) + view.y;
    view.z = ((RandFloat() - 0.5f) * recoil) * RECOIL_VIEW_SCALE + view.z;
    S_vector &dir = reinterpret_cast<S_vector &>(view);
    NormalizeBy(dir, (dir.z * dir.z + dir.y * dir.y) + dir.x * dir.x);
}

// ------------------------------------------------------------------------- the view modes, raising

void C_PlayerController::UpdateZoom()
{
    if (m_human->m_body.IsWorkState5() || m_human->m_carried || m_human->m_body.GetClimbState() != 0)
        return;
    // In a vehicle: only a car's seat that keeps the hands item.
    C_Actor *vehicle = m_human->m_vehicle;
    if (vehicle && vehicle->GetType() == ACTOR_CAR) {
        if (!(vehicle->GetSeatFlags(m_human->m_seat) & SEAT_FLAG_ARMED))
            return;
    } else if (InVehicleSeat(vehicle)) {
        return;
    }
    if (!GetGame()->GetFirstPersonView()->CanUseWeapon())
        return;
    if (m_interaction != -1 && m_interactionTime >= 1)
        return;
    UpdateHandsItem();
    C_Item *item = m_handsItem;
    if (!item || !m_buttons || m_buttons[BUTTON_7] == 0.0f) {
        // The binoculars key takes them out, or puts away what the player looks through.
        if (!m_buttons || m_buttons[BUTTON_BINOCULARS] == 0.0f)   // (Fix: the original reads missing buttons)
            return;
        if (m_viewMode == VIEW_NORMAL) {
            SetViewMode(VIEW_BINOCULARS);
            return;
        }
        m_viewDir.x = (RandFloat() - 0.5f) * BINOCULARS_JITTER + m_viewDir.x;
        m_viewDir.y = (RandFloat() - 0.5f) * BINOCULARS_JITTER + m_viewDir.y;
        m_viewDir.z = (RandFloat() - 0.5f) * BINOCULARS_JITTER + m_viewDir.z;
        S_vector &view = reinterpret_cast<S_vector &>(m_viewDir);
        NormalizeBy(view, (view.x * view.x + view.y * view.y) + view.z * view.z);
    } else {
        // The second fire key looks through the weapon's scope (or the sights), or back.
        const C_ItemComponent *scope = item->GetDef()->GetSecondaryComponent();
        if (!scope || scope->GetKind() != ICK_SCOPE)
            return;
        if (item->GetKind() != ITEM_KIND_WEAPON)       // (Fix: a weapon's fields of any item)
            return;
        const C_WeaponItem *weapon = static_cast<const C_WeaponItem *>(item);
        const uint32_t now = GetGame()->GetMissionTime();
        if (weapon->IsReloadPending() || now < weapon->GetTime() || static_cast<int>(now - weapon->GetAttackTime()) < 0)
            return;
        if ((m_moveFlags & MOVE_MOVING_MASK) && (m_moveFlags & (MOVE_PACE_MARCH | MOVE_PACE_4)))
            return;
        if (BehaviourOf(item) == 5 && (m_moveFlags & MOVE_NOT_LYING_STILL))
            return;
        if (m_viewMode == VIEW_NORMAL) {
            const bool sights = static_cast<const C_ScopeComp *>(scope)->GetAimMode() == SCOPE_AIM_MODE_IRON_SIGHTS;
            SetViewMode(sights ? VIEW_IRON_SIGHTS : VIEW_SCOPE);
            return;
        }
    }
    SetViewMode(VIEW_NORMAL);
}

void C_PlayerController::UpdateWeaponReady(int timeDelta)
{
    if (!GetGame()->GetFirstPersonView()->CanUseWeapon()) {
        if (!m_charging)
            UpdateZoom();
        return;
    }
    if (m_charging)
        return;
    UpdateHandsItem();
    if (C_Actor *vehicle = m_human->m_vehicle) {
        if (vehicle->HasSeatWeapon(m_human->m_seat))
            return;
    }
    UpdateZoom();
    if (!m_handsItem) {
        if (m_weaponRaised)
            LowerWeapon();
        return;
    }
    const int behaviour = BehaviourOf(m_handsItem);
    const uint32_t flags = m_moveFlags;
    const bool moving = (flags & MOVE_MOVING_MASK) != 0;
    if (!m_weaponRaised) {
        if (m_buttons[BUTTON_FIRE] == 0.0f) {
            // The binoculars: put away when walking.
            if (m_viewMode == VIEW_BINOCULARS) {
                if (moving)
                    SetViewMode(VIEW_NORMAL);
                return;
            }
            // Raised for the sights and the scopes, for a lying behaviour 5 weapon, and for a
            // reload.
            if (m_viewMode != VIEW_IRON_SIGHTS && m_viewMode != VIEW_SCOPE &&
                (behaviour != 5 || !(flags & MOVE_LYING) || moving)) {
                if (m_buttons[BUTTON_RELOAD] == 0.0f)
                    return;
                if (behaviour == 5 && (flags & MOVE_NOT_LYING_STILL))
                    return;
            }
            RaiseWeapon();
            return;
        }
        // The trigger raises the weapon (behaviour 5 lying still, 6 standing still, not
        // marching or crawling).
        if (behaviour == 5) {
            if (moving || (flags & (MOVE_CROUCHING | MOVE_STANDING)))
                return;
        } else if (behaviour == ITEM_BEHAVIOUR_LAUNCHER && (flags & MOVE_LAUNCHER_BLOCK)) {
            return;
        }
        if (moving && (flags & (MOVE_PACE_MARCH | MOVE_LYING)))
            return;
        RaiseWeapon();
        return;
    }
    // Raised: lowered when the human moves in a way the weapon does not allow.
    bool lower = (flags & MOVE_LYING) && moving;
    if (!lower) {
        if (behaviour == ITEM_BEHAVIOUR_LAUNCHER)
            lower = moving || (flags & MOVE_LYING);
        else if (behaviour == 5)
            lower = (flags & MOVE_NOT_LYING_STILL) != 0;
    }
    if (lower) {
        SetViewMode(VIEW_NORMAL);
        LowerWeapon();
        m_raisedTime = 0;
        return;
    }
    if (m_viewMode == VIEW_BINOCULARS) {
        if (moving)
            SetViewMode(VIEW_NORMAL);
        else
            LowerWeapon();
        return;
    }
    if (m_viewMode == VIEW_IRON_SIGHTS || m_viewMode == VIEW_SCOPE) {
        if (moving && (flags & (MOVE_PACE_MARCH | MOVE_PACE_4))) {
            SetViewMode(VIEW_NORMAL);
            m_raisedTime = 0;
        }
        return;
    }
    if (moving && (flags & MOVE_PACE_MARCH)) {
        LowerWeapon();
        m_raisedTime = 0;
        return;
    }
    // Lowered again a while after its use (not a grenade).
    if (m_raisedTime != 0 && behaviour != ITEM_BEHAVIOUR_THROWN) {
        m_raisedTime -= timeDelta;
        if (m_raisedTime < 1) {
            m_raisedTime = 0;
            LowerWeapon();
        }
    }
}
