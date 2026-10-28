// C_HumanController, the base of every controller (original source 0x00582ff0..0x00589290 and
// 0x0058b6c0..0x0058c510; the AI task pools between them are part of the AI controllers).
//
// The controllers decide what a human does; the human's body (S_HumanBody) carries it out
// through body commands. This base class holds what they all share: the directions and the
// move flags the body reports back, the interactions with other actors that a command starts
// and EndInteraction completes after a while (doors, mines, explosives, healing, tying up,
// backstabs, wire cutting ...), the inventory requests, speech, and the controls (the axes and
// buttons of the player's keys or of a remote human decoded from the network) that the player
// and remote controllers turn into body commands.
#include "game/first_person_view.h"
#include "game/human_controller.h"

#include <algorithm>
#include <cstdint>

#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/checkpoints.h"
#include "game/collision.h"
#include "game/controller_internal.h"
#include "game/controller_messages.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_body.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/interface.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/sound_module.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// Saved game chunks.
enum : uint16_t {
    CHUNK_CONTROLLER_STATE = 0x457,
    CHUNK_CONTROLLER_INTERACTION = 0x458,               // only while one is going on
    CHUNK_CONTROLLER_SPEECH = 0x459,
};

// Progress texts of the interactions (C_Human's action bar).
enum : int {
    TEXT_ACTION_LAY_MINE = 0x1B9E,
    TEXT_ACTION_PLANT_EXPLOSIVE = 0x1BA0,
    TEXT_ACTION_PICK_LOCK = 0x1BA1,
    TEXT_ACTION_HEAL = 0x1BA2,
    TEXT_ACTION_DEFUSE = 0x1BA3,
    TEXT_ACTION_TIE_UP = 0x1BA4,
    TEXT_ACTION_BACKSTAB = 0x1BA5,
    TEXT_ACTION_CUT_WIRE = 0x1BA6,
};
// Healing messages (C_Human::PrintText; the wording is inferred from the arguments).
enum : int {
    TEXT_YOU_HEALED = 0x2274,                           // %s: the patient
    TEXT_HEALED_YOU = 0x2275,                           // %s: the medic
    TEXT_HEALED = 0x2276,                               // %s %s: the medic, the patient
    TEXT_YOU_HEALED_YOURSELF = 0x2277,
    TEXT_HEALED_HIMSELF = 0x2278,                       // %s: the medic
};

// The photo camera (item type) the photo places need.
constexpr int ITEM_PHOTO_CAMERA = 0xE5;
// A fuse of a mine laid on the ground: the ray down from half a metre in front of the body.
constexpr float MINE_PLACE_DISTANCE = 0.5f;
constexpr float MINE_PLACE_RAY = 2.0f;
// (DISGUISE_LOSS_*: game/controller_internal.h.)
// The stamina a dive takes, and the stamina needed for it.
constexpr float DIVE_STAMINA = 10.0f;
// cos(10 degrees): IsFacing().
constexpr float FACING_COS = 0.98481f;
// Players taken into account by Say() on a server (the actors module's player slots).
constexpr int MAX_PLAYER_SLOTS = 32;

}  // namespace

// ------------------------------------------------------------------------- life cycle

C_HumanController::C_HumanController(E_HumanController type)
    : m_viewDir{0.0f, 0.0f, 1.0f, 0.0f}, m_facingDir{0.0f, 0.0f, 1.0f, 0.0f}, m_lookAt{0.0f, 0.0f, 1.0f, 0.0f},
      m_type(type)
{
}

C_HumanController::~C_HumanController()
{
    // Fix: the original frees only the vectors, not the messages left in them.
    ClearMessagesToServer();
    ClearMessagesToClients();
}

void C_HumanController::Attach(C_Human *human, C_HumanController *previous)
{
    m_human = human;
    m_events = human->m_events;
    m_handsItem = HandsItemOf(human);
    human->GetFrame()->Update();
    m_walkSpeed = 0.0f;
    m_speechDelay = 0;
    m_checkpoints = GetGame()->GetCheckpoints();
    if (m_checkpoints) {
        const S_vector &pos = human->GetFrame()->GetWorldPos();
        m_currentCheckpoint = m_checkpoints->Get(m_checkpoints->FindNearest(pos, 5.0f, -1.0f));
    }
    m_stateFlags = MOVE_PACE_4;
    if (!previous) {
        m_moveFlags = MOVE_STANDING | MOVE_AI_AGGRESSIVE | MOVE_PACE_3;
        m_interaction = -1;
        m_interactionArg = -1;
        m_weaponRaised = false;
        m_unknownAC = false;
        m_quietTime = 10000000;
        const S_matrix &m = human->GetFrame()->GetMatrix();
        const S_vector dir = m(2).Normalized();
        m_viewDir = ToVector4(dir, m.m[2][3]);
        m_facingDir = m_viewDir;
    } else {
        m_stateFlags = previous->m_stateFlags;
        m_moveFlags = previous->m_moveFlags & MOVE_KEPT_ON_ATTACH;
        m_lookAt = previous->m_lookAt;
        m_viewDir = previous->m_viewDir;
        m_facingDir = previous->m_facingDir;
        m_unknownAC = previous->m_unknownAC;
        m_weaponRaised = previous->m_weaponRaised;
        m_quietTime = previous->m_quietTime;
        m_interaction = previous->m_interaction;
        m_interactionTime = previous->m_interactionTime;
        m_interactionTarget = previous->m_interactionTarget;
        m_interactionArg = previous->m_interactionArg;
    }
    m_controlBits = 0;
    m_axes = nullptr;
    m_buttons = nullptr;
    m_stance = m_moveFlags & MOVE_STANCE_MASK;
    m_alarmRaised = false;
    if (IsLocalGame()) {
        m_netObject = nullptr;
        // A living enemy taking over raises the alarm while it is attached.
        if ((m_type == HUMAN_CONTROLLER_ENEMY || m_type == HUMAN_CONTROLLER_SNIPER) && human->GetStat(STAT_HEALTH) > 0) {
            GetGame()->SetAlarm(true);
            m_alarmRaised = true;
        }
    } else {
        m_netObject = g_netReplication->FindObject(human->GetNetId(), true);
    }
}

void C_HumanController::Detach()
{
    if (!IsLocalGame() && m_netObject) {
        g_netReplication->RemoveObject(m_netObject);
        m_netObject = nullptr;
    }
    if (m_alarmRaised) {
        GetGame()->SetAlarm(false);
        m_alarmRaised = false;
    }
    m_human = nullptr;
}

void C_HumanController::OnDeath()
{
    if (!InVehicleSeat(m_human->m_vehicle)) {
        if (IsLocalGame()) {
            // The weapon falls out of the hands (grenades stay with the body).
            C_Item *item = HandsItemOf(m_human);
            if (item && BehaviourOf(item) != ITEM_BEHAVIOUR_THROWN) {
                S_vector dir(m_viewDir.x, m_viewDir.y + 1.0f, m_viewDir.z);
                dir.Normalize();
                const float speed = RandFloat() + 2.0f;
                m_human->DropItem(item->GetUid(), dir * speed);
            }
        } else {
            m_human->DropAllItems();
        }
    }
    m_moveFlags |= MOVE_DEAD;
    if (m_alarmRaised) {
        GetGame()->SetAlarm(false);
        m_alarmRaised = false;
    }
    if (m_interactionTime >= 0) {
        EndInteraction(false);
        ResetInteraction();
    }
}

void C_HumanController::OnKilled()
{
    m_moveFlags |= MOVE_DEAD;
    if (m_interactionTime >= 0) {
        EndInteraction(false);
        ResetInteraction();
    }
}

void C_HumanController::OnKnockedOut()
{
    m_moveFlags |= MOVE_UNCONSCIOUS;
}

void C_HumanController::SetUnconscious()
{
    m_moveFlags |= MOVE_UNCONSCIOUS;
}

void C_HumanController::ResetInteraction()
{
    m_interactionTarget = nullptr;
    m_interaction = -1;
    m_interactionTime = -1;
    m_interactionArg = -1;
}

// ------------------------------------------------------------------------- small slots

bool C_HumanController::AdjustAim(S_vector * /*dir*/)
{
    return true;
}

void C_HumanController::RequestAction(int /*action*/, const S_vector4 * /*pos*/, const S_vector4 * /*dir*/,
                                      float /*strength*/, int /*ammoUid*/, int /*rounds*/)
{
}

const S_vector &C_HumanController::GetFacingDir()
{
    return XYZ(m_facingDir);
}

const S_vector &C_HumanController::GetViewDir()
{
    return XYZ(m_viewDir);
}

void C_HumanController::SetFacingDir(const S_vector4 &dir)
{
    m_facingDir = dir;
}

void C_HumanController::SetViewDir(const S_vector4 &dir)
{
    m_viewDir = dir;
}

bool C_HumanController::AdjustToPathHeight(S_vector * /*pos*/)
{
    return false;
}

bool C_HumanController::Unknown5C(float * /*intensity*/, float * /*reference*/)
{
    return false;
}

void C_HumanController::SetSpectating(bool /*on*/)
{
}

bool C_HumanController::IsSpectating()
{
    return false;
}

bool C_HumanController::OnCollisionEx(C_Actor * /*other*/, const S_vector * /*pos*/, int, int, int, int, int)
{
    return true;
}

void C_HumanController::SetAIMode(int mode)
{
    m_moveFlags ^= (m_moveFlags ^ static_cast<uint32_t>(mode)) & MOVE_AI_MODE_MASK;
}

void C_HumanController::SetMoveFlags(uint32_t flags)
{
    const uint32_t medium = flags & MOVE_MEDIUM_MASK;
    if (medium) {
        // A full group clears it.
        m_moveFlags &= ~MOVE_MEDIUM_MASK;
        if (medium != MOVE_MEDIUM_MASK)
            m_moveFlags |= medium;
        flags &= ~MOVE_MEDIUM_MASK;
        if (!flags)
            return;
    } else if (!flags) {
        m_moveFlags &= ~(MOVE_MOVING_MASK | MOVE_LEAN_MASK);
        return;
    }
    if (flags & MOVE_LEAN_MASK)
        m_moveFlags = (m_moveFlags & ~MOVE_LEAN_MASK) | (flags & MOVE_LEAN_MASK);
    if (flags & MOVE_MOVING_MASK)
        m_moveFlags = (m_moveFlags & ~MOVE_MOVING_MASK) | (flags & MOVE_MOVING_MASK);
    if (flags & MOVE_PACE_MASK)
        m_moveFlags = (m_moveFlags & ~MOVE_PACE_MASK) | (flags & MOVE_PACE_MASK);
    if (flags & MOVE_STANCE_MASK)
        m_moveFlags = (m_moveFlags & ~MOVE_STANCE_MASK) | (flags & MOVE_STANCE_MASK);
}

void C_HumanController::IssueCommand(const S_HumanCommand &command)
{
    ExecuteCommand(command);
}

int C_HumanController::UnknownE8()
{
    return -1;
}

float C_HumanController::GetAimBonus()
{
    return 0.0f;
}

bool C_HumanController::IsSurrendered()
{
    return false;
}

bool C_HumanController::Unknown104()
{
    return !(m_moveFlags & MOVE_LYING);
}

void C_HumanController::RequestLeaveVehicle()
{
    S_HumanCommand command;
    command.category = HUMAN_COMMAND_VEHICLE;
    command.type = HUMAN_COMMAND_LEAVE_VEHICLE;
    IssueCommand(command);
}

void C_HumanController::UpdateHandsItem()
{
    m_handsItem = HandsItemOf(m_human);
}

void C_HumanController::NotifyHud(int event)
{
    if (IsClient() && m_human == g_teamManager.GetPlayerTeam()->GetActivePlayer())
        GetGame()->GetFirstPersonView()->OnInventoryEvent(event, 0);
}

bool C_HumanController::IsFacing(const C_Actor *actor)
{
    S_vector toActor = actor->GetFrame()->GetWorldPos() - m_human->GetFrame()->GetWorldPos();
    S_vector view = GetViewDir();
    toActor.Normalize();
    view.Normalize();
    return view.Dot(toActor) > FACING_COS;
}

// ------------------------------------------------------------------------- inventory

void C_HumanController::OnUniformChanged(int uid)
{
    m_human->PutOnUniform(uid);
}

void C_HumanController::RequestPickUpItem(int actorId, int container)
{
    m_human->PickUpItem(actorId, &container);
}

void C_HumanController::RequestThrowItem(int uid, const S_vector &velocity)
{
    m_human->DropItem(uid, velocity);
}

bool C_HumanController::RequestMoveItem(int uid, int otherId, int fromContainer, int toContainer, int slot)
{
    const bool moved = m_human->ServerMoveItem(uid, otherId, fromContainer, &toContainer, slot);
    if (moved && !IsLocalGame()) {
        S_MsgMoveItem *message = new S_MsgMoveItem;
        message->uid = uid;
        message->other = otherId;
        message->fromContainer = fromContainer;
        message->toContainer = toContainer;
        message->slot = slot;
        SendToClients(message);
    }
    return moved;
}

bool C_HumanController::RequestTransferItem(int uid, int arg, int otherId, int otherUid, int container)
{
    const bool moved = m_human->ServerTransferItem(uid, arg, otherId, otherUid, container);
    if (moved && !IsLocalGame()) {
        // (The original stores CTRL_MSG_MOVE_ITEM as the message's type; its Write sends
        // CTRL_MSG_TRANSFER_ITEM regardless.)
        S_MsgTransferItem *message = new S_MsgTransferItem;
        message->uid = uid;
        message->arg = arg;
        message->other = otherId;
        message->otherUid = otherUid;
        message->container = container;
        SendToClients(message);
    }
    return moved;
}

void C_HumanController::RequestTakeFromBox(int boxId, int index)
{
    int container = 0;
    int uid = 0;
    m_human->TakeFromSupplyBox(boxId, index, &container, &uid);
}

void C_HumanController::OnItemDropped(int actorId, int uid, const S_vector *dir, float speed)
{
    S_MsgDropItem *message = new S_MsgDropItem;
    message->uid = uid;
    message->dir = ToVector4(*dir, 0.0f);               // (the original copies a fourth float past the vector)
    message->speed = speed;
    message->actor = actorId;
    SendToClients(message);
}

// ------------------------------------------------------------------------- messages

void C_HumanController::ClearMessagesToServer()
{
    for (std::size_t i = m_toServer.size(); i-- > 0;)
        delete m_toServer[i];
    m_toServer.clear();
}

void C_HumanController::ClearMessagesToClients()
{
    for (std::size_t i = m_toClients.size(); i-- > 0;)
        delete m_toClients[i];
    m_toClients.clear();
}

void C_HumanController::SendRemoteRequest(int request)
{
    S_MsgRemoteRequest *message = new S_MsgRemoteRequest;
    message->request = request;
    if (IsServer())
        SendToClients(message);
    else
        SendToServer(message);
}

void C_HumanController::SendMessage23(bool on)
{
    S_Msg23 *message = new S_Msg23;
    message->on = on;
    SendToClients(message);
}

// ------------------------------------------------------------------------- controls

void C_HumanController::ControlVehicle()
{
    C_Actor *vehicle = m_human->m_vehicle;
    if (!m_human->IsLocalPlayer() && m_human->m_seat != 0)
        return;
    if (vehicle->GetType() == ACTOR_CAR) {
        static_cast<C_Car *>(vehicle)->ApplyControls(m_axes, m_buttons);
    } else if (vehicle->GetType() == ACTOR_SUBMARINE) {
        const float *axes = m_axes;
        static_cast<C_Submarine *>(vehicle)->SetControls(axes[AXIS_FORWARD] - axes[AXIS_BACK],
                                                         axes[AXIS_LEFT] - axes[AXIS_RIGHT], axes[8] - axes[9]);
    }
}

void C_HumanController::ApplyMoveControls()
{
    if (InVehicleSeat(m_human->m_vehicle))
        return;
    // Divers in the water cannot march or run.
    if (m_human->IsWearingDivingGear() && !m_human->m_body.IsWorkState5() &&
        (m_stateFlags & (MOVE_PACE_MARCH | MOVE_PACE_4)))
        m_stateFlags = MOVE_PACE_3;

    const float *axes = m_axes;
    const float *buttons = m_buttons;
    const uint32_t previous = m_moveFlags;
    // (The original leaves a stack value in w.)
    m_lookAt = {0.0f, 0.0f, 0.0f, 0.0f};

    uint32_t wanted = 0;
    if (axes[AXIS_FORWARD] != 0.0f)
        wanted = MOVE_FORWARD;
    else if (axes[AXIS_BACK] != 0.0f)
        wanted = MOVE_BACK;
    if (axes[AXIS_LEFT] != 0.0f)
        wanted |= MOVE_LEFT;
    else if (axes[AXIS_RIGHT] != 0.0f)
        wanted |= MOVE_RIGHT;
    // Leaning only standing still.
    if (axes[AXIS_LEAN_LEFT] != 0.0f && wanted == 0)
        wanted = MOVE_LEAN_1;
    else if (axes[AXIS_LEAN_RIGHT] != 0.0f && wanted == 0)
        wanted = MOVE_LEAN_2;
    if (buttons[BUTTON_CROUCH] != 0.0f)
        wanted |= MOVE_CROUCHING;
    else if (buttons[BUTTON_LIE] != 0.0f)
        wanted |= MOVE_LYING;

    // The walking animation starts when the direction changes; the walk goes along the facing
    // direction (forward / back) and across it (the sides).
    const uint32_t changed = previous ^ wanted;
    if (wanted & MOVE_LEAN_1) {
        if (!(previous & MOVE_LEAN_1))
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, ANIM_LEAN_LEFT));
    } else if (wanted & MOVE_LEAN_2) {
        if (!(previous & MOVE_LEAN_2))
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, ANIM_LEAN_RIGHT));
    } else if (wanted & MOVE_FORWARD) {
        if (!(previous & MOVE_FORWARD) || (changed & (MOVE_LEFT | MOVE_RIGHT)))
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, ANIM_WALK_FORWARD));
        m_lookAt.x = m_facingDir.x;
        m_lookAt.z = m_facingDir.z;
    } else if (wanted & MOVE_BACK) {
        if (!(previous & MOVE_BACK) || (changed & (MOVE_LEFT | MOVE_RIGHT)))
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, ANIM_WALK_BACK));
        m_lookAt.x -= m_facingDir.x;
        m_lookAt.z -= m_facingDir.z;
    }
    if (wanted & MOVE_LEFT) {
        // (The body command may have changed the flags: they are read again.)
        const uint32_t sideChanged = m_moveFlags ^ wanted;
        if (!(m_moveFlags & MOVE_LEFT) || (sideChanged & (MOVE_FORWARD | MOVE_BACK))) {
            const int anim = (wanted & MOVE_FORWARD) ? ANIM_WALK_FORWARD_LEFT
                             : (wanted & MOVE_BACK)  ? ANIM_WALK_BACK_LEFT
                                                     : ANIM_WALK_LEFT;
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, anim));
        }
        m_lookAt.x -= m_facingDir.z;
        m_lookAt.z += m_facingDir.x;
    } else if (wanted & MOVE_RIGHT) {
        const uint32_t sideChanged = m_moveFlags ^ wanted;
        if (!(m_moveFlags & MOVE_RIGHT) || (sideChanged & (MOVE_FORWARD | MOVE_BACK))) {
            const int anim = (wanted & MOVE_FORWARD) ? ANIM_WALK_FORWARD_RIGHT
                             : (wanted & MOVE_BACK)  ? ANIM_WALK_BACK_RIGHT
                                                     : ANIM_WALK_RIGHT;
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, anim));
        }
        m_lookAt.x += m_facingDir.z;
        m_lookAt.z -= m_facingDir.x;
    }
    S_vector walk(m_lookAt.x, m_lookAt.y, m_lookAt.z);
    if (walk.Square() >= 1e-16f) {
        walk.Normalize();
        walk *= m_walkSpeed != 0.0f ? m_walkSpeed : 10.0f;
        m_lookAt.x = walk.x;
        m_lookAt.y = walk.y;
        m_lookAt.z = walk.z;
    }

    // The pace asked for.
    if (m_stateFlags != (m_moveFlags & MOVE_PACE_MASK)) {
        int gait = -1;
        if (m_stateFlags & MOVE_PACE_MARCH)
            gait = GAIT_MARCH;
        else if (m_stateFlags & MOVE_PACE_4)
            gait = GAIT_PACE_4;
        else if (m_stateFlags & MOVE_PACE_3)
            gait = GAIT_PACE_3;
        else if (m_stateFlags & MOVE_PACE_2)
            gait = GAIT_PACE_2;
        else if (m_stateFlags & MOVE_PACE_1)
            gait = GAIT_PACE_1;
        if (gait >= 0)
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_STANCE, gait));
    }

    // Crouch and lie toggle; marching ends with them.
    if (wanted & (MOVE_CROUCHING | MOVE_LYING)) {
        const uint32_t stance = (wanted & MOVE_CROUCHING) ? MOVE_CROUCHING : MOVE_LYING;
        int command = STANCE_STAND;
        if (!(m_moveFlags & stance)) {
            if (m_stateFlags == MOVE_PACE_MARCH)
                m_stateFlags = MOVE_PACE_4;
            command = stance == MOVE_CROUCHING ? STANCE_CROUCH : STANCE_LIE;
        }
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_3, command));
    }

    if (buttons[BUTTON_CLIMB] != 0.0f) {
        S_HumanCommand command;
        command.category = HUMAN_COMMAND_VEHICLE;
        command.type = HUMAN_COMMAND_CLIMB;
        IssueCommand(command);
        return;
    }
    if (buttons[BUTTON_DIVE] != 0.0f && (wanted & MOVE_FORWARD) && m_human->m_stamina > DIVE_STAMINA) {
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, ANIM_DIVE));
        m_moveFlags = (m_moveFlags & ~MOVE_DIRECTION_BITS) | MOVE_DIVE;
        m_human->m_stamina -= DIVE_STAMINA;
        if (m_human->m_stamina < 0.0f)
            m_human->m_stamina = 0.0f;
    } else if ((previous & MOVE_WALKING_MASK) && !(wanted & MOVE_WALKING_MASK)) {
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, ANIM_STOP));
    }
}

uint32_t EncodeControlBits(const float *axes, uint32_t moveFlags)
{
    // Bits 0..1 the stance (0 standing, 1 lying, 2 crouching), 2..4 the pace (none: 0 or
    // MOVE_PACE_1; 4 PACE_3, 0xC PACE_4, 8 PACE_2, 0x10 marching), 5..6 the leaning,
    // 7..8 the side walked to (1 left, 2 right).
    uint32_t bits = 0;
    if (!(moveFlags & MOVE_STANDING)) {
        if (moveFlags & MOVE_LYING)
            bits = 1;
        else if (moveFlags & MOVE_CROUCHING)
            bits = 2;
    }
    if (!(moveFlags & MOVE_PACE_1)) {
        if (moveFlags & MOVE_PACE_3)
            bits |= 4;
        else if (moveFlags & MOVE_PACE_4)
            bits |= 0xC;
        else if (moveFlags & MOVE_PACE_2)
            bits |= 8;
        else if (moveFlags & MOVE_PACE_MARCH)
            bits |= 0x10;
    }
    if (axes[AXIS_LEAN_LEFT] != 0.0f)
        bits |= 0x20;
    if (axes[AXIS_LEAN_RIGHT] != 0.0f)
        bits |= 0x40;
    if (moveFlags & MOVE_LEFT)
        bits |= 0x80;
    if (moveFlags & MOVE_RIGHT)
        bits |= 0x100;
    return bits;
}

void C_HumanController::DecodeControls(float *axes, float *buttons, int /*unused*/, const float *velocity,
                                       const float *viewDir, int /*unused2*/, uint32_t bits)
{
    if (velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2] < 1e-16f) {
        axes[AXIS_FORWARD] = 0.0f;
        axes[AXIS_BACK] = 0.0f;
        axes[AXIS_LEFT] = 0.0f;
        axes[AXIS_RIGHT] = 0.0f;
    } else if (m_human->m_body.GetClimbState() == 0) {
        // The walk's angle to the view decides forward / back; the sides come with the bits.
        const float view[2] = {viewDir[0], viewDir[2]};
        const float walk[2] = {velocity[0], velocity[2]};
        const float angle = AngleBetween2D(walk, view);
        const uint32_t side = bits >> 7 & 3;
        if (side == 1)
            axes[AXIS_LEFT] = 1.0f;
        if (side == 2)
            axes[AXIS_RIGHT] = 1.0f;
        if (angle > 2.1991148f || angle < -2.1991148f)  // beyond 126 degrees
            axes[AXIS_BACK] = 1.0f;
        if (angle < 0.9424779f && angle > -0.9424779f)  // within 54 degrees
            axes[AXIS_FORWARD] = 1.0f;
    } else if (velocity[1] <= 0.0f) {
        axes[AXIS_BACK] = 1.0f;                         // climbing down
    } else {
        axes[AXIS_FORWARD] = 1.0f;
    }

    // The stance buttons toggle: pressed when the remote human's stance differs.
    switch (bits & 3) {
    case 1:
        buttons[BUTTON_LIE] = static_cast<float>(~m_moveFlags & MOVE_LYING);
        break;
    case 2:
        buttons[BUTTON_CROUCH] = static_cast<float>(~m_moveFlags >> 1 & 1);
        break;
    default:
        buttons[BUTTON_LIE] = static_cast<float>(m_moveFlags & MOVE_LYING);
        buttons[BUTTON_CROUCH] = static_cast<float>(m_moveFlags & MOVE_CROUCHING);   // (2, not 1)
        break;
    }
    switch (bits >> 5 & 3) {
    case 1:
        axes[AXIS_LEAN_LEFT] = 1.0f;
        break;
    case 2:
        axes[AXIS_LEAN_RIGHT] = 1.0f;
        break;
    default:
        break;
    }
    switch (bits >> 2 & 7) {
    case 0:
        m_stateFlags = MOVE_PACE_1;
        break;
    case 2:
        m_stateFlags = MOVE_PACE_2;
        break;
    case 3:
        m_stateFlags = MOVE_PACE_4;
        break;
    case 4:
        m_stateFlags = MOVE_PACE_MARCH;
        break;
    default:
        m_stateFlags = MOVE_PACE_3;
        break;
    }
}

void C_HumanController::OnBodyFlags(uint32_t flags)
{
    m_weaponRaised = (flags & BODY_FLAG_WEAPON_RAISED) != 0;
    m_unknownAC = (flags & BODY_FLAG_UNKNOWN_100) != 0;
    if (flags & BODY_FLAG_UNKNOWN_10000)
        m_moveFlags |= MOVE_UNKNOWN_8000000;
    else
        m_moveFlags &= ~MOVE_UNKNOWN_8000000;
    UpdateHandsItem();
    if (!m_handsItem || GetGame()->GetMissionTime() < m_handsItem->GetTime())
        m_unknownAC = false;
    if (!IsLocalGame() && !m_human->IsLocalPlayer())
        return;

    // A throw left the hand: ask for it from where the human aims.
    S_vector4 origin;
    S_vector4 dir;
    if (flags & BODY_FLAG_THROWN) {
        if (C_Item *item = m_handsItem) {
            const float strength = static_cast<C_WeaponItem *>(item)->GetThrowStrength();
            UseItem(item, true, 0, &origin, &dir);
            RequestAction(CONTROLLER_ACTION_THROW, &origin, &dir, strength, -1, -1);
            m_handsItem = nullptr;
        }
    } else if (flags & BODY_FLAG_THROWN_2) {
        if (C_Item *item = m_handsItem) {
            const float strength = static_cast<C_WeaponItem *>(item)->GetThrowStrength();
            UseItem(item, true, 0, &origin, &dir);
            RequestAction(CONTROLLER_ACTION_THROW_2, &origin, &dir, strength, -1, -1);
            m_handsItem = nullptr;
        }
    } else if (flags & BODY_FLAG_BELT_GRENADE_THROWN) {
        if (C_Item *item = m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN)) {
            const float strength = static_cast<C_WeaponItem *>(item)->GetThrowStrength();
            UseItem(item, true, 0, &origin, &dir);
            RequestAction(CONTROLLER_ACTION_THROW_BELT_GRENADE, &origin, &dir, strength, -1, -1);
        }
    } else if (flags & BODY_FLAG_UNKNOWN_20000) {
        UseItem(m_handsItem, true, 0, &origin, &dir);
        RequestAction(CONTROLLER_ACTION_8, &origin, &dir, 0.0f, -1, -1);
    }
}

// ------------------------------------------------------------------------- interactions

namespace {

// Starts an interaction's progress bar on `human`.
void ShowAction(C_Human *human, int time, int text)
{
    human->SetActionProgress(time, text);
}

S_BodyCommand Interact(uint32_t interaction, C_Actor *target, int arg = 0)
{
    return MakeBodyCommand(BODY_COMMAND_INTERACT, static_cast<int>(interaction), arg, target);
}

}  // namespace

void C_HumanController::EndInteraction(bool completed)
{
    UpdateHandsItem();
    m_human->SetActionProgress(0, -1);
    C_Actor *target = m_interactionTarget;
    switch (m_interaction) {
    case HUMAN_COMMAND_DEFUSE_MINE:
        m_human->ExecuteBodyCommand(Interact(INTERACT_MANIPULATE_LONG_END, target));
        if (completed && IsServer() && target)
            static_cast<C_Mine *>(target)->Defuse();
        break;
    case HUMAN_COMMAND_ENTER_VEHICLE:                   // a photo
        if (target && completed && target->GetType() == ACTOR_PHOTO_PLACE)
            static_cast<C_PhotoPlace *>(target)->TakePhoto(m_human);
        break;
    case HUMAN_COMMAND_PLANT_EXPLOSIVE:
        if (completed && IsServer())
            static_cast<C_ExplosivePlace *>(target)->Place(m_human);
        m_human->ExecuteBodyCommand(Interact(INTERACT_MANIPULATE_LONG_2_END, target));
        break;
    case HUMAN_COMMAND_PICK_LOCK:
        m_human->ExecuteBodyCommand(Interact(INTERACT_MANIPULATE_END, target));
        if (completed && IsServer() && target) {
            static_cast<C_Door *>(target)->PickLock();
            m_human->AddLockpick(0.5f);
        }
        break;
    case HUMAN_COMMAND_HEAL:
        m_human->ExecuteBodyCommand(Interact(INTERACT_HEAL_END, target));
        static_cast<C_Human *>(target)->ExecuteBodyCommand(Interact(INTERACT_HEAL_HERE_END, m_human));
        if (completed && IsServer() && target)
            UseMedkit(target->GetNetId(), -1);
        break;
    case HUMAN_COMMAND_CUT_WIRE:
        m_human->ExecuteBodyCommand(Interact(INTERACT_CUT_END, target));
        if (completed && IsServer() && target)
            static_cast<C_WireCut *>(target)->Cut(XYZ(m_viewDir), false);
        break;
    case HUMAN_COMMAND_LAY_MINE:
        if (completed && IsServer() && m_handsItem) {
            // On the ground half a metre in front of the body.
            const S_matrix &body = m_human->GetBodyFrame()->GetMatrix();
            S_vector forward = body(2);
            forward.Normalize();
            S_vector pos = body(3);
            pos.x += forward.x * MINE_PLACE_DISTANCE;
            pos.z += forward.z * MINE_PLACE_DISTANCE;
            const S_vector down(0.0f, -MINE_PLACE_RAY, 0.0f);
            S_vector hit = pos;
            S_vector normal = forward;
            if (g_collisionWorld.RayTestStatic(pos, down, &hit, &normal, 0))
                g_temporaryActors.CreateMine(m_human, m_handsItem->GetTypeId(), hit, normal, true);
        }
        m_human->ExecuteBodyCommand(Interact(INTERACT_MANIPULATE_LONG_2_END, target));
        UpdateHandsItem();
        break;
    case HUMAN_COMMAND_TIE_UP:
        m_human->ExecuteBodyCommand(Interact(INTERACT_TIE_UP_END, target));
        if (target)
            static_cast<C_Human *>(target)->ExecuteBodyCommand(Interact(INTERACT_BE_TIED_UP_END, m_human));
        if (completed) {
            if (IsServer())
                static_cast<C_Human *>(target)->KnockOut(m_human);
            else
                static_cast<C_Human *>(target)->SetUnconscious(m_human);
        }
        break;
    case HUMAN_COMMAND_HEAL_SELF:
        m_human->ExecuteBodyCommand(Interact(INTERACT_24_END, m_human));
        if (completed && IsServer() && target)
            UseMedkit(m_human->GetNetId(), m_interactionArg);
        break;
    case HUMAN_COMMAND_BACKSTAB:
        if (target && completed) {
            m_human->ExecuteBodyCommand(Interact(INTERACT_BACKSTAB_END, target));
            static_cast<C_Human *>(target)->ExecuteBodyCommand(Interact(INTERACT_BE_BACKSTABBED_END, m_human));
        }
        break;
    case HUMAN_COMMAND_DISARM_EXPLOSIVE:
        m_human->ExecuteBodyCommand(Interact(INTERACT_MANIPULATE_LONG_END, target));
        if (completed && IsServer() && target)
            static_cast<C_ExplosivePlace *>(target)->Disarm(m_human);
        break;
    default:
        break;
    }
    ResetInteraction();
}

void C_HumanController::UpdateInteraction(int timeDelta)
{
    if (m_speechDelay > 0) {
        m_speechDelay -= timeDelta;
        if (m_speechDelay < 0)
            m_speechDelay = 0;
    }
    if (m_interactionTime <= 0)
        return;
    // The interaction breaks off when its actor goes away.
    C_Actor *target = m_interactionTarget;
    switch (m_interaction) {
    case HUMAN_COMMAND_DEFUSE_MINE: {
        const std::vector<C_Actor *> &actors = g_temporaryActors.GetActors();
        if (std::find(actors.rbegin(), actors.rend(), target) == actors.rend()) {
            EndInteraction(false);
            return;
        }
        break;
    }
    case HUMAN_COMMAND_PLANT_EXPLOSIVE:
        if (static_cast<C_ExplosivePlace *>(target)->GetState() != 0) {
            EndInteraction(false);
            return;
        }
        break;
    case HUMAN_COMMAND_HEAL:
    case HUMAN_COMMAND_TIE_UP:
    case HUMAN_COMMAND_HEAL_SELF:
        if (static_cast<C_Human *>(target)->m_stats[STAT_HEALTH] < 1 || m_human->m_stats[STAT_HEALTH] < 1) {
            EndInteraction(false);
            return;
        }
        break;
    case HUMAN_COMMAND_DISARM_EXPLOSIVE:
        if (static_cast<C_ExplosivePlace *>(target)->GetState() != 1) {
            EndInteraction(false);
            return;
        }
        break;
    default:
        break;
    }
    m_interactionTime -= timeDelta;
    if (m_interactionTime < 1)
        EndInteraction(true);
}

void C_HumanController::LeaveVehicle()
{
    C_Actor *vehicle = m_human->m_vehicle;
    if (!vehicle) {
        if (m_human->m_carried) {
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_CARRY_BODY));
            m_human->m_carried = nullptr;
        } else if (m_interaction != 0) {
            // (Also with no interaction going on, -1: EndInteraction then only resets it.)
            EndInteraction(false);
        }
        return;
    }
    const int seat = m_human->m_seat;
    switch (vehicle->GetType()) {
    case ACTOR_CAR:
        vehicle->SetSeatBlocked(seat, false);
        m_human->ExecuteBodyCommand(Interact(INTERACT_FLAG_BODY | INTERACT_LEAVE_CAR, vehicle, seat));
        if (!IsLocalGame()) {
            // The network position jumps to where the human gets out.
            if (C_NetProperty *property = m_netObject->GetProperty(1)) {
                const S_matrix &m = m_human->GetFrame()->GetMatrix();
                property->GetTrack().SetValue(reinterpret_cast<const S_vector4 &>(m.m[3]));
            }
        }
        break;
    case ACTOR_SUBMARINE:
        vehicle->SetSeatBlocked(seat, false);
        m_human->ExecuteBodyCommand(Interact(INTERACT_FLAG_BODY | INTERACT_LEAVE_SUBMARINE, vehicle, seat));
        break;
    case ACTOR_STATIC_WEAPON:
        vehicle->SetSeatBlocked(seat, false);
        m_human->ExecuteBodyCommand(Interact(INTERACT_FLAG_BODY | INTERACT_LEAVE_STATIC_WEAPON, vehicle, seat));
        break;
    default:
        break;                                          // (nothing for the Ju 88)
    }
}

bool C_HumanController::UseActor(const S_HumanCommand &command)
{
    C_Actor *actor = g_actorsModule.GetActorById(command.param1);
    if (!actor)
        return false;
    const int seat = command.param2;
    switch (actor->GetType()) {
    case ACTOR_CAR: {
        // Not into a car with enemies aboard.
        if (!IsLocalGame() && actor->IsSeatBlocked(seat))
            return false;
        if (static_cast<C_Car *>(actor)->HasEnemyAboard(m_human))
            return false;
        actor->SetSeatBlocked(seat, true);
        m_human->ExecuteBodyCommand(Interact(INTERACT_FLAG_BODY | INTERACT_ENTER_CAR, actor, seat));
        return true;
    }
    case ACTOR_RADIO:
        static_cast<C_Radio *>(actor)->Use(m_human);
        return true;
    case ACTOR_SWITCH:
        static_cast<C_Switch *>(actor)->Toggle();
        return true;
    case ACTOR_PHOTO_PLACE: {
        C_Item *camera = m_human->FindItem(ITEM_PHOTO_CAMERA);
        if (!camera)
            return false;
        m_interactionTarget = actor;
        LowerWeapon();
        m_human->SetDisguise(DISGUISE_LOSS_ACTION);
        m_interactionTime = 2000;
        m_interaction = command.type;
        if (IsClient()) {
            m_human->ExecuteBodyCommand(
                MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, WORK_ANIMATION_PHOTO, FrameArg(camera->GetFrame(0))));
            if (m_human->IsLocalPlayer() && GetGame()->IsFirstPersonView())
                GetGame()->GetInterface()->ShowPhotoFlash();
        }
        return true;
    }
    case ACTOR_SUBMARINE:
        if (actor->IsSeatBlocked(seat))
            return false;
        actor->SetSeatBlocked(seat, true);
        m_human->ExecuteBodyCommand(Interact(INTERACT_FLAG_BODY | INTERACT_ENTER_SUBMARINE, actor, seat));
        return true;
    case ACTOR_STATIC_WEAPON:
        if (actor->IsSeatBlocked(seat))
            return false;
        actor->SetSeatBlocked(seat, true);
        m_human->ExecuteBodyCommand(Interact(INTERACT_FLAG_BODY | INTERACT_ENTER_STATIC_WEAPON, actor, seat));
        return true;
    case ACTOR_JUNKERS88:
        if (actor->IsSeatBlocked(seat))
            return false;
        actor->SetSeatBlocked(seat, true);
        m_human->ExecuteBodyCommand(Interact(INTERACT_FLAG_BODY | INTERACT_ENTER_JUNKERS88, actor, seat));
        return true;
    default:
        return false;
    }
}

bool C_HumanController::ExecuteCommand(const S_HumanCommand &command)
{
    switch (command.type) {
    case HUMAN_COMMAND_LOCK_DOOR:
    case HUMAN_COMMAND_UNLOCK_DOOR: {
        if (IsServer()) {
            C_Door *door = static_cast<C_Door *>(g_actorsModule.GetActorById(command.param1));
            if (door) {                                 // (Fix: the original reads a missing door's key)
                // With the door's key if the human carries it.
                const int key = m_human->FindItem(door->GetLockKey()) ? door->GetLockKey() : -1;
                if (command.type == HUMAN_COMMAND_LOCK_DOOR)
                    door->Lock(key);
                else
                    door->Unlock(key);
            }
        }
        if (IsClient())
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, ANIM_DOOR_KEY));
        return true;
    }
    case HUMAN_COMMAND_DEFUSE_MINE:
        m_interactionTarget = g_temporaryActors.FindActor(command.param1);
        if (m_interactionTarget) {
            m_interaction = command.type;
            m_interactionTime = 4000;
            m_human->ExecuteBodyCommand(Interact(INTERACT_MANIPULATE_LONG, m_interactionTarget));
            ShowAction(m_human, m_interactionTime, TEXT_ACTION_DEFUSE);
            return true;
        }
        return false;
    case HUMAN_COMMAND_ENTER_VEHICLE:
        return UseActor(command);
    case HUMAN_COMMAND_PLANT_EXPLOSIVE:
        m_interactionTarget = g_actorsModule.GetActorById(command.param1);
        if (m_interactionTarget) {
            m_human->SetDisguise(DISGUISE_LOSS_ACTION);
            m_interaction = command.type;
            m_interactionTime = 3000;
            m_human->ExecuteBodyCommand(Interact(INTERACT_MANIPULATE_LONG_2, m_interactionTarget));
            ShowAction(m_human, m_interactionTime, TEXT_ACTION_PLANT_EXPLOSIVE);
            return true;
        }
        return false;
    case HUMAN_COMMAND_LEAVE_VEHICLE:
        LeaveVehicle();
        return true;
    case HUMAN_COMMAND_PICK_UP_BODY: {
        C_Actor *body = g_actorsModule.GetActorById(command.param1);
        if (!body || body->GetType() != ACTOR_HUMAN)
            return false;
        m_interaction = command.type;
        m_interactionTime = 1500;
        m_human->m_carried = body;
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_CARRY_BODY, FrameArg(body->GetFrame())));
        return true;
    }
    case HUMAN_COMMAND_PICK_LOCK:
        m_interactionTarget = g_actorsModule.GetActorById(command.param1);
        if (m_interactionTarget) {
            m_human->SetDisguise(DISGUISE_LOSS_ACTION);
            m_interaction = command.type;
            // 5.5 s without the skill, 0.5 s with all of it.
            m_interactionTime = static_cast<int>((1.0f - m_human->GetStatFactor(STAT_LOCKPICK)) * 5000.0f + 500.0f);
            m_human->ExecuteBodyCommand(Interact(INTERACT_MANIPULATE, m_interactionTarget));
            ShowAction(m_human, m_interactionTime, TEXT_ACTION_PICK_LOCK);
            return true;
        }
        return false;
    case HUMAN_COMMAND_HEAL:
        m_interactionTarget = g_actorsModule.GetActorById(command.param1);
        if (m_interactionTarget) {
            C_Human *patient = static_cast<C_Human *>(m_interactionTarget);
            m_interaction = command.type;
            m_interactionTime = 3000;
            m_human->ExecuteBodyCommand(Interact(INTERACT_HEAL, patient));
            patient->ExecuteBodyCommand(Interact(INTERACT_HEAL_HERE, m_human));
            ShowAction(m_human, m_interactionTime, TEXT_ACTION_HEAL);
            ShowAction(patient, m_interactionTime, TEXT_ACTION_HEAL);
            return true;
        }
        return false;
    case HUMAN_COMMAND_CUT_WIRE: {
        C_Item *cutter = m_human->FindBeltItem19();
        if (!cutter)
            return false;
        m_interactionTarget = g_temporaryActors.FindActor(command.param1);
        if (m_interactionTarget) {
            m_interaction = command.type;
            m_interactionTime = 3900;
            m_human->SetDisguise(DISGUISE_LOSS_ACTION);
            m_human->ExecuteBodyCommand(Interact(INTERACT_CUT, m_interactionTarget, FrameArg(cutter->GetFrame(0))));
            ShowAction(m_human, m_interactionTime, TEXT_ACTION_CUT_WIRE);
            return true;
        }
        return false;
    }
    case HUMAN_COMMAND_LAY_MINE:
        UpdateHandsItem();
        // (Fix: the original reads a missing hands item's definition on the server.)
        if (!IsServer() || (m_handsItem && BehaviourOf(m_handsItem) == ITEM_BEHAVIOUR_MINE)) {
            m_interaction = command.type;
            m_interactionTime = 2000;
            m_human->ExecuteBodyCommand(Interact(INTERACT_MANIPULATE_LONG_2, nullptr));
            ShowAction(m_human, m_interactionTime, TEXT_ACTION_LAY_MINE);
            return true;
        }
        return false;
    case HUMAN_COMMAND_TIE_UP: {
        C_Actor *actor = g_actorsModule.GetActorById(command.param1);
        m_interactionTarget = actor;
        if (actor && actor->GetType() == ACTOR_HUMAN && static_cast<C_Human *>(actor)->m_stats[STAT_HEALTH] > 0) {
            m_interaction = command.type;
            m_interactionTime = 5000;
            static_cast<C_Human *>(actor)->ExecuteBodyCommand(Interact(INTERACT_BE_TIED_UP, m_human));
            m_human->ExecuteBodyCommand(Interact(INTERACT_TIE_UP, actor));
            ShowAction(m_human, m_interactionTime, TEXT_ACTION_TIE_UP);
            return true;
        }
        return false;
    }
    case HUMAN_COMMAND_PLACE_CAR_CHARGE: {
        UpdateHandsItem();
        C_Car *car = static_cast<C_Car *>(g_actorsModule.GetActorById(command.param1));
        if (!car || !m_handsItem || BehaviourOf(m_handsItem) != ITEM_BEHAVIOUR_STICKY_CHARGE)
            return false;
        const S_matrix &body = m_human->GetBodyFrame()->GetMatrix();
        const S_vector4 &from = reinterpret_cast<const S_vector4 &>(body.m[3]);
        // Fix: the original passes uninitialised values when the ray misses the car.
        S_vector4 pos = from;
        S_vector4 normal = {0.0f, 0.0f, 0.0f, 0.0f};
        car->RayTest(from, m_human->GetViewDir(), &pos, &normal);
        if (IsServer()) {
            if (car->IsDestroyed())
                return false;
            return car->PlaceCharge(m_handsItem, m_human, pos, normal);
        }
        return car->AttachCharge(m_handsItem, m_human, pos, normal);
    }
    case HUMAN_COMMAND_OPEN_DOOR:
    case HUMAN_COMMAND_CLOSE_DOOR: {
        const bool open = command.type == HUMAN_COMMAND_OPEN_DOOR;
        C_Actor *door = g_actorsModule.GetActorById(command.param1);
        if ((m_moveFlags & MOVE_LYING) && IsServer()) {
            // Lying: the door moves without the body's animation.
            if (door)
                static_cast<C_Door *>(door)->SetState(open ? 1 : 0, true, false, 0);
            return true;
        }
        m_human->ExecuteBodyCommand(Interact(INTERACT_DOOR, door, open ? 1 : 0));
        return true;
    }
    case HUMAN_COMMAND_USE_LADDER:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_USE_OBJECT, 0,
                                                    g_actorsModule.GetActorById(command.param1)));
        return true;
    case HUMAN_COMMAND_CLIMB:
        // A client's copy of another player's human starts from the network position.
        if (m_netObject && g_localPlayerSlot != m_human->GetNetId() && !IsServer()) {
            // (Fix: the original does not check the property, unlike LeaveVehicle.)
            if (C_NetProperty *property = m_netObject->GetProperty(1)) {
                const S_vector4 pos = property->GetTrack().GetValue();
                m_human->GetMovement()->m_visual.SetPosition(pos);
            }
        }
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_CLIMB));
        return true;
    case HUMAN_COMMAND_DELIVER_DOCUMENTS:
        if (!IsServer())
            return true;
        if (C_Actor *place = g_actorsModule.GetActorById(command.param1))   // (Fix: the original calls it on a missing actor)
            return static_cast<C_DocumentPlace *>(place)->Deliver(m_human);
        return false;
    case HUMAN_COMMAND_HEAL_SELF:
        if (m_human->m_stats[STAT_HEALTH] > 0 && m_human->GetHealthPercent() != 100 &&
            m_human->m_body.GetClimbState() == 0 && !InVehicleSeat(m_human->m_vehicle) &&
            !m_human->m_body.IsWorkState5()) {
            m_interactionTarget = m_human;
            m_interactionTime = 3000;
            m_interaction = command.type;
            m_interactionArg = command.param1;           // the medkit's uid
            m_human->ExecuteBodyCommand(Interact(INTERACT_24, m_human));
            ShowAction(m_human, m_interactionTime, TEXT_ACTION_HEAL);
            return true;
        }
        return false;
    case HUMAN_COMMAND_BACKSTAB:
        m_interactionTarget = g_actorsModule.GetActorById(command.param1);
        if (m_interactionTarget) {
            m_interaction = command.type;
            m_interactionTime = 1000;
            m_human->ExecuteBodyCommand(Interact(INTERACT_BACKSTAB, m_interactionTarget));
            static_cast<C_Human *>(m_interactionTarget)->ExecuteBodyCommand(Interact(INTERACT_BE_BACKSTABBED, m_human));
            ShowAction(m_human, m_interactionTime, TEXT_ACTION_BACKSTAB);
            return true;
        }
        return false;
    case HUMAN_COMMAND_DISARM_EXPLOSIVE:
        m_interactionTarget = g_actorsModule.GetActorById(command.param1);
        if (m_interactionTarget) {
            m_interaction = command.type;
            m_interactionTime = 4000;
            m_human->ExecuteBodyCommand(Interact(INTERACT_MANIPULATE_LONG, m_interactionTarget));
            ShowAction(m_human, m_interactionTime, TEXT_ACTION_DEFUSE);
            return true;
        }
        return false;
    default:
        return true;
    }
}

// ------------------------------------------------------------------------- saved games

void C_HumanController::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_CONTROLLER_STATE);
    chunk.Write(m_moveFlags);
    chunk.Write(m_stance);
    chunk.Write(m_quietTime);
    chunk.Write(m_unknownB4);
    chunk.Write(m_weaponRaised);
    chunk.Write(&m_viewDir, sizeof(S_vector));
    chunk.Write(&m_facingDir, sizeof(S_vector));
    chunk.Write(m_stateFlags);
    chunk.Write(m_alarmRaised);
    chunk.Ascend();
    chunk.Begin(CHUNK_CONTROLLER_SPEECH);
    chunk.Write(m_speechDelay);
    chunk.Ascend();
    if (m_interaction != -1) {
        // The actor by its id and list.
        chunk.Begin(CHUNK_CONTROLLER_INTERACTION);
        chunk.Write(m_interaction);
        chunk.Write(m_interactionTime);
        // Fix: the original reads the actor of an interaction without one (laying a mine).
        const int targetId = m_interactionTarget ? m_interactionTarget->GetNetId() : -1;
        const int temporary = m_interactionTarget && m_interactionTarget->IsTemporary() ? 1 : 0;
        chunk.Write(targetId);
        chunk.Write(temporary);
        chunk.Write(m_interactionArg);
        chunk.Ascend();
    }
}

void C_HumanController::LoadGame(C_chunk &chunk)
{
    int targetId = -1;
    int temporary = 0;
    m_speechDelay = 0;
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_CONTROLLER_STATE:
            // (Only the first five fields are checked against the chunk's size.)
            if (chunk.RemainingSize() >= 4)
                chunk.Read(m_moveFlags);
            if (chunk.RemainingSize() >= 4)
                chunk.Read(m_stance);
            if (chunk.RemainingSize() >= 4)
                chunk.Read(m_quietTime);
            if (chunk.RemainingSize() >= 1)
                chunk.Read(m_unknownB4);
            if (chunk.RemainingSize() >= 1)
                chunk.Read(m_weaponRaised);
            chunk.Read(&m_viewDir, sizeof(S_vector));
            chunk.Read(&m_facingDir, sizeof(S_vector));
            chunk.Read(m_stateFlags);
            chunk.Read(m_alarmRaised);
            chunk.Ascend();
            break;
        case CHUNK_CONTROLLER_INTERACTION:
            chunk.Read(m_interaction);
            chunk.Read(m_interactionTime);
            chunk.Read(targetId);
            chunk.Read(temporary);
            chunk.Read(m_interactionArg);
            chunk.Ascend();
            break;
        case CHUNK_CONTROLLER_SPEECH:
            chunk.Read(m_speechDelay);
            chunk.Ascend();
            break;
        default:
            // (The original does not leave unknown chunks.)
            break;
        }
    }
    if (m_interaction == HUMAN_COMMAND_HEAL_SELF)
        m_interactionTarget = m_human;
    else if (m_interaction != -1)
        m_interactionTarget = temporary ? g_temporaryActors.FindActor(targetId) : g_actorsModule.GetActorById(targetId);
    if (m_weaponRaised)
        RaiseWeapon();
    else
        LowerWeapon();
}

// ------------------------------------------------------------------------- speech, stamina

void C_HumanController::SayNow(int sentence, bool silent)
{
    C_SpeechSet *speechSet = m_human->m_speechSet;
    if (!speechSet)                                     // (Fix: the original calls it on a missing set)
        return;
    const uint32_t code = static_cast<uint32_t>(speechSet->SayNow(sentence));
    if (m_human->m_role == HUMAN_ROLE_PLAYER)
        m_human->SayQuickMessage(static_cast<int>(SpeechCode_Base(code)));
    if (silent)
        return;
    // Within twice the code's variant (m) of the camera.
    const S_vector camera = GetGame()->GetScene()->GetActiveCamera()->GetWorldPos();
    const S_vector offset = camera - m_human->GetBodyFrame()->GetWorldPos();
    const float range = static_cast<float>(SpeechCode_GetVariant(code) * 2);
    if (offset.Square() <= range * range) {
        // (The sound module's slot 0x50, PlayDubbing: plays the speech code's dubbing.)
        GetSoundModule()->PlayDubbing(m_human, nullptr, static_cast<int>(SpeechCode_Base(code)),
                                      static_cast<float>(SpeechCode_GetSet(code)), range, false);
    }
}

int C_HumanController::Say(int sentence, int delay, uint32_t flags, bool force)
{
    C_SpeechSet *speechSet = m_human->m_speechSet;
    if (!speechSet)
        return 0;
    const int sample = speechSet->Say(sentence, 0);
    if (sample < 0 || m_speechDelay > 0 || (m_moveFlags & MOVE_SWIMMING))
        return 0;
    const uint32_t code = static_cast<uint32_t>(sample);
    m_speechDelay = 1000;
    speechSet->SetDelay(sentence, delay);
    if (!IsClient() && !(flags & 1)) {
        // The AI hears it. (PlayDubbingWithNoise returns at once without a client, so on a
        // dedicated server this does nothing; as in the original.)
        GetSoundModule()->PlayDubbingWithNoise(m_human, nullptr, static_cast<int>(SpeechCode_Base(code)),
                                               static_cast<float>(SpeechCode_GetSet(code)),
                                               static_cast<float>(SpeechCode_GetVariant(code) * 2), false,
                                               flags | 2);
    }
    if (IsLocalGame()) {
        SayNow(sentence, force);
    } else {
        // A server tells the clients, unless no player in play is within range (the player's
        // soldiers always tell them).
        bool heard = true;
        if (m_human->m_role != HUMAN_ROLE_PLAYER) {
            heard = false;
            const S_vector &pos = m_human->GetFrame()->GetWorldPos();
            const float range = static_cast<float>(SpeechCode_GetVariant(code) * 2);
            for (int i = MAX_PLAYER_SLOTS; i-- > 0;) {
                C_Human *player = static_cast<C_Human *>(g_actorsModule.GetActor(i));
                if (player == m_human || !player || player->m_stats[STAT_HEALTH] <= 0 || player->IsSpectating() ||
                    !player->IsInPlay())
                    continue;
                if ((player->GetFrame()->GetWorldPos() - pos).Magnitude() <= range)
                    heard = true;
            }
        }
        if (heard) {
            S_MsgSay *message = new S_MsgSay;
            message->sentence = sentence;
            message->force = force;
            SendToClients(message);
        }
    }
    // The sample's length (ms; the player's controller waits for it before a spoken command
    // is carried out).
    return GetSoundModule()->GetDubbingSampleSize(static_cast<int>(SpeechCode_Base(code)));
}

void C_HumanController::UpdateStamina(uint32_t timeDelta)
{
    C_Human *human = m_human;
    const float load = human->GetCarriedWeight() / static_cast<float>(human->GetHitPoints());
    // (An integer division: 1 at the full endurance, 0 below it.)
    const float endurance =
        static_cast<float>(human->m_stats[STAT_ENDURANCE] / human->m_initialStats[STAT_ENDURANCE]);
    float rate;
    switch (human->GetMoveSpeedClass()) {
    case 0:
        rate = (1.0f - load + endurance) * 1.5f;
        break;
    case 1:
        rate = (1.0f - load + endurance) * 1.0f;
        break;
    case 2:
        rate = load * -1.0f;
        break;
    case 5:
        rate = load * -20.0f;
        break;
    default: {
        const int stance = human->GetStance();
        const float factor = stance == 1 ? 4.0f : stance == 2 ? 6.0f : 2.0f;
        rate = (1.0f - load + endurance) * factor;
        break;
    }
    }
    human->m_stamina = static_cast<float>(timeDelta) * rate * 0.001f + human->m_stamina;
    const float maximum = static_cast<float>(human->m_stats[STAT_ENDURANCE]);
    if (maximum < human->m_stamina)
        human->m_stamina = maximum;
    else if (human->m_stamina < 0.0f)
        human->m_stamina = 0.0f;
    // Exhausted: no more marching.
    if (human->m_stamina == 0.0f && m_stateFlags == MOVE_PACE_MARCH)
        m_stateFlags = MOVE_PACE_4;
}

// ------------------------------------------------------------------------- healing

void C_HumanController::Heal(int humanId, int itemUid, int health, bool removeItem)
{
    C_Human *patient = static_cast<C_Human *>(g_actorsModule.GetHuman(humanId));
    if (!patient)
        return;
    const S_vector green(0.0f, 160.0f, 0.0f);
    const S_vector white(255.0f, 255.0f, 255.0f);
    if (patient == m_human) {
        if (m_human->IsLocalPlayer())
            C_Human::PrintText(m_human, green, TEXT_YOU_HEALED_YOURSELF);
        else if (IsLocalGame())
            C_Human::PrintText(m_human, white, TEXT_HEALED_HIMSELF, m_human->GetFullName());
    } else if (m_human->IsLocalPlayer()) {
        C_Human::PrintText(m_human, green, TEXT_YOU_HEALED, patient->GetFullName());
    } else if (patient->IsLocalPlayer()) {
        C_Human::PrintText(patient, green, TEXT_HEALED_YOU, m_human->GetFullName());
    } else if (IsLocalGame()) {
        C_Human::PrintText(m_human, white, TEXT_HEALED, m_human->GetFullName(), patient->GetFullName());
    }
    // A client applies the server's result.
    if (!IsServer() && patient->m_stats[STAT_HEALTH] != patient->m_initialStats[STAT_HEALTH]) {
        C_Item *medkit = m_human->FindItemByUid(itemUid);
        if (medkit && BehaviourOf(medkit) == ITEM_BEHAVIOUR_MEDKIT) {
            medkit->Consume(((100.0f - static_cast<float>(m_human->m_stats[STAT_FIRST_AID])) * 0.01f + 1.0f) *
                            static_cast<float>(health));
            patient->m_stats[STAT_HEALTH] += health;
            if (removeItem)
                m_human->RemoveItem(itemUid, true);
        }
    }
}

bool C_HumanController::UseMedkit(int humanId, int itemUid)
{
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
    if (static_cast<int>(medkit->GetCharges()) < health)
        health = static_cast<int>(medkit->GetCharges());
    if (patient->m_stats[STAT_HEALTH] + health > patient->m_initialStats[STAT_HEALTH])
        health = patient->m_initialStats[STAT_HEALTH] - patient->m_stats[STAT_HEALTH];
    medkit->Consume(((100.0f - static_cast<float>(firstAid)) * 0.01f + 1.0f) * static_cast<float>(health));
    patient->m_stats[STAT_HEALTH] += health;
    if (IsLocalGame()) {
        m_human->AddFirstAid(0.1f);
        Heal(humanId, itemUid, health, false);
    }
    if (static_cast<int>(medkit->GetCharges()) == 0)
        m_human->RemoveItem(itemUid, true);
    return true;
}

// ------------------------------------------------------------------------- weapons

int C_HumanController::UseItem(C_Item *item, bool fire, uint32_t flags, S_vector4 *origin, S_vector4 *dir)
{
    // A client fires only for its own player.
    if (!IsLocalGame() && !m_human->IsLocalPlayer() && m_human->m_role == HUMAN_ROLE_PLAYER)
        return 0;
    if (m_human->IsLocalPlayer()) {
        // The local player aims with the camera.
        *dir = GetGame()->GetFirstPersonView()->GetAimDir();
        if (GetGame()->IsFirstPersonView())
            *origin = GetGame()->GetFirstPersonView()->GetAimOrigin();
        else
            *origin = reinterpret_cast<const S_vector4 &>(
                (item ? ShotFrameOf(item) : m_human->GetBodyFrame())->GetMatrix().m[3]);
    } else {
        *dir = m_viewDir;
        *origin = reinterpret_cast<const S_vector4 &>(
            (item ? ShotFrameOf(item) : m_human->GetBodyFrame())->GetMatrix().m[3]);
    }
    // The local player throws grenades a little upward (at most 0.7).
    if (m_human->IsLocalPlayer() && item && BehaviourOf(item) == ITEM_BEHAVIOUR_THROWN && dir->y > -0.9f &&
        dir->y < 0.7f) {
        const float y = dir->y;
        dir->y = y + 0.3f;
        if (y + 0.3f > 0.7f)
            dir->y = 0.7f;
        S_vector d(dir->x, dir->y, dir->z);
        d.Normalize();
        dir->x = d.x;
        dir->y = d.y;
        dir->z = d.z;
    }
    if (!item)                                          // (Fix: the original fires through a null item)
        return 0;
    if (!fire)
        return item->FireSecondary(m_human, XYZ(*origin), XYZ(*dir), static_cast<uint8_t>(flags));
    return item->Fire(m_human, XYZ(*origin), XYZ(*dir), flags);
}

int C_HumanController::UseItemAt(C_Item *item, bool fire, uint32_t flags, const S_vector4 &origin,
                                 const S_vector4 &dir)
{
    if (fire)
        return item->Fire(m_human, XYZ(origin), XYZ(dir), flags);
    return item->FireSecondary(m_human, XYZ(origin), XYZ(dir), static_cast<uint8_t>(flags));
}

void C_HumanController::RaiseWeapon()
{
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RAISE_WEAPON));
    const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};
    RequestAction(CONTROLLER_ACTION_RAISE_WEAPON, &zero, &zero, 0.0f, -1, -1);
}

void C_HumanController::LowerWeapon()
{
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_LOWER_WEAPON));
    const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};
    RequestAction(CONTROLLER_ACTION_LOWER_WEAPON, &zero, &zero, 0.0f, -1, -1);
}

void C_HumanController::ApplyWeaponControls(int timeDelta, uint32_t fireFlags)
{
    m_quietTime += timeDelta;
    if (!m_buttons || !m_axes)
        return;
    // In a vehicle: the seat's weapon; the driver has no other.
    if (C_Actor *vehicle = m_human->m_vehicle) {
        const int seat = m_human->m_seat;
        if (vehicle->HasSeatWeapon(seat)) {
            vehicle->ApplySeatControls(seat, m_axes, m_buttons);
            return;
        }
        if (seat == 0)
            return;
    }
    const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};

    // A grenade from the belt.
    if (m_buttons[BUTTON_THROW] != 0.0f) {
        C_Item *grenade = m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN);
        if (!grenade)
            return;
        m_human->ExecuteBodyCommand(
            MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_THROW_BELT_GRENADE, FrameArg(grenade->GetFrame(0))));
        m_human->SetDisguise(DISGUISE_LOSS_SHOT);
        RequestAction(CONTROLLER_ACTION_START_BELT_THROW, &zero, &zero, 1.0f, -1, -1);
        return;
    }
    if (!m_handsItem)
        return;
    switch (BehaviourOf(m_handsItem)) {
    case ITEM_BEHAVIOUR_THROWN:
        // A grenade in the hands: the throws.
        if (m_buttons[BUTTON_FIRE] != 0.0f) {
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_THROW));
            NotifyHud(FPV_EVENT_FIRE);
        } else if (m_buttons[BUTTON_7] != 0.0f) {
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_THROW_2));
            NotifyHud(FPV_EVENT_AIM_FIRE);
        }
        return;
    case ITEM_BEHAVIOUR_SIDEARM:
    case 4:
    case ITEM_BEHAVIOUR_LAUNCHER:
        // No automatic fire.
        m_axes[AXIS_FIRE] = 0.0f;
        break;
    case 3:
    case 5:
    case 7:
    case 12:
        break;
    default:
        return;
    }

    C_WeaponItem *weapon = static_cast<C_WeaponItem *>(m_handsItem);
    if (m_buttons[BUTTON_FIRE] == 0.0f && m_axes[AXIS_FIRE] == 0.0f) {
        if (m_buttons[BUTTON_RELOAD] == 0.0f)
            return;
        // Enemies keep their last magazine (it refills the weapon without being used up).
        int ammoUid = -1;
        if (!weapon->Reload(m_human, ITEM_CONTAINER_BELT, &ammoUid, m_human->m_role == HUMAN_ROLE_ENEMY))
            return;
        NotifyHud(FPV_EVENT_RELOAD);
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RELOAD));
        RequestAction(CONTROLLER_ACTION_RELOAD, &zero, &zero, 0.0f, ammoUid,
                      static_cast<int>(weapon->GetLoadedRounds()));
        return;
    }
    if (!m_weaponRaised) {
        // (Not while marching forward.)
        if ((m_moveFlags & (MOVE_PACE_MARCH | MOVE_FORWARD)) != (MOVE_PACE_MARCH | MOVE_FORWARD))
            RaiseWeapon();
        return;
    }
    if (weapon->GetAmount() == 0.0f || (weapon->GetFlags() & WEAPON_RELOAD_PENDING)) {
        // Empty: firing reloads.
        if (m_buttons[BUTTON_FIRE] == 0.0f)
            return;
        int ammoUid = -1;
        if (!weapon->Reload(m_human, ITEM_CONTAINER_BELT, &ammoUid, m_human->m_role == HUMAN_ROLE_ENEMY))
            return;
        NotifyHud(FPV_EVENT_RELOAD);
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RELOAD));
        weapon->ClearReloadPending();
        RequestAction(CONTROLLER_ACTION_RELOAD, &zero, &zero, 0.0f, ammoUid,
                      static_cast<int>(weapon->GetLoadedRounds()));
        return;
    }
    if (static_cast<int>(GetGame()->GetMissionTime() - weapon->GetAttackTime()) < 0)
        return;
    // Fire from the shot dummy along the view (the AI's aim adjusts it).
    S_vector4 dir = m_viewDir;
    const S_vector4 origin = reinterpret_cast<const S_vector4 &>(ShotFrameOf(weapon)->GetMatrix().m[3]);
    AdjustAim(reinterpret_cast<S_vector *>(&dir));
    weapon->Fire(m_human, XYZ(origin), XYZ(dir), fireFlags);
    const C_WeaponComp *component = static_cast<const C_WeaponComp *>(weapon->GetDef()->GetComponent());
    weapon->SetAttackTime(GetGame()->GetMissionTime() + static_cast<int>(component->GetAttackDelay()));
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_FIRE));
    m_quietTime = 0;
    m_human->SetDisguise(DISGUISE_LOSS_SHOT);
    NotifyHud(FPV_EVENT_FIRE);
    RequestAction(CONTROLLER_ACTION_FIRE, &origin, &dir, 0.0f, -1, -1);
}
