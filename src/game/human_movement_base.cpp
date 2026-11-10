// The base part of the human movement (original source 0x0075b260..0x00763650): the stance
// dummies and the collision volumes of the body (stance, hit and trail spheres), the joints of
// the skeleton, footsteps, the movement sounds and breath, damaging ground, carrying bodies, the
// network replay of remote humans' actions and the saved games of all that.
#include "game/human_movement.h"

#include <malloc.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_body.h"
#include "game/human_equipment.h"
#include "game/human_internal.h"
#include "game/items.h"
#include "game/ladder.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/shots.h"
#include "game/sound_module.h"
#include "game/surface_marks.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// Names of the stance dummies (E_StanceDummy order; table 0x0084e87c, again at 0x0084e8a0).
const char *const STANCE_DUMMY_NAMES[STANCE_DUMMY_COUNT] = {
    "stand1", "stand2", "stand3", "kneel1", "kneel2", "lie1", "lie2", "lie3", "lie4",
};
// Names of the joints (E_HumanJoint order; 0x0098a348, filled by the static initialiser
// 0x007830e0).
const char *const HUMAN_JOINT_NAMES[HUMAN_JOINT_COUNT] = {
    "u", "q", "t", "p", "f1", "d1", "d", "c1", "g", "k", "h", "l", "i",
    "m", "j", "n", "a", "b", "c", "e", "f", "o", "r", "s", "v",
};
const char *const SHOT_DUMMY_NAMES[2] = {"shot1", "shot2"};

// Frames of the model the scene keeps for humans (I3D_scene::AddHumanModel).
const char *const SCENE_FRAME_Q = "q";
const char *const SCENE_FRAME_Z = "z";

// The body's collision volumes: spheres; the hit spheres carry the material of flesh.
constexpr int BODY_MATERIAL = 0x42;
constexpr uint32_t HIT_VOLUME_FLAGS = COLLISION_SHAPE_SPHERE | (BODY_MATERIAL << 24);
// S_CollisionVolume::type of the body's volumes (bits partly identified).
constexpr uint32_t VOLUME_TYPE_STANCE = 0x101;
constexpr uint32_t VOLUME_TYPE_HIT = 0x102;
constexpr uint32_t VOLUME_TYPE_HIT_PLAIN = 0x2;         // (OnFrozen)
constexpr uint32_t VOLUME_TYPE_DEAD = 0x10000;          // hit volumes of a dead body

// Stance "none" (m_nextStance, m_nextStance2).
constexpr int STANCE_NONE = 3;
// E_HumanAnim ids used here.
constexpr int ANIM_STAND = 0x2D;
constexpr int ANIM_NONE = 0xCE;
constexpr int CARRY_ANIM_NONE = 0x5B;
constexpr int ACTIVITY_NONE = 0x4B;
constexpr int ACTIVITY_GASP = 0x4C;                     // a work animation (surfacing)
constexpr int ACTIVITY_DROWNING = 0x42;
// The aqualung (item type id; on the back it lets the human breathe under water).
constexpr int ITEM_AQUALUNG = 0xE7;
// Dust raised by the steps of remote humans on sand (particle effect).
constexpr int PARTICLE_STEP_DUST = 0x2A;

// Number of humans whose base part is initialised.
int g_movementBases = 0;                                // 0x009832b0
// The volumes the queries ignore while the body is tested (RemoveCollision(false)).
S_CollisionVolume *g_ignoredVolumes[10];                // 0x009831e8

// Saved games of the base part (chunk 0x14D and its sub-chunks).
enum : uint16_t {
    CHUNK_BASE = 0x14D,
    CHUNK_BASE_UNKNOWN180 = 0x00,
    CHUNK_BASE_BLOOD_ZONE = 0x02,
    CHUNK_BASE_BLOOD_FRAME = 0x03,                      // the frame's name
    CHUNK_BASE_STANCE_SCALE = 0x04,
    CHUNK_BASE_IN_UPDATE = 0x05,
    CHUNK_BASE_ANIM = 0x06,                             // S_HumanAnimRef (0x10 bytes)
    CHUNK_BASE_CARRY_ANIM = 0x07,                       // carryAnim, carryVariant, unknown18
    CHUNK_BASE_ACTIVITY = 0x08,                         // activity, activityVariant
    CHUNK_BASE_NEXT_ANIM = 0x09,
    CHUNK_BASE_NEXT_CARRY_ANIM = 0x0A,
    CHUNK_BASE_NEXT_ACTIVITY = 0x0B,
    CHUNK_BASE_UNKNOWN220 = 0x0C,
    CHUNK_BASE_UNKNOWN228 = 0x0D,
    CHUNK_BASE_FLAGS = 0x0E,
    CHUNK_BASE_UNKNOWN98C = 0x0F,
    CHUNK_BASE_HIT_COUNT = 0x10,
    CHUNK_BASE_GROUND_MATERIAL = 0x11,
    CHUNK_BASE_UNKNOWN764 = 0x12,
    CHUNK_BASE_REQUEST = 0x13,                          // S_MoveRequest (chunk 0x308 inside)
    CHUNK_BASE_UNKNOWN940 = 0x14,
    CHUNK_BASE_BREATH_TIME = 0x15,
    CHUNK_BASE_HIT_SCALE0 = 0x16,
    CHUNK_BASE_HIT_SCALE1 = 0x17,
    CHUNK_BASE_UNKNOWN762 = 0x18,
    CHUNK_BASE_UNKNOWN761 = 0x19,
    CHUNK_BASE_STANCE_COUNT = 0x1A,
    CHUNK_BASE_DAMAGE_MATERIAL = 0x1C,
    CHUNK_BASE_UNKNOWN760 = 0x1D,
    CHUNK_BASE_UNKNOWN234 = 0x20,
    CHUNK_BASE_UNKNOWN954 = 0x21,
    CHUNK_BASE_UNKNOWN958 = 0x22,
    CHUNK_BASE_UNKNOWN95C = 0x23,
    CHUNK_BASE_UNKNOWN960 = 0x24,
    CHUNK_BASE_UNKNOWN964 = 0x25,
    CHUNK_BASE_UNKNOWN968 = 0x26,
    CHUNK_BASE_USE_STATE = 0x27,
    CHUNK_BASE_UNKNOWN96C = 0x28,
    CHUNK_BASE_UNKNOWNA90 = 0x29,
    CHUNK_BASE_UNKNOWN998 = 0x2A,
    CHUNK_BASE_UNKNOWNA48 = 0x2B,
    CHUNK_BASE_STANCE_CHANGE = 0x2C,
    CHUNK_BASE_UNKNOWN9A0 = 0x2D,
    CHUNK_BASE_NEXT_STANCE = 0x2E,
    CHUNK_BASE_NEXT_STANCE2 = 0x2F,
    CHUNK_BASE_UNKNOWN9AC = 0x30,
    CHUNK_BASE_UNKNOWN9B0 = 0x31,
    CHUNK_BASE_UNKNOWN9B8 = 0x32,
    CHUNK_BASE_UNKNOWN9BC = 0x33,
    CHUNK_BASE_UNKNOWN9C0 = 0x34,
    CHUNK_BASE_UNKNOWN9C4 = 0x35,
    CHUNK_BASE_UNKNOWN9CC = 0x36,
    CHUNK_BASE_PUSH_STATE = 0x37,
    CHUNK_BASE_UNKNOWN9D0 = 0x38,
    CHUNK_BASE_UNKNOWN9E0 = 0x39,
    CHUNK_BASE_UNKNOWN9F0 = 0x3A,
    CHUNK_BASE_UNKNOWNA00 = 0x3B,
    CHUNK_BASE_UNKNOWNA10 = 0x3C,
    CHUNK_BASE_UNKNOWNA1C = 0x3D,
    CHUNK_BASE_HANDS_CARRY_STATE = 0x3E,
    CHUNK_BASE_UNKNOWNA24 = 0x3F,
    CHUNK_BASE_UNKNOWNA28 = 0x40,
    CHUNK_BASE_UNKNOWNA2C = 0x41,
    CHUNK_BASE_UNKNOWNA30 = 0x42,
    CHUNK_BASE_UNKNOWNA34 = 0x43,
    CHUNK_BASE_UNKNOWNA38 = 0x44,
    CHUNK_BASE_VEHICLE = 0x45,                          // net id (temporary actors + 100000), -1
    CHUNK_BASE_SEAT = 0x46,
    CHUNK_BASE_SEAT_FRAME = 0x47,                       // the frame's name
    CHUNK_BASE_UNKNOWNA44 = 0x51,
    CHUNK_BASE_HIT_SCALE = 0x53,                        // both hit scales (the first one is saved)
    CHUNK_BASE_USE_OBJECT = 0x54,                       // net id, -1
    CHUNK_BASE_TRAIL_TIME = 0x55,
    CHUNK_BASE_TRAIL_COUNT = 0x56,
    CHUNK_BASE_TRAIL_SPHERES = 0x57,                    // 4 x S_SavedSphere
    CHUNK_BASE_UNKNOWN9C8 = 0x58,
    CHUNK_BASE_UNKNOWNA14 = 0x59,
    CHUNK_BASE_UNKNOWNA18 = 0x5A,
    CHUNK_BASE_UNKNOWN950 = 0x5C,
    CHUNK_BASE_UNKNOWN250 = 0x5D,
    CHUNK_BASE_UNKNOWN9B4 = 0x5E,
    CHUNK_BASE_FOOTSTEP_SURFACE = 0x60,
    CHUNK_BASE_UNKNOWN190 = 0x61,
    CHUNK_BASE_CLOCK = 0x62,
    CHUNK_BASE_FOOTSTEP_TIME = 0x63,
    CHUNK_BASE_UNKNOWN1B0 = 0x64,
    CHUNK_BASE_CARRIED = 0x65,
    CHUNK_BASE_FOOTSTEP_COUNT = 0x66,
    CHUNK_BASE_UNKNOWN9C9 = 0x67,
    CHUNK_BASE_UNKNOWN254 = 0x68,                       // as an int
    CHUNK_BASE_REMOTE_COUNT = 0x69,
    CHUNK_BASE_REMOTE_FIRST = 0x6A,                     // one chunk per S_RemoteAction
};
// At most this many remote actions are saved.
constexpr int SAVED_REMOTE_ACTIONS = 19;
// Net ids of temporary actors are saved above this.
constexpr int TEMPORARY_ACTOR_ID_BASE = 100000;

// A trail sphere as saved (the original stages them in a static buffer at 0x009832c0).
struct S_SavedSphere {                                  // 0x20 bytes
    S_vector4 center;
    float radius;
    uint8_t unknown14[0x0C];
};

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

void SetXYZ(S_vector4 &v, const S_vector &p)
{
    v.x = p.x;
    v.y = p.y;
    v.z = p.z;
}

// A body volume (aligned, as the original's _aligned_malloc).
S_CollisionVolume *NewBodyVolume(C_Human *owner, uint32_t flags, uint32_t type)
{
    void *memory = _aligned_malloc(sizeof(S_CollisionVolume), 16);
    S_CollisionVolume *volume = new (memory) S_CollisionVolume;
    volume->flags = flags;
    volume->frame = owner->GetFrame();
    volume->actor = owner;
    volume->type = type;
    return volume;
}

void DeleteBodyVolume(S_CollisionVolume *&volume)
{
    if (volume) {
        volume->~S_CollisionVolume();
        _aligned_free(volume);
    }
    volume = nullptr;
}

// Puts a sphere into a volume (its radius scaled).
void SetVolumeSphere(S_CollisionVolume *volume, const S_vector4 &sphere, float scale)
{
    volume->center = XYZ(sphere);
    volume->centerW = sphere.w;
    volume->radius = scale * sphere.w;
}

// Half the width of a dummy's box (the radius of its sphere).
float DummyRadius(const I3D_frame *dummy)
{
    const I3D_bbox &box = static_cast<const I3D_dummy *>(dummy)->GetBBox();
    return (box.max.x - box.min.x) * 0.5f;
}

bool IsClimbingAnim(int anim) { return anim > 7 && anim < 0x0F; }

}  // namespace

// ------------------------------------------------------------------ the command record

void S_MoveRequest::Reset()
{
    // (The w components are left as the original leaves them: not set.)
    anim = 0x2D;
    activity = 0x4B;
    unknown10.x = unknown10.y = unknown10.z = 0.0f;
    stance = HUMAN_STANCE_STAND;
    gait = 1;
    carryMode = 0;
    action = 0;
    unknown5C = 0;
    hitFlags = 0;
    killer = nullptr;
    workingFrame = nullptr;
    interactTarget = nullptr;
    interaction = 0;
    facingDir.x = facingDir.y = facingDir.z = 0.0f;
    interactArg = 0;
    hitForce = S_vector(0.0f, 0.0f, 0.0f);
    hitPos = S_vector(0.0f, 0.0f, 0.0f);
}

bool S_MoveRequest::SaveGame(C_chunk &chunk) const
{
    chunk.WriteChunk(0x308, this, sizeof(*this));
    return true;
}

bool S_MoveRequest::LoadGame(C_chunk &chunk)
{
    uint16_t id = 0;
    if (chunk.IsOpen())
        chunk.Descend(id);
    if (id != 0x308) {
        chunk.Ascend();
        return false;
    }
    if (chunk.RemainingSize() >= static_cast<int>(sizeof(*this)))
        chunk.Read(this, sizeof(*this));
    killer = nullptr;
    workingFrame = nullptr;
    interactTarget = nullptr;
    chunk.Ascend();
    return true;
}

// ------------------------------------------------------------------ helpers

int GetCollisionMaterial(const S_CollisionVolume *volume)
{
    if (!volume)
        return -1;
    const uint32_t material = volume->flags >> 24;
    return material < g_materialCount ? static_cast<int>(material) : -1;
}

bool IsSingleStageAnim(int anim)
{
    return (anim >= 0 && anim <= 3) || anim == 0x2C || anim == 0x2B || (anim >= 0x1E && anim <= 0x21) ||
           (anim >= 8 && anim <= 0x0E) || anim == 0x10 || anim == 7 || (anim >= 0x12 && anim <= 0x18) ||
           (anim >= 0x1A && anim <= 0x1D) || (anim >= 0x24 && anim <= 0x26) || anim == 0x9C || anim == 0x67 ||
           anim == 0x9D || anim == 0xA0 || anim == 0xA1 || (anim >= 0x5E && anim <= 0x61) ||
           (anim >= 0x2F && anim <= 0x35) || (anim >= 0x39 && anim <= 0x3D) || (anim >= 0xA2 && anim <= 0xA5) ||
           (anim >= 0x71 && anim <= 0x9A);
}

bool IsSingleStageCarryAnim(int carryAnim)
{
    return carryAnim != 0x1F && carryAnim != 0x1E && carryAnim != 0x20 && carryAnim != 0x57 && carryAnim != 0x58 &&
           carryAnim != 0x5B;
}

bool IsSingleStageActivity(int activity)
{
    return activity != 0x19 && activity != 0x34 && activity != 0x47 && activity != 0x48 && activity != 0x4B &&
           activity != 0x41 && activity != 0x42 && activity != 0x4A;
}

// ------------------------------------------------------------------ lifetime

void C_HumanMovement::SetOwnerBase(C_Human *owner, C_HumanWeaponPose *weaponPose)
{
    m_owner = owner;
    m_weaponPose2 = weaponPose;
    m_flags = MOVE_FLAGS_INITIAL;
    m_stanceVolumeCount = 0;
    m_hitVolumeCount = 0;
    m_trailCount = 0;
    m_unknown18C = true;
}

void C_HumanMovement::ResetBase()
{
    UpdateDummies();
    m_unknown18C = true;
    m_clock = 0;
    m_unknown1BC = 0;
    m_animProfileTime = 0;
    m_physicsProfileTime = 0;
    m_stanceVolumeScale = 0.8f;
    m_hitVolumeScale[0] = 1.0f;
    m_hitVolumeScale[1] = 1.0f;
    m_flags = MOVE_FLAGS_INITIAL;
    m_footstepSurface = 0;
    m_animState.anim.Set(ANIM_STAND, HUMAN_STANCE_STAND, 1);
    m_animState.carryAnim = CARRY_ANIM_NONE;
    m_animState.carryVariant = 0;
    m_animState.activity = ACTIVITY_NONE;
    m_animState.activityVariant = 0;
    m_nextAnimState.anim.anim = ANIM_NONE;
    m_nextAnimState.anim.Normalize();
    m_nextAnimState.carryAnim = CARRY_ANIM_NONE;
    m_nextAnimState.activity = ACTIVITY_NONE;
    m_unknown220 = 0;
    m_unknown98C = false;
    m_parachuting = false;
    m_useState = MOVE_NO_TIME;
    m_unknown998 = MOVE_NO_TIME;
    m_stillTime = 0;
    m_unknown950 = 0;
    m_unknown250 = 0.0f;
    m_unknown254 = 0.0f;
    m_carried = false;
    m_unknown9D0.x = m_unknown9D0.y = m_unknown9D0.z = 0.0f;
    m_unknown9B8 = MOVE_NO_TIME;
    m_unknown9BC = MOVE_NO_TIME;
    m_unknown9C0 = MOVE_NO_TIME;
    m_unknown9C4 = MOVE_NO_TIME;
    m_unknown9CC = MOVE_NO_TIME;
    m_unknown9E0 = 3;
    m_unknown1AC = 1;
    m_unknown1B0 = 1;
    m_unknown9F0.x = m_unknown9F0.y = m_unknown9F0.z = MOVE_NO_VALUE;
    m_unknownA18 = 0;
    m_unknownA14 = -1;
    m_unknown9AC = MOVE_NO_TIME;
    m_groundMaterial = -1;
    m_ground = nullptr;
    m_slideFactor = 0;
    m_damageMaterial = -1;
    m_unknown760 = false;
    m_onDynamicGround = false;
    m_onGround = false;
    m_unknown764 = -1;
    m_unknown96C = false;
    m_unknown9B0 = MOVE_NO_TIME;
    m_unknownA24 = 0;
    m_unknownA28 = MoveTime(150.0f);
    m_unknown940.x = m_unknown940.y = m_unknown940.z = 0.0f;
    m_useObject = nullptr;
    m_unknownA2C = MOVE_NO_TIME;
    m_unknownA30 = MOVE_NO_TIME;
    m_unknown224 = true;
    m_useSound = -1;
    m_unknownA1C = MOVE_NO_TIME;
    m_handsCarryState = 0;
    m_bloodZone = 5;
    m_bloodFrame = nullptr;
    m_interaction = 0;
    m_interactionTime = MOVE_NO_TIME;
    m_vehicle = nullptr;
    m_seat = 0;
    m_seatMode = 0;
    m_unknown240.x = m_unknown240.y = m_unknown240.z = MOVE_NO_VALUE;
    m_footstepCount = -1;
    m_footstepTime = 0;
    m_unknown190 = 0;
    m_inUpdate = false;
    m_unknown9C8 = false;
    m_unknown9C9 = false;
    m_swimBobPhase = 0.0f;
    m_breathTime = 0;
    m_gaspTime = 0;
    I3D_frame *model = m_owner->GetFrame();
    m_shotDummies[0] = model->FindChildFrame(SHOT_DUMMY_NAMES[0], FRAME_MASK_DUMMY);
    m_shotDummies[1] = model->FindChildFrame(SHOT_DUMMY_NAMES[1], FRAME_MASK_DUMMY);
    m_stanceChange = MOVE_NO_TIME;
    m_unknown9A0 = MOVE_NO_TIME;
    m_unknown9B4 = MOVE_NO_TIME;
    m_nextStance = STANCE_NONE;
    m_nextStance2 = STANCE_NONE;
    GetSoundModule()->SetChannelLimit(3, 4);
    m_request.Reset();
    m_remoteActions.clear();
}

void C_HumanMovement::InitBase()
{
    I3D_frame *model = m_owner->GetFrame();
    for (int i = 0; i < STANCE_DUMMY_COUNT; ++i) {
        I3D_frame *dummy = model->FindChildFrame(STANCE_DUMMY_NAMES[i], FRAME_MASK_ANY);
        m_stanceDummies[i] = dummy && dummy->GetType() == FRAME_DUMMY ? dummy : nullptr;
    }
    ResetBase();
    m_trailTime = 0;
    m_trailCount = 0;
    m_stanceVolumeCount = 0;
    m_hitVolumeCount = 0;
    for (int i = 0; i < 4; ++i) {
        m_stanceVolumes[i] = NewBodyVolume(m_owner, COLLISION_SHAPE_SPHERE, VOLUME_TYPE_STANCE);
        m_trailVolumes[i] = NewBodyVolume(m_owner, COLLISION_SHAPE_SPHERE, VOLUME_TYPE_STANCE);
    }
    if (!m_owner->GetMovement()->m_detached) {
        // Standing.
        m_stanceVolumeCount = 0;
        UpdateStanceVolumes(HUMAN_STANCE_STAND);
        for (int i = 0; i < m_stanceVolumeCount; ++i) {
            SetVolumeSphere(m_stanceVolumes[i], m_stanceSpheres[i], m_stanceVolumeScale);
            SetVolumeSphere(m_trailVolumes[i], m_stanceSpheres[i], 1.0f);
        }
    }
    m_hitVolumeCount = 0;
    UpdateHitSpheres();
    for (int i = 0; i < 2; ++i) {
        m_hitVolumes[i] = NewBodyVolume(m_owner, HIT_VOLUME_FLAGS, VOLUME_TYPE_HIT);
        SetVolumeSphere(m_hitVolumes[i], m_hitSpheres[i], m_hitVolumeScale[i]);
        if (!m_owner->GetMovement()->m_detached)
            g_collisionWorld.AddVolume(m_hitVolumes[i]);
    }
    for (int i = 0; i < HUMAN_JOINT_COUNT; ++i) {
        m_joints[i].frame = model->FindChildFrame(HUMAN_JOINT_NAMES[i], FRAME_MASK_ANY);
        m_joints[i].unknown04 = false;
    }
    m_morph.Init(m_owner);
    m_morph.SetBlinking(true);
    SetXYZ(m_unknown740, XYZ(m_stanceSpheres[2]) - m_owner->GetPosition());
    m_timer200 = std::rand() % 200;
    m_timer300 = std::rand() % 300;
    m_timer400 = std::rand() % 400;
    m_timer600 = std::rand() % 600;
    ++g_movementBases;
}

void C_HumanMovement::DoneBase()
{
    UpdateSounds(true);
    StopUseSound();
    m_morph.Done();
    m_stanceVolumeScale = 0.8f;
    m_hitVolumeScale[0] = 1.0f;
    m_hitVolumeScale[1] = 1.0f;
    for (int i = 0; i < 4; ++i) {
        g_collisionWorld.RemoveVolume(m_stanceVolumes[i]);
        DeleteBodyVolume(m_stanceVolumes[i]);
        if (m_trailTime > 0)
            g_collisionWorld.RemoveVolume(m_trailVolumes[i]);
        DeleteBodyVolume(m_trailVolumes[i]);
    }
    m_remoteActions.clear();
    for (S_CollisionVolume *&volume : m_hitVolumes) {
        g_collisionWorld.RemoveVolume(volume);
        DeleteBodyVolume(volume);
    }
    m_stanceVolumeCount = -1;
    --g_movementBases;
}

void C_HumanMovement::RestartBase()
{
    UpdateSounds(true);
    StopUseSound();
    m_morph.Done();
    m_morph.Init(m_owner);
    m_morph.SetBlinking(true);
    SurfaceMarks_Clear();
    m_stanceVolumeScale = 0.8f;
    m_hitVolumeScale[0] = 1.0f;
    m_hitVolumeScale[1] = 1.0f;
    ResetBase();
    for (int i = 0; i < 4; ++i) {
        if (i < m_stanceVolumeCount)
            g_collisionWorld.RemoveVolume(m_stanceVolumes[i]);
        if (m_trailTime > 0 && i < m_trailCount)
            g_collisionWorld.RemoveVolume(m_trailVolumes[i]);
    }
    for (S_CollisionVolume *volume : m_hitVolumes)
        g_collisionWorld.RemoveVolume(volume);
    m_trailTime = 0;
    m_trailCount = 0;
    m_hitVolumeCount = 0;
    if (!m_owner->GetMovement()->m_detached) {
        UpdateHitSpheres();
        m_stanceVolumeCount = 0;
        UpdateStanceVolumes(HUMAN_STANCE_STAND);
        for (int i = 0; i < m_stanceVolumeCount; ++i) {
            SetVolumeSphere(m_stanceVolumes[i], m_stanceSpheres[i], m_stanceVolumeScale);
            SetVolumeSphere(m_trailVolumes[i], m_stanceSpheres[i], 1.0f);
        }
        for (int i = 0; i < m_hitVolumeCount; ++i) {
            SetVolumeSphere(m_hitVolumes[i], m_hitSpheres[i], m_hitVolumeScale[i]);
            g_collisionWorld.AddVolume(m_hitVolumes[i]);
        }
    }
    m_timer200 = std::rand() % 200;
    m_timer300 = std::rand() % 300;
    m_timer400 = std::rand() % 400;
    m_timer600 = std::rand() % 600;
}

// ------------------------------------------------------------------ dummies and volumes

void C_HumanMovement::UpdateHitSpheres()
{
    if (m_flags & MOVE_FLAG_FROZEN)
        return;
    m_hitVolumeCount = 2;
    for (int i = 0; i < 2; ++i) {
        const I3D_frame *dummy = m_shotDummies[i];
        SetXYZ(m_hitSpheres[i], dummy->GetWorldPos());
        m_hitSpheres[i].w = DummyRadius(dummy);
        SetXYZ(m_hitOffsets[i], XYZ(m_hitSpheres[i]) - m_owner->GetFrame()->GetWorldPos());
    }
    if (m_owner->GetHealth() < 1) {
        // A dead body lies: smaller spheres moved apart along it.
        const S_vector along = (XYZ(m_hitSpheres[0]) - XYZ(m_hitSpheres[1])).Normalized();
        m_hitSpheres[0].w *= 0.5f;
        SetXYZ(m_hitSpheres[0], XYZ(m_hitSpheres[0]) - along * 0.4f);
        m_hitSpheres[1].w *= 0.5f;
        SetXYZ(m_hitSpheres[1], XYZ(m_hitSpheres[1]) + along * 0.15f);
    }
}

void C_HumanMovement::MoveHitSpheres(const S_vector &pos)
{
    m_hitVolumeCount = 2;
    for (int i = 0; i < 2; ++i)
        SetXYZ(m_hitSpheres[i], XYZ(m_hitOffsets[i]) + pos);
}

void C_HumanMovement::UpdateStanceVolumes(int stance)
{
    int stanceA = stance;
    int stanceB = stance;
    const int previousHits = m_hitVolumeCount;
    int previous = m_stanceVolumeCount;
    if (m_flags & MOVE_FLAG_FROZEN)
        return;
    if (previous < 0)
        previous = 0;
    // A stance change under way (set by the locomotion) takes the next stance's spheres.
    if (m_stanceChange == 0) {
        if (m_nextStance != STANCE_NONE) {
            stanceB = STANCE_NONE;
            stanceA = m_nextStance;
        }
        if (m_nextStance2 != STANCE_NONE)
            stanceB = m_nextStance2;
        m_stanceChange = m_unknown9A0 != MOVE_NO_TIME ? -1 : MOVE_NO_TIME;
    } else if (m_stanceChange == -1) {
        if (m_unknown9A0 == 0) {
            m_stanceChange = MOVE_NO_TIME;
            m_unknown9A0 = MOVE_NO_TIME;
        } else {
            if (m_nextStance != STANCE_NONE)
                stanceA = m_nextStance;
            stanceB = m_nextStance2;
        }
    }

    // Fix: the original gathers the spheres straight into m_stanceSpheres (4 of them), which
    // two stances at once would overrun, and indexes it with the -10000 of a stance whose
    // dummies are missing; they are gathered here and at most 4 used.
    S_vector4 spheres[STANCE_DUMMY_COUNT];
    int count = 0;
    if (stanceA == HUMAN_STANCE_STAND || stanceB == HUMAN_STANCE_STAND) {
        count = 3;
        if (GetStanceSpheres(HUMAN_STANCE_STAND, spheres) != 3)
            count = -10000;
    }
    if (stanceA == HUMAN_STANCE_CROUCH || stanceB == HUMAN_STANCE_CROUCH) {
        const int at = count;
        count += 2;
        if (at < 0 || GetStanceSpheres(HUMAN_STANCE_CROUCH, spheres + at) != 2)
            count = -10000;
    }
    if (stanceA == HUMAN_STANCE_PRONE || stanceB == HUMAN_STANCE_PRONE) {
        if (count >= 0 && GetStanceSpheres(HUMAN_STANCE_PRONE, spheres + count) == 4)
            count += 4;
        else
            count = -10000;
    }
    for (int i = 0; i < std::min(count, 4); ++i)
        m_stanceSpheres[i] = spheres[i];

    // Only a body that pushes (states 1, 7, 8) has stance volumes.
    const int pushState = m_unknown1AC;
    if (pushState != 1 && pushState != 7 && pushState != 8)
        count = 0;
    const int anim = m_animState.anim.anim;
    if (anim > 0x70 && anim < 0x9B && (m_unknown9B8 == 0 || m_unknown9B8 == MOVE_NO_TIME) &&
        m_unknown9F0.x != MOVE_NO_VALUE) {
        m_unknown9F0.x = MOVE_NO_VALUE;
        m_unknown9F0.y = 0.0f;
        m_unknown9F0.z = 0.0f;
        if (pushState < 7)
            SetPushState(0, false);
    }
    if (m_stanceVolumeScale == 0.0f)
        count = 0;
    if (m_hitVolumeScale[0] == 0.0f)
        m_hitVolumeCount = 0;
    m_stanceVolumeCount = std::min(std::max(count, 0), 4);

    for (int i = previous; i < m_stanceVolumeCount; ++i)
        g_collisionWorld.AddVolume(m_stanceVolumes[i]);
    for (int i = m_stanceVolumeCount; i < previous; ++i)
        g_collisionWorld.RemoveVolume(m_stanceVolumes[i]);
    for (int i = previousHits; i < m_hitVolumeCount; ++i)
        g_collisionWorld.AddVolume(m_hitVolumes[i]);
    for (int i = m_hitVolumeCount; i < previousHits; ++i)
        g_collisionWorld.RemoveVolume(m_hitVolumes[i]);
}

int C_HumanMovement::GetStanceSpheres(int stance, S_vector4 *out) const
{
    int first = STANCE_DUMMY_STAND;
    int count = 3;
    if (stance == HUMAN_STANCE_CROUCH) {
        first = STANCE_DUMMY_KNEEL;
        count = 2;
    } else if (stance == HUMAN_STANCE_PRONE) {
        first = STANCE_DUMMY_LIE;
        count = 4;
    }
    int found = count;
    for (int i = 0; i < count; ++i) {
        if (!m_stanceDummies[first + i]) {
            --found;
            continue;
        }
        const S_vector4 &pos = m_dummyPos[first + i];
        out[i] = {pos.x, pos.y, pos.z, m_dummyRadius[first + i]};
    }
    return found;
}

void C_HumanMovement::UpdateDummies()
{
    for (int i = 0; i < STANCE_DUMMY_COUNT; ++i) {
        const I3D_frame *dummy = m_stanceDummies[i];
        if (!dummy)
            continue;
        const S_matrix &matrix = dummy->GetMatrix();
        m_dummyPos[i] = {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2], matrix.m[3][3]};
        SetXYZ(m_dummyOffset[i], XYZ(m_dummyPos[i]) - m_owner->GetFrame()->GetWorldPos());
        m_dummyRadius[i] = DummyRadius(dummy);
    }
}

void C_HumanMovement::UpdateLieDummies()
{
    for (int i = STANCE_DUMMY_LIE; i < STANCE_DUMMY_COUNT; ++i) {
        const I3D_frame *dummy = m_stanceDummies[i];
        if (!dummy)
            continue;
        const S_matrix &matrix = dummy->GetMatrix();
        m_dummyPos[i] = {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2], matrix.m[3][3]};
        SetXYZ(m_dummyOffset[i], XYZ(m_dummyPos[i]) - m_owner->GetFrame()->GetWorldPos());
        m_dummyRadius[i] = DummyRadius(dummy);
    }
}

void C_HumanMovement::MoveDummies(const S_vector &pos)
{
    for (int i = 0; i < STANCE_DUMMY_COUNT; ++i) {
        if (m_stanceDummies[i])
            SetXYZ(m_dummyPos[i], XYZ(m_dummyOffset[i]) + pos);
    }
}

void C_HumanMovement::OnFrozen(bool on)
{
    if (on) {
        for (int i = 0; i < 4; ++i) {
            if (i < m_stanceVolumeCount)
                g_collisionWorld.RemoveVolume(m_stanceVolumes[i]);
        }
        m_stanceVolumeCount = 0;
        for (int i = 0; i < m_hitVolumeCount; ++i)
            g_collisionWorld.RemoveVolume(m_hitVolumes[i]);
        m_hitVolumeCount = 0;
        if (m_shadow)
            m_shadow->SetOn(false);
        return;
    }
    UpdateDummies();
    UpdateStanceVolumes(m_owner->GetWork()->GetStance());
    for (int i = 0; i < m_stanceVolumeCount; ++i) {
        SetVolumeSphere(m_stanceVolumes[i], m_stanceSpheres[i], m_stanceVolumeScale);
        g_collisionWorld.MoveVolume(m_stanceVolumes[i]);
    }
    UpdateHitSpheres();
    for (int i = 0; i < m_hitVolumeCount; ++i) {
        S_CollisionVolume *volume = m_hitVolumes[i];
        SetVolumeSphere(volume, m_hitSpheres[i], m_hitVolumeScale[i]);
        // (Without the material the hit volumes were made with, as in the original.)
        volume->flags = COLLISION_SHAPE_SPHERE;
        volume->type = VOLUME_TYPE_HIT_PLAIN;
        if (m_owner->GetHealth() < 1)
            volume->type |= VOLUME_TYPE_DEAD;
        g_collisionWorld.AddVolume(volume);
    }
    if (m_shadow)
        m_shadow->SetOn(true);
}

void C_HumanMovement::SnapToGround()
{
    I3D_frame *model = m_owner->GetFrame();
    I3D_frame *parent = model->GetParent();
    if (parent && parent->GetType() != FRAME_SECTOR)
        return;
    C_HumanWork *work = m_owner->GetWork();
    const int workMode = work->GetUnknownB8();
    const int anim = m_animState.anim.anim;
    if (workMode == 5 || m_parachuting || workMode == 2 || workMode == 3 || anim == 0x9F || anim == 0x9E ||
        anim == 0x9B || anim == 0x6F || anim == 0x22 || m_owner->GetHealth() < 1)
        return;
    const int stance = work->GetStance();
    const float lift = stance == HUMAN_STANCE_STAND ? 1.0f : stance == HUMAN_STANCE_PRONE ? 0.5f : 0.75f;
    RemoveCollision(false);
    // A sphere of 0.3 m dropped from up to 3 m above.
    S_vector hit(0.0f, 0.0f, 0.0f);
    S_vector normal;
    S_CollisionVolume *ground = nullptr;
    for (int i = 0; i <= 2 && !ground; ++i) {
        const S_vector4 &pos = m_owner->GetPosition4();
        const S_vector from(pos.x, pos.y + static_cast<float>(i) + lift, pos.z);
        ground = CollisionSphereTest(from, S_vector(0.0f, -5.0f, 0.0f), 0.3f, &hit, &normal, 0xC8102, 0x230000);
    }
    if (ground) {
        hit.y -= 0.3f;
        const S_vector4 &pos = m_owner->GetPosition4();
        if (pos.y < hit.y || std::fabs(pos.y - hit.y) > 0.5f) {
            m_owner->SetPosition(hit, pos.w);
            model->SetPos(hit);
            model->Update();
            UpdateDummies();
            UpdateHitSpheres();
        }
    }
    RestoreCollision(false);
}

void C_HumanMovement::ClearFlags(uint32_t flags)
{
    const uint32_t old = m_flags;
    m_flags = ~flags & old;
    if ((flags & MOVE_FLAG_FROZEN) && (old & MOVE_FLAG_FROZEN))
        OnFrozen(false);
    if ((m_flags & 6) == 0 && (old & 6) != 0 && m_flags != old)
        SnapToGround();
}

void C_HumanMovement::RemoveCollision(bool remove)
{
    if (remove) {
        for (int i = 0; i < m_stanceVolumeCount; ++i)
            g_collisionWorld.RemoveVolume(m_stanceVolumes[i]);
        for (int i = 0; i < m_hitVolumeCount; ++i)
            g_collisionWorld.RemoveVolume(m_hitVolumes[i]);
        for (int i = 0; i < m_trailCount; ++i)
            g_collisionWorld.RemoveVolume(m_trailVolumes[i]);
        return;
    }
    uint32_t count = 0;
    for (int i = 0; i < m_stanceVolumeCount; ++i)
        g_ignoredVolumes[count++] = m_stanceVolumes[i];
    for (int i = 0; i < m_hitVolumeCount; ++i)
        g_ignoredVolumes[count++] = m_hitVolumes[i];
    for (int i = 0; i < m_trailCount; ++i)
        g_ignoredVolumes[count++] = m_trailVolumes[i];
    g_collisionWorld.SetIgnoredVolumes(g_ignoredVolumes, count, 0);
}

void C_HumanMovement::RestoreCollision(bool remove)
{
    if (!remove) {
        g_collisionWorld.SetIgnoredVolume(nullptr, 0);
        return;
    }
    for (int i = 0; i < m_stanceVolumeCount; ++i)
        g_collisionWorld.AddVolume(m_stanceVolumes[i]);
    for (int i = 0; i < m_hitVolumeCount; ++i)
        g_collisionWorld.AddVolume(m_hitVolumes[i]);
    for (int i = 0; i < m_trailCount; ++i)
        g_collisionWorld.AddVolume(m_trailVolumes[i]);
}

// ------------------------------------------------------------------ work modes, model

void C_HumanMovement::SetMorph(int which, uint32_t value)
{
    if (which == 1)
        m_morph.SetUnknown1(static_cast<int>(value));
    else if (which == 2)
        m_morph.SetUnknown2(static_cast<int>(value));
    else if (which == 3)
        m_morph.SetUnknown3(static_cast<int>(value));
}

void C_HumanMovement::SetSeatMode(int mode, bool on)
{
    C_HumanWork *work = m_owner->GetWork();
    switch (mode) {
    case 0:
        if (on) {
            work->SetUnknownB8(1);
            work->SetFlagsD0(0x8, true);
            work->SetFlagsD0(0x800, true);
        } else {
            work->SetUnknownB8(0);
            work->SetFlagsD0(0x8, false);
            work->SetFlagsD0(0x800, false);
            m_useState = MOVE_NO_TIME;
        }
        break;
    case 1:
    case 2:
        if (on) {
            work->Play(mode == 1 ? 0x22 : 0x23, true);
            work->SetUnknownB8(mode + 1);
            work->SetFlagsD0(0x8, true);
            work->SetFlagsD0(0x10, true);
            work->SetFlagsD0(0x800, true);
        } else {
            work->SetUnknownB8(0);
            work->Play(ANIM_STAND, true);
            work->SetFlagsD0(0x8, false);
            work->SetFlagsD0(0x10, false);
            work->SetFlagsD0(0x800, false);
        }
        break;
    case 3:
        if (on) {
            work->SetUnknownB8(4);
            work->SetFlagsD0(0x8, true);
            work->SetFlagsD0(0x10, true);
            work->SetFlagsD0(0x2, true);
        } else {
            work->SetUnknownB8(0);
            work->SetFlagsD0(0x8, false);
            work->SetFlagsD0(0x10, false);
            work->SetFlagsD0(0x2, false);
        }
        break;
    case 4:
        work->SetUnknownB8(on ? 5 : 0);
        work->SetFlagsD0(0x8, on);
        break;
    default:
        break;
    }
}

void C_HumanMovement::RelinkModel(I3D_frame *model, I3D_frame *previous)
{
    m_shotDummies[0] = model->FindChildFrame(SHOT_DUMMY_NAMES[0], FRAME_MASK_DUMMY);
    m_shotDummies[1] = model->FindChildFrame(SHOT_DUMMY_NAMES[1], FRAME_MASK_DUMMY);
    // (The stance dummies are looked up in the owner's frame, as in the original.)
    for (int i = 0; i < STANCE_DUMMY_COUNT; ++i) {
        I3D_frame *dummy = m_owner->GetFrame()->FindChildFrame(STANCE_DUMMY_NAMES[i], FRAME_MASK_ANY);
        m_stanceDummies[i] = dummy && dummy->GetType() == FRAME_DUMMY ? dummy : nullptr;
    }
    for (int i = 0; i < HUMAN_JOINT_COUNT; ++i)
        m_joints[i].frame = model->FindChildFrame(HUMAN_JOINT_NAMES[i], FRAME_MASK_ANY);
    for (int i = 0; i < 4; ++i) {
        m_stanceVolumes[i]->frame = model;
        m_trailVolumes[i]->frame = model;
    }
    for (int i = 0; i < m_hitVolumeCount; ++i)
        m_hitVolumes[i]->frame = model;
    m_morph.Relink(model);
    m_mixer.Relink(static_cast<I3D_model *>(model), static_cast<I3D_model *>(previous));
    m_owner->GetEquipment()->FindJoints(model);
}

void C_HumanMovement::RelinkJoints()
{
    I3D_frame *model = m_owner->GetFrame();
    for (int i = 0; i < HUMAN_JOINT_COUNT; ++i)
        m_joints[i].frame = model->FindChildFrame(HUMAN_JOINT_NAMES[i], FRAME_MASK_ANY);
    RelinkModel(model, model);
}

void C_HumanMovement::SetPushState(int state, bool both)
{
    m_unknown1AC = state;
    if (both)
        m_unknown1B0 = state;
    if (state != 1)
        m_unknown18C = true;
    if (state == 0)
        m_morph.ShowDeathFace();
}

void C_HumanMovement::SetPosition(const S_vector4 &pos)
{
    I3D_frame *model = m_owner->GetFrame();
    model->SetPos(XYZ(pos));
    m_owner->SetPosition(XYZ(pos), pos.w);
    m_hitVolumeScale[0] = 1.0f;
    m_hitVolumeScale[1] = 1.0f;
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    m_owner->GetMovement()->m_weaponPose->StartDeath(8, zero, 0, 0, nullptr);
    model->Update();
}

bool C_HumanMovement::IsLeftFootForward() const
{
    const S_vector &velocity = m_owner->GetVelocity();
    const S_vector ahead = velocity + velocity + m_owner->GetFrame()->GetWorldPos();
    const float left = (ahead - m_joints[HUMAN_JOINT_U].frame->GetWorldPos()).Square();
    const float right = (ahead - m_joints[HUMAN_JOINT_Q].frame->GetWorldPos()).Square();
    return !(right < left);
}

// ------------------------------------------------------------------ footsteps

void C_HumanMovement::Footstep(bool left)
{
    S_SoundRequest request;
    if (!IsClient() && !m_owner->IsServerSimulated())
        return;
    if (m_footstepSurface > 0)
        --m_footstepSurface;
    else if (m_footstepSurface < 0)
        ++m_footstepSurface;
    I3D_frame *foot = m_joints[left ? HUMAN_JOINT_U : HUMAN_JOINT_Q].frame;
    const S_vector dir = foot->GetWorldDir();
    const S_vector footPos = foot->GetWorldPos();

    // The ground under the foot.
    int material;
    if (g_surfaceMarksEnabled == 1 && m_groundMaterial == -1 &&
        (!IsLocalGame() || m_owner->IsLocalPlayer() || m_owner->IsServerSimulated())) {
        RemoveCollision(false);
        const S_vector from(footPos.x + dir.x * 0.08f, footPos.y + 0.15f + dir.y * 0.08f, footPos.z + dir.z * 0.08f);
        S_vector hit;
        S_vector normal;
        S_CollisionVolume *ground = CollisionRayTest(from, S_vector(0.0f, -0.5f, 0.0f), &hit, &normal, 0xFFFFFFFF,
                                                     0x230000);
        RestoreCollision(false);
        if (!ground)
            return;
        material = GetCollisionMaterial(ground);
        m_ground = ground;
    } else {
        material = m_groundMaterial;
    }

    // Walking steps (sub-variant 0, not lying) are quiet unless the human fails a stealth roll.
    bool noisy = false;
    if (m_animState.anim.sub == 0 && m_animState.anim.stance != HUMAN_STANCE_PRONE) {
        ++m_footstepCount;
        const int stealth = m_owner->GetStat(STAT_STEALTH);
        if (static_cast<float>(stealth) * 0.1f < static_cast<float>(m_footstepCount) &&
            RandFloat() < static_cast<float>(100 - stealth) * 0.001f)
            noisy = true;
    } else {
        m_footstepCount = -1;
    }

    if (material >= 0 && (m_animState.anim.sub != 0 || noisy) && g_materials[material].footstepSound >= 0) {
        request.pos = S_vector(0.0f, 0.2f, 0.0f);
        request.attachTo = foot;
        request.group = g_materials[material].footstepSound;
        request.source = m_owner;
        request.chapter = 0;
        request.nearScale = 1.0f;
        I3D_sector *sector = m_owner->GetMovement()->m_visual.m_sector;
        const bool primary = sector && sector == GetGame()->GetScene()->GetPrimarySector();
        // Range and volume by the speed (the work's state) and stance; louder outdoors.
        const int stance = m_animState.anim.stance;
        switch (m_owner->GetWork()->GetGait()) {
        case 0:
            if (stance == HUMAN_STANCE_STAND) {
                request.farScale = primary ? 0.3f : 0.25f;
                request.volume = 0.8f;
            } else if (stance == HUMAN_STANCE_CROUCH) {
                request.farScale = primary ? 0.2f : 0.15f;
                request.volume = 0.7f;
            } else {
                request.farScale = 0.1f;
                request.volume = 0.6f;
            }
            break;
        case 1:
        case 3:
        case 4:
            if (stance == HUMAN_STANCE_STAND) {
                request.farScale = primary ? 0.7f : 0.35f;
                request.volume = 0.8f;
            } else if (stance == HUMAN_STANCE_CROUCH) {
                request.farScale = primary ? 0.5f : 0.25f;
                request.volume = 0.7f;
            } else {
                request.farScale = 0.4f;
                request.volume = 0.7f;
            }
            break;
        case 2:
            if (stance == HUMAN_STANCE_STAND)
                request.farScale = primary ? 0.9f : 0.5f;
            else if (stance == HUMAN_STANCE_CROUCH)
                request.farScale = primary ? 0.7f : 0.4f;
            else
                request.farScale = 0.5f;
            request.volume = 0.8f;
            break;
        case 5:
            request.farScale = primary ? 1.0f : 0.5f;
            request.volume = 1.0f;
            break;
        default:
            break;
        }
        if (m_owner->IsLocalPlayer() && GetGame()->IsFirstPersonView()) {
            // The player's own steps a bit ahead of the foot.
            const S_vector forward = foot->GetLocalDir();
            request.pos.x += forward.x * 0.6666667f;
            request.pos.z += forward.z * 0.6666667f;
        }
        // At most one step a second (and the noisy ones) is heard by the AI.
        if (m_footstepTime > 999 || noisy) {
            m_footstepTime = 0;
            request.flags = IsClient() ? SOUND_AI_OCCLUSION : SOUND_AI_OCCLUSION | SOUND_AI_ONLY;
        } else {
            if (!IsClient())
                return;
            request.flags = SOUND_SILENT_TO_AI;
        }
        if (stance == HUMAN_STANCE_PRONE)
            request.flags |= SOUND_AI_ONLY;
        request.channel = 3;
        GetSoundModule()->Play(request);
    }

    if (!IsClient())
        return;
    C_HumanWork *work = m_owner->GetWork();
    C_HumanMovement *movement = m_owner->GetMovement();
    if ((movement->m_flags & MOVE_FLAG_FULL_DETAIL) && !m_owner->IsLocalPlayer() && (material == 0x29 || material == 0x2A)) {
        // Dust of a remote human's step on sand, more often the faster it goes.
        float roll;
        switch (work->GetGait()) {
        case 0:
            roll = RandFloat() - 0.1f;
            break;
        case 1:
            roll = RandFloat() + 0.05f;
            break;
        case 2:
            roll = RandFloat() + 0.25f;
            break;
        default:
            roll = RandFloat();
            break;
        }
        if (roll > 0.85f) {
            const S_vector4 &facing = work->GetUnknown10();
            const S_vector back(-facing.x, -facing.y, -facing.z);
            g_particlesModule.CreateAt(PARTICLE_STEP_DUST, footPos - dir * 0.3f, back, nullptr, true, false);
        }
    }

    // The footprint.
    if (work->GetStance() == HUMAN_STANCE_PRONE || (movement->m_flags & MOVE_FLAG_TINY) || !g_surfaceMarksEnabled)
        return;
    if (SurfaceMarks_IsBloodAt(footPos))
        m_footstepSurface = 10;
    int printMaterial;
    if (m_footstepSurface > 0) {
        printMaterial = m_footstepSurface > 5 ? -2 : -3;
    } else if (m_footstepSurface < 0) {
        printMaterial = m_footstepSurface < -8 ? -4 : -5;
    } else {
        if (material == -1)
            return;
        printMaterial = material;
    }
    const int kind = SurfaceMarks_FindKind(printMaterial);
    if (kind < 0)
        return;
    if (movement->m_locomotion.m_waterHeight != MOVE_NO_VALUE || m_owner->IsFadedOut())
        return;
    S_SurfaceMark *mark = SurfaceMarks_NewMark(1);
    if (!mark->visual)
        mark->visual = I3DGetDriver()->CreateVisual(I3DVISUAL_PROJECTOR);
    float brightness = movement->m_visual.m_lighting.m_intensity;
    if (brightness > g_footprintBrightness)
        brightness = g_footprintBrightness;
    else if (brightness < 0.15f)
        brightness = 0.15f;
    mark->left = left;
    mark->kind = kind;
    auto *projector = static_cast<I3D_projector_visual *>(mark->visual);
    if (printMaterial == -2 || printMaterial == -3)
        projector->SetIntensity(static_cast<float>(m_footstepSurface) * 0.2f + 0.1f);
    else if (printMaterial == -4 || printMaterial == -5)
        projector->SetIntensity(0.5f - static_cast<float>(m_footstepSurface) * 0.0625f);
    else
        projector->SetIntensity(1.0f);
    projector->SetColor(S_vector(brightness, brightness, brightness));
    const float flatDir[2] = {dir.x, dir.z};
    const float north[2] = {0.0f, 1.0f};
    const float angle = AngleBetween2D(flatDir, north);
    S_vector printPos = footPos;
    printPos.x += dir.x * 0.08f;
    printPos.z += dir.z * 0.08f;
    // (The model is hidden while the mark is placed.)
    I3D_frame *model = m_owner->GetFrame();
    const bool wasOn = model->IsOn();
    if (wasOn)
        model->SetOn(false);
    mark->humanId = m_owner->GetNetId();
    SurfaceMarks_AddFootprint(mark, printPos, angle, movement->m_visual.m_sector);
    if (wasOn)
        model->SetOn(true);
}

// ------------------------------------------------------------------ carrying bodies

void C_HumanMovement::OnPickUpBody(C_Human *body)
{
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    equipment->PutAwayHandsItem(true);
    if (I3D_frame *hands = equipment->GetModel(EQUIPMENT_PLACE_HANDS))
        hands->SetOn(false);
    if (I3D_frame *longGun = equipment->GetModel(EQUIPMENT_PLACE_LONG_GUN))
        longGun->SetOn(false);
    m_unknownA30 = 1;
    equipment->SetUnknownD0(1.0f);
    C_HumanWork *work = m_owner->GetWork();
    work->SetUnknownB0(true);
    m_nextAnimState.carryAnim = 0x58;
    work->SetUnknownB0(false);
    if (body) {
        C_HumanMovement *bodyMovement = body->GetMovement();
        bodyMovement->SetPushState(0, true);
        bodyMovement->m_hitVolumeScale[0] = 0.0f;
        bodyMovement->m_hitVolumeScale[1] = 0.0f;
        bodyMovement->UpdateStanceVolumes(body->GetWork()->GetStance());
        if (C_Actor *vehicle = body->GetVehicle()) {
            switch (vehicle->GetType()) {
            case ACTOR_CAR:
            case ACTOR_SUBMARINE:
            case ACTOR_STATIC_WEAPON:
            case ACTOR_JUNKERS88: {
                // Out of the vehicle's seat and back into the scene.
                I3D_frame *bodyModel = body->GetFrame();
                I3D_frame *parent = bodyModel->GetParent();
                if (parent && parent->GetType() != FRAME_SECTOR && bodyMovement->m_vehicle) {
                    bodyMovement->m_vehicle->LeaveSeat(bodyMovement->m_seat);
                    I3D_frame *frameZ = bodyModel->FindChildFrame(SCENE_FRAME_Z, FRAME_MASK_ANY);
                    I3D_frame *frameQ = bodyModel->FindChildFrame(SCENE_FRAME_Q, FRAME_MASK_ANY);
                    GetGame()->GetScene()->AddHumanModel(bodyModel, frameQ, frameZ);
                    body->ClearVehicle();
                    bodyMovement->m_vehicle = nullptr;
                }
                break;
            }
            default:
                break;
            }
        }
#ifndef HD2_DEDICATED   // the server's weapon pose picks up nothing
        bodyMovement->m_weaponPose->PickUpBody();
#endif
        for (int stage = 0; stage < 8; ++stage)
            bodyMovement->m_mixer.m_model->SetAnimation(nullptr, stage, 1.0f, 0);
        bodyMovement->m_carried = true;
        auto *bodyModel = static_cast<I3D_model *>(body->GetFrame());
        for (int i = 0; i < bodyModel->NumModelFrames(); ++i) {
            I3D_frame *frame = bodyModel->GetModelFrame(i);
            if (frame->GetType() == FRAME_VISUAL)
                static_cast<I3D_visual *>(frame)->SetModeA(0);
        }
        GetGEffects()->SetModelTransparency(body->GetFrame(), m_owner->GetUnknown94());
    }
    equipment->OnAnimationEvent(EQUIPMENT_EVENT_SPECIAL_LAST);
    m_owner->GetMovement()->UpdateModelNames();
    work->SetFlagsD0(0x8, true);
}

void C_HumanMovement::OnDropBody(C_Human *body)
{
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    equipment->PutAwayHandsItem(false);
    if (I3D_frame *longGun = equipment->GetModel(EQUIPMENT_PLACE_LONG_GUN))
        longGun->SetOn(true);
    if (I3D_frame *hands = equipment->GetModel(EQUIPMENT_PLACE_HANDS))
        hands->SetOn(true);
    C_HumanWork *work = m_owner->GetWork();
    const int carryMode = work->GetUnknown54();
    if (carryMode == 0 || carryMode == 6)
        m_nextAnimState.carryAnim = 0x5C;
    m_unknownA30 = MOVE_NO_TIME;
    equipment->SetUnknownD0(0.0f);
    equipment->OnAnimationEvent(EQUIPMENT_EVENT_SPECIAL_LAST - 1);
    if (body) {
        C_HumanMovement *bodyMovement = body->GetMovement();
        bodyMovement->UpdateModelNames();
        if (bodyMovement->m_shadow)
            bodyMovement->m_shadow->SetOn(true);
        bodyMovement->m_carried = false;
        bodyMovement->m_hitVolumeScale[0] = 1.0f;
        bodyMovement->m_hitVolumeScale[1] = 1.0f;
        bodyMovement->UpdateStanceVolumes(body->GetWork()->GetStance());
        bodyMovement->UpdateHitSpheres();
        auto *bodyModel = static_cast<I3D_model *>(body->GetFrame());
        for (int i = 0; i < bodyModel->NumModelFrames(); ++i) {
            I3D_frame *frame = bodyModel->GetModelFrame(i);
            if (frame->GetType() == FRAME_VISUAL)
                static_cast<I3D_visual *>(frame)->SetModeA(2);
        }
        // (The original refreshes the carrier's own hit volumes here, typed as a dead body's,
        // instead of the body's.)
        UpdateHitSpheres();
        m_hitVolumeScale[0] = 1.0f;
        m_hitVolumeScale[1] = 1.0f;
        for (int i = 0; i < m_hitVolumeCount; ++i) {
            S_CollisionVolume *volume = m_hitVolumes[i];
            SetVolumeSphere(volume, m_hitSpheres[i], m_hitVolumeScale[i]);
            volume->flags = COLLISION_SHAPE_SPHERE;
            volume->type = VOLUME_TYPE_DEAD | VOLUME_TYPE_HIT_PLAIN;
            g_collisionWorld.MoveVolume(volume);
        }
        const float zero[3] = {0.0f, 0.0f, 0.0f};
        bodyMovement->m_weaponPose->StartDeath(8, zero, 0, 4, m_owner);
        body->GetFrame()->SetOn(true);
        GetGEffects()->SetModelTransparency(body->GetFrame(), 0.0f);
    }
    m_owner->GetMovement()->UpdateModelNames();
    work->SetFlagsD0(0x8, false);
}

// ------------------------------------------------------------------ sounds and breath

void C_HumanMovement::PlaySound(int group)
{
    if (!IsClient())
        return;
    if (m_owner != g_teamManager.GetPlayerTeam()->GetActivePlayer() || group <= 3 || group >= C_MovementSounds::GROUPS)
        return;
    if (m_owner->IsLocalPlayer() && GetGame()->IsFirstPersonView()) {
        // (The original computes the mouth's direction here but does not use it.)
        m_joints[HUMAN_JOINT_C1].frame->GetLocalDir();
    }
    const S_vector offset(0.0f, 0.0f, 0.0f);
    I3D_sound *sound = GetMovementSounds()->Play(group, m_joints[HUMAN_JOINT_C1].frame, &offset);
    if (sound)
        m_sounds.push_back(sound);
}

void C_HumanMovement::UpdateSounds(bool stopAll)
{
    if (!IsClient())
        return;
    if (!stopAll && m_owner == g_teamManager.GetPlayerTeam()->GetActivePlayer()) {
        // The finished ones go back to the bank (the last one takes the slot).
        int count = static_cast<int>(m_sounds.size());
        for (int i = 0; i < count;) {
            if (!m_sounds[i]->IsPlaying(0)) {
                GetMovementSounds()->Return(m_sounds[i]);
                m_sounds[i] = m_sounds[count - 1];
                --count;
                m_sounds.pop_back();
            } else {
                ++i;
            }
        }
        return;
    }
    while (!m_sounds.empty()) {
        GetMovementSounds()->Return(m_sounds.back());
        m_sounds.pop_back();
    }
}

void C_HumanMovement::UpdateBreath(int time, bool underWater)
{
    C_HumanWork *work = m_owner->GetWork();
    if (!underWater) {
        if (m_breathTime > 7499) {
            PlaySound(MOVE_SOUND_GASP);
            work->PlayWorkAnimation(ACTIVITY_GASP, true);
        }
        m_breathTime = 0;
        m_gaspTime = 0;
        return;
    }
    // An aqualung on the back (with an intact copy protection) gives air.
    C_ItemContainer *backpack = m_owner->GetContainer(ITEM_CONTAINER_BACKPACK);
    if (!backpack->IsEmpty() && backpack->GetItem()->GetTypeId() == ITEM_AQUALUNG && CopyProtectionIntactBreath()) {
        if (m_animState.activity == ACTIVITY_DROWNING)
            work->PlayWorkAnimation(ACTIVITY_GASP, true);
        return;
    }
    m_breathTime += time;
    m_gaspTime -= time;
    if (m_breathTime <= 30000)
        return;
    if (IsServer()) {
        m_owner->Hit(HIT_DROWNING, static_cast<float>(m_breathTime - 30000) * 0.08f, S_vector(0.0f, 0.0f, 0.0f),
                     S_vector(0.0f, 0.0f, 0.0f), nullptr, 0);
    }
    if (m_owner->IsLocalPlayer() && (m_gaspTime < 1 || m_owner->GetHealth() < 1)) {
        m_gaspTime = std::rand() % 1900 + 2000;
        PlaySound(MOVE_SOUND_DROWNING);
    }
    m_breathTime = 30000;
    if (m_animState.activity != ACTIVITY_DROWNING)
        work->PlayWorkAnimation(ACTIVITY_DROWNING, true);
}

void C_HumanMovement::StartUseSound()
{
    if (!m_useObject || GetLadderSoundGroup(m_useObject) == -1 || m_useSound != -1)
        return;
    S_SoundRequest request;
    request.source = m_owner;
    request.group = GetLadderSoundGroup(m_useObject);
    request.attachTo = m_joints[HUMAN_JOINT_A].frame;
    request.pos.x = 0.0f;
    m_useSound = GetSoundModule()->Play(request);
}

void C_HumanMovement::StopUseSound()
{
    if (m_useSound != -1)
        GetSoundModule()->Stop(&m_useSound);
}

// ------------------------------------------------------------------ damaging ground

void C_HumanMovement::DamageByMaterial(int material, int time)
{
    const S_vector zero(0.0f, 0.0f, 0.0f);
    if (material < 0x44)
        return;
    switch (material) {
    case 0x46:                                          // fire
        m_owner->Hit(HIT_FIRE, static_cast<float>(time) * 0.12f, zero, zero, nullptr, 0);
        return;
    case 0x47:
        m_owner->Hit(HIT_ENVIRONMENT, static_cast<float>(time) * 0.16f, zero, zero, nullptr, 0);
        m_damageMaterial = 0x47;
        return;
    case 0x5A:
        m_owner->Hit(HIT_ENVIRONMENT, static_cast<float>(time) * 0.14f, zero, zero, nullptr, 0);
        m_damageMaterial = 0x5A;
        return;
    case 0x48:
        m_owner->Hit(HIT_ENVIRONMENT, -1.0f, zero, zero, nullptr, 0);
        return;
    case 0x5D:
        m_owner->Hit(HIT_9, -1.0f, zero, zero, nullptr, 0);
        return;
    default:
        break;
    }
    // Exploding ground (mines): with the body's dying animations only once a second.
    const bool allowed = m_animState.anim.anim < 0xBE || (GetGame()->GetMissionTime() / 100) % 10 == 0;
    if ((material == 0x44 || material == 0x45 || material == 0x62) && allowed && IsServer())
        SendExplosionMessage(m_owner->GetFrame()->GetWorldPos(), material);
}

void C_HumanMovement::DamageByContacts(const S_MoveContacts *contacts, const S_CollisionVolume *volume, int time)
{
    m_damageMaterial = -1;
    m_unknown760 = true;
    if (!contacts) {
        if (volume)
            DamageByMaterial(GetCollisionMaterial(volume), time);
        return;
    }
    // The last three materials met are not damaged by again.
    int recent[3] = {-1, -1, -1};
    for (int i = contacts->count - 1; i >= 0; --i) {
        const int material = GetCollisionMaterial(contacts->volumes[i]);
        if (material != recent[0] && material != recent[1] && material != recent[2])
            DamageByMaterial(material, time);
        recent[i % 3] = material;
    }
}

// ------------------------------------------------------------------ remote humans

void C_HumanMovement::ProcessRemoteActions(int time)
{
    if (m_remoteActions.empty())
        return;
    S_RemoteAction &front = m_remoteActions.front();
    const int action = front.action;
    const int phase = front.phase;
    front.time -= time;
    if (phase == 3) {
        // Done; kept for 16 s.
        if (front.time <= -16000)
            m_remoteActions.pop_front();
        return;
    }
    C_HumanWork *work = m_owner->GetWork();
    // Plays an animation even while the work is busy.
    const auto force = [work](int animation) {
        work->SetUnknownB0(true);
        work->Play(animation, true);
        work->SetUnknownB0(false);
    };
    // The action took place: phases 0 / 1 are dropped, phase 2 is kept as done.
    bool pop = false;
    const auto reached = [&]() {
        if (phase == 0 || phase == 1) {
            pop = true;
        } else if (phase == 2) {
            front.phase = 3;
            front.time = 0;
        }
    };
    // Phase 1 waits until its time runs out.
    const bool waiting = phase == 1 && front.time > 0;
    const int anim = m_animState.anim.anim;

    work->SetUnknownB0(true);
    switch (action) {
    case REMOTE_ACTION_STAND:
    case REMOTE_ACTION_CROUCH:
    case REMOTE_ACTION_PRONE: {
        if (waiting)
            break;
        const int timeout = action == REMOTE_ACTION_STAND ? -4000 : -2000;
        if (work->GetStance() == action)
            reached();
        else if (front.time > timeout) {
            if (m_unknown9AC == MOVE_NO_TIME)
                work->SetStance(action, true);
        } else
            pop = true;
        break;
    }
    case REMOTE_ACTION_USE:
        if (waiting)
            break;
        if (m_owner->GetMovement()->UseObject(m_useObject) < 1) {
            if (front.time > -4000)
                work->Play(0x6A, true);
            else
                pop = true;
        } else {
            reached();
        }
        break;
    case 4:
        if (waiting)
            break;
        if (IsClimbingAnim(anim))
            reached();
        else if (front.time > -800)
            work->Play(0x6A, true);
        else
            pop = true;
        break;
    case 5:
    case 6:
    case 9:
    case 10:
    case 0x12: {
        if (waiting)
            break;
        const int target = action == 5 ? 0x19 : action == 6 ? 4 : action == 9 ? 6 : action == 10 ? 7 : 0x28;
        if (anim == target)
            reached();
        else if (front.time > -4000)
            force(target);
        else
            pop = true;
        break;
    }
    case 7:
    case 8: {
        if (waiting)
            break;
        const int target = work->GetUnknownB8() != 5 ? 5 : 0x59;
        if (anim == target)
            reached();
        else if (front.time < -3999)
            pop = true;
        else
            force(target);
        break;
    }
    case 0x0B:
        if (waiting)
            break;
        if (anim == 0x27) {
            m_owner->GetEquipment()->OnAnimationEvent(9);
            reached();
        } else if (front.time > -4000) {
            force(0x27);
        } else {
            pop = true;
        }
        break;
    case 0x0C:
        if (phase == 0 || (phase == 1 && front.time <= 0)) {
            m_owner->GetEquipment()->PutAwayHandsItem(false);
            pop = true;
        } else if (phase == 2) {
            m_owner->GetEquipment()->PutAwayHandsItem(false);
            front.phase = 3;
        }
        break;
    case 0x0D:
    case 0x0E:
    case 0x0F:
        if (phase == 1 && front.time < 1) {
            m_remoteActions.pop_front();
            work->SetFlagsD0(0x1, false);
            break;
        }
        work->SetUnknownB0(true);
        if (m_interaction == 0 || m_interaction == 1)
            work->Play(ANIM_STAND, true);
        work->SetFlagsD0(0x1, true);
        break;
    case 0x10:
        if (waiting)
            break;
        if (m_animState.carryAnim != 0x47) {
            if (front.time < -3999)
                pop = true;
            else
                m_nextAnimState.carryAnim = 0x47;
            break;
        }
        reached();
        break;
    case 0x11:
        if (waiting)
            break;
        reached();
        break;
    case REMOTE_ACTION_CLIMB_B:
    case REMOTE_ACTION_CLIMB_C:
    case REMOTE_ACTION_CLIMB_D:
    case REMOTE_ACTION_CLIMB_A: {
        if (m_unknown9AC == MOVE_NO_TIME) {
            if (IsClimbingAnim(anim)) {
                // Already climbing: dropped (the original returns here and leaves the work
                // forced; kept).
                m_remoteActions.pop_front();
                return;
            }
            const int climb = action == REMOTE_ACTION_CLIMB_B   ? 0x0B
                              : action == REMOTE_ACTION_CLIMB_C ? 0x0C
                              : action == REMOTE_ACTION_CLIMB_D ? 0x0D
                                                                : 0x0A;
            work->Play(climb, true);
        }
        if (front.time <= 0)
            pop = true;
        break;
    }
    default:
        break;
    }
    if (pop)
        m_remoteActions.pop_front();
    work->SetUnknownB0(false);
}

// ------------------------------------------------------------------ saved games

bool C_HumanMovement::SaveBase(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_BASE))
        return false;
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN180, m_unknown180);
    chunk.WriteChunk(CHUNK_BASE_BLOOD_ZONE, m_bloodZone);
    const char *bloodFrame = m_bloodFrame ? m_bloodFrame->GetName() : "";
    chunk.WriteChunk(CHUNK_BASE_BLOOD_FRAME, bloodFrame, static_cast<int>(std::strlen(bloodFrame)) + 1);
    chunk.WriteChunk(CHUNK_BASE_STANCE_SCALE, m_stanceVolumeScale);
    chunk.WriteChunk(CHUNK_BASE_IN_UPDATE, static_cast<int>(m_inUpdate));
    chunk.WriteChunk(CHUNK_BASE_ANIM, &m_animState.anim, 0x10);
    chunk.WriteChunk(CHUNK_BASE_CARRY_ANIM, &m_animState.carryAnim, 0x0C);
    chunk.WriteChunk(CHUNK_BASE_ACTIVITY, &m_animState.activity, 8);
    chunk.WriteChunk(CHUNK_BASE_NEXT_ANIM, &m_nextAnimState.anim, 0x10);
    chunk.WriteChunk(CHUNK_BASE_NEXT_CARRY_ANIM, &m_nextAnimState.carryAnim, 0x0C);
    chunk.WriteChunk(CHUNK_BASE_NEXT_ACTIVITY, &m_nextAnimState.activity, 8);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN220, m_unknown220);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN228, m_stillTime);
    chunk.WriteChunk(CHUNK_BASE_FLAGS, m_flags);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN98C, static_cast<int>(m_unknown98C));
    chunk.WriteChunk(CHUNK_BASE_HIT_COUNT, m_hitVolumeCount);
    chunk.WriteChunk(CHUNK_BASE_GROUND_MATERIAL, m_groundMaterial);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN764, m_unknown764);
    if (chunk.IsOpen())
        chunk.Begin(CHUNK_BASE_REQUEST);
    m_request.SaveGame(chunk);
    chunk.Ascend();
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN940, &m_unknown940, 0x0C);
    chunk.WriteChunk(CHUNK_BASE_BREATH_TIME, m_breathTime);
    chunk.WriteChunk(CHUNK_BASE_HIT_SCALE0, m_hitVolumeScale[0]);
    chunk.WriteChunk(CHUNK_BASE_HIT_SCALE1, m_hitVolumeScale[1]);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN762, static_cast<int>(m_onGround));
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN761, static_cast<int>(m_onDynamicGround));
    chunk.WriteChunk(CHUNK_BASE_STANCE_COUNT, m_stanceVolumeCount);
    chunk.WriteChunk(CHUNK_BASE_DAMAGE_MATERIAL, m_damageMaterial);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN760, static_cast<int>(m_unknown760));
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN234, m_swimBobPhase);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN954, m_unknown954);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN958, m_unknown958);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN95C, m_unknown95C);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN960, m_unknown960);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN964, m_unknown964);
    // (Saved as an int and loaded back as its bits into the float, as in the original.)
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN968, static_cast<int>(m_unknown968));
    chunk.WriteChunk(CHUNK_BASE_USE_STATE, m_useState);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN96C, static_cast<int>(m_unknown96C));
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA90, static_cast<int>(m_parachuting));
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN998, m_unknown998);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA48, m_seatStance);
    chunk.WriteChunk(CHUNK_BASE_STANCE_CHANGE, m_stanceChange);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9A0, m_unknown9A0);
    chunk.WriteChunk(CHUNK_BASE_NEXT_STANCE, m_nextStance);
    chunk.WriteChunk(CHUNK_BASE_NEXT_STANCE2, m_nextStance2);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9AC, m_unknown9AC);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9B0, m_unknown9B0);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9B8, m_unknown9B8);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9BC, m_unknown9BC);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9C0, m_unknown9C0);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9C4, m_unknown9C4);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9CC, m_unknown9CC);
    chunk.WriteChunk(CHUNK_BASE_PUSH_STATE, m_unknown1AC);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9D0, &m_unknown9D0, 0x0C);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9E0, m_unknown9E0);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9F0, &m_unknown9F0, 0x0C);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA00, &m_unknownA00, 0x0C);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA10, m_unknownA10);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA1C, m_unknownA1C);
    chunk.WriteChunk(CHUNK_BASE_HANDS_CARRY_STATE, m_handsCarryState);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA24, m_unknownA24);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA28, m_unknownA28);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA2C, m_unknownA2C);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA30, m_unknownA30);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA34, m_interaction);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA38, m_interactionTime);
    int vehicle = -1;
    if (m_vehicle)
        vehicle = m_vehicle->IsTemporary() ? m_vehicle->GetNetId() + TEMPORARY_ACTOR_ID_BASE : m_vehicle->GetNetId();
    chunk.WriteChunk(CHUNK_BASE_VEHICLE, vehicle);
    const int seatKind = m_interaction & 0x3FF;
    // (Sitting on a chair or getting up: the seat is a frame of the scene.)
    if (seatKind == INTERACT_SIT || seatKind == INTERACT_STAND_UP) {
        const char *name = m_seatFrame->GetName();
        chunk.WriteChunk(CHUNK_BASE_SEAT_FRAME, name, static_cast<int>(std::strlen(name)) + 1);
    } else {
        chunk.WriteChunk(CHUNK_BASE_SEAT, m_seat);
    }
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA44, m_seatMode);
    chunk.WriteChunk(CHUNK_BASE_HIT_SCALE, m_hitVolumeScale[0]);
    chunk.WriteChunk(CHUNK_BASE_USE_OBJECT, m_useObject ? m_useObject->GetNetId() : -1);
    chunk.WriteChunk(CHUNK_BASE_TRAIL_TIME, m_trailTime);
    chunk.WriteChunk(CHUNK_BASE_TRAIL_COUNT, m_trailCount);
    // (The original stages the spheres in a static buffer and writes all 4 entries, the
    // unused ones with what an earlier save left there; zeros here.)
    S_SavedSphere trail[4] = {};
    for (int i = 0; i < m_trailCount; ++i) {
        const S_CollisionVolume *volume = m_trailVolumes[i];
        trail[i].center = {volume->center.x, volume->center.y, volume->center.z, volume->centerW};
        trail[i].radius = volume->radius;
    }
    chunk.WriteChunk(CHUNK_BASE_TRAIL_SPHERES, trail, sizeof(trail));
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9C8, static_cast<int>(m_unknown9C8));
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA14, m_unknownA14);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWNA18, m_unknownA18);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN950, m_unknown950);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN250, m_unknown250);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9B4, m_unknown9B4);
    chunk.WriteChunk(CHUNK_BASE_FOOTSTEP_SURFACE, m_footstepSurface);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN190, m_unknown190);
    chunk.WriteChunk(CHUNK_BASE_CLOCK, m_clock);
    chunk.WriteChunk(CHUNK_BASE_FOOTSTEP_TIME, m_footstepTime);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN1B0, m_unknown1B0);
    chunk.WriteChunk(CHUNK_BASE_CARRIED, static_cast<int>(m_carried));
    chunk.WriteChunk(CHUNK_BASE_FOOTSTEP_COUNT, m_footstepCount);
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN9C9, static_cast<int>(m_unknown9C9));
    chunk.WriteChunk(CHUNK_BASE_UNKNOWN254, static_cast<int>(m_unknown254));
    const int actions = std::min(static_cast<int>(m_remoteActions.size()), SAVED_REMOTE_ACTIONS);
    chunk.WriteChunk(CHUNK_BASE_REMOTE_COUNT, actions);
    for (int i = 0; i < actions; ++i)
        chunk.WriteChunk(static_cast<uint16_t>(CHUNK_BASE_REMOTE_FIRST + i), m_remoteActions[i]);
    chunk.Ascend();
    return true;
}

bool C_HumanMovement::LoadBase(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != CHUNK_BASE) {
        chunk.Ascend();
        return false;
    }
    // Out of the collision world until the end.
    for (int i = 0; i < 4; ++i) {
        if (i < m_stanceVolumeCount)
            g_collisionWorld.RemoveVolume(m_stanceVolumes[i]);
        if (m_trailTime > 0 && i < m_trailCount)
            g_collisionWorld.RemoveVolume(m_trailVolumes[i]);
    }
    for (int i = 0; i < m_hitVolumeCount; ++i)
        g_collisionWorld.RemoveVolume(m_hitVolumes[i]);
    UpdateSounds(true);
    m_timer200 = std::rand() % 200;
    m_timer300 = std::rand() % 300;
    m_timer400 = std::rand() % 400;
    m_timer600 = std::rand() % 600;

    char name[1000];
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        int value = 0;
        float number = 0.0f;
        switch (id) {
        case CHUNK_BASE_UNKNOWN180:
            chunk.Read(m_unknown180);
            break;
        case CHUNK_BASE_BLOOD_ZONE:
            chunk.Read(m_bloodZone);
            break;
        case CHUNK_BASE_BLOOD_FRAME:
            chunk.ReadString(name, sizeof(name));
            m_bloodFrame = name[0] ? m_owner->GetFrame()->FindChildFrame(name, FRAME_MASK_ANY) : nullptr;
            break;
        case CHUNK_BASE_STANCE_SCALE:
            chunk.Read(m_stanceVolumeScale);
            break;
        case CHUNK_BASE_IN_UPDATE:
            chunk.Read(value);
            m_inUpdate = value != 0;
            break;
        case CHUNK_BASE_ANIM:
            if (chunk.RemainingSize() >= 0x10)
                chunk.Read(&m_animState.anim, 0x10);
            break;
        case CHUNK_BASE_CARRY_ANIM:
            if (chunk.RemainingSize() >= 0x0C)
                chunk.Read(&m_animState.carryAnim, 0x0C);
            break;
        case CHUNK_BASE_ACTIVITY:
            if (chunk.RemainingSize() >= 8)
                chunk.Read(&m_animState.activity, 8);
            break;
        case CHUNK_BASE_NEXT_ANIM:
            if (chunk.RemainingSize() >= 0x10)
                chunk.Read(&m_nextAnimState.anim, 0x10);
            break;
        case CHUNK_BASE_NEXT_CARRY_ANIM:
            if (chunk.RemainingSize() >= 0x0C)
                chunk.Read(&m_nextAnimState.carryAnim, 0x0C);
            break;
        case CHUNK_BASE_NEXT_ACTIVITY:
            if (chunk.RemainingSize() >= 8)
                chunk.Read(&m_nextAnimState.activity, 8);
            break;
        case CHUNK_BASE_UNKNOWN220:
            chunk.Read(m_unknown220);
            break;
        case CHUNK_BASE_UNKNOWN228:
            chunk.Read(m_stillTime);
            break;
        case CHUNK_BASE_FLAGS:
            chunk.Read(m_flags);
            break;
        case CHUNK_BASE_UNKNOWN98C:
            chunk.Read(value);
            m_unknown98C = value != 0;
            break;
        case CHUNK_BASE_HIT_COUNT:
            chunk.Read(m_hitVolumeCount);
            break;
        case CHUNK_BASE_GROUND_MATERIAL:
            chunk.Read(m_groundMaterial);
            break;
        case CHUNK_BASE_UNKNOWN764:
            chunk.Read(m_unknown764);
            break;
        case CHUNK_BASE_REQUEST:
            m_request.LoadGame(chunk);
            break;
        case CHUNK_BASE_UNKNOWN940:
            chunk.Read(&m_unknown940, 0x0C);
            break;
        case CHUNK_BASE_BREATH_TIME:
            chunk.Read(m_breathTime);
            break;
        case CHUNK_BASE_HIT_SCALE0:
            chunk.Read(m_hitVolumeScale[0]);
            break;
        case CHUNK_BASE_HIT_SCALE1:
            chunk.Read(m_hitVolumeScale[1]);
            break;
        case CHUNK_BASE_UNKNOWN762:
            chunk.Read(value);
            m_onGround = value != 0;
            break;
        case CHUNK_BASE_UNKNOWN761:
            chunk.Read(value);
            m_onDynamicGround = value != 0;
            break;
        case CHUNK_BASE_STANCE_COUNT:
            chunk.Read(m_stanceVolumeCount);
            break;
        case CHUNK_BASE_DAMAGE_MATERIAL:
            chunk.Read(m_damageMaterial);
            break;
        case CHUNK_BASE_UNKNOWN760:
            chunk.Read(value);
            m_unknown760 = value != 0;
            break;
        case CHUNK_BASE_UNKNOWN234:
            chunk.Read(m_swimBobPhase);
            break;
        case CHUNK_BASE_UNKNOWN954:
            chunk.Read(m_unknown954);
            break;
        case CHUNK_BASE_UNKNOWN958:
            chunk.Read(m_unknown958);
            break;
        case CHUNK_BASE_UNKNOWN95C:
            chunk.Read(m_unknown95C);
            break;
        case CHUNK_BASE_UNKNOWN960:
            chunk.Read(m_unknown960);
            break;
        case CHUNK_BASE_UNKNOWN964:
            chunk.Read(m_unknown964);
            break;
        case CHUNK_BASE_UNKNOWN968:
            // (The int saved is taken as the float's bits, as in the original.)
            chunk.Read(&m_unknown968, 4);
            break;
        case CHUNK_BASE_USE_STATE:
            chunk.Read(m_useState);
            break;
        case CHUNK_BASE_UNKNOWN96C:
            chunk.Read(value);
            m_unknown96C = value != 0;
            break;
        case CHUNK_BASE_UNKNOWNA90:
            chunk.Read(value);
            m_parachuting = value != 0;
            break;
        case CHUNK_BASE_UNKNOWN998:
            chunk.Read(m_unknown998);
            break;
        case CHUNK_BASE_UNKNOWNA48:
            chunk.Read(m_seatStance);
            break;
        case CHUNK_BASE_STANCE_CHANGE:
            chunk.Read(m_stanceChange);
            break;
        case CHUNK_BASE_UNKNOWN9A0:
            chunk.Read(m_unknown9A0);
            break;
        case CHUNK_BASE_NEXT_STANCE:
            chunk.Read(m_nextStance);
            break;
        case CHUNK_BASE_NEXT_STANCE2:
            chunk.Read(m_nextStance2);
            break;
        case CHUNK_BASE_UNKNOWN9AC:
            chunk.Read(m_unknown9AC);
            break;
        case CHUNK_BASE_UNKNOWN9B0:
            chunk.Read(m_unknown9B0);
            break;
        case CHUNK_BASE_UNKNOWN9B8:
            chunk.Read(m_unknown9B8);
            break;
        case CHUNK_BASE_UNKNOWN9BC:
            chunk.Read(m_unknown9BC);
            break;
        case CHUNK_BASE_UNKNOWN9C0:
            chunk.Read(m_unknown9C0);
            break;
        case CHUNK_BASE_UNKNOWN9C4:
            chunk.Read(m_unknown9C4);
            break;
        case CHUNK_BASE_UNKNOWN9CC:
            chunk.Read(m_unknown9CC);
            break;
        case CHUNK_BASE_PUSH_STATE:
            chunk.Read(m_unknown1AC);
            break;
        case CHUNK_BASE_UNKNOWN9D0:
            chunk.Read(&m_unknown9D0, 0x0C);
            break;
        case CHUNK_BASE_UNKNOWN9E0:
            chunk.Read(m_unknown9E0);
            break;
        case CHUNK_BASE_UNKNOWN9F0:
            chunk.Read(&m_unknown9F0, 0x0C);
            break;
        case CHUNK_BASE_UNKNOWNA00:
            chunk.Read(&m_unknownA00, 0x0C);
            break;
        case CHUNK_BASE_UNKNOWNA10:
            chunk.Read(m_unknownA10);
            break;
        case CHUNK_BASE_UNKNOWNA1C:
            chunk.Read(m_unknownA1C);
            break;
        case CHUNK_BASE_HANDS_CARRY_STATE:
            chunk.Read(m_handsCarryState);
            break;
        case CHUNK_BASE_UNKNOWNA24:
            chunk.Read(m_unknownA24);
            break;
        case CHUNK_BASE_UNKNOWNA28:
            chunk.Read(m_unknownA28);
            break;
        case CHUNK_BASE_UNKNOWNA2C:
            chunk.Read(m_unknownA2C);
            break;
        case CHUNK_BASE_UNKNOWNA30:
            chunk.Read(m_unknownA30);
            break;
        case CHUNK_BASE_UNKNOWNA34:
            chunk.Read(m_interaction);
            break;
        case CHUNK_BASE_UNKNOWNA38:
            chunk.Read(m_interactionTime);
            break;
        case CHUNK_BASE_VEHICLE:
            chunk.Read(value);
            if (value == -1)
                m_vehicle = nullptr;
            else if (value <= TEMPORARY_ACTOR_ID_BASE)
                m_vehicle = g_actorsModule.GetActor(value);
            else
                m_vehicle = g_temporaryActors.FindActor(value - TEMPORARY_ACTOR_ID_BASE);
            break;
        case CHUNK_BASE_SEAT:
            chunk.Read(m_seat);
            break;
        case CHUNK_BASE_SEAT_FRAME:
            chunk.ReadString(name, sizeof(name));
            m_seatFrame = GetGame()->GetScene()->FindFrame(name, FRAME_MASK_ANY);
            break;
        case CHUNK_BASE_UNKNOWNA44:
            chunk.Read(m_seatMode);
            break;
        case CHUNK_BASE_HIT_SCALE:
            chunk.Read(number);
            m_hitVolumeScale[0] = number;
            m_hitVolumeScale[1] = number;
            break;
        case CHUNK_BASE_USE_OBJECT:
            chunk.Read(value);
            m_useObject = value == -1 ? nullptr : g_actorsModule.GetActorById(value);
            break;
        case CHUNK_BASE_TRAIL_TIME:
            chunk.Read(m_trailTime);
            break;
        case CHUNK_BASE_TRAIL_COUNT:
            chunk.Read(m_trailCount);
            break;
        case CHUNK_BASE_TRAIL_SPHERES: {
            S_SavedSphere trail[4] = {};
            chunk.Read(trail, sizeof(trail));
            if (m_trailTime > 0) {
                // Fix: at most the 4 volumes there are (the count is taken from the file).
                for (int i = 0; i < std::min(m_trailCount, 4); ++i) {
                    S_CollisionVolume *volume = m_trailVolumes[i];
                    volume->center = XYZ(trail[i].center);
                    volume->centerW = trail[i].center.w;
                    volume->radius = trail[i].radius;
                    g_collisionWorld.AddVolume(volume);
                }
            }
            break;
        }
        case CHUNK_BASE_UNKNOWN9C8:
            chunk.Read(value);
            m_unknown9C8 = value != 0;
            break;
        case CHUNK_BASE_UNKNOWNA14:
            chunk.Read(m_unknownA14);
            break;
        case CHUNK_BASE_UNKNOWNA18:
            chunk.Read(m_unknownA18);
            break;
        case CHUNK_BASE_UNKNOWN950:
            chunk.Read(m_unknown950);
            break;
        case CHUNK_BASE_UNKNOWN250:
            chunk.Read(m_unknown250);
            break;
        case CHUNK_BASE_UNKNOWN9B4:
            chunk.Read(m_unknown9B4);
            break;
        case CHUNK_BASE_FOOTSTEP_SURFACE:
            chunk.Read(m_footstepSurface);
            break;
        case CHUNK_BASE_UNKNOWN190:
            chunk.Read(m_unknown190);
            break;
        case CHUNK_BASE_CLOCK:
            chunk.Read(m_clock);
            break;
        case CHUNK_BASE_FOOTSTEP_TIME:
            chunk.Read(m_footstepTime);
            break;
        case CHUNK_BASE_UNKNOWN1B0:
            chunk.Read(m_unknown1B0);
            break;
        case CHUNK_BASE_CARRIED:
            chunk.Read(value);
            m_carried = value != 0;
            break;
        case CHUNK_BASE_FOOTSTEP_COUNT:
            chunk.Read(m_footstepCount);
            break;
        case CHUNK_BASE_UNKNOWN9C9:
            chunk.Read(value);
            m_unknown9C9 = value != 0;
            break;
        case CHUNK_BASE_UNKNOWN254:
            chunk.Read(value);
            m_unknown254 = static_cast<float>(value);
            break;
        case CHUNK_BASE_REMOTE_COUNT:
            // The actions follow as chunks of their own (outside this one).
            chunk.Read(value);
            chunk.Ascend();
            for (int i = 0; i < value; ++i) {
                S_RemoteAction action;
                uint16_t actionId;
                chunk.Descend(actionId);
                chunk.Read(&action, sizeof(action));
                chunk.Ascend();
                m_remoteActions.push_back(action);
            }
            continue;
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();

    // Back into the collision world.
    if (m_stanceVolumeCount > 0) {
        m_stanceVolumeCount = 0;
        UpdateDummies();
        UpdateStanceVolumes(m_owner->GetWork()->GetStance());
        for (int i = 0; i < m_stanceVolumeCount; ++i) {
            S_CollisionVolume *volume = m_stanceVolumes[i];
            SetVolumeSphere(volume, m_stanceSpheres[i], m_stanceVolumeScale);
            volume->type = VOLUME_TYPE_STANCE;
            g_collisionWorld.MoveVolume(volume);
        }
    }
    if (m_hitVolumeCount > 0) {
        m_hitVolumeCount = 0;
        UpdateHitSpheres();
        for (int i = 0; i < m_hitVolumeCount; ++i) {
            S_CollisionVolume *volume = m_hitVolumes[i];
            SetVolumeSphere(volume, m_hitSpheres[i], m_hitVolumeScale[i]);
            volume->flags = COLLISION_SHAPE_SPHERE;
            volume->type = m_owner->GetHealth() < 1 ? VOLUME_TYPE_DEAD | VOLUME_TYPE_HIT_PLAIN : VOLUME_TYPE_HIT;
            g_collisionWorld.AddVolume(volume);
        }
    }
    return true;
}
