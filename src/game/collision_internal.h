// Internals shared by the collision world's sources (collision*.cpp): the ray query and the
// ray tests of single primitives.
#pragma once

#include <cstdint>

#include "game/collision.h"

// Filter value that excludes everything: no dynamic volumes (excludeTypes) / no static
// geometry (excludeFlags).
constexpr uint32_t COLLISION_EXCLUDE_ALL = 0xFFFFFFFF;

// Triangle kinds (low byte of the flags, set by ClassifyTriangles): the plane in which the ray
// test solves the barycentric coordinates, and the axis of the edge v0-v1 where it runs along
// one (which simplifies the solution).
enum : uint32_t {
    TRIANGLE_KIND_MASK = 0xFF,
    TRIANGLE_KIND_YZ_EDGE_Z = 1,                            // facing x, (v1 - v0).y == 0
    TRIANGLE_KIND_YZ = 2,                                   // facing x
    TRIANGLE_KIND_XZ_EDGE_Z = 3,                            // facing y, (v1 - v0).x == 0
    TRIANGLE_KIND_XZ_EDGE_X = 4,                            // facing y, (v1 - v0).z == 0
    TRIANGLE_KIND_XZ = 5,                                   // facing y
    TRIANGLE_KIND_XY_EDGE_Y = 6,                            // facing z, (v1 - v0).x == 0
    TRIANGLE_KIND_XY = 7,                                   // facing z
};

// A ray query in progress (0x90 bytes in the original): the segment from `from` to
// `to` = from + dir and the nearest hit so far. The tests only record hits nearer than hitT;
// the normals they record point into the surface (along the ray), except RayTestCapsule's.
struct S_CollisionRay {
    S_vector from;
    S_vector dir;
    S_vector to;
    S_vector boxMin;                                        // bounds of the segment
    S_vector boxMax;
    uint32_t excludeFlags = 0;                              // COLLISION_FLAG_DISABLED always
    uint32_t excludeTypes = 0;                              // COLLISION_TYPE_IGNORED always
    float dirSquare = 0.0f;
    float dirLength = 0.0f;
    float hitT = 1.0f;                                      // fraction of dir to the nearest hit
    S_vector hitNormal = {0, 0, 0};
    void *hit = nullptr;                                    // volume, primitive or triangle
};

inline S_vector ToVector(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

// Squared distance the sphere of the last C_CollisionWorld::SphereTest moved to its contact.
extern float g_sphereTestDistanceSq;                        // 0x008cb8c8

inline S_CollisionMeshCell &GetMeshCell(const S_CollisionMesh &mesh, int x, int y, int z)
{
    return *mesh.cells[(mesh.cellsZ * y + z) * mesh.cellsX + x];
}

// A ray along the segment from `from` to from + dir with no hit yet; the filters get
// COLLISION_TYPE_IGNORED and COLLISION_FLAG_DISABLED added.
S_CollisionRay MakeCollisionRay(const S_vector &from, const S_vector &dir, uint32_t excludeTypes,
                                uint32_t excludeFlags);
// Moves the end of the segment (and its box).
void SetCollisionRayEnd(S_CollisionRay &ray, const S_vector &to);
// The result of a ray query: what was hit (null: nothing), where, and the unit normal out of
// the surface.
S_CollisionVolume *GetCollisionRayHit(const S_CollisionRay &ray, S_vector *hitPos, S_vector *hitNormal);

// Ray tests of single shapes; each records a hit nearer than ray.hitT in `ray` (hitT, hitNormal
// and, where noted, hit) and returns whether it did.
//
// Box min..max in the ray's space (entry face; not `hit`).
bool RayTestAabb(const S_vector &min, const S_vector &max, S_CollisionRay &ray);   // 0x006f46e0
// Capsule along y in the ray's space: the cylinder |y| <= halfHeight - radius and a
// hemisphere at each end (not `hit`). Unlike the other tests it records the normal out of
// the surface, and it does not reject end hits behind the ray's start.
bool RayTestCapsule(float radius, float halfHeight, S_CollisionRay &ray);   // 0x006f4d70
// Sphere (not `hit`).
bool RayTestSphere(const S_vector &center, float radius, S_CollisionRay &ray);
// Convex hull of `numPlanes` planes (pointing inward) in the space of `visual`; records
// `object` as the hit. A start inside the hull hits it at the start.
bool RayTestConvex(I3D_visual *visual, uint32_t numPlanes, const S_plane *planes, void *object,
                   S_CollisionRay &ray);                    // 0x007e1360 (volumes), 0x007e18d0 (primitives)
// Static primitive (COLLISION_PRIM_*).
bool RayTestPrimitive(void *primitive, S_CollisionRay &ray);   // 0x006f50d0
// The triangles and primitives of a static cell.
bool RayTestCell(const S_CollisionMeshCell &cell, S_CollisionRay &ray);   // 0x006f57c0
