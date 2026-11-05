// The network side of C_PlayerController (original source 0x0053b370..0x0053b990 for the net
// object's messages, the player's in 0x0053d340..0x005404f0 and 0x005406c0..0x00541cb0,
// 0x00542920, 0x005441e0, 0x00544b40, 0x005470b0, 0x00547270).
//
// In a network game every player's human has a player controller on the server and on its
// own client; the other clients copy it with a remote controller. The client drives its human
// with its keys and replicates the state through the human's net object (position, view and
// control bits, its messages: the body actions, the dive, the binoculars), and asks the
// server for everything else with controller messages (inventory moves, commands ...). The
// server's copy replays the replicated state, carries the requests out and passes them on
// to the other clients.
#include "game/first_person_view.h"
#include "game/human_controller.h"

#include <windows.h>
#include <mmsystem.h>

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
#include "game/squad_commander.h"
#include "net/bitstream.h"

namespace {

// Bits of the net object messages' kind (NET_MSG_*, 3 kinds) and of an action (19 kinds).
constexpr int NET_MSG_TYPE_BITS = 2;
constexpr int NET_ACTION_BITS = 5;
// A throw's strength: 4 bits (0..15) after a bit telling there is one.
constexpr int NET_STRENGTH_BITS = 4;
constexpr int NET_STRENGTH_MAX = 15;
// The bits of the control state the net object replicates (EncodeControlBits).
constexpr int NET_CONTROL_BITS = 9;

// The actions whose message carries the direction, the position, the shot interval and the
// strength: the throws always, the others from a client only (a client's fire for the server's
// check; what the server passes on needs none).
bool HasAim(int action)
{
    switch (action) {
    case CONTROLLER_ACTION_THROW_BELT_GRENADE:
    case CONTROLLER_ACTION_THROW:
    case CONTROLLER_ACTION_THROW_2:
    case CONTROLLER_ACTION_START_BELT_THROW:
        return true;
    default:
        return false;
    }
}

// The server notes the player's activity (S_NetPlayerState::activityTime).
void StampActivity(const C_Human *human)
{
    S_NetPlayerState *players = g_netGame.LockPlayers();
    players[human->GetNetId()].activityTime = timeGetTime();
    g_netGame.UnlockPlayers();
}

}  // namespace

void ApplySoldierClass(C_Human *human, int soldierClass)
{
    int *playerStats = g_netGame.GetPlayerStats(human->GetNetId());
    playerStats[5] = soldierClass;
    // The soldier's skills at that class (the defaults when the profile has none).
    int skills[STAT_COUNT] = {HUMAN_FULL_HEALTH, 80, 80, 80, 80, 80, 50, 30, 20, 50, 0, 80};
    g_gameProfile.m_humanStore->GetStats(skills, human->GetProfileIndex(), playerStats[5]);
    human->SetSkills(skills);
    // (The class is stored already: the message tells of a rise even when it went down.)
    g_netGame.SetPlayerClass(soldierClass, human->GetNetId());
}

// ------------------------------------------------------------------------- the net object

void C_NetActionMessage::Write(C_BitStream *stream)     // 0x0053b370
{
    stream->WriteBits(static_cast<uint32_t>(type), NET_MSG_TYPE_BITS);
    stream->WriteBits(static_cast<uint32_t>(action), NET_ACTION_BITS);
    if (action == CONTROLLER_ACTION_RELOAD) {
        stream->WriteBits(static_cast<uint32_t>(ammoUid), 16);
        stream->WriteBits(rounds, 8);
    }
    // (The reader tests the role the other way round: a client's fire reaches the server
    // with its aim, the server's goes on without.)
    if (!HasAim(action) && (IsServer() || action <= 1 || action >= 10))
        return;
    C_NetMessage *msg = static_cast<C_NetMessage *>(stream);
    const uint32_t *raw = reinterpret_cast<const uint32_t *>(&dir);
    stream->WriteBits(raw[0], 32);
    stream->WriteBits(raw[1], 32);
    stream->WriteBits(raw[2], 32);
    msg->WriteQuantized(&pos.x, msg->GetSender(-1), 7);
    stream->WriteBits(static_cast<uint32_t>(shotInterval > 0xFF ? 0xFF : shotInterval), 8);
    if (strength == 0.0f) {
        stream->WriteBits(0, 1);
        return;
    }
    stream->WriteBits(1, 1);
    stream->WriteBits(static_cast<uint32_t>(static_cast<int>(static_cast<float>(NET_STRENGTH_MAX) * strength)),
                      NET_STRENGTH_BITS);
}

void C_NetActionMessage::Read(C_BitStream *stream)      // 0x0053b510
{
    action = static_cast<int>(stream->ReadBits(NET_ACTION_BITS));
    if (action == CONTROLLER_ACTION_RELOAD) {
        ammoUid = static_cast<int16_t>(stream->ReadBits(16));
        rounds = static_cast<uint8_t>(stream->ReadBits(8));
    }
    if (!HasAim(action) && (!IsServer() || action <= 1 || action >= 10))
        return;
    C_NetMessage *msg = static_cast<C_NetMessage *>(stream);
    uint32_t *raw = reinterpret_cast<uint32_t *>(&dir);
    raw[0] = stream->ReadBits(32);
    raw[1] = stream->ReadBits(32);
    raw[2] = stream->ReadBits(32);
    msg->ReadQuantized(&pos.x, msg->GetSender(-1), 7);
    shotInterval = static_cast<int>(stream->ReadBits(8));
    if (!stream->ReadBits(1))
        return;
    const float value = static_cast<float>(stream->ReadBits(NET_STRENGTH_BITS)) / static_cast<float>(NET_STRENGTH_MAX);
    if (value < 0.0f) {
        strength = 0.0f;
        return;
    }
    strength = value > 1.0f ? 1.0f : value;
}

void C_NetDiveMessage::Write(C_BitStream *stream)       // 0x0053b6a0
{
    stream->WriteBits(static_cast<uint32_t>(type), NET_MSG_TYPE_BITS);
}

void C_NetBinocularsMessage::Write(C_BitStream *stream)   // 0x0053b6f0
{
    stream->WriteBits(static_cast<uint32_t>(type), NET_MSG_TYPE_BITS);
    stream->WriteBits(on ? 1 : 0, 1);
}

void C_NetBinocularsMessage::Read(C_BitStream *stream)  // 0x0053b740
{
    on = stream->ReadBits(1) != 0;
}

C_NetObjectMessage *__fastcall NetCreateMessage(void * /*controller*/, C_BitStream *stream, bool write)
{
    if (write) {
        stream->WriteBits(NET_MSG_KINDS, NET_MSG_TYPE_BITS);
        return nullptr;
    }
    switch (stream->ReadBits(NET_MSG_TYPE_BITS)) {
    case NET_MSG_ACTION:
        return new C_NetActionMessage;
    case NET_MSG_DIVE:
        return new C_NetDiveMessage;
    case NET_MSG_BINOCULARS:
        return new C_NetBinocularsMessage;
    default:
        return nullptr;
    }
}

void __fastcall NetReadControlBits(void *controller, C_BitStream *stream)
{
    const uint32_t bits = stream->ReadBits(NET_CONTROL_BITS);
    if (controller)
        static_cast<C_HumanController *>(controller)->SetControlBits(bits);
}

void __fastcall NetWriteControlBits(void *controller, C_BitStream *stream)
{
    stream->WriteBits(static_cast<C_HumanController *>(controller)->GetControlBits(), NET_CONTROL_BITS);
}

// ------------------------------------------------------------------------- the replication

void C_PlayerController::UpdateRemote(int frameTime)
{
    // The server's copy of a client's human.
    FlushMessages(true);
    if (IsLocalGame() || g_localPlayerSlot == m_human->GetNetId())
        return;
    m_axes = m_controlAxes;
    m_buttons = m_controlButtons;
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
        C_HumanVisual &visual = m_human->GetMovement()->m_visual;
        visual.SetPosition(pos);
        visual.SetDirection(ToVector4(flat, 0.0f), 0.0f);   // (w: a stale local in the original)
    }
    DecodeControls(m_controlAxes, m_controlButtons, 0, &velocity.x, &flat.x, 0, m_controlBits);
    // The player moved: still active.
    static const int MOVES[] = {0, 1, 2, 3, 8, 9, 10, 11};
    for (int axis : MOVES) {
        if (m_controlAxes[axis] != 0.0f) {
            StampActivity(m_human);
            break;
        }
    }
    ProcessNetMessages();
    if (!InVehicleSeat(m_human->m_vehicle))
        ApplyMoveControls();
    for (float &value : m_controlAxes)
        value = 0.0f;
    for (float &value : m_controlButtons)
        value = 0.0f;
}

void C_PlayerController::TickLocal(int timeDelta)
{
    // The human of this machine's player (or of single player).
    if (!IsLocalGame() && g_localPlayerSlot != m_human->GetNetId()) {
        FlushMessages(false);
        return;
    }
    // The keys of the frame (none yet: cleared ones).
    if (!m_axes || !m_buttons) {
        for (float &value : m_controlAxes)
            value = 0.0f;
        for (float &value : m_controlButtons)
            value = 0.0f;
        m_axes = m_controlAxes;
        m_buttons = m_controlButtons;
    } else {
        for (int i = 0; i < CONTROL_COUNT; i++)
            m_controlAxes[i] = m_axes[i];
        for (int i = 0; i < CONTROL_COUNT; i++)
            m_controlButtons[i] = m_buttons[i];
    }
    if (InVehicleSeat(m_human->m_vehicle))
        ControlVehicle();
    else
        ApplyMoveControls();
    UpdateStamina(static_cast<uint32_t>(timeDelta));
    UpdateWeaponReady(timeDelta);
    // (The server's weapon controls run at its tick.)
    if (!IsServer())
        WeaponControls(timeDelta);
    // The breath held runs out; let go it comes back.
    if (m_holdingBreath) {
        m_breath -= timeDelta;
        if (m_breath < 1) {
            m_breath = 0;
            HoldBreath(false);
        }
    } else if (m_breath < BREATH_FULL) {
        m_breath += timeDelta;
        if (m_breath > BREATH_FULL - 1)
            m_breath = BREATH_FULL;
    }
    // A reload in the scope: the scope's view comes back when it is over.
    if (m_scopeDelay > 0) {
        m_scopeDelay -= timeDelta;
        if (m_scopeDelay < 1) {
            // (Fix: the original reads the scope of a hands item that may be gone.)
            float fov = 0.0f;
            if (m_handsItem && m_handsItem->GetDef()->GetSecondaryComponent())
                fov = static_cast<C_ScopeComp *>(m_handsItem->GetDef()->GetSecondaryComponent())->GetAimedFovFactor();
            GetGame()->GetFirstPersonView()->SetAimView(true, fov);
            m_scopeDelay = 0;
        }
    }
    if (!IsLocalGame()) {
        if (GetGame()->GetRoundState() != ROUND_WARMUP && !m_spectating) {
            const S_vector4 pos = WorldPos4Of(m_human->GetFrame());
            S_vector view = GetViewDir();
            NormalizeBy(view, (view.y * view.y + view.z * view.z) + view.x * view.x);
            C_NetProperty *property = m_netObject->GetProperty(1);
            // (In a vehicle the vehicle replicates the position.)
            if (!InVehicleSeat(m_human->m_vehicle))
                property->GetTrack().SetValue(pos);
            property->GetDirTrack().SetDirection(ToVector4(view, 0.0f), 0.0f);   // (w: a stale local in the original)
            // The rate: the walk's direction at the human's speed.
            S_vector rate = m_human->GetMovement()->GetVelocity();
            NormalizeBy(rate, (rate.y * rate.y + rate.z * rate.z) + rate.x * rate.x);
            const S_vector &velocity = m_human->GetVelocity();
            rate *= MagnitudeBy((velocity.x * velocity.x + velocity.y * velocity.y) + velocity.z * velocity.z);
            property->GetTrack().SetRate(rate);
        }
        if (m_axes && m_buttons)
            m_controlBits = EncodeControlBits(m_axes, m_moveFlags);
        const float seconds = static_cast<float>(static_cast<uint32_t>(timeDelta)) * 0.001f;
        m_netObject->Tick(seconds);
        g_netReplication->Tick30(seconds);
        // A dive.
        if (m_controlButtons[BUTTON_DIVE] != 0.0f && m_human->m_stamina > DIVE_STAMINA_NEEDED &&
            (m_moveFlags & MOVE_MOVING_MASK)) {
            C_NetDiveMessage *message = new C_NetDiveMessage;
            message->fromClient = true;
            m_netObject->QueueMessage(message, g_netReplication->GetLocalId(), true);
        }
    }
    for (float &value : m_controlAxes)
        value = 0.0f;
    for (float &value : m_controlButtons)
        value = 0.0f;
    FlushMessages(false);
}

void C_PlayerController::ProcessNetMessages()
{
    // The server carries a client's actions out and passes them on (marked as the server's);
    // the dive and the binoculars too.
    while (C_NetObjectMessage *message = m_netObject->PopMessage(0)) {
        switch (message->type) {
        case NET_MSG_ACTION:
            if (ServerAction(*static_cast<C_NetActionMessage *>(message))) {
                message->fromClient = false;
                m_netObject->QueueMessage(message, g_netReplication->GetLocalId(), true);
            }
            break;
        case NET_MSG_DIVE:
            m_buttons[BUTTON_DIVE] = 1.0f;
            m_axes[BUTTON_DIVE] = 1.0f;
            message->fromClient = false;
            m_netObject->QueueMessage(message, g_netReplication->GetLocalId(), true);
            break;
        case NET_MSG_BINOCULARS:
            if (!static_cast<C_NetBinocularsMessage *>(message)->on) {
                m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, WORK_ANIMATION_BINOCULARS_AWAY));
            } else {
                C_Item *binoculars = m_human->FindBeltItem(ITEM_BEHAVIOUR_BINOCULARS);
                if (!binoculars)
                    break;
                m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, WORK_ANIMATION_BINOCULARS_OUT,
                                                            FrameArg(binoculars->GetFrame(0))));
            }
            // (Unlike the others, still marked as a client's.)
            message->fromClient = true;
            m_netObject->QueueMessage(message, g_netReplication->GetLocalId(), true);
            break;
        default:
            break;
        }
        ReleaseNetMessage(message);
    }
}

bool C_PlayerController::ServerAction(const C_NetActionMessage &message)
{
    if (m_human->IsDead())
        return false;
    StampActivity(m_human);
    UpdateHandsItem();
    const S_vector4 &pos = message.pos;
    const S_vector4 &dir = message.dir;
    switch (message.action) {
    case CONTROLLER_ACTION_LOWER_WEAPON:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_LOWER_WEAPON));
        return true;
    case CONTROLLER_ACTION_RAISE_WEAPON:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RAISE_WEAPON));
        return true;
    case CONTROLLER_ACTION_FIRE: {
        if (!m_handsItem)
            return false;
        // A client firing faster than the weapon does is refused.
        if (const C_ItemComponent *component = m_handsItem->GetDef()->GetComponent()) {
            const int delay = static_cast<int>(static_cast<const C_WeaponComp *>(component)->GetAttackDelay());
            if (message.shotInterval * 100 < delay)
                return false;
        }
        if (m_handsItem->GetAmount() == 0.0f)
            return false;
        m_handsItem->Fire(m_human, XYZ(pos), XYZ(dir), 0);
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_FIRE));
        return true;
    }
    case CONTROLLER_ACTION_THROW_BELT_GRENADE: {
        C_Item *grenade = m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN);
        if (!grenade)
            return false;
        static_cast<C_WeaponItem *>(grenade)->SetThrowStrength(message.strength);
        UseItemAt(grenade, true, 0, pos, dir);
        return true;
    }
    case CONTROLLER_ACTION_RELOAD:
        if (!m_handsItem)
            return false;
        m_handsItem->SetRounds(m_human, message.ammoUid, message.rounds);
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_RELOAD));
        return true;
    case CONTROLLER_ACTION_THROW:
    case CONTROLLER_ACTION_THROW_2:
        if (!m_handsItem || BehaviourOf(m_handsItem) != ITEM_BEHAVIOUR_THROWN)
            return false;
        static_cast<C_WeaponItem *>(m_handsItem)->SetThrowStrength(message.strength);
        UseItemAt(m_handsItem, message.action == CONTROLLER_ACTION_THROW, 0, pos, dir);
        return true;
    case CONTROLLER_ACTION_8:
        // The knife.
        if (!m_handsItem || BehaviourOf(m_handsItem) != ITEM_BEHAVIOUR_MELEE)
            return false;
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_STAB));
        m_handsItem->Fire(m_human, XYZ(pos), XYZ(dir), 0);
        return true;
    case CONTROLLER_ACTION_MELEE_2:
        if (!m_handsItem || BehaviourOf(m_handsItem) != ITEM_BEHAVIOUR_MELEE)
            return false;
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_STAB_2));
        m_handsItem->FireSecondary(m_human, XYZ(pos), XYZ(dir), 0);
        return true;
    case CONTROLLER_ACTION_EMPTY:
        if (!m_handsItem)
            return false;
        // (Fix: the original plays a weapon's sound of any item in the hands.)
        if (m_handsItem->GetKind() == ITEM_KIND_WEAPON)
            static_cast<C_WeaponItem *>(m_handsItem)->PlayEmptySound(m_human);
        return true;
    case CONTROLLER_ACTION_START_THROW:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_THROW));
        return true;
    case CONTROLLER_ACTION_START_THROW_2:
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_THROW_2));
        return true;
    case CONTROLLER_ACTION_START_BELT_THROW: {
        C_Item *grenade = m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN);
        if (!grenade)
            return false;
        m_human->ExecuteBodyCommand(
            MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_THROW_BELT_GRENADE, FrameArg(grenade->GetFrame(0))));
        return true;
    }
    default:
        // (The others go on unchecked.)
        return true;
    }
}

// ------------------------------------------------------------------------- requests

void C_PlayerController::RequestAction(int action, const S_vector4 *pos, const S_vector4 *dir, float strength,
                                       int ammoUid, int rounds)
{
    switch (action) {
    case CONTROLLER_ACTION_FIRE:
        // Single player: a shot after a quiet time tells the squad.
        if (IsLocalGame() && m_quietTime > 500)
            GetSquadCommander()->OnPlayerNoise();
        m_raisedTime = RAISED_TIME_AFTER_USE;
        m_human->SetDisguise(DISGUISE_LOSS_SHOT);
        break;
    case CONTROLLER_ACTION_RELOAD:
        // A reload in the sights or the scope leaves them until it is over.
        if (m_viewMode != VIEW_NORMAL) {
            const float fov = GetGame()->GetCutsceneFOV();
            GetGame()->GetFirstPersonView()->SetAimView(false, fov);
            m_scopeDelay = static_cast<C_WeaponComp *>(m_handsItem->GetDef()->GetComponent())->GetReloadTimeMs() - 200;
        }
        m_raisedTime = RAISED_TIME_AFTER_USE;
        break;
    case CONTROLLER_ACTION_RAISE_WEAPON:
        m_raisedTime = RAISED_TIME_AFTER_USE;
        break;
    case CONTROLLER_ACTION_8:
    case CONTROLLER_ACTION_MELEE_2:
    case CONTROLLER_ACTION_START_THROW:
    case CONTROLLER_ACTION_START_THROW_2:
    case CONTROLLER_ACTION_START_BELT_THROW:
        m_human->SetDisguise(DISGUISE_LOSS_ACTION);
        break;
    default:
        break;
    }
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

void C_PlayerController::IssueCommand(const S_HumanCommand &command)
{
    // The server carries it out (OnMessage2).
    S_MsgCommand *message = new S_MsgCommand;
    message->command = command.type;
    message->actor = command.param1;
    message->arg = command.param2;
    SendToServer(message);
}

void C_PlayerController::SendActionCommand(const S_ActionCommand &command)
{
    S_MsgAction *message = new S_MsgAction;
    message->pos = WorldPos4Of(m_human->GetBodyFrame());
    message->command.category = command.category;
    message->command.dir = command.dir;
    message->command.action = command.action;
    message->command.command = command.command;
    message->command.actor = command.actor;
    message->command.value = command.value;
    message->command.member = command.member;
    SendToServer(message);
}

void C_PlayerController::OnUniformChanged(int uid)
{
    S_MsgPutOnUniform *message = new S_MsgPutOnUniform;
    message->uid = uid;
    SendToServer(message);
}

void C_PlayerController::RequestTakeFromBox(int boxId, int index)
{
    // (The original stores CTRL_MSG_PICK_UP_ITEM as the message's type; its Write sends
    // CTRL_MSG_TAKE_FROM_BOX regardless.)
    S_MsgTakeFromBox *message = new S_MsgTakeFromBox;
    message->box = boxId;
    message->item = index;
    message->uid = -1;
    message->container = 7;                             // none: the server picks it
    SendToServer(message);
}

void C_PlayerController::RequestPickUpItem(int actorId, int container)
{
    S_MsgPickUpItem *message = new S_MsgPickUpItem;
    message->uid = actorId;
    message->container = container;
    SendToServer(message);
}

bool C_PlayerController::RequestMoveItem(int uid, int otherId, int fromContainer, int toContainer, int slot)
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

bool C_PlayerController::RequestTransferItem(int uid, int arg, int otherId, int otherUid, int container)
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

void C_PlayerController::RequestThrowItem(int uid, const S_vector &velocity)
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

void C_PlayerController::RequestEquipment(int humanId)
{
    S_Msg21 *message = new S_Msg21;
    message->value = humanId;
    SendToServer(message);
}

void C_PlayerController::RequestDamage(int type, int damage, int attackerId, int zone)
{
    S_Msg17 *message = new S_Msg17;
    message->value08 = type;
    message->value0C = zone;
    message->value10 = damage;
    message->value14 = attackerId;
    SendToServer(message);
}

void C_PlayerController::SendSoldierClass(int soldierClass)
{
    S_MsgSoldierClass *message = new S_MsgSoldierClass;
    message->soldierClass = soldierClass;
    SendToClients(message);
}

bool C_PlayerController::OnCollisionEx(C_Actor *other, const S_vector * /*pos*/, int, int, int, int, int)
{
    // A client's human touching a ladder asks the server to use it.
    if (other->GetType() == ACTOR_LADDER && IsClient()) {
        S_MsgUseObject *message = new S_MsgUseObject;
        message->actor = other->GetNetId();
        SendToServer(message);
    }
    return true;
}

// ------------------------------------------------------------------------- controller messages

void C_PlayerController::FlushMessages(bool toClients)
{
    // The control state goes unreliably, the rest reliably; types 5, 18 (the scripted
    // animations) and above 23 are not sent.
    std::vector<S_ControllerMessage *> &queue = toClients ? m_toClients : m_toServer;
    C_BitStream *unreliable = nullptr;
    C_BitStream *reliable = nullptr;
    const int humanId = m_human->GetNetId();
    for (S_ControllerMessage *message : queue) {
        if (message->type == 5 || message->type == CTRL_MSG_ANIMATION || message->type > CTRL_MSG_23)
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

void C_PlayerController::OnMessage(C_BitStream *stream)
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
            m_human->ClientPutOnUniform(message.uid);
            break;
        }
        case CTRL_MSG_COMMAND: {
            S_MsgCommand message;
            message.Read(msg);
            if (!IsServer()) {
                S_HumanCommand command;
                command.category = HUMAN_COMMAND_VEHICLE;
                command.type = message.command;
                command.param1 = message.actor;
                command.param2 = message.arg;
                ExecuteCommand(command);
            }
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
            m_human->GetMovement()->UseObject(g_actorsModule.GetActorById(message.actor));
            break;
        }
        case CTRL_MSG_MOVE_ITEM: {
            S_MsgMoveItem message;
            message.Read(msg);
            m_human->ClientMoveItem(message.uid, message.other, message.fromContainer, message.toContainer,
                                    message.slot);
            if (message.fromContainer == ITEM_CONTAINER_HANDS)
                m_raisedTime = RAISED_TIME_AFTER_USE;
            break;
        }
        case CTRL_MSG_TRANSFER_ITEM: {
            S_MsgTransferItem message;
            message.Read(msg);
            m_human->ClientTransferItem(message.uid, message.arg, message.other, message.otherUid,
                                        message.container);
            if (message.arg == ITEM_CONTAINER_HANDS || message.container == ITEM_CONTAINER_HANDS)
                m_raisedTime = RAISED_TIME_AFTER_USE;
            break;
        }
        case CTRL_MSG_PICK_UP_ITEM: {
            S_MsgPickUpItem message;
            message.Read(msg);
            m_human->ClientPickUpItem(message.uid, message.container);
            if (message.container == ITEM_CONTAINER_HANDS)
                m_raisedTime = RAISED_TIME_AFTER_USE;
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
            break;
        }
        case CTRL_MSG_SOLDIER_CLASS: {
            S_MsgSoldierClass message;
            message.Read(msg);
            ApplySoldierClass(m_human, message.soldierClass);
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
            // (Types 5, 17, 18 and 21 are not read: the next type is read at once, out of
            // step with the stream.)
            break;
        }
    }
}

void C_PlayerController::OnMessage2(C_BitStream *stream)
{
    // The server: a client's requests for its human. What the server carries out goes on to
    // the clients.
    C_NetMessage &msg = *static_cast<C_NetMessage *>(stream);
    const bool network = !IsLocalGame();
    for (;;) {
        // The requests that end here note the player's activity.
        bool activity = false;
        switch (static_cast<int>(msg.ReadBits(CTRL_MSG_TYPE_BITS))) {
        case CTRL_MSG_CONTROLS: {
            S_MsgControls *message = new S_MsgControls;
            message->Read(msg);
            // (The original also asks 0x005ac540, which always says yes.)
            if (g_localPlayerSlot != m_human->GetNetId()) {
                for (int i = 0; i < CONTROL_COUNT; i++)
                    m_controlAxes[i] = message->axes[i];
                for (int i = 0; i < CONTROL_COUNT; i++)
                    m_controlButtons[i] = message->buttons[i];
                SendToClients(message);
            } else {
                delete message;
            }
            break;
        }
        case CTRL_MSG_PUT_ON_UNIFORM: {
            S_MsgPutOnUniform *message = new S_MsgPutOnUniform;
            message->Read(msg);
            if (m_human->PutOnUniform(message->uid) && network)
                SendToClients(message);
            else
                delete message;
            break;
        }
        case CTRL_MSG_COMMAND: {
            S_MsgCommand *message = new S_MsgCommand;
            message->Read(msg);
            S_HumanCommand command;
            command.category = HUMAN_COMMAND_VEHICLE;
            command.type = message->command;
            command.param1 = message->actor;
            command.param2 = message->arg;
            if (ExecuteCommand(command) && network)
                SendToClients(message);
            else
                delete message;
            activity = true;
            break;
        }
        case CTRL_MSG_CONTROL_STATE: {
            // (Fix: the original allocates the message and leaks it, or reads through null
            // when the allocation fails.)
            S_MsgControlState message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_USE_OBJECT: {
            S_MsgUseObject *message = new S_MsgUseObject;
            message->Read(msg);
            m_human->GetMovement()->UseObject(g_actorsModule.GetActorById(message->actor));
            if (network)
                SendToClients(message);
            else
                delete message;                         // (Fix: the original leaks it in a local game)
            break;
        }
        case CTRL_MSG_MOVE_ITEM: {
            S_MsgMoveItem *message = new S_MsgMoveItem;
            message->Read(msg);
            const bool moved = m_human->ServerMoveItem(message->uid, message->other, message->fromContainer,
                                                       &message->toContainer, message->slot);
            // (Fix: the original reads the container from the message after freeing it.)
            const bool fromHands = message->fromContainer == ITEM_CONTAINER_HANDS;
            if (moved && network)
                SendToClients(message);
            else
                delete message;
            if (fromHands)
                m_raisedTime = RAISED_TIME_AFTER_USE;
            activity = true;
            break;
        }
        case CTRL_MSG_TRANSFER_ITEM: {
            S_MsgTransferItem *message = new S_MsgTransferItem;
            message->Read(msg);
            const bool moved = m_human->ServerTransferItem(message->uid, message->arg, message->other,
                                                           message->otherUid, message->container);
            // (Fix: the original reads the containers from the message after freeing it.)
            const bool hands = message->arg == ITEM_CONTAINER_HANDS || message->container == ITEM_CONTAINER_HANDS;
            if (moved && network)
                SendToClients(message);
            else
                delete message;
            if (hands)
                m_raisedTime = RAISED_TIME_AFTER_USE;
            activity = true;
            break;
        }
        case CTRL_MSG_PICK_UP_ITEM: {
            S_MsgPickUpItem *message = new S_MsgPickUpItem;
            message->Read(msg);
            const bool picked = m_human->PickUpItem(message->uid, &message->container);
            // (Fix: the original reads the container from the message after freeing it.)
            const bool toHands = message->container == ITEM_CONTAINER_HANDS;
            if (picked && network)
                SendToClients(message);
            else
                delete message;
            if (toHands)
                m_raisedTime = RAISED_TIME_AFTER_USE;
            activity = true;
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
            // (Also queued in a local game, unlike the others.)
            if (m_human->TakeFromSupplyBox(message->box, message->item, &message->container, &message->uid))
                SendToClients(message);
            else
                delete message;
            activity = true;
            break;
        }
        case CTRL_MSG_ACTION: {
            S_MsgAction *message = new S_MsgAction;
            message->Read(msg);
            if (OnSquadCommand(*message))
                SendToClients(message);
            else
                delete message;
            activity = true;
            break;
        }
        case CTRL_MSG_REMOTE_REQUEST: {
            S_MsgRemoteRequest *message = new S_MsgRemoteRequest;
            message->Read(msg);
            m_human->GetMovement()->OnRemoteAction(message->request);
            SendToClients(message);
            break;
        }
        case CTRL_MSG_SOLDIER_CLASS: {
            S_MsgSoldierClass message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_17: {
            // RequestDamage.
            S_Msg17 message;
            message.Read(msg);
            const S_vector zero(0.0f, 0.0f, 0.0f);
            m_human->Hit(static_cast<E_HitType>(message.value08), static_cast<float>(message.value10), zero, zero,
                         g_actorsModule.GetActor(message.value14), message.value0C);
            break;
        }
        case CTRL_MSG_21: {
            // RequestEquipment: the human's equipment goes to the clients.
            S_Msg21 message;
            message.value = -1;
            message.Read(msg);
            S_MsgEquipment *equipment = new S_MsgEquipment;
            equipment->human = message.value;
            SendToClients(equipment);
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
            // (Types 1, 5, 13, 14, 18, 19 and 20 are not read: the next type is read at once,
            // out of step with the stream.)
            break;
        }
        if (activity && network && IsServer())
            StampActivity(m_human);
    }
}

void C_PlayerController::HandleMessage(C_BitStream *stream)
{
    // A client: the server's messages about a human that is not here (yet), read and dropped.
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
            S_MsgInventoryCheck message;              // (no human: nothing is checked)
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
        case CTRL_MSG_ANIMATION: {
            S_MsgAnimation message;
            message.Read(msg);
            break;
        }
        case CTRL_MSG_SAY: {
            S_MsgSay message;
            message.Read(msg);
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
            // (Types 5, 17 and 21 are not read.)
            break;
        }
    }
}

void C_PlayerController::HandleMessage2(C_BitStream *stream)
{
    // The server: a client's requests for a human that is not here (any more), read and
    // dropped.
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
        case CTRL_MSG_21: {
            S_Msg21 message;
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
            // (Types 5, 18, 19 and 20 are not read.)
            break;
        }
    }
}
