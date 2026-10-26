// C_AIController, the base of the AI controllers (squad members, enemies, scripted humans,
// civilians; original source 0x0056ee90..0x00582000): its life cycle, the network side of an
// AI human, saved games, the car driving the scripts ask for and the small slots. The senses
// are in ai_senses.cpp, the combat in ai_combat.cpp, the movement in ai_movement.cpp; the
// parts it is built from in ai_parts.cpp.
#include "game/human_controller.h"

#include <cmath>

#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/ai_parts.h"
#include "game/checkpoints.h"
#include "game/controller_internal.h"
#include "game/controller_net.h"
#include "game/controller_messages.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_body.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"

float g_aiAxes[CONTROL_COUNT];
float g_aiButtons[CONTROL_COUNT];

namespace {

// Saved game chunks of C_AIController (the parts write their own inside theirs).
enum : uint16_t {
    CHUNK_AI_BASE = 5000,                               // C_HumanController::SaveGame
    CHUNK_AI_TIMERS = 0x1389,
    CHUNK_AI_WAYPOINTS = 0x138A,                        // the waypoints as in memory
    CHUNK_AI_WAYPOINT = 0x138B,
    CHUNK_AI_FACING = 0x138C,
    CHUNK_AI_FIRE_ORDER = 0x138D,
    CHUNK_AI_MOVE = 0x138E,
    CHUNK_AI_ALERT = 0x138F,
    CHUNK_AI_FIRE = 0x1390,
    CHUNK_AI_TASK_TYPE = 0x1391,
    CHUNK_AI_TASK = 0x1392,                             // CHUNK_AI_TASK_TYPE, then the task's data
    CHUNK_AI_DRIVER = 0x139C,
    CHUNK_AI_UNKNOWN11C = 0x13A6,                       // empty: present when the flag is set
};


// The body's movement flags the AI starts with (S_HumanBody::SetMovementFlags; meanings not
// identified): enemies and controller type 8 get 2, the squad 1, the others 4.
constexpr uint32_t BODY_MOVEMENT_ENEMY = 2;
constexpr uint32_t BODY_MOVEMENT_SQUAD = 1;
constexpr uint32_t BODY_MOVEMENT_OTHER = 4;

// The dive costs that much stamina (the network's dive message needs more).
constexpr float DIVE_STAMINA_NEEDED = 10.0f;

}  // namespace

// ------------------------------------------------------------------------- life cycle

C_AIController::C_AIController(E_HumanController type)
    : C_HumanController(type)
{
    m_facing = new S_AIFacing;
    m_fireOrder = new S_AIFireOrder;
    m_alert = new S_AIAlertState;
    m_move = new S_AIMoveOrder;
    m_driver = new C_AIDriver;
}

C_AIController::~C_AIController()
{
    delete m_facing;
    delete m_fireOrder;
    delete m_alert;
    delete m_driver;
    delete m_move;
    // Fix: the original leaves the waypoints and the task out of their pools when the
    // controller goes without Detach().
    for (S_AIWaypoint *waypoint : m_waypoints)
        ReleaseWaypoint(waypoint);
    ReleaseWaypoint(m_waypoint);
    ::ReleaseTask(m_task);
}

void C_AIController::SetAlertState(int state)
{
    m_alert->Set(state);
    ApplyAlertState();
}

void C_AIController::Attach(C_Human *human, C_HumanController *previous)
{
    C_HumanController::Attach(human, previous);
    m_ignoreCircumvention = false;
    m_unknown120 = 0;
    if (m_moveFlags & MOVE_ON_LADDER) {
        // The two walking points of the ladder it is on.
        C_Actor *ladder = reinterpret_cast<C_Actor *>(static_cast<intptr_t>(human->m_body.GetClimbTarget()));
        std::vector<uint16_t> points;
        if (m_checkpoints)                              // (Fix: the original calls it on a missing network)
            m_checkpoints->GetActorPoints(ladder, points);
        if (ladder && points.size() == 2) {
            m_move->ladder = ladder;
            m_move->ladderFrom = points[0];
            m_move->ladderTo = points[1];
            m_move->flags |= S_AIMoveOrder::FLAG_TO_LADDER;
        }
    }
    if (m_type == HUMAN_CONTROLLER_ENEMY || m_type == HUMAN_CONTROLLER_SNIPER) {
        human->m_body.ClearMovementFlags(4);
        human->m_body.SetMovementFlags(BODY_MOVEMENT_ENEMY);
    } else if (m_type == HUMAN_CONTROLLER_SQUAD) {
        human->m_body.ClearMovementFlags(6);
        human->m_body.SetMovementFlags(BODY_MOVEMENT_SQUAD);
    } else {
        human->m_body.ClearMovementFlags(6);
        human->m_body.SetMovementFlags(BODY_MOVEMENT_OTHER);
    }
    if (m_checkpoints) {
        const S_vector &pos = human->GetFrame()->GetWorldPos();
        m_currentCheckpoint = m_checkpoints->Get(m_checkpoints->FindNearest(pos, 5.0f, -1.0f));
    }
    m_facing->SetFacing(m_facingDir);
    m_facing->SetView(m_viewDir);
    if (IsLocalGame()) {
        m_netObject = nullptr;
        return;
    }
    // The human's net object reads and writes the control bits of this controller.
    m_netObject->SetMessageFactory(NetCreateMessage, this);
    m_netObject->SetStateHandlers(NetReadControlBits, NetWriteControlBits, this);
    m_netObject->SetSendMask(7);
    m_netObject->SetUnknown40(false);
    if (!IsServer())
        m_netObject->SetUnknown3C(0);
    m_netObject->GetProperty(1)->GetTrack().SetValue(human->GetPosition4());
    // A new AI human: the clients are told its equipment.
    if (!previous && IsServer()) {
        S_MsgEquipment *message = new S_MsgEquipment;
        message->human = human->GetNetId();
        SendToClients(message);
    }
}

void C_AIController::Detach()
{
    // (The base's Detach is not called: the human's net object stays.)
    for (size_t i = m_waypoints.size(); i-- > 0;)
        ReleaseWaypoint(m_waypoints[i]);
    m_waypoints.clear();
    if (m_waypoint) {
        ReleaseWaypoint(m_waypoint);
        m_waypoint = nullptr;
    }
    FlushMessages(true);
    ReleaseTask();
}

void C_AIController::ReleaseTask()
{
    if (m_task) {
        ::ReleaseTask(m_task);
        m_task = nullptr;
    }
}

// ------------------------------------------------------------------------- the network side

void C_AIController::UpdateNetwork(int frameTime)
{
    // Only co-operative network games replicate the AI.
    if (IsLocalGame() || g_mpSettings->gameType != MP_GAME_COOPERATIVE)
        return;
    const S_vector4 pos = reinterpret_cast<const S_vector4 &>(m_human->GetFrame()->GetMatrix().m[3]);
    const S_vector &viewDir = GetViewDir();
    S_vector4 view = reinterpret_cast<const S_vector4 &>(viewDir);
    reinterpret_cast<S_vector &>(view).Normalize();
    C_NetProperty *property = m_netObject->GetProperty(1);
    // (In a vehicle the vehicle sends the position.)
    if (!InVehicleSeat(m_human->m_vehicle))
        property->GetTrack().SetValue(pos);
    property->GetDirTrack().SetValue(view, 0.0f);
    // The rate: the walk's direction at the human's speed.
    S_vector rate = m_human->GetMovement()->GetVelocity();
    rate.Normalize();
    rate *= m_human->GetVelocity().Magnitude();
    property->GetTrack().SetRate(rate);
    if (m_axes && m_buttons)
        m_controlBits = EncodeControlBits(m_axes, m_moveFlags);
    m_netObject->Tick(static_cast<float>(frameTime) * 0.001f);
    // A dive.
    if (!m_buttons || m_buttons[BUTTON_DIVE] == 0.0f)
        return;
    if (m_human->m_stamina <= DIVE_STAMINA_NEEDED)
        return;
    if (!(m_moveFlags & MOVE_MOVING_MASK))
        return;
    m_netObject->QueueMessage(new C_NetDiveMessage, g_netReplication->GetLocalId(), true);
}

void C_AIController::RequestAction(int action, const S_vector4 *pos, const S_vector4 *dir, float strength,
                                   int ammoUid, int rounds)
{
    if (IsLocalGame())
        return;
    C_NetActionMessage *message = new C_NetActionMessage;
    message->action = action;
    message->fromClient = !IsServer();
    message->dir = *dir;
    message->pos = *pos;
    // (Fix: the original reads a weapon's field of any item in the hands.)
    int shotInterval = 0;
    if (m_handsItem && m_handsItem->GetKind() == ITEM_KIND_WEAPON)
        shotInterval = static_cast<C_WeaponItem *>(m_handsItem)->GetShotInterval() / 100;
    message->shotInterval = shotInterval;
    message->ammoUid = ammoUid;
    message->strength = strength;
    message->rounds = static_cast<uint8_t>(rounds);
    m_netObject->QueueMessage(message, g_netReplication->GetLocalId(), true);
}

void C_AIController::Freeze(bool on)
{
    if (!IsLocalGame())
        m_netObject->SetSendMask(on ? 0 : 7);
}

void C_AIController::FlushMessages(bool toClients)
{
    // The control state goes unreliably, the rest reliably; types 5 and above 23 are not sent.
    std::vector<S_ControllerMessage *> &queue = toClients ? m_toClients : m_toServer;
    C_BitStream *unreliable = nullptr;
    C_BitStream *reliable = nullptr;
    const int humanId = m_human->GetNetId();
    for (S_ControllerMessage *message : queue) {
        if (message->type == 5 || message->type > CTRL_MSG_23)
            continue;
        const bool isReliable = message->type != CTRL_MSG_CONTROL_STATE;
        C_BitStream *&stream = isReliable ? reliable : unreliable;
        if (!stream)
            stream = toClients ? CreateActorMessage(humanId, isReliable) : CreateActorMessageEx(humanId, isReliable);
        message->Write(*static_cast<C_NetMessage *>(stream));
    }
    for (C_BitStream *stream : {unreliable, reliable}) {
        if (!stream)
            continue;
        stream->WriteBits(CTRL_MSG_END, CTRL_MSG_TYPE_BITS);
        SendActorMessage(stream);
    }
    if (toClients)
        ClearMessagesToClients();
    else
        ClearMessagesToServer();
}

void C_AIController::OnMessage(C_BitStream *stream)
{
    // A client: the server's messages about the human.
    C_NetMessage &msg = *static_cast<C_NetMessage *>(stream);
    for (;;) {
        switch (static_cast<int>(msg.ReadBits(CTRL_MSG_TYPE_BITS))) {
        case CTRL_MSG_CONTROLS: {
            S_MsgControls message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_HIT: {
            S_MsgHit message;
            message.Read(msg);
            m_human->ApplyRemoteHit(message.hitType, message.zone, message.health, message.attacker,
                                    XYZ(message.pos), XYZ(message.velocity));
            break;
        }
        case CTRL_MSG_PUT_ON_UNIFORM: {
            S_MsgPutOnUniform message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_COMMAND: {
            S_MsgCommand message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_CONTROL_STATE: {
            S_MsgControlState message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_USE_OBJECT: {
            S_MsgUseObject message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_MOVE_ITEM: {
            S_MsgMoveItem message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_TRANSFER_ITEM: {
            S_MsgTransferItem message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_PICK_UP_ITEM: {
            S_MsgPickUpItem message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_DROP_ITEM: {
            S_MsgDropItem message;
            message.Read(msg);
            m_human->ClientDropItem(message.actor, message.uid, XYZ(message.dir) * message.speed);
            break;
        }
        case CTRL_MSG_TAKE_FROM_BOX: {
            S_MsgTakeFromBox message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_ACTION: {
            S_MsgAction message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_INVENTORY_CHECK: {
            S_MsgInventoryCheck message;
            message.human = m_human;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_HEAL: {
            S_MsgHeal message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_REMOTE_REQUEST: {
            S_MsgRemoteRequest message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_SOLDIER_CLASS: {
            S_MsgSoldierClass message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_17: {
            S_Msg17 message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_ANIMATION: {
            S_MsgAnimation message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_SAY: {
            S_MsgSay message;
            message.Read(msg);
            SayNow(message.sentence, message.force);
            break;
        }
        case CTRL_MSG_EQUIPMENT: {
            S_MsgEquipment message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_SUSPEND: {
            S_MsgSuspend message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_23: {
            S_Msg23 message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_END:
            return;
        default:
            // (Types 5 and 21 and anything else: the next type is read at once.)
            break;
        }
    }
}

void C_AIController::OnMessage2(C_BitStream *stream)
{
    // The server: a client's requests for the human (a client AI only asks for inventory
    // moves). What the server carries out goes on to the clients.
    C_NetMessage &msg = *static_cast<C_NetMessage *>(stream);
    for (;;) {
        switch (static_cast<int>(msg.ReadBits(CTRL_MSG_TYPE_BITS))) {
        case CTRL_MSG_MOVE_ITEM: {
            S_MsgMoveItem *message = new S_MsgMoveItem;
            message->Read(msg);
            const bool moved = m_human->ServerMoveItem(message->uid, message->other, message->fromContainer,
                                                       &message->toContainer, message->slot);
            if (!moved || IsLocalGame())
                delete message;
            else
                SendToClients(message);
            break;
        }
        case CTRL_MSG_TRANSFER_ITEM: {
            S_MsgTransferItem *message = new S_MsgTransferItem;
            message->Read(msg);
            const bool moved = m_human->ServerTransferItem(message->uid, message->arg, message->other,
                                                           message->otherUid, message->container);
            if (!moved || IsLocalGame())
                delete message;
            else
                SendToClients(message);
            break;
        }
        case CTRL_MSG_DROP_ITEM: {
            S_MsgDropItem message;
            message.Read(msg);
            m_human->DropItem(message.uid, XYZ(message.dir) * message.speed);
            break;
        }
        case CTRL_MSG_TAKE_FROM_BOX: {
            S_MsgTakeFromBox *message = new S_MsgTakeFromBox;
            message->Read(msg);
            // (Also queued in a local game, unlike the item moves.)
            if (m_human->TakeFromSupplyBox(message->box, message->item, &message->container, &message->uid))
                SendToClients(message);
            else
                delete message;
            break;
        }
        case CTRL_MSG_END:
            return;
        default:
            break;
        }
    }
}

void C_AIController::IssueCommand(const S_HumanCommand &command)
{
    // Carried out at once; the clients see it too.
    ExecuteCommand(command);
    S_MsgCommand *message = new S_MsgCommand;
    message->command = command.type;
    message->actor = command.param1;
    message->arg = command.param2;
    SendToClients(message);
}

// ------------------------------------------------------------------------- the body

void C_AIController::OnBodyFlags(uint32_t flags)
{
    // The base's handling without its local-player check: the AI's throws always go out.
    m_weaponRaised = (flags & BODY_FLAG_WEAPON_RAISED) != 0;
    m_unknownAC = (flags & BODY_FLAG_UNKNOWN_100) != 0;
    if (flags & BODY_FLAG_UNKNOWN_10000)
        m_moveFlags |= MOVE_UNKNOWN_8000000;
    else
        m_moveFlags &= ~MOVE_UNKNOWN_8000000;
    UpdateHandsItem();
    if (!m_handsItem || GetGame()->GetMissionTime() < m_handsItem->GetTime())
        m_unknownAC = false;

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

void C_AIController::LookAroundAt(int type, const S_vector4 &pos)
{
    S_BodyCommand command = MakeBodyCommand(BODY_COMMAND_POSE, type);
    std::memcpy(command.data, &pos, sizeof(pos));
    m_human->ExecuteBodyCommand(command);
    if (type == 4 || type == 3)
        m_moveFlags |= MOVE_LOOKING_AROUND;
    else
        m_moveFlags &= ~MOVE_LOOKING_AROUND;
}

void C_AIController::LookAround(int type)
{
    m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_POSE, type));
    if (type == 4 || type == 3)
        m_moveFlags |= MOVE_LOOKING_AROUND;
    else
        m_moveFlags &= ~MOVE_LOOKING_AROUND;
}

void C_AIController::SetMoveFlags(uint32_t flags)
{
    const uint32_t old = m_moveFlags;
    C_HumanController::SetMoveFlags(flags);
    // Lying, the view keeps closer to the facing.
    if (m_facing && (old & MOVE_STANCE_MASK) != (m_moveFlags & MOVE_STANCE_MASK))
        m_facing->cone = (m_moveFlags & MOVE_LYING) ? S_AIFacing::CONE_LYING : S_AIFacing::CONE;
}

const S_vector &C_AIController::GetFacingDir()
{
    return reinterpret_cast<const S_vector &>(m_facing->facing);
}

const S_vector &C_AIController::GetViewDir()
{
    return reinterpret_cast<const S_vector &>(m_facing->view);
}

void C_AIController::SetFacingDir(const S_vector4 &dir)
{
    m_facing->SetFacing(dir);
}

void C_AIController::SetViewDir(const S_vector4 &dir)
{
    m_facing->SetView(dir);
}

bool C_AIController::OnCollisionEx(C_Actor *other, const S_vector * /*pos*/, int, int, int, int, int)
{
    // Ladders do not block.
    return other->GetType() != ACTOR_LADDER;
}

bool C_AIController::PlanGrenade()
{
    return !(m_moveFlags & MOVE_FIRING) && !(m_moveFlags & MOVE_LYING) && m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN);
}

bool C_AIController::Unknown108()
{
    return m_move && (m_move->flags & (S_AIMoveOrder::FLAG_TO_LADDER | S_AIMoveOrder::FLAG_ON_LADDER)) != 0;
}

void C_AIController::GetPathPoints(uint16_t *current, uint16_t *next)
{
    // The first waypoint's point, and the index of the walking point the human was at
    // (unchanged without one).
    if (!m_waypoints.empty())
        *next = static_cast<uint16_t>(m_waypoints[0]->checkpoint);
    else
        *next = 0xFFFF;
    if (m_currentCheckpoint) {
        int index = m_checkpoints->GetIndex(m_currentCheckpoint);
        if (index >= m_checkpoints->GetCount() || index < 0)
            index = 0xFFFF;
        *current = static_cast<uint16_t>(index);
    }
}

// ------------------------------------------------------------------------- driving

void C_AIController::Drive(int roadCheckpoint, float speed, int label)
{
    C_Actor *vehicle = m_human->m_vehicle;
    if (!vehicle || vehicle->GetType() != ACTOR_CAR)
        return;
    if (static_cast<int16_t>(roadCheckpoint) == -1) {
        m_driver->Stop();
        return;
    }
    const S_Checkpoint *point = GetGame()->GetCheckpoints()->GetRoadNetwork()->Get(roadCheckpoint);
    if (!point)
        return;
    m_driver->SetVehicle(vehicle);
    m_driver->DriveTo(ToVector4(point->pos, 0.0f), speed);   // (w: a stale local in the original)
    m_driver->m_label = label;
}

// ------------------------------------------------------------------------- saved games

void C_AIController::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_AI_BASE);
    C_HumanController::SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_AI_TIMERS);
    chunk.Write(m_lookAroundTime);
    chunk.Write(m_unknown10C);
    chunk.Write(m_unknown110);
    chunk.Write(m_unknown114);
    chunk.Write(m_unknown118);
    chunk.Ascend();
    chunk.Begin(CHUNK_AI_FACING);
    m_facing->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_AI_FIRE_ORDER);
    m_fireOrder->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_AI_MOVE);
    m_move->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_AI_ALERT);
    m_alert->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_AI_DRIVER);
    m_driver->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_AI_FIRE);
    m_fire.SaveGame(chunk);
    chunk.Ascend();
    if (m_task) {
        chunk.Begin(CHUNK_AI_TASK);
        chunk.WriteChunk(CHUNK_AI_TASK_TYPE, m_task->type);
        m_task->SaveGame(chunk);
        chunk.Ascend();
    }
    if (!m_waypoints.empty()) {
        chunk.Begin(CHUNK_AI_WAYPOINTS);
        for (const S_AIWaypoint *waypoint : m_waypoints)
            chunk.Write(waypoint, sizeof(S_AIWaypoint));
        chunk.Ascend();
    }
    if (m_waypoint) {
        chunk.Begin(CHUNK_AI_WAYPOINT);
        chunk.Write(m_waypoint, sizeof(S_AIWaypoint));
        chunk.Ascend();
    }
    if (m_ignoreCircumvention) {
        chunk.Begin(CHUNK_AI_UNKNOWN11C);
        chunk.Ascend();
    }
}

void C_AIController::LoadGame(C_chunk &chunk)
{
    m_ignoreCircumvention = false;
    for (size_t i = m_waypoints.size(); i-- > 0;)
        ReleaseWaypoint(m_waypoints[i]);
    m_waypoints.clear();
    ReleaseWaypoint(m_waypoint);
    m_waypoint = nullptr;
    if (m_task)
        ::ReleaseTask(m_task);
    m_task = nullptr;

    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_AI_BASE:
            C_HumanController::LoadGame(chunk);
            break;
        case CHUNK_AI_TIMERS:
            if (chunk.RemainingSize() >= 4)
                chunk.Read(m_lookAroundTime);
            if (chunk.RemainingSize() >= 4)
                chunk.Read(m_unknown10C);
            if (chunk.RemainingSize() >= 4)
                chunk.Read(m_unknown110);
            if (chunk.RemainingSize() >= 4)
                chunk.Read(m_unknown114);
            if (chunk.RemainingSize() >= 4)
                chunk.Read(m_unknown118);
            break;
        case CHUNK_AI_WAYPOINTS:
            // (Fix: the original loops forever on a chunk that does not end on a waypoint.)
            while (chunk.RemainingSize() >= static_cast<int>(sizeof(S_AIWaypoint))) {
                S_AIWaypoint *waypoint = AllocWaypoint();
                chunk.Read(waypoint, sizeof(S_AIWaypoint));
                m_waypoints.push_back(waypoint);
            }
            break;
        case CHUNK_AI_WAYPOINT:
            m_waypoint = AllocWaypoint();
            chunk.Read(m_waypoint, sizeof(S_AIWaypoint));
            break;
        case CHUNK_AI_FACING:
            m_facing->LoadGame(chunk);
            break;
        case CHUNK_AI_FIRE_ORDER:
            m_fireOrder->LoadGame(chunk);
            break;
        case CHUNK_AI_MOVE:
            m_move->LoadGame(chunk);
            break;
        case CHUNK_AI_ALERT:
            m_alert->LoadGame(chunk);
            break;
        case CHUNK_AI_FIRE:
            m_fire.LoadGame(chunk);
            break;
        case CHUNK_AI_TASK: {
            uint16_t child;
            if (chunk.Descend(child) && child == CHUNK_AI_TASK_TYPE) {
                int type = 0;
                chunk.Read(type);
                chunk.Ascend();
                m_task = AllocTask(type);
                if (m_task)                             // (Fix: the original calls through null for an unknown type)
                    m_task->LoadGame(chunk);
            }
            break;
        }
        case CHUNK_AI_DRIVER:
            m_driver->LoadGame(chunk);
            break;
        case CHUNK_AI_UNKNOWN11C:
            m_ignoreCircumvention = true;
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
}
