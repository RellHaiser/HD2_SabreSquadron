// Game subsystems driven by the mission scripts (teams, actors, effects, sound,
// cameras, checkpoints, interface). None of these is hand-ported yet: the
// declarations name the original functions (addresses in the comments; their
// mechanical translation is in src/tier1) so that ported code can call them.
// Members that the original accesses inline are declared at their offsets.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/car.h"
#include "game/checkpoints.h"
#include "game/debris.h"
#include "game/item_actor.h"
#include "game/melee_tracker.h"
#include "game/melee_tracker_ai.h"
#include "game/human.h"
#include "game/human_body.h"
#include "game/resource_cache.h"
#include "game/rigid_body.h"
#include "game/sound_module.h"
#include "game/team.h"
#include "game/vehicle.h"
#include "ls3d/i3d.h"

class C_BitStream;
class C_Item;

// ---------------------------------------------------------------------------
// Actor classes (members used by scripts)

// Humans: see game/human.h (controllers: game/human_controller.h, body: game/human_body.h).

class C_Bike : public C_Actor {                         // "Bike", vtable 0x00812440
public:
    void Drive(int roadCheckpoint, float speed, I3D_frame *rider);   // 0x005d8f60
};

// C_MeleeTrackerAI (C_MeleeTracker +0x534, the meleeTracker's AI controller) is in game/melee_tracker_ai.h.

class C_SpawnPoint : public C_Actor {                   // "SpawnPoint", vtable 0x008138e0
public:
    C_Actor *GetSpawnedActor() const { return m_spawned; }
    int GetTeam() const { return m_team; }
    // Which spawns it accepts; 2 = any (C_ActorsModule::FindRandomSpawnPoint).
    int GetKind() const { return m_kind; }
    bool IsEnabled() const { return m_enabled; }
private:
    int m_team;                                         // +0x90
    int m_kind;                                         // +0x94
    uint8_t m_unknown98[4];
    bool m_enabled;                                     // +0x9C
    uint8_t m_unknown9D[0x0B];
    C_Actor *m_spawned;                                 // +0xA8
};

// C_Car (game/car.h) with its vehicle C_Vehicle (C_Car +0x90, game/vehicle.h).

class C_Door : public C_Actor {                         // "Door", vtable 0x00812ff8
public:
    // action 2 = open, 3 = close (4 = lock, 5 = locked rattle)
    void SetState(int action, bool animate, bool silent, int unused);   // 0x005ff4b0
    bool Lock(int key);                                 // 0x005ff650
    bool Unlock(int key);                               // 0x005ff5f0
    int GetLockKey() const;                             // +0x230
    int GetState() const;                               // 0x006029f0
    // (+0x224, the door's motion: 0 closed, 1 open, 2 and 3 moving (inferred: the AI waits
    // at 2 and opens it at 0 and 3; GetState() gives it unless the door is locked).)
    int GetUnknown224() const;
    bool IsLocked() const;                              // byte +0x234 (GetState() 4)
    // (Byte +0x235, not identified: a locked door with it opens without the key.)
    bool GetUnknown235() const;
    // (Byte +0x118, not identified: the interface does not pick a door with it from farther
    // than 0.65 m.)
    bool GetUnknown118() const;
    // (+0xFC / +0x100, not identified: without +0x118, either set lets the signals offer the
    // door's command 0x12.)
    int GetUnknownFC() const;
    int GetUnknown100() const;
    // The side of the door `pos` is on (0 / 1; 2: a door without sides, +0x220 == 1).
    int GetSide(const S_vector4 &pos) const;            // 0x00602320
    // A completed lockpicking: a closed, locked door that can be picked opens its lock
    // (and its other half's).
    void PickLock();                                    // 0x005ff6c0 (__fastcall)
};

// Actors with a script-visible state (SetActorState / _ACTOR_GetState).
class C_Radio : public C_Actor {
public:
    void SetState(int state);                           // 0x0060b570
    int GetState() const;                               // +0xA0
    // The item type it needs (+0x98).
    int GetItemId() const;
    // `human` uses it (when the radio needs an item, it must carry it).
    bool Use(C_Human *human);                           // 0x0060b4c0
    // `human` may use it: it is on (state 1), he carries its item (if it needs one) and it is
    // his side's (+0x9C; 2: either side's).
    bool CanUse(C_Human *human) const;                  // 0x0060b460
};
class C_SupplyBox : public C_Actor {
public:
    void SetEnabled(bool on);                           // 0x005fa290
    // Items left to take (when limited).
    int GetCharges() const;                             // +0x130
    bool IsLocked() const;                              // +0x250 != 0
    bool IsLimited() const;                             // flag 1 of +0x124
    // The box hands out the items of its lists; without lists, ammunition for the weapon
    // in the taker's hands.
    bool HasItemLists() const;                          // +0x11C
    // Item types for the axis / allied side (vectors at +0xFC / +0x10C).
    const int *GetItemList(bool allies) const;
    int GetItemCount(bool allies) const;
    // (+0x128: the signals offer an item to a member who can take no more of it only while he
    // carries fewer than this many.)
    int GetMaxCarried() const;
    // One item was taken: one charge less (empty boxes switch off) and the clients are told
    // (inline in the original: 0x005fcd80, 0x005fd760).
    void UseCharge();
};
class C_Switch : public C_Actor {
public:
    void SetState(int state);                           // 0x006136f0
    int GetState() const;                               // +0xD4
    // Flips it to the other state.
    void Toggle();                                      // 0x00613640 (__fastcall)
};
class C_PhotoPlace : public C_Actor {
public:
    int GetState() const;                               // byte +0x91
    // `human` photographs it (byte +0x91 = 1; `human` is not used).
    void TakePhoto(C_Human *human);                     // 0x0060c130
};
class C_StaticWeapon : public C_Actor {
public:
    void SetState(int state);                           // 0x006059b0
    int GetState() const;                               // +0x2F0
};
class C_ExplosivePlace : public C_Actor {
public:
    void SetState(int state);                           // 0x0060d300
    // 0: empty, 1: the explosive is placed (inferred from the interactions).
    int GetState() const;                               // +0x94
    // `human` places the explosive it carries.
    bool Place(C_Human *human);                         // 0x0060d6f0
    // The item type of the explosive (+0xA0).
    int GetItemId() const;
    bool Disarm(C_Human *human);                        // 0x0060d9f0
    // Ms until the placed explosive goes off (+0xC8; the HUD's explosive timer).
    int GetTimeLeft() const;
};
class C_DocumentPlace : public C_Actor {
public:
    void SetState(int state);                           // 0x0060eac0
    int GetState() const;                               // +0x148
    // `human` delivers the documents it carries.
    bool Deliver(C_Human *human);                       // 0x0060e9b0
    // The item type of the documents (+0x98).
    int GetItemId() const;
    // The text of its signal (+0x9C).
    int GetTextId() const;
};
class C_Submarine : public C_Actor {                    // "Submarine", vtable 0x00812650
public:
    // The pilot's controls: forward, sideways, up (+0x100..+0x108).
    void SetControls(float forward, float side, float up);   // 0x005e0dd0
    // (+0xF4, inferred: the speed, which the squad lets run down to 2.)
    float GetSpeed() const;
    void SetSpeed(float speed);
    void SetUnknownF8(float value);                     // +0xF8
};
// The submarines of the mission (the list at 0x008ac9a8).
const std::vector<C_Actor *> &GetSubmarines();          // 0x005de7b0
class C_Pike : public C_Actor {
public:
    void SetState(int state);                           // 0x005b8ea0 (2 = up, 3 = down)
    int GetState() const;                               // +0x184
};
class C_Junkers88 : public C_Actor {
public:
    void SetState(int state);                           // 0x005f0a90
    // (+0x280, not identified: the squad's gunners fire at it only while nonzero.)
    float GetUnknown280() const;
};
class C_TSExplosion : public C_Actor {
public:
    void Trigger(int unused);                           // 0x0060fb10 (the script VM passes 1)
    int GetState() const;                               // +0x90
};

class C_Train : public C_Actor {                        // "Train", vtable 0x00811e78
public:
    void Start();                                       // 0x005bc970
    // Follows the named track (or stops following when `on` is false).
    void SetTrack(bool on, const char *track);          // 0x0041d590
};

class C_FowlGenerator : public C_Actor {
public:
    void Start(bool on, int count);                     // 0x005bb140
};

class C_AnimationActor : public C_Actor {               // "Animation", vtable 0x008119b8
public:
    void Stop(bool immediately);                        // 0x005b55e0
    void RequestStopAtEnd();                            // byte +0xB8 = 1
    bool IsStopped() const;                             // byte +0xA1
    // (Byte +0xA4, not identified: the signals offer nothing on the animation while it is set.)
    bool GetUnknownA4() const;
    // (+0xD8, not identified: 2 makes it armoured for the squad's weapon choice.)
    int GetUnknownD8() const;
};

// Breakable fence (C_TemporaryActorsList::CreateBreakables).
class C_WireCut : public C_Actor {
public:
    // Cuts the fence along `dir` (the original copies 4 floats from it); `hideBrokenPart`
    // also hides the door's broken part (the script VM: true, the AI: false).
    bool Cut(const S_vector &dir, bool hideBrokenPart); // 0x005e2510
    bool IsCut() const;                                 // 0x005e24c0
    void SetDoor(class C_PhysDoor *door) { m_door = door; }
    // The walking points on either side of the fence (CHECKPOINT_NONE: none).
    int GetPoint(int side) const { return m_points[side]; }
private:
    class C_PhysDoor *m_door;                           // +0x90
    uint16_t m_points[2];                               // +0x94
};

class C_AirAttack : public C_Actor {
public:
    void SetTarget(bool stop, I3D_frame *target);       // 0x005e7570
    // m/s along its z axis (the squad's gunners lead it by it).
    float GetSpeed() const;                             // +0x148
};

// C_ItemActor ("Item", vtable 0x00811aa8, 0x6c0 bytes) is defined in game/item_actor.h, included above.

// Multiplayer zone (actor type 37): a timed objective, a spawn area of a team.
class C_MPZone : public C_Actor {                       // "MPZone", vtable 0x00812b20
public:
    // Its number in the occupation's order.
    int GetNumber() const;                              // +0x90
    // The team it belongs to (spawns there).
    int GetTeam() const;                                // +0x98
    // The side EnableSpawnZone() gave it (-1: none).
    int GetSide() const;                                // +0xA4
    void EnableForSide(int side);                       // 0x005ed900 (+0xE4)
    // Its next spawn position (cycling through them; `cycle` false: false once all were used).
    bool PickSpawnPosition(S_vector4 *pos, S_vector4 *dir, bool cycle);   // 0x005ed700
    // The spawn positions start over (+0x100 = 0).
    void ResetSpawnIndex();
    // The zone's countdown runs.
    bool IsRunning() const;                             // byte +0xF1
    // Progress bar of the zone (time left / total in ms, text id).
    bool GetProgress(int *time, int *total, int *textId) const;   // 0x005ec840
};
class C_Mine : public C_Actor {                         // "Mine", vtable 0x00812f08
public:
    int GetItemId() const { return m_itemId; }
    // Disarms it (and tells the clients; the server removes it).
    void Defuse();                                      // 0x005feee0 (__fastcall)
    // Who laid it (null for mines placed in the mission).
    C_Human *GetOwner() const { return m_owner; }
    // A human stepped on the trigger volume, or a car's wheel met it (the culprit: the layer, else
    // the human or the car's driver; item 0x47 ignores humans, 0x46 cars).
    void OnSteppedOn(C_Actor *actor, int unused);       // 0x005fefb0 (the human passes -1, the car the wheel)
private:
    int m_itemId;                                       // +0x90 (set by SetupMine())
    uint8_t m_unknown94[8];
    C_Human *m_owner;                                   // +0x9C
};

// C_MeleeTracker ("MeleeTracker", vtable 0x008120e0, 0x5b0 bytes) is defined in game/melee_tracker.h, included above.

// The rigid body (C_RigidBody) and its parameters: see game/rigid_body.h, included above.

// C_Debris ("Debris", vtable 0x00812e18, 0x6a0 bytes) is defined in game/debris.h, included above.

// ---------------------------------------------------------------------------
// Modules


// Teams, formations, the team manager: see game/team.h.

// Particles module: see game/particles.h.

// Sound module: see game/sound_module.h.

enum E_FadeDirection {
    FADE_IN = 0,
    FADE_OUT = 1,
};

// (the module object itself is declared in pending.h)
class C_GEffectsModule;

// Humans whose movement takes an effect value from the camera (list at +0x8C of the
// object the camera module returns; C_Human::SetUnknown94).
class C_CameraHumanEffects {
public:
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

class C_CameraModule {                                  // "CameraModule", vtable 0x0080fdd0
public:
    // (C_Module::Update; the game calls it itself while paused.)
    void Update(int timeDelta);                         // vtable +0x10
    C_CameraHumanEffects *GetHumanEffects();            // vtable +0x88
    // The game camera eases into its next position over `time` ms.
    void SetBlendTime(int time);                        // vtable +0x70
    // Special camera `mode` on or off; mode CAMERA_MODE_PROJECTILE follows `target`.
    void SetSpecialMode(int mode, bool on, I3D_frame *target);   // vtable +0x44
    // The same slot with 1 as the third argument (the game's free camera; not identified).
    void SetSpecialMode(int mode, bool on, int unknown);
    // Called when the active soldier moves into another sector (not identified).
    void Unknown40(int arg);                            // vtable +0x40
    bool IsGameCamera(I3D_frame *camera);               // vtable +0x4C
    // Position and view direction of camera `index` (0: the scene's camera, 1: the game camera;
    // false without one). They write a whole S_vector4.
    bool GetCameraPos(int index, S_vector4 *pos);       // vtable +0x64 0x00465a10
    bool GetCameraDir(int index, S_vector4 *dir);       // vtable +0x68 0x00465b40
    // The game camera's field of view (radians; 80 degrees without one) and its second one
    // (camera +0x12C through I3D_camera slot 0x78; the first-person view sets 65 degrees, 40
    // through the sights).
    bool SetFov(float fov);                             // vtable +0x50 0x00465e50
    float GetFov();                                     // vtable +0x54 0x00465e80
    bool SetFov2(float fov);                            // vtable +0x5C 0x00465e00
    float GetFov2();                                    // vtable +0x60 0x00465e30
    // The direction the player's camera looks along (not in mode 10 or while the target's
    // flag 8 is set; +0x20).
    void SetViewDir(const S_vector4 &dir);              // vtable +0x48 0x00465ea0
    // The object at +0x2B4 is set (a scripted view, inferred): the player's controls are
    // dropped meanwhile.
    bool HasScriptedView();                             // vtable +0x80 0x00465080
    // The view distance of the scene's active camera (its +0x128; the AI's sight range limit).
    float GetViewDistance();                            // vtable +0x58
    // The camera frame in use (a human's "camera3RD" / "cameraFPV" dummy when it follows one).
    I3D_frame *GetCamera();                             // vtable +0x6C
    // Makes the game camera watch `frame`; speed -1 = cut, else degrees per second.
    void SetTarget(I3D_frame *frame, int speed);        // vtable +0x74
    // The byte at +0x26D (not identified; the player-driven meleeTracker ignores the mouse while it
    // is set).
    bool GetUnknown78();                                // vtable +0x78 0x00465060
    bool GetState();                                    // vtable +0x7C (1 = underwater)
    bool IsUnderwater() { return GetState(); }
    // Sector the camera (the sound listener) is in.
    I3D_sector *GetListenerSector();                    // vtable +0x8C
};
C_CameraModule *GetCameraModule();                      // 0x00464e90 (&g_cameraModule)
// C_CameraModule::SetSpecialMode() modes.
enum : int {
    CAMERA_MODE_AIM = 7,                                // the first-person view through the sights / scope
    CAMERA_MODE_PROJECTILE = 9,                         // rides along with a fired projectile
};

struct S_TShiftEntry {                                  // 0x1C bytes
    int id;
    int unknown04;
    bool on;                                            // +0x08
    bool reverse;                                       // +0x09
};

class C_TShiftModule {                                  // "TShift", vtable 0x00810418
public:
    bool HasFrame(I3D_frame *frame) const;              // 0x0048e2d0
    S_TShiftEntry *GetEntry(uint32_t id);               // 0x0048e780
};
extern C_TShiftModule g_tshiftModule;                   // 0x0089bc34

class C_LightMapGroupsModule {                          // "LMapsGrp", vtable 0x00810398
public:
    void *FindGroup(const char *name) const;            // 0x0041d5e0
};
C_LightMapGroupsModule *GetLightMapGroups();            // 0x0048cb00

class C_Decorators {
public:
    void SetOn(bool on);                                // 0x0047b010
};
extern C_Decorators *g_decorators;                      // 0x0089bbb0

class C_Junkers88Setup {
public:
    void SetInterceptors(float firepower, float health);   // 0x005eddc0
};
extern C_Junkers88Setup *g_junkers88Setup;              // 0x008ac9e4

// Checkpoints (AI navigation graph): see game/checkpoints.h.

// ---------------------------------------------------------------------------
// Interface, profile, mission state

// The game interface (the HUD): see game/interface.h.
class C_Interface;

// The player's profile, diary and debriefing: see game/profile.h.

void SetObjectiveStatus(int objective, int status);    // 0x006b6830
// True while the network player with this id is connected.
bool IsNetPlayerConnected(int netId);                   // 0x00730970
// Enables a multiplayer spawn zone for a side (0 axis, 1 allied, 2 neutral).
bool EnableSpawnZone(const char *zoneName, int side);   // 0x0069f320

// Values kept across missions (SaveGameValue / _LoadGameValue), stored in the
// script module (C_ScriptModule::GetSaveGameValues(), address 0x00868b3c).
constexpr int SAVE_GAME_VALUES = 256;

// An explosion with area damage (MakeExplosion).
struct S_Explosion {                                    // 0x20 bytes
    S_vector pos = {0, 0, 0};
    float posW = 0.0f;
    float range = 10.0f;                                // +0x10 m
    float damage = 1.0f;                                // +0x14
    bool unknown18 = false;
    C_Actor *source = nullptr;                          // +0x1C who caused it

    void Execute();                                     // 0x006df540
};

// Engine-side hooks on the "a" child of animated models.
void AttachModelUpdateHook(I3D_frame *model);           // 0x007764d0
void DetachModelUpdateHook(I3D_frame *model);           // 0x00776520
// The speech slots (0x008cb8e0, 16 x 0x44 bytes): each plays one dubbing sample at a speaker
// and moves the speaker's mouth from the dubbing's lip-sync data (tables\dabing\<n>.dat, one
// frame per 20 ms) on the channels of the model's 'SMMR' morph visual (the "a" child, as in
// C_HumanMorph). States: 0 free, 1 attached, 2 playing, 3 closing the mouth (200 ms), 4 a delay
// before free, 5 plays like 2 but its sound is never released here. Preceded by a list of morph
// fades (0x008cb8d4, std::vector of 16-byte entries) that the update also runs.
// Neutral mouths, every slot freed and emptied (cutscene start / end).
void ResetSpeechSlots();                                // 0x0071e680
// The mission's start / end with the table (client side): emptied without freeing, and freed.
void ClearSpeechSlots();                                // 0x0071e1e0
void FreeSpeechSlots();                                 // 0x0071e230
// Frame updates of the slots (the game's Update): playback, lip sync, mouth closing, the fades.
void UpdateSpeechSlots(int timeDelta);                  // 0x0071f1d0
// Updates the two extra frames of each slot (the C_Human +0x2B8 / +0x2BC frames; never set here).
void RefreshSpeechSlotFrames();                         // 0x0071f100
// Pausing stops the playing samples and keeps their positions; resuming restarts slots in state 2.
void PauseSpeechSlots(bool paused);                     // 0x0071fa10
int FindFreeSpeechSlot();                               // 0x0071e2c0 (15 when all are in use)
// Ends the speech playing at `owner` (state 3: the mouth closes).
void StopFrameSpeech(I3D_frame *owner);                 // 0x0071e830
// Binds slot `slot` to a speaker (its frame and morph visual; the mouth neutral).
void AttachSpeechSlot(int slot, C_Human *owner);        // 0x0071e300
// A slot plays speech (state 2) at the human's frame.
bool IsHumanSpeaking(const C_Human *human);             // 0x0071f090
// Neutral mouth, frees and empties the slots of the human's frame (before a uniform change
// replaces the model and its morph visual).
void ReleaseHumanSpeech(const C_Human *human);          // 0x0071ea20
void AttachSpeechSlot(int slot, I3D_frame *frame);      // 0x0071e860
// Plays dubbing number `dubbing` in slot `slot`: loads tables\dabing\<dubbing>.dat (the lip-sync
// data) into the slot and the <dubbing>.wav sample (0x0071ebc0) into its sound frame (ranges
// nearRange..farRange; I3DSOUND_AMBIENT for the active player, else I3DSOUND_POINT);
// `showSubtitle` also passes the dubbing to the quick messages (0x0048f180, with the sample's length).
void PlaySpeechSlot(int slot, int dubbing, float nearRange, float farRange, bool showSubtitle,
                    bool isActivePlayer);               // 0x0071ec30 __fastcall

// Model / animation / sound caches: see game/resource_cache.h.
