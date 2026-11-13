// I3D_projector: the texture projector visual (PROJ; vtable 0x1009c1f0, 0x270 bytes; 0x10043730 -
// 0x100462b0).
//
// In LS3DF it projects its texture (or its material's) along the frame's z onto the receiving
// meshes in its volume (x, y -1 - 1, z 0 - 1 in the frame's space; orthogonal or perspective),
// clipped to the volume and drawn blended, the color faded by the mode over the depth. LS3DRF's
// projector keeps only the parameters: nothing is projected or drawn.
//
// LS3DRF's projector is a 0x220-byte object (vtable 0x1003cfc8): LS3DF's fields up to projFlags,
// then receiverSum, and no projected geometry, sector or build stamp (LS3DF's +0x218 - +0x26B).
#pragma once

#include "core/layout.h"
#include "i3d/visual.h"

class I3D_material;
class ITexture;

// I3D_projector::projFlags.
enum : uint32_t {
    PROJF_BUFFERED = 0x00000001,            // LS3DF's geometry is in its vertex / index buffers (LS3DRF only clears it)
};

class I3D_projector : public I3D_visual {
public:
    // 0x4C 0x10043960: the frame; of another projector its parameters, of a mesh visual its
    // first face group's material (of a lens none).
    I3D_RESULT __stdcall Duplicate(I3D_frame *source) override;
    void Init() override;                                                      // 0x60 0x10043730
    void Uninit() override;                                                    // 0x64 0x100438f0
    // (MatrixChanged: the visual's, as in LS3DRF.)
    // 0x70 0x10046130: the frame, then orthogonal, the mode and the material.
    void Load() override;

    virtual void __stdcall SetTexture(ITexture *newTexture);                  // 0x94 0x10043af0
    virtual void __stdcall SetMaterial(I3D_material *newMaterial);            // 0x98 0x10043b20
    virtual void __stdcall SetOrthogonal(bool on);                           // 0x9C 0x10043b50
    virtual void __stdcall SetIntensity(float value);                        // 0xA0 0x10043c50 (at least 0)
    virtual void __stdcall SetOpacity(float value);                          // 0xA4 0x10043ca0 (0 - 1)
    virtual void __stdcall SetColor(const S_vector4 &newColor);               // 0xA8 0x10043b70 (0 - 1)
    virtual void __stdcall SetMode(int newMode);                             // 0xAC 0x10043d10
    virtual void __stdcall SetBuffered(uint32_t value);                      // 0xB0 0x10043d30
    // 0xB4 0x10045ab0: the geometry made now from the receivers below `root` (null: the scene's
    // primary sector, with flag 2 also its omni sector; flag 8: the projector's sector only), in
    // the buffers. Flags: 4 models are skipped, 8 other sectors are, 0x10 children are not walked.
    virtual void __stdcall Build(uint32_t flags, I3D_frame *root);

    bool orthogonal;                       // +0x1E0 (1)
    uint8_t padding1E1[3];
    float range;                            // +0x1E4 (100) drawn within range x the projection scale
    float intensity;                        // +0x1E8 (1)
    uint32_t unknown1EC;                    // +0x1EC
    S_vector4 color;                        // +0x1F0 (1, 1, 1)
    uint32_t buffered;                      // +0x200 0: the geometry in system memory (Build: 3)
    ITexture *texture;                      // +0x204 (a reference)
    I3D_material *material;                 // +0x208 (a reference; its texture when none)
    int mode;                               // +0x20C how the color fades over the depth (0, 4, 5, 8, 9)
    float opacity;                          // +0x210 (0.117) faces turned away are culled more the lower it is
    uint32_t projFlags;                     // +0x214 PROJF_*
    int receiverSum;                        // +0x218 Init and the setters clear it (LS3DF: +0x26C)
    uint32_t padding21C;                    // +0x21C (LS3DRF's pad to 0x220: nothing addresses it)
    // (No projected vertices, faces or buffers, blended center, build stamp, sector or receiver
    // count, LS3DF's +0x218 - +0x26B: LS3DRF has none.)
};
LS3D_LAYOUT32(sizeof(I3D_projector) == 0x220, "I3D_projector layout");
LS3D_LAYOUT32(offsetof(I3D_projector, texture) == 0x204, "I3D_projector layout");
LS3D_LAYOUT32(offsetof(I3D_projector, projFlags) == 0x214, "I3D_projector layout");
LS3D_LAYOUT32(offsetof(I3D_projector, receiverSum) == 0x218, "I3D_projector layout");
