// C_Vehicle: the driving controls, the engine switch, impulses (another car's hit with its dent and
// wear), Reset, the collision volumes and the strut update (original 0x004f2030..0x004f2ef2,
// 0x004f4c50 GetEngineRpm, 0x004f4c80 GetSize, 0x004e95b0 Reset, 0x004f5620 SetSpecialVolumes,
// 0x004f5840 SetVolumesInWorld, 0x004fe610 EndSkidMark, 0x00500fd0 UpdateStruts).
#include "game/vehicle.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "ls3d/i3d.h"

namespace {

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

void SetXYZ(S_vector4 &v, const S_vector &xyz)
{
    v.x = xyz.x;
    v.y = xyz.y;
    v.z = xyz.z;
}

// The throttle's and brake's range: above 1 is 1, below 0.05 nothing.
float PedalValue(float value)
{
    if (value > 1.0f)
        return 1.0f;
    if (value < 0.05f)
        return 0.0f;
    return value;
}

// Adds `step` to `time` (at most `total`) and gives the fraction of `total` reached.
float EaseIn(float &time, float step, float total)
{
    time += step;
    if (time > total)
        time = total;
    return time / total;
}

}  // namespace

// --- the controls -------------------------------------------------------------------------------

bool C_Vehicle::SetGear(int gear)                                               // 0x004f2030
{
    if (gear < -1 || gear > m_gearCount)
        return false;
    if (m_requestedGear == gear)
        return true;
    if (m_unknown550 == 1 && gear < 0)
        return false;
    if ((m_unknownF6C & 0x4000) && !(m_unknown63C > 0.0f))
        return true;
    if (gear != -1 && m_gearRatios[gear + 1] / m_gearRatios[m_gear + 1] * m_engineRevs > m_revLimit)
        return false;
    m_unknown780 = 0.0f;
    if (m_unknown910 & 0x20) {
        m_unknown910 = (m_unknown910 & ~0x1u) | 0x2u;
        m_unknown22C6 = 1;
    }
    m_requestedGear = gear;
    return true;
}

bool C_Vehicle::SetThrottle(float value, bool first)                            // 0x004f2100
{
    value = PedalValue(value);
    if (m_unknown550 == 1) {
        (first ? m_throttle : m_unknown700) = m_engineOn ? value : 0.0f;
        m_brake = 0.0f;
        return true;
    }
    if (m_unknown550 != 0 && m_unknown550 != 2)
        return true;
    m_unknown700 = 0.0f;
    if (m_automatic && m_gear < 0) {
        // In reverse the throttle brakes.
        m_brake = m_unknown229B ? value : EaseIn(m_unknown724, m_unknown7C4, m_unknown720) * value;
        m_unknown600 &= ~0x01000000u;
        return true;
    }
    m_throttle = m_engineOn ? value : 0.0f;
    return true;
}

bool C_Vehicle::SetBrake(float value)                                           // 0x004f2270
{
    value = PedalValue(value);
    if (m_unknown550 == 1)
        return false;
    if (m_automatic && m_gear < 0) {
        // In reverse the brake drives.
        m_throttle = 0.0f;
        m_unknown700 = 0.0f;
        if (m_engineOn)
            m_throttle = value;
        return true;
    }
    m_brake = m_unknown229B ? value : EaseIn(m_unknown724, m_unknown7C4, m_unknown720) * value;
    if (m_automatic)
        m_unknown600 &= ~0x01000000u;
    return true;
}

bool C_Vehicle::SetUnknown4f23b0(float value)                                   // 0x004f23b0
{
    if (value > 1.0f)
        value = 1.0f;
    else if (value < 0.0f)
        value = 0.0f;
    m_unknown908 = m_unknown229C ? ((1.0f - value) * m_unknown5F8 + value) * value : value;
    return true;
}

bool C_Vehicle::SetSteering(float value)                                        // 0x004f2420
{
    if (value > 1.0f)
        value = 1.0f;
    else if (value < -1.0f)
        value = -1.0f;
    // The curve mirrored for the left.
    if (value > 0.0f)
        m_steering = value * ((1.0f - value) * m_unknown5F4 + value) * m_unknown934;
    else
        m_steering = -(value * (value - (1.0f + value) * m_unknown5F4)) * m_unknown934;
    return true;
}

bool C_Vehicle::SetEngine(bool on, bool instant)                                // 0x004f2530
{
    if (instant) {
        m_unknownF48 = 5000.0f;
        m_unknown22A0 = 0;
        if (on) {
            m_engineOn = true;
            m_unknown22A2 = 1;
            m_unknownF48 = 0.0f;
        } else {
            m_engineRevs = 0.0f;
            m_engineOn = false;
            m_unknown22A2 = 0;
#ifndef HD2_DEDICATED   // no sounds on the server
            StopSounds();
#endif
        }
        return true;
    }
    if (on == m_engineOn)
        return true;
    if (on && (!(m_fuel > 0.02f) || ((m_unknownF6C & 0x4000) && !(m_unknown610 > 0.0f)))) {
#ifndef HD2_DEDICATED   // no sounds on the server
        if (m_soundsOn)
            PlayEngineFail();
#endif
        return true;
    }
#ifndef HD2_DEDICATED   // the server's has no SwitchEngine: the engine flag stays as it was
    SwitchEngine(on);
#endif
    if (!m_engineOn) {
        m_throttle = 0.0f;
        m_unknown700 = 0.0f;
        m_unknown22A0 = 0;
    }
    return true;
}

bool C_Vehicle::SetUnknown4f2630(float value)                                   // 0x004f2630
{
    m_unknown790 = value;
    if (value > 0.0f)
        m_unknown790 = value + 0.5f;
    return true;
}

bool C_Vehicle::SetFuel(float litres)                                           // 0x004f2670
{
    m_fuel = litres > m_tankSize ? m_tankSize : litres;
    return true;
}

bool C_Vehicle::SetHandbrake(bool on, bool unknown)                             // 0x004f26a0
{
    if (!on) {
        m_handbrake = 0.0f;
        m_unknown710 = 0.0f;
        m_unknown22C7 = 0;
        return true;
    }
    m_handbrake = EaseIn(m_unknown710, m_unknown7C4, m_unknown70C);
    m_unknown22C7 = unknown;
    return true;
}

void C_Vehicle::EnableSounds(bool on)                                           // 0x004f2710
{
    if (!on && m_soundsOn)
        StopSounds();
    m_soundsOn = on;
}

float C_Vehicle::GetEngineRpm() const                                           // 0x004f4c50
{
    return m_engineOn ? m_engineRevs * 60.0f : 0.0f;
}

void C_Vehicle::GetCameraOffset(S_vector *offset) const                         // 0x004f3630
{
    offset->x = 0.0f;
    offset->y = m_unknown67C;
    offset->z = 0.0f;
}

const S_vector4 &C_Vehicle::GetSize(bool unscaled) const                       // 0x004f4c80
{
    return unscaled ? m_size : m_scaledSize;
}

void C_Vehicle::ApplyImpulse(const S_vector &impulse, const S_vector &point, uint8_t flags,
                             const S_vector &factors)                           // 0x004f2790
{
    const S_vector arm = point - XYZ(m_unknown1B0);
    if (!(flags & 0x1)) {
        SetXYZ(m_velocity, XYZ(m_velocity) + impulse * (1.0f / m_currentMass));
        m_velocityDir = m_velocity;
        S_vector dir = XYZ(m_velocityDir);
        dir.Normalize();
        SetXYZ(m_velocityDir, dir);
    }
    if (!(flags & 0x2)) {
        // The turn in the body's space through the inverse inertia, and back.
        S_vector turn = arm.Cross(impulse);
        turn *= m_unknown350;
        turn = S_vector(turn.x * m_unknown220.x * factors.x, turn.y * m_unknown220.y * factors.y,
                        turn.z * m_unknown220.z * factors.z);
        turn *= m_unknown310;
        m_unknown210 -= turn;
    }
    m_unknown22C0 = 1;
    if (auto callback = reinterpret_cast<T_VehicleCallback>(m_callbacks[VEHICLE_CALLBACK_IMPULSE]))
        callback(m_callbackContext);
}

void C_Vehicle::AddForce(const S_vector &force, const S_vector &point, uint8_t flags,
                         const S_vector &factors)                               // 0x004f2a30
{
    if (!(flags & 0x1))
        m_unknown270 += force;
    if (!(flags & 0x2)) {
        // The torque in the body's space, times the factors, and back.
        S_vector torque = force.Cross(point - XYZ(m_unknown1B0));
        torque *= m_unknown350;
        torque = S_vector(torque.x * factors.x, torque.y * factors.y, torque.z * factors.z);
        torque *= m_unknown310;
        m_unknown280 += torque;
    }
    m_unknown22C0 = 1;
    if (auto callback = reinterpret_cast<T_VehicleCallback>(m_callbacks[VEHICLE_CALLBACK_IMPULSE]))
        callback(m_callbackContext);
}

void C_Vehicle::ApplyCarHit(C_Actor *other, float scale, const S_vector4 &point, const S_vector &impulse,
                            const S_vector &normal, int)                        // 0x004f2b60
{
    const S_vector arm = XYZ(point) - XYZ(m_unknown1B0);
    const S_vector push = impulse * scale;
    SetXYZ(m_velocity, XYZ(m_velocity) + push * (1.0f / m_currentMass));
    // The turn in the body's space through the inverse inertia, and back (ApplyImpulse's).
    S_vector turn = arm.Cross(push);
    turn *= m_unknown350;
    turn = S_vector(turn.x * m_unknown220.x, turn.y * m_unknown220.y, turn.z * m_unknown220.z);
    turn *= m_unknown310;
    m_unknown210 -= turn;

    // The push's size (the SDK's test on the float's bits: below 1e-8, or negative, is none).
    const float square = (push.z * push.z + push.y * push.y) + push.x * push.x;
    int32_t squareBits;
    std::memcpy(&squareBits, &square, sizeof squareBits);
    float size;
    if (squareBits < 0x322bcc77)
        size = 0.0f;
    else if (std::fabs(square - 1.0f) < 1e-8f)
        size = 1.0f;
    else
        size = std::sqrt(square);
    if (m_unknownF6C & VEHICLE_SIM_DEFORM) {
        const S_vector against = -normal;
        Deform(point, S_vector4{against.x, against.y, against.z, 0.0f}, 0.45f, size, 0, nullptr);
    }
    if (m_unknownF6C & VEHICLE_SIM_IMPACT_DAMAGE) {
        ImpactDamage(size);
        if (auto callback = reinterpret_cast<T_VehicleImpactCallback>(m_callbacks[VEHICLE_CALLBACK_IMPACT]))
            callback(m_callbackContext, &push, &push, &point, 0.45f, reinterpret_cast<const S_vector *>(&m_velocity),
                     other, nullptr, 1);
    }

    m_velocityDir = m_velocity;
    S_vector dir = XYZ(m_velocityDir);
    dir.Normalize();
    SetXYZ(m_velocityDir, dir);
    m_unknown22C0 = 1;
    if (auto callback = reinterpret_cast<T_VehicleCallback>(m_callbacks[VEHICLE_CALLBACK_IMPULSE]))
        callback(m_callbackContext);
}

// --- Reset ----------------------------------------------------------------------------------------

void C_Vehicle::EndSkidMark(int wheel)                                          // 0x004fe610
{
    S_VehicleWheel *w = m_wheels[wheel];
    if (w->skidMark >= w->skidMarks.size() || !w->skidMarks[w->skidMark].active)
        return;
    w->skidMarks[w->skidMark].active = false;
    if (++w->skidMark >= w->skidMarks.size())
        w->skidMark = 0;
}

void C_Vehicle::Reset(uint8_t flags)                                            // 0x004e95b0 (DS 0x004d8e00)
{
    // Fix (all three wheel loops): a missing wheel is skipped; the original crashes (see Setup).
    if (flags & VEHICLE_RESET_HIDE_SKID_MARKS) {
        for (int i = 0; i < m_wheelCount; ++i) {
            S_VehicleWheel *wheel = m_wheels[i];
            if (!wheel)
                continue;
            for (size_t k = wheel->skidMarks.size(); k-- > 0;) {
                wheel->skidMarks[k].active = false;
                wheel->skidMarks[k].frame0->SetOn(false);
            }
        }
    }
    if (flags & VEHICLE_RESET_UNLINK_SKID_MARKS) {
        for (int i = 0; i < m_wheelCount; ++i) {
            S_VehicleWheel *wheel = m_wheels[i];
            if (!wheel)
                continue;
            EndSkidMark(i);
            for (size_t k = wheel->skidMarks.size(); k-- > 0;) {
                wheel->skidMarks[k].frame0->LinkTo(nullptr, 0);
                wheel->skidMarks[k].frame1->LinkTo(nullptr, 0);
            }
        }
    }

    m_unknown210 *= m_unknown350;
    S_vector velocity = XYZ(m_velocity);
    velocity *= m_unknown350;
    SetXYZ(m_velocity, velocity);

#ifndef HD2_DEDICATED   // the server has no lamps or projectors
    // The lamps' and projectors' objects out of the scene.
    for (size_t i = m_lamps.size(); i-- > 0;) {
        const S_VehicleLamp &lamp = m_lamps[i];
        if (!lamp.projector || (lamp.flags & VEHICLE_LAMP_SHARED))
            continue;
        lamp.projector->LinkTo(nullptr, 0);
        if (lamp.light)
            lamp.light->LinkTo(nullptr, 0);
    }
    for (size_t i = m_projectors.size(); i-- > 0;) {
        m_projectors[i].projector->LinkTo(nullptr, 0);
        if (m_projectors[i].light)
            m_projectors[i].light->LinkTo(nullptr, 0);
    }
#endif

    // The effects.
    for (int i = 0; i < m_wheelCount; ++i) {
        S_VehicleWheel *wheel = m_wheels[i];
        if (wheel && wheel->effect >= 0) {
            StopEffect(wheel->effect);
            wheel->effect = -1;
        }
    }
    for (size_t i = m_exhausts.size(); i-- > 0;) {
        if (m_exhausts[i].effect >= 0) {
            StopEffect(m_exhausts[i].effect);
            m_exhausts[i].effect = -1;
        }
    }
    if (m_unknown638 >= 0) {
        StopEffect(m_unknown638);
        m_unknown638 = -1;
    }

    SetVolumesInWorld(false, false);
    m_bodyContacts.clear();
    m_bodyContacts.shrink_to_fit();
    for (int i = 3; i-- > 0;) {
        if (m_bodyContactEffects[i] >= 0) {
            StopEffect(m_bodyContactEffects[i]);
            m_bodyContactEffects[i] = -1;
        }
    }
#ifndef HD2_DEDICATED   // no sounds on the server
    StopSounds();
#endif
}

// --- the collision volumes -------------------------------------------------------------------

namespace {

// A volume at the model's position into the collision world.
void AddVolumeAt(S_CollisionVolume &volume, const S_vector4 &pos)
{
    volume.center = XYZ(pos);
    volume.centerW = pos.w;
    g_collisionWorld.AddVolume(&volume);
    g_collisionWorld.MoveVolume(&volume);
}

}  // namespace

void C_Vehicle::AddSpecialVolumes()
{
    for (size_t i = m_specialVolumes.size(); i-- > 0;) {
        S_CollisionVolume &volume = m_specialVolumes[i];
        S_VehicleSpecialCollision &special = m_specialCollisions[i];
        if (special.parent) {
            // On a moving part: the dummy through the part's matrix (the volume's).
            const S_matrix matrix = *volume.matrix;
            volume.center = XYZ(special.localPos) * matrix;
            special.invParentMatrix = matrix;
            special.invParentMatrix.Inverse(matrix);
        } else {
            volume.center = XYZ(m_modelPos);
            volume.centerW = m_modelPos.w;
        }
        if (m_specialCollisionDescs[i].unknown04) {
            g_collisionWorld.AddVolume(&volume);
            g_collisionWorld.MoveVolume(&volume);
        }
    }
}

void C_Vehicle::SetVolumesInWorld(bool main, bool second)                      // 0x004f5840
{
    if (main != m_mainVolumesInWorld) {
        m_mainVolumesInWorld = main;
        if (main) {
            if (m_specialVolumesOn)
                AddSpecialVolumes();
            else
                AddVolumeAt(m_bodyVolume, m_modelPos);
        } else if (m_specialVolumesOn) {
            for (size_t i = m_specialVolumes.size(); i-- > 0;)
                g_collisionWorld.RemoveVolume(&m_specialVolumes[i]);
        } else {
            g_collisionWorld.RemoveVolume(&m_bodyVolume);
        }
    }
    if (second != m_volume490InWorld) {
        m_volume490InWorld = second;
        if (second)
            AddVolumeAt(m_volume490, m_modelPos);
        else
            g_collisionWorld.RemoveVolume(&m_volume490);
    }
}

bool C_Vehicle::SetSpecialVolumes(bool on)                                      // 0x004f5620
{
    if (m_specialVolumesOn == on)
        return true;
    if (m_specialVolumes.empty())
        return false;
    // Quirk kept: the old volumes are taken out of the world even when they are not in it
    // (harmless: RemoveVolume ignores a volume outside the grid).
    if (m_specialVolumesOn) {
        for (size_t i = m_specialVolumes.size(); i-- > 0;)
            g_collisionWorld.RemoveVolume(&m_specialVolumes[i]);
    } else {
        g_collisionWorld.RemoveVolume(&m_bodyVolume);
    }
    m_specialVolumesOn = on;
    if (!m_mainVolumesInWorld)
        return true;
    if (on)
        AddSpecialVolumes();
    else
        AddVolumeAt(m_bodyVolume, m_modelPos);
    return true;
}

// --- the struts -----------------------------------------------------------------------------

void C_Vehicle::UpdateStruts()                                                  // 0x00500fd0
{
    const bool on = (m_unknownF6C & 0x1) != 0;
    if (((m_unknownF70 & 0x1) != 0) != on) {
        for (size_t i = m_sideStruts.size(); i-- > 0;)
            m_sideStruts[i].frame->SetOn(on);
        for (size_t i = m_axleStruts.size(); i-- > 0;)
            m_axleStruts[i].frame->SetOn(on);
    }
    if (!on)
        return;

    // Fix (both loops): a strut of a missing wheel (or of an axle out of range) is left alone;
    // the original reads past the wheels or through a null one.
    const auto wheelAt = [&](int index) -> S_VehicleWheel * {
        return index >= 0 && index < m_wheelCount ? m_wheels[index] : nullptr;
    };

    // One side's strut: from its place to the wheel (plus the offset), as long as the gap.
    for (size_t i = m_sideStruts.size(); i-- > 0;) {
        const S_VehicleStrut &strut = m_sideStruts[i];
        const S_VehicleWheel *wheel = wheelAt(strut.left ? strut.axle * 2 : strut.axle * 2 + 1);
        if (!wheel)
            continue;
        const S_vector wheelPos = wheel->frame->GetPos() * wheel->holder->GetLocalMatrix();
        S_vector dir = strut.offset + wheelPos - strut.frame->GetPos();
        strut.frame->SetScale(S_vector(1.0f, 1.0f, dir.Magnitude()));
        dir.Normalize();
        strut.frame->SetWorldDir(dir, 0.0f);
    }

    // A whole axle's strut: at the right wheel (the offset mirrored), as long as the gap to the
    // left one, along the line between the holders.
    for (size_t i = m_axleStruts.size(); i-- > 0;) {
        const S_VehicleStrut &strut = m_axleStruts[i];
        const S_VehicleWheel *left = wheelAt(strut.axle * 2);
        const S_VehicleWheel *right = wheelAt(strut.axle * 2 + 1);
        if (!left || !right)
            continue;
        const S_vector mirrored(-strut.offset.x, strut.offset.y, strut.offset.z);
        const S_vector from = mirrored + right->frame->GetPos() * right->holder->GetLocalMatrix();
        const S_vector to = strut.offset + left->frame->GetPos() * left->holder->GetLocalMatrix();
        S_vector dir = left->holder->GetPos() - right->holder->GetPos();
        strut.frame->SetPosInline(from);
        strut.frame->SetScale(S_vector(1.0f, 1.0f, (to - from).Magnitude()));
        dir.Normalize();
        strut.frame->SetWorldDir(dir, 0.0f);
    }
}
