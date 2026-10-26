// C_Vehicle: the physics solver (original 0x0050de60 Solve, with its inlines 0x004ccc60 (the cross
// product) and 0x004ccca0 (a vector copy)). The wheel contacts it updates for the next step
// (0x00512390) are in game/vehicle_wheels.cpp, the body's collision (0x00514d70) in
// game/vehicle_collide.cpp.
#include "game/vehicle.h"

#include <cmath>
#include <vector>

#include "game/pending.h"                               // g_tyreTypes
#include "ls3d/i3d.h"

namespace {

constexpr float HALF_PI = 1.5707964f;                   // 0x00810180
constexpr float ONE_DEGREE = 0.017453279f;              // 0x00810948 (a little under pi / 180)

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

void SetXYZ(S_vector4 &v, const S_vector &xyz)
{
    v.x = xyz.x;
    v.y = xyz.y;
    v.z = xyz.z;
}

// Dot products and squares in the orders the original sums them.
float DotZYX(const S_vector &a, const S_vector &b) { return (a.z * b.z + a.y * b.y) + a.x * b.x; }
float DotYZX(const S_vector &a, const S_vector &b) { return (a.y * b.y + a.z * b.z) + a.x * b.x; }
float SquareZYX(const S_vector &v) { return (v.z * v.z + v.y * v.y) + v.x * v.x; }
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

// `m` without its translation (the solver turns directions with such copies).
S_matrix Rotation(const S_matrix &m)
{
    S_matrix r = m;
    r.m[3][0] = r.m[3][1] = r.m[3][2] = 0.0f;
    return r;
}

// The quaternion product a b as the solver inlines it.
S_quat Product(const S_quat &a, const S_quat &b)
{
    S_quat q;
    q.s = a.s * b.s - ((a.v.x * b.v.x + a.v.z * b.v.z) + a.v.y * b.v.y);
    q.v = (b.v * a.s + a.v * b.s) + a.v.Cross(b.v);
    return q;
}

// A wheel's tyre values: its tyre type's entry for the ground's surface, or (tyre index 0) its
// own, which mirror an entry.
S_TyreSurface TyreValues(const S_VehicleWheel &w)
{
    if (w.tyreIndex != 0) {
        // Fix: a wheel without a tyre type, a table or an entry for the surface takes its own
        // values (the original reads outside the tables).
        if (w.tyreIndex > 0 && w.tyreIndex < static_cast<int>(g_tyreTypes.size())) {
            const std::vector<S_TyreSurface> *surfaces = g_tyreTypes[w.tyreIndex].surfaces;
            if (surfaces && w.surfaceEntry >= 0 && w.surfaceEntry < static_cast<int>(surfaces->size()))
                return (*surfaces)[w.surfaceEntry];
        }
    }
    S_TyreSurface own;
    own.surface = w.surface;
    own.unknown04 = w.unknown18C;
    own.unknown08 = w.unknown194;
    own.unknown0C = w.unknown198;
    own.unknown10 = w.unknown19C;
    own.unknown14 = w.unknown190;
    own.unknown18 = w.unknown1B8;
    own.unknown1C = w.unknown1BC;
    own.unknown20 = w.unknown1C0;
    own.unknown24 = w.unknown1C4;
    own.unknown28 = w.unknown1C8;
    own.unknown2C = w.unknown1CC;
    own.unknown30 = w.unknown1D0;
    return own;
}

// The grip left along the wheel beside `sideways` across it (the friction ellipse).
float GripLeftAlong(const S_VehicleWheel &w, float sideways)
{
    const float across2 = w.unknown1B4 * w.unknown1B4;
    const float along2 = w.unknown1B0 * w.unknown1B0;
    return std::sqrt((along2 * across2 - along2 * sideways * sideways) / across2);
}

// The larger grip (the tyre holding against the contact's motion).
float LargerGrip(const S_VehicleWheel &w)
{
    return w.unknown1B0 > w.unknown1B4 ? w.unknown1B0 : w.unknown1B4;
}

// Turns the pose by `angles` (the body's turn over the step, in its own space) about the
// model's x, y and z axes in turn, round the world point `centre`: the y and z axes turned, x
// and y rebuilt square to z, all three normalized, the position carried round.
void TurnPose(S_vector4 &pos, S_vector4 &axisX, S_vector4 &axisY, S_vector4 &axisZ, const S_vector &angles,
              const S_vector4 &centre)
{
    S_quat q;
    q.Make(XYZ(axisX), angles.x);
    S_quat turn;
    turn.Make(XYZ(axisY), angles.y);
    q = Product(q, turn);
    turn.Make(XYZ(axisZ), angles.z);
    q = Product(q, turn);
    const S_matrix rotation = q.RotationMatrix();
    S_vector z = XYZ(axisZ);
    z *= rotation;
    S_vector y = XYZ(axisY);
    y *= rotation;
    // Round the centre. (The original leaves the first matrix's m[3][3] uninitialised; LS3D's
    // product is affine and ignores it.)
    S_matrix toCentre;
    toCentre.Identity();
    toCentre.m[3][0] = -centre.x;
    toCentre.m[3][1] = -centre.y;
    toCentre.m[3][2] = -centre.z;
    S_matrix back;
    back.Identity();
    back.m[3][0] = centre.x;
    back.m[3][1] = centre.y;
    back.m[3][2] = centre.z;
    const S_matrix about = (toCentre * rotation) * back;
    S_vector x = y.Cross(z);
    y = z.Cross(x);
    x.Normalize();
    y.Normalize();
    z.Normalize();
    SetXYZ(axisX, x);
    SetXYZ(axisY, y);
    SetXYZ(axisZ, z);
    S_vector p = XYZ(pos);
    p *= about;
    SetXYZ(pos, p);
}

// Puts the model frame at the pose (the position unless locked; the rotation from the axes).
void PlaceModel(I3D_frame *model, const S_vector4 &pos, const S_vector4 &axisZ, const S_vector4 &axisY)
{
    model->SetPosInline(XYZ(pos));
    model->SetLocalDir3(XYZ(axisZ), XYZ(axisY));
}

}  // namespace

bool C_Vehicle::Solve(float timeDelta, float drag, float frontDownforce, float rearDownforce, float torqueLeft,
                      float torqueRight)                                                // 0x0050de60
{
    S_vector4 down = {-m_modelAxisY.x, -m_modelAxisY.y, -m_modelAxisY.z, 0.0f};
    ++m_unknown2282;
    m_unknown020 = S_vector(0.0f, 0.0f, 0.0f);
    m_unknown010 = S_vector(0.0f, 0.0f, 0.0f);
    // The car's speed at the engine's revs in the gear.
    const float engineSpeed = m_engineRevs / m_gearRatios[m_gear + 1] * m_maxWheelCircumference;
    S_vector force(0.0f, 0.0f, 0.0f);
    S_vector torque(0.0f, 0.0f, 0.0f);

    // --- the grip -----------------------------------------------------------------------------
    // Per wheel on the ground the grip along and across it (N): the load, how upright it stands
    // to the ground, the tyre on the ground's surface (the grip falling off past the peak slip),
    // capped. The driven wheels' grip per side (`left` for an even wheel count: the original
    // alternates from the last wheel).
    float gripLeft = 0.0f;
    float gripRight = 0.0f;
    bool left = false;
    for (int i = m_wheelCount; i-- > 0; left = !left) {
        S_VehicleWheel *w = m_wheels[i];
        if (!w)                                         // Fix: the original crashes on a missing wheel
            continue;
        if (!(w->flags & WHEEL_ON_GROUND) || (w->flags & (WHEEL_DETACHED | WHEEL_DUMMY))) {
            w->unknown1B0 = 0.0f;
            w->unknown1B4 = 0.0f;
            w->unknown070 = S_vector(0.0f, 0.0f, 0.0f);
            w->unknown1D4 = 0.0f;
            continue;
        }
        // The force across: against the contact's sideways motion.
        const float along = DotZYX(w->unknown0B0, w->unknown0A0);
        w->unknown0E0 = S_vector(along * w->unknown0B0.x - w->unknown0A0.x, along * w->unknown0B0.y - w->unknown0A0.y,
                                 along * w->unknown0B0.z - w->unknown0A0.z);
        w->unknown1AC = 1.0f;
        const float tilt = std::fabs((w->frame->GetMatrix()(0).AngleTo(w->unknown080) - HALF_PI) * 5.7295775f);
        w->unknown1A8 = 1.0f - (tilt > 1.0f ? 1.0f : tilt);

        const S_TyreSurface tyre = TyreValues(*w);
        const bool rim = (w->flags & WHEEL_ON_RIM) != 0;
        const float upright = (1.0f - tyre.unknown14) * w->unknown1A8 + tyre.unknown14;
        w->unknown1B0 = upright * (rim ? tyre.unknown28 : tyre.unknown18);
        w->unknown1B4 = upright * (rim ? tyre.unknown2C : tyre.unknown1C);
        if ((w->flags & WHEEL_PAST_PEAK) && w->unknown0E0.Square() > 1.0f) {
            // Sliding past the peak slip the step before: down to the share +0x10 at +0x0C.
            float fall = (w->unknown1D4 - tyre.unknown08) / (tyre.unknown0C - tyre.unknown08);
            if (fall > 1.0f)
                fall = 1.0f;
            w->unknown1AC = (1.0f - (1.0f - tyre.unknown10) * fall) * w->unknown1AC;
        }
        const float load = w->unknown168;
        w->unknown1B0 = load * w->unknown1B0 * w->unknown1AC + m_unknown52C;
        w->unknown1B4 = load * w->unknown1AC * w->unknown1B4 + m_unknown52C;

        // The load along the ground's normal, larger as the normal leans from the car's y axis
        // (over its cosine; doubled past 60°).
        w->unknown070 = w->unknown080 * load;
        const float lean = DotZYX(w->unknown080, XYZ(m_modelAxisY));
        if (lean >= 0.5f)
            w->unknown070 *= 1.0f / lean;
        else
            w->unknown070 = w->unknown070 + w->unknown070;
        m_unknown020 += w->unknown070;

        const float cap = rim ? tyre.unknown24 : tyre.unknown04;
        if (w->unknown1B0 >= cap)
            w->unknown1B0 = cap;
        if (w->unknown1B4 >= cap)
            w->unknown1B4 = cap;
        if ((w->flags & (WHEEL_DRIVEN | WHEEL_TRACKED)) && !(w->flags & WHEEL_DUMMY)) {
            if (left)
                gripLeft += w->unknown1B0;
            else
                gripRight += w->unknown1B0;
        }
    }

    // --- the body's own forces --------------------------------------------------------------------
    if (m_unknownF6C & 0x8) {
        // Downforce at the box's front and back ends.
        const S_vector front = XYZ(m_unknown1C0) * m_modelMatrix;
        const S_vector frontForce = XYZ(down) * frontDownforce;
        force += frontForce;
        m_unknown020 += frontForce;
        torque += frontForce.Cross(front - XYZ(m_unknown1B0));
        const S_vector rear = XYZ(m_unknown1D0) * m_modelMatrix;
        const S_vector rearForce = XYZ(down) * rearDownforce;
        force += rearForce;
        m_unknown020 += rearForce;
        torque += rearForce.Cross(rear - XYZ(m_unknown1B0));
    }
    const S_vector dragForce = XYZ(m_velocityDir) * -drag;
    force += dragForce;
    m_unknown020 += dragForce;

    // +0xF7C picks how the drive is split between the driven wheels: the solver sets it to the grip
    // share (1) here. (The original's other modes, now unreachable: 0 the whole drive per wheel, 2
    // by the other wheel of the pair's grip.)
    m_unknownF7C = 1;
    const float totalGrip = gripRight + gripLeft + 1.0f;
    // The drive's torque, at most what the clutch passes.
    const float driveTorque = torqueLeft + torqueRight;
    const float capacity = m_gearRatios[m_gear + 1] * m_unknown904 * m_unknown8F8 * m_unknown530;
    float drive = driveTorque;
    if (capacity < std::fabs(driveTorque))
        drive = driveTorque < 0.0f ? -capacity : capacity;
    const float brake = m_brake;
    m_unknown86C = 0.0f;
    const uint8_t unknown228F = m_unknown228F;
    // Just past the speed limit (SetUnknown4f2630's, within 2.8 m/s) the brakes go on fully,
    // within the grip.
    if (m_unknown790 < m_speed && m_speed < m_unknown790 + 2.8f) {
        m_unknown228F = 1;
        m_brake = 1.0f;
    }
    const S_matrix inverseRotation = Rotation(m_invModelMatrix);
    const S_vector bodyTurn = m_unknown210 * inverseRotation;

    // --- the tyres' forces -------------------------------------------------------------------------
    // Per wheel on the ground: across against the slip (within the peak slip angle growing with
    // it), the brakes and the drive along, all within the friction ellipse; the rolling resistance.
    float brakeLoad = 0.0f;                             // the driven wheels' brake torque (the engine's load)
    float driveSum = 0.0f;                              // the drive forces' sizes
    S_vector loadTorque(0.0f, 0.0f, 0.0f);
    m_unknown2293 = 0;
    bool anyOnGround = false;
    left = false;
    for (int i = m_wheelCount; i-- > 0; left = !left) {
        S_VehicleWheel *w = m_wheels[i];
        if (!w)                                         // Fix: the original crashes on a missing wheel
            continue;
        const uint32_t flags = w->flags;
        w->flags = flags & ~(WHEEL_SLIDING | WHEEL_LOCKED | WHEEL_SPINNING | WHEEL_PAST_PEAK);
        if (!(flags & WHEEL_ON_GROUND) || (flags & (WHEEL_DETACHED | WHEEL_DUMMY)))
            continue;
        anyOnGround = true;
        const S_TyreSurface tyre = TyreValues(*w);
        S_vector motion = w->unknown0A0;
        motion.Normalize();
        S_vector f = w->unknown0E0;
        const float rolling = DotYZX(motion, w->unknown0B0);   // the slip angle's cosine
        const float slip = std::sin(w->unknown0B0.AngleTo(w->unknown0A0));
        w->unknown1D4 = slip;
        if (SquareZYX(f) > 1.0f) {
            if (slip > w->unknown21C)
                w->flags |= WHEEL_SLIDING;
            NormalizeBy(f, SquareZYX(f));
        }
        if (slip < tyre.unknown08)
            f *= slip / tyre.unknown08;
        else
            w->flags |= WHEEL_PAST_PEAK;
        f *= w->unknown1B4;
        const float sideways = LengthOf(SquareZYX(f));

        float brakeTorque = 0.0f;
        float driveForce = 0.0f;
        if (!(m_unknown600 & VEHICLE_PHYSICS_BRAKE_HELD)) {
            if (m_handbrake > 0.0f && (w->flags & WHEEL_HANDBRAKE))
                brakeTorque = m_unknown228B ? m_unknown708 : m_unknown708 * m_handbrake;
            // The stability brake (+0x2290, over 1 m/s): the first wheel brakes fully, without its
            // grip across, while the last one slides 1° past its peak, turning one way; the second
            // and the second last the other way.
            bool locked = false;
            const float heldBrake = m_brake;
            const int index = bodyTurn.y <= 0.0f ? 0 : 1;
            if (m_unknown2290 && m_speed > 1.0f && i == index) {
                const S_VehicleWheel *last = m_wheels[m_wheelCount - 1 - index];
                // Fix: a missing last wheel does not lock (the original crashes).
                if (last && last->unknown194 + ONE_DEGREE < last->unknown1D4) {
                    f = S_vector(0.0f, 0.0f, 0.0f);
                    m_brake = 1.0f;
                    locked = true;
                }
            }
            if (m_brake > 0.0f) {
                float b = m_brake / w->radius * w->unknown1F4;
                // Fully on (the speed limit, the stability brake): within the grip left along.
                if (m_unknown228F || locked) {
                    const float most = GripLeftAlong(*w, sideways);
                    if (most < b)
                        b = most;
                }
                brakeTorque = b * w->radius + brakeTorque;
            }
            m_brake = heldBrake;

            if ((w->flags & (WHEEL_DRIVEN | WHEEL_TRACKED)) && m_gear != 0) {
                if (((m_unknown600 & VEHICLE_PHYSICS_4) && m_throttle == 0.0f && m_unknown700 == 0.0f) || !m_engineOn) {
                    // Idle in gear at rest, or the engine off: the tyre holds against the contact's
                    // motion.
                    f = -(w->unknown0A0 * LargerGrip(*w));
                    brakeTorque = 0.0f;
                } else {
                    brakeLoad += brakeTorque;
                    // The wheel's share of the drive by its grip, less its brake (what is left
                    // of the brake stays).
                    const float share = drive / totalGrip * w->unknown1B0;
                    float rest = brakeTorque - share;
                    if (!(rest > 0.0f)) {
                        driveForce = (share - brakeTorque) / w->radius;
                        rest = 0.0f;
                    }
                    // From gear +0x674 up within the grip left along.
                    if (m_gear >= m_unknown674) {
                        const float most = GripLeftAlong(*w, sideways);
                        if (driveForce > most)
                            driveForce = most;
                    }
                    // Along the wheel (backwards in reverse, not in mode 1).
                    S_vector push = w->unknown0B0 * driveForce;
                    if ((m_unknown550 == 0 || m_unknown550 == 2) && m_gear < 0)
                        push = w->unknown0B0 * -driveForce;
                    m_unknown010 += push;
                    driveSum = std::fabs(driveForce) + driveSum;
                    f += push;
                    brakeTorque = rest;
                }
            }
            // The brake along the wheel by how far the contact moves along it, within the grip;
            // past it the wheel locks: the tyre holds against the contact's motion.
            const float brakeForce = rolling / w->radius * brakeTorque;
            if (std::fabs(brakeForce) <= w->unknown1B0) {
                f -= w->unknown0B0 * brakeForce;
            } else {
                f = -(w->unknown0A0 * LargerGrip(*w));
                w->flags |= WHEEL_LOCKED;
                if (w->unknown0A0.Square() > 1.0f)
                    w->flags |= WHEEL_SLIDING;
            }
        } else {
            // The brake held: the tyre holds against the contact's motion.
            f = -(w->unknown0A0 * LargerGrip(*w));
        }

        // At most the friction ellipse's radius in the force's direction (unknown274: how much of
        // it the force takes).
        const float t = std::tan(f.AngleTo(w->unknown0B0) - HALF_PI);
        const float d = w->unknown1B4 * (t * t * w->unknown1B4) + w->unknown1B0 * w->unknown1B0;
        const float r = w->unknown1B0 / d * std::sqrt(d) * w->unknown1B4;
        const float most = std::sqrt((r * t) * (r * t) + r * r);
        const float size = LengthOf(SquareZYX(f));
        w->unknown274 = size / most;
        if (size > most) {
            NormalizeBy(f, SquareZYX(f));
            f *= most;
            w->unknown274 = 1.0f;
        }

        if (w->flags & (WHEEL_DRIVEN | WHEEL_TRACKED)) {
            // Wheelspin: the drive asks more than the tyre passes along, the engine's speed over
            // 2 m/s off the car's.
            if (m_gear == 0 || ((m_unknown600 & VEHICLE_PHYSICS_4) && m_throttle == 0.0f && m_unknown700 == 0.0f) ||
                !m_engineOn || std::fabs(engineSpeed - m_speed) <= 2.0f ||
                std::fabs(driveForce) <= std::fabs(DotZYX(f, w->unknown0B0))) {
                m_unknown229F = 0;
            } else {
                if (w->unknown0A0.Square() > 1.0f)
                    w->flags |= WHEEL_SLIDING;
                w->flags |= WHEEL_SPINNING;
                m_unknown229E = 1;
            }
        }
        // The rolling resistance against the motion along the wheel (in full from 1 m/s).
        float roll = ((w->flags & WHEEL_ON_RIM) ? tyre.unknown30 : tyre.unknown20) / w->radius * w->unknown168;
        if (roll > w->unknown1B0)
            roll = w->unknown1B0;
        if (w->flags & WHEEL_ROLLING_FORWARD)
            roll = -roll;
        roll = roll * (m_speed <= 1.0f ? m_speed : 1.0f);
        f += w->unknown0B0 * roll;
        w->unknown0E0 = f;

        if (w->flags & (WHEEL_DRIVEN | WHEEL_TRACKED)) {
            // The driven wheels' grip x radius (UpdateEngineRevs; mode 1 by +0x85C / +0x860 per side).
            m_unknown2293 = 1;
            if (m_unknown550 == 0 || m_unknown550 == 2)
                m_unknown86C = w->unknown1B0 * w->radius + m_unknown86C;
            else if (m_unknown550 == 1)
                m_unknown86C = w->unknown1B0 * w->radius * std::fabs(left ? m_unknown85C : m_unknown860) + m_unknown86C;
        }
        // The load at the contact, the tyre's force at the wheel's centre; their torques about the
        // centre of mass. (The original also sums the contacts' normals, unused.)
        loadTorque += w->unknown070.Cross(XYZ(w->unknown050) - XYZ(m_unknown1B0));
        force = force + w->unknown070 + f;
        torque += f.Cross(XYZ(w->worldPos) - XYZ(m_unknown1B0));
        m_unknown020 += f;
    }
    torque = torque + loadTorque;
    m_unknown020 -= m_unknown010;
    m_unknown228F = unknown228F;
    m_brake = brake;
    force += m_unknown270;
    torque += m_unknown280;
    m_unknown020 += m_unknown270;

    // --- holding still -----------------------------------------------------------------------------
    // Below 3 m/s on the ground the tyres hold the car against creeping: sideways (fading out by
    // 3 m/s, within the grip across), and in gear without throttle below 2 m/s along as well
    // (fading out by 2 m/s, within the grip along the drive leaves; not with a driven wheel
    // sliding); the turn about y damped by the loads'.
    if (m_speed < 3.0f && anyOnGround) {
        S_vector loads(0.0f, 0.0f, 0.0f);
        float gripAlong = 0.0f;
        float gripAcross = 0.0f;
        for (int i = m_wheelCount; i-- > 0;) {
            const S_VehicleWheel *w = m_wheels[i];
            if (w && (w->flags & WHEEL_ON_GROUND) && !(w->flags & (WHEEL_DETACHED | WHEEL_DUMMY))) {
                loads += w->unknown070;
                gripAlong += w->unknown1B0;
                gripAcross += w->unknown1B4;
            }
        }
        gripAlong -= driveSum;
        loads = loads * m_unknown350;
        S_vector held = XYZ(m_weight) * m_unknown350;
        held.x += loads.x;
        held.z += loads.z;
        if (gripAcross < std::fabs(held.x))
            held.x = held.x > 0.0f ? gripAcross : -gripAcross;
        S_vector hold(-((1.0f - m_speed * 0.33333334f) * held.x), 0.0f, 0.0f);
        if (m_gear != 0 && m_speed < 2.0f && m_throttle < 0.3f && m_unknown700 < 0.3f) {
            bool sliding = false;
            for (int i = m_wheelCount; i-- > 0 && !sliding;) {
                const S_VehicleWheel *w = m_wheels[i];
                sliding = w && (w->flags & (WHEEL_DRIVEN | WHEEL_TRACKED)) && !(w->flags & WHEEL_DUMMY) &&
                          (w->flags & WHEEL_ON_GROUND) && (w->flags & WHEEL_SLIDING);
            }
            if (!sliding) {
                if (gripAlong < std::fabs(held.z))
                    held.z = held.z > 0.0f ? gripAlong : -gripAlong;
                hold.z = -((1.0f - m_speed * 0.5f) * held.z);
            }
        }
        hold *= Rotation(m_modelMatrix);
        force += hold;
        torque.y = torque.y - (1.0f - m_speed * 0.33333334f) * loadTorque.y;
    }

    // --- the motion -----------------------------------------------------------------------------------
    const float perMass = timeDelta / m_currentMass;
    m_velocity.x = perMass * (force.x + m_weight.x) + m_velocity.x;
    m_velocity.y = perMass * (force.y + m_weight.y) + m_velocity.y;
    m_velocity.z = perMass * (force.z + m_weight.z) + m_velocity.z;
    m_unknown020 += XYZ(m_weight);
    m_velocityDir = m_velocity;
    S_vector direction = XYZ(m_velocityDir);
    direction.Normalize();
    SetXYZ(m_velocityDir, direction);
    // The turn: the torque into the body's space, by the inverse inertia, back out.
    S_vector spin = torque;
    spin *= m_unknown350;
    spin.x *= m_unknown220.x;
    spin.y *= m_unknown220.y;
    spin.z *= m_unknown220.z;
    spin *= m_unknown310;
    m_unknown210.x = spin.x * timeDelta + m_unknown210.x;
    m_unknown210.y = spin.y * timeDelta + m_unknown210.y;
    m_unknown210.z = spin.z * timeDelta + m_unknown210.z;
    const S_vector move = XYZ(m_velocity) * timeDelta;
    m_unknown8E0 = LengthOf((move.x * move.x + move.z * move.z) + move.y * move.y) + m_unknown8E0;

    // The pose: turned about the centre of mass, then moved.
    S_vector4 savedPos = m_modelPos;
    S_vector4 savedZ = m_modelAxisZ;
    S_vector4 savedY = m_modelAxisY;
    S_vector4 savedX = m_modelAxisX;
    TurnPose(m_modelPos, m_modelAxisX, m_modelAxisY, m_modelAxisZ, (m_unknown210 * timeDelta) * inverseRotation,
             m_unknown1B0);
    SetXYZ(m_modelPos, XYZ(m_modelPos) + move);
    PlaceModel(m_model, m_modelPos, m_modelAxisZ, m_modelAxisY);

    // --- the body against the world (+0xF6C bit 0x10) -------------------------------------------
    // Without bit 0x400 once per step (the last substep) from the pose before the step, with it
    // per substep. On a contact the pose goes back and is turned and moved again by the matrices of
    // that pose; having collided the substep before too, pushed out as well. Nearly at rest after
    // it, the car rests.
    const uint8_t collided = m_unknown2288;
    m_unknown2288 = 0;
    if ((m_unknownF6C & 0x10) && ((m_unknownF6C & 0x400) || m_unknown2282 == m_substeps)) {
        if (!(m_unknownF6C & 0x400)) {
            savedPos = m_previousModelPos;
            savedZ = m_previousAxisZ;
            savedY = m_previousAxisY;
            savedX = m_previousAxisX;
        } else {
            m_previousModelMatrix = m_modelMatrix;
            m_previousInvModelMatrix = m_invModelMatrix;
        }
        S_vector push(0.0f, 0.0f, 0.0f);
        S_vector push2(0.0f, 0.0f, 0.0f);
        if (XYZ(m_velocity).Square() > 0.01f || m_unknown210.Square() > 0.0025f || m_unknown22C0 || collided) {
            float maxImpulse = 0.0f;
            S_vector maxImpulseVector(0.0f, 0.0f, 0.0f);
            m_bodyContacts.clear();
            m_bodyContacts.shrink_to_fit();
            m_unknown2288 = CollideBody(m_bodyCollision, &push, &push2, &maxImpulse, &maxImpulseVector, &savedY,
                                        static_cast<int>(timeDelta * 1000.0f));
        }
        if (m_unknown2288) {
            m_modelPos = savedPos;
            m_modelAxisZ = savedZ;
            m_modelAxisY = savedY;
            m_modelAxisX = savedX;
            PlaceModel(m_model, m_modelPos, m_modelAxisZ, m_modelAxisY);
            const S_vector again = XYZ(m_velocity) * timeDelta;
            TurnPose(m_modelPos, m_modelAxisX, m_modelAxisY, m_modelAxisZ,
                     (m_unknown210 * timeDelta) * Rotation(m_previousInvModelMatrix), m_unknown1B0);
            SetXYZ(m_modelPos, XYZ(m_modelPos) + again);
            if (collided) {
                const float pushed = SquareZYX(push);
                if (pushed > 0.0025f)
                    SetXYZ(m_modelPos, XYZ(m_modelPos) + push);
                if (SquareZYX(push2) > 0.0025f && pushed <= 0.0025f)
                    SetXYZ(m_modelPos, XYZ(m_modelPos) + push2);
            }
            PlaceModel(m_model, m_modelPos, m_modelAxisZ, m_modelAxisY);
            if (XYZ(m_velocity).Square() < 0.04f && m_unknown210.Square() < 0.01f) {
                SetXYZ(m_velocity, S_vector(0.0f, 0.0f, 0.0f));
                m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
                if (!(m_unknown600 & VEHICLE_PHYSICS_KEEP_AWAKE))
                    m_unknown600 |= VEHICLE_PHYSICS_RESTING;
            }
        }
    }

    // --- the matrices -------------------------------------------------------------------------------
    m_modelMatrix = m_model->GetMatrix();
    m_invModelMatrix.Inverse(m_modelMatrix);
    m_unknown310 = Rotation(m_modelMatrix);
    m_unknown350 = Rotation(m_invModelMatrix);
    down = {-m_modelAxisY.x, -m_modelAxisY.y, -m_modelAxisY.z, 0.0f};
    SetXYZ(m_unknown1B0, XYZ(m_unknown1E0) * m_modelMatrix);

    // Steering at the point m_unknown040 (+0x2287; inferred: a target the car is driven to).
    if (m_unknown2287) {
        const S_vector to = m_unknown040 - XYZ(m_modelPos);
        float steering = to.AngleTo(XYZ(m_modelAxisZ)) / m_unknown934;
        if (DotZYX(to, XYZ(m_modelAxisX)) <= 0.0f) {
            steering = -steering;
            if (steering < -1.0f)
                steering = -1.0f;
        } else if (steering > 1.0f) {
            steering = 1.0f;
        }
        SetSteering(steering);
        // Quirk kept: the step in milliseconds (Step passes seconds), the steering eased 1000
        // times as fast.
        UpdateSteering(timeDelta * 1000.0f);
    }

    m_unknown8D0 = 0.0f;
    m_velocityDir = m_velocity;
    direction = XYZ(m_velocityDir);
    direction.Normalize();
    SetXYZ(m_velocityDir, direction);
    m_speed = XYZ(m_velocity).Magnitude();
    // The wheels' contacts for the next step; the speed along their rolling directions (the full
    // speed with barely any wheel on the ground).
    S_vector4 headingSum = {0.0f, 0.0f, 0.0f, 0.0f};
    UpdateWheelContacts(&headingSum, timeDelta, &down, true);
    const float headings = SquareYZX(XYZ(headingSum));
    if (headings <= 0.5f) {
        m_unknown8D0 = m_speed;
    } else {
        S_vector heading = XYZ(headingSum);
        NormalizeBy(heading, headings);
        m_unknown8D0 = std::fabs(DotYZX(heading, XYZ(m_velocityDir))) * m_speed;
    }
    // Barely moving with a driven wheel on the ground: rest (stopped dead without throttle once
    // trying to rest).
    if (XYZ(m_velocity).Square() <= 0.001f && SquareYZX(m_unknown210) <= 0.001f && m_unknown2293 && !m_unknown22C0) {
        if (m_throttle < 0.3f && m_unknown604 > 0) {
            SetXYZ(m_velocity, S_vector(0.0f, 0.0f, 0.0f));
            SetXYZ(m_velocityDir, S_vector(0.0f, 0.0f, 0.0f));
            m_speed = 0.0f;
            m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
        }
        if (!(m_unknown600 & VEHICLE_PHYSICS_KEEP_AWAKE))
            m_unknown600 |= VEHICLE_PHYSICS_RESTING;
    }
    // The engine's revs from the drive and the driven wheels' brakes (mode 1: twice the first
    // channel, turned by +0x85C's sign).
    if (m_unknown550 == 0 || m_unknown550 == 2) {
        UpdateEngineRevs(timeDelta, driveTorque, brakeLoad);
    } else if (m_unknown550 == 1) {
        const float first = m_unknown85C > 0.0f ? torqueLeft : -torqueLeft;
        UpdateEngineRevs(timeDelta, first + first, brakeLoad);
    }
    m_unknownF68 = XYZ(m_modelAxisZ).AngleTo(XYZ(m_velocity));
    return true;
}
