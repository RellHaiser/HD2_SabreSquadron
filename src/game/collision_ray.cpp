// Collision world, part 2 (original source 0x006f3020..0x006f9520, 0x006fd170..0x006fdbb0,
// 0x006ff510..0x007023d0 and the wrappers at 0x00718770): ray and segment queries.
//
// A query sets up an S_CollisionRay over the segment, tests the dynamic volumes in the grid
// cells around it and then the static cells (all those around the segment, or walking the
// grid along the ray), keeping the nearest hit (ray tests) or all of them (QuerySegment).
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>

#include "game/collision.h"
#include "game/collision_internal.h"
#include "ls3d/i3d.h"

namespace {

// The ray in an object's space (boxes, rectangles and capsules are tested there).
S_CollisionRay ToLocalRay(const S_CollisionRay &ray, const S_matrix &invMatrix)
{
    S_CollisionRay local = ray;
    local.from = ray.from * invMatrix;
    local.dir = ray.dir.RotateByMatrix(invMatrix);
    return local;
}

// Takes over the hit of `local` (ToLocalRay of `ray`) on `object`.
void TakeLocalHit(S_CollisionRay &ray, const S_CollisionRay &local, const S_matrix &matrix, void *object)
{
    ray.hitT = local.hitT;
    ray.hitNormal = local.hitNormal.RotateByMatrix(matrix);
    ray.hit = object;
}

// Entry and exit fractions of the ray between the planes `lo` and `hi` of one axis; false if it
// runs outside them or enters them beyond `hitT`.
bool ClipSlab(float from, float dir, float lo, float hi, float hitT, float &enter, float &exit)
{
    if (std::fabs(dir) <= 1e-8f) {
        if (from < lo || hi < from)
            return false;
        enter = 0.0f;
        exit = FLT_MAX;
        return true;
    }
    const float inv = 1.0f / dir;
    enter = ((dir > 0.0f ? lo : hi) - from) * inv;
    if (hitT < enter)
        return false;
    exit = ((dir > 0.0f ? hi : lo) - from) * inv;
    return true;
}

// Fraction of the ray at which it crosses the plane of a triangle. The stored normals point
// into the surface: a one-sided triangle is only crossed from the side the normal points away
// from.
bool GetTrianglePlaneHit(const S_CollisionTriangle &tri, const S_CollisionRay &ray, float &t)
{
    const float cosine = ray.dir.Dot(tri.normal);
    if (tri.flags & COLLISION_TRIANGLE_TWO_SIDED ? std::fabs(cosine) < 1e-8f : cosine < 1e-8f)
        return false;
    t = -(tri.normal.Dot(ray.from) + tri.d) / cosine;
    return true;
}

// Whether the point of the ray at `t` (in the triangle's plane) lies in the triangle (see
// TRIANGLE_KIND_*).
bool IsInTriangle(const S_CollisionTriangle &tri, const S_CollisionRay &ray, float t)
{
    // Solve p = u * e2 + v * e1 in the triangle's projection plane.
    const S_vector v0 = ToVector(*tri.v0);
    const S_vector p = (ray.from + ray.dir * t) - v0;
    const S_vector e1 = ToVector(*tri.v1) - v0;
    const S_vector e2 = ToVector(*tri.v2) - v0;
    const uint32_t kind = tri.flags & TRIANGLE_KIND_MASK;
    float u;
    switch (kind) {
    case TRIANGLE_KIND_YZ_EDGE_Z: u = p.y / e2.y; break;
    case TRIANGLE_KIND_YZ: u = (e1.y * p.z - e1.z * p.y) / (e1.y * e2.z - e2.y * e1.z); break;
    case TRIANGLE_KIND_XZ_EDGE_Z:
    case TRIANGLE_KIND_XY_EDGE_Y: u = p.x / e2.x; break;
    case TRIANGLE_KIND_XZ_EDGE_X: u = p.z / e2.z; break;
    case TRIANGLE_KIND_XZ: u = (e1.x * p.z - e1.z * p.x) / (e2.z * e1.x - e1.z * e2.x); break;
    case TRIANGLE_KIND_XY: u = (e1.x * p.y - e1.y * p.x) / (e2.y * e1.x - e1.y * e2.x); break;
    default: return false;
    }
    if (!(u >= 0.0f && u <= 1.0f))
        return false;
    float v;
    switch (kind) {
    case TRIANGLE_KIND_YZ_EDGE_Z:
    case TRIANGLE_KIND_XZ_EDGE_Z: v = (p.z - e2.z * u) / e1.z; break;
    case TRIANGLE_KIND_YZ:
    case TRIANGLE_KIND_XY_EDGE_Y: v = (p.y - e2.y * u) / e1.y; break;
    default: v = (p.x - e2.x * u) / e1.x; break;
    }
    return v >= 0.0f && u + v <= 1.0f;
}

bool RayTestTriangle(S_CollisionTriangle &tri, S_CollisionRay &ray)
{
    float t;
    if (!GetTrianglePlaneHit(tri, ray, t) || t < 0.0f || t >= ray.hitT || !IsInTriangle(tri, ray, t))
        return false;
    ray.hitT = t;
    ray.hitNormal = tri.normal;
    ray.hit = &tri;
    return true;
}

// Whether a triangle of a cell can be crossed by the segment, by the bounds (the cells' triangles
// are sorted by their smallest x, v0; v2 has the largest). `pastSegment`: no later triangle of the
// cell can be.
bool IsTriangleNearSegment(const S_CollisionTriangle &tri, const S_CollisionRay &ray, bool &pastSegment)
{
    pastSegment = false;
    if (tri.v2->x < ray.boxMin.x)
        return false;
    if (ray.boxMax.x < tri.v0->x) {
        pastSegment = true;
        return false;
    }
    const float minZ = std::min(std::min(tri.v0->z, tri.v2->z), tri.v1->z);
    const float maxZ = std::max(std::max(tri.v0->z, tri.v2->z), tri.v1->z);
    return minZ <= ray.boxMax.z && maxZ >= ray.boxMin.z;
}

// Ray against a static rectangle (in the plane y = 0 of its space). `upNormal`: record the
// local up axis as the normal whichever way the ray crosses (the water test).
bool RayTestRect(S_CollisionRect &rect, S_CollisionRay &ray, bool upNormal)
{
    const S_CollisionRay local = ToLocalRay(ray, rect.invMatrix);
    if (std::fabs(local.dir.y) <= 1e-8f)
        return false;
    const float t = -(local.from.y / local.dir.y);
    if (!(t < ray.hitT && t > 0.0f))
        return false;
    const float x = local.dir.x * t + local.from.x;
    const float z = local.from.z + local.dir.z * t;
    if (!(rect.min.x <= x && x <= rect.max.x && rect.min.z <= z && z <= rect.max.z))
        return false;
    ray.hitT = t;
    const float y = upNormal || local.dir.y > 0.0f ? 1.0f : -1.0f;
    ray.hitNormal = S_vector(0.0f, y, 0.0f).RotateByMatrix(rect.matrix);
    ray.hit = &rect;
    return true;
}

// Ray against one dynamic volume. `startInsideHits`: a ray starting inside a sphere volume
// hits it at the start.
bool RayTestVolume(S_CollisionVolume &volume, S_CollisionRay &ray, bool startInsideHits)
{
    switch (volume.flags & COLLISION_SHAPE_MASK) {
    case COLLISION_SHAPE_BOX: {
        S_CollisionRay local = ToLocalRay(ray, *volume.invMatrix);
        if (!RayTestAabb(volume.bbox.min, volume.bbox.max, local))
            return false;
        TakeLocalHit(ray, local, *volume.matrix, &volume);
        return true;
    }
    case COLLISION_SHAPE_WORLD_BOX:
        if (!RayTestAabb(volume.bbox.min, volume.bbox.max, ray))
            return false;
        ray.hit = &volume;
        return true;
    case COLLISION_SHAPE_SPHERE:
    case COLLISION_SHAPE_SPHERE_44:
        if (startInsideHits && (volume.center - ray.from).Square() < volume.radius * volume.radius) {
            ray.hitT = 0.0f;
            ray.hitNormal = volume.center - ray.from;
        } else if (!RayTestSphere(volume.center, volume.radius, ray)) {
            return false;
        }
        ray.hit = &volume;
        return true;
    case COLLISION_SHAPE_CONVEX: {
        auto &convex = reinterpret_cast<S_CollisionConvexVolume &>(volume);
        return RayTestConvex(convex.visual, convex.numPlanes, convex.planes, &volume, ray);
    }
    case COLLISION_SHAPE_CAPSULE: {
        S_CollisionRay local = ToLocalRay(ray, *volume.invMatrix);
        if (!RayTestCapsule(volume.capsuleRadius, volume.radius, local))
            return false;
        TakeLocalHit(ray, local, *volume.matrix, &volume);
        return true;
    }
    default:
        return false;
    }
}

// Distance of the volume's center from the ray's line (a zero-length ray accepts every volume).
float GetLineDistance(const S_CollisionVolume &volume, const S_CollisionRay &ray)
{
    const float dirSquare = ray.dir.Square();
    if (dirSquare < 1e-8f)
        return -1e16f;
    const S_vector rel = ray.from - volume.center;
    return (rel - ray.dir * (rel.Dot(ray.dir) / dirSquare)).Magnitude();
}

// Calls test(volume) for the volumes passing the ray's filters (`checkFlags`: both, else only
// excludeTypes) in the dynamic grid cells around the segment (with a margin of a cell).
template <class Test>
void ForEachGridVolume(const S_CollisionGrid &grid, const S_CollisionRay &ray, bool checkFlags, Test test)
{
    const uint32_t excludeFlags = checkFlags ? ray.excludeFlags : 0;
    const float invX = 1.0f / grid.cellSizeX;
    const float invZ = 1.0f / grid.cellSizeZ;
    const int x0 = std::max(static_cast<int>((ray.boxMin.x - grid.minX) * invX - 1.0f), 0);
    const int x1 = std::min(static_cast<int>((ray.boxMax.x - grid.minX) * invX + 2.0f), grid.cellsX);
    const int z0 = std::max(static_cast<int>((ray.boxMin.z - grid.minZ) * invZ - 1.0f), 0);
    const int z1 = std::min(static_cast<int>((ray.boxMax.z - grid.minZ) * invZ + 2.0f), grid.cellsZ);
    for (int z = z0; z < z1; ++z) {
        for (int x = x0; x < x1; ++x) {
            const S_CollisionGridCell *cell = grid.cells[grid.cellsX * z + x];
            if (!cell)
                continue;
            for (S_CollisionVolume *volume : cell->volumes) {
                if (!(volume->type & ray.excludeTypes) && !(volume->flags & excludeFlags))
                    test(*volume);
            }
        }
    }
}

// Walks the static grid's columns (x-major, for rays mostly along x) or rows (z-major) that
// the segment crosses, in the ray's direction, calling visit(x, z) for the cells of each that
// the segment spans over it. `untilHit`: stop once the nearest hit lies before the next
// column / row.
template <class Visit>
void WalkCells(const S_CollisionMesh &mesh, const S_CollisionRay &ray, int x0, int x1, int z0, int z1, bool untilHit,
               Visit visit)
{
    const bool zMajor = std::fabs(ray.dir.x) < std::fabs(ray.dir.z);
    // The main axis is walked; the cross axis gives the span over each column / row.
    const float *lines = zMajor ? mesh.gridLinesZ : mesh.gridLinesX;   // cell edges along the main axis
    const float fromMain = zMajor ? ray.from.z : ray.from.x;
    const float dirMain = zMajor ? ray.dir.z : ray.dir.x;
    const float fromCross = zMajor ? ray.from.x : ray.from.z;
    const float dirCross = zMajor ? ray.dir.x : ray.dir.z;
    const float minCross = zMajor ? mesh.minX : mesh.minZ;
    const float invCellCross = 1.0f / (zMajor ? mesh.cellSizeX : mesh.cellSizeZ);
    const int main0 = zMajor ? z0 : x0;
    const int main1 = zMajor ? z1 : x1;
    const int cross0 = zMajor ? x0 : z0;
    const int cross1 = zMajor ? x1 : z1;
    const bool backward = dirMain < 0.0f;
    // Whether the cross coordinate is smallest at the lower edge of a column / row.
    const bool lowerEdgeFirst = (dirCross < 0.0f) == backward;
    const float invDirMain = 1.0f / dirMain;
    const auto crossCell = [&](float line, float bias) {
        return static_cast<int>(((line - fromMain) * invDirMain * dirCross + fromCross - minCross) * invCellCross +
                                bias);
    };
    // (Compares the indices, as the original: an infinite ray's far end converts to INT_MIN, and
    // main1 - main0 would overflow into two billion columns. A step along the ground shorter
    // than 1e-4 makes MoveSpheresStepping's wall probe infinite, in the original too.)
    for (int i = backward ? main1 - 1 : main0; backward ? i >= main0 : i < main1; backward ? --i : ++i) {
        const int c0 = std::max(crossCell(lowerEdgeFirst ? lines[i] : lines[i + 1], 0.0f), cross0);
        const int c1 = std::min(crossCell(lowerEdgeFirst ? lines[i + 1] : lines[i], 1.0f), cross1);
        for (int c = c0; c < c1; ++c) {
            if (zMajor)
                visit(c, i);
            else
                visit(i, c);
        }
        // Done once the nearest hit lies before the far edge of the column / row. (Walking
        // backward along z the original compares with the near edge, which costs a row.)
        const float hitMain = ray.hitT * dirMain + fromMain;
        if (untilHit && (backward ? lines[zMajor ? i + 1 : i] <= hitMain : hitMain <= lines[i + 1]))
            break;
    }
}

} // namespace

// ============================================================================ the ray

S_CollisionRay MakeCollisionRay(const S_vector &from, const S_vector &dir, uint32_t excludeTypes,
                                uint32_t excludeFlags)
{
    S_CollisionRay ray;
    ray.from = from;
    ray.dir = dir;
    SetCollisionRayEnd(ray, from + dir);
    ray.excludeTypes = excludeTypes | COLLISION_TYPE_IGNORED;
    ray.excludeFlags = excludeFlags | COLLISION_FLAG_DISABLED;
    ray.dirSquare = dir.Square();
    ray.dirLength = std::sqrt(ray.dirSquare);
    return ray;
}

void SetCollisionRayEnd(S_CollisionRay &ray, const S_vector &to)
{
    ray.to = to;
    ray.boxMin = S_vector(std::min(ray.from.x, to.x), std::min(ray.from.y, to.y), std::min(ray.from.z, to.z));
    ray.boxMax = S_vector(std::max(ray.from.x, to.x), std::max(ray.from.y, to.y), std::max(ray.from.z, to.z));
}

S_CollisionVolume *GetCollisionRayHit(const S_CollisionRay &ray, S_vector *hitPos, S_vector *hitNormal)
{
    if (!ray.hit)
        return nullptr;
    if (hitPos)
        *hitPos = ray.dir * ray.hitT + ray.from;
    if (hitNormal) {
        *hitNormal = -ray.hitNormal;
        hitNormal->Normalize();
    }
    return static_cast<S_CollisionVolume *>(ray.hit);
}

// ============================================================================ single shapes

bool RayTestAabb(const S_vector &min, const S_vector &max, S_CollisionRay &ray)
{
    float enterX, exitX, enterY, exitY, enterZ, exitZ;
    if (!ClipSlab(ray.from.x, ray.dir.x, min.x, max.x, ray.hitT, enterX, exitX) ||
        !ClipSlab(ray.from.z, ray.dir.z, min.z, max.z, ray.hitT, enterZ, exitZ))
        return false;
    if (enterZ > exitX || enterX > exitZ)
        return false;
    if (!ClipSlab(ray.from.y, ray.dir.y, min.y, max.y, ray.hitT, enterY, exitY))
        return false;
    // The face entered last is the one hit.
    if (enterX <= enterZ) {
        if (enterY < enterZ) {
            if (exitY < enterZ || enterZ < 0.0f)
                return false;
            ray.hitNormal = S_vector(0.0f, 0.0f, ray.dir.z > 0.0f ? 1.0f : -1.0f);
            ray.hitT = enterZ;
            return true;
        }
    } else if (enterY < enterX) {
        if (exitY < enterX || enterX < 0.0f)
            return false;
        ray.hitNormal = S_vector(ray.dir.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
        ray.hitT = enterX;
        return true;
    }
    if (enterY > exitX || enterY > exitZ || enterY < 0.0f)
        return false;
    ray.hitNormal = S_vector(0.0f, ray.dir.y > 0.0f ? 1.0f : -1.0f, 0.0f);
    ray.hitT = enterY;
    return true;
}

bool RayTestCapsule(float radius, float halfHeight, S_CollisionRay &ray)
{
    const float capY = halfHeight - radius;                 // centers of the end hemispheres
    float t = 0.0f;
    float y;                                                // height of the hit on the cylinder
    const float flatSquare = ray.dir.x * ray.dir.x + ray.dir.z * ray.dir.z;
    if (flatSquare <= 1e-8f) {
        // Vertical ray: it can only hit the end it meets first.
        y = ray.dir.y >= 0.0f ? -FLT_MAX : FLT_MAX;
    } else {
        const float b = -(ray.from.x * ray.dir.x + ray.dir.z * ray.from.z);
        const float disc =
            b * b - (ray.from.x * ray.from.x + ray.from.z * ray.from.z - radius * radius) * flatSquare;
        if (disc < 0.0f)
            return false;
        t = (b - std::sqrt(disc)) / flatSquare;
        if (ray.hitT < t || t < 0.0f)
            return false;
        y = t * ray.dir.y + ray.from.y;
        if (y <= capY && -capY <= y) {
            ray.hitT = t;
            ray.hitNormal = S_vector(t * ray.dir.x + ray.from.x, 0.0f, t * ray.dir.z + ray.from.z);
            return true;
        }
    }
    // The hemisphere at the end the cylinder hit lies beyond.
    const float centerY = y <= capY ? -capY : capY;
    const S_vector rel(ray.from.x, ray.from.y - centerY, ray.from.z);
    const float b = -rel.Dot(ray.dir);
    const float disc = b * b - (rel.Square() - radius * radius) * ray.dirSquare;
    if (disc < 0.0f)
        return false;
    t = (b - std::sqrt(disc)) / ray.dirSquare;
    if (ray.hitT < t)
        return false;
    ray.hitT = t;
    ray.hitNormal = ray.dir * t + ray.from;
    ray.hitNormal.y -= centerY;
    return true;
}

bool RayTestSphere(const S_vector &center, float radius, S_CollisionRay &ray)
{
    const S_vector toCenter = center - ray.from;
    const float projection = toCenter.Dot(ray.dir);
    const float disc = projection * projection - (toCenter.Square() - radius * radius) * ray.dirSquare;
    if (disc < 0.0f)
        return false;
    const float t = (projection - std::sqrt(disc)) / ray.dirSquare;
    if (!(t >= 0.0f && t < ray.hitT))
        return false;
    ray.hitT = t;
    ray.hitNormal = center - (ray.dir * t + ray.from);
    return true;
}

bool RayTestConvex(I3D_visual *visual, uint32_t numPlanes, const S_plane *planes, void *object, S_CollisionRay &ray)
{
    if (!visual)
        return false;
    const S_matrix &matrix =
        (visual->VisualFlags() & VISFLAGS_WORLD_SPACE) ? IdentityMatrix() : visual->GetMatrix();
    const S_matrix &invMatrix = visual->GetInvMatrix();
    const S_vector from = ray.from * invMatrix;
    const S_vector dir = ray.dir.RotateByMatrix(invMatrix);

    // The ray enters the hull through the planes it runs along; the last of those is hit.
    uint32_t entering = 0;
    float enterT = 0.0f;
    uint32_t enterPlane = 0;
    for (uint32_t i = 0; i < numPlanes; ++i) {
        const float cosine = dir.Dot(planes[i].normal);
        if (cosine > 1e-8f) {
            entering |= 1u << i;
            const float t = -planes[i].Distance(from) / cosine;
            if (enterT < t) {
                enterT = t;
                enterPlane = i;
            }
        }
    }
    if (!entering || ray.hitT < enterT)
        return false;
    // The entry point must be inside the planes the ray leaves through.
    const S_vector p = dir * enterT + from;
    for (uint32_t i = 0; i < numPlanes; ++i) {
        if (!(entering & (1u << i)) && planes[i].Distance(p) < 0.0f)
            return false;
    }
    ray.hitT = enterT;
    ray.hitNormal = planes[enterPlane].normal.RotateByMatrix(matrix);
    ray.hit = object;
    return true;
}

bool RayTestPrimitive(void *primitive, S_CollisionRay &ray)
{
    switch (*static_cast<const uint32_t *>(primitive) & COLLISION_SHAPE_MASK) {
    case COLLISION_PRIM_BOX: {
        auto &box = *static_cast<S_CollisionBox *>(primitive);
        S_CollisionRay local = ToLocalRay(ray, box.invMatrix);
        if (!RayTestAabb(ToVector(box.localMin), ToVector(box.localMax), local))
            return false;
        TakeLocalHit(ray, local, box.matrix, primitive);
        return true;
    }
    case COLLISION_PRIM_AABB: {
        auto &box = *static_cast<S_CollisionAabb *>(primitive);
        if (!RayTestAabb(box.min, box.max, ray))
            return false;
        ray.hit = primitive;
        return true;
    }
    case COLLISION_PRIM_SPHERE: {
        auto &sphere = *static_cast<S_CollisionSphere *>(primitive);
        if (ray.dirSquare == 0.0f || !RayTestSphere(ToVector(sphere.center), sphere.radius, ray))
            return false;
        ray.hit = primitive;
        return true;
    }
    case COLLISION_PRIM_RECT:
        return RayTestRect(*static_cast<S_CollisionRect *>(primitive), ray, false);
    case COLLISION_PRIM_POLE: {
        // Infinite vertical cylinder: tested in the horizontal plane.
        auto &pole = *static_cast<S_CollisionPole *>(primitive);
        const float dx = pole.x - ray.from.x;
        const float dz = pole.z - ray.from.z;
        const float flatLength = S_vector(ray.dir.x, 0.0f, ray.dir.z).Magnitude();
        if (std::fabs(flatLength) < 1e-8f)
            return false;
        const float inv = 1.0f / flatLength;
        const float projection = ray.dir.x * inv * dx + dz * ray.dir.z * inv;
        const float disc = pole.radius * pole.radius - (dx * dx + dz * dz - projection * projection);
        if (disc < 0.0f)
            return false;
        const float t = (projection - std::sqrt(disc)) * inv;
        if (!(t >= 0.0f && t < ray.hitT))
            return false;
        ray.hitNormal = S_vector(pole.x - (t * ray.dir.x + ray.from.x), 0.0f, pole.z - (t * ray.dir.z + ray.from.z));
        ray.hitT = t;
        ray.hit = primitive;
        return true;
    }
    case COLLISION_PRIM_CONVEX: {
        auto &convex = *static_cast<S_CollisionConvexPrim *>(primitive);
        return RayTestConvex(static_cast<I3D_visual *>(convex.frame), convex.numPlanes, convex.planes, primitive,
                             ray);
    }
    case COLLISION_PRIM_CAPSULE: {
        auto &capsule = *static_cast<S_CollisionCapsule *>(primitive);
        S_CollisionRay local = ToLocalRay(ray, capsule.invMatrix);
        if (!RayTestCapsule(capsule.radius, capsule.halfHeight, local))
            return false;
        TakeLocalHit(ray, local, capsule.matrix, primitive);
        return true;
    }
    default:
        return false;
    }
}

bool RayTestCell(const S_CollisionMeshCell &cell, S_CollisionRay &ray)
{
    bool hit = false;
    for (int i = 0; i < cell.count; ++i) {
        void *entry = cell.entries[i];
        const uint32_t flags = *static_cast<const uint32_t *>(entry);
        if (flags & ray.excludeFlags)
            continue;
        if (flags & (COLLISION_FLAG_PRIMITIVE | COLLISION_FLAG_DISABLED)) {
            hit |= RayTestPrimitive(entry, ray);
            continue;
        }
        auto &tri = *static_cast<S_CollisionTriangle *>(entry);
        bool pastSegment;
        if (IsTriangleNearSegment(tri, ray, pastSegment))
            hit |= RayTestTriangle(tri, ray);
        else if (pastSegment)
            break;
    }
    return hit;
}

// ============================================================================ the world

bool C_CollisionWorld::RayTestVolumes(S_CollisionRay &ray) const
{
    bool hit = false;
    if (!m_grid)
        return false;
    ForEachGridVolume(*m_grid, ray, true, [&](S_CollisionVolume &volume) {
        if (GetLineDistance(volume, ray) > volume.radius)
            return;
        // Within reach of the segment's start or end.
        const S_vector toCenter = volume.center - ray.from;
        const float reach = toCenter.Dot(ray.dir) > 0.0f ? ray.dirLength + volume.radius : volume.radius;
        if (toCenter.Square() > reach * reach)
            return;
        hit |= RayTestVolume(volume, ray, false);
    });
    return hit;
}

bool C_CollisionWorld::RayTestVolumesBox(S_CollisionRay &ray) const
{
    bool hit = false;
    if (!m_grid)
        return false;
    ForEachGridVolume(*m_grid, ray, true, [&](S_CollisionVolume &volume) {
        const float r = volume.radius;
        const S_vector &c = volume.center;
        if (GetLineDistance(volume, ray) > r || ray.boxMin.x > c.x + r || c.x - r > ray.boxMax.x ||
            ray.boxMin.z > c.z + r || c.z - r > ray.boxMax.z)
            return;
        hit |= RayTestVolume(volume, ray, true);
    });
    return hit;
}

C_CollisionWorld::S_CellRange C_CollisionWorld::GetCellRange(const S_CollisionRay &ray) const
{
    const S_CollisionMesh &mesh = *m_mesh;
    const float invX = 1.0f / mesh.cellSizeX;
    const float invY = 1.0f / mesh.cellSizeY;
    const float invZ = 1.0f / mesh.cellSizeZ;
    S_CellRange range;
    range.x0 = std::max(static_cast<int>((ray.boxMin.x - mesh.minX) * invX), 0);
    range.x1 = std::min(static_cast<int>((ray.boxMax.x - mesh.minX) * invX + 1.0f), mesh.cellsX);
    range.y0 = std::max(static_cast<int>((ray.boxMin.y - mesh.minY) * invY), 0);
    range.y1 = std::min(static_cast<int>((ray.boxMax.y - mesh.minY) * invY + 1.0f), mesh.cellsY);
    range.z0 = std::max(static_cast<int>((ray.boxMin.z - mesh.minZ) * invZ), 0);
    range.z1 = std::min(static_cast<int>((ray.boxMax.z - mesh.minZ) * invZ + 1.0f), mesh.cellsZ);
    return range;
}

S_CollisionVolume *C_CollisionWorld::RayTestBrute(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                                  S_vector *hitNormal, uint32_t excludeTypes,
                                                  uint32_t excludeFlags) const
{
    if (!m_mesh)
        return nullptr;
    const bool testStatic = excludeFlags != COLLISION_EXCLUDE_ALL;
    S_CollisionRay ray = MakeCollisionRay(from, dir, excludeTypes, testStatic ? excludeFlags : 0);
    if (excludeTypes != COLLISION_EXCLUDE_ALL && m_grid)
        RayTestVolumes(ray);
    if (testStatic) {
        const S_CellRange range = GetCellRange(ray);
        for (int z = range.z0; z < range.z1; ++z) {
            for (int y = range.y0; y < range.y1; ++y) {
                for (int x = range.x0; x < range.x1; ++x) {
                    const S_CollisionMeshCell &cell = GetMeshCell(*m_mesh, x, y, z);
                    if (ray.boxMin.y <= cell.maxY && cell.minY <= ray.boxMax.y)
                        RayTestCell(cell, ray);
                }
            }
        }
    }
    return GetCollisionRayHit(ray, hitPos, hitNormal);
}

S_CollisionVolume *C_CollisionWorld::RayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                             S_vector *hitNormal, uint32_t excludeTypes,
                                             uint32_t excludeFlags) const
{
    if (std::fabs(dir.z) + std::fabs(dir.x) < 1e-8f)
        return RayTestBrute(from, dir, hitPos, hitNormal, excludeTypes, excludeFlags);
    if (!m_mesh)
        return nullptr;
    const bool testStatic = excludeFlags != COLLISION_EXCLUDE_ALL;
    S_CollisionRay ray = MakeCollisionRay(from, dir, excludeTypes, testStatic ? excludeFlags : 0);
    // (The cells are filtered by the height range of the whole segment.)
    const float minY = ray.boxMin.y;
    const float maxY = ray.boxMax.y;
    // Only static geometry before a hit volume matters.
    if (excludeTypes != COLLISION_EXCLUDE_ALL && m_grid && RayTestVolumes(ray))
        SetCollisionRayEnd(ray, dir * ray.hitT + from);
    if (testStatic) {
        const S_CellRange range = GetCellRange(ray);
        WalkCells(*m_mesh, ray, range.x0, range.x1, range.z0, range.z1, true, [&](int x, int z) {
            for (int y = range.y0; y < range.y1; ++y) {
                const S_CollisionMeshCell &cell = GetMeshCell(*m_mesh, x, y, z);
                if (minY <= cell.maxY && cell.minY <= maxY)
                    RayTestCell(cell, ray);
            }
        });
    }
    return GetCollisionRayHit(ray, hitPos, hitNormal);
}

S_CollisionVolume *C_CollisionWorld::RayTest2D(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                               S_vector *hitNormal, uint32_t excludeTypes,
                                               uint32_t excludeFlags) const
{
    if (!m_mesh)
        return nullptr;
    const bool testStatic = excludeFlags != COLLISION_EXCLUDE_ALL;
    S_CollisionRay ray = MakeCollisionRay(from, dir, excludeTypes, testStatic ? excludeFlags : 0);
    if (excludeTypes != COLLISION_EXCLUDE_ALL && m_grid)
        RayTestVolumesBox(ray);
    if (testStatic) {
        const S_CellRange range = GetCellRange(ray);
        for (int z = range.z0; z < range.z1; ++z) {
            for (int x = range.x0; x < range.x1; ++x) {
                const S_CollisionMeshCell &cell = GetMeshCell(*m_mesh, x, 0, z);
                if (ray.boxMin.y <= cell.maxY)
                    RayTestCell(cell, ray);
            }
        }
    }
    return GetCollisionRayHit(ray, hitPos, hitNormal);
}

S_CollisionVolume *C_CollisionWorld::RayTestStatic(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                                   S_vector *hitNormal, uint32_t excludeFlags) const
{
    if (std::fabs(dir.z) + std::fabs(dir.x) < 1e-8f)
        return RayTestBrute(from, dir, hitPos, hitNormal, COLLISION_EXCLUDE_ALL, excludeFlags);
    if (!m_mesh)
        return nullptr;
    S_CollisionRay ray = MakeCollisionRay(from, dir, 0, excludeFlags);
    if ((excludeFlags & COLLISION_FLAG_BULLETS_PASS) && (std::rand() & 8))
        ray.excludeFlags ^= COLLISION_FLAG_BULLETS_PASS;
    const S_CellRange range = GetCellRange(ray);
    WalkCells(*m_mesh, ray, range.x0, range.x1, range.z0, range.z1, true, [&](int x, int z) {
        const S_CollisionMeshCell &cell = GetMeshCell(*m_mesh, x, 0, z);
        if (ray.boxMin.y <= cell.maxY)
            RayTestCell(cell, ray);
    });
    return GetCollisionRayHit(ray, hitPos, hitNormal);
}

S_CollisionVolume *C_CollisionWorld::RayTestDynamic(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                                    S_vector *hitNormal, uint32_t excludeTypes,
                                                    uint32_t excludeFlags) const
{
    S_CollisionRay ray = MakeCollisionRay(from, dir, excludeTypes, excludeFlags);
    if (!m_grid || !RayTestVolumesBox(ray))
        return nullptr;
    return GetCollisionRayHit(ray, hitPos, hitNormal);
}

// ============================================================================ water

namespace {

// RayTestCell for the water surfaces.
bool RayTestWaterCell(const S_CollisionMeshCell &cell, S_CollisionRay &ray)   // 0x006fd710
{
    bool hit = false;
    for (int i = 0; i < cell.count; ++i) {
        void *entry = cell.entries[i];
        const uint32_t flags = *static_cast<const uint32_t *>(entry);
        if (!(flags & COLLISION_FLAG_WATER) || (flags & ray.excludeFlags))
            continue;
        if (flags & (COLLISION_FLAG_PRIMITIVE | COLLISION_FLAG_DISABLED)) {
            if ((flags & COLLISION_SHAPE_MASK) == COLLISION_PRIM_RECT)   // 0x006fdbb0
                hit |= RayTestRect(*static_cast<S_CollisionRect *>(entry), ray, true);
            continue;
        }
        auto &tri = *static_cast<S_CollisionTriangle *>(entry);
        bool pastSegment;
        if (IsTriangleNearSegment(tri, ray, pastSegment))
            hit |= RayTestTriangle(tri, ray);
        else if (pastSegment)
            break;
    }
    return hit;
}

} // namespace

S_CollisionVolume *C_CollisionWorld::RayTestWater(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                                  S_vector *hitNormal, uint32_t excludeFlags) const
{
    if (!m_mesh)
        return nullptr;
    S_CollisionRay ray = MakeCollisionRay(from, dir, 0, excludeFlags);
    const S_CellRange range = GetCellRange(ray);
    for (int z = range.z0; z < range.z1; ++z) {
        for (int y = range.y0; y < range.y1; ++y) {
            for (int x = range.x0; x < range.x1; ++x) {
                const S_CollisionMeshCell &cell = GetMeshCell(*m_mesh, x, y, z);
                if (ray.boxMin.y <= cell.maxY && cell.minY <= ray.boxMax.y)
                    RayTestWaterCell(cell, ray);
            }
        }
    }
    return GetCollisionRayHit(ray, hitPos, hitNormal);
}

// ============================================================================ segment queries

namespace {

// Adds a hit to the list (sorted by distance) unless its object is listed already at or
// before that distance.
void AddHit(std::vector<S_CollisionHit> &hits, void *object, float distance, const S_vector &normal)
{
    auto at = hits.begin();
    for (; at != hits.end(); ++at) {
        if (at->volume == object)
            return;
        if (distance < at->distance)
            break;
    }
    S_CollisionHit hit = {};
    hit.normal = normal;
    hit.volume = static_cast<S_CollisionVolume *>(object);
    hit.distance = distance;
    hits.insert(at, hit);
}

// The segment against a dynamic volume (box, world box and sphere volumes; normals into the
// volume).
void CollectVolume(S_CollisionVolume &volume, S_CollisionRay &ray, std::vector<S_CollisionHit> &hits)
{
    switch (volume.flags & COLLISION_SHAPE_MASK) {
    case COLLISION_SHAPE_BOX: {
        S_CollisionRay local = ToLocalRay(ray, *volume.invMatrix);
        local.hitT = 1.0f;
        // (The normal is transformed as a point, which adds the volume's position.)
        if (RayTestAabb(volume.bbox.min, volume.bbox.max, local))
            AddHit(hits, &volume, local.hitT, local.hitNormal * *volume.matrix);
        break;
    }
    case COLLISION_SHAPE_WORLD_BOX:
        ray.hitT = 1.0f;
        if (RayTestAabb(volume.bbox.min, volume.bbox.max, ray))
            AddHit(hits, &volume, ray.hitT, ray.hitNormal);
        break;
    case COLLISION_SHAPE_SPHERE:
    case COLLISION_SHAPE_SPHERE_44: {
        const S_vector toCenter = volume.center - ray.from;
        const float projection = toCenter.Dot(ray.dir);
        const float disc =
            projection * projection - (toCenter.Square() - volume.radius * volume.radius) * ray.dirSquare;
        if (disc < 0.0f)
            break;
        const float t = (projection - std::sqrt(disc)) / ray.dirSquare;
        if (t >= 0.0f && t < 1.0f)
            AddHit(hits, &volume, t, volume.center - (ray.dir * t + ray.from));
        break;
    }
    default:
        break;
    }
}

// The segment against a static primitive (box, world box, sphere, rectangle and pole; normals
// out of the surface).
void CollectPrimitive(void *primitive, S_CollisionRay &ray, std::vector<S_CollisionHit> &hits)   // 0x00701720
{
    switch (*static_cast<const uint32_t *>(primitive) & COLLISION_SHAPE_MASK) {
    case COLLISION_PRIM_BOX: {
        auto &box = *static_cast<S_CollisionBox *>(primitive);
        S_CollisionRay local = ToLocalRay(ray, box.invMatrix);
        local.hitT = 1.0f;
        if (RayTestAabb(ToVector(box.localMin), ToVector(box.localMax), local))
            AddHit(hits, primitive, local.hitT, -local.hitNormal.RotateByMatrix(box.matrix));
        break;
    }
    case COLLISION_PRIM_AABB: {
        auto &box = *static_cast<S_CollisionAabb *>(primitive);
        ray.hitT = 1.0f;
        if (RayTestAabb(box.min, box.max, ray))
            AddHit(hits, primitive, ray.hitT, -ray.hitNormal);
        break;
    }
    case COLLISION_PRIM_SPHERE: {
        auto &sphere = *static_cast<S_CollisionSphere *>(primitive);
        const S_vector center = ToVector(sphere.center);
        const S_vector toCenter = center - ray.from;
        const float projection = toCenter.Dot(ray.dir);
        const float disc =
            projection * projection - (toCenter.Square() - sphere.radius * sphere.radius) * ray.dirSquare;
        if (disc < 0.0f)
            break;
        const float t = (projection - std::sqrt(disc)) / ray.dirSquare;
        if (t >= 0.0f && t <= 1.0f)
            AddHit(hits, primitive, t, (ray.dir * t + ray.from) - center);
        break;
    }
    case COLLISION_PRIM_RECT: {
        auto &rect = *static_cast<S_CollisionRect *>(primitive);
        const S_CollisionRay local = ToLocalRay(ray, rect.invMatrix);
        if (std::fabs(local.dir.y) <= 1e-8f)
            break;
        const float t = -(local.from.y / local.dir.y);
        if (!(t <= 1.0f && t > 0.0f))
            break;
        const float x = local.dir.x * t + local.from.x;
        const float z = local.from.z + local.dir.z * t;
        if (rect.min.x <= x && x <= rect.max.x && rect.min.z <= z && z <= rect.max.z) {
            const S_vector normal(0.0f, local.dir.y <= 0.0f ? 1.0f : -1.0f, 0.0f);
            AddHit(hits, primitive, t, normal.RotateByMatrix(rect.matrix));
        }
        break;
    }
    case COLLISION_PRIM_POLE: {
        auto &pole = *static_cast<S_CollisionPole *>(primitive);
        const float dx = pole.x - ray.from.x;
        const float dz = pole.z - ray.from.z;
        const float flatLength = S_vector(ray.dir.x, 0.0f, ray.dir.z).Magnitude();
        if (std::fabs(flatLength) < 1e-8f)
            break;
        const float inv = 1.0f / flatLength;
        const float projection = ray.dir.x * inv * dx + dz * ray.dir.z * inv;
        const float disc = pole.radius * pole.radius - (dx * dx + dz * dz - projection * projection);
        if (disc < 0.0f)
            break;
        const float t = (projection - std::sqrt(disc)) * inv;
        if (t >= 0.0f && t <= 1.0f) {
            const S_vector normal(t * ray.dir.x + ray.from.x - pole.x, 0.0f, t * ray.dir.z + ray.from.z - pole.z);
            AddHit(hits, primitive, t, normal);
        }
        break;
    }
    default:
        break;
    }
}

// The segment against the triangles and primitives of a static cell.
void CollectCell(const S_CollisionMeshCell &cell, S_CollisionRay &ray, std::vector<S_CollisionHit> &hits)   // 0x00700f90
{
    for (int i = 0; i < cell.count; ++i) {
        void *entry = cell.entries[i];
        const uint32_t flags = *static_cast<const uint32_t *>(entry);
        if (flags & ray.excludeFlags)
            continue;
        if (flags & (COLLISION_FLAG_PRIMITIVE | COLLISION_FLAG_DISABLED)) {
            CollectPrimitive(entry, ray, hits);
            continue;
        }
        auto &tri = *static_cast<S_CollisionTriangle *>(entry);
        bool pastSegment;
        float t;
        if (IsTriangleNearSegment(tri, ray, pastSegment) && GetTrianglePlaneHit(tri, ray, t) && t >= 0.0f &&
            t <= 1.0f && IsInTriangle(tri, ray, t))
            AddHit(hits, &tri, t, -tri.normal);
    }
}

} // namespace

int C_CollisionWorld::QuerySegment(S_CollisionQuery &query) const
{
    std::vector<S_CollisionHit> &hits = query.hits;
    hits.clear();
    // (The original sets the static filter only along with the volumes'; when they are
    // excluded it tests the static geometry with an undefined one.)
    S_CollisionRay ray = MakeCollisionRay(query.from, query.dir, query.excludeTypes, query.excludeFlags);
    if (query.excludeTypes != COLLISION_EXCLUDE_ALL) {
        if (!m_grid)
            return 0;
        // (Only the type filter applies to the volumes.)
        ForEachGridVolume(*m_grid, ray, false, [&](S_CollisionVolume &volume) {
            const float r = volume.radius;
            const S_vector &c = volume.center;
            if (GetLineDistance(volume, ray) > r || ray.boxMin.x > c.x + r || c.x - r > ray.boxMax.x ||
                ray.boxMin.z > c.z + r || c.z - r > ray.boxMax.z || ray.boxMin.y > c.y + r ||
                c.y - r > ray.boxMax.y)
                return;
            CollectVolume(volume, ray, hits);
        });
    }
    if (query.excludeFlags != COLLISION_EXCLUDE_ALL) {
        if (!m_mesh)
            return 0;
        ray.hitT = 1.0f;
        const S_CellRange range = GetCellRange(ray);
        WalkCells(*m_mesh, ray, range.x0, range.x1, range.z0, range.z1, false, [&](int x, int z) {
            for (int y = range.y0; y < range.y1; ++y) {
                const S_CollisionMeshCell &cell = GetMeshCell(*m_mesh, x, y, z);
                if (ray.boxMin.y <= cell.maxY && cell.minY <= ray.boxMax.y)
                    CollectCell(cell, ray, hits);
            }
        });
    }
    for (S_CollisionHit &hit : hits) {
        hit.pos = query.dir * hit.distance + query.from;
        hit.normal.Normalize();
    }
    return static_cast<int>(hits.size());
}

S_CollisionVolume *CollisionRayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                                    uint32_t excludeTypes, uint32_t excludeFlags)
{
    return g_collisionWorld.RayTestBrute(from, dir, hitPos, hitNormal, excludeTypes, excludeFlags);
}

S_CollisionVolume *CollisionRayTest2D(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                      S_vector *hitNormal, uint32_t excludeTypes, uint32_t excludeFlags)
{
    return g_collisionWorld.RayTest2D(from, dir, hitPos, hitNormal, excludeTypes, excludeFlags);
}
