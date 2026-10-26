// The meleeTracker's route around parked cars (C_MeleeTrackerAI::RouteAroundCars and its helpers,
// original 0x005c9080..0x005ca5a0). A path built over checkpoints a parked car marked
// (CHECKPOINT_FLAG_CAR_0200, which the waypoints keep) leads through the car. The route finds the car
// that marked the whole run and works on the ground (x, z) in its BODY frame's space, on the car's
// footprint shrunk to 85 %: a tracker standing on it first steps off it; otherwise the waypoints
// between the path's two crossings of the footprint give way to its corners (0.3 m out) the shorter
// way round, or, when the path ends on the footprint, the path ends where it gets there. The
// waypoints added for the corners and the step carry WAYPOINT_DETOUR, which also marks a path the
// route already changed.
#include "game/melee_tracker_ai.h"

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "game/ai_parts.h"
#include "game/car.h"
#include "game/checkpoints.h"
#include "game/melee_tracker.h"
#include "ls3d/i3d.h"
#include "ls3d/math.h"

namespace {

constexpr uint16_t WAYPOINT_DETOUR = 0x1000;            // a waypoint the route added (no checkpoint)
constexpr float FOOTPRINT_SCALE = 0.85f;                // the footprint the route takes, about the frame
constexpr float STEP_OFF_DISTANCE = 0.3f * 10.0f;       // past the footprint's side (the original's x87 0.3 x 10)
constexpr float CORNER_MARGIN = 0.3f;                   // the detour's corners outside the footprint
constexpr float CROSSING_MARGIN = 0.3f;                 // a crossing moved off the footprint
constexpr float TINY = 1e-08f;

// The footprint's corners 0 (min x, min z), 1 (max x, min z), 2 (max x, max z), 3 (min x, max z)
// have the codes 0, 2, 4, 6 going round; its sides the odd codes between them.
enum : int {
    SIDE_MIN_Z = 1,                                     // corners 0 - 1
    SIDE_MAX_X = 3,                                     // corners 1 - 2
    SIDE_MAX_Z = 5,                                     // corners 2 - 3
    SIDE_MIN_X = 7,                                     // corners 3 - 0
};

struct S_GroundPoint {
    float x, z;
};

// The length from a square the caller summed, as the AI's inlined helper: 0 below 1e-8, exactly 1
// for a near-unit square, the square root otherwise.
float LengthOf(float square)
{
    if (square < TINY)
        return 0.0f;
    if (!(std::fabs(square - 1.0f) < TINY))
        return std::sqrt(square);
    return 1.0f;
}

size_t PathSize(const C_MeleeTrackerAI *ai)
{
    return ai->m_pathBegin ? static_cast<size_t>(ai->m_pathEnd - ai->m_pathBegin) : 0;
}

// Inserts a waypoint at `index`, growing the array as the original's vector does (by its size, at
// least one). (0x005cadd0)
void PathInsert(C_MeleeTrackerAI *ai, size_t index, S_AIWaypoint *waypoint)
{
    if (ai->m_pathEnd != ai->m_pathCap) {
        for (S_AIWaypoint **p = ai->m_pathEnd; p != ai->m_pathBegin + index; --p)
            *p = p[-1];
        ai->m_pathBegin[index] = waypoint;
        ++ai->m_pathEnd;
        return;
    }
    const size_t size = PathSize(ai);
    const size_t capacity = size + (size < 2 ? 1 : size);
    auto **buffer = static_cast<S_AIWaypoint **>(operator new(capacity * sizeof(S_AIWaypoint *)));
    for (size_t i = 0; i < index; ++i)
        buffer[i] = ai->m_pathBegin[i];
    buffer[index] = waypoint;
    for (size_t i = index; i < size; ++i)
        buffer[i + 1] = ai->m_pathBegin[i];
    operator delete(ai->m_pathBegin);
    ai->m_pathBegin = buffer;
    ai->m_pathEnd = buffer + size + 1;
    ai->m_pathCap = buffer + capacity;
}

// Returns the waypoint at `index` to the pool and drops it from the path. (0x005caf70 for the drop)
void PathRemove(C_MeleeTrackerAI *ai, size_t index)
{
    ReleaseMeleeTrackerWaypoint(ai->m_pathBegin[index]);
    for (S_AIWaypoint **p = ai->m_pathBegin + index + 1; p != ai->m_pathEnd; ++p)
        p[-1] = *p;
    --ai->m_pathEnd;
}

// A waypoint of the route's own at `pos`. (The original leaves the position's w unset.)
S_AIWaypoint *RouteWaypoint(const S_vector &pos, uint16_t flags)
{
    S_AIWaypoint *waypoint = AllocMeleeTrackerWaypoint(-1, S_vector4{pos.x, pos.y, pos.z, 1.0f}, 0, 0, 0);
    waypoint->flags = flags;
    return waypoint;
}

// The path's first run of waypoints over checkpoints a car marked: its first and last index.
// (0x005c9fe0)
bool FindCarRun(const C_MeleeTrackerAI *ai, int *first, int *last)
{
    bool found = false;
    for (size_t i = 0; i < PathSize(ai); ++i) {
        if (ai->m_pathBegin[i]->flags & CHECKPOINT_FLAG_CAR_0200) {
            if (!found) {
                *first = static_cast<int>(i);
                found = true;
            }
            *last = static_cast<int>(i);
        } else if (found) {
            break;
        }
    }
    return found;
}

// The car that marked every checkpoint of the run, from the car list's end. (0x005ca030)
C_Car *FindParkedCar(const C_MeleeTrackerAI *ai, int first, int last)
{
    for (size_t i = g_cars.size(); i-- > 0;) {
        C_Car *car = g_cars[i];
        int k = first;
        while (k <= last && car->IsCheckpointMarked(ai->m_pathBegin[k]->checkpoint))
            ++k;
        if (k == last + 1)
            return car;
    }
    return nullptr;
}

// A tracker standing on the footprint (strictly inside) steps off it first: a waypoint 3 m past
// the nearest side (of equal ones the first in max z, min z, max x, min x order) goes first. True
// when it stands on it (nothing added without a path); false when it does not, or when the path
// already starts with a step of the route's own. (0x005c9080)
bool StepOffCar(C_MeleeTrackerAI *ai, const S_matrix &toWorld, const S_matrix &toCar, const float *min,
                const float *max, const S_vector4 &pos)
{
    S_vector local(pos.x, pos.y, pos.z);
    local *= toCar;
    if (!(local.x > min[0]) || !(local.x < max[0]) || !(local.z > min[1]) || !(local.z < max[1]))
        return false;
    if (PathSize(ai) == 0)
        return true;
    if (ai->m_pathBegin[0]->flags & WAYPOINT_DETOUR)
        return false;
    const float gaps[4] = {local.x - min[0], max[0] - local.x, local.z - min[1], max[1] - local.z};
    int nearest = 0;
    float best = FLT_MAX;
    for (int i = 4; i-- > 0;) {
        if (gaps[i] < best) {
            best = gaps[i];
            nearest = i;
        }
    }
    switch (nearest) {
    case 1:
        local.x = STEP_OFF_DISTANCE + max[0];
        break;
    case 2:
        local.z = min[1] - STEP_OFF_DISTANCE;
        break;
    case 3:
        local.z = STEP_OFF_DISTANCE + max[1];
        break;
    default:
        local.x = min[0] - STEP_OFF_DISTANCE;
        break;
    }
    local *= toWorld;
    S_AIWaypoint *step = AllocMeleeTrackerWaypoint(-1, S_vector4{local.x, local.y, local.z, pos.w}, 0, 0, 0);
    step->flags = WAYPOINT_DETOUR;
    PathInsert(ai, 0, step);
    return true;
}

// Where the leg (x0, z0) - (x1, z1) crosses the footprint's sides (strictly between their corners,
// and nearer each end than the leg is long), sides min x, max x, min z, max z in turn: the first
// crossing and the second (a later one overwrites the second), each moved 0.3 m off the footprint,
// with their side codes. (0x005ca0c0; the original's general line intersection has denominators
// dz x 0 - dx and dz - dx x 0.)
int CrossFootprint(const float *min, const float *max, float x0, float z0, float x1, float z1,
                   S_GroundPoint *crossings, int *sides)
{
    float dx = x1 - x0;
    float dz = z1 - z0;
    const float lengthSq = dz * dz + dx * dx;
    if (lengthSq == 0.0f)
        return 0;
    const float length = std::sqrt(lengthSq);
    if (length > TINY) {
        const float scale = 1.0f / length;
        dx = dx * scale;
        dz = scale * dz;
    }
    int count = 0;
    const auto within = [&](float x, float z) {
        return (z - z0) * (z - z0) + (x - x0) * (x - x0) < lengthSq &&
               (z - z1) * (z - z1) + (x - x1) * (x - x1) < lengthSq;
    };
    const auto add = [&](float x, float z, int side) {
        const int at = count == 0 ? 0 : 1;
        crossings[at] = {x, z};
        sides[at] = side;
        ++count;
    };
    const float acrossX = -dx;
    if (!(std::fabs(acrossX) < TINY)) {
        for (int k = 0; k < 2; ++k) {
            const float x = k == 0 ? min[0] : max[0];
            const float z = (-z0 * dx - (x - x0) * dz) / acrossX;
            if (z > min[1] && z < max[1] && within(x, z)) {
                if (k == 0)
                    add(x - CROSSING_MARGIN, z, SIDE_MIN_X);
                else
                    add(CROSSING_MARGIN + x, z, SIDE_MAX_X);
            }
        }
    }
    const float acrossZ = dz;
    if (!(std::fabs(acrossZ) < TINY)) {
        for (int k = 0; k < 2; ++k) {
            const float z = k == 0 ? min[1] : max[1];
            const float x = ((z - z0) * dx - -x0 * dz) / acrossZ;
            if (x > min[0] && x < max[0] && within(x, z)) {
                if (k == 0)
                    add(x, z - CROSSING_MARGIN, SIDE_MIN_Z);
                else
                    add(x, CROSSING_MARGIN + z, SIDE_MAX_Z);
            }
        }
    }
    return count;
}

// The corners passed going round the footprint from side code `from` to `to`, down (7, 5, 3, 1,
// 7...) or up. (0x005c93b0)
void WalkCorners(std::vector<int> &corners, int from, int to, bool down)
{
    corners.clear();
    while (from != to) {
        if (down) {
            corners.push_back((from - 1) / 2);
            from -= 2;
            if (from < 1)
                from = SIDE_MIN_X;
        } else {
            // Fix: the corner after side 7 is corner 0 (the original pushes 4, past the four
            // corners, and reads uninitialised memory for it).
            corners.push_back(((from + 1) / 2) & 3);
            from += 2;
            if (from > 8)
                from = SIDE_MIN_Z;
        }
    }
}

// The path's change for `hits` crossings: none for 0; for 1 (the path ends on the footprint) the
// path cut after `before` and ended at `crossing`; for 2 the waypoints between `before` and `after`
// replaced by the corners from side `fromSide` to `toSide` the shorter way round (WAYPOINT_DETOUR).
// False for more, or when the ways do not share the four corners (both crossings on one side).
// (0x005c95b0; Quirk kept: both ends of each way are measured from `crossing`, the near one: the
// caller passes the far one too, unread.)
bool Detour(C_MeleeTrackerAI *ai, int hits, const S_vector &crossing, int before, int after, int fromSide,
            int toSide, const float *min, const float *max, const S_matrix &toWorld)
{
    if (hits == 0)
        return true;
    if (hits == 1) {
        const size_t keep = static_cast<size_t>(before + 1);
        while (ai->m_pathBegin && PathSize(ai) > keep)
            PathRemove(ai, PathSize(ai) - 1);
        PathInsert(ai, PathSize(ai), RouteWaypoint(crossing, 0));
        return true;
    }
    if (hits != 2)
        return false;
    S_vector corners[4] = {
        S_vector(min[0] - CORNER_MARGIN, 0.0f, min[1] - CORNER_MARGIN),
        S_vector(CORNER_MARGIN + max[0], 0.0f, min[1] - CORNER_MARGIN),
        S_vector(CORNER_MARGIN + max[0], 0.0f, CORNER_MARGIN + max[1]),
        S_vector(min[0] - CORNER_MARGIN, 0.0f, CORNER_MARGIN + max[1]),
    };
    for (int i = 4; i-- > 0;)
        corners[i] *= toWorld;
    std::vector<int> down, up;
    WalkCorners(down, fromSide, toSide, true);
    WalkCorners(up, fromSide, toSide, false);
    if (down.empty() || up.empty() || down.size() + up.size() != 4)
        return false;
    // The ways' lengths (the squares summed as the original does at each place).
    S_vector v = crossing - corners[down.front()];
    float downLength = LengthOf((v.x * v.x + v.z * v.z) + v.y * v.y);
    v = crossing - corners[down.back()];
    downLength = LengthOf((v.x * v.x + v.z * v.z) + v.y * v.y) + downLength;
    v = crossing - corners[up.front()];
    float upLength = LengthOf((v.x * v.x + v.z * v.z) + v.y * v.y);
    v = crossing - corners[up.back()];
    upLength = LengthOf((v.z * v.z + v.y * v.y) + v.x * v.x) + upLength;
    for (size_t i = down.size() - 1; i-- > 0;)
        downLength = (corners[down[i]] - corners[down[i + 1]]).Magnitude() + downLength;
    for (size_t i = up.size() - 1; i-- > 0;)
        upLength = (corners[up[i]] - corners[up[i + 1]]).Magnitude() + upLength;
    const std::vector<int> &way = downLength < upLength ? down : up;
    const int at = before + 1;
    for (int n = after - at; n > 0; --n)
        PathRemove(ai, static_cast<size_t>(at));
    for (size_t i = way.size(); i-- > 0;)
        PathInsert(ai, static_cast<size_t>(at), RouteWaypoint(corners[way[i]], WAYPOINT_DETOUR));
    return true;
}

}  // namespace

bool C_MeleeTrackerAI::RouteAroundCars()                // 0x005ca5a0
{
    // A path the route changed already (its first or second waypoint is its own) stays.
    const size_t size = PathSize(this);
    if (size != 0) {
        if (m_pathBegin[0]->flags & WAYPOINT_DETOUR)
            return true;
        if (size > 1 && (m_pathBegin[1]->flags & WAYPOINT_DETOUR))
            return true;
    }
    int first = -1, last = -1;
    if (!FindCarRun(this, &first, &last))
        return true;
    C_Car *car = FindParkedCar(this, first, last);
    if (!car)
        return true;
    I3D_frame *body = nullptr;
    float min[2], max[2];
    car->GetFootprint(&body, min, max);
    min[0] *= FOOTPRINT_SCALE;
    min[1] *= FOOTPRINT_SCALE;
    max[0] *= FOOTPRINT_SCALE;
    max[1] *= FOOTPRINT_SCALE;
    // (The original also copies the matrix into 0x008ac6e0, which nothing reads.)
    const S_matrix toWorld = body->GetMatrix();
    S_matrix toCar = toWorld;
    toCar.Inverse(toWorld);
    I3D_frame *frame = m_meleeTracker->GetFrame();
    const S_matrix &at = frame->GetMatrix();
    if (StepOffCar(this, toWorld, toCar, min, max, S_vector4{at.m[3][0], at.m[3][1], at.m[3][2], at.m[3][3]}))
        return true;
    // The legs on the ground in the car's space (at the car's height): from the tracker when the
    // run starts the path, then over the run with a waypoint more each side where there is one.
    std::vector<S_GroundPoint> points;
    points.push_back({frame->GetWorldPos().x, frame->GetWorldPos().z});
    const int from = first - (first != 0 ? 1 : 0);
    const int to = last + (last != static_cast<int>(size) - 1 ? 1 : 0);
    for (int i = from; i <= to; ++i)
        points.push_back({m_pathBegin[i]->pos.x, m_pathBegin[i]->pos.z});
    const float height = car->GetPosition4().y;
    for (S_GroundPoint &point : points) {
        S_vector v(point.x, height, point.z);
        v *= toCar;
        point = {v.x, v.z};
    }
    // The first two crossings, with the waypoints before the first and after the second.
    int hits = 0;
    S_GroundPoint nearHit = {0.0f, 0.0f}, farHit = {0.0f, 0.0f};
    int nearSide = 0, farSide = 4;
    int before = -1, after = -1;
    const int legs = static_cast<int>(points.size()) - 1;
    for (int k = first == 0 ? 0 : 1; k < legs; ++k) {
        const int end = k - (first != 0 ? 1 : 0) + first;  // the path index of the leg's end
        S_GroundPoint crossings[2];
        int sides[2];
        const int n = CrossFootprint(min, max, points[k].x, points[k].z, points[k + 1].x, points[k + 1].z,
                                     crossings, sides);
        if (n != 0) {
            if (hits == 0) {
                nearHit = crossings[0];
                nearSide = sides[0];
                before = end - 1;
            } else {
                farHit = crossings[0];
                farSide = sides[0];
                after = end;
            }
            if (n == 2) {
                farHit = crossings[1];
                farSide = sides[1];
                after = end;
            }
        }
        hits += n;
        if (hits >= 2)
            break;
    }
    // Back to the world; the crossing nearer the tracker comes first. (Quirk kept: with one crossing
    // the other point is the footprint's origin, and a tracker nearer to that ends the path there.)
    S_vector nearPoint(nearHit.x, 0.0f, nearHit.z), farPoint(farHit.x, 0.0f, farHit.z);
    nearPoint *= toWorld;
    farPoint *= toWorld;
    const S_vector &pos = frame->GetWorldPos();
    S_vector d = pos - nearPoint;
    const float nearDistance = LengthOf((d.z * d.z + d.y * d.y) + d.x * d.x);
    d = pos - farPoint;
    const float farDistance = LengthOf((d.z * d.z + d.y * d.y) + d.x * d.x);
    if (farDistance < nearDistance) {
        std::swap(nearPoint, farPoint);
        std::swap(nearSide, farSide);
    }
    return Detour(this, hits, nearPoint, before, after, nearSide, farSide, min, max, toWorld);
}
