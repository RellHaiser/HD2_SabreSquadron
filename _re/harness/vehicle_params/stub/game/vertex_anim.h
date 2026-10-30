// The vertex animation module as the actors need it (the real one: src/game/vertex_anim.h; the
// harness fakes its functions). The world build (WORLD_TEST, part 8) tests the real one.
#pragma once

#ifdef WORLD_TEST
#include_next "game/vertex_anim.h"
#else

#include <cstdint>

struct S_VertexAnimation {
    uint8_t unknown00[0x18];
    int levelCount;                                     // +0x18
};
struct S_VertexAnimPlayer {
    int maxLevel = 0;                                   // +0x00
    class I3D_frame *visual = nullptr;                  // +0x04
    int startFrame = 0;
    int frame = 0;
    int nextFrame = 1;
    int time = 0;
    int frameTime = 250;                                // +0x18 ms
    bool pingPong = false;
    bool backwards = false;
    bool stopped = false;
    bool loop = true;
    int distant = 0;
    S_VertexAnimation *animation = nullptr;             // +0x24
    int cull = 1;

    void Reset();
    void Init(class I3D_frame *visual, S_VertexAnimation *animation);
};
class C_str;
class C_VertexAnimModule {
public:
    S_VertexAnimation *FindAnimation(const C_str &name);
    void RemovePlayer(S_VertexAnimPlayer *player, bool destroy);
    void AddPlayer(S_VertexAnimPlayer *player);
};
extern C_VertexAnimModule g_vertexAnimModule;
#endif
