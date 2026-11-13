// The weapon in the hands (original source 0x007a3b70, 0x007ae780..0x007b0d70 and 0x007b8a20..
// 0x007c11f0): the weapon's model linked to a hand dummy ("hw1" right, "hw2" left) with its
// grips "LW" / "RW" and stock "BUTT", raising and lowering it (m_blend), the support hand onto
// the left grip (m_support), the aim turning to the view direction, the arms reaching the grips
// (a two-bone solve with the elbow out or in), the recoil and the activity animations of the arms.
#include "game/human_pose_internal.h"

#include <cmath>
#include <cstring>

#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

const char *const DUMMY_LEFT_GRIP = "LW";
const char *const DUMMY_RIGHT_GRIP = "RW";
const char *const DUMMY_STOCK = "BUTT";
const char *const DUMMY_RIGHT_HAND = "hw1";
const char *const DUMMY_LEFT_HAND = "hw2";

// I3D_frame::LinkTo() flags of the weapon's unlinking while the arms are posed.
constexpr uint32_t WEAPON_LINK_FLAGS = 5;

const S_vector DOWN(0.0f, -1.0f, 0.0f);

S_quat QuatMultiply(const S_quat &a, const S_quat &b)
{
    S_quat q;
    q.v = a.v.Cross(b.v) + (b.v * a.s + a.v * b.s);
    q.s = a.s * b.s - a.v.Dot(b.v);
    return q;
}

// The turn of a bone's rotation `current` that takes `from` to `to` about the axis of their
// cross product with its components weighted (the sine of the angle is the cross product's
// length); false when there is (nearly) nothing to turn.
bool WeightedTurn(const S_vector &from, const S_vector &to, const S_vector &weights, const S_quat &current, S_quat &out)
{
    const S_vector cross = to.Cross(from);
    S_vector axis(cross.x * weights.x, cross.y * weights.y, cross.z * weights.z);
    // (The SDK's tests as the original has them: below 1e-8 as an integer compare, a NaN takes the
    // square root and nothing turns then.)
    const float sq = axis.Square();
    if (PoseBelowEpsilon(sq))
        return false;
    float length = 1.0f;
    if (!(std::fabs(sq - 1.0f) < 1e-8f)) {
        length = std::sqrt(sq);
        if (!(length > 1e-8f))
            return false;
    }
    axis *= 1.0f / length;
    const double angle = std::asin(static_cast<double>(length));
    const float s = static_cast<float>(std::sin(angle * 0.5));
    S_quat turn;
    turn.v = S_vector(s * axis.x, axis.y * s, axis.z * s);
    turn.s = static_cast<float>(std::cos(static_cast<float>(angle * 0.5)));
    out = QuatMultiply(current, turn);
    return true;
}

}  // namespace

// ------------------------------------------------------------------ arms

// 0x007a3b70: the elbow (or knee) of a limb from `root` (upper length `upper`) to `target`
// (lower length `lower`): `joint` comes in as where it is and goes out on the circle it can be
// on, on the side it was, turned towards `hint` (when given) by m_elbowOut (on the hint's side)
// or m_elbowIn. False when the target is out of reach (`joint` then as it was).
bool C_HumanWeaponPose::SolveTwoBone(const S_vector &root, float upper, const S_vector &target, float lower,
                                     S_vector4 &joint, const S_vector *hint)
{
    const float lowerSq = lower * lower;
    S_vector d(target.x - root.x, target.y - root.y, target.z - root.z);
    const float length = PoseMagnitude(d.Square());
    const float inv = 1.0f / length;
    float along = (upper * upper - lowerSq) + length * length;
    const float heightSq = upper * upper - along * along * inv * inv * 0.25f;
    if (PoseIsNegative(heightSq))
        return false;
    along = along * inv * 0.5f;
    const S_vector axis(d.x * inv, d.y * inv, d.z * inv);
    const S_vector foot = S_vector(axis.x * along, axis.y * along, axis.z * along) + root;
    PoseSetXYZ(joint, PoseXYZ(joint) - foot);
    S_vector side = PoseXYZ(joint) - axis * axis.Dot(PoseXYZ(joint));
    side.Normalize();
    S_vector offset;
    if (!hint) {
        offset = side * std::sqrt(heightSq);
    } else {
        S_vector h = *hint - axis * axis.Dot(*hint);
        h.Normalize();
        const float w = h.Dot(side) <= 0.0f ? m_elbowIn : m_elbowOut;
        S_vector mixed = side * (1.0f - w) + h * w;
        mixed.Normalize();
        offset = mixed * std::sqrt(heightSq);
    }
    PoseSetXYZ(joint, foot + offset);
    return true;
}

// 0x007bfde0: turns bone `index` so that `from` (world) goes towards `to`, about the model's
// axes weighted by `weights`.
void C_HumanWeaponPose::TurnBone(int index, const S_vector &from, const S_vector4 &to, const S_vector &weights)
{
    const S_matrix &model = m_model->GetMatrix();
    const S_vector f(model(0).Dot(from), model(1).Dot(from), from.Dot(model(2)));
    const S_vector t(PoseXYZ(to).Dot(model(0)), model(1).Dot(PoseXYZ(to)), model(2).Dot(PoseXYZ(to)));
    const S_matrix modelInverse = PoseInverseRotation(m_model->GetMatrix());
    S_PoseBone &bone = m_bones[index];
    S_matrix m = bone.frame->GetMatrix() * modelInverse;
    S_quat current;
    current.Make(m);
    S_quat turned;
    if (!WeightedTurn(f, t, weights, current, turned))
        return;
    m.SetRot3(turned);
    m *= m_model->GetMatrix();
    I3D_frame *parent = bone.parent ? bone.parent->frame : m_model;
    m *= PoseInverseRotation(parent->GetMatrix());
    PoseSetFrameMatrix(bone.frame, PoseWithLocalPos(m, bone.frame));
}

// 0x007c02e0: the same about the axes `axes` (row-major, world).
void C_HumanWeaponPose::TurnBoneAbout(int index, const S_vector &from, const S_vector4 &to, const S_matrix &axes,
                                      const S_vector &weights)
{
    const S_vector f(axes.m[0][1] * from.y + from.z * axes.m[0][2] + from.x * axes.m[0][0],
                     from.x * axes.m[1][0] + axes.m[1][1] * from.y + axes.m[1][2] * from.z,
                     from.x * axes.m[2][0] + axes.m[2][1] * from.y + axes.m[2][2] * from.z);
    const S_vector t(to.y * axes.m[0][1] + to.x * axes.m[0][0] + to.z * axes.m[0][2],
                     to.x * axes.m[1][0] + axes.m[1][1] * to.y + axes.m[1][2] * to.z,
                     axes.m[2][0] * to.x + axes.m[2][1] * to.y + axes.m[2][2] * to.z);
    const S_matrix axesInverse = PoseInverseRotation(axes);
    S_PoseBone &bone = m_bones[index];
    S_matrix m = bone.frame->GetMatrix() * axesInverse;
    S_quat current;
    current.Make(m);
    S_quat turned;
    if (!WeightedTurn(f, t, weights, current, turned))
        return;
    m.SetRot3(turned);
    m *= axes;
    I3D_frame *parent = bone.parent ? bone.parent->frame : m_model;
    m *= PoseInverseRotation(parent->GetMatrix());
    PoseSetFrameMatrix(bone.frame, PoseWithLocalPos(m, bone.frame));
}

// 0x007c0750: the weapon pointing along `dir` (up kept up): with `hands` the hands turn to the
// grips' axes (the left one only with the support hand; below `weight` 1 blended from where
// they are); with `place` the weapon goes to the weapon hand's dummy with its grip there (its
// stock blending to the right shoulder by m_blend while the weapon is held up).
void C_HumanWeaponPose::AimWeapon(const S_vector4 &dir, bool hands, bool place, float weight)
{
    S_matrix m = IdentityMatrix();
    m.SetDir(PoseXYZ(dir), S_vector(0.0f, 1.0f, 0.0f));
    if (hands && m_leftGrip && m_rightGrip) {
        I3D_frame *const grips[2] = {m_leftGrip, m_rightGrip};
        const int bones[2] = {POSE_BONE_LEFT_HAND, POSE_BONE_RIGHT_HAND};
        for (int i = 0; i < 2; ++i) {
            if (i == 0 && !IsSupportHandOn())
                continue;
            S_matrix h = grips[i]->GetLocalMatrix() * m;
            h.m[3][2] = 0.0f;
            h.m[3][1] = 0.0f;
            h.m[3][0] = 0.0f;
            I3D_frame *hand = m_bones[bones[i]].frame;
            const S_matrix parentInverse = PoseInverseRotation(hand->GetParent()->GetMatrix());
            if (weight < 1.0f) {
                S_quat to;
                to.Make(h);
                S_quat from;
                from.Make(hand->GetMatrix());
                h.SetRot3(from.Slerp(to, weight, false));
            }
            h *= parentInverse;
            PoseSetFrameMatrix(hand, PoseWithLocalPos(h, hand));
        }
    }
    if (!place)
        return;
    I3D_frame *grip;
    I3D_frame *handDummy;
    if ((m_hand & 0xF) == 1) {
        grip = m_rightGrip;
        handDummy = m_hw1;
    } else {
        grip = m_leftGrip;
        handDummy = m_hw2;
        if (!grip)
            grip = m_rightGrip;
    }
    const S_vector gripOffset = grip->GetPos().RotateByMatrix(m);
    S_vector pos = handDummy->GetWorldPos() - gripOffset;
    if (m_stock && (m_flags & POSE_FLAG_HOLD)) {
        // The stock to the right shoulder by the raise.
        // (Behaviour 3 standing: a stock further back.)
        const S_vector shoulder = m_behaviour == 3 && m_stance == HUMAN_STANCE_STAND
                                      ? S_vector(0.03f, -0.051f, 0.11f)
                                      : S_vector(0.033f, 0.036f, 0.08f);
        const S_vector stockOffset = m_stock->GetPos().RotateByMatrix(m);
        const S_vector at = shoulder * m_bones[POSE_BONE_RIGHT_COLLAR].frame->GetMatrix();
        pos = (at - stockOffset) * m_blend + pos * (1.0f - m_blend);
    }
    m.m[3][0] = pos.x;
    m.m[3][1] = pos.y;
    m.m[3][2] = pos.z;
    // (The original copies row 3 from a stack temp whose w it never sets, 0x007c0d94: the port's 1.)
    m.m[3][3] = 1.0f;
    m_weapon->SetLocalMatrix(m);
    m_weapon->Update();
}

// 0x007c0e20: the arm from `shoulder` through `elbow` moves the hand by `weight` of the way that
// takes its point `handTarget` to `target` (the elbow on its side, towards `hint` when given);
// the hand keeps its world rotation.
void C_HumanWeaponPose::ReachArm(int shoulder, int elbow, const S_vector &target, const S_vector4 &handTarget,
                                 I3D_frame *hand, float weight, const S_vector *hint)
{
    S_matrix keep = hand->GetMatrix();
    keep.m[3][2] = 0.0f;
    keep.m[3][1] = 0.0f;
    keep.m[3][0] = 0.0f;
    const S_vector root = m_bones[shoulder].frame->GetWorldPos();
    const S_vector &handPos = hand->GetWorldPos();
    const S_vector goal((weight * target.x + (1.0f - weight) * handTarget.x + handPos.x) - handTarget.x,
                        (weight * target.y + (1.0f - weight) * handTarget.y + handPos.y) - handTarget.y,
                        (weight * target.z + (1.0f - weight) * handTarget.z + handPos.z) - handTarget.z);
    S_vector4 joint = PoseWorldPos4(m_bones[elbow].frame);
    S_vector dir;
    if (SolveTwoBone(root, m_bones[shoulder].childLength[0], goal, m_bones[elbow].childLength[0], joint, hint)) {
        AimBone(shoulder, PoseXYZ(joint) - root, -1.0f);
        dir = goal - PoseXYZ(joint);
    } else {
        dir = goal - root;
        AimBone(shoulder, dir, -1.0f);
    }
    AimBone(elbow, dir, -1.0f);
    keep *= PoseInverseRotation(m_bones[elbow].frame->GetMatrix());
    PoseSetFrameMatrix(hand, PoseWithLocalPos(keep, hand));
}

// ------------------------------------------------------------------ the weapon

// 0x007b8a20: the hands' state for a model (`reload`: after a saved game only the frames).
void C_HumanWeaponPose::InitHands(bool reload)
{
    if (!reload) {
        m_raised = false;
        m_weaponKind = WEAPON_KIND_NONE;
        m_stance = HUMAN_STANCE_STAND;
        m_proneFit = false;
        m_proneSettle = 0;
        m_hand = 1;
        m_blendTime = 0;
        m_blendLength = 300;
        m_pauseTime = 0;
        m_supportTime = 0;
        m_blend = 0.0f;
        m_support = 0.0f;
        const S_matrix &model = m_model->GetMatrix();
        const S_vector forward = model(2).Normalized();
        m_aimFrom = PoseVector4(forward, model.m[2][3]);
        m_aimTarget = PoseVector4(forward, m_model->GetMatrix().m[2][3]);
        m_aimDir = PoseVector4(forward, m_model->GetMatrix().m[2][3]);
        PoseSetXYZ(m_weaponDir, S_vector(0.0f, 0.0f, 0.0f));
        m_hasGunnerPose = false;
        PoseSetXYZ(m_velocity, S_vector(0.0f, 0.0f, 0.0f));
        PoseSetXYZ(m_lastVelocity, S_vector(0.0f, 0.0f, 0.0f));
        PoseSetXYZ(m_sampledVelocity, S_vector(0.0f, 0.0f, 0.0f));
        m_velocityTime = 100;
        m_lastPos = PoseWorldPos4(m_model);
    }
    if (m_weapon)
        m_weapon->Release();
    m_weapon = nullptr;
    m_rightGrip = nullptr;
    m_leftGrip = nullptr;
    m_stock = nullptr;
    m_hw1 = m_model->FindChildFrame(DUMMY_RIGHT_HAND, FRAME_MASK_ANY);
    m_hw2 = m_model->FindChildFrame(DUMMY_LEFT_HAND, FRAME_MASK_ANY);
    if (m_camera3rd)
        PoseSetXYZ(m_cameraOffset, S_vector(0.0f, 0.25f, 0.0f));
    m_leftHand = m_bones[POSE_BONE_LEFT_HAND].frame;
    m_rightHand = m_bones[POSE_BONE_RIGHT_HAND].frame;
    // (The original leaves their w undefined.)
    m_leftHandOffset = {-0.075f, -0.041f, -0.003f, 0.0f};
    m_rightHandOffset = {0.075f, -0.041f, -0.003f, 0.0f};
}

// 0x007b9510
void C_HumanWeaponPose::SetModel(I3D_frame *model, int behaviour)
{
    if (m_weapon == model)
        return;
    if (m_weapon)
        m_weapon->Release();
    if (!model) {
        m_weapon = nullptr;
        m_flags &= 0x03FFFFFCu;
        m_hand &= 0x7FFFFFFFu;
        m_raised = false;
        return;
    }
    m_weapon = model;
    model->AddRef();
    m_leftGrip = model->FindChildFrame(DUMMY_LEFT_GRIP, FRAME_MASK_ANY);
    m_rightGrip = model->FindChildFrame(DUMMY_RIGHT_GRIP, FRAME_MASK_ANY);
    m_stock = model->FindChildFrame(DUMMY_STOCK, FRAME_MASK_ANY);
    if (m_leftGrip && m_rightGrip)
        m_flags |= POSE_FLAG_GRIP;
    m_behaviour = behaviour;
    switch (behaviour) {
    case 2:
        m_weaponKind = WEAPON_KIND_PISTOL;
        break;
    case 3:
        m_weaponKind = 3;
        break;
    case 4:
    case 12:
        m_weaponKind = 5;
        break;
    case 5:
        m_weaponKind = 6;
        break;
    case 6:
        m_weaponKind = 7;
        break;
    default:
        m_weaponKind = WEAPON_KIND_NONE;
        break;
    }
    SetMode(1);
}

// 0x007b96c0: the weapon into the right hand (1), the left one (0) or the other hand (2),
// its grip at the hand's dummy.
void C_HumanWeaponPose::SetMode(int mode)
{
    if (!m_weapon)
        return;
    if (!m_rightGrip && !m_leftGrip) {
        m_flags = 0;
        m_raised = false;
        return;
    }
    if (mode == 2) {
        const uint32_t side = m_hand & 0xFF;
        if (side == 0)
            mode = 1;
        else if (side == 1)
            mode = 0;
        else
            mode = -1;
    }
    I3D_frame *dummy = nullptr;
    S_vector offset(0.0f, 0.0f, 0.0f);
    if (mode == 1) {
        m_hand = (m_hand & 0xFFFFFFF1u) | 1;
        m_weapon->LinkTo(m_hw1, 0);
        if (!IsSupportHandOn()) {
            m_weapon->SetWorldDir(S_vector(1.0f, 0.0f, 0.0f), 0.0f);
            m_weapon->Update();
        }
        if (m_rightGrip)
            offset = m_weapon->GetWorldPos() - m_rightGrip->GetWorldPos();
        dummy = m_hw1;
    } else if (mode == 0) {
        m_hand &= 0x7FFFFFF0u;
        m_weapon->LinkTo(m_hw2, 0);
        m_weapon->SetWorldDir(S_vector(-1.0f, 0.0f, 0.0f), 0.0f);
        m_weapon->Update();
        if (I3D_frame *grip = m_rightGrip ? m_rightGrip : m_leftGrip)
            offset = m_weapon->GetWorldPos() - grip->GetWorldPos();
        dummy = m_hw2;
    }
    if (dummy) {
        const S_matrix &axes = dummy->GetMatrix();
        m_weapon->SetPosInline(S_vector(offset.Dot(axes(0)), offset.Dot(axes(1)), offset.Dot(axes(2))));
    }
    m_flags |= POSE_FLAG_GRIP;
}

// 0x007b9b20: the support hand onto the left grip / off it over 200 ms (needs the grip).
void C_HumanWeaponPose::SetSupportHand(bool on)
{
    if (!m_leftGrip)
        return;
    if (on) {
        if (IsSupportHandOn())
            return;
        m_hand |= 0x80000000u;
        m_supportTime = static_cast<int>(m_support * 200.0f);
        m_flags |= POSE_FLAG_SUPPORT_ON;
    } else {
        if (!IsSupportHandOn())
            return;
        m_supportTime = static_cast<int>(m_support * 200.0f);
        m_flags |= POSE_FLAG_SUPPORT_OFF;
    }
    m_flags |= POSE_FLAG_GRIP;
}

// 0x007b9160: raises the weapon over `time` ms (from where the blend is).
void C_HumanWeaponPose::RaiseWeapon(int time)
{
    const uint32_t flags = m_flags;
    if (m_raised || (flags & (POSE_FLAG_STEER | POSE_FLAG_STATIC_WEAPON | POSE_FLAG_SEAT_WEAPON)) || !m_weapon)
        return;
    m_blendLength = time;
    m_blendTime = static_cast<int>(time * m_blend);
    m_raised = true;
    m_flags = (flags & 0x0FFFFFC2u) | POSE_FLAG_RAISE | POSE_FLAG_HOLD;
}

// 0x007b91c0: lowers it over `time` ms.
void C_HumanWeaponPose::LowerWeapon(int time)
{
    if (!m_raised || !m_weapon || !(m_flags & POSE_FLAG_HOLD))
        return;
    m_blendLength = time;
    m_blendTime = static_cast<int>(time * m_blend);
    m_raised = false;
    m_flags = (m_flags & 0x3FFFFFFFu) | POSE_FLAG_LOWER;
}

// 0x007b9270: lowers a held weapon for `time` ms (then it goes up again; `free`: the arms go
// free meanwhile).
void C_HumanWeaponPose::PauseWeapon(int time, bool free)
{
    if (!m_weapon || !(m_flags & POSE_FLAG_HOLD))
        return;
    m_flags |= POSE_FLAG_PAUSED;
    m_pauseTime = time;
    if (free) {
        LowerWeapon(300);
        m_blend = 0.0f;
        m_flags = (m_flags & 0x7FFFFFFAu) | POSE_FLAG_FREE;
        return;
    }
    LowerWeapon(300);
}

// 0x007b9220: the hands free for `time` ms (a held weapon lowered; a passenger's arms free
// without going back).
void C_HumanWeaponPose::PauseHands(int time)
{
    if (m_weapon && (m_flags & POSE_FLAG_HOLD)) {
        PauseWeapon(time, true);
        return;
    }
    if (m_flags & POSE_FLAG_PASSENGER) {
        m_pauseTime = time;
        m_flags |= POSE_FLAG_PAUSED | POSE_FLAG_NO_RAISE | POSE_FLAG_FREE;
        m_blend = 0.0f;
    }
}

// 0x007b92e0 (C_HumanWork::SetViewDir): the aim turns to `dir` (the local player's at once,
// the others' over 300 ms per half turn).
void C_HumanWeaponPose::SetViewDir(const S_vector &view)
{
    if (!(m_flags & POSE_FLAG_HOLD))
        return;
    // Fix: w is 0 (the original copies the 4 bytes after the caller's vector).
    const S_vector4 dir = {view.x, view.y, view.z, 0.0f};
    bool atOnce = m_owner && m_owner->IsLocalPlayer();
    if (!atOnce) {
        // (Summed z first, as the original: its rounding decides the 1e-8 test below.)
        float c = m_aimDir.z * dir.z + m_aimDir.y * dir.y + m_aimDir.x * dir.x;
        if (c > 1.0f)
            c = 1.0f;
        else if (c < -1.0f)
            c = -1.0f;
        m_aimAngle = c;
        atOnce = 1.0f - std::fabs(c) < 1e-8f;
    }
    if (atOnce) {
        m_aimDir = dir;
        m_aimTarget = dir;
        m_aimFrom = dir;
        m_aimTime = 0;
        return;
    }
    m_aimAngle = static_cast<float>(std::acos(m_aimAngle));
    m_aimSin = static_cast<float>(std::sin(m_aimAngle));
    m_aimTime = m_aimLength = static_cast<int>(m_aimAngle * 95.49296569824219f);
    m_aimFrom = m_aimDir;
    m_aimTarget = dir;
}

// The aim's turn to the view direction (the slerp of m_aimFrom to m_aimTarget).
void C_HumanWeaponPose::TurnAim(float f)
{
    const double inv = 1.0 / m_aimSin;
    const double a = std::sin((1.0 - f) * m_aimAngle) * inv;
    const double b = std::sin(static_cast<double>(f) * m_aimAngle) * inv;
    const S_vector to = PoseXYZ(m_aimTarget);
    const S_vector from = PoseXYZ(m_aimFrom);
    const S_vector aim(static_cast<float>(a * to.x), static_cast<float>(a * to.y), static_cast<float>(a * to.z));
    const S_vector back(static_cast<float>(b * from.x), static_cast<float>(b * from.y), static_cast<float>(b * from.z));
    PoseSetXYZ(m_aimDir, aim + back);
}

// 0x007b9bb0: both hands on the grips of a weapon held down (the support hand's weapon
// direction blended from hw1's x axis to the line between the hands; clients only).
void C_HumanWeaponPose::UpdateGrip()
{
    if (!IsClient() || m_weaponKind == WEAPON_KIND_PISTOL || !IsSupportHandOn())
        return;
    I3D_frame *parent = m_weapon->GetParent();
    m_weapon->LinkTo(nullptr, WEAPON_LINK_FLAGS);
    const S_vector x = m_hw1->GetMatrix()(0);
    UpdateRightHandTarget();
    UpdateLeftHandTarget();
    PoseSetXYZ(m_weaponDir, PoseSlerpDir(x, HandsDir(), m_support));
    AimWeapon(m_weaponDir, true, false, m_support);
    UpdateRightHandTarget();
    UpdateLeftHandTarget();
    PoseSetXYZ(m_weaponDir, PoseSlerpDir(x, HandsDir(), m_support));
    AimWeapon(m_weaponDir, false, true, 1.0f);
    if (m_visible) {
        UpdateLeftHandTarget();
        ReachArm(POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_ELBOW, m_leftGrip->GetWorldPos(), m_leftHandTarget,
                 m_leftHand, m_support, nullptr);
    }
    m_weapon->LinkTo(parent, WEAPON_LINK_FLAGS);
}

// 0x007ba110: the weapon held up: the aim turns to the view direction, the stance's hold poses
// the body (clients only) and the local player's third person camera follows the aim up and
// down at the side of the head.
void C_HumanWeaponPose::UpdateHold()
{
    if (m_aimTime != 0) {
        m_aimTime -= m_time;
        if (m_aimTime < 0) {
            m_aimTime = 0;
            m_aimDir = m_aimTarget;
        } else {
            TurnAim(static_cast<float>(m_aimTime) / static_cast<float>(m_aimLength));
        }
    }
    if (!IsClient())
        return;
    I3D_frame *parent = m_weapon->GetParent();
    m_weapon->LinkTo(nullptr, WEAPON_LINK_FLAGS);
    switch (m_stance) {
    case HUMAN_STANCE_STAND:
        HoldStanding();
        break;
    case HUMAN_STANCE_CROUCH:
        HoldCrouching();
        break;
    case HUMAN_STANCE_PRONE:
        HoldProne();
        break;
    case POSE_STANCE_SEATED:
        HoldSeated();
        break;
    }
    if (m_owner && m_owner->IsLocalPlayer()) {
        const S_matrix &model = m_model->GetMatrix();
        const S_vector at = PoseXYZ(m_cameraOffset) * m_bones[POSE_BONE_HEAD].frame->GetMatrix();
        S_vector rel = at - model(3);
        // The camera keeps its side of the model (its x against the model).
        const S_vector &camera = m_camera3rd->GetWorldPos();
        const float side = rel.Dot(model(0)) - (camera - m_model->GetWorldPos()).Dot(model(0));
        rel -= model(0) * side;
        rel += m_model->GetWorldPos();
        const float t = std::fabs(m_aimDir.y) * m_blend;
        m_camera3rd->SetPos(rel * t + m_camera3rd->GetWorldPos() * (1.0f - t));
    }
    m_weapon->LinkTo(parent, WEAPON_LINK_FLAGS);
}

// 0x007ba520: the arms free (POSE_FLAG_FREE): the aim keeps turning (also past its end) and
// the back turns to it, at most 0.7 up / down and 0.34 forward (clients, not lying).
void C_HumanWeaponPose::UpdateFree()
{
    m_aimTime -= m_time;
    if (m_aimTime >= 0)
        TurnAim(static_cast<float>(m_aimTime) / static_cast<float>(m_aimLength));
    if (!IsClient() || m_stance == HUMAN_STANCE_PRONE)
        return;
    const S_matrix &model = m_model->GetMatrix();
    S_vector aim = PoseXYZ(m_aimDir);
    float d = PoseDotZYX(aim, model(1));
    if (std::fabs(d) > 0.7f) {
        d -= (PoseIsNegative(d) ? -1.0f : 1.0f) * 0.7f;
        aim -= model(1) * d;
        aim.Normalize();
        PoseSetXYZ(m_aimDir, aim);
    }
    d = PoseDotZYX(aim, model(2));
    if (d < 0.34f) {
        d -= 0.34f;
        aim -= model(2) * d;
        aim.Normalize();
        PoseSetXYZ(m_aimDir, aim);
    }
    TurnBone(POSE_BONE_LOWER_BACK, m_bones[POSE_BONE_UPPER_CHEST].frame->GetMatrix()(2), m_aimDir,
             S_vector(0.85f, 0.85f, 0.0f));
    TurnBone(POSE_BONE_UPPER_CHEST, m_bones[POSE_BONE_UPPER_CHEST].frame->GetMatrix()(2), m_aimDir,
             S_vector(0.85f, 0.85f, 0.0f));
}

// ------------------------------------------------------------------ recoil and activities

// 0x007ae7f0 (C_HumanAnimPlayer::UpdateCarryTrack; clients only).
void C_HumanWeaponPose::StartRecoil(I3D_animation_set * /*unused*/)
{
    if (!IsClient())
        return;
    m_recoilTime = 0;
    m_flags |= POSE_FLAG_RECOIL;
}

// 0x007b01d0: the kick of a shot, fading over 300 ms: a pistol's arm and chest turn up from a
// direction 10 degrees below the weapon's; a rifle's spine and chest turn back from one 20
// degrees to the side and the hands stay on the grips.
void C_HumanWeaponPose::UpdateRecoil()
{
    if (m_weapon) {
        m_recoilTime += m_time;
        const float t = 1.0f - static_cast<float>(m_recoilTime) * 0.0033333334f;
        if (!PoseIsNegative(t)) {
            const float w = t * 0.5f;
            const S_vector weights(w, w, w);
            if (m_weaponKind == WEAPON_KIND_PISTOL) {
                S_vector from(m_weaponDir.x, m_weaponDir.y - 0.1736f, m_weaponDir.z);
                from.Normalize();
                TurnBone(POSE_BONE_RIGHT_ELBOW, from, m_weaponDir, weights);
                TurnBone(POSE_BONE_RIGHT_SHOULDER, from, m_weaponDir, weights);
                TurnBone(POSE_BONE_CHEST, from, m_weaponDir, weights);
                return;
            }
            I3D_frame *parent = m_weapon->GetParent();
            m_weapon->LinkTo(nullptr, WEAPON_LINK_FLAGS);
            S_vector from(m_weaponDir.x * 0.9396926f - m_weaponDir.z * 0.34202015f, m_weaponDir.y,
                          m_weaponDir.x * 0.34202015f + m_weaponDir.z * 0.9396926f);
            from.Normalize();
            TurnBone(POSE_BONE_SPINE, from, m_weaponDir, weights);
            TurnBone(POSE_BONE_CHEST, from, m_weaponDir, weights);
            AimWeapon(m_weaponDir, true, true, m_support);
            UpdateRightHandTarget();
            UpdateLeftHandTarget();
            if (IsSupportHandOn())
                ReachArm(POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_ELBOW, m_leftGrip->GetWorldPos(), m_leftHandTarget,
                         m_leftHand, m_support, m_stock ? &DOWN : nullptr);
            if (m_stock)
                ReachArm(POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_ELBOW, m_rightGrip->GetWorldPos(),
                         m_rightHandTarget, m_rightHand, 1.0f, nullptr);
            m_weapon->LinkTo(parent, WEAPON_LINK_FLAGS);
            return;
        }
    }
    m_flags &= ~POSE_FLAG_RECOIL;
}

// 0x007ae780 (C_HumanAnimPlayer::UpdateActivityTrack): the arms play `anim` (null: stop;
// clients only).
void C_HumanWeaponPose::PlayActivity(I3D_animation_set *anim)
{
    if (!IsClient())
        return;
    m_activityAnim = anim;
    if (!anim) {
        m_flags &= ~POSE_FLAG_ACTIVITY;
        return;
    }
    std::memset(m_activityPosKey, 0, sizeof(m_activityPosKey));
    std::memset(m_activityRotKey, 0, sizeof(m_activityRotKey));
    m_activityTime = 0;
    m_flags |= POSE_FLAG_ACTIVITY;
}

// 0x007b0170: how much of an activity animation a bone takes (the head all of it, the chest
// and back half, the spine a fifth, the rest all).
float PoseActivityWeight(const S_PoseBone *bone)
{
    switch (bone->index) {
    case POSE_BONE_HEAD_TOP:
    case POSE_BONE_HEAD:
        return 1.0f;
    case POSE_BONE_UPPER_CHEST:
    case POSE_BONE_CHEST:
    case POSE_BONE_LOWER_BACK:
    case POSE_BONE_BACK:
        return 0.5f;
    case POSE_BONE_SPINE:
        return 0.2f;
    default:
        return 1.0f;
    }
}

// 0x007b0080: the activity animation on the bones, blending in over its first 300 ms and out
// over its last (the time advancing at the movement's rate, 1.4).
void C_HumanWeaponPose::UpdateActivity()
{
    I3D_animation_set *anim = m_activityAnim;
    if (anim) {
        const int time = m_activityTime;
        float weight = static_cast<float>(time) * 0.0033333334f;
        if (weight > 1.0f) {
            weight = static_cast<float>(anim->GetEndTime() - time) * 0.0033333334f;
            if (weight > 1.0f)
                weight = 1.0f;
        }
        ApplyAnimation(anim, m_activityPosKey, m_activityRotKey, time, &m_bones[POSE_BONE_PELVIS], weight,
                       PoseActivityWeight);
        m_model->Update();
        // (The x87 rounds the product and the sum to float, 0x007b0122.)
        const float advanced = static_cast<float>(m_time) * 1.4f + static_cast<float>(m_activityTime);
        m_activityTime = static_cast<int>(advanced);
        if (m_activityTime <= m_activityAnim->GetEndTime())
            return;
    }
    m_flags &= ~POSE_FLAG_ACTIVITY;
}

// 0x007b0710: the keys of `anim` at `time` on `bone` and the bones below it (each bone's
// track by its joint name): positions lerped, rotations slerped between the keys around the
// time (`posKeys` / `rotKeys` remember where each bone's search got to), blended into the
// frames by `weight` (times `boneWeight` of the bone).
void C_HumanWeaponPose::ApplyAnimation(I3D_animation_set *anim, int *posKeys, int *rotKeys, int time, S_PoseBone *bone,
                                       float weight, float (*boneWeight)(const S_PoseBone *))
{
    while (bone) {
        const int index = bone->index;
        const int track = anim->FindTrack(g_poseBoneNames[index]);
        if (track >= 0) {
            S_vector4 prevPos = {0.0f, 0.0f, 0.0f, 0.0f};
            S_vector4 nextPos = {0.0f, 0.0f, 0.0f, 0.0f};
            int prevPosTime = 0;
            int nextPosTime = 0;
            S_quat prevRot(1.0f, 0.0f, 0.0f, 0.0f);
            S_quat nextRot(1.0f, 0.0f, 0.0f, 0.0f);
            S_quat rot(1.0f, 0.0f, 0.0f, 0.0f);
            int prevRotTime = 0;
            int nextRotTime = 0;
            // The position keys around the time.
            for (;;) {
                prevPos.x = PoseUnset();
                nextPos.x = PoseUnset();
                const int key = posKeys[index];
                if (key < anim->NumPosKeys(track))
                    anim->GetPosKey(track, key, &prevPosTime, reinterpret_cast<S_vector *>(&prevPos));
                const int next = posKeys[index] + 1;
                if (next < anim->NumPosKeys(track))
                    anim->GetPosKey(track, next, &nextPosTime, reinterpret_cast<S_vector *>(&nextPos));
                if (PoseIsUnset(nextPos.x)) {
                    nextPos = prevPos;
                    nextPosTime = prevPosTime;
                    break;
                }
                if (nextPosTime >= time)
                    break;
                ++posKeys[index];
            }
            // The rotation keys.
            for (;;) {
                prevRot.s = PoseUnset();
                nextRot.s = PoseUnset();
                const int key = rotKeys[index];
                if (key < anim->NumRotKeys(track))
                    anim->GetRotKey(track, key, &prevRotTime, &prevRot);
                const int next = rotKeys[index] + 1;
                if (next < anim->NumRotKeys(track))
                    anim->GetRotKey(track, next, &nextRotTime, &nextRot);
                if (PoseIsUnset(nextRot.s)) {
                    nextRot = prevRot;
                    nextRotTime = prevRotTime;
                    break;
                }
                if (nextRotTime >= time)
                    break;
                ++rotKeys[index];
            }
            float w = weight;
            if (boneWeight)
                w = boneWeight(bone) * weight;
            I3D_frame *frame = m_bones[index].frame;
            if (!PoseIsUnset(prevPos.x) && !PoseIsUnset(nextPos.x)) {
                S_vector p;
                if (nextPosTime - prevPosTime < 1) {
                    p = PoseXYZ(prevPos);
                } else {
                    const float f = static_cast<float>(time - prevPosTime) / static_cast<float>(nextPosTime - prevPosTime);
                    p = PoseXYZ(prevPos) * (1.0f - f) + PoseXYZ(nextPos) * f;
                }
                frame->SetPosInline(frame->GetPos() * (1.0f - w) + p * w);
            }
            if (!PoseIsUnset(prevRot.s) && !PoseIsUnset(nextRot.s)) {
                if (nextRotTime - prevRotTime < 1)
                    rot = prevRot;
                else
                    rot = prevRot.Slerp(nextRot, static_cast<float>(time - prevRotTime) /
                                                     static_cast<float>(nextRotTime - prevRotTime), false);
                const S_quat &current = frame->GetRot();
                frame->SetRot(rot.Slerp(current, 1.0f - w, false));
            }
        }
        if (bone->child[0])
            ApplyAnimation(anim, posKeys, rotKeys, time, bone->child[0], weight, boneWeight);
        if (bone->child[1])
            ApplyAnimation(anim, posKeys, rotKeys, time, bone->child[1], weight, boneWeight);
        bone = bone->child[2];
    }
}
