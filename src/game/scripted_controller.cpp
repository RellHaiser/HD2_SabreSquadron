// C_ScriptedController, the AI the mission script drives, and C_NeutralController
// (game/human_controller.h; original source 0x0058d810..0x00591ee0, 0x005a4290..0x005a43e0).
#include "game/human_controller.h"

#include <windows.h>
#include <mmsystem.h>

#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/checkpoints.h"
#include "game/controller_internal.h"
#include "game/controller_messages.h"
#include "game/enemy_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "script/script.h"

namespace {

// The saved game's chunks.
enum : uint16_t {
    CHUNK_SCRIPTED_AI = 0x139D,                         // the AI base's
    CHUNK_SCRIPTED_STATE = 0x13A7,
    CHUNK_SCRIPTED_FIRE_TARGET = 0x13A8,                // actor id
    CHUNK_SCRIPTED_FIRE_FRAME = 0x13A9,                 // frame name
    CHUNK_SCRIPTED_FIRE_FROM_SEAT = 0x13AA,
    CHUNK_SCRIPTED_ATTACK = 0x13AB,
    CHUNK_SCRIPTED_TURN_TARGET = 0x13AC,                // frame name
    CHUNK_SCRIPTED_TURNING = 0x13AD,
    CHUNK_ATTACK_STATE = 0x13BA,
    CHUNK_ATTACK_TARGET = 0x13BB,                       // actor id
};
// Positions are saved without w.
constexpr int SAVED_VECTOR_SIZE = 12;

// The alarm went off (the script was told); the next one waits until the script calls it
// off (SetAlarm(false), MoveToAlarm).
constexpr uint32_t MOVE_ALARMED = 0x80000000;
// A meleeTracker barks at someone near the human (raised by the meleeTracker's AI, vtable 0x00811fd0: the
// event's source is what it barks at, its position where).
constexpr uint32_t AI_EVENT_MELEE_TRACKER_ALERT = 0x10000017;

// Work animations (BODY_COMMAND_PLAY): standing up from a seat ("zeseduR"; the fast one
// blends quicker) and sitting down cross-legged ("sedimturek").
constexpr int WORK_ANIM_STAND_UP = 0x1D;
constexpr int WORK_ANIM_STAND_UP_FAST = 0x26;
constexpr int WORK_ANIM_SIT_CROSS_LEGGED = 0x23;
// Body actions (BODY_COMMAND_ACTION) of carrying an object: picks it up or puts it down,
// with both hands / one hand (inferred).
constexpr int BODY_ACTION_CARRY_OBJECT = 9;
constexpr int BODY_ACTION_CARRY_OBJECT_ONE_HAND = 12;
// The interaction of sitting down at a seat (BODY_COMMAND_INTERACT).
constexpr int INTERACTION_SIT = 0x408;

// AttackMoveTo's ways of firing (DecideAttack).
enum : int {
    FIRE_NONE = 0,
    FIRE_WALKING = 0x41,                                // goes on walking
    FIRE_CROUCHED = 0x42,                               // stops and crouches
    FIRE_STANDING = 0x43,                               // stops
};

template <class T>
void ReadIfFits(C_chunk &chunk, T &value)
{
    if (chunk.RemainingSize() >= static_cast<int>(sizeof(T)))
        chunk.Read(value);
}

S_vector4 Dir4Of(const S_vector &dir)
{
    return {dir.x, dir.y, dir.z, 0.0f};                 // (w: a stale local in the original)
}

// rand() * 3000 / 32767 + 500 ms: the attack's wait before it looks for a target again.
int AttackPause()
{
    return static_cast<int>(Rand01() * 3000.0f + 500.0f);
}

// The human sits at the weapon of a car or a static weapon (AttackActor / AttackFrame fire
// it). 0x0058f1b0 (__fastcall).
bool HasSeatWeaponToFire(const C_Human *human)
{
    C_Actor *vehicle = human->GetVehicle();
    if (!vehicle || (vehicle->GetType() != ACTOR_STATIC_WEAPON && vehicle->GetType() != ACTOR_CAR))
        return false;
    return vehicle->HasSeatWeapon(human->GetVehicleSeat());
}

// The human "sees" one of the enemies in view again (rand() picks it).
void SeeRandomEnemy(C_HumanController *controller, const C_Human *human)
{
    const std::vector<C_Human *> &enemies = human->GetSeenEnemies();
    if (enemies.empty())
        return;
    controller->OnEnemySeen(enemies[static_cast<unsigned>(std::rand()) % enemies.size()]);
}

}  // namespace

// ------------------------------------------------------------------------- life cycle

void C_ScriptedController::Attach(C_Human *human, C_HumanController *previous)
{
    C_AIController::Attach(human, previous);
    m_label = -1;
    m_fireLabel = -1;
    m_attack.Stop();
    m_scriptFlags = 0;
    const S_FrameData *data = m_human->GetFrame()->GetData();
    m_script = data ? data->script : nullptr;
    m_alarmType = 0;
    m_holdingObject = false;
    m_seated = false;
    m_ignoredAlarms = 0;
    m_moveFlags &= ~MOVE_ALARMED;
    m_fireTime = 0;
    m_carryMode = -1;
    m_suspended = false;
    m_waitForPlayer = false;
    m_fireFromSeat = false;
    m_fireFrame = nullptr;
    m_fireTarget = nullptr;
    m_formation = nullptr;
    m_turnTarget = nullptr;
    m_turning = false;
    m_animation[0] = '\0';
}

void C_ScriptedController::Detach()
{
    C_AIController::Detach();
    m_human = nullptr;
}

void C_ScriptedController::Update(int frameTime)
{
    if (m_moveFlags & MOVE_DEAD) {
        FlushMessages(true);
        return;
    }
    if (m_turning)
        TurnSeatWeapon();
    UpdateNetwork(frameTime);
    FlushMessages(true);
}

void C_ScriptedController::Tick(int timeDelta)
{
    // Network games: nothing to send while the movement is idle.
    if (m_netObject) {
        const C_HumanMovement *movement = m_human->m_body.movement;
        m_netObject->SetSendMask(movement->m_unknown1AC == 0 && movement->m_unknown9C0 == 0 ? 0 : 7);
    }
    if (!(m_moveFlags & MOVE_DEAD)) {
        UpdateTimers(timeDelta);
        std::memset(g_aiAxes, 0, sizeof(g_aiAxes));
        std::memset(g_aiButtons, 0, sizeof(g_aiButtons));
        m_axes = g_aiAxes;
        m_buttons = g_aiButtons;
        ProcessMeleeTrackerAlerts();
        if (m_attack.active)
            UpdateAttack(timeDelta);
        // Waiting for the player: stops while he is far, goes on once he is near.
        if (m_waitForPlayer) {
            if (const C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer()) {
                const S_vector4 &own = m_human->GetPosition4();
                const S_vector4 &their = player->GetPosition4();
                const S_vector4 dir = {their.x - own.x, their.y - own.y, their.z - own.z, 0.0f};
                const float square = dir.z * dir.z + dir.y * dir.y + dir.x * dir.x;
                if (square > m_stopDist2) {
                    if (!m_move->unknown58)
                        m_move->unknown58 = true;
                    if (m_watchPlayer)
                        m_facing->SetView(dir);
                } else if (square < m_resumeDist2 && m_move->unknown58) {
                    m_move->unknown58 = false;
                }
            }
        }
        ApplyAlertState();
        if (!m_human->GetVehicle()) {
            const bool inFormation = FollowFormation();
            const uint32_t before = m_move->flags;
            UpdateMove(timeDelta);
            if ((before & S_AIMoveOrder::FLAG_ROUTE) && !(m_move->flags & S_AIMoveOrder::FLAG_ROUTE)) {
                // The route ended: the script goes on (not after a formation's move).
                if (!inFormation) {
                    if (m_script)
                        m_script->OnActionDone(m_label);
                    m_label = -1;
                }
                if (m_attack.active)
                    m_attack.Stop();
            }
        } else if (m_turning) {
            TurnSeatWeapon();
        }
        if (m_attack.active)
            UpdateFireOrder(timeDelta);
        UpdateLook(timeDelta);
        // The stance: the attack's while it fires, else the script's.
        const uint32_t stance = m_attack.active && m_attack.stance ? m_attack.stance : m_scriptFlags;
        if (!(stance & m_moveFlags & MOVE_STANCE_MASK))
            SetStance(stance & MOVE_STANCE_MASK);
        UpdateDriving(timeDelta);
        if (m_fireTime != 0)
            UpdateFireTime(timeDelta);
        ApplyMoveControls();
        // (The original passes the byte in a register whose other bytes are left over; the
        // weapons only read the low byte.)
        ApplyWeaponControls(timeDelta, m_unknownB4 ? 1 : 0);
    }
    FlushMessages(true);
}

void C_ScriptedController::ControlVehicle()
{
    C_Actor *vehicle = m_human->GetVehicle();
    if (vehicle->GetType() == ACTOR_SUBMARINE)
        static_cast<C_Submarine *>(vehicle)->SetControls(m_axes[AXIS_FORWARD] - m_axes[AXIS_BACK],
                                                         m_axes[AXIS_LEFT] - m_axes[AXIS_RIGHT],
                                                         m_axes[8] - m_axes[9]);
}

// ------------------------------------------------------------------------- the alarm

void C_ScriptedController::RaiseAlarm(uint32_t kind, const S_vector4 &pos)
{
    m_alarmType = kind;
    m_moveFlags |= MOVE_ALARMED;
    m_alarmPos = pos;
    if (m_script)
        m_script->OnAlarm();
}

void C_ScriptedController::OnEnemySeen(C_Human *human)
{
    C_AIController::OnEnemySeen(human);
    if (!(m_moveFlags & MOVE_ALARMED) && !(m_ignoredAlarms & ALARM_ENEMY_SEEN))
        RaiseAlarm(ALARM_ENEMY_SEEN, WorldPos4Of(human->GetFrame()));
}

void C_ScriptedController::OnBodySeen(C_Human *human)
{
    C_AIController::OnBodySeen(human);
    if (!(m_moveFlags & MOVE_ALARMED) && !(m_ignoredAlarms & ALARM_BODY_SEEN))
        RaiseAlarm(ALARM_BODY_SEEN, WorldPos4Of(human->GetFrame()));
}

void C_ScriptedController::OnBodyHeard(C_Actor *source)
{
    C_AIController::OnBodyHeard(source);
    if (!(m_moveFlags & MOVE_ALARMED) && !(m_ignoredAlarms & ALARM_BODY_HEARD))
        RaiseAlarm(ALARM_BODY_HEARD, WorldPos4Of(source->GetFrame()));
}

void C_ScriptedController::OnFriendHeard(C_Actor *source)
{
    C_AIController::OnFriendHeard(source);
    if ((m_moveFlags & MOVE_ALARMED) || (m_ignoredAlarms & ALARM_FRIEND_HEARD))
        return;
    // At one of the enemies the friend (a human) sees, else at the friend.
    const std::vector<C_Human *> &enemies = static_cast<C_Human *>(source)->GetSeenEnemies();
    const I3D_frame *frame;
    if (enemies.empty())
        frame = source->GetFrame();
    else
        frame = enemies[std::rand() % static_cast<int>(enemies.size())]->GetFrame();
    RaiseAlarm(ALARM_FRIEND_HEARD, WorldPos4Of(frame));
}

void C_ScriptedController::OnLoudSound(C_Actor *source)
{
    // Not a friend's. (Fix: the original asks any actor, as a human, whether the human is
    // its friend.)
    if (source && source->GetType() == ACTOR_HUMAN && static_cast<C_Human *>(source)->IsFriendOf(m_human))
        return;
    C_AIController::OnLoudSound(source);
    // Fix: the original read the position of no source.
    if (!source)
        return;
    if (!(m_moveFlags & MOVE_ALARMED) && !(m_ignoredAlarms & ALARM_SHOT_AT))
        RaiseAlarm(ALARM_SHOT_AT, WorldPos4Of(source->GetFrame()));
}

void C_ScriptedController::OnSoundHeard(S_RememberedSound *sound)
{
    C_AIController::OnSoundHeard(sound);
    if (m_moveFlags & MOVE_ALARMED)
        return;
    uint32_t kind;
    switch (sound->chapter) {
    case SOUND_CHAPTER_PUPPET_MOVEMENT: kind = ALARM_FOOTSTEPS; break;
    case SOUND_CHAPTER_OBJECT_MANIPULATION: kind = ALARM_OBJECT_NOISE; break;
    case SOUND_CHAPTER_WEAPON_SHOOTING: kind = ALARM_GUNFIRE; break;
    case SOUND_CHAPTER_EXPLOSION: kind = ALARM_EXPLOSION; break;
    default: return;
    }
    if (!(m_ignoredAlarms & kind))
        RaiseAlarm(kind, {sound->pos.x, sound->pos.y, sound->pos.z, sound->posW});
}

void C_ScriptedController::OnShotAt(C_Actor *shooter, const S_vector &velocity)
{
    C_AIController::OnShotAt(shooter, velocity);
    if (!(m_moveFlags & MOVE_ALARMED) && !(m_ignoredAlarms & ALARM_SHOT_AT))
        RaiseAlarm(ALARM_SHOT_AT, WorldPos4Of(shooter->GetFrame()));
}

void C_ScriptedController::OnHit(int type, int zone, int health, C_Actor *attacker, const S_vector &pos,
                                 const S_vector &dir)
{
    C_AIController::OnHit(type, zone, health, attacker, pos, dir);
    // Hits of the types 1..3 by someone, while the human lives.
    if ((type == 1 || type == 2 || type == 3) && attacker && m_human->GetHealth() > 0 &&
        !(m_moveFlags & MOVE_ALARMED) && !(m_ignoredAlarms & ALARM_HIT))
        RaiseAlarm(ALARM_HIT, WorldPos4Of(attacker->GetFrame()));
}

void C_ScriptedController::SetAlarmType(uint32_t flags, bool set)
{
    // Enemies in view count at once when their sight is switched back on (no alarm yet).
    const bool seeAgain = !(m_moveFlags & MOVE_ALARMED) && set && (m_ignoredAlarms & ALARM_ENEMY_SEEN) &&
                          (flags & ALARM_ENEMY_SEEN);
    if (set)
        m_ignoredAlarms &= ~flags;
    else
        m_ignoredAlarms |= flags;
    if (seeAgain)
        SeeRandomEnemy(this, m_human);
}

void C_ScriptedController::SetAlarm(bool on)
{
    if (on)
        return;
    m_moveFlags &= ~MOVE_ALARMED;
    SeeRandomEnemy(this, m_human);
}

void C_ScriptedController::MoveToAlarm(int label)
{
    m_attack.Stop();
    if (m_move) {
        m_move->unknown54 = (m_scriptFlags & MOVE_PACE_MASK) | MOVE_FORWARD;
        m_move->SetPos(m_alarmPos);
    }
    LookAround(4);
    m_moveFlags &= ~MOVE_ALARMED;
    m_label = label;
}

void C_ScriptedController::StartScript()
{
    const S_FrameData *data = m_human->GetFrame()->GetData();
    m_script = data ? data->script : nullptr;
    if (m_script)
        m_script->OnAlarmDone();
    LookAround(0);
}

void C_ScriptedController::ResetAI()
{
    if (m_moveFlags & MOVE_DEAF) {
        m_moveFlags &= ~MOVE_DEAF;
        m_human->StartListening();
    }
    // (The new controller replaces this one.)
    C_Human *human = m_human;
    switch (human->GetRole()) {
    case HUMAN_ROLE_PLAYER:
        human->SetScripted(false);
        break;
    case HUMAN_ROLE_ENEMY:
        human->CreateController(m_events->IsSniper() ? HUMAN_CONTROLLER_SNIPER : HUMAN_CONTROLLER_ENEMY);
        break;
    case HUMAN_ROLE_NEUTRAL:
        human->CreateController(HUMAN_CONTROLLER_NEUTRAL);
        break;
    case HUMAN_ROLE_NPC_TEAM:
    case HUMAN_ROLE_NPC_TEAM_5:
        human->CreateController(HUMAN_CONTROLLER_SQUAD);
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------------------- moves

bool C_ScriptedController::StartMove(int16_t checkpoint, int label)
{
    int current = CHECKPOINT_NONE;
    if (m_currentCheckpoint) {
        const int index = m_checkpoints->GetIndex(m_currentCheckpoint);
        if (index >= 0 && index < m_checkpoints->GetCount())
            current = index;
    }
    if (checkpoint == static_cast<int16_t>(current)) {
        // There already: a route followed is replaced, the script goes on at once.
        if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE)
            m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
        if (m_script)
            m_script->OnActionDone(label);
        m_label = -1;
        return false;
    }
    if (m_move && checkpoint != -1) {
        m_move->unknown54 = (m_scriptFlags & MOVE_PACE_MASK) | MOVE_FORWARD;
        m_move->SetCheckpoint(checkpoint);
    }
    return true;
}

void C_ScriptedController::MoveTo(int16_t checkpoint, int label)
{
    m_attack.Stop();
    if (!StartMove(checkpoint, label))
        return;
    m_label = label;
    if (!(m_scriptFlags & MOVE_PACE_1))
        LowerWeapon();
}

void C_ScriptedController::AttackMoveTo(int16_t checkpoint, int label)
{
    m_attack.Stop();
    if (!StartMove(checkpoint, label))
        return;
    m_attack.Start();
    m_label = label;
    if (!(m_scriptFlags & MOVE_PACE_1))
        LowerWeapon();
}

void C_ScriptedController::SetWaitForPlayer(bool on, float stopDist2, float resumeDist2, bool watch)
{
    m_waitForPlayer = on;
    m_stopDist2 = stopDist2;
    m_resumeDist2 = resumeDist2;
    m_watchPlayer = watch;
}

uint16_t C_ScriptedController::FindCheckpoint(const char *name) const
{
    return static_cast<uint16_t>(m_checkpoints->Find(name));
}

void C_ScriptedController::SetFormation(C_FormationShape *formation)
{
    m_attack.Stop();
    m_formation = formation;
    if (!formation)
        return;
    // The move going on ends (the script goes on).
    if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE) {
        m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
        if (m_script)
            m_script->OnActionDone(m_label);
    }
    m_label = -1;
    m_alert->Set(1);
}

bool C_ScriptedController::FollowFormation()
{
    if (!m_formation || m_human == m_formation->GetLeader())
        return false;
    S_vector4 pos;
    uint16_t checkpoint = CHECKPOINT_NONE;
    if (!m_formation->GetSlotTarget(m_human, &pos, &checkpoint))
        return true;
    // The slot: the pace by how far it is (between 2 and 4 m, and within 1 m, the pace
    // stays as it was).
    uint32_t pace = m_move->unknown54 & MOVE_PACE_MASK;
    if (m_human->m_body.IsWorkState5())
        m_move->SetPos(pos);
    else
        m_move->SetCheckpointPos(static_cast<int16_t>(checkpoint), pos);
    const S_vector4 &own = m_human->GetPosition4();
    const float dx = own.x - pos.x;
    const float dy = own.y - pos.y;
    const float dz = own.z - pos.z;
    const float square = dz * dz + dy * dy + dx * dx;
    if (square > 144.0f) {
        LowerWeapon();
        m_move->unknown54 = MOVE_PACE_MARCH | MOVE_FORWARD;
        return true;
    }
    if (square > 64.0f) {
        m_move->unknown54 = MOVE_PACE_4 | MOVE_FORWARD;
        return true;
    }
    if (square < 16.0f) {
        if (square > 4.0f) {
            m_move->unknown54 = MOVE_PACE_3 | MOVE_FORWARD;
            return true;
        }
        if (square < 1.0f)
            pace = MOVE_PACE_1;
    }
    m_move->unknown54 = pace | MOVE_FORWARD;
    return true;
}

// ------------------------------------------------------------------------- move modes

void C_ScriptedController::SetMoveModeRun()
{
    m_scriptFlags = (m_scriptFlags & ~0xF000u) | MOVE_PACE_4;
}

void C_ScriptedController::SetMoveModeWalk()
{
    m_scriptFlags = (m_scriptFlags & ~0xF000u) | MOVE_PACE_3;
}

void C_ScriptedController::SetMoveModeSilence()
{
    m_scriptFlags = (m_scriptFlags & ~0xF000u) | MOVE_PACE_1;
}

void C_ScriptedController::SetMoveModeGuard()
{
    m_scriptFlags = (m_scriptFlags & ~0xF000u) | MOVE_PACE_2;
}

void C_ScriptedController::SetMoveModeLie()
{
    m_scriptFlags = (m_scriptFlags & ~MOVE_STANCE_MASK) | MOVE_LYING;
}

void C_ScriptedController::SetMoveModeCrouch()
{
    m_scriptFlags = (m_scriptFlags & ~MOVE_STANCE_MASK) | MOVE_CROUCHING;
}

void C_ScriptedController::SetMoveModeStand()
{
    if (m_seated) {
        m_seated = false;
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, WORK_ANIM_STAND_UP));
        return;
    }
    m_scriptFlags = (m_scriptFlags & ~MOVE_STANCE_MASK) | MOVE_STANDING;
}

void C_ScriptedController::SetMoveModeStandFast()
{
    if (m_seated) {
        m_seated = false;
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, WORK_ANIM_STAND_UP_FAST));
        return;
    }
    m_scriptFlags = (m_scriptFlags & ~MOVE_STANCE_MASK) | MOVE_STANDING;
}

void C_ScriptedController::Sit(I3D_frame *seat)
{
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_INTERACT, INTERACTION_SIT, FrameArg(seat)));
    m_seated = true;
}

void C_ScriptedController::ActivityTurk()
{
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, WORK_ANIM_SIT_CROSS_LEGGED));
    m_seated = true;
}

void C_ScriptedController::DisableEvents()
{
    m_moveFlags |= MOVE_DEAF;
    m_human->StopListening();
}

void C_ScriptedController::EnableEvents()
{
    m_moveFlags &= ~MOVE_DEAF;
    m_human->StartListening();
}

void C_ScriptedController::SetMode(int mode)
{
    m_moveFlags ^= (m_moveFlags ^ static_cast<uint32_t>(mode)) & MOVE_AI_MODE_MASK;
}

bool C_ScriptedController::HasAIMode() const
{
    // (The original returns the masked flags, whose low byte is always 0: false.)
    return static_cast<uint8_t>(m_moveFlags & MOVE_AI_MODE_MASK) != 0;
}

void C_ScriptedController::StopAction()
{
    m_attack.Stop();
    m_fireTime = 0;
    if (m_moveFlags & MOVE_FIRING)
        StopFiring(0);
    if (m_move)
        m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
}

void C_ScriptedController::Suspend(bool on)
{
    if (on == m_suspended)
        return;
    m_suspended = on;
    m_human->Freeze(on);
    if (on) {
        m_moveFlags |= MOVE_DEAF;
        m_human->StopListening();
    } else {
        m_moveFlags &= ~MOVE_DEAF;
        m_human->StartListening();
    }
    if (IsLocalGame())
        return;
    S_MsgSuspend *message = new S_MsgSuspend;
    message->on = on;
    SendToClients(message);
    if (on)
        FlushMessages(true);
}

void C_ScriptedController::SetOptimized(int mode)
{
    if (mode == m_carryMode)
        return;
    m_human->SetCarryMode(mode);
    m_carryMode = mode;
}

// ------------------------------------------------------------------------- objects, weapons

void C_ScriptedController::PickObject(I3D_frame *object, bool oneHand)
{
    const int action = oneHand ? BODY_ACTION_CARRY_OBJECT_ONE_HAND : BODY_ACTION_CARRY_OBJECT;
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, action, FrameArg(object)));
    m_holdingObject = true;
}

void C_ScriptedController::DropObject()
{
    if (!m_holdingObject)
        return;
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_CARRY_OBJECT));
    m_holdingObject = false;
}

void C_ScriptedController::TakeWeapon()
{
    // The long gun, else the sidearm.
    m_human->m_body.movement->m_unknown190 |= 1;
    // (Fix: the original calls through a null container when the human has none.)
    C_ItemContainer *longGun = m_human->GetContainer(ITEM_CONTAINER_LONG_GUN);
    if (longGun && longGun->GetItem())
        m_human->DrawLongGun();
    else
        m_human->DrawSidearm(2);
}

void C_ScriptedController::HolsterWeapon()
{
    // Back to the long gun place. (The original asks the item's behaviour first and drops
    // the answer.)
    C_Item *item = HandsItemOf(m_human);
    if (item)
        m_human->RequestMoveItem(item, m_human, ITEM_CONTAINER_HANDS, ITEM_CONTAINER_LONG_GUN, 0);
}

void C_ScriptedController::Reload()
{
    if (m_buttons)
        m_buttons[BUTTON_RELOAD] = 1.0f;
}

int C_ScriptedController::PlayActivity(int activity)
{
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, activity));
    return S_HumanBody::GetActivityAnimLength(activity, m_human->GetStance());
}

int C_ScriptedController::PlayActivity(int activity, I3D_frame *target)
{
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, activity, FrameArg(target)));
    return S_HumanBody::GetActivityAnimLength(activity, m_human->GetStance());
}

bool C_ScriptedController::SeesEnemy() const
{
    return !m_human->GetSeenEnemies().empty();
}

void C_ScriptedController::SetAnimation(const char *name, int blendTime, int cancelTime, bool loop)
{
    m_human->PlayAnimation(name, blendTime, cancelTime, loop);
    if (IsLocalGame())
        return;
    // Fix: the original copied the name without bounds (and a null one through null).
    if (!name || !*name || !loop) {
        m_animation[0] = '\0';
    } else {
        std::strncpy(m_animation, name, sizeof(m_animation) - 1);
        m_animation[sizeof(m_animation) - 1] = '\0';
        m_animationFrame = cancelTime;
    }
    S_MsgAnimation *message = new S_MsgAnimation;
    if (name) {
        std::strncpy(message->name, name, sizeof(message->name) - 1);
        message->name[sizeof(message->name) - 1] = '\0';
    }
    message->cancelTime = cancelTime;
    message->blendTime = blendTime;
    message->loop = loop;
    SendToClients(message);
}

void C_ScriptedController::TurnAt(I3D_frame *target)
{
    if (target) {
        const S_vector &own = m_human->GetFrame()->GetWorldPos();
        const S_vector &to = target->GetWorldPos();
        S_vector dir(to.x - own.x, to.y - own.y, to.z - own.z);
        NormalizeBy(dir, dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        SetFacingDir(Dir4Of(dir));
        SetViewDir(Dir4Of(dir));
    }
    // In a vehicle's seat with a weapon: the weapon turns to it (Update / Tick).
    C_Actor *vehicle = m_human->GetVehicle();
    if (InVehicleSeat(vehicle) && vehicle->HasSeatWeapon(m_human->GetVehicleSeat())) {
        m_turning = true;
        m_turnTarget = target;
        return;
    }
    m_turning = false;
}

void C_ScriptedController::TurnSeatWeapon()
{
    C_Actor *vehicle = m_human->GetVehicle();
    if (!InVehicleSeat(vehicle)) {
        m_turning = false;
        return;
    }
    const int seat = m_human->GetVehicleSeat();
    S_vector dir;
    float w = 0.0f;                                     // (a stale local in the original)
    if (m_turnTarget) {
        // Toward the target, until the weapon points at it. (The difference is not
        // normalized for the test: a target farther than a metre or so passes it early.)
        const S_vector &own = m_human->GetFrame()->GetWorldPos();
        const S_vector &to = m_turnTarget->GetWorldPos();
        dir = S_vector(to.x - own.x, to.y - own.y, to.z - own.z);
        const S_vector aim = vehicle->GetSeatWeaponAim(seat);
        if (!(aim.z * dir.z + aim.y * dir.y + aim.x * dir.x < 0.985f)) {
            m_turning = false;
            return;
        }
        NormalizeBy(dir, dir.z * dir.z + dir.y * dir.y + dir.x * dir.x);
    } else {
        // The vehicle's way (normalized three times, as in the original); pointing that way,
        // nothing (the turning goes on being checked).
        const S_matrix &m = vehicle->GetFrame()->GetMatrix();
        dir = S_vector(m.m[2][0], m.m[2][1], m.m[2][2]);
        w = m.m[2][3];
        NormalizeBy(dir, dir.y * dir.y + dir.z * dir.z + dir.x * dir.x);
        S_vector aim = vehicle->GetSeatWeaponAim(seat);
        NormalizeBy(dir, dir.z * dir.z + dir.y * dir.y + dir.x * dir.x);
        NormalizeBy(aim, aim.y * aim.y + aim.z * aim.z + aim.x * aim.x);
        if (!(aim.z * dir.z + aim.y * dir.y + aim.x * dir.x < 0.99999f))
            return;
        NormalizeBy(dir, dir.z * dir.z + dir.y * dir.y + dir.x * dir.x);
    }
    const S_vector4 dir4 = {dir.x, dir.y, dir.z, w};
    SetFacingDir(dir4);
    SetViewDir(dir4);
}

// ------------------------------------------------------------------------- firing

void C_ScriptedController::AttackActor(C_Actor *target, int time, bool fake, int label)
{
    m_fireFromSeat = false;
    if (!m_human->GetVehicle()) {
        if (!m_human->GetHandsWeapon())
            return;
    } else {
        if (!HasSeatWeaponToFire(m_human))
            return;
        m_fireFromSeat = true;
        m_turning = false;
    }
    m_fireTime = time;
    m_fireTarget = target;
    m_unknownB4 = fake;
    m_fireLabel = label;
}

void C_ScriptedController::AttackFrame(I3D_frame *target, int time, bool fake, int label)
{
    m_fireFromSeat = false;
    if (!m_human->GetVehicle()) {
        if (!m_human->GetHandsWeapon())
            return;
    } else {
        if (!HasSeatWeaponToFire(m_human))
            return;
        m_fireFromSeat = true;
        m_turning = false;
    }
    m_fireTime = time;
    m_fireTarget = nullptr;
    m_fireFrame = target;
    m_unknownB4 = fake;
    m_fireLabel = label;
}

void C_ScriptedController::UpdateFireTime(int timeDelta)
{
    UpdateHandsItem();
    if (m_fireFromSeat) {
        AimAndFireSeatWeapon();
    } else if (m_handsItem && m_fire.pauseTime <= 0) {
        if (!(m_moveFlags & MOVE_FIRING)) {
            // A burst at the actor, or toward the frame.
            S_vector4 dir;
            if (!m_fireTarget) {
                m_fire.flags |= S_AIFire::FLAG_DIRECTION;
                dir = GetDirToFrame(m_fireFrame);
            } else {
                m_fire.flags = 0;
                dir = GetAimDirTo(m_fireTarget);
            }
            SetViewDir(dir);
            m_fire.target = m_fireTarget;
            StartFiring();
        } else {
            // Bursts of the weapon's length, 300..700 ms apart.
            UpdateFiring(timeDelta);
            if (m_fire.burstTime == 0)
                StopFiring(static_cast<uint32_t>(static_cast<int>(RandFloat() * 400.0f + 300.0f)));
            if (m_fire.flags & S_AIFire::FLAG_RELOAD)
                PressReload();
        }
    }
    m_fireTime -= timeDelta;
    if (m_fireTime < 1) {
        // The time is over: the script goes on.
        m_fireTime = 0;
        StopFiring(0);
        if (m_script)
            m_script->OnActionDone(m_fireLabel);
        m_fireLabel = -1;
    }
}

S_vector C_ScriptedController::GetSeatAimDir(bool scatter)
{
    // The point: a human where it will be (GetLeadPoint), anything else 0.75 m above its
    // frame (or the frame of AttackFrame).
    const I3D_frame *frame = m_fireTarget ? m_fireTarget->GetFrame() : m_fireFrame;
    C_Actor *vehicle = m_human->GetVehicle();
    const S_vector origin = vehicle->GetSeatWeaponOrigin(m_human->GetVehicleSeat());
    S_vector point;
    if (m_fireTarget && m_fireTarget->GetType() == ACTOR_HUMAN) {
        const S_vector4 lead = GetLeadPoint(static_cast<C_Human *>(m_fireTarget));
        point = S_vector(lead.x, lead.y, lead.z);
    } else {
        const S_vector &pos = frame->GetWorldPos();
        point = S_vector(pos.x, pos.y + 0.75f, pos.z);
    }
    const float dx = point.x - origin.x;
    const float dy = point.y - origin.y;
    const float dz = point.z - origin.z;
    const float k = MagnitudeBy(dz * dz + dy * dy + dx * dx) * 0.06f;
    if (scatter) {
        const float r1 = Rand01();
        const float z = (r1 + r1) - 1.0f;
        const float y = Rand01() - 0.5f;
        const float r3 = Rand01();
        const float x = (r3 + r3) - 1.0f;
        point.x += x * k;
        point.y += y * k;
        point.z += z * k;
    }
    return point - origin;
}

void C_ScriptedController::AimAndFireSeatWeapon()
{
    // Turns the weapon to the target; a gun fires pointing at it within 10 degrees, scattered,
    // a cannon (component kind 2) within 1 degree, true.
    C_Actor *vehicle = m_human->GetVehicle();
    const int seat = m_human->GetVehicleSeat();
    S_vector dir = GetSeatAimDir(false);
    S_vector aim = vehicle->GetSeatWeaponAim(seat);
    C_ItemDef *weapon = vehicle->GetSeatWeaponDef(seat);
    const bool cannon = weapon && weapon->GetComponentKind() == ICK_PROJECTILE_WEAPON;
    NormalizeBy(aim, aim.y * aim.y + aim.z * aim.z + aim.x * aim.x);
    NormalizeBy(dir, dir.z * dir.z + dir.y * dir.y + dir.x * dir.x);
    SetViewDir(Dir4Of(dir));
    if (vehicle->GetSeatWeaponRounds(seat) == 0) {
        m_axes[AXIS_RELOAD] = 1.0f;
        return;
    }
    const float dot = aim.z * dir.z + aim.y * dir.y + dir.x * aim.x;
    if (!(dot > (cannon ? 0.9998f : 0.985f)))
        return;
    S_vector shot = GetSeatAimDir(!cannon);
    NormalizeBy(shot, shot.z * shot.z + shot.y * shot.y + shot.x * shot.x);
    SetViewDir(Dir4Of(shot));
    m_axes[AXIS_FIRE] = 1.0f;
    m_buttons[BUTTON_FIRE] = 1.0f;
}

// ------------------------------------------------------------------------- AttackMoveTo

void C_ScriptedController::S_Attack::Start()
{
    active = true;
    state = STATE_DECIDE;
    time = 500;
    stance = 0;
    target = nullptr;
}

void C_ScriptedController::S_Attack::Stop()
{
    if (active)
        state = STATE_STOP;
}

void C_ScriptedController::S_Attack::Clear()
{
    active = false;
    state = STATE_NONE;
    time = 0;
    stance = 0;
    target = nullptr;
}

bool C_ScriptedController::S_Attack::SaveGame(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_ATTACK_STATE);
    chunk.Write(active);
    chunk.Write(state);
    chunk.Write(time);
    chunk.Write(stance);
    chunk.Ascend();
    WriteActorChunk(chunk, CHUNK_ATTACK_TARGET, target);
    return true;
}

bool C_ScriptedController::S_Attack::LoadGame(C_chunk &chunk)
{
    target = nullptr;
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        if (id == CHUNK_ATTACK_STATE) {
            ReadIfFits(chunk, active);
            ReadIfFits(chunk, state);
            ReadIfFits(chunk, time);
            ReadIfFits(chunk, stance);
        } else if (id == CHUNK_ATTACK_TARGET) {
            target = ReadActor(chunk);
        }
        chunk.Ascend();
    }
    return true;
}

void C_ScriptedController::DecideAttack()
{
    S_Attack &attack = m_attack;
    if (attack.state == S_Attack::STATE_FIRING) {
        // The firing time is over: the weapon down, walking on.
        LowerWeapon();
        m_alert->Set(1);
        m_fireOrder->SetTarget(nullptr);
        m_move->unknown58 = false;
        attack.time = AttackPause();
        attack.target = nullptr;
        attack.state = S_Attack::STATE_DECIDE;
        attack.stance = 0;
        return;
    }
    if (attack.state != S_Attack::STATE_DECIDE)
        return;
    if (!m_handsItem) {
        attack.time = AttackPause();
        return;
    }
    C_Human *target = PickTarget(nullptr);
    attack.target = target;
    if (!target) {
        attack.time = AttackPause();
        return;
    }
    // How to fire by the weapon's behaviour and the distance: within range, mostly stopping
    // to fire (behaviours 2 and 3 else fire walking, 4 and 12 else crouch); 5 crouches. The
    // firing lasts 0..1.5 s more than the base time.
    const float distance = GetDistanceTo(target);
    int mode = FIRE_NONE;
    int base = 0;
    switch (BehaviourOf(m_handsItem)) {
    case 2:
        if (distance < 30.0f)
            mode = Rand01() * 10.0f < 7.0f ? FIRE_STANDING : FIRE_WALKING;
        base = 300;
        break;
    case 3:
        if (distance < 70.0f)
            mode = Rand01() * 10.0f < 5.0f ? FIRE_STANDING : FIRE_WALKING;
        base = 300;
        break;
    case 4:
        if (distance < 250.0f)
            mode = Rand01() * 10.0f < 7.0f ? FIRE_STANDING : FIRE_CROUCHED;
        base = 1000;
        break;
    case 12:
        if (distance < 300.0f)
            mode = Rand01() * 10.0f < 7.0f ? FIRE_STANDING : FIRE_CROUCHED;
        base = 1000;
        break;
    case 5:
        if (distance < 300.0f)
            mode = FIRE_CROUCHED;
        base = 600;
        break;
    default:
        break;
    }
    if (mode == FIRE_NONE) {
        attack.time = AttackPause();
        return;
    }
    RaiseWeapon();
    m_alert->Set(mode == FIRE_CROUCHED ? 2 : 4);
    m_fireOrder->SetTarget(attack.target);
    if (mode != FIRE_WALKING)
        m_move->unknown58 = true;
    attack.time = static_cast<int>(Rand01() * 1500.0f + static_cast<float>(base));
    attack.stance = mode == FIRE_CROUCHED ? MOVE_CROUCHING : MOVE_STANDING;
    attack.state = S_Attack::STATE_FIRING;
}

void C_ScriptedController::UpdateAttack(int timeDelta)
{
    S_Attack &attack = m_attack;
    if (attack.state == S_Attack::STATE_STOP) {
        // Stopped (the move ended, another command): the weapon down, the attack over.
        LowerWeapon();
        m_alert->Set(1);
        m_fireOrder->SetTarget(nullptr);
        m_move->unknown58 = false;
        attack.time = AttackPause();
        attack.state = S_Attack::STATE_NONE;
        attack.active = false;
        attack.target = nullptr;
        attack.stance = 0;
        return;
    }
    attack.time -= timeDelta;
    if (!m_human->GetSeenEnemies().empty()) {
        if (attack.time <= 0) {
            DecideAttack();
            return;
        }
        // Firing: a new target when this one is dead.
        if (attack.state == S_Attack::STATE_FIRING &&
            (!attack.target || static_cast<C_Human *>(attack.target)->GetHealth() <= 0)) {
            C_Human *target = PickTarget(nullptr);
            attack.target = target;
            m_fireOrder->SetTarget(target);
        }
        return;
    }
    // Nobody in view: the firing ends.
    if (attack.state != S_Attack::STATE_FIRING)
        return;
    LowerWeapon();
    m_alert->Set(1);
    m_fireOrder->SetTarget(nullptr);
    m_move->unknown58 = false;
    attack.time = AttackPause();
    attack.state = S_Attack::STATE_DECIDE;
    attack.target = nullptr;
    attack.stance = 0;
}

// ------------------------------------------------------------------------- events

void C_ScriptedController::ProcessMeleeTrackerAlerts()
{
    if (!m_events)
        return;
    std::deque<S_HumanEvent *> &pending = m_events->m_pending;
    for (size_t i = pending.size(); i-- > 0;) {
        S_HumanEvent *event = pending[i];
        if (event->type != AI_EVENT_MELEE_TRACKER_ALERT)
            continue;
        // A shot heard there (from what the meleeTracker barks at), by the human and the humans near.
        C_Actor *source = IdToActor(event->sourceId);
        S_RememberedSound *sound =
            AllocRememberedSound(SOUND_CHAPTER_WEAPON_SHOOTING, event->pos, source, timeGetTime());
        ReleaseHumanEvent(event);
        pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
        m_human->m_heardSounds.push_back(sound);
        OnSoundHeard(sound);
        for (int j = m_events->GetNearbyActorCount(); j-- > 0;) {
            C_Actor *actor = m_events->GetNearbyActor(j);
            // Fix: the original read a nearby actor that is gone.
            if (!actor || actor->GetType() != ACTOR_HUMAN)
                continue;
            if (C_HumanController *controller = static_cast<C_Human *>(actor)->GetController())
                controller->OnSoundHeard(sound);
        }
    }
}

// ------------------------------------------------------------------------- the rest

void C_ScriptedController::AdvanceScript(int /*a*/, int /*b*/)
{
    if (m_script)
        m_script->Advance();
}

void C_ScriptedController::PlaySpeech(int group)
{
    // A speech sample of `group`, from the body, not heard by the AI.
    S_SoundRequest request;
    request.flags = SOUND_SILENT_TO_AI | SOUND_KEEP_SLOT;
    request.attachTo = m_human->GetBodyFrame();
    request.source = m_human;
    request.chapter = SOUND_CHAPTER_SPEECH;
    request.group = group;
    GetSoundModule()->Play(request);
}

void C_ScriptedController::LookAround1(int /*arg*/)
{
    LookAround(1);
}

// ------------------------------------------------------------------------- saved games

void C_ScriptedController::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_SCRIPTED_AI);
    C_AIController::SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_SCRIPTED_STATE);
    chunk.Write(&m_alarmPos, SAVED_VECTOR_SIZE);
    // (The waiting for the player as the original's 12 bytes: the two flags, two bytes of
    // padding, the two distances.)
    uint8_t wait[12] = {};
    wait[0] = m_waitForPlayer;
    wait[1] = m_watchPlayer;
    std::memcpy(wait + 4, &m_stopDist2, 4);
    std::memcpy(wait + 8, &m_resumeDist2, 4);
    chunk.Write(wait, sizeof(wait));
    chunk.Write(m_scriptFlags);
    chunk.Write(m_ignoredAlarms);
    chunk.Write(m_fireTime);
    chunk.Write(m_carryMode);
    chunk.Write(m_label);
    chunk.Write(m_fireLabel);
    chunk.Write(m_unknown1BE, sizeof(m_unknown1BE));
    chunk.Write(m_holdingObject);
    chunk.Write(m_seated);
    chunk.Write(m_suspended);
    chunk.Write(m_alarmType);
    chunk.Ascend();
    chunk.Begin(CHUNK_SCRIPTED_FIRE_FROM_SEAT);
    chunk.Write(m_fireFromSeat);
    chunk.Ascend();
    if (m_fireFrame)
        chunk.WriteStringChunk(CHUNK_SCRIPTED_FIRE_FRAME, m_fireFrame->GetName());
    if (m_fireTarget) {
        const int id = m_fireTarget->GetNetId();
        chunk.WriteChunk(CHUNK_SCRIPTED_FIRE_TARGET, &id, 4);
    }
    chunk.Begin(CHUNK_SCRIPTED_ATTACK);
    m_attack.SaveGame(chunk);
    chunk.Ascend();
    if (m_turnTarget)
        chunk.WriteStringChunk(CHUNK_SCRIPTED_TURN_TARGET, m_turnTarget->GetName());
    chunk.Begin(CHUNK_SCRIPTED_TURNING);
    chunk.Write(m_turning);
    chunk.Ascend();
}

void C_ScriptedController::LoadGame(C_chunk &chunk)
{
    m_fireTarget = nullptr;
    m_fireFromSeat = false;
    m_turnTarget = nullptr;
    m_turning = false;
    m_fireFrame = nullptr;
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_SCRIPTED_AI:
            C_AIController::LoadGame(chunk);
            break;
        case CHUNK_SCRIPTED_STATE: {
            chunk.Read(&m_alarmPos, SAVED_VECTOR_SIZE);
            if (chunk.RemainingSize() >= 12) {
                uint8_t wait[12];
                chunk.Read(wait, sizeof(wait));
                m_waitForPlayer = wait[0] != 0;
                m_watchPlayer = wait[1] != 0;
                std::memcpy(&m_stopDist2, wait + 4, 4);
                std::memcpy(&m_resumeDist2, wait + 8, 4);
            }
            ReadIfFits(chunk, m_scriptFlags);
            ReadIfFits(chunk, m_ignoredAlarms);
            ReadIfFits(chunk, m_fireTime);
            ReadIfFits(chunk, m_carryMode);
            ReadIfFits(chunk, m_label);
            ReadIfFits(chunk, m_fireLabel);
            chunk.Read(m_unknown1BE, sizeof(m_unknown1BE));
            chunk.Read(m_holdingObject);
            chunk.Read(m_seated);
            chunk.Read(m_suspended);
            chunk.Read(m_alarmType);
            break;
        }
        case CHUNK_SCRIPTED_FIRE_TARGET: {
            int actorId = 0;
            chunk.Read(actorId);
            if (C_Actor *actor = g_actorsModule.GetActorById(actorId))
                m_fireTarget = actor;
            break;
        }
        case CHUNK_SCRIPTED_FIRE_FRAME:
        case CHUNK_SCRIPTED_TURN_TARGET: {
            // (Fix: the original copies up to 999 bytes without terminating them.)
            char name[1000];
            chunk.ReadString(name, sizeof(name));
            if (I3D_scene *scene = GetGame()->GetScene()) {
                I3D_frame *frame = scene->FindFrame(name, FRAME_MASK_ALL);
                if (id == CHUNK_SCRIPTED_FIRE_FRAME)
                    m_fireFrame = frame;
                else
                    m_turnTarget = frame;
            }
            break;
        }
        case CHUNK_SCRIPTED_FIRE_FROM_SEAT:
            chunk.Read(m_fireFromSeat);
            break;
        case CHUNK_SCRIPTED_ATTACK:
            m_attack.LoadGame(chunk);
            break;
        case CHUNK_SCRIPTED_TURNING:
            chunk.Read(m_turning);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    // The freeze and the deafness as saved.
    m_human->Freeze(m_suspended);
    if (m_moveFlags & MOVE_DEAF) {
        m_moveFlags |= MOVE_DEAF;
        m_human->StopListening();
    } else {
        m_moveFlags &= ~MOVE_DEAF;
        m_human->StartListening();
    }
}

// ------------------------------------------------------------------------- C_NeutralController

void C_NeutralController::Attach(C_Human *human, C_HumanController *previous)
{
    C_AIController::Attach(human, previous);
}

void C_NeutralController::Detach()
{
    C_AIController::Detach();
    m_human = nullptr;
}
