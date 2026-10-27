// Part 4's test (the route build, build_route.bat: -DROUTE_TEST links src/game/melee_tracker_route.cpp
// and runs only RouteTest on every model): C_MeleeTrackerAI::RouteAroundCars on paths over the car's
// marked checkpoints, against the model's real body box (the car's BODY frame turned with the model;
// once tilted). check_route.py reads the result line. CAR_TRACE=1 prints the failing check lines.

#include "game/ai_parts.h"
#include "game/melee_tracker.h"

// --- the meleeTracker's waypoint pool (game/melee_tracker_ai.cpp's; that file is not linked) ----------

std::set<S_AIWaypoint *> g_rtLive;                      // made, not released
std::vector<S_AIWaypoint *> g_rtReleased;               // in release order (kept: no address reuse)

S_AIWaypoint *AllocMeleeTrackerWaypoint(int checkpoint, const S_vector4 &pos, uint16_t flags, uint8_t type,
                                        uint8_t radiusByte)
{
    auto *w = new S_AIWaypoint();
    w->checkpoint = checkpoint;
    w->pos = pos;
    w->pos2 = pos;
    w->flags = flags;
    w->type = type;
    w->radius = static_cast<float>(radiusByte) * 0.0625f;
    g_rtLive.insert(w);
    return w;
}

void ReleaseMeleeTrackerWaypoint(S_AIWaypoint *w)
{
    if (!w)
        return;
    g_rtReleased.push_back(w);
    g_rtLive.erase(w);
}

// --- the test ------------------------------------------------------------------------------------------

void RtExpect(bool &flag, bool ok, int line)
{
    if (!ok && std::getenv("CAR_TRACE"))
        std::printf("  route check failed: line %d\n", line);
    flag = flag && ok;
}
#define RT(flag, ...) RtExpect(flag, (__VA_ARGS__), __LINE__)

// The route's view of a car: its BODY frame's matrices and the footprint it takes (0.5 m out, x 0.85).
struct S_RtCar {
    S_matrix toWorld, toCar;
    float min[2], max[2];
    float height;
};

S_RtCar RtCar(const C_Car *car)
{
    S_RtCar c;
    const C_Vehicle &v = car->m_vehicle;
    c.toWorld = v.m_body->GetMatrix();
    c.toCar = c.toWorld;
    c.toCar.Inverse(c.toWorld);
    c.min[0] = (v.m_bodyMin.x - 0.5f) * 0.85f;
    c.min[1] = (v.m_bodyMin.z - 0.5f) * 0.85f;
    c.max[0] = (v.m_bodyMax.x + 0.5f) * 0.85f;
    c.max[1] = (v.m_bodyMax.z + 0.5f) * 0.85f;
    c.height = car->GetPosition4().y;
    return c;
}

// The world point the route takes for the car-space ground point (x, z): the one at the car's height.
S_vector RtWorld(const S_RtCar &c, float x, float z)
{
    const S_matrix &m = c.toWorld;
    const float y = (c.height - x * m.m[0][1] - z * m.m[2][1] - m.m[3][1]) / m.m[1][1];
    return S_vector(x, y, z) * c.toWorld;
}

// A car-space point on the car's ground plane (y 0), in the world.
S_vector RtGround(const S_RtCar &c, float x, float z)
{
    return S_vector(x, 0.0f, z) * c.toWorld;
}

// The ground point (x, z) in the car's space as the route takes a waypoint's.
S_vector RtLocal(const S_RtCar &c, const S_AIWaypoint *w)
{
    return S_vector(w->pos.x, c.height, w->pos.z) * c.toCar;
}

S_AIWaypoint *RtWaypoint(const S_RtCar &c, float x, float z, int checkpoint, uint16_t flags)
{
    const S_vector p = RtWorld(c, x, z);
    return AllocMeleeTrackerWaypoint(checkpoint, S_vector4{p.x, p.y, p.z, 1.0f}, flags, 0, 0);
}

bool RtNear(const S_vector &a, const S_vector &b, float eps = 2e-3f)
{
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}

// The AI and its tracker (raw: only RouteAroundCars runs; it reads the path and the tracker's frame).
struct S_RtAI {
    alignas(16) unsigned char ai[sizeof(C_MeleeTrackerAI)];
    alignas(16) unsigned char tracker[sizeof(C_MeleeTracker)];
};

void RtSetPath(C_MeleeTrackerAI *ai, const std::vector<S_AIWaypoint *> &path, size_t room)
{
    const size_t capacity = path.size() + room;
    ai->m_pathBegin = ai->m_pathEnd = ai->m_pathCap = nullptr;
    if (capacity == 0)
        return;
    auto **a = static_cast<S_AIWaypoint **>(operator new(capacity * sizeof(S_AIWaypoint *)));
    std::copy(path.begin(), path.end(), a);
    ai->m_pathBegin = a;
    ai->m_pathEnd = a + path.size();
    ai->m_pathCap = a + capacity;
}

std::vector<S_AIWaypoint *> RtGetPath(const C_MeleeTrackerAI *ai)
{
    if (!ai->m_pathBegin)
        return {};
    return std::vector<S_AIWaypoint *>(ai->m_pathBegin, ai->m_pathEnd);
}

void RtClear(C_MeleeTrackerAI *ai)
{
    operator delete(ai->m_pathBegin);
    ai->m_pathBegin = ai->m_pathEnd = ai->m_pathCap = nullptr;
    for (S_AIWaypoint *w : g_rtLive)
        delete w;
    for (S_AIWaypoint *w : g_rtReleased)
        delete w;
    g_rtLive.clear();
    g_rtReleased.clear();
}

// One waypoint of an expected path: the path's own (`kept`), or one the route made at `pos` (no
// checkpoint, `flags`, the position's w).
struct S_RtWant {
    S_AIWaypoint *kept;
    S_vector pos;
    uint16_t flags;
    float w;
};

S_RtWant Kept(S_AIWaypoint *w)
{
    return {w, S_vector(0.0f, 0.0f, 0.0f), 0, 0.0f};
}

S_RtWant Made(const S_vector &pos, uint16_t flags, float w = 1.0f)
{
    return {nullptr, pos, flags, w};
}

// The path is `want`, exactly `released` went back to the pool, and nothing else is alive.
bool RtPathIs(const C_MeleeTrackerAI *ai, const std::vector<S_RtWant> &want, std::vector<S_AIWaypoint *> released)
{
    const std::vector<S_AIWaypoint *> path = RtGetPath(ai);
    if (path.size() != want.size())
        return false;
    for (size_t i = 0; i < path.size(); ++i) {
        const S_AIWaypoint *w = path[i];
        if (want[i].kept) {
            if (w != want[i].kept)
                return false;
            continue;
        }
        if (w->checkpoint != -1 || w->flags != want[i].flags || w->type != 0 || w->radius != 0.0f ||
            !RtNear(S_vector(w->pos.x, w->pos.y, w->pos.z), want[i].pos) || w->pos.w != want[i].w ||
            std::memcmp(&w->pos, &w->pos2, sizeof(S_vector4)) != 0)
            return false;
    }
    std::vector<S_AIWaypoint *> gone = g_rtReleased;
    std::sort(gone.begin(), gone.end());
    std::sort(released.begin(), released.end());
    return gone == released && std::set<S_AIWaypoint *>(path.begin(), path.end()) == g_rtLive;
}

struct S_Rt {
    bool unchanged = true, step = true, detour = true, swap = true, end = true, legs = true, fail = true,
         cars = true, tilt = true;
};

I3D_frame *g_rtTracker;

// The tracker at the world point `p` (its matrix's w 1.5: the step off the car takes it).
void RtPlace(const S_vector &p)
{
    S_matrix &m = g_rtTracker->m_localMatrix;
    m.Identity();
    m.m[3][0] = p.x;
    m.m[3][1] = p.y;
    m.m[3][2] = p.z;
    m.m[3][3] = 1.5f;
}

// The route's cases on `car` (c: its view; r's flags get the results).
void RtCases(S_Rt &r, C_MeleeTrackerAI *ai, C_Car *car, bool tilted)
{
    const S_RtCar c = RtCar(car);
    const float x0 = c.min[0], x1 = c.max[0], z0 = c.min[1], z1 = c.max[1];
    const float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f, w = x1 - x0, d = z1 - z0;
    const S_vector corner[4] = {RtGround(c, x0 - 0.3f, z0 - 0.3f), RtGround(c, x1 + 0.3f, z0 - 0.3f),
                                RtGround(c, x1 + 0.3f, z1 + 0.3f), RtGround(c, x0 - 0.3f, z1 + 0.3f)};
    bool &tilt = r.tilt;
    const auto route = [&](const std::vector<S_AIWaypoint *> &path, size_t room, float tx, float tz) {
        RtSetPath(ai, path, room);
        RtPlace(RtWorld(c, tx, tz));
        return ai->RouteAroundCars();
    };
    const auto unchanged = [&](bool result, const std::vector<S_AIWaypoint *> &path, size_t room, float tx, float tz) {
        const bool ok = route(path, room, tx, tz) == result && RtGetPath(ai) == path && g_rtReleased.empty() &&
                        g_rtLive.size() == path.size() && ai->m_pathCap - ai->m_pathBegin == static_cast<ptrdiff_t>(path.size() + room);
        RtClear(ai);
        return ok;
    };
    S_AIWaypoint *a, *b, *e, *f;

    // --- Unchanged: no run over marked points; a path the route changed already (its first or second
    // waypoint); a run no car marked whole; no car; no crossing; only the first run; the tracker's
    // own leg when the run does not start the path.
    if (!tilted) {
        RT(r.unchanged, unchanged(true, {}, 0, cx, z0 - 6.0f));
        RT(r.unchanged, unchanged(true, {RtWaypoint(c, cx, z0 - 3.0f, 10, 0), RtWaypoint(c, cx, cz, 11, 0),
                                         RtWaypoint(c, cx, z1 + 3.0f, 12, 0)}, 1, cx, z0 - 6.0f));
        RT(r.unchanged, unchanged(true, {RtWaypoint(c, cx, z0 - 3.0f, -1, 0x1000), RtWaypoint(c, cx, cz, 11, 0x200),
                                         RtWaypoint(c, cx, z1 + 3.0f, 12, 0)}, 0, cx, z0 - 6.0f));
        RT(r.unchanged, unchanged(true, {RtWaypoint(c, cx, z0 - 3.0f, 10, 0), RtWaypoint(c, cx, z0 - 1.0f, -1, 0x1000),
                                         RtWaypoint(c, cx, cz, 11, 0x200), RtWaypoint(c, cx, z1 + 3.0f, 12, 0)},
                                  0, cx, z0 - 6.0f));
        RT(r.unchanged, unchanged(true, {RtWaypoint(c, cx, z0 - 3.0f, 10, 0), RtWaypoint(c, cx, cz, 50, 0x200),
                                         RtWaypoint(c, cx, z1 + 3.0f, 12, 0)}, 0, cx, z0 - 6.0f));
        RT(r.unchanged, unchanged(true, {RtWaypoint(c, cx, z0 - 3.0f, 10, 0), RtWaypoint(c, cx, cz - d * 0.125f, 11, 0x200),
                                         RtWaypoint(c, cx, cz + d * 0.125f, 50, 0x200), RtWaypoint(c, cx, z1 + 3.0f, 12, 0)},
                                  0, cx, z0 - 6.0f));
        const std::vector<C_Car *> cars = g_cars;
        g_cars.clear();
        RT(r.unchanged, unchanged(true, {RtWaypoint(c, cx, z0 - 3.0f, 10, 0), RtWaypoint(c, cx, cz, 11, 0x200),
                                         RtWaypoint(c, cx, z1 + 3.0f, 12, 0)}, 0, cx, z0 - 6.0f));
        g_cars = cars;
        RT(r.unchanged, unchanged(true, {RtWaypoint(c, x1 + 3.0f, z0 - 3.0f, 10, 0), RtWaypoint(c, x1 + 3.0f, cz, 11, 0x200),
                                         RtWaypoint(c, x1 + 3.0f, z1 + 3.0f, 12, 0)}, 0, x1 + 3.0f, z0 - 6.0f));
        RT(r.unchanged, unchanged(true, {RtWaypoint(c, x1 + 3.0f, cz, 11, 0x200), RtWaypoint(c, x1 + 6.0f, cz, 10, 0),
                                         RtWaypoint(c, cx, cz, 12, 0x200), RtWaypoint(c, cx, z1 + 3.0f, 13, 0)},
                                  0, x1 + 3.0f, z0 - 3.0f));
        RT(r.unchanged, unchanged(true, {RtWaypoint(c, cx, z1 + 3.0f, 10, 0), RtWaypoint(c, x1 + 10.0f, z1 + 3.0f, 11, 0x200),
                                         RtWaypoint(c, x1 + 12.0f, z1 + 3.0f, 12, 0)}, 0, cx, z0 - 3.0f));
        // Legs beside the footprint (below, left of it): their lines cross its sides' lines outside it.
        RT(r.unchanged, unchanged(true, {RtWaypoint(c, x0 - 3.0f, z0 - 3.0f, 10, 0), RtWaypoint(c, x1 + 3.0f, z0 - 3.0f, 11, 0x200),
                                         RtWaypoint(c, x1 + 6.0f, z0 - 3.0f, 12, 0)}, 0, x0 - 6.0f, z0 - 3.0f));
        RT(r.unchanged, unchanged(true, {RtWaypoint(c, x0 - 3.0f, z0 - 3.0f, 10, 0), RtWaypoint(c, x0 - 3.0f, z1 + 3.0f, 11, 0x200),
                                         RtWaypoint(c, x0 - 3.0f, z1 + 6.0f, 12, 0)}, 0, x0 - 3.0f, z0 - 6.0f));
        // The tracker just off each side: no step (the path leads away).
        for (int side = 0; side < 4; ++side) {
            const float tx = side == 0 ? x0 - 0.05f : side == 1 ? x1 + 0.05f : cx;
            const float tz = side == 2 ? z0 - 0.05f : side == 3 ? z1 + 0.05f : cz;
            const float ox = side == 0 ? -2.0f : side == 1 ? 2.0f : 0.0f;
            const float oz = side == 2 ? -2.0f : side == 3 ? 2.0f : 0.0f;
            RT(r.unchanged, unchanged(true, {RtWaypoint(c, tx + ox, tz + oz, 11, 0x200),
                                             RtWaypoint(c, tx + ox * 2.0f, tz + oz * 2.0f, 12, 0)}, 0, tx, tz));
        }
    }

    // --- The tracker on the footprint: a step 3 m off the nearest side goes first (the tracker's own
    // height and w).
    for (int side = 0; side < 4; ++side) {
        const float tx = side == 0 ? x0 + 0.4f : side == 1 ? x1 - 0.4f : cx;
        const float tz = side == 2 ? z0 + 0.4f : side == 3 ? z1 - 0.4f : cz;
        a = RtWaypoint(c, cx, cz, 11, 0x200);
        b = RtWaypoint(c, cx, z1 + 3.0f, 12, 0);
        RtSetPath(ai, {a, b}, static_cast<size_t>(side % 2));
        const S_vector at = RtWorld(c, tx, tz) + S_vector(0.0f, 0.5f, 0.0f);
        RtPlace(at);
        S_vector local = at * c.toCar;
        if (side == 0)
            local.x = x0 - 3.0f;
        else if (side == 1)
            local.x = x1 + 3.0f;
        else if (side == 2)
            local.z = z0 - 3.0f;
        else
            local.z = z1 + 3.0f;
        const bool ok = ai->RouteAroundCars();
        RT(tilted ? tilt : r.step, ok && RtPathIs(ai, {Made(local * c.toWorld, 0x1000, 1.5f), Kept(a), Kept(b)}, {}));
        RtClear(ai);
    }

    // --- Detours: straight through (min z to max z), either side of the middle: the corners the
    // shorter way, in place of the waypoints on the footprint.
    for (int k = 0; k < 2; ++k) {
        const float x = cx + (k == 0 ? -0.25f : 0.25f) * w;
        a = RtWaypoint(c, x, z0 - 3.0f, 10, 0);
        b = RtWaypoint(c, x, cz, 11, 0x200);
        e = RtWaypoint(c, x, z1 + 3.0f, 12, 0);
        const bool ok = route({a, b, e}, k == 0 ? 0 : 3, x, z0 - 6.0f);
        const S_vector &p = corner[k == 0 ? 0 : 1], &q = corner[k == 0 ? 3 : 2];
        RT(tilted ? tilt : r.detour, ok && RtPathIs(ai, {Kept(a), Made(p, 0x1000), Made(q, 0x1000), Kept(e)}, {b}));
        RtClear(ai);
    }
    if (tilted)
        return;
    // The tracker beyond max z: the crossing there is the near one, the corners from it.
    for (int k = 0; k < 2; ++k) {
        const float x = cx + (k == 0 ? -0.25f : 0.25f) * w;
        a = RtWaypoint(c, x, z0 - 3.0f, 10, 0);
        b = RtWaypoint(c, x, cz, 11, 0x200);
        e = RtWaypoint(c, x, z1 + 3.0f, 12, 0);
        const bool ok = route({a, b, e}, 1, x, z1 + 6.0f);
        const S_vector &p = corner[k == 0 ? 3 : 2], &q = corner[k == 0 ? 0 : 1];
        RT(r.swap, ok && RtPathIs(ai, {Kept(a), Made(p, 0x1000), Made(q, 0x1000), Kept(e)}, {b}));
        RtClear(ai);
    }
    // A corner cut from min x to min z: one corner (0, past side 7 going up).
    a = RtWaypoint(c, x0 - 3.0f, z0 + d * 0.25f, 10, 0);
    b = RtWaypoint(c, x0 + w * 0.25f, z0 + d * 0.25f, 11, 0x200);
    e = RtWaypoint(c, x0 + w * 0.25f, z0 - 3.0f, 12, 0);
    RT(r.detour, route({a, b, e}, 0, x0 - 6.0f, z0 + d * 0.25f) &&
                     RtPathIs(ai, {Kept(a), Made(corner[0], 0x1000), Kept(e)}, {b}));
    RtClear(ai);
    // And from min z to min x (the near crossing on min z): corner 0 going down.
    a = RtWaypoint(c, x0 + w * 0.25f, z0 - 3.0f, 10, 0);
    b = RtWaypoint(c, x0 + w * 0.25f, z0 + d * 0.25f, 11, 0x200);
    e = RtWaypoint(c, x0 - 3.0f, z0 + d * 0.25f, 12, 0);
    RT(r.detour, route({a, b, e}, 2, x0 + w * 0.25f, z0 - 6.0f) &&
                     RtPathIs(ai, {Kept(a), Made(corner[0], 0x1000), Kept(e)}, {b}));
    RtClear(ai);
    // Both crossings on one leg (min x then max x), nearer max z: the corners over the top, before
    // the leg's end; nothing removed.
    a = RtWaypoint(c, x0 - 3.0f, cz + d * 0.125f, 10, 0);
    b = RtWaypoint(c, x1 + 3.0f, cz + d * 0.125f, 11, 0x200);
    e = RtWaypoint(c, x1 + 6.0f, cz + d * 0.125f, 12, 0);
    RT(r.legs, route({a, b, e}, 0, x0 - 6.0f, cz + d * 0.125f) &&
                   RtPathIs(ai, {Kept(a), Made(corner[3], 0x1000), Made(corner[2], 0x1000), Kept(b), Kept(e)}, {}));
    RtClear(ai);
    // The run starts the path: the tracker's leg counts (its waypoint on the footprint goes).
    a = RtWaypoint(c, cx - w * 0.25f, cz, 11, 0x200);
    b = RtWaypoint(c, cx - w * 0.25f, z1 + 3.0f, 12, 0);
    RT(r.legs, route({a, b}, 0, cx - w * 0.25f, z0 - 3.0f) &&
                   RtPathIs(ai, {Made(corner[0], 0x1000), Made(corner[3], 0x1000), Kept(b)}, {a}));
    RtClear(ai);
    // Two crossings, then the run goes on through the car: the first two count.
    a = RtWaypoint(c, cx - w * 0.25f, z0 - 3.0f, 10, 0);
    b = RtWaypoint(c, cx - w * 0.25f, cz, 11, 0x200);
    e = RtWaypoint(c, cx - w * 0.25f, z1 + 3.0f, 12, 0x200);
    f = RtWaypoint(c, cx + w * 0.25f, cz, 13, 0x200);
    S_AIWaypoint *g = RtWaypoint(c, cx + w * 0.25f, z0 - 3.0f, 14, 0);
    RT(r.legs, route({a, b, e, f, g}, 1, cx - w * 0.25f, z0 - 6.0f) &&
                   RtPathIs(ai, {Kept(a), Made(corner[0], 0x1000), Made(corner[3], 0x1000), Kept(e), Kept(f), Kept(g)}, {b}));
    RtClear(ai);
    // A run ending the path on the footprint (in through each side): the path ends at the crossing
    // (no flags).
    for (int side = 0; side < 4; ++side) {
        const float ax = side == 2 ? x0 - 3.0f : side == 3 ? x1 + 3.0f : cx;
        const float az = side == 0 ? z0 - 3.0f : side == 1 ? z1 + 3.0f : cz;
        const float bx = side == 0 ? cx + w * 0.125f : side == 1 ? cx - w * 0.2f : cx;
        const float bz = side == 2 ? cz + d * 0.125f : side == 3 ? cz - d * 0.2f : cz;
        a = RtWaypoint(c, ax, az, 10, 0);
        b = RtWaypoint(c, bx, bz, 11, 0x200);
        const S_vector la = RtLocal(c, a), lb = RtLocal(c, b);
        S_vector at;
        if (side < 2) {
            const float zs = side == 0 ? z0 : z1;
            const float t = (zs - la.z) / (lb.z - la.z);
            at = RtGround(c, la.x + (lb.x - la.x) * t, side == 0 ? zs - 0.3f : zs + 0.3f);
        } else {
            const float xs = side == 2 ? x0 : x1;
            const float t = (xs - la.x) / (lb.x - la.x);
            at = RtGround(c, side == 2 ? xs - 0.3f : xs + 0.3f, la.z + (lb.z - la.z) * t);
        }
        const float tx = ax + (side == 2 ? -3.0f : side == 3 ? 3.0f : 0.0f);
        const float tz = az + (side == 0 ? -3.0f : side == 1 ? 3.0f : 0.0f);
        RT(r.end, route({a, b}, static_cast<size_t>(side % 2), tx, tz) && RtPathIs(ai, {Kept(a), Made(at, 0)}, {b}));
        RtClear(ai);
    }
    // (Quirk kept: with one crossing the far point is the footprint's origin; a tracker nearer to it
    // ends the path there.)
    a = RtWaypoint(c, cx, z0 - 3.0f, 10, 0);
    b = RtWaypoint(c, cx, cz, 11, 0x200);
    RT(r.end, route({a, b}, 0, x1 + 0.5f, 0.0f) && RtPathIs(ai, {Kept(a), Made(RtGround(c, 0.0f, 0.0f), 0)}, {b}));
    RtClear(ai);
    // Failures: both crossings on one side; three crossings.
    a = RtWaypoint(c, cx - w * 0.25f, z0 - 3.0f, 10, 0);
    b = RtWaypoint(c, cx - w * 0.25f, cz, 11, 0x200);
    e = RtWaypoint(c, cx + w * 0.25f, cz, 12, 0x200);
    f = RtWaypoint(c, cx + w * 0.25f, z0 - 3.0f, 13, 0);
    RT(r.fail, unchanged(false, {a, b, e, f}, 0, cx - w * 0.25f, z0 - 6.0f));
    a = RtWaypoint(c, cx, cz, 10, 0);
    b = RtWaypoint(c, x1 + 3.0f, cz, 11, 0x200);
    e = RtWaypoint(c, x0 - 3.0f, cz + d * 0.125f, 12, 0);
    RT(r.fail, unchanged(false, {a, b, e}, 0, x0 - 6.0f, z0 - 6.0f));
}

void RouteTest(const char *path)
{
    if (!LoadModel(path))
        return;
    std::printf("== %s\n", g_modelFile.c_str());
    auto *car = new C_Car();
    car->SetFrame(g_model);
    C_Vehicle &v = car->m_vehicle;
    if (!v.m_body) {
        std::printf("route: no body\n");
        delete car;
        return;
    }
    // A BODY not upright (la_krupp's: its y along the model's x) has no ground plane for the test's
    // paths: the cases run with its rotation cleared (the route itself takes any).
    const S_matrix bodyAsLoaded = v.m_body->m_localMatrix;
    if (std::fabs(v.m_body->GetMatrix().m[1][1]) < 0.9f) {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                v.m_body->m_localMatrix.m[i][j] = i == j ? 1.0f : 0.0f;
    }
    car->m_position.y = 3.0f;                           // (the height the route takes, not the body's)
    for (uint16_t cp = 10; cp < 20; ++cp)
        car->m_marks.push_back({cp, true});
    g_rtTracker = NewFrame(FRAME_DUMMY, "tracker", nullptr);
    static S_RtAI raw;
    std::memset(&raw, 0, sizeof(raw));
    auto *ai = reinterpret_cast<C_MeleeTrackerAI *>(raw.ai);
    ai->m_meleeTracker = reinterpret_cast<C_MeleeTracker *>(raw.tracker);
    static_cast<C_Actor *>(ai->m_meleeTracker)->m_frame = g_rtTracker;
    S_Rt r;
    RtCases(r, ai, car, false);

    // The nearest side of equal ones: max z (a square footprint about an exactly placed BODY).
    {
        const S_matrix bodyLocal = v.m_body->m_localMatrix;
        const S_vector4 bodyMin = v.m_bodyMin, bodyMax = v.m_bodyMax;
        v.m_body->m_localMatrix.Identity();
        v.m_body->m_localMatrix.m[3][0] = 0.5f;
        v.m_body->m_localMatrix.m[3][1] = 0.25f;
        v.m_body->m_localMatrix.m[3][2] = -1.0f;
        v.m_bodyMin.x = v.m_bodyMin.z = -2.0f;
        v.m_bodyMax.x = v.m_bodyMax.z = 2.0f;
        const S_RtCar c = RtCar(car);
        S_AIWaypoint *a = RtWaypoint(c, 0.5f, 0.5f, 11, 0x200);
        RtSetPath(ai, {a}, 0);
        const S_vector at = S_vector(0.0f, 0.0f, 0.0f) * c.toWorld;
        RtPlace(at);
        const S_vector local = at * c.toCar;
        RT(r.step, local.x == 0.0f && local.z == 0.0f && ai->RouteAroundCars() &&
                       RtPathIs(ai, {Made(S_vector(0.0f, local.y, c.max[1] + 3.0f) * c.toWorld, 0x1000, 1.5f), Kept(a)}, {}));
        RtClear(ai);
        v.m_body->m_localMatrix = bodyLocal;
        v.m_bodyMin = bodyMin;
        v.m_bodyMax = bodyMax;
    }

    // Equal ways (a square footprint about a BODY at the world's origin, crossed at its middle): the
    // up way (the lengths compare strictly).
    {
        const S_matrix bodyLocal = v.m_body->m_localMatrix;
        const S_vector4 bodyMin = v.m_bodyMin, bodyMax = v.m_bodyMax;
        v.m_body->m_localMatrix.Identity();
        v.m_body->m_localMatrix.m[3][0] = -40.0f;     // (under the model's turn and place: the origin)
        v.m_body->m_localMatrix.m[3][1] = -5.0f;
        v.m_body->m_localMatrix.m[3][2] = -100.0f;
        v.m_bodyMin.x = v.m_bodyMin.z = -2.0f;
        v.m_bodyMax.x = v.m_bodyMax.z = 2.0f;
        const S_RtCar c = RtCar(car);
        S_AIWaypoint *a = RtWaypoint(c, 0.0f, c.min[1] - 3.0f, 10, 0);
        S_AIWaypoint *b = RtWaypoint(c, 0.0f, 0.0f, 11, 0x200);
        S_AIWaypoint *e = RtWaypoint(c, 0.0f, c.max[1] + 3.0f, 12, 0);
        RtSetPath(ai, {a, b, e}, 0);
        RtPlace(RtWorld(c, 0.0f, c.min[1] - 6.0f));
        RT(r.detour, c.toWorld.m[3][0] == 0.0f && c.toWorld.m[3][1] == 0.0f && c.toWorld.m[3][2] == 0.0f &&
                         ai->RouteAroundCars() &&
                         RtPathIs(ai, {Kept(a), Made(RtGround(c, c.max[0] + 0.3f, c.min[1] - 0.3f), 0x1000),
                                       Made(RtGround(c, c.max[0] + 0.3f, c.max[1] + 0.3f), 0x1000), Kept(e)}, {b}));
        RtClear(ai);
        v.m_body->m_localMatrix = bodyLocal;
        v.m_bodyMin = bodyMin;
        v.m_bodyMax = bodyMax;
    }

    // Two cars: the last listed that marked the run (another car with twice the box: a raw one, the
    // route reads only its marks, box, BODY and height).
    {
        static struct alignas(16) {
            unsigned char bytes[sizeof(C_Car)];
        } other;
        std::memset(&other, 0, sizeof(other));
        auto *car2 = reinterpret_cast<C_Car *>(other.bytes);
        car2->m_vehicle.m_body = v.m_body;
        car2->m_vehicle.m_bodyMin = v.m_bodyMin;
        car2->m_vehicle.m_bodyMax = v.m_bodyMax;
        car2->m_vehicle.m_bodyMin.x *= 2.0f;
        car2->m_vehicle.m_bodyMin.z *= 2.0f;
        car2->m_vehicle.m_bodyMax.x *= 2.0f;
        car2->m_vehicle.m_bodyMax.z *= 2.0f;
        car2->m_position = car->m_position;
        for (uint16_t cp = 10; cp < 20; ++cp)
            car2->m_marks.push_back({cp, true});
        g_cars.push_back(car2);
        for (int k = 0; k < 2; ++k) {
            // k 0: both marked the run: the other car's box; k 1: the other did not: this car's.
            if (k == 1)
                car2->m_marks.erase(car2->m_marks.begin() + 1);
            const S_RtCar c = RtCar(k == 0 ? car2 : car);
            const float x = c.min[0] + (c.max[0] - c.min[0]) * 0.25f, cz = (c.min[1] + c.max[1]) * 0.5f;
            S_AIWaypoint *a = RtWaypoint(c, x, c.min[1] - 3.0f, 10, 0);
            S_AIWaypoint *b = RtWaypoint(c, x, cz, 11, 0x200);
            S_AIWaypoint *e = RtWaypoint(c, x, c.max[1] + 3.0f, 12, 0);
            RtSetPath(ai, {a, b, e}, 0);
            RtPlace(RtWorld(c, x, c.min[1] - 6.0f));
            RT(r.cars, ai->RouteAroundCars() &&
                           RtPathIs(ai, {Kept(a), Made(RtGround(c, c.min[0] - 0.3f, c.min[1] - 0.3f), 0x1000),
                                         Made(RtGround(c, c.min[0] - 0.3f, c.max[1] + 0.3f), 0x1000), Kept(e)}, {b}));
            RtClear(ai);
        }
        g_cars.pop_back();
        std::vector<S_CarCheckpointMark>().swap(car2->m_marks);
    }

    // Tilted (BODY rolled 0.3 rad): the legs at the car's height, the step at the tracker's.
    {
        const S_matrix bodyLocal = v.m_body->m_localMatrix;
        S_matrix roll;
        roll.Identity();
        roll.m[0][0] = roll.m[1][1] = std::cos(0.3f);
        roll.m[0][1] = std::sin(0.3f);
        roll.m[1][0] = -std::sin(0.3f);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += roll.m[i][k] * bodyLocal.m[k][j];
                v.m_body->m_localMatrix.m[i][j] = sum;
            }
        RtCases(r, ai, car, true);
        const S_RtCar c = RtCar(car);
        const float cx = (c.min[0] + c.max[0]) * 0.5f, z0 = c.min[1], cz = (c.min[1] + c.max[1]) * 0.5f;
        const float w = c.max[0] - c.min[0];
        S_AIWaypoint *a = RtWaypoint(c, cx, z0 - 3.0f, 10, 0);
        S_AIWaypoint *b = RtWaypoint(c, cx + w * 0.125f, cz, 11, 0x200);
        const S_vector la = RtLocal(c, a), lb = RtLocal(c, b);
        const float t = (z0 - la.z) / (lb.z - la.z);
        RtSetPath(ai, {a, b}, 0);
        RtPlace(RtWorld(c, cx, z0 - 6.0f));
        RT(r.tilt, ai->RouteAroundCars() &&
                     RtPathIs(ai, {Kept(a), Made(RtGround(c, la.x + (lb.x - la.x) * t, z0 - 0.3f), 0)}, {b}));
        RtClear(ai);
        v.m_body->m_localMatrix = bodyLocal;
    }

    v.m_body->m_localMatrix = bodyAsLoaded;
    car->m_marks.clear();
    delete car;
    std::printf("route: unchanged %d step %d detour %d swap %d end %d legs %d fail %d cars %d tilt %d\n", r.unchanged,
                r.step, r.detour, r.swap, r.end, r.legs, r.fail, r.cars, r.tilt);
}
