// The collision world: game/collision.cpp, collision_body.cpp, collision_ray.cpp and collision_sweep.cpp linked as
// they are, with the LS3DF port's math. MinGW; build.bat; run collision_test.exe ("collision: N checks, 0 failures").
//
// Part 1 (loading, rays, volumes, region queries): the levels' collision files (tree.klz of H:\HD2_Unpacked\MISSIONS,
// env MISSIONS_DIR) are read from disk; the scene's frames are not loaded, so the game's triangles (all on the
// scene's visuals) are disabled and the levels' primitives are tested, against my own ray tests and the brute-force
// RayTestBrute. Triangles, capsule and convex primitives come from collision files written here in the same format
// (WriteSynth), some with fake visuals as the game's. Each query is compared with my own reading of its rules.
// COLLISION_QUICK (the mutation runs): a quarter of the rays.
#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <limits>
#include <set>
#include <string>
#include <vector>

#define private public
#define protected public
#include "game/collision.h"
#include "game/collision_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

extern "C" unsigned int __cdecl _controlfp(unsigned int newValue, unsigned int mask);

// COLLISION_QUICK (the mutation runs): a quarter of the rays.
#ifdef COLLISION_QUICK
constexpr int QUICK = 4;
#else
constexpr int QUICK = 1;
#endif

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            if (failures < 60)                                                      \
                std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);         \
        }                                                                           \
    } while (0)

std::string g_missions;                                 // the missions directory
std::map<int, FILE *> g_open;
int g_nextHandle = 1;
}  // namespace

// ---- files: read from disk under the missions directory ("missions\NAME\tree.klz")
int g_archivesFirst = -1;                               // dtaOpen's second argument
int RW_DATA_CALL dtaOpen(const char *name, bool archivesFirst)
{
    g_archivesFirst = archivesFirst;
    std::string path = name;
    if (path.rfind("missions\\", 0) == 0)
        path = g_missions + "\\" + path.substr(9);
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return DTA_INVALID_HANDLE;
    g_open[g_nextHandle] = f;
    return g_nextHandle++;
}
bool RW_DATA_CALL dtaClose(int handle)
{
    if (!g_open.count(handle))
        return false;
    std::fclose(g_open[handle]);
    g_open.erase(handle);
    return true;
}
int RW_DATA_CALL dtaSeek(int handle, int offset, int origin)
{
    FILE *f = g_open[handle];
    std::fseek(f, offset, origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END);
    return static_cast<int>(std::ftell(f));
}
int RW_DATA_CALL dtaRead(int handle, void *data, int size) { return static_cast<int>(std::fread(data, 1, size, g_open[handle])); }

// ---- the game: a scene whose frames are not found
namespace {
alignas(16) unsigned char g_gameBytes[sizeof(C_game) + 0x100];
void *g_driverVt[64];
struct {
    void **vt = g_driverVt;
} g_driver;
int g_framesAsked;
std::map<std::string, I3D_frame *> g_sceneFrames;      // the frames the scene has (the levels: none)
I3D_frame *__stdcall F_FindFrame(void *, void *, const char *name)
{
    ++g_framesAsked;
    return g_sceneFrames.count(name) ? g_sceneFrames[name] : nullptr;
}
// Visuals with a mesh of one level whose vertices (0x20 bytes each, the position first) are a test's.
void *g_frameVt[64];
void *g_meshVt[16];
void *g_levelVt[16];
std::map<const I3D_frame *, uint32_t> g_visualType;
std::map<const I3D_frame *, int> g_worldSpaceSet;
uint32_t __stdcall F_GetVisualType(I3D_frame *self) { return g_visualType.count(self) ? g_visualType[self] : 0; }
void __stdcall F_SetWorldSpace(I3D_frame *self, int value) { g_worldSpaceSet[self] = value; }
struct S_FakeLevel {
    void **vt = g_levelVt;
    std::vector<float> vertices;                        // 8 floats per vertex
};
struct S_FakeMesh {
    void **vt = g_meshVt;
    S_FakeLevel *level = nullptr;
};
int g_levelAsked = -1;                                 // GetLevel's argument
I3D_mesh_level *__stdcall F_GetLevel(S_FakeMesh *self, int index)
{
    g_levelAsked = index;
    return reinterpret_cast<I3D_mesh_level *>(self->level);
}
int g_verticesAsked = -1;                              // GetVertices' argument
void *__stdcall F_GetVertices(S_FakeLevel *self, int which)
{
    g_verticesAsked = which;
    return self->vertices.data();
}
// A frame of `type` (a visual: of `visualType`, with a mesh of `vertices`).
I3D_frame *SceneFrame(int type, uint32_t visualType, const std::vector<S_vector> &vertices)
{
    auto *bytes = static_cast<unsigned char *>(::operator new(0x400, std::align_val_t(16)));
    std::memset(bytes, 0, 0x400);
    auto *f = reinterpret_cast<I3D_frame *>(bytes);
    *reinterpret_cast<void ***>(f) = g_frameVt;
    f->m_type = static_cast<E_I3D_FRAME_TYPE>(type);
    g_visualType[f] = visualType;
    if (type == FRAME_VISUAL) {
        auto *level = new S_FakeLevel();
        for (const S_vector &v : vertices)
            level->vertices.insert(level->vertices.end(), {v.x, v.y, v.z, 0, 0, 0, 0, 0});
        auto *mesh = new S_FakeMesh();
        mesh->level = level;
        *reinterpret_cast<S_FakeMesh **>(bytes + 0x1E0) = mesh;
    }
    return f;
}
}  // namespace
C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameBytes); }
extern "C" I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&g_driver); }
void I3D_frame::UpdateWMatrixProc() {}
uint32_t C_Human::GetStanceFlags() const { return 0; }

namespace {
// A file of my own in %TEMP%, named after the process (runs in parallel, as the mutation runner's, must not
// share them).
std::set<std::string> g_tempFiles;                     // (deleted at the end)
std::string TempFile(const char *name)
{
    const char *temp = std::getenv("TEMP");
    const std::string path = std::string(temp ? temp : ".") + "\\collision_" + std::to_string(GetCurrentProcessId()) + "_" + name;
    g_tempFiles.insert(path);
    return path;
}

std::vector<std::string> Levels()
{
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((g_missions + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    do {
        const std::string name = fd.cFileName;
        if (name == "." || name == ".." || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (GetFileAttributesA((g_missions + "\\" + name + "\\tree.klz").c_str()) != INVALID_FILE_ATTRIBUTES)
            out.push_back(name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

// ============================================================ part 1a: the levels' files
// My own reading of a level file's layout (header, frame records, the mesh block's tables and cells), to check
// the loader against.
struct S_RawLevel {
    std::vector<uint8_t> bytes;
    int numFrames = 0, meshAt = 0;
    S_CollisionMesh head{};
    // Per cell in the file's order (z, y, x): its index in the loaded array and its raw entries.
    struct S_Cell {
        int index;
        float maxY, minY;
        std::vector<uint32_t> entries;
    };
    std::vector<S_Cell> cells;
    std::map<uint32_t, int> tableOffset;                // table kind -> offset in the mesh block
};
bool ReadRaw(const std::string &path, S_RawLevel &raw)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    raw.bytes.resize(std::ftell(f));
    std::fseek(f, 0, SEEK_SET);
    const size_t got = std::fread(raw.bytes.data(), 1, raw.bytes.size(), f);
    std::fclose(f);
    if (got != raw.bytes.size() || raw.bytes.size() < 24)
        return false;
    const auto u32 = [&](size_t at) {
        uint32_t v;
        std::memcpy(&v, raw.bytes.data() + at, 4);
        return v;
    };
    raw.numFrames = static_cast<int>(u32(12));
    raw.meshAt = static_cast<int>(u32(8));
    if (raw.meshAt < 24 || raw.meshAt + sizeof raw.head > raw.bytes.size())
        return false;
    std::memcpy(&raw.head, raw.bytes.data() + raw.meshAt, sizeof raw.head);
    const S_CollisionMesh &m = raw.head;
    // Tables: the grid lines (x, y, z and 4 more), then the triangles and the primitives in the loader's order,
    // the vertices and one spare vertex, then the cells.
    int at = sizeof(S_CollisionMesh) + 4 * (m.cellsX + 1 + m.cellsY + 1 + m.cellsZ + 5);
    const auto table = [&](uint32_t kind, int count, int size) {
        raw.tableOffset[kind] = at;
        at += count * size;
    };
    table(0, m.numTriangles, 0x20);
    table(COLLISION_PRIM_AABB, m.numAabbs, 0x20);
    table(COLLISION_PRIM_BOX, m.numBoxes, 0xC0);
    table(COLLISION_PRIM_POLE, m.numPoles, 0x20);
    table(COLLISION_PRIM_RECT, m.numRects, 0xA0);
    table(COLLISION_PRIM_SPHERE, m.numSpheres, 0x20);
    table(COLLISION_PRIM_CONVEX, m.numConvexes, 0x220);
    table(0x86, m.numPrims86, 0xA0);
    table(COLLISION_PRIM_CAPSULE, m.numCapsules, 0x90);
    at += m.numVertices * 16 + 16;
    for (int z = 0; z < m.cellsZ; ++z)
        for (int y = 0; y < m.cellsY; ++y)
            for (int x = 0; x < m.cellsX; ++x) {
                S_RawLevel::S_Cell c;
                c.index = (m.cellsZ * y + z) * m.cellsX + x;
                if (raw.meshAt + at + 16 > static_cast<int>(raw.bytes.size()))
                    return false;
                const int count = static_cast<int>(u32(raw.meshAt + at));
                std::memcpy(&c.maxY, raw.bytes.data() + raw.meshAt + at + 8, 4);
                std::memcpy(&c.minY, raw.bytes.data() + raw.meshAt + at + 12, 4);
                at += 16;
                for (int i = 0; i < count; ++i, at += 4)
                    c.entries.push_back(u32(raw.meshAt + at));
                raw.cells.push_back(c);
            }
    return raw.meshAt + at == static_cast<int>(raw.bytes.size());
}

void TestLevelLoading()
{
    const std::vector<std::string> levels = Levels();
    std::printf("  %zu levels\n", levels.size());
    CHECK(levels.size() >= 80);
    long entries = 0, prims = 0;
    int skipped = 0;
    for (const std::string &level : levels) {
        S_RawLevel raw;
        const bool parsed = ReadRaw(g_missions + "\\" + level + "\\tree.klz", raw);
        if (!parsed) {
            // (NORMANDY3_MP_ZONE's file is 16 bytes, a stub like the rest of that mission: the loader would read
            // past its end, the original's as well.)
            std::printf("  %s: not a whole collision file, skipped\n", level.c_str());
            ++skipped;
            continue;
        }
        g_framesAsked = 0;
        g_collisionWorld.Load(("missions\\" + level + "\\tree.klz").c_str(), true);
        const S_CollisionMesh *m = g_collisionWorld.m_mesh;
        CHECK(m != nullptr && g_levelCollisionLoaded && g_framesAsked == raw.numFrames);
        if (!m || !parsed)
            continue;
        // The header's counts and grid as in the file; the grid lines from the minimum in cell steps.
        CHECK(m->cellsX == raw.head.cellsX && m->cellsY == raw.head.cellsY && m->cellsZ == raw.head.cellsZ &&
              m->numTriangles == raw.head.numTriangles && m->numBoxes == raw.head.numBoxes && m->numRects == raw.head.numRects &&
              m->minX == raw.head.minX && m->maxZ == raw.head.maxZ);
        bool lines = true;
        for (int i = 0; i < m->cellsX; ++i)
            lines &= m->gridLinesX[i] < m->gridLinesX[i + 1];
        for (int i = 0; i < m->cellsZ; ++i)
            lines &= m->gridLinesZ[i] < m->gridLinesZ[i + 1];
        CHECK(lines && std::fabs(m->gridLinesX[0] - m->minX) < 0.01f && std::fabs(m->gridLinesZ[m->cellsZ] - m->maxZ) < 0.01f);
        // Every cell's entries point where the file says, into the right table, at an entry of its kind; no scene:
        // every triangle disabled.
        const uint8_t *block = reinterpret_cast<const uint8_t *>(m);
        bool cellsOk = true, kindsOk = true;
        for (const S_RawLevel::S_Cell &c : raw.cells) {
            const S_CollisionMeshCell &cell = *m->cells[c.index];
            cellsOk &= cell.count == static_cast<int>(c.entries.size()) && cell.maxY == c.maxY && cell.minY == c.minY;
            for (size_t i = 0; i < c.entries.size() && i < static_cast<size_t>(cell.count); ++i) {
                const uint32_t kind = c.entries[i] >> 24;
                const uint8_t *want = block + raw.tableOffset[kind] + (c.entries[i] & 0xFFFFFF);
                cellsOk &= cell.entries[i] == want;
                const uint32_t flags = *static_cast<const uint32_t *>(cell.entries[i]);
                kindsOk &= kind == 0 ? (flags & COLLISION_FLAG_DISABLED) != 0 : (flags & COLLISION_SHAPE_MASK) == kind;
                ++entries;
                prims += kind != 0;
            }
        }
        CHECK(cellsOk && kindsOk);
        int enabled = 0;
        for (int i = 0; i < m->numTriangles; ++i)
            enabled += !(m->triangles[i].flags & COLLISION_FLAG_DISABLED);
        CHECK(enabled == 0);
        // The dynamic grid: over the level in cells of the volume cell size (at least one).
        const S_CollisionGrid *g = g_collisionWorld.m_grid;
        CHECK(g && g->cellsX == std::max(1, static_cast<int>((m->maxX - m->minX) / m->volumeCellSize)) && g->minX == m->minX);
        g_collisionWorld.Unload(false);
        CHECK(!g_collisionWorld.HasMesh());
    }
    std::printf("  cell entries %ld (primitives %ld)\n", entries, prims);
    CHECK(skipped == 1);
    // A missing file or one of another kind: nothing loaded.
    g_collisionWorld.Load("missions\\NOSUCH\\tree.klz", true);
    CHECK(!g_collisionWorld.HasMesh());
}

// ============================================================ part 1b: rays on the levels' primitives
// My own ray tests (the segment from + t * dir, 0 <= t <= 1; a start inside a solid does not hit it). They report
// the entry fraction or -1.
S_vector Mul(const S_vector &p, const S_matrix &m, bool point)
{
    S_vector r(p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0], p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1],
               p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2]);
    if (point)
        r = S_vector(r.x + m.m[3][0], r.y + m.m[3][1], r.z + m.m[3][2]);
    return r;
}
// Unit vectors compared within a tolerance.
bool SameDir(const S_vector &a, const S_vector &b, float e = 2e-3f) { return (a - b).Magnitude() < e; }
S_vector Norm(const S_vector &v) { return v.Normalized(); }
double SlabEntry(const S_vector &from, const S_vector &dir, const S_vector &lo, const S_vector &hi)
{
    double enter = -1e30, exit = 1e30;
    const float f[3] = {from.x, from.y, from.z}, d[3] = {dir.x, dir.y, dir.z}, l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) <= 1e-8) {
            if (f[a] < l[a] || f[a] > h[a])
                return -1;
            // (An axis the segment runs along counts as entered at its start, as in the original 0x006f46e0: a
            // segment starting inside a box parallel to a face hits it at once.)
            enter = std::max(enter, 0.0);
            continue;
        }
        double t1 = (l[a] - f[a]) / d[a], t2 = (h[a] - f[a]) / d[a];
        if (t1 > t2)
            std::swap(t1, t2);
        enter = std::max(enter, t1);
        exit = std::min(exit, t2);
    }
    if (enter > exit || enter < 0.0 || enter > 1.0)
        return -1;
    return enter;
}
double OwnPrimitive(const void *p, const S_vector &from, const S_vector &dir)
{
    switch (*static_cast<const uint32_t *>(p) & COLLISION_SHAPE_MASK) {
    case COLLISION_PRIM_BOX: {
        const auto &b = *static_cast<const S_CollisionBox *>(p);
        return SlabEntry(Mul(from, b.invMatrix, true), Mul(dir, b.invMatrix, false), ToVector(b.localMin), ToVector(b.localMax));
    }
    case COLLISION_PRIM_AABB: {
        const auto &b = *static_cast<const S_CollisionAabb *>(p);
        return SlabEntry(from, dir, b.min, b.max);
    }
    case COLLISION_PRIM_SPHERE: {
        const auto &s = *static_cast<const S_CollisionSphere *>(p);
        const double cx = s.center.x - from.x, cy = s.center.y - from.y, cz = s.center.z - from.z;
        const double a = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z, b = cx * dir.x + cy * dir.y + cz * dir.z;
        const double c = cx * cx + cy * cy + cz * cz - s.radius * s.radius;
        const double disc = b * b - a * c;
        if (a == 0.0 || disc < 0.0)
            return -1;
        const double t = (b - std::sqrt(disc)) / a;
        return t >= 0.0 && t < 1.0 ? t : -1;
    }
    case COLLISION_PRIM_RECT: {
        const auto &r = *static_cast<const S_CollisionRect *>(p);
        const S_vector lf = Mul(from, r.invMatrix, true), ld = Mul(dir, r.invMatrix, false);
        if (std::fabs(ld.y) <= 1e-8)
            return -1;
        const double t = -lf.y / ld.y;
        if (!(t > 0.0 && t < 1.0))
            return -1;
        const double x = lf.x + ld.x * t, z = lf.z + ld.z * t;
        return r.min.x <= x && x <= r.max.x && r.min.z <= z && z <= r.max.z ? t : -1;
    }
    case COLLISION_PRIM_POLE: {
        const auto &q = *static_cast<const S_CollisionPole *>(p);
        const double fx = from.x - q.x, fz = from.z - q.z;
        const double a = dir.x * dir.x + dir.z * dir.z, b = fx * dir.x + fz * dir.z, c = fx * fx + fz * fz - q.radius * q.radius;
        const double disc = b * b - a * c;
        if (a < 1e-12 || disc < 0.0)
            return -1;
        const double t = (-b - std::sqrt(disc)) / a;
        return t >= 0.0 && t < 1.0 ? t : -1;
    }
    default:
        return -1;
    }
}
// The nearest primitive of the level along the segment, by my tests (all of them, no grid); `second` the next
// nearest distance (ties are not compared).
const void *OwnNearest(const S_CollisionMesh &m, const S_vector &from, const S_vector &dir, double &best, double &second,
                       const std::set<const void *> *only = nullptr)
{
    best = second = 2.0;
    const void *hit = nullptr;
    const auto consider = [&](const void *p) {
        if (only && !only->count(p))
            return;
        const double t = OwnPrimitive(p, from, dir);
        if (t < 0.0)
            return;
        if (t < best) {
            second = best;
            best = t;
            hit = p;
        } else if (t < second) {
            second = t;
        }
    };
    for (int i = 0; i < m.numBoxes; ++i)
        consider(&m.boxes[i]);
    for (int i = 0; i < m.numAabbs; ++i)
        consider(&m.aabbs[i]);
    for (int i = 0; i < m.numSpheres; ++i)
        consider(&m.spheres[i]);
    for (int i = 0; i < m.numRects; ++i)
        consider(&m.rects[i]);
    for (int i = 0; i < m.numPoles; ++i)
        consider(&m.poles[i]);
    return hit;
}
// The primitives listed in static cells (a primitive no cell lists cannot be hit); `lowest`: in the lowest layer's.
std::set<const void *> Listed(const S_CollisionMesh &m, bool lowest = false)
{
    std::set<const void *> out;
    for (int i = 0; i < (lowest ? 1 : m.cellsY) * m.cellsX * m.cellsZ; ++i)
        for (int k = 0; k < m.cells[i]->count; ++k)
            out.insert(m.cells[i]->entries[k]);
    return out;
}

struct S_RayStats {
    long rays = 0, hits = 0, ownAgree = 0, ownSkipped = 0, gridAgree = 0, staticAgree = 0, verticalQueries = 0, kinds[8] = {};
} g_rayStats;

// Random segments in the level: from a point in its box, short and long, any direction; some along the axes and
// some vertical.
void RaysOnLevel(const std::string &level, std::mt19937 &rng, int count)
{
    g_collisionWorld.Load(("missions\\" + level + "\\tree.klz").c_str(), true);
    const S_CollisionMesh &m = *g_collisionWorld.m_mesh;
    const std::set<const void *> listed = Listed(m), lowest = Listed(m, true);
    const float maxY = m.minY + m.cellsY * m.cellSizeY;
    std::uniform_real_distribution<float> ux(m.minX, m.maxX), uy(m.minY, maxY), uz(m.minZ, m.maxZ), unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> len(0.5f, 150.0f);
    // Points targets the primitives too (else few rays meet them).
    std::vector<S_vector> targets;
    for (int i = 0; i < m.numBoxes; ++i)
        targets.push_back((m.boxes[i].worldMin + m.boxes[i].worldMax) * 0.5f);
    for (int i = 0; i < m.numRects; ++i)
        targets.push_back((m.rects[i].matrix)(3));
    for (int i = 0; i < count; ++i) {
        S_vector from(ux(rng), uy(rng), uz(rng));
        if (!targets.empty() && (i & 1))
            from = targets[rng() % targets.size()] + S_vector(unit(rng) * 6.0f, unit(rng) * 3.0f, unit(rng) * 6.0f);
        S_vector dir(unit(rng), unit(rng) * 0.4f, unit(rng));
        switch (i % 10) {
        case 7: dir = S_vector(dir.x, 0.0f, 0.0f); break;
        case 8: dir = S_vector(0.0f, 0.0f, dir.z); break;
        case 9: dir = S_vector(0.0f, -1.0f, 0.0f); break;
        default: break;
        }
        if (dir.Magnitude() < 1e-3f)
            continue;
        dir = dir.Normalized() * len(rng);
        // When the target is targets: aim at it.
        if (!targets.empty() && i % 10 == 3) {
            const S_vector to = targets[rng() % targets.size()];
            dir = to - from + (to - from).Normalized() * 2.0f;
        }
        ++g_rayStats.rays;
        S_vector p1, n1, p2, n2;
        S_CollisionVolume *grid = g_collisionWorld.RayTest(from, dir, &p1, &n1, COLLISION_EXCLUDE_ALL, 0);
        S_CollisionVolume *brute = g_collisionWorld.RayTestBrute(from, dir, &p2, &n2, COLLISION_EXCLUDE_ALL, 0);
        // The grid walk and the brute force: the same nearest hit.
        const bool gridSame = (grid == brute && (!grid || (p1 - p2).Magnitude() < 1e-3f)) ||
                              (grid && brute && std::fabs((p1 - from).Magnitude() - (p2 - from).Magnitude()) < 1e-3f);
        CHECK(gridSame);
        g_rayStats.gridAgree += gridSame;
        // My own over every listed primitive.
        double best, second;
        const void *own = OwnNearest(m, from, dir, best, second);
        if (own && !listed.count(own))
            own = nullptr;                              // (no cell lists it)
        const double ownLen = best * dir.Magnitude();
        const bool ambiguous = own && ((second - best) * dir.Magnitude() < 2e-3 || best * dir.Magnitude() < 1e-3 || (1.0 - best) * dir.Magnitude() < 1e-3);
        if (ambiguous) {
            ++g_rayStats.ownSkipped;
            continue;
        }
        bool agree = (own == nullptr) == (brute == nullptr);
        if (agree && own)
            agree = brute == static_cast<const S_CollisionVolume *>(own) && std::fabs((p2 - from).Magnitude() - ownLen) < 2e-3 * std::max(1.0, ownLen);
        CHECK(agree);
        if (!agree && failures < 20)
            std::printf("  %s: from %g %g %g dir %g %g %g: brute %p own %p (%u) at %g\n", level.c_str(), from.x, from.y, from.z, dir.x, dir.y,
                        dir.z, static_cast<void *>(brute), own, own ? *static_cast<const uint32_t *>(own) & 0xFF : 0u, ownLen);
        g_rayStats.ownAgree += agree;
        // The static-only test walks the lowest layer of cells only (the original's too): my own over the primitives
        // listed there. (A vertical segment goes to the brute-force test: every layer.)
        {
            double b2, s2;
            const bool vertical = std::fabs(dir.z) + std::fabs(dir.x) < 1e-8f;
            const void *own2 = OwnNearest(m, from, dir, b2, s2, vertical ? &listed : &lowest);
            const double len2 = b2 * dir.Magnitude();
            S_vector p3, n3;
            S_CollisionVolume *st = g_collisionWorld.RayTestStatic(from, dir, &p3, &n3, 0);
            if (!(own2 && ((s2 - b2) * dir.Magnitude() < 2e-3 || len2 < 1e-3 || (1.0 - b2) * dir.Magnitude() < 1e-3))) {
                const bool same = (own2 == nullptr) == (st == nullptr) &&
                                  (!st || (st == static_cast<const S_CollisionVolume *>(own2) && std::fabs((p3 - from).Magnitude() - len2) < 2e-3 * std::max(1.0, len2)));
                CHECK(same);
                g_rayStats.staticAgree += same;
            }
        }
        if (brute) {
            ++g_rayStats.hits;
            ++g_rayStats.kinds[*reinterpret_cast<const uint32_t *>(brute) & 7];
            // The normal: a unit vector against the ray (out of the surface).
            CHECK(std::fabs(n2.Magnitude() - 1.0f) < 1e-3f && n2.Dot(dir) <= 1e-3f);
            // The segment query: the same nearest hit, except along a vertical segment, where it finds no static
            // geometry at all (its cell walk divides by the zero x step; the original 0x006ff510 takes the same
            // case of its jump table, 0x00700f60, and visits no cell).
            S_CollisionQuery q;
            q.from = from;
            q.dir = dir;
            q.excludeTypes = COLLISION_EXCLUDE_ALL;
            q.excludeFlags = 0;
            const int n = g_collisionWorld.QuerySegment(q);
            bool sorted = true;
            for (int k = 1; k < n; ++k)
                sorted &= q.hits[k - 1].distance <= q.hits[k].distance;
            // Every hit's place on the segment, its normal a unit vector against the segment (the primitives').
            for (int k = 0; k < n; ++k)
                sorted &= (q.hits[k].pos - (from + dir * q.hits[k].distance)).Magnitude() < 1e-3f && std::fabs(q.hits[k].normal.Magnitude() - 1.0f) < 1e-3f &&
                          q.hits[k].normal.Dot(dir) <= 1e-3f;
            if (dir.x == 0.0f && dir.z == 0.0f) {
                CHECK(n == 0);
                ++g_rayStats.verticalQueries;
            } else {
                CHECK(sorted && n >= 1 && std::fabs(q.hits[0].distance * dir.Magnitude() - (p1 - from).Magnitude()) < 2e-3f * std::max(1.0f, dir.Magnitude()));
            }
        }
    }
    // The filters: every primitive excluded (their flag bit), or the static geometry: no hit.
    CHECK(!g_collisionWorld.RayTest(S_vector(m.minX, m.minY, m.minZ), S_vector(m.maxX - m.minX, maxY - m.minY, m.maxZ - m.minZ), nullptr, nullptr,
                                    COLLISION_EXCLUDE_ALL, COLLISION_FLAG_PRIMITIVE));
    CHECK(!g_collisionWorld.RayTest(S_vector(m.minX, m.minY, m.minZ), S_vector(m.maxX - m.minX, maxY - m.minY, m.maxZ - m.minZ), nullptr, nullptr,
                                    COLLISION_EXCLUDE_ALL, COLLISION_EXCLUDE_ALL));
    g_collisionWorld.Unload(false);
}

void TestLevelRays()
{
    std::mt19937 rng(12345);
    for (const std::string &level : Levels()) {
        S_RawLevel raw;
        if (ReadRaw(g_missions + "\\" + level + "\\tree.klz", raw))
            RaysOnLevel(level, rng, 2000 / QUICK);
    }
    std::printf("  rays %ld, hits %ld (boxes %ld aabbs %ld spheres %ld rects %ld poles %ld), grid = brute %ld, = own %ld (%ld ties skipped)\n",
                g_rayStats.rays, g_rayStats.hits, g_rayStats.kinds[COLLISION_PRIM_BOX & 7], g_rayStats.kinds[COLLISION_PRIM_AABB & 7],
                g_rayStats.kinds[COLLISION_PRIM_SPHERE & 7], g_rayStats.kinds[COLLISION_PRIM_RECT & 7], g_rayStats.kinds[COLLISION_PRIM_POLE & 7],
                g_rayStats.gridAgree, g_rayStats.ownAgree, g_rayStats.ownSkipped);
    std::printf("  static-only = own (lowest layer) %ld; vertical segment queries %ld\n", g_rayStats.staticAgree, g_rayStats.verticalQueries);
    CHECK(g_rayStats.hits > g_rayStats.rays / 10);
    (void)QUICK;
}
// ============================================================ part 1c: triangles (collision files written here)
// A level of my own in the file format: 64 x 16 x 64 m in 8 m cells (two layers), triangles on the file's own
// vertex table, a water rectangle. The triangles are listed in every cell their box overlaps, sorted by their
// smallest x (v0; v2 has the largest), as the game's files are.
struct S_SynthTri {
    int frame = 0x7FFF;                                 // 0x7FFF: the file's vertex table; else a frame record
    int index[3] = {0, 0, 0};                           // vertex indices in that frame's vertices
    S_vector v[3];
    S_vector normal;                                    // into the surface (as stored)
    uint32_t flags;
    int kind;                                           // the kind it is made to be (0: any)
};
struct S_Synth {
    std::vector<std::pair<uint32_t, std::string>> records = {{0u, "nonexist"}};   // frame records: flags, name
    std::vector<S_SynthTri> tris;
    S_matrix rectMatrix;
    S_vector rectMin, rectMax;
    bool withRect = true;
    uint32_t rectFlags = COLLISION_PRIM_RECT | COLLISION_FLAG_WATER;
    // The grid (x and z may differ), the dynamic grid's cell size.
    int cellsX = 8, cellsZ = 8;
    float originX = 0.0f, originZ = 0.0f;                // the level's corner (its minimum x and z)
    float cellX = 8.0f, cellZ = 8.0f, volumeCell = 16.0f;
    // More primitives: listed in every cell (their heights widen the cells' ranges).
    std::vector<S_CollisionCapsule> capsules;
    std::vector<S_CollisionConvexPrim> convexes;        // (frame: the index of a frame record)
    std::vector<std::pair<float, float>> primHeights;   // their y ranges
};
constexpr float SYN_CELL = 8.0f;
constexpr int SYN_CELLS = 8, SYN_LAYERS = 2;

S_vector Cross(const S_vector &a, const S_vector &b) { return S_vector(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
// Orders the vertices: v0 the smallest x, v2 the largest (v1 between), unless `keep` (made with that order).
void OrderByX(S_SynthTri &t)
{
    std::sort(t.v, t.v + 3, [](const S_vector &a, const S_vector &b) { return a.x < b.x; });
}
S_SynthTri Tri(S_vector a, S_vector b, S_vector c, uint32_t flags, bool flip, int kind = 0)
{
    S_SynthTri t;
    t.v[0] = a;
    t.v[1] = b;
    t.v[2] = c;
    if (!kind)
        OrderByX(t);
    t.normal = Cross(t.v[1] - t.v[0], t.v[2] - t.v[0]).Normalized();
    if (flip)
        t.normal = -t.normal;
    // The kind the files store in the flags' low byte (the plane the ray test solves in: the normal's largest axis;
    // the v0-v1 edge along an axis where it is).
    const float nx = std::fabs(t.normal.x), ny = std::fabs(t.normal.y), nz = std::fabs(t.normal.z);
    const S_vector e = t.v[1] - t.v[0];
    uint32_t stored;
    if (ny >= nx && ny >= nz)
        stored = e.x == 0.0f ? TRIANGLE_KIND_XZ_EDGE_Z : e.z == 0.0f ? TRIANGLE_KIND_XZ_EDGE_X : TRIANGLE_KIND_XZ;
    else if (nx >= nz)
        stored = e.y == 0.0f ? TRIANGLE_KIND_YZ_EDGE_Z : TRIANGLE_KIND_YZ;
    else
        stored = e.x == 0.0f ? TRIANGLE_KIND_XY_EDGE_Y : TRIANGLE_KIND_XY;
    t.flags = flags | stored;
    t.kind = kind;
    if (kind && static_cast<int>(stored) != kind)
        std::printf("  (made kind %d, computed %u)\n", kind, stored);
    return t;
}
S_Synth MakeSynth(std::mt19937 &rng, int count)
{
    S_Synth s;
    std::uniform_real_distribution<float> pos(1.0f, 63.0f), py(0.5f, 15.5f), size(0.3f, 7.0f), unit(-1.0f, 1.0f);
    for (int i = 0; i < count; ++i) {
        const S_vector c(pos(rng), py(rng), pos(rng));
        const float r = size(rng);
        const S_vector a = c + S_vector(unit(rng), unit(rng), unit(rng)) * r;
        const S_vector b = c + S_vector(unit(rng), unit(rng), unit(rng)) * r;
        const S_vector d = c + S_vector(unit(rng), unit(rng), unit(rng)) * r;
        if (Cross(b - a, d - a).Magnitude() < 0.05f)
            continue;
        uint32_t flags = 0;
        if (i % 5 == 0)
            flags |= COLLISION_TRIANGLE_TWO_SIDED;
        if (i % 11 == 0)
            flags |= COLLISION_FLAG_WATER;
        S_SynthTri t = Tri(a, b, d, flags, rng() & 1);
        if (t.v[0].x < 0.0f || t.v[2].x > 64.0f || std::min({t.v[0].z, t.v[1].z, t.v[2].z}) < 0.0f || std::max({t.v[0].z, t.v[1].z, t.v[2].z}) > 64.0f ||
            std::min({t.v[0].y, t.v[1].y, t.v[2].y}) < 0.0f || std::max({t.v[0].y, t.v[1].y, t.v[2].y}) > 16.0f)
            continue;
        s.tris.push_back(t);
    }
    // The axis-aligned kinds (the edge v0-v1 along an axis), each a few times.
    for (int i = 0; i < 6; ++i) {
        const S_vector c(pos(rng), py(rng), pos(rng));
        const float r = 1.0f + i * 0.5f;
        // Facing y, v0-v1 along x (v0 smallest x, v2 largest).
        s.tris.push_back(Tri(c, c + S_vector(r, 0, 0), c + S_vector(2 * r, 0, r), 0, i & 1, TRIANGLE_KIND_XZ_EDGE_X));
        // Facing y, v0-v1 along z.
        s.tris.push_back(Tri(c, c + S_vector(0, 0, r), c + S_vector(r, 0, r * 0.5f), 0, i & 1, TRIANGLE_KIND_XZ_EDGE_Z));
        // Facing y, general.
        s.tris.push_back(Tri(c, c + S_vector(r, 0, r * 0.7f), c + S_vector(2 * r, 0, -r * 0.4f), 0, i & 1, TRIANGLE_KIND_XZ));
        // Facing x, v0-v1 along z (same height).
        s.tris.push_back(Tri(c, c + S_vector(0, 0, r), c + S_vector(0, r, r * 0.5f), 0, i & 1, TRIANGLE_KIND_YZ_EDGE_Z));
        // Facing x, general.
        s.tris.push_back(Tri(c, c + S_vector(0, r * 0.6f, r), c + S_vector(0, r, -r * 0.3f), 0, i & 1, TRIANGLE_KIND_YZ));
        // Facing z, v0-v1 along y.
        s.tris.push_back(Tri(c, c + S_vector(0, r, 0), c + S_vector(r, r * 0.5f, 0), 0, i & 1, TRIANGLE_KIND_XY_EDGE_Y));
        // Facing z, general.
        s.tris.push_back(Tri(c, c + S_vector(r, r * 0.4f, 0), c + S_vector(2 * r, -r * 0.6f, 0), 0, i & 1, TRIANGLE_KIND_XY));
    }
    // The water rectangle: 20 x 12 m at y 2, turned 30 degrees about y.
    const float a = 0.5235988f;
    S_matrix &m = s.rectMatrix;
    std::memset(&m, 0, sizeof m);
    m.m[0][0] = std::cos(a);
    m.m[0][2] = -std::sin(a);
    m.m[1][1] = 1.0f;
    m.m[2][0] = std::sin(a);
    m.m[2][2] = std::cos(a);
    m.m[3][0] = 30.0f;
    m.m[3][1] = 2.0f;
    m.m[3][2] = 30.0f;
    m.m[3][3] = 1.0f;
    s.rectMin = S_vector(-10.0f, 0.0f, -6.0f);
    s.rectMax = S_vector(10.0f, 0.0f, 6.0f);
    return s;
}
S_matrix InverseRigid(const S_matrix &m)
{
    S_matrix r;
    std::memset(&r, 0, sizeof r);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = m.m[j][i];
    for (int j = 0; j < 3; ++j)
        r.m[3][j] = -(m.m[3][0] * r.m[0][j] + m.m[3][1] * r.m[1][j] + m.m[3][2] * r.m[2][j]);
    r.m[3][3] = 1.0f;
    return r;
}
// The file: header, the mesh block (its header, the grid lines, the triangles, the rectangle, the vertices and a
// spare one, the cells in z, y, x order).
std::string WriteSynth(const S_Synth &s, const std::string &path)
{
    std::string out;
    const auto put = [&out](const void *p, size_t n) { out.append(static_cast<const char *>(p), n); };
    const auto u32 = [&](uint32_t v) { put(&v, 4); };
    const auto f32 = [&](float v) { put(&v, 4); };
    // The frame records (the rectangle's frame reference is 0).
    int recordsSize = 0;
    for (const auto &r : s.records)
        recordsSize += 4 + static_cast<int>(r.second.size()) + 1;
    const int recordBase = 24 + 4 * static_cast<int>(s.records.size());
    u32(0x43666947);
    u32(0x10005);
    u32(static_cast<uint32_t>(recordBase + recordsSize));
    u32(static_cast<uint32_t>(s.records.size()));
    u32(0x21F);
    u32(0);
    int at = recordBase;
    for (const auto &r : s.records) {
        u32(static_cast<uint32_t>(at));
        at += 4 + static_cast<int>(r.second.size()) + 1;
    }
    for (const auto &r : s.records) {
        u32(r.first);
        put(r.second.c_str(), r.second.size() + 1);
    }
    S_CollisionMesh head;
    std::memset(&head, 0, sizeof head);
    head.minX = s.originX;
    head.minZ = s.originZ;
    head.maxX = s.originX + s.cellX * s.cellsX;
    head.maxZ = s.originZ + s.cellZ * s.cellsZ;
    head.cellSizeX = s.cellX;
    head.cellSizeZ = s.cellZ;
    head.cellsX = s.cellsX;
    head.cellsZ = s.cellsZ;
    head.volumeCellSize = s.volumeCell;
    head.minY = 0.0f;
    head.cellSizeY = SYN_CELL;
    head.cellsY = SYN_LAYERS;
    head.numTriangles = static_cast<int>(s.tris.size());
    head.numRects = s.withRect ? 1 : 0;
    head.numCapsules = static_cast<int>(s.capsules.size());
    head.numConvexes = static_cast<int>(s.convexes.size());
    head.numVertices = static_cast<int>(s.tris.size() * 3);
    put(&head, sizeof head);
    for (int i = 0; i <= s.cellsX; ++i)
        f32(s.originX + i * s.cellX);
    for (int i = 0; i <= SYN_LAYERS; ++i)
        f32(i * SYN_CELL);
    for (int i = 0; i <= s.cellsZ; ++i)
        f32(s.originZ + i * s.cellZ);
    for (int i = 0; i < 4; ++i)
        f32(0.0f);
    for (size_t i = 0; i < s.tris.size(); ++i) {
        const S_SynthTri &t = s.tris[i];
        u32(t.flags);
        for (int k = 0; k < 3; ++k)
            u32(t.frame == 0x7FFF ? (0x7FFFu << 16) | static_cast<uint32_t>(i * 3 + k)
                                  : (static_cast<uint32_t>(t.frame) << 16) | static_cast<uint32_t>(t.index[k]));
        f32(t.normal.x);
        f32(t.normal.y);
        f32(t.normal.z);
        f32(-t.normal.Dot(t.v[0]));
    }
    if (s.withRect) {
        S_CollisionRect r;
        std::memset(&r, 0, sizeof r);
        r.flags = s.rectFlags;
        r.min = s.rectMin;
        r.max = s.rectMax;
        r.matrix = s.rectMatrix;
        r.invMatrix = InverseRigid(s.rectMatrix);
        put(&r, sizeof r);
    }
    for (const S_CollisionConvexPrim &c : s.convexes)
        put(&c, sizeof c);
    for (const S_CollisionCapsule &c : s.capsules)
        put(&c, sizeof c);
    for (const S_SynthTri &t : s.tris)
        for (const S_vector &v : t.v) {
            f32(v.x);
            f32(v.y);
            f32(v.z);
            f32(1.0f);
        }
    for (int i = 0; i < 4; ++i)
        f32(0.0f);
    for (int z = 0; z < s.cellsZ; ++z)
        for (int y = 0; y < SYN_LAYERS; ++y)
            for (int x = 0; x < s.cellsX; ++x) {
                const float x0 = s.originX + x * s.cellX, z0 = s.originZ + z * s.cellZ, y0 = y * SYN_CELL;
                std::vector<int> in;
                float minY = 1e9f, maxY = -1e9f;
                for (size_t i = 0; i < s.tris.size(); ++i) {
                    const S_SynthTri &t = s.tris[i];
                    S_vector lo = t.v[0], hi = t.v[0];
                    for (const S_vector &v : t.v) {
                        lo = S_vector(std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z));
                        hi = S_vector(std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z));
                    }
                    if (hi.x < x0 || lo.x > x0 + s.cellX || hi.z < z0 || lo.z > z0 + s.cellZ || hi.y < y0 || lo.y > y0 + SYN_CELL)
                        continue;
                    in.push_back(static_cast<int>(i));
                    minY = std::min(minY, lo.y);
                    maxY = std::max(maxY, hi.y);
                }
                std::sort(in.begin(), in.end(), [&](int a, int b) { return s.tris[a].v[0].x < s.tris[b].v[0].x; });
                // The rectangle in the lowest layer's cells under it (all of them: its box is big).
                const bool rect = s.withRect && y == 0 && x0 < 45.0f && x0 + s.cellX > 15.0f && z0 < 45.0f && z0 + s.cellZ > 15.0f;
                if (rect) {
                    minY = std::min(minY, 2.0f - 12.0f);
                    maxY = std::max(maxY, 2.0f + 12.0f);
                }
                const size_t extra = s.capsules.size() + s.convexes.size();
                for (const auto &h : s.primHeights) {
                        minY = std::min(minY, h.first);
                        maxY = std::max(maxY, h.second);
                    }
                u32(static_cast<uint32_t>(in.size() + rect + extra));
                u32(0);
                f32(maxY);
                f32(minY);
                // (Primitives first, then the triangles: the game's files list them so, and the cell tests stop at
                // the first triangle past the segment.)
                if (rect)
                    u32(static_cast<uint32_t>(COLLISION_PRIM_RECT) << 24);
                {
                    for (size_t k = 0; k < s.convexes.size(); ++k)
                        u32((static_cast<uint32_t>(COLLISION_PRIM_CONVEX) << 24) | static_cast<uint32_t>(k * sizeof(S_CollisionConvexPrim)));
                    for (size_t k = 0; k < s.capsules.size(); ++k)
                        u32((static_cast<uint32_t>(COLLISION_PRIM_CAPSULE) << 24) | static_cast<uint32_t>(k * sizeof(S_CollisionCapsule)));
                }
                for (int i : in)
                    u32(static_cast<uint32_t>(i) * 0x20);
            }
    FILE *f = std::fopen(path.c_str(), "wb");
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return out;
}

// My own ray / triangle test (Moller-Trumbore in doubles): the fraction, or -1; one-sided triangles are crossed
// only along their stored normal (into the surface). `margin`: how far inside the triangle the point is
// (barycentric), to leave out edge cases.
double OwnTriangle(const S_SynthTri &t, const S_vector &from, const S_vector &dir, double &margin)
{
    const double cosine = dir.Dot(t.normal);
    if ((t.flags & COLLISION_TRIANGLE_TWO_SIDED) ? std::fabs(cosine) < 1e-8 : cosine < 1e-8)
        return -1;
    const double e1[3] = {t.v[1].x - t.v[0].x, t.v[1].y - t.v[0].y, t.v[1].z - t.v[0].z};
    const double e2[3] = {t.v[2].x - t.v[0].x, t.v[2].y - t.v[0].y, t.v[2].z - t.v[0].z};
    const double d[3] = {dir.x, dir.y, dir.z};
    const double p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    const double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::fabs(det) < 1e-12)
        return -1;
    const double s[3] = {from.x - t.v[0].x, from.y - t.v[0].y, from.z - t.v[0].z};
    const double u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det;
    const double q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const double v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det;
    const double tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
    // (Only where the segment meets the plane: an edge near the line beyond the segment does not matter.)
    margin = tt > -1e-3 && tt < 1.0 + 1e-3 ? std::min({u, v, 1.0 - u - v}) : 1.0;
    if (u < 0 || v < 0 || u + v > 1 || tt < 0.0 || tt >= 1.0)
        return -1;
    return tt;
}

struct S_SynthStats {
    long rays = 0, hits = 0, edge = 0, agree = 0, queryAgree = 0, waterHits = 0, kinds[8] = {};
} g_synth;

void TestSynthTriangles()
{
    std::mt19937 rng(777);
    const S_Synth s = MakeSynth(rng, 500);
    const std::string path = TempFile("synth.klz");
    WriteSynth(s, path);
    g_collisionWorld.Load(path.c_str(), true);
    const S_CollisionMesh *m = g_collisionWorld.m_mesh;
    CHECK(m && m->numTriangles == static_cast<int>(s.tris.size()));
    if (!m)
        return;
    // Every triangle enabled (own vertices), the kinds made to be one got it, the vertices are the file's.
    int enabled = 0, kindsOk = 0, made = 0;
    for (int i = 0; i < m->numTriangles; ++i) {
        const S_CollisionTriangle &t = m->triangles[i];
        enabled += !(t.flags & COLLISION_FLAG_DISABLED);
        ++g_synth.kinds[t.flags & 7];
        if (s.tris[i].kind) {
            ++made;
            kindsOk += static_cast<int>(t.flags & TRIANGLE_KIND_MASK) == s.tris[i].kind;
        }
        // (The loader classifies the lowest layer's triangles again: the same kind as stored.)
        CHECK(t.v0->x == s.tris[i].v[0].x && t.v2->z == s.tris[i].v[2].z && t.flags == s.tris[i].flags);
    }
    CHECK(enabled == m->numTriangles && kindsOk == made && made == 42);
    for (int k = 1; k <= 7; ++k)
        CHECK(g_synth.kinds[k] > 0);
    std::printf("  synthetic: %d triangles, kinds %ld %ld %ld %ld %ld %ld %ld\n", m->numTriangles, g_synth.kinds[1], g_synth.kinds[2], g_synth.kinds[3],
                g_synth.kinds[4], g_synth.kinds[5], g_synth.kinds[6], g_synth.kinds[7]);
    // The water's height range: its triangles' and its rectangle's corners.
    float wmin = 1e9f, wmax = -1e9f;
    for (const S_SynthTri &t : s.tris)
        if (t.flags & COLLISION_FLAG_WATER)
            for (const S_vector &v : t.v) {
                wmin = std::min(wmin, v.y);
                wmax = std::max(wmax, v.y);
            }
    wmin = std::min(wmin, 2.0f);
    wmax = std::max(wmax, 2.0f);
    float gmin, gmax;
    g_collisionWorld.GetWaterHeightRange(gmin, gmax);
    CHECK(std::fabs(gmin - wmin) < 1e-4f && std::fabs(gmax - wmax) < 1e-4f);

    // Rays: the grid walk, the brute force and my own; the segment query lists every crossing; the water test only
    // the water.
    std::uniform_real_distribution<float> pos(-4.0f, 68.0f), py(-2.0f, 18.0f), unit(-1.0f, 1.0f), len(0.5f, 90.0f);
    for (int i = 0; i < 40000 / QUICK; ++i) {
        S_vector from(pos(rng), py(rng), pos(rng));
        S_vector dir(unit(rng), unit(rng), unit(rng));
        if (i % 13 == 0)
            dir = S_vector(0.0f, i % 2 ? 1.0f : -1.0f, 0.0f);
        if (i % 17 == 0)
            dir = S_vector(unit(rng), 0.0f, 0.0f);
        if (i % 19 == 0)
            dir = S_vector(0.0f, 0.0f, unit(rng));
        if (dir.Magnitude() < 1e-3f)
            continue;
        dir = dir.Normalized() * len(rng);
        // Aimed at a triangle a third of the time.
        if (i % 3 == 0) {
            const S_SynthTri &t = s.tris[rng() % s.tris.size()];
            const S_vector target = (t.v[0] + t.v[1] + t.v[2]) * (1.0f / 3.0f);
            dir = (target - from) * 1.5f;
        }
        ++g_synth.rays;
        double best = 2.0, second = 2.0, bestMargin = 1.0;
        int bestTri = -1;
        std::vector<std::pair<double, int>> crossings;
        bool edgeCase = false;
        for (size_t k = 0; k < s.tris.size(); ++k) {
            double margin = 1.0;
            const double t = OwnTriangle(s.tris[k], from, dir, margin);
            if (std::fabs(margin) < 1e-4)
                edgeCase = true;
            if (t < 0.0)
                continue;
            crossings.push_back({t, static_cast<int>(k)});
            if (t < best) {
                second = best;
                best = t;
                bestTri = static_cast<int>(k);
                bestMargin = margin;
            } else if (t < second) {
                second = t;
            }
        }
        // (The rectangle: my own test as well.)
        double rectT = -1;
        {
            const S_matrix inv = InverseRigid(s.rectMatrix);
            const S_vector lf = Mul(from, inv, true), ld = Mul(dir, inv, false);
            if (std::fabs(ld.y) > 1e-8) {
                const double t = -lf.y / ld.y;
                const double x = lf.x + ld.x * t, z = lf.z + ld.z * t;
                if (t > 0.0 && t < 1.0 && x >= -10 && x <= 10 && z >= -6 && z <= 6)
                    rectT = t;
                if (t > 0.0 && t < 1.0 && (std::fabs(std::fabs(x) - 10) < 1e-3 || std::fabs(std::fabs(z) - 6) < 1e-3))
                    edgeCase = true;
            }
        }
        if (edgeCase || (second < 2.0 && (second - best) * dir.Magnitude() < 1e-3) || (rectT >= 0 && std::fabs(rectT - best) * dir.Magnitude() < 1e-3)) {
            ++g_synth.edge;
            continue;
        }
        const void *wantHit = nullptr;
        double want = 2.0;
        if (bestTri >= 0) {
            want = best;
            wantHit = &m->triangles[bestTri];
        }
        if (rectT >= 0 && rectT < want) {
            want = rectT;
            wantHit = m->rects;
        }
        S_vector p1, n1, p2, n2;
        S_CollisionVolume *grid = g_collisionWorld.RayTest(from, dir, &p1, &n1, COLLISION_EXCLUDE_ALL, 0);
        S_CollisionVolume *brute = g_collisionWorld.RayTestBrute(from, dir, &p2, &n2, COLLISION_EXCLUDE_ALL, 0);
        const double len = dir.Magnitude();
        bool ok = grid == brute && static_cast<const void *>(grid) == wantHit;
        if (ok && grid)
            ok = std::fabs((p1 - from).Magnitude() - want * len) < 2e-3 * std::max(1.0, want * len) && std::fabs(n1.Magnitude() - 1.0f) < 1e-3f;
        CHECK(ok);
        if (!ok && failures < 10)
            std::printf("  synth from %g %g %g dir %g %g %g: grid %p brute %p want %p at %g (margin %g)\n", from.x, from.y, from.z, dir.x, dir.y, dir.z,
                        static_cast<void *>(grid), static_cast<void *>(brute), wantHit, want * len, bestMargin);
        g_synth.agree += ok;
        g_synth.hits += grid != nullptr;
        // The triangle's normal comes back out of the surface: against the ray.
        if (grid && wantHit != m->rects)
            CHECK(n1.Dot(dir) < 0.0f || (s.tris[bestTri].flags & COLLISION_TRIANGLE_TWO_SIDED));
        // The segment query: every triangle crossed (t <= 1 there) and the rectangle, sorted; none for vertical
        // segments (see part 1b).
        S_CollisionQuery q;
        q.from = from;
        q.dir = dir;
        q.excludeTypes = COLLISION_EXCLUDE_ALL;
        q.excludeFlags = 0;
        const int n = g_collisionWorld.QuerySegment(q);
        std::vector<const void *> got, wantAll;
        for (int k = 0; k < n; ++k)
            got.push_back(q.hits[k].volume);
        std::sort(crossings.begin(), crossings.end());
        for (auto &c : crossings)
            wantAll.push_back(&m->triangles[c.second]);
        if (rectT >= 0)
            wantAll.push_back(m->rects);
        std::vector<const void *> gs = got, ws = wantAll;
        std::sort(gs.begin(), gs.end());
        std::sort(ws.begin(), ws.end());
        const bool vertical = dir.x == 0.0f && dir.z == 0.0f;
        bool sorted = true;
        for (int k = 1; k < n; ++k)
            sorted &= q.hits[k - 1].distance <= q.hits[k].distance;
        const bool qok = vertical ? n == 0 : (gs == ws && sorted);
        CHECK(qok);
        g_synth.queryAgree += qok;
        // Water: the water triangles and the rectangle only (the rectangle's normal up either way).
        S_vector pw, nw;
        S_CollisionVolume *water = g_collisionWorld.RayTestWater(from, dir, &pw, &nw, 0);
        const void *wantWater = nullptr;
        double ww = 2.0;
        for (auto &c : crossings)
            if ((s.tris[c.second].flags & COLLISION_FLAG_WATER) && c.first < ww) {
                ww = c.first;
                wantWater = &m->triangles[c.second];
            }
        if (rectT >= 0 && rectT < ww) {
            ww = rectT;
            wantWater = m->rects;
        }
        CHECK(static_cast<const void *>(water) == wantWater);
        if (water == static_cast<const void *>(m->rects))
            CHECK(nw.y < -0.999f);                  // (the normal handed back is minus the recorded "up")
        g_synth.waterHits += water != nullptr;
    }
    std::printf("  synthetic rays %ld: %ld hits, %ld agree (grid = brute = own), %ld queries agree, %ld water hits, %ld edge cases skipped\n",
                g_synth.rays, g_synth.hits, g_synth.agree, g_synth.queryAgree, g_synth.waterHits, g_synth.edge);
    CHECK(g_synth.hits > g_synth.rays / 4 && g_synth.waterHits > 100 / QUICK);

    // Exclusions: the water flag, two-sided ones; everything static.
    {
        int excluded = 0;
        for (int i = 0; i < 3000; ++i) {
            const S_SynthTri &t = s.tris[i % s.tris.size()];
            const S_vector target = (t.v[0] + t.v[1] + t.v[2]) * (1.0f / 3.0f);
            const S_vector from = target - t.normal * 3.0f;
            S_CollisionVolume *hit = g_collisionWorld.RayTest(from, t.normal * 6.0f, nullptr, nullptr, COLLISION_EXCLUDE_ALL, COLLISION_FLAG_WATER);
            if (hit && (*reinterpret_cast<uint32_t *>(hit) & COLLISION_FLAG_WATER))
                ++excluded;
        }
        CHECK(excluded == 0);
    }
    // One-sided: a triangle is hit from the side its normal points away from, not from the other.
    {
        int front = 0, back = 0, checked = 0;
        for (size_t i = 0; i < s.tris.size(); ++i) {
            const S_SynthTri &t = s.tris[i];
            if (t.flags & COLLISION_TRIANGLE_TWO_SIDED)
                continue;
            const S_vector target = (t.v[0] + t.v[1] + t.v[2]) * (1.0f / 3.0f);
            S_vector p;
            // (Only where nothing else is in the way: my own test says the triangle is the nearest.)
            double margin;
            const S_vector f1 = target - t.normal * 0.01f, f2 = target + t.normal * 0.01f;
            if (OwnTriangle(t, f1, t.normal * 0.02f, margin) < 0.0)
                continue;
            bool clear = true;
            for (size_t k = 0; k < s.tris.size(); ++k) {
                double mg;
                S_SynthTri both = s.tris[k];
                both.flags |= COLLISION_TRIANGLE_TWO_SIDED;
                if (k != i && OwnTriangle(both, f1, t.normal * 0.02f, mg) >= 0.0)
                    clear = false;
            }
            if (!clear)
                continue;
            ++checked;
            front += g_collisionWorld.RayTestBrute(f1, t.normal * 0.02f, &p, nullptr, COLLISION_EXCLUDE_ALL, 0) ==
                     reinterpret_cast<S_CollisionVolume *>(&m->triangles[i]);
            back += g_collisionWorld.RayTestBrute(f2, -t.normal * 0.02f, &p, nullptr, COLLISION_EXCLUDE_ALL, 0) ==
                    reinterpret_cast<S_CollisionVolume *>(&m->triangles[i]);
        }
        CHECK(checked > 100 && front == checked && back == 0);
    }
    // Infinite and huge segments: answered (the cell walk of an infinite ray is bounded; the knife-kill crash,
    // 43d5ac75), no hang.
    {
        const DWORD start = GetTickCount();
        const float inf = std::numeric_limits<float>::infinity();
        const S_vector dirs[] = {S_vector(inf, 0, 0), S_vector(-inf, 0, 0), S_vector(0, 0, inf), S_vector(inf, -inf, inf), S_vector(1e30f, 0, 1e30f),
                                 S_vector(-1e30f, 1.0f, 0), S_vector(1e20f, -1e20f, -1e20f)};
        for (const S_vector &d : dirs) {
            g_collisionWorld.RayTest(S_vector(32, 8, 32), d, nullptr, nullptr, COLLISION_EXCLUDE_ALL, 0);
            g_collisionWorld.RayTestStatic(S_vector(32, 8, 32), d, nullptr, nullptr, 0);
            S_CollisionQuery q;
            q.from = S_vector(32, 8, 32);
            q.dir = d;
            q.excludeTypes = COLLISION_EXCLUDE_ALL;
            q.excludeFlags = 0;
            g_collisionWorld.QuerySegment(q);
        }
        CHECK(GetTickCount() - start < 5000);
        // A segment ending far outside the level still finds what is in it.
        S_vector p;
        const S_SynthTri &t = s.tris[0];
        const S_vector target = (t.v[0] + t.v[1] + t.v[2]) * (1.0f / 3.0f);
        const S_vector from = target - t.normal * 0.5f;
        double margin;
        if (OwnTriangle(t, from, t.normal * 1.0f, margin) >= 0.0)
            CHECK(g_collisionWorld.RayTest(from, t.normal * 1e6f, &p, nullptr, COLLISION_EXCLUDE_ALL, 0) != nullptr);
    }
    g_collisionWorld.Unload(false);
}
// ============================================================ part 1d: dynamic volumes
// A rigid matrix: turned `yaw` about y and `pitch` about x, at `pos`.
S_matrix Rigid(float yaw, float pitch, const S_vector &pos)
{
    S_matrix m;
    std::memset(&m, 0, sizeof m);
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    // rows: x axis, y axis, z axis (row vectors: p * M)
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
    return m;
}
struct S_TestVolume {
    S_CollisionVolume v;
    S_matrix matrix, inv;
};
// My own ray / capsule (along local y, half height h, radius r): the entry fraction or -1 (a start inside: -1).
double OwnCapsule(const S_vector &from, const S_vector &dir, float r, float h)
{
    const float cap = h - r;
    double best = 2.0;
    // The cylinder.
    const double a = dir.x * dir.x + dir.z * dir.z, b = from.x * dir.x + from.z * dir.z, c = from.x * from.x + from.z * from.z - r * r;
    // (A start within the infinite cylinder, not straight along it, misses: the cylinder's entry lies behind the
    // start and the test gives up there, as the original 0x006f4d70 does.)
    if (a > 1e-8 && c < 0.0)
        return -1;
    if (a > 1e-12 && b * b - a * c >= 0.0) {
        const double t = (-b - std::sqrt(b * b - a * c)) / a;
        const double y = from.y + dir.y * t;
        if (t >= 0.0 && y <= cap && y >= -cap)
            best = std::min(best, t);
    }
    // The ends.
    for (const float cy : {cap, -cap}) {
        const double fx = from.x, fy = from.y - cy, fz = from.z;
        const double aa = dir.Dot(dir), bb = fx * dir.x + fy * dir.y + fz * dir.z, cc = fx * fx + fy * fy + fz * fz - r * r;
        const double disc = bb * bb - aa * cc;
        if (disc < 0.0)
            continue;
        const double t = (-bb - std::sqrt(disc)) / aa;
        const double y = from.y + dir.y * t;
        if (t >= 0.0 && (cy > 0 ? y >= cap - 1e-6 : y <= -cap + 1e-6))
            best = std::min(best, t);
    }
    return best <= 1.0 ? best : -1;
}
bool InsideCapsule(const S_vector &p, float r, float h)
{
    const float cap = h - r;
    const float y = std::max(-cap, std::min(cap, p.y));
    return p.x * p.x + (p.y - y) * (p.y - y) + p.z * p.z < r * r;
}
// The entry fraction of my own test for one volume (-1: none; a start inside: -1, or 0 when `startInsideHits`
// for spheres).
double OwnVolume(const S_TestVolume &tv, const S_vector &from, const S_vector &dir, bool startInsideHits)
{
    const S_CollisionVolume &v = tv.v;
    switch (v.flags & COLLISION_SHAPE_MASK) {
    case COLLISION_SHAPE_BOX:
        return SlabEntry(Mul(from, tv.inv, true), Mul(dir, tv.inv, false), v.bbox.min, v.bbox.max);
    case COLLISION_SHAPE_WORLD_BOX:
        return SlabEntry(from, dir, v.bbox.min, v.bbox.max);
    case COLLISION_SHAPE_SPHERE: {
        if (startInsideHits && (v.center - from).Square() < v.radius * v.radius)
            return 0.0;
        const double cx = v.center.x - from.x, cy = v.center.y - from.y, cz = v.center.z - from.z;
        const double a = dir.Dot(dir), b = cx * dir.x + cy * dir.y + cz * dir.z, c = cx * cx + cy * cy + cz * cz - v.radius * v.radius;
        const double disc = b * b - a * c;
        if (disc < 0.0)
            return -1;
        const double t = (b - std::sqrt(disc)) / a;
        return t >= 0.0 && t < 1.0 ? t : -1;
    }
    case COLLISION_SHAPE_CAPSULE:
        return OwnCapsule(Mul(from, tv.inv, true), Mul(dir, tv.inv, false), v.capsuleRadius, v.radius);
    default:
        return -1;
    }
}
std::vector<S_TestVolume *> MakeVolumes(std::mt19937 &rng, int count, const S_vector &origin = S_vector(0, 0, 0))
{
    std::vector<S_TestVolume *> out;
    std::uniform_real_distribution<float> pos(2.0f, 62.0f), py(1.0f, 14.0f), size(0.3f, 3.0f), ang(0.0f, 6.2831853f);
    for (int i = 0; i < count; ++i) {
        auto *tv = new S_TestVolume();
        S_CollisionVolume &v = tv->v;
        const S_vector c = S_vector(pos(rng), py(rng), pos(rng)) + origin;
        tv->matrix = Rigid(ang(rng), ang(rng) * 0.3f, c);
        tv->inv = InverseRigid(tv->matrix);
        v.matrix = &tv->matrix;
        v.invMatrix = &tv->inv;
        v.center = c;
        v.type = 1u << (rng() % 4) << 8;               // (0x100..0x800: filter bits)
        switch (i % 4) {
        case 0: {
            const S_vector h(size(rng), size(rng), size(rng));
            v.flags = COLLISION_SHAPE_BOX;
            v.bbox.min = -h;
            v.bbox.max = h;
            v.radius = h.Magnitude();
            break;
        }
        case 1: {
            const S_vector h(size(rng), size(rng), size(rng));
            v.flags = COLLISION_SHAPE_WORLD_BOX;
            v.bbox.min = c - h;
            v.bbox.max = c + h;
            v.radius = h.Magnitude();
            break;
        }
        case 2:
            v.flags = COLLISION_SHAPE_SPHERE;
            v.radius = size(rng);
            break;
        default: {
            v.flags = COLLISION_SHAPE_CAPSULE;
            v.capsuleRadius = size(rng) * 0.5f;
            v.radius = v.capsuleRadius + size(rng);    // (the half height)
            break;
        }
        }
        out.push_back(tv);
    }
    return out;
}

void TestVolumes(const S_vector &origin)
{
    std::mt19937 rng(4242);
    S_Synth s = MakeSynth(rng, 0);
    s.withRect = false;
    s.originX = origin.x;
    s.originZ = origin.z;
    const std::string path = TempFile("empty.klz");
    WriteSynth(s, path);
    g_collisionWorld.Load(path.c_str(), true);
    CHECK(g_collisionWorld.HasMesh() && g_collisionWorld.m_grid && g_collisionWorld.m_grid->cellsX == 4 && g_collisionWorld.m_grid->cellSizeX == 16.0f &&
          g_collisionWorld.m_grid->cellsZ == 4 && g_collisionWorld.m_grid->cellSizeZ == 16.0f && g_collisionWorld.m_grid->minX == origin.x &&
          g_collisionWorld.m_grid->minZ == origin.z);
    S_CollisionGrid &grid = *g_collisionWorld.m_grid;
    if (origin.x == 0.0f && origin.z == 0.0f) {

    // The grid: a volume goes into the cell of its center (clamped to the grid); added once; moved between
    // cells; removed. Emptied cells are released on request and reused.
    S_CollisionVolume a, b, c;
    a.flags = b.flags = c.flags = COLLISION_SHAPE_SPHERE;
    a.radius = b.radius = c.radius = 1.0f;
    a.center = S_vector(5, 1, 5);
    b.center = S_vector(70, 1, -9);                     // (outside: the corner cell (3, 0))
    c.center = S_vector(20, 1, 40);
    g_collisionWorld.AddVolume(&a);
    g_collisionWorld.AddVolume(&b);
    g_collisionWorld.AddVolume(&c);
    S_CollisionGridCell *cellA = a.cell;
    g_collisionWorld.AddVolume(&a);
    CHECK(a.cell == grid.cells[0] && a.cell->x == 0 && a.cell->z == 0 && b.cell == grid.cells[3] && c.cell == grid.cells[4 * 2 + 1] &&
          a.cell->volumes.size() == 1);
    // Moved within its cell: nothing; across: out of the old (empty: listed), into the new.
    a.center = S_vector(9, 1, 9);
    g_collisionWorld.MoveVolume(&a);
    CHECK(a.cell == cellA);
    a.center = S_vector(22, 1, 41);
    g_collisionWorld.MoveVolume(&a);
    CHECK(a.cell == c.cell && c.cell->volumes.size() == 2 && cellA->volumes.empty() && g_collisionWorld.m_emptyCells.size() == 1 &&
          grid.cells[0] == cellA);
    // Released: the grid's entry cleared, the cell kept for reuse (the next new cell is it).
    g_collisionWorld.ReleaseEmptyCells();
    CHECK(grid.cells[0] == nullptr && g_collisionWorld.m_freeCells.size() == 1 && g_collisionWorld.m_emptyCells.empty());
    a.center = S_vector(50, 1, 50);
    g_collisionWorld.MoveVolume(&a);
    CHECK(a.cell == cellA && grid.cells[4 * 3 + 3] == cellA && cellA->x == 3 && cellA->z == 3 && g_collisionWorld.m_freeCells.empty());
    // A cell emptied and filled again before the release: kept.
    g_collisionWorld.RemoveVolume(&b);
    S_CollisionGridCell *cellB = grid.cells[3];
    CHECK(!b.cell && cellB && cellB->volumes.empty() && g_collisionWorld.m_emptyCells.size() == 1);
    g_collisionWorld.AddVolume(&b);
    g_collisionWorld.ReleaseEmptyCells();
    CHECK(grid.cells[3] == cellB && b.cell == cellB && g_collisionWorld.m_freeCells.empty());
    // Removing what is not there: nothing. A volume removed from a cell it is not in (its cell pointer only).
    S_CollisionVolume d;
    g_collisionWorld.RemoveVolume(&d);
    d.cell = c.cell;
    g_collisionWorld.RemoveVolume(&d);
    CHECK(!d.cell && c.cell->volumes.size() == 1);

    // Ignored volumes: one, then a list; the previous ones lose the flag; excluded from the queries.
    g_collisionWorld.SetIgnoredVolume(&a, 5);
    CHECK((a.type & COLLISION_TYPE_IGNORED) && g_collisionWorld.m_ignoredUnknown == 5);
    S_CollisionVolume *list[2] = {&b, &c};
    g_collisionWorld.SetIgnoredVolumes(list, 2, 6);
    CHECK(!(a.type & COLLISION_TYPE_IGNORED) && (b.type & COLLISION_TYPE_IGNORED) && (c.type & COLLISION_TYPE_IGNORED));
    S_vector p;
    CHECK(!g_collisionWorld.RayTestDynamic(S_vector(20, 1, 30), S_vector(0, 0, 20), &p, nullptr, 0, 0));
    CHECK(g_collisionWorld.RayTestDynamic(S_vector(50, 1, 40), S_vector(0, 0, 20), &p, nullptr, 0, 0) == &a);
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    CHECK(!(b.type & COLLISION_TYPE_IGNORED) && !(c.type & COLLISION_TYPE_IGNORED) && g_collisionWorld.m_ignoredCount == 0);
    g_collisionWorld.SetIgnoredVolumes(nullptr, 3, 0);
    CHECK(g_collisionWorld.m_ignoredCount == 0);
    CHECK(g_collisionWorld.RayTestDynamic(S_vector(20, 1, 30), S_vector(0, 0, 20), &p, nullptr, 0, 0) == &c);
    // Far outside (more than a cell: the index is negative or past the end): the edge cells.
    {
        S_CollisionVolume f, g;
        f.flags = g.flags = COLLISION_SHAPE_SPHERE;
        f.center = S_vector(-40, 1, -40);
        g.center = S_vector(30, 1, 100);
        g_collisionWorld.AddVolume(&f);
        g_collisionWorld.AddVolume(&g);
        CHECK(f.cell && f.cell->x == 0 && f.cell->z == 0 && g.cell && g.cell->x == 1 && g.cell->z == 3);
        g_collisionWorld.RemoveVolume(&f);
        g_collisionWorld.RemoveVolume(&g);
        // (Released before ClearGrid: ClearGrid leaves an emptied, unreleased cell in the grid and deletes it, the
        // original 0x006eff70 likewise.)
        g_collisionWorld.ReleaseEmptyCells();
    }
    // ClearGrid: every volume out of its cell (column 0 too); the ignored ones lose the flag.
    S_CollisionVolume e;
    e.flags = COLLISION_SHAPE_SPHERE;
    e.center = S_vector(3, 1, 3);
    g_collisionWorld.AddVolume(&e);
    g_collisionWorld.SetIgnoredVolume(&b, 0);
    g_collisionWorld.ClearGrid();
    CHECK(!a.cell && !b.cell && !c.cell && !e.cell && !(b.type & COLLISION_TYPE_IGNORED) && g_collisionWorld.m_freeCells.empty() &&
          g_collisionWorld.m_emptyCells.empty());
    for (int i = 0; i < grid.cellsX * grid.cellsZ; ++i)
        CHECK(grid.cells[i] == nullptr);
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    }

    // Rays against random volumes of each shape: RayTest (no static), RayTestDynamic, my own.
    std::vector<S_TestVolume *> volumes = MakeVolumes(rng, 160, origin);
    for (S_TestVolume *tv : volumes)
        g_collisionWorld.AddVolume(&tv->v);
    std::uniform_real_distribution<float> pos(-2.0f, 66.0f), py(-1.0f, 16.0f), unit(-1.0f, 1.0f), len(1.0f, 60.0f);
    long rays = 0, hits = 0, agree = 0, dynAgree = 0, skipped = 0, queryAgree = 0, kinds[0x50] = {};
    for (int i = 0; i < 30000 / QUICK; ++i) {
        S_vector from = S_vector(pos(rng), py(rng), pos(rng)) + origin;
        S_vector dir(unit(rng), unit(rng) * 0.5f, unit(rng));
        if (i % 3 == 0) {
            const S_TestVolume &tv = *volumes[rng() % volumes.size()];
            dir = (tv.v.center + S_vector(unit(rng), unit(rng), unit(rng)) * 0.5f - from) * 1.3f;
        }
        if (dir.Magnitude() < 1e-3f)
            continue;
        if (i % 3 != 0)
            dir = dir.Normalized() * len(rng);
        const uint32_t exclude = i % 7 == 0 ? 0x200u : 0u;
        // My own: the nearest (a start inside a capsule leaves the ray out: the port's capsule test does not reject
        // its ends behind the start).
        double best = 2.0, second = 2.0, bestDyn = 2.0, secondDyn = 2.0;
        const S_TestVolume *own = nullptr, *ownDyn = nullptr;
        bool insideCapsule = false;
        for (const S_TestVolume *tv : volumes) {
            if (tv->v.type & exclude)
                continue;
            if ((tv->v.flags & COLLISION_SHAPE_MASK) == COLLISION_SHAPE_CAPSULE &&
                InsideCapsule(Mul(from, tv->inv, true), tv->v.capsuleRadius + 1e-3f, tv->v.radius + 1e-3f))
                insideCapsule = true;
            const double t = OwnVolume(*tv, from, dir, false);
            if (t >= 0.0) {
                if (t < best) {
                    second = best;
                    best = t;
                    own = tv;
                } else if (t < second) {
                    second = t;
                }
            }
            const double t2 = OwnVolume(*tv, from, dir, true);
            if (t2 >= 0.0) {
                if (t2 < bestDyn) {
                    secondDyn = bestDyn;
                    bestDyn = t2;
                    ownDyn = tv;
                } else if (t2 < secondDyn) {
                    secondDyn = t2;
                }
            }
        }
        const double L = dir.Magnitude();
        if (insideCapsule || (second < 2.0 && (second - best) * L < 2e-3) || (secondDyn < 2.0 && (secondDyn - bestDyn) * L < 2e-3) ||
            (own && (best * L < 1e-3 || (1.0 - best) * L < 1e-3))) {
            ++skipped;
            continue;
        }
        ++rays;
        S_vector p1, n1;
        S_CollisionVolume *hit = g_collisionWorld.RayTest(from, dir, &p1, &n1, exclude, COLLISION_EXCLUDE_ALL);
        bool ok = hit == (own ? &own->v : nullptr);
        if (ok && hit)
            ok = std::fabs((p1 - from).Magnitude() - best * L) < 2e-3 * std::max(1.0, best * L) && std::fabs(n1.Magnitude() - 1.0f) < 1e-3f;
        CHECK(ok);
        if (!ok && failures < 10)
            std::printf("  vol from %g %g %g dir %g %g %g: port %p (%x) at %g own %p (%x) at %g\n", from.x, from.y, from.z, dir.x, dir.y, dir.z,
                        static_cast<void *>(hit), hit ? hit->flags : 0u, hit ? (p1 - from).Magnitude() : -1.0f, static_cast<const void *>(own),
                        own ? own->v.flags : 0u, best * L);
        agree += ok;
        if (hit) {
            ++hits;
            ++kinds[hit->flags & 0x4F];
        }
        // RayTestDynamic: spheres containing the start are hit there.
        S_vector p2;
        S_CollisionVolume *dyn = g_collisionWorld.RayTestDynamic(from, dir, &p2, nullptr, exclude, 0);
        bool dok = dyn == (ownDyn ? &ownDyn->v : nullptr);
        if (dok && dyn)
            dok = std::fabs((p2 - from).Magnitude() - bestDyn * L) < 2e-3 * std::max(1.0, bestDyn * L);
        CHECK(dok);
        dynAgree += dok;
        // The segment query collects boxes, world boxes and spheres (not capsules); only the type filter applies.
        S_CollisionQuery q;
        q.from = from;
        q.dir = dir;
        q.excludeTypes = exclude;
        q.excludeFlags = COLLISION_EXCLUDE_ALL;
        const int n = g_collisionWorld.QuerySegment(q);
        std::set<const void *> got, want;
        for (int k = 0; k < n; ++k)
            got.insert(q.hits[k].volume);
        bool atEnd = false;
        for (const S_TestVolume *tv : volumes) {
            if ((tv->v.type & exclude) || (tv->v.flags & COLLISION_SHAPE_MASK) == COLLISION_SHAPE_CAPSULE)
                continue;
            const double t = OwnVolume(*tv, from, dir, false);
            if (t >= 0.0)
                want.insert(&tv->v);
            // (Boundary cases left out: an entry at the very end, or a box entered at its start.)
            if (t >= 0.0 && ((1.0 - t) * L < 2e-3 || t * L < 2e-3))
                atEnd = true;
        }
        if (!atEnd) {
            CHECK(got == want);
            queryAgree += got == want;
        }
    }
    std::printf("  volumes: rays %ld (%ld skipped), hits %ld (boxes %ld world boxes %ld spheres %ld capsules %ld), RayTest = own %ld, "
                "RayTestDynamic = own %ld, queries %ld\n",
                rays, skipped, hits, kinds[COLLISION_SHAPE_BOX], kinds[COLLISION_SHAPE_WORLD_BOX], kinds[COLLISION_SHAPE_SPHERE],
                kinds[COLLISION_SHAPE_CAPSULE & 0x4F], agree, dynAgree, queryAgree);
    CHECK(hits > rays / 5);

    // Region queries: the volume containing a point (box, world box, sphere; not capsules); the volumes whose centers
    // lie within a radius on the ground plane.
    long inside = 0;
    for (int i = 0; i < 20000 / QUICK; ++i) {
        const S_vector at = S_vector(pos(rng), py(rng), pos(rng)) + origin;
        const uint32_t exclude = i % 5 == 0 ? 0x100u : 0u;
        std::set<const S_CollisionVolume *> containing;
        bool edge = false;
        for (const S_TestVolume *tv : volumes) {
            const S_CollisionVolume &v = tv->v;
            if (v.type & exclude)
                continue;
            bool in = false;
            float margin = 1.0f;
            switch (v.flags & COLLISION_SHAPE_MASK) {
            case COLLISION_SHAPE_BOX: {
                const S_vector l = Mul(at, tv->inv, true);
                margin = std::min({l.x - v.bbox.min.x, v.bbox.max.x - l.x, l.y - v.bbox.min.y, v.bbox.max.y - l.y, l.z - v.bbox.min.z, v.bbox.max.z - l.z});
                in = margin >= 0.0f;
                break;
            }
            case COLLISION_SHAPE_WORLD_BOX:
                margin = std::min({at.x - v.bbox.min.x, v.bbox.max.x - at.x, at.y - v.bbox.min.y, v.bbox.max.y - at.y, at.z - v.bbox.min.z,
                                   v.bbox.max.z - at.z});
                in = margin >= 0.0f;
                break;
            case COLLISION_SHAPE_SPHERE:
                margin = v.radius - (v.center - at).Magnitude();
                in = margin >= 0.0f;
                break;
            default:
                break;
            }
            if (std::fabs(margin) < 1e-3f)
                edge = true;
            if (in)
                containing.insert(&v);
        }
        if (edge)
            continue;
        S_CollisionVolume *got = g_collisionWorld.GetVolumeAt(at, exclude);
        CHECK(containing.empty() ? got == nullptr : containing.count(got) == 1);
        inside += got != nullptr;
        const float radius = 1.0f + (i % 40);
        // (The list holds the last call's: emptied first.)
        static std::vector<S_CollisionVolume *> found;
        if (found.empty())
            found.push_back(nullptr);
        const int n = g_collisionWorld.GetVolumesInRadius(at, radius, exclude, found);
        std::set<const S_CollisionVolume *> want, gotSet(found.begin(), found.end());
        bool redge = false;
        for (const S_TestVolume *tv : volumes) {
            if (tv->v.type & exclude)
                continue;
            const float dx = tv->v.center.x - at.x, dz = tv->v.center.z - at.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (std::fabs(d - radius) < 1e-3f)
                redge = true;
            if (d <= radius)
                want.insert(&tv->v);
        }
        if (!redge)
            CHECK(n == static_cast<int>(found.size()) && gotSet.size() == found.size() && gotSet == want);
    }
    std::printf("  points inside a volume %ld\n", inside);
    // Starting inside a sphere volume: RayTestDynamic hits it at the start, the normal out from its center; a short
    // segment just outside, toward it, reaches it (it lies ahead: its reach is the segment and the radius).
    {
        // (Spheres of my own where no other volume is near.)
        int checked = 0, okInside = 0, okNear = 0;
        std::vector<S_TestVolume *> own;
        for (float x = 4.0f; x < 64.0f; x += 4.0f)
            for (float z = 4.0f; z < 64.0f; z += 4.0f) {
                const S_vector c = S_vector(x, 30.0f, z) + origin;    // (above the others)
                bool clear = true;
                for (const S_TestVolume *o : volumes)
                    if ((o->v.center - c).Magnitude() < o->v.radius + 5.0f)
                        clear = false;
                if (!clear || own.size() >= 6)
                    continue;
                auto *tv = new S_TestVolume();
                tv->v.flags = COLLISION_SHAPE_SPHERE;
                tv->v.center = c;
                tv->v.radius = 0.5f + 0.2f * static_cast<float>(own.size());
                g_collisionWorld.AddVolume(&tv->v);
                own.push_back(tv);
            }
        for (const S_TestVolume *tv : own) {
            ++checked;
            S_vector p, n;
            const S_vector from = tv->v.center + S_vector(0.2f, 0.1f, 0.0f);
            okInside += g_collisionWorld.RayTestDynamic(from, S_vector(3, 0, 0), &p, &n, 0, 0) == &tv->v && SameDir(p, from, 1e-4f) &&
                        SameDir(n, Norm(from - tv->v.center));
            const S_vector outside = tv->v.center + S_vector(tv->v.radius + 0.2f, 0, 0);
            okNear += g_collisionWorld.RayTest(outside, S_vector(-0.5f, 0, 0), &p, nullptr, 0, COLLISION_EXCLUDE_ALL) == &tv->v &&
                      std::fabs(p.x - (tv->v.center.x + tv->v.radius)) < 1e-3f;
        }
        CHECK(checked >= 3 && okInside == checked && okNear == checked);
        for (S_TestVolume *tv : own) {
            g_collisionWorld.RemoveVolume(&tv->v);
            delete tv;
        }
    }
    CHECK(inside > 100 / QUICK);
    for (S_TestVolume *tv : volumes)
        g_collisionWorld.RemoveVolume(&tv->v);
    g_collisionWorld.Unload(false);
    CHECK(!g_collisionWorld.HasMesh() && !g_collisionWorld.m_grid);
    // Without a level: nothing answers.
    std::vector<S_CollisionVolume *> found;
    CHECK(!g_collisionWorld.RayTest(S_vector(), S_vector(1, 0, 0), nullptr, nullptr, 0, 0) &&
          !g_collisionWorld.RayTestDynamic(S_vector(), S_vector(1, 0, 0), nullptr, nullptr, 0, 0) &&
          !g_collisionWorld.GetVolumeAt(S_vector(), 0) && g_collisionWorld.GetVolumesInRadius(S_vector(), 5.0f, 0, found) == 0);
    for (S_TestVolume *tv : volumes)
        delete tv;
}
// ============================================================ part 1e: static and dynamic together
void TestCombined()
{
    std::mt19937 rng(9001);
    S_Synth s = MakeSynth(rng, 300);
    s.withRect = false;                                 // (triangles only here)
    const std::string path = TempFile("combined.klz");
    WriteSynth(s, path);
    g_collisionWorld.Load(path.c_str(), true);
    const S_CollisionMesh &m = *g_collisionWorld.m_mesh;
    std::vector<S_TestVolume *> volumes = MakeVolumes(rng, 80);
    for (S_TestVolume *tv : volumes)
        g_collisionWorld.AddVolume(&tv->v);
    // The triangles listed in the lowest layer (RayTest2D's).
    std::set<int> lowest;
    for (int i = 0; i < m.cellsX * m.cellsZ; ++i)
        for (int k = 0; k < m.cells[i]->count; ++k) {
            const auto *t = static_cast<const S_CollisionTriangle *>(m.cells[i]->entries[k]);
            if (t >= m.triangles && t < m.triangles + m.numTriangles)
                lowest.insert(static_cast<int>(t - m.triangles));
        }
    std::uniform_real_distribution<float> pos(-2.0f, 66.0f), py(-1.0f, 16.0f), unit(-1.0f, 1.0f), len(1.0f, 60.0f);
    long rays = 0, agree = 0, agree2d = 0, volumeFirst = 0;
    for (int i = 0; i < 20000 / QUICK; ++i) {
        const S_vector from(pos(rng), py(rng), pos(rng));
        S_vector dir(unit(rng), unit(rng) * 0.5f, unit(rng));
        if (dir.Magnitude() < 1e-3f)
            continue;
        dir = dir.Normalized() * len(rng);
        const double L = dir.Magnitude();
        // My own: the nearest triangle (all / the lowest layer's), the nearest volume (as RayTest / as RayTest2D).
        double tri = 2.0, tri2 = 2.0, vol = 2.0, vol2 = 2.0, close = 2.0;
        const void *triHit = nullptr, *tri2Hit = nullptr, *volHit = nullptr, *vol2Hit = nullptr;
        bool edge = false, insideCapsule = false;
        std::vector<double> all;
        for (size_t k = 0; k < s.tris.size(); ++k) {
            double margin = 1.0;
            const double t = OwnTriangle(s.tris[k], from, dir, margin);
            if (std::fabs(margin) < 1e-4)
                edge = true;
            if (t < 0.0)
                continue;
            all.push_back(t);
            if (t < tri) {
                tri = t;
                triHit = &m.triangles[k];
            }
            if (lowest.count(static_cast<int>(k)) && t < tri2) {
                tri2 = t;
                tri2Hit = &m.triangles[k];
            }
        }
        for (const S_TestVolume *tv : volumes) {
            if ((tv->v.flags & COLLISION_SHAPE_MASK) == COLLISION_SHAPE_CAPSULE &&
                InsideCapsule(Mul(from, tv->inv, true), tv->v.capsuleRadius + 1e-3f, tv->v.radius + 1e-3f))
                insideCapsule = true;
            const double t = OwnVolume(*tv, from, dir, false), t2 = OwnVolume(*tv, from, dir, true);
            if (t >= 0.0) {
                all.push_back(t);
                if (t < vol) {
                    vol = t;
                    volHit = &tv->v;
                }
            }
            if (t2 >= 0.0) {
                all.push_back(t2);
                if (t2 < vol2) {
                    vol2 = t2;
                    vol2Hit = &tv->v;
                }
            }
        }
        std::sort(all.begin(), all.end());
        for (size_t k = 1; k < all.size(); ++k)
            if (all[k] - all[k - 1] > 0.0)
                close = std::min(close, (all[k] - all[k - 1]) * L);
        if (edge || insideCapsule || close < 2e-3 || (!all.empty() && (all[0] * L < 1e-3 || (1.0 - all.back()) * L < 1e-3)))
            continue;
        ++rays;
        const void *want = vol <= tri ? volHit : triHit;
        const double wantT = std::min(vol, tri);
        volumeFirst += vol < tri;
        S_vector p;
        S_CollisionVolume *hit = g_collisionWorld.RayTest(from, dir, &p, nullptr, 0, 0);
        bool ok = static_cast<const void *>(hit) == (wantT <= 1.0 ? want : nullptr);
        if (ok && hit)
            ok = std::fabs((p - from).Magnitude() - wantT * L) < 2e-3 * std::max(1.0, wantT * L);
        CHECK(ok);
        agree += ok;
        // RayTest2D: the volumes as RayTestDynamic, the lowest layer's static cells.
        const void *want2 = vol2 <= tri2 ? vol2Hit : tri2Hit;
        const double want2T = std::min(vol2, tri2);
        S_CollisionVolume *hit2 = g_collisionWorld.RayTest2D(from, dir, &p, nullptr, 0, 0);
        bool ok2 = static_cast<const void *>(hit2) == (want2T <= 1.0 ? want2 : nullptr);
        if (ok2 && hit2)
            ok2 = std::fabs((p - from).Magnitude() - want2T * L) < 2e-3 * std::max(1.0, want2T * L);
        CHECK(ok2);
        agree2d += ok2;
        // The filters: no volumes / no static.
        CHECK(static_cast<const void *>(g_collisionWorld.RayTest(from, dir, nullptr, nullptr, COLLISION_EXCLUDE_ALL, 0)) == (tri <= 1.0 ? triHit : nullptr));
        CHECK(static_cast<const void *>(g_collisionWorld.RayTest(from, dir, nullptr, nullptr, 0, COLLISION_EXCLUDE_ALL)) == (vol <= 1.0 ? volHit : nullptr));
    }
    std::printf("  combined: rays %ld, RayTest = own %ld (a volume first %ld), RayTest2D = own %ld\n", rays, agree, volumeFirst, agree2d);
    CHECK(rays > 15000 / QUICK && volumeFirst > 1000 / QUICK);

    // TestBoxSweep: the volumes by bounding spheres (the box's "radius" is its squared half diagonal, as in the
    // original 0x006f0ac0), world boxes by overlap; the static triangles by the distance of the sweep's point
    // (center + velocity * 20) from their planes.
    {
        const S_matrix at = Rigid(0.0f, 0.0f, S_vector(100, 100, 100));    // (outside the level: nothing)
        I3D_bbox box;
        box.min = S_vector(-0.5f, -0.5f, -0.5f);
        box.max = S_vector(0.5f, 0.5f, 0.5f);
        S_vector4 u4 = {0, 0, 0, 0};
        CHECK(!g_collisionWorld.TestBoxSweep(box, at, at, u4, S_vector(), S_vector(), 0, 0));
        // At a sphere volume's center: overlaps; volumes excluded (all types): not by it.
        const S_TestVolume *sphere = nullptr;
        for (const S_TestVolume *tv : volumes)
            if ((tv->v.flags & COLLISION_SHAPE_MASK) == COLLISION_SHAPE_SPHERE) {
                sphere = tv;
                break;
            }
        const S_matrix on = Rigid(0.0f, 0.0f, sphere->v.center);
        CHECK(g_collisionWorld.TestBoxSweep(box, on, on, u4, S_vector(), S_vector(), 0, COLLISION_EXCLUDE_ALL));
        CHECK(!g_collisionWorld.TestBoxSweep(box, on, on, u4, S_vector(), S_vector(), sphere->v.type, COLLISION_EXCLUDE_ALL) ||
              [&] {
                  // (another volume may overlap there too)
                  for (const S_TestVolume *tv : volumes)
                      if (tv != sphere && !(tv->v.type & sphere->v.type) && (tv->v.center - sphere->v.center).Magnitude() < tv->v.radius + 0.75f + 1.0f)
                          return true;
                  return false;
              }());
        // The squared radius: a box of half diagonal 0.866 reaches 0.75 + the sphere's radius; at 0.8 + r it does
        // not, though its corner would at 0.866 + r.
        const S_vector dirOut(1.0f, 0.0f, 0.0f);
        const S_matrix nearM = Rigid(0.0f, 0.0f, sphere->v.center + dirOut * (sphere->v.radius + 0.74f));
        const S_matrix farM = Rigid(0.0f, 0.0f, sphere->v.center + dirOut * (sphere->v.radius + 0.8f));
        const auto alone = [&](const S_matrix &mm) {
            for (const S_TestVolume *tv : volumes)
                if (tv != sphere && (tv->v.center - mm(3)).Magnitude() < tv->v.radius + 3.0f)
                    return false;
            return true;
        };
        if (alone(nearM) && alone(farM)) {
            CHECK(g_collisionWorld.TestBoxSweep(box, nearM, nearM, u4, S_vector(), S_vector(), 0, COLLISION_EXCLUDE_ALL));
            CHECK(!g_collisionWorld.TestBoxSweep(box, farM, farM, u4, S_vector(), S_vector(), 0, COLLISION_EXCLUDE_ALL));
        }
        // A triangle: the sweep's point on its plane within its box: overlaps; static excluded: not.
        const S_SynthTri &t = s.tris[0];
        const S_vector c = (t.v[0] + t.v[1] + t.v[2]) * (1.0f / 3.0f);
        const S_matrix onTri = Rigid(0.0f, 0.0f, c);
        CHECK(g_collisionWorld.TestBoxSweep(box, onTri, onTri, u4, S_vector(), S_vector(), COLLISION_EXCLUDE_ALL, 0));
        CHECK(!g_collisionWorld.TestBoxSweep(box, onTri, onTri, u4, S_vector(), S_vector(), COLLISION_EXCLUDE_ALL, COLLISION_FLAG_DISABLED | 0xFF) ||
              (t.flags & 0xFF) == 0);
    }
    for (S_TestVolume *tv : volumes) {
        g_collisionWorld.RemoveVolume(&tv->v);
        delete tv;
    }
    g_collisionWorld.Unload(false);

    // GetFramePrimitives (a real level): no scene, every primitive's frame is null: all five kinds asked for null.
    S_RawLevel raw;
    const std::string level = "BURMA1";
    if (ReadRaw(g_missions + "\\" + level + "\\tree.klz", raw)) {
        g_collisionWorld.Load(("missions\\" + level + "\\tree.klz").c_str(), true);
        const S_CollisionMesh &lm = *g_collisionWorld.m_mesh;
        std::vector<void *> prims;
        const int n = g_collisionWorld.GetFramePrimitives(nullptr, prims);
        CHECK(n == lm.numBoxes + lm.numAabbs + lm.numSpheres + lm.numRects + lm.numPoles && n == static_cast<int>(prims.size()) && lm.numPoles > 0);
        const I3D_frame *other = reinterpret_cast<const I3D_frame *>(0x1234);
        CHECK(g_collisionWorld.GetFramePrimitives(other, prims) == 0 && prims.empty());
        g_collisionWorld.Unload(false);
    }
    std::vector<void *> none;
    CHECK(g_collisionWorld.GetFramePrimitives(nullptr, none) == 0);
}
// ============================================================ part 1f: triangles on the scene's visuals
// The game's triangles take their vertices from visuals' meshes (first level, 0x20-byte vertices): a file whose
// frame records name a visual (flagged visual), a model frame flagged visual (refused), a mirror (no mesh used), a
// visual not flagged (its mesh used, not set to world space) and a frame the scene does not have. Degenerate
// triangles on a mesh are disabled.
void TestVisualTriangles()
{
    std::mt19937 rng(31337);
    S_Synth s;
    s.withRect = false;
    s.records = {{0u, "nonexist"}, {1u, "vis"}, {1u, "model"}, {1u, "mirror"}, {0u, "plainvis"}};
    std::vector<S_vector> visVerts, plainVerts, mirrorVerts;
    struct S_Want {
        bool enabled;
    };
    std::vector<S_Want> want;
    std::uniform_real_distribution<float> pos(4.0f, 60.0f), py(1.0f, 14.0f), unit(-1.0f, 1.0f);
    const auto add = [&](int frame, std::vector<S_vector> *verts, S_SynthTri t, bool enabled) {
        t.frame = frame;
        for (int k = 0; k < 3; ++k) {
            t.index[k] = verts ? static_cast<int>(verts->size()) : 0;
            if (verts)
                verts->push_back(t.v[k]);
        }
        s.tris.push_back(t);
        want.push_back({enabled});
    };
    for (int i = 0; i < 120; ++i) {
        const S_vector c(pos(rng), py(rng), pos(rng));
        const S_vector a = c + S_vector(unit(rng), unit(rng), unit(rng)) * 2.0f, b = c + S_vector(unit(rng), unit(rng), unit(rng)) * 2.0f,
                       d = c + S_vector(unit(rng), unit(rng), unit(rng)) * 2.0f;
        if (Cross(b - a, d - a).Magnitude() < 0.1f)
            continue;
        const S_SynthTri t = Tri(a, b, d, i % 4 == 0 ? COLLISION_TRIANGLE_TWO_SIDED : 0, rng() & 1);
        switch (i % 6) {
        case 0: add(2, nullptr, t, false); break;           // (the model frame: refused)
        case 1: add(3, &mirrorVerts, t, false); break;      // (the mirror: no mesh)
        case 2: add(0, nullptr, t, false); break;           // (not in the scene)
        case 3: add(4, &plainVerts, t, true); break;        // (a visual not flagged: used)
        default: add(1, &visVerts, t, true); break;
        }
    }
    // Degenerate on the visual: two vertices the same (scored 2), a line along x (two flat axes: 2); a flat
    // horizontal triangle (one flat axis: 1) is kept, and a tall thin one too.
    const S_vector c(30, 5, 30);
    add(1, &visVerts, Tri(c, c + S_vector(1, 0, 1), c + S_vector(1, 0, 1), 0, false), false);
    add(1, &visVerts, Tri(c, c + S_vector(1, 0, 0), c + S_vector(2, 0, 0), 0, false), false);
    add(1, &visVerts, Tri(c + S_vector(0, 2, 0), c + S_vector(1, 2, 1), c + S_vector(2, 2, -1), 0, false), true);
    add(1, &visVerts, Tri(c + S_vector(5, 0, 0), c + S_vector(5.001f, 3, 0), c + S_vector(5.002f, 0, 0.01f), 0, false), true);
    // (The degenerate ones' kinds would not matter.)
    I3D_frame *vis = SceneFrame(FRAME_VISUAL, 0x48534D4C, visVerts);
    I3D_frame *model = SceneFrame(FRAME_MODEL, 0, {});
    I3D_frame *mirror = SceneFrame(FRAME_VISUAL, I3DVISUAL_MIRROR, mirrorVerts);
    I3D_frame *plain = SceneFrame(FRAME_VISUAL, 0x48534D4C, plainVerts);
    g_sceneFrames = {{"vis", vis}, {"model", model}, {"mirror", mirror}, {"plainvis", plain}};
    g_worldSpaceSet.clear();
    const std::string path = TempFile("visuals.klz");
    WriteSynth(s, path);
    g_framesAsked = 0;
    g_collisionWorld.Load(path.c_str(), true);
    const S_CollisionMesh &m = *g_collisionWorld.m_mesh;
    CHECK(g_framesAsked == 5 && g_worldSpaceSet.size() == 2 && g_worldSpaceSet[vis] == 1 && g_worldSpaceSet[mirror] == 1);
    int enabled = 0, wantEnabled = 0;
    bool pointers = true, flags = true;
    for (size_t i = 0; i < s.tris.size(); ++i) {
        const S_CollisionTriangle &t = m.triangles[i];
        const bool on = !(t.flags & COLLISION_FLAG_DISABLED);
        enabled += on;
        wantEnabled += want[i].enabled;
        CHECK(on == want[i].enabled);
        if (!on) {
            flags &= t.flags == COLLISION_FLAG_DISABLED;
            continue;
        }
        const std::vector<float> &vv = (s.tris[i].frame == 1 ? reinterpret_cast<S_FakeMesh *>(*reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(vis) + 0x1E0))
                                                             : reinterpret_cast<S_FakeMesh *>(*reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(plain) + 0x1E0)))
                                           ->level->vertices;
        pointers &= reinterpret_cast<const float *>(t.v0) == &vv[s.tris[i].index[0] * 8] && reinterpret_cast<const float *>(t.v2) == &vv[s.tris[i].index[2] * 8];
        flags &= t.flags == s.tris[i].flags;
    }
    CHECK(pointers && flags && enabled == wantEnabled && enabled > 50);
    // Rays at each triangle's middle along its normal: the enabled ones hit there (where nothing else is in the
    // way, by my own test), the disabled ones never.
    int hitOk = 0, missOk = 0, tested = 0;
    for (size_t i = 0; i < s.tris.size(); ++i) {
        const S_SynthTri &t = s.tris[i];
        const S_vector mid = (t.v[0] + t.v[1] + t.v[2]) * (1.0f / 3.0f);
        const S_vector from = mid - t.normal * 0.05f, dir = t.normal * 0.1f;
        bool clear = true;
        for (size_t k = 0; k < s.tris.size(); ++k) {
            double mg;
            S_SynthTri both = s.tris[k];
            both.flags |= COLLISION_TRIANGLE_TWO_SIDED;
            if (k != i && want[k].enabled && OwnTriangle(both, from, dir, mg) >= 0.0)
                clear = false;
        }
        double mg;
        if (!clear || OwnTriangle(t, from, dir, mg) < 0.0 || std::fabs(mg) < 1e-3)
            continue;
        ++tested;
        S_vector p;
        S_CollisionVolume *hit = g_collisionWorld.RayTest(from, dir, &p, nullptr, COLLISION_EXCLUDE_ALL, 0);
        if (want[i].enabled)
            hitOk += hit == reinterpret_cast<const S_CollisionVolume *>(&m.triangles[i]) && (p - mid).Magnitude() < 1e-3f;
        else
            missOk += hit == nullptr;
    }
    int enabledTested = 0;
    for (size_t i = 0; i < s.tris.size(); ++i)
        enabledTested += want[i].enabled;
    std::printf("  visual triangles: %d enabled of %zu; rays at %d: %d hit, %d disabled missed\n", enabled, s.tris.size(), tested, hitOk, missOk);
    CHECK(tested > 80 && hitOk + missOk == tested);
    g_collisionWorld.Unload(false);
    g_sceneFrames.clear();
}

// ============================================================ part 1g: the rest
void TestCollisionRest()
{
    // The wrappers: the world's queries with the static geometry's default filter.
    std::mt19937 rng(5);
    S_Synth s = MakeSynth(rng, 100);
    const std::string path = TempFile("rest.klz");
    WriteSynth(s, path);
    LoadLevelCollision(path.c_str());
    CHECK(g_collisionWorld.HasMesh() && g_levelCollisionLoaded && g_collisionWorld.m_grid);
    std::uniform_real_distribution<float> pos(0.0f, 64.0f), py(0.0f, 16.0f), unit(-1.0f, 1.0f);
    int same = 0, same2 = 0, n = 0;
    for (int i = 0; i < 2000; ++i) {
        const S_vector from(pos(rng), py(rng), pos(rng)), dir = S_vector(unit(rng), unit(rng), unit(rng)) * 30.0f;
        S_vector p1, n1, p2, n2;
        ++n;
        same += CollisionRayTest(from, dir, &p1, &n1, 0, 0) == g_collisionWorld.RayTest(from, dir, &p2, &n2, 0, 0);
        same2 += CollisionRayTest2D(from, dir, &p1, &n1, 0, 0) == g_collisionWorld.RayTest2D(from, dir, &p2, &n2, 0, 0);
    }
    CHECK(same == n && same2 == n);
    // RayTestStatic with the bullets' flag: half of the time (rand() & 8) the flag is dropped again. A primitive
    // marked with it is then hit or not by the draw.
    S_CollisionMesh &m = *g_collisionWorld.m_mesh;
    S_CollisionRect &rect = m.rects[0];
    rect.flags |= COLLISION_FLAG_BULLETS_PASS;
    const S_vector from(30, 10, 30), down(0, -20, 0.001f);
    int hits = 0, wantHits = 0;
    for (unsigned seed = 1; seed < 200; ++seed) {
        std::srand(seed);
        const bool drop = std::rand() & 8;
        std::srand(seed);
        S_CollisionVolume *hit = g_collisionWorld.RayTestStatic(from, down, nullptr, nullptr, COLLISION_FLAG_BULLETS_PASS);
        // (Something else may lie above the rectangle: the rectangle itself only when nothing nearer.)
        hits += hit == reinterpret_cast<S_CollisionVolume *>(&rect);
        wantHits += drop;
    }
    std::printf("  bullets' flag: the rectangle hit %d of 199 (the draw drops the flag %d times)\n", hits, wantHits);
    CHECK(hits <= wantHits && hits > 0 && wantHits > 50 && wantHits < 150);
    // Without the flag asked: never dropped, the flag only matters when asked (0: always tested).
    rect.flags &= ~COLLISION_FLAG_BULLETS_PASS;
    // QuerySegment asking for volumes without a grid: nothing (the static part not reached either).
    S_CollisionGrid *grid = g_collisionWorld.m_grid;
    g_collisionWorld.m_grid = nullptr;
    S_CollisionQuery q;
    q.from = S_vector(30, 10, 30);
    q.dir = S_vector(0.1f, -20, 0);
    q.excludeTypes = 0;
    q.excludeFlags = 0;
    CHECK(g_collisionWorld.QuerySegment(q) == 0);
    g_collisionWorld.m_grid = grid;
    g_collisionWorld.Unload(false);
    q.excludeTypes = COLLISION_EXCLUDE_ALL;
    CHECK(g_collisionWorld.QuerySegment(q) == 0 && !g_collisionWorld.RayTestWater(q.from, q.dir, nullptr, nullptr, 0) &&
          !g_collisionWorld.RayTest2D(q.from, q.dir, nullptr, nullptr, 0, 0) && !g_collisionWorld.RayTestStatic(q.from, q.dir, nullptr, nullptr, 0));
    float lo, hi;
    g_collisionWorld.GetWaterHeightRange(lo, hi);
    CHECK(lo > hi);

    // TestBoxSweep against a level's primitives (boxes and world boxes by their world box, spheres, rectangles,
    // poles): a box at a primitive's middle overlaps; one far away does not.
    S_RawLevel raw;
    if (ReadRaw(g_missions + "\\BURMA1\\tree.klz", raw)) {
        g_collisionWorld.Load("missions\\BURMA1\\tree.klz", true);
        const S_CollisionMesh &lm = *g_collisionWorld.m_mesh;
        I3D_bbox box;
        box.min = S_vector(-0.1f, -0.1f, -0.1f);
        box.max = S_vector(0.1f, 0.1f, 0.1f);
        S_vector4 u4 = {0, 0, 0, 0};
        int ok = 0, total = 0;
        const auto at = [&](const S_vector &p) {
            const S_matrix mm = Rigid(0.0f, 0.0f, p);
            const bool r = g_collisionWorld.TestBoxSweep(box, mm, mm, u4, S_vector(), S_vector(), COLLISION_EXCLUDE_ALL, 0);
            return r;
        };
        for (int i = 0; i < lm.numBoxes; i += 7, ++total)
            ok += at((lm.boxes[i].worldMin + lm.boxes[i].worldMax) * 0.5f);
        for (int i = 0; i < lm.numAabbs; ++i, ++total)
            ok += at((lm.aabbs[i].min + lm.aabbs[i].max) * 0.5f);
        for (int i = 0; i < lm.numRects; ++i, ++total)
            ok += at(Mul(S_vector((lm.rects[i].min.x + lm.rects[i].max.x) * 0.5f, 0.0f, (lm.rects[i].min.z + lm.rects[i].max.z) * 0.5f),
                         lm.rects[i].matrix, true));
        for (int i = 0; i < lm.numPoles; ++i, ++total) {
            // (A pole is infinite in y: at the middle of the level's height.)
            ok += at(S_vector(lm.poles[i].x, lm.minY + 1.0f, lm.poles[i].z));
        }
        std::printf("  box sweeps at %d primitives: %d overlap\n", total, ok);
        CHECK(ok == total && total > 100);
        CHECK(!at(S_vector(lm.maxX + 500.0f, 0, lm.maxZ + 500.0f)));
        g_collisionWorld.Unload(false);
    }
}
// ============================================================ part 1h: what the first mutation run left untested

// My own world of shapes for the normals: each gives the entry fraction and the normal the queries hand back
// (GetCollisionRayHit: minus the recorded one, normalised). `startInside`: RayTestDynamic's rule for spheres.
struct S_OwnShape {
    enum { TRI, RECT, CAPSULE, CONVEX, BOX, WORLD_BOX, SPHERE } kind;
    const void *object;
    const S_SynthTri *tri = nullptr;
    S_matrix matrix, inv;                               // (box, capsule, rect, convex: object to world / back)
    S_vector lo, hi;                                    // (boxes; a convex is a box in its space)
    float r = 0, h = 0;                                 // (capsule; sphere: r)
    S_vector center;
    bool worldSpace = false;                            // (convex: its visual in world space)
};
// The entry fraction and the normal handed back, or -1.
double OwnShapeHit(const S_OwnShape &s, const S_vector &from, const S_vector &dir, bool startInside, S_vector &normal)
{
    switch (s.kind) {
    case S_OwnShape::TRI: {
        double margin;
        const double t = OwnTriangle(*s.tri, from, dir, margin);
        normal = Norm(-s.tri->normal);
        return t;
    }
    case S_OwnShape::RECT: {
        const S_vector lf = Mul(from, s.inv, true), ld = Mul(dir, s.inv, false);
        if (std::fabs(ld.y) <= 1e-8f)
            return -1;
        const double t = -lf.y / ld.y;
        if (!(t > 0.0 && t < 1.0))
            return -1;
        const double x = lf.x + ld.x * t, z = lf.z + ld.z * t;
        if (!(s.lo.x <= x && x <= s.hi.x && s.lo.z <= z && z <= s.hi.z))
            return -1;
        normal = Norm(-Mul(S_vector(0, ld.y > 0 ? 1.0f : -1.0f, 0), s.matrix, false));
        return t;
    }
    case S_OwnShape::CAPSULE: {
        const S_vector lf = Mul(from, s.inv, true), ld = Mul(dir, s.inv, false);
        const double t = OwnCapsule(lf, ld, s.r, s.h);
        if (t < 0.0)
            return -1;
        // (The capsule's test records the normal out of the surface: handed back, it points in.)
        const S_vector p = lf + ld * static_cast<float>(t);
        const float cap = s.h - s.r;
        const float cy = std::max(-cap, std::min(cap, p.y));
        normal = Norm(-Mul(S_vector(p.x, p.y - cy, p.z), s.matrix, false));
        return t;
    }
    case S_OwnShape::CONVEX:
    case S_OwnShape::BOX:
    case S_OwnShape::WORLD_BOX: {
        const bool world = s.kind == S_OwnShape::WORLD_BOX || (s.kind == S_OwnShape::CONVEX && s.worldSpace);
        const S_vector lf = world ? from : Mul(from, s.inv, true), ld = world ? dir : Mul(dir, s.inv, false);
        // A convex hull entered at its start hits there (its face: unknown, not compared).
        if (s.kind == S_OwnShape::CONVEX && lf.x >= s.lo.x && lf.x <= s.hi.x && lf.y >= s.lo.y && lf.y <= s.hi.y && lf.z >= s.lo.z && lf.z <= s.hi.z) {
            normal = S_vector(0, 0, 0);
            return 0.0;
        }
        const double t = SlabEntry(lf, ld, s.lo, s.hi);
        if (t < 0.0)
            return -1;
        // The face entered: the axis whose entry is the fraction.
        const float f[3] = {lf.x, lf.y, lf.z}, d[3] = {ld.x, ld.y, ld.z}, l[3] = {s.lo.x, s.lo.y, s.lo.z}, hh[3] = {s.hi.x, s.hi.y, s.hi.z};
        int axis = -1;
        for (int a = 0; a < 3; ++a)
            if (std::fabs(d[a]) > 1e-8f && std::fabs(((d[a] > 0 ? l[a] : hh[a]) - f[a]) / d[a] - t) < 1e-6)
                axis = a;
        S_vector n(0, 0, 0);
        if (axis == 0)
            n.x = d[0] > 0 ? -1.0f : 1.0f;
        else if (axis == 1)
            n.y = d[1] > 0 ? -1.0f : 1.0f;
        else if (axis == 2)
            n.z = d[2] > 0 ? -1.0f : 1.0f;
        normal = world ? n : Norm(Mul(n, s.matrix, false));
        return t;
    }
    case S_OwnShape::SPHERE: {
        if (startInside && (s.center - from).Square() < s.r * s.r) {
            normal = Norm(from - s.center);
            return 0.0;
        }
        const double cx = s.center.x - from.x, cy = s.center.y - from.y, cz = s.center.z - from.z;
        const double a = dir.Dot(dir), b = cx * dir.x + cy * dir.y + cz * dir.z, c = cx * cx + cy * cy + cz * cz - s.r * s.r;
        const double disc = b * b - a * c;
        if (disc < 0.0)
            return -1;
        const double t = (b - std::sqrt(disc)) / a;
        if (!(t >= 0.0 && t < 1.0))
            return -1;
        normal = Norm(from + dir * static_cast<float>(t) - s.center);
        return t;
    }
    }
    return -1;
}

// A convex hull: the six inward planes of a box in its visual's space.
void BoxPlanes(const S_vector &lo, const S_vector &hi, S_plane *planes)
{
    planes[0] = {S_vector(1, 0, 0), -lo.x};
    planes[1] = {S_vector(-1, 0, 0), hi.x};
    planes[2] = {S_vector(0, 1, 0), -lo.y};
    planes[3] = {S_vector(0, -1, 0), hi.y};
    planes[4] = {S_vector(0, 0, 1), -lo.z};
    planes[5] = {S_vector(0, 0, -1), hi.z};
}
// A visual for convex hulls: world space, or at `m` (its world matrix; the inverse is cached on first use).
I3D_frame *HullVisual(bool worldSpace, const S_matrix &m)
{
    I3D_frame *f = SceneFrame(FRAME_VISUAL, 0x48534D4C, {});
    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(f) + 0x1D0) = worldSpace ? VISFLAGS_WORLD_SPACE : 0;
    f->m_worldMatrix = m;
    f->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
    return f;
}

void TestNormalsAndShapes()
{
    std::mt19937 rng(2718);
    S_Synth s = MakeSynth(rng, 150);
    // A tilted plain rectangle (not water).
    s.rectFlags = COLLISION_PRIM_RECT;
    s.rectMatrix = Rigid(0.5f, 0.35f, S_vector(30, 6, 30));
    // Capsule primitives and convex primitives (two visuals: world space, and turned).
    std::uniform_real_distribution<float> pos(6.0f, 58.0f), py(2.0f, 12.0f), ang(0.0f, 6.28f);
    std::vector<S_OwnShape> shapes;
    for (int i = 0; i < 6; ++i) {
        S_CollisionCapsule c;
        std::memset(&c, 0, sizeof c);
        c.flags = COLLISION_PRIM_CAPSULE;
        c.radius = 0.6f + 0.1f * i;
        c.halfHeight = c.radius + 1.0f + 0.3f * i;
        c.matrix = Rigid(ang(rng), i < 3 ? 0.0f : ang(rng) * 0.2f, S_vector(pos(rng), py(rng), pos(rng)));   // (three upright)
        c.invMatrix = InverseRigid(c.matrix);
        s.capsules.push_back(c);
        s.primHeights.push_back({c.matrix.m[3][1] - c.halfHeight, c.matrix.m[3][1] + c.halfHeight});
    }
    const S_matrix hullMatrix = Rigid(0.7f, 0.2f, S_vector(20, 6, 44));
    I3D_frame *hullWorld = HullVisual(true, Rigid(0, 0, S_vector()));
    I3D_frame *hullTurned = HullVisual(false, hullMatrix);
    g_sceneFrames = {{"hullworld", hullWorld}, {"hullturned", hullTurned}};
    s.records = {{0u, "nonexist"}, {1u, "hullworld"}, {1u, "hullturned"}};
    const S_vector hullLo[2] = {S_vector(40, 3, 10), S_vector(-2, -1.5f, -1)}, hullHi[2] = {S_vector(44, 6, 13), S_vector(2, 1.5f, 1)};
    for (int i = 0; i < 2; ++i) {
        S_CollisionConvexPrim c;
        std::memset(&c, 0, sizeof c);
        c.flags = COLLISION_PRIM_CONVEX;
        c.frame = reinterpret_cast<I3D_frame *>(static_cast<uintptr_t>(i + 1));
        c.numPlanes = 6;
        BoxPlanes(hullLo[i], hullHi[i], c.planes);
        s.convexes.push_back(c);
    }
    s.primHeights.push_back({3.0f, 6.0f});
    s.primHeights.push_back({2.0f, 10.0f});
    const std::string path = TempFile("shapes.klz");
    WriteSynth(s, path);
    g_collisionWorld.Load(path.c_str(), true);
    const S_CollisionMesh &m = *g_collisionWorld.m_mesh;
    CHECK(m.numCapsules == 6 && m.numConvexes == 2 && m.convexes[0].frame == hullWorld && m.convexes[1].frame == hullTurned);
    // My shapes: the triangles, the rectangle, the capsules, the hulls.
    for (size_t i = 0; i < s.tris.size(); ++i) {
        S_OwnShape o{S_OwnShape::TRI, &m.triangles[i]};
        o.tri = &s.tris[i];
        shapes.push_back(o);
    }
    {
        S_OwnShape o{S_OwnShape::RECT, m.rects};
        o.matrix = s.rectMatrix;
        o.inv = InverseRigid(s.rectMatrix);
        o.lo = s.rectMin;
        o.hi = s.rectMax;
        shapes.push_back(o);
    }
    for (int i = 0; i < 6; ++i) {
        S_OwnShape o{S_OwnShape::CAPSULE, &m.capsules[i]};
        o.matrix = s.capsules[i].matrix;
        o.inv = s.capsules[i].invMatrix;
        o.r = s.capsules[i].radius;
        o.h = s.capsules[i].halfHeight;
        shapes.push_back(o);
    }
    for (int i = 0; i < 2; ++i) {
        S_OwnShape o{S_OwnShape::CONVEX, &m.convexes[i]};
        o.worldSpace = i == 0;
        o.matrix = i == 0 ? Rigid(0, 0, S_vector()) : hullMatrix;
        o.inv = InverseRigid(o.matrix);
        o.lo = hullLo[i];
        o.hi = hullHi[i];
        shapes.push_back(o);
    }
    // Volumes: boxes, world boxes, spheres, capsules and convex hulls (a world-space visual and a turned one).
    std::vector<S_TestVolume *> volumes = MakeVolumes(rng, 60);
    static S_CollisionConvexVolume hulls[2];
    const S_matrix volHullMatrix = Rigid(1.1f, -0.15f, S_vector(50, 8, 50));
    I3D_frame *volHullTurned = HullVisual(false, volHullMatrix);
    const S_vector vhLo[2] = {S_vector(8, 9, 40), S_vector(-1.5f, -1, -2)}, vhHi[2] = {S_vector(11, 12, 43), S_vector(1.5f, 1, 2)};
    for (int i = 0; i < 2; ++i) {
        S_CollisionConvexVolume &h = hulls[i];
        h.volume.flags = COLLISION_SHAPE_CONVEX;
        h.volume.center = i == 0 ? (vhLo[0] + vhHi[0]) * 0.5f : volHullMatrix(3);
        h.volume.radius = 4.0f;
        h.numPlanes = 6;
        h.visual = static_cast<I3D_visual *>(i == 0 ? hullWorld : volHullTurned);
        BoxPlanes(vhLo[i], vhHi[i], h.planes);
        g_collisionWorld.AddVolume(&h.volume);
        S_OwnShape o{S_OwnShape::CONVEX, &h.volume};
        o.worldSpace = i == 0;
        o.matrix = i == 0 ? Rigid(0, 0, S_vector()) : volHullMatrix;
        o.inv = InverseRigid(o.matrix);
        o.lo = vhLo[i];
        o.hi = vhHi[i];
        shapes.push_back(o);
    }
    for (S_TestVolume *tv : volumes) {
        g_collisionWorld.AddVolume(&tv->v);
        const uint32_t shape = tv->v.flags & COLLISION_SHAPE_MASK;
        S_OwnShape o{shape == COLLISION_SHAPE_BOX ? S_OwnShape::BOX : shape == COLLISION_SHAPE_WORLD_BOX ? S_OwnShape::WORLD_BOX
                     : shape == COLLISION_SHAPE_SPHERE ? S_OwnShape::SPHERE : S_OwnShape::CAPSULE, &tv->v};
        o.matrix = tv->matrix;
        o.inv = tv->inv;
        o.lo = tv->v.bbox.min;
        o.hi = tv->v.bbox.max;
        o.center = tv->v.center;
        o.r = shape == COLLISION_SHAPE_SPHERE ? tv->v.radius : tv->v.capsuleRadius;
        o.h = tv->v.radius;
        shapes.push_back(o);
    }
    const size_t staticCount = s.tris.size() + 1 + 6 + 2;
    // Rays: some aimed at the shapes, some vertical (the brute force with volumes), some straight down capsules.
    std::uniform_real_distribution<float> ux(-2.0f, 66.0f), uy(-1.0f, 16.0f), unit(-1.0f, 1.0f), len(1.0f, 50.0f);
    long rays = 0, ok = 0, normalsOk = 0, dynOk = 0, queryOk = 0, kinds[8] = {}, vertical = 0;
    for (int i = 0; i < 30000 / QUICK; ++i) {
        S_vector from(ux(rng), uy(rng), ux(rng));
        S_vector dir(unit(rng), unit(rng), unit(rng));
        const S_OwnShape &target = shapes[rng() % shapes.size()];
        S_vector aim;
        switch (target.kind) {
        case S_OwnShape::TRI: aim = (target.tri->v[0] + target.tri->v[1] + target.tri->v[2]) * (1.0f / 3.0f); break;
        case S_OwnShape::SPHERE: aim = target.center; break;
        case S_OwnShape::WORLD_BOX: aim = (target.lo + target.hi) * 0.5f; break;
        case S_OwnShape::CONVEX: aim = target.worldSpace ? (target.lo + target.hi) * 0.5f : target.matrix(3); break;
        default: aim = target.matrix(3); break;
        }
        if (i % 4 != 0)
            dir = (aim + S_vector(unit(rng), unit(rng), unit(rng)) * 0.7f - from) * 1.4f;
        if (i % 9 == 0) {
            // Straight down (or up) through the target.
            from = S_vector(aim.x + unit(rng) * 0.4f, aim.y + (i % 2 ? 6.0f : -6.0f), aim.z + unit(rng) * 0.4f);
            dir = S_vector(0, i % 2 ? -12.0f : 12.0f, 0);
            ++vertical;
        }
        if (dir.Magnitude() < 1e-3f)
            continue;
        if (i % 4 == 0)
            dir = dir.Normalized() * len(rng);
        const double L = dir.Magnitude();
        // My own nearest: all shapes (RayTest), and with the dynamic rule (RayTestDynamic: volumes only).
        double best = 2.0, close = 2.0, bestDyn = 2.0;
        const S_OwnShape *own = nullptr, *ownDyn = nullptr;
        S_vector ownN, ownNDyn;
        std::vector<double> ts;
        bool edge = false, inCapsule = false;
        for (size_t k = 0; k < shapes.size(); ++k) {
            const S_OwnShape &sh = shapes[k];
            if (sh.kind == S_OwnShape::TRI) {
                double margin;
                OwnTriangle(*sh.tri, from, dir, margin);
                edge |= std::fabs(margin) < 1e-4;
            }
            if (sh.kind == S_OwnShape::CAPSULE && InsideCapsule(Mul(from, sh.inv, true), sh.r + 1e-3f, sh.h + 1e-3f))
                inCapsule = true;
            S_vector n;
            const double t = OwnShapeHit(sh, from, dir, false, n);
            if (t >= 0.0) {
                ts.push_back(t);
                if (t < best) {
                    best = t;
                    own = &sh;
                    ownN = n;
                }
            }
            if (k >= staticCount) {
                S_vector n2;
                const double t2 = OwnShapeHit(sh, from, dir, true, n2);
                if (t2 >= 0.0) {
                    if (t2 != t)
                        ts.push_back(t2);
                    if (t2 < bestDyn) {
                        bestDyn = t2;
                        ownDyn = &sh;
                        ownNDyn = n2;
                    }
                }
            }
        }
        std::sort(ts.begin(), ts.end());
        for (size_t k = 1; k < ts.size(); ++k)
            close = std::min(close, (ts[k] - ts[k - 1]) * L);
        if (edge || inCapsule || close < 2e-3 || (!ts.empty() && (ts[0] * L < 1e-3 || (1.0 - ts.back()) * L < 1e-3)))
            continue;
        ++rays;
        S_vector p, n;
        S_CollisionVolume *hit = g_collisionWorld.RayTest(from, dir, &p, &n, 0, 0);
        const bool same = static_cast<const void *>(hit) == (own ? own->object : nullptr) &&
                          (!hit || std::fabs((p - from).Magnitude() - best * L) < 2e-3 * std::max(1.0, best * L));
        CHECK(same);
        ok += same;
        if (same && hit) {
            ++kinds[own->kind];
            // (Curved surfaces: the place's rounding on long rays turns the normal by up to a few thousandths.)
            const float tolerance = own->kind == S_OwnShape::CAPSULE || own->kind == S_OwnShape::SPHERE ? 1e-2f : 2e-3f;
            const bool nOk = ownN.Magnitude() == 0.0f || SameDir(n, ownN, tolerance);
            CHECK(nOk);
            normalsOk += nOk;
        }
        // The dynamic test (volumes only; spheres around the start hit there).
        S_vector pd, nd;
        S_CollisionVolume *dyn = g_collisionWorld.RayTestDynamic(from, dir, &pd, &nd, 0, 0);
        bool dOk = static_cast<const void *>(dyn) == (ownDyn ? ownDyn->object : nullptr);
        if (dOk && dyn)
            dOk = std::fabs((pd - from).Magnitude() - bestDyn * L) < 2e-3 * std::max(1.0, bestDyn * L) && (ownNDyn.Magnitude() == 0.0f || SameDir(nd, ownNDyn, 1e-2f));
        CHECK(dOk);
        dynOk += dOk;
        // The segment query: every hit's place on the segment; the static ones' normals out of the surface (as
        // the ray tests'), the sphere volumes' into them (capsules and hulls are not collected); twice in a row the
        // same (the list emptied first).
        S_CollisionQuery q;
        q.from = from;
        q.dir = dir;
        q.excludeTypes = 0;
        q.excludeFlags = 0;
        const int n1 = g_collisionWorld.QuerySegment(q);
        const int n2 = g_collisionWorld.QuerySegment(q);
        bool qOk = n1 == n2;
        for (int k = 0; k < n2; ++k) {
            const S_CollisionHit &hk = q.hits[k];
            qOk &= SameDir(hk.pos, from + dir * hk.distance, 1e-3f);
            for (const S_OwnShape &sh : shapes)
                if (sh.object == hk.volume && sh.kind == S_OwnShape::SPHERE)
                    qOk &= SameDir(hk.normal, Norm(sh.center - hk.pos), 1e-2f);
            for (const S_OwnShape &sh : shapes)
                if (sh.object == hk.volume && (sh.kind == S_OwnShape::TRI || sh.kind == S_OwnShape::RECT)) {
                    S_vector on;
                    OwnShapeHit(sh, from, dir, false, on);
                    // (A two-sided triangle crossed from behind: its stored normal along the ray; compared as such.)
                    qOk &= SameDir(hk.normal, on) || (sh.kind == S_OwnShape::TRI && (sh.tri->flags & COLLISION_TRIANGLE_TWO_SIDED));
                }
        }
        // (Reused far away: the old hits gone.)
        q.from = S_vector(-500, -500, -500);
        q.dir = S_vector(1, 0, 1);
        qOk &= g_collisionWorld.QuerySegment(q) == 0 && q.hits.empty();
        CHECK(qOk);
        queryOk += qOk;
    }
    std::printf("  shapes: rays %ld (%ld vertical), RayTest = own %ld (triangles %ld rect %ld capsules %ld hulls %ld boxes %ld world boxes %ld "
                "spheres %ld), normals %ld, RayTestDynamic %ld, queries %ld\n",
                rays, vertical, ok, kinds[S_OwnShape::TRI], kinds[S_OwnShape::RECT], kinds[S_OwnShape::CAPSULE], kinds[S_OwnShape::CONVEX],
                kinds[S_OwnShape::BOX], kinds[S_OwnShape::WORLD_BOX], kinds[S_OwnShape::SPHERE], normalsOk, dynOk, queryOk);
    CHECK(kinds[S_OwnShape::CAPSULE] > 20 / QUICK && kinds[S_OwnShape::CONVEX] > 20 / QUICK && kinds[S_OwnShape::RECT] > 10 / QUICK);
    // Starting inside a hull: hit at the start, the normal minus its first plane's (static: world space; volume: turned).
    {
        S_vector p, n;
        const S_vector inside = (hullLo[0] + hullHi[0]) * 0.5f;
        CHECK(g_collisionWorld.RayTest(inside, S_vector(0.3f, 0.2f, 0.1f), &p, &n, COLLISION_EXCLUDE_ALL, 0) == reinterpret_cast<S_CollisionVolume *>(&m.convexes[0]) &&
              SameDir(p, inside, 1e-4f) && SameDir(n, S_vector(-1, 0, 0)));
        const S_vector inside2 = volHullMatrix(3);
        CHECK(g_collisionWorld.RayTestDynamic(inside2, S_vector(0.3f, 0.2f, 0.1f), &p, &n, 0, 0) == &hulls[1].volume && SameDir(p, inside2, 1e-4f) &&
              SameDir(n, -Mul(S_vector(1, 0, 0), volHullMatrix, false)));
    }
    // A convex hull without a visual is never hit.
    hulls[0].visual = nullptr;
    const S_vector into = (vhLo[0] + vhHi[0]) * 0.5f;
    CHECK(g_collisionWorld.RayTestDynamic(into + S_vector(0, 5, 0), S_vector(0, -5, 0.01f), nullptr, nullptr, 0, 0) != &hulls[0].volume);
    for (S_TestVolume *tv : volumes) {
        g_collisionWorld.RemoveVolume(&tv->v);
        delete tv;
    }
    g_collisionWorld.RemoveVolume(&hulls[0].volume);
    g_collisionWorld.RemoveVolume(&hulls[1].volume);
    g_collisionWorld.Unload(false);
    g_sceneFrames.clear();
}

// TestBoxSweep against my own reading of its rules (the original's, 0x006f0ac0): the box's corners at both ends
// bound the sweep; its center at the end, the squared distance to corner 7 ((min.x, min.y, max.z)) as its
// "radius", the point center + velocity * 20 and its squared distance to corner 7 for the triangles.
struct S_OwnSweep {
    S_vector min, max, center, point;
    float radius, planeSq;
};
S_OwnSweep OwnSweep(const I3D_bbox &b, const S_matrix &from, const S_matrix &to, const S_vector &velocity)
{
    S_OwnSweep q;
    q.min = S_vector(1e30f, 1e30f, 1e30f);
    q.max = S_vector(-1e30f, -1e30f, -1e30f);
    for (int k = 0; k < 8; ++k) {
        const S_vector c(k & 4 ? b.min.x : b.max.x, k & 2 ? b.min.y : b.max.y, k & 1 ? b.max.z : b.min.z);
        for (const S_matrix *m : {&from, &to}) {
            const S_vector w = Mul(c, *m, true);
            q.min = S_vector(std::min(q.min.x, w.x), std::min(q.min.y, w.y), std::min(q.min.z, w.z));
            q.max = S_vector(std::max(q.max.x, w.x), std::max(q.max.y, w.y), std::max(q.max.z, w.z));
        }
    }
    const S_vector corner7 = Mul(S_vector(b.min.x, b.min.y, b.max.z), to, true);
    q.center = Mul((b.min + b.max) * 0.5f, to, true);
    q.radius = (q.center - corner7).Square();
    q.point = velocity * 20.0f + q.center;
    q.planeSq = (q.point - corner7).Square();
    return q;
}
bool OwnSweepStatic(const S_CollisionMesh &m, const S_OwnSweep &q, uint32_t excludeFlags)
{
    const int x0 = std::max(static_cast<int>((q.min.x - m.minX) / m.cellSizeX), 0), x1 = std::min(static_cast<int>((q.max.x - m.minX) / m.cellSizeX + 1.0f), m.cellsX);
    const int z0 = std::max(static_cast<int>((q.min.z - m.minZ) / m.cellSizeZ), 0), z1 = std::min(static_cast<int>((q.max.z - m.minZ) / m.cellSizeZ + 1.0f), m.cellsZ);
    const int y0 = std::max(static_cast<int>((q.min.y - m.minY) / m.cellSizeY), 0), y1 = std::min(static_cast<int>((q.max.y - m.minY) / m.cellSizeY + 1.0f), m.cellsY);
    excludeFlags |= COLLISION_FLAG_DISABLED;
    for (int z = z0; z < z1; ++z)
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const S_CollisionMeshCell &cell = GetMeshCell(m, x, y, z);
                if (!(q.min.y < cell.maxY))
                    continue;
                for (int i = 0; i < cell.count; ++i) {
                    const void *e = cell.entries[i];
                    const uint32_t flags = *static_cast<const uint32_t *>(e);
                    if (flags & excludeFlags)
                        continue;
                    switch (flags & COLLISION_SHAPE_MASK) {
                    case COLLISION_PRIM_BOX:
                    case COLLISION_PRIM_AABB: {
                        const S_vector lo = flags & 1 ? static_cast<const S_CollisionAabb *>(e)->min : static_cast<const S_CollisionBox *>(e)->worldMin;
                        const S_vector hi = flags & 1 ? static_cast<const S_CollisionAabb *>(e)->max : static_cast<const S_CollisionBox *>(e)->worldMax;
                        if (lo.x < q.max.x && hi.x > q.min.x && lo.y < q.max.y && hi.y > q.min.y && lo.z < q.max.z && hi.z > q.min.z)
                            return true;
                        continue;
                    }
                    case COLLISION_PRIM_SPHERE: {
                        const auto *sp = static_cast<const S_CollisionSphere *>(e);
                        const float r = q.radius + sp->radius;
                        if (r * r > (ToVector(sp->center) - q.center).Square())
                            return true;
                        continue;
                    }
                    case COLLISION_PRIM_RECT: {
                        const auto *r = static_cast<const S_CollisionRect *>(e);
                        const S_vector l = Mul(q.center, r->invMatrix, true);
                        if (l.x + q.radius > r->min.x && l.x - q.radius < r->max.x && l.z + q.radius > r->min.z && l.z - q.radius < r->max.z &&
                            std::fabs(l.y) < q.radius)
                            return true;
                        continue;
                    }
                    case COLLISION_PRIM_POLE: {
                        const auto *p = static_cast<const S_CollisionPole *>(e);
                        const float r = q.radius + p->radius, dx = p->x - q.center.x, dz = p->z - q.center.z;
                        if (r * r > dx * dx + dz * dz)
                            return true;
                        continue;
                    }
                    default:
                        if (flags & (COLLISION_FLAG_PRIMITIVE | COLLISION_FLAG_DISABLED))
                            continue;
                        break;
                    }
                    const auto *t = static_cast<const S_CollisionTriangle *>(e);
                    const float lx = std::min({t->v0->x, t->v1->x, t->v2->x}), hx = std::max({t->v0->x, t->v1->x, t->v2->x});
                    const float ly = std::min({t->v0->y, t->v1->y, t->v2->y}), hy = std::max({t->v0->y, t->v1->y, t->v2->y});
                    const float lz = std::min({t->v0->z, t->v1->z, t->v2->z}), hz = std::max({t->v0->z, t->v1->z, t->v2->z});
                    if (hx < q.min.x || lx > q.max.x || ly > q.max.y || hy < q.min.y || lz > q.max.z || hz < q.min.z)
                        continue;
                    const float dd = (q.point - ToVector(*t->v0)).Dot(t->normal);
                    if (dd * dd <= q.planeSq)
                        return true;
                }
            }
    return false;
}
void TestBoxSweeps()
{
    std::mt19937 rng(161);
    // A level of my own with triangles and volumes (boxes and spheres by bounding spheres, world boxes by
    // overlap, hulls always), and a real level's primitives.
    S_Synth s = MakeSynth(rng, 200);
    s.rectFlags = COLLISION_PRIM_RECT;
    s.rectMatrix = Rigid(0.4f, 0.3f, S_vector(30, 5, 30));
    const std::string path = TempFile("sweeps.klz");
    WriteSynth(s, path);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f), ang(0.0f, 6.28f), size(0.2f, 1.5f);
    long tests = 0, agree = 0, hits = 0;
    S_Synth away = MakeSynth(rng, 150);
    away.rectFlags = COLLISION_PRIM_RECT;
    away.rectMatrix = Rigid(0.2f, 0.4f, S_vector(30, 5, 30));
    away.originX = 41.0f;
    away.originZ = 17.5f;
    for (S_SynthTri &tr : away.tris)
        for (S_vector &v : tr.v)
            v = v + S_vector(away.originX, 0, away.originZ);
    away.rectMatrix.m[3][0] += away.originX;
    away.rectMatrix.m[3][2] += away.originZ;
    const std::string awayPath = TempFile("sweeps_away.klz");
    WriteSynth(away, awayPath);
    for (int pass = 0; pass < 4; ++pass) {
        if (pass == 0)
            g_collisionWorld.Load(path.c_str(), true);
        else if (pass == 1)
            g_collisionWorld.Load("missions\\BURMA1\\tree.klz", true);
        else if (pass == 2)
            g_collisionWorld.Load(awayPath.c_str(), true);
        else
            g_collisionWorld.Load("missions\\Co_Sicily1\\tree.klz", true);
        const bool real = pass == 1 || pass == 3;
        const S_vector origin = pass == 2 ? S_vector(away.originX, 0, away.originZ) : S_vector(0, 0, 0);
        const S_CollisionMesh &m = *g_collisionWorld.m_mesh;
        std::vector<S_TestVolume *> volumes;
        static S_CollisionConvexVolume hull;
        if (!real) {
            volumes = MakeVolumes(rng, 40, origin);
            for (S_TestVolume *tv : volumes)
                g_collisionWorld.AddVolume(&tv->v);
            hull = S_CollisionConvexVolume();
            hull.volume.flags = COLLISION_SHAPE_CONVEX;
            hull.volume.center = S_vector(10, 5, 50) + origin;
            g_collisionWorld.AddVolume(&hull.volume);
        }
        std::uniform_real_distribution<float> ux(m.minX, m.maxX), uz(m.minZ, m.maxZ), uy(m.minY > -100 ? m.minY : -5.0f, pass == 0 ? 16.0f : 30.0f);
        // (Near the primitives: the real levels' every kind, my levels' rectangle; half of the sweeps.)
        std::vector<S_vector> near2;
        for (int i = 0; i < m.numBoxes; i += 3)
            near2.push_back((m.boxes[i].worldMin + m.boxes[i].worldMax) * 0.5f);
        for (int k = 0; k < 20; ++k) {
            for (int i = 0; i < m.numAabbs; ++i)
                near2.push_back((m.aabbs[i].min + m.aabbs[i].max) * 0.5f);
            for (int i = 0; i < m.numSpheres; ++i)
                near2.push_back(ToVector(m.spheres[i].center));
            for (int i = 0; i < m.numRects; ++i)
                near2.push_back(Mul((m.rects[i].min + m.rects[i].max) * 0.5f, m.rects[i].matrix, true));
            for (int i = 0; i < m.numPoles; ++i)
                near2.push_back(S_vector(m.poles[i].x, m.minY > -100 ? m.minY + 2.0f : 2.0f, m.poles[i].z));
        }
        for (int i = 0; i < 6000 / QUICK; ++i) {
            S_vector at(ux(rng), uy(rng), uz(rng));
            if (!near2.empty() && (real || i % 2 == 0))
                at = near2[rng() % near2.size()] + S_vector(unit(rng), unit(rng), unit(rng)) * 2.0f;
            I3D_bbox b;
            b.min = S_vector(-size(rng), -size(rng), -size(rng));
            b.max = S_vector(size(rng), size(rng), size(rng));
            const S_matrix from = Rigid(ang(rng), ang(rng) * 0.3f, at);
            const S_matrix to = Rigid(ang(rng), ang(rng) * 0.3f, at + S_vector(unit(rng), unit(rng) * 0.3f, unit(rng)) * 1.5f);
            const S_vector velocity = S_vector(unit(rng), unit(rng), unit(rng)) * 0.05f;
            const uint32_t types = i % 5 == 0 ? COLLISION_EXCLUDE_ALL : i % 5 == 1 ? 0x100u : 0u;
            const uint32_t flags = i % 7 == 0 ? COLLISION_EXCLUDE_ALL : i % 7 == 1 ? COLLISION_FLAG_PRIMITIVE : 0u;
            S_vector4 u4 = {0, 0, 0, 1};
            const bool got = g_collisionWorld.TestBoxSweep(b, from, to, u4, S_vector(), velocity, types, flags);
            const S_OwnSweep q = OwnSweep(b, from, to, velocity);
            bool want = false;
            if (types != COLLISION_EXCLUDE_ALL) {
                for (const S_TestVolume *tv : volumes) {
                    if (tv->v.type & (types | COLLISION_TYPE_IGNORED))
                        continue;
                    const uint32_t shape = tv->v.flags & COLLISION_SHAPE_MASK;
                    if (shape == COLLISION_SHAPE_WORLD_BOX)
                        want |= tv->v.bbox.min.x <= q.max.x && tv->v.bbox.min.y <= q.max.y && tv->v.bbox.min.z <= q.max.z &&
                                q.min.x <= tv->v.bbox.max.x && q.min.y <= tv->v.bbox.max.y && q.min.z <= tv->v.bbox.max.z;
                    else if (shape != COLLISION_SHAPE_CAPSULE) {
                        const float r = tv->v.radius + q.radius;
                        want |= (q.center - tv->v.center).Square() <= r * r;
                    }
                }
                // (The hull: any query whose cells reach it.)
                if (!real) {
                    const S_CollisionGrid &g = *g_collisionWorld.m_grid;
                    const int hx = static_cast<int>((hull.volume.center.x - g.minX) / g.cellSizeX), hz = static_cast<int>((hull.volume.center.z - g.minZ) / g.cellSizeZ);
                    const int x0 = std::max(static_cast<int>((q.min.x - g.minX) / g.cellSizeX - 1.0f), 0), x1 = std::min(static_cast<int>((q.max.x - g.minX) / g.cellSizeX + 2.0f), g.cellsX);
                    const int z0 = std::max(static_cast<int>((q.min.z - g.minZ) / g.cellSizeZ - 1.0f), 0), z1 = std::min(static_cast<int>((q.max.z - g.minZ) / g.cellSizeZ + 2.0f), g.cellsZ);
                    want |= hx >= x0 && hx < x1 && hz >= z0 && hz < z1;
                }
            }
            if (!want && flags != COLLISION_EXCLUDE_ALL)
                want = OwnSweepStatic(m, q, flags);
            ++tests;
            hits += got;
            CHECK(got == want);
            agree += got == want;
        }
        for (S_TestVolume *tv : volumes) {
            g_collisionWorld.RemoveVolume(&tv->v);
            delete tv;
        }
        if (!real)
            g_collisionWorld.RemoveVolume(&hull.volume);
        g_collisionWorld.Unload(false);
    }
    std::printf("  box sweeps: %ld, %ld agree, %ld overlap\n", tests, agree, hits);
    CHECK(hits > tests / 10 && hits < tests * 9 / 10);
    // No level: false.
    I3D_bbox b;
    b.min = S_vector(-1, -1, -1);
    b.max = S_vector(1, 1, 1);
    S_vector4 u4 = {0, 0, 0, 0};
    CHECK(!g_collisionWorld.TestBoxSweep(b, Rigid(0, 0, S_vector()), Rigid(0, 0, S_vector()), u4, S_vector(), S_vector(), 0, 0));
}

void TestLoaderDetails()
{
    // Triangle kinds: the loader sets those of the lowest layer's cells from the normal and the v0-v1 edge (a
    // difference under 1e-4 counts as none; ties of the normal's axes go to y, then x); the upper layer's keep
    // what the file stores.
    {
        S_Synth s;
        s.withRect = false;
        struct S_Case {
            S_vector a, b, c, normal;
            int kind;
        };
        const float k = 0.70710678f;
        const S_Case cases[] = {
            {S_vector(2, 1, 2), S_vector(3, 1, 2.00005f), S_vector(4, 1, 3), S_vector(0, 1, 0), TRIANGLE_KIND_XZ_EDGE_X},   // (dz < 1e-4)
            {S_vector(2, 1, 6), S_vector(3, 1, 6.0002f), S_vector(4, 1, 7), S_vector(0, 1, 0), TRIANGLE_KIND_XZ},
            {S_vector(2, 1, 10), S_vector(3, 1, 10.00012f), S_vector(4, 1, 11), S_vector(0, 1, 0), TRIANGLE_KIND_XZ},   // (1.2e-4)
            {S_vector(10, 1, 10), S_vector(10.00012f, 1, 11), S_vector(11, 1, 10.5f), S_vector(0, 1, 0), TRIANGLE_KIND_XZ},
            {S_vector(20, 1, 10), S_vector(20, 1.00012f, 11), S_vector(20, 2, 10.5f), S_vector(1, 0, 0), TRIANGLE_KIND_YZ},
            {S_vector(10, 1, 2), S_vector(10.00005f, 1, 3), S_vector(11, 1, 2.5f), S_vector(0, 1, 0), TRIANGLE_KIND_XZ_EDGE_Z},
            {S_vector(10, 1, 6), S_vector(10.0002f, 1, 7), S_vector(11, 1, 6.5f), S_vector(0, 1, 0), TRIANGLE_KIND_XZ},
            {S_vector(20, 1, 2), S_vector(20, 1.00005f, 3), S_vector(20, 2, 2.5f), S_vector(1, 0, 0), TRIANGLE_KIND_YZ_EDGE_Z},
            {S_vector(20, 1, 6), S_vector(20, 1.0002f, 7), S_vector(20, 2, 6.5f), S_vector(1, 0, 0), TRIANGLE_KIND_YZ},
            {S_vector(28, 1, 2), S_vector(28.00005f, 2, 2), S_vector(29, 1.5f, 2), S_vector(0, 0, 1), TRIANGLE_KIND_XY_EDGE_Y},
            {S_vector(28, 1, 6), S_vector(28.0002f, 2, 6), S_vector(29, 1.5f, 6), S_vector(0, 0, 1), TRIANGLE_KIND_XY},
            // Ties: x = y (to y), x = z (to x), y = z (to y).
            {S_vector(36, 1, 2), S_vector(37, 1, 3), S_vector(38, 2, 2), S_vector(k, k, 0), TRIANGLE_KIND_XZ},
            {S_vector(36, 1, 6), S_vector(37, 1, 7), S_vector(38, 2, 6), S_vector(k, 0, k), TRIANGLE_KIND_YZ_EDGE_Z},   // (v0-v1 level)
            {S_vector(44, 1, 2), S_vector(45, 1, 3), S_vector(46, 2, 2), S_vector(0, k, k), TRIANGLE_KIND_XZ},
            // x largest, z second: facing x.
            {S_vector(44, 1, 6), S_vector(45, 1, 7), S_vector(46, 2, 6), S_vector(0.8f, 0.1f, 0.5f), TRIANGLE_KIND_YZ_EDGE_Z},
            {S_vector(44, 1, 10), S_vector(45, 1.5f, 11), S_vector(46, 2, 10), S_vector(0.8f, 0.1f, 0.5f), TRIANGLE_KIND_YZ},
            // z largest: facing z.
            {S_vector(52, 1, 2), S_vector(53, 1, 3), S_vector(54, 2, 2), S_vector(0.5f, 0.1f, 0.8f), TRIANGLE_KIND_XY},
        };
        for (const S_Case &c : cases) {
            for (const float dy : {0.0f, 9.0f}) {       // (the lowest layer; the upper one)
                S_SynthTri t = Tri(c.a + S_vector(0, dy, 0), c.b + S_vector(0, dy, 0), c.c + S_vector(0, dy, 0), 0, false, c.kind);
                t.normal = c.normal;
                t.flags = 0;                            // (stored without a kind)
                s.tris.push_back(t);
            }
        }
        const std::string path = TempFile("kinds.klz");
        WriteSynth(s, path);
        g_collisionWorld.Load(path.c_str(), true);
        const S_CollisionMesh &m = *g_collisionWorld.m_mesh;
        // (Only those whose stored normal is their plane's: the ties' cases were given normals for the classification.)
        int lowHit = 0, highHit = 0, lowTried = 0, highTried = 0;
        for (size_t i = 0; i < s.tris.size(); ++i) {
            const S_SynthTri &tr = s.tris[i];
            const S_vector real = Cross(tr.v[1] - tr.v[0], tr.v[2] - tr.v[0]).Normalized();
            if (std::fabs(real.Dot(tr.normal)) < 0.9999f)
                continue;
            const S_vector mid = (tr.v[0] + tr.v[1] + tr.v[2]) * (1.0f / 3.0f);
            S_CollisionVolume *hit = g_collisionWorld.RayTestBrute(mid - tr.normal * 0.05f, tr.normal * 0.1f, nullptr, nullptr, COLLISION_EXCLUDE_ALL, 0);
            (i % 2 == 0 ? lowTried : highTried) += 1;
            (i % 2 == 0 ? lowHit : highHit) += hit == reinterpret_cast<S_CollisionVolume *>(&m.triangles[i]);
        }
        CHECK(lowTried > 8 && lowHit == lowTried && highTried == lowTried && highHit == 0);
        for (size_t i = 0; i < s.tris.size(); ++i) {
            const int got = static_cast<int>(m.triangles[i].flags & TRIANGLE_KIND_MASK);
            CHECK(got == (i % 2 == 0 ? s.tris[i].kind : 0));
            if (got != (i % 2 == 0 ? s.tris[i].kind : 0))
                std::printf("  kind case %zu: got %d want %d\n", i / 2, got, s.tris[i].kind);
        }
        g_collisionWorld.Unload(false);
    }
    // Degenerate triangles on a visual: two vertices closer than 1e-4 (squared distance under 1e-8) score 2, an axis
    // without extent (under 1e-8) scores 1; 2 or more disables. The extent is taken over all three vertices.
    {
        S_Synth s;
        s.withRect = false;
        s.records = {{0u, "nonexist"}, {1u, "vis"}};
        std::vector<S_vector> verts;
        std::vector<bool> enabled;
        const auto add = [&](S_vector a, S_vector b, S_vector c, bool on) {
            S_SynthTri t;
            t.v[0] = a;
            t.v[1] = b;
            t.v[2] = c;
            t.normal = S_vector(0, 1, 0);
            t.flags = TRIANGLE_KIND_XZ;
            t.frame = 1;
            for (int k = 0; k < 3; ++k) {
                t.index[k] = static_cast<int>(verts.size());
                verts.push_back(t.v[k]);
            }
            s.tris.push_back(t);
            enabled.push_back(on);
        };
        const S_vector o(20, 4, 20);
        add(o, o + S_vector(0.00005f, 0, 0.00005f), o + S_vector(1, 0.5f, 1), false);    // a-b close
        add(o, o + S_vector(1, 0.5f, 1), o + S_vector(1.00005f, 0.5f, 1), false);       // b-c close
        add(o + S_vector(1, 0.5f, 1), o + S_vector(2, 1, 0), o + S_vector(1.00005f, 0.5f, 1), false);   // c-a close
        add(o, o + S_vector(0.0002f, 0.3f, 0), o + S_vector(1, 0.5f, 1), true);         // (2e-4: not close)
        add(o, o + S_vector(0, 1, 0.5f), o + S_vector(0, 0, 1), true);                  // x flat only (y from b)
        add(o, o + S_vector(0, 0.5f, 0), o + S_vector(0, 0.2f, 1), true);               // x flat only (z from c)
        add(o, o + S_vector(1, 0, 0.5f), o + S_vector(0.5f, 0, 1), true);               // y flat only
        add(o, o + S_vector(0.5f, 1, 0), o + S_vector(1, 0.5f, 0), true);               // z flat only
        add(o, o + S_vector(0, 0, 1), o + S_vector(0, 0, 2), false);                    // x and y flat
        add(o, o + S_vector(1, 0, 0), o + S_vector(2, 0, 0), false);                    // y and z flat
        add(o, o + S_vector(0, 1, 0), o + S_vector(0, 2, 0), false);                    // x and z flat
        add(o, o + S_vector(0.000001f, 1, 0.5f), o + S_vector(0, 0, 1), true);          // (1e-6 in x: not flat)
        // An extent of 5e-9 is flat (under 1e-8): with another flat axis, disabled. (Near 0, where a float holds it.)
        const S_vector z0(0, 0, 0);
        add(z0, z0 + S_vector(5e-9f, 0, 1), z0 + S_vector(0, 0, 2), false);           // x 5e-9, y flat
        add(z0, z0 + S_vector(0, 5e-9f, 1), z0 + S_vector(0, 0, 2), false);           // y 5e-9, x flat
        add(z0, z0 + S_vector(1, 0, 5e-9f), z0 + S_vector(2, 0, 0), false);           // z 5e-9, y flat
        I3D_frame *vis = SceneFrame(FRAME_VISUAL, 0x48534D4C, verts);
        g_sceneFrames = {{"vis", vis}};
        g_verticesAsked = -1;
        const std::string path = TempFile("degenerate.klz");
        WriteSynth(s, path);
        g_collisionWorld.Load(path.c_str(), true);
        const S_CollisionMesh &m = *g_collisionWorld.m_mesh;
        CHECK(g_verticesAsked == 1 && g_levelAsked == 0 && g_archivesFirst == 0);
        for (size_t i = 0; i < s.tris.size(); ++i) {
            const bool on = !(m.triangles[i].flags & COLLISION_FLAG_DISABLED);
            CHECK(on == enabled[i]);
            if (on != enabled[i])
                std::printf("  degenerate case %zu: enabled %d\n", i, on);
        }
        g_collisionWorld.Unload(false);
        g_sceneFrames.clear();
    }
    // The dynamic grid of a level that is not square, and of one smaller than a volume cell (one cell); volumes
    // outside the level go to the edge cells on every side; a cell released away from the corner.
    {
        S_Synth s;
        s.withRect = false;
        s.cellsX = 6;
        s.cellsZ = 3;
        s.cellX = 10.0f;
        s.cellZ = 7.0f;
        s.volumeCell = 12.0f;
        const std::string path = TempFile("grid.klz");
        WriteSynth(s, path);
        g_collisionWorld.Load(path.c_str(), true);
        S_CollisionGrid &g = *g_collisionWorld.m_grid;
        CHECK(g.cellsX == 5 && g.cellsZ == 1 && g.cellSizeX == 12.0f && g.cellSizeZ == 21.0f && g.minX == 0.0f && g.minZ == 0.0f);
        S_CollisionVolume v[4];
        const S_vector at[4] = {S_vector(-5, 0, 30), S_vector(70, 0, -3), S_vector(30, 0, 10), S_vector(-1, 0, -1)};
        const int cellX[4] = {0, 4, 2, 0};
        for (int i = 0; i < 4; ++i) {
            v[i].flags = COLLISION_SHAPE_SPHERE;
            v[i].center = at[i];
            g_collisionWorld.AddVolume(&v[i]);
            CHECK(v[i].cell && v[i].cell->x == cellX[i] && v[i].cell->z == 0);
        }
        S_CollisionGridCell *mid = v[2].cell;
        g_collisionWorld.RemoveVolume(&v[2]);
        g_collisionWorld.ReleaseEmptyCells();
        CHECK(g.cells[2] == nullptr && g.cells[0] && g.cells[4] && g_collisionWorld.m_freeCells.size() == 1 && g_collisionWorld.m_freeCells[0] == mid);
        for (int i = 0; i < 4; ++i)
            g_collisionWorld.RemoveVolume(&v[i]);
        g_collisionWorld.Unload(false);
        s.cellsX = s.cellsZ = 1;
        s.cellX = s.cellZ = 5.0f;
        s.volumeCell = 16.0f;
        WriteSynth(s, path);
        g_collisionWorld.Load(path.c_str(), true);
        CHECK(g_collisionWorld.m_grid->cellsX == 1 && g_collisionWorld.m_grid->cellsZ == 1 && g_collisionWorld.m_grid->cellSizeX == 5.0f);
        g_collisionWorld.Unload(false);
    }
    // The water's height range: a tilted rectangle's corners, a water triangle first (its v0 the lowest, its v2
    // the highest); the caller's values replaced.
    {
        S_Synth s;
        s.rectMatrix = Rigid(0.3f, 0.6f, S_vector(30, 3, 30));
        s.rectMin = S_vector(-10.0f, 0.0f, -6.0f);
        s.rectMax = S_vector(10.0f, 0.0f, 6.0f);
        S_SynthTri low = Tri(S_vector(10, 2, 10), S_vector(11, 3, 11), S_vector(12, 4.5f, 10), COLLISION_FLAG_WATER, false);
        s.tris.push_back(low);
        s.tris.push_back(Tri(S_vector(20, 2, 20), S_vector(21, 3, 21), S_vector(22, 2.5f, 20), 0, false));
        const std::string path = TempFile("water.klz");
        WriteSynth(s, path);
        g_collisionWorld.Load(path.c_str(), true);
        float lo = 1e9f, hi = 1e9f;
        g_collisionWorld.GetWaterHeightRange(lo, hi);
        float rlo = 1e9f, rhi = -1e9f;
        for (const float x : {s.rectMin.x, s.rectMax.x})
            for (const float z : {s.rectMin.z, s.rectMax.z}) {
                const float y = Mul(S_vector(x, 0, z), s.rectMatrix, true).y;
                rlo = std::min(rlo, y);
                rhi = std::max(rhi, y);
            }
        CHECK(std::fabs(lo - rlo) < 1e-4f && std::fabs(hi - rhi) < 1e-4f && rhi > 4.5f && rlo < 2.0f);
        // Without water: the empty range.
        g_collisionWorld.m_mesh->triangles[0].flags &= ~COLLISION_FLAG_WATER;
        g_collisionWorld.m_mesh->rects[0].flags &= ~COLLISION_FLAG_WATER;
        g_collisionWorld.GetWaterHeightRange(lo, hi);
        CHECK(lo == FLT_MAX && hi == -FLT_MAX);
        g_collisionWorld.Unload(false);
    }
    // A real level with spheres: every primitive's frame reference resolved (no scene: all null).
    S_RawLevel raw;
    if (ReadRaw(g_missions + "\\Co_Sicily1\\tree.klz", raw)) {
        g_collisionWorld.Load("missions\\Co_Sicily1\\tree.klz", true);
        const S_CollisionMesh &m = *g_collisionWorld.m_mesh;
        std::vector<void *> prims;
        CHECK(m.numSpheres > 10 && g_collisionWorld.GetFramePrimitives(nullptr, prims) == m.numBoxes + m.numAabbs + m.numSpheres + m.numRects + m.numPoles);
        // A segment of length exactly 1 toward a sphere from half a metre out: hit (only a zero-length one is refused).
        int spheresHit = 0;
        for (int i = 0; i < m.numSpheres; ++i) {
            const S_vector c = ToVector(m.spheres[i].center);
            S_vector p;
            spheresHit += g_collisionWorld.RayTestBrute(c + S_vector(m.spheres[i].radius + 0.5f, 0, 0), S_vector(-1, 0, 0), &p, nullptr, COLLISION_EXCLUDE_ALL,
                                                        0) == reinterpret_cast<S_CollisionVolume *>(&m.spheres[i]);
        }
        CHECK(spheresHit > m.numSpheres * 3 / 4);
        g_collisionWorld.Unload(false);
    }
}
}  // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    const char *dir = std::getenv("MISSIONS_DIR");
    g_missions = dir ? dir : "H:\\HD2_Unpacked\\MISSIONS";
    g_driverVt[0x88 / 4] = reinterpret_cast<void *>(F_FindFrame);
    g_frameVt[0x04 / 4] = reinterpret_cast<void *>(F_GetVisualType);
    g_frameVt[0x7C / 4] = reinterpret_cast<void *>(F_SetWorldSpace);
    g_meshVt[0x04 / 4] = reinterpret_cast<void *>(F_GetLevel);
    g_levelVt[0x04 / 4] = reinterpret_cast<void *>(F_GetVertices);
    TestLevelLoading();
    TestLevelRays();
    TestSynthTriangles();
    TestVolumes(S_vector(0, 0, 0));
    TestVolumes(S_vector(37.5f, 0, 23.25f));    // (a level whose corner is not at 0)
    TestCombined();
    TestVisualTriangles();
    TestCollisionRest();
    TestNormalsAndShapes();
    TestBoxSweeps();
    TestLoaderDetails();
    for (const std::string &f : g_tempFiles)
        std::remove(f.c_str());
    std::printf("collision: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
