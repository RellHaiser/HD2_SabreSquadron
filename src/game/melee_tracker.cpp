// C_MeleeTracker — the meleeTracker actor: the sighting pool, sight / hearing, the controller swap, the
// per-frame update and the head look, save / load, the mission lifecycle and frame setup, the
// shared animation table, damage. (The autonomous controllers it swaps to are part 2d.)
#include "game/melee_tracker.h"

#include <windows.h>
#include <mmsystem.h>                                   // timeGetTime

#include <cmath>
#include <cstring>
#include <new>

#include "common/chunk.h"
#include "common/object_pool.h"
#include "game/actors_module.h"                         // g_actorsModule (resolve saved actor ids)
#include "game/app.h"                                   // MicrosecondsNow
#include "game/game.h"                                  // GetGame()->Print
#include "game/game_internal.h"                         // IsClient
#include "game/human.h"                                 // C_Human (role / health of a target)
#include "game/human_movement.h"                        // the target's stance volumes, RotateAboutY
#include "game/pending.h"                               // g_texts, g_sightModule, GetGEffects
#include "game/resource_cache.h"                        // g_modelCache (ReloadModel)
#include "game/sound_module.h"
#include "game/sound_table.h"                           // SOUND_CHAPTER_*
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"                               // dtaOpen (LoadAnimTable)

namespace {
// The meleeTracker's sighting pool (the composite pool's second sub-pool, 0x008ac938). Filled 16 at a time.
C_ObjectPool<S_MeleeTrackerSighting> g_meleeTrackerSightings{16};
}  // namespace

S_MeleeTrackerSighting *AllocMeleeTrackerSighting(int type, const S_vector4 &pos, C_Actor *source, uint32_t time)
{
    S_MeleeTrackerSighting *sighting = g_meleeTrackerSightings.Get();
    sighting->pos = pos;
    sighting->type = type;
    sighting->source = source;
    sighting->time = time;
    sighting->loudness = 0;                             // the caller fills it in
    return sighting;
}

void ReleaseMeleeTrackerSighting(S_MeleeTrackerSighting *sighting)
{
    if (sighting)
        g_meleeTrackerSightings.Put(sighting);
}

namespace {
// Length from a squared magnitude, clamped the way the engine's inlined helper does.
float SafeLength(float sq)
{
    if (sq < 1e-08f)
        return 0.0f;
    if (std::fabs(sq - 1.0f) >= 1e-08f)
        return std::sqrt(sq);
    return 1.0f;
}

// Appends to one of the tracker's pointer lists (begin / end / capacity), growing it as the
// original's vector insert does: to twice the size, at least by one.
template <class T> void PushBack(T **&begin, T **&end, T **&cap, T *value)
{
    if (end != cap) {
        *end++ = value;
        return;
    }
    const std::ptrdiff_t oldSize = begin ? end - begin : 0;
    const std::ptrdiff_t newCap = oldSize + (oldSize < 2 ? 1 : oldSize);
    T **buffer = static_cast<T **>(operator new(newCap * sizeof(T *)));
    for (std::ptrdiff_t i = 0; i < oldSize; ++i)
        buffer[i] = begin[i];
    buffer[oldSize] = value;
    operator delete(begin);
    begin = buffer;
    end = buffer + oldSize + 1;
    cap = buffer + newCap;
}

// Appends to the seen-enemy list (0x0045c4b0).
void PushSeenEnemy(C_MeleeTracker *meleeTracker, C_Actor *actor)
{
    PushBack(meleeTracker->m_seenBegin, meleeTracker->m_seenEnd, meleeTracker->m_seenCap, actor);
}

// The player's side (the roles an enemy meleeTracker targets): player, allied NPC, enemy-side NPC.
bool IsMeleeTrackerTargetRole(const C_Actor *actor)
{
    if (actor->GetType() != ACTOR_HUMAN)
        return false;
    const E_HumanRole role = static_cast<const C_Human *>(actor)->GetRole();
    return role == HUMAN_ROLE_PLAYER || role == HUMAN_ROLE_NPC_TEAM || role == HUMAN_ROLE_NPC_TEAM_5;
}
}  // namespace

void C_MeleeTracker::TestSight(C_Actor *target, int *status, float *visibility)   // 0x005d64c0
{
    if (m_ai->GetMode() != 1)
        m_ai->TestSight(target, status, visibility);
}

void C_MeleeTracker::OnSightEvent(int lost, int targetIndex)    // 0x005d5cb0
{
    if (!m_ai || (m_ai->GetOrders() & 0x40000000u) != 0)   // no AI, or suspended
        return;
    // Resolve the sight target (GetTarget is null past the list / for an inactive slot; the original
    // then derefs it, so guarding null is a fix). MeleeTrackers react to human targets only (kind 0).
    C_SightTarget *entry = g_sightModule.GetTarget(targetIndex);
    if (!entry || entry->GetKind() != 0)
        return;
    C_Actor *target = entry->GetActor();

    if (lost == 0) {
        // Spotted: a live, in-range human of the player's side becomes a seen enemy.
        if (!static_cast<C_Human *>(target)->IsAlive())   // +0xEC = m_stats[STAT_HEALTH] > 0
            return;
        const S_vector &meleeTrackerPos = GetFrame()->GetWorldPos();
        const S_vector &targetPos = target->GetFrame()->GetWorldPos();
        const float dx = targetPos.x - meleeTrackerPos.x;
        const float dy = targetPos.y - meleeTrackerPos.y;
        const float dz = targetPos.z - meleeTrackerPos.z;
        const float dist = SafeLength(dx * dx + dy * dy + dz * dz);
        if (dist <= static_cast<float>(m_stats.sightRange) && IsMeleeTrackerTargetRole(target)) {
            GetGame()->Print("Enemy(%s) spotted !!!", target->GetFrame()->GetName());
            PushSeenEnemy(this, target);
            m_ai->OnSpottedEnemy(target);
        }
        return;
    }

    // Lost: drop the target from the seen-enemy list.
    if (IsMeleeTrackerTargetRole(target)) {
        if (m_seenBegin) {
            for (C_Actor **p = m_seenEnd; p != m_seenBegin;) {
                --p;
                if (*p == target) {
                    for (C_Actor **q = p; q + 1 != m_seenEnd; ++q)
                        *q = *(q + 1);
                    --m_seenEnd;
                }
            }
        }
        GetGame()->Print("Enemy(%s) lost !!!", target->GetFrame()->GetName());
        m_ai->OnLostEnemy(target);                      // (empty in every variant)
    }
}

// --- controller -----------------------------------------------------------------------------------

void C_MeleeTracker::SetOptimized(bool on)              // 0x005d5870
{
    m_movement.SetOptimized(on);
    m_optimized = on;
}

C_MeleeTrackerAI *C_MeleeTracker::GetFullAI() const
{
    return m_ai && (m_ai->GetMode() == 4 || m_ai->GetMode() == 5) ? static_cast<C_MeleeTrackerAI *>(m_ai) : nullptr;
}

C_MeleeTrackerScriptedAI *C_MeleeTracker::GetScriptedAI() const
{
    return m_ai && m_ai->GetMode() == 5 ? static_cast<C_MeleeTrackerScriptedAI *>(m_ai) : nullptr;
}

void C_MeleeTracker::SetController(int mode)            // 0x005d5890
{
    C_MeleeTrackerController *controller = nullptr;
    switch (mode) {
    case 1:
        controller = new C_MeleeTrackerPlayerControl();
        break;
    case 4:
        controller = new C_MeleeTrackerAutoAI();
        break;
    case 5:
        controller = new C_MeleeTrackerScriptedAI();
        break;
    case 7:
        controller = new C_MeleeTrackerRemote();
        break;
    default:
        break;
    }
    C_MeleeTrackerController *old = m_ai;
    m_ai = controller;
    if (m_ai) {
        m_ai->Attach(this);
        if (old)
            m_ai->ResetState(old);
    }
    // Fix: an unknown mode leaves no controller (the original called through the null pointer).
    if (old) {
        old->Detach();
        delete old;
    }
}

// --- per frame / per tick ---------------------------------------------------------------------------

void C_MeleeTracker::Update(int frameTime)              // 0x005d5810
{
    if (m_optimized || m_movement.m_374 == 4)
        return;
    TrackVelocity(frameTime);
    if (m_ai)                                           // (fix: the original assumes a controller)
        m_ai->FrameUpdate(frameTime);
    HeadLook(frameTime);
    m_movement.UpdateAnimation(frameTime);
    GetFrame()->Tick(frameTime);
    GetFrame()->Update();
}

void C_MeleeTracker::Tick(int timeDelta)                // 0x005d5620
{
    if (m_optimized || m_movement.m_374 == 4)
        return;
    if (!IsClient())
        TrackVelocity(timeDelta);
    m_55c += timeDelta;
    if (GetFrame()->GetWorldPos().y < -500.0f) {
        const S_vector zero(0.0f, 0.0f, 0.0f);
        Hit(HIT_FALL, 1000.0f, zero, zero, nullptr, 0);  // fell out of the level
    }
    // (Fix: m_ai is null when the frame setup gave up; the original assumes a controller.)
    if (m_ai && m_ai->GetMode() == 1) {
        // The player drives the tracker: the controls' held / pressed values are its behaviour
        // weights.
        float *held = nullptr;
        float *pressed = nullptr;
        g_controls.GetValues(&held, false);
        g_controls.GetValues(&pressed, true);
        m_ai->m_actionA = held;
        m_ai->m_actionB = pressed;
    }
    if (m_ai)
        m_ai->Tick(timeDelta);
    m_movement.BuildSequence();
    m_movement.StepAnimation(timeDelta);
    UpdateLookBehaviour(timeDelta);
    // While biting, the target's stance volumes are enlarged around the body's move step.
    C_Actor *target = m_ai ? m_ai->GetTarget() : nullptr;
    if (target)
        static_cast<C_Human *>(target)->GetMovement()->m_locomotion.ScaleStanceVolumes(true);
    m_movement.Update(timeDelta);
    target = m_ai ? m_ai->GetTarget() : nullptr;
    if (target)
        static_cast<C_Human *>(target)->GetMovement()->m_locomotion.ScaleStanceVolumes(false);
    // The look-around: a livelier one while the body plays animations 7..12.
    const S_vector4 none = {0.0f, 0.0f, 0.0f, 0.0f};
    if (m_movement.m_364 < 7 || m_movement.m_364 > 12)
        SetLookBehaviour(none, 4, 0);
    else
        SetLookBehaviour(none, 0x18, 1);
    HearPlayers();
}

void C_MeleeTracker::TrackVelocity(int timeDelta)       // 0x005d64e0
{
    m_velocity = m_lastPos;
    const S_matrix &world = GetFrame()->GetMatrix();
    m_lastPos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    m_velocity.x -= m_lastPos.x;
    m_velocity.y -= m_lastPos.y;
    m_velocity.z -= m_lastPos.z;
    if (m_velocity.z * m_velocity.z + m_velocity.y * m_velocity.y + m_velocity.x * m_velocity.x < 1e-16f) {
        m_lastFrameTime = timeDelta;
        return;
    }
    // Quirk kept: the frame setup starts m_lastFrameTime at 0, so a move before the first
    // measurement gives an infinite velocity for one frame.
    const float scale = -1000.0f / static_cast<float>(m_lastFrameTime);
    m_velocity.x *= scale;
    m_velocity.y *= scale;
    m_velocity.z *= scale;
    m_lastFrameTime = timeDelta;
}

void C_MeleeTracker::HeadLook(int frameTime)            // 0x005d7a10
{
    // Fix: a tracker whose frame setup gave up has no head override (the original crashes on
    // the null head bone once a look starts).
    if (!m_lookActive || !m_headOverrides)
        return;
    // The override's direction is in the bone's parent space: the inverse of the parent's world
    // rotation.
    S_matrix parentRot = m_headFrame->GetParent()->GetMatrix();
    parentRot.m[3][0] = 0.0f;
    parentRot.m[3][1] = 0.0f;
    parentRot.m[3][2] = 0.0f;
    S_matrix toParent;
    toParent.Inverse(parentRot);

    S_vector forward = GetFrame()->GetMatrix()(2);     // the tracker's facing
    forward.Normalize();
    const S_vector &headPos = m_headFrame->GetWorldPos();
    if (m_lookReturn) {
        // Turning back: toward a point 10 m straight ahead. (The original also stores an
        // uninitialised w.)
        m_lookPoint.x = forward.x * 10.0f + headPos.x;
        m_lookPoint.y = forward.y * 10.0f + headPos.y;
        m_lookPoint.z = forward.z * 10.0f + headPos.z;
    }
    // (AngleBetween2D takes {x, z} pairs.)
    const float toPoint[2] = {m_lookPoint.x - headPos.x, m_lookPoint.z - headPos.z};
    float delta;                                        // the yaw still to turn
    if (m_headTurning) {
        const float forwardXZ[2] = {forward.x, forward.z};
        delta = AngleBetween2D(forwardXZ, toPoint) - m_headYaw;
    } else {
        // A new turn starts from where the head points now.
        S_vector headDir = m_headFrame->GetMatrix()(2);
        headDir.Normalize();
        m_headYaw = 0.0f;
        const float headXZ[2] = {headDir.x, headDir.z};
        delta = AngleBetween2D(headXZ, toPoint);
    }

    // The turn rate per ms: fixed (modes 0 / other) or growing with the angle left (1, 2).
    const float size = std::fabs(delta);
    float rate;
    switch (m_lookRateMode) {
    case 0:
        rate = 0.0028f;
        break;
    case 1: {
        float x = size;
        if (x < 0.1f)
            x = 0.1f;
        else if (x > 3.1415927f)
            x = 3.1415927f;
        rate = std::log(x) * 0.002f + 0.006f;
        break;
    }
    case 2:
        rate = std::log(size < 0.1f ? 0.1f : size) * 0.002f + 0.006f;
        break;
    default:
        rate = 0.002f;
        break;
    }
    const float step = (delta > 0.0f ? 1.0f : -1.0f) * (rate * m_lookSpeed) * static_cast<float>(frameTime);
    float turn = size <= std::fabs(step) ? delta : step;
    // No further than pi/2 - 0.6 rad (~56 degrees) either side.
    if (turn * m_headYaw >= 0.0f && std::fabs(turn + m_headYaw) > 0.9707963f)
        turn = 0.0f;

    if (std::fabs(turn) <= 0.003f && m_lookReturn) {
        // Back straight ahead: the bone follows the animation again.
        m_lookActive = false;
        m_headTurning = false;
        m_lookReturn = false;
        m_headOverrides->GetItem(0)->Clear();
        return;
    }
    if (std::fabs(turn) <= 0.003f)
        m_lookReached = true;
    else
        m_headYaw += turn;
    S_vector dir = forward;
    RotateAboutY(dir, m_headYaw);
    dir *= toParent;
    m_headOverrides->GetItem(0)->SetDir(dir, 0.0f);
    m_headTurning = true;
}

void C_MeleeTracker::SetLookBehaviour(const S_vector4 &target, uint32_t request, uint32_t param)   // 0x005d8070
{
    const int state = static_cast<int>(request & ~0x11u);
    const bool fast = ((request >> 4) & 1) != 0;
    if (m_lookState == state)
        return;
    m_lookState = state;
    m_lookTarget = target;
    auto startLooking = [this, fast]() {
        m_lookActive = true;
        m_lookReached = false;
        m_lookReturn = false;
        m_lookSpeed = static_cast<float>(std::rand() % 30) * 0.01f + (fast ? 1.0f : 0.6f);
    };
    bool noRepeats;
    switch (state) {
    case 2:
        m_lookTimer = -1;
        startLooking();
        return;
    case 4:                                             // look back ahead
        m_lookReturn = true;
        noRepeats = m_lookRepeats == 0;
        break;
    case 6:
        m_lookTimer = -1;
        m_lookParam = static_cast<int>(param);
        startLooking();
        return;
    case 8: {                                           // look around `param` times
        const int r = std::rand();
        m_lookRepeats = static_cast<int>(param);
        m_lookParam = r % 1500;
        if (param > 2) {
            m_lookTimer = 0;
            startLooking();
            return;
        }
        if (param == 0)
            return;
        noRepeats = false;
        break;
    }
    default:
        return;
    }
    m_lookTimer = noRepeats ? 0 : static_cast<int>(static_cast<uint32_t>(std::rand()) % static_cast<uint32_t>(m_lookRepeats * 2000))
                                      + m_lookRepeats * 1000;
}

void C_MeleeTracker::UpdateLookBehaviour(int timeDelta) // 0x005d82b0
{
    if (!m_lookActive && (m_lookState != 8 || (m_lookRepeats != 1 && m_lookRepeats != 2)))
        return;

    // The next look in 1..3 s per repeat (none without repeats).
    auto reschedule = [this]() {
        m_lookTimer = m_lookRepeats == 0
                          ? 0
                          : static_cast<int>(static_cast<uint32_t>(std::rand()) %
                                             static_cast<uint32_t>(m_lookRepeats * 2000)) +
                                m_lookRepeats * 1000;
    };

    if (m_lookState == 6) {
        if (m_lookReached) {
            m_lookParam -= timeDelta;
            if (m_lookParam <= 0) {
                m_lookReturn = true;
                reschedule();
            }
        }
    } else if (m_lookState == 8 && static_cast<uint32_t>(m_lookRepeats) <= 3) {
        bool pickNext = true;
        switch (m_lookRepeats) {
        case 0:
            m_lookTimer = -1;
            pickNext = false;
            break;
        case 1:
        case 2:
            if (m_lookTimer > timeDelta) {
                m_lookTimer -= timeDelta;
                pickNext = false;
            } else if (m_lookTimer > 0) {
                m_lookTimer = 0;
                if (m_lookSpeed > 0.6f)
                    m_lookSpeed *= 0.5f;
            }
            break;
        default:                                        // 3
            break;
        }
        if (pickNext) {
            if (m_lookTimer == 0) {
                // A point 20 m away, the body's facing turned 0.22..1.04 rad either way.
                m_lookActive = true;
                m_lookTimer = -1;
                const S_matrix &world = GetFrame()->GetMatrix();
                S_vector dir = world(2);
                dir.Normalize();
                float angle = static_cast<float>(std::rand() % 10) * 0.0909091f + 0.22f;
                if (std::rand() % 2 != 0)
                    angle = -angle;
                RotateAboutY(dir, angle);
                const S_vector &pos = GetFrame()->GetMatrix()(3);
                // Fix: the original stored an uninitialised stack word as the point's w.
                m_lookTarget = {dir.x * 20.0f + pos.x, dir.y * 20.0f + pos.y, dir.z * 20.0f + pos.z,
                                m_lookTarget.w};
            }
            if (m_lookReached) {
                m_lookParam -= timeDelta;
                if (m_lookParam <= 0) {
                    m_lookParam = static_cast<int>(RandFloat() * 1000.0f);
                    m_lookReturn = true;
                    m_lookReached = false;
                    if (m_lookRepeats == 1 || m_lookRepeats == 2)
                        reschedule();
                }
            }
        }
    }
    m_lookPoint = m_lookTarget;
}

// --- save -----------------------------------------------------------------------------------------

bool C_MeleeTracker::SaveGame(C_chunk &chunk)                   // 0x005d3310
{
    C_Actor::SaveGame(chunk);
    if (!chunk.IsOpen())
        return false;
    chunk.Begin(0x385);
    chunk.Begin(0x2a9);

    // The stats and their mission-start copy, raw.
    chunk.Begin(0);
    chunk.Write(&m_stats, 0x10);
    chunk.Write(&m_startStats, 0x10);
    chunk.Ascend();

    chunk.WriteChunk(1, &m_startPos, 0xc);
    float rot[4];
    m_startRot.ToFile(rot);
    chunk.WriteChunk(2, rot, 0x10);
    chunk.WriteChunk(3, static_cast<int>(m_headTurning));       // the bytes are stored zero-extended to 4
    chunk.WriteChunk(4, static_cast<int>(m_lookActive));
    chunk.WriteChunk(5, &m_lookPoint, 0xc);
    chunk.WriteChunk(6, m_headYaw);
    chunk.WriteChunk(8, static_cast<int>(m_lookReturn));
    chunk.WriteChunk(9, static_cast<int>(m_lookReached));
    chunk.WriteChunk(10, m_lookSpeed);
    chunk.WriteChunk(0xb, m_lookRateMode);
    chunk.WriteChunk(0xc, &m_lookTarget, 0xc);
    chunk.WriteChunk(0xd, m_lookState);
    chunk.WriteChunk(0xe, m_lookParam);
    chunk.WriteChunk(0xf, m_lookTimer);
    chunk.WriteChunk(0x10, m_lookRepeats);
    chunk.WriteChunk(0x11, static_cast<int>(m_optimized ? 1 : 0));
    chunk.WriteChunk(0x12, m_55c);

    // The sightings: each one's raw 0x20, plus a leaf giving its source's actor id (so the stale
    // pointer can be re-resolved on load). The leaf id encodes whether there is a source.
    chunk.Begin(0x13);
    for (int i = 0; m_sightingsBegin != nullptr &&
         i < static_cast<int>(m_sightingsEnd - m_sightingsBegin); ++i) {
        S_MeleeTrackerSighting *sighting = m_sightingsBegin[i];
        chunk.Begin(static_cast<uint16_t>(i));
        chunk.Write(sighting, 0x20);
        chunk.Ascend();
        int id;
        uint16_t leaf;
        if (sighting->source) {
            id = sighting->source->GetNetId();
            leaf = static_cast<uint16_t>(i + 100);
        } else {
            id = -1;
            leaf = static_cast<uint16_t>(i + 1000);
        }
        chunk.WriteChunk(leaf, id);
    }
    chunk.Ascend();

    // The seen-enemy list: an actor id per entry.
    chunk.Begin(0x14);
    for (int i = 0; m_seenBegin != nullptr &&
         i < static_cast<int>(m_seenEnd - m_seenBegin); ++i) {
        const int id = m_seenBegin[i] ? m_seenBegin[i]->GetNetId() : -1;
        chunk.WriteChunk(static_cast<uint16_t>(i), id);
    }
    chunk.Ascend();

    chunk.Ascend();                                     // close 0x2a9

    if (m_movement.SaveGame(chunk)) {
        // The AI controller: its mode (so load can recreate the right controller) then its state.
        chunk.Begin(0);
        chunk.WriteChunk(1, m_ai->GetMode());
        const bool ok = m_ai->SaveGame(chunk);
        chunk.Ascend();
        chunk.Ascend();                                 // close 0x385
        return ok;
    }
    chunk.Ascend();                                     // close 0x385
    return false;
}

// --- load -----------------------------------------------------------------------------------------

namespace {
// Appends to the sighting list (0x005d47a0).
void PushSighting(C_MeleeTracker *tracker, S_MeleeTrackerSighting *sighting)
{
    PushBack(tracker->m_sightingsBegin, tracker->m_sightingsEnd, tracker->m_sightingsCap, sighting);
}

// A flag the save stores as a 4-byte int 0 / 1 (the byte zero-extended).
bool ReadFlag(C_chunk &chunk)
{
    int value = 0;
    chunk.Read(value);
    return value != 0;
}

// An int leaf (an actor id, -1 none; or the controller mode): reads it and leaves the leaf
// (0x0053b1d0).
int ReadIntLeaf(C_chunk &chunk)
{
    int value = 0;
    chunk.Read(value);
    chunk.Ascend();
    return value;
}
}  // namespace

bool C_MeleeTracker::LoadGame(C_chunk &chunk)           // 0x005d3a50
{
    C_Actor::LoadGame(chunk);
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != 0x385 || !chunk.Descend(id)) {
        chunk.Ascend();
        return false;
    }
    if (id != 0x2a9) {
        chunk.Ascend();
        chunk.Ascend();
        return false;
    }

    uint16_t sub;
    while (chunk.RemainingSize() > 0 && chunk.Descend(sub)) {
        switch (sub) {
        case 0:
            chunk.Read(&m_stats, 0x10);
            chunk.Read(&m_startStats, 0x10);
            break;
        // Fix (1, 5, 0xc): the vectors are read as xyz; the original stored an uninitialised 4th
        // word into the w of m_startPos / m_lookPoint / m_lookTarget.
        case 1: chunk.Read(&m_startPos, 0xc); break;
        case 2: {
            float rot[4] = {};
            chunk.Read(rot, 0x10);
            m_startRot = S_quat::FromFile(rot);
            break;
        }
        case 3: m_headTurning = ReadFlag(chunk); break;
        case 4: m_lookActive = ReadFlag(chunk); break;
        case 5: chunk.Read(&m_lookPoint, 0xc); break;
        case 6: chunk.Read(m_headYaw); break;
        case 7:                                         // (never saved; the original maps it to m_lookReturn too)
        case 8: m_lookReturn = ReadFlag(chunk); break;
        case 9: m_lookReached = ReadFlag(chunk); break;
        case 0xa: chunk.Read(m_lookSpeed); break;
        case 0xb: chunk.Read(m_lookRateMode); break;
        case 0xc: chunk.Read(&m_lookTarget, 0xc); break;
        case 0xd: chunk.Read(m_lookState); break;
        case 0xe: chunk.Read(m_lookParam); break;
        case 0xf: chunk.Read(m_lookTimer); break;
        case 0x10: chunk.Read(m_lookRepeats); break;
        case 0x11: m_optimized = ReadFlag(chunk); break;
        case 0x12: chunk.Read(m_55c); break;
        case 0x13: {
            // The sightings: a fresh one per entry, overwritten whole by its raw 0x20 (so the
            // allocation arguments do not matter), then its source re-resolved from the id leaf.
            uint16_t entry;
            while (chunk.RemainingSize() > 0 && chunk.Descend(entry)) {
                S_MeleeTrackerSighting *sighting =
                    AllocMeleeTrackerSighting(0xe, S_vector4{}, nullptr, 0);
                chunk.Read(sighting, 0x20);
                chunk.Ascend();
                chunk.Descend(entry);
                const int sourceId = ReadIntLeaf(chunk);
                sighting->source = sourceId == -1 ? nullptr : g_actorsModule.GetActor(sourceId);
                PushSighting(this, sighting);
            }
            break;
        }
        case 0x14: {
            // The seen-enemy list (entries that were null are dropped).
            uint16_t entry;
            while (chunk.RemainingSize() > 0) {
                chunk.Descend(entry);
                const int actorId = ReadIntLeaf(chunk);
                if (actorId != -1)
                    PushSeenEnemy(this, g_actorsModule.GetActor(actorId));
            }
            break;
        }
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();                                     // 0x2a9

    if (!m_movement.LoadGame(chunk)) {
        chunk.Ascend();                                 // 0x385
        return false;
    }
    // The AI controller: recreate it from the saved mode, then let it load its own state.
    bool ok = false;
    if (chunk.Descend(id)) {
        if (id == 0 && chunk.Descend(id)) {
            if (id == 1) {
                SetController(ReadIntLeaf(chunk));
                ok = m_ai != nullptr && m_ai->LoadGame(chunk);
            } else {
                chunk.Ascend();
            }
        }
        chunk.Ascend();                                 // 0
    }
    chunk.Ascend();                                     // 0x385
    if (!ok)
        return false;

    SetOptimized(m_optimized);
    if (!IsAlive()) {
        // A dead tracker leaves the sight module and stops listening.
        if (m_sightObserverId >= 0) {
            g_sightModule.RemoveObserver(m_sightObserverId);
            m_sightObserverId = -1;
        }
        if (m_sightId >= 0) {
            g_sightModule.RemoveTarget(m_sightId);
            m_sightId = -1;
        }
        if (m_soundListener != -1) {
            GetSoundModule()->RemoveListener(m_soundListener);
            m_soundListener = -1;
        }
    }
    return true;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_MeleeTrackerCreator::Create()                // 0x005d4a60
{
    return new C_MeleeTracker();                        // _aligned_malloc(0x5b0, 16) in the original
}

void C_MeleeTrackerCreator::Destroy(C_Actor *actor)     // 0x005d4ae0
{
    delete actor;
}

static C_MeleeTrackerCreator g_meleeTrackerCreator;     // 0x008ac950

// Registers the creator (static initialiser 0x005d4a40).
static const bool g_meleeTrackerCreatorRegistered = [] {
    g_actorCreators[ACTOR_MELEE_TRACKER] = &g_meleeTrackerCreator;
    return true;
}();

// --- lifetime -------------------------------------------------------------------------------------

C_MeleeTracker::C_MeleeTracker() : C_Actor(ACTOR_MELEE_TRACKER)                     // 0x005d4b00 (allocated by 0x005d4a60)
{
    // The body (m_movement), its animation mixer and the 5-slot sequence queue construct with the
    // object; the "empty sequence" sentinels (-1) are the S_MeleeTrackerAnimSequence defaults, and the
    // counters / list pointers are the member defaults the original ctor also sets.
    m_name = g_texts.Get(0x1bf5);                       // the localized actor name (text 0x1bf5)
    for (S_MeleeTrackerAnim &anim : g_meleeTrackerAnims)                  // clear the shared meleeTracker-animation table (0x008AC5C0)
        anim = {};
}

C_MeleeTracker::~C_MeleeTracker()                                         // 0x005d4cf0 (0x005d4cc0 scalar-deleting)
{
    // The original frees only these three dynamic arrays; the pooled sightings, the AI controller
    // (m_ai) and the body's collision volumes / sounds are released on StopMission / Done.
    ::operator delete(m_seenBegin);                     // the seen-enemy list's buffer
    m_seenBegin = nullptr;
    m_seenEnd = nullptr;
    m_seenCap = nullptr;
    ::operator delete(m_sightingsBegin);                // the sighting list's buffer (not the nodes)
    m_sightingsBegin = nullptr;
    m_sightingsEnd = nullptr;
    m_sightingsCap = nullptr;
    ::operator delete(m_playersBegin);                  // the players list's buffer
    m_playersBegin = nullptr;
    m_playersEnd = nullptr;
    m_playersCap = nullptr;
}

void C_MeleeTracker::CopyFrom(const C_Actor *source)    // 0x005d4c80
{
    C_Actor::CopyFrom(source);
}

// --- mission lifecycle ------------------------------------------------------------------------------

namespace {
// The bone frames of the tracker's model (type 11, like the human model's "f" / "z" / "j").
constexpr uint32_t FRAME_MASK_BONE = 0x400;
// Every tracker model has the bones "b".."y" (one letter each); the setup checks they are there.
const char BONE_LETTERS[] = "bcdefghijklmnopqrstuvwxy"; // 0x0083e1a4 (via 0x0083e19c)
// The head bone, which the head look turns and the sight test measures to.
const char HEAD_BONE_NAME[] = "f";                      // 0x008390ec (also via 0x0083e1a0)
}  // namespace

void C_MeleeTracker::StartMission(bool server)          // 0x005d5290
{
    Activate(server);
    if (!server)
        return;
    // The animation table is shared by every tracker; the first one to start loads it.
    if (!g_meleeTrackerAnims[1].anim && !LoadAnimTable())
        return;
    m_startStats = m_stats;
    const S_matrix &world = GetFrame()->GetMatrix();
    m_startPos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    m_startRot.Make(GetFrame()->GetMatrix());
    SetupFrame();
    m_movement.Bind(static_cast<I3D_model *>(GetFrame()), this);
}

void C_MeleeTracker::ReleaseHeadOverrides()
{
    if (m_headOverrides) {
        while (m_headOverrides->NumItems() > 0)
            m_headOverrides->Remove(m_headOverrides->GetItem(0));
        m_headOverrides->Release();
    }
    m_headOverrides = nullptr;
}

void C_MeleeTracker::ReleaseSightings()
{
    if (m_sightingsBegin) {
        for (std::ptrdiff_t i = m_sightingsEnd - m_sightingsBegin; i-- > 0;)
            ReleaseMeleeTrackerSighting(m_sightingsBegin[i]);
    }
    m_sightingsEnd = m_sightingsBegin;
}

void C_MeleeTracker::StopMission(bool server)           // 0x005d5390
{
    Deactivate(server);
    if (!server)
        return;
    ReleaseHeadOverrides();
    // Fix: the original detaches the controller without checking there is one.
    if (m_ai) {
        m_ai->Detach();
        delete m_ai;
    }
    m_ai = nullptr;
    m_movement.FreeVolumesAndSounds();
    m_seenEnd = m_seenBegin;
    ReleaseSightings();
    if (m_sightObserverId >= 0) {
        g_sightModule.RemoveObserver(m_sightObserverId);
        m_sightObserverId = -1;
    }
    if (m_sightId >= 0) {
        g_sightModule.RemoveTarget(m_sightId);
        m_sightId = -1;
    }
    if (m_soundListener != -1) {
        GetSoundModule()->RemoveListener(m_soundListener);
        m_soundListener = -1;
    }
    // The shared animation table goes too (StartMission reloads it).
    for (S_MeleeTrackerAnim &anim : g_meleeTrackerAnims) {
        if (anim.anim)
            anim.anim->Release();
        anim = {};
    }
    ReloadModel();
    m_stats = m_startStats;
}

void C_MeleeTracker::RestartMission(bool /*server*/)    // 0x005d45f0
{
    m_stats = m_startStats;
    ReleaseHeadOverrides();
    m_seenEnd = m_seenBegin;
    ReleaseSightings();
    // Fix: as in StopMission, the controller may be missing.
    if (m_ai) {
        m_ai->Detach();
        delete m_ai;
    }
    m_ai = nullptr;
    // Back to the mission-start placement. (The sight and sound registrations are not removed:
    // the sight module, and the sound module in single player / co-op, drop them all on a
    // restart, and SetupFrame registers again.)
    I3D_frame *frame = GetFrame();
    frame->SetPos(S_vector(m_startPos.x, m_startPos.y, m_startPos.z));
    frame->SetRot(m_startRot);
    frame->SetOn(true);
    frame->Update();
    SetupFrame();
    m_movement.Reset();
}

void C_MeleeTracker::ReloadModel()                      // 0x005d5560
{
    I3D_model *model = static_cast<I3D_model *>(GetFrame());
    char fileName[0x400];                               // (the reload frees the model's own copy)
    std::strcpy(fileName, model->GetFileName());
    g_modelCache.OpenInto(model, fileName);
    model->SetPos(S_vector(m_startPos.x, m_startPos.y, m_startPos.z));
    model->SetRot(m_startRot);
    model->Update();
}

void C_MeleeTracker::SetupFrame()                       // 0x005d4db0
{
    I3D_frame *frame = GetFrame();
    // Quirk kept: a model without one of the bones gets no setup at all (no controller, no
    // sight / sound registration).
    for (const char *letter = BONE_LETTERS; *letter; ++letter) {
        const char name[2] = {*letter, '\0'};
        if (!frame->FindChildFrame(name, FRAME_MASK_BONE))
            return;
    }

    // The head look's joint override, put on the model.
    I3D_driver *driver = I3DGetDriver();
    m_headOverrides = driver->CreateJointOverrideSet();
    m_headOverrides->Add(driver->CreateJointOverride(HEAD_BONE_NAME));
    static_cast<I3D_model *>(frame)->SetJointOverrides(m_headOverrides);
    m_headFrame = frame->FindChildFrame(HEAD_BONE_NAME, FRAME_MASK_ANY);

    // The look state. (The original leaves the w of m_lookTarget and of m_velocity uninitialised;
    // the port zeroes them. Nothing reads them.)
    m_lookTarget = {};
    m_headTurning = false;
    m_lookReturn = false;
    m_lookReached = false;
    m_lookSpeed = 1.0f;
    m_lookRateMode = 1;
    m_lookActive = false;
    m_lookState = 0;
    m_lookParam = 0;
    m_lookTimer = -1;
    m_lookRepeats = 0;
    m_optimized = false;
    m_538 = 0;
    GetGEffects()->FadeInModel(frame, 0, false);        // fully visible again

    // The motion tracking starts from here.
    m_velocity = {};
    const S_matrix &world = frame->GetMatrix();
    m_lastPos = {world.m[3][0], world.m[3][1], world.m[3][2], world.m[3][3]};
    m_lastFrameTime = 0;
    m_bodyFrame = frame->FindChildFrame(HEAD_BONE_NAME, FRAME_MASK_BONE);
    const S_vector4 none = {0.0f, 0.0f, 0.0f, 0.0f};
    SetLookBehaviour(none, 0x18, 1);

    // The controller: the scripted one when the frame has a script, the autonomous one otherwise;
    // a client gets the remote one.
    int mode;
    if (IsLocalGame() || IsServer()) {
        const S_FrameData *data = frame->GetData();
        mode = data && data->script ? 5 : 4;
    } else {
        mode = 7;
    }
    SetController(mode);

    m_sightObserverId = g_sightModule.AddObserver(frame, SIGHT_ALL);
    m_sightId = g_sightModule.AddTarget(frame, SIGHT_ALL);
    m_soundListener = GetSoundModule()->AddListener(frame, OnSoundHeard);

    // The players, for the hearing scan.
    m_playersEnd = m_playersBegin;
    for (int i = 0; i < g_actorsModule.GetActorCount(); ++i) {
        C_Actor *actor = g_actorsModule.GetHuman(i);
        // Fix: a removed actor GetHuman falls back to need not be a human (the original reads its
        // role regardless).
        if (!actor || actor->GetType() != ACTOR_HUMAN)
            continue;
        C_Human *human = static_cast<C_Human *>(actor);
        if (human->GetRole() == HUMAN_ROLE_PLAYER)
            PushBack(m_playersBegin, m_playersEnd, m_playersCap, human);
    }
    m_55c = -1000;                                      // the first sound scan in a second
    g_sightModule.SetTargetState(m_sightId, SIGHT_TARGET_ALIVE);
}

void C_MeleeTracker::ResetAI()                          // 0x0041d790
{
    // Fix: the original calls the controller without checking there is one.
    if (m_ai)
        m_ai->ReleaseScript();
}

// --- the animation table ---------------------------------------------------------------------------

namespace {
// Tables\dog_anim.sav: one 0x18-byte record per animation id (the file name is the game's own).
constexpr int ANIM_RECORD_SIZE = 0x18;
constexpr int ANIM_NAME_SIZE = 0x14;                    // +0x00 the animation file, then +0x14 the blend ms
constexpr int ANIM_COUNT = 0x17;
// The track whose first position key gives an animation's root-motion speed.
const char ANIM_SPEED_TRACK[] = "anim";                 // 0x0083e1f0
}  // namespace

bool C_MeleeTracker::LoadAnimTable()                    // 0x005d6700
{
    // (The messages are the game's own.)
    const int file = dtaOpen("Tables\\dog_anim.sav", 0);
    if (file == -1) {
        ShowMessage("Chyba pri nahravani souboru dog_anim", "I/O ERROR", 0);
        dtaClose(-1);
        return false;
    }
    uint8_t records[ANIM_COUNT * ANIM_RECORD_SIZE];
    if (dtaRead(file, records, sizeof(records)) == -1) {
        ShowMessage("Chyba pri nahravani souboru dog_anim.sav", "I/O ERROR", 0);
        dtaClose(file);
        return false;
    }
    dtaClose(file);

    for (int i = 0; i < ANIM_COUNT; ++i) {
        const uint8_t *record = records + i * ANIM_RECORD_SIZE;
        S_MeleeTrackerAnim &entry = g_meleeTrackerAnims[i];
        if (record[0] == 0 || entry.anim)               // no animation for this id, or loaded already
            continue;
        std::memcpy(&entry.blendTime, record + ANIM_NAME_SIZE, sizeof(int));
        // Fix: a name filling all its bytes is not read past them. (The original appends an empty
        // extension to it.)
        char name[ANIM_NAME_SIZE + 1];
        std::memcpy(name, record, ANIM_NAME_SIZE);
        name[ANIM_NAME_SIZE] = '\0';
        // (The original inlines the cache's Get, building an error text it never shows.)
        g_animationCache.Get(name, &entry.anim);
        if (!entry.anim)
            continue;
        // The root-motion speed: the length of the speed track's first position key, negative
        // when it points backwards.
        const int track = entry.anim->FindTrack(ANIM_SPEED_TRACK);
        if (track < 0 || entry.anim->NumPosKeys(track) < 0) {
            entry.speed = 0.0f;
            continue;
        }
        int time = 0;
        S_vector pos(0.0f, 0.0f, 0.0f);
        entry.anim->GetPosKey(track, 0, &time, &pos);
        const float length = pos.Magnitude();
        entry.speed = pos.z < 0.0f ? -length : length;
    }
    return true;
}

// --- binding / mission properties ------------------------------------------------------------------

bool C_MeleeTracker::SetFrame(I3D_frame *frame)         // 0x005d4d70
{
    if (!C_Actor::SetFrame(frame))
        return false;
    if (frame->GetType() != FRAME_MODEL)
        return false;
    m_ai = nullptr;
    return true;
}

namespace {
constexpr int PROPERTIES_VERSION = 1;
}  // namespace

bool C_MeleeTracker::WriteProperties(C_chunk &chunk)    // 0x005d65d0
{
    chunk.Write(PROPERTIES_VERSION);
    chunk.Write(&m_stats, sizeof(m_stats));
    return true;
}

bool C_MeleeTracker::ReadProperties(C_chunk &chunk)     // 0x005d6630
{
    int version = 0;                                    // (not checked)
    chunk.Read(version);
    chunk.Read(&m_stats, sizeof(m_stats));
    return true;
}

// --- damage ----------------------------------------------------------------------------------------

float C_MeleeTracker::HitDamage(const S_vector &pos, float amount) const   // 0x005d73b0
{
    // The nearest bone wins (the first of equally near ones).
    const struct {
        const I3D_frame *bone;
        float factor;
    } zones[] = {
        {m_movement.m_3f0, 0.65f},                      // "c"
        {m_movement.m_3e8, 2.0f},                       // "f"
        {m_movement.m_bodyFrame, 1.5f},                 // "d"
        {m_movement.m_3f4, 0.2f},                       // "h"
        {m_movement.m_3f8, 0.2f},                       // "p"
        {m_movement.m_3fc, 0.2f},                       // "t"
        {m_movement.m_400, 0.2f},                       // "l"
    };
    float best = 0.0f;
    float factor = 0.0f;
    bool found = false;
    for (const auto &zone : zones) {
        if (!zone.bone)                                 // Fix: a model without the bone (the original crashes)
            continue;
        const float sq = (pos - zone.bone->GetWorldPos()).Square();
        if (!found || sq < best) {
            best = sq;
            factor = zone.factor;
            found = true;
        }
    }
    // Dropped: the copy protection (the 0x008ca840 / 0x008caf40 check) cuts a cracked copy's
    // damage to a tenth.
    return factor * amount;
}

void C_MeleeTracker::PlayHitReaction()
{
    if (MeleeTrackerAnimClass(m_movement.m_364) != 6 && MeleeTrackerAnimClass(m_movement.GetSeqAnimA()) != 6)
        m_movement.SetMode(6, 2);
}

void C_MeleeTracker::CheckBadlyHurt()
{
    if (m_stats.health < m_startStats.health / 8)
        m_movement.SetSubModeA(2, 10000);
}

void C_MeleeTracker::Hit(E_HitType type, float amount, const S_vector &pos, const S_vector & /*dir*/,
                         C_Actor *attacker, int /*flags*/)   // 0x005d7180
{
    if (!IsServer() || m_stats.health <= 0)
        return;
    if (amount == -1.0f)
        m_stats.health = 0;
    else
        m_stats.health = static_cast<int>(static_cast<float>(m_stats.health) - HitDamage(pos, amount));

    if (m_stats.health <= 0) {
        m_stats.health = 0;
        if (m_ai)                                       // Fix: the original assumes a controller
            m_ai->Disable();
        m_movement.SetMode(5, 0);                       // dead
        if (m_sightObserverId >= 0) {
            g_sightModule.RemoveObserver(m_sightObserverId);
            m_sightObserverId = -1;
        }
        if (m_sightId >= 0) {
            g_sightModule.RemoveTarget(m_sightId);
            m_sightId = -1;
        }
        if (m_soundListener != -1) {
            GetSoundModule()->RemoveListener(m_soundListener);
            m_soundListener = -1;
        }
        return;
    }
    PlayHitReaction();
    if (m_ai)                                           // Fix: as above
        m_ai->OnSeenHuman(static_cast<int>(type), attacker);
    CheckBadlyHurt();
}

void C_MeleeTracker::UnknownD0(int health, int, int, int)   // 0x005d7300
{
    if (IsServer())
        return;
    m_stats.health = health;
    if (health < 1) {
        m_stats.health = 0;
        if (m_ai)                                       // Fix: the original assumes a controller
            m_ai->Disable();
        m_movement.SetMode(5, 0);
        return;
    }
    PlayHitReaction();
    CheckBadlyHurt();
}

// --- hearing ---------------------------------------------------------------------------------------

void __stdcall C_MeleeTracker::OnSoundHeard(const S_HeardSound *sound)   // 0x005d6000
{
    const S_FrameData *data = sound->listener ? sound->listener->GetData() : nullptr;
    C_MeleeTracker *tracker = data ? static_cast<C_MeleeTracker *>(data->actor) : nullptr;
    // Fix: the original assumes the listener frame's actor and its controller.
    if (!tracker || !tracker->m_ai)
        return;
    C_MeleeTrackerController *ai = tracker->m_ai;
    // Not while suspended, nor while the player drives the tracker.
    if ((ai->GetOrders() & MELEE_TRACKER_ORDER_SUSPEND) != 0 || ai->GetMode() == 1)
        return;
    // Only humans of the player's side are heard.
    C_Actor *source = sound->source;
    if (!source || source->GetType() != ACTOR_HUMAN)
        return;
    const E_HumanRole role = static_cast<C_Human *>(source)->GetRole();
    if (role == HUMAN_ROLE_ENEMY || role == HUMAN_ROLE_NEUTRAL)
        return;
    const float distance = (sound->pos - tracker->GetFrame()->GetWorldPos()).Magnitude();
    if (distance > static_cast<float>(tracker->m_stats.hearRange))
        return;

    // How loud the kind of sound is to the tracker (OnHeardSound alarms over 10).
    float loudness = 0.0f;
    switch (sound->chapter) {
    case SOUND_CHAPTER_PUPPET_MOVEMENT:                 // footsteps, by the human's pace
        switch (static_cast<C_Human *>(source)->GetMoveSpeedClass()) {
        case 0:
            loudness = 10.0f;
            break;
        case 2:
            loudness = 14.0f;
            break;
        case 5:
            loudness = 16.0f;
            break;
        default:
            loudness = 12.0f;
            break;
        }
        break;
    case SOUND_CHAPTER_OBJECT_MANIPULATION:
        loudness = 3.0f;
        break;
    case SOUND_CHAPTER_WEAPON_SHOOTING:
        loudness = 1.0f;
        break;
    case SOUND_CHAPTER_WEAPON_MANIPULATION:
        loudness = 2.0f;
        break;
    case SOUND_CHAPTER_BULLET_HIT:
        // A hit close by (3 x intensity over 0.95) calls the controller's hook (empty in every
        // variant); nothing is remembered.
        if (sound->intensity * 3.0f > 0.95f)
            ai->OnBulletHit();
        return;
    case SOUND_CHAPTER_FALLING_HIT:
    case SOUND_CHAPTER_VERSION:
        return;
    case SOUND_CHAPTER_SPEECH:
    case SOUND_CHAPTER_INTERFACE:
        loudness = 15.0f;
        break;
    default:                                            // explosions, vehicles, ...: remembered, silent
        break;
    }

    // The same source making the same kind of sound again refreshes its sighting, and the
    // loudness adds up.
    if (tracker->m_sightingsBegin) {
        for (std::ptrdiff_t i = tracker->m_sightingsEnd - tracker->m_sightingsBegin; i-- > 0;) {
            S_MeleeTrackerSighting *sighting = tracker->m_sightingsBegin[i];
            if (sighting->source == source && sighting->type == sound->chapter) {
                sighting->pos = {sound->pos.x, sound->pos.y, sound->pos.z, sound->posW};
                sighting->time = static_cast<uint32_t>(MicrosecondsNow());
                sighting->loudness = static_cast<int>(static_cast<float>(sighting->loudness) + loudness);
                ai->OnHeardSound(sighting);
                return;
            }
        }
    }
    // Quirk kept: a new sighting is stamped with timeGetTime() (ms), a refreshed one with the
    // microsecond counter's low half.
    const DWORD now = timeGetTime();
    const S_vector4 pos = {sound->pos.x, sound->pos.y, sound->pos.z, sound->posW};
    S_MeleeTrackerSighting *sighting = AllocMeleeTrackerSighting(sound->chapter, pos, source, now);
    sighting->loudness = static_cast<int>(loudness);
    PushSighting(tracker, sighting);
    ai->OnHeardSound(sighting);
}

void C_MeleeTracker::HearPlayers()                      // 0x005d7760
{
    if (m_55c <= 999)
        return;
    m_55c = 0;
    const float rangeSq = static_cast<float>(m_stats.hearRange * m_stats.hearRange);
    // The list from the frame setup; filled from the player slots when it is empty.
    if (m_playersBegin == m_playersEnd) {
        for (int i = 0; i < C_ActorsModule::PLAYER_SLOTS; ++i) {
            C_Actor *actor = g_actorsModule.GetHuman(i);
            // Fix: as in SetupFrame, a non-human removed actor is skipped.
            if (!actor || actor->GetType() != ACTOR_HUMAN)
                continue;
            C_Human *human = static_cast<C_Human *>(actor);
            if (human->GetRole() == HUMAN_ROLE_PLAYER)
                PushBack(m_playersBegin, m_playersEnd, m_playersCap, human);
        }
    }
    const S_vector4 &own = GetPosition4();
    for (C_Human **p = m_playersBegin; p && p != m_playersEnd; ++p) {
        C_Human *human = *p;
        if (!human->IsAlive())                          // (+0xEC, the health stat)
            continue;
        const S_vector4 &theirs = human->GetPosition4();
        const float dx = theirs.x - own.x;
        const float dy = theirs.y - own.y;
        const float dz = theirs.z - own.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (rangeSq <= distSq)
            continue;
        S_HeardSound heard = {};
        heard.pos = S_vector(theirs.x, theirs.y, theirs.z);
        heard.posW = theirs.w;
        heard.listener = GetFrame();
        heard.intensity = 1.0f - distSq / rangeSq;
        heard.chapter = SOUND_CHAPTER_PUPPET_MOVEMENT;
        heard.source = human;
        OnSoundHeard(&heard);
    }
}
