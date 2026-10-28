// A door (C_Door, actor type 6, vtable 0x00812ff8, 0x270 bytes; original 0x005ff150..0x00602a10):
// a frame that turns about an axis through its pivot (DOOR_HINGED) or slides along a direction
// (DOOR_SLIDING) between closed and open, at a speed (rad / m a second), stopping while anything
// dynamic is in the way. Optionally two halves (the other half found by name: what one does, the
// other does), a lock with a key item, a lock that can be picked, handles ("dhandle", "d2handle")
// that turn as it starts, the portals it shuts ("portal doors"), closing by itself 15 s after
// being opened, and nine sounds. Its box (COLLISION_TYPE_ACTOR_DOOR) moves with it. Every change
// of the state goes to all the machines as a message (the sounds are played on it).
#pragma once

#include <cstdint>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/collision.h"
#include "game/sound_module.h"
#include "ls3d/math.h"

class C_BitStream;
class C_chunk;
class I3D_frame;
class I3D_portal;

// What SetState / the scripts ask a door to do.
enum : int {
    DOOR_ACTION_OPEN = 2,
    DOOR_ACTION_CLOSE = 3,
};

// The door's motion (GetMotion; GetState gives DOOR_STATE_LOCKED instead while locked).
enum : int {
    DOOR_CLOSED = 0,
    DOOR_OPEN = 1,
    DOOR_OPENING = 2,
    DOOR_CLOSING = 3,
    DOOR_STATE_LOCKED = 4,
};

// How a door moves (property).
enum : int {
    DOOR_HINGED = 0,
    DOOR_SLIDING = 1,
};

class alignas(16) C_Door : public C_Actor {             // "Door", vtable 0x00812ff8
public:
    C_Door();                                           // 0x005ff210
    ~C_Door() override = default;                       // 0x005ff3a0 (scalar deleting 0x005ff380)

    // DOOR_ACTION_OPEN / DOOR_ACTION_CLOSE (others: only the script told). The other half does the
    // same (`linked`: this is that call). The frame's script is told (OnUse). Opening a locked door
    // only rattles it (the message), unless `force`; it opens after the opening delay, closing by
    // itself 15 s later with `autoClose` (and the door's flag). Dropped: the copy protection's check
    // (single player, from campaign mission 13: a failed check kept every door shut).
    void SetState(int action, bool autoClose, bool linked, int force);   // 0x005ff4b0
    // With the door's key (`key` its item): unlocked (and the other half; the message 3), else the
    // message 5. True when it is unlocked.
    bool Unlock(int key);                               // 0x005ff5f0
    // Only a closed door, with its key: locked (and the other half; the message 4), else the message
    // 5 (a moving door: false, no message). True when it is locked.
    bool Lock(int key);                                 // 0x005ff650
    // A completed lockpicking: a closed, locked door that can be picked opens its lock (and its
    // other half's; the message 3).
    void PickLock();                                    // 0x005ff6c0
    // DOOR_STATE_LOCKED while locked, else the motion.
    int GetState() const { return m_locked ? DOOR_STATE_LOCKED : m_motion; }   // 0x006029f0
    int GetMotion() const { return m_motion; }          // +0x224
    int GetLockKey() const { return m_key; }            // +0x230 (-1: none)
    bool IsLocked() const { return m_locked; }          // +0x234
    bool CanPickLock() const { return m_pickable; }     // +0x235
    // (Property chunk 0x6D's byte, not identified: the interface does not pick a door with it from
    // farther than 0.65 m.)
    bool GetUnknown118() const { return m_unknown118; }
    // The portals a portal door shuts (null: none; the signals offer the door's command 0x12 for
    // one without +0x118).
    I3D_portal *GetPortal() const { return m_portal; }  // +0xFC
    I3D_portal *GetOtherPortal() const { return m_otherPortal; }   // +0x100
    // The side of the door `pos` is on (0 behind its facing, 1 in front; 2: a sliding door).
    int GetSide(const S_vector4 &pos) const;            // 0x00602320

    // The swing (always; Tick: also, but on a pure client).
    void Update(int frameTime) override { Animate(frameTime); }   // 0x14 0x00600940
    void Tick(int timeDelta) override;                  // 0x18 0x00600920
    // A state change (3 bits): 0 open, 1 close (the moving sound), 2 locked, 3 unlocked, 4 locked
    // now, 5 the wrong key (their sounds).
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x00601f90
    void Unknown20(C_BitStream *) override {}           // 0x20 0x006021d0
    bool SetFrame(I3D_frame *frame) override { return C_Actor::SetFrame(frame); }   // 0x2C 0x005ff3d0
    // The fields raw, then chunks 0x65 the sounds, 0x66 the portals flag, 0x67 the other half's
    // name, 0x68 closing by itself, 0x69 (not identified), 0x6A the facing (computed here: the
    // frame's world z, turned to the side a quarter turn about the axis moves the frame's bound
    // centre to), 0x6B shot through, 0x6C pickable, 0x6D the text and +0x118.
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x006013b0
    // Quirk kept: a chunk of another id is entered and not left (its contents are read as chunks).
    // A key 0 is none (-1); a portal door finds its portals.
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x00601ac0
    // The properties but the flags 0x98 / 0x99, the text, the other half's name, the facing and
    // the shot-through flag (the portals found again).
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x005ff3e0
    // Once until StopMission: the other half, the name (its text), the handles, the start placement,
    // the box (added), the sound; an open door opens again on the server. A portal door on a client
    // (not `server`): its portals, the frame under the scene's dynamic root.
    void StartMission(bool server) override;            // 0x3C 0x00600cc0
    // The portals opened, not started, the box out, the motion back.
    void StopMission(bool server) override;             // 0x40 0x00601350
    // The start placement, lock and motion back; an open door opens again on the server.
    void RestartMission(bool server) override;          // 0x44 0x00602400
    // Chunk 3000: how far open, locked, the motion, the delay, the frame's world position and
    // rotation (scalar first). Quirk kept: it returns false.
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x00602500
    // Quirk kept: a chunk of another id is entered and not left.
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x00602700
    // A joining client's: locked (1 bit), then the message 1 (closed / closing) or 0 (open / opening).
    void WriteState(C_BitStream *msg) override;         // 0x50 0x006021e0
    void ReadState(C_BitStream *msg) override;          // 0x54 0x00602290
    // The part's material, else the frame's (another part), else 11.
    int GetMaterialId(I3D_frame *part) override;        // 0xD8 0x006023b0

private:
    // Sounds (+0x240, property chunk 0x65); -1 none.
    enum : int {
        SOUND_OPEN = 0,                                 // starting to open
        SOUND_CLOSED = 1,                               // shut
        SOUND_MOVING = 2,                               // looped while it moves
        SOUND_LOCK = 3,
        SOUND_UNLOCK = 4,
        SOUND_WRONG_KEY = 5,
        SOUND_LOCKED = 8,                               // a locked door tried
        SOUND_COUNT = 9,
    };

    // Moves the door (DOOR_OPENING / DOOR_CLOSING) after its delay, the handles, closing by itself.
    void Animate(int time);                             // 0x005fffe0
    // The frame turned (from its start rotation) / slid (along its rotation turned three quarters
    // about the axis) to m_opened; true when something dynamic touches the box's edges or two of
    // its diagonals there (a turned door: its rotation back). Neither while opening. (The binary
    // passes Animate's time too, unused.)
    bool TurnTo();                                      // 0x005ff700
    bool SlideTo();                                     // 0x005ffbd0
    // The portals open (not `closed`).
    void SetPortalsClosed(bool closed);                 // 0x006007b0
    // The box follows the frame (a turning, opening door's type lets cars through).
    void UpdateVolume();                                // 0x006007f0
    // The nearest portal (10 m at most, within its radius) of the sector the door is in (its
    // incoming ones when it has none of its own) and its opposite.
    void FindPortals();                                 // 0x00600a50
    // The message `kind` (3 bits) to every machine.
    void SendMessage(uint32_t kind);                    // 0x00601f40
    // Plays sound `id` (-1: nothing).
    void PlaySound(int id);
    // The moving sound started (or resumed), not paused.
    void StartMovingSound();

    bool m_started = false;                             // +0x90
    int m_unknown94 = 0;                                // +0x94 (zeroed, not read)
    bool m_autoClose = true;                            // +0x98 chunk 0x68
    bool m_unknown99 = true;                            // +0x99 chunk 0x69 (not read)
    int m_autoCloseTime = 0;                            // +0x9C ms left
    bool m_hasPortals = true;                           // +0xA0 chunk 0x66
    S_SoundRequest m_sound;                             // +0xB0
    int m_movingSound = -1;                             // +0xF0 slot
    int m_movingSoundPos = 0;                           // +0xF4 where it was paused
    bool m_movingSoundPaused = false;                   // +0xF8
    I3D_portal *m_portal = nullptr;                     // +0xFC
    I3D_portal *m_otherPortal = nullptr;                // +0x100
    I3D_frame *m_handle = nullptr;                      // +0x104 "dhandle"
    I3D_frame *m_handle2 = nullptr;                     // +0x108 "d2handle"
    bool m_handleMoving = false;                        // +0x10C
    bool m_handleRising = true;                         // +0x10D
    float m_handleAngle = 0.0f;                         // +0x110 rad
    int m_textId = 0x1BF1;                              // +0x114 chunk 0x6D
    bool m_unknown118 = false;                          // +0x118 chunk 0x6D
    C_Door *m_otherHalf = nullptr;                      // +0x11C
    C_str m_otherHalfName;                              // +0x120 chunk 0x67
    int m_initialMotion = DOOR_CLOSED;                  // +0x124
    bool m_initialLocked = false;                       // +0x128
    S_CollisionVolume m_volume;                         // +0x130
    bool m_shootThrough = false;                        // +0x190 chunk 0x6B
    S_matrix m_invMatrix = {};                          // +0x1A0
    S_vector4 m_startPos = {};                          // +0x1E0
    S_quat m_startRot = {};                             // +0x1F0
    S_vector4 m_facing = {};                            // +0x200 chunk 0x6A
    S_vector4 m_axis = {0.0f, -1.0f, 0.0f, 0.0f};       // +0x210 (w not set by the original)
    int m_kind = DOOR_HINGED;                           // +0x220
    int m_motion = DOOR_CLOSED;                         // +0x224
    float m_openAngle = 1.5707964f;                     // +0x228 rad (m)
    float m_speed = 1.5707964f;                         // +0x22C a second
    int m_key = -1;                                     // +0x230 item
    bool m_locked = false;                              // +0x234
    bool m_pickable = true;                             // +0x235 chunk 0x6C
    int m_openDelay = 0;                                // +0x238 ms
    bool m_unknown23C = false;                          // +0x23C (not read)
    int m_sounds[SOUND_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};   // +0x240
    float m_opened = 0.0f;                            // +0x264 how far open (rad / m)
    int m_delay = 0;                                    // +0x268 ms before it moves
};

// The doors' creator (g_actorCreators[ACTOR_DOOR], vtable 0x00812fe8).
class C_DoorCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005ff180
    const char *GetName() override { return "Door"; }   // 0x005ff1e0
    void Destroy(C_Actor *actor) override;              // 0x005ff1f0
};
