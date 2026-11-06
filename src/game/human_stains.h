// The blood stains on a human's body (C_HumanWeaponPose::m_stains; detailed single player only; original source 0x00796890..0x0079ad10). The body's face
// groups get textures of their own, kept as ARGB 4444 pictures in memory; a hit finds the mesh's vertex nearest it, follows its UV coordinates and paints a
// splat there. The manager lends real textures to the eight bodies hit last. The same painting gives some soldiers camouflage paint and the head its overlay.
#pragma once

#include <cstdint>
#include <vector>

#include "ls3d/math.h"

class C_chunk;
class C_HumanWeaponPose;
class C_HumanStains;
class I3D_frame;
class I3D_mesh_level;
class I3D_singlemesh_visual;
class IImage;

constexpr int STAIN_GROUPS = 7;                         // the body's face groups: 0 head, 1..2 and 3..4 the sides, 5, 6
constexpr int STAIN_LENT_BODIES = 8;

struct S_StainEdge;
// A vertex of a face group, with the edges at it.
struct S_StainVertex {                                  // 0x14 bytes
    uint16_t index = 0;                                 // +0x00 in the mesh level
    std::vector<S_StainEdge *> edges;                   // +0x04
};
struct S_StainEdge {                                    // 0x0C bytes
    S_StainVertex *a;                                   // +0x00
    S_StainVertex *b;                                   // +0x04
    int16_t face;                                       // +0x08
    int16_t otherFace;                                  // +0x0A -1: none yet
};
// A face group's vertices and edges (coinciding vertices are joined by an edge too).
struct S_StainGroup {                                   // 0x128 bytes
    int index = -1;                                     // +0x000
    int numFaces = 0;                                   // +0x004
    int numVertices = 0;                                // +0x008
    std::vector<S_StainVertex *> vertices;              // +0x00C
    std::vector<S_StainEdge *> edges;                   // +0x01C
    // The positions in `vertices` of the extreme ones (+0x30, +0x34, +0x4C, +0x3C, +0x6C, +0xAC).
    unsigned minX = 0, maxX = 0, minZ = 0, maxZ = 0, minY = 0, maxY = 0;

    ~S_StainGroup();
};
// What the bodies of one model share.
struct S_StainMesh {                                    // 0x820 bytes
    S_StainGroup groups[STAIN_GROUPS];
    char *name = nullptr;                               // +0x818 the model's file
    int refCount = 0;                                   // +0x81C

    ~S_StainMesh() { delete[] name; }
};

// The stain textures shared by the humans (the singleton at 0x0098adf0; detailed single player only).
class C_StainManager {
public:
    C_StainManager() = default;                         // 0x007967b0
    ~C_StainManager();                                  // 0x00796830

    void Init(int unknown);                             // 0x00796890 (the callers pass 0)
    void Reset();                                       // 0x00796bf0 (Init(0) again)
    void Done();                                        // 0x00796c00

    S_StainMesh *AcquireMesh(I3D_frame *model);         // 0x00796d00
    void ReleaseMesh(S_StainMesh *mesh);                // 0x00796ff0
    // The textures `body` may use, taking them from the one hit longest ago if none is free.
    IImage **AcquireTextures(C_HumanStains *body);      // 0x007970f0
    void ReleaseTextures(const C_HumanStains *body);    // 0x007971b0

    IImage *NullTexture() const { return m_null; }
    IImage *HeadTexture() const { return m_head; }
    IImage *PaintTexture(int index) const { return m_paint[index]; }

private:
    bool m_initialized = false;                         // +0x000
    std::vector<S_StainMesh *> m_meshes;                // +0x004
    IImage *m_null = nullptr;                           // +0x014 stainNull
    IImage *m_head = nullptr;                           // +0x018 hshadow
    IImage *m_paint[8] = {};                            // +0x01C camouflage, camouflage2 with alpha A..D
    IImage *m_lent[STAIN_LENT_BODIES][STAIN_GROUPS] = {};   // +0x03C
    C_HumanStains *m_borrowers[STAIN_LENT_BODIES] = {};     // +0x11C
    uint16_t m_paintCounter = 0;                        // +0x18C
    friend class C_HumanStains;
};
extern C_StainManager g_stainManager;                   // 0x0098adf0

class C_HumanStains {
public:
    explicit C_HumanStains(C_HumanWeaponPose *pose);    // 0x00798660
    ~C_HumanStains();                                   // 0x007986b0
    C_HumanStains(const C_HumanStains &) = delete;
    C_HumanStains &operator=(const C_HumanStains &) = delete;

    // Binds the stains to the pose's model (`mode` of C_HumanWeaponPose::BindSkeleton).
    void Bind(char mode);                               // 0x00798720
    void SaveGame(C_chunk &chunk);                      // 0x00798930
    void LoadGame(C_chunk &chunk);                      // 0x00798bc0
    // Reads past the stains of a saved game (a body without them now).
    static void SkipSaved(C_chunk &chunk);              // 0x00798f70
    void SetVisible(bool on);                           // 0x007997b0
    // A hit at `pos` from `dir` (the zone bits of the hit).
    void AddHit(const S_vector &pos, const S_vector &dir, uint32_t flags);   // 0x00799bc0

    uint32_t LastHitTime() const { return m_lastHitTime; }
    // The manager wants its textures back (0x007996d0).
    void ReturnTextures();

private:
    void PrepareBuffers();                              // 0x007992a0
    void TakeTextures();                                // 0x007994e0
    void GiveBackTextures();                            // 0x007995d0
    void SetPaint(unsigned paint);                      // 0x007999a0
    void PutBaseTextures();                             // (the common part of 0x007995d0, 0x007996d0)
    IImage *BaseTexture(int group) const;
    void ShowBuffer0();                                 // (the common end of SetVisible, SetPaint)
    // (0x0079a010; the original also passes the vertices it does not use)
    void PaintHit(const S_StainVertex &vertex, const S_vector &point, const float *positions, int group);
    void PaintSplat(uint16_t *buffer, uint16_t *texture, int16_t x, int16_t y, const uint16_t *image, uint16_t width, uint16_t height);   // 0x0079a880

    C_HumanWeaponPose *m_pose;                          // +0x00
    I3D_singlemesh_visual *m_visual = nullptr;          // +0x04 the model's "a"
    I3D_mesh_level *m_level = nullptr;                  // +0x08
    S_StainMesh *m_mesh = nullptr;                      // +0x0C
    uint8_t m_painted[STAIN_GROUPS] = {};               // +0x10
    uint16_t *m_buffers[STAIN_GROUPS] = {};             // +0x18
    uint16_t *m_clean = nullptr;                        // +0x34 group 0 with the hits but no overlay
    bool m_ownTextures = false;                         // +0x38
    IImage *m_textures[STAIN_GROUPS] = {};              // +0x3C lent by the manager
    bool m_visible = false;                             // +0x58 the overlay on the head
    uint8_t m_paint = 0;                                // +0x59 camouflage paint, 0: none
    int m_width = 0;                                    // +0x5C
    int m_height = 0;                                   // +0x60
    int m_maskX = 0;                                    // +0x64
    int m_maskY = 0;                                    // +0x68
    int m_shiftX = 0;                                   // +0x6C
    int m_shiftY = 0;                                   // +0x70
    uint32_t m_lastHitTime = 0;                         // +0x74
};
