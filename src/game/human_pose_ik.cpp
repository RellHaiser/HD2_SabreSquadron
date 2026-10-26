// The skeleton of the weapon pose and its inverse kinematics (original source 0x007a25f0..
// 0x007a78d0): the 23 bones built from the model's joints, the joint limits, aiming a bone's
// first child along a direction, bending a middle joint (elbow / knee) towards a direction
// with the end kept, setting the hands to the weapon's grips, and the chains of the effectors
// ("pins": a bone whose tip should reach a point) solved by cyclic coordinate descent with
// quaternions, each joint turning about the axes its limit allows.
//
// The matrices are row-major with the translation in row 3 (Direct3D convention); the
// original multiplies full 4x4 matrices also where a transposed world matrix leaves the
// translation in the fourth column, and so does the port. Its vectors are normalised with the
// SDK's inline (a zero vector turns into an axis), which S_vector::Normalize() reproduces.
//
// (0x007a2d60, 0x007a33c0, 0x007a3760 (orientation helpers), 0x007a4a90 and 0x007a4c50 (the
// relative matrix of two neighbour bones) have no callers and are not ported.)
#include "game/human_pose_internal.h"

#include <cmath>
#include <cstddef>

#include "ls3d/i3d.h"

// ------------------------------------------------------------------ matrix helpers

// 0x007a25f0
S_matrix PoseTransposed(const S_matrix &m)
{
    S_matrix t;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            t.m[r][c] = m.m[c][r];
    return t;
}

S_matrix PoseInverseRotation(const S_matrix &m)
{
    S_matrix t = PoseTransposed(m);
    t.m[0][3] = 0.0f;
    t.m[1][3] = 0.0f;
    t.m[2][3] = 0.0f;
    return t;
}

// 0x007a2650: half a turn about y (the x and z axes reversed), without the translation.
S_matrix PoseTurnedAround(const S_matrix &m)
{
    S_matrix t;
    for (int c = 0; c < 3; ++c) {
        t.m[0][c] = -m.m[0][c];
        t.m[1][c] = m.m[1][c];
        t.m[2][c] = -m.m[2][c];
    }
    t.m[0][3] = 0.0f;
    t.m[1][3] = 0.0f;
    t.m[2][3] = 0.0f;
    t.m[3][0] = 0.0f;
    t.m[3][1] = 0.0f;
    t.m[3][2] = 0.0f;
    t.m[3][3] = 1.0f;
    return t;
}

// 0x007a2770: z = x * y, then x and z normalised and y = z * x (the translation and the
// fourth column are left).
void PoseOrthonormalize(S_matrix &m)
{
    m(2) = m(0).Cross(m(1));
    m(0) *= 1.0f / std::sqrt(m(0).Square());
    m(2) *= 1.0f / std::sqrt(m(2).Square());
    m(1) = m(2).Cross(m(0));
}

// 0x007a2850: turns the axes so that z points along `dir`, x kept as close as it can (y when
// x is along `dir`); the translation is cleared.
void PoseAimZ(S_matrix &m, const S_vector &dir)
{
    const S_vector z = dir.Normalized();
    S_vector x = m(0) - z * z.Dot(m(0));
    if (x.Square() < 1e-16f) {
        S_vector y = m(1) - z * z.Dot(m(1));
        y.Normalize();
        m(1) = y;
        m(0) = y.Cross(z);
    } else {
        x.Normalize();
        m(0) = x;
        m(1) = z.Cross(x);
    }
    m(2) = z;
    m.m[0][3] = 0.0f;
    m.m[1][3] = 0.0f;
    m.m[2][3] = 0.0f;
    m.m[3][0] = 0.0f;
    m.m[3][1] = 0.0f;
    m.m[3][2] = 0.0f;
    m.m[3][3] = 1.0f;
}

// 0x007a3270: axes with z along `dir` (as given, not normalised) and x horizontal.
S_matrix PoseFrameFromZ(const S_vector &dir)
{
    S_matrix m;
    S_vector x;
    if (dir.x == 0.0f && dir.z == 0.0f) {
        x = S_vector(1.0f, 0.0f, 0.0f);
    } else {
        const float inv = 1.0f / std::sqrt(dir.x * dir.x + dir.z * dir.z);
        x = S_vector(inv * dir.z, 0.0f, -(inv * dir.x));
    }
    m(0) = x;
    m.m[0][3] = 0.0f;
    m(1) = dir.Cross(x);
    m.m[1][3] = 0.0f;
    m(2) = dir;
    m.m[2][3] = 0.0f;
    m.m[3][0] = 0.0f;
    m.m[3][1] = 0.0f;
    m.m[3][2] = 0.0f;
    m.m[3][3] = 1.0f;
    return m;
}

// 0x007a3720: the same turned by `roll` about its z axis.
S_matrix PoseFrameFromZ(const S_vector &dir, float roll)
{
    const S_matrix m = PoseFrameFromZ(dir);
    return RotateZMatrix(roll) * m;
}

// 0x007a3960: axes with z along `dir` (not normalised), y = z * (1, 0, 0) normalised and
// x = y * z, turned by `roll` about z. (The original leaves the fourth column of rows 0 and 1
// undefined and copies dir.w into row 2's; the port clears them.)
S_matrix PoseFrameFromZX(const S_vector &dir, float roll)
{
    S_matrix m = IdentityMatrix();
    const S_vector y = dir.Cross(S_vector(1.0f, 0.0f, 0.0f)).Normalized();
    m(2) = dir;
    m(1) = y;
    m(0) = y.Cross(dir);
    return RotateZMatrix(roll) * m;
}

// 0x007a42c0: the rotation (3x3 part, row-vector convention) that turns the unit vector
// `from` into `to` (Moeller & Hughes: through a reflection when they are nearly parallel).
void PoseRotationBetween(const S_vector &from, const S_vector &to, S_matrix &out)
{
    const S_vector v = from.Cross(to);
    const float e = from.Dot(to);
    if (std::fabs(e) <= 0.999999f) {
        const float h = (1.0f - e) / v.Square();
        const float hvx = h * v.x;
        const float hvyz = h * v.z * v.y;
        out.m[0][0] = hvx * v.x + e;
        out.m[1][0] = hvx * v.y - v.z;
        out.m[2][0] = hvx * v.z + v.y;
        out.m[0][1] = hvx * v.y + v.z;
        out.m[1][1] = h * v.y * v.y + e;
        out.m[2][1] = hvyz - v.x;
        out.m[0][2] = hvx * v.z - v.y;
        out.m[1][2] = hvyz + v.x;
        out.m[2][2] = h * v.z * v.z + e;
        return;
    }
    // The axis along the smallest component of `from`.
    const float ax = from.x <= 0.0f ? -from.x : from.x;
    const float ay = from.y <= 0.0f ? -from.y : from.y;
    const float az = from.z <= 0.0f ? -from.z : from.z;
    S_vector p(0.0f, 0.0f, 1.0f);
    if (ay <= ax) {
        if (az > ay)
            p = S_vector(0.0f, 1.0f, 0.0f);
    } else if (ax < az) {
        p = S_vector(1.0f, 0.0f, 0.0f);
    }
    const S_vector u = p - from;
    const S_vector w = p - to;
    const float c1 = 2.0f / u.Square();
    const float c2 = 2.0f / w.Square();
    const float c3 = w.Dot(u) * c2 * c1;
    const float uu[3] = {u.x, u.y, u.z};
    const float ww[3] = {w.x, w.y, w.z};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            out.m[j][i] = c3 * ww[i] * uu[j] - (c1 * uu[i] * uu[j] + ww[j] * ww[i] * c2);
        out.m[i][i] += 1.0f;
    }
}

// 0x007a3330: turns `m` so that its z axis points along `to` (both unit vectors).
void PoseTurnZTo(S_matrix &m, const S_vector &to)
{
    S_matrix r = {};
    PoseRotationBetween(m(2), to, r);
    r.m[3][3] = 1.0f;
    m *= r;
}

// 0x007a3370: the same for the y axis.
void PoseTurnYTo(S_matrix &m, const S_vector &to)
{
    S_matrix r = {};
    PoseRotationBetween(m(1), to, r);
    r.m[3][3] = 1.0f;
    m *= r;
}

// 0x007a36c0: the roll of `m` about its z axis (against PoseFrameFromZ of that axis).
float PoseGetRoll(const S_matrix &m)
{
    const S_matrix p = m * PoseTransposed(PoseFrameFromZ(m(2)));
    return std::atan2(p.m[0][1], p.m[0][0]);
}

// 0x007a45a0: the spherical interpolation of two unit vectors (`b` itself when they are
// (nearly) parallel or opposite).
S_vector PoseSlerpDir(const S_vector &a, const S_vector &b, float t)
{
    float dot = a.Dot(b);
    if (dot > 1.0f)
        dot = 1.0f;
    else if (dot < -1.0f)
        dot = -1.0f;
    const float angle = static_cast<float>(std::acos(dot));
    if (angle > 1e-8f && 3.14159274f - angle > 1e-8f) {
        const double inv = 1.0 / std::sin(static_cast<double>(angle));
        const float wa = static_cast<float>(std::sin((1.0 - t) * angle) * inv);
        const float wb = static_cast<float>(std::sin(static_cast<double>(angle) * t) * inv);
        return S_vector(wb * b.x + wa * a.x, wb * b.y + wa * a.y, wb * b.z + wa * a.z);
    }
    return b;
}

// ------------------------------------------------------------------ the skeleton

// 0x007a4700
void C_HumanWeaponPose::AddBone(int index, int parent, I3D_frame *frame)
{
    if (index < 0 || index >= POSE_BONE_COUNT || parent >= POSE_BONE_COUNT || !frame)
        return;
    S_PoseBone &bone = m_bones[index];
    bone.frame = frame;
    bone.child[2] = nullptr;
    bone.child[1] = nullptr;
    bone.index = index;
    bone.child[0] = nullptr;
    if (parent < 0) {
        m_root = &bone;
        bone.parent = nullptr;
        ++m_boneCount;
        return;
    }
    bone.parent = &m_bones[parent];
    S_PoseBone &p = m_bones[parent];
    for (int i = 0; i < 3; ++i) {
        if (!p.child[i]) {
            p.child[i] = &bone;
            ++m_boneCount;
            return;
        }
    }
}

// 0x007a51b0: the axes the joint between `index` and its neighbour `other` turns about.
void C_HumanWeaponPose::SetLimitAxes(int index, int other, uint32_t axes)
{
    S_PoseBone &bone = m_bones[index];
    for (int i = 0; i < 3; ++i) {
        if (bone.child[i] && bone.child[i]->index == other) {
            bone.limits[i].axes = axes;
            return;
        }
    }
    if (bone.parent && bone.parent->index == other) {
        bone.limits[3].axes = axes;
        bone.limits[4].axes = axes;
        bone.limits[5].axes = axes;
    }
}

// 0x007a5250
void C_HumanWeaponPose::SetLimitStiffness(int index, int other, float stiffness)
{
    S_PoseBone &bone = m_bones[index];
    for (int i = 0; i < 3; ++i) {
        if (bone.child[i] && bone.child[i]->index == other) {
            bone.limits[i].stiffness = stiffness;
            return;
        }
    }
    if (bone.parent && bone.parent->index == other) {
        bone.limits[3].stiffness = stiffness;
        bone.limits[4].stiffness = stiffness;
        bone.limits[5].stiffness = stiffness;
    }
}

// 0x007a4820: turns bone `index` so that the direction to its first child is `dir` (world);
// `weight` 0..1 blends from the current rotation, a negative one sets it outright.
void C_HumanWeaponPose::AimBone(int index, const S_vector &dir, float weight)
{
    if (index < 0 || index >= POSE_BONE_COUNT)
        return;
    S_PoseBone &bone = m_bones[index];
    S_matrix world = bone.frame->GetMatrix();
    world.m[3][0] = 0.0f;
    world.m[3][1] = 0.0f;
    world.m[3][2] = 0.0f;
    world.m[3][3] = 1.0f;
    I3D_frame *parentFrame = bone.parent ? bone.parent->frame : m_model;
    const S_matrix parentInverse = PoseTransposed(parentFrame->GetMatrix());
    S_matrix m = bone.childMatrix[0] * world;
    PoseAimZ(m, dir);
    m = PoseTransposed(bone.childMatrix[0]) * m;
    m = m * parentInverse;
    if (std::signbit(weight)) {                         // (an integer sign test: -0 too)
        PoseOrthonormalize(m);
    } else {
        S_quat aimed;
        aimed.Make(m);
        S_quat current;
        current.Make(bone.frame->GetLocalMatrix());
        m.SetRot(aimed.Slerp(current, 1.0f - weight, false));
    }
    PoseSetFrameMatrix(bone.frame, PoseWithLocalPos(m, bone.frame));
}

// 0x007a52e0: gives the hands the world rotations of `left` / `right` (null: left as they are).
void C_HumanWeaponPose::SetHandRotations(I3D_frame *left, I3D_frame *right)
{
    const int bones[2] = {POSE_BONE_LEFT_HAND, POSE_BONE_RIGHT_HAND};
    I3D_frame *const targets[2] = {left, right};
    for (int i = 0; i < 2; ++i) {
        if (!targets[i])
            continue;
        S_matrix m = targets[i]->GetMatrix();
        m.m[3][2] = 0.0f;
        m.m[3][1] = 0.0f;
        m.m[3][0] = 0.0f;
        I3D_frame *hand = m_bones[bones[i]].frame;
        m *= PoseInverseRotation(hand->GetParent()->GetMatrix());
        PoseSetFrameMatrix(hand, PoseWithLocalPos(m, hand));
    }
}

// 0x007a5550: the same from world matrices.
void C_HumanWeaponPose::SetHandMatrices(const S_matrix &left, const S_matrix &right)
{
    const int bones[2] = {POSE_BONE_LEFT_HAND, POSE_BONE_RIGHT_HAND};
    const S_matrix *const targets[2] = {&left, &right};
    for (int i = 0; i < 2; ++i) {
        S_matrix m = *targets[i];
        m.m[3][0] = 0.0f;
        m.m[3][1] = 0.0f;
        m.m[3][2] = 0.0f;
        I3D_frame *hand = m_bones[bones[i]].frame;
        m *= PoseInverseRotation(hand->GetParent()->GetMatrix());
        PoseSetFrameMatrix(hand, PoseWithLocalPos(m, hand));
    }
}

// 0x007a5790: swings the middle joint `index` (an elbow or knee) about the line from its
// parent to its child towards `dir`, by `weight`; the child keeps its place and world rotation.
void C_HumanWeaponPose::BendJoint(int index, const S_vector &dir, float weight)
{
    S_PoseBone &bone = m_bones[index];
    S_PoseBone *upper = bone.parent;
    S_PoseBone *lower = bone.child[0];
    if (!upper || !lower)
        return;
    const S_vector &start = upper->frame->GetWorldPos();
    S_vector axis = lower->frame->GetWorldPos() - start;
    axis.Normalize();
    const S_vector toJoint = bone.frame->GetWorldPos() - upper->frame->GetWorldPos();
    const S_vector along = axis * toJoint.Dot(axis);
    const S_vector perp = toJoint - along;
    const float perpSq = perp.Square();
    if (perpSq < 1e-8f)
        return;
    float perpLength = 1.0f;
    if (std::fabs(perpSq - 1.0f) >= 1e-8f) {
        perpLength = std::sqrt(perpSq);
        if (perpLength < 1e-8f)
            return;
    }
    const S_vector wanted = dir - axis * axis.Dot(dir);
    const S_vector mixed = wanted * weight + perp * (1.0f - weight);
    const float mixedSq = mixed.Square();
    if (mixedSq < 1e-8f)
        return;
    float mixedLength = 1.0f;
    if (std::fabs(mixedSq - 1.0f) >= 1e-8f) {
        mixedLength = std::sqrt(mixedSq);
        if (mixedLength < 1e-8f)
            return;
    }
    const S_vector newPerp = mixed * (perpLength / mixedLength);
    const S_matrix lowerWorld = lower->frame->GetMatrix();
    const S_vector newJoint = along + newPerp;
    AimBone(upper->index, newJoint, -1.0f);
    AimBone(bone.index, lowerWorld(3) - bone.frame->GetWorldPos(), -1.0f);
    S_matrix m = lowerWorld * PoseInverseRotation(lower->parent->frame->GetMatrix());
    m = PoseWithLocalPos(m, lower->frame);
    PoseSetFrameMatrix(lower->frame, m);
}

// ------------------------------------------------------------------ effector chains

// 0x007a5f70
void C_HumanWeaponPose::ClearChains()
{
    for (S_PoseBone &bone : m_bones) {
        bone.pinMask = 0;
        bone.chainChildren[2] = nullptr;
        bone.chainChildren[1] = nullptr;
        bone.chainChildren[0] = nullptr;
        bone.chainParent = nullptr;
    }
}

// 0x007a5fa0: `target` is reached from `node` without passing `from`.
static bool LeadsTo(const S_PoseBone *target, const S_PoseBone *node, const S_PoseBone *from)
{
    if (node == target)
        return true;
    for (int i = 0; i < 3; ++i)
        if (node->child[i] && node->child[i] != from && LeadsTo(target, node->child[i], node))
            return true;
    return node->parent && node->parent != from && LeadsTo(target, node->parent, node);
}

// 0x007a6060: links the bones from `from` to `target` into the chain of effector `bit`; each
// bone new to the chains turns about the joint it is entered through (its chain frame) at
// the length of that joint's limb. Returns the degrees of freedom added.
static int LinkChain(S_PoseBone *cur, S_PoseBone *target, uint32_t bit)
{
    int dof = 0;
    S_PoseBone *prev = nullptr;
    S_matrix prevWorld = cur->chainWorld;
    while (cur != target) {
        S_PoseBone *next = nullptr;
        for (int i = 0; i < 3 && !next; ++i) {
            S_PoseBone *c = cur->child[i];
            if (c && c != prev && LeadsTo(target, c, cur)) {
                next = c;
                next->chainOffset = cur->childMatrix[i];
                next->chainFrame = cur->frame;
                next->chainLength = cur->childLength[i];
                next->chainLimit = &cur->limits[i];
            }
        }
        if (!next) {
            S_PoseBone *p = cur->parent;
            if (!p || p == prev || !LeadsTo(target, p, cur))
                return dof;
            int k;
            for (k = 0; k < 3; ++k)
                if (p->child[k] == cur)
                    break;
            if (k == 3)
                return dof;
            next = p;
            next->chainOffset = PoseTurnedAround(p->childMatrix[k]);
            next->chainFrame = p->frame;
            next->chainLength = p->childLength[k];
            next->chainLimit = &cur->limits[3];
        }
        prev = cur;
        if (next->pinMask == 0) {
            next->chainWorld = next->chainOffset * next->chainFrame->GetMatrix();
            next->chainLocal = next->chainWorld * PoseInverseRotation(prevWorld);
            next->chainLocal.m[3][1] = 0.0f;
            next->chainLocal.m[3][0] = 0.0f;
            next->chainParent = cur;
            next->chainLocal.m[3][2] = cur->chainLength;
            int slot;
            for (slot = 0; slot < 3; ++slot)
                if (!cur->chainChildren[slot])
                    break;
            if (slot == 3)
                return dof;
            cur->chainChildren[slot] = next;
            const uint32_t axes = next->chainLimit->axes;
            if (axes & POSE_AXIS_X)
                ++dof;
            if (axes & POSE_AXIS_Y)
                ++dof;
            if (axes & POSE_AXIS_Z)
                ++dof;
        }
        next->pinMask |= bit;
        prevWorld = next->chainWorld;
        cur = next;
    }
    return dof;
}

// 0x007a5e90: builds the chains from the chain root to the effectors.
static void BuildChain(S_PoseChain &chain)
{
    if (chain.count <= 0)
        return;
    S_PoseBone *root = chain.root;
    root->chainLocal = root->frame->GetMatrix();
    root->chainWorld = root->frame->GetMatrix();
    root->chainLength = 0.0f;
    uint32_t bit = 1;
    for (int i = 0; i < chain.count; ++i) {
        root->pinMask |= bit;
        LinkChain(root, chain.pins[i].bone, bit);
        bit *= 2;
    }
}

void C_HumanWeaponPose::BuildChains()
{
    BuildChain(m_chain);
}

// 0x007a6ad0: the chain matrices of `bone` and its chain children from the frames as they are.
static void RefreshChainBone(S_PoseBone *bone, const S_PoseBone *chainParent, const S_matrix &parentWorld)
{
    bone->chainWorld = bone->chainOffset * bone->chainFrame->GetMatrix();
    bone->chainLocal = bone->chainWorld * PoseInverseRotation(parentWorld);
    I3D_frame *frame = bone->frame;
    bone->chainLocal.m[3][1] = 0.0f;
    bone->chainLocal.m[3][0] = 0.0f;
    bone->chainLocal.m[3][2] = chainParent->chainLength;
    if (bone->chainFrame != frame) {
        const S_matrix &world = frame->GetMatrix();
        bone->chainWorld.m[3][0] = world.m[3][0];
        bone->chainWorld.m[3][1] = world.m[3][1];
        bone->chainWorld.m[3][2] = world.m[3][2];
        bone->chainWorld.m[3][3] = world.m[3][3];
    }
    for (S_PoseBone *child : bone->chainChildren)
        if (child)
            RefreshChainBone(child, bone, bone->chainWorld);
    // (Then the position of the chain parent is put in.)
    bone->chainWorld.m[3][0] = parentWorld.m[3][0];
    bone->chainWorld.m[3][1] = parentWorld.m[3][1];
    bone->chainWorld.m[3][2] = parentWorld.m[3][2];
    bone->chainWorld.m[3][3] = parentWorld.m[3][3];
}

// 0x007a69f0
void C_HumanWeaponPose::RefreshChain(S_PoseChain &chain)
{
    S_PoseBone *root = chain.root;
    root->chainLocal = root->frame->GetMatrix();
    root->chainWorld = root->frame->GetMatrix();
    for (S_PoseBone *child : root->chainChildren)
        if (child)
            RefreshChainBone(child, root, root->chainLocal);
}

// 0x007a7840: the chain world matrices below `bone` from their local ones.
static void UpdateChainWorld(S_PoseBone *bone)
{
    while (bone) {
        if (bone->chainParent)
            bone->chainWorld = bone->chainLocal * bone->chainParent->chainWorld;
        if (bone->chainChildren[0])
            UpdateChainWorld(bone->chainChildren[0]);
        if (bone->chainChildren[1])
            UpdateChainWorld(bone->chainChildren[1]);
        bone = bone->chainChildren[2];
    }
}

// The tip of an effector bone in its chain (its chain position plus its length along z).
static S_vector ChainTip(const S_PoseBone *bone)
{
    return bone->chainWorld(2) * bone->chainLength + bone->chainWorld(3);
}

static S_quat QuatMultiply(const S_quat &a, const S_quat &b)
{
    S_quat q;
    q.v = a.v.Cross(b.v) + (b.v * a.s + a.v * b.s);
    q.s = a.s * b.s - a.v.Dot(b.v);
    return q;
}

// 0x007a70d0: one step of the cyclic coordinate descent, leaves first: the joint of `bone`
// turns to bring the effectors of its chains towards their targets (the first effector's
// rotation outright, the others' blended in by half their weights).
static void SolveChainBone(const S_PoseChain &chain, S_PoseBone *bone)
{
    for (S_PoseBone *child : bone->chainChildren)
        if (child)
            SolveChainBone(chain, child);
    uint32_t bit = 1;
    bool turned = false;
    S_quat turn;
    for (int i = 0; i < chain.count; ++i, bit *= 2) {
        if (!(bone->pinMask & bit))
            continue;
        const S_PoseLimit *limit = bone->chainLimit;
        const uint32_t axes = limit->axes;
        if (axes == 0)
            continue;
        const S_matrix &w = bone->chainWorld;
        const S_PosePin &pin = chain.pins[i];
        // The target and the effector's tip in the joint's axes.
        const S_vector t = S_vector(pin.target.x, pin.target.y, pin.target.z) - w(3);
        S_vector toTarget(t.Dot(w(0)), t.Dot(w(1)), t.Dot(w(2)));
        toTarget.Normalize();
        const S_vector e = ChainTip(pin.bone) - w(3);
        S_vector toTip(e.Dot(w(0)), e.Dot(w(1)), e.Dot(w(2)));
        toTip.Normalize();
        const S_vector cross = toTarget.Cross(toTip);
        S_vector axis((axes & POSE_AXIS_X) ? cross.x : 0.0f, (axes & POSE_AXIS_Y) ? cross.y : 0.0f,
                      (axes & POSE_AXIS_Z) ? cross.z : 0.0f);
        axis *= limit->stiffness;
        const float sq = axis.Square();
        if (sq < 1e-8f)
            continue;
        // sin of the angle (axis is the cross product of unit vectors), at most 1; an axis
        // longer than 1 (stiffness over 1) is left as it is.
        float length = 1.0f;
        if (std::fabs(sq - 1.0f) >= 1e-8f) {
            length = std::sqrt(sq);
            if (length <= 1e-8f)
                continue;
            if (length > 1.0f)
                length = 1.0f;
        }
        axis *= 1.0f / length;
        const float half = static_cast<float>(std::asin(length) * 0.5);
        const float s = std::sin(half);
        S_quat q;
        q.v = axis * s;
        q.s = std::cos(half);
        if (turned) {
            turn = turn.Slerp(q, pin.weight * 0.5f, false);
        } else {
            turned = true;
            turn = q;
        }
    }
    if (turned) {
        S_quat current;
        current.Make(bone->chainLocal);
        bone->chainLocal.SetRot3(QuatMultiply(turn, current));
        UpdateChainWorld(bone);
    }
}

// 0x007a6c40: up to `iterations` passes until every effector is within 1 cm of its target;
// when they did not get there and `moveRoot`, the pelvis moves by the average miss. Returns
// the passes made (the original returns `this` after moving the pelvis).
int C_HumanWeaponPose::SolveChain(S_PoseChain &chain, int iterations, bool moveRoot)
{
    int pass = 0;
    for (; pass < iterations; ++pass) {
        float worst = 0.0f;
        for (int i = 0; i < chain.count; ++i) {
            const S_PosePin &pin = chain.pins[i];
            const S_vector miss = S_vector(pin.target.x, pin.target.y, pin.target.z) - pin.bone->chainWorld(3) -
                                  pin.bone->chainWorld(2) * pin.bone->chainLength;
            if (worst < std::fabs(miss.x))
                worst = std::fabs(miss.x);
            if (worst < std::fabs(miss.y))
                worst = std::fabs(miss.y);
            if (worst < std::fabs(miss.z))
                worst = std::fabs(miss.z);
        }
        if (worst < 0.01f)
            break;
        for (S_PoseBone *child : chain.root->chainChildren)
            if (child)
                SolveChainBone(chain, child);
    }
    if (pass != iterations || !moveRoot)
        return pass;
    S_vector sum(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < chain.count; ++i) {
        const S_PosePin &pin = chain.pins[i];
        const S_PoseBone *b = pin.bone;
        sum += S_vector(pin.target.x - b->chainWorld.m[3][0] - b->chainLength * b->chainWorld.m[2][0],
                        pin.target.y - b->chainWorld.m[3][1] - b->chainLength * b->chainWorld.m[2][1],
                        (pin.target.z - b->chainWorld.m[3][2]) - b->chainLength * b->chainWorld.m[2][2]);
    }
    const float inv = 1.0f / static_cast<float>(chain.count);
    I3D_frame *pelvis = m_bones[POSE_BONE_PELVIS].frame;
    S_vector pos = sum * inv + pelvis->GetWorldPos();
    if (I3D_frame *parent = pelvis->GetParent()) {
        const S_vector rel = pos - parent->GetWorldPos();
        const S_matrix &pm = parent->GetMatrix();
        pos = S_vector(rel.Dot(pm(0)), rel.Dot(pm(1)), rel.Dot(pm(2)));
    }
    if (pelvis->GetFrameFlags() & FRMFLAGS_POS_LOCKED) {
        pelvis->Update();
        return pass;
    }
    pelvis->SetPosInline(pos);
    pelvis->Update();
    return pass;
}

// 0x007a6550: the solved world matrices of the joints (the chain matrix back through the
// chain offset); a bone turning about its own joint also gets its tip as position.
static void ComputeSolved(S_PoseBone *bone, S_PoseBone *chainParent)
{
    const S_matrix solved = PoseTransposed(bone->chainOffset) * bone->chainWorld;
    if (bone->chainFrame == bone->frame) {
        bone->solvedWorld = solved;
        bone->solvedWorld.m[3][0] = bone->chainLength * bone->chainWorld.m[2][0] + bone->chainWorld.m[3][0];
        bone->solvedWorld.m[3][1] = bone->chainLength * bone->chainWorld.m[2][1] + bone->chainWorld.m[3][1];
        bone->solvedWorld.m[3][2] = bone->chainLength * bone->chainWorld.m[2][2] + bone->chainWorld.m[3][2];
    } else {
        chainParent->solvedWorld = solved;
    }
    for (S_PoseBone *child : bone->chainChildren)
        if (child)
            ComputeSolved(child, bone);
}

// 0x007a6690: puts the solved joints into the frames, walking the whole skeleton from `bone`
// (not back to `from`). A solved bone below an unsolved one keeps its frame's position and
// passes on how far its solved position is off; the skeleton's root reports that in `shift`.
static void ApplySolved(S_PoseBone *bone, S_PoseBone *from, const S_vector &shiftIn, S_vector &shift)
{
    S_vector delta = shiftIn;
    if (bone->pinMask != 0) {
        S_PoseBone *parent = bone->parent;
        I3D_frame *frame = nullptr;
        S_matrix m;
        if (!parent || parent->pinMask == 0) {
            m = bone->solvedWorld * PoseInverseRotation(bone->frame->GetParent()->GetMatrix());
            I3D_frame *own = bone->frame;
            const S_matrix &local = own->GetLocalMatrix();
            m.m[3][0] = local.m[3][0];
            m.m[3][1] = local.m[3][1];
            m.m[3][2] = local.m[3][2];
            m.m[3][3] = local.m[3][3];
            PoseOrthonormalize(m);
            delta = S_vector(bone->solvedWorld.m[3][0], bone->solvedWorld.m[3][1], bone->solvedWorld.m[3][2]) -
                    own->GetWorldPos();
            frame = own;
        } else {
            S_matrix rel = bone->solvedWorld;
            rel.m[3][0] -= parent->solvedWorld.m[3][0];
            rel.m[3][1] -= parent->solvedWorld.m[3][1];
            rel.m[3][2] -= parent->solvedWorld.m[3][2];
            m = rel * PoseInverseRotation(parent->solvedWorld);
            PoseOrthonormalize(m);
            if (bone->chainChildren[0])
                frame = bone->frame;
        }
        if (frame)
            frame->SetLocalMatrix(m);
    }
    for (S_PoseBone *child : bone->child)
        if (child && child != from)
            ApplySolved(child, bone, delta, shift);
    if (bone->parent) {
        if (bone->parent != from)
            ApplySolved(bone->parent, bone, delta, shift);
        return;
    }
    shift = delta;
}

// 0x007a6420: puts the solved chain into the frames and moves the pelvis by the offset found.
void C_HumanWeaponPose::ApplyChain(S_PoseChain &chain)
{
    S_PoseBone *root = chain.root;
    root->solvedWorld = root->frame->GetMatrix();
    for (S_PoseBone *child : root->chainChildren)
        if (child)
            ComputeSolved(child, root);
    S_vector shift(0.0f, 0.0f, 0.0f);
    ApplySolved(root, nullptr, S_vector(0.0f, 0.0f, 0.0f), shift);
    I3D_frame *pelvis = m_bones[POSE_BONE_PELVIS].frame;
    pelvis->SetPos(shift + pelvis->GetWorldPos());
    pelvis->Update();
}
