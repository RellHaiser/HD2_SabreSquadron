// The mission's loading (game/mission_loader.cpp, original source 0x006caad0..0x006cc280 and
// 0x006d4270..0x006d4ea0, with scene2.bin's reader 0x006cc280..0x006d4270, part 8f-iii):
// C_game::LoadMission and the loader object it keeps on its stack while it reads the mission's
// files: "Scene" (the scene itself), scene2.bin, actors.bin, sounds.bin, check2.bin (the
// checkpoints), tree.klz (the collision world), then the particles' static emitters.
#pragma once

#include <cstdint>
#include <cstring>
#include <map>

#include "common/c_str.h"

class C_chunk;
class C_MissionDef;
class I3D_frame;
typedef long HRESULT;

// The object chunk of scene2.bin, actors.bin and sounds.bin ("SL").
constexpr uint16_t MISSION_CHUNK_OBJECTS = 0x4C53;

// The loader's maps' order: the names' bytes (strcmp).
struct S_MissionNameLess {
    bool operator()(const C_str &a, const C_str &b) const { return std::strcmp(a, b) < 0; }   // 0x006d4f50
};

class C_MissionLoader {                                 // 0x34 bytes (on C_game::LoadMission's stack)
public:
    C_MissionLoader() = default;                        // (inline: its maps' constructors)
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
    // scene2.bin: the scene's frames named (the primary, the backdrop and the omni sector too),
    // the scene's camera set up (made at (10, 5, 10) without one; perspective, 0.1 .. 500 m, 80
    // degrees), then its object chunk (the loading screen's stage 1). 0 when it loaded (or holds
    // another chunk), 1 when the chunk failed, 3 without the file.
    int LoadScene2();                                   // 0x006cc280
    // actors.bin / sounds.bin: their object chunk (the loading screen's stages 2 / 3).
    void LoadActors();                                  // 0x006d4910
    void LoadSounds();                                  // 0x006d4bd0
    // check2.bin: the walking and the road networks; without it both are cleared (the old
    // check.bin is only opened).
    void LoadCheckpoints();                             // 0x006d45d0
    // An object chunk's contents (`stage`: the loading screen's, its progress by the file's
    // position): the scene's settings (the camera's field of view and range, the game's music),
    // the objects (chunk 0x4000), the shifting textures, the actors, the checkpoints, the lightmap
    // groups, the shadows' parameters. False when a chunk header does not read (or an object
    // fails).
    bool LoadObjects(C_chunk &chunk, int stage);        // 0x006ccab0
    // An object (chunk 0x4010; part 8f-iv).
    bool LoadObject(C_chunk &chunk);                    // 0x006cdc90

private:
    // The scene's EnumFrames callback: `context`'s m_frames gets the frame by its name.
    static uint32_t __stdcall NameFrame(I3D_frame *frame, void *context);   // 0x006cc8d0

    // The scene's frames by name (scene2.bin's and the actors' names look them up; a name looked
    // up stays, with a null frame when the scene has none of it).
    std::map<C_str, I3D_frame *, S_MissionNameLess> m_frames;   // +0x00
    std::map<C_str, C_str, S_MissionNameLess> m_unknown10;      // +0x10 (part 8f-iv)
    C_str m_unknown20;                                  // +0x20 (part 8f-iv)
    C_str m_unknown24;                                  // +0x24
    C_str m_unknown28;                                  // +0x28
    C_str m_unknown2C;                                  // +0x2C
    C_str m_name;                                       // +0x30 the mission's directory
};
