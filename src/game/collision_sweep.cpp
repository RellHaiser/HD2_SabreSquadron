// Collision world, part 3 (original source 0x00702be0..0x00717170, the convex hull sweeps at
// 0x007e15e0 / 0x007e1b50 and the wrappers at 0x00718890..0x00718930): sphere sweeps.
//
// A sphere moves from `from` along `dir`; every test finds where it first touches its shape and
// keeps the nearest contact in the S_SphereSweep. Polygons (triangles, box faces, rectangles)
// are tested in three steps: the sphere touching the face from the start, the sweep through the
// face's plane inside the face, then the edges the swept sphere passes over (as cylinders) and
// the corner an edge test ends beyond (as a sphere).
#include <algorithm>
#include <cfloat>
#include <cmath>

#include "game/collision.h"
#include "game/collision_internal.h"
#include "ls3d/i3d.h"

// A sphere moved along a segment (0xC0 bytes in the original). `best` is the squared distance
// moved to the nearest contact so far (its initial value limits the sweep; 0 or -1: touching
// from the start); hitPos is the sphere's center at that contact and hitNormal points from the
// center towards the surface.
struct S_SphereSweep {
    S_vector from;                                          // +0x00
    S_vector dir;                                           // +0x10 (its length is the reach)
    float radius = 0.0f;                                    // +0x20
    S_vector boxMin;                                        // +0x24 bounds of the swept sphere
    S_vector boxMax;                                        // +0x30
    S_vector to;                                            // +0x40
    S_vector normDir;                                       // +0x50
    uint32_t excludeFlags = 0;                              // +0x60
    uint32_t excludeTypes = 0;                              // +0x64
    float length = 0.0f;                                    // +0x68
    float best = 0.0f;                                      // +0x6C
    S_vector side;                                          // +0x70 horizontal, across the path
    float slabMin = 0.0f;                                   // +0x80 the path's extent along `side`,
    float slabMax = 0.0f;                                   // +0x84 as -dot(side, p)
    S_vector hitPos;                                        // +0x90
    S_vector hitNormal;                                     // +0xA0
    void *hit = nullptr;                                    // +0xB0
};

float g_sphereTestDistanceSq = 0.0f;                        // 0x008cb8c8

namespace {

void SetSweepEnd(S_SphereSweep &s, const S_vector &to)
{
    s.to = to;
    const float r = s.radius;
    s.boxMin = S_vector(std::min(s.from.x, to.x) - r, std::min(s.from.y, to.y) - r, std::min(s.from.z, to.z) - r);
    s.boxMax = S_vector(std::max(s.from.x, to.x) + r, std::max(s.from.y, to.y) + r, std::max(s.from.z, to.z) + r);
}

// The sweep in an object's space (from and dir transformed; the caller fixes normDir).
S_SphereSweep ToLocalSweep(const S_SphereSweep &s, const S_matrix &invMatrix)
{
    S_SphereSweep local = s;
    local.from = s.from * invMatrix;
    local.dir = s.dir.RotateByMatrix(invMatrix);
    return local;
}

// Takes over the contact of `local` (a sweep in an object's space).
void TakeLocalContact(S_SphereSweep &s, const S_SphereSweep &local, const S_matrix &matrix, void *object)
{
    s.hitPos = local.hitPos * matrix;
    s.best = local.best;
    s.hitNormal = local.hitNormal.RotateByMatrix(matrix);
    s.hit = object;
}

// The plane test of polygon edges: positive when the line through the edge (a, b) passes on
// the outer side of `point` (sidePoint = n x point with the polygon's normal n) or, with
// sidePoint = dir x point, when the line from `point` along `dir` passes outside the edge.
float EdgeSide(const S_vector &a, const S_vector &b, const S_vector &sidePoint, const S_vector &axis)
{
    return (a - b).Dot(sidePoint) + a.Cross(b).Dot(axis);
}

// The sphere against the edge from `a` to `b` (an infinite cylinder clipped to the edge). When
// the contact would lie beyond an end, `corner` gets that end's code (cornerA / cornerB) for
// the corner test that follows.
bool SweepSphereEdge(const S_vector &a, const S_vector &b, S_SphereSweep &s, uint32_t &corner,
                     uint32_t cornerA, uint32_t cornerB)   // 0x007045a0
{
    const S_vector rel = a - s.from;
    const S_vector &nd = s.normDir;
    const S_vector edge = b - a;
    const S_vector axis = edge.Cross(nd);
    const float axisSq = axis.Square();
    if (!(std::fabs(axisSq) > 1e-8f))
        return false;                                       // moving along the edge
    const float f = edge.Dot(nd.Cross(rel));
    const float edgeSq = edge.Square();
    const float disc = (axisSq * s.radius * s.radius - f * f) * edgeSq;
    if (disc < 0.0f)
        return false;
    const float g = axis.Dot(edge.Cross(rel));
    if (g < -std::sqrt(disc))
        return false;
    const float t = (g - std::sqrt(disc)) / axisSq;
    if (!(-s.radius <= t))
        return false;
    // The point of the edge nearest to the sphere's center at the contact.
    const float onEdge = edge.Dot(rel);
    const float u = (edge.Dot(nd) * t - onEdge) / edgeSq;
    if (u < 0.0f) {
        corner = cornerA;
        return false;
    }
    if (u > 1.0f) {
        corner = cornerB;
        return false;
    }
    if (t >= 0.0f) {
        const float distSq = t * t;
        if (!(distSq < s.best))
            return false;
        const S_vector point = edge * u + a;
        s.best = distSq;
        s.hitPos = s.from + nd * t;
        s.hitNormal = point - s.hitPos;
        return true;
    }
    // Overlapping the edge's line at the start.
    if (onEdge > 0.0f) {
        corner = cornerA;
        return false;
    }
    if ((b - s.from).Dot(edge) < 0.0f) {
        corner = cornerB;
        return false;
    }
    const S_vector point = edge * u + a;
    const S_vector center = s.from + nd * t;
    const S_vector normal = point - center;
    if (!(-normal.Dot(nd) <= t))
        return false;
    s.best = -1.0f;
    s.hitNormal = normal;
    s.hitPos = center;
    return true;
}

// The sphere against a point. `aheadOnly`: ignore points behind the start (box corners).
bool SweepSphereVertex(const S_vector &v, S_SphereSweep &s, bool aheadOnly)
{
    const S_vector rel = v - s.from;
    const float projection = rel.Dot(s.normDir);
    if (aheadOnly && !(projection >= 0.0f))
        return false;
    const float disc = s.radius * s.radius - (rel.Square() - projection * projection);
    if (disc < 0.0f)
        return false;
    const float t = projection - std::sqrt(disc);
    const float distSq = t * t;
    if (!(distSq < s.best))
        return false;
    s.hitPos = s.from + s.normDir * t;
    s.hitNormal = v - s.hitPos;
    s.best = distSq;
    return true;
}

// ============================================================================ boxes

// Corners of a box: (x, y, z) with x, y, z = min or max (upper case).
enum : int {
    CORNER_xyz, CORNER_xyZ, CORNER_xYz, CORNER_Xyz, CORNER_XYz, CORNER_XyZ, CORNER_xYZ, CORNER_XYZ,
};
S_vector GetBoxCorner(const S_vector &min, const S_vector &max, int corner)
{
    static const bool upper[8][3] = {
        {false, false, false}, {false, false, true}, {false, true, false}, {true, false, false},
        {true, true, false},   {true, false, true},  {false, true, true},  {true, true, true},
    };
    const bool *u = upper[corner];
    return S_vector(u[0] ? max.x : min.x, u[1] ? max.y : min.y, u[2] ? max.z : min.z);
}

// The faces of a box facing a sweep: their corners in order and their normals (along the
// motion). Each face has four bits for its edges in the sweep's edge mask, from `bit`.
struct S_BoxFace {
    int corners[4];
    float normal[3];
    bool upper;                                             // at the max side
    int bit;
};
const S_BoxFace BOX_FACES[6] = {
    {{CORNER_xyz, CORNER_xyZ, CORNER_xYZ, CORNER_xYz}, {1, 0, 0}, false, 0},    // -x
    {{CORNER_Xyz, CORNER_XYz, CORNER_XYZ, CORNER_XyZ}, {-1, 0, 0}, true, 4},    // +x
    {{CORNER_xYz, CORNER_xYZ, CORNER_XYZ, CORNER_XYz}, {0, -1, 0}, true, 8},    // +y
    {{CORNER_xyz, CORNER_Xyz, CORNER_XyZ, CORNER_xyZ}, {0, 1, 0}, false, 12},   // -y
    {{CORNER_xyz, CORNER_xYz, CORNER_XYz, CORNER_Xyz}, {0, 0, 1}, false, 16},   // -z
    {{CORNER_xyZ, CORNER_XyZ, CORNER_XYZ, CORNER_xYZ}, {0, 0, -1}, true, 20},   // +z
};
enum { FACE_MIN_X, FACE_MAX_X, FACE_MAX_Y, FACE_MIN_Y, FACE_MIN_Z, FACE_MAX_Z };

// The twelve edges, each tested when the sweep passes outside it on one of its faces.
struct S_BoxEdge {
    int a, b;
    uint32_t faceBits;
};
const S_BoxEdge BOX_EDGES[12] = {
    {CORNER_xyz, CORNER_xyZ, 0x008001}, {CORNER_xyz, CORNER_xYz, 0x010008}, {CORNER_xyz, CORNER_Xyz, 0x081000},
    {CORNER_Xyz, CORNER_XYz, 0x040010}, {CORNER_Xyz, CORNER_XyZ, 0x002080}, {CORNER_xyZ, CORNER_XyZ, 0x104000},
    {CORNER_xyZ, CORNER_xYZ, 0x800002}, {CORNER_XyZ, CORNER_XYZ, 0x200040}, {CORNER_xYz, CORNER_XYz, 0x020800},
    {CORNER_XYz, CORNER_XYZ, 0x000420}, {CORNER_XYZ, CORNER_xYZ, 0x400200}, {CORNER_xYZ, CORNER_xYz, 0x000104},
};

// One face of a box: a contact from the start or through the face; otherwise adds the bits of
// the edges the sweep passes outside to `outside`.
bool SweepSphereBoxFace(const S_vector &min, const S_vector &max, const S_BoxFace &face, S_SphereSweep &s,
                        uint32_t &outside)
{
    const S_vector n(face.normal[0], face.normal[1], face.normal[2]);
    const S_vector &facePoint = face.upper ? max : min;
    S_vector corners[4];
    for (int i = 0; i < 4; ++i)
        corners[i] = GetBoxCorner(min, max, face.corners[i]);

    // Touching the face from the start.
    const float dist = n.Dot(s.from - facePoint);
    if (dist <= 0.0f && -s.radius < dist) {
        const S_vector sidePoint = n.Cross(s.from);
        bool inside = true;
        for (int i = 0; i < 4; ++i)
            inside = inside && EdgeSide(corners[i], corners[(i + 1) % 4], sidePoint, n) <= 0.0f;
        if (inside) {
            s.best = 0.0f;
            s.hitPos = s.from + n * (-dist - s.radius);
            s.hitNormal = n;
            return true;
        }
    }
    // The path of the sphere's leading point against the edges.
    const S_vector sidePoint = s.dir.Cross(s.from + n * s.radius);
    for (int i = 0; i < 4; ++i) {
        if (EdgeSide(corners[i], corners[(i + 1) % 4], sidePoint, s.dir) > 0.0f)
            outside |= 1u << (face.bit + i);
    }
    if (outside & (0xFu << face.bit))
        return false;
    // Through the face.
    const float cosine = n.Dot(s.dir);
    if (!(std::fabs(cosine) >= 1e-8f))
        return false;
    const float t = -(n.Dot(s.from - (facePoint - n * s.radius))) / cosine;
    if (!(t >= 0.0f))
        return false;
    const S_vector center = s.from + s.dir * t;
    const float distSq = (center - s.from).Square();
    if (!(distSq < s.best))
        return false;
    s.best = distSq;
    s.hitPos = center;
    s.hitNormal = n;
    return true;
}

// The sweep against the box min..max: the faces facing the motion (x, then y and z when the
// sweep passes over an edge they share with an earlier face), the edges and a corner.
bool SweepSphereAabb(const S_vector &min, const S_vector &max, S_SphereSweep &s)   // 0x00706cd0
{
    const S_vector to = s.from + s.dir;
    const float r = s.radius;
    const auto overlaps = [r](float a, float b, float lo, float hi) {
        return !(std::max(a, b) < lo - r || std::min(a, b) > hi + r);
    };
    if (!overlaps(s.from.x, to.x, min.x, max.x) || !overlaps(s.from.z, to.z, min.z, max.z) ||
        !overlaps(s.from.y, to.y, min.y, max.y))
        return false;

    uint32_t outside = 0;
    const bool towardMinX = !(s.normDir.x >= 0.0f);         // (NaN included)
    if (SweepSphereBoxFace(min, max, BOX_FACES[towardMinX ? FACE_MAX_X : FACE_MIN_X], s, outside))
        return true;
    if (s.normDir.y < 0.0f) {
        if ((outside & 0x24) && SweepSphereBoxFace(min, max, BOX_FACES[FACE_MAX_Y], s, outside))
            return true;
    } else if ((outside & 0x81) && SweepSphereBoxFace(min, max, BOX_FACES[FACE_MIN_Y], s, outside)) {
        return true;
    }
    if (s.normDir.z < 0.0f) {
        if ((outside & 0x4242) && SweepSphereBoxFace(min, max, BOX_FACES[FACE_MAX_Z], s, outside))
            return true;
    } else if ((outside & 0x1818) && SweepSphereBoxFace(min, max, BOX_FACES[FACE_MIN_Z], s, outside)) {
        return true;
    }

    uint32_t corner = 0;                                    // 1 << CORNER_*
    for (const S_BoxEdge &edge : BOX_EDGES) {
        if ((outside & edge.faceBits) &&
            SweepSphereEdge(GetBoxCorner(min, max, edge.a), GetBoxCorner(min, max, edge.b), s, corner, 1u << edge.a,
                            1u << edge.b))
            return true;
    }
    if (!corner)
        return false;
    int index = 0;
    while (!(corner & (1u << index)))
        ++index;
    return SweepSphereVertex(GetBoxCorner(min, max, index), s, true);
}

// ============================================================================ other shapes

// The sweep against a capsule along y in the sweep's space: the cylinder |y| <= halfHeight -
// capsuleRadius and a hemisphere at each end. (Contacts behind the start are accepted.)
bool SweepSphereCapsule(float capsuleRadius, float halfHeight, S_SphereSweep &s)   // 0x0070ffc0
{
    const S_vector &nd = s.normDir;
    const float reach = capsuleRadius + s.radius;
    const float capY = (halfHeight + s.radius) - reach;     // centers of the end hemispheres
    float t = 0.0f;
    float y;                                                // height of the contact on the cylinder
    const float flatSquare = nd.x * nd.x + nd.z * nd.z;
    if (flatSquare <= 1e-8f) {
        y = s.dir.y >= 0.0f ? -FLT_MAX : FLT_MAX;           // vertical: the end met first
    } else {
        const float b = -(s.from.x * nd.x + nd.z * s.from.z);
        if (b <= 0.0f)
            return false;
        const float disc = b * b - (s.from.x * s.from.x + s.from.z * s.from.z - reach * reach) * flatSquare;
        if (disc < 0.0f)
            return false;
        t = (b - std::sqrt(disc)) / flatSquare;
        y = t * nd.y + s.from.y;
    }
    if (y <= capY && reach - (halfHeight + s.radius) <= y) {
        if (t >= 0.0f && (s.best <= t * t || -s.radius > t))
            return false;
        s.best = t * t;
        s.hitPos = s.from + nd * t;
        s.hitNormal = S_vector(s.hitPos.x, 0.0f, s.hitPos.z);
    } else {
        const float centerY = y <= capY ? -capY : capY;
        const S_vector rel(s.from.x, s.from.y - centerY, s.from.z);
        const float b = -rel.Dot(nd);
        if (b <= 0.0f)
            return false;
        const float disc = reach * reach - (rel.Square() - b * b);
        if (disc < 0.0f)
            return false;
        t = b - std::sqrt(disc);
        if (t >= 0.0f && (s.best <= t * t || -s.radius > t))
            return false;
        s.best = t * t;
        s.hitPos = s.from + nd * t;
        s.hitNormal = s.hitPos;
        s.hitNormal.y -= centerY;
    }
    s.hitNormal = -s.hitNormal;
    return true;
}

// The sweep against a convex hull (planes pointing inward in the space of `visual`, pushed out
// by the radius). Volumes (`center` null) ignore contacts at or behind the start; primitives
// only when moving away from `center`. The contact's distance is kept as a fraction of dir.
bool SweepSphereConvex(I3D_visual *visual, uint32_t numPlanes, const S_plane *planes, const S_vector *center,
                       void *object, S_SphereSweep &s)   // 0x007e15e0 (volumes), 0x007e1b50 (primitives)
{
    if (!visual)
        return false;
    const S_matrix &matrix =
        (visual->VisualFlags() & VISFLAGS_WORLD_SPACE) ? IdentityMatrix() : visual->GetMatrix();
    const S_matrix &invMatrix = visual->GetInvMatrix();
    const S_vector from = s.from * invMatrix;
    const S_vector dir = s.dir.RotateByMatrix(invMatrix);
    uint32_t entering = 0;
    float enterT = 0.0f;
    uint32_t enterPlane = 0;
    for (uint32_t i = 0; i < numPlanes; ++i) {
        const float cosine = dir.Dot(planes[i].normal);
        if (cosine > 1e-8f) {
            entering |= 1u << i;
            const float t = -((planes[i].Distance(from) + s.radius) / cosine);
            if (enterT < t) {
                enterT = t;
                enterPlane = i;
            }
        }
    }
    if (!entering)
        return false;
    if (enterT <= 0.0f && (!center || (s.from - *center).Dot(s.dir) > 0.0f))
        return false;
    const S_vector p = dir * enterT + from;
    for (uint32_t i = 0; i < numPlanes; ++i) {
        if (!(entering & (1u << i)) && planes[i].Distance(p) + s.radius < 0.0f)
            return false;
    }
    s.hitPos = p * matrix;
    s.best = enterT * enterT;
    s.hitNormal = planes[enterPlane].normal.RotateByMatrix(matrix);
    s.hit = object;
    return true;
}

// The sweep against a triangle (see SweepSphereBoxFace; the edges are tested in the order
// v2-v0, v1-v2, v0-v1). The original takes a third argument (a pointer to a local of the
// caller, RET 0xC) that it does not read.
bool SweepSphereTriangle(S_CollisionTriangle &tri, S_SphereSweep &s)   // 0x00705130
{
    // Bits of the edges the sweep passes outside (all flipped for a reversed triangle).
    uint32_t outside = (tri.flags & COLLISION_TRIANGLE_REVERSED) ? 7 : 0;
    S_vector n = tri.normal;
    const float approach = n.Dot(s.normDir);
    if (approach < 0.0f) {
        if (!(tri.flags & COLLISION_TRIANGLE_TWO_SIDED))
            return false;
        n = -n;
        outside ^= 7;
    }
    const S_vector v0 = ToVector(*tri.v0);
    const S_vector v1 = ToVector(*tri.v1);
    const S_vector v2 = ToVector(*tri.v2);
    // The plane the sphere's center crosses when its surface reaches the triangle's.
    const float shiftedD = -n.Dot(v0 - n * s.radius);
    const float dist = approach >= 0.0f ? n.Dot(s.from) + tri.d : -tri.normal.Dot(s.from) - tri.d;
    if (dist > 0.0f)
        return false;                                       // behind the triangle

    // Touching the triangle from the start.
    if (-s.radius < dist) {
        const S_vector sidePoint = n.Cross(s.from);
        const float e0 = EdgeSide(v0, v1, sidePoint, n);
        const float e1 = EdgeSide(v1, v2, sidePoint, n);
        const float e2 = EdgeSide(v2, v0, sidePoint, n);
        const bool inside = outside == 0 ? e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f
                                         : outside == 7 && e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f;
        if (inside) {
            s.best = 0.0f;
            s.hitPos = s.from + n * (-dist - s.radius);
            s.hitNormal = n;
            s.hit = &tri;
            return true;
        }
    }
    const S_vector sidePoint = s.dir.Cross(s.from + n * s.radius);
    if (EdgeSide(v0, v1, sidePoint, s.dir) > 0.0f)
        outside ^= 1;
    if (EdgeSide(v1, v2, sidePoint, s.dir) > 0.0f)
        outside ^= 2;
    if (EdgeSide(v2, v0, sidePoint, s.dir) > 0.0f)
        outside ^= 4;

    // Through the triangle.
    const float cosine = n.Dot(s.dir);
    if (std::fabs(cosine) < 1e-8f)
        return false;
    const float t = -((n.Dot(s.from) + shiftedD) / cosine);
    if (outside == 0) {
        if (t < 0.0f)
            return false;
        const S_vector center = s.from + s.dir * t;
        const float distSq = (center - s.from).Square();
        if (!(distSq < s.best))
            return false;
        s.best = distSq;
        s.hitPos = center;
        s.hitNormal = n;
        s.hit = &tri;
        return true;
    }
    // Over the edges, then a corner (codes 1: v2, 2: v0, 4: v1).
    uint32_t corner = 0;
    if (((outside & 4) && SweepSphereEdge(v2, v0, s, corner, 1, 2)) ||
        ((outside & 2) && SweepSphereEdge(v1, v2, s, corner, 4, 1)) ||
        ((outside & 1) && SweepSphereEdge(v0, v1, s, corner, 2, 4))) {
        s.hit = &tri;
        return true;
    }
    const S_vector *vertex = corner == 1 ? &v2 : corner == 2 ? &v0 : corner == 4 ? &v1 : nullptr;
    if (!vertex || !SweepSphereVertex(*vertex, s, false))
        return false;
    s.hit = &tri;
    return true;
}

// The sweep against a static rectangle (in the plane y = 0 of its space; its corners are
// (min.x, min.z), (max.x, min.z), (max.x, max.z), (min.x, max.z), codes 1, 2, 4, 8).
bool SweepSphereRect(S_CollisionRect &rect, S_SphereSweep &s)
{
    S_SphereSweep local = ToLocalSweep(s, rect.invMatrix);
    local.normDir = local.dir.Normalized();
    const float r = local.radius;
    const S_vector to = local.from + local.dir;
    const auto overlaps = [r](float a, float b, float lo, float hi) {
        return !(std::max(a, b) < lo - r || std::min(a, b) > hi + r);
    };
    if (!overlaps(local.from.x, to.x, rect.min.x, rect.max.x) || !overlaps(local.from.z, to.z, rect.min.z, rect.max.z) ||
        !overlaps(local.from.y, to.y, rect.min.y, rect.max.y))
        return false;

    const S_vector c1(rect.min.x, 0.0f, rect.min.z);
    const S_vector c2(rect.max.x, 0.0f, rect.min.z);
    const S_vector c4(rect.max.x, 0.0f, rect.max.z);
    const S_vector c8(rect.min.x, 0.0f, rect.max.z);
    const auto recordFace = [&](const S_vector &n, const S_vector &localPos, float distSq) {
        s.best = distSq;
        s.hitPos = localPos * rect.matrix;
        s.hitNormal = n.RotateByMatrix(rect.matrix);
        s.hit = &rect;
        return true;
    };

    uint32_t outside = 0;                                   // edges, by their first corner's code
    if (std::fabs(local.dir.y) >= 1e-8f) {
        // The face towards the sweep: its normal along the motion and its corners in order
        // (with the edge bits).
        const bool fromAbove = local.normDir.y < 0.0f;
        const S_vector n(0.0f, fromAbove ? -1.0f : 1.0f, 0.0f);
        const S_vector *above[4] = {&c1, &c8, &c4, &c2};
        const S_vector *below[4] = {&c1, &c2, &c4, &c8};
        const S_vector *const *corners = fromAbove ? above : below;
        static const uint32_t ABOVE_EDGE_BITS[4] = {8, 4, 2, 1};
        const float dist = n.y * local.from.y;
        if (dist <= 0.0f && -r < dist) {
            const S_vector sidePoint = n.Cross(local.from);
            bool inside = true;
            for (int i = 0; i < 4; ++i)
                inside = inside && EdgeSide(*corners[i], *corners[(i + 1) % 4], sidePoint, n) <= 0.0f;
            if (inside)
                return recordFace(n, local.from + n * (-dist - r), 0.0f);
        }
        if (fromAbove) {
            const S_vector sidePoint = local.dir.Cross(local.from + n * r);
            for (int i = 0; i < 4; ++i) {
                if (EdgeSide(*corners[i], *corners[(i + 1) % 4], sidePoint, local.dir) > 0.0f)
                    outside |= ABOVE_EDGE_BITS[i];
            }
            const float cosine = n.Dot(local.dir);
            if (outside == 0 && std::fabs(cosine) >= 1e-8f) {
                const float t = -(r - local.from.y) / cosine;
                const S_vector center = local.from + local.dir * t;
                const float distSq = (center - local.from).Square();
                if (t >= 0.0f && distSq < local.best)
                    return recordFace(n, center, distSq);
            }
        } else {
            // Where the path of the leading point meets the plane.
            const float k = 1.0f / local.normDir.y;
            const float leadY = r + local.from.y;
            const float x = local.from.x - k * leadY * local.normDir.x;
            const float z = local.from.z - k * leadY * local.normDir.z;
            if (x < rect.min.x)
                outside = 8;
            if (rect.max.x < x)
                outside |= 2;
            if (z < rect.min.z)
                outside |= 1;
            if (rect.max.z < z)
                outside |= 4;
            if (outside == 0) {
                const float t = (-1.0f / local.dir.y) * (r + local.from.y);
                if (t >= 0.0f) {
                    const S_vector center = local.from + local.dir * t;
                    const float distSq = (center - local.from).Square();
                    if (distSq < local.best)
                        return recordFace(n, center, distSq);
                }
            }
        }
    } else if (std::fabs(local.from.y) < r) {
        // Moving within the plane.
        outside = (local.dir.x > 0.0f ? 1 : 0) | (local.dir.z < 0.0f ? 2 : 0) | (local.dir.x < 0.0f ? 4 : 0) |
                  (local.dir.z > 0.0f ? 8 : 0);
    }

    uint32_t corner = 0;
    if (((outside & 1) && SweepSphereEdge(c1, c2, local, corner, 1, 2)) ||
        ((outside & 2) && SweepSphereEdge(c2, c4, local, corner, 2, 4)) ||
        ((outside & 4) && SweepSphereEdge(c4, c8, local, corner, 4, 8)) ||
        ((outside & 8) && SweepSphereEdge(c8, c1, local, corner, 8, 1))) {
        TakeLocalContact(s, local, rect.matrix, &rect);
        return true;
    }
    const S_vector *point = corner == 1 ? &c1 : corner == 2 ? &c2 : corner == 4 ? &c4 : corner == 8 ? &c8 : nullptr;
    if (!point)
        return false;
    const S_vector rel = *point - local.from;
    const float projection = rel.Dot(local.normDir);
    if (!(projection > 0.0f))
        return false;
    const float disc = r * r - (rel.Square() - projection * projection);
    if (disc < 0.0f)
        return false;
    const float t = projection - std::sqrt(disc);
    const float distSq = t * t;
    if (!(distSq < local.best))
        return false;
    s.hitPos = (local.from + local.normDir * t) * rect.matrix;
    s.best = distSq;
    // (The original takes the local corner minus the world contact point.)
    s.hitNormal = (*point - s.hitPos).RotateByMatrix(rect.matrix);
    s.hit = &rect;
    return true;
}

// The sweep against a static primitive (all kinds but COLLISION_PRIM_86).
bool SweepSpherePrimitive(void *primitive, S_SphereSweep &s)   // 0x00710580
{
    const S_vector &nd = s.normDir;
    switch (*static_cast<const uint32_t *>(primitive) & COLLISION_SHAPE_MASK) {
    case COLLISION_PRIM_BOX: {
        auto &box = *static_cast<S_CollisionBox *>(primitive);
        S_SphereSweep local = ToLocalSweep(s, box.invMatrix);
        local.normDir = local.dir.Normalized();
        if (!SweepSphereAabb(ToVector(box.localMin), ToVector(box.localMax), local))
            return false;
        TakeLocalContact(s, local, box.matrix, primitive);
        return true;
    }
    case COLLISION_PRIM_AABB: {
        auto &box = *static_cast<S_CollisionAabb *>(primitive);
        if (!SweepSphereAabb(box.min, box.max, s))
            return false;
        s.hit = primitive;
        return true;
    }
    case COLLISION_PRIM_SPHERE: {
        auto &sphere = *static_cast<S_CollisionSphere *>(primitive);
        const S_vector center = ToVector(sphere.center);
        const S_vector rel = center - s.from;
        const float projection = rel.Dot(nd);
        if (!(projection > 0.0f))
            return false;
        const float reach = sphere.radius + s.radius;
        const float disc = reach * reach - (rel.Square() - projection * projection);
        if (disc < 0.0f)
            return false;
        const float t = projection - std::sqrt(disc);
        if (!(-s.radius <= t) || !(t < 0.0f || t * t < s.best))
            return false;
        s.hitPos = s.from + nd * t;
        s.hitNormal = center - s.hitPos;
        s.best = t * t;
        s.hit = primitive;
        return true;
    }
    case COLLISION_PRIM_RECT:
        return SweepSphereRect(*static_cast<S_CollisionRect *>(primitive), s);
    case COLLISION_PRIM_POLE: {
        auto &pole = *static_cast<S_CollisionPole *>(primitive);
        const float dx = pole.x - s.from.x;
        const float dz = pole.z - s.from.z;
        const float flatLength = S_vector(nd.x, 0.0f, nd.z).Magnitude();
        if (std::fabs(flatLength) < 1e-8f)
            return false;
        const float inv = 1.0f / flatLength;
        const float projection = nd.x * inv * dx + dz * nd.z * inv;
        if (!(projection > 0.0f))
            return false;
        const float reach = pole.radius + s.radius;
        const float disc = reach * reach - (dx * dx + dz * dz - projection * projection);
        if (!(disc > 0.0f))
            return false;
        const float t = (projection - std::sqrt(disc)) * inv;
        if (!(-s.radius <= t) || !(t < 0.0f || t * t < s.best))
            return false;
        s.hitPos = s.from + nd * t;
        s.hitNormal = S_vector(pole.x - s.hitPos.x, 0.0f, pole.z - s.hitPos.z);
        s.best = t * t;
        s.hit = primitive;
        return true;
    }
    case COLLISION_PRIM_CONVEX: {
        auto &convex = *static_cast<S_CollisionConvexPrim *>(primitive);
        return SweepSphereConvex(static_cast<I3D_visual *>(convex.frame), convex.numPlanes, convex.planes,
                                 &convex.center, primitive, s);
    }
    case COLLISION_PRIM_CAPSULE: {
        auto &capsule = *static_cast<S_CollisionCapsule *>(primitive);
        S_SphereSweep local = ToLocalSweep(s, capsule.invMatrix);
        local.normDir = s.normDir.RotateByMatrix(capsule.invMatrix);
        if (!SweepSphereCapsule(capsule.radius, capsule.halfHeight, local))
            return false;
        TakeLocalContact(s, local, capsule.matrix, primitive);
        return true;
    }
    default:
        return false;
    }
}

// The sweep against the triangles and primitives of a static cell. Triangles wholly to one
// side of the path (along `side`) are skipped.
bool SweepSphereCell(const S_CollisionMeshCell &cell, S_SphereSweep &s)   // 0x007155a0
{
    bool hit = false;
    for (int i = 0; i < cell.count; ++i) {
        void *entry = cell.entries[i];
        const uint32_t flags = *static_cast<const uint32_t *>(entry);
        if (flags & s.excludeFlags)
            continue;
        if (flags & (COLLISION_FLAG_PRIMITIVE | COLLISION_FLAG_DISABLED)) {
            hit |= SweepSpherePrimitive(entry, s);
            continue;
        }
        auto &tri = *static_cast<S_CollisionTriangle *>(entry);
        // The triangles are sorted by their smallest x (v0); v2 has the largest.
        if (!(s.boxMin.x <= tri.v2->x))
            continue;
        if (tri.v0->x > s.boxMax.x)
            break;
        const float minZ = std::min(std::min(tri.v0->z, tri.v2->z), tri.v1->z);
        const float maxZ = std::max(std::max(tri.v0->z, tri.v2->z), tri.v1->z);
        if (!(minZ <= s.boxMax.z && s.boxMin.z <= maxZ))
            continue;
        const float a = -s.side.Dot(ToVector(*tri.v0));
        const float b = -s.side.Dot(ToVector(*tri.v1));
        const float c = -s.side.Dot(ToVector(*tri.v2));
        const bool straddles = s.slabMax <= a ? (c < s.slabMax || b < s.slabMax)
                                              : (s.slabMin < c || s.slabMin < b || s.slabMin < a);
        if (straddles)
            hit |= SweepSphereTriangle(tri, s);
    }
    return hit;
}

} // namespace

// ============================================================================ the world

bool C_CollisionWorld::SweepSphereVolumes(S_SphereSweep &s) const
{
    if (!m_grid)
        return false;
    const S_CollisionGrid &grid = *m_grid;
    const float invX = 1.0f / grid.cellSizeX;
    const float invZ = 1.0f / grid.cellSizeZ;
    const int x0 = std::max(static_cast<int>((s.boxMin.x - grid.minX) * invX - 1.0f), 0);
    const int x1 = std::min(static_cast<int>((s.boxMax.x - grid.minX) * invX + 2.0f), grid.cellsX);
    const int z0 = std::max(static_cast<int>((s.boxMin.z - grid.minZ) * invZ - 1.0f), 0);
    const int z1 = std::min(static_cast<int>((s.boxMax.z - grid.minZ) * invZ + 2.0f), grid.cellsZ);
    const S_vector &nd = s.normDir;
    bool hit = false;
    for (int z = z0; z < z1; ++z) {
        for (int x = x0; x < x1; ++x) {
            const S_CollisionGridCell *cell = grid.cells[grid.cellsX * z + x];
            if (!cell)
                continue;
            for (S_CollisionVolume *volume : cell->volumes) {
                if ((volume->type & s.excludeTypes) || (volume->flags & s.excludeFlags))
                    continue;
                // Within reach of the path?
                const S_vector rel = volume->center - s.from;
                const float distSq = rel.Square();
                const float projection = rel.Dot(nd);
                const float reach = volume->radius + s.radius;
                const float reachSq = reach * reach;
                if ((nd * projection - rel).Square() > reachSq)
                    continue;
                const float pathReach = projection > 0.0f ? (reach + s.length) * (reach + s.length) : reachSq;
                if (!(distSq <= pathReach))
                    continue;

                switch (volume->flags & COLLISION_SHAPE_MASK) {
                case COLLISION_SHAPE_BOX: {
                    S_SphereSweep local = ToLocalSweep(s, *volume->invMatrix);
                    local.normDir = local.dir.Normalized();
                    if (SweepSphereAabb(volume->bbox.min, volume->bbox.max, local)) {
                        TakeLocalContact(s, local, *volume->matrix, volume);
                        hit = true;
                    }
                    break;
                }
                case COLLISION_SHAPE_WORLD_BOX:
                    if (SweepSphereAabb(volume->bbox.min, volume->bbox.max, s)) {
                        s.hit = volume;
                        hit = true;
                    }
                    break;
                case COLLISION_SHAPE_SPHERE:
                    if (!(projection > 0.0f))
                        break;
                    if (distSq < reachSq && distSq >= 1e-8f) {
                        // Overlapping from the start: pushed out horizontally.
                        const float k = reach / std::sqrt(distSq);
                        s.hitPos = S_vector(volume->center.x - rel.x * k, s.from.y, volume->center.z - rel.z * k);
                        s.hitNormal = S_vector(rel.x, 0.01f, rel.z);
                        s.best = 0.0f;
                        s.hit = volume;
                        hit = true;
                    } else {
                        const float disc = reachSq - (distSq - projection * projection);
                        if (disc < 0.0f)
                            break;
                        const float t = projection - std::sqrt(disc);
                        if (!(-s.radius <= t) || !(t * t < s.best))
                            break;
                        s.hitPos = s.from + nd * t;
                        s.hitNormal = volume->center - s.hitPos;
                        s.hitNormal.y = std::max(s.hitNormal.y, 0.0f);
                        s.best = t * t;
                        s.hit = volume;
                        hit = true;
                    }
                    break;
                case COLLISION_SHAPE_CYLINDER: {
                    // (A vertical sweep, a miss or a farther contact ends the whole volume
                    // test without a hit, as in the original.)
                    const float dx = volume->center.x - s.from.x;
                    const float dz = volume->center.z - s.from.z;
                    const float flatLength = S_vector(nd.x, 0.0f, nd.z).Magnitude();
                    if (std::fabs(flatLength) < 1e-8f)
                        return false;
                    const float inv = 1.0f / flatLength;
                    const float flatProjection = nd.x * inv * dx + dz * nd.z * inv;
                    if (!(flatProjection > 0.0f))
                        break;
                    const float cylinderReach = volume->capsuleRadius + s.radius;
                    const float disc =
                        cylinderReach * cylinderReach - (dx * dx + dz * dz - flatProjection * flatProjection);
                    if (!(disc > 0.0f))
                        return false;
                    const float t = (flatProjection - std::sqrt(disc)) * inv;
                    if (-s.radius > t || s.best <= t * t)
                        return false;
                    s.hitPos = s.from + nd * t;
                    s.hitNormal = S_vector(volume->center.x - s.hitPos.x, 0.0f, volume->center.z - s.hitPos.z);
                    s.best = t * t;
                    s.hit = volume;
                    hit = true;
                    break;
                }
                case COLLISION_SHAPE_SPHERE_44: {
                    if (!(projection > 0.0f))
                        break;
                    const float disc = reachSq - (distSq - projection * projection);
                    if (disc < 0.0f)
                        break;
                    const float t = projection - std::sqrt(disc);
                    if (!(-s.radius <= t) || !(t * t < s.best))
                        break;
                    s.hitPos = s.from + nd * t;
                    s.hitNormal = volume->center - s.hitPos;
                    s.best = t * t;
                    s.hit = volume;
                    hit = true;
                    break;
                }
                case COLLISION_SHAPE_SCALED_BOX: {
                    const S_matrix &invMatrix = *volume->invMatrix;
                    S_SphereSweep local = s;
                    local.from = s.from * invMatrix;
                    local.to = s.to * invMatrix;
                    local.radius = s.radius * invMatrix.GetUScale();
                    local.dir = local.to - local.from;
                    local.normDir = local.dir.Normalized();
                    if (SweepSphereAabb(volume->bbox.min, volume->bbox.max, local)) {
                        s.hitPos = local.hitPos * *volume->matrix;
                        s.best = local.best;
                        s.hitNormal = local.hitNormal.RotateByNormMatrix(*volume->matrix);
                        s.hit = volume;
                        hit = true;
                    }
                    break;
                }
                case COLLISION_SHAPE_CONVEX: {
                    auto &convex = reinterpret_cast<S_CollisionConvexVolume &>(*volume);
                    if (SweepSphereConvex(convex.visual, convex.numPlanes, convex.planes, nullptr, volume, s))
                        hit = true;
                    break;
                }
                case COLLISION_SHAPE_CAPSULE: {
                    // (normDir stays in world space, as in the original.)
                    S_SphereSweep local = ToLocalSweep(s, *volume->invMatrix);
                    if (SweepSphereCapsule(volume->capsuleRadius, volume->radius, local)) {
                        TakeLocalContact(s, local, *volume->matrix, volume);
                        hit = true;
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }
    }
    return hit;
}

S_CollisionVolume *C_CollisionWorld::SphereTest(const S_vector &from, const S_vector &dir, float radius,
                                                S_vector *hitPos, S_vector *hitNormal, uint32_t excludeTypes,
                                                uint32_t excludeFlags) const
{
    if (!m_mesh)
        return nullptr;
    const bool testStatic = excludeFlags != COLLISION_EXCLUDE_ALL;
    S_SphereSweep s;
    s.from = from;
    s.dir = dir;
    s.best = dir.Square();
    if (s.best <= 1e-8f)
        return nullptr;
    s.length = std::sqrt(s.best);
    s.normDir = dir * (1.0f / std::sqrt(s.best));
    s.radius = radius;
    s.excludeTypes = excludeTypes | COLLISION_TYPE_IGNORED;
    s.excludeFlags = (testStatic ? excludeFlags : 0) | COLLISION_FLAG_DISABLED;
    SetSweepEnd(s, from + dir);

    const auto result = [&]() {
        g_sphereTestDistanceSq = s.best;
        if (hitPos)
            *hitPos = s.hitPos;
        if (hitNormal) {
            S_vector normal = s.hitNormal;
            normal.Normalize();
            *hitNormal = -normal;
        }
        return static_cast<S_CollisionVolume *>(s.hit);
    };

    bool hit = false;
    if (excludeTypes != COLLISION_EXCLUDE_ALL && m_grid && SweepSphereVolumes(s)) {
        // Only static geometry before the volume matters.
        s.dir = s.hitPos - from;
        s.best = s.dir.Square();
        if (s.best <= 1e-8f)
            return result();
        s.length = std::sqrt(s.best);
        s.normDir = s.dir * (1.0f / std::sqrt(s.best));
        SetSweepEnd(s, s.hitPos);
        hit = true;
    }
    if (testStatic) {
        s.side = S_vector(s.normDir.z, 0.0f, -s.normDir.x);
        if (s.side.Square() < 0.001f)
            s.side = S_vector(1.0f, 0.0f, 0.0f);
        s.side.Normalize();
        const float a = -s.side.Dot(from + s.side * radius);
        const float b = -s.side.Dot(from - s.side * radius);
        s.slabMin = std::min(a, b);
        s.slabMax = std::max(a, b);
        const S_CollisionMesh &mesh = *m_mesh;
        const float invX = 1.0f / mesh.cellSizeX;
        const float invY = 1.0f / mesh.cellSizeY;
        const float invZ = 1.0f / mesh.cellSizeZ;
        const int x0 = std::max(static_cast<int>((s.boxMin.x - mesh.minX) * invX), 0);
        const int x1 = std::min(static_cast<int>((s.boxMax.x - mesh.minX) * invX + 1.0f), mesh.cellsX);
        const int z0 = std::max(static_cast<int>((s.boxMin.z - mesh.minZ) * invZ), 0);
        const int z1 = std::min(static_cast<int>((s.boxMax.z - mesh.minZ) * invZ + 1.0f), mesh.cellsZ);
        const int y0 = std::max(static_cast<int>((s.boxMin.y - mesh.minY) * invY), 0);
        const int y1 = std::min(static_cast<int>((s.boxMax.y - mesh.minY) * invY + 1.0f), mesh.cellsY);
        for (int z = z0; z < z1; ++z) {
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const S_CollisionMeshCell &cell = GetMeshCell(mesh, x, y, z);
                    if (s.boxMin.y <= cell.maxY && cell.minY <= s.boxMax.y)
                        hit |= SweepSphereCell(cell, s);
                }
            }
        }
    }
    if (!hit)
        return nullptr;
    return result();
}

S_CollisionVolume *C_CollisionWorld::SlideSphere(const S_vector &from, const S_vector &move, float radius,
                                                 S_vector &endPos, S_vector *endNormal, uint32_t excludeTypes,
                                                 uint32_t excludeFlags)
{
    S_vector pos = from;
    S_vector step = move;
    S_vector normal(0.0f, 1.0f, 0.0f);
    S_CollisionVolume *last = nullptr;
    m_numSlideContacts = 0;
    for (int attempt = 0; attempt < 10; ++attempt) {
        S_vector hitPos;
        S_CollisionVolume *hit = SphereTest(pos, step, radius, &hitPos, &normal, excludeTypes, excludeFlags);
        if (!hit)
            break;
        last = hit;
        // The rest of the step projected onto the contact plane, lifted off it a little.
        const float d = -normal.Dot(hitPos);
        S_vector target = pos + step;
        const float normalSq = normal.Square();
        if (std::fabs(normalSq) >= 1e-8f)
            target += normal * -((normal.Dot(target) + d) / normalSq);
        pos = hitPos;
        step = target - hitPos + normal * 0.01f;
        // Not through the planes met before.
        for (int i = 0; i < m_numSlideContacts; ++i) {
            const S_SlideContact &contact = m_slideContacts[i];
            const float cosine = step.Dot(contact.normal);
            if (std::fabs(cosine) >= 1e-8f) {
                const float t = -((pos.Dot(contact.normal) + contact.d) / cosine);
                if (t > 0.0f && t < 1.0f)
                    step = step * t * 0.95f;
            }
        }
        S_SlideContact &contact = m_slideContacts[m_numSlideContacts++];
        contact.volume = (hit->flags & COLLISION_FLAG_DYNAMIC) ? hit : nullptr;
        contact.pos = hitPos;
        contact.d = d;
        contact.normal = normal;
    }
    endPos = pos + step;
    if (endNormal)
        *endNormal = normal;
    return last;
}

S_CollisionVolume *CollisionSphereTest(const S_vector &from, const S_vector &dir, float radius, S_vector *hitPos,
                                       S_vector *hitNormal, uint32_t excludeTypes, uint32_t excludeFlags)
{
    return g_collisionWorld.SphereTest(from, dir, radius, hitPos, hitNormal, excludeTypes, excludeFlags);
}

S_CollisionVolume *CollisionSphereTestDistance(const S_vector &from, const S_vector &dir, float radius,
                                               float &distance, S_vector *hitPos, S_vector *hitNormal,
                                               uint32_t excludeTypes, uint32_t excludeFlags)
{
    S_CollisionVolume *hit =
        g_collisionWorld.SphereTest(from, dir, radius, hitPos, hitNormal, excludeTypes, excludeFlags);
    if (hit)
        distance = g_sphereTestDistanceSq > 0.0f ? std::sqrt(g_sphereTestDistanceSq) : 0.0f;
    return hit;
}

S_CollisionVolume *CollisionSlideSphere(const S_vector &from, const S_vector &move, float radius, S_vector &endPos,
                                        S_vector *endNormal, uint32_t excludeTypes, uint32_t excludeFlags)
{
    return g_collisionWorld.SlideSphere(from, move, radius, endPos, endNormal, excludeTypes, excludeFlags);
}
