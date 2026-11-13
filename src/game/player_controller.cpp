// C_PlayerController, the controller of the local player's soldier and the server's copy of a
// network player (original source 0x0053b9a0..0x0053d870, 0x00540090..0x00540240,
// 0x00542060..0x00542b10, 0x005449b0, 0x00545dd0..0x00546dc0): its life cycle, the keys and
// the mouse (HandleInput), holding the breath, the squad commands spoken before they are
// carried out, the soldier's comments, the sight test, hits, saved games, the binoculars and
// the scopes, the medkit. The weapons are in player_weapons.cpp, the network side in
// player_net.cpp.
#include "game/first_person_view.h"
#include "game/human_controller.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/campaign.h"
#include "game/collision.h"
#include "game/controller_internal.h"
#include "game/controller_messages.h"
#include "game/controller_net.h"
#include "game/game.h"
#include "game/heavy_fire_support.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/interface.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/player_internal.h"
#include "game/profile.h"
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "game/squad_commander.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// Saved game chunks.
enum : uint16_t {
    CHUNK_PLAYER_BASE = 0x83F,                          // C_HumanController::SaveGame
    CHUNK_PLAYER_STATE = 0x840,
    CHUNK_PLAYER_COMMAND = 0x841,                       // a spoken command not carried out yet
};
// The pending command as saved: the original's object with its vtable pointer (0x40 bytes).
constexpr uint32_t ACTION_COMMAND_VTABLE = 0x00810528;
constexpr int ACTION_COMMAND_SAVED_SIZE = 0x40;

// The controls HandleInput reads (E_ControlAxis / E_ControlButton indexes).
enum : int {
    BUTTON_PACE_DOWN = 4,
    BUTTON_PACE_UP = 5,
    BUTTON_HOLD_BREATH = 0x13,
    BUTTON_TOGGLE_HUD = 0x1A,
    BUTTON_DROP_WEAPON = 0x22,
    AXIS_MOUSE_X = 0x34,
    AXIS_MOUSE_Y = 0x35,
};

// The view: the mouse's sensitivity (normal, the sights, the scopes and the binoculars), the
// pitch limits (80 degrees, then the original's second clamp at 89.8), the turn a car seat that
// keeps the hands item or climbing allows from the human's frame (60 degrees).
constexpr float MOUSE_SENSITIVITY = 0.3f;
constexpr float MOUSE_SENSITIVITY_SIGHTS = 0.1f;
constexpr float MOUSE_SENSITIVITY_ZOOM = 0.05f;
constexpr float MAX_PITCH = 1.3962635f;                 // 0x3FB2B8C3
constexpr float MAX_PITCH_2 = 1.5676548f;               // 0x3FC8A8EA
constexpr float MAX_TURN_FROM_FRAME = 1.0471976f;
constexpr float PI = 3.1415927f;
constexpr float TWO_PI = 6.2831855f;

// Holding the breath: the progress text and the sounds (chapter SOUND_CHAPTER_ACTION).
constexpr int TEXT_HOLDING_BREATH = 0x1A57;
constexpr int SOUND_HOLD_BREATH = 0x1F;
constexpr int SOUND_BREATHE_OUT = 0x20;
// Let go early, the breath comes back from at least this much (ms).
constexpr int BREATH_AFTER_RELEASE = 2000;

// The commands whose sentence has a whispered variant (the next one).
constexpr int FIRST_WHISPERED_COMMAND = 0x15;
constexpr int LAST_WHISPERED_COMMAND = 0x20;
// The radios (item types "Radio-AirAttack", "Radio-Artillery") and how far they reach.
constexpr int ITEM_RADIO_AIR_ATTACK = 0xF3;
constexpr int ITEM_RADIO_ARTILLERY = 0xF4;
constexpr float RADIO_RANGE = 500.0f;
// The planes come in this high above the target.
constexpr float AIR_ATTACK_HEIGHT = 60.0f;

// The squad commands' gestures (work animations; GESTURE_NONE none) and sentences, in pairs per
// command (0x0083c358, commands 0..0x2A), followed by the sentences of SQUAD_COMMAND_ON_ACTOR
// by the actor's command (0x0083c4b0, E_HumanCommandType 0..0x26; SENTENCE_NONE none). One
// array as in memory: the original indexes the pairs past their end for higher commands.
constexpr int GESTURE_NONE = WORK_ANIMATION_END_ACTIVITY;
constexpr int SENTENCE_NONE = 0x8A;
constexpr int COMMAND_PAIRS = 0x2B;
constexpr int SAY_SENTENCES = 0x27;
const int COMMAND_SPEECH[COMMAND_PAIRS * 2 + SAY_SENTENCES] = {
    0x4C, 0x00, 0x00, 0x01, 0x4C, 0x02, 0x01, 0x03, 0x4C, 0x04, 0x02, 0x05, 0x4C, 0x06, 0x03, 0x07,
    0x4C, 0x08, 0x04, 0x09, 0x4C, 0x7D, 0x02, 0x7E, 0x4C, 0x0A, 0x05, 0x0B, 0x4C, 0x0C, 0x06, 0x0D,
    0x4C, 0x0E, 0x07, 0x0F, 0x4C, 0x10, 0x4C, 0x12, 0x4C, 0x14, 0x4C, 0x16, 0x4C, 0x18, 0x4C, 0x1A,
    0x4C, 0x1C, 0x4C, 0x1E, 0x4C, 0x20, 0x4C, 0x22, 0x4C, 0x24, 0x4C, 0x26, 0x4C, 0x28, 0x4C, 0x2A,
    0x4C, 0x2C, 0x4C, 0x8A, 0x4C, 0x8A, 0x4C, 0x82, 0x4C, 0x83, 0x4C, 0x84, 0x4C, 0x85, 0x4C, 0x86,
    0x4C, 0x87, 0x4C, 0x88, 0x4C, 0x89,
    // SQUAD_COMMAND_ON_ACTOR's sentences.
    0x2E, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x40, 0x42,
    0x44, 0x8A, 0x48, 0x4A, 0x8A, 0x4E, 0x50, 0x52, 0x54, 0x56, 0x56, 0x58, 0x5A, 0x81, 0x8A, 0x8A,
    0x8A, 0x3A, 0x8A, 0x8A, 0x56, 0x12, 0x32,
};
constexpr int COMMAND_SPEECH_COUNT = static_cast<int>(sizeof(COMMAND_SPEECH) / sizeof(COMMAND_SPEECH[0]));

// The gesture and the sentence of `command`. (Fix: the original reads on past the tables for
// commands over 0x3D; they get none.)
bool GetCommandSpeech(int command, int *gesture, int *sentence)
{
    if (command < 0 || command * 2 + 1 >= COMMAND_SPEECH_COUNT)
        return false;
    *gesture = COMMAND_SPEECH[command * 2];
    *sentence = COMMAND_SPEECH[command * 2 + 1];
    return true;
}

// The soldier's comments (single player).
enum : int {
    SENTENCE_ENEMY_FAR = 0x5C,                          // an enemy 20 m away or more
    SENTENCE_ENEMY_NEAR = 0x5E,
    SENTENCE_ENEMY_LOST = 0x60,
    SENTENCE_KILLED_ENEMY = 0x6A,
};
constexpr float NEAR_SQUARE = 400.0f;                   // 20 m

// The player's sight: 1.4 times the target's visibility, a triangle 120 degrees wide.
constexpr float PLAYER_SIGHT_FACTOR = 1.4f;
constexpr double PLAYER_HALF_FOV = 1.0471975803375244;  // 60 degrees
// TestSight's status (not read by the callers).
constexpr int SIGHT_TESTED = 99;
constexpr int SIGHT_SKIPPED = 500;

// The sound chapters the HUD's indicator shows (the player's steps and the weapons' noises).
constexpr int SOUND_HEARD_CHAPTERS[] = {SOUND_CHAPTER_PUPPET_MOVEMENT, SOUND_CHAPTER_EXPLOSION,
                                        SOUND_CHAPTER_WEAPON_SHOOTING};

// The co-op lives' messages when the player's human is killed (5 s).
constexpr int TEXT_NO_LIVES_LEFT = 0x4F56;
constexpr int TEXT_KILLED = 0x4F58;
constexpr int KILLED_MESSAGE_TIME = 5000;

// OnHit: the helmet's zone (the head item falls off), the body's zone where the weapon may fall
// from the hands (one hit in 20; carry modes 1, 3 and 4).
constexpr int HIT_ZONE_HEAD_ITEM = 0xB;
constexpr int HIT_ZONE_WEAPON = 3;

// The view kicks of a hit: single player on difficulty 1 (fixed), else by the endurance.
constexpr int DIFFICULTY_EASY = 1;

// The score of a medic per health given (network games).
constexpr float MEDIC_SCORE_PER_HEALTH = 0.004f;

// The z axis of `frame`, normalized with the sum the original uses, the matrix's fourth value
// as w.
S_vector4 FrameAxisZ(const I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    S_vector axis(m.m[2][0], m.m[2][1], m.m[2][2]);
    NormalizeBy(axis, (axis.x * axis.x + axis.y * axis.y) + axis.z * axis.z);
    return ToVector4(axis, m.m[2][3]);
}

// The human sits in a car's seat that keeps the hands item (SEAT_FLAG_ARMED).
bool InArmedCarSeat(const C_Human *human)
{
    const C_Actor *vehicle = human->GetVehicle();
    return vehicle && vehicle->GetType() == ACTOR_CAR &&
           (const_cast<C_Actor *>(vehicle)->GetSeatFlags(human->GetVehicleSeat()) & SEAT_FLAG_ARMED);
}

// `yaw` kept within 60 degrees of where the human's frame faces. The frame's angle stays in the
// FPU's precision (double here); the sums and the results are the float operations.
float ClampYawToFrame(const C_Human *human, float yaw)
{
    const S_matrix &m = human->GetFrame()->GetMatrix();
    S_vector axis(m.m[2][0], m.m[2][1], m.m[2][2]);
    NormalizeBy(axis, (axis.y * axis.y + axis.z * axis.z) + axis.x * axis.x);
    double frameYaw = std::atan2(static_cast<double>(axis.x), static_cast<double>(axis.z));
    if (frameYaw < 0.0)
        frameYaw = static_cast<float>(frameYaw + TWO_PI);
    else if (frameYaw > TWO_PI)
        frameYaw = static_cast<float>(frameYaw - TWO_PI);
    float turn = static_cast<float>(yaw - frameYaw);
    if (turn > PI)
        turn -= TWO_PI;
    else if (turn < -PI)
        turn += TWO_PI;
    if (turn > MAX_TURN_FROM_FRAME)
        return static_cast<float>(frameYaw + MAX_TURN_FROM_FRAME);
    if (turn < -MAX_TURN_FROM_FRAME)
        return static_cast<float>(frameYaw - MAX_TURN_FROM_FRAME);
    return yaw;
}

// The controls a scripted view or an interaction leaves to the player (the interface's).
bool IsKeptControl(int index)
{
    return (index >= 21 && index <= 24) || (index >= 26 && index <= 31) || (index >= 35 && index <= 38) ||
           (index >= 46 && index <= 48) || index == 62;
}

// The map's AIRCRAFTATACK value for a caller of `team` (an axis caller gets the second). (Fix:
// the original reads past a single player mission, which has none: 0.)
int AircraftAttackKind(int team)
{
    const C_MissionDef *mission = GetGame()->GetMission();
    if (mission->GetType() != CAMPAIGN_NODE_MP_MISSION)
        return 0;
    const C_MpMissionDef *mpMission = static_cast<const C_MpMissionDef *>(mission);
    return team != TEAM_AXIS ? mpMission->GetAircraftAttackAxis() : mpMission->GetAircraftAttackAllied();
}

// The aimed field of view of the hands item's scope (0 without one).
float ScopeFovOf(const C_Item *item)
{
    // (Fix: the original reads the scope of the hands item without checking either.)
    if (!item || !item->GetDef()->GetSecondaryComponent())
        return 0.0f;
    return static_cast<const C_ScopeComp *>(item->GetDef()->GetSecondaryComponent())->GetAimedFovFactor();
}

// The breath's sounds at the body.
void PlayBreathSound(const C_Human *human, int group)
{
    S_SoundRequest request;
    request.flags = SOUND_SILENT_TO_AI | SOUND_AMBIENT;
    request.chapter = SOUND_CHAPTER_ACTION;
    request.group = group;
    const S_vector4 pos = WorldPos4Of(human->GetBodyFrame());
    request.pos = XYZ(pos);
    request.posW = pos.w;
    GetSoundModule()->Play(request);
}

// A weapon's fields of the item in the hands: ready to be dropped (not busy, no reload
// pending, the attack delay over). (Fix: the original reads them from any item.)
bool IsReadyToDrop(const C_Item *item)
{
    const uint32_t now = GetGame()->GetMissionTime();
    if (item->GetTime() > now)
        return false;
    if (item->GetKind() != ITEM_KIND_WEAPON)
        return true;
    const C_WeaponItem *weapon = static_cast<const C_WeaponItem *>(item);
    return !(weapon->GetFlags() & WEAPON_RELOAD_PENDING) && static_cast<int>(now - weapon->GetAttackTime()) >= 0;
}

}  // namespace

// ------------------------------------------------------------------------- life cycle

C_PlayerController::C_PlayerController()
    : C_HumanController(HUMAN_CONTROLLER_PLAYER)
{
    // (The original sets only the pending command's vtable and category here; Attach the rest.)
}

void C_PlayerController::Attach(C_Human *human, C_HumanController *previous)
{
    C_HumanController::Attach(human, previous);
    for (int i = 0; i < CONTROL_COUNT; i++) {
        m_controlAxes[i] = 0.0f;
        m_controlButtons[i] = 0.0f;
        m_unknown2C4[i] = 0.0f;
        m_unknown3C4[i] = 0.0f;
    }
    m_latchReload = 0;
    m_latchFire2 = 0;
    m_latchFire = 0;
    m_latchThrow = 0;
    m_throwDelay = 0;
    m_squadCombatOrder = SQUAD_ORDER_MODE0;
    C_CameraModule *camera = GetGame()->GetCameraModule();
    if (previous && previous->GetType() == HUMAN_CONTROLLER_SQUAD) {
        // Taking over a squad member: the view of the camera, what the member went on with.
        camera->GetCameraDir(3, &m_viewDir);
        m_facingDir = m_viewDir;
        C_SquadController *squad = static_cast<C_SquadController *>(previous);
        m_squadFlags = squad->GetFlags();
        m_whispering = squad->IsQuiet();
        m_squadCombatOrder = squad->GetCombatOrder();
    } else {
        m_squadFlags = 0;
        camera->SetViewDir(m_viewDir);
    }
    // The look around ends (pose 0).
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_POSE, 0));
    if (IsLocalGame()) {
        m_netObject = nullptr;
    } else {
        m_netObject->SetMessageFactory(NetCreateMessage, this);
        m_netObject->SetStateHandlers(NetReadControlBits, NetWriteControlBits, this);
        m_netObject->SetSendMask(7);
        m_netObject->SetForwarding(false);
        if (!IsServer()) {
            m_netObject->SetLive(0);
        } else {
            g_netReplication->SetWriteEpoch(0);
            g_netReplication->SetReadEpoch(1);
        }
        m_netObject->GetProperty(1)->GetTrack().SetValue(m_human->GetPosition4());
    }
    m_unknown524 = false;
    m_scopeDelay = 0;
    m_ordersChanged = true;
    m_commandDelay = 0;
    m_vip = false;
    m_spectating = false;
    m_charging = false;
    m_chargeTime = 0;
    m_holdingBreath = false;
    m_breath = BREATH_FULL;
    m_human->m_body.ClearMovementFlags(6);
    m_viewMode = -1;
    SetViewMode(VIEW_NORMAL);
    m_human->SetLeader(nullptr);
}

void C_PlayerController::Detach()
{
    // A driver lets go of the car (single player: any seat).
    C_Actor *vehicle = m_human->m_vehicle;
    if (vehicle && vehicle->GetType() == ACTOR_CAR && (m_human->m_seat == 0 || IsLocalGame()))
        static_cast<C_Car *>(vehicle)->SetInput(0.0f, 0.0f);
    if (!IsLocalGame()) {
        // (The base's Detach is not called: the human's net object stays, without handlers.)
        m_netObject->SetMessageFactory(NetCreateMessage, nullptr);
        m_netObject->SetStateHandlers(NetReadControlBits, NetWriteControlBits, nullptr);
        m_netObject->SetSendMask(0);
    } else {
        GetSquadCommander()->ReleaseFollowers(m_human);
    }
    SetViewMode(VIEW_NORMAL);
    m_human = nullptr;
}

void C_PlayerController::OnDeath()
{
    C_HumanController::OnDeath();
    if (IsLocalGame() && m_human->IsLocalPlayer())
        GetGame()->GetFirstPersonView()->SetWeaponView(nullptr, 0, -1);
    SetViewMode(VIEW_NORMAL);
    if (IsLocalGame() || !m_vip)
        return;
    // Objectives games: the VIP died. The first VIP objective is decided by the VIP's team
    // (against the attacking side of the first objective).
    C_MissionDef *mission = GetGame()->GetMission();
    int index = 0;
    for (;; index++) {
        if (index >= mission->GetObjectiveCount())
            return;
        if (static_cast<S_MpObjective *>(mission->GetObjective(index))->type == OBJECTIVE_TYPE_VIP)
            break;
    }
    const int attackers = static_cast<S_MpObjective *>(mission->GetObjective(0))->team;
    const int vipTeam = g_netGame.GetPlayerTeam(GetGame()->GetVipSlot(), false);
    GetGame()->SetObjectiveStatus(index + 1, attackers == vipTeam ? OBJECTIVE_FAILED : OBJECTIVE_COMPLETED);
}

void C_PlayerController::OnKilled()
{
    C_HumanController::OnKilled();
    // A network game's driver takes the foot off the throttle.
    C_Actor *vehicle = m_human->m_vehicle;
    if (!IsLocalGame() && vehicle && vehicle->GetType() == ACTOR_CAR && vehicle->GetSeatOccupant(0) == m_human)
        static_cast<C_Car *>(vehicle)->GetVehicle()->SetThrottle(0.0f, true);
    m_holdingBreath = false;
    m_breath = 0;
    m_human->SetActionProgress(0, 0);
    if (m_human->IsLocalPlayer())
        GetGame()->GetFirstPersonView()->SetWeaponView(nullptr, 0, -1);
    SetViewMode(VIEW_NORMAL);
    if (IsLocalGame() || m_human->GetNetId() != g_localPlayerSlot)
        return;
    // The player's own human: the co-op lives left.
    C_Interface *ui = GetGame()->GetInterface();
    if (!g_gameProfile.IsCooperativeGame()) {
        ui->ShowTimer(TEXT_KILLED, KILLED_MESSAGE_TIME);
        return;
    }
    if (g_mpSettings->teamLives == 0) {
        S_NetPlayerState *players = g_netGame.LockPlayers();
        if (g_mpSettings->coopLives <= players[g_localPlayerSlot].livesUsed)
            ui->ShowTimer(TEXT_NO_LIVES_LEFT, KILLED_MESSAGE_TIME);
        g_netGame.UnlockPlayers();
        return;
    }
    if (g_mpSettings->coopLives <= GetGame()->GetTeamLivesUsed())
        ui->ShowTimer(TEXT_NO_LIVES_LEFT, KILLED_MESSAGE_TIME);
}

void C_PlayerController::Update(int frameTime)
{
    m_unknownBC++;
    FlushMessages(true);
    FlushMessages(false);
    // A spectator's human (or one not in play) leaves the world.
    const bool detached = m_spectating || !m_human->m_inPlay;
    C_HumanMovement *movement = m_human->GetMovement();
    if (movement->IsDetached() != detached) {
        movement->SetDetachedFlag(detached);
        movement->OnFrozen(detached);
        m_human->GetFrame()->SetOn(!detached);
    }
#ifndef HD2_DEDICATED   // the server has only the server's side
    // This machine's spectator looks along the view of the player it follows.
    if (m_spectating && IsClient() && g_localPlayerSlot == m_human->GetNetId()) {
        C_game *game = GetGame();
        if (game->GetCameraMode() == GAME_CAMERA_OWN && game->GetSpectatedSlot() >= 0) {
            C_Human *watched = static_cast<C_Human *>(g_actorsModule.GetActorById(game->GetSpectatedSlot()));
            if (watched)
                game->GetCameraModule()->SetViewDir(Dir4(watched->GetViewDir()));
        }
    }
#endif
    if ((m_moveFlags & MOVE_DEAD) || m_spectating)
        return;
    if (m_throwDelay != 0) {
        m_throwDelay -= frameTime;
        if (m_throwDelay < 0)
            m_throwDelay = 0;
    }
    // The spoken command is carried out after its sentence.
    if (m_commandDelay != 0) {
        m_commandDelay -= frameTime;
        if (m_commandDelay < 1) {
            m_commandDelay = 0;
            ExecutePendingCommand();
        }
    }
    if (!IsServer())
        UpdateInteraction(frameTime);
#ifndef HD2_DEDICATED   // the server has only the server's side
    if (IsClient())
        TickLocal(frameTime);
#endif
    if (IsServer())
        UpdateRemote(frameTime);
}

void C_PlayerController::Tick(int timeDelta)
{
    // The net object replicates the human while its movement's +0x1AC / +0x9C0 is set.
    if (m_netObject)
        m_netObject->SetSendMask(m_human->GetMovement()->IsRespawnBlocked() ? 7 : 0);
    if (m_ordersChanged)
        m_ordersChanged = false;
    if (m_moveFlags & MOVE_DEAD)
        return;
    UpdateInteraction(timeDelta);
    // The weapon keys pressed since the last tick count for it.
    if (m_buttons) {
        if (m_latchThrow != 0)
            m_buttons[BUTTON_THROW] = static_cast<float>(m_latchThrow);
        if (m_latchFire != 0)
            m_buttons[BUTTON_FIRE] = static_cast<float>(m_latchFire);
        if (m_latchFire2 != 0)
            m_buttons[BUTTON_7] = static_cast<float>(m_latchFire2);
        if (m_latchReload != 0)
            m_buttons[BUTTON_RELOAD] = static_cast<float>(m_latchReload);
    }
    WeaponControls(timeDelta);
    if (m_buttons) {
        if (m_latchThrow != 0)
            m_buttons[BUTTON_THROW] = 0.0f;
        if (m_latchFire != 0)
            m_buttons[BUTTON_FIRE] = 0.0f;
        if (m_latchFire2 != 0)
            m_buttons[BUTTON_7] = 0.0f;
        if (m_latchReload != 0)
            m_buttons[BUTTON_RELOAD] = 0.0f;
    }
    m_latchThrow = 0;
    m_latchFire = 0;
    m_latchFire2 = 0;
    m_latchReload = 0;
    // The human faces where its frame does.
    const S_matrix &m = m_human->GetFrame()->GetMatrix();
    S_vector facing(m.m[2][0], m.m[2][1], m.m[2][2]);
    NormalizeBy(facing, (facing.y * facing.y + facing.x * facing.x) + facing.z * facing.z);
    m_facingDir = ToVector4(facing, m.m[2][3]);
    FlushMessages(true);
}

// ------------------------------------------------------------------------- the keys and the mouse

void C_PlayerController::HandleInput(float *held, float *pressed)
{
    m_axes = held;
    m_buttons = pressed;
    if (!held || !pressed)
        return;
    C_game *game = GetGame();
    C_CameraModule *camera = game->GetCameraModule();
    // The HUD on / off (single player, or a network game with the third person view).
    if (!camera->HasScriptedView() && (IsLocalGame() || (g_mpSettings->thirdPersonView != 0 && !m_spectating)) &&
        pressed[BUTTON_TOGGLE_HUD] != 0.0f) {
        game->ShowFirstPersonView(!game->IsFirstPersonView());
        g_hudOption->shown = game->IsFirstPersonView();
        if (!game->IsFirstPersonView())
            SetViewMode(VIEW_NORMAL);
    }
    // A scripted view or an interaction going on: only the interface's controls are left.
    if (camera->HasScriptedView() || (m_interaction != -1 && m_interactionTime > 0)) {
        for (int i = CONTROL_COUNT; i-- > 0;) {
            if (!IsKeptControl(i)) {
                pressed[i] = 0.0f;
                held[i] = 0.0f;
            }
        }
        return;
    }

    // --- the view
    float dx = held[AXIS_MOUSE_X];
    float dy = held[AXIS_MOUSE_Y];
    const bool invertMouse = g_gameProfile.GetDetailSettings()->invertMouse;
    if (invertMouse)
        dy = -dy;
    bool mouseLook = true;
    if (!(m_moveFlags & MOVE_DEAD) && !m_spectating && InVehicleSeat(m_human->m_vehicle)) {
        C_Actor *vehicle = m_human->m_vehicle;
        const int seat = m_human->m_seat;
        const uint32_t flags = vehicle->GetSeatFlags(seat);
        if (flags & SEAT_FLAG_CONTROLS) {
            // At the controls: the view along the human, no mouse look.
            dx = 0.0f;
            dy = 0.0f;
            m_viewDir = FrameAxisZ(m_human->GetFrame());
        }
        if (flags & SEAT_FLAG_WEAPON) {
            // The seat's weapon takes the mouse (inverted as the player wants).
            if (invertMouse)
                held[AXIS_MOUSE_Y] = -held[AXIS_MOUSE_Y];
            vehicle->ApplySeatControls(seat, held, pressed);
            if (invertMouse)
                held[AXIS_MOUSE_Y] = -held[AXIS_MOUSE_Y];
            mouseLook = false;
        } else if (flags & SEAT_FLAG_HIDDEN) {
            // Inside: the view of the seat's camera.
            if (I3D_frame *seatCamera = vehicle->GetSeatCamera(seat))
                m_viewDir = FrameAxisZ(seatCamera);
            mouseLook = false;
        }
    }
    if (mouseLook && (dx != 0.0f || dy != 0.0f)) {
        // The mouse turns the view: slower through the sights, the scopes and the binoculars
        // (not while a reload keeps the scope away).
        float sensitivity = MOUSE_SENSITIVITY;
        if (m_viewMode != VIEW_NORMAL && m_scopeDelay <= 0)
            sensitivity = m_viewMode == VIEW_IRON_SIGHTS ? MOUSE_SENSITIVITY_SIGHTS : MOUSE_SENSITIVITY_ZOOM;
        const float turn = dx * sensitivity;
        const float tilt = dy * sensitivity;
        float horizontal = std::sqrt(m_viewDir.x * m_viewDir.x + m_viewDir.z * m_viewDir.z);
        float yaw = static_cast<float>(std::atan2(static_cast<double>(m_viewDir.x), static_cast<double>(m_viewDir.z)));
        if (turn != 0.0f) {
            yaw = turn * PI + yaw;
            if (yaw < 0.0f)
                yaw += TWO_PI;
            else if (yaw > TWO_PI)
                yaw -= TWO_PI;
            if (InArmedCarSeat(m_human) || m_human->m_body.GetClimbState() != 0)
                yaw = ClampYawToFrame(m_human, yaw);
        }
        if (tilt != 0.0f) {
            float pitch = static_cast<float>(
                std::atan2(static_cast<double>(m_viewDir.y), static_cast<double>(horizontal)) - static_cast<double>(tilt * PI));
            if (pitch > MAX_PITCH)
                pitch = MAX_PITCH;
            else if (pitch < -MAX_PITCH)
                pitch = -MAX_PITCH;
            // (The second clamp cannot act after the first.)
            if (MAX_PITCH_2 < pitch)
                pitch = MAX_PITCH_2;
            if (-MAX_PITCH_2 > pitch)
                pitch = -MAX_PITCH_2;
            m_viewDir.y = static_cast<float>(std::sin(static_cast<double>(pitch)));
            horizontal = static_cast<float>(std::cos(static_cast<double>(pitch)));
        }
        m_viewDir.x = static_cast<float>(std::sin(static_cast<double>(yaw)) * horizontal);
        m_viewDir.z = static_cast<float>(std::cos(static_cast<double>(yaw)) * horizontal);
    } else {
        // No mouse look: a car seat that keeps the hands item holds the view within reach of
        // the car's direction.
        const float horizontal = std::sqrt(m_viewDir.x * m_viewDir.x + m_viewDir.z * m_viewDir.z);
        const double angle = std::atan2(static_cast<double>(m_viewDir.x), static_cast<double>(m_viewDir.z));
        float yaw = static_cast<float>(angle);
        if (angle < 0.0)
            yaw += TWO_PI;
        else if (yaw > TWO_PI)
            yaw -= TWO_PI;
        if (InArmedCarSeat(m_human)) {
            yaw = ClampYawToFrame(m_human, yaw);
            m_viewDir.x = static_cast<float>(std::sin(static_cast<double>(yaw)) * horizontal);
            m_viewDir.z = static_cast<float>(std::cos(static_cast<double>(yaw)) * horizontal);
        }
    }
    m_facingDir = m_viewDir;
    game->GetCameraModule()->SetViewDir(m_viewDir);
    if ((m_moveFlags & MOVE_DEAD) || m_spectating)
        return;

    // --- the keys
    if (m_buttons[BUTTON_DROP_WEAPON] != 0.0f && game->GetFirstPersonView()->CanUseWeapon() && !m_charging) {
        // (Fix: the original calls through a missing hands container.)
        C_ItemContainer *hands = m_human->GetContainer(ITEM_CONTAINER_HANDS);
        C_Item *item = hands ? hands->GetItem() : nullptr;
        if (item && IsReadyToDrop(item))
            m_human->RequestThrowItem(item, ITEM_CONTAINER_HANDS, XYZ(m_viewDir));
    }
    if (m_buttons[BUTTON_THROW] != 0.0f)
        m_latchThrow = 1;
    if (m_buttons[BUTTON_FIRE] != 0.0f)
        m_latchFire = 1;
    if (m_buttons[BUTTON_7] != 0.0f)
        m_latchFire2 = 1;
    if (m_buttons[BUTTON_RELOAD] != 0.0f)
        m_latchReload = 1;
    if (m_buttons[BUTTON_HOLD_BREATH] != 0.0f)
        HoldBreath(true);
    if (m_holdingBreath && m_axes[BUTTON_HOLD_BREATH] == 0.0f)
        HoldBreath(false);
    // The pace: down to the slowest, up to marching (standing, with the stamina for it).
    if (pressed[BUTTON_PACE_DOWN] > 0.0f) {
        if (m_stateFlags == MOVE_PACE_MARCH)
            m_stateFlags = MOVE_PACE_4;
        else if (m_stateFlags == MOVE_PACE_4)
            m_stateFlags = MOVE_PACE_3;
        else if (m_stateFlags == MOVE_PACE_3)
            m_stateFlags = MOVE_PACE_1;
    } else if (pressed[BUTTON_PACE_UP] != 0.0f) {
        if (m_stateFlags == MOVE_PACE_1) {
            m_stateFlags = MOVE_PACE_3;
        } else if (m_stateFlags == MOVE_PACE_3) {
            m_stateFlags = MOVE_PACE_4;
        } else if (m_stateFlags == MOVE_PACE_4 && (m_human->GetStanceFlags() & MOVE_STANDING) &&
                   static_cast<float>(m_human->m_stats[STAT_ENDURANCE]) * 0.1f < m_human->m_stamina) {
            m_stateFlags = MOVE_PACE_MARCH;
        }
    }
    // Carrying a body, at most pace 3; climbing or working, neither the slowest nor marching.
    if (m_human->m_carried && (m_stateFlags & (MOVE_PACE_MARCH | MOVE_PACE_4)))
        m_stateFlags = MOVE_PACE_3;
    if (m_human->m_body.GetClimbState() != 0 || m_human->m_body.IsWorkState5()) {
        if (m_stateFlags == MOVE_PACE_1)
            m_stateFlags = MOVE_PACE_3;
        else if (m_stateFlags == MOVE_PACE_MARCH)
            m_stateFlags = MOVE_PACE_4;
    }
}

void C_PlayerController::HoldBreath(bool on)
{
    if (on && m_viewMode == VIEW_SCOPE) {
        // Only with the full breath and the weapon ready.
        UpdateHandsItem();
        if (!m_handsItem)                               // (Fix: the original reads a missing hands item)
            return;
        const uint32_t now = GetGame()->GetMissionTime();
        if (m_handsItem->GetKind() == ITEM_KIND_WEAPON &&   // (Fix: a weapon's field of any item)
            static_cast<C_WeaponItem *>(m_handsItem)->GetReadyTime() > now)
            return;
        if (m_handsItem->GetTime() > now || m_breath != BREATH_FULL)
            return;
        m_holdingBreath = on;
        GetGame()->Print("Holding breath...");
        m_human->SetActionProgress(BREATH_FULL, TEXT_HOLDING_BREATH);
        PlayBreathSound(m_human, SOUND_HOLD_BREATH);
        return;
    }
    if (!m_holdingBreath)
        return;
    m_holdingBreath = false;
    if (m_breath < BREATH_AFTER_RELEASE)
        m_breath = BREATH_AFTER_RELEASE;
    PlayBreathSound(m_human, SOUND_BREATHE_OUT);
    m_human->SetActionProgress(0, 0);
    GetGame()->Print("Breath again...");
}

// ------------------------------------------------------------------------- squad commands

void C_PlayerController::SetWhisperingFor(int command)
{
    // The commands come in pairs, said aloud and whispered.
    switch (command) {
    case 0x00: case 0x02: case 0x04: case 0x06: case 0x08: case 0x0A: case 0x0C: case 0x0E:
    case 0x10: case 0x12: case 0x13: case 0x14: case SQUAD_COMMAND_ON_ACTOR:
        GetSquadCommander()->SetWhispering(false);
        return;
    case 0x01: case 0x03: case 0x05: case 0x07: case 0x09: case 0x0B: case 0x0D: case 0x0F:
    case 0x11: case SQUAD_COMMAND_ON_ACTOR_QUIET:
        GetSquadCommander()->SetWhispering(true);
        return;
    default:
        return;
    }
}

void C_PlayerController::ExecutePendingCommand()
{
    GetSquadCommander()->ExecuteActionCommand(m_pendingCommand);
}

bool C_PlayerController::OnSquadCommand(const S_MsgAction &message)
{
    const S_ActionCommand &command = message.command;
    const int id = command.action;
    if (id == SQUAD_COMMAND_AIR_ATTACK || id == SQUAD_COMMAND_ARTILLERY)
        return DesignateTarget(id, XYZ(message.pos), XYZ(command.dir));
    int gesture = GESTURE_NONE;
    int sentence = SENTENCE_NONE;
    if (!IsLocalGame()) {
        // The server: the soldier's gesture and sentence for the others.
        if (id != SQUAD_COMMAND_ON_ACTOR && id != SQUAD_COMMAND_ON_ACTOR_QUIET && GetCommandSpeech(id, &gesture, &sentence)) {
            if (gesture != GESTURE_NONE)
                m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, gesture));
            Say(sentence, 100, 1, false);
        }
        return true;
    }
    // Single player: the squad hears it, then carries it out after the sentence.
    SetWhisperingFor(id);
    m_whispering = GetSquadCommander()->IsWhispering();
    m_commandDelay = 0;
    bool say = false;
    if (id == SQUAD_COMMAND_ON_ACTOR || id == SQUAD_COMMAND_ON_ACTOR_QUIET) {
        const int index = command.command;
        if (index >= 0 && index < SAY_SENTENCES && COMMAND_SPEECH[COMMAND_PAIRS * 2 + index] != SENTENCE_NONE) {
            sentence = COMMAND_SPEECH[COMMAND_PAIRS * 2 + index];
            if (id == SQUAD_COMMAND_ON_ACTOR_QUIET)
                sentence++;
            say = true;
        }
    } else if (GetCommandSpeech(id, &gesture, &sentence)) {
        if (gesture != GESTURE_NONE)
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, gesture));
        if (id >= FIRST_WHISPERED_COMMAND && id <= LAST_WHISPERED_COMMAND && m_whispering)
            sentence++;
        say = true;
    }
    if (say)
        m_commandDelay = Say(sentence, 100, 1, false);
    if (m_commandDelay == 0)
        m_commandDelay = static_cast<int>(RandFloat() * 500.0f + 1000.0f);
    m_pendingCommand.category = command.category;
    m_pendingCommand.dir = command.dir;
    m_pendingCommand.action = command.action;
    m_pendingCommand.command = command.command;
    m_pendingCommand.actor = command.actor;
    m_pendingCommand.value = command.value;
    m_pendingCommand.member = command.member;
    return true;
}

bool GetSquadCommandGesture(int command, int *gesture)
{
    int sentence;
    if (command == SQUAD_COMMAND_ON_ACTOR || command == SQUAD_COMMAND_ON_ACTOR_QUIET || !GetCommandSpeech(command, gesture, &sentence))
        return false;
    return *gesture != GESTURE_NONE;
}

void C_PlayerController::PlayCommandGesture(const S_MsgAction &message)
{
    int gesture;
    if (GetSquadCommandGesture(message.command.action, &gesture))
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, gesture));
}

bool C_PlayerController::DesignateTarget(int command, const S_vector &origin, const S_vector &dir)
{
    // The radio's target: where the view meets the ground within 500 m.
    if (command != SQUAD_COMMAND_ARTILLERY && command != SQUAD_COMMAND_AIR_ATTACK)
        return false;
    if (!m_human->FindItem(command == SQUAD_COMMAND_ARTILLERY ? ITEM_RADIO_ARTILLERY : ITEM_RADIO_AIR_ATTACK))
        return false;
    const S_vector ray(dir.x * RADIO_RANGE, dir.y * RADIO_RANGE, dir.z * RADIO_RANGE);
    S_vector hit;
    if (!g_collisionWorld.RayTest(origin, ray, &hit, nullptr, 1, 0))
        return false;
    const int team = m_human->GetTeam(false);
    if (command == SQUAD_COMMAND_ARTILLERY) {
        S_ArtilleryRequest request;
        request.x = hit.x;
        request.z = hit.z;
        request.caller = m_human;
        request.kind = AircraftAttackKind(team);
        request.team = team;
        GetHeavyFireSupport()->RequestArtillery(request);
        return true;
    }
    // The planes come from behind the caller.
    S_AirAttackRequest request;
    request.x = hit.x;
    request.z = hit.z;
    request.dirX = -dir.x;
    request.dirZ = -dir.z;
    request.kind = IsLocalGame() ? (team != TEAM_AXIS ? 1 : 0) : AircraftAttackKind(team);
    const float r = RandFloat();
    request.planes = static_cast<int>(r + r + 1.0f);
    request.height = hit.y + AIR_ATTACK_HEIGHT;
    request.caller = m_human;
    request.team = team;
    GetHeavyFireSupport()->RequestAirAttack(request);
    return true;
}

// ------------------------------------------------------------------------- the soldier's comments

void C_PlayerController::SayNear(int sentence, int delay)
{
    if (!IsLocalGame()) {
        // Network games: said quietly when another player is within 20 m.
        const S_vector &pos = m_human->GetFrame()->GetWorldPos();
        bool force = true;
        for (int i = 32; i-- > 0;) {
            C_Actor *actor = g_actorsModule.GetActor(i);
            // (Fix: the original takes any actor of the first slots for a human.)
            if (actor == m_human || !actor || actor->GetType() != ACTOR_HUMAN)
                continue;
            C_Human *other = static_cast<C_Human *>(actor);
            if (other->m_stats[STAT_HEALTH] <= 0 || other->IsSpectating() || !other->IsInPlay())
                continue;
            const S_vector &otherPos = other->GetFrame()->GetWorldPos();
            const float dx = otherPos.x - pos.x;
            const float dy = otherPos.y - pos.y;
            const float dz = otherPos.z - pos.z;
            if ((dz * dz + dx * dx) + dy * dy <= NEAR_SQUARE)
                force = false;
        }
        Say(sentence, delay, 1, force);
        return;
    }
    // Single player: whispered with the squad around while undiscovered.
    if (m_human->GetDisguisePercent() == 0 && GetSquadCommander()->HasGroupMembers(C_SquadCommander::GROUP_NEAR)) {
        Say(sentence + (m_whispering ? 1 : 0), delay, 1, false);
        return;
    }
    Say(sentence, delay, 1, true);
}

void C_PlayerController::OnEnemySeen(C_Human *human)
{
    if (!IsLocalGame())
        return;
    C_Team *team = g_teamManager.GetPlayerTeam();
    if (!team->IsEnemyKnown(human)) {
        const S_vector offset = human->GetPosition() - m_human->GetPosition();
        const float square = (offset.x * offset.x + offset.y * offset.y) + offset.z * offset.z;
        SayNear(square >= NEAR_SQUARE ? SENTENCE_ENEMY_FAR : SENTENCE_ENEMY_NEAR, 5000);
    }
    g_teamManager.GetPlayerTeam()->GetEnemyRecords().Add(human);
}

void C_PlayerController::OnEnemyLost(C_Human *human)
{
    if (!IsLocalGame())
        return;
    g_teamManager.GetPlayerTeam()->GetEnemyRecords().Remove(human);
    if (!g_teamManager.GetPlayerTeam()->IsEnemyKnown(human))
        SayNear(SENTENCE_ENEMY_LOST, 5000);
}

void C_PlayerController::OnKilledEnemy(C_Human * /*victim*/)
{
    if (IsLocalGame())
        SayNear(SENTENCE_KILLED_ENEMY, 300);
}

// ------------------------------------------------------------------------- senses

void C_PlayerController::TestSightOfHuman(C_Human *target, int *status, float *visibility)
{
    if (m_moveFlags & MOVE_DEAD) {
        *status = SIGHT_SKIPPED;
        *visibility = 0.0f;
        return;
    }
    *status = SIGHT_TESTED;
    float range = target->GetVisibility() * PLAYER_SIGHT_FACTOR;
    const float viewDistance = GetGame()->GetCameraModule()->GetViewDistance();
    if (viewDistance < range)
        range = viewDistance;
    const S_vector &eye = m_human->GetBodyFrame()->GetWorldPos();
    const S_vector &targetBody = target->GetBodyFrame()->GetWorldPos();
    const float dx = eye.x - targetBody.x;
    const float dy = eye.y - targetBody.y;
    const float dz = eye.z - targetBody.z;
    if (!(MagnitudeBy((dz * dz + dy * dy) + dx * dx) <= range)) {
        *visibility = 0.0f;
        return;
    }
    // A triangle ahead of the body in the xz plane: `range` deep, tan(60 degrees) * range to
    // each side.
    const S_matrix &body = m_human->GetBodyFrame()->GetMatrix();
    S_vector axis(body.m[2][0], body.m[2][1], body.m[2][2]);
    NormalizeBy(axis, (axis.y * axis.y + axis.z * axis.z) + axis.x * axis.x);
    S_vector forward(axis.x, 0.0f, axis.z);
    NormalizeBy(forward, forward.z * forward.z + forward.x * forward.x);
    const S_vector side = forward.Cross(S_vector(0.0f, -1.0f, 0.0f));
    const float halfWidth = static_cast<float>(std::tan(PLAYER_HALF_FOV) * range);
    const S_vector &pos = m_human->GetBodyFrame()->GetWorldPos();
    const float sideX = side.x * halfWidth;
    const float sideZ = side.z * halfWidth;
    const float aheadX = forward.x * range + pos.x;
    const float aheadZ = forward.z * range + pos.z;
    const float rightX = aheadX + sideX;
    const float rightZ = aheadZ + sideZ;
    const float leftX = aheadX - sideX;
    const float leftZ = aheadZ - sideZ;
    const S_vector &targetPos = target->GetFrame()->GetWorldPos();
    const float tx = targetPos.x;
    const float tz = targetPos.z;
    if ((rightX - pos.x) * (tz - pos.z) - (tx - pos.x) * (rightZ - pos.z) >= 0.0f &&
        (tz - rightZ) * (leftX - rightX) - (tx - rightX) * (leftZ - rightZ) >= 0.0f &&
        (pos.x - leftX) * (tz - leftZ) - (pos.z - leftZ) * (tx - leftX) >= 0.0f) {
        *visibility = 100.0f;
        return;
    }
    *visibility = 0.0f;
}

void C_PlayerController::OnSoundHeard(S_RememberedSound *sound)
{
    bool shown = false;
    for (int chapter : SOUND_HEARD_CHAPTERS)
        shown = shown || sound->chapter == chapter;
    if (!shown)
        return;
    const S_vector &pos = m_human->GetFrame()->GetWorldPos();
    S_vector dir(sound->pos.x - pos.x, 0.0f, sound->pos.z - pos.z);
    NormalizeBy(dir, dir.x * dir.x + dir.z * dir.z);
#ifndef HD2_DEDICATED   // the server has only the server's side (no HUD)
    if (IsClient())
        GetGame()->GetInterface()->ShowSoundDirection(dir, sound->chapter);
#endif
}

// ------------------------------------------------------------------------- hits

void C_PlayerController::OnHit(int type, int zone, int health, C_Actor *attacker, const S_vector &pos,
                               const S_vector &dir)
{
    // The clients see the hit (also in a local game, where nobody sends it).
    S_MsgHit *message = new S_MsgHit;
    message->hitType = type;
    message->zone = zone;
    message->health = health;
    message->attacker = attacker ? attacker->GetNetId() : -1;
    const S_vector4 &human = m_human->GetPosition4();
    message->pos = {pos.x - human.x, pos.y - human.y, pos.z - human.z, 0.0f};   // (w: a stale local in the original)
    message->velocity = ToVector4(dir, 0.0f);           // (the original copies a fourth float past the vector)
    SendToClients(message);
    // Inside a vehicle or at its weapon, nothing falls.
    bool seated = false;
    if (C_Actor *vehicle = m_human->m_vehicle)
        seated = (vehicle->GetSeatFlags(m_human->m_seat) & SEAT_FLAG_HIDDEN) || vehicle->HasSeatWeapon(m_human->m_seat);
    // What falls goes along the hit, and up.
    S_vector away = dir;
    NormalizeBy(away, (away.y * away.y + away.z * away.z) + away.x * away.x);
    away.x = away.x + away.x;
    away.z = away.z + away.z;
    away.y = away.y + away.y + 1.0f;
    if (zone == HIT_ZONE_HEAD_ITEM) {
        // The helmet falls off. (Fix: the original calls through a missing head container.)
        C_ItemContainer *head = m_human->GetContainer(ITEM_CONTAINER_HEAD);
        if (C_Item *helmet = head ? head->GetItem() : nullptr)
            m_human->DropItem(helmet->GetUid(), away);
    } else if (!seated && zone == HIT_ZONE_WEAPON && std::rand() % 20 == 0) {
        // Sometimes the weapon falls out of the hands.
        UpdateHandsItem();
        if (m_handsItem) {
            const int carryMode = m_handsItem->GetDef()->GetCarryMode();
            if (carryMode == ITEM_CARRY_SMG || carryMode == ITEM_CARRY_RIFLE || carryMode == ITEM_CARRY_LMG) {
                m_human->DropItem(m_handsItem->GetUid(), away);
                m_handsItem = nullptr;
            }
        }
    }
    if (m_human->IsLocalPlayer()) {
        if (GetGame()->IsFirstPersonView())
            GetGame()->GetInterface()->FlashDamage();
        if (type == HIT_BULLET || type == HIT_MELEE || type == HIT_EXPLOSION)
            OnDamaged(type, zone, dir);
    }
    // Single player: an enemy's hit wakes the squad members holding.
    // (Fix: the original takes any attacker for a human.)
    if (IsLocalGame() && attacker && attacker->GetType() == ACTOR_HUMAN &&
        static_cast<C_Human *>(attacker)->IsHostileTo(m_human))
        g_teamManager.GetPlayerTeam()->ReleaseHoldingMembers(m_human);
}

void C_PlayerController::OnDamaged(int /*type*/, int /*zone*/, const S_vector & /*dir*/)
{
    // The view kicks: a little on difficulty 1 of single player, else by the endurance.
    S_vector4 view = m_viewDir;
    if (IsLocalGame() && g_gameProfile.GetDifficulty() == DIFFICULTY_EASY) {
        view.x = (RandFloat() - 0.5f) * 0.01f + view.x;
        view.y = (RandFloat() - 0.5f) * 0.02f + view.y;
        view.z = (RandFloat() - 0.5f) * 0.01f + view.z;
    } else {
        const float weakness = 1.0f - m_human->GetStatFactor(STAT_ENDURANCE);
        const float side = weakness * 0.09f + 0.01f;
        float kick = (RandFloat() - 0.5f) * side;
        view.x = kick + kick + view.x;
        kick = (weakness * 0.18f + 0.02f) * (RandFloat() - 0.5f);
        view.y = kick + kick + view.y;
        kick = (RandFloat() - 0.5f) * side;
        view.z = kick + kick + view.z;
    }
    S_vector &dir = reinterpret_cast<S_vector &>(view);
    NormalizeBy(dir, (dir.z * dir.z + dir.y * dir.y) + dir.x * dir.x);
    SetViewDir(view);
}

// ------------------------------------------------------------------------- saved games

void C_PlayerController::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_PLAYER_BASE);
    C_HumanController::SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_PLAYER_STATE);
    chunk.Write(m_unknown524);
    chunk.Write(m_ordersChanged);
    chunk.Write(m_squadFlags);
    chunk.Write(m_raisedTime);
    chunk.Write(m_throwDelay);
    chunk.Write(m_squadCombatOrder);
    chunk.Ascend();
    if (m_commandDelay > 0) {
        // The pending command as the original's object (its vtable, the fields, the padding).
        chunk.Begin(CHUNK_PLAYER_COMMAND);
        uint8_t image[ACTION_COMMAND_SAVED_SIZE] = {};
        const uint32_t vtable = ACTION_COMMAND_VTABLE;
        std::memcpy(image, &vtable, sizeof(vtable));
        std::memcpy(image + sizeof(vtable), &m_pendingCommand, sizeof(S_ActionCommand));
        chunk.Write(image, ACTION_COMMAND_SAVED_SIZE);
        chunk.Write(m_commandDelay);
        chunk.Ascend();
    }
}

void C_PlayerController::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_PLAYER_BASE:
            C_HumanController::LoadGame(chunk);
            break;
        case CHUNK_PLAYER_STATE:
            if (chunk.RemainingSize() >= 1)
                chunk.Read(m_unknown524);
            if (chunk.RemainingSize() >= 1)
                chunk.Read(m_ordersChanged);
            if (chunk.RemainingSize() >= 4)
                chunk.Read(m_squadFlags);
            if (chunk.RemainingSize() >= 4)
                chunk.Read(m_raisedTime);
            chunk.Read(m_throwDelay);
            chunk.Read(m_squadCombatOrder);
            break;
        case CHUNK_PLAYER_COMMAND: {
            uint8_t image[ACTION_COMMAND_SAVED_SIZE];
            chunk.Read(image, ACTION_COMMAND_SAVED_SIZE);
            std::memcpy(&m_pendingCommand, image + sizeof(uint32_t), sizeof(S_ActionCommand));
            chunk.Read(m_commandDelay);
            break;
        }
        default:
            break;
        }
        chunk.Ascend();
    }
    g_teamManager.GetPlayerTeam()->SelectMember(m_human);
}

// ------------------------------------------------------------------------- the view modes

void C_PlayerController::SetViewMode(int mode)
{
    const int old = m_viewMode;
    if (old == mode)
        return;
    m_viewMode = mode;
    // (Network games: the HUD of this machine's player only.)
    if (!IsLocalGame() && !m_human->IsLocalPlayer())
        return;
    C_game *game = GetGame();
    switch (mode) {
    case VIEW_NORMAL:
        if (old == VIEW_BINOCULARS) {
            // The binoculars put away (the others see it too).
            game->ShowBinocularsView(false);
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, WORK_ANIMATION_BINOCULARS_AWAY));
            if (!IsLocalGame()) {
                C_NetBinocularsMessage *message = new C_NetBinocularsMessage;
                message->on = false;
                message->fromClient = true;
                m_netObject->QueueMessage(message, g_netReplication->GetLocalId(), true);
                m_scopeDelay = 0;
                return;
            }
        } else {
            // Out of the sights or the scope: the HUD as the player keeps it.
            bool show = true;
            if (IsLocalGame() || g_mpSettings->thirdPersonView != 0)
                show = g_hudOption->shown;
            game->ShowFirstPersonView(show);
            const float fov = game->GetCutsceneFOV();
            game->GetFirstPersonView()->SetAimView(false, fov);
        }
        m_scopeDelay = 0;
        return;
    case VIEW_BINOCULARS: {
        C_Item *binoculars = m_human->FindBeltItem(ITEM_BEHAVIOUR_BINOCULARS);
        if (!binoculars) {
            m_viewMode = old;
            return;
        }
        game->ShowBinocularsView(true);
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, WORK_ANIMATION_BINOCULARS_OUT,
                                                    FrameArg(binoculars->GetFrame(0))));
        if (!IsLocalGame()) {
            C_NetBinocularsMessage *message = new C_NetBinocularsMessage;
            message->on = true;
            message->fromClient = true;
            m_netObject->QueueMessage(message, g_netReplication->GetLocalId(), true);
        }
        return;
    }
    case VIEW_IRON_SIGHTS:
    case VIEW_SCOPE:
        // Dropped: a copy protection check: in single player, a failed check kept the view as
        // it was (no sights, no scope).
        game->ShowFirstPersonView(true);
        game->GetFirstPersonView()->SetAimView(true, ScopeFovOf(m_handsItem));
        return;
    default:
        return;
    }
}

// ------------------------------------------------------------------------- the medkit, the aim

bool C_PlayerController::UseMedkit(int humanId, int itemUid)
{
    // Dropped: a copy protection check: in single player, a failed check now and then
    // (rand() < 327.67) gave the healed human a deadly hit (type 6) from the player.
    C_Human *patient = static_cast<C_Human *>(g_actorsModule.GetHuman(humanId));
    if (!patient || patient->m_stats[STAT_HEALTH] == patient->m_initialStats[STAT_HEALTH])
        return false;
    C_Item *medkit;
    if (itemUid == -1) {
        medkit = m_human->FindContainerOfBehaviour(ITEM_BEHAVIOUR_MEDKIT, nullptr);
        if (!medkit)
            return false;
        itemUid = medkit->GetUid();
    } else {
        medkit = m_human->FindItemByUid(itemUid);
    }
    if (!medkit || BehaviourOf(medkit) != ITEM_BEHAVIOUR_MEDKIT)
        return false;
    // The first aid skill's share of full health, as far as the charges and the wounds go;
    // the less skilled use up more of the medkit for it.
    const int firstAid = m_human->m_stats[STAT_FIRST_AID];
    int health = static_cast<int>(static_cast<float>(firstAid * patient->m_initialStats[STAT_HEALTH]) * 0.01f);
    if (health == 0)
        return false;
    if (static_cast<int>(medkit->GetCharges()) < health)
        health = static_cast<int>(medkit->GetCharges());
    if (patient->m_stats[STAT_HEALTH] + health > patient->m_initialStats[STAT_HEALTH])
        health = patient->m_initialStats[STAT_HEALTH] - patient->m_stats[STAT_HEALTH];
    medkit->Consume(((100.0f - static_cast<float>(firstAid)) * 0.01f + 1.0f) * static_cast<float>(health));
    patient->m_stats[STAT_HEALTH] += health;
    const bool used = static_cast<int>(medkit->GetCharges()) == 0;
    if (IsLocalGame()) {
        m_human->AddFirstAid(0.1f);
        Heal(humanId, itemUid, health, false);
    } else {
        S_MsgHeal *message = new S_MsgHeal;
        message->human = humanId;
        message->itemUid = medkit->GetUid();
        message->health = health;
        message->removeItem = used;
        SendToClients(message);
    }
    if (used)
        m_human->RemoveItem(itemUid, true);
    // A medic scores for healing another player.
    if (!IsLocalGame() && humanId != m_human->GetNetId())
        g_netGame.AddScore(m_human->GetNetId(), static_cast<int>(static_cast<float>(health) * MEDIC_SCORE_PER_HEALTH));
    return true;
}

float C_PlayerController::GetAimBonus()
{
    // Standing aims worst, lying best; moving faster aims worse.
    float bonus;
    if (m_moveFlags & MOVE_STANDING)
        bonus = 0.25f;
    else if (m_moveFlags & MOVE_CROUCHING)
        bonus = 0.1f;
    else
        bonus = 0.05f;
    if (m_moveFlags & MOVE_MOVING_MASK) {
        if (m_moveFlags & MOVE_PACE_1)
            bonus += 0.1f;
        else if (m_moveFlags & MOVE_PACE_3)
            bonus += 0.5f;
        else if (m_moveFlags & MOVE_PACE_4)
            bonus += 0.75f;
    }
    return (100.0f - static_cast<float>(m_human->m_stats[STAT_SHOOTING])) * 0.01f + bonus;
}
