// C_Vehicle: the parameter block (original 0x004ed270 ApplyParams, 0x004f2000 GetParams).
#include "game/vehicle.h"

#include <cstring>

#include "game/pending.h"                               // g_tyreTypes

namespace {

// The original's float constants (the conversions use these exact values).
constexpr float KMH_TO_MS = 0.2777778f;                 // 1 / 3.6
constexpr float MS_TO_KMH = 3.6f;
constexpr float DEG_TO_RAD = 0.017453292f;
constexpr float RAD_TO_DEG = 57.295776f;
constexpr float DEG_TO_RAD_6B4 = 0.017453279f;          // the one conversion with a slightly different constant
constexpr float PER_MINUTE = 0.016666668f;              // revolutions per minute -> per second
constexpr float TWO_PI = 6.283185f;

// The shape of every check: a valid value is applied; an invalid one ends ApplyParams (false), or
// with VEHICLE_PARAMS_FIX is replaced in the block (`restore`) and applying goes on. The
// conditions are written as the original's x87 tests: most let a NaN through as valid.
template <class T_Apply, class T_Restore>
bool Check(bool invalid, bool fix, T_Apply &&apply, T_Restore &&restore)
{
    if (!invalid) {
        apply();
        return true;
    }
    if (!fix)
        return false;
    restore();
    return true;
}

// `percent` of the revolution range idle..limit.
float RevsAt(float percent, float idle, float limit)
{
    return percent * 0.01f * (limit - idle) + idle;
}

}  // namespace

bool C_Vehicle::ApplyParams(S_VehicleParams &p, uint8_t flags)                // 0x004ed270
{
    const bool fix = (flags & VEHICLE_PARAMS_FIX) != 0;

    m_unknownF7C = 0;
    m_unknown140.x = p.unknown6B8[0];
    m_unknown140.y = p.unknown6B8[1];
    m_unknown140.z = p.unknown6B8[2];
    m_unknown67C = p.unknown6C4;
    m_unknown2289 = p.unknown698;
    m_unknown228C = p.unknown121C;
    if (!Check(p.unknown688 < 0.0f, fix, [&] { m_unknown6F4 = p.unknown688; },
               [&] { p.unknown688 = m_unknown6F4; }))
        return false;
    if (!Check(p.unknown68C < 1.0f, fix, [&] { m_unknown6F8 = p.unknown68C; },
               [&] { p.unknown68C = m_unknown6F8; }))
        return false;
    m_unknown674 = p.unknown690;
    m_unknown228F = p.unknown121D;
    m_unknown2290 = p.unknown121E;
    m_unknown550 = p.unknown121F ? 2 : 0;
    m_unknown518 = p.unknown694;
    m_unknown510 = p.unknown69C;
    m_unknown514 = p.unknown6A0;
    if (!Check(p.unknown6A4 < 0, fix, [&] { m_unknown754 = p.unknown6A4; },
               [&] { p.unknown6A4 = m_unknown754; }))
        return false;
    if (!Check(p.unknown6A8 < 0.0f, fix, [&] { m_unknown758 = p.unknown6A8 * KMH_TO_MS; },
               [&] { p.unknown6A8 = m_unknown758 * MS_TO_KMH; }))
        return false;
    if (!Check(p.unknown6AC < 0.0f, fix, [&] { m_unknown75C = p.unknown6AC; },
               [&] { p.unknown6AC = m_unknown75C; }))
        return false;
    if (!Check(p.unknown6B0 < p.unknown6AC, fix, [&] { m_unknown760 = p.unknown6B0; },
               [&] { p.unknown6B0 = m_unknown760; }))
        return false;
    m_unknown504 = p.unknown6B4 * DEG_TO_RAD_6B4;

    // The inertia (the block's order: y, z, x); the vehicle keeps its inverse.
    if (!Check(p.inertia[0] <= 0.0f, fix, [&] { m_inverseInertia.y = 1.0f / p.inertia[0]; },
               [&] { p.inertia[0] = 1.0f / m_inverseInertia.y; }))
        return false;
    if (!Check(p.inertia[1] <= 0.0f, fix, [&] { m_inverseInertia.z = 1.0f / p.inertia[1]; },
               [&] { p.inertia[1] = 1.0f / m_inverseInertia.z; }))
        return false;
    if (!Check(p.inertia[2] <= 0.0f, fix, [&] { m_inverseInertia.x = 1.0f / p.inertia[2]; },
               [&] { p.inertia[2] = 1.0f / m_inverseInertia.x; }))
        return false;
    m_unknown220 = m_inverseInertia;

    // The centre of mass within the body's box (z, y, x).
    for (int axis = 2; axis >= 0; --axis) {
        float &value = p.centreOfMass[axis];
        float &fraction = m_centreOfMassFraction[axis];
        if (!Check(value < 0.0f || value > 1.001f, fix, [&] { fraction = value; }, [&] { value = fraction; }))
            return false;
        if (fraction > 1.0f)
            fraction = 1.0f;
    }
    m_centreOfMass.x = (m_bodyMax.x - m_bodyMin.x) * m_centreOfMassFraction[0] + m_bodyMin.x;
    m_centreOfMass.y = (m_bodyMax.y - m_bodyMin.y) * m_centreOfMassFraction[1] + m_bodyMin.y;
    m_centreOfMass.z = (m_bodyMax.z - m_bodyMin.z) * m_centreOfMassFraction[2] + m_bodyMin.z;
    m_unknown1E0 = m_centreOfMass;

    if (!Check(p.gravity <= 0.0f, fix, [&] { m_gravity = p.gravity; }, [&] { p.gravity = m_gravity; }))
        return false;
    if (!Check(p.mass <= 0.0f, fix, [&] { m_mass = p.mass; }, [&] { p.mass = m_mass; }))
        return false;
    m_currentMass = m_mass;
    // (The original stores stack garbage in w, here and in m_unknown140.)
    m_weight.x = 0.0f;
    m_weight.y = -(m_mass * m_gravity);
    m_weight.z = 0.0f;
    if (!Check(p.unknown67C <= 0.0f, fix, [&] { m_unknown720 = p.unknown67C; },
               [&] { p.unknown67C = m_unknown720; }))
        return false;
    if (!Check(p.unknown678 < 0.0f, fix, [&] { m_unknown71C = p.unknown678; },
               [&] { p.unknown678 = m_unknown71C; }))
        return false;
    if (!Check(p.unknown684 <= 0.0f, fix, [&] { m_unknown70C = p.unknown684; },
               [&] { p.unknown684 = m_unknown70C; }))
        return false;
    if (!Check(p.unknown680 < 0.0f, fix, [&] { m_unknown708 = p.unknown680; },
               [&] { p.unknown680 = m_unknown708; }))
        return false;

    // --- the axles (each value goes to both wheels of the axle) ------------------------------
    m_maxWheelRadius = 0.0001f;
    m_maxWheelCircumference = 0.01f;
    for (int a = 0; a < m_axleCount; ++a) {
        for (int i = a * 2; i < a * 2 + 2; ++i) {
            S_VehicleWheel &w = *m_wheels[i];
            if (!Check(p.axleUnknown15C[a] <= 0.0f, fix, [&] { w.unknown160 = p.axleUnknown15C[a] * 1000.0f; },
                       [&] { p.axleUnknown15C[a] = w.unknown160 * 0.001f; }))
                return false;
            if (!Check(p.axleUnknown18C[a] <= 0.009f, fix, [&] { w.unknown164 = p.axleUnknown18C[a]; },
                       [&] { p.axleUnknown18C[a] = w.unknown164; }))
                return false;
            if (!Check(p.axleUnknown1BC[a] <= 0.0f, fix, [&] { w.unknown170 = p.axleUnknown1BC[a]; },
                       [&] { p.axleUnknown1BC[a] = w.unknown170; }))
                return false;
            if (!Check(p.axleUnknown1EC[a] <= 0.0f, fix, [&] { w.unknown174 = p.axleUnknown1EC[a]; },
                       [&] { p.axleUnknown1EC[a] = w.unknown174; }))
                return false;
            if (!Check(p.axleUnknown21C[a] < -10.0f || p.axleUnknown21C[a] > 10.0f, fix,
                       [&] { w.unknown178 = p.axleUnknown21C[a] * DEG_TO_RAD; },
                       [&] { p.axleUnknown21C[a] = w.unknown178 * RAD_TO_DEG; }))
                return false;
            if (!Check(p.axleUnknown24C[a] < -10.0f || p.axleUnknown24C[a] > 10.0f, fix,
                       [&] { w.unknown150 = p.axleUnknown24C[a] * DEG_TO_RAD; },
                       [&] { p.axleUnknown24C[a] = w.unknown150 * RAD_TO_DEG; }))
                return false;
            if (!Check(p.axleUnknown27C[a] < 0.0f, fix, [&] { w.unknown154 = p.axleUnknown27C[a] * DEG_TO_RAD; },
                       [&] { p.axleUnknown27C[a] = w.unknown154 * RAD_TO_DEG; }))
                return false;
            if (!Check(p.axleUnknown2AC[a] < 0.0f, fix, [&] { w.unknown158 = p.axleUnknown2AC[a] * DEG_TO_RAD; },
                       [&] { p.axleUnknown2AC[a] = w.unknown158 * RAD_TO_DEG; }))
                return false;
            // A fixed tyre type becomes 0 in the block; the wheel keeps its own.
            if (!Check(p.axleTyreType[a] < 0, fix, [&] { w.tyreType = p.axleTyreType[a]; },
                       [&] { p.axleTyreType[a] = 0; }))
                return false;
            // The last type in tables\tyres.bin with the wheel's id (-1, and no type, when none).
            int index = static_cast<int>(g_tyreTypes.size());
            while (--index >= 0 && g_tyreTypes[index].id != w.tyreType) {
            }
            w.tyreIndex = index;
            if (index < 0)
                w.tyreType = -1;
            w.surface = 0;
            if (!Check(p.axleUnknown30C[a] <= 0.0f, fix, [&] { w.unknown18C = p.axleUnknown30C[a] * 1000.0f; },
                       [&] { p.axleUnknown30C[a] = w.unknown18C * 0.001f; }))
                return false;
            if (!Check(p.axleUnknown33C[a] < 1.0f, fix, [&] { w.unknown194 = p.axleUnknown33C[a] * DEG_TO_RAD; },
                       [&] { p.axleUnknown33C[a] = w.unknown194 * RAD_TO_DEG; }))
                return false;
            if (!Check(p.axleUnknown36C[a] < 1.0f, fix, [&] { w.unknown198 = p.axleUnknown36C[a] * DEG_TO_RAD; },
                       [&] { p.axleUnknown36C[a] = w.unknown198 * RAD_TO_DEG; }))
                return false;
            if (!Check(p.axleUnknown39C[a] < 0.0f || p.axleUnknown39C[a] > 1.0f, fix,
                       [&] { w.unknown19C = p.axleUnknown39C[a]; }, [&] { p.axleUnknown39C[a] = w.unknown19C; }))
                return false;
            if (!Check(p.axleUnknown3CC[a] < 0.0f || p.axleUnknown3CC[a] > 1.0f, fix,
                       [&] { w.unknown190 = p.axleUnknown3CC[a]; }, [&] { p.axleUnknown3CC[a] = w.unknown190; }))
                return false;
            if (!Check(p.axleUnknown3FC[a] <= 0.0f, fix, [&] { w.unknown1B8 = p.axleUnknown3FC[a]; },
                       [&] { p.axleUnknown3FC[a] = w.unknown1B8; }))
                return false;
            if (!Check(p.axleUnknown42C[a] <= 0.0f, fix, [&] { w.unknown1BC = p.axleUnknown42C[a]; },
                       [&] { p.axleUnknown42C[a] = w.unknown1BC; }))
                return false;
            if (!Check(p.axleUnknown45C[a] < 0.0f, fix, [&] { w.unknown1C0 = p.axleUnknown45C[a]; },
                       [&] { p.axleUnknown45C[a] = w.unknown1C0; }))
                return false;
            if (!Check(p.axleUnknown48C[a] <= 0.0f, fix, [&] { w.unknown1C4 = p.axleUnknown48C[a] * 1000.0f; },
                       [&] { p.axleUnknown48C[a] = w.unknown1C4 * 0.001f; }))
                return false;
            if (!Check(p.axleUnknown4BC[a] <= 0.0f, fix, [&] { w.unknown1C8 = p.axleUnknown4BC[a]; },
                       [&] { p.axleUnknown4BC[a] = w.unknown1C8; }))
                return false;
            if (!Check(p.axleUnknown4EC[a] <= 0.0f, fix, [&] { w.unknown1CC = p.axleUnknown4EC[a]; },
                       [&] { p.axleUnknown4EC[a] = w.unknown1CC; }))
                return false;
            if (!Check(p.axleUnknown51C[a] < 0.0f, fix, [&] { w.unknown1D0 = p.axleUnknown51C[a]; },
                       [&] { p.axleUnknown51C[a] = w.unknown1D0; }))
                return false;
            if (!Check(p.axleUnknown54C[a] < 0.0f || p.axleUnknown54C[a] > 100.0f, fix,
                       [&] { w.unknown1F8 = p.axleUnknown54C[a] * 0.01f; },
                       [&] { p.axleUnknown54C[a] = w.unknown1F8 * 100.0f; }))
                return false;
            w.flags = p.axleFlag2[a] ? w.flags | 0x2 : w.flags & ~0x2u;
            w.flags = p.axleFlag1[a] ? w.flags | 0x1 : w.flags & ~0x1u;
            w.flags = p.axleFlag4[a] ? w.flags | 0x4 : w.flags & ~0x4u;
            w.flags = p.axleFlag800[a] ? w.flags | 0x800 : w.flags & ~0x800u;
            w.flags &= ~WHEEL_TRACKED;
            // Followed by a track strip (either list, searched from the end): WHEEL_TRACKED.
            bool tracked = false;
            for (size_t r = m_tracksL.size(); r-- > 0;) {
                if (m_tracksL[r].wheel == i) {
                    w.flags |= WHEEL_TRACKED;
                    tracked = true;
                    break;
                }
            }
            if (!tracked) {
                for (size_t r = m_tracksR.size(); r-- > 0;) {
                    if (m_tracksR[r].wheel == i) {
                        w.flags |= WHEEL_TRACKED;
                        break;
                    }
                }
            }
            // The axle's share of +0x71C: the left (even) wheel's, the right one's. A fixed share
            // comes back from the stored block.
            if (i % 2 == 0) {
                if (!Check(p.axleLeftShare[a] < 0.0f, fix,
                           [&] { w.unknown1F4 = m_unknown71C * p.axleLeftShare[a] * 0.01f; },
                           [&] { p.axleLeftShare[a] = m_params.axleLeftShare[a]; }))
                    return false;
            } else {
                if (!Check(p.axleRightShare[a] < 0.0f, fix,
                           [&] { w.unknown1F4 = m_unknown71C * p.axleRightShare[a] * 0.01f; },
                           [&] { p.axleRightShare[a] = m_params.axleRightShare[a]; }))
                    return false;
            }
            if (!Check(p.axleUnknown5DC[a] < 1.0f || p.axleUnknown5DC[a] > 90.0f, fix,
                       [&] { w.unknown21C = p.axleUnknown5DC[a] * DEG_TO_RAD; },
                       [&] { p.axleUnknown5DC[a] = w.unknown21C * RAD_TO_DEG; }))
                return false;
            if (p.axleUnknown60C[a] > p.axleUnknown63C[a])
                return false;                           // (even when fixing)
            if (!Check(p.axleUnknown60C[a] < 1.0f || p.axleUnknown60C[a] > 90.0f, fix,
                       [&] { w.unknown220 = p.axleUnknown60C[a] * DEG_TO_RAD; },
                       [&] { p.axleUnknown60C[a] = w.unknown220 * RAD_TO_DEG; }))
                return false;
            if (!Check(p.axleUnknown63C[a] < 1.0f || p.axleUnknown63C[a] > 90.0f, fix,
                       [&] { w.unknown224 = p.axleUnknown63C[a] * DEG_TO_RAD; },
                       [&] { p.axleUnknown63C[a] = w.unknown224 * RAD_TO_DEG; }))
                return false;
            if ((w.flags & WHEEL_DRIVEN) && !(w.flags & WHEEL_DUMMY)) {
                const float circumference = w.radius * TWO_PI;
                if (circumference > m_maxWheelCircumference) {
                    m_maxWheelCircumference = circumference;
                    m_maxWheelRadius = w.radius;
                }
            }
            w.unknown24C = p.unknown6C8[0];
            w.unknown248 = p.unknown6C8[1];
            w.unknown254 = p.unknown6C8[2];
            w.unknown250 = p.unknown6C8[3];
            w.unknown258 = p.unknown6C8[4];
        }
    }

    m_unknown228D = p.unknown1220;
    m_unknown5DC = 0.0f;
    for (int i = 0; i < m_wheelCount; ++i) {
        const S_VehicleWheel &w = *m_wheels[i];
        if (!(w.flags & WHEEL_DUMMY))
            m_unknown5DC = w.unknown1F4 / w.radius + m_unknown5DC;
    }
    m_drivenWheels = 0.0f;
    for (int i = 0; i < m_wheelCount; ++i) {
        const uint32_t wheelFlags = m_wheels[i]->flags;
        if ((wheelFlags & WHEEL_DRIVEN) && !(wheelFlags & WHEEL_DUMMY))
            m_drivenWheels = m_drivenWheels + 1.0f;
    }
    if (!(m_unknownF74 & 0x2) && m_drivenWheels == 0.0f)
        return false;                                   // (even when fixing)

    // --- angles and factors (unknown110) ------------------------------------------------------
    float *u110 = p.unknown110;
    if (!Check(u110[6] <= 0.0f || u110[6] > 90.0f, fix, [&] { m_unknown92C = u110[6] * DEG_TO_RAD; },
               [&] { u110[6] = m_unknown92C * RAD_TO_DEG; }))
        return false;
    if (!Check(u110[7] <= 0.0f || u110[7] > 90.0f, fix, [&] { m_unknown930 = u110[7] * DEG_TO_RAD; },
               [&] { u110[7] = m_unknown930 * RAD_TO_DEG; }))
        return false;
    if (!Check(u110[8] < 0.0f || u110[8] > 50.0f || u110[8] > u110[6], fix,
               [&] { m_unknown954 = u110[8] * DEG_TO_RAD; }, [&] { u110[8] = m_unknown954 * RAD_TO_DEG; }))
        return false;
    if (!Check(u110[9] <= 0.0f, fix, [&] { m_unknown938 = u110[9] * DEG_TO_RAD; },
               [&] { u110[9] = m_unknown938 * RAD_TO_DEG; }))
        return false;
    if (!Check(u110[10] <= 0.0f, fix, [&] { m_unknown93C = u110[10] * DEG_TO_RAD; },
               [&] { u110[10] = m_unknown93C * RAD_TO_DEG; }))
        return false;
    if (!Check(u110[11] <= 0.0f, fix, [&] { m_unknown940 = u110[11] * DEG_TO_RAD; },
               [&] { u110[11] = m_unknown940 * RAD_TO_DEG; }))
        return false;
    if (!Check(u110[12] <= 0.0f, fix, [&] { m_unknown944 = u110[12] * DEG_TO_RAD; },
               [&] { u110[12] = m_unknown944 * RAD_TO_DEG; }))
        return false;
    if (!Check(u110[0] < 0.029f || u110[0] > 1.331f, fix, [&] { m_unknown854 = u110[0]; },
               [&] { u110[0] = m_unknown854; }))
        return false;
    if (!Check(u110[1] < 0.0f, fix, [&] { m_unknown858 = u110[1]; }, [&] { u110[1] = m_unknown858; }))
        return false;
    if (!Check(u110[2] < 0.029f || u110[2] > 1.331f, fix, [&] { m_unknown728 = u110[2]; },
               [&] { u110[2] = m_unknown728; }))
        return false;
    if (!Check(u110[3] < 0.0f, fix, [&] { m_unknown72C = u110[3]; }, [&] { u110[3] = m_unknown72C; }))
        return false;
    if (!Check(u110[4] < 0.029f || u110[4] > 1.331f, fix, [&] { m_unknown730 = u110[4]; },
               [&] { u110[4] = m_unknown730; }))
        return false;
    if (!Check(u110[5] < 0.0f, fix, [&] { m_unknown734 = u110[5]; }, [&] { u110[5] = m_unknown734; }))
        return false;
    m_unknown1C0.x = m_centreOfMass.x;
    m_unknown1C0.y = m_centreOfMass.y;
    m_unknown1C0.z = m_bodyMax.z;
    m_unknown1D0.x = m_centreOfMass.x;
    m_unknown1D0.y = m_centreOfMass.y;
    m_unknown1D0.z = m_bodyMin.z;

    // --- the engine's revolutions and the gears ---------------------------------------------------
    float *u004 = p.unknown004;
    if (!Check(u004[2] < 0.0f, fix, [&] { m_unknown87C = u004[2] * PER_MINUTE; },
               [&] { u004[2] = m_unknown87C * 60.0f; }))
        return false;
    if (!Check(u004[3] <= u004[2], fix, [&] { m_revLimit = u004[3] * PER_MINUTE; },
               [&] { u004[3] = m_revLimit * 60.0f; }))
        return false;
    if (!Check(u004[4] <= 0.0f || u004[4] > u004[2], fix, [&] { m_idleRevs = u004[4] * PER_MINUTE; },
               [&] { u004[4] = m_idleRevs * 60.0f; }))
        return false;
    m_unknown870 = m_idleRevs * 0.5f;
    if (!Check(u004[12] < 0.0f, fix, [&] { m_unknown8F8 = u004[12]; }, [] {}))   // (no fix: the block keeps it)
        return false;
    if (!Check(u004[13] < 0.0f, fix, [&] { m_unknown900 = 1.0f / u004[13]; },
               [&] { u004[13] = 1.0f / m_unknown900; }))
        return false;
    if (!Check(u004[14] < 0.0f, fix, [&] { m_unknown8FC = 1.0f / u004[14]; },
               [&] { u004[14] = 1.0f / m_unknown8FC; }))
        return false;
    if (!Check(u004[15] < 0.0f || u004[15] > u004[17], fix, [&] { m_unknown8EC = u004[15] * PER_MINUTE; },
               [&] { u004[15] = m_unknown8EC * 60.0f; }))
        return false;
    if (!Check(u004[16] > u004[17], fix, [&] { m_unknown8F0 = u004[16] * PER_MINUTE; },
               [&] { u004[16] = m_unknown8F0 * 60.0f; }))
        return false;
    if (!Check(u004[17] > u004[2], fix, [&] { m_unknown8F4 = u004[17] * PER_MINUTE; },
               [&] { u004[17] = m_unknown8F4 * 60.0f; }))
        return false;
    if (!Check(u004[18] < 1.0f || u004[18] > 10.0f, fix, [&] { m_gearCount = static_cast<int>(u004[18]); },
               [&] { u004[18] = static_cast<float>(m_gearCount); }))
        return false;
    for (int gear = 1; gear <= VEHICLE_GEARS; ++gear) {
        if (!Check(p.gearRatios[gear] < 0.0f, fix, [&] { m_gearRatios[gear + 1] = p.gearRatios[gear]; },
                   [&] { p.gearRatios[gear] = m_gearRatios[gear + 1]; }))
            return false;
    }
    if (!Check(p.gearRatios[0] < 0.0f, fix, [&] { m_gearRatios[0] = p.gearRatios[0]; },
               [&] { p.gearRatios[0] = m_gearRatios[0]; }))
        return false;
    m_gearRatios[1] = m_gearRatios[2];
    m_topSpeed = m_maxWheelCircumference / m_gearRatios[m_gearCount + 1] * m_revLimit;
    // Per gear the shift points, in percent of the revolutions idle..limit.
    for (int gear = 1; gear <= VEHICLE_GEARS; ++gear) {
        float &up = p.shiftUpPercent[gear];
        if (!Check(up < 0.0f || up > 100.0001f, fix, [&] { m_shiftUpRevs[gear] = RevsAt(up, m_idleRevs, m_revLimit); },
                   [&] { up = m_params.shiftUpPercent[gear]; }))
            return false;
        if (m_shiftUpRevs[gear] > m_revLimit)
            m_shiftUpRevs[gear] = m_revLimit;
        float &down = p.shiftDownPercent[gear];
        if (!Check(down < 0.0f || down > 100.0001f, fix,
                   [&] { m_shiftDownRevs[gear] = RevsAt(down, m_idleRevs, m_revLimit); },
                   [&] { down = m_params.shiftDownPercent[gear]; }))
            return false;
        if (m_shiftDownRevs[gear] < m_idleRevs)
            m_shiftDownRevs[gear] = m_idleRevs;
    }
    m_shiftDownRevs[1] = 0.0f;

    if (!Check(u004[0] <= 0.0f, fix, [&] { m_unknown850 = u004[0] * 1000.0f; },
               [&] { u004[0] = m_unknown850 * 0.001f; }))
        return false;
    if (!Check(u004[1] <= 0.0f, fix, [&] { m_unknown704 = u004[1] * 0.01f; },
               [&] { u004[1] = m_unknown704 * 100.0f; }))
        return false;
    m_unknown850 = m_unknown704 * m_unknown850;
    m_unknown77C = u004[2] * PER_MINUTE;
    m_unknown76C = m_unknown850 / (m_unknown77C * TWO_PI);
    if (!Check(u004[8] <= u004[4] || u004[8] > u004[9], fix, [&] { m_unknown774 = u004[8] * PER_MINUTE; },
               [&] { u004[8] = m_unknown774 * 60.0f; }))
        return false;
    if (!Check(u004[9] < u004[8] || u004[9] >= u004[2], fix, [&] { m_unknown778 = u004[9] * PER_MINUTE; },
               [&] { u004[9] = m_unknown778 * 60.0f; }))
        return false;
    if (!Check(u004[5] <= 0.0f, fix, [&] { m_unknown768 = u004[5]; }, [&] { u004[5] = m_unknown768; }))
        return false;
    if (!Check(u004[6] <= 0.0f, fix, [&] { m_unknown764 = u004[6]; }, [&] { u004[6] = m_unknown764; }))
        return false;
    if (!Check(u004[7] <= 0.0f, fix, [&] { m_unknown770 = u004[7]; }, [&] { u004[7] = m_unknown770; }))
        return false;
    if (!Check(u004[10] <= 0.0f, fix, [&] { m_unknown8E8 = u004[10]; }, [&] { u004[10] = m_unknown8E8; }))
        return false;
    const float idleShare = m_idleRevs / m_revLimit * m_unknown8E8;
    m_unknown524 = idleShare / (idleShare + m_unknown764);
    if (!Check(u004[11] <= 0.0f, fix, [&] { m_unknown738 = u004[11]; }, [&] { u004[11] = m_unknown738; }))
        return false;
    if (!Check(p.tankSize < 1.0f, fix, [&] { m_tankSize = p.tankSize; }, [&] { p.tankSize = m_tankSize; }))
        return false;
    if (!Check(p.unknown158 <= 0.0f, fix,
               [&] {
                   float value = u004[3] * PER_MINUTE * 0.41666666f;
                   value = value / m_gearRatios[m_gearCount + 1];
                   value = 100000.0f / (value * m_maxWheelCircumference);
                   m_unknown784 = p.unknown158 / value * 1.7f;
               },
               [&] { p.unknown158 = m_params.unknown158; }))
        return false;

    float *u7A0 = p.unknown7A0;
    m_unknown95C = u7A0[0x32];
    m_unknown960 = u7A0[0x33];
    m_unknown964 = u7A0[0x34];
    m_unknown968 = u7A0[0x35];
    if (u7A0[0x36] > u7A0[0x37]) {
        if (!fix)
            return false;
        // Quirk kept: the stored block holds km/h already; the fix multiplies by 3.6 again.
        u7A0[0x36] = m_params.unknown7A0[0x36] * MS_TO_KMH;
        u7A0[0x37] = m_params.unknown7A0[0x37] * MS_TO_KMH;
    }
    m_unknown5E4 = u7A0[0x36] * KMH_TO_MS;
    m_unknown5E8 = u7A0[0x37] * KMH_TO_MS;
    m_unknown5EC = u7A0[0x38];
    m_unknown96C = u7A0[0x39] * m_unknown8E8 * -0.01f;
    m_unknown970 = u7A0[0x3A] * m_unknown768 * 0.01f;
    m_unknown974 = u7A0[0x3B];
    m_unknown978 = u7A0[0x3C];
    for (int i = 0; i < VEHICLE_CRASH_SOUNDS; ++i) {
        m_crash1Thresholds[i] = p.crash1Values[i] * KMH_TO_MS * m_currentMass;
        m_crash1Flags[i] = p.crash1Flags[i];
        m_crash2Thresholds[i] = p.crash2Values[i] * KMH_TO_MS * m_currentMass;
        m_crash2Flags[i] = p.crash2Flags[i];
    }

    // --- the sounds' settings (each block only when its sound is named) --------------------------
    if (p.sound1_9B4[0]) {
        if (!Check(p.unknownCF4 < 0.0f || p.unknownCF4 > 100.001f, fix,
                   [&] { m_unknownBFC = RevsAt(p.unknownCF4, m_idleRevs, m_revLimit); },
                   [&] { p.unknownCF4 = m_params.unknownCF4; }))
            return false;
        if (m_unknownBFC > m_revLimit)
            m_unknownBFC = m_revLimit;
        m_unknownC00 = u004[4] * PER_MINUTE;
        if (!Check(p.unknownCF8 < 0.0f || p.unknownCF8 > 1.001f, fix, [&] { m_unknownC04 = p.unknownCF8; },
                   [&] { p.unknownCF8 = m_unknownC04; }))
            return false;
        if (m_unknownC04 > 1.0f)
            m_unknownC04 = 1.0f;
    }
    if (p.sound2_E50[0]) {
        if (!Check(p.unknown11A0[0] < 0.0f || p.unknown11A0[0] > 100.001f, fix,
                   [&] { m_unknownEDC = RevsAt(p.unknown11A0[0], m_idleRevs, m_revLimit); },
                   [&] { p.unknown11A0[0] = m_params.unknown11A0[0]; }))
            return false;
        if (m_unknownEDC > m_revLimit)
            m_unknownEDC = m_revLimit;
        m_unknownEE0 = u004[4] * PER_MINUTE;
        if (!Check(p.unknown11A0[1] < 0.0f || p.unknown11A0[1] > 1.001f, fix,
                   [&] { m_unknownEE4 = p.unknown11A0[1]; }, [&] { p.unknown11A0[1] = m_unknownEE4; }))
            return false;
        if (m_unknownEE4 > 1.0f)
            m_unknownEE4 = 1.0f;
    }
    for (int i = 0; i < 4; ++i)
        m_unknownEE8[i] = p.unknown11A0[2 + i];

    // A pitch range (0.5 <= low < high <= 2.01), two speeds in percent of the top speed and a
    // volume (0..1).
    auto speedSettings = [&](float *v, const float *stored, float &low, float &high, float &speedA, float &top,
                             float &speedB, float &volume) {
        if (v[0] < 0.5f || v[0] >= v[1]) {
            if (!fix)
                return false;
            v[0] = low;
        }
        low = v[0];
        if (!Check(v[1] > 2.01f || v[1] < v[0], fix, [&] { high = v[1]; }, [&] { v[1] = high; }))
            return false;
        if (high > 2.0f)
            high = 2.0f;
        if (!Check(v[2] < 0.0f || v[2] > 100.001f, fix, [&] { speedA = v[2] * 0.01f * m_topSpeed; },
                   [&] { v[2] = stored[2]; }))
            return false;
        if (speedA > m_topSpeed)
            speedA = m_topSpeed;
        top = m_topSpeed;
        if (!Check(v[3] < 0.0f || v[3] > 100.001f, fix, [&] { speedB = v[3] * 0.01f * m_topSpeed; },
                   [&] { v[3] = stored[3]; }))
            return false;
        if (speedB > m_topSpeed)
            speedB = m_topSpeed;
        if (!Check(v[4] < 0.0f || v[4] > 1.001f, fix, [&] { volume = v[4]; }, [&] { v[4] = volume; }))
            return false;
        if (volume > 1.0f)
            volume = 1.0f;
        return true;
    };
    if (p.sound1_9C4[0] && !speedSettings(p.unknownCFC, m_params.unknownCFC, m_unknownC08, m_unknownC0C,
                                          m_unknownC10, m_unknownC14, m_unknownC18, m_unknownC1C))
        return false;
    if (p.sound2_E60[0] && !speedSettings(p.unknown11B8, m_params.unknown11B8, m_unknownEF8, m_unknownEFC,
                                          m_unknownF00, m_unknownF04, m_unknownF08, m_unknownF0C))
        return false;
    for (int i = 0; i < 4; ++i)
        m_unknownF10[i] = p.unknown11B8[5 + i];
    m_unknownC20[0] = 0.55f;
    m_unknownC20[1] = 1.3f;
    m_unknownC20[2] = m_topSpeed * 0.01f;
    m_unknownC20[3] = m_topSpeed * 0.6f;
    m_unknownC20[4] = m_topSpeed * 0.3f;
    m_unknownC20[5] = 1.0f;
    m_unknownF20[0] = 0.55f;
    m_unknownF20[1] = 1.4f;
    m_unknownF20[2] = m_topSpeed * 0.01f;
    m_unknownF20[3] = m_topSpeed * 0.6f;
    m_unknownF20[4] = m_topSpeed * 0.3f;
    m_unknownF20[5] = 1.0f;
    if (p.sounds2[3][0]) {
        float *v = p.unknownFB0;
        if (v[0] < 0.5f || v[0] >= v[1]) {
            if (!fix)
                return false;
            v[0] = m_unknownCEC;
        }
        m_unknownCEC = v[0];
        if (!Check(v[1] > 2.01f || v[1] < v[0], fix, [&] { m_unknownCF0 = v[1]; }, [&] { v[1] = m_unknownCF0; }))
            return false;
        if (m_unknownCF0 > 2.0f)
            m_unknownCF0 = 2.0f;
        if (!Check(v[2] < 0.0f || v[2] > 100.001f, fix, [&] { m_unknownCF4 = RevsAt(v[2], m_idleRevs, m_revLimit); },
                   [&] { v[2] = m_params.unknownFB0[2]; }))
            return false;
        if (m_unknownCF4 > m_revLimit)
            m_unknownCF4 = m_revLimit;
        if (!Check(v[3] < 0.0f || v[3] > 1.001f, fix, [&] { m_unknownCF8 = v[3]; }, [&] { v[3] = m_unknownCF8; }))
            return false;
        if (m_unknownCF8 > 1.0f)
            m_unknownCF8 = 1.0f;
    }

    // --- the sound layers: per layer a pitch range (0.5 <= low < high <= 2.01), two revolution
    // ranges in percent of idle..limit and two volumes (0..1). Groups 1 and 2 differ in how the
    // pitch range is taken (group 2 checks the high pitch against the vehicle's low one).
    auto layer = [&](int group, float (*values)[VEHICLE_SOUND_LAYERS], const float (*stored)[VEHICLE_SOUND_LAYERS],
                     float (*rows)[VEHICLE_SOUND_LAYERS], int i) {
        float *const v[8] = {&values[0][i], &values[1][i], &values[2][i], &values[3][i],
                             &values[4][i], &values[5][i], &values[6][i], &values[7][i]};
        if (*v[0] < 0.5f || *v[0] >= *v[1]) {
            if (!fix)
                return false;
            *v[0] = rows[0][i];
        } else if (group == 2) {
            rows[0][i] = *v[0];
        }
        if (group == 1)
            rows[0][i] = *v[0];
        const float low = group == 1 ? *v[0] : rows[0][i];
        if (!Check(*v[1] > 2.01f || *v[1] < low, fix, [&] { rows[1][i] = *v[1]; }, [&] { *v[1] = rows[1][i]; }))
            return false;
        if (rows[1][i] > 2.0f)
            rows[1][i] = 2.0f;
        for (int row = 2; row <= 5; ++row) {
            // Rows 3 and 5 must not be below rows 2 and 4.
            const float minimum = row % 2 ? *v[row - 1] : 0.0f;
            if (!Check(*v[row] < minimum || *v[row] > 100.001f, fix,
                       [&] { rows[row][i] = RevsAt(*v[row], m_idleRevs, m_revLimit); },
                       [&] { *v[row] = stored[row][i]; }))
                return false;
            if (rows[row][i] > m_revLimit)
                rows[row][i] = m_revLimit;
        }
        for (int row = 6; row <= 7; ++row) {
            if (!Check(*v[row] < 0.0f || *v[row] > 1.001f, fix, [&] { rows[row][i] = *v[row]; },
                       [&] { *v[row] = rows[row][i]; }))
                return false;
            if (rows[row][i] > 1.0f)
                rows[row][i] = 1.0f;
        }
        return true;
    };
    for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i) {
        if (p.layers1A[i][0]) {
            if (!layer(1, p.layers1AValues, m_params.layers1AValues, m_layers1[0], i))
                return false;
            m_layers1AFlags[i] = p.layers1AFlags[i];
        }
        if (p.layers2A[i][0]) {
            if (!layer(2, p.layers2AValues, m_params.layers2AValues, m_layers2[0], i))
                return false;
            m_layers2AFlags[i] = p.layers2AFlags[i];
        }
    }
    for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i) {
        if (p.layers1B[i][0]) {
            if (!layer(1, p.layers1BValues, m_params.layers1BValues, m_layers1[1], i))
                return false;
            m_layers1BFlags[i] = p.layers1BFlags[i];
        }
        if (p.layers2B[i][0]) {
            if (!layer(2, p.layers2BValues, m_params.layers2BValues, m_layers2[1], i))
                return false;
            m_layers2BFlags[i] = p.layers2BFlags[i];
        }
    }
    for (int i = 0; i < VEHICLE_SOUND_LAYERS; ++i) {
        if (p.layers1C[i][0] && !layer(1, p.layers1CValues, m_params.layers1CValues, m_layers1[2], i))
            return false;
        if (p.layers2C[i][0] && !layer(2, p.layers2CValues, m_params.layers2CValues, m_layers2[2], i))
            return false;
    }

    // --- the rest: copied as they are -----------------------------------------------------------
    const float *u6DC = p.unknown6DC;
    m_unknown61C = u6DC[0] * KMH_TO_MS * m_currentMass;
    m_unknown620 = u6DC[1] * KMH_TO_MS * m_currentMass;
    m_unknown614 = u6DC[2];
    m_unknown618 = u6DC[3];
    m_unknown608 = u6DC[4];
    m_unknown624 = u6DC[5];
    m_unknown628 = u6DC[6];
    m_unknown62C = u6DC[7] * KMH_TO_MS;
    std::memcpy(&m_unknown630, &u6DC[8], sizeof m_unknown630);   // (ints: the bits)
    std::memcpy(&m_unknown634, &u6DC[9], sizeof m_unknown634);
    m_unknown648 = u6DC[10] * KMH_TO_MS * m_currentMass;
    m_unknown64C = u6DC[11] * KMH_TO_MS * m_currentMass;
    m_unknown640 = u6DC[12];
    m_unknown644 = u6DC[13];
    for (int i = 0; i < m_wheelCount; ++i) {
        S_VehicleWheel &w = *m_wheels[i];
        w.maxBend = u6DC[18] * DEG_TO_RAD;
        w.fullHealth = u6DC[16];
        w.bendHealth = u6DC[19];
        w.fullImpulse = u6DC[14] * KMH_TO_MS * m_currentMass;
        w.minImpulse = u6DC[15] * KMH_TO_MS * m_currentMass;
        w.absorbed = u6DC[17];
    }
    m_lampFullImpulse = u7A0[0] * KMH_TO_MS * m_currentMass;
    m_lampMinImpulse = u7A0[1] * KMH_TO_MS * m_currentMass;
    m_lampFullHealth = u7A0[2];
    m_lampBreakHealth = u7A0[3];
    m_lampAbsorbed = u7A0[4];

    // The damage parts' values, each from the group its type selects: the full and the least
    // impulse (km/h x mass), the full health, the absorbed percentage, the furthest dents (cm) and
    // (glass and lights) the health they crack below.
    const float *u72C = p.unknown72C;
    for (S_VehiclePart &r : m_parts) {
        const float *g;
        switch (r.type) {
        case 1:
            r.fullImpulse = u72C[0] * KMH_TO_MS * m_currentMass;
            r.minImpulse = u72C[1] * KMH_TO_MS * m_currentMass;
            r.fullHealth = 100.0f;
            r.absorbed = u72C[2];
            r.maxDent.x = u72C[3] * 0.01f;
            r.maxDent.y = u72C[4] * 0.01f;
            r.maxDent.z = u72C[5] * 0.01f;
            continue;
        case 3:
            r.fullImpulse = u72C[0xD] * KMH_TO_MS * m_currentMass;
            r.minImpulse = u72C[0xE] * KMH_TO_MS * m_currentMass;
            r.fullHealth = u72C[0xF];
            r.absorbed = u72C[0x10];
            r.maxDent.x = u72C[0x11] * 0.01f;
            r.maxDent.y = u72C[0x12] * 0.01f;
            r.maxDent.z = u72C[0x13] * 0.01f;
            r.crackHealth = u72C[0x14];
            continue;
        case 5:
            r.fullImpulse = u7A0[0] * KMH_TO_MS * m_currentMass;
            r.minImpulse = u7A0[1] * KMH_TO_MS * m_currentMass;
            r.fullHealth = u7A0[2];
            r.absorbed = u7A0[4];
            r.maxDent.x = u7A0[5] * 0.01f;
            r.maxDent.y = u7A0[6] * 0.01f;
            r.maxDent.z = u7A0[7] * 0.01f;
            r.crackHealth = u7A0[3];
            continue;
        case 2: g = &u72C[6]; break;
        case 4: g = &u7A0[0xF]; break;
        case 6: g = &u7A0[0x16]; break;
        case 7: g = &u7A0[8]; break;
        case 8: g = &u7A0[0x1D]; break;
        case 9: g = &u7A0[0x24]; break;
        case 10: g = &u7A0[0x2B]; break;
        default: continue;
        }
        r.fullImpulse = g[0] * KMH_TO_MS * m_currentMass;
        r.minImpulse = g[1] * KMH_TO_MS * m_currentMass;
        r.fullHealth = g[2];
        r.absorbed = g[3];
        r.maxDent.x = g[4] * 0.01f;
        r.maxDent.y = g[5] * 0.01f;
        r.maxDent.z = g[6] * 0.01f;
    }

    m_params = p;
#ifndef HD2_DEDICATED   // no sounds on the server
    if (flags & VEHICLE_PARAMS_LOAD_SOUNDS)
        LoadSounds(m_params);
#endif
    return true;
}

bool C_Vehicle::GetParams(S_VehicleParams *params) const                       // 0x004f2000
{
    if (!params)
        return false;
    *params = m_params;
    return true;
}
