"""Mutations of the engine sounds in src/game/vehicle_sounds.cpp for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
K = os.path.join(ROOT, r"src\game\vehicle_sounds.cpp")

LAYER_TAIL = "+ rows[0][i];\n    if (!sound->IsPlaying(0)) {\n        if (!(volume > 0.01f))\n            return;\n        sound->SetOn(true);\n    } else if (volume < 0.01f) {"

MUTATIONS = [
    # UpdateLayer
    (K, "layer low edge", "if (revs < low || !(revs <= high) || muted) {", "if (revs <= low || !(revs <= high) || muted) {"),
    (K, "layer high edge", "if (revs < low || !(revs <= high) || muted) {", "if (revs < low || !(revs < high) || muted) {"),
    (K, "layer muted ignored", "if (revs < low || !(revs <= high) || muted) {", "if (revs < low || !(revs <= high)) {"),
    (K, "layer off kept", "        if (sound->IsPlaying(0))\n            sound->SetOn(false);\n        return;\n    }\n    float volume = (rows",
     "        return;\n    }\n    float volume = (rows"),
    (K, "layer mix", "(rows[6][i] - rows[7][i]) * load + rows[7][i];", "(rows[6][i] - rows[7][i]) * load + rows[6][i];"),
    (K, "start gate", LAYER_TAIL, LAYER_TAIL.replace("(volume > 0.01f)", "(volume > 0.0f)")),
    (K, "stop gate", LAYER_TAIL, LAYER_TAIL.replace("(volume < 0.01f)", "(volume < 0.001f)")),
    (K, "no frequency", "    sound->SetFrequency(pitch);\n    sound->SetVolume(scale * volume);\n}\n\n// A sound by a speed",
     "    sound->SetVolume(scale * volume);\n}\n\n// A sound by a speed"),
    (K, "layer unscaled", "    sound->SetVolume(scale * volume);\n}\n\n// A sound by a speed",
     "    sound->SetVolume(volume);\n}\n\n// A sound by a speed"),
    (K, "fade gate", "    if (volume > 0.01f) {\n        if (revs < fadeIn", "    if (volume > 0.1f) {\n        if (revs < fadeIn"),
    (K, "fade in", "volume *= (revs - low) / (fadeIn - low);", "volume *= (revs - low) / (fadeIn - low + 1.0f);"),
    (K, "fade out", "volume *= 1.0f - (revs - fadeOut) / (high - fadeOut);", "volume *= 1.0f - (revs - fadeOut) / (high - low);"),
    (K, "pitch", "((high - low) / (rows[1][i] - rows[0][i])) + rows[0][i];", "((high - low) / (rows[1][i] - rows[0][i])) + rows[1][i];"),
    # UpdateEngineSound
    (K, "fade clamp", "    if (m_unknownF48 > 0.3f)\n        m_unknownF48 = 0.3f;", "    if (m_unknownF48 > 0.4f)\n        m_unknownF48 = 0.4f;"),
    (K, "fade scale", "s_soundVolume = s_soundVolume * m_unknownF48 * 3.3333333f;", "s_soundVolume = s_soundVolume * 0.3f * 3.3333333f;"),
    (K, "scale kept", "    s_soundVolume = volume;\n}", "}"),
    (K, "revs low", "    if (revs < m_idleRevs)\n        revs = m_idleRevs;", "    if (revs < m_idleRevs - 1.0f)\n        revs = m_idleRevs;"),
    (K, "revs high", "    else if (revs > m_revLimit - 0.001f)\n        revs = m_revLimit - 0.001f;",
     "    else if (revs > m_revLimit - 1.0f)\n        revs = m_revLimit - 1.0f;"),
    (K, "switch group kept", "        m_unknown2298 = m_unknown2297;\n    }\n\n    if (!(m_unknownF6C", "    }\n\n    if (!(m_unknownF6C"),
    (K, "switch idle 2", "            switchOff(m_sounds2[IDLE2]);\n", ""),
    (K, "switch single", "            switchOff(m_sounds2[SINGLE_LOOP2]);\n", ""),
    (K, "switch rolling 1", "            switchOff(m_sounds1[ROLLING1]);\n", ""),
    (K, "switch sets", "        for (int set = 0; set < 3; ++set)\n            stopSet(set);", "        for (int set = 0; set < 2; ++set)\n            stopSet(set);"),
    (K, "single mode group", "if (!(m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS) && !m_unknown2298) {", "if (!(m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS)) {"),
    (K, "single volume", "loudness = (revs - m_idleRevs) / (m_unknownCF4 - m_idleRevs) * loudness;", "loudness = (revs - m_idleRevs) / (m_unknownCF4 - m_idleRevs);"),
    (K, "single edge", "            if (revs < m_unknownCF4)\n", "            if (revs < m_unknownCF4 - 5.0f)\n"),
    (K, "single pitch", "(m_unknownCF0 - m_unknownCEC)) + m_unknownCEC;", "(m_unknownCF0 - m_unknownCEC)) + m_unknownCF0;"),
    (K, "single start", "            if (!sound->IsPlaying(0))\n                sound->SetOn(true);\n", ""),
    (K, "reverse stops C", "        } else {\n            stopSet(2);\n        }", "        } else {\n        }"),
    (K, "reverse keeps B", "            stopSet(0);\n            stopSet(1);", "            stopSet(0);"),
    (K, "set B load", "1.0f - m_unknown78C, flagsB[i]", "m_unknown78C, flagsB[i]"),
    (K, "set B flags", "flagsB[i] && m_gear == 0", "flagsA[i] && m_gear == 0"),
    (K, "neutral", "flagsA[i] && m_gear == 0", "flagsA[i] && m_gear == 1"),
    (K, "set C rows", "UpdateLayer(layer(2, i), rows[2]", "UpdateLayer(layer(2, i), rows[0]"),
    (K, "layer slots", ": m_sounds2[LAYERS2 + set * VEHICLE_SOUND_LAYERS + i];", ": m_sounds2[LAYERS1 + set * VEHICLE_SOUND_LAYERS + i];"),
    (K, "group 1 rows", "const auto &rows = group1 ? m_layers1 : m_layers2;", "const auto &rows = m_layers2;"),
    # (Not "revs < loud" alone: past the silent revs the volume is negative and switches it off too.)
    (K, "idle low edge", "        if (revs < loud || !(revs <= silent)) {", "        if (revs <= loud || !(revs <= silent)) {"),
    (K, "idle formula", "(silent - revs) / (silent - loud)", "(revs - loud) / (silent - loud)"),
    (K, "idle peak", "(group1 ? m_unknownC04 : m_unknownEE4)", "(group1 ? m_unknownEE4 : m_unknownC04)"),
    (K, "idle slots", "group1 ? m_sounds1[IDLE1] : m_sounds2[IDLE2]", "group1 ? m_sounds1[IDLE1] : m_sounds2[IDLE1]"),
    (K, "idle unscaled", "idle->SetVolume(s_soundVolume * loudness);", "idle->SetVolume(loudness);"),
    (K, "idle stop gate", "            } else if (loudness < 0.01f) {", "            } else if (loudness < 0.0f) {"),
    # SwitchEngine
    (K, "quiet ignored", "            if (IsPlaying(sound))\n                quiet = false;", "            if (false)\n                quiet = false;"),
    (K, "off without start", "        if (!m_unknown22A2)\n            return;\n        StopSounds();", "        StopSounds();"),
    (K, "off stop group", "I3D_sound *stop = m_unknown2297 ? m_sounds1[1] : m_sounds2[1];", "I3D_sound *stop = m_unknown2298 ? m_sounds1[1] : m_sounds2[1];"),
    (K, "off rewind", "                sound->Play(0);", "                sound->Play(1);"),
    (K, "off revs", "        m_engineRevs = 0.0f;\n        m_engineOn = false;", "        m_engineOn = false;"),
    (K, "off 22A0", "        m_unknown22A0 = 0;\n        return;", "        return;"),
    (K, "start no rewind", "            starter->Play(0);\n", ""),
    (K, "start group", "I3D_sound *starter = m_unknown2297 ? m_sounds1[0] : m_sounds2[0];", "I3D_sound *starter = m_unknown2298 ? m_sounds1[0] : m_sounds2[0];"),
    (K, "start when on", "if (!m_unknown22A2 && !m_engineOn && quiet) {", "if (!m_unknown22A2 && quiet) {"),
    (K, "300 length", "if (length > 300 && length - ", "if (length >= 300 && length - "),
    (K, "300 left", "static_cast<uint32_t>(sound->GetPosition()) > 300)", "static_cast<uint32_t>(sound->GetPosition()) >= 300)"),
    (K, "signed", "            const uint32_t length = static_cast<uint32_t>(sound->GetLength());\n            if (length > 300 && length - static_cast<uint32_t>(sound->GetPosition()) > 300) {",
     "            const int length = sound->GetLength();\n            if (length > 300 && length - sound->GetPosition() > 300) {"),
    (K, "waiting fade", "                m_unknownF48 = 0.0f;\n                done = false;", "                done = false;"),
    (K, "silent wait", "    if (m_soundsOn) {\n        bool done = true;", "    if (true) {\n        bool done = true;"),
    (K, "group taken", "    m_engineOn = true;\n    m_unknown2298 = m_unknown2297;\n}", "    m_engineOn = true;\n}"),
    # PlayGearSound, PlayEngineFail
    (K, "gear detail", "        return;\n    // Not over the last one", "        ;\n    // Not over the last one"),
    (K, "gear odds", "std::rand() % 100 > 79;", "std::rand() % 100 > 80;"),
    (K, "gear group 2 block", "if (IsPlaying(m_sounds2[GEAR2 + m_unknown650]) || !quiet)", "if (!quiet)"),
    (K, "gear group 1 block", "const bool quiet = !IsPlaying(m_sounds1[GEAR1 + m_unknown650]);", "const bool quiet = true;"),
    (K, "gear slots", "m_sounds2[GEAR2 + m_unknown650])\n        StartSound", "m_sounds2[GEAR1 + m_unknown650])\n        StartSound"),
    (K, "fail group", "if (I3D_sound *sound = m_unknown2298 ? m_sounds1[2] : m_sounds2[2])", "if (I3D_sound *sound = m_unknown2297 ? m_sounds1[2] : m_sounds2[2])"),
    (K, "fail block 1", "const bool quiet = !IsPlaying(m_sounds1[2]);", "const bool quiet = true;"),
    (K, "start order", "    sound->SetOn(true);\n    sound->SetVolume(volume);", "    sound->SetVolume(volume);\n    sound->SetOn(true);"),
    # StopEngineLoops, StopSounds
    (K, "loops idle 1", "    stop(m_sounds1[IDLE1]);\n    stop(m_sounds2[SINGLE_LOOP2]);", "    stop(m_sounds2[SINGLE_LOOP2]);"),
    (K, "loops count", "for (int i = 0; i < 3 * VEHICLE_SOUND_LAYERS; ++i)\n        stop(m_sounds2[LAYERS2 + i]);",
     "for (int i = 0; i < 2 * VEHICLE_SOUND_LAYERS; ++i)\n        stop(m_sounds2[LAYERS2 + i]);"),
    (K, "loops not immediate", "            sound->SetActive(false, true);\n    };", "            sound->SetActive(false, false);\n    };"),
    (K, "stop group 1 only", "    ForEachSound(m_sounds1, m_sounds2, [](I3D_sound *sound) {\n        if (sound)\n            sound->SetActive(false, true);",
     "    ForEachSlot(m_sounds1, SOUNDS1_CRASH, SOUNDS1, [](I3D_sound *sound) {\n        if (sound)\n            sound->SetActive(false, true);"),
]


if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
