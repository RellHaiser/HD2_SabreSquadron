// C_AIController's movement (original source 0x00578770..0x00581f20): driving the car along
// the driver's road path, and walking the move order: planning the route over the walking
// network, cutting its corners, the doors, ladders and climbs on the way, stepping round the
// others and off the points cars stand on, and the walk itself (where the human faces, the
// walking axes, the pace).
#include "game/human_controller.h"

#include <cmath>
#include <limits>
#include <vector>

#include "game/actor.h"
#include "game/ai_parts.h"
#include "game/checkpoints.h"
#include "game/collision.h"
#include "game/controller_internal.h"
#include "game/controller_messages.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/ladder.h"
#include "game/pending.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "script/script.h"

namespace {

// Walking point types the AI handles (S_Checkpoint::type, partly inferred: a pair of the
// same kind is a way over the thing; the ladder's and the door's actors are the points').
enum : uint8_t {
    CHECKPOINT_TYPE_LADDER = 3,
    CHECKPOINT_TYPE_4 = 4,                              // a pair: climbed over
    CHECKPOINT_TYPE_5 = 5,                              // climbed onto from 6, or from below
    CHECKPOINT_TYPE_6 = 6,
};
// A walking point the AI does not hurry over (S_Checkpoint::flags; inferred).
constexpr uint16_t CHECKPOINT_FLAG_SLOW = 0x0800;
// A waypoint of these flags stays out of the height fixing of type 15 runs.
constexpr uint16_t WAYPOINT_FLAGS_KEEP_HEIGHT = 0x0100;
// The control axis 12 (not identified; the walking clears it).
constexpr int AXIS_12 = 12;

S_vector4 WorldPos4(const I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
}

void ReleaseWaypoints(std::vector<S_AIWaypoint *> &waypoints)
{
    for (size_t i = waypoints.size(); i-- > 0;)
        ReleaseWaypoint(waypoints[i]);
    waypoints.clear();
}

// |b - a| < radius (0x0057feb0).
bool IsWithin(const S_vector &a, float radius, const S_vector &b)
{
    return (b - a).Square() < radius * radius;
}

// The same x and z (0x0057ca50).
bool SamePosXZ(const S_vector4 &a, const S_vector4 &b)
{
    return a.x == b.x && a.z == b.z;
}

// The cosine of `degrees` (0x0057e020).
float CosDeg(float degrees)
{
    return std::cos(degrees * 0.017453292f);
}

// The waypoint's position moves toward `target`: onto it when it is within the waypoint's
// radius, else to the radius (a type 15 waypoint measures in the xz plane and takes the
// target's height). 0x0057a8e0.
void MoveWaypointToward(S_AIWaypoint &waypoint, const S_vector4 &target)
{
    S_vector offset = XYZ(target) - XYZ(waypoint.pos);
    if (waypoint.type == CHECKPOINT_TYPE_15)
        offset.y = 0.0f;
    if (offset.Magnitude() <= waypoint.radius) {
        waypoint.pos = target;
        return;
    }
    offset.Normalize();
    waypoint.pos.x += offset.x * waypoint.radius;
    waypoint.pos.y += offset.y * waypoint.radius;
    waypoint.pos.z += offset.z * waypoint.radius;
    if (waypoint.type == CHECKPOINT_TYPE_15)
        waypoint.pos.y = target.y;
}

// Moves the waypoint within its radius (less `margin`) toward the line from `prev` to
// `next`, cutting the corner; not for small points and doors (0x0057bdc0 / 0x0057c2b0).
void CutCorner(S_AIWaypoint &waypoint, const S_vector &prev, const S_vector &next, float margin)
{
    if (!(waypoint.radius > 0.3f) || waypoint.type == CHECKPOINT_TYPE_DOOR)
        return;
    const S_vector point = XYZ(waypoint.pos);
    const S_vector line = next - prev;
    const float t = (point - prev).Dot(line) / (prev - next).Square();
    const S_vector foot = prev + line * t;
    const S_vector offset = foot - point;
    S_vector moved;
    if (offset.Magnitude() <= waypoint.radius)
        moved = foot;
    else
        moved = point + offset.Normalized() * (waypoint.radius - margin);
    // (The original writes a stale w with it; the port keeps the waypoint's.)
    waypoint.pos.x = moved.x;
    waypoint.pos.y = moved.y;
    waypoint.pos.z = moved.z;
}

}  // namespace

float SegmentDistance2(const S_vector &p, const S_vector &a, const S_vector &b, float *t)   // 0x00581210 (DS 0x00586b50)
{
    const S_vector ab = b - a;
    *t = 0.0f;
    const float length2 = ab.Square();
    if (length2 < 1e-8f)
        return (p - a).Square();
    const float s = (p - a).Dot(ab) / length2;
    if (s <= 0.0f) {
        *t = 0.0f;
        return (p - a).Square();
    }
    if (s >= 1.0f) {
        *t = 1.0f;
        return (p - b).Square();
    }
    *t = s;
    return ((p - a) - ab * s).Square();
}

namespace {

// A human within 0.9 m of `pos` (0x00581aa0).
bool IsNear(const C_Human *human, const S_vector4 &pos)
{
    return (human->GetFrame()->GetWorldPos() - XYZ(pos)).Square() < 0.81f;
}

// The first of `blockers` along from..to within 0.8 m of the way (0x005814d0).
C_Human *FindBlocker(const std::vector<C_Human *> &blockers, const S_vector4 &from, const S_vector4 &to)
{
    C_Human *nearest = nullptr;
    float nearestT = 10.0f;
    for (size_t i = blockers.size(); i-- > 0;) {
        float t;
        const float distance2 = SegmentDistance2(blockers[i]->GetFrame()->GetWorldPos(), XYZ(from), XYZ(to), &t);
        if (t >= 0.0f && distance2 < 0.64f && t < nearestT) {
            nearest = blockers[i];
            nearestT = t;
        }
    }
    return nearest;
}

// Ground under `pos` (a ray from 0.3 m over it, 10 m down). 0x00580790.
bool HasGroundBelow(const S_vector4 &pos)
{
    const S_vector from(pos.x, pos.y + 0.3f, pos.z);
    const S_vector dir(0.0f, -10.0f, 0.0f);
    S_vector hitPos;
    S_vector hitNormal;
    return CollisionRayTest(from, dir, &hitPos, &hitNormal, 0xFFFFFFFF, 0) != nullptr;
}

// The two points beside `blocker` (0.9 m from it, across the way from..to) to step round it
// by, the farther from the way first; those with ground under them. 0x00581580.
void GetDetourPoints(std::vector<S_vector4> &points, const C_Human *blocker, const S_vector4 &from,
                     const S_vector4 &to)
{
    points.clear();
    const S_vector center = blocker->GetFrame()->GetWorldPos();
    float t;
    SegmentDistance2(center, XYZ(from), XYZ(to), &t);
    const S_vector foot = XYZ(from) + (XYZ(to) - XYZ(from)) * t;
    const S_vector rel = foot - center;
    const S_vector back = XYZ(from) - foot;
    const S_vector side(back.z, 0.0f, -back.x);
    // The circle round the blocker must reach back along the way ...
    const float c = rel.Square() - 0.81f;
    float a = back.Square();
    float b = back.Dot(rel);
    float disc = b * b - c * a;
    if (!(disc >= 0.0f) || !((std::sqrt(disc) + b) / a >= 0.0f))
        return;
    // ... and the points are where it crosses the line across the way.
    a = side.Square();
    b = side.Dot(rel);
    disc = b * b - c * a;
    if (!(disc >= 0.0f))
        return;
    const float root = std::sqrt(disc);
    const float s1 = (root - b) / a;
    if (s1 < 0.0f)
        return;
    const float s2 = (b + root) / -a;
    const float first = std::fabs(s1) <= std::fabs(s2) ? s2 : s1;
    const float second = std::fabs(s1) <= std::fabs(s2) ? s1 : s2;
    const S_vector4 firstPoint = ToVector4(foot + side * first, 0.0f);
    const S_vector4 secondPoint = ToVector4(foot + side * second, 0.0f);
    if (HasGroundBelow(firstPoint))
        points.push_back(firstPoint);
    if (HasGroundBelow(secondPoint))
        points.push_back(secondPoint);
}

// Keeps the humans in the way: alive, on foot, controlled, ahead of `self` (toward the next
// waypoint, the one after it when `skipFirst`), and not walking on away beyond 2 m. The
// count kept. 0x00580e30.
size_t FilterBlockers(std::vector<C_Human *> &others, const C_Human *self, const std::vector<S_AIWaypoint *> &route,
                      bool skipFirst)
{
    if (others.empty() || route.empty())
        return 0;
    const S_vector4 &target = skipFirst && route.size() >= 2 ? route[1]->pos : route[0]->pos;
    const S_vector &selfPos = self->GetFrame()->GetWorldPos();
    for (size_t i = others.size(); i-- > 0;) {
        const C_Human *other = others[i];
        bool remove = other->IsAISuspended() || other->GetVehicle() || !other->GetController() || other->IsDead();
        if (!remove) {
            const S_vector offset = other->GetFrame()->GetWorldPos() - selfPos;
            if (offset.Dot(XYZ(target) - selfPos) >= 0.0f) {
                if (offset.Square() > 4.0f) {
                    const S_vector &walk = XYZ(other->GetController()->GetLookAt());
                    remove = walk.Square() >= 1e-16f && offset.Dot(walk) > 0.0f;
                }
            } else {
                remove = true;                          // behind
            }
        }
        if (remove) {
            others[i] = others.back();
            others.pop_back();
        }
    }
    return others.size();
}

}  // namespace

// ------------------------------------------------------------------------- driving

int C_AIController::UpdateDriving(int timeDelta)
{
    int result = 0;
    if (!m_driver)
        return 0;
    C_Car *car = static_cast<C_Car *>(m_driver->GetVehicle());
    if (!car || !m_human)
        return 0;
    C_Actor *vehicle = m_human->m_vehicle;
    if (!vehicle)
        return 0;
    C_Vehicle *controls = car->GetVehicle();

    if (m_driver->HasFlags(C_AIDriver::FLAG_STOP)) {
        m_driver->ClearFlags(C_AIDriver::FLAG_NEW_TARGET);
        m_driver->ClearFlags(C_AIDriver::FLAG_DRIVING);
        m_driver->ClearFlags(C_AIDriver::FLAG_STOP);
        m_driver->ResetPath();
        controls->SetUnknown4f2630(car->GetUnknown780());
        controls->SetHandbrake(true, true);
        result = OnDriveArrived(car);
    }

    if (m_driver->HasFlags(C_AIDriver::FLAG_NEW_TARGET)) {
        m_driver->ClearFlags(C_AIDriver::FLAG_NEW_TARGET);
        // Only the driver's seat drives.
        const C_Actor *seat = m_human->m_vehicle;
        if (!seat || seat->GetType() != ACTOR_CAR || m_human->m_seat != 0) {
            m_driver->ClearFlags(C_AIDriver::FLAG_DRIVING);
        } else {
            m_driver->SetFlags(C_AIDriver::FLAG_DRIVING);
            m_driver->ResetPath();
            m_driver->PlanPath(WorldPos4(vehicle->GetFrame()), m_driver->m_target, m_driver->m_targetDir);
            controls->SetUnknown4f2630(car->GetUnknown780());
            car->SetInput(0.0f, 0.0f);
            controls->SetBrake(0.0f);
            controls->SetHandbrake(false, false);
            m_driver->m_nearestDist2 = 1e8f;
            m_driver->m_noProgressTime = 0;
            m_driver->m_moving = false;
            m_driver->m_lastPos = WorldPos4(car->GetFrame());
        }
    }

    if (!m_driver->HasFlags(C_AIDriver::FLAG_DRIVING))
        return result;
    const std::vector<S_AIPathPoint> &path = m_driver->m_path;
    const int count = static_cast<int>(path.size());
    // Fix: a path of one point (the car stands at the destination) is read past its end by
    // the original; the port has arrived.
    if (count < 2)
        return OnDriveArrived(car);

    const float seconds = static_cast<float>(static_cast<uint32_t>(timeDelta)) * 0.001f;
    uint32_t index = m_driver->m_pathIndex;
    // Where the car will be in four times the frame's time.
    const S_vector4 vehiclePos = WorldPos4(vehicle->GetFrame());
    const S_vector ahead = car->GetVelocity() * (seconds * 4.0f) + XYZ(vehiclePos);
    const S_vector carDir = car->GetDirection();

    // How fast it moves over the ground (squared, m/s).
    const S_vector &carPos = car->GetFrame()->GetWorldPos();
    const float dx = carPos.x - m_driver->m_lastPos.x;
    const float dz = carPos.z - m_driver->m_lastPos.z;
    m_driver->m_lastPos = WorldPos4(car->GetFrame());
    const float speed2 = (1.0f / (seconds * seconds)) * (dx * dx + dz * dz);
    if (!m_driver->m_moving) {
        if (speed2 > 0.0025f)
            m_driver->m_moving = true;
    } else if (speed2 >= 0.0025f) {
        // Getting nearer the segment's end; 5 s without, and it goes for the next segment.
        const float distance2 = (XYZ(path[m_driver->m_pathIndex + 1].pos) - XYZ(vehiclePos)).Square();
        if (distance2 <= m_driver->m_nearestDist2) {
            m_driver->m_nearestDist2 = distance2;
            m_driver->m_noProgressTime = 0;
        } else {
            m_driver->m_noProgressTime += timeDelta;
            if (m_driver->m_noProgressTime > 5000) {
                ++index;
                if ((index & 0xFFFF) == static_cast<uint32_t>(count - 1))
                    return OnDriveArrived(car);
                m_driver->m_noProgressTime = 0;
                m_driver->m_nearestDist2 = 1e8f;
            }
        }
    } else {
        // Standing again after it got going: 3 s of it and it backs up (or stops backing up).
        m_driver->m_stuckTime += timeDelta;
        if (m_driver->m_stuckTime > 3000) {
            m_driver->m_reversing = !m_driver->m_reversing;
            m_driver->m_stuckTime = 0;
            m_driver->m_moving = false;
        }
        m_driver->m_noProgressTime = 0;
        m_driver->m_nearestDist2 = 1e8f;
    }

    const bool backward = std::signbit(m_driver->m_speed);
    if (!m_driver->m_reversing) {
        // The nearest of the next three segments to where the car will be.
        float nearest = 1e8f;
        const uint32_t first = index & 0xFFFF;
        for (uint32_t i = first; i < first + 3; ++i) {
            if (count - 1 <= static_cast<int>(i))
                break;
            const S_vector &p0 = XYZ(path[i].pos);
            const S_vector &p1 = XYZ(path[i + 1].pos);
            const S_vector offset = ahead - p0;
            const S_vector segment = p1 - p0;
            float t = segment.Dot(offset);
            float distance2;
            if (std::signbit(t)) {
                distance2 = offset.Square();
            } else {
                t /= segment.Square();
                if (t <= 1.0f)
                    distance2 = (offset - segment * t).Square();
                else if (i == static_cast<uint32_t>(count - 2))
                    distance2 = (p1 - ahead).Square();
                else
                    continue;
            }
            if (distance2 < nearest) {
                nearest = distance2;
                index = i;
            }
        }

        // The point to steer to on the Hermite curve between the segment's points, a third
        // further along than the car is (never back along the same segment).
        const uint32_t i = index & 0xFFFF;
        const S_AIPathPoint &a = path[i];
        const S_AIPathPoint &b = path[i + 1];
        const S_vector &p0 = XYZ(a.pos);
        const S_vector &t0 = XYZ(a.dir);
        const S_vector &p1 = XYZ(b.pos);
        const S_vector &t1 = XYZ(b.dir);
        const S_vector diff = p0 - p1;
        const S_vector c3 = diff + diff + t1 + t0;
        const S_vector c2 = t0 * -2.0f - t1 - diff * 3.0f;
        const S_vector segment = p1 - p0;
        float u = segment.Dot(ahead - p0);
        if (std::signbit(u))
            u = 0.1f;
        else
            u /= segment.Square();
        u *= 1.3f;
        if (u > 1.0f)
            u = 1.0f;
        if (i == m_driver->m_pathIndex && u < m_driver->m_progress)
            u = m_driver->m_progress;
        const float u2 = u * u;
        const float u3 = u2 * u;
        const S_vector aim = c3 * u3 + c2 * u2 + t0 * u + p0;
        const S_vector slope = c3 * (u2 * 3.0f) + c2 * (u + u) + t0;
        S_vector tangent(slope.x, 0.0f, slope.z);
        tangent.Normalize();

        if (m_driver->m_pathIndex != static_cast<uint16_t>(i)) {
            m_driver->m_nearestDist2 = 1e8f;
            m_driver->m_noProgressTime = 0;
        }
        m_driver->m_pathIndex = static_cast<uint16_t>(i);
        m_driver->m_progress = u;
        // (A negative speed drives backward.)
        if (backward)
            SteerReverse(car, ahead, -carDir, aim, tangent, b.last, p1);
        else
            SteerForward(car, ahead, carDir, aim, tangent, b.last, p1);
        // At the destination: within 2 m of it or past it.
        if (b.last) {
            const S_vector past = ahead - p1;
            if (past.Square() < 4.0f || !std::signbit(past.Dot(segment)))
                return OnDriveArrived(car);
        }
    } else {
        // Backing up toward the segment's end, until past it.
        const uint32_t i = index & 0xFFFF;
        const S_AIPathPoint &a = path[i];
        const S_AIPathPoint &b = path[i + 1];
        if (backward)
            SteerForward(car, ahead, carDir, XYZ(b.pos), XYZ(b.dir), true, XYZ(b.pos));
        else
            SteerReverse(car, ahead, -carDir, XYZ(b.pos), XYZ(b.dir), true, XYZ(b.pos));
        const S_vector past = ahead - XYZ(b.pos);
        if (!std::signbit(past.Dot(XYZ(b.pos) - XYZ(a.pos))))
            m_driver->m_reversing = false;
        if (m_driver->m_pathIndex != static_cast<uint16_t>(i)) {
            m_driver->m_nearestDist2 = 1e8f;
            m_driver->m_noProgressTime = 0;
        }
        m_driver->m_pathIndex = static_cast<uint16_t>(i);
    }
    return result;
}

int C_AIController::OnDriveArrived(C_Car *car)
{
    m_driver->ClearFlags(C_AIDriver::FLAG_DRIVING);
    car->GetVehicle()->SetUnknown4f2630(car->GetUnknown780());
    if (m_type == HUMAN_CONTROLLER_AI) {
        // The script goes on at the label it gave.
        const S_FrameData *data = m_human->GetFrame()->GetData();
        if (data && data->script)
            data->script->OnActionDone(m_driver->m_label);
    } else if (m_human->GetOrderCount() == 1) {
        car->GetVehicle()->SetHandbrake(true, true);
    }
    return 1;
}

float C_AIController::GetDriveSpeed(const C_Car *car) const
{
    if (m_type == HUMAN_CONTROLLER_AI)
        return std::fabs(m_driver->m_speed);
    switch (m_driver->m_pace) {
    case MOVE_PACE_4:
        return car->GetUnknown780() * 1.0f;
    case MOVE_PACE_1:
    case MOVE_PACE_2:
        return car->GetUnknown780() * 0.25f;
    case MOVE_PACE_3:
        return car->GetUnknown780() * 0.75f;
    case MOVE_PACE_MARCH:
        return car->GetUnknown780() * 1.25f;
    default:
        return car->GetUnknown780() * 0.1f;
    }
}

namespace {

// SteerForward / SteerReverse: the direction to drive in, turning from the curve's point to
// its tangent within 3 m of it, against the car's (both in the xz plane); the steering is
// how far it turns (-1..1, full lock past 90 degrees).
float SteerAngle(const S_vector &ahead, S_vector carDir, const S_vector &aim, const S_vector &tangent, bool reverse)
{
    S_vector toAim(aim.x - ahead.x, 0.0f, aim.z - ahead.z);
    const float distance = toAim.Magnitude();
    float blend = (3.0f - distance) * 0.33333334f;
    if (std::signbit(blend))
        blend = 0.0f;
    if (distance > 1e-8f)
        toAim *= 1.0f / distance;
    S_vector dir = tangent * blend + toAim * (1.0f - blend);
    dir.Normalize();
    carDir.y = 0.0f;
    carDir.Normalize();
    const float cross = dir.y * 0.0f + dir.x * carDir.z + dir.z * -carDir.x;
    if (!std::signbit(dir.Dot(carDir))) {
        float clamped = cross;
        if (clamped > 1.0f)
            clamped = 1.0f;
        else if (clamped < -1.0f)
            clamped = -1.0f;
        const float angle = std::acos(clamped);
        return reverse ? (angle - 1.5707964f) * 0.63661975f : (1.5707964f - angle) * 0.63661975f;
    }
    const float lock = std::signbit(cross) ? -1.0f : 1.0f;
    return reverse ? -lock : lock;
}

// The share of the top speed: less in turns, and slowing down over the last 4 m.
float SpeedShare(float steer, float turnFactor, float least, bool last, const S_vector &ahead, const S_vector &end)
{
    float share = (1.0f - std::fabs(steer)) * turnFactor + least;
    if (last) {
        float nearness = (ahead - end).Square() * 0.0625f;
        if (nearness > 1.0f)
            nearness = 1.0f;
        share = nearness * share;
    }
    return share;
}

}  // namespace

void C_AIController::SteerForward(C_Car *car, const S_vector &ahead, const S_vector &carDir, const S_vector &aim,
                                  const S_vector &tangent, bool last, const S_vector &end)
{
    const float speed = car->GetSpeed();
    const float steer = SteerAngle(ahead, carDir, aim, tangent, false);
    const float target = SpeedShare(steer, 0.7f, 0.3f, last, ahead, end) * GetDriveSpeed(car);
    C_Vehicle *controls = car->GetVehicle();
    controls->SetUnknown4f2630(std::fabs(target));
    float brake;
    if (target <= speed) {
        controls->SetThrottle(0.0f, true);
        brake = (speed - target) / speed;
    } else {
        controls->SetThrottle((target - speed) / target, true);
        brake = 0.0f;
    }
    controls->SetBrake(brake);
    controls->SetSteering(steer);
    controls->SetHandbrake(false, false);
}

void C_AIController::SteerReverse(C_Car *car, const S_vector &ahead, const S_vector &carDir, const S_vector &aim,
                                  const S_vector &tangent, bool last, const S_vector &end)
{
    const float speed = car->GetSpeed();
    const float steer = SteerAngle(ahead, carDir, aim, tangent, true);
    const float target = SpeedShare(steer, 0.8f, 0.2f, last, ahead, end) * GetDriveSpeed(car);
    C_Vehicle *controls = car->GetVehicle();
    controls->SetUnknown4f2630(std::fabs(target));
    float throttle;
    if (target <= speed) {
        controls->SetBrake(0.0f);
        throttle = (speed - target) / speed;
    } else {
        controls->SetBrake((target - speed) / target);
        throttle = 0.0f;
    }
    controls->SetThrottle(throttle, true);
    controls->SetSteering(steer);
    controls->SetHandbrake(false, false);
}

// ------------------------------------------------------------------------- the move order

int C_AIController::UpdateMove(int timeDelta)
{
    S_AIMoveOrder *move = m_move;
    move->unknown34 -= timeDelta;
    if (move->unknown34 < 0)
        move->unknown34 = 0;
    move->unknown24 -= timeDelta;
    if (move->unknown24 < 0)
        move->unknown24 = 0;
    move->unknown4C = MOVE_RESULT_MOVING;
    if (!CheckCarsOnRoute())
        return m_move->unknown4C;

    if (m_move->flags & S_AIMoveOrder::FLAG_MAKE_WAY) {
        FollowStep(timeDelta);
        if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE) {
            m_move->unknown4C = MOVE_RESULT_MAKING_WAY;
            return MOVE_RESULT_MAKING_WAY;
        }
        m_move->flags &= ~S_AIMoveOrder::FLAG_MAKE_WAY;
    }
    if (m_move->flags & S_AIMoveOrder::FLAG_WAIT) {
        m_move->unknown4C = MOVE_RESULT_WAITING;
        return MOVE_RESULT_WAITING;
    }
    if (m_move->flags & S_AIMoveOrder::FLAG_REROUTE)
        EndMove(MOVE_RESULT_ENDED);
    if (m_move->unknown58) {
        m_move->unknown4C = MOVE_RESULT_STAYING;
        return MOVE_RESULT_STAYING;
    }
    if ((m_move->flags & S_AIMoveOrder::FLAG_NEW) && NeedsNewRoute()) {
        if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE)
            EndMove(MOVE_RESULT_ENDED);
        if (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && !StartRoute())
            return m_move->unknown4C;
    }
    if (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE)) {
        UpdateAvoidance();
    } else {
        FollowStep(timeDelta);
        if (m_move->flags & S_AIMoveOrder::FLAG_DONE)
            return m_move->unknown4C;
    }
    m_move->unknown4C = MOVE_RESULT_MOVING;
    return MOVE_RESULT_MOVING;
}

bool C_AIController::NeedsNewRoute()
{
    S_AIMoveOrder *move = m_move;
    if (move->flags & S_AIMoveOrder::FLAG_FLEE)
        return true;
    if (move->flags & S_AIMoveOrder::FLAG_MAKE_WAY)
        return false;

    if (!(move->flags & S_AIMoveOrder::FLAG_ROUTE)) {
        // Standing within 0.5 m of the destination: arrived (a missing point too).
        if (!(move->flags & S_AIMoveOrder::FLAG_CHECKPOINT_POS) && move->checkpoint != 0xFFFF) {
            // (Fix: the original asks a missing network.)
            const S_Checkpoint *point = m_checkpoints ? m_checkpoints->Get(move->checkpoint) : nullptr;
            if (point && !IsHumanWithin(&point->pos, 0.5f))
                return true;
        } else if (!IsHumanWithin(&XYZ(move->pos), 0.5f)) {
            return true;
        }
        move->flags &= ~(S_AIMoveOrder::FLAG_NEW | S_AIMoveOrder::FLAG_CHECKPOINT_POS | S_AIMoveOrder::FLAG_REROUTE);
        move->flags |= S_AIMoveOrder::FLAG_DONE;
        return false;
    }

    // On a route: a destination near enough to its end moves the end there.
    if (m_waypoints.empty())
        return true;
    S_AIWaypoint *last = m_waypoints.back();
    if (!(move->flags & S_AIMoveOrder::FLAG_CHECKPOINT_POS)) {
        if (move->checkpoint == 0xFFFF) {
            if (!IsWithin(XYZ(last->pos2), last->radius, XYZ(move->pos)))
                return true;
            last->pos = move->pos;
        } else {
            if (last->checkpoint != static_cast<int>(move->checkpoint))
                return true;
            const S_Checkpoint *point = m_checkpoints->Get(move->checkpoint);
            if (!point)
                return true;
            // (w: a stale local in the original.)
            last->pos.x = point->pos.x;
            last->pos.y = point->pos.y;
            last->pos.z = point->pos.z;
        }
        move->flags &= ~(S_AIMoveOrder::FLAG_NEW | S_AIMoveOrder::FLAG_REROUTE);
        return false;
    }
    if (last->checkpoint == static_cast<int>(move->checkpoint)) {
        last->pos = move->pos;
        move->flags &= ~(S_AIMoveOrder::FLAG_NEW | S_AIMoveOrder::FLAG_CHECKPOINT_POS | S_AIMoveOrder::FLAG_REROUTE);
        return false;
    }
    return true;
}

bool C_AIController::CheckCarsOnRoute()
{
    // (The scripted AI only while it follows a route.)
    if (m_type == HUMAN_CONTROLLER_AI && !(m_move->flags & S_AIMoveOrder::FLAG_ROUTE))
        return true;
    const uint32_t flags = m_move->flags;
    const bool onRoute = (flags & S_AIMoveOrder::FLAG_ROUTE) != 0;
    // A car stands on the human's point: off it.
    if ((!onRoute || (flags & S_AIMoveOrder::FLAG_WAIT) || m_move->unknown58) && m_currentCheckpoint &&
        (m_currentCheckpoint->flags & CHECKPOINT_FLAG_UNDER_CAR)) {
        MakeWay();
        return true;
    }
    // ... or on the point it makes way to: on to another.
    if ((flags & S_AIMoveOrder::FLAG_MAKE_WAY) && onRoute && !m_waypoints.empty()) {
        const S_Checkpoint *point = m_checkpoints->Get(m_waypoints.back()->checkpoint);
        if (point && (point->flags & CHECKPOINT_FLAG_UNDER_CAR))
            MakeWay();
    }
    // Waiting for the car on the next point to leave; in vain when the time is up.
    if (m_move->flags & S_AIMoveOrder::FLAG_WAIT) {
        if ((m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && !m_waypoints.empty()) {
            const S_Checkpoint *point = m_checkpoints->Get(m_waypoints.front()->checkpoint);
            if (point && !(point->flags & CHECKPOINT_FLAG_UNDER_CAR)) {
                m_move->flags &= ~S_AIMoveOrder::FLAG_WAIT;
                return true;
            }
        }
        if (m_move->unknown34 == 0) {
            EndMove(MOVE_RESULT_WAITED);
            return false;
        }
    }
    return true;
}

bool C_AIController::MakeWay()
{
    // To the nearest point no car stands on, at the running pace.
    if (!m_checkpoints || !m_currentCheckpoint)
        return false;
    const int point = m_checkpoints->FindNearestFree(m_currentCheckpoint);
    if (point == CHECKPOINT_NONE)
        return false;
    m_move->SetCheckpoint(static_cast<int16_t>(point));
    m_move->unknown54 = MOVE_PACE_4;
    m_move->flags |= S_AIMoveOrder::FLAG_MAKE_WAY;
    EndMove(MOVE_RESULT_ENDED);
    StartRoute();
    return true;
}

// ------------------------------------------------------------------------- routes

bool C_AIController::PlanRoute()
{
    const S_vector4 from = WorldPos4(m_human->GetFrame());
    if (m_move->flags & S_AIMoveOrder::FLAG_FLEE)
        return RouteAwayFromEnemies(from);
    if (m_move->checkpoint == 0xFFFF)
        return RouteToPos(from, m_move->pos);
    if (!RouteToCheckpoint(from, m_move->checkpoint))
        return false;
    m_move->flags &= ~S_AIMoveOrder::FLAG_CHECKPOINT_POS;
    return true;
}

int C_AIController::GetRouteStart(const S_vector4 &from) const
{
    if ((m_moveFlags & MOVE_ON_LADDER) && m_move->ladderTo != 0xFFFF)
        return m_move->ladderTo;
    return m_checkpoints->FindNearest(XYZ(from), 5.0f, -1.0f);
}

uint32_t C_AIController::GetRouteExcludeFlags() const
{
    return m_type == HUMAN_CONTROLLER_ENEMY || m_type == HUMAN_CONTROLLER_SNIPER ? CHECKPOINT_FLAG_NO_STOP : 0;
}

void C_AIController::AddPathWaypoints(const std::vector<uint16_t> &path)
{
    for (uint16_t index : path) {
        const S_Checkpoint *point = m_checkpoints->Get(index);
        // (w: a stale local in the original.)
        m_waypoints.push_back(AllocWaypoint(index, ToVector4(point->pos, 0.0f), point->flags & CHECKPOINT_SEARCH_FLAGS,
                                            point->type, static_cast<float>(point->radius) * 0.0625f));
    }
}

void C_AIController::TrimLadderStart()
{
    if ((m_moveFlags & MOVE_ON_LADDER) && m_waypoints.size() > 1 &&
        m_waypoints[1]->checkpoint == static_cast<int>(m_move->ladderFrom)) {
        ReleaseWaypoint(m_waypoints[0]);
        m_waypoints.erase(m_waypoints.begin());
    }
}

bool C_AIController::RouteToPos(const S_vector4 &from, const S_vector4 &to)
{
    if (!m_checkpoints)
        return false;
    const uint32_t exclude = GetRouteExcludeFlags();
    int start = GetRouteStart(from);
    int end = m_checkpoints->FindNearest(XYZ(to), 5.0f, -1.0f);
    if (m_human->m_body.IsWorkState5()) {
        if (start == CHECKPOINT_NONE)
            start = m_checkpoints->FindNearestType15(XYZ(from), 5.0f, -1.0f);
        if (end == CHECKPOINT_NONE)
            end = m_checkpoints->FindNearestType15(XYZ(to), 5.0f, -1.0f);
    }
    ReleaseWaypoints(m_waypoints);
    std::vector<uint16_t> path;
    if (start != CHECKPOINT_NONE && end != CHECKPOINT_NONE && m_checkpoints->FindPath(start, end, &path, exclude)) {
        AddPathWaypoints(path);
        if (!m_waypoints.empty())
            MoveWaypointToward(*m_waypoints.back(), to);
        TrimLadderStart();
        SmoothRoute(from, to);
        // Already past the first waypoint: the route starts where the human stands.
        if (m_waypoints.size() > 1) {
            S_AIWaypoint *first = m_waypoints[0];
            const S_AIWaypoint *second = m_waypoints[1];
            if ((XYZ(second->pos) - XYZ(from)).Dot(XYZ(first->pos) - XYZ(from)) < 0.0f)
                first->pos = from;
        }
        return true;
    }
    // Swimming: straight there.
    if (m_moveFlags & MOVE_SWIMMING) {
        m_waypoints.push_back(AllocWaypoint(-1, to, 0, 0, 0.0f));
        return true;
    }
    if (start == CHECKPOINT_NONE)
        m_move->unknown4C = MOVE_RESULT_NO_START_POINT;
    else if (end == CHECKPOINT_NONE)
        m_move->unknown4C = MOVE_RESULT_NO_END_POINT;
    else
        m_move->unknown4C = MOVE_RESULT_NO_PATH;
    return false;
}

bool C_AIController::RouteToCheckpoint(const S_vector4 &from, uint16_t checkpoint)
{
    if (checkpoint == 0xFFFF) {
        m_move->unknown4C = MOVE_RESULT_NO_END_POINT;
        return false;
    }
    // Fix: the original counts the points of a missing network first.
    if (!m_checkpoints)
        return false;
    if (checkpoint >= m_checkpoints->GetCount()) {
        m_move->unknown4C = MOVE_RESULT_NO_END_POINT;
        return false;
    }
    const uint32_t exclude = GetRouteExcludeFlags();
    int start = GetRouteStart(from);
    if (m_human->m_body.IsWorkState5() && start == CHECKPOINT_NONE)
        start = m_checkpoints->FindNearestType15(XYZ(from), 5.0f, -1.0f);
    ReleaseWaypoints(m_waypoints);
    if (start == CHECKPOINT_NONE) {
        m_move->unknown4C = MOVE_RESULT_NO_START_POINT;
        return false;
    }
    std::vector<uint16_t> path;
    if (!m_checkpoints->FindPath(start, checkpoint, &path, exclude)) {
        m_move->unknown4C = MOVE_RESULT_NO_PATH;
        return false;
    }
    AddPathWaypoints(path);
    TrimLadderStart();
    if ((m_move->flags & S_AIMoveOrder::FLAG_CHECKPOINT_POS) && !m_waypoints.empty())
        MoveWaypointToward(*m_waypoints.back(), m_move->pos);
    SmoothRouteTo(from);
    // Already past the first waypoint (in the xz plane): from where the human stands.
    if (!(m_moveFlags & MOVE_ON_LADDER) && m_waypoints.size() > 1) {
        S_AIWaypoint *first = m_waypoints[0];
        const S_AIWaypoint *second = m_waypoints[1];
        if ((second->pos.x - from.x) * (first->pos.x - from.x) + (second->pos.z - from.z) * (first->pos.z - from.z) < 0.0f)
            first->pos = from;
    }
    return true;
}

bool C_AIController::RouteAwayFromEnemies(const S_vector4 &from)
{
    // Only enemies flee (and controller type 8), from the enemies they see.
    if (m_type != HUMAN_CONTROLLER_ENEMY && m_type != HUMAN_CONTROLLER_SNIPER)
        return false;
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    if (enemies.empty() || !m_checkpoints)
        return false;
    int start;
    if (m_currentCheckpoint &&
        IsHumanWithin(&m_currentCheckpoint->pos, static_cast<float>(m_currentCheckpoint->radius) * 0.0625f)) {
        start = m_checkpoints->GetIndex(m_currentCheckpoint);
        if (start >= m_checkpoints->GetCount() || start < 0)
            start = CHECKPOINT_NONE;
    } else {
        start = GetRouteStart(from);
    }
    ReleaseWaypoints(m_waypoints);

    std::vector<S_vector4> positions;
    for (const C_Human *enemy : enemies)
        positions.push_back(WorldPos4(enemy->GetFrame()));
    S_Vector4Array threats;
    threats.data = positions.data();
    threats.capacity = static_cast<int>(positions.size());
    threats.count = static_cast<int>(positions.size());

    if (start == CHECKPOINT_NONE) {
        m_move->unknown4C = MOVE_RESULT_NO_START_POINT;
        return false;
    }
    std::vector<uint16_t> path;
    if (!m_checkpoints->FindEscapePath(start, threats, static_cast<float>(m_move->unknown38),
                                       static_cast<float>(m_move->unknown3C), path, CHECKPOINT_FLAG_NO_STOP)) {
        m_move->unknown4C = MOVE_RESULT_NO_PATH;
        return false;
    }
    AddPathWaypoints(path);
    TrimLadderStart();
    SmoothRouteTo(from);
    if (m_waypoints.size() > 1) {
        S_AIWaypoint *first = m_waypoints[0];
        const S_AIWaypoint *second = m_waypoints[1];
        if ((second->pos.x - from.x) * (first->pos.x - from.x) + (second->pos.z - from.z) * (first->pos.z - from.z) < 0.0f)
            first->pos = from;
    }
    return true;
}

void C_AIController::SmoothRoute(const S_vector4 &from, const S_vector4 &to)
{
    S_vector prev = XYZ(from);
    for (size_t i = 0; i < m_waypoints.size(); ++i) {
        const S_vector next = i + 1 == m_waypoints.size() ? XYZ(to) : XYZ(m_waypoints[i + 1]->pos);
        CutCorner(*m_waypoints[i], prev, next, 0.0f);
        prev = XYZ(m_waypoints[i]->pos);
    }
    FixRouteHeights(from, to);
}

void C_AIController::SmoothRouteTo(const S_vector4 &from)
{
    // (The last waypoint stays; the corners keep 0.1 m inside the radius.)
    // Fix: the original reads the first waypoint of an empty route.
    if (m_waypoints.empty())
        return;
    S_vector prev = XYZ(from);
    for (size_t i = 0; i + 1 < m_waypoints.size(); ++i) {
        CutCorner(*m_waypoints[i], prev, XYZ(m_waypoints[i + 1]->pos), 0.1f);
        prev = XYZ(m_waypoints[i]->pos);
    }
    FixRouteHeights(from, m_waypoints.back()->pos);
}

void C_AIController::FixRouteHeights(const S_vector4 &from, const S_vector4 &to)
{
    // A run of type 15 waypoints gets heights along the line from the point before it to the
    // one after it (each from the one before by the distances left), at least 1 m over its
    // points.
    S_vector start(0.0f, 0.0f, 0.0f);
    S_vector end(0.0f, 0.0f, 0.0f);
    int runStart = -1;
    for (size_t i = 0; i < m_waypoints.size(); ++i) {
        const S_AIWaypoint *waypoint = m_waypoints[i];
        const bool inRun = waypoint->type == CHECKPOINT_TYPE_15 && waypoint->flags != WAYPOINT_FLAGS_KEEP_HEIGHT;
        if (runStart == -1) {
            if (inRun) {
                start = i == 0 ? XYZ(from) : XYZ(m_waypoints[i - 1]->pos);
                runStart = static_cast<int>(i);
            }
            continue;
        }
        int runEnd = -1;
        if (!inRun) {
            runEnd = static_cast<int>(i) - 1;
            end = XYZ(waypoint->pos);
        } else if (i + 1 >= m_waypoints.size()) {
            runEnd = static_cast<int>(i);
            end = XYZ(to);
        }
        if (runEnd == -1)
            continue;
        float length = S_vector(end.x - start.x, 0.0f, end.z - start.z).Magnitude();
        if (length > 0.1f) {
            float height = start.y;
            for (int k = runStart; k <= runEnd; ++k) {
                S_AIWaypoint *point = m_waypoints[k];
                const float distance = S_vector(end.x - point->pos.x, 0.0f, end.z - point->pos.z).Magnitude();
                const float line = (height - end.y) / length * distance + end.y;
                height = point->pos2.y + 1.0f;
                if (height < line)
                    height = line;
                point->pos.y = height;
                length = distance;
            }
        }
        runStart = -1;
    }
}

bool C_AIController::StartRoute()
{
    if (!PlanRoute()) {
        m_move->flags &= ~(S_AIMoveOrder::FLAG_NEW | S_AIMoveOrder::FLAG_CHECKPOINT_POS | S_AIMoveOrder::FLAG_FLEE);
        m_move->flags |= S_AIMoveOrder::FLAG_DONE;
        return false;
    }
    m_move->BeginRoute();
    if (m_human->m_body.IsWorkState5()) {
        m_move->flags &= ~S_AIMoveOrder::FLAG_TURN_FIRST;
        m_move->flags |= S_AIMoveOrder::FLAG_ROUTE_15;
    } else if (m_moveFlags & MOVE_UNKNOWN_8000000) {
        // Out of cover first.
        m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_ACTION, BODY_ACTION_BACK_TO_COVER));
        const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};
        RequestAction(CONTROLLER_ACTION_BACK_TO_COVER, &zero, &zero, 0.0f, -1, -1);
    }
    m_unknown10C = 0;
    m_walkSpeed = 0.0f;
    // Turn toward the first waypoint (the second when standing on the first).
    const S_vector4 pos = WorldPos4(m_human->GetFrame());
    if (!SamePosXZ(pos, m_waypoints[0]->pos))
        TurnTowards(pos, m_waypoints[0]->pos);
    else if (m_waypoints.size() > 1)
        TurnTowards(pos, m_waypoints[1]->pos);
    return true;
}

// ------------------------------------------------------------------------- following

void C_AIController::FollowStep(int timeDelta)
{
    if (m_move->flags & S_AIMoveOrder::FLAG_DONE)
        return;
    UpdateCurrentCheckpoint();
    UpdateMoveStance();
    const uint32_t flags = m_move->flags;
    if (flags & S_AIMoveOrder::FLAG_DOOR)
        PassDoor(timeDelta);
    else if (flags & (S_AIMoveOrder::FLAG_TO_LADDER | S_AIMoveOrder::FLAG_ON_LADDER))
        UseLadder(timeDelta);
    else if (flags & S_AIMoveOrder::FLAG_ROUTE_15)
        FollowType15Route(timeDelta);
    else
        FollowRoute(timeDelta);
}

void C_AIController::UseLadder(int /*timeDelta*/)
{
    S_AIMoveOrder *move = m_move;
    const float height = m_human->GetFrame()->GetWorldPos().y;
    if (move->flags & S_AIMoveOrder::FLAG_TO_LADDER) {
        if (m_human->m_body.GetClimbState() == 0) {
            // Not on it yet: enemies wait while someone else is on it; nobody climbs in alert
            // state 2.
            if (m_type == HUMAN_CONTROLLER_ENEMY || m_type == HUMAN_CONTROLLER_SNIPER) {
                const std::vector<C_Human *> &climbers = GetLadderClimbers(move->ladder);
                if (!climbers.empty() && (climbers.size() > 1 || climbers[0] != m_human)) {
                    EndMove(MOVE_RESULT_LADDER_BUSY);
                    return;
                }
            }
            if (m_alert->Is2()) {
                EndMove(MOVE_RESULT_LADDER_BUSY);
                return;
            }
            if (m_alert->Is4() && !m_facing->IsFacingLocked()) {
                m_facing->UnlockView();
                m_facing->LockFacing();
            }
            S_HumanCommand command;
            command.category = HUMAN_COMMAND_VEHICLE;
            command.type = HUMAN_COMMAND_USE_LADDER;
            command.param1 = move->ladder->GetNetId();
            C_HumanController::IssueCommand(command);
            if (IsLocalGame())
                return;
            // Fix: the original makes the clients' message on a client too and leaves it.
            if (!IsServer())
                return;
            S_MsgUseObject *message = new S_MsgUseObject;
            message->actor = move->ladder->GetNetId();
            SendToClients(message);
            return;
        }
        move->flags &= ~S_AIMoveOrder::FLAG_TO_LADDER;
        move->flags |= S_AIMoveOrder::FLAG_ON_LADDER;
        if (height - m_waypoints[0]->pos.y > 0.0f)
            move->flags |= S_AIMoveOrder::FLAG_LADDER_DOWN;
    } else if ((move->flags & S_AIMoveOrder::FLAG_ON_LADDER) && m_human->m_body.GetClimbState() == 0) {
        // Off it.
        if (m_alert->Is4() && m_facing->IsFacingLocked()) {
            m_facing->UnlockFacing();
            m_facing->LockView();
        }
        move->ladder = nullptr;
        move->ladderFrom = 0xFFFF;
        move->ladderTo = 0xFFFF;
        move->flags &= ~S_AIMoveOrder::FLAG_LADDER_MASK;
        return;
    }
    // Up or down (the forward / back axes), walking or faster.
    if (height - m_waypoints[0]->pos.y <= 0.0f)
        move->flags &= ~S_AIMoveOrder::FLAG_LADDER_DOWN;
    else
        move->flags |= S_AIMoveOrder::FLAG_LADDER_DOWN;
    m_stateFlags = move->unknown54 & (MOVE_PACE_4 | MOVE_PACE_MARCH) ? MOVE_PACE_4 : MOVE_PACE_3;
    if (!(move->unknown54 & 0x10) && !(move->flags & S_AIMoveOrder::FLAG_LADDER_DOWN))
        m_axes[AXIS_FORWARD] = 1.0f;
    else
        m_axes[AXIS_BACK] = 1.0f;
}

int C_AIController::PassDoor(int /*timeDelta*/)
{
    if (m_move->unknown34 != 0)
        return 3;
    C_Door *door = static_cast<C_Door *>(m_move->door);
    const int state = door->GetMotion();
    if (state == 3 || state == 0) {
        // Closed: face the waypoint behind it (0.3 s), lower the weapon (0.5 s), open it.
        if (m_type == HUMAN_CONTROLLER_SQUAD && door->IsLocked()) {
            EndMove(MOVE_RESULT_DOOR_LOCKED);
            return MOVE_RESULT_DOOR_LOCKED;
        }
        if (m_alert->Is2()) {
            EndMove(MOVE_RESULT_DOOR_ALARM);
            return MOVE_RESULT_DOOR_ALARM;
        }
        if (m_alert->Is4() && !m_facing->IsFacingLocked()) {
            m_facing->UnlockView();
            m_facing->LockFacing();
        }
        const I3D_frame *frame = m_human->GetFrame();
        S_vector facing = frame->GetMatrix()(2);
        facing.Normalize();
        facing.Normalize();                             // (twice in the original)
        S_vector toWaypoint = XYZ(m_waypoints[0]->pos) - frame->GetWorldPos();
        toWaypoint.Normalize();
        if (facing.Dot(toWaypoint) < 0.86f) {
            m_facing->SetFacing(ToVector4(toWaypoint, 0.0f));
            m_move->unknown34 = 300;
        }
        if (m_handsItem && m_weaponRaised) {
            LowerWeapon();
            m_move->unknown34 = 500;
        }
        if (m_move->unknown34 != 0)
            return 3;
        S_HumanCommand command;
        command.category = HUMAN_COMMAND_VEHICLE;
        command.type = HUMAN_COMMAND_OPEN_DOOR;
        command.param1 = door->GetNetId();
        C_HumanController::IssueCommand(command);
    } else if (state == 2) {
        // Swinging open toward the human's side: out of its way.
        if (door->GetSide(WorldPos4(m_human->GetFrame())) == 1) {
            m_axes[AXIS_BACK] = 1.0f;
            return 1;
        }
    } else if (state == 1) {
        // Open: through it.
        if (m_alert->Is4() && m_facing->IsFacingLocked()) {
            m_facing->UnlockFacing();
            m_facing->LockView();
        }
        m_move->door = nullptr;
        m_move->flags &= ~(S_AIMoveOrder::FLAG_DOOR | S_AIMoveOrder::FLAG_20000000);
        WalkToWaypoint();
        return 1;
    }
    return 1;
}

bool C_AIController::ReachedWaypoint()
{
    if (m_waypoints.empty())
        return false;
    const S_vector &pos = m_human->GetFrame()->GetWorldPos();
    const S_AIWaypoint *waypoint = m_waypoints[0];
    const float distance = S_vector(waypoint->pos.x - pos.x, 0.0f, waypoint->pos.z - pos.z).Magnitude();
    S_AIMoveOrder *move = m_move;
    if (move->unknown2C <= distance)
        ++move->unknown28;
    else
        move->unknown2C = distance;
    if (distance < 1.0f) {
        if (m_waypoints.size() == 1)
            m_walkSpeed = distance;                     // slowing down for the end
        // Within 0.1 m, or up to 0.5 m after steps that brought it no nearer ...
        const int steps = move->unknown28;
        if ((steps < 2 && distance < 0.1f) || (steps == 2 && distance < 0.2f) || (steps == 3 && distance < 0.3f) ||
            (steps == 4 && distance < 0.5f)) {
            move->unknown28 = 0;
            move->unknown2C = std::numeric_limits<float>::max();
            return true;
        }
        // ... or walking past it.
        if (XYZ(m_lookAt).Square() >= 1e-16f) {
            const S_vector walk = XYZ(m_lookAt).Normalized();
            const S_vector toWaypoint = (XYZ(waypoint->pos) - pos).Normalized();
            if (toWaypoint.Dot(walk) <= 0.0f) {
                move->unknown28 = 0;
                move->unknown2C = std::numeric_limits<float>::max();
                return true;
            }
        }
    }
    if (move->unknown28 > 29)
        EndMove(MOVE_RESULT_STUCK);
    return false;
}

void C_AIController::FollowType15Route(int /*timeDelta*/)
{
    if (m_waypoints.empty()) {
        EndMove(MOVE_RESULT_ENDED);
        return;
    }
    const bool reached = ReachedWaypoint();
    if (m_move->flags & S_AIMoveOrder::FLAG_DONE)
        return;
    AvoidOthersOnRoute(reached);
    if (m_waypoints.size() > 1 && m_waypoints[0]->type != CHECKPOINT_TYPE_15)
        m_move->flags &= ~S_AIMoveOrder::FLAG_ROUTE_15;
    if (reached) {
        ReleaseWaypoint(m_waypoint);
        m_waypoint = m_waypoints[0];
        m_waypoints.erase(m_waypoints.begin());
        if (m_waypoints.empty()) {
            EndMove(MOVE_RESULT_ENDED);
            return;
        }
    }
    WalkToWaypoint();
}

void C_AIController::Climb()
{
    if (!IsLocalGame()) {
        if (m_unknown120 > 0)
            return;
        m_unknown120 = 1000;
    }
    S_HumanCommand command;
    command.category = HUMAN_COMMAND_VEHICLE;
    command.type = HUMAN_COMMAND_CLIMB;
    C_HumanController::IssueCommand(command);
}

void C_AIController::FollowRoute(int /*timeDelta*/)
{
    if (m_waypoints.empty()) {
        EndMove(MOVE_RESULT_ENDED);
        return;
    }
    const bool reached = ReachedWaypoint();
    if (m_move->flags & S_AIMoveOrder::FLAG_DONE)
        return;
    const float height = m_human->GetFrame()->GetWorldPos().y;

    if (!reached) {
        // The climbs on the way to the next point.
        const S_Checkpoint *current = m_currentCheckpoint;
        if (current && current != m_checkpoints->Get(m_waypoints[0]->checkpoint)) {
            const S_AIWaypoint *next = m_waypoints[0];
            if (current->type == CHECKPOINT_TYPE_6 && next->type == CHECKPOINT_TYPE_5 && next->pos.y - height > 0.3f)
                Climb();
            if (m_currentCheckpoint->type == CHECKPOINT_TYPE_4 && next->type == CHECKPOINT_TYPE_4)
                Climb();
        }
        UpdateCheckpointStance();
        WalkToWaypoint();
        return;
    }

    // Reached: what lies between it and the next one.
    if (m_waypoints.size() > 1) {
        const S_AIWaypoint *point = m_waypoints[0];
        const S_AIWaypoint *next = m_waypoints[1];
        switch (point->type) {
        case CHECKPOINT_TYPE_NORMAL:
            break;
        case CHECKPOINT_TYPE_15:
            m_move->flags |= S_AIMoveOrder::FLAG_ROUTE_15;
            break;
        case CHECKPOINT_TYPE_DOOR:
            if (next->type == CHECKPOINT_TYPE_DOOR) {
                C_Actor *actor = m_checkpoints->GetActor(point->checkpoint & 0xFFFF);
                // (Fix: the original takes the point's actor for a door unchecked.)
                if (actor && actor->GetType() == ACTOR_DOOR) {
                    const int state = static_cast<C_Door *>(actor)->GetMotion();
                    if (state == 3 || state == 0) {
                        m_move->flags |= S_AIMoveOrder::FLAG_DOOR;
                        m_move->door = actor;
                    }
                }
            }
            break;
        case CHECKPOINT_TYPE_LADDER:
            if (next->type == CHECKPOINT_TYPE_LADDER) {
                m_move->ladder = m_checkpoints->GetActor(point->checkpoint & 0xFFFF);
                m_move->ladderFrom = static_cast<uint16_t>(point->checkpoint);
                m_move->ladderTo = static_cast<uint16_t>(next->checkpoint);
                if (m_move->ladder)
                    m_move->flags |= S_AIMoveOrder::FLAG_TO_LADDER;
            }
            break;
        case CHECKPOINT_TYPE_4:
            if (next->type == CHECKPOINT_TYPE_4)
                Climb();
            break;
        case CHECKPOINT_TYPE_6:
            if (next->type == CHECKPOINT_TYPE_5)
                Climb();
            break;
        case CHECKPOINT_TYPE_5: {
            float rise;
            if (next->type == CHECKPOINT_TYPE_5) {
                rise = point->pos.y - next->pos.y;
            } else if (next->type == CHECKPOINT_TYPE_6) {
                // A 5 point 0.5 .. 3 m on: over the gap with the dive button.
                if (m_waypoints.size() > 2 && m_waypoints[2]->type == CHECKPOINT_TYPE_5) {
                    const float distance2 = (XYZ(point->pos) - XYZ(m_waypoints[2]->pos)).Square();
                    if (0.25f < distance2 && distance2 < 9.0f)
                        m_buttons[BUTTON_DIVE] = 1.0f;
                }
                break;
            } else {
                rise = point->pos.y - height;
            }
            if (rise > 0.3f)
                Climb();
            break;
        }
        default:
            break;
        }
    }

    ReleaseWaypoint(m_waypoint);
    m_waypoint = m_waypoints[0];
    m_waypoints.erase(m_waypoints.begin());
    m_human->m_aiMemory->routeChanged = true;
    if (m_waypoints.empty()) {
        EndMove(MOVE_RESULT_ENDED);
        return;
    }
    // A car stands on the next point: wait for it.
    if (!(m_move->flags & S_AIMoveOrder::FLAG_MAKE_WAY)) {
        const S_Checkpoint *next = m_checkpoints->Get(m_waypoints[0]->checkpoint);
        if (next && (next->flags & CHECKPOINT_FLAG_UNDER_CAR)) {
            m_move->Wait();
            return;
        }
    }
    UpdateCheckpointStance();
    WalkToWaypoint();
}

// ------------------------------------------------------------------------- walking

void C_AIController::WalkToWaypoint()
{
    if (m_moveFlags & MOVE_MEDIUM_80000) {
        m_axes[AXIS_FORWARD] = 1.0f;
        m_stateFlags = MOVE_PACE_4;
        return;
    }
    const I3D_frame *frame = m_human->GetFrame();
    const C_HumanAIMemory *memory = m_human->m_aiMemory;
    const S_vector4 pos = WorldPos4(frame);
    int turned;
    if (!memory->stepping) {
        turned = TurnTowards(pos, m_waypoints[0]->pos);
    } else {
        // Stepping aside instead.
        const S_vector4 aside = {memory->step[0] + pos.x, memory->step[1] + pos.y,
                                 memory->step[2] + pos.z, 0.0f};
        turned = TurnTowards(pos, aside);
        m_walkSpeed = S_vector(memory->step[0], memory->step[1], memory->step[2]).Magnitude();
    }

    bool crawlingSideways = false;
    if (turned == 0) {
        // The body turns toward the waypoint: walk on ahead (after the first turn, once
        // within 60 degrees of it).
        if (m_move->flags & S_AIMoveOrder::FLAG_TURN_FIRST) {
            S_vector facing = frame->GetMatrix()(2);
            facing.Normalize();
            facing.Normalize();
            S_vector target(m_facing->facing.x, 0.0f, m_facing->facing.z);
            target.Normalize();
            if (facing.Dot(target) < 0.5f)
                return;
            m_move->flags &= ~S_AIMoveOrder::FLAG_TURN_FIRST;
        }
        m_axes[AXIS_FORWARD] = 1.0f;
    } else {
        // The view stays (locked): walk toward the waypoint forward, back, sideways or
        // diagonally, as near the view's way as it is (keeping the way it walks a little).
        if (m_axes[AXIS_12] != 0.0f)
            m_axes[AXIS_12] = 0.0f;
        S_vector toWaypoint(m_waypoints[0]->pos.x - pos.x, 0.0f, m_waypoints[0]->pos.z - pos.z);
        toWaypoint.Normalize();
        S_vector view(m_facing->view.x, 0.0f, m_facing->view.z);
        view.Normalize();
        const float dot = view.Dot(toWaypoint);
        const bool left = view.x * toWaypoint.z - view.z * toWaypoint.x > 0.0f;
        const float absDot = std::fabs(dot);
        const bool forwardBack = (m_moveFlags & (MOVE_FORWARD | MOVE_BACK)) != 0;
        const bool sideways = (m_moveFlags & (MOVE_LEFT | MOVE_RIGHT)) != 0;
        float straight = 22.5f;
        float side = 67.5f;
        if (forwardBack && sideways) {
            straight = 12.5f;
            side = 77.5f;
        } else if (forwardBack) {
            straight = 32.5f;
        } else if (sideways) {
            side = 57.5f;
        }
        int mode;
        if (absDot > CosDeg(straight))
            mode = 1;
        else
            mode = absDot > CosDeg(side) ? 3 : 2;
        if (mode == 2)
            crawlingSideways = (m_moveFlags & MOVE_LYING) != 0;
        SetWalkAxes(mode, toWaypoint, dot, left);
    }
    SetWalkPace(crawlingSideways);
}

void C_AIController::SetWalkAxes(int mode, const S_vector &dir, float dot, bool left)
{
    const S_vector along = dot <= 0.0f ? -dir : dir;
    const S_vector side = left ? S_vector(dir.z, 0.0f, -dir.x) : S_vector(-dir.z, 0.0f, dir.x);
    if (mode != 2) {
        m_axes[AXIS_FORWARD] = dot > 0.0f ? 1.0f : 0.0f;
        m_axes[AXIS_BACK] = dot <= 0.0f ? 1.0f : 0.0f;
    }
    if (mode != 1) {
        m_axes[AXIS_RIGHT] = left ? 0.0f : 1.0f;
        m_axes[AXIS_LEFT] = left ? 1.0f : 0.0f;
    }
    S_vector facing = mode == 1 ? along : mode == 2 ? side : side + along;
    facing.y = 0.0f;
    facing.Normalize();
    m_facing->SetFacing(ToVector4(facing, 0.0f));
}

void C_AIController::SetWalkPace(bool crawlingSideways)
{
    const uint32_t pace = m_move->unknown54;
    if (pace & MOVE_PACE_2) {
        m_stateFlags = MOVE_PACE_2;
        return;
    }
    if (pace & MOVE_PACE_1) {
        m_stateFlags = MOVE_PACE_1;
        return;
    }
    if ((pace & MOVE_PACE_4) && !MustSlowDown() && !crawlingSideways) {
        m_stateFlags = MOVE_PACE_4;
        return;
    }
    if (pace & MOVE_PACE_MARCH) {
        if (!IsMarchBlocked()) {
            m_stateFlags = MOVE_PACE_MARCH;
            return;
        }
        if (!MustSlowDown()) {
            m_stateFlags = MOVE_PACE_4;
            return;
        }
    }
    m_stateFlags = MOVE_PACE_3;
}

bool C_AIController::MustSlowDown()
{
    if (m_currentCheckpoint && ((m_currentCheckpoint->flags & CHECKPOINT_FLAG_SLOW) || m_human->m_aiMemory->unknown109))
        return true;
    if (!m_waypoints.empty() &&
        ((m_waypoints[0]->flags & CHECKPOINT_FLAG_SLOW) || m_waypoints[0]->type == CHECKPOINT_TYPE_LADDER))
        return true;
    return m_waypoints.size() >= 2 && m_waypoints[0]->type == CHECKPOINT_TYPE_DOOR &&
           m_waypoints[1]->type == CHECKPOINT_TYPE_DOOR;
}

bool C_AIController::IsMarchBlocked()
{
    if (MustSlowDown())
        return true;
    if (!m_alert->Is1() || !(m_moveFlags & MOVE_STANDING))
        return true;
    if (m_weaponRaised && m_handsItem)
        return true;
    return m_axes[AXIS_RIGHT] != 0.0f || m_axes[AXIS_LEFT] != 0.0f || m_axes[AXIS_BACK] != 0.0f;
}

void C_AIController::UpdateMoveStance()
{
    // Near the special points and the others (not the squad): upright, the movement flags
    // put aside until it is clear again (enemies go back to flag 2).
    if (m_type == HUMAN_CONTROLLER_SQUAD)
        return;
    S_HumanBody &body = m_human->m_body;
    S_AIMoveOrder *move = m_move;
    const bool clear = IsOnPlainPoints() && IsAwayFromOthers();
    if (m_type == HUMAN_CONTROLLER_ENEMY || m_type == HUMAN_CONTROLLER_SNIPER) {
        if (clear) {
            if (!(move->flags & S_AIMoveOrder::FLAG_STANCE_SAVED))
                return;
            body.ClearMovementFlags(4);
            body.SetMovementFlags(2);
            move->flags &= ~S_AIMoveOrder::FLAG_STANCE_SAVED;
            return;
        }
        if (move->flags & S_AIMoveOrder::FLAG_STANCE_SAVED)
            return;
    } else {
        if (clear) {
            if (!(move->flags & S_AIMoveOrder::FLAG_STANCE_SAVED) || move->unknown30 == -1)
                return;
            body.ClearMovementFlags(6);
            body.SetMovementFlags(static_cast<uint32_t>(move->unknown30) & 6);
            move->unknown30 = -1;
            move->flags &= ~S_AIMoveOrder::FLAG_STANCE_SAVED;
            return;
        }
        if (move->flags & S_AIMoveOrder::FLAG_STANCE_SAVED)
            return;
        if (move->unknown30 != -1)
            return;
        move->unknown30 = static_cast<int>(body.GetMovementStance());
    }
    body.ClearMovementFlags(6);
    move->flags |= S_AIMoveOrder::FLAG_STANCE_SAVED;
}

bool C_AIController::IsOnPlainPoints()
{
    // Neither the human's point nor the next waypoint's is of types 1..6.
    // (Fix: the original reads the first waypoint of an empty route.)
    if (!m_currentCheckpoint || m_waypoints.empty() || !m_waypoints[0])
        return false;
    const auto plain = [](uint8_t type) { return type == 0 || type > 6; };
    return plain(m_currentCheckpoint->type) && plain(m_waypoints[0]->type);
}

bool C_AIController::IsAwayFromOthers()
{
    return m_events->IsAwayFromNearbyActors(m_human->GetFrame()->GetWorldPos());
}

// ------------------------------------------------------------------------- stepping aside

void C_AIController::UpdateAvoidance()
{
    S_AIMoveOrder *move = m_move;
    if (!(move->flags & S_AIMoveOrder::FLAG_AVOIDING) && HasAvoidance())
        move->flags |= S_AIMoveOrder::FLAG_AVOIDING;
    if (move->flags & S_AIMoveOrder::FLAG_AVOIDING) {
        if (HasAvoidance() && CanAvoid()) {
            WalkAvoiding();
            return;
        }
        move->flags &= ~S_AIMoveOrder::FLAG_AVOIDING;
    }
}

bool C_AIController::HasAvoidance()
{
    const float *step = m_human->m_aiMemory->step;
    return step[2] * step[2] + step[1] * step[1] + step[0] * step[0] > 0.01f;
}

bool C_AIController::CanAvoid()
{
    return !(m_moveFlags & MOVE_LYING) && !(m_move->flags & S_AIMoveOrder::FLAG_ROUTE) &&
           !(m_move->flags & S_AIMoveOrder::FLAG_BUSY);
}

void C_AIController::WalkAvoiding()
{
    m_axes[AXIS_12] = 0.0f;
    const float *step = m_human->m_aiMemory->step;
    m_walkSpeed = S_vector(step[0], step[1], step[2]).Magnitude();
    S_vector dir(step[0], 0.0f, step[2]);
    dir.Normalize();
    S_vector view(m_facing->view.x, 0.0f, m_facing->view.z);
    view.Normalize();
    const float dot = view.Dot(dir);
    const bool left = view.x * dir.z - view.z * dir.x > 0.0f;
    const float absDot = std::fabs(dot);
    int mode;
    if (absDot >= 0.382683f)                            // within 67.5 degrees of the view
        mode = absDot >= 0.92388f ? 1 : 3;              // within 22.5
    else
        mode = 2;
    SetWalkAxes(mode, dir, dot, left);
    SetWalkPace(mode == 2 && (m_moveFlags & MOVE_LYING));
}

// ------------------------------------------------------------------------- the end of a move

void C_AIController::EndMove(int result)
{
    // (The original reads the body's climb state here and drops it.)
    ReleaseWaypoints(m_waypoints);
    ReleaseWaypoint(m_waypoint);
    m_waypoint = nullptr;
    m_move->unknown4C = result;
    m_move->Finish();
}

// ------------------------------------------------------------------------- where the human is

bool C_AIController::IsHumanWithin(const S_vector *pos, float radius)
{
    const S_vector offset = m_human->GetFrame()->GetWorldPos() - *pos;
    return offset.Square() < radius * radius;
}

void C_AIController::UpdateCurrentCheckpoint()
{
    const S_vector &pos = m_human->GetFrame()->GetWorldPos();
    if (!m_currentCheckpoint) {
        m_currentCheckpoint = m_checkpoints->Get(m_checkpoints->FindNearest(pos, 5.0f, -1.0f));
        if (m_currentCheckpoint || !m_human->m_body.IsWorkState5())
            return;
        m_currentCheckpoint = m_checkpoints->Get(m_checkpoints->FindNearestType15(pos, 5.0f, -1.0f));
        return;
    }
    if (IsHumanWithin(&m_currentCheckpoint->pos, static_cast<float>(m_currentCheckpoint->radius) * 0.0625f))
        return;
    // Entering the next waypoint's point, or between it and the last one's: the nearer.
    if (!m_waypoints.empty()) {
        const S_AIWaypoint *next = m_waypoints[0];
        if (IsHumanWithin(&XYZ(next->pos2), next->radius))
            m_currentCheckpoint = m_checkpoints->Get(next->checkpoint);
        else if (m_waypoint && IsNearSegment(*m_waypoint, *next))
            m_currentCheckpoint = NearerPoint(*m_waypoint, *next);
        if (m_currentCheckpoint)
            return;
    }
    m_currentCheckpoint = m_checkpoints->Get(m_checkpoints->FindNearest(pos, 5.0f, -1.0f));
}

bool C_AIController::IsNearSegment(const S_AIWaypoint &a, const S_AIWaypoint &b)
{
    const S_vector &pos = m_human->GetFrame()->GetWorldPos();
    const S_vector segment = XYZ(b.pos2) - XYZ(a.pos2);
    const float t = (pos - XYZ(a.pos2)).Dot(segment) / segment.Square();
    if (!(t > 0.0f) || !(t < 1.0f))
        return false;
    const float radius = (b.radius - a.radius) * t + a.radius;
    return (segment * t + XYZ(a.pos2) - pos).Magnitude() < radius;
}

S_Checkpoint *C_AIController::NearerPoint(const S_AIWaypoint &a, const S_AIWaypoint &b)
{
    // (The squared distances less the radii.)
    const S_vector &pos = m_human->GetFrame()->GetWorldPos();
    const float toA = (XYZ(a.pos2) - pos).Square() - a.radius;
    const float toB = (XYZ(b.pos2) - pos).Square() - b.radius;
    return m_checkpoints->Get(toA < toB ? a.checkpoint : b.checkpoint);
}

bool C_AIController::AdjustToPathHeight(S_vector *pos)
{
    // On a route: the height along the segment walked.
    if ((m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && m_waypoint && !m_waypoints.empty()) {
        const S_vector4 &a = m_waypoint->pos;
        const S_vector4 &b = m_waypoints[0]->pos;
        const float sx = b.x - a.x;
        const float sz = b.z - a.z;
        float t = ((pos->x - a.x) * sx + (pos->z - a.z) * sz) / (sx * sx + sz * sz);
        if (t >= 0.0f) {
            if (t > 1.0f)
                t = 1.0f;
        } else {
            t = 0.0f;
        }
        pos->y = (b.y - a.y) * t + a.y;
        return true;
    }
    // Else at a point: its height.
    if (!m_currentCheckpoint)
        return false;
    if (IsHumanWithin(&m_currentCheckpoint->pos, static_cast<float>(m_currentCheckpoint->radius) * 0.0625f))
        pos->y = m_currentCheckpoint->pos.y;
    return false;
}

bool C_AIController::Unknown5C(float *intensity, float *reference)
{
    if (m_moveFlags & MOVE_DEAD)
        return false;
    if (m_type == HUMAN_CONTROLLER_SQUAD && m_human->m_vehicle && m_human->m_vehicle->GetType() == ACTOR_CAR)
        return false;
    // Value set 0 lying, 1 standing or moving.
    int set;
    if (m_moveFlags & MOVE_LYING)
        set = 0;
    else if (m_moveFlags & MOVE_STANDING)
        set = 1;
    else
        set = (m_moveFlags & MOVE_PACE_MASK) != 0 ? 1 : 0;

    const S_Checkpoint *current = m_currentCheckpoint;
    if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE) {
        if (current && IsHumanWithin(&current->pos, static_cast<float>(current->radius) * 0.0625f))
            return current->GetValuePair(set, intensity, reference);
        // Between the last waypoint's point and the next one's: interpolated.
        const S_AIWaypoint *last = m_waypoint;
        if (last && last->checkpoint != -1 && !m_waypoints.empty() && m_waypoints[0]->checkpoint != -1) {
            const S_AIWaypoint *next = m_waypoints[0];
            const S_vector segment = XYZ(next->pos) - XYZ(last->pos);
            const S_vector offset = m_human->GetFrame()->GetWorldPos() - XYZ(last->pos);
            const float t = offset.Dot(segment) / segment.Square();
            if (t >= 0.0f) {
                if (t <= 1.0f) {
                    float lastIntensity;
                    float lastReference;
                    float nextIntensity;
                    float nextReference;
                    if (!m_checkpoints->Get(last->checkpoint)->GetValuePair(set, &lastIntensity, &lastReference))
                        return false;
                    if (!m_checkpoints->Get(next->checkpoint)->GetValuePair(set, &nextIntensity, &nextReference))
                        return false;
                    *intensity = (nextIntensity - lastIntensity) * t + lastIntensity;
                    *reference = (nextReference - lastReference) * t + lastReference;
                    return true;
                }
                return m_checkpoints->Get(next->checkpoint)->GetValuePair(set, intensity, reference);
            }
            return m_checkpoints->Get(last->checkpoint)->GetValuePair(set, intensity, reference);
        }
    }
    if (current && IsHumanWithin(&current->pos, static_cast<float>(current->radius) * 0.0625f))
        return current->GetValuePair(set, intensity, reference);
    return false;
}

// ------------------------------------------------------------------------- stepping round others

void C_AIController::AvoidOthersOnRoute(bool reached)
{
    const int count = m_events->GetNearbyActorCount();
    if (count == 0)
        return;
    std::vector<C_Human *> others;
    for (int i = count; i-- > 0;) {
        C_Actor *actor = m_events->GetNearbyActor(i);
        // Fix: the original takes the gone ones (null) and the other kinds of actor for
        // humans too.
        if (actor && actor->GetType() == ACTOR_HUMAN)
            others.push_back(static_cast<C_Human *>(actor));
    }
    if (FilterBlockers(others, m_human, m_waypoints, reached) != 0)
        InsertDetours(others);
}

void C_AIController::InsertDetours(std::vector<C_Human *> &blockers)
{
    // Along the route from the human: a blocker on the way to a waypoint gets a waypoint
    // beside it before that one (or, standing at the waypoint, the waypoint moves beside
    // it); the first blocker with no way round ends it.
    if (blockers.empty() || m_waypoints.empty())
        return;
    S_vector4 from = WorldPos4(m_human->GetFrame());
    std::vector<S_vector4> points;
    for (size_t k = 0; k < m_waypoints.size(); ++k) {
        const S_vector4 to = m_waypoints[k]->pos;
        const C_Human *blocker = FindBlocker(blockers, from, to);
        if (blocker && !IsNear(blocker, from)) {
            GetDetourPoints(points, blocker, from, to);
            bool found = false;
            for (size_t j = points.size(); j-- > 0;) {
                if (FindBlocker(blockers, from, points[j]))
                    continue;
                if (!IsNear(blocker, to))
                    m_waypoints.insert(m_waypoints.begin() + static_cast<std::ptrdiff_t>(k),
                                       AllocWaypoint(-1, points[j], 0, 0, 0.0f));
                else
                    m_waypoints[k]->pos = points[j];
                found = true;
                break;
            }
            if (!found)
                return;
        }
        from = m_waypoints[k]->pos;
    }
}

bool C_AIController::IsNearSelectedSoldier() const
{
    const C_Human *selected = g_teamManager.GetPlayerTeam()->GetSelectedMember();
    if (!selected)
        return false;
    const S_vector &a = selected->GetFrame()->GetWorldPos();
    const S_vector &b = m_human->GetFrame()->GetWorldPos();
    const float x = b.x - a.x;
    const float y = b.y - a.y;
    const float z = b.z - a.z;
    return y * y + x * x + z * z < 9.0f;
}

void C_AIController::ClearRoute()
{
    ReleaseWaypoints(m_waypoints);
    ReleaseWaypoint(m_waypoint);
    m_waypoint = nullptr;
}

void C_AIController::SkipPassedWaypoint()
{
    if (m_waypoints.size() < 2)
        return;
    const S_matrix &m = m_human->GetFrame()->GetMatrix();
    const float x = m.m[3][0];
    const float z = m.m[3][2];
    S_vector4 &first = m_waypoints[0]->pos;
    const S_vector4 &second = m_waypoints[1]->pos;
    if ((second.x - x) * (first.x - x) + (second.z - z) * (first.z - z) < 0.0f)
        first = {x, m.m[3][1], z, m.m[3][3]};
}

// The last actor within 0.8 m of `pos`, null if none.
C_Actor *FindActorNear(const S_vector &pos, const std::vector<C_Actor *> &actors)   // 0x00581440 (DS 0x00586e10)
{
    for (size_t i = actors.size(); i-- > 0;) {
        const S_vector &at = actors[i]->GetFrame()->GetWorldPos();
        const float x = at.x - pos.x;
        const float y = at.y - pos.y;
        const float z = at.z - pos.z;
        if (y * y + x * x + z * z < 0.64f)
            return actors[i];
    }
    return nullptr;
}

// `pos` is beside the stretch from `a` to `b`, within the radius interpolated between theirs.
bool IsPosNearSegment(const S_vector &pos, const S_AIWaypoint &a, const S_AIWaypoint &b)   // 0x00580120 (DS 0x005855e0)
{
    const float dx = b.pos2.x - a.pos2.x;
    const float dy = b.pos2.y - a.pos2.y;
    const float dz = b.pos2.z - a.pos2.z;
    const float t = ((pos.x - a.pos2.x) * dx + (pos.y - a.pos2.y) * dy + (pos.z - a.pos2.z) * dz)
                    / (dx * dx + dy * dy + dz * dz);
    if (!(0.0f < t && t < 1.0f))
        return false;
    const S_vector offset((dx * t + a.pos2.x) - pos.x, (dy * t + a.pos2.y) - pos.y, (dz * t + a.pos2.z) - pos.z);
    return offset.Magnitude() < (b.radius - a.radius) * t + a.radius;
}

void C_AIController::SetRoutePoints(const std::vector<uint16_t> &path)
{
    ClearRoute();
    for (const uint16_t index : path) {
        const S_Checkpoint *point = m_checkpoints->Get(index);
        const S_vector4 pos = {point->pos.x, point->pos.y, point->pos.z, 0.0f};
        m_waypoints.push_back(AllocWaypoint(index, pos, point->flags & 0xFB40, point->type,
                                            static_cast<float>(point->radius) * 0.0625f));
    }
}
