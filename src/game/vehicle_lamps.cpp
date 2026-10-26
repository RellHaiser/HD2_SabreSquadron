// C_Vehicle: the lamps (original 0x00501cb0 UpdateLamps, with the lamp and projector vectors'
// at() 0x00509a40 / 0x00509a50 its later cases call).
#include "game/vehicle.h"

#include <cmath>

#include "ls3d/i3d.h"

namespace {

float SquareYZX(const S_vector &v) { return (v.y * v.y + v.z * v.z) + v.x * v.x; }

// I3D_frame::GetWorldDir as the original sums it (0x0041d200, also inlined here): the world z
// axis normalized (a nearly zero one along its first non-zero axis).
S_vector WorldDir(const I3D_frame *frame)
{
    S_vector dir = frame->GetMatrix()(2);
    const float square = SquareYZX(dir);
    if (std::fabs(square - 1.0f) < 1e-8f)
        return dir;
    if (square >= 1e-8f)
        dir *= 1.0f / std::sqrt(square);
    else if (dir.x != 0.0f)
        dir.x = dir.x >= 0.0f ? 1.0f : -1.0f;
    else if (dir.z != 0.0f)
        dir.z = dir.z >= 0.0f ? 1.0f : -1.0f;
    else
        dir.y = dir.y >= 0.0f ? 1.0f : -1.0f;
    return dir;
}

}  // namespace

// A lamp's projected light or spot light (`object`) into the scene (m_unknown544) at the lamp,
// or at its PROJECTOR (marked lit), facing along it; `dir` overrides the projector's facing.
void C_Vehicle::PlaceLampObject(I3D_frame *object, const S_VehicleLamp &lamp, const S_vector *sharedDir)
{
    if (object->GetParent() != m_unknown544)
        object->LinkTo(m_unknown544, 0);
    const I3D_frame *at = lamp.frame;
    const S_vector *dir = nullptr;
    if (lamp.flags & VEHICLE_LAMP_SHARED) {
        S_VehicleProjector &projector = m_projectors[lamp.projectorIndex];
        projector.state |= VEHICLE_PROJECTOR_LIT;
        at = projector.frame;
        dir = sharedDir;
    }
    object->SetPosInline(at->GetWorldPos());
    object->SetWorldDir(dir ? *dir : WorldDir(at), 0.0f);
}

// Switches `object` (a lamp's projected or spot light) off unless the lamp's PROJECTOR is lit.
void C_Vehicle::DarkenLampObject(I3D_frame *object, const S_VehicleLamp &lamp)
{
    if ((lamp.flags & VEHICLE_LAMP_SHARED) && (m_projectors[lamp.projectorIndex].state & VEHICLE_PROJECTOR_LIT))
        return;
    // Fix: a lamp without a spot light (the driver made none) is skipped (the original calls it).
    if (object)
        object->SetOn(false);
}

void C_Vehicle::UpdateLamps(float timeDelta)                                    // 0x00501cb0
{
    if (!(m_unknownF6C & VEHICLE_SIM_LAMPS) && !(m_unknownF70 & VEHICLE_SIM_LAMPS))
        return;

    // The flashers (LI) alternate every +0x5C8 s; switched off, both go dark.
    m_unknown5CC += timeDelta;
    if (m_unknown5B0 & VEHICLE_LIGHT_FLASHING) {
        if (m_unknown5CC > m_unknown5C8) {
            m_unknown5CC = 0.0f;
            m_unknown5B0 ^= VEHICLE_LIGHT_FLASH_PHASE;
        }
        if (m_unknown5B0 & VEHICLE_LIGHT_FLASH_PHASE)
            m_unknown5B0 = (m_unknown5B0 & ~VEHICLE_LIGHT_FLASH_RIGHT) | VEHICLE_LIGHT_FLASH_LEFT;
        else
            m_unknown5B0 = (m_unknown5B0 & ~VEHICLE_LIGHT_FLASH_LEFT) | VEHICLE_LIGHT_FLASH_RIGHT;
    } else if (m_unknown5B4 & VEHICLE_LIGHT_FLASHING) {
        m_unknown5B0 &= ~(VEHICLE_LIGHT_FLASH_LEFT | VEHICLE_LIGHT_FLASH_RIGHT);
    }
    // The indicators' blink: on for +0x5B8 s, off for +0x5BC s.
    m_unknown5C0 += timeDelta;
    if (m_unknown5B0 & VEHICLE_LIGHT_BLINK) {
        if (m_unknown5C0 >= m_unknown5B8) {
            m_unknown5C0 = 0.0f;
            m_unknown5B0 &= ~VEHICLE_LIGHT_BLINK;
        }
    } else if (m_unknown5C0 >= m_unknown5BC) {
        m_unknown5C0 = 0.0f;
        m_unknown5B0 |= VEHICLE_LIGHT_BLINK;
    }
    // The lights that follow the car.
    if (m_unknown5B0 & VEHICLE_LIGHT_AUTO_REVERSE) {
        if (m_gear < 0)
            m_unknown5B0 |= VEHICLE_LIGHT_REVERSE;
        else
            m_unknown5B0 &= ~VEHICLE_LIGHT_REVERSE;
    }
    if (m_unknown5B0 & VEHICLE_LIGHT_AUTO_BRAKE) {
        if (m_brake > 0.0f)
            m_unknown5B0 |= VEHICLE_LIGHT_BRAKE;
        else
            m_unknown5B0 &= ~VEHICLE_LIGHT_BRAKE;
    }
    if (m_unknown5B0 & VEHICLE_LIGHT_AUTO_INDICATORS) {
        if (!(m_unknown5C4 < std::fabs(m_unknown94C)))
            m_unknown5B0 &= ~(VEHICLE_LIGHT_INDICATOR_LEFT | VEHICLE_LIGHT_INDICATOR_RIGHT);
        else if (m_unknown94C > 0.0f)
            m_unknown5B0 = (m_unknown5B0 & ~VEHICLE_LIGHT_INDICATOR_LEFT) | VEHICLE_LIGHT_INDICATOR_RIGHT;
        else
            m_unknown5B0 = (m_unknown5B0 & ~VEHICLE_LIGHT_INDICATOR_RIGHT) | VEHICLE_LIGHT_INDICATOR_LEFT;
    }

    for (size_t i = m_projectors.size(); i-- > 0;)
        m_projectors[i].state &= ~VEHICLE_PROJECTOR_LIT;

    for (size_t i = m_lamps.size(); i-- > 0;) {
        const S_VehicleLamp &lamp = m_lamps[i];
        const uint32_t lights = m_unknown5B0;
        const bool working = !(lamp.flags & VEHICLE_LAMP_BROKEN) && (m_unknownF6C & VEHICLE_SIM_LAMPS);
        const bool blink = (lights & VEHICLE_LIGHT_BLINK) && working;
        bool lit = false;
        uint32_t beams = 0;
        switch (lamp.kind) {
        case VEHICLE_LAMP_IN_LEFT:
            lit = (lights & VEHICLE_LIGHT_INDICATOR_LEFT) && blink;
            beams = VEHICLE_LIGHT_INDICATOR_BEAMS;
            break;
        case VEHICLE_LAMP_IN_RIGHT:
            lit = (lights & VEHICLE_LIGHT_INDICATOR_RIGHT) && blink;
            beams = VEHICLE_LIGHT_INDICATOR_BEAMS;
            break;
        // The rear lamps: indicators, else brake lights.
        case VEHICLE_LAMP_BI_LEFT:
            lit = (lights & VEHICLE_LIGHT_INDICATOR_LEFT) ? blink : (lights & VEHICLE_LIGHT_BRAKE) && working;
            beams = VEHICLE_LIGHT_REAR_BEAMS;
            break;
        case VEHICLE_LAMP_BI_RIGHT:
            lit = (lights & VEHICLE_LIGHT_INDICATOR_RIGHT) ? blink : (lights & VEHICLE_LIGHT_BRAKE) && working;
            beams = VEHICLE_LIGHT_REAR_BEAMS;
            break;
        case VEHICLE_LAMP_LI_LEFT:
            lit = (lights & VEHICLE_LIGHT_FLASH_LEFT) && working;
            beams = VEHICLE_LIGHT_FLASH_BEAMS;
            break;
        case VEHICLE_LAMP_LI_RIGHT:
            lit = (lights & VEHICLE_LIGHT_FLASH_RIGHT) && working;
            beams = VEHICLE_LIGHT_FLASH_BEAMS;
            break;
        case VEHICLE_LAMP_RE:
            lit = (lights & VEHICLE_LIGHT_HEADLIGHTS) && working;
            beams = VEHICLE_LIGHT_HEADLIGHT_BEAMS;
            break;
        case VEHICLE_LAMP_BR:
            lit = (lights & VEHICLE_LIGHT_BRAKE) && working;
            beams = VEHICLE_LIGHT_BRAKE_BEAMS;
            break;
        case VEHICLE_LAMP_BA:
            lit = (lights & VEHICLE_LIGHT_REVERSE) && working;
            beams = VEHICLE_LIGHT_REVERSE_BEAMS;
            break;
        default:
            break;                                      // (no other kinds are made)
        }

        if (beams && lit) {
            if (lamp.billboard)
                lamp.billboard->SetOn(true);
            if ((m_unknown5B0 & beams) && lamp.projector) {
                if (m_unknownF6C & VEHICLE_SIM_LAMP_PROJECTORS) {
                    PlaceLampObject(lamp.projector, lamp, nullptr);
                    lamp.projector->SetOn(true);
                    lamp.projector->Update();
                } else {
                    DarkenLampObject(lamp.projector, lamp);
                }
                if (m_unknownF6C & VEHICLE_SIM_LAMP_SPOT_LIGHTS) {
                    // Fix: a lamp without a spot light is skipped (the original calls it).
                    if (lamp.light) {
                        // The headlights' shared spot light shines along the car.
                        const S_vector ahead(m_modelAxisZ.x, m_modelAxisZ.y, m_modelAxisZ.z);
                        PlaceLampObject(lamp.light, lamp, lamp.kind == VEHICLE_LAMP_RE ? &ahead : nullptr);
                        lamp.light->SetOn(true);
                        lamp.light->Update();
                    }
                } else if (lamp.kind == VEHICLE_LAMP_RE) {
                    DarkenLampObject(lamp.light, lamp);
                } else {
                    // Quirk kept: the others switch their projected light off here, not the spot.
                    DarkenLampObject(lamp.projector, lamp);
                }
            }
        } else if (beams) {
            if (lamp.billboard)
                lamp.billboard->SetOn(false);
            if (lamp.projector &&
                !((lamp.flags & VEHICLE_LAMP_SHARED) && (m_projectors[lamp.projectorIndex].state & VEHICLE_PROJECTOR_LIT))) {
                lamp.projector->SetOn(false);
                DarkenLampObject(lamp.light, lamp);
            }
        }
        if (lamp.billboard)
            lamp.billboard->Update();
    }
    m_unknown5B4 = m_unknown5B0;
}

void C_Vehicle::SetBeamScale(uint32_t beams, const S_vector &scale)              // 0x004f4e40
{
    // Kind by kind, each kind's lamps from the last. (The original copies a fourth float of the
    // caller's along into the frame's +0xDC.)
    static const struct {
        uint32_t beam;
        uint16_t kinds[2];
    } groups[] = {
        {VEHICLE_LIGHT_INDICATOR_BEAMS, {VEHICLE_LAMP_IN_LEFT, VEHICLE_LAMP_IN_RIGHT}},
        {VEHICLE_LIGHT_FLASH_BEAMS, {VEHICLE_LAMP_LI_LEFT, VEHICLE_LAMP_LI_RIGHT}},
        {VEHICLE_LIGHT_REAR_BEAMS, {VEHICLE_LAMP_BI_LEFT, VEHICLE_LAMP_BI_RIGHT}},
        {VEHICLE_LIGHT_HEADLIGHT_BEAMS, {VEHICLE_LAMP_RE, VEHICLE_LAMP_RE}},
        {VEHICLE_LIGHT_BRAKE_BEAMS, {VEHICLE_LAMP_BR, VEHICLE_LAMP_BR}},
        {VEHICLE_LIGHT_REVERSE_BEAMS, {VEHICLE_LAMP_BA, VEHICLE_LAMP_BA}},
    };
    for (const auto &group : groups) {
        if (!(beams & group.beam))
            continue;
        for (size_t i = m_lamps.size(); i-- > 0;) {
            const S_VehicleLamp &lamp = m_lamps[i];
            if ((lamp.kind == group.kinds[0] || lamp.kind == group.kinds[1]) && lamp.projector)
                lamp.projector->SetScale(scale);
        }
    }
}
