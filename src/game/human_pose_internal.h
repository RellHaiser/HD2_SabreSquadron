// Helpers shared by the weapon pose's source files (human_weapon_pose.cpp, human_pose_*.cpp).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/human_weapon_pose.h"
#include "ls3d/i3d.h"

// Matrix helpers of the pose (human_pose_ik.cpp).
S_matrix PoseTransposed(const S_matrix &m);                       // 0x007a25f0
// Transposed with the fourth column cleared: the inverse rotation of an orthonormal matrix.
S_matrix PoseInverseRotation(const S_matrix &m);
S_matrix PoseTurnedAround(const S_matrix &m);                     // 0x007a2650
void PoseOrthonormalize(S_matrix &m);                             // 0x007a2770
void PoseAimZ(S_matrix &m, const S_vector &dir);                  // 0x007a2850
S_matrix PoseFrameFromZ(const S_vector &dir);                     // 0x007a3270
S_matrix PoseFrameFromZ(const S_vector &dir, float roll);         // 0x007a3720
S_matrix PoseFrameFromZX(const S_vector &dir, float roll);        // 0x007a3960
void PoseRotationBetween(const S_vector &from, const S_vector &to, S_matrix &out);   // 0x007a42c0
void PoseTurnZTo(S_matrix &m, const S_vector &to);                // 0x007a3330
void PoseTurnYTo(S_matrix &m, const S_vector &to);                // 0x007a3370
float PoseGetRoll(const S_matrix &m);                             // 0x007a36c0
S_vector PoseSlerpDir(const S_vector &a, const S_vector &b, float t);   // 0x007a45a0

inline S_vector PoseXYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }
inline void PoseSetXYZ(S_vector4 &v, const S_vector &p)
{
    v.x = p.x;
    v.y = p.y;
    v.z = p.z;
}
inline S_vector4 PoseVector4(const S_vector &p, float w) { return {p.x, p.y, p.z, w}; }

// The world position row of a frame with its w (the original copies whole matrix rows).
inline S_vector4 PoseWorldPos4(I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
}

inline S_vector4 PoseRow4(const S_matrix &m, int row)
{
    return {m.m[row][0], m.m[row][1], m.m[row][2], m.m[row][3]};
}

// A value set to the quiet NaN 0x7FC00000 (the original marks unset values so and tests the
// bits, -NaN 0xFFC00000 included).
inline bool PoseIsUnset(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits == 0x7FC00000u || bits == 0xFFC00000u;
}
inline float PoseUnset()
{
    const uint32_t bits = 0x7FC00000u;
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The original's sign tests of float bits (-0 counts as negative).
inline bool PoseIsNegative(float value) { return std::signbit(value); }

// The direction of `v` in the axes of `m` (dotted with its rows).
inline S_vector PoseIntoAxes(const S_matrix &m, const S_vector &v)
{
    return S_vector(v.Dot(m(0)), v.Dot(m(1)), v.Dot(m(2)));
}

// The SDK's magnitude test as the original inlines it: 0 below 1e-8, exactly 1 for unit lengths.
inline float PoseMagnitude(float squared)
{
    if (squared < 1e-8f)
        return 0.0f;
    if (std::fabs(squared - 1.0f) < 1e-8f)
        return 1.0f;
    return std::sqrt(squared);
}

// The fast inverse square root the ragdoll uses (0x5F3759DF and one Newton step).
inline float PoseFastInvSqrt(float value)
{
    int32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    bits = 0x5F3759DF - (bits >> 1);
    float y;
    std::memcpy(&y, &bits, sizeof(y));
    return (1.5f - y * y * value * 0.5f) * y;
}

// The local matrices of these bones are kept when the ragdoll is built (C_HumanWeaponPose::
// m_restMatrices, in this order) and put back when the body falls.
constexpr int RAGDOLL_REST_BONE_COUNT = 14;
extern const int g_ragdollRestBones[RAGDOLL_REST_BONE_COUNT];

// `rotation` with the local position (row 3, w included) of `frame`.
inline S_matrix PoseWithLocalPos(const S_matrix &rotation, I3D_frame *frame)
{
    S_matrix m = rotation;
    const S_vector &pos = frame->GetPos();
    m.m[3][0] = pos.x;
    m.m[3][1] = pos.y;
    m.m[3][2] = pos.z;
    m.m[3][3] = frame->GetPosW();
    return m;
}

// Replaces the local matrix of `frame` and lets the engine propagate it.
inline void PoseSetFrameMatrix(I3D_frame *frame, const S_matrix &m)
{
    frame->SetLocalMatrix(m);
    frame->Update();
}

// Turns `frame` by the world rotation `turn` (3x3, rows), keeping its local position: the
// world matrix times `turn`, back into its parent's axes. Every 4th update the result is
// orthonormalised (`orthonormalize`).
inline void PoseTurnFrame(I3D_frame *frame, I3D_frame *parent, const S_matrix &turn, bool orthonormalize)
{
    S_matrix m = frame->GetMatrix() * turn;
    m *= PoseInverseRotation(parent->GetMatrix());
    if (orthonormalize)
        PoseOrthonormalize(m);
    const S_vector &pos = frame->GetPos();
    m.m[3][0] = pos.x;
    m.m[3][1] = pos.y;
    m.m[3][2] = pos.z;
    m.m[3][3] = 1.0f;
    PoseSetFrameMatrix(frame, m);
}
