// I3D_mirror: the mirror visual (MIRR; vtable 0x1009c138, 0x310 bytes; 0x10032280 - 0x10033fb0).
//
// A flat mesh (its own vertices and faces) that reflects the visuals in front of it: the
// renderer clears z to 0, draws the mirror's faces with z always written (so only they can be
// drawn over), then each reflected visual through a copy of its own (VISF_MIRRORED, `data` the
// original) placed at the original's position reflected through the mirror's plane.
//
// LS3DRF's mirror is only its plane, color and geometry (ray tests read them), with no render path:
// a 0x250-byte object (vtable 0x1003cf18), LS3DF's fields up to mirrorFlags (+0x240), then `faces`
// at +0x244. (No plane sphere, plane inverse, reflection, scene, copies, stamp or colored vertices,
// LS3DF's +0x244 - +0x304: LS3DRF has none.)
#pragma once

#include "i3d/vb_manager.h"
#include "i3d/visual.h"

// I3D_mirror::mirrorFlags.
enum : uint32_t {
    MIRF_PLANE = 0x00000001,                // the plane and geometry are set (Load, Duplicate, SetPlane)
    MIRF_REFLECTION_VALID = 0x00000004,     // LS3DF's reflection and plane sphere are up to date (LS3DRF only clears it)
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
    ls3d::S_IBAlloc faces;                  // +0x244 (LS3DF: +0x304)
};
static_assert(sizeof(I3D_mirror) == 0x250, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, color) == 0x220, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, range) == 0x230, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, numVertices) == 0x234, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, vertices) == 0x23C, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, mirrorFlags) == 0x240, "I3D_mirror layout");
static_assert(offsetof(I3D_mirror, faces) == 0x244, "I3D_mirror layout");
