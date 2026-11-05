// C_RemoteController, a network client's copy of another player's human (original source
// 0x00592140..0x005941b0): it replays what the server sends, the replicated position, view and
// control bits, the actions of the human's net object and the controller messages; the few
// requests the client makes on the human (inventory moves) go to the server.
#include "game/human_controller.h"

#include <cstdint>

#include "game/actors_module.h"
#include "game/controller_internal.h"
#include "game/controller_messages.h"
#include "game/controller_net.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/player_internal.h"
#include "game/profile.h"
#include "game/soldier.h"
#include "net/bitstream.h"

// ------------------------------------------------------------------------- life cycle

void C_RemoteController::Attach(C_Human *human, C_HumanController *previous)
{
    C_HumanController::Attach(human, previous);
    for (int i = 0; i < CONTROL_COUNT; i++) {
        m_controlAxes[i] = 0.0f;
        m_controlButtons[i] = 0.0f;
    }
    m_human->GetMovement()->SetUnknown18C(false);
    m_netObject->SetMessageFactory(NetCreateMessage, this);
    m_netObject->SetStateHandlers(NetReadControlBits, NetWriteControlBits, this);
    m_netObject->GetProperty(1)->GetTrack().SetValue(m_human->GetPosition4());
    m_spectating = false;
    m_vip = false;
    m_surrendered = false;
}

void C_RemoteController::Detach()
{
    C_HumanController::Detach();
    m_human = nullptr;
}

void C_RemoteController::Update(int frameTime)
{
    FlushMessages(false);
    if (m_moveFlags & MOVE_DEAD)
        return;
    // A spectator's human (or one not in play) leaves the world.
    const bool detached = m_spectating || !m_human->m_inPlay;
    C_HumanMovement *movement = m_human->GetMovement();
    if (movement->IsDetached() != detached) {
        movement->SetDetachedFlag(detached);
        movement->OnFrozen(detached);
        m_human->GetFrame()->SetOn(!detached);
    }
    UpdateInteraction(frameTime);
    m_axes = m_controlAxes;
    m_unknownBC++;
    m_buttons = m_controlButtons;
    // (Fix: without a net object the original decodes stale values and then reads the missing
    // object's messages.)
    if (!m_netObject)
        return;
    m_netObject->Update(static_cast<float>(static_cast<uint32_t>(frameTime)) * 0.001f);
    C_NetProperty *property = m_netObject->GetProperty(1);
    const bool seated = InVehicleSeat(m_human->m_vehicle);
    // (In a vehicle the vehicle replicates the position.)
    const S_vector4 pos = seated ? m_human->GetPosition4() : property->GetTrack().GetCurrent();
    const S_vector4 dir = property->GetDirTrack().GetCurrent();
    const S_vector4 velocity = property->GetTrack().GetRate();
    SetViewDir(dir);
    S_vector flat(dir.x, 0.0f, dir.z);
    NormalizeBy(flat, flat.z * flat.z + flat.x * flat.x);
    if (!seated && (pos.y * pos.y + pos.z * pos.z) + pos.x * pos.x >= 1e-16f) {
        C_HumanVisual &visual = movement->m_visual;
        visual.SetPosition(pos);
        visual.SetDirection(ToVector4(flat, 0.0f), 0.0f);   // (w: a stale local in the original)
    }
    DecodeControls(m_controlAxes, m_controlButtons, 0, &velocity.x, &flat.x, 0, m_controlBits);
    ProcessNetMessages();
    ApplyMoveControls();
    for (float &value : m_controlButtons)
        value = 0.0f;
    for (float &value : m_controlAxes)
        value = 0.0f;
}

// ------------------------------------------------------------------------- the net object

void C_RemoteController::ProcessNetMessages()
{
    while (C_NetObjectMessage *message = m_netObject->PopMessage(0)) {
        switch (message->type) {
        case NET_MSG_ACTION:
            PlayAction(*static_cast<C_NetActionMessage *>(message));
            break;
        case NET_MSG_DIVE:
            m_buttons[BUTTON_DIVE] = 1.0f;
            m_axes[BUTTON_DIVE] = 1.0f;
            break;
        case NET_MSG_BINOCULARS:
            if (!static_cast<C_NetBinocularsMessage *>(message)->on) {
                m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, WORK_ANIMATION_BINOCULARS_AWAY));
            } else if (C_Item *binoculars = m_human->FindBeltItem(ITEM_BEHAVIOUR_BINOCULARS)) {
                m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, WORK_ANIMATION_BINOCULARS_OUT,
                                                            FrameArg(binoculars->GetFrame(0))));
            }
            break;
        default:
            break;
        }
        ReleaseNetMessage(message);
    }
}

void C_RemoteController::PlayAction(const C_NetActionMessage &message)
{
    // What the player did, as the server passed it on (the weapons fire here too: the shots'
    // effects; UseItemAt's flag 1).
    UpdateHandsItem();
    switch (message.action) {
    case CONTROLLER_ACTION_LOWER_WEAPON:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_LOWER_WEAPON));
        return;
    case CONTROLLER_ACTION_RAISE_WEAPON:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RAISE_WEAPON));
        return;
    case CONTROLLER_ACTION_FIRE: {
        UpdateHandsItem();
        if (!m_handsItem)
            return;
        // From the muzzle along the view.
        const S_vector &muzzle = ShotFrameOf(m_handsItem)->GetWorldPos();
        m_handsItem->Fire(m_human, muzzle, GetViewDir(), 0);
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_FIRE));
        return;
    }
    case CONTROLLER_ACTION_THROW_BELT_GRENADE:
        if (C_Item *grenade = m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN)) {
            static_cast<C_WeaponItem *>(grenade)->SetThrowStrength(message.strength);
            UseItemAt(grenade, true, 1, message.pos, message.dir);
        }
        return;
    case CONTROLLER_ACTION_RELOAD:
        if (m_handsItem)
            m_handsItem->SetRounds(m_human, message.ammoUid, message.rounds);
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RELOAD));
        return;
    case CONTROLLER_ACTION_THROW:
    case CONTROLLER_ACTION_THROW_2:
        if (m_handsItem && BehaviourOf(m_handsItem) == ITEM_BEHAVIOUR_THROWN) {
            static_cast<C_WeaponItem *>(m_handsItem)->SetThrowStrength(message.strength);
            UseItemAt(m_handsItem, message.action == CONTROLLER_ACTION_THROW, 1, message.pos, message.dir);
        }
        return;
    case CONTROLLER_ACTION_8:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_STAB));
        return;
    case CONTROLLER_ACTION_MELEE_2:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_STAB_2));
        return;
    case CONTROLLER_ACTION_EMPTY:
        UpdateHandsItem();
        // (Fix: the original plays a weapon's sound of any item in the hands.)
        if (m_handsItem && m_handsItem->GetKind() == ITEM_KIND_WEAPON)
            static_cast<C_WeaponItem *>(m_handsItem)->PlayEmptySound(m_human);
        return;
    case CONTROLLER_ACTION_START_THROW:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_THROW));
        return;
    case CONTROLLER_ACTION_START_THROW_2:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_THROW_2));
        return;
    case CONTROLLER_ACTION_START_BELT_THROW:
        if (C_Item *grenade = m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN)) {
            static_cast<C_WeaponItem *>(grenade)->SetThrowStrength(message.strength);
            m_human->ExecuteBodyCommand(
                MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_THROW_BELT_GRENADE, FrameArg(grenade->GetFrame(0))));
        }
        return;
    case CONTROLLER_ACTION_RISE_FROM_COVER:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RISE_FROM_COVER));
        return;
    case CONTROLLER_ACTION_BACK_TO_COVER:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_BACK_TO_COVER));
        return;
    default:
        return;
    }
}

void C_RemoteController::PlayCommandGesture(const S_MsgAction &message)
{
    int gesture;
    if (GetSquadCommandGesture(message.command.action, &gesture))
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, gesture));
}

void C_RemoteController::OnBodyFlags(uint32_t flags)
{
    m_weaponRaised = (flags & BODY_FLAG_WEAPON_RAISED) != 0;
    m_unknownAC = (flags & BODY_FLAG_UNKNOWN_100) != 0;
}

// ------------------------------------------------------------------------- requests

bool C_RemoteController::RequestMoveItem(int uid, int otherId, int fromContainer, int toContainer, int slot)
{
    S_MsgMoveItem *message = new S_MsgMoveItem;
    message->uid = uid;
    message->other = otherId;
    message->fromContainer = fromContainer;
    message->toContainer = toContainer;
    message->slot = slot;
    SendToServer(message);
    return true;
}

bool C_RemoteController::RequestTransferItem(int uid, int arg, int otherId, int otherUid, int container)
{
    // (The original stores CTRL_MSG_MOVE_ITEM as the message's type; its Write sends
    // CTRL_MSG_TRANSFER_ITEM regardless.)
    S_MsgTransferItem *message = new S_MsgTransferItem;
    message->uid = uid;
    message->arg = arg;
    message->other = otherId;
    message->otherUid = otherUid;
    message->container = container;
    SendToServer(message);
    return true;
}

void C_RemoteController::RequestThrowItem(int uid, const S_vector &velocity)
{
    S_vector dir = velocity;
    NormalizeBy(dir, (dir.z * dir.z + dir.y * dir.y) + dir.x * dir.x);
    const float speed = MagnitudeBy((velocity.x * velocity.x + velocity.y * velocity.y) + velocity.z * velocity.z);
    S_MsgDropItem *message = new S_MsgDropItem;
    message->uid = uid;
    message->dir = ToVector4(dir, 0.0f);                // (the original copies a fourth float past the vector)
    message->speed = speed;
    message->actor = -1;
    SendToServer(message);
}

// ------------------------------------------------------------------------- controller messages

void C_RemoteController::FlushMessages(bool toClients)
{
    // Only the inventory moves (types 7, 8 and 10) are sent, in one reliable message.
    std::vector<S_ControllerMessage *> &queue = toClients ? m_toClients : m_toServer;
    C_BitStream *stream = nullptr;
    const int humanId = m_human->GetNetId();
    for (S_ControllerMessage *message : queue) {
        const int type = message->type;
        if (type != CTRL_MSG_MOVE_ITEM && type != CTRL_MSG_TRANSFER_ITEM && type != CTRL_MSG_DROP_ITEM)
            continue;
        if (!stream)
            stream = toClients ? CreateActorMessage(humanId, true) : CreateActorMessageEx(humanId, true);
        message->Write(*static_cast<C_NetMessage *>(stream));
    }
    if (stream) {
        stream->WriteBits(CTRL_MSG_END, CTRL_MSG_TYPE_BITS);
        SendActorMessage(stream);
    }
    if (toClients)
        ClearMessagesToClients();
    else
        ClearMessagesToServer();
}

void C_RemoteController::OnMessage(C_BitStream *stream)
{
    // The server's messages about the human.
    C_NetMessage &msg = *static_cast<C_NetMessage *>(stream);
    for (;;) {
        switch (static_cast<int>(msg.ReadBits(CTRL_MSG_TYPE_BITS))) {
        case CTRL_MSG_CONTROLS: {
            S_MsgControls message;
            message.Read(msg);
            for (int i = 0; i < CONTROL_COUNT; i++)
                m_controlAxes[i] = message.axes[i];
            for (int i = 0; i < CONTROL_COUNT; i++)
                m_controlButtons[i] = message.buttons[i];
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
            m_human->ClientPutOnUniform(message.uid);
            break;
        }
        case CTRL_MSG_COMMAND: {
            S_MsgCommand message;
            message.Read(msg);
            S_HumanCommand command;
            command.category = HUMAN_COMMAND_VEHICLE;
            command.type = message.command;
            command.param1 = message.actor;
            command.param2 = message.arg;
            ExecuteCommand(command);
            break;
        }
        case CTRL_MSG_CONTROL_STATE: {
            S_MsgControlState message;
            message.Read(msg);
            SetViewDir(message.dir);
            break;
        }
        case CTRL_MSG_USE_OBJECT: {
            S_MsgUseObject message;
            message.Read(msg);
            m_human->GetMovement()->UseObject(g_actorsModule.GetActorById(message.actor));
            break;
        }
        case CTRL_MSG_MOVE_ITEM: {
            S_MsgMoveItem message;
            message.Read(msg);
            m_human->ClientMoveItem(message.uid, message.other, message.fromContainer, message.toContainer,
                                    message.slot);
            break;
        }
        case CTRL_MSG_TRANSFER_ITEM: {
            S_MsgTransferItem message;
            message.Read(msg);
            m_human->ClientTransferItem(message.uid, message.arg, message.other, message.otherUid,
                                        message.container);
            break;
        }
        case CTRL_MSG_PICK_UP_ITEM: {
            S_MsgPickUpItem message;
            message.Read(msg);
            m_human->ClientPickUpItem(message.uid, message.container);
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
            m_human->ClientTakeFromSupplyBox(message.box, message.item, message.container, message.uid);
            break;
        }
        case CTRL_MSG_ACTION: {
            S_MsgAction message;
            message.Read(msg);
            PlayCommandGesture(message);
            break;
        }
        case CTRL_MSG_INVENTORY_CHECK: {
            // (The original leaves the message's human unset: a stale value.)
            S_MsgInventoryCheck message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_HEAL: {
            S_MsgHeal message;
            message.Read(msg);
            Heal(message.human, message.itemUid, message.health, message.removeItem);
            break;
        }
        case CTRL_MSG_REMOTE_REQUEST: {
            S_MsgRemoteRequest message;
            message.Read(msg);
            m_human->GetMovement()->OnRemoteAction(message.request);
            break;
        }
        case CTRL_MSG_SOLDIER_CLASS: {
            S_MsgSoldierClass message;
            message.Read(msg);
            // The skills at the class the net game knows (unlike the player controller, before
            // this message sets it), then the new class.
            int skills[STAT_COUNT] = {HUMAN_FULL_HEALTH, 80, 80, 80, 80, 80, 50, 30, 20, 50, 0, 80};
            g_gameProfile.m_humanStore->GetStats(skills, m_human->GetProfileIndex(),
                                                 g_netGame.GetPlayerStats(m_human->GetNetId())[5]);
            m_human->SetSkills(skills);
            g_netGame.SetPlayerClass(message.soldierClass, m_human->GetNetId());
            break;
        }
        case CTRL_MSG_ANIMATION: {
            S_MsgAnimation message;
            message.Read(msg);
            m_human->PlayAnimation(message.name, message.blendTime, message.cancelTime, message.loop);
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
            m_human->Freeze(message.on);
            break;
        }
        case CTRL_MSG_23: {
            // The human surrenders (hands up) or stops.
            S_Msg23 message;
            message.Read(msg);
            m_surrendered = message.on;
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION,
                                                        message.on ? WORK_ANIMATION_SURRENDER : WORK_ANIMATION_END_ACTIVITY));
            break;
        }
        case CTRL_MSG_END:
            return;
        default:
            // (Types 5, 17 and 21 are not read: the next type is read at once, out of step
            // with the stream.)
            break;
        }
    }
}
