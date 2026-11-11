// Tumbling box (type 3) — see physics.h. The game never creates one; this is
// ported from the decompilation, whose register/stack aliasing leaves a few
// details open (noted below).
#include <cmath>

#include "game/physics.h"
#include "game/physics_internal.h"

using namespace physics;

namespace {

constexpr float GRAVITY = 9.81f;

// Points along the axis through the box center parallel to `axis` (0 = x,
// 1 = y, 2 = z): its start and end on the box faces.
void AxisThroughCenter(const I3D_bbox &box, int axis, S_vector &start, S_vector &end)
{
    const S_vector center = (box.min + box.max) * 0.5f;
    start = center;
    end = center;
    switch (axis) {
    case 0: start.x = box.min.x; end.x = box.max.x; break;
    case 1: start.y = box.min.y; end.y = box.max.y; break;
    default: start.z = box.min.z; end.z = box.max.z; break;
    }
}

} // namespace

C_PhysBox::C_PhysBox(I3D_frame *frame, S_PhysicsHandle *handle) : C_PhysObject(frame, handle)
{
    m_type = PHYS_BOX;
    if (!m_frame)
        return;
    const I3D_bound_volume bound = HierarchyBound(frame);
    InitBoxVolume(m_volume, m_matrix, m_invMatrix, bound, COLLISION_SHAPE_BOX, COLLISION_TYPE_LIGHT_OBJECT);
    // (The original transforms the center by the inverse matrix here.)
    m_volume.center = bound.center * m_invMatrix;
    g_collisionWorld.AddVolume(&m_volume);
    g_collisionWorld.MoveVolume(&m_volume);

    // The longest axis, and the longer of the other two.
    const S_vector size(std::fabs(bound.bbox.min.x - bound.bbox.max.x), std::fabs(bound.bbox.min.y - bound.bbox.max.y),
                        std::fabs(bound.bbox.min.z - bound.bbox.max.z));
    int axis1, axis2;
    if (!(size.x < size.y || size.x < size.z)) {
        axis1 = 0;
        axis2 = size.y < size.z ? 2 : 1;
    } else if (!(size.y < size.x || size.y < size.z)) {
        axis1 = 1;
        axis2 = size.x < size.z ? 2 : 0;
    } else {
        axis1 = 2;
        axis2 = size.y < size.x ? 0 : 1;
    }
    AxisThroughCenter(bound.bbox, axis1, m_axis1Start, m_axis1End);
    AxisThroughCenter(bound.bbox, axis2, m_axis2Start, m_axis2End);
}

void C_PhysBox::ApplyGravity(float dt)
{
    if (m_state == BOX_FLYING)
        C_PhysObject::ApplyGravity(dt);
}

void C_PhysBox::Rotate(float dt)
{
    if (m_state == BOX_FLYING)
        C_PhysObject::Rotate(dt);
}

bool C_PhysBox::Tick(float dt)
{
    switch (m_state) {
    case BOX_FLYING: return TickFlying(dt);
    case BOX_TIPPING: return TickTipping(dt);
    case BOX_ROLLING: return TickRolling(dt);
    default: return true;
    }
}

bool C_PhysBox::TickFlying(float dt)
{
    if (!m_frame)
        return false;
    const S_vector move = m_velocity * dt;
    m_frame->SetPos(m_pos + move);
    m_frame->Update();
    S_vector a = MulAssigned(m_axis1Start, m_frame->GetMatrix());
    S_vector b = MulAssigned(m_axis1End, m_frame->GetMatrix());

    // Does the box's axis at the new position, or the move itself, touch anything?
    S_vector hitPos, hitNormal;
    g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
    const S_CollisionVolume *hit = CollisionRayTest(a, b - a, &hitPos, &hitNormal, 0xFFFFFFFF, 0);
    if (!hit)
        hit = CollisionRayTest(b, a - b, &hitPos, &hitNormal, 0xFFFFFFFF, 0);
    if (!hit)
        hit = CollisionRayTest(m_pos, move, &hitPos, &hitNormal, 0xFFFFFFFF, 0);
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);

    if (!hit) {
        m_pos += move;
        const S_quat previousRot = m_frame->GetRot();
        Rotate(dt);
        m_frame->SetRot(m_rot);
        m_frame->Update();
        a = MulAssigned(m_axis1Start, m_frame->GetMatrix());
        b = MulAssigned(m_axis1End, m_frame->GetMatrix());
        g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
        hit = CollisionRayTest(a, b - a, &hitPos, &hitNormal, 0xFFFFFFFF, 0);
        if (!hit)
            hit = CollisionRayTest(b, a - b, &hitPos, &hitNormal, 0xFFFFFFFF, 0);
        g_collisionWorld.SetIgnoredVolume(nullptr, 0);
        if (!hit) {
            ApplyGravity(dt);
            return true;
        }
        // The turn hit something: undo it and bounce.
        m_rot = previousRot;
        Bounce(m_velocity, hitNormal);
        m_velocity *= 0.5f;
        if (m_velocity.Magnitude() < 0.1f) {
            m_state = BOX_TIPPING;
            m_contact = hitPos;
            m_angularVelocity.Zero();
        }
        return true;
    }

    Bounce(m_velocity, hitNormal);
    m_velocity *= 0.4f;
    const float speed = m_velocity.Magnitude();
    if (speed >= 0.1f) {
        if (speed >= 0.5f)
            return true;
        if (hitNormal.y > 0.7f)     // slow on a floor: stops
            m_velocity.Zero();
    }
    m_state = BOX_TIPPING;
    m_contact = hitPos;
    m_angularVelocity.Zero();
    return true;
}

void C_PhysBox::Tip(float dt)
{
    if (!m_frame)
        return;
    // Gravity turns the box around its contact point, faster the further away its
    // center is.
    const S_vector gravity(0.0f, -GRAVITY * dt, 0.0f);
    const S_vector pos = m_frame->GetWorldPos();
    const S_vector lever = pos - m_contact;
    const S_vector axis = gravity.Normalized().Cross(lever.Normalized()).Normalized();
    S_quat turn;
    turn.Make(axis, lever.Magnitude() * gravity.Magnitude());
    const S_matrix turnMatrix = turn.RotationMatrix();

    m_frame->SetRot(m_rot.RotateByMatrix(turnMatrix));
    m_frame->Update();
    m_frame->SetPos(m_contact + lever.RotateByMatrix(turnMatrix));
    m_frame->Update();
}

bool C_PhysBox::TickTipping(float dt)
{
    if (!m_frame)
        return false;
    const S_vector a0 = MulAssigned(m_axis1Start, m_frame->GetMatrix());
    const S_vector b0 = MulAssigned(m_axis1End, m_frame->GetMatrix());
    Tip(dt);
    const S_vector a1 = MulAssigned(m_axis1Start, m_frame->GetMatrix());
    const S_vector b1 = MulAssigned(m_axis1End, m_frame->GetMatrix());

    // The end further from the contact moves most; test it first.
    S_vector hitPos, hitNormal;
    g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
    const S_CollisionVolume *hit;
    if (DistanceZYX(m_contact, a0) <= DistanceZYX(m_contact, b0)) {
        hit = CollisionRayTest(b0, b1 - b0, &hitPos, &hitNormal, 0xFFFFFFFF, 0);
        if (!hit)
            hit = CollisionRayTest(a0, a1 - a0, &hitPos, &hitNormal, 0xFFFFFFFF, 0);
    } else {
        hit = CollisionRayTest(a0, a1 - a0, &hitPos, &hitNormal, 0xFFFFFFFF, 0);
        if (!hit)
            hit = CollisionRayTest(b0, b1 - b0, &hitPos, &hitNormal, 0xFFFFFFFF, 0);
    }
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);

    if (!hit) {
        m_rot = m_frame->GetRot();
        m_pos = m_frame->GetWorldPos();
        return true;
    }
    // Lies flat now: undo the last step and start rolling around the long axis.
    m_frame->SetRot(m_rot);
    m_frame->SetPos(m_pos);
    m_frame->Update();
    m_state = BOX_ROLLING;
    // The box's long axis in world space, from its start to its end (0x0040d21a).
    m_rollAxis = (b0 - a0).Normalized();
    m_contact = m_pos;
    const S_vector c = MulAssigned(m_axis2Start, m_frame->GetMatrix());
    const S_vector d = MulAssigned(m_axis2End, m_frame->GetMatrix());
    if (d.y < c.y)
        m_rollAxis = -m_rollAxis;
    return false;
}

void C_PhysBox::Roll(float dt)
{
    if (!m_frame)
        return;
    const S_vector gravity(0.0f, -GRAVITY * dt, 0.0f);
    S_quat turn;
    turn.Make(m_rollAxis, gravity.Magnitude() * m_mass * 0.25f);
    m_frame->SetRot(m_rot.RotateByMatrix(turn.RotationMatrix()));
    m_frame->Update();
}

bool C_PhysBox::TickRolling(float dt)
{
    if (!m_frame)
        return false;
    const S_vector a0 = MulAssigned(m_axis2Start, m_frame->GetMatrix());
    const S_vector b0 = MulAssigned(m_axis2End, m_frame->GetMatrix());
    Roll(dt);
    const S_vector a1 = MulAssigned(m_axis2Start, m_frame->GetMatrix());
    const S_vector b1 = MulAssigned(m_axis2End, m_frame->GetMatrix());

    S_vector hitPos, hitNormal;
    g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
    const S_CollisionVolume *hit = DistanceZYX(m_contact, a0) <= DistanceZYX(m_contact, b0)
                                       ? CollisionRayTest(b0, b1 - b0, &hitPos, &hitNormal, 0xFFFFFFFF, 0)
                                       : CollisionRayTest(a0, a1 - a0, &hitPos, &hitNormal, 0xFFFFFFFF, 0);
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);

    if (hit) {
        m_frame->SetRot(m_rot);
        m_frame->SetPos(m_pos);
        m_frame->Update();
        return false;
    }
    m_rot = m_frame->GetRot();
    m_pos = m_frame->GetWorldPos();
    return true;
}
