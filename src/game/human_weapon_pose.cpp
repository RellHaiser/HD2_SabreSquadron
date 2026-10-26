// The weapon pose's core (original source 0x0079bd70..0x0079f3f0, 0x007a78d0..0x007a8760 and the
// inline setters of its users): creating it, binding it to a model's skeleton (the bones and
// their joint limits, the camera dummies, the weapon hands), saving and loading it, and the
// update that runs the mode the pose is in (holding the weapon, the hands free, a seat, a
// static weapon, the dead body) with the blends of raising / lowering the weapon and of the
// support hand.
#include "game/human_pose_internal.h"

#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_body.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/human_stains.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "game/world.h"
#include "ls3d/i3d.h"

// The joint names of the human models, by E_PoseBone.
const char g_poseBoneNames[POSE_BONE_COUNT][12] = {"u", "q", "t", "p", "e", "f1", "f", "d1", "d", "b", "s", "o",
                                                   "c1", "c", "g", "k", "h", "l", "i", "m", "j", "n", "a"};   // 0x00816d18

// Draws the pelvis's joint (I3D_joint::UnknownA8) after every update (a debug switch).
bool g_poseDebugJoints = false;                         // 0x0098af98

namespace {

// The save game chunk of the pose and the markers between its parts.
const uint16_t CHUNK_WEAPON_POSE = 0xDEF0;
const uint32_t POSE_SAVE_MARKER = 0xDEFEDEFE;

const char *const FRAME_CAMERA_3RD = "camera3RD";
const char *const FRAME_CAMERA_FPV = "cameraFPV";
const char *const FRAME_HAND_1 = "hw1";
const char *const FRAME_HAND_2 = "hw2";
const char *const FRAME_DUMMY_Z = "z";
const char *const GRIP_LEFT_CAR = "LWC";
const char *const GRIP_RIGHT_CAR = "RWC";
const char *const GRIP_LEFT_SEAT = "LWS";
const char *const GRIP_RIGHT_SEAT = "RWS";

// The prone body fits six spheres to the ground; a static weapon has six gunner poses.
const int PRONE_SPHERE_COUNT = 6;
const int GUNNER_POSE_COUNT = 6;

// What the third person / first person camera's sweep from the head / chest tests.
const uint32_t CAMERA_TEST_TYPES = 0xC8002;
const uint32_t CAMERA_TEST_FLAGS = 0x230000;

const float PI = 3.1415927f;

int BoneNumber(const S_PoseBone *bone) { return bone ? bone->index + 1 : 0; }

void WriteInt(C_chunk &chunk, int value) { chunk.Write(&value, 4); }
int ReadInt(C_chunk &chunk)
{
    int value = 0;
    chunk.Read(&value, 4);
    return value;
}

void WriteQuat(C_chunk &chunk, const S_quat &q)
{
    float f[4];
    q.ToFile(f);
    chunk.Write(f, sizeof(f));
}

// The file name an animation set was loaded from (empty: none).
void WriteAnimName(C_chunk &chunk, I3D_animation_set *anim)
{
    const char *name = anim && anim->GetFileName() ? anim->GetFileName() : "";
    chunk.WriteSizedString(name);
}

// The animation set of a saved name (through the animation cache; null: none).
I3D_animation_set *ReadAnim(C_chunk &chunk)
{
    char name[0x80];
    name[0] = '\0';
    chunk.ReadSizedString(name, sizeof(name));
    if (!name[0])
        return nullptr;
    I3D_animation_set *anim = nullptr;
    g_animationCache.Get(name, &anim);
    return anim;
}

}  // namespace

// ---- life -------------------------------------------------------------------------------

// 0x0079bd70
C_HumanWeaponPose::C_HumanWeaponPose()
{
    m_model = nullptr;
    m_owner = nullptr;
    m_movement = nullptr;
    m_weapon = nullptr;
    m_proneHits = static_cast<uint8_t *>(operator new(PRONE_SPHERE_COUNT));
    m_proneGround = static_cast<S_vector4 *>(_aligned_malloc(PRONE_SPHERE_COUNT * sizeof(S_vector4), 16));
    m_proneNormals = static_cast<S_vector4 *>(_aligned_malloc(PRONE_SPHERE_COUNT * sizeof(S_vector4), 16));
    m_proneFrom = static_cast<S_vector4 *>(_aligned_malloc(PRONE_SPHERE_COUNT * sizeof(S_vector4), 16));
    m_proneTo = static_cast<S_vector4 *>(_aligned_malloc(PRONE_SPHERE_COUNT * sizeof(S_vector4), 16));
    m_proneNow = static_cast<S_vector4 *>(_aligned_malloc(PRONE_SPHERE_COUNT * sizeof(S_vector4), 16));
    m_proneSpheres = static_cast<S_vector4 *>(_aligned_malloc(PRONE_SPHERE_COUNT * sizeof(S_vector4), 16));
    m_proneBlendTime = static_cast<int *>(operator new(PRONE_SPHERE_COUNT * sizeof(int)));
    m_gunnerRot = static_cast<S_quat **>(operator new(GUNNER_POSE_COUNT * sizeof(S_quat *)));
    m_gunnerPos = static_cast<S_vector4 **>(operator new(GUNNER_POSE_COUNT * sizeof(S_vector4 *)));
    for (int i = 0; i < GUNNER_POSE_COUNT; ++i) {
        m_gunnerRot[i] = static_cast<S_quat *>(_aligned_malloc(POSE_BONE_COUNT * sizeof(S_quat), 16));
        m_gunnerPos[i] = static_cast<S_vector4 *>(_aligned_malloc(POSE_BONE_COUNT * sizeof(S_vector4), 16));
    }
    m_rootBody = nullptr;
    m_rootPoint = nullptr;
    for (S_RagdollBody *&body : m_bodies)
        body = nullptr;
    for (S_RagdollPoint *&point : m_points)
        point = nullptr;
    m_restMatrices = nullptr;
    m_pushes = nullptr;
    m_holds = nullptr;
    m_slidePoints = nullptr;
    m_carrier = nullptr;
    m_stains = nullptr;
    m_impactSound = -1;
    m_dropSound = -1;
    m_camera3rd = nullptr;
    m_cameraFpv = nullptr;
    Attach(nullptr, nullptr, false);
}

// 0x0079bf20
C_HumanWeaponPose::~C_HumanWeaponPose()
{
    if (m_proneHits)
        operator delete(m_proneHits);
    for (S_vector4 *buffer : {m_proneGround, m_proneNormals, m_proneFrom, m_proneTo, m_proneNow, m_proneSpheres})
        if (buffer)
            _aligned_free(buffer);
    if (m_proneBlendTime)
        operator delete(m_proneBlendTime);
    if (m_gunnerRot) {
        for (int i = 0; i < GUNNER_POSE_COUNT; ++i)
            if (m_gunnerRot[i])
                _aligned_free(m_gunnerRot[i]);
        operator delete(m_gunnerRot);
    }
    if (m_gunnerPos) {
        for (int i = 0; i < GUNNER_POSE_COUNT; ++i)
            if (m_gunnerPos[i])
                _aligned_free(m_gunnerPos[i]);
        operator delete(m_gunnerPos);
    }
    if (m_contactFrames)
        ReleaseRagdollContacts();
    if (m_rootBody)
        FreeRagdollBody(m_rootBody);
    if (m_rootPoint)
        FreeRagdollPoint(m_rootPoint);
    if (m_restMatrices)
        _aligned_free(m_restMatrices);
    if (m_pushes)
        _aligned_free(m_pushes);
    if (m_holds)
        _aligned_free(m_holds);
    if (m_weapon)
        m_weapon->Release();
    m_weapon = nullptr;
    if (m_slidePoints)
        _aligned_free(m_slidePoints);
    delete m_stains;
    if (m_camera3rd)
        m_camera3rd->Release();
    if (m_cameraFpv)
        m_cameraFpv->Release();
    if (m_impactSound != -1 && GetSoundModule())
        GetSoundModule()->Stop(&m_impactSound);
    m_impactSound = -1;
    if (m_dropSound >= 0 && GetSoundModule())
        GetSoundModule()->Stop(&m_dropSound);
    m_dropSound = -1;
}

// 0x0079c190: the pose of `model` (the owner's): unless `reload`, everything back to the start
// (the bones without joints, the limits open, no chains, no ragdoll); then the skeleton bound,
// the hands found and the ragdoll built (its rolls kept). After a saved game the dying body
// takes its saved bone matrices again, a ragdoll is posed and the scene told.
void C_HumanWeaponPose::Attach(I3D_frame *model, C_Human *owner, bool reload)
{
    m_model = model;
    m_owner = owner;
    m_movement = owner ? owner->GetMovement() : nullptr;
    if (!reload) {
        m_flags = 0;
        m_boneCount = 0;
        m_root = nullptr;
        if (m_weapon)
            m_weapon->Release();
        m_weapon = nullptr;
        m_aimLength = 0;
        m_aimTime = 0;
        m_updateCount = std::rand() + 1;
        m_updateSeen = 0;
        m_visible = true;
        m_stance = 0;
        m_stanceBeforeSeat = 0;
        m_elbowOut = 0.5f;
        m_elbowIn = 0.9f;
        m_lookFlags = 0;
        PoseSetXYZ(m_lookFrom, S_vector(0.0f, 0.0f, 0.0f));
        PoseSetXYZ(m_lookTo, S_vector(0.0f, 0.0f, 0.0f));
        m_proneBlendLength = 100;
        m_impactSound = -1;
        m_dropSound = -1;
        for (S_PoseBone &bone : m_bones) {
            bone.index = -1;
            bone.frame = nullptr;
            for (int i = 0; i < 3; ++i) {
                bone.child[i] = nullptr;
                bone.childPos[i].x = bone.childPos[i].y = bone.childPos[i].z = 0.0f;
                bone.childLength[i] = 0.0f;
                bone.chainChildren[i] = nullptr;
            }
            bone.parent = nullptr;
            bone.chainFrame = nullptr;
            bone.chainLimit = nullptr;
            bone.chainLength = 0.0f;
            bone.pinMask = 0;
            bone.chainParent = nullptr;
            // The joints open (+-pi), turning about all axes, taking whole steps.
            S_PoseLimit &limit = bone.limits[0];
            limit.max.x = limit.max.y = limit.max.z = PI;
            limit.min.x = limit.min.y = limit.min.z = -PI;
            limit.unknown20.x = limit.unknown20.y = limit.unknown20.z = 0.0f;
            limit.axes = POSE_AXIS_X | POSE_AXIS_Y | POSE_AXIS_Z;
            limit.stiffness = 1.0f;
            for (int i = 1; i < 6; ++i)
                bone.limits[i] = limit;
        }
        for (S_PosePin &pin : m_chain.pins) {
            pin.bone = nullptr;
            pin.target.x = pin.target.y = pin.target.z = 0.0f;
            pin.weight = 1.0f;
        }
        m_chain.root = nullptr;
        m_chain.count = 0;
        m_vehicle = nullptr;
        for (int i = 0; i < 6; ++i) {
            if (m_proneFrom)
                PoseSetXYZ(m_proneFrom[i], S_vector(0.0f, 1.0f, 0.0f));
            if (m_proneTo)
                PoseSetXYZ(m_proneTo[i], S_vector(0.0f, 1.0f, 0.0f));
            if (m_proneNow)
                PoseSetXYZ(m_proneNow[i], S_vector(0.0f, 1.0f, 0.0f));
            if (m_proneBlendTime)
                m_proneBlendTime[i] = 0;
        }
        PoseSetXYZ(m_proneDir, S_vector(0.0f, 0.0f, 0.0f));
        m_proneNow[0].x = PoseUnset();
        if (m_rootBody)
            FreeRagdollBody(m_rootBody);
        if (m_rootPoint)
            FreeRagdollPoint(m_rootPoint);
        m_rootBody = nullptr;
        m_rootPoint = nullptr;
        for (S_RagdollBody *&body : m_bodies)
            body = nullptr;
        for (S_RagdollPoint *&point : m_points)
            point = nullptr;
        if (m_restMatrices)
            _aligned_free(m_restMatrices);
        m_restMatrices = nullptr;
        if (m_pushes)
            _aligned_free(m_pushes);
        m_pushes = nullptr;
        if (m_holds)
            _aligned_free(m_holds);
        m_holds = nullptr;
        m_activityAnim = nullptr;
        m_deathAnim = nullptr;
        if (m_slidePoints)
            _aligned_free(m_slidePoints);
        m_slidePoints = nullptr;
        m_carrier = nullptr;
        delete m_stains;
        m_stains = nullptr;
        m_bloodPool = false;
        for (float &roll : m_savedRolls)
            roll = 0.0f;
    }
    if (m_model) {
        BindSkeleton(reload ? 1 : 0);
        if (m_model) {
            InitHands(reload);
            if (m_model && m_owner) {
                BuildRagdoll(reload);
                for (int i = 0; i < RAGDOLL_BODY_COUNT; ++i)
                    if (m_bodies[i])
                        m_savedRolls[i] = m_bodies[i]->roll;
            }
        }
    }
    if (!reload)
        return;
    if (m_deathAnim) {
        // The dying body: its bones as saved (S_PoseBone::poseMatrix), then the animation on.
        for (S_PoseBone &bone : m_bones)
            bone.frame->SetLocalMatrix(bone.poseMatrix);
        m_model->Update();
        UpdateDeathAnim();
        return;
    }
    if (m_flags & POSE_FLAG_DEAD) {
        PoseFromRagdoll(m_rootBody, 1.0f);
        // Quirk: the scene's frames the other way round (see StartRagdoll).
        GetGame()->GetScene()->AddHumanModel(m_model, m_dummyZ, m_bones[POSE_BONE_RIGHT_FOOT].frame);
        GetGame()->GetScene()->Unknown144(m_model);
    }
}

// 0x0079c780: binds the bones to the model's joints. After a saved game (`mode` 1) only the frames are
// found again (by name; the chain frames from their saved bone numbers) and a dead body is
// laid down again; otherwise the skeleton is built: the hierarchy, the joints' axes and
// stiffness, and each bone's children (their positions and the axes pointing at them). Then
// the camera and hand dummies, the bones' scales reset, and the stains (detailed single
// player only).
void C_HumanWeaponPose::BindSkeleton(int mode)
{
    const bool reload = static_cast<char>(mode) == 1;
    if (reload) {
        for (int i = 0; i < POSE_BONE_COUNT; ++i)
            m_bones[i].frame = m_model->FindChildFrame(g_poseBoneNames[i], FRAME_MASK_ANY);
        // (The chain frames hold bone numbers after LoadGame.)
        for (S_PoseBone &bone : m_bones)
            if (bone.chainFrame)
                bone.chainFrame = m_bones[reinterpret_cast<intptr_t>(bone.chainFrame) - 1].frame;
        if ((m_flags & POSE_FLAG_DEAD) || m_deathAnim)
            m_movement->Ragdoll(true, true);
    } else {
        // The chain frames as bone numbers while the bones are bound again.
        for (S_PoseBone &bone : m_bones) {
            if (!bone.chainFrame)
                continue;
            for (const S_PoseBone &other : m_bones)
                if (bone.chainFrame == other.frame)
                    bone.chainFrame = reinterpret_cast<I3D_frame *>(static_cast<intptr_t>(other.index + 1));
        }
        struct S_Joint {
            int bone;
            int parent;
        };
        static const S_Joint joints[] = {
            {POSE_BONE_PELVIS, -1},
            {POSE_BONE_SPINE, POSE_BONE_PELVIS},
            {POSE_BONE_LOWER_BACK, POSE_BONE_SPINE},
            {POSE_BONE_BACK, POSE_BONE_LOWER_BACK},
            {POSE_BONE_UPPER_CHEST, POSE_BONE_BACK},
            {POSE_BONE_CHEST, POSE_BONE_UPPER_CHEST},
            {POSE_BONE_NECK, POSE_BONE_CHEST},
            {POSE_BONE_HEAD_TOP, POSE_BONE_NECK},
            {POSE_BONE_HEAD, POSE_BONE_HEAD_TOP},
            {POSE_BONE_RIGHT_COLLAR, POSE_BONE_CHEST},
            {POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_COLLAR},
            {POSE_BONE_RIGHT_ELBOW, POSE_BONE_RIGHT_SHOULDER},
            {POSE_BONE_RIGHT_HAND, POSE_BONE_RIGHT_ELBOW},
            {POSE_BONE_LEFT_COLLAR, POSE_BONE_CHEST},
            {POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_COLLAR},
            {POSE_BONE_LEFT_ELBOW, POSE_BONE_LEFT_SHOULDER},
            {POSE_BONE_LEFT_HAND, POSE_BONE_LEFT_ELBOW},
            {POSE_BONE_RIGHT_HIP, POSE_BONE_PELVIS},
            {POSE_BONE_RIGHT_KNEE, POSE_BONE_RIGHT_HIP},
            {POSE_BONE_RIGHT_FOOT, POSE_BONE_RIGHT_KNEE},
            {POSE_BONE_LEFT_HIP, POSE_BONE_PELVIS},
            {POSE_BONE_LEFT_KNEE, POSE_BONE_LEFT_HIP},
            {POSE_BONE_LEFT_FOOT, POSE_BONE_LEFT_KNEE},
        };
        for (const S_Joint &joint : joints)
            AddBone(joint.bone, joint.parent, m_model->FindChildFrame(g_poseBoneNames[joint.bone], FRAME_MASK_ANY));
        m_root = &m_bones[POSE_BONE_PELVIS];
        for (S_PoseBone &bone : m_bones)
            if (bone.chainFrame)
                bone.chainFrame = m_bones[reinterpret_cast<intptr_t>(bone.chainFrame) - 1].frame;

        // The axes the joints turn about (of the bone towards a neighbour).
        const uint32_t XY = POSE_AXIS_X | POSE_AXIS_Y;
        const uint32_t XYZ = POSE_AXIS_X | POSE_AXIS_Y | POSE_AXIS_Z;
        struct S_Axes {
            int bone;
            int other;
            uint32_t axes;
        };
        static const S_Axes axes[] = {
            {POSE_BONE_PELVIS, POSE_BONE_SPINE, 0},           {POSE_BONE_SPINE, POSE_BONE_PELVIS, 0},
            {POSE_BONE_SPINE, POSE_BONE_LOWER_BACK, XYZ},     {POSE_BONE_LOWER_BACK, POSE_BONE_SPINE, 0},
            {POSE_BONE_LOWER_BACK, POSE_BONE_BACK, 0},        {POSE_BONE_BACK, POSE_BONE_LOWER_BACK, XYZ},
            {POSE_BONE_BACK, POSE_BONE_UPPER_CHEST, XYZ},     {POSE_BONE_UPPER_CHEST, POSE_BONE_BACK, 0},
            {POSE_BONE_UPPER_CHEST, POSE_BONE_CHEST, XYZ},    {POSE_BONE_CHEST, POSE_BONE_UPPER_CHEST, XYZ},
            {POSE_BONE_CHEST, POSE_BONE_LEFT_COLLAR, 0},      {POSE_BONE_LEFT_COLLAR, POSE_BONE_CHEST, XY},
            {POSE_BONE_CHEST, POSE_BONE_RIGHT_COLLAR, 0},     {POSE_BONE_RIGHT_COLLAR, POSE_BONE_CHEST, XY},
            {POSE_BONE_CHEST, POSE_BONE_NECK, 0},             {POSE_BONE_NECK, POSE_BONE_CHEST, XY},
            {POSE_BONE_NECK, POSE_BONE_HEAD_TOP, XY},         {POSE_BONE_HEAD_TOP, POSE_BONE_NECK, XY},
            {POSE_BONE_HEAD_TOP, POSE_BONE_HEAD, 0},          {POSE_BONE_HEAD, POSE_BONE_HEAD_TOP, 0},
            {POSE_BONE_LEFT_COLLAR, POSE_BONE_LEFT_SHOULDER, XY},
            {POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_COLLAR, XY},
            {POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_ELBOW, XY},
            {POSE_BONE_LEFT_ELBOW, POSE_BONE_LEFT_SHOULDER, XY},
            {POSE_BONE_LEFT_ELBOW, POSE_BONE_LEFT_HAND, XY},  {POSE_BONE_LEFT_HAND, POSE_BONE_LEFT_ELBOW, XYZ},
            {POSE_BONE_RIGHT_COLLAR, POSE_BONE_RIGHT_SHOULDER, XY},
            {POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_COLLAR, XY},
            {POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_ELBOW, XY},
            {POSE_BONE_RIGHT_ELBOW, POSE_BONE_RIGHT_SHOULDER, XY},
            {POSE_BONE_RIGHT_ELBOW, POSE_BONE_RIGHT_HAND, XY},
            {POSE_BONE_RIGHT_HAND, POSE_BONE_RIGHT_ELBOW, XYZ},
            {POSE_BONE_PELVIS, POSE_BONE_LEFT_HIP, 0},        {POSE_BONE_LEFT_HIP, POSE_BONE_PELVIS, XY},
            {POSE_BONE_LEFT_HIP, POSE_BONE_LEFT_KNEE, XY},    {POSE_BONE_LEFT_KNEE, POSE_BONE_LEFT_HIP, POSE_AXIS_X},
            {POSE_BONE_LEFT_KNEE, POSE_BONE_LEFT_FOOT, POSE_AXIS_X},
            {POSE_BONE_LEFT_FOOT, POSE_BONE_LEFT_KNEE, POSE_AXIS_X},
            {POSE_BONE_PELVIS, POSE_BONE_RIGHT_HIP, 0},       {POSE_BONE_RIGHT_HIP, POSE_BONE_PELVIS, XY},
            {POSE_BONE_RIGHT_HIP, POSE_BONE_RIGHT_KNEE, XY},
            {POSE_BONE_RIGHT_KNEE, POSE_BONE_RIGHT_HIP, POSE_AXIS_X},
            {POSE_BONE_RIGHT_KNEE, POSE_BONE_RIGHT_FOOT, POSE_AXIS_X},
            {POSE_BONE_RIGHT_FOOT, POSE_BONE_RIGHT_KNEE, POSE_AXIS_X},
        };
        for (const S_Axes &a : axes)
            SetLimitAxes(a.bone, a.other, a.axes);
        SetLimitStiffness(POSE_BONE_SPINE, POSE_BONE_LOWER_BACK, 0.2f);
        SetLimitStiffness(POSE_BONE_BACK, POSE_BONE_UPPER_CHEST, 0.4f);
        SetLimitStiffness(POSE_BONE_UPPER_CHEST, POSE_BONE_CHEST, 0.7f);
        SetLimitStiffness(POSE_BONE_LEFT_COLLAR, POSE_BONE_LEFT_SHOULDER, 0.9f);
        SetLimitStiffness(POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_ELBOW, 1.0f);
        SetLimitStiffness(POSE_BONE_LEFT_ELBOW, POSE_BONE_LEFT_HAND, 1.0f);
        SetLimitStiffness(POSE_BONE_RIGHT_COLLAR, POSE_BONE_RIGHT_SHOULDER, 0.9f);
        SetLimitStiffness(POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_ELBOW, 1.0f);
        SetLimitStiffness(POSE_BONE_RIGHT_ELBOW, POSE_BONE_RIGHT_HAND, 1.0f);
        SetLimitStiffness(POSE_BONE_CHEST, POSE_BONE_UPPER_CHEST, 0.5f);
        SetLimitStiffness(POSE_BONE_BACK, POSE_BONE_LOWER_BACK, 0.5f);

        // Each bone's children: where they are and the axes aiming at them.
        for (S_PoseBone &bone : m_bones) {
            if (bone.index < 0)
                continue;
            for (int i = 0; i < 3; ++i) {
                if (!bone.child[i])
                    continue;
                I3D_frame *child = bone.child[i]->frame;
                bone.childPos[i] = PoseRow4(child->GetLocalMatrix(), 3);
                bone.childLength[i] = PoseMagnitude(PoseXYZ(bone.childPos[i]).Square());
                bone.childMatrix[i].Identity();
                PoseAimZ(bone.childMatrix[i], PoseXYZ(bone.childPos[i]));
            }
        }
    }
    if (m_camera3rd)
        m_camera3rd->Release();
    m_camera3rd = m_model->FindChildFrame(FRAME_CAMERA_3RD, FRAME_MASK_ANY);
    if (m_camera3rd)
        m_camera3rd->AddRef();
    if (m_cameraFpv)
        m_cameraFpv->Release();
    m_cameraFpv = m_model->FindChildFrame(FRAME_CAMERA_FPV, FRAME_MASK_ANY);
    if (m_cameraFpv)
        m_cameraFpv->AddRef();
    m_hw1 = m_model->FindChildFrame(FRAME_HAND_1, FRAME_MASK_ANY);
    m_hw2 = m_model->FindChildFrame(FRAME_HAND_2, FRAME_MASK_ANY);
    m_dummyZ = m_model->FindChildFrame(FRAME_DUMMY_Z, FRAME_MASK_ANY);
    m_leftHand = m_bones[POSE_BONE_LEFT_HAND].frame;
    m_rightHand = m_bones[POSE_BONE_RIGHT_HAND].frame;
    for (S_PoseBone &bone : m_bones)
        bone.frame->SetScale(S_vector(1.0f, 1.0f, 1.0f));
    m_model->SetScale(S_vector(1.0f, 1.0f, 1.0f));
    if (IsLocalGame() && g_gameProfile.GetDetailSettings()->IsEnabled(DETAIL_STAINS)) {
        if (!m_stains)
            m_stains = new C_HumanStains(this);
        m_stains->Bind(static_cast<char>(mode));
    }
}

// 0x0079d530
void C_HumanWeaponPose::Reset()
{
    Attach(m_model, m_owner, false);
}

// ---- saved games ------------------------------------------------------------------------

// 0x0079d540: the pose (the bones and the chains while in a vehicle, the seat, the head's
// look, the prone fit, the gunner poses, the activity and death animations by name, the
// dying body, the ragdoll of a dead one with its pushes, the stains).
bool C_HumanWeaponPose::SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen())
        return false;
    if (!chunk.Begin(CHUNK_WEAPON_POSE))
        return false;
    WriteInt(chunk, m_time);
    WriteInt(chunk, m_updateCount);
    WriteInt(chunk, m_aimTime);
    WriteInt(chunk, m_aimLength);
    WriteInt(chunk, m_raised);
    WriteInt(chunk, m_weaponKind);
    WriteInt(chunk, m_stance);
    WriteInt(chunk, m_stanceBeforeSeat);
    WriteInt(chunk, static_cast<int>(m_hand));
    WriteInt(chunk, m_behaviour);
    WriteInt(chunk, static_cast<int>(m_flags));
    WriteInt(chunk, m_blendTime);
    WriteInt(chunk, m_blendLength);
    WriteInt(chunk, m_pauseTime);
    WriteInt(chunk, m_supportTime);
    chunk.Write(&m_blend, 4);
    chunk.Write(&m_support, 4);
    for (const S_vector4 *v : {&m_leftHandTarget, &m_rightHandTarget, &m_weaponDir, &m_aimDir, &m_aimFrom,
                               &m_aimTarget, &m_cameraOffset, &m_leftHandOffset, &m_rightHandOffset})
        chunk.Write(v, 12);
    chunk.Write(&m_aimAngle, 4);
    chunk.Write(&m_aimSin, 4);
    for (const S_vector4 *v : {&m_velocity, &m_lastVelocity, &m_sampledVelocity, &m_lastPos})
        chunk.Write(v, 12);
    WriteInt(chunk, m_velocityTime);
    WriteInt(chunk, static_cast<int>(POSE_SAVE_MARKER));

    // The skeleton and the chains (only while in a vehicle, not dead).
    if ((m_flags & (POSE_FLAG_STEER | POSE_FLAG_STATIC_WEAPON | POSE_FLAG_SEAT_WEAPON | POSE_FLAG_PASSENGER)) &&
        !(m_flags & POSE_FLAG_DEAD)) {
        for (const S_PoseBone &bone : m_bones) {
            WriteInt(chunk, bone.index);
            for (int i = 0; i < 3; ++i)
                chunk.Write(&bone.childPos[i], 12);
            chunk.Write(&bone.poseMatrix, sizeof(S_matrix));
            for (int i = 0; i < 3; ++i)
                chunk.Write(&bone.childMatrix[i], sizeof(S_matrix));
            for (const S_matrix *m :
                 {&bone.chainLocal, &bone.chainWorld, &bone.matrix1C0, &bone.solvedWorld, &bone.chainOffset})
                chunk.Write(m, sizeof(S_matrix));
            int frame = 0;
            for (int i = 0; i < POSE_BONE_COUNT; ++i)
                if (m_bones[i].frame == bone.chainFrame) {
                    frame = i + 1;
                    break;
                }
            WriteInt(chunk, frame);
            chunk.Write(&bone.chainLength, 4);
            WriteInt(chunk, static_cast<int>(bone.pinMask));
            // The joint as (bone << 8) | 1..3 (the limits towards the children only).
            uint32_t limit = 0;
            for (int i = 0; i < POSE_BONE_COUNT && !limit; ++i)
                for (int k = 0; k < 3; ++k)
                    if (bone.chainLimit == &m_bones[i].limits[k]) {
                        limit = static_cast<uint32_t>(i) << 8 | static_cast<uint32_t>(k + 1);
                        break;
                    }
            WriteInt(chunk, static_cast<int>(limit));
            for (int i = 0; i < 3; ++i)
                WriteInt(chunk, BoneNumber(bone.chainChildren[i]));
            WriteInt(chunk, BoneNumber(bone.chainParent));
            for (int i = 0; i < 3; ++i)
                chunk.Write(&bone.childLength[i], 4);
            for (int i = 0; i < 3; ++i)
                WriteInt(chunk, BoneNumber(bone.child[i]));
            WriteInt(chunk, BoneNumber(bone.parent));
            for (const S_PoseLimit &l : bone.limits) {
                chunk.Write(&l.max, 12);
                chunk.Write(&l.min, 12);
                chunk.Write(&l.unknown20, 12);
                WriteInt(chunk, static_cast<int>(l.axes));
                chunk.Write(&l.stiffness, 4);
            }
        }
        WriteInt(chunk, BoneNumber(m_root));
        WriteInt(chunk, m_boneCount);
        for (const S_PosePin &pin : m_chain.pins) {
            WriteInt(chunk, BoneNumber(pin.bone));
            chunk.Write(&pin.target, 12);
            chunk.Write(&pin.weight, 4);
        }
        WriteInt(chunk, BoneNumber(m_chain.root));
        WriteInt(chunk, m_chain.count);
    }
    WriteInt(chunk, static_cast<int>(POSE_SAVE_MARKER));

    // The seat.
    WriteInt(chunk, m_vehicle ? m_vehicle->GetNetId() + 1 : 0);
    WriteInt(chunk, static_cast<int>(m_seatFlags));
    WriteInt(chunk, m_seat);
    WriteInt(chunk, m_seatCount);
    for (const S_matrix &m : m_wheel)
        chunk.Write(&m, sizeof(S_matrix));
    for (const S_matrix &m : m_unknown64F0)
        chunk.Write(&m, sizeof(S_matrix));

    // The head.
    WriteInt(chunk, static_cast<int>(m_lookFlags));
    for (const S_vector4 *v : {&m_lookAt, &m_lookDir, &m_lookFrom, &m_lookTo})
        chunk.Write(v, 12);
    WriteInt(chunk, m_lookTime);
    WriteInt(chunk, m_glanceTime);
    WriteInt(chunk, m_lookLength);
    WriteInt(chunk, m_holdTime);
    chunk.Write(&m_lookPower, 4);
    chunk.Write(&m_lookWeight, 4);
    chunk.Write(&m_proneDir, 12);

    // The prone body.
    WriteInt(chunk, m_proneFit);
    WriteInt(chunk, m_proneSettle);
    for (int i = 0; i < PRONE_SPHERE_COUNT; ++i) {
        WriteInt(chunk, m_proneHits[i]);
        for (const S_vector4 *buffer : {m_proneGround, m_proneNormals, m_proneFrom, m_proneTo, m_proneNow, m_proneSpheres})
            chunk.Write(&buffer[i], 12);
        WriteInt(chunk, m_proneBlendTime[i]);
    }
    WriteInt(chunk, m_proneBlendLength);

    // The gunner poses.
    for (int bone = 0; bone < POSE_BONE_COUNT; ++bone)
        for (int k = 0; k < GUNNER_POSE_COUNT; ++k) {
            WriteQuat(chunk, m_gunnerRot[k][bone]);
            chunk.Write(&m_gunnerPos[k][bone], 12);
        }
    WriteInt(chunk, m_hasGunnerPose);

    // The activity.
    WriteAnimName(chunk, m_activityAnim);
    WriteInt(chunk, m_activityTime);
    for (int i = 0; i < POSE_BONE_COUNT; ++i) {
        WriteInt(chunk, m_activityPosKey[i]);
        WriteInt(chunk, m_activityRotKey[i]);
    }
    WriteInt(chunk, m_recoilTime);

    // The dying body (its bones as they are while it plays the animation).
    WriteInt(chunk, m_bloodPool);
    WriteAnimName(chunk, m_deathAnim);
    if (m_deathAnim) {
        for (S_PoseBone &bone : m_bones) {
            chunk.Write(&bone.frame->GetPos(), 12);
            WriteQuat(chunk, bone.frame->GetRot());
        }
    }
    WriteInt(chunk, static_cast<uint16_t>(m_deathKind));
    WriteInt(chunk, m_deathTime);
    for (int i = 0; i < POSE_BONE_COUNT; ++i) {
        WriteInt(chunk, m_deathPosKey[i]);
        WriteInt(chunk, m_deathRotKey[i]);
    }
    chunk.Write(&m_bodyPos, 12);
    chunk.Write(&m_slideVelocity, 12);
    chunk.Write(&m_cameraPos, 12);
    WriteInt(chunk, m_slidePoints ? 1 : 0);
    if (m_slidePoints)
        for (int i = 0; i < DEATH_HELD_POINT_COUNT; ++i)
            chunk.Write(&m_slidePoints[i], 12);
    WriteInt(chunk, m_carrier ? m_carrier->GetNetId() + 1 : 0);
    WriteInt(chunk, m_accelTime);
    for (const S_vector4 *v : {&m_vehicleVelocity, &m_accel, &m_lastAccel, &m_sampledAccel})
        chunk.Write(v, 12);
    WriteInt(chunk, static_cast<int>(POSE_SAVE_MARKER));

    // The ragdoll of a dead body.
    if (m_flags & POSE_FLAG_DEAD) {
        WriteInt(chunk, m_rootBody ? m_rootBody->index + 1 : 0);
        WriteInt(chunk, m_rootPoint ? m_rootPoint->index + 1 : 0);
        if (m_rootBody)
            SaveRagdollBodies(chunk, m_rootBody);
        if (m_rootPoint)
            SaveRagdollPoints(chunk, m_rootPoint);
    }
    WriteInt(chunk, static_cast<int>(POSE_SAVE_MARKER));
    chunk.Write(&m_gravity, 12);
    chunk.Write(&m_ragdollWeight, 4);
    chunk.Write(&m_settleCount, 4);
    chunk.Write(&m_settleSum, 4);
    for (int i = 0; i < RAGDOLL_PUSH_COUNT; ++i) {
        const S_RagdollPush &push = m_pushes[i];
        WriteInt(chunk, push.point ? push.point->index + 1 : 0);
        chunk.Write(&push.force, 12);
        chunk.Write(&push.time, 4);
        chunk.Write(&push.length, 4);
    }
    for (int i = 0; i < RAGDOLL_HOLD_COUNT; ++i) {
        const S_RagdollPush &hold = m_holds[i];
        WriteInt(chunk, hold.point ? hold.point->index + 1 : 0);
        chunk.Write(&hold.force, 12);
        chunk.Write(&hold.time, 4);
    }
    WriteInt(chunk, m_unknown68E8);
    chunk.Write(&m_settleDelay, 4);
    chunk.Write(&m_groundTime, 4);
    chunk.Write(&m_sinceCollision, 4);
    chunk.Write(&m_stepTime, 4);
    WriteInt(chunk, m_step);
    WriteInt(chunk, m_steps);
    WriteInt(chunk, m_unknown6904);
    WriteInt(chunk, m_unknown6908);
    WriteInt(chunk, m_contactFrames);
    WriteInt(chunk, m_groundedPoints);
    WriteInt(chunk, m_collideStep);
    WriteInt(chunk, m_inWater);
    WriteInt(chunk, m_dropSound);
    WriteInt(chunk, static_cast<int>(POSE_SAVE_MARKER));

    // The stains.
    WriteInt(chunk, m_stains ? 1 : 0);
    if (m_stains)
        m_stains->SaveGame(chunk);
    WriteInt(chunk, static_cast<int>(POSE_SAVE_MARKER));
    return chunk.Ascend();
}

// 0x0079f3f0: reads what SaveGame wrote (into the bound pose and its built ragdoll; bones and
// chain frames as numbers until Attach after the load).
bool C_HumanWeaponPose::LoadGame(C_chunk &chunk)
{
    if (!chunk.IsOpen())
        return false;
    uint16_t id = 0;
    if (!chunk.Descend(id))
        return false;
    if (id != CHUNK_WEAPON_POSE) {
        chunk.Ascend();
        return false;
    }
    chunk.Read(&m_time, 4);
    m_updateCount = ReadInt(chunk);
    chunk.Read(&m_aimTime, 4);
    chunk.Read(&m_aimLength, 4);
    m_raised = ReadInt(chunk) != 0;
    m_weaponKind = ReadInt(chunk);
    m_stance = ReadInt(chunk);
    m_stanceBeforeSeat = ReadInt(chunk);
    chunk.Read(&m_hand, 4);
    m_behaviour = ReadInt(chunk);
    chunk.Read(&m_flags, 4);
    chunk.Read(&m_blendTime, 4);
    chunk.Read(&m_blendLength, 4);
    chunk.Read(&m_pauseTime, 4);
    chunk.Read(&m_supportTime, 4);
    chunk.Read(&m_blend, 4);
    chunk.Read(&m_support, 4);
    for (S_vector4 *v : {&m_leftHandTarget, &m_rightHandTarget, &m_weaponDir, &m_aimDir, &m_aimFrom, &m_aimTarget,
                         &m_cameraOffset, &m_leftHandOffset, &m_rightHandOffset})
        chunk.Read(v, 12);
    chunk.Read(&m_aimAngle, 4);
    chunk.Read(&m_aimSin, 4);
    for (S_vector4 *v : {&m_velocity, &m_lastVelocity, &m_sampledVelocity, &m_lastPos})
        chunk.Read(v, 12);
    chunk.Read(&m_velocityTime, 4);
    ReadInt(chunk);

    if ((m_flags & (POSE_FLAG_STEER | POSE_FLAG_STATIC_WEAPON | POSE_FLAG_SEAT_WEAPON | POSE_FLAG_PASSENGER)) &&
        !(m_flags & POSE_FLAG_DEAD)) {
        auto boneOf = [&](int number) { return number ? &m_bones[number - 1] : nullptr; };
        for (S_PoseBone &bone : m_bones) {
            bone.index = ReadInt(chunk);
            for (int i = 0; i < 3; ++i)
                chunk.Read(&bone.childPos[i], 12);
            chunk.Read(&bone.poseMatrix, sizeof(S_matrix));
            for (int i = 0; i < 3; ++i)
                chunk.Read(&bone.childMatrix[i], sizeof(S_matrix));
            for (S_matrix *m :
                 {&bone.chainLocal, &bone.chainWorld, &bone.matrix1C0, &bone.solvedWorld, &bone.chainOffset})
                chunk.Read(m, sizeof(S_matrix));
            bone.chainFrame = reinterpret_cast<I3D_frame *>(static_cast<intptr_t>(ReadInt(chunk)));
            chunk.Read(&bone.chainLength, 4);
            chunk.Read(&bone.pinMask, 4);
            const uint32_t limit = static_cast<uint32_t>(ReadInt(chunk));
            const int limitBone = static_cast<int>(limit) >> 8;
            switch (limit & 0xFF) {
            case 1:
                bone.chainLimit = &m_bones[limitBone].limits[0];
                break;
            case 2:
                bone.chainLimit = &m_bones[limitBone].limits[1];
                break;
            case 3:
                bone.chainLimit = &m_bones[limitBone].limits[2];
                break;
            default:
                bone.chainLimit = nullptr;
                break;
            }
            for (int i = 0; i < 3; ++i)
                bone.chainChildren[i] = boneOf(ReadInt(chunk));
            bone.chainParent = boneOf(ReadInt(chunk));
            for (int i = 0; i < 3; ++i)
                chunk.Read(&bone.childLength[i], 4);
            for (int i = 0; i < 3; ++i)
                bone.child[i] = boneOf(ReadInt(chunk));
            bone.parent = boneOf(ReadInt(chunk));
            for (S_PoseLimit &l : bone.limits) {
                chunk.Read(&l.max, 12);
                chunk.Read(&l.min, 12);
                chunk.Read(&l.unknown20, 12);
                chunk.Read(&l.axes, 4);
                chunk.Read(&l.stiffness, 4);
            }
        }
        m_root = boneOf(ReadInt(chunk));
        chunk.Read(&m_boneCount, 4);
        for (S_PosePin &pin : m_chain.pins) {
            pin.bone = boneOf(ReadInt(chunk));
            chunk.Read(&pin.target, 12);
            chunk.Read(&pin.weight, 4);
        }
        m_chain.root = boneOf(ReadInt(chunk));
        chunk.Read(&m_chain.count, 4);
    }
    ReadInt(chunk);

    // The seat (its grips found on the vehicle again).
    const int vehicle = ReadInt(chunk);
    m_vehicle = vehicle ? g_actorsModule.GetActorById(vehicle - 1) : nullptr;
    m_seatFlags = static_cast<uint32_t>(ReadInt(chunk));
    chunk.Read(&m_seat, 4);
    chunk.Read(&m_seatCount, 4);
    m_seatRightGrip = nullptr;
    m_seatLeftGrip = nullptr;
    if (m_vehicle) {
        if (m_flags & POSE_FLAG_STEER) {
            m_seatLeftGrip = m_vehicle->GetFrame()->FindChildFrame(GRIP_LEFT_CAR, FRAME_MASK_ANY);
            m_seatRightGrip = m_vehicle->GetFrame()->FindChildFrame(GRIP_RIGHT_CAR, FRAME_MASK_ANY);
        } else if (m_flags & (POSE_FLAG_STATIC_WEAPON | POSE_FLAG_SEAT_WEAPON)) {
            m_seatLeftGrip = m_vehicle->GetFrame()->FindChildFrame(GRIP_LEFT_SEAT, FRAME_MASK_ANY);
            m_seatRightGrip = m_vehicle->GetFrame()->FindChildFrame(GRIP_RIGHT_SEAT, FRAME_MASK_ANY);
        }
    }
    for (S_matrix &m : m_wheel)
        chunk.Read(&m, sizeof(S_matrix));
    for (S_matrix &m : m_unknown64F0)
        chunk.Read(&m, sizeof(S_matrix));

    chunk.Read(&m_lookFlags, 4);
    for (S_vector4 *v : {&m_lookAt, &m_lookDir, &m_lookFrom, &m_lookTo})
        chunk.Read(v, 12);
    chunk.Read(&m_lookTime, 4);
    chunk.Read(&m_glanceTime, 4);
    chunk.Read(&m_lookLength, 4);
    chunk.Read(&m_holdTime, 4);
    chunk.Read(&m_lookPower, 4);
    chunk.Read(&m_lookWeight, 4);
    chunk.Read(&m_proneDir, 12);
    m_proneFit = ReadInt(chunk) != 0;
    chunk.Read(&m_proneSettle, 4);
    for (int i = 0; i < PRONE_SPHERE_COUNT; ++i) {
        m_proneHits[i] = static_cast<uint8_t>(ReadInt(chunk));
        for (S_vector4 *buffer : {m_proneGround, m_proneNormals, m_proneFrom, m_proneTo, m_proneNow, m_proneSpheres})
            chunk.Read(&buffer[i], 12);
        chunk.Read(&m_proneBlendTime[i], 4);
    }
    chunk.Read(&m_proneBlendLength, 4);
    for (int bone = 0; bone < POSE_BONE_COUNT; ++bone)
        for (int k = 0; k < GUNNER_POSE_COUNT; ++k) {
            float f[4];
            chunk.Read(f, sizeof(f));
            m_gunnerRot[k][bone] = S_quat::FromFile(f);
            chunk.Read(&m_gunnerPos[k][bone], 12);
        }
    m_hasGunnerPose = ReadInt(chunk) != 0;

    m_activityAnim = ReadAnim(chunk);
    chunk.Read(&m_activityTime, 4);
    for (int i = 0; i < POSE_BONE_COUNT; ++i) {
        chunk.Read(&m_activityPosKey[i], 4);
        chunk.Read(&m_activityRotKey[i], 4);
    }
    chunk.Read(&m_recoilTime, 4);

    m_bloodPool = ReadInt(chunk) != 0;
    m_deathAnim = ReadAnim(chunk);
    if (m_deathAnim) {
        // The bones' local matrices (set by Attach after the load).
        for (S_PoseBone &bone : m_bones) {
            S_matrix &m = bone.poseMatrix;
            chunk.Read(&m.m[3][0], 12);
            float f[4];
            chunk.Read(f, sizeof(f));
            m.SetRot3(S_quat::FromFile(f));
            m.m[3][3] = 1.0f;
            m.m[2][3] = 0.0f;
            m.m[1][3] = 0.0f;
            m.m[0][3] = 0.0f;
        }
    }
    m_deathKind = static_cast<int16_t>(ReadInt(chunk));
    chunk.Read(&m_deathTime, 4);
    for (int i = 0; i < POSE_BONE_COUNT; ++i) {
        chunk.Read(&m_deathPosKey[i], 4);
        chunk.Read(&m_deathRotKey[i], 4);
    }
    chunk.Read(&m_bodyPos, 12);
    chunk.Read(&m_slideVelocity, 12);
    chunk.Read(&m_cameraPos, 12);
    if (ReadInt(chunk)) {
        m_slidePoints = static_cast<S_vector4 *>(_aligned_malloc(DEATH_HELD_POINT_COUNT * sizeof(S_vector4), 16));
        for (int i = 0; i < DEATH_HELD_POINT_COUNT; ++i)
            chunk.Read(&m_slidePoints[i], 12);
    }
    const int carrier = ReadInt(chunk);
    m_carrier = carrier ? static_cast<C_Human *>(g_actorsModule.GetActorById(carrier - 1)) : nullptr;
    chunk.Read(&m_accelTime, 4);
    for (S_vector4 *v : {&m_vehicleVelocity, &m_accel, &m_lastAccel, &m_sampledAccel})
        chunk.Read(v, 12);
    ReadInt(chunk);

    if (m_flags & POSE_FLAG_DEAD) {
        const int body = ReadInt(chunk);
        m_rootBody = body ? m_bodies[body - 1] : nullptr;
        const int point = ReadInt(chunk);
        m_rootPoint = point ? m_points[point - 1] : nullptr;
        if (m_rootBody)
            LoadRagdollBodies(chunk, m_rootBody);
        if (m_rootPoint)
            LoadRagdollPoints(chunk, m_rootPoint);
    }
    ReadInt(chunk);
    chunk.Read(&m_gravity, 12);
    chunk.Read(&m_ragdollWeight, 4);
    chunk.Read(&m_settleCount, 4);
    chunk.Read(&m_settleSum, 4);
    auto pointOf = [&](int number) { return number ? m_points[number - 1] : nullptr; };
    for (int i = 0; i < RAGDOLL_PUSH_COUNT; ++i) {
        S_RagdollPush &push = m_pushes[i];
        push.point = pointOf(ReadInt(chunk));
        chunk.Read(&push.force, 12);
        chunk.Read(&push.time, 4);
        chunk.Read(&push.length, 4);
    }
    for (int i = 0; i < RAGDOLL_HOLD_COUNT; ++i) {
        S_RagdollPush &hold = m_holds[i];
        hold.point = pointOf(ReadInt(chunk));
        chunk.Read(&hold.force, 12);
        chunk.Read(&hold.time, 4);
    }
    chunk.Read(&m_unknown68E8, 4);
    chunk.Read(&m_settleDelay, 4);
    chunk.Read(&m_groundTime, 4);
    chunk.Read(&m_sinceCollision, 4);
    chunk.Read(&m_stepTime, 4);
    chunk.Read(&m_step, 4);
    chunk.Read(&m_steps, 4);
    chunk.Read(&m_unknown6904, 4);
    chunk.Read(&m_unknown6908, 4);
    chunk.Read(&m_contactFrames, 4);
    // (Counted again from the contacts found by name.)
    m_contactFrames = 0;
    if (m_flags & POSE_FLAG_DEAD)
        for (S_RagdollPoint *p : m_points)
            if (p && p->contactFrame)
                ++m_contactFrames;
    m_groundedPoints = static_cast<uint8_t>(ReadInt(chunk));
    m_collideStep = ReadInt(chunk) != 0;
    m_inWater = ReadInt(chunk) != 0;
    m_dropSound = ReadInt(chunk);
    if (m_dropSound >= 0)
        m_dropSound = -2;
    ReadInt(chunk);
    if (ReadInt(chunk)) {
        if (!m_stains)
            C_HumanStains::SkipSaved(chunk);
        else
            m_stains->LoadGame(chunk);
    }
    ReadInt(chunk);
    chunk.Ascend();
    return true;
}

// ---- the update ---------------------------------------------------------------------------

// The hands keep holding the weapon while it is in a hand and has its grips (the support hand
// needs the left one); otherwise the pose lets it go.
bool C_HumanWeaponPose::KeepsWeapon()
{
    if (m_weapon && m_weapon->GetParent() && (m_rightGrip || m_leftGrip) &&
        !(IsSupportHandOn() && !m_leftGrip))
        return true;
    m_flags = 0;
    m_raised = false;
    return false;
}

// 0x007a78d0: the pose for `time` ms: the model's smoothed speed (sampled every 100 ms); a
// dead body plays its death animation or simulates its ragdoll (in 100 ms steps, shown blended
// between them); otherwise the mode of the pose, the raise / lower and support hand blends,
// the seat, the recoil, the activity and the head; the cameras kept out of walls.
void C_HumanWeaponPose::Update(int time)
{
    m_velocityTime -= time;
    m_time = time;
    ++m_updateCount;
    if (m_velocityTime < 1) {
        m_velocityTime += 100;
        m_lastVelocity = m_velocity;
        const float k = 1000.0f / static_cast<float>(m_time);
        PoseSetXYZ(m_sampledVelocity, (m_model->GetWorldPos() - PoseXYZ(m_lastPos)) * k);
    }
    const float t = static_cast<float>(m_velocityTime) * 0.01f;
    m_lastPos = PoseWorldPos4(m_model);
    PoseSetXYZ(m_velocity, PoseXYZ(m_lastVelocity) * t + PoseXYZ(m_sampledVelocity) * (1.0f - t));
    m_visible = true;
    if (m_movement)
        m_visible = !(m_movement->GetFlags() & (MOVE_FLAG_TINY | MOVE_FLAG_HIDDEN | MOVE_FLAG_NOT_DRAWN));

    if (m_flags & POSE_FLAG_DEAD) {
        UpdateDead(time);
        return;
    }
    if (m_owner && m_owner->IsDead())
        return;
    if (m_proneFit && m_movement && m_movement->m_owner->GetWork()->m_mode != WORK_MODE_SWIMMING &&
        (IsLocalGame() || m_owner->IsLocalPlayer()))
        FitProneBody();
    const uint32_t mode = m_flags;
    if (mode & POSE_FLAG_HOLD) {
        if (!KeepsWeapon())
            return;
        UpdateHold();
    } else if (mode & POSE_FLAG_FREE) {
        UpdateFree();
    } else if (mode & POSE_FLAG_STEER) {
        UpdateSteering();
        TurnHead(m_model->GetMatrix()(2), 1.0f);
    } else if (mode & POSE_FLAG_STATIC_WEAPON) {
        UpdateStaticWeapon();
    } else if (mode & POSE_FLAG_SEAT_WEAPON) {
        UpdateSeat();
    } else if (mode & POSE_FLAG_GRIP) {
        if (!KeepsWeapon())
            return;
        UpdateGrip();
    }

    // Raising and lowering the weapon; a pause runs out into raising it.
    if (m_flags & POSE_FLAG_RAISE) {
        m_blendTime += time;
        if (m_blendLength < m_blendTime) {
            m_blendTime = m_blendLength;
            m_flags &= ~POSE_FLAG_RAISE;
        }
        m_blend = static_cast<float>(m_blendTime) / static_cast<float>(m_blendLength);
    } else if (m_flags & POSE_FLAG_LOWER) {
        m_blendTime -= time;
        if (m_blendTime < 1) {
            m_blendTime = 0;
            m_flags &= ~(POSE_FLAG_LOWER | POSE_FLAG_HOLD | POSE_FLAG_FREE);
            // A pistol lowered points along its hand again.
            if (m_weapon && m_weaponKind == WEAPON_KIND_PISTOL)
                m_weapon->SetWorldDir(S_vector((m_hand & 0xFF) ? 1.0f : -1.0f, 0.0f, 0.0f), 0.0f);
        }
        m_blend = static_cast<float>(m_blendTime) / static_cast<float>(m_blendLength);
    } else if ((m_flags & POSE_FLAG_PAUSED) && (m_pauseTime -= time) < 1) {
        m_pauseTime = 0;
        if (!(m_flags & POSE_FLAG_NO_RAISE))
            RaiseWeapon(300);
        m_flags &= ~(POSE_FLAG_PAUSED | POSE_FLAG_NO_RAISE | POSE_FLAG_FREE);
    }

    // The support hand onto / off the grip (200 ms).
    if (m_flags & POSE_FLAG_SUPPORT_ON) {
        m_supportTime += time;
        if (m_supportTime > 200) {
            m_supportTime = 200;
            m_flags &= ~POSE_FLAG_SUPPORT_ON;
        }
        m_support = static_cast<float>(m_supportTime) * 0.005f;
    } else if (m_flags & POSE_FLAG_SUPPORT_OFF) {
        m_supportTime -= time;
        if (m_supportTime < 1) {
            m_supportTime = 0;
            m_flags &= ~POSE_FLAG_SUPPORT_OFF;
            const uint32_t hand = m_hand;
            m_hand &= ~0x80000000u;
            // The weapon back along its hand, where it is now against its grip.
            I3D_frame *axes = nullptr;
            if ((hand & 0xFF) == 1 && m_weapon) {
                m_weapon->SetWorldDir(S_vector(1.0f, 0.0f, 0.0f), 0.0f);
                axes = m_hw1;
            } else if ((hand & 0xFF) == 0 && m_weapon) {
                m_weapon->SetWorldDir(S_vector(-1.0f, 0.0f, 0.0f), 0.0f);
                axes = m_hw2;
            }
            if (axes) {
                const S_vector offset = m_weapon->GetWorldPos() - m_rightGrip->GetWorldPos();
                m_weapon->SetPosInline(PoseIntoAxes(axes->GetMatrix(), offset));
            }
        }
        m_support = static_cast<float>(m_supportTime) * 0.005f;
    }

    if (m_flags & POSE_FLAG_PASSENGER)
        UpdateSeat();
    if (m_flags & POSE_FLAG_RECOIL)
        UpdateRecoil();
    if (m_flags & POSE_FLAG_ACTIVITY)
        UpdateActivity();
    UpdateHeadLook(time);
    KeepCamerasOutOfWalls();
    if (g_poseDebugJoints && m_bones[POSE_BONE_PELVIS].frame)
        static_cast<I3D_joint *>(m_bones[POSE_BONE_PELVIS].frame)->UnknownA8(0);
}

// The dead body's part of 0x007a78d0.
void C_HumanWeaponPose::UpdateDead(int time)
{
    if (m_deathAnim) {
        UpdateDeathAnim();
        const S_vector spine = m_bones[POSE_BONE_SPINE].frame->GetWorldPos();
        const float moved = (PoseXYZ(m_bodyPos) - spine).Square();
        if (1e-6f < moved)
            GetGame()->GetScene()->Unknown144(m_model);
        if (1.0f < moved) {
            m_bodyPos = PoseWorldPos4(m_bones[POSE_BONE_SPINE].frame);
            m_movement->Ragdoll(true, true);
            return;
        }
        m_movement->Ragdoll(true, false);
        return;
    }
    if (1e-8f < m_ragdollWeight) {
        m_owner->RemoveCollision(false);
        if (m_carrier)
            m_carrier->RemoveCollision(false);
        // The simulation in 100 ms steps: the last step's result is the start of the next.
        m_unknown68E8 -= time;
        if (m_unknown68E8 < 1) {
            m_unknown68E8 += 100;
            ResetRagdollToRest(m_rootBody);
            UpdateRagdollContacts(100);
            StepRagdoll(100);
            StoreRagdollRest(m_rootBody);
            S_RagdollPoint *pelvis = m_points[RAGDOLL_POINT_PELVIS];
            const bool moved = 1.0f < (PoseXYZ(m_bodyPos) - PoseXYZ(pelvis->pos)).Square();
            if (moved)
                m_bodyPos = pelvis->pos;
            m_movement->Ragdoll(true, moved);
        }
        // Shown between the last two steps.
        BlendRagdollToRest(m_rootBody, 1.0f - static_cast<float>(m_unknown68E8) * 0.01f);
        const S_vector4 before = PoseWorldPos4(m_model);
        PoseFromRagdoll(m_rootBody, 1.0f);
        if (1e-6f < (PoseXYZ(before) - m_model->GetWorldPos()).Square())
            GetGame()->GetScene()->Unknown144(m_model);
        // The first person camera follows the head.
        if (m_owner->IsLocalPlayer()) {
            C_CameraModule *cameras = GetGame()->GetCameraModule();
            S_RagdollPoint *head = m_points[RAGDOLL_POINT_HEAD];
            // Fix: the original follows the head point without checking it is still there (it
            // is dropped with a collapsed head).
            if (cameras && cameras->GetCamera() == m_cameraFpv && head) {
                S_vector hit;
                if (CollisionSphereTest(PoseXYZ(m_cameraPos), PoseXYZ(head->pos) - PoseXYZ(m_cameraPos), 0.17f, &hit,
                                        nullptr, CAMERA_TEST_TYPES, CAMERA_TEST_FLAGS))
                    m_cameraPos = PoseVector4(hit, before.w);
                else
                    m_cameraPos = head->pos;
                m_cameraFpv->SetPos(PoseXYZ(m_cameraPos));
            }
        }
        m_owner->RestoreCollision(false);
        if (m_carrier)
            m_carrier->RestoreCollision(false);
        return;
    }
    // At rest: let go of moving contacts, end the sounds.
    if (m_contactFrames)
        UpdateRagdollContacts(time);
    if (m_impactSound != -1)
        UpdateRagdollSound(false);
    if (m_dropSound < 0)
        return;
    GetSoundModule()->Stop(&m_dropSound);
    m_dropSound = -2;
}

// The cameras of 0x007a78d0 (clients): the third person camera swept from the head, the first
// person one from the chest, stopped at walls.
// Quirk: the hit point (world) is set as the camera dummy's local position.
void C_HumanWeaponPose::KeepCamerasOutOfWalls()
{
    C_CameraModule *cameras = GetGame()->GetCameraModule();
    if (!cameras || !IsClient())
        return;
    I3D_frame *camera = cameras->GetCamera();
    I3D_frame *from = nullptr;
    if (camera == m_camera3rd)
        from = m_bones[POSE_BONE_HEAD].frame;
    else if (cameras->GetCamera() == m_cameraFpv)
        from = m_bones[POSE_BONE_CHEST].frame;
    if (!from)
        return;
    camera = camera == m_camera3rd ? m_camera3rd : m_cameraFpv;
    if (m_owner)
        m_owner->RemoveCollision(false);
    const S_vector dir = camera->GetWorldPos() - from->GetWorldPos();
    S_vector hit;
    if (CollisionSphereTest(from->GetWorldPos(), dir, 0.15f, &hit, nullptr, CAMERA_TEST_TYPES, CAMERA_TEST_FLAGS))
        camera->SetPos(hit);
    if (m_owner)
        m_owner->RestoreCollision(false);
}

// 0x007a8760: the pose again once per update of the owner (the mode and the head, no time
// passing). (`unused` is not used.)
void C_HumanWeaponPose::Refresh(int unused)
{
    (void)unused;
    if (m_updateSeen == m_updateCount)
        return;
    m_updateSeen = m_updateCount;
    m_visible = true;
    if (m_proneFit && m_movement && m_movement->m_owner->GetWork()->m_mode != WORK_MODE_SWIMMING &&
        (IsLocalGame() || m_owner->IsLocalPlayer()))
        FitProneBody();
    const uint32_t mode = m_flags;
    if (mode & POSE_FLAG_HOLD) {
        if (!KeepsWeapon())
            return;
        UpdateHold();
    } else if (mode & POSE_FLAG_FREE) {
        UpdateFree();
    } else if (mode & POSE_FLAG_STEER) {
        UpdateSteering();
        TurnHead(m_model->GetMatrix()(2), 1.0f);
    } else if (mode & POSE_FLAG_STATIC_WEAPON) {
        UpdateStaticWeapon();
    } else if (mode & POSE_FLAG_SEAT_WEAPON) {
        UpdateSeat();
    } else if (mode & POSE_FLAG_GRIP) {
        if (!KeepsWeapon())
            return;
        UpdateGrip();
    }
    UpdateHeadLook(0);
}

// ---- inline setters of the users --------------------------------------------------------

// (Inline in C_HumanIdle::UpdateInteraction.) The stance taking a seat: prone starts fitting
// the body to the ground (settling for 500 ms), anything else stops it.
void C_HumanWeaponPose::SetSeatStance(int stance)
{
    m_stance = stance;
    if (stance == POSE_STANCE_PRONE) {
        m_proneFit = true;
        m_proneSettle = 500;
    }
    if (stance != POSE_STANCE_PRONE)
        m_proneFit = false;
}

// (Inline in C_HumanLocomotion::ChangeStance.) A change of stance: going prone starts fitting
// the body to the ground.
void C_HumanWeaponPose::SetStanceChange(int stance)
{
    m_stance = stance;
    if (stance == POSE_STANCE_PRONE) {
        m_proneFit = true;
        m_proneSettle = 500;
    }
}

// (Inline in the animation player.) The raise / lower blends take `time` ms (the blend kept
// where it is).
void C_HumanWeaponPose::SetBlendLength(int time)
{
    m_blendLength = time;
    m_blendTime = static_cast<int>(static_cast<float>(time) * m_blend);
}

// 0x007b8a00
void C_HumanWeaponPose::SetStainsVisible(bool on)
{
    if (m_stains)
        m_stains->SetVisible(on);
}
