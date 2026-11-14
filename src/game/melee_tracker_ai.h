// The meleeTracker's controllers (C_MeleeTracker +0x534): a small hierarchy rooted in
// C_MeleeTrackerController (see below) -- the full AI with its scripted (mode 5) and autonomous
// (mode 4) variants, a client's remote mirror (mode 7) and the player's control (mode 1).
// Original source 0x005cb0d0..0x005cfc70 (the controllers) and 0x005c9050..0x005cb090 (the A*
// pathfinder on the checkpoint network), save / load 0x005d2000..0x005d32a0.
#pragma once

#include <cstddef>
#include <cstdint>

#include "ls3d/math.h"

class C_MeleeTracker;
class C_MeleeTrackerMovement;
class C_chunk;
class C_Script;
class C_CheckpointNetwork;
class C_Actor;
class C_NetMessage;
struct S_Checkpoint;
struct S_AIWaypoint;
struct S_MeleeTrackerSighting;
class I3D_frame;

// The orders bitfield (C_MeleeTrackerController::m_flags, +0x20). The script commands set these; the update reads
// them. 0x40000 (ACTIVE) is set whenever the meleeTracker is told to do something.
enum : uint32_t {
    MELEE_TRACKER_ORDER_LIE = 0x00000001,                         // SetModeLie (clears STAND)
    MELEE_TRACKER_ORDER_STAND = 0x00000002,                       // SetModeStand (clears LIE)
    MELEE_TRACKER_ORDER_AGGRESSIVE = 0x00000100,                  // SetAggressive
    MELEE_TRACKER_ORDER_DEFENSIVE = 0x00000200,                   // SetDefensive
    MELEE_TRACKER_ORDER_HAS_TARGET = 0x00000400,                  // a target was heard/seen while defensive
    MELEE_TRACKER_ORDER_WALK = 0x00002000,                        // SetModeWalk (clears RUN; the initial mode)
    MELEE_TRACKER_ORDER_RUN = 0x00004000,                         // SetModeRun (clears WALK)
    MELEE_TRACKER_ORDER_ACTIVE = 0x00040000,                      // the meleeTracker has an order
    MELEE_TRACKER_ORDER_ATTACK = 0x00100000,                      // Attack
    MELEE_TRACKER_ORDER_MOVETO = 0x00200000,                      // MoveTo (with a path)
    MELEE_TRACKER_ORDER_SUSPEND = 0x40000000,                      // cleared on the next update
    MELEE_TRACKER_ORDER_ALARM = 0x80000000,                        // an alarm is pending for the script
};

// The root of the tracker's controllers (vtable 0x00812048, 0x70 bytes): the tracker, the orders, the
// owner, the alarm, the facing and the two behaviour-weight buffers Apply commits to the body.
// Most hooks are empty here. The variants (C_MeleeTracker::SetController picks one by mode):
//   C_MeleeTrackerAI            0x00811fd0  the full AI (sensing, target, heel, checkpoint paths)
//     C_MeleeTrackerScriptedAI  0x00812250  mode 5, 0xD0: a script drives it
//     C_MeleeTrackerAutoAI      0x008122c8  mode 4, 0xC0: on its own
//   C_MeleeTrackerRemote        0x008121e0  mode 7, 0x270: a client's mirror of the server's tracker
//   C_MeleeTrackerPlayerControl 0x00812340  mode 1, 0x4B0: the player drives the tracker
// (Each `new` in SetController inlines the root's field set-up; the port's constructors do it.)
class C_MeleeTrackerController {
public:
    explicit C_MeleeTrackerController(int mode);
    C_MeleeTrackerController(const C_MeleeTrackerController &) = delete;
    C_MeleeTrackerController &operator=(const C_MeleeTrackerController &) = delete;

    // --- the virtual interface (the slot offsets are the original's) ----------------------------
    // Binds the tracker and resets (SetMeleeTracker).
    virtual bool Attach(C_MeleeTracker *meleeTracker);  // +0x00 0x005ce630
    // The facing from the tracker; alarm, orders, owner and the buffers cleared.
    virtual void Reset();                               // +0x04 0x005ce5d0
    virtual ~C_MeleeTrackerController();                // +0x08 (0x005cb360); the body 0x005cb220 (DS 0x005e2b40)
    // Unbinds the tracker.
    virtual bool Detach();                              // +0x0C 0x005cb230
    // Takes over the transform and the owner of the controller this one replaces.
    virtual void ResetState(C_MeleeTrackerController *source);   // +0x10 0x005ce650
    // Every frame (C_MeleeTracker::Update) / every game tick (C_MeleeTracker::Tick).
    virtual void FrameUpdate(int /*frameTime*/) {}      // +0x14 0x005cb240
    virtual void Tick(int /*timeDelta*/) {}             // +0x18 0x005cb250
    // The tracker's sight test of `target` (for the sight module's observer): seen (*visibility
    // 100, else 0) inside a 40-degree triangle ahead of its "f" bone, as deep as the sight range
    // scaled by the target's stance / pace (lying 0.6 .. running 1.4). *status is 99 (tested);
    // nothing is written while the controller is suspended or disabled. (Every variant's.)
    virtual void TestSight(C_Actor *target, int *status, float *visibility);   // +0x1C 0x005ce740
    virtual void Unknown20(int /*unused*/) {}           // +0x20 0x005cb260 (empty in every variant)
    // The current target (none here).
    virtual C_Actor *GetTarget() const { return nullptr; }   // +0x24 0x005cb270 (DS 0x005e2be0)
    // How far the target is: 0 on top of it, FLT_MAX without one (always FLT_MAX here).
    virtual float DistanceToTarget() { return 3.4028235e+38f; }   // +0x28 0x005cb280 (DS 0x005e2c00)
    // Reads and clears the "changed" flag (none here).
    virtual bool TakeChanged() { return false; }        // +0x2C 0x005cb290 (DS 0x005e2c20)
    // The owner the tracker keeps to (and the "has owner" order bit 0x10).
    virtual void SetOwnerActor(C_Actor *owner);         // +0x30 0x005cb2a0
    C_Actor *GetOwner() const { return m_owner; }       // +0x34 0x005cb2c0 (DS 0x005e2c70) (the same in every variant)
    // Raises (value != 0) or clears (0) the alarm, without telling a script.
    virtual void SetAlarm(int value);                   // +0x38 0x005ce700
    // The script lets go of the tracker.
    virtual void ReleaseScript() {}                     // +0x3C 0x005cb2d0
    virtual void SetScript(C_Script * /*script*/) {}    // +0x40 0x005cb2e0
    // --- the sensing hooks (C_MeleeTracker calls them; empty here) -------------------------------
    // A bullet struck close by (OnSoundHeard; empty in every variant).
    virtual void OnBulletHit() {}                       // +0x44 0x005cb2f0
    // A sound became (or refreshed) a sighting.
    virtual void OnHeardSound(S_MeleeTrackerSighting * /*sighting*/) {}   // +0x48 0x005cb300
    // An enemy came into view / left it (OnSightEvent).
    virtual void OnSpottedEnemy(C_Actor * /*target*/) {}   // +0x4C 0x005cb310
    virtual void OnLostEnemy(C_Actor * /*target*/) {}   // +0x50 0x005cb320 (empty in every variant)
    // Hit by `actor` (C_MeleeTracker::Hit; `type` the hit type).
    virtual void OnSeenHuman(int /*type*/, C_Actor * /*actor*/) {}   // +0x54 0x005cb330
    // A network state / control message about the tracker (the remote and player controllers;
    // the dispatcher is not identified).
    virtual void OnNetState(C_NetMessage * /*msg*/) {}  // +0x58 0x005cb340
    virtual void OnNetControl(C_NetMessage * /*msg*/) {}   // +0x5C 0x005cb350
    virtual void PrepareSave() {}                       // +0x60 0x005d2570 (empty in every variant)
    // The root's chunk 0x22b8: owner id, alarm, orders, the transform.
    virtual bool SaveGame(C_chunk &chunk);              // +0x64 0x005d2000
    virtual bool LoadGame(C_chunk &chunk);              // +0x68 0x005d21d0

    // --- non-virtual ---------------------------------------------------------------------------
    // Commits the behaviour-weight buffers to the body (modes, sub-modes, the bark turn).
    void Apply();                                       // 0x005cec60
    // Stops the AI updating (order bit 0x800).
    void Disable();                                     // 0x005ce6e0
    // Binds the tracker and resets (Reset is virtual).
    bool SetMeleeTracker(C_MeleeTracker *meleeTracker); // 0x005ce630
    uint32_t GetOrders() const { return m_flags; }      // +0x20 the orders bitfield
    int GetAlarmValue() const { return m_alarm; }       // +0x1C (C_MeleeTracker::GetAlarmType reads this)
    C_MeleeTracker *GetMeleeTracker() const { return m_meleeTracker; }
    int GetMode() const { return m_mode; }              // +0x14 the variant (the body reads == 1)

    uint8_t m_unknown04[0x0C] = {};                     // +0x04 (after the vtable)
    C_MeleeTracker *m_meleeTracker = nullptr;           // +0x10
    int m_mode = 0;                                     // +0x14 the controller variant
    C_Actor *m_owner = nullptr;                         // +0x18 the tracker's owner (SetOwner)
    int m_alarm = 0;                                    // +0x1C the alarm value for the script
    uint32_t m_flags = 0x2000;                          // +0x20 MELEE_TRACKER_ORDER_*
    uint8_t m_unknown24[0x0C] = {};                     // +0x24
    S_vector4 m_savedPos = {0.0f, 0.0f, 1.0f, 0.0f};    // +0x30 the AI move accumulator (cleared by Apply)
    S_vector4 m_facing = {0.0f, 0.0f, 1.0f, 0.0f};      // +0x40 the desired facing (the remote: the net velocity)
    S_vector4 m_050 = {};                               // +0x50
    // The behaviour weights Apply reads (the AI's shared buffers, a variant's own, or the player's
    // control values).
    float *m_actionA = nullptr;                         // +0x60
    float *m_actionB = nullptr;                         // +0x64
    int m_068 = 0;                                      // +0x68 frames applied (the remote / player)
    uint8_t m_unknown6C[4] = {};                        // +0x6C
};

// The full AI (vtable 0x00811fd0, 0xC0 bytes; abstract in the original -- only its two variants
// are made): sensing, the target and the attack, heel, the checkpoint paths. Its own vtable's
// save / load (0x005d25b0 / 0x005d28b0) are unreachable and not ported.
class C_MeleeTrackerAI : public C_MeleeTrackerController {   // deleting dtor and dtor: 0x005cb3c0 (DS 0x005e2e60), 0x005cb3e0 (DS 0x005e2e90)
public:
    // --- script commands (C_MeleeTrackerScript::ExecuteCommand calls them directly) ----------
    void MoveTo(uint16_t checkpoint);                   // 0x005cf700
    void Attack(I3D_frame *target);                     // 0x005cf7c0
    void SetModeRun();                                  // 0x005cf820
    void SetModeWalk();                                 // 0x005cf830
    void SetModeStand();                                // 0x005cf850
    void SetModeLie();                                  // 0x005cf840
    void SetAggressive();                               // 0x005cf890
    void SetDefensive();                                // 0x005cf8a0
    void Suspend(bool on);                              // 0x005cf930
    void SetOptimized(bool on);                         // 0x005cf950
    void SetSightRange(int range);                      // 0x005cf980
    void SetHearRange(int range);                       // 0x005cf990
    void SetOwner(I3D_frame *owner);                    // 0x005cf9b0

    // --- lifecycle -------------------------------------------------------------------------------
    // The root's set-up for `mode`, then InitBase.
    explicit C_MeleeTrackerAI(int mode);                // 0x005cb190
    // Frees the path array (the waypoints are released by Detach first).
    ~C_MeleeTrackerAI() override;                       // 0x005d5b50
    // (Only while constructing: the variants override Reset.)
    void Reset() override;                              // +0x04 0x005cb0d0 (InitBase)
    // Empties the path.
    bool Detach() override;                             // +0x0C 0x005cb410 (ResetPath)

    // --- the update ----------------------------------------------------------------------------
    // The per-tick AI update: senses, decides the target / the heel, follows the path, and applies
    // the chosen behaviour to the body.
    void Update(int timeDelta);                         // 0x005ce190
    void FrameUpdate(int /*frameTime*/) override {}     // +0x14 0x005ce180
    void Tick(int timeDelta) override;                  // +0x18 0x005ce190 (Update)
    void Unknown20(int /*unused*/) override {}          // +0x20 0x005cb3a0
    C_Actor *GetTarget() const override { return m_target; }   // +0x24 0x005cb390
    // Distance to the current target: 0 on top of it, FLT_MAX when there is none.
    float DistanceToTarget() override;                  // +0x28 0x005cde60
    bool TakeChanged() override;                        // +0x2C 0x005cb380
    // The owner, and a path to the heel spot.
    void SetOwnerActor(C_Actor *owner) override;        // +0x30 0x005cd720
    // A bark at a sighting louder than 9, unless one is due.
    void OnHeardSound(S_MeleeTrackerSighting *sighting) override;   // +0x48 0x005cddd0
    // A bark at a spotted enemy, unless one is due.
    void OnSpottedEnemy(C_Actor *target) override;      // +0x4C 0x005cde20
    void OnLostEnemy(C_Actor * /*target*/) override {}  // +0x50 0x005cb3b0
    // Hit by a human: clears the chase of a lost target, or (defensive) takes it as the target.
    void OnSeenHuman(int type, C_Actor *actor) override;   // +0x54 0x005cdf30
    void PrepareSave() override {}                      // +0x60 0x005d2d90
    // The variant's behaviour weights from the orders.
    virtual void SubUpdate(int timeDelta);              // +0x6C 0x005ce110
    // How far the heel spot is (FLT_MAX without an owner; 0 / 1 / the distance).
    virtual float DistanceToOwnerSpot();                // +0x70 0x005cd7d0

protected:
    // The script of the scripted variant (mode 5), null for the others (the original reads +0xC0
    // after a mode check).
    C_Script *ScriptOf();

public:
    // The full AI's init: the checkpoint network, no targets, the target positions unset.
    void InitBase();                                    // 0x005cb0d0
    void UpdateTimers(int timeDelta);                   // 0x005cdf90
    // Owner-relative stay position.
    void OwnerPosition(float *out);                     // 0x005cd690
    // --- the decision stages ----------------------------------------------------------
    void CullSightings(int timeDelta);                  // forget stale sightings (5cdff0; arg unread)
    // The attack: the chase / bite / hold-back distances to the target, and the facing toward it;
    // with no target the autonomous controller starts an attack on anything seen or heard.
    void DecideTarget(int timeDelta);                   // 0x005cd1f0
    // Heel: keep to the owner's side spot -- run when far, walk at 1.0..1.4 speed closer, stop
    // within a margin that grows when the spot is behind; standing, face the owner's way.
    void DecideMove(int timeDelta);                     // 0x005cd890 (the time is unread)
    // Steers along the checkpoint path: walks while the next waypoint is within an angle of the
    // facing (else turns on the spot), drops waypoints reached or passed during the last frame.
    void FollowPath(int timeDelta);                     // 0x005cbf00 (the time is unread)
    // The path bookkeeping before steering: re-plans toward the target / the heel spot, advances a
    // MoveTo script near the last waypoint; true when the path ran out (nothing to steer by).
    bool AdvanceWaypoint();                             // 0x005cc420
    // Heel: near the owner's spot (no long path) close in; else re-plan when the spot moved over
    // 0.5 m. False when no path is found.
    bool CheckHeel(int unused);                         // 0x005ccec0 (callers pass 1; unread)
    // Faces `point` unless moving or ordered (a point at the origin means none).
    void FacePoint(int unused, const float *point);     // 0x005cb480
    // Returns the first waypoint to the pool and drops it from the path (inlined in FollowPath).
    void PopFirstWaypoint();
    // Picks the target: the ordered one, else the first live human (not in a vehicle) among the
    // seen enemies and the sightings louder than 19 to beat the score of the current one (`force`:
    // start over). Tells the owner (a "tracker alert" event).
    void FindTarget(bool force);                        // 0x005cc8d0
    // Scores `candidate` against the best so far (`*bestScore`, `*best`).
    void ScoreTarget(C_Actor *candidate, float *bestScore, C_Actor **best);   // 0x005cc7a0
    // Within lunge reach of the target (no long path left): close in. Else a new path to the
    // target unless it moved under 1.25 m since the last one; false when no path is found.
    bool CheckReached(int unused);                      // 0x005cccc0 (callers pass 1; unread)
    // Routes the path around a parked car standing on its checkpoints (the path-geometry cluster
    // 0x005c9080..0x005ca5a0; ported with it). False when the path cannot be kept.
    bool RouteAroundCars();                             // 0x005ca5a0
    // Sets the desired facing (m_facing) toward `to` from `from` (horizontal).
    void SetMoveToward(const float *from, const float *to);   // 0x005cc5f0
    // --- the pathfinder and the attack / owner internals ---------------------------
    // Finds a checkpoint path from `from` to `to`; true when one is built. (0x005cb500)
    bool FindPath(const float *from, const float *to);
    // Searches a path between the checkpoints nearest `from`/`to` and builds the waypoint list.
    bool RunPathSearch(uint16_t startCheckpoint, uint16_t goalCheckpoint,
                       const float *from, const float *to);                  // 0x005cb610
    // Smooths the built path, cutting corners within each point's radius. (0x005cb9b0)
    void SmoothPath(const float *from, const float *to);
    // Frees the path's waypoints, clears the path/order bits, and advances a waiting script.
    void ClearPath();                                                        // 0x005cbe60
    // Frees the path's waypoints and empties it (keeps the capacity).
    bool ResetPath();                                                        // 0x005cb410
    // Clears the current attack target and path. (0x005cd120)
    void ClearAttack();
    // Starts attacking the current target. (0x005cd0a0)
    void BeginAttack();

    C_CheckpointNetwork *m_checkpoints = nullptr;       // +0x70
    uint32_t m_074 = 0;                                 // +0x74 low byte = the mode (the path container's lead word)
    // The checkpoint path being followed: a grown array of waypoints (begin/end/cap of S_AIWaypoint*).
    S_AIWaypoint **m_pathBegin = nullptr;               // +0x78
    S_AIWaypoint **m_pathEnd = nullptr;                 // +0x7C
    S_AIWaypoint **m_pathCap = nullptr;                 // +0x80
    int m_084 = 0;                                      // +0x84
    C_Actor *m_target = nullptr;                        // +0x88 the current target (GetTarget)
    C_Actor *m_orderedTarget = nullptr;                 // +0x8C the target from Attack
    S_vector4 m_090 = {};                               // +0x90 a target position (-10000: none)
    S_vector4 m_0a0 = {};                               // +0xA0 another (-10000: none)
    int m_0b0 = 0;                                      // +0xB0
    int m_0b4 = 0;                                      // +0xB4
    uint8_t m_changed = 0;                              // +0xB8 (TakeChanged reads + clears)
    uint8_t m_unknownB9[3] = {};
    int m_0bc = 0;                                      // +0xBC
};

// The scripted tracker (vtable 0x00812250, mode 5, 0xD0 bytes): a script orders it, raising the
// alarm calls the script back, and it barks now and then when the script asks.
class C_MeleeTrackerScriptedAI : public C_MeleeTrackerAI {
public:
    C_MeleeTrackerScriptedAI();                         // (SetController case 5)
    ~C_MeleeTrackerScriptedAI() override;               // 0x005d5bd0
    // Binds the tracker and picks up the script of its frame; true when it has one.
    bool Attach(C_MeleeTracker *meleeTracker) override; // +0x00 0x005cf6c0
    // The root's reset, idle barks off.
    void Reset() override;                              // +0x04 0x005d5b80
    // Unbinds the tracker and empties the path.
    bool Detach() override;                             // +0x0C 0x005d5bb0
    void FrameUpdate(int /*frameTime*/) override {}     // +0x14 0x005cfa10
    // The AI update, then the idle-bark timer.
    void Tick(int timeDelta) override;                  // +0x18 0x005cfa20
    // Raises (value != 0, the script is told) or clears the alarm.
    void SetAlarm(int value) override;                  // +0x38 0x005cf8e0
    // No order, not suspended; hands over to the autonomous controller (which deletes this one).
    void ReleaseScript() override;                      // +0x3C 0x005cf8b0
    void SetScript(C_Script *script) override;          // +0x40 0x005d5bc0
    // A sound louder than 10 raises alarm 1 (a footstep: 4).
    void OnHeardSound(S_MeleeTrackerSighting *sighting) override;   // +0x48 0x005cfb70
    // Raises the "spotted" alarm (0x20).
    void OnSpottedEnemy(C_Actor *target) override;      // +0x4C 0x005cfba0 (target unread)
    // Defensive: latches the human that hit it as the target.
    void OnSeenHuman(int unused, C_Actor *actor) override;   // +0x54 0x005cfb40
    void PrepareSave() override {}                      // +0x60 0x005d3270
    // The root's chunk plus the bark timer (0x2054).
    bool SaveGame(C_chunk &chunk) override;             // +0x64 0x005d2dd0
    bool LoadGame(C_chunk &chunk) override;             // +0x68 0x005d2fc0
    // HafHaf: barks start within 2 s / stop.
    int SetBarking(bool on);                            // 0x005cfbb0

    C_Script *m_script = nullptr;                       // +0xC0 the tracker's script
    int m_barkTimer = -1;                               // +0xC4 ms to the next idle bark (-1: off)
    uint8_t m_unknownC8[8] = {};                        // +0xC8 (0xD0 bytes)
};

// The autonomous tracker (vtable 0x008122c8, mode 4, 0xC0 bytes): the full AI without a script,
// deaf to the hearing / sight hooks (it attacks what it has seen or heard on its own).
class C_MeleeTrackerAutoAI : public C_MeleeTrackerAI {
public:
    C_MeleeTrackerAutoAI();                             // (SetController case 4)
    ~C_MeleeTrackerAutoAI() override;                   // 0x005d5b30
    bool Attach(C_MeleeTracker *meleeTracker) override; // +0x00 0x005cf0a0
    void Reset() override;                              // +0x04 0x005d5b00 (the root's)
    bool Detach() override;                             // +0x0C 0x005d5b20
    void FrameUpdate(int /*frameTime*/) override {}     // +0x14 0x005cf0c0
    void Tick(int timeDelta) override;                  // +0x18 0x005cf0d0 (Update)
    void OnHeardSound(S_MeleeTrackerSighting * /*sighting*/) override {}   // +0x48 0x005cf0e0
    void OnSpottedEnemy(C_Actor * /*target*/) override {}   // +0x4C 0x005cf0f0
    void PrepareSave() override {}                      // +0x60 0x005d2dc0
    bool SaveGame(C_chunk &chunk) override;             // +0x64 0x005d2da0 (the root's)
    bool LoadGame(C_chunk &chunk) override;             // +0x68 0x005d2db0 (the root's)
};

// A client's mirror of a tracker the server simulates (vtable 0x008121e0, mode 7, 0x270 bytes):
// every frame it applies its own behaviour buffers (filled by OnNetState) to the body.
class C_MeleeTrackerRemote : public C_MeleeTrackerController {
public:
    C_MeleeTrackerRemote();                             // (SetController case 7)
    ~C_MeleeTrackerRemote() override;                   // 0x005d5c40
    bool Attach(C_MeleeTracker *meleeTracker) override; // +0x00 0x005cfc10
    void Reset() override;                              // +0x04 0x005d5c20 (the root's)
    bool Detach() override;                             // +0x0C 0x005d5c30
    // Applies the buffers, then clears B.
    void FrameUpdate(int frameTime) override;           // +0x14 0x005cfc20
    void Tick(int /*timeDelta*/) override {}            // +0x18 0x005cfc60
    // The tracker's position (the frame is moved there) and velocity (into m_facing).
    void OnNetState(C_NetMessage *msg) override;        // +0x58 0x005cfc70
    void PrepareSave() override {}                      // +0x60 0x005d32a0
    bool SaveGame(C_chunk &chunk) override;             // +0x64 0x005d3280 (the root's)
    bool LoadGame(C_chunk &chunk) override;             // +0x68 0x005d3290 (the root's)

    float m_bufferA[0x40] = {};                         // +0x070 the behaviour weights A
    float m_bufferB[0x40] = {};                         // +0x170 B (-> 0x270)
};

// The player drives the tracker (vtable 0x00812340, mode 1, 0x4B0 bytes): the control values are
// the behaviour weights (C_MeleeTracker::Tick points the buffers at them), the mouse turns it
// (MouseLook, from the body's UpdateAnimation), and a client sends its state to the server.
class C_MeleeTrackerPlayerControl : public C_MeleeTrackerController {
public:
    C_MeleeTrackerPlayerControl();                      // (SetController case 1)
    ~C_MeleeTrackerPlayerControl() override;            // 0x005d5ad0
    // Binds the tracker and clears the four buffers.
    bool Attach(C_MeleeTracker *meleeTracker) override; // +0x00 0x005cf130
    void Reset() override {}                            // +0x04 0x005d5aa0
    bool Detach() override;                             // +0x0C 0x005d5ac0
    // The root's, and the camera looks along the facing.
    void ResetState(C_MeleeTrackerController *source) override;   // +0x10 0x005cf180
    // Applies the controls (a networked game: its own buffers), and a client sends its state.
    void FrameUpdate(int frameTime) override;           // +0x14 0x005cf320
    // Counts the ticks on a dedicated server.
    void Tick(int timeDelta) override;                  // +0x18 0x005cf570
    // A state / control message: the buffers cleared, the network state (and the control: the
    // orders) taken.
    void OnNetState(C_NetMessage *msg) override;        // +0x58 0x005cf590
    void OnNetControl(C_NetMessage *msg) override;      // +0x5C 0x005cf5e0
    void PrepareSave() override {}                      // +0x60 0x005d25a0
    bool SaveGame(C_chunk &chunk) override;             // +0x64 0x005d2580 (the root's)
    bool LoadGame(C_chunk &chunk) override;             // +0x68 0x005d2590 (the root's)
    // The mouse turns the facing: `dx` the yaw, `dy` the pitch (2 pi per 1000 counts; the pitch
    // within +-60 degrees). Not while the camera's flag (+0x78) is set.
    void MouseLook(int dx, int dy);                     // 0x005cf1b0

    float m_bufferA[0x40] = {};                         // +0x070
    float m_bufferB[0x40] = {};                         // +0x170
    float m_buffer270[0x40] = {};                       // +0x270 (cleared by Attach only)
    float m_buffer370[0x40] = {};                       // +0x370 (cleared by Attach only)
    S_vector4 m_netPos = {};                            // +0x470 the received position
    S_vector4 m_netDir = {};                            // +0x480 the received direction
    S_vector4 m_netVelocity = {};                       // +0x490 the received velocity
    uint8_t m_netUnused[0x10] = {};                     // +0x4A0 (-> 0x4B0)
};

// (The layouts follow the original's offsets; the controllers are reached through pointers only.)
static_assert(sizeof(C_MeleeTrackerController) == 0x70, "C_MeleeTrackerController layout");
static_assert(sizeof(C_MeleeTrackerAI) == 0xC0, "C_MeleeTrackerAI layout");
static_assert(sizeof(C_MeleeTrackerScriptedAI) == 0xD0, "C_MeleeTrackerScriptedAI layout");
static_assert(sizeof(C_MeleeTrackerAutoAI) == 0xC0, "C_MeleeTrackerAutoAI layout");
static_assert(sizeof(C_MeleeTrackerRemote) == 0x270, "C_MeleeTrackerRemote layout");
static_assert(sizeof(C_MeleeTrackerPlayerControl) == 0x4B0, "C_MeleeTrackerPlayerControl layout");

// The meleeTracker's checkpoint path uses the same waypoint node as the human AI (S_AIWaypoint), from the
// meleeTracker's own pool (0x008ac920, separate from the human AI's). AllocMeleeTrackerWaypoint fills one in (the
// checkpoint radius is a byte in 1/16 m, scaled to metres here); ReleaseMeleeTrackerWaypoint recycles it.
S_AIWaypoint *AllocMeleeTrackerWaypoint(int checkpoint, const S_vector4 &pos, uint16_t flags,
                               uint8_t type, uint8_t radiusByte);            // 0x005ce4c0
void ReleaseMeleeTrackerWaypoint(S_AIWaypoint *waypoint);                            // 0x005cafc0
