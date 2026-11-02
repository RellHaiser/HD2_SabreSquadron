// I3D_mirror: the mirror visual (MIRR; vtable 0x1009c138, 0x310 bytes; 0x10032280 - 0x10033fb0).
//
// A flat mesh (its own vertices and faces) that reflects the visuals in front of it: the
// renderer clears z to 0, draws the mirror's faces with z always written (so only they can be
// drawn over), then each reflected visual through a copy of its own (VISF_MIRRORED, `data` the
// original) placed at the original's position reflected through the mirror's plane.
//
// LS3DRF's mirror is only its plane, color and geometry (ray tests read them), with no render path.
// Its object is 0x250 bytes: the fields up to mirrorFlags (+0x240) as here, then `faces` at +0x244;
// the ones from planeRadius2 to colorVertices stay for the LS3DF layout.
#pragma once

#include "core/c_vector.h"
#include "i3d/vb_manager.h"
#include "i3d/visual.h"

class I3D_scene;

// I3D_mirror::mirrorFlags.
enum : uint32_t {
    MIRF_PLANE = 0x00000001,                // the plane and geometry are set (Load, Duplicate, SetPlane)
    MIRF_REFLECTION_VALID = 0x00000004,     // reflection and the plane's sphere are up to date
    MIRF_VISIBLE = 0x00000100,              // (renderer) seen this frame
    MIRF_REFLECTS = 0x00000200,             // ... and its reflection is drawn
};

class I3D_mirror : public I3D_visual {
public:
    // (Tick, LinkTo, Duplicate and MatrixChanged: the frame's and the visual's, as in LS3DRF.)
    void Init() override;                                                      // 0x60 0x10032280
    // 0x64 0x10032330: the geometry released.
    void Uninit() override;
    // 0x70 0x10033e80: the frame, then the bound, plane, color, range and geometry.
    void Load() override;
    // 0x94 0x10033d50: the plane matrix: `matrix` scaled by the box's half size, at its center.
    virtual void __stdcall SetPlane(const I3D_bbox &box, const S_matrix &matrix);
    // 0x98 0x100323c0: the color (clamped to 0 - 1); true when unchanged.
    virtual bool __stdcall SetColor(const S_vector4 &newColor);
    // 0x9C 0x10032520: the faces (the index memory; `flags` unread); null without faces.
    virtual I3D_triface *__stdcall LockFaces(uint32_t flags);
    // (The vtable ends here: the four entries after it, LS3DF's 0x1009c1e0, are I3D_visual_morph's.)

    S_matrix planeMatrix;                   // +0x1E0 the plane in the frame's space (its z the normal)
    S_vector4 color;                        // +0x220 the surface's color (0)
    float range;                            // +0x230 (Load) reflects within range x the LOD scale
    int numVertices;                        // +0x234
    int numFaces;                           // +0x238
    S_vector4 *vertices;                    // +0x23C 16-byte aligned
    uint32_t mirrorFlags;                   // +0x240 MIRF_*
    float planeRadius2;                     // +0x244 planeRadius squared
    uint32_t unknown248[2];                 // +0x248
    S_vector planeCenter;                   // +0x250 world space
    float planeRadius;                      // +0x25C the plane's sphere: what it reflects must touch it
    S_matrix planeInverse;                  // +0x260 world space into the plane's (-1 - 1 within it)
    S_matrix reflection;                    // +0x2A0 world space reflection through the mirror plane
    I3D_scene *scene;                       // +0x2E0 (not initialised; nothing sets it)
    ls3d::C_vector<I3D_visual *> copies;    // +0x2E4 (grow 0x20) LS3DF's mirrored copies (LS3DRF: none)
    uint16_t stamp;                         // +0x2F4 (not initialised) mirrored lights follow it
    uint16_t padding2F6;
    ls3d::S_VBAlloc colorVertices;          // +0x2F8 (not initialised) LS3DF's position and color (LS3DRF: none)
    ls3d::S_IBAlloc faces;                  // +0x304
};
static_assert(sizeof(I3D_mirror) == 0x310, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, numVertices) == 0x234, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, planeCenter) == 0x250, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, reflection) == 0x2A0, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, copies) == 0x2E4, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, stamp) == 0x2F4, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, faces) == 0x304, "I3D_mirror layout");
