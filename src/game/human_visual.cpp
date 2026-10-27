// Where a human's model is shown and lit (C_HumanVisual, C_HumanMovement +0xCD0; original source
// 0x00758080..0x0075b0c0): gliding the model of a remote human toward the network positions, the
// sector and light level of the model, its shadow, and the animation events of the stages.
#include "game/human_movement.h"

#include <cmath>
#include <cstdlib>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_anim_tables.h"
#include "game/human_body.h"
#include "game/human_equipment.h"
#include "game/human_internal.h"
#include "game/ladder.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/sound_module.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

constexpr uint16_t CHUNK_VISUAL = 0x22B;
enum : uint16_t {
    CHUNK_VISUAL_LIGHTING = 0x00,                       // C_HumanLighting (0x1C bytes)
    CHUNK_VISUAL_UNKNOWN0C = 0x03,
    CHUNK_VISUAL_SHOW_HANDS_TIME = 0x04,
    CHUNK_VISUAL_INITIALIZED = 0x05,
    CHUNK_VISUAL_NETWORK_DRIVEN = 0x07,
    CHUNK_VISUAL_GLIDE_TIME = 0x08,
    CHUNK_VISUAL_GLIDE_DURATION = 0x09,
    CHUNK_VISUAL_PREVIOUS_TARGET = 0x0A,
    CHUNK_VISUAL_UNKNOWN40 = 0x0B,
    CHUNK_VISUAL_TARGET = 0x0C,
    CHUNK_VISUAL_NEW_TARGET = 0x0D,
    CHUNK_VISUAL_UNKNOWN70 = 0x12,
    CHUNK_VISUAL_UNKNOWNC0 = 0x19,
    CHUNK_VISUAL_FLAGS = 0x1A,
    CHUNK_VISUAL_GLIDE_DONE = 0x1B,
};

constexpr int PARTICLE_BREATH = 0x21;
constexpr int CARRY_ANIM_DRAW = 0x1F;
constexpr int CARRY_ANIM_NONE = 0x5B;
constexpr int ACTIVITY_NONE = 0x4B;
constexpr int ANIM_STAND = 0x2D;

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

void SetXYZ(S_vector4 &v, const S_vector &p)
{
    v.x = p.x;
    v.y = p.y;
    v.z = p.z;
}

S_vector4 FromMatrixRow(const S_matrix &m, int row)
{
    return {m.m[row][0], m.m[row][1], m.m[row][2], m.m[row][3]};
}

// The length as the engine's inline computes it (0 below 1e-8 squared).
float Length(const S_vector &v)
{
    const float square = v.Square();
    if (square < 1e-8f)
        return 0.0f;
    if (std::fabs(square - 1.0f) < 1e-8f)
        return 1.0f;
    return std::sqrt(square);
}

bool IsActivePlayer(const C_Human *human) { return g_teamManager.GetPlayerTeam()->GetActivePlayer() == human; }

}  // namespace

// ------------------------------------------------------------------ lifetime

void C_HumanVisual::ResetState()
{
    m_newTarget = false;
    m_glideTime = 0;
    m_glideDuration = -1;
    m_unknown0C = m_owner->GetGroup() & 4;
    m_movement->m_groundMaterial = -1;
    m_flags = 0;
    m_glideDone = false;
    m_unknown70.x = m_unknown70.y = m_unknown70.z = 0.0f;
    m_networkDriven = false;
    I3D_frame *model = m_owner->GetFrame();
    m_unknownC0 = model->GetWorldPos().z;
    m_showHandsTime = -1;
    m_moveSoundTime = 0;
    m_owner->GetEquipment()->UpdateModelNames();
    m_sectorTime = std::rand() % 1000;
    m_sector = GetGame()->GetScene()->GetSector(model->GetWorldPos());
    m_lighting.SetSector(m_sector);
    m_lighting.Update(model, 10000, 0);
    m_target = FromMatrixRow(model->GetMatrix(), 3);
    m_previousTarget = m_target;
    m_glideFrom = m_target;
    m_glideTo = m_target;
    SetTarget(m_target, true);
}

void C_HumanVisual::InitModel()
{
    m_movement->m_mixer.Init(m_owner);
    m_movement->m_shadow = nullptr;
    ResetState();
}

void C_HumanVisual::DoneModel()
{
    m_movement->m_mixer.Done();
    RemoveShadow();
}

void C_HumanVisual::Init()
{
    InitModel();
    C_HumanAnimMixer &mixer = m_movement->m_mixer;
    mixer.Init(m_owner);
    C_HumanWork *work = m_owner->GetWork();
    // Standing, the weapon (if any) carried, no activity.
    S_HumanAnimRef anim;
    anim.Set(ANIM_STAND, HUMAN_STANCE_STAND, 1);
    const int carry[2] = {work->GetUnknown5C() != 0 ? 0x20 : CARRY_ANIM_DRAW, 0};
    const int activity[2] = {ACTIVITY_NONE, 0};
    // Fix: an animation group other than 1 / 2 has no table (the original reads near null).
    const C_HumanAnimTable *table = GetHumanAnimTable(work->GetGroup());
    // (Only the first track gets an animation; the original leaves its crossfade unset.)
    S_MixerAnim stages[MIXER_TRACKS] = {};
    stages[0].anim = table ? table->GetAnim(ANIM_STAND, HUMAN_STANCE_STAND) : nullptr;
    stages[0].weight = 1.0f;
    stages[0].mode = I3DANIM_LOOP;
    stages[0].blendTime = 100.0f;
    if (stages[0].anim)
        stages[0].anim->SetPlayMode(I3DANIM_LOOP);
    mixer.Play(stages, &anim, carry, activity, false);
    mixer.UpdateStages(1000, 1000, true, stages);
    mixer.UpdateStages(1000, 1000, false, nullptr);
    m_initialized = true;
}

void C_HumanVisual::Done()
{
    DoneModel();
    m_initialized = false;
}

void C_HumanVisual::Reset()
{
    C_HumanWork *work = m_owner->GetWork();
    const int carry[2] = {work->GetUnknown5C() != 0 ? 0x20 : CARRY_ANIM_DRAW, 0};
    const int activity[2] = {ACTIVITY_NONE, 0};
    ResetState();
    const C_HumanAnimTable *table = GetHumanAnimTable(work->GetGroup());
    C_HumanAnimMixer &mixer = m_movement->m_mixer;
    mixer.Init(m_owner);
    S_HumanAnimRef anim;
    anim.Set(ANIM_STAND, HUMAN_STANCE_STAND, 1);
    S_MixerAnim stages[MIXER_TRACKS] = {};
    stages[0].anim = table ? table->GetAnim(anim.anim, anim.stance) : nullptr;
    stages[0].weight = 1.0f;
    stages[0].mode = I3DANIM_LOOP;
    stages[0].blendTime = 0.0f;
    if (stages[0].anim)
        stages[0].anim->SetPlayMode(I3DANIM_LOOP);
    mixer.Play(stages, &anim, carry, activity, false);
    mixer.UpdateStages(1000, 1000, true, stages);
    mixer.UpdateStages(1000, 1000, false, nullptr);
    m_lighting.SetSector(m_sector);
}

void C_HumanVisual::SetNetworkDriven(bool on)
{
    if (!IsLocalGame() && !m_owner->IsServerSimulated()) {
        m_networkDriven = true;
        return;
    }
    m_networkDriven = on;
}

// ------------------------------------------------------------------ gliding

void C_HumanVisual::UpdateStandingFlag()
{
    const S_HumanAnimRef &anim = m_movement->m_animState.anim;
    const C_HumanAnimMixer &mixer = m_movement->m_mixer;
    bool moving = anim.anim > 0xBD;
    if (!moving) {
        float speed = 0.0f;
        bool known = false;
        switch (anim.anim) {
        case 0x12:
            if (anim.stance == HUMAN_STANCE_STAND) {
                speed = g_moveSpeeds[8][anim.sub];
                known = true;
            }
            break;
        case 0x17:
            if (anim.stance != HUMAN_STANCE_STAND) {
                speed = g_moveSpeeds[10][anim.sub + anim.stance * 6];
                known = true;
            }
            break;
        case 0x18:
            speed = g_moveSpeeds[8][anim.sub + anim.stance * 6];
            known = true;
            break;
        case 0x59:
        case 0x5A:
            speed = 0.63f;
            known = true;
            break;
        case 0x6A:
            if (mixer.m_tracks[0].hold < 1) {
                const int state = m_owner->GetWork()->GetGait();
                speed = state == 2 || state == 5 ? 0.63f : 0.504f;
                known = true;
            }
            break;
        case 0x6B:
            if (mixer.m_tracks[0].hold < 1) {
                speed = 0.504f;
                known = true;
            }
            break;
        case 0x6C:
            if (mixer.m_tracks[0].hold < 1) {
                speed = 3.0f;
                known = true;
            }
            break;
        default:
            break;
        }
        moving = known && speed != 0.0f;
    }
    // (The mixer never sets m_extraMoveDuration to MOVE_NO_TIME; in the original it is left
    // uninitialised, so the flag is in effect never set. Kept as it is.)
    if (!moving && m_movement->m_unknown1AC == 1 && mixer.m_addMove2Duration == MOVE_NO_TIME &&
        mixer.m_addMoveDuration == MOVE_NO_TIME && mixer.m_moveToDuration == MOVE_NO_TIME &&
        mixer.m_slideDuration == MOVE_NO_TIME && mixer.m_extraMoveDuration == MOVE_NO_TIME) {
        m_flags |= VISUAL_FLAG_STANDING;
        return;
    }
    m_flags &= ~VISUAL_FLAG_STANDING;
}

void C_HumanVisual::SetTarget(const S_vector4 &pos, bool restart)
{
    UpdateStandingFlag();
    const S_matrix &matrix = m_owner->GetFrame()->GetMatrix();
    if (restart) {
        m_glideDuration = 100;
        m_glideTime = 0;
        m_newTarget = true;
        m_previousTarget = m_target;
        m_target = pos;
        m_glideFrom = FromMatrixRow(matrix, 3);
        m_glideTo = pos;
        SetXYZ(m_step, XYZ(m_target) - XYZ(m_previousTarget));
        return;
    }
    // On along the last step: the glide is timed by how far the model still is from the target.
    const float remaining = Length(XYZ(m_target) - matrix(3));
    const float step = Length(XYZ(m_target) - XYZ(m_previousTarget));
    m_glideFrom = FromMatrixRow(m_owner->GetFrame()->GetMatrix(), 3);
    SetXYZ(m_step, XYZ(m_target) - XYZ(m_previousTarget));
    m_glideDuration = 100;
    SetXYZ(m_glideTo, XYZ(m_step) + XYZ(m_target));
    if (remaining < step) {
        m_glideTime = static_cast<int>(remaining / step * 100.0f);
        return;
    }
    m_glideTime = 0;
    if (step > 0.0f)
        m_glideDuration = static_cast<int>(remaining / step * 100.0f);
}

void C_HumanVisual::Glide(int time)
{
    const int useKind = m_movement->m_interaction;
    if (m_networkDriven || !(useKind == 0 || useKind == 1) || !m_movement->m_unknown18C) {
        m_glideTime = 0;
        return;
    }
    const int pushState = m_movement->m_unknown1AC;
    if (pushState != 1 && pushState != 7 && pushState != 8 && pushState != 6 && pushState != 9)
        return;
    if (m_glideDone)
        return;
    m_newTarget = false;
    float t;
    if (m_glideTime < m_glideDuration) {
        t = static_cast<float>(m_glideTime) / static_cast<float>(m_glideDuration);
    } else if (m_flags == 0) {
        // Past the target: on along the last step.
        const S_vector4 next = {m_glideTo.x + m_step.x, m_glideTo.y + m_step.y, m_glideTo.z + m_step.z, 0.0f};
        SetTarget(next, false);
        m_glideTime = time;
        t = static_cast<float>(time) / static_cast<float>(m_glideDuration);
    } else {
        t = 1.0f;
        m_glideDone = true;
        m_glideTime = 0;
    }
    const S_vector pos = XYZ(m_glideTo) * t + XYZ(m_glideFrom) * (1.0f - t);
    I3D_frame *model = m_owner->GetFrame();
    if ((pos - model->GetWorldPos()).Square() <= 1e-7f)
        return;
    model->SetPos(pos);
    GetGame()->GetScene()->Unknown144(model);
    model->Update();
}

void C_HumanVisual::UpdatePosition(int time)
{
    if (!m_newTarget) {
        if (m_flags != 0)
            m_glideDone = true;
    } else {
        m_glideDone = false;
    }
    m_glideTime += time;
    Glide(time);
}

void C_HumanVisual::SetNetworkPosition(const S_vector4 &pos)
{
    const S_vector4 target = {pos.x, pos.y, pos.z, 0.0f};
    if (IsLocalGame() || m_owner->IsServerSimulated()) {
        m_flags &= ~VISUAL_FLAG_2;
        SetTarget(target, true);
    }
    const int useKind = m_movement->m_interaction;
    if (!m_networkDriven || (useKind != 0 && useKind != 1))
        return;
    I3D_frame *model = m_owner->GetFrame();
    const S_vector delta = model->GetWorldPos() - XYZ(pos);
    if (std::fabs(delta.x) + std::fabs(delta.y) + std::fabs(delta.z) < 0.0005f)
        return;
    m_movement->m_unknown240.x = MOVE_NO_VALUE;
    m_movement->m_unknown240.y = MOVE_NO_VALUE;
    m_movement->m_unknown240.z = MOVE_NO_VALUE;
    model->SetPos(XYZ(pos));
    GetGame()->GetScene()->Unknown144(model);
    model->Update();
}

void C_HumanVisual::SetNetworkDirection(const S_vector4 &dir)
{
    const int pushState = m_movement->m_unknown1AC;
    if (pushState != 1 && pushState != 2 && pushState != 0 && pushState != 9 && m_movement->m_unknown9CC == MOVE_NO_TIME)
        return;
    const S_vector direction = XYZ(dir);
    I3D_frame *model = m_owner->GetFrame();
    const S_vector delta = model->GetWorldDir() - direction;
    C_HumanMovement *movement = m_movement;
    if (movement->m_seatMode >= 1)
        return;
    if (movement->m_unknown254 == movement->m_unknown250 &&
        std::fabs(delta.x) + std::fabs(delta.y) + std::fabs(delta.z) < 0.002f)
        return;
    movement->m_unknown254 = movement->m_unknown250;
    model->SetDir(direction, movement->m_unknown250);
    model->Update();
    if (m_owner->GetWork()->GetStance() == HUMAN_STANCE_PRONE)
        m_locomotion->UpdateCollisionVolumes(true);
}

void C_HumanVisual::SetPosition(const S_vector4 &pos)
{
    C_HumanMovement *movement = m_owner->GetMovement();
    if (movement->m_detached)
        return;
    const int stance = m_owner->GetWork()->GetStance();
    bool place = !IsLocalGame() && m_movement->m_unknown1AC == 1;
    if (place) {
        // Not in a vehicle seat.
        if (C_Actor *vehicle = m_owner->GetVehicle()) {
            switch (vehicle->GetType()) {
            case ACTOR_CAR:
            case ACTOR_SUBMARINE:
            case ACTOR_STATIC_WEAPON:
            case ACTOR_JUNKERS88:
                place = false;
                break;
            default:
                break;
            }
        }
    }
    if (!place) {
        m_movement->m_unknown18C = true;
        return;
    }
    I3D_frame *model = m_owner->GetFrame();
    model->GetWorldPos();   // (the original measures the move but does not use it)
    model->SetPos(XYZ(pos));
    GetGame()->GetScene()->Unknown144(model);
    model->Update();
    C_HumanMovement &body = *m_movement;
    body.UpdateDummies();
    body.UpdateStanceVolumes(stance);
    for (int i = 0; i < body.m_stanceVolumeCount; ++i) {
        S_CollisionVolume *volume = body.m_stanceVolumes[i];
        volume->center = XYZ(body.m_stanceSpheres[i]);
        volume->centerW = body.m_stanceSpheres[i].w;
        volume->radius = body.m_stanceSpheres[i].w * body.m_stanceVolumeScale;
        g_collisionWorld.MoveVolume(volume);
    }
    body.UpdateHitSpheres();
    for (int i = 0; i < body.m_hitVolumeCount; ++i) {
        S_CollisionVolume *volume = body.m_hitVolumes[i];
        volume->center = XYZ(body.m_hitSpheres[i]);
        volume->centerW = body.m_hitSpheres[i].w;
        volume->radius = body.m_hitVolumeScale[i] * body.m_hitSpheres[i].w;
        g_collisionWorld.MoveVolume(volume);
    }
    m_owner->SetPosition(XYZ(pos), pos.w);
}

void C_HumanVisual::SetDirection(const S_vector4 &dir, float roll)
{
    if (m_owner->GetMovement()->m_detached)
        return;
    S_vector4 direction = dir;
    C_HumanMovement *movement = m_movement;
    const int useKind = movement->m_interaction & 0x3FF;
    if (IsLocalGame() || movement->m_unknown1AC != 1 || (useKind == 0x1A && !IsServer())) {
        movement->m_unknown18C = true;
        return;
    }
    const int anim = movement->m_animState.anim.anim;
    const bool climbing = (anim >= 0x6A && anim <= 0x6C) || anim == 8 || anim == 0x0E;
    if (climbing && movement->m_useObject) {
        // Facing the ladder.
        const S_vector ladder = movement->m_useObject->GetFrame()->GetWorldDir();
        direction.x = ladder.x;
        direction.y = ladder.y;
        direction.z = ladder.z;
        direction.w = movement->m_useObject->GetFrame()->GetMatrix().m[2][3];
    } else {
        movement->m_unknown18C = false;
    }
    if (movement->m_unknown18C)
        return;
    I3D_frame *model = m_owner->GetFrame();
    model->SetDir(XYZ(direction), roll);
    C_HumanWork *work = m_owner->GetWork();
    work->SetUnknownB0(true);
    work->SetViewDir(XYZ(direction));
    work->SetFacingDir(XYZ(direction), true);
    m_owner->SetDirection(XYZ(direction), direction.w);
    work->SetUnknownB0(false);
    model->Update();
    if (work->GetStance() == HUMAN_STANCE_PRONE)
        movement->m_weaponPose2->FitProneBody();
}

// ------------------------------------------------------------------ the shadow

void C_HumanVisual::RebuildShadow()
{
    RemoveShadow();
    I3D_frame *model = m_owner->GetFrame();
    I3D_frame *shadow = GetGEffects()->CreateShadow(model);
    m_movement->m_shadow = shadow;
    if (!shadow)
        return;
    shadow->AddRef();
    static_cast<I3D_shadow *>(shadow)->SetUnknown1FC(20.0f);
    shadow->SetOn(model->IsOn());
}

void C_HumanVisual::RemoveShadow()
{
    I3D_frame *shadow = m_movement->m_shadow;
    if (!shadow)
        return;
    shadow->SetOn(false);
    shadow->LinkTo(nullptr, 0);
    if (shadow->GetRefCount() > 1) {
        shadow->Release();
        shadow->Release();
    } else if (shadow->GetRefCount() == 1) {
        shadow->Release();
    }
    m_movement->m_shadow = nullptr;
}

// ------------------------------------------------------------------ per frame

void C_HumanVisual::Unknown7585d0(bool on)
{
    C_HumanMovement *movement = m_movement;
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    const int carry = movement->m_animState.carryAnim;
    int event = -1;
    if (on) {
        movement->m_nextAnimState.carryAnim = CARRY_ANIM_DRAW;
        movement->m_unknownA28 = MoveTime(150.0f);
        event = 3;
    } else if (carry == 0x22) {
        event = 2;
    } else if (carry >= 0x23 && carry <= 0x24) {
        event = 3;
    }
    if (event >= 0) {
        // (Only on foot or in some seats; the shadow keeps its state.)
        if (movement->m_interaction == 0 || movement->m_interaction == 1 || movement->m_seatMode == 2 ||
            movement->m_seatMode == 4) {
            const bool shadowOn = movement->m_shadow && movement->m_shadow->IsOn();
            equipment->OnAnimationEvent(event);
            if (movement->m_shadow && !shadowOn)
                movement->m_shadow->SetOn(false);
        }
    }
    if (on) {
        const int carryMode = m_owner->GetWork()->GetUnknown54();
        if (carryMode != 2 && carryMode != 6)
            equipment->OnAnimationEvent(EQUIPMENT_EVENT_0);
    }
}

void C_HumanVisual::Update(int time)
{
    m_moveSoundTime += time;
    C_HumanMovement *movement = m_movement;
    const int pushState = movement->m_unknown1AC;
    if (pushState > 6 || pushState == 1) {
        ProcessAnimEvents();
        movement->m_mixer.UpdateStages(time, time, false, nullptr);
    }
    // The sector every 300 ms (900 for remote humans moved by the network); the light level
    // sampled by the human's speed.
    int mode;
    int interval;
    if (m_owner->IsLocalPlayer()) {
        mode = 0;
        interval = 300;
    } else {
        const uint32_t flags = m_owner->GetMovement()->m_flags;
        interval = (flags & MOVE_FLAG_SMALL) || (flags & MOVE_FLAG_HIDDEN) ? 900 : 300;
        switch (m_owner->GetWork()->GetGait()) {
        case 0:
        case 3:
        case 4:
            mode = 5;
            break;
        case 2:
        case 5:
            mode = 0;
            break;
        default:
            mode = 4;
            break;
        }
    }
    if (movement->m_stillTime > 1999 && movement->m_seatMode < 1) {
        mode = 3;
        interval = 30000;
    }
    m_sectorTime += time;
    if (m_sectorTime > interval) {
        I3D_sector *previous = m_sector;
        m_sectorTime %= interval;
        I3D_scene *scene = GetGame()->GetScene();
        m_sector = scene->GetSector(m_owner->GetFrame()->GetWorldPos());
        if (previous != m_sector) {
            if (IsClient()) {
                I3D_sector *primary = scene->GetPrimarySector();
                if ((primary == previous || primary == m_sector || !previous || !m_sector) && IsActivePlayer(m_owner))
                    GetGame()->GetCameraModule()->Unknown40(0);
            }
            m_lighting.SetSector(m_sector);
        }
    }
    m_lighting.Update(m_owner->GetFrame(), time, mode);

    C_HumanEquipment *equipment = m_owner->GetEquipment();
    if (movement->m_unknownA30 > 0) {
        // The carried body takes the carrier's light.
        equipment->Unknown7938f0(time);
        I3D_frame *body = equipment->GetModel(EQUIPMENT_PLACE_CARRIED);
        if (body && body->GetData() && body->GetData()->actor) {
            C_HumanLighting &lighting = static_cast<C_Human *>(body->GetData()->actor)->GetMovement()->m_visual.m_lighting;
            lighting.m_level = m_lighting.m_level;
            lighting.m_ambient = m_lighting.m_ambient;
            lighting.Update(equipment->GetModel(EQUIPMENT_PLACE_CARRIED), time, mode);
        }
    }
    if (m_showHandsTime > 0 && (m_showHandsTime -= time) < 1)
        equipment->ShowHandsItem(true, 1, false);

    // The weapon drawn: its model goes to the hands at the end of the drawing.
    const int carry = movement->m_animState.carryAnim;
    C_HumanWeaponPose *pose = movement->m_weaponPose2;
    if ((carry == CARRY_ANIM_DRAW || carry == 0x20 || carry == 0x1E) &&
        movement->m_nextAnimState.carryAnim == CARRY_ANIM_NONE && movement->m_animState.activity == ACTIVITY_NONE &&
        movement->m_nextAnimState.activity == ACTIVITY_NONE && pose->GetWeapon() != 0 &&
        (pose->GetHand() & 0xF) == 1) {
        const int carryMode = m_owner->GetWork()->GetUnknown54();
        if (carryMode != 2 && carryMode != 0 && movement->m_mixer.GetBlendProgress(1) >= 1.0f &&
            !(equipment->GetFlags() & EQUIPMENT_HANDS_PUT_AWAY) && !(pose->GetHand() & 0x80000000u))
            equipment->OnAnimationEvent(EQUIPMENT_EVENT_0);
    }
    movement->m_morph.Update(time);
}

void C_HumanVisual::ProcessAnimEvents()
{
    C_HumanMovement *movement = m_movement;
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    C_HumanWork *work = m_owner->GetWork();
    for (int stage = 0; stage < 3; ++stage) {
        int count = 0;
        const int *events = movement->m_mixer.GetEvents(stage, &count);
        for (int i = 0; i < count; ++i) {
            const int event = events[i];
            switch (event) {
            case ANIM_EVENT_WEAPON_DRAWN:
                Unknown7585d0(false);
                break;
            case ANIM_EVENT_EQUIPMENT_1:
                equipment->OnAnimationEvent(EQUIPMENT_EVENT_1);
                break;
            case ANIM_EVENT_WEAPON_READY: {
                const C_HumanWeaponPose *pose = movement->m_weaponPose2;
                if (pose->GetWeapon() != 0 && (pose->GetHand() & 0xF) == 1) {
                    const int carryMode = work->GetUnknown54();
                    if (carryMode != 2 && carryMode != 0)
                        equipment->OnAnimationEvent(EQUIPMENT_EVENT_0);
                }
                break;
            }
            case ANIM_EVENT_STEP:
            case ANIM_EVENT_STEP_LEFT:
            case ANIM_EVENT_STEP_6: {
                movement->Footstep(event == ANIM_EVENT_STEP_LEFT);
                // The active player's clothes rustle now and then (by stance and speed; the
                // quiet walk by the stealth stat).
                if (!IsClient() || !IsActivePlayer(m_owner) || m_moveSoundTime <= 329)
                    break;
                m_moveSoundTime = 0;
                const S_HumanAnimRef &anim = movement->m_animState.anim;
                const int sub = anim.sub;
                // Fix: a stealth stat of 0 divides by zero in the original.
                const auto quietRoll = [this]() {
                    const int stealth = m_owner->GetStat(STAT_STEALTH);
                    return stealth > 0 && (std::rand() % stealth) / 5 == 0;
                };
                if (anim.stance == HUMAN_STANCE_STAND) {
                    if (sub == 2 || sub == 5)
                        movement->PlaySound(MOVE_SOUND_RUN);
                    else if (sub == 1 || sub == 3 || sub == 4)
                        movement->PlaySound(MOVE_SOUND_MOVE);
                    else if (sub == 0 && quietRoll())
                        movement->PlaySound(MOVE_SOUND_MOVE);
                } else if (anim.stance == HUMAN_STANCE_CROUCH) {
                    if (sub == 2 || sub == 5)
                        movement->PlaySound(anim.anim >= 0xA2 && anim.anim <= 0xA5 ? MOVE_SOUND_MOVE_LOW : MOVE_SOUND_MOVE);
                    else if (sub == 1 || sub == 3 || sub == 4)
                        movement->PlaySound(MOVE_SOUND_MOVE);
                    else if (sub == 0 && quietRoll())
                        movement->PlaySound(MOVE_SOUND_MOVE);
                } else if (anim.stance == HUMAN_STANCE_PRONE) {
                    if (sub >= 1 && sub <= 5)
                        movement->PlaySound(MOVE_SOUND_MOVE_LOW);
                    else if (sub == 0 && quietRoll())
                        movement->PlaySound(MOVE_SOUND_MOVE_LOW);
                }
                break;
            }
            case ANIM_EVENT_DOOR:
                // Opening / closing the door being used (m_vehicle; the seat says which).
                if ((movement->m_interaction & 0x3FF) == 1) {
                    movement->m_seatMode = -1;
                    if (IsServer()) {
                        auto *door = static_cast<C_Door *>(movement->m_vehicle);
                        door->SetState(movement->m_seat == 0 ? 3 : 2, true, false, m_owner->GetRole() != 1);
                    }
                }
                break;
            case ANIM_EVENT_PICK_UP: {
                const int pickUp = movement->m_unknownA2C & ~8;
                if (pickUp == 0) {
                    movement->m_unknownA2C |= 1;
                    work->SetUnknownB0(true);
                    work->SetCarryMode(1, false);
                    work->SetUnknownB0(false);
                    equipment->OnAnimationEvent((movement->m_unknownA2C & 8) ? 0x0D : 5);
                    equipment->UpdateModelNames();
                } else if (pickUp == 2) {
                    movement->m_unknownA2C = MOVE_NO_TIME;
                    // (The flag is read after the reset, as in the original: always event 4.)
                    equipment->OnAnimationEvent((movement->m_unknownA2C & 8) ? 0x0C : 4);
                    equipment->UpdateModelNames();
                } else if (movement->m_unknownA30 == 0 || movement->m_unknownA30 == 2) {
                    I3D_frame *body = equipment->GetModel(EQUIPMENT_PLACE_CARRIED);
                    C_Human *human = body && body->GetData() ? static_cast<C_Human *>(body->GetData()->actor) : nullptr;
                    if (movement->m_unknownA30 == 0)
                        movement->OnPickUpBody(human);
                    else
                        movement->OnDropBody(human);
                }
                break;
            }
            case ANIM_EVENT_ACTIVITY_END: {
                uint32_t bits = 0;
                switch (movement->m_animState.carryAnim) {
                case 0x46:
                    bits = 0x20000;
                    break;
                case 0x4C:
                    bits = 0x1000;
                    break;
                case 0x4D:
                    bits = 0x2000;
                    break;
                case 0x4F:
                case 0x5A: {
                    // The object of place 7 is used up; the hands item comes back later.
                    m_showHandsTime = MoveTime(1400.0f);
                    if (I3D_frame *model = equipment->GetModel(7)) {
                        if (model->GetRefCount() < 2) {
                            model->Release();
                        } else {
                            model->Release();
                            model->Release();
                        }
                        // (One more release for 0x5A, as in the original.)
                        if (movement->m_animState.carryAnim == 0x5A)
                            model->Release();
                    }
                    equipment->SetModel(nullptr, 7, -1, MOVE_NO_TIME, -1);
                    bits = movement->m_animState.carryAnim == 0x4F ? 0x4000 : 0x8000;
                    break;
                }
                default:
                    break;
                }
                if (bits)
                    work->OrFlagsCC(bits);
                if (work->GetUnknownB8() == 4)
                    movement->SetSeatMode(3, false);
                break;
            }
            case ANIM_EVENT_HIDE_HANDS_ITEM: {
                const int carryMode = work->GetUnknown54();
                equipment->ShowHandsItem(false, carryMode == 0 || carryMode == 2 ? 1 : 0, true);
                break;
            }
            case ANIM_EVENT_HIDE_HANDS_ITEM_B:
                if (m_showHandsTime > 0) {
                    m_showHandsTime = -1;
                    break;
                }
                equipment->ShowHandsItem(false, 0, true);
                break;
            case ANIM_EVENT_HIDE_HANDS_ITEM_C:
                equipment->ShowHandsItem(false, 0, true);
                m_showHandsTime = -1;
                break;
            case ANIM_EVENT_SPECIAL_DETACH: {
                const int carried = movement->m_mixer.GetTrackTag(2);
                if (carried == 0x50)
                    equipment->OnAnimationEvent(0x0B);
                else if (carried == 0x52)
                    equipment->OnAnimationEvent(7);
                else if (carried == 0x53)
                    equipment->OnAnimationEvent(9);
                else if (movement->m_animState.carryAnim == 0x4F || movement->m_animState.carryAnim == 0x5A)
                    equipment->OnAnimationEvent(0x0B);
                break;
            }
            case ANIM_EVENT_SPECIAL_ATTACH: {
                const int carried = movement->m_mixer.GetTrackTag(2);
                if (carried == 0x50)
                    equipment->OnAnimationEvent(0x0A);
                else if (carried == 0x52)
                    equipment->OnAnimationEvent(6);
                else if (carried == 0x53)
                    equipment->OnAnimationEvent(8);
                break;
            }
            case ANIM_EVENT_PROJECTILE_HAND2:
                equipment->OnAnimationEvent(EQUIPMENT_EVENT_PROJECTILE_HAND2);
                break;
            case ANIM_EVENT_PROJECTILE_HAND1:
                equipment->OnAnimationEvent(EQUIPMENT_EVENT_PROJECTILE_HAND1);
                break;
            case ANIM_EVENT_PROJECTILE_WEAPON:
                equipment->OnAnimationEvent(EQUIPMENT_EVENT_PROJECTILE_WEAPON);
                break;
            case ANIM_EVENT_BREATH: {
                I3D_frame *head = movement->m_joints[HUMAN_JOINT_F1].frame;
                const S_vector dir = head->GetWorldDir();
                S_vector pos = head->GetWorldPos() + dir * 0.15f;
                pos.y += 0.04f;
                g_particlesModule.CreateAt(PARTICLE_BREATH, pos, dir, nullptr, true, false);
                break;
            }
            case ANIM_EVENT_LADDER_STEP: {
                // (Without a ladder or its sound the original stops handling the events.)
                if (!movement->m_useObject || GetLadderStepSoundGroup(movement->m_useObject) == -1)
                    return;
                S_SoundRequest request;
                request.pos = S_vector(0.0f, -0.4f, 0.0f);
                request.flags = SOUND_SILENT_TO_AI;
                request.attachTo = movement->m_joints[HUMAN_JOINT_A].frame;
                request.source = m_owner;
                request.chapter = 0;
                request.group = GetLadderStepSoundGroup(movement->m_useObject);
                request.channel = 3;
                GetSoundModule()->Play(request);
                break;
            }
            case ANIM_EVENT_LIGHT:
                if (movement->m_animState.activity == 0x11)
                    equipment->SetFlashlight(true);
                equipment->UpdateLensFlare(true);
                break;
            case ANIM_EVENT_UNKNOWN21:
                movement->m_unknown9C8 = true;
                break;
            case ANIM_EVENT_UNKNOWN22: {
                movement->m_unknown9C9 = true;
                const S_vector4 &facing = m_owner->GetDirection4();
                const S_vector4 offset = {facing.x * 0.05f, 0.1f, facing.z * 0.05f, 0.0f};
                movement->m_mixer.Slide(offset, MoveTime(700.0f));
                break;
            }
            case ANIM_EVENT_UNKNOWN23:
                movement->m_stillTime = -2000;
                m_locomotion->m_volumesTime = 0;
                break;
            case ANIM_EVENT_SOUND: {
                if (!IsClient())
                    break;
                if (GetGame()->IsFirstPersonView() && IsActivePlayer(m_owner))
                    break;
                S_SoundRequest request;
                request.pos = S_vector(0.0f, 0.1f, 0.0f);
                request.flags = SOUND_SILENT_TO_AI;
                request.attachTo = movement->m_joints[HUMAN_JOINT_F1].frame;
                request.source = m_owner;
                request.chapter = 1;
                request.group = 0x2E;
                GetSoundModule()->Play(request);
                break;
            }
            default:
                break;
            }
        }
    }
}

// ------------------------------------------------------------------ saved games

bool C_HumanVisual::SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_VISUAL))
        return false;
    chunk.WriteChunk(CHUNK_VISUAL_LIGHTING, &m_lighting, sizeof(m_lighting));
    chunk.WriteChunk(CHUNK_VISUAL_UNKNOWN0C, m_unknown0C);
    chunk.WriteChunk(CHUNK_VISUAL_SHOW_HANDS_TIME, m_showHandsTime);
    chunk.WriteChunk(CHUNK_VISUAL_INITIALIZED, static_cast<int>(m_initialized));
    chunk.WriteChunk(CHUNK_VISUAL_NETWORK_DRIVEN, static_cast<int>(m_networkDriven));
    chunk.WriteChunk(CHUNK_VISUAL_GLIDE_TIME, m_glideTime);
    chunk.WriteChunk(CHUNK_VISUAL_GLIDE_DURATION, m_glideDuration);
    chunk.WriteChunk(CHUNK_VISUAL_PREVIOUS_TARGET, &m_previousTarget, 0x0C);
    chunk.WriteChunk(CHUNK_VISUAL_UNKNOWN40, &m_unknown40, 0x0C);
    chunk.WriteChunk(CHUNK_VISUAL_TARGET, &m_target, 0x0C);
    chunk.WriteChunk(CHUNK_VISUAL_NEW_TARGET, static_cast<int>(m_newTarget));
    chunk.WriteChunk(CHUNK_VISUAL_UNKNOWN70, &m_unknown70, 0x0C);
    chunk.WriteChunk(CHUNK_VISUAL_UNKNOWNC0, m_unknownC0);
    chunk.WriteChunk(CHUNK_VISUAL_FLAGS, m_flags);
    chunk.WriteChunk(CHUNK_VISUAL_GLIDE_DONE, static_cast<int>(m_glideDone));
    chunk.Ascend();
    return true;
}

bool C_HumanVisual::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != CHUNK_VISUAL) {
        chunk.Ascend();
        return false;
    }
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        int value = 0;
        switch (id) {
        case CHUNK_VISUAL_LIGHTING:
            if (chunk.RemainingSize() >= static_cast<int>(sizeof(m_lighting)))
                chunk.Read(&m_lighting, sizeof(m_lighting));
            break;
        case CHUNK_VISUAL_UNKNOWN0C:
            chunk.Read(m_unknown0C);
            break;
        case CHUNK_VISUAL_SHOW_HANDS_TIME:
            chunk.Read(m_showHandsTime);
            break;
        case CHUNK_VISUAL_INITIALIZED:
            chunk.Read(value);
            m_initialized = value != 0;
            break;
        case CHUNK_VISUAL_NETWORK_DRIVEN:
            chunk.Read(value);
            m_networkDriven = value != 0;
            break;
        case CHUNK_VISUAL_GLIDE_TIME:
            chunk.Read(m_glideTime);
            break;
        case CHUNK_VISUAL_GLIDE_DURATION:
            chunk.Read(m_glideDuration);
            break;
        case CHUNK_VISUAL_PREVIOUS_TARGET:
            chunk.Read(&m_previousTarget, 0x0C);
            break;
        case CHUNK_VISUAL_UNKNOWN40:
            chunk.Read(&m_unknown40, 0x0C);
            break;
        case CHUNK_VISUAL_TARGET:
            chunk.Read(&m_target, 0x0C);
            break;
        case CHUNK_VISUAL_NEW_TARGET:
            chunk.Read(value);
            m_newTarget = value != 0;
            break;
        case CHUNK_VISUAL_UNKNOWN70:
            chunk.Read(&m_unknown70, 0x0C);
            break;
        case CHUNK_VISUAL_UNKNOWNC0:
            chunk.Read(m_unknownC0);
            break;
        case CHUNK_VISUAL_FLAGS:
            chunk.Read(m_flags);
            break;
        case CHUNK_VISUAL_GLIDE_DONE:
            chunk.Read(value);
            m_glideDone = value != 0;
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    m_sectorTime = 0;
    m_sector = GetGame()->GetScene()->GetSector(m_owner->GetFrame()->GetWorldPos());
    return true;
}
