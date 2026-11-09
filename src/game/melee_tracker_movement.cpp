// The movement/physics of a meleeTracker (C_MeleeTrackerMovement, C_MeleeTracker +0x90; original source 0x005c0ce0..0x005c9050
// and the serialization 0x005d09b0 / 0x005d11a0 / 0x005d1f50). It follows the ground on collision
// spheres, turns and moves the body from the animation mixer's root motion, picks the animations
// for the current mode, and carries the networked position. This file holds the state machine,
// animation, networking and the bite; the collision Move step and its helpers are in the same
// file further down.
#include "game/melee_tracker_movement.h"

#include <malloc.h>

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/melee_tracker.h"
#include "game/resource_cache.h"
#include "game/shots.h"
#include "game/sound_module.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"                              // the mouse (the player's tracker)
#include "ls3d/math.h"

// Shared math helpers (game/human_movement.cpp).
float AngleBetween2D(const float *a, const float *b);   // 0x00788a10
void RotateAboutY(S_vector &v, float angle);            // 0x007888f0

namespace {

// Body dummy frames looked up in the meleeTracker's model (single-letter LS3D helper names).
const char COL_FRAME_NAMES[3][12] = {"col dummy1", "col dummy2", "col dummy"};   // 0x0083e150 table
const char ANIM_FRAME_NAME[] = "anim2";                 // the model's animation driver frame

// The variant of the tracker's AI controller (C_MeleeTrackerAI +0x14): 1 = the player drives the
// tracker / it turns fast. (0 while it has none.)
int AiMode(const C_MeleeTracker *meleeTracker)
{
    const C_MeleeTrackerController *ai = meleeTracker->GetAI();
    return ai ? ai->GetMode() : 0;
}

// The AI's current target (controller vtable +0x24); null without a controller.
C_Actor *AiTarget(const C_MeleeTracker *meleeTracker)
{
    const C_MeleeTrackerController *ai = meleeTracker->GetAI();
    return ai ? ai->GetTarget() : nullptr;
}

// How far the tracker is from that target (controller vtable +0x28; FLT_MAX without one).
float AiTargetDistance(const C_MeleeTracker *meleeTracker)
{
    C_MeleeTrackerController *ai = meleeTracker->GetAI();
    return ai ? ai->DistanceToTarget() : FLT_MAX;
}

// A meleeTracker collision volume (aligned, as the original's _aligned_malloc(0x60, 0x10)).
S_CollisionVolume *NewMeleeTrackerVolume(C_MeleeTracker *meleeTracker, uint32_t flags, uint32_t type)
{
    void *memory = _aligned_malloc(sizeof(S_CollisionVolume), 16);
    S_CollisionVolume *volume = new (memory) S_CollisionVolume;
    volume->flags = flags;
    volume->frame = meleeTracker->GetFrame();
    volume->actor = meleeTracker;
    volume->type = type;
    return volume;
}

void DeleteMeleeTrackerVolume(S_CollisionVolume *&volume)
{
    if (volume) {
        g_collisionWorld.RemoveVolume(volume);
        volume->~S_CollisionVolume();
        _aligned_free(volume);
    }
    volume = nullptr;
}

// Half the x-extent of a dummy's box times 0.7 (the original's (max.x - min.x) * 0.35).
float MeleeTrackerSphereRadius(const I3D_frame *dummy)
{
    const I3D_bbox &box = static_cast<const I3D_dummy *>(dummy)->GetBBox();
    return (box.max.x - box.min.x) * 0.35f;
}

// Normalizes a direction, snapping to the nearest axis when it is too short to normalise (the
// original's inlined pattern).
void NormalizeDir(S_vector &v)
{
    const float sq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (std::fabs(sq - 1.0f) < 1e-08f)
        return;
    if (sq >= 1e-08f) {
        const float inv = 1.0f / std::sqrt(sq);
        v.x *= inv;
        v.y *= inv;
        v.z *= inv;
    } else if (v.x != 0.0f) {
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    } else if (v.z == 0.0f) {
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
    } else {
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    }
}

// The length of a vector whose square is `sq`, treating tiny values as 0 and near-unit as 1 (the
// original's inlined pattern).
float SafeLength(float sq)
{
    if (sq < 1e-08f)
        return 0.0f;
    if (std::fabs(sq - 1.0f) < 1e-08f)
        return 1.0f;
    return std::sqrt(sq);
}

// Resets the "anim2" frame's local translation and re-enables its animation blending (engine
// internals: clears frame flags 0x120, sets 0x40000000).
void ResetAnimFrame(I3D_frame *frame)
{
    uint32_t &flags = *reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(frame) + 0xE0);
    if ((flags & 0x80) == 0) {
        float *row3 = reinterpret_cast<float *>(reinterpret_cast<char *>(frame) + 0xB0);
        row3[0] = 0.0f;
        row3[1] = 0.0f;
        row3[2] = 0.0f;
        flags = (flags & 0xFFFFFEDF) | 0x40000000;
    }
}

// The meleeTracker's frame world matrix row (position / direction), ensuring the matrix is valid.
const S_matrix &MeleeTrackerMatrix(I3D_frame *frame)
{
    return frame->GetMatrix();
}

}  // namespace

// ---------------------------------------------------------------------------
// setup

void C_MeleeTrackerMovement::Init()
{
    m_airSteps = 0.0f;
    // The collision sphere source frames ("col dummy1/2/.."): kept only when they are dummies.
    for (int i = 0; i < 3; ++i) {
        I3D_frame *frame = m_meleeTracker->GetFrame()->FindChildFrame(COL_FRAME_NAMES[i], FRAME_MASK_ANY);
        m_colFrame[i] = (frame && frame->GetType() == FRAME_DUMMY) ? frame : nullptr;
    }
    UpdateSpheres();

    // Two small body volumes (radius * 1.65) and three larger ones (radius * 2.5, raised 0.22).
    for (int i = 0; i < 2; ++i) {
        m_volumeA[i] = NewMeleeTrackerVolume(m_meleeTracker, 0x42, 0x100001);
        m_volumeA[i]->center = S_vector(m_spherePos[i].x, m_spherePos[i].y, m_spherePos[i].z);
        m_volumeA[i]->centerW = m_spherePos[i].w;
        m_volumeA[i]->radius = m_sphereRadius[i] * 1.65f;
        g_collisionWorld.AddVolume(m_volumeA[i]);
    }
    for (int i = 0; i < 3; ++i) {
        m_volumeB[i] = NewMeleeTrackerVolume(m_meleeTracker, 0x42000042, 0x100002);
        m_volumeB[i]->center = S_vector(m_spherePos[i].x, m_spherePos[i].y + 0.22f, m_spherePos[i].z);
        m_volumeB[i]->centerW = m_spherePos[i].w;
        m_volumeB[i]->radius = m_sphereRadius[i] * 2.5f;
        g_collisionWorld.AddVolume(m_volumeB[i]);
    }

    m_animFrame = m_meleeTracker->GetFrame()->FindChildFrame(ANIM_FRAME_NAME, FRAME_MASK_ANY);
    m_168 = nullptr;
    m_moveDir = {};
    // The initial facing is the meleeTracker's direction.
    const S_vector4 &dir = m_meleeTracker->GetDirection4();
    m_facing = dir;
    m_currentSeq.readIndex = -1;
    m_currentSeq.endIndex = -1;
    for (int i = 0; i < 5; ++i) {
        m_queue[i].readIndex = -1;
        m_queue[i].endIndex = -1;
    }
    m_364 = 0x17;
    m_currentSeq.animA = 0x17;
    m_368 = 0x17;
    m_35c = -1;
    m_360 = -1;
    m_38c = -1;
    m_currentSeq.animB = 0;
    m_36c = 0;
    m_3c0 = 0;
    m_388 = 0;
    m_netVelocity = {};
    m_3e0 = 0;
    m_3e4 = 0;
    m_currentSeq.animC = 0;

    // The meleeTracker's current world position seeds the networked position.
    const S_matrix &m = MeleeTrackerMatrix(m_meleeTracker->GetFrame());
    const S_vector &worldPos = m(3);
    m_netPos = {worldPos.x, worldPos.y, worldPos.z, 1.0f};
    m_prevNetPos = m_netPos;
    SetNetPosition(&m_netPos.x);
    m_meleeTracker->SetPosition4(m_netPos);

    m_speed = 1.0f;
    m_speedTarget = 1.0f;
    m_240 = 0.0f;
    m_250 = 0.0f;
    m_254 = 0.0f;
    m_258 = -10000.1f;                                  // 0xC61C4066, the "no ground height" sentinel
    m_25c = MELEE_TRACKER_MOVE_NO_TIME;
    m_264 = 0;
    m_1f4 = 0xFFFFFE5C;
    m_200 = 1.0f;
    m_1f8 = -10000.1f;                                  // 0xC61C4066
    m_210 = {0.0f, -1.0f, 0.0f, 0.0f};
    m_material = -1;
    m_1fc = 0;
    m_23c = 0;
    m_234 = 0;
    m_238 = 0;
    m_220 = {0.0f, -1.0f, 0.0f, 0.0f};
    m_noCollision = false;
    m_370 = 0;
    m_374 = 0;
    m_378 = MELEE_TRACKER_MOVE_NO_TIME;

    // Body frames used for sounds, ground tests and effects.
    m_bodyFrame = m_meleeTracker->GetFrame()->FindChildFrame("d", FRAME_MASK_ANY);        // 0x008387e4
    m_groundFrame = m_meleeTracker->GetFrame()->FindChildFrame("w", FRAME_MASK_ANY);      // 0x0083e180
    m_3e8 = m_meleeTracker->GetFrame()->FindChildFrame("f", FRAME_MASK_ANY);              // 0x008390ec
    m_3f0 = m_meleeTracker->GetFrame()->FindChildFrame("c", FRAME_MASK_ANY);              // 0x008390f0
    m_3f4 = m_meleeTracker->GetFrame()->FindChildFrame("h", FRAME_MASK_ANY);              // 0x008390e0
    m_3f8 = m_meleeTracker->GetFrame()->FindChildFrame("p", FRAME_MASK_ANY);              // 0x008390cc
    m_3fc = m_meleeTracker->GetFrame()->FindChildFrame("t", FRAME_MASK_ANY);              // 0x008390d4
    m_400 = m_meleeTracker->GetFrame()->FindChildFrame("l", FRAME_MASK_ANY);              // 0x008390e8

    m_bitePending = false;
    m_soundHandle[0] = -1;
    m_soundHandle[1] = -1;
    m_soundHandle[2] = -1;
    m_turned = 0;
    m_moved = 0;

    SetMode(3, 0);
    SetSubModeA(0, 0);
    SetSubModeB(0, 0);
    m_168 = nullptr;
    if (m_animFrame)
        ResetAnimFrame(m_animFrame);
}

void C_MeleeTrackerMovement::UpdateSpheres()
{
    for (int i = 0; i < 3; ++i) {
        I3D_frame *frame = m_colFrame[i];
        if (!frame)
            continue;
        const S_vector &pos = MeleeTrackerMatrix(frame)(3);
        m_spherePos[i] = {pos.x, pos.y - 0.1f, pos.z, m_spherePos[i].w};
        m_sphereRadius[i] = MeleeTrackerSphereRadius(frame);
    }
}

void C_MeleeTrackerMovement::Bind(I3D_model *model, C_MeleeTracker *owner)
{
    m_meleeTracker = owner;
    m_mixer.Init(model);
    Init();
}

void C_MeleeTrackerMovement::Reset()
{
    for (int i = 0; i < 2; ++i)
        DeleteMeleeTrackerVolume(m_volumeA[i]);
    for (int i = 0; i < 3; ++i)
        DeleteMeleeTrackerVolume(m_volumeB[i]);
    m_mixer.Reset();
    Init();
}

void C_MeleeTrackerMovement::FreeVolumesAndSounds()
{
    for (int i = 0; i < 2; ++i)
        DeleteMeleeTrackerVolume(m_volumeA[i]);
    for (int i = 0; i < 3; ++i)
        DeleteMeleeTrackerVolume(m_volumeB[i]);
    for (int i = 0; i < 3; ++i) {
        if (m_soundHandle[i] != -1)
            GetSoundModule()->Stop(&m_soundHandle[i]);
    }
    m_soundHandle[0] = -1;
    m_soundHandle[1] = -1;
    m_soundHandle[2] = -1;
}

void C_MeleeTrackerMovement::SetOptimizedController(bool optimized)
{
    m_noCollision = optimized;
}

void C_MeleeTrackerMovement::SetOptimized(bool optimized)
{
    if (!optimized) {
        // Back into the world: re-add the collision volumes (alive meleeTrackers) and show the model.
        UpdateSpheres();
        if (m_meleeTracker->IsAlive()) {
            for (int i = 0; i < 2; ++i) {
                m_volumeA[i]->center = S_vector(m_spherePos[i].x, m_spherePos[i].y, m_spherePos[i].z);
                m_volumeA[i]->centerW = m_spherePos[i].w;
                m_volumeA[i]->radius = m_sphereRadius[i] * 1.65f;
                g_collisionWorld.AddVolume(m_volumeA[i]);
            }
        }
        for (int i = 0; i < 3; ++i) {
            m_volumeB[i]->center = S_vector(m_spherePos[i].x, m_spherePos[i].y + 0.22f, m_spherePos[i].z);
            m_volumeB[i]->centerW = m_spherePos[i].w;
            m_volumeB[i]->radius = m_sphereRadius[i] * 2.5f;
            g_collisionWorld.AddVolume(m_volumeB[i]);
        }
        m_meleeTracker->GetFrame()->SetOn(true);
    } else {
        for (int i = 0; i < 2; ++i)
            g_collisionWorld.RemoveVolume(m_volumeA[i]);
        for (int i = 0; i < 3; ++i)
            g_collisionWorld.RemoveVolume(m_volumeB[i]);
        m_meleeTracker->GetFrame()->SetOn(false);
    }
}

// ---------------------------------------------------------------------------
// the movement mode

void C_MeleeTrackerMovement::SetMode(int mode, int priority)
{
    if (m_374 != 0)
        return;
    const int effective = mode != 5 ? priority : 0x7FFFFFFF;
    if (m_modePriority > effective)
        return;
    m_modePriority = effective;
    if (mode == 5) {
        // Dead: clear the animation queue, settle on the ground and drop the body volumes.
        while (m_35c != -1)
            QueuePopFront();
        CheckGround();
        for (int i = 0; i < 2; ++i)
            DeleteMeleeTrackerVolume(m_volumeA[i]);
        m_mode = 5;
        return;
    }
    m_mode = mode;
}

void C_MeleeTrackerMovement::SetSubModeA(int value, int priority)
{
    if (m_subModeAPriority <= priority) {
        m_subModeAPriority = priority;
        m_subModeA = value;
    }
}

void C_MeleeTrackerMovement::SetSubModeB(int value, int priority)
{
    if (m_subModeBPriority <= priority) {
        m_subModeBPriority = priority;
        m_subModeB = value;
    }
}

int C_MeleeTrackerMovement::GetSeqAnimA()
{
    return m_35c != -1 ? m_queue[m_360].animA : m_currentSeq.animA;
}

int C_MeleeTrackerMovement::GetSeqAnimB()
{
    return m_35c != -1 ? m_queue[m_360].animB : m_currentSeq.animB;
}

int C_MeleeTrackerMovement::GetSeqAnimC()
{
    return m_35c != -1 ? m_queue[m_360].animC : m_currentSeq.animC;
}

// ---------------------------------------------------------------------------
// the animation-sequence ring

S_MeleeTrackerAnimSequence *C_MeleeTrackerMovement::QueuePushSlot()
{
    if (m_35c == -1) {
        m_35c = 0;
        m_360 = 0;
        m_queue[0].readIndex = -1;
        m_queue[0].endIndex = -1;
        return &m_queue[0];
    }
    const int read = m_35c;
    if (++m_360 > 4)
        m_360 = 0;
    if (read == m_360) {
        m_35c = read + 1;
        if (read + 1 > 4)
            m_35c = 0;
    }
    m_queue[m_360].readIndex = -1;
    m_queue[m_360].endIndex = -1;
    return &m_queue[m_360];
}

void C_MeleeTrackerMovement::QueuePopFront()
{
    const int read = m_35c;
    if (read == -1)
        return;
    if (read == m_360) {
        m_35c = -1;
        return;
    }
    m_35c = read + 1;
    if (read + 1 > 4)
        m_35c = 0;
}

void C_MeleeTrackerMovement::QueuePopBack()
{
    if (m_35c == -1)
        return;
    if (m_35c == m_360)
        m_35c = -1;
    else
        --m_360;
    // Quirk kept: the original sets m_35c (not m_360) to 4 when the write index goes negative.
    if (m_360 < 0)
        m_35c = 4;
}

// ---------------------------------------------------------------------------
// networking

void C_MeleeTrackerMovement::SetNetPosition(const float *pos)
{
    m_3c0 = 1;
    m_netPos = {pos[0], pos[1], pos[2], pos[3]};

    // The velocity since the last net position.
    S_vector velocity(m_netPos.x - m_prevNetPos.x, m_netPos.y - m_prevNetPos.y, m_netPos.z - m_prevNetPos.z);
    m_netVelocity = {velocity.x, velocity.y, velocity.z, m_netVelocity.w};
    const float moved = SafeLength(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);

    m_prevNetPos = {m_netPos.x, m_netPos.y, m_netPos.z, m_netPos.w};

    // How far the frame is from the last net position; a big jump snaps the reference instead of
    // interpolating.
    const S_vector fromFrame(m_netPos.x - m_prevFramePos.x, m_netPos.y - m_prevFramePos.y,
                             m_netPos.z - m_prevFramePos.z);
    const float frameDist =
        SafeLength(fromFrame.x * fromFrame.x + fromFrame.y * fromFrame.y + fromFrame.z * fromFrame.z);
    if (std::fabs(moved - frameDist) <= 2.0f) {
        const S_vector &framePos = MeleeTrackerMatrix(m_meleeTracker->GetFrame())(3);
        m_prevFramePos = {framePos.x, framePos.y, framePos.z, 1.0f};
    } else {
        m_prevFramePos = {m_netPos.x, m_netPos.y, m_netPos.z, m_netPos.w};
    }

    // The interpolation direction.
    S_vector dir(m_netPos.x - m_prevFramePos.x, m_netPos.y - m_prevFramePos.y, m_netPos.z - m_prevFramePos.z);
    NormalizeDir(dir);

    if (m_3e0 != 0) {
        const S_vector toPos(m_netPos.x - m_prevFramePos.x, m_netPos.y - m_prevFramePos.y,
                             m_netPos.z - m_prevFramePos.z);
        const double distance = SafeLength(toPos.x * toPos.x + toPos.y * toPos.y + toPos.z * toPos.z);
        m_38c = static_cast<int>((distance / 1e-05) * 100.0 + 1.0);
        if (m_38c > 0xA6) {
            m_38c = 0xA6;
            m_netVelocity = {m_netPos.x - m_prevFramePos.x, m_netPos.y - m_prevFramePos.y,
                             m_netPos.z - m_prevFramePos.z, m_netVelocity.w};
            return;
        }
        const float speed = static_cast<float>(distance);
        m_netVelocity = {dir.x * speed, dir.y * speed, dir.z * speed, m_netVelocity.w};
        return;
    }
    m_38c = -1;
    m_netVelocity = {dir.x * static_cast<float>(moved), dir.y * static_cast<float>(moved),
                     dir.z * static_cast<float>(moved), m_netVelocity.w};
}

void C_MeleeTrackerMovement::ClearNetBit(const float *pos)
{
    m_3e0 &= ~2u;
    SetNetPosition(pos);
}

void C_MeleeTrackerMovement::SetWorldDir(const float *dir)
{
    I3D_frame *frame = m_meleeTracker->GetFrame();
    S_vector forward = MeleeTrackerMatrix(frame)(2);
    NormalizeDir(forward);
    if (std::fabs(forward.x - dir[0]) >= 0.001f || std::fabs(forward.y - dir[1]) >= 0.001f ||
        std::fabs(forward.z - dir[2]) >= 0.001f) {
        frame->SetDir(S_vector(dir[0], dir[1], dir[2]), 0.0f);
        frame->Update();
    }
}

// ---------------------------------------------------------------------------
// facing / turning

void C_MeleeTrackerMovement::SetFacing(const float *dir)
{
    if ((dir[0] != m_facing.x || dir[1] != m_facing.y || dir[2] != m_facing.z) && GetSeqAnimC() != 1)
        m_facing = {dir[0], dir[1], dir[2], dir[3]};
}

// Eases the meleeTracker's (actor) direction toward its facing by a time-scaled step; snaps when within a
// step. Returns true when aligned/snapped.
bool C_MeleeTrackerMovement::TurnToFacing(int timeDelta, int /*flag*/)
{
    const S_vector4 current = m_meleeTracker->GetDirection4();
    S_vector forward(current.x, current.y, current.z);
    // (AngleBetween2D takes {x, z} pairs.)
    const float facingXZ[2] = {m_facing.x, m_facing.z};
    const float forwardXZ[2] = {forward.x, forward.z};
    const float angle = AngleBetween2D(facingXZ, forwardXZ);
    if (std::fabs(angle) < 0.0001f)
        return true;

    float rate;
    if (AiMode(m_meleeTracker) == 1)
        rate = 3.0f;
    else
        rate = GetSeqAnimB() == 1 ? 0.01f : 0.0005f;
    float step = (std::fabs(angle) + 1.0f) * rate;
    if (angle > 0.0f)
        step = -step;
    step *= static_cast<float>(timeDelta);

    m_turned = 1;
    if (std::fabs(angle) <= std::fabs(step)) {
        // Close enough: set the direction straight to the facing (keeping y).
        m_meleeTracker->SetDirection4({m_facing.x, current.y, m_facing.z, m_facing.w});
        return true;
    }
    RotateAboutY(forward, step);
    m_meleeTracker->SetDirection4({forward.x, current.y, forward.z, current.w});
    return false;
}

// Clamps a move direction `dir` to the meleeTracker's (actor) direction rotated by a capped step toward
// `dir` (limits how fast the move direction may swing from the facing).
void C_MeleeTrackerMovement::TurnDir(float *dir)
{
    const S_vector4 current = m_meleeTracker->GetDirection4();
    const float a[2] = {dir[0], dir[2]};                // {x, z} pairs
    const float b[2] = {current.x, current.z};
    const float angle = AngleBetween2D(a, b);
    if (std::fabs(angle) < 0.0001f)
        return;

    float rate;
    if (AiMode(m_meleeTracker) == 1)
        rate = 3.0f;
    else
        rate = GetSeqAnimB() == 1 ? 0.01f : 0.0025f;
    float step = (std::fabs(angle) + 1.0f) * rate;
    if (angle > 0.0f)
        step = -step;
    const float applied = step * 100.0f;
    if (std::fabs(applied) < std::fabs(angle)) {
        S_vector v(current.x, current.y, current.z);
        RotateAboutY(v, applied);
        dir[0] = v.x;
        dir[1] = current.y;
        dir[2] = v.z;
        dir[3] = current.w;
    }
}

// ---------------------------------------------------------------------------
// the per-frame animation update

void C_MeleeTrackerMovement::UpdateAnimation(int timeDelta)
{
    const float seconds = static_cast<float>(timeDelta) * 0.001f;
    // Ease the animation-speed weight toward its target.
    if (m_speedTarget != m_speed) {
        if (std::fabs(m_speedTarget - m_speed) <= seconds)
            m_speed = m_speedTarget;
        else
            m_speed += m_speedTarget <= m_speed ? -seconds : seconds;
    }
    m_mixer.SetTrackRate(0, m_speed);

#ifndef HD2_DEDICATED   // not in the server (DS 0x005d69f0); MouseLook would also reset m_facing
    if (AiMode(m_meleeTracker) == 1) {
        // The player drives the tracker: the mouse turns it.
        IGraph *graph = GetIGraph();
        const int dy = graph->GetMouseDeltaY();
        const int dx = graph->GetMouseDeltaX();
        static_cast<C_MeleeTrackerPlayerControl *>(m_meleeTracker->GetAI())->MouseLook(dx, dy);
    }
#endif

    m_mixer.UpdateStages(timeDelta, timeDelta, false, nullptr);
    TurnToFacing(timeDelta, 1);

    if (AiMode(m_meleeTracker) == 1) {
        const S_vector4 dir = m_meleeTracker->GetDirection4();
        GetGame()->GetCameraModule()->SetViewDir(dir);
    }

    const S_vector4 facing = m_meleeTracker->GetDirection4();
    SetWorldDir(&facing.x);

    const bool stageFinished = m_mixer.TakeStageFinished();
    if (stageFinished && m_364 != 5 && m_364 != 6) {
        if (m_animFrame)
            ResetAnimFrame(m_animFrame);
        m_168 = nullptr;
        m_moveDir = {};
    }

    // The frame time clock for the net interpolation (ApplyNetInterp plays it back).
    if (m_3c0 == 0) {
        if (m_3e0 != 0)
            m_3e4 = 1;
        m_388 += timeDelta;
    } else if (m_3e4 == 0) {
        m_388 = timeDelta;
    } else {
        m_3e4 = 0;
        m_388 = timeDelta;
    }
    HandleAnimEvents();
    ApplyNetInterp(m_388);
}

// Plays the note events of the two mixer tracks: footstep sounds and the lie-down slide.
void C_MeleeTrackerMovement::HandleAnimEvents()
{
    for (int track = 0; track < 2; ++track) {
        int count = 0;
        const int *events = m_mixer.TakeSoundEvents(track, &count);
        for (int i = 0; i < count; ++i) {
            const int event = events[i];
            if (event == 0) {
                // A footstep: the ground material's footstep sound at the meleeTracker's position.
                S_SoundRequest request;
                const int footstep =
                    m_material >= 0 ? reinterpret_cast<const int *>(&g_materials[m_material])[9] : -1;
                if (m_material >= 0 && footstep >= 0) {
                    const S_vector &pos = MeleeTrackerMatrix(m_meleeTracker->GetFrame())(3);
                    request.pos = pos;
                    request.posW = 1.0f;  // (matrix row 3 w)
                    request.flags = 0;
                    request.attachTo = nullptr;
                    request.source = nullptr;
                    request.chapter = 0;
                    request.group = footstep;
                    GetSoundModule()->Play(request);
                }
            } else if (event == 1) {
                // The body starts/stops lying: slide the body down or forward.
                S_vector4 dir = m_meleeTracker->GetDirection4();
                m_bitePending = true;
                if (m_364 == 5) {
                    if (dir.y != 0.0f) {
                        dir.y = 0.0f;
                        NormalizeDir(reinterpret_cast<S_vector &>(dir));
                    }
                    float offset[4];
                    if (GetSeqAnimB() == 1) {
                        offset[0] = dir.x * 0.8f;
                        offset[1] = 14.0f;
                        offset[2] = dir.z * 0.8f;
                    } else {
                        offset[0] = 0.0f;
                        offset[1] = 12.0f;
                        offset[2] = 0.0f;
                    }
                    offset[3] = dir.w;
                    m_mixer.SetSlide(offset, 1000);
                }
            } else if (event == 2) {
                m_bitePending = false;
            }
        }
    }
}

// Applies the net interpolation move back onto the meleeTracker's frame over its window.
void C_MeleeTrackerMovement::ApplyNetInterp(int /*frameTime*/)
{
    m_moved = 0;
    m_3c0 = 0;
    S_vector pos;
    bool moved = false;
    if (m_3e0 == 0 && m_3e4 == 0) {
        const float t = static_cast<float>(m_388) * 0.01f;
        pos = S_vector(t * m_netVelocity.x + m_prevFramePos.x, t * m_netVelocity.y + m_prevFramePos.y,
                       t * m_netVelocity.z + m_prevFramePos.z);
        moved = true;
    } else if (m_38c > 0) {
        if (m_38c <= m_388)
            m_388 = m_38c;
        const float t = static_cast<float>(m_388) / static_cast<float>(m_38c);
        pos = S_vector(t * m_netVelocity.x + m_prevFramePos.x, t * m_netVelocity.y + m_prevFramePos.y,
                       t * m_netVelocity.z + m_prevFramePos.z);
        moved = true;
        if (m_388 == m_38c)
            m_38c = -1;
    }
    if (moved) {
        I3D_frame *frame = m_meleeTracker->GetFrame();
        frame->SetPos(pos);
        const S_vector sectorPos(pos.x, pos.y + 0.5f, pos.z);
        GetGame()->GetScene()->UpdateFrameSector(frame, sectorPos);
        frame->Update();
    }
}

// ---------------------------------------------------------------------------
// the ground settle (on death / stance change)

void C_MeleeTrackerMovement::CheckGround()
{
    const S_vector4 dir = m_meleeTracker->GetDirection4();
    if (m_groundFrame)
        MeleeTrackerMatrix(m_groundFrame);                        // (ensure its matrix is valid)
    S_vector origin;
    if (m_bodyFrame) {
        const S_vector &body = MeleeTrackerMatrix(m_bodyFrame)(3);
        origin = S_vector(body.x, body.y + 0.5f, body.z);
    }
    S_vector down(0.0f, -1.0f, 0.0f);
    S_vector hit, normal;
    S_CollisionVolume *volume = CollisionSphereTest(origin, down, 0.2f, &hit, &normal, 0xFFFFFFFF, 0x10000);
    if (!volume) {
        float move[4] = {dir.x, dir.y, dir.z, dir.w};
        m_mixer.SetAddMove(move, 800);
        m_374 = 1;
        m_378 = 5000;
    } else {
        m_374 = 2;
        m_378 = 5000;
    }
}

// ---------------------------------------------------------------------------
// the bite

void C_MeleeTrackerMovement::Bite()
{
    C_Actor *target = AiTarget(m_meleeTracker);
    if (!target || (m_364 != 5 && m_364 != 6))
        return;
    if (AiTargetDistance(m_meleeTracker) >= 2.1f || m_airSteps != 0.0f)   // within 2.1 m of the target
        return;
    if (IsServer()) {
        // Dropped: the copy protection makes a cracked copy bite for ten times the damage
        // (0x008caf40 / 0x008cac4a check); the port always uses the normal bite damage.
        const int damage = m_meleeTracker->GetStats().biteDamage;
        const S_vector4 dir = m_meleeTracker->GetDirection4();
        const S_vector zero(0.0f, 0.0f, 0.0f);
        const S_vector biteDir(dir.x, dir.y, dir.z);
        target->Hit(HIT_MELEE_TRACKER_BITE, static_cast<float>(damage), zero, biteDir, nullptr, 0);
        g_teamManager.GetPlayerTeam()->AddSeenMeleeTracker(m_meleeTracker);
    }
    GetGame()->Print("Kousnul jsem humana");
}

// ---------------------------------------------------------------------------
// serialization

bool C_MeleeTrackerMovement::SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(0x2F59))
        return false;
    chunk.WriteChunk(0, m_168);
    chunk.WriteChunk(1, &m_moveDir, 3 * sizeof(float));
    chunk.WriteChunk(2, &m_facing, 3 * sizeof(float));
    chunk.WriteChunk(3, m_material);
    chunk.WriteChunk(4, m_1f4);
    chunk.WriteChunk(5, m_1f8);
    chunk.WriteChunk(6, m_1fc);
    chunk.WriteChunk(7, m_200);
    chunk.WriteChunk(8, &m_210, 3 * sizeof(float));
    chunk.WriteChunk(9, &m_220, 3 * sizeof(float));
    chunk.WriteChunk(10, static_cast<int>(m_noCollision));
    chunk.WriteChunk(0x0B, m_234);
    chunk.WriteChunk(0x0C, m_238);
    chunk.WriteChunk(0x0D, m_23c);
    chunk.WriteChunk(0x0E, m_240);
    chunk.WriteChunk(0x0F, m_speed);
    chunk.WriteChunk(0x10, m_speedTarget);
    chunk.WriteChunk(0x11, m_250);
    chunk.WriteChunk(0x12, m_254);
    chunk.WriteChunk(0x13, m_258);
    chunk.WriteChunk(0x14, m_25c);
    chunk.WriteChunk(0x15, static_cast<int>(m_260));
    chunk.WriteChunk(0x16, m_264);
    chunk.WriteChunk(0x17, static_cast<int>(m_bitePending));
    chunk.WriteChunk(0x18, m_mode);
    chunk.WriteChunk(0x19, m_subModeA);
    chunk.WriteChunk(0x1A, m_subModeB);
    chunk.WriteChunk(0x1B, m_modePriority);
    chunk.WriteChunk(0x1C, m_subModeAPriority);
    chunk.WriteChunk(0x1D, m_subModeBPriority);
    chunk.WriteChunk(0x1E, &m_currentSeq, sizeof(m_currentSeq));
    chunk.WriteChunk(0x1F, &m_queue, sizeof(m_queue));
    chunk.WriteChunk(0x20, m_35c);
    chunk.WriteChunk(0x21, m_360);
    chunk.WriteChunk(0x22, m_364);
    chunk.WriteChunk(0x23, m_368);
    chunk.WriteChunk(0x24, static_cast<int>(m_36c));
    chunk.WriteChunk(0x25, m_374);
    chunk.WriteChunk(0x26, m_378);
    chunk.WriteChunk(0x27, m_soundHandle[0]);
    chunk.WriteChunk(0x28, m_soundHandle[1]);
    chunk.WriteChunk(0x29, &m_prevNetPos, 3 * sizeof(float));
    chunk.WriteChunk(0x2A, &m_prevFramePos, 3 * sizeof(float));
    chunk.WriteChunk(0x2B, &m_netPos, 3 * sizeof(float));
    chunk.WriteChunk(0x2C, static_cast<int>(m_3c0));
    chunk.WriteChunk(0x2D, &m_netVelocity, 3 * sizeof(float));
    chunk.WriteChunk(0x2E, m_3e0);
    chunk.WriteChunk(0x2F, static_cast<int>(m_3e4));
    chunk.WriteChunk(0x30, m_airSteps);
    chunk.Ascend();
    return m_mixer.SaveGame(chunk);
}

bool C_MeleeTrackerMovement::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != 0x2F59)
        return false;
    for (int i = 0; i < 2; ++i)
        if (m_volumeA[i])
            g_collisionWorld.RemoveVolume(m_volumeA[i]);
    for (int i = 0; i < 3; ++i)
        if (m_volumeB[i])
            g_collisionWorld.RemoveVolume(m_volumeB[i]);

    auto readVec3 = [&chunk](S_vector4 &v) {
        S_vector value(0.0f, 0.0f, 0.0f);
        chunk.Read(value);
        v.x = value.x;
        v.y = value.y;
        v.z = value.z;
    };
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case 0: chunk.Read(m_168); break;
        case 1: readVec3(m_moveDir); break;
        case 2: readVec3(m_facing); break;
        case 3: chunk.Read(m_material); break;
        case 4: chunk.Read(m_1f4); break;
        case 5: chunk.Read(m_1f8); break;
        case 6: chunk.Read(m_1fc); break;
        case 7: chunk.Read(m_200); break;
        case 8: readVec3(m_210); break;
        case 9: readVec3(m_220); break;
        case 10: { int v = 0; chunk.Read(v); m_noCollision = v != 0; break; }
        case 0x0B: chunk.Read(m_234); break;
        case 0x0C: chunk.Read(m_238); break;
        case 0x0D: chunk.Read(m_23c); break;
        case 0x0E: chunk.Read(m_240); break;
        case 0x0F: chunk.Read(m_speed); break;
        case 0x10: chunk.Read(m_speedTarget); break;
        case 0x11: chunk.Read(m_250); break;
        case 0x12: chunk.Read(m_254); break;
        case 0x13: chunk.Read(m_258); break;
        case 0x14: chunk.Read(m_25c); break;
        case 0x15: { int v = 0; chunk.Read(v); m_260 = v != 0; break; }
        case 0x16: chunk.Read(m_264); break;
        case 0x17: { int v = 0; chunk.Read(v); m_bitePending = v != 0; break; }
        case 0x18: chunk.Read(m_mode); break;
        case 0x19: chunk.Read(m_subModeA); break;
        case 0x1A: chunk.Read(m_subModeB); break;
        case 0x1B: chunk.Read(m_modePriority); break;
        case 0x1C: chunk.Read(m_subModeAPriority); break;
        case 0x1D: chunk.Read(m_subModeBPriority); break;
        case 0x1E: chunk.Read(&m_currentSeq, sizeof(m_currentSeq)); break;
        // Quirk kept: the original reads only 4 bytes of the 0xB4-byte queue here.
        case 0x1F: chunk.Read(&m_queue, 4); break;
        case 0x20: chunk.Read(m_35c); break;
        case 0x21: chunk.Read(m_360); break;
        case 0x22: chunk.Read(m_364); break;
        case 0x23: chunk.Read(m_368); break;
        case 0x24: { int v = 0; chunk.Read(v); m_36c = static_cast<uint8_t>(v); break; }
        case 0x25: chunk.Read(m_374); break;
        case 0x26: chunk.Read(m_378); break;
        case 0x27: chunk.Read(m_soundHandle[0]); break;
        case 0x28: chunk.Read(m_soundHandle[1]); break;
        case 0x29: readVec3(m_prevNetPos); break;
        case 0x2A: readVec3(m_prevFramePos); break;
        case 0x2B: readVec3(m_netPos); break;
        case 0x2C: { int v = 0; chunk.Read(v); m_3c0 = static_cast<uint8_t>(v); break; }
        case 0x2D: readVec3(m_netVelocity); break;
        case 0x2E: chunk.Read(m_3e0); break;
        case 0x2F: { int v = 0; chunk.Read(v); m_3e4 = static_cast<uint8_t>(v); break; }
        case 0x30: chunk.Read(m_airSteps); break;
        default: break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    m_meleeTracker->GetFrame()->SetOn(true);
    UpdateSpheres();
    if (m_meleeTracker->IsAlive()) {
        for (int i = 0; i < 2; ++i) {
            if (!m_volumeA[i])
                continue;
            m_volumeA[i]->center = S_vector(m_spherePos[i].x, m_spherePos[i].y, m_spherePos[i].z);
            m_volumeA[i]->centerW = m_spherePos[i].w;
            m_volumeA[i]->radius = m_sphereRadius[i] * 1.65f;
            g_collisionWorld.AddVolume(m_volumeA[i]);
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!m_volumeB[i])
            continue;
        m_volumeB[i]->center = S_vector(m_spherePos[i].x, m_spherePos[i].y + 0.22f, m_spherePos[i].z);
        m_volumeB[i]->centerW = m_spherePos[i].w;
        m_volumeB[i]->radius = m_sphereRadius[i] * 2.5f;
        g_collisionWorld.AddVolume(m_volumeB[i]);
    }
    return m_mixer.LoadGame(chunk);
}

// ---------------------------------------------------------------------------
// the per-frame update

void C_MeleeTrackerMovement::Update(int timeDelta)
{
    float facing[4] = {m_facing.x, m_facing.y, m_facing.z, m_facing.w};
    TurnDir(facing);
    m_mixer.UpdateMove(timeDelta, facing);
    Move(timeDelta);
    if (m_bitePending) {
        m_bitePending = false;
        Bite();
    }
    if (m_370 > 0)
        m_370 -= timeDelta;

    // Keep the networked position following the frame.
    const S_vector4 framePos = m_meleeTracker->GetPosition4();
    ClearNetBit(&framePos.x);

    // Deadly / damaging ground materials (server only).
    if (IsServer() && m_material > 0x45) {
        float amount = 0.0f;
        E_HitType type = HIT_ENVIRONMENT;
        bool hit = true;
        switch (m_material) {
        case 0x46: amount = static_cast<float>(timeDelta) * 0.01f; break;
        case 0x47: amount = static_cast<float>(timeDelta) * 0.03f; break;
        case 0x5A: amount = static_cast<float>(timeDelta) * 0.02f; break;
        case 0x48: amount = static_cast<float>(timeDelta) * 0.5f; break;
        case 0x5D: amount = -1.0f; type = HIT_9; break;
        default: hit = false; break;
        }
        if (hit) {
            const S_vector zero(0.0f, 0.0f, 0.0f);
            m_meleeTracker->Hit(type, amount, zero, zero, nullptr, 0);
        }
    }

    // Move the collision volumes to the new sphere positions and update the frame's sector.
    I3D_frame *frame = m_meleeTracker->GetFrame();
    const S_vector &framePosNow = MeleeTrackerMatrix(frame)(3);
    const S_vector4 simPos = m_meleeTracker->GetPosition4();
    if ((framePosNow.x - simPos.x) * (framePosNow.x - simPos.x) +
            (framePosNow.y - simPos.y) * (framePosNow.y - simPos.y) +
            (framePosNow.z - simPos.z) * (framePosNow.z - simPos.z) >
        0.0001f) {
        frame->SetPos(S_vector(simPos.x, simPos.y, simPos.z));
        const S_vector sectorPos(simPos.x, simPos.y + 0.5f, simPos.z);
        I3D_sector *oldSector = static_cast<I3D_sector *>(frame->GetParent());
        I3D_sector *newSector = GetGame()->GetScene()->GetSector(sectorPos);
        if (oldSector && oldSector->GetType() == FRAME_SECTOR && oldSector != newSector)
            frame->LinkTo(newSector, 0);
        frame->Update();
    }

    UpdateSpheres();
    if (m_meleeTracker->IsAlive()) {
        for (int i = 0; i < 2; ++i) {
            if (!m_volumeA[i])
                continue;
            m_volumeA[i]->center = S_vector(m_spherePos[i].x, m_spherePos[i].y, m_spherePos[i].z);
            m_volumeA[i]->centerW = m_spherePos[i].w;
            m_volumeA[i]->radius = m_sphereRadius[i] * 1.65f;
            g_collisionWorld.MoveVolume(m_volumeA[i]);
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!m_volumeB[i])
            continue;
        m_volumeB[i]->center = S_vector(m_spherePos[i].x, m_spherePos[i].y + 0.22f, m_spherePos[i].z);
        m_volumeB[i]->centerW = m_spherePos[i].w;
        m_volumeB[i]->radius = m_sphereRadius[i] * 2.5f;
        g_collisionWorld.MoveVolume(m_volumeB[i]);
    }
}

// ===========================================================================
// The collision Move step and its helpers (0x005c3420 and friends). Faithful
// transcriptions; the x87 gravity maths and the stance/slope alignment are kept
// exactly as the original (quirks included).

// Appends a debug move-trajectory line to a global buffer (rendered only in a debug build).
void DrawDebugLine(const float *from, const float *to, uint32_t color, uint32_t color2);   // 0x0071fad0

int MeleeTrackerAnimClass(int group)
{
    if (group > 6 && group < 0xD)
        return 3;
    if (group >= 0 && group < 5)
        return (group != 0 && group != 2) ? 1 : 0;
    if (group == 5 || group == 6)
        return 2;
    if (group == 0x12)
        return 4;
    if (group > 0xC && group < 0x12)
        return 5;
    if (group == 0x14 || group == 0x13)
        return 6;
    return 7;
}

namespace {

constexpr uint32_t MELEE_TRACKER_MOVE_EXCLUDE_FLAGS = 0x00B10000;   // &DAT_00b10000

// The material id carried by a collision volume (its flags' top byte), -1 if none/out of range.
// (0x005c6360)
int VolumeMaterial(const S_CollisionVolume *volume)
{
    if (!volume)
        return -1;
    const unsigned id = GetVolumeMaterial(*volume);
    return id < g_materialCount ? static_cast<int>(id) : -1;
}

// True when every contact of a sphere step is "ground" (its normal points up, or it is a floor
// volume); true when there are no contacts. (0x005c6380)
bool IsGrounded(const S_MoveContacts &contacts)
{
    bool grounded = true;
    for (int i = 0; i < contacts.count; ++i) {
        const bool floor = contacts.normals[i].y > 0.707f ||
                           ((contacts.volumes[i]->flags & 0x40) != 0 &&
                            (contacts.volumes[i]->type & 0x100) != 0);
        grounded = grounded && floor;
    }
    return grounded;
}

// Advances a gravity phase in 85 ms steps, adding 3.27 to *velocity each step. (0x005c6250)
void MeleeTrackerSlideStep(int timeDelta, float *velocity, float *phase)
{
    int t = timeDelta;
    if (t <= 0)
        return;
    while (*phase + static_cast<float>(t) > 85.0f) {
        *velocity += 9.81f * 0.33333334f;
        t = static_cast<int>(static_cast<float>(t) - (85.0f - *phase));
        *phase = 0.0f;
        if (t < 1)
            return;
    }
    *phase = static_cast<float>(t) + *phase;
}

// The fall distance over `timeDelta` ms from gravity `velocity` at `phase` (same 85 ms stepping; a
// faithful transcription of the original's x87, dimensional quirks included). (0x005c62c0)
float MeleeTrackerGravityDistance(int timeDelta, float velocity, float phase)
{
    float dist = 0.0f;
    if (timeDelta > 0) {
        float vel = velocity;
        int t = timeDelta;
        while (phase + static_cast<float>(t) > 85.0f) {
            dist = 9.81f * 0.33333334f + vel;
            t = static_cast<int>(static_cast<float>(t) - (85.0f - phase));
            phase = 0.0f;
            vel = dist;
            if (t < 1)
                return dist;
        }
        dist = static_cast<float>(t) * 0.001f * vel + dist;
    }
    return dist;
}

}  // namespace

// ---------------------------------------------------------------------------
// the gravity landing / contact response (0x005c2260)

void C_MeleeTrackerMovement::ContactResponse(S_CollisionVolume *hit, char grounded, int timeDelta, float *move)
{
    const bool fallen = (hit == nullptr || grounded == 0) && m_250 != 0.0f;
    if (fallen) {
        MeleeTrackerSlideStep(timeDelta, &m_250, &m_254);
        if (m_25c == MELEE_TRACKER_MOVE_NO_TIME) {
            if (m_volumeA[0])
                g_collisionWorld.SetIgnoredVolumes(m_volumeA, 2, 0);
            const S_vector sphere0(m_spherePos[0].x, m_spherePos[0].y, m_spherePos[0].z);
            S_vector down(0.0f, -100.0f, 0.0f);
            S_vector hitPos, hitNormal;
            S_CollisionVolume *ground =
                CollisionRayTest(sphere0, down, &hitPos, &hitNormal, 0x80002, 0x10000);
            float drop = 50.0f;
            bool doSphere = true;
            if (ground) {
                const S_vector4 sim = m_meleeTracker->GetPosition4();
                drop = std::fabs(sim.y - hitPos.y) + 0.5f;
                if (drop <= 1.0f)
                    doSphere = false;
            }
            if (doSphere && m_374 == 0) {
                S_vector sdown(0.0f, -drop, 0.0f);
                S_vector sPos, sNormal;
                S_CollisionVolume *sground = CollisionSphereTest(sphere0, sdown,
                                                                 m_sphereRadius[0] - 0.1f, &sPos,
                                                                 &sNormal, 0x80002, 0x10000);
                if (!sground) {
                    m_258 = -10000.1f;
                } else {
                    const S_vector4 sim = m_meleeTracker->GetPosition4();
                    const float groundY = (sPos.y - m_sphereRadius[0]) + 0.1f;
                    m_258 = groundY;
                    m_25c = 0;
                    const float diff = std::fabs(sim.y - groundY);
                    if (sim.y < groundY || diff < 1.3f) {
                        if (std::fabs(drop - diff) < 1.0f)
                            m_25c = 0;
                    } else if (diff > 4.5f) {
                        m_25c = 1;
                    }
                }
            }
            g_collisionWorld.SetIgnoredVolume(nullptr, 0);
        }
        if (move[0] * move[0] + move[1] * move[1] + move[2] * move[2] >= 1e-16f && hit == nullptr) {
            S_vector4 sim = m_meleeTracker->GetPosition4();
            sim.x += move[0] * 0.9f;
            sim.y += move[1] * 0.9f;
            sim.z += move[2] * 0.9f;
            m_meleeTracker->SetPosition4(sim);
            m_264 += timeDelta;
            m_moved = 1;
        }
    } else if (hit != nullptr) {
        if (grounded != 0)
            m_258 = -10000.1f;
    } else {
        MeleeTrackerSlideStep(timeDelta, &m_250, &m_254);
        if (move[0] * move[0] + move[1] * move[1] + move[2] * move[2] >= 1e-16f) {
            S_vector4 sim = m_meleeTracker->GetPosition4();
            sim.x += move[0] * 0.9f;
            sim.y += move[1] * 0.9f;
            sim.z += move[2] * 0.9f;
            m_meleeTracker->SetPosition4(sim);
            m_moved = 1;
        }
    }
    if (m_25c != MELEE_TRACKER_MOVE_NO_TIME && m_258 != -10000.1f && hit == nullptr && m_374 == 0 && m_25c == 0)
        m_258 = -10000.1f;
}

// ---------------------------------------------------------------------------
// tilt the meleeTracker to follow the ground slope under it (0x005c26d0)

void C_MeleeTrackerMovement::AlignToSlope(int timeDelta)
{
    const float rate = static_cast<float>(timeDelta) * 0.004f;
    const S_vector4 sim = m_meleeTracker->GetPosition4();
    const S_vector4 dir = m_meleeTracker->GetDirection4();

    if (m_volumeA[0])
        g_collisionWorld.SetIgnoredVolumes(m_volumeA, 2, 0);

    // Ray-cast down at the back and the front of the body.
    S_vector down(0.0f, -2.0f, 0.0f);
    S_vector backPos(sim.x - dir.x * 0.1f, (sim.y + 0.4f) - dir.y * 0.1f, sim.z - dir.z * 0.1f);
    S_vector frontPos(dir.x * 0.6f + backPos.x, backPos.y + dir.y * 0.6f, backPos.z + dir.z * 0.6f);
    S_vector backHitN, backHit, frontHitN, frontHit;
    S_CollisionVolume *back = CollisionRayTest(backPos, down, &backHitN, &backHit, 0xFFFFFFFF, 0x10000);
    if (!back) {
        backPos.y += 0.4f;
        back = CollisionRayTest(backPos, down, &backHitN, &backHit, 0xFFFFFFFF, 0x10000);
    }
    S_CollisionVolume *front = CollisionRayTest(frontPos, down, &frontHitN, &frontHit, 0xFFFFFFFF, 0x10000);
    if (!front) {
        frontPos.y += 0.4f;
        front = CollisionRayTest(frontPos, down, &frontHitN, &frontHit, 0xFFFFFFFF, 0x10000);
    }

    S_vector slope;
    bool haveGround = true;
    if (!back) {
        if (!front) {
            haveGround = false;
        } else {
            const S_vector a(frontHit.x, frontHit.y + 1.0f, frontHit.z);
            slope = S_vector(a.x * 0.5f, a.y * 0.5f, a.z * 0.5f);
            back = front;
        }
    } else if (!front) {
        const S_vector a(backHit.x, backHit.y + 1.0f, backHit.z);
        slope = S_vector(a.x * 0.5f, a.y * 0.5f, a.z * 0.5f);
    } else {
        slope = S_vector((frontHit.x + backHit.x) * 0.5f, (frontHit.y + backHit.y) * 0.5f,
                         (frontHit.z + backHit.z) * 0.5f);
    }

    g_collisionWorld.SetIgnoredVolume(nullptr, 0);

    if (haveGround && back) {
        const S_vector flatDir(dir.x, 0.0f, dir.z);
        const float slopeAngle = flatDir.AngleTo(slope);
        float facing2[3] = {dir.x, dir.z, 0.0f};
        float slope2a[3] = {slope.x, slope.z, 0.0f};
        float ab = std::fabs(AngleBetween2D(facing2, slope2a));
        m_200 = ab;
        if (ab > 1.5707964f)
            m_200 = 3.1415927f - ab;
        m_200 = 1.0f - m_200 * 0.63661975f;

        float z1[3] = {0.0f, 1.0f, 0.0f};
        float slopeXZ[3] = {slope.x, slope.z, 0.0f};
        const float a1 = AngleBetween2D(z1, slopeXZ);
        slope *= RotateYMatrix(-a1);
        S_vector4 s4{slope.x, slope.y, slope.z, 0.0f};
        RotateAboutX(s4, slopeAngle < 1.5707964f ? -1.5707964f : 1.5707964f);
        slope = S_vector(s4.x, s4.y, s4.z);
        float facingXZ[3] = {dir.x, dir.z, 0.0f};
        float slopeXZ2[3] = {slope.x, slope.z, 0.0f};
        const float a2 = AngleBetween2D(facingXZ, slopeXZ2);
        slope *= RotateYMatrix(-a2);
        NormalizeDir(slope);

        const float w = 1.0f - m_200;
        slope.x = w * dir.x + slope.x * m_200;
        slope.y = w * 0.0f + slope.y * m_200;
        slope.z = w * dir.z + slope.z * m_200;
        NormalizeDir(slope);

        if (rate < std::fabs(slope.y - dir.y)) {
            const float step = slope.y <= dir.y ? -rate : rate;
            slope.y = step + dir.y;
            NormalizeDir(slope);
        }
        m_meleeTracker->SetDirection4({slope.x, slope.y, slope.z, dir.w});
        m_turned = 1;
        return;
    }

    // No ground: flatten the direction's pitch toward level.
    m_200 = 1.0f;
    float y = dir.y;
    if (std::fabs(y) <= rate)
        y = 0.0f;
    else
        y += (y >= 0.0f) ? -rate : rate;
    S_vector flat(dir.x, y, dir.z);
    NormalizeDir(flat);
    m_meleeTracker->SetDirection4({flat.x, flat.y, flat.z, dir.w});
    m_turned = 1;
}

// ---------------------------------------------------------------------------
// slope slide: blend the step's end position toward the ground and set the body rate (0x005c43d0)

void C_MeleeTrackerMovement::SlopeSlide(S_MoveContacts *contacts, const float *normal, S_CollisionVolume *hit,
                               bool moving, const float *groundPos, const float *move, int timeDelta)
{
    float factor = 0.0f;
    if (hit != nullptr && moving && m_364 <= 4 &&
        std::fabs(contacts->pos.y - contacts->spheres[0].y) <= 1.0f && m_250 <= 19.62f) {
        const float ny = normal[1];
        // The move direction (xz) and the surface normal (xz), normalised, dotted.
        S_vector moveDir(contacts->move.x, 0.0f, contacts->move.z);
        NormalizeDir(moveDir);
        S_vector nrm(normal[0], 0.0f, normal[2]);
        NormalizeDir(nrm);
        float dotXZ = moveDir.x * nrm.x + moveDir.y * nrm.y + moveDir.z * nrm.z;
        if (ny <= 0.5f && dotXZ <= 0.6f && dotXZ >= -0.6f) {
            factor = m_1fc;
        } else if (ny <= 0.95f) {
            float vertical = (1000.0f / static_cast<float>(timeDelta)) * (contacts->pos.y - groundPos[1]);
            float horizSq = move[0] * move[0] + move[2] * move[2];
            float horiz = SafeLength(horizSq);
            horiz = (horiz * 1000.0f) / static_cast<float>(timeDelta);
            if (horiz > 0.0f && horiz < 1.0f)
                vertical = vertical / horiz;
            if (ny <= 0.0f || vertical <= 0.2f || m_1f8 != -10000.1f) {
                factor = 0.0f;
            } else {
                if (dotXZ < 0.0f)
                    dotXZ = -dotXZ;
                factor = (1.0f - ny) * dotXZ * 2.1f;
                if (factor > 1.0f || ny < 0.55f)
                    factor = 1.0f;
            }
        }
    }

    // Ease m_1fc toward the target factor.
    if (0.17f < std::fabs((m_1fc - factor) * 100.0f) / static_cast<float>(timeDelta)) {
        const float d = static_cast<float>(timeDelta) * 0.17f * 0.01f;
        factor = m_1fc <= factor ? m_1fc + d : m_1fc - d;
    }
    m_1fc = factor;
    float rate = 1.0f - factor * 0.5f;
    if (rate < 0.65f)
        rate = 0.65f;
    if (factor != 0.0f) {
        if (factor < 0.9f) {
            contacts->pos.x -= (contacts->pos.x - groundPos[0]) * factor;
            contacts->pos.y -= (contacts->pos.y - groundPos[1]) * factor;
            contacts->pos.z -= (contacts->pos.z - groundPos[2]) * factor;
            contacts->pos.w = move[3];
        } else {
            contacts->pos = {groundPos[0], groundPos[1], groundPos[2], groundPos[3]};
        }
    }
    m_mixer.SetTrackRate(0, rate != 1.0f ? rate : 1.0f);
}

// ---------------------------------------------------------------------------
// the move / collision step (0x005c3420)

void C_MeleeTrackerMovement::Move(int timeDelta)
{
    S_MoveContacts contacts{};
    bool rayGround = false;                              // bVar7: ground found by the standing ray test
    S_vector4 groundRef = m_spherePos[0];               // local_2a0: the reference sphere (SlopeSlide groundPos)
    S_vector delta(0.0f, 0.0f, 0.0f);                   // local_2b0: sim position - the first sphere
    const float savedIterations = g_moveIterations;     // local_274
    const int animClass = MeleeTrackerAnimClass(m_364);          // local_2d4

    if (m_378 != MELEE_TRACKER_MOVE_NO_TIME) {
        if (m_378 < 1) {
            m_374 = 4;
            return;
        }
        m_378 -= timeDelta;
    }
    if (m_240 > 0)
        --m_240;

    const S_vector mv = m_mixer.GetMove();
    float moveX = mv.x;
    float moveY = mv.y;
    float moveZ = mv.z;
    const float moveW = 0.0f;                            // local_2c4 (the move's w, carried through)

    S_CollisionVolume *hit = nullptr;                   // local_314
    if (moveX * moveX + moveY * moveY + moveZ * moveZ < 1e-16f) {
        // The root motion is still: settle with a standing ray test under both spheres.
        m_1f8 = -10000.1f;
        if (static_cast<int>(m_1f4) >= 0x8C) {
            if (static_cast<int>(m_1f4) > 10000)
                m_1f4 = 10000;
            if (m_volumeA[0])
                g_collisionWorld.SetIgnoredVolumes(m_volumeA, 2, 0);
            S_vector hitPos, hitNormal;
            S_vector d0(0.0f, -m_sphereRadius[0] - 0.1f, 0.0f);
            S_CollisionVolume *front =
                CollisionRayTest(S_vector(m_spherePos[0].x, m_spherePos[0].y, m_spherePos[0].z), d0,
                                 &hitPos, &hitNormal, 0x80002, 0x10000);
            S_vector d1(0.0f, -m_sphereRadius[1] - 0.1f, 0.0f);
            S_CollisionVolume *back =
                CollisionRayTest(S_vector(m_spherePos[1].x, m_spherePos[1].y, m_spherePos[1].z), d1,
                                 &hitPos, &hitNormal, 0x80002, 0x10000);
            rayGround = front != nullptr || back != nullptr;
            g_collisionWorld.SetIgnoredVolume(nullptr, 0);
            if (rayGround) {
                m_264 = 0;
                m_material = VolumeMaterial(front);
                if (!front && back)
                    m_material = VolumeMaterial(back);
            }
            m_250 = 0.0f;
            m_254 = 0.0f;
        }
        m_1f4 += timeDelta;
        if (m_250 == 0.0f) {
            if (rayGround)
                goto applyPosition;
            m_250 = 9.81f;
        } else if (rayGround) {
            goto applyPosition;
        }
    } else if (m_250 == 0.0f) {
        m_250 = 9.81f;
    }

    moveY -= MeleeTrackerGravityDistance(timeDelta, m_250, m_254);

    if (!m_noCollision) {
        const S_vector4 dir = m_meleeTracker->GetDirection4();
        const S_vector4 sim = m_meleeTracker->GetPosition4();
        int sc = 0;
        for (int i = 0; i < 2; ++i) {
            bool include = std::fabs(dir.y) >= 0.05f || animClass == 2 || m_240 != 0;
            if (!include) {
                const bool skip = (animClass == 0) ? (i == 0) : (i == 1);
                include = !skip;
            }
            if (!include)
                continue;
            contacts.radii[sc] = m_sphereRadius[i] * 1.5f;
            const S_vector4 centre{m_spherePos[i].x, m_sphereRadius[i] * 0.25f + m_spherePos[i].y,
                                   m_spherePos[i].z, m_spherePos[i].w};
            contacts.spheres[sc] = centre;
            if (i == 0) {
                delta = S_vector(sim.x - centre.x, sim.y - centre.y, sim.z - centre.z);
                groundRef = centre;
            } else if (animClass == 0 && sc == 0) {
                delta = S_vector(sim.x - contacts.spheres[0].x, sim.y - contacts.spheres[0].y,
                                 sim.z - contacts.spheres[0].z);
                groundRef = centre;
            }
            ++sc;
        }
        contacts.sphereCount = sc;
        contacts.move = {moveX, moveY, moveZ, moveW};
        contacts.excludeTypes = 0x88002;
        contacts.excludeFlags = MELEE_TRACKER_MOVE_EXCLUDE_FLAGS;
        if (m_volumeA[0])
            g_collisionWorld.SetIgnoredVolumes(m_volumeA, 2, 0);
        if (m_364 == 5) {
            g_moveIterations = 4.0f;
            if (contacts.move.y < -0.1f) {
                contacts.move.y = -0.1f;
                moveY = -0.1f;
            }
        } else {
            const float biteDist = AiTargetDistance(m_meleeTracker);
            g_moveIterations = biteDist <= 1.65f ? 4.0f : 3.0f;
        }
        hit = g_collisionWorld.MoveSpheresStepping(contacts);
        g_collisionWorld.SetIgnoredVolume(nullptr, 0);
        if (hit) {
            // Step climbing: if the body ended higher while trying to go down, retry a vertical move.
            const float dx = (delta.x + contacts.pos.x) - sim.x;
            const float dy = (contacts.pos.y + delta.y) - sim.y;
            const float dz = (delta.z + contacts.pos.z) - sim.z;
            if (dy > 0.2f && contacts.move.y < 0.0f && dy - contacts.move.y > 0.3f) {
                contacts.move = {0.0f, -dy, 0.0f, moveW};
                for (unsigned s = 0; s < contacts.sphereCount; ++s) {
                    contacts.spheres[s].x += dx;
                    contacts.spheres[s].y += dy;
                    contacts.spheres[s].z += dz;
                    contacts.radii[s] *= 0.9f;
                }
                contacts.normals[0] = {0.0f, 0.0f, 0.0f, contacts.normals[0].w};
                contacts.pos = {0.0f, 0.0f, 0.0f, contacts.pos.w};
                if (m_volumeA[0])
                    g_collisionWorld.SetIgnoredVolumes(m_volumeA, 2, 0);
                hit = g_collisionWorld.MoveSpheresStepping(contacts);
                g_collisionWorld.SetIgnoredVolume(nullptr, 0);
                if (!hit) {
                    contacts.pos = {contacts.move.x + dx, dy + contacts.move.y, contacts.move.z + dz, moveW};
                }
            }
        }
        g_moveIterations = savedIterations;
    } else {
        // No collision: a single ray down from the body sets the ground contact.
        const S_vector4 sim = m_meleeTracker->GetPosition4();
        S_vector from(sim.x + moveX, sim.y + 1.0f, sim.z + moveZ);
        S_vector down(0.0f, moveY - 1.0f, 0.0f);
        if (animClass == 0) {
            delta = S_vector(sim.x - m_spherePos[1].x, sim.y - m_spherePos[1].y, sim.z - m_spherePos[1].z);
            groundRef = m_spherePos[1];
        } else {
            delta = S_vector(sim.x - m_spherePos[0].x, sim.y - m_spherePos[0].y, sim.z - m_spherePos[0].z);
            groundRef = m_spherePos[0];
        }
        if (m_volumeA[0])
            g_collisionWorld.SetIgnoredVolumes(m_volumeA, 2, 0);
        S_vector rayN, rayP;
        hit = CollisionRayTest(from, down, &rayN, &rayP, 0xFFFFFFFF, 0x10000);
        g_collisionWorld.SetIgnoredVolume(nullptr, 0);
        if (hit) {
            contacts.count = 1;
            contacts.sphere[0] = 0;
            contacts.normals[0] = {rayP.x, rayP.y, rayP.z, 0.0f};
            contacts.pos = {rayN.x - delta.x, rayN.y - delta.y, rayN.z - delta.z, 0.0f};
            contacts.volumes[0] = hit;
        }
        m_airSteps = 0.0f;
    }

    {
        S_CollisionVolume *savedHit = hit;
        const bool grounded = IsGrounded(contacts);
        if (!grounded)
            m_airSteps = m_airSteps + 1.0f;
        else
            m_airSteps = 0.0f;
        // Fix: the original builds a "moving" flag and a surface normal from an uninitialised stack
        // vector here; the port zeroes them (deterministic: the slope-slide via that path stays off).
        const float normal[3] = {0.0f, 0.0f, 0.0f};
        const bool moving = false;
        float moveVec[4] = {moveX, moveY, moveZ, moveW};
        SlopeSlide(&contacts, normal, savedHit, moving, &groundRef.x, moveVec, timeDelta);
        ContactResponse(hit, grounded ? 1 : 0, timeDelta, &contacts.move.x);
    }

applyPosition:
    if (hit != nullptr && !rayGround) {
        contacts.pos.x += delta.x;
        contacts.pos.y += delta.y;
        contacts.pos.z += delta.z;
        const S_vector4 sim = m_meleeTracker->GetPosition4();
        if (moveX == 0.0f && moveZ == 0.0f && static_cast<int>(m_1f4) >= 0) {
            m_264 += timeDelta;
            contacts.pos = {sim.x, sim.y, sim.z, sim.w};
        } else {
            m_264 = 0;
        }
        const bool grounded = IsGrounded(contacts);
        if (!grounded) {
            m_240 = 5;
        } else {
            m_250 = 0.0f;
            m_254 = 0.0f;
            m_25c = MELEE_TRACKER_MOVE_NO_TIME;
            m_264 = 0;
        }
        m_material = VolumeMaterial(hit);
        const S_vector4 now = m_meleeTracker->GetPosition4();
        if (contacts.pos.x != now.x || contacts.pos.y != now.y || contacts.pos.z != now.z) {
            m_meleeTracker->SetPosition4(contacts.pos);
            m_moved = 1;
        }
    }

    // On a slope (the frame pitches), mark moved so the frame is refreshed.
    S_vector forward = MeleeTrackerMatrix(m_meleeTracker->GetFrame())(2);
    const float sq = forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;
    if (std::fabs(sq - 1.0f) >= 1e-08f) {
        if (sq >= 1e-08f)
            forward.y *= 1.0f / std::sqrt(sq);
        else if (forward.x == 0.0f && forward.z == 0.0f)
            forward.y = forward.y >= 0.0f ? 1.0f : -1.0f;
    }
    if (std::fabs(forward.y) > 0.01f)
        m_moved = 1;

    if (m_374 == 0)
        AlignToSlope(timeDelta);

    // A debug trajectory line from the body to where it moved (rendered only in a debug build).
    const S_vector &world = MeleeTrackerMatrix(m_meleeTracker->GetFrame())(3);
    const float from[4] = {world.x, world.y, world.z, 1.0f};
    const float to[4] = {world.x + moveX, world.y + moveY, world.z + moveZ, 1.0f};
    DrawDebugLine(from, to, 0xFFFF00, 0);
}

// ===========================================================================
// The animation decision layer (driven by the actor / AI): picks and sequences the meleeTracker's
// animations. g_meleeTrackerAnims is loaded from Tables\dog_anim.sav by the meleeTracker setup (part 3).

S_MeleeTrackerAnim g_meleeTrackerAnims[0x17];                             // 0x008AC5C0
namespace {
int g_meleeTrackerBarkTurnCooldown = 0;                          // DAT_008AC5B8

// Whether an animation id plays once (attacks, and the high-numbered one-shots) rather than
// looping. (0x005c8880, as used by StepAnimation.)
int MeleeTrackerAnimOneShot(int id)
{
    if ((id < 0xD || id == 0x12) && id != 5 && id != 6)
        return 0;
    return 1;
}
}  // namespace

// ---------------------------------------------------------------------------
// play the next animation of the sequence through the mixer (0x005c4cb0)

void C_MeleeTrackerMovement::StepAnimation(int /*unused*/)
{
    S_MeleeTrackerAnimSequence &seq = m_currentSeq;
    if ((seq.readIndex == -1 || seq.readIndex < seq.endIndex) && m_35c == -1)
        return;
    if (seq.readIndex == -1 || seq.readIndex < seq.endIndex) {
        if (m_35c == -1)
            return;
        seq = m_queue[m_35c];
        QueuePopFront();
    }

    int id = seq.anims[seq.readIndex];
    bool advance = (id == 0x17);
    if (!advance && (id == 5 || id == 6)) {
        if (AiTarget(m_meleeTracker) == nullptr)
            advance = true;
    }
    if (advance) {
        if (seq.readIndex != -1 && seq.readIndex >= seq.endIndex)
            --seq.readIndex;
        if (seq.readIndex == -1 || seq.readIndex < seq.endIndex) {
            if (m_35c == -1)
                return;
            seq = m_queue[m_35c];
            QueuePopFront();
            return;
        }
        id = seq.anims[seq.readIndex];
    } else if (id == 6 &&
               AiTargetDistance(m_meleeTracker) >= 1.8f) {
        id = 5;
    }

    const S_MeleeTrackerAnim &entry = g_meleeTrackerAnims[id];
    int blendTime = entry.blendTime;
    int trackTime = m_mixer.GetActiveTrackTime(0);
    const int trackLen = m_mixer.GetTrackLength(0);
    if (m_364 == 5) {
        trackTime += 0x4B0;
        blendTime = 400;
    }
    // Keep the current (looping) animation if it is still playing out.
    if (trackTime != -1 && trackTime + blendTime < trackLen && m_36c == 0 && MeleeTrackerAnimOneShot(m_364))
        return;

    if (entry.anim) {
        S_MeleeTrackerMixerAnim anims[2] = {};
        anims[0].anim = entry.anim;
        anims[0].speed = entry.speed;
        anims[0].weight = 1.0f;
        anims[0].blendTime = static_cast<float>(blendTime);
        anims[0].crossfade = 1;
        anims[0].anim->SetPlayMode(MeleeTrackerAnimOneShot(id) ? 1 : 2);
        m_mixer.PlayStages(anims);
        m_368 = m_364;
        m_364 = id;
        if (entry.speed == 0.0f)
            m_3e0 |= 1;
        else
            m_3e0 &= ~1u;
        PlayAnimSound(id);
    }
    m_36c = 0;

    if (seq.readIndex != -1 && seq.readIndex >= seq.endIndex)
        --seq.readIndex;
    if ((seq.readIndex == -1 || seq.readIndex < seq.endIndex) && m_35c != -1) {
        seq = m_queue[m_35c];
        QueuePopFront();
    }
}

// ---------------------------------------------------------------------------
// build the animation-sequence queue for the current mode / sub-modes (0x005c5030)

namespace {
// Pushes an animation id onto a sequence slot (the run is filled from the back, read from 3 down).
void SeqPush(S_MeleeTrackerAnimSequence *s, int id)
{
    if (s->readIndex == -1) {
        s->readIndex = 3;
        s->endIndex = 4;
    }
    if (s->endIndex > 0)
        s->anims[--s->endIndex] = id;
}
}  // namespace

void C_MeleeTrackerMovement::BuildSequence()
{
    S_MeleeTrackerAnimSequence *slot = nullptr;                  // iVar11
    const int local_88 = GetSeqAnimA();
    int chosen = GetSeqAnimA();                         // uVar6: the final animation state
    int subB = GetSeqAnimC();                           // local_78
    int subA = GetSeqAnimB();                           // local_7c

    auto ensureSlot = [this, &slot]() { return slot != nullptr || (slot = QueuePushSlot()) != nullptr; };
    auto mouthSound = [this](int flags, int group) {
        S_SoundRequest req;
        const S_vector &mouth = MeleeTrackerMatrix(m_bodyFrame)(3);
        const S_vector4 dp = m_meleeTracker->GetPosition4();
        req.pos = S_vector(mouth.x - dp.x, mouth.y - dp.y, mouth.z - dp.z);
        req.flags = static_cast<uint32_t>(flags);
        req.attachTo = m_meleeTracker->GetFrame();
        req.source = nullptr;
        req.chapter = 0xB;
        req.group = group;
        return GetSoundModule()->Play(req);
    };

    // --- sub-mode B transitions ---
    if (GetSeqAnimC() != m_subModeB && (slot = QueuePushSlot()) != nullptr) {
        if (GetSeqAnimC() == 0) {
            if (MeleeTrackerAnimClass(GetSeqAnimA()) != 3)
                SeqPush(slot, 9);
            SeqPush(slot, 0x15);
            SeqPush(slot, 7);
            subB = m_subModeB;
            chosen = 7;
        } else {
            if (MeleeTrackerAnimClass(GetSeqAnimA()) != 3)
                SeqPush(slot, 7);
            SeqPush(slot, 0x16);
            SeqPush(slot, 9);
            subB = m_subModeB;
            chosen = 9;
        }
    }

    // --- sub-mode A transitions ---
    if (GetSeqAnimB() != m_subModeA && MeleeTrackerAnimClass(GetSeqAnimA()) == m_mode && GetSeqAnimB() != 2 &&
        (MeleeTrackerAnimClass(GetSeqAnimA()) == 1 || MeleeTrackerAnimClass(GetSeqAnimA()) == 0)) {
        bool done = false;
        if (m_subModeA == 1) {
            if (MeleeTrackerAnimClass(GetSeqAnimA()) == 1 && ensureSlot()) {
                SeqPush(slot, 4);
                subA = m_subModeA;
                chosen = 4;
            }
        } else if (m_subModeA == 0) {
            if (MeleeTrackerAnimClass(GetSeqAnimA()) != 1) {
                if (ensureSlot()) {
                    SeqPush(slot, 2);
                    subA = m_subModeA;
                    chosen = 2;
                }
                done = true;
            } else if (ensureSlot()) {
                SeqPush(slot, 3);
                chosen = 3;
                subA = m_subModeA;
            } else {
                done = true;
            }
        } else {
            if (MeleeTrackerAnimClass(GetSeqAnimA()) == 1) {
                if (ensureSlot()) {
                    SeqPush(slot, 1);
                    subA = m_subModeA;
                    chosen = 1;
                }
                done = true;
            } else if (ensureSlot()) {
                SeqPush(slot, 0);
                chosen = 0;
                subA = m_subModeA;
            } else {
                done = true;
            }
        }
        (void)done;
    }

    // --- mode transitions ---
    const bool idleReset = m_364 == 0x17 && m_368 == 0x17 && m_mode != 0x17;
    if ((MeleeTrackerAnimClass(GetSeqAnimA()) != m_mode || idleReset) && ensureSlot()) {
        int local88 = local_88;
        if (m_subModeB == 1 && m_mode != 3 && m_mode != 6 && m_mode != 5) {
            if (MeleeTrackerAnimClass(GetSeqAnimA()) != 3)
                SeqPush(slot, 7);
            SeqPush(slot, 0x16);
            SeqPush(slot, 9);
            chosen = 9;
            local88 = 9;
            SetSubModeB(0, 0);
            subB = 0;
        }
        switch (m_mode) {
        case 0:
            chosen = 2;
            if (m_subModeA == 2) {
                SeqPush(slot, 0);
                subA = m_subModeA;
                chosen = 0;
            } else {
                SeqPush(slot, 2);
                subA = m_subModeA;
            }
            break;
        case 1:
            if (m_subModeA == 2) {
                SeqPush(slot, 1);
                subA = m_subModeA;
                chosen = 1;
            } else if (m_subModeA == 1) {
                SeqPush(slot, 4);
                subA = m_subModeA;
                chosen = 4;
            } else {
                SeqPush(slot, 3);
                subA = m_subModeA;
                chosen = 3;
            }
            break;
        case 2:
            if (local88 < 5) {
                SeqPush(slot, 5);
                chosen = 5;
            } else {
                SeqPush(slot, 6);
                chosen = 6;
            }
            SeqPush(slot, local88);
            break;
        case 3:
            if (m_subModeB == 1) {
                SeqPush(slot, 7);
                chosen = 7;
            } else {
                SeqPush(slot, 9);
                chosen = 9;
            }
            break;
        case 4:
            if (m_364 < 7 || m_364 > 0xC) {
                QueuePopBack();
                if (m_370 > 0)
                    return;
                if (m_soundHandle[1] != -1)
                    GetSoundModule()->Stop(&m_soundHandle[1]);
                m_soundHandle[1] = -1;
                m_370 = 500;
                m_soundHandle[1] = mouthSound(5, 4);
                return;
            }
            SeqPush(slot, 0x12);
            chosen = 0x12;
            break;
        case 5: {
            if (GetSeqAnimC() == 7) {
                int r = std::rand() & static_cast<int>(0x80000001);
                if (r < 0)
                    r = ((r - 1) | static_cast<int>(0xFFFFFFFE)) + 1;
                SeqPush(slot, r + 0xD);
            } else {
                SeqPush(slot, std::rand() % 3 + 0xF);
            }
            mouthSound(1, 2);
            chosen = 0xD;
            break;
        }
        case 6:
            if (GetSeqAnimC() == 1) {
                SeqPush(slot, 0x13);
                chosen = 0x13;
            } else {
                SeqPush(slot, 0x14);
                chosen = 0x14;
            }
            SeqPush(slot, local88);
            break;
        default:
            break;
        }
    }

    m_modePriority = 0;
    m_subModeBPriority = 0;
    m_subModeAPriority = 0;
    if (slot) {
        slot->animA = chosen;
        slot->animB = subA;
        slot->animC = subB;
    }
}

// ---------------------------------------------------------------------------
// advance the attack animation sequence when attacking (0x005c5a90)

void C_MeleeTrackerMovement::CheckAttackSequence()
{
    if (m_364 == 5 || m_364 == 6)
        m_36c = 1;
    S_MeleeTrackerAnimSequence &seq = m_currentSeq;
    if (!((seq.readIndex != -1 && seq.readIndex >= seq.endIndex) || m_35c != -1))
        return;
    if (seq.readIndex == -1 || seq.readIndex < seq.endIndex) {
        if (m_35c == -1)
            return;
        seq = m_queue[m_35c];
        QueuePopFront();
    }
    if (seq.anims[seq.readIndex] == 5 || seq.anims[seq.readIndex] == 6) {
        if (seq.readIndex != -1 && seq.readIndex >= seq.endIndex)
            --seq.readIndex;
        if ((seq.readIndex == -1 || seq.readIndex < seq.endIndex) && m_35c != -1) {
            seq = m_queue[m_35c];
            QueuePopFront();
        }
    }
}

// ---------------------------------------------------------------------------
// per-animation sounds / effects (0x005c5bf0)

void C_MeleeTrackerMovement::PlayAnimSound(int animId)
{
    const int animClass = MeleeTrackerAnimClass(animId);
    if (m_soundHandle[0] != -1)
        GetSoundModule()->Stop(&m_soundHandle[0]);
    m_soundHandle[0] = -1;
    SetAnimSpeed(1.0f);

    // A looped ACTION-chapter sound (chapter 0xB) from the meleeTracker's mouth frame.
    auto mouthSound = [this](int flags, int group) {
        S_SoundRequest req;
        const S_vector &mouth = MeleeTrackerMatrix(m_bodyFrame)(3);
        const S_vector4 dp = m_meleeTracker->GetPosition4();
        req.pos = S_vector(mouth.x - dp.x, mouth.y - dp.y, mouth.z - dp.z);
        req.flags = static_cast<uint32_t>(flags);
        req.attachTo = m_meleeTracker->GetFrame();
        req.source = nullptr;
        req.chapter = 0xB;
        req.group = group;
        return GetSoundModule()->Play(req);
    };

    if (animId == 4) {
        m_soundHandle[0] = mouthSound(0xD, 6);
        return;
    }
    switch (animClass) {
    case 2:
        if (m_soundHandle[2] != -1)
            GetSoundModule()->Stop(&m_soundHandle[2]);
        if (m_soundHandle[1] != -1)
            GetSoundModule()->Stop(&m_soundHandle[1]);
        m_soundHandle[1] = -1;
        m_168 = nullptr;
        m_soundHandle[2] = -1;
        if (m_animFrame)
            ResetAnimFrame(m_animFrame);
        m_moveDir = {0.0f, 0.0f, 0.0f, m_moveDir.w};
        m_soundHandle[1] = mouthSound(5, 5);
        return;
    case 4:
        if (m_370 < 1) {
            if (m_soundHandle[2] != -1)
                GetSoundModule()->Stop(&m_soundHandle[2]);
            if (m_soundHandle[1] != -1)
                GetSoundModule()->Stop(&m_soundHandle[1]);
            m_soundHandle[1] = -1;
            m_soundHandle[2] = -1;
            m_370 = 500;
            m_soundHandle[1] = mouthSound(5, 4);
        }
        break;
    case 5:
        if (m_soundHandle[2] != -1)
            GetSoundModule()->Stop(&m_soundHandle[2]);
        m_soundHandle[2] = -1;
        mouthSound(1, 8);
        break;
    case 6:
        if (m_soundHandle[2] != -1)
            GetSoundModule()->Stop(&m_soundHandle[2]);
        if (m_soundHandle[1] != -1)
            GetSoundModule()->Stop(&m_soundHandle[1]);
        m_soundHandle[1] = -1;
        m_soundHandle[2] = -1;
        mouthSound(1, 7);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// turn to bark at something, with a cooldown (0x005c6c70)

void C_MeleeTrackerMovement::BarkTurn()
{
    if (g_meleeTrackerBarkTurnCooldown != 0) {
        --g_meleeTrackerBarkTurnCooldown;
        return;
    }
    const S_vector4 d = m_meleeTracker->GetDirection4();
    S_vector dir(d.x, d.y, d.z);
    RotateAboutY(dir, 2.8f);
    const float facing[4] = {dir.x, dir.y, dir.z, d.w};
    SetFacing(facing);
    g_meleeTrackerBarkTurnCooldown = 0;
}
