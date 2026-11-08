// I3D_driver: the engine's root object (vtable 0x1009b640; one global instance, I3DGetDriver).
// Methods 0x10015100 - 0x1001b8f0 (the frame factories, the name tree, hierarchy services) plus
// the device reset (0x10063560). (No Render 0x1005edc0: LS3DRF's driver draws nothing.)
//
// LS3DRF's driver is a 0x2c-byte object (vtable 0x1003c470): LS3DF's fields without the
// envSectors and lightmapTextures vectors (LS3DF's +0x18 / +0x28), so scenes and mirrorStamp sit
// 0x20 lower.
#pragma once

#include <cstring>

#include "core/c_vector.h"
#include "i3d/i3d.h"

class I3D_scene;

// I3D_driver::EnumVisualTypes callback: a visual fourCC and its display name.
typedef void __stdcall I3D_VISUALTYPEPROC(uint32_t fourCC, const char *name);

class I3D_driver {
public:
    I3D_driver();                                                       // 0x100151c0
    ~I3D_driver();                                                      // 0x10015210

    virtual I3D_RESULT __stdcall Init(const void *init);                 // 0x00 0x100152f0
    virtual void __stdcall Close();                                     // 0x04 0x10015650
    virtual void __stdcall FreeUnusedResources();                       // 0x08 0x10015560
    virtual void __stdcall Tick(int time);                              // 0x0C 0x100152c0
    virtual int __stdcall GetTime();                                    // 0x10 0x100152e0
    virtual I3D_RESULT __stdcall GetStats(int index, void *out);        // 0x14 0x100156f0
    virtual I3D_RESULT __stdcall SetState(int state, uint32_t value);   // 0x18 0x10015770
    virtual uint32_t __stdcall GetState(int state);                     // 0x1C 0x10015c20
    virtual I3D_frame *__stdcall CreateVisual(uint32_t fourCC);         // 0x20 0x10019e30
    virtual I3D_frame *__stdcall CreateFrame(int type);                 // 0x24 0x10019e10
    virtual I3D_material *__stdcall CreateMaterial();                   // 0x28 0x10018ae0
    virtual I3D_anim_material *__stdcall CreateAnimMaterial();          // 0x2C 0x10018b60
    // 0x30 0x10018be0: room for `count` faces (operator new).
    virtual I3D_triface *__stdcall CreateTrifaceArray(int count);
    virtual I3D_face_group *__stdcall CreateFaceGroup();                // 0x34 0x10018c00
    virtual I3D_mesh_level *__stdcall CreateMeshLevel();                // 0x38 0x10018c60
    virtual I3D_mesh *__stdcall CreateMesh();                           // 0x3C 0x10018cc0
    virtual I3D_animation_set *__stdcall CreateAnimationSet();          // 0x40 0x10018d20
    virtual I3D_pose *__stdcall CreatePose();                           // 0x44 0x10018d80
    virtual I3D_pose_set *__stdcall CreatePoseSet();                    // 0x48 0x10018de0
    virtual const char *__stdcall GetFrameTypeName(int type);           // 0x4C 0x10019f00
    virtual const char *__stdcall GetVisualTypeName(uint32_t fourCC);   // 0x50 0x1001a000
    // 0x54 0x10019e50: the visual types and their display names, to `proc` (the shadow only with
    // `withShadow`).
    virtual I3D_RESULT __stdcall EnumVisualTypes(I3D_VISUALTYPEPROC *proc, bool withShadow);
    // 0x58 0x1001a0f0: the frame named `name` (in `scene`'s sectors; any frame when null).
    virtual I3D_frame *__stdcall FindFrame(I3D_scene *scene, const char *name);
    // 0x5C 0x1001a310: rebuilds the name tree balanced (every 0x800 insertions).
    virtual void __stdcall RebalanceNameTree();
    // 0x60 0x1001a5a0: every named frame, in name order, until the callback returns nonzero
    // (then I3DERR_CANCELED).
    virtual I3D_RESULT __stdcall EnumNamedFrames(I3D_ENUMPROC *proc, void *context);
    // 0x64 0x1001a670: tells the scenes (from the last) that a frame they know goes away, until one
    // takes it (scene slot 0xC8 returns 0).
    virtual void __stdcall FrameReleased(I3D_frame *frame);
    // 0x68 0x1001a6b0: does nothing (I3D_frame::Uninit takes a caster off the shadows' lists).
    virtual void __stdcall RemoveShadowCaster(I3D_frame *frame);
    // 0x6C 0x1001a6c0: removes a frame from the targets' frame lists.
    virtual void __stdcall RemoveTargetFrame(I3D_frame *frame);
    // 0x70 / 0x74 0x1001a6d0 / 0x1001a780: I3D_frame::SetHierarchyLightLevel / Alpha of a frame.
    virtual void __stdcall SetHierarchyLightLevel(I3D_frame *frame, float level);
    virtual void __stdcall SetHierarchyAlpha(I3D_frame *frame, float alpha);
    // 0x78 0x1001a830: box of a frame and its hierarchy in the frame's space, each child's box
    // transformed as a whole (a sector: its own box).
    virtual I3D_bbox *__stdcall GetHierarchyBound(I3D_bbox &out, I3D_frame *frame);
    // 0x7C 0x1001ae10: the same box made of every visual's own box transformed to the frame.
    virtual I3D_bbox *__stdcall GetHierarchyBBox(I3D_bbox &out, I3D_frame *frame);
    // 0x80 0x10063560: does nothing (no device; no return value).
    virtual void __stdcall ResetDevice(I3D_scene *) {}

    // 0x10087b10: does nothing (after the buffer managers, lightmaps and shadows make or free
    // device resources).
    void __stdcall SlotB4() {}

    uint32_t unknown04;                                                 // +0x04
    ls3d::C_vector<I3D_frame *> soundFrames;                            // +0x08
    // (No envSectors or lightmapTextures vectors, LS3DF's +0x18 / +0x28: LS3DRF has none.)
    ls3d::C_vector<I3D_scene *> scenes;                                 // +0x18 (LS3DF: +0x38)
    int16_t mirrorStamp;                                                // +0x28 (LS3DF: +0x48) nothing counts: mirrors keep no stamp
};
static_assert(sizeof(I3D_driver) == 0x2C, "I3D_driver layout");
static_assert(offsetof(I3D_driver, unknown04) == 0x04, "I3D_driver layout");
static_assert(offsetof(I3D_driver, soundFrames) == 0x08, "I3D_driver layout");
static_assert(offsetof(I3D_driver, scenes) == 0x18, "I3D_driver layout");
static_assert(offsetof(I3D_driver, mirrorStamp) == 0x28, "I3D_driver layout");

namespace ls3d::i3d {

// The engine's driver object (0x100d16f8; I3DGetDriver).
extern I3D_driver *g_driver;

// Driver states (SetState / GetState, 0x100be058).
enum : int {
    DRVSTATE_ZBUFFER = 0x03,            // z test (D3DRS_ZENABLE; Render sets it per camera: orthogonal z, else the w-buffer choice)
    DRVSTATE_WBUFFER = 0x04,            // w-buffer instead of z
    DRVSTATE_CLEAR = 0x05,              // Render clears the target
    DRVSTATE_CLEAR_Z = 0x06,            // ... and z (+ stencil)
    DRVSTATE_TEX_MIPMAP = 0x0D,         // material textures: mip maps where their flags allow
    DRVSTATE_TEX_DITHER = 0x10,         // ... dithered
    DRVSTATE_TEX_TRUECOLOR = 0x11,      // ... 32-bit
    DRVSTATE_DRAW_VISUALS = 0x12,       // the sectors' visuals are drawn
    DRVSTATE_FOG = 0x13,                // the sectors' fog lights apply
    DRVSTATE_LOD_SCALE = 0x14,          // (a float) LOD distance scale
    DRVSTATE_TEX_HALF = 0x15,           // ... at half size
    DRVSTATE_TEX_COMPRESS = 0x19,       // ... DXT compressed
    DRVSTATE_OCCLUSION = 0x27,          // occluders hide sectors' visuals and portals (Init sets it, as LS3DRF's; unread: no occlusion)
    DRVSTATE_LM_SAMPLES = 0x28,         // Construct: light samples per lightmap texel, along each axis
    DRVSTATE_SHADOWS = 0x2B,            // shadows are drawn
    DRVSTATE_LM_TRUECOLOR = 0x22,       // lightmap atlas textures 32-bit (when the device has it)
    DRVSTATE_LM_DITHER = 0x23,          // ... else dithered into 16-bit
    DRVSTATE_FRAME_NAMES = 0x31,        // named frames are entered in the name tree
    DRVSTATE_PROJECTORS = 0x33,         // projectors are drawn
    DRVSTATE_MIRRORS = 0x3B,            // mirrors draw their reflections
    DRVSTATE_PROJECTOR_OFFSET = 0x3C,   // (a float) the projected polygons' lift off their surfaces
    DRVSTATE_ENV_UV_SCALE = 0x3E,       // (a float, 1.0) scale of the generated environment texture coordinates
    DRVSTATE_SORT_BLENDED = 0x3F,       // the blended items are sorted by overlap (not only depth)
    DRVSTATE_NEAR_LOD_SCALE = 0x41,     // (a float) the near sector's LOD distance scale
    DRVSTATE_COUNT = 0x45,
};
extern uint32_t g_driverStates[DRVSTATE_COUNT];
// The projectors' z bias (driver state 0x36; 0x100be018).
extern int g_projectorZBias;
// Set by Init, cleared by Close (0x100be401).
extern bool g_driverInitialized;
// The driver's time in ms (0x100be394; Tick advances it).
extern int g_driverTime;
inline bool NamesEnabled() { return g_driverStates[DRVSTATE_FRAME_NAMES] != 0; }
// A state that holds a float's bits.
inline float DriverStateFloat(int state)
{
    float f;
    std::memcpy(&f, &g_driverStates[state], 4);
    return f;
}

}  // namespace ls3d::i3d
