// The volumetrics (game/volumetrics.cpp, original source 0x00480390..0x00482e50): light shafts on the
// mission's lights from volumetrics.bin, each a few cones (or flat fans) of a texture turned to the
// camera, faded in and out with the distance, their textures turning as the viewer goes round
// them; and the clouds (0x00483de0..0x00483e50 and the drawing 0x00482e50): billboards in a
// lattice of 16 m cells around the camera, two textures. The GEffects module owns the volumetrics
// (+0xC4) and draws both.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "ls3d/math.h"

class C_chunk;
class I3D_frame;
class IImage;

// A vertex as IGraph::DrawPrimitives takes it here (0x18 bytes).
struct S_LitVertex {
    S_vector pos;
    uint32_t color;                                     // alpha << 24 | white
    float u, v;
};

// A cone or flat fan of a shaft (0x3E0 bytes).
struct S_VolumetricCone {
    static constexpr int MAX_SEGMENTS = 10;             // (the room for them)

    explicit S_VolumetricCone(float base);              // 0x00481080
    ~S_VolumetricCone();                                // 0x00480d00
    S_VolumetricCone(const S_VolumetricCone &) = delete;
    S_VolumetricCone &operator=(const S_VolumetricCone &) = delete;
    // From its chunk of volumetrics.bin (subchunks 0x3F2..0x460); true.
    bool Load(C_chunk &chunk);                          // 0x00481440
    // The vertices made for `base` (the shaft's z offset); `load`: the texture loaded again.
    void Build(bool load, float base);                  // 0x00480740
    // Drawn: a flat fan's texture scrolled by the time, a cone's turned by `phase`; `fade` (not
    // under 0) the vertices' alpha.
    void Render(int time, float phase, float fade);     // 0x00480390

    C_str texture;                                      // +0x000 its file (".bmp" without an extension)
    float angle = 0.7853982f;                           // +0x004 degrees: the fan's width / the cone's spread
    float length = 2.0f;                                // +0x008
    float alphaNear = 0.5f;                             // +0x00C at the apex
    float alphaFar = 0.5f;                              // +0x010 at the rim
    float base;                                         // +0x014 the time / phase offset
    bool flat = false;                                  // +0x018 one fan, not a cone
    IImage *image = nullptr;                            // +0x01C
    S_LitVertex fan[3];                                 // +0x020 apex, rim right, rim left
    float fanUV[3][2];                                  // +0x068 their texture coordinates at rest
    S_LitVertex cone[MAX_SEGMENTS][3];                  // +0x080 per segment: apex, two rim points
    float alphas[MAX_SEGMENTS][3];                      // +0x350 per vertex (the fan: the first 3)
    float rotate = 0.1f;                                // +0x3C8 the cone's texture turn per phase
    int segments = 3;                                   // +0x3CC
    float spread = 3.1415927f;                          // +0x3D0 degrees the segments cover
    float scrollU = 0.0f;                               // +0x3D4 the fan's texture per s
    float scrollV = 0.0f;                               // +0x3D8
    float uvScale = 1.0f;                               // +0x3DC the fan's
};

// A light shaft (0x48 bytes).
struct S_Volumetric {
    explicit S_Volumetric(I3D_frame *frame);            // 0x00481320
    ~S_Volumetric();                                    // 0x00481380
    S_Volumetric(const S_Volumetric &) = delete;
    S_Volumetric &operator=(const S_Volumetric &) = delete;
    // The cones deleted.
    void DeleteCones();                                 // 0x004813d0
    // From its chunk of volumetrics.bin: the light's name (chunk 10, its frame), the distances,
    // the cones (chunk 0x28); true (a light not found ends it).
    bool Load(C_chunk &chunk);                          // 0x00481810
    // Its distance from the origin; the cones built (`load`: their textures loaded, only those
    // with a name).
    void Refresh(bool load);                            // 0x00480fa0
    // Turned to the camera at `camera` and drawn with the fades of its distance; false when not
    // drawn. `yaw`: the camera's turn so far.
    bool Draw(int time, const S_vector &camera, float yaw);   // 0x00480d80

    I3D_frame *frame;                                   // +0x00 the light
    std::vector<S_VolumetricCone *> cones;              // +0x04
    float distance = 0.0f;                              // +0x14 from the camera (xz)
    float depthDistance = 10.0f;                        // +0x18 drawn with render state 0xD beyond it
    float fadeInEnd = 0.0f;                             // +0x1C fully in from here (0: no fade in)
    float fadeInStart = 0.0f;                           // +0x20 nothing nearer
    bool visible = false;                               // +0x24 this frame
    float distanceTurn = 0.0f;                          // +0x28 the phase per m of distance
    float viewTurn = 0.0f;                              // +0x2C the phase per radian the viewer went round
    float fadeOutEnd = 30.0f;                           // +0x30 nothing beyond
    float fadeOutStart = 25.0f;                         // +0x34
    float base = 0.0f;                                  // +0x38 the cones' z offset
    S_vector2 viewDir = {0.0f, 0.0f};                   // +0x3C the camera's direction to it (xz) last
    float viewAngle;                                    // +0x44 the turn of it so far
};

class C_Volumetrics {                                   // 0x20 bytes
public:
    // (Both inlined in the GEffects module's constructor and destructor; quirk kept: the shafts
    // are not deleted with it, only the list.)
    C_Volumetrics() = default;
    ~C_Volumetrics() = default;
    C_Volumetrics(const C_Volumetrics &) = delete;
    C_Volumetrics &operator=(const C_Volumetrics &) = delete;

    // The clouds, then the visible shafts, farthest first. `time`: ms since the mission's start.
    void Render(int time);                              // 0x00482260
    // The mission's volumetrics.bin (then Prune(true)).
    void Load();                                        // 0x00482a00
    // The shafts whose light is gone deleted, the others refreshed (`load`: their textures).
    void Prune(bool load);                              // 0x00482980
    // Everything deleted (the clouds' textures too).
    void Free();                                        // 0x00482d70

private:
    std::vector<S_Volumetric *> m_shafts;               // +0x04
    float m_yaw = 0.0f;                                 // +0x10 the camera's turn so far
    float m_unknown14 = 0.0f;                           // +0x14
    S_vector2 m_viewDir = {0.0f, 0.0f};                 // +0x18 the camera's last direction (xz; Free: 1, 0)

    friend struct S_VolumetricsTest;
};

// The clouds (0x0089b74c): their two textures; the vertex buffers (0x0089b754, 0x0089b758).
struct S_Clouds {
    IImage *images[2] = {nullptr, nullptr};
};
extern S_Clouds g_clouds;                               // 0x0089b74c (static init 0x00482df0)
extern S_LitVertex *g_cloudVertices[2];                 // 0x0089b754, 0x0089b758 (0x3CC00 bytes each)
void FreeClouds();                                      // 0x00483de0
void LoadClouds();                                      // 0x00483e50
// A billboard per 16 m cell of the 12 cells each way around the camera, in front of it, faded by
// the distance; the first texture where the cell's sine is
// positive, the second elsewhere. (Faded in from 10 m to 20 m, out from 45 m to about 71 m.) `time`
// is not used.
void DrawClouds(int time);                              // 0x00482e50
