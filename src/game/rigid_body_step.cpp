// The rigid body's integration steps (C_RigidBody; part 5b): the rolling disc's step (StepWheel),
// the box's (StepBox), the turn and move (Integrate), the contact impulse (Collide) and the contact
// filter (FilterContact). The sums keep the original's order: each place has its own.
#include "game/rigid_body.h"

#include <cmath>
#include <cstdint>

#include "game/actor.h"
#include "game/particles.h"
#include "game/rigid_body_internal.h"
#include "game/shots.h"

namespace {

// Materials a contact always counts on (their volumes' actors are not asked about).
constexpr int MATERIAL_TENT_CANVAS = 0x43;              // "Plátěná stěna stanu"
constexpr int MATERIAL_PAPER = 0x55;                    // "Papír"

constexpr float MIN_DENOMINATOR = 1e-06f;               // Collide's impulse denominator, at least
constexpr float FRICTION_WEIGHTS = 3.0f;                // friction is at most 3 x the weight's
constexpr float RAY_REACH = 0.3f;                       // the box's rays reach this far past a point
constexpr float SLOW_SPEED_SQUARE = 1.0f;               // under 1 m/s: every point tested, the spin damped
constexpr float SPIN_DAMPING = 0.5f;                    // per second, when slow
constexpr float WATER_G = 9.0f;
constexpr float WATER_FAST = 1.0f;                      // m/s: faster bodies entering water slow to 3/4
constexpr float WATER_SLOWING = 0.75f;
constexpr float WATER_DRAG = 200.0f;                    // m/s per second, down to WATER_MIN_SPEED
constexpr float WATER_MIN_SPEED = 0.5f;
constexpr int SPLASH_EFFECT = 0x2D;
constexpr float SPLASH_HEIGHT = 0.02f;
constexpr uint32_t RAY_EXCLUDE_TYPES = 0x88002;         // the box's rays
constexpr uint32_t WHEEL_RAY_EXCLUDE_TYPES = 0x8002;
constexpr uint32_t RAY_EXCLUDE_FLAGS = COLLISION_FLAG_BULLETS_PASS;
constexpr uint32_t RAY_EXCLUDE_FLAGS_DRY = COLLISION_FLAG_BULLETS_PASS | COLLISION_FLAG_WATER;
constexpr int CONTACT_TIME = 1000;                      // ms an actor's record lasts untouched
constexpr float FLAT_GROUND = 0.9f;                     // the normal's y over which m_stopOnFlat stops a disc
constexpr float TOUCHING = 0.0001f;                     // the disc's ground distance of a contact

// The rotation of quaternion `a` and then `b` (the game's inlined product).
S_quat Compose(const S_quat &a, const S_quat &b)
{
    S_quat q;
    q.s = a.s * b.s - DotZYX(a.v, b.v);
    const S_vector c = a.v.Cross(b.v);
    q.v = S_vector((b.v.x * a.s + a.v.x * b.s) + c.x, (b.v.y * a.s + a.v.y * b.s) + c.y,
                   (b.v.z * a.s + a.v.z * b.s) + c.z);
    return q;
}

// A turn (an angular impulse or torque) through the inverse inertia, which is about the body's
// axes: into them, scaled, back.
S_vector ThroughInertia(S_vector turn, const S_matrix &invRotation, const S_vector &invInertia, const S_matrix &rotation)
{
    turn *= invRotation;
    turn.x = turn.x * invInertia.x;
    turn.y = turn.y * invInertia.y;
    turn.z = turn.z * invInertia.z;
    turn *= rotation;
    return turn;
}

// Where the disc (centre `pos`, axle `right`) touches the ground plane: its rim's point nearest
// the ground (along the line where the ground meets the wheel's own plane; the centre when they
// are parallel), half the width along the axle away from the ground's side; and its height over
// the ground (none under 0). The point's w is the axle's.
S_vector4 RimContact(const S_plane &ground, const S_vector4 &pos, const S_vector4 &right, float radius, float width,
                     float &height)
{
    S_plane wheel;
    wheel.normal = XYZ(right);
    wheel.d = -((right.z * pos.z + right.y * pos.y) + pos.x * right.x);
    S_vector linePos, lineDir;
    S_vector base = XYZ(pos);
    if (wheel.IntersectionLine(ground, linePos, lineDir)) {
        S_vector down = XYZ(right).Cross(lineDir);
        NormalizeBy(down, (down.x * down.x + down.y * down.y) + down.z * down.z);
        base = S_vector(down.x * radius + pos.x, down.y * radius + pos.y, down.z * radius + pos.z);
    }
    float half = width * 0.5f;
    if ((ground.normal.z * right.z + ground.normal.y * right.y) + right.x * ground.normal.x > 0.0f)
        half = -half;
    const S_vector4 contact = {right.x * half + base.x, half * right.y + base.y, half * right.z + base.z, right.w};
    height = DotZYX(XYZ(contact), ground.normal) + ground.d;
    if (height <= 0.0f)
        height = 0.0f;
    return contact;
}

}  // namespace

void C_RigidBody::Integrate(float dt, const S_vector &move)   // 0x004cc520
{
    // The spin's turn in the body's axes (the inverse matrix without its move), about the x, y and z
    // axes in turn.
    S_vector turn(dt * m_spin.x, dt * m_spin.y, dt * m_spin.z);
    S_matrix toBody = m_invMatrix;
    toBody.m[3][0] = toBody.m[3][1] = toBody.m[3][2] = 0.0f;
    turn *= toBody;
    S_quat rotation;
    rotation.Make(XYZ(m_right), turn.x);
    S_quat step;
    step.Make(XYZ(m_up), turn.y);
    rotation = Compose(rotation, step);
    step.Make(XYZ(m_dir), turn.z);
    rotation = Compose(rotation, step);
    const S_matrix turning = rotation.RotationMatrix();
    S_vector dir = XYZ(m_dir);
    dir *= turning;
    SetXYZ(m_dir, dir);
    S_vector up = XYZ(m_up);
    up *= turning;
    SetXYZ(m_up, up);
    // The place turns about the centre of mass. (The original's first matrix has an unset w; the
    // products are affine and never read it.)
    if (m_hasCentre) {
        S_matrix toCentre, fromCentre;
        toCentre.Identity();
        fromCentre.Identity();
        toCentre.m[3][0] = -m_centreWorld.x;
        toCentre.m[3][1] = -m_centreWorld.y;
        toCentre.m[3][2] = -m_centreWorld.z;
        fromCentre.m[3][0] = m_centreWorld.x;
        fromCentre.m[3][1] = m_centreWorld.y;
        fromCentre.m[3][2] = m_centreWorld.z;
        fromCentre.m[3][3] = m_centreWorld.w;
        S_matrix about = toCentre * turning;
        about = about * fromCentre;
        S_vector pos = XYZ(m_pos);
        pos *= about;
        SetXYZ(m_pos, pos);
    }
    // The axes square again (their w are left: the original's are unset), each unit.
    const S_vector right(up.y * dir.z - dir.y * up.z, up.z * dir.x - up.x * dir.z, up.x * dir.y - up.y * dir.x);
    up = S_vector(right.z * dir.y - dir.z * right.y, dir.z * right.x - right.z * dir.x, right.y * dir.x - right.x * dir.y);
    S_vector unit = right;
    NormalizeBy(unit, (unit.x * unit.x + unit.y * unit.y) + unit.z * unit.z);
    SetXYZ(m_right, unit);
    NormalizeBy(up, (up.x * up.x + up.y * up.y) + up.z * up.z);
    SetXYZ(m_up, up);
    NormalizeBy(dir, (dir.x * dir.x + dir.y * dir.y) + dir.z * dir.z);
    SetXYZ(m_dir, dir);
    m_pos.x = move.x + m_pos.x;
    m_pos.y = move.y + m_pos.y;
    m_pos.z = move.z + m_pos.z;
    m_frame->SetWorldPosDir3(XYZ(m_pos), dir, up);
}

void C_RigidBody::Collide(S_vector &velocity, S_vector &spin, const S_vector &normal, const S_vector4 &point,
                          float /*unusedDt*/, int16_t pointIndex, int /*unused7*/)   // 0x004cccc0
{
    // The point's velocity: the body's less the spin's (impulses subtract their turn).
    const S_vector arm(point.x - m_centreWorld.x, point.y - m_centreWorld.y, point.z - m_centreWorld.z);
    const S_vector turning(arm.z * m_spin.y - arm.y * m_spin.z, arm.x * m_spin.z - arm.z * m_spin.x,
                           arm.y * m_spin.x - arm.x * m_spin.y);
    const S_vector relative(m_velocity.x - turning.x, m_velocity.y - turning.y, m_velocity.z - turning.z);
    const S_vector reach = ThroughInertia(arm.Cross(normal), m_invRotation, m_invInertia, m_rotation);
    const float approach = (relative.z * normal.z + relative.y * normal.y) + relative.x * normal.x;
    if (!(approach < 0.0f))
        return;
    // The bounce: the impulse along the normal that turns the approach into m_bounce - 1 of it away.
    const float bounce = -m_bounce * approach;
    const S_vector lever(reach.y * arm.z - reach.z * arm.y, reach.z * arm.x - arm.z * reach.x,
                         arm.y * reach.x - reach.y * arm.x);
    const float inverseMass = 1.0f / m_mass;
    float denominator = ((lever.x * normal.x + lever.y * normal.y) + lever.z * normal.z) + inverseMass;
    if (std::fabs(denominator) < 1e-06)                 // (a double)
        denominator = MIN_DENOMINATOR;
    const float size = bounce / denominator;
    const S_vector impulse(size * normal.x, size * normal.y, size * normal.z);
    velocity.x = impulse.x * inverseMass + velocity.x;
    velocity.y = impulse.y * inverseMass + velocity.y;
    velocity.z = impulse.z * inverseMass + velocity.z;
    const S_vector twist = ThroughInertia(arm.Cross(impulse), m_invRotation, m_invInertia, m_rotation);
    spin.x = spin.x - twist.x;
    spin.y = spin.y - twist.y;
    spin.z = spin.z - twist.z;
    // The frame's hardest impulse (the hit sound's).
    const float strength = LengthOf((impulse.x * impulse.x + impulse.y * impulse.y) + impulse.z * impulse.z);
    if (strength > m_hardestHit) {
        m_hardestHit = strength;
        m_hitPointIndex = static_cast<uint16_t>(pointIndex);
        m_hitPoint = point;
    }
    // Friction: against the slide (the approach's part along the surface), the impulse's friction
    // times the sine of the approach's angle to the normal, at most 3 x the weight's.
    S_vector slide = relative;
    NormalizeBy(slide, (relative.x * relative.x + relative.y * relative.y) + relative.z * relative.z);
    S_vector along = slide.Cross(normal).Cross(normal);
    NormalizeBy(along, (along.x * along.x + along.y * along.y) + along.z * along.z);
    // (The sine is the x87's, unrounded; the product rounds to a float.)
    float friction = static_cast<float>(static_cast<double>(strength * m_friction) *
                                        std::sin(static_cast<double>(relative.AngleTo(normal))));
    const float most = ((m_g * m_friction) * m_mass) * FRICTION_WEIGHTS;
    if (friction > most)
        friction = most;
    along = S_vector(along.x * friction, along.y * friction, along.z * friction);
    const float inverseMass2 = 1.0f / m_mass;
    velocity.x = along.x * inverseMass2 + velocity.x;
    velocity.y = along.y * inverseMass2 + velocity.y;
    velocity.z = along.z * inverseMass2 + velocity.z;
    const S_vector drag = ThroughInertia(along.Cross(arm), m_invRotation, m_invInertia, m_rotation);
    spin.x = drag.x + spin.x;
    spin.y = drag.y + spin.y;
    spin.z = drag.z + spin.z;
}

S_CollisionVolume *C_RigidBody::FilterContact(S_CollisionVolume *volume, const S_vector4 &pos, const S_vector4 &normal,
                                              bool *onActor)   // 0x004ce990
{
    if (m_callback) {
        S_RigidBodyContact contact = {};
        contact.volume = volume;
        contact.pos = XYZ(pos);
        contact.posW = pos.w;
        contact.normal = XYZ(normal);
        contact.normalW = normal.w;
        // (The original passes the body in EDX.)
        if (m_callback(RIGID_BODY_EVENT_CONTACT, 0, m_context, &contact) == RIGID_BODY_REFUSE)
            return nullptr;
    }
    const int material = GetVolumeMaterial(*volume);
    if (material == MATERIAL_PAPER || material == MATERIAL_TENT_CANVAS)
        return volume;
    if (material == MATERIAL_WADING_WATER || material == MATERIAL_SWIMMING_WATER || material == MATERIAL_COLD_WATER)
        return nullptr;
    if ((volume->flags & COLLISION_FLAG_DYNAMIC) != 0 && volume->actor) {
        if (m_ignoreActors)
            return nullptr;
        const E_ActorType type = volume->actor->GetType();
        *onActor = type == ACTOR_CAR || type == ACTOR_ANIMATION;
    }
    return volume;
}

void C_RigidBody::StepWheel(float dt)                   // 0x004c94b0
{
    const float speed = LengthOf((m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y) + m_velocity.z * m_velocity.z);
    S_vector torque(0.0f, 0.0f, 0.0f);
    S_vector force(0.0f, 0.0f, 0.0f);
    if (m_springForce > 0.0f) {
        // On the ground: rolling resistance against the motion (m_unknown594 / radius of the weight;
        // in proportion under 1 m/s), at the arm under the centre (through the place, as rounded).
        S_vector back(-m_velocity.x, -m_velocity.y, -m_velocity.z);
        NormalizeBy(back, (back.z * back.z + back.y * back.y) + back.x * back.x);
        if (speed < 1.0f)
            back = S_vector(back.x * speed, back.y * speed, back.z * speed);
        const float resistance = (m_g * m_mass) * (m_unknown594 / m_radius);
        force = S_vector(back.x * resistance, back.y * resistance, resistance * back.z);
        const float under = -m_radius;
        const S_vector arm((under * m_normal.x + m_pos.x) - m_pos.x, (under * m_normal.y + m_pos.y) - m_pos.y,
                           (under * m_normal.z + m_pos.z) - m_pos.z);
        torque = S_vector(arm.z * force.y - arm.y * force.z, force.z * arm.x - arm.z * force.x,
                          arm.y * force.x - force.y * arm.x);
        // Friction against the slide of the point under the centre: friction x the weight, x the sine
        // of the slide's angle to the normal, x the slide's speed under 1 m/s.
        const S_vector bottom(under * m_normal.x, under * m_normal.y, under * m_normal.z);
        const S_vector turning(bottom.z * m_spin.y - bottom.y * m_spin.z, bottom.x * m_spin.z - bottom.z * m_spin.x,
                               bottom.y * m_spin.x - bottom.x * m_spin.y);
        const S_vector slide(m_velocity.x - turning.x, m_velocity.y - turning.y, m_velocity.z - turning.z);
        S_vector rub = slide;
        NormalizeBy(rub, (slide.z * slide.z + slide.y * slide.y) + slide.x * slide.x);
        const float grip = (-m_friction * m_g) * m_mass;
        rub = S_vector(rub.x * grip, rub.y * grip, rub.z * grip);
        // (The sine is the x87's, unrounded; the products round to floats.)
        const double sine = std::sin(static_cast<double>(slide.AngleTo(XYZ(m_normal))));
        if (sine < 1.0)
            rub = S_vector(static_cast<float>(rub.x * sine), static_cast<float>(rub.y * sine),
                           static_cast<float>(rub.z * sine));
        const float slideSpeed = LengthOf((slide.x * slide.x + slide.z * slide.z) + slide.y * slide.y);
        if (slideSpeed < 1.0f)
            rub = S_vector(rub.x * slideSpeed, rub.y * slideSpeed, rub.z * slideSpeed);
        force = S_vector(rub.x + force.x, rub.y + force.y, rub.z + force.z);
        torque.x = (bottom.z * rub.y - bottom.y * rub.z) + torque.x;
        torque.y = (rub.z * bottom.x - bottom.z * rub.x) + torque.y;
        torque.z = (bottom.y * rub.x - rub.y * bottom.x) + torque.z;
        // The ground's spring, at the last contact.
        const S_vector spring(m_normal.x * m_springForce, m_normal.y * m_springForce, m_normal.z * m_springForce);
        force = S_vector(force.x + spring.x, force.y + spring.y, force.z + spring.z);
        const S_vector lever(m_contactPoint.x - m_pos.x, m_contactPoint.y - m_pos.y, m_contactPoint.z - m_pos.z);
        torque.x = (lever.z * spring.y - lever.y * spring.z) + torque.x;
        torque.y = (lever.x * spring.z - lever.z * spring.x) + torque.y;
        torque.z = (lever.y * spring.x - lever.x * spring.y) + torque.z;
    }
    const float perMass = dt / m_mass;
    m_velocity.x = (force.x + m_gravity.x) * perMass + m_velocity.x;
    m_velocity.y = perMass * (force.y + m_gravity.y) + m_velocity.y;
    m_velocity.z = perMass * (force.z + m_gravity.z) + m_velocity.z;
    const S_vector move(dt * m_velocity.x, dt * m_velocity.y, dt * m_velocity.z);
    const S_vector turn = ThroughInertia(torque, m_invRotation, m_invInertia, m_rotation);
    m_spin.x = turn.x * dt + m_spin.x;
    m_spin.y = turn.y * dt + m_spin.y;
    m_spin.z = turn.z * dt + m_spin.z;
    const S_vector4 oldPos = m_pos;
    const S_vector4 oldDir = m_dir;
    const S_vector4 oldUp = m_up;
    const S_vector4 oldRight = m_right;
    m_centreWorld = m_pos;
    Integrate(dt, move);
    m_springForce = 0.0f;
    m_groundDistance = m_contactRange;
    // The ground under the old place: down past the disc's diameter, the step's sink and the
    // contact range.
    const float sink = (move.z + move.x) * 0.0f - move.y * 1.0f;
    const float depth = -((sink + (m_radius + m_radius)) + m_contactRange);
    m_pointSurface[0] = 0;
    // Fix: the original's contact point is unset stack when the ray misses (and is stored then);
    // here it stays the last one.
    S_vector4 hit = m_contactPoint;
    S_vector hitPos = XYZ(hit);
    S_vector hitNormal = XYZ(m_normal);
    bool onActor = false;
    g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
    S_CollisionVolume *volume = CollisionRayTest(XYZ(oldPos), S_vector(0.0f, depth, 0.0f), &hitPos, &hitNormal,
                                                 WHEEL_RAY_EXCLUDE_TYPES, RAY_EXCLUDE_FLAGS_DRY);
    SetXYZ(m_normal, hitNormal);
    SetXYZ(hit, hitPos);
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    if (volume && (volume = FilterContact(volume, hit, m_normal, &onActor)) != nullptr) {
        if (m_stopOnFlat && (m_normal.z + m_normal.x) * 0.0f + m_normal.y > FLAT_GROUND) {
            m_velocity = S_vector(0.0f, 0.0f, 0.0f);
            m_spin = S_vector(0.0f, 0.0f, 0.0f);
            SetState(RIGID_BODY_AT_REST);
            return;
        }
        S_plane ground;
        ground.normal = XYZ(m_normal);
        ground.d = -((hit.y * m_normal.y + hit.z * m_normal.z) + hit.x * m_normal.x);
        m_groundDistance = (DotZYX(XYZ(m_pos), ground.normal) + ground.d) - m_radius;
        if (m_groundDistance <= m_contactRange) {
            m_pointSurface[0] = static_cast<uint16_t>(GetVolumeMaterial(*volume));
            hit = RimContact(ground, m_pos, m_right, m_radius, m_width, m_groundDistance);
            m_springForce = (m_contactRange - m_groundDistance) * m_stiffness;
        }
    }
    m_contactPoint = hit;
    // Touching and moving into the ground: the step again from the old place, after the impulse.
    if (m_groundDistance < TOUCHING &&
        (m_velocity.z * m_normal.z + m_velocity.y * m_normal.y) + m_velocity.x * m_normal.x < 0.0f) {
        m_dir = oldDir;
        m_up = oldUp;
        m_right = oldRight;
        m_pos = oldPos;
        m_frame->SetWorldPosDir3(XYZ(m_pos), XYZ(m_dir), XYZ(m_up));
        m_centreWorld = m_pos;
        Collide(m_velocity, m_spin, XYZ(m_normal), hit, dt, 0, 0);
        Integrate(dt, S_vector(dt * m_velocity.x, dt * m_velocity.y, dt * m_velocity.z));
    }
    // What lies along the way (from the old place, as far as the step and the radius).
    m_groundDistance = m_contactRange;
    const S_vector step(dt * m_velocity.x, dt * m_velocity.y, dt * m_velocity.z);
    const float length = LengthOf((step.x * step.x + step.y * step.y) + step.z * step.z);
    S_vector way = step;
    NormalizeBy(way, (step.z * step.z + step.x * step.x) + step.y * step.y);
    const float reach = length + m_radius;
    way = S_vector(way.x * reach, reach * way.y, reach * way.z);
    // Fix: the original's normal is unset stack when nothing is met (read only with a contact
    // range under 0.0001); 0 here.
    S_vector4 normal = {0.0f, 0.0f, 0.0f, 0.0f};
    hitPos = XYZ(hit);
    hitNormal = XYZ(normal);
    g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
    volume = CollisionRayTest(XYZ(oldPos), way, &hitPos, &hitNormal, WHEEL_RAY_EXCLUDE_TYPES, RAY_EXCLUDE_FLAGS_DRY);
    SetXYZ(normal, hitNormal);
    SetXYZ(hit, hitPos);
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    if (volume && FilterContact(volume, hit, normal, &onActor)) {
        S_plane ground;
        ground.normal = XYZ(normal);
        ground.d = -((normal.x * hit.x + normal.z * hit.z) + normal.y * hit.y);
        m_groundDistance = (DotZYX(XYZ(m_pos), ground.normal) + ground.d) - m_radius;
        if (m_groundDistance <= m_contactRange)
            hit = RimContact(ground, m_pos, m_right, m_radius, m_width, m_groundDistance);
    }
    if (m_groundDistance < TOUCHING &&
        (normal.z * m_velocity.z + normal.y * m_velocity.y) + normal.x * m_velocity.x < 0.0f) {
        m_centreWorld = m_pos;
        Collide(m_velocity, m_spin, XYZ(normal), hit, dt, 0, 0);
    }
    m_matrix = m_frame->GetMatrix();
    const S_matrix source = m_matrix;
    m_invMatrix = m_matrix;
    m_invMatrix.Inverse(source);
    m_rotation = m_matrix;
    m_rotation.m[3][0] = m_rotation.m[3][1] = m_rotation.m[3][2] = 0.0f;
    m_invRotation = m_invMatrix;
    m_invRotation.m[3][0] = m_invRotation.m[3][1] = m_invRotation.m[3][2] = 0.0f;
    m_centreWorld = m_pos;
}

void C_RigidBody::StepBox(float dt)                     // 0x004cb2b0
{
    const float perMass = dt / m_mass;
    m_testTrailing = !m_testTrailing;
    m_velocity.x = perMass * m_gravity.x + m_velocity.x;
    m_velocity.y = perMass * m_gravity.y + m_velocity.y;
    m_velocity.z = perMass * m_gravity.z + m_velocity.z;
    const S_vector move(dt * m_velocity.x, dt * m_velocity.y, dt * m_velocity.z);
    const S_vector4 from = m_centreWorld;               // the rays start at the centre before the move
    Integrate(dt, move);
    S_matrix world = m_frame->GetMatrix();
    if (m_hasCentre)
        SetXYZ(m_centreWorld, m_centre * world);        // (the original's w is a temporary's, unset)
    else
        m_centreWorld = m_pos;
    C_Actor *pointActors[RIGID_BODY_POINTS] = {};
    S_vector push(0.0f, 0.0f, 0.0f);
    if ((m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y) + m_velocity.z * m_velocity.z < SLOW_SPEED_SQUARE) {
        m_testTrailing = true;
        const float damping = 1.0f - dt * SPIN_DAMPING;
        m_spin.x = damping * m_spin.x;
        m_spin.y = damping * m_spin.y;
        m_spin.z = damping * m_spin.z;
    }
    // Each point (the last first): the leading ones (and every other step, or when slow, the
    // trailing ones too) are tested from the old centre out to 0.3 m past them, by a ray when they
    // moved or an actor's record names them, else against the planes the points met last.
    bool contact = false;
    uint32_t touching = 0;
    S_vector points[RIGID_BODY_POINTS];
    S_plane planes[RIGID_BODY_POINTS];
    S_vector4 hits[RIGID_BODY_POINTS] = {};
    const S_vector start = XYZ(from);
    for (int i = RIGID_BODY_POINTS; i-- > 0;) {
        const S_vector p = XYZ(m_points[i]) * world;
        points[i] = p;
        const S_vector arm(p.x - from.x, p.y - from.y, p.z - from.z);
        if (!((arm.z * m_velocity.z + arm.y * m_velocity.y) + arm.x * m_velocity.x > 0.0f) && !m_testTrailing)
            continue;
        S_vector out = arm;
        NormalizeBy(out, (arm.z * arm.z + arm.y * arm.y) + arm.x * arm.x);
        const S_vector ray(out.x * RAY_REACH + arm.x, arm.y + out.y * RAY_REACH, arm.z + out.z * RAY_REACH);
        bool retest = m_contactsChanged;
        for (int k = m_actorContactCount; k-- > 0;) {
            if (m_actorContacts[k].point == i) {
                retest = true;
                break;
            }
        }
        S_vector4 &last = m_lastPoints[i];
        const float mx = p.x - last.x;
        const float my = p.y - last.y;
        const float mz = p.z - last.z;
        // (Quirk kept: the squared move is compared with the skin.)
        if (!((mz * mz + my * my) + mx * mx > m_skin) && !retest) {
            // Against the planes the points met last: the first (from the last) the point is in.
            for (int k = RIGID_BODY_POINTS; k-- > 0;) {
                if (!m_pointHit[k])
                    continue;
                const S_plane &plane = m_pointPlanes[k];
                const float across = (ray.x * plane.normal.x + ray.z * plane.normal.z) + ray.y * plane.normal.y;
                if (std::fabs(across) < 1e-08f)
                    continue;
                const float t = -((DotZYX(plane.normal, start) + plane.d) / across);
                const S_vector hit(ray.x * t + start.x, ray.y * t + start.y, ray.z * t + start.z);
                SetXYZ(hits[i], hit);
                const float depth = DotZYX(p, plane.normal) - DotZYX(plane.normal, hit);
                m_groundDistance = depth;
                if (depth <= 0.0f) {
                    contact = true;
                    planes[i] = plane;
                    touching |= 1u << i;
                    if (m_pointActors[k]) {
                        pointActors[i] = m_pointActors[k];
                        m_pointOnActor[i] = m_pointOnActor[k];
                    }
                    break;
                }
            }
            continue;
        }
        // (The tests write m_normal and the point's hit directly; a miss leaves them.)
        g_collisionWorld.SetIgnoredVolume(&m_volume, 0);
        S_vector hitPos = XYZ(hits[i]);
        S_vector hitNormal = XYZ(m_normal);
        S_CollisionVolume *hit = CollisionRayTest(start, ray, &hitPos, &hitNormal, RAY_EXCLUDE_TYPES, RAY_EXCLUDE_FLAGS);
        SetXYZ(m_normal, hitNormal);
        SetXYZ(hits[i], hitPos);
        if (hit && (hit->flags & COLLISION_FLAG_WATER) != 0) {
            // Into water: a lighter fall, a fast body slowed, a splash; the ray again past the water.
            m_g = WATER_G;
            if (LengthOf((m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y) + m_velocity.z * m_velocity.z) >
                WATER_FAST) {
                m_velocity.x = m_velocity.x * WATER_SLOWING;
                m_velocity.y = m_velocity.y * WATER_SLOWING;
                m_velocity.z = m_velocity.z * WATER_SLOWING;
            }
            const S_vector splash(hits[i].x, hits[i].y + SPLASH_HEIGHT, hits[i].z);
            g_particlesModule.CreateAt(SPLASH_EFFECT, splash, S_vector(0.0f, 1.0f, 0.0f), nullptr, false, false);
            hit = CollisionRayTest(start, ray, &hitPos, &hitNormal, RAY_EXCLUDE_TYPES, RAY_EXCLUDE_FLAGS_DRY);
            SetXYZ(m_normal, hitNormal);
            SetXYZ(hits[i], hitPos);
        }
        g_collisionWorld.SetIgnoredVolume(nullptr, 0);
        SetXYZ(last, p);                                // (the original's w is a temporary's, unset)
        m_pointHit[i] = false;
        if (!hit)
            continue;
        S_CollisionVolume *volume = FilterContact(hit, hits[i], m_normal, &m_pointOnActor[i]);
        if (!volume)
            continue;
        const S_vector n = XYZ(m_normal);
        const S_vector h = XYZ(hits[i]);
        m_pointPlanes[i].normal = n;
        m_pointPlanes[i].d = -((h.z * n.z + n.y * h.y) + n.x * h.x);
        m_pointHit[i] = true;
        m_pointActors[i] = nullptr;
        if ((volume->flags & COLLISION_FLAG_DYNAMIC) != 0) {
            pointActors[i] = volume->actor;
            m_pointActors[i] = volume->actor;
        }
        m_pointSurface[i] = static_cast<uint16_t>(GetVolumeMaterial(*volume));
        const float depth = ((n.y * p.y + n.z * p.z) + n.x * p.x) - DotZYX(n, h);
        m_groundDistance = depth;
        if (depth <= 0.0f) {
            contact = true;
            // (Quirk kept: the plane's d is m_normal's w, never read.)
            planes[i].normal = n;
            planes[i].d = m_normal.w;
            touching |= 1u << i;
        }
    }
    if (contact) {
        // The impulses of the points in a surface moving into it; their actors' records.
        for (int i = RIGID_BODY_POINTS; i-- > 0;) {
            if ((touching & (1u << i)) == 0)
                continue;
            const S_vector &p = points[i];
            const float rx = p.x - m_centreWorld.x;
            const float ry = p.y - m_centreWorld.y;
            const float rz = p.z - m_centreWorld.z;
            const S_vector turning(rz * m_spin.y - ry * m_spin.z, rx * m_spin.z - rz * m_spin.x,
                                   ry * m_spin.x - rx * m_spin.y);
            const S_vector &n = planes[i].normal;
            if (((m_velocity.z - turning.z) * n.z + (m_velocity.y - turning.y) * n.y) + (m_velocity.x - turning.x) * n.x <
                0.0f) {
                // (The original passes the point's actor last, unless on a car's or an animation's
                // surface; Collide does not read it.)
                const S_vector4 at = {p.x, p.y, p.z, 0.0f};
                Collide(m_velocity, m_spin, n, at, dt, static_cast<int16_t>(i), 0);
            }
            C_Actor *actor = pointActors[i];
            if (!actor)
                continue;
            int k = m_actorContactCount;
            while (k-- > 0 && m_actorContacts[k].actor != actor) {
            }
            if (k >= 0) {
                m_actorContacts[k].time = CONTACT_TIME;
                continue;
            }
            if (m_actorContactCount >= m_actorContactsCapacity) {
                // (The original's _aligned_malloc / _aligned_free; the port's new[] / delete[].)
                m_actorContactsCapacity += m_actorContactsGrowBy;
                S_RigidBodyActorContact *grown = new S_RigidBodyActorContact[m_actorContactsCapacity];
                for (int j = 0; j < m_actorContactCount; ++j)
                    grown[j] = m_actorContacts[j];
                delete[] m_actorContacts;
                m_actorContacts = grown;
            }
            // (The record's +0x1C is the original's unset stack.)
            m_actorContacts[m_actorContactCount] = {actor->GetPosition4(), actor, CONTACT_TIME, i, 0};
            ++m_actorContactCount;
        }
        // Out of the surfaces: the deepest point's way out (on no actor's, or a car's or animation
        // actor's, surface).
        world = m_frame->GetMatrix();
        for (int i = RIGID_BODY_POINTS; i-- > 0;) {
            if ((touching & (1u << i)) == 0)
                continue;
            const S_vector p = XYZ(m_points[i]) * world;
            points[i] = p;
            const S_vector &n = planes[i].normal;
            const S_vector4 &h = hits[i];
            const float depth = ((p.z * n.z + p.y * n.y) + p.x * n.x) - ((h.z * n.z + h.y * n.y) + h.x * n.x);
            m_groundDistance = depth;
            if (!(depth <= 0.0f))
                continue;
            const float out = -depth;
            const S_vector away(out * n.x, out * n.y, out * n.z);
            if (pointActors[i] && !m_pointOnActor[i])
                continue;
            if ((push.z * push.z + push.y * push.y) + push.x * push.x <
                (away.z * away.z + away.y * away.y) + away.x * away.x)
                push = away;
        }
    }
    m_pos.x = push.x + m_pos.x;
    m_pos.y = push.y + m_pos.y;
    m_pos.z = push.z + m_pos.z;
    // In water (g under 10): faster than 0.5 m/s, slowed by 200 m/s per second down to it.
    if (m_g < 10.0f) {
        const float speed =
            LengthOf((m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y) + m_velocity.z * m_velocity.z);
        if (speed > WATER_MIN_SPEED) {
            S_vector v = m_velocity;
            NormalizeBy(v, (v.x * v.x + v.y * v.y) + v.z * v.z);
            float slower = speed - dt * WATER_DRAG;
            if (slower < WATER_MIN_SPEED)
                slower = WATER_MIN_SPEED;
            m_velocity = S_vector(slower * v.x, slower * v.y, slower * v.z);
        }
    }
    m_frame->SetPos(XYZ(m_pos));
    m_frame->Update();
    m_matrix = m_frame->GetMatrix();
    const S_matrix source = m_matrix;
    m_invMatrix = m_matrix;
    m_invMatrix.Inverse(source);
    m_rotation = m_matrix;
    m_rotation.m[3][0] = m_rotation.m[3][1] = m_rotation.m[3][2] = 0.0f;
    m_invRotation = m_invMatrix;
    m_invRotation.m[3][0] = m_invRotation.m[3][1] = m_invRotation.m[3][2] = 0.0f;
    if (m_hasCentre)
        SetXYZ(m_centreWorld, m_centre * m_matrix);
    else
        m_centreWorld = m_pos;
    m_contactsChanged = false;
}
