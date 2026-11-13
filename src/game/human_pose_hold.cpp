// The weapon held up in the stances (original source 0x007bac90..0x007bfde0): the aim kept
// within reach of the stance, the spine turned towards it, the weapon aimed and the hands put
// on its grips. A rifle turns the lower and upper back ("c1", "d1") by weights that depend on
// the stance and the body's speed; a pistol (m_weaponKind 4) turns them towards the right
// hand's x axis and the collar after them.
#include "game/human_pose_internal.h"

#include <cmath>

#include "game/collision.h"
#include "game/human.h"
#include "ls3d/i3d.h"

namespace {

const S_vector UP(0.0f, 1.0f, 0.0f);
const S_vector DOWN(0.0f, -1.0f, 0.0f);

// What the prone hold's ray down from the chest tests (C_CollisionWorld type and flag masks to
// skip).
const uint32_t PRONE_HOLD_TEST_TYPES = 0xC8102;
const uint32_t PRONE_HOLD_TEST_FLAGS = 0x230000;

}  // namespace

void C_HumanWeaponPose::UpdateRightHandTarget()
{
    PoseSetXYZ(m_rightHandTarget, PoseXYZ(m_rightHandOffset) * m_rightHand->GetMatrix());
}

void C_HumanWeaponPose::UpdateLeftHandTarget()
{
    PoseSetXYZ(m_leftHandTarget, PoseXYZ(m_leftHandOffset) * m_leftHand->GetMatrix());
}

// From the right hand's target to the left one's.
S_vector C_HumanWeaponPose::HandsDir() const
{
    S_vector d = PoseXYZ(m_leftHandTarget) - PoseXYZ(m_rightHandTarget);
    d.Normalize();
    return d;
}

// The direction the weapon hand holds the weapon in: hw1's x axis (right hand) or hw2's
// reversed (left hand); `dir` as it is for other hands.
S_vector C_HumanWeaponPose::HandAxis(const S_vector &dir) const
{
    switch (m_hand & 0xFF) {
    case 1:
        return m_hw1->GetMatrix()(0);
    case 0:
        return -m_hw2->GetMatrix()(0);
    default:
        return dir;
    }
}

// The aim within 0.7 of the model's up and down (its horizontal part scaled to 0.7141) and at
// least `minForward` forward (the rest scaled to `side`).
void C_HumanWeaponPose::ClampAim(float minForward, float side)
{
    const S_matrix &model = m_model->GetMatrix();
    S_vector aim = PoseXYZ(m_aimDir);
    float d = PoseDotZYX(aim, model(1));
    if (d > 0.7f || d < -0.7f) {
        const float sign = PoseIsNegative(d) ? -1.0f : 1.0f;
        const S_vector perp = aim - model(1) * d;
        const float s = std::sqrt(1.0f - d * d);
        if (!(s > 1e-8f))
            aim = model(1) * sign;
        else
            aim = perp * (0.7141f / s) + model(1) * (sign * 0.7f);
        PoseSetXYZ(m_aimDir, aim);
    }
    d = PoseDotZYX(aim, model(2));
    if (d < minForward) {
        const S_vector perp = aim - model(2) * d;
        const float s = std::sqrt(1.0f - d * d);
        if (!(s > 1e-8f))
            m_aimDir = PoseRow4(model, 2);
        else
            PoseSetXYZ(m_aimDir, perp * (side / s) + model(2) * minForward);
    }
}

// A pistol: the back towards the aim (standing still or seated by 0.35 / 0.75; walking the
// upper back straightened up first, then 0.15 / 0.35 and 0.25 / 0.35), the collar after them,
// the weapon from the right hand's axis towards the aim by m_blend and the left hand onto
// the grip by m_support.
void C_HumanWeaponPose::HoldPistol(bool moving)
{
    const float b = m_blend;
    S_vector collar;
    if (!moving) {
        TurnBone(POSE_BONE_LOWER_BACK, m_rightHand->GetMatrix()(0), m_aimDir, S_vector(b * 0.35f, b * 0.75f, b * 0.0f));
        TurnBone(POSE_BONE_UPPER_CHEST, m_rightHand->GetMatrix()(0), m_aimDir, S_vector(b * 0.35f, b * 0.75f, b * 0.0f));
        collar = S_vector(b * 0.75f, b * 0.75f, b * 0.0f);
    } else {
        S_vector up = m_bones[POSE_BONE_NECK].frame->GetWorldPos() - m_bones[POSE_BONE_UPPER_CHEST].frame->GetWorldPos();
        up.Normalize();
        TurnBone(POSE_BONE_UPPER_CHEST, up, PoseVector4(UP, 0.0f), S_vector(b * 0.0f, b * 0.0f, b * 0.5f));
        TurnBone(POSE_BONE_LOWER_BACK, m_rightHand->GetMatrix()(0), m_aimDir, S_vector(b * 0.15f, b * 0.35f, b * 0.0f));
        TurnBone(POSE_BONE_UPPER_CHEST, m_rightHand->GetMatrix()(0), m_aimDir, S_vector(b * 0.25f, b * 0.35f, b * 0.0f));
        collar = S_vector(b * 0.25f, b * 0.5f, b * 0.0f);
    }
    TurnBone(POSE_BONE_RIGHT_COLLAR, m_rightHand->GetMatrix()(0), m_aimDir, collar);
    UpdateRightHandTarget();
    UpdateLeftHandTarget();
    PoseSetXYZ(m_weaponDir, PoseSlerpDir(m_bones[POSE_BONE_RIGHT_HAND].frame->GetMatrix()(0), PoseXYZ(m_aimDir), m_blend));
    AimWeapon(m_weaponDir, true, true, 1.0f);
    if (IsSupportHandOn()) {
        UpdateLeftHandTarget();
        ReachArm(POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_ELBOW, m_leftGrip->GetWorldPos(), m_leftHandTarget,
                 m_leftHand, m_support, nullptr);
    }
}

// A rifle: the lower and upper back turn the line between the hands towards the aim (by
// `lower` / `upper` times m_blend; `axes`: about the spine's axes, else the model's), then the
// right arm settles 80% back towards where its target was (`settle`, without a stock); the
// weapon turns from the hand's axis (towards the hands' line by m_support) to the aim and the
// hands go onto the grips.
void C_HumanWeaponPose::HoldRifle(const S_matrix *axes, const S_vector &lower, const S_vector &upper, bool settle)
{
    UpdateRightHandTarget();
    UpdateLeftHandTarget();
    S_vector dir = HandsDir();
    const S_vector4 rightTarget = m_rightHandTarget;
    if (axes)
        TurnBoneAbout(POSE_BONE_LOWER_BACK, dir, m_aimDir, *axes, lower * m_blend);
    else
        TurnBone(POSE_BONE_LOWER_BACK, dir, m_aimDir, lower * m_blend);
    UpdateRightHandTarget();
    UpdateLeftHandTarget();
    dir = HandsDir();
    if (axes)
        TurnBoneAbout(POSE_BONE_UPPER_CHEST, dir, m_aimDir, *axes, upper * m_blend);
    else
        TurnBone(POSE_BONE_UPPER_CHEST, dir, m_aimDir, upper * m_blend);
    if (settle && !m_stock && m_visible) {
        UpdateRightHandTarget();
        UpdateLeftHandTarget();
        const S_vector from = PoseXYZ(rightTarget);
        const S_vector goal = (PoseXYZ(m_rightHandTarget) - from) * 0.8f + from;
        ReachArm(POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_ELBOW, goal, m_rightHandTarget, m_rightHand, 1.0f, nullptr);
    }
    HoldOnGrips(dir);
}

// The weapon from the hand's axis (with the support hand: towards the line between the hands)
// to the aim by m_blend, then the hands onto the grips (the left by m_support; with a stock
// the elbow down and the right hand onto its grip as well).
void C_HumanWeaponPose::HoldOnGrips(S_vector dir)
{
    if (IsSupportHandOn()) {
        UpdateRightHandTarget();
        UpdateLeftHandTarget();
        dir = HandsDir();
        const uint32_t side = m_hand & 0xFF;
        if (side == 1)
            dir = PoseSlerpDir(m_hw1->GetMatrix()(0), dir, m_support);
        else if (side == 0)
            dir = PoseSlerpDir(-m_hw2->GetMatrix()(0), dir, m_support);
    } else {
        dir = HandAxis(dir);
    }
    PoseSetXYZ(m_weaponDir, PoseSlerpDir(dir, PoseXYZ(m_aimDir), m_blend));
    AimWeapon(m_weaponDir, true, true, m_support);
    if (!m_visible)
        return;
    UpdateRightHandTarget();
    UpdateLeftHandTarget();
    if (IsSupportHandOn())
        ReachArm(POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_ELBOW, m_leftGrip->GetWorldPos(), m_leftHandTarget,
                 m_leftHand, m_support, m_stock ? &DOWN : nullptr);
    if (m_stock)
        ReachArm(POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_ELBOW, m_rightGrip->GetWorldPos(), m_rightHandTarget,
                 m_rightHand, 1.0f, nullptr);
}

// The spine's axes: the model's turned so that y runs from the spine to the chest.
S_matrix C_HumanWeaponPose::SpineAxes() const
{
    S_vector up = m_bones[POSE_BONE_CHEST].frame->GetWorldPos() - m_bones[POSE_BONE_SPINE].frame->GetWorldPos();
    up.Normalize();
    S_matrix axes = m_model->GetMatrix();
    PoseTurnYTo(axes, up);
    return axes;
}

static float SquaredSpeed(const C_Human *owner)
{
    if (!owner)
        return 0.0f;
    const S_vector &v = owner->GetVelocity();
    return v.x * v.x + v.y * v.y + v.z * v.z;   // (x first, as the original sums it)
}

// 0x007bd420
void C_HumanWeaponPose::HoldStanding()
{
    ClampAim(0.34f, 0.9404f);
    if (m_weaponKind == WEAPON_KIND_PISTOL) {
        HoldPistol(m_owner && SquaredSpeed(m_owner) > 1e-8f);
        return;
    }
    const S_matrix axes = SpineAxes();
    // The weights go from standing still to moving at 4 m/s.
    float speed = 0.0f;
    if (m_owner) {
        speed = SquaredSpeed(m_owner) * 0.0625f;
        if (speed > 1.0f)
            speed = 1.0f;
    }
    const float still = 1.0f - speed;
    const S_vector lower(speed * 0.15f + still * 0.35f, speed * 0.7f + still * 0.95f, 0.0f);
    const S_vector upper(speed * 0.25f + still * 0.45f, lower.y, 0.0f);
    HoldRifle(&axes, lower, upper, speed < 1e-8f);
}

// 0x007bb9a0
void C_HumanWeaponPose::HoldCrouching()
{
    ClampAim(0.34f, 0.9404f);
    const bool moving = m_owner && SquaredSpeed(m_owner) > 1e-8f;
    if (m_weaponKind == WEAPON_KIND_PISTOL) {
        HoldPistol(moving);
        return;
    }
    const S_matrix axes = SpineAxes();
    if (!moving)
        HoldRifle(&axes, S_vector(0.25f, 0.75f, 0.0f), S_vector(0.25f, 0.75f, 0.0f), true);
    else
        HoldRifle(&axes, S_vector(0.15f, 0.7f, 0.0f), S_vector(0.25f, 0.7f, 0.0f), false);
}

// 0x007beb50
void C_HumanWeaponPose::HoldSeated()
{
    ClampAim(0.707f, 0.707f);
    if (m_weaponKind == WEAPON_KIND_PISTOL) {
        HoldPistol(false);
        return;
    }
    HoldRifle(nullptr, S_vector(0.35f, 0.95f, 0.0f), S_vector(0.45f, 0.95f, 0.0f), true);
}

// 0x007bac90: lying: the aim within 60 degrees down / 71 up of the ground under the hands
// (the normal from a ray down from between them) and 30 degrees of the model's forward; a
// pistol only turns the collar, the left hand aiming between the grips.
void C_HumanWeaponPose::HoldProne()
{
    S_vector from = PoseXYZ(m_proneNow[1]) * 0.5f + (m_bones[POSE_BONE_RIGHT_HAND].frame->GetWorldPos() * 0.25f +
                                                     m_bones[POSE_BONE_LEFT_HAND].frame->GetWorldPos() * 0.75f);
    S_vector normal = PoseXYZ(m_proneNow[1]);
    S_vector hit;
    CollisionRayTest(from, PoseXYZ(m_proneNow[1]) * -2.0f, &hit, &normal, PRONE_HOLD_TEST_TYPES, PRONE_HOLD_TEST_FLAGS);
    S_vector aim = PoseXYZ(m_aimDir);
    float d = PoseDotZYX(aim, normal);
    if (d < -0.5f) {
        const S_vector perp = aim - normal * d;
        const float s = std::sqrt(1.0f - d * d);
        aim = !(s > 1e-8f) ? -normal : perp * (0.866f / s) + normal * -0.5f;
        PoseSetXYZ(m_aimDir, aim);
    } else if (d > 0.3226f) {
        const S_vector perp = aim - normal * d;
        const float s = std::sqrt(1.0f - d * d);
        aim = !(s > 1e-8f) ? normal : perp * (0.9465f / s) + normal * 0.3226f;
        PoseSetXYZ(m_aimDir, aim);
    }
    const S_matrix &model = m_model->GetMatrix();
    d = PoseDotZYX(aim, model(2));
    if (d < 0.866f) {
        const S_vector perp = aim - model(2) * d;
        const float s = std::sqrt(1.0f - d * d);
        if (!(s > 1e-8f))
            m_aimDir = PoseRow4(model, 2);
        else
            PoseSetXYZ(m_aimDir, perp * (0.5f / s) + model(2) * 0.866f);
    }
    if (m_weaponKind == WEAPON_KIND_PISTOL) {
        m_weaponDir = m_aimDir;
        const float b = m_blend;
        TurnBone(POSE_BONE_RIGHT_COLLAR, m_rightHand->GetMatrix()(0), m_aimDir, S_vector(b * 0.25f, b * 0.5f, b * 0.0f));
        AimWeapon(m_weaponDir, true, true, 1.0f);
        if (IsSupportHandOn()) {
            UpdateLeftHandTarget();
            ReachArm(POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_ELBOW, m_leftGrip->GetWorldPos(), m_leftHandTarget,
                     m_leftHand, m_support, nullptr);
        }
        return;
    }
    S_vector dir(0.0f, 0.0f, 0.0f);
    if (IsSupportHandOn()) {
        UpdateRightHandTarget();
        UpdateLeftHandTarget();
        dir = HandsDir();
        const uint32_t side = m_hand & 0xFF;
        if (side == 1)
            dir = PoseSlerpDir(m_hw1->GetMatrix()(0), dir, m_support);
        else if (side == 0)
            dir = PoseSlerpDir(-m_hw2->GetMatrix()(0), dir, m_support);
    } else {
        dir = HandAxis(dir);
    }
    PoseSetXYZ(m_weaponDir, PoseSlerpDir(dir, PoseXYZ(m_aimDir), m_blend));
    AimWeapon(m_weaponDir, true, true, m_support);
    if (!m_visible)
        return;
    UpdateRightHandTarget();
    UpdateLeftHandTarget();
    if (IsSupportHandOn()) {
        // The left hand between the grips, nearer the left one.
        const S_vector grip = m_rightGrip->GetWorldPos() * 0.25f + m_leftGrip->GetWorldPos() * 0.75f;
        ReachArm(POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_ELBOW, grip, m_leftHandTarget, m_leftHand, m_support,
                 m_stock ? &DOWN : nullptr);
    }
    if (m_stock)
        ReachArm(POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_ELBOW, m_rightGrip->GetWorldPos(), m_rightHandTarget,
                 m_rightHand, 1.0f, nullptr);
}
