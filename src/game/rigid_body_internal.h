// Helpers shared by the rigid body's sources (rigid_body*.cpp); not part of its interface.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "ls3d/math.h"

namespace {

// The length from a square the caller summed, as the SDK's inlined helper: 0 when its bits are
// below 1e-8's, exactly 1 for a near-unit square, the square root otherwise.
inline float LengthOf(float square)
{
    int32_t bits;
    std::memcpy(&bits, &square, sizeof bits);
    if (bits < 0x322bcc77)
        return 0.0f;
    if (!(std::fabs(square - 1.0f) < 1e-08f))
        return std::sqrt(square);
    return 1.0f;
}

// S_vector::Normalize from a square the caller summed (the last bit follows its order; each place
// sums its own way).
inline void NormalizeBy(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-08f)
        return;
    if (square >= 1e-08f) {
        const float scale = 1.0f / std::sqrt(square);
        v.x = v.x * scale;
        v.y = v.y * scale;
        v.z = v.z * scale;
    } else if (v.x != 0.0f) {
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    } else if (v.z != 0.0f) {
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    } else {
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
    }
}

// The game's own dot product (0x00454660) (DS 0x004b6550): z, then y, then x.
inline float DotZYX(const S_vector &a, const S_vector &b)
{
    return (a.z * b.z + a.y * b.y) + a.x * b.x;
}

inline S_vector4 Row(const S_matrix &m, int row)
{
    return {m.m[row][0], m.m[row][1], m.m[row][2], m.m[row][3]};
}

inline S_vector XYZ(const S_vector4 &v)
{
    return S_vector(v.x, v.y, v.z);
}

inline void SetXYZ(S_vector4 &v, const S_vector &p)
{
    v.x = p.x;
    v.y = p.y;
    v.z = p.z;
}

}  // namespace
