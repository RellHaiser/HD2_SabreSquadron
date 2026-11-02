// I3D_object: the mesh visual (OBJ_; vtable 0x1009c7c8, 0x210 bytes; 0x10035fc0 - 0x10038410)
// and the base of the other mesh visuals (morph, billboard, single mesh, single morph; the
// lightmap visual shares its methods).
//
// An object draws one level of its mesh, chosen by the camera distance (the levels' lodDistance;
// the last level fades out with g_lodFadeScale), each face group a queued item with its material's
// textures. With VISF_TEXTURE_LOD the levels share level 0's geometry and the chosen level gives
// the texture coordinates and materials.
// A mirror's copy (VISF_MIRRORED) draws the visual it reflects (`data`) with mirrored lights.
#pragma once

#include "i3d/render_helpers.h"
#include "i3d/visual.h"

class I3D_face_group;
class I3D_mesh;
class I3D_mesh_level;
class I3D_material;

class I3D_object : public I3D_visual {
public:
    // 0x4C 0x100361e0: the frame, and of a mesh visual (or a lens: no mesh) the mesh (shared),
    // the light level, alpha and bounds.
    I3D_RESULT __stdcall Duplicate(I3D_frame *source) override;
    // 0x60 / 0x64 0x10035fc0 / 0x10036020
    void Init() override;
    void Uninit() override;
    // 0x70 0x10038410: the frame, then the mesh.
    void Load() override;
    // 0x7C 0x10036350: the mesh copied (DuplicateMesh) and its vertices baked into world space,
    // or back into the frame's space.
    void __stdcall SetWorldSpace(bool on) override;
    // 0x9C 0x10036040: the mesh (a reference; the local bound follows unless in world space).
    virtual I3D_RESULT __stdcall SetMesh(I3D_mesh *newMesh, uint32_t flags);
    // 0xA0 0x10036160: a mesh shared with others replaced by a copy of its own.
    virtual I3D_RESULT __stdcall DuplicateMesh();
    virtual void __stdcall SetLightLevel(float level);                  // 0xA4 0x100368d0
    virtual void __stdcall SetAlpha(float value);                       // 0xA8 0x100368f0
    // 0xAC / 0xB0 0x10036960 / 0x10036910: a level's / every level's render vertices refilled
    // when next drawn.
    virtual void __stdcall RefreshLevel(int level);
    virtual void __stdcall RefreshLevels();

    I3D_mesh *mesh;                         // +0x1E0
    int textureLevel;                       // +0x1E4 the level drawn (VISF_TEXTURE_LOD: whose UVs and materials)
    float lightLevel;                       // +0x1E8
    float alpha;                            // +0x1EC
    float lodFade;                          // +0x1F0 the last level's fade out, else 1
    ls3d::i3d::S_visualLights lights;       // +0x1F4
    uint32_t padding20C;
};
static_assert(sizeof(I3D_object) == 0x210, "I3D_object layout");
static_assert(offsetof(I3D_object, mesh) == 0x1E0, "I3D_object layout");
static_assert(offsetof(I3D_object, lights) == 0x1F4, "I3D_object layout");

namespace ls3d::i3d {

// The last level's fade-out range (0x100adf30; set by the driver's states).
extern float g_lodFadeScale;

}  // namespace ls3d::i3d
