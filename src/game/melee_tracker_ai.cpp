// The meleeTracker's AI controller (C_MeleeTrackerAI): the script-command API (C_MeleeTrackerScript::ExecuteCommand),
// the per-frame update and its decision stages (the target, heel, path following, sight test),
// the checkpoint pathfinder and save / load (the route around parked cars, RouteAroundCars, is in
// game/melee_tracker_route.cpp). Still to port: the other controller variants (modes 1, 4, 7).
#include "game/melee_tracker_ai.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>

#include "common/chunk.h"
#include "common/object_pool.h"
#include "game/actor.h"
#include "game/actors_module.h"                         // g_actorsModule (owner by id)
#include "game/ai_parts.h"
#include "game/app.h"                                   // MicrosecondsNow
#include "game/checkpoints.h"
#include "game/controller_messages.h"                   // Read / WriteControlState
#include "game/game.h"                                  // GetGame()->GetCheckpoints()
#include "game/game_internal.h"                         // IsServer / IsClient / IsLocalGame
#include "game/human.h"                                 // the target's health / vehicle / velocity
#include "game/melee_tracker_movement.h"
#include "game/sound_module.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"                              // C_NetMessage
#include "script/script.h"

namespace {

// The movement sub-object of the meleeTracker (embedded; C_MeleeTracker +0x90 in the original).
C_MeleeTrackerMovement *MeleeTrackerMovement(C_MeleeTracker *meleeTracker)
{
    return &meleeTracker->GetMovement();
}

// The human actor a frame carries (meleeTrackers take orders about / attack humans), null otherwise.
C_Actor *HumanOfFrame(I3D_frame *frame)
{
    if (!frame || !frame->GetData())
        return nullptr;
    C_Actor *actor = frame->GetData()->actor;
    return (actor && actor->GetType() == ACTOR_HUMAN) ? actor : nullptr;
}

// The meleeTracker's waypoint pool (0x008ac920; the original co-locates a second 0x20-byte pool for the
// path-following geometry, ported with that code). Filled with 16 nodes at a time.
C_ObjectPool<S_AIWaypoint> g_meleeTrackerWaypoints{16};  // constructed by 0x005ce2c0 (DS 0x005e6a20)

}  // namespace

S_AIWaypoint *AllocMeleeTrackerWaypoint(int checkpoint, const S_vector4 &pos, uint16_t flags,
                               uint8_t type, uint8_t radiusByte)
{
    S_AIWaypoint *waypoint = g_meleeTrackerWaypoints.Get();
    waypoint->checkpoint = checkpoint;
    waypoint->pos = pos;
    waypoint->pos2 = pos;
    waypoint->flags = flags;
    waypoint->type = type;
    waypoint->radius = static_cast<float>(radiusByte) * 0.0625f;   // the radius byte is in 1/16 m
    return waypoint;
}

void ReleaseMeleeTrackerWaypoint(S_AIWaypoint *waypoint)
{
    if (waypoint)
        g_meleeTrackerWaypoints.Put(waypoint);
}

// ------------------------------------------------------------------ movement orders

void C_MeleeTrackerAI::MoveTo(uint16_t checkpoint)
{
    S_Checkpoint *point = m_checkpoints->Get(checkpoint);
    const S_vector4 pos = m_meleeTracker->GetPosition4();
    MeleeTrackerMovement(m_meleeTracker)->SetAnimSpeed(1.0f);
    // (Fix: the script is advanced only when there is one; the original reads the scripted
    // controller's +0xC0 whatever the variant.)
    C_Script *script = ScriptOf();
    if (!point) {
        if (script)
            script->Advance();
        return;
    }
    const float from[4] = {pos.x, pos.y, pos.z, pos.w};
    const float to[4] = {point->pos.x, point->pos.y, point->pos.z, 1.0f};
    if (!FindPath(from, to)) {
        if (script)
            script->Advance();
        return;
    }
    m_flags |= MELEE_TRACKER_ORDER_MOVETO | MELEE_TRACKER_ORDER_ACTIVE;
}

void C_MeleeTrackerAI::Attack(I3D_frame *target)
{
    m_flags &= ~MELEE_TRACKER_ORDER_ACTIVE;
    ClearAttack();
    if (!target) {
        m_orderedTarget = nullptr;
    } else if (C_Actor *human = HumanOfFrame(target)) {
        m_orderedTarget = human;
    }
    BeginAttack();
    m_flags |= MELEE_TRACKER_ORDER_ATTACK | MELEE_TRACKER_ORDER_ACTIVE;
}

// ------------------------------------------------------------------ stance / mode

void C_MeleeTrackerAI::SetModeRun()
{
    m_flags = (m_flags & ~MELEE_TRACKER_ORDER_WALK) | MELEE_TRACKER_ORDER_RUN | MELEE_TRACKER_ORDER_ACTIVE;
}

void C_MeleeTrackerAI::SetModeWalk()
{
    m_flags = (m_flags & ~MELEE_TRACKER_ORDER_RUN) | MELEE_TRACKER_ORDER_WALK | MELEE_TRACKER_ORDER_ACTIVE;
}

void C_MeleeTrackerAI::SetModeLie()
{
    m_flags = (m_flags & ~MELEE_TRACKER_ORDER_STAND) | MELEE_TRACKER_ORDER_LIE | MELEE_TRACKER_ORDER_ACTIVE;
}

void C_MeleeTrackerAI::SetModeStand()
{
    m_flags = (m_flags & ~MELEE_TRACKER_ORDER_LIE) | MELEE_TRACKER_ORDER_STAND | MELEE_TRACKER_ORDER_ACTIVE;
}

void C_MeleeTrackerAI::SetAggressive()
{
    m_flags = (m_flags & ~(MELEE_TRACKER_ORDER_DEFENSIVE | MELEE_TRACKER_ORDER_HAS_TARGET)) | MELEE_TRACKER_ORDER_AGGRESSIVE | MELEE_TRACKER_ORDER_ACTIVE;
}

void C_MeleeTrackerAI::SetDefensive()
{
    m_flags = (m_flags & ~(MELEE_TRACKER_ORDER_AGGRESSIVE | MELEE_TRACKER_ORDER_HAS_TARGET)) | MELEE_TRACKER_ORDER_DEFENSIVE | MELEE_TRACKER_ORDER_ACTIVE;
}

// ------------------------------------------------------------------ optimisation / ranges

void C_MeleeTrackerAI::Suspend(bool on)
{
    m_flags |= MELEE_TRACKER_ORDER_ACTIVE;
    m_meleeTracker->SetOptimized(on);
}

void C_MeleeTrackerAI::SetOptimized(bool on)
{
    m_flags |= MELEE_TRACKER_ORDER_ACTIVE;
    MeleeTrackerMovement(m_meleeTracker)->SetOptimizedController(on);
}

void C_MeleeTrackerAI::SetSightRange(int range)
{
    m_meleeTracker->GetStats().sightRange = range;      // C_MeleeTracker +0x4A8
}

void C_MeleeTrackerAI::SetHearRange(int range)
{
    m_meleeTracker->GetStats().hearRange = range;       // C_MeleeTracker +0x4AC
}

void C_MeleeTrackerAI::SetOwner(I3D_frame *owner)
{
    SetOwnerActor(HumanOfFrame(owner));
}

// ------------------------------------------------------------------ barking / alarm

int C_MeleeTrackerScriptedAI::SetBarking(bool on)       // 0x005cfbb0
{
    if (on) {
        const int r = std::rand();
        m_barkTimer = r % 2000;
        return r / 2000;
    }
    m_barkTimer = -1;
    return on ? 1 : 0;
}

void C_MeleeTrackerScriptedAI::SetAlarm(int value)      // 0x005cf8e0
{
    if (value == 0) {
        m_alarm = 0;
        m_flags &= ~MELEE_TRACKER_ORDER_ALARM;
        return;
    }
    // (The original's `(flags & 0x80000000) != 0xFFFFFFFF` guard is always true, so the alarm is
    // always raised.)
    m_alarm = value;
    m_flags |= MELEE_TRACKER_ORDER_ALARM;
    m_script->OnAlarm();
}

void C_MeleeTrackerScriptedAI::SetScript(C_Script *script)   // 0x005d5bc0
{
    m_script = script;
}

// ===========================================================================
// The AI update pipeline (part 2b): orchestration, behaviour apply, attack/owner management.

// Turns the meleeTracker to bark at something (game/melee_tracker_movement.cpp).
void MeleeTrackerBarkTurn(C_MeleeTracker *meleeTracker);                           // 0x005c6c70

namespace {
// The AI's shared behaviour-weight buffers (one meleeTracker is processed at a time). The decision stages
// raise weights here; Apply commits the chosen ones to the movement.
float g_meleeTrackerActionA[0x40];                               // 0x008AC820
float g_meleeTrackerActionB[0x40];                               // 0x008AC720
}  // namespace

// ------------------------------------------------------------------ the per-frame update

void C_MeleeTrackerAI::UpdateTimers(int timeDelta)
{
    if (m_0b0 > 0)
        m_0b0 -= timeDelta;
    if (m_0bc > 0)
        m_0bc -= timeDelta;
    if (m_084 > 0)
        m_084 -= timeDelta;
    if (m_084 < 0)
        m_084 = 0;
}

void C_MeleeTrackerAI::SubUpdate(int /*timeDelta*/)
{
    // Base stance weights from the orders flags.
    if (m_flags & 0x4)
        m_actionA[0] = 1.0f;
    else if (m_flags & 0x8)
        m_actionA[1] = 1.0f;
    if ((m_flags & 0x4000) == 0 || (m_flags & 0x20000000) != 0)
        m_actionA[5] = 0.0f;
    else
        m_actionA[5] = 1.0f;
    if ((m_flags & 0x3) == 0) {
        m_actionB[9] = 0.0f;
    } else {
        m_actionB[9] = 1.0f;
        m_flags &= ~0x3u;
    }
    if (m_flags & 0x80000)
        m_actionA[4] = 1.0f;
}

void C_MeleeTrackerController::Apply()                  // 0x005cec60
{
    C_MeleeTrackerMovement *mv = MeleeTrackerMovement(m_meleeTracker);
    m_savedPos = {0.0f, 0.0f, 0.0f, m_savedPos.w};      // (the AI move accumulator is cleared)
    mv->SetMode(3, 0);
    if (m_actionA[0] != 0.0f)
        mv->SetMode(1, 0);
    if (m_actionA[1] != 0.0f)
        mv->SetMode(0, 0);
    if (m_actionB[6] != 0.0f)
        mv->SetMode(2, 0);
    mv->SetSubModeA(m_actionA[5] != 0.0f ? 1 : 0, 0);
    if (m_actionB[0x3b] != 0.0f)
        mv->SetMode(5, 0);
    if (m_actionB[0x3c] != 0.0f || m_actionA[4] != 0.0f)
        mv->SetMode(4, 0);
    if (m_actionB[9] != 0.0f) {
        int value;
        if (mv->GetSeqAnimC() == 1)
            value = 0;
        else if (mv->GetSeqAnimC() != 0)
            value = -1;                                 // (skip: the original falls through)
        else
            value = 1;
        if (value != -1)
            mv->SetSubModeB(value, 0);
    }
    if (m_actionB[0x3c] != 0.0f)
        MeleeTrackerBarkTurn(m_meleeTracker);
}

void C_MeleeTrackerAI::Update(int timeDelta)
{
    if (m_flags & 0x800)
        return;
    UpdateTimers(timeDelta);
    for (int i = 0; i < 0x40; ++i)
        g_meleeTrackerActionA[i] = 0.0f;
    for (int i = 0; i < 0x40; ++i)
        g_meleeTrackerActionB[i] = 0.0f;
    m_actionA = g_meleeTrackerActionA;
    m_actionB = g_meleeTrackerActionB;
    CullSightings(timeDelta);
    DecideTarget(timeDelta);
    DecideMove(timeDelta);
    FollowPath(timeDelta);
    SubUpdate(timeDelta);
    Apply();
    m_flags &= ~0x80000u;
    MeleeTrackerMovement(m_meleeTracker)->SetFacing(&m_facing.x);
}

// ------------------------------------------------------------------ lifecycle

// The root's set-up, inlined by SetController for every variant (0x005d5890).
C_MeleeTrackerController::C_MeleeTrackerController(int mode)
{
    m_meleeTracker = nullptr;
    m_mode = mode;
    m_flags = MELEE_TRACKER_ORDER_WALK;                 // 0x2000
    m_savedPos = {0.0f, 0.0f, 1.0f, 0.0f};
    m_facing = {0.0f, 0.0f, 1.0f, 0.0f};
    m_actionA = nullptr;
    m_actionB = nullptr;
    m_068 = 0;
}

C_MeleeTrackerController::~C_MeleeTrackerController() = default;   // 0x005cb360 (DS 0x005e2db0)

bool C_MeleeTrackerController::SetMeleeTracker(C_MeleeTracker *meleeTracker)   // 0x005ce630
{
    m_meleeTracker = meleeTracker;
    Reset();
    return true;
}

bool C_MeleeTrackerController::Attach(C_MeleeTracker *meleeTracker)   // 0x005ce630
{
    return SetMeleeTracker(meleeTracker);
}

void C_MeleeTrackerController::Reset()                  // 0x005ce5d0
{
    const S_vector4 dir = m_meleeTracker->GetDirection4();
    m_facing = dir;
    m_050 = dir;
    m_alarm = 0;
    m_flags = 0;
    m_owner = nullptr;
    m_actionA = nullptr;
    m_actionB = nullptr;
}

bool C_MeleeTrackerController::Detach()                 // 0x005cb230 (DS 0x005e2b60)
{
    m_meleeTracker = nullptr;
    return true;
}

void C_MeleeTrackerController::SetOwnerActor(C_Actor *owner)   // 0x005cb2a0
{
    m_owner = owner;
    if (owner)
        m_flags |= 0x10;
    else
        m_flags &= ~0x10u;
}

C_MeleeTrackerAI::C_MeleeTrackerAI(int mode)            // 0x005cb190
    : C_MeleeTrackerController(mode)
{
    m_074 = static_cast<uint8_t>(mode);
    m_pathBegin = m_pathEnd = m_pathCap = nullptr;
    InitBase();
}

void C_MeleeTrackerAI::InitBase()                       // 0x005cb0d0
{
    m_checkpoints = GetGame()->GetCheckpoints();
    // Fix: the original leaves the two w components uninitialised (a stack word); they are unused.
    m_090 = {-10000.0f, -10000.0f, -10000.0f, 0.0f};
    m_0a0 = {-10000.0f, -10000.0f, -10000.0f, 0.0f};
    m_target = nullptr;
    m_orderedTarget = nullptr;
    m_owner = nullptr;
    m_0b0 = 0;
    m_084 = 0;
    m_0b4 = 0;
    m_changed = 0;
    m_0bc = 0;
}

C_MeleeTrackerAI::~C_MeleeTrackerAI()                   // 0x005d5b50
{
    ::operator delete(m_pathBegin);                     // the array only (Detach freed the waypoints)
    m_pathBegin = m_pathEnd = m_pathCap = nullptr;
}

void C_MeleeTrackerAI::Reset()                          // 0x005cb0d0
{
    InitBase();
}

bool C_MeleeTrackerAI::Detach()                         // 0x005cb410
{
    return ResetPath();
}

void C_MeleeTrackerAI::Tick(int timeDelta)              // 0x005ce190
{
    Update(timeDelta);
}

bool C_MeleeTrackerAI::TakeChanged()                    // 0x005cb380
{
    const bool changed = m_changed != 0;
    m_changed = 0;
    return changed;
}

// --- the scripted tracker (mode 5) ---

C_MeleeTrackerScriptedAI::C_MeleeTrackerScriptedAI()    // (SetController's case 5)
    : C_MeleeTrackerAI(5)
{
    m_script = nullptr;
}

C_MeleeTrackerScriptedAI::~C_MeleeTrackerScriptedAI() = default;   // 0x005d5bd0 (DS 0x005f0640)

bool C_MeleeTrackerScriptedAI::Attach(C_MeleeTracker *meleeTracker)   // 0x005cf6c0
{
    SetMeleeTracker(meleeTracker);
    const S_FrameData *data = m_meleeTracker->GetFrame()->GetData();
    m_script = data ? data->script : nullptr;
    return m_script != nullptr;
}

void C_MeleeTrackerScriptedAI::Reset()                  // 0x005d5b80
{
    C_MeleeTrackerController::Reset();
    m_barkTimer = -1;                                   // no idle barks
}

bool C_MeleeTrackerScriptedAI::Detach()                 // 0x005d5bb0 (DS 0x005f0600)
{
    m_meleeTracker = nullptr;
    ResetPath();
    return true;
}

void C_MeleeTrackerScriptedAI::Tick(int timeDelta)      // 0x005cfa20
{
    Update(timeDelta);
    if (m_barkTimer < 0)
        return;
    m_barkTimer -= timeDelta;
    if (m_barkTimer > 0)
        return;
    m_barkTimer = std::rand() % 25000 + 1000;
    // An idle bark from the mouth bone, positioned relative to (and moving with) the actor's frame.
    I3D_frame *frame = m_meleeTracker->GetFrame();
    const S_vector &mouth = frame->FindChildFrame("d", FRAME_MASK_ANY)->GetWorldPos();   // 0x008387e4
    const S_vector4 pos = m_meleeTracker->GetPosition4();
    S_SoundRequest request;
    request.pos = S_vector(mouth.x - pos.x, mouth.y - pos.y, mouth.z - pos.z);
    // Fix: the original copied an uninitialised stack word into posW; the default is kept.
    request.flags = SOUND_SILENT_TO_AI;
    request.attachTo = frame;
    request.source = nullptr;
    request.chapter = 0xB;
    request.group = 10;
    GetSoundModule()->Play(request);
}

C_Script *C_MeleeTrackerAI::ScriptOf()
{
    return m_mode == 5 ? static_cast<C_MeleeTrackerScriptedAI *>(this)->m_script : nullptr;
}

// --- the autonomous tracker (mode 4) ---

C_MeleeTrackerAutoAI::C_MeleeTrackerAutoAI()            // (SetController's case 4)
    : C_MeleeTrackerAI(4)
{
}

C_MeleeTrackerAutoAI::~C_MeleeTrackerAutoAI() = default;   // 0x005d5b30

bool C_MeleeTrackerAutoAI::Attach(C_MeleeTracker *meleeTracker)   // 0x005cf0a0 (DS 0x005e7c10)
{
    SetMeleeTracker(meleeTracker);
    return true;
}

void C_MeleeTrackerAutoAI::Reset()                      // 0x005d5b00
{
    C_MeleeTrackerController::Reset();
}

bool C_MeleeTrackerAutoAI::Detach()                     // 0x005d5b20 (DS 0x005f0520)
{
    m_meleeTracker = nullptr;
    ResetPath();
    return true;
}

void C_MeleeTrackerAutoAI::Tick(int timeDelta)          // 0x005cf0d0
{
    Update(timeDelta);
}

bool C_MeleeTrackerAutoAI::SaveGame(C_chunk &chunk)     // 0x005d2da0
{
    return C_MeleeTrackerController::SaveGame(chunk);
}

bool C_MeleeTrackerAutoAI::LoadGame(C_chunk &chunk)     // 0x005d2db0
{
    return C_MeleeTrackerController::LoadGame(chunk);
}

void C_MeleeTrackerController::ResetState(C_MeleeTrackerController *source)   // 0x005ce650
{
    m_flags = 0;
    m_savedPos = source->m_savedPos;
    m_facing = source->m_facing;
    m_050 = source->m_050;
    SetOwnerActor(source->m_owner);
    m_actionA = nullptr;
    m_actionB = nullptr;
}

void C_MeleeTrackerController::Disable()                // 0x005ce6e0
{
    m_flags |= 0x800;
}

void C_MeleeTrackerScriptedAI::ReleaseScript()          // 0x005cf8b0
{
    m_flags &= ~(MELEE_TRACKER_ORDER_ACTIVE | MELEE_TRACKER_ORDER_SUSPEND);
    if (m_mode == 5)
        m_meleeTracker->SetController(4);               // deletes this controller: nothing after it
}

void C_MeleeTrackerController::SetAlarm(int value)     // 0x005ce700
{
    if (value == 0) {
        m_alarm = 0;
        m_flags &= ~MELEE_TRACKER_ORDER_ALARM;
        return;
    }
    // (same vacuously-true guard as ClearAlarm, without the script notification)
    m_alarm = value;
    m_flags |= MELEE_TRACKER_ORDER_ALARM;
}

// ------------------------------------------------------------------ attack / owner

void C_MeleeTrackerAI::BeginAttack()
{
    m_0b4 = 200 - std::rand() % 400;
    FindTarget(true);
    if (m_target) {
        if (CheckReached(1)) {
            m_flags = (m_flags & ~0x2000u) | 0x24000;
            return;
        }
        if (m_0b0 < 1) {
            m_0b0 = std::rand() % 0x708 + 100;
            m_flags |= 0x80000;
        }
    }
}

void C_MeleeTrackerAI::ClearAttack()
{
    m_090 = {-10000.0f, -10000.0f, -10000.0f, m_090.w};
    m_0a0 = {-10000.0f, -10000.0f, -10000.0f, m_0a0.w};
    const uint32_t flags = m_flags;
    m_flags &= ~0x30000u;
    m_target = nullptr;
    m_orderedTarget = nullptr;
    C_Script *script = ScriptOf();                      // (mode 5 only)
    if ((flags & 0x100000) != 0 && script && (flags & 0x40000) != 0) {
        script->Advance();
        m_flags &= ~0x100000u;
    }
}

void C_MeleeTrackerAI::OwnerPosition(float *out)
{
    C_Actor *owner = m_owner;
    if (!owner)
        return;
    const S_vector4 dir = owner->GetDirection4();
    S_vector offset(dir.x, dir.y, dir.z);
    RotateAboutY(offset, 1.5707964f);
    const S_vector4 pos = owner->GetPosition4();
    out[0] = offset.x + pos.x;
    out[1] = offset.y + pos.y;
    out[2] = offset.z + pos.z;
    out[3] = pos.w;
}

void C_MeleeTrackerAI::SetOwnerActor(C_Actor *owner)
{
    m_owner = owner;
    if (owner) {
        m_flags |= 0x10;
        OwnerPosition(&m_0a0.x);
        const S_vector4 pos = m_meleeTracker->GetPosition4();
        const float from[4] = {pos.x, pos.y, pos.z, pos.w};
        FindPath(from, &m_0a0.x);
        return;
    }
    m_flags &= ~0x10u;
    m_0a0 = {-10000.0f, -10000.0f, -10000.0f, m_0a0.w};
}

// ===========================================================================
// Decision-stage helpers (part 2b-ii).

namespace {
// Normalises (x,y,z), snapping to the nearest axis when too short, leaving a near-unit vector
// alone (the AI's inlined normalise pattern).
void NormalizeXYZ(float &x, float &y, float &z)
{
    const float sq = x * x + y * y + z * z;
    if (std::fabs(sq - 1.0f) < 1e-08f)
        return;
    if (sq >= 1e-08f) {
        const float inv = 1.0f / std::sqrt(sq);
        x *= inv;
        y *= inv;
        z *= inv;
    } else if (x != 0.0f) {
        x = x >= 0.0f ? 1.0f : -1.0f;
    } else if (z == 0.0f) {
        y = y >= 0.0f ? 1.0f : -1.0f;
    } else {
        z = z >= 0.0f ? 1.0f : -1.0f;
    }
}

void NormalizeFacing(S_vector4 &v) { NormalizeXYZ(v.x, v.y, v.z); }
}  // namespace

void C_MeleeTrackerAI::SetMoveToward(const float *from, const float *to)
{
    m_facing.x = to[0] - from[0];
    m_facing.y = 0.0f;
    m_facing.z = to[2] - from[2];
    NormalizeFacing(m_facing);
}

void C_MeleeTrackerAI::OnHeardSound(S_MeleeTrackerSighting *sighting)   // 0x005cddd0
{
    if (sighting->loudness > 9 && m_0bc < 1) {
        m_changed = 1;
        m_flags |= 0x80000;
        m_0bc = std::rand() % 300 + 800;
    }
}

void C_MeleeTrackerAI::OnSpottedEnemy(C_Actor * /*target*/)   // 0x005cde20 (the target is unread)
{
    if (m_0bc < 1) {
        m_changed = 1;
        m_flags |= 0x80000;
        m_0bc = std::rand() % 300 + 800;
    }
}

void C_MeleeTrackerAI::OnSeenHuman(int /*type*/, C_Actor *actor)   // 0x005cdf30
{
    if (m_flags & 0x80000) {
        m_target = nullptr;
        m_flags &= ~0x80000u;
        m_0b4 = 200 - std::rand() % 0x5DC;
    }
    if ((m_flags & 0x200) != 0 && actor && actor->GetType() == ACTOR_HUMAN) {
        m_orderedTarget = actor;
        m_flags |= 0x400;
    }
}

// ===========================================================================
// Part 2c: the checkpoint path. FindPath resolves the checkpoints nearest the ends, the graph
// search (C_CheckpointNetwork::FindMeleeTrackerPath) returns the checkpoint sequence, and the controller
// turns it into a list of waypoints (S_AIWaypoint, from the meleeTracker's pool), smoothing the corners.

namespace {

// Length of a vector from its squared magnitude, clamped the way the AI's inlined helper does:
// 0 below ~1e-8, exactly 1 for a near-unit square, the square root otherwise.
float SafeLength(float sq)
{
    if (sq < 1e-08f)
        return 0.0f;
    if (std::fabs(sq - 1.0f) >= 1e-08f)
        return std::sqrt(sq);
    return 1.0f;
}

// Appends a waypoint to the controller's path, growing the array as the original does (double the
// size, or one when empty). 0x005cadd0 (the end-insertion the search uses).
void PathPushBack(C_MeleeTrackerAI *controller, S_AIWaypoint *node)
{
    if (controller->m_pathEnd != controller->m_pathCap) {
        *controller->m_pathEnd++ = node;
        return;
    }
    const std::ptrdiff_t oldSize =
        controller->m_pathBegin ? controller->m_pathEnd - controller->m_pathBegin : 0;
    const std::ptrdiff_t newCap = oldSize + (oldSize < 2 ? 1 : oldSize);
    S_AIWaypoint **buffer =
        static_cast<S_AIWaypoint **>(operator new(newCap * sizeof(S_AIWaypoint *)));
    for (std::ptrdiff_t i = 0; i < oldSize; ++i)
        buffer[i] = controller->m_pathBegin[i];
    buffer[oldSize] = node;
    operator delete(controller->m_pathBegin);
    controller->m_pathBegin = buffer;
    controller->m_pathEnd = buffer + oldSize + 1;
    controller->m_pathCap = buffer + newCap;
}

// Frees every waypoint back to the pool and empties the path (the capacity is kept).
void FreeAllWaypoints(C_MeleeTrackerAI *controller)
{
    if (controller->m_pathBegin) {
        for (S_AIWaypoint **p = controller->m_pathEnd; p != controller->m_pathBegin;) {
            --p;
            if (*p)
                ReleaseMeleeTrackerWaypoint(*p);
        }
    }
    controller->m_pathEnd = controller->m_pathBegin;
}

}  // namespace

void C_MeleeTrackerAI::ClearPath()
{
    FreeAllWaypoints(this);
    const uint32_t orders = m_flags;
    m_flags = orders & 0xdffffff3u;                 // clear the path bits (0x4, 0x8) and 0x20000000
    C_Script *script = ScriptOf();                  // (mode 5 only)
    if ((orders & 0x200000) != 0 && script != nullptr && (orders & 0x40000) != 0) {
        script->Advance();                           // a scripted MoveTo finished: carry the script on
        m_flags &= ~0x200000u;
    }
}

bool C_MeleeTrackerAI::ResetPath()
{
    FreeAllWaypoints(this);
    return true;
}

void C_MeleeTrackerAI::SmoothPath(const float *from, const float *to)
{
    float prevX = from[0];
    float prevY = from[1];
    float prevZ = from[2];
    for (uint32_t i = 0;
         m_pathBegin != nullptr && i < static_cast<uint32_t>(m_pathEnd - m_pathBegin); ++i) {
        S_AIWaypoint *node = m_pathBegin[i];
        const float nodeX = node->pos.x;
        const float nodeY = node->pos.y;
        const float nodeZ = node->pos.z;

        const uint32_t count = static_cast<uint32_t>(m_pathEnd - m_pathBegin);
        const float *next = (i == count - 1) ? to : &m_pathBegin[i + 1]->pos.x;
        // Project the point onto the line from the previous (smoothed) point to the next one.
        const float npX = next[0] - prevX;
        const float npY = next[1] - prevY;
        const float npZ = next[2] - prevZ;
        const float t = ((nodeZ - prevZ) * npZ + (nodeY - prevY) * npY + (nodeX - prevX) * npX) /
                        (npX * npX + npY * npY + npZ * npZ);
        const float projX = npX * t + prevX;
        const float projY = npY * t + prevY;
        const float projZ = npZ * t + prevZ;

        float dx = projX - nodeX;
        float dy = projY - nodeY;
        float dz = projZ - nodeZ;
        if (SafeLength(dx * dx + dy * dy + dz * dz) <= node->radius) {
            // Within the point's radius: move the walk point straight onto the line.
            node->pos.x = projX;
            node->pos.y = projY;
            node->pos.z = projZ;
        } else {
            // Too far: clamp to the circle of `radius` around the point, towards the line.
            NormalizeXYZ(dx, dy, dz);
            node->pos.x = dx * node->radius + nodeX;
            node->pos.y = dy * node->radius + nodeY;
            node->pos.z = dz * node->radius + nodeZ;
        }
        // Fix: the original stored an uninitialised w into each smoothed point; keep the waypoint's.

        prevX = node->pos.x;
        prevY = node->pos.y;
        prevZ = node->pos.z;
    }
}

bool C_MeleeTrackerAI::RunPathSearch(uint16_t startCheckpoint, uint16_t goalCheckpoint,
                            const float *from, const float *to)
{
    if (!m_checkpoints)
        return false;
    std::vector<uint16_t> checkpoints;
    if (!m_checkpoints->FindMeleeTrackerPath(startCheckpoint, goalCheckpoint, &checkpoints))
        return false;

    ClearPath();
    m_flags = (m_flags & 0xfffffff7u) | 4;          // on a path: set bit 4, clear bit 8

    for (uint16_t cp : checkpoints) {
        S_Checkpoint *point = m_checkpoints->Get(cp);
        // Fix: the original builds the node from the checkpoint's 3-float position and left w
        // uninitialised; a position's w is 1.
        const S_vector4 pos = {point->pos.x, point->pos.y, point->pos.z, 1.0f};
        PathPushBack(this,
                     AllocMeleeTrackerWaypoint(cp, pos, point->flags & 0xfb40, point->type, point->radius));
    }

    SmoothPath(from, to);

    // The last waypoint sits exactly on the goal.
    if (m_pathBegin != nullptr && m_pathEnd != m_pathBegin)
        m_pathEnd[-1]->pos = {to[0], to[1], to[2], to[3]};

    // While on the ground (a lunge range), drop the first waypoint when it is already within reach or behind.
    const uint32_t count = m_pathBegin ? static_cast<uint32_t>(m_pathEnd - m_pathBegin) : 0;
    if (MeleeTrackerMovement(m_meleeTracker)->GetLungeRange() != 0.0f && count >= 1) {   // Fix: guard the empty path
        S_vector4 segStart;
        S_vector4 segEnd;
        if (count != 1) {
            segStart = m_pathBegin[0]->pos;
            segEnd = m_pathBegin[1]->pos;
        } else {
            segStart = {from[0], from[1], from[2], from[3]};
            segEnd = m_pathBegin[0]->pos;
        }
        const float aX = segStart.x - from[0];
        const float aY = segStart.y - from[1];
        const float aZ = segStart.z - from[2];
        const float bX = segEnd.x - from[0];
        const float bY = segEnd.y - from[1];
        const float bZ = segEnd.z - from[2];
        const S_vector &worldPos = m_meleeTracker->GetFrame()->GetWorldPos();
        S_AIWaypoint *first = m_pathBegin[0];
        const float dx = first->pos.x - worldPos.x;
        const float dz = first->pos.z - worldPos.z;
        if (dx * dx + dz * dz <= first->radius * first->radius ||
            bX * aX + bY * aY + bZ * aZ < 0.0f) {
            if (first)
                ReleaseMeleeTrackerWaypoint(first);
            for (S_AIWaypoint **p = m_pathBegin; p + 1 != m_pathEnd; ++p)
                *p = *(p + 1);
            --m_pathEnd;
        }
    }
    return true;
}

bool C_MeleeTrackerAI::FindPath(const float *from, const float *to)
{
    if (!m_checkpoints)
        return false;
    float start[4] = {from[0], from[1], from[2], from[3]};
    if ((m_flags & 0xc) != 0 && (m_flags & 0x4000) != 0) {
        // Running: aim from a little ahead of the meleeTracker (0.2 of its facing).
        const S_vector4 &dir = m_meleeTracker->GetDirection4();
        start[0] += dir.x * 0.2f;
        start[1] += dir.y * 0.2f;
        start[2] += dir.z * 0.2f;
    }
    const int startCheckpoint =
        m_checkpoints->FindNearest(S_vector(start[0], start[1], start[2]), 5.0f, -1.0f);
    const int goalCheckpoint =
        m_checkpoints->FindNearest(S_vector(to[0], to[1], to[2]), 5.0f, -1.0f);
    return RunPathSearch(static_cast<uint16_t>(startCheckpoint),
                         static_cast<uint16_t>(goalCheckpoint), start, to);
}

// ===========================================================================
// Part 2b-ii-b: the decision core. The stages read the C_MeleeTracker actor's sensing / steering
// state (the seen enemies, the sightings, the measured velocity).

float C_MeleeTrackerAI::DistanceToOwnerSpot()           // 0x005cd7d0
{
    if (!m_owner)
        return 3.4028235e+38f;
    float spot[4] = {};
    OwnerPosition(spot);
    const S_vector &framePos = m_meleeTracker->GetFrame()->GetWorldPos();
    const float dx = spot[0] - framePos.x;
    const float dy = spot[1] - framePos.y;
    const float dz = spot[2] - framePos.z;
    return SafeLength(dz * dz + dy * dy + dx * dx);
}

float C_MeleeTrackerAI::DistanceToTarget()
{
    if (!m_target)
        return FLT_MAX;                             // no target
    const S_vector &meleeTrackerPos = m_meleeTracker->GetFrame()->GetWorldPos();
    const S_vector &targetPos = m_target->GetFrame()->GetWorldPos();
    const float dx = targetPos.x - meleeTrackerPos.x;
    const float dy = targetPos.y - meleeTrackerPos.y;
    const float dz = targetPos.z - meleeTrackerPos.z;
    return SafeLength(dx * dx + dy * dy + dz * dz);   // 0 on top of it, else the distance
}

void C_MeleeTrackerAI::CullSightings(int /*timeDelta*/)          // 0x005cdff0 (the frame time is pushed but unread)
{
    C_MeleeTracker *meleeTracker = m_meleeTracker;
    // Drops sightings whose timestamp runs more than 2.5 s past the performance counter's low half
    // (the original's literal unsigned comparison). Swap-removes with the last; i always advances
    // (so the swapped-in entry is left for the next pass), matching the original.
    for (uint32_t i = 0;
         meleeTracker->m_sightingsBegin != nullptr &&
         i < static_cast<uint32_t>(meleeTracker->m_sightingsEnd - meleeTracker->m_sightingsBegin); ++i) {
        const uint32_t nowLow = static_cast<uint32_t>(MicrosecondsNow());
        S_MeleeTrackerSighting *sighting = meleeTracker->m_sightingsBegin[i];
        if (nowLow + 2500000u < sighting->time) {
            if (sighting)
                ReleaseMeleeTrackerSighting(sighting);
            const uint32_t count =
                static_cast<uint32_t>(meleeTracker->m_sightingsEnd - meleeTracker->m_sightingsBegin);
            meleeTracker->m_sightingsBegin[i] = meleeTracker->m_sightingsBegin[count - 1];
            --meleeTracker->m_sightingsEnd;
        }
    }
}

// --- the sensing hooks the C_MeleeTracker actor calls on the controller -----------------------------------

// ------------------------------------------------------------------ the decision stages

void C_MeleeTrackerAI::DecideTarget(int timeDelta)      // 0x005cd1f0
{
    // Holding back: defensive, unless the target is the one the script ordered it to attack.
    const bool holdBack = (m_flags & MELEE_TRACKER_ORDER_DEFENSIVE) != 0 &&
                          !((m_flags & MELEE_TRACKER_ORDER_HAS_TARGET) != 0 && m_orderedTarget == m_target);
    if (m_0b4 < 100000)                                 // ms since the last attack decision
        m_0b4 += timeDelta;

    C_MeleeTrackerMovement *body = MeleeTrackerMovement(m_meleeTracker);
    if (!m_target) {
        // The autonomous controller goes for anything it has seen or heard, at most every 1.2 s.
        if (m_mode != 4)
            return;
        const bool seen = m_meleeTracker->m_seenBegin && m_meleeTracker->m_seenEnd != m_meleeTracker->m_seenBegin;
        const bool heard =
            m_meleeTracker->m_sightingsBegin && m_meleeTracker->m_sightingsEnd != m_meleeTracker->m_sightingsBegin;
        if ((seen || heard) && m_0b4 > 1200)
            BeginAttack();
        return;
    }

    if ((m_flags & 0x20000) != 0) {                     // attacking
        body->SetAnimSpeed(1.0f);
        C_Human *target = static_cast<C_Human *>(m_target);
        if (!target->IsAlive() || target->GetVehicle()) {
            // Dead, or out of reach in a vehicle.
            ClearAttack();
            body->CheckAttackSequence();
            return;
        }
        // Where the target will be in 0.1 s.
        const S_vector4 &own = m_meleeTracker->GetPosition4();
        const S_vector4 &theirs = target->GetPosition4();
        const S_vector &velocity = target->GetVelocity();
        const float dx = theirs.x + velocity.x * 0.1f - own.x;
        const float dy = theirs.y + velocity.y * 0.1f - own.y;
        const float dz = theirs.z + velocity.z * 0.1f - own.z;
        const float distance = SafeLength(dz * dz + dy * dy + dx * dx);
        // Close enough to bite: a held-back tracker keeps 4.2 m, else 2.1 m on the run, 1.3 m.
        float nearRange = 1.4f;                         // and within this it stops moving
        float biteRange;
        if (holdBack) {
            biteRange = 4.2f;
            nearRange = 4.2f;
        } else {
            biteRange = MeleeTrackerAnimClass(body->m_364) == 1 ? 2.1f : 1.3f;
        }

        if (distance >= biteRange) {
            // Out of reach: a new path unless it can lunge (on the ground, within 4.5 m, no path
            // of more than one waypoint left).
            const std::ptrdiff_t pathSize = m_pathBegin ? m_pathEnd - m_pathBegin : 0;
            if (body->GetLungeRange() <= distance || pathSize > 1) {
                FindTarget(false);
                m_flags = (m_flags & ~0x10000u) | 0x4;
                return;
            }
            if (pathSize != 0)
                ClearPath();
            m_flags |= 0x10004;
        } else {
            // Within reach: unless it is already biting, the bite starts (held back: no bite).
            if (MeleeTrackerAnimClass(body->m_364) != 2 && MeleeTrackerAnimClass(body->GetSeqAnimA()) != 2) {
                if (m_pathBegin && m_pathEnd != m_pathBegin)
                    ClearPath();
                m_flags |= 0x10000;
                m_actionB[6] = holdBack ? 0.0f : 1.0f;
            }
            const bool targetMoving = velocity.Magnitude() > 1e-08f;
            if (distance >= nearRange || (targetMoving && !holdBack)) {
                m_flags |= 0x4004;                      // keep moving, running
            } else {
                // Standing over a still target. A defensive tracker barks now and then.
                const uint32_t flags = m_flags;
                m_flags &= ~0xCu;
                if ((flags & MELEE_TRACKER_ORDER_DEFENSIVE) != 0 && m_0b0 < 1) {
                    m_0b0 = std::rand() % 1800 + 100;
                    m_flags |= 0x80000;
                }
            }
        }
    }
    // Face the target.
    const S_vector4 &own = m_meleeTracker->GetPosition4();
    const S_vector4 &theirs = m_target->GetPosition4();
    const float from[4] = {own.x, own.y, own.z, own.w};
    const float to[4] = {theirs.x, theirs.y, theirs.z, theirs.w};
    SetMoveToward(from, to);
}

void C_MeleeTrackerAI::DecideMove(int /*timeDelta*/)    // 0x005cd890
{
    if ((m_flags & 0x20000) != 0 || !m_owner)           // attacking, or nobody to follow
        return;
    float spot[4];
    OwnerPosition(spot);
    const S_vector4 &own = m_meleeTracker->GetPosition4();
    const float from[4] = {own.x, own.y, own.z, own.w};
    if ((m_flags & 0x10) == 0) {                        // not following: just face the spot
        SetMoveToward(from, spot);
        return;
    }

    const float dx = spot[0] - own.x;
    const float dz = spot[2] - own.z;
    const float distance = SafeLength(dz * dz + dx * dx);
    const float inv = 1.0f / distance;
    m_flags &= ~(MELEE_TRACKER_ORDER_WALK | MELEE_TRACKER_ORDER_RUN);
    // How far the spot is off the tracker's facing: the margin it may stay away grows when the
    // spot is to the side, 1.2 m when it is behind.
    const S_vector4 &dir = m_meleeTracker->GetDirection4();
    const float dot = (dir.z * (dz * inv) + dir.y * (inv * 0.0f)) + dir.x * (dx * inv);
    float margin = 0.2f;
    if (dot <= 0.0f)
        margin = 1.2f;
    else if (dot <= 0.85f)
        margin = (1.0f - dot * 1.17f) + 0.2f;

    float speed = 1.0f;
    if (margin + 5.0f < distance) {
        m_flags |= MELEE_TRACKER_ORDER_RUN;
    } else {
        m_flags |= MELEE_TRACKER_ORDER_WALK;
        if (margin + 1.0f < distance)
            speed = 1.4f;
        else if (margin + 0.4f < distance)
            speed = 1.2f;
        else if (margin + 0.05f < distance)
            speed = 1.1f;
    }
    MeleeTrackerMovement(m_meleeTracker)->SetAnimSpeed(speed);

    const std::ptrdiff_t pathSize = m_pathBegin ? m_pathEnd - m_pathBegin : 0;
    if (distance >= 3.5f || pathSize > 1) {
        // Far off (or on a path): the path following leads.
        m_flags = (m_flags & ~0x10000u) | 0x4;
        return;
    }
    if (pathSize != 0)
        ClearPath();
    // Hysteresis: start moving beyond margin + 0.2, stop within the margin.
    const bool moving = (m_flags & 0xC) != 0;
    if (moving ? distance >= margin : margin + 0.2f < distance) {
        m_flags |= 0x4;
        SetMoveToward(from, spot);
        return;
    }
    if (moving)
        m_flags &= ~0xCu;
    // At heel: face the owner's way.
    const S_vector4 &ownerPos = m_owner->GetPosition4();
    const S_vector4 &ownerDir = m_owner->GetDirection4();
    const float ownerFrom[4] = {ownerPos.x, ownerPos.y, ownerPos.z, ownerPos.w};
    const float ownerAhead[4] = {ownerDir.x + ownerPos.x, ownerDir.y + ownerPos.y, ownerDir.z + ownerPos.z, ownerPos.w};
    SetMoveToward(ownerFrom, ownerAhead);
}

void C_MeleeTrackerAI::PopFirstWaypoint()
{
    if (!m_pathBegin || m_pathEnd == m_pathBegin)
        return;
    ReleaseMeleeTrackerWaypoint(m_pathBegin[0]);
    for (S_AIWaypoint **p = m_pathBegin + 1; p != m_pathEnd; ++p)
        p[-1] = *p;
    --m_pathEnd;
}

void C_MeleeTrackerAI::FacePoint(int /*unused*/, const float *point)   // 0x005cb480
{
    if ((m_flags & 0xC) != 0 || (m_flags & MELEE_TRACKER_ORDER_ACTIVE) != 0)
        return;
    if (point[2] * point[2] + point[1] * point[1] + point[0] * point[0] < 1e-16f)
        return;
    const S_vector4 &own = m_meleeTracker->GetPosition4();
    const float from[4] = {own.x, own.y, own.z, own.w};
    SetMoveToward(from, point);
}

bool C_MeleeTrackerAI::CheckHeel(int /*unused*/)        // 0x005ccec0
{
    float spot[4] = {};
    OwnerPosition(spot);
    const S_vector &framePos = m_meleeTracker->GetFrame()->GetWorldPos();
    const float dx = spot[0] - framePos.x;
    const float dz = spot[2] - framePos.z;
    const float distance = SafeLength(dx * dx + dz * dz);
    const std::ptrdiff_t pathSize = m_pathBegin ? m_pathEnd - m_pathBegin : 0;
    if (distance >= 3.5f || pathSize > 1) {
        // Re-plan when the spot moved more than 0.5 m since the last path.
        if (m_owner) {
            const float ex = m_0a0.x - spot[0];
            const float ey = m_0a0.y - spot[1];
            const float ez = m_0a0.z - spot[2];
            if (SafeLength(ex * ex + ey * ey + ez * ez) > 0.5f) {
                m_0a0 = {spot[0], spot[1], spot[2], spot[3]};
                const S_vector4 &own = m_meleeTracker->GetPosition4();
                const float from[4] = {own.x, own.y, own.z, own.w};
                if (!FindPath(from, spot)) {
                    ClearPath();
                    return false;
                }
            }
        }
        return true;
    }
    // Close: walk straight in.
    if (pathSize != 0)
        ClearPath();
    m_flags |= 0x10004;
    return true;
}

bool C_MeleeTrackerAI::AdvanceWaypoint()                // 0x005cc420
{
    if (!m_pathBegin || m_pathEnd == m_pathBegin) {
        // The path ran out.
        ClearPath();
        if ((m_flags & 0x20000) != 0) {
            CheckReached(0);
            return true;
        }
        if ((m_flags & 0x10) != 0)
            CheckHeel(1);
        return true;
    }
    if ((m_flags & 0x20000) != 0 && !CheckReached(1))
        return true;
    if ((m_flags & 0x20000) == 0 && (m_flags & 0x10) != 0 && !CheckHeel(1))
        return true;
    // On the last leg of a scripted MoveTo: the script goes on once the tracker is within the
    // checkpoint's radius, or within 0.8 m (3 m running) of the walking point.
    const uint32_t flags = m_flags;
    if ((flags & 0x20000) == 0 && (flags & MELEE_TRACKER_ORDER_ACTIVE) != 0 && (flags & 0xC) != 0 && m_pathBegin &&
        m_pathEnd - m_pathBegin == 1) {
        const float reach = (flags & (MELEE_TRACKER_ORDER_WALK | MELEE_TRACKER_ORDER_RUN)) != MELEE_TRACKER_ORDER_RUN ? 0.8f : 3.0f;
        const S_vector &framePos = m_meleeTracker->GetFrame()->GetWorldPos();
        const S_AIWaypoint *last = m_pathBegin[0];
        const float dx = last->pos.x - framePos.x;
        const float dz = last->pos.z - framePos.z;
        const float px = last->pos2.x - framePos.x;
        const float pz = last->pos2.z - framePos.z;
        C_Script *script = ScriptOf();                  // (mode 5 only)
        if ((m_flags & MELEE_TRACKER_ORDER_MOVETO) != 0 && script &&
            (px * px + pz * pz <= last->radius * last->radius || dx * dx + dz * dz <= reach * reach)) {
            script->Advance();
            m_flags &= ~MELEE_TRACKER_ORDER_MOVETO;
        }
    }
    return false;
}

void C_MeleeTrackerAI::FollowPath(int /*timeDelta*/)    // 0x005cbf00
{
    if ((m_flags & 0xC) == 0 || (m_flags & 0x10000) != 0)   // standing, or at the target
        return;
    float distance = 0.0f;                              // to the next waypoint, flat
    if (m_pathBegin && m_pathEnd != m_pathBegin) {
        if (!RouteAroundCars()) {
            ClearPath();
            return;
        }
        const S_vector &framePos = m_meleeTracker->GetFrame()->GetWorldPos();
        const S_vector4 &next = m_pathBegin[0]->pos;
        const S_vector toNext(next.x - framePos.x, 0.0f, next.z - framePos.z);
        distance = SafeLength(toNext.z * toNext.z + toNext.x * toNext.x);
        // How far off the facing the waypoint may be to keep walking: wider when far (running: a
        // quarter of the distance counts); once turning on the spot (0x20000000) a tight angle is
        // needed to walk again.
        const bool turning = (m_flags & 0x20000000) != 0;
        float a = distance;
        if ((m_flags & (MELEE_TRACKER_ORDER_WALK | MELEE_TRACKER_ORDER_RUN)) == MELEE_TRACKER_ORDER_RUN && !turning)
            a = distance * 0.25f;
        if (a > 2.0f)
            a = 2.0f;
        const float limit = turning ? a * 0.1f + 0.2f : (a + 0.5f) * 1.0471976f;
        const S_vector4 &dir4 = m_meleeTracker->GetDirection4();
        const S_vector dir(dir4.x, dir4.y, dir4.z);
        if (toNext.AngleTo(dir) < limit)
            m_flags = (m_flags & ~0x20000008u) | 0x4;
        else
            m_flags |= 0x20000000;
    }
    if (AdvanceWaypoint())
        return;

    // Moving: drop the waypoint once within 0.3 m, or (within 1 m) once the last frame's line of
    // motion passed within 0.3 m of it.
    const S_vector4 &velocity = m_meleeTracker->m_velocity;
    if ((velocity.x * velocity.x + velocity.y * velocity.y) + velocity.z * velocity.z >= 1e-16f && distance < 1.0f) {
        bool passed = distance < 0.3f;
        if (!passed) {
            const float t = static_cast<float>(m_meleeTracker->m_lastFrameTime) * 0.001f;
            const S_vector move(t * velocity.x, t * velocity.y, t * velocity.z);
            const float lengthSq = (move.x * move.x + move.y * move.y) + move.z * move.z;
            float offset = -1e+16f;
            if (lengthSq >= 1e-08f) {
                const S_vector4 &last = m_meleeTracker->m_lastPos;
                const S_vector4 &next = m_pathBegin[0]->pos;
                const S_vector rel(last.x - next.x, 0.0f, last.z - next.z);
                const float s = ((rel.z * move.z + rel.y * move.y) + rel.x * move.x) / lengthSq;
                offset = (rel - move * s).Magnitude();
            }
            passed = offset < 0.3f && offset >= 0.0f;
        }
        if (passed) {
            PopFirstWaypoint();
            if (AdvanceWaypoint())
                return;
        }
    }

    if (m_pathBegin && m_pathEnd != m_pathBegin) {
        const S_vector &framePos = m_meleeTracker->GetFrame()->GetWorldPos();
        const float from[3] = {framePos.x, framePos.y, framePos.z};
        SetMoveToward(from, &m_pathBegin[0]->pos.x);
    }
    if ((m_flags & 0x400000) != 0 && m_084 == 0) {
        const float none[3] = {0.0f, 0.0f, 0.0f};
        FacePoint(1, none);                             // (the origin: a no-op)
        m_084 = static_cast<int>(RandFloat() * 2000.0f + 4000.0f);
    }
}

void C_MeleeTrackerController::TestSight(C_Actor *target, int *status, float *visibility)   // 0x005ce740
{
    if ((m_flags & (MELEE_TRACKER_ORDER_SUSPEND | 0x800)) != 0)
        return;
    *status = 99;                                       // tested (as the human AI's SIGHT_TESTED)
    float range = static_cast<float>(m_meleeTracker->GetStats().sightRange);
    // How easy the target is to see: another tracker by its orders, a human by stance and pace.
    uint32_t bits = 0;
    if (target && target->GetType() == ACTOR_MELEE_TRACKER) {
        const C_MeleeTrackerController *ai = static_cast<const C_MeleeTracker *>(target)->GetAI();
        bits = ai ? ai->GetOrders() : 0;                // (fix: the original assumes a controller)
    } else if (target && target->GetType() == ACTOR_HUMAN) {
        const C_Human *human = static_cast<const C_Human *>(target);
        switch (human->GetMoveSpeedClass()) {
        case 0:
            bits = 0x1000;
            break;
        case 1:
            bits = MELEE_TRACKER_ORDER_WALK;
            break;
        case 2:
            bits = MELEE_TRACKER_ORDER_RUN;
            break;
        default:
            break;
        }
        const int stance = human->GetStance();
        if (stance == 1)
            bits |= MELEE_TRACKER_ORDER_STAND;
        else if (stance == 2)
            bits |= MELEE_TRACKER_ORDER_LIE;
    }
    if ((bits & MELEE_TRACKER_ORDER_LIE) != 0)
        range *= 0.6f;
    else if ((bits & MELEE_TRACKER_ORDER_STAND) != 0)
        range *= 0.8f;
    if ((bits & 0x1000) != 0)
        range *= 1.05f;
    else if ((bits & MELEE_TRACKER_ORDER_WALK) != 0)
        range *= 1.2f;
    else if ((bits & MELEE_TRACKER_ORDER_RUN) != 0)
        range *= 1.4f;
    // Fix: no target, nothing seen (the original reads its frame regardless).
    if (!target) {
        *visibility = 0.0f;
        return;
    }

    const S_vector &targetPos = target->GetFrame()->GetWorldPos();
    // The view triangle: from the eye ("f") along its facing (flattened), `range` deep and
    // tan(40 degrees) * range to either side.
    const S_matrix &eye = m_meleeTracker->GetBodyFrame()->GetMatrix();
    S_vector look = eye(2);
    look.Normalize();
    S_vector flat(look.x, 0.0f, look.z);
    flat.Normalize();
    const float halfWidth = std::tan(0.6981317f) * range;
    const float sideX = (flat.y * 0.0f - flat.z * -1.0f) * halfWidth;
    const float sideZ = (flat.x * -1.0f - flat.y * 0.0f) * halfWidth;
    const float ax = eye.m[3][0];
    const float az = eye.m[3][2];
    const float tipX = flat.x * range + ax;
    const float tipZ = flat.z * range + az;
    const float c1x = tipX + sideX;
    const float c1z = tipZ + sideZ;
    const float c2x = tipX - sideX;
    const float c2z = tipZ - sideZ;
    const float tx = targetPos.x;
    const float tz = targetPos.z;
    // (Each edge rejects only on "less than", as the original's compares.)
    const bool inside = !((tz - az) * (c1x - ax) - (c1z - az) * (tx - ax) < 0.0f) &&
                        !((tz - c1z) * (c2x - c1x) - (c2z - c1z) * (tx - c1x) < 0.0f) &&
                        !((tz - c2z) * (ax - c2x) - (tx - c2x) * (az - c2z) < 0.0f);
    *visibility = inside ? 100.0f : 0.0f;
}

void C_MeleeTrackerAI::ScoreTarget(C_Actor *candidate, float *bestScore, C_Actor **best)   // 0x005cc7a0
{
    // (Null / non-human: skipped; the original assumes a human.)
    if (!candidate || candidate->GetType() != ACTOR_HUMAN)
        return;
    const C_Human *human = static_cast<const C_Human *>(candidate);
    if (!human->IsAlive() || human->GetVehicle())
        return;
    const S_vector4 &own = m_meleeTracker->GetPosition4();
    const S_vector4 &theirs = candidate->GetPosition4();
    const float dx = theirs.x - own.x;
    const float dy = theirs.y - own.y;
    const float dz = theirs.z - own.z;
    float distance = SafeLength(dx * dx + dy * dy + dz * dz);
    if (distance > 1e+06f)
        distance = 1e+06f;
    // Quirk kept: meant to fall with the distance, the score (1e6 - d) + d is ~1e6 for everyone
    // (the float rounding decides), so the first live candidate usually wins.
    const float score = (1e+06f - distance) + distance;
    if (*bestScore < score) {
        *bestScore = score;
        *best = candidate;
    }
}

void C_MeleeTrackerAI::FindTarget(bool force)           // 0x005cc8d0
{
    if (m_orderedTarget) {
        m_target = m_orderedTarget;
        return;
    }
    float bestScore = 0.0f;
    if (!force || !m_target) {
        if (!m_target) {
            bestScore = -10000.0f;
        } else {
            // The current target's score to beat.
            const S_vector4 &own = m_meleeTracker->GetPosition4();
            const S_vector4 &theirs = m_target->GetPosition4();
            const float dx = theirs.x - own.x;
            const float dy = theirs.y - own.y;
            const float dz = theirs.z - own.z;
            float distance = SafeLength(dz * dz + dy * dy + dx * dx);
            if (distance > 1e+06f)
                distance = 1e+06f;
            bestScore = (1e+06f - distance) + distance;
        }
    } else {
        // Start over. (The original also stores an uninitialised w.)
        m_090 = {-10000.0f, -10000.0f, -10000.0f, m_090.w};
        m_target = nullptr;
    }

    C_MeleeTracker *tracker = m_meleeTracker;
    for (std::size_t i = 0; tracker->m_seenBegin && i < static_cast<std::size_t>(tracker->m_seenEnd - tracker->m_seenBegin); ++i)
        ScoreTarget(tracker->m_seenBegin[i], &bestScore, &m_target);
    for (std::size_t i = 0;
         tracker->m_sightingsBegin && i < static_cast<std::size_t>(tracker->m_sightingsEnd - tracker->m_sightingsBegin); ++i) {
        const S_MeleeTrackerSighting *sighting = tracker->m_sightingsBegin[i];
        if (sighting->loudness > 19)
            ScoreTarget(sighting->source, &bestScore, &m_target);
    }
    if (!m_target)
        return;

    if (m_mode == 3)
        GetGame()->Print("Found target %s", m_target->GetFrame()->GetName());
    // Tell the owner (vtable +0x34, the owner).
    if (m_owner) {
        S_HumanEvent *event = AllocHumanEvent();
        event->type = HUMAN_EVENT_MELEE_TRACKER_ALERT;
        event->age = 0;
        event->timeLeft = 10000;
        event->actorId = ActorToId(m_meleeTracker);
        event->sourceId = ActorToId(m_target);
        event->pos = m_target->GetPosition4();
        event->unknown30 = 0;
        static_cast<C_Human *>(m_owner)->RaiseEvent(event);
    }
}

bool C_MeleeTrackerAI::CheckReached(int /*unused*/)     // 0x005cccc0
{
    const float distance = DistanceToTarget();          // (vtable +0x28)
    const std::ptrdiff_t pathSize = m_pathBegin ? m_pathEnd - m_pathBegin : 0;
    if (MeleeTrackerMovement(m_meleeTracker)->GetLungeRange() > distance && pathSize < 2) {
        // Within lunge reach: close in directly.
        if (pathSize != 0)
            ClearPath();
        m_flags |= 0x10004;
        return true;
    }
    if (!m_target)
        return true;
    if (pathSize != 0) {
        // The path is kept while the target stays within 1.25 m of where it was planned to.
        const S_vector4 &theirs = m_target->GetPosition4();
        const float dx = m_090.x - theirs.x;
        const float dy = m_090.y - theirs.y;
        const float dz = m_090.z - theirs.z;
        if (SafeLength(dz * dz + dy * dy + dx * dx) <= 1.25f)
            return true;
    }
    m_090 = m_target->GetPosition4();
    const S_vector4 &own = m_meleeTracker->GetPosition4();
    const float from[4] = {own.x, own.y, own.z, own.w};
    const float to[4] = {m_090.x, m_090.y, m_090.z, m_090.w};
    if (!FindPath(from, to)) {
        ClearAttack();
        ClearPath();
        return false;
    }
    if (!RouteAroundCars()) {
        ClearPath();
        return false;
    }
    return true;
}

void C_MeleeTrackerScriptedAI::OnHeardSound(S_MeleeTrackerSighting *sighting)   // 0x005cfb70
{
    if (sighting->loudness <= 10)
        return;
    SetAlarm(sighting->type != 0 ? 1 : 4);
}

void C_MeleeTrackerScriptedAI::OnSpottedEnemy(C_Actor * /*target*/)   // 0x005cfba0 (the target is unread)
{
    SetAlarm(0x20);
}

void C_MeleeTrackerScriptedAI::OnSeenHuman(int /*unused*/, C_Actor *actor)   // 0x005cfb40
{
    if ((m_flags & 0x200) != 0 && actor && actor->GetType() == ACTOR_HUMAN) {
        m_orderedTarget = actor;
        m_flags |= 0x400;
    }
}

// --- controller save / load ----------------------------------------------------------------------

bool C_MeleeTrackerController::SaveGame(C_chunk &chunk)    // 0x005d2000
{
    if (!chunk.IsOpen())
        return false;
    chunk.Begin(0x22b8);
    const int ownerId = m_owner ? m_owner->GetNetId() : -1;
    chunk.WriteChunk(0, ownerId);
    chunk.WriteChunk(1, m_alarm);
    chunk.WriteChunk(2, m_flags);
    chunk.WriteChunk(3, &m_savedPos, 0xc);              // xyz only (w is not saved)
    chunk.WriteChunk(4, &m_facing, 0xc);
    chunk.WriteChunk(5, &m_050, 0xc);
    chunk.Ascend();
    return true;
}

bool C_MeleeTrackerController::LoadGame(C_chunk &chunk)    // 0x005d21d0
{
    if (!chunk.IsOpen())
        return false;
    uint16_t id;
    if (!chunk.Descend(id))
        return false;
    if (id != 0x22b8) {
        chunk.Ascend();
        return false;
    }
    {
        uint16_t sub;
        while (chunk.RemainingSize() > 0 && chunk.Descend(sub)) {
            switch (sub) {
            case 0: {
                int ownerId;
                chunk.Read(ownerId);
                m_owner = ownerId == -1 ? nullptr : g_actorsModule.GetActor(ownerId);
                break;
            }
            case 1: chunk.Read(m_alarm); break;
            case 2: chunk.Read(m_flags); break;
            // Fix: read xyz only (the original read 0xc but stored a 4th uninitialised word).
            case 3: chunk.Read(&m_savedPos, 0xc); break;
            case 4: chunk.Read(&m_facing, 0xc); break;
            case 5: chunk.Read(&m_050, 0xc); break;
            default: break;
            }
            chunk.Ascend();
        }
    }
    chunk.Ascend();
    return true;
}

bool C_MeleeTrackerScriptedAI::SaveGame(C_chunk &chunk) // 0x005d2dd0
{
    if (!C_MeleeTrackerController::SaveGame(chunk))
        return false;
    // The bark timer.
    chunk.Begin(0x2054);
    chunk.WriteChunk(0, m_barkTimer);
    chunk.Ascend();
    return true;
}

bool C_MeleeTrackerScriptedAI::LoadGame(C_chunk &chunk) // 0x005d2fc0
{
    if (!C_MeleeTrackerController::LoadGame(chunk))
        return false;
    uint16_t id;
    if (chunk.Descend(id)) {
        if (id == 0x2054) {
            uint16_t sub;
            while (chunk.RemainingSize() > 0 && chunk.Descend(sub)) {
                if (sub == 0)
                    chunk.Read(m_barkTimer);
                chunk.Ascend();
            }
        }
        chunk.Ascend();
    }
    return true;
}

// ===========================================================================
// Part 2d: the remote mirror (mode 7) and the player's control (mode 1).

namespace {
// Clears one of a controller's 0x40-float behaviour buffers.
void ClearBuffer(float (&buffer)[0x40])
{
    for (float &value : buffer)
        value = 0.0f;
}
}  // namespace

// --- the remote mirror (mode 7) ---

C_MeleeTrackerRemote::C_MeleeTrackerRemote()            // (SetController's case 7)
    : C_MeleeTrackerController(7)
{
}

C_MeleeTrackerRemote::~C_MeleeTrackerRemote() = default;   // 0x005d5c40 (DS 0x005f06f0)

bool C_MeleeTrackerRemote::Attach(C_MeleeTracker *meleeTracker)   // 0x005cfc10 (DS 0x005e8c00)
{
    SetMeleeTracker(meleeTracker);
    return true;
}

void C_MeleeTrackerRemote::Reset()                      // 0x005d5c20
{
    C_MeleeTrackerController::Reset();
}

bool C_MeleeTrackerRemote::Detach()                     // 0x005d5c30 (DS 0x005f06d0)
{
    m_meleeTracker = nullptr;
    return true;
}

void C_MeleeTrackerRemote::FrameUpdate(int /*frameTime*/)   // 0x005cfc20
{
    if ((m_flags & 0x800) != 0)
        return;
    ++m_068;
    m_actionA = m_bufferA;
    m_actionB = m_bufferB;
    Apply();
    ClearBuffer(m_bufferB);
}

void C_MeleeTrackerRemote::OnNetState(C_NetMessage *msg)   // 0x005cfc70
{
    ClearBuffer(m_bufferA);
    ClearBuffer(m_bufferB);
    float pos[4] = {};
    float velocity[4] = {};
    float dir[4] = {};
    uint8_t unused[0x10] = {};
    uint32_t bits = 0;
    ReadControlState(msg, pos, velocity, dir, unused, &bits);
    // Quirk kept: the received velocity lands in the facing (the direction is dropped).
    m_facing = {velocity[0], velocity[1], velocity[2], velocity[3]};
    m_meleeTracker->GetFrame()->SetPos(S_vector(pos[0], pos[1], pos[2]));
}

bool C_MeleeTrackerRemote::SaveGame(C_chunk &chunk)     // 0x005d3280
{
    return C_MeleeTrackerController::SaveGame(chunk);
}

bool C_MeleeTrackerRemote::LoadGame(C_chunk &chunk)     // 0x005d3290
{
    return C_MeleeTrackerController::LoadGame(chunk);
}

// --- the player's control (mode 1) ---

C_MeleeTrackerPlayerControl::C_MeleeTrackerPlayerControl()   // (SetController's case 1)
    : C_MeleeTrackerController(1)
{
}

C_MeleeTrackerPlayerControl::~C_MeleeTrackerPlayerControl() = default;   // 0x005d5ad0

bool C_MeleeTrackerPlayerControl::Attach(C_MeleeTracker *meleeTracker)   // 0x005cf130
{
    SetMeleeTracker(meleeTracker);
    ClearBuffer(m_bufferA);
    ClearBuffer(m_bufferB);
    ClearBuffer(m_buffer270);
    ClearBuffer(m_buffer370);
    return true;
}

bool C_MeleeTrackerPlayerControl::Detach()              // 0x005d5ac0 (DS 0x005f0470)
{
    m_meleeTracker = nullptr;
    return true;
}

void C_MeleeTrackerPlayerControl::ResetState(C_MeleeTrackerController *source)   // 0x005cf180
{
    C_MeleeTrackerController::ResetState(source);
    GetGame()->GetCameraModule()->SetViewDir(m_facing);
}

void C_MeleeTrackerPlayerControl::FrameUpdate(int /*frameTime*/)   // 0x005cf320
{
    ++m_068;
    if ((m_flags & 0x800) != 0)
        return;
    if (IsLocalGame()) {
        // The buffers are the controls' values (C_MeleeTracker::Tick points them there).
        if (m_actionA && m_actionB)
            Apply();
    } else {
        m_actionA = m_bufferA;
        m_actionB = m_bufferB;
        Apply();
        ClearBuffer(m_bufferB);
    }
    if (IsServer() || !m_actionA || !m_actionB)
        return;
    // A client tells the server where the tracker is and which way it faces.
    C_BitStream *stream = CreateActorMessageEx(m_meleeTracker->GetNetId(), true);
    S_vector dir = m_meleeTracker->GetFrame()->GetMatrix()(2);
    dir.Normalize();
    const S_matrix &world = m_meleeTracker->GetFrame()->GetMatrix();
    const float dir4[4] = {dir.x, dir.y, dir.z, world.m[2][3]};
    // Fix: the original sends an uninitialised velocity; the port sends none.
    const float velocity[4] = {};
    const uint8_t unused[0x10] = {};
    WriteControlState(static_cast<C_NetMessage *>(stream), world.m[3], velocity, dir4, unused, m_flags);
    SendActorMessage(stream);
}

void C_MeleeTrackerPlayerControl::Tick(int /*timeDelta*/)   // 0x005cf570 (DS 0x005e8290)
{
#ifdef HD2_DEDICATED   // the server has only the server's side
    if ((m_flags & 0x800) == 0)
#else
    if ((m_flags & 0x800) == 0 && !IsClient())          // (a dedicated server counts the ticks)
#endif
        ++m_068;
}

void C_MeleeTrackerPlayerControl::OnNetState(C_NetMessage *msg)   // 0x005cf590
{
    ClearBuffer(m_bufferA);
    ClearBuffer(m_bufferB);
    uint32_t bits = 0;
    ReadControlState(msg, &m_netPos.x, &m_netVelocity.x, &m_netDir.x, m_netUnused, &bits);
}

void C_MeleeTrackerPlayerControl::OnNetControl(C_NetMessage *msg)   // 0x005cf5e0
{
    ClearBuffer(m_bufferA);
    ClearBuffer(m_bufferB);
    ReadControlState(msg, &m_netPos.x, &m_netVelocity.x, &m_netDir.x, m_netUnused, &m_flags);
}

bool C_MeleeTrackerPlayerControl::SaveGame(C_chunk &chunk)   // 0x005d2580
{
    return C_MeleeTrackerController::SaveGame(chunk);
}

bool C_MeleeTrackerPlayerControl::LoadGame(C_chunk &chunk)   // 0x005d2590
{
    return C_MeleeTrackerController::LoadGame(chunk);
}

void C_MeleeTrackerPlayerControl::MouseLook(int dx, int dy)   // 0x005cf1b0
{
    if ((m_flags & 0x800) != 0 || GetGame()->GetCameraModule()->GetUnknown78())
        return;
    m_facing = m_meleeTracker->GetDirection4();
    if (dx == 0 && dy == 0)
        return;
    constexpr float RADIANS_PER_COUNT = 0.0062831854f;  // 2 pi per 1000 mouse counts
    float length = std::sqrt(m_facing.x * m_facing.x + m_facing.z * m_facing.z);   // flat
    float yaw = std::atan2(-m_facing.x, m_facing.z);
    if (dx != 0)
        yaw -= static_cast<float>(dx) * RADIANS_PER_COUNT;
    if (dy != 0) {
        float pitch = std::atan2(m_facing.y, length) - static_cast<float>(dy) * RADIANS_PER_COUNT;
        if (pitch > 1.0471976f)
            pitch = 1.0471976f;
        else if (pitch < -1.0471976f)
            pitch = -1.0471976f;
        // (A second clamp to +-1.5676548 rad, which the first makes moot.)
        if (pitch > 1.5676548f)
            pitch = 1.5676548f;
        if (pitch < -1.5676548f)
            pitch = -1.5676548f;
        m_facing.y = std::sin(pitch);
        length = std::cos(pitch);
    }
    m_facing.x = -(std::sin(yaw) * length);
    m_facing.z = std::cos(yaw) * length;
    MeleeTrackerMovement(m_meleeTracker)->SetFacing(&m_facing.x);
}
