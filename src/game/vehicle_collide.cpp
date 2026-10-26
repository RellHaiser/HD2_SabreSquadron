// C_Vehicle: the body against the world (original 0x00514d70 CollideBody, with its work lists'
// insert helper 0x005172d0; the lists' atexit frees 0x00517290 / 0x005172b0 are not needed here).
#include "game/vehicle.h"

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "game/collision.h"                             // CollisionSphereTest, g_collisionWorld
#include "game/pending.h"                               // g_carSurfaces
#include "ls3d/i3d.h"

namespace {

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

void SetXYZ(S_vector4 &v, const S_vector &xyz)
{
    v.x = xyz.x;
    v.y = xyz.y;
    v.z = xyz.z;
}

S_vector4 Vector4(const S_vector &v) { return {v.x, v.y, v.z, 0.0f}; }

// Dot products and squares in the orders the original sums them.
float DotZYX(const S_vector &a, const S_vector &b) { return (a.z * b.z + a.y * b.y) + a.x * b.x; }
float DotXZY(const S_vector &a, const S_vector &b) { return (a.x * b.x + a.z * b.z) + a.y * b.y; }
float SquareXYZ(const S_vector &v) { return (v.x * v.x + v.y * v.y) + v.z * v.z; }
float SquareXZY(const S_vector &v) { return (v.x * v.x + v.z * v.z) + v.y * v.y; }
float SquareZYX(const S_vector &v) { return (v.z * v.z + v.y * v.y) + v.x * v.x; }

// S_vector::Magnitude and Normalize from a square summed in another order.
float LengthOf(float square)
{
    if (square < 1e-8f)
        return 0.0f;
    if (std::fabs(square - 1.0f) < 1e-8f)
        return 1.0f;
    return std::sqrt(square);
}

void NormalizeBy(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-8f)
        return;
    if (square >= 1e-8f)
        v *= 1.0f / std::sqrt(square);
    else if (v.x != 0.0f)
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    else if (v.z != 0.0f)
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    else
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
}

// The work lists keep the farthest first and give up their last: an entry goes in after the
// last one farther than it (of equal ones the one in first comes out first).
template <class T>
void InsertByDistance(std::vector<std::pair<T *, float>> &list, T *item, float distance)
{
    size_t at = list.size();
    while (at > 0 && !(distance < list[at - 1].second))
        --at;
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(at), {item, distance});
}

}  // namespace

bool C_Vehicle::CollideBody(S_BodyCollisionBox *boxes, S_vector *push, S_vector *push2, float *maxImpulse,
                            S_vector *maxImpulseVector, const S_vector4 *up, int ms)
{
    if (!boxes)
        return false;
    bool collided = false;
    const S_matrix world = m_model->GetMatrix();
    S_matrix inverse;
    inverse.Inverse(world);

    // Where each box was before the step (through the pose then; one on a part through the part
    // now, as if the part had moved with the body) and how far it moved.
    for (S_BodyCollisionBox *box = boxes; box; box = box->next) {
        if (!box->parent || !box->parent->IsOn()) {
            SetXYZ(box->start, box->pos * m_previousModelMatrix);
            SetXYZ(box->move, box->pos * world);
        } else {
            SetXYZ(box->move, box->pos * box->parent->GetMatrix());
            S_vector start = XYZ(box->move) * inverse;
            start *= m_previousModelMatrix;
            SetXYZ(box->start, start);
        }
        SetXYZ(box->move, XYZ(box->move) - XYZ(box->start));
    }

    // A point 1 km ahead along the velocity from the centre of mass, carried to the pose now: the
    // boxes and points nearest it are tested first.
    S_vector ahead = XYZ(m_velocity);
    NormalizeBy(ahead, SquareZYX(ahead));
    S_vector target(ahead.x * 1000.0f + m_unknown1B0.x, ahead.y * 1000.0f + m_unknown1B0.y,
                    ahead.z * 1000.0f + m_unknown1B0.z);
    target *= m_invModelMatrix;
    target *= world;

    // Fix: the work lists are this call's (the original's are function statics that the
    // recursion into a box's children frees: with more boxes of the list waiting it then read
    // through a null pointer; no shipped model has child boxes). The normal and the actor met
    // are locals too (statics there).
    std::vector<std::pair<S_BodyCollisionBox *, float>> boxList;
    for (S_BodyCollisionBox *box = boxes; box; box = box->next) {
        if (box->parent && !box->parent->IsOn())
            continue;                                   // on a part switched off
        InsertByDistance(boxList, box, LengthOf(SquareXZY(target - XYZ(box->start))) - box->halfHeight);
    }

    const auto ignoreOwnVolumes = [this] {
        if (!m_specialVolumesOn)
            g_collisionWorld.SetIgnoredVolume(&m_bodyVolume, 0);
        else
            g_collisionWorld.SetIgnoredVolumes(m_specialVolumeList.data(),
                                               static_cast<uint32_t>(m_specialVolumeList.size()), 0);
    };
    const uint32_t excludeTypes = static_cast<uint32_t>(m_unknown53C);
    const uint32_t excludeFlags = static_cast<uint32_t>(m_unknown540);

    while (!boxList.empty()) {
        S_BodyCollisionBox *box = boxList.back().first;
        boxList.pop_back();
        // A 'B' box swept from the pose before the step to the pose now, the others a sphere.
        ignoreOwnVolumes();
        bool touched;
        if (!box->flagB) {
            S_vector hitPos = XYZ(box->start), normal;
            touched = CollisionSphereTest(XYZ(box->start), XYZ(box->move), box->halfHeight, &hitPos, &normal,
                                          excludeTypes, excludeFlags) != nullptr;
        } else {
            I3D_bbox local;
            local.min = box->min;
            local.minW = box->unknown0C;
            local.max = box->max;
            local.maxW = box->unknown1C;
            if (box->parent) {
                // Through the part into the body's space.
                local.min *= box->parent->GetMatrix();
                local.max *= box->parent->GetMatrix();
                local.min *= inverse;
                local.max *= inverse;
            }
            touched = g_collisionWorld.TestBoxSweep(local, m_previousModelMatrix, world, box->move, XYZ(*up),
                                                    XYZ(m_modelAxisY), excludeTypes, excludeFlags);
        }
        g_collisionWorld.SetIgnoredVolume(nullptr, 0);
        if (!touched)
            continue;

        // Its points, nearest the target first (those on a part through the part now).
        std::vector<std::pair<S_BodyCollisionPoint *, float>> pointList;
        for (S_BodyCollisionPoint *point = box->points; point; point = point->next) {
            if (!box->parent) {
                SetXYZ(point->start, point->pos * m_previousModelMatrix);
                SetXYZ(point->move, point->pos * world);
            } else {
                SetXYZ(point->move, point->pos * box->parent->GetMatrix());
                S_vector start = XYZ(point->move) * inverse;
                start *= m_previousModelMatrix;
                SetXYZ(point->start, start);
            }
            SetXYZ(point->move, XYZ(point->move) - XYZ(point->start));
            InsertByDistance(pointList, point, LengthOf(SquareXZY(target - XYZ(point->start))) - point->radius);
        }
        while (!pointList.empty()) {
            S_BodyCollisionPoint *point = pointList.back().first;
            pointList.pop_back();
            ignoreOwnVolumes();
            S_vector hitPos, normal;
            S_CollisionVolume *hit = CollisionSphereTest(XYZ(point->start), XYZ(point->move), point->radius, &hitPos,
                                                         &normal, excludeTypes, excludeFlags);
            g_collisionWorld.SetIgnoredVolume(nullptr, 0);
            if (auto callback = reinterpret_cast<T_VehicleBodyGroundCallback>(m_callbacks[VEHICLE_CALLBACK_BODY_GROUND]))
                if (hit)
                    hit = callback(m_callbackContext, hit, &hitPos, &normal);
            if (hit && BodyContact(*point, hit, hitPos, normal, push, push2, maxImpulse, maxImpulseVector))
                collided = true;
        }
        // Then the boxes below it.
        if (box->children &&
            CollideBody(box->children, push, push2, maxImpulse, maxImpulseVector, up, ms))
            collided = true;
    }
    return collided;
}

bool C_Vehicle::BodyContact(const S_BodyCollisionPoint &point, S_CollisionVolume *hit, const S_vector &hitPos,
                            const S_vector &normal, S_vector *push, S_vector *push2, float *maxImpulse,
                            S_vector *maxImpulseVector)
{
    if (hit->flags & COLLISION_FLAG_WATER)
        m_wheelOnWater = 1;
    const int material = GetVolumeMaterial(*hit);
    C_Actor *actor = (hit->flags & COLLISION_FLAG_DYNAMIC) ? hit->actor : nullptr;
    const float radius = point.radius;
    // The sphere's side towards the surface before the step, its arm from the centre of mass and
    // its velocity there.
    const S_vector contact = XYZ(point.start) - normal * radius;
    const S_vector4 contact4 = Vector4(contact);
    const S_vector arm = contact - XYZ(m_unknown1B0);
    const S_vector velocity = XYZ(m_velocity) + arm.Cross(m_unknown210);
    if (!(DotXZY(velocity, normal) < 0.0f))
        return false;                                   // not moving into it

    bool deform = true;
    // A sphere already over 5 cm in (the sphere test moved its centre out to `hitPos`) with the
    // centre of mass outside: the way out.
    S_vector out(0.0f, 0.0f, 0.0f);
    const S_vector surface = hitPos - normal * radius;
    const float depth = radius - (DotZYX(normal, XYZ(point.start)) - DotZYX(normal, surface));
    if (depth > 0.05f && DotXZY(XYZ(m_unknown1B0) - hitPos, normal) >= 0.0f)
        out = normal * depth;

    // An actor's volume: whether it counts as the world.
    bool firm = false;
    if (actor) {
        // Fix: an empty slot counts it as the world (the original calls the slot unchecked).
        auto callback = reinterpret_cast<T_VehicleActorFirmCallback>(m_callbacks[VEHICLE_CALLBACK_ACTOR_FIRM]);
        firm = callback ? callback(m_callbackContext, hit, &deform) : true;
    }

    // The body's turn for a moment (in the world): into its space, through the inverse inertia
    // with the loads, back.
    const auto turnFor = [this](S_vector moment) {
        moment *= m_unknown350;
        moment = S_vector(moment.x * m_unknown220.x, moment.y * m_unknown220.y, moment.z * m_unknown220.z);
        moment *= m_unknown310;
        return moment;
    };
    // The largest impulse; then the dent and the wear (unless the actor callback cleared
    // `deform`), the impact callback and the sound.
    const auto impact = [&](const S_vector &impulse, float size, C_Actor *with, const S_CollisionVolume *on) {
        if (size > *maxImpulse) {
            *maxImpulse = size;
            *maxImpulseVector = impulse;
        }
        // Quirk kept: the dent and the wear take the largest impulse of the step so far.
        if (deform) {
            if (m_unknownF6C & VEHICLE_SIM_DEFORM)
                Deform(contact4, Vector4(normal), radius, *maxImpulse, 0, nullptr);
            if (m_unknownF6C & VEHICLE_SIM_IMPACT_DAMAGE)
                ImpactDamage(*maxImpulse);
        }
        if (auto callback = reinterpret_cast<T_VehicleImpactCallback>(m_callbacks[VEHICLE_CALLBACK_IMPACT]))
            callback(m_callbackContext, &impulse, maxImpulseVector, &contact4, radius, &velocity, with, on, 0);
        if (m_soundsOn)
            PlayImpactSound(size, m_unknown2293 != 0);
    };
    const S_vector lever = turnFor(arm.Cross(normal)).Cross(arm);

    if (!actor || firm) {
        // The world: the impulse that leaves the point the surface's bounce (1 + restitution)
        // times its speed into it.
        const S_CarSurface *ground = m_unknown228C ? nullptr : &g_carSurfaces[g_carSurfaceOfMaterial[material]];
        const float speed = DotXZY(velocity, normal) * -(m_unknown228C ? m_unknown6F8 : ground->bounce);
        float resistance = DotXZY(lever, normal) + 1.0f / m_currentMass;
        if (std::fabs(resistance) < 1e-6)
            resistance = 1e-6f;
        const S_vector impulse = normal * (speed / resistance);
        const float inverseMass = 1.0f / m_currentMass;
        SetXYZ(m_velocity, XYZ(m_velocity) + impulse * inverseMass);
        m_unknown210 -= turnFor(arm.Cross(impulse));
        const float size = LengthOf(SquareZYX(impulse));
        impact(impulse, size, nullptr, hit);
        if (size / m_currentMass > 5.0f || m_speed > 3.0f) {
            S_VehicleBodyContact record = {};
            record.point = contact4;
            record.material = material;
            m_bodyContacts.push_back(record);
        }
        if (SquareXYZ(*push) < SquareZYX(out)) {
            bool pushes = true;
            if (actor) {
                // The callback's answers but the last are not used. (An empty slot pushes, as for
                // the world; the original calls it unchecked.)
                S_vector4 arm2, velocity2, turn2;
                float mass2, scale2;
                if (auto callback = reinterpret_cast<T_VehicleActorResponseCallback>(
                        m_callbacks[VEHICLE_CALLBACK_ACTOR_RESPONSE])) {
                    pushes = false;
                    // Quirk kept: this slot gets the vehicle, not m_callbackContext.
                    callback(this, hit, &contact4, &normal, &arm2, &velocity2, &mass2, &turn2, &scale2, &pushes);
                }
            }
            if (pushes)
                *push = out;
        }
        if (firm)
            if (auto callback = reinterpret_cast<T_VehicleActorTouchCallback>(m_callbacks[VEHICLE_CALLBACK_ACTOR_TOUCH]))
                callback(m_callbackContext, hit, &contact4, &m_velocity, &normal);

        // The friction along the surface against the sliding: the impulse's size times the
        // friction, by the sine of the angle between the contact's velocity and the normal.
        S_vector slide = velocity;
        NormalizeBy(slide, SquareZYX(slide));
        S_vector along = slide.Cross(normal).Cross(normal);
        NormalizeBy(along, SquareZYX(along));
        S_vector force = along * (LengthOf(SquareZYX(impulse)) * (m_unknown228C ? m_unknown6F4 : ground->friction));
        force *= std::sin(velocity.AngleTo(normal));
        SetXYZ(m_velocity, XYZ(m_velocity) + force * inverseMass);
        m_unknown210 += turnFor(force.Cross(arm));
        return true;
    }

    // A moving actor: the impulse shared by both (its side from the callback; scaled for the body
    // after the actor's part went out).
    S_vector4 otherArm = {}, otherVelocity = {}, otherTurn = {};
    float otherMass = 0.0f, scale = 0.0f;
    bool pushes = false;
    // Fix: an empty slot means no contact (the original calls the slot unchecked).
    auto response = reinterpret_cast<T_VehicleActorResponseCallback>(m_callbacks[VEHICLE_CALLBACK_ACTOR_RESPONSE]);
    // Quirk kept: this slot gets the vehicle, not m_callbackContext.
    if (!response || !response(this, hit, &contact4, &normal, &otherArm, &otherVelocity, &otherMass, &otherTurn,
                               &scale, &pushes))
        return false;
    float speed = DotXZY(velocity - XYZ(otherVelocity), normal);
    if (speed > 0.0f)
        speed *= 0.01f;
    const S_vector otherLever = XYZ(otherTurn).Cross(XYZ(otherArm));
    float resistance = (((((1.0f / otherMass + 1.0f / m_currentMass) + otherLever.z * normal.z) + otherLever.y * normal.y) +
                         lever.z * normal.z) +
                        lever.y * normal.y);
    resistance = (resistance + normal.x * otherLever.x) + normal.x * lever.x;
    if (std::fabs(resistance) < 1e-6)
        resistance = 1e-6f;
    S_vector impulse = normal * (speed / resistance);
    // Fix: an empty slot is skipped (called unchecked in the original).
    if (auto callback = reinterpret_cast<T_VehicleActorImpulseCallback>(m_callbacks[VEHICLE_CALLBACK_ACTOR_IMPULSE])) {
        const S_vector4 at = Vector4(surface);
        const S_vector4 impulse4 = Vector4(impulse);
        callback(m_callbackContext, hit, &at, &impulse4, &normal, m_currentMass);
    }
    impulse *= scale;
    const float inverseMass = 1.0f / m_currentMass;
    SetXYZ(m_velocity, XYZ(m_velocity) + impulse * inverseMass);
    m_unknown210 -= turnFor(arm.Cross(impulse));
    impact(impulse, LengthOf(SquareXZY(impulse)), actor, nullptr);
    if (pushes && SquareXYZ(*push2) < SquareZYX(out))
        *push2 = out;
    return true;
}

void C_Vehicle::GetContactResponse(const S_vector4 &point, const S_vector &normal, S_vector4 *arm,
                                   S_vector4 *velocity, float *mass, S_vector4 *turn) const   // 0x004f32a0
{
    const S_vector lever = XYZ(point) - XYZ(m_unknown1B0);
    *arm = {lever.x, lever.y, lever.z, 0.0f};
    *velocity = m_velocity;
    SetXYZ(*velocity, XYZ(m_velocity) - m_unknown210.Cross(lever));
    *mass = m_currentMass;
    // The turn per impulse: into the body's space, through the inverse inertia, back.
    S_vector t = lever.Cross(normal);
    t *= m_unknown350;
    t = S_vector(m_unknown220.x * t.x, m_unknown220.y * t.y, m_unknown220.z * t.z);
    t *= m_unknown310;
    *turn = {t.x, t.y, t.z, 0.0f};
}
