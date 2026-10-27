"""Mutations of src/game/vehicle_damage.cpp for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
K = os.path.join(ROOT, r"src\game\vehicle_damage.cpp")

MUTATIONS = [
    # Limit / OtherSide
    (K, "limit sign", "return value > 0.0f ? limit : -limit;", "return limit;"),
    (K, "limit by value", "if (!(limit < std::fabs(value)))", "if (!(limit < value))"),
    (K, "other side", "return lamp.frame->GetPos().x < 0.0f ? VEHICLE_PROJECTOR_RIGHT : VEHICLE_PROJECTOR_LEFT;",
     "return lamp.frame->GetPos().x > 0.0f ? VEHICLE_PROJECTOR_RIGHT : VEHICLE_PROJECTOR_LEFT;"),
    # the lamps
    (K, "wear ignores no-wear", "const bool wear = (m_unknownF6C & VEHICLE_SIM_IMPACT_DAMAGE) && !(flags & DEFORM_NO_WEAR);",
     "const bool wear = (m_unknownF6C & VEHICLE_SIM_IMPACT_DAMAGE) != 0;"),
    (K, "lamp threshold", "if (wear && ((flags & DEFORM_ANY_STRENGTH) || m_lampMinImpulse < strength)) {", "if (wear) {"),
    (K, "lamp absorbed", "m_lampAbsorbed * 0.01f * strength", "m_lampAbsorbed * 0.02f * strength"),
    (K, "lamp radius", "if (!((dz * dz + dy * dy) + dx * dx < radius * radius))", "if (!((dz * dz + dy * dy) + dx * dx < radius))"),
    (K, "lamp break level", "            if (lamp.health < m_lampBreakHealth)\n                lamp.flags |= VEHICLE_LAMP_BROKEN;\n            if (lamp.health < 0.0f)",
     "            if (lamp.health < 0.0f)\n                lamp.flags |= VEHICLE_LAMP_BROKEN;\n            if (lamp.health < 0.0f)"),
    (K, "shared side only broken", "            if (lamp.flags & VEHICLE_LAMP_SHARED)\n                BreakProjectorSide(lamp.projector, OtherSide(lamp));\n        }\n    }\n\n    // The parts",
     "            if ((lamp.flags & VEHICLE_LAMP_SHARED) && (lamp.flags & VEHICLE_LAMP_BROKEN))\n                BreakProjectorSide(lamp.projector, OtherSide(lamp));\n        }\n    }\n\n    // The parts"),
    # the parts: the dent
    (K, "part threshold", "if (!(flags & DEFORM_ANY_STRENGTH) && !(part.minImpulse < strength))",
     "if (!(flags & DEFORM_ANY_STRENGTH) && !(part.minImpulse < strength * 3.0f))"),
    (K, "part absorbed", "const float taken = strength - part.absorbed * 0.01f * strength;",
     "const float taken = strength - part.absorbed * 0.02f * strength;"),
    (K, "share unclamped", "        if (share > 1.0f)\n            share = 1.0f;\n", ""),
    (K, "dent with translation", "        inverse.m[3][0] = inverse.m[3][1] = inverse.m[3][2] = 0.0f;\n        S_vector dent = XYZ(direction) * inverse;",
     "        S_vector dent = XYZ(direction) * inverse;"),
    (K, "dent x unlimited", "        dent.x = Limit(dent.x, part.maxDent.x);\n", ""),
    (K, "vertex radius", "if (!((dx * dx + dz * dz) + dy * dy < radius2))", "if (!((dx * dx + dz * dz) + dy * dy < radius))"),
    (K, "random factor", "* 0.1f * 0.3f + 0.7f;", "* 0.1f * 0.3f + 0.6f;"),
    (K, "offset from current", "const float x = Limit(moved[0] - from[0], part.maxDent.x);",
     "const float x = Limit(moved[0] - to[0], part.maxDent.x);"),
    (K, "y limit by x", "const float y = Limit(moved[1] - from[1], part.maxDent.y);",
     "const float y = Limit(moved[1] - from[1], part.maxDent.x);"),
    (K, "keep each vertex", "                if (!hit) {\n                    if (!kept)\n                        keep();",
     "                if (true) {\n                    if (!kept)\n                        keep();"),
    (K, "no level drop", "                    kept.mesh->SetLevel(nullptr, level);\n", "                    ;\n"),
    (K, "level 1 keeps again", "dentLevel(src1, dst1, to1->NumVertices(), hit1, hit0);",
     "dentLevel(src1, dst1, to1->NumVertices(), hit1, false);"),
    (K, "level 1 from itself", "I3D_mesh_level *from1 = !part.frames && !fresh ? m_meshOriginals[original].mesh->GetLevel(1)",
     "I3D_mesh_level *from1 = false ? m_meshOriginals[original].mesh->GetLevel(1)"),
    (K, "no level 1", "        if (levels > 1) {", "        if (levels > 2) {"),
    (K, "original not marked", "                m_meshOriginals.push_back(kept);\n                part.state |= VEHICLE_PART_DEFORMED;",
     "                m_meshOriginals.push_back(kept);"),
    (K, "refs not counted", "                ++m_meshOriginals[original].refs;\n", ""),
    # (The originals' lookup is FindMeshOriginal now: mutate_state.py's "original by name".)
    (K, "group dented", "                if (part.frames)\n                    continue;\n", ""),
    (K, "hit not dented", "hitCallback(m_callbackContext, part.frame, part.type, &point, &impulse, true);",
     "hitCallback(m_callbackContext, part.frame, part.type, &point, &impulse, false);"),
    (K, "impulse axis", "const S_vector impulse(strength * direction.x, strength * direction.y, strength * direction.z);",
     "const S_vector impulse(strength * direction.x, strength * direction.x, strength * direction.z);"),
    (K, "bumper passes all", "        if (part.type == VEHICLE_PART_BUMPER)\n            strength = taken;\n", ""),
    (K, "dented bit", "        m_unknown58C |= 1;\n", ""),
    (K, "level 1 not refreshed", "            if (hit1)\n                part.mesh->GetLevel(1)->Unknown2C();\n", ""),
    # the parts: wear
    (K, "health every hit", "        if (!(part.state & VEHICLE_PART_STATE_2)) {", "        if (true) {"),
    (K, "health from strength", "part.health = part.health - taken / part.fullImpulse * part.fullHealth;",
     "part.health = part.health - strength / part.fullImpulse * part.fullHealth;"),
    (K, "health unclamped", "            if (part.health < 0.0f)\n                part.health = 0.0f;\n            ShowPartDamage",
     "            ShowPartDamage"),
    (K, "damage not shown", "            ShowPartDamage(static_cast<uint32_t>(index), push, true);\n", ""),
    (K, "no glass crack", "        if (part.type == VEHICLE_PART_GLASS)\n            CrackGlass(static_cast<uint32_t>(index), XYZ(direction));\n", ""),
    (K, "gone below zero", "        if (part.health <= 0.0f) {\n            // Gone", "        if (part.health < 0.0f) {\n            // Gone"),
    (K, "part off no push", "callback(m_callbackContext, static_cast<int>(index), &velocity, push);\n            for",
     "callback(m_callbackContext, static_cast<int>(index), &velocity, nullptr);\n            for"),
    (K, "part off velocity", "const S_vector velocity = Carried(m_velocity, m_unknown210, m_unknown1B0, part.frame->GetWorldPos());\n            // Fix",
     "const S_vector velocity = XYZ(m_velocity);\n            // Fix"),
    (K, "gone lamps kept", "                lamp.health = 0.0f;\n                lamp.flags |= VEHICLE_LAMP_BROKEN;\n                if (lamp.flags & VEHICLE_LAMP_SHARED)\n                    BreakProjectorSide(lamp.projector, OtherSide(lamp));\n            }\n        } else",
     "                lamp.health = 0.0f;\n                if (lamp.flags & VEHICLE_LAMP_SHARED)\n                    BreakProjectorSide(lamp.projector, OtherSide(lamp));\n            }\n        } else"),
    (K, "light lamps' health", "                lamp.health = part.health;\n", ""),
    (K, "light lamps break", "                if (lamp.health < m_lampBreakHealth) {\n                    lamp.flags |= VEHICLE_LAMP_BROKEN;",
     "                if (true) {\n                    lamp.flags |= VEHICLE_LAMP_BROKEN;"),
    # the wheels
    (K, "wheel threshold", "if ((w->flags & (WHEEL_DETACHED | WHEEL_DUMMY)) || !(strength > w->minImpulse))",
     "if ((w->flags & (WHEEL_DETACHED | WHEEL_DUMMY)))"),
    (K, "wheel reach", "if (!((dz * dz + dy * dy) + dx * dx < 1.0f))", "if (!((dz * dz + dy * dy) + dx * dx < 4.0f))"),
    (K, "wheel every hit", "        if (w->flags & WHEEL_HIT)\n            continue;\n", ""),
    (K, "wheel absorbed", "(strength - w->absorbed * 0.01f * strength)", "(strength - w->absorbed * 0.02f * strength)"),
    (K, "wheel hit bit", "        m_unknown58C |= 2;\n        if (w->flags & WHEEL_HIT)", "        if (w->flags & WHEEL_HIT)"),
    (K, "wheel off push", "callback(m_callbackContext, i, &velocity, nullptr);", "callback(m_callbackContext, i, &velocity, &velocity);"),
    (K, "wheel off velocity", "const S_vector velocity = Carried(m_velocity, m_unknown210, m_unknown1B0, XYZ(w->worldPos));",
     "const S_vector velocity = Carried(m_velocity, m_unknown210, m_unknown1B0, XYZ(w->modelPos));"),
    # ShowPartDamage
    (K, "door windows kept", "if (index < m_parts.size() && m_parts[index].type == VEHICLE_PART_DOOR && m_parts[index].health <= 0.0f) {",
     "if (index < m_parts.size() && m_parts[index].type == VEHICLE_PART_DOOR && m_parts[index].health < 0.0f) {"),
    (K, "door window state", "            glass.health = 0.0f;\n            glass.state |= VEHICLE_PART_STATE_2;\n            CrackGlass",
     "            glass.health = 0.0f;\n            CrackGlass"),
    (K, "group stage count", "const float inverse = 1.0f / (static_cast<float>(count) + 1.0f);",
     "const float inverse = 1.0f / static_cast<float>(count);"),
    (K, "group shown index", "    const int shown = left - 1;", "    const int shown = left > 1 ? left - 2 : 0;"),
    (K, "group body hidden", "            visual->SetOn(part.health > 0.0f || part.type == VEHICLE_PART_BODY);",
     "            visual->SetOn(part.health > 0.0f);"),
    (K, "group not placed", "            visual->SetPosInline(part.frame->GetPos());\n            visual->Update();\n            if (seat >= 0)",
     "            visual->Update();\n            if (seat >= 0)"),
    (K, "door frame kept", "                m_seats[seat].door->frame = visual;     // the door swings the visual shown\n",
     "                ;\n"),
    (K, "main shown", "    part.frame->SetOn(false);\n\n    // Its lamps", "\n    // Its lamps"),
    (K, "attached not gone", "        other.health = 0.0f;\n        other.state |= VEHICLE_PART_STATE_2;\n        if (other.type",
     "        other.state |= VEHICLE_PART_STATE_2;\n        if (other.type"),
    (K, "attached ignore detach", "        } else if (detach) {", "        } else if (true) {"),
    # CrackGlass
    (K, "crack never", "if (part.health < part.crackHealth && !(part.state & VEHICLE_PART_GLASS_BROKEN)) {",
     "if (part.health < 0.0f && !(part.state & VEHICLE_PART_GLASS_BROKEN)) {"),
    (K, "crack no ref", "            shown->AddRef();\n", ""),
    (K, "crack not marked", "            part.state |= VEHICLE_PART_GLASS_BROKEN;\n", ""),
    (K, "shatter group swapped", "const char *sound = m_unknown2298 ? m_params.sounds1_9E4[8] : m_params.sounds2_E80[8];",
     "const char *sound = m_unknown2298 ? m_params.sounds2_E80[8] : m_params.sounds1_9E4[8];"),
    (K, "shatter empty name", "        if (!*sound)\n            sound = nullptr;\n", ""),
    (K, "shatter not marked", "        part.state |= VEHICLE_PART_STATE_1;\n        const char *sound", "        const char *sound"),
    # BreakProjectorSide
    (K, "projector textures swapped", "        p.projector->SetTexture(p.textures[2]);\n    else if (p.state & VEHICLE_PROJECTOR_LEFT)\n        p.projector->SetTexture(p.textures[1]);",
     "        p.projector->SetTexture(p.textures[1]);\n    else if (p.state & VEHICLE_PROJECTOR_LEFT)\n        p.projector->SetTexture(p.textures[2]);"),
    (K, "projector state kept", "    p.state &= keep;\n", ""),
]

if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
