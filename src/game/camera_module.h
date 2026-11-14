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
struct S_CollisionVolume;

// The command map's tactical camera (0xA0 bytes, _aligned_malloc'd by the module's constructor;
// the object behind g_cameraHumanEffects): its own camera, made the scene's active one while the
// map is up, moved by the map's cursor modes (TACTICAL_*), and the humans it comes near faded out
// (C_Human::SetUnknown94, their models' transparency) and back.
class alignas(16) C_CameraHumanEffects {
public:
    // Makes its camera (+0x80): the zoom 1, the height 5.
    C_CameraHumanEffects();                             // 0x00462650
    ~C_CameraHumanEffects();                            // 0x004626e0

    // Drops `human` from the faded ones (its transparency back to 0). (Quirk kept: the last one
    // takes its place keeping the dropped one's value.)
    void Remove(C_Human *human);                        // 0x00463040
    // Puts its camera 3 m behind and 1.5 m above `human` along its view (in a car, a submarine,
    // a static weapon or the Junkers: 5 m behind, 3 m above), looking at it; null: the game
    // camera's (+0x84) place and direction. (The command map's double click.)
    void LookAt(C_Human *human);                        // 0x00462780

    // On: the scene's camera kept (+0x84) and copied, then 7 m behind and 3 m above the active
    // soldier, looking down along his view; off: the scene's camera back, the module's
    // SelectView(0), the faded humans back.
    void SetTactical(bool on);                          // 0x00462ae0
    bool IsTactical() const;                            // 0x00462770
    // The cursor's mode (TACTICAL_*) and the map point under it (null: kept). Leaving a mode
    // other than a turn puts the look point (+0x10) in front of the camera. A second TACTICAL_DRAG
    // within 500 ms starts the flight to the point (TACTICAL_FLY); the flight ends at the next
    // mode but TACTICAL_IDLE, or a TACTICAL_DRAG after 300 ms (a third click: the flight again,
    // its speed kept).
    void SetTacticalMode(int mode, const S_vector4 *point);   // 0x004632c0
    // The mode's speeds along the two axes (the cursor's or the keys').
    void SetTacticalMotion(float x, float y);           // 0x004633a0
    // The zoom (the moves' speed; 0.25 .. 4).
    void SetTacticalZoom(float zoom);                   // 0x00462750
    // Moves it by its mode (the command map calls it while the game is paused), then fades the
    // humans near it.
    void UpdateTactical(int timeDelta);                 // 0x00463ba0

private:
    struct S_FadedHuman {                               // 8 bytes
        C_Human *human;
        float transparency;
    };

    // The flight (TACTICAL_FLY): to three quarters of the way to the point (not at all within
    // 1 m; the point moved to the obstacle in between), turning to look along the way; its
    // acceleration for a flight of 1.25 / 1.875 / 2.5 s (under 5 m / 35 m / farther), its first
    // third speeding up, its last slowing down.
    void StartFlight();                                 // 0x00462350
    // The look point: 10 m in front of the camera (2 m outside the primary sector).
    void UpdateLookPoint();                             // 0x004630e0
    // Off: the faded humans' transparency back to 0, the list emptied.
    void ResetHumans(bool tactical);                    // 0x00463620
    // 1 within 0.4 m^2 (squared distance) of the camera, 0 beyond 2.4, linear in between: the
    // segment from the human's body frame to his frame (0.1 m up).
    float GetTransparency(C_Human *human) const;        // 0x00463690
    // A human's transparency set when it changes; one at 0 dropped at its next change (quirk
    // kept), one not listed added when it is not 0.
    void UpdateHuman(C_Human *human);                   // 0x00463780
    // The player team's known enemies, then its members (the time not used).
    void UpdateHumans(int timeDelta);                   // 0x00463890
    // The camera moved by `move`, sliding along what it hits (then 5 cm off it); what it hit.
    S_CollisionVolume *Move(const S_vector &move);     // 0x00464b80

    S_vector4 m_target;                                 // +0x00 the map point (the flight's end)
    S_vector4 m_lookPoint;                              // +0x10 (not read)
    S_vector4 m_pos;                                    // +0x20 its camera's position
    float m_lookDistance;                               // +0x30
    float m_motionX = 0.0f;                             // +0x34
    float m_motionY = 0.0f;                             // +0x38
    float m_height = 5.0f;                              // +0x3C (not read)
    float m_zoom = 1.0f;                                // +0x40
    int m_clickTime = 0;                                // +0x44 ms left for a double click
    bool m_tactical = false;                            // +0x48
    bool m_keepSpeed;                                   // +0x49 the next flight keeps the speed
    int m_mode;                                         // +0x4C TACTICAL_*
    S_quat m_flightFrom;                                // +0x50 the camera's rotation at the start
    S_quat m_flightTo;                                  // +0x60 ... looking along the flight
    float m_flightSpeed;                                // +0x70 m/s
    float m_flightAccel;                                // +0x74 m/s^2
    float m_flightDistance;                             // +0x78 m
    float m_flightTime;                                 // +0x7C ms
    I3D_camera *m_camera;                               // +0x80
    I3D_camera *m_gameCamera = nullptr;                 // +0x84 the scene's camera while tactical
    uint8_t m_unknown88[4];                             // +0x88 (MSVC 6: the vector's allocator)
    std::vector<S_FadedHuman> m_humans;                 // +0x8C
    uint8_t m_unknown98[8];                             // +0x98
};

// C_CameraHumanEffects::SetTacticalMode() modes (named by the command map's cursor shapes).
enum : int {
    TACTICAL_IDLE = 0,
    TACTICAL_MOVE = 1,                                  // the arrows / the screen's edges
    TACTICAL_TURN = 2,                                  // the keypad's 4, 6, 8, 2 / the middle button
    TACTICAL_DRAG = 3,                                  // the map dragged with the left button
    // 4 .. 7 turn as TACTICAL_TURN (not set by the command map).
    TACTICAL_UP = 8,                                    // page up / the wheel
    TACTICAL_DOWN = 9,
    TACTICAL_FLY = 10,                                  // the double click's flight
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
    float radius = 0.2f;                                // +0x0C the camera's sphere (also off the water's surface)
    uint32_t flags = 0x8B;                              // +0x10 (CAMERA_SETTINGS_*)
    float maxPitch = 0.5f;                              // +0x14 radians
    float minPitch = -0.5f;                             // +0x18
    float speed = 12.0f;                                // +0x1C m/s the distance eases at
};
enum : uint32_t {
    CAMERA_SETTINGS_FOLLOW = 0x01,                      // (with FIXED_DIR: the camera's direction kept)
    CAMERA_SETTINGS_PITCH = 0x02,                       // the mouse pitches it with a member selected too
    CAMERA_SETTINGS_FIXED_DIR = 0x08,                   // SetViewDir ignored; the camera's direction
                                                        //   taken (views 7 .. 9 or none selected)
    CAMERA_SETTINGS_COLLIDE = 0x80,                     // the camera swept back to what is in the way
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

    // (C_Module::Update; the game calls it itself while paused.) The debug camera, the scripted
    // view or the car's camera when on; else the view direction turned by the mouse (in the
    // view's pitch limits; the first-person view: the soldier's, kept out of the water's surface),
    // the camera placed and moved (PlaceCamera, MoveCamera), the listener's sector looked up every
    // 500 ms, the offsets shrunk.
    void Update(int timeDelta) override;                // 0x00469300
    // The client side: the settings loaded, the game camera made the scene's active one (in the
    // place of the scene's camera, with the game's field of view and range).
    void StartMission(bool server) override;            // 0x00465160
    // The client side: the scene's camera back, the game camera and the camera in use released.
    void StopMission(bool server) override;             // 0x00465610
    // A client stops and starts again, then SelectView(0) unless the active soldier is in the
    // Junkers.
    void RestartMission(bool server) override;          // 0x00464e10
    bool SaveGame(C_chunk &chunk) override;             // 0x00464e60 (DS 0x00481dc0)
    // SelectView(0) and an update of 100 ms.
    bool LoadGame(C_chunk &chunk) override;             // 0x00464e70

    // The camera dummy and the view for the active soldier: a seat's camera (8 a seat weapon's,
    // 10 the Junkers gunner's in third person, 7 / 9 a passenger's, 1 / 2 an armed seat's), else
    // his "camera3RD*" (1 outdoors, 3 indoors, 4 close indoors, 5 / 6 under water) or "cameraFPV*"
    // (2). A new dummy is held and the camera starts from it. (The argument is not used.)
    void SelectView(int unused);                        // +0x40 0x0046c370 (DS 0x00487500)
    // Special camera mode `mode` (CAMERA_MODE_*): `on` sets it to whether the third argument is
    // non-zero, else toggles it (CAMERA_MODE_DEBUG, _SPECTATOR, _MOUSE_LOOK); CAMERA_MODE_AIM,
    // _UNDERWATER and _PROJECTILE take `on` itself (the projectile's frame, the water's surface).
    void SetSpecialMode(int mode, bool on, I3D_frame *target);   // +0x44 0x0046cb20 (DS 0x00487e00)
    void SetSpecialMode(int mode, bool on, int value);
    void SetSpecialMode(int mode, bool on, float height);
    // The same slot with a vector: CAMERA_MODE_EXPLOSION's place (`value` metres: the shake
    // within), CAMERA_MODE_KICK_DIR / _POS's kick.
    void SetSpecialMode(int mode, const S_vector4 *vector, uint32_t value);
    // The direction the player's camera looks along (+0x20; not in view 10 or while the view's
    // CAMERA_SETTINGS_FIXED_DIR is set).
    void SetViewDir(const S_vector4 &dir);              // +0x48 0x00465ea0
    bool IsGameCamera(I3D_frame *camera);               // +0x4C 0x00465ee0
    // The game camera's field of view (radians; 80 degrees without one) and its near sector's
    // (the first-person view's weapon: 65 degrees, 40 through the sights).
    bool SetFov(float fov);                             // +0x50 0x00465e50
    float GetFov();                                     // +0x54 0x00465e80
    // The view distance of the scene's active camera (its far range; the AI's sight range limit).
    float GetViewDistance();                            // +0x58 0x00465de0 (DS 0x004831b0)
    bool SetNearSectorFov(float fov);                   // +0x5C 0x00465e00
    float GetNearSectorFov();                           // +0x60 0x00465e30
    // Position and view direction of camera `index` (0: the scene's camera, 1: the game camera;
    // false without one; 2 and 3: the module's, CAMERA_POS_*). They write a whole S_vector4.
    bool GetCameraPos(int index, S_vector4 *pos);       // +0x64 0x00465a10 (DS 0x00482d20)
    bool GetCameraDir(int index, S_vector4 *dir);       // +0x68 0x00465b40
    // The camera frame in use (a human's "camera3RD" / "cameraFPV" dummy when it follows one).
    I3D_frame *GetCamera();                             // +0x6C 0x004650b0
    // The game camera eases into its next position over `time` ms (ignored on a client whose
    // active soldier is in a vehicle).
    void SetBlendTime(int time);                        // +0x70 0x00465ad0 (DS 0x00482e10)
    // Makes the game camera watch `frame`; speed -1 = cut, else degrees per second.
    void SetTarget(I3D_frame *frame, int speed);        // +0x74 0x00465b20
    // The debug camera is on (+0x26D; the player-driven meleeTracker ignores the mouse
    // meanwhile).
    bool GetUnknown78();                                // +0x78 0x00465060
    bool GetState();                                    // +0x7C 0x00465070 (1 = underwater)
    bool IsUnderwater() { return GetState(); }
    // The object at +0x2B4 is set (a scripted view, inferred): the player's controls are
    // dropped meanwhile.
    bool HasScriptedView();                             // +0x80 0x00465080
    // A spectator's camera is on (+0x26E).
    bool GetUnknown84();                                // +0x84 0x00465090
    C_CameraHumanEffects *GetHumanEffects();            // +0x88 0x004650a0 (DS 0x00482110)
    // Sector the camera (the sound listener) is in.
    I3D_sector *GetListenerSector();                    // +0x8C 0x004650c0

private:
    friend C_CameraHumanEffects *&CameraHumanEffectsRef();
    // Reads tables\gamecam.set into the views' settings (as many whole records as the chunk
    // holds, up to the 11); +0x2AC from view 1's distance.
    void LoadSettings();                                // 0x004656d0
    void ResetUnknown2AC();                             // 0x00465a00
    // The active soldier in a car seat (not a seat weapon's): the car's own camera, turned by
    // the look actions (30 degrees aside, 25 up or down) about the camera dummy's view; the
    // soldier's view in view 9 from a hidden seat or from an armed one (the local player).
    void UpdateCarCamera(int timeDelta);                // 0x0046ada0
    // The scripted view: 10 m behind its frame looking along it, swaying with the game's time;
    // the shake and the offsets on top.
    void UpdateScriptedView(int timeDelta);             // 0x0046b550
    // The debug camera: turned by the look actions, moved along its view by the mouse buttons
    // (DEBUG_CAMERA_SPEEDS by m_debugSpeed, the speed action).
    void UpdateDebugCamera(int timeDelta);              // 0x0046beb0
    // The camera's place for the view: its direction turned towards the wanted one, then the
    // distance behind the camera dummy (CAMERA_SETTINGS_COLLIDE: swept back to what is in the
    // way, easing in and out at the view's speed); kept out of the water's surface.
    void PlaceCamera(int timeDelta);                    // 0x00468130
    // The game camera there (unless the turret's view), shaken and offset, turned (to the
    // target: the active camera; aiming: swaying); the turret's view; the camera dummy's soldier
    // faded as the camera nears him.
    void MoveCamera(int timeDelta);                     // 0x00466970
    // (Inline in MoveCamera and UpdateScriptedView.) The shake's offsets for the time left.
    void UpdateShake(int timeDelta);
    // (Inline in MoveCamera.) The scene's active camera turned to the target.
    void TurnToTarget(int timeDelta);
    // (Inline in MoveCamera.) The turret's view (8) and the seat's own view.
    void PlaceTurretCamera();
    // SetSpecialMode's switch (the overloads' arguments as the original's slot takes them).
    void SpecialMode(int mode, bool on, bool value, I3D_frame *frame, const S_vector4 *vector, float number);

    C_CameraHumanEffects *m_humanEffects;               // +0x00C
    S_vector4 m_wantedPos = {};                         // +0x010 (index 3)
    S_vector4 m_wantedDir = {0.0f, -1.0f, 1.0f, 0.0f};  // +0x020 (index 3; Quirk kept: not unit;
                                                        //   w not set by the original)
    S_vector4 m_pos = {};                               // +0x030 (index 2)
    S_vector4 m_dir = {};                               // +0x040 (index 2)
    float m_unknown050 = 0.0f;                          // +0x050
    // This frame's push out of the band around the water's surface (in the first-person view
    // with the head under water; m_waterLevel +- the view's radius + 0.1 m).
    float m_waterOffset = 0.0f;                         // +0x054
    float m_unknown058 = 0.0f;                          // +0x058
    uint8_t m_unknown05C[4] = {};                       // +0x05C
    // The shake (m_shakeTime ms left): offsets of the position and of the view direction.
    S_vector4 m_shakePos = {};                          // +0x060
    S_vector4 m_shakeDir = {};                          // +0x070
    // Offsets of the view direction and of the position in the game camera's space, shrinking
    // by 1 per second once their delays (m_offsetDirDelay, m_offsetPosDelay) are over.
    S_vector4 m_offsetDir = {};                         // +0x080
    S_vector4 m_offsetPos = {};                         // +0x090
    uint8_t m_unknown0A0[0x30] = {};                    // +0x0A0
    int m_unknown0D0 = 0;                               // +0x0D0
    uint8_t m_unknown0D4[0xC] = {};                     // +0x0D4
    S_CameraSettings m_settings[CAMERA_VIEW_COUNT];     // +0x0E0
    I3D_camera *m_gameCamera = nullptr;                 // +0x240
    I3D_camera *m_sceneCamera = nullptr;                // +0x244 the scene's camera, back at the stop
    I3D_frame *m_camera = nullptr;                      // +0x248 in use (held)
    I3D_sector *m_listenerSector = nullptr;             // +0x24C
    int m_listenerTime = 0;                             // +0x250 ms to the listener sector's next look
    float m_distance = 0.0f;                            // +0x254 the camera's distance now (eases)
    float m_turnSpeed = 0.0f;                           // +0x258 the turn's (16; 6 in view 7 after a frame)
    S_CollisionVolume *m_hit = nullptr;                 // +0x25C what the camera is held off by
    int m_view = 1;                                     // +0x260 its settings' index
    S_CameraSettings *m_viewSettings = &m_settings[1];  // +0x264
    int m_debugSpeed = 0;                               // +0x268 the debug camera's speed (0 .. 9)
    bool m_cut = false;                                 // +0x26C the next placement at once (speed 1000)
    bool m_debugCamera = false;                         // +0x26D the free debug camera
    bool m_spectating = false;                          // +0x26E a spectator's camera (Update: only the debug camera)
    bool m_mouseLook = true;                            // +0x26F the mouse turns it
    bool m_underwater = false;                          // +0x270
    int m_spectator = 0;                                // +0x274 its number
    bool m_unknown278 = false;                          // +0x278 (never set: the first-person view off at unknown08)
    float m_waterLevel = 0.0f;                          // +0x27C (CAMERA_MODE_UNDERWATER's)
    int m_blendTime = 0;                                // +0x280 ms
    int m_shakeTime = 0;                                // +0x284 ms
    I3D_frame *m_target = nullptr;                      // +0x288
    float m_targetSpeed = 0.0f;                         // +0x28C radians per second (-1: cut)
    bool m_aiming = false;                              // +0x290 (CAMERA_MODE_AIM: the sway; no kicks)
    int m_aimTime = 0;                                  // +0x294 ms (up to 5000)
    int m_time = 0;                                     // +0x298 ms of updates
    int m_offsetPosDelay = 0;                           // +0x29C ms
    int m_offsetDirDelay = 0;                           // +0x2A0 ms
    float m_fadeTarget = 0.0f;                          // +0x2A4 the camera dummy's soldier's transparency
    float m_fade = 0.0f;                                // +0x2A8 ... now (0.1 a frame towards it)
    float m_unknown2AC = 0.0f;                          // +0x2AC
    bool m_scriptedViewOn = false;                      // +0x2B0
    I3D_frame *m_scriptedView = nullptr;                // +0x2B4 the camera 10 m behind it, swaying
    float m_carPitch = 0.0f;                            // +0x2B8 the car camera's look (radians)
    float m_carYaw = 0.0f;                              // +0x2BC
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
    CAMERA_MODE_DEBUG = 0,                              // the free debug camera
    CAMERA_MODE_EXPLOSION = 1,                          // a client's shake near an explosion
    CAMERA_MODE_SPECTATOR = 2,                          // a "spectator<n>" camera (at random)
    CAMERA_MODE_MOUSE_LOOK = 3,                         // the mouse turns the camera
    CAMERA_MODE_UNDERWATER = 4,                         // the effects module's water camera
    CAMERA_MODE_KICK_DIR = 5,                           // a kick of the view direction (at most 0.25)
    CAMERA_MODE_KICK_POS = 6,                           // a kick of the position (at most 1 m)
    CAMERA_MODE_AIM = 7,                                // the first-person view through the sights / scope
    CAMERA_MODE_NEXT_SPECTATOR = 8,                     // the next spectator's camera
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
