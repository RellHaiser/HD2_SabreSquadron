// Collision part 2, function level: the port's sphere sweeps against one shape (collision_sweep.cpp, included here
// for its file-local functions) against the original's, called in the original executable (original_exe.h) with
// the same sweep: SweepSphereAabb (0x00706cd0; also the boxes, in their space), SweepSphereEdge (0x007045a0),
// SweepSphereTriangle (0x00705130), SweepSphereCapsule (0x0070ffc0), and every static primitive kind as the files
// store them through SweepSpherePrimitive (0x00710580: boxes, axis-aligned boxes, spheres, rectangles, poles,
// capsules; not the convex hulls, which need visuals), on random shapes and sweeps. The same result
// (hit or not, the squared distance, contact and normal) within the x87's 24-bit precision against the port's
// floats. Without the executable: nothing to compare (says so, passes).
//
// Modes for collision_test.exe's dumps (development aids): "tri FILE" (TRI_REPLAY: triangle sweeps where my exact
// sweep differs) and "capsule FILE" (CAPSULE_REPLAY) run the port's and the original's on each line.
// MinGW; sweep_diff.bat; "sweep diff: N checks, 0 failures".
#include <windows.h>

#include "game/collision_sweep.cpp"

#include <cfloat>
#include <cstdio>
#include <random>

#include "game/game.h"
#include "game/human.h"
#include "ls3d/rw_data.h"
#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

// (Link stubs: nothing here loads a level.)
int RW_DATA_CALL dtaOpen(const char *, bool) { return 0; }
bool RW_DATA_CALL dtaClose(int) { return false; }
int RW_DATA_CALL dtaSeek(int, int, int) { return 0; }
int RW_DATA_CALL dtaRead(int, void *, int) { return 0; }
C_game *GetGame() { return nullptr; }
extern "C" I3D_driver *I3DGetDriver() { return nullptr; }
void I3D_frame::UpdateWMatrixProc() {}
uint32_t C_Human::GetStanceFlags() const { return 0; }

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            if (failures <= 40)                                                     \
                std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);         \
        }                                                                           \
    } while (0)

// The original's sweep, by the offsets in S_SphereSweep's comments: its vectors are padded to 16 bytes (the port's
// S_vector is 12), the bounds packed.
uint8_t g_orig[0xC0];
uint8_t g_scratch[4096];                                // (the functions' ECX: not read)
template <class T> T &At(uint32_t offset) { return *reinterpret_cast<T *>(g_orig + offset); }
void PutV(uint32_t offset, const S_vector &v)
{
    At<S_vector>(offset) = v;
    At<float>(offset + 12) = 0;
}
void ToOrig(const S_SphereSweep &s)
{
    std::memset(g_orig, 0, sizeof g_orig);
    PutV(0x00, s.from);
    PutV(0x10, s.dir);
    At<float>(0x20) = s.radius;
    At<S_vector>(0x24) = s.boxMin;
    At<S_vector>(0x30) = s.boxMax;
    PutV(0x40, s.to);
    PutV(0x50, s.normDir);
    At<uint32_t>(0x60) = s.excludeFlags;
    At<uint32_t>(0x64) = s.excludeTypes;
    At<float>(0x68) = s.length;
    At<float>(0x6C) = s.best;
    PutV(0x70, s.side);
    At<float>(0x80) = s.slabMin;
    At<float>(0x84) = s.slabMax;
    PutV(0x90, s.hitPos);
    PutV(0xA0, s.hitNormal);
    At<void *>(0xB0) = s.hit;
}
void FromOrig(S_SphereSweep &s)
{
    s.best = At<float>(0x6C);
    s.hitPos = At<S_vector>(0x90);
    s.hitNormal = At<S_vector>(0xA0);
    s.hit = At<void *>(0xB0);
}
typedef bool(__attribute__((thiscall)) * T_OrigAabb)(void *, const S_vector *, const S_vector *, void *);
typedef bool(__attribute__((thiscall)) * T_OrigEdge)(void *, const S_vector *, const S_vector *, void *, uint32_t *, uint32_t, uint32_t);
typedef bool(__attribute__((thiscall)) * T_OrigTri)(void *, S_CollisionTriangle *, void *, void *);
typedef bool(__attribute__((thiscall)) * T_OrigCapsule)(void *, float, float, void *);

struct S_Stats {
    long sweeps = 0, hits = 0, differ = 0;
};
// The port's result `a` against the original's `b` from the same sweep `s`.
void Compare(const char *what, bool ha, const S_SphereSweep &a, bool hb, const S_SphereSweep &b, const S_SphereSweep &s, S_Stats &st,
             uint32_t cornerA = 0, uint32_t cornerB = 0)
{
    ++st.sweeps;
    st.hits += hb;
    const float tol = 1e-3f * std::max(1.0f, s.length);
    bool same = ha == hb && cornerA == cornerB;
    if (same && ha) {
        S_vector na = a.hitNormal, nb = b.hitNormal;
        na.Normalize();
        nb.Normalize();
        same = (a.hitPos - b.hitPos).Magnitude() <= tol && (na - nb).Magnitude() <= 1e-2f &&
               std::fabs(std::sqrt(std::max(a.best, 0.0f)) - std::sqrt(std::max(b.best, 0.0f))) <= tol && (a.best < 0) == (b.best < 0);
    }
    CHECK(same);
    if (!same && st.differ++ < 6)
        std::printf("  %s from %.7g %.7g %.7g dir %.7g %.7g %.7g r %.7g best %.7g: port %d %.7g corner %x, original %d %.7g corner %x\n", what,
                    s.from.x, s.from.y, s.from.z, s.dir.x, s.dir.y, s.dir.z, s.radius, s.best, ha, a.best, cornerA, hb, b.best, cornerB);
}

// A random sweep toward a point near `at` (from 1.5..6 m beyond `reach`), its best the move's squared length, or
// (every fourth) a nearer contact met before.
S_SphereSweep RandomSweep(std::mt19937 &rng, const S_vector &at, float reach, float spread)
{
    std::uniform_real_distribution<float> u(-1, 1), e(0.2f, 2.0f), rr(0.1f, 1.2f), len(0.5f, 20.0f);
    S_SphereSweep s;
    s.from = at + S_vector(u(rng), u(rng), u(rng)).Normalized() * (reach + 1.5f + 3 * e(rng));
    const S_vector target = at + S_vector(u(rng), u(rng), u(rng)) * spread;
    s.dir = (target - s.from).Normalized() * len(rng);
    if (rng() % 7 == 0)
        s.dir = S_vector(0, s.dir.y < 0 ? -len(rng) : len(rng), 0);                // (straight up or down)
    s.radius = rr(rng);
    s.best = s.dir.Square();
    s.length = std::sqrt(s.best);
    s.normDir = s.dir * (1.0f / s.length);
    SetSweepEnd(s, s.from + s.dir);
    if (rng() % 4 == 0)
        s.best *= 0.1f + 0.8f * (u(rng) + 1) * 0.5f;
    return s;
}

void TestAabbs(std::mt19937 &rng)
{
    std::uniform_real_distribution<float> e(0.05f, 2.0f);
    S_Stats st;
    for (int i = 0; i < 200000; ++i) {
        const S_vector half(e(rng), e(rng), e(rng)), center(e(rng), e(rng), e(rng));
        const S_vector min = center - half, max = center + half;
        const S_SphereSweep s = RandomSweep(rng, center, half.Magnitude(), half.Magnitude() * 1.2f);
        S_SphereSweep a = s, b = s;
        const bool ha = SweepSphereAabb(min, max, a);
        ToOrig(b);
        const bool hb = OriginalAt<T_OrigAabb>(0x00706cd0)(g_scratch, &min, &max, g_orig);
        FromOrig(b);
        Compare("aabb", ha, a, hb, b, s, st);
    }
    std::printf("  boxes: %ld sweeps, %ld hits, %ld differ\n", st.sweeps, st.hits, st.differ);
    CHECK(st.hits > st.sweeps / 4);
}

void TestEdges(std::mt19937 &rng)
{
    std::uniform_real_distribution<float> u(-1, 1), e(0.1f, 4.0f);
    S_Stats st;
    for (int i = 0; i < 200000; ++i) {
        const S_vector a0(u(rng) * 3, u(rng) * 3, u(rng) * 3);
        const S_vector b0 = a0 + S_vector(u(rng), u(rng), u(rng)).Normalized() * e(rng);
        const S_SphereSweep s = RandomSweep(rng, (a0 + b0) * 0.5f, (b0 - a0).Magnitude() * 0.5f, (b0 - a0).Magnitude());
        S_SphereSweep a = s, b = s;
        uint32_t ca = 0, cb = 0;
        const bool ha = SweepSphereEdge(a0, b0, a, ca, 1, 2);
        ToOrig(b);
        const bool hb = OriginalAt<T_OrigEdge>(0x007045a0)(g_scratch, &a0, &b0, g_orig, &cb, 1, 2);
        FromOrig(b);
        Compare("edge", ha, a, hb, b, s, st, ca, cb);
    }
    std::printf("  edges: %ld sweeps, %ld hits, %ld differ\n", st.sweeps, st.hits, st.differ);
    CHECK(st.hits > st.sweeps / 20);
}

void TestTriangles(std::mt19937 &rng)
{
    std::uniform_real_distribution<float> u(-1, 1), e(0.1f, 4.0f);
    S_Stats st;
    for (int i = 0; i < 200000; ++i) {
        // A triangle as the levels store it: v0..v2 by x, the normal into the surface, d of its plane.
        S_vector v[3];
        for (S_vector &p : v)
            p = S_vector(u(rng), u(rng), u(rng)) * e(rng);
        std::sort(v, v + 3, [](const S_vector &p, const S_vector &q) { return p.x < q.x; });
        S_vector n = (v[1] - v[0]).Cross(v[2] - v[0]);
        if (n.Magnitude() < 1e-3f)
            continue;
        n.Normalize();
        if (rng() & 1)
            n = -n;
        S_vector4 corners[3] = {{v[0].x, v[0].y, v[0].z, 0}, {v[1].x, v[1].y, v[1].z, 0}, {v[2].x, v[2].y, v[2].z, 0}};
        S_CollisionTriangle tri;
        tri.flags = (i % 3 == 0 ? COLLISION_TRIANGLE_TWO_SIDED : 0u) | (i % 5 == 0 ? COLLISION_TRIANGLE_REVERSED : 0u) | (1 + i % 7);
        tri.v0 = &corners[0];
        tri.v1 = &corners[1];
        tri.v2 = &corners[2];
        tri.normal = n;
        tri.d = -n.Dot(v[0]);
        const S_vector c = (v[0] + v[1] + v[2]) * (1.0f / 3.0f);
        const S_SphereSweep s = RandomSweep(rng, c, (v[2] - v[0]).Magnitude() * 0.5f, (v[2] - v[0]).Magnitude() * 0.6f);
        S_SphereSweep a = s, b = s;
        const bool ha = SweepSphereTriangle(tri, a);
        ToOrig(b);
        uint32_t unused = 0;
        const bool hb = OriginalAt<T_OrigTri>(0x00705130)(g_scratch, &tri, g_orig, &unused);
        FromOrig(b);
        Compare("triangle", ha, a, hb, b, s, st);
        CHECK(!ha || a.hit == &tri);
    }
    std::printf("  triangles: %ld sweeps, %ld hits, %ld differ\n", st.sweeps, st.hits, st.differ);
    CHECK(st.hits > st.sweeps / 10);
}

void TestCapsules(std::mt19937 &rng)
{
    std::uniform_real_distribution<float> e(0.2f, 1.5f), h(0.1f, 3.0f);
    S_Stats st;
    for (int i = 0; i < 200000; ++i) {
        const float radius = e(rng), half = radius + h(rng);
        const S_SphereSweep s = RandomSweep(rng, S_vector(0, 0, 0), half, half);
        S_SphereSweep a = s, b = s;
        const bool ha = SweepSphereCapsule(radius, half, a);
        ToOrig(b);
        const bool hb = OriginalAt<T_OrigCapsule>(0x0070ffc0)(g_scratch, radius, half, g_orig);
        FromOrig(b);
        Compare("capsule", ha, a, hb, b, s, st);
    }
    std::printf("  capsules: %ld sweeps, %ld hits, %ld differ\n", st.sweeps, st.hits, st.differ);
    CHECK(st.hits > st.sweeps / 10);
}

// A rigid matrix (row vectors: p * M) turned by yaw about y, then pitch, at pos; and its inverse.
S_matrix RigidAt(float yaw, float pitch, const S_vector &pos, S_matrix &inv)
{
    S_matrix m;
    std::memset(&m, 0, sizeof m);
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    m.m[0][0] = cy;
    m.m[0][2] = -sy;
    m.m[1][0] = sy * sp;
    m.m[1][1] = cp;
    m.m[1][2] = cy * sp;
    m.m[2][0] = sy * cp;
    m.m[2][1] = -sp;
    m.m[2][2] = cy * cp;
    m.m[3][0] = pos.x;
    m.m[3][1] = pos.y;
    m.m[3][2] = pos.z;
    m.m[3][3] = 1.0f;
    std::memset(&inv, 0, sizeof inv);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            inv.m[i][j] = m.m[j][i];
    for (int j = 0; j < 3; ++j)
        inv.m[3][j] = -(pos.x * inv.m[0][j] + pos.y * inv.m[1][j] + pos.z * inv.m[2][j]);
    inv.m[3][3] = 1.0f;
    return m;
}

// The static primitives as the files store them (boxes, axis-aligned boxes, spheres, rectangles, poles, capsules)
// through SweepSpherePrimitive (0x00710580): the same layouts are read by both.
void TestPrimitives(std::mt19937 &rng)
{
    typedef bool(__attribute__((thiscall)) * T_OrigPrim)(void *, void *, void *);
    std::uniform_real_distribution<float> u(-1, 1), e(0.1f, 2.5f), ang(0.0f, 6.2831853f);
    const char *names[8] = {"box", "aabb", "sphere", "rect", "pole", "", "", "capsule"};
    S_Stats st[8];
    alignas(16) uint8_t prim[0x100];
    for (int i = 0; i < 300000; ++i) {
        int kind = i % 6;
        if (kind == 5)
            kind = 7;
        std::memset(prim, 0, sizeof prim);
        *reinterpret_cast<uint32_t *>(prim) = COLLISION_PRIM_BOX + kind;
        const S_vector at(u(rng) * 5, u(rng) * 5, u(rng) * 5), half(e(rng), e(rng), e(rng));
        S_matrix inv;
        float reach = half.Magnitude();
        switch (kind) {
        case 0: {
            auto &b = *reinterpret_cast<S_CollisionBox *>(prim);
            b.matrix = RigidAt(ang(rng), u(rng) * 0.6f, at, inv);
            b.invMatrix = inv;
            b.localMin = {-half.x, -half.y, -half.z, 0};
            b.localMax = {half.x, half.y, half.z, 0};
            b.worldMin = at - S_vector(reach, reach, reach);
            b.worldMax = at + S_vector(reach, reach, reach);
            break;
        }
        case 1: {
            auto &b = *reinterpret_cast<S_CollisionAabb *>(prim);
            b.min = at - half;
            b.max = at + half;
            break;
        }
        case 2: {
            auto &b = *reinterpret_cast<S_CollisionSphere *>(prim);
            b.radius = half.x;
            b.center = {at.x, at.y, at.z, 0};
            reach = half.x;
            break;
        }
        case 3: {
            auto &b = *reinterpret_cast<S_CollisionRect *>(prim);
            b.matrix = RigidAt(ang(rng), u(rng) * 0.8f, at, inv);
            b.invMatrix = inv;
            b.min = S_vector(-half.x, 0, -half.z);
            b.max = S_vector(half.x, 0, half.z);
            break;
        }
        case 4: {
            auto &b = *reinterpret_cast<S_CollisionPole *>(prim);
            b.x = at.x;
            b.z = at.z;
            b.radius = half.x * 0.5f;
            reach = b.radius;
            break;
        }
        default: {
            auto &b = *reinterpret_cast<S_CollisionCapsule *>(prim);
            b.matrix = RigidAt(ang(rng), u(rng) * 0.8f, at, inv);
            b.invMatrix = inv;
            b.radius = half.x * 0.6f;
            b.halfHeight = b.radius + half.y;
            reach = b.halfHeight;
            break;
        }
        }
        const S_SphereSweep s = RandomSweep(rng, at, reach, reach * 1.2f);
        S_SphereSweep a = s, b = s;
        const bool ha = SweepSpherePrimitive(prim, a);
        ToOrig(b);
        const bool hb = OriginalAt<T_OrigPrim>(0x00710580)(g_scratch, prim, g_orig);
        FromOrig(b);
        Compare(names[kind], ha, a, hb, b, s, st[kind]);
        CHECK(!ha || (a.hit == prim && b.hit == prim));
    }
    for (int k = 0; k < 8; ++k)
        if (st[k].sweeps) {
            std::printf("  %s primitives: %ld sweeps, %ld hits, %ld differ\n", names[k], st[k].sweeps, st[k].hits, st[k].differ);
            CHECK(st[k].hits > st[k].sweeps / 20);
        }
}

// The dumps' replays.
int ReplayTriangles(const char *path)
{
    FILE *f = std::fopen(path, "r");
    float v[21];
    unsigned flags;
    int lines = 0, same = 0;
    while (f && std::fscanf(f, "%f %f %f %f %f %f %f %f %f %f %f %f %f %u %f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
                            &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &flags, &v[13], &v[14], &v[15], &v[16], &v[17], &v[18], &v[19], &v[20]) == 22) {
        S_vector4 corners[3] = {{v[0], v[1], v[2], 0}, {v[3], v[4], v[5], 0}, {v[6], v[7], v[8], 0}};
        S_CollisionTriangle tri;
        tri.flags = flags;
        tri.v0 = &corners[0];
        tri.v1 = &corners[1];
        tri.v2 = &corners[2];
        tri.normal = S_vector(v[9], v[10], v[11]);
        tri.d = v[12];
        S_SphereSweep s;
        s.from = S_vector(v[13], v[14], v[15]);
        s.dir = S_vector(v[16], v[17], v[18]);
        s.radius = v[19];
        s.best = s.dir.Square();
        s.length = std::sqrt(s.best);
        s.normDir = s.dir * (1.0f / s.length);
        SetSweepEnd(s, s.from + s.dir);
        S_SphereSweep a = s, b = s;
        const bool ha = SweepSphereTriangle(tri, a);
        ToOrig(b);
        uint32_t unused = 0;
        const bool hb = OriginalAt<T_OrigTri>(0x00705130)(g_scratch, &tri, g_orig, &unused);
        FromOrig(b);
        ++lines;
        same += ha == hb && (!ha || std::fabs(a.best - b.best) <= 1e-3f * std::max(1.0f, b.best));
        if (lines <= 12)
            std::printf("  flags %x: port %d %.6g, original %d %.6g, my exact distance %.6g\n", flags, ha, ha ? std::sqrt(std::max(a.best, 0.0f)) : -1.0f, hb,
                        hb ? std::sqrt(std::max(b.best, 0.0f)) : -1.0f, v[20]);
    }
    if (f)
        std::fclose(f);
    std::printf("tri: %d sweeps, the port's and the original's the same on %d\n", lines, same);
    return lines != same;
}
int ReplayCapsules(const char *path)
{
    FILE *f = std::fopen(path, "r");
    float v[13], exact;
    int met, lines = 0, same = 0;
    while (f && std::fscanf(f, "%f %f %f %f %f %f %f %f %f %f %f %f %f %d %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9],
                            &v[10], &v[11], &v[12], &met, &exact) == 15) {
        S_SphereSweep s;
        s.from = S_vector(v[2], v[3], v[4]);
        s.dir = S_vector(v[5], v[6], v[7]);
        s.normDir = S_vector(v[8], v[9], v[10]);
        s.radius = v[11];
        s.best = v[12];
        s.length = std::sqrt(s.best);
        S_SphereSweep a = s, b = s;
        const bool ha = SweepSphereCapsule(v[0], v[1], a);
        ToOrig(b);
        const bool hb = OriginalAt<T_OrigCapsule>(0x0070ffc0)(g_scratch, v[0], v[1], g_orig);
        FromOrig(b);
        ++lines;
        same += ha == hb && (!ha || std::fabs(a.best - b.best) <= 1e-3f * std::max(1.0f, b.best));
        if (lines <= 16)
            std::printf("  capsule r %.4g half %.4g from %.4g %.4g %.4g nd %.4g %.4g %.4g radius %.4g best %.4g (SphereTest met it %d, my exact %.4g): "
                        "port %d %.6g, original %d %.6g\n",
                        v[0], v[1], v[2], v[3], v[4], v[8], v[9], v[10], v[11], v[12], met, exact, ha, a.best, hb, b.best);
    }
    if (f)
        std::fclose(f);
    std::printf("capsule: %d sweeps, the port's and the original's the same on %d\n", lines, same);
    return lines != same;
}
}  // namespace

int main(int argc, char **argv)
{
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (!LoadOriginalExe()) {
        std::printf("sweep diff: no original executable, nothing compared\n");
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[1], "tri") == 0)
        return ReplayTriangles(argv[2]);
    if (argc > 2 && std::strcmp(argv[1], "capsule") == 0)
        return ReplayCapsules(argv[2]);
    std::mt19937 rng(argc > 1 ? std::atoi(argv[1]) : 1);
    TestAabbs(rng);
    TestEdges(rng);
    TestTriangles(rng);
    TestCapsules(rng);
    TestPrimitives(rng);
    std::printf("sweep diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
