// The simulation of the ragdoll (original source 0x007c7c40..0x007ce4d0, see
// human_pose_ragdoll.cpp for the parts and points): each update runs 3..6 substeps by the
// body's detail. A substep applies the pushes and holds, moves the points (Verlet: the last
// move, gravity and the forces; bouncing off the ground they lie on), measures the parts again,
// corrects the points towards the parts' lengths, the other parts and the joint limits (and the
// twist of the chest against the pelvis, the knees and elbows folding back), and every other
// substep collides the points and parts with the world. The body settles (its weight runs down)
// once it lies on the ground without moving.
#include "game/human_pose_internal.h"

#include <cmath>

#include "game/actor.h"
#include "game/collision.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/sound_module.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// What the collision queries of the ragdoll test (C_CollisionWorld type and flag masks to skip).
const uint32_t RAGDOLL_TEST_TYPES = 0xCC002;
const uint32_t RAGDOLL_TEST_FLAGS = 0x230000;
// The same without skipping water (the pelvis tests for it first).
const uint32_t RAGDOLL_TEST_FLAGS_WATER = 0x220000;

// Pushes into cars are capped at this impulse.
const float RAGDOLL_CAR_IMPULSE = 800.0f;

// The sounds of the body (sound table chapter / group): hitting the ground (at the pelvis) and
// a second one at the neck (skipped on odd updates).
const int RAGDOLL_IMPACT_CHAPTER = 0;
const int RAGDOLL_IMPACT_GROUP = 0x2C;
const int RAGDOLL_DROP_CHAPTER = 1;
const int RAGDOLL_DROP_GROUP = 0x37;

// The ground under the body averaged over one settle check (the original's file statics).
int g_ragdollContactCount = 0;                          // 0x0098af84
float g_ragdollContactNormalY = 0.0f;                   // 0x0098af88

void ClearXYZ(S_vector4 &v)
{
    v.x = 0.0f;
    v.y = 0.0f;
    v.z = 0.0f;
}

void AddXYZ(S_vector4 &v, const S_vector &d)
{
    v.x += d.x;
    v.y += d.y;
    v.z += d.z;
}

// The slot of the correction `body` makes to `point` (null if the body is not at the point).
S_vector4 *CorrectionOf(S_RagdollPoint *point, const S_RagdollBody *body)
{
    for (int i = 0; i < 4; ++i)
        if (point->bodies[i] == body)
            return &point->corrections[i];
    return nullptr;
}

// A car the ragdoll hit is pushed if this machine simulates it.
C_Car *PushableCar(C_Actor *actor)
{
    if (!actor || actor->GetType() != ACTOR_CAR)
        return nullptr;
    C_Car *car = static_cast<C_Car *>(actor);
    return car->IsSimulatedHere() ? car : nullptr;
}

// 0x007ce420: how much of the speed into the ground a material gives back.
float RagdollRestitution(int material)
{
    switch (material) {
    case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x14: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x55:
    case 0x56: case 0x57: case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E:
    case 0x5F:
        return 0.5f;
    case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D:
    case 0x1E: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D:
    case 0x2E: case 0x2F: case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36:
    case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
    case 0x40: case 0x41:
        return 0.25f;
    case 0x1F: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x49: case 0x4A:
    case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54:
        return 0.75f;
    default:
        return 1.0f;
    }
}

// 0x007ce4d0: the closest points of the segments `p0` + `d0` and `p1` + `d1` (on them at `t0` /
// `t1`) and their distance; unset (NaN) for parallel segments.
// Quirk: the second segment is taken off the first one's direction without normalising it.
float SegmentClosestPoints(const S_vector &p0, const S_vector &d0, const S_vector &p1, const S_vector &d1,
                           S_vector &closest0, S_vector &closest1, float &t0, float &t1)
{
    S_vector a = p1 - p0;
    S_vector b = (p1 + d1) - p0;
    a -= d0 * a.Dot(d0);
    b -= d0 * b.Dot(d0);
    const S_vector c = b - a;
    const float sq = c.Square();
    if (sq < 1e-8f)
        return PoseUnset();
    const float inv = 1.0f / PoseMagnitude(sq);
    const S_vector n = c * inv;
    t1 = -(n.Dot(a) * inv);
    if (PoseIsNegative(t1))
        closest1 = p1;
    else if (t1 <= 1.0f)
        closest1 = p1 + d1 * t1;
    else
        closest1 = p1 + d1;
    if (d0.Square() < 1e-8f)
        return PoseUnset();
    const float invLength = 1.0f / d0.Magnitude();
    const S_vector dir = d0 * invLength;
    t0 = (closest1 - p0).Dot(dir) * invLength;
    if (PoseIsNegative(t0))
        closest0 = p0;
    else if (t0 <= 1.0f)
        closest0 = p0 + d0 * t0;
    else
        closest0 = p0 + d0;
    return (closest1 - closest0).Magnitude();
}

// 0x007ca7f0: the corrections keeping `body` at its length (unset if it is close enough).
void RagdollLengthCorrection(const S_RagdollBody *body, S_vector4 &start, S_vector4 &end)
{
    const float stretch = body->currentLength - body->length;
    if (std::fabs(stretch) < 0.001f)
        return;
    const S_vector axis = PoseXYZ(body->axis);
    PoseSetXYZ(start, axis * (stretch * 0.5f));
    PoseSetXYZ(end, axis * ((body->length - body->currentLength) * 0.5f));
}

// One axis of the joint limits of 0x007ca8d0: `value` held at `limit`, the other two scaled to
// stay on the unit sphere (by the limit's sine).
void ClampLimitAxis(float &value, float &a, float &b, float limit, float sine)
{
    const float s = std::sqrt(1.0f - value * value);
    // (Not above the limit, as the original's x87 test: a value past 1 after an earlier axis's scaling gives a NaN
    // here, which takes this branch rather than spreading.)
    if (!(s > 1e-8f)) {
        b = 0.0f;
        a = sine;
    } else {
        const float k = sine / s;
        b *= k;
        a *= k;
    }
    value = limit;
}

// 0x007ca8d0: the corrections keeping `body` within its joint limits against its parent (the
// end point takes most of it and is marked with the limits it hit).
void RagdollLimitCorrection(const S_RagdollBody *body, S_vector4 &start, S_vector4 &end)
{
    const S_RagdollBody *parent = body->parent;
    if (!parent)
        return;
    S_vector local = PoseIntoAxes(parent->world, PoseXYZ(body->axis));
    for (float *c : {&local.x, &local.y, &local.z}) {
        if (*c > 1.0f)
            *c = 1.0f;
        else if (*c < -1.0f)
            *c = -1.0f;
    }
    uint8_t hit = 0;
    if (!PoseIsUnset(body->limitMin.z) && local.z < body->limitMin.z) {
        ClampLimitAxis(local.z, local.x, local.y, body->limitMin.z, body->limitMinSin.z);
        hit = RAGDOLL_POINT_MIN_Z;
    }
    if (!PoseIsUnset(body->limitMax.z) && body->limitMax.z < local.z) {
        ClampLimitAxis(local.z, local.x, local.y, body->limitMax.z, body->limitMaxSin.z);
        hit |= RAGDOLL_POINT_MAX_Z;
    }
    if (!PoseIsUnset(body->limitMin.x) && local.x < body->limitMin.x) {
        ClampLimitAxis(local.x, local.y, local.z, body->limitMin.x, body->limitMinSin.x);
        hit |= RAGDOLL_POINT_MIN_X;
    }
    if (!PoseIsUnset(body->limitMax.x) && body->limitMax.x < local.x) {
        ClampLimitAxis(local.x, local.y, local.z, body->limitMax.x, body->limitMaxSin.x);
        hit |= RAGDOLL_POINT_MAX_X;
    }
    if (!PoseIsUnset(body->limitMin.y) && local.y < body->limitMin.y) {
        ClampLimitAxis(local.y, local.x, local.z, body->limitMin.y, body->limitMinSin.y);
        hit |= RAGDOLL_POINT_MIN_Y;
    }
    if (!PoseIsUnset(body->limitMax.y) && body->limitMax.y < local.y) {
        ClampLimitAxis(local.y, local.x, local.z, body->limitMax.y, body->limitMaxSin.y);
        hit |= RAGDOLL_POINT_MAX_Y;
    }
    const S_vector clamped = local.RotateByMatrix(parent->world);
    const S_vector diff = clamped * body->length - PoseXYZ(body->axis) * body->currentLength;
    if (hit) {
        PoseSetXYZ(end, diff * 0.8f);
        PoseSetXYZ(start, diff * -0.2f);
        body->end->flags |= hit;
    }
}

// 0x007cba30: the corrections pushing `self` (from `start` to `end`) out of `other` and the
// parts beyond it (not back through `from`), averaged; the points of `self` touching a part
// first in this step take the push's direction as their normal.
void RagdollPartCollisionWith(S_RagdollBody *other, const S_RagdollBody *from, S_RagdollBody *self,
                              const S_vector4 &start, const S_vector4 &end, S_vector4 &startOut, S_vector4 &endOut)
{
    const S_vector otherStart = PoseXYZ(other->start->pos);
    const S_vector otherDir = PoseXYZ(other->end->pos) - otherStart;
    const S_vector selfDir = PoseXYZ(end) - PoseXYZ(start);
    S_vector onOther;
    S_vector onSelf;
    float t0;
    float t1;
    const float distance =
        SegmentClosestPoints(otherStart, otherDir, PoseXYZ(start), selfDir, onOther, onSelf, t0, t1);
    if (!PoseIsUnset(distance)) {
        const float reach = (other->thickness + self->thickness) * 0.5f;
        if (distance < reach && distance > 1e-8f) {
            const S_vector push = (onSelf - onOther) * (reach / distance);
            for (S_vector4 *out : {&startOut, &endOut}) {
                if (PoseIsUnset(out->x))
                    PoseSetXYZ(*out, push);
                else
                    PoseSetXYZ(*out, PoseXYZ(*out) * 0.5f + push * 0.5f);
            }
            const float inv = 1.0f / reach;
            for (S_RagdollPoint *point : {self->start, self->end}) {
                if (point->flags & RAGDOLL_POINT_TOUCHING)
                    continue;
                point->flags |= RAGDOLL_POINT_TOUCHING;
                PoseSetXYZ(point->normal, push * inv);
            }
        }
    }
    S_RagdollBody *next[4] = {other->parent, other->children[0], other->children[1], other->children[2]};
    for (S_RagdollBody *n : next)
        if (n && n != from)
            RagdollPartCollisionWith(n, other, self, start, end, startOut, endOut);
}

// 0x007cb970: the corrections pushing `body` out of the other parts (all but its parent).
void RagdollPartCollision(S_RagdollBody *body, const S_vector4 &start, const S_vector4 &end, S_vector4 &startOut,
                          S_vector4 &endOut)
{
    S_RagdollBody *parent = body->parent;
    if (!parent)
        return;
    S_RagdollBody *next[4] = {parent->parent, parent->children[0], parent->children[1], parent->children[2]};
    for (S_RagdollBody *n : next)
        if (n && n != body)
            RagdollPartCollisionWith(n, parent, body, start, end, startOut, endOut);
}

// The frame and actor of a dynamic volume the ragdoll touches.
bool IsDynamic(const S_CollisionVolume *volume)
{
    return (volume->flags & COLLISION_FLAG_DYNAMIC) && (volume->flags & (COLLISION_FLAG_DYNAMIC | COLLISION_FLAG_PRIMITIVE));
}

}  // namespace

// ---- a step -------------------------------------------------------------------------------

// 0x007c7c40: `time` ms of the ragdoll in substeps (fewer the less of the body is seen; the
// weights scaled to the new count).
void C_HumanWeaponPose::StepRagdoll(int time)
{
    const int lastSteps = m_steps;
    m_steps = 6;
    const uint32_t detail = m_movement->GetFlags();
    if (detail & MOVE_FLAG_LARGE)
        m_steps = 6;
    if (detail & MOVE_FLAG_FULL_DETAIL)
        m_steps = 5;
    if (detail & MOVE_FLAG_SMALL)
        m_steps = 5;
    if (detail & MOVE_FLAG_TINY)
        m_steps = 4;
    if (detail & MOVE_FLAG_HIDDEN)
        m_steps = 4;
    if (detail & MOVE_FLAG_NOT_DRAWN)
        m_steps = 3;
    const float ratio = static_cast<float>(lastSteps) / static_cast<float>(m_steps);
    m_ragdollWeight = 1.0f - (1.0f - m_ragdollWeight) * ratio;
    for (S_RagdollPoint *point : m_points)
        if (point)
            point->weight = 1.0f - (1.0f - point->weight) * ratio;
    if (!(m_ragdollWeight > 1e-8f))
        return;
    m_step = 0;
    m_stepTime = static_cast<float>(time) / static_cast<float>(m_steps * 1000);
    for (; m_step < m_steps; ++m_step) {
        // The collisions every other substep and the last one.
        m_collideStep = (m_step & 1) != 0 || m_step == m_steps - 1;
        ApplyRagdollPushes(m_stepTime);
        IntegrateRagdollPoints(m_rootPoint, m_stepTime);
        UpdateRagdollBodies(m_rootBody, false, true);
        ConstrainRagdollBody(m_rootBody, m_stepTime);
        ApplyRagdollCorrections();
        if (m_collideStep) {
            CollideRagdoll(m_stepTime);
            UpdateRagdollBodies(m_rootBody, true, true);
        }
        SettleRagdoll(m_rootPoint);
        if (!m_collideStep)
            m_sinceCollision += m_stepTime;
        else
            m_sinceCollision = 0.0f;
    }
}

// 0x007c7e50: moves `point` and the points below it by Verlet integration: the last move, the
// gravity and the forces (bouncing off the ground a point lies on by its material, and damped
// in water and by the points lying on the ground), scaled by the ragdoll's weight.
void C_HumanWeaponPose::IntegrateRagdollPoints(S_RagdollPoint *point, float dt)
{
    for (; point; point = point->children[2]) {
        const S_vector4 before = point->pos;
        const float inv = 1.0f / dt;
        S_vector velocity = (PoseXYZ(point->pos) - PoseXYZ(point->oldPos)) * inv;
        if (point->flags & (RAGDOLL_POINT_CONTACT | RAGDOLL_POINT_TOUCHING)) {
            const S_vector normal = PoseXYZ(point->normal);
            const float into = normal.Dot(velocity);
            if (PoseIsNegative(into)) {
                velocity -= normal * (into + into);
                if (normal.y > 0.3f) {
                    const S_vector along = normal * normal.Dot(velocity);
                    const S_vector across = velocity - along;
                    if (into <= -0.01f)
                        velocity = along * RagdollRestitution(point->material) + across;
                    else
                        velocity = across * 0.75f + along * RagdollRestitution(point->material);
                }
            }
        }
        if (m_inWater)
            velocity *= 0.9f;
        PoseSetXYZ(point->velocity, velocity);
        const S_vector gravityStep = (PoseXYZ(point->force) + PoseXYZ(m_gravity)) * dt;
        PoseSetXYZ(point->gravityStep, gravityStep);
        S_vector step = velocity + gravityStep;
        step *= m_ragdollWeight;
        if (step.y > 20.0f)
            step.y = 20.0f;
        if (m_groundedPoints)
            step *= 1.0f - static_cast<float>(m_groundedPoints) * 0.0625f * 0.6f;
        PoseSetXYZ(point->step, step);
        AddXYZ(point->pos, step * dt);
        point->oldPos = before;
        ClearXYZ(point->force);
        if (point->children[0])
            IntegrateRagdollPoints(point->children[0], dt);
        if (point->children[1])
            IntegrateRagdollPoints(point->children[1], dt);
    }
}

// 0x007c8540: `body` and the parts below it measured again from their points (`rebuild`: the
// axes along them, the roll kept against the parent; the pelvis and the chest also turned to
// their hips / shoulders), and kept as the last pose (`save`).
void C_HumanWeaponPose::UpdateRagdollBodies(S_RagdollBody *body, bool save, bool rebuild)
{
    for (; body; body = body->children[2]) {
        if (rebuild) {
            const S_vector4 start = body->start->pos;
            S_vector axis = PoseXYZ(body->end->pos) - PoseXYZ(start);
            const float length = PoseMagnitude(axis.Square());
            body->currentLength = length;
            if (length > 1e-8f)
                axis *= 1.0f / length;
            PoseSetXYZ(body->axis, axis);
            if (!body->parent) {
                PoseTurnZTo(body->world, axis);
                PoseOrthonormalize(body->world);
            } else {
                const S_vector local = PoseIntoAxes(body->parent->world, axis);
                switch (body->index) {
                case RAGDOLL_LEFT_FOREARM:
                case RAGDOLL_RIGHT_FOREARM:
                case RAGDOLL_LEFT_SHIN:
                case RAGDOLL_RIGHT_SHIN:
                    body->world = PoseFrameFromZX(local, body->roll);
                    break;
                case RAGDOLL_LEFT_THIGH:
                case RAGDOLL_RIGHT_THIGH:
                    body->world = PoseFrameFromZX(local, body->roll + 3.1415927f);
                    break;
                default:
                    body->world = PoseFrameFromZ(local, body->roll);
                    break;
                }
                body->world *= body->parent->world;
            }
            body->world.m[3][0] = start.x;
            body->world.m[3][1] = start.y;
            body->world.m[3][2] = start.z;
            body->world.m[3][3] = 1.0f;
            int left = -1;
            int right = -1;
            if (body->index == RAGDOLL_PELVIS) {
                left = RAGDOLL_POINT_LEFT_HIP;
                right = RAGDOLL_POINT_RIGHT_HIP;
            } else if (body->index == RAGDOLL_CHEST) {
                left = RAGDOLL_POINT_LEFT_SHOULDER;
                right = RAGDOLL_POINT_RIGHT_SHOULDER;
            }
            if (left >= 0) {
                // The line between the hips / shoulders across the axis, turned by the part's
                // twist, becomes the x axis.
                S_vector a = PoseXYZ(m_points[left]->pos) - PoseXYZ(start);
                a -= axis * a.Dot(axis);
                S_vector b = PoseXYZ(m_points[right]->pos) - PoseXYZ(start);
                b -= axis * b.Dot(axis);
                S_vector across = b - a;
                const float acrossLength = PoseMagnitude(across.Square());
                if (acrossLength > 1e-8f) {
                    across *= 1.0f / acrossLength;
                    S_vector x = PoseIntoAxes(body->world, across).RotateByMatrix(body->lateral);
                    x = x.RotateByMatrix(body->world);
                    S_matrix &w = body->world;
                    w.m[0][0] = x.x;
                    w.m[0][1] = x.y;
                    w.m[0][2] = x.z;
                    w(1) = w(2).Cross(w(0));
                    w.m[2][3] = 0.0f;
                    w.m[1][3] = 0.0f;
                    w.m[0][3] = 0.0f;
                }
            }
        }
        if (save) {
            body->savedStart = body->start->pos;
            body->savedEnd = body->end->pos;
            body->savedLength = body->currentLength;
            body->savedAxis = body->axis;
        }
        if (body->children[0])
            UpdateRagdollBodies(body->children[0], save, rebuild);
        if (body->children[1])
            UpdateRagdollBodies(body->children[1], save, rebuild);
    }
}

// 0x007c8f70: the corrections `body` and the parts below it ask of their points: the length,
// the other parts (half), the joint limits (three quarters); the pelvis pins the hips, the chest
// pins the shoulders and keeps its twist against the pelvis within 30 degrees (turning the
// pelvis back the other way), the arms keep the neck at the shoulders, the thighs keep the
// pelvis at the hips, and knees and elbows folded up above their parent are pushed back.
// (`dt` is not used.)
void C_HumanWeaponPose::ConstrainRagdollBody(S_RagdollBody *body, float dt)
{
    S_RagdollPoint *start = body->start;
    S_RagdollPoint *end = body->end;
    S_vector4 *startCorrection = CorrectionOf(start, body);
    if (!startCorrection)
        return;
    S_vector4 *endCorrection = CorrectionOf(end, body);
    if (!endCorrection)
        return;
    ClearXYZ(*startCorrection);
    ClearXYZ(*endCorrection);
    if (!body->parent || body->parent->end != start)
        start->flags &= RAGDOLL_POINT_CONTACT;
    end->flags &= RAGDOLL_POINT_CONTACT;

    S_vector4 startOut;
    S_vector4 endOut;
    startOut.x = PoseUnset();
    endOut.x = PoseUnset();
    RagdollLengthCorrection(body, startOut, endOut);
    if (!PoseIsUnset(startOut.x))
        PoseSetXYZ(*startCorrection, PoseXYZ(startOut));
    if (!PoseIsUnset(endOut.x))
        PoseSetXYZ(*endCorrection, PoseXYZ(endOut));

    startOut.x = PoseUnset();
    endOut.x = PoseUnset();
    RagdollPartCollision(body, start->pos, end->pos, startOut, endOut);
    if (!PoseIsUnset(startOut.x))
        PoseSetXYZ(*startCorrection, PoseXYZ(*startCorrection) * 0.5f + PoseXYZ(startOut) * 0.5f);
    if (!PoseIsUnset(endOut.x))
        PoseSetXYZ(*endCorrection, PoseXYZ(*endCorrection) * 0.5f + PoseXYZ(endOut) * 0.5f);

    startOut.x = PoseUnset();
    endOut.x = PoseUnset();
    RagdollLimitCorrection(body, startOut, endOut);
    if (!PoseIsUnset(startOut.x))
        PoseSetXYZ(*startCorrection, PoseXYZ(*startCorrection) * 0.25f + PoseXYZ(startOut) * 0.75f);
    if (!PoseIsUnset(endOut.x))
        PoseSetXYZ(*endCorrection, PoseXYZ(*endCorrection) * 0.25f + PoseXYZ(endOut) * 0.75f);

    // The point `point` held at its place in the axes of `axes` (its parent-local position).
    auto pinned = [](const S_RagdollPoint *point, const S_matrix &axes) {
        return PoseXYZ(point->parentLocalPos) * axes;
    };
    const S_vector axis = PoseXYZ(body->axis);

    switch (body->index) {
    case RAGDOLL_PELVIS: {
        S_RagdollPoint *left = m_points[RAGDOLL_POINT_LEFT_HIP];
        S_RagdollPoint *right = m_points[RAGDOLL_POINT_RIGHT_HIP];
        S_vector4 *leftCorrection = CorrectionOf(left, body);
        if (!leftCorrection)
            leftCorrection = startCorrection;
        S_vector4 *rightCorrection = CorrectionOf(right, body);
        if (!rightCorrection)
            rightCorrection = endCorrection;
        ClearXYZ(*leftCorrection);
        ClearXYZ(*rightCorrection);
        PoseSetXYZ(*leftCorrection, pinned(left, body->world) - PoseXYZ(left->pos));
        PoseSetXYZ(*rightCorrection, pinned(right, body->world) - PoseXYZ(right->pos));
        break;
    }
    case RAGDOLL_CHEST: {
        S_RagdollPoint *left = m_points[RAGDOLL_POINT_LEFT_SHOULDER];
        S_RagdollPoint *right = m_points[RAGDOLL_POINT_RIGHT_SHOULDER];
        S_vector4 *leftCorrection = CorrectionOf(left, body);
        if (!leftCorrection)
            leftCorrection = startCorrection;
        S_vector4 *rightCorrection = CorrectionOf(right, body);
        if (!rightCorrection)
            rightCorrection = endCorrection;
        ClearXYZ(*leftCorrection);
        ClearXYZ(*rightCorrection);
        const S_vector leftTarget = pinned(left, body->world);
        PoseSetXYZ(*leftCorrection, leftTarget - PoseXYZ(left->pos));
        const S_vector rightTarget = pinned(right, body->world);
        PoseSetXYZ(*rightCorrection, rightTarget - PoseXYZ(right->pos));
        S_RagdollBody *pelvis = body->parent;
        S_vector local = PoseIntoAxes(pelvis->world, body->world(0));
        float across = local.x * local.x + local.y * local.y;
        if (!(1e-8f < across))
            break;
        across = std::sqrt(across);
        if (!(local.x / across < 0.866f))
            break;
        // Twisted more than 30 degrees: the chest's x turned back to 30 degrees...
        local.y = (PoseIsNegative(local.y) ? -0.5f : 0.5f) * across;
        local.x = across * 0.866f;
        S_vector x = local.RotateByMatrix(pelvis->world);
        x -= body->world(2) * x.Dot(body->world(2));
        x.Normalize();
        S_matrix turned = body->world;
        turned(0) = x;
        turned(1) = turned(2).Cross(x);
        turned.m[2][3] = 0.0f;
        turned.m[1][3] = 0.0f;
        turned.m[0][3] = 0.0f;
        const S_vector leftTurned = pinned(left, turned);
        const S_vector rightTurned = pinned(right, turned);
        PoseSetXYZ(*leftCorrection, PoseXYZ(*leftCorrection) * 0.65f + (leftTurned - leftTarget) * 0.35f);
        PoseSetXYZ(*rightCorrection, PoseXYZ(*rightCorrection) * 0.65f + (rightTurned - rightTarget) * 0.35f);
        left->flags |= RAGDOLL_POINT_LIMITS;
        right->flags |= RAGDOLL_POINT_LIMITS;
        // ... and the pelvis turned back the other way.
        S_RagdollPoint *leftHip = m_points[RAGDOLL_POINT_LEFT_HIP];
        S_RagdollPoint *rightHip = m_points[RAGDOLL_POINT_RIGHT_HIP];
        S_vector4 *leftHipCorrection = CorrectionOf(leftHip, pelvis);
        if (!leftHipCorrection)
            return;
        S_vector4 *rightHipCorrection = CorrectionOf(rightHip, pelvis);
        if (!rightHipCorrection)
            return;
        S_vector mirrored = PoseIntoAxes(body->world, x);
        mirrored.y = -mirrored.y;
        mirrored = mirrored.RotateByMatrix(pelvis->world);
        S_matrix back = pelvis->world;
        back(0) = mirrored;
        back(1) = back(2).Cross(mirrored);
        back.m[2][3] = 0.0f;
        back.m[1][3] = 0.0f;
        back.m[0][3] = 0.0f;
        const S_vector leftHipTarget = pinned(leftHip, pelvis->world);
        const S_vector rightHipTarget = pinned(rightHip, pelvis->world);
        const S_vector leftHipTurned = pinned(leftHip, back);
        const S_vector rightHipTurned = pinned(rightHip, back);
        PoseSetXYZ(*leftHipCorrection,
                   PoseXYZ(*leftHipCorrection) * 0.65f + (leftHipTurned - leftHipTarget) * 0.35f);
        PoseSetXYZ(*rightHipCorrection,
                   PoseXYZ(*rightHipCorrection) * 0.65f + (rightHipTurned - rightHipTarget) * 0.35f);
        leftHip->flags |= RAGDOLL_POINT_LIMITS;
        rightHip->flags |= RAGDOLL_POINT_LIMITS;
        break;
    }
    case RAGDOLL_LEFT_UPPER_ARM:
    case RAGDOLL_RIGHT_UPPER_ARM: {
        // The neck moves by how far the shoulder is off its place on the chest.
        S_vector4 *neckCorrection = CorrectionOf(m_points[RAGDOLL_POINT_NECK], body);
        if (!neckCorrection)
            neckCorrection = startCorrection;
        ClearXYZ(*neckCorrection);
        PoseSetXYZ(*neckCorrection, PoseXYZ(start->pos) - pinned(start, m_bodies[RAGDOLL_CHEST]->world));
        break;
    }
    case RAGDOLL_LEFT_THIGH:
    case RAGDOLL_RIGHT_THIGH: {
        // The pelvis moves by how far the hip is off its place on it...
        S_vector4 *pelvisCorrection = CorrectionOf(m_points[RAGDOLL_POINT_PELVIS], body);
        if (!pelvisCorrection)
            pelvisCorrection = startCorrection;
        ClearXYZ(*pelvisCorrection);
        PoseSetXYZ(*pelvisCorrection, PoseXYZ(start->pos) - pinned(start, m_bodies[RAGDOLL_PELVIS]->world));
        // ... and a knee above the hip with the foot above it is pushed back down.
        if (end->pos.y <= start->pos.y || body->children[0]->end->pos.y <= end->pos.y ||
            (end->flags & RAGDOLL_POINT_CONTACT) || !(start->flags & RAGDOLL_POINT_CONTACT))
            break;
        const S_vector bend = axis.Cross(PoseXYZ(body->parent->axis).Cross(axis));
        AddXYZ(*endCorrection, bend * -0.1f);
        break;
    }
    case RAGDOLL_LEFT_FOREARM:
    case RAGDOLL_RIGHT_FOREARM:
    case RAGDOLL_LEFT_SHIN:
    case RAGDOLL_RIGHT_SHIN: {
        // A hand / foot folded up above the shoulder / hip is pushed back.
        if (end->pos.y <= start->pos.y)
            break;
        const S_RagdollBody *parent = body->parent;
        if (end->pos.y <= parent->start->pos.y || (end->flags & RAGDOLL_POINT_CONTACT) ||
            !(parent->start->flags & RAGDOLL_POINT_CONTACT))
            break;
        S_vector bend;
        if (!(start->flags & RAGDOLL_POINT_CONTACT))
            bend = axis.Cross(PoseXYZ(parent->parent->axis).Cross(axis)) * -0.15f;
        else
            bend = axis.Cross(PoseXYZ(parent->axis).Cross(axis)) * 0.15f;
        AddXYZ(*endCorrection, bend);
        break;
    }
    default:
        break;
    }
    for (S_RagdollBody *child : body->children)
        if (child)
            ConstrainRagdollBody(child, dt);
}

// 0x007cbf40: every point moved by the corrections of its parts, weighted by their masses (on
// collision steps not into the ground it lies on).
void C_HumanWeaponPose::ApplyRagdollCorrections()
{
    for (S_RagdollPoint *point : m_points) {
        if (!point)
            continue;
        float total = 0.0f;
        for (S_RagdollBody *body : point->bodies)
            if (body)
                total += body->mass;
        S_vector move(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < 4; ++i)
            if (point->bodies[i])
                move += PoseXYZ(point->corrections[i]) * (point->bodies[i]->mass / total);
        if (m_collideStep && (point->flags & (RAGDOLL_POINT_CONTACT | RAGDOLL_POINT_TOUCHING))) {
            const S_vector normal = PoseXYZ(point->normal);
            const float into = move.Dot(normal);
            if (PoseIsNegative(into))
                move -= normal * into;
        }
        AddXYZ(point->pos, move);
    }
}

// ---- collisions ---------------------------------------------------------------------------

// 0x007cc1b0: the points and then the parts against the world; the points' smoothed speeds.
void C_HumanWeaponPose::CollideRagdoll(float dt)
{
    for (S_RagdollPoint *point : m_points)
        if (point)
            point->flags &= ~RAGDOLL_POINT_CONTACT;
    for (S_RagdollPoint *point : m_points) {
        if (!point)
            continue;
        S_vector4 move = {0.0f, 0.0f, 0.0f, 0.0f};
        CollideRagdollPoint(point, move);
        AddXYZ(point->pos, PoseXYZ(move));
    }
    for (S_RagdollBody *body : m_bodies) {
        if (!body)
            continue;
        S_vector4 startMove = {0.0f, 0.0f, 0.0f, 0.0f};
        S_vector4 endMove = {0.0f, 0.0f, 0.0f, 0.0f};
        CollideRagdollBody(body, startMove, endMove);
        AddXYZ(body->start->pos, PoseXYZ(startMove));
        AddXYZ(body->end->pos, PoseXYZ(endMove));
    }
    for (S_RagdollPoint *point : m_points) {
        if (!point)
            continue;
        point->lastPos = point->pos;
        const float inv = 1.0f / dt;
        const S_vector speed = (PoseXYZ(point->pos) - PoseXYZ(point->oldPos)) * inv;
        PoseSetXYZ(point->smoothVelocity, (speed + PoseXYZ(point->smoothVelocity)) * 0.5f);
    }
}

// 0x007cc3f0: `point` swept from its last position against the world (as a sphere of its
// widest part's half thickness); `out` the move back to the contact. The pelvis first finds
// water (sinking slowly when it went in). Dynamic volumes are remembered as the contact (not
// the vehicle the body died in) and cars hit are pushed; a point that would end above its
// parent slides off steep ground. Hard landings play the impact sounds.
void C_HumanWeaponPose::CollideRagdollPoint(S_RagdollPoint *point, S_vector4 &out)
{
    float radius = 0.0f;
    for (int i = 0; i < 4; ++i) {
        S_RagdollBody *body = point->bodies[i];
        if (body && (body->start == point || body->end == point) && (i == 0 || radius < body->thickness))
            radius = body->thickness;
    }
    radius *= 0.5f;
    // (From where it was at the last collision step, +0x100, as the original: not the last integration step.)
    S_vector from = PoseXYZ(point->lastPos);
    S_vector move = PoseXYZ(point->pos) - from;
    S_vector hit;
    S_vector normal;
    S_CollisionVolume *volume = nullptr;
    bool tested = false;
    if (point->index == RAGDOLL_POINT_PELVIS) {
        volume = CollisionSphereTest(from, move, radius, &hit, &normal, RAGDOLL_TEST_TYPES, RAGDOLL_TEST_FLAGS_WATER);
        if (!volume)
            return;
        if (volume->flags & COLLISION_FLAG_WATER) {
            if (PoseIsNegative(normal.Dot(PoseXYZ(point->step)))) {
                m_inWater = true;
                m_gravity.y = -2.25f;
            } else {
                m_inWater = false;
                m_gravity.y = -9.0f;
            }
            m_gravity.x = 0.0f;
            m_gravity.z = 0.0f;
            from = hit;
            move = PoseXYZ(point->pos) - hit;
        } else {
            tested = true;
        }
    }
    if (!tested)
        volume = CollisionSphereTest(from, move, radius, &hit, &normal, RAGDOLL_TEST_TYPES, RAGDOLL_TEST_FLAGS);
    if (!volume)
        return;
    if (!IsDynamic(volume)) {
        if (point->contactFrame)
            point->contactFrame->Release();
        point->contactFrame = nullptr;
    } else if (volume->frame != point->contactFrame) {
        if (point->contactFrame)
            point->contactFrame->Release();
        C_Actor *actor = GetFrameActor(volume->frame);
        if ((m_flags & POSE_FLAG_ON_VEHICLE) && actor == m_vehicle) {
            point->contactFrame = nullptr;
            return;
        }
        point->contactFrame = volume->frame;
        point->contactFrame->AddRef();
        point->contactFramePos = PoseWorldPos4(point->contactFrame);
        if (C_Car *car = PushableCar(actor)) {
            S_vector impulse = PoseXYZ(point->step) * 100.0f;
            const float sq = impulse.Square();
            if (sq > RAGDOLL_CAR_IMPULSE * RAGDOLL_CAR_IMPULSE)
                impulse *= PoseFastInvSqrt(sq) * RAGDOLL_CAR_IMPULSE;
            car->GetVehicle()->ApplyImpulse(impulse, hit, 0, S_vector(1.0f, 1.0f, 1.0f));
        }
    }
    hit += normal * 0.001f;
    switch (point->index) {
    case RAGDOLL_POINT_NECK:
    case RAGDOLL_POINT_HEAD:
    case RAGDOLL_POINT_LEFT_SHOULDER:
    case RAGDOLL_POINT_LEFT_ELBOW:
    case RAGDOLL_POINT_LEFT_HAND:
    case RAGDOLL_POINT_RIGHT_SHOULDER:
    case RAGDOLL_POINT_RIGHT_ELBOW:
    case RAGDOLL_POINT_RIGHT_HAND:
    case RAGDOLL_POINT_LEFT_KNEE:
    case RAGDOLL_POINT_LEFT_FOOT:
    case RAGDOLL_POINT_RIGHT_KNEE:
    case RAGDOLL_POINT_RIGHT_FOOT: {
        // Above its parent on ground steeper than 45 degrees towards it: slides off sideways.
        const S_vector parentPos = PoseXYZ(point->parent->pos);
        if (!(parentPos.y < point->pos.y))
            break;
        const S_vector toParent = parentPos - PoseXYZ(point->pos);
        const float sq = toParent.Square();
        if (!(1e-8f < sq))
            break;
        if (!((toParent * PoseFastInvSqrt(sq)).Dot(normal) < -0.707f))
            break;
        S_vector slide = (toParent - normal * toParent.Dot(normal)) * 0.75f;
        const float slideSq = slide.Square();
        if (radius * radius * 0.998f < slideSq)
            slide *= PoseFastInvSqrt(slideSq) * radius * 0.999f;
        hit += slide;
        break;
    }
    default:
        break;
    }
    point->flags |= RAGDOLL_POINT_CONTACT;
    PoseSetXYZ(point->normal, normal);
    point->material = GetVolumeMaterial(*volume);
    PoseSetXYZ(out, hit - PoseXYZ(point->pos));
    switch (point->index) {
    case RAGDOLL_POINT_PELVIS:
    case RAGDOLL_POINT_CHEST:
    case RAGDOLL_POINT_NECK:
    case RAGDOLL_POINT_HEAD:
    case RAGDOLL_POINT_LEFT_SHOULDER:
    case RAGDOLL_POINT_RIGHT_SHOULDER:
    case RAGDOLL_POINT_LEFT_HIP:
    case RAGDOLL_POINT_RIGHT_HIP:
        if (m_impactSound == -1 && PoseXYZ(point->step).Square() > 7.2900004f) {
            PlayRagdollSound(point, false);
            PlayRagdollSound(point, true);
        }
        break;
    default:
        break;
    }
}

// 0x007ccba0: `body` swept from its last position against the world (a sphere around its middle
// as wide as the part is long); where it hits within the part's thickness of its sweep, both
// points go back along their moves to the contact (`startOut` / `endOut`), the volume becomes
// their contact and a car hit is pushed by the faster point.
void C_HumanWeaponPose::CollideRagdollBody(S_RagdollBody *body, S_vector4 &startOut, S_vector4 &endOut)
{
    S_RagdollPoint *start = body->start;
    S_RagdollPoint *end = body->end;
    const S_vector lastMiddle = (PoseXYZ(end->lastPos) + PoseXYZ(start->lastPos)) * 0.5f;
    const S_vector middle = (PoseXYZ(end->pos) + PoseXYZ(start->pos)) * 0.5f;
    const S_vector move = middle - lastMiddle;
    const float radius = (body->currentLength + body->thickness) * 0.5f;
    S_vector hit;
    S_vector normal;
    S_CollisionVolume *volume =
        CollisionSphereTest(lastMiddle, move, radius, &hit, &normal, RAGDOLL_TEST_TYPES, RAGDOLL_TEST_FLAGS);
    if (!volume)
        return;
    hit -= normal * radius;
    const S_vector startMove = PoseXYZ(start->pos) - PoseXYZ(start->lastPos);
    const S_vector lastSegment = PoseXYZ(end->lastPos) - PoseXYZ(start->lastPos);
    S_vector side = lastSegment.Cross(startMove);
    side.Normalize();
    if (!(std::fabs((hit - PoseXYZ(start->lastPos)).Dot(side)) < body->thickness * 0.5f))
        return;
    if (PoseIsNegative((hit - lastMiddle).Dot(move)))
        return;
    S_vector dir = middle - lastMiddle;
    const float sq = dir.Square();
    if (sq < 1e-8f)
        return;
    float length = 1.0f;
    if (std::fabs(sq - 1.0f) >= 1e-8f) {
        length = std::sqrt(sq);
        if (length <= 1e-8f)
            return;
    }
    dir *= 1.0f / length;
    const float t = dir.Dot(hit - lastMiddle) / (body->thickness * 0.5f + length);
    if (!(t < 1.0f))
        return;
    C_Actor *actor = nullptr;
    if (!IsDynamic(volume)) {
        if (start->contactFrame)
            start->contactFrame->Release();
        if (end->contactFrame)
            end->contactFrame->Release();
        end->contactFrame = nullptr;
        start->contactFrame = nullptr;
    } else if (volume->frame != start->contactFrame || volume->frame != end->contactFrame) {
        if (start->contactFrame)
            start->contactFrame->Release();
        if (end->contactFrame)
            end->contactFrame->Release();
        actor = GetFrameActor(volume->frame);
        if ((m_flags & POSE_FLAG_ON_VEHICLE) && actor == m_vehicle) {
            end->contactFrame = nullptr;
            start->contactFrame = nullptr;
            return;
        }
        end->contactFrame = volume->frame;
        start->contactFrame = end->contactFrame;
        start->contactFrame->AddRef();
        end->contactFrame->AddRef();
        end->contactFramePos = PoseWorldPos4(start->contactFrame);
        start->contactFramePos = end->contactFramePos;
    }
    const float u = 1.0f - t;
    const int material = GetVolumeMaterial(*volume);
    for (S_RagdollPoint *point : {start, end}) {
        const S_vector back = PoseXYZ(point->lastPos) * u + PoseXYZ(point->pos) * t;
        point->flags |= RAGDOLL_POINT_CONTACT;
        PoseSetXYZ(point->normal, normal);
        point->material = material;
        PoseSetXYZ(point == start ? startOut : endOut, back - PoseXYZ(point->pos));
    }
    if (C_Car *car = PushableCar(actor)) {
        const float startSq = PoseXYZ(start->step).Square();
        const float endSq = PoseXYZ(end->step).Square();
        const S_vector4 &faster = startSq <= endSq ? end->step : start->step;
        S_vector impulse = PoseXYZ(faster) * 100.0f;
        const float squared = (endSq < startSq ? startSq : endSq) * 10000.0f;
        if (squared > RAGDOLL_CAR_IMPULSE * RAGDOLL_CAR_IMPULSE)
            impulse *= PoseFastInvSqrt(squared) * RAGDOLL_CAR_IMPULSE;
        car->GetVehicle()->ApplyImpulse(impulse, hit, 0, S_vector(1.0f, 1.0f, 1.0f));
    }
}

// ---- settling -----------------------------------------------------------------------------

// 0x007cd690: once the settle delay has run out, the weight of `point` and the points below it
// grows while they fall and runs down while they rest on the ground (faster in water); then
// (at the root) the ragdoll's weight follows the points' average: it grows while they move and
// runs down by how flat the ground is (and while points lie on it for the ground time); a body
// coming to rest leaves a blood pool.
void C_HumanWeaponPose::SettleRagdoll(S_RagdollPoint *point)
{
    float fall = 0.0f;
    if (!point->parent) {
        m_settleSum = 0.0f;
        m_settleCount = 0.0f;
        m_groundedPoints = 0;
        m_contactFrames = 0;
        g_ragdollContactNormalY = 0.0f;
        g_ragdollContactCount = 0;
    }
    if (PoseIsNegative(m_settleDelay)) {
        fall = point->smoothVelocity.y;
        if (std::fabs(fall) >= 0.48f) {
            point->weight += 0.01f;
            if (point->weight > 1.0f)
                point->weight = 1.0f;
        } else {
            if (point->flags & (RAGDOLL_POINT_CONTACT | RAGDOLL_POINT_TOUCHING)) {
                const float sq = PoseXYZ(point->step).Square();
                if (1e-8f < sq) {
                    const float inv = PoseFastInvSqrt(sq);
                    float along = (PoseXYZ(point->step) * inv).Dot(PoseXYZ(point->normal));
                    along *= along;
                    point->weight -= along * along * 0.01f;
                    if (m_inWater)
                        point->weight -= 0.01f;
                }
            }
            if (PoseIsNegative(point->weight))
                point->weight = 0.0f;
        }
    }
    if (point->flags & (RAGDOLL_POINT_CONTACT | RAGDOLL_POINT_TOUCHING)) {
        g_ragdollContactNormalY += point->normal.y;
        ++g_ragdollContactCount;
        if (point->normal.y > 0.5f)
            ++m_groundedPoints;
    }
    if (point->contactFrame)
        ++m_contactFrames;
    m_settleSum += point->weight;
    m_settleCount += 1.0f;
    for (S_RagdollPoint *child : point->children)
        if (child)
            SettleRagdoll(child);
    if (point->parent)
        return;
    if (!PoseIsNegative(m_settleDelay)) {
        m_settleDelay -= m_stepTime;
        return;
    }
    // How fast the weight runs down: by the ground the points lie on (flat ground faster).
    // Quirk: the average is kept in the static, so a second use in the same check divides and
    // squares it again.
    auto settleRate = []() {
        if (g_ragdollContactCount == 0)
            return 0.02f;
        g_ragdollContactNormalY /= static_cast<float>(g_ragdollContactCount);
        if (PoseIsNegative(g_ragdollContactNormalY))
            g_ragdollContactNormalY = 0.0f;
        g_ragdollContactNormalY *= g_ragdollContactNormalY;
        return g_ragdollContactNormalY * 0.02f + 0.001f;
    };
    if (fall < -400.0f)
        m_ragdollWeight = 0.0f;
    m_settleCount = m_settleSum / m_settleCount;
    if (m_settleCount >= 0.85f) {
        m_ragdollWeight += 0.01f;
        if (m_ragdollWeight > 1.0f)
            m_ragdollWeight = 1.0f;
    } else {
        m_ragdollWeight -= settleRate();
        if (m_ragdollWeight < 1e-8f)
            m_ragdollWeight = 0.0f;
        if (!m_bloodPool)
            AddSettledBloodPool();
    }
    if (m_groundedPoints) {
        m_groundTime -= m_stepTime;
        if (PoseIsNegative(m_groundTime)) {
            m_ragdollWeight -= settleRate();
            if (m_ragdollWeight < 1e-8f)
                m_ragdollWeight = 0.0f;
            if (!m_bloodPool)
                AddSettledBloodPool();
        }
    }
}

// ---- pushes and holds ---------------------------------------------------------------------

// 0x007cda70: pushes `point` by `force` for `time` ms (in a free one of the eight slots) and
// wakes the ragdoll.
void C_HumanWeaponPose::AddRagdollPush(S_RagdollPoint *point, const S_vector4 &force, int time)
{
    if (!point)
        return;
    int slot = 0;
    while (slot < RAGDOLL_PUSH_COUNT && m_pushes[slot].point)
        ++slot;
    if (slot == RAGDOLL_PUSH_COUNT)
        return;
    S_RagdollPush &push = m_pushes[slot];
    push.point = point;
    push.force = force;
    push.time = static_cast<float>(time) * 0.001f;
    push.length = push.time;
    m_ragdollWeight = 1.0f;
    for (S_RagdollPoint *p : m_points)
        if (p)
            p->weight = 1.0f;
}

// 0x007cdb20: pulls `point` towards `target` for `time` ms (in a free one of the two slots).
// Quirk: the time goes into the push of the same slot, so the hold (with no time of its own)
// ends at the next step.
void C_HumanWeaponPose::AddRagdollHold(S_RagdollPoint *point, const S_vector4 &target, int time)
{
    if (!point)
        return;
    int slot = 0;
    while (slot < RAGDOLL_HOLD_COUNT && m_holds[slot].point)
        ++slot;
    if (slot == RAGDOLL_HOLD_COUNT)
        return;
    m_holds[slot].point = point;
    m_holds[slot].force = target;
    m_pushes[slot].time = static_cast<float>(time) * 0.001f;
}

// 0x007cdb90: the pushes add their forces while they last and their points are free; the holds
// pull their points towards their targets.
void C_HumanWeaponPose::ApplyRagdollPushes(float dt)
{
    for (int i = 0; i < RAGDOLL_PUSH_COUNT; ++i) {
        S_RagdollPush &push = m_pushes[i];
        if (!push.point)
            continue;
        if (!PoseIsNegative(push.time - dt) &&
            !(push.point->flags & (RAGDOLL_POINT_CONTACT | RAGDOLL_POINT_TOUCHING))) {
            push.time -= dt;
            AddXYZ(push.point->force, PoseXYZ(push.force));
        } else {
            push.point = nullptr;
        }
    }
    for (int i = 0; i < RAGDOLL_HOLD_COUNT; ++i) {
        S_RagdollPush &hold = m_holds[i];
        if (!hold.point)
            continue;
        if (PoseIsNegative(hold.time - dt)) {
            hold.point = nullptr;
        } else {
            hold.time -= dt;
            AddXYZ(hold.point->force, PoseXYZ(hold.force) - PoseXYZ(hold.point->pos));
        }
    }
}

// ---- contacts with moving things ----------------------------------------------------------

// 0x007cdca0: points resting on a dynamic volume let go of it (and wake the ragdoll, taking a
// tenth of its move along) when it moves or leaves the scene (clients only).
void C_HumanWeaponPose::UpdateRagdollContacts(int time)
{
    if (!IsClient())
        return;
    m_contactFrames = 0;
    for (S_RagdollPoint *point : m_points) {
        if (!point || !point->contactFrame)
            continue;
        I3D_frame *frame = point->contactFrame;
        S_vector speed(0.0f, 0.0f, 0.0f);
        bool moved = true;
        if (frame->GetRefCount() != 1) {
            speed = (frame->GetWorldPos() - PoseXYZ(point->contactFramePos)) * (1.0f / static_cast<float>(time));
            moved = 0.0001f < speed.Square();
        }
        if (moved) {
            if (m_ragdollWeight < 1e-8f) {
                m_groundTime = 4.0f;
                m_ragdollWeight = 1.0f;
                m_settleDelay = 1.0f;
                for (S_RagdollPoint *p : m_points)
                    if (p)
                        p->weight = 1.0f;
            }
            const float k = static_cast<float>(time) * 0.1f;
            point->oldPos.x -= speed.x * k;
            point->oldPos.y -= speed.y * k;
            point->oldPos.z -= speed.z * k;
            point->contactFrame->Release();
            point->contactFrame = nullptr;
        }
        if (point->contactFrame)
            ++m_contactFrames;
    }
}

// 0x007cdea0
void C_HumanWeaponPose::ReleaseRagdollContacts()
{
    for (S_RagdollPoint *point : m_points)
        if (point && point->contactFrame) {
            point->contactFrame->Release();
            point->contactFrame = nullptr;
        }
    m_contactFrames = 0;
}

// ---- sounds -------------------------------------------------------------------------------

// 0x007ce260: the sound of the body hitting the ground (`second`: the second one, at the neck;
// skipped for good on odd updates). (`point` is not used.)
void C_HumanWeaponPose::PlayRagdollSound(S_RagdollPoint *point, bool second)
{
    (void)point;
    if (!second) {
        if (m_impactSound != -1)
            return;
        S_SoundRequest request;
        request.attachTo = m_bones[POSE_BONE_PELVIS].frame;
        request.source = m_owner;
        request.flags = SOUND_SILENT_TO_AI | SOUND_KEEP_SLOT;
        request.chapter = RAGDOLL_IMPACT_CHAPTER;
        request.group = RAGDOLL_IMPACT_GROUP;
        request.channel = SOUND_CHANNEL_NONE;
        m_impactSound = GetSoundModule()->Play(request);
    } else if (m_dropSound == -1) {
        if (m_updateCount & 1) {
            m_dropSound = -2;
            return;
        }
        S_SoundRequest request;
        request.attachTo = m_bones[POSE_BONE_NECK].frame;
        request.source = m_owner;
        request.flags = SOUND_SILENT_TO_AI | SOUND_KEEP_SLOT;
        request.chapter = RAGDOLL_DROP_CHAPTER;
        request.group = RAGDOLL_DROP_GROUP;
        request.channel = SOUND_CHANNEL_NONE;
        m_dropSound = GetSoundModule()->Play(request);
    }
}

// 0x007ce370: frees the slot of a ground sound that has finished (the second one for good).
void C_HumanWeaponPose::UpdateRagdollSound(bool second)
{
    int *slot = second ? &m_dropSound : &m_impactSound;
    if (second ? *slot < 0 : *slot == -1)
        return;
    I3D_sound *sound = GetSoundModule()->GetSound(*slot);
    if (!sound || sound->IsPlaying(0))
        return;
    GetSoundModule()->Stop(slot);
    *slot = second ? -2 : -1;
}
