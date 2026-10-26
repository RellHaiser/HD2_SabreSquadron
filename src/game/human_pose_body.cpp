// The body parts of the weapon pose that do not hold anything (original source 0x007a8980..
// 0x007ae190): a prone body fitted to the ground under it, and the head looking at a point or
// glancing round.
#include "game/human_pose_internal.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "game/collision.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "ls3d/i3d.h"

namespace {

// The collision filters of the prone body's ground tests.
constexpr uint32_t PRONE_TEST_TYPES = 0xC8102;
constexpr uint32_t PRONE_TEST_FLAGS = 0x230000;

// The roll of a prone body is held within 30 degrees.
const float MAX_PRONE_ROLL = 0.5235988f;

S_vector &AsVector(S_vector4 &v) { return *reinterpret_cast<S_vector *>(&v); }

// `n` without its part along `axis` (normalised first), normalised.
S_vector Across(const S_vector &n, S_vector axis)
{
    axis.Normalize();
    S_vector r = n - axis * n.Dot(axis);
    r.Normalize();
    return r;
}

// A unit vector at least `minY` up (its horizontal part scaled to `xz` when it was steeper).
S_vector NotSteeper(S_vector n, float minY, float xz)
{
    if (n.y < minY) {
        const float s = std::sqrt(1.0f - n.y * n.y);
        if (s <= 1e-8f) {
            n.x = xz;
            n.z = 0.0f;
        } else {
            const float k = xz / s;
            n.x *= k;
            n.z *= k;
        }
        n.y = minY;
    }
    return n;
}

// (rand() >> 4) % range, as the original draws its random times and angles.
int RandomBelow(int range) { return (std::rand() >> 4) % range; }

}  // namespace

// ------------------------------------------------------------------ the prone body

// 0x007a8980: the body lying on the ground: the four lying spheres dropped onto the ground
// below them give the ground's normals at the chest, the middle and the legs and its heights
// there; the model is turned to the middle normal (rolling at most 30 degrees), the pelvis
// raised / lowered, the back and chest turned to the front normal and the hips and knees to the
// back one. The normals and heights blend over m_proneBlendLength ms; the tests run every
// second update (each one while m_proneSettle runs), the bones are orthonormalised every
// fourth.
void C_HumanWeaponPose::FitProneBody()
{
    // The body's extent along the model's z (hands to feet; the lowest value is only taken
    // when it is not a new highest one): under a metre it is not lying out.
    float lowest = 1e6f;
    float highest = -1e6f;
    const int ends[4] = {POSE_BONE_LEFT_HAND, POSE_BONE_RIGHT_HAND, POSE_BONE_LEFT_FOOT, POSE_BONE_RIGHT_FOOT};
    for (int end : ends) {
        const float d = (m_bones[end].frame->GetWorldPos() - m_model->GetWorldPos()).Dot(m_model->GetMatrix()(2));
        if (d > highest)
            highest = d;
        else if (d < lowest)
            lowest = d;
    }
    if (highest - lowest < 1.0f) {
        m_proneNow[0].x = PoseUnset();
        return;
    }

    C_HumanMovement *movement = m_movement;
    movement->UpdateLieDummies();
    S_vector4 spheres[4];
    movement->GetStanceSpheres(HUMAN_STANCE_PRONE, spheres);
    const bool orthonormalize = (m_updateCount & 3) == 0;
    bool settle = m_proneSettle > 0;
    const S_vector4 up4 = PoseRow4(m_model->GetMatrix(), 1);
    const S_vector up = PoseXYZ(up4);
    const S_vector down = up * -10.0f;
    if (PoseIsUnset(m_proneNow[0].x)) {
        for (int i = 0; i < 3; ++i) {
            m_proneTo[i] = up4;
            m_proneFrom[i] = m_proneTo[i];
            m_proneNow[i] = m_proneFrom[i];
        }
        for (int i = 3; i < 6; ++i) {
            PoseSetXYZ(m_proneNow[i], S_vector(0.0f, 0.0f, 0.0f));
            PoseSetXYZ(m_proneFrom[i], S_vector(0.0f, 0.0f, 0.0f));
            PoseSetXYZ(m_proneTo[i], S_vector(0.0f, 0.0f, 0.0f));
        }
        for (int i = 0; i < 6; ++i)
            m_proneBlendTime[i] = 1;
        settle = true;
    }

    const S_vector4 dir4 = m_owner->GetDirection4();
    const float turned = std::fabs(1.0f - (dir4.x * m_proneDir.x + dir4.y * m_proneDir.y + dir4.z * m_proneDir.z));
    const S_vector moved = m_model->GetWorldPos() - PoseXYZ(m_proneNow[4]);
    if (settle || moved.Square() > 1e-6f || turned > 0.001f) {
        m_proneSettle -= m_time;
        if (m_proneSettle < 0)
            m_proneSettle = 0;
        // Head end first.
        m_proneSpheres[0] = spheres[3];
        m_proneSpheres[1] = spheres[2];
        m_proneSpheres[2] = spheres[1];
        m_proneSpheres[3] = spheres[0];
        if ((m_updateCount & 1) != 0 || settle) {
            S_vector4 start[4];
            for (int i = 0; i < 4; ++i)
                start[i] = m_proneSpheres[i];
            if (m_proneSettle != 0) {
                // Settling: the tests start no lower than the neck.
                const float neck = (m_bones[POSE_BONE_NECK].frame->GetWorldPos() - m_model->GetWorldPos()).Dot(up);
                for (int i = 0; i < 4; ++i) {
                    if ((PoseXYZ(m_proneSpheres[i]) - m_model->GetWorldPos()).Dot(up) < neck)
                        start[i] = PoseVector4(up * neck + PoseXYZ(m_proneSpheres[i]), dir4.w);
                }
            }
            float radius[4];
            for (int i = 0; i < 4; ++i) {
                radius[i] = m_proneSpheres[i].w * 0.9f;
                m_proneGround[i] = PoseVector4(PoseXYZ(m_proneSpheres[i]) - up * radius[i], dir4.w);
            }
            std::memset(m_proneHits, 0, 6);
            m_owner->RemoveCollision(false);
            for (int i = 0; i < 4; ++i) {
                if (CollisionSphereTest(PoseXYZ(start[i]), down, radius[i], &AsVector(m_proneGround[i]),
                                        &AsVector(m_proneNormals[i]), PRONE_TEST_TYPES, PRONE_TEST_FLAGS)) {
                    PoseSetXYZ(m_proneGround[i], PoseXYZ(m_proneGround[i]) - PoseXYZ(m_proneNormals[i]) * radius[i]);
                    m_proneHits[i] = (PoseXYZ(m_proneGround[i]) - PoseXYZ(m_proneSpheres[i])).Square() >= 0.25f ? 2 : 1;
                }
            }
            m_owner->RestoreCollision(false);
            for (int i = 0; i < 4; ++i)
                if (m_proneHits[i] != 1)
                    PoseSetXYZ(m_proneNormals[i], S_vector(0.0f, 1.0f, 0.0f));
            const S_vector g[4] = {PoseXYZ(m_proneGround[0]), PoseXYZ(m_proneGround[1]),
                                   PoseXYZ(m_proneGround[2]), PoseXYZ(m_proneGround[3])};
            const S_vector nrm[4] = {PoseXYZ(m_proneNormals[0]), PoseXYZ(m_proneNormals[1]),
                                     PoseXYZ(m_proneNormals[2]), PoseXYZ(m_proneNormals[3])};
            // The chest: the first two normals across the line between them.
            S_vector n = PoseSlerpDir(nrm[0], nrm[1], 0.25f);
            n = NotSteeper(Across(n, g[0] - g[1]), 0.7071f, 0.7071f);
            if ((n - PoseXYZ(m_proneTo[0])).Square() > 1e-5f) {
                m_proneFrom[0] = m_proneNow[0];
                PoseSetXYZ(m_proneTo[0], n);
                m_proneBlendTime[0] = m_proneBlendLength;
            }
            // The legs: the last two.
            n = PoseSlerpDir(nrm[2], nrm[3], 0.75f);
            n = NotSteeper(Across(n, g[2] - g[3]), 0.7071f, 0.7071f);
            if ((n - PoseXYZ(m_proneTo[2])).Square() > 1e-5f) {
                m_proneFrom[2] = m_proneNow[2];
                PoseSetXYZ(m_proneTo[2], n);
                m_proneBlendTime[2] = m_proneBlendLength;
            }
            // The middle: the middle two across their line, then across the two diagonals.
            n = PoseSlerpDir(nrm[1], nrm[2], 0.5f);
            n = Across(n, g[1] - g[2]);
            const S_vector a = Across(n, g[0] * 0.5f - g[2] * 0.5f);
            const S_vector b = Across(n, g[3] * 0.5f - g[1] * 0.5f);
            n = NotSteeper(PoseSlerpDir(a, b, 0.5f), 0.7071f, 0.7071f);
            if ((n - PoseXYZ(m_proneTo[1])).Square() > 1e-5f) {
                m_proneFrom[1] = m_proneNow[1];
                PoseSetXYZ(m_proneTo[1], n);
                m_proneBlendTime[1] = m_proneBlendLength;
            }
            // The spheres that found nothing (or the ground far down) rest at their height.
            for (int i = 0; i < 4; ++i)
                if (m_proneHits[i] != 1)
                    m_proneGround[i] = PoseVector4(PoseXYZ(m_proneSpheres[i]) - up * radius[i], dir4.w);
            // The ground's heights at the chest, the middle and the legs against the model.
            const S_vector modelPos = m_model->GetWorldPos();
            float *const to[3] = {&m_proneTo[3].x, &m_proneTo[3].y, &m_proneTo[3].z};
            float *const from[3] = {&m_proneFrom[3].x, &m_proneFrom[3].y, &m_proneFrom[3].z};
            const float *const now[3] = {&m_proneNow[3].x, &m_proneNow[3].y, &m_proneNow[3].z};
            for (int i = 0; i < 3; ++i) {
                const S_vector mid = (PoseXYZ(m_proneGround[i]) + PoseXYZ(m_proneGround[i + 1])) * 0.5f;
                const float h = (mid - modelPos).Dot(up);
                if (std::fabs(h - *to[i]) > 0.0001f) {
                    *from[i] = *now[i];
                    *to[i] = h;
                    m_proneBlendTime[3 + i] = m_proneBlendLength;
                }
            }
        }
        // The blends.
        for (int i = 0; i < 3; ++i) {
            if (m_proneBlendTime[i] == 0)
                continue;
            m_proneBlendTime[i] -= m_time;
            if (m_proneBlendTime[i] < 0)
                m_proneBlendTime[i] = 0;
            PoseSetXYZ(m_proneNow[i], PoseSlerpDir(PoseXYZ(m_proneFrom[i]), PoseXYZ(m_proneTo[i]),
                                                   1.0f - static_cast<float>(m_proneBlendTime[i]) /
                                                              static_cast<float>(m_proneBlendLength)));
        }
        float *const now[3] = {&m_proneNow[3].x, &m_proneNow[3].y, &m_proneNow[3].z};
        const float *const from[3] = {&m_proneFrom[3].x, &m_proneFrom[3].y, &m_proneFrom[3].z};
        const float *const to[3] = {&m_proneTo[3].x, &m_proneTo[3].y, &m_proneTo[3].z};
        for (int i = 0; i < 3; ++i) {
            int &left = m_proneBlendTime[3 + i];
            if (left == 0)
                continue;
            left -= m_time;
            if (left < 0)
                left = 0;
            const float f = static_cast<float>(left) / static_cast<float>(m_proneBlendLength);
            *now[i] = f * *from[i] + (1.0f - f) * *to[i];
        }
    }

    // The model turned to the middle normal (at most 40 degrees off up), rolling 30 at most.
    S_matrix m = m_model->GetMatrix();
    I3D_frame *parent = m_model->GetParent();
    const S_matrix parentInverse = parent ? PoseInverseRotation(parent->GetMatrix()) : IdentityMatrix();
    S_vector n = NotSteeper(PoseXYZ(m_proneNow[1]), 0.766f, 0.6427f);
    n.Normalize();
    m(1) = n;
    m.m[1][3] = m_proneNow[1].w;
    S_vector z = m(2) - n * n.Dot(m(2));
    z.Normalize();
    m(2) = z;
    m(0) = n.Cross(z);
    m.m[0][3] = dir4.w;
    float roll = PoseGetRoll(m);
    if (roll > MAX_PRONE_ROLL) {
        roll = MAX_PRONE_ROLL;
        m.SetDir(m(2), roll);
    } else if (roll < -MAX_PRONE_ROLL) {
        roll = -MAX_PRONE_ROLL;
        m.SetDir(m(2), roll);
    }
    m_owner->SetDirection4(PoseRow4(m, 2));
    m_movement->m_unknown250 = roll;
    m_proneDir = PoseRow4(m, 2);
    m_proneNow[4] = PoseWorldPos4(m_model);
    m *= parentInverse;
    const S_vector &modelPos = m_model->GetPos();
    m.m[3][0] = modelPos.x;
    m.m[3][1] = modelPos.y;
    m.m[3][2] = modelPos.z;
    m.m[3][3] = 1.0f;
    m_model->SetLocalMatrix(m);
    m_model->Update();

    // The pelvis up or down by the ground's height (the middle one when it is the smallest).
    const S_vector4 &heights = m_proneNow[3];
    float h = (heights.z + heights.x + heights.y) * 0.33f;
    if (std::fabs(heights.y) < std::fabs(h))
        h = heights.y;
    I3D_frame *pelvis = m_bones[POSE_BONE_PELVIS].frame;
    pelvis->SetPos(PoseXYZ(m_proneNow[1]) * h + pelvis->GetWorldPos());

    // The back to the chest's normal (along the body), the chest the rest of the way.
    S_vector front = PoseSlerpDir(n, PoseXYZ(m_proneNow[0]), 0.75f);
    front = Across(front, (PoseXYZ(spheres[0]) * 0.5f + up * heights.x) - PoseXYZ(spheres[2]) * 0.5f - up * heights.y);
    S_matrix turn = {};
    PoseRotationBetween(n, front, turn);
    turn.m[3][3] = 1.0f;
    PoseTurnFrame(m_bones[POSE_BONE_BACK].frame, m_bones[POSE_BONE_BACK].parent->frame, turn, orthonormalize);
    turn = {};
    PoseRotationBetween(front, PoseXYZ(m_proneNow[0]), turn);
    turn.m[3][3] = 1.0f;
    PoseTurnFrame(m_bones[POSE_BONE_CHEST].frame, m_bones[POSE_BONE_CHEST].parent->frame, turn, orthonormalize);

    // The hips to the legs' normal, the knees the rest of the way.
    S_vector back = PoseSlerpDir(n, PoseXYZ(m_proneNow[2]), 0.75f);
    back = Across(back, (PoseXYZ(spheres[3]) * 0.5f + up * heights.z) - PoseXYZ(spheres[1]) * 0.5f - up * heights.y);
    turn = {};
    PoseRotationBetween(n, back, turn);
    turn.m[3][3] = 1.0f;
    PoseTurnFrame(m_bones[POSE_BONE_LEFT_HIP].frame, m_bones[POSE_BONE_LEFT_HIP].parent->frame, turn, orthonormalize);
    PoseTurnFrame(m_bones[POSE_BONE_RIGHT_HIP].frame, m_bones[POSE_BONE_RIGHT_HIP].parent->frame, turn, orthonormalize);
    turn = {};
    PoseRotationBetween(back, PoseXYZ(m_proneNow[2]), turn);
    turn.m[3][3] = 1.0f;
    PoseTurnFrame(m_bones[POSE_BONE_LEFT_KNEE].frame, m_bones[POSE_BONE_LEFT_KNEE].parent->frame, turn, orthonormalize);
    PoseTurnFrame(m_bones[POSE_BONE_RIGHT_KNEE].frame, m_bones[POSE_BONE_RIGHT_KNEE].parent->frame, turn, orthonormalize);
    m_model->Update();
}

// ------------------------------------------------------------------ the head

namespace {

// A direction in the axes of `m` (its 3x3 part).
S_vector IntoAxes(const S_vector &v, const S_matrix &m)
{
    return S_vector(v.Dot(m(0)), v.Dot(m(1)), v.Dot(m(2)));
}

// The head's direction in the chest's axes kept forward: x and y within [-1, 1], z at least
// `minZ` (the rest scaled to `xy`).
S_vector KeepForward(S_vector c, float minZ, float xy)
{
    if (c.x > 1.0f)
        c.x = 1.0f;
    else if (c.x < -1.0f)
        c.x = -1.0f;
    if (c.y > 1.0f)
        c.y = 1.0f;
    else if (c.y < -1.0f)
        c.y = -1.0f;
    if (c.z > 1.0f) {
        c.z = 1.0f;
        return c;
    }
    if (c.z < -1.0f)
        c.z = -1.0f;
    else if (c.z >= minZ)
        return c;
    const float s = std::sqrt(1.0f - c.z * c.z);
    if (s <= 1e-8f) {
        c.x = xy;
        c.y = 0.0f;
    } else {
        const float k = xy / s;
        c.x *= k;
        c.y *= k;
    }
    c.z = minZ;
    return c;
}

// The ease of a head turn: (time left / length) ^ power.
float LookEase(int left, int length, float power)
{
    return static_cast<float>(std::pow(static_cast<double>(left) / static_cast<double>(length),
                                       static_cast<double>(power)));
}

float CosDegrees(int degrees) { return static_cast<float>(std::cos(degrees * static_cast<double>(0.017453292f))); }
float SinDegrees(int degrees) { return static_cast<float>(std::sin(degrees * static_cast<double>(0.017453292f))); }

}  // namespace

// 0x007acbe0 (C_HumanWork::SetPose): the head looks at `pos` (LOOK_AT; LOOK_ONCE: it turns
// back after 0.7..1.2 s) or glances round (LOOK_GLANCE; LOOK_ONCE: one wider glance;
// LOOK_GLANCE_LEFT / RIGHT, LOOK_QUICK and LOOK_NERVOUS pick the sides and times); 0 ends a
// look at.
void C_HumanWeaponPose::SetLook(uint32_t flags, const S_vector4 *pos)
{
    const S_vector headZ = m_bones[POSE_BONE_HEAD].frame->GetMatrix()(2);
    const S_matrix &model = m_model->GetMatrix();
    if (flags & LOOK_AT) {
        if (!pos) {
            m_lookFlags = 0;
            return;
        }
        m_lookAt = *pos;
        PoseSetXYZ(m_lookFrom, IntoAxes(headZ, model));
        if (flags & LOOK_ONCE)
            m_holdTime = RandomBelow(500) + 700;
        else
            m_holdTime = 0x7FFFFFFF;
        m_lookPower = 2.5f;
        m_lookLength = RandomBelow(100) + 300;
        m_lookTime = m_lookLength;
        m_lookFlags = flags;
        return;
    }
    if (flags & LOOK_GLANCE) {
        PoseSetXYZ(m_lookDir, IntoAxes(headZ, model));
        if (!(m_lookFlags & LOOK_GLANCE))
            PickGlance();
        m_lookFlags = flags;
        return;
    }
    if (flags == 0 && (m_lookFlags & LOOK_AT)) {
        // The head turns from where it looks... but the flags are put back without
        // LOOK_RETURNING (the original stores its copy of the old ones), so it turns to the
        // same point again.
        const uint32_t old = m_lookFlags;
        m_lookFlags = old | LOOK_RETURNING;
        m_lookTime = m_lookLength;
        m_lookFrom = m_lookDir;
        flags = old;
    }
    m_lookFlags = flags;
}

// 0x007ace90: the next glance: its angle to the side (alternating, or the side asked), how long
// it holds and how long until the next one.
void C_HumanWeaponPose::PickGlance()
{
    const int r = std::rand();
    uint32_t flags = m_lookFlags;
    const bool wide = (flags & LOOK_ONCE) != 0;
    int angle;
    float x;
    if (flags & LOOK_GLANCE_LEFT) {
        angle = wide ? (r >> 4) % 20 + 50 : (r >> 4) % 20 + 25;
        m_lookTo.z = CosDegrees(angle);
        x = -SinDegrees(angle);
    } else if (!(flags & LOOK_GLANCE_RIGHT)) {
        angle = wide ? (r >> 4) % 50 + 20 : (r >> 4) % 45 + 10;
        m_lookTo.z = CosDegrees(angle);
        bool left = false;
        if (!(flags & LOOK_LAST_LEFT)) {
            if (flags & LOOK_LAST_RIGHT) {
                m_lookFlags = (flags & ~LOOK_LAST_RIGHT) | LOOK_LAST_LEFT;
                left = true;
            } else if (r > 0x3FFF) {
                m_lookFlags = flags | LOOK_LAST_LEFT;
                left = true;
            }
        } else {
            flags &= ~LOOK_LAST_LEFT;
        }
        if (left) {
            x = -SinDegrees(angle);
        } else {
            m_lookFlags = flags | LOOK_LAST_RIGHT;
            x = SinDegrees(angle);
        }
    } else {
        angle = wide ? (r >> 4) % 20 + 50 : (r >> 4) % 20 + 25;
        m_lookTo.z = CosDegrees(angle);
        x = SinDegrees(angle);
    }
    m_lookTo.y = 0.0f;
    m_lookTo.x = x;
    int length;
    if (m_lookFlags & LOOK_NERVOUS) {
        m_glanceTime = RandomBelow(2000) + 1000;
        m_holdTime = RandomBelow(1000) + 500;
        length = RandomBelow(300) + 400;
    } else if (m_lookFlags & LOOK_QUICK) {
        m_glanceTime = RandomBelow(1000) + 2000;
        m_holdTime = RandomBelow(500) + 300;
        length = RandomBelow(300) + 600;
    } else {
        m_glanceTime = RandomBelow(2000) + 5000;
        m_holdTime = RandomBelow(1000) + 500;
        length = RandomBelow(500) + 1000;
    }
    m_lookLength = length;
    m_lookPower = 2.5f;
    m_lookFrom = m_lookDir;
    m_lookTime = m_lookLength;
}

// 0x007ad120: the head turns to look at the point, glances or (holding a weapon) looks along
// m_aimDir; it keeps to the front of the chest (clients only).
void C_HumanWeaponPose::UpdateHeadLook(int time)
{
    if (!IsClient())
        return;
    const uint32_t flags = m_lookFlags;
    I3D_frame *model = m_model;
    I3D_frame *chest = m_bones[POSE_BONE_CHEST].frame;
    if (!(flags & LOOK_AT)) {
        if (flags & LOOK_GLANCE) {
            const S_vector headZ = m_bones[POSE_BONE_HEAD].frame->GetMatrix()(2);
            if (flags & LOOK_RETURNING) {
                PoseSetXYZ(m_lookTo, IntoAxes(headZ, model->GetMatrix()));
                m_lookTime -= time;
                if (m_lookTime < 0) {
                    m_lookTime = 0;
                    if (m_lookFlags & LOOK_ONCE) {
                        m_lookFlags = 0;
                    } else {
                        m_glanceTime -= time;
                        if (m_glanceTime < 0) {
                            m_lookFlags &= ~LOOK_RETURNING;
                            PickGlance();
                        }
                    }
                }
            } else {
                PoseSetXYZ(m_lookFrom, IntoAxes(headZ, model->GetMatrix()));
                m_lookTime -= time;
                if (m_lookTime < 0) {
                    m_lookTime = 0;
                    m_holdTime -= time;
                    if (m_holdTime < 0) {
                        m_lookFrom = m_lookTo;
                        m_lookFlags |= LOOK_RETURNING;
                        m_lookTime = m_lookLength;
                    }
                }
            }
            const float w = LookEase(m_lookTime, m_lookLength, m_lookPower);
            m_lookWeight = w;
            PoseSetXYZ(m_lookDir, PoseSlerpDir(PoseXYZ(m_lookFrom), PoseXYZ(m_lookTo), 1.0f - w));
            const S_vector v = PoseXYZ(m_lookDir).RotateByMatrix(model->GetMatrix());
            const S_vector c = KeepForward(IntoAxes(v, chest->GetMatrix()), 0.1736f, 0.9848f);
            TurnHead(c.RotateByMatrix(chest->GetMatrix()), 1.0f);
            return;
        }
        if (!(m_flags & (POSE_FLAG_HOLD | POSE_FLAG_FREE | POSE_FLAG_STATIC_WEAPON | POSE_FLAG_SEAT_WEAPON)))
            return;
        const S_vector c = KeepForward(IntoAxes(PoseXYZ(m_aimDir), chest->GetMatrix()), 0.1736f, 0.9848f);
        TurnHead(c.RotateByMatrix(chest->GetMatrix()), m_blend);
        return;
    }

    // Looking at a point.
    if (flags & LOOK_RETURNING) {
        m_lookTime -= time;
        if (m_lookTime < 0) {
            m_lookTime = 0;
            m_lookFlags = 0;
        }
        const S_vector headZ = m_bones[POSE_BONE_HEAD].frame->GetMatrix()(2);
        PoseSetXYZ(m_lookTo, IntoAxes(headZ, model->GetMatrix()));
    } else {
        m_lookTime -= time;
        if (m_lookTime < 0) {
            m_lookTime = 0;
            m_holdTime -= time;
            if (m_holdTime < 0) {
                m_lookFlags = flags | LOOK_RETURNING;
                m_lookTime = m_lookLength;
                m_lookFrom = m_lookDir;
            }
        }
        S_vector d = PoseXYZ(m_lookAt) - m_bones[POSE_BONE_HEAD].frame->GetWorldPos();
        d.Normalize();
        PoseSetXYZ(m_lookTo, IntoAxes(d, model->GetMatrix()));
    }
    const float w = LookEase(m_lookTime, m_lookLength, m_lookPower);
    m_lookWeight = w;
    PoseSetXYZ(m_lookDir, PoseSlerpDir(PoseXYZ(m_lookTo), PoseXYZ(m_lookFrom), w));
    const S_vector v = PoseXYZ(m_lookDir).RotateByMatrix(model->GetMatrix());
    S_vector c = KeepForward(IntoAxes(v, chest->GetMatrix()), 0.707f, 0.707f);
    if (c.y < -0.342f) {
        const float s = std::sqrt(1.0f - c.y * c.y);
        if (s <= 1e-8f) {
            c.x = 0.0f;
            c.z = 0.9396f;
        } else {
            const float k = 0.9396f / s;
            c.x *= k;
            c.z *= k;
        }
        c.y = -0.342f;
    }
    TurnHead(c.RotateByMatrix(chest->GetMatrix()), 1.0f);
}

// 0x007ade70: turns the head so that its z axis points along `dir`, by `blend`.
void C_HumanWeaponPose::TurnHead(const S_vector &dir, float blend)
{
    I3D_frame *head = m_bones[POSE_BONE_HEAD].frame;
    S_matrix m;
    m.Identity();
    const S_vector headZ = head->GetMatrix()(2).Normalized();
    PoseRotationBetween(headZ, dir, m);
    m.m[3][3] = 1.0f;
    S_quat from;
    from.Make(head->GetMatrix());
    S_quat to;
    to.Make(head->GetMatrix() * m);
    m.SetRot3(from.Slerp(to, blend, false));
    m *= PoseInverseRotation(m_bones[POSE_BONE_HEAD].parent->frame->GetMatrix());
    PoseSetFrameMatrix(head, PoseWithLocalPos(m, head));
}
