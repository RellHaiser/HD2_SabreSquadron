"""Mutations of src/game/vehicle_state.cpp for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
K = os.path.join(ROOT, r"src\game\vehicle_state.cpp")

MUTATIONS = [
    # the helpers
    (K, "offset scale", "static_cast<int>((to - from) * 50.0f)) & 0x3F;", "static_cast<int>((to - from) * 40.0f)) & 0x3F;"),
    (K, "offset unsigned", "return static_cast<float>(static_cast<int8_t>((low & 0x20) ? low | 0xC0 : low)) * 0.02f;",
     "return static_cast<float>(static_cast<int8_t>(low)) * 0.02f;"),
    (K, "moved threshold", "return (dz * dz + dy * dy) + dx * dx > 0.0001f;", "return (dz * dz + dy * dy) + dx * dx > 0.01f;"),
    (K, "pack offset sign", "const float x = pos.x + m_boxOffset.x;", "const float x = pos.x - m_boxOffset.x;"),
    (K, "pack z bits", "const int zBits = xyBits + bits % 3;\n    const float x", "const int zBits = xyBits;\n    const float x"),
    (K, "pack y unscaled", "packed |= static_cast<uint32_t>(static_cast<int>(y / m_size.y * range));",
     "packed |= static_cast<uint32_t>(static_cast<int>(y / m_size.x * range));"),
    (K, "unpack x y swapped", "const uint32_t y = packed & xyRange;\n    const uint32_t x = (packed >> xyBits) & xyRange;",
     "const uint32_t x = packed & xyRange;\n    const uint32_t y = (packed >> xyBits) & xyRange;"),
    (K, "unpack offset sign", "* m_size.z - m_boxOffset.z);", "* m_size.z + m_boxOffset.z);"),
    (K, "key whole name", "    if (const char *dot = std::strrchr(name, '.'))\n        name = dot + 1;\n", ""),
    (K, "key empty", "    if (left == 0)\n        return 0xFFFFFFFF;\n", ""),
    (K, "key short zero", "        std::memcpy(&key, name, static_cast<size_t>(left));\n        return key;",
     "        return key;"),
    (K, "key add", "        key ^= word;", "        key += word;"),
    (K, "find first", "    for (int i = static_cast<int>(m_parts.size()); i-- > 0;) {\n        if (PartKey(static_cast<uint32_t>(i)) == key)\n            return i;\n    }",
     "    for (int i = 0; i < static_cast<int>(m_parts.size()); ++i) {\n        if (PartKey(static_cast<uint32_t>(i)) == key)\n            return i;\n    }"),
    (K, "find no-key", "    if (key == 0xFFFFFFFF)\n        return -1;\n    for", "    for"),
    (K, "original by name", "while (--original >= 0 && (m_modelFileName != m_meshOriginals[original].modelFile ||",
     "while (--original >= 0 && (false ||"),
    # the size
    (K, "size lamps", "static_cast<uint32_t>(m_lamps.size()) * sizeof(S_VehicleLampState);",
     "static_cast<uint32_t>(m_lamps.size()) * 16;"),
    (K, "size level 1", "            if (original.mesh->NumLevels() > 1)\n                size += CountMoved", "            if (false)\n                size += CountMoved"),
    (K, "size records", "static_cast<uint32_t>(m_seats.size()) + records) * 0x14;", "static_cast<uint32_t>(m_seats.size())) * 0x14;"),
    # saving
    (K, "save version", "    s.version = STATE_VERSION;", "    s.version = 9;"),
    (K, "save size", "    s.size = StateSize();", "    s.size = 0;"),
    (K, "save rot order", "    s.rot[0] = rot.s;\n    s.rot[1] = rot.v.x;", "    s.rot[0] = rot.v.x;\n    s.rot[1] = rot.s;"),
    (K, "save field swap", "    s.u90C = m_unknown90C;\n    s.u908 = m_unknown908;", "    s.u90C = m_unknown908;\n    s.u908 = m_unknown90C;"),
    (K, "save volumes flag", "    s.volumesOut = !m_mainVolumesInWorld;", "    s.volumesOut = m_mainVolumesInWorld;"),
    (K, "save lamp place", "lamps[i].pos = PackPosition(POSITION_BITS, lamp.frame->GetWorldPos() * m_invModelMatrix);",
     "lamps[i].pos = PackPosition(POSITION_BITS, lamp.frame->GetWorldPos());"),
    (K, "save lamp health", "        lamps[i].health = lamp.health;\n", ""),
    (K, "save part key", "            record.key = PartKey(static_cast<uint32_t>(index));", "            record.key = 0;"),
    (K, "save part 39", "            record.unknown39 = part.unknown39;\n", ""),
    (K, "save level 1", "                if (level == 1 && !(original.mesh->NumLevels() > 1))\n                    break;\n                I3D_mesh_level *from = original.mesh->GetLevel(level);",
     "                if (level == 1)\n                    break;\n                I3D_mesh_level *from = original.mesh->GetLevel(level);"),
    (K, "save entry shift", "const uint32_t xy = (static_cast<uint32_t>(i) << 6 | OffsetBits(a[0], b[0])) << 6 |",
     "const uint32_t xy = (static_cast<uint32_t>(i) << 5 | OffsetBits(a[0], b[0])) << 6 |"),
    (K, "save entry order", "                    *--out = xy << 6 | OffsetBits(a[2], b[2]);",
     "                    *out++ = xy << 6 | OffsetBits(a[2], b[2]);"),
    (K, "save door flags", "            record.doorFlags = door->flags;\n", ""),
    (K, "save wheel sliding", "        record.flags = w->flags & ~WHEEL_SLIDING;", "        record.flags = w->flags;"),
    (K, "save wheel spin", "        record.spin = w->unknown1F0;\n", ""),
    # Replace
    (K, "replace no place", "    m_model->Update();\n    ReadModelPlace();\n    // The turn", "    m_model->Update();\n    // The turn"),
    (K, "replace turn kept", "    m_unknown210 *= m_unknown310;\n", ""),
    (K, "replace velocity kept", "    velocity *= m_unknown310;\n", ""),
    (K, "replace contact radius", "        const float down = -w->radius;", "        const float down = -w->unknown1E4;"),
    (K, "replace ray end", "SetXYZ(w->unknown060, XYZ(w->unknown050) - XYZ(m_modelAxisY));\n        for",
     "SetXYZ(w->unknown060, XYZ(w->unknown050));\n        for"),
    (K, "replace skids out", "            w->skidMarks[k].frame1->LinkTo(g_carScene->GetPrimarySector(), 0);\n", ""),
    (K, "replace ground kept", "        w->ground = nullptr;\n        w->flags &=", "        w->flags &="),
    (K, "replace flags", "w->flags &= ~(WHEEL_ON_GROUND | WHEEL_SLIDING | WHEEL_SPINNING | WHEEL_FLAG_1000000 | WHEEL_HIT);",
     "w->flags &= ~(WHEEL_ON_GROUND | WHEEL_SLIDING | WHEEL_SPINNING | WHEEL_FLAG_1000000 | WHEEL_HIT | WHEEL_LOCKED);"),
    (K, "replace lamps", "        lamp.projector->LinkTo(m_unknown544, 0);\n        if (lamp.light)\n            lamp.light->LinkTo(m_unknown544, 0);\n    }\n    for (size_t i = m_projectors.size(); i-- > 0;) {\n        m_projectors[i].projector->LinkTo(m_unknown544, 0);",
     "        lamp.projector->LinkTo(m_unknown544, 0);\n        if (lamp.light)\n            lamp.light->LinkTo(m_unknown544, 0);\n    }\n    for (size_t i = m_projectors.size(); i-- > 0;) {\n        m_projectors[i].projector->LinkTo(m_model, 0);"),
    (K, "replace volumes", "    UpdateWheelSpin(1.0f);\n    SetVolumesInWorld(true, true);", "    UpdateWheelSpin(1.0f);"),
    (K, "replace controls", "    m_unknown2294 = 0;\n    m_unknown2295 = 0;\n\n    // The lamps", "    m_unknown2295 = 0;\n\n    // The lamps"),
    # loading
    (K, "load any version", "    if (s.version != STATE_VERSION)\n        return false;\n", ""),
    (K, "load roof never", "    if (world(1).AngleTo(S_vector(0.0f, -1.0f, 0.0f)) < 0.52f) {", "    if (world(1).AngleTo(S_vector(0.0f, -1.0f, 0.0f)) > 0.52f) {"),
    (K, "load roof x", "SetXYZ(m_modelAxisX, S_vector(z.z * y.y - z.y * y.z, z.x * y.z - z.z * y.x, z.y * y.x - y.y * z.x));",
     "SetXYZ(m_modelAxisX, S_vector(z.y * y.z - z.z * y.y, z.z * y.x - z.x * y.z, y.y * z.x - z.y * y.x));"),
    (K, "load no replace", "    Replace();\n\n    // The motion", "\n    // The motion"),
    (K, "load speed", "    m_speed = XYZ(m_velocity).Magnitude();", "    m_speed = 0.0f;"),
    (K, "load field swap", "    m_unknown908 = s.u908;\n    m_unknown90C = s.u90C;", "    m_unknown908 = s.u90C;\n    m_unknown90C = s.u908;"),
    (K, "load volumes", "    SetVolumesInWorld(s.volumesOut == 0, m_volume490InWorld);", "    SetVolumesInWorld(true, m_volume490InWorld);"),
    (K, "load lamp flags", "            m_lamps[best].flags = lamps[i].flags;\n", ""),
    (K, "load original refs", "            ++m_meshOriginals[original].refs;\n", "            ;\n"),
    (K, "load original levels", "                kept.mesh->SetLevel(nullptr, level);\n            m_meshOriginals.push_back(kept);",
     "                ;\n            m_meshOriginals.push_back(kept);"),
    (K, "load health", "        part.health = record.health;\n", ""),
    (K, "load glass state", "        if (part.type == VEHICLE_PART_GLASS) {\n            CrackGlass", "        if (false) {\n            CrackGlass"),
    (K, "load part state", "            part.state = record.state;\n", ""),
    (K, "load level 1 offsets", "const uint32_t *moved = offsets + (level ? record.moved[0] : 0);", "const uint32_t *moved = offsets;"),
    (K, "load vertex z", "                b[2] = OffsetOf(entry) + a[2];", "                b[2] = OffsetOf(entry >> 6) + a[2];"),
    (K, "load no refresh", "            part.mesh->GetLevel(level)->Unknown2C();\n", ""),
    (K, "load seat load", "        seat.load = seats[i].load;\n", ""),
    (K, "load door angle", "            door->angle = seats[i].doorAngle;\n", ""),
    (K, "load wheel 090", "        w->unknown090 = w->unknown080;\n", ""),
    (K, "load wheel load", "        w->unknown168 = w->extension * w->unknown160;\n", ""),
    (K, "load holder", "        w->holder->SetPosInline(S_vector(w->holderPos.x, w->holderPos.y - w->extension, w->holderPos.z));",
     "        w->holder->SetPosInline(S_vector(w->holderPos.x, w->holderPos.y, w->holderPos.z));"),
    (K, "load contact", "SetXYZ(w->unknown050, S_vector(w->worldPos.x - r * m_modelAxisY.x,", "SetXYZ(w->unknown050, S_vector(w->worldPos.x + r * m_modelAxisY.x,"),
    (K, "load detached shown", "        if (w->flags & (WHEEL_DETACHED | WHEEL_DUMMY))\n            w->frame->SetOn(false);\n", ""),
]

if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
