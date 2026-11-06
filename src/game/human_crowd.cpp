// The humans keeping out of each other's way (0x005641b0..0x0056ea00): each tick, in single player and co-op, every AI human's next position is worked out
// with those of the others near it and the cars standing near it, in eight small steps: each is pushed out of the others and held inside the checkpoint
// network (its circles and the strips between them). What comes out is the step C_HumanAIMemory gives the AI (C_AIController walks along it).
// A route blocked by a human or a standing car is planned around it (FindBlocker, ExpandNode, Replan).
//
// Sums of squares etc. are not always in the original's order: only the last bits of a float can differ.
#include <cmath>
#include <cstring>
#include <vector>

#include "common/chunk.h"
#include "game/ai_parts.h"
#include "game/car.h"
#include "game/checkpoints.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/human_crowd.h"
#include "game/human_movement.h"
#include "game/pending.h"
#include "game/profile.h"
#include "ls3d/i3d.h"

namespace crowd {

const float HUMAN_RADIUS = 0.8f;
const float HUMAN_RADIUS_SQ = 0.64000005f;
const float PRONE_MARGIN = 0.4f;
const float SAME_FLOOR = 2.0f;                          // how far apart in height two can be and still meet
const float NEAR_CAR_SQ = 49.0f;
const float WALK_SPEED_SCALE = 1.4f;                    // 0x0084e920
const int SOLVER_STEPS = 8;
const int NEAREST_SEARCH_DEPTH = 4;
const int NEAREST_SEARCH_DEPTH_REPLAN = 5;
const uint8_t HUMAN_FLAG_SUSPENDED = 4;
const uint8_t HUMAN_FLAGS_OUT = 0x3F;                   // dead, unconscious, suspended, with an order, in a vehicle, climbing
const uint8_t HUMAN_FLAGS_NOT_BLOCKING = 0x37;          // (all but having an order)

using Waypoints = std::vector<S_AIWaypoint *>;

bool IsInvalid(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits == 0x7FC00000 || bits == 0xFFC00000;
}

bool IsNegative(float value)                            // (the original's tests of the sign bit)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return (bits & 0x80000000u) != 0;
}

// The original's approximate 1 / sqrt.
float FastInvSqrt(float value)
{
    int32_t bits;
    std::memcpy(&bits, &value, 4);
    bits = 0x5F3759DF - (bits >> 1);
    float estimate;
    std::memcpy(&estimate, &bits, 4);
    return (1.5f - estimate * estimate * value * 0.5f) * estimate;
}

S_vector Xyz(const S_vector4 &value) { return S_vector(value.x, value.y, value.z); }
S_vector4 Xyzw(const S_vector &value, float w = 0.0f) { return {value.x, value.y, value.z, w}; }
float Dot(const S_vector &a, const S_vector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Sq(const S_vector &a) { return Dot(a, a); }
float SqFlat(const S_vector &a) { return a.x * a.x + a.z * a.z; }
S_vector Flat(const S_vector &a) { return S_vector(a.x, 0.0f, a.z); }
S_vector Sub4(const S_vector4 &a, const S_vector4 &b) { return S_vector(a.x - b.x, a.y - b.y, a.z - b.z); }

// As the game's inline copies do it (y² + x² + z² here, to the last bits).
S_vector NormalizedZero(S_vector v)
{
    const float sq = Sq(v);
    if (std::fabs(sq - 1.0f) >= 1e-8f) {
        if (sq >= 1e-8f) {
            const float k = 1.0f / std::sqrt(sq);
            v = S_vector(v.x * k, v.y * k, v.z * k);
        } else if (v.x != 0.0f) {
            v.x = v.x >= 0.0f ? 1.0f : -1.0f;
        } else if (v.z != 0.0f) {
            v.z = v.z >= 0.0f ? 1.0f : -1.0f;
        } else {
            v.y = v.y >= 0.0f ? 1.0f : -1.0f;
        }
    }
    return v;
}

// An upright box seen as an ellipsoid: its axes and half sizes.
struct S_Ellipsoid {
    S_vector axis[3];
    S_vector half;

    S_vector Local(const S_vector &v) const
    {
        return S_vector(Dot(v, axis[0]) / half.x, Dot(v, axis[1]) / half.y, Dot(v, axis[2]) / half.z);
    }
    S_vector World(const S_vector &v) const
    {
        const S_vector x = axis[0] * (v.x * half.x);
        const S_vector y = axis[1] * (v.y * half.y);
        const S_vector z = axis[2] * (v.z * half.z);
        return S_vector(x.x + y.x + z.x, x.y + y.y + z.y, x.z + y.z + z.z);
    }
};

S_Ellipsoid ProneEllipsoid(const C_HumanAIMemory &memory)
{
    S_Ellipsoid shape;
    shape.axis[0] = Xyz(memory.axisX);
    shape.axis[1] = Xyz(memory.axisY);
    shape.axis[2] = Xyz(memory.axisZ);
    shape.half = S_vector(memory.halfSize[0] + PRONE_MARGIN, memory.halfSize[1] + PRONE_MARGIN, memory.halfSize[2] + PRONE_MARGIN);
    return shape;
}

// The car's half sizes are 0.55 of its box's (C_Car +0x210, +0x220).
S_vector CarHalfSize(const C_Car *car)
{
    const float *const box = reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(car) + 0x210);
    return S_vector((box[4] - box[0]) * 0.55f, (box[5] - box[1]) * 0.55f, (box[6] - box[2]) * 0.55f);
}

S_Ellipsoid CarEllipsoid(const C_Car *car, const S_vector &halfSize)
{
    const S_matrix &world = car->GetFrame()->GetMatrix();
    S_Ellipsoid shape;
    shape.axis[0] = world(0);
    shape.axis[1] = world(1);
    shape.axis[2] = world(2);
    shape.half = S_vector(halfSize.x + PRONE_MARGIN, halfSize.y + PRONE_MARGIN, halfSize.z + PRONE_MARGIN);
    return shape;
}

// Adds a push to the pushes so far, not more than needed along the same line (0x00565540).
void AddPush(S_vector4 &total, const S_vector &push)
{
    const S_vector sum = Xyz(total);
    const float sq = Sq(sum);
    if (sq < 1e-8f) {
        total = Xyzw(push, total.w);
        return;
    }
    const float along = Dot(push, sum) / sq;
    if (IsNegative(along)) {
        total.x += push.x;
        total.y += push.y;
        total.z += push.z;
        return;
    }
    if (along > 1.0f) {
        const float back = along * sq / Sq(push);
        total.x = (total.x + push.x) - back * push.x;
        total.y = (total.y + push.y) - back * push.y;
        total.z = (total.z + push.z) - back * push.z;
        return;
    }
    total.x = (push.x - along * total.x) + total.x;
    total.y = (push.y - along * total.y) + total.y;
    total.z = (push.z - along * total.z) + total.z;
}

// Of the two roots, the first ahead (>= 0), not beyond 1.
bool FirstRootAhead(float doubled, float discriminant, float a, float *t)
{
    const float root = std::sqrt(discriminant);
    const float k = 1.0f / (a + a);
    float first = (root - doubled) * k;
    const float second = (-doubled - root) * k;
    if (!IsNegative(second) && second < first)
        first = second;
    if (IsNegative(first)) {
        first = second;
        if (IsNegative(first))
            return false;
    }
    if (!(first <= 1.0f))
        return false;
    *t = first;
    return true;
}

// Coming through a unit sphere: `l` where we are in it, `r` the relative velocity; aside, in the sphere's coordinates, for how far off it passes (the common
// ends of 0x00566130, 0x00568580).
bool SideStepEllipsoid(const S_vector &l, const S_vector &r, float c, float scale, S_vector *out)
{
    const float a = Sq(r);
    if (a < 1e-8f)
        return false;
    const float half = Dot(r, l);
    const float doubled = half + half;
    if (!IsNegative(doubled))
        return false;
    const float inverse = 1.0f / a;
    const float k = half * inverse;
    const S_vector off(l.x - r.x * k, l.y - r.y * k, l.z - r.z * k);
    if (!(Sq(off) <= 1.0f))
        return false;
    const float discriminant = doubled * doubled - a * c * 4.0f;
    if (IsNegative(discriminant))
        return false;
    float t;
    if (!FirstRootAhead(doubled, discriminant, a, &t))
        return false;
    const float tangentX = r.z, tangentZ = -r.x;
    const float v = ((l.y + t * r.y) * 0.0f + tangentX * (l.x + t * r.x) + tangentZ * (l.z + t * r.z)) * inverse;
    const float px = tangentX * v, pz = tangentZ * v;
    float force = 1.0f - (pz * pz + px * px);
    if (IsNegative(v))
        force = -force;
    force *= FastInvSqrt(a);
    const float s = (1.0f - t) * scale;
    *out = S_vector(tangentX * force * s, force * 0.0f * s, tangentZ * force * s);
    return true;
}

// A checkpoint's circle radius.
float RadiusOf(const S_Checkpoint &point) { return static_cast<float>(point.radius) * 0.0625f; }

// `pos` kept within the strip between two circles: true when it was (0x00567df0).
bool KeepInsideStrip(S_vector &pos, const S_vector &a, float radiusA, const S_vector &b, float radiusB)
{
    const S_vector ab = Flat(S_vector(b.x - a.x, b.y - a.y, b.z - a.z));
    const S_vector fromB(pos.x - b.x, pos.y - b.y, pos.z - b.z);
    const float radiusBSq = radiusB * radiusB;
    if (SqFlat(fromB) < radiusBSq)
        return true;
    const S_vector fromA(pos.x - a.x, pos.y - a.y, pos.z - a.z);
    const float distASq = SqFlat(fromA);
    const float radiusASq = radiusA * radiusA;
    if (distASq < radiusASq)
        return true;
    const float abSq = SqFlat(ab);
    const auto pull = [&pos](const S_vector &offset, float length, float radius) {
        const float k = (length - radius) / length;
        pos.x -= offset.x * k;
        pos.y -= offset.y * k;
        pos.z -= offset.z * k;
    };
    if (abSq <= 1e-8f || IsNegative((ab.x * fromA.x + fromA.z * ab.z) / abSq)) {
        if (distASq > radiusASq) {
            pull(Flat(fromA), std::sqrt(distASq), radiusA);
            return false;
        }
        return true;
    }
    const float t = (ab.x * fromA.x + fromA.z * ab.z) / abSq;
    if (t <= 1.0f) {
        const S_vector off(fromA.x - ab.x * t, 0.0f, fromA.z - ab.z * t);
        const float radius = t * radiusB + (1.0f - t) * radiusA;
        const float offSq = SqFlat(off);
        if (offSq > radius * radius) {
            pull(off, std::sqrt(offSq), radius);
            return false;
        }
        return true;
    }
    const float distBSq = SqFlat(fromB);
    if (distBSq > radiusBSq) {
        pull(Flat(fromB), std::sqrt(distBSq), radiusB);
        return false;
    }
    return true;
}

// `pos` kept within the point's circle or a strip to a neighbour: true when it was, with the point it is at; else it is moved to the nearest such place
// (0x00568190).
bool KeepOnNetwork(const C_CheckpointNetwork *network, S_vector4 &pos, const S_Checkpoint *point, const S_Checkpoint **outPoint)
{
    if (!point)
        return false;
    const S_vector center = point->pos;
    const float radius = RadiusOf(*point);
    const S_vector offset = Flat(S_vector(pos.x - center.x, pos.y - center.y, pos.z - center.z));
    const float offsetSq = SqFlat(offset);
    if (offsetSq < radius * radius) {
        *outPoint = point;
        return true;
    }
    const float k = (std::sqrt(offsetSq) - radius) / std::sqrt(offsetSq);
    S_vector best(pos.x - offset.x * k, pos.y - k * 0.0f, pos.z - offset.z * k);
    float bestSq = Sq(S_vector(best.x - pos.x, best.y - pos.y, best.z - pos.z));
    const S_Checkpoint *bestPoint = point;
    const S_vector here(pos.x, pos.y, pos.z);
    for (int i = (point->flags & CHECKPOINT_LINK_COUNT) - 1; i >= 0; --i) {
        const S_Checkpoint *const other = network->Get(point->links[i].target);
        const S_vector otherPos = other->pos;
        S_vector moved = here;
        if (KeepInsideStrip(moved, center, radius, otherPos, RadiusOf(*other))) {
            const S_vector toOther(otherPos.x - pos.x, otherPos.y - pos.y, otherPos.z - pos.z);
            const S_vector toPoint(center.x - pos.x, center.y - pos.y, center.z - pos.z);
            *outPoint = Sq(toOther) < Sq(toPoint) ? other : point;
            return true;
        }
        const float movedSq = Sq(S_vector(moved.x - pos.x, moved.y - pos.y, moved.z - pos.z));
        if (movedSq < bestSq) {
            best = moved;
            bestSq = movedSq;
            bestPoint = other;
        }
    }
    pos.x = best.x;
    pos.y = best.y;
    pos.z = best.z;
    *outPoint = bestPoint;
    return false;
}

// The four corners of the unit square in the plane, from `pos`: which side of `side` they lie on and, each side, in order along `dir` (0x0056de90).
void SortCorners(const S_vector &pos, const S_vector &side, const S_vector &dir, S_vector corners[4], int *negative, int *positive, int *negativeCount, int *positiveCount)
{
    static const float SIGNS[4][2] = {{1.0f, 1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {-1.0f, -1.0f}};
    float along[4];
    int countNegative = 0, countPositive = 0;
    for (int i = 0; i < 4; ++i) {
        corners[i] = S_vector(SIGNS[i][0] - pos.x, -pos.y, SIGNS[i][1] - pos.z);
        along[i] = Dot(dir, corners[i]);
        const bool isNegative = IsNegative(Dot(corners[i], side));
        int *const list = isNegative ? negative : positive;
        int &count = isNegative ? countNegative : countPositive;
        int at = 0;
        while (at < count && !(along[i] <= along[list[at]]))
            ++at;
        for (int j = count; j > at; --j)
            list[j] = list[j - 1];
        list[at] = i;
        ++count;
    }
    *negativeCount = countNegative;
    *positiveCount = countPositive;
}

}  // namespace crowd

using namespace crowd;

// ---- the memory's saved part -----------------------------------------------------------------------------------------------------

bool C_HumanAIMemory::SaveGame(C_chunk &chunk)
{
    chunk.Begin(100);
    chunk.WriteChunk(0x3E9, &stuckTimer, 2);
    chunk.WriteChunk(0x3EA, &bestDistance, 4);
    chunk.WriteChunk(0x3EB, &plannedFrom, 4);
    chunk.WriteChunk(0x3EC, &routeChanged, 1);
    chunk.WriteChunk(0x3ED, &notPlayer, 1);
    chunk.WriteChunk(0x3EE, &walking, 1);
    chunk.WriteChunk(0x3EF, &solved, 1);
    chunk.WriteChunk(0x3F0, &givesWay, 1);
    chunk.WriteChunk(0x3F1, &unknown109, 1);
    chunk.WriteChunk(0x3F2, &lastGoal, 16);
    chunk.WriteChunk(0x3F3, &routeStart, 16);
    chunk.Ascend();
    return true;
}

bool C_HumanAIMemory::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.Descend(id))
        return false;
    if (id == 100) {
        while (chunk.HasMore()) {
            if (!chunk.Descend(id))
                break;
            switch (id) {
            case 0x3E9: chunk.Read(&stuckTimer, 2); break;
            case 0x3EA: chunk.Read(&bestDistance, 4); break;
            case 0x3EB: chunk.Read(&plannedFrom, 4); break;
            case 0x3EC: chunk.Read(&routeChanged, 1); break;
            case 0x3ED: chunk.Read(&notPlayer, 1); break;
            case 0x3EE: chunk.Read(&walking, 1); break;
            case 0x3EF: chunk.Read(&solved, 1); break;
            case 0x3F0: chunk.Read(&givesWay, 1); break;
            case 0x3F1: chunk.Read(&unknown109, 1); break;
            case 0x3F2: chunk.Read(&lastGoal, 16); break;
            case 0x3F3: chunk.Read(&routeStart, 16); break;
            default: break;
            }
            chunk.Ascend();
        }
    }
    chunk.Ascend();
    return true;
}

// ---- the pass -------------------------------------------------------------------------------------------------------------------

C_HumanCrowd g_humanCrowd;

namespace crowd {

const C_CheckpointNetwork *Network() { return GetGame()->GetCheckpoints(); }

bool RouteHasPoints(const Waypoints *route) { return route && !route->empty(); }

float PointRate(C_Human *owner)
{
    int speedClass = owner->GetMoveSpeedClass();
    if (speedClass < 0)
        speedClass = 1;
    return WALK_SPEED_SCALE * g_moveSpeeds[0][speedClass + owner->GetStance() * 6];
}

// The next waypoint within reach of this move is skipped to (0x005653c0).
void AdvanceWaypoint(C_HumanAIMemory &memory, float time)
{
    const Waypoints &route = *memory.route;
    uint16_t index = memory.waypoint;
    S_vector previous = index == 0 ? Xyz(memory.lastGoal) : Xyz(route[index - 1u]->pos);
    const unsigned count = static_cast<uint16_t>(route.size());
    while (static_cast<uint16_t>(count - 1u) > index) {
        const S_vector point = Xyz(route[memory.waypoint]->pos);
        const S_vector ahead(point.x - (time * memory.velocity.x + memory.pos.x), point.y - (time * memory.velocity.y + memory.pos.y),
                             point.z - (time * memory.velocity.z + memory.pos.z));
        const S_vector leg(point.x - previous.x, point.y - previous.y, point.z - previous.z);
        if (!IsNegative(Dot(leg, ahead)) && 0.0025000002f < Sq(ahead))
            return;
        previous = point;
        index = static_cast<uint16_t>(memory.waypoint + 1u);
        memory.waypoint = index;
    }
}

// One small step along the way, or as the pushes say with no route (0x00565f30).
void MoveOneStep(C_HumanAIMemory &memory, float step)
{
    bool move = true;
    if (!RouteHasPoints(memory.route)) {
        if (memory.walking) {
            const float k = 1.0f / step;
            memory.velocity.x = k * memory.push.x;
            memory.velocity.y = k * memory.push.y;
            memory.velocity.z = k * memory.push.z;
            move = false;
        }
    } else {
        AdvanceWaypoint(memory, step);
        const S_AIWaypoint *const waypoint = (*memory.route)[memory.waypoint];
        memory.target = waypoint->pos;
        memory.targetPoint = Network()->Get(waypoint->checkpoint);
        const float dx = memory.target.x - memory.pos.x, dz = memory.target.z - memory.pos.z;
        const float sq = dx * dx + dz * dz;
        if (1e-06f < sq) {
            const float k = FastInvSqrt(sq) * memory.speed;
            memory.velocity.x = k * dx;
            memory.velocity.y = k * 0.0f;
            memory.velocity.z = dz * k;
        }
    }
    if (move) {
        memory.pos.x += step * memory.velocity.x;
        memory.pos.y += step * memory.velocity.y;
        memory.pos.z += step * memory.velocity.z;
    }
    if (memory.owner->GetStance() == 2)
        memory.boxCenter = memory.pos;
}

// Pushed out of another human (0x00566130).
void PushFromHuman(C_HumanAIMemory &self, const C_HumanAIMemory &other)
{
    if (!self.solved)
        return;
    S_vector offset = Sub4(self.pos, other.pos);
    if (!(std::fabs(offset.y) <= SAME_FLOOR))
        return;
    offset.y = 0.0f;
    if (other.owner->GetStance() == 2) {
        const S_Ellipsoid shape = ProneEllipsoid(other);
        S_vector l = shape.Local(offset);
        const float c = Sq(l) - 1.0f;
        if (c <= 1e-8f) {
            if (1e-8f < c + 1.0f) {
                const float length = std::sqrt(c + 1.0f);
                const float k = (1.0f - length) / length;
                AddPush(self.push, shape.World(S_vector(l.x * k, l.y * k, l.z * k)));
            }
        } else if (other.givesWay) {
            S_vector relative = Sub4(self.velocity, other.velocity);
            relative.y = 0.0f;
            S_vector aside;
            if (SideStepEllipsoid(l, shape.Local(relative), c, 0.125f, &aside))
                AddPush(self.push, shape.World(aside));
        }
        return;
    }
    const float c = SqFlat(offset) - HUMAN_RADIUS_SQ;
    if (c <= 1e-8f) {
        const float sq = c + HUMAN_RADIUS_SQ;
        const float speedSq = Sq(Xyz(self.velocity));
        if (1e-8f < sq && 1e-8f < speedSq) {
            const float t = Dot(offset, Xyz(self.velocity)) / speedSq;
            const S_vector across(offset.x - t * self.velocity.x, offset.y - t * self.velocity.y, offset.z - t * self.velocity.z);
            const float acrossSq = Sq(across);
            if (acrossSq <= 1e-8f) {
                const float length = std::sqrt(sq);
                AddPush(self.push, offset * ((0.55f / length) * (HUMAN_RADIUS - length)));
            } else {
                const float length = std::sqrt(acrossSq);
                AddPush(self.push, across * ((0.75f / length) * (HUMAN_RADIUS - length)));
            }
        }
    } else if (other.givesWay) {
        S_vector relative = Sub4(self.velocity, other.velocity);
        relative.y = 0.0f;
        const float a = SqFlat(relative);
        if (1e-8f <= a) {
            const float half = offset.x * relative.x + offset.z * relative.z;
            const float doubled = half + half;
            if (IsNegative(doubled)) {
                const float inverse = 1.0f / a;
                const float k = inverse * half;
                const S_vector off(offset.x - k * relative.x, 0.0f, offset.z - k * relative.z);
                const float discriminant = doubled * doubled - a * c * 4.0f;
                float t;
                if (Sq(off) <= HUMAN_RADIUS_SQ && !IsNegative(discriminant) && FirstRootAhead(doubled, discriminant, a, &t)) {
                    const float tangentX = relative.z, tangentZ = -relative.x;
                    const float atX = offset.x + relative.x * t, atZ = offset.z + relative.z * t;
                    const float v = (atZ * tangentZ + tangentX * atX) * inverse;
                    const float px = tangentX * v, pz = tangentZ * v;
                    float force = 1.0f - (pz * pz + px * px) * 1.5625f;
                    force *= IsNegative(v) ? -0.4f : 0.4f;
                    force *= FastInvSqrt(a);
                    const float s = (1.0f - t) * 0.125f;
                    AddPush(self.push, S_vector(tangentX * force * s, force * 0.0f * s, tangentZ * force * s));
                }
            }
        }
    }
}

// Sets a target pushed on, kept on the network (the ends of 0x00566e50, 0x00568dc0).
void MoveTarget(C_HumanAIMemory &memory, const S_vector &by)
{
    S_vector4 moved = {memory.target.x + by.x, memory.target.y + by.y, memory.target.z + by.z, 0.0f};
    if (memory.targetPoint)
        KeepOnNetwork(Network(), moved, memory.targetPoint, &memory.targetPoint);
    memory.target = moved;
}

// Two targets too close (0x00566e50).
void PushTargetFromHuman(C_HumanAIMemory &self, C_HumanAIMemory &other)
{
    if (!self.solved || (self.route && self.route->empty()))
        return;
    S_vector offset = Sub4(self.target, other.target);
    if (!(std::fabs(offset.y) <= SAME_FLOOR))
        return;
    offset.y = 0.0f;
    S_vector by;
    if (other.owner->GetStance() == 2) {
        const S_Ellipsoid shape = ProneEllipsoid(other);
        S_vector l = shape.Local(offset);
        const float sq = Sq(l);
        if (1e-06f <= sq - 1.0f)
            return;
        if (1e-8f < sq) {
            const float length = std::sqrt(sq);
            const float k = other.solved ? (0.75f / length) * (1.0f - length) : (1.0f - length) / length;
            by = shape.World(S_vector(l.x * k, l.y * k, l.z * k));
        } else {
            // On top of each other: along where they themselves are.
            const S_vector apart = Sub4(self.pos, other.pos);
            by = shape.World(NormalizedZero(S_vector(Dot(apart, shape.axis[0]), Dot(apart, shape.axis[1]), Dot(apart, shape.axis[2]))));
        }
    } else {
        const float sq = Sq(offset);
        if (1e-06f <= sq - HUMAN_RADIUS_SQ)
            return;
        if (1e-8f < sq) {
            const float length = std::sqrt(sq);
            by = offset * ((0.75f / length) * (HUMAN_RADIUS - length));
        } else {
            by = NormalizedZero(Sub4(self.pos, other.pos)) * HUMAN_RADIUS;
        }
    }
    MoveTarget(self, by);
    if (other.solved && RouteHasPoints(other.route))
        MoveTarget(other, S_vector(-by.x, -by.y, -by.z));
}

// The push also taken, kept on the network (0x00567ab0).
void PushOntoNetwork(C_HumanAIMemory &memory)
{
    if (!memory.solved)
        return;
    const C_CheckpointNetwork *const network = Network();
    if (!memory.nearPoint)
        memory.nearPoint = network->Get(network->FindNearestPoint(Xyz(memory.pos), 5.0f, -1.0f));
    if (!memory.nearPoint)
        return;
    S_vector4 kept = memory.pos;
    if (!KeepOnNetwork(network, kept, memory.nearPoint, &memory.currentPoint))
        AddPush(memory.push, Sub4(kept, memory.pos));
}

// The position kept on the network (0x00567ba0).
void HoldOnNetwork(C_HumanAIMemory &memory)
{
    if (!memory.solved)
        return;
    const C_CheckpointNetwork *const network = Network();
    if (!memory.nearPoint)
        memory.nearPoint = network->Get(network->FindNearestPoint(Xyz(memory.pos), 5.0f, -1.0f));
    if (!memory.nearPoint)
        return;
    S_vector4 kept = memory.pos;
    if (!KeepOnNetwork(network, kept, memory.nearPoint, &memory.currentPoint))
        memory.pos = kept;
}

// A push almost straight back is turned aside a bit, so that two do not stand nose to nose (0x00567c80).
void TurnPushAside(C_HumanAIMemory &memory, float step)
{
    if (!memory.solved)
        return;
    const S_vector velocity = Xyz(memory.velocity), push = Xyz(memory.push);
    const float speedSq = Sq(velocity);
    if (!(1e-8f < Sq(push)) || !(1e-8f < speedSq))
        return;
    float along = Dot(velocity, push);
    if (!(along < -1e-08f))
        return;
    along /= speedSq;
    const S_vector back(along * velocity.x, along * velocity.y, along * velocity.z);
    const S_vector across(push.x - back.x, push.y - back.y, push.z - back.z);
    if (Sq(across) / Sq(back) < 0.08f) {
        const float f = step + step;
        memory.push.x += velocity.z * f;
        memory.push.y += f * 0.0f;
        memory.push.z += -velocity.x * f;
    }
}

// How long since it last came nearer the waypoint (0x00569530).
void UpdateStuckTimer(C_HumanAIMemory &memory, int16_t timeDelta)
{
    const S_vector point = Xyz((*memory.route)[memory.waypoint]->pos);
    const S_vector off(memory.pos.x - point.x, memory.pos.y - point.y, memory.pos.z - point.z);
    const float sq = Sq(off);
    if (sq < memory.bestDistance) {
        memory.bestDistance = sq;
        memory.stuckTimer = 2000;
        return;
    }
    memory.stuckTimer = static_cast<int16_t>(memory.stuckTimer - timeDelta);
    if (memory.stuckTimer < 0)
        memory.stuckTimer = 2000;
}

}  // namespace crowd

bool C_HumanCrowd::StepAlone(C_Human *human, float time)
{
    bool alone;
    const C_HumanController *const controller = human->GetController();
    if (human->IsBodyWorkState5() || controller->GetType() == 1) {
        alone = true;
    } else {
        const S_vector here = human->GetFrame()->GetWorldPos();
        alone = human->m_events->GetNearbyActorCount() == 0;
        for (size_t i = m_cars.size(); i-- != 0;) {
            if (!alone)
                return false;
            const S_vector car = m_cars[i]->GetFrame()->GetWorldPos();
            const float dx = here.x - car.x, dz = here.z - car.z;
            if (std::fabs(here.y - car.y) <= SAME_FLOOR && dx * dx + dz * dz <= NEAR_CAR_SQ)
                alone = false;
        }
        if (!alone)
            return false;
    }

    C_HumanAIMemory &memory = *human->m_aiMemory;
    const int type = controller->GetType();
    C_Human *const owner = memory.owner;
    memory.route = owner->GetController()->GetWaypoints();
    memory.waypoint = 0;
    memory.pos = Xyzw(owner->GetFrame()->GetWorldPos(), owner->GetFrame()->GetMatrix().m[3][3]);
    memory.speed = PointRate(owner);
    if (owner->GetMoveMode() == 0) {
        memory.velocity.x = memory.velocity.y = memory.velocity.z = 0.0f;
    } else {
        memory.velocity = owner->m_velocity;
    }
    if (!RouteHasPoints(memory.route)) {
        memory.velocity.x = memory.velocity.y = memory.velocity.z = 0.0f;
        memory.routeChanged = true;
        memory.target = memory.pos;
        memory.routeStart = memory.pos;
        memory.lastGoal = memory.pos;
    } else {
        AdvanceWaypoint(memory, time);
        memory.target = (*memory.route)[memory.waypoint]->pos;
        memory.velocity.x = memory.target.x - memory.pos.x;
        memory.velocity.y = 0.0f;
        memory.velocity.z = memory.target.z - memory.pos.z;
        if (IsInvalid(memory.routeStart.x) || IsInvalid(memory.lastGoal.x)) {
            memory.routeStart = memory.pos;
            memory.lastGoal = memory.pos;
        }
        if (memory.routeChanged) {
            memory.routeChanged = false;
            memory.lastGoal = memory.routeStart;
        }
        memory.routeStart = (*memory.route)[0]->pos;
    }
    const float sq = Sq(Xyz(memory.velocity));
    if (sq <= 1e-8f) {
        memory.velocity.x = memory.velocity.y = memory.velocity.z = 0.0f;
        memory.step[0] = memory.step[1] = memory.step[2] = 0.0f;
        memory.stepping = false;
    } else {
        const float k = FastInvSqrt(sq) * memory.speed;
        memory.velocity.x *= k;
        memory.velocity.y *= k;
        memory.velocity.z *= k;
        memory.stepping = true;
        memory.step[0] = memory.velocity.x;
        memory.step[1] = memory.velocity.y;
        memory.step[2] = memory.velocity.z;
    }
    if (type == 1 || type == 5)
        memory.nearPoint = nullptr;
    return alone;
}

void C_HumanCrowd::Prepare(C_HumanAIMemory &memory, float time)
{
    const C_CheckpointNetwork *const network = Network();
    C_Human *const owner = memory.owner;
    const int type = owner->GetController()->GetType();
    memory.pos = Xyzw(owner->GetFrame()->GetWorldPos(), owner->GetFrame()->GetMatrix().m[3][3]);
    memory.targetPoint = nullptr;
    memory.start = memory.pos;
    memory.speed = PointRate(owner);
    memory.route = owner->GetController()->GetWaypoints();
    memory.waypoint = 0;
    if (owner->GetMoveMode() == 0) {
        memory.velocity.x = memory.velocity.y = memory.velocity.z = 0.0f;
    } else {
        memory.velocity = owner->m_velocity;
    }
    if (!RouteHasPoints(memory.route)) {
        memory.routeChanged = true;
        memory.plannedFrom = -1;
        memory.target = memory.pos;
        memory.routeStart = memory.pos;
        memory.lastGoal = memory.pos;
        if (type == 1) {
            memory.nearPoint = nullptr;
            memory.target.x = time * memory.velocity.x + memory.pos.x;
            memory.target.y = time * memory.velocity.y + memory.pos.y;
            memory.target.z = time * memory.velocity.z + memory.pos.z;
        } else if (type == 5) {
            memory.nearPoint = nullptr;
            memory.velocity.x = memory.velocity.y = memory.velocity.z = 0.0f;
        } else if (1e-8f < Sq(Xyz(memory.velocity))) {
            if (!memory.nearPoint) {
                memory.nearPoint = network->Get(network->FindNearestPoint(Xyz(memory.pos), 5.0f, -1.0f));
            } else {
                FindPointNear(network, &memory.nearPoint, Xyz(memory.pos), NEAREST_SEARCH_DEPTH, nullptr);
            }
        }
    } else {
        const S_Checkpoint *const previous = memory.nearPoint;
        memory.nearPoint = network->Get((*memory.route)[memory.waypoint]->checkpoint);
        FindPointNear(network, &memory.nearPoint, Xyz(memory.pos), NEAREST_SEARCH_DEPTH, previous);
        if (IsInvalid(memory.routeStart.x) || IsInvalid(memory.lastGoal.x)) {
            memory.routeStart = memory.pos;
            memory.lastGoal = memory.pos;
        }
        if (memory.routeChanged) {
            memory.routeChanged = false;
            memory.stuckTimer = 2000;
            memory.bestDistance = 1e8f;
            memory.lastGoal = memory.routeStart;
            if (memory.plannedFrom < 0 || static_cast<unsigned>(memory.plannedFrom) >= memory.route->size())
                Replan(memory);
        }
        memory.routeStart = (*memory.route)[0]->pos;
        const S_AIWaypoint *const waypoint = (*memory.route)[memory.waypoint];
        memory.target = waypoint->pos;
        memory.targetPoint = network->Get(waypoint->checkpoint);
    }

    memory.currentPoint = nullptr;
    if (owner->GetStance() == 2) {
        // Lying, the box around the body along where it faces.
        S_vector dir = owner->GetFrame()->GetLocalDir();
        dir.y = 0.0f;
        const float sq = dir.x * dir.x + dir.z * dir.z;
        if (std::fabs(sq - 1.0f) >= 1e-8f) {
            if (sq >= 1e-8f) {
                const float k = 1.0f / std::sqrt(sq);
                dir.x *= k;
                dir.z *= k;
            } else if (dir.x != 0.0f) {
                dir.x = dir.x >= 0.0f ? 1.0f : -1.0f;
            } else if (dir.z != 0.0f) {
                dir.z = dir.z >= 0.0f ? 1.0f : -1.0f;
            }
        }
        memory.axisZ = {dir.x, 0.0f, dir.z, 0.0f};
        memory.axisX = {-dir.z, 0.0f, dir.x, 0.0f};
        memory.axisY = {0.0f, 1.0f, 0.0f, 0.0f};
        memory.boxCenter = memory.pos;
        memory.halfSize[0] = 0.7f;
        memory.halfSize[1] = 0.3f;
        memory.halfSize[2] = 1.3f;
    }
    memory.notPlayer = type != 1;
    memory.walking = type != 5 && owner->GetStance() != 2 && memory.notPlayer;
    memory.walking = memory.walking && owner->ControllerUnknown104();
    memory.solved = memory.notPlayer && (memory.walking || RouteHasPoints(memory.route));
    memory.givesWay = (g_gameRole & 7) == 7 || !g_gameProfile.IsCooperativeGame() || type != 1;
    if (!memory.walking) {
        memory.velocity.x = memory.velocity.y = memory.velocity.z = 0.0f;
    }
}

int C_HumanCrowd::FindPointNear(const C_CheckpointNetwork *network, const S_Checkpoint **point, const S_vector &pos, int depth, const S_Checkpoint *hint)
{
    const S_Checkpoint *const start = *point;
    if (!start)
        return 0;
    const auto off = [&pos](const S_Checkpoint *p) { return S_vector(pos.x - p->pos.x, pos.y - p->pos.y, pos.z - p->pos.z); };
    const auto radiusSq = [](const S_Checkpoint *p) {
        const float radius = static_cast<float>(p->radius) * 0.0625f;
        return radius * radius;
    };
    const float startSq = radiusSq(start);
    const S_vector fromStart = off(start);
    const float startOutside = Sq(fromStart) - startSq;
    if (IsNegative(startOutside))
        return 1;
    if (hint && hint != start) {
        const float hintSq = radiusSq(hint);
        const float hintOutside = Sq(off(hint)) - hintSq;
        if (IsNegative(hintOutside)) {
            *point = hint;
            return 1;
        }
        bool linked = false;
        for (int i = (hint->flags & CHECKPOINT_LINK_COUNT) - 1; i >= 0 && !linked; --i)
            linked = network->Get(hint->links[i].target) == start;
        if (linked) {
            const S_vector leg(hint->pos.x - start->pos.x, hint->pos.y - start->pos.y, hint->pos.z - start->pos.z);
            const float legSq = Sq(leg);
            if (1e-8f < legSq) {
                const float t = Dot(leg, fromStart) / legSq;
                if (!IsNegative(t) && t < 1.0f) {
                    const S_vector across(fromStart.x - leg.x * t, fromStart.y - leg.y * t, fromStart.z - leg.z * t);
                    if (IsNegative(Sq(across) - (t * hintSq + (1.0f - t) * startSq))) {
                        *point = hintOutside <= startOutside ? hint : start;
                        return 1;
                    }
                }
            }
        }
    }
    float best = startOutside;
    const S_Checkpoint *bestPoint = start;
    m_scratch.clear();
    m_scratch.push_back(start);
    for (int pass = 0; pass < depth; ++pass) {
        const int count = static_cast<int>(m_scratch.size());
        m_scratch.push_back(nullptr);
        for (int i = count - 1; i >= 0 && m_scratch[i]; --i) {
            const S_Checkpoint *const from = m_scratch[i];
            const float fromSq = radiusSq(from);
            const S_vector fromHere = off(from);
            const float fromOutside = Sq(fromHere) - fromSq;
            for (int link = (from->flags & CHECKPOINT_LINK_COUNT) - 1; link >= 0; --link) {
                const S_Checkpoint *const next = network->Get(from->links[link].target);
                bool known = false;
                for (size_t k = m_scratch.size(); k-- != 0 && !known;)
                    known = m_scratch[k] == next;
                if (known)
                    continue;
                // (Ladders and the like are not gone through.)
                if (next->type < 3 || next->type > 6)
                    m_scratch.push_back(next);
                const float nextSq = radiusSq(next);
                const float nextOutside = Sq(off(next)) - nextSq;
                if (IsNegative(nextOutside)) {
                    *point = next;
                    return 1;
                }
                if (best >= 1e-8f) {
                    if (nextOutside < best) {
                        best = nextOutside;
                        bestPoint = next;
                    }
                    const S_vector leg(next->pos.x - from->pos.x, next->pos.y - from->pos.y, next->pos.z - from->pos.z);
                    const float legSq = Sq(leg);
                    if (1e-8f <= legSq) {
                        const float t = Dot(leg, fromHere) / legSq;
                        if (!IsNegative(t) && t <= 1.0f) {
                            const S_vector across(fromHere.x - leg.x * t, fromHere.y - leg.y * t, fromHere.z - leg.z * t);
                            float outside = Sq(across) - (t * nextSq + (1.0f - t) * fromSq);
                            if (IsNegative(outside))
                                outside = 0.0f;
                            if (outside < best) {
                                best = outside;
                                bestPoint = nextOutside < fromOutside ? next : from;
                            }
                        }
                    }
                }
            }
        }
    }
    *point = bestPoint;
    return 2;
}

bool C_HumanCrowd::IsPointBlocked(const C_HumanAIMemory &memory, const S_vector4 &point)
{
    const C_Human *const self = memory.owner;
    const C_HumanEvents *const events = self->m_events;
    for (int i = events->GetNearbyActorCount() - 1; i >= 0; --i) {
        C_Human *const human = static_cast<C_Human *>(events->GetNearbyActor(i));
        if (!human)
            continue;
        C_HumanAIMemory *const other = human->m_aiMemory;
        if ((other->flags & HUMAN_FLAGS_NOT_BLOCKING) != 0)
            continue;
        if (human->GetController() && human->GetController()->Unknown108())
            continue;
        if (human->IsAISuspended()) {
            other->flags |= HUMAN_FLAG_SUSPENDED;
            continue;
        }
        const S_vector off = Sub4(point, other->pos);
        if (other->owner->GetStance() == 2) {
            if (std::fabs(off.y) <= SAME_FLOOR) {
                const S_Ellipsoid shape = ProneEllipsoid(*other);
                const float x = Dot(off, shape.axis[0]) / shape.half.x;
                const float z = Dot(off, shape.axis[2]) / shape.half.z;
                if (x * x + z * z < 1.0f)
                    return true;
            }
        } else if (std::fabs(off.y) <= SAME_FLOOR && SqFlat(off) < HUMAN_RADIUS_SQ) {
            return true;
        }
    }
    for (int i = static_cast<int>(m_cars.size()) - 1; i >= 0; --i) {
        C_Car *const car = m_cars[i];
        if (self->GetVehicle() == car || !(car->GetSpeed() <= 1e-8f))
            continue;
        const S_vector carPos = car->GetFrame()->GetWorldPos();
        const S_vector off(point.x - carPos.x, point.y - carPos.y, point.z - carPos.z);
        if (std::fabs(off.y) <= SAME_FLOOR && Sq(off) <= NEAR_CAR_SQ) {
            const S_Ellipsoid shape = CarEllipsoid(car, m_carHalves[i]);
            const float x = Dot(off, shape.axis[0]) / shape.half.x;
            const float z = Dot(off, shape.axis[2]) / shape.half.z;
            if (x * x + z * z < 1.0f)
                return true;
        }
    }
    return false;
}

bool C_HumanCrowd::Replan(C_HumanAIMemory &memory)
{
    const C_CheckpointNetwork *const network = Network();
    Waypoints &route = *memory.route;
    // On to the first waypoint not blocked (the last one at worst); those before it go.
    size_t index = memory.waypoint;
    while (index < route.size() && IsPointBlocked(memory, route[index]->pos))
        ++index;
    if (index == route.size())
        index = route.size() - 1;
    for (; index != 0; --index) {
        ReleaseWaypoint(route.front());
        route.erase(route.begin());
    }
    memory.waypoint = 0;
    memory.unknown109 = false;
    m_goalPoint = network->Get(route.front()->checkpoint);
    m_goal = route[memory.waypoint]->pos;

    S_PlanNode &root = m_nodes[0];
    root.pos = memory.pos;
    root.point = memory.nearPoint;
    root.avoidedCount = 0;
    root.avoidedCar = nullptr;
    root.childCount = 0;
    S_PlanNode &goal = m_nodes[1];
    goal.pos = m_goal;
    goal.point = m_goalPoint;
    goal.avoidedCount = 0;
    goal.avoidedCar = nullptr;
    goal.childCount = 0;
    root.children[0] = reinterpret_cast<uintptr_t>(&goal);
    ++root.childCount;
    m_nodeCount = 2;
    ExpandNode(memory, &root, 0);
    if (m_nodeCount == 2)
        return false;

    m_bestCost = 1e8f;
    m_bestDistance = 1e8f;
    m_resultCount = 0;
    S_PlanNode *path[CROWD_MAX_PATH];
    CollectBest(memory, &root, 0.0f, false, path, 0);
    if (m_resultCount == 0)
        return false;
    if (!(Sq(Sub4(m_results[0].pos, route[memory.waypoint]->pos)) > 1e-08f))
        return false;
    if (m_bestDistance < 0.010000001f) {
        ReleaseWaypoint(route.front());
        route.erase(route.begin());
    }
    memory.plannedFrom = static_cast<int>(route.size());
    for (int i = m_resultCount - 1; i >= 0; --i) {
        const S_PlanNode &node = m_results[i];
        if (!node.point)
            break;
        const int pointIndex = network->GetIndex(node.point);
        S_AIWaypoint *const waypoint = AllocWaypoint(pointIndex < network->GetCount() && pointIndex >= 0 ? pointIndex : 0xFFFF, Xyzw(node.point->pos),
                                                     node.point->flags & CHECKPOINT_SEARCH_FLAGS, node.point->type, RadiusOf(*node.point));
        waypoint->pos = node.pos;
        route.insert(route.begin(), waypoint);
    }
    return true;
}

void C_HumanCrowd::CollectBest(C_HumanAIMemory &memory, const S_PlanNode *node, float cost, bool notRoot, S_PlanNode **path, int depth)
{
    if (depth == CROWD_MAX_PATH)
        return;
    bool leaf = true;
    for (int i = 0; i < node->childCount; ++i) {
        const uintptr_t value = node->children[i];
        S_PlanNode *const child = reinterpret_cast<S_PlanNode *>(value);
        if ((value & 1) == 0 && child->point) {
            path[depth] = child;
            CollectBest(memory, child, Sq(Sub4(child->pos, node->pos)) + cost, true, path, depth + 1);
            leaf = false;
        }
    }
    if (!leaf || !notRoot)
        return;
    const float distance = Sq(Sub4(node->pos, (*memory.route)[memory.waypoint]->pos));
    const float difference = distance - m_bestDistance;
    if (IsNegative(difference) || (difference < 0.0001f && cost < m_bestCost)) {
        for (int i = 0; i < depth; ++i)
            m_results[i] = *path[i];
        m_bestDistance = distance;
        m_bestCost = cost;
        m_resultCount = depth;
    }
}

// The way past a blocker on one side: new nodes along the corners, the old way going on from the last (the two like blocks of 0x0056a8e0).
void C_HumanCrowd::AddSideWay(S_PlanNode *node, S_PlanNode *child, int blockKind, const S_vector4 *corners, int cornerCount, C_Human *human, C_Car *car)
{
    if (cornerCount == 0 || node->childCount >= CROWD_MAX_CHILDREN)
        return;
    // Known already?
    for (int i = node->childCount - 1; i >= 0; --i) {
        const S_PlanNode *const known = reinterpret_cast<const S_PlanNode *>(node->children[i] & ~static_cast<uintptr_t>(1));
        if (Sq(Sub4(corners[0], known->pos)) < 1e-16f)
            return;
    }
    S_PlanNode *added = nullptr;
    S_PlanNode *parent = nullptr;
    for (int i = 0; i < cornerCount; ++i) {
        if (m_nodeCount >= CROWD_MAX_NODES)
            return;
        added = &m_nodes[m_nodeCount++];
        added->pos = corners[i];
        added->point = nullptr;
        added->avoidedCount = 0;
        if (human)
            added->avoided[added->avoidedCount++] = human;
        added->avoidedCar = car;
        added->childCount = 0;
        S_PlanNode *const owner = parent ? parent : node;
        owner->children[owner->childCount++] = reinterpret_cast<uintptr_t>(added);
        parent = added;
    }
    if (blockKind == 1) {
        added->children[added->childCount++] = reinterpret_cast<uintptr_t>(child);
        if (human && child->avoidedCount < CROWD_MAX_AVOIDED && child->avoided[child->avoidedCount] != human)
            child->avoided[child->avoidedCount++] = human;
        if (car)
            child->avoidedCar = car;
    } else if (blockKind == 2) {
        added->children[0] = child->children[0];
        added->children[1] = child->children[1];
        added->childCount = child->childCount;
    }
}

void C_HumanCrowd::ExpandNode(C_HumanAIMemory &memory, S_PlanNode *node, int depth)
{
    if (depth == CROWD_MAX_PATH)
        return;
    const float dx = node->pos.x - m_goal.x, dz = node->pos.z - m_goal.z;
    if (dx * dx + dz * dz < 1e-8f) {
        node->childCount = 0;
        node->point = m_goalPoint;
        return;
    }
    for (int i = 0; i < node->childCount; ++i) {
        const uintptr_t value = node->children[i];
        if (value & 1)
            continue;
        S_PlanNode *const child = reinterpret_cast<S_PlanNode *>(value);
        static S_vector4 s_left[4], s_right[4];         // 0x008aaea0, 0x008aae60
        int leftCount = 0, rightCount = 0;
        C_Human *human = nullptr;
        C_Car *car = nullptr;
        const int kind = FindBlocker(memory, node, child, s_left, &leftCount, s_right, &rightCount, &human, &car);
        if (kind == 0) {
            node->children[i] = value | 1;
        } else if (human || car) {
            const int nodesBefore = m_nodeCount;
            AddSideWay(node, child, kind, s_left, leftCount, human, car);
            if (m_nodeCount >= CROWD_MAX_NODES && nodesBefore < CROWD_MAX_NODES)
                return;
            AddSideWay(node, child, kind, s_right, rightCount, human, car);
            if (m_nodeCount >= CROWD_MAX_NODES)
                return;
            node->children[i] = value | 1;
        } else {
            ExpandNode(memory, child, depth + 1);
            if (!child->point) {
                child->point = node->point;
                FindPointNear(Network(), &child->point, Xyz(child->pos), NEAREST_SEARCH_DEPTH_REPLAN, nullptr);
                if (!child->point)
                    node->children[i] = reinterpret_cast<uintptr_t>(child) | 1;
            }
        }
    }
}

int C_HumanCrowd::FindBlocker(C_HumanAIMemory &memory, const S_PlanNode *from, S_PlanNode *to, S_vector4 *left, int *leftCount, S_vector4 *right, int *rightCount, C_Human **outHuman, C_Car **outCar)
{
    const C_CheckpointNetwork *const network = Network();
    const S_Checkpoint *point = to->point;
    if (!point) {
        point = from->point;
        if (!point)
            return 0;
        FindPointNear(network, &point, Xyz(to->pos), NEAREST_SEARCH_DEPTH_REPLAN, nullptr);
        if (!point)
            return 0;
    }
    const S_vector a = Xyz(from->pos);
    S_vector4 kept = to->pos;
    if (!KeepOnNetwork(network, kept, point, &point))
        return 0;
    to->point = point;
    const S_vector dir(kept.x - a.x, 0.0f, kept.z - a.z);
    const float lengthSq = dir.z * dir.z + dir.x * dir.x;
    if (lengthSq < 1e-8f)
        return 0;

    int result = 1;
    float t1 = 1e8f, t2 = 1e8f, nearest = 1e8f;
    C_Human *blockerHuman = nullptr;
    C_Car *blockerCar = nullptr;
    const auto setResult = [&]() { result = (1.0f < t1 || 1.0f < t2) ? 2 : 1; };

    const C_HumanEvents *const events = memory.owner->m_events;
    for (int i = events->GetNearbyActorCount() - 1; i >= 0 && !(nearest < 1e-8f); --i) {
        C_Human *const human = static_cast<C_Human *>(events->GetNearbyActor(i));
        if (!human)
            continue;
        C_HumanAIMemory *const other = human->m_aiMemory;
        if ((other->flags & HUMAN_FLAGS_NOT_BLOCKING) != 0)
            continue;
        if (human->IsAISuspended()) {
            other->flags |= HUMAN_FLAG_SUSPENDED;
            continue;
        }
        bool avoided = false;
        for (int k = to->avoidedCount - 1; k >= 0 && !avoided; --k)
            avoided = to->avoided[k] == other->owner;
        if (avoided)
            continue;
        const S_vector otherPos = Xyz(other->pos);
        if (other->owner->GetStance() != 2) {
            const float rx = a.x - otherPos.x, rz = a.z - otherPos.z;
            if (!(std::fabs(a.y - otherPos.y) <= SAME_FLOOR))
                continue;
            const float half = rz * dir.z + rx * dir.x;
            const float doubled = half + half;
            if (!IsNegative(doubled))
                continue;
            const float k = half / lengthSq;
            const S_vector off(rx - k * dir.x, 0.0f, rz - dir.z * k);
            const float offSq = off.x * off.x + off.z * off.z;
            if (!(offSq <= HUMAN_RADIUS_SQ))
                continue;
            const float c = (rx * rx + rz * rz) - HUMAN_RADIUS_SQ;
            if (IsNegative(c)) {
                blockerHuman = other->owner;
                nearest = 0.0f;
            }
            const float discriminant = doubled * doubled - c * lengthSq * 4.0f;
            if (IsNegative(discriminant))
                continue;
            t1 = (std::sqrt(discriminant) - doubled) / (lengthSq + lengthSq);
            t2 = (-doubled - std::sqrt(discriminant)) / (lengthSq + lengthSq);
            if (!IsNegative(t1) && t1 < 1.0f && t1 < nearest) {
                blockerHuman = other->owner;
                nearest = t1;
            }
            if (!IsNegative(t2) && t2 < 1.0f && t2 < nearest) {
                blockerHuman = other->owner;
                nearest = t2;
            }
            if (blockerHuman == other->owner) {
                setResult();
                S_vector side;
                if (offSq <= 1e-8f) {
                    const float inv = FastInvSqrt(lengthSq) * HUMAN_RADIUS;
                    side = S_vector(dir.z * inv, inv * 0.0f, inv * -dir.x);
                } else {
                    side = off * (FastInvSqrt(offSq) * HUMAN_RADIUS);
                }
                left[0] = Xyzw(S_vector(side.x + otherPos.x, side.y + otherPos.y, side.z + otherPos.z));
                right[0] = Xyzw(S_vector(otherPos.x - side.x, otherPos.y - side.y, otherPos.z - side.z));
                *leftCount = 1;
                *rightCount = 1;
            }
            continue;
        }

        // Lying: its ellipse, four corners around it.
        const S_vector rel(a.x - otherPos.x, a.y - otherPos.y, a.z - otherPos.z);
        if (2.0f < std::fabs(rel.y))
            continue;
        const S_Ellipsoid shape = ProneEllipsoid(*other);
        const S_vector l = shape.Local(Flat(rel));
        const S_vector r = shape.Local(dir);
        float a2 = Sq(r);
        const float half = Dot(r, l);
        const float doubled = half + half;
        if (!IsNegative(doubled))
            continue;
        float offSq = 0.0f;
        S_vector off;
        const float c = Sq(l) - 1.0f;
        if (!IsNegative(c)) {
            const float k = half / a2;
            off = S_vector(l.x - r.x * k, l.y - r.y * k, l.z - r.z * k);
            offSq = Sq(off);
            const float discriminant = doubled * doubled - c * a2 * 4.0f;
            if (1.0f < offSq || IsNegative(discriminant))
                continue;
            const float twice = a2 + a2;
            t1 = (std::sqrt(discriminant) - doubled) * (1.0f / twice);
            t2 = (-doubled - std::sqrt(discriminant)) * (1.0f / twice);
            if (!IsNegative(t1) && t1 < 1.0f && t1 < nearest) {
                blockerHuman = other->owner;
                nearest = t1;
            }
            if (!IsNegative(t2) && t2 < 1.0f && t2 < nearest) {
                blockerHuman = other->owner;
                nearest = t2;
            }
        } else {
            blockerHuman = other->owner;
            nearest = 0.0f;
        }
        if (blockerHuman != other->owner)
            continue;
        setResult();
        S_vector side;
        if (offSq <= 1e-8f) {
            const float inv = FastInvSqrt(a2);
            side = S_vector(r.z * inv, inv * 0.0f, inv * -r.x);
        } else {
            side = off * FastInvSqrt(offSq);
        }
        S_vector corners[4];
        int negative[4], positive[4], negativeCount = 0, positiveCount = 0;
        SortCorners(l, side, r, corners, negative, positive, &negativeCount, &positiveCount);
        const auto worldCorner = [&](int index) {
            const S_vector v(l.x + corners[index].x, l.y + corners[index].y, l.z + corners[index].z);
            const S_vector world = shape.World(v);
            return Xyzw(S_vector(world.x + otherPos.x, world.y + otherPos.y, world.z + otherPos.z));
        };
        for (int k = 0; k < positiveCount; ++k)
            left[k] = worldCorner(positive[k]);
        *leftCount = positiveCount;
        for (int k = 0; k < negativeCount; ++k)
            right[k] = worldCorner(negative[k]);
        *rightCount = negativeCount;
    }

    for (int i = static_cast<int>(m_cars.size()) - 1; i >= 0; --i) {
        if (nearest < 1e-8f)
            break;
        C_Car *const car = m_cars[i];
        if (memory.owner->GetVehicle() == car || !(car->GetSpeed() <= 1e-8f) || to->avoidedCar == car)
            continue;
        const S_vector carPos = car->GetFrame()->GetWorldPos();
        const S_vector fromCar(a.x - carPos.x, a.y - carPos.y, a.z - carPos.z);
        const S_vector toCar(kept.x - carPos.x, kept.y - carPos.y, kept.z - carPos.z);
        if (!((std::fabs(fromCar.y) <= SAME_FLOOR || std::fabs(toCar.y) <= SAME_FLOOR) && (Sq(fromCar) <= NEAR_CAR_SQ || Sq(toCar) <= NEAR_CAR_SQ)))
            continue;
        const S_Ellipsoid shape = CarEllipsoid(car, m_carHalves[i]);
        const S_vector l = shape.Local(Flat(fromCar));
        const S_vector r = shape.Local(dir);
        const float a2 = Sq(r);
        const float half = Dot(r, l);
        const float doubled = half + half;
        if (!IsNegative(doubled))
            continue;
        float offSq = 0.0f;
        S_vector off;
        const float c = Sq(l) - 1.0f;
        if (!IsNegative(c)) {
            const float k = half / a2;
            off = S_vector(l.x - r.x * k, l.y - r.y * k, l.z - r.z * k);
            offSq = Sq(off);
            const float discriminant = doubled * doubled - c * a2 * 4.0f;
            if (1.0f < offSq || IsNegative(discriminant))
                continue;
            const float twice = a2 + a2;
            t1 = (std::sqrt(discriminant) - doubled) * (1.0f / twice);
            t2 = (-doubled - std::sqrt(discriminant)) * (1.0f / twice);
            if (!IsNegative(t1) && t1 < 1.0f && t1 < nearest) {
                blockerHuman = nullptr;
                blockerCar = car;
                nearest = t1;
            }
            if (!IsNegative(t2) && t2 < 1.0f && t2 < nearest) {
                blockerHuman = nullptr;
                blockerCar = car;
                nearest = t2;
            } else if (blockerCar != car) {
                continue;
            }
        } else {
            nearest = 0.0f;
            blockerHuman = nullptr;
            blockerCar = car;
        }
        setResult();
        S_vector side;
        if (offSq <= 1e-8f) {
            const float inv = FastInvSqrt(a2);
            side = S_vector(r.z * inv, inv * 0.0f, inv * -r.x);
        } else {
            side = off * FastInvSqrt(offSq);
        }
        S_vector corners[4];
        int negative[4], positive[4], negativeCount = 0, positiveCount = 0;
        SortCorners(l, side, r, corners, negative, positive, &negativeCount, &positiveCount);
        const auto worldCorner = [&](int index) {
            const S_vector v(l.x + corners[index].x, l.y + corners[index].y, l.z + corners[index].z);
            const S_vector world = shape.World(S_vector(v.x * 1.05f, v.y * 1.05f, v.z * 1.05f));
            return Xyzw(S_vector(world.x + carPos.x, world.y + carPos.y, world.z + carPos.z));
        };
        for (int k = 0; k < positiveCount; ++k)
            left[k] = worldCorner(positive[k]);
        *leftCount = positiveCount;
        for (int k = 0; k < negativeCount; ++k)
            right[k] = worldCorner(negative[k]);
        *rightCount = negativeCount;
    }
    *outHuman = blockerHuman;
    *outCar = blockerCar;
    return result;
}

void C_HumanCrowd::PushFromCar(C_HumanAIMemory &memory, int index, float step)
{
    if (!memory.solved)
        return;
    C_Car *const car = m_cars[index];
    const S_vector carPos = car->GetFrame()->GetWorldPos();
    const S_Ellipsoid shape = CarEllipsoid(car, m_carHalves[index]);
    const S_vector l = shape.Local(Flat(S_vector(memory.pos.x - carPos.x, memory.pos.y - carPos.y, memory.pos.z - carPos.z)));
    const float c = Sq(l) - 1.0f;
    if (c <= 1e-8f) {
        if (1e-8f < c + 1.0f) {
            const float length = std::sqrt(c + 1.0f);
            const float k = (1.0f - length) / length;
            AddPush(memory.push, shape.World(S_vector(l.x * k, l.y * k, l.z * k)));
        }
        return;
    }
    const S_vector carVelocity = car->GetVelocity();
    const S_vector relative(memory.velocity.x - carVelocity.x, 0.0f, memory.velocity.z - carVelocity.z);
    S_vector aside;
    if (SideStepEllipsoid(l, shape.Local(relative), c, step, &aside))
        AddPush(memory.push, shape.World(aside));
}

void C_HumanCrowd::PushTargetFromCar(C_HumanAIMemory &memory, int index)
{
    if (!memory.solved || (memory.route && memory.route->empty()))
        return;
    C_Car *const car = m_cars[index];
    const S_vector carPos = car->GetFrame()->GetWorldPos();
    const S_Ellipsoid shape = CarEllipsoid(car, m_carHalves[index]);
    const S_vector l = shape.Local(Flat(S_vector(memory.target.x - carPos.x, memory.target.y - carPos.y, memory.target.z - carPos.z)));
    const float sq = Sq(l);
    if (!(sq - 1.0f < 1e-06f))
        return;
    S_vector unit;
    if (sq <= 1e-8f) {
        const S_vector apart(memory.pos.x - carPos.x, memory.pos.y - carPos.y, memory.pos.z - carPos.z);
        unit = NormalizedZero(S_vector(Dot(apart, shape.axis[0]), Dot(apart, shape.axis[1]), Dot(apart, shape.axis[2])));
    } else {
        const float length = std::sqrt(sq);
        const float k = (1.0f - length) / length;
        unit = S_vector(l.x * k, l.y * k, l.z * k);
    }
    const S_vector by = shape.World(unit);
    MoveTarget(memory, S_vector(by.x * 1.1f, by.y * 1.1f, by.z * 1.1f));
}

void C_HumanCrowd::Tick(int timeDelta, const std::vector<C_Actor *> &active)
{
    if ((g_gameRole & 7) != 7 && !g_gameProfile.IsCooperativeGame())
        return;
    m_humans.clear();
    m_cars.clear();
    m_carHalves.clear();
    for (C_Actor *const actor : active) {
        if (actor->GetType() == ACTOR_HUMAN) {
            C_Human *const human = static_cast<C_Human *>(actor);
            uint8_t flags = static_cast<uint8_t>(((human->HasOrder() ? 1 : 0) << 2 | (human->IsUnconscious() ? 1 : 0)) * 2 | (human->m_stats[0] < 1 ? 1 : 0));
            if (human->GetVehicle())
                flags |= 0x10;
            if (human->GetBodyClimbState() != 0)
                flags |= 0x20;
            human->m_aiMemory->flags = flags;
            if ((flags & HUMAN_FLAGS_OUT) == 0)
                m_humans.push_back(human);
            else
                human->m_aiMemory->stepping = false;
        } else if (actor->GetType() == ACTOR_CAR) {
            C_Car *const car = static_cast<C_Car *>(actor);
            m_cars.push_back(car);
            m_carHalves.push_back(CarHalfSize(car));
        }
    }
    if (m_humans.empty())
        return;

    const float time = static_cast<float>(timeDelta) * 0.001f;
    const float step = time * 0.125f;
    const int humanCount = static_cast<int>(m_humans.size());
    for (int i = humanCount - 1; i >= 0; --i)
        if (StepAlone(m_humans[i], time))
            m_humans[i] = nullptr;
    for (int i = humanCount - 1; i >= 0; --i)
        if (m_humans[i])
            Prepare(*m_humans[i]->m_aiMemory, time);

    for (int iteration = SOLVER_STEPS; iteration != 0; --iteration) {
        for (int i = humanCount - 1; i >= 0; --i) {
            if (!m_humans[i])
                continue;
            C_HumanAIMemory &memory = *m_humans[i]->m_aiMemory;
            MoveOneStep(memory, step);
            memory.push.x = memory.push.y = memory.push.z = 0.0f;
        }
        for (int i = humanCount - 1; i >= 0; --i) {
            C_Human *const human = m_humans[i];
            if (!human)
                continue;
            C_HumanAIMemory &memory = *human->m_aiMemory;
            for (int n = human->m_events->GetNearbyActorCount() - 1; n >= 0; --n) {
                C_Human *const other = static_cast<C_Human *>(human->m_events->GetNearbyActor(n));
                if (!other || (other->m_aiMemory->flags & HUMAN_FLAGS_OUT) != 0)
                    continue;
                if (other->IsAISuspended()) {
                    other->m_aiMemory->flags |= HUMAN_FLAG_SUSPENDED;
                } else {
                    PushFromHuman(memory, *other->m_aiMemory);
                    PushTargetFromHuman(memory, *other->m_aiMemory);
                }
            }
            for (int c = static_cast<int>(m_cars.size()) - 1; c >= 0; --c) {
                // Kept: the rider is compared with the car at the HUMAN's index, not this one, so a rider does get pushed from its own car.
                // (Fix: not beyond the cars; the original reads past them.)
                if (human->GetVehicle() && i < static_cast<int>(m_cars.size()) && human->GetVehicle() == m_cars[i])
                    continue;
                const S_vector carPos = m_cars[c]->GetFrame()->GetWorldPos();
                const S_vector here = human->GetFrame()->GetWorldPos();
                const float dx = here.x - carPos.x, dz = here.z - carPos.z;
                if (std::fabs(here.y - carPos.y) <= SAME_FLOOR && dz * dz + dx * dx <= NEAR_CAR_SQ) {
                    PushFromCar(memory, c, step);
                    PushTargetFromCar(memory, c);
                }
            }
            PushOntoNetwork(memory);
        }
        for (int i = humanCount - 1; i >= 0; --i) {
            if (!m_humans[i])
                continue;
            C_HumanAIMemory &memory = *m_humans[i]->m_aiMemory;
            TurnPushAside(memory, step);
            memory.pos.x += memory.push.x;
            memory.pos.y += memory.push.y;
            memory.pos.z += memory.push.z;
            HoldOnNetwork(memory);
        }
    }

    for (int i = humanCount - 1; i >= 0; --i) {
        if (!m_humans[i])
            continue;
        C_HumanAIMemory &memory = *m_humans[i]->m_aiMemory;
        if (RouteHasPoints(memory.route)) {
            S_AIWaypoint *const waypoint = (*memory.route)[memory.waypoint];
            if (1e-08f < Sq(Sub4(memory.target, waypoint->pos)))
                waypoint->pos = memory.target;
        }
        const S_vector moved = Sub4(memory.pos, memory.start);
        if (Sq(moved) <= 1e-08f) {
            memory.step[0] = memory.step[1] = memory.step[2] = 0.0f;
            memory.stepping = false;
        } else {
            const float k = 1.0f / time;
            memory.stepping = true;
            memory.step[0] = moved.x * k;
            memory.step[1] = moved.y * k;
            memory.step[2] = moved.z * k;
        }
        if (RouteHasPoints(memory.route))
            UpdateStuckTimer(memory, static_cast<int16_t>(timeDelta));
    }
}

void HumanPass_Tick(int timeDelta, const std::vector<C_Actor *> &active)
{
    g_humanCrowd.Tick(timeDelta, active);
}
