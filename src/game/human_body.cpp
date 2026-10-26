// S_HumanBody, the body part of a human: creates and drives its movement, work and equipment
// objects (original source 0x005ab330..0x005ac4c0; the objects themselves are declared in
// human_body.h and not ported yet).
#include "game/human.h"

#include "common/chunk.h"
#include "game/game.h"
#include "game/human_anim_tables.h"
#include "game/human_body.h"
#include "game/human_internal.h"
#include "game/pending.h"
#include "game/world.h"

namespace {

// Saved-game chunks of the body.
enum : uint16_t {
    CHUNK_BODY_STATE = 2000,
    CHUNK_BODY_WORK = 0x7D1,
    CHUNK_BODY_MOVEMENT = 0x7D2,
    CHUNK_BODY_EQUIPMENT = 0x7D3,
};

// Movement flags (C_HumanMovement::GetFlags()).
enum : uint32_t {
    MOVEMENT_CARRY_ONE_HAND = 0x2,
    MOVEMENT_CARRY_TWO_HANDS = 0x4,
    MOVEMENT_CARRY_MASK = 0x6,
    MOVEMENT_FROZEN = 0x8,
};

// Work animations.
enum : int {
    WORK_STANCE_CRIPPLED = 3,
    WORK_ANIMATION_DEATH = 0x71,
    WORK_ANIMATION_6A = 0x6A,
};

// The head's looks (C_HumanWeaponPose LOOK_* flags) of S_HumanBody::SetPose(): 0 none; glancing
// round once / nervously, on and on; glancing right / left once; looking at a point once /
// nervously (7, 8: the point passed on).
const uint32_t POSE_LOOK_FLAGS[] = {0, 0x86, 0x92, 0x06, 0x12, 0xC2, 0xA2, 0x85, 0x91};

}  // namespace

bool S_HumanBody::Create(C_Human *human, int bodyGroup)
{
    owner = human;
    group = bodyGroup;
    work = new C_HumanWork();
    movement = new C_HumanMovement();
    equipment = new C_HumanEquipment(owner);
    work->SetMovement(movement);
    work->SetGroup(group);
    movement->SetOwner(owner);
    movement->Init();
    equipment->Init();
    work->SetLook(0, nullptr);
    crippled = false;
    // Network games: only the humans this machine simulates (or controls) push others.
    if (!IsLocalGame() && g_localPlayerSlot != owner->GetNetId() && !owner->IsServerSimulated())
        movement->SetUnknown18C(false);
    return true;
}

bool S_HumanBody::Destroy()
{
    equipment->Done();
    movement->Done();
    delete work;
    work = nullptr;
    delete movement;
    movement = nullptr;
    delete equipment;
    equipment = nullptr;
    owner = nullptr;
    return true;
}

void S_HumanBody::Reset()
{
    crippled = false;
    work->Reset();
    equipment->Reset();
    movement->Reset();
}

bool S_HumanBody::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_BODY_STATE);
    chunk.Write(crippled);
    chunk.Ascend();
    chunk.Begin(CHUNK_BODY_WORK);
    work->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_BODY_MOVEMENT);
    movement->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_BODY_EQUIPMENT);
    equipment->SaveGame(chunk);
    chunk.Ascend();
    return true;
}

bool S_HumanBody::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_BODY_STATE:
            if (chunk.RemainingSize() >= 1)
                chunk.Read(crippled);
            chunk.Ascend();
            break;
        case CHUNK_BODY_WORK:
            work->LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_BODY_MOVEMENT:
            movement->LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_BODY_EQUIPMENT:
            equipment->LoadGame(chunk);
            chunk.Ascend();
            break;
        default:
            // (The original does not leave unknown chunks.)
            break;
        }
    }
    return true;
}

void S_HumanBody::Update(int frameTime)
{
    if ((owner->IsLocalPlayer() && IsClient()) || !IsServer())
        movement->UpdateAnimation(frameTime);
    movement->UpdatePhysics(frameTime);
    equipment->Update(frameTime);
}

void S_HumanBody::Tick(int timeDelta)
{
    if (!IsClient()) {
        movement->UpdateAnimation(timeDelta);
        equipment->Update(timeDelta);
        return;
    }
    if (!owner->IsLocalPlayer())
        movement->UpdateAnimation(timeDelta);
}

void S_HumanBody::SetFacingDir(const S_vector &dir)
{
    work->SetFacingDir(dir, true);
}

void S_HumanBody::SetLookAt(const S_vector4 &target)
{
    work->SetLookAt(target, true);
}

void S_HumanBody::SetViewDir(const S_vector &dir)
{
    work->SetViewDir(dir);
}

uint32_t S_HumanBody::TakeFlags()
{
    return work->TakeFlags();
}

void S_HumanBody::ApplyHit(uint32_t flags, const S_vector &pos, const S_vector &force)
{
    work->ApplyHit(flags, pos, force);
}

void S_HumanBody::Die(C_Actor *killer)
{
    work->Die(killer);
}

void S_HumanBody::PlayDeath()
{
    work->SetUnknownB0(true);
    work->Play(WORK_ANIMATION_DEATH, true);
    work->SetUnknownB0(false);
}

void S_HumanBody::Cripple()
{
    crippled = true;
    work->SetGait(WORK_STANCE_CRIPPLED, true);
}

void S_HumanBody::Ragdoll()
{
    movement->Ragdoll(true, true);
}

void S_HumanBody::ResetMovement()
{
    movement->ResetPushState();
}

// ------------------------------------------------------------------------- commands

void S_HumanBody::Execute(const S_BodyCommand &command)
{
    switch (command.kind) {
    case BODY_COMMAND_PLAY:
        work->Play(command.arg0, true);
        break;
    case BODY_COMMAND_WORK_ANIMATION:
        PlayWorkAnimation(command.arg0, reinterpret_cast<I3D_frame *>(static_cast<intptr_t>(command.arg1)));
        break;
    case BODY_COMMAND_STANCE:
        // Crippled humans keep limping.
        work->SetGait(crippled ? WORK_STANCE_CRIPPLED : command.arg0, true);
        break;
    case BODY_COMMAND_3:
        work->SetStance(command.arg0, true);
        break;
    case BODY_COMMAND_ACTION:
        DoAction(command.arg0, reinterpret_cast<I3D_frame *>(static_cast<intptr_t>(command.arg1)), command.arg2);
        break;
    case BODY_COMMAND_POSE:
        SetPose(command.arg0, command.data);
        break;
    case BODY_COMMAND_INTERACT:
        Interact(static_cast<C_Actor *>(command.arg2), static_cast<uint32_t>(command.arg0), command.arg1);
        break;
    default:
        break;
    }
}

void S_HumanBody::Play(int animation)
{
    work->Play(animation, true);
}

void S_HumanBody::SetStance(int stance)
{
    work->SetGait(crippled ? WORK_STANCE_CRIPPLED : stance, true);
}

float S_HumanBody::GetSpeed() const
{
    return movement->GetVelocity().Magnitude();
}

bool S_HumanBody::CanRespawn() const
{
    return !movement->IsRespawnBlocked();
}

void S_HumanBody::PlayWorkAnimation(int animation, I3D_frame *frame)
{
    if (frame)
        work->SetWorkingFrame(frame, true);
    work->PlayWorkAnimation(animation, true);
}

void S_HumanBody::DoAction(int action, I3D_frame *frame, void *arg)
{
    switch (action) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
        work->SetWorkingFrame(frame, true);
        work->DoAction(action, true);
        break;
    case 13:
    case 14:
        work->SetWorkingFrame2(frame, true);
        work->DoAction(action, true);
        break;
    case 20:
        work->SetUnknown5C(true, true);
        break;
    case 21:
        work->SetUnknown5C(false, true);
        break;
    case 22:
        work->DoAction(-1, true);
        break;
    case 30: {
        C_Human *human = movement->GetOwner();
        if (IsLocalGame() || human->IsLocalPlayer() || human->IsServerSimulated())
            human->GetWork()->Play(WORK_ANIMATION_6A, true);
        break;
    }
    case 31:
        movement->UseObject(static_cast<C_Actor *>(arg));
        break;
    default:
        break;
    }
}

void S_HumanBody::SetPose(int pose, const void *arg)
{
    if (pose < 0 || pose > 8)
        return;
    // Poses 7 and 8 pass their argument on.
    work->SetLook(POSE_LOOK_FLAGS[pose], pose >= 7 ? static_cast<const S_vector4 *>(arg) : nullptr);
}

void S_HumanBody::Interact(C_Actor *target, uint32_t interaction, int arg)
{
    if (IsLocalGame()) {
        work->Interact(target, interaction, arg, true);
        if ((work->GetUnknownC8() & WORK_REQUEST_INTERACT) == WORK_REQUEST_INTERACT)
            return;
    } else {
        movement->Interact(target, interaction, arg);
    }
    switch (interaction & INTERACT_CODE_MASK) {
    case INTERACT_ENTER_CAR:
    case INTERACT_ENTER_SUBMARINE:
    case INTERACT_ENTER_STATIC_WEAPON:
    case INTERACT_ENTER_JUNKERS88:
    case INTERACT_ENTER_SEAT_D:
        // The camera of the controlled soldier eases into the new position.
        if (IsClient() && g_teamManager.GetPlayerTeam()->GetActivePlayer() == owner)
            GetGame()->GetCameraModule()->SetBlendTime(500);
        break;
    default:
        break;
    }
}

void S_HumanBody::PlayAnimation(const char *name, int a, int b, int c)
{
    movement->PlayAnimation(name, a, b, c);
}

// -------------------------------------------------------------------------- state

void S_HumanBody::SetPlayerControlled(bool on)
{
    movement->SetPlayerControlled(on);
}

void S_HumanBody::SetModelVisible(bool on)
{
    movement->SetModelVisible(on);
}

void S_HumanBody::SetCarryMode(int mode)
{
    switch (mode) {
    case 0:
        movement->ClearFlags(MOVEMENT_CARRY_MASK);
        break;
    case 1:
        movement->ClearFlags(MOVEMENT_CARRY_MASK);
        movement->OrFlags(MOVEMENT_CARRY_ONE_HAND);
        break;
    case 2:
        movement->ClearFlags(MOVEMENT_CARRY_MASK);
        movement->OrFlags(MOVEMENT_CARRY_TWO_HANDS);
        break;
    default:
        break;
    }
}

void S_HumanBody::Freeze(bool on)
{
    if (!on) {
        movement->ClearFlags(MOVEMENT_FROZEN);
        return;
    }
    if ((movement->GetFlags() & MOVEMENT_FROZEN) == 0)
        movement->OnFrozen(true);
    movement->OrFlags(MOVEMENT_FROZEN);
}

void S_HumanBody::RestoreCollision(bool unknown)
{
    movement->RestoreCollision(unknown);
}

void S_HumanBody::RemoveCollision(bool unknown)
{
    movement->RemoveCollision(unknown);
}

void S_HumanBody::SetMovementFlags(uint32_t flags)
{
    if ((movement->GetFlags() & MOVEMENT_FROZEN) == 0 && (flags & MOVEMENT_FROZEN) != 0)
        movement->OnFrozen(true);
    movement->OrFlags(flags);
}

void S_HumanBody::ClearMovementFlags(uint32_t flags)
{
    movement->ClearFlags(flags);
}

uint32_t S_HumanBody::GetMovementStance() const
{
    return movement->GetFlags() & 7;
}

int S_HumanBody::GetStance() const
{
    return work->GetStance();
}

int S_HumanBody::GetWorkState() const
{
    return work->GetGait();
}

int S_HumanBody::GetClimbState() const
{
    const int state = movement->GetState();
    if (state >= 0x6A && state <= 0x6C)
        return 1;
    if (state == 8 || state == 0x0E)
        return 2;
    return 0;
}

int S_HumanBody::GetClimbTarget() const
{
    const int state = movement->GetState();
    if ((state >= 0x6A && state <= 0x6C) || state == 8 || state == 0x0E)
        return movement->GetUnknown990();
    return 0;
}

bool S_HumanBody::IsWorkState5() const
{
    return movement->GetOwner()->GetWork()->GetUnknownB8() == 5;
}

float S_HumanBody::GetVisibilityFactor() const
{
    return movement->GetVisibilityFactor();
}

float S_HumanBody::GetLightLevel() const
{
    return movement->GetLightLevel();
}

bool S_HumanBody::GetEquipmentFlag1() const
{
    return (equipment->GetFlags() & EQUIPMENT_HANDS_PUT_AWAY) != 0;
}

bool S_HumanBody::IsLeftFootForward() const
{
    return movement->IsLeftFootForward();
}

const uint32_t *S_HumanBody::GetGround() const
{
    return movement->GetGround();
}

void S_HumanBody::Unknown5ac470(int arg)
{
    if (owner->GetCarried())
        equipment->Unknown7938f0(0);
    movement->Unknown7865c0(arg);
}

I3D_frame *S_HumanBody::GetUnknown6E0(int *out) const
{
    *out = movement->GetUnknown698();
    return movement->GetUnknown6E0();
}

I3D_sector *S_HumanBody::GetSector() const
{
    return movement->GetSector();
}

int S_HumanBody::GetActivityAnimLength(int id, int variant)
{
    return ::GetActivityAnimLength(id, variant);
}

int S_HumanBody::GetCarryAnimLength(int id, int variant, int carryMode)
{
    return ::GetCarryAnimLength(id, variant, carryMode);
}
