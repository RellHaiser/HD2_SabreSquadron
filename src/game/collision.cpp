// Collision world, part 1 (original source 0x006ee000..0x006f2fe0 and the wrappers at
// 0x00718700): loading the level collision, the dynamic volume grid, ignored volumes and the
// box sweep overlap test.
#include "game/collision.h"
#include "game/collision_internal.h"

#include <malloc.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <vector>

#include "game/game.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"

C_CollisionWorld g_collisionWorld;
bool g_levelCollisionLoaded = false;

namespace {

constexpr uint32_t COLLISION_FILE_MAGIC = 0x43666947;      // "GifC"
constexpr int SEEK_FROM_START = 0;
constexpr int SEEK_FROM_CURRENT = 1;
constexpr int SEEK_FROM_END = 2;

// Header of a level collision file. The frame references follow it: an offset table, then
// the records (flags, then the frame's name); the rest of the file is the mesh block.
struct S_CollisionFileHeader {                              // 0x18 bytes
    uint32_t magic;
    uint32_t version;                                       // +0x04 0x10005 in the game's files
    int frameBlockEnd;                                      // +0x08 file offset after the records (and their mesh block's start)
    int numFrames;                                          // +0x0C
    uint32_t unknown10;                                     // (0x21F in the game's files)
    uint32_t unknown14;
};
// Frame reference flags.
enum : uint32_t {
    FRAME_REFERENCE_VISUAL = 1,                             // must be a visual; its geometry is used
};
// Frame index of triangle vertices that index the mesh's own vertex table.
constexpr uint32_t TRIANGLE_MESH_VERTICES = 0x7FFF;
// Stride of the vertices of a visual's mesh.
constexpr int VISUAL_VERTEX_SIZE = 0x20;

// Kind of a cell entry in the file (top byte of the entry; 0 = triangle offset).
enum : uint32_t {
    CELL_ENTRY_TRIANGLE = 0,
};

uint8_t *AddressOf(void *p, uint32_t offset) { return static_cast<uint8_t *>(p) + offset; }

bool IsTriangle(uint32_t flags) { return (flags & (COLLISION_FLAG_PRIMITIVE | COLLISION_FLAG_DISABLED)) == 0; }

// Corners of `box` (min / max): (max.x | min.x) x (max.y | min.y) x (min.z, max.z).
void GetBoxCorners(const I3D_bbox &box, S_vector corners[8])
{
    int n = 0;
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            const float x = i == 0 ? box.max.x : box.min.x;
            const float y = j == 0 ? box.max.y : box.min.y;
            corners[n++] = S_vector(x, y, box.min.z);
            corners[n++] = S_vector(x, y, box.max.z);
        }
    }
}

void Expand(S_vector &min, S_vector &max, const S_vector &p)
{
    if (p.x < min.x)
        min.x = p.x;
    if (max.x < p.x)
        max.x = p.x;
    if (p.z < min.z)
        min.z = p.z;
    if (max.z < p.z)
        max.z = p.z;
    if (p.y < min.y)
        min.y = p.y;
    if (max.y < p.y)
        max.y = p.y;
}

} // namespace

// The swept box of TestBoxSweep, laid out as the original's stack record (only the members
// that the tests read; +0x00..+0x2F are not used).
struct C_CollisionWorld::S_OverlapQuery {
    S_vector center;                                        // +0x30 box center at the end of the sweep
    S_vector point;                                         // +0x40 center + velocity * 20
    float unknown4C;                                        // +0x4C
    S_vector min;                                           // +0x50 bounds of the whole sweep
    S_vector max;                                           // +0x5C
    // +0x68: the squared radius of the box (used as a radius by the tests, which therefore
    // accept more than the box touches).
    float radius;
    float planeDistanceSq;                                  // +0x6C triangles: max squared distance of `point`
    uint32_t excludeTypes;                                  // +0x88
    uint32_t excludeFlags;                                  // +0x8C
};

// ============================================================================ the world

C_CollisionWorld::~C_CollisionWorld() = default;          // (frees only the cell lists)

void C_CollisionWorld::Free(bool keepMesh)
{
    if (!keepMesh || !m_buffer) {
        if (m_mesh) {
            delete[] m_mesh->cells;
            _aligned_free(m_mesh);
            m_mesh = nullptr;
        }
        m_buffer = nullptr;
        m_unknown10 = 0;
        m_ownBuffer = false;
    }
    if (m_grid) {
        // (Cells still in the grid or in the lists are not deleted; Unload clears the grid first.)
        delete[] m_grid->cells;
        delete m_grid;
        m_grid = nullptr;
        m_freeCells.clear();
        m_emptyCells.clear();
    }
}

void C_CollisionWorld::Unload(bool keepMesh)
{
    ClearGrid();
    Free(keepMesh);
}

void LoadLevelCollision(const char *fileName)
{
    g_collisionWorld.Load(fileName, true);
}

// Sets the kind (low byte 1..7) of the triangles from their normal and edges. (Only the cells
// of the lowest layer are visited: [cellsZ][cellsX] of the [cellsY][cellsZ][cellsX] array.)
static void ClassifyTriangles(S_CollisionMesh &mesh)       // 0x006ee750
{
    for (int z = 0; z < mesh.cellsZ; ++z) {
        for (int x = 0; x < mesh.cellsX; ++x) {
            S_CollisionMeshCell *cell = mesh.cells[mesh.cellsX * z + x];
            for (int i = 0; i < cell->count; ++i) {
                auto *tri = static_cast<S_CollisionTriangle *>(cell->entries[i]);
                if (!IsTriangle(tri->flags))
                    continue;
                const float nx = std::fabs(tri->normal.x);
                const float ny = std::fabs(tri->normal.y);
                const float nz = std::fabs(tri->normal.z);
                const S_vector4 &a = *tri->v0;
                const S_vector4 &b = *tri->v1;
                const bool edgeX = std::fabs(b.x - a.x) >= 0.0001f;
                const bool edgeY = std::fabs(b.y - a.y) >= 0.0001f;
                const bool edgeZ = std::fabs(b.z - a.z) >= 0.0001f;
                uint32_t kind;
                if (nx <= ny && nz <= ny)
                    kind = !edgeX ? TRIANGLE_KIND_XZ_EDGE_Z : edgeZ ? TRIANGLE_KIND_XZ : TRIANGLE_KIND_XZ_EDGE_X;
                else if (nx > ny && nz <= nx)
                    kind = edgeY ? TRIANGLE_KIND_YZ : TRIANGLE_KIND_YZ_EDGE_Z;
                else
                    kind = edgeX ? TRIANGLE_KIND_XY : TRIANGLE_KIND_XY_EDGE_Y;
                tri->flags = (tri->flags & ~TRIANGLE_KIND_MASK) | kind;
            }
        }
    }
}

void C_CollisionWorld::Load(const char *fileName, bool buildGrid)
{
    m_unknown428 = 1e6f;
    m_unknown424 = 0;
    Free(m_ownBuffer);
    const int file = dtaOpen(fileName, 0);
    if (file < 0)
        return;
    const int fileSize = dtaSeek(file, 0, SEEK_FROM_END);
    dtaSeek(file, 0, SEEK_FROM_START);
    S_CollisionFileHeader header;
    dtaRead(file, &header, sizeof(header));
    if (header.magic != COLLISION_FILE_MAGIC) {
        dtaClose(file);                                     // (the original leaves it open)
        return;
    }
    // Frame references.
    std::vector<int> recordOffsets(header.numFrames);
    dtaRead(file, recordOffsets.data(), header.numFrames * 4);
    const int recordBase = static_cast<int>(sizeof(header)) + header.numFrames * 4;
    std::vector<uint8_t> records(header.frameBlockEnd - recordBase);
    dtaRead(file, records.data(), static_cast<int>(records.size()));
    // The mesh block.
    const int meshSize = fileSize - dtaSeek(file, 0, SEEK_FROM_CURRENT);
    auto *mesh = static_cast<S_CollisionMesh *>(m_ownBuffer ? m_buffer : _aligned_malloc(meshSize, 16));
    m_meshSize = meshSize;
    dtaRead(file, mesh, meshSize);
    dtaClose(file);

    // Look up the referenced frames; `refs` later holds their vertex data.
    std::vector<void *> refs(header.numFrames);
    I3D_scene *scene = GetGame()->GetScene();
    for (int i = header.numFrames - 1; i >= 0; --i) {
        const uint8_t *record = records.data() + recordOffsets[i] - recordBase;
        const uint32_t flags = *reinterpret_cast<const uint32_t *>(record);
        I3D_frame *frame = I3DGetDriver()->FindFrame(scene, reinterpret_cast<const char *>(record + 4));
        if ((flags & FRAME_REFERENCE_VISUAL) && frame && frame->GetType() != FRAME_VISUAL)
            frame = nullptr;
        refs[i] = frame;
        if ((flags & FRAME_REFERENCE_VISUAL) && frame)
            static_cast<I3D_visual *>(frame)->SetWorldSpace(1);
    }
    g_levelCollisionLoaded = true;

    // Tables after the header.
    m_mesh = mesh;
    mesh->gridLinesX = reinterpret_cast<float *>(mesh + 1);
    mesh->gridLinesY = mesh->gridLinesX + mesh->cellsX + 1;
    mesh->gridLinesZ = mesh->gridLinesY + mesh->cellsY + 1;
    uint8_t *table = reinterpret_cast<uint8_t *>(mesh->gridLinesZ + mesh->cellsZ + 5);
    const auto place = [&table](int count, int size) -> uint8_t * {
        uint8_t *at = count ? table : nullptr;
        table += count * size;
        return at;
    };
    mesh->triangles = reinterpret_cast<S_CollisionTriangle *>(table);
    table += mesh->numTriangles * sizeof(S_CollisionTriangle);
    mesh->aabbs = reinterpret_cast<S_CollisionAabb *>(place(mesh->numAabbs, sizeof(S_CollisionAabb)));
    mesh->boxes = reinterpret_cast<S_CollisionBox *>(place(mesh->numBoxes, sizeof(S_CollisionBox)));
    mesh->poles = reinterpret_cast<S_CollisionPole *>(place(mesh->numPoles, sizeof(S_CollisionPole)));
    mesh->rects = reinterpret_cast<S_CollisionRect *>(place(mesh->numRects, sizeof(S_CollisionRect)));
    mesh->spheres = reinterpret_cast<S_CollisionSphere *>(place(mesh->numSpheres, sizeof(S_CollisionSphere)));
    mesh->convexes = reinterpret_cast<S_CollisionConvexPrim *>(place(mesh->numConvexes, sizeof(S_CollisionConvexPrim)));
    mesh->prims86 = place(mesh->numPrims86, 0xA0);
    mesh->capsules = reinterpret_cast<S_CollisionCapsule *>(place(mesh->numCapsules, sizeof(S_CollisionCapsule)));
    mesh->vertices = reinterpret_cast<S_vector4 *>(place(mesh->numVertices, sizeof(S_vector4)));
    table += sizeof(S_vector4);                             // one spare vertex
    auto *cell = reinterpret_cast<S_CollisionMeshCell *>(table);

    // The primitives' frames (the file stores frame reference indices).
    const auto relocateFrames = [&refs](void *items, int count, int size) {
        for (int i = 0; i < count; ++i) {
            I3D_frame *&frame = *reinterpret_cast<I3D_frame **>(AddressOf(items, i * size + 4));
            frame = static_cast<I3D_frame *>(refs[reinterpret_cast<uintptr_t>(frame)]);
        }
    };
    relocateFrames(mesh->aabbs, mesh->numAabbs, sizeof(S_CollisionAabb));
    relocateFrames(mesh->boxes, mesh->numBoxes, sizeof(S_CollisionBox));
    relocateFrames(mesh->poles, mesh->numPoles, sizeof(S_CollisionPole));
    relocateFrames(mesh->rects, mesh->numRects, sizeof(S_CollisionRect));
    relocateFrames(mesh->spheres, mesh->numSpheres, sizeof(S_CollisionSphere));
    relocateFrames(mesh->convexes, mesh->numConvexes, sizeof(S_CollisionConvexPrim));
    relocateFrames(mesh->prims86, mesh->numPrims86, 0xA0);
    relocateFrames(mesh->capsules, mesh->numCapsules, sizeof(S_CollisionCapsule));

    // Triangles may use the vertices of a visual's mesh (first level) directly.
    for (int i = header.numFrames - 1; i >= 0; --i) {
        auto *frame = static_cast<I3D_frame *>(refs[i]);
        I3D_mesh *visualMesh = nullptr;
        if (frame && frame->GetType() == FRAME_VISUAL && frame->GetVisualType() != I3DVISUAL_MIRROR)
            visualMesh = static_cast<I3D_visual *>(frame)->GetVisualMesh();
        refs[i] = visualMesh ? visualMesh->GetLevel(0)->GetVertices(1) : nullptr;
    }
    for (int i = 0; i < mesh->numTriangles; ++i) {
        S_CollisionTriangle &tri = mesh->triangles[i];
        const uint32_t i0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tri.v0));
        const uint32_t i1 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tri.v1));
        const uint32_t i2 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tri.v2));
        const uint32_t frameIndex = i0 >> 16;
        if (frameIndex == TRIANGLE_MESH_VERTICES) {
            tri.v0 = &mesh->vertices[i0 & 0xFFFF];
            tri.v1 = &mesh->vertices[i1 & 0xFFFF];
            tri.v2 = &mesh->vertices[i2 & 0xFFFF];
            continue;
        }
        if (void *vertices = refs[frameIndex]) {
            tri.v0 = reinterpret_cast<S_vector4 *>(AddressOf(vertices, (i0 & 0xFFFF) * VISUAL_VERTEX_SIZE));
            tri.v1 = reinterpret_cast<S_vector4 *>(AddressOf(vertices, (i1 & 0xFFFF) * VISUAL_VERTEX_SIZE));
            tri.v2 = reinterpret_cast<S_vector4 *>(AddressOf(vertices, (i2 & 0xFFFF) * VISUAL_VERTEX_SIZE));
            // Degenerate triangles (coincident vertices, flat boxes) are disabled.
            const S_vector4 &a = *tri.v0;
            const S_vector4 &b = *tri.v1;
            const S_vector4 &c = *tri.v2;
            S_vector min(a.x, a.y, a.z), max(a.x, a.y, a.z);
            Expand(min, max, S_vector(b.x, b.y, b.z));
            Expand(min, max, S_vector(c.x, c.y, c.z));
            const auto distSq = [](const S_vector4 &p, const S_vector4 &q) {
                return (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z);
            };
            unsigned score = 0;
            if (distSq(a, b) < 1e-8f)
                score += 2;
            if (distSq(b, c) < 1e-8f)
                score += 2;
            if (distSq(c, a) < 1e-8f)
                score += 2;
            if (std::fabs(max.x - min.x) < 1e-8)
                ++score;
            if (std::fabs(max.y - min.y) < 1e-8)
                ++score;
            if (std::fabs(max.z - min.z) < 1e-8)
                ++score;
            if (score < 2)
                continue;
        }
        tri.flags = COLLISION_FLAG_DISABLED;
    }

    // Cells: their entries hold table offsets (top byte: the table) until relocated.
    mesh->cells = new S_CollisionMeshCell *[mesh->cellsY * mesh->cellsZ * mesh->cellsX];
    for (int z = 0; z < mesh->cellsZ; ++z) {
        for (int y = 0; y < mesh->cellsY; ++y) {
            for (int x = 0; x < mesh->cellsX; ++x) {
                cell->entries = reinterpret_cast<void **>(cell + 1);
                auto *next = reinterpret_cast<S_CollisionMeshCell *>(cell->entries + cell->count);
                mesh->cells[(mesh->cellsZ * y + z) * mesh->cellsX + x] = cell;
                for (int i = 0; i < cell->count; ++i) {
                    const uint32_t entry = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cell->entries[i]));
                    const uint32_t offset = entry & 0xFFFFFF;
                    void *table = nullptr;
                    switch (entry >> 24) {
                    case CELL_ENTRY_TRIANGLE: cell->entries[i] = AddressOf(mesh->triangles, entry); continue;
                    case COLLISION_PRIM_BOX: table = mesh->boxes; break;
                    case COLLISION_PRIM_AABB: table = mesh->aabbs; break;
                    case COLLISION_PRIM_SPHERE: table = mesh->spheres; break;
                    case COLLISION_PRIM_RECT: table = mesh->rects; break;
                    case COLLISION_PRIM_POLE: table = mesh->poles; break;
                    case COLLISION_PRIM_CONVEX: table = mesh->convexes; break;
                    case COLLISION_PRIM_CAPSULE: table = mesh->capsules; break;
                    default: continue;
                    }
                    cell->entries[i] = AddressOf(table, offset);
                }
                cell = next;
            }
        }
    }

    if (buildGrid) {
        if (!m_grid)
            m_grid = new S_CollisionGrid;
        else
            delete[] m_grid->cells;
        m_grid->cellsX = static_cast<int>((mesh->maxX - mesh->minX) / mesh->volumeCellSize);
        m_grid->cellsZ = static_cast<int>((mesh->maxZ - mesh->minZ) / mesh->volumeCellSize);
        if (m_grid->cellsX < 1)
            m_grid->cellsX = 1;
        if (m_grid->cellsZ < 1)
            m_grid->cellsZ = 1;
        m_grid->cellSizeX = (mesh->maxX - mesh->minX) / static_cast<float>(m_grid->cellsX);
        m_grid->cellSizeZ = (mesh->maxZ - mesh->minZ) / static_cast<float>(m_grid->cellsZ);
        m_grid->minX = mesh->minX;
        m_grid->minZ = mesh->minZ;
        m_grid->cells = new S_CollisionGridCell *[m_grid->cellsZ * m_grid->cellsX]();
        m_freeCells.clear();
        m_emptyCells.clear();
    }
    ClassifyTriangles(*mesh);
}

// ============================================================================ dynamic volumes

void C_CollisionWorld::GetCellCoords(const S_vector &pos, int &x, int &z) const
{
    x = static_cast<int>((pos.x - m_grid->minX) / m_grid->cellSizeX);
    z = static_cast<int>((pos.z - m_grid->minZ) / m_grid->cellSizeZ);
    if (x < 0)
        x = 0;
    if (z < 0)
        z = 0;
    if (x >= m_grid->cellsX)
        x = m_grid->cellsX - 1;
    if (z >= m_grid->cellsZ)
        z = m_grid->cellsZ - 1;
}

S_CollisionGridCell *C_CollisionWorld::AllocCell(int x, int z)
{
    S_CollisionGridCell *cell;
    if (m_freeCells.empty()) {
        cell = new S_CollisionGridCell;
        cell->volumes.reserve(8);
    } else {
        cell = m_freeCells.back();
        m_freeCells.pop_back();
    }
    m_grid->cells[m_grid->cellsX * z + x] = cell;
    cell->x = x;
    cell->z = z;
    return cell;
}

void C_CollisionWorld::AddVolume(S_CollisionVolume *volume)
{
    if (!m_grid || volume->cell)
        return;
    int x, z;
    GetCellCoords(volume->center, x, z);
    volume->cell = m_grid->cells[m_grid->cellsX * z + x];
    if (!volume->cell)
        volume->cell = AllocCell(x, z);
    volume->cell->volumes.push_back(volume);
}

void C_CollisionWorld::RemoveVolume(S_CollisionVolume *volume)
{
    if (!m_grid || !volume->cell)
        return;
    std::vector<S_CollisionVolume *> &volumes = volume->cell->volumes;
    const auto it = std::find(volumes.begin(), volumes.end(), volume);
    if (it != volumes.end()) {
        volumes.erase(it);
        if (volumes.empty())
            m_emptyCells.push_back(volume->cell);
    }
    volume->cell = nullptr;
}

void C_CollisionWorld::MoveVolume(S_CollisionVolume *volume)
{
    if (!m_grid)
        return;
    int x, z;
    GetCellCoords(volume->center, x, z);
    S_CollisionGridCell *cell = m_grid->cells[m_grid->cellsX * z + x];
    if (cell == volume->cell)
        return;
    if (volume->cell) {
        std::vector<S_CollisionVolume *> &volumes = volume->cell->volumes;
        const auto it = std::find(volumes.begin(), volumes.end(), volume);
        if (it != volumes.end()) {
            volumes.erase(it);
            if (volumes.empty())
                m_emptyCells.push_back(volume->cell);
        }
    }
    if (!cell)
        cell = AllocCell(x, z);
    cell->volumes.push_back(volume);
    volume->cell = cell;
}

void C_CollisionWorld::ReleaseEmptyCells()
{
    while (!m_emptyCells.empty()) {
        S_CollisionGridCell *cell = m_emptyCells.back();
        if (cell->volumes.empty()) {
            m_grid->cells[m_grid->cellsX * cell->z + cell->x] = nullptr;
            m_freeCells.push_back(cell);
        }
        m_emptyCells.pop_back();
    }
}

void C_CollisionWorld::ClearGrid()
{
    // (The ignored volumes lose their flag but stay registered.)
    for (uint32_t i = 0; i < m_ignoredCount; ++i)
        m_ignored[i]->type &= ~COLLISION_TYPE_IGNORED;
    if (!m_grid)
        return;
    for (int z = 0; z < m_grid->cellsZ; ++z) {
        for (int x = 0; x < m_grid->cellsX; ++x) {
            S_CollisionGridCell *&cell = m_grid->cells[m_grid->cellsX * z + x];
            if (!cell || cell->volumes.empty())
                continue;
            // Fixed: the original leaves the volumes pointing to the cell it deletes below.
            for (S_CollisionVolume *volume : cell->volumes)
                volume->cell = nullptr;
            cell->volumes.clear();
            m_emptyCells.push_back(cell);
            cell = nullptr;
        }
    }
    // Delete every cell of both lists once.
    std::vector<S_CollisionGridCell *> cells;
    for (std::vector<S_CollisionGridCell *> *list : {&m_freeCells, &m_emptyCells}) {
        for (S_CollisionGridCell *cell : *list) {
            if (std::find(cells.begin(), cells.end(), cell) == cells.end())
                cells.push_back(cell);
        }
        list->clear();
    }
    for (S_CollisionGridCell *cell : cells)
        delete cell;
}

void C_CollisionWorld::SetIgnoredFlags(bool on)
{
    for (uint32_t i = 0; i < m_ignoredCount; ++i) {
        if (on)
            m_ignored[i]->type |= COLLISION_TYPE_IGNORED;
        else
            m_ignored[i]->type &= ~COLLISION_TYPE_IGNORED;
    }
}

void C_CollisionWorld::SetIgnoredVolume(S_CollisionVolume *volume, int unknown)
{
    SetIgnoredFlags(false);
    if (volume) {
        m_ignoredCount = 1;
        m_ignored = &m_ignoredSingle;
        m_ignoredSingle = volume;
    } else {
        m_ignoredCount = 0;
    }
    SetIgnoredFlags(true);
    m_ignoredUnknown = unknown;
}

void C_CollisionWorld::SetIgnoredVolumes(S_CollisionVolume **volumes, uint32_t count, int unknown)
{
    SetIgnoredFlags(false);
    m_ignored = volumes;
    m_ignoredCount = volumes ? count : 0;
    m_ignoredUnknown = unknown;
    SetIgnoredFlags(true);
}

// ============================================================================ overlap tests

bool C_CollisionWorld::TestVolumesOverlap(const S_OverlapQuery &query) const
{
    if (!m_grid)
        return false;
    const float invX = 1.0f / m_grid->cellSizeX;
    const float invZ = 1.0f / m_grid->cellSizeZ;
    int x0 = static_cast<int>((query.min.x - m_grid->minX) * invX - 1.0f);
    int x1 = static_cast<int>((query.max.x - m_grid->minX) * invX + 2.0f);
    int z0 = static_cast<int>((query.min.z - m_grid->minZ) * invZ - 1.0f);
    int z1 = static_cast<int>((query.max.z - m_grid->minZ) * invZ + 2.0f);
    x0 = std::max(x0, 0);
    z0 = std::max(z0, 0);
    x1 = std::min(x1, m_grid->cellsX);
    z1 = std::min(z1, m_grid->cellsZ);
    for (int z = z0; z < z1; ++z) {
        for (int x = x0; x < x1; ++x) {
            const S_CollisionGridCell *cell = m_grid->cells[m_grid->cellsX * z + x];
            if (!cell)
                continue;
            for (const S_CollisionVolume *volume : cell->volumes) {
                if (volume->type & query.excludeTypes)
                    continue;
                switch (volume->flags & COLLISION_SHAPE_MASK) {
                case COLLISION_SHAPE_BOX:
                case COLLISION_SHAPE_SPHERE:
                case COLLISION_SHAPE_SPHERE_44: {
                    // Bounding spheres.
                    const float r = volume->radius + query.radius;
                    if ((query.center - volume->center).Square() <= r * r)
                        return true;
                    break;
                }
                case COLLISION_SHAPE_WORLD_BOX:
                    if (volume->bbox.min.x <= query.max.x && volume->bbox.min.y <= query.max.y &&
                        volume->bbox.min.z <= query.max.z && query.min.x <= volume->bbox.max.x &&
                        query.min.y <= volume->bbox.max.y && query.min.z <= volume->bbox.max.z)
                        return true;
                    break;
                case COLLISION_SHAPE_CONVEX:
                    return true;
                default:
                    break;
                }
            }
        }
    }
    return false;
}

bool C_CollisionWorld::TestPrimitiveOverlap(const void *primitive, const S_OverlapQuery &query)
{
    const uint32_t kind = *static_cast<const uint32_t *>(primitive) & COLLISION_SHAPE_MASK;
    switch (kind) {
    case COLLISION_PRIM_BOX:
    case COLLISION_PRIM_AABB: {
        // World boxes (S_CollisionBox starts with one as well).
        const auto *box = static_cast<const S_CollisionAabb *>(primitive);
        return box->min.x < query.max.x && box->max.x > query.min.x && box->min.z < query.max.z &&
               box->max.z > query.min.z && box->min.y < query.max.y && box->max.y > query.min.y;
    }
    case COLLISION_PRIM_SPHERE: {
        const auto *sphere = static_cast<const S_CollisionSphere *>(primitive);
        const S_vector center(sphere->center.x, sphere->center.y, sphere->center.z);
        const float r = query.radius + sphere->radius;
        return r * r > (center - query.center).Square();
    }
    case COLLISION_PRIM_RECT: {
        const auto *rect = static_cast<const S_CollisionRect *>(primitive);
        const S_vector local = query.center * rect->invMatrix;
        const float r = query.radius;
        return local.x + r > rect->min.x && local.x - r < rect->max.x && local.z + r > rect->min.z &&
               local.z - r < rect->max.z && local.y + r > 0.0f && local.y - r < 0.0f;
    }
    case COLLISION_PRIM_POLE: {
        const auto *pole = static_cast<const S_CollisionPole *>(primitive);
        const float dx = pole->x - query.center.x;
        const float dz = pole->z - query.center.z;
        const float r = query.radius + pole->radius;
        return r * r > dx * dx + dz * dz;
    }
    default:
        return false;
    }
}

bool C_CollisionWorld::TestCellOverlap(const S_CollisionMeshCell &cell, const S_OverlapQuery &query)
{
    for (int i = 0; i < cell.count; ++i) {
        const auto *tri = static_cast<const S_CollisionTriangle *>(cell.entries[i]);
        if (tri->flags & query.excludeFlags)
            continue;
        if (!IsTriangle(tri->flags)) {
            if (TestPrimitiveOverlap(tri, query))
                return true;
            continue;
        }
        // Triangles are sorted by their smallest x (v0) and span to v2's x.
        if (tri->v2->x < query.min.x)
            continue;
        if (tri->v0->x > query.max.x)
            break;
        const float minY = std::min(std::min(tri->v0->y, tri->v2->y), tri->v1->y);
        const float maxY = std::max(std::max(tri->v0->y, tri->v2->y), tri->v1->y);
        if (minY > query.max.y || maxY < query.min.y)
            continue;
        const float minZ = std::min(std::min(tri->v0->z, tri->v2->z), tri->v1->z);
        const float maxZ = std::max(std::max(tri->v0->z, tri->v2->z), tri->v1->z);
        if (minZ > query.max.z || maxZ < query.min.z)
            continue;
        const S_vector d(query.point.x - tri->v0->x, query.point.y - tri->v0->y, query.point.z - tri->v0->z);
        const float distance = std::fabs(d.Dot(tri->normal));
        if (distance * distance <= query.planeDistanceSq)
            return true;
    }
    return false;
}

bool C_CollisionWorld::TestBoxSweep(const I3D_bbox &localBox, const S_matrix &from, const S_matrix &to,
                                    const S_vector4 &unknown4, const S_vector &, const S_vector &velocity,
                                    uint32_t excludeTypes, uint32_t excludeFlags) const
{
    if (!m_mesh)
        return false;
    const bool volumesOnly = excludeFlags == 0xFFFFFFFF;
    if (volumesOnly)
        excludeFlags = 0;
    S_OverlapQuery query;
    query.excludeTypes = excludeTypes | COLLISION_TYPE_IGNORED;
    query.excludeFlags = excludeFlags | COLLISION_FLAG_DISABLED;
    // (The original computes a few cross products of unknown4 / unknown5 here and discards them.)

    // Bounds of the box at both ends of the sweep.
    S_vector corners[8];
    GetBoxCorners(localBox, corners);
    query.min = query.max = corners[0] * from;
    for (int i = 1; i < 8; ++i)
        Expand(query.min, query.max, corners[i] * from);
    for (int i = 0; i < 8; ++i) {
        corners[i] = corners[i] * to;
        Expand(query.min, query.max, corners[i]);
    }
    const S_vector localCenter((localBox.max.x + localBox.min.x) * 0.5f, (localBox.max.y + localBox.min.y) * 0.5f,
                               (localBox.max.z + localBox.min.z) * 0.5f);
    query.center = localCenter * to;
    query.radius = (query.center - corners[7]).Square();
    query.point = velocity * 20.0f + query.center;
    query.unknown4C = unknown4.w;
    query.planeDistanceSq = (query.point - corners[7]).Square();

    if (excludeTypes != 0xFFFFFFFF && TestVolumesOverlap(query))
        return true;
    if (volumesOnly)
        return false;

    const S_CollisionMesh &mesh = *m_mesh;
    const float invX = 1.0f / mesh.cellSizeX;
    const float invZ = 1.0f / mesh.cellSizeZ;
    const float invY = 1.0f / mesh.cellSizeY;
    int x0 = static_cast<int>((query.min.x - mesh.minX) * invX);
    int x1 = static_cast<int>((query.max.x - mesh.minX) * invX + 1.0f);
    int z0 = static_cast<int>((query.min.z - mesh.minZ) * invZ);
    int z1 = static_cast<int>((query.max.z - mesh.minZ) * invZ + 1.0f);
    int y1 = static_cast<int>((query.max.y - mesh.minY) * invY + 1.0f);
    int y0 = static_cast<int>((query.min.y - mesh.minY) * invY);
    x0 = std::max(x0, 0);
    z0 = std::max(z0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, mesh.cellsX);
    z1 = std::min(z1, mesh.cellsZ);
    y1 = std::min(y1, mesh.cellsY);
    bool hit = false;
    for (int z = z0; z < z1; ++z) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const S_CollisionMeshCell *cell = mesh.cells[(mesh.cellsZ * y + z) * mesh.cellsX + x];
                if (query.min.y < cell->maxY)
                    hit |= TestCellOverlap(*cell, query);
            }
        }
    }
    return hit;
}

// ============================================================================ region queries

S_CollisionVolume *C_CollisionWorld::GetVolumeAt(const S_vector &pos, uint32_t excludeTypes) const
{
    if (!m_grid)                                            // (the original assumes the grid)
        return nullptr;
    const S_CollisionGrid &grid = *m_grid;
    const float cellX = (pos.x - grid.minX) / grid.cellSizeX;
    const float cellZ = (pos.z - grid.minZ) / grid.cellSizeZ;
    const int x0 = std::max(static_cast<int>(cellX - 1.0f), 0);
    const int x1 = std::min(static_cast<int>(cellX + 2.0f), grid.cellsX);
    const int z0 = std::max(static_cast<int>(cellZ - 1.0f), 0);
    const int z1 = std::min(static_cast<int>(cellZ + 2.0f), grid.cellsZ);
    const auto inside = [](const I3D_bbox &box, const S_vector &p) {
        return box.min.x <= p.x && p.x <= box.max.x && box.min.y <= p.y && p.y <= box.max.y && box.min.z <= p.z &&
               p.z <= box.max.z;
    };
    for (int z = z0; z < z1; ++z) {
        for (int x = x0; x < x1; ++x) {
            const S_CollisionGridCell *cell = grid.cells[grid.cellsX * z + x];
            if (!cell)
                continue;
            for (S_CollisionVolume *volume : cell->volumes) {
                if (volume->type & excludeTypes)
                    continue;
                switch (volume->flags & COLLISION_SHAPE_MASK) {
                case COLLISION_SHAPE_BOX:
                    if (inside(volume->bbox, pos * *volume->invMatrix))
                        return volume;
                    break;
                case COLLISION_SHAPE_WORLD_BOX:
                    if (inside(volume->bbox, pos))
                        return volume;
                    break;
                case COLLISION_SHAPE_SPHERE:
                case COLLISION_SHAPE_SPHERE_44:
                    if ((volume->center - pos).Square() <= volume->radius * volume->radius)
                        return volume;
                    break;
                default:
                    break;
                }
            }
        }
    }
    return nullptr;
}

int C_CollisionWorld::GetVolumesInRadius(const S_vector &pos, float radius, uint32_t excludeTypes,
                                         std::vector<S_CollisionVolume *> &volumes) const
{
    if (!m_grid)
        return 0;
    volumes.clear();
    const S_CollisionGrid &grid = *m_grid;
    const float invX = 1.0f / grid.cellSizeX;
    const float invZ = 1.0f / grid.cellSizeZ;
    // The cells within the radius and a cell more, with the usual margin.
    const int x0 = std::max(static_cast<int>((pos.x - radius - grid.cellSizeX - grid.minX) * invX - 1.0f), 0);
    const int x1 = std::min(static_cast<int>((pos.x + grid.cellSizeX + radius - grid.minX) * invX + 2.0f), grid.cellsX);
    const int z0 = std::max(static_cast<int>((pos.z - radius - grid.cellSizeZ - grid.minZ) * invZ - 1.0f), 0);
    const int z1 = std::min(static_cast<int>((grid.cellSizeZ + pos.z + radius - grid.minZ) * invZ + 2.0f), grid.cellsZ);
    for (int z = z0; z < z1; ++z) {
        for (int x = x0; x < x1; ++x) {
            const S_CollisionGridCell *cell = grid.cells[grid.cellsX * z + x];
            if (!cell)
                continue;
            for (S_CollisionVolume *volume : cell->volumes) {
                if (volume->type & excludeTypes)
                    continue;
                const float dx = volume->center.x - pos.x;
                const float dz = volume->center.z - pos.z;
                if (dx * dx + dz * dz <= radius * radius &&
                    std::find(volumes.begin(), volumes.end(), volume) == volumes.end())
                    volumes.push_back(volume);
            }
        }
    }
    return static_cast<int>(volumes.size());
}

int C_CollisionWorld::GetFramePrimitives(const I3D_frame *frame, std::vector<void *> &primitives) const
{
    if (!m_mesh)
        return 0;
    primitives.clear();
    const auto collect = [&](auto *items, int count) {
        for (int i = 0; i < count; ++i) {
            if (items[i].frame == frame)
                primitives.push_back(&items[i]);
        }
    };
    collect(m_mesh->boxes, m_mesh->numBoxes);
    collect(m_mesh->aabbs, m_mesh->numAabbs);
    collect(m_mesh->spheres, m_mesh->numSpheres);
    collect(m_mesh->rects, m_mesh->numRects);
    collect(m_mesh->poles, m_mesh->numPoles);
    return static_cast<int>(primitives.size());
}

void C_CollisionWorld::GetWaterHeightRange(float &minY, float &maxY) const
{
    minY = FLT_MAX;
    maxY = -FLT_MAX;
    if (!m_mesh)
        return;
    const auto add = [&](float y) {
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    };
    for (int i = 0; i < m_mesh->numTriangles; ++i) {
        const S_CollisionTriangle &tri = m_mesh->triangles[i];
        if (tri.flags & COLLISION_FLAG_WATER) {
            add(tri.v0->y);
            add(tri.v1->y);
            add(tri.v2->y);
        }
    }
    for (int i = 0; i < m_mesh->numRects; ++i) {
        const S_CollisionRect &rect = m_mesh->rects[i];
        if (!(rect.flags & COLLISION_FLAG_WATER))
            continue;
        const S_vector corners[4] = {S_vector(rect.min.x, 0.0f, rect.min.z), S_vector(rect.min.x, 0.0f, rect.max.z),
                                     S_vector(rect.max.x, 0.0f, rect.min.z), S_vector(rect.max.x, 0.0f, rect.max.z)};
        for (const S_vector &corner : corners)
            add((corner * rect.matrix).y);
    }
}
