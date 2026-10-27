"""Mutations of the running sounds in src/game/vehicle_sounds.cpp for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
K = os.path.join(ROOT, r"src\game\vehicle_sounds.cpp")

SPEED_TAIL = "+ curve[0];\n    if (!sound->IsPlaying(0)) {\n        if (!(volume > 0.01f))\n            return;\n        sound->SetOn(true);\n    } else if (volume < 0.01f) {"

MUTATIONS = [
    # UpdateSpeedSound
    (K, "speed clamp", "    if (speed > top)\n        speed = top;\n", ""),
    (K, "speed low edge", "    if (!(speed >= low)) {", "    if (!(speed > low)) {"),
    (K, "speed off kept", "        if (sound->IsPlaying(0))\n            sound->SetOn(false);\n        return;\n    }\n    float volume = 1.0f;",
     "        return;\n    }\n    float volume = 1.0f;"),
    (K, "speed fade", "volume = (speed - low) / (full - low);", "volume = (speed - low) / (top - low);"),
    (K, "speed pitch", "((top - low) / (curve[1] - curve[0])) + curve[0];", "((top - low) / (curve[1] - curve[0])) + curve[1];"),
    (K, "speed start gate", SPEED_TAIL, SPEED_TAIL.replace("(volume > 0.01f)", "(volume > 0.0f)")),
    (K, "speed stop gate", SPEED_TAIL, SPEED_TAIL.replace("(volume < 0.01f)", "(volume < 0.001f)")),
    (K, "speed no frequency", "    sound->SetFrequency(pitch);\n    sound->SetVolume(scale * volume);\n}\n\n// A switched loop",
     "    sound->SetVolume(scale * volume);\n}\n\n// A switched loop"),
    # UpdateRollingSound, UpdateRimSound
    (K, "rolling speed", "S_vector(m_velocity.x, m_velocity.y, m_velocity.z).Magnitude();", "S_vector(m_velocity.x, 0.0f, m_velocity.z).Magnitude();"),
    (K, "rolling curve 1", "m_unknownC10, m_unknownC14, m_unknownC18};", "m_unknownC10, m_unknownC18, m_unknownC14};"),
    (K, "rolling slot 1", "if (I3D_sound *sound = m_sounds1[ROLLING1])", "if (I3D_sound *sound = m_sounds1[ROLLING2])"),
    (K, "rolling detail", "} else if ((m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS) && m_sounds2[ROLLING2]) {", "} else if (m_sounds2[ROLLING2]) {"),
    (K, "rolling curve 2", "{m_unknownEF8, m_unknownEFC, m_unknownF00", "{m_unknownEFC, m_unknownEF8, m_unknownF00"),
    (K, "rim off 2", "        if (m_sounds2[RIM2])\n            m_sounds2[RIM2]->SetOn(false);\n        return;", "        return;"),
    (K, "rim speed sign", "const float speed = std::fabs((m_modelAxisZ.z", "const float speed = ((m_modelAxisZ.z"),
    (K, "rim speed axis", "m_modelAxisZ.y * m_velocity.y) +", "m_modelAxisZ.z * m_velocity.y) +"),
    (K, "rim curves", "UpdateSpeedSound(sound, speed, m_unknownC20, s_soundVolume);", "UpdateSpeedSound(sound, speed, m_unknownF20, s_soundVolume);"),
    (K, "rim detail", "} else if ((m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS) && m_sounds2[RIM2]) {", "} else if (m_sounds2[RIM2]) {"),
    (K, "rim slot 2", "UpdateSpeedSound(m_sounds2[RIM2], speed, m_unknownF20", "UpdateSpeedSound(m_sounds2[RIM2 - 1], speed, m_unknownF20"),
    # UpdateTyreSqueal
    (K, "squeal ground", "if (!(w->flags & WHEEL_ON_GROUND) || (w->flags & WHEEL_DUMMY) ||", "if ((w->flags & WHEEL_DUMMY) ||"),
    (K, "squeal dummy", "if (!(w->flags & WHEEL_ON_GROUND) || (w->flags & WHEEL_DUMMY) ||", "if (!(w->flags & WHEEL_ON_GROUND) ||"),
    (K, "squeal surface", "!g_carSurfaces[g_carSurfaceOfMaterial[w->surface]].tyreSqueal || ", ""),
    (K, "squeal speed", "!(m_speed > 0.6f))", "!(m_speed >= 0.6f))"),
    (K, "squeal locked", "        if (w->flags & WHEEL_LOCKED) {\n            wheel = 1.0f;", "        if (false) {\n            wheel = 1.0f;"),
    (K, "squeal past upper", "} else if (w->unknown1D4 > w->unknown224) {", "} else if (w->unknown1D4 > w->unknown224 + 1.0f) {"),
    (K, "squeal slip", "/ (w->unknown224 - w->unknown220);", "/ (w->unknown224);"),
    (K, "squeal slow contact", "        if (square < 4.0f)\n", "        if (square < 1.0f)\n"),
    (K, "squeal quarter", "wheel = wheel * square * 0.25f;", "wheel = wheel * square * 0.5f;"),
    (K, "squeal spin ratio", "m_engineRevs / m_gearRatios[m_gear + 1]", "m_engineRevs / m_gearRatios[m_gear]"),
    (K, "squeal spin over", "const float over = m_unknown5E4 + m_speed;", "const float over = m_unknown5E4;"),
    (K, "squeal spin full", "m_unknown5E8 + m_speed < spin ? 1.0f", "m_unknown5E8 < spin ? 1.0f"),
    (K, "squeal spin max", "                if (spinning > wheel)\n                    wheel = spinning;", "                wheel = spinning;"),
    (K, "squeal max", "        if (wheel > level)\n            level = wheel;", "        level = wheel;"),
    (K, "squeal rise", "const float step = timeDelta * m_unknown5EC;", "const float step = timeDelta * m_unknown5EC * 2.0f;"),
    (K, "squeal falls slowly", "    if (step > level - m_unknown5F0)\n", "    if (step > std::fabs(level - m_unknown5F0))\n"),
    (K, "squeal off gate", "    if (m_unknown5F0 < 0.01f) {", "    if (m_unknown5F0 < 0.001f) {"),
    (K, "squeal both off", "        if (m_sounds2[SQUEAL2])\n            m_sounds2[SQUEAL2]->SetOn(false);\n        return;", "        return;"),
    (K, "squeal restart", "        if (!sound->IsPlaying(0))\n            sound->SetOn(true);\n        sound->SetVolume(s_soundVolume * m_unknown5F0);",
     "        sound->SetOn(true);\n        sound->SetVolume(s_soundVolume * m_unknown5F0);"),
    (K, "squeal other kept", "    if (other)\n        other->SetOn(false);\n}\n\nvoid C_Vehicle::PlayHandbrakeSound", "}\n\nvoid C_Vehicle::PlayHandbrakeSound"),
    (K, "squeal groups", "I3D_sound *sound = m_unknown2298 ? m_sounds1[SQUEAL1] : m_sounds2[SQUEAL2];", "I3D_sound *sound = m_unknown2298 ? m_sounds2[SQUEAL2] : m_sounds1[SQUEAL1];"),
    # PlayHandbrakeSound
    (K, "handbrake detail", "    if (!(m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS))\n        return;\n    // Once per pull", "    // Once per pull"),
    (K, "handbrake once", "if (sound && !sound->IsPlaying(0) && !m_unknown228E)", "if (sound && !sound->IsPlaying(0))"),
    (K, "handbrake playing", "if (sound && !sound->IsPlaying(0) && !m_unknown228E)", "if (sound && !m_unknown228E)"),
    (K, "handbrake flag", "        StartSound(sound, s_soundVolume);\n    m_unknown228E = 1;", "    {\n        StartSound(sound, s_soundVolume);\n        m_unknown228E = 1;\n    }"),
    (K, "handbrake slots", "m_unknown2298 ? m_sounds1[HANDBRAKE1] : m_sounds2[HANDBRAKE2]", "m_unknown2298 ? m_sounds1[HANDBRAKE2] : m_sounds2[HANDBRAKE2]"),
    # SwitchLoop, SwitchLoopSoundA / B
    (K, "loop restart", "    if (sound && !sound->IsPlaying(0))\n        StartSound(sound, volume);", "    if (sound)\n        StartSound(sound, volume);"),
    (K, "loop other kept", "    if (other)\n        other->SetOn(false);\n}\n\n}  // namespace", "}\n\n}  // namespace"),
    (K, "loop off 1", "    if (!on) {\n        if (sound1)\n            sound1->SetOn(false);\n", "    if (!on) {\n"),
    (K, "loop group", "I3D_sound *sound = group1 ? sound1 : sound2, *other = group1 ? sound2 : sound1;",
     "I3D_sound *sound = group1 ? sound2 : sound1, *other = group1 ? sound1 : sound2;"),
    (K, "loop A slots", "SwitchLoop(m_sounds1[LOOP_A1], m_sounds2[LOOP_A2]", "SwitchLoop(m_sounds1[LOOP_B1], m_sounds2[LOOP_A2]"),
    (K, "loop B slots", "SwitchLoop(m_sounds1[LOOP_B1], m_sounds2[LOOP_B2]", "SwitchLoop(m_sounds1[LOOP_B1], m_sounds2[LOOP_A2]"),
    (K, "loop B on ignored", "SwitchLoop(m_sounds1[LOOP_B1], m_sounds2[LOOP_B2], m_unknown2298 != 0, on,", "SwitchLoop(m_sounds1[LOOP_B1], m_sounds2[LOOP_B2], m_unknown2298 != 0, true,"),
    # PlayWheelBurst
    (K, "burst sounds off", "    if (!m_soundsOn)\n        return;\n    if (m_unknown2298) {\n        if (I3D_sound *sound = m_sounds1[BURST1])",
     "    if (m_unknown2298) {\n        if (I3D_sound *sound = m_sounds1[BURST1])"),
    (K, "burst group 1 on", "            StartSound(sound, s_soundVolume);\n        return;\n    }\n    if (I3D_sound *sound = m_sounds2[BURST2]) {",
     "            StartSound(sound, s_soundVolume);\n    }\n    if (I3D_sound *sound = m_sounds2[BURST2]) {"),
    (K, "burst no place", "        sound->SetPosInline(S_vector(pos.x, pos.y, pos.z));\n", ""),
    (K, "burst wheel", "m_wheels[wheel]->modelPos;", "m_wheels[0]->modelPos;"),
    (K, "burst place order", "        sound->SetPosInline(S_vector(pos.x, pos.y, pos.z));", "        sound->SetPosInline(S_vector(pos.x, pos.z, pos.y));"),
]


if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
