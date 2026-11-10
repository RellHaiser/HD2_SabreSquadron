// The graphic effects module ("GEffects", module 1; the object 0x0089b880; game/geffects.cpp,
// original source 0x00484e90..0x0048c740): the screen fade, visuals fading out and in, humans
// fading, the thunderstorm's lightning, rain, the underwater view (missions\<name>\watercam.set),
// the sun following the camera, camera animations, the time scale, the models' shadows (from the
// shadow cache), uniforms and face textures, shattered glass. It owns the decorators, the
// volumetrics and the mesh player (part 8d) and drives the birds and the clouds (8d too).
#pragma once

#include <cstdint>
#include <vector>

#include "game/module.h"
#include "game/pending.h"                               // the decorators, volumetrics, mesh player
#include "ls3d/math.h"

class C_Human;
class I3D_camera;
class I3D_frame;
class I3D_light;
class I3D_model;
struct S_vector;

// The block of missions\<name>\watercam.set (its version 1 chunk's second chunk, read raw).
struct S_WaterCamera {                                  // 0xA0 bytes (the module's +0x10C)
    bool enabled;                                       // +0x00
    bool fovWobble;                                     // +0x01 the fov sways under water
    bool weather;                                       // +0x02 the scene's weather on under water only
    uint8_t unknown03;
    float nearRange;                                    // +0x04 the camera's range under water
    float farRange;                                     // +0x08
    float fov;                                          // +0x0C degrees
    // The camera's own fov and range, kept while it is under water (the file's are overwritten).
    float savedFov;                                     // +0x10
    float savedNear;                                    // +0x14
    float savedFar;                                     // +0x18
    char aboveWater[0x40];                              // +0x1C a hierarchy shown above water
    char underWater[0x40];                              // +0x5C one shown under water
    bool underwaterNow;                                 // +0x9C
    bool switchBackdrop;                                // +0x9D the backdrop sector's children too
    uint8_t unknown9E[2];
};
static_assert(sizeof(S_WaterCamera) == 0xA0, "S_WaterCamera layout");

// The parameters CreateShadow gives a shadow (I3D_shadow's).
struct S_ShadowParams {                                 // 0x34 bytes (the module's +0x1C0)
    S_vector4 color = {0.0f, 0.0f, 0.0f, 0.0f};         // +0x00 SetColor
    uint32_t unknown10[4] = {};                         // +0x10 (not read)
    float maxHeight = 1.0f;                             // +0x20 SetMaxHeight
    float depth = 8.0f;                                 // +0x24 SetDepth
    float range = 100.0f;                               // +0x28 (I3D_shadow +0x1FC)
    int width = 64;                                     // +0x2C SetSize
    int height = 64;                                    // +0x30
};

// A visual fading out (FadeOutModel) or in (FadeIn).
struct S_ModelFade {                                    // 0x10 bytes
    I3D_frame *model;                                   // +0x00 (a reference)
    int timeLeft;                                       // +0x04 ms
    int time;                                           // +0x08 ms
    bool switchOff;                                     // +0x0C fade-outs: off at the end (the fade-ins' is not read)
};

// A human fading (FadeHuman).
struct S_HumanFade {                                    // 0x10 bytes
    C_Human *human;                                     // +0x00
    bool in;                                            // +0x04
    float rate;                                         // +0x08 per ms (1 / time)
    float alpha;                                        // +0x0C
};

// A camera playing a model's animation (SetCameraAnimation): it follows the model's camera frame.
struct alignas(16) S_CameraAnimation {                  // 0x30 bytes (_aligned_malloc)
    I3D_model *model;                                   // +0x00 the animation's model (its own)
    I3D_frame *camera;                                  // +0x04 (a reference)
    I3D_frame *cameraFrame;                             // +0x08 the model's "*cam*" frame, else its first child
    I3D_frame *parent;                                  // +0x0C the camera's own parent
    S_vector pos;                                       // +0x10 the camera's own place (+0x1C: its w, not used)
    S_quat rot;                                         // +0x20

    // The camera back where it was, its reference and the model released (0x00485fd0).
    void Restore();
};

#ifdef HD2_DEDICATED
// The server's module derives from an abstract one: its vtable 0x0084edf8 holds C_Module's slots,
// then _purecall for each of the module's own (+0x40..+0xBC). (The client has one too, vtable
// 0x008102b8, constructor 0x00486eb0; folded into C_GEffectsModule there.)
class C_GEffectsBase : public C_Module {
public:
    C_GEffectsBase(int id, const char *name) : C_Module(id, name) {}   // (DS 0x004a1970)
    ~C_GEffectsBase() override {}                       // (DS 0x004a19d0), deleting (DS 0x004a19a0)

    virtual void SetRain(bool on) = 0;
    virtual void SetThunderstorm(bool on, I3D_light *light, bool immediate) = 0;
    virtual void StartMpLightning(int time) = 0;
    virtual bool SetModelAlpha(I3D_frame *model, float level) = 0;
    virtual bool SetModelTransparency(I3D_frame *model, float level) = 0;
    virtual bool SetModelLightLevel(I3D_frame *model, float level) = 0;
    virtual void SetHierarchyOn(I3D_frame *frame, bool on, uint32_t typeMask) = 0;
    virtual void HideVisual(I3D_frame *visual, bool on) = 0;
    virtual bool ClearModelTransparency(I3D_frame *model) = 0;
    virtual void FadeOutModel(I3D_frame *model, int time, bool switchOff) = 0;
    virtual void FadeIn(I3D_frame *model, int time, bool turnOn) = 0;
    virtual void FadeInModel(I3D_frame *model, int time, bool turnOn) = 0;
    virtual void FadeOutHierarchy(I3D_frame *model, int time, bool switchOff) = 0;
    virtual void FadeHuman(C_Human *human, int time, bool in) = 0;
    virtual I3D_frame *CreateShadow(I3D_frame *model) = 0;
    virtual I3D_frame *GetShadow(I3D_frame *model) = 0;
    virtual void SetShadowParams(const S_ShadowParams *params) = 0;
    virtual S_ShadowParams *GetShadowParams() = 0;
    virtual void SetFadeLevel(float level, const S_vector *color) = 0;
    virtual void Fade(int time, int direction, const S_vector *color) = 0;
    virtual void SetWeatherOn(bool on) = 0;
    virtual float GetGroundCover(int material) = 0;
    virtual void SetTimeScale(float scale, float acceleration) = 0;
    virtual float GetTimeScale() = 0;
    virtual void RenderFade() = 0;
    virtual void RenderVolumetrics() = 0;
    virtual bool DressUniform(C_Human *human, int uniform) = 0;
    virtual bool SwitchFaceTexture(I3D_frame *model, const char *texture) = 0;
    virtual void SetCameraAnimation(I3D_frame *camera, const char *fileName) = 0;
    virtual void ShatterGlass(I3D_frame *visual, float maxArea) = 0;
    virtual void SetCloudsOn(bool on) = 0;
    virtual void SetLensFlareOn(bool on) = 0;
};
#endif

// On the server (vtable 0x0084eee0) every slot is a stub (geffects.cpp's HD2_DEDICATED part): no
// fades, alpha, shadows, time scale or weather. Its object there is a bare C_Module; the members
// below are still built but never read.
#ifdef HD2_DEDICATED
class C_GEffectsModule : public C_GEffectsBase {        // vtable 0x008101f0
#else
class C_GEffectsModule : public C_Module {              // vtable 0x008101f0
#endif
public:
    C_GEffectsModule(int id, const char *name);         // 0x004860b0
    ~C_GEffectsModule() override;                       // 0x00486f00

    // The camera animations, the lightning, the fade, the underwater view, the fades, the sun,
    // the birds, rain and the decorators (not while paused, unless the command map is open), the
    // time scale. (The command map calls it itself while the game is paused.)
    void Update(int timeDelta) override;                // 0x00487240
    // (The client side.)
    void LoadMissionData(bool server) override;         // 0x00488560
    void FreeMissionData(bool server) override;         // 0x004885a0
    void StartMission(bool server) override;            // 0x004885d0
    void StopMission(bool server) override;             // 0x004888c0
    void RestartMission(bool server) override;          // 0x004863f0
    bool SaveGame(C_chunk &) override { return true; }  // 0x00486460 (DS 0x004a1df0)
    // The start's black screen skipped (2.5 s past it).
    bool LoadGame(C_chunk &) override;                  // 0x00486470

    virtual void SetRain(bool on);                      // +0x40 0x004863e0
    // On: `light` (a light frame; nothing without one, or with a storm on) flickers at random up
    // to its power; off: at once when `immediate`, else at the next update.
    virtual void SetThunderstorm(bool on, I3D_light *light, bool immediate);   // +0x44 0x004870a0
    // The scene's "MP_Lightning" light flickers for `time` ms (the multiplayer games).
    virtual void StartMpLightning(int time);            // +0x48 0x004871a0
    // An object visual's transparency (1: invisible); false for none.
    virtual bool SetModelAlpha(I3D_frame *model, float level);   // +0x4C 0x00486e60
    // A model's hierarchy's transparency (C_Human::SetUnknown94: the camera close to a human);
    // false for none.
    virtual bool SetModelTransparency(I3D_frame *model, float level);   // +0x50 0x00486d30
    // Light level of a model (C_HumanLighting; 1: full light); the client side, false elsewhere
    // or for none.
    virtual bool SetModelLightLevel(I3D_frame *model, float level);   // +0x54 0x00486df0
    // Switches on / off every frame under `frame` (itself included) whose type is in `typeMask`
    // (bits by type: 1 visual, 2 light, 4 camera, 8 sound, 0x10 sector, 0x20 dummy, 0x40 target,
    // 0x100 user, 0x80 model, ...; sounds also start / stop).
    virtual void SetHierarchyOn(I3D_frame *frame, bool on, uint32_t typeMask);   // +0x58 0x004865e0
    virtual void HideVisual(I3D_frame *visual, bool on);   // +0x5C 0x004866f0
    // SetModelTransparency(model, 0); false for no model (debris and item actors going away).
    virtual bool ClearModelTransparency(I3D_frame *model);   // +0x60 0x00486e90
    // Fades an object visual out over `time` ms (< 1: at once); `switchOff` switches it off at
    // the end. A fade-in of it in progress is dropped.
    virtual void FadeOutModel(I3D_frame *model, int time, bool switchOff);   // +0x64 0x00488d60
    // The same the other way (it starts fully transparent); `turnOn` switches it on.
    virtual void FadeIn(I3D_frame *model, int time, bool turnOn);   // +0x68 0x00488ee0
    // Fades every visual under `model` (itself included) back in / out.
    virtual void FadeInModel(I3D_frame *model, int time, bool turnOn);   // +0x6C 0x00489060
    virtual void FadeOutHierarchy(I3D_frame *model, int time, bool switchOff);   // +0x70 0x004890f0
    // Fades a human's model out (`in` false) or back in over `time` ms (0: at once).
    virtual void FadeHuman(C_Human *human, int time, bool in);   // +0x74 0x00489180
    // A shadow for a model (the client side; null without a model or the shadow file): a copy of
    // the cached "s<the model file's name>" (a human's "shdpuppet"), "._shad.<model>", under the
    // model's own "a" (else the model), one of the model's frames; a shadow made before is taken
    // off first. The model holds the only reference.
    virtual I3D_frame *CreateShadow(I3D_frame *model);  // +0x78 0x004894e0
    // The model's shadow (no reference added), null if none.
    virtual I3D_frame *GetShadow(I3D_frame *model);     // +0x7C 0x00489470
    virtual void SetShadowParams(const S_ShadowParams *params);   // +0x80 0x00489bd0
    virtual S_ShadowParams *GetShadowParams();          // +0x84 0x00489be0
    // A full-screen quad of `color` (null: black) at opacity `level` (0..1).
    virtual void SetFadeLevel(float level, const S_vector *color);   // +0x88 0x004867f0
    // Screen fade over `time` ms (0: at once): FADE_OUT towards `color` (null: black), FADE_IN
    // back from it.
    virtual void Fade(int time, int direction, const S_vector *color);   // +0x8C 0x00486b30
    // The scene's weather on / off (the client side).
    virtual void SetWeatherOn(bool on);                 // +0x90 0x004867b0
    // How much ground of material `material` hides a lying / crouching human (1 = none).
    virtual float GetGroundCover(int material);         // +0x94 0x004867e0
    // Game speed (1.0 = normal) and its rate of change per second.
    virtual void SetTimeScale(float scale, float acceleration);   // +0x98 0x00486480
    virtual float GetTimeScale();                       // +0x9C 0x004864a0
    // The fade's quad (and the start's black screen: 3 s, then gone in 1 s).
    virtual void RenderFade();                          // +0xA0 0x00486c10
    virtual void RenderVolumetrics();                   // +0xA4 0x00486bf0
    // A human's model reloaded from item `uniform`'s model ("NA01" when it is not a uniform), its
    // animations, attached frames and face texture kept; false without a face texture ("e_f0"),
    // the file or the face texture again.
    virtual bool DressUniform(C_Human *human, int uniform);   // +0xA8 0x0048a780
    // The face ("e_f0" in its texture's name) of a skinned model gets `texture` (.bmp); false
    // without one of them.
    virtual bool SwitchFaceTexture(I3D_frame *model, const char *texture);   // +0xAC 0x0048a5c0
    // `camera` follows the camera frame of the model `fileName` while its animation plays (an
    // animation of the camera given before is dropped; an empty name only drops it).
    virtual void SetCameraAnimation(I3D_frame *camera, const char *fileName);   // +0xB0 0x00485c70
    // A glass visual's faces as falling shards: each split in two at its longest edge until its
    // area is at most `maxArea` (the car's broken windows: 0.01 square metres), each shard a
    // physics object pushed at random.
    virtual void ShatterGlass(I3D_frame *visual, float maxArea);   // +0xB4 0x00485ba0
    virtual void SetCloudsOn(bool on);                  // +0xB8 0x00485b50
    // The backdrop sector's "lensflare" frame on / off (off while the first-person view is on).
    virtual void SetLensFlareOn(bool on);               // +0xBC 0x00485b70

private:
    friend struct S_GEffectsTest;

    // The fades, the human fades, the sun, the camera animations dropped; the time scale 1.
    void Reset();                                       // 0x00488b80
    // The above-water / underwater hierarchy (SetHierarchyOn), the backdrop sector's children on
    // / off (each when the water camera has one).
    void ShowAboveWater(bool on);                       // 0x00486580
    void ShowUnderWater(bool on);                       // 0x00486750
    void ShowBackdrop(bool on);                         // 0x00486520
    // The frame `name` of the scene (`typeMask`), else the driver's search of it.
    static I3D_frame *FindSceneFrame(const char *name, uint32_t typeMask);
    // The water camera's file (all zero without one).
    void LoadWaterCamera();                             // 0x00489c60
    // The camera taken (its fov and range kept), the one before restored.
    void TakeCamera(I3D_camera *camera);
    void RestoreCamera();
    // The sounds of the sea (above / under water) and the ambient ones switched for the view.
    void SwitchSounds(bool underwater);
    // Rain drops onto the ground near the camera each 100 ms, every third step also onto the
    // player's team (RainOnTeam).
    void UpdateRain(int timeDelta);
    // Drops on the members in the primary sector within 10 m of the camera, drawn in the last
    // frame and under none of the dummies drawn.
    void RainOnTeam();

    // (+0x04 the C_Module part.)
    S_vector m_sunPos = {0.0f, 0.0f, 0.0f};             // +0x010 "mirasun"'s place (relative to the camera; +0x1C its w)
    C_Decorators m_decorators;                          // +0x020 (g_decorators)
    C_Volumetrics m_volumetrics;                        // +0x0C4
    C_MeshPlayer m_meshPlayer;                          // +0x0E4
    S_WaterCamera m_waterCamera = {};                   // +0x10C
    I3D_camera *m_waterCameraFrame = nullptr;           // +0x1AC the camera under water (a reference)
    I3D_frame *m_sun = nullptr;                         // +0x1B0 "mirasun"
    S_ShadowParams m_shadowParams;                      // +0x1C0
    std::vector<S_ModelFade *> m_fadeOuts;              // +0x200
    std::vector<S_ModelFade *> m_fadeIns;               // +0x210
    bool m_unknown220 = false;                          // +0x220 (only cleared)
    int m_time = 0;                                     // +0x224 ms since the mission's start
    std::vector<S_HumanFade> m_humanFades;              // +0x22C
    I3D_frame *m_seaSound = nullptr;                    // +0x238 "a_nsea_"
    I3D_frame *m_underwaterSound = nullptr;             // +0x23C "a_undw_"
    I3D_frame *m_ambientSound = nullptr;                // +0x240 "amb"
    I3D_frame *m_seaAmbientSound = nullptr;             // +0x244 "sea_amb"
    bool m_started = false;                             // +0x248
    bool m_rain = false;                                // +0x24C
    int m_rainRate = 200;                               // +0x250 drops per second
    float m_rainArea = 8.0f;                            // +0x254 half the square's side, m
    uint8_t m_unknown258[0x64] = {};                    // +0x258 (only cleared)
    S_vector m_fadeColor = {0.0f, 0.0f, 0.0f};          // +0x2C0 (+0x2CC: a fourth float, not used)
    float m_fadeLevel = 0.0f;                           // +0x2D0 (< 0: none)
    float m_fadeTarget = 0.0f;                          // +0x2D4
    float m_fadeRate = 0.0f;                            // +0x2D8 per second
    I3D_light *m_lightning = nullptr;                   // +0x2E0 (a reference)
    uint32_t m_lightningMode = 0;                       // +0x2E4 its own mode
    float m_lightningPower = 0.0f;                      // +0x2E8 its own power
    bool m_lightningStop = false;                       // +0x2EC
    int m_lightningTime = -1;                           // +0x2F0 ms left (StartMpLightning)
    float m_timeScale = 1.0f;                           // +0x2F4
    float m_timeScaleTarget = 1.0f;                     // +0x2F8
    float m_timeScaleRate = 1.0f;                       // +0x2FC per second
    std::vector<S_CameraAnimation *> m_cameraAnimations;   // +0x304
};
extern C_GEffectsModule g_geffectsModule;               // 0x0089b880 (static init 0x00486080)
C_GEffectsModule *GetGEffects();                        // 0x00486510

// The water surface's height last found under the player's head (C_HumanMovement::GetHeadWater).
extern float g_waterLevel;                              // 0x0089b760
// Ms gathered towards the next rain step.
extern int g_rainTime;                                  // 0x0089bbb4
// Rain steps left until the next drops on the team (every third).
extern int g_rainTeamSteps;                             // 0x008396e8 (3)
