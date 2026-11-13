// I3D_scene: the root of a rendered world (FRAME_SCENE; vtable 0x1009b798, 0x450 bytes; methods
// 0x100462b0 - 0x1004c730, collision 0x1000c7a0 - 0x10015050; no weather update or drawing).
//
// A scene owns four special sectors that are never inside another: the primary sector (the space
// outside every other sector), the backdrop (drawn behind everything with its own projection), the
// "omni" sector (shots, shells and hit zones live there: it is drawn wherever the camera is) and the
// near sector (drawn with the camera's near projection). The frames added to it (AddFrame) are
// its children by ownership; models and emitters it ticks (AddTickFrame). It also keeps the
// sound listener and its EAX environment (from the camera's sector), an animation stack, fog,
// the weather particles, the drawn dummy boxes and frames tracked between two sectors.
//
// LS3DRF's scene is a 0x420-byte object (vtable 0x1003c588): LS3DF's fields without the shadows
// and mirrors vectors (LS3DF's +0x138 / +0x148), the listener (+0x28C) and the lens flares
// (+0x290), with a pad at +0x29C that keeps weatherWind 16-byte aligned. From animStack on the
// fields sit lower than LS3DF's: by 0x20 up to areasChanged, 0x34 from weatherFlags to
// weatherParam298, 0x30 from weatherWind on.
#pragma once

#include "core/c_vector.h"
#include "i3d/collision.h"
#include "i3d/frame.h"
#include "i3d/sector.h"
#include "i3d/vb_manager.h"

class I3D_anim_stack;
class I3D_animation_set;
class I3D_area;
class I3D_camera;
class I3D_occluder;
class I3D_portal;
class ITexture;

// A frame kept in whichever sector of two reference frames (AddTrackedModel: a human model and
// two of its frames) it is better in.
struct S_trackedModel {
    I3D_frame *frame;                       // +0x00
    I3D_frame *refA;                        // +0x04
    I3D_sector *sectorA;                    // +0x08 refA's sector (a reference)
    float distanceA;                        // +0x0C how deep refA is in it (-1e16: not located)
    I3D_frame *refB;                        // +0x10
    I3D_sector *sectorB;                    // +0x14
    float distanceB;                        // +0x18
};
static_assert(sizeof(S_trackedModel) == 0x1C, "S_trackedModel layout");

// A drawn dummy's box as three planes and a sphere (AddDrawnDummy, 0x10049000).
struct S_dummyBox {
    I3D_frame *frame;                       // +0x00
    int changeCount;                        // +0x04 the frame's changeCount when it was taken
    uint32_t padding08[2];
    S_plane planes[3];                      // +0x10 three faces through the corner
    S_vector center;                        // +0x40
    float radiusSquare;                     // +0x4C
    float extents[3];                       // +0x50 the box's edge lengths
    uint32_t padding5C;
};
static_assert(sizeof(S_dummyBox) == 0x60, "S_dummyBox layout");

// Scene weather flags (I3D_scene::weatherFlags).
enum : uint32_t {
    WEATHER_ON = 0x001,                     // param 0
    WEATHER_PARAM1 = 0x002,
    WEATHER_PARAM2 = 0x004,
    WEATHER_SNOW = 0x040,                   // param 14: snow (the snow texture) instead of rain
    WEATHER_RESHAPE = 0x080,                // the particle shapes need rebuilding
    WEATHER_RECOLOR = 0x100,                // the particle colours need rebuilding
    WEATHER_MODE1 = 0x200,                  // param 15: 1
    WEATHER_MODE2 = 0x400,                  //           2
};

// Frame flags the scene uses.
enum : uint32_t {
    FRMFLAGS_TRACKED_STALE = 0x00020000,    // a tracked model whose sectors need locating again
};

class I3D_scene : public I3D_frame {
public:
    // (Frame slots the scene does not support.)
    // LS3DRF 0x10011b40 (from LS3DF 0x1001ba60): one empty stub for SetDir and SetCallback (also
    // a projector slot).
    void __stdcall SetDir(const S_vector &, float) override {}           // 0x1C 0x1001ba60
    void __stdcall Tick(int time) override;                             // 0x20 0x10047910
    // LS3DRF 0x10011a30 (from LS3DF 0x1002b0a0): an empty stub (also material 0x44 / 0x48).
    void __stdcall Update() override {}                                 // 0x24 0x1002b0a0
    void __stdcall SetCallback(I3D_CALLBACK *, uint32_t) override {}     // 0x28 0x1001ba60
    // LS3DRF 0x10011a40 (from LS3DF 0x1001ba70): one return-0 stub for SetName and Duplicate.
    I3D_RESULT __stdcall SetName(const char *) override { return I3D_OK; }    // 0x34 0x1001ba70
    // LS3DRF 0x10011a50 (from LS3DF 0x1001ba80)
    I3D_RESULT __stdcall LinkTo(I3D_frame *, uint32_t) override { return I3DERR_UNSUPPORTED; }  // 0x38 0x1001ba80
    // 0x44 0x10046880: the primary sector's frames, then those of the special sectors the flags
    // ask for (ENUMF_BACKDROP / ENUMF_OMNI / ENUMF_NEAR).
    I3D_RESULT __stdcall EnumFrames(I3D_ENUMPROC *proc, void *context, uint32_t flags, const char *mask) override;
    I3D_RESULT __stdcall Duplicate(I3D_frame *) override { return I3D_OK; }   // 0x4C 0x1001ba70
    // LS3DRF 0x1000fa10 (from LS3DF 0x10019470): the scalar deleting destructor.
    ~I3D_scene() override;                                              // 0x5C 0x10019470 / 0x100195d0
    void Init() override;                                               // 0x60 0x100462b0
    void Uninit() override;                                             // 0x64 0x100466c0

    // 0x74 0x10046820: a function called while rendering (flags: when); returns the old one.
    virtual void *__stdcall SetRenderCallback(void *function, uint32_t flags);
    // (No Render, LS3DF's slot 0x78 0x10046860: LS3DRF has none.)
    // 0x78 0x10046920: the frame named `name` (the special sectors as the flags ask).
    virtual I3D_frame *__stdcall FindFrame(const char *frameName, uint32_t flags);
    // 0x7C 0x100469a0: the scene owns the frame (a reference); emitters are ticked.
    virtual void __stdcall AddFrame(I3D_frame *frame);
    virtual I3D_RESULT __stdcall RemoveFrame(I3D_frame *frame);         // 0x80 0x10046b30
    // 0x84 0x10046c50: the colour of every light of the primary sector.
    virtual void __stdcall SetLightsColor(const S_vector &color);
    // 0x88 0x10046c90: brightness (0 - 1) of the lights on `pos` (in `sector`, null: found);
    // flags bit 0: lights hidden by geometry do not count (bit 6: the rays skip I3DCOL_SKIP_FLAGGED
    // visuals), `excluded` not tested by the rays.
    virtual float __stdcall GetLightIntensity(const S_vector &pos, I3D_sector *sector, uint32_t flags,
                                              I3D_frame *excluded);
    virtual void __stdcall SetActiveCamera(I3D_camera *camera);         // 0x8C 0x100471a0
    // 0x90 0x10047250: the ray from the camera through the pixel (x, y).
    virtual I3D_RESULT __stdcall UnmapScreenPoint(int x, int y, S_vector4 &origin, S_vector4 &dir);
    // 0x94 0x100476a0: world points to the screen (x, y in pixels, z, w = 1 / clip w).
    virtual I3D_RESULT __stdcall TransformPoints(const S_vector4 *points, S_vector4 *out, int count);
    // 0x98 0x1004bd90: the frame's bound against the camera's frustum.
    virtual bool __stdcall IsFrameVisible(I3D_frame *frame, I3D_camera *camera);
    // 0x9C - 0xC0 0x10048b80 - 0x10048e40: the scene's animation stack (as I3D_model's).
    virtual I3D_RESULT __stdcall SetAnimation(I3D_animation_set *set, int stage, float weight, int mode);
    virtual I3D_animation_set *__stdcall GetAnimation(int stage);
    virtual void __stdcall StopAnimation(int stage);
    virtual I3D_RESULT __stdcall SetAnimWeight(int stage, float weight);
    virtual float __stdcall GetAnimWeight(int stage);
    virtual I3D_RESULT __stdcall SetAnimTime(int stage, int time);
    virtual int __stdcall GetAnimTime(int stage);
    virtual bool __stdcall IsAnimActive(int stage);
    virtual int __stdcall NumTracks();
    virtual void *__stdcall GetTracks();
    // 0xC4 / 0xC8 0x100477d0 / 0x10047880: frames ticked with the scene (FRMFLAGS_SCENE_SOUND).
    virtual void __stdcall AddTickFrame(I3D_frame *frame);
    virtual I3D_RESULT __stdcall DeleteTickFrame(I3D_frame *frame);
    virtual I3D_sector *__stdcall CreateSector();                       // 0xCC 0x100479a0
    // 0xD0 0x10047c20: the last of `sectors` that holds `pos`, else the primary sector.
    virtual I3D_sector *__stdcall LocateSectorIn(const S_vector &pos, I3D_sector **sectors, int count);
    // 0xD4 0x10047b70: the sector holding `pos` (`hint` first) and how deep (InsideDistance).
    virtual I3D_sector *__stdcall LocateSectorDepth(const S_vector &pos, I3D_sector *hint, float *depth);
    // 0xD8 0x10047af0: the sector holding `pos` (`hint` first), else the primary sector.
    virtual I3D_sector *__stdcall LocateSector(const S_vector &pos, I3D_sector *hint);
    virtual I3D_sector *__stdcall GetSector(const S_vector &pos);       // 0xDC 0x10047a90
    // 0xE0 0x10048270: the backdrop's range (ignored unless both are 0.001+ and differ by 0.01+).
    virtual void __stdcall SetBackdropRange(float nearRange, float farRange);
    // 0xE4 0x10047cf0: UpdateFrameSector when there is no active camera.
    virtual void __stdcall UpdateFrameSectorIdle(I3D_frame *frame, const S_vector &pos, int unused);
    // 0xE8 0x10047c70: links a moved frame to the sector it is now in.
    virtual void __stdcall UpdateFrameSector(I3D_frame *frame, const S_vector &pos);
    // 0xEC 0x1004be10: the sectors and portals again after any of them changed.
    virtual void __stdcall UpdateSectors();
    // 0xF0 0x10047d20: loads a scene file (the loader) into the primary sector.
    virtual I3D_RESULT __stdcall Open(const char *fileName, uint32_t flags, void *loadCallback, void *context);
    virtual void __stdcall Close();                                     // 0xF4 0x10047db0
    // 0xF8 - 0x110: collision (collision_ray.cpp, collision_sphere.cpp; I3DCOL_* flags).
    // 0xF8 0x10010c20: the nearest hit of the ray, its point and world normal; every frame tested
    // at `lodLevel`.
    virtual I3D_frame *__stdcall PickFrame(const S_vector &from, const S_vector &dir, uint32_t flags,
                                           S_vector4 *outPoint, S_vector4 *outNormal, I3D_COLLISION_HIT *hitCb,
                                           void *user, I3D_COLLISION_FILTER *filter, int lodLevel);
    // 0xFC 0x1000c7a0: the nearest hit of the ray (its length the direction's), its distance and
    // world normal; `frame` skipped, excluded or tested at `lodLevel` by the flags.
    virtual I3D_frame *__stdcall TestRay(const S_vector &from, const S_vector &dir, uint32_t flags,
                                         float *outDistance, S_vector4 *outNormal, I3D_COLLISION_HIT *hitCb,
                                         void *user, I3D_COLLISION_FILTER *filter, I3D_frame *frame,
                                         int lodLevel);
    // 0x100 0x100110f0: the ray against `frame`'s subtree only, the hit's details in `info`.
    virtual I3D_frame *__stdcall IntersectFrame(const S_vector &from, const S_vector &dir, I3D_frame *frame,
                                                I3D_collision_info *info, uint32_t flags, I3D_frame *queryFrame,
                                                int lodLevel, I3D_COLLISION_HIT *hitCb);
    // 0x104 0x100115a0: a sphere moving from `from` by `dir` against the sectors' visuals: the
    // contact point of its first touch in `outPoint`.
    virtual I3D_frame *__stdcall MoveSphere(const S_vector &from, const S_vector &dir, float radius, uint32_t flags,
                                            S_vector4 *outPoint, I3D_frame *frame, I3D_COLLISION_HIT *hitCb);
    // 0x108 0x100145e0: the same against `root`'s subtree.
    virtual I3D_frame *__stdcall MoveSphereFrame(const S_vector &from, const S_vector &dir, float radius,
                                                 I3D_frame *root, uint32_t flags, S_vector4 *outPoint,
                                                 I3D_frame *frame, I3D_COLLISION_HIT *hitCb);
    // 0x10C 0x10014940: the same against one visual.
    virtual I3D_frame *__stdcall MoveSphereVisual(const S_vector &from, const S_vector &dir, float radius,
                                                  I3D_frame *visual, uint32_t flags, S_vector4 *outPoint);
    // 0x110 0x10014ed0: when (0 - 1) two moving spheres meet, -1 if they do not.
    virtual float __stdcall SweepSpheres(float radiusA, const S_vector &fromA, const S_vector &toA, float radiusB,
                                         const S_vector &fromB, const S_vector &toB);
    // 0x114 0x10048330: slot 0x7C of every object / lightmap visual below the primary sector.
    virtual void __stdcall SetWorldSpace(bool value);
    // 0x118 / 0x11C 0x10049820 / 0x1004a100: the weather (E_WeatherParam; floats as bits).
    virtual void __stdcall SetWeather(int param, uint32_t value);
    virtual int __stdcall GetWeather(int param);
    virtual void __stdcall ResetWeather();                              // 0x120 0x10048e60
    // 0x124 / 0x128 0x10049690 / 0x10049700: dummies whose boxes are drawn (FRMFLAGS_SCENE_DUMMY).
    virtual void __stdcall AddDrawnDummy(I3D_frame *dummy);
    virtual void __stdcall RemoveDrawnDummy(I3D_frame *dummy);
    virtual int __stdcall NumDrawnDummies();                            // 0x12C 0x10049800
    virtual S_dummyBox *__stdcall GetDrawnDummies();                    // 0x130 0x10049810
    // 0x134 - 0x140 0x1004be80 - 0x1004c190: models kept in the sector of two of their frames.
    virtual I3D_RESULT __stdcall AddTrackedModel(I3D_frame *model, I3D_frame *refA, I3D_frame *refB);
    virtual I3D_RESULT __stdcall RemoveTrackedModel(I3D_frame *model);
    virtual void __stdcall ClearTrackedModels();
    virtual void __stdcall MarkTrackedModel(I3D_frame *model);
    // 0x144 0x10048440: the screen rectangle rendered into (pixels).
    virtual void __stdcall SetViewport(int left, int top, int right, int bottom);

private:
    // Weather parameter 10: the particle count; the shared buffers grow to it (0x10049820).
    void SetWeatherCount(int count);
    static void MakeDummyBox(S_dummyBox &out, I3D_frame *frame);

public:

    void *renderCallback;                   // +0x120 SetRenderCallback
    uint32_t renderCallbackFlags;           // +0x124
    ls3d::C_vector<I3D_frame *> tickFrames; // +0x128 AddTickFrame
    // (No shadows or mirrors vectors, LS3DF's +0x138 / +0x148: LS3DRF has none.)
    I3D_anim_stack *animStack;              // +0x138 (LS3DF: +0x158) made by SetAnimation
    I3D_camera *activeCamera;               // +0x13C (LS3DF: +0x15C) SetActiveCamera
    float viewportCenterX;                  // +0x140 (LS3DF: +0x160)
    float viewportCenterY;                  // +0x144 (LS3DF: +0x164)
    uint32_t unknown148[2];                 // +0x148 (LS3DF: +0x168)
    S_matrix screenProjection;              // +0x150 (LS3DF: +0x170; its Render set it, nothing does here)
    uint32_t unknown190[16];                // +0x190 (LS3DF: +0x1B0)
    float viewportHalfWidth;                // +0x1D0 (LS3DF: +0x1F0)
    float viewportHalfHeight;               // +0x1D4 (LS3DF: +0x1F4)
    // "Primary sector" (SCTF_PRIMARY; its scene is set without the ring).
    I3D_sector *primarySector;              // +0x1D8 (LS3DF: +0x1F8)
    I3D_sector *backdropSector;             // +0x1DC (LS3DF: +0x1FC) "Backdrop sector" (SCTF_BACKDROP)
    I3D_sector *omniSector;                 // +0x1E0 (LS3DF: +0x200) "Omni sector"
    I3D_sector *nearSector;                 // +0x1E4 (LS3DF: +0x204) "Near sector"
    uint32_t unknown1E8[2];                 // +0x1E8 (LS3DF: +0x208)
    S_vector backgroundColor;               // +0x1F0 (LS3DF: +0x210) (0, 0.5, 0.4) the color the renderer clears to
    float backgroundColorW;                 // +0x1FC (LS3DF: +0x21C)
    ls3d::C_vector<S_trackedModel> trackedModels{4};  // +0x200 (LS3DF: +0x220; Init: grow 25)
    // LS3DF's Render: the tracked models' camera sector; Init clears it.
    I3D_sector *trackedCameraSector;        // +0x210 (LS3DF: +0x230)
    ls3d::C_vector<I3D_frame *> frames;     // +0x214 (LS3DF: +0x234) the frames it owns (AddFrame)
    int viewportLeft;                       // +0x224 (LS3DF: +0x244) SetViewport
    int viewportTop;                        // +0x228 (LS3DF: +0x248)
    int viewportRight;                      // +0x22C (LS3DF: +0x24C)
    int viewportBottom;                     // +0x230 (LS3DF: +0x250)
    float backdropNear;                     // +0x234 (LS3DF: +0x254) the backdrop's range (SetBackdropRange)
    float backdropFar;                      // +0x238 (LS3DF: +0x258)
    int portalCount;                        // +0x23C (LS3DF: +0x25C) the portals of the scene's sectors
    I3D_portal *firstPortal;                // +0x240 (LS3DF: +0x260) ring (I3D_portal::prev / next)
    int portalChangesSeen;                  // +0x244 (LS3DF: +0x264) g_portalChanges when UpdateSectors last ran
    int sectorCount;                        // +0x248 (LS3DF: +0x268)
    I3D_sector *firstSector;                // +0x24C (LS3DF: +0x26C) ring (I3D_sector::prevInScene / nextInScene)
    int sectorChangesSeen;                  // +0x250 (LS3DF: +0x270) g_sectorChanges when UpdateSectors last ran
    // The occluder ring: Init's zeros, nothing joins it (no occlusion).
    int occluderCount;                      // +0x254 (LS3DF: +0x274)
    I3D_occluder *firstOccluder;            // +0x258 (LS3DF: +0x278) ring (I3D_occluder::prevInScene / nextInScene)
    bool occludersChanged;                  // +0x25C (LS3DF: +0x27C) set when one joins
    bool occluderListsStale;                // +0x25D (LS3DF: +0x27D) the sectors' occluder lists need rebuilding
    uint8_t padding25E[2];
    int areaCount;                          // +0x260 (LS3DF: +0x280)
    I3D_area *firstArea;                    // +0x264 (LS3DF: +0x284) ring (I3D_area::prevInScene / nextInScene)
    bool areasChanged;                      // +0x268 (LS3DF: +0x288) set when the ring changes
    uint8_t padding269[3];
    // (No listener or lens flares, LS3DF's +0x28C / +0x290: LS3DRF has none.)
    uint32_t weatherFlags;                  // +0x26C (LS3DF: +0x2A0) WEATHER_*
    // Positions (w: height reached; grow 1000 once set).
    ls3d::C_vector<S_vector4> weatherParticles{8};   // +0x270 (LS3DF: +0x2A4)
    float weatherFallSpeed;                 // +0x280 (LS3DF: +0x2B4) param 5 (ms -> s)
    float weatherParam6;                    // +0x284 (LS3DF: +0x2B8)
    float weatherParam7;                    // +0x288 (LS3DF: +0x2BC; stored halved)
    float weatherRangeXZ;                   // +0x28C (LS3DF: +0x2C0) param 8: half the box around the camera
    float weatherRangeY;                    // +0x290 (LS3DF: +0x2C4) param 9
    int weatherCount;                       // +0x294 (LS3DF: +0x2C8) param 10 (at most 0x3333)
    int weatherParam298;                    // +0x298 (LS3DF: +0x2CC; 3000)
    uint32_t padding29C;                    // +0x29C (LS3DRF's pad: keeps weatherWind 16-byte aligned)
    S_vector4 weatherWind;                  // +0x2A0 (LS3DF: +0x2D0) params 11 - 13
    S_vector4 weatherWindStart;             // +0x2B0 (LS3DF: +0x2E0)
    S_matrix weatherDirection;              // +0x2C0 (LS3DF: +0x2F0; SetDir of the wind)
    uint32_t weatherColor1;                 // +0x300 (LS3DF: +0x330) param 3
    uint32_t weatherColor2;                 // +0x304 (LS3DF: +0x334) param 4
    float weatherRadius;                    // +0x308 (LS3DF: +0x338) the box's half diagonal
    uint32_t unknown30C;                    // +0x30C (LS3DF: +0x33C)
    S_matrix weatherMatrices[4];            // +0x310 (LS3DF: +0x340)
    ls3d::C_vector<S_dummyBox> drawnDummies{1};  // +0x410 (LS3DF: +0x440; 16-byte aligned blocks)
};
static_assert(sizeof(I3D_scene) == 0x420, "I3D_scene layout");
static_assert(offsetof(I3D_scene, tickFrames) == 0x128, "I3D_scene layout");
static_assert(offsetof(I3D_scene, animStack) == 0x138, "I3D_scene layout");
static_assert(offsetof(I3D_scene, activeCamera) == 0x13C, "I3D_scene layout");
static_assert(offsetof(I3D_scene, screenProjection) == 0x150, "I3D_scene layout");
static_assert(offsetof(I3D_scene, primarySector) == 0x1D8, "I3D_scene layout");
static_assert(offsetof(I3D_scene, nearSector) == 0x1E4, "I3D_scene layout");
static_assert(offsetof(I3D_scene, backgroundColor) == 0x1F0, "I3D_scene layout");
static_assert(offsetof(I3D_scene, trackedModels) == 0x200, "I3D_scene layout");
static_assert(offsetof(I3D_scene, trackedCameraSector) == 0x210, "I3D_scene layout");
static_assert(offsetof(I3D_scene, frames) == 0x214, "I3D_scene layout");
static_assert(offsetof(I3D_scene, viewportLeft) == 0x224, "I3D_scene layout");
static_assert(offsetof(I3D_scene, backdropNear) == 0x234, "I3D_scene layout");
static_assert(offsetof(I3D_scene, portalCount) == 0x23C, "I3D_scene layout");
static_assert(offsetof(I3D_scene, firstSector) == 0x24C, "I3D_scene layout");
static_assert(offsetof(I3D_scene, firstOccluder) == 0x258, "I3D_scene layout");
static_assert(offsetof(I3D_scene, firstArea) == 0x264, "I3D_scene layout");
static_assert(offsetof(I3D_scene, areasChanged) == 0x268, "I3D_scene layout");
static_assert(offsetof(I3D_scene, weatherFlags) == 0x26C, "I3D_scene layout");
static_assert(offsetof(I3D_scene, weatherParticles) == 0x270, "I3D_scene layout");
static_assert(offsetof(I3D_scene, weatherParam298) == 0x298, "I3D_scene layout");
static_assert(offsetof(I3D_scene, weatherWind) == 0x2A0, "I3D_scene layout");
static_assert(offsetof(I3D_scene, weatherColor1) == 0x300, "I3D_scene layout");
static_assert(offsetof(I3D_scene, weatherMatrices) == 0x310, "I3D_scene layout");
static_assert(offsetof(I3D_scene, drawnDummies) == 0x410, "I3D_scene layout");

// EnumFrames / FindFrame flags for the special sectors.
enum : uint32_t {
    ENUMF_SECTOR = 1u << (FRAME_SECTOR - 1),
    ENUMF_BACKDROP = 0x00040000,
    ENUMF_OMNI = 0x00080000,
    ENUMF_NEAR = 0x00100000,
};

namespace ls3d::i3d {

// The scenes' shared resources (0x100bf2xx): their count (Init counts up, nothing reads it), the
// weather's vertex and index buffers.
extern int g_sceneCount;                    // 0x100bf324
extern S_VBAlloc g_weatherVB;               // 0x100bf308
extern S_IBAlloc g_weatherIB;               // 0x100bf314
extern int g_weatherCapacity;               // 0x100bf320 particles the buffers hold
// The environment of sectors without one (0x100bf290).
extern I3D_environment g_defaultEnvironment;

// The environment `t` of the way from `a` to `b` (0x1004c730; the integer parameters truncated);
// `all` false: only the reverb delay (parameter 9).
void LerpEnvironment(I3D_environment &out, const I3D_environment &a, const I3D_environment &b, float t, bool all);

}  // namespace ls3d::i3d
