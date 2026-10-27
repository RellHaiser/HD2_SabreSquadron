"""Mutations of src/game/vehicle_effects.cpp for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
V = os.path.join(ROOT, r"src\game\vehicle_effects.cpp")
C = os.path.join(ROOT, r"src\game\car_effects.cpp")

MUTATIONS = [
    (V, "carried sign", "return S_vector(velocity.x - tx, velocity.y - ty, velocity.z - tz);",
     "return S_vector(velocity.x + tx, velocity.y + ty, velocity.z + tz);"),
    (V, "along ground x", "return S_vector(z * n.y - y * n.z,", "return S_vector(y * n.z - z * n.y,"),
    (V, "colour not lit", "hsv[2] = hsv[2] * light;", "hsv[2] = hsv[2];"),
    (V, "share dropped", "Bits(carried.x * share + emitter.velocity2.x)", "Bits(carried.x + emitter.velocity2.x)"),
    (V, "not placed", "    visual->SetPosInline(pos);\n", ""),
    (V, "reuse ignored", "            if (reused >= 0)\n                return reused;", "            (void)reused;"),
    (V, "free same effect skipped", "        if (emitter.effect == effect) {\n            emitter.active = true;",
     "        if (false) {\n            emitter.active = true;"),
    (V, "stop not marked", "    g_carEmitters[slot].stopping = true;", "    (void)0;"),
    (V, "smoke second threshold", "if (m_unknown610 < m_unknown628 &&", "if (m_unknown610 < m_unknown624 &&"),
    (V, "smoke quirk fixed", "if (m_unknown638 >= 0 && m_unknown638 != effect) {",
     "if (m_unknown638 >= 0 && g_carEmitters[m_unknown638].effect != effect) {"),
    (V, "skid as dust", "                effect = surface.skidEffect;", "                effect = surface.dustEffect;"),
    (V, "rolling share", "carried = S_vector(carried.x * 0.65f, carried.y * 0.65f, carried.z * 0.65f);",
     "carried = S_vector(carried.x * 0.6f, carried.y * 0.6f, carried.z * 0.6f);"),
    (V, "dust speed", "} else if (m_speed > 3.0f) {", "} else if (m_speed > 1.0f) {"),
    (V, "mission 9", "GetCampaignMissionNumber() == 8)", "GetCampaignMissionNumber() == 9)"),
    (V, "exhaust blow", "(m_engineRevs / m_revLimit) + m_unknown75C;", "(m_engineRevs / m_revLimit) + m_unknown760;"),
    (V, "exhaust speed", "!(m_speed < m_unknown758) ||", "!(m_speed < m_unknown758 * 3.0f) ||"),
    (V, "contact sign", "effect = std::abs(g_carSurfaces[g_carSurfaceOfMaterial[m_bodyContacts[k].material]].contactEffect);",
     "effect = g_carSurfaces[g_carSurfaceOfMaterial[m_bodyContacts[k].material]].contactEffect;"),
    (V, "contact restart dropped", "        if (g_carEmitters[slot].effect != effect) {", "        if (false) {"),
    (V, "wheel restart dropped", "            if (w->effect >= 0 && g_carEmitters[w->effect].effect != effect) {",
     "            if (false) {"),
    (C, "property 0x22 dropped", "    if (all)\n        visual->SetRenderState(0x22", "    if (false)\n        visual->SetRenderState(0x22"),
    (C, "reuse never", "    if (std::strcmp(emitter.visual->GetTextureFileName(), def.texture) != 0)\n        return -1;",
     "    return -1;"),
    (C, "keys short", "    for (uint32_t i = g_carEffectCount; i-- > 0;) {", "    for (uint32_t i = g_carEffectCount; i-- > 2;) {"),
    (C, "select rect partial", " && def.rect[3] == rect[3])", ")"),
    (C, "freed at once", "        if (emitter.visual->NumParticles() < 1) {", "        if (true) {"),
    (C, "no tick", "        emitter.visual->Tick(timeDelta);\n", ""),
    (C, "29 emitters", "    for (int left = 30; left-- > 0;) {", "    for (int left = 29; left-- > 0;) {"),
    (C, "velocity not kept", "    emitter.velocity = {def.velocity.x, def.velocity.y, def.velocity.z, 0.0f};",
     "    emitter.velocity = {0.0f, 0.0f, 0.0f, 0.0f};"),
    (C, "reuse keeps stopping", "    emitter.stopping = false;\n    emitter.visual->SetEmitting(true);\n    return slot;",
     "    emitter.visual->SetEmitting(true);\n    return slot;"),
]

if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
