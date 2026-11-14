// C_Vehicle: the damage (original 0x004ff8b0 Deform) and its steps: 0x004fefd0 ShowPartDamage,
// 0x00500da0 CrackGlass, 0x00500cb0 BreakProjectorSide.
#include "game/vehicle.h"

#include <cmath>
#include <cstdlib>

#include "ls3d/i3d.h"

namespace {

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

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

// `value` within -limit..limit: past it, the limit with its sign (0 counting as negative).
float Limit(float value, float limit)
{
    if (!(limit < std::fabs(value)))
        return value;
    return value > 0.0f ? limit : -limit;
}

#ifndef HD2_DEDICATED   // no lamps on the server
// The half of a shared lamp's projector that stays lit when the lamp breaks: the other side's
// (a lamp left of the model's centre, x < 0, leaves the right one).
uint16_t OtherSide(const S_VehicleLamp &lamp)
{
    return lamp.frame->GetPos().x < 0.0f ? VEHICLE_PROJECTOR_RIGHT : VEHICLE_PROJECTOR_LEFT;
}
#endif

}  // namespace

void C_Vehicle::Deform(const S_vector4 &point, const S_vector4 &direction, float radius, float strength,
                       uint8_t flags, const S_vector *push)                    // 0x004ff8b0 (DS 0x004f2be0)
{
    const bool wear = (m_unknownF6C & VEHICLE_SIM_IMPACT_DAMAGE) && !(flags & DEFORM_NO_WEAR);

#ifndef HD2_DEDICATED   // no lamps on the server
    // The lamps within `radius` (but those of a light part, which go with the part) lose health;
    // below m_lampBreakHealth they break.
    if (wear && ((flags & DEFORM_ANY_STRENGTH) || m_lampMinImpulse < strength)) {
        const float damage = (strength - m_lampAbsorbed * 0.01f * strength) / m_lampFullImpulse * m_lampFullHealth;
        for (size_t i = m_lamps.size(); i-- > 0;) {
            S_VehicleLamp &lamp = m_lamps[i];
            if (lamp.flags & VEHICLE_LAMP_BROKEN)
                continue;
            if (!(lamp.part & 0x8000) && m_parts[lamp.part].type == VEHICLE_PART_LIGHT)
                continue;
            const S_vector &pos = lamp.frame->GetWorldPos();
            const float dx = pos.x - point.x, dy = pos.y - point.y, dz = pos.z - point.z;
            if (!((dz * dz + dy * dy) + dx * dx < radius * radius))
                continue;
            lamp.health = lamp.health - damage;
            if (lamp.health < m_lampBreakHealth)
                lamp.flags |= VEHICLE_LAMP_BROKEN;
            if (lamp.health < 0.0f)
                lamp.health = 0.0f;
            // Quirk kept: a shared lamp's half of the projector goes dark with any hit, broken or not.
            if (lamp.flags & VEHICLE_LAMP_SHARED)
                BreakProjectorSide(lamp.projector, OtherSide(lamp));
        }
    }
#endif

    // The parts: each one the impulse passes (minImpulse; DEFORM_ANY_STRENGTH: every one) has its
    // vertices within `radius` of the point moved along the direction (both in the part's space)
    // by its share of the impulse (at most 1) x 0.7..0.97 at random, each at most maxDent from its
    // original, in its first two detail levels. The first dent of a part keeps its mesh as it was
    // (m_meshOriginals, by model file and part name; levels past the second dropped) for
    // RepairParts. A group's visuals are not dented. Then the hit callback; with wear the health.
    const float radius2 = radius * radius;
    for (size_t index = m_parts.size(); index-- > 0;) {
        S_VehiclePart &part = m_parts[index];
        if (part.state & VEHICLE_PART_STATE_1)
            continue;
        if (!(flags & DEFORM_ANY_STRENGTH) && !(part.minImpulse < strength))
            continue;
        const float taken = strength - part.absorbed * 0.01f * strength;
        float share = taken / part.fullImpulse;
        if (share > 1.0f)
            share = 1.0f;

        // The original's mesh, or the part's own (fresh: none kept yet).
        bool fresh = false;
        int original = 0;
        I3D_mesh_level *from0;
        if (!part.frames) {
            original = FindMeshOriginal(part);
            if (original < 0) {
                fresh = true;
                from0 = part.mesh->GetLevel(0);
            } else {
                from0 = m_meshOriginals[original].mesh->GetLevel(0);
            }
        } else {
            from0 = part.mesh->GetLevel(0);
        }
        I3D_mesh_level *to0 = part.mesh->GetLevel(0);
        auto *src0 = static_cast<uint8_t *>(from0->GetVertices(0));
        auto *dst0 = static_cast<uint8_t *>(to0->GetVertices(0));

        // The point and the direction in the part's space (the direction turned only).
        const S_matrix &world = part.frame->GetMatrix();
        S_matrix inverse = world;
        inverse.Inverse(world);
        const S_vector local = XYZ(point) * inverse;
        inverse.m[3][0] = inverse.m[3][1] = inverse.m[3][2] = 0.0f;
        S_vector dent = XYZ(direction) * inverse;
        dent.x = Limit(dent.x, part.maxDent.x);
        dent.y = Limit(dent.y, part.maxDent.y);
        dent.z = Limit(dent.z, part.maxDent.z);

        // The first vertex in reach of a part keeps its original (or counts the part as one of the
        // original's users).
        const auto keep = [&] {
            if (fresh) {
                S_VehicleMeshOriginal kept;
                kept.refs = 1;
                kept.partName = part.name;
                kept.modelFile = m_modelFileName;
                kept.mesh = I3DGetDriver()->CreateMesh();
                kept.mesh->Duplicate(part.mesh);
                for (int level = static_cast<int>(kept.mesh->NumLevels()) - 1; level > 1; --level)
                    kept.mesh->SetLevel(nullptr, level);
                m_meshOriginals.push_back(kept);
                part.state |= VEHICLE_PART_DEFORMED;
            } else if (!part.frames && !(part.state & VEHICLE_PART_DEFORMED)) {
                part.state |= VEHICLE_PART_DEFORMED;
                ++m_meshOriginals[original].refs;
            }
        };
        // One detail level: `hit` set at its first vertex in reach (`kept`: the other level kept it).
        const auto dentLevel = [&](const uint8_t *src, uint8_t *dst, int count, bool &hit, bool kept) {
            for (int i = count; i-- > 0;) {
                auto *to = reinterpret_cast<float *>(dst + i * 0x20);
                const auto *from = reinterpret_cast<const float *>(src + i * 0x20);
                const float dx = to[0] - local.x, dy = to[1] - local.y, dz = to[2] - local.z;
                if (!((dx * dx + dz * dz) + dy * dy < radius2))
                    continue;
                if (!hit) {
                    if (!kept)
                        keep();
                    hit = true;
                }
                if (part.frames)
                    continue;
                const float scale = static_cast<float>(std::rand() % 10) * 0.1f * 0.3f + 0.7f;
                const float moved[3] = {dent.x * scale * share + to[0], scale * dent.y * share + to[1],
                                        scale * dent.z * share + to[2]};
                const float x = Limit(moved[0] - from[0], part.maxDent.x);
                const float y = Limit(moved[1] - from[1], part.maxDent.y);
                const float z = Limit(moved[2] - from[2], part.maxDent.z);
                to[0] = x + from[0];
                to[1] = y + from[1];
                to[2] = z + from[2];
            }
        };
        bool hit0 = false, hit1 = false;
        dentLevel(src0, dst0, to0->NumVertices(), hit0, false);
        from0->UnlockVertices();
        to0->UnlockVertices();

        const int levels = static_cast<int>(!part.frames && !fresh ? m_meshOriginals[original].mesh->NumLevels()
                                                                    : part.mesh->NumLevels());
        if (levels > 1) {
            I3D_mesh_level *from1 = !part.frames && !fresh ? m_meshOriginals[original].mesh->GetLevel(1)
                                                            : part.mesh->GetLevel(1);
            I3D_mesh_level *to1 = part.mesh->GetLevel(1);
            if (from1 && to1) {
                auto *src1 = static_cast<uint8_t *>(from1->GetVertices(0));
                auto *dst1 = static_cast<uint8_t *>(to1->GetVertices(0));
                dentLevel(src1, dst1, to1->NumVertices(), hit1, hit0);
                from1->UnlockVertices();
                to1->UnlockVertices();
            }
        }

        const auto hitCallback = reinterpret_cast<T_VehiclePartHitCallback>(m_callbacks[VEHICLE_CALLBACK_PART_HIT]);
        const S_vector impulse(strength * direction.x, strength * direction.y, strength * direction.z);
        if (!hit0 && !hit1) {
            if (hitCallback)
                hitCallback(m_callbackContext, part.frame, part.type, &point, &impulse, false);
            continue;
        }
        if (hitCallback)
            hitCallback(m_callbackContext, part.frame, part.type, &point, &impulse, true);
        // A bumper takes its absorbed share: the parts after it and the wheels get the rest.
        if (part.type == VEHICLE_PART_BUMPER)
            strength = taken;
        m_unknown58C |= 1;
        if (!part.frames) {
            if (hit0)
                part.mesh->GetLevel(0)->Unknown2C();
            if (hit1)
                part.mesh->GetLevel(1)->Unknown2C();
            part.mesh->ComputeBounds();
            static_cast<I3D_object_visual *>(part.frame)->UnknownAC(0);
            part.frame->Update();
        }
        if (!wear)
            continue;

        // The health: lost once per step (VEHICLE_PART_STATE_2, Update clears it), then shown.
        if (!(part.state & VEHICLE_PART_STATE_2)) {
            part.health = part.health - taken / part.fullImpulse * part.fullHealth;
            if (part.health < 0.0f)
                part.health = 0.0f;
            ShowPartDamage(static_cast<uint32_t>(index), push, true);
        }
        part.state |= VEHICLE_PART_STATE_2;
        m_unknown58C |= 2;
        if (part.type == VEHICLE_PART_GLASS)
            CrackGlass(static_cast<uint32_t>(index), XYZ(direction));
        if (part.health <= 0.0f) {
            // Gone: it comes off, its lamps break.
            const S_vector velocity = Carried(m_velocity, m_unknown210, m_unknown1B0, part.frame->GetWorldPos());
            // Fix: no callback, no call (the original calls through a null one).
            if (auto callback = reinterpret_cast<T_VehiclePartOffCallback>(m_callbacks[VEHICLE_CALLBACK_PART_OFF]))
                callback(m_callbackContext, static_cast<int>(index), &velocity, push);
#ifndef HD2_DEDICATED   // no lamps on the server
            for (size_t i = m_lamps.size(); i-- > 0;) {
                S_VehicleLamp &lamp = m_lamps[i];
                if (lamp.part != index)
                    continue;
                lamp.health = 0.0f;
                lamp.flags |= VEHICLE_LAMP_BROKEN;
                if (lamp.flags & VEHICLE_LAMP_SHARED)
                    BreakProjectorSide(lamp.projector, OtherSide(lamp));
            }
        } else if (part.type == VEHICLE_PART_LIGHT) {
            // A light part's lamps share its health.
            for (size_t i = m_lamps.size(); i-- > 0;) {
                S_VehicleLamp &lamp = m_lamps[i];
                if (lamp.part != index)
                    continue;
                lamp.health = part.health;
                if (lamp.health < m_lampBreakHealth) {
                    lamp.flags |= VEHICLE_LAMP_BROKEN;
                    if (lamp.flags & VEHICLE_LAMP_SHARED)
                        BreakProjectorSide(lamp.projector, OtherSide(lamp));
                }
            }
#endif
        }
    }

    // The wheels within 1 m of the point (in the model's space) lose health once per step
    // (WHEEL_HIT); at 0 the wheel comes off.
    if (!wear)
        return;
    const S_vector local = XYZ(point) * m_invModelMatrix;
    for (int i = m_wheelCount; i-- > 0;) {
        S_VehicleWheel *w = m_wheels[i];
        if (!w)                                         // Fix: the original crashes on a missing wheel
            continue;
        if ((w->flags & (WHEEL_DETACHED | WHEEL_DUMMY)) || !(strength > w->minImpulse))
            continue;
        const float dx = w->modelPos.x - local.x, dy = w->modelPos.y - local.y, dz = w->modelPos.z - local.z;
        if (!((dz * dz + dy * dy) + dx * dx < 1.0f))
            continue;
        m_unknown58C |= 2;
        if (w->flags & WHEEL_HIT)
            continue;
        w->flags |= WHEEL_HIT;
        w->health = w->health - (strength - w->absorbed * 0.01f * strength) / w->fullImpulse * w->fullHealth;
        if (w->health < 0.0f) {
            w->health = 0.0f;
            const S_vector velocity = Carried(m_velocity, m_unknown210, m_unknown1B0, XYZ(w->worldPos));
            // Fix: no callback, no call (the original calls through a null one).
            if (auto callback = reinterpret_cast<T_VehiclePartOffCallback>(m_callbacks[VEHICLE_CALLBACK_WHEEL_OFF]))
                callback(m_callbackContext, i, &velocity, nullptr);
        }
    }
}

void C_Vehicle::ShowPartDamage(uint32_t index, const S_vector *push, bool detach)   // 0x004fefd0 (DS 0x004f2140)
{
    // A door gone breaks its windows (the glass parts on its frame).
    if (index < m_parts.size() && m_parts[index].type == VEHICLE_PART_DOOR && m_parts[index].health <= 0.0f) {
        for (size_t j = m_parts.size(); j-- > 0;) {
            S_VehiclePart &glass = m_parts[j];
            if (glass.type != VEHICLE_PART_GLASS || (glass.state & VEHICLE_PART_STATE_1) ||
                glass.frame->GetParent() != m_parts[index].frame)
                continue;
            glass.health = 0.0f;
            glass.state |= VEHICLE_PART_STATE_2;
            CrackGlass(static_cast<uint32_t>(j),
                       Carried(m_velocity, m_unknown210, m_unknown1B0, glass.frame->GetWorldPos()));
        }
    }

    // A group shows the visual for its health: of its n others, the one at (1 - health share) x
    // (n + 1) from the first, past the whole ones (none while above n / (n + 1) of its health);
    // gone, none (a body's stays). The main visual goes.
    if (index >= m_parts.size())                        // Fix: the original reads past the parts
        return;
    S_VehiclePart &part = m_parts[index];
    if (!part.frames)
        return;
    int seat = -1;
    for (size_t s = m_seats.size(); s-- > 0;) {
        const S_VehicleDoor *door = m_seats[s].door;
        if (door && door->doorPart == index) {
            seat = static_cast<int>(s);
            break;
        }
    }
    const int count = static_cast<int>(part.frames->size());
    const float inverse = 1.0f / (static_cast<float>(count) + 1.0f);
    const int whole = static_cast<int>(part.health / part.fullHealth * (1.0f / inverse));
    const int left = count - whole;
    if (left <= 0)
        return;
    const int shown = left - 1;
    for (int k = count; k-- > 0;) {
        I3D_frame *visual = (*part.frames)[k];
        if (!visual)                                    // Fix: a group's empty slot (null)
            continue;
        if (k == shown) {
            visual->SetOn(part.health > 0.0f || part.type == VEHICLE_PART_BODY);
            visual->SetPosInline(part.frame->GetPos());
            visual->Update();
            if (seat >= 0)
                m_seats[seat].door->frame = visual;     // the door swings the visual shown
        } else {
            visual->SetOn(false);
            visual->Update();
        }
    }
    part.frame->SetOn(false);

#ifndef HD2_DEDICATED   // no lamps on the server
    // Its lamps (and those on its frame) break.
    for (size_t i = m_lamps.size(); i-- > 0;) {
        S_VehicleLamp &lamp = m_lamps[i];
        if ((lamp.part != index && lamp.frame->GetParent() != part.frame) || (lamp.flags & VEHICLE_LAMP_BROKEN))
            continue;
        lamp.health = 0.0f;
        lamp.flags |= VEHICLE_LAMP_BROKEN;
        if (lamp.flags & VEHICLE_LAMP_SHARED)
            BreakProjectorSide(lamp.projector, OtherSide(lamp));
    }
#endif

    // The parts on its frame are gone: a group shows its last visual (a body's or one with health
    // left on), glass cracks, the others come off (`detach`).
    for (size_t j = m_parts.size(); j-- > 0;) {
        S_VehiclePart &other = m_parts[j];
        if ((other.state & VEHICLE_PART_STATE_1) || other.frame->GetParent() != part.frame)
            continue;
        if (other.frames) {
            const size_t visuals = other.frames->size();
            for (size_t k = visuals; k-- > 0;) {
                I3D_frame *visual = (*other.frames)[k];
                if (!visual)                            // Fix: a group's empty slot (null)
                    continue;
                if (k == visuals - 1) {
                    visual->SetOn(other.health > 0.0f || other.type == VEHICLE_PART_BODY);
                    visual->SetPosInline(other.frame->GetPos());
                } else {
                    visual->SetOn(false);
                }
                visual->Update();
            }
            other.frame->SetOn(false);
        }
        other.health = 0.0f;
        other.state |= VEHICLE_PART_STATE_2;
        if (other.type == VEHICLE_PART_GLASS) {
            CrackGlass(static_cast<uint32_t>(j),
                       Carried(m_velocity, m_unknown210, m_unknown1B0, other.frame->GetWorldPos()));
        } else if (detach) {
            const S_vector velocity = Carried(m_velocity, m_unknown210, m_unknown1B0, other.frame->GetWorldPos());
            // Fix: no callback, no call (the original calls through a null one).
            if (auto callback = reinterpret_cast<T_VehiclePartOffCallback>(m_callbacks[VEHICLE_CALLBACK_PART_OFF]))
                callback(m_callbackContext, static_cast<int>(j), &velocity, push);
        }
    }
}

void C_Vehicle::CrackGlass(uint32_t index, const S_vector &velocity)          // 0x00500da0 (DS 0x004f4170)
{
#ifdef HD2_DEDICATED   // the server's is empty
    return;
#endif
    S_VehiclePart &part = m_parts[index];
    // Below its crack health the glass shows the cracked texture (kept in glassTexture, which then
    // keeps the intact one).
    if (part.health < part.crackHealth && !(part.state & VEHICLE_PART_GLASS_BROKEN)) {
        I3D_material *material = part.mesh->GetLevel(0)->GetFaceGroup(0)->material;
        I3D_texture *shown = material->GetTexture();
        if (shown && part.glassTexture) {
            shown->AddRef();
            material->SetTexture(part.glassTexture);
            part.glassTexture->Release();
            part.glassTexture = shown;
            part.state |= VEHICLE_PART_GLASS_BROKEN;
        }
    }
    // At no health it shatters: the glass callback with the sound group's ninth name (null when
    // empty).
    if (part.health <= 0.0f) {
        part.state |= VEHICLE_PART_STATE_1;
        const char *sound = m_unknown2298 ? m_params.sounds1_9E4[8] : m_params.sounds2_E80[8];
        if (!*sound)
            sound = nullptr;
        // Fix: no callback, no call (the original calls through a null one).
        if (auto callback = reinterpret_cast<T_VehicleGlassCallback>(m_callbacks[VEHICLE_CALLBACK_GLASS_BROKEN]))
            callback(m_callbackContext, static_cast<int>(index), &velocity, sound);
    }
}

#ifndef HD2_DEDICATED   // no lamps on the server
bool C_Vehicle::BreakProjectorSide(const I3D_projector_visual *projector, uint16_t keep)   // 0x00500cb0
{
    int i = static_cast<int>(m_projectors.size());
    while (--i >= 0 && m_projectors[i].projector != projector) {
    }
    if (i < 0)
        return false;
    S_VehicleProjector &p = m_projectors[i];
    p.state &= keep;
    if (p.state & VEHICLE_PROJECTOR_RIGHT)
        p.projector->SetTexture(p.textures[2]);
    else if (p.state & VEHICLE_PROJECTOR_LEFT)
        p.projector->SetTexture(p.textures[1]);
    p.projector->Update();
    if (p.light) {                                      // Fix: the original crashes without a spot light
        p.light->SetPower(1.0f);
        p.light->Update();
    }
    return false;
}
#endif
