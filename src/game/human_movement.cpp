// The movement of a human: lifecycle, saved games, the per-frame updates and the network replay of
// moves (original source 0x00784430..0x0078a0b0).
#include "game/human_movement.h"

#include <windows.h>

#include <cmath>
#include <cstring>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_body.h"
#include "game/human_controller.h"
#include "game/human_anim_tables.h"
#include "game/human_equipment.h"
#include "game/human_internal.h"
#include "game/pending.h"
#include "game/shots.h"
#include "game/surface_marks.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// Save game chunk of the movement (the work, the base and the parts are inside).
constexpr uint16_t CHUNK_MOVEMENT = 0x29A;

// Frames of the model the scene keeps for humans (I3D_scene::AddHumanModel).
const char *const SCENE_FRAME_Q = "q";
const char *const SCENE_FRAME_Z = "z";

// Number of humans initialised in the mission (the first / last one sets up / frees the shared
// footprint marks).
int g_humansInMission = 0;                              // 0x0098a6a0
bool g_cameraUnderWater = false;                        // 0x0098a6ac
// Per mission a variant number (1: Brest, Norway, Sicily 1; 2: Prole, Burma, Czech, Sicily 2).
bool g_unknown84f418 = false;                           // 0x0084f418
// Lighting of the bodies on snow missions (C_HumanLighting::m_climate).
float g_missionClimate = 0.0f;                          // 0x0084f410

// The steady turn speed (rad/ms) of the humans not controlled here (C_HumanLocomotion::
// m_turnSpeed; 0.006 for the local player).
constexpr float TURN_SPEED_OTHERS = 0.008f;         // 0x0084ea04

// I3D driver state that the multiplayer games turn off (not identified).
constexpr int DRIVER_STATE_2B = 0x2B;

// Anims that put the body lower (lying, sitting, being carried; the light is sampled at the
// pelvis then, 0.8 m higher otherwise).
bool IsLoweredState(int workMode, int anim, int seatKind)
{
    return workMode == 2 || workMode == 3 || anim == 0x9F || anim == 0x9E || anim == 0x9B || anim == 0x6F ||
           anim == 0x22 || seatKind > 0;
}

bool StartsWith(const C_str &text, const char *prefix)
{
    return _strnicmp(text, prefix, std::strlen(prefix)) == 0;
}

// Microseconds of the performance counter (the update profiling).
int ProfileClock()
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return static_cast<int>(static_cast<uint64_t>(counter.QuadPart) * 1000000 / static_cast<uint64_t>(frequency.QuadPart));
}

// A car the human sits in that has burnt out (no light sampling, a dark model).
bool InBurntCar(const C_HumanMovement &movement)
{
    if (movement.m_seatMode < 1 || movement.m_seatMode > 7 || !movement.m_vehicle)
        return false;
    if (movement.m_vehicle->GetType() != ACTOR_CAR)
        return false;
    return static_cast<C_Car *>(movement.m_vehicle)->IsDestroyed();
}

// Joint of the light samples.
const I3D_frame *LightJoint(const C_HumanMovement &movement) { return movement.m_joints[HUMAN_JOINT_A].frame; }

}  // namespace

int g_missionVariant = 0;                               // 0x0098a6b0

// Per mission: humans breathe vapour (cold missions).
bool g_coldMission = false;                             // 0x0098a6ad

float g_footprintBrightness = 1.0f;

// ------------------------------------------------------------------ animation references

void S_HumanAnimRef::Normalize()
{
    // Ids that name their stance.
    if (anim > 0x19 && anim < 0x22) {
        switch (anim) {
        case 0x1A:
        case 0x20:
            stance = HUMAN_STANCE_STAND;
            break;
        case 0x1B:
        case 0x1E:
            stance = HUMAN_STANCE_CROUCH;
            break;
        case 0x1F:
        case 0x21:
            stance = HUMAN_STANCE_PRONE;
            break;
        default:
            break;
        }
    }
    // The stand (0x2D..0x36) / crouch (0x37..0x3E) / prone (0x3F..0x41) groups: the base id of
    // the stance (the second of each group is the "aiming" one, inferred).
    if (anim > 0x2C && anim < 0x42) {
        if (stance == HUMAN_STANCE_STAND) {
            if (anim > 0x36) {
                if (anim != 0x38 && anim != 0x40) {
                    anim = 0x2D;
                    valid = true;
                    return;
                }
                anim = 0x2E;
            }
        } else if (stance == HUMAN_STANCE_CROUCH) {
            if (anim < 0x37 || anim > 0x3E) {
                anim = (anim != 0x2E && anim != 0x40) ? 0x37 : 0x38;
                valid = true;
                return;
            }
        } else if (stance == HUMAN_STANCE_PRONE && anim < 0x3F) {
            anim = (anim != 0x2E && anim != 0x38) ? 0x3F : 0x40;
            valid = true;
            return;
        }
    }
    valid = true;
}

// ------------------------------------------------------------------ lifetime

C_HumanMovement::C_HumanMovement()
{
    m_weaponPose = new C_HumanWeaponPose;
    m_stanceVolumeScale = 0.8f;
    m_hitVolumeScale[0] = 1.0f;
    m_hitVolumeScale[1] = 1.0f;
    m_detached = false;
    GetMovementSounds();
    ++g_movementSoundUsers;
}

C_HumanMovement::~C_HumanMovement()
{
    delete m_weaponPose;
    m_weaponPose = nullptr;
    GetMovementSounds();
    if (--g_movementSoundUsers < 1) {
        delete g_movementSounds;
        g_movementSounds = nullptr;
    }
}

void C_HumanMovement::SetOwner(C_Human *owner)
{
    SetOwnerBase(owner, m_weaponPose);
    m_idle.m_movement = this;
    m_idle.m_animPlayer = &m_animPlayer;
    m_idle.m_locomotion = &m_locomotion;
    m_idle.m_owner = owner;
    m_visual.m_movement = this;
    m_visual.m_owner = owner;
    m_visual.m_locomotion = &m_locomotion;
    m_visual.m_lighting.Init();
    m_locomotion.Init(this, &m_specialMoves, &m_animPlayer, m_owner);
    m_specialMoves.Init(this, &m_locomotion, &m_animPlayer, &m_idle, m_owner);
    m_animPlayer.Init(this, &m_specialMoves, &m_locomotion, m_owner);
#ifndef HD2_DEDICATED   // the server attaches no weapon pose
    m_weaponPose->Attach(owner->GetFrame(), owner, false);
#endif
}

void C_HumanMovement::Init()
{
    I3D_frame *model = m_owner->GetFrame();
    I3D_scene *scene = GetGame()->GetScene();
    I3D_frame *frameZ = model->FindChildFrame(SCENE_FRAME_Z, FRAME_MASK_ANY);
    I3D_frame *frameQ = model->FindChildFrame(SCENE_FRAME_Q, FRAME_MASK_ANY);
    scene->AddHumanModel(model, frameQ, frameZ);
    model->OrFrameFlags(FRMFLAGS_HUMAN_MODEL);
    g_unknown84f418 = false;

    if (g_humansInMission < 1) {
        // The first human of the mission: footprints and the mission's climate.
        g_cameraUnderWater = false;
        SurfaceMarks_Init();
        if (!IsLocalGame() && !g_gameProfile.IsCooperativeGame())
            I3DGetDriver()->SetState(DRIVER_STATE_2B, 0);
        const C_str &mission = GetGame()->GetMissionName();
        g_coldMission = StartsWith(mission, "Arcti") || StartsWith(mission, "Norwa") || StartsWith(mission, "Prole") ||
                        StartsWith(mission, "Arden") || StartsWith(mission, "Co_Ar");
        if (StartsWith(mission, "Bre") || StartsWith(mission, "Co_Bre") || StartsWith(mission, "Norway") ||
            StartsWith(mission, "Sicily1") || StartsWith(mission, "Co_Sicily1"))
            g_missionVariant = 1;
        else if (StartsWith(mission, "Prole") || StartsWith(mission, "Bur") || StartsWith(mission, "Co_Bu") ||
                 StartsWith(mission, "Cze") || StartsWith(mission, "Sicily2") || StartsWith(mission, "Co_Sicily2"))
            g_missionVariant = 2;
        else
            g_missionVariant = 0;
        if (StartsWith(mission, "Arctic1")) {
            g_missionClimate = 0.32f;
            g_footprintBrightness = 0.45f;
        } else if (StartsWith(mission, "Arctic3")) {
            g_missionClimate = 0.26f;
            g_footprintBrightness = 0.45f;
        } else if (StartsWith(mission, "Africa4")) {
            g_missionClimate = 1.0f;
            g_footprintBrightness = 0.45f;
        } else {
            g_missionClimate = 0.0f;
            g_footprintBrightness = 1.0f;
        }
    }
    ++g_humansInMission;

    m_visual.m_lighting.m_climate = g_missionClimate;
    InitBase();
    m_idle.Reset();
    m_visual.Init();
    m_locomotion.Init(this, &m_specialMoves, &m_animPlayer, m_owner);
    m_specialMoves.Init(this, &m_locomotion, &m_animPlayer, &m_idle, m_owner);
    m_animPlayer.Init(this, &m_specialMoves, &m_locomotion, m_owner);
    m_unknownDC8 = 0;
    for (S_HumanJoint &joint : m_joints)
        joint.frame->AddRef();
}

void C_HumanMovement::Done()
{
#ifndef HD2_DEDICATED   // no surface marks on the server
    if (g_humansInMission == 1)
        SurfaceMarks_Done();
#endif
    GetGame()->GetScene()->RemoveHumanModel(m_owner->GetFrame());
    --g_humansInMission;
    DoneBase();
    m_idle.Done();
    m_visual.Done();
    m_locomotion.Done();
    for (S_HumanJoint &joint : m_joints)
        joint.frame->Release();
}

void C_HumanMovement::Reset()
{
    I3D_frame *model = m_owner->GetFrame();
    GetGame()->GetScene()->RemoveHumanModel(model);
    for (int stage = 0; stage < 8; ++stage)
        m_mixer.m_model->SetAnimation(nullptr, stage, 1.0f, 0);
    RestartBase();
    m_locomotion.Reset();
    m_animPlayer.Reset();
    m_visual.Reset();
    m_idle.Reset();
    m_morph.Reset();
#ifndef HD2_DEDICATED   // the server's resets no weapon pose
    m_weaponPose->Reset();
#endif
    C_HumanLighting &lighting = m_visual.m_lighting;
    lighting.SetSector(m_visual.m_sector);
    RemoveCollision(false);
    I3D_frame *pelvis = m_joints[HUMAN_JOINT_A].frame;
    lighting.Sample(m_visual.m_sector, model, pelvis->GetWorldPos(), true);
    lighting.Update(model, 10000, 0);
    RestoreCollision(false);
    m_unknownDC8 = 0;
    if (!model->GetParent() || model->GetParent()->GetType() == FRAME_SECTOR) {
        I3D_frame *frameZ = model->FindChildFrame(SCENE_FRAME_Z, FRAME_MASK_ANY);
        I3D_frame *frameQ = model->FindChildFrame(SCENE_FRAME_Q, FRAME_MASK_ANY);
        GetGame()->GetScene()->AddHumanModel(model, frameQ, frameZ);
    }
}

void C_HumanMovement::ResetToFrame()
{
    m_stillTime = 0;
    const S_vector4 &pos = m_owner->GetPosition4();
    m_visual.SetNetworkPosition(pos);
    m_visual.SetNetworkPosition(pos);
    m_visual.m_sectorTime = 10000;
    m_visual.m_lighting.m_sampleDue = true;
    GetGame()->GetScene()->MarkTrackedModel(m_owner->GetFrame());
}

// ------------------------------------------------------------------ saved games

bool C_HumanMovement::SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_MOVEMENT))
        return false;
    if (!m_owner->GetWork()->SaveGame(chunk) || !SaveBase(chunk)) {
        chunk.Ascend();
        return false;
    }
    // (A failure of the statics leaves the chunk unfinished on the stack, as in the original.)
    if (!C_HumanIdle::SaveStatics(chunk)) {
        chunk.Ascend();
        return false;
    }
    if (!m_locomotion.SaveGame(chunk) || !m_animPlayer.SaveGame(chunk) || !m_idle.SaveGame(chunk) ||
        !m_visual.SaveGame(chunk) || !m_mixer.SaveGame(chunk) || !m_morph.SaveGame(chunk)) {
        chunk.Ascend();
        return false;
    }
#ifndef HD2_DEDICATED   // the server's saves have no weapon pose
    m_weaponPose->SaveGame(chunk);
#endif
    chunk.Ascend();
    return true;
}

bool C_HumanMovement::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != CHUNK_MOVEMENT) {
        chunk.Ascend();
        return false;
    }
    bool ok = m_owner->GetWork()->LoadGame(chunk);
    if (ok) {
        if (!LoadBase(chunk) || !C_HumanIdle::LoadStatics(chunk)) {
            chunk.Ascend();
            return false;
        }
        ok = m_locomotion.LoadGame(chunk) && m_animPlayer.LoadGame(chunk) && m_idle.LoadGame(chunk) &&
             m_visual.LoadGame(chunk) && m_mixer.LoadGame(chunk) && m_morph.LoadGame(chunk);
        if (ok) {
#ifndef HD2_DEDICATED   // the server's saves have no weapon pose
            m_weaponPose->LoadGame(chunk);
            m_weaponPose->Attach(m_owner->GetFrame(), m_owner, true);
#endif
            chunk.Ascend();
            AfterLoad();
            return true;
        }
    }
    chunk.Ascend();
    return ok;
}

void C_HumanMovement::AfterLoad()
{
    // The light level where the body is now.
    RemoveCollision(false);
    C_HumanLighting &lighting = m_visual.m_lighting;
    lighting.SetSector(m_visual.m_sector);
    const S_vector lift(0.0f, IsLoweredState(m_owner->GetWork()->GetUnknownB8(), m_animState.anim.anim, m_seatMode) ? 0.8f : 0.0f,
                        0.0f);
    I3D_frame *pelvis = m_joints[HUMAN_JOINT_A].frame;
    lighting.Sample(m_visual.m_sector, m_owner->GetFrame(), pelvis->GetWorldPos() + lift, true);
    lighting.Update(m_owner->GetFrame(), 10000, 0);
    RestoreCollision(false);
    m_visual.SetNetworkPosition(m_owner->GetPosition4());
    I3D_frame *parent = m_owner->GetFrame()->GetParent();
    if (!parent || parent->GetType() == FRAME_SECTOR)
        ResetToFrame();
    if ((m_seatMode > 0 && m_seatMode < 9 && m_vehicle) || IsLowered())
        GetGame()->GetScene()->RemoveHumanModel(m_owner->GetFrame());
    if (m_seatMode > 0 && m_seatMode < 6 && m_vehicle && m_vehicle->GetType() == ACTOR_CAR &&
        static_cast<C_Car *>(m_vehicle)->IsDestroyed()) {
        // In a burnt-out car: dark.
        C_HumanLighting &own = m_owner->GetMovement()->m_visual.m_lighting;
        own.m_level = 0.2f;
        own.m_shownLevel = 0.3f;
        own.Update(m_owner->GetFrame(), 500, 0);
        return;
    }
    Ragdoll(true, true);
}

// ------------------------------------------------------------------ queries

bool C_HumanMovement::IsLowered() const
{
    return IsLoweredState(m_owner->GetWork()->GetUnknownB8(), m_animState.anim.anim, 0);
}

bool C_HumanMovement::GetHeadWater(bool *underWater, float *surface) const
{
    *underWater = false;
    if (m_locomotion.m_waterSurface == MOVE_NO_VALUE)
        return false;
    const float head = m_joints[HUMAN_JOINT_F1].frame->GetWorldPos().y + 0.2f;
    *surface = m_locomotion.m_waterSurface;
    if (head <= m_locomotion.m_waterSurface)
        *underWater = true;
    return m_owner->GetWork()->GetUnknownB8() == 5;
}

int C_HumanMovement::GetAnimLength(int anim, int stance, int sub) const
{
    // Fix: an animation group other than 1 / 2 has no table (the original reads near null).
    const C_HumanAnimTable *table = GetHumanAnimTable(m_owner->GetWork()->GetGroup());
    return table ? table->GetAnimLength(anim, stance, sub) : -1;
}

I3D_frame *C_HumanMovement::GetSectorFrame() const
{
    return m_visual.m_sector;
}

void C_HumanMovement::SetModelVisible(bool on)
{
    if (m_shadow)
        m_shadow->SetOn(on);
}

// ------------------------------------------------------------------ actions

void C_HumanMovement::Unknown7865c0(int arg)
{
    if (m_owner->GetHealth() > 0) {
        m_locomotion.UpdateDetail();
        m_weaponPose2->Refresh(arg);
    }
}

// Kind of a climbing state: 1 a ladder (0x6A..0x6C), 2 a window or wall (8, 0x0E), 0 none.
static int ClimbKind(int anim)                          // 0x00786a40
{
    if (anim > 0x69 && anim < 0x6D)
        return 1;
    if (anim == 8 || anim == 0x0E)
        return 2;
    return 0;
}

int C_HumanMovement::UseObject(C_Actor *object)
{
    if (ClimbKind(m_animState.anim.anim) != 0)
        return 1;
    const int next = m_nextAnimState.anim.anim;
    if ((next >= 0x6A && next <= 0x6C) || next == 8 || next == 0x0E)
        return 1;
    C_HumanWork *work = m_owner->GetWork();
    if (!object || work->GetStance() == HUMAN_STANCE_PRONE)
        return -1;
    m_useObject = object;
    const bool simulatedHere = IsLocalGame() || m_owner->IsLocalPlayer() || m_owner->IsServerSimulated();
    if (simulatedHere) {
        if (m_animState.anim.stance == HUMAN_STANCE_STAND && work->GetStance() == HUMAN_STANCE_STAND &&
            m_unknown9AC == MOVE_NO_TIME)
            return m_specialMoves.UseLadder(object);
    } else if (work->GetStance() == HUMAN_STANCE_STAND) {
        return m_specialMoves.UseLadder(object);
    }
    // Stand up first and use it then (the remote action queue; remembers the stance).
    work->SetStance(HUMAN_STANCE_STAND, true);
    if (m_unknown9AC == MOVE_NO_TIME && work->GetStance() == HUMAN_STANCE_STAND && m_animState.anim.stance != 0)
        m_request.stance = m_animState.anim.stance;
    if (m_remoteActions.empty())
        m_remoteActions.push_back({MoveTime(2500.0f), REMOTE_ACTION_USE, 0});
    return 0;
}

void C_HumanMovement::PlayDubbing(int dubbing, I3D_frame *frame, C_Human *owner, float nearRange, float farRange)
{
    GetSoundModule()->PlayDubbing(owner, frame, dubbing, nearRange, farRange, true);
}

void C_HumanMovement::SetUnknown94(float value)
{
    GetGEffects()->SetModelTransparency(m_owner->GetFrame(), value);
    if (m_unknownA30 == 1) {
        if (I3D_frame *body = m_owner->GetEquipment()->GetModel(EQUIPMENT_PLACE_CARRIED))
            GetGEffects()->SetModelTransparency(body, value);
    }
}

void C_HumanMovement::SetCameraUnderWater(bool on)
{
    g_cameraUnderWater = on;
}

bool C_HumanMovement::IsCameraUnderWater()
{
    return g_cameraUnderWater;
}

void C_HumanMovement::UpdateModelNames()
{
    m_owner->GetEquipment()->UpdateModelNames();
}

void C_HumanMovement::OnRemoteAction(int request)
{
    if (IsLocalGame() || m_owner->IsLocalPlayer() || m_owner->IsServerSimulated())
        return;
    C_HumanWork *work = m_owner->GetWork();
    work->SetUnknownB0(true);
    // Queues `action` after making the human stand (unless it is busy).
    const auto queue = [this, work](int action) {
        if ((work->GetStance() != HUMAN_STANCE_STAND || m_unknown9AC != MOVE_NO_TIME) && m_unknown9AC == MOVE_NO_TIME) {
            work->SetUnknownB0(true);
            work->SetStance(HUMAN_STANCE_STAND, true);
            work->SetUnknownB0(false);
        }
        if (!m_remoteActions.empty())
            return;
        m_remoteActions.push_back({MoveTime(2500.0f), action, 0});
    };
    switch (request) {
    case REMOTE_REQUEST_CLIMB_END:
        work->Play(0x0E, true);
        SetSeatMode(0, false);
        m_useState = MOVE_NO_TIME;
        m_owner->GetEquipment()->PutAwayHandsItem(false);
        break;
    case REMOTE_REQUEST_CLIMB_B:
        queue(REMOTE_ACTION_CLIMB_B);
        break;
    case REMOTE_REQUEST_CLIMB_C:
        queue(REMOTE_ACTION_CLIMB_C);
        break;
    case REMOTE_REQUEST_CLIMB_D:
        queue(REMOTE_ACTION_CLIMB_D);
        break;
    case REMOTE_REQUEST_CLIMB_START:
        work->Play(8, true);
        SetSeatMode(0, true);
        m_owner->GetEquipment()->PutAwayHandsItem(true);
        break;
    case REMOTE_REQUEST_CLIMB_ABORT:
        work->Play(0x2D, true);
        m_useState = MOVE_NO_TIME;
        m_unknown98C = true;
        SetSeatMode(0, false);
        m_owner->GetEquipment()->PutAwayHandsItem(false);
        break;
    case REMOTE_REQUEST_CLIMB_A:
        queue(REMOTE_ACTION_CLIMB_A);
        break;
    default:
        break;
    }
    work->SetUnknownB0(false);
    work->OrFlagsD8(1);
}

// ------------------------------------------------------------------ model changes

void C_HumanMovement::OnModelChanged()
{
    // The joints keep their pose over the change.
    GetGame()->GetScene()->RemoveHumanModel(m_owner->GetFrame());
    S_quat rotations[HUMAN_JOINT_COUNT];
    S_vector positions[HUMAN_JOINT_COUNT];
    for (int i = 0; i < HUMAN_JOINT_COUNT; ++i) {
        I3D_frame *frame = m_joints[i].frame;
        rotations[i] = frame->GetRot();
        positions[i] = frame->GetLocalMatrix()(3);
        frame->Release();
    }
    RelinkJoints();
    m_animPlayer.FindHeadDummy();
    m_weaponPose->BindSkeleton(2);
    if (C_HumanEquipment *equipment = m_owner->GetEquipment())
        equipment->Unknown793b40();
    for (int i = 0; i < HUMAN_JOINT_COUNT; ++i) {
        I3D_frame *frame = m_joints[i].frame;
        frame->SetRot(rotations[i]);
        frame->SetPosInline(positions[i]);
        frame->AddRef();
    }
    I3D_frame *model = m_owner->GetFrame();
    if (!model->GetParent() || model->GetParent()->GetType() == FRAME_SECTOR) {
        I3D_frame *frameZ = model->FindChildFrame(SCENE_FRAME_Z, FRAME_MASK_ANY);
        I3D_frame *frameQ = model->FindChildFrame(SCENE_FRAME_Q, FRAME_MASK_ANY);
        GetGame()->GetScene()->AddHumanModel(model, frameQ, frameZ);
    }
    model->OrFrameFlags(FRMFLAGS_HUMAN_MODEL);
    model->Update();
}

// ------------------------------------------------------------------ volumes, sector, light

void C_HumanMovement::Ragdoll(bool volumes, bool light)
{
    if (volumes && m_hitVolumeCount > 0) {
        UpdateHitSpheres();
        for (int i = 0; i < m_hitVolumeCount; ++i) {
            S_CollisionVolume *volume = m_hitVolumes[i];
            volume->center = S_vector(m_hitSpheres[i].x, m_hitSpheres[i].y, m_hitSpheres[i].z);
            volume->centerW = m_hitSpheres[i].w;
            volume->radius = m_hitVolumeScale[i == 0 ? 0 : 1] * m_hitSpheres[i].w;
            volume->type = 0x10002;
            g_collisionWorld.MoveVolume(volume);
        }
    }
#ifdef HD2_DEDICATED   // no lighting on the server
    (void)light;
#else
    if (!light || !IsClient())
        return;
    // The sector is looked up every 300 ms (900 for remote humans moving by the network).
    int mode = 1;
    int interval;
    if (m_owner->IsLocalPlayer()) {
        mode = 0;
        interval = 300;
    } else {
        interval = (m_flags & (MOVE_FLAG_SMALL | MOVE_FLAG_HIDDEN)) ? 900 : 300;
    }
    m_visual.m_sectorTime += 100;
    if (m_visual.m_sectorTime > interval) {
        I3D_sector *previous = m_visual.m_sector;
        m_visual.m_sectorTime -= interval;
        // (Half a metre above the model's position, as the original: 0x007872b0.)
        S_vector at = m_owner->GetFrame()->GetWorldPos();
        at.y += 0.5f;
        m_visual.m_sector = GetGame()->GetScene()->GetSector(at);
        if (previous != m_visual.m_sector) {
            if (IsClient() && g_teamManager.GetPlayerTeam()->GetActivePlayer() == m_owner)
                GetGame()->GetCameraModule()->SelectView(0);
            m_visual.m_lighting.SetSector(m_visual.m_sector);
        }
    }
    RemoveCollision(false);
    if (!InBurntCar(*this)) {
        C_HumanLighting &lighting = m_visual.m_lighting;
        lighting.Sample(m_visual.m_sector, m_owner->GetFrame(), LightJoint(*this)->GetWorldPos(), false);
        lighting.Update(m_owner->GetFrame(), 100, mode);
    }
    RestoreCollision(false);
#endif
}

void C_HumanMovement::SetPlayerControlled(bool on)
{
    m_locomotion.m_steadyTurnTime = 500;
    m_visual.SetNetworkDriven(on);
    if (m_unknownA30 == 1)
        m_owner->GetEquipment()->GetModel(EQUIPMENT_PLACE_CARRIED);
}

void C_HumanMovement::AddBloodPool(const S_vector &pos, const S_vector &normal, int /*unused*/)
{
    if (!IsClient())
        return;
    const float size = (m_bloodZone == 0 ? 0.35f : 0.25f) + static_cast<float>(std::rand() % 150) * 0.0013f;
    if (m_bloodZone != 5) {
        // On the ground under the frame of the wound.
        const S_vector from = m_bloodFrame->GetWorldPos();
        RemoveCollision(false);
        S_vector hit;
        S_vector hitNormal;
        S_CollisionVolume *ground = CollisionRayTest(from, S_vector(0.0f, -0.2f, 0.0f), &hit, &hitNormal, 0x48002, 0);
        RestoreCollision(false);
        if (ground) {
            g_shotManager.AddBloodPool(hit, hitNormal, size);
            SurfaceMarks_AddBlood(hit);
            return;
        }
    }
    g_shotManager.AddBloodPool(pos, normal, size);
    SurfaceMarks_AddBlood(pos);
}

// ------------------------------------------------------------------ updates

void C_HumanMovement::UpdateAnimation(int time)
{
    if (m_detached)
        return;
    m_owner->GetFrame()->GetWorldPos();   // (refreshes the world matrix)
    const bool localPlayer = m_owner->IsLocalPlayer();
    m_animProfileTime = 0;
    m_unknown760 = false;
    m_inUpdate = true;
    m_unknown224 = true;
    m_animProfileTime = -ProfileClock();
    if (m_unknown1AC == 0 && m_unknown9C0 == 0) {
        // Not moving at all: only the weapon pose.
        m_animProfileTime += ProfileClock();
        m_inUpdate = false;
        if ((m_weaponPose2->GetFlags() & 0x200) == 0)
            return;
        if (m_weaponPose2->GetRagdollWeight() < 1e-08f)
            return;
        if (m_locomotion.m_waterSurface != MOVE_NO_VALUE)
            return;
        m_specialMoves.UpdateWater();
        return;
    }
    if (m_flags & MOVE_FLAG_FROZEN) {
        m_inUpdate = false;
        m_animProfileTime += ProfileClock();
        return;
    }
    C_HumanWork *work = m_owner->GetWork();
    if (!localPlayer && work->GetStance() != HUMAN_STANCE_PRONE && work->GetUnknownB8() != 5)
        m_flags |= MOVE_FLAG_REMOTE;
    else
        ClearFlags(MOVE_FLAG_REMOTE);
    m_locomotion.UpdateDetail();
    work->ClearFlagsC8CCD8();
    if (work->GetUnknown5C() == 1)
        work->OrFlagsCC(0x40);
    if (m_animState.activity == 0x4A)
        work->OrFlagsCC(0x10000);
    if (m_unknownA24 > 1 && m_animState.activity == 0x4B && (m_unknownA24 -= time) < 1)
        m_unknownA24 = 1;
    work->UpdateStanceTimer(time);
    ProcessRemoteActions(time);
    work->Update(time);
    const int anim = m_animState.anim.anim;
    if (((anim < 0x29 || anim > 0x41) && (anim < 0x1E || anim > 0x21) && (anim < 0xA2 || anim > 0xA5)) || m_parachuting) {
        m_stillTime = 0;
        m_locomotion.m_unknown0E8 = 3;
    }
    if (m_unknown9AC != MOVE_NO_TIME && (m_unknown9AC -= time) < 1) {
        m_unknown9AC = MOVE_NO_TIME;
        m_stillTime = -500;
    }
    if (m_unknownA28 > 0 && (m_unknownA28 -= time) < 0)
        m_unknownA28 = 0;
    if (m_unknown1AC == 1)
        m_locomotion.Update(time);
    m_idle.Update(time);
    m_animPlayer.Update();

    if (IsClient()) {
        // The light level at the pelvis (the carried body is hidden for the sample).
        I3D_frame *carried = m_unknownA30 > 0 ? m_owner->GetEquipment()->GetModel(EQUIPMENT_PLACE_CARRIED) : nullptr;
        RemoveCollision(false);
        if (!carried) {
            if (!InBurntCar(*this)) {
                // (Lowered bodies are sampled 0.8 m above the pelvis.)
                const S_vector lift(0.0f,
                                    IsLoweredState(work->GetUnknownB8(), m_animState.anim.anim, m_seatMode) ? 0.8f : 0.0f,
                                    0.0f);
                m_visual.m_lighting.Sample(m_visual.m_sector, m_owner->GetFrame(), LightJoint(*this)->GetWorldPos() + lift,
                                           false);
            }
        } else {
            carried->SetOn(false);
            m_visual.m_lighting.Sample(m_visual.m_sector, m_owner->GetFrame(), LightJoint(*this)->GetWorldPos(), false);
            carried->SetOn(m_owner->GetFrame()->IsOn());
        }
        RestoreCollision(false);
    }
    work->SetChanged(0);

    if (m_trailTime > 0 && (m_trailTime -= time) < 1) {
        for (int i = 0; i < m_trailCount; ++i)
            g_collisionWorld.RemoveVolume(m_trailVolumes[i]);
        m_trailCount = 0;
    }
    m_idle.UpdateInteraction(time);
    UpdateSounds(false);
    if (IsServer()) {
        // Damaging ground; falling out of the world kills.
        if (work->GetUnknownB8() == 5 && m_unknown764 == 0x46)
            DamageByMaterial(0x46, time);
        else if (!m_unknown760)
            DamageByMaterial(m_damageMaterial, time);
        const S_vector pos = m_owner->GetPosition();
        if (pos.y < g_scanMin[1] - 5.0f)
            m_owner->Hit(HIT_ENVIRONMENT, 1000.0f, S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, 0.0f, 0.0f), nullptr, 0);
    }

    // The medium the body moves in, for the controller (MOVE_MEDIUM_MASK: 0x100000 a ladder,
    // 0x20000 climbing over, 0x80000 sliding, MOVE_SWIMMING the head under water).
    uint32_t surfaceFlags = 0;
    const int state = m_animState.anim.anim;
    if ((state >= 0x6A && state <= 0x6C) || state == 8 || state == 0x0E)
        surfaceFlags = 0x100000;
    else if (state >= 8 && state <= 0x0E)
        surfaceFlags = 0x20000;
    else if (state == 0x12 || state == 0x18 || state == 0x17)
        surfaceFlags = 0x80000;
    // Breath: the head under water.
    if (m_locomotion.m_waterSurface != MOVE_NO_VALUE &&
        m_joints[HUMAN_JOINT_F1].frame->GetWorldPos().y + 0.08f <= m_locomotion.m_waterSurface) {
        UpdateBreath(time, true);
        surfaceFlags |= 0x40000;
    } else {
        UpdateBreath(time, false);
    }
    // (None: the group is cleared.)
    m_owner->SetMoveFlags(surfaceFlags != 0 ? surfaceFlags : MOVE_MEDIUM_MASK);
    if (m_timer200 >= 200)
        m_timer200 -= 200;
    if (m_timer300 >= 300)
        m_timer300 -= 300;
    if (m_timer400 >= 400)
        m_timer400 -= 400;
    if (m_timer600 >= 600)
        m_timer600 -= 600;

    const bool remotePhysics =
        (m_flags & MOVE_FLAG_NOT_DRAWN) && !m_owner->IsLocalPlayer() && m_unknown1AC == 1 && !m_onDynamicGround;
    if (!remotePhysics) {
        if (m_unknown18C && (static_cast<float>(m_stillTime) <= 280.0f / MOVE_TIME_SCALE || m_onDynamicGround))
            m_visual.SetNetworkPosition(m_owner->GetPosition4());
        m_animProfileTime += ProfileClock();
    } else {
        if (m_unknown18C && static_cast<float>(m_stillTime) <= 280.0f / MOVE_TIME_SCALE) {
            m_visual.SetNetworkDriven(true);
            m_visual.SetNetworkPosition(m_owner->GetPosition4());
            m_visual.SetNetworkDriven(false);
        }
        m_animProfileTime += ProfileClock();
        m_visual.SetNetworkDriven(true);
        UpdatePhysics(time);
        m_visual.SetNetworkDriven(false);
    }

    // The velocity of the root motion (the stronger of the mixer's two sets).
    const S_vector4 *velocity;
    float weight;
    if (m_mixer.m_weightB <= m_mixer.m_weightA) {
        velocity = &m_mixer.m_velocityA;
        weight = m_mixer.m_weightA;
    } else {
        velocity = &m_mixer.m_velocityB;
        weight = m_mixer.m_weightB;
    }
    m_locomotion.m_velocity.x = velocity->x * weight;
    m_locomotion.m_velocity.y = velocity->y * weight;
    m_locomotion.m_velocity.z = velocity->z * weight;
    // (The original stores a leftover of its stack frame in w.)
    m_inUpdate = false;
    if (m_unknownDC8 > 0)
        --m_unknownDC8;
}

void C_HumanMovement::UpdatePhysics(int time)
{
    if (m_detached)
        return;
    if (m_unknown1AC == 0 && m_unknown9C0 == 0) {
        m_physicsProfileTime = 0;
#ifndef HD2_DEDICATED   // no weapon pose on the server
        m_weaponPose->Update(time);
#endif
        return;
    }
    if (!IsLocalGame() && !m_owner->GetFrame()->IsOn()) {
        // Hidden models (remote humans): those in a sector or in a visible seat are shown again
        // (except the local player's own while the first person weapon view is up).
        if (!GetGame()->IsFirstPersonView() || !m_owner->IsLocalPlayer()) {
            C_Actor *vehicle = m_owner->GetVehicle();
            I3D_frame *parent = m_owner->GetFrame()->GetParent();
            if ((parent && parent->GetType() == FRAME_SECTOR) ||
                (vehicle && (vehicle->GetType() == ACTOR_STATIC_WEAPON ||
                             (vehicle->GetType() == ACTOR_CAR && !(vehicle->GetSeatFlags(m_seat) & SEAT_FLAG_HIDDEN)))))
                m_owner->GetFrame()->SetOn(true);
        }
    }
    if (m_inUpdate) {
        UpdatePhysicsParts(time);
        return;
    }
    m_timer200 += time;
    m_timer300 += time;
    m_timer400 += time;
    m_timer600 += time;
    if ((m_flags & MOVE_FLAG_NOT_DRAWN) && !m_owner->IsLocalPlayer() && m_unknown1AC == 1)
        return;
    m_physicsProfileTime = -ProfileClock();
    if (m_unknown1AC != 1) {
        m_mixer.Update(time);
        m_locomotion.Update(time);
    }
    if (m_flags & MOVE_FLAG_FROZEN) {
        m_physicsProfileTime += ProfileClock();
        return;
    }
    UpdatePhysicsRest(time);
    m_physicsProfileTime += ProfileClock();
#ifndef HD2_DEDICATED   // no weapon pose on the server
    m_weaponPose->Update(time);
#endif
}

void C_HumanMovement::UpdatePhysicsParts(int time)
{
    if (m_detached)
        return;
    if (m_unknown1AC != 1) {
        m_mixer.Update(time);
        m_locomotion.Update(time);
    }
    if (m_flags & MOVE_FLAG_FROZEN)
        return;
    UpdatePhysicsRest(time);
#ifndef HD2_DEDICATED   // the server's has no weapon pose update here
    m_weaponPose->Update(time);
#endif
}

void C_HumanMovement::UpdatePhysicsRest(int time)
{
    // (Shared by UpdatePhysics and UpdatePhysicsParts, inline in both.)
    // (The original calls an empty locomotion member here when m_unknown1AC is 1.)
    if (!m_owner->IsLocalPlayer()) {
        m_locomotion.SetTurnTarget(m_owner->GetWork()->GetFacingDir(), time);
        const int state = m_animState.anim.anim;
        if ((state < 8 || state > 0x0E || state == 8) && state != 0x12 &&
            (m_useState == MOVE_NO_TIME || m_unknown998 != MOVE_NO_TIME)) {
            if (m_locomotion.m_turnTarget.x > MOVE_NO_VALUE) {
                m_locomotion.m_turning = true;
                m_locomotion.m_unknown0E4 = false;
                if (m_unknown998 != MOVE_NO_TIME && (m_unknown998 -= time) < 1)
                    m_unknown998 = MOVE_NO_TIME;
                m_locomotion.m_turnSpeed = TURN_SPEED_OTHERS;
                if (m_locomotion.UpdateTurn(time, true))
                    m_locomotion.m_turnTarget.x = MOVE_NO_VALUE;
            }
        } else {
            m_locomotion.m_turning = false;
        }
    }
    if (m_unknown18C)
        m_visual.SetNetworkDirection(m_owner->GetDirection4());
    m_visual.UpdatePosition(time);
    m_visual.Update(time);
    if (m_unknown950 > 0 && (m_unknown950 -= time) < 0)
        m_unknown950 = 0;
    if (m_animPlayer.m_unknown58 != MOVE_NO_TIME && m_animPlayer.m_unknown58 > 0)
        m_animPlayer.m_unknown58 -= time;
    if (m_footstepTime < 1000)
        m_footstepTime += time;
    m_clock += time;
    if (m_clock > 0xFFFFFC17u)
        m_clock = 0;
}

// ------------------------------------------------------------------ the movement sound bank

C_MovementSounds *g_movementSounds = nullptr;
int g_movementSoundUsers = 0;

namespace {

// A sound file of the bank (table 0x0084ea48: 20 groups of 6).
struct S_MovementSoundFile {                            // 0x10 bytes
    const char *fileName;                               // null: the group has no more
    float nearRange;
    float farRange;
    float volume;
};

const S_MovementSoundFile MOVEMENT_SOUND_FILES[C_MovementSounds::GROUPS][C_MovementSounds::MAX_VARIANTS] = {
    {{"p_swim01.wav", 1.0f, 20.0f, 1.0f}, {"p_swim02.wav", 1.0f, 20.0f, 1.0f}},
    {{"p_swuw01.wav", 1.0f, 3.0f, 1.0f}, {"p_swuw02.wav", 1.0f, 3.0f, 1.0f}},
    {{"uvs01.wav", 0.1f, 15.0f, 0.8f}, {"uvs02.wav", 0.1f, 15.0f, 0.8f}},
    {{"uv100.wav", 0.1f, 15.0f, 0.8f}, {"uv101.wav", 0.1f, 15.0f, 0.8f}, {"uv102.wav", 0.1f, 15.0f, 0.8f},
     {"uv103.wav", 0.1f, 15.0f, 0.8f}},
    {{"o_lay.wav", 0.3f, 15.0f, 1.0f}},
    {{"o_kneel.wav", 0.3f, 15.0f, 1.0f}},
    {{"o_stay.wav", 0.3f, 15.0f, 1.0f}},
    {{"o_lwmv01.wav", 0.3f, 10.0f, 1.0f}, {"o_lwmv02.wav", 0.3f, 10.0f, 1.0f}, {"o_lwmv03.wav", 0.3f, 10.0f, 1.0f}},
    {{"o_mimv01.wav", 0.3f, 10.0f, 1.0f}, {"o_mimv02.wav", 0.3f, 10.0f, 1.0f}, {"o_mimv03.wav", 0.3f, 10.0f, 1.0f},
     {"o_mimv04.wav", 0.3f, 10.0f, 1.0f}, {"o_mimv05.wav", 0.3f, 10.0f, 1.0f}, {"o_mimv06.wav", 0.3f, 10.0f, 1.0f}},
    {{"o_frun01.wav", 0.3f, 10.0f, 1.0f}, {"o_frun02.wav", 0.3f, 10.0f, 1.0f}, {"o_frun03.wav", 0.3f, 10.0f, 1.0f},
     {"o_frun04.wav", 0.3f, 10.0f, 1.0f}, {"o_frun05.wav", 0.3f, 10.0f, 1.0f}, {"o_frun06.wav", 0.3f, 10.0f, 1.0f}},
    {{"o_wout.wav", 0.3f, 10.0f, 1.0f}},
    {{"o_win.wav", 0.3f, 10.0f, 1.0f}},
    {{"o_grtake.wav", 0.3f, 10.0f, 1.0f}},
    {{"o_over.wav", 0.3f, 10.0f, 1.0f}},
    {{"o_over2.wav", 0.3f, 10.0f, 1.0f}},
    {{"o_over3.wav", 0.3f, 10.0f, 1.0f}},
    {},
    {{"o_juover.wav", 0.3f, 10.0f, 0.9f}},
    {{"drown01.wav", 0.3f, 10.0f, 1.0f}, {"drown02.wav", 0.3f, 10.0f, 1.0f}, {"drown03.wav", 0.3f, 10.0f, 1.0f},
     {"drown04.wav", 0.3f, 10.0f, 1.0f}},
    {{"uv100.wav", 0.3f, 10.0f, 1.0f}},
};

// The order the files of a group are played in: shuffled once, then each played file goes
// back at a random place behind `split` (the files played most recently stay at the back).
struct S_SoundDeck {                                    // 0x14 bytes (0x0098a4b0 + 0x14 * group)
    std::vector<int> order;
    int split = 0;
};
S_SoundDeck g_soundDecks[C_MovementSounds::GROUPS];
int g_soundCounts[C_MovementSounds::GROUPS];            // 0x0098a640
bool g_soundDecksBuilt = false;                         // 0x0098a698

// Puts `variant` at a random place from `from` on (the end counts twice).
void InsertAtRandom(std::vector<int> &order, int variant, int from)
{
    const int size = static_cast<int>(order.size());
    const int at = std::rand() % (size - from + 2) + from;
    if (at < 0 || at > size - 1)
        order.push_back(variant);
    else
        order.insert(order.begin() + at, variant);
}

}  // namespace

C_MovementSounds *GetMovementSounds()
{
    if (!g_movementSounds)
        g_movementSounds = new C_MovementSounds;
    return g_movementSounds;
}

C_MovementSounds::C_MovementSounds()
{
    g_movementSoundUsers = 0;
    // One sound of each file made in advance.
    for (int group = 0; group < GROUPS; ++group) {
        m_caches[group] = new S_Cache[GetCount(group)];
        for (int variant = 0; variant < GetCount(group); ++variant) {
            I3D_sound *sound = GetSound(group, variant);
            if (!sound)
                continue;
            sound->SetOn(false);
            S_Cache &cache = m_caches[group][variant];
            if (cache.count < CACHED)
                cache.sounds[cache.count++] = sound;
            else
                sound->Release();
        }
    }
}

C_MovementSounds::~C_MovementSounds()
{
    for (int group = 0; group < GROUPS; ++group) {
        for (int variant = 0; variant < GetCount(group); ++variant) {
            S_Cache &cache = m_caches[group][variant];
            for (int i = 0; i < cache.count; ++i) {
                cache.sounds[i]->Release();
                cache.sounds[i] = nullptr;
            }
            cache.count = 0;
        }
        delete[] m_caches[group];
        m_caches[group] = nullptr;
    }
}

int C_MovementSounds::GetCount(int group)
{
    if (!g_soundDecksBuilt) {
        for (int g = 0; g < GROUPS; ++g) {
            S_SoundDeck &deck = g_soundDecks[g];
            g_soundCounts[g] = 0;
            for (int variant = 0; variant < MAX_VARIANTS; ++variant) {
                if (!MOVEMENT_SOUND_FILES[g][variant].fileName)
                    break;
                InsertAtRandom(deck.order, g_soundCounts[g], deck.split);
                ++g_soundCounts[g];
            }
        }
        g_soundDecksBuilt = true;
    }
    return g_soundCounts[group];
}

I3D_sound *C_MovementSounds::GetSound(int group, int variant)
{
#ifdef HD2_DEDICATED   // no sounds on the server
    (void)group;
    (void)variant;
    return nullptr;
#else
    if (!ISndGetDriver()->IsInitialized())
        return nullptr;
    const S_MovementSoundFile &file = MOVEMENT_SOUND_FILES[group][variant];
    S_Cache &cache = m_caches[group][variant];
    if (cache.count) {
        I3D_sound *sound = cache.sounds[cache.count - 1];
        cache.sounds[cache.count - 1] = nullptr;
        --cache.count;
        if (sound) {
            sound->Play(0);
            sound->SetOn(true);
            sound->SetVolume(file.volume);
            return sound;
        }
    }
    auto *sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
    if (g_soundCache.OpenInto(sound, file.fileName) != I3D_OK) {
        // (The original builds an "AESound '<file>' not found!" message here, unless the
        // result is 0x80000009, and drops it.)
        sound->Release();
        return nullptr;
    }
    sound->SetSoundType(I3DSOUND_POINT);
    sound->SetVolume(file.volume);
    sound->SetRange(file.nearRange, file.farRange, 0.5f, 0.5f);
    sound->SetLoop(false);
    sound->Play(0);
    sound->SetOn(true);
    // The group and the file, for Return().
    sound->SetData(reinterpret_cast<S_FrameData *>(static_cast<uintptr_t>(group << 8 | variant)));
    return sound;
#endif
}

I3D_sound *C_MovementSounds::Play(int group, I3D_frame *frame, const S_vector *pos)
{
    S_SoundDeck &deck = g_soundDecks[group];
    // Fix: a group without files (16) has no order; the original reads through a null pointer.
    if (deck.order.empty())
        return nullptr;
    // The next file; it goes back behind the split, which moves with the queue.
    const int variant = deck.order.front();
    deck.order.erase(deck.order.begin());
    const int size = static_cast<int>(deck.order.size());
    if (deck.split == 0)
        deck.split = size;
    else
        --deck.split;
    if (deck.split == 0) {
        const int at = std::rand() % (size + 1) + 1;
        if (at >= 0 && at <= size - 1)
            deck.order.insert(deck.order.begin() + at, variant);
        else
            deck.order.push_back(variant);
    } else {
        InsertAtRandom(deck.order, variant, deck.split);
    }
    I3D_sound *sound = GetSound(group, variant);
    if (sound && frame) {
        sound->LinkTo(frame, 0);
        sound->SetPosInline(*pos);
    }
    return sound;
}

void C_MovementSounds::Return(I3D_sound *sound)
{
    if (!sound)
        return;
    const uint32_t data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sound->GetData()));
    const int group = static_cast<int>(data >> 8);
    const int variant = static_cast<int>(data & 0xFF);
    sound->LinkTo(nullptr, 0);
    if (group >= GROUPS || variant >= GetCount(group)) {
        sound->Release();
        return;
    }
    sound->SetOn(false);
    S_Cache &cache = m_caches[group][variant];
    if (cache.count < CACHED)
        cache.sounds[cache.count++] = sound;
    else
        sound->Release();
}

// ------------------------------------------------------------------ vector helpers

void RotateAboutY(S_vector &v, float angle)
{
    S_quat rot;
    rot.Make(S_vector(0.0f, 1.0f, 0.0f), angle);
    v *= rot.RotationMatrix();
}

void RotateAboutX(S_vector4 &v, float angle)
{
    S_matrix m;
    m.Identity();
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    m.m[2][2] = cosine;
    m.m[1][1] = cosine;
    m.m[2][1] = sine;
    m.m[1][2] = -sine;
    // (The original also copies 4 bytes past the rotated vector into w.)
    const S_vector rotated = S_vector(v.x, v.y, v.z).RotateByMatrix(m);
    v.x = rotated.x;
    v.y = rotated.y;
    v.z = rotated.z;
}

void RotateAboutZ(S_vector4 &v, float angle)
{
    S_matrix m;
    m.Identity();
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    m.m[1][1] = cosine;
    m.m[0][0] = cosine;
    m.m[1][0] = sine;
    m.m[0][1] = -sine;
    const S_vector rotated = S_vector(v.x, v.y, v.z).RotateByMatrix(m);
    v.x = rotated.x;
    v.y = rotated.y;
    v.z = rotated.z;
}

float AngleBetween2D(const float *a, const float *b)
{
    constexpr float PI = 3.1415927f;
    // (The two FPATANs at the x87's full precision, their difference rounded to float: as the original.)
    float angle = static_cast<float>(std::atan2(static_cast<double>(b[1]), static_cast<double>(b[0])) -
                                     std::atan2(static_cast<double>(a[1]), static_cast<double>(a[0])));
    if (angle < -PI)
        angle += 2.0f * PI;
    if (angle > PI)
        angle -= 2.0f * PI;
    return angle;
}
