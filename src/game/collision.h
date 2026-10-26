// Collision world (g_collisionWorld; source 0x006ee000..0x00702ba0, sphere sweeps
// 0x00702bb0..0x007182c0, wrappers 0x00718700..).
//
// Two kinds of geometry are tested:
//  - the static level collision, loaded from the mission's "GifC" file: triangles and simple
//    primitives (boxes, spheres, rectangles, poles, capsules) listed in the cells of a 3D grid
//    (S_CollisionMesh);
//  - the dynamic volumes of objects (doors, physics objects, destructible parts, vehicles,
//    humans), each filed in one cell of a 2D grid over the level by its center
//    (S_CollisionVolume, S_CollisionGrid).
// Volumes and primitives start with the same flags word: the shape in the low byte,
// COLLISION_FLAG_* bits and the surface material in the top byte.
//
// Sources: collision.cpp (loading, the dynamic grid, ignored volumes, the box sweep overlap
// test, region queries), collision_ray.cpp (ray and segment tests), collision_sweep.cpp
// (sphere sweeps and sliding). Unreferenced code of the original is not ported (see
// _re/PROGRESS.md).
#pragma once

#include <cstdint>
#include <vector>

#include "ls3d/i3d.h"

class C_Actor;
struct S_CollisionGridCell;
struct S_CollisionRay;
struct S_SphereSweep;

// Flags of volumes and primitives (+0x00).
enum : uint32_t {
    COLLISION_SHAPE_MASK = 0xFF,
    COLLISION_FLAG_DISABLED = 0x20,                         // skipped by every query
    COLLISION_FLAG_DYNAMIC = 0x40,                          // an object's S_CollisionVolume
    COLLISION_FLAG_PRIMITIVE = 0x80,                        // a static primitive (not a triangle)
    COLLISION_TRIANGLE_REVERSED = 0x100,                    // wound the other way round (sphere sweeps)
    COLLISION_FLAG_WATER = 0x10000,                         // water surface (triangles, rectangles)
    COLLISION_TRIANGLE_TWO_SIDED = 0x40000,
    COLLISION_VOLUME_SHOOT_THROUGH = 0x100000,              // bullets pass ("trans" parts, some doors)
    // Static geometry the bullets' segment queries skip (and RayTestStatic half of the time).
    COLLISION_FLAG_BULLETS_PASS = 0x200000,
};

// Shapes of dynamic volumes (low byte of the flags).
enum : uint32_t {
    COLLISION_SHAPE_BOX = 0x40,                             // oriented box: local bbox, matrix / invMatrix
    COLLISION_SHAPE_WORLD_BOX = 0x41,                       // bbox in world space
    COLLISION_SHAPE_SPHERE = 0x42,                          // center, radius
    COLLISION_SHAPE_CYLINDER = 0x43,                        // vertical, radius `capsuleRadius` (sphere sweeps only)
    COLLISION_SHAPE_SPHERE_44 = 0x44,                       // tested like a sphere
    COLLISION_SHAPE_SCALED_BOX = 0x46,                      // oriented box with scale (sphere sweeps only)
    COLLISION_SHAPE_CONVEX = 0x49,                          // S_CollisionConvexVolume
    COLLISION_SHAPE_CAPSULE = 0x4A,                         // along local y: half height `radius`
};

// Kinds of static primitives (low byte of the flags).
enum : uint32_t {
    COLLISION_PRIM_BOX = 0x80,
    COLLISION_PRIM_AABB = 0x81,
    COLLISION_PRIM_SPHERE = 0x82,
    COLLISION_PRIM_RECT = 0x83,
    COLLISION_PRIM_POLE = 0x84,
    COLLISION_PRIM_CONVEX = 0x85,                           // S_CollisionConvexPrim
    COLLISION_PRIM_86 = 0x86,                               // 0xA0 bytes, never listed in cells
    COLLISION_PRIM_CAPSULE = 0x87,
};

// S_CollisionVolume::type bits.
enum : uint32_t {
    COLLISION_TYPE_SWITCH = 0x1,                            // a switch's box (C_Switch)
    COLLISION_TYPE_LIGHT_OBJECT = 0x2,                      // cars drive through it (physics objects, document places)
    COLLISION_TYPE_IGNORED = 0x8,                           // excluded from queries (SetIgnoredVolume)
    COLLISION_TYPE_VERSION = 0x20000,                       // destructible model
    COLLISION_TYPE_MINE = 0x40000,                          // trigger volume of a mine
    COLLISION_TYPE_DOOR = 0x200000,                         // physics door
    COLLISION_TYPE_PLACE = 0x800000,                        // the box of a place to use (C_PhotoPlace, C_Radio)
};

// Dynamic volume of an object.
struct S_CollisionVolume {                                  // 0x60 bytes
    uint32_t flags = 0;                                     // +0x00 COLLISION_SHAPE_* / COLLISION_FLAG_*
    I3D_frame *frame = nullptr;                             // +0x04
    uint32_t type = 0;                                      // +0x08 COLLISION_TYPE_* (query filter bits)
    S_CollisionGridCell *cell = nullptr;                    // +0x0C grid cell (owned by the world)
    I3D_bbox bbox = {};                                     // +0x10 local box (world box for COLLISION_SHAPE_WORLD_BOX)
    S_vector center = {0, 0, 0};                            // +0x30 world center of the bounding sphere
    float centerW = 0.0f;
    const S_matrix *matrix = nullptr;                       // +0x40 world matrix without scale
    const S_matrix *invMatrix = nullptr;                    // +0x44 its inverse
    C_Actor *actor = nullptr;                               // +0x48 owning actor
    float radius = 0.0f;                                    // +0x4C bounding sphere (capsules: half height)
    float capsuleRadius = 0.0f;                             // +0x50
    uint8_t unknown54[0x0C] = {};
};

// The material of a volume or primitive is the top byte of its flags.
inline int GetVolumeMaterial(const S_CollisionVolume &volume) { return static_cast<int>(volume.flags >> 24); }

// Convex hulls are given by up to 32 planes in the space of a visual, pointing inward.
constexpr uint32_t COLLISION_CONVEX_MAX_PLANES = 32;

// Dynamic volume of shape COLLISION_SHAPE_CONVEX (the overlap test of TestBoxSweep always
// accepts it).
struct S_CollisionConvexVolume {
    S_CollisionVolume volume;                               // +0x00
    uint32_t numPlanes = 0;                                 // +0x60
    uint32_t unknown64 = 0;
    I3D_visual *visual = nullptr;                           // +0x68
    uint32_t unknown6C = 0;
    S_plane planes[COLLISION_CONVEX_MAX_PLANES] = {};       // +0x70
};

// ---- the dynamic grid ----------------------------------------------------------------

// Cell of the dynamic grid; made when the first volume enters it and kept for reuse.
struct S_CollisionGridCell {                                // 0x18 bytes
    int x = 0;                                              // +0x00
    int z = 0;                                              // +0x04
    std::vector<S_CollisionVolume *> volumes;               // +0x08
};

struct S_CollisionGrid {                                    // 0x1C bytes
    float minX;                                             // +0x00
    float minZ;                                             // +0x04
    float cellSizeX;                                        // +0x08
    float cellSizeZ;                                        // +0x0C
    int cellsX;                                             // +0x10
    int cellsZ;                                             // +0x14
    S_CollisionGridCell **cells;                            // +0x18 [cellsZ][cellsX], null = empty
};

// ---- the static level collision ------------------------------------------------------

// Triangle. Its vertices are sorted by x (v0 smallest, v2 largest) and the triangles of a
// cell by their smallest x. The low byte of the flags (1..7, ClassifyTriangles) says which
// coordinates the ray test solves for.
struct S_CollisionTriangle {                                // 0x20 bytes
    uint32_t flags;                                         // +0x00 COLLISION_TRIANGLE_TWO_SIDED, COLLISION_FLAG_DISABLED (degenerate)
    S_vector4 *v0;                                          // +0x04
    S_vector4 *v1;                                          // +0x08
    S_vector4 *v2;                                          // +0x0C
    S_vector normal;                                        // +0x10
    float d;                                                // +0x1C plane: dot(normal, p) + d = 0
};

struct S_CollisionBox {                                     // COLLISION_PRIM_BOX, 0xC0 bytes
    uint32_t flags;
    I3D_frame *frame;                                       // +0x04
    S_vector worldMin;                                      // +0x08
    S_vector worldMax;                                      // +0x14
    S_vector4 localMin;                                     // +0x20
    S_vector4 localMax;                                     // +0x30
    S_matrix matrix;                                        // +0x40
    S_matrix invMatrix;                                     // +0x80
};

struct S_CollisionAabb {                                    // COLLISION_PRIM_AABB, 0x20 bytes
    uint32_t flags;
    I3D_frame *frame;
    S_vector min;                                           // +0x08
    S_vector max;                                           // +0x14
};

struct S_CollisionSphere {                                  // COLLISION_PRIM_SPHERE, 0x20 bytes
    uint32_t flags;
    I3D_frame *frame;
    float radius;                                           // +0x08
    float unknown0C;
    S_vector4 center;                                       // +0x10
};

// Rectangle in its local plane y = 0.
struct S_CollisionRect {                                    // COLLISION_PRIM_RECT, 0xA0 bytes
    uint32_t flags;
    I3D_frame *frame;
    S_vector min;                                           // +0x08
    S_vector max;                                           // +0x14
    S_matrix matrix;                                        // +0x20
    S_matrix invMatrix;                                     // +0x60
};

// Vertical cylinder without ends.
struct S_CollisionPole {                                    // COLLISION_PRIM_POLE, 0x20 bytes
    uint32_t flags;
    I3D_frame *frame;
    float x;                                                // +0x08
    float z;                                                // +0x0C
    float radius;                                           // +0x10
    uint8_t unknown14[0x0C];
};

// Convex hull in the space of its frame (a visual).
struct S_CollisionConvexPrim {                              // COLLISION_PRIM_CONVEX, 0x220 bytes
    uint32_t flags;
    I3D_frame *frame;                                       // +0x04
    uint32_t numPlanes;                                     // +0x08
    uint32_t unknown0C;
    S_vector center;                                        // +0x10 (world space)
    uint32_t unknown1C;
    S_plane planes[COLLISION_CONVEX_MAX_PLANES];            // +0x20
};

// Capsule along its local y axis.
struct S_CollisionCapsule {                                 // COLLISION_PRIM_CAPSULE, 0x90 bytes
    uint32_t flags;
    I3D_frame *frame;
    float halfHeight;                                       // +0x08
    float radius;                                           // +0x0C
    S_matrix matrix;                                        // +0x10
    S_matrix invMatrix;                                     // +0x50
};

// Cell of the static grid: the triangles and primitives in it and its height range. The
// entries follow the header.
struct S_CollisionMeshCell {
    int count;                                              // +0x00
    void **entries;                                         // +0x04 S_CollisionTriangle / primitives
    float maxY;                                             // +0x08
    float minY;                                             // +0x0C
};

// Header of the level collision; the grid lines and tables follow it in the same block.
struct S_CollisionMesh {                                    // 0xA4 bytes
    float minX;                                             // +0x00
    float minZ;                                             // +0x04
    float maxX;                                             // +0x08
    float maxZ;                                             // +0x0C
    float cellSizeX;                                        // +0x10
    float cellSizeZ;                                        // +0x14
    int cellsX;                                             // +0x18
    int cellsZ;                                             // +0x1C
    float volumeCellSize;                                   // +0x20 cell size of the dynamic grid
    float minY;                                             // +0x24
    float unknown28;
    float cellSizeY;                                        // +0x2C
    int cellsY;                                             // +0x30
    float *gridLinesY;                                      // +0x34
    int unknown38;
    float *gridLinesX;                                      // +0x3C
    float *gridLinesZ;                                      // +0x40
    S_CollisionMeshCell **cells;                            // +0x44 [cellsY][cellsZ][cellsX]
    int numTriangles;                                       // +0x48
    S_CollisionTriangle *triangles;                         // +0x4C
    int numBoxes;                                           // +0x50
    S_CollisionBox *boxes;                                  // +0x54
    int numAabbs;                                           // +0x58
    S_CollisionAabb *aabbs;                                 // +0x5C
    int numSpheres;                                         // +0x60
    S_CollisionSphere *spheres;                             // +0x64
    int numRects;                                           // +0x68
    S_CollisionRect *rects;                                 // +0x6C
    int numPoles;                                           // +0x70
    S_CollisionPole *poles;                                 // +0x74
    int numVertices;                                        // +0x78
    S_vector4 *vertices;                                    // +0x7C
    int numConvexes;                                        // +0x80
    S_CollisionConvexPrim *convexes;                        // +0x84
    int numPrims86;                                         // +0x88
    void *prims86;                                          // +0x8C COLLISION_PRIM_86, 0xA0 bytes each
    int numCapsules;                                        // +0x90
    S_CollisionCapsule *capsules;                           // +0x94
    uint8_t unknown98[0x0C];
};

// ---- queries --------------------------------------------------------------------------

// One volume, primitive or triangle crossed by a segment query.
struct alignas(16) S_CollisionHit {                         // 0x30 bytes
    S_vector pos;                                           // +0x00 where the segment enters it
    float posW;
    S_vector normal;                                        // +0x10 unit
    float normalW;
    S_CollisionVolume *volume;                              // +0x20 (a primitive or triangle: see RayTest)
    float distance;                                         // +0x24 fraction of the segment (sort key)
    uint8_t unknown28[8];
};

// Everything a segment crosses (C_CollisionWorld::QuerySegment()).
struct alignas(16) S_CollisionQuery {
    S_vector from;                                          // +0x00
    float fromW = 0.0f;
    S_vector dir;                                           // +0x10 (its length is the reach)
    float dirW = 0.0f;
    uint32_t excludeTypes = 0;                              // +0x20 (see C_CollisionWorld)
    uint32_t excludeFlags = 0;                              // +0x24
    uint32_t unknown28 = 0;
    uint32_t unknown2C = 0;
    std::vector<S_CollisionHit> hits;                       // +0x30 (16-aligned allocator originally)
};
// Filters of the segment queries of bullets and projectiles (the type bits are not
// identified).
constexpr uint32_t COLLISION_QUERY_SHOT_EXCLUDE_TYPES = 0x48001;
constexpr uint32_t COLLISION_QUERY_SHOT_EXCLUDE_FLAGS = COLLISION_FLAG_BULLETS_PASS;

// A body of spheres moved together (a human's stance spheres; C_CollisionWorld::MoveSpheres*):
// the spheres, the move and the filters in; where the first sphere ends and what it touched
// on the way out.
constexpr int MOVE_CONTACTS_MAX = 10;
struct S_MoveContacts {                                     // 0x1F4 bytes
    uint32_t sphereCount;                                   // +0x00
    float radii[MOVE_CONTACTS_MAX + 1];                     // +0x04 of the spheres
    S_vector4 spheres[MOVE_CONTACTS_MAX];                   // +0x30 centers
    S_vector4 move;                                         // +0xD0
    uint32_t excludeFlags;                                  // +0xE0
    uint32_t excludeTypes;                                  // +0xE4
    uint8_t unknownE8[8];
    S_vector4 pos;                                          // +0xF0 where the first sphere ends
    S_vector4 normals[MOVE_CONTACTS_MAX];                   // +0x100 of the contacts, unit, out of the surfaces
    int sphere[MOVE_CONTACTS_MAX];                          // +0x1A0 the sphere that touched
    S_CollisionVolume *volumes[MOVE_CONTACTS_MAX];          // +0x1C8 what (null: static geometry)
    int count;                                              // +0x1F0
};
// The most contacts of a body sweep (the locomotion sets it before each sweep).
extern float g_moveIterations;                              // 0x008441e4 (10 initially)

// The test filters of the queries: `excludeTypes` (S_CollisionVolume::type bits; -1 = no
// dynamic volumes; COLLISION_TYPE_IGNORED is always added) and `excludeFlags` (flags bits of
// volumes and primitives; -1 = no static geometry; COLLISION_FLAG_DISABLED is always added).
class C_CollisionWorld {                                    // 0x42C bytes
public:
    C_CollisionWorld() = default;                           // 0x006ee010
    ~C_CollisionWorld();                                    // 0x006ee0c0
    C_CollisionWorld(const C_CollisionWorld &) = delete;
    C_CollisionWorld &operator=(const C_CollisionWorld &) = delete;

    // Loads the level collision (a "GifC" file: frame references, then the mesh block);
    // `buildGrid` also makes the dynamic grid over the level.
    void Load(const char *fileName, bool buildGrid);       // 0x006ee940
    // Frees the level collision (unless `keepMesh` and it was loaded into the caller's
    // buffer) and the dynamic grid.
    void Free(bool keepMesh);                               // 0x006ee4f0
    // Empties the dynamic grid and deletes its cells.
    void ClearGrid();                                       // 0x006eff70
    // ClearGrid + Free (end of a mission).
    void Unload(bool keepMesh);                             // 0x00718750
    bool HasMesh() const { return m_mesh != nullptr; }

    // --- dynamic volumes --------------------------------------------------------------
    void AddVolume(S_CollisionVolume *volume);              // 0x006ef7b0
    void RemoveVolume(S_CollisionVolume *volume);           // 0x006ef9b0
    // Re-files the volume after its center moved.
    void MoveVolume(S_CollisionVolume *volume);             // 0x006efbc0
    // Unlinks the cells that became empty and keeps them for reuse (every game tick).
    void ReleaseEmptyCells();                               // 0x006ef700
    // Excludes `volume` (null: none) / the `count` volumes of `volumes` from the queries
    // (COLLISION_TYPE_IGNORED) until the next call.
    void SetIgnoredVolume(S_CollisionVolume *volume, int unknown);   // 0x006efe40
    void SetIgnoredVolumes(S_CollisionVolume **volumes, uint32_t count, int unknown);   // 0x006efee0

    // Whether the box `localBox` (min, max) moving from placement `from` to `to` touches
    // anything (the vehicles' body boxes and supply boxes; a broad test: a triangle within the
    // swept bounds whose plane lies within reach of the point 20 along `velocity` from the box's
    // centre). `velocity`: the vehicles pass their y axis; `unknown4` (the vehicles: the box's
    // move) / `unknown5` (their previous y axis) are only used for discarded values.
    bool TestBoxSweep(const I3D_bbox &localBox, const S_matrix &from, const S_matrix &to, const S_vector4 &unknown4,
                      const S_vector &unknown5, const S_vector &velocity, uint32_t excludeTypes,
                      uint32_t excludeFlags) const;          // 0x006f0ac0 (thunk 0x007189b0)

    // --- ray tests ----------------------------------------------------------------------
    // Cast a ray from `from` along `dir` (its length is the reach) and return what it hits
    // first: a dynamic volume, a static primitive or a triangle (they share the flags word:
    // COLLISION_FLAG_DYNAMIC / COLLISION_FLAG_PRIMITIVE tell them apart), or null. `hitPos`
    // and `hitNormal` (unit, out of the surface) are optional.
    //
    // Tests every static cell around the segment.
    S_CollisionVolume *RayTestBrute(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                                    uint32_t excludeTypes, uint32_t excludeFlags) const;   // 0x006f5c60
    // Walks the static grid along the ray (vertical rays: RayTestBrute).
    S_CollisionVolume *RayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                               uint32_t excludeTypes, uint32_t excludeFlags) const;   // 0x006f8060 (thunk 0x00718830)
    // Only the lowest layer of static cells; the volumes as RayTestDynamic.
    S_CollisionVolume *RayTest2D(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                                 uint32_t excludeTypes, uint32_t excludeFlags) const;   // 0x006f62b0
    // Only static geometry, walking the lowest layer of cells along the ray (vertical rays:
    // RayTestBrute). With COLLISION_FLAG_BULLETS_PASS in `excludeFlags`, that geometry is
    // only excluded half of the time.
    S_CollisionVolume *RayTestStatic(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                                     uint32_t excludeFlags) const;   // 0x006f6c40
    // Only the dynamic volumes (melee hits, vehicles, the AI). A ray starting inside a
    // sphere volume hits it at the start.
    S_CollisionVolume *RayTestDynamic(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                      S_vector *hitNormal, uint32_t excludeTypes,
                                      uint32_t excludeFlags) const;   // 0x006f7cc0

    // Only the water surfaces (COLLISION_FLAG_WATER triangles and rectangles; the normal of a
    // rectangle is always taken as facing down, as for the upward rays of the callers).
    S_CollisionVolume *RayTestWater(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                                    uint32_t excludeFlags) const;   // 0x006fd170

    // Collects what the segment of `query` crosses into query.hits, nearest first, each once,
    // and returns their number (0 without the grids the filters ask for). Tested are the box
    // and sphere volumes, the triangles and the box, rectangle, sphere and pole primitives.
    // The normals point out of static surfaces but into volumes (and those of oriented box
    // volumes are moved by the volume's position, as in the original).
    int QuerySegment(S_CollisionQuery &query) const;        // 0x006ff510

    // --- region queries ------------------------------------------------------------------
    // The first dynamic volume containing `pos` (box, world box and sphere volumes).
    S_CollisionVolume *GetVolumeAt(const S_vector &pos, uint32_t excludeTypes) const;   // 0x006fbcf0
    // The dynamic volumes whose center lies horizontally within `radius` of `pos`, into
    // `volumes` (emptied first); returns their number.
    int GetVolumesInRadius(const S_vector &pos, float radius, uint32_t excludeTypes,
                           std::vector<S_CollisionVolume *> &volumes) const;   // 0x006fcf30
    // The static box, world box, sphere, rectangle and pole primitives of `frame`, into
    // `primitives` (emptied first); returns their number.
    int GetFramePrimitives(const I3D_frame *frame, std::vector<void *> &primitives) const;   // 0x006fba30
    // Height range of the water surfaces (FLT_MAX / -FLT_MAX without water).
    void GetWaterHeightRange(float &minY, float &maxY) const;   // 0x006fddd0

    // --- sphere sweeps (collision_sweep.cpp) ---------------------------------------------
    // Moves a sphere of `radius` from `from` along `dir` and returns what it touches first
    // (see RayTest), `hitPos` the sphere's center at the contact and `hitNormal` the unit normal
    // out of the surface. Tested are the box, sphere, cylinder, convex and capsule volumes, the
    // triangles and all primitives but COLLISION_PRIM_86. Also keeps the squared distance
    // moved (GetSphereTestDistance).
    S_CollisionVolume *SphereTest(const S_vector &from, const S_vector &dir, float radius, S_vector *hitPos,
                                  S_vector *hitNormal, uint32_t excludeTypes,
                                  uint32_t excludeFlags) const;   // 0x007158e0
    // Moves a sphere along `move`, sliding along what it meets (at most 10 contacts, kept in
    // the slide contacts); `endPos` is where it stops and `endNormal` the last contact normal
    // (up without one). Returns the last thing met.
    S_CollisionVolume *SlideSphere(const S_vector &from, const S_vector &move, float radius, S_vector &endPos,
                                   S_vector *endNormal, uint32_t excludeTypes,
                                   uint32_t excludeFlags);   // 0x00717170

    // A contact plane of the last SlideSphere.
    struct S_SlideContact {                                 // 0x30 bytes
        S_vector pos;                                       // +0x00 sphere center at the contact
        float posW;
        S_vector normal;                                    // +0x10 unit, out of the surface
        float normalW;
        float d;                                            // +0x20 plane: dot(normal, p) + d = 0
        S_CollisionVolume *volume;                          // +0x24 null: static geometry
        uint8_t unknown28[8];
    };
    int GetNumSlideContacts() const { return m_numSlideContacts; }
    const S_SlideContact &GetSlideContact(int i) const { return m_slideContacts[i]; }

    // --- body sweeps (collision_body.cpp; S_MoveContacts) --------------------------------
    // Move the spheres of a body together by contacts.move, sliding along what the nearest
    // touches (at most g_moveIterations contacts), and fill in the end and the contacts. They
    // return the last thing touched (null: nothing).
    //
    // Plain: swimming, dead bodies.
    S_CollisionVolume *MoveSpheres(S_MoveContacts &contacts) const;   // 0x0071ba20
    // Walking: the move is horizontal, the spheres lifted 0.3 m to step over low obstacles
    // (not near a wall ahead, not by a sphere above the lowest, not onto bodies or dynamic
    // objects), then dropped by the lift and the vertical move onto the ground (null when
    // nothing is below). Stops at a second lying human in the way.
    S_CollisionVolume *MoveSpheresStepping(S_MoveContacts &contacts) const;   // 0x0071bec0
    // Lying and swimming: lifted 0.1 m (none after touching a human); then the vertical move
    // in up to 3 steps, sliding off humans and out of wedges between walls; without ground
    // there, tested again from 1 m up.
    S_CollisionVolume *MoveSpheresLow(S_MoveContacts &contacts) const;   // 0x0071cba0

private:
    struct S_OverlapQuery;
    // The ray tests of the dynamic volumes in the grid cells around the segment; they keep
    // the nearest hit in `ray`. The first pre-filters the volumes by their distance from
    // the segment, the second by the segment's box (and counts a start inside a sphere).
    bool RayTestVolumes(S_CollisionRay &ray) const;         // 0x006f3020
    bool RayTestVolumesBox(S_CollisionRay &ray) const;      // 0x006f3620
    // Cell range of the static grid covering the segment's box (half-open).
    struct S_CellRange {
        int x0, x1, y0, y1, z0, z1;
    };
    S_CellRange GetCellRange(const S_CollisionRay &ray) const;
    // The sphere sweep against the dynamic volumes in the grid cells around it.
    bool SweepSphereVolumes(S_SphereSweep &sweep) const;    // 0x00702be0
    bool TestVolumesOverlap(const S_OverlapQuery &query) const;   // 0x006f0420
    static bool TestPrimitiveOverlap(const void *primitive, const S_OverlapQuery &query);   // 0x006f0690
    static bool TestCellOverlap(const S_CollisionMeshCell &cell, const S_OverlapQuery &query);   // 0x006f08c0
    // Cell of the dynamic grid containing `pos`, clamped to the grid.
    void GetCellCoords(const S_vector &pos, int &x, int &z) const;
    S_CollisionGridCell *AllocCell(int x, int z);
    void SetIgnoredFlags(bool on);

    int m_unknown00 = 0;                                    // +0x00
    int m_unknown04 = 0;                                    // +0x04
    bool m_ownBuffer = false;                               // +0x08 load into m_buffer (never set)
    void *m_buffer = nullptr;                               // +0x0C
    int m_unknown10 = 0;                                    // +0x10
    uint8_t m_unknown14[0x0C] = {};                         // +0x14
    S_SlideContact m_slideContacts[20] = {};                // +0x20
    int m_numSlideContacts = 0;                             // +0x3E0
    S_CollisionMesh *m_mesh = nullptr;                      // +0x3E4
    int m_meshSize = 0;                                     // +0x3E8
    S_CollisionGrid *m_grid = nullptr;                      // +0x3EC
    std::vector<S_CollisionGridCell *> m_freeCells;         // +0x3F0 unlinked cells for reuse
    std::vector<S_CollisionGridCell *> m_emptyCells;        // +0x400 cells that may have become empty
    int m_ignoredUnknown = 0;                               // +0x410
    S_CollisionVolume *m_ignoredSingle = nullptr;           // +0x414
    S_CollisionVolume **m_ignored = nullptr;                // +0x418
    uint32_t m_ignoredCount = 0;                            // +0x41C
    int m_unknown420 = 0;                                   // +0x420
    int m_unknown424 = 0;                                   // +0x424
    float m_unknown428 = 0.0f;                              // +0x428 1e6 after a load
};
extern C_CollisionWorld g_collisionWorld;                   // 0x008cb480
// Set once a level collision was loaded (not read anywhere).
extern bool g_levelCollisionLoaded;                         // 0x008cb8c0

// g_collisionWorld.RayTestBrute() / RayTest2D() (see C_CollisionWorld).
S_CollisionVolume *CollisionRayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                    S_vector *hitNormal, uint32_t excludeTypes,
                                    uint32_t excludeFlags); // 0x00718770
S_CollisionVolume *CollisionRayTest2D(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                      S_vector *hitNormal, uint32_t excludeTypes,
                                      uint32_t excludeFlags);   // 0x007187a0
// g_collisionWorld.SphereTest() / SlideSphere() (see C_CollisionWorld). The distance variant
// also returns how far the sphere moved to the contact.
S_CollisionVolume *CollisionSphereTest(const S_vector &from, const S_vector &dir, float radius,
                                       S_vector *hitPos, S_vector *hitNormal, uint32_t excludeTypes,
                                       uint32_t excludeFlags);  // 0x00718900
S_CollisionVolume *CollisionSphereTestDistance(const S_vector &from, const S_vector &dir, float radius,
                                               float &distance, S_vector *hitPos, S_vector *hitNormal,
                                               uint32_t excludeTypes, uint32_t excludeFlags);   // 0x00718890
S_CollisionVolume *CollisionSlideSphere(const S_vector &from, const S_vector &move, float radius,
                                        S_vector &endPos, S_vector *endNormal, uint32_t excludeTypes,
                                        uint32_t excludeFlags);   // 0x00718930
// Loads the mission's level collision into g_collisionWorld (with the dynamic grid).
void LoadLevelCollision(const char *fileName);              // 0x00718730
