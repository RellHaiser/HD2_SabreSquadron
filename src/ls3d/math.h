// LS3D engine math types.
//
// Layouts are fixed by the LS3DF.DLL ABI. Non-inline methods are exported by
// LS3DF.DLL as __stdcall member functions (mangled "QAG"/"QBG"); the inline ones
// were compiled into the game executable (original addresses noted).
#pragma once

#include <cmath>
#include <cstdint>

#ifndef LS3D_API
#define LS3D_API __declspec(dllimport)
#endif

struct S_matrix;
struct S_quat;

struct S_vector2 {
    float x, y;
};

struct S_vector {
    float x, y, z;

    S_vector() = default;
    S_vector(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    // 0x00404260
    void Set(float x_, float y_, float z_) { x = x_; y = y_; z = z_; }
    void Zero() { x = y = z = 0.0f; }

    // 0x00404280
    S_vector operator+(const S_vector &v) const { return S_vector(x + v.x, y + v.y, z + v.z); }
    S_vector operator-(const S_vector &v) const { return S_vector(x - v.x, y - v.y, z - v.z); }
    S_vector operator-() const { return S_vector(-x, -y, -z); }
    // 0x004042b0
    S_vector operator*(float s) const { return S_vector(x * s, y * s, z * s); }
    S_vector operator/(float s) const { return *this * (1.0f / s); }
    S_vector &operator+=(const S_vector &v) { x += v.x; y += v.y; z += v.z; return *this; }
    S_vector &operator-=(const S_vector &v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    S_vector &operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    bool operator==(const S_vector &v) const { return x == v.x && y == v.y && z == v.z; }
    bool operator!=(const S_vector &v) const { return !(*this == v); }

    float Dot(const S_vector &v) const { return x * v.x + y * v.y + z * v.z; }
    S_vector Cross(const S_vector &v) const {
        return S_vector(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
    }
    float Square() const { return Dot(*this); }
    // SDK inline: exact 0 and 1 for (nearly) zero and unit vectors.
    float Magnitude() const {
        const float sq = Square();
        if (sq < 1e-8f)
            return 0.0f;
        if (std::fabs(sq - 1.0f) < 1e-8f)
            return 1.0f;
        return std::sqrt(sq);
    }
    // SDK inline: a (nearly) zero vector becomes a unit vector along its first
    // non-zero axis (x, then z, then y).
    void Normalize() {
        const float sq = Square();
        if (std::fabs(sq - 1.0f) < 1e-8f)
            return;
        if (sq >= 1e-8f) {
            *this *= 1.0f / std::sqrt(sq);
        } else if (x != 0.0f) {
            x = x >= 0.0f ? 1.0f : -1.0f;
        } else if (z != 0.0f) {
            z = z >= 0.0f ? 1.0f : -1.0f;
        } else {
            y = y >= 0.0f ? 1.0f : -1.0f;
        }
    }
    S_vector Normalized() const { S_vector v(*this); v.Normalize(); return v; }

    LS3D_API float __stdcall AngleTo(const S_vector &v) const;
    LS3D_API S_vector __stdcall RotateByMatrix(const S_matrix &m) const;
    LS3D_API S_vector __stdcall RotateByNormMatrix(const S_matrix &m) const;
    LS3D_API S_vector __stdcall operator*(const S_matrix &m) const;
    LS3D_API S_vector &__stdcall operator*=(const S_matrix &m);
};

inline S_vector operator*(float s, const S_vector &v) { return v * s; }

struct S_vector4 {
    float x, y, z, w;
};

// Quaternion. In memory the vector part comes first (identity = {0, 0, 0, 1}),
// while the game's files store the scalar first: use FromFile()/ToFile() for those.
// The constructor takes the scalar first, as in w + xi + yj + zk.
struct S_quat {
    S_vector v;
    float s;

    S_quat() = default;
    S_quat(float s_, float x, float y, float z) : v(x, y, z), s(s_) {}

    // File order {s, x, y, z} (track keys, saved games).
    static S_quat FromFile(const float f[4]) { return S_quat(f[0], f[1], f[2], f[3]); }
    void ToFile(float f[4]) const { f[0] = s; f[1] = v.x; f[2] = v.y; f[3] = v.z; }

    void Identity() { s = 1.0f; v.Zero(); }

    LS3D_API void __stdcall Make(const S_matrix &m);
    LS3D_API void __stdcall Make(const S_vector &axis, float angle);
    LS3D_API void __stdcall Normalize();
    LS3D_API void __stdcall SetDir(const S_vector &dir, float roll);
    LS3D_API S_vector __stdcall GetDir() const;
    // The rotation's axis (normalized) and angle (2 acos s; 0 for s >= 1, 2 pi for s <= -1).
    // Careful: it writes 16 bytes, s into the float after `axis` (pass an S_vector4's).
    LS3D_API void __stdcall Inverse(S_vector &axis, float &angle) const;
    LS3D_API S_matrix __stdcall RotationMatrix() const;
    LS3D_API S_quat __stdcall RotateByMatrix(const S_matrix &m) const;
    LS3D_API S_quat __stdcall Slerp(const S_quat &q, float t, bool shortestPath) const;
};

// Row-major 4x4 matrix; row 3 holds the translation (Direct3D convention).
struct S_matrix {
    float m[4][4];

    S_vector &operator()(int row) { return *reinterpret_cast<S_vector *>(m[row]); }
    const S_vector &operator()(int row) const { return *reinterpret_cast<const S_vector *>(m[row]); }

    LS3D_API void __stdcall Identity();
    LS3D_API void __stdcall SetDir(const S_vector &dir);
    LS3D_API void __stdcall SetDir(const S_vector &dir, float roll);
    LS3D_API void __stdcall SetDir(const S_vector &dir, const S_vector &up);
    LS3D_API void __stdcall SetDir3(const S_vector &dir);
    LS3D_API void __stdcall SetDir3(const S_vector &dir, const S_vector &up);
    LS3D_API void __stdcall SetRot(const S_quat &q);
    LS3D_API void __stdcall SetRot3(const S_quat &q);
    LS3D_API bool __stdcall Inverse(const S_matrix &src);
    LS3D_API S_vector __stdcall GetScale() const;
    LS3D_API float __stdcall GetUScale() const;
    LS3D_API S_matrix __stdcall RotateByMatrix(const S_matrix &m) const;
    LS3D_API S_matrix __stdcall operator*(const S_matrix &m) const;
    LS3D_API S_matrix &__stdcall operator*=(const S_matrix &m);
};

// Constant identity matrix (a static of the SDK headers, 0x0080f950 in the game).
inline const S_matrix &IdentityMatrix()
{
    static const S_matrix identity = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    return identity;
}

// Axis-aligned bounding box.
struct S_bbox {
    S_vector min;
    S_vector max;
};

struct S_plane {
    S_vector normal;
    float d;

    float Distance(const S_vector &p) const { return normal.Dot(p) + d; }

    LS3D_API bool __stdcall IntersectionLine(const S_plane &other, S_vector &linePos, S_vector &lineDir) const;
};

LS3D_API S_matrix __stdcall RotateXMatrix(float angle);
LS3D_API S_matrix __stdcall RotateYMatrix(float angle);
LS3D_API S_matrix __stdcall RotateZMatrix(float angle);

// Uniform random number in [0, 1).
extern "C" LS3D_API float RandFloat();

static_assert(sizeof(S_vector) == 0x0C, "S_vector layout");
static_assert(sizeof(S_quat) == 0x10, "S_quat layout");
static_assert(sizeof(S_matrix) == 0x40, "S_matrix layout");
static_assert(sizeof(S_plane) == 0x10, "S_plane layout");
