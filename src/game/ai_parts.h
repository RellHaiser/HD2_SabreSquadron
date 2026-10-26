// The parts the AI controllers (C_AIController, game/human_controller.h) are built from: where
// the AI looks (S_AIFacing), the fire order it carries out (S_AIFireOrder), where it moves
// (S_AIMoveOrder), the road path it drives a car along (C_AIDriver), how alert it is
// (S_AIAlertState), what it fires at (S_AIFire), its route (S_AIWaypoint) and its tasks
// (S_AITask), with the object pools the waypoints, tasks and remembered sounds come from.
//
// Original source: 0x0055d1e0..0x00563c70 (the parts), 0x00576ac0..0x00576d00 (S_AIFire),
// 0x0054fc40..0x005516b0 (the tasks' saved games), 0x00582030..0x00582df0 and 0x00589310..
// 0x0058b6c0 (the pools at 0x008ab158).
#pragma once

#include <cstdint>
#include <vector>

#include "ls3d/math.h"

class C_Actor;
class C_chunk;
class C_CheckpointNetwork;
class C_Human;
class I3D_frame;
struct S_RememberedSound;

// A waypoint of an AI's route (0x30 bytes; saved games store it as it is in memory): a
// walking point, or a point of its own (checkpoint -1: the end of the route, a step round
// someone in the way).
struct alignas(16) S_AIWaypoint {
    S_vector4 pos;                                      // +0x00 where the AI walks (moved within the radius to cut corners)
    S_vector4 pos2;                                     // +0x10 the point itself
    int checkpoint = -1;                                // +0x20 the walking point, -1 none
    uint16_t flags = 0;                                 // +0x24 the point's flags & CHECKPOINT_SEARCH_FLAGS
    uint8_t type = 0;                                   // +0x26 the point's type (CHECKPOINT_TYPE_*)
    float radius = 0.0f;                                // +0x28 the point's radius in m
};
static_assert(sizeof(S_AIWaypoint) == 0x30, "S_AIWaypoint layout");

// Waypoints come from a pool (0x008ab188). AllocWaypoint fills one in (0x00582670), the raw
// one is for saved games (0x00582700).
S_AIWaypoint *AllocWaypoint(int checkpoint, const S_vector4 &pos, uint16_t flags, uint8_t type, float radius);
S_AIWaypoint *AllocWaypoint();
void ReleaseWaypoint(S_AIWaypoint *waypoint);           // inline (0x005638e0)

// Where an AI looks (C_AIController +0x124; 0x60 bytes). The view stays within a cone around
// the body's facing: turning the view out of it turns the body too, and turning the body
// takes the view along, unless the one that would follow is locked.
struct alignas(16) S_AIFacing {
    S_AIFacing();                                       // 0x0055d860

    // Turns the view to `dir`: 0, or 1 when the body would have to turn and its facing is
    // locked (nothing changes then).
    int SetView(const S_vector4 &dir);                  // 0x0055d2e0
    // Turns the body to `dir`: 0, or 2 when the view would leave the cone and is locked.
    int SetFacing(const S_vector4 &dir);                // 0x0055d630
    int SetLookAt(I3D_frame *frame) { lookAt = frame; return 0; }   // 0x0055d1e0
    // Looks around toward `dir`: more than 75 degrees off the view, the view turns there
    // (2 when it is locked, else SetView's result); nearer, the next look around waits 5 s.
    int LookToward(const S_vector4 &dir);               // 0x0055d1f0
    // Looks around every `period` ms (the direction and the frame to look at cleared), or
    // stops.
    void SetLookAround(bool on, int period);            // 0x0055d280
    // Counts the time to the next look around down; true when it is due (and restarts it).
    bool TickLookTimer(int timeDelta);                  // 0x0055d2b0
    // Set / clear a lock; false when it already was / was not set.
    bool LockFacing();                                  // 0x0055d8c0
    bool LockView();                                    // 0x0055d8e0
    bool UnlockFacing();                                // 0x0055d900
    bool UnlockView();                                  // 0x0055d920
    bool IsFacingLocked() const { return (locks & LOCK_FACING) != 0; }   // 0x0055d950
    bool IsViewLocked() const { return (locks & LOCK_VIEW) != 0; }       // 0x0055d940

    bool SaveGame(C_chunk &chunk) const;                // 0x0055d960
    bool LoadGame(C_chunk &chunk);                      // 0x0055dbc0

    enum : uint16_t {
        LOCK_FACING = 1,
        LOCK_VIEW = 2,
    };
    // The cosine of the cone (lying: 0.94).
    static constexpr float CONE = 0.7f;
    static constexpr float CONE_LYING = 0.94f;

    S_vector4 facing = {};                              // +0x00
    S_vector4 view = {};                                // +0x10
    S_vector4 lookDir = {};                             // +0x20 a direction to look around towards (zero: none)
    float facingX = 0.0f;                               // +0x30 the facing's normalized x / z
    float facingZ = 0.0f;
    float viewX = 0.0f;                                 // +0x38 the view's normalized x / z
    float viewZ = 0.0f;
    uint16_t locks = 0;                                 // +0x40 LOCK_*
    float cone = CONE;                                  // +0x44
    int lookPeriod = -1;                                // +0x48 ms between looks around (-1 none)
    int lookTimer = -1;                                 // +0x4C
    I3D_frame *lookAt = nullptr;                        // +0x50 a frame to keep looking at
};

// A fire order an AI carries out (C_AIController +0x128; 0x50 bytes): at a target actor or in
// a direction, with the bursts' and pauses' lengths, or a reload / a grenade to throw.
struct alignas(16) S_AIFireOrder {
    S_AIFireOrder() { ResetTimes(); }                   // 0x005607d0

    // At an actor / in a direction (the other is cleared).
    void SetTarget(C_Actor *actor);                     // 0x005607b0
    void SetFireDir(const S_vector4 &dir);              // 0x00560780
    // A grenade to throw at `pos` (the times go back to the defaults).
    void SetThrow(const S_vector4 &pos);                // 0x00560750
    void ResetTimes();                                  // 0x00560810
    void SetBurstTime(int time, int spread) { burstTime = time; burstSpread = spread; }   // 0x00560820
    void SetPauseTime(int time, int spread) { pauseTime = time; pauseSpread = spread; }   // 0x00560840
    // A burst's / pause's length: `time` give or take `spread` ms; false when it is the
    // default (-1).
    bool GetBurstTime(int *time, int *spread) const;        // 0x00560860
    bool GetPauseTime(int *time, int *spread) const;        // 0x00560880

    bool SaveGame(C_chunk &chunk) const;                // 0x005608a0
    bool LoadGame(C_chunk &chunk);                      // 0x00560ac0

    S_vector4 unknown00 = {};                           // +0x00
    S_vector4 fireDir = {};                             // +0x10 fire in a direction (no target; zero: none)
    S_vector4 throwPos = {};                            // +0x20 where a grenade is to be thrown
    C_Actor *target = nullptr;                          // +0x30
    bool reload = false;                                // +0x34 asked to reload
    bool throwGrenade = false;                          // +0x35 asked to throw a grenade at throwPos
    uint32_t result = 0;                                // +0x38 what the last update did (AI_FIRE_*)
    int burstTime = -1;                                 // +0x3C ms of a burst (-1: default)
    int burstSpread = -1;                               // +0x40
    int pauseTime = -1;                                 // +0x44 ms between bursts (-1: default)
    int pauseSpread = -1;                               // +0x48
};

// Where an AI moves (C_AIController +0x12C; 0x60 bytes, made inline by the constructor): a
// destination the controller makes a route of waypoints to (C_AIController::UpdateMove), and
// the state of following it.
struct alignas(16) S_AIMoveOrder {
    // flags (names partly inferred)
    enum : uint32_t {
        FLAG_NEW = 0x00000001,                          // a destination was given (route to it)
        FLAG_CHECKPOINT_POS = 0x00000002,               // the position at the checkpoint (SetCheckpointPos)
        FLAG_FLEE = 0x00000004,                         // away from the enemies seen (SetFlee)
        FLAG_TO_LADDER = 0x00000010,                    // walking onto `ladder`
        FLAG_ON_LADDER = 0x00000020,                    // climbing it
        FLAG_LADDER_DOWN = 0x00000080,                  // the next waypoint is below
        FLAG_LADDER_MASK = 0x000000F0,
        FLAG_ROUTE_15 = 0x00000700,                     // on points of type 15 (stepping round the others)
        FLAG_DOOR = 0x00000800,                         // passing `door`
        FLAG_BUSY = 0x00000F30,                         // a ladder, a door or type 15 points
        FLAG_TURN_FIRST = 0x00001000,                   // turn toward the first waypoint before walking
        FLAG_REROUTE = 0x00002000,                      // the route ends and a new one is made
        FLAG_MAKE_WAY = 0x00004000,                     // stepping off a point a car stands on
        FLAG_WAIT = 0x00008000,                         // waiting for a car to leave the route (unknown34 ms)
        FLAG_STANCE_SAVED = 0x00010000,                 // the body's movement flags are put aside (unknown30)
        FLAG_80000 = 0x00080000,                        // cleared by every setter
        FLAG_AVOIDING = 0x00100000,                     // stepping aside (C_HumanAIMemory::unknown00)
        FLAG_ROUTE = 0x10000000,                        // following the waypoints
        FLAG_20000000 = 0x20000000,                     // cleared with FLAG_DOOR (not set anywhere seen)
        FLAG_DONE = 0x80000000,                         // the move ended (unknown4C says how)
    };

    // A destination: a position, a walking point, or both (the position at the point).
    void SetPos(const S_vector4 &pos);                  // 0x00560ea0 (ignored for |y| > 100000)
    void SetCheckpoint(int16_t checkpoint);             // 0x00560f00
    void SetCheckpointPos(int16_t checkpoint, const S_vector4 &pos);   // 0x00560e50
    // Away from the enemies seen: not within `minDist` m of one, `safeDist` m from each.
    void SetFlee(int minDist, int safeDist);             // 0x00560e20
    // The move ends (the route and the door forgotten).
    void Finish();                                      // 0x00560f30
    // A route starts (turning toward it first).
    void BeginRoute();                                  // 0x00560f50
    // Waits 5 .. 10 s for a car to leave the route.
    void Wait();                                        // 0x00560f90

    bool SaveGame(C_chunk &chunk) const;                // 0x00560fd0
    bool LoadGame(C_chunk &chunk);                      // 0x00561280

    S_vector4 unknown00 = {};                           // +0x00 (not saved)
    S_vector4 pos = {};                                 // +0x10
    uint16_t checkpoint = 0xFFFF;                       // +0x20 -1 none (not set by the original)
    int unknown24 = 0;                                  // +0x24 ms counted down (UpdateMove)
    int unknown28 = 0;                                  // +0x28 steps without getting nearer the waypoint
    float unknown2C = 3.402823466e+38f;                 // +0x2C the nearest it got (FLT_MAX: none yet)
    int unknown30 = -1;                                 // +0x30 the body's movement stance put aside (-1 none)
    int unknown34 = 0;                                  // +0x34 ms to wait (doors, cars on the route)
    int unknown38 = 6;                                  // +0x38 SetFlee's minDist
    int unknown3C = 40;                                 // +0x3C SetFlee's safeDist
    C_Actor *door = nullptr;                            // +0x40 (loaded only if it is a door)
    C_Actor *ladder = nullptr;                          // +0x44 (loaded only if it is a ladder)
    uint16_t ladderFrom = 0xFFFF;                       // +0x48 the ladder's walking points
    uint16_t ladderTo = 0xFFFF;                         // +0x4A
    int unknown4C = 0;                                  // +0x4C the result (MOVE_RESULT_*, human_controller.h)
    uint32_t flags = 0;                                 // +0x50 FLAG_*
    uint32_t unknown54 = 0;                             // +0x54 the pace (MOVE_PACE_*; 0x10: a ladder downward)
    bool unknown58 = false;                             // +0x58 (UpdateMove: stays put, result 2)
};

// A point of the AI driver's road path (0x30 bytes).
struct alignas(16) S_AIPathPoint {
    S_vector4 pos = {};                                 // +0x00
    S_vector4 dir = {};                                 // +0x10 the curve's tangent (half the neighbours' distances)
    bool last = false;                                  // +0x20 the destination
};

// Drives a car along the road network (C_AIController +0x130; 0xA0 bytes).
class alignas(16) C_AIDriver {
public:
    enum : uint32_t {
        FLAG_NEW_TARGET = 1,                            // plan a path to m_target
        FLAG_DRIVING = 2,                               // following the path
        FLAG_STOP = 4,                                  // stop the car (Stop / a zero speed)
    };

    C_AIDriver();                                       // 0x0055df90
    ~C_AIDriver();                                      // 0x0055e030
    C_AIDriver(const C_AIDriver &) = delete;
    C_AIDriver &operator=(const C_AIDriver &) = delete;

    // The car to drive (null: none; leaving the old one sets its unknown value back).
    void SetVehicle(C_Actor *vehicle);                  // 0x0055e0e0
    C_Actor *GetVehicle() const { return m_vehicle; }   // 0x0055e120
    void SetPace(uint32_t pace) { m_pace = pace & 0x1F000; }   // 0x0055e130 (MOVE_PACE_MASK)
    // Drive to `target` at `speed` (a zero speed stops the car); with a direction to arrive in.
    void DriveTo(const S_vector4 &target, float speed); // 0x0055e160
    void DriveTo(const S_vector4 &target, const S_vector4 &targetDir, float speed);   // 0x0055e1d0
    void Stop();                                        // 0x0055e250
    void SetFlags(uint32_t flags) { m_flags |= flags; } // 0x0055e2b0
    void ClearFlags(uint32_t flags) { m_flags &= ~flags; }   // 0x0055e2c0
    bool HasFlags(uint32_t flags) const { return (m_flags & flags) != 0; }   // 0x0055e2e0
    // Clears the path and its state.
    void ResetPath();                                   // 0x0055f2c0
    // Adds the road from `from` to `to` to the path: the walking points of the road network
    // between them, moved within their radius to cut the curves; `beyond` (NaN: none) is a
    // point the last tangent points on to.
    void PlanPath(const S_vector4 &from, const S_vector4 &to, const S_vector4 &beyond);   // 0x0055f320

    bool SaveGame(C_chunk &chunk) const;                // 0x0055e8d0
    bool LoadGame(C_chunk &chunk);                      // 0x0055ec80

    std::vector<S_AIPathPoint> m_path;                  // +0x00 (16-byte aligned storage)
    uint16_t m_pathIndex = 0;                           // +0x10
    float m_progress = 0.0f;                            // +0x14 how far along the segment the car steers to (0..1)
    float m_nearestDist2 = 1e8f;                        // +0x18 the nearest it got to the segment's end (squared)
    bool m_reversing = false;                           // +0x1C backing up to get unstuck
    bool m_moving = false;                              // +0x1D it got going (over 5 cm/s)
    int m_stuckTime = 0;                                // +0x20 ms standing after it got going
    int m_noProgressTime = 0;                           // +0x24 ms without getting nearer the segment's end
    S_vector4 m_lastPos = {};                           // +0x30 the car's position at the last update
    uint32_t m_flags = 0;                               // +0x40 FLAG_*
    S_vector4 m_target = {};                            // +0x50
    S_vector4 m_targetDir = {};                         // +0x60 NaN x: none
    C_CheckpointNetwork *m_roads = nullptr;             // +0x70 the game's road network
    C_Actor *m_vehicle = nullptr;                       // +0x74
    // +0x78: points of a route only an unreferenced planner (0x0055e2f0) fills in; freed by
    // the destructor and LoadGame.
    std::vector<void *> m_route;
    bool m_unknown88 = false;                           // +0x88
    float m_speed = -1.0f;                              // +0x8C
    uint32_t m_pace = 0;                                // +0x90 MOVE_PACE_*
    int m_label = 0;                                    // +0x94 the script's label when it arrives
};

// How alert an AI is (C_AIController +0x134; 8 bytes). The states seen are 1, 2 and 4
// (names not identified); `changed` marks a change not handled yet.
struct S_AIAlertState {
    void Set(int value);                                // 0x00563a20
    int Get() const { return state; }                   // 0x00563a40
    bool Is1() const { return state == 1; }             // 0x005639f0
    bool Is2() const { return state == 2; }             // 0x00563a00
    bool Is4() const { return state == 4; }             // 0x00563a10

    bool SaveGame(C_chunk &chunk) const;                // 0x00563a80
    bool LoadGame(C_chunk &chunk);                      // 0x00563c70

    int state = 0;                                      // +0x00
    bool changed = false;                               // +0x04
};

// What an AI fires at (C_AIController +0xC0; 0x30 bytes).
struct alignas(16) S_AIFire {
    // flags
    enum : uint32_t {
        FLAG_RELOAD = 1,                                // a reload was pressed
        FLAG_DIRECTION = 2,                             // firing in a direction (no target)
    };

    S_AIFire() = default;                               // 0x00576ac0
    bool SaveGame(C_chunk &chunk) const;                // 0x00576ae0
    bool LoadGame(C_chunk &chunk);                      // 0x00576d00

    bool unknown00 = false;                             // +0x00
    int burstTime = 0;                                  // +0x04 ms left of the burst
    int pauseTime = 0;                                  // +0x08 ms until the next burst
    int unknown0C = 0;                                  // +0x0C
    C_Actor *target = nullptr;                          // +0x10
    C_Actor *orderTarget = nullptr;                     // +0x14 the fire order's target (GetLastTarget)
    uint32_t flags = 0;                                 // +0x18
    S_vector4 throwPos = {};                            // +0x20 (zero-initialized by the port)
};

// A task of an AI (C_AIController +0xF0): polymorphic, 12 types, from the pools. The derived
// controllers make them (purpose not identified; named by type). Types 7..12 save only the
// base fields.
struct alignas(16) S_AITask {
    explicit S_AITask(int t) : type(t) {}
    virtual ~S_AITask() = default;                      // slot 0
    virtual bool SaveGame(C_chunk &chunk) const;        // slot 1 0x0054fc40
    virtual bool LoadGame(C_chunk &chunk);              // slot 2 0x0054fcd0

    int type;                                           // +0x04 1..12
    int unknown08 = 0;                                  // +0x08
    int unknown0C = 0;                                  // +0x0C
    int unknown10 = 0;                                  // +0x10
};
// Type 1 (vtable 0x008110d8, 0x50 bytes).
struct S_AITask1 : S_AITask {
    S_AITask1() : S_AITask(1) {}
    bool SaveGame(C_chunk &chunk) const override;       // 0x0054fe40
    bool LoadGame(C_chunk &chunk) override;             // 0x00550090
    S_vector4 pos = {};                                 // +0x20
    S_vector4 dir = {};                                 // +0x30
    float unknown40 = 0.0f;                             // +0x40 (the squad's idle task: 15)
    int unknown44 = 0;                                  // +0x44 -1 when made
    int checkpoint = 0;                                 // +0x48
};
// Type 2 (vtable 0x008110f0, 0x50 bytes).
struct S_AITask2 : S_AITask {
    S_AITask2() : S_AITask(2) {}
    bool SaveGame(C_chunk &chunk) const override;       // 0x00550370
    bool LoadGame(C_chunk &chunk) override;             // 0x005505b0
    S_vector4 unknown20 = {};                           // +0x20
    S_vector4 unknown30 = {};                           // +0x30
    int unknown40 = 0;                                  // +0x40
    C_Human *human = nullptr;                           // +0x44
};
// Type 3 (vtable 0x008110fc, 0x40 bytes).
struct S_AITask3 : S_AITask {
    S_AITask3() : S_AITask(3) {}
    bool SaveGame(C_chunk &chunk) const override;       // 0x00550870
    bool LoadGame(C_chunk &chunk) override;             // 0x00550a70
    S_vector4 pos = {};                                 // +0x20
    C_Actor *actor = nullptr;                           // +0x30
};
// Types 4, 5 and 6 (vtables 0x00811108, 0x00811114, 0x00811120; 0x14 bytes): an empty chunk
// of their own after the base's.
struct S_AITaskEmpty : S_AITask {
    explicit S_AITaskEmpty(int t) : S_AITask(t) {}
    bool SaveGame(C_chunk &chunk) const override;       // 0x00550d20 / 0x00551100 / 0x005514e0
    bool LoadGame(C_chunk &chunk) override;             // 0x00550ed0 / 0x005512b0 / 0x00551690
};
struct S_AITask4 : S_AITaskEmpty { S_AITask4() : S_AITaskEmpty(4) {} };
struct S_AITask5 : S_AITaskEmpty { S_AITask5() : S_AITaskEmpty(5) {} };
struct S_AITask6 : S_AITaskEmpty { S_AITask6() : S_AITaskEmpty(6) {} };
// Type 7 (vtable 0x0081112c, 0x50 bytes).
struct S_AITask7 : S_AITask {
    S_AITask7() : S_AITask(7) {}
    S_vector4 unknown20 = {};                           // +0x20
    S_vector4 unknown30 = {};                           // +0x30
};
// Type 8 (vtable 0x00811138, 0x20 bytes).
struct S_AITask8 : S_AITask {
    S_AITask8() : S_AITask(8) {}
    int unknown14 = 0;                                  // +0x14
    int unknown18 = 0;                                  // +0x18
    int unknown1C = 0;                                  // +0x1C
};
// Types 9, 10 and 11 (vtables 0x00811144, 0x00811150, 0x0081115c; 0x18 bytes).
struct S_AITask9 : S_AITask { S_AITask9() : S_AITask(9) {} };
struct S_AITask10 : S_AITask { S_AITask10() : S_AITask(10) {} };
struct S_AITask11 : S_AITask { S_AITask11() : S_AITask(11) {} };
// Type 12 (vtable 0x00811168, 0x40 bytes).
struct S_AITask12 : S_AITask {
    S_AITask12() : S_AITask(12) {}
    S_vector4 pos = {};                                 // +0x20
    int unknown30 = 0;                                  // +0x30
};

// The tasks' pools. AllocTask(type) is the saved games' (null for an unknown type);
// ReleaseTask puts a task of any type back.
S_AITask *AllocTask(int type);                          // 0x00582df0
void ReleaseTask(S_AITask *task);                       // 0x00582c50
S_AITask1 *AllocTask1(const S_vector4 &pos, uint16_t checkpoint, float unknown40, const S_vector4 &dir);   // 0x005827b0
S_AITask2 *AllocTask2(const S_vector4 &unknown20, const S_vector4 &unknown30, int unknown08);           // 0x00582890
S_AITask3 *AllocTask3(int unknown08, C_Actor *actor, const S_vector4 &pos);   // 0x00582990
S_AITask4 *AllocTask4();                                // 0x00582a00
S_AITask5 *AllocTask5(int unknown08);                   // 0x00582a40
S_AITask6 *AllocTask6();                                // 0x00582a90
S_AITask7 *AllocTask7(const S_vector4 &unknown20, const S_vector4 &unknown30);   // 0x00582ad0
S_AITask8 *AllocTask8(int unknown14);                   // 0x00582b50
S_AITask12 *AllocTask12(const S_vector4 &pos, int unknown30);   // 0x00582920

// Writes chunk `id` with the actor's id when there is an actor (0x004752e0 __fastcall);
// reads one (0x00475470; it does not leave the chunk).
void WriteActorChunk(C_chunk &chunk, uint16_t id, const C_Actor *actor);
C_Actor *ReadActor(C_chunk &chunk);
