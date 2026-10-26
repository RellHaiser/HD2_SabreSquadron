// C_Human: the full state sent to a joining network client (C_ActorsModule::WriteState /
// ReadState; original source 0x005a9c40..0x005aa780).
#include "game/human.h"

#include "game/actors_module.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "net/bitstream.h"

namespace {

// Body commands that bring a joining client's copy into the same pose.
constexpr int WORK_ANIMATION_SURRENDER = 0x34;
constexpr int WORK_ANIMATION_UNCONSCIOUS = 0x62;

S_BodyCommand MakeBodyCommand(int kind, int arg0)
{
    S_BodyCommand command;
    command.unknown04 = 1;
    command.kind = kind;
    command.arg0 = arg0;
    return command;
}

}  // namespace

void C_Human::WriteState(C_BitStream *stream)
{
    C_NetMessage *msg = static_cast<C_NetMessage *>(stream);
    bool seatWritten = true;
    if (!m_vehicle) {
        msg->WriteBits(0, 1);
    } else {
        switch (m_vehicle->GetType()) {
        case ACTOR_CAR:
        case ACTOR_SUBMARINE:
        case ACTOR_STATIC_WEAPON:
        case ACTOR_JUNKERS88:
            msg->WriteBits(1, 1);
            msg->WriteBits(static_cast<uint32_t>(m_vehicle->GetNetId()), 10);
            // (Only cars and static weapons send the seat; ReadState always reads one.)
            if (m_vehicle->GetType() == ACTOR_CAR || m_vehicle->GetType() == ACTOR_STATIC_WEAPON)
                msg->WriteBits(static_cast<uint32_t>(m_vehicle->FindSeat(this)), 5);
            else
                seatWritten = false;
            break;
        default:
            msg->WriteBits(0, 1);
            break;
        }
    }
    (void)seatWritten;
    if (m_role == HUMAN_ROLE_PLAYER)
        return;

    S_SoldierState state;
    GetSoldierState(state);
    NetWriteSoldierState(msg, state, false);
    if (m_stats[STAT_HEALTH] < 1)
        msg->WritePosition(reinterpret_cast<const S_vector &>(m_position));
    msg->WriteBits(IsAISuspended() ? 1 : 0, 1);
    if (m_faceTexture.IsEmpty()) {
        msg->WriteBits(0, 1);
    } else {
        msg->WriteBits(1, 1);
        msg->WriteString(m_faceTexture);
    }
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI) {
        // The animation the script set, with its frame.
        const C_ScriptedController *scripted = GetScriptedController();
        C_str animation;
        int frame = 0;
        if (scripted->GetAnimationName()[0] != '\0') {
            animation = scripted->GetAnimationName();
            frame = scripted->GetAnimationFrame();
        }
        msg->WriteBits(1, 1);
        msg->WriteString(animation);
        msg->WriteBits(static_cast<uint32_t>(frame), 16);
    } else {
        msg->WriteBits(0, 1);
    }
    msg->WriteBits(m_controller->IsWeaponRaised() ? 1 : 0, 1);
    msg->WriteBits(IsSurrendered() ? 1 : 0, 1);
    msg->WriteBits(IsUnconscious() ? 1 : 0, 1);
}

void C_Human::ReadState(C_BitStream *stream)
{
    C_NetMessage *msg = static_cast<C_NetMessage *>(stream);
    if (msg->ReadBits(1)) {
        const int vehicleId = static_cast<int>(msg->ReadBits(10));
        const int seat = static_cast<int>(msg->ReadBits(5));
        if (C_Actor *vehicle = g_actorsModule.GetActorById(vehicleId)) {
            vehicle->SetSeatBlocked(seat, true);
            S_BodyCommand command = MakeBodyCommand(BODY_COMMAND_INTERACT, INTERACT_FLAG_BODY | INTERACT_ENTER_CAR);
            command.arg1 = seat;
            command.arg2 = vehicle;
            ExecuteBodyCommand(command);
        }
    }
    if (m_role == HUMAN_ROLE_PLAYER)
        return;

    S_SoldierState state;
    NetReadSoldierState(msg, state, false);
    m_stats[STAT_HEALTH] = state.health;
    if (state.health < 1) {
        S_vector pos;
        msg->ReadPosition(pos);
        bool keepPosition = false;
        if (m_vehicle) {
            switch (m_vehicle->GetType()) {
            case ACTOR_CAR:
            case ACTOR_SUBMARINE:
            case ACTOR_STATIC_WEAPON:
            case ACTOR_JUNKERS88:
                keepPosition = true;
                break;
            default:
                break;
            }
        }
        if (!keepPosition) {
            m_position = {pos.x, pos.y, pos.z, m_position.w};
            m_frame->SetPosInline(pos);
        }
        // Dead: the body lies there.
        ApplyRemoteHit(HIT_SCRIPT_KILL_EX, 0, 0, -1, S_vector(0, 0, 0), S_vector(0, 0, 0));
    }
    ClearActorContainers(this);
    Setup(state, m_role, m_group, nullptr, m_netId);
    Freeze(msg->ReadBits(1) == 1);
    if (msg->ReadBits(1) == 1) {
        char face[100];
        msg->ReadString(face, sizeof(face));
        SetTexture(face);
    }
    if (msg->ReadBits(1)) {
        char animation[0xFF];
        msg->ReadString(animation, sizeof(animation));
        const int frame = static_cast<int16_t>(msg->ReadBits(16));
        PlayAnimation(animation, 0, frame, 1);
    }
    if (msg->ReadBits(1) == 0)
        m_controller->LowerWeapon();
    else
        m_controller->RaiseWeapon();
    if (msg->ReadBits(1))
        ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, WORK_ANIMATION_SURRENDER));
    if (msg->ReadBits(1))
        ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_PLAY, WORK_ANIMATION_UNCONSCIOUS));
}
