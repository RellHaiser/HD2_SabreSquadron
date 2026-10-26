// C_Vehicle: the particle effects (original 0x00506f00 UpdateEngineSmoke, 0x00508350
// UpdateWheelEffects, 0x00508ee0 UpdateExhausts, 0x00509540 UpdateContactEffects; the emitters
// 0x004ece20 StartEffect, 0x004ecfc0 StopEffect). The emitters are the pool's
// (game/car_effects.cpp).
#include "game/vehicle.h"

#include <cstdlib>
#include <cstring>

#include "common/color.h"                               // RgbToHsv, HsvToRgb
#include "game/car_effects.h"
#include "game/game.h"                                  // GetGame
#include "game/pending.h"                               // g_carScene, g_carSurfaces
#include "ls3d/i3d.h"

namespace {

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
uint32_t Bits(const void *pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

// The velocity the body gives a world point as these take it: `velocity` less `turn` x the arm
// from the centre of mass.
S_vector Carried(const S_vector4 &velocity, const S_vector &turn, const S_vector4 &centre, const S_vector &point)
{
    const float x = point.x - centre.x, y = point.y - centre.y, z = point.z - centre.z;
    const float tx = z * turn.y - y * turn.z;
    const float ty = x * turn.z - z * turn.x;
    const float tz = y * turn.x - x * turn.y;
    return S_vector(velocity.x - tx, velocity.y - ty, velocity.z - tz);
}

// A contact's velocity along the ground: n x (v x n).
S_vector AlongGround(const S_vector &v, const S_vector &n)
{
    const float x = n.z * v.y - n.y * v.z;
    const float y = n.x * v.z - n.z * v.x;
    const float z = n.y * v.x - n.x * v.y;
    return S_vector(z * n.y - y * n.z, x * n.z - z * n.x, y * n.x - x * n.y);
}

// A taken emitter: in its sector, its effect's colours with their value (HSV) x `light`, its
// velocities its effect's plus `carried` and its effect's second plus `share` x `carried`, placed
// at `pos` and updated. Fix: the colours' fourth (alpha) float, which the original leaves unset,
// is the effect's.
void RunEmitter(const S_CarEmitter &emitter, float light, const S_vector &carried, float share, const S_vector &pos)
{
    I3D_particle_visual *visual = emitter.visual;
    g_carScene->UpdateFrameSector(visual, visual->GetWorldPos());
    const S_CarEffectDef &def = g_carEffects[emitter.effect];
    for (int i = 0; i < 2; ++i) {
        const float *color = i == 0 ? def.color1 : def.color2;
        float hsv[3], rgb[4];
        RgbToHsv(color, hsv);
        hsv[2] = hsv[2] * light;
        HsvToRgb(hsv, rgb);
        rgb[3] = color[3];
        visual->SetRenderState(i == 0 ? 0x1D : 0x1E, Bits(rgb));
    }
    visual->SetRenderState(0xD, Bits(carried.x + emitter.velocity.x));
    visual->SetRenderState(0xE, Bits(carried.y + emitter.velocity.y));
    visual->SetRenderState(0xF, Bits(carried.z + emitter.velocity.z));
    visual->SetRenderState(0x10, Bits(carried.x * share + emitter.velocity2.x));
    visual->SetRenderState(0x11, Bits(carried.y * share + emitter.velocity2.y));
    visual->SetRenderState(0x12, Bits(carried.z * share + emitter.velocity2.z));
    visual->SetPosInline(pos);
    visual->Update();
}

}  // namespace

int C_Vehicle::StartEffect(int effect, int slot)                               // 0x004ece20
{
    const int count = static_cast<int>(g_carEmitters.size());
    if (auto reuse = reinterpret_cast<T_VehicleReuseEmitterCallback>(m_callbacks[VEHICLE_CALLBACK_REUSE_EMITTER])) {
        if (slot >= 0 && slot < count) {
            const int reused = reuse(slot, static_cast<uint32_t>(effect));
            if (reused >= 0)
                return reused;
        }
    }
    // From the last: the first free emitter, the first with the effect; a free one with the
    // effect is taken at once.
    int free = -1, same = -1;
    for (int i = count; i-- > 0;) {
        S_CarEmitter &emitter = g_carEmitters[i];
        if (same == -1 && emitter.effect == effect)
            same = i;
        if (emitter.active)
            continue;
        if (free == -1)
            free = i;
        if (emitter.effect == effect) {
            emitter.active = true;
            emitter.visual->SetEmitting(true);
            return i;
        }
    }
    if (free < 0)
        return -1;
    S_CarEmitter &emitter = g_carEmitters[free];
    if (same >= 0) {
        const S_CarEmitter &model = g_carEmitters[same];
        emitter.visual->Duplicate(model.visual);
        emitter.effect = model.effect;
        emitter.velocity = model.velocity;
        emitter.velocity2 = model.velocity2;
        emitter.active = true;
        emitter.visual->SetEmitting(true);
        return free;
    }
    if (auto setup = reinterpret_cast<T_VehicleSetupEmitterCallback>(m_callbacks[VEHICLE_CALLBACK_SETUP_EMITTER])) {
        if (setup(&emitter, static_cast<uint32_t>(effect))) {
            emitter.active = true;
            emitter.visual->SetEmitting(true);
            return free;
        }
    }
    return -1;
}

bool C_Vehicle::StopEffect(int slot)                                           // 0x004ecfc0
{
    if (slot < 0 || slot >= static_cast<int>(g_carEmitters.size()))
        return false;
    g_carEmitters[slot].stopping = true;
    return true;
}

bool C_Vehicle::UpdateEngineSmoke()                                            // 0x00506f00
{
    if (!(m_unknownF6C & VEHICLE_SIM_EFFECTS) || !m_unknown2292 || !(m_unknown610 < m_unknown624) ||
        !(m_speed < m_unknown62C)) {
        if (m_unknown638 >= 0) {
            StopEffect(m_unknown638);
            m_unknown638 = -1;
        }
        return true;
    }
    int effect = m_unknown630;
    // Quirk kept: the second smoke's effect is checked against the number of emitters.
    if (m_unknown610 < m_unknown628 && m_unknown634 != 0 && m_unknown634 < static_cast<int>(g_carEmitters.size()))
        effect = m_unknown634;
    if (m_unknown638 < 0)
        m_unknown638 = StartEffect(effect, m_unknown638);
    // Quirk kept: the slot's number is compared with the effect (the other effects compare the
    // slot's effect), so the smoke starts again every step (the car's callback switches the same
    // emitter in place).
    if (m_unknown638 >= 0 && m_unknown638 != effect) {
        StopEffect(m_unknown638);
        m_unknown638 = StartEffect(effect, m_unknown638);
    }
    if (m_unknown638 >= 0) {
        const S_vector pos = XYZ(m_smokePos) * m_modelMatrix;
        RunEmitter(g_carEmitters[m_unknown638], m_unknown548, Carried(m_velocity, m_unknown210, m_unknown1B0, pos),
                   0.1f, pos);
    }
    return false;
}

void C_Vehicle::UpdateWheelEffects()                                           // 0x00508350
{
    if (GetGame()->GetCampaignMissionNumber() == 8)       // (none in the campaign's mission 8)
        return;
    if (!(m_unknownF6C & VEHICLE_SIM_EFFECTS) || !m_unknown2292) {
        for (int i = m_wheelCount; i-- > 0;) {
            S_VehicleWheel *w = m_wheels[i];
            if (w && w->effect >= 0) {                  // (Fix: a missing wheel is skipped)
                StopEffect(w->effect);
                w->effect = -1;
            }
        }
        return;
    }
    for (int i = m_wheelCount; i-- > 0;) {
        S_VehicleWheel *w = m_wheels[i];
        if (!w || (w->flags & WHEEL_DUMMY))             // (Fix: a missing wheel is skipped)
            continue;
        // Its surface's skid effect when spinning (or locked, or flag 0x1000000, sliding over 2
        // m/s), its dust over 3 m/s.
        const uint32_t flags = w->flags;
        int effect = 0;
        bool skid = false;
        if (!(flags & WHEEL_DETACHED) && (flags & WHEEL_ON_GROUND) && (flags & WHEEL_EFFECTS)) {
            const S_vector &v = w->unknown0A0;
            const S_CarSurface &surface = g_carSurfaces[g_carSurfaceOfMaterial[w->surface]];
            if ((flags & WHEEL_SPINNING) ||
                ((flags & (WHEEL_LOCKED | WHEEL_FLAG_1000000)) && (v.x * v.x + v.y * v.y) + v.z * v.z > 4.0f)) {
                skid = true;
                effect = surface.skidEffect;
            } else if (m_speed > 3.0f) {
                effect = surface.dustEffect;
            }
        }
        if (effect > 0) {
            if (w->effect < 0)
                w->effect = StartEffect(effect, w->effect);
            if (w->effect >= 0 && g_carEmitters[w->effect].effect != effect) {
                StopEffect(w->effect);
                w->effect = StartEffect(effect, w->effect);
            }
            if (w->effect >= 0) {
                // The contact's velocity along the ground (0.65 of it rolling).
                S_vector carried = AlongGround(w->unknown0A0, w->unknown080);
                if (!skid)
                    carried = S_vector(carried.x * 0.65f, carried.y * 0.65f, carried.z * 0.65f);
                RunEmitter(g_carEmitters[w->effect], m_unknown548, carried, skid ? 0.01f : 0.001f, XYZ(w->unknown050));
                continue;
            }
        }
        if (w->effect >= 0) {
            StopEffect(w->effect);
            w->effect = -1;
        }
    }
}

void C_Vehicle::UpdateExhausts()                                               // 0x00508ee0
{
    if (!(m_unknownF6C & VEHICLE_SIM_EFFECTS) || !m_unknown2292 || !m_engineOn || !(m_speed < m_unknown758) ||
        m_unknown754 <= 0) {
        for (size_t i = m_exhausts.size(); i-- > 0;) {
            if (m_exhausts[i].effect >= 0) {
                StopEffect(m_exhausts[i].effect);
                m_exhausts[i].effect = -1;
            }
        }
        return;
    }
    // Blown out along the exhaust (rotated with the body) at +0x75C..+0x760 by the revs.
    const float blow = (m_unknown760 - m_unknown75C) * (m_engineRevs / m_revLimit) + m_unknown75C;
    for (size_t i = m_exhausts.size(); i-- > 0;) {
        S_VehicleExhaust &exhaust = m_exhausts[i];
        if (exhaust.effect < 0)
            exhaust.effect = StartEffect(m_unknown754, exhaust.effect);
        if (exhaust.effect < 0)
            continue;
        const S_vector dir = XYZ(exhaust.dir) * m_unknown310;
        const S_vector pos = XYZ(exhaust.pos) * m_modelMatrix;
        const S_vector body = Carried(m_velocity, m_unknown210, m_unknown1B0, pos);
        const S_vector carried(body.x + dir.x * blow, body.y + dir.y * blow, body.z + dir.z * blow);
        RunEmitter(g_carEmitters[exhaust.effect], m_unknown548, carried, 0.1f, pos);
    }
}

void C_Vehicle::UpdateContactEffects()                                         // 0x00509540
{
    if (!(m_unknownF6C & VEHICLE_SIM_EFFECTS) || !m_unknown2292) {
        for (int k = 3; k-- > 0;) {
            if (m_bodyContactEffects[k] >= 0) {
                StopEffect(m_bodyContactEffects[k]);
                m_bodyContactEffects[k] = -1;
            }
        }
        return;
    }
    for (int k = 3; k-- > 0;) {
        int &slot = m_bodyContactEffects[k];
        int effect = 0;
        if (k < static_cast<int>(m_bodyContacts.size()))
            effect = std::abs(g_carSurfaces[g_carSurfaceOfMaterial[m_bodyContacts[k].material]].contactEffect);
        if (effect <= 0) {
            if (slot >= 0) {
                StopEffect(slot);
                slot = -1;
            }
            continue;
        }
        if (slot < 0) {
            slot = StartEffect(effect, slot);
            if (slot < 0)
                continue;
        }
        if (g_carEmitters[slot].effect != effect) {
            StopEffect(slot);
            slot = StartEffect(effect, slot);
        }
        if (slot < 0)
            continue;
        // (Every contact's at the car's position.)
        const S_vector pos = XYZ(m_modelPos);
        RunEmitter(g_carEmitters[slot], m_unknown548, Carried(m_velocity, m_unknown210, m_unknown1B0, pos), 0.01f, pos);
    }
}
