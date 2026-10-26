// C_SquadController, the player's soldiers the player does not control (game/human_controller.h;
// original source 0x00524c60..0x0053b1d0): the life cycle, the frame and tick updates, the idle
// member and the leader's formation, the order queue, the icons of the member's panel, the
// slots of the senses, the vehicles and saved games. The actions and the squad commands are
// in squad_actions.cpp, the fighting in squad_combat.cpp, the stepping aside and the pace in
// squad_movement.cpp; the orders themselves in squad_order.cpp.
#include "game/human_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <vector>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/ai_parts.h"
#include "game/checkpoints.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_body.h"
#include "game/human_movement.h"
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

// Saved game chunks.
enum : uint16_t {
    CHUNK_SQUAD = 0xCEF,
    CHUNK_SQUAD_VERSION = 200,                          // 1
    CHUNK_SQUAD_AI = 300,                               // C_AIController::SaveGame
    CHUNK_SQUAD_ENGAGE_POS = 400,
    CHUNK_SQUAD_BOUND_DIR = 500,
    CHUNK_SQUAD_HOLD_DIR = 0x226,
    CHUNK_SQUAD_PACE = 600,
    CHUNK_SQUAD_FLAGS = 700,
    CHUNK_SQUAD_ORDER_STANCE = 800,
    CHUNK_SQUAD_COVER_STANCE = 900,
    CHUNK_SQUAD_ORDER_PACE = 1000,
    CHUNK_SQUAD_IDLE_CHECKPOINT = 0x44C,
    CHUNK_SQUAD_QUIET = 0x4B0,
    CHUNK_SQUAD_FOLLOWING = 0x514,
    CHUNK_SQUAD_SILENT_MODE = 0x578,
    CHUNK_SQUAD_LYING_TO_FIRE = 0x5DC,
    CHUNK_SQUAD_STEPPED_ASIDE = 0x640,
    CHUNK_SQUAD_ORDERS_HELD = 0x672,
    CHUNK_SQUAD_RESTART_ORDER = 0x677,
    CHUNK_SQUAD_BOUND_HOPS = 0x6A4,
    CHUNK_SQUAD_ENGAGE_TIME = 0x708,
    CHUNK_SQUAD_EVENT_TIME = 0x76C,
    CHUNK_SQUAD_ALERT_TIME = 2000,
    CHUNK_SQUAD_WEAPON_CHECK_TIME = 0x834,
    CHUNK_SQUAD_FORMATION_TIME = 0x898,
    CHUNK_SQUAD_FIRE_MOVE_TIME = 0x8FC,
    CHUNK_SQUAD_FOLLOW_DELAY = 0x9C4,
    CHUNK_SQUAD_STATUS_ICON_TIME = 0xA28,
    CHUNK_SQUAD_MODE_ICON_TIME = 0xA8C,
    CHUNK_SQUAD_ORDERS_CHANGED = 0xAF0,
    CHUNK_SQUAD_COVER_RADIUS = 0xB54,
    CHUNK_SQUAD_FRIENDLY_FIRE = 3000,
    CHUNK_SQUAD_STATUS_ICON = 0xC1C,
    CHUNK_SQUAD_MODE_ICON = 0xC80,
    CHUNK_SQUAD_TEMP_STATUS_ICON = 0xCE4,
    CHUNK_SQUAD_TEMP_MODE_ICON = 0xD48,
    CHUNK_SQUAD_COMBAT_MODE = 0xDAC,
    CHUNK_SQUAD_ACTION = 0xE10,
    CHUNK_SQUAD_ORDERS = 0xE74,
    CHUNK_SQUAD_GUNNER_LOOKS_AHEAD = 0xED8,
};
// An order of the queue (in CHUNK_SQUAD_ORDERS).
enum : uint16_t {
    CHUNK_ORDER = 0x1A0A,
    CHUNK_ORDER_KIND = 1000,
    CHUNK_ORDER_ARG = 0x44C,
    CHUNK_ORDER_POS = 0x4B0,                            // the actor's position when it has one
    CHUNK_ORDER_DATA = 0x514,
    CHUNK_ORDER_PACE = 0x578,
    CHUNK_ORDER_STANCE = 0x5DC,
    CHUNK_ORDER_MODE = 0x640,
    CHUNK_ORDER_ACTOR = 0x6A4,                          // its id, -1 none
    CHUNK_ORDER_TEMPORARY = 0x6A5,                      // the actor is a temporary one
    CHUNK_ORDER_UNKNOWN70 = 0x708,
};
// The action (S_SquadAction::SaveGame).
enum : uint16_t {
    CHUNK_ACTION = 100,
    CHUNK_ACTION_POS = 200,
    CHUNK_ACTION_DIR = 0xD2,
    CHUNK_ACTION_ARRIVED = 300,
    CHUNK_ACTION_TURNED = 400,
    CHUNK_ACTION_FINISHED = 500,
    CHUNK_ACTION_KEEP_GOING = 600,
    CHUNK_ACTION_NO_COMMAND = 700,
    CHUNK_ACTION_STARTED = 800,
    CHUNK_ACTION_COMPLETED = 900,
    CHUNK_ACTION_SKIP_TURN = 1000,
    CHUNK_ACTION_NOTIFY_MODULE = 0x44C,
    CHUNK_ACTION_USE_DIR = 0x456,
    CHUNK_ACTION_TARGET_ID = 0x4B0,
    CHUNK_ACTION_SLOT = 0x514,
    CHUNK_ACTION_DELAY = 0x578,
    CHUNK_ACTION_RADIUS = 0x5DC,
    CHUNK_ACTION_TYPE = 0x640,
    CHUNK_ACTION_TARGET = 0x6A4,                        // its id, -1 none
    CHUNK_ACTION_TARGET_TEMPORARY = 0x6A5,
    CHUNK_ACTION_STANCE = 0x708,
};

// Positions and directions are saved without w.
constexpr int SAVED_VECTOR_SIZE = 12;
// At most this many orders queue up.
constexpr int MAX_ORDERS = 10;
// The flag of a vehicle seat with its own camera (C_Actor::GetSeatFlags).
constexpr uint32_t SEAT_FLAG_CAMERA = 0x10;

template <class T>
void ReadIfFits(C_chunk &chunk, T &value)
{
    if (chunk.RemainingSize() >= static_cast<int>(sizeof(T)))
        chunk.Read(value);
}

// A saved position (12 bytes; the w stays).
void ReadVector(C_chunk &chunk, S_vector4 &v)
{
    chunk.Read(&v, SAVED_VECTOR_SIZE);
}

// The saved actor reference: its id and whether it is a temporary actor.
C_Actor *FindSavedActor(int id, bool temporary)
{
    if (id < 0)
        return nullptr;
    if (temporary)
        return g_temporaryActors.FindActor(id);
    return g_actorsModule.GetActorById(id);
}

}  // namespace

// ------------------------------------------------------------------------- life cycle

C_SquadController::C_SquadController()
    : C_AIController(HUMAN_CONTROLLER_SQUAD)
{
}

C_SquadController *C_SquadController::Create()
{
    return new C_SquadController;
}

C_SquadController::~C_SquadController()
{
    // Fix: the original frees only the queue's storage (Detach has released the orders).
    for (S_HumanOrder *order : m_orders)
        ReleaseHumanOrder(order);
}

void C_SquadController::Attach(C_Human *human, C_HumanController *previous)
{
    C_AIController::Attach(human, previous);
    const bool alive = m_human->m_stats[STAT_HEALTH] > 0;
    m_ordersChangedTime = 0;
    m_orderStance = 0;
    m_orderPace = 0;
    m_pace = MOVE_PACE_3;
    m_coverStance = 0;
    m_flags = 0;
    m_friendlyFire = 0.0f;
    m_weaponCheckTime = 0;
    m_steppedAside = false;
    m_formationTime = 0;
    m_following = true;
    m_quiet = false;
    m_silentMode = false;
    m_alertTime = 200;
    if (alive)
        SetCombatOrder(SQUAD_ORDER_MODE0);
    m_boundHops = 0;
    m_followDelay = 0;
    m_lyingToFire = false;
    m_fireMoveTime = 0;
    m_airCheckTime = 0;
    m_airTarget = nullptr;
    m_airTargetFrame = nullptr;
    m_holdDir = Dir4(m_human->GetFacingDir());
    m_restartOrder = false;
    SetFollow(false, true);
    m_tempStatusIcon = -1;
    m_tempModeIcon = -1;
    m_statusIconTime = 0;
    m_modeIconTime = 0;
    m_ordersHeld = false;
    m_mineNear = false;
    m_mineCheckTime = 100;
    if (!previous) {
        m_flags = 0;
    } else {
        // What the member did under the previous controller goes on.
        bool reportEnemies = true;
        switch (previous->GetType()) {
        case HUMAN_CONTROLLER_PLAYER: {
            const C_PlayerController *player = static_cast<const C_PlayerController *>(previous);
            if (alive)
                SetCombatOrder(player->GetSquadCombatOrder());
            m_flags = player->GetSquadFlags();
            SetFollow(false, false);
            if (m_flags & FLAG_HOLDING)
                m_coverStance = previous->GetMoveFlags() & MOVE_STANCE_MASK;
            reportEnemies = false;
            m_holdDir = Dir4(previous->GetViewDir());
            break;
        }
        case HUMAN_CONTROLLER_SQUAD:
            reportEnemies = false;
            break;
        case HUMAN_CONTROLLER_AI: {
            const uint32_t flags = static_cast<const C_ScriptedController *>(previous)->GetScriptFlags();
            if (flags & 0x1000)
                SetFollow(false, true);
            if (flags & 0x8000) {
                m_flags |= FLAG_1;
                SetFollow(false, true);
            }
            if ((flags & 0x100) && alive)
                SetCombatOrder(SQUAD_ORDER_MODE1);
            break;
        }
        default:
            break;
        }
        // The enemies the human saw become the team's.
        if (m_human->m_stats[STAT_HEALTH] > 0 && reportEnemies) {
            for (size_t i = m_human->m_seenEnemies.size(); i-- > 0;)
                g_teamManager.GetPlayerTeam()->GetEnemyRecords().Add(m_human->m_seenEnemies[i]);
        }
    }
    m_selfHealTime = 0;
    if (!m_handsItem && alive)
        m_human->DrawLongGun();
    SetIdleTask(nullptr);
    m_alertTime = 0;
    m_eventTime = 0;
    UpdateModeIcon();
    LookAround(0);
}

void C_SquadController::Detach()
{
    C_AIController::Detach();
    CancelOrders(false);
    m_human = nullptr;
}

void C_SquadController::OnDeath()
{
    C_HumanController::OnDeath();
    CancelOrders(false);
    C_HumanController::OnDeath();
}

// ------------------------------------------------------------------------- updates

void C_SquadController::Update(int frameTime)
{
    if (m_moveFlags & MOVE_DEAD)
        return;
    C_Actor *vehicle = m_human->m_vehicle;
    if (vehicle && vehicle->GetType() == ACTOR_CAR) {
        if (vehicle->HasSeatWeapon(m_human->m_seat)) {
            // The gunner looks along the body unless it fires or checks an event.
            const bool busy = m_fireOrder->target || XYZ(m_fireOrder->fireDir).Square() >= 1e-16f || m_eventTime != 0;
            if (m_gunnerLooksAhead || !busy) {
                m_viewDir = FrameDir4(m_human->GetFrame());
                m_facing->SetView(m_viewDir);
                m_facing->SetFacing(m_viewDir);
            }
        } else {
            // A seat with a camera: the view follows it. (The original tests MOVE_DEAD again.)
            const int seat = m_human->m_seat;
            if (vehicle->GetSeatFlags(seat) & SEAT_FLAG_CAMERA) {
                if (I3D_frame *camera = vehicle->GetSeatCamera(seat)) {
                    const S_vector4 dir = FrameDir4(camera);
                    m_facing->SetView(dir);
                    m_facing->SetFacing(FrameDir4(camera));
                    // (The controller's own view direction, not set here.)
                    m_holdDir = m_viewDir;
                }
            }
        }
    }
    m_engageTime -= frameTime;
    if (m_engageTime < 0)
        m_engageTime = 0;
}

void C_SquadController::Tick(int timeDelta)
{
    if (m_moveFlags & MOVE_DEAD)
        return;
    UpdateTimers(timeDelta);
    // A mine within 3 m (looked for every 250 ms).
    if (m_mineCheckTime != 0) {
        m_mineCheckTime -= timeDelta;
        if (m_mineCheckTime < 1) {
            m_mineCheckTime = 250;
            m_mineNear = g_temporaryActors.IsMineInRange(XYZ(m_human->GetPosition4()), 3);
        }
    }
    if (m_selfHealTime != 0) {
        m_selfHealTime -= timeDelta;
        if (m_selfHealTime < 0)
            m_selfHealTime = 0;
    }
    if (m_friendlyFire != 0.0f) {
        m_friendlyFire -= static_cast<float>(timeDelta) * 0.1f;
        if (m_friendlyFire < 0.0f)
            m_friendlyFire = 0.0f;
    }
    if (m_formationTime != 0 && !(m_move->flags & S_AIMoveOrder::FLAG_ROUTE)) {
        m_formationTime -= timeDelta;
        if (m_formationTime < 0)
            m_formationTime = 0;
    }
    if (m_followDelay != 0) {
        m_followDelay -= timeDelta;
        if (m_followDelay < 1)
            m_followDelay = 0;
    }
    std::fill(std::begin(g_aiAxes), std::end(g_aiAxes), 0.0f);
    std::fill(std::begin(g_aiButtons), std::end(g_aiButtons), 0.0f);
    m_axes = g_aiAxes;
    m_buttons = g_aiButtons;

    // The orders, else the action, else idle (following: the action and idle).
    bool ordersDone = false;
    if (m_action.type == SQUAD_ACTION_FOLLOW && !m_action.completed) {
        ExecuteAction(timeDelta);
        UpdateIdle(timeDelta);
        ordersDone = true;
    } else if (!ProcessOrders(timeDelta) && !ExecuteAction(timeDelta)) {
        UpdateIdle(timeDelta);
        ordersDone = true;
    }
    m_human->ProcessEvents();
    UpdateIconTimers(timeDelta);

    const bool gunner = HasSeatWeapon();
    if (InVehicleSeat(m_human->m_vehicle)) {
        if (gunner)
            VehicleCombat(timeDelta);
        ControlVehicle();
        if (UpdateDriving(timeDelta) == 1)
            m_action.arrived = true;
    } else {
        MakeRoomForLeader();
        UpdateHandsItem();
        ApplyAlertState();
        switch (UpdateMove(timeDelta)) {
        case MOVE_RESULT_ENDED:
            if (!m_action.finished) {
                if (!m_action.completed)
                    m_action.arrived = true;
            } else if (m_following) {
                m_formationTime = 0;
            }
            if (m_steppedAside)
                m_steppedAside = false;
            break;
        case MOVE_RESULT_NO_PATH:
            // An attack the member cannot walk to: from the map's point to attack the target
            // from, else not at all.
            if (!m_action.finished
                && (m_action.type == SQUAD_ACTION_ATTACK || m_action.type == SQUAD_ACTION_ATTACK_SILENTLY)) {
                S_vector4 cover;
                float radius;
                if (!FindAttackPoint(&cover, &radius, m_action.target)) {
                    SayReply(SENTENCE_CANNOT, true, 300, false);
                    DropAction();
                } else {
                    m_action.completed = true;
                    m_action.Set(m_action.target, m_action.type, cover, m_human->GetDirection4(), false,
                                 radius <= 0.5f ? 0.5f : radius);
                    m_action.noCommand = true;
                    m_action.slot = 1;
                }
            }
            break;
        case -6:
            SayReply(SENTENCE_CANNOT, true, 300, false);
            break;
        case MOVE_RESULT_STUCK:
        case MOVE_RESULT_WAITED:
            // Again to the action's place. (With a target the original only reads its frame.)
            if (!m_action.finished && !m_action.arrived) {
                if (!m_action.target)
                    m_move->SetPos(m_action.pos);
                else
                    m_action.target->GetFrame()->GetMatrix();
            }
            break;
        default:
            break;
        }
        UpdatePace(timeDelta, ordersDone);
    }

    if (!gunner) {
        switch (UpdateFireOrder(timeDelta)) {
        case AI_FIRE_STARTED:
        case AI_FIRE_0400:
            SetModeIcon(0xC);
            break;
        case AI_FIRE_NOT_READY:
            if (C_Item *item = m_handsItem) {
                // Kneel down for a weapon about to be ready (unless the order or the leader
                // keep the member up).
                // (Fix: the original reads a weapon's ready time of any item in the hands.)
                const uint32_t readyTime = item->GetKind() == ITEM_KIND_WEAPON
                                               ? static_cast<C_WeaponItem *>(item)->GetReadyTime() : 0;
                const int readyIn = readyTime == 0 ? 0 : static_cast<int>(readyTime - GetGame()->GetMissionTime());
                uint32_t leaderStance = MOVE_STANDING;
                if (ordersDone) {
                    if (C_Human *leader = m_human->GetLeader())
                        leaderStance = leader->GetController()->GetMoveFlags() & MOVE_STANCE_MASK;
                }
                if (readyIn < 400 && !(m_moveFlags & MOVE_STANDING) && m_orderStance == 0
                    && (!(m_flags & FLAG_2) || leaderStance == MOVE_STANDING))
                    SetStance(MOVE_STANDING);
            }
            break;
        case AI_FIRE_BLOCKED:
            SayReply(SENTENCE_LINE_BLOCKED, true, 7000, false);
            break;
        case AI_FIRE_NO_WEAPON:
            if (m_weaponCheckTime < 200)
                UpdateWeaponChoice();
            break;
        case AI_FIRE_RELOADING:
            if (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && m_orderStance == 0
                && (m_moveFlags & MOVE_STANCE_MASK) == MOVE_STANDING)
                SetStance(MOVE_CROUCHING);
            SetModeIcon(0xD);
            break;
        case AI_FIRE_NO_AMMO:
            if (!ReloadFromBelt() && m_weaponCheckTime < 200)
                UpdateWeaponChoice();
            SetModeIcon(0xE);
            break;
        default:
            break;
        }
        UpdateCombat(timeDelta, ordersDone);
        ApplyMoveControls();
        UpdateStamina(static_cast<uint32_t>(timeDelta));

        // A magazine of the weapon in the hands carried: reload when it is empty, or top it
        // up while calm (as the original: only a weapon that cannot be topped up).
        if (m_handsItem && m_handsItem->GetMaxAmount() != -1.0f) {
            const int behaviour = BehaviourOf(m_handsItem);
            if (behaviour > 1 && (behaviour < 7 || behaviour == 12)) {
                int container = 0;
                C_Item *ammo = m_human->FindItem(m_handsItem->GetDef()->GetAmmoItemId(), &container);
                bool reload = false;
                bool tried = false;
                if (ammo) {
                    if (m_handsItem->GetAmount() == 0.0f) {
                        reload = true;
                    } else if (m_alertTime == 0) {
                        C_Item *hands = m_handsItem;
                        const float rounds = ammo->GetAmount();
                        if (hands->GetAmount() + 1.0f < rounds
                            && !static_cast<C_WeaponComp *>(m_handsItem->GetDef()->GetComponent())->CanReload())
                            reload = true;
                    }
                }
                if (reload) {
                    tried = true;
                    int ammoUid;
                    if (m_handsItem->Reload(m_human, container, &ammoUid, false)) {
                        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RELOAD));
                        m_alertTime = 5000;
                    }
                }
                // (Weapons only have the flag; the original clears it on whatever the hands hold.)
                if (!tried && m_handsItem->GetKind() == ITEM_KIND_WEAPON) {
                    C_WeaponItem *weapon = static_cast<C_WeaponItem *>(m_handsItem);
                    if (weapon->IsReloadPending())
                        weapon->ClearReloadPending();
                }
            }
        }

        // Looking around while there is nothing to fire at.
        if (!ReactToStimuli() && !(m_moveFlags & MOVE_LOOKING_AROUND)) {
            int lookType;
            if (m_fireOrder->target || XYZ(m_fireOrder->fireDir).Square() >= 1e-16f || !m_human->m_seenEnemies.empty()) {
                m_facing->SetLookAround(false, 4000);
                lookType = 0;
            } else {
                m_facing->SetLookAround(true, 4000);
                lookType = 4;
            }
            LookAround(lookType);
        }
    }
    ApplyWeaponControls(timeDelta, 0);
    UpdateAlertPreset();
    UpdateLook(timeDelta);

    // An enemy killed (the last such stimulus): after 0.75 s it goes, with a remark.
    std::deque<S_HumanEvent *> &stimuli = m_human->m_events->GetStimuli();
    for (size_t i = stimuli.size(); i-- > 0;) {
        if (stimuli[i]->type != AI_EVENT_KILLED_ENEMY)
            continue;
        if (stimuli[i]->age > 750) {
            ReleaseHumanEvent(stimuli[i]);
            stimuli.erase(stimuli.begin() + static_cast<std::ptrdiff_t>(i));
            SayReply(SENTENCE_ENEMY_DOWN, true, 300, false);
        }
        break;
    }
    SelfHeal(timeDelta);
}

void C_SquadController::SelfHeal(int /*timeDelta*/)
{
    // A calm member not lying, with nothing to do (or walking an order), heals itself now and
    // then with a medkit; the better its first aid, the sooner.
    if (m_selfHealTime != 0)
        return;
    if ((m_moveFlags & MOVE_LYING) || m_quietTime <= 4999
        || (m_action.type != SQUAD_ACTION_ORDER_MOVE && !m_action.completed)) {
        m_selfHealTime = 2000;
        return;
    }
    if (m_human && m_human->m_carried)
        return;
    const int firstAid = m_human->GetStat(STAT_FIRST_AID);
    if (firstAid < 10) {
        m_selfHealTime = 60000;
        return;
    }
    if (m_human->GetHealthPercent() > 94) {
        m_selfHealTime = 5000;
        return;
    }
    if (std::rand() % ((100 - firstAid) / 2 + 10) != 0)
        return;
    C_Item *medkit = m_human->GetMedkit();
    if (!medkit)
        return;
    C_HumanController::IssueCommand(MakeCommand(HUMAN_COMMAND_HEAL_SELF, medkit->GetUid(), 0));
    m_selfHealTime = static_cast<int>(RandFloat() * 4000.0f + static_cast<float>((100 - firstAid) * 200));
}

void C_SquadController::UpdateIdle(int /*timeDelta*/)
{
    if (!m_following) {
        // Calm and still: now and then the member turns back to where it was told to face.
        if (m_eventTime != 0 || m_alertTime != 0 || (m_move->flags & S_AIMoveOrder::FLAG_ROUTE)
            || !m_human->m_seenEnemies.empty())
            return;
        const S_vector &facing = m_human->GetFacingDir();
        const float dot = m_holdDir.z * facing.z + m_holdDir.y * facing.y + m_holdDir.x * facing.x;
        if (!(dot < 0.995f))
            return;
        if (std::rand() % 20 != 0)
            return;
        m_facing->SetView(m_holdDir);
        m_facing->SetFacing(m_holdDir);
        return;
    }

    // Following: the member keeps its place in the leader's formation, or its seat in the
    // leader's vehicle.
    UpdateHandsItem();
    C_Human *leader = m_human->GetLeader();
    if (!leader)
        return;
    C_FormationShape *formation = leader->GetFormation();
    S_vector4 slotPos = {};                             // (a stale local in the original without a slot)
    uint16_t slotPoint = 0xFFFF;
    formation->GetSlotTarget(m_human, &slotPos, &slotPoint);
    const S_vector &pos = m_human->GetFrame()->GetWorldPos();
    const float dx = pos.x - slotPos.x;
    float dy = pos.y - slotPos.y;
    const float dz = pos.z - slotPos.z;
    // Heights within 5 m do not count.
    if (std::fabs(dy) < 5.0f)
        dy = 0.0f;
    const float distance2 = dx * dx + dz * dz + dy * dy;

    C_Actor *leaderVehicle = leader->m_vehicle;
    bool walk = true;
    if (InVehicleSeat(leaderVehicle)) {
        C_Actor *own = m_human->m_vehicle;
        // Out of another vehicle (not a submarine) first.
        if (own && own != leaderVehicle && own->GetType() != ACTOR_SUBMARINE)
            IssueCommand(MakeCommand(HUMAN_COMMAND_LEAVE_VEHICLE, 0, 0));
        if (leaderVehicle->GetType() == ACTOR_CAR) {
            if (own)
                return;
            // A carried body is put down.
            if (m_human->m_carried)
                IssueCommand(MakeCommand(HUMAN_COMMAND_LEAVE_VEHICLE, m_action.targetId, 0));
        } else if (own) {
            return;
        }
        // Near the slot, or at the leader's car: into the seat the formation gives.
        if (distance2 < 2.25f
            || (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && leaderVehicle->GetType() == ACTOR_CAR
                && IsAtTarget(leaderVehicle)))
            walk = false;
    } else if (InVehicleSeat(m_human->m_vehicle)) {
        IssueCommand(MakeCommand(HUMAN_COMMAND_LEAVE_VEHICLE, 0, 0));
    }
    if (!walk) {
        int vehicleId = -1;
        const int seat = formation->GetSlotSeat(m_human, &vehicleId);
        if (vehicleId < 0)
            return;
        C_Actor *vehicle = g_actorsModule.GetActorById(vehicleId);
        if (vehicle->GetSeatCount() <= seat || vehicle->GetSeatOccupant(seat))
            return;
        if (m_human->m_carried) {
            IssueCommand(MakeCommand(HUMAN_COMMAND_LEAVE_VEHICLE, m_action.targetId, 0));
            return;
        }
        IssueCommand(MakeCommand(HUMAN_COMMAND_ENTER_VEHICLE, vehicleId, seat));
        ShowActionIcon(seat + 8);
        return;
    }

    // The slot moved away from where the member walks to.
    bool slotMoved = false;
    if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE) {
        const float mx = m_move->pos.x - slotPos.x;
        const float my = m_move->pos.y - slotPos.y;
        const float mz = m_move->pos.z - slotPos.z;
        const float moved2 = mx * mx + mz * mz + my * my;
        const bool leaderMoving = leader->m_vehicle || !(leader->GetVelocity().Square() < 1e-16f);
        slotMoved = leaderMoving ? moved2 > 36.0f : moved2 > 0.25f;
    } else if (distance2 < 0.25f) {
        // In the slot: its direction is the one to keep.
        formation->GetSlotDir(m_human, &m_holdDir);
    }
    if (distance2 > 0.25f && (m_formationTime == 0 || slotMoved)) {
        if (m_human->m_body.IsWorkState5()) {
            m_move->SetPos(slotPos);
        } else if (m_fireMoveTime >= 0) {
            // To the slot's walking point, unless the slot is under a roof while the member is
            // outdoors and the leader too.
            I3D_scene *scene = GetGame()->GetScene();
            I3D_sector *outdoors = scene->GetPrimarySector();
            if (leader->m_body.GetSector() != outdoors) {
                m_move->SetCheckpointPos(static_cast<int16_t>(slotPoint), slotPos);
            } else {
                const S_vector above(slotPos.x, slotPos.y + 0.5f, slotPos.z);
                if (scene->GetSector(above) == outdoors || m_human->m_body.GetSector() != outdoors)
                    m_move->SetCheckpointPos(static_cast<int16_t>(slotPoint), slotPos);
            }
        }
        m_formationTime = 2200;
        GetSquadCommander()->DelayFormationChecks();
    }

    // The stance follows the leader's when the leader is near or in sight.
    if (AtSpecialPoint()) {
        SetStance(MOVE_STANDING);
    } else {
        const uint32_t own = m_moveFlags & MOVE_STANCE_MASK;
        const uint32_t leaderStance = leader->GetController()->GetMoveFlags() & MOVE_STANCE_MASK;
        if ((m_fireMoveTime == 0 || (m_move->flags & S_AIMoveOrder::FLAG_ROUTE)) && !m_lyingToFire && m_orders.empty()
            && own != leaderStance && (std::rand() % 10 == 0 || m_fireMoveTime != 0)
            && (!(m_flags & FLAG_2) || leaderStance < own)) {
            const S_vector4 &from = m_human->GetPosition4();
            const S_vector4 &to = leader->GetPosition4();
            const float lx = to.x - from.x;
            const float ly = to.y - from.y;
            const float lz = to.z - from.z;
            const float leader2 = lz * lz + ly * ly + lx * lx;
            if (!m_move->unknown58 && leader2 < 1225.0f) {
                const bool close = leader2 < 225.0f;
                const int stance = leaderStance == MOVE_LYING ? MOVE_LYING
                                   : leaderStance == MOVE_CROUCHING ? MOVE_CROUCHING : MOVE_STANDING;
                if (close || g_sightModule.TestNow(m_human->GetSightId(), leader->GetSightTargetId()))
                    SetStance(stance);
            }
        }
    }

    // While alert the weapon follows the leader's (raised now and then).
    if (m_alertTime == 0)
        return;
    const C_HumanController *leaderController = leader->GetController();
    if (!leaderController->GetHandsItem() || m_weaponRaised || !leaderController->IsWeaponRaised())
        return;
    if (std::rand() % 6 != 0 || !m_handsItem)
        return;
    if (m_handsItem->GetDef()->GetCarryMode() == 3) {
        // Not while hurrying along a route.
        const uint32_t pace = m_move->unknown54 & MOVE_PACE_MASK;
        if ((m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && pace > MOVE_PACE_1 && pace != MOVE_PACE_2) {
            m_fireOrder->SetTarget(nullptr);
            if (m_unknownAC)
                LowerWeapon();
            return;
        }
    } else if (BehaviourOf(m_handsItem) == 5) {
        return;
    }
    if (m_human->m_role != HUMAN_ROLE_NPC_TEAM_5)
        RaiseWeapon();
}

// ------------------------------------------------------------------------- the order queue

namespace {

// The order (or its actor) is at a walking point (within 5 m).
bool IsOrderReachable(const S_HumanOrder *order)
{
    const S_vector &pos = order->actor ? order->actor->GetFrame()->GetWorldPos() : XYZ(order->pos);
    return GetGame()->GetCheckpoints()->FindNearest(pos, 5.0f, -1.0f) != CHECKPOINT_NONE;
}

}  // namespace

void C_SquadController::FollowSelectedMember()
{
    if (C_Human *leader = g_teamManager.GetPlayerTeam()->GetSelectedMember())
        m_human->SetLeader(leader);
}

void C_SquadController::AddOrder(S_HumanOrder *order)
{
    if (GetOrderCount() == MAX_ORDERS || !IsOrderReachable(order)) {
        ReleaseHumanOrder(order);
        SayReply(SENTENCE_CANNOT, true, 0, true);
        return;
    }
    // A watch order at the end gives way; so does a follow order (the member follows the
    // selected one then).
    if (!m_orders.empty()) {
        S_HumanOrder *last = m_orders.back();
        if (last->kind == HUMAN_ORDER_WATCH) {
            ReleaseHumanOrder(last);
            m_orders.pop_back();
        } else if (last->arg == SQUAD_ACTION_FOLLOW) {
            FollowSelectedMember();
            ReleaseHumanOrder(last);
            m_orders.pop_back();
        }
    }
    m_orders.push_back(order);
    m_ordersChangedTime = static_cast<int>(GetGame()->GetTime()) + 1;
}

void C_SquadController::InsertOrder(S_HumanOrder *order, int index)
{
    if (GetOrderCount() == MAX_ORDERS || !IsOrderReachable(order)) {
        ReleaseHumanOrder(order);
        SayReply(SENTENCE_CANNOT, true, 0, true);
        return;
    }
    if (!m_orders.empty()) {
        S_HumanOrder *last = m_orders.back();
        if (last->kind == HUMAN_ORDER_WATCH) {
            ReleaseHumanOrder(last);
            m_orders.pop_back();
            --index;
        } else if (last->arg == SQUAD_ACTION_FOLLOW) {
            FollowSelectedMember();
            ReleaseHumanOrder(last);
            m_orders.pop_back();
            --index;
        }
    }
    // (After `index`, as in the original.)
    m_orders.insert(m_orders.begin() + (index + 1), order);
    m_ordersChangedTime = static_cast<int>(GetGame()->GetTime()) + 1;
}

void C_SquadController::SetOrder(int index, S_HumanOrder *order)
{
    m_orders[index]->CopyFrom(*order);
    if (index == 0) {
        m_restartOrder = true;
        if (order->kind == HUMAN_ORDER_WATCH) {
            // The member turns to watch along the order at once.
            m_holdDir = order->data;
            m_viewDir = order->data;
            m_facingDir = order->data;
            m_facing->SetView(m_viewDir);
            m_facing->SetFacing(m_viewDir);
        }
    }
    m_ordersChangedTime = static_cast<int>(GetGame()->GetTime()) + 1;
}

void C_SquadController::CancelOrders(bool turn)
{
    for (size_t i = m_orders.size(); i-- > 0;)
        RemoveOrder(static_cast<int>(i), turn);
    m_ordersChangedTime = static_cast<int>(GetGame()->GetTime()) + 1;
    m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
    if (m_driver)
        m_driver->Stop();
    m_action.finished = true;
    m_action.completed = true;
    HoldOrders(false);
}

void C_SquadController::RemoveOrder(int index, bool turn)
{
    m_ordersChangedTime = static_cast<int>(GetGame()->GetTime()) + 1;
    if (index == 0) {
        // The order going on ends.
        m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
        if (m_driver)
            m_driver->Stop();
        m_action.finished = true;
        m_flags |= FLAG_1;
        m_action.completed = true;
        if (m_orders[0]->arg == SQUAD_ACTION_FOLLOW)
            FollowSelectedMember();
        if (m_orders[0]->kind == HUMAN_ORDER_FOLLOW)
            turn = false;
    }
    S_HumanOrder *removed = m_orders[index];
    ReleaseHumanOrder(removed);
    m_orders.erase(m_orders.begin() + index);
    // The last one gone: the member watches along where it faces.
    if (m_orders.empty() && turn) {
        m_holdDir = Dir4(m_human->GetFacingDir());
        // (The original reads the queue's first slot, which still holds the order just
        // released.)
        QueueWatchOrder(WorldPos4Of(m_human->GetFrame()), m_holdDir, removed);
    }
}

bool C_SquadController::HaveOrdersChanged()
{
    return !(static_cast<uint32_t>(m_ordersChangedTime) < GetGame()->GetTime());
}

int C_SquadController::GetOrderCount()
{
    return static_cast<int>(m_orders.size());
}

S_HumanOrder *C_SquadController::GetOrder(int index)
{
    return m_orders[index];
}

// ------------------------------------------------------------------------- the member's panel

void C_SquadController::SetStatusIcon(int icon)
{
    if (icon >= 0 && icon <= 15) {
        m_statusIcon = icon;
    } else if (icon >= 16 && icon <= 19) {
        m_tempStatusIcon = icon;
        m_statusIconTime = 2000;
    }
}

void C_SquadController::SetModeIcon(int icon)
{
    if (icon >= 0 && icon <= 11) {
        m_modeIcon = icon;
    } else if (icon >= 12 && icon <= 39) {
        m_tempModeIcon = icon;
        m_modeIconTime = 2000;
    }
}

void C_SquadController::UpdateIconTimers(int timeDelta)
{
    if (m_statusIconTime != 0) {
        m_statusIconTime -= timeDelta;
        if (m_statusIconTime < 1) {
            m_statusIconTime = 0;
            m_tempStatusIcon = -1;
        }
    }
    if (m_modeIconTime != 0) {
        m_modeIconTime -= timeDelta;
        if (m_modeIconTime < 1) {
            m_modeIconTime = 0;
            m_tempModeIcon = -1;
        }
    }
}

int C_SquadController::GetStatusIcon()
{
    return m_tempStatusIcon != -1 ? m_tempStatusIcon : m_statusIcon;
}

int C_SquadController::GetModeIcon()
{
    return m_tempModeIcon != -1 ? m_tempModeIcon : m_modeIcon;
}

int C_SquadController::GetCombatOrder()
{
    switch (m_combatMode) {
    case 0:
        return SQUAD_ORDER_MODE0 + (m_silentMode ? 1 : 0);
    case 1:
        return SQUAD_ORDER_MODE1 + (m_silentMode ? 1 : 0);
    case 2:
        return SQUAD_ORDER_MODE2 + (m_silentMode ? 1 : 0);
    default:
        return SQUAD_ORDER_MODE0;
    }
}

void C_SquadController::UpdateModeIcon()
{
    switch (m_combatMode) {
    case 0:
        SetModeIcon(m_silentMode ? 1 : 0);
        break;
    case 1:
        SetModeIcon(4 + (m_silentMode ? 1 : 0));
        break;
    case 2:
        SetModeIcon(2);
        break;
    default:
        SetModeIcon(4);
        break;
    }
}

void C_SquadController::NotifyIfIdle()
{
    if (GetOrderCount() == 0)
        GetSquadCommander()->OnMemberIdle(m_human);
}

void C_SquadController::SayReply(int sentence, bool alt, int delay, bool force)
{
    if (m_moveFlags & MOVE_SWIMMING)
        return;
    // Sneaking or silent members (or the squad whispering) say the quiet variant.
    if (alt && (m_silentMode || m_quiet || GetSquadCommander()->IsWhispering()))
        ++sentence;
    Say(sentence, delay, 1, force);
}

// ------------------------------------------------------------------------- the senses

void C_SquadController::OnEnemySeen(C_Human *human)
{
    C_AIController::OnEnemySeen(human);
    const S_vector4 &enemy = human->GetPosition4();
    const S_vector4 &own = m_human->GetPosition4();
    const float dx = enemy.x - own.x;
    const float dy = enemy.y - own.y;
    const float dz = enemy.z - own.z;
    // A new enemy for the team: far (20 m and more) or near.
    if (!g_teamManager.GetPlayerTeam()->IsEnemyKnown(human))
        SayReply(dx * dx + dy * dy + dz * dz >= 400.0f ? SENTENCE_ENEMY_FAR : SENTENCE_ENEMY_NEAR, true, 2000, false);
    g_teamManager.GetPlayerTeam()->GetEnemyRecords().Add(human);
    // The first enemy seen: the weapon is checked at once.
    if (m_human->m_seenEnemies.size() == 1)
        m_weaponCheckTime = 0;
}

void C_SquadController::OnBodySeen(C_Human *human)
{
    // (The result is not used.)
    g_teamManager.GetPlayerTeam()->IsEnemyKnown(human);
}

void C_SquadController::OnEnemyLost(C_Human *human)
{
    C_AIController::OnEnemyLost(human);
    g_teamManager.GetPlayerTeam()->GetEnemyRecords().Remove(human);
    if (!g_teamManager.GetPlayerTeam()->IsEnemyKnown(human))
        SayReply(SENTENCE_ENEMY_LOST, true, 2000, false);
}

void C_SquadController::OnLoudSound(C_Actor *source)
{
    C_AIController::OnLoudSound(source);
    SayReply(SENTENCE_LOUD_SOUND, true, 5000, false);
}

void C_SquadController::OnShotAt(C_Actor *shooter, const S_vector &velocity)
{
    C_AIController::OnShotAt(shooter, velocity);
    if (g_teamManager.GetPlayerTeam()->GetAliveCount() > 1)
        SayReply(SENTENCE_UNDER_FIRE, true, 5000, false);
}

void C_SquadController::OnHit(int type, int zone, int health, C_Actor *attacker, const S_vector &pos,
                              const S_vector &dir)
{
    TakeHit(type, zone, health, attacker, pos, dir, 100.0f);
}

void C_SquadController::OnUniformChanged(int uid)
{
    m_human->PutOnUniform(uid);
}

void C_SquadController::EndInteraction(bool completed)
{
    if (!completed) {
        m_action.completed = true;
        m_action.target = nullptr;
    }
    C_HumanController::EndInteraction(completed);
}

bool C_SquadController::Unknown104()
{
    if (m_flags & FLAG_1)
        return false;
    return C_HumanController::Unknown104();
}

// ------------------------------------------------------------------------- vehicles

void C_SquadController::ControlVehicle()
{
    C_Actor *vehicle = m_human->m_vehicle;
    if (vehicle->GetType() == ACTOR_CAR) {
        // A driver nobody drives for leaves the controls alone.
        if (m_human->m_seat == 0 && !static_cast<C_Car *>(vehicle)->HasLocalPlayerAboard() && !m_driver->GetVehicle())
            static_cast<C_Car *>(vehicle)->SetInput(0.0f, 0.0f);
    } else if (vehicle->GetType() == ACTOR_SUBMARINE) {
        ControlSubmarine();
    }
}

void C_SquadController::ControlSubmarine()
{
    // Without the player aboard the member steers (with two passengers only the pilot).
    C_Submarine *submarine = static_cast<C_Submarine *>(m_human->m_vehicle);
    int seat = submarine->GetSeatCount();
    int others = 0;
    while (seat != 0) {
        C_Human *occupant = static_cast<C_Human *>(submarine->GetSeatOccupant(seat - 1));
        if (occupant && occupant->GetControllerType() == HUMAN_CONTROLLER_PLAYER)
            break;
        ++others;
        --seat;
    }
    if (seat != 0 || (others == 2 && m_human->m_seat != 0))
        return;
    submarine->SetControls(m_axes[AXIS_FORWARD] - m_axes[AXIS_BACK], m_axes[AXIS_LEFT] - m_axes[AXIS_RIGHT],
                           m_axes[8] - m_axes[9]);
    // Following: behind another submarine (the last of the list that is not this one).
    const std::vector<C_Actor *> &submarines = GetSubmarines();
    for (size_t i = submarines.size(); i-- > 0;) {
        if (submarines[i] == submarine)
            continue;
        if (submarines[i] && m_following)
            FollowSubmarine(submarines[i]);
        return;
    }
}

void C_SquadController::FollowSubmarine(C_Actor *other)
{
    // Keeps 7..45 m behind the other submarine: the throttle by the distance, the turn by the
    // angle to it, the dive by the height difference (and no throttle or turn once past it).
    C_Submarine *submarine = static_cast<C_Submarine *>(m_human->m_vehicle);
    const S_vector4 &own = submarine->GetPosition4();
    const S_vector4 &target = other->GetPosition4();
    S_vector offset(target.x - own.x, target.y - own.y, target.z - own.z);
    const float distance2 = offset.y * offset.y + offset.z * offset.z + offset.x * offset.x;
    float throttle = 0.0f;
    float dive = 0.0f;
    if (offset.y > 1.0f)
        dive = 1.0f;
    if (offset.y < -1.0f)
        dive = -1.0f;
    if (distance2 <= 49.0f || distance2 >= 2000.0f) {
        // Too near or too far: the speed runs down (to 2).
        if (submarine->GetSpeed() > 2.0f)
            submarine->SetSpeed(submarine->GetSpeed() - 0.03f);
    } else {
        throttle = std::max((distance2 + 49.0f) * 0.0005f, 0.25f);
        if (distance2 < 225.0f && submarine->GetSpeed() > 2.0f)
            throttle = submarine->GetSpeed() * -0.1f;
    }
    // The turn: the angle between the heading and the way to the other (xz).
    const S_vector4 &heading = submarine->GetDirection4();
    float headingXZ[2] = {heading.x, heading.z};
    float length = std::sqrt(heading.x * heading.x + heading.z * heading.z);
    if (length > 1e-8f) {
        const float inv = 1.0f / length;
        headingXZ[0] = heading.x * inv;
        headingXZ[1] = inv * heading.z;
    }
    float toOther[2] = {offset.x, offset.z};
    length = std::sqrt(offset.x * offset.x + offset.z * offset.z);
    if (length > 1e-8f) {
        const float inv = 1.0f / length;
        toOther[0] = offset.x * inv;
        toOther[1] = inv * offset.z;
    }
    float turn = AngleBetween2D(headingXZ, toOther) * 500.0f;
    offset.Normalize();
    // Past the other one (it faces away from this one): neither throttle nor turn.
    const S_vector4 otherDir = FrameDir4(other->GetFrame());
    if (otherDir.x * offset.x + otherDir.y * offset.y + otherDir.z * offset.z < 0.0f) {
        throttle = 0.0f;
        turn = 0.0f;
    }
    submarine->SetUnknownF8(0.0f);
    submarine->SetControls(throttle, turn, dive);
}

// ------------------------------------------------------------------------- saved games

void S_SquadAction::SaveGame(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_ACTION);
    chunk.WriteChunk(CHUNK_ACTION_POS, &pos, SAVED_VECTOR_SIZE);
    chunk.WriteChunk(CHUNK_ACTION_DIR, &dir, SAVED_VECTOR_SIZE);
    chunk.WriteChunk(CHUNK_ACTION_ARRIVED, arrived);
    chunk.WriteChunk(CHUNK_ACTION_TURNED, turned);
    chunk.WriteChunk(CHUNK_ACTION_FINISHED, finished);
    chunk.WriteChunk(CHUNK_ACTION_KEEP_GOING, keepGoing);
    chunk.WriteChunk(CHUNK_ACTION_NO_COMMAND, noCommand);
    chunk.WriteChunk(CHUNK_ACTION_STARTED, started);
    chunk.WriteChunk(CHUNK_ACTION_COMPLETED, completed);
    chunk.WriteChunk(CHUNK_ACTION_SKIP_TURN, skipTurn);
    chunk.WriteChunk(CHUNK_ACTION_NOTIFY_MODULE, notifyModule);
    chunk.WriteChunk(CHUNK_ACTION_USE_DIR, useDir);
    chunk.WriteChunk(CHUNK_ACTION_TARGET_ID, targetId);
    chunk.WriteChunk(CHUNK_ACTION_SLOT, slot);
    chunk.WriteChunk(CHUNK_ACTION_DELAY, delay);
    chunk.WriteChunk(CHUNK_ACTION_RADIUS, radius);
    chunk.WriteChunk(CHUNK_ACTION_TYPE, type);
    const int actorId = target ? target->GetNetId() : -1;
    chunk.WriteChunk(CHUNK_ACTION_TARGET, actorId);
    const int temporary = target ? (target->IsTemporary() ? 1 : 0) : 0;
    chunk.WriteChunk(CHUNK_ACTION_TARGET_TEMPORARY, temporary);
    chunk.WriteChunk(CHUNK_ACTION_STANCE, stance);
    chunk.Ascend();
}

void S_SquadAction::LoadGame(C_chunk &chunk)
{
    int actorId = -1;
    bool temporary = false;
    uint16_t id;
    if (chunk.Descend(id) && id == CHUNK_ACTION) {
        while (!chunk.AtEnd() && chunk.Descend(id)) {
            switch (id) {
            case CHUNK_ACTION_POS: ReadVector(chunk, pos); break;
            case CHUNK_ACTION_DIR: ReadVector(chunk, dir); break;
            case CHUNK_ACTION_ARRIVED: ReadIfFits(chunk, arrived); break;
            case CHUNK_ACTION_TURNED: ReadIfFits(chunk, turned); break;
            case CHUNK_ACTION_FINISHED: ReadIfFits(chunk, finished); break;
            case CHUNK_ACTION_KEEP_GOING: chunk.Read(keepGoing); break;
            case CHUNK_ACTION_NO_COMMAND: chunk.Read(noCommand); break;
            case CHUNK_ACTION_STARTED: chunk.Read(started); break;
            case CHUNK_ACTION_COMPLETED: chunk.Read(completed); break;
            case CHUNK_ACTION_SKIP_TURN: chunk.Read(skipTurn); break;
            case CHUNK_ACTION_NOTIFY_MODULE: chunk.Read(notifyModule); break;
            case CHUNK_ACTION_USE_DIR: chunk.Read(useDir); break;
            case CHUNK_ACTION_TARGET_ID: ReadIfFits(chunk, targetId); break;
            case CHUNK_ACTION_SLOT: ReadIfFits(chunk, slot); break;
            case CHUNK_ACTION_DELAY: ReadIfFits(chunk, delay); break;
            case CHUNK_ACTION_RADIUS: ReadIfFits(chunk, radius); break;
            case CHUNK_ACTION_TYPE: ReadIfFits(chunk, type); break;
            case CHUNK_ACTION_TARGET: ReadIfFits(chunk, actorId); break;
            case CHUNK_ACTION_TARGET_TEMPORARY: {
                int value = 0;
                ReadIfFits(chunk, value);
                temporary = value != 0;
                break;
            }
            case CHUNK_ACTION_STANCE: ReadIfFits(chunk, stance); break;
            default: break;
            }
            chunk.Ascend();
        }
        chunk.Ascend();
    }
    // (A chunk of another id is entered and not left, as in the original.)
    target = FindSavedActor(actorId, temporary);
}

void C_SquadController::SaveOrders(C_chunk &chunk)
{
    for (S_HumanOrder *order : m_orders) {
        chunk.Begin(CHUNK_ORDER);
        chunk.WriteChunk(CHUNK_ORDER_KIND, order->kind);
        chunk.WriteChunk(CHUNK_ORDER_ARG, order->arg);
        const S_vector &pos = order->actor ? order->actor->GetFrame()->GetWorldPos() : XYZ(order->pos);
        chunk.WriteChunk(CHUNK_ORDER_POS, &pos, SAVED_VECTOR_SIZE);
        chunk.WriteChunk(CHUNK_ORDER_DATA, &order->data, SAVED_VECTOR_SIZE);
        // The defaults go into the order as it is saved.
        if (!(order->flags & MOVE_PACE_MASK))
            order->flags |= MOVE_PACE_4;
        chunk.WriteChunk(CHUNK_ORDER_PACE, order->flags & MOVE_PACE_MASK);
        if (!(order->flags & MOVE_STANCE_MASK))
            order->flags |= MOVE_STANDING;
        chunk.WriteChunk(CHUNK_ORDER_STANCE, order->flags & MOVE_STANCE_MASK);
        if (!(order->flags & MOVE_AI_MODE_MASK))
            order->flags |= MOVE_AI_DEFENSIVE;
        chunk.WriteChunk(CHUNK_ORDER_MODE, order->flags & MOVE_AI_MODE_MASK);
        const int actorId = order->actor ? order->actor->GetNetId() : -1;
        chunk.WriteChunk(CHUNK_ORDER_ACTOR, actorId);
        const int temporary = order->actor ? (order->actor->IsTemporary() ? 1 : 0) : 0;
        chunk.WriteChunk(CHUNK_ORDER_TEMPORARY, temporary);
        chunk.WriteChunk(CHUNK_ORDER_UNKNOWN70, order->unknown70);
        chunk.Ascend();
    }
}

bool C_SquadController::LoadOrder(C_chunk &chunk)
{
    int kind = HUMAN_ORDER_NONE;
    int arg = 0x27;
    uint32_t pace = 0;
    uint32_t stance = 0;
    uint32_t mode = 0;
    int actorId = -1;
    bool temporary = false;
    int unknown70 = 0;
    S_vector4 pos = {};                                 // (stale locals in the original when missing)
    S_vector4 data = {};
    uint16_t id;
    if (chunk.Descend(id) && id == CHUNK_ORDER) {
        while (!chunk.AtEnd() && chunk.Descend(id)) {
            switch (id) {
            case CHUNK_ORDER_KIND: chunk.Read(kind); break;
            case CHUNK_ORDER_ARG: chunk.Read(arg); break;
            case CHUNK_ORDER_POS: ReadVector(chunk, pos); break;
            case CHUNK_ORDER_DATA: ReadVector(chunk, data); break;
            case CHUNK_ORDER_PACE: chunk.Read(pace); break;
            case CHUNK_ORDER_STANCE: chunk.Read(stance); break;
            case CHUNK_ORDER_MODE: chunk.Read(mode); break;
            case CHUNK_ORDER_ACTOR: chunk.Read(actorId); break;
            case CHUNK_ORDER_TEMPORARY: {
                int value = 0;
                chunk.Read(value);
                temporary = value != 0;
                break;
            }
            case CHUNK_ORDER_UNKNOWN70: chunk.Read(unknown70); break;
            default: break;
            }
            chunk.Ascend();
        }
        chunk.Ascend();
    }
    // (A chunk of another id is entered and not left, as in the original.)
    if (kind == HUMAN_ORDER_NONE)
        return false;
    C_Actor *actor = FindSavedActor(actorId, temporary);
    m_orders.push_back(AllocHumanOrder(m_human, kind, arg, pos, data, pace, stance, mode, actor, unknown70));
    return true;
}

void C_SquadController::LoadOrders(C_chunk &chunk)
{
    while (!chunk.AtEnd()) {
        if (!LoadOrder(chunk))
            return;
    }
}

void C_SquadController::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_SQUAD);
    chunk.WriteChunk(CHUNK_SQUAD_VERSION, 1);
    chunk.Begin(CHUNK_SQUAD_AI);
    C_AIController::SaveGame(chunk);
    chunk.Ascend();
    chunk.WriteChunk(CHUNK_SQUAD_ENGAGE_POS, &m_engagePos, SAVED_VECTOR_SIZE);
    chunk.WriteChunk(CHUNK_SQUAD_BOUND_DIR, &m_boundDir, SAVED_VECTOR_SIZE);
    chunk.WriteChunk(CHUNK_SQUAD_HOLD_DIR, &m_holdDir, SAVED_VECTOR_SIZE);
    chunk.WriteChunk(CHUNK_SQUAD_PACE, m_pace);
    chunk.WriteChunk(CHUNK_SQUAD_FLAGS, m_flags);
    chunk.WriteChunk(CHUNK_SQUAD_ORDER_STANCE, m_orderStance);
    chunk.WriteChunk(CHUNK_SQUAD_COVER_STANCE, m_coverStance);
    chunk.WriteChunk(CHUNK_SQUAD_ORDER_PACE, m_orderPace);
    chunk.WriteChunk(CHUNK_SQUAD_IDLE_CHECKPOINT, m_idleCheckpoint);
    chunk.WriteChunk(CHUNK_SQUAD_QUIET, m_quiet);
    chunk.WriteChunk(CHUNK_SQUAD_FOLLOWING, m_following);
    chunk.WriteChunk(CHUNK_SQUAD_SILENT_MODE, m_silentMode);
    chunk.WriteChunk(CHUNK_SQUAD_LYING_TO_FIRE, m_lyingToFire);
    chunk.WriteChunk(CHUNK_SQUAD_STEPPED_ASIDE, m_steppedAside);
    chunk.WriteChunk(CHUNK_SQUAD_ORDERS_HELD, m_ordersHeld);
    chunk.WriteChunk(CHUNK_SQUAD_RESTART_ORDER, m_restartOrder);
    chunk.WriteChunk(CHUNK_SQUAD_BOUND_HOPS, m_boundHops);
    chunk.WriteChunk(CHUNK_SQUAD_ENGAGE_TIME, m_engageTime);
    chunk.WriteChunk(CHUNK_SQUAD_EVENT_TIME, m_eventTime);
    chunk.WriteChunk(CHUNK_SQUAD_ALERT_TIME, m_alertTime);
    chunk.WriteChunk(CHUNK_SQUAD_WEAPON_CHECK_TIME, m_weaponCheckTime);
    chunk.WriteChunk(CHUNK_SQUAD_FORMATION_TIME, m_formationTime);
    chunk.WriteChunk(CHUNK_SQUAD_FIRE_MOVE_TIME, m_fireMoveTime);
    chunk.WriteChunk(CHUNK_SQUAD_FOLLOW_DELAY, m_followDelay);
    chunk.WriteChunk(CHUNK_SQUAD_STATUS_ICON_TIME, m_statusIconTime);
    chunk.WriteChunk(CHUNK_SQUAD_MODE_ICON_TIME, m_modeIconTime);
    chunk.WriteChunk(CHUNK_SQUAD_ORDERS_CHANGED, m_ordersChangedTime);
    chunk.WriteChunk(CHUNK_SQUAD_COVER_RADIUS, m_coverRadius);
    chunk.WriteChunk(CHUNK_SQUAD_FRIENDLY_FIRE, m_friendlyFire);
    chunk.WriteChunk(CHUNK_SQUAD_STATUS_ICON, m_statusIcon);
    chunk.WriteChunk(CHUNK_SQUAD_MODE_ICON, m_modeIcon);
    chunk.WriteChunk(CHUNK_SQUAD_TEMP_STATUS_ICON, m_tempStatusIcon);
    chunk.WriteChunk(CHUNK_SQUAD_TEMP_MODE_ICON, m_tempModeIcon);
    chunk.WriteChunk(CHUNK_SQUAD_COMBAT_MODE, m_combatMode);
    chunk.Begin(CHUNK_SQUAD_ACTION);
    m_action.SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_SQUAD_ORDERS);
    SaveOrders(chunk);
    chunk.Ascend();
    chunk.WriteChunk(CHUNK_SQUAD_GUNNER_LOOKS_AHEAD, m_gunnerLooksAhead);
    chunk.Ascend();
}

void C_SquadController::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (chunk.Descend(id) && id == CHUNK_SQUAD) {
        while (!chunk.AtEnd() && chunk.Descend(id)) {
            switch (id) {
            case CHUNK_SQUAD_VERSION: {
                int version;
                chunk.Read(version);
                break;
            }
            case CHUNK_SQUAD_AI: C_AIController::LoadGame(chunk); break;
            case CHUNK_SQUAD_ENGAGE_POS: ReadVector(chunk, m_engagePos); break;
            case CHUNK_SQUAD_BOUND_DIR: ReadVector(chunk, m_boundDir); break;
            case CHUNK_SQUAD_HOLD_DIR: ReadVector(chunk, m_holdDir); break;
            case CHUNK_SQUAD_PACE: ReadIfFits(chunk, m_pace); break;
            case CHUNK_SQUAD_FLAGS: ReadIfFits(chunk, m_flags); break;
            case CHUNK_SQUAD_ORDER_STANCE: ReadIfFits(chunk, m_orderStance); break;
            case CHUNK_SQUAD_COVER_STANCE: ReadIfFits(chunk, m_coverStance); break;
            case CHUNK_SQUAD_ORDER_PACE: ReadIfFits(chunk, m_orderPace); break;
            case CHUNK_SQUAD_IDLE_CHECKPOINT: chunk.Read(m_idleCheckpoint); break;
            case CHUNK_SQUAD_QUIET: chunk.Read(m_quiet); break;
            case CHUNK_SQUAD_FOLLOWING: chunk.Read(m_following); break;
            case CHUNK_SQUAD_SILENT_MODE: chunk.Read(m_silentMode); break;
            case CHUNK_SQUAD_LYING_TO_FIRE: chunk.Read(m_lyingToFire); break;
            case CHUNK_SQUAD_STEPPED_ASIDE: chunk.Read(m_steppedAside); break;
            case CHUNK_SQUAD_ORDERS_HELD: chunk.Read(m_ordersHeld); break;
            case CHUNK_SQUAD_RESTART_ORDER: chunk.Read(m_restartOrder); break;
            case CHUNK_SQUAD_BOUND_HOPS: ReadIfFits(chunk, m_boundHops); break;
            case CHUNK_SQUAD_ENGAGE_TIME: ReadIfFits(chunk, m_engageTime); break;
            case CHUNK_SQUAD_EVENT_TIME: ReadIfFits(chunk, m_eventTime); break;
            case CHUNK_SQUAD_ALERT_TIME: ReadIfFits(chunk, m_alertTime); break;
            case CHUNK_SQUAD_WEAPON_CHECK_TIME: ReadIfFits(chunk, m_weaponCheckTime); break;
            case CHUNK_SQUAD_FORMATION_TIME: ReadIfFits(chunk, m_formationTime); break;
            case CHUNK_SQUAD_FIRE_MOVE_TIME: ReadIfFits(chunk, m_fireMoveTime); break;
            case CHUNK_SQUAD_FOLLOW_DELAY: ReadIfFits(chunk, m_followDelay); break;
            case CHUNK_SQUAD_STATUS_ICON_TIME: ReadIfFits(chunk, m_statusIconTime); break;
            case CHUNK_SQUAD_MODE_ICON_TIME: ReadIfFits(chunk, m_modeIconTime); break;
            case CHUNK_SQUAD_ORDERS_CHANGED: ReadIfFits(chunk, m_ordersChangedTime); break;
            case CHUNK_SQUAD_COVER_RADIUS: ReadIfFits(chunk, m_coverRadius); break;
            case CHUNK_SQUAD_FRIENDLY_FIRE: ReadIfFits(chunk, m_friendlyFire); break;
            case CHUNK_SQUAD_STATUS_ICON: ReadIfFits(chunk, m_statusIcon); break;
            case CHUNK_SQUAD_MODE_ICON: ReadIfFits(chunk, m_modeIcon); break;
            case CHUNK_SQUAD_TEMP_STATUS_ICON: ReadIfFits(chunk, m_tempStatusIcon); break;
            case CHUNK_SQUAD_TEMP_MODE_ICON: ReadIfFits(chunk, m_tempModeIcon); break;
            case CHUNK_SQUAD_COMBAT_MODE: ReadIfFits(chunk, m_combatMode); break;
            case CHUNK_SQUAD_ACTION: m_action.LoadGame(chunk); break;
            case CHUNK_SQUAD_ORDERS: LoadOrders(chunk); break;
            case CHUNK_SQUAD_GUNNER_LOOKS_AHEAD: chunk.Read(m_gunnerLooksAhead); break;
            default: break;
            }
            chunk.Ascend();
        }
        chunk.Ascend();
    }
    // (A chunk of another id is entered and not left, as in the original.) Without orders a
    // quiet member makes the squad whisper.
    if (m_orders.empty() && (m_quiet || m_silentMode))
        GetSquadCommander()->SetWhispering(true);
}
