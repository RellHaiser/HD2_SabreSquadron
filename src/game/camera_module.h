// The camera module ("CameraModule", module 15; the object 0x0089afb0; game/camera_module.cpp,
// original source 0x00462320..0x0046cb20): the game camera (its own I3D camera, made the scene's
// active one for the mission), its settings per view (tables\gamecam.set), the views' targets,
// the command map's tactical camera (C_CameraHumanEffects) and the vehicles' third-person camera
// dummies (C_CameraFrameKeeper).
#pragma once

#include <cstdint>
#include <vector>

#include "game/module.h"
#include "ls3d/math.h"

class C_Actor;
class C_Human;
class I3D_camera;
class I3D_frame;
class I3D_sector;

// Humans whose movement takes an effect value from the camera (the list at +0x8C; C_Human::
// SetUnknown94) and the command map's tactical camera (0xA0 bytes, _aligned_malloc'd by the
// module's constructor).
class alignas(16) C_CameraHumanEffects {
public:
    // Makes its camera (+0x80): the zoom 1, the height 5.
    C_CameraHumanEffects();                             // 0x00462650
    ~C_CameraHumanEffects();                            // 0x004626e0

    // Removes `human` (its value goes back to 0).
    void Remove(C_Human *human);                        // 0x00463040
    // Puts its camera (+0x80) 3 m behind and 1.5 m above `human` along its view (5 m in a
    // car, a submarine, a seat weapon or the Junkers), looking at it; null: the default
    // frame's (+0x84) place and direction. (The command map's double click.)
    void LookAt(C_Human *human);                        // 0x00462780

    // The command map's tactical camera (its own camera +0x80; the game's active one is kept at
    // +0x84 meanwhile).
    // On: it starts 7 m behind and 3 m above the active soldier, looking down along his view;
    // off: the game's camera back.
    void SetTactical(bool on);                          // 0x00462ae0
    bool IsTactical() const;                            // 0x00462770 (byte +0x48)
    // Its motion mode (TACTICAL_*; +0x4C) and the point it follows (TACTICAL_DRAG; null: kept).
    void SetTacticalMode(int mode, const S_vector4 *point);   // 0x004632c0
    // The mode's speeds along the two axes (+0x34, +0x38).
    void SetTacticalMotion(float x, float y);           // 0x004633a0
    // The zoom (+0x40; 0.25 .. 4).
    void SetTacticalZoom(float zoom);                   // 0x00462750
    // Moves it by its mode (the command map calls it while the game is paused).
    void UpdateTactical(int timeDelta);                 // 0x00463ba0

private:
    uint8_t m_unknown00[0x20];                          // +0x00 (not set by the constructor)
    S_vector4 m_pos;                                    // +0x20 its camera's position
    uint8_t m_unknown30[4];                             // +0x30
    float m_motionX = 0.0f;                             // +0x34
    float m_motionY = 0.0f;                             // +0x38
    float m_height = 5.0f;                              // +0x3C
    float m_zoom = 1.0f;                                // +0x40
    float m_unknown44 = 0.0f;                           // +0x44
    bool m_tactical = false;                            // +0x48
    uint8_t m_unknown49[0x37];                          // +0x49 (+0x4C the mode)
    I3D_camera *m_camera;                               // +0x80
    I3D_camera *m_gameCamera = nullptr;                 // +0x84 the scene's camera while tactical
    uint8_t m_unknown88[4];                             // +0x88 (MSVC 6: the vector's allocator)
    std::vector<C_Human *> m_humans;                    // +0x8C
    uint8_t m_unknown98[8];                             // +0x98
};

// C_CameraHumanEffects::SetTacticalMode() modes (named by the command map's cursor shapes).
enum : int {
    TACTICAL_IDLE = 0,
    TACTICAL_MOVE = 1,                                  // the arrows / the screen's edges
    TACTICAL_TURN = 2,                                  // the keypad's 4, 6, 8, 2 / the middle button
    TACTICAL_DRAG = 3,                                  // the map dragged with the left button
    TACTICAL_UP = 8,                                    // page up / the wheel
    TACTICAL_DOWN = 9,
    TACTICAL_PLUS = 11,                                 // the keypad's + / -
    TACTICAL_MINUS = 12,
};

// A view's settings (tables\gamecam.set: chunk 0, the version 2; chunk 0, the 11 views' records;
// the defaults where the file is missing or short). The fields' meanings are inferred from the
// file's values.
struct S_CameraSettings {                               // 0x20 bytes
    float distance = 3.0f;                              // +0x00 behind the target
    float unknown04 = 0.5f;                             // +0x04
    float unknown08 = 1.0f;                             // +0x08
    float unknown0C = 0.2f;                             // +0x0C
    uint32_t flags = 0x8B;                              // +0x10 (CAMERA_SETTINGS_*)
    float maxPitch = 0.5f;                              // +0x14 radians
    float minPitch = -0.5f;                             // +0x18
    float unknown1C = 12.0f;                            // +0x1C
};
enum : uint32_t {
    CAMERA_SETTINGS_FIXED_DIR = 0x08,                   // SetViewDir ignored
};
enum : int {
    CAMERA_VIEW_COUNT = 11,
};

// The module's slots past C_Module's (+0x40 ..; its intermediate class's vtable 0x0080fd40 has
// them too) are called directly here: nothing calls them through the vtable but the module itself.
class alignas(16) C_CameraModule : public C_Module {   // vtable 0x0080fdd0, 0x2C0 bytes
public:
    C_CameraModule(int id, const char *name);           // 0x00464ef0
    ~C_CameraModule() override;                         // 0x004650f0

    // (C_Module::Update; the game calls it itself while paused.)
    void Update(int timeDelta) override;                // 0x00469300
    // The client side: the settings loaded, the game camera made the scene's active one (in the
    // place of the scene's camera, with the game's field of view and range).
    void StartMission(bool server) override;            // 0x00465160
    // The client side: the scene's camera back, the game camera and the camera in use released.
    void StopMission(bool server) override;             // 0x00465610
    // A client stops and starts again, then Unknown40(0) unless the active soldier is in the
    // Junkers.
    void RestartMission(bool server) override;          // 0x00464e10
    bool SaveGame(C_chunk &chunk) override;             // 0x00464e60
    // Unknown40(0) and an update of 100 ms.
    bool LoadGame(C_chunk &chunk) override;             // 0x00464e70

    // Called when the active soldier moves into another sector (not identified).
    void Unknown40(int arg);                            // +0x40 0x0046c370
    // Special camera `mode` on or off; mode CAMERA_MODE_PROJECTILE follows `target`.
    void SetSpecialMode(int mode, bool on, I3D_frame *target);   // +0x44 0x0046cb20
    // The same slot with 1 as the third argument (the game's free camera; not identified).
    void SetSpecialMode(int mode, bool on, int unknown);
    // The same slot with a height (CAMERA_MODE_UNDERWATER: the water's surface).
    void SetSpecialMode(int mode, bool on, float height);
    // The direction the player's camera looks along (+0x20; not in view 10 or while the view's
    // CAMERA_SETTINGS_FIXED_DIR is set).
    void SetViewDir(const S_vector4 &dir);              // +0x48 0x00465ea0
    bool IsGameCamera(I3D_frame *camera);               // +0x4C 0x00465ee0
    // The game camera's field of view (radians; 80 degrees without one) and its near sector's
    // (the first-person view's weapon: 65 degrees, 40 through the sights).
    bool SetFov(float fov);                             // +0x50 0x00465e50
    float GetFov();                                     // +0x54 0x00465e80
    // The view distance of the scene's active camera (its far range; the AI's sight range limit).
    float GetViewDistance();                            // +0x58 0x00465de0
    bool SetNearSectorFov(float fov);                   // +0x5C 0x00465e00
    float GetNearSectorFov();                           // +0x60 0x00465e30
    // Position and view direction of camera `index` (0: the scene's camera, 1: the game camera;
    // false without one; 2 and 3: the module's, CAMERA_POS_*). They write a whole S_vector4.
    bool GetCameraPos(int index, S_vector4 *pos);       // +0x64 0x00465a10
    bool GetCameraDir(int index, S_vector4 *dir);       // +0x68 0x00465b40
    // The camera frame in use (a human's "camera3RD" / "cameraFPV" dummy when it follows one).
    I3D_frame *GetCamera();                             // +0x6C 0x004650b0
    // The game camera eases into its next position over `time` ms (ignored on a client whose
    // active soldier is in a vehicle).
    void SetBlendTime(int time);                        // +0x70 0x00465ad0
    // Makes the game camera watch `frame`; speed -1 = cut, else degrees per second.
    void SetTarget(I3D_frame *frame, int speed);        // +0x74 0x00465b20
    // The byte at +0x26D (not identified; the player-driven meleeTracker ignores the mouse while it
    // is set).
    bool GetUnknown78();                                // +0x78 0x00465060
    bool GetState();                                    // +0x7C 0x00465070 (1 = underwater)
    bool IsUnderwater() { return GetState(); }
    // The object at +0x2B4 is set (a scripted view, inferred): the player's controls are
    // dropped meanwhile.
    bool HasScriptedView();                             // +0x80 0x00465080
    // The byte at +0x26E (not identified).
    bool GetUnknown84();                                // +0x84 0x00465090
    C_CameraHumanEffects *GetHumanEffects();            // +0x88 0x004650a0
    // Sector the camera (the sound listener) is in.
    I3D_sector *GetListenerSector();                    // +0x8C 0x004650c0

private:
    friend C_CameraHumanEffects *&CameraHumanEffectsRef();
    // Reads tables\gamecam.set into the views' settings (as many whole records as the chunk
    // holds, up to the 11); +0x2AC from view 1's distance.
    void LoadSettings();                                // 0x004656d0
    void ResetUnknown2AC();                             // 0x00465a00

    C_CameraHumanEffects *m_humanEffects;               // +0x00C
    S_vector4 m_wantedPos = {};                         // +0x010 (index 3)
    S_vector4 m_wantedDir = {0.0f, -1.0f, 1.0f, 0.0f};  // +0x020 (index 3; Quirk kept: not unit;
                                                        //   w not set by the original)
    S_vector4 m_pos = {};                               // +0x030 (index 2)
    S_vector4 m_dir = {};                               // +0x040 (index 2)
    float m_unknown050[3] = {};                         // +0x050
    uint8_t m_unknown05C[4] = {};                       // +0x05C
    int m_unknown060 = 0;                               // +0x060
    int m_unknown064 = 0;                               // +0x064
    int m_unknown068 = 0;                               // +0x068
    float m_unknown06C = 0.0f;                          // +0x06C
    int m_unknown070 = 0;                               // +0x070
    int m_unknown074 = 0;                               // +0x074
    int m_unknown078 = 0;                               // +0x078
    float m_unknown07C = 0.0f;                          // +0x07C
    float m_unknown080[3] = {};                         // +0x080
    uint8_t m_unknown08C[4] = {};                       // +0x08C
    float m_unknown090[3] = {};                         // +0x090
    uint8_t m_unknown09C[0x34] = {};                    // +0x09C
    int m_unknown0D0 = 0;                               // +0x0D0
    uint8_t m_unknown0D4[0xC] = {};                     // +0x0D4
    S_CameraSettings m_settings[CAMERA_VIEW_COUNT];     // +0x0E0
    I3D_camera *m_gameCamera = nullptr;                 // +0x240
    I3D_camera *m_sceneCamera = nullptr;                // +0x244 the scene's camera, back at the stop
    I3D_frame *m_camera = nullptr;                      // +0x248 in use (held)
    I3D_sector *m_listenerSector = nullptr;             // +0x24C
    int m_unknown250 = 0;                               // +0x250
    float m_unknown254 = 0.0f;                          // +0x254
    float m_unknown258 = 0.0f;                          // +0x258
    void *m_unknown25C = nullptr;                       // +0x25C
    int m_view = 1;                                     // +0x260 its settings' index
    S_CameraSettings *m_viewSettings = &m_settings[1];  // +0x264
    int m_unknown268 = 0;                               // +0x268
    bool m_unknown26C = false;                          // +0x26C
    bool m_unknown26D = false;                          // +0x26D
    bool m_unknown26E = false;                          // +0x26E
    bool m_unknown26F = true;                           // +0x26F
    bool m_underwater = false;                          // +0x270
    int m_unknown274 = 0;                               // +0x274
    bool m_unknown278 = false;                          // +0x278
    float m_unknown27C = 0.0f;                          // +0x27C
    int m_blendTime = 0;                                // +0x280 ms
    int m_unknown284 = 0;                               // +0x284
    I3D_frame *m_target = nullptr;                      // +0x288
    float m_targetSpeed = 0.0f;                         // +0x28C radians per second (-1: cut)
    bool m_unknown290 = false;                          // +0x290
    int m_unknown294 = 0;                               // +0x294
    int m_unknown298 = 0;                               // +0x298
    int m_unknown29C = 0;                               // +0x29C
    int m_unknown2A0 = 0;                               // +0x2A0
    float m_unknown2A4 = 0.0f;                          // +0x2A4
    float m_unknown2A8 = 0.0f;                          // +0x2A8
    float m_unknown2AC = 0.0f;                          // +0x2AC
    bool m_unknown2B0 = false;                          // +0x2B0
    void *m_scriptedView = nullptr;                     // +0x2B4
    float m_unknown2B8 = 0.0f;                          // +0x2B8
    float m_unknown2BC = 0.0f;                          // +0x2BC
};
// GetCameraPos / GetCameraDir indices.
enum : int {
    CAMERA_POS_SCENE = 0,
    CAMERA_POS_GAME = 1,
    CAMERA_POS_CURRENT = 2,                             // +0x30 / +0x40
    CAMERA_POS_WANTED = 3,                              // +0x10 / +0x20
};

extern C_CameraModule g_cameraModule;                   // 0x0089afb0 (static init 0x00464dc0)
C_CameraModule *GetCameraModule();                      // 0x00464e90 (&g_cameraModule)
// The module's human effects (its +0xC, 0x0089afbc: the effects module reads it directly).
extern C_CameraHumanEffects *&g_cameraHumanEffects;
// C_CameraModule::SetSpecialMode() modes.
enum : int {
    CAMERA_MODE_UNDERWATER = 4,                         // the effects module's water camera
    CAMERA_MODE_AIM = 7,                                // the first-person view through the sights / scope
    CAMERA_MODE_PROJECTILE = 9,                         // rides along with a fired projectile
};

// The camera module's keeper of a camera dummy's placement (0x50 bytes, 16-byte aligned; the
// car's StartMission makes one per seat with a "camera3RD*" dummy, S_VehicleSeat::camera; the
// submarine one for its pilot): the dummy's local rotation and position, its parent's and its own
// world position. Its Update raises the dummy over its parent (by its height, to the ground
// below) and turns it to look down along the vehicle's motion.
class alignas(16) C_CameraFrameKeeper {
public:
    C_CameraFrameKeeper(C_Actor *owner, I3D_frame *frame);   // 0x00465f00
    // Restore (0x00466000, a JMP to it; its two callers, the car's StopMission and 0x005dfca0,
    // _aligned_free the keeper after it).
    ~C_CameraFrameKeeper();
    // Puts the dummy back where it was made (true; false without a frame); the next Update cuts.
    bool Restore();                                 // 0x00466010
    // Every frame (the car's / the submarine's Update; the time is not used): true with a frame
    // and an owner.
    bool Update(int time);                          // 0x00466110

private:
    S_quat m_rot;                                   // +0x00 the dummy's local rotation
    S_vector4 m_localPos;                           // +0x10 its local position (y: its height)
    S_vector4 m_parentPos;                          // +0x20 its parent's world position
    S_vector4 m_worldPos;                           // +0x30 its own
    I3D_frame *m_frame;                             // +0x40
    C_Actor *m_owner;                               // +0x44
    bool m_cut = false;                             // +0x48 the next Update's blend speed 1000
    uint8_t m_unknown49[7];                         // +0x49
};
// The game camera's blend speed the active soldier's vehicle's keeper sets (3; a car: (its speed
// + 2.5) * 0.3, a submarine: its speed / 40, at least 0.03; 1000 after a Restore). Not read.
extern float g_vehicleCameraSpeed;                  // 0x0089b208
