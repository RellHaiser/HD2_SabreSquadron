// Runs the ported C_Vehicle::Setup (src/game/vehicle_setup.cpp) on the shipped car models with a
// stub LS3D engine: frames with fake COM-style vtables built from export_cars.py's files, the
// matrix math (as LS3DF.dll computes it), the driver's hierarchy box. Prints what Setup found per
// model, then Reset, Start, a drive test (Step through the real solver, wheel contacts and body
// collision over a flat ground the stub CollisionRayTest / CollisionSphereTest / TestBoxSweep give;
// DRIVE_TRACE=<model> traces one, DRIVE_EVERY=1 every step, DRIVE_SETTLE=<model> its settling,
// DRIVE_DEBUG=1 the traced model's forces over the first steps, DRIVE_F6C=<hex> other simulation
// options (the ground modes), DRIVE_SYMMETRIC=1 the centre of mass centred across), a crash test
// (full throttle at a wall 15 m ahead; CRASH_TRACE=<model> traces one) and Stop.
//
//   harness.exe MODEL.txt [...]
//
// The tests' strength: mutfast.py runs the mutation lists in mutations/ (each mutation of a ported
// file should make check.py fail).
//
// Not an equivalence test (the original cannot run here): it checks that the port runs on real
// models and finds what their frame names say (export_cars.py / check.py compare the counts).
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#define private public
#define protected public
#include "game/vehicle.h"
#include "game/car.h"
#include "common/chunk.h"
#include "game/human.h"
#include "game/seat_weapon.h"
#include "script/script.h"
#include "game/actors_module.h"
#include "game/checkpoints.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/profile.h"
#include "game/sound_module.h"
#include "net/bitstream.h"
#include "game/car_effects.h"
#include "ls3d/i3d.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/game.h"
#include "game/physics.h"
#include "game/shots.h"
#include "game/versions.h"
#include "game/world.h"
#include "game/team.h"
#include "game/particles.h"
#include "ls3d/igraph.h"
#undef private
#undef protected

// --- LS3D math ---------------------------------------------------------------------------------

S_vector S_vector::operator*(const S_matrix &m) const
{
    return S_vector(x * m.m[0][0] + y * m.m[1][0] + z * m.m[2][0] + m.m[3][0],
                    x * m.m[0][1] + y * m.m[1][1] + z * m.m[2][1] + m.m[3][1],
                    x * m.m[0][2] + y * m.m[1][2] + z * m.m[2][2] + m.m[3][2]);
}

S_vector &S_vector::operator*=(const S_matrix &m)
{
    *this = *this * m;
    return *this;
}

void S_matrix::Identity()
{
    std::memset(m, 0, sizeof(m));
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}

bool S_matrix::Inverse(const S_matrix &s)
{
    // Affine: the 3x3 part's inverse, then the translation.
    const float a = s.m[0][0], b = s.m[0][1], c = s.m[0][2];
    const float d = s.m[1][0], e = s.m[1][1], f = s.m[1][2];
    const float g = s.m[2][0], h = s.m[2][1], i = s.m[2][2];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-12f)
        return false;
    const float k = 1.0f / det;
    S_matrix r;
    r.m[0][0] = (e * i - f * h) * k;
    r.m[0][1] = (c * h - b * i) * k;
    r.m[0][2] = (b * f - c * e) * k;
    r.m[1][0] = (f * g - d * i) * k;
    r.m[1][1] = (a * i - c * g) * k;
    r.m[1][2] = (c * d - a * f) * k;
    r.m[2][0] = (d * h - e * g) * k;
    r.m[2][1] = (b * g - a * h) * k;
    r.m[2][2] = (a * e - b * d) * k;
    r.m[0][3] = r.m[1][3] = r.m[2][3] = 0.0f;
    const S_vector t(s.m[3][0], s.m[3][1], s.m[3][2]);
    for (int col = 0; col < 3; ++col)
        r.m[3][col] = -(t.x * r.m[0][col] + t.y * r.m[1][col] + t.z * r.m[2][col]);
    r.m[3][3] = 1.0f;
    *this = r;
    return true;
}

S_vector S_matrix::GetScale() const
{
    return S_vector((*this)(0).Magnitude(), (*this)(1).Magnitude(), (*this)(2).Magnitude());
}

// A quaternion's rotation rows as LS3D's S_quat::RotationMatrix / S_matrix::SetRot3 compute them
// (LS3DF.DLL 0x1002ee70 / 0x1002d450): the identity for |s| >= 1.
static void QuatRows(const S_quat &q, float r[3][3])
{
    if (!(std::fabs(q.s) < 1.0f)) {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r[i][j] = i == j ? 1.0f : 0.0f;
        return;
    }
    const float x2 = q.v.x + q.v.x, y2 = q.v.y + q.v.y, z2 = q.v.z + q.v.z;
    const float xx = x2 * q.v.x, xy = y2 * q.v.x, xz = z2 * q.v.x;
    const float yy = y2 * q.v.y, yz = z2 * q.v.y, zz = z2 * q.v.z;
    const float sx = x2 * q.s, sy = y2 * q.s, sz = z2 * q.s;
    r[0][0] = 1.0f - (zz + yy); r[0][1] = xy - sz;          r[0][2] = sy + xz;
    r[1][0] = sz + xy;          r[1][1] = 1.0f - (zz + xx); r[1][2] = yz - sx;
    r[2][0] = xz - sy;          r[2][1] = sx + yz;          r[2][2] = 1.0f - (yy + xx);
}

void S_matrix::SetRot3(const S_quat &q)
{
    float r[3][3];
    QuatRows(q, r);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[i][j] = r[i][j];
}

S_matrix S_quat::RotationMatrix() const
{
    S_matrix out;
    out.Identity();
    float r[3][3];
    QuatRows(*this, r);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out.m[i][j] = r[i][j];
    return out;
}

// The inverse of QuatRows for an unscaled rotation (LS3D's own also handles scaled rows).
void S_quat::Make(const S_matrix &mat)
{
    S_matrix r = mat;
    for (int row = 0; row < 3; ++row)
        r(row).Normalize();
    const float tr = r.m[0][0] + r.m[1][1] + r.m[2][2];
    if (tr > 0.0f) {
        const float t = std::sqrt(tr + 1.0f) * 2.0f;
        s = 0.25f * t;
        v.x = (r.m[2][1] - r.m[1][2]) / t;
        v.y = (r.m[0][2] - r.m[2][0]) / t;
        v.z = (r.m[1][0] - r.m[0][1]) / t;
    } else {
        s = 1.0f;
        v.Zero();
    }
}

// LS3D's product is affine (0x1002d830): the result's column 3 is (0, 0, 0, 1).
S_matrix S_matrix::operator*(const S_matrix &b) const
{
    S_matrix out;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            out.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j];
        out.m[i][3] = 0.0f;
    }
    for (int j = 0; j < 3; ++j)
        out.m[3][j] = m[3][0] * b.m[0][j] + b.m[3][j] + m[3][1] * b.m[1][j] + m[3][2] * b.m[2][j];
    out.m[3][3] = 1.0f;
    return out;
}

// LS3D's (0x1002e8c0): 0 for (nearly) zero vectors, acos of the clamped cosine.
float S_vector::AngleTo(const S_vector &b) const
{
    const float lengths = b.Magnitude() * Magnitude();
    if (lengths < 1e-8f)
        return 0.0f;
    const float c = (x * b.x + z * b.z + y * b.y) / lengths;
    if (c <= -1.0f)
        return std::acos(-1.0f);
    if (c >= 1.0f)
        return 0.0f;
    return std::acos(c);
}

void S_quat::Normalize()
{
    const float l = std::sqrt(v.Square() + s * s);
    if (l > 0.0f) {
        v *= 1.0f / l;
        s /= l;
    }
}

// LS3D's (0x1002f070): then normalized (the identity when nearly zero).
void S_quat::Make(const S_vector &axis, float angle)
{
    const float half = angle * 0.5f;
    s = std::cos(half);
    v = axis * std::sin(half);
    const float sq = s * s + v.z * v.z + v.x * v.x + v.y * v.y;
    if (std::fabs(sq - 1.0f) < 1e-8f)
        return;
    if (sq < 1e-8f) {
        Identity();
        return;
    }
    const float k = 1.0f / std::sqrt(sq);
    v *= k;
    s *= k;
}

// LS3DF's (0x10030160): the four floats copied (s lands after the axis: the caller's S_vector4),
// the axis normalized (squares x, y, z), the angle 2 acos s (0 at 1 or more, 2 pi at -1 or less).
void S_quat::Inverse(S_vector &axis, float &angle) const
{
    reinterpret_cast<S_vector4 &>(axis) = {v.x, v.y, v.z, s};
    const float sq = (axis.x * axis.x + axis.y * axis.y) + axis.z * axis.z;
    if (std::fabs(sq - 1.0f) >= 1e-8f) {
        if (sq >= 1e-8f) {
            const float k = 1.0f / std::sqrt(sq);
            axis.x *= k;
            axis.y *= k;
            axis.z *= k;
        } else if (axis.x != 0.0f) {
            axis.x = axis.x < 0.0f ? -1.0f : 1.0f;
        } else if (axis.z != 0.0f) {
            axis.z = axis.z < 0.0f ? -1.0f : 1.0f;
        } else {
            axis.y = axis.y < 0.0f ? -1.0f : 1.0f;
        }
    }
    angle = s >= 1.0f ? 0.0f : s <= -1.0f ? 6.2831855f : std::acos(s) * 2.0f;
}

// The rotation rows looking along `dir` with `up` (row 3 kept).
void S_matrix::SetDir3(const S_vector &dir, const S_vector &up)
{
    const S_vector z = dir.Normalized();
    const S_vector x = up.Cross(z).Normalized();
    const S_vector y = z.Cross(x);
    const S_vector rows[3] = {x, y, z};
    for (int r = 0; r < 3; ++r) {
        m[r][0] = rows[r].x;
        m[r][1] = rows[r].y;
        m[r][2] = rows[r].z;
        m[r][3] = 0.0f;
    }
}

// LS3D's (0x100306c0; ../LS3DF's port): the line where two planes meet (a point with one coordinate
// 0, the unit direction); false when they are parallel.
bool S_plane::IntersectionLine(const S_plane &o, S_vector &pos, S_vector &dir) const
{
    const S_vector &a = normal, &b = o.normal;
    dir = S_vector(a.y * b.z - b.y * a.z, a.z * b.x - a.x * b.z, b.y * a.x - a.y * b.x);
    const float xx = dir.x * dir.x, yy = dir.y * dir.y, zz = dir.z * dir.z;
    float f;
    if (zz > yy && zz > xx && zz > 1e-8f) {
        f = dir.z;
        pos.x = a.y * o.d - b.y * d;
        pos.y = b.x * d - a.x * o.d;
        pos.z = 0.0f;
    } else if (yy > xx && yy > 1e-8f) {
        f = dir.y;
        pos.y = 0.0f;
        pos.x = b.z * d - a.z * o.d;
        pos.z = a.x * o.d - b.x * d;
    } else if (xx > 1e-8f) {
        f = dir.x;
        pos.x = 0.0f;
        pos.y = a.z * o.d - b.z * d;
        pos.z = b.y * d - a.y * o.d;
    } else {
        return false;
    }
    const float inv = 1.0f / f;
    pos.x = inv * pos.x;
    pos.y = inv * pos.y;
    pos.z = inv * pos.z;
    const float n = 1.0f / std::sqrt((xx + yy) + zz);
    dir.x = n * dir.x;
    dir.y = n * dir.y;
    dir.z = n * dir.z;
    return true;
}

// --- the stub engine ---------------------------------------------------------------------------

void __stdcall F_SetPosRot(I3D_frame *self, const S_vector &pos, const S_quat &rot);   // (the parts' tests)

namespace {

constexpr size_t FRAME_SIZE = 0x300;
constexpr int SLOTS = 64;

struct S_Mesh;
struct S_StubFrame {
    I3D_frame *frame;
    std::string name;
    std::string props;
    std::vector<float> vertices;                        // 8 floats per vertex (position first)
    std::vector<std::vector<float>> lods;               // the further detail levels' vertices
    S_Mesh *mesh = nullptr;
    bool removed = false;
};

std::map<const I3D_frame *, S_StubFrame *> g_stubs;
I3D_frame *g_model;
std::string g_modelFile;

S_StubFrame &Stub(const I3D_frame *f)
{
    return *g_stubs.at(f);
}

bool Match(const char *mask, const char *name)
{
    if (!*mask)
        return !*name;
    if (*mask == '*')
        return Match(mask + 1, name) || (*name && Match(mask, name + 1));
    return *name && std::toupper(static_cast<unsigned char>(*mask)) == std::toupper(static_cast<unsigned char>(*name)) &&
           Match(mask + 1, name + 1);
}

bool Matches(I3D_frame *f, uint32_t typeMask, const char *mask)
{
    if (!(typeMask & (1u << (f->GetType() - 1))))
        return false;
    if (!mask)
        return true;
    const char *shortName = f->GetShortName();
    if (typeMask & ENUMF_WILDMASK)
        return Match(mask, shortName);
    // An exact name (case-insensitive, an assumption about the engine).
    size_t i = 0;
    for (; mask[i] && shortName[i]; ++i) {
        if (std::toupper(static_cast<unsigned char>(mask[i])) != std::toupper(static_cast<unsigned char>(shortName[i])))
            return false;
    }
    return !mask[i] && !shortName[i];
}

template <int N> void __stdcall Trap()
{
    std::fprintf(stderr, "unexpected engine call: slot 0x%X\n", N * 4);
    std::exit(3);
}

template <int... N> void FillTraps(void **vt, std::integer_sequence<int, N...>)
{
    ((vt[N] = reinterpret_cast<void *>(&Trap<N>)), ...);
}

uint32_t __stdcall F_Release(I3D_frame *self)
{
    return static_cast<uint32_t>(--self->m_refCount);
}

// Update calls per frame while g_countUpdates (the car's callbacks; counting every call slows the
// simulation tests down).
bool g_countUpdates;
std::map<I3D_frame *, int> g_frameUpdates;
void __stdcall F_Update(I3D_frame *self)
{
    if (g_countUpdates)
        ++g_frameUpdates[self];
}

void Unlink(I3D_frame *f)
{
    I3D_frame *parent = f->m_parent;
    if (!parent)
        return;
    I3D_frame **link = &parent->m_firstChild;
    while (*link && *link != f)
        link = &(*link)->m_nextSibling;
    if (*link)
        *link = f->m_nextSibling;
    f->m_parent = nullptr;
    f->m_nextSibling = nullptr;
}

int g_unlinked;                                         // LinkTo(null) calls
std::map<const I3D_frame *, int> g_unlinks;              // ... per frame
std::map<const I3D_frame *, uint32_t> g_linkFlags;      // the last LinkTo's flags per frame

void LinkUnder(I3D_frame *f, I3D_frame *parent)
{
    f->m_parent = parent;
    I3D_frame **link = &parent->m_firstChild;
    while (*link)
        link = &(*link)->m_nextSibling;
    *link = f;
}

void __stdcall F_LinkTo(I3D_frame *self, I3D_frame *parent, uint32_t flags)
{
    g_linkFlags[self] = flags;
    Unlink(self);
    if (parent) {
        LinkUnder(self, parent);
    } else {
        ++g_unlinked;
        ++g_unlinks[self];
    }
    self->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
}

I3D_frame *__stdcall F_GetChild(I3D_frame *self, int index)
{
    I3D_frame *c = self->m_firstChild;
    while (c && index-- > 0)
        c = c->m_nextSibling;
    return c;
}

// As LS3DF's EnumFramesProc: a callback's 2 ends the enumeration, 1 skips the frame's children.
bool Enum(I3D_frame *f, I3D_ENUMPROC *callback, void *context, uint32_t typeMask, const char *mask)
{
    for (I3D_frame *c = f->m_firstChild; c;) {
        I3D_frame *next = c->m_nextSibling;            // the callback may not unlink, but be safe
        uint32_t result = 0;
        if (Matches(c, typeMask, mask))
            result = callback(c, context);
        if (result == 2)
            return false;
        if (result != 1 && !Enum(c, callback, context, typeMask, mask))
            return false;
        c = next;
    }
    return true;
}

void __stdcall F_EnumFrames(I3D_frame *self, I3D_ENUMPROC *callback, void *context, uint32_t typeMask,
                            const char *mask)
{
    Enum(self, callback, context, typeMask, mask);
}

I3D_frame *Find(I3D_frame *f, const char *mask, uint32_t typeMask)
{
    for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling) {
        if (Matches(c, typeMask, mask))
            return c;
        if (I3D_frame *found = Find(c, mask, typeMask))
            return found;
    }
    return nullptr;
}

I3D_frame *__stdcall F_FindChildFrame(I3D_frame *self, const char *name, uint32_t typeMask)
{
    return Find(self, name, typeMask);
}

void __stdcall F_RemoveModelFrame(I3D_frame *self, I3D_frame *frame)
{
    if (self != g_model) {
        std::fprintf(stderr, "RemoveModelFrame on a non-model\n");
        std::exit(3);
    }
    Stub(frame).removed = true;
}

int g_duplicatedMeshes;
void __stdcall F_DuplicateMesh(I3D_frame *)
{
    ++g_duplicatedMeshes;
}

std::map<const I3D_frame *, S_vector> g_dirs;          // the last SetWorldDir per frame

void __stdcall F_SetWorldDir(I3D_frame *self, const S_vector &dir, float)
{
    g_dirs[self] = dir;
}

std::map<const I3D_frame *, int> g_frameTicks;            // the last Tick per frame

void __stdcall F_Tick(I3D_frame *self, int time)
{
    g_frameTicks[self] = time;
}

void __stdcall F_SetPos(I3D_frame *self, const S_vector &pos)
{
    self->m_localMatrix(3) = pos;
}

void __stdcall F_SetOn(I3D_frame *self, bool on)
{
    self->m_frameFlags = on ? self->m_frameFlags | 1u : self->m_frameFlags & ~1u;
}

struct S_Mesh;
void __stdcall F_SetMesh(I3D_frame *self, S_Mesh *mesh, int unknown);

void *g_frameVtable[SLOTS];

// Meshes: I3D_mesh (slot 1 GetLevel, slot 2 SetLevel, slot 3 Duplicate: a deep copy, +0x44 the
// level count) -> I3D_mesh_level (slot 1 GetVertices, slot 2 UnlockVertices, +0x54 the vertex
// count). Materials keep their flags at +0x08 and texture at +0x0C (slot 7 SetTexture: the new one
// referenced, the old one released); textures their count at +0x14.
struct S_Image;
struct S_Material {
    void **vtable;
    int refs = 1;
    uint32_t flags = 0;                                 // +0x08
    S_Image *texture = nullptr;                         // +0x0C
    const S_Material *copiedFrom = nullptr;
};
struct S_FaceGroup {
    void **vtable;
    S_Material *material;                               // +0x04
};
struct S_Level {
    void **vtable;
    uint8_t pad[0x50];
    int count;                                          // +0x54
    float *data;
    int locks;
    S_FaceGroup faces;
    std::vector<float> own;                             // a duplicate's vertices
};
struct S_Mesh {
    void **vtable;
    uint8_t pad[0x40];
    uint32_t levelCount;                                // +0x44
    std::vector<S_Level *> levels;
    int refs;
    bool created;
    int levelsDropped;                                  // SetLevel(null, n) calls
};
static_assert(offsetof(S_Level, count) == 0x54, "level layout");
static_assert(offsetof(S_FaceGroup, material) == 4, "face group layout");
static_assert(offsetof(S_Material, texture) == 0x0C, "material layout");
static_assert(offsetof(S_Mesh, levelCount) == 0x44, "mesh layout");

void *g_materialVtable[SLOTS];
void *g_faceVtable[SLOTS];
S_Material g_modelMaterial;                             // every model face group's material
int g_materialsMade, g_materialsReleased, g_meshes, g_meshesReleased;

uint32_t __stdcall F_MaterialRelease(S_Material *self)
{
    ++g_materialsReleased;
    return static_cast<uint32_t>(--self->refs);
}
void __stdcall F_MaterialCopyFrom(S_Material *self, const S_Material *source)
{
    self->copiedFrom = source;
}
void __stdcall F_MaterialSetTexture(S_Material *self, S_Image *texture);
void __stdcall F_SetMaterial(S_FaceGroup *self, S_Material *material)
{
    ++material->refs;
    self->material = material;
}
S_FaceGroup *__stdcall F_GetFaceGroup(S_Level *self, int index)
{
    if (index != 0)
        std::exit(4);
    return &self->faces;
}

S_Level *__stdcall F_GetLevel(S_Mesh *self, int index)
{
    if (index < 0 || static_cast<uint32_t>(index) >= self->levelCount)
        return nullptr;
    return self->levels[static_cast<size_t>(index)];
}
void __stdcall F_MeshSetLevel(S_Mesh *self, S_Level *level, int index)
{
    if (level || index < 0)
        std::exit(4);
    ++self->levelsDropped;
    if (static_cast<uint32_t>(index) < self->levelCount)
        self->levelCount = static_cast<uint32_t>(index);
}
void *__stdcall F_GetVertices(S_Level *self, int flags)
{
    if (flags != 0)
        std::exit(4);
    ++self->locks;
    return self->data;
}
void __stdcall F_UnlockVertices(S_Level *self)
{
    --self->locks;
}
void *g_meshVtable[SLOTS];
void *g_levelVtable[SLOTS];
int g_meshRefreshes;                                    // Unknown2C + Unknown14 + UnknownAC calls
void __stdcall F_LevelUnknown2C(S_Level *) { ++g_meshRefreshes; }
void __stdcall F_MeshUnknown14(S_Mesh *) { ++g_meshRefreshes; }
void __stdcall F_VisualUnknownAC(I3D_frame *, int) { ++g_meshRefreshes; }
uint32_t __stdcall F_GetVisualType(I3D_frame *) { return I3DVISUAL_OBJECT; }   // (the loaded visuals are objects)

uint32_t __stdcall F_MeshRelease(S_Mesh *self)
{
    ++g_meshesReleased;
    return static_cast<uint32_t>(--self->refs);
}
S_Level *NewLevel()
{
    auto *level = new S_Level();
    level->vtable = g_levelVtable;
    level->faces.vtable = g_faceVtable;
    level->faces.material = &g_modelMaterial;
    return level;
}
HRESULT __stdcall F_MeshDuplicate(S_Mesh *self, const S_Mesh *source)
{
    if (!source)
        return 1;
    self->levels.clear();
    for (uint32_t i = 0; i < source->levelCount; ++i) {
        const S_Level *from = source->levels[i];
        S_Level *level = NewLevel();
        level->own.assign(from->data, from->data + from->count * 8);
        level->data = level->own.data();
        level->count = from->count;
        level->faces.material = from->faces.material;
        self->levels.push_back(level);
    }
    self->levelCount = source->levelCount;
    return 0;
}
S_Mesh *NewMesh()
{
    auto *mesh = new S_Mesh();
    mesh->vtable = g_meshVtable;
    mesh->levels.push_back(NewLevel());
    mesh->levelCount = 1;
    mesh->refs = 1;
    return mesh;
}
std::map<const I3D_frame *, int> g_meshFlags;          // SetMesh's flag per frame
void __stdcall F_SetMesh(I3D_frame *self, S_Mesh *mesh, int flag)
{
    g_meshFlags[self] = flag;
    ++mesh->refs;
    *reinterpret_cast<S_Mesh **>(reinterpret_cast<uint8_t *>(self) + 0x1E0) = mesh;
}

// Frames the vehicle makes: projector visuals and lights (FRAME_SIZE blocks, their own vtables).
void *g_projectorVtable[SLOTS];
void *g_lightVtable[SLOTS];
struct S_Made {
    I3D_frame *frame;
    bool projector;
    int textures = 0;
    int lightType = 0;
    const void *lastTexture = nullptr;                  // the last SetTexture's
};
std::vector<S_Made> g_made;
void __stdcall F_ProjSetTexture(I3D_frame *self, void *texture)
{
    for (S_Made &m : g_made)
        if (m.frame == self) {
            ++m.textures;
            m.lastTexture = texture;
        }
}
void __stdcall F_ProjInt(I3D_frame *, int) {}
void __stdcall F_ProjColor(I3D_frame *, const S_vector &) {}
void __stdcall F_LightType(I3D_frame *self, int type)
{
    for (S_Made &m : g_made)
        if (m.frame == self)
            m.lightType = type;
}
void __stdcall F_LightFloat(I3D_frame *, float) {}
void __stdcall F_LightMode(I3D_frame *, uint32_t) {}
void __stdcall F_LightFloat2(I3D_frame *, float, float) {}
void __stdcall F_LightFloat3(I3D_frame *, float, float, float) {}
I3D_frame *MakeFrame(void **vtable, E_I3D_FRAME_TYPE type, bool projector)
{
    auto *mem = static_cast<uint8_t *>(::operator new(FRAME_SIZE, std::align_val_t(16)));
    std::memset(mem, 0, FRAME_SIZE);
    *reinterpret_cast<void ***>(mem) = vtable;
    auto *f = reinterpret_cast<I3D_frame *>(mem);
    f->m_type = type;
    f->m_refCount = 1;
    f->m_frameFlags = FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID | FRMFLAGS_ROT_DIRTY | 1u;
    f->m_localMatrix.Identity();
    f->m_scale = S_vector(1.0f, 1.0f, 1.0f);
    f->m_rot.Identity();
    g_made.push_back({f, projector});
    return f;
}
I3D_frame *MakeEmitter();
I3D_frame *NewFrame(E_I3D_FRAME_TYPE type, const std::string &name, I3D_frame *parent);
I3D_frame *__stdcall F_CreateVisual(void *, uint32_t type)
{
    if (type == I3DVISUAL_EMITTER)                      // the car effects' pool (not the vehicle's)
        return MakeEmitter();
    if (type == I3DVISUAL_OBJECT)                       // a copy (the car's placed charge)
        return NewFrame(FRAME_VISUAL, "visual copy", nullptr);
    if (type != I3DVISUAL_PROJECTOR)
        std::exit(5);
    return MakeFrame(g_projectorVtable, FRAME_VISUAL, true);
}
// Sound frames (LoadSound's and the sound cache's copies): the frame slots plus the sound's. Open
// keeps the file's own name (after the last '\') as the source's name GetFileName reads, and the
// flags; a name with "missing" in it is not found.
void *g_soundVtable[SLOTS];
struct S_FakeSound {
    std::string file;                                   // the last Open's
    int opens = 0, type = 0, unknown88 = -1, volumes = 0;
    float range[4] = {}, cone[2] = {}, volume = 0.0f;
    bool loop = false;
    // Playing while on and not `ended`; GetLength / GetPosition give `length` / `position`.
    bool ended = false;
    int length = 0, position = 0, ons = 0, frequencies = 0, plays = 0, playAt = -1;
    // PlantCountingSounds' fakes: never playing, each start counted into `starts`.
    bool oneShot = false;
    int *starts = nullptr;
    float frequency = 0.0f;
    std::vector<uint8_t> name;                          // the C_str block: refs, length, the chars
    uint8_t source[16] = {};                            // +0xC: the name block
};
std::map<const I3D_frame *, S_FakeSound> g_sounds;
std::vector<const I3D_frame *> g_soundSetups;           // the sounds SetVolume reached, in turn
HRESULT __stdcall F_SoundOpen(I3D_frame *self, const char *fileName, uint32_t flags)
{
    S_FakeSound &s = g_sounds[self];
    ++s.opens;
    if (std::strstr(fileName, "missing"))
        return I3DERR_NOFILE;
    const char *slash = std::strrchr(fileName, '\\');
    s.file = slash ? slash + 1 : fileName;
    s.name.assign(8 + s.file.size() + 1, 0);
    const int refs = 1, length = static_cast<int>(s.file.size());
    std::memcpy(s.name.data(), &refs, 4);
    std::memcpy(s.name.data() + 4, &length, 4);
    std::memcpy(s.name.data() + 8, s.file.c_str(), s.file.size() + 1);
    uint8_t *block = s.name.data(), *source = s.source;
    std::memcpy(s.source + 0xC, &block, sizeof block);
    auto *base = reinterpret_cast<uint8_t *>(self);
    std::memcpy(base + 0x140, &source, sizeof source);
    std::memcpy(base + 0x178, &flags, 4);
    return I3D_OK;
}
void __stdcall F_SoundType(I3D_frame *self, int type)
{
    g_sounds[self].type = type;
}
void __stdcall F_SoundRange(I3D_frame *self, float a, float b, float c, float d)
{
    float *r = g_sounds[self].range;
    r[0] = a, r[1] = b, r[2] = c, r[3] = d;
}
void __stdcall F_SoundCone(I3D_frame *self, float inner, float outer)
{
    g_sounds[self].cone[0] = inner, g_sounds[self].cone[1] = outer;
}
void __stdcall F_Sound88(I3D_frame *self, int value)
{
    g_sounds[self].unknown88 = value;
}
// The sounds' calls in turn: 'O' / 'o' on / off, 'V' volume, 'F' frequency, 'P' play, 'S' stopped
// (SetActive(false, true)), 'X' another SetActive.
std::vector<std::pair<const I3D_frame *, char>> g_soundLog;
void __stdcall F_SoundVolume(I3D_frame *self, float volume)
{
    g_sounds[self].volume = volume;
    ++g_sounds[self].volumes;
    std::memcpy(reinterpret_cast<uint8_t *>(self) + 0x16C, &volume, 4);   // (GetVolumeInline)
    g_soundSetups.push_back(self);
    g_soundLog.push_back({self, 'V'});
}
void __stdcall F_SoundLoop(I3D_frame *self, bool loop)
{
    g_sounds[self].loop = loop;
}
void __stdcall F_SoundSetOn(I3D_frame *self, bool on)
{
    F_SetOn(self, on);
    g_sounds[self].ons += on;
    if (on && g_sounds[self].starts)
        ++*g_sounds[self].starts;
    g_soundLog.push_back({self, on ? 'O' : 'o'});
}
bool __stdcall F_SoundPlaying(I3D_frame *self, int)
{
    return self->IsOn() && !g_sounds[self].ended && !g_sounds[self].oneShot;
}
void __stdcall F_SoundPlay(I3D_frame *self, int position)
{
    g_sounds[self].playAt = position;
    ++g_sounds[self].plays;
    g_soundLog.push_back({self, 'P'});
}
void __stdcall F_SoundFrequency(I3D_frame *self, float ratio)
{
    g_sounds[self].frequency = ratio;
    ++g_sounds[self].frequencies;
    g_soundLog.push_back({self, 'F'});
}
int __stdcall F_SoundPosition(I3D_frame *self)
{
    return g_sounds[self].position;
}
int __stdcall F_SoundLength(I3D_frame *self)
{
    return g_sounds[self].length;
}
void __stdcall F_SoundActive(I3D_frame *self, bool on, bool immediate)
{
    g_soundLog.push_back({self, !on && immediate ? 'S' : 'X'});
}
I3D_frame *MakeSound()
{
    I3D_frame *f = MakeFrame(g_soundVtable, FRAME_SOUND, false);
    g_made.pop_back();                                  // (not one of the lamps' objects)
    g_sounds[f];
    return f;
}
// The gear sounds started so far (the drive's count).
int GearSoundStarts(const C_Vehicle *v)
{
    int n = 0;
    for (const I3D_sound *s : {v->m_sounds1[0x17], v->m_sounds1[0x18], v->m_sounds2[0x18], v->m_sounds2[0x19]})
        n += s ? g_sounds[s].ons : 0;
    return n;
}
int LoadedSounds(const C_Vehicle *v)
{
    int n = 0;
    for (const I3D_sound *s : v->m_sounds1)
        n += s != nullptr;
    for (const I3D_sound *s : v->m_sounds2)
        n += s != nullptr;
    return n;
}
// Sound sectors (C_Vehicle::s_soundSectors): AddSound / RemoveSound recorded in turn.
void *g_soundSectorVtable[SLOTS];
struct S_SectorCall {
    const void *sector;
    const void *sound;
    bool add;
};
std::vector<S_SectorCall> g_sectorCalls;
void __stdcall F_SectorAddSound(I3D_frame *self, I3D_sound *sound)
{
    g_sectorCalls.push_back({self, sound, true});
}
void __stdcall F_SectorRemoveSound(I3D_frame *self, I3D_sound *sound)
{
    g_sectorCalls.push_back({self, sound, false});
}

std::vector<I3D_frame *> g_createdModels;             // the models CreateFrame made (the last: the car's wreck)
I3D_frame *__stdcall F_CreateFrame(void *, int type)
{
    if (type == FRAME_MODEL) {                          // the skid marks' models (Start), the car's wreck
        g_createdModels.push_back(MakeFrame(g_frameVtable, FRAME_MODEL, false));
        return g_createdModels.back();
    }
    if (type == FRAME_SOUND)                            // LoadSound's, the sound cache's copies
        return MakeSound();
    if (type != FRAME_LIGHT)
        std::exit(5);
    return MakeFrame(g_lightVtable, FRAME_LIGHT, false);
}
S_Material *__stdcall F_CreateMaterial(void *)
{
    ++g_materialsMade;
    auto *m = new S_Material();
    m->vtable = g_materialVtable;
    return m;
}
S_Mesh *__stdcall F_CreateMesh(void *)
{
    ++g_meshes;
    S_Mesh *mesh = NewMesh();
    mesh->created = true;
    return mesh;
}

// IGraph: slot 0x3C LoadTexture (every name loads).
struct S_Image {
    void **vtable;
    uint8_t pad[0x10];
    int refs = 1;                                       // +0x14 (I3D_texture::AddRef)
    const char *fileName = nullptr;                     // +0x18
    std::string name;
};
static_assert(offsetof(S_Image, refs) == 0x14, "image layout");
void *g_imageVtable[SLOTS];
void *g_igraphVtable[SLOTS];
struct S_IGraph {
    void **vtable;
} g_igraph = {g_igraphVtable};
std::vector<S_Image *> g_images;
uint32_t __stdcall F_ImageRelease(S_Image *self)
{
    return static_cast<uint32_t>(--self->refs);
}
void __stdcall F_MaterialSetTexture(S_Material *self, S_Image *texture)
{
    if (texture)
        ++texture->refs;
    if (self->texture)
        --self->texture->refs;
    self->texture = texture;
}
HRESULT __stdcall F_LoadTexture(void *, const char *name, const char *, uint32_t, S_Image **out)
{
    auto *image = new S_Image();
    image->vtable = g_imageVtable;
    image->name = name;
    image->fileName = image->name.c_str();
    g_images.push_back(image);
    *out = image;
    return 0;
}

// The car effects' emitters (I3DVISUAL_EMITTER): their properties (the colours read at once),
// texture, keys, emission and a few particles (up by one per Tick while emitting, down by one
// after), kept where the port reads them (+0x1F0 the texture's name at +0x18, +0x1F4 / +0x1FC the
// keys, +0x220 the particles).
struct S_FakeEmitter {
    std::map<int, uint32_t> props;
    float color1[4] = {}, color2[4] = {};
    std::string texture;
    struct {
        uint8_t pad[0x18];
        const char *name;
    } textureRecord = {};
    std::vector<std::array<uint8_t, 0x3C>> keys;
    std::vector<int> selected;                          // SelectKey's indices, in order
    bool emitting = false;
    int particles = 0, ticks = 0, updates = 0, unknownA8 = 0, duplicates = 0;
    std::string name;
};
std::map<const I3D_frame *, S_FakeEmitter> g_emitters;
void *g_emitterVtable[SLOTS];
void SyncEmitter(I3D_frame *f)
{
    S_FakeEmitter &e = g_emitters[f];
    e.textureRecord.name = e.texture.empty() ? nullptr : e.texture.c_str();
    auto *raw = reinterpret_cast<uint8_t *>(f);
    *reinterpret_cast<void **>(raw + 0x1F0) = &e.textureRecord;
    *reinterpret_cast<void **>(raw + 0x1F4) = e.keys.empty() ? nullptr : e.keys.data();
    *reinterpret_cast<int *>(raw + 0x1FC) = static_cast<int>(e.keys.size());
    *reinterpret_cast<int *>(raw + 0x220) = e.particles;
}
void __stdcall F_EmTick(I3D_frame *self, int)
{
    S_FakeEmitter &e = g_emitters[self];
    ++e.ticks;
    e.particles = e.emitting ? std::min(e.particles + 1, 3) : std::max(e.particles - 1, 0);
    SyncEmitter(self);
}
void __stdcall F_EmUpdate(I3D_frame *self)
{
    ++g_emitters[self].updates;
}
void __stdcall F_EmSetName(I3D_frame *self, const char *name)
{
    g_emitters[self].name = name;
}
void __stdcall F_EmDuplicate(I3D_frame *self, I3D_frame *source)
{
    S_FakeEmitter &e = g_emitters[self];
    const S_FakeEmitter &from = g_emitters[source];
    e.props = from.props;
    std::memcpy(e.color1, from.color1, sizeof e.color1);
    std::memcpy(e.color2, from.color2, sizeof e.color2);
    e.texture = from.texture;
    e.keys = from.keys;
    e.selected = from.selected;
    ++e.duplicates;
    SyncEmitter(self);
}
void __stdcall F_EmSetTexture(I3D_frame *self, S_Image *image)
{
    g_emitters[self].texture = image->name;            // (no reference kept: the pool outlives the models)
    SyncEmitter(self);
}
void __stdcall F_EmSetProperty(I3D_frame *self, int id, uint32_t value)
{
    S_FakeEmitter &e = g_emitters[self];
    e.props[id] = value;
    if (id == 0x1D || id == 0x1E)
        std::memcpy(id == 0x1D ? e.color1 : e.color2, reinterpret_cast<const float *>(static_cast<uintptr_t>(value)),
                    sizeof e.color1);
}
void __stdcall F_EmUnknownA8(I3D_frame *self)
{
    ++g_emitters[self].unknownA8;
}
void __stdcall F_EmSetEmitting(I3D_frame *self, bool on)
{
    g_emitters[self].emitting = on;
}
void __stdcall F_EmSelectKey(I3D_frame *self, int index)
{
    g_emitters[self].selected.push_back(index);
}
void __stdcall F_EmAddKey(I3D_frame *self, const I3D_emitter_key &key, int index)
{
    S_FakeEmitter &e = g_emitters[self];
    std::array<uint8_t, 0x3C> k = {};
    std::memcpy(k.data(), &key, sizeof key);
    if (index < 0)
        e.keys.push_back(k);
    else
        e.keys.insert(e.keys.begin() + index, k);
    SyncEmitter(self);
}
void __stdcall F_EmRemoveKey(I3D_frame *self, int index)
{
    S_FakeEmitter &e = g_emitters[self];
    e.keys.erase(e.keys.begin() + index);
    SyncEmitter(self);
}
I3D_frame *MakeEmitter()
{
    I3D_frame *f = MakeFrame(g_emitterVtable, FRAME_VISUAL, false);
    g_made.pop_back();                                  // (not the vehicle's)
    SyncEmitter(f);
    return f;
}

// The driver: slot 0xAC GetHierarchyBBox (the frame's subtree in its own space).
void Expand(I3D_bbox &box, I3D_frame *f, const S_matrix &toRoot)
{
    const S_StubFrame &s = Stub(f);
    for (size_t i = 0; i + 8 <= s.vertices.size(); i += 8)
        box.Expand(S_vector(s.vertices[i], s.vertices[i + 1], s.vertices[i + 2]) * toRoot);
    // A dummy's own box counts (an assumption about the engine).
    if (f->GetType() == FRAME_DUMMY) {
        const I3D_bbox &b = static_cast<I3D_dummy *>(f)->GetBBox();
        box.Expand(b.min * toRoot);
        box.Expand(b.max * toRoot);
    }
    for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling) {
        S_matrix m;
        const S_matrix &l = c->m_localMatrix;
        for (int r = 0; r < 4; ++r)
            for (int col = 0; col < 4; ++col) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += l.m[r][k] * toRoot.m[k][col];
                m.m[r][col] = sum;
            }
        Expand(box, c, m);
    }
}
const I3D_bbox *__stdcall F_GetHierarchyBBox(void *, I3D_bbox &out, I3D_frame *frame)
{
    out = I3D_bbox::Empty();
    S_matrix identity;
    identity.Identity();
    Expand(out, frame, identity);
    return &out;
}
void *g_driverVtable[SLOTS];
struct S_Driver {
    void **vtable;
} g_driver = {g_driverVtable};

void InitVtables()
{
    FillTraps(g_frameVtable, std::make_integer_sequence<int, SLOTS>());
    FillTraps(g_meshVtable, std::make_integer_sequence<int, SLOTS>());
    FillTraps(g_levelVtable, std::make_integer_sequence<int, SLOTS>());
    FillTraps(g_driverVtable, std::make_integer_sequence<int, SLOTS>());
    g_frameVtable[0x00 / 4] = reinterpret_cast<void *>(&F_Release);
    g_frameVtable[0x24 / 4] = reinterpret_cast<void *>(&F_Update);
    g_frameVtable[0x18 / 4] = reinterpret_cast<void *>(&F_SetPosRot);
    g_frameVtable[0x04 / 4] = reinterpret_cast<void *>(&F_GetVisualType);
    g_frameVtable[0xAC / 4] = reinterpret_cast<void *>(&F_VisualUnknownAC);
    g_frameVtable[0x38 / 4] = reinterpret_cast<void *>(&F_LinkTo);
    g_frameVtable[0x44 / 4] = reinterpret_cast<void *>(&F_EnumFrames);
    g_frameVtable[0x48 / 4] = reinterpret_cast<void *>(&F_FindChildFrame);
    g_frameVtable[0x80 / 4] = reinterpret_cast<void *>(&F_RemoveModelFrame);
    g_frameVtable[0xA0 / 4] = reinterpret_cast<void *>(&F_DuplicateMesh);
    g_frameVtable[0x1C / 4] = reinterpret_cast<void *>(&F_SetWorldDir);
    g_frameVtable[0x30 / 4] = reinterpret_cast<void *>(&F_SetOn);
    g_frameVtable[0x08 / 4] = reinterpret_cast<void *>(&F_SetPos);
    g_frameVtable[0x20 / 4] = reinterpret_cast<void *>(&F_Tick);
    g_frameVtable[0x9C / 4] = reinterpret_cast<void *>(&F_SetMesh);
    g_frameVtable[0x40 / 4] = reinterpret_cast<void *>(&F_GetChild);
    FillTraps(g_materialVtable, std::make_integer_sequence<int, SLOTS>());
    FillTraps(g_faceVtable, std::make_integer_sequence<int, SLOTS>());
    FillTraps(g_imageVtable, std::make_integer_sequence<int, SLOTS>());
    FillTraps(g_igraphVtable, std::make_integer_sequence<int, SLOTS>());
    g_materialVtable[0x00 / 4] = reinterpret_cast<void *>(&F_MaterialRelease);
    g_materialVtable[0x20 / 4] = reinterpret_cast<void *>(&F_MaterialCopyFrom);
    g_materialVtable[0x1C / 4] = reinterpret_cast<void *>(&F_MaterialSetTexture);
    g_faceVtable[0x00 / 4] = reinterpret_cast<void *>(&F_SetMaterial);
    g_imageVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ImageRelease);
    g_igraphVtable[0x3C / 4] = reinterpret_cast<void *>(&F_LoadTexture);
    // Projectors and lights: the frame slots plus their own (a frame slot 0x9C would be SetMesh).
    for (int i = 0; i < SLOTS; ++i)
        g_projectorVtable[i] = g_lightVtable[i] = g_frameVtable[i];
    FillTraps(g_projectorVtable + 0x5C / 4, std::make_integer_sequence<int, SLOTS - 0x5C / 4>());
    FillTraps(g_lightVtable + 0x5C / 4, std::make_integer_sequence<int, SLOTS - 0x5C / 4>());
    g_projectorVtable[0x9C / 4] = reinterpret_cast<void *>(&F_ProjSetTexture);
    g_projectorVtable[0xA4 / 4] = reinterpret_cast<void *>(&F_ProjInt);
    g_projectorVtable[0xB0 / 4] = reinterpret_cast<void *>(&F_ProjColor);
    g_projectorVtable[0xB4 / 4] = reinterpret_cast<void *>(&F_ProjInt);
    for (int i = 0; i < SLOTS; ++i)
        g_soundVtable[i] = g_soundSectorVtable[i] = g_frameVtable[i];
    FillTraps(g_soundVtable + 0x5C / 4, std::make_integer_sequence<int, SLOTS - 0x5C / 4>());
    FillTraps(g_soundSectorVtable + 0x5C / 4, std::make_integer_sequence<int, SLOTS - 0x5C / 4>());
    g_soundVtable[0x74 / 4] = reinterpret_cast<void *>(&F_SoundOpen);
    g_soundVtable[0x7C / 4] = reinterpret_cast<void *>(&F_SoundType);
    g_soundVtable[0x80 / 4] = reinterpret_cast<void *>(&F_SoundRange);
    g_soundVtable[0x84 / 4] = reinterpret_cast<void *>(&F_SoundCone);
    g_soundVtable[0x88 / 4] = reinterpret_cast<void *>(&F_Sound88);
    g_soundVtable[0x8C / 4] = reinterpret_cast<void *>(&F_SoundVolume);
    g_soundVtable[0x90 / 4] = reinterpret_cast<void *>(&F_SoundLoop);
    g_soundVtable[0x30 / 4] = reinterpret_cast<void *>(&F_SoundSetOn);
    g_soundVtable[0x78 / 4] = reinterpret_cast<void *>(&F_SoundPlaying);
    g_soundVtable[0x94 / 4] = reinterpret_cast<void *>(&F_SoundPlay);
    g_soundVtable[0x98 / 4] = reinterpret_cast<void *>(&F_SoundFrequency);
    g_soundVtable[0xA4 / 4] = reinterpret_cast<void *>(&F_SoundPosition);
    g_soundVtable[0xA8 / 4] = reinterpret_cast<void *>(&F_SoundLength);
    g_soundVtable[0xAC / 4] = reinterpret_cast<void *>(&F_SoundActive);
    g_soundSectorVtable[0x7C / 4] = reinterpret_cast<void *>(&F_SectorAddSound);
    g_soundSectorVtable[0x80 / 4] = reinterpret_cast<void *>(&F_SectorRemoveSound);
    for (int i = 0; i < SLOTS; ++i)
        g_emitterVtable[i] = g_frameVtable[i];
    FillTraps(g_emitterVtable + 0x5C / 4, std::make_integer_sequence<int, SLOTS - 0x5C / 4>());
    g_emitterVtable[0x20 / 4] = reinterpret_cast<void *>(&F_EmTick);
    g_emitterVtable[0x24 / 4] = reinterpret_cast<void *>(&F_EmUpdate);
    g_emitterVtable[0x34 / 4] = reinterpret_cast<void *>(&F_EmSetName);
    g_emitterVtable[0x4C / 4] = reinterpret_cast<void *>(&F_EmDuplicate);
    g_emitterVtable[0x9C / 4] = reinterpret_cast<void *>(&F_EmSetTexture);
    g_emitterVtable[0xA0 / 4] = reinterpret_cast<void *>(&F_EmSetProperty);
    g_emitterVtable[0xA8 / 4] = reinterpret_cast<void *>(&F_EmUnknownA8);
    g_emitterVtable[0xAC / 4] = reinterpret_cast<void *>(&F_EmSetEmitting);
    g_emitterVtable[0xC0 / 4] = reinterpret_cast<void *>(&F_EmSelectKey);
    g_emitterVtable[0xC4 / 4] = reinterpret_cast<void *>(&F_EmAddKey);
    g_emitterVtable[0xC8 / 4] = reinterpret_cast<void *>(&F_EmRemoveKey);
    g_lightVtable[0x74 / 4] = reinterpret_cast<void *>(&F_LightType);
    g_lightVtable[0x78 / 4] = reinterpret_cast<void *>(&F_LightFloat3);
    g_lightVtable[0x88 / 4] = reinterpret_cast<void *>(&F_LightFloat);
    g_lightVtable[0x94 / 4] = reinterpret_cast<void *>(&F_LightMode);
    g_lightVtable[0x98 / 4] = reinterpret_cast<void *>(&F_LightFloat);
    g_lightVtable[0xA4 / 4] = reinterpret_cast<void *>(&F_LightFloat2);
    g_driverVtable[0x50 / 4] = reinterpret_cast<void *>(&F_CreateVisual);
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_CreateFrame);
    g_driverVtable[0x58 / 4] = reinterpret_cast<void *>(&F_CreateMaterial);
    g_driverVtable[0x6C / 4] = reinterpret_cast<void *>(&F_CreateMesh);
    g_meshVtable[0x00 / 4] = reinterpret_cast<void *>(&F_MeshRelease);
    g_meshVtable[0x0C / 4] = reinterpret_cast<void *>(&F_MeshDuplicate);
    g_meshVtable[0x08 / 4] = reinterpret_cast<void *>(&F_MeshSetLevel);
    g_levelVtable[0x14 / 4] = reinterpret_cast<void *>(&F_GetFaceGroup);
    g_modelMaterial.vtable = g_materialVtable;
    g_meshVtable[0x04 / 4] = reinterpret_cast<void *>(&F_GetLevel);
    g_levelVtable[0x04 / 4] = reinterpret_cast<void *>(&F_GetVertices);
    g_levelVtable[0x08 / 4] = reinterpret_cast<void *>(&F_UnlockVertices);
    g_levelVtable[0x2C / 4] = reinterpret_cast<void *>(&F_LevelUnknown2C);
    g_meshVtable[0x14 / 4] = reinterpret_cast<void *>(&F_MeshUnknown14);
    g_driverVtable[0xAC / 4] = reinterpret_cast<void *>(&F_GetHierarchyBBox);
}

std::vector<S_StubFrame *> g_frames;

I3D_frame *NewFrame(E_I3D_FRAME_TYPE type, const std::string &name, I3D_frame *parent)
{
    auto *s = new S_StubFrame();
    auto *mem = static_cast<uint8_t *>(::operator new(FRAME_SIZE, std::align_val_t(16)));
    std::memset(mem, 0, FRAME_SIZE);
    *reinterpret_cast<void ***>(mem) = g_frameVtable;
    auto *f = reinterpret_cast<I3D_frame *>(mem);
    s->frame = f;
    s->name = name;
    f->m_type = type;
    f->m_refCount = 1;
    f->m_frameFlags = FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID | FRMFLAGS_ROT_DIRTY;
    f->m_localMatrix.Identity();
    f->m_scale = S_vector(1.0f, 1.0f, 1.0f);
    f->m_rot.Identity();
    if (parent) {
        f->m_parent = parent;
        I3D_frame **link = &parent->m_firstChild;
        while (*link)
            link = &(*link)->m_nextSibling;
        *link = f;
    }
    g_stubs[f] = s;
    g_frames.push_back(s);
    return f;
}

std::string Unhex(const std::string &h)
{
    std::string out;
    if (h == "-")
        return out;
    for (size_t i = 0; i + 1 < h.size(); i += 2)
        out += static_cast<char>(std::strtol(h.substr(i, 2).c_str(), nullptr, 16));
    return out;
}

// The damage models' folder (LoadModel's: "dm4ds" next to the models' folder) and the frame lists
// of those loaded.
std::string g_damageDir;
std::map<const I3D_frame *, std::vector<I3D_frame *>> g_modelFrameLists;
void ReadFrames(FILE *fp, I3D_frame *root, const std::string &prefix, std::vector<I3D_frame *> *list);
void FinishFrames(size_t from, I3D_frame *owner);

bool LoadModel(const char *path)
{
    FILE *fp = std::fopen(path, "r");
    if (!fp)
        return false;
    for (S_StubFrame *s : g_frames)
        ::operator delete(s->frame, std::align_val_t(16));
    g_frames.clear();
    g_stubs.clear();
    // (The per-frame records of the frames just freed.)
    g_frameUpdates.clear();
    g_linkFlags.clear();
    g_dirs.clear();
    g_frameTicks.clear();
    char file[260];
    int count = 0;
    if (std::fscanf(fp, "model %259s %d\n", file, &count) != 2)
        return false;
    g_modelFile = file;
    g_modelFrameLists.clear();
    // The damage models: "dm4ds" next to the models' folder.
    std::string dir = path;
    for (int up = 0; up < 2; ++up) {
        const size_t slash = dir.find_last_of("/\\");
        dir = slash == std::string::npos ? std::string(".") : dir.substr(0, slash);
    }
    g_damageDir = dir + "/dm4ds";
    g_model = NewFrame(FRAME_MODEL, "car", nullptr);
    // The model at a place and turn, to exercise the world / inverse matrices.
    g_model->m_localMatrix.m[0][0] = 0.0f;
    g_model->m_localMatrix.m[0][2] = -1.0f;
    g_model->m_localMatrix.m[2][0] = 1.0f;
    g_model->m_localMatrix.m[2][2] = 0.0f;
    g_model->m_localMatrix.m[3][0] = 100.0f;
    g_model->m_localMatrix.m[3][1] = 5.0f;
    g_model->m_localMatrix.m[3][2] = -40.0f;
    g_model->m_rot.Make(g_model->m_localMatrix);
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(g_model) + 0x134) = g_modelFile.c_str();
    ReadFrames(fp, g_model, "car.", nullptr);
    std::fclose(fp);
    FinishFrames(0, g_model);
    return true;
}

// The frames of a model text file (after its "model" line) under `root`, named `prefix` + their
// names; `list` (if any) gets them in the file's order.
void ReadFrames(FILE *fp, I3D_frame *root, const std::string &prefix, std::vector<I3D_frame *> *list)
{
    std::vector<I3D_frame *> byIndex;
    char line[4096];
    S_StubFrame *cur = nullptr;
    while (std::fgets(line, sizeof(line), fp)) {
        if (line[0] == 'f' && line[1] == ' ') {
            int type, parent, used = 0;
            std::sscanf(line, "f %d %d %n", &type, &parent, &used);
            std::string name = line + used;             // the rest of the line (names may hold spaces)
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
                name.pop_back();
            I3D_frame *p = parent ? byIndex.at(parent - 1) : root;
            I3D_frame *f = NewFrame(static_cast<E_I3D_FRAME_TYPE>(type), prefix + name, p);
            f->m_frameFlags |= 1u;                      // on, as loaded (the export keeps no hidden flag)
            byIndex.push_back(f);
            if (list)
                list->push_back(f);
            cur = &Stub(f);
        } else if (line[0] == 'L') {
            float v[16];
            const char *q = line + 2;
            for (float &x : v)
                x = std::strtof(q, const_cast<char **>(&q));
            std::memcpy(cur->frame->m_localMatrix.m, v, sizeof(v));
        } else if (line[0] == 'S') {
            S_vector &sc = cur->frame->m_scale;
            std::sscanf(line, "S %f %f %f", &sc.x, &sc.y, &sc.z);
            S_matrix rot = cur->frame->m_localMatrix;
            cur->frame->m_rot.Make(rot);
        } else if (line[0] == 'P') {
            char hex[4000];
            std::sscanf(line, "P %3999s", hex);
            cur->props = Unhex(hex);
        } else if (line[0] == 'B') {
            float b[8];
            const char *q = line + 2;
            for (float &x : b)
                x = std::strtof(q, const_cast<char **>(&q));
            std::memcpy(reinterpret_cast<uint8_t *>(cur->frame) + 0x120, b, sizeof(b));
        } else if (line[0] == 'V') {
            int n = std::atoi(line + 2);
            cur->vertices.assign(static_cast<size_t>(n) * 8, 0.0f);
            for (int i = 0; i < n; ++i) {
                std::fgets(line, sizeof(line), fp);
                std::sscanf(line, "%f %f %f", &cur->vertices[i * 8], &cur->vertices[i * 8 + 1],
                            &cur->vertices[i * 8 + 2]);
            }
            S_Mesh *mesh = NewMesh();
            mesh->levels[0]->count = n;
            mesh->levels[0]->data = cur->vertices.data();
            cur->mesh = mesh;
            *reinterpret_cast<S_Mesh **>(reinterpret_cast<uint8_t *>(cur->frame) + 0x1E0) = mesh;
        } else if (line[0] == 'W') {
            int lod = 0, n = 0;
            std::sscanf(line, "W %d %d", &lod, &n);
            cur->lods.emplace_back(static_cast<size_t>(n) * 8, 0.0f);
            std::vector<float> &v = cur->lods.back();
            for (int i = 0; i < n; ++i) {
                std::fgets(line, sizeof(line), fp);
                std::sscanf(line, "%f %f %f", &v[i * 8], &v[i * 8 + 1], &v[i * 8 + 2]);
            }
            S_Level *level = NewLevel();
            level->count = n;
            level->data = v.data();                     // (the buffer stays when lods grows)
            cur->mesh->levels.push_back(level);
            cur->mesh->levelCount = static_cast<uint32_t>(cur->mesh->levels.size());
        }
    }
}

// The frames made since g_frames[from]: their names (the prefix up to the first '.' skipped by
// GetShortName), properties and owner.
void FinishFrames(size_t from, I3D_frame *owner)
{
    for (size_t i = from; i < g_frames.size(); ++i) {
        S_StubFrame *s = g_frames[i];
        s->frame->m_name = s->name.c_str();
        const size_t dot = s->name.find('.');
        s->frame->m_nameOffset = static_cast<int16_t>(s->frame == g_model || dot == std::string::npos ? 0 : dot + 1);
        s->frame->m_userProps = s->props.empty() ? nullptr : s->props.c_str();
        s->frame->m_ownerModel = owner;
    }
}

// A damage model ("DM" + the car model's file, game/car_parts.cpp) loaded into `object` from
// g_damageDir's export: its frames under it, its frame list (+0x120 / +0x128) in the file's order;
// false without the file.
std::string DamagePath(const std::string &fileName)
{
    std::string base = fileName.substr(0, fileName.rfind('.'));
    for (char &c : base)
        if (c == ' ')
            c = '_';
    return g_damageDir + "/" + base + ".txt";
}
bool LoadDamageModel(I3D_frame *object, const char *fileName)
{
    FILE *fp = std::fopen(DamagePath(fileName).c_str(), "r");
    if (!fp)
        return false;
    char file[260];
    int count = 0;
    if (std::fscanf(fp, "model %259s %d\n", file, &count) != 2) {
        std::fclose(fp);
        return false;
    }
    const size_t from = g_frames.size();
    std::vector<I3D_frame *> &list = g_modelFrameLists[object];
    list.clear();
    ReadFrames(fp, object, "dm.", &list);
    std::fclose(fp);
    FinishFrames(from, object);
    *reinterpret_cast<I3D_frame ***>(reinterpret_cast<uint8_t *>(object) + 0x120) = list.data();
    *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(object) + 0x128) = static_cast<int>(list.size());
    return true;
}

}  // namespace

void I3D_frame::UpdateWMatrixProc()
{
    const S_matrix &l = m_localMatrix;
    if (!m_parent) {
        m_worldMatrix = l;
    } else {
        const S_matrix &p = m_parent->GetMatrix();
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += l.m[r][k] * p.m[k][c];
                m_worldMatrix.m[r][c] = sum;
            }
    }
    // Never marked valid: a parent's change does not reach the children's flags here (the engine
    // tracks that), so every GetMatrix recomputes from the root.
}

extern "C" I3D_driver *I3DGetDriver()
{
    return reinterpret_cast<I3D_driver *>(&g_driver);
}

extern "C" IGraph *GetIGraph()
{
    return reinterpret_cast<IGraph *>(&g_igraph);
}

extern "C" void dbgPrintf(const char *, ...) {}       // (the sound cache's reports)

// --- the parts of the vehicle not ported yet (1b-ii / 1c / 1d) --------------------------------

int g_effectsStopped, g_engineSoundStops;
int g_volumeCalls;
// The calls in order (A add, R remove, M move; the rigid body's test reads them).
struct S_VolumeOp {
    char op;
    S_CollisionVolume *volume;
};
std::vector<S_VolumeOp> g_volumeLog;
C_CollisionWorld::~C_CollisionWorld() {}
void C_CollisionWorld::AddVolume(S_CollisionVolume *volume)
{
    ++g_volumeCalls;
    g_volumeLog.push_back({'A', volume});
}
void C_CollisionWorld::RemoveVolume(S_CollisionVolume *volume)
{
    ++g_volumeCalls;
    g_volumeLog.push_back({'R', volume});
}
void C_CollisionWorld::MoveVolume(S_CollisionVolume *volume)
{
    ++g_volumeCalls;
    g_volumeLog.push_back({'M', volume});
}
C_CollisionWorld g_collisionWorld;

// Update's sounds and effects (parts 1c-iii and 1d, later).
int g_doorSounds[2], g_skidLays;                        // (the drive's sliding wheel steps)
// The light level: half under cover.
C_game *GetGame()
{
    alignas(16) static uint8_t game[0x200];
    return reinterpret_cast<C_game *>(game);
}
float C_game::GetLightIntensity(const S_vector &, I3D_sector *, bool occlusion)
{
    return occlusion ? 0.5f : 1.0f;
}

// The ground for the drive test: a flat plane at g_groundY (material 1) that the wheels' rays
// (the real UpdateWheelContacts) meet when it lies within them; nothing above the start.
bool g_flatGround;
float g_groundY;
uint32_t g_groundExtraFlags;                            // added to the ground volume's (HUMAN_ENV_DEADLY)
int g_rayTests, g_bodyCollisions, g_deforms, g_partHits, g_bumpSounds, g_impactSounds;
S_CollisionVolume g_groundVolume;
// A test's own world (the rigid body's step test): every ray test goes to it while set.
S_CollisionVolume *(*g_rayTestHook)(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                                    uint32_t excludeTypes, uint32_t excludeFlags);
// (Part 7f: the ray, sphere and box sweep tests' arguments, recorded while g_recordTests is set.)
struct S_TestRecord {
    char kind;                                          // 'r' ray, 's' sphere, 'b' box sweep
    S_vector from, dir;                                 // (a sweep: its move and velocity)
    float radius;                                       // (a sweep: the move's w)
    S_vector extra;                                     // (a sweep: its second vector)
    uint32_t types, flags;
};
bool g_recordTests;
std::vector<S_TestRecord> g_testRecords;
S_CollisionVolume *CollisionRayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                                    uint32_t excludeTypes, uint32_t excludeFlags)
{
    if (g_recordTests)
        g_testRecords.push_back({'r', from, dir, 0.0f, S_vector(0, 0, 0), excludeTypes, excludeFlags});
    if (g_rayTestHook)
        return g_rayTestHook(from, dir, hitPos, hitNormal, excludeTypes, excludeFlags);
    ++g_rayTests;
    if (!g_flatGround || dir.y > -1e-6f)
        return nullptr;
    const float t = (g_groundY - from.y) / dir.y;
    if (t < 0.0f || t > 1.0f)
        return nullptr;
    if (hitPos)
        *hitPos = from + dir * t;
    if (hitNormal)
        *hitNormal = S_vector(0.0f, 1.0f, 0.0f);
    g_groundVolume.flags = 0x01000000 | g_groundExtraFlags;
    return &g_groundVolume;
}

// The world the body meets (the real CollideBody): the flat ground and, for the crash test, a
// wall across the way (the plane normal . p = level, its outside where normal . p > level), as
// infinite one-sided faces. The spheres meet them as the static triangle sweep does (the
// sphere's centre where it touches, the normal out of the face; one already in a face and
// moving into it at once), the boxes by TestBoxSweep's broad test of a triangle (the bounds
// swept from the pose before to the pose now across the plane, and the plane within the
// distance of the point 20 above the box's centre to its corner 7).
struct S_Face {
    S_vector normal;
    float level;
    S_CollisionVolume *volume;
};
bool g_wall;
S_Face g_wallFace;
S_CollisionVolume g_wallVolume;
int g_sphereTests, g_boxTests, g_boxTouches, g_ignores, g_ignoreClears;

int Faces(S_Face faces[2])
{
    int n = 0;
    if (g_flatGround) {
        g_groundVolume.flags = 0x01000000 | g_groundExtraFlags;
        faces[n++] = {S_vector(0.0f, 1.0f, 0.0f), g_groundY, &g_groundVolume};
    }
    if (g_wall)
        faces[n++] = g_wallFace;
    return n;
}

// (Part 6b: a test's own sphere test, when set.)
bool g_sphereHookSetsHit;
S_vector g_sphereHookHit, g_sphereHookNormal;
S_CollisionVolume *(*g_sphereTestHook)(const S_vector &from, const S_vector &dir, float radius, uint32_t excludeTypes,
                                       uint32_t excludeFlags);
S_CollisionVolume *CollisionSphereTest(const S_vector &from, const S_vector &dir, float radius, S_vector *hitPos,
                                       S_vector *hitNormal, uint32_t excludeTypes, uint32_t excludeFlags)
{
    if (g_recordTests)
        g_testRecords.push_back({'s', from, dir, radius, S_vector(0, 0, 0), excludeTypes, excludeFlags});
    if (g_sphereTestHook) {
        S_CollisionVolume *met = g_sphereTestHook(from, dir, radius, excludeTypes, excludeFlags);
        if (met && hitPos && g_sphereHookSetsHit)       // (part 7d: where it met and its normal, when set)
            *hitPos = g_sphereHookHit;
        if (met && hitNormal && g_sphereHookSetsHit)
            *hitNormal = g_sphereHookNormal;
        return met;
    }
    ++g_sphereTests;
    if (dir.Square() <= 1e-8f)
        return nullptr;
    S_Face faces[2];
    const int count = Faces(faces);
    float best = 1.0f;
    const S_Face *met = nullptr;
    S_vector centre;
    for (int i = 0; i < count; ++i) {
        const S_Face &f = faces[i];
        const float into = -f.normal.Dot(dir);
        if (into < 0.0f)
            continue;                                   // moving out of it
        const float gap = f.normal.Dot(from) - f.level;
        if (gap < 0.0f)
            continue;                                   // behind it
        float t;
        S_vector at;
        if (gap < radius) {
            t = 0.0f;
            at = from + f.normal * (radius - gap);
        } else {
            if (into < 1e-8f)
                continue;
            t = (gap - radius) / into;
            at = from + dir * t;
        }
        if (t < best) {
            best = t;
            met = &f;
            centre = at;
        }
    }
    if (!met)
        return nullptr;
    *hitPos = centre;
    if (hitNormal)                                      // (C_Car::GetSeatEntry passes none)
        *hitNormal = met->normal;
    return met->volume;
}

bool C_CollisionWorld::TestBoxSweep(const I3D_bbox &localBox, const S_matrix &from, const S_matrix &to,
                                    const S_vector4 &move, const S_vector &second, const S_vector &velocity,
                                    uint32_t types, uint32_t flags) const
{
    if (g_recordTests)
        g_testRecords.push_back({'b', S_vector(move.x, move.y, move.z), velocity, move.w, second, types, flags});
    ++g_boxTests;
    S_vector corners[8];
    int n = 0;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            const float x = i == 0 ? localBox.max.x : localBox.min.x;
            const float y = j == 0 ? localBox.max.y : localBox.min.y;
            corners[n++] = S_vector(x, y, localBox.min.z);
            corners[n++] = S_vector(x, y, localBox.max.z);
        }
    S_vector swept[16];
    for (int i = 0; i < 8; ++i) {
        swept[i] = corners[i] * from;
        swept[8 + i] = corners[i] * to;
    }
    const S_vector localCenter((localBox.max.x + localBox.min.x) * 0.5f, (localBox.max.y + localBox.min.y) * 0.5f,
                               (localBox.max.z + localBox.min.z) * 0.5f);
    const S_vector point = velocity * 20.0f + localCenter * to;
    const float reach = (point - swept[15]).Square();
    S_Face faces[2];
    const int count = Faces(faces);
    for (int i = 0; i < count; ++i) {
        const S_Face &f = faces[i];
        float lowest = 1e30f, highest = -1e30f;
        for (const S_vector &c : swept) {
            lowest = std::min(lowest, f.normal.Dot(c) - f.level);
            highest = std::max(highest, f.normal.Dot(c) - f.level);
        }
        const float distance = f.normal.Dot(point) - f.level;
        if (lowest <= 0.0f && highest >= 0.0f && distance * distance <= reach) {
            ++g_boxTouches;
            return true;
        }
    }
    return false;
}
int g_ignoreArg = -1, g_ignoreClearArg = -1;           // (part 7f: the last calls' second arguments)
void C_CollisionWorld::SetIgnoredVolume(S_CollisionVolume *volume, int arg)
{
    (volume ? g_ignoreArg : g_ignoreClearArg) = arg;
    ++(volume ? g_ignores : g_ignoreClears);
}
int g_ignoreLists;
void C_CollisionWorld::SetIgnoredVolumes(S_CollisionVolume **, uint32_t, int)
{
    ++g_ignores;
    ++g_ignoreLists;
}
bool g_traceDrive = true;                               // the first model's drive, second by second

// The model cache (the skid marks' models): every file loads as a model with one child frame
// (its reference counted as the model's, so 0 here); g_cacheFailName is not found, and while
// g_cacheFill is set it fills the model instead (the car's model reloaded, part 2e).
std::vector<std::string> g_cacheLoads;
std::vector<I3D_frame *> g_skidFrames;
std::string g_cacheFailName;
void (*g_cacheFill)(I3D_model *object);
template <>
C_ResourceCache<I3D_model>::~C_ResourceCache()
{
}
template <>
HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *object, const char *fileName, uint32_t, void *, void *, int)
{
    g_cacheLoads.push_back(fileName);
    if (g_cacheFailName == fileName)
        return I3DERR_NOFILE;
    if (std::toupper(static_cast<unsigned char>(fileName[0])) == 'D' &&
        std::toupper(static_cast<unsigned char>(fileName[1])) == 'M')
        return LoadDamageModel(reinterpret_cast<I3D_frame *>(object), fileName) ? I3D_OK : I3DERR_NOFILE;
    if (g_cacheFill) {
        g_cacheFill(object);
        return I3D_OK;
    }
    I3D_frame *child = MakeFrame(g_frameVtable, FRAME_VISUAL, false);
    child->m_refCount = 0;
    LinkUnder(child, reinterpret_cast<I3D_frame *>(object));
    g_skidFrames.push_back(child);
    return I3D_OK;
}
I3D_model *C_ModelCache::Create()
{
    return nullptr;
}
void C_ModelCache::Duplicate(I3D_model *, I3D_model *)
{
}
C_ModelCache g_modelCache;
// The sound cache (LoadSound's): the file opened into the sound (with CACHE_OPEN_INTO, as the real
// cache's first load of a file).
template <>
C_ResourceCache<I3D_sound>::~C_ResourceCache()
{
}
template <>
HRESULT C_ResourceCache<I3D_sound>::OpenInto(I3D_sound *object, const char *fileName, uint32_t flags, void *, void *,
                                            int)
{
    return object->Open(fileName, flags | CACHE_OPEN_INTO);
}
I3D_sound *C_SoundCache::Create()
{
    return nullptr;
}
void C_SoundCache::Duplicate(I3D_sound *, I3D_sound *)
{
}
C_SoundCache g_soundCache;

// The scene: slot 0xEC UpdateFrameSector, +0x1F8 the primary sector (a stub frame).
void *g_sceneVtable[SLOTS];
I3D_frame *g_sector;
int g_sectorUpdates;
struct S_SectorUpdate {
    I3D_frame *frame;
    S_vector pos, at;                                   // at: the frame's world position then
};
std::vector<S_SectorUpdate> g_sectorFrames;
void __stdcall F_UpdateFrameSector(void *, I3D_frame *frame, const S_vector &pos)
{
    ++g_sectorUpdates;
    g_sectorFrames.push_back({frame, pos, frame->GetWorldPos()});
}
I3D_scene *MakeScene()
{
    FillTraps(g_sceneVtable, std::make_integer_sequence<int, SLOTS>());
    g_sceneVtable[0xEC / 4] = reinterpret_cast<void *>(&F_UpdateFrameSector);
    auto *mem = static_cast<uint8_t *>(::operator new(FRAME_SIZE, std::align_val_t(16)));
    std::memset(mem, 0, FRAME_SIZE);
    *reinterpret_cast<void ***>(mem) = g_sceneVtable;
    auto *sector = static_cast<uint8_t *>(::operator new(FRAME_SIZE, std::align_val_t(16)));
    std::memset(sector, 0, FRAME_SIZE);
    *reinterpret_cast<void ***>(sector) = g_frameVtable;
    g_sector = reinterpret_cast<I3D_frame *>(sector);
    g_sector->m_refCount = 1;
    // (At the origin, as the engine's: the frames linked under it keep their places.)
    g_sector->m_type = FRAME_SECTOR;
    g_sector->m_frameFlags = FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID | FRMFLAGS_ROT_DIRTY;
    g_sector->m_localMatrix.Identity();
    g_sector->m_scale = S_vector(1.0f, 1.0f, 1.0f);
    g_sector->m_rot.Identity();
    *reinterpret_cast<I3D_frame **>(mem + 0x1F8) = g_sector;
    return reinterpret_cast<I3D_scene *>(mem);
}
I3D_scene *g_carScene;
std::vector<S_TyreType> g_tyreTypes;
std::vector<S_TyreSurface> g_tyreSurfaces(1);
// One surface of InitCarSurfaces' defaults (no material table) for every material.
// (Its effects: dust 2 and skid 8 as the shipped materials have them; a contact effect 3 for the
// test: no shipped material has one. It takes skid marks, as the roads do.)
std::vector<S_CarSurface> g_carSurfaces = {{0, 100.0f, 100.0f, 100.0f, 100.0f, 0.0f, 0.35f, 1.0f, 2, 8, -3, 1, 1}};
int g_carSurfaceOfMaterial[256];

// --- the run -----------------------------------------------------------------------------------

namespace {

std::string Short(const I3D_frame *f)
{
    return f ? f->GetShortName() : "-";
}

// --- the lamps (the real UpdateLamps) --------------------------------------------------------------

bool On(const I3D_frame *f) { return f && f->IsOn(); }

// The lamps' billboards, projected lights and spot lights switched on.
int LampObjectsOn(const C_Vehicle *v)
{
    int on = 0;
    for (const S_VehicleLamp &l : v->m_lamps)
        on += On(l.billboard) + On(l.projector) + On(l.light);
    return on;
}

S_vector UnitZ(const I3D_frame *f)
{
    S_vector z = f->GetMatrix()(2);
    return z * (1.0f / std::sqrt(z.Dot(z)));
}

// Per switch setting (one 50 ms update from the bits given, the car's brake, gear and steering
// set): the lamps of the kinds it lights lit (their billboard, projected light and spot light
// on), the others dark (their shared projector may be lit by a partner); each lit projected /
// spot light at its lamp or PROJECTOR, facing along it (a shared headlight's spot along the car).
// Then the indicators' blink over 2 s and the flashers' alternation.
void LampTest(C_Vehicle *v)
{
    const uint32_t savedGear = static_cast<uint32_t>(v->m_gear);
    const float savedBrake = v->m_brake, savedSteering = v->m_unknown94C;
    v->m_unknown544 = g_sector;                         // as if the model were in the scene
    struct S_Scene {
        const char *name;
        uint32_t lights;
        float brake;
        int gear;
        float steering;
        uint32_t kinds;                                 // bit per lamp kind lit
    };
    const auto bit = [](int kind) { return 1u << kind; };
    const S_Scene scenes[] = {
        {"headlights", VEHICLE_LIGHT_HEADLIGHTS | VEHICLE_LIGHT_HEADLIGHT_BEAMS, 0.0f, 1, 0.0f, bit(VEHICLE_LAMP_RE)},
        {"brake", VEHICLE_LIGHT_AUTO_BRAKE | VEHICLE_LIGHT_BRAKE_BEAMS | VEHICLE_LIGHT_REAR_BEAMS, 1.0f, 1, 0.0f,
         bit(VEHICLE_LAMP_BR) | bit(VEHICLE_LAMP_BI_LEFT) | bit(VEHICLE_LAMP_BI_RIGHT)},
        {"reverse", VEHICLE_LIGHT_AUTO_REVERSE | VEHICLE_LIGHT_REVERSE_BEAMS, 0.0f, -1, 0.0f, bit(VEHICLE_LAMP_BA)},
        {"left", VEHICLE_LIGHT_INDICATOR_LEFT | VEHICLE_LIGHT_BLINK | VEHICLE_LIGHT_INDICATOR_BEAMS |
                     VEHICLE_LIGHT_REAR_BEAMS,
         0.0f, 1, 0.0f, bit(VEHICLE_LAMP_IN_LEFT) | bit(VEHICLE_LAMP_BI_LEFT)},
        {"right", VEHICLE_LIGHT_AUTO_INDICATORS | VEHICLE_LIGHT_BLINK | VEHICLE_LIGHT_INDICATOR_BEAMS |
                      VEHICLE_LIGHT_REAR_BEAMS,
         0.0f, 1, 0.5f, bit(VEHICLE_LAMP_IN_RIGHT) | bit(VEHICLE_LAMP_BI_RIGHT)},
        {"flashers", VEHICLE_LIGHT_FLASHING | VEHICLE_LIGHT_FLASH_BEAMS, 0.0f, 1, 0.0f, bit(VEHICLE_LAMP_LI_RIGHT)},
        {"dark", 0, 0.0f, 1, 0.0f, 0},
    };
    const auto run = [&](const S_Scene &scene, const char *label) {
        v->m_unknown5B0 = scene.lights;
        v->m_unknown5B4 = 0;
        v->m_unknown5C0 = v->m_unknown5CC = 0.0f;
        v->m_brake = scene.brake;
        v->m_gear = scene.gear;
        v->m_unknown94C = scene.steering;
        g_dirs.clear();
        v->UpdateLamps(0.05f);
        int lit = 0, expected = 0, wrong = 0, misplaced = 0;
        for (const S_VehicleLamp &l : v->m_lamps) {
            const bool want = (scene.kinds & bit(l.kind)) != 0;
            const bool shared = (l.flags & VEHICLE_LAMP_SHARED) != 0;
            bool partnerLit = false;
            for (const S_VehicleLamp &o : v->m_lamps)
                partnerLit |= &o != &l && shared && (o.flags & VEHICLE_LAMP_SHARED) &&
                              o.projectorIndex == l.projectorIndex && (scene.kinds & bit(o.kind));
            const bool full = (!l.billboard || On(l.billboard)) && (!l.projector || On(l.projector)) &&
                              (!l.projector || !l.light || On(l.light));
            const bool any = On(l.billboard) || ((On(l.projector) || On(l.light)) && !partnerLit);
            expected += want;
            lit += want && full;
            wrong += want ? !full : any;
            if (!want || !l.projector)
                continue;
            const I3D_frame *at = shared ? v->m_projectors[l.projectorIndex].frame : l.frame;
            const S_vector pos = at->GetWorldPos();
            const S_vector dir = UnitZ(at);
            const auto check = [&](const I3D_frame *f, const S_vector &want_dir) {
                const S_vector &p = f->m_localMatrix(3);
                const auto d = g_dirs.find(f);
                if ((p - pos).Dot(p - pos) > 1e-6f || d == g_dirs.end() ||
                    (d->second - want_dir).Dot(d->second - want_dir) > 1e-6f)
                    ++misplaced;
            };
            check(l.projector, dir);
            if (l.light) {
                const S_vector ahead(v->m_modelAxisZ.x, v->m_modelAxisZ.y, v->m_modelAxisZ.z);
                check(l.light, shared && l.kind == VEHICLE_LAMP_RE ? ahead : dir);
            }
        }
        std::printf("lamps %s%s: lit %d expected %d wrong %d misplaced %d\n", scene.name, label, lit, expected, wrong,
                    misplaced);
    };
    for (const S_Scene &scene : scenes)
        run(scene, "");

    // The blink (left indicators, 50 ms updates over 2 s) and the flashers' alternation: the lit
    // runs of the first indicator / flasher in steps.
    const auto runs = [&](uint32_t lights, int kind) {
        const S_VehicleLamp *lamp = nullptr;
        for (const S_VehicleLamp &l : v->m_lamps)
            if (l.kind == kind && (l.billboard || (l.projector && !(l.flags & VEHICLE_LAMP_SHARED))) && !lamp)
                lamp = &l;
        std::string pattern;
        if (!lamp)
            return pattern;
        v->m_unknown5B0 = lights;
        v->m_unknown5B4 = 0;
        v->m_unknown5C0 = v->m_unknown5CC = 0.0f;
        for (int step = 0; step < 40; ++step) {
            v->UpdateLamps(0.05f);
            pattern += (lamp->billboard ? On(lamp->billboard) : On(lamp->projector)) ? '#' : '.';
        }
        return pattern;
    };
    std::printf("lamps blink: %s flash: %s\n",
                runs(VEHICLE_LIGHT_INDICATOR_LEFT | VEHICLE_LIGHT_INDICATOR_BEAMS, VEHICLE_LAMP_IN_LEFT).c_str(),
                runs(VEHICLE_LIGHT_FLASHING | VEHICLE_LIGHT_FLASH_BEAMS, VEHICLE_LAMP_LI_LEFT).c_str());

    // No shipped model has reversing lights or flashers: the headlights relabelled as those (the
    // flashers alternately left and right).
    std::vector<uint16_t> kinds;
    for (const S_VehicleLamp &l : v->m_lamps)
        kinds.push_back(l.kind);
    for (S_VehicleLamp &l : v->m_lamps)
        if (l.kind == VEHICLE_LAMP_RE)
            l.kind = VEHICLE_LAMP_BA;
    run(scenes[2], "*");
    run(scenes[6], "*");
    bool left = true;
    for (S_VehicleLamp &l : v->m_lamps)
        if (l.kind == VEHICLE_LAMP_BA) {
            l.kind = left ? VEHICLE_LAMP_LI_LEFT : VEHICLE_LAMP_LI_RIGHT;
            left = !left;
        }
    run(scenes[5], "*");
    run(scenes[6], "*");
    std::printf("lamps flash*: %s\n", runs(VEHICLE_LIGHT_FLASHING | VEHICLE_LIGHT_FLASH_BEAMS, VEHICLE_LAMP_LI_LEFT).c_str());
    for (size_t i = 0; i < kinds.size(); ++i)
        v->m_lamps[i].kind = kinds[i];

    v->m_unknown5B0 = v->m_unknown5B4 = 0;
    v->m_unknown5C0 = v->m_unknown5CC = 0.0f;
    v->m_gear = static_cast<int>(savedGear);
    v->m_brake = savedBrake;
    v->m_unknown94C = savedSteering;
    v->UpdateLamps(0.05f);
}

// --- Update's doors, windows and track strips --------------------------------------------------------

// Every door opened (to its +0x2C) and closed, every window wound down and up, by Updates of 83 ms:
// the steps taken, the door sounds, a door frame's turn from its rest as large as its angle
// (|rot . rest| = cos(angle / 2)) and back at rest, a window hidden at the bottom 0.1 inwards,
// shown and in place at the top.
void DoorTest(C_Vehicle *v)
{
    std::vector<S_VehicleDoor *> doors;
    for (S_VehicleSeat &seat : v->m_seats)
        if (seat.door)
            doors.push_back(seat.door);
    const auto run = [&](const auto &moving) {
        int steps = 0, misturned = 0;
        while (steps < 100 && moving()) {
            v->Update(83);
            ++steps;
            for (const S_VehicleDoor *d : doors) {
                const S_quat &r = d->frame->m_rot;
                const S_quat &c = d->closedRot;
                const float dot = std::fabs(r.v.x * c.v.x + r.v.y * c.v.y + r.v.z * c.v.z + r.s * c.s);
                misturned += std::fabs(dot - std::cos(d->angle * 0.5f)) > 1e-4f;
            }
        }
        return std::make_pair(steps, misturned);
    };
    const auto anyDoorMoving = [&] {
        for (const S_VehicleDoor *d : doors)
            if (d->flags & DOOR_MOVING)
                return true;
        return false;
    };
    g_doorSounds[0] = g_doorSounds[1] = 0;
    for (S_VehicleDoor *d : doors) {
        d->targetAngle = d->unknown2C;
        d->flags |= DOOR_MOVING;
    }
    // The steps to open (the angle past the target) and to shut (down to 0), the slowest door's.
    int openSteps = 0, shutSteps = 0;
    for (const S_VehicleDoor *d : doors) {
        const double step = static_cast<double>(d->speed) * 0.083;
        openSteps = std::max(openSteps, static_cast<int>(std::floor(d->unknown2C / step)) + 1);
        shutSteps = std::max(shutSteps, static_cast<int>(std::ceil(d->unknown2C / step)));
    }
    const auto opened = run(anyDoorMoving);
    int open = 0, outward = 0;
    for (const S_VehicleDoor *d : doors) {
        open += d->angle == d->unknown2C;
        // The free end's sideways move from shut, against the door's side.
        const float end = (d->flags & DOOR_BACKWARD) ? 1.0f : -1.0f;
        const float dx = (d->frame->m_rot.RotationMatrix()(2).x - d->closedRot.RotationMatrix()(2).x) * end;
        outward += dx * d->pos.x > 0.0f;
    }
    for (S_VehicleDoor *d : doors) {
        d->targetAngle = 0.0f;
        d->flags |= DOOR_MOVING;
    }
    const auto closed = run(anyDoorMoving);
    int shut = 0;
    for (const S_VehicleDoor *d : doors) {
        const S_quat &r = d->frame->m_rot;
        const S_quat &c = d->closedRot;
        shut += d->angle == 0.0f && std::fabs(r.v.x * c.v.x + r.v.y * c.v.y + r.v.z * c.v.z + r.s * c.s) > 0.99999f;
    }
    std::printf("doors: %zu open %d in %d steps shut %d in %d steps sounds %d/%d misturned %d expected steps %d/%d "
                "outward %d\n", doors.size(), open, opened.first, shut, closed.first, g_doorSounds[1], g_doorSounds[0],
                opened.second + closed.second, openSteps, shutSteps, outward);

    // The windows. No shipped door has one: the first two doors get one as Setup would set it
    // up (0.4 up, either side; 0.3 a second), removed after the test.
    std::vector<S_VehicleDoor *> windows;
    for (S_VehicleDoor *d : doors)
        if (d->window)
            windows.push_back(d);
    std::vector<S_VehicleDoor *> synthetic;
    for (size_t i = 0; windows.empty() && i < doors.size() && i < 2; ++i) {
        S_VehicleDoor *d = doors[i];
        d->window = MakeFrame(g_frameVtable, FRAME_VISUAL, false);
        g_made.pop_back();                              // (not Setup's)
        d->windowPos.x = i == 0 ? 0.6f : -0.6f;
        d->windowPos.y = 0.4f;
        d->windowPos.z = 0.1f;
        d->windowPos.w = 1.0f;
        d->window->SetPosInline(S_vector(d->windowPos.x, d->windowPos.y, d->windowPos.z));
        d->windowX = d->windowPos.x;
        d->windowY = d->windowPos.y;
        d->windowLowY = d->windowPos.y - 0.5f;
        d->unknown60 = 0.3f;
        d->windowPart = 0xFFFF;
        synthetic.push_back(d);
    }
    if (windows.empty())
        windows = synthetic;
    const auto anyWindowMoving = [&] {
        for (const S_VehicleDoor *d : windows)
            if (d->windowMoving)
                return true;
        return false;
    };
    for (S_VehicleDoor *d : windows) {
        d->windowDown = true;
        d->windowMoving = true;
    }
    const auto down = run(anyWindowMoving);
    int hidden = 0;
    for (const S_VehicleDoor *d : windows) {
        const S_vector &p = d->window->m_localMatrix(3);
        const float in = d->windowPos.x > 0.0f ? 0.1f : -0.1f;
        hidden += !d->window->IsOn() && std::fabs(p.y - d->windowLowY) < 1e-5f &&
                  std::fabs(p.x - (d->windowPos.x + in)) < 6e-3f && p.z == d->windowPos.z;
    }
    for (S_VehicleDoor *d : windows) {
        d->windowDown = false;
        d->windowMoving = true;
    }
    const auto up = run(anyWindowMoving);
    int shown = 0;
    for (const S_VehicleDoor *d : windows) {
        const S_vector &p = d->window->m_localMatrix(3);
        shown += d->window->IsOn() && p.y == d->windowPos.y && p.x == d->windowPos.x;
    }
    // A window whose part has VEHICLE_PART_STATE_1 stops where it is.
    int stopped = 0;
    if (!windows.empty() && !v->m_parts.empty()) {
        S_VehicleDoor *d = windows[0];
        const uint32_t state = v->m_parts[0].state;
        v->m_parts[0].state |= VEHICLE_PART_STATE_1;
        d->windowPart = 0;
        d->windowDown = true;
        d->windowMoving = true;
        v->Update(83);
        stopped = !d->windowMoving && d->windowY == d->windowPos.y && d->window->m_localMatrix(3).y == d->windowPos.y;
        v->m_parts[0].state = state;
        d->windowPart = 0xFFFF;
        d->windowMoving = false;
    }
    std::printf("windows: %zu down in %d steps hidden %d up in %d steps shown %d stopped %d\n", windows.size(),
                down.first, hidden, up.first, shown, stopped);
    for (S_VehicleDoor *d : synthetic) {
        ::operator delete(d->window, std::align_val_t(16));
        d->window = nullptr;
        d->windowPos = S_vector4{};
        d->windowX = d->windowY = d->windowLowY = d->unknown60 = 0.0f;
        d->windowPart = 0;
        d->windowMoving = d->windowDown = false;
    }
}

// --- the car effects -----------------------------------------------------------------------------

S_vector XYZ4(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }
bool Near(const S_vector &a, const S_vector &b, float eps = 1e-4f)
{
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}
uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
float PropFloat(const S_FakeEmitter &e, int id)
{
    const auto it = e.props.find(id);
    if (it == e.props.end())
        return std::nanf("");
    float f;
    std::memcpy(&f, &it->second, sizeof f);
    return f;
}
S_vector PropVector(const S_FakeEmitter &e, int first)
{
    return S_vector(PropFloat(e, first), PropFloat(e, first + 1), PropFloat(e, first + 2));
}

// An effect's properties on an emitter (`all`: also 0x22; the colours its own).
bool PropsAre(const S_FakeEmitter &e, const S_CarEffectDef &def, bool all)
{
    std::map<int, uint32_t> want = {
        {0x14, 0}, {0x13, def.flags & 0x107}, {0, static_cast<uint32_t>(def.unknown70)},
        {1, static_cast<uint32_t>(def.unknown74)}, {2, static_cast<uint32_t>(def.unknown78)},
        {3, static_cast<uint32_t>(def.unknown7C)}, {4, FloatBits(def.unknown80)}, {5, FloatBits(def.unknown84)},
        {0x1A, FloatBits(def.unknownD4)}, {0xB, FloatBits(def.unknownA4)}, {0xC, FloatBits(def.unknownA8)},
        {6, FloatBits(def.unknown88)}, {7, static_cast<uint32_t>(static_cast<int>(def.unknown8C))},
        {0xD, FloatBits(def.velocity.x)}, {0xE, FloatBits(def.velocity.y)}, {0xF, FloatBits(def.velocity.z)},
        {0x15, 1}, {0x10, FloatBits(def.velocity2.x)}, {0x11, FloatBits(def.velocity2.y)},
        {0x12, FloatBits(def.velocity2.z)}, {0x16, 1}, {0x18, FloatBits(def.unknownC8)},
        {0x19, FloatBits(def.unknownCC)}, {8, static_cast<uint32_t>(def.unknown90)},
        {9, static_cast<uint32_t>(def.unknown94)}, {0xA, FloatBits(def.unknownA0)}};
    if (all)
        want[0x22] = FloatBits(def.unknownAC);
    for (const auto &[id, value] : want) {
        const auto it = e.props.find(id);
        if (it == e.props.end() || it->second != value)
            return false;
    }
    return std::memcmp(e.color1, def.color1, sizeof e.color1) == 0 && std::memcmp(e.color2, def.color2, sizeof e.color2) == 0;
}

// The selected keys: every key of the effect's texture rectangle, each once since `from`.
bool KeysSelected(const S_FakeEmitter &e, const S_CarEffectDef &def, size_t from)
{
    std::vector<int> want;
    for (size_t k = 0; k < e.keys.size(); ++k)
        if (std::memcmp(e.keys[k].data(), def.rect, sizeof def.rect) == 0)
            want.push_back(static_cast<int>(k));
    std::vector<int> got(e.selected.begin() + static_cast<std::ptrdiff_t>(std::min(from, e.selected.size())), e.selected.end());
    std::sort(got.begin(), got.end());
    return !want.empty() && got == want;
}

// The pool after InitCarEffects: 30 emitters, each on effect 1 (its texture, named, its
// properties, a key per effect of that texture, its rectangle's selected), free.
void PoolTest()
{
    const S_CarEffectDef &def = g_carEffects[1];
    int keysWanted = 0;
    for (uint32_t i = 0; i < g_carEffectCount; ++i)
        keysWanted += std::strcmp(g_carEffects[i].texture, def.texture) == 0;
    int onFirst = 0, keys = 0, selected = 0, props = 0, named = 0, idle = 0;
    for (const S_CarEmitter &em : g_carEmitters) {
        const S_FakeEmitter &e = g_emitters.at(em.visual);
        onFirst += em.effect == 1 && e.texture == def.texture && Near(XYZ4(em.velocity), def.velocity, 0.0f) &&
                   Near(XYZ4(em.velocity2), def.velocity2, 0.0f);
        keys += static_cast<int>(e.keys.size()) == keysWanted;
        selected += KeysSelected(e, def, 0);
        props += PropsAre(e, def, true);
        named += e.name == "Car_particle" && e.unknownA8 == 1 && e.updates == 2;
        idle += !em.active && !em.stopping && !e.emitting && !em.visual->m_parent;
    }
    std::printf("effects pool: definitions %u emitters %zu on effect 1 %d keys %d (%d each) selected %d properties %d named %d idle %d\n",
                g_carEffectCount, g_carEmitters.size(), onFirst, keys, keysWanted, selected, props, named, idle);
}

// An emitter running `effect`: taken and emitting, its velocities the effect's plus `carried`
// (the second plus `share` of it), its colours the effect's x `light` (the HSV value scales the
// RGB; the alpha its own), placed at `pos`.
bool EmitterAs(int slot, int effect, const S_vector &carried, float share, const S_vector &pos, float light)
{
    if (slot < 0)
        return false;
    const S_CarEmitter &emitter = g_carEmitters[slot];
    const S_CarEffectDef &def = g_carEffects[effect];
    const S_FakeEmitter &e = g_emitters.at(emitter.visual);
    bool ok = emitter.active && !emitter.stopping && emitter.effect == effect && e.emitting;
    ok &= Near(PropVector(e, 0xD), carried + def.velocity);
    ok &= Near(PropVector(e, 0x10), carried * share + def.velocity2);
    ok &= Near(emitter.visual->m_localMatrix(3), pos);
    for (int c = 0; c < 2; ++c) {
        const float *want = c ? def.color2 : def.color1;
        const float *got = c ? e.color2 : e.color1;
        for (int k = 0; k < 3; ++k)
            ok &= std::fabs(got[k] - want[k] * light) < 1e-4f;
        ok &= got[3] == want[3];
    }
    return ok;
}

// Let-go emitters freed by the pool's update (the ticks it took).
int FlushEmitters()
{
    int ticks = 0;
    for (; ticks < 20 && std::any_of(g_carEmitters.begin(), g_carEmitters.end(),
                                     [](const S_CarEmitter &e) { return e.active && e.stopping; });
         ++ticks)
        UpdateCarEffectEmitters(83);
    return ticks;
}

// The effects under set states (the vehicle at rest; restored afterwards).
void EffectsTest(C_Vehicle *v)
{
    const S_vector4 velocity0 = v->m_velocity;
    const S_vector turn0 = v->m_unknown210;
    const float speed0 = v->m_speed, revs0 = v->m_engineRevs;
    const bool engine0 = v->m_engineOn;
    const S_vector velocity(1.0f, 0.0f, 2.0f), turn(0.2f, -0.3f, 0.1f);
    v->m_velocity = {velocity.x, velocity.y, velocity.z, 0.0f};
    v->m_unknown210 = turn;
    const float light0 = v->m_unknown548;
    v->m_unknown548 = 0.5f;
    const float light = v->m_unknown548;
    const auto carried = [&](const S_vector &point) {
        return velocity - turn.Cross(point - XYZ4(v->m_unknown1B0));
    };

    // A wheel (the others off the ground): its dust rolling over 3 m/s, the same emitter switched
    // to the skid when locked over 2 m/s or spinning, back to the dust locked under 2 m/s,
    // stopped under 3 m/s; nothing in mission 8; the let-go emitter freed.
    int wheel = -1;
    for (int i = 0; i < v->m_wheelCount && wheel < 0; ++i)
        if (v->m_wheels[i] && !(v->m_wheels[i]->flags & WHEEL_DUMMY) && (v->m_wheels[i]->flags & WHEEL_EFFECTS))
            wheel = i;
    if (wheel >= 0) {
        std::vector<uint32_t> flags0;
        for (int i = 0; i < v->m_wheelCount; ++i) {
            flags0.push_back(v->m_wheels[i] ? v->m_wheels[i]->flags : 0);
            if (v->m_wheels[i])
                v->m_wheels[i]->flags &= ~WHEEL_ON_GROUND;
        }
        S_VehicleWheel *w = v->m_wheels[wheel];
        const S_vector4 point0 = w->unknown050;
        const S_vector normal0 = w->unknown080, contact0 = w->unknown0A0;
        w->flags = (flags0[wheel] | WHEEL_ON_GROUND) &
                   ~(WHEEL_SPINNING | WHEEL_LOCKED | WHEEL_FLAG_1000000 | WHEEL_DETACHED);
        S_vector n(0.1f, 1.0f, 0.05f);
        n.Normalize();
        w->unknown080 = n;
        w->unknown0A0 = S_vector(3.0f, 0.5f, 4.0f);
        w->unknown050 = {1.0f, 2.0f, 3.0f, 1.0f};
        const S_vector at(1.0f, 2.0f, 3.0f);
        const S_vector along = w->unknown0A0 - n * n.Dot(w->unknown0A0);
        const int dust = g_carSurfaces[0].dustEffect, skid = g_carSurfaces[0].skidEffect;
        v->m_speed = 10.0f;
        v->UpdateWheelEffects();
        const int slot = w->effect;
        const bool rolling = EmitterAs(slot, dust, along * 0.65f, 0.001f, at, light);
        w->flags |= WHEEL_LOCKED;
        v->UpdateWheelEffects();
        const bool locked = w->effect == slot && EmitterAs(slot, skid, along, 0.01f, at, light);
        w->flags = (w->flags & ~WHEEL_LOCKED) | WHEEL_SPINNING;
        w->unknown0A0 = S_vector(0.5f, 0.0f, 0.5f);
        const S_vector slow = w->unknown0A0 - n * n.Dot(w->unknown0A0);
        v->UpdateWheelEffects();
        const bool spinning = w->effect == slot && EmitterAs(slot, skid, slow, 0.01f, at, light);
        w->flags = (w->flags & ~WHEEL_SPINNING) | WHEEL_LOCKED;
        v->UpdateWheelEffects();
        const bool slowLocked = w->effect == slot && EmitterAs(slot, dust, slow * 0.65f, 0.001f, at, light);
        w->flags &= ~WHEEL_LOCKED;
        v->m_speed = 2.0f;
        v->UpdateWheelEffects();
        const bool stopped = w->effect == -1 && g_carEmitters[slot].active && g_carEmitters[slot].stopping;
        const int freed = FlushEmitters();
        const bool free = !g_carEmitters[slot].active && !g_emitters.at(g_carEmitters[slot].visual).emitting;
        int copies = 0;
        for (const auto &[f, e] : g_emitters)
            copies += e.duplicates;
        w->unknown0A0 = S_vector(3.0f, 0.5f, 4.0f);
        v->m_speed = 10.0f;
        v->UpdateWheelEffects();
        int copiesAfter = 0;
        for (const auto &[f, e] : g_emitters)
            copiesAfter += e.duplicates;
        const bool retaken = w->effect >= 0 && copiesAfter == copies &&
                             EmitterAs(w->effect, dust, along * 0.65f, 0.001f, at, light);
        v->m_speed = 2.0f;
        v->UpdateWheelEffects();
        FlushEmitters();
        *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(GetGame()) + 0x3C) = 8;
        v->m_speed = 10.0f;
        v->UpdateWheelEffects();
        const bool mission8 = w->effect == -1;
        *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(GetGame()) + 0x3C) = 0;
        v->m_unknownF6C &= ~VEHICLE_SIM_EFFECTS;
        v->UpdateWheelEffects();
        v->UpdateWheelEffects();
        v->m_unknownF6C |= VEHICLE_SIM_EFFECTS;
        const bool off = w->effect == -1;
        for (int i = 0; i < v->m_wheelCount; ++i)
            if (v->m_wheels[i])
                v->m_wheels[i]->flags = flags0[i];
        w->unknown050 = point0;
        w->unknown080 = normal0;
        w->unknown0A0 = contact0;
        std::printf("effects wheel: rolling %d locked %d spinning %d slow locked %d stopped %d freed in %d free %d retaken %d mission 8 %d off %d\n",
                    rolling, locked, spinning, slowLocked, stopped, freed, free, retaken, mission8, off);
    } else {
        std::printf("effects wheel: none\n");
    }
    FlushEmitters();

    // The exhausts: the engine on under +0x758 m/s, each blown out along its direction at the
    // revs' share of +0x75C..+0x760, at its place; stopped over +0x758 m/s.
    {
        v->m_engineOn = true;
        v->m_speed = v->m_unknown758 * 0.5f;
        v->m_engineRevs = v->m_revLimit * 0.5f;
        v->UpdateExhausts();
        const float blow = (v->m_unknown760 - v->m_unknown75C) * 0.5f + v->m_unknown75C;
        int started = 0, right = 0;
        std::vector<int> slots;
        for (const S_VehicleExhaust &e : v->m_exhausts) {
            started += e.effect >= 0;
            const S_vector pos = XYZ4(e.pos) * v->m_modelMatrix;
            const S_vector dir = XYZ4(e.dir) * v->m_unknown310;
            right += EmitterAs(e.effect, v->m_unknown754, carried(pos) + dir * blow, 0.1f, pos, light);
            slots.push_back(e.effect);
        }
        v->m_speed = v->m_unknown758 + 1.0f;
        v->UpdateExhausts();
        int stopped = 0;
        for (size_t i = 0; i < v->m_exhausts.size(); ++i)
            stopped += v->m_exhausts[i].effect == -1 && slots[i] >= 0 && g_carEmitters[slots[i]].stopping;
        std::printf("effects exhaust: %zu started %d right %d stopped %d\n", v->m_exhausts.size(), started, right,
                    stopped);
        v->m_engineOn = engine0;
    }
    FlushEmitters();

    // The engine smoke: +0x630 with +0x610 under +0x624 (true when none); +0x634 under +0x628;
    // started again each call on the same emitter (the slot against the effect: Quirk); stopped
    // with +0x610 back.
    {
        const float health0 = v->m_unknown610;
        const int smoke0 = v->m_unknown630, second0 = v->m_unknown634;
        v->m_unknown630 = 6;
        v->m_unknown634 = 7;
        v->m_speed = 1.0f;
        v->m_unknown610 = (v->m_unknown624 + v->m_unknown628) * 0.5f;
        const bool quiet = v->UpdateEngineSmoke();
        const S_vector pos = XYZ4(v->m_smokePos) * v->m_modelMatrix;
        const int slot = v->m_unknown638;
        const bool first = !quiet && EmitterAs(slot, 6, carried(pos), 0.1f, pos, light);
        const size_t props = slot >= 0 ? g_emitters.at(g_carEmitters[slot].visual).selected.size() : 0;
        v->UpdateEngineSmoke();
        const bool again = slot >= 0 && v->m_unknown638 == slot && EmitterAs(slot, 6, carried(pos), 0.1f, pos, light) &&
                           g_emitters.at(g_carEmitters[slot].visual).selected.size() > props;
        v->m_unknown610 = v->m_unknown628 * 0.5f;
        v->UpdateEngineSmoke();
        const bool second = EmitterAs(v->m_unknown638, 7, carried(pos), 0.1f, pos, light);
        const int secondSlot = v->m_unknown638;
        v->m_unknown610 = v->m_unknown624 + 1.0f;
        const bool stopped = v->UpdateEngineSmoke() && v->m_unknown638 == -1 && secondSlot >= 0 &&
                             g_carEmitters[secondSlot].stopping;
        std::printf("effects smoke: first %d again on the same %d second %d stopped %d\n", first, again, second,
                    stopped);
        v->m_unknown610 = health0;
        v->m_unknown630 = smoke0;
        v->m_unknown634 = second0;
    }
    FlushEmitters();

    // The body contacts': the first two (their surface's |effect|) at the car's position, none for
    // the third; stopped without contacts and without VEHICLE_SIM_EFFECTS.
    {
        const std::vector<S_VehicleBodyContact> contacts0 = v->m_bodyContacts;
        v->m_bodyContacts.assign(2, S_VehicleBodyContact{});
        v->m_bodyContacts[0].material = 1;
        v->m_bodyContacts[1].material = 2;
        v->UpdateContactEffects();
        const S_vector pos = XYZ4(v->m_modelPos);
        const int effect = std::abs(g_carSurfaces[0].contactEffect);
        int right = 0;
        for (int k = 0; k < 2; ++k)
            right += EmitterAs(v->m_bodyContactEffects[k], effect, carried(pos), 0.01f, pos, light);
        const bool third = v->m_bodyContactEffects[2] == -1;
        const int slots[2] = {v->m_bodyContactEffects[0], v->m_bodyContactEffects[1]};
        const int contact0 = g_carSurfaces[0].contactEffect;
        g_carSurfaces[0].contactEffect = -5;
        v->UpdateContactEffects();
        int switched = 0;
        for (int k = 0; k < 2; ++k)
            switched += slots[k] >= 0 && v->m_bodyContactEffects[k] == slots[k] &&
                        EmitterAs(slots[k], 5, carried(pos), 0.01f, pos, light);
        g_carSurfaces[0].contactEffect = contact0;
        v->m_bodyContacts.resize(1);
        v->UpdateContactEffects();
        const bool one = v->m_bodyContactEffects[0] >= 0 && v->m_bodyContactEffects[1] == -1;
        v->m_unknownF6C &= ~VEHICLE_SIM_EFFECTS;
        v->UpdateContactEffects();
        v->m_unknownF6C |= VEHICLE_SIM_EFFECTS;
        const bool off = v->m_bodyContactEffects[0] == -1;
        std::printf("effects contact: right %d third none %d switched %d one left %d off %d\n", right, third,
                    switched, one, off);
        v->m_bodyContacts = contacts0;
    }
    FlushEmitters();
    v->m_velocity = velocity0;
    v->m_unknown210 = turn0;
    v->m_speed = speed0;
    v->m_engineRevs = revs0;
    v->m_unknown548 = light0;
}

// --- the skid marks ----------------------------------------------------------------------------

// A strip segment's place, direction (its rotation's z axis) and scale.
struct S_Segment {
    S_vector pos, dir, scale;
    bool on;
};
S_Segment Segment(const I3D_frame *f)
{
    const S_matrix r = f->m_rot.RotationMatrix();
    return {f->m_localMatrix(3), r(2), f->m_scale, const_cast<I3D_frame *>(f)->IsOn()};
}

// A wheel's skid marks laid along set contacts (every 0.1 m, the wheel sliding on flat ground at
// 5 m/s along the path; the vehicle at rest, restored afterwards), checked by the strip they
// leave:
// - straight on: a mark from the first contact (lifted 0.05), segments of 0.3 m end to end along
//   the path, the corners between them without width;
// - round an arc (3 m): the segments along the path, turning, each next one's start edge through
//   its corner piece's pivot (within 1 % of the width), the corners open, nearly as long as the
//   strip is wide;
// - a mark ends past 5 m and off the ground, on a surface without marks and on water; one turned
//   back ends and a new one starts.
void SkidTest(C_Vehicle *v)
{
    int wheel = -1;
    for (int i = 0; i < v->m_wheelCount && wheel < 0; ++i)
        if (v->m_wheels[i] && !(v->m_wheels[i]->flags & WHEEL_DUMMY) && !v->m_wheels[i]->skidMarks.empty())
            wheel = i;
    if (wheel < 0) {
        std::printf("skids: none\n");
        return;
    }
    S_VehicleWheel *w = v->m_wheels[wheel];
    const uint32_t flags0 = w->flags;
    const S_vector4 point0 = w->unknown050;
    const S_vector normal0 = w->unknown080, velocity0 = w->unknown0A0;
    S_CollisionVolume *ground0 = w->ground;
    const float slip0 = w->unknown21C;
    S_CollisionVolume ground = {};
    const auto reset = [&] {
        for (S_VehicleSkidMark &m : w->skidMarks) {
            m.frame0->SetOn(false);
            m.frame1->SetOn(false);
            m.active = m.fresh = m.cornerShown = false;
        }
        w->skidMark = 0;
    };
    reset();
    w->flags = (flags0 | WHEEL_ON_GROUND) & ~WHEEL_DUMMY;
    w->ground = &ground;
    w->unknown21C = std::max(slip0, 0.0f);
    const S_vector up(0.0f, 1.0f, 0.0f);
    w->unknown080 = up;
    const float half = w->width * 0.5f;
    const auto lay = [&](const S_vector &at, const S_vector &along) {
        w->unknown050 = {at.x, at.y, at.z, 1.0f};
        w->unknown0A0 = along * 5.0f;
        v->LaySkidMark(wheel);
    };
    // The closed segments from the first mark's start (ring index 0) to the current.
    const auto closed = [&] {
        std::vector<int> list;
        for (uint32_t k = 0; k < w->skidMark; ++k)
            list.push_back(static_cast<int>(k));
        return list;
    };

    // Straight on: 3 m along +z.
    const S_vector origin(1.0f, 2.0f, 3.0f), ahead(0.0f, 0.0f, 1.0f);
    for (int k = 0; k <= 30; ++k)
        lay(origin + ahead * (0.1f * k), ahead);
    int straight = 0, contiguous = 0, along = 0, flat = 0;
    const std::vector<int> segments = closed();
    for (size_t j = 0; j < segments.size(); ++j) {
        const S_Segment a = Segment(w->skidMarks[segments[j]].frame0);
        straight += Near(a.scale, S_vector(w->width, 1.0f, 0.3f), 1e-5f) && !w->skidMarks[segments[j]].active;
        along += a.dir.Dot(ahead) > 0.99999f;
        if (j == 0)
            contiguous += Near(a.pos, origin + up * 0.05f, 1e-5f);
        else {
            const S_Segment before = Segment(w->skidMarks[segments[j - 1]].frame0);
            contiguous += Near(a.pos, before.pos + before.dir * 0.3f, 1e-4f);
            const S_Segment corner = Segment(w->skidMarks[segments[j - 1]].frame1);
            flat += corner.on && std::fabs(corner.scale.x) < 1e-4f;
        }
    }
    std::printf("skids straight: closed %zu right %d contiguous %d along %d corners flat %d of %zu\n", segments.size(),
                straight, contiguous, along, flat, segments.size() > 0 ? segments.size() - 1 : 0);

    // Round an arc of 3 m radius (turning left, the centre at -x).
    reset();
    const float radius = 3.0f;
    const auto arc = [&](float s) {
        const float a = s / radius;
        return origin + S_vector(radius * (std::cos(a) - 1.0f), 0.0f, radius * std::sin(a));
    };
    const auto tangent = [&](float s) { return S_vector(-std::sin(s / radius), 0.0f, std::cos(s / radius)); };
    for (int k = 0; k <= 40; ++k)
        lay(arc(0.1f * k), tangent(0.1f * k));
    const std::vector<int> bend = closed();
    int arcRight = 0, joined = 0, turning = 0, corners = 0, shaped = 0;
    for (size_t j = 0; j < bend.size(); ++j) {
        const S_Segment a = Segment(w->skidMarks[bend[j]].frame0);
        // Along the path: its start on the arc's side and its direction close to the tangent there.
        arcRight += Near(a.scale, S_vector(w->width, 1.0f, 0.3f), 1e-5f) && a.dir.Dot(tangent(0.3f * j)) > 0.98f;
        if (j == 0)
            continue;
        const S_Segment before = Segment(w->skidMarks[bend[j - 1]].frame0);
        turning += before.dir.Cross(a.dir).y < 0.0f;      // (turning towards -x: LS3D's frame is left-handed)
        const S_Segment corner = Segment(w->skidMarks[bend[j - 1]].frame1);
        S_vector left = a.dir.Cross(up);
        left.Normalize();
        left = left * half;
        // (Within 1 % of the width: the start is set off from the pivot across the old direction.)
        joined += corner.on && (Near(a.pos + left, corner.pos, w->width * 0.01f) ||
                                Near(a.pos - left, corner.pos, w->width * 0.01f));
        corners += corner.scale.x > 1e-4f && corner.scale.z <= w->width && corner.scale.z > w->width * 0.99f;
        // The corner (turning left): at the end's left corner of the segment before, as wide as the
        // gap between its right corner and the next segment's, as long as from the pivot to the
        // gap's middle, aimed there (within 1 % of the width).
        const S_vector end = before.pos + before.dir * 0.3f;
        S_vector beforeLeft = before.dir.Cross(up);
        beforeLeft.Normalize();
        beforeLeft = beforeLeft * half;
        const S_vector outerBefore = end - beforeLeft, outerNext = a.pos - left;
        const S_vector gap = outerNext - outerBefore;
        const S_vector middle = (outerBefore + outerNext) * 0.5f;
        const S_vector reach = middle - corner.pos;
        const float tolerance = w->width * 0.01f;
        shaped += Near(corner.pos, end + beforeLeft, 1e-4f) && std::fabs(corner.scale.x - std::sqrt(gap.Dot(gap))) < tolerance &&
                  std::fabs(corner.scale.z - std::sqrt(reach.Dot(reach))) < tolerance &&
                  corner.dir.Dot(reach) > std::sqrt(reach.Dot(reach)) * 0.999f;
    }
    std::printf("skids arc: closed %zu right %d joined %d turning %d corners %d shaped %d of %zu\n", bend.size(),
                arcRight, joined, turning, corners, shaped, bend.size() > 0 ? bend.size() - 1 : 0);

    // 16 m straight on: the ring (50 segments) wraps round, every segment on.
    reset();
    uint32_t most = 0;
    bool wrapped = false;
    for (int k = 0; k <= 160; ++k) {
        lay(origin + ahead * (0.1f * k), ahead);
        wrapped |= w->skidMark < most;
        most = std::max(most, w->skidMark);
    }
    int on = 0;
    for (const S_VehicleSkidMark &m : w->skidMarks)
        on += m.frame0->IsOn();
    std::printf("skids ring: %zu wrapped %d at %u on %d\n", w->skidMarks.size(), wrapped, w->skidMark, on);

    // The ends: past 5 m, turned back (a new mark), off the ground, no marks, water.
    const auto ended = [&](const auto &how) {
        reset();
        lay(origin, ahead);
        lay(origin + ahead * 0.1f, ahead);
        const uint32_t at = w->skidMark;
        how();
        return !w->skidMarks[at].active && w->skidMark == at + 1;
    };
    const bool distant = ended([&] { lay(origin + ahead * 6.0f, ahead); });
    reset();
    lay(origin, ahead);
    lay(origin + ahead * 0.1f, ahead);
    lay(origin + ahead * 0.2f, -ahead);
    const bool back = !w->skidMarks[0].active && w->skidMark == 1 && w->skidMarks[1].active &&
                      w->skidMarks[1].fresh && Near(Segment(w->skidMarks[1].frame0).pos, origin + ahead * 0.2f + up * 0.05f, 1e-5f);
    const bool off = ended([&] {
        w->flags &= ~WHEEL_ON_GROUND;
        lay(origin + ahead * 0.2f, ahead);
        w->flags |= WHEEL_ON_GROUND;
    });
    const bool bare = ended([&] {
        g_carSurfaces[0].skidMarks = 0;
        lay(origin + ahead * 0.2f, ahead);
        g_carSurfaces[0].skidMarks = 1;
    });
    const bool water = ended([&] {
        ground.flags = COLLISION_FLAG_WATER;
        lay(origin + ahead * 0.2f, ahead);
        ground.flags = 0;
    });
    std::printf("skids end: far %d back %d off ground %d no marks %d water %d\n", distant, back, off, bare, water);

    // Stretching: 4 cm leaves the new segment, 6 cm stretches it, 38 cm closes it at 0.3 m and
    // stretches the next 8 cm.
    reset();
    lay(origin, ahead);
    lay(origin + ahead * 0.04f, ahead);
    const bool still = Near(Segment(w->skidMarks[0].frame0).scale, S_vector(w->width, 1.0f, 0.01f), 1e-6f);
    lay(origin + ahead * 0.06f, ahead);
    const bool stretched = w->skidMark == 0 &&
                           Near(Segment(w->skidMarks[0].frame0).scale, S_vector(w->width, 1.0f, 0.06f), 1e-5f);
    lay(origin + ahead * 0.38f, ahead);
    const bool closedNext = w->skidMark == 1 &&
                            Near(Segment(w->skidMarks[0].frame0).scale, S_vector(w->width, 1.0f, 0.3f), 1e-5f) &&
                            Near(Segment(w->skidMarks[1].frame0).scale, S_vector(w->width, 1.0f, 0.08f), 1e-4f);
    std::printf("skids stretch: still %d stretched %d closed %d\n", still, stretched, closedNext);

    reset();
    w->flags = flags0;
    w->unknown050 = point0;
    w->unknown080 = normal0;
    w->unknown0A0 = velocity0;
    w->ground = ground0;
    w->unknown21C = slip0;
}

// After an Update (UpdateSteering at steering 0): each wheel holder on the model turned from its
// rest about its y axis by the toe, left (even) +toe and right -toe in S_quat::Make's sense; the
// fronts turned in (towards the middle) or out.
void ToeTest(C_Vehicle *v)
{
    v->Update(83);
    int wheels = 0, off = 0, in = 0, out = 0;
    float toe = 0.0f;
    for (int i = 0; i < v->m_wheelCount; ++i) {
        const S_VehicleWheel *w = v->m_wheels[i];
        if (!w || (w->flags & WHEEL_DUMMY) || w->holder->m_parent != v->m_model)
            continue;
        const S_matrix rest = w->unknown000.RotationMatrix();
        const S_matrix now = w->holder->m_rot.RotationMatrix();
        const S_vector restZ = rest(2), nowZ = now(2);
        const float turn = std::atan2(-restZ.Cross(nowZ).Dot(rest(1)), restZ.Dot(nowZ));
        const float expected = i % 2 ? -w->unknown178 : w->unknown178;
        ++wheels;
        off += std::fabs(turn - expected) > 1e-5f;
        const float sideways = (nowZ.x - restZ.x) * (w->modelPos.x < 0.0f ? 1.0f : -1.0f);
        in += sideways > 1e-6f;
        out += sideways < -1e-6f;
        toe = w->unknown178;
    }
    std::printf("toe: %.4f rad wheels %d off %d in %d out %d\n", toe, wheels, off, in, out);
}

// Driving 10 s at full throttle over the surface's bumps: each grounded wheel's cycle (a smooth
// 10..20 m, a bump 1..2 m, a height 1..2 cm either way, Setup's defaults) in range, the bump
// (unknown25C[2]) 0 or a drawn height, the cycles run (the wheel's distance in the cycle reset),
// the bumped share of the steps (about 1.5 / 16.5).
void BumpTest(C_Vehicle *v)
{
    v->m_velocity.x = v->m_velocity.y = v->m_velocity.z = 0.0f;
    v->m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
    int samples = 0, outOfRange = 0, cycles = 0, bumped = 0;
    std::vector<float> last(v->m_wheelCount, 0.0f), heights(v->m_wheelCount, 0.0f), previous(v->m_wheelCount, 0.0f);
    for (int step = 0; step < 120; ++step) {
        v->SetThrottle(1.0f, true);
        v->SetBrake(0.0f);
        v->Step(83);
        for (int i = 0; i < v->m_wheelCount; ++i)
            if (v->m_wheels[i])
                heights[i] = v->m_wheels[i]->unknown268;
        v->Update(83);
        UpdateCarEffectEmitters(83);
        for (int i = 0; i < v->m_wheelCount; ++i) {
            const S_VehicleWheel *w = v->m_wheels[i];
            if (!w || (w->flags & (WHEEL_DUMMY | WHEEL_DETACHED)) || !(w->flags & WHEEL_ON_GROUND))
                continue;
            ++samples;
            cycles += w->unknown26C < last[i];
            last[i] = w->unknown26C;
            if (w->unknown26C > 1000.0f)                // the first cycle (Start's 1000) not yet drawn
                continue;
            // (The bump: none, this cycle's height, or the one before the last draw: a cycle drawn
            // past its smooth time keeps the old height through the new one's smooth time.)
            if (w->unknown268 != heights[i])
                previous[i] = heights[i];
            const float h = std::fabs(w->unknown268);
            outOfRange += !(w->unknown25C[0] >= 10.0f && w->unknown25C[0] <= 20.0f) ||
                          !(w->unknown25C[1] >= 1.0f && w->unknown25C[1] <= 2.0f) || !(h >= 0.01f && h <= 0.02f) ||
                          !(w->unknown25C[2] == 0.0f || w->unknown25C[2] == w->unknown268 ||
                            w->unknown25C[2] == heights[i] || w->unknown25C[2] == previous[i]);
            bumped += w->unknown25C[2] != 0.0f;
        }
    }
    std::printf("bumps: samples %d cycles %d out of range %d bumped share %.3f\n", samples, cycles, outOfRange,
                samples ? static_cast<float>(bumped) / samples : 0.0f);
}

// After an Update: every track strip vertex of a wheel at the wheel holder's height plus its offset
// (in the strip's parent), the strips' texture moved on by the scroll, the scroll by the engine's speed.
void TrackTest(C_Vehicle *v)
{
    int vertices = 0, off = 0, strips = 0, scrolled = 0;
    std::map<const I3D_frame *, std::vector<float>> before;
    for (const auto *tracks : {&v->m_tracksL, &v->m_tracksR})
        for (const S_VehicleTrack &t : *tracks) {
            const S_StubFrame &s = Stub(t.strip);
            std::vector<float> u;
            for (size_t k = 0; k < s.vertices.size(); k += 8)
                u.push_back(s.vertices[k + 6]);
            before[t.strip] = u;
        }
    g_meshRefreshes = 0;
    v->Update(83);
    for (const auto *tracks : {&v->m_tracksL, &v->m_tracksR}) {
        const float scroll = tracks == &v->m_tracksL ? v->m_unknown51C : v->m_unknown520;
        for (const S_VehicleTrack &t : *tracks) {
            const S_StubFrame &s = Stub(t.strip);
            const S_matrix &m = t.strip->m_localMatrix;
            const float holderY = v->m_wheels[t.wheel]->holder->m_localMatrix(3).y;
            for (size_t k = 0; k < t.vertices.size(); ++k) {
                const float *p = &s.vertices[t.vertices[k] * 8];
                const S_vector world = S_vector(p[0], p[1], p[2]) * m;
                ++vertices;
                off += std::fabs(world.y - (holderY + t.offsets[k])) > 1e-4f;
            }
            const std::vector<float> &u = before[t.strip];
            if (u.empty())
                continue;
            ++strips;
            bool ok = true;
            for (size_t k = 0; k < u.size(); ++k)
                ok &= s.vertices[k * 8 + 6] == u[k] + scroll;
            scrolled += ok;
            before[t.strip].clear();                    // each strip once
        }
    }
    // The scroll against the engine's speed (+0x229F, as Update leaves it: the revs over the gear
    // ratio round the largest wheel; x +0x518 x the step, less whole turns).
    const float engineSpeed = v->m_engineRevs / v->m_gearRatios[v->m_gear + 1] * v->m_maxWheelCircumference;
    const float along = (v->m_gear < 0 ? -engineSpeed : engineSpeed) * v->m_unknown518 * 0.083f;
    float scrollError = 0.0f;
    for (const float scroll : {v->m_unknown51C, v->m_unknown520}) {
        const float d = scroll - along;
        scrollError = std::max(scrollError, std::fabs(d - std::round(d)));
    }
    std::printf("tracks: vertices %d off %d strips %d scrolled %d refreshes %d scroll error %.3f\n", vertices, off,
                strips, scrolled, g_meshRefreshes, scrollError);
}

// The body box's corners in the world, the farthest along `dir`.
float Farthest(const C_Vehicle *v, const S_vector &dir)
{
    float farthest = -1e30f;
    for (int i = 0; i < 8; ++i) {
        const S_vector c(i & 1 ? v->m_bodyMax.x : v->m_bodyMin.x, i & 2 ? v->m_bodyMax.y : v->m_bodyMin.y,
                         i & 4 ? v->m_bodyMax.z : v->m_bodyMin.z);
        farthest = std::max(farthest, (c * v->m_modelMatrix).Dot(dir));
    }
    return farthest;
}

// The body points' spheres in the world, the farthest reach along `dir`.
float PointsFarthest(const C_Vehicle *v, const S_vector &dir)
{
    float farthest = -1e30f;
    std::vector<const S_BodyCollisionBox *> todo = {v->m_bodyCollision};
    while (!todo.empty()) {
        const S_BodyCollisionBox *b = todo.back();
        todo.pop_back();
        for (; b; b = b->next) {
            const S_matrix &m = b->parent ? b->parent->GetMatrix() : v->m_modelMatrix;
            for (const S_BodyCollisionPoint *p = b->points; p; p = p->next)
                farthest = std::max(farthest, (p->pos * m).Dot(dir) + p->radius);
            todo.push_back(b->children);
        }
    }
    return farthest;
}

void Crash(C_Vehicle *v)
{
    v->m_velocity.x = v->m_velocity.y = v->m_velocity.z = 0.0f;
    v->m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
    S_vector heading(v->m_modelAxisZ.x, 0.0f, v->m_modelAxisZ.z);
    heading.Normalize();
    const float front = Farthest(v, heading);
    g_wallVolume.flags = 0x02000000;
    g_wallFace = {-heading, -(front + 15.0f), &g_wallVolume};
    g_wall = true;
    g_bodyCollisions = g_deforms = g_partHits = g_impactSounds = g_sphereTests = g_boxTests = g_boxTouches = 0;
    g_ignores = g_ignoreClears = 0;
    float into = -1e30f, pointsInto = -1e30f, impact = 0.0f, peak = 0.0f, lowest = 1e9f;
    int firstContact = -1, stoppedSteps = 0;
    float second = 0.0f;                                // the speed a second after the first contact
    const float height0 = v->m_modelPos.y;
    const char *trace = std::getenv("CRASH_TRACE");     // a model's name: trace its crash
    const bool traced = trace && g_modelFile.find(trace) != std::string::npos;
    for (int step = 0; step < 300; ++step) {
        const float before = v->m_velocity.x * heading.x + v->m_velocity.z * heading.z;
        const int sounds = g_impactSounds;
        v->SetThrottle(1.0f, true);
        v->SetBrake(0.0f);
        v->Step(83);
        v->Update(83);
        UpdateCarEffectEmitters(83);
        const float along = v->m_velocity.x * heading.x + v->m_velocity.z * heading.z;
        peak = std::max(peak, along);
        // How far the body box's front is past the wall.
        into = std::max(into, Farthest(v, heading) - (front + 15.0f));
        pointsInto = std::max(pointsInto, PointsFarthest(v, heading) - (front + 15.0f));
        lowest = std::min(lowest, v->m_modelAxisY.y);
        g_bodyCollisions += v->m_unknown2288 != 0;
        // A contact on any substep (the body's impact sound; m_unknown2288 is the last substep's).
        if (firstContact < 0 && g_impactSounds > sounds) {
            firstContact = step;
            impact = before;
        }
        if (firstContact >= 0 && step == firstContact + 12)
            second = along;
        if (firstContact >= 0 && step >= firstContact + 24 && std::fabs(along) < 0.5f)
            ++stoppedSteps;
        if (traced)
            std::printf("crash %d speed %.3f gap %.3f points gap %.3f collided %d sounds %d contacts %zu up %.4f dy %.3f gear %d\n",
                        step, along, (front + 15.0f) - Farthest(v, heading), (front + 15.0f) - PointsFarthest(v, heading),
                        v->m_unknown2288, g_impactSounds - sounds, v->m_bodyContacts.size(), v->m_modelAxisY.y,
                        v->m_modelPos.y - height0, v->m_gear);
    }
    g_wall = false;
    std::printf("crash stop: %.3f m/s a second after the first contact, tracked %d\n", second,
                !v->m_tracksL.empty());
    std::printf("crash: first contact step %d impact %.3f m/s peak %.3f m/s past wall max %.4f (points %.4f) upright min %.5f stopped steps %d of %d collisions %d box tests %d touched %d sphere tests %d deforms %d (part hits %d) impact sounds %d ignores %d/%d\n",
                firstContact, impact, peak, into, pointsInto, lowest, stoppedSteps,
                firstContact < 0 ? 0 : 300 - firstContact - 24, g_bodyCollisions, g_boxTests, g_boxTouches,
                g_sphereTests, g_deforms, g_partHits, g_impactSounds, g_ignores, g_ignoreClears);
}

// --- the damage (the real Deform) ------------------------------------------------------------------

// The damage callbacks (the car's as Report sets them): what they were given.
struct S_DamageLog {
    std::vector<int> partsOff, wheelsOff, glass;
    std::vector<S_vector> partOffVelocities, wheelOffVelocities;
    std::vector<const S_vector *> partOffPushes, wheelOffPushes;
    std::vector<std::string> glassSounds;
    std::map<const I3D_frame *, int> hits, dents, types;
    std::map<const I3D_frame *, S_vector> impulses;
    std::map<const I3D_frame *, S_vector4> points;
} g_damage;
bool __fastcall F_PartOff(void *, int index, const S_vector *velocity, const S_vector *push)
{
    g_damage.partsOff.push_back(index);
    g_damage.partOffVelocities.push_back(*velocity);
    g_damage.partOffPushes.push_back(push);
    return true;
}
bool __fastcall F_WheelOff(void *, int index, const S_vector *velocity, const S_vector *push)
{
    g_damage.wheelsOff.push_back(index);
    g_damage.wheelOffVelocities.push_back(*velocity);
    g_damage.wheelOffPushes.push_back(push);
    return true;
}
void __fastcall F_GlassBroken(void *, int part, const S_vector *, const char *sound)
{
    g_damage.glass.push_back(part);
    g_damage.glassSounds.push_back(sound ? sound : "(null)");
}
void __fastcall F_PartHit(void *, I3D_frame *frame, int type, const S_vector4 *point, const S_vector *impulse,
                          bool dented)
{
    ++g_damage.hits[frame];
    g_damage.dents[frame] += dented;
    g_damage.types[frame] = type;
    g_damage.impulses[frame] = *impulse;
    g_damage.points[frame] = *point;
    g_deforms += dented;
    ++g_partHits;
}

const S_Mesh *MeshOf(const S_VehiclePart &p) { return reinterpret_cast<const S_Mesh *>(p.mesh); }

// A mesh's positions per level (the first two).
std::vector<std::vector<S_vector>> Positions(const S_Mesh *m)
{
    std::vector<std::vector<S_vector>> out;
    for (uint32_t l = 0; l < std::min<uint32_t>(m->levelCount, 2); ++l) {
        const S_Level *level = m->levels[l];
        std::vector<S_vector> v;
        for (int i = 0; i < level->count; ++i)
            v.emplace_back(level->data[i * 8], level->data[i * 8 + 1], level->data[i * 8 + 2]);
        out.push_back(v);
    }
    return out;
}

S_vector4 Point4(const S_vector &v) { return {v.x, v.y, v.z, 1.0f}; }

S_vector CarriedAt(const C_Vehicle *v, const S_vector &p)
{
    const float x = p.x - v->m_unknown1B0.x, y = p.y - v->m_unknown1B0.y, z = p.z - v->m_unknown1B0.z;
    const S_vector &t = v->m_unknown210;
    return S_vector(v->m_velocity.x - (z * t.y - y * t.z), v->m_velocity.y - (x * t.z - z * t.x),
                    v->m_velocity.z - (y * t.x - x * t.y));
}

// A part's health after an impact of `strength` (Deform's).
float HealthAfter(const S_VehiclePart &p, float strength)
{
    const float taken = strength - p.absorbed * 0.01f * strength;
    const float h = p.health - taken / p.fullImpulse * p.fullHealth;
    return h < 0.0f ? 0.0f : h;
}

// The real Deform and its steps, with the car at rest at its setup pose (all restored after):
// - dents (wear off): the part with the most of two levels and vertices hit at one of its vertices
//   (0.3 m): in both levels each vertex in reach (in the part's space) moved along the direction
//   turned into the part (each axis within maxDent) x its share of the impulse x one of 0.7..0.97,
//   or to maxDent from the original; those out of reach (every part) unchanged; the original kept
//   once (its levels, the first two), the part marked; the hit callback (impulse, type, point);
//   again: no second original, every vertex within maxDent of it; repaired: every part's vertices
//   back, no original left; under the least impulse nothing, with DEFORM_ANY_STRENGTH a dent;
// - wear: the part's health as Deform's formula (once per step, then again), at none it comes
//   off (velocity, push) with its lamps; DEFORM_NO_WEAR keeps it; a glass part cracks (the
//   textures swapped, references kept) then shatters with the sound group's name (or null); a door
//   gone breaks its windows; a bumper passes on what it does not absorb; a wheel's health (once per
//   step), at none it comes off; a lamp's health, broken below its level, a shared one's projector
//   side; a light part's lamps share its health;
// - a synthetic group: its visual for its health (none while whole enough), the main one hidden,
//   the door's frame, the parts on it gone.
void DeformTest(C_Vehicle *v)
{
    if (v->m_parts.empty()) {
        std::printf("deform: no parts\n");
        return;
    }
    const uint32_t savedF6C = v->m_unknownF6C;
    const S_vector4 savedVelocity = v->m_velocity;
    const S_vector savedTurn = v->m_unknown210;
    const uint8_t saved2298 = v->m_unknown2298;
    char savedSounds[2][VEHICLE_SOUND_NAME];
    std::memcpy(savedSounds[0], v->m_params.sounds1_9E4[8], VEHICLE_SOUND_NAME);
    std::memcpy(savedSounds[1], v->m_params.sounds2_E80[8], VEHICLE_SOUND_NAME);
    v->m_unknownF6C &= ~VEHICLE_SIM_IMPACT_DAMAGE;
    std::srand(7);
    // (The shipped bumpers as bodies meanwhile: one in reach would pass on less to the others.)
    std::vector<int> shippedBumpers;
    for (size_t i = 0; i < v->m_parts.size(); ++i)
        if (v->m_parts[i].type == VEHICLE_PART_BUMPER) {
            shippedBumpers.push_back(static_cast<int>(i));
            v->m_parts[i].type = VEHICLE_PART_BODY;
        }

    int P = -1;
    const auto score = [](const S_VehiclePart &q) {
        return std::make_pair(std::min<uint32_t>(MeshOf(q)->levelCount, 2), MeshOf(q)->levels[0]->count);
    };
    for (size_t i = 0; i < v->m_parts.size(); ++i)
        if (!v->m_parts[i].frames && (P < 0 || score(v->m_parts[i]) > score(v->m_parts[P])))
            P = static_cast<int>(i);
    S_VehiclePart &part = v->m_parts[P];
    const S_Mesh *mesh = MeshOf(part);
    const int levels = static_cast<int>(std::min<uint32_t>(mesh->levelCount, 2));
    const float radius = 0.3f;
    const S_matrix world = part.frame->GetMatrix();
    const S_vector anchor = Positions(mesh)[0][mesh->levels[0]->count / 2];
    const S_vector point = anchor * world;
    S_vector dir(0.36f, -0.48f, 0.8f);
    const float strength = part.minImpulse + 1.6f * part.fullImpulse;   // (the share mostly past 1)
    std::vector<std::vector<std::vector<S_vector>>> before;
    for (const S_VehiclePart &p : v->m_parts)
        before.push_back(Positions(MeshOf(p)));
    g_damage = S_DamageLog();
    const size_t originals0 = v->m_meshOriginals.size();
    const int refreshes0 = g_meshRefreshes;
    v->Deform(Point4(point), Point4(dir), radius, strength, 0, nullptr);
    const int refreshes = g_meshRefreshes - refreshes0;

    // The dent in the part's space.
    S_matrix inverse = world;
    inverse.Inverse(world);
    const S_vector local = point * inverse;
    inverse.m[3][0] = inverse.m[3][1] = inverse.m[3][2] = 0.0f;
    S_vector dent = dir * inverse;
    const float limits[3] = {part.maxDent.x, part.maxDent.y, part.maxDent.z};
    float *d = &dent.x;
    for (int c = 0; c < 3; ++c)
        if (limits[c] < std::fabs(d[c]))
            d[c] = d[c] > 0.0f ? limits[c] : -limits[c];
    const float taken = strength - part.absorbed * 0.01f * strength;
    const float share = std::min(1.0f, taken / part.fullImpulse);
    int reach = 0, total = 0, moved = 0, clamped = 0, off = 0, outside = 0, dentedParts = 0;
    std::set<int> factors;
    const std::vector<std::vector<S_vector>> after = Positions(mesh);
    for (int l = 0; l < levels; ++l)
        for (size_t i = 0; i < after[l].size(); ++i) {
            const S_vector &o = before[P][l][i], &n = after[l][i];
            const S_vector g = o - local;
            ++total;
            if (!((g.x * g.x + g.z * g.z) + g.y * g.y < radius * radius)) {
                off += n != o;
                continue;
            }
            ++reach;
            const float delta[3] = {n.x - o.x, n.y - o.y, n.z - o.z};
            float factor = -1.0f;
            bool ok = true;
            for (int c = 0; c < 3; ++c) {
                if (std::fabs(delta[c]) == limits[c]) {
                    ++clamped;
                    continue;
                }
                const float expected = d[c] * share;
                if (std::fabs(expected) < 1e-4f) {
                    ok &= std::fabs(delta[c]) < 1e-5f;
                    continue;
                }
                const float f = delta[c] / expected;
                if (factor < 0.0f)
                    factor = f;
                ok &= std::fabs(f - factor) < 2e-3f;
            }
            const int k = static_cast<int>(std::lround((factor - 0.7f) / 0.03f));
            ok &= factor < 0.0f || (k >= 0 && k <= 9 && std::fabs(factor - (0.7f + 0.03f * k)) < 2e-3f);
            if (factor >= 0.0f)
                factors.insert(k);
            moved += ok;
        }
    // The other parts: out of reach unchanged. (The refreshes: per part dented each changed level, the
    // mesh and the visual.)
    int expectedRefreshes = 0;
    for (size_t j = 0; j < v->m_parts.size(); ++j) {
        const auto levelsNow = Positions(MeshOf(v->m_parts[j]));
        int changedLevels = 0;
        for (size_t l = 0; l < levelsNow.size(); ++l)
            changedLevels += levelsNow[l] != before[j][l];
        expectedRefreshes += changedLevels ? changedLevels + 2 : 0;
        if (static_cast<int>(j) == P)
            continue;
        const S_VehiclePart &q = v->m_parts[j];
        S_matrix qi = q.frame->GetMatrix();
        qi.Inverse(q.frame->GetMatrix());
        const S_vector ql = point * qi;
        const auto now = Positions(MeshOf(q));
        bool changed = false;
        for (size_t l = 0; l < now.size(); ++l)
            for (size_t i = 0; i < now[l].size(); ++i) {
                const S_vector g = before[j][l][i] - ql;
                const bool in = (g.x * g.x + g.z * g.z) + g.y * g.y < radius * radius;
                outside += !in && now[l][i] != before[j][l][i];
                changed |= now[l][i] != before[j][l][i];
            }
        dentedParts += changed;
    }
    // The original kept.
    int kept = 0, keptRefs = 0, keptLevels = 0, keptDropped = 0, keptSame = 0;
    for (const S_VehicleMeshOriginal &o : v->m_meshOriginals) {
        if (o.partName != part.name || o.modelFile != v->m_modelFileName)
            continue;
        ++kept;
        keptRefs = o.refs;
        const auto *om = reinterpret_cast<const S_Mesh *>(o.mesh);
        keptLevels = static_cast<int>(om->levelCount);
        keptDropped = om->levelsDropped;
        keptSame = Positions(om) == before[P];
    }
    const bool hitOk = g_damage.hits[part.frame] == 1 && g_damage.dents[part.frame] == 1 &&
                       g_damage.types[part.frame] == part.type &&
                       g_damage.impulses[part.frame] == S_vector(strength * dir.x, strength * dir.y, strength * dir.z) &&
                       S_vector(g_damage.points[part.frame].x, g_damage.points[part.frame].y,
                                g_damage.points[part.frame].z) == point;
    std::printf("deform dent: levels %d of %u reach %d of %d moved %d clamped %d factors %zu off %d other parts %d "
                "out %d originals %zu kept %d refs %d levels %d dropped %d same %d marked %d hit %d bits %X health %d "
                "refreshes %d\n",
                levels, mesh->levelCount, reach, total, moved, clamped, factors.size(), off, dentedParts, outside,
                v->m_meshOriginals.size() - originals0, kept, keptRefs, keptLevels, keptDropped, keptSame,
                (part.state & VEHICLE_PART_DEFORMED) != 0, hitOk, v->m_unknown58C & 3,
                part.health == part.fullHealth && !(part.state & VEHICLE_PART_STATE_2),
                refreshes == expectedRefreshes);

    // Again (30 hits over 1 m): no second original; every vertex within maxDent of the original,
    // some at it.
    for (int n = 0; n < 30; ++n)
        v->Deform(Point4(point), Point4(dir), 1.0f, strength, 0, nullptr);
    int partOriginals = 0;
    for (const S_VehicleMeshOriginal &o : v->m_meshOriginals)
        partOriginals += o.partName == part.name;
    // (Within a rounding of the limit: the original's coordinate plus the limited offset.)
    // (Each offset only grows: every step is along the direction.)
    int within = 0, count = 0, atLimit = 0, grown = 0;
    const auto again = Positions(mesh);
    for (int l = 0; l < levels; ++l)
        for (size_t i = 0; i < again[l].size(); ++i) {
            const S_vector g = again[l][i] - before[P][l][i];
            const S_vector g1 = after[l][i] - before[P][l][i];
            const float a[3] = {std::fabs(g.x), std::fabs(g.y), std::fabs(g.z)};
            const float a1[3] = {std::fabs(g1.x), std::fabs(g1.y), std::fabs(g1.z)};
            ++count;
            bool in = true, limited = false, grew = true;
            for (int c = 0; c < 3; ++c) {
                in &= a[c] <= limits[c] + 1e-5f;
                limited |= limits[c] > 0.0f && std::fabs(a[c] - limits[c]) <= 1e-5f;
                grew &= a[c] >= a1[c] - 1e-6f;
            }
            within += in;
            atLimit += limited;
            grown += grew;
        }
    std::printf("deform again: originals %d refs %d within %d of %d at the limit %d grown %d\n", partOriginals,
                keptRefs, within, count, atLimit, grown);

    // Repaired: every part's vertices back, no original left.
    v->RepairParts(false);
    int restored = 0, marked = 0;
    for (size_t j = 0; j < v->m_parts.size(); ++j) {
        restored += Positions(MeshOf(v->m_parts[j])) == before[j];
        marked += (v->m_parts[j].state & VEHICLE_PART_DEFORMED) != 0;
    }
    std::printf("deform repair: restored %d of %zu originals %zu marked %d\n", restored, v->m_parts.size(),
                v->m_meshOriginals.size(), marked);

    // An original's users: the part unmarked (as another part of the name would be) dents again: the
    // same original, two users. Another model file: another original.
    {
        // (The other parts out of it: past their least impulse never.)
        std::vector<float> leastImpulses;
        for (S_VehiclePart &p : v->m_parts) {
            leastImpulses.push_back(p.minImpulse);
            if (&p != &part)
                p.minImpulse = 1e30f;
        }
        const auto originalsOf = [&] {
            std::vector<S_VehicleMeshOriginal *> found;
            for (S_VehicleMeshOriginal &o : v->m_meshOriginals)
                if (o.partName == part.name)
                    found.push_back(&o);
            return found;
        };
        v->Deform(Point4(point), Point4(dir), 1.0f, strength, 0, nullptr);
        part.state &= ~VEHICLE_PART_DEFORMED;
        v->Deform(Point4(point), Point4(dir), 1.0f, strength, 0, nullptr);
        const auto shared = originalsOf();
        const bool users = shared.size() == 1 && shared[0]->refs == 2 && (part.state & VEHICLE_PART_DEFORMED);
        const std::string modelFile = v->m_modelFileName;
        v->m_modelFileName = "other.4ds";
        v->Deform(Point4(point), Point4(dir), 1.0f, strength, 0, nullptr);
        const auto two = originalsOf();
        const bool byModel = two.size() == 2 && two[1]->modelFile == "other.4ds" && two[1]->refs == 1;
        // (Repaired from each: the other model's, then the first with one user.)
        v->RepairParts(false);
        v->m_modelFileName = modelFile;
        for (S_VehicleMeshOriginal *o : originalsOf())
            o->refs = 1;
        part.state |= VEHICLE_PART_DEFORMED;
        v->RepairParts(false);
        int back = 0;
        for (size_t j = 0; j < v->m_parts.size(); ++j) {
            back += Positions(MeshOf(v->m_parts[j])) == before[j];
            v->m_parts[j].minImpulse = leastImpulses[j];
        }
        std::printf("deform originals: users %d by model %d back %d left %zu\n", users, byModel,
                    back == static_cast<int>(v->m_parts.size()), v->m_meshOriginals.size());
    }

    // The least impulse: nothing under it; DEFORM_ANY_STRENGTH dents anyway.
    g_damage = S_DamageLog();
    v->Deform(Point4(point), Point4(dir), radius, part.minImpulse * 0.5f, 0, nullptr);
    const bool weak = Positions(mesh) == before[P] && g_damage.hits[part.frame] == 0;
    v->Deform(Point4(point), Point4(dir), radius, part.minImpulse * 0.5f, DEFORM_ANY_STRENGTH, nullptr);
    const bool forced = Positions(mesh) != before[P] && g_damage.dents[part.frame] == 1;
    v->RepairParts(false);
    std::printf("deform strength: weak untouched %d forced dented %d\n", weak, forced);

    // --- wear -------------------------------------------------------------------------------------
    v->m_unknownF6C |= VEHICLE_SIM_IMPACT_DAMAGE;
    const auto clearStep = [&] {
        for (S_VehiclePart &p : v->m_parts)
            p.state &= ~VEHICLE_PART_STATE_2;
        for (int i = 0; i < v->m_wheelCount; ++i)
            if (v->m_wheels[i])
                v->m_wheels[i]->flags &= ~WHEEL_HIT;
        v->m_unknown58C = 0;
    };
    const auto fresh = [&] {
        v->RepairParts(false);
        v->ResetCondition();
        clearStep();
        g_damage = S_DamageLog();
    };
    fresh();
    // The part's health: once per step, then again.
    const float wearStrength = part.minImpulse + 0.2f * part.fullImpulse;
    const float h1 = HealthAfter(part, wearStrength);
    v->Deform(Point4(point), Point4(dir), radius, wearStrength, 0, nullptr);
    const bool first = part.health == h1 && (part.state & VEHICLE_PART_STATE_2) && (v->m_unknown58C & 3) == 3;
    v->Deform(Point4(point), Point4(dir), radius, wearStrength, 0, nullptr);
    const bool once = part.health == h1;
    clearStep();
    const float h2 = HealthAfter(part, wearStrength);
    v->Deform(Point4(point), Point4(dir), radius, wearStrength, 0, nullptr);
    const bool second = part.health == h2;
    fresh();
    v->Deform(Point4(point), Point4(dir), radius, wearStrength, DEFORM_NO_WEAR, nullptr);
    const bool noWear = part.health == part.fullHealth && !(part.state & VEHICLE_PART_STATE_2);
    // Gone: off with the body's velocity there and the push, its lamps broken.
    fresh();
    v->m_velocity.x = 1.0f, v->m_velocity.y = 2.0f, v->m_velocity.z = 3.0f;
    v->m_unknown210 = S_vector(0.1f, 0.2f, 0.3f);
    const S_vector push(4.0f, 5.0f, 6.0f);
    const uint16_t firstLampPart = v->m_lamps.empty() ? 0 : v->m_lamps[0].part;
    if (!v->m_lamps.empty())
        v->m_lamps[0].part = static_cast<uint16_t>(P);
    v->Deform(Point4(point), Point4(dir), radius, part.minImpulse + 50.0f * part.fullImpulse, 0, &push);
    const auto offAt = std::find(g_damage.partsOff.begin(), g_damage.partsOff.end(), P);
    const size_t offIndex = static_cast<size_t>(offAt - g_damage.partsOff.begin());
    const bool gone = part.health == 0.0f && offAt != g_damage.partsOff.end() &&
                      g_damage.partOffVelocities[offIndex] == CarriedAt(v, part.frame->GetWorldPos()) &&
                      g_damage.partOffPushes[offIndex] == &push;
    int partLamps = 0, partLampsBroken = 0;
    for (const S_VehicleLamp &l : v->m_lamps)
        if (l.part == P) {
            ++partLamps;
            partLampsBroken += (l.flags & VEHICLE_LAMP_BROKEN) && l.health == 0.0f;
        }
    if (!v->m_lamps.empty())
        v->m_lamps[0].part = firstLampPart;
    std::printf("deform wear: first %d once %d second %d no wear %d gone %d lamps %d/%d\n", first, once, second, noWear,
                gone, partLampsBroken, partLamps);

    // Glass: cracked (the textures swapped, one reference each), then shattered with the sound.
    fresh();
    int G = -1;
    for (size_t i = 0; i < v->m_parts.size() && G < 0; ++i)
        if (v->m_parts[i].type == VEHICLE_PART_GLASS && v->m_parts[i].glassTexture && !v->m_parts[i].frames)
            G = static_cast<int>(i);
    if (G >= 0) {
        S_VehiclePart &glass = v->m_parts[G];
        S_Material *material = MeshOf(glass)->levels[0]->faces.material;
        S_Image intact;
        intact.vtable = g_imageVtable;
        intact.name = "intact";
        auto *cracked = reinterpret_cast<S_Image *>(glass.glassTexture);
        material->texture = &intact;
        const int crackedRefs = cracked->refs;
        const S_vector gp = Positions(MeshOf(glass))[0][0] * glass.frame->GetMatrix();
        glass.crackHealth = glass.fullHealth * 0.8f;
        // Half its health: cracked, whole.
        const float s = (0.5f * glass.fullImpulse) / (1.0f - glass.absorbed * 0.01f);
        v->Deform(Point4(gp), Point4(dir), 0.05f, std::max(s, glass.minImpulse * 1.01f), 0, nullptr);
        const bool crackedNow = (glass.state & VEHICLE_PART_GLASS_BROKEN) && material->texture == cracked &&
                                glass.glassTexture == reinterpret_cast<I3D_texture *>(&intact) && intact.refs == 1 &&
                                cracked->refs == crackedRefs && glass.health > 0.0f &&
                                !(glass.state & VEHICLE_PART_STATE_1) && g_damage.glass.empty();
        // Gone: shattered with the second group's ninth name (the first group's when +0x2298; null
        // when empty).
        clearStep();
        v->m_unknown2298 = 0;
        std::strcpy(v->m_params.sounds2_E80[8], "glass2.wav");
        // (Other glass in reach, or the windows of a door gone, shatter as well.)
        const auto soundOf = [&](int index) -> std::string {
            if (std::count(g_damage.glass.begin(), g_damage.glass.end(), index) != 1)
                return "?";
            return g_damage.glassSounds[static_cast<size_t>(
                std::find(g_damage.glass.begin(), g_damage.glass.end(), index) - g_damage.glass.begin())];
        };
        v->Deform(Point4(gp), Point4(dir), 0.05f, glass.minImpulse + 50.0f * glass.fullImpulse, 0, nullptr);
        const bool shattered = glass.health == 0.0f && (glass.state & VEHICLE_PART_STATE_1) &&
                               soundOf(G) == "glass2.wav";
        fresh();
        v->m_unknown2298 = 1;
        v->m_params.sounds1_9E4[8][0] = 0;
        v->Deform(Point4(gp), Point4(dir), 0.05f, glass.minImpulse + 50.0f * glass.fullImpulse, 0, nullptr);
        const bool silent = soundOf(G) == "(null)";
        v->m_unknown2298 = saved2298;
        fresh();
        material->texture = nullptr;
        std::printf("deform glass: cracked %d shattered %d silent %d\n", crackedNow, shattered, silent);
    } else {
        std::printf("deform glass: none\n");
    }

    // A door gone breaks its windows (a glass part moved onto the door's frame when none is).
    fresh();
    int D = -1;
    for (size_t i = 0; i < v->m_parts.size() && D < 0; ++i)
        if (v->m_parts[i].type == VEHICLE_PART_DOOR && !v->m_parts[i].frames)
            D = static_cast<int>(i);
    if (D >= 0 && G >= 0) {
        S_VehiclePart &door = v->m_parts[D];
        I3D_frame *glassParent = v->m_parts[G].frame->m_parent;
        const bool moved = glassParent != door.frame;
        if (moved) {
            Unlink(v->m_parts[G].frame);
            LinkUnder(v->m_parts[G].frame, door.frame);
        }
        std::vector<int> windows;
        for (size_t i = 0; i < v->m_parts.size(); ++i)
            if (v->m_parts[i].type == VEHICLE_PART_GLASS && v->m_parts[i].frame->m_parent == door.frame)
                windows.push_back(static_cast<int>(i));
        const S_vector dp = Positions(MeshOf(door))[0][0] * door.frame->GetMatrix();
        v->Deform(Point4(dp), Point4(dir), 0.001f, door.minImpulse + 50.0f * door.fullImpulse, 0, nullptr);
        int broken = 0;
        for (int w : windows)
            broken += v->m_parts[w].health == 0.0f && (v->m_parts[w].state & VEHICLE_PART_STATE_1) &&
                      (v->m_parts[w].state & VEHICLE_PART_STATE_2) &&
                      std::count(g_damage.glass.begin(), g_damage.glass.end(), w) == 1;
        if (moved) {
            Unlink(v->m_parts[G].frame);
            LinkUnder(v->m_parts[G].frame, glassParent);
        }
        std::printf("deform door: windows %zu broken %d moved %d gone %d\n", windows.size(), broken, moved,
                    door.health == 0.0f);
    } else {
        std::printf("deform door: none\n");
    }

    // A bumper passes on what it does not absorb: a part after it (a lower index) in reach of the
    // same point (2 m) takes the rest (the part made a bumper absorbing half, the shipped bumpers
    // made bodies meanwhile).
    fresh();
    {
        int Q = -1;
        for (int i = P; i-- > 0 && Q < 0;) {
            const S_VehiclePart &p = v->m_parts[i];
            if (p.frames)
                continue;
            S_matrix qi = p.frame->GetMatrix();
            qi.Inverse(p.frame->GetMatrix());
            const S_vector ql = point * qi;
            const auto positions = Positions(MeshOf(p));
            for (const S_vector &x : positions[0])
                if ((x - ql).Magnitude() < 1.9f) {
                    Q = i;
                    break;
                }
        }
        if (Q >= 0) {
            std::vector<int> bumpers;
            for (size_t i = 0; i < v->m_parts.size(); ++i)
                if (v->m_parts[i].type == VEHICLE_PART_BUMPER) {
                    bumpers.push_back(static_cast<int>(i));
                    v->m_parts[i].type = VEHICLE_PART_BODY;
                }
            S_VehiclePart &bumper = v->m_parts[P];
            S_VehiclePart &next = v->m_parts[Q];
            const uint8_t type = bumper.type;
            const float absorbed = bumper.absorbed;
            bumper.type = VEHICLE_PART_BUMPER;
            bumper.absorbed = 50.0f;
            const float s = std::max(bumper.minImpulse, next.minImpulse * 2.0f) * 1.5f + 0.1f * next.fullImpulse;
            const float rest = s - bumper.absorbed * 0.01f * s;
            const float expected = HealthAfter(next, rest);
            const float unreduced = HealthAfter(next, s);
            v->Deform(Point4(point), Point4(dir), 2.0f, s, 0, nullptr);
            const bool passed = next.health == expected && expected != unreduced;
            bumper.type = type;
            bumper.absorbed = absorbed;
            for (int i : bumpers)
                v->m_parts[i].type = VEHICLE_PART_BUMPER;
            std::printf("deform bumper: passed %d\n", passed);
        } else {
            std::printf("deform bumper: none\n");
        }
    }

    // A wheel: its health once per step, at none off with the body's velocity there and no push.
    fresh();
    int W = -1;
    for (int i = 0; i < v->m_wheelCount && W < 0; ++i)
        if (v->m_wheels[i] && !(v->m_wheels[i]->flags & WHEEL_DUMMY))
            W = i;
    if (W >= 0) {
        S_VehicleWheel *w = v->m_wheels[W];
        const S_vector wp(w->worldPos.x, w->worldPos.y, w->worldPos.z);
        const float s = w->minImpulse + 0.3f * w->fullImpulse;
        const float expected = w->health - (s - w->absorbed * 0.01f * s) / w->fullImpulse * w->fullHealth;
        v->Deform(Point4(wp), Point4(dir), 0.01f, w->minImpulse, 0, nullptr);
        const bool weakWheel = w->health == w->fullHealth && !(w->flags & WHEEL_HIT);
        v->Deform(Point4(wp), Point4(dir), 0.01f, s, 0, nullptr);
        const bool hit = w->health == (expected < 0.0f ? 0.0f : expected) && (w->flags & WHEEL_HIT) &&
                         (v->m_unknown58C & 2);
        const float h = w->health;
        v->Deform(Point4(wp), Point4(dir), 0.01f, s, 0, nullptr);
        const bool wheelOnce = w->health == h;
        // 1.5 m off: no wheel within 1 m of it.
        clearStep();
        v->Deform(Point4(S_vector(wp.x, wp.y + 1.5f, wp.z)), Point4(dir), 0.01f, s, 0, nullptr);
        const bool distant = w->health == h;
        clearStep();
        v->m_velocity.x = -1.0f, v->m_velocity.y = 0.5f, v->m_velocity.z = 2.0f;
        v->m_unknown210 = S_vector(0.3f, -0.2f, 0.1f);
        const S_vector push(1.0f, 1.0f, 1.0f);
        v->Deform(Point4(wp), Point4(dir), 0.01f, w->minImpulse + 50.0f * w->fullImpulse, 0, &push);
        const auto at = std::find(g_damage.wheelsOff.begin(), g_damage.wheelsOff.end(), W);
        const size_t k = static_cast<size_t>(at - g_damage.wheelsOff.begin());
        const bool lost = w->health == 0.0f && at != g_damage.wheelsOff.end() &&
                          g_damage.wheelOffVelocities[k] == CarriedAt(v, wp) && g_damage.wheelOffPushes[k] == nullptr;
        std::printf("deform wheel: weak %d hit %d once %d far %d lost %d\n", weakWheel, hit, wheelOnce, distant, lost);
    }

    // A lamp (no light part): its health, broken below its level; a shared one's projector side.
    fresh();
    int lampsHit = 0, lampsRight = 0, sharedSides = 0, sharedLamps = 0, lampsWeak = 0, sharedQuirk = 0, lampsOut = 0;
    for (size_t i = 0; i < v->m_lamps.size(); ++i) {
        S_VehicleLamp &lamp = v->m_lamps[i];
        if (!(lamp.part & 0x8000) && v->m_parts[lamp.part].type == VEHICLE_PART_LIGHT)
            continue;
        fresh();
        const S_vector lp = lamp.frame->GetWorldPos();
        const float s = v->m_lampMinImpulse + 0.1f * v->m_lampFullImpulse;
        const float damage = (s - v->m_lampAbsorbed * 0.01f * s) / v->m_lampFullImpulse * v->m_lampFullHealth;
        float expected = lamp.health - damage;
        const bool breaks = expected < v->m_lampBreakHealth;
        expected = expected < 0.0f ? 0.0f : expected;
        S_VehicleProjector *projector = nullptr;
        for (S_VehicleProjector &p : v->m_projectors)
            if (p.projector == lamp.projector)
                projector = &p;
        S_Made *made = nullptr;
        for (S_Made &m : g_made)
            if (projector && m.frame == projector->projector)
                made = &m;
        if (made)
            made->lastTexture = nullptr;
        const float health0 = lamp.health;
        v->Deform(Point4(lp), Point4(dir), 0.01f, v->m_lampMinImpulse, 0, nullptr);
        lampsWeak += lamp.health == health0 && !(lamp.flags & VEHICLE_LAMP_BROKEN);
        v->Deform(Point4(lp), Point4(dir), 0.01f, s, 0, nullptr);
        ++lampsHit;
        lampsRight += lamp.health == expected && ((lamp.flags & VEHICLE_LAMP_BROKEN) != 0) == breaks;
        if ((lamp.flags & VEHICLE_LAMP_SHARED) && projector && made) {
            ++sharedLamps;
            const uint16_t keep = lamp.frame->GetPos().x < 0.0f ? VEHICLE_PROJECTOR_RIGHT : VEHICLE_PROJECTOR_LEFT;
            const void *texture = keep == VEHICLE_PROJECTOR_RIGHT ? projector->textures[2] : projector->textures[1];
            sharedSides += projector->state == keep && made->lastTexture == texture;
            // Quirk kept: unbroken (no break level), its side goes all the same.
            fresh();
            const float breakHealth = v->m_lampBreakHealth;
            v->m_lampBreakHealth = -1.0f;
            v->Deform(Point4(lp), Point4(dir), 0.01f, s, 0, nullptr);
            sharedQuirk += !(lamp.flags & VEHICLE_LAMP_BROKEN) && projector->state == keep;
            v->m_lampBreakHealth = breakHealth;
        }
        fresh();
        const float health1 = lamp.health;
        v->Deform(Point4(S_vector(lp.x + 0.4f, lp.y, lp.z)), Point4(dir), 0.3f, s, 0, nullptr);
        lampsOut += lamp.health == health1;
    }
    std::printf("deform lamps: hit %d right %d weak %d out %d shared %d sides %d quirk %d\n", lampsHit, lampsRight,
                lampsWeak, lampsOut, sharedLamps, sharedSides, sharedQuirk);

    // A light part's lamps share its health (no shipped lamp names a light part: the first lamp is
    // given each light part in turn).
    fresh();
    int lightParts = 0, lightRight = 0;
    const uint16_t lampPart = v->m_lamps.empty() ? 0 : v->m_lamps[0].part;
    for (size_t i = 0; i < v->m_parts.size() && !v->m_lamps.empty(); ++i) {
        S_VehiclePart &light = v->m_parts[i];
        if (light.type != VEHICLE_PART_LIGHT || light.frames)
            continue;
        v->m_lamps[0].part = static_cast<uint16_t>(i);
        fresh();
        ++lightParts;
        const S_vector lp = Positions(MeshOf(light))[0][0] * light.frame->GetMatrix();
        const float s = light.minImpulse + 0.3f * light.fullImpulse;
        bool ok = true;
        const float breakHealth = v->m_lampBreakHealth;
        for (const float level : {-1.0f, light.fullHealth * 2.0f}) {
            fresh();
            v->m_lampBreakHealth = level;
            v->Deform(Point4(lp), Point4(dir), 0.01f, s, 0, nullptr);
            ok &= light.health > 0.0f;
            for (const S_VehicleLamp &l : v->m_lamps)
                if (l.part == i)
                    ok &= l.health == light.health && ((l.flags & VEHICLE_LAMP_BROKEN) != 0) == (level > 0.0f);
        }
        v->m_lampBreakHealth = breakHealth;
        lightRight += ok;
    }
    if (!v->m_lamps.empty())
        v->m_lamps[0].part = lampPart;
    std::printf("deform light parts: %d right %d\n", lightParts, lightRight);

    // A synthetic group of three visuals on the part (the door's when one has a door): the visual
    // for its health, the main one hidden; a part on its frame gone.
    fresh();
    {
        std::vector<I3D_frame *> visuals;
        for (int k = 0; k < 3; ++k) {
            I3D_frame *f = NewFrame(FRAME_VISUAL, "group visual", part.frame->m_parent);
            f->m_localMatrix(3) = S_vector(9.0f, 9.0f, 9.0f);
            visuals.push_back(f);
        }
        auto *frames = new std::vector<I3D_frame *>(visuals);
        part.frames = frames;
        S_VehicleDoor *door = nullptr;
        for (S_VehicleSeat &s : v->m_seats)
            if (s.door && !door)
                door = s.door;
        const uint16_t doorPart = door ? door->doorPart : 0;
        I3D_frame *doorFrame = door ? door->frame : nullptr;
        if (door)
            door->doorPart = static_cast<uint16_t>(P);
        const S_vector mainPos = part.frame->GetPos();
        const auto shownIs = [&](int expected, bool on) {
            bool ok = !part.frame->IsOn();
            for (int k = 0; k < 3; ++k) {
                ok &= visuals[k]->IsOn() == (k == expected && on);
                if (k == expected)
                    ok &= visuals[k]->GetPos() == mainPos && (!door || door->frame == visuals[k]);
            }
            return ok;
        };
        // n = 3: whole above 3/4 of the health (nothing), the first visual from 1/2, the last below 1/4.
        part.health = part.fullHealth * 0.8f;
        part.frame->SetOn(true);
        v->ShowPartDamage(static_cast<uint32_t>(P), nullptr, true);
        const bool whole = part.frame->IsOn() && !visuals[0]->IsOn() && !visuals[1]->IsOn() && !visuals[2]->IsOn();
        part.health = part.fullHealth * 0.6f;
        v->ShowPartDamage(static_cast<uint32_t>(P), nullptr, true);
        const bool half = shownIs(0, true);
        part.health = part.fullHealth * 0.3f;
        v->ShowPartDamage(static_cast<uint32_t>(P), nullptr, true);
        const bool third = shownIs(1, true);
        part.health = part.fullHealth * 0.1f;
        v->ShowPartDamage(static_cast<uint32_t>(P), nullptr, true);
        const bool last = shownIs(2, true);
        // Gone: the last one hidden too (a body's shown).
        const uint8_t type = part.type;
        part.type = VEHICLE_PART_ROOF;
        part.health = 0.0f;
        v->ShowPartDamage(static_cast<uint32_t>(P), nullptr, true);
        const bool goneHidden = shownIs(2, false);
        part.type = VEHICLE_PART_BODY;
        v->ShowPartDamage(static_cast<uint32_t>(P), nullptr, true);
        const bool bodyShown = shownIs(2, true);
        part.type = type;
        // A part on its frame: gone (off without `detach`, glass shattered).
        int R = -1;
        for (size_t i = 0; i < v->m_parts.size() && R < 0; ++i)
            if (static_cast<int>(i) != P && !v->m_parts[i].frames && v->m_parts[i].type != VEHICLE_PART_GLASS)
                R = static_cast<int>(i);
        bool attached = true, attachedKept = true;
        if (R >= 0) {
            S_VehiclePart &other = v->m_parts[R];
            I3D_frame *parent = other.frame->m_parent;
            Unlink(other.frame);
            LinkUnder(other.frame, part.frame);
            part.health = part.fullHealth * 0.1f;
            g_damage = S_DamageLog();
            v->ShowPartDamage(static_cast<uint32_t>(P), nullptr, false);
            attachedKept = other.health == 0.0f && (other.state & VEHICLE_PART_STATE_2) && g_damage.partsOff.empty();
            other.health = other.fullHealth;
            g_damage = S_DamageLog();
            v->ShowPartDamage(static_cast<uint32_t>(P), nullptr, true);
            attached = other.health == 0.0f && std::count(g_damage.partsOff.begin(), g_damage.partsOff.end(), R) == 1;
            Unlink(other.frame);
            LinkUnder(other.frame, parent);
        }
        // Deform on a group: no dent, no original.
        clearStep();
        g_damage = S_DamageLog();
        part.health = part.fullHealth;
        const size_t originalsBefore = v->m_meshOriginals.size();
        v->Deform(Point4(point), Point4(dir), radius, strength, DEFORM_NO_WEAR, nullptr);
        // (Other parts in reach are dented and kept.)
        bool groupOriginal = false;
        for (const S_VehicleMeshOriginal &o : v->m_meshOriginals)
            groupOriginal |= o.partName == part.name;
        const bool groupKept = Positions(mesh) == before[P] && !groupOriginal &&
                               v->m_meshOriginals.size() >= originalsBefore && g_damage.dents[part.frame] == 1;
        part.frames = nullptr;
        delete frames;
        if (door) {
            door->doorPart = doorPart;
            door->frame = doorFrame;
        }
        part.frame->SetOn(true);
        for (I3D_frame *f : visuals)
            Unlink(f);
        std::printf("deform group: whole %d half %d third %d last %d gone %d body %d attached %d kept %d dent %d\n",
                    whole, half, third, last, goneHidden, bodyShown, attached, attachedKept, groupKept);
    }

    fresh();
    v->m_unknownF6C = savedF6C;
    v->m_velocity = savedVelocity;
    v->m_unknown210 = savedTurn;
    v->m_unknown2298 = saved2298;
    std::memcpy(v->m_params.sounds1_9E4[8], savedSounds[0], VEHICLE_SOUND_NAME);
    std::memcpy(v->m_params.sounds2_E80[8], savedSounds[1], VEHICLE_SOUND_NAME);
    for (int i : shippedBumpers)
        v->m_parts[i].type = VEHICLE_PART_BUMPER;
}

// --- the saved state (the real StateSize / SaveState / LoadState) -----------------------------------

S_vector4 MatrixRow(const S_matrix &m, int row);
bool SameRow(const S_vector4 &a, const S_vector4 &b);
bool SameXYZ(const S_vector4 &a, const S_vector &b);

// The fixed part's fields at the original's offsets (from SaveState's code, 0x004f7650), each with
// its size and whether it is a bool (0 / 1 only).
struct S_StateField {
    uint32_t offset;
    void *field;
    uint32_t size;
    bool flag;
};
std::vector<S_StateField> StateFields(C_Vehicle *v)
{
    std::vector<S_StateField> f;
    const auto add = [&](uint32_t offset, void *field, uint32_t size, bool flag = false) {
        f.push_back({offset, field, size, flag});
    };
    const auto add3 = [&](uint32_t offset, float *xyz) {
        for (int c = 0; c < 3; ++c)
            add(offset + 4 * c, xyz + c, 4);
    };
    add3(0x24, &v->m_velocity.x);
    add3(0x30, &v->m_unknown210.x);
    add(0x3C, &v->m_unknown554, 4);
    add(0x40, &v->m_unknown58C, 4);
    add(0x44, &v->m_unknown5B0, 4);
    add(0x48, &v->m_unknown5B4, 4);
    add(0x4C, &v->m_unknown5B8, 4);
    add(0x50, &v->m_unknown5BC, 4);
    add(0x54, &v->m_unknown5C0, 4);
    add(0x58, &v->m_unknown5C4, 4);
    add(0x5C, &v->m_unknown5C8, 4);
    add(0x60, &v->m_unknown5CC, 4);
    add(0x64, &v->m_unknown5D0[0], 4);
    add(0x68, &v->m_unknown5D0[1], 4);
    add(0x6C, &v->m_unknown5D0[2], 4);
    add(0x70, &v->m_unknown5E0, 4);
    add(0x74, &v->m_unknown5F4, 4);
    add(0x78, &v->m_unknown5F8, 4);
    add(0x7C, &v->m_unknown5FC, 4);
    add(0x80, &v->m_unknown600, 4);
    add(0x84, &v->m_unknown604, 4);
    add(0x88, &v->m_unknown60C, 4);
    add(0x8C, &v->m_unknown610, 4);
    add(0x90, &v->m_unknown63C, 4);
    add(0x94, &v->m_unknown674, 4);
    add(0x98, &v->m_unknown6E8, 4);
    add(0x9C, &v->m_unknown6EC, 4);
    add(0xA0, &v->m_unknown714, 4);
    add(0xA4, &v->m_unknown718, 4);
    add(0xA8, &v->m_unknown724, 4);
    add3(0xAC, &v->m_unknown1E0.x);
    add(0xB8, &v->m_handbrake, 4);
    add3(0xBC, &v->m_unknown220.x);
    add(0xC8, &v->m_unknown780, 4);
    add(0xCC, &v->m_unknown78C, 4);
    add(0xD0, &v->m_unknown790, 4);
    add(0xD4, &v->m_unknown7C4, 4);
    add(0xD8, &v->m_unknown85C, 4);
    add(0xDC, &v->m_unknown860, 4);
    add(0xE0, &v->m_engineRevs, 4);
    add(0xE4, &v->m_unknown86C, 4);
    add(0xF0, &v->m_unknown8E0, 4);
    add(0xF4, &v->m_brake, 4);
    add(0xF8, &v->m_unknown904, 4);
    add(0xFC, &v->m_unknown90C, 4);
    add(0x100, &v->m_unknown908, 4);
    add(0x104, &v->m_unknown910, 4);
    add(0x108, &v->m_currentMass, 4);
    add(0x10C, &v->m_unknown934, 4);
    add(0x110, &v->m_steering, 4);
    add(0x114, &v->m_unknown94C, 4);
    add(0x118, &v->m_unknown958, 4);
    add(0x11C, &v->m_unknownF54, 4);
    add(0x120, &v->m_fuel, 4);
    add(0x124, &v->m_unknownF68, 4);
    add(0x128, &v->m_unknownF6C, 4);
    add(0x12C, &v->m_unknownF70, 4);
    add3(0x130, &v->m_unknown270.x);
    add3(0x13C, &v->m_unknown280.x);
    add(0x148, &v->m_unknown2224, 4);
    add(0x15C, &v->m_unknown2297, 1);
    add(0x15D, &v->m_unknown2298, 1);
    add(0x15E, &v->m_unknown2299, 1);
    add(0x15F, &v->m_unknown229A, 1);
    add(0x160, &v->m_unknown229B, 1);
    add(0x161, &v->m_unknown229C, 1);
    add(0x162, &v->m_automatic, 1, true);
    add(0x163, &v->m_unknown229E, 1);
    add(0x164, &v->m_unknown22A0, 1);
    add(0x165, &v->m_unknown22A2, 1);
    add(0x166, &v->m_engineOn, 1, true);
    add(0x167, &v->m_unknown22C0, 1);
    add(0x168, &v->m_unknown22C1, 1);
    add(0x169, &v->m_unknown22C2, 1);
    add(0x16A, &v->m_specialVolumesOn, 1, true);
    add(0x16C, &v->m_soundsOn, 1, true);
    add(0x16D, &v->m_unknown228B, 1);
    add(0x16E, &v->m_unknown228F, 1);
    add(0x16F, &v->m_driven, 1);
    add(0x170, &v->m_unknown2292, 1);
    return f;
}

// Fills the fields with values made from `seed` (bools alternate; a byte its low bits).
void FillState(const std::vector<S_StateField> &fields, uint32_t seed)
{
    for (size_t i = 0; i < fields.size(); ++i) {
        const S_StateField &f = fields[i];
        if (f.flag) {
            *static_cast<bool *>(f.field) = ((i + seed) & 1) != 0;
        } else if (f.size == 1) {
            *static_cast<uint8_t *>(f.field) = static_cast<uint8_t>(seed * 7 + i);
        } else {
            const float value = static_cast<float>(seed) * 100.0f + static_cast<float>(i) * 0.25f + 0.5f;
            std::memcpy(f.field, &value, 4);
        }
    }
}

// SaveState / LoadState, on the car at rest after the other tests:
// - the size: the block is StateSize() bytes, the header says so, nothing written past it, the
//   records add up to it;
// - the fixed part: each field at the original's offset (values made distinct), the model's place
//   and turn (s first), the counts, the main volumes' flag; loaded into the car with other values:
//   every field back, the speed and direction from the velocity, Replace's fields; a block of
//   another version refused, the car unchanged;
// - each lamp's place packed, flags and health; back on load;
// - the dented parts (two dented, one a glass part when there is one): their records (key, health,
//   state, the moved vertices per level); repaired and loaded: the vertices at the original plus the
//   offsets in 2 cm steps, the health and state (glass: cracked by its health), one original each;
// - the seats and doors, the wheels' records and their places on load (the holder by the
//   extension, the contact under the wheel);
// - a car saved lying on its roof comes back on its wheels;
// - PartKey (short names, the dot, an empty name) and FindPartByKey (the last part of the key; none
//   for the empty key, not even a part whose name ends with the dot);
// - PackPosition / UnpackPosition within a step of the box over 1023 (1/4095 for z).
void StateTest(C_Vehicle *v)
{
    // A known state: dents on two parts (one glass when there is one), lamps, seats, doors, wheels.
    const uint32_t savedF6C = v->m_unknownF6C;
    v->m_unknownF6C &= ~VEHICLE_SIM_IMPACT_DAMAGE;
    std::srand(11);
    // The first part and a glass part (else the last), dented one at a time (the others out of it).
    std::vector<int> dented;
    for (size_t i = 0; i < v->m_parts.size() && dented.empty(); ++i)
        if (!v->m_parts[i].frames)
            dented.push_back(static_cast<int>(i));
    int second = -1;
    for (size_t i = 0; i < v->m_parts.size() && !dented.empty(); ++i)
        if (!v->m_parts[i].frames && static_cast<int>(i) != dented[0] &&
            (second < 0 || v->m_parts[i].type == VEHICLE_PART_GLASS) &&
            (second < 0 || v->m_parts[second].type != VEHICLE_PART_GLASS))
            second = static_cast<int>(i);
    if (second >= 0)
        dented.push_back(second);
    std::vector<float> leastImpulses;
    for (const S_VehiclePart &p : v->m_parts)
        leastImpulses.push_back(p.minImpulse);
    for (int index : dented) {
        for (size_t j = 0; j < v->m_parts.size(); ++j)
            v->m_parts[j].minImpulse = static_cast<int>(j) == index ? leastImpulses[j] : 1e30f;
        S_VehiclePart &p = v->m_parts[index];
        const S_vector at = Positions(MeshOf(p))[0][0] * p.frame->GetMatrix();
        v->Deform(Point4(at), Point4(S_vector(0.2f, -0.9f, 0.4f)), 0.4f, p.minImpulse + 2.0f * p.fullImpulse, 0,
                  nullptr);
        p.health = p.fullHealth * 0.4f;
        p.state |= VEHICLE_PART_STATE_2;
        p.unknown39 = 0x5A;
    }
    for (size_t j = 0; j < v->m_parts.size(); ++j)
        v->m_parts[j].minImpulse = leastImpulses[j];
    std::vector<std::vector<std::vector<S_vector>>> dents;
    for (int index : dented)
        dents.push_back(Positions(MeshOf(v->m_parts[index])));
    for (size_t i = 0; i < v->m_lamps.size(); ++i) {
        v->m_lamps[i].flags = (v->m_lamps[i].flags & ~VEHICLE_LAMP_BROKEN) | ((i & 1) ? VEHICLE_LAMP_BROKEN : 0u);
        v->m_lamps[i].health = 10.0f + static_cast<float>(i);
    }
    for (size_t i = 0; i < v->m_seats.size(); ++i) {
        v->m_seats[i].load = 50.0f + static_cast<float>(i);
        v->m_seats[i].flags = 0x100u + static_cast<uint32_t>(i);
        if (S_VehicleDoor *d = v->m_seats[i].door) {
            d->angle = 0.1f * static_cast<float>(i + 1);
            d->targetAngle = 0.2f * static_cast<float>(i + 1);
            d->flags = static_cast<uint8_t>(i + 1);
        }
    }
    for (int i = 0; i < v->m_wheelCount; ++i) {
        S_VehicleWheel *w = v->m_wheels[i];
        if (!w)
            continue;
        const float k = static_cast<float>(i);
        w->unknown070 = S_vector(k, k + 0.1f, k + 0.2f);
        w->unknown080 = S_vector(0.0f, 1.0f, k * 0.01f);
        w->unknown0A0 = S_vector(k * 2.0f, 0.5f, 0.25f);
        w->unknown0B0 = S_vector(0.0f, 0.0f, 1.0f);
        w->unknown0E0 = S_vector(k * 3.0f, 4.0f, 5.0f);
        w->extension = 0.01f * k;
        w->unknown1B0 = 100.0f + k;
        w->unknown1B4 = 200.0f + k;
        w->surfaceEntry = i % 3 - 1;
        w->surface = i;
        w->unknown1D4 = 0.3f;
        w->unknown1F0 = 1.5f + k;
        w->bend = 0.02f * k;
        w->health = 70.0f - k;
        w->flags |= WHEEL_SLIDING | WHEEL_HIT;
    }
    // The first wheel came off (hidden on load).
    S_VehicleWheel *detached = nullptr;
    for (int i = 0; i < v->m_wheelCount && !detached; ++i)
        detached = v->m_wheels[i];
    if (detached)
        detached->flags |= WHEEL_DETACHED;
    const std::vector<S_StateField> fields = StateFields(v);
    FillState(fields, 3);
    v->m_gear = 1;
    v->m_requestedGear = v->m_gearCount > 1 ? 2 : 1;
    v->m_unknownF6C &= ~VEHICLE_SIM_IMPACT_DAMAGE;
    v->SetVolumesInWorld(false, true);
    std::vector<std::vector<uint8_t>> expected;
    for (const S_StateField &f : fields)
        expected.emplace_back(static_cast<uint8_t *>(f.field), static_cast<uint8_t *>(f.field) + f.size);
    const int gear = v->m_gear, requested = v->m_requestedGear;
    const S_vector4 modelPos = v->m_modelPos;

    // Saved.
    const uint32_t size = v->StateSize();
    std::vector<uint8_t> block(size + 64, 0xCD);
    const bool saved = v->SaveState(block.data());
    uint32_t word[4];
    std::memcpy(word, block.data(), 8);
    int layout = 0, headerOff = 0;
    for (size_t i = 0; i < fields.size(); ++i)
        headerOff += std::memcmp(block.data() + fields[i].offset, expected[i].data(), fields[i].size) != 0;
    float stored[11];
    std::memcpy(stored, block.data() + 0x08, sizeof stored);
    const S_quat &rot = v->m_model->m_rot;
    headerOff += stored[0] != modelPos.x || stored[1] != modelPos.y || stored[2] != modelPos.z || stored[3] != rot.s ||
                 stored[4] != rot.v.x || stored[5] != rot.v.y || stored[6] != rot.v.z;
    int32_t gears[2];
    std::memcpy(gears, block.data() + 0xE8, 8);
    headerOff += gears[0] != gear || gears[1] != requested || block[0x16B] != 1;
    uint32_t counts[4];
    std::memcpy(counts, block.data() + 0x14C, sizeof counts);
    layout += word[0] != 10 || word[1] != size || counts[0] != v->m_lamps.size() || counts[2] != v->m_seats.size() ||
              counts[3] != static_cast<uint32_t>(v->m_wheelCount);
    // The records, walked.
    uint32_t at = 0x174;
    int lampsOff = 0;
    for (size_t i = 0; i < counts[0]; ++i, at += 12) {
        uint32_t rec[3];
        std::memcpy(rec, block.data() + at, 12);
        const S_VehicleLamp &l = v->m_lamps[i];
        float health;
        std::memcpy(&health, &rec[2], 4);
        lampsOff += rec[0] != v->PackPosition(0x20, l.frame->GetWorldPos() * v->m_invModelMatrix) ||
                    rec[1] != l.flags || health != l.health;
    }
    int partRecords = 0, partsOff = 0;
    std::vector<uint32_t> movedCounts;
    for (size_t r = 0; r < counts[1]; ++r) {
        uint32_t rec[5];
        std::memcpy(rec, block.data() + at, 20);
        const int index = v->FindPartByKey(rec[0]);
        float health;
        std::memcpy(&health, &rec[1], 4);
        ++partRecords;
        if (index < 0) {
            ++partsOff;
        } else {
            const S_VehiclePart &p = v->m_parts[index];
            partsOff += health != p.health || (rec[4] & 0xFFFFu) != static_cast<uint32_t>(p.state | p.unknown39 << 8);
        }
        movedCounts.push_back(rec[2] + rec[3]);
        at += 20 + 4 * (rec[2] + rec[3]);
    }
    int seatsOff = 0;
    for (size_t i = 0; i < counts[2]; ++i, at += 20) {
        float rec[4];
        std::memcpy(rec, block.data() + at, 16);
        const S_VehicleSeat &s = v->m_seats[i];
        uint32_t u24;
        std::memcpy(&u24, block.data() + at + 4, 4);
        seatsOff += rec[0] != s.load || u24 != s.flags;
        if (s.door)
            seatsOff += rec[2] != s.door->angle || rec[3] != s.door->targetAngle || block[at + 16] != s.door->flags;
    }
    int wheelsOff = 0;
    for (size_t i = 0; i < counts[3]; ++i, at += 100) {
        const S_VehicleWheel *w = v->m_wheels[i];
        float rec[25];
        std::memcpy(rec, block.data() + at, 100);
        uint32_t flags;
        int entry, surface;
        std::memcpy(&flags, &rec[22], 4);
        std::memcpy(&entry, &rec[18], 4);
        std::memcpy(&surface, &rec[19], 4);
        wheelsOff += !w || rec[0] != w->unknown070.x || rec[5] != w->unknown080.z || rec[6] != w->unknown0A0.x ||
                     rec[11] != w->unknown0B0.z || rec[12] != w->unknown0E0.x || rec[15] != w->extension ||
                     rec[16] != w->unknown1B0 || rec[17] != w->unknown1B4 || entry != w->surfaceEntry ||
                     surface != w->surface || rec[20] != w->unknown1D4 || rec[21] != w->unknown1F0 ||
                     flags != (w->flags & ~WHEEL_SLIDING) || rec[23] != w->bend || rec[24] != w->health;
    }
    layout += at != size || block[size] != 0xCD || block[size + 63] != 0xCD;
    std::printf("state save: saved %d size %u layout %d header %d lamps %zu off %d parts %d (dented %zu) off %d "
                "seats %u off %d wheels %u off %d\n",
                saved, size, layout, headerOff, v->m_lamps.size(), lampsOff, partRecords, dented.size(), partsOff,
                counts[2], seatsOff, counts[3], wheelsOff);

    // Another version: refused, the car unchanged.
    std::vector<uint8_t> other(block);
    other[0] = 9;
    FillState(fields, 5);
    v->m_gear = 1;
    v->m_requestedGear = 1;
    const float before = v->m_unknown5B8;
    const bool refused = !v->LoadState(other.data()) && v->m_unknown5B8 == before;

    // Loaded into the car repaired and changed: everything back.
    v->RepairParts(false);
    v->ResetCondition();
    for (S_VehicleLamp &l : v->m_lamps)
        l.flags &= ~VEHICLE_LAMP_BROKEN;
    for (S_VehicleSeat &s : v->m_seats) {
        s.load = 0.0f;
        if (s.door)
            s.door->angle = s.door->targetAngle = 0.0f;
    }
    // (The refreshes: per record and level the visual, the level and the mesh.)
    int expectedRefreshes = 0;
    for (int index : dented)
        expectedRefreshes += 3 * static_cast<int>(std::min<uint32_t>(MeshOf(v->m_parts[index])->levelCount, 2));
    // (The dented glass part's material given a texture, so that its crack shows.)
    static S_Image intactGlass;
    S_Material *glassMaterial = nullptr;
    S_Image *glassMaterialTexture = nullptr;
    for (int index : dented)
        if (v->m_parts[index].type == VEHICLE_PART_GLASS && v->m_parts[index].glassTexture) {
            glassMaterial = MeshOf(v->m_parts[index])->levels[0]->faces.material;
            glassMaterialTexture = glassMaterial->texture;
            intactGlass.vtable = g_imageVtable;
            intactGlass.name = "intact glass";
            intactGlass.refs = 1;
            glassMaterial->texture = &intactGlass;
        }
    const int refreshes0 = g_meshRefreshes;
    const bool loaded = v->LoadState(block.data());
    const bool refreshed = g_meshRefreshes - refreshes0 == expectedRefreshes;
    int fieldsBack = 0;
    for (size_t i = 0; i < fields.size(); ++i)
        fieldsBack += std::memcmp(fields[i].field, expected[i].data(), fields[i].size) == 0;
    const S_vector velocity(v->m_velocity.x, v->m_velocity.y, v->m_velocity.z);
    const bool derived = v->m_gear == gear && v->m_requestedGear == requested && !v->m_mainVolumesInWorld &&
                         v->m_speed == velocity.Magnitude() &&
                         S_vector(v->m_velocityDir.x, v->m_velocityDir.y, v->m_velocityDir.z) == velocity.Normalized() &&
                         v->m_throttle == 0.0f && v->m_unknown700 == 0.0f && v->m_unknown228E == 0 &&
                         v->m_unknown2293 == 0 && !v->m_frontGroundVolume && !v->m_rearGroundVolume;
    int lampsBack = 0;
    for (size_t i = 0; i < v->m_lamps.size(); ++i)
        lampsBack += v->m_lamps[i].health == 10.0f + static_cast<float>(i) &&
                     ((v->m_lamps[i].flags & VEHICLE_LAMP_BROKEN) != 0) == ((i & 1) != 0);
    // The parts: the vertices at the original plus the offsets in 2 cm steps.
    int partsBack = 0, originalsRight = 0;
    for (size_t d = 0; d < dented.size(); ++d) {
        const S_VehiclePart &p = v->m_parts[dented[d]];
        const int o = v->FindMeshOriginal(p);
        if (o < 0)
            continue;
        // (Kept anew from the part's mesh, its levels past the second dropped.)
        const auto *om = reinterpret_cast<const S_Mesh *>(v->m_meshOriginals[o].mesh);
        const uint32_t levels = MeshOf(p)->levelCount;
        originalsRight += v->m_meshOriginals[o].refs == 1 && om->levelCount == std::min<uint32_t>(levels, 2) &&
                          om->levelsDropped == static_cast<int>(levels > 2 ? levels - 2 : 0);
        const auto now = Positions(MeshOf(p));
        const auto orig = Positions(reinterpret_cast<const S_Mesh *>(v->m_meshOriginals[o].mesh));
        bool ok = now.size() == dents[d].size();
        for (size_t l = 0; ok && l < now.size(); ++l)
            for (size_t i = 0; i < now[l].size(); ++i) {
                const S_vector &a = orig[l][i], &b = dents[d][l][i];
                const float gx = b.x - a.x, gy = b.y - a.y, gz = b.z - a.z;
                S_vector want = a;
                if ((gz * gz + gy * gy) + gx * gx > 0.0001f) {
                    const auto step = [](float from, float to) {
                        const int s = static_cast<int>((to - from) * 50.0f) & 0x3F;
                        return static_cast<float>(s >= 32 ? s - 64 : s) * 0.02f;
                    };
                    want = S_vector(step(a.x, b.x) + a.x, step(a.y, b.y) + a.y, step(a.z, b.z) + a.z);
                }
                ok &= now[l][i] == want;
            }
        // (Glass: its state not loaded, LoadState's quirk: not marked deformed, cracked by its health.)
        const bool glass = p.type == VEHICLE_PART_GLASS;
        const bool crackable = p.health < p.crackHealth && glassMaterial;
        ok &= p.health == p.fullHealth * 0.4f &&
              (glass ? !(p.state & VEHICLE_PART_DEFORMED) && ((p.state & VEHICLE_PART_GLASS_BROKEN) != 0) == crackable &&
                           (!crackable || (p.glassTexture == reinterpret_cast<I3D_texture *>(&intactGlass) &&
                                           glassMaterial->texture != &intactGlass))
                     : (p.state == (VEHICLE_PART_STATE_2 | VEHICLE_PART_DEFORMED) && p.unknown39 == 0x5A));
        partsBack += ok;
    }
    int seatsBack = 0;
    for (size_t i = 0; i < v->m_seats.size(); ++i) {
        const S_VehicleSeat &s = v->m_seats[i];
        bool ok = s.load == 50.0f + static_cast<float>(i) && s.flags == 0x100u + i;
        if (const S_VehicleDoor *d = s.door)
            ok &= d->angle == 0.1f * static_cast<float>(i + 1) && d->targetAngle == 0.2f * static_cast<float>(i + 1) &&
                  d->flags == static_cast<uint8_t>(i + 1);
        seatsBack += ok;
    }
    int wheelsBack = 0;
    for (int i = 0; i < v->m_wheelCount; ++i) {
        const S_VehicleWheel *w = v->m_wheels[i];
        if (!w)
            continue;
        const float k = static_cast<float>(i);
        const S_vector4 &up = v->m_modelAxisY;
        const S_vector contact(w->worldPos.x - w->radius * up.x, w->worldPos.y - up.y * w->radius,
                               w->worldPos.z - up.z * w->radius);
        wheelsBack += w->unknown070 == S_vector(k, k + 0.1f, k + 0.2f) && w->unknown080 == w->unknown090 &&
                      w->unknown0E0 == S_vector(k * 3.0f, 4.0f, 5.0f) && w->extension == 0.01f * k &&
                      w->surfaceEntry == i % 3 - 1 && w->surface == i && w->unknown1F0 == 1.5f + k &&
                      w->bend == 0.02f * k && w->health == 70.0f - k && (w->flags & WHEEL_HIT) &&
                      !(w->flags & WHEEL_SLIDING) && w->unknown168 == w->extension * w->unknown160 &&
                      w->holder->m_localMatrix(3) ==
                          S_vector(w->holderPos.x, w->holderPos.y - w->extension, w->holderPos.z) &&
                      SameRow(w->worldPos, MatrixRow(w->frame->GetMatrix(), 3)) && SameXYZ(w->unknown050, contact) &&
                      SameRow(w->unknown060, w->unknown050) && (w != detached || !w->frame->IsOn());
    }
    // Loaded again over the dents: the originals found, a second user each.
    v->LoadState(block.data());
    int reused = 0;
    for (int index : dented) {
        const int o = v->FindMeshOriginal(v->m_parts[index]);
        reused += o >= 0 && v->m_meshOriginals[o].refs == 2;
        if (o >= 0)
            v->m_meshOriginals[o].refs = 1;             // (one user again: RepairParts frees it)
    }
    if (detached) {
        detached->flags &= ~WHEEL_DETACHED;
        detached->frame->SetOn(true);
    }
    std::printf("state load: refused %d loaded %d fields %d of %zu derived %d lamps %d parts %d originals %d seats %d "
                "wheels %d refreshed %d reused %d\n",
                refused, loaded, fieldsBack, fields.size(), derived, lampsBack, partsBack, originalsRight, seatsBack,
                wheelsBack, refreshed, reused);

    // On its roof: back on its wheels (its y axis up, its z axis kept).
    {
        const S_quat rot0 = v->m_model->m_rot;
        S_quat roll;
        roll.Make(S_vector(0.0f, 0.0f, 1.0f), 3.1415927f);
        S_quat flipped;
        flipped.v = S_vector(rot0.s * roll.v.x + roll.s * rot0.v.x, rot0.s * roll.v.y + roll.s * rot0.v.y,
                             rot0.s * roll.v.z + roll.s * rot0.v.z) + rot0.v.Cross(roll.v);
        flipped.s = rot0.s * roll.s - rot0.v.Dot(roll.v);
        v->m_model->SetRot(flipped);
        v->m_model->GetLocalMatrix();
        const S_vector z0 = v->m_model->GetMatrix()(2);
        const bool down = v->m_model->GetMatrix()(1).y < -0.9f;
        std::vector<uint8_t> roof(v->StateSize());
        v->SaveState(roof.data());
        v->LoadState(roof.data());
        const S_matrix &m = v->m_model->GetMatrix();
        const bool rolled = down && m(1).y > 0.99f && (m(2) - z0).Magnitude() < 1e-4f &&
                            SameXYZ(v->m_modelAxisY, m(1)) && SameXYZ(v->m_modelAxisZ, m(2));
        v->m_model->SetRot(rot0);
        v->m_model->GetLocalMatrix();
        v->ReadModelPlace();                            // (the car's matrices as the model's again)
        std::printf("state roof: rolled %d\n", rolled);
    }

    // PartKey and FindPartByKey (with parts); PackPosition and UnpackPosition.
    bool keys = true, found = true;
    if (!v->m_parts.empty()) {
        I3D_frame *f = v->m_parts[0].frame;
        const char *name0 = f->m_name;
        const auto keyOf = [&](const char *name) {
            f->m_name = name;
            return v->PartKey(0);
        };
        const uint32_t abc = keyOf("car.abc");
        const uint32_t long9 = keyOf("x.y.abcdefghi");
        const uint32_t empty = keyOf("car.");
        const uint32_t word4 = keyOf("wxyz");
        // (No part answers the empty key, not even one whose name ends with the dot.)
        keyOf("car.");
        found &= v->FindPartByKey(0xFFFFFFFF) == -1;
        uint32_t a, b, c;
        std::memcpy(&a, "abcd", 4);
        std::memcpy(&b, "efgh", 4);
        c = 'i';
        keys = abc == ('a' | 'b' << 8 | 'c' << 16) && long9 == (a ^ b ^ c) && empty == 0xFFFFFFFF &&
               word4 == ('w' | 'x' << 8 | 'y' << 16 | static_cast<uint32_t>('z') << 24);
        f->m_name = name0;
        // (The last part of a key: the first part's name given to the last.)
        if (v->m_parts.size() > 1) {
            I3D_frame *last = v->m_parts.back().frame;
            const char *nameLast = last->m_name;
            last->m_name = name0;
            found &= v->FindPartByKey(v->PartKey(0)) == static_cast<int>(v->m_parts.size()) - 1;
            last->m_name = nameLast;
        }
    }
    found &= v->FindPartByKey(0xFFFFFFFF) == -1;
    int packed = 0;
    const S_vector min(-v->m_boxOffset.x, -v->m_boxOffset.y, -v->m_boxOffset.z);
    for (int i = 0; i < 20; ++i) {
        const float t[3] = {(i * 37 % 100) * 0.0099f, (i * 53 % 100) * 0.0099f, (i * 71 % 100) * 0.0099f};
        const S_vector p(min.x + t[0] * v->m_size.x, min.y + t[1] * v->m_size.y, min.z + t[2] * v->m_size.z);
        const S_vector u = v->UnpackPosition(0x20, v->PackPosition(0x20, p));
        packed += std::fabs(u.x - p.x) <= v->m_size.x / 1023.0f * 1.01f &&
                  std::fabs(u.y - p.y) <= v->m_size.y / 1023.0f * 1.01f &&
                  std::fabs(u.z - p.z) <= v->m_size.z / 4095.0f * 1.01f && u.x <= p.x + 1e-4f;
    }
    std::printf("state keys: %d found %d packed %d of 20\n", keys, found, packed);
    v->RepairParts(false);
    v->ResetCondition();
    v->m_unknownF6C = savedF6C;
    if (glassMaterial)
        glassMaterial->texture = glassMaterialTexture;
}

// Replace (LoadState's step), called after Reset as the load does: the movement and turn back from
// the body's space into the world's; each wheel at its frame, its contact under it at its radius
// and the ray end one below, off the ground (ON_GROUND, SLIDING, SPINNING, 0x1000000 and HIT
// cleared, LOCKED and BUMPED kept), no ground, its skid marks back in the scene; the lamps' own and
// the projectors' objects in the model's parent; the controls released, the volumes in.
void ReplaceTest(C_Vehicle *v)
{
    v->m_velocity.x = 3.0f, v->m_velocity.y = -1.0f, v->m_velocity.z = 2.0f;
    v->m_unknown210 = S_vector(0.2f, -0.1f, 0.3f);
    const S_vector velocity0(v->m_velocity.x, v->m_velocity.y, v->m_velocity.z), turn0 = v->m_unknown210;
    const uint32_t kept = WHEEL_LOCKED | WHEEL_BUMPED;
    const uint32_t cleared = WHEEL_ON_GROUND | WHEEL_SLIDING | WHEEL_SPINNING | WHEEL_FLAG_1000000 | WHEEL_HIT;
    for (int i = 0; i < v->m_wheelCount; ++i)
        if (S_VehicleWheel *w = v->m_wheels[i]) {
            w->flags |= kept | cleared;
            w->ground = &g_wallVolume;
            w->unknown050.x = w->unknown060.x = 99.0f;
        }
    v->m_unknown228E = v->m_unknown2293 = v->m_unknown2294 = v->m_unknown2295 = 1;
    v->m_unknown908 = v->m_unknown90C = v->m_throttle = v->m_unknown700 = v->m_brake = 0.5f;
    v->m_frontGroundVolume = v->m_rearGroundVolume = &g_wallVolume;
    v->m_unknown4F8[0] = v->m_unknown4F8[1] = 1;
    v->m_unknown22C0 = 0;
    v->Reset(VEHICLE_RESET_UNLINK_SKID_MARKS);
    v->Replace();
    const S_vector velocity(v->m_velocity.x, v->m_velocity.y, v->m_velocity.z);
    const bool motion = (velocity - velocity0).Magnitude() < 1e-4f && (v->m_unknown210 - turn0).Magnitude() < 1e-4f;
    int wheels = 0, wheelsRight = 0, skids = 0, skidsIn = 0;
    for (int i = 0; i < v->m_wheelCount; ++i) {
        S_VehicleWheel *w = v->m_wheels[i];
        if (!w)
            continue;
        ++wheels;
        const S_vector4 &up = v->m_modelAxisY;
        const float down = -w->radius;
        const S_vector contact(down * up.x + w->worldPos.x, down * up.y + w->worldPos.y, down * up.z + w->worldPos.z);
        wheelsRight += SameRow(w->worldPos, MatrixRow(w->frame->GetMatrix(), 3)) && SameXYZ(w->unknown050, contact) &&
                       SameXYZ(w->unknown060, contact - S_vector(up.x, up.y, up.z)) && !w->ground &&
                       (w->flags & kept) == kept && !(w->flags & cleared);
        for (const S_VehicleSkidMark &m : w->skidMarks) {
            skids += 2;
            skidsIn += (m.frame0->m_parent == g_sector) + (m.frame1->m_parent == g_sector);
        }
        w->flags &= ~kept;
    }
    int lampObjects = 0, lampsIn = 0;
    for (const S_VehicleLamp &l : v->m_lamps)
        if (l.projector && !(l.flags & VEHICLE_LAMP_SHARED))
            for (const I3D_frame *f : {static_cast<const I3D_frame *>(l.projector), static_cast<const I3D_frame *>(l.light)})
                if (f) {
                    ++lampObjects;
                    lampsIn += f->m_parent == v->m_unknown544;
                }
    for (const S_VehicleProjector &p : v->m_projectors)
        for (const I3D_frame *f : {static_cast<const I3D_frame *>(p.projector), static_cast<const I3D_frame *>(p.light)})
            if (f) {
                ++lampObjects;
                lampsIn += f->m_parent == v->m_unknown544;
            }
    const bool controls = v->m_unknown228E == 0 && v->m_unknown2293 == 0 && v->m_unknown2294 == 0 &&
                          v->m_unknown2295 == 0 && v->m_unknown908 == 1.0f && v->m_unknown90C == 1.0f &&
                          v->m_throttle == 0.0f && v->m_unknown700 == 0.0f && v->m_brake == 0.0f &&
                          !v->m_frontGroundVolume && !v->m_rearGroundVolume && v->m_unknown4F8[0] == 0 &&
                          v->m_unknown4F8[1] == 0 && v->m_unknown22C0 == 1 && v->m_mainVolumesInWorld &&
                          v->m_volume490InWorld;
    std::printf("state replace: motion %d wheels %d of %d skid frames %d of %d lamps %d of %d controls %d\n", motion,
                wheelsRight, wheels, skidsIn, skids, lampsIn, lampObjects, controls);
}

// --- the hits --------------------------------------------------------------------------------------

// The impact and impulse callbacks' arguments, and what the car held when they came.
struct S_HitLog {
    int impacts = 0, impulses = 0;
    void *impactContext = nullptr, *impulseContext = nullptr;
    const S_vector *impulse = nullptr, *largest = nullptr, *velocity = nullptr;
    S_vector impulseValue, velocityThen;
    const S_vector4 *point = nullptr;
    float radius = 0.0f;
    C_Actor *actor = nullptr;
    const S_CollisionVolume *volume = nullptr;
    int unknown = -1;
    S_vector4 dirAtImpact{}, dirAtImpulse{};
    uint32_t wornAtImpact = 0;
    uint8_t flagAtImpulse = 0;
} g_hits;
C_Vehicle *g_hitVehicle;

void __fastcall F_Impact(void *context, const S_vector *impulse, const S_vector *largest, const S_vector4 *point,
                         float radius, const S_vector *velocity, C_Actor *actor, const S_CollisionVolume *volume,
                         int unknown)
{
    ++g_hits.impacts;
    g_hits.impactContext = context;
    g_hits.impulse = impulse;
    g_hits.largest = largest;
    g_hits.impulseValue = *impulse;
    g_hits.point = point;
    g_hits.radius = radius;
    g_hits.velocity = velocity;
    g_hits.velocityThen = *velocity;
    g_hits.actor = actor;
    g_hits.volume = volume;
    g_hits.unknown = unknown;
    g_hits.dirAtImpact = g_hitVehicle->m_velocityDir;
    g_hits.wornAtImpact = g_hitVehicle->m_unknown58C;
}

void __fastcall F_Impulse(void *context)
{
    ++g_hits.impulses;
    g_hits.impulseContext = context;
    g_hits.dirAtImpulse = g_hitVehicle->m_velocityDir;
    g_hits.flagAtImpulse = g_hitVehicle->m_unknown22C0;
}

// v x m (a row vector, LS3D's order; m's last row added).
S_vector RowTimes(const S_vector &v, const S_matrix &m)
{
    return S_vector(v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0],
                    v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1],
                    v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2]);
}

bool NearVec(const S_vector &a, const S_vector &b, float tolerance)
{
    return (a - b).Magnitude() <= tolerance * std::max(1.0f, b.Magnitude());
}

// The push's size as the SDK inline gives it (the bits below 1e-8: none).
float PushSize(const S_vector &p)
{
    const float square = (p.z * p.z + p.y * p.y) + p.x * p.x;
    int32_t bits;
    std::memcpy(&bits, &square, sizeof bits);
    if (bits < 0x322bcc77)
        return 0.0f;
    return std::fabs(square - 1.0f) < 1e-8f ? 1.0f : std::sqrt(square);
}

// AddForce and ApplyCarHit, the car's collisions with a human and with another car (the motion, the
// condition and the meshes restored after), with the body turned (+0x310 a rotation, +0x350 its
// inverse) so that the body's space matters:
// - AddForce: the force added to +0x270 (not with flags bit 0), its torque (the force about the lever
//   arm into the body, times the factors, back) to +0x280 (not with bit 1), +0x22C0 set before the
//   impulse callback (each of the four flag values);
// - ApplyCarHit without dent or wear: the motion as ApplyImpulse's with the push (impulse x scale)
//   and no factors (bit for bit) and as the arithmetic gives it, the direction normalized (all four
//   floats from the velocity) before the impulse callback, no impact callback, no dent;
// - with wear: ImpactDamage with the push's size (the condition bit for bit as the reference call's),
//   before the impact callback: the push twice, the point, 0.45, the velocity after the push, the
//   other car, no volume, 1, the direction not yet normalized; a push under 1e-4 is none;
// - with dents (parts): Deform at the point against the normal, radius 0.45, the push's size (the
//   meshes and the part hits bit for bit as the reference call's, with the same rand()).
void HitTest(C_Vehicle *v)
{
    g_hitVehicle = v;
    const uint32_t savedF6C = v->m_unknownF6C, saved58C = v->m_unknown58C;
    void *const savedImpact = v->m_callbacks[VEHICLE_CALLBACK_IMPACT];
    void *const savedImpulse = v->m_callbacks[VEHICLE_CALLBACK_IMPULSE];
    const S_matrix saved310 = v->m_unknown310, saved350 = v->m_unknown350;
    const S_vector4 savedVelocity = v->m_velocity, savedDir = v->m_velocityDir;
    const S_vector savedTurn = v->m_unknown210, saved270 = v->m_unknown270, saved280 = v->m_unknown280;
    float *const wear[] = {&v->m_unknown608, &v->m_unknown60C, &v->m_unknown610, &v->m_unknown614, &v->m_unknown618,
                           &v->m_unknown61C, &v->m_unknown620, &v->m_unknown63C, &v->m_unknown640, &v->m_unknown644,
                           &v->m_unknown648, &v->m_unknown64C};
    float savedWear[12];
    for (int i = 0; i < 12; ++i)
        savedWear[i] = *wear[i];
    const uint8_t saved22A0 = v->m_unknown22A0;
    v->m_callbacks[VEHICLE_CALLBACK_IMPACT] = reinterpret_cast<void *>(&F_Impact);
    v->m_callbacks[VEHICLE_CALLBACK_IMPULSE] = reinterpret_cast<void *>(&F_Impulse);
    v->m_unknownF6C &= ~(VEHICLE_SIM_DEFORM | VEHICLE_SIM_IMPACT_DAMAGE);

    // The body turned 0.7 about y and 0.3 about x.
    const float cy = std::cos(0.7f), sy = std::sin(0.7f), cx = std::cos(0.3f), sx = std::sin(0.3f);
    S_matrix r;
    std::memset(&r, 0, sizeof r);
    r.m[0][0] = cy, r.m[0][1] = sy * sx, r.m[0][2] = -sy * cx;
    r.m[1][1] = cx, r.m[1][2] = sx;
    r.m[2][0] = sy, r.m[2][1] = -cy * sx, r.m[2][2] = cy * cx;
    r.m[3][3] = 1.0f;
    S_matrix rt = r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            rt.m[i][j] = r.m[j][i];
    v->m_unknown310 = r;
    v->m_unknown350 = rt;

    // AddForce.
    const S_vector center = XYZ4(v->m_unknown1B0);
    const S_vector point = center + S_vector(1.2f, 0.4f, -2.1f), arm = point - center;
    const S_vector force(40.0f, -25.0f, 90.0f), factors(0.5f, 2.0f, 1.5f);
    const S_vector force0(1.0f, 2.0f, 3.0f), torque0(0.5f, -0.5f, 0.25f);
    S_vector torque = RowTimes(force.Cross(arm), rt);
    torque = RowTimes(S_vector(torque.x * factors.x, torque.y * factors.y, torque.z * factors.z), r);
    int added = 0;
    for (uint8_t flags = 0; flags < 4; ++flags) {
        v->m_unknown270 = force0;
        v->m_unknown280 = torque0;
        v->m_unknown22C0 = 0;
        g_hits = S_HitLog();
        v->AddForce(force, point, flags, factors);
        const bool forceRight = v->m_unknown270 == (flags & 1 ? force0 : force0 + force);
        const bool torqueRight = flags & 2 ? v->m_unknown280 == torque0 : NearVec(v->m_unknown280, torque0 + torque, 1e-5f);
        added += forceRight && torqueRight && v->m_unknown22C0 == 1 && g_hits.impulses == 1 &&
                 g_hits.impulseContext == v->m_callbackContext && g_hits.flagAtImpulse == 1 && g_hits.impacts == 0;
    }
    std::printf("hit force: %d of 4\n", added);

    // ApplyCarHit: the motion.
    alignas(8) static char otherCar[64];
    C_Actor *const other = reinterpret_cast<C_Actor *>(otherCar);
    const S_vector4 point4 = Point4(point);
    const S_vector impulse(300.0f, -50.0f, 1200.0f), normal(0.6f, 0.0f, 0.8f);
    const float scale = 1.2f;
    const S_vector push = impulse * scale;
    const S_vector4 velocity0 = {3.0f, -0.5f, 7.0f, 0.25f}, dir0 = {9.0f, 9.0f, 9.0f, 9.0f};
    const S_vector turn0(0.1f, -0.2f, 0.05f);
    const auto start = [&] {
        v->m_velocity = velocity0;
        v->m_velocityDir = dir0;
        v->m_unknown210 = turn0;
        v->m_unknown22C0 = 0;
        g_hits = S_HitLog();
        g_partHits = 0;
    };
    start();
    v->ApplyImpulse(push, point, 0, S_vector(1.0f, 1.0f, 1.0f));
    const S_vector4 refVelocity = v->m_velocity, refDir = v->m_velocityDir;
    const S_vector refTurn = v->m_unknown210;
    start();
    v->ApplyCarHit(other, scale, point4, impulse, normal, 77);
    const bool same = SameRow(v->m_velocity, refVelocity) && SameRow(v->m_velocityDir, refDir) && v->m_unknown210 == refTurn;
    const S_vector expectedVelocity = XYZ4(velocity0) + push / v->m_currentMass;
    S_vector turn = RowTimes(arm.Cross(push), rt);
    turn = RowTimes(S_vector(turn.x * v->m_unknown220.x, turn.y * v->m_unknown220.y, turn.z * v->m_unknown220.z), r);
    const bool motion = NearVec(XYZ4(v->m_velocity), expectedVelocity, 1e-5f) &&
                        NearVec(v->m_unknown210, turn0 - turn, 1e-4f) && v->m_velocity.w == velocity0.w;
    const bool direction = NearVec(XYZ4(v->m_velocityDir), expectedVelocity / expectedVelocity.Magnitude(), 1e-5f) &&
                           v->m_velocityDir.w == velocity0.w;
    const bool calls = g_hits.impulses == 1 && g_hits.impacts == 0 && SameRow(g_hits.dirAtImpulse, v->m_velocityDir) &&
                       g_hits.flagAtImpulse == 1 && g_hits.impulseContext == v->m_callbackContext && g_partHits == 0 &&
                       v->m_unknown22C0 == 1;
    std::printf("hit car: same %d motion %d direction %d callbacks %d\n", same, motion, direction, calls);

    // With wear (the reference: ImpactDamage itself).
    const auto wearStart = [&](float least, float full) {
        v->m_unknown58C &= ~0xCu;
        v->m_unknown608 = 0.25f, v->m_unknown60C = 0.5f, v->m_unknown22A0 = 0;
        v->m_unknown610 = v->m_unknown614 = v->m_unknown63C = v->m_unknown640 = 1.0f;
        v->m_unknown618 = v->m_unknown644 = 0.0f;
        v->m_unknown61C = v->m_unknown648 = full;
        v->m_unknown620 = v->m_unknown64C = least;
    };
    const auto worn = [&] {
        return std::vector<float>{static_cast<float>(v->m_unknown58C & 0xC), v->m_unknown60C, v->m_unknown610,
                                  v->m_unknown63C, static_cast<float>(v->m_unknown22A0)};
    };
    v->m_unknownF6C |= VEHICLE_SIM_IMPACT_DAMAGE;
    wearStart(0.0f, 1e4f);
    v->ImpactDamage(PushSize(push));
    const std::vector<float> refWorn = worn();
    wearStart(0.0f, 1e4f);
    start();
    v->ApplyCarHit(other, scale, point4, impulse, normal, 77);
    const bool wore = worn() == refWorn && refWorn[2] < 1.0f && refWorn[0] == 12.0f;
    const bool impact = g_hits.impacts == 1 && g_hits.impulses == 1 && g_partHits == 0 &&
                        SameRow(v->m_velocity, refVelocity) && SameRow(v->m_velocityDir, refDir);
    const bool args = g_hits.impactContext == v->m_callbackContext && g_hits.impulse == g_hits.largest &&
                      g_hits.impulseValue == push && g_hits.point == &point4 && g_hits.radius == 0.45f &&
                      g_hits.velocity == reinterpret_cast<const S_vector *>(&v->m_velocity) &&
                      g_hits.velocityThen == XYZ4(v->m_velocity) && g_hits.actor == other && !g_hits.volume &&
                      g_hits.unknown == 1 && SameRow(g_hits.dirAtImpact, dir0) && (g_hits.wornAtImpact & 0xC) == 0xC;
    // (A push of 1e-5: none, so the least of -1 takes nothing.)
    wearStart(-1.0f, 1.0f);
    v->ImpactDamage(0.0f);
    const std::vector<float> refTiny = worn();
    wearStart(-1.0f, 1.0f);
    start();
    v->ApplyCarHit(other, 1.0f, point4, S_vector(1e-5f, 0.0f, 0.0f), normal, 77);
    const bool tiny = worn() == refTiny && refTiny[2] == 1.0f && refTiny[0] == 12.0f;
    std::printf("hit wear: wear %d impact %d args %d tiny %d\n", wore, impact, args, tiny);

    // With dents.
    v->m_unknownF6C &= ~VEHICLE_SIM_IMPACT_DAMAGE;
    v->m_unknownF6C |= VEHICLE_SIM_DEFORM;
    if (v->m_parts.empty()) {
        std::printf("hit dent: no parts\n");
    } else {
        int P = -1;
        for (size_t i = 0; i < v->m_parts.size(); ++i)
            if (!v->m_parts[i].frames &&
                (P < 0 || MeshOf(v->m_parts[i])->levels[0]->count > MeshOf(v->m_parts[P])->levels[0]->count))
                P = static_cast<int>(i);
        // (StateTest's loads leave glass dented but not marked, LoadState's quirk, and originals with
        // users counted twice: every part with an original marked and repaired until none is left.)
        for (int round = 0; round < 8 && !v->m_meshOriginals.empty(); ++round) {
            for (S_VehiclePart &p : v->m_parts)
                for (const S_VehicleMeshOriginal &o : v->m_meshOriginals)
                    if (o.partName == p.name)
                        p.state |= VEHICLE_PART_DEFORMED;
            v->RepairParts(false);
        }
        const S_VehiclePart &part = v->m_parts[P];
        const S_Mesh *mesh = MeshOf(part);
        const S_vector4 at = Point4(Positions(mesh)[0][mesh->levels[0]->count / 2] * part.frame->GetMatrix());
        const S_vector hitImpulse = S_vector(0.36f, -0.48f, 0.8f) * ((part.minImpulse + 1.6f * part.fullImpulse) / scale);
        const auto meshes = [&] {
            std::vector<std::vector<std::vector<S_vector>>> out;
            for (const S_VehiclePart &p : v->m_parts)
                out.push_back(Positions(MeshOf(p)));
            return out;
        };
        const auto before = meshes();
        g_damage = S_DamageLog();
        std::srand(11);
        v->Deform(at, S_vector4{-normal.x, -normal.y, -normal.z, 0.0f}, 0.45f, PushSize(hitImpulse * scale), 0, nullptr);
        const auto refMeshes = meshes();
        const S_DamageLog refLog = g_damage;
        v->RepairParts(false);
        const bool repaired = meshes() == before;
        g_damage = S_DamageLog();
        start();
        std::srand(11);
        v->ApplyCarHit(other, scale, at, hitImpulse, normal, 77);
        const bool sameDents = meshes() == refMeshes && refMeshes != before && g_damage.hits == refLog.hits &&
                               g_damage.dents == refLog.dents && g_damage.impulses == refLog.impulses &&
                               g_damage.points.size() == refLog.points.size() && g_hits.impacts == 0;
        std::printf("hit dent: same %d repaired %d\n", sameDents, repaired);
        v->RepairParts(false);
    }

    v->ResetCondition();
    v->m_unknownF6C = savedF6C;
    v->m_unknown58C = saved58C;
    v->m_callbacks[VEHICLE_CALLBACK_IMPACT] = savedImpact;
    v->m_callbacks[VEHICLE_CALLBACK_IMPULSE] = savedImpulse;
    v->m_unknown310 = saved310;
    v->m_unknown350 = saved350;
    v->m_velocity = savedVelocity;
    v->m_velocityDir = savedDir;
    v->m_unknown210 = savedTurn;
    v->m_unknown270 = saved270;
    v->m_unknown280 = saved280;
    for (int i = 0; i < 12; ++i)
        *wear[i] = savedWear[i];
    v->m_unknown22A0 = saved22A0;
}

// --- the sounds' loading ---------------------------------------------------------------------------

// What LoadSounds should do with each slot, in its order (from the original's calls): the group,
// the slot, the block's name field (null: the fixed "PNEU_PUNC.WAV"), the flags, the range (0: 4 /
// 50 m, 0.3, 0.2; 1: +0xEE8; 2: +0xF10; 3: +0x95C..; 4: 4 / 110 m, 0.45, 0.37; 5: 6 / 30 m, 0.3, 0.2),
// looping, at the engine; only with a wheel circumference over 0.01 (the rolling sounds).
struct S_SoundExpect {
    int group, slot;
    char *field;
    uint32_t flags;
    int range;
    bool loop, atEngine, rolling;
};
std::vector<S_SoundExpect> SoundExpectations(S_VehicleParams &p)
{
    std::vector<S_SoundExpect> e;
    const auto add = [&](int g, int s, char *f, uint32_t flags, int range, bool loop, bool engine, bool rolling = false) {
        e.push_back({g, s, f, flags, range, loop, engine, rolling});
    };
    for (int i = 0; i < 3; ++i)
        add(1, i, p.sounds1[i], 3, 0, false, true);
    for (int i = 0; i < 5; ++i)
        add(1, 3 + i, p.layers1A[i], 0xB, 0, true, true);
    for (int i = 0; i < 5; ++i)
        add(1, 8 + i, p.layers1B[i], 0xB, 0, true, true);
    for (int i = 0; i < 5; ++i)
        add(1, 13 + i, p.layers1C[i], 0xB, 0, true, true);
    add(1, 0x12, p.sound1_9B4, 3, 0, true, true);
    add(1, 0x13, p.sound1_9C4, 0xB, 0, true, false, true);
    add(1, 0x14, p.sound1_9D4, 3, 0, true, false);
    add(1, 0x15, p.sounds1_9E4[0], 3, 0, true, false);
    for (int i = 1; i < 4; ++i)
        add(1, 0x15 + i, p.sounds1_9E4[i], 3, 0, false, false);
    add(1, 0x19, p.sounds1_9E4[4], 0xB, 0, true, false);
    add(1, 0x1A, p.sounds1_9E4[5], 3, 0, true, false);
    add(1, 0x1B, p.sounds1_9E4[6], 3, 0, false, false);
    add(1, 0x1C, p.sounds1_9E4[7], 3, 0, false, false);
    for (int k = 3; k >= 0; --k) {
        add(1, 0x1D + k, p.crash1A[k], 3, 0, false, false);
        add(1, 0x21 + k, p.crash1B[k], 3, 0, false, false);
    }
    add(1, 0x25, p.sound1_AF4, 0, 0, false, false);
    add(1, 0x26, p.sound1_B04, 0, 0, false, false);
    add(1, 0x27, nullptr, 0xB, 5, true, false);
    for (int i = 0; i < 3; ++i)
        add(2, i, p.sounds2[i], 0, 1, false, true);
    add(2, 3, p.sounds2[3], 8, 1, true, true);
    for (int i = 0; i < 5; ++i)
        add(2, 4 + i, p.layers2A[i], 8, 1, true, true);
    for (int i = 0; i < 5; ++i)
        add(2, 9 + i, p.layers2B[i], 8, 1, true, true);
    for (int i = 0; i < 5; ++i)
        add(2, 14 + i, p.layers2C[i], 8, 1, true, true);
    add(2, 0x13, p.sound2_E50, 0, 1, true, true);
    add(2, 0x14, p.sound2_E60, 8, 2, true, false, true);
    add(2, 0x15, p.sound2_E70, 0, 3, true, false);
    add(2, 0x16, p.sounds2_E80[0], 0, 4, true, false);
    for (int i = 1; i < 4; ++i)
        add(2, 0x16 + i, p.sounds2_E80[i], 0, 3, false, false);
    add(2, 0x1A, p.sounds2_E80[4], 8, 3, true, false);
    add(2, 0x1B, p.sounds2_E80[5], 0, 3, true, false);
    add(2, 0x1C, p.sounds2_E80[6], 0, 3, false, false);
    add(2, 0x1D, p.sounds2_E80[7], 0, 3, false, false);
    for (int k = 3; k >= 0; --k) {
        add(2, 0x1E + k, p.crash2A[k], 0, 3, false, false);
        add(2, 0x22 + k, p.crash2B[k], 0, 3, false, false);
    }
    add(2, 0x26, p.sound2_F90, 0, 3, false, false);
    add(2, 0x27, p.sound2_FA0, 0, 3, false, false);
    add(2, 0x28, nullptr, 8, 3, true, false);
    return e;
}

// LoadSounds and LoadSound, the sectors' loops and ReleaseSounds on the started car (its own sounds
// loaded again after): every slot named (synthetic names) and loaded fresh: the slot's file, sound
// type, cone, range, parent (the primary sector or the model), loop, place (the engine's smoke dummy),
// volume 1, +0x88 0, off, the open flags (bit 3 and the cache's 0x10), in the original's order; the
// rolling sounds skipped without a wheel circumference over 0.01; loaded again: kept (no open, no
// setup); a changed name reopened in the same frame (switched off first); an empty name releases;
// a missing file releases (a slot's frame, and a new one); two sectors get every sound (the last
// sector first, the slots in order) and lose them alike; ReleaseSounds unlinks and releases all.
void SoundTest(C_Vehicle *v)
{
    S_VehicleParams p = v->m_params;
    std::vector<S_SoundExpect> e = SoundExpectations(p);
    for (const S_SoundExpect &x : e)
        if (x.field)
            std::snprintf(x.field, VEHICLE_SOUND_NAME, "g%d_%02x.wav", x.group, x.slot);
    const auto slotOf = [&](const S_SoundExpect &x) -> I3D_sound *& {
        return x.group == 1 ? v->m_sounds1[x.slot] : v->m_sounds2[x.slot];
    };
    const auto nameOf = [](const S_SoundExpect &x) {
        return std::string(x.field ? x.field : "PNEU_PUNC.WAV");
    };
    // (The block's three ranges told apart.)
    float savedRanges[12];
    std::memcpy(savedRanges, v->m_unknownEE8, 16);
    std::memcpy(savedRanges + 4, v->m_unknownF10, 16);
    savedRanges[8] = v->m_unknown95C, savedRanges[9] = v->m_unknown960;
    savedRanges[10] = v->m_unknown964, savedRanges[11] = v->m_unknown968;
    for (int i = 0; i < 4; ++i) {
        v->m_unknownEE8[i] = 1.0f + static_cast<float>(i);
        v->m_unknownF10[i] = 5.0f + static_cast<float>(i);
    }
    v->m_unknown95C = 9.0f, v->m_unknown960 = 10.0f, v->m_unknown964 = 11.0f, v->m_unknown968 = 12.0f;
    const float ranges[6][4] = {{4.0f, 50.0f, 0.3f, 0.2f},
                                {v->m_unknownEE8[0], v->m_unknownEE8[1], v->m_unknownEE8[2], v->m_unknownEE8[3]},
                                {v->m_unknownF10[0], v->m_unknownF10[1], v->m_unknownF10[2], v->m_unknownF10[3]},
                                {v->m_unknown95C, v->m_unknown960, v->m_unknown964, v->m_unknown968},
                                {4.0f, 110.0f, 0.45f, 0.37f},
                                {6.0f, 30.0f, 0.3f, 0.2f}};
    const S_vector engine(v->m_smokePos.x, v->m_smokePos.y, v->m_smokePos.z);
    const float savedCircumference = v->m_maxWheelCircumference;
    v->m_maxWheelCircumference = 0.02f;                // (just over the rolling sounds' gate)

    // Fresh: every slot.
    v->ReleaseSounds();
    g_soundSetups.clear();
    v->LoadSounds(p);
    int right = 0;
    bool order = g_soundSetups.size() == e.size();
    for (size_t i = 0; i < e.size(); ++i) {
        const S_SoundExpect &x = e[i];
        I3D_sound *s = slotOf(x);
        if (!s)
            continue;
        order &= g_soundSetups[i] == s;
        const S_FakeSound &f = g_sounds[s];
        const bool ambient = (x.flags & 3) != 0;
        uint32_t openFlags;
        std::memcpy(&openFlags, reinterpret_cast<const uint8_t *>(s) + 0x178, 4);
        bool ok = f.file == nameOf(x) && f.type == (ambient ? 3 : 1) && f.loop == x.loop && f.volume == 1.0f &&
                  f.volumes == 1 && f.unknown88 == 0 && !s->IsOn() && openFlags == ((x.flags & 8) | 0x10) &&
                  s->m_parent == (ambient ? g_sector : v->m_model) && s->m_refCount == 1;
        ok &= ambient ? f.cone[0] == 0.0f && f.cone[1] == 0.0f
                      : f.cone[0] == 6.283185f && f.cone[1] == 6.283185f &&
                            std::memcmp(f.range, ranges[x.range], sizeof f.range) == 0;
        const S_vector pos = s->GetPos();
        ok &= x.atEngine ? pos == engine : pos == S_vector(0.0f, 0.0f, 0.0f);
        right += ok;
    }
    // Without a wheel circumference over 0.01: no rolling sounds.
    v->ReleaseSounds();
    v->m_maxWheelCircumference = 0.01f;
    v->LoadSounds(p);
    v->m_maxWheelCircumference = 0.02f;                // (just over the rolling sounds' gate)
    int rollingSkipped = 0, others = 0;
    for (const S_SoundExpect &x : e) {
        if (x.rolling)
            rollingSkipped += slotOf(x) == nullptr;
        else
            others += slotOf(x) != nullptr;
    }
    v->LoadSounds(p);                                   // (the rolling ones too)
    // Again: kept.
    std::vector<I3D_sound *> before;
    int opens0 = 0;
    for (const S_SoundExpect &x : e) {
        before.push_back(slotOf(x));
        opens0 += g_sounds[slotOf(x)].opens;
    }
    g_soundSetups.clear();
    v->LoadSounds(p);
    int opens1 = 0;
    bool kept = g_soundSetups.empty();
    for (size_t i = 0; i < e.size(); ++i) {
        kept &= slotOf(e[i]) == before[i];
        opens1 += g_sounds[slotOf(e[i])].opens;
    }
    kept &= opens1 == opens0;
    // A changed name: the same frame, off, the new file.
    const S_SoundExpect &first = e[0];
    I3D_sound *const frame0 = slotOf(first);
    frame0->SetOn(true);
    std::strcpy(first.field, "other.wav");
    v->LoadSounds(p);
    const bool changed = slotOf(first) == frame0 && g_sounds[frame0].file == "other.wav" && !frame0->IsOn() &&
                         g_sounds[frame0].volumes == 2;
    // An empty name: released and out of the scene.
    const auto find = [&](int group, int slot) -> const S_SoundExpect & {
        return *std::find_if(e.begin(), e.end(),
                             [&](const S_SoundExpect &x) { return x.group == group && x.slot == slot; });
    };
    const S_SoundExpect &engineLoop = find(2, 3);
    I3D_sound *const frame3 = slotOf(engineLoop);
    engineLoop.field[0] = 0;
    v->LoadSounds(p);
    const bool emptied = engineLoop.group == 2 && engineLoop.slot == 3 && !slotOf(engineLoop) &&
                         frame3->m_refCount == 0 && !frame3->m_parent;
    // A missing file: the slot's frame released; a new one too.
    const S_SoundExpect &layer = find(1, 3);
    I3D_sound *const frameL = slotOf(layer);
    std::strcpy(layer.field, "missing1.wav");
    v->LoadSounds(p);
    const bool missing = layer.slot == 3 && !slotOf(layer) && frameL->m_refCount == 0;
    layer.field[0] = 0;                                 // (else its missing file is tried again)
    std::strcpy(engineLoop.field, "missing2.wav");
    const size_t sounds0 = g_sounds.size();
    v->LoadSounds(p);
    bool missingNew = !slotOf(engineLoop) && g_sounds.size() == sounds0 + 1;
    for (const auto &kv : g_sounds)
        if (kv.second.file.empty() && kv.second.opens == 1 && kv.first->m_refCount != 0)
            missingNew = false;

    // The sectors: the last first, each with every sound in turn.
    alignas(16) static uint8_t sectorMem[2][FRAME_SIZE];
    I3D_sector *sectors[2];
    for (int i = 0; i < 2; ++i) {
        std::memset(sectorMem[i], 0, FRAME_SIZE);
        *reinterpret_cast<void ***>(sectorMem[i]) = g_soundSectorVtable;
        sectors[i] = reinterpret_cast<I3D_sector *>(sectorMem[i]);
    }
    C_Vehicle::s_soundSectors.assign(sectors, sectors + 2);
    std::vector<S_SectorCall> want;
    for (int sector = 1; sector >= 0; --sector)
        for (const S_SoundExpect &x : e)
            if (slotOf(x))
                want.push_back({sectors[sector], slotOf(x), true});
    g_sectorCalls.clear();
    v->AddSoundsToSectors();
    const auto same = [&](bool add) {
        if (g_sectorCalls.size() != want.size())
            return 0;
        int n = 0;
        for (size_t i = 0; i < want.size(); ++i)
            n += g_sectorCalls[i].sector == want[i].sector && g_sectorCalls[i].sound == want[i].sound &&
                 g_sectorCalls[i].add == add;
        return n;
    };
    const int added = same(true);
    g_sectorCalls.clear();
    v->RemoveSoundsFromSectors();
    const int removed = same(false);
    C_Vehicle::s_soundSectors.clear();
    g_sectorCalls.clear();
    v->AddSoundsToSectors();                            // (no sectors: nothing)
    const bool none = g_sectorCalls.empty();

    // Released: every slot empty, each sound unlinked with its reference gone.
    std::vector<I3D_sound *> loaded;
    for (const S_SoundExpect &x : e)
        if (slotOf(x))
            loaded.push_back(slotOf(x));
    v->ReleaseSounds();
    int released = 0;
    for (I3D_sound *s : loaded)
        released += s->m_refCount == 0 && !s->m_parent;
    const bool emptyAfter = LoadedSounds(v) == 0;

    // Flags 2 (bit 1 alone, which no call gives): ambient as well.
    I3D_sound *probe = nullptr;
    v->LoadSound("probe.wav", probe, 2, 1.0f, 2.0f, 3.0f, 4.0f, false, false);
    const bool flags2 = probe && g_sounds[probe].type == 3 && probe->m_parent == g_sector;
    if (probe) {
        probe->LinkTo(nullptr, 0);
        probe->Release();
    }

    std::memcpy(v->m_unknownEE8, savedRanges, 16);
    std::memcpy(v->m_unknownF10, savedRanges + 4, 16);
    v->m_unknown95C = savedRanges[8], v->m_unknown960 = savedRanges[9];
    v->m_unknown964 = savedRanges[10], v->m_unknown968 = savedRanges[11];
    v->m_maxWheelCircumference = savedCircumference;
    v->LoadSounds(v->m_params);
    std::printf("sounds load: slots %d of %zu order %d rolling skipped %d others %d kept %d changed %d emptied %d "
                "missing %d/%d\n",
                right, e.size(), order, rollingSkipped, others, kept, changed, emptied, missing, missingNew);
    std::printf("sounds sectors: added %d removed %d of %zu none %d released %d of %zu empty %d flags 2 ambient %d\n", added, removed,
                want.size(), none, released, loaded.size(), emptyAfter, flags2);
}

// --- the engine sounds -----------------------------------------------------------------------------

bool Near(double a, double b)
{
    return (std::isnan(a) && std::isnan(b)) || std::fabs(a - b) <= 1e-4 * std::max(1.0, std::fabs(b));
}

// What UpdateEngineSound should leave on an engine layer (worked out apart from the port, in
// double): playing, and then its volume and frequency; `unsure` at the 0.01 edge.
struct S_SoundWant {
    bool on;
    double volume, frequency;
    bool unsure;
};

S_SoundWant WantLayer(const float (*rows)[VEHICLE_SOUND_LAYERS], int i, double r, double load, bool muted, bool wasOn,
                      double scale)
{
    const double low = rows[2][i], high = rows[3][i], in = rows[4][i], out = rows[5][i];
    if (!(r >= low && r <= high) || muted)
        return {false, 0.0, 0.0, false};
    double volume = rows[7][i] + (rows[6][i] - rows[7][i]) * load;
    const bool unsure = std::fabs(volume - 0.01) < 1e-5;
    if (volume > 0.01) {
        if (r < in && low != in)
            volume *= (r - low) / (in - low);
        else if (r > out && high != out)
            volume *= (high - r) / (high - out);
    }
    const double pitch = rows[0][i] + (rows[1][i] - rows[0][i]) * (r - low) / (high - low);
    const bool on = wasOn ? volume >= 0.01 : volume > 0.01;
    return {on, scale * volume, pitch, unsure || std::fabs(volume - 0.01) < 1e-5};
}

// One UpdateEngineSound at `revs` (after `dt`), its layers and idle loop checked (counted).
struct S_EngineCounts {
    int layers = 0, layersRight = 0, idle = 0, idleRight = 0, single = 0, singleRight = 0, calls = 0, kept = 0;
    bool untouched = true;
};

void EngineStep(C_Vehicle *v, float revs, float dt, S_EngineCounts &c)
{
    const bool group1 = v->m_unknown2297 != 0;          // (the group after the call)
    I3D_sound **layers = group1 ? v->m_sounds1 + 3 : v->m_sounds2 + 4;
    I3D_sound *idle = group1 ? v->m_sounds1[0x12] : v->m_sounds2[0x13];
    I3D_sound *single = v->m_sounds2[3];
    bool wasOn[15];
    int frequencies[15];
    for (int k = 0; k < 15; ++k) {
        wasOn[k] = layers[k] && layers[k]->IsOn();
        frequencies[k] = layers[k] ? g_sounds[layers[k]].frequencies : 0;
    }
    const bool idleWasOn = idle && idle->IsOn();
    const bool singleMode = !group1 && !(v->m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS);
    // The fade-in: up by dt to 0.3 s, the volumes scaled by it over 0.3.
    const float fade = std::min(v->m_unknownF48 + dt, 0.3f);
    g_soundLog.clear();
    v->m_engineRevs = revs;
    v->UpdateEngineSound(dt);
    ++c.calls;
    c.kept += C_Vehicle::s_soundVolume == 1.0f && v->m_unknownF48 == fade;
    const double scale = static_cast<double>(fade) / 0.3;
    const float idleRevs = v->m_idleRevs, top = v->m_revLimit - 0.001f;
    const double r = std::min(std::max(revs, idleRevs), top);

    if (singleMode) {
        // The layers alone; the single loop by the revs.
        for (const auto &call : g_soundLog)
            for (int k = 0; k < 15; ++k)
                c.untouched &= call.first != layers[k] || !layers[k];
        if (single) {
            const double idleR = v->m_idleRevs, limit = v->m_revLimit;
            const double volume = r < v->m_unknownCF4 ? (r - idleR) / (v->m_unknownCF4 - idleR) * v->m_unknownCF8
                                                      : v->m_unknownCF8;
            const double pitch = v->m_unknownCEC + (v->m_unknownCF0 - v->m_unknownCEC) * (r - idleR) / (limit - idleR);
            ++c.single;
            c.singleRight += single->IsOn() && Near(g_sounds[single].volume, scale * volume) &&
                             Near(g_sounds[single].frequency, pitch);
        }
    } else {
        const auto &rows = group1 ? v->m_layers1 : v->m_layers2;
        const uint8_t *flags[2] = {group1 ? v->m_layers1AFlags : v->m_layers2AFlags,
                                   group1 ? v->m_layers1BFlags : v->m_layers2BFlags};
        const double load = v->m_unknown78C;
        for (int set = 0; set < 3; ++set) {
            for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i) {
                I3D_sound *s = layers[set * VEHICLE_SOUND_LAYERS + i];
                if (!s)
                    continue;
                const bool active = v->m_gear >= 0 ? set < 2 : set == 2;
                S_SoundWant w = {false, 0.0, 0.0, false};
                if (active)
                    w = WantLayer(rows[set], i, r, set == 1 ? 1.0 - load : load,
                                  set < 2 && flags[set][i] && v->m_gear == 0, wasOn[set * VEHICLE_SOUND_LAYERS + i], scale);
                if (w.unsure)
                    continue;
                const S_FakeSound &f = g_sounds[s];
                bool ok = s->IsOn() == w.on;
                if (w.on)
                    ok = ok && f.frequencies > frequencies[set * VEHICLE_SOUND_LAYERS + i] &&
                         Near(f.frequency, w.frequency) && Near(f.volume, w.volume);
                ++c.layers;
                c.layersRight += ok;
            }
        }
    }
    if (idle) {
        const double loud = group1 ? v->m_unknownC00 : v->m_unknownEE0, silent = group1 ? v->m_unknownBFC : v->m_unknownEDC;
        const double peak = group1 ? v->m_unknownC04 : v->m_unknownEE4;
        const bool in = r >= loud && r <= silent;
        const double volume = in ? (silent - r) / (silent - loud) * peak : 0.0;
        if (!in || std::fabs(volume - 0.01) >= 1e-5) {
            const bool on = in && (idleWasOn ? volume >= 0.01 : volume > 0.01);
            ++c.idle;
            c.idleRight += idle->IsOn() == on && (!on || Near(g_sounds[idle].volume, scale * volume));
        }
    }
}

// The engine sounds: UpdateEngineSound over the revs (both groups, in gear, neutral, reverse,
// the layers' flags as loaded and all set; fading in), the single loop, the group switch;
// SwitchEngine's start and stop, PlayEngineFail, PlayGearSound, StopEngineLoops, StopSounds (on
// fakes in every slot).
void EngineSoundTest(C_Vehicle *v)
{
    const bool engineOn = v->m_engineOn, soundsOn = v->m_soundsOn;
    const uint8_t u22A2 = v->m_unknown22A2, u22A0 = v->m_unknown22A0, u2297 = v->m_unknown2297, u2298 = v->m_unknown2298;
    const float f48 = v->m_unknownF48, revs0 = v->m_engineRevs, load0 = v->m_unknown78C;
    const int gear0 = v->m_gear, u650 = v->m_unknown650;
    const uint32_t f6c = v->m_unknownF6C;
    uint8_t flags[4][VEHICLE_SOUND_LAYERS];
    std::memcpy(flags[0], v->m_layers1AFlags, 5), std::memcpy(flags[1], v->m_layers1BFlags, 5);
    std::memcpy(flags[2], v->m_layers2AFlags, 5), std::memcpy(flags[3], v->m_layers2BFlags, 5);
    I3D_sound *saved1[40], *saved2[41];
    std::memcpy(saved1, v->m_sounds1, sizeof saved1);
    std::memcpy(saved2, v->m_sounds2, sizeof saved2);
    std::vector<std::pair<I3D_sound *, uint32_t>> onBits;
    for (I3D_sound *s : saved1)
        if (s)
            onBits.push_back({s, s->m_frameFlags});
    for (I3D_sound *s : saved2)
        if (s)
            onBits.push_back({s, s->m_frameFlags});
    const auto allOff = [&] {
        for (I3D_sound *s : v->m_sounds1)
            if (s)
                F_SetOn(s, false), g_sounds[s].ended = false;
        for (I3D_sound *s : v->m_sounds2)
            if (s)
                F_SetOn(s, false), g_sounds[s].ended = false;
    };

    // Fakes in every slot, synthetic layers (the models' blocks name no engine sounds): per layer
    // a pitch range, its revs across idle..limit (some from below idle or past the limit, one in
    // four without a fade-in, one without a fade-out, one nearly silent on load).
    I3D_sound *fake1[40], *fake2[41];
    for (I3D_sound *&s : fake1)
        s = static_cast<I3D_sound *>(MakeSound());
    for (I3D_sound *&s : fake2)
        s = static_cast<I3D_sound *>(MakeSound());
    std::memcpy(v->m_sounds1, fake1, sizeof fake1);
    std::memcpy(v->m_sounds2, fake2, sizeof fake2);
    float savedLayers[2][3][8][VEHICLE_SOUND_LAYERS];
    std::memcpy(savedLayers[0], v->m_layers1, sizeof v->m_layers1);
    std::memcpy(savedLayers[1], v->m_layers2, sizeof v->m_layers2);
    const float savedLoops[11] = {v->m_unknownBFC, v->m_unknownC00, v->m_unknownC04, v->m_unknownEDC,
                                  v->m_unknownEE0, v->m_unknownEE4, v->m_unknownCEC, v->m_unknownCF0,
                                  v->m_unknownCF4, v->m_unknownCF8, 0.0f};
    uint32_t seed = 12345;
    const auto next = [&] {
        seed = seed * 1103515245u + 12345u;
        return static_cast<float>((seed >> 8) % 1000) / 1000.0f;
    };
    const float idleRevs = v->m_idleRevs, span = v->m_revLimit - idleRevs;
    for (auto *layers : {&v->m_layers1, &v->m_layers2})
        for (int set = 0; set < 3; ++set)
            for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i) {
                float(*l)[VEHICLE_SOUND_LAYERS] = (*layers)[set];
                float a = next(), b = next();
                if (a > b)
                    std::swap(a, b);
                l[0][i] = 0.5f + next();
                l[1][i] = l[0][i] + 0.1f + next();
                l[2][i] = idleRevs + span * a * 0.9f - 2.0f;
                l[3][i] = idleRevs + span * (b * 0.9f + 0.1f) + 2.0f;
                const int kind = (set * VEHICLE_SOUND_LAYERS + i) % 4;
                l[4][i] = kind == 1 ? l[2][i] : l[2][i] + (l[3][i] - l[2][i]) * 0.3f;
                l[5][i] = kind == 2 ? l[3][i] : l[3][i] - (l[3][i] - l[2][i]) * 0.3f;
                l[6][i] = kind == 3 ? 0.005f : 0.1f + next();
                l[7][i] = next() * 0.5f;
            }
    v->m_unknownC00 = idleRevs + span * 0.1f, v->m_unknownBFC = idleRevs + span * 0.4f, v->m_unknownC04 = 0.8f;
    v->m_unknownEE0 = idleRevs + span * 0.05f, v->m_unknownEDC = idleRevs + span * 0.6f, v->m_unknownEE4 = 0.6f;
    v->m_unknownCEC = 0.8f, v->m_unknownCF0 = 1.6f, v->m_unknownCF4 = idleRevs + span * 0.5f, v->m_unknownCF8 = 0.9f;

    // The revs: over idle - 5 .. the limit + 5 and at every layer's and idle loop's edges, up and down.
    std::vector<float> grid;
    for (int k = 0; k <= 40; ++k)
        grid.push_back(v->m_idleRevs - 5.0f + (v->m_revLimit - v->m_idleRevs + 10.0f) * static_cast<float>(k) / 40.0f);
    for (const auto *layers : {&v->m_layers1, &v->m_layers2})
        for (int set = 0; set < 3; ++set)
            for (int row = 2; row <= 5; ++row)
                for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i)
                    grid.push_back((*layers)[set][row][i]);
    for (float edge : {v->m_unknownBFC, v->m_unknownC00, v->m_unknownEDC, v->m_unknownEE0, v->m_unknownCF4})
        grid.push_back(edge);
    std::sort(grid.begin(), grid.end());

    S_EngineCounts c;
    v->m_engineOn = v->m_soundsOn = true;
    v->m_unknown78C = 0.7f;
    for (int allFlags = 0; allFlags < 2; ++allFlags) {
        for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i) {
            // Flags on layers 0, 2, 4 of set A and 1, 3 of set B.
            v->m_layers1AFlags[i] = v->m_layers2AFlags[i] = allFlags && i % 2 == 0;
            v->m_layers1BFlags[i] = v->m_layers2BFlags[i] = allFlags && i % 2 == 1;
        }
        for (int group = 0; group < 2; ++group) {
            for (uint32_t detail : {static_cast<uint32_t>(VEHICLE_SIM_DETAIL_SOUNDS), 0u}) {
                allOff();
                v->m_unknownF6C = (f6c & ~VEHICLE_SIM_DETAIL_SOUNDS) | detail;
                v->m_unknown2297 = v->m_unknown2298 = static_cast<uint8_t>(group);
                v->m_unknownF48 = 0.0f;                     // fading in over the first 75 steps
                if (!detail && !group) {
                    // The single loop: the layers left as they are.
                    for (int k = 0; k < 15; ++k)
                        if (v->m_sounds2[4 + k])
                            F_SetOn(v->m_sounds2[4 + k], true);
                }
                // (The gears in turn: each change leaves the other sets' layers playing.)
                for (int gear : {-1, 1, 0, -1}) {
                    v->m_gear = gear;
                    for (float r : grid)
                        EngineStep(v, r, 0.004f, c);
                    for (size_t k = grid.size(); k-- > 0;)
                        EngineStep(v, grid[k], 0.004f, c);
                    for (size_t k = 0; k < grid.size() / 2; ++k)   // (up to the middle for the change)
                        EngineStep(v, grid[k], 0.004f, c);
                }
            }
        }
    }
    std::memcpy(v->m_layers1AFlags, flags[0], 5), std::memcpy(v->m_layers1BFlags, flags[1], 5);
    std::memcpy(v->m_layers2AFlags, flags[2], 5), std::memcpy(v->m_layers2BFlags, flags[3], 5);
    v->m_unknownF6C = f6c | VEHICLE_SIM_DETAIL_SOUNDS;
    const float middle = (v->m_idleRevs + v->m_revLimit) * 0.5f;

    // The group switch: the old group's loops (the layers, idle, rolling, group 2's single) stop,
    // its others go on.
    bool switched = true;
    for (int to = 0; to < 2; ++to) {
        allOff();
        v->m_unknown2298 = static_cast<uint8_t>(!to);
        v->m_unknown2297 = static_cast<uint8_t>(to);
        I3D_sound **old = to ? v->m_sounds2 : v->m_sounds1;
        std::vector<int> stops;
        for (int k = 0; k < 15; ++k)
            stops.push_back((to ? 4 : 3) + k);
        for (int k : to ? std::vector<int>{0x13, 0x14, 3} : std::vector<int>{0x12, 0x13})
            stops.push_back(k);
        const int other = to ? 0x15 : 0x14;
        for (int k : stops)
            if (old[k])
                F_SetOn(old[k], true);
        if (old[other])
            F_SetOn(old[other], true);
        v->m_engineRevs = middle;
        v->UpdateEngineSound(0.01f);
        for (int k : stops)
            switched &= !old[k] || !old[k]->IsOn();
        switched &= (!old[other] || old[other]->IsOn()) && v->m_unknown2298 == to;
    }

    // Off: nothing.
    allOff();
    v->m_engineOn = false;
    v->m_unknownF48 = 0.123f;
    g_soundLog.clear();
    v->UpdateEngineSound(0.01f);
    const bool off = g_soundLog.empty() && v->m_unknownF48 == 0.123f;

    I3D_sound **s1 = v->m_sounds1, **s2 = v->m_sounds2;
    const auto reset = [&] {
        for (I3D_sound *s : fake1)
            F_SetOn(s, false), g_sounds[s].ended = false, g_sounds[s].length = g_sounds[s].position = 0;
        for (I3D_sound *s : fake2)
            F_SetOn(s, false), g_sounds[s].ended = false, g_sounds[s].length = g_sounds[s].position = 0;
        g_soundLog.clear();
    };
    using Log = std::vector<std::pair<const I3D_frame *, char>>;
    const auto started = [&](I3D_sound *s) {
        return g_soundLog == Log{{s, 'O'}, {s, 'V'}} && g_sounds[s].volume == 1.0f;
    };
    // StopSounds' order: each group's slots before the crash rows, the rows pairwise from the last,
    // the rest.
    Log allStops;
    for (int k = 0; k < 0x1D; ++k)
        allStops.push_back({fake1[k], 'S'});
    for (int k = 4; k-- > 0;)
        allStops.push_back({fake1[0x1D + k], 'S'}), allStops.push_back({fake1[0x21 + k], 'S'});
    for (int k = 0x25; k < 40; ++k)
        allStops.push_back({fake1[k], 'S'});
    for (int k = 0; k < 0x1E; ++k)
        allStops.push_back({fake2[k], 'S'});
    for (int k = 4; k-- > 0;)
        allStops.push_back({fake2[0x1E + k], 'S'}), allStops.push_back({fake2[0x22 + k], 'S'});
    for (int k = 0x26; k < 41; ++k)
        allStops.push_back({fake2[k], 'S'});

    reset();
    v->StopSounds();
    const bool soundsStopped = g_soundLog == allStops;
    reset();
    v->StopEngineLoops();
    Log loops;
    for (int k = 3; k < 18; ++k)
        loops.push_back({fake1[k], 'S'});
    loops.push_back({fake1[0x12], 'S'});
    loops.push_back({fake2[3], 'S'});
    for (int k = 4; k < 19; ++k)
        loops.push_back({fake2[k], 'S'});
    loops.push_back({fake2[0x13], 'S'});
    const bool loopsStopped = g_soundLog == loops;

    // SwitchEngine on: the starter (1000 ms long) plays, the engine waits for its last 300 ms.
    reset();
    v->m_soundsOn = true;
    v->m_engineOn = false;
    v->m_unknown22A2 = 0;
    v->m_unknown2297 = 0, v->m_unknown2298 = 1;
    v->m_unknownF48 = 5.0f;
    g_sounds[s2[0]].length = 1000;
    v->SwitchEngine(true);
    const bool start = g_soundLog == Log{{s2[0], 'P'}, {s2[0], 'O'}, {s2[0], 'V'}} && g_sounds[s2[0]].playAt == 0 &&
                       g_sounds[s2[0]].volume == 1.0f && v->m_unknown22A2 == 1 && !v->m_engineOn &&
                       v->m_unknownF48 == 0.0f && v->m_unknown2298 == 1;
    g_soundLog.clear();
    g_sounds[s2[0]].position = 699;
    v->m_unknownF48 = 5.0f;
    v->SwitchEngine(true);
    const bool waiting = g_soundLog.empty() && !v->m_engineOn && v->m_unknownF48 == 0.0f;
    g_sounds[s2[0]].position = 700;
    v->m_unknownF48 = 5.0f;
    v->SwitchEngine(true);
    const bool running = g_soundLog.empty() && v->m_engineOn && v->m_unknown2298 == 0 && v->m_unknownF48 == 5.0f;
    // A starter of 300 ms runs at once; a position past the length waits (unsigned).
    reset();
    v->m_engineOn = false, v->m_unknown22A2 = 0;
    g_sounds[s2[0]].length = 300, g_sounds[s2[0]].position = 400;   // (300 - 400 wraps: only the length decides)
    v->SwitchEngine(true);
    const bool shortStart = v->m_engineOn;
    reset();
    v->m_engineOn = false, v->m_unknown22A2 = 0;
    g_sounds[s2[0]].length = 1000, g_sounds[s2[0]].position = 2000;
    v->SwitchEngine(true);
    const bool wrapped = !v->m_engineOn && v->m_unknown22A2 == 1;
    // Each of the four playing blocks a new start (an ended one does not); a long one playing
    // when starting (22A2 already set) keeps it waiting.
    bool blocked = true;
    for (I3D_sound *s : {s1[0], s1[1], s2[0], s2[1]}) {
        reset();
        v->m_engineOn = false, v->m_unknown22A2 = 0;
        F_SetOn(s, true);
        g_soundLog.clear();
        v->SwitchEngine(true);
        blocked &= g_soundLog.empty() && v->m_unknown22A2 == 0 && !v->m_engineOn;
        reset();
        v->m_engineOn = false, v->m_unknown22A2 = 0;
        F_SetOn(s, true);
        g_sounds[s].ended = true;
        g_soundLog.clear();
        v->SwitchEngine(true);
        blocked &= v->m_unknown22A2 == 1 && v->m_engineOn;
        reset();
        v->m_engineOn = false, v->m_unknown22A2 = 1;
        F_SetOn(s, true);
        g_sounds[s].length = 5000;
        g_soundLog.clear();
        v->SwitchEngine(true);
        blocked &= g_soundLog.empty() && !v->m_engineOn;
    }
    // Group 1's starter; without sounds: on at once (a long starter playing ignored).
    reset();
    v->m_engineOn = false, v->m_unknown22A2 = 0;
    v->m_unknown2297 = 1, v->m_unknown2298 = 0;
    v->SwitchEngine(true);
    const bool group1Start =
        g_soundLog == Log{{s1[0], 'P'}, {s1[0], 'O'}, {s1[0], 'V'}} && v->m_engineOn && v->m_unknown2298 == 1;
    reset();
    v->m_soundsOn = false;
    v->m_engineOn = false, v->m_unknown22A2 = 0;
    v->m_unknown2297 = 0, v->m_unknown2298 = 1;
    F_SetOn(s2[0], true);
    g_sounds[s2[0]].length = 5000;
    g_soundLog.clear();
    v->SwitchEngine(true);
    const bool silentStart = g_soundLog.empty() && v->m_engineOn && v->m_unknown22A2 == 1 && v->m_unknown2298 == 0;

    // SwitchEngine off: every sound stops, the wanted group's stop sound plays, the four rewound.
    bool stopped[3] = {};
    for (int k = 0; k < 3; ++k) {
        reset();
        v->m_soundsOn = k != 2;
        v->m_engineOn = true, v->m_unknown22A2 = 1, v->m_unknown22A0 = 1;
        v->m_unknown2297 = static_cast<uint8_t>(k == 1);
        v->m_engineRevs = 50.0f;
        v->SwitchEngine(false);
        Log want = allStops;
        I3D_sound *stop = k == 1 ? s1[1] : s2[1];
        if (k != 2)
            want.push_back({stop, 'O'}), want.push_back({stop, 'V'});
        for (I3D_sound *s : {s1[0], s1[1], s2[0], s2[1]})
            want.push_back({s, 'P'});
        stopped[k] = g_soundLog == want && v->m_engineRevs == 0.0f && !v->m_engineOn && v->m_unknown22A2 == 0 &&
                     v->m_unknown22A0 == 0 && g_sounds[s2[1]].playAt == 0;
    }
    reset();
    v->m_soundsOn = true;
    v->m_engineOn = true, v->m_unknown22A2 = 0;
    v->m_engineRevs = 50.0f;
    v->SwitchEngine(false);
    bool notStarted = g_soundLog.empty() && v->m_engineOn && v->m_engineRevs == 50.0f;
    // On while not started (22A2 clear): no starter, still not started.
    reset();
    v->SwitchEngine(true);
    notStarted &= g_soundLog.empty() && v->m_unknown22A2 == 0;

    // PlayEngineFail: the playing group's sound, unless either plays.
    reset();
    v->m_unknown2298 = 0;
    v->PlayEngineFail();
    bool fail = started(s2[2]);
    g_soundLog.clear();
    v->PlayEngineFail();
    fail &= g_soundLog.empty();
    reset();
    F_SetOn(s1[2], true);
    g_soundLog.clear();
    v->PlayEngineFail();
    fail &= g_soundLog.empty();
    reset();
    v->m_unknown2298 = 1;
    v->PlayEngineFail();
    fail &= started(s1[2]);

    // PlayGearSound: with the detail sounds, one of the two (the second one time in five, by
    // rand), unless the last one plays in either group.
    reset();
    v->m_unknownF6C = f6c & ~VEHICLE_SIM_DETAIL_SOUNDS;
    std::srand(7);
    const int firstRand = std::rand();
    std::srand(7);
    v->PlayGearSound();
    const bool noDetail = g_soundLog.empty() && std::rand() == firstRand;
    v->m_unknownF6C = f6c | VEHICLE_SIM_DETAIL_SOUNDS;
    constexpr int GEAR_RUNS = 40;
    int wantIndex[GEAR_RUNS];
    // A seed whose draws hold both 79 and 80 (the edge).
    unsigned gearSeed = 1;
    for (;; ++gearSeed) {
        std::srand(gearSeed);
        bool has79 = false, has80 = false;
        for (int &w : wantIndex) {
            const int draw = std::rand() % 100;
            has79 |= draw == 79, has80 |= draw == 80;
            w = draw > 79;
        }
        if (has79 && has80)
            break;
    }
    std::srand(gearSeed);
    int gearRight = 0, seconds = 0;
    for (int n = 0; n < GEAR_RUNS; ++n) {
        reset();
        v->m_unknown2298 = static_cast<uint8_t>(n % 2);
        v->m_unknown650 = (n / 2) % 2;
        v->PlayGearSound();
        I3D_sound *want = n % 2 ? s1[0x17 + wantIndex[n]] : s2[0x18 + wantIndex[n]];
        gearRight += started(want) && v->m_unknown650 == wantIndex[n];
        seconds += wantIndex[n];
    }
    bool gearBlocked = true;
    for (I3D_sound *s : {s1[0x18], s2[0x19]}) {
        reset();
        v->m_unknown650 = 1;
        F_SetOn(s, true);
        g_soundLog.clear();
        std::srand(7);
        v->PlayGearSound();
        gearBlocked &= g_soundLog.empty() && v->m_unknown650 == 1 && std::rand() == firstRand;
    }
    reset();
    v->m_unknown650 = 1, v->m_unknown2298 = 0;
    F_SetOn(s1[0x17], true);                            // (not the last one)
    g_soundLog.clear();
    v->PlayGearSound();
    gearBlocked &= g_soundLog.size() == 2;

    // Put back.
    std::memcpy(v->m_sounds1, saved1, sizeof saved1);
    std::memcpy(v->m_sounds2, saved2, sizeof saved2);
    std::memcpy(v->m_layers1, savedLayers[0], sizeof v->m_layers1);
    std::memcpy(v->m_layers2, savedLayers[1], sizeof v->m_layers2);
    v->m_unknownBFC = savedLoops[0], v->m_unknownC00 = savedLoops[1], v->m_unknownC04 = savedLoops[2];
    v->m_unknownEDC = savedLoops[3], v->m_unknownEE0 = savedLoops[4], v->m_unknownEE4 = savedLoops[5];
    v->m_unknownCEC = savedLoops[6], v->m_unknownCF0 = savedLoops[7], v->m_unknownCF4 = savedLoops[8];
    v->m_unknownCF8 = savedLoops[9];
    for (const auto &bits : onBits)
        bits.first->m_frameFlags = bits.second;
    v->m_engineOn = engineOn, v->m_soundsOn = soundsOn;
    v->m_unknown22A2 = u22A2, v->m_unknown22A0 = u22A0, v->m_unknown2297 = u2297, v->m_unknown2298 = u2298;
    v->m_unknownF48 = f48, v->m_engineRevs = revs0, v->m_unknown78C = load0;
    v->m_gear = gear0, v->m_unknown650 = u650;
    v->m_unknownF6C = f6c;
    std::printf("engine sounds: layers %d of %d idle %d of %d single %d of %d untouched %d volume kept %d of %d "
                "switch %d off %d\n",
                c.layersRight, c.layers, c.idleRight, c.idle, c.singleRight, c.single, c.untouched, c.kept, c.calls,
                switched, off);
    std::printf("engine start: started %d waiting %d running %d short %d wrapped %d blocked %d group 1 %d silent %d "
                "stopped %d %d %d not started %d\n",
                start, waiting, running, shortStart, wrapped, blocked, group1Start, silentStart, stopped[0], stopped[1],
                stopped[2], notStarted);
    std::printf("engine one-offs: fail %d gear %d of %d second %d no detail %d blocked %d loops stopped %d sounds "
                "stopped %d\n",
                fail, gearRight, GEAR_RUNS, seconds, noDetail, gearBlocked, loopsStopped, soundsStopped);
}

// The door, bump and crash slots the models leave empty get one-shot fakes that count their
// starts into the door / bump / impact counters (the real PlayDoorSound, PlayBumpSound and
// PlayImpactSound run: a one-shot never plays on, so each call that sounds starts one).
void PlantCountingSounds(C_Vehicle *v)
{
    const auto plant = [&](I3D_sound *&slot, int *counter) {
        if (slot)
            return;
        slot = static_cast<I3D_sound *>(MakeSound());
        g_sounds[slot].oneShot = true;
        g_sounds[slot].starts = counter;
    };
    plant(v->m_sounds1[0x25], &g_doorSounds[1]), plant(v->m_sounds2[0x26], &g_doorSounds[1]);
    plant(v->m_sounds1[0x26], &g_doorSounds[0]), plant(v->m_sounds2[0x27], &g_doorSounds[0]);
    plant(v->m_sounds1[0x1B], &g_bumpSounds), plant(v->m_sounds2[0x1C], &g_bumpSounds);
    for (int k = 0; k < 2 * VEHICLE_CRASH_SOUNDS; ++k)
        plant(v->m_sounds1[0x1D + k], &g_impactSounds), plant(v->m_sounds2[0x1E + k], &g_impactSounds);
}

// --- the running sounds ------------------------------------------------------------------------

// A speed sound's (rolling, rim) state after an update at `speed` (worked out apart, in double):
// `curve` pitch low / high, the lowest, top and full-volume speeds.
S_SoundWant WantSpeedSound(const float *curve, double speed, bool wasOn)
{
    const double low = curve[2], top = curve[3], full = curve[4];
    const double s = std::min(speed, top);
    if (!(s >= low))
        return {false, 0.0, 0.0, false};
    const double volume = s < full && full != low ? (s - low) / (full - low) : 1.0;
    const double pitch = curve[0] + (curve[1] - curve[0]) * (s - low) / (top - low);
    const bool on = wasOn ? volume >= 0.01 : volume > 0.01;
    return {on, volume, pitch, std::fabs(volume - 0.01) < 1e-5};
}

// The tyres' squeal level of the wheels as they are (worked out apart, after the original's
// float steps).
float WantSquealLevel(const C_Vehicle *v)
{
    float level = 0.0f;
    for (int i = 0; i < v->m_wheelCount; ++i) {
        const S_VehicleWheel *w = v->m_wheels[i];
        const uint32_t f = w->flags;
        if (!(f & WHEEL_ON_GROUND) || (f & WHEEL_DUMMY) || !(v->m_speed > 0.6f) ||
            !g_carSurfaces[g_carSurfaceOfMaterial[w->surface]].tyreSqueal)
            continue;
        const float s = w->unknown1D4, lo = w->unknown220, hi = w->unknown224;
        float x = (f & WHEEL_LOCKED) || s > hi ? 1.0f : s > lo ? std::max((s - lo) / (hi - lo), 0.0f) : 0.0f;
        const S_vector &a = w->unknown0A0;
        const float square = (a.x * a.x + a.y * a.y) + a.z * a.z;
        if (square < 4.0f)
            x = x * square * 0.25f;
        if (f & WHEEL_SPINNING) {
            const float spin = v->m_engineRevs / v->m_gearRatios[v->m_gear + 1] * v->m_maxWheelCircumference;
            const float over = v->m_unknown5E4 + v->m_speed;
            if (spin > over)
                x = std::max(x, spin > v->m_unknown5E8 + v->m_speed ? 1.0f : (spin - over) / v->m_unknown5E8);
        }
        level = std::max(level, x);
    }
    return level > 0.99999f ? 1.0f : level;
}

// The running sounds on fakes in every slot: UpdateRollingSound and UpdateRimSound over the speeds
// (both groups and curves, without fade-in, group 2 without the details; the rim by the speed
// along the car, off); UpdateTyreSqueal over random wheel states (a squealing and a quiet surface,
// rising and falling); PlayHandbrakeSound, SwitchLoopSoundA / B, PlayWheelBurst.
void RunningSoundTest(C_Vehicle *v)
{
    I3D_sound *saved1[40], *saved2[41], *fake1[40], *fake2[41];
    std::memcpy(saved1, v->m_sounds1, sizeof saved1);
    std::memcpy(saved2, v->m_sounds2, sizeof saved2);
    for (I3D_sound *&s : fake1)
        s = static_cast<I3D_sound *>(MakeSound());
    for (I3D_sound *&s : fake2)
        s = static_cast<I3D_sound *>(MakeSound());
    std::memcpy(v->m_sounds1, fake1, sizeof fake1);
    std::memcpy(v->m_sounds2, fake2, sizeof fake2);
    const auto reset = [&] {
        for (I3D_sound *s : fake1)
            F_SetOn(s, false), g_sounds[s].ended = false;
        for (I3D_sound *s : fake2)
            F_SetOn(s, false), g_sounds[s].ended = false;
        g_soundLog.clear();
    };
    using Log = std::vector<std::pair<const I3D_frame *, char>>;
    const uint8_t u2298 = v->m_unknown2298, u228E = v->m_unknown228E;
    const bool soundsOn = v->m_soundsOn;
    const uint32_t f6c = v->m_unknownF6C;
    const S_vector4 velocity = v->m_velocity, axisZ = v->m_modelAxisZ;
    float curves[4][5];
    float *const rolling1[5] = {&v->m_unknownC08, &v->m_unknownC0C, &v->m_unknownC10, &v->m_unknownC14, &v->m_unknownC18};
    float *const rolling2[5] = {&v->m_unknownEF8, &v->m_unknownEFC, &v->m_unknownF00, &v->m_unknownF04, &v->m_unknownF08};
    for (int k = 0; k < 5; ++k) {
        curves[0][k] = *rolling1[k], curves[1][k] = *rolling2[k];
        curves[2][k] = v->m_unknownC20[k], curves[3][k] = v->m_unknownF20[k];
    }
    const float speed0 = v->m_speed, revs0 = v->m_engineRevs, f5E4 = v->m_unknown5E4, f5E8 = v->m_unknown5E8;
    const float f5EC = v->m_unknown5EC, f5F0 = v->m_unknown5F0;
    const int gear0 = v->m_gear;

    // --- the speed sounds ---
    const float synthetic[4][5] = {{0.7f, 1.5f, 2.0f, 30.0f, 10.0f},
                                   {0.6f, 1.8f, 3.0f, 25.0f, 12.0f},
                                   {0.8f, 1.4f, 1.0f, 20.0f, 6.0f},
                                   {0.9f, 1.6f, 4.0f, 28.0f, 9.0f}};
    std::vector<float> speeds;
    for (int k = 0; k <= 110; ++k)
        speeds.push_back(static_cast<float>(k) * 0.37f);
    for (float edge : {1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 9.0f, 10.0f, 12.0f, 20.0f, 25.0f, 28.0f, 30.0f})
        speeds.push_back(edge);
    for (float low : {1.0f, 2.0f, 3.0f, 4.0f})          // (just over the lowest: under 0.01's volume)
        speeds.push_back(low + 0.03f), speeds.push_back(low + 0.05f);
    std::sort(speeds.begin(), speeds.end());
    int rollingChecked = 0, rollingRight = 0, rimChecked = 0, rimRight = 0;
    bool untouched = true;
    v->m_modelAxisZ = {0.0f, 0.0f, 1.0f, 0.0f};
    for (int noFade = 0; noFade < 2; ++noFade) {
        float c[4][5];
        std::memcpy(c, synthetic, sizeof c);
        if (noFade)
            for (auto &curve : c)
                curve[4] = curve[2];                    // full volume from the lowest speed
        for (int k = 0; k < 5; ++k) {
            *rolling1[k] = c[0][k], *rolling2[k] = c[1][k];
            v->m_unknownC20[k] = c[2][k], v->m_unknownF20[k] = c[3][k];
        }
        for (int mode = 0; mode < 3; ++mode) {          // group 1, group 2, group 2 without the details
            v->m_unknown2298 = mode == 0;
            v->m_unknownF6C = mode == 2 ? f6c & ~VEHICLE_SIM_DETAIL_SOUNDS : f6c | VEHICLE_SIM_DETAIL_SOUNDS;
            for (int rim = 0; rim < 2; ++rim) {
                I3D_sound *sound = rim ? (mode == 0 ? fake1[0x27] : fake2[0x28]) : (mode == 0 ? fake1[0x13] : fake2[0x14]);
                const float *curve = c[(rim ? 2 : 0) + (mode != 0)];
                reset();
                for (int pass = 0; pass < 2; ++pass) {
                    for (size_t n = 0; n < speeds.size(); ++n) {
                        const float s = speeds[pass ? speeds.size() - 1 - n : n];
                        // The rolling sound by the speed (along x or down); the rim's by the speed
                        // along the car only (sideways and up / down besides).
                        if (rim)
                            v->m_velocity = {3.0f, 1.0f, n % 2 ? -s : s, 0.0f};
                        else
                            v->m_velocity = n % 2 ? S_vector4{s, 0.0f, 0.0f, 0.0f} : S_vector4{0.0f, -s, 0.0f, 0.0f};
                        const bool wasOn = sound->IsOn();
                        const int frequencies = g_sounds[sound].frequencies;
                        g_soundLog.clear();
                        if (rim)
                            v->UpdateRimSound(true);
                        else
                            v->UpdateRollingSound();
                        if (mode == 2) {
                            untouched &= g_soundLog.empty();
                            continue;
                        }
                        const S_SoundWant w = WantSpeedSound(curve, s, wasOn);
                        if (w.unsure)
                            continue;
                        bool ok = sound->IsOn() == w.on;
                        for (const auto &call : g_soundLog)
                            ok &= call.first == sound;
                        if (w.on)
                            ok &= g_sounds[sound].frequencies > frequencies && Near(g_sounds[sound].frequency, w.frequency) &&
                                  Near(g_sounds[sound].volume, w.volume);
                        (rim ? rimChecked : rollingChecked) += 1;
                        (rim ? rimRight : rollingRight) += ok;
                    }
                }
            }
        }
    }
    reset();
    v->UpdateRimSound(false);
    const bool rimOff = g_soundLog == Log{{fake1[0x27], 'o'}, {fake2[0x28], 'o'}};

    // --- the tyres' squeal: random wheel states (a quiet surface for material 5) ---
    struct S_WheelState {
        uint32_t flags;
        int surface;
        float s1D4, s220, s224;
        S_vector a0A0;
    };
    std::vector<S_WheelState> wheels0;
    for (int i = 0; i < v->m_wheelCount; ++i) {
        const S_VehicleWheel *w = v->m_wheels[i];
        wheels0.push_back({w->flags, w->surface, w->unknown1D4, w->unknown220, w->unknown224, w->unknown0A0});
    }
    g_carSurfaces.push_back(g_carSurfaces[0]);
    g_carSurfaces.back().tyreSqueal = 0;
    g_carSurfaceOfMaterial[5] = 1;
    uint32_t seed = 777;
    const auto next = [&] {
        seed = seed * 1103515245u + 12345u;
        return static_cast<float>((seed >> 8) % 10000) / 10000.0f;
    };
    v->m_unknown5EC = 3.0f;
    v->m_unknown5F0 = 0.0f;
    v->m_unknown5E4 = 2.0f, v->m_unknown5E8 = 4.0f;
    int squealChecked = 0, squealRight = 0, soundsChecked = 0, soundsRight = 0;
    for (int n = 0; n < 400; ++n) {
        // Mostly on a squealing ground; now and then still, quiet or slow.
        const bool calm = n % 40 >= 30;
        for (int i = 0; i < v->m_wheelCount; ++i) {
            S_VehicleWheel *w = v->m_wheels[i];
            uint32_t f = w->flags & ~(WHEEL_ON_GROUND | WHEEL_DUMMY | WHEEL_LOCKED | WHEEL_SPINNING);
            f |= next() < 0.9f ? WHEEL_ON_GROUND : 0u;
            f |= next() < 0.05f ? WHEEL_DUMMY : 0u;
            f |= next() < 0.1f ? WHEEL_LOCKED : 0u;
            f |= next() < 0.3f ? WHEEL_SPINNING : 0u;
            w->flags = f;
            w->surface = calm || next() < 0.15f ? 5 : 0;
            w->unknown220 = 0.05f + next() * 0.1f;
            w->unknown224 = w->unknown220 + 0.05f + next() * 0.2f;
            w->unknown1D4 = next() * 0.45f;
            w->unknown0A0 = S_vector(next() * 3.0f, next() * 0.5f, next() * 3.0f);
        }
        const float pick = next();
        v->m_speed = pick < 0.1f ? 0.6f : pick < 0.2f ? 0.3f : 2.0f + next() * 20.0f;
        v->m_gear = 1 + static_cast<int>(next() * static_cast<float>(v->m_gearCount - 1));
        v->m_engineRevs = next() * v->m_revLimit;
        v->m_unknown2298 = next() < 0.5f;
        const float dt = 0.01f + next() * 0.1f;
        const float level = WantSquealLevel(v);
        const float before = v->m_unknown5F0;
        float want = dt * v->m_unknown5EC > level - before ? level : dt * v->m_unknown5EC + before;
        if (want >= 0.99999f)
            want = 1.0f;
        const bool on1 = fake1[0x19]->IsOn(), on2 = fake2[0x1A]->IsOn();
        g_soundLog.clear();
        v->UpdateTyreSqueal(dt);
        ++squealChecked;
        squealRight += v->m_unknown5F0 == want;
        // The sounds: both off under 0.01; else the playing group's at the level (started when
        // not playing), the other off.
        I3D_sound *sound = v->m_unknown2298 ? fake1[0x19] : fake2[0x1A];
        I3D_sound *other = v->m_unknown2298 ? fake2[0x1A] : fake1[0x19];
        Log expect;
        if (want < 0.01f) {
            expect = {{fake1[0x19], 'o'}, {fake2[0x1A], 'o'}};
        } else {
            if (!(sound == fake1[0x19] ? on1 : on2))
                expect.push_back({sound, 'O'});
            expect.push_back({sound, 'V'});
            expect.push_back({other, 'o'});
        }
        ++soundsChecked;
        soundsRight += g_soundLog == expect && (want < 0.01f || g_sounds[sound].volume == want);
    }
    g_carSurfaces.pop_back();
    g_carSurfaceOfMaterial[5] = 0;
    for (int i = 0; i < v->m_wheelCount; ++i) {
        S_VehicleWheel *w = v->m_wheels[i];
        const S_WheelState &s = wheels0[i];
        w->flags = s.flags, w->surface = s.surface;
        w->unknown1D4 = s.s1D4, w->unknown220 = s.s220, w->unknown224 = s.s224, w->unknown0A0 = s.a0A0;
    }

    // --- the handbrake: once per pull, with the details ---
    bool handbrake = true;
    reset();
    v->m_unknownF6C = f6c & ~VEHICLE_SIM_DETAIL_SOUNDS;
    v->m_unknown228E = 0;
    v->PlayHandbrakeSound();
    handbrake &= g_soundLog.empty() && v->m_unknown228E == 0;
    v->m_unknownF6C = f6c | VEHICLE_SIM_DETAIL_SOUNDS;
    for (int group = 0; group < 2; ++group) {
        I3D_sound *sound = group ? fake1[0x16] : fake2[0x17];
        reset();
        v->m_unknown2298 = static_cast<uint8_t>(group);
        v->m_unknown228E = 0;
        v->PlayHandbrakeSound();
        handbrake &= g_soundLog == Log{{sound, 'O'}, {sound, 'V'}} && v->m_unknown228E == 1;
        g_sounds[sound].ended = true;                   // (over: once per pull still)
        g_soundLog.clear();
        v->PlayHandbrakeSound();
        handbrake &= g_soundLog.empty() && v->m_unknown228E == 1;
        reset();
        F_SetOn(sound, true);                           // playing: not again, the pull counted
        g_soundLog.clear();
        v->m_unknown228E = 0;
        v->PlayHandbrakeSound();
        handbrake &= g_soundLog.empty() && v->m_unknown228E == 1;
    }

    // --- the switched loops ---
    bool loops = true;
    for (int b = 0; b < 2; ++b) {
        I3D_sound *s1 = fake1[0x14 + b], *s2 = fake2[0x15 + b];
        const auto run = [&](bool on) {
            g_soundLog.clear();
            if (b)
                v->SwitchLoopSoundB(on);
            else
                v->SwitchLoopSoundA(on);
        };
        reset();
        v->m_unknown2298 = 0;
        run(true);
        loops &= g_soundLog == Log{{s2, 'O'}, {s2, 'V'}, {s1, 'o'}};
        run(true);
        loops &= g_soundLog == Log{{s1, 'o'}};
        v->m_unknown2298 = 1;
        run(true);
        loops &= g_soundLog == Log{{s1, 'O'}, {s1, 'V'}, {s2, 'o'}};
        run(false);
        loops &= g_soundLog == Log{{s1, 'o'}, {s2, 'o'}};
    }

    // --- a tyre's burst ---
    bool burst = true;
    reset();
    v->m_soundsOn = false;
    v->PlayWheelBurst(0);
    burst &= g_soundLog.empty();
    v->m_soundsOn = true;
    v->m_unknown2298 = 1;
    const S_vector before1 = fake1[0x1C]->GetPos();
    v->PlayWheelBurst(0);
    burst &= g_soundLog == Log{{fake1[0x1C], 'O'}, {fake1[0x1C], 'V'}} && fake1[0x1C]->GetPos() == before1;
    for (int i : {0, v->m_wheelCount - 1}) {
        reset();
        v->m_unknown2298 = 0;
        fake2[0x1D]->SetPosInline(S_vector(-9.0f, -9.0f, -9.0f));
        fake2[0x1D]->m_frameFlags &= ~FRMFLAGS_CHANGED;
        v->PlayWheelBurst(i);
        const S_vector4 &at = v->m_wheels[i]->modelPos;
        burst &= g_soundLog == Log{{fake2[0x1D], 'O'}, {fake2[0x1D], 'V'}} &&
                 fake2[0x1D]->GetPos() == S_vector(at.x, at.y, at.z) && (fake2[0x1D]->m_frameFlags & FRMFLAGS_CHANGED);
    }

    std::memcpy(v->m_sounds1, saved1, sizeof saved1);
    std::memcpy(v->m_sounds2, saved2, sizeof saved2);
    v->m_unknown2298 = u2298, v->m_unknown228E = u228E, v->m_soundsOn = soundsOn, v->m_unknownF6C = f6c;
    v->m_velocity = velocity, v->m_modelAxisZ = axisZ;
    for (int k = 0; k < 5; ++k) {
        *rolling1[k] = curves[0][k], *rolling2[k] = curves[1][k];
        v->m_unknownC20[k] = curves[2][k], v->m_unknownF20[k] = curves[3][k];
    }
    v->m_speed = speed0, v->m_engineRevs = revs0, v->m_unknown5E4 = f5E4, v->m_unknown5E8 = f5E8;
    v->m_unknown5EC = f5EC, v->m_unknown5F0 = f5F0, v->m_gear = gear0;
    std::printf("running sounds: rolling %d of %d rim %d of %d untouched %d rim off %d squeal %d of %d squeal "
                "sounds %d of %d\n",
                rollingRight, rollingChecked, rimRight, rimChecked, untouched, rimOff, squealRight, squealChecked,
                soundsRight, soundsChecked);
    std::printf("running one-offs: handbrake %d loops %d burst %d\n", handbrake, loops, burst);
}

// The hit sounds on fakes in every slot: PlayImpactSound's levels (both groups, ordered and
// unordered thresholds, every edge), its flags, its two sounds (either alone, by rand, not over
// itself nor within 800 ms of the other); PlayBumpSound (again unless 0.15 quieter); PlayDoorSound
// (each way, both groups, at the door).
void HitSoundTest(C_Vehicle *v)
{
    I3D_sound *saved1[40], *saved2[41], *fake1[40], *fake2[41];
    std::memcpy(saved1, v->m_sounds1, sizeof saved1);
    std::memcpy(saved2, v->m_sounds2, sizeof saved2);
    for (I3D_sound *&s : fake1)
        s = static_cast<I3D_sound *>(MakeSound());
    for (I3D_sound *&s : fake2)
        s = static_cast<I3D_sound *>(MakeSound());
    std::memcpy(v->m_sounds1, fake1, sizeof fake1);
    std::memcpy(v->m_sounds2, fake2, sizeof fake2);
    const auto reset = [&] {
        for (I3D_sound *s : fake1)
            F_SetOn(s, false), g_sounds[s].ended = false, g_sounds[s].position = 0;
        for (I3D_sound *s : fake2)
            F_SetOn(s, false), g_sounds[s].ended = false, g_sounds[s].position = 0;
        g_soundLog.clear();
    };
    using Log = std::vector<std::pair<const I3D_frame *, char>>;
    const uint8_t u2298 = v->m_unknown2298;
    const bool soundsOn = v->m_soundsOn;
    float thresholds[2][VEHICLE_CRASH_SOUNDS];
    uint8_t flags[2][VEHICLE_CRASH_SOUNDS];
    std::memcpy(thresholds[0], v->m_crash1Thresholds, sizeof thresholds[0]);
    std::memcpy(thresholds[1], v->m_crash2Thresholds, sizeof thresholds[1]);
    std::memcpy(flags[0], v->m_crash1Flags, sizeof flags[0]);
    std::memcpy(flags[1], v->m_crash2Flags, sizeof flags[1]);
    v->m_soundsOn = true;

    // --- the impacts: the level apart (the first threshold the impulse is under, from the second) ---
    int impactChecked = 0, impactRight = 0;
    const float sets[3][2][VEHICLE_CRASH_SOUNDS] = {{{10.0f, 20.0f, 30.0f, 40.0f}, {12.0f, 24.0f, 36.0f, 48.0f}},
                                                    {{10.0f, 35.0f, 20.0f, 45.0f}, {5.0f, 50.0f, 15.0f, 25.0f}},
                                                    {{10.0f, 10.0f, 10.0f, 10.0f}, {8.0f, 8.0f, 30.0f, 30.0f}}};
    unsigned seed = 3;
    for (const auto &set : sets) {
        std::memcpy(v->m_crash1Thresholds, set[0], sizeof set[0]);
        std::memcpy(v->m_crash2Thresholds, set[1], sizeof set[1]);
        for (int group = 0; group < 2; ++group) {
            v->m_unknown2298 = static_cast<uint8_t>(group == 0);
            const float *t = set[group];
            I3D_sound *const *crash = group == 0 ? fake1 + 0x1D : fake2 + 0x1E;
            std::vector<float> impulses = {-1.0f, 0.0f, std::nanf("")};
            for (int k = 0; k < VEHICLE_CRASH_SOUNDS; ++k)
                impulses.push_back(t[k]), impulses.push_back(std::nextafter(t[k], 0.0f)),
                    impulses.push_back(std::nextafter(t[k], 1e9f));
            impulses.push_back(1000.0f);
            for (float impulse : impulses) {
                int level = -1;
                if (impulse > t[0]) {
                    level = 0;
                    while (level < 3 && !(impulse < t[level + 1]))
                        ++level;
                }
                reset();
                const unsigned spread = ++seed * 2654435761u;   // (a small seed's first draw is small)
                std::srand(spread);
                const int draw = std::rand();
                std::srand(spread);
                v->PlayImpactSound(impulse, false);
                Log want;
                if (level >= 0) {
                    I3D_sound *s = crash[level + (draw > 0x3FFE ? VEHICLE_CRASH_SOUNDS : 0)];
                    want = {{s, 'O'}, {s, 'V'}};
                }
                ++impactChecked;
                impactRight += g_soundLog == want;
            }
        }
    }
    // The flags (with `unknown` only), one sound alone, not over itself, the other within 800 ms.
    std::memcpy(v->m_crash1Thresholds, sets[0][0], sizeof sets[0][0]);
    std::memcpy(v->m_crash2Thresholds, sets[0][1], sizeof sets[0][1]);
    bool impactRules = true;
    // The draw's edge: a first rand of 0x3FFE takes the first sound, 0x3FFF the second.
    for (int edge = 0x3FFE; edge <= 0x3FFF; ++edge) {
        unsigned found = 1;
        for (; found < 0x10000000u; ++found) {
            std::srand(found);
            if (std::rand() == edge)
                break;
        }
        reset();
        v->m_unknown2298 = 1;
        std::srand(found);
        v->PlayImpactSound(15.0f, false);
        I3D_sound *s = fake1[edge == 0x3FFE ? 0x1D : 0x21];
        impactRules &= found < 0x10000000u && g_soundLog == Log{{s, 'O'}, {s, 'V'}};
    }
    for (int group = 0; group < 2; ++group) {
        v->m_unknown2298 = static_cast<uint8_t>(group == 0);
        uint8_t *crashFlags = group == 0 ? v->m_crash1Flags : v->m_crash2Flags;
        I3D_sound **crash = (group == 0 ? v->m_sounds1 + 0x1D : v->m_sounds2 + 0x1E);
        const float level2 = (group == 0 ? 30.0f : 36.0f) + 1.0f;
        std::memset(crashFlags, 0, VEHICLE_CRASH_SOUNDS);
        crashFlags[2] = 1;
        std::srand(5);
        const int first = std::rand();
        for (int unknown = 0; unknown < 2; ++unknown) {
            reset();
            std::srand(5);
            v->PlayImpactSound(level2, unknown != 0);
            I3D_sound *s = crash[2 + (first > 0x3FFE ? VEHICLE_CRASH_SOUNDS : 0)];
            impactRules &= unknown ? g_soundLog.empty() && std::rand() == first : g_soundLog == Log{{s, 'O'}, {s, 'V'}};
        }
        reset();
        std::srand(5);
        v->PlayImpactSound(level2 - 10.0f, true);       // (level 1: not flagged)
        impactRules &= g_soundLog.size() == 2;
        crashFlags[2] = 0;
        // Alone (no rand): A, then B.
        for (int alone = 0; alone < 2; ++alone) {
            I3D_sound *&gone = crash[2 + (alone ? 0 : VEHICLE_CRASH_SOUNDS)];
            I3D_sound *kept = crash[2 + (alone ? VEHICLE_CRASH_SOUNDS : 0)], *was = gone;
            gone = nullptr;
            reset();
            std::srand(5);
            v->PlayImpactSound(level2, false);
            impactRules &= g_soundLog == Log{{kept, 'O'}, {kept, 'V'}} && std::rand() == first;
            g_soundLog.clear();
            v->PlayImpactSound(level2, false);          // playing: not again
            impactRules &= g_soundLog.empty();
            gone = was;
        }
        // Both: the drawn one playing (nothing), the other playing for under 800 ms (nothing), at
        // 800 ms (the drawn one), at a position past 2^31 (unsigned: the drawn one).
        I3D_sound *drawn = crash[2 + (first > 0x3FFE ? VEHICLE_CRASH_SOUNDS : 0)];
        I3D_sound *other = crash[2 + (first > 0x3FFE ? 0 : VEHICLE_CRASH_SOUNDS)];
        for (int position : {-1, 799, 800, -5}) {
            reset();
            if (position == -1)
                F_SetOn(drawn, true);
            else
                F_SetOn(other, true), g_sounds[other].position = position;
            g_soundLog.clear();
            std::srand(5);
            v->PlayImpactSound(level2, false);
            const bool starts = position == 800 || position == -5;
            impactRules &= starts ? g_soundLog == Log{{drawn, 'O'}, {drawn, 'V'}} : g_soundLog.empty();
        }
        reset();
        g_sounds[other].ended = true;                   // (over: no wait)
        F_SetOn(other, true);
        g_soundLog.clear();
        std::srand(5);
        v->PlayImpactSound(level2, false);
        impactRules &= g_soundLog == Log{{drawn, 'O'}, {drawn, 'V'}};
    }

    // --- the bumps ---
    bool bump = true;
    for (int group = 0; group < 2; ++group) {
        v->m_unknown2298 = static_cast<uint8_t>(group == 0);
        I3D_sound *s = group == 0 ? fake1[0x1B] : fake2[0x1C];
        reset();
        v->m_soundsOn = false;
        v->PlayBumpSound(0.5f);
        bump &= g_soundLog.empty();
        v->m_soundsOn = true;
        v->PlayBumpSound(0.5f);
        bump &= g_soundLog == Log{{s, 'O'}, {s, 'V'}} && g_sounds[s].volume == 0.5f;
        // Quieter by 0.15 or more: left; else again (at its volume).
        for (float next : {0.3f, 0.4f, 0.2f, 0.26f, 0.9f}) {
            const bool again = next + 0.15f > g_sounds[s].volume + 0.001f;
            g_soundLog.clear();
            v->PlayBumpSound(next);
            bump &= again ? g_soundLog == Log{{s, 'O'}, {s, 'V'}} && g_sounds[s].volume == next : g_soundLog.empty();
        }
        // At the edge (exactly 0.15 quieter in float): left.
        const float playing = g_sounds[s].volume;
        float edge = playing - 0.15f;
        for (int k = 0; k < 16 && edge + 0.15f != playing; ++k)
            edge = std::nextafter(edge, edge + 0.15f < playing ? 1.0f : 0.0f);
        g_soundLog.clear();
        v->PlayBumpSound(edge);
        bump &= edge + 0.15f == playing && g_soundLog.empty();
        g_sounds[s].ended = true;                       // over: any bump
        g_soundLog.clear();
        v->PlayBumpSound(0.1f);
        bump &= g_soundLog == Log{{s, 'O'}, {s, 'V'}};
    }

    // --- the doors: each way, both groups, at the door ---
    bool door = !v->m_seats.empty();
    if (door) {
        S_VehicleDoor made{};
        made.pos = {1.5f, -2.0f, 3.25f, 0.0f};
        S_VehicleDoor *kept = v->m_seats.back().door;
        v->m_seats.back().door = &made;
        const int seat = static_cast<int>(v->m_seats.size()) - 1;
        reset();
        v->m_soundsOn = false;
        v->PlayDoorSound(seat, true);
        door &= g_soundLog.empty();
        v->m_soundsOn = true;
        for (int group = 0; group < 2; ++group)
            for (int opening = 0; opening < 2; ++opening) {
                v->m_unknown2298 = static_cast<uint8_t>(group == 0);
                I3D_sound *s = group == 0 ? fake1[opening ? 0x25 : 0x26] : fake2[opening ? 0x26 : 0x27];
                reset();
                s->SetPosInline(S_vector(-9.0f, -9.0f, -9.0f));
                v->PlayDoorSound(seat, opening != 0);
                door &= g_soundLog == Log{{s, 'O'}, {s, 'V'}} && s->GetPos() == S_vector(1.5f, -2.0f, 3.25f);
            }
        v->m_seats.back().door = kept;
    }

    std::memcpy(v->m_sounds1, saved1, sizeof saved1);
    std::memcpy(v->m_sounds2, saved2, sizeof saved2);
    std::memcpy(v->m_crash1Thresholds, thresholds[0], sizeof thresholds[0]);
    std::memcpy(v->m_crash2Thresholds, thresholds[1], sizeof thresholds[1]);
    std::memcpy(v->m_crash1Flags, flags[0], sizeof flags[0]);
    std::memcpy(v->m_crash2Flags, flags[1], sizeof flags[1]);
    v->m_unknown2298 = u2298, v->m_soundsOn = soundsOn;
    std::printf("hit sounds: impact %d of %d rules %d bump %d door %d\n", impactRight, impactChecked, impactRules, bump,
                door);
}

// --- the car's helpers (part 1e) -----------------------------------------------------------------

// A row vector through a matrix (the SDK's S_vector * S_matrix), in double.
void ThroughMatrix(const double v[3], const S_matrix &m, double out[3])
{
    for (int c = 0; c < 3; ++c)
        out[c] = v[0] * m.m[0][c] + v[1] * m.m[1][c] + v[2] * m.m[2][c] + m.m[3][c];
}

// GetCameraOffset; GetContactResponse against an apart sum (synthetic centre, velocity, turn,
// mass, inertia and a turned body); SetBeamScale per beam bit and all of them over the model's
// lamps; ReadModelState from a turned and moved model (and a still one); SetCallback.
void CarHelperTest(C_Vehicle *v)
{
    // --- the camera's offset ---
    const float lift0 = v->m_unknown67C;
    v->m_unknown67C = 1.75f;
    S_vector offset(9.0f, 9.0f, 9.0f);
    v->GetCameraOffset(&offset);
    const bool camera = offset == S_vector(0.0f, 1.75f, 0.0f);
    v->m_unknown67C = lift0;

    // --- the contact response ---
    const S_vector4 centre0 = v->m_unknown1B0, velocity0 = v->m_velocity, inertia0 = v->m_unknown220;
    const S_vector turn0 = v->m_unknown210;
    const S_matrix m310 = v->m_unknown310, m350 = v->m_unknown350;
    const float mass0 = v->m_currentMass;
    // A body turned 30 degrees about y and 20 about x (row 3 left zero as the original's copies).
    const float a = 0.5235988f, b = 0.34906584f;
    S_matrix ry = IdentityMatrix(), rx = IdentityMatrix();
    ry.m[0][0] = std::cos(a), ry.m[0][2] = -std::sin(a), ry.m[2][0] = std::sin(a), ry.m[2][2] = std::cos(a);
    rx.m[1][1] = std::cos(b), rx.m[1][2] = std::sin(b), rx.m[2][1] = -std::sin(b), rx.m[2][2] = std::cos(b);
    S_matrix body = ry * rx, unbody;
    unbody.Inverse(body);
    v->m_unknown310 = body, v->m_unknown350 = unbody;
    v->m_unknown1B0 = {1.0f, 0.5f, -2.0f, 7.0f};
    v->m_velocity = {3.0f, -1.0f, 8.0f, 0.25f};
    v->m_unknown210 = S_vector(0.4f, -0.3f, 0.2f);
    v->m_unknown220 = {0.002f, 0.0015f, 0.003f, 0.0f};
    v->m_currentMass = 1450.0f;
    int contactChecked = 0, contactRight = 0;
    const S_vector4 points[3] = {{2.0f, 1.0f, 0.0f, 0.0f}, {-1.5f, 0.2f, -4.0f, 1.0f}, {1.0f, 0.5f, -2.0f, 0.0f}};
    const S_vector normals[3] = {S_vector(0.0f, 0.0f, 1.0f), S_vector(0.6f, 0.0f, -0.8f), S_vector(0.0f, 1.0f, 0.0f)};
    for (const S_vector4 &p : points) {
        for (const S_vector &n : normals) {
            S_vector4 arm = {}, vel = {}, turn = {};
            float mass = 0.0f;
            v->GetContactResponse(p, n, &arm, &vel, &mass, &turn);
            const double r[3] = {double(p.x) - 1.0, double(p.y) - 0.5, double(p.z) + 2.0};
            const double w[3] = {0.4, -0.3, 0.2};
            // The velocity less the turn x the arm.
            const double wantVel[3] = {3.0 - (w[1] * r[2] - w[2] * r[1]), -1.0 - (w[2] * r[0] - w[0] * r[2]),
                                       8.0 - (w[0] * r[1] - w[1] * r[0])};
            // arm x normal into the body's space, through the inertia, back.
            const double cross[3] = {r[1] * n.z - r[2] * n.y, r[2] * n.x - r[0] * n.z, r[0] * n.y - r[1] * n.x};
            double local[3], scaled[3], wantTurn[3];
            ThroughMatrix(cross, unbody, local);
            scaled[0] = local[0] * 0.002, scaled[1] = local[1] * 0.0015, scaled[2] = local[2] * 0.003;
            ThroughMatrix(scaled, body, wantTurn);
            bool ok = Near(arm.x, r[0]) && Near(arm.y, r[1]) && Near(arm.z, r[2]) && mass == 1450.0f && vel.w == 0.25f;
            for (int c = 0; c < 3; ++c)
                ok &= Near((&vel.x)[c], wantVel[c]) && std::fabs((&turn.x)[c] - wantTurn[c]) < 1e-7;
            ++contactChecked;
            contactRight += ok;
        }
    }
    v->m_unknown1B0 = centre0, v->m_velocity = velocity0, v->m_unknown210 = turn0, v->m_unknown220 = inertia0;
    v->m_unknown310 = m310, v->m_unknown350 = m350, v->m_currentMass = mass0;

    // --- the beams: each bit's kinds (IN, LI, BI, RE, BR, BA), then all ---
    const uint32_t bits[6] = {VEHICLE_LIGHT_INDICATOR_BEAMS, VEHICLE_LIGHT_FLASH_BEAMS, VEHICLE_LIGHT_REAR_BEAMS,
                              VEHICLE_LIGHT_HEADLIGHT_BEAMS, VEHICLE_LIGHT_BRAKE_BEAMS, VEHICLE_LIGHT_REVERSE_BEAMS};
    const int kindsOf[6][2] = {{1, 2}, {5, 6}, {3, 4}, {7, 7}, {8, 8}, {9, 9}};
    // Synthetic lamps (the models' projectors are on the headlights at most): of each kind 1..9 two
    // with a projector (plain frames: SetScale is inline), one without.
    alignas(16) static uint8_t pool[18][sizeof(I3D_projector_visual)];
    std::memset(pool, 0, sizeof pool);
    std::vector<S_VehicleLamp> lamps0;
    lamps0.swap(v->m_lamps);
    for (int k = 1; k <= 9; ++k)
        for (int n = 0; n < 3; ++n) {
            S_VehicleLamp l{};
            l.kind = static_cast<uint16_t>(k);
            if (n < 2) {
                l.projector = reinterpret_cast<I3D_projector_visual *>(pool[(k - 1) * 2 + n]);
                l.projector->m_frameFlags = FRMFLAGS_ROT_DIRTY;
            }
            v->m_lamps.push_back(l);
        }
    int beamChecked = 0, beamRight = 0;
    for (int k = 0; k <= 6; ++k) {
        for (const S_VehicleLamp &l : v->m_lamps)
            if (l.projector)
                l.projector->SetScale(S_vector(1.0f, 1.0f, 1.0f));
        const uint32_t beams = k < 6 ? bits[k] : 0xFFFFFFFFu;
        v->SetBeamScale(beams, S_vector(12.0f, 13.0f, 40.0f));   // (each axis its own)
        for (const S_VehicleLamp &l : v->m_lamps) {
            if (!l.projector)
                continue;
            const bool chosen = k == 6 ? l.kind >= 1 && l.kind <= 9 : l.kind == kindsOf[k][0] || l.kind == kindsOf[k][1];
            ++beamChecked;
            beamRight += l.projector->GetScale() == (chosen ? S_vector(12.0f, 13.0f, 40.0f) : S_vector(1.0f, 1.0f, 1.0f));
        }
    }
    v->m_lamps.swap(lamps0);

    // --- the model's state read again ---
    I3D_frame *model = v->m_model;
    const S_matrix world0 = model->m_worldMatrix;
    const uint32_t flags0 = model->m_frameFlags;
    struct S_Saved {
        S_matrix modelMatrix, inv, m310, m350;
        S_vector4 pos, centre, ax, ay, az, dir, velocity;
        float speed;
        uint8_t u229F;
        I3D_frame *parent;
    } saved = {v->m_modelMatrix, v->m_invModelMatrix, v->m_unknown310, v->m_unknown350, v->m_modelPos,
               v->m_unknown1B0, v->m_modelAxisX, v->m_modelAxisY, v->m_modelAxisZ, v->m_velocityDir,
               v->m_velocity, v->m_speed, v->m_unknown229F, v->m_unknown544};
    S_matrix placed = body;
    placed.m[3][0] = 120.0f, placed.m[3][1] = -3.5f, placed.m[3][2] = 47.25f;
    model->m_worldMatrix = placed;
    model->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
    v->m_velocity = {3.0f, -4.0f, 12.0f, 0.5f};
    v->m_unknown229F = 1;
    v->m_unknown544 = nullptr;
    const int sectorUpdates0 = g_sectorUpdates;
    v->ReadModelState();
    S_matrix inverse;
    inverse.Inverse(placed);
    bool read = std::memcmp(&v->m_modelMatrix, &placed, sizeof placed) == 0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            read &= std::fabs(v->m_invModelMatrix.m[r][c] - inverse.m[r][c]) < 1e-5f;
            const bool cleared = r == 3 && c < 3;
            read &= v->m_unknown310.m[r][c] == (cleared ? 0.0f : placed.m[r][c]);
            read &= v->m_unknown350.m[r][c] == (cleared ? 0.0f : v->m_invModelMatrix.m[r][c]);
        }
    const S_vector4 *axes[3] = {&v->m_modelAxisX, &v->m_modelAxisY, &v->m_modelAxisZ};
    for (int r = 0; r < 3; ++r)
        read &= std::memcmp(axes[r], placed.m[r], 16) == 0;
    read &= std::memcmp(&v->m_modelPos, placed.m[3], 16) == 0;
    const double com[3] = {v->m_unknown1E0.x, v->m_unknown1E0.y, v->m_unknown1E0.z};
    double wantCentre[3];
    ThroughMatrix(com, placed, wantCentre);
    read &= Near(v->m_unknown1B0.x, wantCentre[0]) && Near(v->m_unknown1B0.y, wantCentre[1]) &&
            Near(v->m_unknown1B0.z, wantCentre[2]);
    read &= Near(v->m_velocityDir.x, 3.0 / 13) && Near(v->m_velocityDir.y, -4.0 / 13) &&
            Near(v->m_velocityDir.z, 12.0 / 13) && v->m_velocityDir.w == 0.5f && Near(v->m_speed, 13.0);
    read &= v->m_unknown229F == 0 && v->m_unknown544 == nullptr && g_sectorUpdates == sectorUpdates0;
    // Still: the direction the SDK's for a zero vector (x, z, then y: +y), no speed.
    v->m_velocity = {0.0f, 0.0f, 0.0f, 0.0f};
    v->ReadModelState();
    const bool still = v->m_velocityDir.x == 0.0f && v->m_velocityDir.y == 1.0f && v->m_velocityDir.z == 0.0f &&
                       v->m_speed == 0.0f;
    model->m_worldMatrix = world0;
    model->m_frameFlags = flags0;
    v->m_modelMatrix = saved.modelMatrix, v->m_invModelMatrix = saved.inv;
    v->m_unknown310 = saved.m310, v->m_unknown350 = saved.m350, v->m_modelPos = saved.pos;
    v->m_unknown1B0 = saved.centre, v->m_modelAxisX = saved.ax, v->m_modelAxisY = saved.ay;
    v->m_modelAxisZ = saved.az, v->m_velocityDir = saved.dir, v->m_velocity = saved.velocity;
    v->m_speed = saved.speed, v->m_unknown229F = saved.u229F, v->m_unknown544 = saved.parent;

    // --- a callback slot ---
    void *const slot0 = v->m_callbacks[VEHICLE_CALLBACK_GLASS_BROKEN];
    v->SetCallback(VEHICLE_CALLBACK_GLASS_BROKEN, reinterpret_cast<void *>(&CarHelperTest));
    const bool callback = v->m_callbacks[VEHICLE_CALLBACK_GLASS_BROKEN] == reinterpret_cast<void *>(&CarHelperTest);
    v->m_callbacks[VEHICLE_CALLBACK_GLASS_BROKEN] = slot0;

    std::printf("car helpers: camera %d contact %d of %d beams %d of %d read %d still %d callback %d\n", camera,
                contactRight, contactChecked, beamRight, beamChecked, read, still, callback);
}

// --- Restart ---------------------------------------------------------------------------------------

S_vector4 MatrixRow(const S_matrix &m, int row)
{
    return {m.m[row][0], m.m[row][1], m.m[row][2], m.m[row][3]};
}

bool SameRow(const S_vector4 &a, const S_vector4 &b)
{
    return std::memcmp(&a, &b, sizeof a) == 0;
}

bool SameXYZ(const S_vector4 &a, const S_vector &b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool SameQuat(const S_quat &a, const S_quat &b)
{
    return a.s == b.s && a.v.x == b.v.x && a.v.y == b.v.y && a.v.z == b.v.z;
}

constexpr uint32_t RESTART_CLEARED = WHEEL_ON_GROUND | WHEEL_SLIDING | WHEEL_LOCKED | WHEEL_BUMPED | WHEEL_SPINNING |
                                     WHEEL_HIT;
constexpr uint32_t RESTART_REPAIRED = WHEEL_ON_RIM | WHEEL_DETACHED;

// Restart as after the script's teleport (Reset, the model moved, Restart): the state the drive
// left scrambled (the motion, the controls and the clutch bits, each wheel's contact state, flags,
// spin, frame, holder's turn and place, the condition and the parts' health), the car moved 10 m
// along x and turned a quarter round on its heading, under a parent frame. Checked (mismatch
// counts): the model's matrices, axes, place and sector (place), `speed` along the new z axis and
// the engine with it (motion), the controls in gear 1 (controls), each wheel's state (wheels) and
// its contact under it at its reach (contacts), the steered holders straight (their rest turn) and
// the others' turn kept, every holder in place (holders), repaired or kept (repair), the wheel
// frames' spin as UpdateWheelSpin sets it (spin), the lamps' and projectors' objects in the model's
// parent and a shared lamp's not (lamps; with two test lamps), the volumes in the world. With a
// positive speed the car then coasts half a
// second: along its new heading at about that speed.
void RestartTest(C_Vehicle *v, float speed, bool repair)
{
    v->Reset(0);
    v->m_unknown2293 = v->m_unknown2294 = v->m_unknown2295 = v->m_unknown22A0 = v->m_unknown228E = 1;
    v->m_unknown22C0 = 0;
    v->m_unknown22C1 = 1;
    v->m_unknown210 = S_vector(0.1f, 0.2f, 0.3f);
    v->m_velocity.x = 1.0f, v->m_velocity.y = 2.0f, v->m_velocity.z = 3.0f;
    v->m_velocityDir.x = v->m_velocityDir.y = v->m_velocityDir.z = 0.5f;
    v->m_unknown600 = 0x2E;
    v->m_unknown5F0 = v->m_unknown710 = v->m_engineRevs = v->m_previousRevs = v->m_speed = 0.5f;
    v->m_gear = 0;
    v->m_requestedGear = -1;
    v->m_unknown78C = v->m_handbrake = v->m_unknown724 = v->m_unknown94C = v->m_brake = 0.5f;
    v->m_unknown85C = v->m_unknown860 = v->m_steering = v->m_unknown780 = v->m_unknown958 = 0.5f;
    v->m_unknown904 = v->m_unknown908 = v->m_unknown90C = 0.5f;
    v->m_unknown910 = 0x3F;
    v->m_unknownF68 = v->m_unknown700 = v->m_throttle = 0.5f;
    v->m_frontGroundVolume = v->m_rearGroundVolume = &g_wallVolume;
    v->m_unknown4F8[0] = v->m_unknown4F8[1] = 1;
    v->m_sinceRimScrape = 5;
    v->m_unknown610 = v->m_unknown614 - 1.0f;
    v->m_unknown63C = v->m_unknown640 - 1.0f;
    for (S_VehiclePart &p : v->m_parts)
        p.health = p.fullHealth + 1.0f;
    std::vector<S_quat> holderRots(v->m_wheelCount);
    for (int i = 0; i < v->m_wheelCount; ++i) {
        S_VehicleWheel *w = v->m_wheels[i];
        if (!w)
            continue;
        w->flags |= RESTART_CLEARED | RESTART_REPAIRED;
        w->skidMark = w->skidMarks.size() > 1 ? 1 : 0;
        w->extension = 0.3f;
        w->unknown0A0 = w->unknown0B0 = S_vector(1.0f, 2.0f, 3.0f);
        w->unknown1F0 = 1.0f;
        w->surface = 3;
        w->unknown25C[0] = w->unknown25C[1] = w->unknown25C[2] = 1.0f;
        w->unknown26C = 5.0f;
        w->ground = w->groundPlaneVolume = &g_wallVolume;
        w->unknown168 = 100.0f;
        w->groundPlane = {S_vector(1.0f, 0.0f, 0.0f), 2.0f};
        w->unknown050.x = w->unknown050.y = w->unknown050.z = 9.0f;
        w->unknown060 = w->unknown050;
        w->frame->SetOn(false);
        S_quat turned;
        turned.Make(S_vector(0.0f, 1.0f, 0.0f), 0.3f);
        w->holder->SetRot(turned);
        w->holder->GetLocalMatrix();
        w->holder->SetPosInline(S_vector(w->holderPos.x, w->holderPos.y + 0.1f, w->holderPos.z));
        holderRots[i] = w->holder->m_rot;
    }

    // A lamp with its own objects (no shipped model has one: a lamp with a "[<texture>]" property)
    // and a shared lamp with objects: Restart links the first's, never the second's (taken out
    // again before the coasting).
    const size_t lampCount = v->m_lamps.size();
    for (const bool sharing : {false, true}) {
        S_VehicleLamp lamp = {};
        lamp.projector = reinterpret_cast<I3D_projector_visual *>(NewFrame(FRAME_VISUAL, "lamp projector", nullptr));
        lamp.light = reinterpret_cast<I3D_light *>(NewFrame(FRAME_LIGHT, "lamp light", nullptr));
        lamp.flags = sharing ? static_cast<uint32_t>(VEHICLE_LAMP_SHARED) : 0u;
        lamp.part = 0xFFFF;
        v->m_lamps.push_back(lamp);
    }

    // The teleport.
    I3D_frame *place = NewFrame(FRAME_DUMMY, "place", nullptr);
    g_model->LinkTo(place, 0);
    const float heading = std::atan2(v->m_modelAxisZ.x, v->m_modelAxisZ.z);
    const S_vector pos = g_model->m_localMatrix(3) + S_vector(10.0f, 0.0f, 0.0f);
    S_quat turn;
    turn.Make(S_vector(0.0f, 1.0f, 0.0f), heading + 1.5707964f);
    g_model->SetRot(turn);
    g_model->GetLocalMatrix();
    g_model->SetPosInline(pos);
    g_sectorUpdates = g_volumeCalls = 0;
    v->Restart(speed, repair);

    // The model's place.
    const S_matrix world = g_model->GetMatrix();
    S_matrix inverse = world;
    inverse.Inverse(world);
    S_matrix rotation = world, invRotation = inverse;
    rotation.m[3][0] = rotation.m[3][1] = rotation.m[3][2] = 0.0f;
    invRotation.m[3][0] = invRotation.m[3][1] = invRotation.m[3][2] = 0.0f;
    const S_vector com = S_vector(v->m_unknown1E0.x, v->m_unknown1E0.y, v->m_unknown1E0.z) * world;
    const int placeOff = (std::memcmp(&v->m_modelMatrix, &world, sizeof world) != 0) +
                         (std::memcmp(&v->m_invModelMatrix, &inverse, sizeof inverse) != 0) +
                         (std::memcmp(&v->m_unknown310, &rotation, sizeof rotation) != 0) +
                         (std::memcmp(&v->m_unknown350, &invRotation, sizeof invRotation) != 0) +
                         !SameRow(v->m_modelAxisX, MatrixRow(world, 0)) + !SameRow(v->m_modelAxisY, MatrixRow(world, 1)) +
                         !SameRow(v->m_modelAxisZ, MatrixRow(world, 2)) + !SameRow(v->m_modelPos, MatrixRow(world, 3)) +
                         !SameXYZ(v->m_unknown1B0, com) + (v->m_unknown544 != place) + (g_sectorUpdates != 1);

    // The motion and the controls.
    const S_vector velocity(speed * v->m_modelAxisZ.x, speed * v->m_modelAxisZ.y, speed * v->m_modelAxisZ.z);
    const S_vector dir = speed != 0.0f ? velocity.Normalized() : S_vector(0.0f, 0.0f, 0.0f);
    const int motionOff = !SameXYZ(v->m_velocity, velocity) + !SameXYZ(v->m_velocityDir, dir) +
                          (v->m_speed != std::fabs(speed)) + (v->m_engineOn != (speed != 0.0f)) +
                          (v->m_unknown210 != S_vector(0.0f, 0.0f, 0.0f)) + (v->m_engineRevs != 0.0f) +
                          (v->m_previousRevs != 0.0f) + (v->m_unknown600 != VEHICLE_PHYSICS_AWAKE) +
                          (v->m_unknown5F0 != 0.0f) + (v->m_unknown710 != 0.0f) + (v->m_unknown2293 != 0) +
                          (v->m_unknown22C0 != 1) + (v->m_unknown22C1 != 0);
    int controlsOff = (v->m_gear != 1) + (v->m_requestedGear != 1) + (v->m_unknown904 != 1.0f) +
                      (v->m_unknown908 != 1.0f) + (v->m_unknown90C != 1.0f) + (v->m_unknown910 != 0x20) +
                      (v->m_unknown22A0 != 0) + (v->m_unknown2294 != 0) + (v->m_unknown2295 != 0) +
                      (v->m_unknown228E != 0) + (v->m_frontGroundVolume != nullptr) + (v->m_rearGroundVolume != nullptr) +
                      (v->m_unknown4F8[0] != 0) + (v->m_unknown4F8[1] != 0);
    for (const float control : {v->m_unknown78C, v->m_handbrake, v->m_unknown724, v->m_unknown94C, v->m_brake,
                                v->m_unknown85C, v->m_unknown860, v->m_steering, v->m_unknown780, v->m_unknown958,
                                v->m_unknownF68, v->m_unknown700, v->m_throttle})
        controlsOff += control != 0.0f;

    // The wheels.
    int wheelsOff = 0, contactsOff = 0, holdersOff = 0, repairOff = 0, spinChecked = 0, spinOff = 0;
    std::vector<S_quat> spins(v->m_wheelCount);
    for (int i = 0; i < v->m_wheelCount; ++i) {
        S_VehicleWheel *w = v->m_wheels[i];
        if (!w)
            continue;
        const S_vector4 &up = v->m_modelAxisY;
        const S_vector down(w->unknown1E4 * up.x, up.y * w->unknown1E4, up.z * w->unknown1E4);
        const S_vector contact = S_vector(w->worldPos.x, w->worldPos.y, w->worldPos.z) - down;
        wheelsOff += !SameRow(w->worldPos, MatrixRow(w->frame->GetMatrix(), 3)) || (w->flags & RESTART_CLEARED) ||
                     w->skidMark != 0 || w->extension != 0.0f || w->unknown0A0 != S_vector(0.0f, 0.0f, 0.0f) ||
                     w->unknown0B0 != S_vector(0.0f, 0.0f, 0.0f) || w->unknown1F0 != 0.0f || w->surface != 0 ||
                     w->unknown25C[0] != 0.0f || w->unknown25C[1] != 0.0f || w->unknown25C[2] != 0.0f ||
                     w->unknown26C != 1000.0f || w->ground || w->unknown168 != 0.0f ||
                     w->groundPlane.normal != S_vector(0.0f, 1.0f, 0.0f) || w->groundPlane.d != 0.0f ||
                     w->groundPlaneVolume;
        contactsOff += !SameXYZ(w->unknown050, contact) ||
                       !SameXYZ(w->unknown060, contact - S_vector(up.x, up.y, up.z));
        S_quat expected = holderRots[i];
        if ((w->flags & WHEEL_STEERED) && !(w->flags & WHEEL_DUMMY) && w->holder->m_parent == v->m_model) {
            expected = w->unknown000;
            expected.Normalize();
        }
        holdersOff += !SameQuat(w->holder->m_rot, expected) ||
                      w->holder->m_localMatrix(3) != S_vector(w->holderPos.x, w->holderPos.y, w->holderPos.z);
        repairOff += repair ? (w->flags & RESTART_REPAIRED) || !w->frame->IsOn()
                            : (w->flags & RESTART_REPAIRED) != RESTART_REPAIRED || w->frame->IsOn();
        spins[i] = w->frame->m_rot;
    }
    repairOff += repair ? v->m_sinceRimScrape != 30000 || v->m_unknown610 != v->m_unknown614 ||
                              v->m_unknown63C != v->m_unknown640
                        : v->m_sinceRimScrape != 5 || v->m_unknown610 == v->m_unknown614 ||
                              v->m_unknown63C == v->m_unknown640;
    for (const S_VehiclePart &p : v->m_parts)
        repairOff += repair ? p.health != p.fullHealth : p.health == p.fullHealth;
    // The spin: the frames turned elsewhere, UpdateWheelSpin(1) gives them back (the turning ones).
    for (int i = 0; i < v->m_wheelCount; ++i) {
        S_VehicleWheel *w = v->m_wheels[i];
        if (!w)
            continue;
        S_quat other;
        other.Make(S_vector(1.0f, 0.0f, 0.0f), 0.7f);
        w->frame->SetRot(other);
    }
    v->UpdateWheelSpin(1.0f);
    for (int i = 0; i < v->m_wheelCount; ++i) {
        const S_VehicleWheel *w = v->m_wheels[i];
        if (!w || (w->flags & (WHEEL_DUMMY | WHEEL_DETACHED)))
            continue;
        ++spinChecked;
        spinOff += !SameQuat(w->frame->m_rot, spins[i]);
    }

    // The lamps' own and the projectors' objects in the model's parent, the shared test lamp's left
    // out (a shipped shared lamp's objects are its projector's).
    int lampObjects = 0, lampsIn = 0;
    for (size_t i = 0; i < v->m_lamps.size(); ++i) {
        const S_VehicleLamp &l = v->m_lamps[i];
        const bool shared = (l.flags & VEHICLE_LAMP_SHARED) != 0;
        if (!l.projector || (shared && i < lampCount))
            continue;
        for (const I3D_frame *f : {static_cast<const I3D_frame *>(l.projector), static_cast<const I3D_frame *>(l.light)})
            if (f) {
                ++lampObjects;
                lampsIn += (f->m_parent == place) != shared;
            }
    }
    v->m_lamps.resize(lampCount);
    for (const S_VehicleProjector &p : v->m_projectors)
        for (const I3D_frame *f : {static_cast<const I3D_frame *>(p.projector), static_cast<const I3D_frame *>(p.light)})
            if (f) {
                ++lampObjects;
                lampsIn += f->m_parent == place;
            }
    const bool volumes = v->m_mainVolumesInWorld && v->m_volume490InWorld && g_volumeCalls > 0;

    // Coasting half a second.
    float along = 0.0f, side = 0.0f;
    if (speed > 0.0f) {
        const S_vector start(v->m_modelPos.x, v->m_modelPos.y, v->m_modelPos.z);
        const S_vector z(v->m_modelAxisZ.x, v->m_modelAxisZ.y, v->m_modelAxisZ.z);
        const S_vector x(v->m_modelAxisX.x, v->m_modelAxisX.y, v->m_modelAxisX.z);
        for (int step = 0; step < 6; ++step) {
            v->SetThrottle(0.0f, true);
            v->SetBrake(0.0f);
            v->Step(83);
            v->Update(83);
            UpdateCarEffectEmitters(83);
        }
        const S_vector moved = S_vector(v->m_modelPos.x, v->m_modelPos.y, v->m_modelPos.z) - start;
        along = moved.Dot(z) / (speed * 0.498f);
        side = std::fabs(moved.Dot(x)) / (speed * 0.498f);
    }
    std::printf("restart speed %.1f repair %d: place %d motion %d controls %d wheels %d contacts %d holders %d "
                "repair %d spin %d of %d lamps %d of %d volumes %d coast %.3f side %.4f\n",
                speed, repair, placeOff, motionOff, controlsOff, wheelsOff, contactsOff, holdersOff, repairOff, spinOff,
                spinChecked, lampsIn, lampObjects, volumes, along, side);
}

void Report(const char *path)
{
    if (!LoadModel(path)) {
        std::printf("%s: cannot load\n", path);
        return;
    }
    g_duplicatedMeshes = 0;
    auto *v = new C_Vehicle();
    const bool ok = v->Setup(g_model);
    // As the car's SetFrame: the emitter callbacks; the exhausts' effect as most shipped cars have
    // it (12; Setup's default has none).
    v->m_callbacks[VEHICLE_CALLBACK_SETUP_EMITTER] = reinterpret_cast<void *>(&SetupCarEmitter);
    v->m_callbacks[VEHICLE_CALLBACK_REUSE_EMITTER] = reinterpret_cast<void *>(&ReuseCarEmitter);
    v->m_callbacks[VEHICLE_CALLBACK_PART_OFF] = reinterpret_cast<void *>(&F_PartOff);
    v->m_callbacks[VEHICLE_CALLBACK_WHEEL_OFF] = reinterpret_cast<void *>(&F_WheelOff);
    v->m_callbacks[VEHICLE_CALLBACK_GLASS_BROKEN] = reinterpret_cast<void *>(&F_GlassBroken);
    v->m_callbacks[VEHICLE_CALLBACK_PART_HIT] = reinterpret_cast<void *>(&F_PartHit);
    v->m_unknown754 = 12;
    if (v->m_wheelCount > 0 && v->m_wheels[0]) {
        // The ground's tyre entry: the first wheel's own values (as the solver mirrors them).
        const S_VehicleWheel &w = *v->m_wheels[0];
        g_tyreSurfaces[0] = {1, w.unknown18C, w.unknown194, w.unknown198, w.unknown19C, w.unknown190, w.unknown1B8,
                             w.unknown1BC, w.unknown1C0, w.unknown1C4, w.unknown1C8, w.unknown1CC, w.unknown1D0};
    }
    std::printf("== %s setup=%d\n", g_modelFile.c_str(), ok);
    std::printf("body=%s exhausts=%zu specials=%zu/%zu/%zu head=(%.3f %.3f %.3f)\n", Short(v->m_body).c_str(),
                v->m_exhausts.size(), v->m_specialCollisions.size(), v->m_specialCollisionCorners.size(),
                v->m_specialCollisionDescs.size(), v->m_headPos.x, v->m_headPos.y, v->m_headPos.z);
    int boxes = 0, points = 0, flagB = 0, roots = 0, depth = 0, onParts = 0;
    std::vector<std::pair<S_BodyCollisionBox *, int>> todo = {{v->m_bodyCollision, 1}};
    while (!todo.empty()) {
        S_BodyCollisionBox *b = todo.back().first;
        const int level = todo.back().second;
        todo.pop_back();
        for (; b; b = b->next) {
            ++boxes;
            roots += level == 1;
            depth = std::max(depth, level);
            flagB += b->flagB;
            onParts += b->parent != nullptr;
            for (S_BodyCollisionPoint *p = b->points; p; p = p->next)
                ++points;
            todo.push_back({b->children, level + 1});
        }
    }
    std::printf("bodycol boxes=%d (B %d) points=%d roots=%d depth=%d on parts=%d\n", boxes, flagB, points, roots,
                depth, onParts);
    std::printf("doors=%zu:", v->m_doors.size());
    for (const S_VehicleDoor &d : v->m_doors)
        std::printf(" %s%s%s", Short(d.frame).c_str(), d.window ? "+w" : "", d.flags ? "!" : "");
    std::printf("\nseats=%zu:", v->m_seats.size());
    for (const S_VehicleSeat &s : v->m_seats)
        std::printf(" %s(%s)", Short(s.frame).c_str(), s.door ? Short(s.door->frame).c_str() : "-");
    std::printf("\nballasts=%zu wheels=%d axles=%d:", v->m_ballasts.size(), v->m_wheelCount, v->m_axleCount);
    for (int i = 0; i < v->m_wheelCount && v->m_wheels; ++i) {
        const S_VehicleWheel *w = v->m_wheels[i];
        if (!w) {
            std::printf(" null");
            continue;
        }
        std::printf(" %s(r%.3f w%.3f%s%s)", w->name.c_str() + 4, w->radius, w->width,
                    w->flags & WHEEL_DUMMY ? " dummy" : "", w->flags & WHEEL_TRACKED ? " tracked" : "");
    }
    std::printf("\nstruts axle=%zu side=%zu:", v->m_axleStruts.size(), v->m_sideStruts.size());
    for (const S_VehicleStrut &s : v->m_axleStruts)
        std::printf(" %s[%d]%s", Short(s.frame).c_str(), s.axle, s.single ? "*" : "");
    for (const S_VehicleStrut &s : v->m_sideStruts)
        std::printf(" %s[%d%c]%s", Short(s.frame).c_str(), s.axle, s.left ? 'L' : 'R', s.single ? "*" : "");
    // UpdateStruts (run by Setup): each strut's place, length (z scale) and direction.
    for (const auto *list : {&v->m_axleStruts, &v->m_sideStruts})
        for (const S_VehicleStrut &s : *list) {
            const S_vector &p = s.frame->GetPos();
            const S_vector &k = s.frame->GetScale();
            const auto it = g_dirs.find(s.frame);
            const S_vector d = it != g_dirs.end() ? it->second : S_vector(0.0f, 0.0f, 0.0f);
            std::printf("\nstrut %s pos %.4f %.4f %.4f scale %.4f %.4f %.4f dir %.4f %.4f %.4f on %d",
                        Short(s.frame).c_str(), p.x, p.y, p.z, k.x, k.y, k.z, d.x, d.y, d.z, s.frame->IsOn());
        }
    size_t trackVerts = 0;
    for (const S_VehicleTrack &t : v->m_tracksL)
        trackVerts += t.vertices.size();
    for (const S_VehicleTrack &t : v->m_tracksR)
        trackVerts += t.vertices.size();
    std::printf("\ntracks L=%zu R=%zu vertices=%zu meshes duplicated=%d\n", v->m_tracksL.size(),
                v->m_tracksR.size(), trackVerts, g_duplicatedMeshes);
    std::printf("centre box (%.3f %.3f %.3f)..(%.3f %.3f %.3f) size (%.3f %.3f %.3f) swheel=%s smoke=(%.3f %.3f %.3f)\n",
                v->m_bodyMin.x, v->m_bodyMin.y, v->m_bodyMin.z, v->m_bodyMax.x, v->m_bodyMax.y, v->m_bodyMax.z,
                v->m_size.x, v->m_size.y, v->m_size.z, Short(v->m_steeringWheel).c_str(), v->m_smokePos.x,
                v->m_smokePos.y, v->m_smokePos.z);
    std::printf("top speed %.2f m/s, gears %d\n", v->m_topSpeed, v->m_gearCount);
    int byType[11] = {};
    int grouped = 0, glass = 0;
    for (const S_VehiclePart &p : v->m_parts) {
        ++byType[p.type <= 10 ? p.type : 0];
        grouped += p.frames != nullptr;
        if (p.type == VEHICLE_PART_GLASS) {
            const auto *mesh = reinterpret_cast<const S_Mesh *>(p.mesh);
            glass += mesh && mesh->levels[0]->faces.material != &g_modelMaterial;
        }
    }
    std::printf("parts=%zu:", v->m_parts.size());
    for (int t = 1; t <= 10; ++t)
        std::printf(" %d", byType[t]);
    std::printf(" grouped=%d glass-own-material=%d meshes made=%d\n", grouped, glass, g_meshes);
    int kinds[10] = {};
    int shared = 0, linked = 0;
    for (const S_VehicleLamp &l : v->m_lamps) {
        ++kinds[l.kind <= 9 ? l.kind : 0];
        shared += (l.flags & VEHICLE_LAMP_SHARED) != 0;
        linked += l.part != 0xFFFF;
    }
    std::printf("lamps=%zu:", v->m_lamps.size());
    for (int k = 1; k <= 9; ++k)
        std::printf(" %d", kinds[k]);
    std::printf(" shared=%d with-part=%d projectors=%zu\n", shared, linked, v->m_projectors.size());
    int removed = 0;
    for (S_StubFrame *s : g_frames)
        removed += s->removed;
    std::printf("removed frames=%d model refs=%d\n", removed, g_model->m_refCount);
    // Reset with both skid-mark flags (no skid marks before Start): the lamps' own and the
    // projectors' objects are unlinked, no effect slot is set, the volumes are not in the world.
    g_unlinked = g_effectsStopped = g_engineSoundStops = g_volumeCalls = 0;
    int ownLamps = 0, projectorLights = 0;
    for (const S_VehicleLamp &l : v->m_lamps)
        if (l.projector && !(l.flags & VEHICLE_LAMP_SHARED))
            ownLamps += 1 + (l.light != nullptr);
    for (const S_VehicleProjector &p : v->m_projectors)
        projectorLights += 1 + (p.light != nullptr);
    // (No sound loaded before Start: one in the last slot for StopSounds to stop.)
    I3D_sound *resetProbe = static_cast<I3D_sound *>(MakeSound());
    v->m_sounds2[40] = resetProbe;
    g_soundLog.clear();
    v->Reset(VEHICLE_RESET_UNLINK_SKID_MARKS | VEHICLE_RESET_HIDE_SKID_MARKS);
    for (const auto &call : g_soundLog)
        g_engineSoundStops += call.first == resetProbe && call.second == 'S' ? 1 : 100;
    v->m_sounds2[40] = nullptr;
    for (const S_CarEmitter &e : g_carEmitters)
        g_effectsStopped += e.stopping;
    std::printf("reset: unlinked %d (expected %d) effects stopped %d engine sounds stopped %d volume calls %d\n",
                g_unlinked, ownLamps + projectorLights, g_effectsStopped, g_engineSoundStops, g_volumeCalls);

    // Start: the volumes, the turning circle and its inputs, the axle middles, the skid marks,
    // the glass textures, the projectors' textures, the calls into later parts.
    g_volumeCalls = g_sectorUpdates = 0;
    g_cacheLoads.clear();
    g_skidFrames.clear();
    int projectorTextures = 0;
    for (const S_Made &m : g_made)
        projectorTextures -= m.projector ? m.textures : 0;
    v->Start();
    for (const S_Made &m : g_made)
        projectorTextures += m.projector ? m.textures : 0;
    const S_CollisionVolume &bv = v->m_bodyVolume;
    const S_CollisionVolume &sv = v->m_volume490;
    std::printf("start: volume calls %d specials %zu/%zu sector updates %d sounds loaded %d projector textures %d\n",
                g_volumeCalls, v->m_specialVolumes.size(), v->m_specialVolumeList.size(), g_sectorUpdates,
                LoadedSounds(v), projectorTextures);
    SoundTest(v);
    EngineSoundTest(v);
    RunningSoundTest(v);
    HitSoundTest(v);
    CarHelperTest(v);
    PlantCountingSounds(v);
    std::printf("start body volume r %.5f (%.5f %.5f %.5f)..(%.5f %.5f %.5f) type %X\n", bv.radius, bv.bbox.min.x,
                bv.bbox.min.y, bv.bbox.min.z, bv.bbox.max.x, bv.bbox.max.y, bv.bbox.max.z, bv.type);
    std::printf("start second volume r %.5f (%.5f %.5f %.5f)..(%.5f %.5f %.5f) type %X\n", sv.radius,
                sv.bbox.min.x, sv.bbox.min.y, sv.bbox.min.z, sv.bbox.max.x, sv.bbox.max.y, sv.bbox.max.z, sv.type);
    for (const S_CollisionVolume &vol : v->m_specialVolumes)
        std::printf("start special volume r %.5f (%.5f %.5f %.5f)..(%.5f %.5f %.5f)\n", vol.radius, vol.bbox.min.x,
                    vol.bbox.min.y, vol.bbox.min.z, vol.bbox.max.x, vol.bbox.max.y, vol.bbox.max.z);
    for (size_t i = 0; i < v->m_specialCollisionCorners.size(); ++i) {
        const S_vector4 &c = v->m_specialCollisionCorners[i];
        std::printf("start special corner %.5f %.5f %.5f\n", c.x, c.y, c.z);
    }
    std::printf("start body box (%.5f %.5f %.5f)..(%.5f %.5f %.5f)\n", v->m_bodyMin.x, v->m_bodyMin.y, v->m_bodyMin.z,
                v->m_bodyMax.x, v->m_bodyMax.y, v->m_bodyMax.z);
    std::printf("start turning %.5f %.5f steer %.5f f74 %X\n", v->m_unknown6E8, v->m_unknown6EC, v->m_unknown92C,
                v->m_unknownF74);
    for (int i = 0; i < v->m_wheelCount; ++i) {
        const S_VehicleWheel *w = v->m_wheels[i];
        if (w)
            std::printf("start wheel %d model %.5f %.5f %.5f flags %X skids %zu\n", i, w->modelPos.x, w->modelPos.y,
                        w->modelPos.z, w->flags, w->skidMarks.size());
    }
    std::printf("start axles (%.5f %.5f %.5f) (%.5f %.5f %.5f)\n", v->m_frontAxleCentre.x, v->m_frontAxleCentre.y,
                v->m_frontAxleCentre.z, v->m_rearAxleCentre.x, v->m_rearAxleCentre.y, v->m_rearAxleCentre.z);
    int skidOff = 0, skidInScene = 0;
    for (const I3D_frame *f : g_skidFrames) {
        skidOff += !f->IsOn();
        skidInScene += f->m_parent == g_sector && f->m_refCount == 1;
    }
    int shlaif1 = 0, shlaif2 = 0;
    for (const std::string &name : g_cacheLoads) {
        shlaif1 += name == "shlaif01.i3d";
        shlaif2 += name == "shlaif02.i3d";
    }
    int glassParts = 0, glassTextures = 0;
    for (const S_VehiclePart &p : v->m_parts) {
        glassParts += p.type == VEHICLE_PART_GLASS;
        glassTextures += p.type == VEHICLE_PART_GLASS && p.glassTexture;
    }
    std::printf("start skid frames %zu off %d in scene %d loads %d/%d per wheel %d glass %d/%d engine %d gear %d/%d\n",
                g_skidFrames.size(), skidOff, skidInScene, shlaif1, shlaif2, v->m_unknown678, glassTextures,
                glassParts, v->m_engineOn, v->m_gear, v->m_requestedGear);
    std::printf("start com (%.5f %.5f %.5f) com1E0 (%.5f %.5f %.5f) inv (%.6g %.6g %.6g) inv220 (%.6g %.6g %.6g) mass %.3f\n",
                v->m_centreOfMass.x, v->m_centreOfMass.y, v->m_centreOfMass.z, v->m_unknown1E0.x, v->m_unknown1E0.y,
                v->m_unknown1E0.z, v->m_inverseInertia.x, v->m_inverseInertia.y, v->m_inverseInertia.z,
                v->m_unknown220.x, v->m_unknown220.y, v->m_unknown220.z, v->m_mass);
    LampTest(v);
    DoorTest(v);
    ToeTest(v);
    EffectsTest(v);
    SkidTest(v);
    DeformTest(v);

    // A driver of 80 kg on the first seat.
    v->SetSeatLoad(0, 80.0f);
    if (!v->m_seats.empty())
        std::printf("load seat (%.5f %.5f %.5f) com1E0 (%.5f %.5f %.5f) inv220 (%.6g %.6g %.6g)\n", v->m_seats[0].pos.x,
                    v->m_seats[0].pos.y, v->m_seats[0].pos.z, v->m_unknown1E0.x, v->m_unknown1E0.y, v->m_unknown1E0.z,
                    v->m_unknown220.x, v->m_unknown220.y, v->m_unknown220.z);

    // The drive test: 40 s at full throttle, then 25 s on the brake (at a standstill the automatic
    // box goes into reverse and the brake drives), in Step's 83 ms steps through the real solver
    // over the toy flat ground, laid where the car's static load would hold it (on equal
    // springs; the car first settles 3 s, engine off, its motion stopped after each step). +0x2291
    // (one of the automatic box's conditions; set elsewhere, assumed) on.
    if (const char *options = std::getenv("DRIVE_F6C"))  // the simulation options (hex), e.g. the ground modes
        v->m_unknownF6C = static_cast<uint32_t>(std::strtoul(options, nullptr, 16));
    if (std::getenv("DRIVE_SYMMETRIC")) {
        // A check of the port's symmetry: the centre of mass midway between the wheels across.
        float sum = 0.0f;
        int count = 0;
        for (int i = 0; i < v->m_wheelCount; ++i)
            if (v->m_wheels[i] && !(v->m_wheels[i]->flags & WHEEL_DUMMY)) {
                sum += v->m_wheels[i]->modelPos.x;
                ++count;
            }
        v->m_centreOfMass.x = count ? sum / count : v->m_centreOfMass.x;
    }
    v->SetSeatLoad(0, 0.0f);
    v->SetFuel(v->m_tankSize);
    v->m_driven = 1;
    // Setup's default parameters toe every wheel in by 1 degree and lay the surface's bumps (1 to 2
    // cm) under each wheel on its own (C_Vehicle +0x528). Both turn the body off its line (the
    // toe's forces through wheels staggered along the sides, the bumps at random), and a body
    // turned round on the brake reverses: the straight drive and the crash go without them
    // (ToeTest and BumpTest check them).
    for (int i = 0; i < v->m_wheelCount; ++i)
        if (v->m_wheels[i])
            v->m_wheels[i]->unknown178 = 0.0f;
    const float bumps = v->m_unknown528;
    v->m_unknown528 = 0.0f;
    int grounded = 0;
    for (int i = 0; i < v->m_wheelCount; ++i)
        grounded += v->m_wheels[i] && !(v->m_wheels[i]->flags & WHEEL_DUMMY);
    g_groundY = 1e9f;
    for (int i = 0; i < v->m_wheelCount; ++i) {
        const S_VehicleWheel *w = v->m_wheels[i];
        if (!w || (w->flags & WHEEL_DUMMY))
            continue;
        const float hubY = (S_vector(w->modelPos.x, w->modelPos.y, w->modelPos.z) * v->m_modelMatrix).y;
        const float rest = std::clamp(w->unknown164 + v->m_weight.y / grounded / w->unknown160, 0.0f, w->unknown164);
        g_groundY = std::min(g_groundY, hubY - w->radius - rest);
    }
    g_flatGround = true;
    for (int step = 0; step < 36; ++step) {
        v->Step(83);
        v->m_velocity.x = v->m_velocity.y = v->m_velocity.z = 0.0f;
        v->m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
        if (std::getenv("DRIVE_SETTLE") && g_modelFile.find(std::getenv("DRIVE_SETTLE")) != std::string::npos) {
            std::printf("settle %d up %.4f y %.4f vel %.3f %.3f %.3f turn %.4f %.4f %.4f loads", step, v->m_modelAxisY.y,
                        v->m_modelPos.y, v->m_velocity.x, v->m_velocity.y, v->m_velocity.z, v->m_unknown210.x,
                        v->m_unknown210.y, v->m_unknown210.z);
            for (int i = 0; i < v->m_wheelCount; ++i)
                if (v->m_wheels[i] && !(v->m_wheels[i]->flags & WHEEL_DUMMY))
                    std::printf(" %.0f (%.1f %.1f %.1f)", v->m_wheels[i]->unknown168, v->m_wheels[i]->unknown0E0.x,
                                v->m_wheels[i]->unknown0E0.y, v->m_wheels[i]->unknown0E0.z);
            std::printf(" 600 %X\n", v->m_unknown600);
            std::printf("  ground %.3f com %.3f %.3f %.3f pos %.3f %.3f %.3f", g_groundY, v->m_unknown1B0.x,
                        v->m_unknown1B0.y, v->m_unknown1B0.z, v->m_modelPos.x, v->m_modelPos.y, v->m_modelPos.z);
            if (const S_VehicleWheel *w0 = v->m_wheels[0])
                std::printf(" w0 flags %X ground %p ext %.3f centre %.3f %.3f %.3f tyre %d/%d", w0->flags,
                            static_cast<void *>(w0->ground), w0->extension, w0->worldPos.x, w0->worldPos.y,
                            w0->worldPos.z, w0->tyreType, w0->tyreIndex);
            for (int i = 0; i < v->m_wheelCount; ++i)
                if (v->m_wheels[i] && !(v->m_wheels[i]->flags & WHEEL_DUMMY))
                    std::printf(" | c %.3f %.3f %.3f f %.0f %.0f %.0f", v->m_wheels[i]->unknown050.x,
                                v->m_wheels[i]->unknown050.y, v->m_wheels[i]->unknown050.z, v->m_wheels[i]->unknown070.x,
                                v->m_wheels[i]->unknown070.y, v->m_wheels[i]->unknown070.z);
            std::printf("\n");
        }
    }
    v->SetEngine(true, true);
    if (const char *trace = std::getenv("DRIVE_TRACE"))  // a model's name: trace that one instead
        g_traceDrive = g_modelFile.find(trace) != std::string::npos;
    const int gearStarts0 = GearSoundStarts(v);
    g_rayTests = g_bodyCollisions = g_deforms = g_bumpSounds = g_impactSounds = 0;
    g_sphereTests = g_boxTests = g_boxTouches = g_ignores = g_ignoreClears = 0;
    const float fuel0 = v->m_fuel;
    const float height0 = v->m_modelPos.y;
    const float heading0 = std::atan2(v->m_modelAxisZ.x, v->m_modelAxisZ.z) * 57.29578f;
    float maxRevs = 0.0f, topSpeed = 0.0f, minForward = 1e9f, minUpright = 1.0f, maxSide = 0.0f;
    float lowest = 1e9f, highest = -1e9f, maxTurn = 0.0f, maxTurnAccelerating = 0.0f, maxSideAccelerating = 0.0f;
    int lowestGear = 99, highestGear = -99, downshifts = 0, lastGear = v->m_gear, stoppedAt = -1, reverseAt = -1;
    // The wheel whose spin is measured while accelerating: the first one not driven, else the first.
    int spun = -1;
    for (int i = 0; i < v->m_wheelCount; ++i) {
        const S_VehicleWheel *w = v->m_wheels[i];
        if (w && !(w->flags & WHEEL_DUMMY) && (spun < 0 || ((v->m_wheels[spun]->flags & WHEEL_DRIVEN) &&
                                                           !(w->flags & WHEEL_DRIVEN))))
            spun = i;
    }
    double rolled = 0.0, travelled = 0.0;
    int results[3] = {};
    int dustTaken = 0, skidTaken = 0, exhaustTaken = 0, mostTaken = 0;
    g_skidLays = 0;
    for (int step = 0; step < 780; ++step) {
        const bool accelerating = step < 480;
        v->SetThrottle(accelerating ? 1.0f : 0.0f, true);
        v->SetBrake(accelerating ? 0.0f : 1.0f);
        const float spin0 = spun >= 0 ? v->m_wheels[spun]->unknown1F0 : 0.0f;
        v->Step(83);
        const int result = v->Update(83);
        UpdateCarEffectEmitters(83);
        ++results[result >= 0 && result <= 2 ? result : 0];
        for (int i = 0; i < v->m_wheelCount; ++i)
            g_skidLays += v->m_wheels[i] && (v->m_wheels[i]->flags & WHEEL_SLIDING) &&
                          (v->m_unknownF6C & VEHICLE_SIM_SKID_MARKS);
        // The effects taken: the wheels' dust / skid, the exhausts', the most emitters taken.
        int taken = 0;
        for (const S_CarEmitter &e : g_carEmitters)
            taken += e.active;
        mostTaken = std::max(mostTaken, taken);
        for (int i = 0; i < v->m_wheelCount; ++i) {
            const S_VehicleWheel *w = v->m_wheels[i];
            if (w && w->effect >= 0) {
                dustTaken += g_carEmitters[w->effect].effect == g_carSurfaces[0].dustEffect;
                skidTaken += g_carEmitters[w->effect].effect == g_carSurfaces[0].skidEffect;
            }
        }
        for (const S_VehicleExhaust &e : v->m_exhausts)
            exhaustTaken += e.effect >= 0;
        // (Only while a step turns the wheel by less than 2 radians: its spin is kept in -2 pi..2 pi.)
        const float moved = (v->m_velocity.x * v->m_modelAxisZ.x + v->m_velocity.y * v->m_modelAxisZ.y +
                             v->m_velocity.z * v->m_modelAxisZ.z) * 0.083f;
        if (spun >= 0 && accelerating && std::fabs(moved) < v->m_wheels[spun]->radius * 2.0f) {
            float turned = v->m_wheels[spun]->unknown1F0 - spin0;
            turned = turned > 3.14159265f ? turned - 6.2831853f : turned < -3.14159265f ? turned + 6.2831853f : turned;
            rolled -= turned * v->m_wheels[spun]->radius;
            travelled += moved;
        }
        g_bodyCollisions += v->m_unknown2288 != 0;
        if (g_traceDrive && step < 30 && std::getenv("DRIVE_DEBUG")) {
            const S_VehicleWheel *w0 = v->m_wheels[v->m_wheelCount - 1];
            std::printf("dbg %d f010 %.1f %.1f %.1f f020 %.1f %.1f %.1f vel %.3f %.3f %.3f revs %.1f clutch %.2f 8F8 %.2f 530 %.2f ratio %.2f 86C %.1f flags %X 600 %X 910 %X g %.0f/%.0f E0 %.1f %.1f %.1f B0 %.2f %.2f %.2f\n",
                        step, v->m_unknown010.x, v->m_unknown010.y, v->m_unknown010.z, v->m_unknown020.x,
                        v->m_unknown020.y, v->m_unknown020.z, v->m_velocity.x, v->m_velocity.y, v->m_velocity.z,
                        v->m_engineRevs, v->m_unknown904, v->m_unknown8F8, v->m_unknown530, v->m_gearRatios[v->m_gear + 1],
                        v->m_unknown86C, w0->flags, v->m_unknown600, v->m_unknown910, w0->unknown1B0, w0->unknown1B4,
                        w0->unknown0E0.x, w0->unknown0E0.y, w0->unknown0E0.z, w0->unknown0B0.x, w0->unknown0B0.y,
                        w0->unknown0B0.z);
        }
        const float forward = v->m_velocity.x * v->m_modelAxisZ.x + v->m_velocity.y * v->m_modelAxisZ.y +
                              v->m_velocity.z * v->m_modelAxisZ.z;
        maxRevs = std::max(maxRevs, v->m_engineRevs);
        if (accelerating) {
            topSpeed = std::max(topSpeed, forward);
            highestGear = std::max(highestGear, v->m_gear);
            lowestGear = std::min(lowestGear, v->m_gear);
            downshifts += v->m_gear < lastGear;
        } else {
            minForward = std::min(minForward, forward);
            if (stoppedAt < 0 && v->m_speed < 0.5f)     // Step's standstill
                stoppedAt = step;
            if (reverseAt < 0 && v->m_gear < 0)
                reverseAt = step;
        }
        lastGear = v->m_gear;
        // The attitude: upright (the y axis' height), the side slip, the heading off +z, the
        // model's height over the start.
        const float side = v->m_velocity.x * v->m_modelAxisX.x + v->m_velocity.y * v->m_modelAxisX.y +
                           v->m_velocity.z * v->m_modelAxisX.z;
        // The heading's change since the start (-180..180).
        float turned = std::atan2(v->m_modelAxisZ.x, v->m_modelAxisZ.z) * 57.29578f - heading0;
        turned = turned > 180.0f ? turned - 360.0f : turned < -180.0f ? turned + 360.0f : turned;
        minUpright = std::min(minUpright, v->m_modelAxisY.y);
        maxSide = std::max(maxSide, std::fabs(side));
        maxTurn = std::max(maxTurn, std::fabs(turned));
        if (accelerating) {
            maxTurnAccelerating = std::max(maxTurnAccelerating, std::fabs(turned));
            maxSideAccelerating = std::max(maxSideAccelerating, std::fabs(side));
        }
        if (step >= 24) {
            lowest = std::min(lowest, v->m_modelPos.y - height0);
            highest = std::max(highest, v->m_modelPos.y - height0);
        }
        if (g_traceDrive && (step % 12 == 11 || std::getenv("DRIVE_EVERY"))) {
            std::printf("drive t %5.2f gear %d rpm %6.0f speed %6.2f km/h clutch %.2f fuel %.3f brake %.2f", (step + 1) * 0.083f,
                        v->m_gear, v->m_engineRevs * 60.0f, forward * 3.6f, v->m_unknown904, v->m_fuel, v->m_brake);
            std::printf(" up %.4f side %.3f turn %.2f dy %.3f loads", v->m_modelAxisY.y, side, turned,
                        v->m_modelPos.y - height0);
            for (int i = 0; i < v->m_wheelCount; ++i)
                if (v->m_wheels[i] && !(v->m_wheels[i]->flags & WHEEL_DUMMY))
                    std::printf(" %.0f%s", v->m_wheels[i]->unknown168,
                                v->m_wheels[i]->flags & WHEEL_SPINNING  ? "s"
                                : v->m_wheels[i]->flags & WHEEL_LOCKED ? "l"
                                                                       : "");
            std::printf("\n");
        }
    }
    g_traceDrive = false;
    std::printf("drive: gears %d..%d of %d downshifts %d max rpm %.1f limit %.1f top %.2f m/s fuel used %.4f stop step %d reverse step %d back %.2f m/s gear sounds %d\n",
                lowestGear, highestGear, v->m_gearCount, downshifts, maxRevs * 60.0f, v->m_revLimit * 60.0f, topSpeed,
                fuel0 - v->m_fuel, stoppedAt, reverseAt, minForward, GearSoundStarts(v) - gearStarts0);
    std::printf("drive body: upright min %.5f side max %.4f m/s turn max %.3f deg (accelerating %.4f m/s %.3f deg) height %.4f..%.4f rays %d collisions %d steps %d deforms %d bump sounds %d\n",
                minUpright, maxSide, maxTurn, maxSideAccelerating, maxTurnAccelerating, lowest, highest,
                g_rayTests, g_bodyCollisions, 780, g_deforms, g_bumpSounds);
    std::printf("drive collide: box tests %d touched %d sphere tests %d impact sounds %d ignores %d/%d\n", g_boxTests,
                g_boxTouches, g_sphereTests, g_impactSounds, g_ignores, g_ignoreClears);
    int laid = 0;
    for (int i = 0; i < v->m_wheelCount; ++i)
        if (v->m_wheels[i])
            for (const S_VehicleSkidMark &m : v->m_wheels[i]->skidMarks)
                laid += m.frame0->IsOn();
    std::printf("drive update: done %d idle %d lost %d spin wheel %d rolled %.2f m travelled %.2f m skid marks %d "
                "(segments on %d)\n", results[1], results[2], results[0], spun, rolled, travelled, g_skidLays, laid);
    std::printf("drive effects: dust %d skid %d exhaust %d (emitter steps) most taken %d of %zu\n", dustTaken,
                skidTaken, exhaustTaken, mostTaken, g_carEmitters.size());
    TrackTest(v);

    // The crash test: from a standstill (the motion stopped), full throttle at a wall across the
    // heading 15 m ahead of the body box's front, 25 s. The body's points meet it: the car stops
    // there. (The front: the farthest corner of the body box along the heading.)
    Crash(v);
    // The crash's damage repaired (the bent wheels would steer the tests after it).
    v->ResetCondition();
    v->RepairParts(false);
    v->m_unknown528 = bumps;
    BumpTest(v);
    v->m_unknown528 = 0.0f;
    RestartTest(v, 0.0f, false);
    RestartTest(v, -6.0f, true);
    RestartTest(v, 10.0f, true);
    StateTest(v);
    ReplaceTest(v);
    HitTest(v);
    g_flatGround = false;

    // Stop: the volumes out, the skid marks freed, the parts repaired, the lights and sounds
    // unlinked.
    g_volumeCalls = g_unlinked = 0;
    const int soundsBeforeStop = LoadedSounds(v);
    v->Stop();
    size_t skidsLeft = 0;
    for (int i = 0; i < v->m_wheelCount; ++i)
        skidsLeft += v->m_wheels[i] ? v->m_wheels[i]->skidMarks.size() : 0;
    int skidLive = 0;
    for (const I3D_frame *f : g_skidFrames)
        skidLive += f->m_refCount != 0;
    int lampLights = 0, projectorSpots = 0;
    for (const S_VehicleLamp &l : v->m_lamps)
        lampLights += l.light != nullptr;
    for (const S_VehicleProjector &p : v->m_projectors)
        projectorSpots += p.light != nullptr;
    std::printf("stop: volume calls %d specials %zu/%zu skids left %zu skid frames live %d lamp objects on %d sounds left %d unlinked %d (expected %d)\n",
                g_volumeCalls, v->m_specialVolumes.size(), v->m_specialVolumeList.size(), skidsLeft, skidLive, LampObjectsOn(v),
                LoadedSounds(v), g_unlinked, lampLights + projectorSpots + soundsBeforeStop);
    int stopping = 0, ticks = 0;
    for (const S_CarEmitter &e : g_carEmitters)
        stopping += e.active && e.stopping;
    for (; ticks < 20 && std::any_of(g_carEmitters.begin(), g_carEmitters.end(),
                                     [](const S_CarEmitter &e) { return e.active; });
         ++ticks)
        UpdateCarEffectEmitters(83);
    int active = 0;
    for (const S_CarEmitter &e : g_carEmitters)
        active += e.active;
    std::printf("stop effects: let go %d freed in %d ticks active %d\n", stopping, ticks, active);
    delete v;                                       // Release: drops the model's reference
    int live = 0, images = 0;
    for (const S_Made &m : g_made)
        live += m.frame->m_refCount != 0;
    for (const S_Image *i : g_images)
        images += i->refs != 0;
    std::printf("after delete: model refs=%d, made frames %zu (live %d), images %zu (live %d), materials %d/%d released\n",
                g_model->m_refCount, g_made.size(), live, g_images.size(), images, g_materialsReleased, g_materialsMade);
    g_made.clear();
    g_images.clear();
    g_meshes = g_meshesReleased = g_materialsMade = g_materialsReleased = 0;
}

}  // namespace

// --- the car (src/game/car.cpp; part 2a) ---------------------------------------------------------

// An interface released through its first slot (S_CarParts::object), counting.
// The particle effects (the damage parts'): Create logs and hands out handles from 500, Destroy
// logs the handles.
struct S_ParticleMade {
    int effect;
    S_matrix matrix;
    I3D_frame *parent;
    bool faceCamera, transient;
    int handle;
};
std::vector<S_ParticleMade> g_particlesMade;
int g_nextParticle = 500;
int C_ParticlesModule::Create(int effect, const S_matrix &matrix, I3D_frame *parent, bool faceCamera, bool transient)
{
    g_particlesMade.push_back({effect, matrix, parent, faceCamera, transient, g_nextParticle});
    return g_nextParticle++;
}
std::vector<int> g_particlesDestroyed;
void C_ParticlesModule::Destroy(int handle)
{
    g_particlesDestroyed.push_back(handle);
}
// The frames' slot 0x18 (SetPosRot: the damage parts' model on the car): the local matrix the turn
// and the place, logged.
struct S_PosRot {
    I3D_frame *frame;
    S_vector pos;
    S_quat rot;
};
std::vector<S_PosRot> g_posRots;
void __stdcall F_SetPosRot(I3D_frame *self, const S_vector &pos, const S_quat &rot)
{
    g_posRots.push_back({self, pos, rot});
    self->m_rot = rot;
    self->m_localMatrix = rot.RotationMatrix();
    self->m_localMatrix(3) = pos;
}

extern int g_actorInits, g_actorCopies;
extern std::map<std::string, std::vector<uint8_t>> g_files;

// The car on a freshly loaded model: the creator and its name; the constructor's state and the car
// list; SetFrame (the model's vehicle, the callbacks, the seat weapons cleared, the parts set up;
// null and a frame Setup refuses); the properties written and read back over changed values,
// another version, the wrong direction; CopyFrom (a car's block; an actor of another kind: the
// frame only); the list after deleting from the middle; the timer.
void CarTest(const char *path)
{
    if (!LoadModel(path))
        return;
    InitPerfTime();
    const int64_t time0 = GetPerfTimeMs();
    while (GetPerfTimeMs() < 3) {
    }
    C_ActorCreator *creator = g_actorCreators[ACTOR_CAR];
    const bool named = creator && std::strcmp(creator->GetName(), "Car") == 0;
    const size_t cars0 = g_cars.size();
    auto *a = static_cast<C_Car *>(creator->Create());
    const bool made = a->GetType() == ACTOR_CAR && g_cars.size() == cars0 + 1 && g_cars.back() == a &&
                      std::strcmp(*a->GetName(), "Car (text 0x1bf0)") == 0 && a->m_vehicle.m_callbackContext == a &&
                      a->m_periodicLink == -1 && !a->m_driver && a->m_parts.fuse == -1 && !a->m_parts.model &&
                      !a->m_parts.car && !a->m_parts.particleFrame && a->m_parts.fireEffect == -1 &&
                      a->m_parts.wreckSound == -1 && a->m_parts.fireHandle == -1 && a->m_parts.smokeHandle == -1 &&
                      !a->m_parts.wrecked && a->GetHitPoints() == 500 &&
                      a->m_respawnTimer.start >= 3 && a->m_respawnTimer.start < 100 && !a->m_respawnTimer.running && a->m_respawnTimer.period == 0;

    // SetFrame on the model.
    for (C_SeatWeapon *&w : a->m_seatWeapons)
        w = reinterpret_cast<C_SeatWeapon *>(a);
    a->m_started = true;
    const int inits0 = g_actorInits;
    g_cacheLoads.clear();
    const bool framed = a->SetFrame(g_model);
    const void *const want[VEHICLE_CALLBACKS] = {
        reinterpret_cast<void *>(&CarOnBodyActorImpulse), reinterpret_cast<void *>(&CarOnBodyActorTouch),
        reinterpret_cast<void *>(&CarOnBodyActorResponse), reinterpret_cast<void *>(&CarIsActorFirm),
        reinterpret_cast<void *>(&CarOnVehicleImpact), reinterpret_cast<void *>(&CarOnPartOff),
        reinterpret_cast<void *>(&CarOnWheelOff), reinterpret_cast<void *>(&CarOnGlassBroken), nullptr, nullptr,
        reinterpret_cast<void *>(&CarOnBodyGround), reinterpret_cast<void *>(&CarOnWheelGround),
        reinterpret_cast<void *>(&CarOnPartHit), reinterpret_cast<void *>(&CarOnRepaired), nullptr,
        reinterpret_cast<void *>(&SetupCarEmitter), reinterpret_cast<void *>(&ReuseCarEmitter)};
    bool frame = framed && a->GetFrame() == g_model && g_actorInits == inits0 + 1 && a->m_parts.car == a &&
                 std::count(g_cacheLoads.begin(), g_cacheLoads.end(), "DM" + g_modelFile) == 1 &&
                 !a->m_started && a->GetSeatCount() == static_cast<int>(a->m_vehicle.m_seats.size()) &&
                 a->GetEntryCount() == a->GetSeatCount() && a->m_vehicle.m_model == g_model;
    for (void *w : a->m_seatWeapons)
        frame &= !w;
    for (int k = 0; k < VEHICLE_CALLBACKS; ++k)
        frame &= a->m_vehicle.m_callbacks[k] == want[k];
    // Null (C_Actor's refuses) and a frame Setup refuses: no callbacks, no parts.
    auto *b = new C_Car();
    b->m_started = true;
    b->m_seatWeapons[3] = reinterpret_cast<C_SeatWeapon *>(b);
    bool refused = !b->SetFrame(nullptr) && !b->m_started && !b->m_seatWeapons[3] && !b->GetFrame();
    I3D_frame *plain = NewFrame(FRAME_MODEL, "plain", nullptr);
    I3D_frame *plain2 = NewFrame(FRAME_MODEL, "plain2", nullptr);
    refused &= !b->SetFrame(plain) && b->GetFrame() == plain && !b->m_parts.car;
    for (void *c : b->m_vehicle.m_callbacks)
        refused &= !c;

    // The properties: written, the block changed, read back.
    g_files.clear();
    const S_VehicleParams saved = a->m_vehicle.m_params;
    C_chunk out;
    out.Open("car.bin", true);
    bool props = a->WriteProperties(out);
    out.Close();
    const std::vector<uint8_t> file = g_files["car.bin"];
    int version = 0;
    uint32_t size = 0;
    if (file.size() >= 12) {
        std::memcpy(&version, file.data(), 4);
        std::memcpy(&size, file.data() + 4, 4);
    }
    props &= version == 3 && size + 8 == file.size() && std::memcmp(file.data() + 8, "COMP", 4) == 0;
    a->m_vehicle.m_params.mass += 250.0f;
    a->m_vehicle.m_params.tankSize = 1.0f;
    C_chunk in;
    in.Open("car.bin", false);
    props &= a->ReadProperties(in);
    in.Close();
    props &= std::memcmp(&a->m_vehicle.m_params, &saved, sizeof saved) == 0;
    // Another version: refused, the block as it was.
    std::vector<uint8_t> other = file;
    a->m_vehicle.m_params.mass += 250.0f;
    for (uint8_t v : {2, 4}) {
        other[0] = v;
        g_files["v.bin"] = other;
        in.Open("v.bin", false);
        props &= !a->ReadProperties(in) && a->m_vehicle.m_params.mass == saved.mass + 250.0f;
        in.Close();
    }
    // The wrong direction: nothing written, nothing read.
    in.Open("car.bin", false);
    props &= a->WriteProperties(in);
    in.Close();
    props &= g_files["car.bin"] == file;
    out.Open("w.bin", true);
    props &= !a->ReadProperties(out);
    out.Close();
    a->m_vehicle.ApplyParams(const_cast<S_VehicleParams &>(saved), 0);

    // CopyFrom: a car's block (the source without a frame: C_Actor's SetFrame refuses, the frame
    // stays); an actor of another kind: the frame only.
    auto *s = new C_Car();
    s->m_vehicle.m_params = saved;
    s->m_vehicle.m_params.mass = saved.mass + 100.0f;
    const int copies0 = g_actorCopies;
    a->CopyFrom(s);
    bool copy = a->m_vehicle.m_params.mass == saved.mass + 100.0f && g_actorCopies == copies0 + 1 && a->GetFrame() == g_model;
    a->m_vehicle.ApplyParams(const_cast<S_VehicleParams &>(saved), 0);
    struct C_Other : C_Actor {
        C_Other() : C_Actor(ACTOR_SPAWN_POINT) {}
        void CopyFrom(const C_Actor *) override {}
        void StartMission(bool) override {}
        void StopMission(bool) override {}
        void RestartMission(bool) override {}
    } otherActor;
    otherActor.SetFrame(plain2);
    const int inits1 = g_actorInits;
    b->CopyFrom(&otherActor);
    copy &= b->GetFrame() == plain2 && g_actorInits == inits1 + 1 && g_actorCopies == copies0 + 1;

    // The damage parts' release: the object through its first slot, +0x24's blocks freed, +0x34
    // emptied (also from the destructor).
    I3D_frame *partsModel = NewFrame(FRAME_MODEL, "dm", nullptr);
    partsModel->m_refCount = 3;
    S_CarParts parts;
    parts.model = static_cast<I3D_model *>(partsModel);
    for (int k = 0; k < 2; ++k) {
        auto *e = static_cast<S_CarExplosive *>(_aligned_malloc(sizeof(S_CarExplosive), 16));
        e->effect = 50 + k;
        parts.explosives.push_back(e);
    }
    parts.turretFrames = {partsModel, partsModel};
    parts.zones = {{partsModel, 1, 2}};
    parts.seatFrames = {partsModel};
    g_particlesDestroyed.clear();
    parts.Release();
    bool released = partsModel->m_refCount == 2 && !parts.model && parts.explosives.empty() && parts.turretFrames.empty() &&
                    parts.zones.size() == 1 && parts.seatFrames.size() == 1 && g_particlesDestroyed.empty();
    parts.Release();                                    // (no model: nothing)
    released &= partsModel->m_refCount == 2;

    // The list: deleting from the middle moves the last car into the place.
    auto *x = new C_Car();
    auto *y = new C_Car();
    auto *z = new C_Car();
    delete x;
    const size_t n = g_cars.size();
    bool list = n >= 4 && g_cars[n - 4] == b && g_cars[n - 3] == s && g_cars[n - 2] == z && g_cars[n - 1] == y;
    delete y;
    delete z;
    delete s;
    delete b;
    a->m_parts.model = static_cast<I3D_model *>(partsModel);
    delete a;
    released &= partsModel->m_refCount == 1;
    list &= g_cars.size() == cars0;
    // The timer against the performance counter: 50 ms.
    LARGE_INTEGER frequency, start, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const int64_t before = GetPerfTimeMs();
    do {
        QueryPerformanceCounter(&now);
    } while ((now.QuadPart - start.QuadPart) * 1000 < frequency.QuadPart * 50);
    const int64_t elapsed = GetPerfTimeMs() - before;
    const bool timer = time0 >= 0 && time0 < 3 && elapsed >= 49 && elapsed <= 60;
    std::printf("car: named %d made %d frame %d refused %d properties %d copy %d released %d list %d timer %d\n", named,
                made, frame, refused, props, copy, released, list, timer);
}

// --- the seats (src/game/car_seats.cpp; part 2b) ---------------------------------------------------

uint32_t g_gameRole = GAME_ROLE_LOCAL;
int g_localPlayerSlot;
bool g_firstPerson;
bool C_game::IsFirstPersonView() const
{
    return g_firstPerson;
}
std::vector<std::pair<int, int>> g_linkRates;
void PeriodicLinks_SetRate(int id, int rate)
{
    g_linkRates.emplace_back(id, rate);
}
std::vector<int> g_uses;
void C_Script::OnUse(int arg)
{
    g_uses.push_back(arg);
}
std::vector<const C_CameraFrameKeeper *> g_restored;
bool C_CameraFrameKeeper::Restore()
{
    g_restored.push_back(this);
    return true;
}
C_NetObjectMessage::C_NetObjectMessage(int t) : type(t), sender(0), unknown24(0) {}
C_NetObjectMessage::~C_NetObjectMessage() = default;
#ifndef REAL_SEAT_WEAPON
void C_SeatWeaponRoundsMessage::Write(C_BitStream *) {}
void C_SeatWeaponRoundsMessage::Read(C_BitStream *) {}
#endif
struct S_Queued {
    int type, rounds, seat, sender;
    bool reliable, fromClient;
};
std::vector<S_Queued> g_queued;
// (The queue holds the message: +0x24 counted up, kept here; the car's ReadMessages deletes what
// nothing holds.)
std::vector<C_NetObjectMessage *> g_heldMessages;
void C_NetObject::QueueMessage(C_NetObjectMessage *msg, int sender, bool reliable)
{
    if (msg->type == SEAT_WEAPON_MSG_AMMO) {
        auto *m = static_cast<C_SeatWeaponAmmoMessage *>(msg);
        g_queued.push_back({m->type, m->typeId, m->uid, sender, reliable, m->fromClient});
    } else if (msg->type == SEAT_WEAPON_MSG_ROUNDS) {
        auto *m = static_cast<C_SeatWeaponRoundsMessage *>(msg);
        g_queued.push_back({m->type, m->rounds, m->seat, sender, reliable, m->fromClient});
    } else if (msg->type == SEAT_WEAPON_MSG_SHOT) {
        g_queued.push_back({msg->type, -1, static_cast<C_SeatWeaponShotMessage *>(msg)->seat, sender, reliable, msg->fromClient});
    } else {
        g_queued.push_back({msg->type, -1, static_cast<C_SeatWeaponReloadMessage *>(msg)->seat, sender, reliable, msg->fromClient});
    }
    ++msg->unknown24;
    g_heldMessages.push_back(msg);
}
int C_NetReplication::GetLocalId() const
{
    return 6;
}
C_NetReplication *g_netReplication;

// The seat weapons as the car's seats see them (part 3 ports the real ones): what each call got.
struct S_FakeWeapon {
    int rounds = 0;
    C_ItemDef *def = nullptr;
    int text = 0;
    S_vector aim = {0.0f, 0.0f, 0.0f}, origin = {0.0f, 0.0f, 0.0f};
    float limits[4] = {};
    bool aimable = false, c8 = false, view = false;
    C_Human *gunner = nullptr;
    int gunners = 0, views = 0, controls = 0;
    float *axes = nullptr, *buttons = nullptr, *aimPos = nullptr;
    int *c8Args[3] = {};
    std::vector<int> updates, ticks;
    uint32_t refillTime = 0;
    int refillBelow = 0;
    // (Part 2e: the lifetime.)
    C_Car *car = nullptr;
    I3D_frame *frame = nullptr, *seatFrame = nullptr;
    int seat = -1, inits = 0, stops = 0, restarts = 0;
    C_SeatWeapon::T_MessageSender sender = nullptr;
    C_Actor *senderContext = nullptr;
    // (Part 2f-i: the network messages.)
    struct S_Shot {
        S_vector4 pos, dir;
        int barrel, unused;
    };
    std::vector<S_Shot> shots;
    bool fires = true;
    int playShots = 0, reloads = 0, showReloads = 0;
    // (Part 3b: the arguments the original passes and does not read.)
    int playShotBarrel = -1;
    C_ItemContainer *reloadStore = nullptr, *showReloadStore = nullptr;
    std::vector<int> roundsSet;
    // (Part 2f-ii: the saved game. SaveGame writes saveTag; LoadGame logs the int read, -1 when none.)
    int saveTag = 0;
    std::vector<int> loadedTags;
};
S_FakeWeapon &Fake(C_SeatWeapon *w)
{
    return *reinterpret_cast<S_FakeWeapon *>(w);
}
C_SeatWeapon *AsWeapon(S_FakeWeapon &f)
{
    return reinterpret_cast<C_SeatWeapon *>(&f);
}
#ifndef REAL_SEAT_WEAPON
// (The fakes; the REAL_SEAT_WEAPON build links src/game/seat_weapon.cpp instead.)
void C_SeatWeapon::SetGunner(C_Human *gunner)
{
    Fake(this).gunner = gunner;
    ++Fake(this).gunners;
}
int C_SeatWeapon::GetRounds()
{
    return Fake(this).rounds;
}
C_ItemDef *C_SeatWeapon::GetItemDef()
{
    return Fake(this).def;
}
int C_SeatWeapon::GetItemText()
{
    return Fake(this).text;
}
S_vector C_SeatWeapon::GetAim()
{
    return Fake(this).aim;
}
S_vector C_SeatWeapon::GetOrigin()
{
    return Fake(this).origin;
}
void C_SeatWeapon::GetLimits(float *a, float *b, float *c, float *d) const
{
    const float *l = reinterpret_cast<const S_FakeWeapon *>(this)->limits;
    *a = l[0];
    *b = l[1];
    *c = l[2];
    *d = l[3];
}
bool C_SeatWeapon::CanAim(float *pos)
{
    Fake(this).aimPos = pos;
    return Fake(this).aimable;
}
void C_SeatWeapon::ApplyControls(float *axes, float *buttons)
{
    Fake(this).axes = axes;
    Fake(this).buttons = buttons;
    ++Fake(this).controls;
}
void C_SeatWeapon::ShowView(bool on)
{
    Fake(this).view = on;
    ++Fake(this).views;
}
void C_SeatWeapon::Update(int time)
{
    Fake(this).updates.push_back(time);
}
void C_SeatWeapon::Tick(int time)
{
    Fake(this).ticks.push_back(time);
}
uint32_t C_SeatWeapon::GetRefillTime() const
{
    return reinterpret_cast<const S_FakeWeapon *>(this)->refillTime;
}
void C_SeatWeapon::SetRefillTime(uint32_t time)
{
    Fake(this).refillTime = time;
}
int C_SeatWeapon::GetRefillBelow() const
{
    return reinterpret_cast<const S_FakeWeapon *>(this)->refillBelow;
}
bool C_SeatWeapon::UnknownC8(int *a, int *b, int *c)
{
    Fake(this).c8Args[0] = a;
    Fake(this).c8Args[1] = b;
    Fake(this).c8Args[2] = c;
    return Fake(this).c8;
}
#endif

// A client's wreck darkens the crew (game/car_net.cpp): the lighting's updates.
struct S_LightingUpdate {
    const C_HumanLighting *lighting;
    I3D_frame *model;
    int time, mode;
    float level, shownLevel;                            // at the call
};
std::vector<S_LightingUpdate> g_lightingUpdates;
void C_HumanLighting::Update(I3D_frame *model, int time, int mode)
{
    g_lightingUpdates.push_back({this, model, time, mode, m_level, m_shownLevel});
}

// --- the mission's surroundings (src/game/car_mission.cpp; part 2e) --------------------------------

// The seat weapons' lifetime: a S_FakeWeapon in the weapon's storage, each step in g_weaponEvents;
// Init refuses g_failInitSeat and hands the weapon g_weaponDef.
std::vector<std::string> g_weaponEvents;
int g_failInitSeat = -1;
C_ItemDef *g_weaponDef;
static_assert(sizeof(S_FakeWeapon) <= sizeof(C_SeatWeapon), "the fake fits the weapon's storage");
#ifndef REAL_SEAT_WEAPON
C_SeatWeapon::C_SeatWeapon()
{
    new (static_cast<void *>(this)) S_FakeWeapon();
    g_weaponEvents.push_back("make");
}
C_SeatWeapon::~C_SeatWeapon()
{
    g_weaponEvents.push_back("delete " + std::to_string(Fake(this).seat));
    Fake(this).~S_FakeWeapon();
    // The real members' destructors run next (part 3a gave the weapon a vector): zeros are an empty one.
    std::memset(static_cast<void *>(this), 0, sizeof(C_SeatWeapon));
}
bool C_SeatWeapon::Init(C_Actor *owner, I3D_frame *frame, I3D_frame *seatFrame, int seat)
{
    S_FakeWeapon &f = Fake(this);
    f.car = static_cast<C_Car *>(owner);
    f.frame = frame;
    f.seatFrame = seatFrame;
    f.seat = seat;
    f.def = g_weaponDef;
    ++f.inits;
    g_weaponEvents.push_back("init " + std::to_string(seat));
    return seat != g_failInitSeat;
}
void C_SeatWeapon::Stop()
{
    ++Fake(this).stops;
    g_weaponEvents.push_back("stop " + std::to_string(Fake(this).seat));
}
void C_SeatWeapon::Restart()
{
    ++Fake(this).restarts;
    g_weaponEvents.push_back("restart " + std::to_string(Fake(this).seat));
}
void C_SeatWeapon::SetRefillBelow(int rounds)
{
    Fake(this).refillBelow = rounds;
}
void C_SeatWeapon::SetMessageSender(T_MessageSender sender, C_Actor *context)
{
    Fake(this).sender = sender;
    Fake(this).senderContext = context;
}
bool __fastcall SeatWeaponSendMessage(int, const S_vector4 *, const S_vector4 *, int, int, C_Actor *)
{
    return true;
}
bool C_SeatWeapon::FireShot(const S_vector4 *pos, const S_vector4 *dir, int barrel, int unused)
{
    Fake(this).shots.push_back({*pos, *dir, barrel, unused});
    return Fake(this).fires;
}
void C_SeatWeapon::PlayShot(int barrel)
{
    ++Fake(this).playShots;
    Fake(this).playShotBarrel = barrel;
}
void C_SeatWeapon::Reload(C_ItemContainer *store)
{
    ++Fake(this).reloads;
    Fake(this).reloadStore = store;
}
void C_SeatWeapon::ShowReload(C_ItemContainer *store)
{
    ++Fake(this).showReloads;
    Fake(this).showReloadStore = store;
}
void C_SeatWeapon::SetRounds(int rounds)
{
    Fake(this).roundsSet.push_back(rounds);
}
bool C_SeatWeapon::SaveGame(C_chunk &chunk)
{
    chunk.Write(Fake(this).saveTag);
    return true;
}
bool C_SeatWeapon::LoadGame(C_chunk &chunk)
{
    int tag = -1;
    chunk.Read(tag);
    Fake(this).loadedTags.push_back(tag);
    return true;
}
void C_SeatWeaponShotMessage::Write(C_BitStream *) {}
void C_SeatWeaponShotMessage::Read(C_BitStream *) {}
void C_SeatWeaponReloadMessage::Write(C_BitStream *) {}
void C_SeatWeaponReloadMessage::Read(C_BitStream *) {}
C_NetObjectMessage *__fastcall MakeSeatWeaponMessage(void *, C_BitStream *, bool)
{
    return nullptr;
}
#endif

// The camera keepers: who made them for which dummy; deleting one restores it (the original's
// destructor is Restore).
struct S_FakeKeeper {
    C_Actor *owner;
    I3D_frame *frame;
};
std::vector<const C_CameraFrameKeeper *> g_keepersMade, g_keepersDeleted;
C_CameraFrameKeeper::C_CameraFrameKeeper(C_Actor *owner, I3D_frame *frame)
{
    new (static_cast<void *>(this)) S_FakeKeeper{owner, frame};
    g_keepersMade.push_back(this);
}
C_CameraFrameKeeper::~C_CameraFrameKeeper()
{
    Restore();
    g_keepersDeleted.push_back(this);
}
const S_FakeKeeper &Keeper(const C_CameraFrameKeeper *k)
{
    return *reinterpret_cast<const S_FakeKeeper *>(k);
}

// The car's net object and its property 1 (the tracks of the place), and the replication.
struct S_FakeTrack {
    std::vector<S_vector4> values;
    std::vector<S_vector> rates;
    S_vector4 current = {}, rate = {};                  // what the replay reads
};
struct S_FakeDirTrack {
    std::vector<std::pair<S_vector4, float>> values;
    S_vector4 current = {}, target = {};
    float angle = 0.0f;
};
struct S_FakeProperty {
    std::vector<std::pair<int, int>> fields;
    S_FakeTrack track;
    S_FakeDirTrack dir;
    uint32_t flags = 0;
    std::vector<uint32_t> flagSets;
};
struct S_FakeNet {
    NetStateHandler read = nullptr, write = nullptr;
    void *stateContext = nullptr;
    NetMessageFactory factory = nullptr;
    void *factoryContext = nullptr;
    int unknown40 = -1;
    S_NetQuantization *q0[3] = {}, *q1[3] = {};
    int sets0 = 0, sets1 = 0;
    std::vector<int> propertyAsks;
    S_FakeProperty property;
    // (Part 2f-i.)
    int sendMask = -1, unknown3C = 0;
    std::vector<float> ticks, updates;
    std::vector<C_NetObjectMessage *> incoming;         // PopMessage's, from the front
};
S_FakeNet g_fakeNet;
S_FakeNet &Net(C_NetObject *o);
// The replication's times (Tick on the driving machine, Update on the others; ms) and the message
// reads (PopMessage finding none: one per ReadMessages).
std::vector<int> g_networkTimes;
int g_messageReads;
void C_NetObject::Tick(float seconds)
{
    Net(this).ticks.push_back(seconds);
    g_networkTimes.push_back(static_cast<int>(std::lround(seconds * 1000.0f)));
}
void C_NetObject::Update(float seconds)
{
    Net(this).updates.push_back(seconds);
    g_networkTimes.push_back(static_cast<int>(std::lround(seconds * 1000.0f)));
}
void C_NetObject::SetSendMask(int mask)
{
    Net(this).sendMask = mask;
}
void C_NetObject::SetUnknown3C(int value)
{
    Net(this).unknown3C = value;
}
int C_NetObject::GetUnknown3C() const
{
    return reinterpret_cast<const S_FakeNet *>(this)->unknown3C;
}
C_NetObjectMessage *C_NetObject::PopMessage(int)
{
    std::vector<C_NetObjectMessage *> &in = Net(this).incoming;
    if (in.empty()) {
        ++g_messageReads;
        return nullptr;
    }
    C_NetObjectMessage *msg = in.front();
    in.erase(in.begin());
    --msg->unknown24;
    return msg;
}
uint32_t C_NetProperty::GetFlags() const
{
    return reinterpret_cast<const S_FakeProperty *>(this)->flags;
}
void C_NetProperty::SetFlags(uint32_t flags)
{
    reinterpret_cast<S_FakeProperty *>(this)->flags = flags;
    reinterpret_cast<S_FakeProperty *>(this)->flagSets.push_back(flags);
}
void C_NetTrack::SetRate(const S_vector &rate)
{
    reinterpret_cast<S_FakeTrack *>(this)->rates.push_back(rate);
}
const S_vector4 &C_NetTrack::GetCurrent()
{
    return reinterpret_cast<S_FakeTrack *>(this)->current;
}
const S_vector4 &C_NetTrack::GetRate()
{
    return reinterpret_cast<S_FakeTrack *>(this)->rate;
}
const S_vector4 &C_NetDirTrack::GetValue()
{
    return reinterpret_cast<S_FakeDirTrack *>(this)->current;
}
const S_vector4 &C_NetDirTrack::GetTarget()
{
    return reinterpret_cast<S_FakeDirTrack *>(this)->target;
}
float C_NetDirTrack::GetAngle()
{
    return reinterpret_cast<S_FakeDirTrack *>(this)->angle;
}
S_FakeNet &Net(C_NetObject *o)
{
    return *reinterpret_cast<S_FakeNet *>(o);
}
void C_NetObject::SetQuantizations0(S_NetQuantization *a, S_NetQuantization *b, S_NetQuantization *c)
{
    S_FakeNet &n = Net(this);
    n.q0[0] = a;
    n.q0[1] = b;
    n.q0[2] = c;
    ++n.sets0;
}
void C_NetObject::SetQuantizations1(S_NetQuantization *a, S_NetQuantization *b, S_NetQuantization *c)
{
    S_FakeNet &n = Net(this);
    n.q1[0] = a;
    n.q1[1] = b;
    n.q1[2] = c;
    ++n.sets1;
}
C_NetProperty *C_NetObject::GetProperty(int index)
{
    Net(this).propertyAsks.push_back(index);
    return index == 1 ? reinterpret_cast<C_NetProperty *>(&Net(this).property) : nullptr;
}
void C_NetObject::SetStateHandlers(NetStateHandler read, NetStateHandler write, void *context)
{
    Net(this).read = read;
    Net(this).write = write;
    Net(this).stateContext = context;
}
void C_NetObject::SetMessageFactory(NetMessageFactory factory, void *context)
{
    Net(this).factory = factory;
    Net(this).factoryContext = context;
}
void C_NetObject::SetUnknown40(bool value)
{
    Net(this).unknown40 = value;
}
C_NetTrack &C_NetProperty::GetTrack()
{
    return *reinterpret_cast<C_NetTrack *>(&reinterpret_cast<S_FakeProperty *>(this)->track);
}
C_NetDirTrack &C_NetProperty::GetDirTrack()
{
    return *reinterpret_cast<C_NetDirTrack *>(&reinterpret_cast<S_FakeProperty *>(this)->dir);
}
void C_NetProperty::SetField(int field, int value)
{
    reinterpret_cast<S_FakeProperty *>(this)->fields.emplace_back(field, value);
}
void C_NetTrack::SetValue(const S_vector4 &value)
{
    reinterpret_cast<S_FakeTrack *>(this)->values.push_back(value);
}
void C_NetDirTrack::SetValue(const S_vector4 &dir, float angle)
{
    reinterpret_cast<S_FakeDirTrack *>(this)->values.emplace_back(dir, angle);
}
std::vector<std::pair<int, bool>> g_netFinds;
std::vector<C_NetObject *> g_netRemoved;
std::vector<int> g_netC4, g_netC8;
bool g_netFindNone;                                     // FindObject finds nothing (part 3a)
C_NetObject *C_NetReplication::FindObject(int id, bool create)
{
    g_netFinds.emplace_back(id, create);
    return g_netFindNone ? nullptr : reinterpret_cast<C_NetObject *>(&g_fakeNet);
}
void C_NetReplication::RemoveObject(C_NetObject *object)
{
    g_netRemoved.push_back(object);
}
void C_NetReplication::SetUnknownC4(int value)
{
    g_netC4.push_back(value);
}
void C_NetReplication::SetUnknownC8(int value)
{
    g_netC8.push_back(value);
}
// The quantisations' setup (net/bitstream.cpp's is checked on its own): the arguments.
struct S_QuantSetup {
    S_NetQuantization *q;
    S_vector4 span, from, step;
    int unknown;
};
std::vector<S_QuantSetup> g_quantSetups;
void S_NetQuantization::Setup(const S_vector4 &span, const S_vector4 &from, const S_vector4 &step, int unknown)
{
    g_quantSetups.push_back({this, span, from, step, unknown});
}
// The periodic links.
std::vector<std::pair<C_Actor *, int>> g_linkAdds;
std::vector<int> g_linkRemoves;
int PeriodicLinks_Add(C_Actor *actor, int arg)
{
    g_linkAdds.emplace_back(actor, arg);
    return 40 + static_cast<int>(g_linkAdds.size());
}
void PeriodicLinks_Remove(int id)
{
    g_linkRemoves.push_back(id);
}

// The actor messages (CreateActorMessage's, below): emptied; how many of a car message kind.
void ClearStreams();
int CountStreams(uint32_t kind);

namespace {

// A frame under `parent` named `prefix.name` (FindChildFrame matches the part after the prefix).
I3D_frame *AddFrame(E_I3D_FRAME_TYPE type, const std::string &prefix, const char *name, I3D_frame *parent)
{
    I3D_frame *f = NewFrame(type, prefix + "." + name, parent);
    f->m_name = Stub(f).name.c_str();
    f->m_nameOffset = static_cast<uint16_t>(prefix.size() + 1);
    f->m_frameFlags |= 1u;
    return f;
}

bool Near(float a, double b, double eps)
{
    return std::fabs(a - b) <= eps;
}

// The way out of `seat` worked out here (in doubles, from the matrices: the body's rows as axes)
// over the stub's flat ground at `groundY`: whether the spheres let it out (the first, moved
// level, meets the ground only when already within it), where it starts (the world y) and ends.
struct S_Entry {
    bool found;
    double startY, px, py, pz, dx, dz;
};
S_Entry ExpectedEntry(C_Car *car, int seat, float groundY)
{
    const C_Vehicle &v = car->m_vehicle;
    const S_matrix &b = v.m_body->GetMatrix();
    const S_vector p = v.m_seats[seat].frame->GetWorldPos();
    const double d[3] = {p.x - b.m[3][0], p.y - b.m[3][1], p.z - b.m[3][2]};
    double local[3];
    for (int i = 0; i < 3; ++i) {
        const double len2 = double(b.m[i][0]) * b.m[i][0] + double(b.m[i][1]) * b.m[i][1] + double(b.m[i][2]) * b.m[i][2];
        local[i] = (d[0] * b.m[i][0] + d[1] * b.m[i][1] + d[2] * b.m[i][2]) / len2;
    }
    const bool behind = (v.m_seats[seat].flags & 0x200) != 0;
    double sx = b.m[0][0], sz = b.m[0][2];
    const double sl = std::sqrt(sx * sx + sz * sz);
    sx /= sl;
    sz /= sl;
    double width = (double(v.m_bodyMax.x) - v.m_bodyMin.x) * 0.75 + (behind ? 0.0 : 0.5);
    if (local[0] < 0.0)
        width = -width;
    const double lz = behind ? v.m_bodyMin.z - 1.0 : local[2];
    double start[3];
    for (int c = 0; c < 3; ++c)
        start[c] = b.m[3][c] + local[1] * b.m[1][c] + lz * b.m[2][c];
    const double px = start[0] + sx * width, pz = start[2] + sz * width;
    double dx = p.x - px, dz = p.z - pz;
    const double dl = std::sqrt(dx * dx + dz * dz);
    dx /= dl;
    dz /= dl;
    const double gap1 = start[1] + 1.0 - groundY, gap2 = start[1] + 1.5 - groundY;
    const bool found = !(gap1 >= 0.0 && gap1 < 0.25) && gap2 >= 0.0 && gap2 < 3.75;
    return {found, start[1], px, groundY + 0.25 - 0.2, pz, dx, dz};
}

// Flat ground a metre below where the way out of `seat` starts.
float GroundBelow(C_Car *car, int seat)
{
    return static_cast<float>(ExpectedEntry(car, seat, 0.0f).startY - 1.0);
}

// The model's "VIEW<nn>" visual (as ShowSeatView looks it up), one added if it has none.
I3D_frame *ViewFrame(int seat)
{
    char name[16];
    std::snprintf(name, sizeof name, "VIEW%02d", seat);
    if (I3D_frame *f = g_model->FindChildFrame(name, ENUMF_WILDMASK | FRAME_MASK_VISUAL))
        return f;
    return AddFrame(FRAME_VISUAL, "car", name, g_model);
}

// The camera dummy a seat looked up through its own dummy should give: with its occupant out of
// the way, under the seat dummy, else under its parent.
I3D_frame *SeatDummyCamera(C_Car *car, int seat, const char *name)
{
    I3D_frame *seatFrame = car->m_vehicle.m_seats[seat].frame;
    C_Actor *occupant = car->m_vehicle.m_seats[seat].occupant;
    if (occupant)
        Unlink(occupant->GetFrame());
    I3D_frame *found = seatFrame->FindChildFrame(name, ENUMF_WILDMASK | FRAME_MASK_DUMMY);
    if (!found)
        found = seatFrame->GetParent()->FindChildFrame(name, ENUMF_WILDMASK | FRAME_MASK_DUMMY);
    if (occupant)
        LinkUnder(occupant->GetFrame(), seatFrame);
    return found;
}

bool EntryIs(const S_vector4 &pos, const S_vector4 &dir, const S_Entry &want)
{
    return Near(pos.x, want.px, 2e-3) && Near(pos.y, want.py, 1e-4) && Near(pos.z, want.pz, 2e-3) && pos.w == 1.0f &&
           Near(dir.x, want.dx, 1e-4) && dir.y == 0.0f && Near(dir.z, want.dz, 1e-4) && dir.w == 0.0f &&
           Near(dir.x * dir.x + dir.z * dir.z, 1.0, 1e-6);
}

// The direction the seat dummy faces (its world z axis), normalized here.
S_vector4 SeatFacing(C_Car *car, int seat)
{
    const S_matrix &m = car->m_vehicle.m_seats[seat].frame->GetMatrix();
    const double l = std::sqrt(double(m.m[2][0]) * m.m[2][0] + double(m.m[2][1]) * m.m[2][1] + double(m.m[2][2]) * m.m[2][2]);
    return {float(m.m[2][0] / l), float(m.m[2][1] / l), float(m.m[2][2] / l), m.m[2][3]};
}

bool Same4(const S_vector4 &a, const S_vector4 &b, float eps)
{
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps && a.w == b.w;
}

// A wall on `seat`'s side of the car (between the seat and its way out, half way along the step).
void WallBeside(C_Car *car, int seat, float groundY)
{
    const S_Entry e = ExpectedEntry(car, seat, groundY);
    const S_vector seatAt = car->m_vehicle.m_seats[seat].frame->GetWorldPos();
    // The way out from the seat: towards e (level); the wall short of it, facing the car.
    S_vector out(float(e.px - seatAt.x), 0.0f, float(e.pz - seatAt.z));
    const S_matrix &b = car->m_vehicle.m_body->GetMatrix();
    S_vector side(b.m[0][0], 0.0f, b.m[0][2]);
    side.Normalize();
    if (side.Dot(out) < 0.0f)
        side = -side;
    const S_vector at(float(e.px) - side.x * 0.3f, 0.0f, float(e.pz) - side.z * 0.3f);
    g_wall = true;
    g_wallFace.normal = -side;
    g_wallFace.level = g_wallFace.normal.Dot(at);
    g_wallFace.volume = &g_wallVolume;
}

}  // namespace

extern int g_ignoreLists;

// The seats on a freshly loaded model, the car's own frames, humans of the stub game/human.h:
// the ways out over flat ground (worked out here; a wall, too high and too low ground), seating,
// moving, leaving and getting out (the fallback to another seat's way out), the camera dummies,
// the seats' properties, the seat weapons' slots and the network's bookkeeping.
void SeatTest(const char *path)
{
    if (!LoadModel(path))
        return;
    auto *car = new C_Car();
    car->SetFrame(g_model);
    C_Vehicle &v = car->m_vehicle;
    const int n = static_cast<int>(v.m_seats.size());
    v.m_fuel = 50.0f;
    v.m_unknown610 = 1.0f;
    car->m_periodicLink = 41;
    // The scene's primary sector, the car's script, its first-person visuals.
    I3D_frame *sector = NewFrame(FRAME_SECTOR, "primary", nullptr);
    alignas(16) static uint8_t scene[0x400];
    *reinterpret_cast<I3D_frame **>(scene + 0x1F8) = sector;
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(scene);
    static int script;
    S_FrameData data = {};
    data.script = reinterpret_cast<C_Script *>(&script);
    g_model->m_data = &data;
    for (int i = 0; i < std::max(n, 2); ++i)
        ViewFrame(i);
    I3D_frame *view0 = ViewFrame(0);
    I3D_frame *view1 = ViewFrame(1);
    static int replication;
    g_netReplication = reinterpret_cast<C_NetReplication *>(&replication);
    static S_FakeNet netObject;
    car->m_netObject = reinterpret_cast<C_NetObject *>(&netObject);
    g_gameRole = GAME_ROLE_LOCAL;
    g_firstPerson = false;
    g_flatGround = true;
    g_wall = false;
    // Some seats get out behind the car (SEAT_FLAG_200; StartMission sets the flags).
    for (int i = 0; i < n; ++i)
        v.m_seats[i].flags = i % 3 == 2 ? 0x200u : 0u;

    // The ways out over flat ground a metre below where they start (the special volumes ignored
    // for one; SEAT_FLAG_200 starts behind the box in the body's space: below the seat for a body
    // turned like la_krupp's).
    bool entry = n > 0;
    for (int i = 0; i < n; ++i) {
        const float groundY = GroundBelow(car, i);
        g_groundY = groundY;
        v.m_specialVolumesOn = i == 1;
        const int ignores0 = g_ignores, clears0 = g_ignoreClears, lists0 = g_ignoreLists;
        S_vector4 pos = {}, dir = {};
        const S_Entry want = ExpectedEntry(car, i, groundY);
        entry &= want.found && car->GetSeatEntry(i, &pos, &dir) && EntryIs(pos, dir, want);
        entry &= g_ignores == ignores0 + 1 && g_ignoreClears == clears0 + 1 && g_ignoreLists == lists0 + (i == 1);
        // The ground: just clear of the first sphere (and not), within reach of the second (and not).
        const float startY = static_cast<float>(want.startY);
        for (const auto &[ground, ok] : {std::pair<float, bool>{startY + 0.74f, true}, {startY + 0.76f, false},
                                         {startY - 2.24f, true}, {startY - 2.26f, false}}) {
            g_groundY = ground;
            const int clears1 = g_ignoreClears;
            const S_Entry e = ExpectedEntry(car, i, ground);
            const bool got = car->GetSeatEntry(i, &pos, &dir);
            entry &= got == ok && e.found == ok && g_ignoreClears == clears1 + 1;
            if (ok)
                entry &= EntryIs(pos, dir, e);
        }
    }
    v.m_specialVolumesOn = false;
    S_vector4 pos = {}, dir = {};
    entry &= !car->GetSeatEntry(-1, &pos, &dir) && !car->GetSeatEntry(n, &pos, &dir);

    // A wall beside a seat: no way out there.
    bool blocked = true;
    for (int i = 0; i < n; ++i) {
        g_groundY = GroundBelow(car, i);
        WallBeside(car, i, g_groundY);
        blocked &= !car->GetSeatEntry(i, &pos, &dir);
        g_wall = false;
    }

    // Seating: the driver (the local player, a player's controller) and a passenger (an enemy).
    auto *h0 = new C_Human();
    auto *h1 = new C_Human();
    auto *h2 = new C_Human();
    C_Human *humans[3] = {h0, h1, h2};
    for (int k = 0; k < 3; ++k) {
        I3D_frame *f = NewFrame(FRAME_MODEL, "h" + std::to_string(k), nullptr);
        AddFrame(FRAME_DUMMY, "h" + std::to_string(k), "camera3RD", f);
        AddFrame(FRAME_DUMMY, "h" + std::to_string(k), "cameraFPV", f);
        humans[k]->SetFrame(f);
        humans[k]->periodicLink = 50 + k;
        humans[k]->SetNetId(20 + k);
    }
    h0->localPlayer = true;
    h0->controllerType = HUMAN_CONTROLLER_PLAYER;
    S_FakeWeapon weapons[10];
    g_linkRates.clear();
    g_uses.clear();
    v.m_unknown538 = 0x00800000;
    v.m_unknown540 = 0x00800001;
    v.m_unknown2299 = v.m_unknown229A = v.m_unknown229B = v.m_unknown229C = 1;
    v.m_unknown600 = 0;
    view0->m_frameFlags &= ~1u;
    g_firstPerson = true;
    ClearStreams();
    h0->GetFrame()->m_localMatrix(3) = S_vector(1.0f, 2.0f, 3.0f);
    h0->GetFrame()->m_frameFlags &= ~FRMFLAGS_CHANGED;
    v.m_unknownF48 = 123.0f;                            // (a slow start leaves it)
    car->SetSeatOccupant(0, h0);
    const S_vector4 facing0 = SeatFacing(car, 0);
    const S_matrix &seat0 = v.m_seats[0].frame->GetMatrix();
    bool occupy = v.m_seats[0].occupant == h0 && v.m_seats[0].load == 80.0f && car->m_driver == h0 && v.m_driven &&
                  v.m_engineOn && h0->GetFrame()->GetParent() == v.m_seats[0].frame &&
                  h0->GetFrame()->m_localMatrix(3) == S_vector(0.0f, 0.0f, 0.0f) &&
                  (h0->GetFrame()->m_frameFlags & FRMFLAGS_CHANGED) && g_dirs[h0->GetFrame()] == S_vector(0.0f, 0.0f, 1.0f) &&
                  h0->GetPosition4().x == seat0.m[3][0] && h0->GetPosition4().y == seat0.m[3][1] &&
                  h0->GetPosition4().z == seat0.m[3][2] && h0->GetPosition4().w == seat0.m[3][3] &&
                  Same4(h0->viewDir, facing0, 2e-7f) && Same4(h0->facingDir, facing0, 2e-7f) &&
                  Same4(h0->GetDirection4(), facing0, 2e-7f) && h0->viewDirs == 1 && h0->facingDirs == 1 &&
                  v.m_unknown538 == 0x00200000 && v.m_unknown540 == 0x00200001 &&
                  g_linkRates == std::vector<std::pair<int, int>>{{41, 3}, {50, 3}} && !v.m_unknown2299 &&
                  !v.m_unknown229A && !v.m_unknown229B && !v.m_unknown229C &&
                  (v.m_unknown600 & VEHICLE_PHYSICS_KEEP_AWAKE) && g_uses == std::vector<int>{1} &&
                  (view0->m_frameFlags & 1u) && CountStreams(CAR_MSG_FUEL) == 0 && v.m_unknownF48 == 123.0f &&
                  g_linkFlags[h0->GetFrame()] == 0;
    // Seated again in third person: out and in (the view off).
    g_firstPerson = false;
    car->SetSeatOccupant(0, h0);
    occupy &= v.m_seats[0].occupant == h0 && !(view0->m_frameFlags & 1u) && h0->viewDirs == 2 && car->m_driver == h0;
    g_firstPerson = true;
    // Another into the taken seat: nothing.
    g_uses.clear();
    car->SetSeatOccupant(0, h1);
    occupy &= v.m_seats[0].occupant == h0 && g_uses.empty() && h1->GetFrame()->GetParent() == nullptr;
    if (n >= 2) {
        // A passenger (an enemy: not the local player, no driver's state).
        g_linkRates.clear();
        v.m_unknown2299 = 1;
        view1->m_frameFlags &= ~1u;
        car->SetSeatOccupant(1, h1);
        occupy &= v.m_seats[1].occupant == h1 && v.m_seats[1].load == 80.0f && car->m_driver == h0 &&
                  g_linkRates.empty() && !v.m_unknown2299 && !(view1->m_frameFlags & 1u) &&
                  h1->GetFrame()->GetParent() == v.m_seats[1].frame && car->FindSeat(h1) == 1 &&
                  car->GetSeatOccupant(1) == h1;
        // The driver moves over: out of the driver's seat first (no driver, the engine off).
        v.m_seats[0].flags |= SEAT_FLAG_TAKEN;
        car->SetSeatOccupant(n >= 3 ? 2 : 1, h0);
        if (n >= 3)
            occupy &= v.m_seats[2].occupant == h0 && !v.m_seats[0].occupant && !car->m_driver && !v.m_driven &&
                      !v.m_engineOn && v.m_seats[0].load == 0.0f && !(v.m_seats[0].flags & SEAT_FLAG_TAKEN) &&
                      car->FindSeat(h0) == 2;
        else
            occupy &= v.m_seats[0].occupant == h0;     // (seat 1 taken: nothing)
        // An enemy at the wheel: 0x800000 set.
        if (n >= 3) {
            car->SetSeatOccupant(0, h2);
            occupy &= car->m_driver == h2 && (v.m_unknown538 & 0x00A00000) == 0x00A00000 &&
                      (v.m_unknown540 & 0x00A00000) == 0x00A00000;
            // A squad member at the wheel: 0x800000 cleared (the passenger moves over).
            car->LeaveSeat(0);
            h1->controllerType = HUMAN_CONTROLLER_SQUAD;
            car->SetSeatOccupant(0, h1);
            occupy &= car->m_driver == h1 && !v.m_seats[1].occupant && (v.m_unknown538 & 0x00A00000) == 0x00200000 &&
                      (v.m_unknown540 & 0x00A00000) == 0x00200000;
            h1->controllerType = HUMAN_CONTROLLER_ENEMY;
        }
    }
    occupy &= n == 0 || (!car->GetSeatOccupant(-1) && !car->GetSeatOccupant(n));

    // Leaving: back into the primary sector where it is, the camera put back, unblocked.
    bool leave = true;
    if (n >= 1) {
        g_restored.clear();
        int keeper = 0;
        const int seat = car->FindSeat(h0);
        v.m_seats[seat].camera = reinterpret_cast<C_CameraFrameKeeper *>(&keeper);
        v.m_seats[seat].flags |= SEAT_FLAG_TAKEN;
        v.m_unknown600 = VEHICLE_PHYSICS_KEEP_AWAKE;
        if (seat < 10)
            car->m_seatWeapons[seat] = AsWeapon(weapons[seat]);
        weapons[seat].gunner = h1;
        weapons[seat].views = 0;
        g_firstPerson = false;
        leave &= car->LeaveSeat(seat) == h0 && !v.m_seats[seat].occupant && v.m_seats[seat].load == 0.0f &&
                 h0->GetFrame()->GetParent() == sector && g_linkFlags[h0->GetFrame()] == I3DLINK_KEEP_WORLD &&
                 g_restored.size() == 1 &&
                 g_restored[0] == reinterpret_cast<C_CameraFrameKeeper *>(&keeper) &&
                 !(v.m_seats[seat].flags & SEAT_FLAG_TAKEN) && !weapons[seat].gunner && weapons[seat].views == 1 &&
                 !weapons[seat].view;
        // Others still in: the vehicle not kept awake; the last one out: kept awake.
        const bool others = n >= 2 && car->GetSeatOccupant(seat == 0 ? 1 : 0);
        leave &= ((v.m_unknown600 & VEHICLE_PHYSICS_KEEP_AWAKE) != 0) == !others;
        v.m_seats[seat].camera = nullptr;
        car->m_seatWeapons[seat] = nullptr;
        leave &= car->LeaveSeat(seat) == nullptr && car->LeaveSeat(-1) == nullptr && car->LeaveSeat(n) == nullptr;
        // The driver's seat: no driver, the engine off, 0x004f2630 back to the top speed, no handbrake.
        if (car->GetSeatOccupant(0)) {
            C_Actor *driver = car->GetSeatOccupant(0);
            v.m_handbrake = 1.0f;
            v.m_topSpeed = 30.0f;
            leave &= car->LeaveSeat(0) == driver && !car->m_driver && !v.m_driven && !v.m_engineOn &&
                     v.m_unknown790 == 30.5f && v.m_handbrake == 0.0f;
        }
        for (int i = 0; i < n; ++i)
            car->LeaveSeat(i);
        leave &= (v.m_unknown600 & VEHICLE_PHYSICS_KEEP_AWAKE) != 0;
    }

    // Getting out: at the seat's way out (else another seat's), the driver's state cleared, the
    // controls let go for the local player; no room: stays in.
    bool exited = true;
    for (int pass = 0; pass <= n && n > 0; ++pass) {
        // (Each seat, then the driver's again with a dead driver.)
        const int i = pass < n ? pass : 0;
        g_groundY = GroundBelow(car, i);
        car->SetSeatOccupant(i, h0);
        g_linkRates.clear();
        g_uses.clear();
        h0->health = pass == n ? 0 : 800;
        g_firstPerson = true;                           // (seated: the view on)
        const S_Entry want = ExpectedEntry(car, i, g_groundY);
        exited &= car->ExitSeat(i) && !v.m_seats[i].occupant && v.m_seats[i].load == 0.0f &&
                  h0->GetFrame()->GetParent() == sector && g_linkFlags[h0->GetFrame()] == 0 &&
                  EntryIs(h0->GetPosition4(), h0->GetDirection4(), want) && !(ViewFrame(i)->m_frameFlags & 1u) &&
                  h0->GetFrame()->m_localMatrix(3) == S_vector(h0->GetPosition4().x, h0->GetPosition4().y, h0->GetPosition4().z) &&
                  g_dirs[h0->GetFrame()] == S_vector(h0->GetDirection4().x, 0.0f, h0->GetDirection4().z) &&
                  g_uses == std::vector<int>{0};
        if (i == 0)
            exited &= !car->m_driver && !v.m_driven &&
                      g_linkRates == std::vector<std::pair<int, int>>{{41, 4}, {50, pass == n ? 1 : 0}};
        else
            exited &= g_linkRates.empty();
    }
    exited &= !car->ExitSeat(0) && !car->ExitSeat(-1);    // (empty; out of range)
    if (n >= 2) {
        // A local passenger getting out lets the driver's controls go (SetInput(0, 0)); another's
        // does not.
        h1->health = 800;
        car->SetSeatOccupant(0, h1);
        for (C_Human *passenger : {h0, h2}) {
            g_groundY = GroundBelow(car, 1);
            car->SetSeatOccupant(1, passenger);
            v.m_steering = 0.3f;
            v.m_throttle = 0.4f;
            v.m_engineOn = true;
            v.m_unknown550 = 0;
            v.m_automatic = false;
            car->ExitSeat(1);
            exited &= (v.m_steering == 0.0f && v.m_throttle == 0.0f) == (passenger == h0);
        }
        car->LeaveSeat(0);
    }
    if (n >= 2) {
        // The own way out walled off: another seat's (the first open one).
        const int seat = 0;
        g_groundY = GroundBelow(car, seat);
        car->SetSeatOccupant(seat, h0);
        WallBeside(car, seat, g_groundY);
        int open = -1;
        for (int i = 0; i < n && open < 0; ++i)
            if (i != seat && car->GetSeatEntry(i, &pos, &dir))
                open = i;
        S_vector4 openPos = {}, openDir = {};
        if (open >= 0)
            car->GetSeatEntry(open, &openPos, &openDir);
        const bool out = car->ExitSeat(seat);
        exited &= out == (open >= 0);
        if (out)
            exited &= h0->GetPosition4().x == openPos.x && h0->GetPosition4().y == openPos.y &&
                      h0->GetPosition4().z == openPos.z && h0->GetDirection4().x == openDir.x &&
                      h0->GetDirection4().z == openDir.z;
        // No ground anywhere: stays in.
        g_wall = false;
        if (!out)
            car->LeaveSeat(seat);
        car->SetSeatOccupant(seat, h0);
        g_groundY = -1000.0f;
        exited &= !car->ExitSeat(seat) && v.m_seats[seat].occupant == h0;
        car->LeaveSeat(seat);
    }
    h0->health = 800;

    // The camera dummies: a passenger's own (by the view); the driver's, a hidden seat's and a
    // gunner's under the seat dummy (else its parent).
    bool camera = true;
    if (n >= 2) {
        I3D_frame *seat0Camera = AddFrame(FRAME_DUMMY, "car", "camera3RD_seat0", v.m_seats[0].frame);
        AddFrame(FRAME_DUMMY, "car", "cameraFPV_body", v.m_seats[0].frame->GetParent());
        car->SetSeatOccupant(0, h0);
        car->SetSeatOccupant(1, h1);
        I3D_frame *h0Fpv = h0->GetFrame()->FindChildFrame("cameraFPV", FRAME_MASK_DUMMY);
        I3D_frame *h1Fpv = h1->GetFrame()->FindChildFrame("cameraFPV", FRAME_MASK_DUMMY);
        I3D_frame *h1Third = h1->GetFrame()->FindChildFrame("camera3RD", FRAME_MASK_DUMMY);
        const bool firstOnly0 = std::strstr(v.m_seats[0].frame->GetUserProps(), "NO3RD") != nullptr;
        // The driver's: the seat's dummy, the occupant out of the way while it is looked for.
        const int unlinked0 = g_unlinked;
        g_firstPerson = false;
        I3D_frame *want = SeatDummyCamera(car, 0, firstOnly0 ? "cameraFPV*" : "camera3RD*");
        camera &= want && car->GetSeatCamera(0) == want && h0->GetFrame()->GetParent() == v.m_seats[0].frame &&
                  g_unlinked == unlinked0 + 1 && (firstOnly0 || seat0Camera);
        g_firstPerson = true;
        want = SeatDummyCamera(car, 0, "cameraFPV*");
        camera &= want && want != h0Fpv && car->GetSeatCamera(0) == want && h0->GetFrame()->GetParent() == v.m_seats[0].frame;
        // A passenger: its own, by the view.
        camera &= car->GetSeatCamera(1) == h1Fpv;
        g_firstPerson = false;
        const bool firstOnly1 = std::strstr(v.m_seats[1].frame->GetUserProps(), "NO3RD") != nullptr;
        camera &= car->GetSeatCamera(1) == (firstOnly1 ? h1Fpv : h1Third);
        // "NO3RD": first person only.
        Stub(v.m_seats[1].frame).props += " NO3RD";
        v.m_seats[1].frame->m_userProps = Stub(v.m_seats[1].frame).props.c_str();
        camera &= car->GetSeatCamera(1) == h1Fpv && car->IsSeatFirstPersonOnly(1) &&
                  car->IsSeatFirstPersonOnly(0) == firstOnly0;
        // Hidden and armed passengers: the seat's dummy.
        v.m_seats[1].flags |= SEAT_FLAG_HIDDEN;
        want = SeatDummyCamera(car, 1, "cameraFPV*");
        camera &= car->GetSeatCamera(1) == want && want != h1Fpv && h1->GetFrame()->GetParent() == v.m_seats[1].frame;
        v.m_seats[1].flags &= ~SEAT_FLAG_HIDDEN;
        car->m_seatWeapons[1] = AsWeapon(weapons[1]);
        camera &= car->GetSeatCamera(1) == want;
        car->m_seatWeapons[1] = nullptr;
        car->LeaveSeat(1);
        camera &= car->GetSeatCamera(1) == nullptr && car->GetSeatCamera(-1) == nullptr && car->GetSeatCamera(n) == nullptr;
        car->LeaveSeat(0);
    }

    // The seats' properties.
    bool props = n == 0 || (car->SetSeatBlocked(0, true) && car->IsSeatBlocked(0) &&
                            v.m_seats[0].flags == (0x400u | (0 % 3 == 2 ? 0x200u : 0u)) && !car->SetSeatBlocked(0, false) &&
                            !car->IsSeatBlocked(0) && !car->SetSeatBlocked(-1, true) && !car->SetSeatBlocked(n, true) &&
                            !car->IsSeatBlocked(-1) && !car->IsSeatBlocked(n));
    for (int i = 0; i < n; ++i) {
        v.m_seats[i].text = 900 + i;
        props &= car->GetSeatFlags(i) == v.m_seats[i].flags && car->GetSeatText(i) == 900 + i;
    }
    props &= car->GetSeatFlags(-1) == 0 && car->GetSeatFlags(n) == 0 && car->GetSeatText(-1) == 0 &&
             car->GetSeatText(n) == 0 && !car->IsSeatFirstPersonOnly(-1) && !car->IsSeatFirstPersonOnly(n);
    // The VIEW visuals without a seat weapon.
    car->ShowSeatView(1, true);
    props &= (view1->m_frameFlags & 1u) != 0;
    car->ShowSeatView(1, false);
    props &= (view1->m_frameFlags & 1u) == 0;

    // The seat weapons' slots: forwarded with one, the defaults without.
    bool slots = true;
    for (int s = 0; s < 10; ++s) {
        S_FakeWeapon &w = weapons[s];
        w = S_FakeWeapon();
        w.rounds = 30 + s;
        w.def = reinterpret_cast<C_ItemDef *>(&weapons[(s + 1) % 10]);
        w.text = 700 + s;
        w.aim = S_vector(1.0f + s, 2.0f, 3.0f);
        w.origin = S_vector(4.0f, 5.0f + s, 6.0f);
        w.limits[0] = 0.1f * s, w.limits[1] = 0.2f, w.limits[2] = 0.3f, w.limits[3] = 0.4f + s;
        w.aimable = s % 2 == 0;
        w.c8 = s % 2 == 1;
        car->m_seatWeapons[s] = (s % 3 != 1) ? AsWeapon(w) : nullptr;
        const bool has = car->m_seatWeapons[s] != nullptr;
        float a = -1.0f, b = -1.0f, c = -1.0f, d = -1.0f, axes[4], buttons[4], aimAt[3];
        int x = 0, y = 0, z = 0;
        car->GetSeatWeaponLimits(s, &a, &b, &c, &d);
        car->ApplySeatControls(s, axes, buttons);
        car->ShowSeatView(s, true);
        const bool canAim = car->CanSeatWeaponAim(s, aimAt);
        const bool c8 = car->UnknownC8(s, &x, &y, &z);
        if (has)
            slots &= car->HasSeatWeapon(s) && car->GetSeatWeaponRounds(s) == 30 + s && car->GetSeatWeaponDef(s) == w.def &&
                     car->GetSeatWeaponText(s) == 700 + s && car->GetSeatWeaponAim(s) == w.aim &&
                     car->GetSeatWeaponOrigin(s) == w.origin && car->UnknownAC(s) == 0.0f && a == w.limits[0] &&
                     b == 0.2f && c == 0.3f && d == w.limits[3] && w.axes == axes && w.buttons == buttons &&
                     w.controls == 1 && w.view && w.views == 1 && canAim == w.aimable && w.aimPos == aimAt &&
                     c8 == w.c8 && w.c8Args[0] == &x && w.c8Args[1] == &y && w.c8Args[2] == &z;
        else
            slots &= !car->HasSeatWeapon(s) && car->GetSeatWeaponRounds(s) == 0 && !car->GetSeatWeaponDef(s) &&
                     car->GetSeatWeaponText(s) == -1 && car->GetSeatWeaponAim(s) == S_vector(0.0f, 0.0f, 0.0f) &&
                     car->GetSeatWeaponOrigin(s) == S_vector(0.0f, 0.0f, 0.0f) && car->UnknownAC(s) == 3.14159265f &&
                     a == -1.0f && d == -1.0f && w.controls == 0 && w.views == 0 && !canAim && !c8;
    }
    slots &= !car->HasSeatWeapon(10) && !car->HasSeatWeapon(-1) && !car->GetSeatWeaponDef(10) &&
             car->GetSeatWeaponRounds(-1) == 0;
    for (C_SeatWeapon *&w : car->m_seatWeapons)
        w = nullptr;

    // The network's bookkeeping: the server tells the rounds of a seat weapon taken; who left a
    // seat and when; a client's local driver getting out sends the state.
    bool net = true;
    if (n >= 1) {
        int ids[10], times[10];
        std::fill(std::begin(ids), std::end(ids), -1);
        std::fill(std::begin(times), std::end(times), 0);
        g_gameRole = GAME_ROLE_SERVER;
        g_queued.clear();
        ClearStreams();
        g_linkRates.clear();
        // The weapon in the last seat (the driver's when there is one only).
        const int ws = n - 1;
        weapons[ws] = S_FakeWeapon();
        weapons[ws].rounds = 77;
        car->m_seatWeapons[ws] = AsWeapon(weapons[ws]);
        h0->m_netId = 20;
        v.m_unknown600 = 0;
        v.m_engineOn = false;
        v.m_unknownF48 = 123.0f;
        car->SetSeatOccupant(ws, h0);
        net &= CountStreams(CAR_MSG_FUEL) == 1 && g_queued.size() == 1 && g_queued[0].type == 2 && g_queued[0].rounds == 77 &&
               g_queued[0].seat == ws && g_queued[0].sender == 6 && g_queued[0].reliable && g_queued[0].fromClient &&
               weapons[ws].gunner == h0 && g_linkRates.empty() && !(v.m_unknown600 & VEHICLE_PHYSICS_KEEP_AWAKE);
        // The server starts the engine at once (a client the slow way).
        if (ws != 0)
            car->SetSeatOccupant(0, h1);
        net &= v.m_engineOn && v.m_unknownF48 == 0.0f && g_queued.size() == 1 && CountStreams(CAR_MSG_FUEL) == 1 + (ws != 0) &&
               g_linkRates.empty();
        if (ws != 0)
            car->LeaveSeat(0);
        net &= car->LeaveSeat(ws) == h0;
        car->m_seatWeapons[ws] = nullptr;
        // (Without the mission's arrays: nothing remembered.)
        car->m_seatLeftIds = ids;
        car->m_seatLeftTimes = times;
        car->SetSeatOccupant(0, h0);
        const int64_t now = GetPerfTimeMs();
        net &= car->LeaveSeat(0) == h0 && ids[0] == 20 && times[0] >= now && times[0] <= now + 5 &&
               !(v.m_unknown600 & VEHICLE_PHYSICS_KEEP_AWAKE);
        // A client: the local player's net id getting out of the driver's seat sends the state.
        g_gameRole = GAME_ROLE_CLIENT;
        g_localPlayerSlot = 20;
        g_groundY = GroundBelow(car, 0);
        car->SetSeatOccupant(0, h0);
        ClearStreams();
        ids[0] = -1;
        net &= car->ExitSeat(0) && CountStreams(CAR_MSG_FUEL) == 1 && ids[0] == 20;
        // Another's driver getting out: no state.
        car->SetSeatOccupant(0, h1);
        ClearStreams();
        net &= car->ExitSeat(0) && CountStreams(CAR_MSG_FUEL) == 0 && ids[0] == 21;
        if (n >= 2) {
            // Not the driver's seat: no state from the local player; no controls let go on a client
            // (were they, the driver's own machine would apply them: h1 the local slot).
            car->SetSeatOccupant(1, h0);
            ClearStreams();
            net &= car->ExitSeat(1) && CountStreams(CAR_MSG_FUEL) == 0 && ids[1] == 20;
            g_localPlayerSlot = 21;
            car->SetSeatOccupant(0, h1);
            car->SetSeatOccupant(1, h0);
            v.m_steering = 0.3f;
            net &= car->ExitSeat(1) && v.m_steering == 0.3f;
            car->LeaveSeat(0);
        }
        car->m_seatLeftIds = nullptr;
        car->m_seatLeftTimes = nullptr;
        g_gameRole = GAME_ROLE_LOCAL;
        g_localPlayerSlot = 0;
    }

    std::printf("seats: %d entry %d blocked %d occupy %d leave %d exit %d camera %d props %d slots %d net %d\n", n, entry,
                blocked, occupy, leave, exited, camera, props, slots, net);
    g_model->m_data = nullptr;
    g_flatGround = false;
    g_wall = false;
    delete h0;
    delete h1;
    delete h2;
    delete car;
}

// --- the car's controls and updates (src/game/car_update.cpp, car_marks.cpp; part 2c) -------------

// The game profile, the multiplayer settings (the stub game/profile.h).
C_GameProfile g_gameProfile;
bool g_coop;
bool C_GameProfile::IsCooperativeGame() const
{
    return g_coop;
}
S_MultiplayerSettings g_mpSettingsValue = {MP_GAME_DEATHMATCH, 0};
S_MultiplayerSettings *g_mpSettings = &g_mpSettingsValue;

// The actor messages: what each wrote.
struct S_FakeStream {
    int id;
    bool ex;
    std::vector<std::pair<uint32_t, int>> bits;
    bool sent = false;
    bool reliable = false;                              // (CreateActorMessage's)
};
std::vector<S_FakeStream *> g_streams;
bool g_reliableActorStreams;                            // (part 6b: reliable messages get a stream too)
C_BitStream *CreateActorMessage(int id, bool reliable)
{
    g_streams.push_back(new S_FakeStream{id, false, {}, false});
    g_streams.back()->reliable = reliable;
    return reliable && !g_reliableActorStreams ? nullptr : reinterpret_cast<C_BitStream *>(g_streams.back());
}
C_BitStream *CreateActorMessageEx(int id, bool reliable)
{
    g_streams.push_back(new S_FakeStream{id, true, {}, false});
    return reliable ? nullptr : reinterpret_cast<C_BitStream *>(g_streams.back());
}
void SendActorMessage(C_BitStream *msg)
{
    reinterpret_cast<S_FakeStream *>(msg)->sent = true;
}
void C_BitStream::WriteBits(uint32_t value, int bitCount)
{
    reinterpret_cast<S_FakeStream *>(this)->bits.emplace_back(value, bitCount);
}
void ClearStreams()
{
    for (S_FakeStream *s : g_streams)
        delete s;
    g_streams.clear();
}
int CountStreams(uint32_t kind)
{
    int count = 0;
    for (const S_FakeStream *s : g_streams)
        count += !s->bits.empty() && s->bits[0] == std::pair<uint32_t, int>(kind, CAR_MSG_KIND_BITS);
    return count;
}

// The items: what was made and stored.
C_ItemsModule g_itemsModule;
struct S_Created {
    int typeId, container;
    C_Actor *owner;
    int uid;
    C_Item *item;
};
std::vector<S_Created> g_created;
C_Item::~C_Item() {}
void C_Item::Init(int) {}
// (Part 6a's item actors: what every item weighs and its size.)
float g_itemWeight = 0.0f;
int g_itemSize = 0;
float C_Item::GetWeight()
{
    return g_itemWeight;
}
int C_Item::GetSize()
{
    return g_itemSize;
}
float C_Item::GetSpace()
{
    return 0.0f;
}
bool C_Item::SaveGame(C_chunk &)
{
    return true;
}
bool C_Item::LoadGame(C_chunk &)
{
    return true;
}
class C_FakeItem : public C_Item {
public:
    C_FakeItem() : C_Item(static_cast<E_ItemKind>(0)) {}
    int Fire(C_Actor *, const S_vector &, const S_vector &, uint32_t) override { return 0; }
    int FireSecondary(C_Actor *, const S_vector &, const S_vector &, uint8_t) override { return 0; }
};
// The item types' table (the car's LoadGame asks for the charge's): one fake definition, each ask
// logged (C_ItemDef::GetModel gives g_itemModel).
std::vector<int> g_itemDefAsks;
C_ItemDef *C_ItemsModule::GetItemDef(int typeId) const
{
    alignas(16) static uint8_t def[sizeof(C_ItemDef)];
    g_itemDefAsks.push_back(typeId);
    return reinterpret_cast<C_ItemDef *>(def);
}
C_Item *C_ItemsModule::CreateItem(int typeId, int container, C_Actor *owner, int uid)
{
    C_Item *item = new C_FakeItem();
    item->m_typeId = typeId;
    item->m_uid = 900 + static_cast<int>(g_created.size());
    g_created.push_back({typeId, container, owner, uid, item});
    return item;
}
C_ItemContainer::C_ItemContainer(int kind) : m_kind(kind) {}
class C_FakeStore : public C_ItemContainer {
public:
    C_FakeStore() : C_ItemContainer(0) {}
    float GetTotalWeight() override { return 0.0f; }
    float GetTotalSpace() override { return 0.0f; }
    int CountItems(int typeId) override
    {
        counted.push_back(typeId);
        return count;
    }
    int GetUsedSlotCount() override { return static_cast<int>(slots.size()); }
    int AddItem(C_Item *item, C_Item **replaced, int index) override
    {
        added.push_back(item);
        addIndices.push_back(index);
        return replaced ? 0 : -1;
    }
    int RemoveItem(C_Item *, int) override { return 0; }
    void Clear() override {}
    bool IsEmpty() override { return true; }
    std::vector<C_Item *> *GetSlots() override { return slots.empty() ? nullptr : slots.data(); }
    C_Item *GetItem() override { return nullptr; }
    int GetSlotCount() override { return 0; }

    std::vector<std::vector<C_Item *>> slots;           // (the store's contents: WriteStore)
    int count = 0;
    std::vector<int> counted, addIndices;
    std::vector<C_Item *> added;
};
C_ItemDef::C_ItemDef(E_ItemRecordType recordType, int id) : m_recordType(recordType), m_id(id) {}
C_ItemDef::~C_ItemDef() {}
void C_ItemDef::Save(int *, void *) {}
void C_ItemDef::Load(int *, const void *) {}
class C_FakeDef : public C_ItemDef {
public:
    explicit C_FakeDef(int ammo) : C_ItemDef(static_cast<E_ItemRecordType>(0), 0), ammo(ammo) {}
    int GetCarryMode() override { return 0; }
    int GetBehaviour() override { return 0; }
    int GetAmmoItemId() override { return ammo; }
    int ammo;
};

// The sound module: the requests.
alignas(16) uint8_t g_soundModuleMemory[0x200];
std::vector<S_SoundRequest> g_soundRequests;
C_SoundModule *GetSoundModule()
{
    return reinterpret_cast<C_SoundModule *>(g_soundModuleMemory);
}
std::vector<std::string> g_soundFiles;                  // (part 6b: each request's file name, "" for none)
int g_soundSlot;                                        // (part 7c: the slot Play returns)
int g_soundSlotStep;                                    // (part 7d: added to it after each Play)
int C_SoundModule::Play(const S_SoundRequest &request)
{
    g_soundRequests.push_back(request);
    g_soundFiles.push_back(request.fileName ? request.fileName : "");
    const int slot = g_soundSlot;
    g_soundSlot += g_soundSlotStep;
    return slot;
}

// The water: its top (-FLT_MAX none) and whether the upward ray meets it.
float g_waterTop = -FLT_MAX;
bool g_waterAbove;
S_CollisionVolume g_waterVolume;
std::vector<std::pair<S_vector, S_vector>> g_waterRays;
void C_CollisionWorld::GetWaterHeightRange(float &minY, float &maxY) const
{
    minY = g_waterTop - 3.0f;
    maxY = g_waterTop;
}
S_CollisionVolume *C_CollisionWorld::RayTestWater(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                                  S_vector *hitNormal, uint32_t excludeFlags) const
{
    g_waterRays.emplace_back(from, dir);
    if (hitPos || hitNormal || excludeFlags)
        return nullptr;                                 // (the car passes none)
    return g_waterAbove ? &g_waterVolume : nullptr;
}

// The checkpoint network (faked over a list of points): FindNearest the nearest within the
// distance, GetNeighbours those within the radius of a point (itself first).
S_Checkpoint::~S_Checkpoint() {}
struct S_FindCall {
    S_vector pos;
    float maxDist, minDist;
};
std::vector<S_FindCall> g_finds;
std::vector<std::pair<int, float>> g_neighbourCalls;
S_Checkpoint *C_CheckpointNetwork::Get(int index)
{
    return index >= 0 && index < m_pointCount ? &m_points[index] : nullptr;
}
int C_CheckpointNetwork::FindNearest(S_vector pos, float maxDist, float minDist) const
{
    g_finds.push_back({pos, maxDist, minDist});
    int best = CHECKPOINT_NONE;
    float bestSq = maxDist * maxDist;
    for (int i = 0; i < m_pointCount; ++i) {
        const float sq = (m_points[i].pos - pos).Square();
        if (sq < bestSq) {
            bestSq = sq;
            best = i;
        }
    }
    return best;
}
bool C_CheckpointNetwork::GetNeighbours(int from, float radius, std::vector<uint16_t> *out)
{
    g_neighbourCalls.emplace_back(from, radius);
    const S_Checkpoint *start = Get(from);
    if (!start)
        return false;
    out->push_back(static_cast<uint16_t>(from));
    for (int i = 0; i < m_pointCount; ++i) {
        if (i != from && (m_points[i].pos - start->pos).Square() <= radius * radius)
            out->push_back(static_cast<uint16_t>(i));
    }
    return true;
}
float SegmentDistance2(const S_vector &p, const S_vector &a, const S_vector &b, float *t)   // (as game/ai_movement.cpp)
{
    const S_vector ab = b - a;
    *t = 0.0f;
    const float length2 = ab.Square();
    if (length2 < 1e-8f)
        return (p - a).Square();
    const float s = (p - a).Dot(ab) / length2;
    if (s <= 0.0f) {
        *t = 0.0f;
        return (p - a).Square();
    }
    if (s >= 1.0f) {
        *t = 1.0f;
        return (p - b).Square();
    }
    *t = s;
    return ((p - a) - ab * s).Square();
}

// The camera keepers' updates.
std::vector<std::pair<const C_CameraFrameKeeper *, int>> g_keeperUpdates;
void C_CameraFrameKeeper::Update(int time)
{
    g_keeperUpdates.emplace_back(this, time);
}

#ifndef REAL_SEAT_WEAPON
void C_SeatWeaponAmmoMessage::Write(C_BitStream *) {}
void C_SeatWeaponAmmoMessage::Read(C_BitStream *) {}
#endif


namespace {

// The vehicle's control fields (what the setters write).
struct S_Controls {
    float throttle, u700, brake, steering, handbrake, u710, u724, u908, u790;
    uint8_t u22C7;
    uint32_t u600;
    bool operator==(const S_Controls &o) const
    {
        return std::memcmp(this, &o, sizeof *this) == 0;
    }
};
S_Controls Controls(const C_Vehicle &v)
{
    S_Controls c;
    std::memset(&c, 0, sizeof c);
    c.throttle = v.m_throttle;
    c.u700 = v.m_unknown700;
    c.brake = v.m_brake;
    c.steering = v.m_steering;
    c.handbrake = v.m_handbrake;
    c.u710 = v.m_unknown710;
    c.u724 = v.m_unknown724;
    c.u908 = v.m_unknown908;
    c.u790 = v.m_unknown790;
    c.u22C7 = v.m_unknown22C7;
    c.u600 = v.m_unknown600;
    return c;
}
void SetControls(C_Vehicle &v, const S_Controls &c)
{
    v.m_throttle = c.throttle;
    v.m_unknown700 = c.u700;
    v.m_brake = c.brake;
    v.m_steering = c.steering;
    v.m_handbrake = c.handbrake;
    v.m_unknown710 = c.u710;
    v.m_unknown724 = c.u724;
    v.m_unknown908 = c.u908;
    v.m_unknown790 = c.u790;
    v.m_unknown22C7 = c.u22C7;
    v.m_unknown600 = c.u600;
}

// The restrictions as worked out here.
void ExpectedRestrict(uint32_t r, float &throttle, float &steering)
{
    if (r & 4)
        throttle = 0.0f;
    switch (r & 3) {
    case 3:
        throttle = steering = 0.0f;
        break;
    case 1:
        steering = steering < 0.0f ? 0.0f : steering;
        if (throttle != 0.0f && steering == 0.0f) {
            steering = -1.0f;
            throttle = throttle * 0.3f;
        }
        break;
    case 2:
        steering = steering > 0.0f ? 0.0f : steering;
        if (throttle != 0.0f && steering == 0.0f) {
            steering = 1.0f;
            throttle = throttle * 0.3f;
        }
        break;
    default:
        break;
    }
}

// IsDrivable as worked out here.
bool ExpectedDrivable(const C_Vehicle &v, uint32_t checks)
{
    if (checks & 1) {
        if (!(v.m_unknown610 > 0.0f) && !std::isnan(v.m_unknown610))
            return false;
        if (!(v.m_unknown63C > 0.0f) && !std::isnan(v.m_unknown63C))
            return false;
        if (!(v.m_fuel > 0.0f) && !std::isnan(v.m_fuel))
            return false;
        for (int i = 0; i < v.m_wheelCount; ++i)
            if (v.m_wheels[i] && (v.m_wheels[i]->flags & WHEEL_DETACHED))
                return false;
    }
    if ((checks & 2) && v.m_speed < 1.0f) {
        int n = 0;
        for (int i = 0; i < v.m_wheelCount; ++i)
            n += v.m_wheels[i] && (v.m_wheels[i]->flags & WHEEL_ON_GROUND);
        if (n < 2 || v.m_modelAxisY.y < 0.6f)
            return false;
    }
    if (checks & 4) {
        bool sides[2] = {false, false};
        for (int i = 0; i < v.m_wheelCount; ++i) {
            const S_VehicleWheel *w = v.m_wheels[i];
            if (w && (w->flags & WHEEL_ON_GROUND) && !(w->flags & WHEEL_DETACHED))
                sides[i % 2] = true;
        }
        if (!sides[0] || !sides[1])
            return false;
    }
    return true;
}

// UpdateDetail's options as worked out here.
uint32_t ExpectedDetail(uint32_t before, bool full, uint32_t role, float d2, bool shown)
{
    uint32_t o = before & 0x7FFC2026u;
    if (full)
        return o | 0x800313D1u;
    if ((role & 7) != 7) {
        if (!(role & 2))
            return o | 0x8000CC10u;
        if (role & 1)
            o |= 0x8000CC10u;
    }
    if (d2 < 900.0f)
        o |= 0x41;
    if (shown || d2 < 900.0f)
        o |= 0x200;
    if (shown || d2 < 600.0f)
        o |= 0x1100;
    if (d2 < 22500.0f)
        o |= 0x80;
    if (shown || d2 < 400.0f)
        o |= 0xC000;
    o |= shown && d2 < 600.0f ? 0x800u : 0x20000u;
    o |= 0x10;
    if (d2 < 900.0f)
        o |= 0x400;
    return o | 0x80000000u;
}

I3D_frame *MakeSound(float volume, bool on)
{
    I3D_frame *f = MakeFrame(g_soundVtable, FRAME_SOUND, false);
    *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(f) + 0x16C) = volume;
    f->m_frameFlags = on ? f->m_frameFlags | 1u : f->m_frameFlags & ~1u;
    return f;
}

}  // namespace

// The controls, the car's state, the water, the detail, Update and Tick, the checkpoint marks, the
// timer and the messages on a freshly loaded and started model.
// A check of CarUpdateTest (CAR_TRACE=1: the failing lines).
void Expect(bool &flag, bool ok, int line)
{
    if (!ok && std::getenv("CAR_TRACE"))
        std::printf("  car update check failed: line %d\n", line);
    flag = flag && ok;
}

// The height of a flat ground under the wheels at rest (as the drive test's; 1 m under the model
// without wheels).
float GroundUnderWheels(const C_Vehicle &v)
{
    int grounded = 0;
    for (int i = 0; i < v.m_wheelCount; ++i)
        grounded += v.m_wheels[i] && !(v.m_wheels[i]->flags & WHEEL_DUMMY);
    float groundY = g_model->GetWorldPos().y - 1.0f;
    for (int i = 0, first = 1; i < v.m_wheelCount; ++i) {
        const S_VehicleWheel *w = v.m_wheels[i];
        if (!w || (w->flags & WHEEL_DUMMY))
            continue;
        const float hubY = (S_vector(w->modelPos.x, w->modelPos.y, w->modelPos.z) * g_model->GetMatrix()).y;
        const float rest = std::clamp(w->unknown164 + v.m_weight.y / grounded / w->unknown160, 0.0f, w->unknown164);
        groundY = first ? hubY - w->radius - rest : std::min(groundY, hubY - w->radius - rest);
        first = 0;
    }
    return groundY;
}

void CarUpdateTest(const char *path)
{
    if (!LoadModel(path))
        return;
    auto *car = new C_Car();
    car->SetFrame(g_model);
    C_Vehicle &v = car->m_vehicle;
    v.Start();
    const int n = static_cast<int>(v.m_seats.size());
    // The scene: its primary sector, the active camera, the render stamp.
    alignas(16) static uint8_t scene[0x400];
    std::memset(scene, 0, sizeof scene);
    I3D_frame *camera = NewFrame(FRAME_CAMERA, "camera", nullptr);
    *reinterpret_cast<I3D_frame **>(scene + 0x15C) = camera;
    *reinterpret_cast<I3D_frame **>(scene + 0x1F8) = NewFrame(FRAME_SECTOR, "primary", nullptr);
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(scene);
    int &sceneStamp = *reinterpret_cast<int *>(scene + 0x11C);
    int &carStamp = *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(g_model) + 0x11C);
    g_gameRole = GAME_ROLE_LOCAL;
    g_coop = false;
    g_mpSettingsValue.gameType = MP_GAME_DEATHMATCH;
    static int replication;
    static S_FakeNet netObject;                         // (the restart, part 2e, sets its place)
    g_netReplication = reinterpret_cast<C_NetReplication *>(&replication);
    car->m_netObject = reinterpret_cast<C_NetObject *>(&netObject);
    car->SetNetId(77);
    // An empty checkpoint network (the marks' test fills it).
    alignas(16) static uint8_t networkMemory[sizeof(C_CheckpointNetwork)];
    std::memset(networkMemory, 0, sizeof networkMemory);
    C_CheckpointNetwork &network = *reinterpret_cast<C_CheckpointNetwork *>(networkMemory);
    GetGame()->m_checkpoints = reinterpret_cast<C_Checkpoints *>(&network);

    // --- the controls: each restriction set with each input, against the setters given the
    // restricted values here.
    auto *driver = new C_Human();
    driver->SetFrame(NewFrame(FRAME_MODEL, "driver", nullptr));
    driver->localPlayer = true;
    driver->SetNetId(30);
    car->m_driver = driver;
    v.m_engineOn = true;
    v.m_unknown790 = 123.0f;
    v.m_unknown934 = 0.6f;                              // (0 here: the steering would always come out 0)
    bool controls = true;
    const float inputs[][4] = {{0.8f, 0.0f, 0.0f, 0.6f}, {0.8f, 0.0f, 0.7f, 0.0f}, {0.8f, 0.0f, 0.0f, 0.0f},
                               {0.0f, 0.0f, 0.5f, 0.0f}, {0.0f, 0.4f, 0.0f, 0.35f}, {0.6f, 0.2f, 0.3f, 0.3f}};
    int k = 0;
    for (uint32_t r = 0; r < 8; ++r) {
        for (const auto &in : inputs) {
            float axes[64] = {};
            std::memcpy(axes, in, sizeof in);
            axes[18] = (k++ % 2) ? 1.0f : 0.0f;
            axes[61] = 0.25f;
            const S_Controls before = Controls(v);
            car->m_restrictions = r;
            car->ApplyControls(axes, nullptr);
            const S_Controls got = Controls(v);
            SetControls(v, before);
            float t = axes[0], s = axes[3] - axes[2];
            ExpectedRestrict(r, t, s);
            v.SetThrottle(t, true);
            v.SetSteering(s);
            v.SetBrake(axes[1]);
            v.SetHandbrake(axes[18] != 0.0f, !(t > 0.0f));
            v.SetUnknown4f2630(v.m_topSpeed);
            v.SetUnknown4f23b0(1.0f - axes[61]);
            Expect(controls, got == Controls(v), __LINE__);
            SetControls(v, before);
            // SetInput: the same restrictions on its two values.
            car->m_restrictions = r;
            Expect(controls, car->SetInput(in[0], in[3] - in[2]), __LINE__);
            const S_Controls set = Controls(v);
            SetControls(v, before);
            float t2 = in[0], s2 = in[3] - in[2];
            ExpectedRestrict(r, t2, s2);
            v.SetThrottle(t2, true);
            v.SetSteering(s2);
            Expect(controls, set == Controls(v), __LINE__);
            SetControls(v, before);
        }
    }
    car->m_restrictions = 0;
    float axes[64] = {};
    axes[0] = 0.5f;
    axes[3] = 0.5f;
    {
        // No driver; a network game's driver of another machine: nothing.
        const S_Controls before = Controls(v);
        car->m_driver = nullptr;
        car->ApplyControls(axes, nullptr);
        Expect(controls, Controls(v) == before && !car->SetInput(0.5f, 0.5f) && Controls(v) == before, __LINE__);
        car->m_driver = driver;
        g_gameRole = GAME_ROLE_CLIENT;
        driver->localPlayer = false;
        car->ApplyControls(axes, nullptr);
        Expect(controls, Controls(v) == before, __LINE__);
        // SetInput: true, applied only by the driver's own machine.
        g_localPlayerSlot = 31;
        Expect(controls, car->SetInput(0.5f, 0.5f) && Controls(v) == before, __LINE__);
        g_localPlayerSlot = 30;
        Expect(controls, car->SetInput(0.5f, 0.5f) && !(Controls(v) == before), __LINE__);
        SetControls(v, before);
        driver->localPlayer = true;
        car->ApplyControls(axes, nullptr);
        Expect(controls, !(Controls(v) == before), __LINE__);
        SetControls(v, before);
        g_gameRole = GAME_ROLE_LOCAL;
        g_localPlayerSlot = 0;
        // A dead driver: everything let go (the top speed's value left alone).
        driver->health = 0;
        car->ApplyControls(axes, nullptr);
        const S_Controls got = Controls(v);
        SetControls(v, before);
        v.SetThrottle(0.0f, true);
        v.SetSteering(0.0f);
        v.SetBrake(0.0f);
        v.SetHandbrake(false, true);
        v.SetUnknown4f23b0(0.0f);
        Expect(controls, got == Controls(v) && !car->SetInput(0.5f, 0.5f), __LINE__);
        SetControls(v, before);
        driver->health = 800;
        // The engine starts with 0.001 l of fuel (the slow way), not below.
        v.m_engineOn = false;
        v.m_unknown22A2 = 0;
        v.m_fuel = 0.0005f;
        car->ApplyControls(axes, nullptr);
        Expect(controls, !v.m_engineOn && !v.m_unknown22A2, __LINE__);
        v.m_fuel = 0.05f;                               // (the vehicle itself wants 0.02 l)
        v.m_unknown610 = 1.0f;
        car->ApplyControls(axes, nullptr);
        Expect(controls, v.m_engineOn || v.m_unknown22A2, __LINE__);
        SetControls(v, before);
        v.m_engineOn = true;
        v.m_fuel = 50.0f;
    }

    I3D_sound *sounds0[41];                             // (group 2, put back at the end)

    // --- the car's state: IsDrivable over a set of states, against the checks worked out here;
    // the environment flags.
    bool state = true;
    {
        std::vector<uint32_t> flags0;
        for (int i = 0; i < v.m_wheelCount; ++i)
            flags0.push_back(v.m_wheels[i] ? v.m_wheels[i]->flags : 0);
        const S_vector4 axisY0 = v.m_modelAxisY;
        const float speed0 = v.m_speed;
        const float conditions[][3] = {{1, 1, 1}, {0, 1, 1}, {1, -1, 1}, {1, 1, 0}, {NAN, 1, 1}};
        int cases = 0;
        for (const auto &c : conditions) {
            for (int detach = -1; detach < std::min(v.m_wheelCount, 2); ++detach) {
                for (int ground = 0; ground < 6; ++ground) {
                    for (float upY : {0.8f, 0.5f}) {
                        for (float speed : {0.5f, 1.5f}) {
                            v.m_unknown610 = c[0];
                            v.m_unknown63C = c[1];
                            v.m_fuel = c[2];
                            v.m_speed = speed;
                            v.m_modelAxisY = {0.0f, upY, 0.0f, 0.0f};
                            for (int i = 0; i < v.m_wheelCount; ++i) {
                                if (!v.m_wheels[i])
                                    continue;
                                uint32_t f = flags0[i] & ~(WHEEL_ON_GROUND | WHEEL_DETACHED);
                                // ground: 0 none, 1 the first only, 2 the even ones, 3 the odd ones, 4 all,
                                // 5 the first two
                                if (ground == 4 || (ground == 1 && i == 0) || (ground == 2 && i % 2 == 0) ||
                                    (ground == 3 && i % 2 == 1) || (ground == 5 && i < 2))
                                    f |= WHEEL_ON_GROUND;
                                if (i == detach)
                                    f |= WHEEL_DETACHED;
                                v.m_wheels[i]->flags = f;
                            }
                            for (uint32_t checks = 0; checks < 8; ++checks) {
                                Expect(state, car->IsDrivable(checks) == ExpectedDrivable(v, checks), __LINE__);
                                ++cases;
                            }
                        }
                    }
                }
            }
        }
        Expect(state, cases > 0, __LINE__);
        // The environment flags: the grounded wheels' ground's 0x2000 / 0x4000.
        S_CollisionVolume deadly, restricted, both;
        deadly.flags = 0x2000 | 0x10;
        restricted.flags = 0x4000 | 0x1;
        both.flags = 0x6000;
        std::vector<S_CollisionVolume *> grounds0;
        for (int i = 0; i < v.m_wheelCount; ++i)
            grounds0.push_back(v.m_wheels[i] ? v.m_wheels[i]->ground : nullptr);
        for (int i = 0; i < v.m_wheelCount; ++i) {
            if (v.m_wheels[i]) {
                v.m_wheels[i]->ground = nullptr;
                v.m_wheels[i]->flags = flags0[i] | WHEEL_ON_GROUND;
            }
        }
        Expect(state, car->GetEnvironmentFlags() == 0, __LINE__);
        if (v.m_wheelCount >= 2 && v.m_wheels[0] && v.m_wheels[1]) {
            v.m_wheels[0]->ground = &deadly;
            Expect(state, car->GetEnvironmentFlags() == 0x2000, __LINE__);
            v.m_wheels[1]->ground = &restricted;
            Expect(state, car->GetEnvironmentFlags() == 0x6000, __LINE__);
            v.m_wheels[0]->flags &= ~WHEEL_ON_GROUND;
            Expect(state, car->GetEnvironmentFlags() == 0x4000, __LINE__);
            v.m_wheels[1]->ground = &both;
            v.m_wheels[1]->flags &= ~WHEEL_ON_GROUND;
            Expect(state, car->GetEnvironmentFlags() == 0, __LINE__);
        }
        for (int i = 0; i < v.m_wheelCount; ++i) {
            if (v.m_wheels[i]) {
                v.m_wheels[i]->flags = flags0[i];
                v.m_wheels[i]->ground = grounds0[i];
            }
        }
        v.m_modelAxisY = axisY0;
        v.m_speed = speed0;
        v.m_unknown610 = 1.0f;
        v.m_unknown63C = 1.0f;
        v.m_fuel = 50.0f;

        // The engine's noise: the loudest playing of group 2's 3..18 (with the engine on).
        std::copy(std::begin(v.m_sounds2), std::end(v.m_sounds2), sounds0);
        for (I3D_sound *&s : v.m_sounds2)
            s = nullptr;
        v.m_sounds2[2] = reinterpret_cast<I3D_sound *>(MakeSound(0.95f, true));    // (not the engine's)
        v.m_sounds2[19] = reinterpret_cast<I3D_sound *>(MakeSound(0.9f, true));
        v.m_sounds2[3] = reinterpret_cast<I3D_sound *>(MakeSound(0.4f, true));
        v.m_sounds2[7] = reinterpret_cast<I3D_sound *>(MakeSound(0.6f, true));
        v.m_sounds2[12] = reinterpret_cast<I3D_sound *>(MakeSound(0.8f, false));  // (off)
        v.m_sounds2[18] = reinterpret_cast<I3D_sound *>(MakeSound(0.5f, true));
        v.m_sounds2[4] = reinterpret_cast<I3D_sound *>(MakeSound(0.7f, true));
        v.m_unknownEE8[1] = 42.0f;
        float volume = -1.0f, range = -1.0f;
        car->GetEngineNoise(&volume, &range);
        Expect(state, volume == 0.7f && range == 42.0f, __LINE__);
        v.m_sounds2[4]->m_frameFlags &= ~1u;
        car->GetEngineNoise(&volume, &range);
        Expect(state, volume == 0.6f && range == 42.0f, __LINE__);
        v.m_sounds2[7]->m_frameFlags &= ~1u;
        car->GetEngineNoise(&volume, &range);
        Expect(state, volume == 0.5f, __LINE__);
        v.m_sounds2[18]->m_frameFlags &= ~1u;
        car->GetEngineNoise(&volume, &range);
        Expect(state, volume == 0.4f, __LINE__);
        v.m_engineOn = false;
        car->GetEngineNoise(&volume, &range);
        Expect(state, volume == 0.0f && range == 42.0f, __LINE__);
        v.m_engineOn = true;
    }

    // --- the water: the engine dies and the crew drowns once water stands above the car.
    bool water = true;
    {
        std::vector<C_Human *> crew;
        for (int i = 0; i < std::min(n, 2); ++i) {
            crew.push_back(new C_Human());
            v.m_seats[i].occupant = crew.back();
        }
        const float carY = g_model->GetWorldPos().y;
        struct S_Case {
            float top;
            float engine;
            bool above;
            uint32_t role;
            bool coop;
            int type;
            bool drowns, timer;
        } cases[] = {
            {-FLT_MAX, 1.0f, true, GAME_ROLE_LOCAL, false, 0, false, false},
            {carY - 1.0f, 1.0f, true, GAME_ROLE_LOCAL, false, 0, false, false},
            {carY + 1.0f, 0.0f, true, GAME_ROLE_LOCAL, false, 0, false, false},
            {carY + 1.0f, 1.0f, false, GAME_ROLE_LOCAL, false, 0, false, false},
            {carY + 1.0f, 1.0f, true, GAME_ROLE_LOCAL, false, 0, true, false},
            {carY + 1.0f, 1.0f, true, GAME_ROLE_SERVER, false, MP_GAME_DEATHMATCH, true, true},
            {carY + 1.0f, 1.0f, true, GAME_ROLE_SERVER, true, MP_GAME_DEATHMATCH, true, false},
            {carY + 1.0f, 1.0f, true, GAME_ROLE_CLIENT, false, MP_GAME_OBJECTIVES, true, false},
            {carY + 1.0f, 1.0f, true, GAME_ROLE_CLIENT, false, MP_GAME_OCCUPATION, true, true},
        };
        for (const S_Case &c : cases) {
            g_waterTop = c.top;
            g_waterAbove = c.above;
            g_gameRole = c.role;
            g_coop = c.coop;
            g_mpSettingsValue.gameType = c.type;
            v.m_unknown610 = c.engine;
            car->m_respawnTimer.running = false;
            car->m_respawnTimer.start = -1;
            for (C_Human *h : crew)
                h->hits.clear();
            g_waterRays.clear();
            car->CheckWater();
            const bool rayed = c.top != -FLT_MAX && carY < c.top && c.engine > 0.0f;
            Expect(water, g_waterRays.size() == (rayed ? 1u : 0u), __LINE__);
            if (rayed && !g_waterRays.empty())
                Expect(water, g_waterRays[0].first == g_model->GetWorldPos() && g_waterRays[0].second == S_vector(0.0f, 100.0f, 0.0f), __LINE__);
            Expect(water, (v.m_unknown610 == 0.0f) == c.drowns || (!c.drowns && c.engine == 0.0f), __LINE__);
            for (C_Human *h : crew)
                Expect(water, h->hits.size() == (c.drowns ? 1u : 0u) &&
                         (!c.drowns || (h->hits[0].type == HIT_DROWNING && h->hits[0].amount == 1000.0f &&
                                        h->hits[0].pos == S_vector(0.0f, 0.0f, 0.0f) && !h->hits[0].attacker &&
                                        h->hits[0].flags == 0)), __LINE__);
            const int64_t now = GetPerfTimeMs();
            Expect(water, car->m_respawnTimer.running == c.timer &&
                     (!c.timer || (car->m_respawnTimer.start >= now - 5 && car->m_respawnTimer.start <= now)), __LINE__);
        }
        for (int i = 0; i < std::min(n, 2); ++i) {
            v.m_seats[i].occupant = nullptr;
            delete crew[i];
        }
        g_waterTop = -FLT_MAX;
        g_gameRole = GAME_ROLE_LOCAL;
        g_coop = false;
        g_mpSettingsValue.gameType = MP_GAME_DEATHMATCH;
        v.m_unknown610 = 1.0f;
    }

    // --- the detail: the options by the camera's distance, whether the car was drawn, the role,
    // full detail; the sounds on within 100 m of a shown model.
    bool detail = true;
    {
        const S_vector at = g_model->GetWorldPos();
        const float distances[] = {5.0f, 19.9f, 22.0f, 25.0f, 29.0f, 31.0f, 99.0f, 101.0f, 149.0f, 151.0f};
        const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_SERVER | GAME_ROLE_CLIENT | 0x10};
        for (uint32_t role : roles) {
            for (int full = 0; full < 2; ++full) {
                for (int shown = 0; shown < 2; ++shown) {
                    for (float d : distances) {
                        for (int modelOn = 0; modelOn < 2; ++modelOn) {
                            camera->m_localMatrix(3) = at + S_vector(d * 0.6f, d * 0.8f, 0.0f);
                            g_gameRole = role;
                            car->m_wrecked = full != 0;
                            v.m_unknown2292 = static_cast<uint8_t>(shown);
                            g_model->m_frameFlags = modelOn ? g_model->m_frameFlags | 1u : g_model->m_frameFlags & ~1u;
                            v.m_unknownF6C = 0xFFFFFFFFu;
                            v.m_soundsOn = !v.m_soundsOn;
                            const bool soundsBefore = v.m_soundsOn;
                            car->UpdateDetail();
                            const S_vector dv = at - camera->GetWorldPos();
                            const float d2 = (dv.z * dv.z + dv.x * dv.x) + dv.y * dv.y;
                            Expect(detail, v.m_unknownF6C == ExpectedDetail(0xFFFFFFFFu, full, role, d2, shown), __LINE__);
                            const bool returnsEarly = full || ((role & 7) != 7 && !(role & 2));
                            if (returnsEarly)
                                Expect(detail, v.m_soundsOn == soundsBefore, __LINE__);
                            else
                                Expect(detail, v.m_soundsOn == (d2 < 10000.0f && modelOn), __LINE__);
                        }
                    }
                }
            }
        }
        g_model->m_frameFlags |= 1u;
        g_gameRole = GAME_ROLE_LOCAL;
        car->m_wrecked = false;
    }

    // --- Update (a network client's: the replication stubbed): the capped time to the seat
    // weapons, the seat cameras and the model; the indicators' threshold, drawn or not; the actor
    // at the model; the deadly ground's message once; who left forgotten after 3 s; the marks every
    // 500 ms of driving.
    bool update = true;
    {
        S_FakeWeapon weapons[10];
        car->m_seatWeapons[2] = AsWeapon(weapons[2]);
        car->m_seatWeapons[9] = AsWeapon(weapons[9]);
        int keepers[2];
        if (n >= 1)
            v.m_seats[0].camera = reinterpret_cast<C_CameraFrameKeeper *>(&keepers[0]);
        g_gameRole = GAME_ROLE_CLIENT;
        int ids[10], times[10];
        std::fill(std::begin(ids), std::end(ids), -1);
        std::fill(std::begin(times), std::end(times), 0);
        car->m_seatLeftIds = ids;
        car->m_seatLeftTimes = times;
        const int now = static_cast<int>(GetPerfTimeMs());
        if (n >= 2) {
            ids[0] = 5;
            times[0] = now - 4000;                      // forgotten
            ids[1] = 6;
            times[1] = now;                             // kept
        }
        sceneStamp = 9;
        carStamp = 9;
        v.m_unknown934 = 0.5f;
        g_networkTimes.clear();
        g_keeperUpdates.clear();
        g_frameTicks.clear();
        g_messageReads = 0;
        ClearStreams();
        car->m_deadlyReported = false;
        car->m_driver = nullptr;
        v.m_speed = 0.0f;
        car->m_marksTime = 0;
        g_finds.clear();
        car->Update(120);
        Expect(update, g_networkTimes == std::vector<int>{83} && weapons[2].updates == std::vector<int>{83} &&
                  weapons[9].updates == std::vector<int>{83} && g_frameTicks[g_model] == 83 &&
                  (n < 1 || (g_keeperUpdates.size() == 1 && g_keeperUpdates[0].second == 83)) &&
                  v.m_unknown5C4 == 0.5f * 0.9f && v.m_unknown2292 == 1 && g_messageReads == 1 &&
                  car->GetPosition4().x == v.m_modelPos.x && car->GetPosition4().y == v.m_modelPos.y &&
                  car->GetPosition4().z == v.m_modelPos.z && car->GetDirection4().x == v.m_modelAxisZ.x &&
                  car->GetDirection4().z == v.m_modelAxisZ.z && g_streams.empty() && g_finds.empty() &&
                  (n < 2 || (ids[0] == -1 && ids[1] == 6)), __LINE__);
        // Under the cap, not drawn.
        carStamp = 8;
        car->Update(40);
        Expect(update, g_networkTimes.back() == 40 && weapons[2].updates.back() == 40 && g_frameTicks[g_model] == 40 &&
                  v.m_unknown2292 == 0, __LINE__);
        // Deadly ground: without a driver (not in co-op) the message once.
        S_CollisionVolume deadly;
        deadly.flags = HUMAN_ENV_DEADLY;
        S_VehicleWheel *w0 = v.m_wheelCount > 0 ? v.m_wheels[0] : nullptr;
        if (w0) {
            S_CollisionVolume *ground0 = w0->ground;
            const uint32_t flags0 = w0->flags;
            // (The client's replay, UpdateNetwork, looks for the wheels' ground again: the flat
            // ground is deadly too.)
            g_groundY = GroundUnderWheels(v);
            g_flatGround = true;
            g_groundExtraFlags = HUMAN_ENV_DEADLY;
            const auto deadlyGround = [&] {
                w0->ground = &deadly;
                w0->flags |= WHEEL_ON_GROUND;
            };
            deadlyGround();
            g_coop = true;
            car->Update(40);
            Expect(update, g_streams.empty() && !car->m_deadlyReported, __LINE__);
            g_coop = false;
            deadlyGround();
            car->Update(40);
            Expect(update, g_streams.size() == 1 && g_streams[0]->ex && g_streams[0]->id == 77 && g_streams[0]->sent &&
                      g_streams[0]->bits == std::vector<std::pair<uint32_t, int>>{{8, 4}} && car->m_deadlyReported, __LINE__);
            deadlyGround();
            car->Update(40);
            Expect(update, g_streams.size() == 1, __LINE__);
            // An enemy at the wheel: none; a player: the message.
            car->m_deadlyReported = false;
            auto *enemy = new C_Human();
            enemy->role = HUMAN_ROLE_ENEMY;
            car->m_driver = enemy;
            deadlyGround();
            car->Update(40);
            Expect(update, g_streams.size() == 1, __LINE__);
            enemy->role = HUMAN_ROLE_PLAYER;
            deadlyGround();
            car->Update(40);
            Expect(update, g_streams.size() == 2, __LINE__);
            car->m_driver = nullptr;
            delete enemy;
            g_groundExtraFlags = 0;
            g_flatGround = false;
            w0->ground = ground0;
            w0->flags = flags0;
        }
        // Single player: no replication, no messages read; the simulation steps (the car falls).
        g_gameRole = GAME_ROLE_LOCAL;
        g_flatGround = false;
        const size_t networkCalls = g_networkTimes.size();
        const int reads = g_messageReads;
        const float y0 = v.m_modelPos.y;
        v.m_unknown600 |= VEHICLE_PHYSICS_AWAKE;
        // (Off the ground the replay above found: no contact kept from it.)
        for (int i = 0; i < v.m_wheelCount; ++i) {
            if (S_VehicleWheel *w = v.m_wheels[i]) {
                w->flags &= ~(WHEEL_ON_GROUND | WHEEL_ON_REST_PLANE);
                w->ground = nullptr;
                w->groundPlaneVolume = nullptr;
            }
        }
        for (int i = 0; i < 5; ++i)
            car->Update(40);
        Expect(update, g_networkTimes.size() == networkCalls && g_messageReads == reads && v.m_modelPos.y < y0, __LINE__);
        // The marks: after 500 ms of driving, then once more after it stopped.
        g_gameRole = GAME_ROLE_CLIENT;
        g_finds.clear();
        car->m_marksTime = 0;
        // (The client's replay takes the speed from the velocity: ReadModelState.)
        v.m_velocity = {3.0f, 0.0f, 0.0f, 0.0f};
        v.m_speed = 3.0f;
        for (int i = 0; i < 12; ++i)
            car->Update(40);                            // 480 ms
        Expect(update, g_finds.empty() && car->m_marksTime == 480, __LINE__);
        car->Update(40);
        Expect(update, g_finds.size() == 1 && car->m_marksTime == 1, __LINE__);
        v.m_velocity = {0.0f, 0.0f, 0.0f, 0.0f};
        v.m_speed = 0.0f;
        for (int i = 0; i < 12; ++i)
            car->Update(40);
        Expect(update, g_finds.size() == 1 && car->m_marksTime == 481, __LINE__);
        car->Update(40);
        Expect(update, g_finds.size() == 2 && car->m_marksTime == 0, __LINE__);
        car->Update(40);
        Expect(update, g_finds.size() == 2 && car->m_marksTime == 0, __LINE__);
        car->m_seatWeapons[2] = car->m_seatWeapons[9] = nullptr;
        if (n >= 1)
            v.m_seats[0].camera = nullptr;
        car->m_seatLeftIds = nullptr;
        car->m_seatLeftTimes = nullptr;
    }

    // --- Tick: the sunk car's timer, a charge, the server's state, the replication, the seat
    // weapons and their ammo, the parts, the crew bailing out, the engine's noise.
    bool tick = true;
    {
        S_FakeWeapon weapons[10];
        C_FakeDef def(55);
        C_FakeStore store;
        C_ItemContainer *containers[1] = {&store};
        car->m_containers = containers;
        // The timer fires: the message, the restart on the server (not on a client; the real
        // RestartMission, which sets the replicated place, here where the car is).
        car->m_startPosition = car->GetPosition4();
        car->m_startDirection = car->GetDirection4();
        for (uint32_t role : {GAME_ROLE_SERVER, GAME_ROLE_CLIENT}) {
            g_gameRole = role;
            ClearStreams();
            const size_t restarts0 = netObject.property.track.values.size();
            car->m_respawnTimer.running = true;
            car->m_respawnTimer.period = 38000;
            car->m_respawnTimer.start = static_cast<int>(GetPerfTimeMs()) - 38001;
            car->Tick(100);
            // (The server: the restart's place and the tick's replication's; the restart flags the
            // store, sent in the same tick.)
            const bool server = role == GAME_ROLE_SERVER;
            Expect(tick, !car->m_respawnTimer.running && g_streams.size() == (server ? 2u : 1u) && !g_streams[0]->ex &&
                    g_streams[0]->id == 77 && g_streams[0]->sent &&
                    g_streams[0]->bits == std::vector<std::pair<uint32_t, int>>{{6, 4}} &&
                    (!server || g_streams[1]->bits[0] == std::pair<uint32_t, int>(CAR_MSG_STORE, 4)) &&
                    netObject.property.track.values.size() == restarts0 + (server ? 2 : 0), __LINE__);
            car->m_respawnTimer.running = true;
            car->m_respawnTimer.start = static_cast<int>(GetPerfTimeMs());
            ClearStreams();
            car->Tick(100);
            Expect(tick, car->m_respawnTimer.running && g_streams.empty(), __LINE__);
            car->m_respawnTimer.running = false;
        }
        // A charge's fuse; the server's state (and not on a client).
        car->m_chargeTime = 300;
        car->Tick(100);
        const bool fused = car->m_chargeTime == 200;
        car->m_chargeTime = -1;
        car->Tick(100);
        Expect(tick, fused && car->m_chargeTime == -1 && !car->m_wrecked, __LINE__);
        for (uint32_t role : {GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_LOCAL}) {
            g_gameRole = role;
            ClearStreams();
            car->m_stateChanged = true;
            car->Tick(100);
            const bool server = role == GAME_ROLE_SERVER;
            Expect(tick, car->m_stateChanged == !server, __LINE__);
            // (The store's items after the kind: none, the end mark.)
            if (server)
                Expect(tick, g_streams.size() == 1 &&
                        g_streams[0]->bits == std::vector<std::pair<uint32_t, int>>{{7, 4}, {0xFFFF, 16}} &&
                        g_streams[0]->sent, __LINE__);
            else
                Expect(tick, g_streams.empty(), __LINE__);
            car->m_stateChanged = false;
        }
        // The replication, the messages, who left and the vehicle on the dedicated server only;
        // the seat weapons and the parts everywhere.
        car->m_seatWeapons[4] = AsWeapon(weapons[4]);
        for (uint32_t role : {GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_LOCAL}) {
            g_gameRole = role;
            g_networkTimes.clear();
            g_messageReads = 0;
            car->m_parts.fuse = 1000;
            car->m_parts.wrecked = false;
            weapons[4].ticks.clear();
            int ids[10], times[10];
            std::fill(std::begin(ids), std::end(ids), -1);
            std::fill(std::begin(times), std::end(times), 0);
            ids[0] = 3;
            times[0] = static_cast<int>(GetPerfTimeMs()) - 4000;
            car->m_seatLeftIds = ids;
            car->m_seatLeftTimes = times;
            car->SetPosition4({0.0f, -999.0f, 0.0f, 1.0f});
            car->Tick(70);
            const bool dedicated = role == GAME_ROLE_SERVER;
            Expect(tick, g_networkTimes == (dedicated ? std::vector<int>{70} : std::vector<int>{}) &&
                    g_messageReads == (dedicated ? 1 : 0) && (ids[0] == -1) == (dedicated && n >= 1) &&
                    (car->GetPosition4().y == v.m_modelPos.y) == dedicated &&
                    weapons[4].ticks == std::vector<int>{70} && car->m_parts.fuse == 930, __LINE__);
            car->m_seatLeftIds = nullptr;
            car->m_seatLeftTimes = nullptr;
        }
        // The ammo: a weapon's store short of it gets one every 30 s (network games, but co-op).
        g_gameRole = GAME_ROLE_SERVER;
        weapons[4] = S_FakeWeapon();
        weapons[4].def = &def;
        weapons[4].refillBelow = 5;
        GetGame()->m_missionTime = 100000;
        struct S_Refill {
            uint32_t role;
            uint32_t refill;
            int count;
            bool coop;
            bool refills;
        } refills[] = {
            {GAME_ROLE_SERVER, 69999, 5, false, true},  {GAME_ROLE_SERVER, 70000, 5, false, false},
            {GAME_ROLE_SERVER, 69999, 6, false, false}, {GAME_ROLE_SERVER, 69999, 0, true, false},
            {GAME_ROLE_LOCAL, 0, 0, false, false},      {GAME_ROLE_CLIENT, 69999, 4, false, true},
        };
        for (const S_Refill &c : refills) {
            g_gameRole = c.role;
            weapons[4].refillTime = c.refill;
            store.count = c.count;
            store.counted.clear();
            store.added.clear();
            g_coop = c.coop;
            g_created.clear();
            g_queued.clear();
            car->Tick(100);
            if (c.refills)
                Expect(tick, g_created.size() == 1 && g_created[0].typeId == 55 && g_created[0].container == 0 &&
                        !g_created[0].owner && g_created[0].uid == -1 && store.added.size() == 1 &&
                        store.added[0] == g_created[0].item && store.addIndices.back() == -1 && g_queued.size() == 1 &&
                        g_queued[0].type == 3 && g_queued[0].rounds == 55 && g_queued[0].seat == g_created[0].item->m_uid &&
                        g_queued[0].sender == 6 && g_queued[0].reliable && g_queued[0].fromClient &&
                        weapons[4].refillTime == 100000 && store.counted == std::vector<int>{55}, __LINE__);
            else
                Expect(tick, g_created.empty() && store.added.empty() && g_queued.empty() && weapons[4].refillTime == c.refill, __LINE__);
        }
        g_coop = false;
        // (A weapon without a definition is passed over.)
        weapons[4].def = nullptr;
        weapons[4].refillTime = 0;
        g_gameRole = GAME_ROLE_SERVER;
        g_created.clear();
        car->Tick(100);
        Expect(tick, g_created.empty(), __LINE__);
        car->m_seatWeapons[4] = nullptr;
        // The water (Tick's CheckWater): the engine dies.
        g_gameRole = GAME_ROLE_LOCAL;
        g_waterTop = g_model->GetWorldPos().y + 1.0f;
        g_waterAbove = true;
        v.m_unknown610 = 1.0f;
        car->Tick(100);
        Expect(tick, v.m_unknown610 == 0.0f, __LINE__);
        g_waterTop = -FLT_MAX;
        v.m_unknown610 = 1.0f;
        // Single player: the crew bails out of a car stopped on its side (not of one that can drive,
        // stands upright, moves or is destroyed).
        g_gameRole = GAME_ROLE_LOCAL;
        std::vector<C_Human *> crew;
        for (int i = 0; i < std::min(n, 3); ++i) {
            crew.push_back(new C_Human());
            v.m_seats[i].occupant = crew.back();
        }
        std::vector<uint32_t> flags0;
        for (int i = 0; i < v.m_wheelCount; ++i)
            flags0.push_back(v.m_wheels[i] ? v.m_wheels[i]->flags : 0);
        // (Destroyed: the engine's health at 0.)
        const float engineWas = v.m_unknown610;
        const float engine = engineWas > 0.0f ? engineWas : 1.0f;
        struct S_Bail {
            float speed, upY;
            bool grounded, destroyed, bails;
        } bails[] = {
            {0.0f, 0.5f, false, false, true}, {0.2f, 0.5f, false, false, false}, {0.0f, 0.75f, false, false, false},
            {0.0f, 0.65f, true, false, false}, {0.0f, 0.65f, false, false, true}, {0.0f, 0.5f, true, false, true},
            {0.0f, 0.5f, false, true, false},
        };
        for (const S_Bail &b : bails) {
            v.m_speed = b.speed;
            v.m_modelAxisY = {0.0f, b.upY, 0.0f, 0.0f};
            for (int i = 0; i < v.m_wheelCount; ++i)
                if (v.m_wheels[i])
                    v.m_wheels[i]->flags = b.grounded ? flags0[i] | WHEEL_ON_GROUND : flags0[i] & ~WHEEL_ON_GROUND;
            v.m_unknown610 = b.destroyed ? 0.0f : engine;
            for (C_Human *h : crew)
                h->leaveRequests = 0;
            car->Tick(100);
            for (C_Human *h : crew)
                Expect(tick, h->leaveRequests == (b.bails ? 1 : 0), __LINE__);
        }
        v.m_unknown610 = engineWas;
        for (int i = 0; i < v.m_wheelCount; ++i)
            if (v.m_wheels[i])
                v.m_wheels[i]->flags = flags0[i];
        for (int i = 0; i < std::min(n, 3); ++i) {
            v.m_seats[i].occupant = nullptr;
            delete crew[i];
        }
        v.m_modelAxisY = {0.0f, 1.0f, 0.0f, 0.0f};
        // The engine's noise (single player, with a driver): an AI-only request at the car, the
        // loudest playing engine sound's volume.
        for (I3D_sound *&s : v.m_sounds2)
            s = nullptr;
        v.m_sounds2[3] = reinterpret_cast<I3D_sound *>(MakeSound(0.4f, true));
        v.m_sounds2[9] = reinterpret_cast<I3D_sound *>(MakeSound(0.3f, true));
        car->m_driver = driver;
        v.m_engineOn = true;
        v.m_unknownEE8[1] = 42.0f;
        g_soundRequests.clear();
        car->Tick(100);
        Expect(tick, g_soundRequests.size() == 1, __LINE__);
        if (g_soundRequests.size() == 1) {
            const S_SoundRequest &r = g_soundRequests[0];
            S_SoundRequest defaults;
            Expect(tick, r.flags == SOUND_AI_ONLY && r.attachTo == g_model && r.source == driver && r.chapter == 0 &&
                    r.group == 0x2B && r.farScale == 42.0f && r.volume == 0.4f && r.pos == S_vector(0.0f, 0.0f, 0.0f) &&
                    r.fileName == defaults.fileName && r.channel == defaults.channel && r.nearScale == defaults.nearScale, __LINE__);
        }
        v.m_engineOn = false;
        g_soundRequests.clear();
        car->Tick(100);
        Expect(tick, g_soundRequests.empty(), __LINE__);
        v.m_engineOn = true;
        car->m_driver = nullptr;
        car->Tick(100);
        g_gameRole = GAME_ROLE_SERVER;
        car->m_driver = driver;
        car->Tick(100);
        Expect(tick, g_soundRequests.empty(), __LINE__);
        g_gameRole = GAME_ROLE_LOCAL;
        car->m_containers = nullptr;
    }

    // --- the checkpoint marks: around the car (within 15 m of the nearest within 5 m; the
    // nearest twice), ahead of it (single player, moving), cleared again.
    bool marks = true;
    {
        const S_vector at = g_model->GetWorldPos();
        S_Checkpoint points[8];
        const S_vector offsets[8] = {{3, 0, 0},  {0, 0, 1},   {9, 0, 9},  {-12, 0, 0},
                                     {0, 0, 14}, {30, 0, 30}, {0, 0, 6},  {1.5f, 0, 9}};
        for (int i = 0; i < 8; ++i)
            points[i].pos = at + offsets[i];
        network.m_points = points;
        network.m_pointCount = 8;
        g_gameRole = GAME_ROLE_LOCAL;
        v.m_speed = 0.0f;
        g_finds.clear();
        g_neighbourCalls.clear();
        car->MarkCheckpoints();
        // The nearest within 5 m: point 1 (1 m); its neighbours within 15 m: 1 itself, 0, 2, 3, 4, 6, 7.
        std::vector<uint16_t> want = {1, 0, 2, 3, 4, 6, 7, 1};
        std::vector<uint16_t> got;
        for (const S_CarCheckpointMark &m : car->m_marks) {
            got.push_back(m.checkpoint);
            Expect(marks, m.flagged, __LINE__);
        }
        std::reverse(want.begin(), want.end());
        Expect(marks, got == want && g_finds.size() == 1 && g_finds[0].pos == at && g_finds[0].maxDist == 5.0f &&
                 g_finds[0].minDist == -1.0f && g_neighbourCalls.size() == 1 && g_neighbourCalls[0].first == 1 &&
                 g_neighbourCalls[0].second == 15.0f && car->m_marksAhead.empty(), __LINE__);
        for (int i = 0; i < 8; ++i)
            Expect(marks, ((points[i].flags & CHECKPOINT_FLAG_CAR_0200) != 0) == (i != 5) &&
                     car->IsCheckpointMarked(i) == (i != 5), __LINE__);
        Expect(marks, !car->IsCheckpointMarked(100), __LINE__);
        // Moving along +z: those within 2 m of the 10 m ahead (1, 6, 7 at 1.5 m; not 0 at 3 m).
        v.m_speed = 5.0f;
        v.m_velocity = {0.0f, 0.0f, 5.0f, 0.0f};
        g_neighbourCalls.clear();
        car->MarkCheckpoints();
        Expect(marks, g_neighbourCalls.size() == 2 && g_neighbourCalls[1].second == 16.0f && car->m_marks.size() == 8, __LINE__);
        std::vector<uint16_t> ahead(car->m_marksAhead.begin(), car->m_marksAhead.end());
        std::vector<uint16_t> wantAhead = {7, 6, 1};
        Expect(marks, ahead == wantAhead, __LINE__);
        for (int i = 0; i < 8; ++i)
            Expect(marks, ((points[i].flags & CHECKPOINT_FLAG_UNDER_CAR) != 0) == (i == 1 || i == 6 || i == 7), __LINE__);
        // Below 0.1 m/s none; in network games none at all (nor cleared).
        v.m_speed = 0.05f;
        car->MarkCheckpoints();
        Expect(marks, car->m_marksAhead.empty() && !(points[6].flags & CHECKPOINT_FLAG_UNDER_CAR), __LINE__);
        v.m_speed = 5.0f;
        car->MarkCheckpoints();
        g_gameRole = GAME_ROLE_CLIENT;
        car->MarkCheckpoints();
        Expect(marks, car->m_marksAhead.size() == 3 && (points[6].flags & CHECKPOINT_FLAG_UNDER_CAR), __LINE__);
        g_gameRole = GAME_ROLE_LOCAL;
        // Cleared: every flag back, an unflagged mark counts +0x1A down.
        car->m_marks.push_back({5, false});
        points[5].unknown1A = 0x45;
        car->ClearMarks();
        Expect(marks, car->m_marks.empty() && car->m_marksAhead.empty() && points[5].unknown1A == 0x25, __LINE__);
        for (int i = 0; i < 8; ++i)
            Expect(marks, !(points[i].flags & (CHECKPOINT_FLAG_CAR_0200 | CHECKPOINT_FLAG_UNDER_CAR)), __LINE__);
        car->m_marks.push_back({5, false});
        points[5].unknown1A = 0x05;
        car->ClearMarks();
        Expect(marks, points[5].unknown1A == 0x05, __LINE__);
        // No point within 5 m: nothing around (and nothing ahead).
        g_model->m_localMatrix(3) = g_model->m_localMatrix(3) + S_vector(500.0f, 0.0f, 0.0f);
        car->MarkCheckpoints();
        Expect(marks, car->m_marks.empty() && car->m_marksAhead.empty(), __LINE__);
        g_model->m_localMatrix(3) = g_model->m_localMatrix(3) - S_vector(500.0f, 0.0f, 0.0f);
        network.m_points = nullptr;
        network.m_pointCount = 0;
        v.m_speed = 0.0f;
    }

    // --- the timer, the messages.
    bool timer = true;
    {
        S_PerfTimer t(500);
        const int now = static_cast<int>(GetPerfTimeMs());
        Expect(timer, t.period == 500 && !t.running && t.start >= now - 5 && t.start <= now && !t.Fire(), __LINE__);
        t.start -= 1000;
        Expect(timer, !t.Fire(), __LINE__);                // (idle)
        t.Start();
        Expect(timer, t.running && !t.Fire(), __LINE__);
        t.start -= 100;
        Expect(timer, !t.Fire(), __LINE__);                // (100 of 500 ms)
        t.start += 100;
        t.start -= 501;
        Expect(timer, t.Fire() && t.start >= now - 5 && !t.Fire() && t.running, __LINE__);
        S_PerfTimer d;
        Expect(timer, d.period == 0 && !d.running, __LINE__);
        ClearStreams();
        C_CarMessage msg(CAR_MSG_RESTART);
        car->SendMessage(msg);
        C_CarMessage msg2(9);
        car->SendMessageEx(msg2);
        Expect(timer, g_streams.size() == 2 && !g_streams[0]->ex && g_streams[1]->ex && g_streams[0]->id == 77 &&
                 g_streams[1]->id == 77 && g_streams[0]->sent && g_streams[1]->sent &&
                 g_streams[0]->bits == std::vector<std::pair<uint32_t, int>>{{6, 4}} &&
                 g_streams[1]->bits == std::vector<std::pair<uint32_t, int>>{{9, 4}}, __LINE__);
        ClearStreams();
    }

    std::copy(std::begin(sounds0), std::end(sounds0), std::begin(v.m_sounds2));
    std::printf("car update: controls %d state %d water %d detail %d update %d tick %d marks %d timer %d\n", controls,
                state, water, detail, update, tick, marks, timer);
    car->m_driver = nullptr;
    delete driver;
    g_gameRole = GAME_ROLE_LOCAL;
    g_flatGround = false;
    // As Report ends: stopped, the effects let go, deleted, the made objects forgotten (the next
    // model's Report counts its own).
    v.Stop();
    for (int ticks = 0; ticks < 20 && std::any_of(g_carEmitters.begin(), g_carEmitters.end(),
                                                  [](const S_CarEmitter &e) { return e.active; });
         ++ticks)
        UpdateCarEffectEmitters(83);
    delete car;                                         // (its marks cleared on the empty network)
    g_made.clear();
    g_images.clear();
    g_meshes = g_meshesReleased = g_materialsMade = g_materialsReleased = 0;
    GetGame()->m_scene = nullptr;
    GetGame()->m_checkpoints = nullptr;
}

// --- the car's callbacks (part 2d-i; game/car_callbacks.cpp) -------------------------------------

// Their surroundings: the debris thrown, the glass shattered, the physics objects hit, the
// destructibles broken, the mines set off, the explosions, the game's prints, the bits read back.
C_TemporaryActorsList g_temporaryActors;
struct S_DebrisMade {
    I3D_frame *frame;
    S_vector pos, dir;
    bool setup = false, wheel = false, fadeOut = false;
    S_DebrisParams params;
    S_WheelDebrisParams wheelParams;
};
std::vector<S_DebrisMade> g_debris;
bool g_noDebris;                                        // CreateDebris finds no visual type
alignas(16) uint8_t g_debrisObjects[16][16];
C_Debris *C_TemporaryActorsList::CreateDebris(I3D_frame *frame, const S_vector &pos, const S_vector &dir)
{
    if (g_noDebris)
        return nullptr;
    S_DebrisMade made;
    made.frame = frame;
    made.pos = pos;
    made.dir = dir;
    g_debris.push_back(made);
    return reinterpret_cast<C_Debris *>(g_debrisObjects[(g_debris.size() - 1) % 16]);
}
S_DebrisMade &DebrisOf(C_Debris *debris)
{
    const size_t i = static_cast<size_t>(reinterpret_cast<uint8_t *>(debris) - &g_debrisObjects[0][0]) / 16;
    return g_debris.at(i);
}
#ifndef ITEM_TEST                                       // (the actors build links the real ones)
void C_Debris::Setup(const S_DebrisParams &params, bool fadeOut)
{
    S_DebrisMade &made = DebrisOf(this);
    made.setup = true;
    made.params = params;
    made.fadeOut = fadeOut;
}
void C_Debris::SetupWheel(const S_WheelDebrisParams &params)
{
    S_DebrisMade &made = DebrisOf(this);
    made.wheel = true;
    made.wheelParams = params;
}
#endif

C_GEffectsModule g_geffects;
std::vector<std::pair<I3D_frame *, float>> g_shattered;
C_GEffectsModule *GetGEffects()
{
    return &g_geffects;
}
void C_GEffectsModule::ShatterGlass(I3D_frame *visual, float unknown)
{
    g_shattered.emplace_back(visual, unknown);
}

S_PhysicsHandle g_noPhysicsHandle;
T_PhysicsObject const g_noPhysicsObject = &g_noPhysicsHandle;
struct S_PhysicsHit {
    T_PhysicsObject object;
    int kind;
    const S_vector *dir;
    S_vector dirValue, extra;
};
std::vector<S_PhysicsHit> g_physicsHits;
C_Module::C_Module(int id, const char *name) : m_name(name), m_id(id) {}
C_Module::~C_Module() {}
// The objects made (the seat weapon's shells, part 3a: the descriptions; each gets a handle of
// its own) and the pushes given.
struct S_PhysicsMade {
    I3D_frame *frame;
    int type;
    T_PhysicsCallback callback;
    void *context;
    int material;
    bool unknown14, collideWithBoxes, startActive, shootThrough;
    int unknown18;
    bool freeSwing, tree, breakable;
    float angle, radius;
    T_PhysicsObject handle;
};
std::vector<S_PhysicsMade> g_physicsMade;
struct S_PhysicsPush {
    T_PhysicsObject object;
    S_vector impulse, point;
    float factor;
};
std::vector<S_PhysicsPush> g_physicsPushes;
S_PhysicsHandle g_physicsHandles[256];
class C_FakePhysics : public C_PhysicsModuleBase {
public:
    C_FakePhysics() : C_PhysicsModuleBase(0, "Physics") {}
    T_PhysicsObject CreateObject(const S_PhysicsObjectDesc &d) override
    {
        T_PhysicsObject handle = &g_physicsHandles[g_physicsMade.size() % 256];
        g_physicsMade.push_back({d.frame, d.type, d.callback, d.callbackContext, d.materialId, d.unknown14,
                                 d.collideWithBoxes, d.startActive, d.shootThrough, d.unknown18, d.door.freeSwing,
                                 d.door.tree, d.door.breakable, d.door.angle, d.radius, handle});
        return handle;
    }
    bool DestroyObject(T_PhysicsObject *) override { return true; }
    void ApplyImpulse(T_PhysicsObject object, const S_vector &impulse, const S_vector &point, float factor) override
    {
        g_physicsPushes.push_back({object, impulse, point, factor});
    }
    bool Hit(T_PhysicsObject object, int kind, const S_vector *dir, void *extra) override
    {
        g_physicsHits.push_back({object, kind, dir, *dir, *static_cast<const S_vector *>(extra)});
        return true;
    }
};
C_PhysicsModuleBase *GetPhysicsModule()
{
    static C_FakePhysics physics;
    return &physics;
}

std::vector<std::tuple<C_VersionMaster *, int, int>> g_versionDamage;
bool C_VersionMaster::DamageAll(int damage, E_VersionEffects effects)
{
    g_versionDamage.emplace_back(this, damage, effects);
    return true;
}

struct S_MineStep {
    C_Mine *mine;
    C_Actor *actor;
    int unused;
};
std::vector<S_MineStep> g_mineSteps;
#ifndef ITEM_TEST                                       // (the actors build links the real mine, part 7b)
void C_Mine::OnSteppedOn(C_Actor *actor, int unused)
{
    g_mineSteps.push_back({this, actor, unused});
}
#endif

struct S_ExplosionMade {
    bool manager;                                       // on g_shotManager
    C_Actor *source;
    S_vector pos;
    float range, damage;
    int sound;
};
struct S_ExplosionSeen {
    bool manager;
    S_vector pos, dir;
    int effect, material, sound;
};
std::vector<S_ExplosionMade> g_explosions;
std::vector<S_ExplosionSeen> g_explosionsShown;
void C_ShotManager::Explode(C_Actor *source, const S_vector &pos, float range, float damage, int soundGroup)
{
    g_explosions.push_back({this == &g_shotManager, source, pos, range, damage, soundGroup});
}
void C_ShotManager::PlayExplosion(const S_vector &pos, const S_vector &dir, int effect, int material, int soundGroup)
{
    g_explosionsShown.push_back({this == &g_shotManager, pos, dir, effect, material, soundGroup});
}

std::vector<std::string> g_prints;
void C_game::Print(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_prints.push_back(text);
}

// What ReadBits and ReadPosition give, in order (a position's floats with bit count -1, as the fake
// WritePosition writes them); ReadBits masks to its count as the real stream does.
std::vector<std::pair<uint32_t, int>> g_readBits;
size_t g_readPos;
bool g_readMismatch;
uint32_t C_BitStream::ReadBits(int bitCount)
{
    if (g_readPos >= g_readBits.size() || g_readBits[g_readPos].second != bitCount) {
        g_readMismatch = true;
        return 0;
    }
    const uint32_t value = g_readBits[g_readPos++].first;
    return bitCount >= 32 ? value : value & ((1u << bitCount) - 1u);
}
void C_NetMessage::WritePosition(const S_vector &pos)
{
    auto *s = reinterpret_cast<S_FakeStream *>(this);
    for (float f : {pos.x, pos.y, pos.z}) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof bits);
        s->bits.emplace_back(bits, -1);
    }
}
void C_NetMessage::ReadPosition(S_vector &pos)
{
    float v[3] = {};
    for (float &f : v) {
        if (g_readPos >= g_readBits.size() || g_readBits[g_readPos].second != -1) {
            g_readMismatch = true;
            return;
        }
        std::memcpy(&f, &g_readBits[g_readPos++].first, sizeof f);
    }
    pos = S_vector(v[0], v[1], v[2]);
}

namespace {

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}
int Ptr(const void *p)
{
    return static_cast<int>(reinterpret_cast<intptr_t>(p));
}
bool SameV(const S_vector &a, const S_vector &b, float eps)
{
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}
S_vector V3(const S_vector4 &v)
{
    return S_vector(v.x, v.y, v.z);
}

// Another actor of any type: its contacts as the stub human's.
class C_FakeActor : public C_Actor {
public:
    explicit C_FakeActor(int type) : C_Actor(static_cast<E_ActorType>(type)) {}
    void CopyFrom(const C_Actor *) override {}
    void StartMission(bool) override {}
    void StopMission(bool) override {}
    void RestartMission(bool) override {}
    void OnCollision(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6) override
    {
        contacts.push_back({false, other, pos, *pos, a3, a4, {}, {}, a5, a6, 0});
        if (a3)
            contacts.back().v3 = *reinterpret_cast<const S_vector *>(static_cast<intptr_t>(a3));
    }
    bool OnCollisionEx(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6, int a7) override
    {
        contacts.push_back({true, other, pos, *pos, a3, a4, {}, {}, a5, a6, a7});
        if (a3)
            contacts.back().v3 = *reinterpret_cast<const S_vector *>(static_cast<intptr_t>(a3));
        return false;
    }
    std::vector<C_Human::S_Contact> contacts;
};

S_CollisionVolume Volume(uint32_t flags, I3D_frame *frame, uint32_t type, C_Actor *actor, int material = 0)
{
    S_CollisionVolume v;
    v.flags = flags | static_cast<uint32_t>(material) << 24;
    v.frame = frame;
    v.type = type;
    v.actor = actor;
    return v;
}

using T_Bits = std::vector<std::pair<uint32_t, int>>;

}  // namespace

// The vehicle's callbacks on a freshly loaded and started model: each branch against what the
// original does (game/car_callbacks.cpp's comments), the messages written and read back.
void CallbacksTest(const char *path)
{
    if (!LoadModel(path))
        return;
    auto *car = new C_Car();
    car->SetFrame(g_model);
    C_Vehicle &v = car->m_vehicle;
    v.Start();
    const int n = static_cast<int>(v.m_seats.size());
    car->SetNetId(77);
    g_gameRole = GAME_ROLE_LOCAL;
    ClearStreams();
    std::vector<C_Human *> crew;
    for (int i = 0; i < n; ++i) {
        crew.push_back(new C_Human());
        crew.back()->SetNetId(40 + i);
    }
    const auto seatCrew = [&] {
        for (int i = 0; i < n; ++i) {
            crew[i]->health = 800;
            crew[i]->hits.clear();
            v.m_seats[i].occupant = crew[i];
        }
        g_hitOrder.clear();
    };
    const auto emptySeats = [&] {
        for (int i = 0; i < n; ++i)
            v.m_seats[i].occupant = nullptr;
    };

    // --- the response.
    bool response = true;
    {
        auto *h = new C_Human();
        C_FakeActor barrel(ACTOR_BARREL);
        const S_vector4 point = {0.3f, 0.5f, 1.2f, 0.0f};
        const S_vector normal(0.0f, 0.6f, 0.8f);
        S_vector4 arm, vel, turn;
        float mass, scale;
        bool push;
        const auto reset = [&](bool p) {
            arm = vel = turn = {9.0f, 9.0f, 9.0f, 9.0f};
            mass = scale = 9.0f;
            push = p;
        };
        S_CollisionVolume hv = Volume(COLLISION_FLAG_DYNAMIC, nullptr, 0, h);
        reset(true);
        Expect(response, CarOnBodyActorResponse(&v, &hv, &point, &normal, &arm, &vel, &mass, &turn, &scale, &push),
               __LINE__);
        Expect(response, Same4(arm, {0, 0, 0, 0}, 0.0f) && Same4(vel, {0, 0, 0, 0}, 0.0f) && mass == 0.0f &&
                             turn.x == 0.0f && turn.y == 0.0f && turn.z == 0.0f && turn.w == 9.0f && scale == -1.0f &&
                             !push, __LINE__);
        // Another car (here the car itself): its vehicle's answer.
        v.m_velocity = {1.0f, -2.0f, 0.5f, 0.0f};
        v.m_unknown210 = S_vector(0.2f, 0.1f, -0.3f);
        S_CollisionVolume cv = Volume(COLLISION_FLAG_DYNAMIC, nullptr, 0, car);
        S_vector4 arm2, vel2, turn2;
        float mass2;
        v.GetContactResponse(point, normal, &arm2, &vel2, &mass2, &turn2);
        reset(false);
        Expect(response, CarOnBodyActorResponse(&v, &cv, &point, &normal, &arm, &vel, &mass, &turn, &scale, &push),
               __LINE__);
        Expect(response, SameV(V3(arm), V3(arm2), 0.0f) && SameV(V3(vel), V3(vel2), 0.0f) &&
                             SameV(V3(turn), V3(turn2), 0.0f) && mass == mass2 && scale == -1.2f && push, __LINE__);
        S_CollisionVolume bv = Volume(COLLISION_FLAG_DYNAMIC, nullptr, 0, &barrel);
        reset(false);
        Expect(response, !CarOnBodyActorResponse(&v, &bv, &point, &normal, &arm, &vel, &mass, &turn, &scale, &push),
               __LINE__);
        Expect(response, push && Same4(arm, {9, 9, 9, 9}, 0.0f) && Same4(vel, {9, 9, 9, 9}, 0.0f) && mass == 9.0f &&
                             scale == 9.0f && Same4(turn, {9, 9, 9, 9}, 0.0f), __LINE__);
        v.m_velocity = {};
        v.m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
        delete h;
    }

    // --- the impulse.
    bool impulse = true;
    {
        I3D_frame *f = NewFrame(FRAME_VISUAL, "touched", nullptr);
        S_FrameData data = {};
        f->m_data = &data;
        C_FakeActor otherCar(ACTOR_CAR), barrel(ACTOR_BARREL);
        auto *toucher = new C_Human();
        auto *hurt = new C_Human();
        hurt->SetNetId(33);
        auto *driver = new C_Human();
        car->m_driver = driver;
        seatCrew();
        const S_vector4 point = {1.0f, 2.0f, 3.0f, 0.0f}, imp = {4.0f, 5.0f, 6.0f, 0.0f};
        const S_vector normal(0.0f, 0.0f, 1.0f);
        const float mass = 1234.5f;
        v.m_velocity = {7.0f, 8.0f, 9.0f, 0.0f};
        S_CollisionVolume vol = Volume(COLLISION_FLAG_DYNAMIC, f, 0, hurt);
        const auto none = [&] {
            return otherCar.contacts.empty() && barrel.contacts.empty() && toucher->contacts.empty() &&
                   hurt->contacts.empty() && hurt->hits.empty() && g_streams.empty();
        };
        // Not a moving actor's volume, no frame, no data, no actor: nothing.
        data.actor = &barrel;
        S_CollisionVolume still = Volume(0, f, 0, hurt);
        CarOnBodyActorImpulse(car, &still, &point, &imp, &normal, mass);
        S_CollisionVolume noFrame = Volume(COLLISION_FLAG_DYNAMIC, nullptr, 0, hurt);
        CarOnBodyActorImpulse(car, &noFrame, &point, &imp, &normal, mass);
        f->m_data = nullptr;
        CarOnBodyActorImpulse(car, &vol, &point, &imp, &normal, mass);
        f->m_data = &data;
        data.actor = nullptr;
        CarOnBodyActorImpulse(car, &vol, &point, &imp, &normal, mass);
        Expect(impulse, none(), __LINE__);
        // A car: OnCollision with the impulse; another actor: OnCollisionEx with the car's velocity
        // and driver.
        data.actor = &otherCar;
        CarOnBodyActorImpulse(car, &vol, &point, &imp, &normal, mass);
        Expect(impulse, otherCar.contacts.size() == 1, __LINE__);
        if (otherCar.contacts.size() == 1) {
            const C_Human::S_Contact &c = otherCar.contacts[0];
            Expect(impulse, !c.ex && c.other == car && c.pos == reinterpret_cast<const S_vector *>(&point) &&
                                c.a3 == Ptr(&imp) && c.a4 == Ptr(&normal) && c.a5 == static_cast<int>(Bits(mass)) &&
                                c.a6 == 0, __LINE__);
        }
        otherCar.contacts.clear();
        data.actor = &barrel;
        CarOnBodyActorImpulse(car, &vol, &point, &imp, &normal, mass);
        Expect(impulse, barrel.contacts.size() == 1, __LINE__);
        if (barrel.contacts.size() == 1) {
            const C_Human::S_Contact &c = barrel.contacts[0];
            Expect(impulse, c.ex && c.other == car && c.pos == reinterpret_cast<const S_vector *>(&point) &&
                                c.a3 == Ptr(&car->GetVelocity()) && SameV(c.v3, S_vector(7, 8, 9), 0.0f) &&
                                c.a4 == Ptr(&normal) && c.a5 == static_cast<int>(Bits(mass)) && c.a6 == 0 &&
                                c.a7 == Ptr(driver), __LINE__);
        }
        barrel.contacts.clear();
        Expect(impulse, none(), __LINE__);
        // A human: the frame's gets the contact, the volume's the damage at 4 m/s.
        data.actor = toucher;
        const auto run = [&](float speed) {
            toucher->contacts.clear();
            hurt->hits.clear();
            ClearStreams();
            v.m_speed = speed;
            CarOnBodyActorImpulse(car, &vol, &point, &imp, &normal, mass);
        };
        const auto touched = [&] {
            if (toucher->contacts.size() != 1)
                return false;
            const C_Human::S_Contact &c = toucher->contacts[0];
            return c.ex && c.other == car && c.a3 == Ptr(&car->GetVelocity()) && c.a4 == Ptr(&normal) &&
                   c.a5 == static_cast<int>(Bits(mass)) && c.a6 == 0 && c.a7 == Ptr(driver);
        };
        const auto hurtOnce = [&] {
            if (hurt->hits.size() != 1 || !crew.size())
                return false;
            const C_Human::S_Hit &h = hurt->hits[0];
            return h.type == HIT_THROWN_OBJECT && h.amount == 1000.0f && SameV(h.pos, V3(point), 0.0f) &&
                   SameV(h.dir, V3(imp), 0.0f) && h.attacker == crew[0] && h.flags == 0;
        };
        run(3.9f);
        Expect(impulse, touched() && hurt->hits.empty() && toucher->hits.empty(), __LINE__);
        run(4.0f);
        Expect(impulse, touched() && (n == 0 || hurtOnce()) && toucher->hits.empty() && g_streams.empty(), __LINE__);
        hurt->health = 0;
        run(6.0f);
        Expect(impulse, touched() && hurt->hits.empty(), __LINE__);
        hurt->health = 800;
        // Network games: none within 3 s of anyone leaving a seat.
        std::vector<int> ids(std::max(n, 1), -1), times(std::max(n, 1), 0);
        car->m_seatLeftIds = ids.data();
        car->m_seatLeftTimes = times.data();
        g_gameRole = GAME_ROLE_SERVER;
        run(5.0f);
        Expect(impulse, touched() && (n == 0 || hurtOnce()), __LINE__);
        if (n > 0) {
            const int now = static_cast<int>(GetPerfTimeMs());
            ids[n - 1] = 9;
            times[n - 1] = now - 2900;
            run(5.0f);
            Expect(impulse, toucher->contacts.empty() && hurt->hits.empty(), __LINE__);
            times[n - 1] = now - 3200;
            run(5.0f);
            Expect(impulse, touched() && hurtOnce(), __LINE__);
            ids[n - 1] = -1;
            times[n - 1] = now;
            run(5.0f);
            Expect(impulse, touched() && hurtOnce(), __LINE__);
            ids[0] = 4;
            times[0] = now - 100;
            run(5.0f);
            Expect(impulse, toucher->contacts.empty() && hurt->hits.empty(), __LINE__);
            ids[0] = -1;
        }
        // Single player ignores who left.
        if (n > 0) {
            g_gameRole = GAME_ROLE_LOCAL;
            ids[0] = 4;
            times[0] = static_cast<int>(GetPerfTimeMs());
            run(5.0f);
            Expect(impulse, touched() && hurtOnce(), __LINE__);
            ids[0] = -1;
        }
        // A client tells the server.
        g_gameRole = GAME_ROLE_CLIENT;
        run(5.0f);
        Expect(impulse, touched() && hurt->hits.empty() && g_streams.size() == 1, __LINE__);
        if (g_streams.size() == 1) {
            const T_Bits want = {{9, 4},           {33, 16},          {1000, 16},        {Bits(4.0f), 32},
                                 {Bits(5.0f), 32}, {Bits(6.0f), 32},  {Bits(1.0f), 32},  {Bits(2.0f), 32},
                                 {Bits(3.0f), 32}};
            Expect(impulse, g_streams[0]->ex && g_streams[0]->id == 77 && g_streams[0]->sent &&
                                g_streams[0]->bits == want, __LINE__);
        }
        run(3.0f);
        Expect(impulse, touched() && g_streams.empty(), __LINE__);
        ClearStreams();
        car->m_seatLeftIds = nullptr;
        car->m_seatLeftTimes = nullptr;
        g_gameRole = GAME_ROLE_LOCAL;
        v.m_speed = 0.0f;
        v.m_velocity = {};
        car->m_driver = nullptr;
        emptySeats();
        f->m_data = nullptr;
        delete toucher;
        delete hurt;
        delete driver;
    }

    // --- the touch.
    bool touch = true;
    {
        auto *h = new C_Human();
        C_FakeActor barrel(ACTOR_BARREL);
        I3D_frame *f = NewFrame(FRAME_VISUAL, "touched", nullptr);
        S_CollisionVolume vol = Volume(COLLISION_FLAG_DYNAMIC, f, 0, h);
        const S_vector4 point = {1.0f, 2.0f, 3.0f, 0.0f};
        S_vector4 vel = {3.0f, -4.0f, 12.0f, 0.0f};
        const S_vector along = V3(v.m_modelAxisZ), across = V3(v.m_modelAxisX);
        const auto at = [&](float dot) { return along * dot + across * std::sqrt(1.0f - dot * dot); };
        const auto push = [&](float speed, const S_vector &normal) {
            h->contacts.clear();
            v.m_speed = speed;
            CarOnBodyActorTouch(car, &vol, &point, &vel, &normal);
            return h->contacts.size();
        };
        const auto pushed = [&](const S_vector &away) {
            if (h->contacts.size() != 1)
                return false;
            const C_Human::S_Contact &c = h->contacts[0];
            return !c.ex && c.other == car && c.pos == reinterpret_cast<const S_vector *>(&point) &&
                   c.a3 == Ptr(&vel) && SameV(c.v4, away, 1e-6f) && c.a5 == static_cast<int>(Bits(v.m_currentMass)) &&
                   c.a6 == Ptr(f);
        };
        const S_vector away(-3.0f / 13.0f, 4.0f / 13.0f, -12.0f / 13.0f);
        Expect(touch, push(2.0f, across) == 1 && pushed(away), __LINE__);
        Expect(touch, push(1.9f, across) == 0, __LINE__);
        Expect(touch, push(1.9f, along) == 1 && pushed(away), __LINE__);
        Expect(touch, push(1.9f, at(0.69f)) == 0, __LINE__);
        Expect(touch, push(1.9f, at(0.71f)) == 1, __LINE__);
        Expect(touch, push(1.9f, at(0.71f) * -1.0f) == 1, __LINE__);
        // Standing still: up; nearly: the sign of the first axis not zero.
        vel = {0.0f, 0.0f, 0.0f, 0.0f};
        Expect(touch, push(3.0f, across) == 1 && pushed(S_vector(0.0f, 1.0f, 0.0f)), __LINE__);
        vel = {0.0f, 1e-5f, 0.0f, 0.0f};
        Expect(touch, push(3.0f, across) == 1 && pushed(S_vector(0.0f, -1.0f, 0.0f)), __LINE__);
        vel = {1e-5f, -1e-5f, 0.0f, 0.0f};
        Expect(touch, push(3.0f, across) == 1 && pushed(S_vector(-1.0f, 1e-5f, 0.0f)), __LINE__);
        vel = {0.0f, 1e-5f, -1e-5f, 0.0f};
        Expect(touch, push(3.0f, across) == 1 && pushed(S_vector(0.0f, -1e-5f, 1.0f)), __LINE__);
        vel = {0.0f, 1e-5f, 1e-5f, 0.0f};
        Expect(touch, push(3.0f, across) == 1 && pushed(S_vector(0.0f, -1e-5f, -1.0f)), __LINE__);
        vel = {0.0f, 0.0f, 0.0f, 0.0f};
        vel.x = 0.6f;
        vel.y = 0.8f;
        Expect(touch, push(3.0f, across) == 1 && pushed(S_vector(-0.6f, -0.8f, 0.0f)), __LINE__);
        // (A square of 0.72: scaled too.)
        vel.y = 0.6f;
        Expect(touch, push(3.0f, across) == 1 && pushed(S_vector(-0.70710678f, -0.70710678f, 0.0f)), __LINE__);
        // Not a human: nothing.
        S_CollisionVolume bv = Volume(COLLISION_FLAG_DYNAMIC, f, 0, &barrel);
        v.m_speed = 5.0f;
        CarOnBodyActorTouch(car, &bv, &point, &vel, &along);
        S_CollisionVolume nv = Volume(COLLISION_FLAG_DYNAMIC, f, 0, nullptr);
        CarOnBodyActorTouch(car, &nv, &point, &vel, &along);
        Expect(touch, barrel.contacts.empty(), __LINE__);
        v.m_speed = 0.0f;
        delete h;
    }

    // --- whether an actor is firm.
    bool firm = true;
    {
        S_CollisionVolume still = Volume(0, nullptr, 0, nullptr);
        Expect(firm, CarIsActorFirm(car, &still, nullptr), __LINE__);
        S_CollisionVolume none = Volume(COLLISION_FLAG_DYNAMIC, nullptr, 0, nullptr);
        Expect(firm, !CarIsActorFirm(car, &none, nullptr), __LINE__);
        car->SetPosition(S_vector(5.0f, 1.0f, -2.0f));
        const std::set<int> soft = {0, 1, ACTOR_MINE, ACTOR_CAR, ACTOR_SPAWN_POINT, ACTOR_ITEM, ACTOR_DEBRIS,
                                    ACTOR_EXPLOSIVE_PLACE, ACTOR_FOWL_GENERATOR, ACTOR_FISH, ACTOR_NULL, ACTOR_MP_ZONE};
        for (int type = 0; type <= 45; ++type) {
            C_FakeActor a(type);
            a.SetPosition(S_vector(0.0f, 1.2f, 0.0f));
            S_CollisionVolume av = Volume(COLLISION_FLAG_DYNAMIC, nullptr, 0, &a);
            const bool special = type == ACTOR_HUMAN || type == ACTOR_MELEE_TRACKER;
            for (float speed : {0.0f, 3.9f, 4.0f, 9.0f}) {
                v.m_speed = speed;
                bool deform = true;
                const bool want = special ? speed < 4.0f : !soft.count(type);
                Expect(firm, CarIsActorFirm(car, &av, &deform) == want && deform, __LINE__);
            }
        }
        // A human not below the car's position + 0.3: not firm (a meleeTracker is).
        C_FakeActor h(ACTOR_HUMAN), m(ACTOR_MELEE_TRACKER);
        S_CollisionVolume hv = Volume(COLLISION_FLAG_DYNAMIC, nullptr, 0, &h);
        S_CollisionVolume mv = Volume(COLLISION_FLAG_DYNAMIC, nullptr, 0, &m);
        v.m_speed = 1.0f;
        for (float y : {1.0f + 0.3f, 1.5f, 9.0f}) {
            h.SetPosition(S_vector(5.0f, y, -2.0f));
            m.SetPosition(S_vector(5.0f, y, -2.0f));
            Expect(firm, !CarIsActorFirm(car, &hv, nullptr) && CarIsActorFirm(car, &mv, nullptr), __LINE__);
        }
        h.SetPosition(S_vector(5.0f, 1.29f, -2.0f));
        Expect(firm, CarIsActorFirm(car, &hv, nullptr), __LINE__);
        car->SetPosition(S_vector(5.0f, 1.2f, -2.0f));
        Expect(firm, CarIsActorFirm(car, &hv, nullptr), __LINE__);
        v.m_speed = 0.0f;
        car->SetPosition(S_vector(0.0f, 0.0f, 0.0f));
    }

    // --- the impacts.
    bool impact = true;
    {
        const float m = v.m_currentMass;
        const S_vector up = V3(v.m_modelAxisY), side = V3(v.m_modelAxisX);
        const S_vector4 point = {0.5f, 1.5f, 2.5f, 0.0f};
        C_FakeActor other(ACTOR_CAR);
        // An impulse of `perKg` per kg at `angle` from the model's y axis.
        const auto impulseOf = [&](float perKg, float angle) {
            return (up * std::cos(angle) + side * std::sin(angle)) * (perKg * m);
        };
        // The damage the callback works out (the order of the sums as the original's).
        const auto damageOf = [&](const S_vector &i, float factor) {
            const float square = (i.z * i.z + i.x * i.x) + i.y * i.y;
            return std::sqrt(square) / m * factor;
        };
        const auto hitAll = [&](float amount, const S_vector &i) {
            if (static_cast<int>(g_hitOrder.size()) != n)
                return false;
            for (int s = 0; s < n; ++s) {
                if (g_hitOrder[s] != crew[n - 1 - s] || crew[s]->hits.size() != 1)
                    return false;
                const C_Human::S_Hit &h = crew[s]->hits[0];
                if (h.type != HIT_THROWN_OBJECT || std::fabs(h.amount - amount) > amount * 1e-5f ||
                    !SameV(h.pos, V3(point), 0.0f) || !SameV(h.dir, i, 0.0f) || h.attacker != crew[0] || h.flags)
                    return false;
            }
            return true;
        };
        const auto hitNone = [&] {
            for (C_Human *c : crew)
                if (!c->hits.empty())
                    return false;
            return true;
        };
        const auto impactOf = [&](const S_vector &i, C_Actor *actor, const S_CollisionVolume *volume) {
            seatCrew();
            g_prints.clear();
            ClearStreams();
            CarOnVehicleImpact(car, &i, &i, &point, 0.2f, &i, actor, volume, 0);
        };
        // Single player: over 20 the crew, the last seat first, the driver the culprit.
        S_vector i = impulseOf(4.2f, 0.5f);
        impactOf(i, nullptr, nullptr);
        Expect(impact, hitAll(damageOf(i, 5.0f), i) && g_prints.size() == 1 && g_streams.empty(), __LINE__);
        if (g_prints.size() == 1) {
            char want[128];
            std::snprintf(want, sizeof want, "Damage driver - Damage :%.2f, impuls %.2f ", damageOf(i, 5.0f),
                          damageOf(i, 1.0f));
            Expect(impact, g_prints[0] == want, __LINE__);
        }
        i = impulseOf(3.8f, 0.5f);
        impactOf(i, nullptr, nullptr);
        Expect(impact, hitNone() && g_prints.empty(), __LINE__);
        // Against an actor 3 times.
        i = impulseOf(7.0f, 1.0f);
        impactOf(i, &other, nullptr);
        Expect(impact, hitAll(damageOf(i, 3.0f), i), __LINE__);
        i = impulseOf(6.5f, 1.0f);
        impactOf(i, &other, nullptr);
        Expect(impact, hitNone(), __LINE__);
        // The roof (over 2.7 rad): 15 times, or 1000 with +0x2289 clear; not against an actor.
        const uint8_t made = v.m_unknown2289;
        v.m_unknown2289 = 1;
        i = impulseOf(0.3f, 2.8f);
        impactOf(i, nullptr, nullptr);
        Expect(impact, hitAll(damageOf(i, 75.0f), i), __LINE__);
        i = impulseOf(0.25f, 2.8f);
        impactOf(i, nullptr, nullptr);
        Expect(impact, hitNone(), __LINE__);
        i = impulseOf(0.3f, 2.6f);
        impactOf(i, nullptr, nullptr);
        Expect(impact, hitNone(), __LINE__);
        i = impulseOf(5.0f, 2.8f);
        impactOf(i, &other, nullptr);
        Expect(impact, hitNone(), __LINE__);
        i = impulseOf(7.0f, 2.8f);
        impactOf(i, &other, nullptr);
        Expect(impact, hitAll(damageOf(i, 3.0f), i), __LINE__);
        v.m_unknown2289 = 0;
        i = impulseOf(0.01f, 3.0f);
        impactOf(i, nullptr, nullptr);
        Expect(impact, hitAll(1000.0f, i) && g_prints.size() == 1, __LINE__);
        i = impulseOf(0.01f, 2.6f);
        impactOf(i, nullptr, nullptr);
        Expect(impact, hitNone(), __LINE__);
        v.m_unknown2289 = made;
        // A (nearly) zero impulse has no size, however light the car.
        const float mass = v.m_currentMass;
        v.m_currentMass = 0.01f;
        i = S_vector(5e-5f, 0.0f, 0.0f);
        impactOf(i, nullptr, nullptr);
        Expect(impact, hitNone() && g_prints.empty(), __LINE__);
        v.m_currentMass = mass;
        // Single player: whoever drives.
        i = impulseOf(4.2f, 0.5f);
        if (n > 0) {
            crew[0]->role = HUMAN_ROLE_PLAYER;
            impactOf(i, nullptr, nullptr);
            Expect(impact, hitAll(damageOf(i, 5.0f), i), __LINE__);
            crew[0]->role = HUMAN_ROLE_ENEMY;
        }
        // A dead one is left out; the human taken as the world spares them all.
        if (n > 1) {
            seatCrew();
            crew[n - 1]->health = 0;
            g_prints.clear();
            CarOnVehicleImpact(car, &i, &i, &point, 0.2f, &i, nullptr, nullptr, 0);
            Expect(impact, crew[n - 1]->hits.empty() && crew[0]->hits.size() == 1 &&
                               static_cast<int>(g_hitOrder.size()) == n - 1, __LINE__);
        }
        auto *walker = new C_Human();
        C_FakeActor barrel(ACTOR_BARREL);
        S_CollisionVolume hv = Volume(COLLISION_FLAG_DYNAMIC, nullptr, 0, walker);
        impactOf(i, nullptr, &hv);
        Expect(impact, hitNone() && g_prints.size() == 1, __LINE__);
        for (S_CollisionVolume vol : {Volume(COLLISION_FLAG_DYNAMIC, nullptr, 0, &barrel), Volume(0, nullptr, 0, walker),
                                      Volume(COLLISION_FLAG_DYNAMIC, nullptr, 0, nullptr)}) {
            impactOf(i, nullptr, &vol);
            Expect(impact, hitAll(damageOf(i, 5.0f), i), __LINE__);
        }
        // Network games: the local driver's client tells the server (no human: -1).
        g_gameRole = GAME_ROLE_CLIENT;
        if (n > 0) {
            crew[0]->localPlayer = true;
            impactOf(i, nullptr, nullptr);
            Expect(impact, hitNone() && g_prints.size() == 1 && g_streams.size() == 1, __LINE__);
            if (g_streams.size() == 1) {
                const int amount = static_cast<int>(damageOf(i, 5.0f));
                const T_Bits want = {{9, 4},           {0xFFFFFFFFu, 16}, {static_cast<uint32_t>(amount), 16},
                                     {Bits(i.x), 32},  {Bits(i.y), 32},   {Bits(i.z), 32},
                                     {Bits(0.5f), 32}, {Bits(1.5f), 32},  {Bits(2.5f), 32}};
                Expect(impact, g_streams[0]->ex && g_streams[0]->id == 77 && g_streams[0]->sent &&
                                   g_streams[0]->bits == want, __LINE__);
            }
            crew[0]->localPlayer = false;
            impactOf(i, nullptr, nullptr);
            Expect(impact, hitNone() && g_prints.empty() && g_streams.empty(), __LINE__);
        }
        emptySeats();
        g_prints.clear();
        CarOnVehicleImpact(car, &i, &i, &point, 0.2f, &i, nullptr, nullptr, 0);
        Expect(impact, g_prints.empty() && g_streams.empty(), __LINE__);
        // The server: not when a player drives (their client tells it).
        g_gameRole = GAME_ROLE_SERVER;
        if (n > 0) {
            crew[0]->role = HUMAN_ROLE_PLAYER;
            impactOf(i, nullptr, nullptr);
            Expect(impact, hitNone() && g_prints.empty(), __LINE__);
            crew[0]->localPlayer = true;
            impactOf(i, nullptr, nullptr);
            Expect(impact, hitNone() && g_prints.empty(), __LINE__);
            crew[0]->localPlayer = false;
            crew[0]->role = HUMAN_ROLE_ENEMY;
            impactOf(i, nullptr, nullptr);
            Expect(impact, hitAll(damageOf(i, 5.0f), i) && g_streams.empty(), __LINE__);
        }
        emptySeats();
        g_prints.clear();
        CarOnVehicleImpact(car, &i, &i, &point, 0.2f, &i, nullptr, nullptr, 0);
        Expect(impact, g_prints.size() == 1, __LINE__);
        g_gameRole = GAME_ROLE_LOCAL;
        emptySeats();
        ClearStreams();
        delete walker;
    }

    // --- a part off.
    bool partOff = true;
    {
        std::vector<S_VehiclePart> parts;
        std::vector<S_VehicleLamp> lamps;
        std::swap(parts, v.m_parts);
        std::swap(lamps, v.m_lamps);
        I3D_frame *pf = NewFrame(FRAME_VISUAL, "thrown", nullptr);
        pf->m_localMatrix.Identity();
        pf->m_localMatrix(2) = S_vector(1.0f, 2.0f, 2.0f);
        pf->m_localMatrix(3) = S_vector(5.0f, 6.0f, 7.0f);
        I3D_frame *qf = NewFrame(FRAME_VISUAL, "under", pf), *rf = NewFrame(FRAME_VISUAL, "under off", pf);
        I3D_frame *sf = NewFrame(FRAME_VISUAL, "elsewhere", nullptr);
        S_VehiclePart part = {};
        v.m_parts.assign(5, part);
        v.m_parts[0].frame = pf;
        v.m_parts[1].frame = qf;
        v.m_parts[2].frame = rf;
        v.m_parts[2].state = VEHICLE_PART_STATE_1;
        v.m_parts[3].frame = sf;
        S_VehicleLamp lamp = {};
        const uint16_t lampParts[] = {1, 2, 1, 0xFFFF, 3, 4};
        for (uint16_t p : lampParts) {
            lamp.part = p;
            v.m_lamps.push_back(lamp);
        }
        const S_vector vel(1.0f, -2.0f, 3.0f), push(10.0f, 20.0f, -40.0f);
        const auto reset = [&] {
            v.m_parts[0].state = 0;
            pf->m_frameFlags |= 1u;
            for (S_VehicleLamp &l : v.m_lamps) {
                l.health = 50.0f;
                l.flags = 0;
            }
            v.m_lamps[2].flags = VEHICLE_LAMP_BROKEN;
            g_debris.clear();
            g_frameUpdates.clear();
        };
        const auto lampsAsBefore = [&](int broken) {
            for (int l = 0; l < static_cast<int>(v.m_lamps.size()); ++l) {
                const bool want = l == broken || l == 2;
                if ((v.m_lamps[l].flags == VEHICLE_LAMP_BROKEN) != want ||
                    v.m_lamps[l].health != (l == broken ? 0.0f : 50.0f))
                    return false;
            }
            return true;
        };
        g_countUpdates = true;
        const float weights[11] = {0, 0, 10, 0, 2, 4, 7, 2, 20, 0, 0};
        for (uint8_t type = 0; type <= 10; ++type) {
            v.m_parts[0].type = type;
            for (const S_vector *p : {&push, static_cast<const S_vector *>(nullptr)}) {
                reset();
                const bool off = CarOnPartOff(car, 0, &vel, p);
                if (!weights[type]) {
                    Expect(partOff, !off && g_debris.empty() && v.m_parts[0].state == 0 && pf->IsOn() &&
                                        lampsAsBefore(-1) && g_frameUpdates.empty(), __LINE__);
                    continue;
                }
                const float w = weights[type];
                S_vector want = vel;
                if (p)
                    want = S_vector(1.0f / w * push.x + vel.x, vel.y + 1.0f / w * push.y, vel.z + 1.0f / w * push.z);
                Expect(partOff, off && v.m_parts[0].state == VEHICLE_PART_STATE_1 && !pf->IsOn() &&
                                    g_frameUpdates[pf] == 1 && lampsAsBefore(0) && g_debris.size() == 1, __LINE__);
                if (g_debris.size() != 1)
                    continue;
                const S_DebrisMade &d = g_debris[0];
                const S_DebrisParams &q = d.params;
                Expect(partOff, d.frame == pf && SameV(d.pos, S_vector(5, 6, 7), 0.0f) &&
                                    SameV(d.dir, S_vector(1.0f / 3, 2.0f / 3, 2.0f / 3), 1e-6f) && d.setup && !d.wheel &&
                                    d.fadeOut && q.weight == w && q.friction == 0.5f &&
                                    SameV(V3(q.velocity), want, 0.0f) && q.velocity.w == 0.0f && !q.callback &&
                                    !q.context && !q.moveCollision && !q.unknown41 && Same4(q.unknown00, {}, 0.0f) &&
                                    Same4(q.unknown10, {}, 0.0f), __LINE__);
            }
        }
        // Of two lamps on parts under it the last breaks.
        v.m_parts[0].type = VEHICLE_PART_DOOR;
        lamp.part = 1;
        v.m_lamps.push_back(lamp);
        reset();
        CarOnPartOff(car, 0, &vel, nullptr);
        Expect(partOff, lampsAsBefore(6), __LINE__);
        v.m_lamps.pop_back();
        // A frame that is not a visual: nothing thrown, still off; no copy: none set up.
        I3D_frame *dummy = NewFrame(FRAME_DUMMY, "dummy", nullptr);
        dummy->m_frameFlags |= 1u;
        v.m_parts[0].frame = dummy;
        reset();
        Expect(partOff, CarOnPartOff(car, 0, &vel, nullptr) && g_debris.empty() && !dummy->IsOn() &&
                            v.m_parts[0].state == VEHICLE_PART_STATE_1, __LINE__);
        v.m_parts[0].frame = pf;
        reset();
        g_noDebris = true;
        Expect(partOff, CarOnPartOff(car, 0, &vel, nullptr) && g_debris.empty() && !pf->IsOn() && lampsAsBefore(0),
               __LINE__);
        g_noDebris = false;
        g_countUpdates = false;
        std::swap(parts, v.m_parts);
        std::swap(lamps, v.m_lamps);
        g_debris.clear();
    }

    // --- a wheel off.
    bool wheelOff = true;
    {
        int w = 0;
        while (w < v.m_wheelCount && !v.m_wheels[w])
            ++w;
        if (w < v.m_wheelCount) {
            S_VehicleWheel &wh = *v.m_wheels[w];
            const S_VehicleWheel saved = wh;
            const bool wasOn = wh.frame->IsOn();
            wh.unknown1B0 = 3.5f;
            wh.unknown1C0 = 4.5f;
            const S_vector vel(1.0f, -2.0f, 3.0f), push(10.0f, 20.0f, -40.0f);
            auto *driver = new C_Human();
            const auto off = [&](const S_vector *p, float health) {
                wh.flags = saved.flags & ~WHEEL_DETACHED;
                wh.health = health;
                wh.frame->m_frameFlags |= 1u;
                g_debris.clear();
                ClearStreams();
                return CarOnWheelOff(car, w, &vel, p);
            };
            const auto hidden = [&] { return (wh.flags & WHEEL_DETACHED) && !wh.frame->IsOn(); };
            const auto sent = [&](bool ex, int health) {
                if (g_streams.size() != 1)
                    return false;
                T_Bits want = {{2, 4}, {static_cast<uint32_t>(w), 8}, {static_cast<uint32_t>(health), 10}};
                if (health <= 0)
                    for (float f : {vel.x, vel.y, vel.z})
                        want.emplace_back(Bits(f), -1);
                return g_streams[0]->ex == ex && g_streams[0]->id == 77 && g_streams[0]->sent && g_streams[0]->bits == want;
            };
            const bool visual = wh.frame->GetType() == FRAME_VISUAL;
            const auto thrown = [&](const S_vector &want) {
                if (!visual)
                    return g_debris.empty();
                if (g_debris.size() != 1)
                    return false;
                const S_DebrisMade &d = g_debris[0];
                const S_WheelDebrisParams &q = d.wheelParams;
                return d.frame == wh.frame && d.wheel && !d.setup && SameV(d.pos, wh.frame->GetWorldPos(), 0.0f) &&
                       SameV(d.dir, wh.frame->GetWorldDir(), 0.0f) && SameV(V3(q.velocity), want, 0.0f) &&
                       q.velocity.w == 0.0f && q.radius == wh.radius && q.width == wh.width && q.weight == 10.0f &&
                       q.unknown1C == 3.5f && q.unknown20 == 4.5f && !q.callback && !q.moveCollision;
            };
            // A server without the view: hidden, the clients told.
            g_gameRole = GAME_ROLE_SERVER;
            Expect(wheelOff, off(&push, 0.0f) && hidden() && g_debris.empty() && sent(false, 0), __LINE__);
            Expect(wheelOff, off(&push, 7.9f) && hidden() && sent(false, 7), __LINE__);
            // Shown: thrown with a tenth of the push.
            g_gameRole = GAME_ROLE_LOCAL;
            const S_vector pushed(push.x * 0.1f + vel.x, vel.y + push.y * 0.1f, vel.z + push.z * 0.1f);
            Expect(wheelOff, off(&push, 0.0f) && hidden() && thrown(pushed) && sent(false, 0), __LINE__);
            Expect(wheelOff, off(nullptr, -2.5f) && hidden() && thrown(vel) && sent(false, -2), __LINE__);
            g_noDebris = true;
            Expect(wheelOff, off(nullptr, 0.0f) && hidden() && g_debris.empty() && sent(false, 0), __LINE__);
            g_noDebris = false;
            // A client: the local driver's tells the server.
            g_gameRole = GAME_ROLE_CLIENT;
            Expect(wheelOff, off(&push, 0.0f) && hidden() && thrown(pushed) && g_streams.empty(), __LINE__);
            if (n > 0) {
                v.m_seats[0].occupant = driver;
                driver->SetNetId(12);
                g_localPlayerSlot = 12;
                Expect(wheelOff, off(&push, 0.0f) && hidden() && sent(true, 0), __LINE__);
                g_localPlayerSlot = 13;
                Expect(wheelOff, off(&push, 0.0f) && hidden() && g_streams.empty(), __LINE__);
                v.m_seats[0].occupant = nullptr;
            }
            g_gameRole = GAME_ROLE_LOCAL;
            g_localPlayerSlot = 0;
            wh.flags = saved.flags;
            wh.health = saved.health;
            wh.unknown1B0 = saved.unknown1B0;
            wh.unknown1C0 = saved.unknown1C0;
            if (wasOn)
                wh.frame->m_frameFlags |= 1u;
            else
                wh.frame->m_frameFlags &= ~1u;
            g_debris.clear();
            ClearStreams();
            delete driver;
        }
    }

    // --- glass broken.
    bool glass = true;
    {
        std::vector<S_VehiclePart> parts;
        std::swap(parts, v.m_parts);
        static int mesh;
        S_VehiclePart part = {};
        v.m_parts.assign(4, part);
        I3D_frame *frames[3];
        for (int i = 0; i < 3; ++i) {
            frames[i] = NewFrame(FRAME_VISUAL, "glass", nullptr);
            frames[i]->m_frameFlags |= 1u;
            v.m_parts[i].frame = frames[i];
            v.m_parts[i].mesh = reinterpret_cast<I3D_mesh *>(&mesh);
        }
        frames[1]->m_frameFlags &= ~1u;
        v.m_parts[2].mesh = nullptr;
        v.m_parts[3].mesh = reinterpret_cast<I3D_mesh *>(&mesh);
        g_shattered.clear();
        for (int i = 3; i >= 0; --i)
            CarOnGlassBroken(car, i, nullptr, nullptr);
        Expect(glass, g_shattered.size() == 1 && g_shattered[0].first == frames[0] && g_shattered[0].second == 0.01f &&
                          !frames[0]->IsOn() && frames[2]->IsOn(), __LINE__);
        g_shattered.clear();
        std::swap(parts, v.m_parts);
    }

    // --- the body's ground.
    bool bodyGround = true;
    {
        I3D_frame *f = NewFrame(FRAME_VISUAL, "object", nullptr);
        S_FrameData data = {};
        data.physicsObject = g_noPhysicsObject;
        f->m_data = &data;
        S_PhysicsHandle handle;
        alignas(16) static uint8_t master[64];
        v.m_velocity = {2.0f, 0.5f, -1.0f, 0.0f};
        const auto met = [&](uint32_t flags, uint32_t type, int material) {
            g_physicsHits.clear();
            g_versionDamage.clear();
            S_CollisionVolume hit = Volume(flags, f, type, nullptr, material);
            S_CollisionVolume *got = CarOnBodyGround(car, &hit, nullptr, nullptr);
            return got == &hit ? 1 : got ? 2 : 0;
        };
        for (int water : {MATERIAL_WADING_WATER, MATERIAL_SWIMMING_WATER, MATERIAL_COLD_WATER})
            for (uint32_t flags : {0u, static_cast<uint32_t>(COLLISION_FLAG_DYNAMIC)})
                Expect(bodyGround, met(flags, 0, water) == 0, __LINE__);
        Expect(bodyGround, met(0, 0, 0x47) == 1 && met(0, COLLISION_TYPE_LIGHT_OBJECT, 5) == 1, __LINE__);
        Expect(bodyGround, met(COLLISION_FLAG_DYNAMIC, 0, 5) == 1 && met(COLLISION_FLAG_DYNAMIC, COLLISION_TYPE_MINE, 5) == 1,
               __LINE__);
        Expect(bodyGround, met(COLLISION_FLAG_DYNAMIC, COLLISION_TYPE_LIGHT_OBJECT, 5) == 0 && g_physicsHits.empty() &&
                               g_versionDamage.empty(), __LINE__);
        // A physics object: hit along the car's velocity (kind 2 in mode +0x550), not met.
        data.physicsObject = &handle;
        for (int mode : {0, 3}) {
            v.m_unknown550 = mode;
            Expect(bodyGround, met(COLLISION_FLAG_DYNAMIC, 0, 5) == 0 && g_physicsHits.size() == 1 &&
                                   g_versionDamage.empty(), __LINE__);
            if (g_physicsHits.size() == 1) {
                const S_PhysicsHit &h = g_physicsHits[0];
                Expect(bodyGround, h.object == &handle && h.kind == (mode ? 2 : PHYS_HIT_KICK) &&
                                       h.dir == &car->GetVelocity() && SameV(h.dirValue, S_vector(2.0f, 0.5f, -1.0f), 0.0f) &&
                                       SameV(h.extra, S_vector(0, 0, 0), 0.0f), __LINE__);
            }
        }
        v.m_unknown550 = 0;
        // A destructible one: broken, not met.
        data.physicsObject = g_noPhysicsObject;
        data.versionMaster = reinterpret_cast<C_VersionMaster *>(master);
        Expect(bodyGround, met(COLLISION_FLAG_DYNAMIC, 0, 5) == 0 && g_physicsHits.empty() &&
                               g_versionDamage.size() == 1 &&
                               g_versionDamage[0] == std::make_tuple(reinterpret_cast<C_VersionMaster *>(master), 1000,
                                                                     static_cast<int>(VERSION_EFFECTS_ALL)), __LINE__);
        data.versionMaster = nullptr;
        f->m_data = nullptr;
        Expect(bodyGround, met(COLLISION_FLAG_DYNAMIC, 0, 5) == 1 && met(COLLISION_FLAG_DYNAMIC, COLLISION_TYPE_LIGHT_OBJECT, 5) == 0,
               __LINE__);
        v.m_velocity = {};
    }

    // --- the wheels' ground.
    bool wheelGround = true;
    {
        int w = 0;
        while (w < v.m_wheelCount && !v.m_wheels[w])
            ++w;
        auto *driver = new C_Human();
        car->m_driver = driver;
        C_FakeActor mine(ACTOR_MINE);
        if (w < v.m_wheelCount)
            v.m_wheels[w]->unknown050 = {3.0f, -0.5f, 8.0f, 1.0f};
        const auto met = [&](uint32_t flags, uint32_t type, int material) {
            g_mineSteps.clear();
            g_explosions.clear();
            g_explosionsShown.clear();
            S_CollisionVolume ground = Volume(flags, nullptr, type, &mine, material);
            S_CollisionVolume *got = CarOnWheelGround(car, &ground, w);
            return got == &ground ? 1 : got ? 2 : 0;
        };
        const auto quiet = [&] { return g_mineSteps.empty() && g_explosions.empty() && g_explosionsShown.empty(); };
        const auto stepped = [&] {
            return g_mineSteps.size() == 1 && g_mineSteps[0].mine == static_cast<C_Mine *>(static_cast<C_Actor *>(&mine)) &&
                   g_mineSteps[0].actor == car && g_mineSteps[0].unused == w && g_explosions.empty();
        };
        const uint32_t dynamic = COLLISION_FLAG_DYNAMIC;
        for (uint32_t role : {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT}) {
            g_gameRole = role;
            for (int water : {MATERIAL_WADING_WATER, MATERIAL_SWIMMING_WATER, MATERIAL_COLD_WATER})
                Expect(wheelGround, met(dynamic, COLLISION_TYPE_MINE, water) == 0 && quiet(), __LINE__);
        }
        if (w < v.m_wheelCount) {
            // The server's part: a mine is set off; single player: a minefield blows up.
            for (uint32_t role : {GAME_ROLE_LOCAL, GAME_ROLE_SERVER}) {
                g_gameRole = role;
                Expect(wheelGround, met(dynamic, COLLISION_TYPE_MINE, 5) == 0 && stepped(), __LINE__);
                Expect(wheelGround, met(0, COLLISION_TYPE_MINE, 5) == 1 && quiet(), __LINE__);
                Expect(wheelGround, met(dynamic, 0, 5) == 1 && quiet(), __LINE__);
            }
            g_gameRole = GAME_ROLE_LOCAL;
            for (int material : {MATERIAL_MINEFIELD, MATERIAL_MINEFIELD_2, MATERIAL_62}) {
                for (unsigned seed : {1u, 2u, 3u, 4u, 5u, 6u}) {
                    std::srand(seed);
                    const int effects[3] = {0x2B, 0x30, 0x31};
                    const int effect = effects[std::rand() % 3];
                    std::srand(seed);
                    Expect(wheelGround, met(0, 0, material) == 1 && g_mineSteps.empty() && g_explosions.size() == 1 &&
                                            g_explosionsShown.size() == 1, __LINE__);
                    if (g_explosions.size() != 1 || g_explosionsShown.size() != 1)
                        continue;
                    const S_ExplosionMade &e = g_explosions[0];
                    const S_ExplosionSeen &s = g_explosionsShown[0];
                    Expect(wheelGround, e.manager && e.source == driver && SameV(e.pos, S_vector(3.0f, -0.5f, 8.0f), 0.0f) &&
                                            e.range == 3.0f && e.damage == 5000.0f && e.sound == 3 && s.manager &&
                                            SameV(s.pos, e.pos, 0.0f) && SameV(s.dir, S_vector(0, 1, 0), 0.0f) &&
                                            s.effect == effect && s.material == material && s.sound == 3, __LINE__);
                }
            }
            Expect(wheelGround, met(0, 0, 0x43) == 1 && met(0, 0, 0x46 + 1) == 1 && quiet(), __LINE__);
            g_gameRole = GAME_ROLE_SERVER;
            Expect(wheelGround, met(0, 0, MATERIAL_MINEFIELD) == 1 && quiet(), __LINE__);
            // Clients, and a destroyed car: neither.
            g_gameRole = GAME_ROLE_CLIENT;
            Expect(wheelGround, met(dynamic, COLLISION_TYPE_MINE, 5) == 1 && met(0, 0, MATERIAL_MINEFIELD) == 1 && quiet(),
                   __LINE__);
            g_gameRole = GAME_ROLE_LOCAL;
            const float engine = v.m_unknown610;
            v.m_unknown610 = 0.0f;
            Expect(wheelGround, met(dynamic, COLLISION_TYPE_MINE, 5) == 1 && met(0, 0, MATERIAL_MINEFIELD) == 1 && quiet(),
                   __LINE__);
            Expect(wheelGround, met(dynamic, COLLISION_TYPE_MINE, MATERIAL_COLD_WATER) == 0, __LINE__);
            v.m_unknown610 = engine;
        }
        g_gameRole = GAME_ROLE_LOCAL;
        car->m_driver = nullptr;
        delete driver;
    }

    // --- the messages written and read back.
    bool messages = true;
    {
        const auto readBack = [&](C_CarMessage &msg) {
            g_readBits.assign(g_streams[0]->bits.begin() + 1, g_streams[0]->bits.end());
            g_readPos = 0;
            g_readMismatch = false;
            static int stream;
            msg.Read(reinterpret_cast<C_BitStream *>(&stream));
            return !g_readMismatch;
        };
        ClearStreams();
        C_CarHitMessage hit(-1, 40000, S_vector(1.5f, -2.25f, 3.0f), S_vector(4.0f, 5.0f, -6.0f));
        car->SendMessage(hit);
        const T_Bits want = {{9, 4},           {0xFFFFFFFFu, 16}, {40000, 16},      {Bits(1.5f), 32}, {Bits(-2.25f), 32},
                             {Bits(3.0f), 32}, {Bits(4.0f), 32},  {Bits(5.0f), 32}, {Bits(-6.0f), 32}};
        Expect(messages, g_streams.size() == 1 && g_streams[0]->bits == want, __LINE__);
        C_CarHitMessage back;
        Expect(messages, g_streams.size() == 1 && readBack(back) && g_readPos == g_readBits.size() && back.human == -1 &&
                             back.amount == 40000 - 65536 && SameV(V3(back.impulse), S_vector(1.5f, -2.25f, 3.0f), 0.0f) &&
                             SameV(V3(back.point), S_vector(4.0f, 5.0f, -6.0f), 0.0f) && back.GetKind() == CAR_MSG_HIT,
               __LINE__);
        ClearStreams();
        C_CarHitMessage hit2(33, 1000, S_vector(0, 0, 1), S_vector(0, 2, 0));
        car->SendMessage(hit2);
        C_CarHitMessage back2;
        Expect(messages, g_streams.size() == 1 && readBack(back2) && back2.human == 33 && back2.amount == 1000 &&
                             SameV(V3(back2.impulse), S_vector(0, 0, 1), 0.0f) && SameV(V3(back2.point), S_vector(0, 2, 0), 0.0f),
               __LINE__);
        const S_vector vel(1.0f, -2.0f, 3.5f);
        for (int health : {0, 1, 12, -2}) {
            ClearStreams();
            C_CarWheelMessage wheel(3, health, vel);
            car->SendMessageEx(wheel);
            T_Bits w = {{2, 4}, {3, 8}, {static_cast<uint32_t>(health), 10}};
            if (health <= 0)
                for (float f : {vel.x, vel.y, vel.z})
                    w.emplace_back(Bits(f), -1);
            Expect(messages, g_streams.size() == 1 && g_streams[0]->ex && g_streams[0]->bits == w, __LINE__);
            if (g_streams.size() != 1)
                continue;
            C_CarWheelMessage wback;
            wback.velocity = {9.0f, 9.0f, 9.0f, 9.0f};
            const bool read = readBack(wback);
            if (health == 0)
                Expect(messages, read && g_readPos == g_readBits.size() && wback.wheel == 3 && wback.health == 0 &&
                                     Same4(wback.velocity, {1.0f, -2.0f, 3.5f, 9.0f}, 0.0f) &&
                                     wback.GetKind() == CAR_MSG_WHEEL, __LINE__);
            else if (health > 0)
                Expect(messages, read && g_readPos == g_readBits.size() && wback.health == health &&
                                     Same4(wback.velocity, {9, 9, 9, 9}, 0.0f), __LINE__);
            else    // (Quirk kept: read back unsigned, without the velocity written.)
                Expect(messages, read && g_readPos == 2 && wback.health == 1022 && Same4(wback.velocity, {9, 9, 9, 9}, 0.0f),
                       __LINE__);
        }
        ClearStreams();
        g_readBits.clear();
    }

    std::printf("callbacks: response %d impulse %d touch %d firm %d impact %d part off %d wheel off %d glass %d "
                "body ground %d wheel ground %d messages %d\n", response, impulse, touch, firm, impact, partOff,
                wheelOff, glass, bodyGround, wheelGround, messages);
    for (C_Human *c : crew)
        delete c;
    g_gameRole = GAME_ROLE_LOCAL;
    v.Stop();
    for (int ticks = 0; ticks < 20 && std::any_of(g_carEmitters.begin(), g_carEmitters.end(),
                                                  [](const S_CarEmitter &e) { return e.active; });
         ++ticks)
        UpdateCarEffectEmitters(83);
    delete car;
    g_made.clear();
    g_images.clear();
    g_meshes = g_meshesReleased = g_materialsMade = g_materialsReleased = 0;
}

// --- the car's hits (part 2d-ii; game/car_hits.cpp) --------------------------------------------------

// Their surroundings: the actors by index and id, the net game's local human, the player's team,
// the script's death, the damage parts' share, the frames' materials, the item's model, the
// scene's ray, a frame's copy and placement, the directions sent, RandFloat (as LS3DF.dll's:
// rand() times the float 0x38000100).
C_ActorsModule g_actorsModule;
std::map<int, C_Actor *> g_actorsByIndex, g_actorsById;
C_Actor *C_ActorsModule::GetActor(int index) const
{
    auto it = g_actorsByIndex.find(index);
    return it == g_actorsByIndex.end() ? nullptr : it->second;
}
C_Actor *C_ActorsModule::GetActorById(int id) const
{
    auto it = g_actorsById.find(id);
    return it == g_actorsById.end() ? nullptr : it->second;
}
C_NetGame g_netGame;
C_Human *g_localHuman;
C_Human *C_NetGame::GetLocalHuman()
{
    return g_localHuman;
}
std::set<const C_Human *> g_playerTeam;
C_Human *g_selectedMember;                              // the player's team's selected member (ReadMessages)
C_Human *C_Team::GetSelectedMember() const
{
    return g_selectedMember;
}
bool g_teamAsked;                                       // IsMember on the player's team
bool C_TeamBase::IsMember(const C_Human *human) const
{
    g_teamAsked = this == g_teamManager.GetPlayerTeam();
    return g_playerTeam.count(human) != 0;
}
int g_deaths;
void C_Script::OnDeath()
{
    ++g_deaths;
}
// The materials (the damage parts' zones; set by the tests).
S_MaterialDef *g_materials;
uint32_t g_materialCount;
std::map<const I3D_frame *, int> g_frameMaterials;
int GetFrameMaterialId(const I3D_frame *frame)
{
    auto it = g_frameMaterials.find(frame);
    return it == g_frameMaterials.end() ? -1 : it->second;
}
I3D_frame *g_itemModel;
I3D_frame *C_ItemDef::GetModel()
{
    return g_itemModel;
}
extern "C" float RandFloat()
{
    const uint32_t bits = 0x38000100;
    float scale;
    std::memcpy(&scale, &bits, sizeof scale);
    return static_cast<float>(std::rand()) * scale;
}
struct S_Intersect {
    S_vector from, dir;
    I3D_frame *frame;
    uint32_t flags;
    int a, b, c;
    // (Part 2g-ii: the frame's on-state at the test, the watched frames', the damage model's.)
    bool frameOn;
    std::vector<bool> watched;
    bool modelOn;
};
std::vector<S_Intersect> g_intersects;
I3D_frame *g_intersectHit;
// The damage parts' tests (part 2g-ii): with g_rayPick set a ray at g_rayModel meets g_rayModelHit,
// one at another frame meets it when it is in g_rayHits; g_rayWatch's on-states logged.
bool g_rayPick;
I3D_frame *g_rayModel, *g_rayModelHit;
std::set<const I3D_frame *> g_rayHits;
std::vector<I3D_frame *> g_rayWatch;
float g_intersectDistance;
S_vector g_intersectNormal;
I3D_frame *__stdcall F_IntersectFrame(void *, const S_vector &from, const S_vector &dir, I3D_frame *frame,
                                      I3D_collision_info *info, uint32_t flags, int a, int b, int c)
{
    std::vector<bool> watched;
    for (const I3D_frame *w : g_rayWatch)
        watched.push_back(w->IsOn());
    g_intersects.push_back({from, dir, frame, flags, a, b, c, frame && frame->IsOn(), watched, g_rayModel && g_rayModel->IsOn()});
    if (g_rayPick)
        return frame == g_rayModel ? g_rayModelHit : g_rayHits.count(frame) ? frame : nullptr;
    if (g_intersectHit) {
        info->distance = g_intersectDistance;
        info->normal = g_intersectNormal;
    }
    return g_intersectHit;
}
std::map<I3D_frame *, I3D_frame *> g_duplicates;
void __stdcall F_Duplicate(I3D_frame *self, I3D_frame *source)
{
    g_duplicates[self] = source;
}
struct S_PosDir {
    S_vector pos, dir;
    float roll;
};
std::map<I3D_frame *, S_PosDir> g_posDirs;
void __stdcall F_SetPosDir(I3D_frame *self, const S_vector &pos, const S_vector &dir, float roll)
{
    g_posDirs[self] = {pos, dir, roll};
}
void C_NetMessage::WriteDirection3D(const S_vector &dir, int playerIndex)
{
    auto *s = reinterpret_cast<S_FakeStream *>(this);
    for (float f : {dir.x, dir.y, dir.z}) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof bits);
        s->bits.emplace_back(bits, playerIndex == -1 ? -2 : -3);
    }
}
void C_NetMessage::ReadDirection3D(S_vector &dir, int playerIndex)
{
    float v[3] = {};
    for (float &f : v) {
        if (g_readPos >= g_readBits.size() || g_readBits[g_readPos].second != (playerIndex == -1 ? -2 : -3)) {
            g_readMismatch = true;
            return;
        }
        std::memcpy(&f, &g_readBits[g_readPos++].first, sizeof f);
    }
    dir = S_vector(v[0], v[1], v[2]);
}
int g_impulseCallbacks;                                 // the vehicle's callback 9 (Wreck's lights)
void *g_impulseContext;
void __fastcall CountImpulse(void *context)
{
    ++g_impulseCallbacks;
    g_impulseContext = context;
}

namespace {

// The vehicle's motion and accumulated force (what a push changes).
struct S_Motion {
    S_vector4 velocity;
    S_vector turn, force, torque;
    bool operator==(const S_Motion &o) const
    {
        return Same4(velocity, o.velocity, 0.0f) && SameV(turn, o.turn, 0.0f) && SameV(force, o.force, 0.0f) &&
               SameV(torque, o.torque, 0.0f);
    }
};
S_Motion MotionOf(const C_Vehicle &v)
{
    return {v.m_velocity, v.m_unknown210, v.m_unknown270, v.m_unknown280};
}
void SetMotion(C_Vehicle &v, const S_Motion &m)
{
    v.m_velocity = m.velocity;
    v.m_unknown210 = m.turn;
    v.m_unknown270 = m.force;
    v.m_unknown280 = m.torque;
}
// Squares and S_vector::Normalize in the car's orders.
float SquareZXY(const S_vector &v) { return (v.z * v.z + v.x * v.x) + v.y * v.y; }
float SquareZYX(const S_vector &v) { return (v.z * v.z + v.y * v.y) + v.x * v.x; }
void NormalizeZXY(S_vector &v)
{
    const float square = SquareZXY(v);
    if (std::fabs(square - 1.0f) < 1e-8f)
        return;
    if (square >= 1e-8f)
        v *= 1.0f / std::sqrt(square);
    else if (v.x != 0.0f)
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    else if (v.z != 0.0f)
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    else
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
}
T_Bits Floats(const S_vector &v, int marker)
{
    return {{Bits(v.x), marker}, {Bits(v.y), marker}, {Bits(v.z), marker}};
}
T_Bits operator+(T_Bits a, const T_Bits &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

}  // namespace

// The contacts, the hits, the wreck, the charges, the ray and the crew's queries on a freshly loaded
// and started model (another car for the pushes), the messages written and read back.
void CarHitsTest(const char *path)
{
    if (!LoadModel(path))
        return;
    auto *car = new C_Car();
    car->SetFrame(g_model);
    C_Vehicle &v = car->m_vehicle;
    v.Start();
    const int n = static_cast<int>(v.m_seats.size());
    car->SetNetId(77);
    g_gameRole = GAME_ROLE_LOCAL;
    g_coop = false;
    g_mpSettingsValue.gameType = MP_GAME_DEATHMATCH;
    ClearStreams();
    g_prints.clear();
    g_sceneVtable[0x104 / 4] = reinterpret_cast<void *>(&F_IntersectFrame);
    g_frameVtable[0x4C / 4] = reinterpret_cast<void *>(&F_Duplicate);
    g_frameVtable[0x14 / 4] = reinterpret_cast<void *>(&F_SetPosDir);
    GetGame()->m_scene = g_carScene;
    static int replication;
    static S_FakeNet netObject;
    g_netReplication = reinterpret_cast<C_NetReplication *>(&replication);
    car->m_netObject = reinterpret_cast<C_NetObject *>(&netObject);
    // No dents nor wear while pushing (the comparisons push twice).
    const uint32_t options = v.m_unknownF6C;
    v.m_unknownF6C &= ~(VEHICLE_SIM_DEFORM | VEHICLE_SIM_IMPACT_DAMAGE);
    std::vector<C_Human *> crew;
    for (int i = 0; i < n; ++i) {
        crew.push_back(new C_Human());
        crew.back()->SetNetId(40 + i);
    }
    const auto seatCrew = [&] {
        for (int i = 0; i < n; ++i) {
            crew[i]->health = 800;
            crew[i]->hits.clear();
            crew[i]->role = HUMAN_ROLE_ENEMY;
            crew[i]->localPlayer = false;
            crew[i]->hostileTo.clear();
            crew[i]->inPlay = true;
            crew[i]->spectating = false;
            crew[i]->team = 0;
            v.m_seats[i].occupant = crew[i];
        }
        g_hitOrder.clear();
    };
    const auto emptySeats = [&] {
        for (int i = 0; i < n; ++i)
            v.m_seats[i].occupant = nullptr;
    };
    const auto readBack = [&](C_CarMessage &msg) {
        g_readBits.assign(g_streams[0]->bits.begin() + 1, g_streams[0]->bits.end());
        g_readPos = 0;
        g_readMismatch = false;
        static int stream;
        msg.Read(reinterpret_cast<C_BitStream *>(&stream));
        return !g_readMismatch;
    };
    int w = 0;
    while (w < v.m_wheelCount && !v.m_wheels[w])
        ++w;

    // --- the messages.
    bool messages = true;
    {
        const S_vector a(0.6f, -0.25f, 0.75f), b(1.5f, 2.0f, -3.0f);
        ClearStreams();
        C_CarGlassMessage glass(5, 37, a);
        car->SendMessage(glass);
        Expect(messages, g_streams.size() == 1 && g_streams[0]->bits == T_Bits{{0, 4}, {5, 10}, {37, 10}} + Floats(a, -2),
               __LINE__);
        C_CarGlassMessage glass2;
        glass2.dir = {9, 9, 9, 9};
        Expect(messages, g_streams.size() == 1 && readBack(glass2) && g_readPos == g_readBits.size() && glass2.part == 5 &&
                             glass2.health == 37 && Same4(glass2.dir, {a.x, a.y, a.z, 9.0f}, 0.0f) &&
                             glass2.GetKind() == CAR_MSG_GLASS, __LINE__);
        for (int health : {0, 1, 20, -1}) {
            ClearStreams();
            C_CarPartMessage part(3, health, a, b);
            car->SendMessageEx(part);
            T_Bits want = {{1, 4}, {3, 10}, {static_cast<uint32_t>(health), 10}};
            if (health <= 0)
                want = want + Floats(a, -1) + Floats(b, -1);
            Expect(messages, g_streams.size() == 1 && g_streams[0]->ex && g_streams[0]->bits == want, __LINE__);
            if (g_streams.size() != 1)
                continue;
            C_CarPartMessage back;
            back.velocity = back.push = {9, 9, 9, 9};
            const bool read = readBack(back);
            if (health == 0)
                Expect(messages, read && g_readPos == g_readBits.size() && back.part == 3 && back.health == 0 &&
                                     Same4(back.velocity, {a.x, a.y, a.z, 9.0f}, 0.0f) &&
                                     Same4(back.push, {b.x, b.y, b.z, 9.0f}, 0.0f) && back.GetKind() == CAR_MSG_PART,
                       __LINE__);
            else if (health > 0)
                Expect(messages, read && g_readPos == g_readBits.size() && back.health == health &&
                                     Same4(back.velocity, {9, 9, 9, 9}, 0.0f) && Same4(back.push, {9, 9, 9, 9}, 0.0f),
                       __LINE__);
            else    // (Quirk kept: read back unsigned, without the vectors written.)
                Expect(messages, read && g_readPos == 2 && back.health == 1023 && Same4(back.velocity, {9, 9, 9, 9}, 0.0f),
                       __LINE__);
        }
        ClearStreams();
        C_CarTyreMessage tyre(4);
        car->SendMessage(tyre);
        C_CarTyreMessage tyre2;
        Expect(messages, g_streams.size() == 1 && g_streams[0]->bits == T_Bits{{3, 4}, {4, 8}} && readBack(tyre2) &&
                             tyre2.wheel == 4 && tyre2.GetKind() == CAR_MSG_TYRE, __LINE__);
        ClearStreams();
        C_CarImpulseMessage impulse({1.0f, 2.0f, 3.0f, 4.0f}, 9);
        car->SendMessage(impulse);
        C_CarImpulseMessage impulse2;
        impulse2.point = {8, 8, 8, 8};
        Expect(messages, g_streams.size() == 1 &&
                             g_streams[0]->bits == T_Bits{{11, 4}} + Floats(S_vector(1, 2, 3), -1) + T_Bits{{9, 10}} &&
                             readBack(impulse2) && g_readPos == g_readBits.size() &&
                             Same4(impulse2.point, {1, 2, 3, 8}, 0.0f) && impulse2.actor == 9 &&
                             impulse2.GetKind() == CAR_MSG_IMPULSE, __LINE__);
        ClearStreams();
        g_readBits.clear();
    }

    // --- where the car is simulated.
    bool simulated = true;
    {
        auto *local = new C_Human();
        const auto is = [&](uint32_t role, C_Human *driver, C_Human *localHuman) {
            g_gameRole = role;
            g_localHuman = localHuman;
            if (n > 0)
                v.m_seats[0].occupant = driver;
            return car->IsSimulatedHere();
        };
        Expect(simulated, is(GAME_ROLE_LOCAL, nullptr, local), __LINE__);
        Expect(simulated, is(GAME_ROLE_SERVER, nullptr, nullptr) && is(GAME_ROLE_CLIENT, nullptr, nullptr), __LINE__);
        Expect(simulated, is(GAME_ROLE_SERVER, nullptr, local) && !is(GAME_ROLE_CLIENT, nullptr, local), __LINE__);
        if (n > 0) {
            crew[0]->role = HUMAN_ROLE_PLAYER;
            Expect(simulated, is(GAME_ROLE_CLIENT, crew[0], crew[0]) && is(GAME_ROLE_SERVER, crew[0], crew[0]), __LINE__);
            Expect(simulated, !is(GAME_ROLE_SERVER, crew[0], local) && !is(GAME_ROLE_CLIENT, crew[0], local), __LINE__);
            crew[0]->role = HUMAN_ROLE_ENEMY;
            Expect(simulated, is(GAME_ROLE_SERVER, crew[0], local) && !is(GAME_ROLE_CLIENT, crew[0], local), __LINE__);
            Expect(simulated, is(GAME_ROLE_LOCAL, crew[0], nullptr), __LINE__);
            crew[0]->role = HUMAN_ROLE_PLAYER;
            Expect(simulated, is(GAME_ROLE_LOCAL, crew[0], local), __LINE__);
            crew[0]->role = HUMAN_ROLE_ENEMY;
        }
        g_gameRole = GAME_ROLE_LOCAL;
        g_localHuman = nullptr;
        emptySeats();
        delete local;
    }

    // --- the contacts: OnCollision and the pushes taken.
    bool collision = true;
    {
        auto *other = new C_Car();
        other->SetNetId(5);
        other->m_vehicle.m_unknown550 = 0;
        other->m_vehicle.m_currentMass = v.m_currentMass * 3.0f;
        other->m_vehicle.m_velocity = {2.0f, -1.0f, 3.0f, 0.0f};
        C_FakeActor barrel(ACTOR_BARREL);
        barrel.SetNetId(6);
        auto *walker = new C_Human();
        walker->SetNetId(7);
        g_actorsByIndex = {{5, other}, {6, &barrel}, {7, walker}};
        const S_Motion rest = {{0.5f, 0.25f, -0.75f, 0.0f}, S_vector(0.1f, -0.2f, 0.05f), S_vector(1, 2, 3),
                               S_vector(-1, 0.5f, 2)};
        SetMotion(v, rest);
        const S_vector4 point = {0.4f, 1.1f, -0.6f, 1.0f};
        const S_vector push(3.0f, -1.0f, 2.0f), normal(0.0f, 0.6f, 0.8f);
        const int mass = static_cast<int>(Bits(1500.0f));
        const auto collide = [&](C_Actor *with) {
            SetMotion(v, rest);
            car->OnCollision(with, reinterpret_cast<const S_vector *>(&point), Ptr(&push), Ptr(&normal), mass, 0);
            const S_Motion got = MotionOf(v);
            SetMotion(v, rest);
            return got;
        };
        // Simulated here: a human's push as a force at the centre of mass (only the turn).
        const S_vector &c = V3(v.m_unknown1B0);
        const S_vector at(c.x, (v.m_modelPos.y - c.y) + c.y, c.z);
        S_Motion got = collide(walker);
        v.AddForce(push * 500.0f, at, 1, S_vector(1.0f, 1.0f, 1.0f));
        S_Motion want = MotionOf(v);
        Expect(collision, got == want && !(got == rest), __LINE__);
        // Another car's through ApplyCarHit.
        got = collide(other);
        SetMotion(v, rest);
        v.ApplyCarHit(other, 1.2f, point, push, normal, mass);
        want = MotionOf(v);
        Expect(collision, got == want && !(got == rest), __LINE__);
        // Destroyed, both in mode +0x550: half the mass, 1.4 times the push.
        const float massWas = v.m_currentMass;
        const float engine = v.m_unknown610;
        v.m_unknown550 = 2;
        other->m_vehicle.m_unknown550 = 1;
        v.m_unknown610 = 0.0f;
        got = collide(other);
        const float massAfter = v.m_currentMass;
        SetMotion(v, rest);
        v.m_currentMass = massWas * 0.5f;
        v.ApplyCarHit(other, 1.2f, point, push * 1.4f, normal, mass);
        v.m_currentMass = massWas;
        want = MotionOf(v);
        Expect(collision, got == want && massAfter == massWas, __LINE__);
        for (int k = 0; k < 3; ++k) {
            v.m_unknown550 = k == 0 ? 0 : 2;
            other->m_vehicle.m_unknown550 = k == 1 ? 0 : 1;
            v.m_unknown610 = k == 2 ? engine : 0.0f;
            got = collide(other);
            SetMotion(v, rest);
            v.ApplyCarHit(other, 1.2f, point, push, normal, mass);
            want = MotionOf(v);
            Expect(collision, got == want, __LINE__);
        }
        v.m_unknown550 = 0;
        other->m_vehicle.m_unknown550 = 0;
        v.m_unknown610 = engine;
        Expect(collision, collide(&barrel) == rest, __LINE__);

        // Not simulated here (a client, a player drives): the server told, at most once a second.
        if (n > 0) {
            seatCrew();
            crew[0]->role = HUMAN_ROLE_PLAYER;
            g_localPlayerSlot = 99;
            g_gameRole = GAME_ROLE_CLIENT;
            car->m_impulseSent = static_cast<int>(GetPerfTimeMs()) - 2000;
            ClearStreams();
            g_prints.clear();
            Expect(collision, collide(other) == rest && g_streams.size() == 1 && g_prints.size() == 1 &&
                                  g_prints[0] == "send clsv car impuls", __LINE__);
            if (g_streams.size() == 1)
                Expect(collision, g_streams[0]->ex &&
                                      g_streams[0]->bits == T_Bits{{11, 4}} + Floats(V3(point), -1) + T_Bits{{5, 10}} &&
                                      std::abs(car->m_impulseSent - static_cast<int>(GetPerfTimeMs())) < 50, __LINE__);
            ClearStreams();
            g_prints.clear();
            Expect(collision, collide(other) == rest && g_streams.empty() && g_prints.empty(), __LINE__);
            car->m_impulseSent -= 900;
            Expect(collision, collide(other) == rest && g_streams.empty(), __LINE__);
            car->m_impulseSent -= 200;
            Expect(collision, collide(other) == rest && g_streams.size() == 1, __LINE__);
            // A server that does not simulate it (a player drives): that client told.
            g_gameRole = GAME_ROLE_SERVER;
            car->m_impulseSent = static_cast<int>(GetPerfTimeMs()) - 2000;
            ClearStreams();
            g_prints.clear();
            Expect(collision, collide(other) == rest && g_streams.size() == 1 && !g_streams[0]->ex &&
                                  g_prints.size() == 1 && g_prints[0] == "send svcl car impuls", __LINE__);
            ClearStreams();
        }

        // TakePush: the local driver's machine or the server for a car without a driver applies
        // the other's velocity (at most this mass' share).
        const auto take = [&](int id) {
            SetMotion(v, rest);
            ClearStreams();
            g_prints.clear();
            car->TakePush(id, point);
            const S_Motion m = MotionOf(v);
            SetMotion(v, rest);
            return m;
        };
        const auto pushedBy = [&](const C_Car &by) {
            SetMotion(v, rest);
            const C_Vehicle &p = by.m_vehicle;
            const float share = p.m_currentMass > v.m_currentMass ? 1.0f : p.m_currentMass / v.m_currentMass;
            const float m = v.m_currentMass;
            v.ApplyImpulse(S_vector(share * p.m_velocity.x * m * 0.3f, share * p.m_velocity.y * m * 0.3f,
                                    share * p.m_velocity.z * m * 0.3f),
                           V3(point), 0, S_vector(1.0f, 1.0f, 1.0f));
            const S_Motion r = MotionOf(v);
            SetMotion(v, rest);
            return r;
        };
        g_gameRole = GAME_ROLE_CLIENT;
        if (n > 0) {
            g_localPlayerSlot = crew[0]->GetNetId();
            Expect(collision, take(5) == pushedBy(*other) && !(take(5) == rest) && g_prints.size() == 1 &&
                                  g_prints[0] == "car do collision", __LINE__);
            other->m_vehicle.m_currentMass = v.m_currentMass * 0.5f;
            Expect(collision, take(5) == pushedBy(*other) && !(take(5) == pushedBy(*car)), __LINE__);
            other->m_vehicle.m_currentMass = v.m_currentMass * 3.0f;
            // (Fix: only a car pushes.)
            Expect(collision, take(6) == rest && g_prints.size() == 1, __LINE__);
            Expect(collision, take(7) == rest && take(8) == rest && g_prints.empty(), __LINE__);
            g_localPlayerSlot = 99;
            Expect(collision, take(5) == rest && g_streams.empty() && g_prints.empty(), __LINE__);
            // The server, a driver: that client told, at most once a second.
            g_gameRole = GAME_ROLE_SERVER;
            car->m_impulseSent = static_cast<int>(GetPerfTimeMs()) - 1500;
            Expect(collision, take(5) == rest && g_streams.size() == 1 && g_prints.size() == 1 &&
                                  g_prints[0] == "send svcl car impuls", __LINE__);
            if (g_streams.size() == 1)
                Expect(collision, !g_streams[0]->ex &&
                                      g_streams[0]->bits == T_Bits{{11, 4}} + Floats(V3(point), -1) + T_Bits{{5, 10}},
                       __LINE__);
            Expect(collision, take(5) == rest && g_streams.empty(), __LINE__);
            car->m_impulseSent = static_cast<int>(GetPerfTimeMs()) - 500;
            Expect(collision, take(5) == rest && g_streams.empty(), __LINE__);
            car->m_impulseSent -= 600;
            Expect(collision, take(5) == rest && g_streams.size() == 1, __LINE__);
        }
        emptySeats();
        g_gameRole = GAME_ROLE_SERVER;
        Expect(collision, take(5) == pushedBy(*other) && g_streams.empty(), __LINE__);
        g_gameRole = GAME_ROLE_CLIENT;
        Expect(collision, take(5) == rest, __LINE__);
        // Simulated here (a server's driverless car) OnCollision does not push by TakePush.
        g_gameRole = GAME_ROLE_SERVER;
        Expect(collision, collide(&barrel) == rest && g_streams.empty(), __LINE__);
        g_gameRole = GAME_ROLE_LOCAL;
        g_localPlayerSlot = 0;
        g_actorsByIndex.clear();
        SetMotion(v, {});
        delete other;
        delete walker;
        ClearStreams();
        g_prints.clear();
    }

    // --- the tyres.
    bool tyre = true;
    if (w < v.m_wheelCount) {
        const S_VehicleWheel &wh = *v.m_wheels[w];
        const float r = wh.radius;
        const S_vector centre = V3(wh.worldPos);
        Expect(tyre, car->IsTyreHit(w, centre + S_vector(0.0f, 0.75f * r, 0.0f), false), __LINE__);
        Expect(tyre, car->IsTyreHit(w, centre + S_vector(0.0f, 0.0f, 0.75f * r), false), __LINE__);
        Expect(tyre, car->IsTyreHit(w, centre + S_vector(0.75f * r, 0.0f, 0.0f), false), __LINE__);
        Expect(tyre, !car->IsTyreHit(w, centre + S_vector(0.0f, 0.65f * r, 0.0f), false), __LINE__);
        Expect(tyre, !car->IsTyreHit(w, centre, false), __LINE__);
        const S_matrix &m = wh.frame->GetMatrix();
        Expect(tyre, car->IsTyreHit(w, S_vector(5.0f, 0.75f * r, 0.0f) * m, true), __LINE__);
        Expect(tyre, car->IsTyreHit(w, S_vector(-3.0f, 0.0f, 0.75f * r) * m, true), __LINE__);
        Expect(tyre, !car->IsTyreHit(w, S_vector(5.0f, 0.65f * r, 0.0f) * m, true), __LINE__);
        Expect(tyre, !car->IsTyreHit(w, S_vector(9.0f, 0.0f, 0.0f) * m, true), __LINE__);
        v.m_unknown550 = 1;
        Expect(tyre, !car->IsTyreHit(w, centre + S_vector(0.0f, 0.75f * r, 0.0f), false), __LINE__);
        v.m_unknown550 = 0;
    }

    // --- the hits.
    bool hit = true;
    {
        std::vector<S_VehiclePart> parts;
        std::swap(parts, v.m_parts);
        I3D_frame *f[3];
        for (int i = 0; i < 3; ++i) {
            f[i] = NewFrame(FRAME_VISUAL, "hit part", nullptr);
            f[i]->m_localMatrix(3) = S_vector(1.0f + i, 0.5f, -2.0f + 0.5f * i);
        }
        S_VehiclePart part = {};
        part.health = 50.0f;
        part.absorbed = 20.0f;
        v.m_parts.assign(4, part);
        v.m_parts[0].frame = f[0];
        v.m_parts[0].type = VEHICLE_PART_BUMPER;
        v.m_parts[1].frame = f[1];
        v.m_parts[1].type = VEHICLE_PART_GLASS;
        v.m_parts[2].frame = f[2];
        v.m_parts[2].type = VEHICLE_PART_DOOR;
        v.m_parts[3].frame = f[0];
        v.m_parts[3].type = VEHICLE_PART_MIRROR;
        auto *shooter = new C_Human();
        shooter->SetNetId(61);
        const S_vector pos(0.5f, 1.0f, 3.0f), dir(0.0f, -0.6f, -0.8f);
        v.m_velocity = {1.0f, 0.5f, -2.0f, 0.0f};
        v.m_unknown210 = S_vector(0.2f, -0.4f, 0.3f);
        const auto shoot = [&](E_HitType type, float amount, C_Actor *attacker, I3D_frame *at) {
            g_intersects.clear();
            car->m_parts.wrecked = true;                // (the parts' own: PartsHitTest)
            ClearStreams();
            g_intersectHit = at;
            v.m_unknown58C = 0;
            v.m_unknown22C0 = 0;
            car->m_lastAttacker = 1234;
            car->Hit(type, amount, pos, dir, attacker, 0);
        };
        const auto shared = [&](E_HitType type, float amount, C_Actor *attacker) {
            (void)type, (void)amount, (void)attacker;
            return v.m_unknown22C0 == 1;
        };
        const auto rayed = [&] {
            return g_intersects.size() == 1 && SameV(g_intersects[0].from, pos, 0.0f) &&
                   SameV(g_intersects[0].dir, dir * 10.0f, 0.0f) && g_intersects[0].frame == g_model &&
                   g_intersects[0].flags == 0 && !g_intersects[0].a && !g_intersects[0].b && !g_intersects[0].c;
        };
        const auto lost = [&](float health, float amount) { return health - (amount - 20.0f * 0.01f * amount) * 0.2f; };
        const auto partBits = [&](int index, float health, const S_vector &velocity, const S_vector &push) {
            T_Bits b = {{1, 4}, {static_cast<uint32_t>(index), 10}, {static_cast<uint32_t>(static_cast<int>(health)), 10}};
            if (static_cast<int>(health) <= 0)
                b = b + Floats(velocity, -1) + Floats(push, -1);
            return b;
        };
        const auto carried = [&](I3D_frame *at) {
            const S_vector arm = at->GetWorldPos() - V3(v.m_unknown1B0);
            const S_vector &t = v.m_unknown210;
            const S_vector c(arm.z * t.y - arm.y * t.z, arm.x * t.z - arm.z * t.x, arm.y * t.x - arm.x * t.y);
            return V3(v.m_velocity) - c;
        };
        // An enemy without anyone hostile to them aboard, an explosion on a wreck: nothing.
        shooter->role = HUMAN_ROLE_ENEMY;
        shoot(HIT_BULLET, 30.0f, shooter, f[1]);
        Expect(hit, g_intersects.empty() && v.m_unknown22C0 == 0 && car->m_lastAttacker == 1234 &&
                        v.m_parts[1].health == 50.0f, __LINE__);
        if (n > 0) {
            seatCrew();
            shooter->hostileTo = {crew[0]};
            shoot(HIT_BULLET, 30.0f, shooter, nullptr);
            Expect(hit, rayed() && shared(HIT_BULLET, 30.0f, shooter) && car->m_lastAttacker == 61, __LINE__);
            shooter->hostileTo.clear();
            emptySeats();
        }
        shooter->role = HUMAN_ROLE_PLAYER;
        car->m_wrecked = true;
        shoot(HIT_EXPLOSION, 30.0f, shooter, f[1]);
        Expect(hit, g_intersects.empty() && v.m_unknown22C0 == 0 && car->m_lastAttacker == 1234, __LINE__);
        shoot(HIT_BULLET, 30.0f, shooter, nullptr);
        Expect(hit, rayed() && shared(HIT_BULLET, 30.0f, shooter) && car->m_lastAttacker == 61 && g_streams.empty(),
               __LINE__);
        car->m_wrecked = false;
        shoot(HIT_EXPLOSION, 30.0f, nullptr, nullptr);
        Expect(hit, rayed() && shared(HIT_EXPLOSION, 30.0f, nullptr) && car->m_lastAttacker == -1 && v.m_unknown58C == 0,
               __LINE__);
        // Glass: the part's health and direction.
        shoot(HIT_BULLET, 30.0f, shooter, f[1]);
        const float glassHealth = lost(50.0f, 30.0f);
        Expect(hit, rayed() && shared(HIT_BULLET, 30.0f, shooter) && v.m_parts[1].health == glassHealth &&
                        v.m_parts[1].state == VEHICLE_PART_STATE_2 && v.m_unknown58C == 2 && g_streams.size() == 1 &&
                        v.m_parts[0].health == 50.0f && v.m_parts[3].health == 50.0f, __LINE__);
        if (g_streams.size() == 1)
            Expect(hit, !g_streams[0]->ex && g_streams[0]->bits == T_Bits{{0, 4}, {1, 10}, {45, 10}} + Floats(dir, -2),
                   __LINE__);
        // The last part of the frame first (a mirror: along the shot, a bullet's hundredth; its
        // health runs out so the push is sent).
        v.m_parts[3].health = 1.0f;
        shoot(HIT_BULLET, 30.0f, shooter, f[0]);
        {
            const float s = 30.0f * 0.01f;
            const S_vector push(s * dir.x, s * (dir.y + 0.1f), s * dir.z);
            Expect(hit, v.m_parts[3].health == 0.0f && v.m_parts[0].health == 50.0f && g_streams.size() == 1 &&
                            g_streams[0]->bits == partBits(3, 0.0f, carried(f[0]), push), __LINE__);
        }
        // Out of health: none left, the vectors sent; a blast's push not scaled down.
        shoot(HIT_EXPLOSION, 400.0f, shooter, f[2]);
        {
            const S_vector push(400.0f * dir.x, 400.0f * (dir.y + 0.1f), 400.0f * dir.z);
            Expect(hit, v.m_parts[2].health == 0.0f && g_streams.size() == 1 &&
                            g_streams[0]->bits == partBits(2, 0.0f, carried(f[2]), push), __LINE__);
        }
        // A bumper (and a wing): up and away from the model's place.
        v.m_parts[3].state = VEHICLE_PART_STATE_1;
        for (uint8_t type : {VEHICLE_PART_BUMPER, VEHICLE_PART_WING}) {
            v.m_parts[0].type = type;
            v.m_parts[0].health = 1.0f;
            shoot(HIT_MELEE, 10.0f, shooter, f[0]);
            const S_vector away = f[0]->GetWorldPos() - v.m_modelMatrix(3);
            const S_vector push(10.0f * (away.x * 0.2f), 10.0f * 1.5f, 10.0f * (away.z * 0.2f));
            Expect(hit, v.m_parts[0].health == 0.0f && v.m_parts[3].health == 0.0f && g_streams.size() == 1 &&
                            g_streams[0]->bits == partBits(0, 0.0f, carried(f[0]), push), __LINE__);
        }
        // All parts off: none.
        for (S_VehiclePart &p : v.m_parts)
            p.state = VEHICLE_PART_STATE_1;
        shoot(HIT_BULLET, 30.0f, shooter, f[1]);
        Expect(hit, g_streams.empty() && v.m_parts[1].health == glassHealth && shared(HIT_BULLET, 30.0f, shooter), __LINE__);
        std::swap(parts, v.m_parts);
        // The wheels: their frame, a frame under the holder, a frame under the wheel's.
        if (w < v.m_wheelCount) {
            S_VehicleWheel &wh = *v.m_wheels[w];
            const uint32_t flagsWas = wh.flags;
            wh.flags &= ~(WHEEL_DETACHED | WHEEL_ON_RIM);
            const auto tyres = [&](I3D_frame *at) {
                T_Bits want;
                std::vector<int> wheels;
                for (int k = v.m_wheelCount; k-- > 0;) {
                    const S_VehicleWheel *o = v.m_wheels[k];
                    if (!o || (o->flags & WHEEL_DETACHED) || (o->flags & WHEEL_ON_RIM))
                        continue;
                    if (o->frame != at && o->holder != at->GetParent() && o->frame != at->GetParent())
                        continue;
                    const bool axle = o->frame != at;
                    float d;
                    if (axle) {
                        S_matrix inverse;
                        inverse.Inverse(o->frame->GetMatrix());
                        const S_vector l = pos * inverse;
                        d = std::sqrt(l.y * l.y + l.z * l.z);
                    } else {
                        d = std::sqrt(SquareZYX(V3(o->worldPos) - pos));
                    }
                    if (d > o->radius * 0.7f)
                        wheels.push_back(k);
                }
                if (g_streams.size() != wheels.size())
                    return false;
                for (size_t k = 0; k < wheels.size(); ++k)
                    if (g_streams[k]->ex || g_streams[k]->bits != T_Bits{{3, 4}, {static_cast<uint32_t>(wheels[k]), 8}})
                        return false;
                return !wheels.empty();
            };
            shoot(HIT_BULLET, 30.0f, shooter, wh.frame);
            Expect(hit, tyres(wh.frame) && shared(HIT_BULLET, 30.0f, shooter), __LINE__);
            for (int k = 0; k < v.m_wheelCount; ++k) {
                const S_VehicleWheel *o = v.m_wheels[k];
                if (!o || (o->flags & (WHEEL_DETACHED | WHEEL_ON_RIM)))
                    continue;
                shoot(HIT_BULLET, 30.0f, shooter, o->frame);
                Expect(hit, tyres(o->frame), __LINE__);
            }
            I3D_frame *hub = NewFrame(FRAME_VISUAL, "hub", wh.holder);
            shoot(HIT_BULLET, 30.0f, shooter, hub);
            Expect(hit, tyres(hub), __LINE__);
            {
                // On the axle's line, 3 out: from the axle no tyre (from the centre it would be).
                const S_vector onAxle = S_vector(3.0f, 0.0f, 0.0f) * wh.frame->GetMatrix();
                g_intersectHit = hub;
                ClearStreams();
                car->Hit(HIT_BULLET, 30.0f, onAxle, dir, shooter, 0);
                bool none = true;
                for (const S_FakeStream *x : g_streams)
                    none = none && x->bits != T_Bits{{3, 4}, {static_cast<uint32_t>(w), 8}};
                Expect(hit, none, __LINE__);
            }
            I3D_frame *bolt = NewFrame(FRAME_VISUAL, "bolt", wh.frame);
            shoot(HIT_BULLET, 30.0f, shooter, bolt);
            Expect(hit, tyres(bolt), __LINE__);
            // A flat tyre, a wheel gone: not again.
            shoot(HIT_BULLET, 30.0f, shooter, wh.frame);
            const size_t all = g_streams.size();
            wh.flags |= WHEEL_ON_RIM;
            shoot(HIT_BULLET, 30.0f, shooter, wh.frame);
            const size_t flat = g_streams.size();
            wh.flags = (flagsWas & ~WHEEL_ON_RIM) | WHEEL_DETACHED;
            shoot(HIT_BULLET, 30.0f, shooter, wh.frame);
            Expect(hit, flat + 1 == all && g_streams.size() + 1 == all, __LINE__);
            // The hub of the tyre: not.
            wh.flags = flagsWas & ~(WHEEL_DETACHED | WHEEL_ON_RIM);
            g_intersectHit = wh.frame;
            const S_vector hubPos = V3(wh.worldPos);
            ClearStreams();
            car->Hit(HIT_BULLET, 30.0f, hubPos, dir, shooter, 0);
            for (const S_FakeStream *s : g_streams)
                Expect(hit, s->bits != T_Bits{{3, 4}, {static_cast<uint32_t>(w), 8}}, __LINE__);
            wh.flags = flagsWas;
        }
        g_intersectHit = nullptr;
        ClearStreams();
        v.m_velocity = {};
        v.m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
        delete shooter;
    }

    // --- the wreck.
    bool wreck = true;
    {
        static int script;
        S_FrameData data = {};
        data.script = reinterpret_cast<C_Script *>(&script);
        g_model->m_data = &data;
        auto *attacker = new C_Human();
        attacker->SetNetId(61);
        g_actorsById = {{61, attacker}};
        auto *driver = new C_Human();
        std::vector<uint32_t> flags0;
        std::vector<bool> on0;
        for (int i = 0; i < v.m_wheelCount; ++i) {
            flags0.push_back(v.m_wheels[i] ? v.m_wheels[i]->flags & ~WHEEL_DETACHED : 0);
            on0.push_back(v.m_wheels[i] && v.m_wheels[i]->frame->IsOn());
        }
        const float engine = v.m_unknown610 > 0.0f ? v.m_unknown610 : 1.0f;
        const auto reset = [&] {
            car->m_wrecked = false;
            car->m_wreckShown = false;
            car->m_respawnTimer.running = false;
            v.m_unknown5B0 = 0xFFFFFFFFu;
            v.m_unknown610 = engine;
            v.m_engineOn = true;
            v.m_fuel = 30.0f;
            for (int i = 0; i < v.m_wheelCount; ++i)
                if (v.m_wheels[i]) {
                    v.m_wheels[i]->flags = flags0[i];
                    v.m_wheels[i]->frame->m_frameFlags = on0[i] ? v.m_wheels[i]->frame->m_frameFlags | 1u
                                                               : v.m_wheels[i]->frame->m_frameFlags & ~1u;
                }
            seatCrew();
            car->m_driver = driver;
            car->m_lastAttacker = 61;
            attacker->experience = 0;
            g_deaths = 0;
            g_impulseCallbacks = 0;
            g_debris.clear();
            ClearStreams();
        };
        v.m_callbacks[VEHICLE_CALLBACK_IMPULSE] = reinterpret_cast<void *>(&CountImpulse);
        const uint32_t lightsOff = ~(VEHICLE_LIGHT_AUTO_INDICATORS | VEHICLE_LIGHT_AUTO_BRAKE | VEHICLE_LIGHT_AUTO_REVERSE |
                                     VEHICLE_LIGHT_FLASHING | VEHICLE_LIGHT_FLASH_LEFT | VEHICLE_LIGHT_FLASH_RIGHT |
                                     VEHICLE_LIGHT_INDICATOR_LEFT | VEHICLE_LIGHT_INDICATOR_RIGHT |
                                     VEHICLE_LIGHT_HEADLIGHTS | VEHICLE_LIGHT_BRAKE | VEHICLE_LIGHT_REVERSE);
        // The wheels that fly off with this seed, and how (the original's order of draws).
        struct S_Thrown {
            int wheel;
            S_vector velocity, push;
        };
        const auto thrownFor = [&](unsigned seed) {
            std::srand(seed);
            std::vector<S_Thrown> out;
            for (int i = 0; i < v.m_wheelCount && i < 6; ++i) {
                const S_VehicleWheel *wheel = v.m_wheels[i];
                if (!wheel || (wheel->flags & WHEEL_DETACHED) || !(std::rand() & 1) || v.m_unknown550)
                    continue;
                S_vector push = V3(wheel->worldPos) - V3(v.m_unknown1B0);
                NormalizeZXY(push);
                const float speed = ((RandFloat() - 0.5f) + 1.0f) * 10.0f;
                push = S_vector(push.x * speed, std::fabs(push.y * speed), push.z * speed);
                S_vector velocity = push;
                NormalizeZXY(velocity);
                const float up = RandFloat() + 2.0f;
                out.push_back({i, S_vector(velocity.x * up, up, velocity.z * up), push});
            }
            std::srand(seed);
            return out;
        };
        const auto crewKilled = [&](C_Actor *by) {
            for (int i = 0; i < n; ++i) {
                if (crew[i]->hits.size() != 1)
                    return false;
                const C_Human::S_Hit &h = crew[i]->hits[0];
                if (h.type != HIT_THROWN_OBJECT || h.amount != -1.0f || !SameV(h.pos, car->GetPosition(), 0.0f) ||
                    !SameV(h.dir, S_vector(0.0f, 1.0f, 0.0f), 0.0f) || h.attacker != by || h.flags)
                    return false;
                if (g_hitOrder[i] != crew[n - 1 - i])
                    return false;
            }
            return static_cast<int>(g_hitOrder.size()) == n;
        };
        for (unsigned seed : {3u, 4u, 11u}) {
            reset();
            car->SetPosition(S_vector(2.0f, 0.5f, -1.0f));
            const bool hostileAboard = n > 1 && seed != 4;
            if (hostileAboard)
                crew[1]->hostileTo = {attacker};
            const int off = seed == 11 && w < v.m_wheelCount ? 1 : 0;
            if (off)
                v.m_wheels[w]->flags |= WHEEL_DETACHED;
            I3D_frame *charge = NewFrame(FRAME_VISUAL, "charge", nullptr);
            charge->m_refCount = 2;
            car->m_chargeVisual = charge;
            const std::vector<S_Thrown> thrown = thrownFor(seed);
            car->Wreck(false);
            Expect(wreck, car->m_wrecked && !g_streams.empty() && !g_streams[0]->ex &&
                              g_streams[0]->bits == T_Bits{{4, 4}} && !car->m_respawnTimer.running && !car->m_wreckShown,
                   __LINE__);
            Expect(wreck, v.m_unknown5B0 == lightsOff && g_impulseCallbacks == 6 && g_impulseContext == car && !v.m_engineOn &&
                              v.m_fuel == 0.0f && v.m_unknown610 == 0.0f && car->IsDestroyed() && g_deaths == 1, __LINE__);
            Expect(wreck, crewKilled(attacker) && attacker->experience == (hostileAboard ? 600 : 0) && !car->m_chargeVisual &&
                              charge->m_refCount == 1, __LINE__);
            // The wheels thrown: hidden, as debris with the velocity and a tenth of the push.
            int detached = 0;
            for (int i = 0; i < v.m_wheelCount; ++i)
                if (v.m_wheels[i] && (v.m_wheels[i]->flags & WHEEL_DETACHED))
                    ++detached;
            Expect(wreck, detached == static_cast<int>(thrown.size()) + off && g_streams.size() == 1 + thrown.size(), __LINE__);
            for (size_t k = 0; k < thrown.size(); ++k) {
                const S_Thrown &t = thrown[k];
                const S_VehicleWheel &wheel = *v.m_wheels[t.wheel];
                Expect(wreck, (wheel.flags & WHEEL_DETACHED) && !wheel.frame->IsOn(), __LINE__);
                if (k < g_debris.size()) {
                    const S_vector want(t.push.x * 0.1f + t.velocity.x, t.velocity.y + t.push.y * 0.1f,
                                        t.velocity.z + t.push.z * 0.1f);
                    Expect(wreck, g_debris[k].frame == wheel.frame && SameV(V3(g_debris[k].wheelParams.velocity), want, 0.0f),
                           __LINE__);
                }
                if (k + 1 < g_streams.size())
                    Expect(wreck, g_streams[k + 1]->bits.size() >= 2 &&
                                      g_streams[k + 1]->bits[1] == std::make_pair(static_cast<uint32_t>(t.wheel), 8), __LINE__);
            }
            // Once.
            ClearStreams();
            g_deaths = 0;
            car->Wreck(false);
            Expect(wreck, g_streams.empty() && g_deaths == 0, __LINE__);
        }
        // Told by the server: the driver to blame, no experience; a dedicated server reloads the
        // model at the restart; network games start the timer but co-op and objectives.
        reset();
        if (n > 1)
            crew[1]->hostileTo = {attacker};
        g_gameRole = GAME_ROLE_SERVER;
        thrownFor(5);
        car->Wreck(true);
        Expect(wreck, crewKilled(driver) && attacker->experience == 0 && car->m_respawnTimer.running && car->m_wreckShown,
               __LINE__);
        reset();
        g_gameRole = GAME_ROLE_CLIENT;
        car->m_lastAttacker = -1;
        auto *decoy = new C_Human();
        g_actorsById[-1] = decoy;
        car->Wreck(false);
        Expect(wreck, crewKilled(nullptr) && car->m_respawnTimer.running && !car->m_wreckShown, __LINE__);
        g_actorsById.erase(-1);
        delete decoy;
        reset();
        g_coop = true;
        car->Wreck(false);
        Expect(wreck, !car->m_respawnTimer.running, __LINE__);
        g_coop = false;
        reset();
        g_mpSettingsValue.gameType = MP_GAME_OBJECTIVES;
        car->Wreck(false);
        Expect(wreck, !car->m_respawnTimer.running, __LINE__);
        g_mpSettingsValue.gameType = MP_GAME_DEATHMATCH;
        g_gameRole = GAME_ROLE_LOCAL;
        // Mode +0x550: no wheels thrown (the coin flipped all the same).
        reset();
        v.m_unknown550 = 1;
        std::srand(8);
        for (int i = 0; i < v.m_wheelCount && i < 6; ++i)
            if (v.m_wheels[i] && !(v.m_wheels[i]->flags & WHEEL_DETACHED))
                std::rand();
        const int next = std::rand();
        std::srand(8);
        car->Wreck(false);
        const int after = std::rand();
        int detached = 0;
        for (int i = 0; i < v.m_wheelCount; ++i)
            if (v.m_wheels[i] && (v.m_wheels[i]->flags & WHEEL_DETACHED))
                ++detached;
        Expect(wreck, detached == 0 && after == next, __LINE__);
        v.m_unknown550 = 0;
        // The blast (its list is the harness's here): a human within 5 m, nothing between.
        reset();
        const S_vector centre = v.m_modelMatrix(3) + S_vector(0.0f, 0.5f, 0.0f);
        const auto body = [&](const S_vector &at) {
            auto *h = new C_Human();
            h->bodyFrame = NewFrame(FRAME_VISUAL, "body", nullptr);
            h->bodyFrame->m_localMatrix(3) = at;
            h->bonePos = at + S_vector(0.0f, 0.3f, 0.0f);
            return h;
        };
        C_Human *nearBy = body(centre + S_vector(1.2f, 1.6f, 0.0f));
        C_Human *farAway = body(centre + S_vector(6.0f, 0.0f, 0.0f));
        C_Human *edge = body(centre + S_vector(0.0f, 0.0f, 4.9f));
        C_Human *under = body(centre + S_vector(0.0f, -1.0f, 1.0f));
        C_Human *beyond = body(centre + S_vector(0.0f, 0.0f, 5.5f));
        C_FakeActor barrel(ACTOR_BARREL);
        g_blastActors = {nearBy, &barrel, nullptr, farAway, edge, under, beyond};
        const bool flatWas = g_flatGround;
        const float groundWas = g_groundY;
        g_flatGround = true;
        g_groundY = centre.y - 0.5f;
        car->Wreck(false);
        const auto blasted = [&](const C_Human *h) {
            if (h->hits.size() != 1)
                return false;
            const C_Human::S_Hit &x = h->hits[0];
            const S_vector toward = h->bodyFrame->GetWorldPos() - centre;
            const float d = std::sqrt(SquareZYX(toward));
            return x.type == HIT_EXPLOSION && std::fabs(x.amount - (1.0f - d * 0.2f) * 1000.0f) < 1e-3f &&
                   SameV(x.pos, h->bonePos, 0.0f) && SameV(x.dir, toward, 0.0f) && x.attacker == attacker && !x.flags;
        };
        Expect(wreck, blasted(nearBy) && std::fabs(nearBy->hits[0].amount - 600.0f) < 0.01f && farAway->hits.empty() &&
                          blasted(edge) && under->hits.empty() && beyond->hits.empty(), __LINE__);
        reset();
        g_flatGround = false;
        nearBy->hits.clear();
        edge->hits.clear();
        car->m_lastAttacker = -1;
        car->Wreck(false);
        Expect(wreck, under->hits.size() == 1 && under->hits[0].attacker == nullptr && nearBy->hits.size() == 1, __LINE__);
        g_flatGround = flatWas;
        g_groundY = groundWas;
        g_blastActors.clear();
        for (C_Human *h : {nearBy, farAway, edge, under, beyond})
            delete h;
        // Back as it was.
        reset();
        emptySeats();
        car->m_driver = nullptr;
        v.m_unknown5B0 = 0;
        v.m_callbacks[VEHICLE_CALLBACK_IMPULSE] = nullptr;
        car->SetPosition(S_vector(0.0f, 0.0f, 0.0f));
        g_model->m_data = nullptr;
        g_actorsById.clear();
        delete driver;
        delete attacker;
        ClearStreams();
        g_debris.clear();
    }

    // --- the charges.
    bool charges = true;
    {
        auto *placer = new C_Human();
        placer->SetNetId(23);
        C_FakeDef def(0);
        C_FakeItem item;
        item.m_def = &def;
        item.m_uid = 317;
        g_itemModel = NewFrame(FRAME_VISUAL, "charge model", nullptr);
        const S_vector4 pos = {0.25f, 0.75f, -1.5f, 1.0f}, normal = {0.0f, 0.0f, -1.0f, 0.0f};
        car->m_chargeTime = -1;
        car->m_chargeVisual = nullptr;
        Expect(charges, !car->PlaceCharge(nullptr, placer, pos, normal) && !car->PlaceCharge(&item, nullptr, pos, normal) &&
                            placer->removedItems.empty() && car->m_chargeTime == -1, __LINE__);
        // Shown here: the copy attached.
        g_duplicates.clear();
        g_posDirs.clear();
        g_linkFlags.clear();
        Expect(charges, car->PlaceCharge(&item, placer, pos, normal) && car->m_chargeTime == 15000 &&
                            car->m_chargeOwner == 23 && Same4(car->m_chargePos, pos, 0.0f) &&
                            Same4(car->m_chargeNormal, normal, 0.0f) && car->m_chargeVisual &&
                            placer->removedItems == std::vector<std::pair<int, bool>>{{317, true}}, __LINE__);
        if (I3D_frame *copy = car->m_chargeVisual) {
            Expect(charges, g_duplicates[copy] == g_itemModel && copy->GetParent() == g_model && g_linkFlags[copy] == 0 &&
                                g_posDirs.count(copy) && SameV(g_posDirs[copy].pos, V3(pos), 0.0f) &&
                                SameV(g_posDirs[copy].dir, V3(normal), 0.0f) && g_posDirs[copy].roll == 0.0f, __LINE__);
        }
        // Not while one is set.
        Expect(charges, !car->PlaceCharge(&item, placer, pos, normal) && placer->removedItems.size() == 1, __LINE__);
        // A dedicated server: no copy, the item removed.
        car->m_chargeTime = -1;
        car->m_chargeVisual = nullptr;
        placer->removedItems.clear();
        g_gameRole = GAME_ROLE_SERVER;
        Expect(charges, car->PlaceCharge(&item, placer, pos, normal) && !car->m_chargeVisual && car->m_chargeTime == 15000 &&
                            placer->removedItems == std::vector<std::pair<int, bool>>{{317, true}}, __LINE__);
        g_gameRole = GAME_ROLE_LOCAL;
        // The fuse: at its end the car wrecked by the placer.
        car->m_lastAttacker = 5;
        car->m_parts.wrecked = false;
        car->m_wrecked = false;
        car->UpdateCharge(14999);
        Expect(charges, car->m_chargeTime == 1 && !car->m_wrecked && car->m_lastAttacker == 5, __LINE__);
        ClearStreams();
        seatCrew();
        auto *driver = new C_Human();
        car->m_driver = driver;
        g_actorsById = {{23, placer}};
        car->UpdateCharge(1);
        Expect(charges, car->m_chargeTime == -1 && car->m_wrecked && car->m_lastAttacker == 23 && car->m_parts.wrecked &&
                            !g_streams.empty() && g_streams[0]->bits == T_Bits{{4, 4}}, __LINE__);
        if (n > 0)
            Expect(charges, crew[0]->hits.size() == 1 && crew[0]->hits[0].attacker == placer, __LINE__);
        emptySeats();
        car->m_driver = nullptr;
        g_actorsById.clear();
        delete driver;
        car->m_chargeTime = 50;
        car->m_wrecked = false;
        car->UpdateCharge(80);
        Expect(charges, car->m_chargeTime == -1 && car->m_wrecked, __LINE__);
        car->m_wrecked = false;
        car->m_parts.wrecked = false;
        v.m_unknown610 = 1.0f;
        for (int i = 0; i < v.m_wheelCount; ++i)
            if (v.m_wheels[i])
                v.m_wheels[i]->flags &= ~WHEEL_DETACHED;
        ClearStreams();
        g_debris.clear();
        delete placer;
    }

    // --- the ray.
    bool ray = true;
    {
        I3D_frame *f = NewFrame(FRAME_VISUAL, "rayed", nullptr);
        const S_vector4 from = {1.0f, 2.0f, 3.0f, 1.0f};
        const S_vector dir(0.0f, 0.6f, 0.8f);
        S_vector4 pos = {9, 9, 9, 9}, normal = {9, 9, 9, 9};
        g_intersects.clear();
        g_intersectHit = nullptr;
        Expect(ray, !car->RayTest(from, dir, &pos, &normal) && Same4(pos, {9, 9, 9, 9}, 0.0f) &&
                        Same4(normal, {9, 9, 9, 9}, 0.0f), __LINE__);
        g_intersectHit = f;
        g_intersectDistance = 4.0f;
        g_intersectNormal = S_vector(0.0f, -0.6f, -0.8f);
        Expect(ray, car->RayTest(from, dir, &pos, &normal) &&
                        Same4(pos, {4.0f * dir.x + 1.0f, 4.0f * dir.y + 2.0f, 4.0f * dir.z + 3.0f, 0.0f}, 0.0f) &&
                        Same4(normal, {0.0f, 0.6f, 0.8f, 0.0f}, 0.0f), __LINE__);
        Expect(ray, g_intersects.size() == 2 && SameV(g_intersects[1].from, V3(from), 0.0f) &&
                        SameV(g_intersects[1].dir, dir * 10.0f, 0.0f) && g_intersects[1].frame == g_model &&
                        g_intersects[1].flags == (I3DCOL_HIT_ZONE | I3DCOL_VISUAL_GEOMETRY) && !g_intersects[1].a &&
                        !g_intersects[1].b && !g_intersects[1].c, __LINE__);
        g_intersectHit = nullptr;
    }

    // --- the crew's queries and the materials.
    bool queries = true;
    {
        const float engine = v.m_unknown610;
        v.m_unknown610 = 0.0f;
        const bool destroyed = car->IsDestroyed();
        v.m_unknown610 = 0.001f;
        const bool little = car->IsDestroyed();
        v.m_unknown610 = -1.0f;
        const bool negative = car->IsDestroyed();
        v.m_unknown610 = 0.01f;
        Expect(queries, destroyed && !car->IsDestroyed() && !little && !negative, __LINE__);
        v.m_unknown610 = engine;
        seatCrew();
        Expect(queries, !car->HasLocalPlayerAboard(), __LINE__);
        if (n > 0) {
            crew[n - 1]->localPlayer = true;
            Expect(queries, car->HasLocalPlayerAboard(), __LINE__);
            crew[n - 1]->localPlayer = false;
            crew[0]->localPlayer = true;
            Expect(queries, car->HasLocalPlayerAboard(), __LINE__);
            crew[0]->localPlayer = false;
        }
        // Enemies aboard: single player by hostility, network games by team.
        auto *human = new C_Human();
        human->team = 1;
        Expect(queries, !car->HasEnemyAboard(human), __LINE__);
        if (n > 0) {
            human->hostileTo = {crew[0]};
            Expect(queries, car->HasEnemyAboard(human), __LINE__);
            crew[0]->health = 0;
            Expect(queries, !car->HasEnemyAboard(human), __LINE__);
            crew[0]->health = 800;
            human->hostileTo.clear();
            g_gameRole = GAME_ROLE_SERVER;
            for (int i = 0; i < n; ++i)
                crew[i]->team = 1;
            Expect(queries, !car->HasEnemyAboard(human), __LINE__);
            crew[0]->team = 0;
            Expect(queries, car->HasEnemyAboard(human), __LINE__);
            for (int k = 0; k < 3; ++k) {
                crew[0]->health = k == 0 ? 0 : 800;
                crew[0]->inPlay = k != 1;
                crew[0]->spectating = k == 2;
                Expect(queries, !car->HasEnemyAboard(human), __LINE__);
            }
            seatCrew();
            g_gameRole = GAME_ROLE_LOCAL;
        }
        // Getting in.
        std::vector<uint32_t> flags0;
        for (int i = 0; i < v.m_wheelCount; ++i)
            flags0.push_back(v.m_wheels[i] ? v.m_wheels[i]->flags : 0);
        const S_vector4 upWas = v.m_modelAxisY;
        const auto standing = [&](bool on) {
            v.m_speed = 0.0f;
            v.m_modelAxisY = {0.0f, 1.0f, 0.0f, 0.0f};
            for (int i = 0; i < v.m_wheelCount; ++i)
                if (v.m_wheels[i])
                    v.m_wheels[i]->flags = on ? (flags0[i] | WHEEL_ON_GROUND) & ~WHEEL_DETACHED : flags0[i] & ~WHEEL_ON_GROUND;
        };
        standing(true);
        g_playerTeam.clear();
        emptySeats();
        const bool drivable = car->IsDrivable(CAR_DRIVABLE_ON_WHEELS);
        Expect(queries, car->CanEnter() == drivable, __LINE__);
        if (drivable) {
            car->m_wrecked = true;
            Expect(queries, !car->CanEnter(), __LINE__);
            car->m_wrecked = false;
            standing(false);
            Expect(queries, !car->CanEnter(), __LINE__);
            standing(true);
            if (n > 0) {
                seatCrew();
                g_teamAsked = false;
                Expect(queries, !car->CanEnter() && g_teamAsked, __LINE__);
                for (C_Human *c : crew)
                    g_playerTeam.insert(c);
                Expect(queries, car->CanEnter(), __LINE__);
                g_playerTeam.erase(crew[0]);
                crew[0]->health = 0;
                Expect(queries, car->CanEnter(), __LINE__);
                crew[0]->health = 800;
                // Network games: the local human's team.
                g_gameRole = GAME_ROLE_CLIENT;
                auto *local = new C_Human();
                local->team = 0;
                g_localHuman = local;
                Expect(queries, car->CanEnter(), __LINE__);
                crew[0]->team = 1;
                Expect(queries, !car->CanEnter(), __LINE__);
                for (int k = 0; k < 3; ++k) {
                    crew[0]->health = k == 0 ? 0 : 800;
                    crew[0]->inPlay = k != 1;
                    crew[0]->spectating = k == 2;
                    Expect(queries, car->CanEnter(), __LINE__);
                }
                g_gameRole = GAME_ROLE_LOCAL;
                g_localHuman = nullptr;
                delete local;
            }
        }
        for (int i = 0; i < v.m_wheelCount; ++i)
            if (v.m_wheels[i])
                v.m_wheels[i]->flags = flags0[i];
        v.m_modelAxisY = upWas;
        g_playerTeam.clear();
        // The crew's side: the first from the last seat that counts.
        seatCrew();
        for (C_Human *c : crew)
            c->role = HUMAN_ROLE_NEUTRAL;
        Expect(queries, car->GetCrewSide() == 2, __LINE__);
        if (n > 0) {
            const struct {
                int role, side;
            } sides[] = {{HUMAN_ROLE_ENEMY, 0}, {HUMAN_ROLE_NPC_TEAM_5, 0}, {HUMAN_ROLE_NPC_TEAM, 1}, {HUMAN_ROLE_PLAYER, 6},
                         {0, 2}, {7, 2}};
            for (const auto &s : sides) {
                crew[0]->role = static_cast<E_HumanRole>(s.role);
                crew[0]->team = 6;
                Expect(queries, car->GetCrewSide() == s.side, __LINE__);
            }
            crew[0]->role = HUMAN_ROLE_ENEMY;
            for (int k = 0; k < 3; ++k) {
                crew[0]->health = k == 0 ? 0 : 800;
                crew[0]->inPlay = k != 1;
                crew[0]->spectating = k == 2;
                Expect(queries, car->GetCrewSide() == 2, __LINE__);
            }
            seatCrew();
            if (n > 1) {
                for (C_Human *c : crew)
                    c->role = HUMAN_ROLE_NEUTRAL;
                crew[0]->role = HUMAN_ROLE_ENEMY;
                crew[n - 1]->role = HUMAN_ROLE_NPC_TEAM;
                Expect(queries, car->GetCrewSide() == 1, __LINE__);
            }
        }
        emptySeats();
        // The materials.
        I3D_frame *f = NewFrame(FRAME_VISUAL, "material", nullptr);
        Expect(queries, car->GetMaterialId(f) == 11, __LINE__);
        g_frameMaterials[f] = 5;
        Expect(queries, car->GetMaterialId(f) == 5, __LINE__);
        g_frameMaterials[f] = 0;
        Expect(queries, car->GetMaterialId(f) == 0, __LINE__);
        g_frameMaterials.clear();
        // (The parts' material: PartsHitTest.)
        Expect(queries, car->GetHitMaterial(S_vector(1, 2, 3), S_vector(0, 0, 1)) ==
                            car->m_parts.GetHitMaterial(S_vector(1, 2, 3), S_vector(0, 0, 1)), __LINE__);
        delete human;
    }

    std::printf("car hits: messages %d simulated %d collision %d tyre %d hit %d wreck %d charges %d ray %d queries %d\n",
                messages, simulated, collision, tyre, hit, wreck, charges, ray, queries);
    for (C_Human *c : crew)
        delete c;
    g_gameRole = GAME_ROLE_LOCAL;
    v.m_unknownF6C = options;
    GetGame()->m_scene = nullptr;
    v.Stop();
    for (int ticks = 0; ticks < 20 && std::any_of(g_carEmitters.begin(), g_carEmitters.end(),
                                                  [](const S_CarEmitter &e) { return e.active; });
         ++ticks)
        UpdateCarEffectEmitters(83);
    delete car;
    g_made.clear();
    g_images.clear();
    g_meshes = g_meshesReleased = g_materialsMade = g_materialsReleased = 0;
}

// The parts reset (StartMission, RestoreModel, the restart): a planted null turret frame gone
// (gathered again), no fuse.
bool PartsWereReset(const S_CarParts &p)
{
    return std::find(p.turretFrames.begin(), p.turretFrames.end(), nullptr) == p.turretFrames.end() && p.fuse == 0;
}

// --- the mission (src/game/car_mission.cpp; part 2e) -----------------------------------------------

// The reloaded model (RestoreModel, through the cache's g_cacheFill): a visual per name of
// g_namesakeMeshes with that mesh.
std::map<std::string, S_Mesh *> g_namesakeMeshes;
void FillNamesakes(I3D_model *object)
{
    for (const auto &named : g_namesakeMeshes) {
        I3D_frame *f = AddFrame(FRAME_VISUAL, "reload", named.first.c_str(), object);
        *reinterpret_cast<S_Mesh **>(reinterpret_cast<uint8_t *>(f) + 0x1E0) = named.second;
    }
}
S_Mesh *MeshOf(const I3D_frame *f)
{
    return *reinterpret_cast<S_Mesh *const *>(reinterpret_cast<const uint8_t *>(f) + 0x1E0);
}
uint32_t __stdcall CollectVisual(I3D_frame *frame, void *context)
{
    static_cast<std::vector<I3D_frame *> *>(context)->push_back(frame);
    return 0;
}
extern std::vector<bool> g_activations, g_deactivations;
// The lights' notifications (the vehicle's callback 9): the lights then (of g_lightVehicle); the
// first adds the indicators, the flashers and the beams, which the later setters clear or keep.
int g_lightNotes;
void *g_lightNoteContext;
C_Vehicle *g_lightVehicle;
std::vector<uint32_t> g_lightValues;
void __fastcall NoteLights(void *context)
{
    ++g_lightNotes;
    g_lightNoteContext = context;
    if (!g_lightVehicle)
        return;
    g_lightValues.push_back(g_lightVehicle->m_unknown5B0);
    if (g_lightNotes == 1)
        g_lightVehicle->m_unknown5B0 |= VEHICLE_LIGHT_INDICATOR_LEFT | VEHICLE_LIGHT_INDICATOR_RIGHT |
                                        VEHICLE_LIGHT_FLASH_LEFT | VEHICLE_LIGHT_FLASH_RIGHT | 0x0FF00000u;
}

void MissionTest(const char *path)
{
    if (!LoadModel(path))
        return;
    InitPerfTime();
    while (GetPerfTimeMs() < 3) {
    }
    auto *car = new C_Car();
    car->SetFrame(g_model);
    C_Vehicle &v = car->m_vehicle;
    const int n = static_cast<int>(v.m_seats.size());
    car->SetNetId(77);
    // A flat ground under the wheels at rest (as the drive test's), for the settling.
    g_groundY = GroundUnderWheels(v);
    g_flatGround = true;
    g_gameRole = GAME_ROLE_LOCAL;
    g_coop = false;
    g_mpSettingsValue.gameType = MP_GAME_DEATHMATCH;
    GetGame()->m_scene = g_carScene;
    static int replication;
    g_netReplication = reinterpret_cast<C_NetReplication *>(&replication);
    g_fakeNet = S_FakeNet();
    g_netFinds.clear();
    g_netRemoved.clear();
    g_netC4.clear();
    g_netC8.clear();
    g_quantSetups.clear();
    g_linkAdds.clear();
    g_linkRemoves.clear();
    g_activations.clear();
    g_deactivations.clear();
    g_weaponEvents.clear();
    g_keepersMade.clear();
    g_keepersDeleted.clear();
    g_restored.clear();
    g_sectorFrames.clear();
    g_cacheFailName.clear();
    g_failInitSeat = -1;
    C_FakeDef def(61);
    g_weaponDef = &def;
    C_FakeStore store;
    store.count = 17;
    C_ItemContainer *containers[1] = {&store};
    car->m_containers = containers;
    // A checkpoint network of one point far away (the marks find nothing; a mark on it shows the
    // clearing).
    alignas(16) static uint8_t networkMemory[sizeof(C_CheckpointNetwork)];
    std::memset(networkMemory, 0, sizeof networkMemory);
    C_CheckpointNetwork &network = *reinterpret_cast<C_CheckpointNetwork *>(networkMemory);
    S_Checkpoint farPoint[1];
    farPoint[0].pos = S_vector(5000.0f, 0.0f, 5000.0f);
    network.m_points = farPoint;
    network.m_pointCount = 1;
    GetGame()->m_checkpoints = reinterpret_cast<C_Checkpoints *>(&network);
    // Each seat's "VIEWnn" visual, on (ShowSeatView turns a seat's without a weapon off).
    for (int i = 0; i < n; ++i)
        ViewFrame(i)->m_frameFlags |= 1u;
    // The actor where the model is (its place and z axis, as Init reads them).
    const S_vector at0 = g_model->GetWorldPos(), z0 = g_model->GetWorldDir();
    const S_vector4 pos0 = {at0.x, at0.y, at0.z, 1.0f}, dir0 = {z0.x, z0.y, z0.z, 0.0f};
    car->SetPosition4(pos0);
    car->m_direction = dir0;
    g_lightNotes = 0;
    g_lightNoteContext = nullptr;
    g_lightVehicle = nullptr;
    v.SetCallback(VEHICLE_CALLBACK_IMPULSE, reinterpret_cast<void *>(&NoteLights));
    // A seat's camera dummy ("camera3RD*" anywhere under the seat dummy).
    const auto cameraOf = [&](int seat) -> I3D_frame * {
        return v.m_seats[seat].frame->FindChildFrame("camera3RD*", ENUMF_WILDMASK | FRAME_MASK_DUMMY);
    };

    // --- StartMission on single player's server side: the common part only.
    bool start = true;
    {
        car->m_wreckShown = true;
        car->m_wrecked = true;
        car->m_chargeVisual = reinterpret_cast<I3D_frame *>(car);   // (dropped, never used)
        car->m_chargeOwner = 5;
        car->m_chargeTime = 9;
        car->m_deadlyReported = true;
        car->m_lastAttacker = 4;
        car->m_restrictions = 3;
        car->m_stateChanged = false;
        car->m_respawnTimer.period = 0;
        v.m_unknownF50 = 0.0f;
        g_mpSettingsValue.allowVehicles = 1;
        car->m_vehiclesAllowed = false;
        car->m_parts.turretFrames.push_back(nullptr);
        car->m_parts.fuse = 9;
        const int t0 = static_cast<int>(GetPerfTimeMs());
        car->StartMission(true);
        const int t1 = static_cast<int>(GetPerfTimeMs());
        Expect(start, !car->m_wreckShown && !car->m_wrecked && !car->m_chargeVisual && car->m_chargeOwner == -1 &&
                          car->m_chargeTime == -1 && !car->m_deadlyReported && car->m_lastAttacker == -1 &&
                          car->m_restrictions == 0 && car->m_stateChanged && car->m_impulseSent >= t0 &&
                          car->m_impulseSent <= t1 && v.m_unknownF50 == 0.04f && car->m_vehiclesAllowed &&
                          PartsWereReset(car->m_parts) && car->m_respawnTimer.period == 38000, __LINE__);
        Expect(start, g_linkAdds.size() == 1 && g_linkAdds[0].first == car && g_linkAdds[0].second == 4 &&
                          car->m_periodicLink == 41 && g_activations == std::vector<bool>{true} &&
                          Same4(car->m_startPosition, pos0, 0.0f) && Same4(car->m_startDirection, dir0, 0.0f) &&
                          g_netFinds.empty() && g_quantSetups.empty(), __LINE__);
        // (Nothing of the vehicle's.)
        bool none = !car->m_started && g_weaponEvents.empty() && g_keepersMade.empty() && g_sectorFrames.empty() &&
                    !car->m_wreckModel && g_lightNotes == 0;
        for (int i = 0; i < 10; ++i)
            none &= !car->m_seatWeapons[i];
        Expect(start, none, __LINE__);
        car->m_lastAttacker = 4;                        // (the client side keeps it)
    }

    // --- StartMission on single player's client side: the vehicle once.
    bool vehicle = true, seats = true;
    std::string seatLine;
    {
        auto *driver = new C_Human();
        car->m_driver = driver;
        g_lightVehicle = &v;
        g_lightValues.clear();
        v.m_unknown534 = 0;
        v.m_unknown53C = 0;
        v.m_unknown7C4 = 0.0f;
        v.m_fuel = 0.0f;
        car->m_marksTime = 123;
        g_finds.clear();
        g_cacheLoads.clear();
        g_createdModels.clear();
        g_mpSettingsValue.allowVehicles = 1;
        car->StartMission(false);
        // (The settling has moved the model since: 20 ms of wall-clock steps without the body's
        // collision (options 0x80000801), so the BmwR75 sidecar bike tips over and falls through
        // the ground; only the horizontal place is near.)
        if (std::getenv("CAR_TRACE"))
            std::printf("  mission start place %.3f %.3f %.3f (actor %.3f %.3f %.3f) driver %p attacker %d\n",
                        v.m_modelPos.x, v.m_modelPos.y, v.m_modelPos.z, pos0.x, pos0.y, pos0.z,
                        static_cast<void *>(car->m_driver), car->m_lastAttacker);
        Expect(vehicle, car->m_started && !car->m_driver && std::fabs(v.m_modelPos.x - pos0.x) < 10.0f &&
                            std::fabs(v.m_modelPos.z - pos0.z) < 10.0f &&
                            car->m_lastAttacker == 4 &&
                            g_activations == std::vector<bool>({true, false}) && g_linkAdds.size() == 1, __LINE__);
        // The volumes (single player: the special ones on), the fuel, the lights on automatic (from
        // Start's none: four notifications, the flashers and the beams cleared, the indicators
        // kept), the beams, the two words, the settling (a step after Start's 0.02 s).
        g_lightVehicle = nullptr;
        const uint32_t automatic = VEHICLE_LIGHT_AUTO_INDICATORS | VEHICLE_LIGHT_AUTO_BRAKE | VEHICLE_LIGHT_AUTO_REVERSE;
        const uint32_t indicators = VEHICLE_LIGHT_INDICATOR_LEFT | VEHICLE_LIGHT_INDICATOR_RIGHT;
        const std::vector<uint32_t> lights = {automatic, automatic | indicators | VEHICLE_LIGHT_FLASH_RIGHT | 0x0FF00000u,
                                              automatic | indicators | 0x0FF00000u, automatic | indicators};
        Expect(vehicle, v.m_specialVolumesOn == !v.m_specialVolumes.empty() &&
                            v.m_fuel == std::min(1000.0f, v.m_tankSize) && g_lightValues.size() >= 4 &&
                            std::equal(lights.begin(), lights.end(), g_lightValues.begin()) &&
                            g_lightNoteContext == v.m_callbackContext && v.m_unknown534 == 0x98302u &&
                            v.m_unknown53C == 0xd8002 && v.m_unknown7C4 > 0.05f && !car->m_seatLeftTimes &&
                            !car->m_seatLeftIds, __LINE__);
        for (const S_VehicleLamp &lamp : v.m_lamps) {
            if (lamp.projector)
                Expect(vehicle, SameV(lamp.projector->GetScale(), S_vector(12.0f, 12.0f, 40.0f), 0.0f), __LINE__);
        }
        // The sector at the model's place a metre up (once; the vehicle's own are at its centre of
        // mass), the marks redone, the wreck's model.
        int sectors = 0;
        for (const S_SectorUpdate &u : g_sectorFrames)
            sectors += u.frame == g_model && SameV(u.pos, S_vector(u.at.x, u.at.y + 1.0f, u.at.z), 0.0f);
        Expect(vehicle, sectors == 1 && car->m_marksTime == 0 &&
                            g_finds.size() == 1 && !g_cacheLoads.empty() && g_cacheLoads.back() == "C" + g_modelFile &&
                            !g_createdModels.empty() && car->m_wreckModel == static_cast<I3D_model *>(g_createdModels.back()) &&
                            car->m_wreckModel->m_refCount == 1, __LINE__);
        // The seats, from the last: flags, texts, weapons, views, cameras.
        std::vector<std::string> events;
        for (int i = n; i-- > 0;) {
            if (v.m_seats[i].flags & SEAT_FLAG_WEAPON) {
                events.push_back("make");
                events.push_back("init " + std::to_string(i));
            }
        }
        Expect(seats, g_weaponEvents == events, __LINE__);
        size_t keeper = 0;
        for (int i = n; i-- > 0;) {
            const S_VehicleSeat &s = v.m_seats[i];
            C_SeatWeapon *w = car->m_seatWeapons[i];
            if (s.flags & SEAT_FLAG_WEAPON) {
                Expect(seats, w && Fake(w).inits == 1 && Fake(w).car == car && Fake(w).frame == g_model &&
                                  Fake(w).seatFrame == s.frame && Fake(w).seat == i && Fake(w).views == 1 &&
                                  !Fake(w).view && !Fake(w).sender && Fake(w).refillBelow == 0 && ViewFrame(i)->IsOn(),
                       __LINE__);
            } else {
                Expect(seats, !w && !ViewFrame(i)->IsOn(), __LINE__);
            }
            I3D_frame *camera = cameraOf(i);
            const bool kept = camera && !(s.flags & (SEAT_FLAG_ARMED | SEAT_FLAG_WEAPON));
            if (kept) {
                Expect(seats, s.camera && keeper < g_keepersMade.size() && g_keepersMade[keeper] == s.camera &&
                                  Keeper(s.camera).owner == car && Keeper(s.camera).frame == camera, __LINE__);
                ++keeper;
            } else {
                Expect(seats, !s.camera, __LINE__);
            }
        }
        Expect(seats, keeper == g_keepersMade.size(), __LINE__);
        for (int i = 0; i < n; ++i) {
            const S_VehicleSeat &s = v.m_seats[i];
            char item[160];
            std::snprintf(item, sizeof item, " %s=%x/%d/%d/%d", s.frame->GetShortName(), s.flags, s.text,
                          car->m_seatWeapons[i] ? 1 : 0, s.camera ? 1 : 0);
            seatLine += item;
        }
        delete driver;

        // Started already: only the common part.
        const size_t loads = g_cacheLoads.size(), events0 = g_weaponEvents.size(), sectors0 = g_sectorFrames.size();
        g_lightNotes = 0;
        car->StartMission(false);
        Expect(vehicle, car->m_started && g_cacheLoads.size() == loads && g_weaponEvents.size() == events0 &&
                            g_lightNotes == 0 && g_sectorFrames.size() == sectors0, __LINE__);
    }

    // --- The wreck's meshes and the model's back.
    bool wreck = true, restore = true;
    {
        std::vector<I3D_frame *> all, visuals;
        g_model->EnumFrames(&CollectVisual, &all, FRAME_MASK_VISUAL, nullptr);
        for (I3D_frame *f : all) {
            // (The vehicle's projectors are unnamed; and only visuals with a mesh.)
            if (*f->GetShortName() && MeshOf(f))
                visuals.push_back(f);
        }
        while (I3D_frame *c = car->m_wreckModel->m_firstChild)   // (the fake cache's own child)
            Unlink(c);
        // The wreck's model: a namesake with its own mesh for every name but the first visual's
        // (the first of each name wins, as FindChildFrame).
        std::map<std::string, S_Mesh *> original, wrecked;
        for (I3D_frame *f : visuals) {
            if (!original.count(f->GetShortName()))
                original[f->GetShortName()] = MeshOf(f);
        }
        const std::string skipped = visuals.empty() ? "" : visuals[0]->GetShortName();
        for (const auto &named : original) {
            if (named.first == skipped)
                continue;
            I3D_frame *f = AddFrame(FRAME_VISUAL, "wreck", named.first.c_str(), car->m_wreckModel);
            wrecked[named.first] = NewMesh();
            *reinterpret_cast<S_Mesh **>(reinterpret_cast<uint8_t *>(f) + 0x1E0) = wrecked[named.first];
        }
        std::map<const I3D_frame *, S_Mesh *> before;
        for (I3D_frame *f : visuals)
            before[f] = MeshOf(f);
        // An occupant whose own visual shares a name: unlinked meanwhile, so not swapped.
        auto *occupant = new C_Human();
        I3D_frame *body = NewFrame(FRAME_MODEL, "occupant", nullptr);
        occupant->SetFrame(body);
        I3D_frame *own = visuals.empty() ? nullptr : AddFrame(FRAME_VISUAL, "occ", visuals.back()->GetShortName(), body);
        S_Mesh *ownMesh = NewMesh();
        if (own)
            *reinterpret_cast<S_Mesh **>(reinterpret_cast<uint8_t *>(own) + 0x1E0) = ownMesh;
        const int seat = n - 1;
        if (n > 0) {
            LinkUnder(body, v.m_seats[seat].frame);
            v.m_seats[seat].occupant = occupant;
        }
        g_meshFlags.clear();
        g_countUpdates = true;
        g_frameUpdates.clear();
        g_unlinks.clear();
        car->ShowWreck();
        g_countUpdates = false;
        Expect(wreck, car->m_wreckShown && (!own || MeshOf(own) == ownMesh) &&
                          (n == 0 || (body->m_parent == v.m_seats[seat].frame &&
                                      g_linkFlags[body] == I3DLINK_KEEP_WORLD && g_unlinks[body] == 1 &&
                                      g_frameUpdates[body] >= 1)), __LINE__);
        for (I3D_frame *f : visuals) {
            const auto w = wrecked.find(f->GetShortName());
            if (w != wrecked.end())
                Expect(wreck, MeshOf(f) == w->second && g_meshFlags[f] == 1 && g_frameUpdates[f] >= 1, __LINE__);
            else
                Expect(wreck, MeshOf(f) == before[f] && !g_meshFlags.count(f), __LINE__);
        }
        for (const S_VehiclePart &part : v.m_parts)
            Expect(wreck, reinterpret_cast<S_Mesh *>(part.mesh) == MeshOf(part.frame), __LINE__);
        // Once only; and none without the wreck's model (Fix).
        g_meshFlags.clear();
        car->ShowWreck();
        Expect(wreck, g_meshFlags.empty(), __LINE__);
        I3D_model *wreckModel = car->m_wreckModel;
        car->m_wreckModel = nullptr;
        car->m_wreckShown = false;
        car->ShowWreck();
        Expect(wreck, g_meshFlags.empty() && !car->m_wreckShown, __LINE__);
        car->m_wreckModel = wreckModel;
        car->m_wreckShown = true;

        // RestoreModel: the model's file (without its directories) reloaded, its meshes back.
        const char *&fileName = *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(g_model) + 0x134);
        const std::string inDirs = "models\\cars\\" + g_modelFile;
        fileName = inDirs.c_str();
        g_namesakeMeshes = original;
        g_cacheFill = &FillNamesakes;
        g_meshFlags.clear();
        car->m_wrecked = true;
        car->m_parts.turretFrames.push_back(nullptr);
        car->m_parts.fuse = 9;
        v.m_gear = 3;
        S_VehicleWheel *rim = nullptr;
        for (int i = 0; i < v.m_wheelCount && !rim; ++i)
            rim = v.m_wheels[i];
        if (rim)
            rim->flags |= WHEEL_ON_RIM;
        g_linkFlags.erase(body);
        const size_t made0 = g_createdModels.size();
        car->RestoreModel();
        g_cacheFill = nullptr;
        Expect(restore, !car->m_wreckShown && !car->m_wrecked && PartsWereReset(car->m_parts) && v.m_gear == 1 &&
                            g_cacheLoads.back() == g_modelFile && g_createdModels.size() == made0 + 1 &&
                            g_createdModels.back()->m_refCount == 0 && (!own || MeshOf(own) == ownMesh) &&
                            (n == 0 || (body->m_parent == v.m_seats[seat].frame && g_unlinks[body] == 2 &&
                                        g_linkFlags[body] == I3DLINK_KEEP_WORLD)) &&
                            (!rim || !(rim->flags & WHEEL_ON_RIM)), __LINE__);
        for (I3D_frame *f : visuals) {
            const auto o = original.find(f->GetShortName());
            Expect(restore, MeshOf(f) == (o != original.end() ? o->second : before[f]), __LINE__);
        }
        for (const S_VehiclePart &part : v.m_parts)
            Expect(restore, reinterpret_cast<S_Mesh *>(part.mesh) == MeshOf(part.frame), __LINE__);
        // Not reloaded without the wreck shown (the restart all the same); a model that does not
        // load: released, no restart (Quirk kept).
        v.m_gear = 3;
        car->RestoreModel();
        Expect(restore, v.m_gear == 1 && g_createdModels.size() == made0 + 1, __LINE__);
        car->m_wreckShown = true;
        g_cacheFailName = g_modelFile;
        car->m_wrecked = true;
        v.m_gear = 3;
        car->RestoreModel();
        g_cacheFailName.clear();
        Expect(restore, car->m_wreckShown && !car->m_wrecked && v.m_gear == 3 && g_createdModels.size() == made0 + 2 &&
                            g_createdModels.back()->m_refCount == 0, __LINE__);
        car->m_wreckShown = false;
        fileName = g_modelFile.c_str();
        if (n > 0) {
            Unlink(body);
            v.m_seats[seat].occupant = nullptr;
        }
        delete occupant;
    }

    // --- RestartMission (single player's server side): back to the start.
    bool restart = true;
    {
        car->SetPosition4({pos0.x + 30.0f, pos0.y + 1.0f, pos0.z - 20.0f, 1.0f});
        car->m_direction = {-dir0.z, 0.0f, dir0.x, 0.0f};
        g_model->SetPosInline(S_vector(pos0.x + 30.0f, pos0.y + 1.0f, pos0.z - 20.0f));
        car->m_parts.turretFrames.push_back(nullptr);
        car->m_parts.fuse = 9;
        auto *rider = new C_Human();
        rider->SetFrame(NewFrame(FRAME_MODEL, "rider", nullptr));
        if (n > 0)
            v.m_seats[0].occupant = rider;
        I3D_frame *charge = NewFrame(FRAME_VISUAL, "charge", nullptr);
        car->m_chargeVisual = charge;
        car->m_chargeOwner = 3;
        car->m_chargeTime = 50;
        car->m_lastAttacker = 8;
        car->m_deadlyReported = true;
        auto *driver = new C_Human();
        car->m_driver = driver;
        v.m_fuel = 0.0f;
        g_mpSettingsValue.allowVehicles = 0;
        g_finds.clear();
        g_weaponEvents.clear();
        g_dirs.erase(g_model);
        int marks = 0;
        for (int i = 0; i < v.m_wheelCount; ++i) {
            if (!v.m_wheels[i])
                continue;
            for (S_VehicleSkidMark &mark : v.m_wheels[i]->skidMarks) {
                mark.active = true;
                mark.frame0->m_frameFlags |= 1u;
                ++marks;
            }
        }
        car->RestartMission(true);
        bool hidden = true;
        for (int i = 0; i < v.m_wheelCount; ++i) {
            if (!v.m_wheels[i])
                continue;
            for (const S_VehicleSkidMark &mark : v.m_wheels[i]->skidMarks)
                hidden &= !mark.active && !mark.frame0->IsOn();
        }
        Expect(restart, hidden && marks > 0 && v.m_specialVolumesOn == !v.m_specialVolumes.empty(), __LINE__);
        std::vector<std::string> events;
        for (int i = 0; i < n; ++i) {
            if (car->m_seatWeapons[i])
                events.push_back("restart " + std::to_string(i));
        }
        Expect(restart, Same4(car->GetPosition4(), pos0, 0.0f) && Same4(car->m_direction, dir0, 0.0f) &&
                            g_dirs.count(g_model) && SameV(g_dirs[g_model], S_vector(dir0.x, dir0.y, dir0.z), 0.0f) &&
                            std::fabs(g_model->GetPos().x - pos0.x) < 10.0f && std::fabs(g_model->GetPos().z - pos0.z) < 10.0f &&
                            !car->m_vehiclesAllowed && v.m_fuel == std::min(1000.0f, v.m_tankSize) &&
                            car->m_lastAttacker == -1 && !car->m_driver && !car->m_deadlyReported && g_finds.size() == 1 &&
                            charge->m_refCount == 0 && !car->m_chargeVisual && car->m_chargeOwner == -1 &&
                            car->m_chargeTime == -1 && g_weaponEvents == events && g_linkAdds.size() == 2 &&
                            car->m_periodicLink == 42 && g_fakeNet.propertyAsks.empty() &&
                            PartsWereReset(car->m_parts) && (n == 0 || !v.m_seats[0].occupant), __LINE__);
        delete driver;
        Unlink(rider->GetFrame());                      // (LeaveSeat put it in the scene's sector)
        delete rider;
        // The client side: no link.
        car->RestartMission(false);
        Expect(restart, g_linkAdds.size() == 2, __LINE__);
    }

    // --- StopMission (single player's server side), then the client side's of a stopped car.
    bool stop = true;
    {
        car->m_marks.push_back({0, true});
        farPoint[0].flags |= CHECKPOINT_FLAG_CAR_0200;
        auto *occupant = new C_Human();
        occupant->SetFrame(NewFrame(FRAME_MODEL, "occupant2", nullptr));
        if (n > 0)
            v.m_seats[0].occupant = occupant;
        std::vector<const C_CameraFrameKeeper *> keepers;
        for (int i = 0; i < n; ++i) {
            if (v.m_seats[i].camera)
                keepers.push_back(v.m_seats[i].camera);
        }
        std::vector<std::string> events;
        for (int i = 10; i-- > 0;) {
            if (car->m_seatWeapons[i]) {
                events.push_back("stop " + std::to_string(i));
                events.push_back("delete " + std::to_string(i));
            }
        }
        I3D_model *wreckModel = car->m_wreckModel;
        const int link = car->m_periodicLink;
        g_weaponEvents.clear();
        g_deactivations.clear();
        v.m_unknown5B0 = 0x77;
        car->m_wrecked = true;
        car->StopMission(true);
        bool cleared = true;
        for (int i = 0; i < 10; ++i)
            cleared &= !car->m_seatWeapons[i];
        for (int i = 0; i < n; ++i)
            cleared &= !v.m_seats[i].camera && !v.m_seats[i].occupant;
        Expect(stop, cleared && g_weaponEvents == events && g_keepersDeleted == keepers && !car->m_started &&
                         v.m_specialVolumes.empty() && v.m_unknown5B0 == 0 && g_deactivations == std::vector<bool>{true} &&
                         g_linkRemoves == std::vector<int>{link} && car->m_periodicLink == -1 && car->m_marks.empty() &&
                         !(farPoint[0].flags & CHECKPOINT_FLAG_CAR_0200) && !car->m_wreckModel &&
                         wreckModel->m_refCount == 0 && g_netRemoved.empty() && !car->m_wrecked, __LINE__);
        Unlink(occupant->GetFrame());                   // (LeaveSeat put it in the scene's sector)
        delete occupant;
        // Single player's client side stops the vehicle even when not started; a client does not.
        v.m_unknown5B0 = 0x55;
        car->StopMission(false);
        Expect(stop, v.m_unknown5B0 == 0 && g_deactivations == std::vector<bool>({true, false}) &&
                         g_linkRemoves.size() == 1, __LINE__);
        g_gameRole = GAME_ROLE_CLIENT;
        v.m_unknown5B0 = 0x55;
        car->m_netObject = reinterpret_cast<C_NetObject *>(&g_fakeNet);
        car->StopMission(false);
        Expect(stop, v.m_unknown5B0 == 0x55 && g_netRemoved.size() == 1 && g_netRemoved[0] == car->m_netObject, __LINE__);
        g_netRemoved.clear();
        g_gameRole = GAME_ROLE_LOCAL;
        car->StopMission(true);
        Expect(stop, v.m_unknown5B0 == 0x55 && g_linkRemoves.size() == 2, __LINE__);
    }

    // --- Network games: the net object, the quantisations, the place; the seats' bookkeeping and
    // the weapons' refills; a client's car solid only where vehicles are allowed.
    bool net = true;
    for (uint32_t role : {GAME_ROLE_CLIENT, GAME_ROLE_SERVER}) {
        const bool server = role == GAME_ROLE_SERVER;
        g_gameRole = role;
        g_fakeNet = S_FakeNet();
        g_netFinds.clear();
        g_quantSetups.clear();
        g_netC4.clear();
        g_netC8.clear();
        g_weaponEvents.clear();
        g_mpSettingsValue.allowVehicles = 0;
        store.counted.clear();
        car->m_netObject = nullptr;
        car->m_stateChanged = !server;
        car->SetPosition4(pos0);
        car->m_direction = dir0;
        v.m_topSpeed = server ? 41.5f : 30.0f;
        const float top = v.m_topSpeed;
        // A refused weapon (the first weapon seat on the client: seat 1 of the crafted ones) is
        // deleted.
        g_failInitSeat = !server && n > 1 ? 1 : -1;
        // Crafted dummy properties (the shipped ones never stop at a space, have "nam" before
        // "name" or lack the text): the flags start over.
        static const char *const props[] = {"SEAT12 3,name7", "SEAT8,4 namX name42", "seat9", "SEAT01"};
        static const uint32_t flags[] = {0x6, 0x100, 0, 0x3};
        static const int texts[] = {7, 42, -1, -1};
        for (int i = 0; i < n; ++i) {
            v.m_seats[i].frame->m_userProps = props[i % 4];
            v.m_seats[i].flags = 0;
        }
        if (server)
            g_cacheFailName = "C" + g_modelFile;        // (the wreck's model missing: released, Fix)
        const size_t made0 = g_createdModels.size();
        const int t0 = static_cast<int>(GetPerfTimeMs());
        car->StartMission(server);
        const int t1 = static_cast<int>(GetPerfTimeMs());
        g_cacheFailName.clear();
        g_failInitSeat = -1;
        const S_FakeNet &o = g_fakeNet;
        Expect(net, car->m_stateChanged == server, __LINE__);
        for (int i = 0; i < n; ++i)
            Expect(net, v.m_seats[i].flags == flags[i % 4] && v.m_seats[i].text == texts[i % 4], __LINE__);
        Expect(net, g_netFinds.size() == 1 && g_netFinds[0].first == 77 && g_netFinds[0].second &&
                        car->m_netObject == reinterpret_cast<C_NetObject *>(&g_fakeNet) && o.read == &CarNetReadState &&
                        o.write == &CarNetWriteState && o.stateContext == car && o.unknown40 == 0 &&
                        o.factory == &MakeSeatWeaponMessage && o.factoryContext == car, __LINE__);
        const auto same = [](const S_vector4 &a, float x, float y, float z) {
            return a.x == x && a.y == y && a.z == z;
        };
        Expect(net, g_quantSetups.size() == 2 && g_quantSetups[0].q == &car->m_quantizations[0] &&
                        same(g_quantSetups[0].span, top + top, 20.0f, top + top) &&
                        same(g_quantSetups[0].from, -top, -10.0f, -top) &&
                        same(g_quantSetups[0].step, top * 0.01f, 0.2f, top * 0.01f) && g_quantSetups[0].unknown == 100 &&
                        g_quantSetups[1].q == &car->m_quantizations[1] &&
                        same(g_quantSetups[1].span, top * 0.1f, top * 0.05f, top * 0.1f) &&
                        same(g_quantSetups[1].from, top * -0.05f, top * -0.025f, top * -0.05f) &&
                        same(g_quantSetups[1].step, 0.01f, 0.013f, 0.01f) && g_quantSetups[1].unknown == 100, __LINE__);
        Expect(net, o.sets0 == 1 && o.sets1 == 1 && !o.q0[0] && o.q0[1] == &car->m_quantizations[1] &&
                        o.q0[2] == &car->m_quantizations[0] && !o.q1[0] && o.q1[1] == &car->m_quantizations[1] &&
                        o.q1[2] == &car->m_quantizations[0] && o.propertyAsks == std::vector<int>{1} &&
                        o.property.fields == std::vector<std::pair<int, int>>{{2, 0x3b}} &&
                        o.property.track.values.size() == 1 && Same4(o.property.track.values[0], pos0, 0.0f) &&
                        o.property.dir.values.size() == 1 && Same4(o.property.dir.values[0].first, dir0, 0.0f) &&
                        o.property.dir.values[0].second == 0 && g_netC4 == (server ? std::vector<int>{0} : std::vector<int>{}) &&
                        g_netC8 == (server ? std::vector<int>{1} : std::vector<int>{}), __LINE__);
        // Started (the server's side too, in a network game), the seats' leavers, the volumes.
        bool started = car->m_started && car->m_seatLeftTimes && car->m_seatLeftIds &&
                       v.m_specialVolumesOn == (server && !v.m_specialVolumes.empty()) &&
                       v.m_mainVolumesInWorld == server && v.m_volume490InWorld == server && g_linkAdds.size() == 2 &&
                       car->m_lastAttacker == -1;
        for (int i = 0; started && i < n; ++i)
            started = car->m_seatLeftTimes[i] >= t0 && car->m_seatLeftTimes[i] <= t1 && car->m_seatLeftIds[i] == -1;
        Expect(net, started, __LINE__);
        // The weapons: the store's rounds now, the messages' sender; the refused one deleted.
        int refused = -1, armed = 0;
        for (int i = 0; i < n; ++i)
            armed += (v.m_seats[i].flags & SEAT_FLAG_WEAPON) ? 1 : 0;
        for (int i = 0; i < n && !server; ++i) {
            if (v.m_seats[i].flags & SEAT_FLAG_WEAPON) {
                refused = i;
                break;
            }
        }
        std::vector<std::string> events;
        for (int i = n; i-- > 0;) {
            if (!(v.m_seats[i].flags & SEAT_FLAG_WEAPON))
                continue;
            events.push_back("make");
            events.push_back("init " + std::to_string(i));
            if (i == refused)
                events.push_back("delete " + std::to_string(i));
        }
        Expect(net, g_weaponEvents == events, __LINE__);
        for (int i = 0; i < n; ++i) {
            C_SeatWeapon *w = car->m_seatWeapons[i];
            if ((v.m_seats[i].flags & SEAT_FLAG_WEAPON) && i != refused)
                Expect(net, w && Fake(w).refillBelow == 17 && Fake(w).sender == &SeatWeaponSendMessage &&
                                Fake(w).senderContext == car, __LINE__);
            else
                Expect(net, !w, __LINE__);
        }
        Expect(net, store.counted == std::vector<int>(static_cast<size_t>(armed - (refused >= 0 ? 1 : 0)), 61), __LINE__);
        if (server)
            Expect(net, !car->m_wreckModel && g_createdModels.size() > made0 && g_createdModels.back()->m_refCount == 0,
                   __LINE__);
        // The restart: the replicated place and the state; then the stop: the object removed, the
        // leavers freed.
        car->m_stateChanged = false;
        car->RestartMission(server);
        // (Quirk kept: the vehicle's Restart puts the volumes back in the world, also a client's car
        // where vehicles are not allowed.)
        Expect(net, g_fakeNet.property.track.values.size() == 2 && g_fakeNet.property.dir.values.size() == 2 &&
                        g_fakeNet.property.dir.values[1].second == 0 && car->m_stateChanged &&
                        v.m_specialVolumesOn == (server && !v.m_specialVolumes.empty()) && v.m_mainVolumesInWorld &&
                        v.m_volume490InWorld && g_linkAdds.size() == 2, __LINE__);
        g_mpSettingsValue.allowVehicles = 1;
        car->RestartMission(server);
        Expect(net, car->m_vehiclesAllowed && v.m_mainVolumesInWorld && v.m_volume490InWorld &&
                        v.m_specialVolumesOn == !v.m_specialVolumes.empty(), __LINE__);
        g_netRemoved.clear();
        g_weaponEvents.clear();
        car->StopMission(server);
        Expect(net, g_netRemoved.size() == 1 && g_netRemoved[0] == car->m_netObject && !car->m_seatLeftTimes &&
                        !car->m_seatLeftIds && !car->m_started && g_linkRemoves.size() == 2, __LINE__);
    }

    std::printf("mission: start %d vehicle %d seats %d wreck %d restore %d restart %d stop %d net %d\n", start, vehicle,
                seats, wreck, restore, restart, stop, net);
    g_flatGround = false;
    std::printf("mission seats:%s\n", seatLine.c_str());
    g_gameRole = GAME_ROLE_LOCAL;
    g_mpSettingsValue.allowVehicles = 0;
    GetGame()->m_scene = nullptr;
    GetGame()->m_checkpoints = nullptr;
    car->m_containers = nullptr;
    for (int ticks = 0; ticks < 20 && std::any_of(g_carEmitters.begin(), g_carEmitters.end(),
                                                  [](const S_CarEmitter &e) { return e.active; });
         ++ticks)
        UpdateCarEffectEmitters(83);
    delete car;
    g_made.clear();
    g_images.clear();
    g_meshes = g_meshesReleased = g_materialsMade = g_materialsReleased = 0;
}

// --- the network side (src/game/car_net.cpp; part 2f-i) -----------------------------------------------


// A seat weapon's message that tells when it is deleted.
struct S_TestShot : C_SeatWeaponShotMessage {
    bool *deleted = nullptr;
    ~S_TestShot() override
    {
        if (deleted)
            *deleted = true;
    }
};
struct S_TestReload : C_SeatWeaponReloadMessage {
    bool *deleted = nullptr;
    ~S_TestReload() override
    {
        if (deleted)
            *deleted = true;
    }
};

// The bits a car message writes (through SendMessage's stream).
T_Bits BitsOf(C_Car *car, C_CarMessage &msg)
{
    ClearStreams();
    car->SendMessage(msg);
    return g_streams.back()->bits;
}
S_vector XyzOf(const S_vector4 &v)
{
    return S_vector(v.x, v.y, v.z);
}
// `bits` to the car as the server's message (OnMessage) or a client's (Unknown20): all read?
bool Deliver(C_Car *car, const T_Bits &bits, bool fromClient)
{
    g_readBits = bits;
    g_readPos = 0;
    g_readMismatch = false;
    static int stream;
    if (fromClient)
        car->Unknown20(reinterpret_cast<C_BitStream *>(&stream));
    else
        car->OnMessage(reinterpret_cast<C_BitStream *>(&stream));
    return !g_readMismatch && g_readPos == g_readBits.size();
}

void NetTest(const char *path)
{
    if (!LoadModel(path))
        return;
    InitPerfTime();
    auto *car = new C_Car();
    car->SetFrame(g_model);
    C_Vehicle &v = car->m_vehicle;
    v.Start();
    const int n = static_cast<int>(v.m_seats.size());
    car->SetNetId(77);
    g_gameRole = GAME_ROLE_CLIENT;
    g_coop = false;
    g_mpSettingsValue.gameType = MP_GAME_DEATHMATCH;
    GetGame()->m_scene = g_carScene;
    static int replication;
    static S_FakeNet netObject;
    netObject = S_FakeNet();
    g_netReplication = reinterpret_cast<C_NetReplication *>(&replication);
    car->m_netObject = reinterpret_cast<C_NetObject *>(&netObject);
    C_FakeStore store;
    C_ItemContainer *containers[1] = {&store};
    car->m_containers = containers;
    g_created.clear();
    g_queued.clear();
    g_actorsById.clear();
    g_lightingUpdates.clear();
    g_selectedMember = nullptr;
    g_localPlayerSlot = 3;
    // A checkpoint network of one point far away (the restart marks the checkpoints).
    alignas(16) static uint8_t networkMemory[sizeof(C_CheckpointNetwork)];
    std::memset(networkMemory, 0, sizeof networkMemory);
    C_CheckpointNetwork &network = *reinterpret_cast<C_CheckpointNetwork *>(networkMemory);
    S_Checkpoint farPoint[1];
    farPoint[0].pos = S_vector(5000.0f, 0.0f, 5000.0f);
    network.m_points = farPoint;
    network.m_pointCount = 1;
    GetGame()->m_checkpoints = reinterpret_cast<C_Checkpoints *>(&network);
    int wheel = -1;                                     // the first wheel there is
    for (int i = 0; i < v.m_wheelCount && wheel < 0; ++i)
        if (v.m_wheels[i])
            wheel = i;

    // --- the messages: written and read back; the fuel to the other machines.
    bool messages = true;
    {
        C_CarPartEffectMessage effect;
        effect.part = 9;
        const T_Bits bits = BitsOf(car, effect);
        Expect(messages, bits == T_Bits{{5, 4}, {9, 4}}, __LINE__);
        g_readBits.assign(bits.begin() + 1, bits.end());
        g_readPos = 0;
        g_readMismatch = false;
        static int stream;
        C_CarPartEffectMessage back;
        back.Read(reinterpret_cast<C_BitStream *>(&stream));
        Expect(messages, !g_readMismatch && g_readPos == 1 && back.part == 9 && back.GetKind() == CAR_MSG_PART_EFFECT, __LINE__);
        for (float fuel : {0.0f, 0.1f, 123.4f, 2500.0f, 4999.9f, 5000.0f}) {
            C_CarFuelMessage m(fuel);
            const T_Bits written = BitsOf(car, m);
            const uint32_t raw = static_cast<uint32_t>(static_cast<int>(fuel * 0.0002f * 65535.0f));
            Expect(messages, written == T_Bits{{10, 4}, {raw, 16}}, __LINE__);
            g_readBits.assign(written.begin() + 1, written.end());
            g_readPos = 0;
            C_CarFuelMessage read;
            read.Read(reinterpret_cast<C_BitStream *>(&stream));
            const float want = std::min(static_cast<float>(raw) / 65535.0f * 5000.0f, 5000.0f);
            Expect(messages, read.fuel == want && std::fabs(read.fuel - fuel) <= 0.08f, __LINE__);
        }
        // SendFuel: the server's to the clients (SendMessage), a client's to the server (Ex).
        v.m_fuel = 33.0f;
        for (uint32_t role : {GAME_ROLE_SERVER, GAME_ROLE_CLIENT}) {
            g_gameRole = role;
            ClearStreams();
            car->SendFuel();
            const uint32_t raw = static_cast<uint32_t>(static_cast<int>(33.0f * 0.0002f * 65535.0f));
            Expect(messages, g_streams.size() == 1 && g_streams[0]->ex == (role == GAME_ROLE_CLIENT) &&
                                 g_streams[0]->id == 77 && g_streams[0]->sent && g_streams[0]->bits == T_Bits{{10, 4}, {raw, 16}},
                   __LINE__);
        }
        g_gameRole = GAME_ROLE_CLIENT;
    }

    // --- OnMessage: the server's messages on a client.
    bool server = true;
    {
        // Glass: the part's health, hit this step, cracked (at none shattered).
        int glass = -1;
        for (int i = 0; i < static_cast<int>(v.m_parts.size()) && glass < 0; ++i)
            if (v.m_parts[i].type == VEHICLE_PART_GLASS)
                glass = i;
        const int parts = static_cast<int>(v.m_parts.size());
        if (glass >= 0) {
            S_VehiclePart &p = v.m_parts[glass];
            p.state = 0;
            v.m_unknown58C = 0;
            C_CarGlassMessage m(glass, 0, S_vector(0.1f, 0.2f, 0.3f));
            Expect(server, Deliver(car, BitsOf(car, m), false) && p.health == 0.0f && (p.state & VEHICLE_PART_STATE_2) &&
                               (p.state & VEHICLE_PART_STATE_1) && (v.m_unknown58C & 2), __LINE__);
        }
        // A part: its health; at none it comes off (a bumper-like part is thrown).
        int loose = -1;
        for (int i = 0; i < parts && loose < 0; ++i) {
            const int type = v.m_parts[i].type;
            if (type == VEHICLE_PART_BUMPER || type == VEHICLE_PART_MIRROR || type == VEHICLE_PART_LIGHT ||
                type == VEHICLE_PART_WING || type == VEHICLE_PART_PLATE || type == VEHICLE_PART_DOOR)
                loose = i;
        }
        if (loose >= 0) {
            S_VehiclePart &p = v.m_parts[loose];
            p.state = 0;
            p.health = 100.0f;
            p.frame->m_frameFlags |= 1u;
            v.m_unknown58C = 0;
            g_debris.clear();
            C_CarPartMessage hurt(loose, 20, S_vector(1, 0, 0), S_vector(0, 1, 0));
            Expect(server, Deliver(car, BitsOf(car, hurt), false) && p.health == 20.0f && (p.state & VEHICLE_PART_STATE_2) &&
                               (v.m_unknown58C & 2) && g_debris.empty() && p.frame->IsOn(), __LINE__);
            C_CarPartMessage off(loose, 0, S_vector(1, 0, 0), S_vector(0, 1, 0));
            Expect(server, Deliver(car, BitsOf(car, off), false) && p.health == 0.0f && g_debris.size() == 1 &&
                               !p.frame->IsOn() && g_debris[0].setup && g_debris[0].params.velocity.x == 1.0f &&
                               g_debris[0].params.velocity.y == 1.0f / g_debris[0].params.weight, __LINE__);
        }
        // (Fix) a part past the car's: read, nothing else.
        {
            v.m_unknown58C = 0;
            C_CarGlassMessage m(parts, 5, S_vector(0, 0, 1));
            C_CarPartMessage m2(parts, 5, S_vector(0, 0, 1), S_vector(0, 0, 1));
            Expect(server, Deliver(car, BitsOf(car, m), false) && Deliver(car, BitsOf(car, m2), false) && v.m_unknown58C == 0,
                   __LINE__);
        }
        if (wheel >= 0) {
            S_VehicleWheel *w = v.m_wheels[wheel];
            // A wheel: the client takes the health (hit this step); at none it comes off; the server
            // leaves the server's message alone; a broken wheel is passed over.
            w->flags &= ~(WHEEL_HIT | WHEEL_DETACHED);
            w->health = 100.0f;
            v.m_unknown58C = 0;
            C_CarWheelMessage hurt(wheel, 50, S_vector(0, 0, 0));
            Expect(server, Deliver(car, BitsOf(car, hurt), false) && w->health == 50.0f && (w->flags & WHEEL_HIT) &&
                               (v.m_unknown58C & 2) && !(w->flags & WHEEL_DETACHED), __LINE__);
            g_gameRole = GAME_ROLE_SERVER;
            C_CarWheelMessage hurt2(wheel, 30, S_vector(0, 0, 0));
            Expect(server, Deliver(car, BitsOf(car, hurt2), false) && w->health == 50.0f, __LINE__);
            g_gameRole = GAME_ROLE_CLIENT;
            const int mode = v.m_unknown550;
            v.m_unknown550 = 1;
            C_CarWheelMessage none(wheel, 0, S_vector(0, 2, 0));
            Expect(server, Deliver(car, BitsOf(car, none), false) && w->health == 0.0f && !(w->flags & WHEEL_DETACHED),
                   __LINE__);
            v.m_unknown550 = mode;
            w->health = 100.0f;
            Expect(server, Deliver(car, BitsOf(car, none), false) && w->health == 0.0f && (w->flags & WHEEL_DETACHED), __LINE__);
            w->flags &= ~WHEEL_HIT;
            Expect(server, Deliver(car, BitsOf(car, hurt), false) && w->health == 0.0f && !(w->flags & WHEEL_HIT), __LINE__);
            w->flags &= ~WHEEL_DETACHED;
            w->frame->m_frameFlags |= 1u;
            w->health = 100.0f;
            // A tyre: on its rim.
            w->flags &= ~WHEEL_ON_RIM;
            const bool soundsOn = v.m_soundsOn;
            v.m_soundsOn = true;
            // (A fake burst where the model has none, taken out again after.)
            I3D_sound *&slot = v.m_unknown2298 ? v.m_sounds1[0x1C] : v.m_sounds2[0x1D];
            const bool planted = slot == nullptr;
            if (planted)
                slot = static_cast<I3D_sound *>(MakeSound());
            const I3D_frame *burst = slot;
            g_soundLog.clear();
            C_CarTyreMessage tyre(wheel);
            bool burstPlayed = false;
            const bool rim = Deliver(car, BitsOf(car, tyre), false) && (w->flags & WHEEL_ON_RIM);
            for (const auto &e : g_soundLog)
                burstPlayed |= e.first == burst && e.second == 'O';
            if (planted)
                slot = nullptr;
            v.m_soundsOn = soundsOn;
            Expect(server, rim && burstPlayed, __LINE__);
            w->flags &= ~WHEEL_ON_RIM;
        }
        // (Fix) a wheel past the car's.
        {
            C_CarWheelMessage m(v.m_wheelCount, 5, S_vector(0, 0, 0));
            C_CarTyreMessage tyre(v.m_wheelCount);
            Expect(server, Deliver(car, BitsOf(car, m), false) && Deliver(car, BitsOf(car, tyre), false), __LINE__);
        }
        // A part's effect; the restart (the replicated place); the store (added on a client only); the
        // fuel; the server's kinds: the kind only.
        C_CarPartEffectMessage effect;
        effect.part = 3;
        // (The last explosive part, its effect started; none: nothing.)
        S_CarParts &carParts = car->m_parts;
        effect.part = carParts.explosives.empty() ? 3 : static_cast<int>(carParts.explosives.size()) - 1;
        for (S_CarExplosive *e : carParts.explosives)
            e->effect = -1;
        g_particlesMade.clear();
        const bool started = carParts.explosives.empty() || carParts.explosives.back()->effectId == -1;
        Expect(server, Deliver(car, BitsOf(car, effect), false) &&
                           (started ? g_particlesMade.empty()
                                    : g_particlesMade.size() == 1 && carParts.explosives.back()->effect == g_particlesMade[0].handle &&
                                          g_particlesMade[0].effect == carParts.explosives.back()->effectId), __LINE__);
        car->m_startPosition = car->GetPosition4();
        car->m_startDirection = car->GetDirection4();
        const size_t places = netObject.property.track.values.size();
        car->m_stateChanged = false;
        Expect(server, Deliver(car, T_Bits{{6, 4}}, false) && netObject.property.track.values.size() == places + 1 &&
                           car->m_stateChanged, __LINE__);
        const T_Bits storeBits = {{7, 4}, {300, 16}, {12, 8}, {301, 16}, {40, 8}, {0xFFFF, 16}};
        g_created.clear();
        store.added.clear();
        Expect(server, Deliver(car, storeBits, false) && g_created.size() == 2 && g_created[0].typeId == 12 &&
                           g_created[0].uid == 300 && g_created[1].typeId == 40 && g_created[1].uid == 301 &&
                           !g_created[0].owner && g_created[0].container == 0 && store.added.size() == 2 &&
                           store.added[0] == g_created[0].item && store.addIndices.back() == -1, __LINE__);
        g_gameRole = GAME_ROLE_SERVER;
        g_created.clear();
        Expect(server, Deliver(car, storeBits, false) && g_created.empty(), __LINE__);
        g_gameRole = GAME_ROLE_CLIENT;
        C_CarFuelMessage fuel(17.0f);
        v.m_fuel = 0.0f;
        const T_Bits fuelBits = BitsOf(car, fuel);
        Expect(server, Deliver(car, fuelBits, false) && std::fabs(v.m_fuel - std::min(17.0f, v.m_tankSize)) < 0.08f, __LINE__);
        Expect(server, Deliver(car, T_Bits{{8, 4}}, false) && Deliver(car, T_Bits{{9, 4}}, false) && !car->m_wrecked, __LINE__);
        // The wreck: the parts' effects, ShowDestroyed (below).
        g_particlesMade.clear();
        v.m_unknown610 = 1.0f;
        Expect(server, Deliver(car, T_Bits{{4, 4}}, false) && g_particlesMade.size() == 2 && v.m_unknown610 == 0.0f, __LINE__);
        v.m_unknown610 = 1.0f;
    }

    // --- Unknown20: a client's messages on the server.
    bool client = true;
    {
        g_gameRole = GAME_ROLE_SERVER;
        if (wheel >= 0) {
            S_VehicleWheel *w = v.m_wheels[wheel];
            w->health = 100.0f;
            w->flags &= ~(WHEEL_HIT | WHEEL_DETACHED);
            C_CarWheelMessage hurt(wheel, 40, S_vector(0, 0, 0));
            Expect(client, Deliver(car, BitsOf(car, hurt), true) && w->health == 40.0f && (w->flags & WHEEL_HIT), __LINE__);
            w->health = 100.0f;
        }
        // The deadly ground: the parts' +0x64, wrecked as told.
        car->m_wrecked = false;
        car->m_parts.wrecked = false;
        Expect(client, Deliver(car, T_Bits{{8, 4}}, true) && car->m_wrecked && car->m_parts.wrecked, __LINE__);
        car->m_wrecked = false;
        // A hit: the crew's living occupants (from the last) by the driver; or the human of the id.
        std::vector<C_Human *> crew;
        for (int i = 0; i < n; ++i) {
            crew.push_back(new C_Human());
            crew.back()->SetNetId(60 + i);
            v.m_seats[i].occupant = crew.back();
        }
        if (n >= 2)
            crew[1]->health = 0;
        g_hitOrder.clear();
        const S_vector impulse(1.0f, 2.0f, 3.0f), point(4.0f, 5.0f, 6.0f);
        C_CarHitMessage all(-1, 250, impulse, point);
        bool crewHit = Deliver(car, BitsOf(car, all), true);
        std::vector<const C_Actor *> order;
        for (int i = n; i-- > 0;)
            if (crew[i]->health > 0)
                order.push_back(crew[i]);
        crewHit &= g_hitOrder == order;
        for (int i = 0; i < n; ++i) {
            const bool alive = crew[i]->health > 0;
            crewHit &= crew[i]->hits.size() == (alive ? 1u : 0u);
            if (alive) {
                const C_Human::S_Hit &h = crew[i]->hits[0];
                crewHit &= h.type == HIT_THROWN_OBJECT && h.amount == 250.0f && SameV(h.pos, point, 0.0f) &&
                           SameV(h.dir, impulse, 0.0f) && h.attacker == crew[0] && h.flags == 0;
            }
        }
        Expect(client, crewHit, __LINE__);
        auto *runOver = new C_Human();
        runOver->SetNetId(90);
        g_actorsById[90] = runOver;
        C_CarHitMessage one(90, 1000, impulse, point);
        Expect(client, Deliver(car, BitsOf(car, one), true) && runOver->hits.size() == 1 && runOver->hits[0].amount == 1000.0f &&
                           SameV(runOver->hits[0].pos, point, 0.0f) && runOver->hits[0].attacker == (n ? crew[0] : nullptr),
               __LINE__);
        C_CarHitMessage nobody(91, 1000, impulse, point);
        Expect(client, Deliver(car, BitsOf(car, nobody), true) && runOver->hits.size() == 1, __LINE__);
        g_actorsById.clear();
        delete runOver;
        for (int i = 0; i < n; ++i)
            v.m_seats[i].occupant = nullptr;
        for (C_Human *c : crew)
            delete c;
        // The fuel: taken and sent on to the clients.
        C_CarFuelMessage fuel(21.0f);
        const T_Bits fuelBits = BitsOf(car, fuel);
        ClearStreams();
        v.m_fuel = 0.0f;
        const bool taken = Deliver(car, fuelBits, true) && std::fabs(v.m_fuel - std::min(21.0f, v.m_tankSize)) < 0.08f &&
                           g_streams.size() == 1 && !g_streams[0]->ex;
        // (Sent on as read: quantised again, it may lose a step.)
        const T_Bits relayed = g_streams.empty() ? T_Bits{} : g_streams[0]->bits;
        g_readBits.assign(fuelBits.begin() + 1, fuelBits.end());
        g_readPos = 0;
        static int fuelStream;
        C_CarFuelMessage read;
        read.Read(reinterpret_cast<C_BitStream *>(&fuelStream));
        Expect(client, taken && relayed == BitsOf(car, read), __LINE__);
        // The server's kinds: the kind only.
        g_particlesMade.clear();
        Expect(client, Deliver(car, T_Bits{{4, 4}}, true) && Deliver(car, T_Bits{{6, 4}}, true) && g_particlesMade.empty(), __LINE__);
        g_gameRole = GAME_ROLE_CLIENT;
    }

    // --- ReadMessages: the seat weapons' messages.
    bool weapons = true;
    {
        S_FakeWeapon fakes[10];
        const int seat = n - 1;                         // (a gunner seat)
        auto *gunner = new C_Human();
        if (n > 0) {
            car->m_seatWeapons[seat] = AsWeapon(fakes[seat]);
            v.m_seats[seat].occupant = gunner;
        }
        const S_vector4 pos = {1, 2, 3, 0}, dir = {0, 0, 1, 0};
        const auto shot = [&](bool *deleted) {
            auto *m = new S_TestShot();
            m->deleted = deleted;
            m->seat = seat;
            m->pos = pos;
            m->dir = dir;
            m->barrel = 1;
            m->fromClient = true;
            m->unknown24 = 1;                           // (held by the queue it came from)
            netObject.incoming.push_back(m);
        };
        const auto reload = [&](bool *deleted) {
            auto *m = new S_TestReload();
            m->deleted = deleted;
            m->seat = seat;
            m->fromClient = true;
            m->unknown24 = 1;
            netObject.incoming.push_back(m);
        };
        if (n > 0) {
            S_FakeWeapon &f = fakes[seat];
            // A client shows another's shot and reload (then deletes them).
            g_gameRole = GAME_ROLE_CLIENT;
            bool d1 = false, d2 = false;
            shot(&d1);
            reload(&d2);
            const int reads = g_messageReads;
            car->ReadMessages();
            Expect(weapons, f.playShots == 1 && f.showReloads == 1 && f.shots.empty() && f.reloads == 0 && d1 && d2 &&
                                g_queued.empty() && g_messageReads == reads + 1 && netObject.incoming.empty(), __LINE__);
            // (The arguments the original passes: the message's barrel, the car's item store.)
            Expect(weapons, f.playShotBarrel == 1 && f.showReloadStore == car->GetContainer(0) &&
                                f.showReloadStore != nullptr, __LINE__);
            // Its own player's seat, a dead gunner: passed over.
            g_selectedMember = gunner;
            d1 = d2 = false;
            shot(&d1);
            reload(&d2);
            car->ReadMessages();
            g_selectedMember = nullptr;
            gunner->health = 0;
            bool d3 = false;
            shot(&d3);
            car->ReadMessages();
            gunner->health = 800;
            Expect(weapons, f.playShots == 1 && f.showReloads == 1 && d1 && d2 && d3, __LINE__);
            // The server fires it and sends it on (not deleted: the queue holds it), reloads likewise;
            // a shot it does not fire is deleted.
            g_gameRole = GAME_ROLE_SERVER;
            g_selectedMember = gunner;              // (the server's own player does not matter)
            d1 = d2 = d3 = false;
            shot(&d1);
            reload(&d2);
            car->ReadMessages();
            Expect(weapons, f.shots.size() == 1 && Same4(f.shots[0].pos, pos, 0.0f) && Same4(f.shots[0].dir, dir, 0.0f) &&
                                f.shots[0].barrel == 1 && f.shots[0].unused == 1000 && f.reloads == 1 &&
                                f.reloadStore == car->GetContainer(0) && !d1 && !d2 && g_queued.size() == 2 &&
                                g_queued[0].type == SEAT_WEAPON_MSG_SHOT && g_queued[0].seat == seat &&
                                g_queued[0].sender == 6 && g_queued[0].reliable && !g_queued[0].fromClient &&
                                g_queued[1].type == SEAT_WEAPON_MSG_RELOAD && !g_queued[1].fromClient &&
                                g_queued[1].reliable && g_queued[1].sender == 6, __LINE__);
            f.fires = false;
            shot(&d3);
            car->ReadMessages();
            Expect(weapons, f.shots.size() == 2 && d3 && g_queued.size() == 2, __LINE__);
            f.fires = true;
            g_selectedMember = nullptr;
            // (Fix) a seat without a weapon: passed over.
            car->m_seatWeapons[seat] = nullptr;
            bool d4 = false;
            shot(&d4);
            car->ReadMessages();
            Expect(weapons, f.shots.size() == 2 && d4, __LINE__);
            car->m_seatWeapons[seat] = AsWeapon(f);
            // The rounds; the ammo for the store.
            auto *rounds = new C_SeatWeaponRoundsMessage();
            rounds->seat = seat;
            rounds->rounds = 44;
            rounds->unknown24 = 1;
            netObject.incoming.push_back(rounds);
            auto *ammo = new C_SeatWeaponAmmoMessage();
            ammo->typeId = 61;
            ammo->uid = 500;
            ammo->unknown24 = 1;
            netObject.incoming.push_back(ammo);
            g_created.clear();
            store.added.clear();
            car->ReadMessages();
            Expect(weapons, f.roundsSet == std::vector<int>{44} && g_created.size() == 1 && g_created[0].typeId == 61 &&
                                g_created[0].uid == 500 && store.added.size() == 1 && store.added[0] == g_created[0].item &&
                                store.addIndices.back() == -1, __LINE__);
            car->m_seatWeapons[seat] = nullptr;
            v.m_seats[seat].occupant = nullptr;
        }
        delete gunner;
        g_gameRole = GAME_ROLE_CLIENT;
    }

    // --- the item store: written and read back.
    bool items = true;
    {
        C_FakeItem a, b, c;
        a.m_uid = 7;
        a.m_typeId = 2;
        b.m_uid = 1023;
        b.m_typeId = 200;
        c.m_uid = 55;
        c.m_typeId = 9;
        store.slots = {{&a, &b}, {&c}};
        ClearStreams();
        C_BitStream *msg = CreateActorMessage(77, false);
        car->WriteStore(msg);
        const T_Bits bits = g_streams.back()->bits;
        Expect(items, bits == T_Bits{{7, 16}, {2, 8}, {1023, 16}, {200, 8}, {55, 16}, {9, 8}, {0xFFFF, 16}}, __LINE__);
        store.slots.clear();
        for (bool add : {true, false}) {
            g_readBits = bits;
            g_readPos = 0;
            g_readMismatch = false;
            g_created.clear();
            static int stream;
            car->ReadStore(reinterpret_cast<C_BitStream *>(&stream), add);
            Expect(items, !g_readMismatch && g_readPos == bits.size() && g_created.size() == (add ? 3u : 0u), __LINE__);
            if (add)
                Expect(items, g_created[0].uid == 7 && g_created[0].typeId == 2 && g_created[1].uid == 1023 &&
                                  g_created[2].typeId == 9, __LINE__);
        }
    }

    // --- UpdateNetwork: the driving machine's place, rotation and velocity; the others' replay.
    bool replicated = true;
    {
        // The model turned: its rotation's axis and angle (LS3D's Inverse, the s after the axis).
        S_quat rot;
        rot.Make(S_vector(0.3f, 0.8f, 0.5f).Normalized(), 0.7f);
        g_model->SetRot(rot);
        S_vector4 axis = {};
        float angle = 0.0f;
        g_model->m_rot.Inverse(reinterpret_cast<S_vector &>(axis), angle);
        auto *driver = new C_Human();
        driver->SetNetId(3);                            // the local player's slot
        car->m_driver = driver;
        car->SetPosition4({10.0f, 20.0f, 30.0f, 1.0f});
        v.m_velocity = {1.0f, 0.0f, -2.0f, 0.0f};
        car->m_vehiclesAllowed = true;
        v.m_unknown600 |= VEHICLE_PHYSICS_KEEP_AWAKE;
        netObject = S_FakeNet();
        g_gameRole = GAME_ROLE_CLIENT;
        g_networkTimes.clear();
        car->UpdateNetwork(50);
        const S_FakeNet &o = netObject;
        Expect(replicated, o.unknown3C == 7 && o.sendMask == 7 && o.property.track.values.size() == 1 &&
                                Same4(o.property.track.values[0], {10.0f, 20.0f, 30.0f, 1.0f}, 0.0f) &&
                                o.property.dir.values.size() == 1 && Same4(o.property.dir.values[0].first, axis, 0.0f) &&
                                o.property.dir.values[0].second == angle && o.property.track.rates.size() == 1 &&
                                SameV(o.property.track.rates[0], S_vector(1.0f, 0.0f, -2.0f), 0.0f) &&
                                o.ticks == std::vector<float>{50.0f * 0.001f} && o.updates.empty() &&
                                !(v.m_unknown600 & VEHICLE_PHYSICS_KEEP_AWAKE) && o.property.flagSets.empty(), __LINE__);
        // Vehicles not allowed: kept 8 m under the start place.
        car->m_vehiclesAllowed = false;
        car->m_startPosition = {5.0f, 6.0f, 7.0f, 1.0f};
        car->m_direction = {0.0f, 0.0f, 1.0f, 0.0f};
        g_dirs.erase(g_model);
        car->UpdateNetwork(50);
        Expect(replicated, Same4(car->GetPosition4(), {5.0f, -2.0f, 7.0f, 1.0f}, 0.0f) &&
                                Same4(v.m_modelPos, {5.0f, -2.0f, 7.0f, 1.0f}, 0.0f) &&
                                SameV(g_model->GetPos(), S_vector(5.0f, -2.0f, 7.0f), 0.0f) && g_dirs.count(g_model) &&
                                SameV(g_dirs[g_model], S_vector(0.0f, 0.0f, 1.0f), 0.0f), __LINE__);
        car->m_vehiclesAllowed = true;
        // Elsewhere: another's driver (a player on the server, anyone on a client), or none on a
        // client. The replay only once the rotation's track has a value.
        driver->SetNetId(4);
        driver->role = HUMAN_ROLE_PLAYER;
        for (uint32_t role : {GAME_ROLE_CLIENT, GAME_ROLE_SERVER}) {
            g_gameRole = role;
            netObject = S_FakeNet();
            netObject.property.flags = 0xFFFFFFFFu;
            netObject.property.track.current = {40.0f, 2.0f, -9.0f, 1.0f};
            netObject.property.track.rate = {3.0f, 4.0f, 0.0f, 0.0f};
            netObject.property.dir.current = {0.0f, 1.0f, 0.0f, 0.0f};
            netObject.property.dir.angle = 1.25f;
            car->SetPosition4({1.0f, 1.0f, 1.0f, 1.0f});
            g_lightNotes = 0;
            v.SetCallback(VEHICLE_CALLBACK_IMPULSE, reinterpret_cast<void *>(&NoteLights));
            car->UpdateNetwork(40);
            Expect(replicated, o.unknown3C == 7 && o.sendMask == (role == GAME_ROLE_SERVER ? 7 : 2) &&
                                    o.property.flagSets == std::vector<uint32_t>{0xFFFDFFFFu} && o.ticks.empty() &&
                                    o.updates == std::vector<float>{40.0f * 0.001f} && o.property.track.values.empty() &&
                                    Same4(car->GetPosition4(), {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f) && g_lightNotes == 0 &&
                                    v.m_unknown600 == VEHICLE_PHYSICS_AWAKE, __LINE__);
            // The rotation set: the place, the turn (axis and angle), the velocity and its speed.
            netObject.property.dir.target = {0.0f, 0.0f, 0.5f, 0.0f};
            std::vector<S_vector> was(static_cast<size_t>(v.m_wheelCount));
            for (int i = 0; i < v.m_wheelCount; ++i)
                if (v.m_wheels[i])
                    was[static_cast<size_t>(i)] = XyzOf(v.m_wheels[i]->unknown050);
            car->UpdateNetwork(40);
            S_quat want;
            want.Make(S_vector(0.0f, 1.0f, 0.0f), 1.25f);
            want.Normalize();
            bool replayed = Same4(car->GetPosition4(), {40.0f, 2.0f, -9.0f, 1.0f}, 0.0f) &&
                            Same4(car->GetDirection4(), {0.0f, 1.0f, 0.0f, 0.0f}, 0.0f) &&
                            SameV(g_model->GetPos(), S_vector(40.0f, 2.0f, -9.0f), 0.0f) &&
                            g_model->m_rot.s == want.s && SameV(g_model->m_rot.v, want.v, 0.0f) &&
                            Same4(v.m_velocity, {3.0f, 4.0f, 0.0f, 0.0f}, 0.0f) && g_lightNotes == 1 &&
                            g_lightNoteContext == v.m_callbackContext && SameV(XyzOf(v.m_modelPos), S_vector(40.0f, 2.0f, -9.0f), 0.0f);
            // (The speed: ReadModelState takes it from the velocity again.)
            replayed &= std::fabs(v.m_speed - 5.0f) < 1e-5f;
            // The rear axle's wheels (the middle two): their centres' velocities.
            const float inverse = 1.0f / (40.0f * 0.001f);
            for (int i = v.m_wheelCount / 2; i < v.m_wheelCount / 2 + 2 && i < v.m_wheelCount; ++i) {
                const S_VehicleWheel *w = v.m_wheels[i];
                if (!w)
                    continue;
                const S_vector &old = was[static_cast<size_t>(i)];
                replayed &= SameXYZ(w->unknown060, XyzOf(w->worldPos)) &&
                            SameV(w->unknown0A0, S_vector((w->worldPos.x - old.x) * inverse, (w->worldPos.y - old.y) * inverse,
                                                          (w->worldPos.z - old.z) * inverse), 0.0f);
            }
            Expect(replicated, replayed, __LINE__);
        }
        // The server: its own player driving, a car without a player driving (another's AI): its own.
        g_gameRole = GAME_ROLE_SERVER;
        driver->SetNetId(3);
        netObject = S_FakeNet();
        car->UpdateNetwork(40);
        Expect(replicated, netObject.ticks.size() == 1 && netObject.updates.empty() && netObject.sendMask == 7, __LINE__);
        driver->SetNetId(4);
        driver->role = HUMAN_ROLE_ENEMY;
        netObject = S_FakeNet();
        car->UpdateNetwork(40);
        Expect(replicated, netObject.ticks.size() == 1 && netObject.updates.empty() && netObject.sendMask == 7, __LINE__);
        car->m_driver = nullptr;
        netObject = S_FakeNet();
        car->UpdateNetwork(40);
        Expect(replicated, netObject.ticks.size() == 1 && netObject.updates.empty(), __LINE__);
        delete driver;
        g_gameRole = GAME_ROLE_CLIENT;
        v.SetCallback(VEHICLE_CALLBACK_IMPULSE, nullptr);
    }

    // --- the state handlers: every 300 ms the sliding and locked wheels, the gear, the steering
    // and the revs; applied while the net object's +0x3C is set.
    bool state = true;
    {
        // Wait for the shared timer (made at the first call; on GetPerfTimeMs, which every model's
        // tests restart, so it may hold the last model's time): until it fires, then its period.
        const int64_t giveUp = GetPerfTimeMs() + 20000;
        do {
            ClearStreams();
            CarNetWriteState(car, CreateActorMessage(77, false));
        } while (g_streams.back()->bits[0].first == 0 && GetPerfTimeMs() < giveUp);
        const int64_t until = GetPerfTimeMs() + 320;
        while (GetPerfTimeMs() < until) {
        }
        for (int i = 0; i < v.m_wheelCount; ++i)
            if (v.m_wheels[i])
                v.m_wheels[i]->flags &= ~(WHEEL_SLIDING | WHEEL_LOCKED);
        std::vector<int> sliding, locked;
        for (int i = 0; i < v.m_wheelCount; ++i) {
            if (!v.m_wheels[i])
                continue;
            if (i % 2 == 0) {
                v.m_wheels[i]->flags |= WHEEL_SLIDING;
                sliding.push_back(i);
            }
            if (i % 3 == 0) {
                v.m_wheels[i]->flags |= WHEEL_LOCKED;
                locked.push_back(i);
            }
        }
        v.m_gear = 2;
        v.m_unknown94C = 0.25f;
        const float limit = v.m_revLimit > 0.0f ? v.m_revLimit : 100.0f;
        v.m_revLimit = limit;
        v.m_engineRevs = limit * 1.5f;                  // (clamped by the writer)
        ClearStreams();
        C_BitStream *msg = CreateActorMessage(77, false);
        CarNetWriteState(car, msg);
        const T_Bits written = g_streams.back()->bits;
        // The expectation, worked out apart (the bit counts in float as the original's FYL2X).
        const auto countBits = [](float x) { return static_cast<int>(std::log(x) / std::log(2.0f) + 1.0f); };
        const int wheelBits = countBits(static_cast<float>(v.m_wheelCount));
        T_Bits want = {{1, 1}};
        for (int i : sliding)
            want.emplace_back(static_cast<uint32_t>(i), wheelBits);
        want.emplace_back(static_cast<uint32_t>(v.m_wheelCount), wheelBits);
        for (int i : locked)
            want.emplace_back(static_cast<uint32_t>(i), wheelBits);
        want.emplace_back(static_cast<uint32_t>(v.m_wheelCount), wheelBits);
        want.emplace_back(3u, 4);
        want.emplace_back(static_cast<uint32_t>(static_cast<int>((0.25f + 1.0f) * 0.5f * 63.0f)), 6);
        want.emplace_back(255u, 8);                     // (the clamped revs: all of the limit)
        Expect(state, written == want && v.m_engineRevs == limit, __LINE__);
        // A little later again: not due.
        const int64_t later = GetPerfTimeMs() + 5;
        while (GetPerfTimeMs() < later) {
        }
        ClearStreams();
        msg = CreateActorMessage(77, false);
        CarNetWriteState(car, msg);
        Expect(state, g_streams.back()->bits == T_Bits{{0, 1}}, __LINE__);
        // Read back: applied (+0x3C set) or only the wheels' flags cleared.
        v.m_unknown934 = 1.0f;
        v.m_unknown5F4 = 1.0f;                          // (linear: the steering as read)
        for (int apply : {1, 0}) {
            for (int i = 0; i < v.m_wheelCount; ++i)
                if (v.m_wheels[i])
                    v.m_wheels[i]->flags |= WHEEL_SLIDING | WHEEL_LOCKED;
            v.m_gear = 0;
            v.m_steering = 0.0f;
            v.m_engineRevs = 0.0f;
            netObject.unknown3C = apply;
            g_readBits = written;
            g_readPos = 0;
            g_readMismatch = false;
            static int stream;
            CarNetReadState(car, reinterpret_cast<C_BitStream *>(&stream));
            bool flags = true;
            for (int i = 0; i < v.m_wheelCount; ++i) {
                if (!v.m_wheels[i])
                    continue;
                const bool s = apply && std::find(sliding.begin(), sliding.end(), i) != sliding.end();
                const bool l = apply && std::find(locked.begin(), locked.end(), i) != locked.end();
                flags &= ((v.m_wheels[i]->flags & WHEEL_SLIDING) != 0) == s && ((v.m_wheels[i]->flags & WHEEL_LOCKED) != 0) == l;
            }
            const float steering = static_cast<float>(static_cast<int>((0.25f + 1.0f) * 0.5f * 63.0f)) / 63.0f;
            Expect(state, !g_readMismatch && g_readPos == written.size() && flags && v.m_gear == (apply ? 2 : 0) &&
                              v.m_steering == (apply ? steering + steering - 1.0f : 0.0f) &&
                              v.m_engineRevs == (apply ? limit : 0.0f), __LINE__);
        }
        // Half the revs: read back as their share of the limit.
        {
            const int64_t due = GetPerfTimeMs() + 320;
            while (GetPerfTimeMs() < due) {
            }
            v.m_engineRevs = limit * 0.5f;
            ClearStreams();
            CarNetWriteState(car, CreateActorMessage(77, false));
            const T_Bits half = g_streams.back()->bits;
            const uint32_t raw = static_cast<uint32_t>(static_cast<int>(limit * 0.5f / limit * 255.0f));
            netObject.unknown3C = 1;
            g_readBits = half;
            g_readPos = 0;
            g_readMismatch = false;
            static int stream;
            CarNetReadState(car, reinterpret_cast<C_BitStream *>(&stream));
            Expect(state, half.back() == std::pair<uint32_t, int>(raw, 8) && !g_readMismatch &&
                              v.m_engineRevs == static_cast<float>(raw) / 255.0f * limit, __LINE__);
            netObject.unknown3C = 0;
        }
        // Nothing sent: nothing read past the bit.
        g_readBits = {{0, 1}};
        g_readPos = 0;
        g_readMismatch = false;
        static int stream;
        CarNetReadState(car, reinterpret_cast<C_BitStream *>(&stream));
        Expect(state, !g_readMismatch && g_readPos == 1, __LINE__);
        // (Fix) an index past the wheels: ignored.
        g_readBits = {{1, 1}, {static_cast<uint32_t>(v.m_wheelCount + 1), wheelBits}, {static_cast<uint32_t>(v.m_wheelCount), wheelBits},
                      {static_cast<uint32_t>(v.m_wheelCount), wheelBits}, {1, 4}, {0, 6}, {0, 8}};
        g_readPos = 0;
        netObject.unknown3C = 1;
        CarNetReadState(car, reinterpret_cast<C_BitStream *>(&stream));
        Expect(state, !g_readMismatch && g_readPos == g_readBits.size() && v.m_gear == 0, __LINE__);
        netObject.unknown3C = 0;
    }

    // --- ShowDestroyed: a client's wreck.
    bool destroyed = true;
    {
        // The wheels: the first of the first six off stays off, one past them is whole again.
        std::vector<int> present;
        for (int i = 0; i < v.m_wheelCount; ++i)
            if (v.m_wheels[i])
                present.push_back(i);
        const int offWheel = present.empty() ? -1 : present[0];
        const int offWheel2 = present.size() > 1 && present[1] < 6 ? present[1] : -1;
        const int lateWheel = !present.empty() && present.back() >= 6 ? present.back() : -1;
        for (int i : present) {
            v.m_wheels[i]->flags &= ~WHEEL_DETACHED;
            v.m_wheels[i]->frame->m_frameFlags |= 1u;
        }
        if (offWheel >= 0)
            v.m_wheels[offWheel]->flags |= WHEEL_DETACHED;
        if (offWheel2 >= 0)
            v.m_wheels[offWheel2]->flags |= WHEEL_DETACHED;
        if (lateWheel >= 0)
            v.m_wheels[lateWheel]->flags |= WHEEL_DETACHED;
        // The wreck's model (empty: no meshes change), the charge, the crew (one with a movement).
        car->m_wreckModel = static_cast<I3D_model *>(NewFrame(FRAME_MODEL, "wreck", nullptr));
        car->m_wreckShown = false;
        I3D_frame *charge = NewFrame(FRAME_VISUAL, "charge", nullptr);
        car->m_chargeVisual = charge;
        alignas(16) static uint8_t movementMemory[sizeof(C_HumanMovement)];
        std::memset(movementMemory, 0, sizeof movementMemory);
        auto *movement = reinterpret_cast<C_HumanMovement *>(movementMemory);
        auto *rider = new C_Human();
        rider->SetFrame(NewFrame(FRAME_MODEL, "rider", nullptr));
        rider->movement = movement;
        auto *other = new C_Human();
        other->SetFrame(NewFrame(FRAME_MODEL, "other", nullptr));
        if (n > 0)
            v.m_seats[n - 1].occupant = rider;
        if (n > 1)
            v.m_seats[0].occupant = other;
        // The parts (whole again after the vehicle's restart): glass shatters, the others are thrown
        // off by seed.
        for (S_VehiclePart &p : v.m_parts) {
            p.state = VEHICLE_PART_STATE_1;
            p.health = 0.0f;
        }
        std::srand(1234);
        std::vector<char> thrown(v.m_parts.size(), 0);
        std::vector<float> speeds(v.m_parts.size(), 0.0f);
        for (size_t i = v.m_parts.size(); i-- > 0;) {
            speeds[i] = (RandFloat() - 0.5f) * 10.0f + 20.0f;
            if (v.m_parts[i].type != VEHICLE_PART_GLASS)
                thrown[i] = std::rand() & 1;
        }
        g_debris.clear();
        std::srand(1234);
        g_gameRole = GAME_ROLE_LOCAL;                   // (simulated here: the hop)
        v.m_speed = 0.0f;
        v.m_velocity = {0.0f, 0.0f, 0.0f, 0.0f};
        v.m_fuel = 10.0f;
        v.m_unknown610 = 1.0f;
        v.m_unknown5B0 = VEHICLE_LIGHT_HEADLIGHTS;
        car->ShowDestroyed();
        bool wheels = true;
        for (int i : present) {
            const bool off = i == offWheel || i == offWheel2;
            wheels &= ((v.m_wheels[i]->flags & WHEEL_DETACHED) != 0) == off && v.m_wheels[i]->frame->IsOn() == !off;
        }
        Expect(destroyed, wheels && v.m_fuel == 0.0f && v.m_unknown610 == 0.0f && !(v.m_unknown5B0 & VEHICLE_LIGHT_HEADLIGHTS) &&
                              v.m_velocity.y > 0.0f && car->m_wreckShown && !car->m_chargeVisual && charge->m_refCount == 0,
               __LINE__);
        bool parts = true;
        for (size_t i = 0; i < v.m_parts.size(); ++i) {
            const S_VehiclePart &p = v.m_parts[i];
            if (p.type == VEHICLE_PART_GLASS)
                parts &= p.health == 0.0f && (p.state & VEHICLE_PART_STATE_1);
            else
                parts &= p.health == (thrown[i] ? 0.0f : p.fullHealth);
        }
        Expect(destroyed, parts, __LINE__);
        // The thrown ones' debris: pushed out from the model's place (the direction normalized, at its
        // speed, at least 5 up), from rest.
        bool pushes = true;
        for (size_t i = 0; i < v.m_parts.size(); ++i) {
            const S_VehiclePart &p = v.m_parts[i];
            const S_DebrisMade *made = nullptr;
            for (const S_DebrisMade &d : g_debris)
                if (d.frame == p.frame && d.setup)
                    made = &d;
            if (!thrown[i] || !made)
                continue;
            S_vector dir = p.frame->GetWorldPos() - v.m_modelMatrix(3);
            dir = dir.Normalized();
            dir = S_vector(dir.x * speeds[i], std::fabs(dir.y * speeds[i]), dir.z * speeds[i]);
            if (dir.y < 5.0f)
                dir.y = 5.0f;
            const S_vector got(made->params.velocity.x * made->params.weight, made->params.velocity.y * made->params.weight,
                               made->params.velocity.z * made->params.weight);
            pushes &= std::fabs(got.x - dir.x) < 1e-3f && std::fabs(got.y - dir.y) < 1e-3f && std::fabs(got.z - dir.z) < 1e-3f;
        }
        Expect(destroyed, pushes, __LINE__);
        // The crew darkened: the one with a movement.
        Expect(destroyed, n == 0 || (g_lightingUpdates.size() == 1 && g_lightingUpdates[0].lighting == &movement->m_visual.m_lighting &&
                                     g_lightingUpdates[0].model == rider->GetFrame() && g_lightingUpdates[0].time == 500 &&
                                     g_lightingUpdates[0].mode == 0 && g_lightingUpdates[0].level == 0.2f &&
                                     g_lightingUpdates[0].shownLevel == 0.3f), __LINE__);
        // Not simulated here (another's car on a client): no hop.
        g_gameRole = GAME_ROLE_CLIENT;
        auto *driver = new C_Human();
        driver->SetNetId(8);
        car->m_driver = driver;
        v.m_velocity = {0.0f, 0.0f, 0.0f, 0.0f};
        v.m_speed = 0.0f;
        car->m_wreckShown = false;
        car->ShowDestroyed();
        Expect(destroyed, v.m_velocity.y == 0.0f, __LINE__);
        car->m_driver = nullptr;
        delete driver;
        for (int i = 0; i < n; ++i)
            v.m_seats[i].occupant = nullptr;
        delete rider;
        delete other;
        car->m_wreckModel = nullptr;
    }

    std::printf("car net: messages %d server %d client %d weapons %d items %d replication %d state %d destroyed %d\n",
                messages, server, client, weapons, items, replicated, state, destroyed);
    g_gameRole = GAME_ROLE_LOCAL;
    GetGame()->m_scene = nullptr;
    GetGame()->m_checkpoints = nullptr;
    car->m_containers = nullptr;
    v.Stop();
    for (int ticks = 0; ticks < 20 && std::any_of(g_carEmitters.begin(), g_carEmitters.end(),
                                                  [](const S_CarEmitter &e) { return e.active; });
         ++ticks)
        UpdateCarEffectEmitters(83);
    delete car;
    g_made.clear();
    g_images.clear();
    g_meshes = g_meshesReleased = g_materialsMade = g_materialsReleased = 0;
}

// --- the saved game and a joining client's state (src/game/car_save.cpp, car_net.cpp; part 2f-ii) ----

extern int g_actorSaveTag;
extern std::vector<int> g_actorLoads;
extern std::vector<int> g_itemDefAsks;

// A chunk file's chunks at one level: id, contents (0xFFFF: a header past the end).
struct S_SavedChunk {
    uint16_t id;
    std::vector<uint8_t> data;
};
std::vector<S_SavedChunk> ChunksOf(const std::vector<uint8_t> &file)
{
    std::vector<S_SavedChunk> out;
    size_t pos = 0;
    while (pos + 6 <= file.size()) {
        uint16_t id;
        uint32_t size;
        std::memcpy(&id, file.data() + pos, 2);
        std::memcpy(&size, file.data() + pos + 2, 4);
        if (size < 6 || pos + size > file.size()) {
            out.push_back({0xFFFF, {}});
            break;
        }
        out.push_back({id, std::vector<uint8_t>(file.begin() + static_cast<long>(pos) + 6,
                                                file.begin() + static_cast<long>(pos + size))});
        pos += size;
    }
    if (pos != file.size() && (out.empty() || out.back().id != 0xFFFF))
        out.push_back({0xFFFF, {}});
    return out;
}
template <typename T> void Put(std::vector<uint8_t> &out, const T &value)
{
    const auto *p = reinterpret_cast<const uint8_t *>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}
void PutBytes(std::vector<uint8_t> &out, const void *data, size_t size)
{
    const auto *p = static_cast<const uint8_t *>(data);
    out.insert(out.end(), p, p + size);
}
std::vector<uint8_t> ChunkBytes(uint16_t id, const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> out;
    Put(out, id);
    Put(out, static_cast<uint32_t>(data.size() + 6));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}
// The car's LoadGame on `chunks` (inside an outer chunk, as the actors module's).
bool LoadCar(C_Car *car, const std::vector<uint8_t> &chunks)
{
    g_files["load.sav"] = ChunkBytes(0x7777, chunks);
    C_chunk in;
    in.Open("load.sav", false);
    uint16_t id = 0;
    in.Descend(id);
    const bool loaded = car->LoadGame(in);
    in.Ascend();
    in.Close();
    return loaded;
}

void SaveTest(const char *path)
{
    if (!LoadModel(path))
        return;
    InitPerfTime();
    auto *car = new C_Car();
    car->SetFrame(g_model);
    C_Vehicle &v = car->m_vehicle;
    v.Start();
    const int n = static_cast<int>(v.m_seats.size());
    car->SetNetId(77);
    g_gameRole = GAME_ROLE_LOCAL;
    GetGame()->m_scene = g_carScene;
    g_frameVtable[0x4C / 4] = reinterpret_cast<void *>(&F_Duplicate);
    g_frameVtable[0x14 / 4] = reinterpret_cast<void *>(&F_SetPosDir);
    g_itemModel = NewFrame(FRAME_VISUAL, "charge model", nullptr);
    car->m_wreckModel = static_cast<I3D_model *>(NewFrame(FRAME_MODEL, "wreck", nullptr));
    int wheel = -1;                                     // the first wheel there is
    for (int i = 0; i < v.m_wheelCount && wheel < 0; ++i)
        if (v.m_wheels[i])
            wheel = i;

    // The seats: the even ones with a weapon (flagged; seat 3's not), seat 1 flagged without one.
    std::vector<C_SeatWeapon *> weapons(n, nullptr);
    for (int i = 0; i < n; ++i) {
        v.m_seats[i].flags &= ~SEAT_FLAG_WEAPON;
        if (i % 2 == 0 || i == 3) {
            weapons[i] = new C_SeatWeapon();
            Fake(weapons[i]).saveTag = 100 + i;
            car->m_seatWeapons[i] = weapons[i];
        }
        if (i % 2 == 0 || i == 1)
            v.m_seats[i].flags |= SEAT_FLAG_WEAPON;
    }
    const auto savedSeat = [&](int i) { return i < n && weapons[i] && (v.m_seats[i].flags & SEAT_FLAG_WEAPON); };

    // --- SaveGame: the chunks in turn.
    bool save = true;
    car->m_wreckShown = false;
    car->m_wrecked = true;
    car->m_periodicLink = 41;
    car->m_lastAttacker = 9;
    car->m_chargeTime = 1234;
    car->m_chargeOwner = 6;
    car->m_chargePos = {1.5f, -2.0f, 3.25f, 9.0f};
    car->m_chargeNormal = {0.0f, 1.0f, 0.0f, 7.0f};
    v.m_unknown610 = 0.625f;
    g_actorSaveTag = 5150;
    car->m_parts.fuse = 616;
    car->m_parts.wrecked = false;
    g_files.clear();
    C_chunk out;
    out.Open("car.sav", true);
    out.Begin(0x7777);
    const bool saved = car->SaveGame(out);
    out.Ascend();
    out.Close();
    const std::vector<uint8_t> file = g_files["car.sav"];
    const std::vector<S_SavedChunk> outer = ChunksOf(file);
    std::vector<S_SavedChunk> chunks;
    if (outer.size() == 1 && outer[0].id == 0x7777)
        chunks = ChunksOf(outer[0].data);
    std::vector<S_SavedChunk> want;
    {
        std::vector<uint8_t> actor;
        Put(actor, g_actorSaveTag);
        want.push_back({0x2715, ChunkBytes(0x3039, actor)});
        const uint32_t size = v.StateSize();
        std::vector<uint8_t> block(size);
        v.SaveState(block.data());
        std::vector<uint8_t> vehicle;
        Put(vehicle, size);
        vehicle.insert(vehicle.end(), block.begin(), block.end());
        want.push_back({10000, vehicle});
        for (int i = n; i-- > 0;) {
            if (!savedSeat(i))
                continue;
            std::vector<uint8_t> seat;
            Put(seat, i);
            Put(seat, 100 + i);
            want.push_back({0x2711, seat});
        }
        want.push_back({0x2713, {0, 1}});
        std::vector<uint8_t> links;
        Put(links, 41);
        Put(links, 9);
        want.push_back({0x2714, links});
        C_chunk partsOut;
        partsOut.Open("parts.sav", true);
        car->m_parts.SaveGame(partsOut);
        partsOut.Close();
        want.push_back({0x2716, g_files["parts.sav"]});
        std::vector<uint8_t> charge;
        Put(charge, 1234);
        Put(charge, 6);
        PutBytes(charge, &car->m_chargePos, 12);
        PutBytes(charge, &car->m_chargeNormal, 12);
        want.push_back({0x2717, charge});
    }
    Expect(save, !saved && chunks.size() == want.size(), __LINE__);
    for (size_t k = 0; k < chunks.size() && k < want.size(); ++k)
        Expect(save, chunks[k].id == want[k].id && chunks[k].data == want[k].data, __LINE__);
    // The wreck's flags as they are.
    car->m_wreckShown = true;
    car->m_wrecked = false;
    g_files.clear();
    out.Open("car.sav", true);
    out.Begin(0x7777);
    car->SaveGame(out);
    out.Ascend();
    out.Close();
    {
        const std::vector<S_SavedChunk> again = ChunksOf(ChunksOf(g_files["car.sav"])[0].data);
        bool wreck = false;
        for (const S_SavedChunk &c : again)
            wreck |= c.id == 0x2713 && c.data == std::vector<uint8_t>{1, 0};
        Expect(save, wreck, __LINE__);
    }
    car->m_wreckShown = false;

    // --- LoadGame: every chunk back; the charge's visual made again; the wreck shown.
    bool load = true;
    std::vector<uint8_t> body;
    for (const S_SavedChunk &c : chunks) {
        const std::vector<uint8_t> bytes = ChunkBytes(c.id, c.data);
        body.insert(body.end(), bytes.begin(), bytes.end());
    }
    // Shown as a wreck: the glass not yet gone shatters, the rest stays.
    std::vector<uint8_t> shown = body;
    for (size_t k = 0, pos = 0; k < chunks.size(); pos += chunks[k].data.size() + 6, ++k)
        if (chunks[k].id == 0x2713)
            shown[pos + 6] = 1;
    const auto resetCar = [&] {
        car->m_wreckShown = false;
        car->m_wrecked = false;
        car->m_periodicLink = -5;
        car->m_lastAttacker = -6;
        car->m_chargeTime = -1;
        car->m_chargeOwner = 0;
        car->m_chargePos = {};
        car->m_chargeNormal = {};
        car->m_chargeVisual = nullptr;
        v.m_unknown610 = 0.1f;
        for (int i = 0; i < n; ++i)
            if (weapons[i])
                Fake(weapons[i]).loadedTags.clear();
        g_actorLoads.clear();
        car->m_parts.fuse = 0;
        g_itemDefAsks.clear();
        g_duplicates.clear();
        g_posDirs.clear();
        for (S_VehiclePart &p : v.m_parts) {
            p.state = 0;
            p.health = p.fullHealth;
            p.frame->m_frameFlags |= 1u;
        }
    };
    for (bool wreck : {false, true}) {
        resetCar();
        // A glass part already gone keeps its health and stays seen.
        int goneGlass = -1;
        for (size_t i = 0; i < v.m_parts.size() && goneGlass < 0; ++i)
            if (v.m_parts[i].type == VEHICLE_PART_GLASS) {
                goneGlass = static_cast<int>(i);
                v.m_parts[i].state = VEHICLE_PART_STATE_1;
                v.m_parts[i].health = 7.0f;
            }
        const bool loaded = LoadCar(car, wreck ? shown : body);
        Expect(load, loaded && g_actorLoads == std::vector<int>{5150} && car->m_parts.fuse == 616 &&
                         v.m_unknown610 == 0.625f && car->m_wrecked && car->m_periodicLink == 41 &&
                         car->m_lastAttacker == 9 && car->m_chargeTime == 1234 && car->m_chargeOwner == 6 &&
                         SameV(V3(car->m_chargePos), S_vector(1.5f, -2.0f, 3.25f), 0.0f) &&
                         SameV(V3(car->m_chargeNormal), S_vector(0.0f, 1.0f, 0.0f), 0.0f), __LINE__);
        for (int i = 0; i < n; ++i)
            if (weapons[i])
                Expect(load, Fake(weapons[i]).loadedTags == (savedSeat(i) ? std::vector<int>{100 + i} : std::vector<int>{}),
                       __LINE__);
        I3D_frame *visual = car->m_chargeVisual;
        Expect(load, visual && g_itemDefAsks == std::vector<int>{66} && g_duplicates[visual] == g_itemModel &&
                         visual->GetParent() == car->GetFrame() && g_posDirs.count(visual) &&
                         SameV(g_posDirs[visual].pos, S_vector(1.5f, -2.0f, 3.25f), 0.0f) &&
                         SameV(g_posDirs[visual].dir, S_vector(0.0f, 1.0f, 0.0f), 0.0f) && g_posDirs[visual].roll == 0.0f,
               __LINE__);
        Expect(load, car->m_wreckShown == wreck, __LINE__);
        bool parts = true;
        for (size_t i = 0; i < v.m_parts.size(); ++i) {
            const S_VehiclePart &p = v.m_parts[i];
            if (static_cast<int>(i) == goneGlass)
                parts &= p.health == 7.0f && p.state == VEHICLE_PART_STATE_1 && p.frame->IsOn();
            else if (wreck && p.type == VEHICLE_PART_GLASS)
                parts &= p.health == 0.0f && p.state == VEHICLE_PART_STATE_1 && !p.frame->IsOn();
            else
                parts &= p.health == p.fullHealth && p.state == 0 && p.frame->IsOn();
        }
        Expect(load, parts, __LINE__);
        if (visual)
            visual->LinkTo(nullptr, 0);
    }
    // No charge: no visual.
    {
        resetCar();
        std::vector<uint8_t> none;
        Put(none, -1);
        Put(none, 3);
        PutBytes(none, &car->m_chargePos, 12);
        PutBytes(none, &car->m_chargeNormal, 12);
        Expect(load, LoadCar(car, ChunkBytes(0x2717, none)) && car->m_chargeTime == -1 && car->m_chargeOwner == 3 &&
                         !car->m_chargeVisual && g_itemDefAsks.empty(), __LINE__);
    }

    // --- The quirks and fixes: an unknown chunk is not left (its contents read next); a seat's index
    // that cannot be read is the one before; seats past the weapons or without one are passed over;
    // an empty vehicle block is not loaded.
    bool quirks = true;
    {
        resetCar();
        std::vector<uint8_t> links;
        Put(links, 77);
        Put(links, 78);
        Expect(quirks, LoadCar(car, ChunkBytes(0x2712, ChunkBytes(0x2714, links))) && car->m_periodicLink == 77 &&
                           car->m_lastAttacker == 78, __LINE__);
        resetCar();
        std::vector<uint8_t> file2;
        std::vector<uint8_t> seat0;
        Put(seat0, 0);
        Put(seat0, 500);
        for (const std::vector<uint8_t> &c : {ChunkBytes(0x2711, seat0), ChunkBytes(0x2711, {})})
            file2.insert(file2.end(), c.begin(), c.end());
        for (int bad : {12, -1, 1, 3, 10}) {
            std::vector<uint8_t> s;
            Put(s, bad);
            Put(s, 600);
            const std::vector<uint8_t> c = ChunkBytes(0x2711, s);
            file2.insert(file2.end(), c.begin(), c.end());
        }
        LoadCar(car, file2);
        bool seats = n == 0 || Fake(weapons[0]).loadedTags == std::vector<int>{500, -1};
        if (n > 3)
            seats &= Fake(weapons[3]).loadedTags == std::vector<int>{600};   // (a weapon, though not flagged)
        Expect(quirks, seats, __LINE__);
        resetCar();
        std::vector<uint8_t> empty;
        Put(empty, 0u);
        Expect(quirks, LoadCar(car, ChunkBytes(10000, empty)) && v.m_unknown610 == 0.1f, __LINE__);
    }

    // --- WriteState: the frame's place and rotation, the store, the engine's health, the wreck, the
    // wheels off (from the last), the fuel.
    bool state = true;
    C_FakeStore store;
    C_ItemContainer *containers[1] = {&store};
    car->m_containers = containers;
    C_FakeItem item;
    item.m_uid = 300;
    item.m_typeId = 12;
    store.slots = {{&item}};
    S_quat rot;
    rot.Make(S_vector(0.3f, 0.8f, 0.5f).Normalized(), 0.7f);
    g_model->SetPosInline(S_vector(4.0f, 1.25f, -8.5f));
    g_model->SetRot(rot);
    S_vector4 axis = {};
    float angle = 0.0f;
    g_model->m_rot.Inverse(reinterpret_cast<S_vector &>(axis), angle);
    for (int i = 0; i < v.m_wheelCount; ++i)
        if (v.m_wheels[i])
            v.m_wheels[i]->flags &= ~WHEEL_DETACHED;
    if (wheel >= 0)
        v.m_wheels[wheel]->flags |= WHEEL_DETACHED;
    v.m_unknown610 = 0.75f;
    v.m_fuel = 1234.5f;
    // (The model under a frame placed elsewhere: its own place is sent, not the world's.)
    I3D_frame *const parent0 = g_model->GetParent();
    I3D_frame *holder = NewFrame(FRAME_DUMMY, "holder", nullptr);
    holder->m_localMatrix(3) = S_vector(100.0f, 0.0f, 0.0f);
    Unlink(g_model);
    g_model->LinkTo(holder, 0);
    for (bool wreck : {false, true}) {
        car->m_wreckShown = wreck;
        ClearStreams();
        C_BitStream *msg = CreateActorMessage(77, false);
        car->WriteState(msg);
        T_Bits bits = {{Bits(4.0f), 32},    {Bits(1.25f), 32},  {Bits(-8.5f), 32}, {Bits(axis.x), 32},
                       {Bits(axis.y), 32}, {Bits(axis.z), 32}, {Bits(angle), 32}, {300, 16},
                       {12, 8},            {0xFFFF, 16},       {Bits(0.75f), 32}, {wreck ? 1u : 0u, 1}};
        for (int i = v.m_wheelCount; i-- > 0;)
            bits.emplace_back(i == wheel ? 1u : 0u, 1);
        bits.emplace_back(static_cast<uint32_t>(static_cast<int>(1234.5f * 0.0002f * 65535.0f)), 16);
        Expect(state, g_streams.back()->bits == bits, __LINE__);
    }
    Expect(state, std::fabs(g_model->GetWorldPos().x - 104.0f) < 1e-3f, __LINE__);
    Unlink(g_model);
    if (parent0)
        g_model->LinkTo(parent0, 0);
    store.slots.clear();
    car->m_wreckShown = false;

    // --- ReadState: the place and rotation to the frame and the tracks, the items added, the wreck
    // shown with the engine off, the wheels sent off thrown off, the fuel.
    bool read = true;
    {
        static S_FakeNet netObject;
        netObject = S_FakeNet();
        car->m_netObject = reinterpret_cast<C_NetObject *>(&netObject);
        g_gameRole = GAME_ROLE_CLIENT;
        g_created.clear();
        g_debris.clear();
        for (int i = 0; i < v.m_wheelCount; ++i)
            if (v.m_wheels[i]) {
                v.m_wheels[i]->flags &= ~WHEEL_DETACHED;
                v.m_wheels[i]->frame->m_frameFlags |= 1u;
            }
        v.m_unknown22C0 = 0;
        v.SetEngine(true, true);
        const S_vector readAxis = S_vector(0.0f, 0.6f, 0.8f);
        // (Half a tank: not clamped.)
        const uint32_t fuelRaw = static_cast<uint32_t>(v.m_tankSize * 0.5f / 5000.0f * 65535.0f);
        T_Bits bits = {{Bits(-3.0f), 32},     {Bits(2.5f), 32},      {Bits(11.0f), 32},     {Bits(readAxis.x), 32},
                       {Bits(readAxis.y), 32}, {Bits(readAxis.z), 32}, {Bits(1.1f), 32},     {450, 16},
                       {21, 8},                {0xFFFF, 16},          {Bits(0.375f), 32},    {1, 1}};
        for (int i = v.m_wheelCount; i-- > 0;)
            bits.emplace_back(i == wheel ? 1u : 0u, 1);
        bits.emplace_back(fuelRaw, 16);
        // The thrown wheel, as Wreck's: pushed out from the centre of mass, spun up.
        S_vector push(0.0f, 0.0f, 0.0f), velocity(0.0f, 0.0f, 0.0f);
        std::srand(77);
        if (wheel >= 0) {
            push = V3(v.m_wheels[wheel]->worldPos) - V3(v.m_unknown1B0);
            NormalizeZXY(push);
            const float speed = ((RandFloat() - 0.5f) + 1.0f) * 10.0f;
            push = S_vector(push.x * speed, std::fabs(push.y * speed), push.z * speed);
            velocity = push;
            NormalizeZXY(velocity);
            const float up = RandFloat() + 2.0f;
            velocity = S_vector(velocity.x * up, up, velocity.z * up);
        }
        std::srand(77);
        g_readBits = bits;
        g_readPos = 0;
        g_readMismatch = false;
        static int stream;
        car->ReadState(reinterpret_cast<C_BitStream *>(&stream));
        Expect(read, !g_readMismatch && g_readPos == bits.size(), __LINE__);
        S_quat want;
        want.Make(readAxis, 1.1f);
        want.Normalize();
        const I3D_frame *frame = car->GetFrame();
        Expect(read, SameV(V3(car->m_position), S_vector(-3.0f, 2.5f, 11.0f), 0.0f) &&
                         SameV(V3(car->m_direction), readAxis, 0.0f) &&
                         SameV(frame->GetPos(), S_vector(-3.0f, 2.5f, 11.0f), 0.0f) && frame->m_rot.s == want.s &&
                         SameV(frame->m_rot.v, want.v, 0.0f) && (frame->m_frameFlags & FRMFLAGS_CHANGED) &&
                         (frame->m_frameFlags & FRMFLAGS_ROT_DIRTY), __LINE__);
        Expect(read, netObject.propertyAsks == std::vector<int>{1} && netObject.property.track.values.size() == 1 &&
                         SameV(V3(netObject.property.track.values[0]), S_vector(-3.0f, 2.5f, 11.0f), 0.0f) &&
                         netObject.property.dir.values.size() == 1 &&
                         SameV(V3(netObject.property.dir.values[0].first), readAxis, 0.0f) &&
                         netObject.property.dir.values[0].second == 1.1f, __LINE__);
        Expect(read, g_created.size() == 1 && g_created[0].typeId == 21 && g_created[0].uid == 450 && store.added.size() == 1,
               __LINE__);
        Expect(read, v.m_unknown610 == 0.375f && car->m_wreckShown && !v.m_engineOn &&
                         v.m_fuel == std::min(static_cast<float>(fuelRaw) / 65535.0f * 5000.0f, v.m_tankSize),
               __LINE__);
        bool wheels = true;
        for (int i = 0; i < v.m_wheelCount; ++i)
            if (v.m_wheels[i])
                wheels &= ((v.m_wheels[i]->flags & WHEEL_DETACHED) != 0) == (i == wheel);
        if (wheel >= 0) {
            const S_vector debris(push.x * 0.1f + velocity.x, velocity.y + push.y * 0.1f, velocity.z + push.z * 0.1f);
            wheels &= v.m_unknown22C0 == 1 && !g_debris.empty() && g_debris[0].frame == v.m_wheels[wheel]->frame &&
                      SameV(V3(g_debris[0].wheelParams.velocity), debris, 0.0f);
        }
        Expect(read, wheels, __LINE__);
        // No wreck: the engine as it was; the wheels' bits all read (a missing wheel's too).
        car->m_wreckShown = false;
        v.SetEngine(true, true);
        bits[11].first = 0;
        for (size_t k = 12; k + 1 < bits.size(); ++k)
            bits[k].first = 0;
        g_readBits = bits;
        g_readPos = 0;
        g_readMismatch = false;
        g_countUpdates = true;
        g_frameUpdates.clear();
        car->ReadState(reinterpret_cast<C_BitStream *>(&stream));
        g_countUpdates = false;
        Expect(read, !g_readMismatch && g_readPos == bits.size() && !car->m_wreckShown && v.m_engineOn &&
                         g_frameUpdates[g_model] == 1, __LINE__);
        car->m_netObject = nullptr;
    }

    std::printf("car save: save %d load %d quirks %d state %d read %d\n", save, load, quirks, state, read);
    g_gameRole = GAME_ROLE_LOCAL;
    GetGame()->m_scene = nullptr;
    car->m_containers = nullptr;
    for (int i = 0; i < n; ++i) {
        car->m_seatWeapons[i] = nullptr;
        delete weapons[i];
    }
    car->m_wreckModel = nullptr;
    v.Stop();
    for (int ticks = 0; ticks < 20 && std::any_of(g_carEmitters.begin(), g_carEmitters.end(),
                                                  [](const S_CarEmitter &e) { return e.active; });
         ++ticks)
        UpdateCarEffectEmitters(83);
    delete car;
    g_made.clear();
    g_images.clear();
    g_meshes = g_meshesReleased = g_materialsMade = g_materialsReleased = 0;
}

// --- the damage parts (src/game/car_parts.cpp; part 2g-i) ---------------------------------------------

// A damage model's frames as its export has them (the harness's own reading): type, name, user
// properties, in the file's order.
struct S_DamageFrame {
    int type;
    std::string name, props;
};
std::vector<S_DamageFrame> ReadDamageFrames(const std::string &path)
{
    std::vector<S_DamageFrame> out;
    FILE *fp = std::fopen(path.c_str(), "r");
    if (!fp)
        return out;
    char line[4096];
    while (std::fgets(line, sizeof line, fp)) {
        if (line[0] == 'f' && line[1] == ' ') {
            int type = 0, parent = 0, used = 0;
            std::sscanf(line, "f %d %d %n", &type, &parent, &used);
            std::string name = line + used;
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
                name.pop_back();
            out.push_back({type, name, ""});
        } else if (line[0] == 'P' && !out.empty()) {
            char hex[4000];
            std::sscanf(line, "P %3999s", hex);
            out.back().props = Unhex(hex);
        }
    }
    std::fclose(fp);
    return out;
}
// The int after `key` in `props` (C's atoi), -1 without the key.
int PropInt(const std::string &props, const char *key)
{
    const char *p = props.c_str();
    const char *at = std::strstr(p, key);
    return at ? std::atoi(at + std::strlen(key)) : -1;
}
bool SameName(const std::string &a, const char *b)
{
    if (a.size() != std::strlen(b))
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}
// The parts' LoadGame on `chunks` (inside an outer chunk, as the car's).
void LoadParts(S_CarParts &parts, const std::vector<uint8_t> &chunks)
{
    g_files["parts.sav"] = ChunkBytes(0x7777, chunks);
    C_chunk in;
    in.Open("parts.sav", false);
    uint16_t id = 0;
    in.Descend(id);
    parts.LoadGame(in);
    in.Ascend();
    in.Close();
}

void PartsTest(const char *path)
{
    if (!LoadModel(path))
        return;
    InitPerfTime();
    g_cacheLoads.clear();
    auto *car = new C_Car();
    car->SetFrame(g_model);
    C_Vehicle &v = car->m_vehicle;
    v.Start();
    g_gameRole = GAME_ROLE_LOCAL;
    S_CarParts &parts = car->m_parts;
    const int n = car->GetSeatCount();
    const std::string dmFile = "DM" + g_modelFile;
    const std::vector<S_DamageFrame> frames = ReadDamageFrames(DamagePath(dmFile));
    const bool has = !frames.empty();

    // What Gather finds, from the export: the frames from the last; the particle dummy's values.
    std::vector<std::string> turrets, zoneNames, explosiveNames;
    std::vector<std::pair<int, int>> zoneValues, explosiveValues;
    for (size_t i = frames.size(); i-- > 0;) {
        const S_DamageFrame &f = frames[i];
        if (f.name.find("HOR") != std::string::npos || f.name.find("VERT") != std::string::npos) {
            turrets.push_back(f.name);
        } else if (f.name.find("D_ZONE") != std::string::npos) {
            zoneNames.push_back(f.name);
            zoneValues.emplace_back(PropInt(f.props, "HP"), PropInt(f.props, "matid"));
        } else if (f.name.find("EXPL_D") != std::string::npos) {
            explosiveNames.push_back(f.name);
            explosiveValues.emplace_back(PropInt(f.props, "EXPL") * 1000, PropInt(f.props, "PARTID"));
        }
    }
    const S_DamageFrame *particle = nullptr;
    for (const S_DamageFrame &f : frames)
        if (!particle && f.type == FRAME_DUMMY && SameName(f.name, "particle"))
            particle = &f;
    const int sound = particle ? PropInt(particle->props, "SID") : -1;
    int fire = particle ? PropInt(particle->props, "PFIRE") : -1, smoke = particle ? PropInt(particle->props, "PSMOKE") : -1;
    if (fire == -1)
        fire = 0x60;
    if (smoke == -1)
        smoke = 0x5F;
    int hitBoxes = 0;
    const auto gathered = [&] {
        if (!has || !parts.model)
            return !has && !parts.model && parts.explosives.empty() && parts.zones.empty() && parts.turretFrames.empty() &&
                   parts.seatFrames.empty() && !parts.particleFrame;
        bool ok = parts.turretFrames.size() == turrets.size() && parts.zones.size() == zoneNames.size() &&
                  parts.explosives.size() == explosiveNames.size() && parts.seatFrames.size() == static_cast<size_t>(n);
        for (size_t k = 0; ok && k < turrets.size(); ++k)
            ok &= parts.turretFrames[k]->GetShortName() == turrets[k];
        for (size_t k = 0; ok && k < zoneNames.size(); ++k)
            ok &= parts.zones[k].frame->GetShortName() == zoneNames[k] && parts.zones[k].hp == zoneValues[k].first &&
                  parts.zones[k].material == zoneValues[k].second;
        for (size_t k = 0; ok && k < explosiveNames.size(); ++k) {
            const S_CarExplosive *e = parts.explosives[k];
            ok &= e->frame->GetShortName() == explosiveNames[k] && e->fuse == explosiveValues[k].first &&
                  e->effectId == explosiveValues[k].second && e->effect == -1 && e->started == 0;
        }
        // The seats' hit boxes: the visual of the seat's name, if any.
        hitBoxes = 0;
        for (int s = 0; ok && s < n; ++s) {
            char name[16];
            std::sprintf(name, "SEAT%02d", s);
            const I3D_frame *want = nullptr;
            for (int i = 0; i < parts.model->NumModelFrames(); ++i) {
                const I3D_frame *f = parts.model->GetModelFrame(i);
                if (!want && f->GetType() == FRAME_VISUAL && SameName(f->GetShortName(), name))
                    want = f;
            }
            ok &= parts.seatFrames[s] == want;
            hitBoxes += want != nullptr;
        }
        ok &= particle ? parts.particleFrame && parts.particleFrame->GetShortName() == particle->name : !parts.particleFrame;
        ok &= parts.wreckSound == sound && parts.fireEffect == fire && parts.smokeEffect == smoke;
        return ok;
    };

    // --- Setup (SetFrame's): the damage model loaded and hidden, its parts gathered.
    bool setup = true;
    Expect(setup, std::count(g_cacheLoads.begin(), g_cacheLoads.end(), dmFile) == 1 && parts.car == car, __LINE__);
    Expect(setup, has ? parts.model && !parts.model->IsOn() : !parts.model, __LINE__);
    Expect(setup, gathered(), __LINE__);

    // --- GetFrameInt: the int after the key (C's atoi), -1 without the frame or the key.
    bool props = true;
    {
        I3D_frame *f = NewFrame(FRAME_DUMMY, "test.props", nullptr);
        f->m_userProps = "HP5000,matid16,EXPL,SID-3";
        Expect(props, S_CarParts::GetFrameInt(nullptr, "HP") == -1 && S_CarParts::GetFrameInt(f, "HP") == 5000 &&
                          S_CarParts::GetFrameInt(f, "matid") == 16 && S_CarParts::GetFrameInt(f, "EXPL") == 0 &&
                          S_CarParts::GetFrameInt(f, "SID") == -3 && S_CarParts::GetFrameInt(f, "PFIRE") == -1, __LINE__);
        f->m_userProps = nullptr;
        Expect(props, S_CarParts::GetFrameInt(f, "HP") == -1, __LINE__);
    }

    // --- Reset: the explosive parts' effects destroyed and the parts gathered again whole, no fuse,
    // not wrecked, the wreck's fire and smoke destroyed (their handles kept).
    bool reset = true;
    {
        for (size_t k = 0; k < parts.explosives.size(); ++k) {
            parts.explosives[k]->effect = k == 0 ? 0 : -1;
            parts.explosives[k]->started = 1;
        }
        for (S_CarZone &z : parts.zones)
            z.hp = 1;
        parts.fuse = 50;
        parts.wrecked = true;
        parts.fireHandle = 801;
        parts.smokeHandle = 802;
        g_particlesDestroyed.clear();
        const bool explosive = !parts.explosives.empty();
        parts.Reset();
        std::vector<int> destroyed;
        if (explosive)
            destroyed.push_back(0);
        destroyed.push_back(801);
        destroyed.push_back(802);
        Expect(reset, g_particlesDestroyed == destroyed && gathered() && parts.fuse == 0 && !parts.wrecked &&
                          parts.fireHandle == 801 && parts.smokeHandle == 802, __LINE__);
        parts.fireHandle = parts.smokeHandle = -1;
        g_particlesDestroyed.clear();
        parts.Reset();
        Expect(reset, g_particlesDestroyed.empty() && gathered(), __LINE__);
    }

    // --- Tick: the fuse counted down; at its end the car wrecked, once; not once wrecked.
    bool tick = true;
    {
        parts.fuse = 100;
        parts.Tick(30);
        Expect(tick, parts.fuse == 70 && !parts.wrecked && !car->m_wrecked, __LINE__);
        // (Not told: the occupant is killed by the last attacker, none, not by the driver.)
        auto *occupant = new C_Human();
        auto *driver = new C_Human();
        if (n > 0)
            v.m_seats[0].occupant = occupant;
        car->m_driver = driver;
        car->m_lastAttacker = -1;
        ClearStreams();
        parts.Tick(70);
        Expect(tick, parts.fuse == 0 && parts.wrecked && car->m_wrecked && !g_streams.empty() &&
                         g_streams[0]->bits == T_Bits{{4, 4}}, __LINE__);
        Expect(tick, n == 0 || (occupant->hits.size() == 1 && !occupant->hits[0].attacker), __LINE__);
        if (n > 0)
            v.m_seats[0].occupant = nullptr;
        car->m_driver = nullptr;
        delete occupant;
        delete driver;
        car->m_wrecked = false;
        ClearStreams();
        parts.WreckOnce();
        Expect(tick, !car->m_wrecked && g_streams.empty() && parts.wrecked, __LINE__);
        car->m_wrecked = true;
        parts.fuse = 50;
        parts.Tick(10);
        Expect(tick, parts.fuse == 50, __LINE__);
        parts.wrecked = false;
        parts.fuse = 0;
        parts.Tick(10);
        Expect(tick, parts.fuse == 0 && !parts.wrecked, __LINE__);
        // Past the end at once: 0, wrecked (the car's Wreck once already: the parts' flag).
        parts.fuse = 5;
        parts.Tick(9);
        Expect(tick, parts.fuse == 0 && parts.wrecked, __LINE__);
        parts.wrecked = false;
    }

    // --- SaveGame: each explosive part from the last (its index, started's first byte, fuse,
    // effect), each zone from the last (its index, HP), then wrecked and the fuse.
    bool save = true;
    for (size_t k = 0; k < parts.explosives.size(); ++k) {
        S_CarExplosive *e = parts.explosives[k];
        e->started = k % 2 ? 0x301 : 0;
        e->fuse = 1000 + static_cast<int>(k);
        e->effect = k % 2 ? -1 : 40 + static_cast<int>(k);
    }
    for (size_t k = 0; k < parts.zones.size(); ++k)
        parts.zones[k].hp = 10 * static_cast<int>(k) + 1;
    parts.wrecked = true;
    parts.fuse = 333;
    g_files.clear();
    C_chunk out;
    out.Open("parts.sav", true);
    out.Begin(0x7777);
    const bool saved = parts.SaveGame(out);
    out.Ascend();
    out.Close();
    std::vector<uint8_t> body;                          // (what is expected)
    for (size_t k = parts.explosives.size(); k-- > 0;) {
        std::vector<uint8_t> c;
        Put(c, static_cast<int>(k));
        Put(c, static_cast<uint8_t>(k % 2 ? 1 : 0));
        Put(c, 1000 + static_cast<int>(k));
        Put(c, k % 2 ? -1 : 40 + static_cast<int>(k));
        const std::vector<uint8_t> bytes = ChunkBytes(10000, c);
        body.insert(body.end(), bytes.begin(), bytes.end());
    }
    for (size_t k = parts.zones.size(); k-- > 0;) {
        std::vector<uint8_t> c;
        Put(c, static_cast<int>(k));
        Put(c, 10 * static_cast<int>(k) + 1);
        const std::vector<uint8_t> bytes = ChunkBytes(0x2711, c);
        body.insert(body.end(), bytes.begin(), bytes.end());
    }
    {
        std::vector<uint8_t> c;
        Put(c, static_cast<uint8_t>(1));
        Put(c, 333);
        const std::vector<uint8_t> bytes = ChunkBytes(0x2712, c);
        body.insert(body.end(), bytes.begin(), bytes.end());
    }
    Expect(save, saved && g_files["parts.sav"] == ChunkBytes(0x7777, body), __LINE__);

    // --- LoadGame: back as saved (started: its first byte only).
    bool load = true;
    const auto scramble = [&] {
        for (S_CarExplosive *e : parts.explosives) {
            e->started = 0x200;
            e->fuse = e->effect = 7;
        }
        for (S_CarZone &z : parts.zones)
            z.hp = 9;
        parts.wrecked = false;
        parts.fuse = 0;
    };
    scramble();
    LoadParts(parts, body);
    {
        bool back = parts.wrecked && parts.fuse == 333;
        for (size_t k = 0; k < parts.explosives.size(); ++k) {
            const S_CarExplosive *e = parts.explosives[k];
            back &= e->started == (k % 2 ? 0x201 : 0x200) && e->fuse == 1000 + static_cast<int>(k) &&
                    e->effect == (k % 2 ? -1 : 40 + static_cast<int>(k));
        }
        for (size_t k = 0; k < parts.zones.size(); ++k)
            back &= parts.zones[k].hp == 10 * static_cast<int>(k) + 1;
        Expect(load, back, __LINE__);
    }

    // --- The quirks and fixes: an index out of range (the count itself among them) is passed over and
    // its chunk not left, as an unknown chunk is not: their contents are read next.
    bool quirks = true;
    {
        std::vector<uint8_t> state;
        Put(state, static_cast<uint8_t>(1));
        Put(state, 777);
        for (int bad : {static_cast<int>(parts.explosives.size()), -1, 1 << 20}) {
            scramble();
            // (After the index, a state chunk: read as the parts' own.)
            std::vector<uint8_t> nested;
            Put(nested, bad);
            const std::vector<uint8_t> inner = ChunkBytes(0x2712, state);
            nested.insert(nested.end(), inner.begin(), inner.end());
            LoadParts(parts, ChunkBytes(10000, nested));
            bool untouched = parts.wrecked && parts.fuse == 777;
            for (const S_CarExplosive *e : parts.explosives)
                untouched &= e->fuse == 7;
            Expect(quirks, untouched, __LINE__);
        }
        for (int bad : {static_cast<int>(parts.zones.size()), -1}) {
            scramble();
            std::vector<uint8_t> nested;
            Put(nested, bad);
            const std::vector<uint8_t> inner = ChunkBytes(0x2712, state);
            nested.insert(nested.end(), inner.begin(), inner.end());
            LoadParts(parts, ChunkBytes(0x2711, nested));
            bool untouched = parts.wrecked && parts.fuse == 777;
            for (const S_CarZone &z : parts.zones)
                untouched &= z.hp == 9;
            Expect(quirks, untouched, __LINE__);
        }
        scramble();
        LoadParts(parts, ChunkBytes(0x2713, ChunkBytes(0x2712, state)));
        Expect(quirks, parts.wrecked && parts.fuse == 777, __LINE__);
    }

    // --- Gather on a made model: the names' tests ("HOR" or "VERT" anywhere, "D_ZONE", "EXPL_D"; an
    // "EXPL" alone is nothing), from the last frame; no "particle" dummy: the default effects.
    bool names = true;
    {
        I3D_frame *made = NewFrame(FRAME_MODEL, "made", nullptr);
        const char *const madeNames[] = {"aVERTb", "xHORy", "D_ZONE7", "EXPLOSION", "EXPL_D2", "ZONE", "SEAT00"};
        const char *const madeProps[] = {"", "", "HP12,matid3", "EXPL4", "EXPL2,PARTID9", "HP1", ""};
        std::vector<I3D_frame *> &list = g_modelFrameLists[made];
        list.clear();
        const size_t from = g_frames.size();
        for (int k = 0; k < 7; ++k) {
            list.push_back(NewFrame(k == 6 ? FRAME_VISUAL : FRAME_DUMMY, std::string("made.") + madeNames[k], made));
            Stub(list.back()).props = madeProps[k];
        }
        FinishFrames(from, made);
        *reinterpret_cast<I3D_frame ***>(reinterpret_cast<uint8_t *>(made) + 0x120) = list.data();
        *reinterpret_cast<int *>(reinterpret_cast<uint8_t *>(made) + 0x128) = static_cast<int>(list.size());
        S_CarParts madeParts;
        madeParts.car = car;
        madeParts.model = static_cast<I3D_model *>(made);
        madeParts.Reset();
        Expect(names, madeParts.turretFrames == std::vector<I3D_frame *>{list[1], list[0]} && madeParts.zones.size() == 1 &&
                          madeParts.zones[0].frame == list[2] && madeParts.zones[0].hp == 12 &&
                          madeParts.zones[0].material == 3 && madeParts.explosives.size() == 1 &&
                          madeParts.explosives[0]->frame == list[4] && madeParts.explosives[0]->fuse == 2000 &&
                          madeParts.explosives[0]->effectId == 9 && !madeParts.particleFrame &&
                          madeParts.fireEffect == 0x60 && madeParts.smokeEffect == 0x5F && madeParts.wreckSound == -1 &&
                          madeParts.seatFrames.size() == static_cast<size_t>(n) && (n == 0 || madeParts.seatFrames[0] == list[6]),
               __LINE__);
        madeParts.Clear();
        madeParts.model = nullptr;
    }

    // --- No damage model (the load fails): the frame released, nothing gathered; Reset gathers
    // nothing (the Fix: no frame list to read).
    bool none = true;
    {
        S_CarParts other;
        g_cacheFailName = dmFile;
        g_createdModels.clear();
        other.Setup(car);
        g_cacheFailName.clear();
        Expect(none, !other.model && other.car == car && g_createdModels.size() == 1 && g_createdModels[0]->m_refCount == 0 &&
                         other.zones.empty() && other.explosives.empty() && other.seatFrames.empty(), __LINE__);
        other.particleFrame = car->GetFrame();
        other.Reset();
        Expect(none, other.zones.empty() && other.explosives.empty() && other.turretFrames.empty() &&
                         other.seatFrames.empty() && other.fuse == 0 && !other.particleFrame, __LINE__);
    }

    // --- Release: the model released, the explosive parts freed (their effects left), the turrets'
    // frames forgotten; the zones and seats kept.
    bool release = true;
    {
        const int refs = parts.model ? parts.model->m_refCount : 0;
        I3D_model *model = parts.model;
        for (S_CarExplosive *e : parts.explosives)
            e->effect = 900;
        const size_t zones = parts.zones.size(), seats = parts.seatFrames.size();
        g_particlesDestroyed.clear();
        parts.Release();
        Expect(release, !parts.model && (!model || model->m_refCount == refs - 1) && parts.explosives.empty() &&
                            parts.turretFrames.empty() && parts.zones.size() == zones && parts.seatFrames.size() == seats &&
                            g_particlesDestroyed.empty(), __LINE__);
    }

    std::printf("car parts: setup %d props %d reset %d tick %d save %d load %d quirks %d names %d none %d release %d\n",
                setup, props, reset, tick, save, load, quirks, names, none, release);
    std::printf("car parts model: dm %d zones %zu explosives %zu turrets %zu hitboxes %d fire %d smoke %d sound %d\n", has,
                zoneNames.size(), explosiveNames.size(), turrets.size(), hitBoxes, fire, smoke, sound);
    v.Stop();
    for (int ticks = 0; ticks < 20 && std::any_of(g_carEmitters.begin(), g_carEmitters.end(),
                                                  [](const S_CarEmitter &e) { return e.active; });
         ++ticks)
        UpdateCarEffectEmitters(83);
    delete car;
    g_made.clear();
    g_images.clear();
    g_meshes = g_meshesReleased = g_materialsMade = g_materialsReleased = 0;
}

// --- the damage parts' hits and effects (src/game/car_parts.cpp; part 2g-ii) ---------------------------

void PartsHitTest(const char *path)
{
    if (!LoadModel(path))
        return;
    InitPerfTime();
    auto *car = new C_Car();
    car->SetFrame(g_model);
    C_Vehicle &v = car->m_vehicle;
    v.Start();
    g_gameRole = GAME_ROLE_LOCAL;
    GetGame()->m_scene = g_carScene;
    g_sceneVtable[0x104 / 4] = reinterpret_cast<void *>(&F_IntersectFrame);
    S_CarParts &parts = car->m_parts;
    const int n = car->GetSeatCount();
    I3D_frame *carFrame = car->GetFrame();
    // The materials: material m absorbs 100 + m.
    static S_MaterialDef materials[64];
    for (int m = 0; m < 64; ++m)
        materials[m].absorption = static_cast<uint32_t>(100 + m);
    g_materials = materials;
    g_materialCount = 64;
    const auto absorbs = [&](int m) { return m >= 0 && m < 64 ? 100.0f + static_cast<float>(m) : 0.0f; };
    // The crew: an occupant in every seat; who has a box.
    std::vector<C_Human *> crew;
    for (int i = 0; i < n; ++i) {
        crew.push_back(new C_Human());
        crew.back()->SetNetId(50 + i);
    }
    const auto seat = [&](bool on) {
        for (int i = 0; i < n; ++i) {
            v.m_seats[i].occupant = on ? crew[i] : nullptr;
            crew[i]->hits.clear();
        }
    };
    int box = -1;                                       // a seat with a box
    for (int i = 0; i < n && box < 0 && i < static_cast<int>(parts.seatFrames.size()); ++i)
        if (parts.seatFrames[i])
            box = i;
    auto *shooter = new C_Human();
    shooter->SetNetId(61);
    shooter->role = HUMAN_ROLE_PLAYER;                  // (the car's Hit ignores an enemy's)
    const S_vector pos(0.25f, 1.5f, -0.75f), dir(0.0f, -0.6f, 0.8f);
    const S_vector from = pos - dir;
    // The rays: at the damage model they meet `modelHit`; at another frame, that frame when it is in
    // `hits`; the watched frames' on-states logged at each.
    const auto rays = [&](I3D_frame *modelHit, std::set<const I3D_frame *> hits) {
        g_rayPick = true;
        g_rayModel = parts.model;
        g_rayModelHit = modelHit;
        g_rayHits = std::move(hits);
        g_intersects.clear();
        g_rayWatch.clear();
        for (const S_CarExplosive *e : parts.explosives)
            e->frame->SetOn(true);
        for (size_t s = 0; s < parts.seatFrames.size(); ++s)
            if (parts.seatFrames[s])
                parts.seatFrames[s]->SetOn(!(static_cast<int>(s) < n && v.m_seats[s].occupant != nullptr));
        for (const S_CarExplosive *e : parts.explosives)
            g_rayWatch.push_back(e->frame);
        for (I3D_frame *f : parts.seatFrames)
            if (f)
                g_rayWatch.push_back(f);
    };
    // At a ray: the explosive parts hidden, the seats' boxes shown where someone sits.
    const auto prepared = [&](const S_Intersect &r) {
        bool ok = true;
        size_t k = 0;
        for (size_t e = 0; e < parts.explosives.size(); ++e, ++k)
            ok &= !r.watched[k];
        for (size_t s = 0; s < parts.seatFrames.size(); ++s)
            if (parts.seatFrames[s])
                ok &= r.watched[k++] == (static_cast<int>(s) < n && v.m_seats[s].occupant != nullptr);
        return ok;
    };

    if (!parts.model) {
        // No damage model: hits and materials do nothing; the wreck's effects at the car's frame.
        bool none = true;
        rays(nullptr, {});
        g_posRots.clear();
        parts.Hit(HIT_BULLET, 50.0f, pos, dir, shooter);
        Expect(none, g_intersects.empty() && g_posRots.empty() && parts.GetHitMaterial(pos, dir) == -1 && g_intersects.empty(),
               __LINE__);
        g_particlesMade.clear();
        parts.StartWreckEffects();
        Expect(none, g_particlesMade.size() == 2 && g_particlesMade[0].parent == carFrame, __LINE__);
        parts.StartPartEffect(0);
        Expect(none, g_particlesMade.size() == 2, __LINE__);
        std::printf("car parts hits: place 1 material 1 hit 1 zone 1 explosive 1 seats 1 effects 1 wreck 1 car %d\n", none);
    } else {
        // --- Place: the model at the car's world place and turn in the primary sector; each turret frame
        // turned as the car's namesake, updated; the model updated.
        bool place = true;
        {
            for (I3D_frame *t : parts.turretFrames) {
                if (I3D_frame *own = carFrame->FindChildFrame(t->GetShortName(), FRAME_MASK_ANY)) {
                    S_quat r;
                    r.Make(S_vector(0.0f, 1.0f, 0.0f), 0.4f);
                    own->SetRot(r);
                }
                t->m_rot.Identity();
            }
            I3D_frame *holder = NewFrame(FRAME_DUMMY, "holder", nullptr);
            holder->m_localMatrix(3) = S_vector(30.0f, 0.0f, 0.0f);
            I3D_frame *const parent0 = carFrame->GetParent();
            Unlink(carFrame);
            carFrame->LinkTo(holder, 0);
            g_posRots.clear();
            g_countUpdates = true;
            g_frameUpdates.clear();
            parts.Place();
            g_countUpdates = false;
            S_quat want;
            want.Make(carFrame->GetMatrix());
            Expect(place, g_posRots.size() == 1 && !SameV(g_posRots[0].pos, carFrame->GetPos(), 0.5f), __LINE__);
            Unlink(carFrame);
            if (parent0)
                carFrame->LinkTo(parent0, 0);
            g_posRots.clear();
            g_countUpdates = true;
            g_frameUpdates.clear();
            parts.Place();
            g_countUpdates = false;
            want.Make(carFrame->GetMatrix());
            Expect(place, g_posRots.size() == 1 && g_posRots[0].frame == parts.model &&
                              SameV(g_posRots[0].pos, carFrame->GetWorldPos(), 0.0f) && g_posRots[0].rot.s == want.s &&
                              SameV(g_posRots[0].rot.v, want.v, 0.0f) &&
                              parts.model->GetParent() == GetGame()->GetScene()->GetPrimarySector() &&
                              g_frameUpdates[parts.model] == 1, __LINE__);
            for (I3D_frame *t : parts.turretFrames) {
                I3D_frame *own = carFrame->FindChildFrame(t->GetShortName(), FRAME_MASK_ANY);
                S_quat r = own ? own->GetRot() : S_quat(1.0f, 0.0f, 0.0f, 0.0f);
                r.Normalize();
                Expect(place, !own || (t->m_rot.s == r.s && SameV(t->m_rot.v, r.v, 0.0f) && g_frameUpdates[t] == 1 &&
                                       (t->m_frameFlags & FRMFLAGS_ROT_DIRTY)), __LINE__);
            }
            // A turret frame without a namesake on the car: left (the Fix), the others turned.
            I3D_frame *stray = NewFrame(FRAME_DUMMY, "dm.HORTstray", nullptr);
            FinishFrames(g_frames.size() - 1, parts.model);
            S_quat odd;
            odd.Make(S_vector(1.0f, 0.0f, 0.0f), 0.3f);
            stray->m_rot = odd;
            parts.turretFrames.insert(parts.turretFrames.begin(), stray);
            g_countUpdates = true;
            g_frameUpdates.clear();
            parts.Place();
            g_countUpdates = false;
            Expect(place, stray->m_rot.s == odd.s && g_frameUpdates[stray] == 0 && g_frameUpdates[parts.model] == 1, __LINE__);
            parts.turretFrames.erase(parts.turretFrames.begin());
        }

        // --- GetHitMaterial: the ray from pos - dir along dir at the shown model (the explosive parts
        // hidden, the seats' boxes shown where someone sits), the model hidden after; the hit frame's
        // matid, a seat's box flesh (0x42), none -1.
        bool material = true;
        seat(true);
        if (n > 1)
            v.m_seats[n - 1].occupant = nullptr;
        if (!parts.zones.empty()) {
            const S_CarZone &z = parts.zones[0];
            rays(z.frame, {});
            g_posRots.clear();
            const int got = car->GetHitMaterial(pos, dir);
            Expect(material, g_posRots.size() == 1 && got == z.material && g_intersects.size() == 1 && g_intersects[0].frame == parts.model &&
                                 SameV(g_intersects[0].from, from, 0.0f) && SameV(g_intersects[0].dir, dir, 0.0f) &&
                                 g_intersects[0].flags == I3DCOL_VISUAL_GEOMETRY && g_intersects[0].frameOn &&
                                 prepared(g_intersects[0]) && !parts.model->IsOn(), __LINE__);
        }
        if (box >= 0) {
            rays(parts.seatFrames[box], {});
            Expect(material, parts.GetHitMaterial(pos, dir) == 0x42, __LINE__);
        }
        rays(nullptr, {});
        Expect(material, parts.GetHitMaterial(pos, dir) == -1 && g_intersects.size() == 1, __LINE__);

        // --- Hit: only bullets, projectiles, explosions and script kills, not once wrecked; the ray
        // from pos - 2 dir; a zone's HP less what its material does not absorb.
        bool hit = true;
        seat(true);
        for (int type : {HIT_FALL, HIT_MELEE, HIT_THROWN_OBJECT, HIT_ENVIRONMENT, HIT_BACKSTAB, HIT_RUN_OVER}) {
            rays(nullptr, {});
            g_posRots.clear();
            parts.Hit(type, 500.0f, pos, dir, shooter);
            Expect(hit, g_intersects.empty() && g_posRots.empty(), __LINE__);
        }
        parts.wrecked = true;
        rays(nullptr, {});
        g_posRots.clear();
        parts.Hit(HIT_BULLET, 500.0f, pos, dir, shooter);
        Expect(hit, g_intersects.empty() && g_posRots.empty(), __LINE__);
        parts.wrecked = false;
        bool zoneOk = true;
        if (!parts.zones.empty()) {
            const size_t k = parts.zones.size() - 1;
            S_CarZone &z = parts.zones[k];
            for (int type : {HIT_BULLET, HIT_PROJECTILE, HIT_SCRIPT_KILL}) {
                z.hp = 1000;
                rays(z.frame, {});
                g_posRots.clear();
                parts.Hit(type, absorbs(z.material) + 300.0f, pos, dir, shooter);
                const S_vector twice = from - dir;
                Expect(zoneOk, z.hp == 700 && g_intersects.size() == 1 && g_intersects[0].frame == parts.model &&
                                   SameV(g_intersects[0].from, twice, 0.0f) && SameV(g_intersects[0].dir, dir, 0.0f) &&
                                   g_intersects[0].frameOn && prepared(g_intersects[0]) && !parts.model->IsOn() &&
                                   g_posRots.size() == 1 && !parts.wrecked, __LINE__);
            }
            // Nothing through the material: nothing (below it too; a negative HP stays).
            for (float less : {0.0f, 60.0f}) {
                z.hp = 1000;
                rays(z.frame, {});
                parts.Hit(HIT_BULLET, absorbs(z.material) - less, pos, dir, shooter);
                Expect(zoneOk, z.hp == 1000, __LINE__);
            }
            z.hp = -5;
            rays(z.frame, {});
            parts.Hit(HIT_PROJECTILE, absorbs(z.material), pos, dir, shooter);
            Expect(zoneOk, z.hp == -5 && !parts.wrecked && !car->m_wrecked, __LINE__);
            // A material past the table's count absorbs nothing (the Fix).
            {
                const int material = z.material;
                g_materialCount = 16;
                z.material = 20;
                z.hp = 1000;
                rays(z.frame, {});
                parts.Hit(HIT_BULLET, 30.0f, pos, dir, shooter);
                Expect(zoneOk, z.hp == 970, __LINE__);
                g_materialCount = 64;
                z.material = material;
            }
            // Down to 0 exactly: not broken.
            z.hp = 50;
            rays(z.frame, {});
            parts.Hit(HIT_PROJECTILE, absorbs(z.material) + 50.0f, pos, dir, shooter);
            Expect(zoneOk, z.hp == 0 && !parts.wrecked && !car->m_wrecked, __LINE__);
            // Broken by a projectile: the rest to the boxes on the ray (at pos - dir), the car wrecked
            // (its Wreck kills every occupant after).
            z.hp = 10;
            std::set<const I3D_frame *> boxes;
            for (I3D_frame *f : parts.seatFrames)
                if (f)
                    boxes.insert(f);
            rays(z.frame, boxes);
            ClearStreams();
            parts.Hit(HIT_PROJECTILE, absorbs(z.material) + 50.0f, pos, dir, shooter);
            bool crewHit = true;
            for (int i = 0; i < n; ++i) {
                const bool hasBox = i < static_cast<int>(parts.seatFrames.size()) && parts.seatFrames[i];
                crewHit &= crew[i]->hits.size() == (hasBox ? 2u : 1u) && crew[i]->hits.back().type == HIT_THROWN_OBJECT;
                if (hasBox && crewHit)
                    crewHit &= crew[i]->hits[0].type == HIT_BULLET && crew[i]->hits[0].amount == 50.0f &&
                               SameV(crew[i]->hits[0].pos, from, 0.0f) && SameV(crew[i]->hits[0].dir, dir, 0.0f) &&
                               crew[i]->hits[0].attacker == shooter && crew[i]->hits[0].flags == 0;
            }
            Expect(zoneOk, z.hp == 0 && parts.wrecked && car->m_wrecked && crewHit, __LINE__);
            car->m_wrecked = false;
            parts.wrecked = false;
            seat(true);
            // Broken by a bullet: the explosive part on the ray starts its fuse (the earliest) and
            // effect; started, it does no more; one without a fuse wrecks the car; none: no more.
            if (!parts.explosives.empty()) {
                const int j = static_cast<int>(parts.explosives.size()) - 1;
                S_CarExplosive *e = parts.explosives[j];
                e->started = 0;
                e->fuse = 4000;
                for (int earlier : {0, 9000, 3000}) {
                    e->started = 0;
                    parts.fuse = earlier;
                    z.hp = 10;
                    std::set<const I3D_frame *> hits = {e->frame};
                    rays(z.frame, hits);
                    ClearStreams();
                    parts.Hit(HIT_BULLET, absorbs(z.material) + 50.0f, pos, dir, shooter);
                    Expect(zoneOk, e->started == 1 && parts.fuse == (earlier == 3000 ? 3000 : 4000) && !parts.wrecked &&
                                       !g_streams.empty() &&
                                       g_streams.back()->bits == T_Bits{{5, 4}, {static_cast<uint32_t>(j), 4}} &&
                                       parts.model->IsOn(), __LINE__);
                    parts.model->SetOn(false);
                }
                z.hp = 10;
                parts.fuse = 7;
                rays(z.frame, {e->frame});
                ClearStreams();
                parts.Hit(HIT_BULLET, absorbs(z.material) + 50.0f, pos, dir, shooter);
                Expect(zoneOk, parts.fuse == 7 && g_streams.empty() && !parts.wrecked, __LINE__);
                e->started = 0;
                e->fuse = 0;
                z.hp = 10;
                rays(z.frame, {e->frame});
                parts.Hit(HIT_BULLET, absorbs(z.material) + 50.0f, pos, dir, shooter);
                Expect(zoneOk, parts.wrecked && car->m_wrecked, __LINE__);
                car->m_wrecked = false;
                parts.wrecked = false;
                parts.model->SetOn(false);
            }
            z.hp = 10;
            for (S_CarExplosive *e : parts.explosives)
                e->started = 0, e->fuse = 1000;
            rays(z.frame, {});
            ClearStreams();
            parts.Hit(HIT_BULLET, absorbs(z.material) + 50.0f, pos, dir, shooter);
            bool unstarted = true;
            for (const S_CarExplosive *e : parts.explosives)
                unstarted &= !e->started;
            Expect(zoneOk, z.hp == 0 && !parts.wrecked && !car->m_wrecked && unstarted && g_streams.empty(), __LINE__);
            // An explosion: the zone nearest (a tie: the earlier), no ray.
            if (std::getenv("CAR_TRACE"))
                for (const S_CarZone &each : parts.zones) {
                    const S_vector w = each.frame->GetWorldPos();
                    std::printf("  zone %s at %g %g %g\n", each.frame->GetShortName(), w.x, w.y, w.z);
                }
            for (size_t target = 0; target < parts.zones.size(); ++target) {
                const S_vector at = parts.zones[target].frame->GetWorldPos() + S_vector(0.01f, 0.0f, 0.0f);
                size_t nearest = 0;
                float best = 1e30f;
                for (size_t i = 0; i < parts.zones.size(); ++i) {
                    const S_vector d = parts.zones[i].frame->GetWorldPos() - at;
                    const float sq = (d.z * d.z + d.y * d.y) + d.x * d.x;
                    if (sq < best) {
                        best = sq;
                        nearest = i;
                    }
                }
                for (S_CarZone &each : parts.zones)
                    each.hp = 1000;
                rays(nullptr, {});
                parts.Hit(HIT_EXPLOSION, absorbs(parts.zones[nearest].material) + 25.0f, at, dir, shooter);
                bool only = g_intersects.empty();
                for (size_t i = 0; i < parts.zones.size(); ++i)
                    only &= parts.zones[i].hp == (i == nearest ? 975 : 1000);
                Expect(zoneOk, only, __LINE__);
            }
        }
        // Nothing met: nothing more (no boxes tested).
        {
            seat(true);
            std::set<const I3D_frame *> boxes;
            for (I3D_frame *f : parts.seatFrames)
                if (f)
                    boxes.insert(f);
            rays(nullptr, boxes);
            parts.Hit(HIT_BULLET, 40.0f, pos, dir, shooter);
            bool untouched = g_intersects.size() == 1;
            for (int i = 0; i < n; ++i)
                untouched &= crew[i]->hits.empty();
            Expect(zoneOk, untouched, __LINE__);
        }
        // A seat's box: its occupant takes the hit (at pos); without one, nothing. Another frame: the
        // boxes on the ray.
        bool seatsOk = true;
        if (box >= 0) {
            seat(true);
            rays(parts.seatFrames[box], {});
            parts.Hit(HIT_PROJECTILE, 77.0f, pos, dir, shooter);
            Expect(seatsOk, crew[box]->hits.size() == 1 && crew[box]->hits[0].type == HIT_BULLET &&
                                crew[box]->hits[0].amount == 77.0f && SameV(crew[box]->hits[0].pos, pos, 0.0f) &&
                                SameV(crew[box]->hits[0].dir, dir, 0.0f) && crew[box]->hits[0].attacker == shooter, __LINE__);
            v.m_seats[box].occupant = nullptr;
            crew[box]->hits.clear();
            rays(parts.seatFrames[box], {parts.seatFrames[box]});
            parts.Hit(HIT_PROJECTILE, 77.0f, pos, dir, shooter);
            Expect(seatsOk, crew[box]->hits.empty() && g_intersects.size() == 1, __LINE__);
            seat(true);
            rays(carFrame, {parts.seatFrames[box]});
            parts.Hit(HIT_BULLET, 33.0f, pos, dir, shooter);
            Expect(seatsOk, crew[box]->hits.size() == 1 && crew[box]->hits[0].amount == 33.0f &&
                                SameV(crew[box]->hits[0].pos, from, 0.0f), __LINE__);
            // The car's Hit hands its own on: the type, amount, place, direction and attacker.
            seat(true);
            shooter->role = HUMAN_ROLE_PLAYER;
            rays(parts.seatFrames[box], {});
            car->Hit(HIT_PROJECTILE, 88.0f, pos, dir, shooter, 0);
            Expect(seatsOk, crew[box]->hits.size() == 1 && crew[box]->hits[0].amount == 88.0f &&
                                SameV(crew[box]->hits[0].pos, pos, 0.0f) && SameV(crew[box]->hits[0].dir, dir, 0.0f) &&
                                crew[box]->hits[0].attacker == shooter, __LINE__);
            seat(true);
            rays(parts.seatFrames[box], {});
            car->Hit(HIT_MELEE, 88.0f, pos, dir, shooter, 0);
            Expect(seatsOk, crew[box]->hits.empty(), __LINE__);
        }
        if (!parts.zones.empty()) {
            const size_t k = parts.zones.size() - 1;
            for (S_CarZone &each : parts.zones)
                each.hp = 1000;
            const S_vector at = parts.zones[k].frame->GetWorldPos();
            size_t nearest = 0;
            float best = 1e30f;
            for (size_t i = 0; i < parts.zones.size(); ++i) {
                const S_vector d = parts.zones[i].frame->GetWorldPos() - at;
                const float sq = (d.z * d.z + d.y * d.y) + d.x * d.x;
                if (sq < best) {
                    best = sq;
                    nearest = i;
                }
            }
            rays(nullptr, {});
            car->Hit(HIT_EXPLOSION, absorbs(parts.zones[nearest].material) + 40.0f, at, dir, shooter, 0);
            Expect(seatsOk, parts.zones[nearest].hp == 960, __LINE__);
        }

        // --- ExplosiveAt: from the last part; on a hit the model is left shown; none: -1, hidden.
        bool explosive = true;
        if (!parts.explosives.empty()) {
            std::set<const I3D_frame *> all;
            for (const S_CarExplosive *e : parts.explosives)
                all.insert(e->frame);
            parts.model->SetOn(false);
            rays(nullptr, all);
            Expect(explosive, parts.ExplosiveAt(from, dir) == static_cast<int>(parts.explosives.size()) - 1 &&
                                  parts.model->IsOn() && g_intersects.size() == 1 &&
                                  g_intersects[0].frame == parts.explosives.back()->frame && g_intersects[0].frameOn &&
                                  g_intersects[0].modelOn &&
                                  SameV(g_intersects[0].from, from, 0.0f), __LINE__);
            rays(nullptr, {parts.explosives[0]->frame});
            Expect(explosive, parts.ExplosiveAt(from, dir) == 0 && g_intersects.size() == parts.explosives.size(), __LINE__);
            rays(nullptr, {});
            Expect(explosive, parts.ExplosiveAt(from, dir) == -1 && !parts.model->IsOn(), __LINE__);
        }

        // --- HitSeats: each occupant whose box the ray meets (from the last), the model hidden after.
        bool seats = true;
        {
            seat(true);
            int empty = -1, missed = -1;                // the last box's seat empty, the first other off the ray
            for (int i = 0; i < n && i < static_cast<int>(parts.seatFrames.size()); ++i)
                if (parts.seatFrames[i])
                    empty = i;
            for (int i = 0; i < n && i < static_cast<int>(parts.seatFrames.size()); ++i)
                if (parts.seatFrames[i] && i != empty && missed < 0)
                    missed = i;
            std::set<const I3D_frame *> boxes;
            for (int i = 0; i < static_cast<int>(parts.seatFrames.size()); ++i)
                if (parts.seatFrames[i] && i != missed)
                    boxes.insert(parts.seatFrames[i]);
            if (empty >= 0)
                v.m_seats[empty].occupant = nullptr;
            rays(nullptr, boxes);
            for (I3D_frame *f : parts.seatFrames)
                if (f)
                    f->SetOn(false);
            parts.model->SetOn(false);
            parts.HitSeats(from, dir, 12.0f, shooter);
            int tested = 0;
            for (int i = 0; i < n; ++i) {
                const bool hasBox = i < static_cast<int>(parts.seatFrames.size()) && parts.seatFrames[i];
                const bool hitOne = hasBox && i != empty && i != missed;
                tested += hasBox && i != empty;
                seats &= crew[i]->hits.size() == (hitOne ? 1u : 0u);
                if (hitOne)
                    seats &= crew[i]->hits[0].type == HIT_BULLET && crew[i]->hits[0].amount == 12.0f &&
                             SameV(crew[i]->hits[0].pos, from, 0.0f) && crew[i]->hits[0].attacker == shooter;
            }
            seats &= static_cast<int>(g_intersects.size()) == tested && !parts.model->IsOn();
            for (size_t r = 0; r < g_intersects.size(); ++r)
                seats &= g_intersects[r].frameOn && g_intersects[r].modelOn;
            Expect(seats, seats, __LINE__);
        }

        // --- The effects: a part's (its PARTID at its frame, the car's frame the parent; Fix: past the
        // list nothing), the wreck's (the parts' effects destroyed, fire and smoke at the particle
        // dummy, the sound).
        bool effects = true;
        {
            ClearStreams();
            parts.SendPartEffect(9);
            Expect(effects, g_streams.size() == 1 && g_streams[0]->bits == T_Bits{{5, 4}, {9, 4}}, __LINE__);
            g_particlesMade.clear();
            parts.StartPartEffect(-1);
            parts.StartPartEffect(static_cast<int>(parts.explosives.size()));
            Expect(effects, g_particlesMade.empty(), __LINE__);
            for (size_t j = 0; j < parts.explosives.size(); ++j) {
                S_CarExplosive *e = parts.explosives[j];
                const int id = e->effectId;
                g_particlesMade.clear();
                g_posRots.clear();
                e->effect = -1;
                parts.StartPartEffect(static_cast<int>(j));
                if (id == -1) {
                    Expect(effects, g_particlesMade.empty() && e->effect == -1 && g_posRots.empty(), __LINE__);
                } else {
                    Expect(effects, g_particlesMade.size() == 1 && g_particlesMade[0].effect == id &&
                                        std::memcmp(&g_particlesMade[0].matrix, &e->frame->GetMatrix(), sizeof(S_matrix)) == 0 &&
                                        g_particlesMade[0].parent == carFrame && g_particlesMade[0].faceCamera &&
                                        !g_particlesMade[0].transient && e->effect == g_particlesMade[0].handle &&
                                        g_posRots.size() == 1, __LINE__);
                }
            }
        }
        bool wreck = true;
        for (int sound : {parts.wreckSound, 0, 1}) {
            for (size_t j = 0; j < parts.explosives.size(); ++j)
                parts.explosives[j]->effect = j % 2 ? -1 : 0;
            const int saved = parts.wreckSound;
            parts.wreckSound = sound;
            g_particlesMade.clear();
            g_particlesDestroyed.clear();
            g_soundRequests.clear();
            g_posRots.clear();
            parts.StartWreckEffects();
            I3D_frame *at = parts.particleFrame ? parts.particleFrame : carFrame;
            Expect(wreck, g_posRots.size() == 1, __LINE__);
            std::vector<int> destroyed;
            for (size_t j = parts.explosives.size(); j-- > 0;)
                if (j % 2 == 0)
                    destroyed.push_back(0);
            bool cleared = g_particlesDestroyed == destroyed;
            for (const S_CarExplosive *e : parts.explosives)
                cleared &= e->effect == -1;
            Expect(wreck, cleared && g_particlesMade.size() == 2 && g_particlesMade[0].effect == parts.fireEffect &&
                              g_particlesMade[1].effect == parts.smokeEffect &&
                              std::memcmp(&g_particlesMade[0].matrix, &at->GetMatrix(), sizeof(S_matrix)) == 0 &&
                              std::memcmp(&g_particlesMade[1].matrix, &at->GetMatrix(), sizeof(S_matrix)) == 0 &&
                              g_particlesMade[0].parent == carFrame && g_particlesMade[1].parent == carFrame &&
                              g_particlesMade[0].faceCamera && !g_particlesMade[0].transient &&
                              parts.fireHandle == g_particlesMade[0].handle && parts.smokeHandle == g_particlesMade[1].handle,
                   __LINE__);
            if (sound >= 1)
                Expect(wreck, g_soundRequests.size() == 1 && g_soundRequests[0].flags == SOUND_SILENT_TO_AI &&
                                  g_soundRequests[0].attachTo == at && g_soundRequests[0].chapter == 6 &&
                                  g_soundRequests[0].group == sound && !g_soundRequests[0].source &&
                                  SameV(g_soundRequests[0].pos, S_vector(0.0f, 0.0f, 0.0f), 0.0f), __LINE__);
            else
                Expect(wreck, g_soundRequests.empty(), __LINE__);
            parts.wreckSound = saved;
        }
        // Without the particle dummy: at the car's frame.
        I3D_frame *particle = parts.particleFrame;
        parts.particleFrame = nullptr;
        g_particlesMade.clear();
        g_soundRequests.clear();
        parts.StartWreckEffects();
        Expect(wreck, g_particlesMade.size() == 2 &&
                          std::memcmp(&g_particlesMade[0].matrix, &carFrame->GetMatrix(), sizeof(S_matrix)) == 0 &&
                          (parts.wreckSound < 1 || (g_soundRequests.size() == 1 && g_soundRequests[0].attachTo == carFrame)),
               __LINE__);
        parts.particleFrame = particle;

        std::printf("car parts hits: place %d material %d hit %d zone %d explosive %d seats %d effects %d wreck %d car %d\n",
                    place, material, hit, zoneOk, explosive, seats, effects, wreck, seatsOk);
    }
    g_rayPick = false;
    g_rayWatch.clear();
    seat(false);
    for (C_Human *h : crew)
        delete h;
    delete shooter;
    g_materials = nullptr;
    g_materialCount = 0;
    GetGame()->m_scene = nullptr;
    v.Stop();
    for (int ticks = 0; ticks < 20 && std::any_of(g_carEmitters.begin(), g_carEmitters.end(),
                                                  [](const S_CarEmitter &e) { return e.active; });
         ++ticks)
        UpdateCarEffectEmitters(83);
    delete car;
    g_made.clear();
    g_images.clear();
    g_meshes = g_meshesReleased = g_materialsMade = g_materialsReleased = 0;
}

#ifdef REAL_SEAT_WEAPON
#include "seat_weapon_test.inc"
#endif
#ifdef ROUTE_TEST
#include "route_test.inc"
#endif
#if defined(RIGID_TEST) || defined(RIGID_STEP_TEST)
#include "rigid_body_test.inc"
#endif
#ifdef RIGID_STEP_TEST
#include "rigid_step_test.inc"
#endif
#ifdef ITEM_TEST
#include "item_test.inc"
#include "animation_test.inc"
#include "simple_actors_test.inc"
#include "objective_actors_test.inc"
#include "world_actors_test.inc"
#include "moving_actors_test.inc"
#include "seat_actors_test.inc"
#include "prop_actors_test.inc"
#include "air_actors_test.inc"
#endif

int main(int argc, char **argv)
{
    InitVtables();
    g_carScene = MakeScene();
#ifdef REAL_SEAT_WEAPON
    for (int i = 1; i < argc; ++i)
        SeatWeaponTest(argv[i]);
    return 0;
#endif
#ifdef ROUTE_TEST
    for (int i = 1; i < argc; ++i)
        RouteTest(argv[i]);
    return 0;
#endif
#ifdef RIGID_TEST
    for (int i = 1; i < argc; ++i)
        RigidBodyTest(argv[i]);
    return 0;
#endif
#ifdef ITEM_TEST
    for (int i = 1; i < argc; ++i) {
        ItemTest(argv[i]);
        AnimationTest();
        SimpleActorsTest();
        ObjectiveActorsTest();
        WorldActorsTest();
        MovingActorsTest();
        SeatActorsTest();
        PropActorsTest();
        AirActorsTest();
    }
    return 0;
#endif
#ifdef RIGID_STEP_TEST
    for (int i = 1; i < argc; ++i)
        RigidStepTest(argv[i]);
    return 0;
#endif
    // tables\tyres.bin for the drive test: an unused type, then the default block's type 0 (the
    // last type with an id wins: index 1, so the wheels look up its entry for the flat ground's
    // material 1, made from the first wheel's own values after Setup).
    g_tyreTypes.resize(2);
    g_tyreTypes[0].id = 7;
    g_tyreTypes[0].surfaces = nullptr;
    g_tyreTypes[1].id = 0;
    g_tyreTypes[1].surfaces = &g_tyreSurfaces;
    // tables\effects_car.def (EFFECTS_CAR: another path) and the pool.
    const char *effectsFile = std::getenv("EFFECTS_CAR") ? std::getenv("EFFECTS_CAR") : "H:/HD2_Unpacked/tables/effects_car.def";
    if (FILE *f = std::fopen(effectsFile, "rb")) {
        std::vector<S_CarEffectDef> defs(64);
        g_carEffectCount = static_cast<uint32_t>(std::fread(defs.data(), sizeof(S_CarEffectDef), defs.size(), f));
        std::fclose(f);
        g_carEffects = new S_CarEffectDef[g_carEffectCount];
        std::copy(defs.begin(), defs.begin() + g_carEffectCount, g_carEffects);
    }
    std::printf("effects init: %d\n", InitCarEffects());
    PoolTest();
    g_images.clear();                                   // (the pool's, not a model's)
    for (int i = 1; i < argc; ++i) {
        Report(argv[i]);
        CarTest(argv[i]);
        SeatTest(argv[i]);
        CarUpdateTest(argv[i]);
        CallbacksTest(argv[i]);
        CarHitsTest(argv[i]);
        MissionTest(argv[i]);
        NetTest(argv[i]);
        SaveTest(argv[i]);
        PartsTest(argv[i]);
        PartsHitTest(argv[i]);
    }
    return 0;
}
