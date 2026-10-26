// C_Vehicle: the physics step's drivetrain (original 0x00509aa0 Step, 0x004f67b0 Settle, 0x00507400
// UpdateSteering, 0x0050d5c0 EngineTorque, 0x004f3840 GetEngineTorque, 0x0050d2c0 DriveTorque,
// 0x0050d760 UpdateEngineRevs, 0x0050db90 Integrate). The solver that Integrate runs is in
// game/vehicle_solve.cpp; the per-step update after it (0x0050b180) is a later part.
#include "game/vehicle.h"

#include <cmath>

#include <windows.h>
#include <mmsystem.h>

#include "ls3d/i3d.h"

namespace {

constexpr float HALF_PI = 1.5707964f;                   // 0x00810180
constexpr uint32_t STEP_MS = 0x53;                      // the car's physics step (83 ms)

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

void SetXYZ(S_vector4 &v, const S_vector &xyz)
{
    v.x = xyz.x;
    v.y = xyz.y;
    v.z = xyz.z;
}

// From `from` to `to` by `t` in either of the original's two forms (the same but for rounding).
float Lerp(float from, float to, float t)
{
    return to < from ? from - (from - to) * t : (to - from) * t + from;
}

}  // namespace

// --- the engine -----------------------------------------------------------------------------------

float C_Vehicle::EngineTorque(float revs, float throttle) const                 // 0x0050d5c0
{
    const float r = revs < m_idleRevs ? m_idleRevs : revs;
    float torque;
    if (r <= m_unknown774)
        torque = Lerp(m_unknown764, m_unknown768, (r - m_idleRevs) / (m_unknown774 - m_idleRevs));
    else if (r <= m_unknown778)
        torque = m_unknown768;
    else if (r <= m_unknown77C)
        torque = Lerp(m_unknown768, m_unknown76C, (r - m_unknown778) / (m_unknown77C - m_unknown778));
    else
        torque = Lerp(m_unknown76C, m_unknown770, (r - m_unknown77C) / (m_revLimit - m_unknown77C));
    // The engine braking grows with the (unclamped) revs.
    const float braking = revs / m_revLimit * m_unknown8E8;
    return (torque + braking) * throttle - braking;
}

bool C_Vehicle::GetEngineTorque(float rpm, float *torque, int gear, bool engineBraking) const   // 0x004f3840
{
    if (gear < -1 || gear > m_gearCount)
        return false;
    // The parameters' rpm range (+0x14 the lowest, +0x10 the highest).
    if (rpm + 0.001f < m_params.unknown004[4] || rpm - 0.001f > m_params.unknown004[3])
        return false;
    const float revs = rpm * 0.016666668f;
    float t = EngineTorque(revs, 1.0f);
    if (engineBraking)
        t = EngineTorque(revs, 0.0f) + t;
    if (gear != 0)
        t *= m_gearRatios[gear + 1];
    *torque = t;
    return true;
}

float C_Vehicle::DriveTorque(float timeDelta, float throttle)                   // 0x0050d2c0
{
    if (throttle < m_unknown524 && m_idleRevs + 2.0f > m_engineRevs)
        throttle = m_unknown524;                        // the idle throttle near idle
    if (m_engineRevs > m_revLimit)
        throttle = 0.0f;
    if (m_unknown790 < m_speed)
        throttle = 0.0f;                                // past the speed limit
    m_unknown600 &= ~VEHICLE_PHYSICS_4;
    if (!m_engineOn) {
        if (XYZ(m_velocity).Magnitude() < 0.5f && m_gear != 0)
            m_unknown600 |= VEHICLE_PHYSICS_4;
        return 0.0f;
    }

    const float torque = EngineTorque(m_engineRevs, throttle * m_unknown60C);
    // The engine load: where the torque lies between +0x96C and +0x970, followed at +0x974 /
    // +0x978 per second.
    float load = 0.0f;
    if (torque > m_unknown96C)
        load = torque > m_unknown970 ? 1.0f : (torque - m_unknown96C) / ((m_unknown970 - m_unknown96C) + 0.01f);
    const float up = timeDelta * m_unknown974;
    if (load > up + m_unknown78C) {
        m_unknown78C = up + m_unknown78C;
    } else {
        const float down = timeDelta * m_unknown978;
        m_unknown78C = load < m_unknown78C - down ? m_unknown78C - down : load;
    }
    if (m_unknown78C > 1.0f)
        m_unknown78C = 1.0f;
    else if (m_unknown78C < 0.0f)
        m_unknown78C = 0.0f;

    float wheelTorque = torque * m_gearRatios[m_gear + 1];
    bool dragging = wheelTorque <= 0.0f;
    if (m_unknown790 - 1.0f < m_speed && wheelTorque >= 0.0f) {
        wheelTorque = -1.0f;                            // near the speed limit
        dragging = true;
    }
    if (dragging && m_unknown904 > 0.9f && m_gear != 0 && m_speed < 0.5f)
        m_unknown600 |= VEHICLE_PHYSICS_4;
    if (m_topSpeed + 2.0f < m_speed)
        m_brake = 0.5f;
    return wheelTorque;
}

void C_Vehicle::UpdateEngineRevs(float timeDelta, float torque, float brakeTorque)  // 0x0050d760
{
    const int gear = m_gear;
    const float capacity = m_unknown8F8 * m_unknown530 * m_unknown904;   // what the clutch passes
    const float inverseRatio = 1.0f / m_gearRatios[gear + 1];
    // What the clutch passes to the wheels: at most what their grip holds (the solver's +0x86C, the
    // driven wheels' grip x radius, through the gear).
    float grip = inverseRatio * m_unknown86C;
    // The revs the wheels turn the engine at.
    const float wheelRevs = std::fabs(((m_modelAxisZ.z * m_velocity.z + m_modelAxisZ.y * m_velocity.y) +
                                       m_modelAxisZ.x * m_velocity.x) *
                                      m_gearRatios[gear + 1] / m_maxWheelCircumference);
    float load = brakeTorque * inverseRatio;
    if (load > capacity)
        load = capacity;
    float net = inverseRatio * torque - load;
    if (grip > capacity)
        grip = capacity;
    bool withinClutch = std::fabs(net) < grip;

    // The clutch pulls the revs towards the wheels'.
    if (m_unknown550 == 1) {
        if (m_engineRevs > wheelRevs)
            net -= grip;
        if (m_engineRevs < wheelRevs)
            net += grip;
    } else if (m_unknown550 == 0 || m_unknown550 == 2) {
        if ((m_unknownF68 > HALF_PI && gear > 0) || (!(m_unknownF68 >= HALF_PI) && gear < 0 && m_speed > 0.0001f)) {
            net -= grip;                                // driving against the motion
        } else if (gear != 0) {
            if (m_engineRevs > wheelRevs)
                net -= grip;
            if (m_engineRevs < wheelRevs) {
                withinClutch = true;
                net += grip;
            }
        }
    }
    m_engineRevs = timeDelta * net / (m_unknown738 * 6.283185f) + m_engineRevs;

    const uint32_t clutch = m_unknown910;
    if (!(clutch & VEHICLE_CLUTCH_4) && !(clutch & (VEHICLE_CLUTCH_RELEASING | VEHICLE_CLUTCH_10)) && withinClutch &&
        gear != 0) {
        // Reaching the wheels' revs locks them to those.
        if ((m_engineRevs < wheelRevs && net < 0.0f) || (m_engineRevs > wheelRevs && net > 0.0f))
            m_engineRevs = wheelRevs;
    }
    if (m_engineRevs > m_revLimit)
        m_engineRevs = m_revLimit + 0.001f;
    if (m_engineRevs < 0.0f)
        m_engineRevs = 0.0f;

    if (clutch & VEHICLE_CLUTCH_RELEASING) {
        m_unknown904 = m_unknown904 - timeDelta * m_unknown900;
        if (m_unknown904 <= 0.0f) {
            // Out: the requested gear goes in.
            m_unknown910 = (clutch & ~VEHICLE_CLUTCH_RELEASING) | VEHICLE_CLUTCH_ENGAGING;
            m_unknown904 = 0.0f;
            if (m_soundsOn)
                PlayGearSound();
            m_gear = m_requestedGear;
        }
        return;
    }
    if (clutch & VEHICLE_CLUTCH_ENGAGING) {
        m_gear = m_requestedGear;
        m_unknown904 = timeDelta * m_unknown8FC + m_unknown904;
        bool engaged = false;
        if (m_unknown904 > m_unknown90C) {
            m_unknown904 = m_unknown90C;
            engaged = true;
        } else if (m_unknown904 > 1.0f) {
            m_unknown904 = 1.0f;
            engaged = true;
        }
        if (engaged) {
            m_unknown910 = clutch & ~(VEHICLE_CLUTCH_ENGAGING | VEHICLE_CLUTCH_RELEASING);
            if (m_unknown22C6) {
                m_unknown22C6 = 0;
                m_unknown508 = m_unknown50C = 0.8f;
            }
            return;
        }
        // With +0x22C6: in at once while the wheels are below 15 % of the gear's shift band.
        if (!m_unknown22C6)
            return;
        const int g = m_requestedGear;
        if ((m_shiftUpRevs[g] - m_shiftDownRevs[g]) * 0.15f + m_shiftDownRevs[g] > wheelRevs) {
            m_unknown910 &= ~(VEHICLE_CLUTCH_ENGAGING | VEHICLE_CLUTCH_RELEASING);
            m_unknown508 = m_unknown50C = 1.0f;
            m_unknown904 = m_unknown90C;
            m_unknown22C6 = 0;
        }
        return;
    }
    if (!(clutch & VEHICLE_CLUTCH_4) && !(clutch & VEHICLE_CLUTCH_AUTOMATIC)) {
        m_unknown904 = m_unknown90C;
        return;
    }
    if (m_unknown90C < m_unknown904)
        m_unknown904 = m_unknown90C;
}

// --- the steering (Update: mode 0; Step: mode 2) --------------------------------------------------

void C_Vehicle::UpdateSteering(float timeDelta)                                 // 0x00507400
{
    // The speed share: 1 at rest, 0 at the top speed (1 while sliding sideways into the steering).
    float share = 1.0f - m_speed / m_topSpeed;
    if (share < 0.0f)
        share = 0.0f;
    if (m_speed > 1.0f) {
        const float inverse = 1.0f / m_speed;
        const float side = (inverse * m_velocity.z * m_modelAxisX.z + inverse * m_velocity.y * m_modelAxisX.y) +
                           inverse * m_velocity.x * m_modelAxisX.x;
        if ((side > 0.34 && m_steering > 0.0f) || (side < -0.34 && m_steering < 0.0f))
            share = 1.0f;
    }
    m_unknown934 = (m_unknown92C - m_unknown930) * share + m_unknown930;

    // The steering eases towards SetSteering's (back to the centre at +0x93C / +0x944, out at
    // +0x938 / +0x940 per second, by the speed share).
    if (m_unknown2299) {
        m_unknown94C = m_steering;
    } else {
        const float rate = m_steering * m_unknown94C < 0.0f || std::fabs(m_unknown94C) > std::fabs(m_steering)
                               ? (m_unknown93C - m_unknown944) * share + m_unknown944
                               : (m_unknown938 - m_unknown940) * share + m_unknown940;
        const float step = rate * timeDelta;
        if (step >= std::fabs(m_steering - m_unknown94C))
            m_unknown94C = m_steering;
        else if (m_steering > m_unknown94C)
            m_unknown94C = step + m_unknown94C;
        else
            m_unknown94C = m_unknown94C - step;
    }
    m_unknown958 = std::fabs(m_unknown94C) / m_unknown92C * m_unknown954;

    // Each holder on the model turns about its local y axis: a steered wheel by the steering
    // (the front ones the other way; +0x958 apart on the two sides), every wheel by its toe
    // +0x178 (negated on the right), after its pose from Start.
    for (int i = m_wheelCount; i-- > 0;) {
        S_VehicleWheel *wheel = m_wheels[i];
        if (!wheel)                                     // Fix: the original crashes on a missing wheel
            continue;
        if (wheel->flags & WHEEL_DUMMY)
            continue;
        I3D_frame *holder = wheel->holder;
        if (holder->GetParent() != m_model)
            continue;
        const S_vector axis = holder->GetLocalMatrix()(1);
        const bool right = i % 2 != 0;
        float angle;
        if (!(wheel->flags & WHEEL_STEERED)) {
            angle = right ? -wheel->unknown178 : wheel->unknown178;
        } else if (wheel->modelPos.z >= m_centreOfMass.z) {
            angle = right ? -((m_unknown94C + m_unknown958) * wheel->unknown1F8) - wheel->unknown178
                          : -((m_unknown94C - m_unknown958) * wheel->unknown1F8) + wheel->unknown178;
        } else {
            angle = right ? (m_unknown94C + m_unknown958) * wheel->unknown1F8 - wheel->unknown178
                          : (m_unknown94C - m_unknown958) * wheel->unknown1F8 + wheel->unknown178;
        }
        S_quat q;
        q.Make(axis, angle);
        const S_quat &w = wheel->unknown000;
        S_quat rot;
        rot.v = S_vector((q.v.x * w.s + q.s * w.v.x) + (q.v.z * w.v.y - q.v.y * w.v.z),
                         (q.v.y * w.s + q.s * w.v.y) + (q.v.x * w.v.z - q.v.z * w.v.x),
                         (q.v.z * w.s + q.s * w.v.z) + (q.v.y * w.v.x - q.v.x * w.v.y));
        rot.s = q.s * w.s - ((q.v.x * w.v.x + q.v.z * w.v.z) + q.v.y * w.v.y);
        holder->SetRot(rot);
    }
}

// --- the step -------------------------------------------------------------------------------------

void C_Vehicle::Integrate(float timeDelta, float torqueLeft, float torqueRight)  // 0x0050db90
{
    // The air drag terms (+0x854 x +0x858, +0x728 x +0x72C, +0x730 x +0x734) over 1 m/s.
    float drag1 = 0.0f, drag2 = 0.0f, drag3 = 0.0f;
    if (m_speed > 1.0f) {
        const float speed2 = m_speed * m_speed;
        drag1 = m_unknown858 * m_unknown854 * speed2 * 0.65f;
        drag2 = m_unknown72C * m_unknown728 * speed2 * 0.65f;
        drag3 = m_unknown734 * m_unknown730 * speed2 * 0.65f;
    }
    if (m_brake == 0.0f)
        m_unknown724 = 0.0f;
    m_unknown8E0 = 0.0f;
    if (m_unknownF6C & 0x10) {
        if (!(m_unknownF6C & 0x400)) {
            m_previousModelMatrix = m_modelMatrix;
            m_previousInvModelMatrix = m_invModelMatrix;
        }
        m_previousModelPos = m_modelPos;
        m_previousAxisZ = m_modelAxisZ;
        m_previousAxisY = m_modelAxisY;
        m_previousAxisX = m_modelAxisX;
    }
    if (timeDelta > m_unknownF54) {
        const uint16_t steps = static_cast<uint16_t>(static_cast<int>(timeDelta / m_unknownF54) + 1);
        m_unknown2282 = 0;
        m_substeps = steps;
        const float step = timeDelta / static_cast<float>(steps);
        for (int i = 0; i < m_substeps; ++i)
            Solve(step, drag1, drag2, drag3, torqueLeft, torqueRight);
    } else {
        m_substeps = 1;
        m_unknown2282 = 0;
        Solve(timeDelta, drag1, drag2, drag3, torqueLeft, torqueRight);
    }
    for (int i = m_wheelCount; i-- > 0;) {
        if (m_wheels[i])                                // Fix: the original crashes on a missing wheel
            m_wheels[i]->unknown26C = m_unknown8E0 + m_wheels[i]->unknown26C;
    }
    m_unknown7C4 = timeDelta;
    if (timeDelta < 0.0001f)
        m_unknown7C4 = 0.0001f;
}

void C_Vehicle::Step(uint32_t ms)                                               // 0x00509aa0
{
    if (ms == 0)
        ms = 1;
    // The revs the wheels turn the engine at in the current gear.
    const float wheelRevs = std::fabs(((m_modelAxisZ.z * m_velocity.z + m_modelAxisZ.y * m_velocity.y) +
                                       m_modelAxisZ.x * m_velocity.x) *
                                      (m_gearRatios[m_gear + 1] / m_maxWheelCircumference));
    if (m_unknown22C7) {
        m_throttle = 0.0f;
        m_unknown700 = 0.0f;
    }
    if (m_unknown22C1) {
        m_unknown22C2 = m_engineOn;
        m_engineOn = false;
        m_throttle = 0.0f;
        m_unknown700 = 0.0f;
    }
    uint32_t state = m_unknown600;
    if (m_unknown554 > 0)
        state |= VEHICLE_PHYSICS_FROZEN;
    else
        state &= ~VEHICLE_PHYSICS_FROZEN;
    m_unknown600 = state;
    if (m_unknownF68 > HALF_PI && m_gear > 0 && m_throttle > 0.3f && (m_unknown910 & VEHICLE_CLUTCH_4) &&
        (m_unknown910 & VEHICLE_CLUTCH_AUTOMATIC) && m_speed > 1.0f) {
        m_brake = 1.0f;                                 // moving backwards in a forward gear
        m_unknown600 = state | VEHICLE_PHYSICS_BRAKE_HELD;
    }
    m_unknown600 &= ~VEHICLE_PHYSICS_RESTING;
    if (m_unknown600 & VEHICLE_PHYSICS_KEEP_AWAKE)
        m_unknown600 |= VEHICLE_PHYSICS_AWAKE;
    if (m_unknown22C0) {
        m_unknown500 = 0;
        m_unknown600 |= VEHICLE_PHYSICS_AWAKE;
        if (m_unknown604 > 0)
            m_unknown604 = 0;
    }
    const float dt = static_cast<float>(ms) * 0.001f;
    if (m_unknown550 == 2)
        UpdateSteering(dt);

    // Fuel: by the throttle (+0x85C, both channels in mode 1) and the revs.
    if (m_engineOn) {
        bool empty = !(m_fuel > 0.0001f);
        if (!empty) {
            const float throttleUse = m_unknown550 == 1 ? (std::fabs(m_unknown860) + std::fabs(m_unknown85C)) * 0.5f
                                                        : m_unknown85C;
            m_fuel = m_fuel - (m_engineRevs / m_revLimit * 0.2f + throttleUse * 0.8f) * m_unknown784 * dt;
            empty = m_fuel < 0.0001f;
        }
        if (empty) {
            m_fuel = 0.0f;
            m_unknown22A0 = 1;
            m_throttle = 0.0f;
            m_unknown700 = 0.0f;
        }
    }
    float throttle = m_throttle;
    m_unknown860 = m_unknown700;
    m_unknown85C = throttle;
    // +0x90C follows SetUnknown4f23b0's value at +0x900 per second (at once with +0x229C).
    if (m_unknown229C) {
        m_unknown90C = m_unknown908;
    } else if (m_unknown90C > m_unknown908) {
        m_unknown90C = m_unknown90C - dt * m_unknown900;
        if (m_unknown90C < m_unknown908)
            m_unknown90C = m_unknown908;
    } else if (m_unknown90C < m_unknown908) {
        m_unknown90C = dt * m_unknown900 + m_unknown90C;
        if (m_unknown90C > m_unknown908)
            m_unknown90C = m_unknown908;
    }

    // The gear may change unless the engine races more than 3 rev/s ahead of the wheels, or a
    // driven wheel has WHEEL_SPINNING below the rev limit.
    bool gripping = true;
    if (m_engineRevs > m_unknown774) {
        const float ahead = m_engineRevs - m_speed / m_maxWheelCircumference * m_gearRatios[m_gear + 1];
        if (std::fabs(ahead) > 3.0 && ahead > 0.0f)
            gripping = false;
    }
    if (m_gear != 0 && gripping) {
        for (int i = 0; i < m_wheelCount; ++i) {
            const S_VehicleWheel *wheel = m_wheels[i];
            if (!wheel)                                 // Fix (in every wheel loop here): a missing wheel
                continue;
            const uint32_t f = wheel->flags;
            if ((f & (WHEEL_DRIVEN | WHEEL_TRACKED)) && !(f & WHEEL_DUMMY) && (f & WHEEL_SPINNING) &&
                m_revLimit - 2.0f > m_engineRevs) {
                gripping = false;
                break;
            }
        }
    }

    // The automatic gearbox.
    int next = m_gear + 1;
    const bool featureReady = !(m_unknownF6C & 0x4000) || m_unknown63C > 0.0f;
    if (featureReady && m_engineOn && m_automatic && m_driven && !(m_unknown600 & VEHICLE_PHYSICS_BRAKE_HELD) &&
        !m_unknown22C6 && m_gear != 0 && (m_unknown2293 || m_tracked)) {
        const uint32_t now = m_unknown600;
        const int gear = m_gear;
        if (gear > 0) {
            if (m_unknownF68 > HALF_PI && throttle < 0.3f && !(now & VEHICLE_PHYSICS_4) &&
                (m_unknown550 == 0 || m_unknown550 == 2) && m_speed > 1.0f) {
                // Rolling backwards with the throttle up: reverse.
                next = 0;
                m_gear = m_requestedGear = -1;
            } else if (wheelRevs > m_engineRevs && m_brake < 0.001f && m_handbrake < 0.001f) {
                // The wheels drive the engine: up to the first gear whose revs stay under its
                // shift-up point.
                int c = next;
                if (c <= m_gearCount) {
                    const float inverse = 1.0f / m_gearRatios[c];
                    for (; c <= m_gearCount; ++c) {
                        if (!(m_shiftUpRevs[c - 1] < inverse * m_gearRatios[c] * wheelRevs))
                            break;
                    }
                }
                m_requestedGear = m_gear = c - 1;
            } else {
                enum { DONE, KICKDOWN, BRAKE_REVERSE } action = KICKDOWN;
                if (m_engineRevs > m_shiftUpRevs[gear] && gear < m_gearCount && gripping) {
                    // Up: at once nose down, else when the next gear still accelerates.
                    action = DONE;
                    if (!(m_modelAxisZ.y > -0.09f)) {
                        SetGear(next);
                    } else {
                        float lowTorque = 1.0f, idleTorque = 1.0f, highTorque = 1.0f;
                        GetEngineTorque(m_shiftDownRevs[next] * 60.0f, &lowTorque, next, false);
                        GetEngineTorque(m_unknown774 * 60.0f, &idleTorque, next, false);
                        GetEngineTorque(m_shiftUpRevs[gear] * 60.0f, &highTorque, m_gear, false);
                        const float average = (idleTorque + lowTorque) * 0.5f;
                        const S_vector &u = m_unknown010;
                        const S_vector &w = m_unknown020;
                        const float inverse = 1.0f / highTorque;
                        const S_vector p(average * u.x * inverse + w.x, average * u.y * inverse + w.y,
                                         average * u.z * inverse + w.z);
                        if ((p.z * u.z + p.y * u.y) + p.x * u.x > 0.0f || u.Square() < 1.0f) {
                            const float reachable = -EngineTorque(m_engineRevs, 0.0f) * m_maxWheelRadius /
                                                    (m_gearRatios[next] * m_unknown738);
                            if (w.Magnitude() / m_currentMass < reachable)
                                SetGear(m_gear + 1);
                        }
                    }
                } else if (m_engineRevs < m_shiftDownRevs[gear]) {
                    if (gear <= 1) {
                        action = BRAKE_REVERSE;
                    } else if (m_brake < 0.3f && m_handbrake < 0.3f && gripping) {
                        // Down: to the first gear whose revs lie within its shift band.
                        action = DONE;
                        int c = 2;
                        if (next > 2) {
                            const float inverse = 1.0f / m_gearRatios[next];
                            for (; c < next; ++c) {
                                const float revs = inverse * m_gearRatios[c] * m_engineRevs;
                                if (revs < m_shiftDownRevs[c - 1])
                                    continue;
                                if (revs < m_shiftUpRevs[c - 1])
                                    break;
                            }
                        }
                        if (throttle > 0.3f || c == 2) {
                            SetGear(c - 1);
                            if (c == 2) {
                                // Into the first gear: unless the first gear accelerates, at once.
                                float firstTorque = 1.0f, secondTorque = 1.0f;
                                GetEngineTorque(60.0f / m_gearRatios[2] * m_gearRatios[3] * m_engineRevs, &firstTorque,
                                                1, false);
                                GetEngineTorque(m_engineRevs * 60.0f, &secondTorque, 2, false);
                                const S_vector &u = m_unknown010;
                                const S_vector &w = m_unknown020;
                                const float inverse = 1.0f / secondTorque;
                                const S_vector p(firstTorque * u.x * inverse + w.x, firstTorque * u.y * inverse + w.y,
                                                 firstTorque * u.z * inverse + w.z);
                                if ((p.z * u.z + p.y * u.y) + p.x * u.x < 0.0f) {
                                    m_unknown910 = (m_unknown910 & ~(VEHICLE_CLUTCH_ENGAGING | VEHICLE_CLUTCH_RELEASING)) |
                                                   VEHICLE_CLUTCH_4;
                                    m_unknown22C6 = 0;
                                    m_unknown904 = 0.0f;
                                    m_gear = 1;
                                    PlayGearSound();
                                }
                            }
                        }
                    }
                }
                if (action == KICKDOWN) {
                    // Kickdown: the throttle just pressed with the revs low in the band.
                    action = BRAKE_REVERSE;
                    if (gear > 1 &&
                        m_engineRevs < m_shiftUpRevs[gear - 1] - (m_shiftUpRevs[gear - 1] - m_shiftDownRevs[gear - 1]) * 0.25f &&
                        throttle > 0.3f && m_unknown714 < 0.3f && (m_unknown550 == 0 || m_unknown550 == 2) && gripping) {
                        SetGear(gear - 1);
                        action = DONE;
                    }
                }
                if (action == BRAKE_REVERSE && (m_unknown550 == 0 || m_unknown550 == 2) && m_brake > 0.3f &&
                    m_speed < 0.5f) {
                    // Braking at a standstill: reverse.
                    m_unknown780 = dt + m_unknown780;
                    if (m_unknown780 > 0.002f)
                        SetGear(-1);
                }
            }
        } else if (m_unknownF68 < HALF_PI && throttle < 0.3f && !(now & (VEHICLE_PHYSICS_RESTING | VEHICLE_PHYSICS_4)) &&
                   m_speed > 1.0f) {
            // Reverse while moving forwards with the throttle up: the gear the speed fits.
            const float revs = m_speed / m_maxWheelCircumference * m_gearRatios[gear + 1];
            int c = 2;
            for (; c < m_gearCount; ++c) {
                if (revs < m_shiftUpRevs[c - 1])
                    break;
            }
            next = c;
            m_gear = m_requestedGear = c - 1;
        } else if (m_brake > 0.3f && m_speed < 1.0f) {
            m_unknown780 = dt + m_unknown780;
            if (m_unknown780 > 0.002f)
                SetGear(1);
        }
    }

    m_unknown600 &= ~VEHICLE_PHYSICS_BRAKE_HELD;
    m_unknown714 = m_unknown85C;
    m_unknown718 = m_unknown860;
    if (m_automatic && m_unknown22C6 && !(m_unknown910 & VEHICLE_CLUTCH_10)) {
        if (m_unknown910 & VEHICLE_CLUTCH_RELEASING) {
            m_unknown85C = 0.0f;
            m_unknown860 = 0.0f;
        }
        m_unknown85C = m_unknown904 * m_unknown85C;
        m_unknown860 = m_unknown904 * m_unknown860;
    }

    // Traction easing: an acceleration along the car over +0x510 backs the throttles off at
    // +0x514 per second; below it they come back as fast.
    float acceleration = (((m_modelAxisZ.z * m_velocity.z + m_modelAxisZ.y * m_velocity.y) + m_velocity.x * m_modelAxisZ.x) -
                          ((m_modelAxisZ.z * m_previousVelocity.z + m_modelAxisZ.y * m_previousVelocity.y) +
                           m_modelAxisZ.x * m_previousVelocity.x)) /
                         m_unknown7C4;
    if (m_gear < 0)
        acceleration = -acceleration;
    if (acceleration > m_unknown510 && !(m_unknown910 & VEHICLE_CLUTCH_4)) {
        if (m_unknown85C < m_unknown508) {
            m_unknown508 = m_unknown85C;
        } else {
            m_unknown508 = m_unknown508 - dt * m_unknown514;
            if (m_unknown508 < 0.0f)
                m_unknown508 = 0.0f;
        }
        if (m_unknown860 < m_unknown50C) {
            m_unknown50C = m_unknown860;
        } else {
            m_unknown50C = m_unknown50C - dt * m_unknown514;
            if (m_unknown50C < 0.0f)
                m_unknown50C = 0.0f;
        }
    } else {
        if (m_unknown85C > m_unknown508 && !(m_unknown910 & VEHICLE_CLUTCH_4)) {
            m_unknown508 = dt * m_unknown514 + m_unknown508;
            if (m_unknown508 > m_unknown85C)
                m_unknown508 = m_unknown85C;
        } else {
            m_unknown508 = m_unknown85C;
        }
        if (m_unknown860 > m_unknown50C) {
            m_unknown50C = dt * m_unknown514 + m_unknown50C;
            if (m_unknown50C > m_unknown860)
                m_unknown50C = m_unknown860;
        } else {
            m_unknown50C = m_unknown860;
        }
    }
    if (m_unknown910 & VEHICLE_CLUTCH_10) {
        m_unknown508 = m_unknown85C;
        m_unknown50C = m_unknown860;
    }
    const float eased = m_unknown508;
    m_unknown85C = eased;
    m_unknown860 = m_unknown50C;

    // The wheels' torque, half to each side (mode 1: one channel per side), limited by the
    // clutch while it moves.
    float torqueLeft = 0.0f, torqueRight = 0.0f;
    if (m_unknown550 == 0 || m_unknown550 == 2) {
        const float half = DriveTorque(dt, eased) * 0.5f;
        torqueLeft = half * m_unknown530;
        torqueRight = half * m_unknown530;
        const uint32_t clutch = m_unknown910;
        if (m_automatic && (clutch & (VEHICLE_CLUTCH_ENGAGING | VEHICLE_CLUTCH_RELEASING)) &&
            !(clutch & VEHICLE_CLUTCH_4)) {
            float sum = torqueRight + torqueLeft;
            if (std::fabs(sum) < 0.01)
                sum = 0.01f;
            const float capacity = m_gearRatios[m_gear + 1] * m_unknown904 * m_unknown8F8 * m_unknown530;
            if ((sum > capacity && !(clutch & VEHICLE_CLUTCH_ENGAGING)) || sum * 0.8f > capacity) {
                const float scale = capacity / sum;
                torqueLeft = torqueLeft * scale;
                torqueRight = scale * torqueRight;
            }
        }
    } else if (m_unknown550 == 1) {
        float left = DriveTorque(dt, std::fabs(eased)) * 0.5f;
        if (m_unknown85C < 0.0f)
            left = -left;
        float right = DriveTorque(dt, std::fabs(m_unknown860)) * 0.5f;
        if (m_unknown860 < 0.0f)
            right = -right;
        torqueLeft = left * m_unknown530;
        torqueRight = right * m_unknown530;
    }

    // Resting: steering changes wake the car; idle in gear (no throttle) it only turns the engine.
    m_unknown604 += static_cast<int>(ms);
    if (std::fabs(m_unknown94C - m_unknown950) > 0.035) {
        m_unknown950 = m_unknown94C;
        m_unknown500 = 0;
        m_unknown600 = (m_unknown600 & ~VEHICLE_PHYSICS_RESTING) | VEHICLE_PHYSICS_AWAKE;
        m_unknown604 = -500;
    }
    const uint32_t awake = m_unknown600;
    if (!(awake & VEHICLE_PHYSICS_AWAKE) || (awake & VEHICLE_PHYSICS_FROZEN)) {
        if (((m_throttle > m_unknown524 || m_unknown700 > m_unknown524) && m_gear != 0) || m_gear == 0 ||
            m_unknown229E) {
            if (!(awake & VEHICLE_PHYSICS_FROZEN))
                m_unknown600 = (awake & ~VEHICLE_PHYSICS_RESTING) | VEHICLE_PHYSICS_AWAKE;
            m_unknown500 = 0;
            if (m_unknown604 > 0)
                m_unknown604 = 0;
        } else {
            m_previousRevs = m_engineRevs;
            UpdateEngineRevs(dt, torqueRight + torqueLeft, 0.0f);
            if (m_idleRevs + 1.0f < m_engineRevs) {
                m_unknown910 &= ~VEHICLE_CLUTCH_10;
                m_unknown600 |= VEHICLE_PHYSICS_AWAKE;
            } else {
                // Stalling: clutch out, and the brake on at a standstill.
                m_unknown904 = 0.0f;
                m_unknown910 = (m_unknown910 & ~VEHICLE_CLUTCH_ENGAGING) | VEHICLE_CLUTCH_10;
                if (m_engineOn && m_speed < 0.7f) {
                    m_brake = 1.0f;
                    m_unknown600 |= VEHICLE_PHYSICS_BRAKE_HELD;
                }
                m_unknown229F = 0;
                m_unknown22C6 = 0;
            }
            const uint32_t now = m_unknown600;
            if (!(now & VEHICLE_PHYSICS_AWAKE) && (!m_driven || (now & VEHICLE_PHYSICS_BRAKE_HELD)) &&
                m_unknown604 > 1000) {
                m_unknown600 = now | VEHICLE_PHYSICS_RESTING;
                return;                                 // no solver this step
            }
        }
    }

    // The automatic clutch (modes 0 and 2).
    if ((m_unknown550 == 0 || m_unknown550 == 2) && (m_unknown910 & VEHICLE_CLUTCH_AUTOMATIC)) {
        uint32_t clutch = m_unknown910 & ~VEHICLE_CLUTCH_10;
        m_unknown910 = clutch;
        int gear = m_gear;
        enum { EVALUATE, ENGAGE, STORE, WHEELS } then = EVALUATE;
        if (gear == 0) {
            then = ENGAGE;
        } else {
            const float top = m_unknown8EC + 1.0f;
            if (!(top < m_engineRevs)) {
                if (wheelRevs < top) {
                    // Too slow for the gear: clutch out (down a gear if the throttle is down).
                    const bool pressed = m_unknown714 > m_unknown524;
                    clutch &= ~(VEHICLE_CLUTCH_ENGAGING | VEHICLE_CLUTCH_RELEASING);
                    m_unknown22C6 = 0;
                    m_unknown910 = clutch;
                    m_unknown904 = 0.0f;
                    if (pressed) {
                        if (m_automatic && wheelRevs < m_shiftDownRevs[gear] && gear > 1) {
                            int c = gear;
                            if (c > 2) {
                                const float inverse = 1.0f / m_gearRatios[next];
                                for (; c > 2; --c) {
                                    if (inverse * wheelRevs * m_gearRatios[c] < m_shiftDownRevs[c - 1])
                                        continue;
                                    if (inverse * m_engineRevs * m_gearRatios[c] < m_shiftUpRevs[c - 1])
                                        break;
                                }
                            }
                            SetGear(c - 1);
                        }
                        m_unknown910 |= VEHICLE_CLUTCH_4 | VEHICLE_CLUTCH_10;
                    } else {
                        m_unknown910 = clutch & ~VEHICLE_CLUTCH_4;
                        if (m_engineOn) {
                            m_brake = 0.1f;
                            if (m_speed < 0.7f) {
                                m_brake = 1.0f;
                                m_unknown600 |= VEHICLE_PHYSICS_BRAKE_HELD;
                            }
                        }
                        m_unknown910 |= VEHICLE_CLUTCH_10;
                    }
                } else {
                    clutch &= ~VEHICLE_CLUTCH_4;
                    clutch |= VEHICLE_CLUTCH_ENGAGING;
                    m_unknown910 = clutch;
                    if (!m_unknown22C6)
                        m_unknown904 = m_unknown90C;
                }
            } else if (m_unknown8F4 + 8.0f < m_engineRevs && (clutch & VEHICLE_CLUTCH_8)) {
                if (!(clutch & (VEHICLE_CLUTCH_ENGAGING | VEHICLE_CLUTCH_RELEASING)))
                    m_unknown904 = m_unknown90C;
                m_unknown910 = clutch & ~VEHICLE_CLUTCH_4;
            } else if (!(clutch & VEHICLE_CLUTCH_4)) {
                clutch |= VEHICLE_CLUTCH_ENGAGING;
                m_unknown910 = clutch;
                if (!m_unknown22C6)
                    m_unknown904 = m_unknown90C;
            }
            // Bit 4 (low gears, or a manual box): slipping the clutch above +0x8F4.
            clutch = m_unknown910;
            gear = m_gear;
            if (!(clutch & VEHICLE_CLUTCH_4) || (gear >= 2 && m_automatic) || gear == 0) {
                then = ENGAGE;
            } else {
                m_unknown22C6 = 0;
                if (!(m_engineRevs > m_unknown8F4)) {
                    clutch &= ~VEHICLE_CLUTCH_8;
                    m_unknown904 = 0.0f;
                    then = STORE;
                } else if (!(clutch & VEHICLE_CLUTCH_8)) {
                    m_engineRevs = m_unknown8F4 + 1.0f;
                    m_unknown904 = std::fabs(torqueRight + torqueLeft) /
                                   (m_gearRatios[gear + 1] * m_unknown8F8 * m_unknown530);
                    if (m_unknown904 > 1.0f)
                        m_unknown904 = 1.0f;
                    clutch = (clutch & ~(VEHICLE_CLUTCH_ENGAGING | VEHICLE_CLUTCH_RELEASING)) | VEHICLE_CLUTCH_8;
                    then = STORE;
                } else if (wheelRevs > m_unknown8F4) {
                    m_unknown904 = m_unknown90C;
                    then = ENGAGE;
                } else {
                    then = WHEELS;
                }
            }
        }
        if (then == ENGAGE)
            clutch = (clutch & ~(VEHICLE_CLUTCH_4 | VEHICLE_CLUTCH_8)) | VEHICLE_CLUTCH_ENGAGING;
        if (then == ENGAGE || then == STORE)
            m_unknown910 = clutch;
        // A driven wheel with WHEEL_LOCKED and no throttle: clutch out.
        for (int i = m_wheelCount; i-- > 0;) {
            const S_VehicleWheel *wheel = m_wheels[i];
            if (!wheel)
                continue;
            const uint32_t f = wheel->flags;
            if ((f & WHEEL_DRIVEN) && (f & WHEEL_LOCKED) && !(f & WHEEL_DUMMY) && m_unknown85C <= 0.0f) {
                m_unknown904 = 0.0f;
                break;
            }
        }
    }

    m_previousRevs = m_engineRevs;
    m_previousVelocity = m_velocity;
    m_previousSpeed = m_speed;
    m_unknown8D4 = m_unknown8D0;
    if (!m_engineOn)
        m_unknown600 |= VEHICLE_PHYSICS_BRAKE_HELD;
    if (!(m_unknown600 & VEHICLE_PHYSICS_FROZEN))
        Integrate(dt, torqueLeft, torqueRight);
    if (m_unknown22A0)
        SwitchEngine(false);

    // Airborne: no wheel on the ground; down again: all on it and none with WHEEL_SLIDING.
    const uint32_t now = m_unknown600;
    const auto any = [&](auto test) {
        for (int i = m_wheelCount; i-- > 0;) {
            if (m_wheels[i] && test(m_wheels[i]->flags))
                return true;
        }
        return false;
    };
    if (!(now & VEHICLE_PHYSICS_AIRBORNE)) {
        if (!any([](uint32_t f) { return (f & WHEEL_ON_GROUND) && !(f & WHEEL_DUMMY); }))
            m_unknown600 = now | VEHICLE_PHYSICS_AIRBORNE;
    } else if (!any([](uint32_t f) { return !(f & WHEEL_ON_GROUND) && !(f & WHEEL_DUMMY); }) &&
               !any([](uint32_t f) { return (f & WHEEL_SLIDING) != 0; })) {
        m_unknown600 = now & ~VEHICLE_PHYSICS_AIRBORNE;
    }

    // Resting: 4 s within 0.15 m, or 1 s trying to.
    const float dx = m_modelPos.x - m_unknown030.x;
    const float dy = m_modelPos.y - m_unknown030.y;
    const float dz = m_modelPos.z - m_unknown030.z;
    if ((dz * dz + dy * dy) + dx * dx >= 0.0225f || (m_unknown600 & VEHICLE_PHYSICS_KEEP_AWAKE)) {
        m_unknown030 = m_modelPos;
        m_unknown500 = 0;
    } else {
        m_unknown500 += ms;
    }
    if (!(m_unknown600 & VEHICLE_PHYSICS_RESTING) && m_unknown604 > 0)
        m_unknown604 = 0;
    if (m_unknown604 > 1000 || m_unknown500 > 4000)
        m_unknown600 &= ~VEHICLE_PHYSICS_AWAKE;
}

bool C_Vehicle::Settle(uint32_t maxMs, float speed, int minSteps)              // 0x004f67b0
{
    const uint32_t features = m_unknownF6C;
    const uint32_t state = m_unknown600;
    const float maxStep = m_unknownF54;
    const int frozen = m_unknown554;
    const bool sounds = m_soundsOn;
    m_soundsOn = false;
    m_unknownF54 = 0.028f;
    m_unknownF6C = 0x80000801;
    m_unknown22C0 = 1;
    m_unknown600 = VEHICLE_PHYSICS_AWAKE;
    m_unknown7C4 = 0.02f;
    SetXYZ(m_velocity, S_vector(0.0f, 0.0f, 0.0f));
    m_speed = 0.0f;
    m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
    m_unknown554 = 0;
    const DWORD start = timeGetTime();
    while (timeGetTime() - start < maxMs || minSteps > 0) {
        Step(STEP_MS);
        m_unknown600 = VEHICLE_PHYSICS_AWAKE;
        --minSteps;
        m_unknown604 = 0;
        // The vertical speed kept within -0.4 (on the ground) .. 0.1, the turning damped.
        if (m_velocity.y > 0.1f) {
            SetXYZ(m_velocity, S_vector(0.0f, 0.1f, 0.0f));
        } else {
            bool onGround = false;
            for (int i = m_wheelCount; i-- > 0;) {
                const S_VehicleWheel *wheel = m_wheels[i];
                if (wheel && (wheel->flags & WHEEL_ON_GROUND) && !(wheel->flags & WHEEL_DUMMY)) {
                    onGround = true;
                    break;
                }
            }
            if (!onGround || !(m_velocity.y < -0.4f))
                continue;
            SetXYZ(m_velocity, S_vector(0.0f, -0.4f, 0.0f));
        }
        m_unknown210 = S_vector(m_unknown210.x * 0.8f, m_unknown210.y * 0.8f, m_unknown210.z * 0.8f);
    }
    m_unknown600 = state;
    m_unknown554 = frozen;
    Update(STEP_MS);
    m_soundsOn = sounds;
    m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
    SetXYZ(m_velocity, S_vector(speed * m_modelAxisZ.x, speed * m_modelAxisZ.y, speed * m_modelAxisZ.z));
    m_velocityDir = m_velocity;
    S_vector dir = XYZ(m_velocityDir);
    dir.Normalize();
    SetXYZ(m_velocityDir, dir);
    m_speed = std::fabs(speed);
    m_unknownF6C = features;
    m_unknownF54 = maxStep;
    return false;
}
