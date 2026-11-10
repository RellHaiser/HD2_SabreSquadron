// C_Vehicle: the wheels' ground contacts (original 0x00512390 UpdateWheelContacts, 0x00500eb0
// ImpactDamage). The body's deformation (0x004ff8b0) and the bump sound (0x0051b560) are later
// parts.
#include "game/vehicle.h"

#include <cmath>
#include <vector>

#include "game/collision.h"                             // CollisionRayTest, GetVolumeMaterial
#include "game/pending.h"                               // g_tyreTypes
#include "ls3d/i3d.h"

namespace {

constexpr float HALF_PI = 1.5707964f;                   // 0x00810180

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

void SetXYZ(S_vector4 &v, const S_vector &xyz)
{
    v.x = xyz.x;
    v.y = xyz.y;
    v.z = xyz.z;
}

// Dot products and squares in the orders the original sums them.
float DotZYX(const S_vector &a, const S_vector &b) { return (a.z * b.z + a.y * b.y) + a.x * b.x; }
float SquareZYX(const S_vector &v) { return (v.z * v.z + v.y * v.y) + v.x * v.x; }
float SquareXZY(const S_vector &v) { return (v.x * v.x + v.z * v.z) + v.y * v.y; }
float SquareYZX(const S_vector &v) { return (v.y * v.y + v.z * v.z) + v.x * v.x; }

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

// Where the ray from `start` crosses `plane`: false when it runs along the plane or the point lies
// farther from the start than the ray reaches. (The rest pass sums the distance in another order.)
bool MeetPlane(const S_plane &plane, const S_vector &start, const S_vector &ray, bool restPass, S_vector *hit)
{
    const float along = DotZYX(ray, plane.normal);
    if (std::fabs(along) < 1e-8f)
        return false;
    const float t = -((DotZYX(start, plane.normal) + plane.d) / along);
    *hit = start + ray * t;
    const S_vector away = *hit - start;
    return !(SquareZYX(ray) < (restPass ? SquareZYX(away) : SquareXZY(away)));
}

// How far down from its centre a wheel standing on ground of `normal` reaches: the tyre's edge
// (unknown1E4 from the centre at unknown1EC to the axis; the rim's on a flat) as the wheel tilts.
float TiltedReach(const S_VehicleWheel &w, const S_vector &normal)
{
    const float angle = w.frame->GetMatrix()(0).AngleTo(normal);
    const float edge = angle <= HALF_PI ? angle + w.unknown1EC : angle - w.unknown1EC;
    return std::cos(edge - HALF_PI) * ((w.flags & WHEEL_ON_RIM) ? w.unknown1E8 : w.unknown1E4);
}

// The length of a wheel's ray: its reach (the rim's on a flat), unknown25C[2], the track strips'
// unknown1E0, the travel and 1 more.
float RayLength(const S_VehicleWheel &w)
{
    return ((((w.flags & WHEEL_ON_RIM) ? w.unknown1E8 : w.unknown1E4) + w.unknown25C[2]) + w.unknown1E0 +
            w.unknown164) +
           1.0f;
}

// The tyre type's entry for `material` (-1: none). Fix: a type without a table has none (the
// original reads through it).
int FindSurface(const S_VehicleWheel &w, int material)
{
    const std::vector<S_TyreSurface> *surfaces = g_tyreTypes[w.tyreIndex].surfaces;
    int entry = surfaces ? static_cast<int>(surfaces->size()) : 0;
    while (--entry >= 0 && (*surfaces)[entry].surface != material) {
    }
    return entry;
}

}  // namespace

void C_Vehicle::UpdateWheelContacts(S_vector4 *headingSum, float timeDelta, const S_vector4 *down,
                                    bool bumps)                                         // 0x00512390 (DS 0x00501b20)
{
    const S_vector y = XYZ(m_modelAxisY);
    const S_vector downward = XYZ(*down);
    // Where a wheel's ray starts: 0.2 above its holder's height, over the wheel.
    auto rayStart = [&](const S_VehicleWheel &w) {
        S_vector p;
        if (w.holder->GetParent() == m_model) {
            p = w.frame->GetPos();
            p *= w.holder->GetLocalMatrix();
        } else {
            p = w.frame->GetWorldPos();
            p *= m_invModelMatrix;
        }
        p.y = w.modelPos.y + 0.2f;
        p *= m_modelMatrix;
        return p;
    };

    for (int i = m_wheelCount; i-- > 0;) {
        S_VehicleWheel *w = m_wheels[i];
        if (!w)                                         // Fix: the original crashes on a missing wheel
            continue;
        w->holder->Update();
        w->frame->Update();
        w->unknown060 = w->unknown050;
        if (m_unknownF6C & VEHICLE_SIM_RAY_EVERY_WHEEL)
            w->groundPlaneVolume = nullptr;
    }
    const bool everyWheel = (m_unknownF6C & VEHICLE_SIM_RAY_EVERY_WHEEL) != 0;
    if (!everyWheel && (!(m_unknownF6C & VEHICLE_SIM_GROUND_FIRST_SUBSTEP) || m_unknown2282 == 1) &&
        (m_unknownF6C & VEHICLE_SIM_WHEEL_PLANES)) {
        // The wheel planes' clock: past +0xF50 the other half of the wheels casts again.
        m_unknownF4C = timeDelta + m_unknownF4C;
        if (m_unknownF4C > m_unknownF50)
            m_unknown22C8 = !m_unknown22C8;
    }

    // --- the ground's planes ------------------------------------------------------------------
    bool resting = false;
    if (m_wheelCount != 0 && !(m_unknown600 & VEHICLE_PHYSICS_IN_WATER)) {
        bool still = m_speed < 1.5626f;
        for (int i = m_wheelCount; still && i-- > 0;) {
            const S_VehicleWheel *w = m_wheels[i];
            if (w && !(w->flags & (WHEEL_DETACHED | WHEEL_DUMMY)) &&
                (w->unknown0A0.Square() > 1.5625f || w->unknown0D0.Square() > 0.0025f))
                still = false;
        }
        if (still) {
            // At rest: each wheel on the ground stays on its own plane (placed there, no ray this
            // pass) while within its reach. The plane goes through m_frontGround, the reach through
            // m_maxWheelRadius (Quirk kept: both left with the last wheel's).
            resting = true;
            for (int i = m_wheelCount; i-- > 0;) {
                S_VehicleWheel *w = m_wheels[i];
                if (!w || (w->flags & WHEEL_DETACHED) || !(w->flags & WHEEL_ON_GROUND) || (w->flags & WHEEL_DUMMY))
                    continue;
                m_frontGround.normal = w->unknown080;
                m_frontGround.d = -DotZYX(m_frontGround.normal, XYZ(w->unknown050));
                const S_vector start = rayStart(*w);
                const S_vector ray = downward * RayLength(*w);
                S_vector hit;
                if (!MeetPlane(m_frontGround, start, ray, true, &hit))
                    continue;
                m_maxWheelRadius = TiltedReach(*w, w->unknown080);
                const float distance = LengthOf(SquareZYX(hit - start));
                if (!(distance <= (((m_maxWheelRadius + w->unknown25C[2]) + w->unknown1E0) + w->unknown164) + 0.2f))
                    continue;
                w->flags |= WHEEL_ON_REST_PLANE;
                const S_vector centre = hit + y * ((m_maxWheelRadius + w->unknown25C[2]) + w->unknown1E0);
                SetXYZ(w->unknown050, hit);
                const S_vector moved = centre - XYZ(w->worldPos);
                w->unknown0C0 += moved;
                w->unknown0D0 += moved;
                SetXYZ(w->worldPos, centre);
                w->groundPlane = m_frontGround;
                w->groundPlaneVolume = w->ground;
            }
        } else if (!everyWheel && (!(m_unknownF6C & VEHICLE_SIM_GROUND_FIRST_SUBSTEP) || m_unknown2282 == 1) &&
                   !(m_unknownF6C & VEHICLE_SIM_WHEEL_PLANES)) {
            // The ground under the axle centres (3 down).
            const S_vector ray = downward * 3.0f;
            auto axleGround = [&](const S_vector4 &centre, S_plane &plane, S_CollisionVolume *&volume) {
                S_vector hit, normal;
                volume = CollisionRayTest(XYZ(centre) * m_modelMatrix, ray, &hit, &normal, m_unknown534,
                                          static_cast<uint32_t>(m_unknown538));
                if (volume) {
                    plane.normal = normal;
                    plane.d = -DotZYX(hit, normal);
                }
            };
            axleGround(m_frontAxleCentre, m_frontGround, m_frontGroundVolume);
            axleGround(m_rearAxleCentre, m_rearGround, m_rearGroundVolume);
        }
    }

    // --- each wheel's ground --------------------------------------------------------------------
    // The wheel's own ray (every wheel, at rest, in water; with the wheel planes in turns or without
    // a kept plane), else its kept plane, else its axle's plane (the rear one for wheels not ahead of
    // the centre of mass).
    for (int i = m_wheelCount; i-- > 0;) {
        S_VehicleWheel *w = m_wheels[i];
        if (!w)                                         // Fix: the original crashes on a missing wheel
            continue;
        if ((w->flags & WHEEL_DETACHED) ||
            ((w->flags & WHEEL_ON_REST_PLANE) && !(m_unknown600 & VEHICLE_PHYSICS_IN_WATER)) ||
            (w->flags & WHEEL_DUMMY))
            continue;
        const S_vector ray = downward * RayLength(*w);
        const S_vector start = rayStart(*w);
        const uint32_t options = m_unknownF6C;
        const bool cast = (options & (VEHICLE_SIM_RAY_EVERY_WHEEL | VEHICLE_SIM_WHEEL_PLANES)) || resting ||
                          (m_unknown600 & VEHICLE_PHYSICS_IN_WATER);
        bool castNow = false;
        if (cast) {
            if (!(options & VEHICLE_SIM_WHEEL_PLANES) || !w->groundPlaneVolume) {
                castNow = true;
            } else if (!(options & VEHICLE_SIM_GROUND_FIRST_SUBSTEP) || m_unknown2282 == 1) {
                // The half whose turn it is: odd or even wheels by +0x22C8, the other way round for
                // wheels 2 and 3 of every four.
                const bool flipped = (m_unknown22C8 != 0) != (i % 4 >= 2);
                castNow = (i % 2 != 0) == flipped && m_unknownF4C > m_unknownF50;
            }
        }
        S_CollisionVolume *ground = nullptr;
        S_vector hit(0.0f, 0.0f, 0.0f);
        S_vector normal(0.0f, 0.0f, 0.0f);
        if (castNow) {
            ground = CollisionRayTest(start, ray, &hit, &normal, m_unknown534, static_cast<uint32_t>(m_unknown538));
        } else if (options & VEHICLE_SIM_WHEEL_PLANES) {
            ground = w->groundPlaneVolume;
            if (MeetPlane(w->groundPlane, start, ray, false, &hit))
                normal = w->groundPlane.normal;
            else
                ground = nullptr;
        } else {
            const bool rear = w->modelPos.z <= m_centreOfMass.z;
            ground = rear ? m_rearGroundVolume : m_frontGroundVolume;
            const S_plane &plane = rear ? m_rearGround : m_frontGround;
            if (ground) {
                if (MeetPlane(plane, start, ray, false, &hit))
                    normal = plane.normal;
                else
                    ground = nullptr;
            }
        }

        // Kept as the wheel's plane; too far for the tilted wheel to reach: no ground.
        float reach = 0.0f;
        if (!ground) {
            w->groundPlaneVolume = nullptr;
        } else {
            w->groundPlane.normal = normal;
            w->groundPlaneVolume = ground;
            w->groundPlane.d = -DotZYX(hit, normal);
            reach = TiltedReach(*w, normal);
#ifdef HD2_DEDICATED   // the server sums the squares z, y, x
            const float distance = LengthOf(SquareZYX(hit - start));
#else
            const float distance = LengthOf(SquareXZY(hit - start));
#endif
            if ((((reach + w->unknown25C[2]) + w->unknown1E0) + w->unknown164) + 0.2f < distance)
                ground = nullptr;
        }
        w->ground = ground;
        if (ground) {
            if (auto callback = reinterpret_cast<T_VehicleGroundCallback>(m_callbacks[VEHICLE_CALLBACK_WHEEL_GROUND]))
                ground = callback(m_callbackContext, ground, i);
        }
        if (ground && w->tyreType >= 0) {
            w->unknown080 = normal;
            if (!(w->flags & WHEEL_ON_GROUND))
                w->unknown090 = normal;
            w->flags |= WHEEL_ON_GROUND;
            const int material = GetVolumeMaterial(*ground);
            if (w->surface != material) {
                if (w->tyreIndex > 0)
                    w->surfaceEntry = FindSurface(*w, material);
                w->unknown26C = (w->unknown25C[1] + w->unknown25C[0]) + 1.0f;
            }
            w->surface = material;
            SetXYZ(w->unknown050, hit);
            if (w->tyreIndex > 0 && w->surfaceEntry < 0) {
                // A surface the tyre has no values for: no contact (at the ray's end).
                SetXYZ(w->unknown050, (start + ray) + y);
                w->flags &= ~WHEEL_ON_GROUND;
            }
        } else {
            // Off the ground: the wheel hangs at its full travel.
            reach = (w->flags & WHEEL_ON_RIM) ? w->unknown1E8 : w->unknown1E4;
            SetXYZ(w->unknown050, ((start + ray) + y) - y * 0.2f);
            w->flags &= ~WHEEL_ON_GROUND;
        }
        const S_vector centre = XYZ(w->unknown050) + y * ((reach + w->unknown25C[2]) + w->unknown1E0);
        w->unknown0C0 += centre - XYZ(w->worldPos);
        SetXYZ(w->worldPos, centre);
        w->unknown0D0 = S_vector(0.0f, 0.0f, 0.0f);
    }
    if (m_unknownF4C > m_unknownF50)
        m_unknownF4C = 0.0f;

    // --- the suspension ------------------------------------------------------------------------
    // Each wheel's holder follows the wheel (never above its place); the extension gives the load,
    // damped. A wheel bottoming out (or on a face over 60° from the car's y) with `bumps` stops the
    // body moving into the ground with an impulse at the contact (averaged over such wheels): the
    // deformation, the wear, the impact callback, the bump sound; no damping then.
    S_vector heading(0.0f, 0.0f, 0.0f);
    S_vector pushVelocity(0.0f, 0.0f, 0.0f);
    S_vector pushTurn(0.0f, 0.0f, 0.0f);
    int pushes = 0;
    float bumpVolume = 0.0f;
    for (int i = m_wheelCount; i-- > 0;) {
        S_VehicleWheel *w = m_wheels[i];
        if (!w)                                         // Fix: the original crashes on a missing wheel
            continue;
        if (!(w->flags & (WHEEL_DETACHED | WHEEL_DUMMY))) {
            S_vector local = XYZ(w->worldPos) * m_invModelMatrix;
            if (!(local.y <= w->modelPos.y))
                local.y = w->modelPos.y;
            I3D_frame *holder = w->holder;
            if (holder->GetParent() == m_model) {
                holder->SetPosInline(S_vector(w->modelPos.x, local.y, w->modelPos.z));
            } else {
                // Along the holder's own y axis.
                const S_matrix &matrix = holder->GetLocalMatrix();
                const float lowered = local.y - w->modelPos.y;
                holder->SetPosInline(S_vector(matrix.m[1][0] * lowered + w->holderPos.x,
                                              lowered * matrix.m[1][1] + w->holderPos.y,
                                              lowered * matrix.m[1][2] + w->holderPos.z));
            }
            float extension = w->modelPos.y - local.y;
            if (!(extension < w->unknown164))
                extension = w->unknown164;
            float rate = (w->extension - extension) / timeDelta;   // compressing: positive
            const bool steep = (w->flags & WHEEL_ON_GROUND) && DotZYX(w->unknown080, y) < 0.5f;
            if ((extension <= 0.005f || steep) && bumps) {
                if (!steep)
                    extension = 0.0f;
                if (!(w->flags & WHEEL_BUMPED)) {
                    w->flags |= WHEEL_BUMPED;
                    const S_vector arm = XYZ(w->unknown050) - XYZ(m_unknown1B0);
                    const S_vector velocity = XYZ(m_velocity) - m_unknown210.Cross(arm);
                    S_vector normal = w->unknown080;
                    // A face turned far from the step before's: along the car's y by the normal's share.
                    if (DotZYX(w->unknown090, normal) < 0.86f)
                        normal = y * DotZYX(y, normal);
                    const float into = DotZYX(velocity, normal);
                    if (into < 0.0f) {
                        const float speed = std::fabs(into * -1.0f);
                        // The impulse that stops the contact moving into the ground.
                        S_vector lever = arm.Cross(normal);
                        lever *= m_unknown350;
                        lever = S_vector(lever.x * m_unknown220.x, lever.y * m_unknown220.y, lever.z * m_unknown220.z);
                        lever *= m_unknown310;
                        const S_vector spin = lever.Cross(arm);
                        const float inverseMass = 1.0f / m_currentMass;
                        float resistance = ((spin.x * normal.x + spin.z * normal.z) + spin.y * normal.y) + inverseMass;
                        if (std::fabs(resistance) < 1e-5f)
                            resistance = 1e-5f;
                        const float size = speed / resistance;
                        const S_vector impulse = normal * size;
                        pushVelocity += impulse * inverseMass;
                        S_vector turn = arm.Cross(impulse);
                        turn *= m_unknown350;
                        turn = S_vector(turn.x * m_unknown220.x, turn.y * m_unknown220.y, turn.z * m_unknown220.z);
                        turn *= m_unknown310;
                        pushTurn -= turn;
                        ++pushes;
                        const float strength = LengthOf(SquareXZY(impulse));
                        if (m_unknownF6C & VEHICLE_SIM_DEFORM)
                            Deform(w->worldPos, m_modelAxisY, 0.2f, strength, 0, nullptr);
                        if (m_unknownF6C & VEHICLE_SIM_IMPACT_DAMAGE) {
                            ImpactDamage(strength);
                            if (auto callback = reinterpret_cast<T_VehicleImpactCallback>(m_callbacks[VEHICLE_CALLBACK_IMPACT]))
                                callback(m_callbackContext, &impulse, &impulse, &w->worldPos, 0.2f, &velocity, 0, nullptr, 0);
                        }
                        rate = 0.0f;
                        if ((m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS) && m_soundsOn && speed > 1.0f) {
                            const float volume = speed <= 7.0f ? (speed - 1.0f) * 0.16666667f : 1.0f;
                            if (bumpVolume < volume)
                                bumpVolume = volume;
                        }
                    }
                }
            }
            w->unknown090 = w->unknown080;
#ifndef HD2_DEDICATED   // no sounds on the server
            // Quirk kept: once loud enough, the sound plays again for each later wheel of the pass.
            if (bumpVolume > 0.2f)
                PlayBumpSound(bumpVolume);
#endif
            const float spring = (w->unknown164 - extension) * w->unknown160;
            if (rate < 0.0f) {
                w->unknown168 = rate * w->unknown174 + spring;
            } else {
                w->unknown168 = spring;
                if (!(w->flags & WHEEL_BUMPED))
                    w->unknown168 = rate * w->unknown170 + spring;
            }
            if (w->unknown168 < 0.0f)
                w->flags &= ~WHEEL_ON_GROUND;
            w->extension = extension;
            w->flags &= ~WHEEL_BUMPED;

            // The contact's velocity and rolling direction for the solver.
            S_vector forward = holder->GetMatrix()(2);
            forward.Normalize();
            if (w->flags & WHEEL_ON_GROUND) {
                if (w->ground && (w->ground->flags & COLLISION_FLAG_WATER))
                    m_wheelOnWater = 1;
                const float perSecond = 1.0f / timeDelta;
                w->unknown0A0 = (XYZ(w->unknown050) - XYZ(w->unknown060)) * perSecond;
                w->unknown0B0 = y.Cross(forward).Cross(w->unknown080);
            } else {
                const S_vector arm = XYZ(w->worldPos) - XYZ(m_unknown1B0);
                w->unknown0A0 = XYZ(m_velocity) - m_unknown210.Cross(arm);
                w->unknown0B0 = forward;
            }
            if (w->bend > 0.0f) {
                // Knocked out of true (inferred): turned about the car's y.
                S_quat turn;
                turn.Make(y, std::cos(w->unknown1F0) * w->bend);
                w->unknown0B0 *= turn.RotationMatrix();
            }
            NormalizeBy(w->unknown0B0, SquareYZX(w->unknown0B0));
            if (DotZYX(w->unknown0A0, w->unknown0B0) >= 0.0f)
                w->flags |= WHEEL_ROLLING_FORWARD;
            else
                w->flags &= ~WHEEL_ROLLING_FORWARD;
            if (w->flags & WHEEL_ON_GROUND)
                heading += w->unknown0B0;
        }
        w->flags &= ~WHEEL_ON_REST_PLANE;
    }
    if (pushes != 0) {
        const float share = 1.0f / static_cast<float>(pushes);
        SetXYZ(m_velocity, pushVelocity * share + XYZ(m_velocity));
        m_unknown210 = pushTurn * share + m_unknown210;
    }
    *headingSum = {heading.x, heading.y, heading.z, 0.0f};
}

void C_Vehicle::ImpactDamage(float impulse)                                             // 0x00500eb0
{
    if (m_unknown620 < impulse && !(m_unknown58C & 0x4)) {
        m_unknown610 = m_unknown610 - ((impulse - m_unknown618 * 0.01f * impulse) / m_unknown61C) * m_unknown614;
        if (m_unknown610 < 0.0f)
            m_unknown610 = 0.0f;
        m_unknown58C |= 0x4;
        m_unknown60C = (1.0f - m_unknown608) * (m_unknown610 / m_unknown614) + m_unknown608;
    }
    if (m_unknown610 <= 0.0f)
        m_unknown22A0 = 1;                              // the engine is dead
    if (m_unknown64C < impulse && !(m_unknown58C & 0x8)) {
        m_unknown63C = m_unknown63C - ((impulse - m_unknown644 * 0.01f * impulse) / m_unknown648) * m_unknown640;
        if (m_unknown63C < 0.0f)
            m_unknown63C = 0.0f;
        m_unknown58C |= 0x8;
    }
}
