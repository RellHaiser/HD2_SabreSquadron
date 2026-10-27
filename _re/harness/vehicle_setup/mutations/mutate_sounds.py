"""Mutations of src/game/vehicle_sounds.cpp for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
K = os.path.join(ROOT, r"src\game\vehicle_sounds.cpp")

MUTATIONS = [
    # the slot order
    (K, "crash pair order", "        f(sounds[crash + k]);\n        f(sounds[crash + VEHICLE_CRASH_SOUNDS + k]);",
     "        f(sounds[crash + VEHICLE_CRASH_SOUNDS + k]);\n        f(sounds[crash + k]);"),
    (K, "crash direction", "    for (int k = VEHICLE_CRASH_SOUNDS; k-- > 0;) {\n        f(sounds[crash + k]);",
     "    for (int k = 0; k < VEHICLE_CRASH_SOUNDS; ++k) {\n        f(sounds[crash + k]);"),
    (K, "group 2 crash slot", "SOUNDS1_CRASH = 0x1D, SOUNDS2_CRASH = 0x1E;", "SOUNDS1_CRASH = 0x1D, SOUNDS2_CRASH = 0x1D;"),
    (K, "group 2 count", "constexpr int SOUNDS1 = 40, SOUNDS2 = 41;", "constexpr int SOUNDS1 = 40, SOUNDS2 = 40;"),
    # group 1
    (K, "first flags", "one(params.sounds1[i], i, 3, false, true);", "one(params.sounds1[i], i, 0, false, true);"),
    (K, "first at engine", "one(params.sounds1[i], i, 3, false, true);", "one(params.sounds1[i], i, 3, false, false);"),
    (K, "layers A loop", "one(params.layers1A[i], 3 + i, 0xB, true, true);", "one(params.layers1A[i], 3 + i, 0xB, false, true);"),
    (K, "layers B slot", "one(params.layers1B[i], 8 + i, 0xB, true, true);", "one(params.layers1B[i], 9 + i, 0xB, true, true);"),
    (K, "layers C flags", "one(params.layers1C[i], 13 + i, 0xB, true, true);", "one(params.layers1C[i], 13 + i, 3, true, true);"),
    (K, "9B4 at engine", "one(params.sound1_9B4, 0x12, 3, true, true);", "one(params.sound1_9B4, 0x12, 3, true, false);"),
    (K, "rolling 1 gate", "    if (m_maxWheelCircumference > 0.01f)\n        one(", "    if (true)\n        one("),
    (K, "rolling 1 name", "one(params.sound1_9C4, 0x13, 0xB, true, false);", "one(params.sound1_9D4, 0x13, 0xB, true, false);"),
    (K, "9E4 0 loop", "one(params.sounds1_9E4[0], 0x15, 3, true, false);", "one(params.sounds1_9E4[0], 0x15, 3, false, false);"),
    (K, "9E4 4 flags", "one(params.sounds1_9E4[4], 0x19, 0xB, true, false);", "one(params.sounds1_9E4[4], 0x19, 3, true, false);"),
    (K, "crash 1 B row", "one(params.crash1B[k], SOUNDS1_CRASH", "one(params.crash1B[3 - k], SOUNDS1_CRASH"),
    (K, "AF4 positional", "one(params.sound1_AF4, 0x25, 0, false, false);", "one(params.sound1_AF4, 0x25, 3, false, false);"),
    (K, "puncture 1 loop", "0xB, 6.0f, 30.0f, 0.3f, 0.2f, true, false);", "0xB, 6.0f, 30.0f, 0.3f, 0.2f, false, false);"),
    (K, "group 1 range", "flags, 4.0f, 50.0f, 0.3f, 0.2f, loop, atEngine);", "flags, 4.0f, 50.0f, 0.2f, 0.3f, loop, atEngine);"),
    # group 2
    (K, "first 2 loop", "two(params.sounds2[i], i, 0, m_unknownEE8, false, true);", "two(params.sounds2[i], i, 0, m_unknownEE8, true, true);"),
    (K, "sounds2 3 flags", "two(params.sounds2[3], 3, 8, m_unknownEE8, true, true);", "two(params.sounds2[3], 3, 0, m_unknownEE8, true, true);"),
    (K, "layers 2A range", "two(params.layers2A[i], 4 + i, 8, m_unknownEE8, true, true);", "two(params.layers2A[i], 4 + i, 8, m_unknownF10, true, true);"),
    (K, "layers 2C slot", "two(params.layers2C[i], 14 + i, 8, m_unknownEE8, true, true);", "two(params.layers2C[i], 13 + i, 8, m_unknownEE8, true, true);"),
    (K, "E50 at engine", "two(params.sound2_E50, 0x13, 0, m_unknownEE8, true, true);", "two(params.sound2_E50, 0x13, 0, m_unknownEE8, true, false);"),
    (K, "rolling 2 gate", "    if (m_maxWheelCircumference > 0.01f)\n        two(", "    if (m_maxWheelCircumference > 1.0f)\n        two("),
    (K, "rolling 2 range", "two(params.sound2_E60, 0x14, 8, m_unknownF10, true, false);", "two(params.sound2_E60, 0x14, 8, m_unknownEE8, true, false);"),
    (K, "E70 range", "two(params.sound2_E70, 0x15, 0, other, true, false);", "two(params.sound2_E70, 0x15, 0, m_unknownEE8, true, false);"),
    (K, "E80 0 range", "0, 4.0f, 110.0f, 0.45f, 0.37f, true, false);", "0, 4.0f, 100.0f, 0.45f, 0.37f, true, false);"),
    (K, "other order", "{m_unknown95C, m_unknown960, m_unknown964, m_unknown968};", "{m_unknown960, m_unknown95C, m_unknown964, m_unknown968};"),
    (K, "E80 4 flags", "two(params.sounds2_E80[4], 0x1A, 8, other, true, false);", "two(params.sounds2_E80[4], 0x1A, 0, other, true, false);"),
    (K, "E80 5 loop", "two(params.sounds2_E80[5], 0x1B, 0, other, true, false);", "two(params.sounds2_E80[5], 0x1B, 0, other, false, false);"),
    (K, "crash 2 slot", "two(params.crash2A[k], SOUNDS2_CRASH + k,", "two(params.crash2A[k], SOUNDS1_CRASH + k,"),
    (K, "F90 slot", "two(params.sound2_F90, 0x26,", "two(params.sound2_F90, 0x27,"),
    (K, "puncture 2 flags", "two(\"PNEU_PUNC.WAV\", 0x28, 8, other, true, false);", "two(\"PNEU_PUNC.WAV\", 0x28, 0, other, true, false);"),
    # LoadSound
    (K, "empty kept", "            slot->Release();\n            slot = nullptr;\n        }\n        return;",
     "        }\n        return;"),
    (K, "empty not unlinked", "            slot->LinkTo(nullptr, 0);\n            slot->SetOn(false);\n", "            slot->SetOn(false);\n"),
    (K, "same name reloads", "        if (std::strcmp(name, slot->GetFileName()) == 0)\n            return;\n", ""),
    (K, "created type", "CreateFrame(FRAME_SOUND)", "CreateFrame(FRAME_MODEL)"),
    (K, "open flags", "OpenSound(slot, name, flags & VEHICLE_SOUND_OPEN_8)", "OpenSound(slot, name, flags)"),
    (K, "failed kept", "!= 0) {\n        slot->Release();\n        slot = nullptr;\n        return;\n    }",
     "!= 0) {\n        slot = nullptr;\n        return;\n    }"),
    (K, "volume", "slot->SetVolume(1.0f);", "slot->SetVolume(0.5f);"),
    (K, "slot 88", "slot->Unknown88(0);", "slot->Unknown88(1);"),
    (K, "no loop", "    if (loop)\n        slot->SetLoop(true);\n", ""),
    (K, "ambient cone", "slot->SetCone(0.0f, 0.0f);", "slot->SetCone(6.283185f, 6.283185f);"),
    (K, "ambient parent", "slot->LinkTo(g_carScene->GetPrimarySector(), 0);", "slot->LinkTo(m_model, 0);"),
    (K, "ambient flag", "    if (flags & VEHICLE_SOUND_AMBIENT) {", "    if (flags & 1) {"),
    (K, "point type", "slot->SetSoundType(I3DSOUND_POINT);", "slot->SetSoundType(I3DSOUND_AMBIENT);"),
    (K, "point range order", "slot->SetRange(nearRange, farRange, range2, range3);", "slot->SetRange(farRange, nearRange, range2, range3);"),
    (K, "point parent", "        slot->LinkTo(m_model, 0);", "        slot->LinkTo(g_carScene->GetPrimarySector(), 0);"),
    (K, "engine place", "S_vector(m_smokePos.x, m_smokePos.y, m_smokePos.z)", "S_vector(m_smokePos.x, m_smokePos.z, m_smokePos.y)"),
    # the sectors, the release
    (K, "add sector order", "    for (size_t s = s_soundSectors.size(); s-- > 0;)\n        ForEachSound(m_sounds1, m_sounds2, [&](I3D_sound *sound) {\n            if (sound)\n                s_soundSectors[s]->AddSound",
     "    for (size_t s = 0; s < s_soundSectors.size(); ++s)\n        ForEachSound(m_sounds1, m_sounds2, [&](I3D_sound *sound) {\n            if (sound)\n                s_soundSectors[s]->AddSound"),
    (K, "add null", "            if (sound)\n                s_soundSectors[s]->AddSound(sound);", "            s_soundSectors[s]->AddSound(sound);"),
    (K, "remove adds", "s_soundSectors[s]->RemoveSound(sound);", "s_soundSectors[s]->AddSound(sound);"),
    (K, "release not unlinked", "            sound->LinkTo(nullptr, 0);\n", ""),
    (K, "release slot kept", "            sound->Release();\n            sound = nullptr;\n", "            sound->Release();\n"),
]


if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
