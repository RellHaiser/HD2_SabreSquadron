// Part 2 of the collision harness (included by collision_test.cpp): the sphere sweeps (collision_sweep.cpp) and the
// body sweeps (collision_body.cpp).
//
// Two oracles. The original executable (original_exe.h): its SphereTest, SlideSphere and body sweeps are called on
// the port's own world (the same layout; its dynamic grid through a copy with MSVC6's vectors) with the same
// queries, and must give the same contacts within the x87's 24-bit precision against the port's floats. And my own
// exact sweep: the sphere's center moves from `from` along `dir`; it touches a convex shape when the distance from
// the center to the shape's core (a point, a segment, a line, a triangle, a box, a rectangle) comes down to the sum
// of the radii. That distance is convex along the path, so the first contact is found by a ternary search for the
// nearest point of the path and a bisection before it (in doubles). Where the original's own sweeps approximate
// (Approximate: the shapes' edges and corners), mine differs and the original's decides.
//
// Scenes: the levels' primitives; a level of my own (triangles, a rectangle, capsule and convex primitives); volumes
// of every sphere-sweep shape among triangles; ground of my own (a floor, steps, walls, ramps) with humans, standing
// and lying, and crates for the body sweeps. Slides and body moves are chains: each step's rounding goes into the
// next, so where they differ the cause is shown. The steps are written out again here (HybridSlide,
// HybridMoveSpheres) and must give the port's chain over the port's own sweeps; over the original's sweeps they give
// the original's chain (the difference: rounding inside the sweeps) or the port's (rounding inside the steps); or
// the port moved by 0.1 mm gives the original's; or the chain was stuck touching one place when they parted.
namespace {

struct D3 {
    double x, y, z;
};
D3 D(const S_vector &v) { return {v.x, v.y, v.z}; }
D3 operator+(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
D3 operator-(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
D3 operator*(D3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double Dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double Len(D3 a) { return std::sqrt(Dot(a, a)); }
D3 MulD(D3 p, const S_matrix &m, bool point)
{
    D3 r = {p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0], p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1],
            p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2]};
    if (point)
        r = r + D3{m.m[3][0], m.m[3][1], m.m[3][2]};
    return r;
}
double Clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
// The nearest point of a triangle to p (Ericson's).
D3 ClosestOnTriangle(D3 p, D3 a, D3 b, D3 c)
{
    const D3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = Dot(ab, ap), d2 = Dot(ac, ap);
    if (d1 <= 0 && d2 <= 0)
        return a;
    const D3 bp = p - b;
    const double d3 = Dot(ab, bp), d4 = Dot(ac, bp);
    if (d3 >= 0 && d4 <= d3)
        return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
        return a + ab * (d1 / (d1 - d3));
    const D3 cp = p - c;
    const double d5 = Dot(ab, cp), d6 = Dot(ac, cp);
    if (d6 >= 0 && d5 <= d6)
        return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
        return a + ac * (d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double denom = 1.0 / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

// A shape for my own sweeps: its kind, the object the port reports, the core and the shape's own radius.
struct S_SweepShape {
    enum { TRI, BOX, AABB, SPHERE, CAPSULE, POLE, RECT, CONVEX } kind;
    const void *object;
    D3 a, b, c;                                         // (triangle: corners; sphere: a center; aabb: a min, b max)
    S_matrix matrix, inv;                               // (box, capsule, rect: object to world / back)
    D3 lo, hi;                                          // (box: local min / max; rect: x, z extents)
    double radius = 0;                                  // (sphere, capsule, pole)
    double half = 0;                                    // (capsule: the half height of its core segment)
    bool twoSided = false;
    bool dynamic = false;                               // a volume (else static geometry)
    D3 normal;                                          // (triangle: stored, into the surface)
};
S_SweepShape MakeShape(int kind, const void *object)
{
    S_SweepShape s{};
    s.kind = static_cast<decltype(s.kind)>(kind);
    s.object = object;
    return s;
}
// The nearest point of the shape's core to p, in world space.
D3 Closest(const S_SweepShape &s, D3 p)
{
    switch (s.kind) {
    case S_SweepShape::TRI: return ClosestOnTriangle(p, s.a, s.b, s.c);
    case S_SweepShape::AABB: return {Clamp(p.x, s.a.x, s.b.x), Clamp(p.y, s.a.y, s.b.y), Clamp(p.z, s.a.z, s.b.z)};
    case S_SweepShape::BOX:
    case S_SweepShape::CONVEX: {
        const D3 l = MulD(p, s.inv, true);
        return MulD({Clamp(l.x, s.lo.x, s.hi.x), Clamp(l.y, s.lo.y, s.hi.y), Clamp(l.z, s.lo.z, s.hi.z)}, s.matrix, true);
    }
    case S_SweepShape::SPHERE: return s.a;
    case S_SweepShape::CAPSULE: {
        const D3 l = MulD(p, s.inv, true);
        return MulD({0, Clamp(l.y, -s.half, s.half), 0}, s.matrix, true);
    }
    case S_SweepShape::POLE: return {s.a.x, p.y, s.a.z};
    case S_SweepShape::RECT: {
        const D3 l = MulD(p, s.inv, true);
        return MulD({Clamp(l.x, s.lo.x, s.hi.x), 0, Clamp(l.z, s.lo.z, s.hi.z)}, s.matrix, true);
    }
    }
    return p;
}
// The first contact of a sphere of `r` moved from `from` by `dir` (fraction 0..1), -1 for none, -2 when touching at
// the start; `graze`: how close the nearest pass comes (to leave out near misses).
double OwnSweep(const S_SweepShape &s, D3 from, D3 dir, double r, double &graze)
{
    const double reach = r + s.radius;
    const auto f = [&](double t) {
        const D3 c = from + dir * t;
        return Len(c - Closest(s, c)) - reach;
    };
    graze = 1e9;
    if (f(0) <= 0)
        return -2;
    double lo = 0, hi = 1;
    for (int i = 0; i < 100; ++i) {
        const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
        if (f(m1) < f(m2))
            hi = m2;
        else
            lo = m1;
    }
    const double tmin = (lo + hi) / 2;
    graze = std::fabs(f(tmin)) * 1.0;
    if (f(tmin) > 0)
        return -1;
    lo = 0;
    hi = tmin;
    for (int i = 0; i < 100; ++i) {
        const double m = (lo + hi) / 2;
        if (f(m) > 0)
            lo = m;
        else
            hi = m;
    }
    return (lo + hi) / 2;
}
// Whether the port's rules let the shape be met at all on this sweep (beyond the geometry): one-sided triangles only
// from their front moving into them; spheres, poles and capsules only approached (moving toward them).
bool SweepAllowed(const S_SweepShape &s, D3 from, D3 dir)
{
    switch (s.kind) {
    case S_SweepShape::TRI: {
        const double approach = Dot(s.normal, dir);
        const double side = Dot(s.normal, from - s.a);
        if (approach < 0 && !s.twoSided)
            return false;
        return approach >= 0 ? side <= 0 : side >= 0;
    }
    default:
        return true;
    }
}

S_SweepShape SweepTri(const S_CollisionTriangle &t)
{
    S_SweepShape s = MakeShape(S_SweepShape::TRI, &t);
    s.a = D(ToVector(*t.v0));
    s.b = D(ToVector(*t.v1));
    s.c = D(ToVector(*t.v2));
    s.normal = D(t.normal);
    s.twoSided = (t.flags & COLLISION_TRIANGLE_TWO_SIDED) != 0;
    return s;
}
// The level's primitives as my shapes (boxes, world boxes, spheres, poles, rectangles).
std::vector<S_SweepShape> LevelShapes(const S_CollisionMesh &m, const std::set<const void *> &listed)
{
    std::vector<S_SweepShape> out;
    for (int i = 0; i < m.numBoxes; ++i) {
        const S_CollisionBox &b = m.boxes[i];
        S_SweepShape s = MakeShape(S_SweepShape::BOX, &b);
        s.matrix = b.matrix;
        s.inv = b.invMatrix;
        s.lo = D(ToVector(b.localMin));
        s.hi = D(ToVector(b.localMax));
        out.push_back(s);
    }
    for (int i = 0; i < m.numAabbs; ++i) {
        S_SweepShape s = MakeShape(S_SweepShape::AABB, &m.aabbs[i]);
        s.a = D(m.aabbs[i].min);
        s.b = D(m.aabbs[i].max);
        out.push_back(s);
    }
    for (int i = 0; i < m.numSpheres; ++i) {
        S_SweepShape s = MakeShape(S_SweepShape::SPHERE, &m.spheres[i]);
        s.a = D(ToVector(m.spheres[i].center));
        s.radius = m.spheres[i].radius;
        out.push_back(s);
    }
    for (int i = 0; i < m.numPoles; ++i) {
        S_SweepShape s = MakeShape(S_SweepShape::POLE, &m.poles[i]);
        s.a = {m.poles[i].x, 0, m.poles[i].z};
        s.radius = m.poles[i].radius;
        out.push_back(s);
    }
    for (int i = 0; i < m.numRects; ++i) {
        S_SweepShape s = MakeShape(S_SweepShape::RECT, &m.rects[i]);
        s.matrix = m.rects[i].matrix;
        s.inv = m.rects[i].invMatrix;
        s.lo = D(m.rects[i].min);
        s.hi = D(m.rects[i].max);
        out.push_back(s);
    }
    std::vector<S_SweepShape> kept;
    for (const S_SweepShape &s : out)
        if (listed.count(s.object))
            kept.push_back(s);
    return kept;
}

struct S_SweepStats {
    long sweeps = 0, compared = 0, agree = 0, hits = 0, skipped = 0, kinds[8] = {};
    long origCompared = 0, origAgree = 0, origHits = 0, origTies = 0, approximate = 0;
};

// The original's C_CollisionWorld::SphereTest (0x007158e0, thiscall, RET 0x1c) called on the port's world (the
// same layout); its distance is the squared one it leaves in 0x008cb8c8.
typedef S_CollisionVolume *(__attribute__((thiscall)) * T_OrigSphereTest)(C_CollisionWorld *, const S_vector &, const S_vector &, float,
                                                                         S_vector *, S_vector *, uint32_t, uint32_t);
bool Near(const S_vector &a, const S_vector &b, float tol) { return (a - b).Magnitude() <= tol; }

// The dynamic grid as the original reads it: its cells' std::vector is MSVC6's (four words, the allocator first, so
// the volumes at +0x0C..+0x14; g++'s is three). Runs `f` with the world's grid swapped for a copy of that layout
// over the same volumes.
struct S_MsvcGridCell {
    int x, z;
    uint32_t allocator;
    S_CollisionVolume **first, **last, **end;
};
static_assert(sizeof(S_MsvcGridCell) == 0x18, "the original's grid cell");
template <class F> auto WithOriginalGrid(F f) -> decltype(f())
{
    S_CollisionGrid *const saved = g_collisionWorld.m_grid;
    S_CollisionGrid copy{};
    std::vector<S_MsvcGridCell> cells;
    std::vector<S_MsvcGridCell *> table;
    if (saved) {
        copy = *saved;
        const int n = saved->cellsX * saved->cellsZ;
        cells.resize(n);
        table.assign(n, nullptr);
        for (int i = 0; i < n; ++i)
            if (S_CollisionGridCell *c = saved->cells[i]) {
                S_MsvcGridCell &m = cells[i];
                m.x = c->x;
                m.z = c->z;
                m.allocator = 0;
                m.first = c->volumes.data();
                m.last = m.first + c->volumes.size();
                m.end = m.last;
                table[i] = &m;
            }
        copy.cells = reinterpret_cast<S_CollisionGridCell **>(table.data());
        g_collisionWorld.m_grid = &copy;
    }
    auto result = f();
    g_collisionWorld.m_grid = saved;
    return result;
}
// A vector the original writes: it copies four words (its vectors are padded to 16 bytes).
struct S_OrigOut {
    S_vector v = S_vector(0, 0, 0);
    float w = 0;
};
// The original's SphereTest on the port's world; `d2` its squared distance (0x008cb8c8).
S_CollisionVolume *OriginalSphereTest(const S_vector &from, const S_vector &dir, float r, S_vector *p, S_vector *n, uint32_t excludeTypes,
                                      uint32_t excludeFlags, float &d2)
{
    float &origD2 = *OriginalAt<float *>(0x008cb8c8);
    origD2 = -1;
    S_OrigOut op, on;
    S_CollisionVolume *hit = WithOriginalGrid([&] {
        return OriginalAt<T_OrigSphereTest>(0x007158e0)(&g_collisionWorld, from, dir, r, &op.v, &on.v, excludeTypes, excludeFlags);
    });
    d2 = origD2;
    if (hit && p)                                       // (written on a hit only, as the port's)
        *p = op.v;
    if (hit && n)
        *n = on.v;
    return hit;
}

// The port's SphereTest against the original's on the same query: the same contact point, normal and distance
// (within the x87's 24-bit precision against the port's floats) on the same object, or on another one at the same
// place (two objects met equally near: which is found first can turn on the last bit). 1 agree, 0 differ, -1 no
// original executable.
int CompareOriginal(const std::vector<S_SweepShape> &shapes, const S_vector &from, const S_vector &dir, float r, uint32_t excludeTypes,
                    uint32_t excludeFlags, S_SweepStats &st, const char *where)
{
    if (!OriginalExeLoaded())
        return -1;
    S_vector p(0, 0, 0), n(0, 0, 0), op(0, 0, 0), on(0, 0, 0);
    S_CollisionVolume *hit = g_collisionWorld.SphereTest(from, dir, r, &p, &n, excludeTypes, excludeFlags);
    const float d2 = g_sphereTestDistanceSq;
    float origD2;
    S_CollisionVolume *orig = OriginalSphereTest(from, dir, r, &op, &on, excludeTypes, excludeFlags, origD2);
    ++st.origCompared;
    st.origHits += orig != nullptr;
    const float tol = 1e-3f * std::max(1.0f, dir.Magnitude());
    bool ok = (hit == nullptr) == (orig == nullptr);
    if (ok && hit)
        ok = Near(p, op, tol) && Near(n, on, 1e-2f) && std::fabs(std::sqrt(std::max(d2, 0.0f)) - std::sqrt(std::max(origD2, 0.0f))) <= tol;
    CHECK(ok);
    st.origAgree += ok;
    st.origTies += ok && hit != orig;
    static int shown;
    if (!ok && shown++ < 20) {
        std::printf("  %s original sweep from %g %g %g dir %g %g %g r %g: port %p", where, from.x, from.y, from.z, dir.x, dir.y, dir.z, r,
                    static_cast<void *>(hit));
        if (hit)
            std::printf(" at %g %g %g n %g %g %g d2 %g", p.x, p.y, p.z, n.x, n.y, n.z, d2);
        std::printf("; original %p", static_cast<void *>(orig));
        if (orig)
            std::printf(" at %g %g %g n %g %g %g d2 %g", op.x, op.y, op.z, on.x, on.y, on.z, origD2);
        for (const S_SweepShape &s : shapes)
            if (s.object == hit || s.object == orig)
                std::printf(" [%p kind %d]", s.object, static_cast<int>(s.kind));
        std::printf("\n");
    }
    return ok;
}

// The original's own approximations, where my exact sweep can find another contact: triangles, boxes, AABBs and
// rectangles (one-sided by the approach; the faces facing the motion, then only the edges the leading point's path
// passes outside of, the first that is met, then one corner: earlier contacts on other edges or corners are missed),
// convex hulls (flat planes pushed out by the radius, no rounded edges; the distance kept as a fraction of the move)
// and capsules (no contact when the move goes away from the axis across it, though it would meet an end cap from
// above or below; the volumes are also swept in the capsule's space with the direction left in world space).
// (Checked: on the 52 triangle sweeps and 13 capsule sweeps where mine differs, the port's SweepSphereTriangle /
// SweepSphereCapsule and the original's 0x00705130 / 0x0070ffc0 alone agree: sweep_diff.cpp's tri and capsule modes.)
bool Approximate(const S_SweepShape &s)
{
    return s.kind == S_SweepShape::TRI || s.kind == S_SweepShape::BOX || s.kind == S_SweepShape::AABB || s.kind == S_SweepShape::RECT ||
           s.kind == S_SweepShape::CONVEX || s.kind == S_SweepShape::CAPSULE;
}
// Whether the query's filters keep the shape out (the volumes: their type; static geometry: its flags; -1: all).
bool Filtered(const S_SweepShape &s, uint32_t excludeTypes, uint32_t excludeFlags)
{
    if (s.dynamic) {
        const auto &v = *static_cast<const S_CollisionVolume *>(s.object);
        return excludeTypes == COLLISION_EXCLUDE_ALL || (v.type & (excludeTypes | COLLISION_TYPE_IGNORED)) ||
               (v.flags & ((excludeFlags == COLLISION_EXCLUDE_ALL ? 0 : excludeFlags) | COLLISION_FLAG_DISABLED));
    }
    const uint32_t flags = *static_cast<const uint32_t *>(s.object);
    return excludeFlags == COLLISION_EXCLUDE_ALL || (flags & (excludeFlags | COLLISION_FLAG_DISABLED));
}
// A convex hull's contact is not held to the move nor to a nearer one met before: it takes the entry as far ahead as
// it lies and keeps it as the squared fraction of the move, so a shape tested after it can be met beyond the move's
// end (as in the original: 0x007e1b50). Sweeps whose line enters a hull ahead are left out of my comparison.
bool HullAhead(const std::vector<S_SweepShape> &shapes, D3 from, D3 dir, double r, uint32_t excludeTypes, uint32_t excludeFlags)
{
    for (const S_SweepShape &s : shapes) {
        double graze;
        if (s.kind == S_SweepShape::CONVEX && !Filtered(s, excludeTypes, excludeFlags) && OwnSweep(s, from, dir * 50.0, r, graze) != -1)
            return true;
    }
    return false;
}

// One sweep compared: the port's SphereTest against the original's (CompareOriginal) and against my own exact
// nearest contact over `shapes` (the volumes / static filters as given; ties, near misses and starts touching
// something left out). Where my contact differs on a box, AABB or rectangle and the original agrees with the port,
// it is the original's approximation (counted); elsewhere a difference fails.
void CompareSweep(const std::vector<S_SweepShape> &shapes, const S_vector &from, const S_vector &dir, float r, uint32_t excludeTypes,
                  uint32_t excludeFlags, S_SweepStats &st, const char *where)
{
    ++st.sweeps;
    const int original = CompareOriginal(shapes, from, dir, r, excludeTypes, excludeFlags, st, where);
    const D3 df = D(from), dd = D(dir);
    const double L = Len(dd);
    const D3 mid = df + dd * 0.5;
    if (HullAhead(shapes, df, dd, r, excludeTypes, excludeFlags)) {
        ++st.skipped;
        return;
    }
    double best = 2, second = 2;
    const S_SweepShape *own = nullptr;
    for (const S_SweepShape &s : shapes) {
        // (Out of reach of the whole path: every point of it is within L / 2 of its middle.)
        if (Filtered(s, excludeTypes, excludeFlags) || Len(mid - Closest(s, mid)) > L * 0.5 + r + s.radius + 1e-3)
            continue;
        double graze;
        const double t = OwnSweep(s, df, dd, r, graze);
        if (t == -2 || graze < 2e-3) {
            ++st.skipped;
            return;
        }
        if (t < 0 || !SweepAllowed(s, df, dd))
            continue;
        if (t < best) {
            second = best;
            best = t;
            own = &s;
        } else if (t < second) {
            second = t;
        }
    }
    if (own && (second - best) * L < 2e-3) {
        ++st.skipped;
        return;
    }
    ++st.compared;
    S_vector p, n;
    float distance = -1;
    S_CollisionVolume *hit = CollisionSphereTestDistance(from, dir, r, distance, &p, &n, excludeTypes, excludeFlags);
    bool ok = static_cast<const void *>(hit) == (own ? own->object : nullptr);
    if (ok && hit) {
        const D3 c = df + dd * best;
        const D3 q = Closest(*own, c);
        D3 out = (c - q) * (1.0 / Len(c - q));
        if (own->dynamic && own->kind == S_SweepShape::SPHERE && out.y > 0) {
            // (Sphere volumes: the normal's part pointing down into them is dropped, as in the original.)
            out.y = 0;
            out = out * (1.0 / Len(out));
        }
        ok = Len(D(p) - c) < 2e-3 * std::max(1.0, best * L) && std::fabs(distance - best * L) < 2e-3 * std::max(1.0, best * L) &&
             Len(D(n) - out) < 2e-2;
        ++st.kinds[own->kind];
    }
    st.agree += ok;
    st.hits += hit != nullptr;
    if (!ok && std::getenv("CAPSULE_REPLAY")) {
        // (A development aid: a static capsule mine or the port's met, in its space, for boxdiff.cpp's capsule mode.)
        for (const S_SweepShape &s : shapes)
            if ((s.object == hit || (own && s.object == own->object)) && s.kind == S_SweepShape::CAPSULE && !s.dynamic) {
                const auto &c = *static_cast<const S_CollisionCapsule *>(s.object);
                const S_vector lf = from * c.invMatrix, ld = dir.RotateByMatrix(c.invMatrix), ln = dir.Normalized().RotateByMatrix(c.invMatrix);
                if (FILE *f = std::fopen(std::getenv("CAPSULE_REPLAY"), "a")) {
                    std::fprintf(f, "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %d %.9g\n", c.radius, c.halfHeight, lf.x, lf.y, lf.z,
                                 ld.x, ld.y, ld.z, ln.x, ln.y, ln.z, r, dir.Square(), s.object == hit, own && s.object == own->object ? best * L : -1.0);
                    std::fclose(f);
                }
            }
    }
    if (!ok && own && own->kind == S_SweepShape::TRI && std::getenv("TRI_REPLAY")) {
        // (A development aid: the triangle my sweep meets first, for boxdiff.cpp's tri mode.)
        const auto &t = *static_cast<const S_CollisionTriangle *>(own->object);
        if (FILE *f = std::fopen(std::getenv("TRI_REPLAY"), "a")) {
            std::fprintf(f, "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %u %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", t.v0->x,
                         t.v0->y, t.v0->z, t.v1->x, t.v1->y, t.v1->z, t.v2->x, t.v2->y, t.v2->z, t.normal.x, t.normal.y, t.normal.z, t.d, t.flags,
                         from.x, from.y, from.z, dir.x, dir.y, dir.z, r, best * L);
            std::fclose(f);
        }
    }
    if (!ok && original != 0) {
        // My nearest or the port's (when mine is another shape or none) made by an approximation of the original's
        // (and the original agrees with the port; without it, -1, such a difference is only counted).
        bool approximate = own && Approximate(*own);
        for (const S_SweepShape &s : shapes)
            approximate = approximate || (s.object == hit && Approximate(s));
        if (approximate) {
            ++st.approximate;
            return;
        }
    }
    CHECK(ok);
    if (!ok && failures < 30) {
        std::printf("  %s sweep from %g %g %g dir %g %g %g r %g: port %p", where, from.x, from.y, from.z, dir.x, dir.y, dir.z, r, static_cast<void *>(hit));
        if (hit)
            std::printf(" at %g %g %g (dist %g) n %g %g %g", p.x, p.y, p.z, distance, n.x, n.y, n.z);
        std::printf("; own %p kind %d", own ? own->object : nullptr, own ? static_cast<int>(own->kind) : -1);
        if (own) {
            const D3 c = df + dd * best, q = Closest(*own, c);
            const D3 out = (c - q) * (1.0 / Len(c - q));
            std::printf(" at %g %g %g (dist %g) n %g %g %g", c.x, c.y, c.z, best * L, out.x, out.y, out.z);
        }
        std::printf("\n");
    }
}

void TestLevelSweeps()
{
    std::mt19937 rng(8080);
    S_SweepStats st;
    for (const std::string &level : Levels()) {
        S_RawLevel raw;
        if (!ReadRaw(g_missions + "\\" + level + "\\tree.klz", raw))
            continue;
        g_collisionWorld.Load(("missions\\" + level + "\\tree.klz").c_str(), true);
        const S_CollisionMesh &m = *g_collisionWorld.m_mesh;
        const std::vector<S_SweepShape> shapes = LevelShapes(m, Listed(m));
        if (shapes.empty()) {
            g_collisionWorld.Unload(false);
            continue;
        }
        std::uniform_real_distribution<float> unit(-1.0f, 1.0f), rad(0.1f, 1.2f), len(0.5f, 20.0f);
        for (int i = 0; i < 300 / QUICK; ++i) {
            const S_SweepShape &target = shapes[rng() % shapes.size()];
            D3 aim = target.kind == S_SweepShape::AABB ? (target.a + target.b) * 0.5
                     : target.kind == S_SweepShape::BOX || target.kind == S_SweepShape::RECT ? MulD((target.lo + target.hi) * 0.5, target.matrix, true)
                     : target.kind == S_SweepShape::POLE ? D3{target.a.x, m.minY > -100 ? m.minY + 1.0 : 1.0, target.a.z}
                                                          : target.a;
            const S_vector at(static_cast<float>(aim.x), static_cast<float>(aim.y), static_cast<float>(aim.z));
            const S_vector from = at + S_vector(unit(rng), unit(rng) * 0.5f, unit(rng)).Normalized() * (3.0f + len(rng) * 0.3f);
            S_vector dir = (at + S_vector(unit(rng), unit(rng), unit(rng)) * 1.5f - from);
            if (i % 5 == 0)
                dir = S_vector(0, -len(rng), 0);
            if (i % 7 == 0)
                dir = S_vector(dir.x, 0, dir.z);
            if (dir.Magnitude() < 0.1f)
                continue;
            dir = dir.Normalized() * len(rng);
            CompareSweep(shapes, from, dir, rad(rng), COLLISION_EXCLUDE_ALL, 0, st, level.c_str());
        }
        g_collisionWorld.Unload(false);
    }
    std::printf("  level sweeps %ld: compared %ld, agree %ld, hits %ld (boxes %ld aabbs %ld spheres %ld poles %ld rects %ld), %ld left out\n", st.sweeps,
                st.compared, st.agree, st.hits, st.kinds[S_SweepShape::BOX], st.kinds[S_SweepShape::AABB], st.kinds[S_SweepShape::SPHERE],
                st.kinds[S_SweepShape::POLE], st.kinds[S_SweepShape::RECT], st.skipped);
    std::printf("  %ld differ from my exact contact on boxes / rectangles (the original's approximations: %s)\n", st.approximate,
                OriginalExeLoaded() ? "the original agrees with the port on each" : "not checked without the original");
    if (OriginalExeLoaded())
        std::printf("  against the original: %ld compared, %ld agree (%ld on another object at the same place), %ld hits\n", st.origCompared,
                    st.origAgree, st.origTies, st.origHits);
    else
        std::printf("  (no original executable: the port is compared with my own sweeps only)\n");
    CHECK(st.hits > st.compared / 4);
    CHECK(st.approximate < st.compared / 50);
}

void PrintSweepStats(const char *what, const S_SweepStats &st)
{
    std::printf("  %s sweeps %ld: compared %ld, agree %ld, hits %ld (triangles %ld boxes %ld aabbs %ld spheres %ld capsules %ld poles %ld "
                "rects %ld hulls %ld), %ld left out, %ld the original's approximations (checked against it when it is there)\n",
                what, st.sweeps, st.compared, st.agree, st.hits, st.kinds[S_SweepShape::TRI], st.kinds[S_SweepShape::BOX], st.kinds[S_SweepShape::AABB],
                st.kinds[S_SweepShape::SPHERE], st.kinds[S_SweepShape::CAPSULE], st.kinds[S_SweepShape::POLE], st.kinds[S_SweepShape::RECT],
                st.kinds[S_SweepShape::CONVEX], st.skipped, st.approximate);
    if (OriginalExeLoaded())
        std::printf("    against the original: %ld compared, %ld agree (%ld on another object at the same place), %ld hits\n", st.origCompared,
                    st.origAgree, st.origTies, st.origHits);
    CHECK(st.approximate <= st.compared / 5);   // (random small triangles and volumes: edges and corners are met often)
}

// A point to aim at on the shape.
D3 Aim(const S_SweepShape &t)
{
    switch (t.kind) {
    case S_SweepShape::TRI: return (t.a + t.b + t.c) * (1.0 / 3.0);
    case S_SweepShape::AABB: return (t.a + t.b) * 0.5;
    case S_SweepShape::BOX:
    case S_SweepShape::CONVEX:
    case S_SweepShape::RECT: return MulD((t.lo + t.hi) * 0.5, t.matrix, true);
    case S_SweepShape::CAPSULE: return MulD({0, 0, 0}, t.matrix, true);
    default: return t.a;
    }
}
S_vector F(const D3 &d) { return S_vector(static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z)); }

// A sweep toward a shape: from 2..8 m away, at a point near it; every fifth straight down, every seventh level.
bool AimedSweep(std::mt19937 &rng, const S_SweepShape &target, int i, S_vector &from, S_vector &dir)
{
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f), len(0.5f, 16.0f);
    const S_vector at = F(Aim(target));
    from = at + S_vector(unit(rng), unit(rng), unit(rng)).Normalized() * (2.0f + len(rng) * 0.4f);
    dir = at + S_vector(unit(rng), unit(rng), unit(rng)) * 1.2f - from;
    if (i % 5 == 0)
        dir = S_vector(0, i % 10 ? len(rng) * 0.2f : -len(rng), 0);       // (straight down; up, some shorter than 1 m)
    if (i % 7 == 0)
        dir = S_vector(dir.x, 0, dir.z);
    if (dir.Magnitude() < 0.1f)
        return false;
    dir = dir.Normalized() * len(rng);
    return true;
}

// ---- SlideSphere against the original's (0x00717170, thiscall, RET 0x1c): the end, its normal, what was met last
// and every contact plane it kept (the world's +0x20 table, the same layout).
typedef S_CollisionVolume *(__attribute__((thiscall)) * T_OrigSlideSphere)(C_CollisionWorld *, const S_vector &, const S_vector &, float,
                                                                          S_vector &, S_vector *, uint32_t, uint32_t);
struct S_SlideStats {
    long compared = 0, agree = 0, contacts = 0, sensitive = 0, rounding = 0, repeats = 0, beyond = 0, ownArithmetic = 0;
};
// SphereTest: the original's (`original`) or the port's; d2 its squared distance.
S_CollisionVolume *EitherSphereTest(bool original, const S_vector &from, const S_vector &dir, float r, S_vector *p, S_vector *n, uint32_t excludeTypes,
                                    uint32_t excludeFlags, float &d2)
{
    if (original)
        return OriginalSphereTest(from, dir, r, p, n, excludeTypes, excludeFlags, d2);
    S_CollisionVolume *hit = g_collisionWorld.SphereTest(from, dir, r, p, n, excludeTypes, excludeFlags);
    d2 = g_sphereTestDistanceSq;
    return hit;
}
// The port's SlideSphere (collision_sweep.cpp) written out again, step for step, over the port's SphereTest or the
// original's. Over the port's it must give the port's slide (the port's steps are these, on that input); then, over
// the original's giving the original's slide or the port's, the chains differ by the rounding inside the sweeps or
// inside the steps only (CompareSlide).
S_CollisionVolume *HybridSlide(bool original, const S_vector &from, const S_vector &move, float r, uint32_t excludeTypes, uint32_t excludeFlags,
                               S_vector &end, S_vector &endNormal, std::vector<C_CollisionWorld::S_SlideContact> &contacts)
{
    S_vector pos = from, step = move, normal(0.0f, 1.0f, 0.0f);
    S_CollisionVolume *last = nullptr;
    contacts.clear();
    for (int attempt = 0; attempt < 10; ++attempt) {
        S_vector hitPos;
        float d2;
        S_CollisionVolume *hit = EitherSphereTest(original, pos, step, r, &hitPos, &normal, excludeTypes, excludeFlags, d2);
        if (!hit)
            break;
        last = hit;
        const float d = -normal.Dot(hitPos);
        S_vector target = pos + step;
        const float normalSq = normal.Square();
        if (std::fabs(normalSq) >= 1e-8f)
            target += normal * -((normal.Dot(target) + d) / normalSq);
        pos = hitPos;
        step = target - hitPos + normal * 0.01f;
        for (const auto &c : contacts) {
            const float cosine = step.Dot(c.normal);
            if (std::fabs(cosine) >= 1e-8f) {
                const float t = -((pos.Dot(c.normal) + c.d) / cosine);
                if (t > 0.0f && t < 1.0f)
                    step = step * t * 0.95f;
            }
        }
        C_CollisionWorld::S_SlideContact c{};
        c.volume = (hit->flags & COLLISION_FLAG_DYNAMIC) ? hit : nullptr;
        c.pos = hitPos;
        c.d = d;
        c.normal = normal;
        contacts.push_back(c);
    }
    end = pos + step;
    endNormal = normal;
    return last;
}
void CompareSlide(const S_vector &from, const S_vector &move, float r, uint32_t excludeTypes, uint32_t excludeFlags, S_SlideStats &st,
                  const char *where)
{
    if (!OriginalExeLoaded())
        return;
    S_vector end(0, 0, 0), normal(0, 0, 0);
    S_CollisionVolume *hit = g_collisionWorld.SlideSphere(from, move, r, end, &normal, excludeTypes, excludeFlags);
    const int count = g_collisionWorld.m_numSlideContacts;
    std::vector<C_CollisionWorld::S_SlideContact> contacts(g_collisionWorld.m_slideContacts, g_collisionWorld.m_slideContacts + count);
    std::memset(g_collisionWorld.m_slideContacts, 0, sizeof g_collisionWorld.m_slideContacts);
    g_collisionWorld.m_numSlideContacts = -1;
    S_OrigOut endOut, normalOut;
    S_CollisionVolume *orig = WithOriginalGrid([&] {
        return OriginalAt<T_OrigSlideSphere>(0x00717170)(&g_collisionWorld, from, move, r, endOut.v, &normalOut.v, excludeTypes, excludeFlags);
    });
    const S_vector oend = endOut.v, onormal = normalOut.v;
    ++st.compared;
    st.contacts += count;
    const float tol = 2e-3f * std::max(1.0f, move.Magnitude());
    bool ok = hit == orig && count == g_collisionWorld.m_numSlideContacts && Near(end, oend, tol) && Near(normal, onormal, 1e-2f);
    for (int i = 0; ok && i < count; ++i) {
        const auto &a = contacts[i], &b = g_collisionWorld.m_slideContacts[i];
        ok = a.volume == b.volume && Near(a.pos, b.pos, tol) && Near(a.normal, b.normal, 1e-2f) && std::fabs(a.d - b.d) <= tol * 4;
    }
    st.agree += ok;
    const int origCount = g_collisionWorld.m_numSlideContacts;
    if (!ok) {
        // A chain of contacts carries the rounding of each step (the port's floats, the original's x87) into the
        // next sweep's start, and a sweep grazing an edge or corner, or touching right at its start, turns on it.
        // Such a slide is left out when the port itself gives the original's result with the start or the radius
        // moved by 0.01 or 0.1 mm (its SphereTest itself is compared above), or when the port's slide over the
        // original's SphereTest gives it (HybridSlide).
        const std::vector<C_CollisionWorld::S_SlideContact> origContacts(g_collisionWorld.m_slideContacts,
                                                                         g_collisionWorld.m_slideContacts + std::max(origCount, 0));
        const auto sameAsOriginal = [&](S_CollisionVolume *h, int n, const C_CollisionWorld::S_SlideContact *c, const S_vector &e, const S_vector &nn) {
            bool same = h == orig && n == origCount && Near(e, oend, tol) && Near(nn, onormal, 1e-2f);
            for (int i = 0; same && i < origCount; ++i)
                same = c[i].volume == origContacts[i].volume && Near(c[i].pos, origContacts[i].pos, tol) &&
                       Near(c[i].normal, origContacts[i].normal, 1e-2f) && std::fabs(c[i].d - origContacts[i].d) <= tol * 4;
            return same;
        };
        // A contact farther from the start than the move can reach: a convex hull met beyond the move (see
        // HullAhead), from which the rest goes elsewhere; left out.
        bool beyond = false;
        for (int i = 0; i < count; ++i)
            beyond = beyond || (contacts[i].pos - from).Magnitude() > move.Magnitude() + 2 * r + tol;
        for (int i = 0; i < origCount; ++i)
            beyond = beyond || (origContacts[i].pos - from).Magnitude() > move.Magnitude() + 2 * r + tol;
        if (beyond) {
            ++st.beyond;
            return;
        }
        // The port's steps written out again over the port's own sweeps: the port's slide, or the steps are not the
        // port's (a fault in SlideSphere itself: nothing below excuses it).
        std::vector<C_CollisionWorld::S_SlideContact> copy;
        S_vector cend, cnormal;
        S_CollisionVolume *chit = HybridSlide(false, from, move, r, excludeTypes, excludeFlags, cend, cnormal, copy);
        bool portSteps = chit == hit && static_cast<int>(copy.size()) == count && Near(cend, end, 0) && Near(cnormal, normal, 0);
        for (int i = 0; portSteps && i < count; ++i)
            portSteps = copy[i].volume == contacts[i].volume && Near(copy[i].pos, contacts[i].pos, 0) && Near(copy[i].normal, contacts[i].normal, 0) &&
                        copy[i].d == contacts[i].d;
        std::vector<C_CollisionWorld::S_SlideContact> hybrid;
        S_vector hend, hnormal;
        S_CollisionVolume *hhit = HybridSlide(true, from, move, r, excludeTypes, excludeFlags, hend, hnormal, hybrid);
        if (portSteps && sameAsOriginal(hhit, static_cast<int>(hybrid.size()), hybrid.data(), hend, hnormal)) {
            ++st.rounding;
            return;
        }
        // The port's slide over the original's sweeps gives the port's: every sweep of the port's chain is the
        // original's at the same start, so only the slide's own arithmetic (the same steps as 0x00717170, summed in
        // another order) set the original's chain off elsewhere, mostly bouncing in a wedge.
        bool hybridIsPort = hhit == hit && static_cast<int>(hybrid.size()) == count && Near(hend, end, tol) && Near(hnormal, normal, 1e-2f);
        for (int i = 0; hybridIsPort && i < count; ++i)
            hybridIsPort = hybrid[i].volume == contacts[i].volume && Near(hybrid[i].pos, contacts[i].pos, tol) &&
                           Near(hybrid[i].normal, contacts[i].normal, 1e-2f) && std::fabs(hybrid[i].d - contacts[i].d) <= tol * 4;
        if (portSteps && hybridIsPort) {
            ++st.ownArithmetic;
            return;
        }
        if (std::getenv("SLIDE_DUMP")) {
            std::printf("  slide dump from %.9g %.9g %.9g move %.9g %.9g %.9g r %.9g\n", from.x, from.y, from.z, move.x, move.y, move.z, r);
            for (size_t i = 0; i < std::max({contacts.size(), origContacts.size(), hybrid.size()}); ++i) {
                const auto show = [](const char *who, const std::vector<C_CollisionWorld::S_SlideContact> &v, size_t i) {
                    if (i < v.size())
                        std::printf("    %s %zu: %p pos %.7g %.7g %.7g n %.6g %.6g %.6g d %.6g\n", who, i, static_cast<void *>(v[i].volume), v[i].pos.x,
                                    v[i].pos.y, v[i].pos.z, v[i].normal.x, v[i].normal.y, v[i].normal.z, v[i].d);
                };
                show("port    ", contacts, i);
                show("original", origContacts, i);
                show("hybrid  ", hybrid, i);
            }
        }
        bool sensitive = false;
        for (int k = 0; k < 16 && !sensitive; ++k) {
            // (The start along each axis; the radius, on which touching at the start turns; by 0.01 and 0.1 mm.)
            const float e = k < 8 ? 1e-5f : 1e-4f;
            S_vector nudge(0, 0, 0);
            if (k % 8 < 6)
                (&nudge.x)[k % 8 / 2] = k % 2 ? -e : e;
            const float r2 = k % 8 == 6 ? r + e : k % 8 == 7 ? r - e : r;
            S_vector end2, normal2;
            S_CollisionVolume *hit2 = g_collisionWorld.SlideSphere(from + nudge, move, r2, end2, &normal2, excludeTypes, excludeFlags);
            sensitive = sameAsOriginal(hit2, g_collisionWorld.m_numSlideContacts, g_collisionWorld.m_slideContacts, end2, normal2);
        }
        if (portSteps && sensitive) {
            ++st.sensitive;
            return;
        }
        // Stuck against one place: the sphere touches it again and again without moving (a sphere volume touched at
        // the start: pushed out sideways, or not, by the last bit; a wedge of triangles), and when it gets loose,
        // or touches once more before the ten are used up, turns on rounding. The contacts agree up to the first
        // that differs, and the last two before it are at one place.
        int first = 0;
        while (first < std::min(count, origCount) && contacts[first].volume == origContacts[first].volume &&
               Near(contacts[first].pos, origContacts[first].pos, tol) && Near(contacts[first].normal, origContacts[first].normal, 1e-2f) &&
               std::fabs(contacts[first].d - origContacts[first].d) <= tol * 4)
            ++first;
        const bool repeats = first >= 2 && Near(contacts[first - 1].pos, contacts[first - 2].pos, tol);
        if (portSteps && repeats) {
            ++st.repeats;
            return;
        }
    }
    CHECK(ok);
    static int shown;
    if (!ok && shown++ < 10)
        std::printf("  %s slide from %g %g %g move %g %g %g r %g: port %p end %g %g %g (%d contacts), original %p end %g %g %g (%d)\n", where, from.x,
                    from.y, from.z, move.x, move.y, move.z, r, static_cast<void *>(hit), end.x, end.y, end.z, count, static_cast<void *>(orig), oend.x,
                    oend.y, oend.z, origCount);
}

// ---- static sweeps on a level of my own
// Part 1's file format: the triangles (one- and two-sided, every kind, some water), a tilted plain rectangle, capsule
// primitives and convex primitives (hulls on two visuals: world space and turned); the static filters (water, two
// sided, none at all). SlideSphere on the same.
void TestSynthSweeps()
{
    std::mt19937 rng(9191);
    S_Synth s = MakeSynth(rng, 300);
    s.rectFlags = COLLISION_PRIM_RECT;
    s.rectMatrix = Rigid(0.4f, 0.3f, S_vector(34, 7, 26));
    std::uniform_real_distribution<float> pos(6.0f, 58.0f), py(2.0f, 12.0f), ang(0.0f, 6.28f);
    for (int i = 0; i < 8; ++i) {
        S_CollisionCapsule c;
        std::memset(&c, 0, sizeof c);
        c.flags = COLLISION_PRIM_CAPSULE;
        c.radius = 0.4f + 0.15f * i;
        c.halfHeight = c.radius + 0.8f + 0.3f * i;
        c.matrix = Rigid(ang(rng), i < 3 ? 0.0f : ang(rng) * 0.25f, S_vector(pos(rng), py(rng), pos(rng)));   // (three upright)
        c.invMatrix = InverseRigid(c.matrix);
        s.capsules.push_back(c);
        s.primHeights.push_back({c.matrix.m[3][1] - c.halfHeight, c.matrix.m[3][1] + c.halfHeight});
    }
    const S_matrix hullMatrix = Rigid(1.1f, 0.25f, S_vector(24, 7, 40));
    I3D_frame *hullWorld = HullVisual(true, Rigid(0, 0, S_vector()));
    I3D_frame *hullTurned = HullVisual(false, hullMatrix);
    g_sceneFrames = {{"sweephull0", hullWorld}, {"sweephull1", hullTurned}};
    s.records = {{0u, "nonexist"}, {1u, "sweephull0"}, {1u, "sweephull1"}};
    const S_vector hullLo[2] = {S_vector(44, 3, 12), S_vector(-2.5f, -1.5f, -1.2f)}, hullHi[2] = {S_vector(49, 6.5f, 15), S_vector(2.5f, 1.5f, 1.2f)};
    for (int i = 0; i < 2; ++i) {
        S_CollisionConvexPrim c;
        std::memset(&c, 0, sizeof c);
        c.flags = COLLISION_PRIM_CONVEX;
        c.frame = reinterpret_cast<I3D_frame *>(static_cast<uintptr_t>(i + 1));
        c.numPlanes = 6;
        BoxPlanes(hullLo[i], hullHi[i], c.planes);
        s.convexes.push_back(c);
    }
    s.primHeights.push_back({3.0f, 6.5f});
    s.primHeights.push_back({4.0f, 10.0f});
    const std::string path = TempFile("sweeps.klz");
    WriteSynth(s, path);
    g_collisionWorld.Load(path.c_str(), true);
    const S_CollisionMesh &m = *g_collisionWorld.m_mesh;
    CHECK(m.numCapsules == 8 && m.numConvexes == 2 && m.numRects == 1 && m.numTriangles == static_cast<int>(s.tris.size()));
    std::vector<S_SweepShape> shapes;
    for (int i = 0; i < m.numTriangles; ++i)
        shapes.push_back(SweepTri(m.triangles[i]));
    {
        S_SweepShape r = MakeShape(S_SweepShape::RECT, &m.rects[0]);
        r.matrix = m.rects[0].matrix;
        r.inv = m.rects[0].invMatrix;
        r.lo = D(m.rects[0].min);
        r.hi = D(m.rects[0].max);
        shapes.push_back(r);
    }
    for (int i = 0; i < m.numCapsules; ++i) {
        S_SweepShape c = MakeShape(S_SweepShape::CAPSULE, &m.capsules[i]);
        c.matrix = m.capsules[i].matrix;
        c.inv = m.capsules[i].invMatrix;
        c.radius = m.capsules[i].radius;
        c.half = m.capsules[i].halfHeight - m.capsules[i].radius;
        shapes.push_back(c);
    }
    for (int i = 0; i < 2; ++i) {
        S_SweepShape h = MakeShape(S_SweepShape::CONVEX, &m.convexes[i]);
        h.matrix = i == 0 ? Rigid(0, 0, S_vector()) : hullMatrix;
        h.inv = InverseRigid(h.matrix);
        h.lo = D(hullLo[i]);
        h.hi = D(hullHi[i]);
        shapes.push_back(h);
    }
    S_SweepStats st;
    S_SlideStats slides;
    std::uniform_real_distribution<float> rad(0.1f, 1.2f);
    for (int i = 0; i < 12000 / QUICK; ++i) {
        // (Every shape kind about as often: the triangles are most of the list.)
        const S_SweepShape &target = i % 3 ? shapes[rng() % shapes.size()] : shapes[m.numTriangles + rng() % (shapes.size() - m.numTriangles)];
        S_vector from, dir;
        if (!AimedSweep(rng, target, i, from, dir))
            continue;
        const uint32_t flags = i % 6 == 0 ? COLLISION_FLAG_WATER : i % 9 == 0 ? COLLISION_TRIANGLE_TWO_SIDED : i % 50 == 0 ? COLLISION_EXCLUDE_ALL : 0;
        const float r = rad(rng);
        CompareSweep(shapes, from, dir, r, COLLISION_EXCLUDE_ALL, flags, st, "synthetic");
        if (i % 3 == 0)
            CompareSlide(from, dir, r, COLLISION_EXCLUDE_ALL, flags, slides, "synthetic");
    }
    PrintSweepStats("synthetic", st);
    if (OriginalExeLoaded())
        std::printf("    slides: %ld compared, %ld agree, %ld contacts; differ: %ld as the port's slide over the original's sweeps, %ld as "
                    "the port's within 0.1 mm, %ld stuck repeating one touch, %ld the port's over the original's sweeps (the slide's own "
                    "rounding); %ld met a hull beyond the move\n",
                    slides.compared, slides.agree, slides.contacts, slides.rounding, slides.sensitive, slides.repeats, slides.ownArithmetic,
                    slides.beyond);
    CHECK(slides.sensitive + slides.rounding + slides.repeats + slides.ownArithmetic + slides.beyond <= slides.compared / 25);
    CHECK(st.kinds[S_SweepShape::TRI] > 300 / QUICK && st.kinds[S_SweepShape::CAPSULE] > 60 / QUICK && st.kinds[S_SweepShape::RECT] > 20 / QUICK);
    g_collisionWorld.Unload(false);
    g_sceneFrames.clear();
}

// ---- the dynamic volumes
S_SweepShape VolumeShape(const S_TestVolume &tv)
{
    const S_CollisionVolume &v = tv.v;
    S_SweepShape s{};
    switch (v.flags & COLLISION_SHAPE_MASK) {
    case COLLISION_SHAPE_BOX:
        s = MakeShape(S_SweepShape::BOX, &v);
        s.matrix = tv.matrix;
        s.inv = tv.inv;
        s.lo = D(v.bbox.min);
        s.hi = D(v.bbox.max);
        break;
    case COLLISION_SHAPE_WORLD_BOX:
        s = MakeShape(S_SweepShape::AABB, &v);
        s.a = D(v.bbox.min);
        s.b = D(v.bbox.max);
        break;
    case COLLISION_SHAPE_SPHERE:
        s = MakeShape(S_SweepShape::SPHERE, &v);
        s.a = D(v.center);
        s.radius = v.radius;
        break;
    default:
        s = MakeShape(S_SweepShape::CAPSULE, &v);
        s.matrix = tv.matrix;
        s.inv = tv.inv;
        s.radius = v.capsuleRadius;
        s.half = v.radius - v.capsuleRadius;
        break;
    }
    s.dynamic = true;
    return s;
}
// A rigid matrix scaled by k, and its inverse.
S_matrix ScaledRigid(float yaw, float pitch, const S_vector &pos, float k, S_matrix &inv)
{
    S_matrix m = Rigid(yaw, pitch, pos);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m.m[i][j] *= k;
    std::memset(&inv, 0, sizeof inv);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            inv.m[i][j] = m.m[j][i] / (k * k);
    for (int j = 0; j < 3; ++j)
        inv.m[3][j] = -(m.m[3][0] * inv.m[0][j] + m.m[3][1] * inv.m[1][j] + m.m[3][2] * inv.m[2][j]);
    inv.m[3][3] = 1.0f;
    return m;
}
// The volume shapes MakeVolumes does not make (the sphere sweeps' own): vertical cylinders, the second sphere kind,
// scaled boxes, convex hulls on visuals.
struct S_ExtraVolume {
    S_CollisionConvexVolume c;
    S_matrix matrix, inv;
};
std::vector<S_ExtraVolume *> MakeExtraVolumes(std::mt19937 &rng, int count)
{
    std::vector<S_ExtraVolume *> out;
    std::uniform_real_distribution<float> pos(2.0f, 62.0f), py(1.0f, 14.0f), size(0.3f, 2.5f), ang(0.0f, 6.2831853f), scale(0.5f, 2.0f);
    for (int i = 0; i < count; ++i) {
        auto *e = new S_ExtraVolume();
        S_CollisionVolume &v = e->c.volume;
        const S_vector c(pos(rng), py(rng), pos(rng));
        v.center = c;
        v.type = 1u << (rng() % 4) << 8;
        const S_vector h(size(rng), size(rng), size(rng));
        switch (i % 4) {
        case 0:
            v.flags = COLLISION_SHAPE_CYLINDER;
            v.capsuleRadius = h.x * 0.6f;
            v.radius = h.Magnitude() + 1.0f;
            e->matrix = Rigid(0, 0, c);
            e->inv = InverseRigid(e->matrix);
            break;
        case 1:
            v.flags = COLLISION_SHAPE_SPHERE_44;
            v.radius = h.x;
            e->matrix = Rigid(0, 0, c);
            e->inv = InverseRigid(e->matrix);
            break;
        case 2: {
            v.flags = COLLISION_SHAPE_SCALED_BOX;
            const float k = scale(rng);
            e->matrix = ScaledRigid(ang(rng), ang(rng) * 0.3f, c, k, e->inv);
            v.bbox.min = -h;
            v.bbox.max = h;
            v.radius = h.Magnitude() * k;
            break;
        }
        default:
            v.flags = COLLISION_SHAPE_CONVEX;
            e->matrix = Rigid(ang(rng), ang(rng) * 0.3f, c);
            e->inv = InverseRigid(e->matrix);
            e->c.visual = reinterpret_cast<I3D_visual *>(HullVisual(i % 8 == 7, e->matrix));
            e->c.numPlanes = 6;
            BoxPlanes(-h, h, e->c.planes);
            v.radius = h.Magnitude();
            if (i % 8 == 7)
                v.center = S_vector(0, 0, 0);       // (a world-space hull at the origin: its planes are in world space)
            break;
        }
        v.matrix = &e->matrix;
        v.invMatrix = &e->inv;
        out.push_back(e);
    }
    return out;
}

// Sweeps among volumes and static triangles: the filters by type (one bit, all volumes, none) and by flags (static
// geometry or not, the water). My own exact sweeps for the volume shapes MakeVolumes makes, then the original only
// with the others added. SlideSphere on the same.
void TestVolumeSweeps()
{
    std::mt19937 rng(5150);
    S_Synth s = MakeSynth(rng, 120);
    const std::string path = TempFile("volsweeps.klz");
    WriteSynth(s, path);
    g_collisionWorld.Load(path.c_str(), true);
    const S_CollisionMesh &m = *g_collisionWorld.m_mesh;
    std::vector<S_TestVolume *> volumes = MakeVolumes(rng, 120);
    for (S_TestVolume *tv : volumes)
        g_collisionWorld.AddVolume(&tv->v);
    std::vector<S_SweepShape> shapes;
    for (int i = 0; i < m.numTriangles; ++i)
        shapes.push_back(SweepTri(m.triangles[i]));
    for (int i = 0; i < m.numRects; ++i) {
        S_SweepShape r = MakeShape(S_SweepShape::RECT, &m.rects[i]);
        r.matrix = m.rects[i].matrix;
        r.inv = m.rects[i].invMatrix;
        r.lo = D(m.rects[i].min);
        r.hi = D(m.rects[i].max);
        shapes.push_back(r);
    }
    const size_t firstVolume = shapes.size();
    for (S_TestVolume *tv : volumes)
        shapes.push_back(VolumeShape(*tv));
    S_SweepStats st;
    S_SlideStats slides;
    std::uniform_real_distribution<float> rad(0.1f, 1.2f);
    for (int i = 0; i < 12000 / QUICK; ++i) {
        const S_SweepShape &target = i % 4 ? shapes[firstVolume + rng() % (shapes.size() - firstVolume)] : shapes[rng() % shapes.size()];
        S_vector from, dir;
        if (!AimedSweep(rng, target, i, from, dir))
            continue;
        const uint32_t types = i % 4 == 1 ? 0x200u : i % 11 == 0 ? COLLISION_EXCLUDE_ALL : 0u;
        const uint32_t flags = i % 3 == 0 ? COLLISION_EXCLUDE_ALL : i % 7 == 0 ? COLLISION_FLAG_WATER : 0u;
        const float r = rad(rng);
        CompareSweep(shapes, from, dir, r, types, flags, st, "volumes");
        if (i % 3 == 1)
            CompareSlide(from, dir, r, types, flags, slides, "volumes");
    }
    PrintSweepStats("volume", st);
    CHECK(st.kinds[S_SweepShape::SPHERE] > 100 / QUICK && st.kinds[S_SweepShape::BOX] > 100 / QUICK);

    // The other shapes: against the original (my shapes left out: no own comparison).
    std::vector<S_ExtraVolume *> extra = MakeExtraVolumes(rng, 80);
    for (S_ExtraVolume *e : extra)
        g_collisionWorld.AddVolume(&e->c.volume);
    S_SweepStats st2;
    const std::vector<S_SweepShape> none;
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f), len(0.5f, 16.0f);
    for (int i = 0; i < 8000 / QUICK && OriginalExeLoaded(); ++i) {
        const S_CollisionVolume &v = extra[rng() % extra.size()]->c.volume;
        const S_vector at = (v.flags & COLLISION_SHAPE_MASK) == COLLISION_SHAPE_CONVEX ? *reinterpret_cast<const S_vector *>(&v.matrix->m[3][0]) : v.center;
        const S_vector from = at + S_vector(unit(rng), unit(rng), unit(rng)).Normalized() * (2.0f + len(rng) * 0.4f);
        S_vector dir = at + S_vector(unit(rng), unit(rng), unit(rng)) * 1.2f - from;
        if (i % 5 == 0)
            dir = S_vector(0, -len(rng), 0);
        if (dir.Magnitude() < 0.1f)
            continue;
        dir = dir.Normalized() * len(rng);
        const uint32_t types = i % 4 == 1 ? 0x200u : 0u;
        const uint32_t flags = i % 2 ? COLLISION_EXCLUDE_ALL : 0u;
        const float r = rad(rng);
        CompareOriginal(none, from, dir, r, types, flags, st2, "other volumes");
        if (i % 3 == 0)
            CompareSlide(from, dir, r, types, flags, slides, "other volumes");
    }
    if (OriginalExeLoaded())
        std::printf("  other volume shapes against the original: %ld compared, %ld agree (%ld ties), %ld hits; slides (with the volumes' above) %ld, "
                    "%ld agree, %ld contacts; differ: %ld as the port's slide over the original's sweeps, %ld as the port's within 0.1 mm, %ld stuck "
                    "repeating one touch, %ld the port's over the original's sweeps (the slide's own rounding); %ld met a hull beyond the move\n",
                    st2.origCompared, st2.origAgree, st2.origTies, st2.origHits, slides.compared, slides.agree, slides.contacts, slides.rounding,
                    slides.sensitive, slides.repeats, slides.ownArithmetic, slides.beyond);
    CHECK(slides.sensitive + slides.rounding + slides.repeats + slides.ownArithmetic + slides.beyond <= slides.compared / 25);
    for (S_ExtraVolume *e : extra) {
        g_collisionWorld.RemoveVolume(&e->c.volume);
        delete e;
    }
    for (S_TestVolume *tv : volumes) {
        g_collisionWorld.RemoveVolume(&tv->v);
        delete tv;
    }
    g_collisionWorld.Unload(false);
}

// ---- the body sweeps (collision_body.cpp) against the original's
// MoveSpheres 0x0071ba20, MoveSpheresStepping 0x0071bec0, MoveSpheresLow 0x0071cba0 (thiscall, the contacts: the
// same layout); g_moveIterations is the original's 0x008441e4.
typedef S_CollisionVolume *(__attribute__((thiscall)) * T_OrigMove)(C_CollisionWorld *, S_MoveContacts *);
const uint32_t kOrigMove[3] = {0x0071ba20, 0x0071bec0, 0x0071cba0};

// Two triangles of the quad a b c d (the stored normal into the surface: against `outward`).
void AddQuad(S_Synth &s, S_vector a, S_vector b, S_vector c, S_vector d, const S_vector &outward)
{
    const S_vector corners[2][3] = {{a, b, c}, {a, c, d}};
    for (const auto &q : corners) {
        S_SynthTri t = Tri(q[0], q[1], q[2], 0, false);
        if (t.normal.Dot(outward) > 0.0f)
            t = Tri(q[0], q[1], q[2], 0, true);
        s.tris.push_back(t);
    }
}
// A block lo..hi: its top and sides.
void AddBlock(S_Synth &s, const S_vector &lo, const S_vector &hi)
{
    AddQuad(s, S_vector(lo.x, hi.y, lo.z), S_vector(hi.x, hi.y, lo.z), S_vector(hi.x, hi.y, hi.z), S_vector(lo.x, hi.y, hi.z), S_vector(0, 1, 0));
    AddQuad(s, S_vector(lo.x, lo.y, lo.z), S_vector(lo.x, hi.y, lo.z), S_vector(lo.x, hi.y, hi.z), S_vector(lo.x, lo.y, hi.z), S_vector(-1, 0, 0));
    AddQuad(s, S_vector(hi.x, lo.y, lo.z), S_vector(hi.x, hi.y, lo.z), S_vector(hi.x, hi.y, hi.z), S_vector(hi.x, lo.y, hi.z), S_vector(1, 0, 0));
    AddQuad(s, S_vector(lo.x, lo.y, lo.z), S_vector(hi.x, lo.y, lo.z), S_vector(hi.x, hi.y, lo.z), S_vector(lo.x, hi.y, lo.z), S_vector(0, 0, -1));
    AddQuad(s, S_vector(lo.x, lo.y, hi.z), S_vector(hi.x, lo.y, hi.z), S_vector(hi.x, hi.y, hi.z), S_vector(lo.x, hi.y, hi.z), S_vector(0, 0, 1));
}
// A ramp along +x from (x0, 1, z0), `length` long and `width` wide, rising by `height`.
void AddRamp(S_Synth &s, float x0, float z0, float length, float width, float height)
{
    AddQuad(s, S_vector(x0, 1, z0), S_vector(x0 + length, 1 + height, z0), S_vector(x0 + length, 1 + height, z0 + width), S_vector(x0, 1, z0 + width),
            S_vector(-height, length, 0).Normalized());
}

// A human the stepping sweep asks (C_Human::GetStanceFlags reads its controller's move flags, +0xB0 then +0x50;
// collision_test.cpp's stub does the same).
struct S_FakeHuman {
    uint8_t bytes[0x400] = {};
    uint8_t controller[0x100] = {};
    explicit S_FakeHuman(uint32_t moveFlags)
    {
        *reinterpret_cast<uint8_t **>(bytes + 0xB0) = controller;
        *reinterpret_cast<uint32_t *>(controller + 0x50) = moveFlags;
    }
};

struct S_BodyStats {
    long compared = 0, agree = 0, contacts = 0, sensitive = 0, hybrid = 0;
};
// The port's MoveSpheres (collision_body.cpp) written out again, step for step, over the port's SphereTest or the
// original's (as HybridSlide).
S_CollisionVolume *HybridMoveSpheres(bool original, S_MoveContacts &contacts)
{
    const uint32_t count = contacts.sphereCount;
    S_vector4 spheres[MOVE_CONTACTS_MAX] = {};
    for (uint32_t i = 0; i < count; ++i)
        spheres[i] = contacts.spheres[i];
    S_vector move = ToVector(contacts.move);
    S_CollisionVolume *last = nullptr;
    int touched = 0;
    S_vector hitPos[MOVE_CONTACTS_MAX];
    S_vector4 hitNormal[MOVE_CONTACTS_MAX] = {};
    int nearest;
    do {
        if (g_moveIterations <= static_cast<float>(static_cast<uint32_t>(touched)))
            break;
        nearest = -1;
        float best = FLT_MAX;
        S_vector step = move;
        for (uint32_t i = 0; i < count; ++i) {
            S_vector normal;
            float d2;
            S_CollisionVolume *hit = EitherSphereTest(original, ToVector(spheres[i]), move, contacts.radii[i], &hitPos[i], &normal, contacts.excludeTypes,
                                                        contacts.excludeFlags, d2);
            if (hit) {
                hitNormal[i] = {normal.x, normal.y, normal.z, hitNormal[i].w};
                if (d2 < best) {
                    best = d2;
                    nearest = static_cast<int>(i);
                    last = hit;
                }
            }
        }
        if (nearest >= 0) {
            const S_vector from = ToVector(spheres[nearest]), n = ToVector(hitNormal[nearest]);
            step = hitPos[nearest] - from;
            // (SlideAlong: the rest of the move projected onto the contact plane, pushed off it by 0.01.)
            const float d = -hitPos[nearest].Dot(n);
            const S_vector end = from + move;
            const float denom = n.Dot(n);
            S_vector projected = end;
            if (!(std::fabs(denom) < 1e-8f))
                projected = end + n * -((n.Dot(end) + d) / denom);
            move = (projected - hitPos[nearest]) + n * 0.01f;
            contacts.normals[touched] = hitNormal[nearest];
            contacts.sphere[touched] = nearest;
            contacts.volumes[touched] = last;
            ++touched;
        }
        for (uint32_t i = 0; i < count; ++i) {
            spheres[i].x += step.x;
            spheres[i].y += step.y;
            spheres[i].z += step.z;
        }
    } while (nearest >= 0);
    contacts.pos = spheres[0];
    contacts.count = touched;
    return last;
}
bool SameMove(const S_MoveContacts &a, const S_MoveContacts &b, float tol)
{
    if (a.count != b.count || !Near(ToVector(a.pos), ToVector(b.pos), tol))
        return false;
    for (int i = 0; i < std::min(a.count, MOVE_CONTACTS_MAX); ++i)
        if (a.sphere[i] != b.sphere[i] || a.volumes[i] != b.volumes[i] || !Near(ToVector(a.normals[i]), ToVector(b.normals[i]), 1e-2f))
            return false;
    return true;
}
S_CollisionVolume *PortMove(int which, S_MoveContacts &c)
{
    return which == 0 ? g_collisionWorld.MoveSpheres(c) : which == 1 ? g_collisionWorld.MoveSpheresStepping(c) : g_collisionWorld.MoveSpheresLow(c);
}
// One body moved by the port and by the original: what was met last, the end, every contact (its sphere, what, the
// normal). A difference is left out (counted) when the port gives the original's result with the body moved by 0.01
// or 0.1 mm (as the slides: each step's rounding goes into the next).
void CompareBody(int which, const S_MoveContacts &input, S_BodyStats &st, const char *where)
{
    if (!OriginalExeLoaded())
        return;
    S_MoveContacts a = input, b = input;
    S_CollisionVolume *ha = PortMove(which, a);
    *OriginalAt<float *>(0x008441e4) = g_moveIterations;
    S_CollisionVolume *hb = WithOriginalGrid([&] { return OriginalAt<T_OrigMove>(kOrigMove[which])(&g_collisionWorld, &b); });
    ++st.compared;
    st.contacts += std::max(a.count, 0);
    const float tol = 2e-3f * std::max(1.0f, ToVector(input.move).Magnitude());
    bool ok = ha == hb && SameMove(a, b, tol);
    st.agree += ok;
    if (!ok && which == 0) {
        // The port's own steps over the original's sweeps: giving the original's or the port's, the sweeps along the
        // chain are the original's and the difference is the rounding of the steps (as the slides).
        // (Over the port's own sweeps the steps written out again must give the port's: they are the port's steps.)
        S_MoveContacts copy = input, h = input;
        S_CollisionVolume *hc = HybridMoveSpheres(false, copy);
        S_CollisionVolume *hh = HybridMoveSpheres(true, h);
        if (hc == ha && SameMove(copy, a, 0) && ((hh == hb && SameMove(h, b, tol)) || (hh == ha && SameMove(h, a, tol)))) {
            ++st.hybrid;
            return;
        }
    }
    if (!ok) {
        bool sensitive = false;
        for (int k = 0; k < 12 && !sensitive; ++k) {
            const float e = k < 6 ? 1e-5f : 1e-4f;
            S_MoveContacts c = input;
            for (uint32_t i = 0; i < c.sphereCount; ++i)
                (&c.spheres[i].x)[k % 6 / 2] += k % 2 ? -e : e;
            S_CollisionVolume *hc = PortMove(which, c);
            sensitive = hc == hb && SameMove(c, b, tol);
        }
        if (sensitive) {
            ++st.sensitive;
            return;
        }
    }
    CHECK(ok);
    static int shown;
    if (!ok && shown++ < 10) {
        std::printf("  %s body sweep %d (%u spheres, iterations %g) from %g %g %g move %g %g %g: port %p count %d end %g %g %g; original %p count %d end %g "
                    "%g %g\n",
                    where, which, input.sphereCount, g_moveIterations, input.spheres[0].x, input.spheres[0].y, input.spheres[0].z, input.move.x,
                    input.move.y, input.move.z, static_cast<void *>(ha), a.count, a.pos.x, a.pos.y, a.pos.z, static_cast<void *>(hb), b.count, b.pos.x,
                    b.pos.y, b.pos.z);
        for (int i = 0; i < std::min(std::max(a.count, b.count), MOVE_CONTACTS_MAX); ++i)
            std::printf("    contact %d: sphere %d / %d, %p / %p, n %g %g %g / %g %g %g\n", i, a.sphere[i], b.sphere[i], static_cast<void *>(a.volumes[i]),
                        static_cast<void *>(b.volumes[i]), a.normals[i].x, a.normals[i].y, a.normals[i].z, b.normals[i].x, b.normals[i].y,
                        b.normals[i].z);
    }
}

// Bodies of one to three spheres (a human's: 0.35 m, stacked) walking on ground of my own: a floor, blocks to step
// onto (0.2 m) and not (0.6 m), walls and a corner, ramps of 20 and 45 degrees; humans standing and lying (volumes of
// type 0x100 with an actor), crates and other volumes of the types that stop a step up (0x1000) or not (0x800).
// Every body sweep with the iterations the locomotion uses.
void TestBodySweeps()
{
    S_Synth s;
    s.withRect = false;
    for (int x = 0; x < 64; x += 8)
        for (int z = 0; z < 64; z += 8)
            AddQuad(s, S_vector(x, 1, z), S_vector(x + 8, 1, z), S_vector(x + 8, 1, z + 8), S_vector(x, 1, z + 8), S_vector(0, 1, 0));
    AddBlock(s, S_vector(10, 1, 10), S_vector(14, 1.2f, 14));
    AddBlock(s, S_vector(30, 1, 40), S_vector(33, 1.25f, 44));
    AddBlock(s, S_vector(20, 1, 12), S_vector(24, 1.6f, 16));
    AddBlock(s, S_vector(44, 1, 30), S_vector(48, 1.7f, 34));
    AddBlock(s, S_vector(8, 1, 30), S_vector(8.3f, 4, 50));
    AddBlock(s, S_vector(36, 1, 52), S_vector(50, 4, 52.3f));
    AddBlock(s, S_vector(50, 1, 8), S_vector(50.3f, 4, 20.3f));
    AddBlock(s, S_vector(40, 1, 20), S_vector(50, 4, 20.3f));
    AddRamp(s, 14, 24, 8, 4, 2.9f);
    AddRamp(s, 26, 2, 3, 4, 3);
    // A V of two 62-degree slopes (walls to the sweeps: normals below 0.6 up) and a slot narrower than a body.
    AddQuad(s, S_vector(34.4f, 4, 10), S_vector(36, 1, 10), S_vector(36, 1, 14), S_vector(34.4f, 4, 14), S_vector(3, 1.6f, 0).Normalized());
    AddQuad(s, S_vector(36, 1, 10), S_vector(37.6f, 4, 10), S_vector(37.6f, 4, 14), S_vector(36, 1, 14), S_vector(-3, 1.6f, 0).Normalized());
    AddBlock(s, S_vector(20, 1, 40), S_vector(22, 3, 44));
    AddBlock(s, S_vector(22.65f, 1, 40), S_vector(24.65f, 3, 44));
    // A roof 2 m over the floor (its underside too), and an overhang: a wall leaning over the floor (its normal 0.4 down).
    AddBlock(s, S_vector(10, 3, 50), S_vector(16, 3.3f, 56));
    AddQuad(s, S_vector(10, 3, 50), S_vector(16, 3, 50), S_vector(16, 3, 56), S_vector(10, 3, 56), S_vector(0, -1, 0));
    AddQuad(s, S_vector(3, 1, 50), S_vector(3, 1, 56), S_vector(2.13f, 3, 56), S_vector(2.13f, 3, 50), S_vector(-2, -0.87f, 0).Normalized());
    const std::string path = TempFile("ground.klz");
    WriteSynth(s, path);
    g_collisionWorld.Load(path.c_str(), true);
    CHECK(g_collisionWorld.m_mesh && g_collisionWorld.m_mesh->numTriangles == static_cast<int>(s.tris.size()));

    std::mt19937 rng(6060);
    std::uniform_real_distribution<float> pos(3.0f, 61.0f), unit(-1.0f, 1.0f), ang(0.0f, 6.2831853f), step(0.05f, 2.0f);
    // Humans (standing; lying: on their side), crates, other volumes.
    std::vector<S_TestVolume *> volumes;
    std::vector<S_FakeHuman *> humans;
    for (int i = 0; i < 40; ++i) {
        auto *tv = new S_TestVolume();
        S_CollisionVolume &v = tv->v;
        const bool lying = i % 4 == 1;
        S_vector c(pos(rng), lying ? 1.36f : 1.9f, pos(rng));
        if (i == 0 || i == 2)
            c = i == 0 ? S_vector(49.2f, 1.9f, 14.0f) : S_vector(45.0f, 1.9f, 19.3f);     // (by the corner of the walls)
        tv->matrix = Rigid(ang(rng), lying ? 1.5707963f : 0.0f, c);
        tv->inv = InverseRigid(tv->matrix);
        v.matrix = &tv->matrix;
        v.invMatrix = &tv->inv;
        v.center = c;
        if (i % 2 == 0 || lying) {
            v.flags = COLLISION_SHAPE_CAPSULE;
            v.type = 0x100;
            v.capsuleRadius = 0.35f;
            v.radius = 0.9f;
            humans.push_back(new S_FakeHuman(lying ? static_cast<uint32_t>(MOVE_LYING) : 0u));
            v.actor = reinterpret_cast<C_Actor *>(humans.back()->bytes);
        } else {
            const S_vector h(0.3f + 0.4f * (i % 3), 0.3f + 0.2f * (i % 5), 0.4f + 0.3f * (i % 2));
            v.flags = i % 3 ? COLLISION_SHAPE_BOX : COLLISION_SHAPE_SPHERE;
            v.type = i % 3 == 2 ? 0x800 : 0x1000;
            v.center = S_vector(c.x, 1.0f + h.y, c.z);
            tv->matrix = Rigid(ang(rng), 0.0f, v.center);
            tv->inv = InverseRigid(tv->matrix);
            v.bbox.min = -h;
            v.bbox.max = h;
            v.radius = v.flags == COLLISION_SHAPE_SPHERE ? h.y : h.Magnitude();
        }
        volumes.push_back(tv);
        g_collisionWorld.AddVolume(&v);
    }
    // Places: the floor, the blocks' tops, near the walls and the volumes.
    const S_vector tops[] = {S_vector(12, 1.2f, 12), S_vector(31.5f, 1.25f, 42), S_vector(22, 1.6f, 14), S_vector(46, 1.7f, 32), S_vector(9, 1, 40),
                             S_vector(43, 1, 51), S_vector(49, 1, 19), S_vector(18, 1, 26), S_vector(27, 1, 4)};
    // (The locomotion's caps. Above 6 the low sweep can write an 11th contact past the table (most horizontal, 3 wedged
    // steps, the ground), into the sphere indices after it, as the original does.)
    const float iterations[] = {2, 3, 4, 5, 6};
    S_BodyStats st[3];
    long count = 0;
    for (int i = 0; i < 6000 / QUICK; ++i) {
        S_MoveContacts c;
        std::memset(&c, 0, sizeof c);
        c.sphereCount = 1 + i % 3;
        S_vector foot;
        if (i % 3 == 0) {
            foot = tops[rng() % 9] + S_vector(unit(rng) * 2.5f, 0, unit(rng) * 2.5f);
        } else if (i % 3 == 1) {
            const S_CollisionVolume &v = volumes[rng() % volumes.size()]->v;
            foot = S_vector(v.center.x + unit(rng) * 2.0f, 1, v.center.z + unit(rng) * 2.0f);
        } else {
            foot = S_vector(pos(rng), 1, pos(rng));
        }
        foot.y += 0.002f + 0.05f * (unit(rng) + 1.0f);
        for (uint32_t k = 0; k < c.sphereCount; ++k) {
            c.radii[k] = 0.3f + 0.05f * ((i + k) % 3);
            c.spheres[k] = {foot.x, foot.y + 0.35f + 0.55f * k, foot.z, 0};
        }
        c.radii[c.sphereCount] = 0.35f;
        for (int k = 0; k < MOVE_CONTACTS_MAX; ++k)
            c.sphere[k] = 7;                                    // (what a sweep leaves alone stays 7)
        const float a = ang(rng), d = step(rng);
        c.move = {std::cos(a) * d, -0.4f + 0.5f * (unit(rng) + 1.0f) * 0.5f, std::sin(a) * d, 0};
        if (i % 11 == 0)
            c.move.x = c.move.z = 0.0f;                       // (straight down)
        c.excludeTypes = i % 5 == 0 ? 0x100u : 0u;
        c.excludeFlags = 0;
        c.pos = c.spheres[0];
        const int which = i / 3 % 3;                         // (the places go by i % 3: every sweep at every kind)
        g_moveIterations = which == 0 && i % 2 ? 10.0f : iterations[rng() % 5];
        CompareBody(which, c, st[which], "ground");
        ++count;
    }
    // Harder places: dropped into the V and the slot (wedged between two walls), onto standing humans, feet below a
    // surface (the low sweep's second try from 1 m up) or well above it, moves going up more than the step's lift.
    for (int i = 0; i < 4500 / QUICK; ++i) {
        S_MoveContacts c;
        std::memset(&c, 0, sizeof c);
        c.sphereCount = 1 + i % 3;
        S_vector foot;
        switch (i % 7) {
        case 0: foot = S_vector(36.0f + unit(rng) * 1.6f, 1.3f + 1.2f * (unit(rng) + 1.0f), 12.0f + unit(rng) * 1.5f); break;
        case 3: foot = S_vector(13.0f + unit(rng) * 2.5f, 1.01f + 0.05f * (unit(rng) + 1.0f), 53.0f + unit(rng) * 2.5f); break;
        case 4: foot = S_vector(1.6f + unit(rng) * 0.8f, 1.02f + 0.1f * (unit(rng) + 1.0f), 53.0f + unit(rng) * 2.5f); break;
        case 1: foot = S_vector(22.32f + unit(rng) * 0.4f, 3.05f + 0.3f * (unit(rng) + 1.0f), 42.0f + unit(rng) * 1.5f); break;
        case 2: {
            const S_CollisionVolume &v = volumes[2 * (rng() % (volumes.size() / 2))]->v;    // (the even ones: humans)
            foot = S_vector(v.center.x + unit(rng) * 0.3f, v.center.y + v.radius + 0.02f + 0.1f * (unit(rng) + 1.0f), v.center.z + unit(rng) * 0.3f);
            break;
        }
        default: foot = tops[rng() % 9] + S_vector(unit(rng) * 3.0f, -0.3f + 0.55f * (unit(rng) + 1.0f), unit(rng) * 3.0f); break;
        }
        for (uint32_t k = 0; k < c.sphereCount; ++k) {
            c.radii[k] = 0.3f + 0.05f * ((i + k) % 3);
            c.spheres[k] = {foot.x, foot.y + 0.35f + 0.55f * k, foot.z, 0};
        }
        c.radii[c.sphereCount] = 0.35f;
        for (int k = 0; k < MOVE_CONTACTS_MAX; ++k)
            c.sphere[k] = 7;                                    // (what a sweep leaves alone stays 7)
        const float a = ang(rng), d = i % 4 == 0 ? 0.0f : step(rng) * 0.5f;
        c.move = {std::cos(a) * d, -0.6f + 0.6f * (unit(rng) + 1.0f), std::sin(a) * d, 0};
        if (i % 7 == 3 && c.sphereCount == 3)
            c.sphereCount = 2;                                  // (three would reach into the roof)
        if (i % 7 == 3)
            c.move.y = 0.5f + 0.4f * (unit(rng) + 1.0f);       // (up under the roof: the step goes up from the top sphere)
        if (i % 7 == 4)
            c.move.x = std::fabs(c.move.x) + 0.3f;               // (toward the overhang)
        c.excludeTypes = i % 11 == 0 ? 0x100u : 0u;
        c.pos = c.spheres[0];
        const int which = i % 3;
        g_moveIterations = iterations[rng() % 5];
        CompareBody(which, c, st[which], "harder");
    }
    // Two humans lying in an L (the first along x, the second across its end, along z): sliding along the first into
    // the second, the stepping sweep stops on meeting it. (Boxes: a capsule laid down is missed by the sphere sweeps,
    // which take the direction in world space into the capsule's own.)
    {
        S_FakeHuman lyingActor(MOVE_LYING);
        S_TestVolume pair[2];
        for (int k = 0; k < 2; ++k) {
            S_CollisionVolume &v = pair[k].v;
            v.center = k == 0 ? S_vector(30.0f, 1.3f, 28.0f) : S_vector(31.3f, 1.3f, 27.2f);
            pair[k].matrix = Rigid(0.0f, 0.0f, v.center);
            pair[k].inv = InverseRigid(pair[k].matrix);
            v.matrix = &pair[k].matrix;
            v.invMatrix = &pair[k].inv;
            v.flags = COLLISION_SHAPE_BOX;
            v.type = 0x100;
            v.bbox.min = k == 0 ? S_vector(-0.9f, -0.3f, -0.3f) : S_vector(-0.3f, -0.3f, -0.9f);
            v.bbox.max = -v.bbox.min;
            v.radius = v.bbox.max.Magnitude();
            v.actor = reinterpret_cast<C_Actor *>(lyingActor.bytes);
            g_collisionWorld.AddVolume(&v);
        }
        long stopped = 0;
        for (int i = 0; i < 60; ++i) {
            S_MoveContacts c;
            std::memset(&c, 0, sizeof c);
            c.sphereCount = 1 + i % 3;
            for (uint32_t k = 0; k < c.sphereCount; ++k) {
                c.radii[k] = 0.35f;
                c.spheres[k] = {28.6f + 0.02f * i, 1.36f + 0.55f * k, 26.9f, 0};
            }
            c.move = {2.2f + 0.05f * (i % 7), -0.05f, 0.6f + 0.01f * i, 0};
            c.pos = c.spheres[0];
            g_moveIterations = 4.0f + i % 5;
            S_MoveContacts own = c;
            S_CollisionVolume *hit = g_collisionWorld.MoveSpheresStepping(own);
            stopped += hit == &pair[1].v && hit && own.pos.x < 31.3f;       // (stopped at the second, short of its axis)
            CompareBody(1, c, st[1], "lying pair");
        }
        std::printf("  two lying humans in an L: stopped at the second %ld of 60\n", stopped);
        CHECK(stopped > 0);
        for (S_TestVolume &tv : pair)
            g_collisionWorld.RemoveVolume(&tv.v);
    }
    // The free functions: the world's.
    {
        const S_vector from(20, 3, 20), dir(0, -4, 0);
        S_vector p1, n1, p2, n2, e1, e2, m1, m2;
        float distance = -1;
        CHECK(CollisionSphereTest(from, dir, 0.4f, &p1, &n1, 0, 0) == g_collisionWorld.SphereTest(from, dir, 0.4f, &p2, &n2, 0, 0) && Near(p1, p2, 0) &&
              Near(n1, n2, 0));
        CHECK(CollisionSphereTestDistance(from, dir, 0.4f, distance, &p1, &n1, 0, 0) && std::fabs(distance - (3.0f - 1.0f - 0.4f)) < 1e-4f);
        CHECK(CollisionSlideSphere(from, S_vector(1, -4, 0.5f), 0.4f, e1, &m1, 0, 0) ==
                  g_collisionWorld.SlideSphere(from, S_vector(1, -4, 0.5f), 0.4f, e2, &m2, 0, 0) &&
              Near(e1, e2, 0) && Near(m1, m2, 0));
    }
    g_moveIterations = 10.0f;
    const char *names[3] = {"MoveSpheres", "MoveSpheresStepping", "MoveSpheresLow"};
    for (int w = 0; w < 3; ++w) {
        if (OriginalExeLoaded())
            std::printf("  %s against the original: %ld compared, %ld agree, %ld contacts; differ: %ld as the port's steps over the original's "
                        "sweeps, %ld as the port's within 0.1 mm\n",
                        names[w], st[w].compared, st[w].agree, st[w].contacts, st[w].hybrid, st[w].sensitive);
        CHECK(st[w].sensitive + st[w].hybrid <= st[w].compared / 25);
        CHECK(!OriginalExeLoaded() || st[w].contacts > st[w].compared / 3);
    }
    for (S_TestVolume *tv : volumes) {
        g_collisionWorld.RemoveVolume(&tv->v);
        delete tv;
    }
    for (S_FakeHuman *h : humans)
        delete h;
    g_collisionWorld.Unload(false);
}

void TestSweeps()
{
    g_synthQuiet = true;
    TestLevelSweeps();
    TestSynthSweeps();
    TestVolumeSweeps();
    TestBodySweeps();
}
}  // namespace
