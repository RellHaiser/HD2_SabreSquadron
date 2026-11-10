// The vertex animation module ("Vertex anim", module 9; the object 0x0089b474; game/vertex_anim.cpp,
// original source 0x004736d0..0x00474f80): flags, palm leaves and the supply box's lid, animated by
// rewriting an object visual's vertices from recorded frames.
//
// Tables\Vertanim.def holds the animations: after a 4-byte header (0, not used) each one is
//   char name[0x14]; int lastFrame; int levelCount; int vertexCount[levelCount];
//   then for every frame 0..lastFrame, for every level: vertexCount[level] S_VertexAnimVertex.
// Missions\<mission>\Vertanim.bin places them on the mission's objects: an int version (1 adds the
// last field), then per player
//   int maxLevel; char frame[0x80]; int startFrame; int time; int frameTime; char pingPong;
//   char animation[0x14]; (version 1) int cull.
// Only the client side animates, and only while the detail switch DETAIL_VERTEX_ANIMATION is on.
#pragma once

#include <cstdint>
#include <vector>

#include "game/module.h"
#include "ls3d/math.h"

class C_str;
class I3D_frame;

// A frame's vertex of an animation level (0x20 bytes): the mesh vertex's position and normal (the
// mesh's texture coordinates stay).
struct S_VertexAnimVertex {
    S_vector4 pos;                                      // +0x00
    S_vector4 normal;                                   // +0x10
};

// A loaded animation (0x24 bytes; ctor 0x00473700, dtor 0x00473720).
struct S_VertexAnimation {
    S_VertexAnimation() = default;
    ~S_VertexAnimation();                               // 0x00473720
    S_VertexAnimation(const S_VertexAnimation &) = delete;
    S_VertexAnimation &operator=(const S_VertexAnimation &) = delete;

    char name[0x14] = {};                               // +0x00
    int lastFrame = 0;                                  // +0x14
    int levelCount = 0;                                 // +0x18 the mesh's detail levels it covers
    int *vertexCounts = nullptr;                        // +0x1C per level
    // [frame][level] -> vertexCounts[level] vertices (16-byte aligned).
    S_VertexAnimVertex ***frames = nullptr;             // +0x20
};

// A player of an animation on an object visual (0x2C bytes; the multiplayer zone's flag and the
// supply box make their own with new).
struct S_VertexAnimPlayer {
    // The detail levels up to this one are animated (the visual's level drawn decides).
    int maxLevel = 0;                                   // +0x00
    I3D_frame *visual = nullptr;                        // +0x04
    // The frame StartMission begins at (a random one when negative; else 0).
    int startFrame = 0;                                 // +0x08
    int frame = 0;                                      // +0x0C blended from
    int nextFrame = 1;                                  // +0x10 blended to
    int time = 0;                                       // +0x14 ms into the frame
    int frameTime = 250;                                // +0x18 ms per frame
    bool pingPong = false;                              // +0x1C back and forth, else round
    bool backwards = false;                             // +0x1D (ping-pong) on the way back
    bool stopped = false;                               // +0x1E (also set at the end without loop)
    bool loop = true;                                   // +0x1F
    // Set by the module's Tick every 300 ms: small on screen, it moves in the 100 ms ticks only.
    // (This and the animation are not set by the original's constructor; Init sets them.)
    int distant = 0;                                    // +0x20
    S_VertexAnimation *animation = nullptr;             // +0x24
    // The file's last field: while set, the vertices are only rewritten after the visual was drawn
    // and the player may go to the ticks.
    int cull = 1;                                       // +0x28

    void Init(I3D_frame *visual, S_VertexAnimation *animation);   // 0x00473790
    // The visual's mesh back to the animation's first frame.
    void Reset();                                       // 0x004737c0
    // Advances `timeDelta` ms and rewrites the visual's vertices.
    void Update(int timeDelta);                         // 0x00473960

private:
    // The drawn level's vertices blended from `from` to `to` by `t` (Reset: frame 0 to itself).
    void Blend(int from, int to, float t);
};

class C_VertexAnimModule : public C_Module {           // vtable 0x00810060
public:
    C_VertexAnimModule(int id, const char *name);       // 0x00473d60
    ~C_VertexAnimModule() override;                     // 0x00473df0

    void Init() override;                               // 0x00474a00
    void Done() override;                               // 0x00474a20
    // The far players advance in the ticks; every 300 ms all are sorted near or far again.
    void Tick(int timeDelta) override;                  // 0x00474740
    // The near players advance every frame.
    void Update(int timeDelta) override;                // 0x004749a0
    void LoadMissionData(bool server) override;         // 0x00474aa0
    void FreeMissionData(bool server) override;         // 0x00474ac0
    void StartMission(bool server) override;            // 0x00474b40
    // Deletes the players (the mission's Vertanim.bin is not read again).
    void StopMission(bool server) override;             // 0x00474c10
    void RestartMission(bool server) override;          // 0x00474c90
    bool SaveGame(C_chunk &) override { return true; }  // 0x00473db0 (DS 0x0048fe60)
    bool LoadGame(C_chunk &) override { return true; }  // 0x00473dc0 (DS 0x0048fe80)

    // The loaded animation of that name (case sensitive), or null.
    S_VertexAnimation *FindAnimation(const C_str &name);   // 0x00473e30
    // The player out of the playing ones (the last one fills its place), deleted with `destroy`.
    void RemovePlayer(S_VertexAnimPlayer *player, bool destroy);   // 0x004746b0
    // Plays it (the callers' vector::insert at the end, 0x00474cf0).
    void AddPlayer(S_VertexAnimPlayer *player) { m_players.push_back(player); }

private:
    // Tables\Vertanim.def (the original returns false either way).
    void LoadAnimations();                              // 0x00473ec0
    // The mission's Vertanim.bin (the original returns false either way).
    void LoadPlayers();                                 // 0x00474290

    // (+0x04 the C_Module part.)
    int m_started = 0;                                  // +0x0C
    std::vector<S_VertexAnimation *> m_animations;      // +0x10
    std::vector<S_VertexAnimPlayer *> m_players;        // +0x20
};
extern C_VertexAnimModule g_vertexAnimModule;          // 0x0089b474 (static init 0x00473d10)
// Ms gathered towards the module's next sorting of the players (Tick).
extern uint32_t g_vertexAnimSortTime;                   // 0x0089b4a8
