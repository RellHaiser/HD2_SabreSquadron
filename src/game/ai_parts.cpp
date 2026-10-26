// The parts of the AI controllers (game/ai_parts.h): where the AI looks, its fire and move
// orders, the car driver's road path, the alert state, what it fires at, its tasks, and the
// pools of waypoints, tasks and remembered sounds (original source 0x0055d1e0..0x00563c70,
// 0x00576ac0..0x00576d00, 0x0054fc40..0x005516b0, 0x00582030..0x00582df0, 0x00589310..
// 0x0058b6c0).
#include "game/ai_parts.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "common/chunk.h"
#include "common/object_pool.h"
#include "game/actors_module.h"
#include "game/checkpoints.h"
#include "game/game.h"
#include "game/human.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// Saved game chunks. Every part writes its own chunk 0x41 with the data in a child.
enum : uint16_t {
    CHUNK_AI_PART = 0x41,
    CHUNK_FACING = 0xD02,
    CHUNK_FACING_LOOK_AT = 0xD03,                       // the frame's name
    CHUNK_ALERT = 0x456,
    CHUNK_MOVE = 0x8AC,
    CHUNK_MOVE_DOOR = 0x8AD,                            // actor id
    CHUNK_MOVE_LADDER = 0x8AE,                          // actor id
    CHUNK_MOVE_8AF = 0x8AF,
    CHUNK_FIRE_ORDER = 0x1158,
    CHUNK_FIRE_ORDER_TARGET = 0x1159,                   // actor id
};

// C_AIDriver's chunks (no 0x41 around them).
enum : uint16_t {
    CHUNK_DRIVER = 100,
    CHUNK_DRIVER_FLAGS = 200,                           // only when set
    CHUNK_DRIVER_TARGET = 300,
    CHUNK_DRIVER_VEHICLE = 400,                         // actor id
    CHUNK_DRIVER_POINT = 500,                           // one per path point
    CHUNK_DRIVER_UNKNOWN88 = 600,
    CHUNK_DRIVER_SPEED = 700,
    CHUNK_DRIVER_PACE = 800,                            // only when set
    CHUNK_DRIVER_LABEL = 900,
    CHUNK_DRIVER_TARGET_DIR = 1000,
    CHUNK_DRIVER_PATH_INDEX = 0x44C,
    CHUNK_DRIVER_PROGRESS = 0x4B0,
    CHUNK_DRIVER_STUCK_TIME = 0x514,
    CHUNK_DRIVER_MOVING = 0x578,
    CHUNK_DRIVER_REVERSING = 0x5DC,
    CHUNK_DRIVER_NEAREST = 0x640,
    CHUNK_DRIVER_NO_PROGRESS = 0x6A4,
    CHUNK_DRIVER_LAST_POS = 0x708,
};

// S_AIFire's chunks and the tasks' (the same numbers, other meanings).
enum : uint16_t {
    CHUNK_FIRE = 0x157C,
    CHUNK_FIRE_TARGET = 0x157D,                         // actor id
    CHUNK_FIRE_LAST_TARGET = 0x157E,                    // actor id (orderTarget)
    CHUNK_FIRE_UNKNOWN0C = 0x157F,
    CHUNK_TASK_BASE = 0x157C,
    CHUNK_TASK1 = 0x157D,
    CHUNK_TASK2 = 0x157E,
    CHUNK_TASK3 = 0x157F,
    CHUNK_TASK6 = 0x1580,
    CHUNK_TASK5 = 0x1581,
    CHUNK_TASK4 = 0x1582,
};

// Positions are saved without w.
constexpr int SAVED_POS_SIZE = 12;

S_vector &Xyz(S_vector4 &v)
{
    return reinterpret_cast<S_vector &>(v);
}

const S_vector &Xyz(const S_vector4 &v)
{
    return reinterpret_cast<const S_vector &>(v);
}

// The x / z of `dir` normalized in the xz plane (left as they are when nearly zero).
void FlatDirection(const S_vector4 &dir, float &x, float &z)
{
    x = dir.x;
    z = dir.z;
    const float length = std::sqrt(x * x + z * z);
    if (length > 1e-8f) {
        const float inv = 1.0f / length;
        x *= inv;
        z = inv * z;
    }
}

template <class T>
void ReadIfFits(C_chunk &chunk, T &value)
{
    if (chunk.RemainingSize() >= static_cast<int>(sizeof(T)))
        chunk.Read(value);
}

// The AI's pools (one object of 16 pools at 0x008ab158 in the original, each filled with 16
// objects at start-up; the body commands at +0x00 and the squad orders at +0x18 belong to the
// squad controller).
struct S_AIPools {
    C_ObjectPool<S_AIWaypoint> waypoints{16};           // +0x30 (0x008ab188)
    C_ObjectPool<S_RememberedSound> sounds{16};         // +0x48 (0x008ab1a0)
    C_ObjectPool<S_AITask1> task1{16};                  // +0x60
    C_ObjectPool<S_AITask2> task2{16};                  // +0x78
    C_ObjectPool<S_AITask3> task3{16};                  // +0x90
    C_ObjectPool<S_AITask4> task4{16};                  // +0xA8
    C_ObjectPool<S_AITask5> task5{16};                  // +0xC0
    C_ObjectPool<S_AITask6> task6{16};                  // +0xD8
    C_ObjectPool<S_AITask7> task7{16};                  // +0xF0
    C_ObjectPool<S_AITask8> task8{16};                  // +0x108
    C_ObjectPool<S_AITask9> task9{16};                  // +0x120
    C_ObjectPool<S_AITask10> task10{16};                // +0x138
    C_ObjectPool<S_AITask11> task11{16};                // +0x150
    C_ObjectPool<S_AITask12> task12{16};                // +0x168
};
S_AIPools g_aiPools;                                    // 0x008ab158

}  // namespace

// ------------------------------------------------------------------------- pools

S_AIWaypoint *AllocWaypoint(int checkpoint, const S_vector4 &pos, uint16_t flags, uint8_t type, float radius)
{
    S_AIWaypoint *waypoint = g_aiPools.waypoints.Get();
    waypoint->checkpoint = checkpoint;
    waypoint->pos = pos;
    waypoint->pos2 = pos;
    waypoint->flags = flags;
    waypoint->type = type;
    waypoint->radius = radius;
    return waypoint;
}

S_AIWaypoint *AllocWaypoint()
{
    return g_aiPools.waypoints.Get();
}

void ReleaseWaypoint(S_AIWaypoint *waypoint)
{
    if (waypoint)
        g_aiPools.waypoints.Put(waypoint);
}

S_RememberedSound *AllocRememberedSound(int chapter, const S_vector4 &pos, C_Actor *source, uint32_t time)
{
    S_RememberedSound *sound = g_aiPools.sounds.Get();
    sound->chapter = chapter;
    sound->pos = Xyz(pos);
    sound->refreshed = false;
    sound->posW = pos.w;
    sound->source = source;
    sound->time = time;
    return sound;
}

void FreeRememberedSound(S_RememberedSound *sound)
{
    g_aiPools.sounds.Put(sound);
}

S_AITask *AllocTask(int type)
{
    switch (type) {
    case 1: return g_aiPools.task1.Get();
    case 2: return g_aiPools.task2.Get();
    case 3: return g_aiPools.task3.Get();
    case 4: return g_aiPools.task4.Get();
    case 5: return g_aiPools.task5.Get();
    case 6: return g_aiPools.task6.Get();
    case 7: return g_aiPools.task7.Get();
    case 8: return g_aiPools.task8.Get();
    case 9: return g_aiPools.task9.Get();
    case 10: return g_aiPools.task10.Get();
    case 11: return g_aiPools.task11.Get();
    case 12: return g_aiPools.task12.Get();
    default: return nullptr;
    }
}

void ReleaseTask(S_AITask *task)
{
    if (!task)
        return;
    // (A task of another type is dropped, as in the original.)
    switch (task->type) {
    case 1: g_aiPools.task1.Put(static_cast<S_AITask1 *>(task)); break;
    case 2: g_aiPools.task2.Put(static_cast<S_AITask2 *>(task)); break;
    case 3: g_aiPools.task3.Put(static_cast<S_AITask3 *>(task)); break;
    case 4: g_aiPools.task4.Put(static_cast<S_AITask4 *>(task)); break;
    case 5: g_aiPools.task5.Put(static_cast<S_AITask5 *>(task)); break;
    case 6: g_aiPools.task6.Put(static_cast<S_AITask6 *>(task)); break;
    case 7: g_aiPools.task7.Put(static_cast<S_AITask7 *>(task)); break;
    case 8: g_aiPools.task8.Put(static_cast<S_AITask8 *>(task)); break;
    case 9: g_aiPools.task9.Put(static_cast<S_AITask9 *>(task)); break;
    case 10: g_aiPools.task10.Put(static_cast<S_AITask10 *>(task)); break;
    case 11: g_aiPools.task11.Put(static_cast<S_AITask11 *>(task)); break;
    case 12: g_aiPools.task12.Put(static_cast<S_AITask12 *>(task)); break;
    default: break;
    }
}

S_AITask1 *AllocTask1(const S_vector4 &pos, uint16_t checkpoint, float unknown40, const S_vector4 &dir)
{
    S_AITask1 *task = g_aiPools.task1.Get();
    task->pos = pos;
    task->unknown44 = -1;
    task->checkpoint = checkpoint;
    task->unknown40 = unknown40;
    task->dir = dir;
    return task;
}

S_AITask2 *AllocTask2(const S_vector4 &unknown20, const S_vector4 &unknown30, int unknown08)
{
    S_AITask2 *task = g_aiPools.task2.Get();
    task->human = nullptr;
    task->unknown20 = unknown20;
    task->unknown30 = unknown30;
    task->unknown0C = 0;
    task->unknown08 = unknown08;
    return task;
}

S_AITask3 *AllocTask3(int unknown08, C_Actor *actor, const S_vector4 &pos)
{
    S_AITask3 *task = g_aiPools.task3.Get();
    task->actor = actor;
    task->unknown08 = unknown08;
    task->pos = pos;
    return task;
}

S_AITask4 *AllocTask4()
{
    return g_aiPools.task4.Get();
}

S_AITask5 *AllocTask5(int unknown08)
{
    S_AITask5 *task = g_aiPools.task5.Get();
    task->unknown08 = unknown08;
    return task;
}

S_AITask6 *AllocTask6()
{
    return g_aiPools.task6.Get();
}

S_AITask7 *AllocTask7(const S_vector4 &unknown20, const S_vector4 &unknown30)
{
    S_AITask7 *task = g_aiPools.task7.Get();
    task->unknown20 = unknown20;
    task->unknown30 = unknown30;
    return task;
}

S_AITask8 *AllocTask8(int unknown14)
{
    S_AITask8 *task = g_aiPools.task8.Get();
    task->unknown14 = unknown14;
    task->unknown18 = 0;
    task->unknown1C = 0;
    return task;
}

S_AITask12 *AllocTask12(const S_vector4 &pos, int unknown30)
{
    S_AITask12 *task = g_aiPools.task12.Get();
    task->unknown30 = unknown30;
    task->pos = pos;
    return task;
}

void WriteActorChunk(C_chunk &chunk, uint16_t id, const C_Actor *actor)
{
    if (!actor)
        return;
    const int netId = actor->GetNetId();
    chunk.Begin(id);
    chunk.Write(netId);
    chunk.Ascend();
}

C_Actor *ReadActor(C_chunk &chunk)
{
    int id = -1;
    ReadIfFits(chunk, id);
    return g_actorsModule.GetActorById(id);
}

// ------------------------------------------------------------------------- S_AIFacing

S_AIFacing::S_AIFacing() = default;

int S_AIFacing::SetView(const S_vector4 &dir)
{
    float x, z;
    FlatDirection(dir, x, z);
    if (cone <= x * facingX + z * facingZ) {
        // Within the cone around the facing.
        view = dir;
        viewX = x;
        viewZ = z;
        Xyz(view).Normalize();
        return 0;
    }
    if (locks & LOCK_FACING)
        return 1;
    // The body turns along. (Only the view is normalized.)
    view = dir;
    facing = dir;
    Xyz(view).Normalize();
    viewX = x;
    viewZ = z;
    facingX = x;
    facingZ = z;
    return 0;
}

int S_AIFacing::SetFacing(const S_vector4 &dir)
{
    float x, z;
    FlatDirection(dir, x, z);
    if (x * viewX + z * viewZ < cone) {
        // The view would leave the cone: it follows (not normalized then).
        if (locks & LOCK_VIEW)
            return 2;
        view = dir;
        facing = dir;
        viewX = x;
        viewZ = z;
    } else {
        facing = dir;
    }
    facingX = x;
    facingZ = z;
    Xyz(facing).Normalize();
    return 0;
}

int S_AIFacing::LookToward(const S_vector4 &dir)
{
    const float angle = Xyz(dir).AngleTo(Xyz(view));
    lookDir = dir;
    if (!(std::fabs(angle) < 1.3089969f)) {            // 75 degrees
        if (locks & LOCK_VIEW)
            return 2;
        return SetView(dir);
    }
    if (lookTimer > 0)
        lookTimer = 5000;
    return 0;
}

void S_AIFacing::SetLookAround(bool on, int period)
{
    if (!on) {
        lookTimer = -1;
        return;
    }
    lookDir.x = lookDir.y = lookDir.z = 0.0f;
    lookPeriod = period;
    lookTimer = period;
    lookAt = nullptr;
}

bool S_AIFacing::TickLookTimer(int timeDelta)
{
    if (lookTimer > 0) {
        lookTimer -= timeDelta;
        if (lookTimer < 0) {
            lookTimer += lookPeriod;
            return true;
        }
    }
    return false;
}

bool S_AIFacing::LockFacing()
{
    if (locks & LOCK_FACING)
        return false;
    locks |= LOCK_FACING;
    return true;
}

bool S_AIFacing::LockView()
{
    if (locks & LOCK_VIEW)
        return false;
    locks |= LOCK_VIEW;
    return true;
}

bool S_AIFacing::UnlockFacing()
{
    if (!(locks & LOCK_FACING))
        return false;
    locks &= ~LOCK_FACING;
    return true;
}

bool S_AIFacing::UnlockView()
{
    if (!(locks & LOCK_VIEW))
        return false;
    locks &= ~LOCK_VIEW;
    return true;
}

bool S_AIFacing::SaveGame(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_AI_PART);
    chunk.Begin(CHUNK_FACING);
    chunk.Write(&facing, SAVED_POS_SIZE);
    chunk.Write(&view, SAVED_POS_SIZE);
    chunk.Write(&lookDir, SAVED_POS_SIZE);
    chunk.Write(&facingX, 8);                           // facingX, facingZ
    chunk.Write(&viewX, 8);                             // viewX, viewZ
    chunk.Write(locks);
    chunk.Write(cone);
    chunk.Write(lookPeriod);
    chunk.Write(lookTimer);
    chunk.Ascend();
    if (lookAt) {
        const char *name = lookAt->GetName();
        chunk.WriteChunk(CHUNK_FACING_LOOK_AT, name, static_cast<int>(std::strlen(name)) + 1);
    }
    chunk.Ascend();
    return true;
}

bool S_AIFacing::LoadGame(C_chunk &chunk)
{
    lookAt = nullptr;
    uint16_t id = 0;
    if (chunk.IsOpen() && chunk.Descend(id) && id == CHUNK_AI_PART) {
        uint16_t child;
        while (!chunk.AtEnd() && chunk.Descend(child)) {
            if (child == CHUNK_FACING) {
                chunk.Read(&facing, SAVED_POS_SIZE);
                chunk.Read(&view, SAVED_POS_SIZE);
                chunk.Read(&lookDir, SAVED_POS_SIZE);
                if (chunk.RemainingSize() >= 8)
                    chunk.Read(&facingX, 8);
                if (chunk.RemainingSize() >= 8)
                    chunk.Read(&viewX, 8);
                ReadIfFits(chunk, locks);
                ReadIfFits(chunk, cone);
                chunk.Read(lookPeriod);
                chunk.Read(lookTimer);
                chunk.Ascend();
            } else if (child == CHUNK_FACING_LOOK_AT) {
                // (Fix: the original copies up to 999 bytes without terminating them.)
                char name[1000];
                chunk.ReadString(name, sizeof(name));
                chunk.Ascend();
                if (I3D_scene *scene = GetGame()->GetScene())
                    lookAt = scene->FindFrame(name, 0xFFFF);
            } else {
                chunk.Ascend();
            }
        }
    }
    // (Leaves the chunk it descended into, or the caller's when there was none.)
    chunk.Ascend();
    return true;
}

// ------------------------------------------------------------------------- S_AIFireOrder

void S_AIFireOrder::SetTarget(C_Actor *actor)
{
    target = actor;
    fireDir.x = fireDir.y = fireDir.z = 0.0f;
    unknown00.x = unknown00.y = unknown00.z = 0.0f;
}

void S_AIFireOrder::SetFireDir(const S_vector4 &dir)
{
    fireDir = dir;
    target = nullptr;
    unknown00.x = unknown00.y = unknown00.z = 0.0f;
}

void S_AIFireOrder::SetThrow(const S_vector4 &pos)
{
    throwPos = pos;
    throwGrenade = true;
    ResetTimes();
}

void S_AIFireOrder::ResetTimes()
{
    burstTime = -1;
    burstSpread = -1;
    pauseTime = -1;
    pauseSpread = -1;
}

bool S_AIFireOrder::GetBurstTime(int *time, int *spread) const
{
    *time = burstTime;
    *spread = burstSpread;
    return burstTime != -1;
}

bool S_AIFireOrder::GetPauseTime(int *time, int *spread) const
{
    *time = pauseTime;
    *spread = pauseSpread;
    return pauseTime != -1;
}

bool S_AIFireOrder::SaveGame(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_AI_PART);
    chunk.Begin(CHUNK_FIRE_ORDER);
    chunk.Write(&unknown00, SAVED_POS_SIZE);
    chunk.Write(&fireDir, SAVED_POS_SIZE);
    chunk.Write(&throwPos, SAVED_POS_SIZE);
    chunk.Write(reload);
    chunk.Write(throwGrenade);
    chunk.Write(result);
    chunk.Ascend();
    if (target) {
        chunk.Begin(CHUNK_FIRE_ORDER_TARGET);
        chunk.Write(target->GetNetId());
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}

bool S_AIFireOrder::LoadGame(C_chunk &chunk)
{
    target = nullptr;
    uint16_t id = 0;
    if (chunk.IsOpen() && chunk.Descend(id) && id == CHUNK_AI_PART) {
        uint16_t child;
        while (!chunk.AtEnd() && chunk.Descend(child)) {
            if (child == CHUNK_FIRE_ORDER) {
                chunk.Read(&unknown00, SAVED_POS_SIZE);
                chunk.Read(&fireDir, SAVED_POS_SIZE);
                chunk.Read(&throwPos, SAVED_POS_SIZE);
                ReadIfFits(chunk, reload);
                ReadIfFits(chunk, throwGrenade);
                chunk.Read(result);
                // (The target chunk comes after this one.)
                target = nullptr;
            } else if (child == CHUNK_FIRE_ORDER_TARGET) {
                int targetId = -1;
                chunk.Read(targetId);
                target = g_actorsModule.GetActorById(targetId);
            }
            chunk.Ascend();
        }
    }
    chunk.Ascend();
    return true;
}

// ------------------------------------------------------------------------- S_AIMoveOrder

namespace {
// The flags every setter clears before setting its own.
constexpr uint32_t MOVE_ORDER_KEPT = 0x7FF7FFFC;
}  // namespace

void S_AIMoveOrder::SetPos(const S_vector4 &position)
{
    if (position.y > 100000.0f || position.y < -100000.0f)
        return;
    pos = position;
    checkpoint = 0xFFFF;
    flags = (flags & MOVE_ORDER_KEPT) | FLAG_NEW;     // (keeps FLAG_FLEE)
    unknown58 = false;
    unknown24 = 0;
}

void S_AIMoveOrder::SetCheckpoint(int16_t index)
{
    if (index == -1)
        return;
    checkpoint = static_cast<uint16_t>(index);
    flags = (flags & MOVE_ORDER_KEPT) | FLAG_NEW;
    unknown58 = false;
    unknown24 = 0;
}

void S_AIMoveOrder::SetCheckpointPos(int16_t index, const S_vector4 &position)
{
    if (index == -1)
        return;
    pos = position;
    checkpoint = static_cast<uint16_t>(index);
    flags = (flags & MOVE_ORDER_KEPT) | FLAG_NEW | FLAG_CHECKPOINT_POS;
    unknown58 = false;
    unknown24 = 0;
}

void S_AIMoveOrder::SetFlee(int minDist, int safeDist)
{
    unknown38 = minDist;
    unknown3C = safeDist;
    checkpoint = 0xFFFF;
    flags = (flags & (MOVE_ORDER_KEPT & ~FLAG_FLEE)) | FLAG_NEW | FLAG_FLEE;
    unknown58 = false;
    unknown24 = 0;
}

void S_AIMoveOrder::Finish()
{
    door = nullptr;
    flags = (flags & ~(FLAG_DOOR | FLAG_REROUTE | FLAG_WAIT | FLAG_ROUTE | FLAG_20000000)) | FLAG_DONE;
}

void S_AIMoveOrder::BeginRoute()
{
    unknown2C = std::numeric_limits<float>::max();
    door = nullptr;
    unknown28 = 0;
    unknown34 = 0;
    flags = (flags & ~(FLAG_NEW | FLAG_CHECKPOINT_POS | FLAG_FLEE | FLAG_TURN_FIRST | FLAG_ROUTE | FLAG_DONE)) |
            FLAG_ROUTE | FLAG_TURN_FIRST;
}

void S_AIMoveOrder::Wait()
{
    flags |= FLAG_WAIT;
    // 5 .. 10 s.
    unknown34 = static_cast<int>((static_cast<float>(std::rand()) * (1.0f / 32767.0f) + 1.0f) * 5000.0f);
}

bool S_AIMoveOrder::SaveGame(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_AI_PART);
    chunk.Begin(CHUNK_MOVE);
    chunk.Write(&pos, SAVED_POS_SIZE);
    chunk.Write(checkpoint);
    chunk.Write(unknown24);
    chunk.Write(unknown28);
    chunk.Write(unknown2C);
    chunk.Write(unknown30);
    chunk.Write(unknown34);
    chunk.Write(ladderFrom);
    chunk.Write(ladderTo);
    chunk.Write(flags);
    chunk.Write(unknown54);
    chunk.Write(unknown58);
    chunk.Write(unknown4C);
    chunk.Ascend();
    chunk.Begin(CHUNK_MOVE_8AF);
    chunk.Write(unknown38);
    chunk.Write(unknown3C);
    chunk.Ascend();
    if (door)
        chunk.WriteChunk(CHUNK_MOVE_DOOR, door->GetNetId());
    if (ladder)
        chunk.WriteChunk(CHUNK_MOVE_LADDER, ladder->GetNetId());
    chunk.Ascend();
    return true;
}

bool S_AIMoveOrder::LoadGame(C_chunk &chunk)
{
    door = nullptr;
    ladder = nullptr;
    uint16_t id = 0;
    if (chunk.IsOpen() && chunk.Descend(id) && id == CHUNK_AI_PART) {
        uint16_t child;
        while (!chunk.AtEnd() && chunk.Descend(child)) {
            int actorId = -1;
            switch (child) {
            case CHUNK_MOVE:
                chunk.Read(&pos, SAVED_POS_SIZE);
                ReadIfFits(chunk, checkpoint);
                ReadIfFits(chunk, unknown24);
                ReadIfFits(chunk, unknown28);
                ReadIfFits(chunk, unknown2C);
                ReadIfFits(chunk, unknown30);
                chunk.Read(unknown34);
                chunk.Read(ladderFrom);
                chunk.Read(ladderTo);
                chunk.Read(flags);
                chunk.Read(unknown54);
                chunk.Read(unknown58);
                chunk.Read(unknown4C);
                break;
            case CHUNK_MOVE_DOOR:
                chunk.Read(actorId);
                if (C_Actor *actor = g_actorsModule.GetActorById(actorId)) {
                    if (actor->GetType() == ACTOR_DOOR)
                        door = actor;
                }
                break;
            case CHUNK_MOVE_LADDER:
                chunk.Read(actorId);
                if (C_Actor *actor = g_actorsModule.GetActorById(actorId)) {
                    if (actor->GetType() == ACTOR_LADDER)
                        ladder = actor;
                }
                break;
            case CHUNK_MOVE_8AF:
                chunk.Read(unknown38);
                chunk.Read(unknown3C);
                break;
            default:
                break;
            }
            chunk.Ascend();
        }
    }
    chunk.Ascend();
    return true;
}

// ------------------------------------------------------------------------- C_AIDriver

C_AIDriver::C_AIDriver()
{
    m_roads = GetGame()->GetCheckpoints()->GetRoadNetwork();
    SetPace(0x2000);                                    // MOVE_PACE_3
    ResetPath();
}

C_AIDriver::~C_AIDriver()
{
    // (The route's points were _aligned_malloc'd by the unreferenced planner; none are made
    // by the port.)
    m_route.clear();
}

void C_AIDriver::SetVehicle(C_Actor *vehicle)
{
    if (!vehicle && m_vehicle) {
        C_Car *car = static_cast<C_Car *>(m_vehicle);
        car->GetVehicle()->SetUnknown4f2630(car->GetUnknown780());
    }
    m_vehicle = vehicle;
    m_unknown88 = false;
}

void C_AIDriver::DriveTo(const S_vector4 &target, float speed)
{
    if (std::fabs(speed) > 1e-8f) {
        ClearFlags(FLAG_STOP);
        SetFlags(FLAG_NEW_TARGET);
        m_target = target;
        m_targetDir.x = std::numeric_limits<float>::quiet_NaN();   // (only x: none)
        m_speed = speed;
        return;
    }
    SetFlags(FLAG_STOP);
}

void C_AIDriver::DriveTo(const S_vector4 &target, const S_vector4 &targetDir, float speed)
{
    if (std::fabs(speed) > 1e-8f) {
        ClearFlags(FLAG_STOP);
        SetFlags(FLAG_NEW_TARGET);
        m_target = target;
        m_targetDir = targetDir;
        m_speed = speed;
        return;
    }
    SetFlags(FLAG_STOP);
}

void C_AIDriver::Stop()
{
    if (m_vehicle) {
        C_Car *car = static_cast<C_Car *>(m_vehicle);
        car->GetVehicle()->SetUnknown4f2630(car->GetUnknown780());
    }
    SetFlags(FLAG_STOP);
}

void C_AIDriver::ResetPath()
{
    m_path.clear();
    m_pathIndex = 0;
    m_progress = 0.0f;
    m_nearestDist2 = 1e8f;
    m_stuckTime = 0;
    m_noProgressTime = 0;
    m_moving = false;
    m_reversing = false;
    m_lastPos.x = m_lastPos.y = m_lastPos.z = 0.0f;
}

void C_AIDriver::PlanPath(const S_vector4 &from, const S_vector4 &to, const S_vector4 &beyond)
{
    const size_t oldCount = m_path.size();
    S_AIPathPoint point;
    // The start, unless the path already ends there (then it is no longer the destination).
    if (oldCount == 0) {
        point.pos = from;
        m_path.push_back(point);
    } else {
        const S_vector offset = Xyz(from) - Xyz(m_path.back().pos);
        if (offset.Square() <= 1e-8f) {
            m_path.back().last = false;
        } else {
            point.pos = from;
            point.last = false;
            m_path.push_back(point);
        }
    }

    // The road network's points between them, each moved within its radius toward the
    // straight line from the point before to the point after (cutting the curve).
    std::vector<uint16_t> roadPath;
    const int start = m_roads->FindNearest(Xyz(from), 10.0f, -1.0f);
    const int end = m_roads->FindNearest(Xyz(to), 10.0f, -1.0f);
    if (start != CHECKPOINT_NONE && end != CHECKPOINT_NONE && m_roads->FindPath(start, end, &roadPath, 0)) {
        for (size_t i = 0; i < roadPath.size(); ++i) {
            const S_vector4 &previous = m_path.back().pos;
            const S_Checkpoint *checkpoint = m_roads->Get(roadPath[i]);
            const S_vector center = checkpoint->pos;
            const S_vector next = i == roadPath.size() - 1 ? Xyz(to) : m_roads->Get(roadPath[i + 1])->pos;
            float radius = static_cast<float>(m_roads->Get(roadPath[i])->radius) * 0.0625f;
            // In the xz plane, relative to the point.
            const float aX = previous.x - center.x;
            const float aZ = previous.z - center.z;
            const float bX = next.x - center.x;
            const float bZ = next.z - center.z;
            const S_vector chord(bX - aX, 0.0f, bZ - aZ);
            // Only where the point lies between its neighbours along the chord (the signs of
            // the products, as the original tests them).
            if (!std::signbit(chord.x * aX + chord.z * aZ + chord.y * 0.0f) ||
                std::signbit(chord.x * bX + chord.z * bZ + chord.y * 0.0f))
                continue;
            S_vector normal(chord.z, 0.0f, -chord.x);
            normal.Normalize();
            float shift = normal.y * 0.0f + aX * normal.x + aZ * normal.z;
            if (shift <= radius) {
                const float limit = -radius;
                radius = shift;
                if (shift < limit)
                    radius = limit;
            }
            const S_vector moved = center + normal * radius;
            if ((moved - Xyz(m_path.back().pos)).Square() < 1e-8f)
                continue;
            point.pos = {moved.x, moved.y, moved.z, 0.0f};  // (w: a stale local in the original)
            point.last = false;
            m_path.push_back(point);
        }
    }

    // The destination.
    if ((Xyz(to) - Xyz(m_path.back().pos)).Square() <= 1e-8f) {
        m_path.back().last = true;
    } else {
        point.pos = to;
        point.last = true;
        m_path.push_back(point);
    }

    // The tangents, from the old last point on: half the sum of the distances to the
    // neighbours (the first: to the next; the last: from the previous and on to `beyond`).
    size_t i = oldCount > 0 ? oldCount - 1 : 0;
    while (i < m_path.size()) {
        S_vector dir;
        if (i == 0) {
            // Fix: the original reads past the end of a path of one point.
            dir = m_path.size() > 1 ? (Xyz(m_path[1].pos) - Xyz(m_path[0].pos)) * 0.5f : S_vector(0.0f, 0.0f, 0.0f);
        } else if (i == m_path.size() - 1) {
            const S_vector back = Xyz(m_path[i].pos) - Xyz(m_path[i - 1].pos);
            if (std::isnan(beyond.x))
                dir = back * 0.5f;
            else
                dir = ((Xyz(beyond) - Xyz(m_path[i].pos)) + back) * 0.5f;
        } else {
            dir = ((Xyz(m_path[i + 1].pos) - Xyz(m_path[i].pos)) + (Xyz(m_path[i].pos) - Xyz(m_path[i - 1].pos))) * 0.5f;
        }
        m_path[i].dir = {dir.x, dir.y, dir.z, 0.0f};
        ++i;
    }
}

bool C_AIDriver::SaveGame(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_DRIVER);
    if (m_flags) {
        chunk.Begin(CHUNK_DRIVER_FLAGS);
        chunk.Write(m_flags);
        chunk.Ascend();
    }
    chunk.WriteChunk(CHUNK_DRIVER_TARGET, &m_target, SAVED_POS_SIZE);
    if (m_vehicle)
        chunk.WriteChunk(CHUNK_DRIVER_VEHICLE, m_vehicle->GetNetId());
    chunk.Begin(CHUNK_DRIVER_UNKNOWN88);
    chunk.Write(m_unknown88);
    chunk.Ascend();
    chunk.WriteChunk(CHUNK_DRIVER_SPEED, m_speed);
    if (m_pace) {
        chunk.Begin(CHUNK_DRIVER_PACE);
        chunk.Write(m_pace);
        chunk.Ascend();
    }
    chunk.WriteChunk(CHUNK_DRIVER_LABEL, m_label);
    chunk.WriteChunk(CHUNK_DRIVER_TARGET_DIR, &m_targetDir, SAVED_POS_SIZE);
    chunk.WriteChunk(CHUNK_DRIVER_PATH_INDEX, static_cast<uint32_t>(m_pathIndex));
    chunk.WriteChunk(CHUNK_DRIVER_PROGRESS, m_progress);
    chunk.WriteChunk(CHUNK_DRIVER_NEAREST, m_nearestDist2);
    chunk.WriteChunk(CHUNK_DRIVER_STUCK_TIME, m_stuckTime);
    chunk.WriteChunk(CHUNK_DRIVER_NO_PROGRESS, m_noProgressTime);
    chunk.Begin(CHUNK_DRIVER_MOVING);
    chunk.Write(m_moving);
    chunk.Ascend();
    chunk.Begin(CHUNK_DRIVER_REVERSING);
    chunk.Write(m_reversing);
    chunk.Ascend();
    chunk.WriteChunk(CHUNK_DRIVER_LAST_POS, &m_lastPos, SAVED_POS_SIZE);
    for (const S_AIPathPoint &point : m_path) {
        chunk.Begin(CHUNK_DRIVER_POINT);
        chunk.Write(&point.pos, SAVED_POS_SIZE);
        chunk.Write(&point.dir, SAVED_POS_SIZE);
        chunk.Write(point.last);
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}

bool C_AIDriver::LoadGame(C_chunk &chunk)
{
    m_vehicle = nullptr;
    m_flags = 0;
    m_pace = 0;
    m_unknown88 = false;
    m_route.clear();
    uint16_t id = 0;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id == CHUNK_DRIVER) {
        // (The path is added to, not cleared first.)
        uint16_t child;
        while (!chunk.AtEnd() && chunk.Descend(child)) {
            int value = 0;
            float number = 0.0f;
            S_vector4 vector = {};
            switch (child) {
            case CHUNK_DRIVER_TARGET_DIR:
                ReadIfFits(chunk, Xyz(vector));
                m_targetDir = vector;
                break;
            case CHUNK_DRIVER_SPEED:
                chunk.Read(number);
                m_speed = number;
                break;
            case CHUNK_DRIVER_PACE:
                ReadIfFits(chunk, m_pace);
                break;
            case CHUNK_DRIVER_LABEL:
                chunk.Read(value);
                m_label = value;
                break;
            case CHUNK_DRIVER_UNKNOWN88:
                ReadIfFits(chunk, m_unknown88);
                break;
            case CHUNK_DRIVER_VEHICLE:
                chunk.Read(value);
                m_vehicle = g_actorsModule.GetActorById(value);
                break;
            case CHUNK_DRIVER_FLAGS:
                ReadIfFits(chunk, m_flags);
                break;
            case CHUNK_DRIVER_TARGET:
                chunk.Read(&vector, SAVED_POS_SIZE);
                m_target.x = vector.x;
                m_target.y = vector.y;
                m_target.z = vector.z;
                break;
            case CHUNK_DRIVER_POINT: {
                S_AIPathPoint point;
                chunk.Read(&point.pos, SAVED_POS_SIZE);
                chunk.Read(&point.dir, SAVED_POS_SIZE);
                chunk.Read(point.last);
                m_path.push_back(point);
                break;
            }
            case CHUNK_DRIVER_NEAREST:
                ReadIfFits(chunk, m_nearestDist2);
                break;
            case CHUNK_DRIVER_NO_PROGRESS:
                ReadIfFits(chunk, m_noProgressTime);
                break;
            case CHUNK_DRIVER_LAST_POS:
                ReadIfFits(chunk, Xyz(vector));
                m_lastPos = vector;
                break;
            case CHUNK_DRIVER_REVERSING:
                chunk.Read(m_reversing);
                break;
            case CHUNK_DRIVER_STUCK_TIME:
                ReadIfFits(chunk, m_stuckTime);
                break;
            case CHUNK_DRIVER_PATH_INDEX:
                ReadIfFits(chunk, value);
                m_pathIndex = static_cast<uint16_t>(value);
                break;
            case CHUNK_DRIVER_PROGRESS:
                ReadIfFits(chunk, m_progress);
                break;
            case CHUNK_DRIVER_MOVING:
                chunk.Read(m_moving);
                break;
            default:
                break;
            }
            chunk.Ascend();
        }
    }
    chunk.Ascend();
    return true;
}

// ------------------------------------------------------------------------- S_AIAlertState

void S_AIAlertState::Set(int value)
{
    if (state != value) {
        state = value;
        changed = true;
    }
}

bool S_AIAlertState::SaveGame(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_AI_PART);
    chunk.Begin(CHUNK_ALERT);
    chunk.Write(state);
    chunk.Write(changed);
    chunk.Ascend();
    chunk.Ascend();
    return true;
}

bool S_AIAlertState::LoadGame(C_chunk &chunk)
{
    uint16_t id = 0;
    if (chunk.IsOpen() && chunk.Descend(id) && id == CHUNK_AI_PART) {
        uint16_t child;
        while (!chunk.AtEnd() && chunk.Descend(child)) {
            if (child == CHUNK_ALERT) {
                // (Fix: the original stores an uninitialized value when the chunk is short.)
                ReadIfFits(chunk, state);
                ReadIfFits(chunk, changed);
            }
            chunk.Ascend();
        }
    }
    chunk.Ascend();
    return true;
}

// ------------------------------------------------------------------------- S_AIFire

bool S_AIFire::SaveGame(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_FIRE);
    chunk.Write(unknown00);
    chunk.Write(burstTime);
    chunk.Write(pauseTime);
    chunk.Write(flags);
    chunk.Write(&throwPos, SAVED_POS_SIZE);
    chunk.Ascend();
    chunk.Begin(CHUNK_FIRE_UNKNOWN0C);
    chunk.Write(unknown0C);
    chunk.Ascend();
    WriteActorChunk(chunk, CHUNK_FIRE_TARGET, target);
    WriteActorChunk(chunk, CHUNK_FIRE_LAST_TARGET, orderTarget);
    return true;
}

bool S_AIFire::LoadGame(C_chunk &chunk)
{
    target = nullptr;
    orderTarget = nullptr;
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_FIRE:
            ReadIfFits(chunk, unknown00);
            ReadIfFits(chunk, burstTime);
            ReadIfFits(chunk, pauseTime);
            ReadIfFits(chunk, flags);
            chunk.Read(&throwPos, SAVED_POS_SIZE);
            break;
        case CHUNK_FIRE_TARGET:
            target = ReadActor(chunk);
            break;
        case CHUNK_FIRE_LAST_TARGET:
            orderTarget = ReadActor(chunk);
            break;
        case CHUNK_FIRE_UNKNOWN0C:
            ReadIfFits(chunk, unknown0C);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    return true;
}

// ------------------------------------------------------------------------- the tasks

bool S_AITask::SaveGame(C_chunk &chunk) const
{
    chunk.Write(type);
    chunk.Write(unknown08);
    chunk.Write(unknown0C);
    chunk.Write(unknown10);
    return true;
}

bool S_AITask::LoadGame(C_chunk &chunk)
{
    // (Fix: the original compares an uninitialized value when the chunk is short.)
    int savedType = -1;
    ReadIfFits(chunk, savedType);
    if (savedType != type)
        return false;
    ReadIfFits(chunk, unknown08);
    ReadIfFits(chunk, unknown0C);
    ReadIfFits(chunk, unknown10);
    return true;
}

namespace {

void SaveTaskBase(const S_AITask &task, C_chunk &chunk)
{
    chunk.Begin(CHUNK_TASK_BASE);
    task.S_AITask::SaveGame(chunk);
    chunk.Ascend();
}

}  // namespace

bool S_AITask1::SaveGame(C_chunk &chunk) const
{
    SaveTaskBase(*this, chunk);
    chunk.Begin(CHUNK_TASK1);
    chunk.Write(&pos, SAVED_POS_SIZE);
    chunk.Write(&dir, SAVED_POS_SIZE);
    chunk.Write(unknown40);
    chunk.Write(unknown44);
    chunk.Write(checkpoint);
    chunk.Ascend();
    return true;
}

bool S_AITask1::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        if (id == CHUNK_TASK_BASE) {
            S_AITask::LoadGame(chunk);
        } else if (id == CHUNK_TASK1) {
            chunk.Read(&pos, SAVED_POS_SIZE);
            chunk.Read(&dir, SAVED_POS_SIZE);
            ReadIfFits(chunk, unknown40);
            ReadIfFits(chunk, unknown44);
            ReadIfFits(chunk, checkpoint);
        }
        chunk.Ascend();
    }
    return true;
}

bool S_AITask2::SaveGame(C_chunk &chunk) const
{
    SaveTaskBase(*this, chunk);
    const int humanId = human ? human->GetNetId() : -1;
    chunk.Begin(CHUNK_TASK2);
    chunk.Write(&unknown20, SAVED_POS_SIZE);
    chunk.Write(&unknown30, SAVED_POS_SIZE);
    chunk.Write(unknown40);
    chunk.Write(humanId);
    chunk.Ascend();
    return true;
}

bool S_AITask2::LoadGame(C_chunk &chunk)
{
    int humanId = -1;                                   // (kept from one chunk to the next)
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        if (id == CHUNK_TASK_BASE) {
            S_AITask::LoadGame(chunk);
        } else if (id == CHUNK_TASK2) {
            chunk.Read(&unknown20, SAVED_POS_SIZE);
            chunk.Read(&unknown30, SAVED_POS_SIZE);
            ReadIfFits(chunk, unknown40);
            ReadIfFits(chunk, humanId);
            if (humanId == -1) {
                human = nullptr;
            } else {
                // (Another actor leaves the task's human as it was.)
                C_Actor *actor = g_actorsModule.GetActorById(humanId);
                if (actor && actor->GetType() == ACTOR_HUMAN)
                    human = static_cast<C_Human *>(actor);
            }
        }
        chunk.Ascend();
    }
    return true;
}

bool S_AITask3::SaveGame(C_chunk &chunk) const
{
    SaveTaskBase(*this, chunk);
    const int actorId = actor ? actor->GetNetId() : -1;
    chunk.Begin(CHUNK_TASK3);
    chunk.Write(&pos, SAVED_POS_SIZE);
    chunk.Write(actorId);
    chunk.Ascend();
    return true;
}

bool S_AITask3::LoadGame(C_chunk &chunk)
{
    int actorId = -1;
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        if (id == CHUNK_TASK_BASE) {
            S_AITask::LoadGame(chunk);
        } else if (id == CHUNK_TASK3) {
            if (chunk.RemainingSize() >= SAVED_POS_SIZE)
                chunk.Read(&pos, SAVED_POS_SIZE);
            ReadIfFits(chunk, actorId);
            if (actorId == -1) {
                actor = nullptr;
            } else if (C_Actor *found = g_actorsModule.GetActorById(actorId)) {
                actor = found;
            }
        }
        chunk.Ascend();
    }
    return true;
}

namespace {

uint16_t EmptyTaskChunk(int type)
{
    switch (type) {
    case 4: return CHUNK_TASK4;
    case 5: return CHUNK_TASK5;
    default: return CHUNK_TASK6;
    }
}

}  // namespace

bool S_AITaskEmpty::SaveGame(C_chunk &chunk) const
{
    SaveTaskBase(*this, chunk);
    chunk.Begin(EmptyTaskChunk(type));
    chunk.Ascend();
    return true;
}

bool S_AITaskEmpty::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        if (id == CHUNK_TASK_BASE)
            S_AITask::LoadGame(chunk);
        chunk.Ascend();
    }
    return true;
}
