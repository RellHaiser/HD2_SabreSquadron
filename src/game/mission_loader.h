// The mission's loading (game/mission_loader.cpp, original source 0x006caad0..0x006cc280 and
// 0x006d4270..0x006d4ea0, with scene2.bin's reader 0x006cc280..0x006d4270, part 8f-iii):
// C_game::LoadMission and the loader object it keeps on its stack while it reads the mission's
// files: "Scene" (the scene itself), scene2.bin, actors.bin, sounds.bin, check2.bin (the
// checkpoints), tree.klz (the collision world), then the particles' static emitters.
#pragma once

#include <cstdint>

#include "common/c_str.h"

class C_chunk;
class C_MissionDef;
typedef long HRESULT;

// The object chunk of scene2.bin, actors.bin and sounds.bin ("SL").
constexpr uint16_t MISSION_CHUNK_OBJECTS = 0x4C53;

class C_MissionLoader {                                 // 0x34 bytes (on C_game::LoadMission's stack)
public:
    C_MissionLoader() = default;                        // (inline; its maps' constructors: part 8f-iii)
    ~C_MissionLoader() = default;                       // 0x006cb160 (inline in C_game::LoadMission)
    C_MissionLoader(const C_MissionLoader &) = delete;
    C_MissionLoader &operator=(const C_MissionLoader &) = delete;

    // The loading screen started for `name` (shown on a client, waiting for space in single
    // player, in two parts), the old mission freed, the name kept (C_game's and the loader's),
    // the car's scene set; the model, shadow and version model caches emptied.
    void Begin(const char *name, const C_MissionDef *mission);   // 0x006d4270
    // "missions\<m>\Scene" into the game's scene (its progress: the loading screen's first
    // stage).
    HRESULT LoadScene();                                // 0x006d44d0
    // scene2.bin (part 8f-iii); non-zero: failed.
    int LoadScene2();                                   // 0x006cc280
    // actors.bin / sounds.bin: their object chunk (the loading screen's stages 2 / 3).
    void LoadActors();                                  // 0x006d4910
    void LoadSounds();                                  // 0x006d4bd0
    // check2.bin: the walking and the road networks; without it both are cleared (the old
    // check.bin is only opened).
    void LoadCheckpoints();                             // 0x006d45d0
    // An object chunk's contents (part 8f-iii), `stage`: the loading screen's.
    void LoadObjects(C_chunk &chunk, int stage);        // 0x006ccab0

private:
    // (+0x00 .. +0x2F: the maps and texts scene2.bin's reader keeps, part 8f-iii.)
    C_str m_name;                                       // +0x30 the mission's directory
};
