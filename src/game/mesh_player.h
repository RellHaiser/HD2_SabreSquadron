// The mesh player (game/mesh_player.cpp, original source 0x004840d0..0x00484e70; the GEffects
// module's +0xE4): missions\<name>\meshplayer.bin names a movie and visuals (a cinema's screen);
// the movie (GameMovie\<movie>) plays on their faces, a picture decoded whenever one of them was
// drawn, from the start again at its end.
#pragma once

#include <vector>

#include "common/c_str.h"

class I3D_frame;
class I3D_texture;
class ISND_stream;

class C_MeshPlayer {                                    // 0x28 bytes
public:
    C_MeshPlayer();                                     // 0x00484100
    ~C_MeshPlayer();                                    // 0x004841e0
    C_MeshPlayer(const C_MeshPlayer &) = delete;
    C_MeshPlayer &operator=(const C_MeshPlayer &) = delete;

    // The mission's file: chunk 0x44C the movie's name, chunks 0x4B0 the visuals' (scene frames).
    void Load();                                        // 0x00484260
    // (`timeDelta` is not used.)
    void Update(int timeDelta);                         // 0x00484a70
    // The visuals' own textures back, the movie stopped.
    void Free();                                        // 0x00484ae0

private:
    friend struct S_MeshPlayerTest;

    // `movie` on `frames` (the object visuals among them with flag 0x800): their first face
    // group's texture kept, every face group showing the movie's picture.
    void Play(const std::vector<I3D_frame *> &frames, const char *movie);   // 0x004847a0

    ISND_stream *m_stream = nullptr;                    // +0x00
    C_str m_movie;                                      // +0x04
    std::vector<I3D_frame *> m_frames;                  // +0x0C
    std::vector<I3D_texture *> m_textures;              // +0x1C their own (a reference each)
};
