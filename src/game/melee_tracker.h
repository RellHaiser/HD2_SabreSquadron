// C_MeleeTracker — the meleeTracker actor (vtable 0x008120e0, 0x5b0 bytes). Source around 0x005d2000..0x005d86b0.
//
// The meleeTracker embeds its movement body (C_MeleeTrackerMovement at +0x90, the part-1 port) and owns a
// controller on the heap (C_MeleeTrackerController* at +0x534, one of four variants; the part-2 port). It keeps the sensing/steering
// state the AI reads each frame: a list of sightings (+0x564, pooled 0x20-byte S_MeleeTrackerSighting from
// 0x008ac938) and the steering fields at +0x580.. The actor drives the body + AI every tick and
// swaps the AI controller by behaviour mode (FUN_005d5890).
//
// Like the other actors, the port is not byte-exact to the binary (the engine's S_vector4 is
// 16-byte aligned, the port's is 4-byte, so the compiled layout is smaller): the `+0xNNN` comments
// are the ORIGINAL offsets and the code accesses everything by name. The save format is preserved
// because save/load is field-by-field through the chunk API. The lifecycle (ctor/dtor/save/load),
// the sensing and the per-frame update are ported in game/melee_tracker.cpp over the part-3 increments.
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/actor.h"
#include "game/melee_tracker_ai.h"
#include "game/melee_tracker_movement.h"

class C_chunk;
class C_Human;
class I3D_frame;
struct S_HeardSound;

// A remembered sighting / heard sound (0x20 bytes). Pooled from the composite pool's second
// sub-pool (0x008ac938); saved to disk as raw memory, so it is byte-exact. The meleeTracker keeps a list of
// these at +0x564 and the AI culls the stale ones.
struct S_MeleeTrackerSighting {
    S_vector4 pos = {};                                 // +0x00 where it was seen / heard
    int type = 0;                                       // +0x10 the sense group (sound group / sight)
    C_Actor *source = nullptr;                          // +0x14 the actor seen / heard
    uint32_t time = 0;                                  // +0x18 when (microseconds, low half)
    int loudness = 0;                                   // +0x1C how loud / strong (the AI alarms when > 10)
};
static_assert(sizeof(S_MeleeTrackerSighting) == 0x20, "S_MeleeTrackerSighting layout");

// The tracker's tunables (0x10 bytes, saved raw). The script commands set the ranges; StartMission
// keeps a copy that StopMission / RestartMission put back.
struct S_MeleeTrackerStats {
    int health;                                         // +0x0 IsAlive: > 0
    int biteDamage;                                     // +0x4 the bite's Hit amount
    int sightRange;                                     // +0x8 m (OnSightEvent; the script's SetSightRange)
    int hearRange;                                      // +0xC (the script's SetHearRange)
};
static_assert(sizeof(S_MeleeTrackerStats) == 0x10, "S_MeleeTrackerStats layout");

// Fills a sighting from the meleeTracker's pool (0x005ce560, sub-pool B of 0x008ac920); timeHigh is left 0
// for the caller. ReleaseMeleeTrackerSighting returns it.
S_MeleeTrackerSighting *AllocMeleeTrackerSighting(int type, const S_vector4 &pos, C_Actor *source, uint32_t time);
void ReleaseMeleeTrackerSighting(S_MeleeTrackerSighting *sighting);

class alignas(16) C_MeleeTracker : public C_Actor {             // vtable 0x008120e0, 0x5b0 bytes
public:
    C_MeleeTracker();                                            // 0x005d4b00 (via 0x005d4a60)
    ~C_MeleeTracker() override;                                  // 0x005d4cf0 (0x005d4cc0)

    // --- C_Actor overrides ------------------------------------------------------------
    // (OnMessage 0x1C 0x005d4c90 and slot 0x20 0x005d4ca0 are empty, as the base's.)
    // The base binding; the tracker's frame must be an animated model.
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005d4d70
    // The mission file's actor properties: a version word (1) and the stats.
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x005d65d0
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005d6630
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x005d4c80 (the base's only)
    // Activates; the server side loads the shared animation table if needed, remembers the stats and
    // the frame's placement for the restarts, sets the frame up and binds the body.
    void StartMission(bool server) override;            // 0x3C 0x005d5290
    // Deactivates; the server side drops the controller, the head override, the lists and the
    // registrations, releases the shared animation table, reloads the model and puts the stats back.
    void StopMission(bool server) override;             // 0x40 0x005d5390
    // Back to the mission start: stats, lists, controller, placement, then the frame setup again.
    void RestartMission(bool server) override;          // 0x44 0x005d45f0 (`server` unread)
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005d3310
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005d3a50
    // A client takes the health the server tells it (the first argument): 0 or less kills, else
    // the hit reaction. (The caller is not identified.)
    void UnknownD0(int health, int, int, int) override; // 0xD0 0x005d7300
    // Server: damage by where it hit (HitDamage; amount -1 kills). Death stops the controller,
    // lays the body down and leaves the sight / sound modules; a wound plays the hit reaction and
    // tells the controller who did it.
    void Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
             int flags) override;                       // 0xD4 0x005d7180
    // The tracker's surface material.
    int GetMaterialId(I3D_frame * /*part*/) override { return 0x42; }   // 0xD8 0x005d4cb0
    // The tracker's own virtual (vtable +0xE0): reloads the model from its file and puts the frame
    // back at the mission-start placement.
    virtual void ReloadModel();                         // 0xE0 0x005d5560

    // --- accessors (the stubs that used to live in world.h) ------------------------
    C_MeleeTrackerController *GetAI() const { return m_ai; }   // +0x534 (null until the frame setup)
    // The controller as the full AI (modes 4 / 5) / as the scripted AI (mode 5); null otherwise.
    C_MeleeTrackerAI *GetFullAI() const;
    C_MeleeTrackerScriptedAI *GetScriptedAI() const;
    I3D_frame *GetBodyFrame() const { return m_bodyFrame; }   // +0x544
    // The body's two collision volumes (+0x26C; the sight module ignores them on its rays).
    S_CollisionVolume **GetBodyVolumes() { return m_movement.m_volumeA; }
    int GetSightId() const { return m_sightId; }        // +0x540
    bool IsAlive() const { return m_stats.health > 0; } // +0x4A0 > 0 (0x0041d780)
    uint32_t GetAlarmType() const { return static_cast<uint32_t>(m_ai->GetAlarmValue()); }  // AI +0x1C
    C_MeleeTrackerMovement &GetMovement() { return m_movement; }
    const C_MeleeTrackerMovement &GetMovement() const { return m_movement; }
    S_MeleeTrackerStats &GetStats() { return m_stats; }
    const S_MeleeTrackerStats &GetStats() const { return m_stats; }

    // Collision of the body on / off (the script's SetOptimizedController).
    void SetOptimizedController(bool on) { m_movement.SetOptimizedController(on); }   // 0x0041d7a0
    void SetOptimized(bool on);                         // 0x005d5870
    // Replaces the controller with a new one of variant `mode` (1 the player's, 4 autonomous, 5
    // scripted, 7 a client's mirror), handing it the old one's transform and owner.
    void SetController(int mode);                       // 0x005d5890

    // --- per frame / per tick --------------------------------------------------------------------
    // Every frame: motion tracking, the head look, the body animation and the frame.
    void Update(int frameTime) override;                // 0x14 0x005d5810
    // Every game tick: the fall-out check, the AI, the body sequence + move, the look behaviour.
    void Tick(int timeDelta) override;                  // 0x18 0x005d5620
    // The velocity over the last frame from the frame's position (read by the AI's FollowPath).
    void TrackVelocity(int timeDelta);                  // 0x005d64e0
    // Turns the head bone toward m_lookPoint through the head override, at a rate by
    // m_lookRateMode, at most ~56 degrees either side; ends a "turn back" once straight ahead.
    void HeadLook(int frameTime);                       // 0x005d7a10
    // Counts down the look-around timers and picks the next look point.
    void UpdateLookBehaviour(int timeDelta);            // 0x005d82b0
    // Starts look behaviour `request` (state = request & ~0x11; bit 4 = a faster turn) at `target`.
    void SetLookBehaviour(const S_vector4 &target, uint32_t request, uint32_t param);   // 0x005d8070
    // Releases the script's hold on the tracker (the controller's vtable +0x3C).
    void ResetAI();                                     // 0x0041d790

    // The sight module's observer callback: a target entered view (lost == 0) or left it. Keeps the
    // seen-enemy list (+0x574) and alarms the AI.
    void OnSightEvent(int lost, int targetIndex);       // 0x005d5cb0
    // The AI's view test (none in the AI's mode 1; the sight module's tests).
    void TestSight(C_Actor *target, int *status, float *visibility);   // 0x005d64c0
    // The sound module's listener callback (AddListener in SetupFrame): a sound the tracker can hear.
    // Sounds of the player's side within the hearing range become (or refresh) sightings with a
    // loudness by kind, and the controller is told.
    static void __stdcall OnSoundHeard(const S_HeardSound *sound);   // 0x005d6000
    // Every second (Tick): the player humans within the hearing range are "heard" as footsteps,
    // fainter with the distance.
    void HearPlayers();                                 // 0x005d7760

    // --- layout --------------------------------------------------------------------
    C_MeleeTrackerMovement m_movement;                           // +0x090 the body (0x410, ends +0x4A0)
    S_MeleeTrackerStats m_stats = {250, 50, 20, 100};   // +0x4A0 (save chunk 0, with the next)
    S_MeleeTrackerStats m_startStats = {};              // +0x4B0 the stats at StartMission
    S_vector4 m_startPos = {};                          // +0x4C0 the frame's world position at StartMission (chunk 1, xyz)
    S_quat m_startRot = {};                             // +0x4D0 its world rotation (chunk 2, file order s,x,y,z)
    // --- the procedural head look (the head bone turned toward a point; SetLookBehaviour /
    // UpdateLookBehaviour pick the point, HeadLook turns the bone) -----------------------------
    I3D_joint_override_set *m_headOverrides = nullptr;  // +0x4E0 on the model; item 0 turns the head (SetupFrame)
    I3D_frame *m_headFrame = nullptr;                   // +0x4E4 the head bone "f"
    bool m_headTurning = false;                         // +0x4E8 the bone override is applied (save chunk 3)
    bool m_lookActive = false;                          // +0x4E9 (save chunk 4)
    uint8_t _pad_4ea[6] = {};
    S_vector4 m_lookPoint = {};                         // +0x4F0 the point being looked at (save chunk 5, xyz)
    float m_headYaw = 0.0f;                             // +0x500 the head's current yaw, rad (save chunk 6)
    bool m_lookReturn = false;                          // +0x504 turn back to straight ahead, then stop (chunk 8)
    bool m_lookReached = false;                         // +0x505 the head is on the point (save chunk 9)
    uint8_t _pad_506[2] = {};
    float m_lookSpeed = 0.0f;                           // +0x508 turn-rate factor (save chunk 10)
    int m_lookRateMode = 0;                             // +0x50C turn-rate curve 0..2 (save chunk 11; setup: 1)
    S_vector4 m_lookTarget = {};                        // +0x510 the next point (save chunk 12, xyz)
    int m_lookState = 0;                                // +0x520 2/4/6/8 (SetLookBehaviour; save chunk 13)
    int m_lookParam = 0;                                // +0x524 its parameter / countdown ms (save chunk 14)
    int m_lookTimer = 0;                                // +0x528 ms to the next look, -1 off (save chunk 15)
    int m_lookRepeats = 0;                              // +0x52C how many looks (save chunk 16)
    bool m_optimized = false;                           // +0x530 out of simulation (SetOptimized; save chunk 17)
    uint8_t _pad_531[3] = {};
    C_MeleeTrackerController *m_ai = nullptr;           // +0x534 the controller (heap)
    uint8_t m_538 = 0;                                  // +0x538
    uint8_t _pad_539[3] = {};
    // (The original leaves these three unset until the frame setup; the port starts them at -1, none.)
    int m_sightObserverId = -1;                         // +0x53C its observer id in the sight module
    int m_sightId = -1;                                 // +0x540 its target id in the sight module
    I3D_frame *m_bodyFrame = nullptr;                   // +0x544 the frame the sight test measures to
    int m_soundListener = -1;                           // +0x548 its listener index in the sound module
    uint32_t m_54c = 0;                                 // +0x54C the players container's lead word
    // The player humans at the frame setup (role HUMAN_ROLE_PLAYER; the hearing scan walks them).
    C_Human **m_playersBegin = nullptr;                 // +0x550
    C_Human **m_playersEnd = nullptr;                   // +0x554
    C_Human **m_playersCap = nullptr;                   // +0x558
    int m_55c = 0;                                      // +0x55C ms since the last sound scan (save chunk 18)
    uint8_t _pad_560[4] = {};                           // +0x560
    S_MeleeTrackerSighting **m_sightingsBegin = nullptr;         // +0x564 the sighting list (save chunk 0x13)
    S_MeleeTrackerSighting **m_sightingsEnd = nullptr;           // +0x568
    S_MeleeTrackerSighting **m_sightingsCap = nullptr;           // +0x56C
    uint32_t m_570 = 0;                                 // +0x570 the seen-enemy container's lead word
    C_Actor **m_seenBegin = nullptr;                    // +0x574 humans currently in view (the sight event)
    C_Actor **m_seenEnd = nullptr;                      // +0x578
    C_Actor **m_seenCap = nullptr;                      // +0x57C
    // The actor's measured motion (TrackVelocity; FollowPath reads it).
    S_vector4 m_velocity = {};                          // +0x580 m/s over the last frame (w: the previous w)
    S_vector4 m_lastPos = {};                           // +0x590 the frame's world position last frame
    int m_lastFrameTime = 0;                            // +0x5A0 ms of that frame
    uint8_t _pad_5a4[12] = {};                          // +0x5A4 -> 0x5B0 (original end)

private:
    // The per-mission setup of the bound frame: the head override, the look and motion state, the
    // controller, the sight / sound registrations and the players list. Gives up (all of it) when
    // the model lacks one of its bones "b".."y".
    void SetupFrame();                                  // 0x005d4db0
    // Loads the shared animation table (Tables\dog_anim.sav) and the animations; false on failure.
    static bool LoadAnimTable();                        // 0x005d6700
    // Takes the head override off the model and releases it.
    void ReleaseHeadOverrides();                        // (inlined by Stop/RestartMission)
    // Returns every sighting to the pool and empties the list.
    void ReleaseSightings();                            // (inlined by Stop/RestartMission)
    // `amount` scaled by the bone nearest `pos`: "f" (the head look's bone) 2, "d" 1.5, "c" 0.65,
    // "h" / "p" / "t" / "l" 0.2.
    float HitDamage(const S_vector &pos, float amount) const;   // 0x005d73b0
    // The wound reactions shared by Hit and UnknownD0 (inlined in both): the hit-reaction animation
    // unless one is playing, and the hurt gait below an eighth of the starting health.
    void PlayHitReaction();
    void CheckBadlyHurt();
};

// The tracker's creator (g_actorCreators[ACTOR_MELEE_TRACKER], vtable 0x008120d0).
class C_MeleeTrackerCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005d4a60
    // The original's type name (game data, kept); nothing in the game reads it.
    const char *GetName() override { return "Dog"; }    // 0x005d4ad0
    void Destroy(C_Actor *actor) override;              // 0x005d4ae0
};
