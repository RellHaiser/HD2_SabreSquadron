// The special moves of a human's body (C_HumanSpecialMoves, C_HumanMovement +0xAA0; original
// source 0x0074a020..0x00750940): what the body may do where it is, tested against the
// collision world with its stance spheres: turning on the spot, leaning around a corner,
// climbing over obstacles, getting onto ladders, room for the stance changes, the water it
// stands in, and the death (the death animation for the hit and the room to fall).
//
// (0x0074a020..0x0074a1a0 are static initialisers: the two lean directions of the stance test
// at 0x009831b0 / 0x009831c0, their reference at 0x009831d0, and the death animations' random
// seed at 0x009831a0 taken from clock().)
#include "game/human_movement.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include "game/actor.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_anim_tables.h"
#include "game/human_body.h"
#include "game/human_equipment.h"
#include "game/human_internal.h"
#include "game/ladder.h"
#include "game/pending.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

const float HALF_PI = 1.57079637f;                      // 0x3fc90fdb

// The collision filters of the body's tests (excluded volume types / flags).
constexpr uint32_t TEST_TYPES = 0xC8002;
constexpr uint32_t TEST_FLAGS = 0x230000;

// The turn in place animations ("otocP" / "otocL").
constexpr int ANIM_TURN_RIGHT = 0x2B;
constexpr int ANIM_TURN_LEFT = 0x2C;
constexpr int ANIM_SITTING = 0x22;                      // "sedim1"
constexpr int ANIM_SIT_DOWN = 0x1C;                     // "sednuti"
constexpr int ANIM_SIT_DOWN_CROSS_LEGGED = 0x24;        
bool InRange(int value, int low, int high) { return value >= low && value <= high; }
bool IsLean(int anim) { return InRange(anim, 0xA2, 0xA5); }

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

}  // namespace

// The random seed of the death animations (a frame count at the start; +1 per death).
int g_deathSeed = static_cast<int>(std::labs(static_cast<long>(std::clock())) % 100);   // 0x009831a0

// ------------------------------------------------------------------ setting up

void C_HumanSpecialMoves::Init(C_HumanMovement *movement, C_HumanLocomotion *locomotion, C_HumanAnimPlayer *animPlayer,
                               C_HumanIdle *idle, C_Human *owner)
{
    m_movement = movement;
    m_owner = owner;
    m_locomotion = locomotion;
    m_animPlayer = animPlayer;
    m_idle = idle;
}

// ------------------------------------------------------------------ turning and leaning

bool C_HumanSpecialMoves::CanTurn(float angle)
{
    const S_vector4 pos = m_owner->GetPosition4();
    // (Tested only where the body is simulated and not for the enemies of a local game, and
    // only swimming, leaning or lying.)
    if (!(IsLocalGame() || m_owner->IsLocalPlayer()) || (IsLocalGame() && m_owner->GetRole() == HUMAN_ROLE_ENEMY))
        return true;
    C_HumanMovement *movement = m_movement;
    const int stance = m_owner->GetWork()->m_request.stance;
    if (!(movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING || IsLean(movement->m_animState.anim.anim) ||
          (stance != HUMAN_STANCE_STAND && stance != HUMAN_STANCE_CROUCH)))
        return true;
    movement->UpdateStanceVolumes(stance);
    // Each sphere swept around the body's position by the angle, a quarter turn at most per
    // step (the end spheres as spheres, the others as rays).
    for (int i = 0; i < movement->m_stanceVolumeCount; ++i) {
        const S_vector4 &sphere = movement->m_stanceSpheres[i];
        S_vector at(sphere.x, sphere.y + 0.2f, sphere.z);
        float step = angle;
        float left = angle;
        do {
            S_vector offset(at.x - pos.x, 0.0f, at.z - pos.z);
            const float height = at.y - pos.y;
            if (std::fabs(step) > HALF_PI)
                step = angle <= 0.0f ? -HALF_PI : HALF_PI;
            RotateAboutY(offset, -step);
            offset.y = height;
            const S_vector to(offset.x + pos.x, pos.y + offset.y, offset.z + pos.z);
            S_vector hitPos, normal;
            movement->RemoveCollision(false);
            S_CollisionVolume *hit = i == 0 || i == movement->m_stanceVolumeCount - 1
                                         ? CollisionSphereTest(at, to - at, sphere.w, &hitPos, &normal, TEST_TYPES, TEST_FLAGS)
                                         : g_collisionWorld.RayTest(at, to - at, &hitPos, &normal, TEST_TYPES, TEST_FLAGS);
            movement->RestoreCollision(false);
            if (!hit) {
                at = to;
            } else {
                at = hitPos;
                if ((normal.z + normal.x) * 0.0f + normal.y < 0.55f) {
                    // A wall: no turning in place.
                    S_HumanAnimRef &asked = movement->m_nextAnimState.anim;
                    if (asked.anim == ANIM_TURN_LEFT || asked.anim == ANIM_TURN_RIGHT) {
                        asked.anim = HUMAN_ANIM_NONE;
                        asked.Normalize();
                    }
                    return false;
                }
            }
            left -= left <= 0.0f ? -HALF_PI : HALF_PI;
            step = left;
        } while ((step >= 0.0f && angle >= 0.0f) || (step < 0.0f && angle < 0.0f));
    }
    return true;
}

void C_HumanSpecialMoves::AdjustLean(S_HumanAnimState *next)
{
    const S_HumanAnimRef &asked = next->anim;
    if (!IsLean(asked.anim))
        return;
    C_HumanMovement *movement = m_movement;
    const S_HumanAnimRef &current = movement->m_animState.anim;
    if (asked.anim == current.anim && asked.stance == current.stance && asked.sub == current.sub)
        return;
    movement->UpdateStanceVolumes(m_owner->GetWork()->m_request.stance);
    // From the head sideways (a little down), as far as the lean goes for the stance.
    const S_vector head = movement->m_joints[HUMAN_JOINT_F1].frame->GetWorldPos();
    const S_vector from(head.x, head.y + 0.05f, head.z);
    const S_vector4 &facing = m_owner->GetDirection4();
    S_vector reach = S_vector(facing.x, -0.2f, facing.z).Normalized() * 1.4f;
    RotateAboutY(reach, asked.anim == 0xA2 || asked.anim == 0xA4 ? HALF_PI : -HALF_PI);
    if (asked.stance == HUMAN_STANCE_STAND)
        reach *= 0.5f;
    else if (asked.stance == HUMAN_STANCE_CROUCH)
        reach *= 0.45f;
    else if (asked.stance == HUMAN_STANCE_PRONE)
        reach *= 0.3f;
    S_vector hitPos, normal;
    movement->RemoveCollision(false);
    S_CollisionVolume *hit = CollisionSphereTest(from, reach, 0.2f, &hitPos, &normal, TEST_TYPES, TEST_FLAGS);
    movement->RestoreCollision(false);
    if (!hit)
        return;
    // A wall in the way: the lean holds where the head touches it.
    const float length = reach.Magnitude();
    const float free = (from - hitPos).Magnitude();
    if (length != 0.0f) {
        m_animPlayer->m_unknownA0 = free / (length + length);
        return;
    }
    next->anim.anim = HUMAN_ANIM_NONE;
    next->anim.Normalize();
}

void C_HumanSpecialMoves::StartSitting(S_HumanAnimState *next)
{
    next->anim.anim = next->anim.anim == ANIM_SITTING ? ANIM_SIT_DOWN : ANIM_SIT_DOWN_CROSS_LEGGED;
    next->anim.Normalize();
}

void C_HumanSpecialMoves::PushOutOfWalls()
{
    C_HumanMovement *movement = m_movement;
    movement->UpdateStanceVolumes(m_owner->GetWork()->m_request.stance);
    const S_vector4 &facing = m_owner->GetDirection4();
    const S_vector reach = XYZ(facing) * 0.4f;
    for (int i = 0; i < movement->m_stanceVolumeCount; ++i) {
        const S_vector4 &sphere = movement->m_stanceSpheres[i];
        const S_vector center = XYZ(sphere);
        S_vector hitPos, normal;
        movement->RemoveCollision(false);
        S_CollisionVolume *hit = CollisionSphereTest(center, reach, sphere.w * 0.9f, &hitPos, &normal, 0x48002, TEST_FLAGS);
        // Fix: the collision of the body back in on a hit too (the original leaves it out).
        movement->RestoreCollision(false);
        if (!hit)
            continue;
        if ((normal.z + normal.x) * 0.0f + normal.y < 0.5f) {
            // A wall: back by what the 0.4 m did not reach.
            const float back = 0.4f - (hitPos - center).Magnitude();
            movement->m_mixer.AddMove2({-facing.x * back, -facing.y * back, -facing.z * back, facing.w}, 500);
        }
        return;
    }
}

// ------------------------------------------------------------------ water

bool C_HumanSpecialMoves::UpdateWater()
{
    C_HumanLocomotion *locomotion = m_locomotion;
    float surface = -1000.0f;
    float deep = 1.4f;
    float shallow = 0.9f;
    S_vector pos = XYZ(m_owner->GetPosition4());
    const S_vector4 &facing = m_owner->GetDirection4();
    // No water in the sector.
    if (locomotion->m_waterLevel == -FLT_MAX)
        return false;
    if (m_movement->m_unknown9AC != MOVE_NO_TIME)
        return false;
    if (locomotion->m_waterLevel < pos.y) {
        locomotion->m_waterHeight = MOVE_NO_VALUE;
        locomotion->m_waterDepth = MOVE_NO_VALUE;
        locomotion->m_waterSurface = MOVE_NO_VALUE;
        return false;
    }
    locomotion->m_waterHeight = MOVE_NO_VALUE;
    locomotion->m_waterDepth = MOVE_NO_VALUE;
    // The depths of the stance (swimming: a little ahead).
    const int stance = m_owner->GetWork()->m_request.stance;
    bool stanceKnown = true;
    if (stance == HUMAN_STANCE_STAND) {
        deep = 1.4f;
        shallow = 0.9f;
    } else if (stance == HUMAN_STANCE_CROUCH) {
        deep = 1.2f;
        shallow = 0.7f;
    } else if (stance == HUMAN_STANCE_PRONE) {
        deep = 1.0f;
        shallow = 0.7f;
    } else {
        stanceKnown = false;
    }
    if (stanceKnown && m_movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING) {
        pos.x += facing.x * 0.3f;
        pos.z += facing.z * 0.3f;
    }
    // The surface above.
    S_vector hitPos, normal;
    S_CollisionVolume *water = g_collisionWorld.RayTestWater(pos, S_vector(0.0f, 600.0f, 0.0f), &hitPos, &normal, 0);
    if (!water) {
        locomotion->m_waterHeight = -1.0f;
    } else {
        locomotion->m_waterHeight = hitPos.y - pos.y;
        surface = hitPos.y;
        m_movement->m_unknown764 = GetCollisionMaterial(water);
    }
    if (locomotion->m_waterHeight < 0.0f) {
        locomotion->m_waterHeight = MOVE_NO_VALUE;
        locomotion->m_waterDepth = MOVE_NO_VALUE;
        locomotion->m_waterSurface = water ? surface : MOVE_NO_VALUE;
        return false;
    }
    locomotion->m_waterSurface = surface;
    m_movement->m_footstepSurface = -8;
    // The ground below (from the body's height).
    pos.y = m_owner->GetPosition4().y;
    m_movement->RemoveCollision(false);
    S_CollisionVolume *ground = CollisionRayTest(pos, S_vector(0.0f, -600.0f, 0.0f), &hitPos, &normal, 0xFFFFFFFF, TEST_FLAGS);
    m_movement->RestoreCollision(false);
    if (locomotion->m_waterHeight > 0.2f)
        m_owner->GetWork()->m_bodyFlags |= 0x800;
    locomotion->m_waterDepth = ground ? surface - hitPos.y + 0.01f : 10000.0f;

    C_HumanMovement *movement = m_movement;
    if (movement->m_unknownA2C == MOVE_NO_TIME) {
        if (movement->m_unknownA30 == MOVE_NO_TIME) {
            const int mode = movement->m_owner->GetWork()->m_mode;
            if (deep < locomotion->m_waterDepth) {
                // Deep: swimming once the water is high enough at the body.
                if (mode != WORK_MODE_SWIMMING) {
                    const int requested = m_owner->GetWork()->m_request.stance;
                    if (requested == HUMAN_STANCE_STAND) {
                        if (locomotion->m_waterHeight < 1.0f)
                            return false;
                    } else if (requested == HUMAN_STANCE_CROUCH) {
                        if (locomotion->m_waterHeight < 0.7f)
                            return false;
                    } else if (requested == HUMAN_STANCE_PRONE && locomotion->m_waterHeight < 0.4f) {
                        return false;
                    }
                }
                return true;
            }
            // Shallow: swimming goes on until the ground comes close.
            if (mode != WORK_MODE_SWIMMING)
                return false;
            if (locomotion->m_waterDepth < shallow && locomotion->m_waterHeight < locomotion->m_waterDepth)
                return false;
            return true;
        }
    } else if (movement->m_unknownA30 == MOVE_NO_TIME) {
        return false;
    }
    // Carrying something into the water: dropped.
    m_owner->DropCarried();
    if (locomotion->m_fallStartY != MOVE_NO_VALUE)
        locomotion->m_fallStartY = m_owner->GetPosition4().y;
    return false;
}

// ------------------------------------------------------------------ ladders

int C_HumanSpecialMoves::UseLadder(C_Actor *ladder)
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    const S_vector4 ladderDir = GetLadderDir(movement->m_useObject);
    const S_vector pos = XYZ(m_owner->GetPosition4());
    const S_vector toTop = pos - XYZ(GetLadderTop(ladder));
    const S_vector toBottom = pos - XYZ(GetLadderBottom(ladder));
    const bool atBottom = toBottom.Square() <= toTop.Square();
    if (!IsLocalGame() && !m_owner->IsLocalPlayer() && !m_owner->IsServerSimulated()) {
        // A replica: on the ladder at once (climbing up from the bottom).
        movement->SetSeatMode(0, true);
        movement->m_useState = 2;
        if (atBottom) {
            movement->m_nextAnimState.anim.anim = 0x6A;
            movement->m_nextAnimState.anim.Normalize();
            work->Play(0x6A, true);
            equipment->PutAwayHandsItem(true);
        }
        m_locomotion->m_fallSpeed = 0.0f;
        m_locomotion->m_fallStepTime = 0.0f;
        return 1;
    }
    if (movement->m_useState != MOVE_NO_TIME)
        return 1;
    if (movement->m_unknownA30 != MOVE_NO_TIME || InRange(movement->m_animState.carryAnim, 0x50, 0x52))
        return -1;
    movement->m_useState = MOVE_NO_TIME;
    const S_vector4 still = {0.0f, 0.0f, 0.0f, 0.0f};
    if (atBottom) {
        if (m_locomotion->m_fallSpeed > 10.791f || !CanEnterLadderFromBottom(ladderDir))
            return 0;
        movement->m_useState = 2;
        movement->m_nextAnimState.anim.anim = 0x6A;
        movement->m_nextAnimState.anim.Normalize();
        work->m_forced = true;
        work->Play(0x6A, true);
        work->m_forced = false;
        equipment->PutAwayHandsItem(true);
        movement->m_mixer.SetVelocity(still, 0.0f, 0.0f, &m_locomotion->m_velocity);
        movement->m_mixer.SetVelocity(still, 0.0f, 0.0f, &m_locomotion->m_velocity);
    } else {
        if (!EnterLadderFromTop(ladderDir))
            return 0;
        // From the top: onto the ladder going down (animation 8), standing.
        movement->m_useState = 2;
        movement->m_nextAnimState.anim.anim = 8;
        movement->m_nextAnimState.anim.Normalize();
        work->m_forced = true;
        work->Play(movement->m_nextAnimState.anim.anim, true);
        work->m_blocked |= WORK_REQUEST_ANIM;
        work->SetLookAt(still, true);
        movement->m_owner->GetWork()->SetStance(HUMAN_STANCE_STAND, true);
        work->m_forced = false;
        movement->m_mixer.SetVelocity(still, 0.0f, 0.0f, &m_locomotion->m_velocity);
        // At the top, a little along the ladder's facing (not below the body's height).
        const S_vector4 &top = GetLadderTop(ladder);
        const S_vector4 &dir = GetLadderDir(ladder);
        const float bodyY = m_owner->GetPosition4().y;
        const float y = top.y <= bodyY ? bodyY : top.y;
        m_owner->SetPosition(S_vector(dir.x * 0.1f + top.x, dir.y * 0.1f + y, top.z + dir.z * 0.1f), top.w);
        equipment->PutAwayHandsItem(true);
    }
    movement->m_mixer.m_move.x = 0.0f;
    movement->m_mixer.m_move.y = 0.0f;
    movement->m_mixer.m_move.z = 0.0f;
    m_locomotion->m_fallSpeed = 0.0f;
    m_locomotion->m_fallStepTime = 0.0f;
    movement->m_unknown998 = m_locomotion->GetTurnTime(XYZ(GetLadderDir(ladder)));
    if (movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING) {
        movement->SetSeatMode(4, false);
        movement->m_mixer.AddMove2({0.0f, -0.6f, 0.0f, 0.0f}, 500);
    }
    work->m_forced = true;
    work->SetFacingDir(XYZ(GetLadderDir(movement->m_useObject)), true);
    work->m_forced = false;
    movement->SetSeatMode(0, true);
    AddLadderClimber(ladder, m_owner);
    return 1;
}

bool C_HumanSpecialMoves::EnterLadderFromTop(const S_vector4 &ladderDir)
{
    if (!IsLocalGame() && !m_movement->m_owner->IsLocalPlayer() && !m_owner->IsServerSimulated())
        return true;
    C_HumanMovement *movement = m_movement;
    const C_Actor *ladder = movement->m_useObject;
    const S_vector top = XYZ(GetLadderTop(ladder));
    const S_vector down = XYZ(GetLadderBottom(ladder)) - top;
    movement->UpdateStanceVolumes(m_owner->GetWork()->m_request.stance);
    movement->m_trailCount = movement->m_stanceVolumeCount;
    const S_vector4 lowest = movement->m_stanceSpheres[0];
    const float r = lowest.w;
    if (lowest.y < top.y - 0.3f) {
        // Below the top. (The original restores the collision it has not removed yet.)
        movement->RestoreCollision(false);
        return false;
    }
    movement->RemoveCollision(false);
    // Over to the top (at the body's height), then 1.8 m down the ladder.
    const S_vector over((top.x - lowest.x) + -ladderDir.x * r, -ladderDir.y * r + 0.0f, (top.z - lowest.z) + -ladderDir.z * r);
    const float radius = r - 0.1f;
    S_vector hitPos, normal;
    if (CollisionSphereTest(S_vector(lowest.x, lowest.y + 0.1f, lowest.z), over, radius, &hitPos, &normal, 0x8C002,
                            0xFFFFFFFF)) {
        movement->RestoreCollision(false);
        return false;
    }
    const S_vector onTop(lowest.x + over.x, over.y + lowest.y, lowest.z + over.z);
    const S_vector climb = down.Normalized() * 1.8f;
    if (CollisionSphereTest(onTop, climb, radius, &hitPos, &normal, 0x8C002, 0xFFFFFFFF)) {
        movement->RestoreCollision(false);
        return false;
    }
    // The trail spheres up the ladder from there.
    const S_vector end = onTop + climb;
    const S_vector step = -climb * r;
    for (int i = 0; i < movement->m_trailCount; ++i) {
        S_CollisionVolume *volume = movement->m_trailVolumes[i];
        volume->center = end + step * static_cast<float>(i);
        // (The original's w is an uninitialised stack value.)
        volume->centerW = 0.0f;
        volume->radius = movement->m_stanceSpheres[i].w;
    }
    movement->RestoreCollision(false);
    if (!IsLocalGame())
        movement->m_owner->SendRemoteRequest(REMOTE_REQUEST_CLIMB_START);
    return true;
}

bool C_HumanSpecialMoves::CanEnterLadderFromBottom(const S_vector4 &ladderDir)
{
    C_HumanMovement *movement = m_movement;
    const C_Actor *ladder = movement->m_useObject;
    const S_vector bottom = XYZ(GetLadderBottom(ladder));
    const S_vector up = (XYZ(GetLadderTop(ladder)) - bottom).Normalized() * 1.8f;
    // Nothing between the foot of the ladder (a little before it) and 1.8 m up.
    const S_vector from(up.x + -ladderDir.x * 0.25f + bottom.x, bottom.y + -ladderDir.y * 0.25f + up.y,
                        bottom.z + -ladderDir.z * 0.25f + up.z);
    S_vector hitPos, normal;
    movement->RemoveCollision(false);
    if (CollisionRayTest2D(from, -up, &hitPos, &normal, 0x14002, 0xFFFFFFFF)) {
        movement->RestoreCollision(false);
        return false;
    }
    movement->RestoreCollision(false);
    if (m_owner->IsLocalPlayer()) {
        // The player must face the ladder.
        const S_vector4 &pos = m_owner->GetPosition4();
        const S_vector toLadder = S_vector(bottom.x - pos.x, 0.0f, bottom.z - pos.z).Normalized();
        const S_vector facing = S_vector(ladderDir.x, 0.0f, ladderDir.z).Normalized();
        if (facing.Dot(toLadder) < 0.0f)
            return false;
    }
    return true;
}

// ------------------------------------------------------------------ climbing over obstacles

bool C_HumanSpecialMoves::TryClimbOver(S_HumanAnimState *next)
{
    if (!IsLocalGame() && !m_movement->m_owner->IsLocalPlayer() && !m_owner->IsServerSimulated())
        return true;
    if (m_owner->GetVelocity().y <= -0.08f)
        return false;
    C_HumanMovement *movement = m_movement;
    if (movement->m_unknownA2C != MOVE_NO_TIME || movement->m_unknownA30 != MOVE_NO_TIME)
        return false;
    if (InRange(movement->m_animState.anim.anim, 8, 0x0E))
        return false;
    C_HumanWork *work = m_owner->GetWork();
    // The lowest stance sphere as it was before this update.
    const S_vector4 lowest = movement->m_stanceSpheres[0];
    const float radius = lowest.w;
    S_vector facing = XYZ(m_owner->GetDirection4());
    int lowGap = 0;
    int probes = 3;
    float top = MOVE_NO_VALUE;
    const bool notStanding = work->m_request.stance != HUMAN_STANCE_STAND || movement->m_unknown9AC != MOVE_NO_TIME;
    if (movement->m_animState.anim.anim > 0xBD && work->m_request.gait == 5)
        probes = 2;
    movement->m_unknown96C = false;
    movement->UpdateStanceVolumes(m_owner->GetWork()->m_request.stance);
    movement->m_trailCount = movement->m_stanceVolumeCount;
    movement->RemoveCollision(false);
    const float foot = lowest.y - radius;
    facing.y = 0.0f;

    // The top of the obstacle: spheres dropped from 2.3 m up, from 0.4 m ahead on in 0.15 m
    // steps.
    bool found = false;
    float ground = MOVE_NO_VALUE;
    S_vector probe(facing.x * 0.4f + lowest.x, foot + radius + 2.3f, lowest.z + facing.z * 0.4f);
    S_vector hitPos, normal;
    int step = 0;
    do {
        S_CollisionVolume *hit =
            CollisionSphereTest(probe, S_vector(0.0f, -2.3f, 0.0f), radius, &hitPos, &normal, 0xCC202, TEST_FLAGS);
        if (hit) {
            // (Not over a static weapon.)
            if ((hit->flags & 0xC0) && (hit->flags & 0x40)) {
                const C_Actor *actor = GetFrameActor(hit->frame);
                if (actor && actor->GetType() == ACTOR_STATIC_WEAPON)
                    break;
            }
            if (normal.AngleTo(S_vector(0.0f, 1.0f, 0.0f)) < 0.5235988f)
                ground = hitPos.y;
            if (foot + 2.5f < ground)
                break;
            if (foot + 0.25f < ground) {
                // Higher than a step: an obstacle. Ground just behind it?
                const S_vector beyond(hitPos.x + facing.x * 0.275f, hitPos.y + 0.1f, hitPos.z + facing.z * 0.275f);
                top = ground;
                found = true;
                if (!CollisionRayTest(beyond, S_vector(0.0f, -0.35f - radius, 0.0f), &hitPos, &normal, 0xCC202, TEST_FLAGS))
                    movement->m_unknown96C = true;
            }
        }
        if (probes - 1 <= step)
            break;
        ++step;
        probe.x = facing.x * 0.15f + probe.x;
        probe.z = facing.z * 0.15f + probe.z;
    } while (!found);
    if (top == MOVE_NO_VALUE || ((top - radius) - foot <= 0.45f && m_locomotion->m_unknown0F0 == 0)) {
        movement->RestoreCollision(false);
        return false;
    }

    // Room to get over it: up from where the body stands (off the face of the ladder when on
    // one), ahead at that height, and above it the height of the body.
    const S_vector4 &ownerFacing = m_owner->GetDirection4();
    const S_vector reach(ownerFacing.x * 0.45f, 0.0f, ownerFacing.z * 0.45f);
    float backX = 0.0f;
    float backZ = 0.0f;
    if (movement->m_useState != MOVE_NO_TIME) {
        const S_vector4 &ladderDir = GetLadderDir(movement->m_useObject);
        backX = ladderDir.x * -0.3f;
        backZ = ladderDir.z * -0.3f;
    }
    const float height = (top - radius) - foot;
    const float lift = height + radius;
    const float up = lift - 0.06f;
    const float smaller = (radius - 0.06f) - 0.02f;
    const uint32_t types = movement->m_useState == MOVE_NO_TIME ? 0x88002 : 0x8C002;
    S_CollisionVolume *hit = CollisionSphereTest(S_vector(lowest.x + backX, foot + radius, lowest.z + backZ),
                                                 S_vector(0.0f, up, 0.0f), smaller, &hitPos, &normal, types, 0);
    if (hit) {
        // Something above: it is touched instead.
        S_MoveContacts contacts = {};
        contacts.pos = {hitPos.x, hitPos.y, hitPos.z, 0.0f};
        contacts.normals[0] = {normal.x, normal.y, normal.z, 0.0f};
        contacts.count = 1;
        contacts.volumes[0] = hit;
        contacts.sphere[0] = 0;
        C_Actor *touched = nullptr;
        ProcessMoveContacts(m_owner->GetMovement(), &contacts, true, &touched);
        movement->RestoreCollision(false);
        return false;
    }
    if (CollisionSphereTest(S_vector(lowest.x, lift + foot, lowest.z), reach, smaller, &hitPos, &normal, types, TEST_FLAGS)) {
        movement->RestoreCollision(false);
        return false;
    }
    const float rise = movement->m_unknown740.y - 0.12f;
    hit = CollisionSphereTest(S_vector(lowest.x + reach.x * 0.8f, (up + foot) + reach.y * 0.8f, lowest.z + reach.z * 0.8f),
                              S_vector(0.0f, rise, 0.0f), smaller, &hitPos, &normal, types, TEST_FLAGS);
    // The trail spheres over the obstacle (`offset` above the stance spheres).
    const auto placeTrail = [&](float offset) {
        for (int i = 0; i < movement->m_trailCount; ++i) {
            const S_vector4 &sphere = movement->m_stanceSpheres[i];
            S_CollisionVolume *volume = movement->m_trailVolumes[i];
            volume->center = S_vector(reach.x + sphere.x, offset + sphere.y, reach.z + sphere.z);
            // (The original's w is an uninitialised stack value.)
            volume->centerW = 0.0f;
            volume->radius = sphere.w;
        }
    };
    bool chooseAnim = false;
    if (!hit) {
        if (!notStanding) {
            placeTrail(height);
            chooseAnim = true;
        }
    } else {
        const float room = (hitPos.y - height) - foot;
        if (movement->m_unknown740.y * 0.95f + radius <= room) {
            if (!notStanding) {
                placeTrail(height);
                chooseAnim = true;
            }
        } else {
            if (room < movement->m_unknown740.y * 0.45f + radius) {
                movement->RestoreCollision(false);
                return false;
            }
            // Under something: over it crouching.
            lowGap = 1;
            if (!notStanding) {
                --movement->m_trailCount;
                placeTrail(lift);
                chooseAnim = true;
            }
        }
    }
    if (chooseAnim) {
        // The climbing animation for the height (from a ladder: 0x0E).
        movement->m_unknown958 = height;
        if (movement->m_useState == MOVE_NO_TIME) {
            const float h = movement->m_unknown958;
            if (h < 0.45f) {
                movement->RestoreCollision(false);
                return false;
            }
            next->anim.anim = h < 0.6f ? 0x0A : h < 1.078f ? 0x0B : h < 1.55f ? 0x0C : 0x0D;
        } else {
            next->anim.anim = 0x0E;
        }
        next->anim.Normalize();
    }
    if ((next->anim.anim == 0x0A || next->anim.anim == 9) && lowGap == 1) {
        movement->RestoreCollision(false);
        return false;
    }
    if (notStanding) {
        // Standing up first; the replicas climb and go back to the stance (remote steps).
        if (!movement->m_remoteActions.empty()) {
            movement->RestoreCollision(false);
            return false;
        }
        S_RemoteAction climb;
        climb.time = 1500;
        climb.action = 4;
        climb.phase = 0;
        movement->m_remoteActions.push_back(climb);
        S_RemoteAction back;
        back.time = 500;
        back.action = (m_owner->GetWork()->m_request.stance != HUMAN_STANCE_CROUCH) + 1;
        back.phase = 0;
        movement->m_remoteActions.push_back(back);
        m_owner->GetWork()->SetStance(HUMAN_STANCE_STAND, true);
        movement->RestoreCollision(false);
        return false;
    }
    next->anim.stance = HUMAN_STANCE_STAND;
    next->anim.Normalize();
    if (!IsLocalGame() && InRange(next->anim.anim, 0x0A, 0x0E)) {
        // The climbing animations 0x0A..0x0D and 0x0E (off a ladder) as the other side's
        // requests.
        static const int requests[] = {REMOTE_REQUEST_CLIMB_A, REMOTE_REQUEST_CLIMB_B, REMOTE_REQUEST_CLIMB_C,
                                       REMOTE_REQUEST_CLIMB_D, REMOTE_REQUEST_CLIMB_END};
        movement->m_owner->SendRemoteRequest(requests[next->anim.anim - 0x0A]);
    }
    movement->m_unknown954 = lowGap;
    const S_vector4 still = {0.0f, 0.0f, 0.0f, 0.0f};
    work->m_forced = true;
    work->SetLookAt(still, true);
    work->m_forced = false;
    movement->m_mixer.SetVelocity(still, 0.0f, 0.0f, &m_locomotion->m_velocity);
    movement->m_mixer.m_move.x = 0.0f;
    movement->m_mixer.m_move.y = 0.0f;
    movement->m_mixer.m_move.z = 0.0f;
    movement->RestoreCollision(false);
    return true;
}

// ------------------------------------------------------------------ dying

bool C_HumanSpecialMoves::HasRoomToFall(const S_vector4 &dir, int side)
{
    C_HumanMovement *movement = m_movement;
    movement->m_unknown1AC = 2;
    movement->m_unknown18C = true;
    const S_vector way = XYZ(dir).Normalized();
    const int anim = movement->m_animState.anim.anim;
    if (InRange(anim, 8, 0x0E) || InRange(anim, 0x6A, 0x6C)) {
        // Climbing: falls off backwards (a slide along the old direction, a little up).
        movement->m_unknown1AC = 3;
        movement->m_unknown18C = true;
        S_vector slide = XYZ(movement->m_unknown9D0);
        slide.Normalize();
        movement->m_unknown9D0.x = slide.x;
        movement->m_unknown9D0.y = 0.4f;
        movement->m_unknown9D0.z = slide.z;
        return true;
    }
    movement->RemoveCollision(false);
    const S_vector pos = XYZ(m_owner->GetPosition4());
    const S_vector up(0.0f, 1.0f, 0.0f);
    int blocked = 0;
    // (The angle of the last surface hit carries over from sphere to sphere, as in the
    // original.)
    float angle = 0.0f;
    for (int i = movement->m_stanceVolumeCount - 1; i >= 0; --i) {
        const S_vector4 &sphere = movement->m_stanceSpheres[i];
        const S_vector center = XYZ(sphere);
        // Where the sphere ends up: the body falling along the direction (crouching further).
        S_vector target;
        if (m_owner->GetWork()->m_request.stance == HUMAN_STANCE_CROUCH)
            target = pos + way * 2.1f * (center - pos).Magnitude();
        else if (i == movement->m_stanceVolumeCount - 1)
            target = center + way * 1.4f;
        else
            target = pos + way * 1.3f * (center - pos).Magnitude();
        S_vector hitPos, normal;
        S_CollisionVolume *hit = CollisionSphereTest(center, target - center, sphere.w, &hitPos, &normal, 0x88002, TEST_FLAGS);
        S_CollisionVolume *sideHit = nullptr;
        S_vector sidePos, sideNormal;
        if (i == 1 && movement->m_stanceVolumeCount > 2) {
            bool test = true;
            if (hit) {
                angle = normal.AngleTo(up);
                test = angle < 0.7853982f;
            }
            if (test)
                sideHit = CollisionSphereTest(center, way * 1.4f, movement->m_stanceSpheres[1].w, &sidePos, &sideNormal,
                                              0x88002, TEST_FLAGS);
        }
        if (hit) {
            if (i == 1 && m_owner->GetWork()->m_request.stance == HUMAN_STANCE_CROUCH)
                angle = normal.AngleTo(up);
            if (!(angle < 0.7853982f) && i != 0) {
                // A wall in the way.
                blocked = i;
                continue;
            }
            if (sideHit && !(sideNormal.AngleTo(up) < 0.7853982f))
                blocked = i;
            continue;
        }
        if (i > 0) {
            // Nothing in the way: is there ground to fall on?
            const bool highest = i == movement->m_stanceVolumeCount - 1;
            S_CollisionVolume *ground = CollisionSphereTest(target, highest ? S_vector(0.0f, -2.4f, 0.0f) : S_vector(0.0f, -1.0f, 0.0f),
                                                            highest ? sphere.w : sphere.w * 0.95f, &hitPos, &normal, 0x88002,
                                                            TEST_FLAGS);
            if (!ground) {
                // Over an edge.
                if (movement->m_unknown1AC == 2) {
                    movement->m_unknown1AC = 3;
                    movement->m_unknown18C = true;
                }
            } else if (highest && (normal.z + normal.x) * 0.0f + normal.y < 0.707f && hitPos.y < m_owner->GetPosition4().y) {
                // Down a slope.
                movement->m_unknown1AC = 3;
                movement->m_unknown18C = true;
            }
            if (sideHit && !(sideNormal.AngleTo(up) < 0.7853982f))
                blocked = i;
        }
    }
    movement->RestoreCollision(false);
    if (movement->m_unknown1AC == 5) {
        movement->m_unknown9D0.y = 0.0f;
        S_vector slide = XYZ(movement->m_unknown9D0);
        slide.Normalize();
        movement->m_unknown9D0.x = slide.x * 0.5f;
        movement->m_unknown9D0.y = slide.y * 0.5f;
        movement->m_unknown9D0.z = slide.z * 0.5f;
    } else if (movement->m_unknown1AC == 3) {
        // Falling over an edge or down a slope: a slide along the direction, a little up.
        movement->m_unknown9D0 = dir;
        movement->m_unknown9D0.y = 0.0f;
        S_vector slide = XYZ(movement->m_unknown9D0);
        slide.Normalize();
        movement->m_unknown9D0.x = slide.x;
        movement->m_unknown9D0.y = 0.4f;
        movement->m_unknown9D0.z = slide.z;
    }
    // (To the sides this never succeeds: a side always counts as blocked, as in the original,
    // so the side deaths are never played.)
    if (side != 0 && blocked == 0)
        blocked = 1;
    if (blocked == 0)
        return true;
    movement->m_unknown9D0.x = 0.0f;
    movement->m_unknown9D0.y = 0.0f;
    movement->m_unknown9D0.z = 0.0f;
    movement->m_unknown1AC = 2;
    movement->m_unknown18C = true;
    return false;
}

// The last death animation of a passenger turned round in the seat (not twice in a row if it
// can be helped).
int g_lastSeatDeathAnim = -1;                           // 0x0084e858

bool C_HumanSpecialMoves::Die(S_HumanAnimState *next)
{
    C_HumanMovement *movement = m_movement;
    if (movement->m_unknown1AC != 1) {
        next->anim.anim = HUMAN_ANIM_NONE;
        next->anim.Normalize();
        return false;
    }
    C_HumanWork *work = m_owner->GetWork();
    C_HumanEquipment *equipment = m_owner->GetEquipment();
    bool chosen = false;
    int reaction = 0;                                   // for the weapon pose (C_HumanWeaponPose::StartDeath)
    // The side the fatal hit came from (WORK_HIT_FROM_*); the sides become ahead or behind at
    // random, none: from behind.
    const uint32_t hitFlags = work->m_request.hitFlags;
    const uint32_t from = hitFlags & 0x3C0;
    movement->m_unknownA10 = static_cast<int>(from);
    const auto frontOrBack = []() { return std::rand() % 2 != 0 ? 0x40 : 0x80; };
    if (movement->m_unknownA10 & 0x100)
        movement->m_unknownA10 = frontOrBack();
    else if (movement->m_unknownA10 & 0x200)
        movement->m_unknownA10 = frontOrBack();
    uint32_t fallback = from;
    if (movement->m_unknownA10 == 0) {
        movement->m_unknownA10 = 0x80;
        fallback = 0x80;
    }
    const int current = movement->m_animState.anim.anim;
    if (InRange(current, 8, 0x0E) || InRange(current, 0x6A, 0x6C))
        movement->m_unknownA10 = 0x40;
    // The stance to die in (swimming: lying).
    if (movement->m_unknown9E0 == 3)
        movement->m_unknown9E0 = movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING ? HUMAN_STANCE_PRONE
                                                                                                : m_owner->GetWork()->m_request.stance;
    next->anim.stance = movement->m_unknown9E0;
    next->anim.Normalize();
    movement->UpdateDummies();
    movement->UpdateStanceVolumes(movement->m_unknown9E0 != 0);
    movement->m_unknown9B8 = MOVE_NO_TIME;
    movement->m_unknown9BC = MOVE_NO_TIME;
    movement->m_unknown9C0 = MOVE_NO_TIME;
    movement->m_unknown9C4 = MOVE_NO_TIME;
    const auto byStance = [&](int stand, int crouch, int prone) {
        const int stance = movement->m_unknown9E0;
        if (stance == HUMAN_STANCE_STAND)
            next->anim.anim = stand;
        else if (stance == HUMAN_STANCE_CROUCH)
            next->anim.anim = crouch;
        else if (stance == HUMAN_STANCE_PRONE)
            next->anim.anim = prone;
        else
            return;
        next->anim.Normalize();
    };
    const auto noSlide = [&]() {
        movement->m_unknown9D0.x = 0.0f;
        movement->m_unknown9D0.y = 0.0f;
        movement->m_unknown9D0.z = 0.0f;
    };
    if ((hitFlags & 0x400) && movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING) {
        // Drowned.
        next->anim.anim = 0x9A;
        next->anim.Normalize();
        movement->m_unknown1AC = 8;
        movement->m_unknown18C = true;
        noSlide();
    } else if (hitFlags & 0x800) {
        byStance(0x74, 0x75, 0x76);
        movement->m_unknown1AC = 2;
        movement->m_unknown18C = true;
        noSlide();
    } else if (hitFlags & 0x2000) {
        // A backstab: held in front of the killer, facing away from it.
        byStance(0x98, 0x99, 0x8D);
        reaction = 3;
        movement->m_unknown1AC = 6;
        movement->m_unknown18C = true;
        noSlide();
        const S_vector4 &pos = m_owner->GetPosition4();
        const C_Actor *killer = work->m_request.killer;
        const S_vector4 killerPos = killer ? killer->GetPosition4() : pos;
        const S_vector away = (XYZ(pos) - XYZ(killerPos)).Normalized();
        work->SetFacingDir(away, true);
        movement->m_stanceVolumeScale = 0.45f;
        movement->m_mixer.MoveTo({away.x * 0.18f + killerPos.x, away.y * 0.18f + killerPos.y, away.z * 0.18f + killerPos.z, pos.w},
                                 4.0f, 2000);
        work->SetLookAt({0.0f, 0.0f, 0.0f, 0.0f}, true);
    } else if (hitFlags & 0x1000) {
        byStance(0x80, 0x8A, 0x8D);
        reaction = 4;
        movement->m_unknown1AC = 2;
        movement->m_unknown18C = true;
        noSlide();
    } else {
        byStance(0x80, 0x8A, 0x8D);
        if (movement->m_seatMode == 4) {
            movement->m_unknown18C = true;
            reaction = 2;
            movement->m_unknown1AC = 2;
        } else if (movement->m_animState.anim.anim == 0x62) {
            // Tied up.
            next->anim.anim = 0x71;
            next->anim.Normalize();
            reaction = 3;
            movement->m_unknown1AC = 2;
            movement->m_unknown18C = true;
        } else {
            // A fall with room for it: away from the hit (another way if blocked; three tries).
            for (int tries = 3; tries > 0 && !chosen; --tries) {
                S_vector4 dir = m_owner->GetDirection4();
                const int stance = movement->m_unknown9E0;
                if (stance == HUMAN_STANCE_STAND) {
                    const C_HumanWork *ownerWork = movement->m_owner->GetWork();
                    const int anim = movement->m_animState.anim.anim;
                    if (ownerWork->m_mode == 2 || ownerWork->m_mode == 3 || anim == 0x9F || anim == 0x9E || anim == 0x9B ||
                        anim == 0x6F || anim == 0x22) {
                        // Sitting.
                        movement->m_unknown1AC = 7;
                        movement->m_unknown18C = true;
                        if (m_owner->GetWork()->m_mode == 2)
                            next->anim.anim = (g_deathSeed & 1) + 0x92;
                        else if (movement->m_seatMode < 1)
                            next->anim.anim = 0x94;
                        else if (movement->m_animState.anim.anim == 0x9B || movement->m_animState.anim.anim == 0x6F)
                            next->anim.anim = (g_deathSeed & 1) + 0x72;
                        else
                            next->anim.anim = g_deathSeed % 3 + 0x95;
                        next->anim.Normalize();
                        chosen = true;
                    } else {
                        switch (movement->m_unknownA10) {
                        case 0x100: {
                            S_vector side = XYZ(dir);
                            RotateAboutY(side, -HALF_PI);
                            dir = {side.x, side.y, side.z, dir.w};
                            if (HasRoomToFall(dir, 2)) {
                                next->anim.anim = 0x82;
                                next->anim.Normalize();
                                chosen = true;
                            } else {
                                movement->m_unknownA10 = static_cast<int>(fallback & 0xC0);
                            }
                            break;
                        }
                        case 0x40:
                            if (HasRoomToFall({-dir.x, -dir.y, -dir.z, dir.w}, 0)) {
                                next->anim.anim = (g_deathSeed & 3) + 0x7C;
                                next->anim.Normalize();
                                chosen = true;
                            } else {
                                movement->m_unknownA10 = 0x80;
                            }
                            break;
                        case 0x80:
                            if (HasRoomToFall(dir, 0)) {
                                next->anim.anim = g_deathSeed % 5 + 0x77;
                                next->anim.Normalize();
                                chosen = true;
                            } else {
                                movement->m_unknownA10 = 0x40;
                            }
                            break;
                        case 0x200: {
                            S_vector side = XYZ(dir);
                            RotateAboutY(side, HALF_PI);
                            dir = {side.x, side.y, side.z, dir.w};
                            if (HasRoomToFall(dir, 1)) {
                                next->anim.anim = 0x81;
                                next->anim.Normalize();
                                chosen = true;
                            } else {
                                movement->m_unknownA10 = static_cast<int>(fallback & 0xC0);
                            }
                            break;
                        }
                        default:
                            break;
                        }
                    }
                } else if (stance == HUMAN_STANCE_CROUCH) {
                    if (movement->m_animState.anim.anim == 0x62) {
                        next->anim.anim = 0x71;
                        next->anim.Normalize();
                        chosen = true;
                    } else {
                        switch (movement->m_unknownA10) {
                        case 0x100: {
                            S_vector side = XYZ(dir);
                            RotateAboutY(side, -HALF_PI);
                            dir = {side.x, side.y, side.z, dir.w};
                            if (HasRoomToFall(dir, 2)) {
                                next->anim.anim = 0x8C;
                                next->anim.Normalize();
                                chosen = true;
                            } else {
                                movement->m_unknownA10 = static_cast<int>(fallback & 0xC0);
                            }
                            break;
                        }
                        case 0x40:
                            if (HasRoomToFall({-dir.x, -dir.y, -dir.z, dir.w}, 0)) {
                                next->anim.anim = g_deathSeed % 3 + 0x87;
                                next->anim.Normalize();
                                chosen = true;
                            } else {
                                movement->m_unknownA10 = 0x80;
                            }
                            break;
                        case 0x80:
                            if (HasRoomToFall(dir, 0)) {
                                next->anim.anim = (g_deathSeed & 3) + 0x83;
                                next->anim.Normalize();
                                chosen = true;
                            } else {
                                movement->m_unknownA10 = 0x40;
                            }
                            break;
                        case 0x200: {
                            S_vector side = XYZ(dir);
                            RotateAboutY(side, HALF_PI);
                            dir = {side.x, side.y, side.z, dir.w};
                            if (HasRoomToFall(dir, 1)) {
                                next->anim.anim = 0x8B;
                                next->anim.Normalize();
                                chosen = true;
                            } else {
                                movement->m_unknownA10 = static_cast<int>(fallback & 0xC0);
                            }
                            break;
                        }
                        default:
                            break;
                        }
                    }
                } else if (stance == HUMAN_STANCE_PRONE) {
                    next->anim.anim = g_deathSeed % 5 + 0x8D;
                    next->anim.Normalize();
                    chosen = true;
                    movement->m_unknown1AC = 2;
                    movement->m_unknown18C = true;
                }
            }
        }
    }
    ++g_deathSeed;
    // The trail spheres go.
    for (int i = 0; i < movement->m_trailCount; ++i)
        g_collisionWorld.RemoveVolume(movement->m_trailVolumes[i]);
    movement->m_trailCount = 0;
    movement->m_trailTime = 0;

    const int seatMode = movement->m_seatMode;
    if (seatMode < 6 || seatMode > 8 || movement->m_animState.anim.anim == 0x9F) {
        const int anim = movement->m_animState.anim.anim;
        if (seatMode == 4 && InRange(anim, 0x37, 0x3E) && movement->m_vehicle &&
            (movement->m_vehicle->GetSeatFlags(movement->m_seat) & SEAT_FLAG_200)) {
            // Crouching in such a seat: turned round, falling forward.
            I3D_frame *model = m_owner->GetFrame();
            const S_matrix &m = model->GetMatrix();
            const S_vector facing = S_vector(m.m[2][0], m.m[2][1], m.m[2][2]).Normalized();
            model->SetDir(S_vector(-facing.x, facing.y, -facing.z), 0.0f);
            if (std::rand() % 3 != 0) {
                next->anim.anim = g_deathSeed % 3 + 0x83;
                next->anim.Normalize();
            }
            if (next->anim.anim == g_lastSeatDeathAnim && std::rand() % 3 != 0) {
                next->anim.anim = g_deathSeed % 3 + 0x83;
                next->anim.Normalize();
            }
            if (movement->m_seat == 3) {
                if (next->anim.anim == 0x84) {
                    next->anim.anim = 0x8A;
                    next->anim.Normalize();
                }
            } else if (movement->m_seat == 2 && next->anim.anim == 0x83) {
                next->anim.anim = 0x8A;
                next->anim.Normalize();
            }
            g_lastSeatDeathAnim = next->anim.anim;
        }
    } else {
        // At a seat weapon: out of the seat.
        movement->m_vehicle->LeaveSeat(movement->m_seat);
        m_owner->ClearVehicle();
        movement->m_hitVolumeScale[0] = 1.0f;
        movement->m_hitVolumeScale[1] = 1.0f;
    }
    movement->StopUseSound();
    movement->SetPushState(movement->m_unknown1AC, true);
    for (int seat = 0; seat < 5; ++seat)
        movement->SetSeatMode(seat, false);
    // The work takes nothing but the death any more.
    work->m_blocked2 = 0;
    work->m_blocked = 0;
    work->m_blockedTimer = 0;
    work->m_blocked |= WORK_REQUEST_ANIM | WORK_REQUEST_ACTIVITY | 0x4 | WORK_REQUEST_GAIT | WORK_REQUEST_LOOK_AT |
                       WORK_REQUEST_ACTION | WORK_REQUEST_UNKNOWN5C | WORK_REQUEST_FACING | WORK_REQUEST_LOOK;
    equipment->SetLongGunLift(2);
    m_animPlayer->m_animDelay = MOVE_NO_TIME;
    m_animPlayer->m_animDelayState = 0;
    m_animPlayer->m_carryDelay = MOVE_NO_TIME;
    m_animPlayer->m_carryDelayState = 0;
    m_animPlayer->m_activityDelay = MOVE_NO_TIME;
    m_animPlayer->m_activityDelayState = 0;
    movement->m_mixer.FadeOutTrack(1, 350);
    movement->m_mixer.FadeOutTrack(2, 350);
    if (movement->m_unknown1AC == 4 || movement->m_unknown1AC == 6) {
        const S_vector4 none = {0.0f, 0.0f, 0.0f, 0.0f};
        movement->m_mixer.AddMove(none, 0);
        movement->m_mixer.AddMove2(none, 0);
        movement->m_mixer.Slide(none, 0);
    }
    movement->m_animState.carryAnim = 0x5C;
    equipment->OnAnimationEvent(2);
    equipment->SetFlashlight(false);
    movement->m_morph.SetExhausted(false, true);
    m_locomotion->StopSwimSound(true);
    if (movement->m_useObject)
        RemoveLadderClimber(movement->m_useObject, m_owner);
    // The collision volumes of the body (as a dead body).
    for (S_CollisionVolume *volume : movement->m_stanceVolumes) {
        volume->flags = 0x42;
        volume->type = 1;
    }
    movement->UpdateHitSpheres();
    for (S_CollisionVolume *volume : movement->m_hitVolumes) {
        volume->flags = 0x42;
        volume->type = 0x10002;
    }
    // Falling over an edge / down a slope: the slide (faster for an edge).
    const S_vector4 &slide = movement->m_unknown9D0;
    if (slide.x * slide.x + slide.y * slide.y + slide.z * slide.z >= 1e-16f) {
        if (movement->m_unknown1AC == 3)
            movement->m_mixer.Slide({slide.x * 4.0f, slide.y * 4.0f, slide.z * 4.0f, 0.0f}, 1000);
        else
            movement->m_mixer.Slide(slide, 1000);
    }
    m_animPlayer->PlayAnimation("", 1, 1, 0);
    movement->m_mixer.UpdateStages(100, 100, false, nullptr);
    if (movement->m_unknownA30 == 0 || movement->m_unknownA30 == MOVE_NO_TIME) {
        equipment->SetFlashlight(false);
        equipment->OnAnimationEvent(10);
    } else {
        // A carried body falls too.
        I3D_frame *body = equipment->GetModel(9);
        movement->OnDropBody(body && body->GetData() ? static_cast<C_Human *>(body->GetData()->actor) : nullptr);
    }
    movement->m_unknown9D0.x = -10000.0f;
    // The death animation's set for the weapon pose.
    I3D_animation_set *deathAnim = nullptr;
    const int group = movement->m_owner->GetWork()->GetGroup();
    const int index = next->anim.anim * 3 + m_owner->GetWork()->m_request.stance;
    if (group == 1)
        deathAnim = g_humanAnimTable->GetSlot(index).anim;
    else if (group == 2)
        deathAnim = g_humanAnimTable2->GetSlot(index).anim;
    if (movement->m_unknown1AC >= 7) {
        // Sitting: no physics.
        movement->m_unknown1AC = 0;
        movement->m_unknown18C = true;
        movement->m_morph.ShowDeathFace();
        reaction = 2;
    } else {
        if (movement->m_seatMode == 0 ||
            (movement->m_seatMode >= 6 && movement->m_seatMode <= 8 && movement->m_animState.anim.anim != 0x9F))
            m_owner->GetFrame()->LinkTo(nullptr, 1);
        I3D_frame *model = m_owner->GetFrame();
        I3D_scene *scene = GetGame()->GetScene();
        scene->UpdateFrameSector(model, model->GetWorldPos());
        GetGame()->GetScene()->MarkTrackedModel(m_owner->GetFrame());
    }
    const S_vector4 force = {work->m_request.hitForce.x, work->m_request.hitForce.y, work->m_request.hitForce.z,
                             *reinterpret_cast<const float *>(work->m_request.unknown7C)};
    movement->m_weaponPose2->StartDeath(work->m_request.hitFlags, &force.x, deathAnim, reaction, nullptr);
    return true;
}

// ------------------------------------------------------------------ stance changes

namespace {

// Where the head and the feet of a lying body are (from the pelvis) for a body facing
// LYING_REFERENCE (x / z), set by the static initialisers 0x0074a110..0x0074a170.
const S_vector LYING_HEAD(-0.00422000000f, -0.0558560006f, 0.590300024f);   // 0x009831c0
const S_vector LYING_FEET(0.0462899990f, -0.407458007f, -0.525900006f);     // 0x009831b0
const float LYING_REFERENCE[2] = {0.0199986994f, -0.999800026f};            // 0x009831d0 (x, z)

// The head (0) or feet (1) offset of a lying body turned to `facing`.
S_vector LyingOffset(int which, const S_vector4 &facing)   // 0x0074a1c0
{
    S_vector offset = which == 0 ? LYING_HEAD : LYING_FEET;
    const float flat[2] = {facing.x, facing.z};
    RotateAboutY(offset, AngleBetween2D(LYING_REFERENCE, flat));
    return offset;
}

}  // namespace

bool C_HumanSpecialMoves::HasRoomForStance(int mode)
{
    if (!IsLocalGame() && !m_owner->IsLocalPlayer() && !m_owner->IsServerSimulated())
        return true;
    C_HumanMovement *movement = m_movement;
    // A step back to the stance for the replicas while a special carry animation plays.
    const auto refuse = [&](int stance) {
        S_RemoteAction step;
        step.time = 250;
        step.action = stance;
        step.phase = 0;
        movement->m_remoteActions.push_back(step);
        return false;
    };
    // Moves the body by `dir` x `steps` (the locomotion waits for the move).
    const auto shift = [&](const S_vector &dir, int steps, int time) {
        m_locomotion->m_shiftTime = time;
        const float k = static_cast<float>(steps);
        movement->m_mixer.AddMove2({dir.x * k, dir.y * k, dir.z * k, 0.0f}, m_locomotion->m_shiftTime);
        m_locomotion->m_shiftStance = 2;
    };
    const S_vector up(0.0f, 1.0f, 0.0f);
    S_vector hitPos, normal;
    switch (mode) {
    case 0:
    case 1: {
        // Lying down (from standing / crouching): room for the head and the feet, the body
        // shifted forward or back in steps when needed (moving: along the way it goes).
        const int carry = movement->m_animState.carryAnim;
        if (InRange(carry, 0x50, 0x52))
            return refuse(REMOTE_ACTION_PRONE);
        int steps = 4;
        const bool moving = movement->m_animState.anim.anim > 0xBD;
        movement->UpdateStanceVolumes(mode != 0);
        const S_vector4 &facing = m_owner->GetDirection4();
        S_vector dir = S_vector(facing.x, 0.0f, facing.z).Normalized();
        bool scaled = false;
        if (moving) {
            const float speed = m_owner->GetVelocity().Square();
            if (speed < 1.0f) {
                dir *= 0.25f;
                steps = 2;
                scaled = true;
            } else if (speed < 2.0f) {
                steps = 2;
            } else if (speed < 4.0f) {
                steps = 3;
            } else if (speed >= 9.0f) {
                dir *= 0.454545438f;
                scaled = true;
            }
        }
        if (!scaled)
            dir *= 0.333333343f;
        const S_vector feet = LyingOffset(1, m_owner->GetDirection4()) * 2.0f;
        const S_vector head = LyingOffset(0, m_owner->GetDirection4()) * 2.0f;
        int state = 0;                                  // 1: the feet blocked, 2: turned round
        int i = -1;
        float feetUp = 0.0f;
        float headUp = 0.0f;
        for (;;) {
            const int previous = i;
            ++i;
            if (previous != -1) {
                if (!moving && state == 0)
                    break;
                if (i >= steps) {
                    if (state != 0)
                        return false;
                    break;
                }
            }
            const float k = static_cast<float>(i);
            const S_vector4 &s1 = movement->m_stanceSpheres[1];
            const S_vector4 &s0 = movement->m_stanceSpheres[0];
            movement->RemoveCollision(false);
            const S_vector from1 = moving ? XYZ(s1) + dir * k : XYZ(s1) - dir * k;
            S_vector normalFeet, normalHead;
            S_CollisionVolume *hitFeet = CollisionSphereTest(from1, feet, s1.w * 0.95f, &hitPos, &normalFeet, TEST_TYPES, TEST_FLAGS);
            const S_vector from0 = moving ? XYZ(s0) + dir * k : XYZ(s0) - dir * k;
            S_CollisionVolume *hitHead = CollisionSphereTest(from0, head, s0.w * 0.95f, &hitPos, &normalHead, TEST_TYPES, TEST_FLAGS);
            movement->RestoreCollision(false);
            if (hitFeet)
                feetUp = (normalFeet.z + normalFeet.x) * 0.0f + normalFeet.y;
            if (hitHead)
                headUp = (normalHead.z + normalHead.x) * 0.0f + normalHead.y;
            if (hitFeet && feetUp <= 0.707f) {
                if (state == 0)
                    state = 1;
                else if (state == 2)
                    return false;
                if (moving)
                    return false;
            } else if (state == 1) {
                state = 0;
            }
            if (hitHead && headUp <= 0.707f) {
                if (i == 0 && state == 0) {
                    // Blocked at the head at once: the other way.
                    dir = -dir;
                    state = 2;
                } else if (state == 1) {
                    return false;
                }
                if (moving)
                    return false;
            } else if (state == 2) {
                state = 0;
            }
            if (!hitFeet && !hitHead)
                state = 0;
        }
        if (i <= 1) {
            m_locomotion->m_shiftTime = MOVE_NO_TIME;
            return state == 0;
        }
        shift(dir, 1 - i, mode == 0 ? MoveTime(500.0f) : MoveTime(400.0f));
        return state == 0;
    }

    case 2: {
        // Standing up: room above the highest sphere for the standing height.
        movement->UpdateStanceVolumes(m_owner->GetWork()->m_request.stance);
        const float bodyY = m_owner->GetPosition4().y;
        const S_vector4 &top = movement->m_stanceSpheres[movement->m_stanceVolumeCount - 1];
        const S_vector rise(0.0f, top.w * 0.0500000119f + (movement->m_unknown740.y - (top.y - bodyY)), 0.0f);
        movement->RemoveCollision(false);
        S_CollisionVolume *hit = CollisionSphereTest(XYZ(top), rise, top.w * 0.95f, &hitPos, &normal, 0xCC002, TEST_FLAGS);
        movement->RestoreCollision(false);
        return hit == nullptr;
    }

    case 3: {
        // Standing at a wall: a step back from it.
        movement->UpdateStanceVolumes(HUMAN_STANCE_STAND);
        movement->RemoveCollision(false);
        const S_vector4 &lowest = movement->m_stanceSpheres[0];
        const S_vector reach = XYZ(m_owner->GetDirection4()) * (lowest.w + 0.1f);
        S_CollisionVolume *hit = CollisionSphereTest(XYZ(lowest), reach, movement->m_stanceSpheres[movement->m_stanceVolumeCount - 1].w * 0.8f,
                                                     &hitPos, &normal, 0xCC002, TEST_FLAGS);
        movement->RestoreCollision(false);
        if (hit && std::fabs((normal.z + normal.x) * 0.0f + normal.y) < 0.8f) {
            const S_vector4 &facing = m_owner->GetDirection4();
            movement->m_mixer.AddMove2({-facing.x * 0.15f, -facing.y * 0.15f, -facing.z * 0.15f, 0.0f}, 300);
        }
        return true;
    }

    case 4:
    case 5: {
        // Getting up from lying (to standing / crouching): room for the body's ends where it
        // lies (shifted back in steps when needed) and above the body.
        const int carry = movement->m_animState.carryAnim;
        if (InRange(carry, 0x50, 0x52) || carry == 0x23 || carry == 0x24)
            return refuse(mode == 5 ? REMOTE_ACTION_CROUCH : REMOTE_ACTION_STAND);
        movement->UpdateStanceVolumes(HUMAN_STANCE_PRONE);
        const S_vector4 &facing = m_owner->GetDirection4();
        S_vector dir = S_vector(facing.x, 0.0f, facing.z).Normalized() * 0.333333343f;
        const S_vector feet = -LyingOffset(1, m_owner->GetDirection4()) * 1.4f;
        const S_vector head = -LyingOffset(0, m_owner->GetDirection4()) * 1.4f;
        int state = 0;
        int i = -1;
        for (;;) {
            const int previous = i;
            ++i;
            if (previous != -1) {
                if (state == 0)
                    break;
                if (i >= 3)
                    return state == 0;
            }
            int blocked = 0;
            const float k = static_cast<float>(i);
            const S_vector4 &s3 = movement->m_stanceSpheres[3];
            const S_vector4 &s0 = movement->m_stanceSpheres[0];
            movement->RemoveCollision(false);
            S_vector normalFeet, normalHead;
            S_CollisionVolume *hitFeet = CollisionSphereTest(XYZ(s3) - dir * k, feet, s3.w * 1.1f, &hitPos, &normalFeet, 0xC9002, TEST_FLAGS);
            S_CollisionVolume *hitHead = CollisionSphereTest(XYZ(s0) - dir * k, head, s0.w * 1.1f, &hitPos, &normalHead, 0xC9002, TEST_FLAGS);
            movement->RestoreCollision(false);
            if (hitFeet && !(normalFeet.AngleTo(up) < 1.0471976f)) {
                if (i == 0)
                    state = 1;
                else
                    blocked = 1;
            } else if (state == 1) {
                state = 0;
            }
            if (hitHead && !(normalHead.AngleTo(up) < 1.0471976f)) {
                if (state == 0 && i == 0) {
                    dir = -dir;
                    state = 2;
                } else {
                    blocked = 2;
                    if (state == 0)
                        state = blocked;
                }
            } else if (state == 2 || state == 0) {
                state = blocked;
            }
            if (!hitFeet && !hitHead)
                state = 0;
        }
        // Room above to get up (1.5 m for standing, 1 m for crouching).
        const S_vector4 &pos = m_owner->GetPosition4();
        S_vector from(pos.x, pos.y + 0.05f, pos.z);
        if (i > 1)
            from += dir * static_cast<float>(i - 1);
        movement->RemoveCollision(false);
        S_CollisionVolume *hit = CollisionSphereTest(from, S_vector(0.0f, mode == 4 ? 1.5f : 1.0f, 0.0f), 0.25f, &hitPos, &normal, 0xC9002,
                                                     TEST_FLAGS);
        movement->RestoreCollision(false);
        if (hit)
            return false;
        if (i <= 1) {
            m_locomotion->m_shiftTime = MOVE_NO_TIME;
            return state == 0;
        }
        // (The same time for both, as in the original.)
        shift(dir, i - 1, MoveTime(100.0f));
        return state == 0;
    }

    default:
        return true;
    }
}
