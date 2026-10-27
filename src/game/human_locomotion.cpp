// The locomotion of a human's body (C_HumanLocomotion, C_HumanMovement +0xAC0; original source
// 0x00763950..0x007724d0, the contact helpers at 0x00788a60..0x0078a020): each update moves the
// body by what the animation mixer moved it, swept through the collision world with the stance
// spheres (C_CollisionWorld::MoveSpheres*): walking with steps up and slides down slopes, falls
// with their damage and landing animations, the climbing moves led by the head dummy, ladders,
// jumps, swimming, the moves of a dying and dead body; it turns the body towards the work's
// facing direction, sets the root motion speed of the animation playing and keeps the collision
// volumes, the dummies and the frame in step with the logical position.
//
// The body's position (C_Actor::m_position) is the logical one; the frame follows it smoothed
// (C_HumanVisual). The original copies whole S_vector4s (the w of positions included, often
// garbage); the port keeps the owner's w where the original wrote an undefined one.
//
// (0x0076d350, a blend of two vectors, 0x00788b60 and 0x00789220, tests of the contacts of a
// move, and 0x007893a0, a model blending debug helper, have no callers and are not ported;
// 0x00771540 / 0x007724f0 are static initialisers.)
#include "game/human_movement.h"

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <windows.h>

#include "common/chunk.h"
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
#include "game/physics.h"
#include "game/world.h"
#include "ls3d/i3d.h"

static_assert(sizeof(C_HumanLocomotion) == 0x140, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_movement) == 0x40, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_directionChanged) == 0x50, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_push) == 0x70, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_turning) == 0x88, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_velocity) == 0xB0, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_shiftTime) == 0xD0, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_fallSpeed) == 0xF4, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_waterDepth) == 0x108, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_swimSound) == 0x11C, "C_HumanLocomotion layout");
static_assert(offsetof(C_HumanLocomotion, m_steadyTurnTime) == 0x130, "C_HumanLocomotion layout");
static_assert(sizeof(S_MoveContacts) == 0x1F4, "S_MoveContacts layout");
static_assert(offsetof(S_MoveContacts, move) == 0xD0, "S_MoveContacts layout");
static_assert(offsetof(S_MoveContacts, pos) == 0xF0, "S_MoveContacts layout");
static_assert(offsetof(S_MoveContacts, sphere) == 0x1A0, "S_MoveContacts layout");
static_assert(offsetof(S_MoveContacts, count) == 0x1F0, "S_MoveContacts layout");

namespace {

const float HALF_PI = 1.57079637f;                      // 0x3fc90fdb
const double HALF_PI_D = 1.5707963705062866;            // 0x008166f0 ((double)(float)(pi / 2))
const float TWO_OVER_PI = 0.636619747f;                 // 0x00810f4c

// The collision filters of the body's tests (excluded volume types / flags; the humans other
// than the player's squad also skip the static geometry flagged 0x800000 / 0xA30000).
constexpr uint32_t TEST_TYPES = 0xC8002;
constexpr uint32_t TEST_FLAGS = 0x230000;
constexpr uint32_t TEST_FLAGS_OTHERS = 0xA30000;
constexpr uint32_t GROUND_RAY_TYPES = 0x8C8102;

// The turn limits of the turn in place (radians): standing, crouching / lying.
const float TURN_LIMIT_STAND = 1.22173047f;             // 0x00816768 (70 degrees)
const float TURN_LIMIT_LOW = 1.04719758f;               // 0x0081676c (60 degrees)
// The steady turn speeds (rad/ms): the local player, the others.
const float TURN_SPEED_PLAYER = 0.006f;                 // 0x0084ea00
const float TURN_SPEED_OTHERS = 0.008f;                 // 0x0084ea04

// Falls: the acceleration step (the speed rises by a third of g every 85 ms).
const float GRAVITY = 9.81f;                            // 0x00816778
const float FALL_STEP_MS = 85.0f;                       // 0x00816c8c


// Animations (E_HumanAnim; names from Tables\anim.sav).
constexpr int ANIM_LADDER_TOP_OFF = 0x08;               // "slezZEBda"
constexpr int ANIM_LADDER_TOP_ON = 0x0E;                // "zZEBda"
constexpr int ANIM_FALLING = 0x11;                      // "vPAD01"
constexpr int ANIM_JUMP_12 = 0x12;
constexpr int ANIM_LAND_HIGH = 0x14;                    // "doskKLa" (standing, from high up)
constexpr int ANIM_LAND_CROUCHED = 0x15;                // "doskKLa"
constexpr int ANIM_LAND_LOW = 0x16;                     // "doskSTb"
constexpr int ANIM_JUMP_CROUCHED = 0x17;                // "skokDRc"
constexpr int ANIM_JUMP = 0x18;                         // "jump"
constexpr int ANIM_STAND_TO_CROUCH = 0x1A;              // "st-dr3"
constexpr int ANIM_CROUCH_TO_STAND = 0x1B;              // "dr-st3"
constexpr int ANIM_CROUCH_TO_LIE = 0x1E;                // "dr-leR"
constexpr int ANIM_LIE_TO_CROUCH = 0x1F;                // "le-drR"
constexpr int ANIM_STAND_TO_LIE = 0x20;                 // "st-leR"
constexpr int ANIM_LIE_TO_STAND = 0x21;                 // "le-st3"
constexpr int ANIM_TURN_RIGHT = 0x2B;                   // "otocP"
constexpr int ANIM_TURN_LEFT = 0x2C;                    // "otocL"
constexpr int ANIM_STAND = 0x2D;
constexpr int ANIM_CROUCH = 0x37;
constexpr int ANIM_SQUAT = 0x38;                        // "drep1"
constexpr int ANIM_LIE = 0x3F;
constexpr int ANIM_SWIM = 0x5A;                         // "plavani3"
constexpr int ANIM_SWIM_STILL = 0x5B;                   // "plavST"
constexpr int ANIM_DIVE = 0x5C;                         // "potapec" (diving gear)
constexpr int ANIM_DIVE_STILL = 0x5D;                   // "potapecM"
constexpr int ANIM_LADDER_UP = 0x6A;                    // "LEZUP2da"
constexpr int ANIM_LADDER_DOWN = 0x6B;                  // "LEZDWN1da"
constexpr int ANIM_LADDER_SLIDE = 0x6C;                 // "rychleZEBD"
constexpr int ANIM_WALK = 0xBE;                         // the moving animations 0xBE..0xCD
constexpr int ANIM_WALK_BACK = 0xBF;
constexpr int ANIM_WALK_BACK_2 = 0xC7;
constexpr int ANIM_CLIMB_OVER = 0x0A;                   // "nakrocV6a"
constexpr int ANIM_LADDER_IDLE = 0xCE;

bool InRange(int value, int low, int high) { return value >= low && value <= high; }
// The climbing moves led by the head dummy (onto / off ladders, over obstacles).
bool IsClimbing(int anim) { return InRange(anim, ANIM_LADDER_TOP_OFF, ANIM_LADDER_TOP_ON); }
bool IsOnLadder(int anim) { return InRange(anim, ANIM_LADDER_UP, ANIM_LADDER_SLIDE); }
bool IsInPlace(int anim) { return InRange(anim, 0x29, 0x41); }
bool IsLean(int anim) { return InRange(anim, 0xA2, 0xA5); }

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }
S_vector4 ToVector4(const S_vector &v, float w) { return {v.x, v.y, v.z, w}; }
float LengthSquared(const S_vector4 &v) { return v.x * v.x + v.y * v.y + v.z * v.z; }
// The SDK's normalisation of (x, 0, z) (a horizontal unit vector; up when zero).
S_vector Flat(const S_vector &v) { return S_vector(v.x, 0.0f, v.z).Normalized(); }

// The last contact normal of a move (the end position itself without contacts, as the
// original indexes it).
const S_vector4 &LastContactNormal(const S_MoveContacts &contacts)
{
    return contacts.count == 0 ? contacts.pos : contacts.normals[contacts.count - 1];
}

// The simulation of this body runs here (the replicas of a network game are moved by the
// network messages).
bool IsSimulatedHere(const C_Human *owner)
{
    return IsLocalGame() || owner->IsLocalPlayer() || owner->IsServerSimulated();
}

// The ground normal of the last ground ray of SweepBody (up when none).
S_vector4 g_groundNormal = {0.0f, 1.0f, 0.0f, 0.0f};    // 0x00983340
// Stands for the ground where the cheap ground mode found none (a disabled volume of no
// material; the original keeps only its flags word there).
S_CollisionVolume g_assumedGround;                      // 0x00983350
// The view of the active camera (tan(fov / 2) / aspect) and the mission time it was taken at.
uint32_t g_detailTime = 0;                              // 0x009833b0
float g_detailTan = 0.0f;                               // 0x009833b4

// The speed of a fall after `time` ms (`speed` m/s, `stepTime` ms into the current 85 ms
// step); `parachute`: the speed decays (40 % per 100 ms) to 5 m/s. (__fastcall.)
void AccelerateFall(int time, float *speed, float *stepTime, bool parachute)   // 0x00789f60
{
    if (parachute) {
        *stepTime = 0.0f;
        *speed = *speed - *speed * 0.4f * (static_cast<float>(time) * 0.01f);
        if (*speed < 5.0f)
            *speed = 5.0f;
        return;
    }
    if (time <= 0)
        return;
    while (static_cast<float>(time) + *stepTime > FALL_STEP_MS) {
        *speed += GRAVITY * 0.333333343f;
        time = static_cast<int>(static_cast<float>(time) - (FALL_STEP_MS - *stepTime));
        *stepTime = 0.0f;
        if (time <= 0)
            return;
    }
    *stepTime += static_cast<float>(time);
}

// How far a fall goes in `time` ms (as AccelerateFall steps the speed; nothing changed).
float FallDistance(int time, float speed, float stepTime)   // 0x0078a020
{
    float distance = 0.0f;
    if (time <= 0)
        return distance;
    while (stepTime + static_cast<float>(time) > FALL_STEP_MS) {
        const float rest = FALL_STEP_MS - stepTime;
        distance += 0.001f * rest * speed;
        speed += GRAVITY * 0.333333343f;
        time = static_cast<int>(static_cast<float>(time) - rest);
        stepTime = 0.0f;
        if (time <= 0)
            return distance;
    }
    return distance + static_cast<float>(time) * 0.001f * speed;
}

// The contact of a move the body stands on: the latest of the lowest sphere's (`anySphere`:
// any sphere's, skipping a ceiling after a floor) whose normal points up; `normal` gets it
// and `volume` what was touched there when it is a floor or a ceiling. True for a floor or
// ceiling, false for a wall. (__fastcall.)
bool FindGroundContact(const S_MoveContacts &contacts, bool anySphere, S_vector4 *normal, S_CollisionVolume **volume)   // 0x00788a60
{
    *volume = nullptr;
    normal->x = normal->y = normal->z = 0.0f;
    bool standing = false;
    for (int i = contacts.count - 1; i >= 0; --i) {
        const float up = contacts.normals[i].y;
        if (!anySphere && contacts.sphere[i] != 0)
            continue;
        if (anySphere && i != 0 && contacts.normals[i - 1].y >= 0.0f && up <= 0.0f)
            continue;
        if (up < -0.3f || up > 0.6f) {
            standing = true;
            *volume = contacts.volumes[i];
        } else {
            standing = false;
        }
        *normal = contacts.normals[i];
        if (normal->y > 0.0f)
            return standing;
    }
    return standing;
}

// The roll of the body (C_HumanMovement::m_unknown250) back to level at 1 rad/s.
void RelaxRoll(C_HumanMovement *movement, int time)
{
    if (movement->m_unknown250 == 0.0f)
        return;
    float turn = -movement->m_unknown250;
    if (std::fabs(turn) < 0.05f) {
        movement->m_unknown250 = 0.0f;
        return;
    }
    const float most = static_cast<float>(time) * 0.001f;
    if (most < std::fabs(turn))
        turn = (turn <= 0.0f ? -1.0f : 1.0f) * most;
    movement->m_unknown250 += turn;
}

// The body's volumes as the hit spheres (and the stance spheres) are now.
void MoveHitVolumes(C_HumanMovement *movement)
{
    for (int i = 0; i < movement->m_hitVolumeCount; ++i) {
        S_CollisionVolume *volume = movement->m_hitVolumes[i];
        const S_vector4 &sphere = movement->m_hitSpheres[i];
        volume->center = XYZ(sphere);
        volume->centerW = sphere.w;
        volume->radius = (i == 0 ? movement->m_hitVolumeScale[0] : movement->m_hitVolumeScale[1]) * sphere.w;
        g_collisionWorld.MoveVolume(volume);
    }
}

// An argument of the actor collision callbacks (the original passes pointers as ints).
int PointerArg(const void *p) { return static_cast<int>(reinterpret_cast<intptr_t>(p)); }

}  // namespace

float g_moveIterations = 10.0f;                         // 0x008441e4

const float g_moveSpeeds[13][18] = {
    {0.63f, 1.3f, 2.8f, 0.781f, 0.457f, 4.1f, 0.32f, 0.65f, 2.0f, 0.32f, 0.65f, 2.0f, 0.2f, 0.476f, 0.8f, 0.2f, 0.476f, 0.8f},
    {0.63f, 0.85f, 0.85f, 0.536f, 0.85f, 0.85f, 0.29f, 0.62f, 0.62f, 0.29f, 0.62f, 0.62f, 0.2f, 0.3f, 0.3f, 0.2f, 0.3f, 0.3f},
    {0.6f, 1.3f, 2.5f, 0.568f, 1.3f, 2.5f, 0.33f, 0.72f, 1.8f, 0.33f, 0.72f, 1.8f, 0.2f, 0.3f, 0.55f, 0.2f, 0.3f, 0.55f},
    {0.6f, 1.3f, 2.5f, 0.568f, 1.3f, 2.5f, 0.33f, 0.72f, 1.8f, 0.33f, 0.72f, 1.8f, 0.2f, 0.3f, 0.55f, 0.2f, 0.3f, 0.55f},
    {0.63f, 1.3f, 2.8f, 0.781f, 0.457f, 4.1f, 0.32f, 0.65f, 2.0f, 0.32f, 0.65f, 2.0f, 0.2f, 0.476f, 0.8f, 0.2f, 0.476f, 0.8f},
    {0.63f, 1.3f, 2.8f, 0.781f, 0.457f, 4.1f, 0.32f, 0.65f, 2.0f, 0.32f, 0.65f, 2.0f, 0.2f, 0.476f, 0.8f, 0.2f, 0.476f, 0.8f},
    {0.673f, 0.85f, 0.85f, 0.536f, 0.85f, 0.85f, 0.29f, 0.62f, 0.62f, 0.29f, 0.62f, 0.62f, 0.2f, 0.3f, 0.3f, 0.2f, 0.3f, 0.3f},
    {0.673f, 0.85f, 0.85f, 0.536f, 0.85f, 0.85f, 0.29f, 0.62f, 0.62f, 0.29f, 0.62f, 0.62f, 0.2f, 0.3f, 0.3f, 0.2f, 0.3f, 0.3f},
    {0.0f, 1.8f, 3.5f, 0.0f, 5.0f, 6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f, 2.5f, 0.0f, 1.5f, 2.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.7f, 0.7f, 1.4f, 0.7f, 0.7f, 1.4f, 0.7f, 0.7f, 1.4f, 0.7f, 0.7f, 1.4f, 0.7f, 0.7f, 1.4f, 0.7f, 0.7f, 1.4f},
    {1.05f, 1.05f, 2.1f, 1.05f, 1.05f, 2.1f, 1.05f, 1.05f, 2.1f, 1.05f, 1.05f, 2.1f, 1.05f, 1.05f, 2.1f, 1.05f, 1.05f, 2.1f},
};

int ProcessMoveContacts(C_HumanMovement *movement, S_MoveContacts *contacts, bool openDoors, C_Actor **touched)
{
    C_Human *owner = movement->m_owner;
    for (int i = 0; i < contacts->count; ++i) {
        S_CollisionVolume *volume = contacts->volumes[i];
        if (!volume || !(volume->flags & (COLLISION_FLAG_DYNAMIC | COLLISION_FLAG_PRIMITIVE)) ||
            !(volume->flags & COLLISION_FLAG_DYNAMIC) || !volume->frame || (volume->type & COLLISION_TYPE_MINE))
            continue;
        S_FrameData *data = volume->frame->GetData();
        T_PhysicsObject physics = data ? data->physicsObject : g_noPhysicsObject;
        if (physics != g_noPhysicsObject) {
            // A physics object: pushed by the body's velocity.
            GetPhysicsModule()->Hit(physics, PHYS_HIT_PUSH, &owner->GetVelocity(), nullptr);
            continue;
        }
        C_Actor *actor = data ? data->actor : nullptr;
        *touched = actor;
        if (!actor)
            return 0;
        switch (actor->GetType()) {
        case ACTOR_HUMAN: {
            // Bumped into a human: on which side of the move it is.
            owner->OnCollisionEx(actor, reinterpret_cast<const S_vector *>(&contacts->pos), PointerArg(&contacts->move),
                                 PointerArg(&contacts->normals[i]), 0, 0, 0);
            const S_vector4 &other = actor->GetPosition4();
            const S_vector4 &own = owner->GetPosition4();
            const float move[2] = {contacts->move.x, contacts->move.z};
            const float toOther[2] = {other.x - own.x, other.z - own.z};
            return AngleBetween2D(move, toOther) >= 0.0f ? 1 : 2;
        }
        case ACTOR_LADDER: {
            // Walked into a ladder: used when it can be climbed from here.
            C_HumanWork *work = owner->GetWork();
            if (movement->m_unknownA30 != MOVE_NO_TIME || work->m_request.stance == HUMAN_STANCE_PRONE ||
                InRange(movement->m_animState.carryAnim, 0x4C, 0x4F))
                break;
            const bool lookingUp = work->GetViewDir().y >= -0.08;
            S_vector velocity(0.0f, 0.0f, 0.0f);
            if (movement->m_animState.anim.anim >= ANIM_WALK || work->m_mode == WORK_MODE_SWIMMING)
                velocity = XYZ(movement->m_mixer.m_move) * 100.0f;
            if (!CanUseLadder(actor, owner->GetPosition4(), owner->GetDirection4(), velocity, lookingUp) ||
                !movement->m_remoteActions.empty())
                break;
            const S_vector zero(0.0f, 0.0f, 0.0f);
            const S_vector zero2(0.0f, 0.0f, 0.0f);
            const S_vector zero3(0.0f, 0.0f, 0.0f);
            if (owner->OnCollisionEx(actor, &zero, PointerArg(&zero2), PointerArg(&zero3), 0, 0, 0)) {
                movement->m_useObject = actor;
                S_RemoteAction use;
                use.time = 0;
                use.action = REMOTE_ACTION_USE;
                use.phase = 0;
                movement->m_remoteActions.push_back(use);
            }
            break;
        }
        case ACTOR_DOOR:
            // A closed door opens (when asked to).
            if (IsServer() && openDoors && static_cast<C_Door *>(actor)->GetUnknown224() == 0)
                static_cast<C_Door *>(actor)->SetState(2, false, false, owner->GetRole() != HUMAN_ROLE_PLAYER);
            break;
        case ACTOR_TRAIN:
        case ACTOR_BARREL:
            if (IsServer())
                actor->OnCollision(owner, reinterpret_cast<const S_vector *>(&contacts->pos), PointerArg(&contacts->move),
                                   PointerArg(&LastContactNormal(*contacts)), 0, 0);
            break;
        default:
            break;
        }
    }
    return 0;
}

// ------------------------------------------------------------------ setting up

void C_HumanLocomotion::Init(C_HumanMovement *movement, C_HumanSpecialMoves *specialMoves, C_HumanAnimPlayer *animPlayer,
                             C_Human *owner)
{
    m_owner = owner;
    m_specialMoves = specialMoves;
    m_movement = movement;
    m_animPlayer = animPlayer;
    float lowest;
    g_collisionWorld.GetWaterHeightRange(lowest, m_waterLevel);
    ResetState();
}

void C_HumanLocomotion::Done()
{
    StopSwimSound(true);
}

void C_HumanLocomotion::Reset()
{
    StopSwimSound(true);
    ResetState();
}

void C_HumanLocomotion::ResetState()
{
    m_fallGround = MOVE_NO_VALUE;
    m_fallHeight = MOVE_NO_TIME;
    m_fallSpeed = 0.0f;
    m_fallStepTime = 0.0f;
    m_pushTime = MOVE_NO_TIME;
    m_groundTestTime = 250;
    m_push.x = m_push.y = m_push.z = 0.0f;
    m_edgeTime = 0;
    m_edgeHeight = MOVE_NO_VALUE;
    m_turning = false;
    m_turnTarget.x = MOVE_NO_VALUE;
    m_shiftTime = MOVE_NO_TIME;
    m_directionChanged = false;
    m_positionChanged = false;
    m_unknown0E8 = 1;
    m_unknown0E4 = false;
    m_unknown0DC = 0;
    m_unknown0E0 = 0;
    m_deadSlideTime = 0;
    m_jumpBack = false;
    m_slopeSlowdown = 0.0f;
    m_unknown0EC = 0;
    m_volumesTime = 0;
    m_volumesDir = m_owner->GetDirection4();
    m_unknown090 = 0;
    m_waterTransition = 0;
    m_waterTestTime = 10000;
    m_waterHeight = MOVE_NO_VALUE;
    m_waterDepth = MOVE_NO_VALUE;
    m_inWater = false;
    m_swimSoundFade = MOVE_NO_TIME;
    m_steadyTurnTime = MOVE_NO_TIME;
    m_waterSurface = MOVE_NO_VALUE;
    m_unknown030 = {};
    m_swimSound = nullptr;
    m_swimSoundLeft = 0;
    m_swimSoundKind = 0x14;
    m_unknown058 = 1.0f;
    m_unknown0F0 = 0;
    m_fallStartY = MOVE_NO_VALUE;
    m_swimBack = false;
    m_turnSpeed = 0.015f;
    m_unknown000.x = m_unknown000.y = m_unknown000.z = 0.0f;
    m_move.x = m_move.y = m_move.z = 0.0f;
    if (m_owner->GetRole() != HUMAN_ROLE_PLAYER)
        return;
    // A soldier of the player's squad over 20 m above the ground (and not above water) comes
    // down on a parachute.
    m_movement->RemoveCollision(false);
    const S_vector framePos = m_owner->GetFrame()->GetWorldPos();
    S_vector hitPos(0.0f, 0.0f, 0.0f), normal;
    S_CollisionVolume *ground = CollisionSphereTest(S_vector(framePos.x, framePos.y + 1.0f, framePos.z),
                                                    S_vector(0.0f, -1000.0f, 0.0f), 0.2f, &hitPos, &normal, 0, 0);
    if (!ground || (!((hitPos - m_owner->GetFrame()->GetWorldPos()).Square() <= 400.0f) && hitPos.y > m_waterLevel))
        m_movement->m_parachuting = true;
    m_movement->RestoreCollision(false);
}

// ------------------------------------------------------------------ the update

void C_HumanLocomotion::Update(int time)
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    m_unknown0F0 = 0;
    if (movement->m_stanceChange > -2) {
        if (movement->m_stanceChange >= 0) {
            movement->m_stanceChange -= time;
            if (movement->m_stanceChange < 0)
                movement->m_stanceChange = 0;
        }
        if (movement->m_unknown9A0 >= 0) {
            movement->m_unknown9A0 -= time;
            if (movement->m_unknown9A0 < 0)
                movement->m_unknown9A0 = 0;
        }
    }
    if (movement->m_unknown9B4 >= 0) {
        movement->m_unknown9B4 -= time;
        if (movement->m_unknown9B4 < 1) {
            movement->m_unknown9B4 = MOVE_NO_TIME;
            movement->m_weaponPose2->StopProneFit();
        }
    }
    if (m_unknown0EC > 0)
        m_unknown0EC -= time;
    if (m_volumesTime > 0)
        m_volumesTime -= time;
    if (m_steadyTurnTime != MOVE_NO_TIME) {
        if (m_steadyTurnTime < 1)
            m_steadyTurnTime = MOVE_NO_TIME;
        else
            m_steadyTurnTime -= time;
    }
    const int stance = m_shiftTime != MOVE_NO_TIME ? m_shiftStance : work->m_request.stance;
    movement->UpdateStanceVolumes(stance);

    // The turn of the local player (the others turn in C_HumanMovement::UpdatePhysicsRest).
    if (m_owner->IsLocalPlayer()) {
        const int anim = movement->m_animState.anim.anim;
        if ((InRange(anim, 9, 0x0E)) || anim == ANIM_JUMP_12 || m_turnTarget.x <= MOVE_NO_VALUE) {
            m_turning = false;
        } else {
            m_unknown0E4 = false;
            if (movement->m_unknown998 != MOVE_NO_TIME) {
                movement->m_unknown998 -= time;
                if (movement->m_unknown998 < 1)
                    movement->m_unknown998 = MOVE_NO_TIME;
            }
            m_turnSpeed = m_owner->IsLocalPlayer() ? TURN_SPEED_PLAYER : TURN_SPEED_OTHERS;
            const bool steady = !(m_owner->IsLocalPlayer() && movement->m_unknown998 == MOVE_NO_TIME);
            if (UpdateTurn(time, steady))
                m_turnTarget.x = MOVE_NO_VALUE;
            UpdateVelocity(true);
        }
    } else if (m_turning) {
        UpdateVelocity(true);
    }

    movement->m_mixer.Update(time);
    if (movement->m_unknownA1C != MOVE_NO_TIME && movement->m_unknownA1C > 0) {
        movement->m_unknownA1C -= time;
        if (movement->m_unknownA1C < 1) {
            if (movement->m_handsCarryState == 1)
                movement->m_handsCarryState = 0;
            else if (movement->m_handsCarryState == 2)
                movement->m_handsCarryState = 3;
        }
    }
    if (stance != HUMAN_STANCE_PRONE && movement->m_unknown1AC == 1 && movement->m_owner->GetWork()->m_mode != WORK_MODE_SWIMMING)
        UpdatePitch(time);
    UpdateSwimSound(time);

    if (movement->m_unknown1AC == 1) {
        const int mode = movement->m_owner->GetWork()->m_mode;
        const int anim = movement->m_animState.anim.anim;
        const uint32_t interaction = movement->m_interaction;
        // Interacting, sitting, in a vehicle (the seat animations): the body stays where the
        // frame is.
        if ((interaction != 0 && interaction != 1) || mode == 2 || mode == 3 || anim == 0x9F || anim == 0x9E ||
            anim == 0x9B || anim == 0x6F || anim == 0x22 || movement->m_seatMode > 0 || time <= 0) {
            if (IsSimulatedHere(m_owner) || movement->m_unknown1AC != 1) {
                const S_matrix &m = m_owner->GetFrame()->GetMatrix();
                m_owner->SetPosition4({m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]});
            }
        } else {
            const auto clearGround = [movement](bool ground) {
                movement->m_groundMaterial = -1;
                if (ground)
                    movement->m_ground = nullptr;
                movement->m_slideFactor = 0.0f;
                movement->m_damageMaterial = -1;
                movement->m_onDynamicGround = false;
                movement->m_onGround = false;
            };
            if (mode == WORK_MODE_SWIMMING) {
                clearGround(true);
                MoveSwimming(time);
            } else if (IsClimbing(anim)) {
                // (The ground volume is kept.)
                clearGround(false);
                MoveClimbing(time);
            } else if (anim == ANIM_JUMP_12 || anim == ANIM_JUMP || anim == ANIM_JUMP_CROUCHED) {
                clearGround(true);
                MoveJumping(time);
            } else if (movement->m_useState == MOVE_NO_TIME) {
                MoveOnGround(time);
            } else {
                clearGround(true);
                MoveOnLadder(time);
            }
        }
    } else {
        MoveDying(time);
    }

    if (m_groundTestTime >= 0)
        m_groundTestTime -= time;
    if (m_directionChanged) {
        const S_vector4 &dir = m_owner->GetDirection4();
        m_owner->SetDirection4(ToVector4(XYZ(dir).Normalized(), dir.w));
    }
    if (!m_positionChanged)
        m_move.x = m_move.y = m_move.z = 0.0f;
    if (movement->m_unknown1AC >= 7 || movement->m_unknown1AC == 1)
        UpdateCollisionVolumes(false);
    m_directionChanged = false;
    m_positionChanged = false;
}

void C_HumanLocomotion::UpdateCollisionVolumes(bool remote)
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    const bool prone = work->m_request.stance == HUMAN_STANCE_PRONE;
    const float minDot = movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING || prone ? 0.9975f : 0.9553365f;
    const S_vector4 &dir = m_owner->GetDirection4();
    const bool turned = dir.x * m_volumesDir.x + dir.y * m_volumesDir.y + dir.z * m_volumesDir.z < minDot ||
                        IsLean(movement->m_animState.anim.anim);
    if (!movement->m_unknown18C)
        return;
    // Still for 2 s and not turned: nothing to do.
    if (movement->m_stillTime > 1999 && !movement->m_onDynamicGround && movement->m_unknown9AC == MOVE_NO_TIME && !turned)
        return;
    I3D_frame *frame = m_owner->GetFrame();
    const S_matrix &m = frame->GetMatrix();
    S_vector4 framePos = {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
    const S_vector pos = XYZ(m_owner->GetPosition4());
    const S_vector delta = XYZ(framePos) - pos;
    int stance = work->m_request.stance;
    // `relink`: the frame is put back where it was (and relinked); `full`: the dummies and
    // hit spheres are recomputed from the skeleton (otherwise moved along).
    bool relink = false;
    bool full = false;
    bool frameMoved = false;
    I3D_frame *parent = nullptr;
    enum { SET_FRAME, MOVE_DUMMIES, UPDATE_DUMMIES } step;

    if (m_shiftTime == MOVE_NO_TIME && movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING) {
        stance = HUMAN_STANCE_PRONE;
        relink = full = true;
        step = SET_FRAME;
    } else if (m_shiftTime != MOVE_NO_TIME && (stance = m_shiftStance) == HUMAN_STANCE_PRONE) {
        relink = full = true;
        step = SET_FRAME;
    } else if (prone || movement->m_unknown1AC != 1 || (IsLocalGame() && m_owner->GetRole() == HUMAN_ROLE_PLAYER) ||
               m_owner->IsServerSimulated() || m_owner->IsLocalPlayer()) {
        relink = full = true;
        step = SET_FRAME;
    } else if (remote) {
        relink = false;
        full = true;
        step = UPDATE_DUMMIES;
    } else if (movement->m_seatMode > 0) {
        relink = full = true;
        step = SET_FRAME;
    } else {
        // Did the frame drift from the position? (The local player's x is not compared, as
        // in the original.)
        bool moved;
        if (m_owner->IsLocalPlayer())
            moved = std::fabs(delta.y) > 0.001f || std::fabs(delta.z) > 0.001f;
        else
            moved = std::fabs(delta.x) > 0.001f || std::fabs(delta.y) > 0.001f || std::fabs(delta.z) > 0.001f;
        const int anim = movement->m_animState.anim.anim;
        const uint32_t interaction = movement->m_interaction;
        const float fallSpeed = m_owner->GetVelocity().y;
        const bool big = fallSpeed < -1.0f || fallSpeed > 1.0f || movement->m_unknown9AC != MOVE_NO_TIME ||
                         (anim < ANIM_WALK && !IsInPlace(anim)) || turned || (interaction != 0 && interaction != 1) ||
                         std::fabs(framePos.x - movement->m_stanceSpheres[0].x) > 0.5f ||
                         std::fabs(framePos.z - movement->m_stanceSpheres[0].z) > 0.5f;
        full = big;
        relink = moved;
        if (!moved) {
            if (!big && m_volumesTime > 0)
                return;
            step = UPDATE_DUMMIES;
        } else if (big || m_volumesTime < 1) {
            step = SET_FRAME;
        } else {
            step = MOVE_DUMMIES;
        }
    }

    if (step == SET_FRAME) {
        // The frame at the logical position for the dummies.
        parent = frame->GetParent();
        frame->SetPos(pos);
        frame->Update();
        frameMoved = true;
        step = !full && m_volumesTime > 0 ? MOVE_DUMMIES : UPDATE_DUMMIES;
    }
    if (step == MOVE_DUMMIES) {
        movement->MoveDummies(pos);
        movement->MoveHitSpheres(pos);
    } else {
        movement->UpdateDummies();
        movement->UpdateHitSpheres();
        const S_matrix &world = frame->GetMatrix();
        m_volumesDir = ToVector4(world(2).Normalized(), world.m[2][3]);
        if (movement->m_stillTime > -1000) {
            if (movement->m_clock < 5000) {
                m_volumesTime = 0;
            } else {
                // The next full update: sooner for the player's squad and the fast gaits.
                m_volumesTime = m_owner->GetRole() == HUMAN_ROLE_PLAYER ? 200 : MoveTime(1400.0f);
                float factor;
                switch (work->m_request.gait) {
                case 0: factor = 1.2f; break;
                case 2: factor = 0.9f; break;
                case 4: factor = 1.5f; break;
                case 5: factor = 0.6f; break;
                default: factor = 1.0f; break;
                }
                m_volumesTime = static_cast<int>(static_cast<float>(m_volumesTime) * factor);
                if (!relink && !full)
                    m_volumesTime *= 4;
            }
        }
    }

    // The collision volumes at the stance and hit spheres.
    movement->UpdateStanceVolumes(stance);
    for (int i = 0; i < movement->m_stanceVolumeCount; ++i) {
        S_CollisionVolume *volume = movement->m_stanceVolumes[i];
        const S_vector4 &sphere = movement->m_stanceSpheres[i];
        volume->center = XYZ(sphere);
        volume->centerW = sphere.w;
        volume->radius = sphere.w * movement->m_stanceVolumeScale;
        g_collisionWorld.MoveVolume(volume);
    }
    MoveHitVolumes(movement);
    if (relink && (full || frameMoved)) {
        // The frame back where it was (the original also raises its copy of the position by
        // 0.5 m afterwards, unused).
        frame->SetPos(XYZ(framePos));
        frame->LinkTo(parent, 1);
        frame->Update();
    }
}

// ------------------------------------------------------------------ on the ground

bool C_HumanLocomotion::MoveOnGround(int time)
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    const float seconds = static_cast<float>(time) * 0.001f;
    int stance = work->m_request.stance;
    const S_vector4 startSphere = movement->m_stanceSpheres[0];
    const bool simulated = IsSimulatedHere(m_owner) || movement->m_unknown1AC != 1;
    S_CollisionVolume *hit = nullptr;                   // what the sweep touched last
    S_CollisionVolume *groundVolume = nullptr;          // the floor found (FindGroundContact)
    bool groundBelow = true;                            // the ground ray (SweepBody) found ground
    bool idle = false;                                  // not moving, on stable ground
    bool floorBelow = false;                            // a dynamic floor: the ray found a floor
    S_GroundContact ground;
    S_vector rayPos, rayNormal(0.0f, 0.0f, 0.0f);
    g_groundNormal = {0.0f, 1.0f, 0.0f, g_groundNormal.w};
    m_move = movement->m_mixer.m_move;

    if (movement->m_flags & MOVE_FLAG_FOLLOW_PATH) {
        // Along the AI's path: its height, at most 4.5 m/s up or down.
        const S_vector4 &pos = m_owner->GetPosition4();
        S_vector to = XYZ(pos) + XYZ(m_move);
        if (m_owner->AdjustToPathHeight(&to)) {
            const float y = pos.y + m_move.y;
            const float most = seconds * 4.5f;
            if (std::fabs(y - to.y) > most)
                to.y = y <= to.y ? y + most : y - most;
        }
        if (m_unknown0EC < 1) {
            if (LengthSquared(m_move) < 1e-16f) {
                movement->m_stillTime += time;
                if (movement->m_stillTime > 10000000)
                    movement->m_stillTime = 10000000;
            } else {
                const S_vector dir = (to - XYZ(pos)).Normalized();
                const float flatness = static_cast<float>(std::cos(HALF_PI_D - std::acos(std::fabs(dir.y))));
                const float up = dir.y <= 0.0f ? 0.0f : 1.0f;
                S_vector4 to4 = ToVector4(to, pos.w);
                SlowOnSlope(flatness, (1000.0f / static_cast<float>(time)) * (to.y - pos.y), up, &to4, &pos, time);
                if (simulated) {
                    m_owner->SetPosition4({to4.x, to4.y, to4.z, pos.w});
                    m_positionChanged = true;
                }
            }
            movement->m_slideFactor = 0.0f;
            return false;
        }
    }

    S_MoveContacts contacts;
    std::memset(&contacts, 0, sizeof(contacts));
    if (m_shiftTime != MOVE_NO_TIME)
        stance = m_shiftStance;
    if (movement->m_stanceVolumeCount == 0)
        return false;
    // The position relative to the lowest sphere.
    S_vector offset = XYZ(m_owner->GetPosition4()) - XYZ(movement->m_stanceSpheres[0]);
    if (m_shiftTime != MOVE_NO_TIME)
        m_shiftTime = m_shiftTime - time < 1 ? MOVE_NO_TIME : m_shiftTime - time;
    if (m_pushTime != MOVE_NO_TIME) {
        // Pushed off an edge (for what is left of the push time).
        m_pushTime -= time;
        const float ms = m_pushTime < 0 ? static_cast<float>(m_pushTime + time) : static_cast<float>(time);
        const S_vector push = XYZ(m_push) * ms * 0.002f;
        m_move.x += push.x;
        m_move.y += push.y;
        m_move.z += push.z;
    }

    bool fall = false;
    if (LengthSquared(m_move) < 1e-16f) {
        // Not moving: on the ground below (tested while on a dynamic volume or falling).
        if (movement->m_clock > 2000 && movement->m_stillTime > 0) {
            if (movement->m_stillTime > 10000000)
                movement->m_stillTime = 10000000;
            if (!movement->m_onDynamicGround && m_fallSpeed == 0.0f) {
                idle = true;
            } else {
                S_CollisionVolume *second = nullptr;
                movement->RemoveCollision(false);
                const S_vector4 &lowest = movement->m_stanceSpheres[0];
                hit = CollisionRayTest(XYZ(lowest), S_vector(0.0f, -lowest.w - 0.1f, 0.0f), &rayPos, &rayNormal, TEST_TYPES, TEST_FLAGS);
                if (stance == HUMAN_STANCE_PRONE) {
                    // Lying: also under the last sphere.
                    const S_vector4 &last = movement->m_stanceSpheres[movement->m_stanceVolumeCount - 1];
                    S_vector pos2, normal2;
                    second = CollisionRayTest(XYZ(last), S_vector(0.0f, -last.w - 0.1f, 0.0f), &pos2, &normal2, TEST_TYPES, TEST_FLAGS);
                    idle = hit || second;
                } else {
                    idle = hit != nullptr;
                }
                movement->RestoreCollision(false);
                if (idle) {
                    movement->m_groundMaterial = GetCollisionMaterial(hit);
                    movement->m_ground = hit;
                    if (IsServer())
                        movement->DamageByContacts(nullptr, hit, time);
                    if (hit) {
                        movement->m_onDynamicGround = (hit->flags & COLLISION_FLAG_DYNAMIC) != 0;
                    } else if (second) {
                        movement->m_groundMaterial = GetCollisionMaterial(second);
                        movement->m_ground = second;
                        if (IsServer())
                            movement->DamageByContacts(nullptr, second, time);
                        movement->m_onDynamicGround = (second->flags & COLLISION_FLAG_DYNAMIC) != 0;
                    }
                }
            }
        }
        movement->m_stillTime += time;
        if (m_fallSpeed > 1e-8f) {
            fall = !idle;
        } else if (!idle) {
            m_fallSpeed = GRAVITY;
            fall = true;
        }
        if (!fall) {
            movement->m_onGround = hit && (rayNormal.y < -0.3f || rayNormal.y > 0.6f);
            if (movement->m_mixer.m_tracks[0].hold < 1)
                movement->m_mixer.SetTrackRate(0, 1.0f);
        }
    } else {
        if (m_fallSpeed <= 1e-8f)
            m_fallSpeed = 4.0f;
        fall = true;
    }

    if (fall) {
        // The move (with the fall) swept through the world.
        m_move.y -= FallDistance(time, m_fallSpeed, m_fallStepTime);
        SweepBody(contacts, &hit, &groundBelow, &offset, 0.0f);
        if (hit && stance == HUMAN_STANCE_PRONE && contacts.spheres[0].y - contacts.pos.y < static_cast<float>(time) * -0.0017f) {
            // Lying: up by 1 m/s at most, pushed off the contact.
            contacts.pos = {contacts.spheres[0].x, static_cast<float>(time) * 0.001f + contacts.spheres[0].y, contacts.spheres[0].z,
                            contacts.pos.w};
            const S_vector4 &normal = LastContactNormal(contacts);
            movement->m_mixer.AddMove({normal.x * 0.4f, normal.y * 0.4f, normal.z * 0.4f, normal.w}, 80);
        }
        movement->m_groundMaterial = -1;
        ground.hit = hit;
        ground.standing = FindGroundContact(contacts, stance == HUMAN_STANCE_PRONE, &ground.normal, &groundVolume);
        movement->m_onGround = ground.standing;
        movement->m_groundMaterial = GetCollisionMaterial(groundVolume);
        movement->m_ground = groundVolume;
        if (IsServer())
            movement->DamageByContacts(&contacts, nullptr, time);
        if (!groundVolume || movement->m_onDynamicGround) {
            if (movement->m_onDynamicGround) {
                // On a dynamic volume: is it still below?
                S_CollisionVolume *second = nullptr;
                movement->RemoveCollision(false);
                const S_vector4 &lowest = movement->m_stanceSpheres[0];
                S_CollisionVolume *below = CollisionRayTest(XYZ(lowest), S_vector(0.0f, -lowest.w - 0.15f, 0.0f), &rayPos, &rayNormal,
                                                            TEST_TYPES, TEST_FLAGS);
                if (stance == HUMAN_STANCE_PRONE) {
                    const S_vector4 &last = movement->m_stanceSpheres[movement->m_stanceVolumeCount - 1];
                    S_vector pos2, normal2;
                    second = CollisionRayTest(XYZ(last), S_vector(0.0f, -last.w - 0.15f, 0.0f), &pos2, &normal2, TEST_TYPES, TEST_FLAGS);
                }
                movement->RestoreCollision(false);
                movement->m_onDynamicGround = false;
                if (below) {
                    movement->m_onDynamicGround = (below->flags & COLLISION_FLAG_DYNAMIC) != 0;
                    floorBelow = rayNormal.y < -0.3f || rayNormal.y > 0.6f;
                }
                if (second && !movement->m_onDynamicGround)
                    movement->m_onDynamicGround = (second->flags & COLLISION_FLAG_DYNAMIC) != 0;
            }
        } else {
            movement->m_onDynamicGround = (groundVolume->flags & COLLISION_FLAG_DYNAMIC) != 0;
        }
        if (LengthSquared(ground.normal) >= 1e-16f)
            ground.hasNormal = true;
        AdjustToGround(contacts, ground, startSphere, time);
    }

    if (m_pushTime < 1) {
        m_pushTime = MOVE_NO_TIME;
        m_push.x = m_push.y = m_push.z = 0.0f;
    }
    if (IsSimulatedHere(m_owner) && !idle) {
        // Ending against a wall or a ceiling (not a floor).
        const bool onWall = contacts.count != 0 && !(0.26f <= LastContactNormal(contacts).y);
        const bool standing = !hit || !groundBelow || ground.standing || m_fallSpeed <= 14.715f ? ground.standing : false;
        UpdateFall(hit, standing, time, onWall, true);
    }

    if (idle) {
        m_edgeTime = 0;
        m_edgeHeight = MOVE_NO_VALUE;
    } else {
        if (hit) {
            contacts.pos.x += offset.x;
            contacts.pos.y += offset.y;
            contacts.pos.z += offset.z;
            const S_vector delta = XYZ(contacts.pos) - XYZ(m_owner->GetPosition4());
            C_Actor *touched = nullptr;
            if (IsSimulatedHere(m_owner)) {
                const int anim = movement->m_animState.anim.anim;
                const bool openDoors = movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING || IsOnLadder(anim) ||
                                       anim == ANIM_LADDER_TOP_OFF || anim == ANIM_LADDER_TOP_ON;
                ProcessMoveContacts(m_owner->GetMovement(), &contacts, openDoors, &touched);
            }
            if (std::fabs(delta.y) * 0.5f + std::fabs(delta.x) + std::fabs(delta.z) >= 0.001f && simulated) {
                if (!movement->m_onDynamicGround) {
                    m_owner->SetPosition4(contacts.pos);
                    movement->m_stillTime = 0;
                } else if ((std::fabs(delta.x) > 0.0001f || std::fabs(delta.z) > 0.0001f) &&
                           (std::fabs(m_move.x) > 0.0001f || std::fabs(m_move.z) > 0.0001f || (!ground.standing && !floorBelow))) {
                    // Moved on a dynamic volume: the visual position jumps along.
                    m_owner->SetPosition4(contacts.pos);
                    C_HumanMovement *body = m_owner->GetMovement();
                    body->m_stillTime = 0;
                    body->m_visual.SetNetworkPosition(body->m_owner->GetPosition4());
                    body->m_visual.SetNetworkPosition(body->m_owner->GetPosition4());
                    body->m_visual.m_sectorTime = 10000;
                    body->m_visual.m_lighting.m_sampleDue = true;
                    GetGame()->GetScene()->Unknown144(body->m_owner->GetFrame());
                    movement->m_stillTime = 0;
                }
                m_positionChanged = true;
            }
        }
        const int anim = movement->m_animState.anim.anim;
        // (A debugging leftover: the falling animation touching something beeps.)
        if (anim == ANIM_FALLING && hit)
            MessageBeep(0xFFFFFFFF);
        if ((groundBelow || m_fallSpeed > GRAVITY) && hit && ground.standing) {
            Land(m_fallSpeed > GRAVITY);
            m_edgeTime = 0;
            m_edgeHeight = MOVE_NO_VALUE;
        } else {
            movement->m_stillTime = 0;
            if ((!groundBelow || !ground.standing) && m_waterSurface == MOVE_NO_VALUE) {
                const S_vector4 &last = LastContactNormal(contacts);
                if (stance != HUMAN_STANCE_PRONE && contacts.count != 0 &&
                    (m_owner->GetRole() == HUMAN_ROLE_PLAYER || !IsLocalGame()) && anim != ANIM_SQUAT &&
                    (m_pushTime < 1 || last.x != 0.0f || last.z != 0.0f)) {
                    // At an edge: pushed away from it (or slid off it falling fast).
                    const float y = m_owner->GetPosition4().y;
                    const bool slideOff = groundBelow && m_fallSpeed > GRAVITY && hit && !ground.standing;
                    const bool atEdge = m_edgeTime < 250 || (m_edgeHeight != MOVE_NO_VALUE && std::fabs(m_edgeHeight - y) > 0.15f);
                    if (slideOff) {
                        movement->m_mixer.AddMove({last.x * 0.6666667f, last.y * 0.6666667f, last.z * 0.6666667f, last.w}, 120);
                    } else if (atEdge) {
                        m_push = last;
                        m_push.y = 0.0f;
                        if (LengthSquared(m_push) < 1e-16f) {
                            m_pushTime = MOVE_NO_TIME;
                        } else {
                            const S_vector dir = XYZ(m_push).Normalized() * 1.5f;
                            m_push.x = dir.x;
                            m_push.y = dir.y;
                            m_push.z = dir.z;
                            m_pushTime = !groundBelow || (g_groundNormal.z + g_groundNormal.x) * 0.0f + g_groundNormal.y <= 0.58f ? 100 : 0;
                            if (m_edgeTime < 1) {
                                m_edgeHeight = y;
                            } else if (m_edgeTime > 250) {
                                m_edgeTime -= 250;
                                m_edgeHeight = y;
                            }
                            m_edgeTime += time;
                        }
                    }
                }
                if (anim == ANIM_FALLING && movement->m_parachuting)
                    work->Play(ANIM_STAND, false);
                if (anim == ANIM_FALLING && m_pushTime != MOVE_NO_TIME && m_owner->GetVelocity().y >= -0.01f) {
                    m_pushTime = MOVE_NO_TIME;
                    m_edgeTime = 0;
                    m_edgeHeight = MOVE_NO_VALUE;
                }
            } else {
                if (anim == ANIM_FALLING)
                    work->Play(ANIM_STAND, false);
                m_edgeTime = 0;
                m_edgeHeight = MOVE_NO_VALUE;
            }
        }
    }

    // Into the water (tested every 150 ms; the enemies less often by their detail).
    if (m_waterTransition < 0) {
        m_waterTransition += time;
        if (m_waterTransition >= 0)
            m_waterTransition = 0;
        return hit != nullptr;
    }
    if (!idle)
        m_waterTestTime += time;
    if (m_owner->GetRole() == HUMAN_ROLE_ENEMY) {
        const float interval = (movement->m_flags & MOVE_FLAG_HIDDEN) ? 500.0f : (movement->m_flags & MOVE_FLAG_SMALL) ? 400.0f : 300.0f;
        if (static_cast<float>(m_waterTestTime) <= interval)
            return hit != nullptr;
    } else if (m_waterTestTime < 151) {
        return hit != nullptr;
    }
    const bool deep = m_specialMoves->UpdateWater();
    m_waterTestTime = 0;
    if (deep) {
        StartSwimming();
        m_inWater = true;
        return hit != nullptr;
    }
    m_inWater = false;
    return hit != nullptr;
}

void C_HumanLocomotion::SweepBody(S_MoveContacts &contacts, S_CollisionVolume **hit, bool *groundBelow, S_vector *offset,
                                  float lift)
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    const bool swimming = movement->m_owner->GetWork()->m_mode == WORK_MODE_SWIMMING;
    // (MOVE_FLAG_REMOTE sweeps the lowest sphere only.)
    int count = movement->m_stanceVolumeCount;
    if (!swimming && (movement->m_flags & MOVE_FLAG_REMOTE))
        count = 1;
    const bool lying = work->m_request.stance == HUMAN_STANCE_PRONE ||
                       (movement->m_nextStance == HUMAN_STANCE_PRONE && movement->m_unknown9A0 > 0);
    const bool localPlayer = m_owner->IsLocalPlayer();
    if (localPlayer || ((!(movement->m_flags & MOVE_FLAG_GROUND_RAY) || m_unknown0EC > 0 || lying || swimming) &&
                        (IsLocalGame() || m_owner->IsServerSimulated()))) {
        // The stance spheres swept.
        for (int i = 0; i < count; ++i) {
            const S_vector4 &sphere = movement->m_stanceSpheres[i];
            contacts.spheres[i] = sphere;
            contacts.spheres[i].y += lift;
            contacts.sphereCount = count;
            contacts.move = m_move;
            contacts.excludeTypes = TEST_TYPES;
            if ((std::fabs(m_move.x) <= 1e-8f && std::fabs(m_move.z) <= 1e-8f) || !localPlayer)
                contacts.excludeTypes |= COLLISION_TYPE_LADDER;
            if (!localPlayer)
                contacts.excludeTypes |= 0x100;
            contacts.excludeFlags = m_owner->GetRole() != HUMAN_ROLE_PLAYER ? TEST_FLAGS_OTHERS : TEST_FLAGS;
            if (!localPlayer)
                contacts.excludeFlags |= 0x800000;
            // Falling from high up: smaller spheres, a little lower (the original compares the
            // ground's height itself, not the height above it, with 2 m).
            if (m_fallHeight == MOVE_NO_TIME || m_fallGround == MOVE_NO_VALUE || std::fabs(m_fallGround) <= 2.0f) {
                contacts.radii[i] = sphere.w;
            } else {
                contacts.radii[i] = sphere.w * 0.7f;
                contacts.spheres[i].y -= sphere.w * 0.3f;
                if (i == 0)
                    offset->y += movement->m_stanceSpheres[0].w * 0.3f;
            }
        }
        // The contacts allowed: fewer for the bodies far away.
        if (!lying) {
            if (movement->m_flags & MOVE_FLAG_HIDDEN)
                g_moveIterations = 2.0f;
            else if (movement->m_flags & MOVE_FLAG_SMALL)
                g_moveIterations = 3.0f;
            else
                g_moveIterations = 4.0f;
            if (m_owner->IsLocalPlayer())
                g_moveIterations += 1.0f;
        } else {
            g_moveIterations = 6.0f;
        }
        movement->RemoveCollision(false);
        if (!lying && movement->m_owner->GetWork()->m_mode != WORK_MODE_SWIMMING)
            *hit = g_collisionWorld.MoveSpheresStepping(contacts);
        else
            *hit = g_collisionWorld.MoveSpheresLow(contacts);
        if ((m_owner->IsLocalPlayer() || (!IsLocalGame() && !m_owner->IsServerSimulated())) && m_groundTestTime < 0) {
            // The ground right below (every 50 ms; 150 ms for the replicas).
            const uint32_t flags = m_owner->GetRole() != HUMAN_ROLE_PLAYER ? TEST_FLAGS_OTHERS : TEST_FLAGS;
            if (!m_owner->IsLocalPlayer())
                contacts.excludeFlags |= 0x800000;      // (into the contacts, not the ray's filter)
            const S_vector4 &lowest = movement->m_stanceSpheres[0];
            S_vector rayPos, rayNormal;
            S_CollisionVolume *below = CollisionRayTest(XYZ(lowest), S_vector(0.0f, -0.35f - lowest.w, 0.0f), &rayPos, &rayNormal,
                                                        TEST_TYPES, flags);
            *groundBelow = below != nullptr;
            if (below) {
                g_groundNormal = ToVector4(rayNormal, g_groundNormal.w);
                if (!IsLocalGame() && !m_owner->IsLocalPlayer() && !m_owner->IsServerSimulated()) {
                    m_groundTestTime = 150;
                    movement->RestoreCollision(false);
                    return;
                }
                m_groundTestTime = 50;
            }
        }
        movement->RestoreCollision(false);
        return;
    }

    // The cheap ground mode: a ray down at the end of the move.
    S_vector4 move = {m_move.x, 0.0f, m_move.z, m_move.w};
    if (IsLocalGame() && movement->m_slideFactor > 1e-5f) {
        // Down a steep slope: faster by the slide factor.
        const float length = S_vector(move.x, 0.0f, move.z).Magnitude();
        if (std::fabs(length) > 1e-5f) {
            const float scale = 1.0f / length * (length * movement->m_slideFactor);
            move.x *= scale;
            move.y = scale * 0.0f;
            move.z *= scale;
        }
    }
    const uint32_t flags = m_owner->GetRole() != HUMAN_ROLE_PLAYER ? TEST_FLAGS_OTHERS : TEST_FLAGS;
    const S_vector4 pos = m_owner->GetPosition4();
    const S_vector from(pos.x + move.x, pos.y + move.y + 1.0f, pos.z + move.z);
    S_vector down(0.0f, -2.0f, 0.0f);
    if (!IsLocalGame()) {
        if (m_fallStartY == 100000.0f || m_fallStartY == MOVE_NO_VALUE || m_fallStartY - pos.y < 2.0f)
            down.y = -3.0f;
        else
            down.y = -1.35f;
    }
    S_vector hitPos, hitNormal;
    movement->RemoveCollision(false);
    *hit = CollisionRayTest(from, down, &hitPos, &hitNormal, GROUND_RAY_TYPES, flags);
    movement->RestoreCollision(false);
    S_vector normal(0.0f, 1.0f, 0.0f);
    if (*hit) {
        const int material = GetCollisionMaterial(*hit);
        movement->m_unknown224 = hitNormal.y < -0.3f || hitNormal.y > 0.6f || material == 0x27 || material == 0x28;
        contacts.move = move;
        contacts.pos = ToVector4(hitPos - *offset, pos.w);
        contacts.spheres[0] = m_owner->GetPosition4();
        normal = hitNormal;
    } else {
        if (m_fallStartY == MOVE_NO_VALUE || m_fallStartY < pos.y)
            m_fallStartY = pos.y;
        movement->m_unknown224 = false;
        if (!IsServer()) {
            // A client: the body stays at the height it has.
            g_assumedGround.flags = COLLISION_FLAG_DISABLED;
            *hit = &g_assumedGround;
            contacts.move = move;
            contacts.pos = {pos.x + move.x, pos.y + move.y, pos.z + move.z, move.w};
            contacts.spheres[0] = m_owner->GetPosition4();
        } else {
            if (IsLocalGame() || m_owner->IsServerSimulated()) {
                // Something ahead (a step up)?
                const S_vector dir = XYZ(move).Normalized();
                movement->RemoveCollision(false);
                const S_vector start(pos.x - dir.x, (pos.y - dir.y) + 0.5f, pos.z - dir.z);
                *hit = g_collisionWorld.RayTest(start, dir + XYZ(m_move), &hitPos, &hitNormal, GROUND_RAY_TYPES, flags);
                movement->RestoreCollision(false);
                if (*hit) {
                    movement->RemoveCollision(false);
                    *hit = CollisionRayTest(hitPos, down, &hitPos, &hitNormal, GROUND_RAY_TYPES, flags);
                    movement->RestoreCollision(false);
                    if (*hit) {
                        contacts.pos = ToVector4(hitPos - *offset, pos.w);
                        contacts.spheres[0] = m_owner->GetPosition4();
                        contacts.count = 1;
                        contacts.normals[0] = ToVector4(hitNormal, 0.0f);
                        contacts.volumes[0] = *hit;
                        contacts.sphere[0] = 0;
                        return;
                    }
                }
            } else if (*hit) {
                return;
            }
            // Ground far below: the move goes on (a fall); none: the body stays.
            S_CollisionVolume *deep = nullptr;
            if (IsLocalGame() || m_owner->IsServerSimulated()) {
                movement->RemoveCollision(false);
                deep = CollisionRayTest(from, S_vector(0.0f, -100.0f, 0.0f), &hitPos, &hitNormal, GROUND_RAY_TYPES, flags);
                movement->RestoreCollision(false);
            }
            if (deep)
                contacts.pos = {pos.x + move.x, pos.y + move.y, pos.z + move.z, pos.w};
            else
                contacts.pos = pos;
            g_assumedGround.flags = COLLISION_FLAG_DISABLED;
            *hit = &g_assumedGround;
        }
    }
    contacts.normals[0] = ToVector4(normal, pos.w);
    contacts.count = 1;
    contacts.sphere[0] = 0;
    contacts.volumes[0] = *hit;
}

void C_HumanLocomotion::SlowOnSlope(float flatness, float climb, float facing, S_vector4 *pos, const S_vector4 *from, int time)
{
    float slowdown = 0.0f;
    const float w = m_move.w;
    if (!(flatness > 0.95f)) {
        // The climbing speed against the walking speed (slow walks climb at full speed).
        const float speed = (m_move.x * m_move.x * 1000.0f + m_move.z * m_move.z * 1000.0f) / static_cast<float>(time);
        if (speed > 0.0f && speed < 1.0f)
            climb /= speed;
        if (flatness > 0.0f && std::fabs(climb) > 0.2f) {
            const int gait = m_owner->GetWork()->m_request.gait;
            slowdown = (1.0f - flatness) * std::fabs(facing) * 1.5f;
            if (gait == 1 || gait == 4)
                slowdown *= 0.7f;
            else if (gait == 0)
                slowdown *= 0.5f;
            if (slowdown > 1.0f || flatness <= 0.55f)
                slowdown = 1.0f;
        }
    }
    // Changes by 0.17 per 100 ms at most.
    if (std::fabs((m_slopeSlowdown - slowdown) * 100.0f) / static_cast<float>(time) > 0.17f) {
        const float step = static_cast<float>(time) * 0.17f * 0.01f;
        slowdown = m_slopeSlowdown <= slowdown ? step + m_slopeSlowdown : m_slopeSlowdown - step;
    }
    m_slopeSlowdown = slowdown;
    float rate = 1.0f - slowdown * 0.5f;
    if (rate < 0.65f)
        rate = 0.65f;
    if (m_movement->m_mixer.m_tracks[0].hold < 1)
        m_movement->m_mixer.SetTrackRate(0, rate);
    if (slowdown == 0.0f)
        return;
    if (slowdown >= 0.9f) {
        *pos = *from;
        return;
    }
    pos->x -= (pos->x - from->x) * slowdown;
    pos->y -= (pos->y - from->y) * slowdown;
    pos->z -= (pos->z - from->z) * slowdown;
    pos->w = w;
}

void C_HumanLocomotion::AdjustToGround(S_MoveContacts &contacts, const S_GroundContact &ground, const S_vector4 &from, int time)
{
    C_HumanMovement *movement = m_movement;
    S_vector4 *end = &contacts.pos;
    movement->m_slideFactor = 0.0f;
    if (ground.hit && ground.hasNormal && movement->m_animState.anim.anim >= ANIM_WALK &&
        std::fabs(contacts.pos.y - contacts.spheres[0].y) < 1.0f) {
        const float up = (ground.normal.z + ground.normal.x) * 0.0f + ground.normal.y;
        const S_vector move = Flat(XYZ(contacts.move));
        const S_vector slope = Flat(XYZ(ground.normal));
        const float facing = move.Dot(slope);
        if (up < 0.9f && ground.standing) {
            // Steep ground: the cheap ground mode slides down it.
            movement->m_slideFactor = -(facing * up);
            if (movement->m_slideFactor < 0.5f)
                movement->m_slideFactor = 0.5f;
        }
        if (up > 0.5f && facing < 0.0f) {
            // Uphill.
            SlowOnSlope(up, (1000.0f / static_cast<float>(time)) * (contacts.pos.y - from.y), facing, end, &from, time);
            return;
        }
    }
    SlowOnSlope(1.0f, 1.0f, 1.0f, end, &from, time);
}

void C_HumanLocomotion::UpdateFall(S_CollisionVolume *ground, bool standing, int time, bool onWall, bool slide)
{
    C_HumanMovement *movement = m_movement;
    const S_vector4 pos = m_owner->GetPosition4();
    // Faster (from the height the fall started at: the fall damage), or out of the fall in
    // the water.
    const auto accelerate = [&](bool resetStart) {
        if (!m_specialMoves->UpdateWater()) {
            if (m_fallStartY == MOVE_NO_VALUE || m_fallStartY < pos.y || resetStart)
                m_fallStartY = pos.y;
            else
                ApplyFallDamage(false);
            AccelerateFall(time, &m_fallSpeed, &m_fallStepTime, movement->m_parachuting);
        } else if (m_waterTransition < 0) {
            m_fallSpeed = 0.0f;
            m_fallStepTime = 0.0f;
        } else {
            m_fallSpeed = 2.0f;
            m_fallStepTime = 0.0f;
        }
    };
    const bool replica = !IsLocalGame() && !m_owner->IsLocalPlayer() && !m_owner->IsServerSimulated() && movement->m_unknown1AC == 1;
    bool slid = false;
    if ((!ground || !standing) && m_fallSpeed != 0.0f) {
        accelerate(ground && !onWall);
        if (m_fallHeight == MOVE_NO_TIME) {
            const int anim = movement->m_animState.anim.anim;
            if (anim != ANIM_LAND_LOW && anim != ANIM_LAND_CROUCHED && anim != ANIM_LAND_HIGH) {
                // How high above the ground: the landing animation to play.
                movement->RemoveCollision(false);
                const S_vector4 &lowest = movement->m_stanceSpheres[0];
                S_vector hitPos, normal;
                float depth = 150.0f;
                bool test = true;
                if (CollisionRayTest(XYZ(lowest), S_vector(0.0f, -20.0f, 0.0f), &hitPos, &normal, 0xFFBFFFFF, 0x20000)) {
                    depth = std::fabs(pos.y - hitPos.y) + 0.5f;
                    test = depth > 1.0f;
                }
                if (test && movement->m_unknown1AC == 1) {
                    S_CollisionVolume *below = CollisionSphereTest(XYZ(lowest), S_vector(0.0f, -depth, 0.0f), lowest.w * 0.7f, &hitPos,
                                                                   &normal, 0xFFBFFFFF, 0x20000);
                    m_fallHeight = MOVE_NO_TIME;
                    m_fallGround = below ? (hitPos.y - lowest.w) + 0.1f : lowest.y - depth;
                    const float height = std::fabs(pos.y - m_fallGround);
                    if (pos.y < m_fallGround || height < 1.3f) {
                        m_fallHeight = std::fabs(depth - height) >= 1.0f ? MOVE_NO_TIME : 0;
                    } else if (height < 2.5f) {
                        m_fallHeight = 1;
                    } else if (height < 5.0f) {
                        m_fallHeight = 2;
                    } else {
                        // A long fall (a parachute lands high).
                        C_HumanWork *work = m_owner->GetWork();
                        if (work->m_request.stance == HUMAN_STANCE_STAND && !movement->m_parachuting) {
                            movement->m_nextAnimState.anim.anim = ANIM_FALLING;
                            movement->m_nextAnimState.anim.Normalize();
                            work->Play(ANIM_FALLING, false);
                        }
                        if (movement->m_parachuting)
                            m_fallHeight = 2;
                    }
                }
                movement->RestoreCollision(false);
            }
        }
        slid = !(LengthSquared(m_move) < 1e-16f || ground || replica || !slide || movement->m_onDynamicGround);
    } else if (!ground) {
        // Starting to fall.
        accelerate(false);
        slid = !(LengthSquared(m_move) < 1e-16f || replica || !slide);
    }
    if (slid) {
        // Off the edge: on along the move.
        m_owner->SetPosition4({pos.x + m_move.x * 0.9f, pos.y + m_move.y * 0.9f, pos.z + m_move.z * 0.9f, pos.w});
        m_positionChanged = true;
    }

    // The landing animation coming up.
    if (m_fallHeight == MOVE_NO_TIME || m_fallGround == MOVE_NO_VALUE || ground || movement->m_unknown1AC != 1 ||
        m_waterSurface != MOVE_NO_VALUE)
        return;
    const float y = m_owner->GetPosition4().y;
    const float ahead = static_cast<float>(time) * 0.001f * m_fallSpeed * 1.5f + m_fallGround;
    const int stance = m_owner->GetWork()->m_request.stance;
    S_HumanAnimRef &next = movement->m_nextAnimState.anim;
    if (m_fallHeight == 1) {
        if (ahead + 1.6f < y)
            return;
        if (stance == HUMAN_STANCE_STAND) {
            next.anim = ANIM_LAND_LOW;
            next.Normalize();
            return;
        }
        if (stance != HUMAN_STANCE_CROUCH)
            return;
    } else {
        if (m_fallHeight < 2 || m_fallHeight > 3)
            return;
        if (ahead + 2.9f < y)
            return;
        if (stance == HUMAN_STANCE_STAND) {
            next.anim = ANIM_LAND_HIGH;
            next.Normalize();
            return;
        }
    }
    next.anim = ANIM_LAND_CROUCHED;
    next.Normalize();
}

void C_HumanLocomotion::ApplyFallDamage(bool landed)
{
    float safe = 3.0f;
    float perMetre = 240.0f;
    if (m_waterSurface != MOVE_NO_VALUE) {
        safe = 6.0f;
        perMetre = 60.0f;
    }
    if (!IsSimulatedHere(m_owner))
        return;
    C_Human *owner = m_owner;
    // The damage is done on the server (or asked for by a client); none on a parachute and in
    // network games without fall damage.
    const auto damage = [&](float amount) {
        if (m_movement->m_parachuting || !(IsLocalGame() || g_mpSettings->fallingDamage != 0))
            return;
        if (amount > 1000.0f)
            amount = 1000.0f;
        if (IsServer()) {
            const S_vector zero(0.0f, 0.0f, 0.0f);
            owner->Hit(HIT_FALL, amount, zero, zero, owner, 0);
        } else {
            owner->PlayerRequestDamage(0, static_cast<int>(amount), owner->GetNetId(), 0);
        }
    };
    const float y = owner->GetPosition4().y;
    if (!landed) {
        // In the air: only a deadly fall counts now (unless into deep water).
        if (!(safe < m_fallStartY - y) || !(m_fallStartY < 100000.0f))
            return;
        float amount = ((m_fallStartY - y) - safe) * perMetre;
        if (!(static_cast<float>(owner->GetStat(0)) <= amount))
            return;
        S_CollisionVolume *water = CollisionRayTest(XYZ(owner->GetPosition4()), S_vector(0.0f, -50.0f, 0.0f), nullptr, nullptr, 0, 0);
        if (water && (water->flags & COLLISION_FLAG_WATER) && (water->flags & COLLISION_TRIANGLE_TWO_SIDED)) {
            amount = ((m_fallStartY - y) - 6.0f) * 240.0f * 0.25f;
            if (amount < static_cast<float>(owner->GetStat(0)))
                return;
        }
        damage(amount);
        m_fallStartY = MOVE_NO_VALUE;
        return;
    }
    if (m_fallStartY != MOVE_NO_VALUE && m_fallStartY < 100000.0f && safe < m_fallStartY - y)
        damage(((m_fallStartY - y) - safe) * perMetre);
    m_fallStartY = MOVE_NO_VALUE;
}

void C_HumanLocomotion::Land(bool hard)
{
    ApplyFallDamage(true);
    if (m_movement->m_animState.anim.anim == ANIM_FALLING || m_movement->m_parachuting)
        m_owner->GetWork()->Play(ANIM_STAND, false);
    if (hard) {
        if (m_fallSpeed > GRAVITY && m_owner->GetVelocity().y < -1.0f)
            m_movement->PlaySound(0x11);
        m_movement->m_parachuting = false;
    }
    m_fallSpeed = 0.0f;
    m_fallStepTime = 0.0f;
    m_fallHeight = MOVE_NO_TIME;
    m_fallGround = MOVE_NO_VALUE;
}

// ------------------------------------------------------------------ climbing, jumps, ladders

void C_HumanLocomotion::MoveClimbing(int time)
{
    C_HumanMovement *movement = m_movement;
    C_HumanAnimPlayer *player = m_animPlayer;
    const float ms = static_cast<float>(time);
    // The height the climb rises in this update (over m_unknown960..m_unknown964 ms of the
    // animation by m_unknown958 - m_unknown95C).
    float rise = 0.0f;
    player->m_unknown5C += time;
    movement->m_stillTime = 0;
    m_unknown0E8 = 3;
    movement->m_unknown968 += ms;
    const float sinceStart = movement->m_unknown968 - static_cast<float>(movement->m_unknown960);
    const float end = static_cast<float>(movement->m_unknown964);
    if (ms + sinceStart >= 0.0f && movement->m_unknown968 < end) {
        float part;
        if (ms + movement->m_unknown968 <= end)
            part = sinceStart > 0.0f ? ms : ms + sinceStart;
        else if (sinceStart >= 0.0f)
            part = end - movement->m_unknown968;
        else
            part = end - static_cast<float>(movement->m_unknown960);
        rise = ((movement->m_unknown958 - movement->m_unknown95C) * part) / (end - static_cast<float>(movement->m_unknown960));
    }
    I3D_frame *head = player->m_headDummy;
    if (!head)
        return;
    if (player->m_unknown60 != MOVE_NO_TIME && player->m_unknown60 < player->m_unknown5C) {
        movement->m_mixer.AddMove(player->m_unknown90, MoveTime(1000.0f));
        player->m_unknown60 = MOVE_NO_TIME;
    }
    // The body follows the head dummy of the animation (its move in the model's space).
    S_vector step = head->GetPos() - XYZ(player->m_unknown70);
    S_matrix parent = head->GetParent()->GetMatrix();
    parent.m[3][0] = parent.m[3][1] = parent.m[3][2] = 0.0f;
    step *= parent;
    player->m_unknown70 = ToVector4(head->GetPos(), head->GetPosW());
    if (IsSimulatedHere(m_owner) || movement->m_unknown1AC != 1) {
        const S_vector4 &mixed = movement->m_mixer.m_move;
        m_move.x = step.x + mixed.x;
        m_move.y = step.y + mixed.y + rise;
        m_move.z = step.z + mixed.z;
        const S_vector4 &pos = m_owner->GetPosition4();
        m_owner->SetPosition4({pos.x + m_move.x, pos.y + m_move.y, pos.z + m_move.z, pos.w});
        m_positionChanged = true;
    }
}

void C_HumanLocomotion::MoveJumping(int time)
{
    m_jumpTime += time;
    if (!MoveOnGround(time))
        return;
    // Landed: the jump's moves end.
    if ((m_jumpTime > 1 && IsLocalGame()) || (m_jumpTime > 0x50 && !IsLocalGame())) {
        const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};
        m_movement->m_mixer.AddMove(zero, 0);
        m_movement->m_mixer.Slide(zero, 0);
        m_animPlayer->m_animDelay = 0;
    }
}

void C_HumanLocomotion::MoveOnLadder(int time)
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    S_vector4 pos = m_owner->GetPosition4();
    if (!IsSimulatedHere(m_owner))
        return;
    UpdateVelocity(true);
    m_move = movement->m_mixer.m_move;
    movement->m_stillTime = 0;
    C_Actor *ladder = movement->m_useObject;
    // A little back from the ladder for the tests.
    const S_vector back = movement->m_unknown998 == MOVE_NO_TIME ? -XYZ(m_owner->GetDirection4()) : -XYZ(GetLadderDir(ladder));
    const S_vector away = back * 0.05f;
    const int anim = movement->m_animState.anim.anim;
    const auto keepClimbing = [&]() {
        // The walking animation goes on as the climbing one.
        C_HumanWork *bodyWork = movement->m_owner->GetWork();
        const int asked = bodyWork->m_request.anim;
        return IsOnLadder(asked) || asked == ANIM_LADDER_TOP_OFF || asked == ANIM_LADDER_TOP_ON;
    };
    // Touching something: its reaction, no move.
    const auto touch = [&](const S_vector &hitPos, const S_vector4 &normal, S_CollisionVolume *hit) {
        S_MoveContacts contacts;
        std::memset(&contacts, 0, sizeof(contacts));
        contacts.pos = ToVector4(hitPos, 0.0f);
        contacts.normals[0] = normal;
        contacts.count = 1;
        contacts.sphere[0] = 0;
        contacts.volumes[0] = hit;
        C_Actor *touched = nullptr;
        ProcessMoveContacts(m_owner->GetMovement(), &contacts, true, &touched);
    };
    // Off the top: over onto it (false: not possible, the ladder idle animation).
    const auto offTop = [&]() {
        S_HumanAnimRef &next = movement->m_nextAnimState.anim;
        next.anim = ANIM_CLIMB_OVER;
        next.Normalize();
        if (!m_specialMoves->TryClimbOver(&movement->m_nextAnimState)) {
            next.anim = ANIM_LADDER_IDLE;
            next.Normalize();
            return false;
        }
        movement->SetSeatMode(0, false);
        movement->m_useState = MOVE_NO_TIME;
        work->Play(ANIM_LADDER_IDLE, false);
        m_owner->GetEquipment()->PutAwayHandsItem(false);
        RemoveLadderClimber(ladder, m_owner);
        return true;
    };
    const auto distanceToTop = [&]() {
        return (XYZ(pos) - XYZ(GetLadderTop(ladder))).Magnitude();
    };

    if (anim == ANIM_LADDER_UP && movement->m_mixer.m_tracks[0].hold < 1) {
        // Up: the head end tested.
        if (keepClimbing())
            movement->m_owner->GetWork()->Play(ANIM_WALK, true);
        movement->RemoveCollision(false);
        movement->UpdateStanceVolumes(work->m_request.stance);
        const S_vector4 &top = movement->m_stanceSpheres[movement->m_stanceVolumeCount - 1];
        S_vector hitPos, normal;
        S_CollisionVolume *hit = CollisionSphereTest(XYZ(top) + away, XYZ(m_move), top.w - 0.05f, &hitPos, &normal, 0xCC002, 0xFFFFFFFF);
        movement->RestoreCollision(false);
        if (hit) {
            touch(hitPos, ToVector4(normal, 0.0f), hit);
            return;
        }
        pos.x += m_move.x;
        pos.y += m_move.y;
        pos.z += m_move.z;
        if (distanceToTop() <= 1.7f && !offTop())
            return;
    } else if ((anim == ANIM_LADDER_DOWN || anim == ANIM_LADDER_SLIDE) && movement->m_mixer.m_tracks[0].hold < 1) {
        // Down: the feet end tested.
        if (keepClimbing())
            movement->m_owner->GetWork()->Play(ANIM_WALK_BACK, true);
        movement->RemoveCollision(false);
        movement->UpdateStanceVolumes(work->m_request.stance);
        const S_vector4 &lowest = movement->m_stanceSpheres[0];
        S_vector hitPos, normal;
        S_CollisionVolume *hit = CollisionSphereTest(XYZ(lowest) + away, XYZ(m_move), lowest.w - 0.05f, &hitPos, &normal, 0xCC002,
                                                     0xFFFFFFFF);
        movement->RestoreCollision(false);
        if (hit) {
            touch(hitPos, ToVector4(normal, 0.0f), hit);
            return;
        }
        pos.x += m_move.x;
        pos.y += m_move.y;
        pos.z += m_move.z;
        const float bottom = GetLadderBottom(ladder).y;
        if (pos.y < bottom) {
            // At the bottom: off the ladder, a step back from it.
            if (!IsLocalGame())
                movement->m_owner->SendRemoteRequest(5);
            pos.y = bottom;
            movement->m_useState = MOVE_NO_TIME;
            S_HumanAnimRef &next = movement->m_nextAnimState.anim;
            next.anim = ANIM_STAND;
            next.Normalize();
            movement->m_unknown98C = true;
            const S_vector4 &facing = m_owner->GetDirection4();
            S_vector4 step;
            if (!GetLadderFlag180(ladder))
                step = {facing.x * -0.15f, facing.y * -0.15f, facing.z * -0.15f, facing.w};
            else
                step = {facing.x * -1.25f, facing.y * -1.25f - 0.25f, facing.z * -1.25f, facing.w};
            movement->m_mixer.AddMove(step, 200);
            movement->SetSeatMode(0, false);
            work->Play(ANIM_STAND, false);
            m_owner->GetEquipment()->PutAwayHandsItem(false);
            RemoveLadderClimber(ladder, m_owner);
        }
    } else {
        if (!((LengthSquared(m_move) >= 1e-16f && anim < ANIM_WALK) || movement->m_mixer.m_tracks[0].hold > 0))
            return;
        // Getting on / held: towards the climbing position.
        pos.x += m_move.x;
        pos.y += m_move.y;
        pos.z += m_move.z;
        if (distanceToTop() <= 1.7f && movement->m_animState.anim.anim == ANIM_LADDER_UP) {
            const int next = movement->m_nextAnimState.anim.anim;
            if ((next == ANIM_LADDER_UP || next == ANIM_LADDER_IDLE) && !offTop())
                return;
        }
        GetLadderPos(&pos);
        work->m_forced = true;
        work->SetFacingDir(XYZ(GetLadderDir(ladder)), true);
        work->m_forced = false;
        // At 1 m/s at most.
        const S_vector4 &own = m_owner->GetPosition4();
        const S_vector delta = XYZ(pos) - XYZ(own);
        const float length = delta.Magnitude();
        if (static_cast<float>(time) * 0.001f < length) {
            const S_vector to = XYZ(own) + delta * (1.0f / length) * 0.001f * static_cast<float>(time);
            pos = ToVector4(to, pos.w);
        }
        m_owner->SetPosition4(pos);
        m_positionChanged = true;
        return;
    }
    m_owner->SetPosition4(pos);
    GetLadderPos(&pos);
    m_owner->SetPosition4(pos);
    m_positionChanged = true;
}

void C_HumanLocomotion::GetLadderPos(S_vector4 *pos)
{
    const S_vector4 &own = m_owner->GetPosition4();
    C_Actor *ladder = m_movement->m_useObject;
    if (ladder) {
        // The point of the ladder at the body's height (between its ends), 0.17 m in front.
        const S_vector4 &top = GetLadderTop(ladder);
        const S_vector4 &bottom = GetLadderBottom(ladder);
        const S_vector4 &dir = GetLadderDir(ladder);
        float y = own.y;
        if (y > top.y)
            y = top.y;
        else if (y < bottom.y)
            y = bottom.y;
        const float share = (y - bottom.y) / (top.y - bottom.y);
        const float dx = (own.x - ((top.x - bottom.x) * share + bottom.x)) + dir.x * 0.17f;
        const float dz = (own.z - ((top.z - bottom.z) * share + bottom.z)) + dir.z * 0.17f;
        if (dx * dx + dz * dz != 0.0f) {
            *pos = {own.x - dx, own.y, own.z - dz, own.w};
            return;
        }
    }
    *pos = own;
}

// ------------------------------------------------------------------ swimming

void C_HumanLocomotion::MoveSwimming(int time)
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    const float seconds = static_cast<float>(time) * 0.001f;
    RelaxRoll(movement, time);
    S_MoveContacts contacts;
    std::memset(&contacts, 0, sizeof(contacts));
    m_inWater = false;
    work->m_bodyFlags |= 0x20;
    work->m_bodyFlags |= 0x400;
    if (m_waterTransition > 0) {
        m_waterTransition -= time;
        if (m_waterTransition < 0)
            m_waterTransition = 0;
    }
    if (!m_specialMoves->UpdateWater() && m_waterTransition == 0) {
        StopSwimming(true);
        return;
    }
    movement->UpdateStanceVolumes(HUMAN_STANCE_PRONE);
    if (movement->m_stanceVolumeCount == 0)
        return;
    const S_vector offset = XYZ(m_owner->GetPosition4()) - XYZ(movement->m_stanceSpheres[0]);
    m_move = movement->m_mixer.m_move;
    if (m_shiftTime != MOVE_NO_TIME)
        m_shiftTime = m_shiftTime - time < 1 ? MOVE_NO_TIME : m_shiftTime - time;
    // The swimming direction: kept level at the surface and over shallow ground.
    S_vector4 dir = movement->m_unknown940;
    if (m_waterHeight - m_move.y >= 0.12f) {
        if (!((m_waterHeight + 0.4f) - m_move.y <= m_waterDepth)) {
            if (m_move.y < 0.0f)
                m_move.y = 0.0f;
            dir.y = 0.0f;
        }
    } else {
        if (m_move.y > 0.0f)
            m_move.y = 0.0f;
        dir.y = 0.0f;
    }
    if (LengthSquared(m_move) < 1e-16f) {
        dir = ToVector4(Flat(XYZ(dir)), dir.w);
        PitchTowards(dir, time);
        if (movement->m_swimBobPhase == 0.0f)
            return;
    } else {
        PitchTowards(dir, time);
    }
    if (movement->m_swimBobPhase != 0.0f && m_waterHeight > 0.16f) {
        // Bobbing at the surface.
        const double scale = m_waterHeight >= 0.26f ? 0.125 : 0.03571428571428571;
        m_move.y = static_cast<float>(std::sin(static_cast<double>(movement->m_swimBobPhase)) * seconds * scale + m_move.y);
    }
    if (!(IsSimulatedHere(m_owner) || movement->m_unknown1AC != 1))
        return;
    for (int i = 0; i < movement->m_stanceVolumeCount; ++i) {
        const S_vector4 &sphere = movement->m_stanceSpheres[i];
        contacts.radii[i] = sphere.w;
        contacts.spheres[i] = sphere;
        contacts.sphereCount = movement->m_stanceVolumeCount;
        contacts.excludeTypes = TEST_TYPES;
        contacts.excludeFlags = TEST_FLAGS;
        contacts.move = m_move;
    }
    movement->RemoveCollision(false);
    S_CollisionVolume *hit = g_collisionWorld.MoveSpheres(contacts);
    movement->RestoreCollision(false);
    contacts.pos.x += offset.x;
    contacts.pos.y += offset.y;
    contacts.pos.z += offset.z;
    const S_vector4 &pos = m_owner->GetPosition4();
    if (!hit) {
        m_owner->SetPosition4({pos.x + m_move.x, pos.y + m_move.y, pos.z + m_move.z, pos.w});
        m_positionChanged = true;
    } else if (contacts.pos.x != pos.x || contacts.pos.y != pos.y || contacts.pos.z != pos.z) {
        C_Actor *touched = nullptr;
        ProcessMoveContacts(m_owner->GetMovement(), &contacts, true, &touched);
        m_owner->SetPosition4(contacts.pos);
        m_positionChanged = true;
    }
}

void C_HumanLocomotion::PitchTowards(const S_vector4 &target, int time)
{
    S_vector4 facing = m_owner->GetDirection4();
    S_vector dir = XYZ(target);
    if (dir.x * dir.x + dir.z * dir.z + dir.y * dir.y < 1e-16f) {
        dir.x = facing.x;
        dir.z = facing.z;
    }
    dir.Normalize();
    const S_vector up(0.0f, 1.0f, 0.0f);
    // 1 rad per 650 ms at most.
    const float most = static_cast<float>(time) * 0.0015384615f;
    float pitch = XYZ(facing).AngleTo(up) - dir.AngleTo(up);
    if (!(std::fabs(pitch) > 0.0001f))
        return;
    if (most < std::fabs(pitch))
        pitch = pitch <= 0.0f ? -most : most;
    // Pitched about the body's side: turned to face z, rotated about x, turned back.
    const float north[2] = {0.0f, 1.0f};
    const float flat[2] = {facing.x, facing.z};
    const float yaw = -AngleBetween2D(north, flat);
    S_vector v = XYZ(facing);
    v *= RotateYMatrix(yaw);
    S_vector4 rotated = ToVector4(v, 0.0f);
    RotateAboutX(rotated, pitch);
    v = XYZ(rotated);
    v *= RotateYMatrix(-yaw);
    v.Normalize();
    if (IsSimulatedHere(m_owner) || m_movement->m_unknown1AC != 1) {
        m_owner->SetDirection4(ToVector4(v, facing.w));
        m_directionChanged = true;
    }
}

void C_HumanLocomotion::StartSwimming()
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    if (m_fallStartY != MOVE_NO_VALUE)
        ApplyFallDamage(true);
    movement->m_morph.SetExhausted(false, true);
    work->m_forced = true;
    const int anim = movement->m_animState.anim.anim;
    int swim;
    if (anim == ANIM_WALK || anim == 0xC6) {
        m_swimBack = false;
        swim = m_owner->IsWearingDivingGear() ? ANIM_DIVE : ANIM_SWIM;
    } else if (anim == ANIM_WALK_BACK || anim == ANIM_WALK_BACK_2) {
        m_swimBack = true;
        swim = m_owner->IsWearingDivingGear() ? ANIM_DIVE : ANIM_SWIM;
    } else {
        m_swimBack = false;
        swim = m_owner->IsWearingDivingGear() ? ANIM_DIVE_STILL : ANIM_SWIM_STILL;
    }
    work->Play(swim, true);
    work->m_forced = false;
    movement->SetSeatMode(4, true);
    work->m_bodyFlags |= 0x20;
    work->m_bodyFlags |= 0x400;
    m_owner->GetEquipment()->PutAwayHandsItem(true);
    // Up to the surface (0.15 m under it at most 0.5 m).
    float rise = 0.5f;
    if (m_waterHeight != MOVE_NO_VALUE && m_waterHeight - 0.15f < 0.5f)
        rise = m_waterHeight - 0.15f;
    movement->m_mixer.AddMove({0.0f, rise, 0.0f, 0.0f}, MoveTime(800.0f));
    m_waterTransition = MoveTime(1200.0f);
}

void C_HumanLocomotion::StopSwimming(bool putAway)
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    movement->SetSeatMode(4, false);
    work->m_forced = true;
    const int stance = work->m_request.stance;
    if (stance == HUMAN_STANCE_STAND || stance == HUMAN_STANCE_CROUCH || stance == HUMAN_STANCE_PRONE)
        work->Play(ANIM_WALK, true);
    work->m_forced = false;
    movement->m_unknown764 = -1;
    // Down onto the ground (0.5 m at most).
    float drop = 0.5f;
    if (m_waterHeight != MOVE_NO_VALUE && m_waterHeight < 0.5f)
        drop = m_waterHeight;
    movement->m_mixer.AddMove({0.0f, -drop, 0.0f, 0.0f}, 500);
    m_waterTransition = -300;
    if (putAway)
        m_owner->GetEquipment()->PutAwayHandsItem(false);
    StopSwimSound(false);
}

// ------------------------------------------------------------------ dying and dead

void C_HumanLocomotion::MoveDying(int time)
{
    C_HumanMovement *movement = m_movement;
    if (movement->m_unknown1AC < 7) {
        // (States 2..6, those of most deaths, end here at once: the body is dead where the
        // death animation leaves it. The tests of those states below never hold.)
        movement->UpdateHitSpheres();
        MoveHitVolumes(movement);
        movement->m_unknown9C0 = 0;
        movement->m_unknown9B8 = 0;
        movement->UpdateStanceVolumes(m_owner->GetWork()->m_request.stance);
        if (movement->m_shadow)
            movement->m_shadow->OrFrameFlags(0x80);
        movement->m_unknown1AC = 0;
        movement->m_unknown18C = true;
        movement->m_morph.ShowDeathFace();
        return;
    }
    if (movement->m_unknown9D0.x == -10000.0f) {
        movement->m_unknown9D0.x = 0.0f;
        return;
    }
    if (movement->m_unknown9CC != MOVE_NO_TIME) {
        movement->m_unknown9CC -= time;
        if (movement->m_unknown9CC < 1) {
            // Settled.
            movement->m_unknown1AC = 9;
            movement->m_unknown18C = true;
            movement->m_unknown9CC = 0;
        }
    }
    movement->m_unknown9B8 -= time;
    if (movement->m_unknown9B8 < 0)
        movement->m_unknown9B8 = 0;
    movement->m_unknown9BC -= time;
    if (movement->m_unknown9BC < 0) {
        if (movement->m_unknown1AC == 2 || movement->m_unknown1AC == 9)
            movement->m_mixer.SetVelocity(m_owner->GetDirection4(), 0.0f, 0.0f, &m_velocity);
        movement->m_unknown9BC = 0;
    }
    if (movement->m_unknown1AC != 3 && movement->m_unknown1AC != 4) {
        movement->m_unknown9C0 -= time;
        if (movement->m_unknown9C0 < 0) {
            // Dead.
            movement->m_unknown9C0 = 0;
            movement->m_unknown1AC = 0;
            movement->m_unknown18C = true;
            movement->m_morph.ShowDeathFace();
            if (movement->m_shadow)
                movement->m_shadow->OrFrameFlags(0x80);
        }
        movement->m_unknown9C4 -= time;
        if (movement->m_unknown9C4 < 0)
            movement->m_unknown9C4 = 0;
    }
    const int state = movement->m_unknown1AC;
    if (state == 8) {
        // Back to level (0.9 per s at most).
        S_vector4 dir = m_owner->GetDirection4();
        if (dir.y == 0.0f)
            return;
        float pitch = -dir.y;
        if (std::fabs(pitch) >= 0.05f) {
            const float most = static_cast<float>(time) * 0.0009f;
            if (most < std::fabs(pitch))
                pitch = (pitch <= 0.0f ? -1.0f : 1.0f) * most;
            pitch += dir.y;
        } else {
            pitch = 0.0f;
            movement->m_unknown98C = false;
        }
        const S_vector level = S_vector(dir.x, pitch, dir.z).Normalized();
        m_owner->SetDirection4(ToVector4(level, dir.w));
        m_directionChanged = true;
        return;
    }
    if (state == 2 || state == 6 || state == 0 || state == 9 ||
        ((state == 4 || state == 3) && movement->m_unknown9CC != MOVE_NO_TIME))
        AlignDeadBody(time);
    m_deadSlideTime += time;
    if (m_deadSlideTime > 99) {
        m_deadSlideTime -= 100;
        const int now = movement->m_unknown1AC;
        if (now == 3 || now == 6 || now == 2)
            MoveDeadBody(100);
    }
}

bool C_HumanLocomotion::GetSlideOff(S_vector4 *dir)
{
    C_HumanMovement *movement = m_movement;
    const S_vector down(0.0f, -1.85f, 0.0f);
    const S_vector head = movement->m_joints[HUMAN_JOINT_F1].frame->GetWorldPos();
    const S_vector chest = movement->m_joints[HUMAN_JOINT_C1].frame->GetWorldPos();
    const S_vector along = chest - head;
    // Below the body ahead of the head, at the chest and beyond the chest (0.2 m up).
    const S_vector headSide = chest - along * 1.2f + S_vector(0.0f, 0.2f, 0.0f);
    const S_vector middle = chest + S_vector(0.0f, 0.2f, 0.0f);
    const S_vector farSide = chest + along * 1.5f + S_vector(0.0f, 0.2f, 0.0f);
    dir->x = dir->y = dir->z = 0.0f;
    S_vector hitPos, headNormal, middleNormal, farNormal;
    movement->RemoveCollision(false);
    S_CollisionVolume *headHit = CollisionRayTest(headSide, down, &hitPos, &headNormal, 0xFFFFFFFF, TEST_FLAGS);
    S_CollisionVolume *middleHit = CollisionRayTest(middle, down, &hitPos, &middleNormal, 0xFFFFFFFF, TEST_FLAGS);
    S_CollisionVolume *farHit = CollisionRayTest(farSide, down, &hitPos, &farNormal, 0xFFFFFFFF, TEST_FLAGS);
    movement->RestoreCollision(false);
    // Ground under one end only (and flat there): slides off towards the other end.
    S_vector slide;
    if (!headHit) {
        if (middleHit || !farHit || !(farNormal.y > 0.7f))
            return false;
        slide = -along;
    } else {
        if (middleHit || farHit || !(headNormal.y > 0.7f))
            return false;
        slide = along;
    }
    slide = Flat(slide) * 0.25f;
    dir->x = slide.x;
    dir->y = slide.y;
    dir->z = slide.z;
    return true;
}

void C_HumanLocomotion::MoveDeadBody(int time)
{
    C_HumanMovement *movement = m_movement;
    const float ms = static_cast<float>(time);
    const float seconds = ms * 0.001f;
    S_MoveContacts contacts;
    std::memset(&contacts, 0, sizeof(contacts));
    S_GroundContact ground;
    const S_vector4 pos = m_owner->GetPosition4();
    const S_vector offset = XYZ(pos) - XYZ(movement->m_stanceSpheres[0]);
    if (movement->m_unknown9CC == MOVE_NO_TIME)
        movement->m_unknown9CC = 1400;
    m_move = movement->m_mixer.m_move;
    m_move.y -= FallDistance(time, m_fallSpeed, m_fallStepTime);
    for (int i = 0; i < movement->m_stanceVolumeCount; ++i) {
        const S_vector4 &sphere = movement->m_stanceSpheres[i];
        contacts.radii[i] = sphere.w * 0.85f;
        contacts.spheres[i] = sphere;
        contacts.sphereCount = movement->m_stanceVolumeCount;
        contacts.excludeTypes = 0xCC002;
        contacts.excludeFlags = TEST_FLAGS;
        contacts.move = m_move;
    }
    movement->RemoveCollision(false);
    S_CollisionVolume *hit = g_collisionWorld.MoveSpheres(contacts);
    movement->RestoreCollision(false);
    S_CollisionVolume *groundVolume = nullptr;
    ground.standing = FindGroundContact(contacts, true, &ground.normal, &groundVolume);
    if (LengthSquared(ground.normal) >= 1e-16f)
        ground.hasNormal = true;
    // (Passed no time: the fall does not speed up here.)
    UpdateFall(hit, ground.standing, 0, time != 0, true);
    S_vector4 slide;
    if (GetSlideOff(&slide)) {
        ground.standing = false;
        movement->m_mixer.AddMove2(slide, MoveTime(300.0f));
    }
    if (!hit) {
        movement->m_unknown9CC += time;
        if (IsSimulatedHere(m_owner)) {
            m_owner->SetPosition4({pos.x + m_move.x, pos.y + m_move.y, pos.z + m_move.z, pos.w});
            m_positionChanged = true;
        }
        return;
    }
    // On a slope: settles later, slides down it.
    const float speed = XYZ(m_move).Magnitude();
    const S_vector4 &last = LastContactNormal(contacts);
    const float tilt = XYZ(last).AngleTo(S_vector(0.0f, 1.0f, 0.0f));
    const float slope = ground.hasNormal ? tilt * TWO_OVER_PI : 0.0f;
    if (!ground.standing) {
        movement->m_unknown9CC = static_cast<int>(ms * slope + static_cast<float>(movement->m_unknown9CC));
        m_fallSpeed -= (1.0f - slope) * GRAVITY * seconds;
    } else {
        movement->m_unknown9CC = static_cast<int>(static_cast<float>(movement->m_unknown9CC) - static_cast<float>(time / 2) * (1.0f - slope));
        m_fallSpeed -= (1.0f - slope) * GRAVITY * seconds;
        if (m_fallSpeed < 0.0f)
            m_fallSpeed = 0.0f;
    }
    if (contacts.count != 0) {
        const float perSecond = 1000.0f / ms;
        if (speed * perSecond > 0.4f) {
            // Moving fast into something: bounces off it (the move mirrored about the
            // contact's normal), slower the more head-on.
            const float inverse = 1.0f / speed;
            m_move.x *= inverse;
            m_move.y *= inverse;
            m_move.z *= inverse;
            const S_vector moveDir = XYZ(m_move);
            const float angle = (-moveDir).AngleTo(XYZ(last));
            float headOn = angle * TWO_OVER_PI;
            if (headOn > 1.0f)
                headOn = 1.0f;
            const S_vector axis = moveDir.Cross(XYZ(last));
            S_vector bounce = -moveDir;
            if (axis.Square() >= 1e-16f) {
                S_quat q;
                q.Make(axis.Normalized(), angle + angle);
                bounce *= q.RotationMatrix();
            }
            const float factor = (1.0f - headOn) * speed * 0.5f;
            if (perSecond * factor > 0.15f)
                movement->m_mixer.Slide(ToVector4(bounce * factor, 0.0f), MoveTime(1000.0f));
        }
    }
    C_Actor *touched = nullptr;
    ProcessMoveContacts(m_owner->GetMovement(), &contacts, false, &touched);
    if (IsSimulatedHere(m_owner) || movement->m_unknown1AC != 1) {
        m_owner->SetPosition4({contacts.pos.x + offset.x, contacts.pos.y + offset.y, contacts.pos.z + offset.z, pos.w});
        m_positionChanged = true;
    }
}

void C_HumanLocomotion::AlignDeadBody(int time)
{
    C_HumanMovement *movement = m_movement;
    if (movement->m_unknown9C0 == MOVE_NO_TIME || !movement->m_unknown9C8)
        return;
    const int anim = movement->m_animState.anim.anim;
    // Falling to a side (those death animations keep the roll).
    const bool sideLeft = anim == 0x8B || anim == 0x81;
    const bool sideRight = anim == 0x8C || anim == 0x82;
    const bool onBack = movement->m_unknownA10 == 0x40;
    const S_vector4 facing = m_owner->GetDirection4();
    const S_vector head = movement->m_joints[HUMAN_JOINT_F1].frame->GetWorldPos();
    const float previousRoll = movement->m_unknown250;
    m_pushTime = MOVE_NO_TIME;
    const S_vector flat = Flat(XYZ(facing));
    const float sign = onBack ? -1.0f : 1.0f;
    // Rays from the head: ahead and down, along the body, back.
    const S_vector ahead(flat.x * sign * 0.5f, -1.3f, flat.z * sign * 0.5f);
    const S_vector along(flat.x * sign * 1.3f, -1.8f, flat.z * sign * 1.3f);
    const S_vector back(-flat.x * sign * 0.43478262f, -1.8f, -flat.z * sign * 0.43478262f);
    const S_vector4 &own = m_owner->GetPosition4();
    const S_vector middle = XYZ(own) + (S_vector(head.x, head.y + 0.6f, head.z) - XYZ(own)) * 0.5f;
    const S_vector up(XYZ(own).x, own.y + 0.6f, own.z);
    S_vector belowPos, belowNormal, hitPos, hitNormal, middlePos, middleNormal;
    movement->RemoveCollision(false);
    S_CollisionVolume *below = CollisionSphereTest(up, S_vector(0.0f, -2.5f, 0.0f), 0.15f, &belowPos, &belowNormal, 0xFFFFFFFF, TEST_FLAGS);
    S_CollisionVolume *hit = CollisionRayTest(head + (-ahead).Normalized() * 0.3f, ahead, &hitPos, &hitNormal, 0xFFFFFFFF, TEST_FLAGS);
    if (!hit) {
        hit = CollisionSphereTest(head + (-along).Normalized() * 0.3f, along, 0.3f, &hitPos, &hitNormal, 0xFFFFFFFF, TEST_FLAGS);
        if (!hit) {
            hit = CollisionSphereTest(head + (-along).Normalized(), along, 0.3f, &hitPos, &hitNormal, 0xFFFFFFFF, TEST_FLAGS);
            if (!hit)
                hit = CollisionSphereTest(head + (-back).Normalized() * 0.3f, back, 0.3f, &hitPos, &hitNormal, 0xFFFFFFFF, TEST_FLAGS);
        }
    } else {
        hitPos.y += 0.3f;
    }
    S_CollisionVolume *middleHit = CollisionSphereTest(middle, S_vector(0.0f, -1.5f, 0.0f), 0.3f, &middlePos, &middleNormal, 0xFFFFFFFF,
                                                       TEST_FLAGS);
    movement->RestoreCollision(false);
    if (!hit)
        return;
    const S_vector4 pos = m_owner->GetPosition4();
    float y = pos.y;
    const float ms = static_cast<float>(time);
    float pitchStep, yStep;
    if (movement->m_unknown9CC == MOVE_NO_TIME) {
        pitchStep = ms * 0.00086666667f;
        yStep = ms * 0.001f;
    } else {
        pitchStep = ms * 0.003f;
        yStep = ms * 0.0015f;
    }
    if (below) {
        // Down onto the ground below (not into the slope between the head and the middle).
        belowPos.y -= 0.14285715f;
        hitPos.y -= 0.3f;
        middlePos.y -= 0.3f;
        float to = belowPos.y;
        if (yStep < std::fabs(y - belowPos.y))
            to = (belowPos.y <= y ? -yStep : yStep) + y;
        y = to;
        if (middleHit) {
            const float rise = middlePos.y - hitPos.y;
            const float span = (hitPos - middlePos).Magnitude();
            const float reach = (belowPos - middlePos).Magnitude();
            if (y < (reach / span + 1.0f) * rise + hitPos.y && y < m_owner->GetPosition4().y) {
                y = m_owner->GetPosition4().y;
                belowPos.y = y;
            }
        }
    }
    // The body along the line from the head's contact.
    S_vector dir = onBack ? S_vector(pos.x, y, pos.z) - hitPos : hitPos - S_vector(pos.x, y, pos.z);
    dir.Normalize();
    const S_vector average = (belowNormal + hitNormal) * 0.5f;
    const float facingXZ[2] = {facing.x, facing.z};
    const float averageXZ[2] = {average.x, average.z};
    const float side = AngleBetween2D(facingXZ, averageXZ);
    const float tilt = average.AngleTo(S_vector(0.0f, 1.0f, 0.0f));
    const float dirXZ[2] = {dir.x, dir.z};
    const float yaw = -AngleBetween2D(facingXZ, dirXZ);
    dir *= RotateYMatrix(yaw);
    dir.Normalize();
    const S_vector4 &now = m_owner->GetDirection4();
    if (pitchStep < std::fabs(now.y - dir.y)) {
        dir.y = now.y + (dir.y <= now.y ? -pitchStep : pitchStep);
        dir.Normalize();
    }
    // A change of the pitch's way holds the pitch for a while (6 updates the same way to
    // turn, then about 50 / 90 updates).
    const float current = m_owner->GetDirection4().y;
    if (!(dir.y <= current)) {
        // Pitching up.
        if (movement->m_unknownA14 == 1) {
            if (static_cast<float>(++movement->m_unknownA18) >= 6.0f)
                movement->m_unknownA18 = static_cast<int>(6.0f + 45.0f - static_cast<float>(time / 10));
        } else if (movement->m_unknownA18 > 0) {
            --movement->m_unknownA18;
        }
        movement->m_unknownA14 = 0;
    } else if (dir.y < current) {
        // Pitching down.
        if (movement->m_unknownA14 == 0) {
            if (static_cast<float>(++movement->m_unknownA18) >= 6.0f)
                movement->m_unknownA18 = static_cast<int>(6.0f + 85.0f - static_cast<float>(time / 10));
        } else if (movement->m_unknownA18 > 0) {
            --movement->m_unknownA18;
        }
        movement->m_unknownA14 = 1;
    } else if (movement->m_unknownA18 > 0) {
        --movement->m_unknownA18;
    }
    if (static_cast<float>(movement->m_unknownA18) < 6.0f && anim != 0x80 && anim != 0x8A) {
        if (!sideRight && !sideLeft) {
            // Rolled along the slope across the body.
            float roll = side <= 0.0f ? -tilt : tilt;
            float across = std::fabs(side);
            if (across > HALF_PI)
                across = 3.14159274f - across;
            roll = across * roll * TWO_OVER_PI;
            const float most = ms * 0.001f;
            if (most < std::fabs(roll - previousRoll))
                roll = roll <= previousRoll ? previousRoll - most : most + previousRoll;
            movement->m_unknown250 = roll;
        }
        m_owner->SetDirection4(ToVector4(dir, facing.w));
        m_directionChanged = true;
    }
    if (IsSimulatedHere(m_owner) || movement->m_unknown1AC != 1) {
        m_owner->SetPosition4({pos.x, y, pos.z, pos.w});
        m_positionChanged = true;
    }
    // Where the blood pools: under a joint (the head, the chest or the belly) while dying.
    if (movement->m_unknown1AC != 1 && movement->m_unknown9C0 < 200 && movement->m_unknown9F0.z == MOVE_NO_VALUE) {
        const float r = RandFloat();
        int joint = HUMAN_JOINT_F1;
        if (r > 0.6f)
            joint = r > 0.85f ? HUMAN_JOINT_C1 : HUMAN_JOINT_D1;
        const S_vector at = movement->m_joints[joint].frame->GetWorldPos();
        S_vector poolPos, poolNormal;
        if (CollisionRayTest(S_vector(at.x, at.y + 0.1f, at.z), S_vector(0.0f, -0.4f, 0.0f), &poolPos, &poolNormal, 0xFFFFFFFF,
                             TEST_FLAGS)) {
            movement->m_unknown9F0 = ToVector4(poolPos, movement->m_unknown9F0.w);
            movement->m_unknownA00 = ToVector4(poolNormal, movement->m_unknownA00.w);
        }
    }
}

void C_HumanLocomotion::UpdatePitch(int time)
{
    C_HumanMovement *movement = m_movement;
    const S_vector4 facing = m_owner->GetDirection4();
    if (IsOnLadder(movement->m_animState.anim.anim)) {
        // On a ladder: pitched along it (0.15 per update, 1.8 per s at most).
        C_Actor *ladder = movement->m_useObject;
        if (!ladder)
            return;
        const S_vector slant = XYZ(GetLadderTop(ladder)) - XYZ(GetLadderBottom(ladder));
        float pitch = slant.AngleTo(S_vector(0.0f, 1.0f, 0.0f)) * -0.8333333f - facing.y;
        if (std::fabs(pitch) < 1e-8f)
            return;
        const float most = static_cast<float>(time) * 0.0018f;
        if (most < std::fabs(pitch))
            pitch = (pitch <= 0.0f ? -1.0f : 1.0f) * most;
        if (std::fabs(pitch) > 0.15f)
            pitch = (pitch <= 0.0f ? -1.0f : 1.0f) * 0.15f;
        const S_vector dir = S_vector(facing.x, facing.y + pitch, facing.z).Normalized();
        m_owner->SetDirection4(ToVector4(dir, facing.w));
        return;
    }
    if (facing.y != 0.0f) {
        // Back to level: slowly at first, fast once under way.
        float pitch = -facing.y;
        if (std::fabs(pitch) >= 0.05f) {
            const float rate = movement->m_unknown98C ? 0.018f : 0.0009f;
            movement->m_unknown98C = true;
            const float most = static_cast<float>(time) * rate;
            if (most < std::fabs(pitch))
                pitch = (pitch <= 0.0f ? -1.0f : 1.0f) * most;
            pitch += facing.y;
        } else {
            pitch = 0.0f;
            movement->m_unknown98C = false;
        }
        const S_vector dir = S_vector(facing.x, pitch, facing.z).Normalized();
        m_owner->SetDirection4(ToVector4(dir, facing.w));
        m_directionChanged = true;
    }
    RelaxRoll(movement, time);
}

// ------------------------------------------------------------------ stance changes

void C_HumanLocomotion::ChangeStance()
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    const int anim = movement->m_animState.anim.anim;
    S_HumanAnimRef &next = movement->m_nextAnimState.anim;
    if (IsClimbing(anim)) {
        // Climbing: back to standing at the end (the stance kept for 200 ms).
        if (IsSingleStageAnim(anim) && work->m_request.stance != HUMAN_STANCE_STAND)
            return;
        // (The original tests the animation again for anything but climbing here, which never
        // holds: the stance kept, the timer set.)
        next.anim = ANIM_STAND;
        next.Normalize();
        next.stance = movement->m_request.stance;
        next.Normalize();
        work->Play(ANIM_STAND, false);
        work->m_refused |= WORK_REQUEST_STANCE;
        work->SetStanceTimer(MoveTime(200.0f), work->m_request.stance, 9);
        return;
    }
    // Moving: the change animation is left out (the mixer blends the stances).
    const bool moving = anim >= ANIM_WALK && movement->m_animState.anim.sub != 0;
    const int current = movement->m_request.stance;
    const int asked = work->m_request.stance;
    C_HumanWeaponPose *pose = movement->m_weaponPose2;
    // No room: the work keeps the stance.
    const auto refuse = [&]() {
        work->SetStance(movement->m_request.stance, false);
        work->m_refused |= WORK_REQUEST_STANCE;
    };
    // The change starts (`time` ms to the stance in m_nextStance).
    const auto start = [&](int time, int from) {
        movement->m_stanceChange = 1;
        movement->m_unknown9A0 = time;
        movement->m_nextStance = from;
        movement->m_nextStance2 = 3;
    };
    const auto play = [&](int changeAnim) {
        next.anim = changeAnim;
        next.Normalize();
    };
    if (current == HUMAN_STANCE_STAND) {
        if (asked == HUMAN_STANCE_CROUCH) {
            m_specialMoves->HasRoomForStance(3);        // (a step back from a wall; the result is not used)
            pose->SetStanceChange(asked);
            start(MoveTime(400.0f), HUMAN_STANCE_STAND);
            if (!moving)
                play(ANIM_STAND_TO_CROUCH);
        } else if (asked == HUMAN_STANCE_PRONE) {
            if (!m_specialMoves->HasRoomForStance(0)) {
                refuse();
                return;
            }
            pose->SetStanceChange(asked);
            m_owner->GetEquipment()->SetLongGunLift(asked == HUMAN_STANCE_PRONE);
            start(MoveTime(220.0f), HUMAN_STANCE_STAND);
            play(ANIM_STAND_TO_LIE);
        }
    } else if (current == HUMAN_STANCE_CROUCH) {
        if (asked == HUMAN_STANCE_STAND) {
            if (!m_specialMoves->HasRoomForStance(2)) {
                refuse();
                return;
            }
            pose->SetStanceChange(asked);
            m_owner->GetEquipment()->SetLongGunLift(asked == HUMAN_STANCE_PRONE);
            if (!moving)
                play(ANIM_CROUCH_TO_STAND);
        } else if (asked == HUMAN_STANCE_PRONE) {
            if (!m_specialMoves->HasRoomForStance(1)) {
                refuse();
                return;
            }
            pose->SetStanceChange(asked);
            m_owner->GetEquipment()->SetLongGunLift(asked == HUMAN_STANCE_PRONE);
            start(MoveTime(900.0f), HUMAN_STANCE_CROUCH);
            play(ANIM_CROUCH_TO_LIE);
        }
    } else if (current == HUMAN_STANCE_PRONE) {
        if (asked == HUMAN_STANCE_STAND) {
            if (!m_specialMoves->HasRoomForStance(4)) {
                refuse();
                return;
            }
            pose->SetStanceChange(asked);
            m_owner->GetEquipment()->SetLongGunLift(asked == HUMAN_STANCE_PRONE);
            start(moving ? 500 : 1000, HUMAN_STANCE_PRONE);
            movement->m_unknown9B4 = 0x1AE;
            play(ANIM_LIE_TO_STAND);
        } else if (asked == HUMAN_STANCE_CROUCH) {
            if (!m_specialMoves->HasRoomForStance(5)) {
                refuse();
                return;
            }
            pose->SetStanceChange(asked);
            m_owner->GetEquipment()->SetLongGunLift(asked == HUMAN_STANCE_PRONE);
            movement->m_unknown9B4 = MoveTime(1100.0f);
            start(MoveTime(1000.0f), HUMAN_STANCE_PRONE);
            play(ANIM_LIE_TO_CROUCH);
        }
    }
}

// ------------------------------------------------------------------ the velocity

void C_HumanLocomotion::UpdateVelocity(bool replace)
{
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    const int sub = movement->m_animState.anim.sub;
    const int stance = movement->m_animState.anim.stance;
    // The way to move (none: the facing) and which way of the facing: 0 ahead, 1 back,
    // 2 / 3 to the left / right.
    S_vector dir(0.0f, 0.0f, 0.0f);
    int way = 0;
    float scale = 1.0f;
    // The move direction the work asks for (C_HumanWork::SetLookAt).
    S_vector4 asked = work->m_request.unknown10;
    // The animation coming up while climbing, the current one otherwise.
    const S_HumanAnimRef &source =
        IsClimbing(movement->m_nextAnimState.anim.anim) ? movement->m_nextAnimState.anim : movement->m_animState.anim;
    S_HumanAnimRef anim;
    anim.Set(source.anim, source.stance, source.sub);
    bool done = false;
    // The squad of a single player game, controlled by the AI, a little faster.
    if (IsLocalGame()) {
        if (!m_owner->IsLocalPlayer() && m_owner->GetRole() == HUMAN_ROLE_PLAYER)
            scale = 1.1f;
    } else {
        scale = 1.0f;
    }
    movement->m_mixer.m_speedScale = scale;
    const S_vector4 &facing = m_owner->GetDirection4();
    const S_vector4 none = {0.0f, 0.0f, 0.0f, 0.0f};
    // No root motion.
    const auto stand = [&]() {
        if (replace)
            movement->m_mixer.ReplaceVelocity(none, 0.0f, 0.0f, &m_velocity);
        else
            movement->m_mixer.SetVelocity(none, 0.0f, 0.0f, &m_velocity);
        done = true;
    };
    // The move direction turned a quarter towards the side the body walks to.
    const auto sideways = [&](float turn) {
        S_vector side = XYZ(facing);
        RotateAboutY(side, turn);
        work->SetLookAt(ToVector4(side, facing.w), true);
    };
    const auto lookAngle = [&]() {
        const float a[2] = {asked.x, asked.z};
        const float b[2] = {facing.x, facing.z};
        return LengthSquared(asked) >= 1e-16f ? AngleBetween2D(a, b) : 0.0f;
    };
    const auto speedOf = [&](int table) { return g_moveSpeeds[table][sub + stance * 6]; };
    float speed = 0.0f;
    bool refreshAsked = false;
    switch (anim.anim) {
    case ANIM_JUMP_12:
        if (m_jumpBack)
            dir = -XYZ(facing);
        speed = stance == HUMAN_STANCE_STAND ? g_moveSpeeds[9][sub] : 0.0f;
        break;
    case ANIM_JUMP_CROUCHED:
        if (m_jumpBack)
            dir = -XYZ(facing);
        speed = stance == HUMAN_STANCE_CROUCH ? g_moveSpeeds[10][sub + 6] : 0.0f;
        break;
    case ANIM_JUMP:
        if (m_jumpBack)
            dir = -XYZ(facing);
        speed = speedOf(8);
        break;
    case ANIM_CROUCH_TO_LIE:
    case ANIM_STAND_TO_LIE: {
        // Lying down while walking: on at the walking speed of the stance left.
        const int pending = m_animPlayer->m_pendingAnim.anim;
        if (m_animPlayer->m_animDelay == MOVE_NO_TIME || pending < ANIM_WALK) {
            stand();
            break;
        }
        const int row = sub + (anim.anim != ANIM_STAND_TO_LIE ? 6 : 0);
        speed = pending == ANIM_WALK_BACK || pending == ANIM_WALK_BACK_2 ? g_moveSpeeds[1][row] : g_moveSpeeds[0][row];
        break;
    }
    case ANIM_LIE_TO_CROUCH:
    case ANIM_LIE_TO_STAND: {
        movement->m_mixer.SetTrackRate(0, 1.3f);
        const int pending = m_animPlayer->m_pendingAnim.anim;
        if (m_animPlayer->m_animDelay == MOVE_NO_TIME || pending < ANIM_WALK) {
            stand();
            break;
        }
        speed = pending == ANIM_WALK_BACK || pending == ANIM_WALK_BACK_2 ? g_moveSpeeds[1][12 + sub] : g_moveSpeeds[0][12 + sub];
        break;
    }
    case ANIM_SWIM:
    case ANIM_DIVE: {
        const int table = anim.anim != ANIM_SWIM ? 12 : 11;
        if (m_owner->IsLocalPlayer()) {
            // Along the facing, at the pitch of the swimming direction.
            const S_vector4 &swim = movement->m_unknown940;
            const float level = S_vector(swim.x, 0.0f, swim.z).Magnitude();
            dir = S_vector(facing.x * level, swim.y, facing.z * level).Normalized();
        } else {
            dir = XYZ(movement->m_unknown940);
        }
        if (m_swimBack)
            dir = -dir;
        if (work->m_request.gait == 2) {
            const float rate = m_swimBack ? 0.25f : 1.0f;
            speed = rate * g_moveSpeeds[table][2];
            movement->m_mixer.SetTrackRate(0, rate * 1.5f);
        } else {
            const float rate = m_swimBack ? 0.4f : 1.0f;
            speed = rate * g_moveSpeeds[table][1];
            movement->m_mixer.SetTrackRate(0, rate);
        }
        break;
    }
    case ANIM_LADDER_UP: {
        if (movement->m_mixer.m_tracks[0].hold > 0) {
            // Fix: the original passes an uninitialised vector here (weight 0).
            movement->m_mixer.SetVelocity(none, 0.0f, 0.0f, &m_velocity);
            return;
        }
        C_Actor *ladder = movement->m_useObject;
        if (ladder)
            dir = XYZ(GetLadderTop(ladder)) - XYZ(GetLadderBottom(ladder));
        // (Fix: the original leaves the direction uninitialised without a ladder.)
        const int gait = work->m_request.gait;
        if (gait == 2 || gait == 5) {
            speed = 0.504f * 1.25f;
            movement->m_mixer.SetTrackRate(0, 1.75f);
        } else {
            movement->m_mixer.SetTrackRate(0, 1.4f);
            speed = 0.504f;
        }
        break;
    }
    case ANIM_LADDER_DOWN:
    case ANIM_LADDER_SLIDE: {
        if (movement->m_mixer.m_tracks[0].hold > 0) {
            movement->m_mixer.SetVelocity(none, 0.0f, 0.0f, &m_velocity);
            return;
        }
        C_Actor *ladder = movement->m_useObject;
        if (ladder)
            dir = XYZ(GetLadderBottom(ladder)) - XYZ(GetLadderTop(ladder));
        if (anim.anim == ANIM_LADDER_DOWN) {
            movement->m_mixer.SetTrackRate(0, 1.4f);
            speed = 0.504f;
        } else {
            speed = 3.0f;
            movement->m_mixer.SetTrackRate(0, 1.0f);
        }
        break;
    }
    case ANIM_WALK:
    case 0xC6:
        speed = speedOf(0);
        break;
    case ANIM_WALK_BACK:
    case ANIM_WALK_BACK_2:
        way = 1;
        speed = speedOf(1);
        break;
    case 0xC0:
    case 0xC8:
        way = 2;
        if (lookAngle() > 0.1f)
            sideways(HALF_PI);
        speed = speedOf(2);
        refreshAsked = true;
        break;
    case 0xC1:
    case 0xC9: {
        way = 3;
        float a = lookAngle();
        if (a < -3.0f)
            a = -a;
        if (a < -0.1f)
            sideways(-HALF_PI);
        speed = speedOf(3);
        refreshAsked = true;
        break;
    }
    case 0xC2:
    case 0xC4:
    case 0xCA:
    case 0xCC:
        way = 2;
        if (lookAngle() > 0.1f)
            sideways(HALF_PI);
        speed = anim.anim == 0xC2 || anim.anim == 0xCA ? speedOf(4) : speedOf(6);
        refreshAsked = true;
        break;
    case 0xC3:
    case 0xC5:
    case 0xCB:
    case 0xCD: {
        way = 3;
        float a = lookAngle();
        if (a < -3.0f)
            a = -a;
        if (a < -0.1f)
            sideways(-HALF_PI);
        speed = anim.anim == 0xC3 || anim.anim == 0xCB ? speedOf(5) : speedOf(7);
        refreshAsked = true;
        break;
    }
    default:
        // No root motion (the climbing moves played faster).
        if (IsClimbing(anim.anim))
            movement->m_mixer.SetTrackRate(0, 1.3f);
        stand();
        break;
    }
    if (refreshAsked)
        asked = work->m_request.unknown10;
    speed = MOVE_TIME_SCALE * scale * speed;
    // Carrying a body: the gun lift follows the speed.
    const int carrying = movement->m_unknownA30;
    if (carrying != 0 && carrying != MOVE_NO_TIME) {
        float lift = speed * 0.5f;
        if (lift < 0.1f)
            lift = 0.1f;
        m_owner->GetEquipment()->SetUnknownD0(lift);
    }
    if (done)
        return;
    const int now = movement->m_animState.anim.anim;
    const bool climbing = now == ANIM_SWIM || now == ANIM_DIVE || IsOnLadder(now) || now == ANIM_LADDER_TOP_OFF ||
                          now == ANIM_LADDER_TOP_ON;
    const float askedSquared = LengthSquared(asked);
    S_vector4 velocity;
    float maxSpeed = 0.0f;
    if (askedSquared >= 1e-16f && !climbing) {
        // Along the move the work asks for, at most its length.
        const float length = S_vector(asked.x, 0.0f, asked.z).Magnitude();
        if (length < 1e-8f)
            velocity = {0.0f, 0.0f, 0.0f, asked.w};
        else
            velocity = {asked.x * (1.0f / length), (1.0f / length) * 0.0f, asked.z * (1.0f / length), asked.w};
        maxSpeed = length;
    } else {
        const int coming = movement->m_nextAnimState.anim.anim;
        if (!IsInPlace(coming) || (replace && m_owner->IsLocalPlayer()) || climbing) {
            S_vector v;
            if (LengthSquared(ToVector4(dir, 0.0f)) >= 1e-16f) {
                v = dir;
                if (now != ANIM_SWIM && now != ANIM_DIVE && !IsOnLadder(now) && now != ANIM_LADDER_TOP_OFF && now != ANIM_LADDER_TOP_ON)
                    v.y = 0.0f;
            } else {
                v = XYZ(facing);
                if (!IsOnLadder(now) && now != ANIM_LADDER_TOP_OFF && now != ANIM_LADDER_TOP_ON)
                    v.y = 0.0f;
            }
            v.Normalize();
            if (way == 1) {
                v = -v;
            } else if (askedSquared < 1e-16f) {
                if (way == 2)
                    RotateAboutY(v, HALF_PI);
                else if (way == 3)
                    RotateAboutY(v, -HALF_PI);
            }
            velocity = ToVector4(v, asked.w);
        } else {
            velocity = {0.0f, 0.0f, 0.0f, asked.w};
        }
    }
    if (replace)
        movement->m_mixer.ReplaceVelocity(velocity, speed, maxSpeed, &m_velocity);
    else
        movement->m_mixer.SetVelocity(velocity, speed, maxSpeed, &m_velocity);
}

// ------------------------------------------------------------------ turning

bool C_HumanLocomotion::IsTurnAllowed() const
{
    const C_HumanMovement *movement = m_movement;
    if (IsClimbing(movement->m_animState.anim.anim) && movement->m_unknown998 == MOVE_NO_TIME)
        return false;
    const uint32_t interaction = movement->m_interaction;
    return interaction == 0 || interaction == 1 || movement->m_seatMode < 1 || movement->m_seatMode > 2;
}

int C_HumanLocomotion::GetTurnTime(const S_vector &dir)
{
    const S_vector4 &facing = m_owner->GetDirection4();
    const float a[2] = {dir.x, dir.z};
    const float b[2] = {facing.x, facing.z};
    const float angle = AngleBetween2D(a, b);
    if (m_owner->IsLocalPlayer())
        return static_cast<int>(std::fabs(angle / TURN_SPEED_PLAYER));
    return static_cast<int>(std::fabs(angle / m_turnSpeed) * 2.5);
}

void C_HumanLocomotion::SetTurnTarget(const S_vector4 &dir, int time)
{
    C_HumanMovement *movement = m_movement;
    const S_vector4 &facing = m_owner->GetDirection4();
    m_turnTarget.x = MOVE_NO_VALUE;
    if (dir.z * dir.z + dir.x * dir.x < 1e-16f)
        return;
    if (movement->m_seatMode > 0 && movement->m_seatMode != 4)
        return;
    const float from[2] = {facing.x, facing.z};
    const float to[2] = {dir.x, dir.z};
    const float angle = AngleBetween2D(from, to);
    const float absAngle = std::fabs(angle);
    C_HumanWork *work = m_owner->GetWork();
    // Ends the turn in place animation (none asked: the idle of the stance).
    const auto endTurnAnim = [&]() {
        const int asked = work->m_request.anim;
        if (asked != ANIM_TURN_RIGHT && asked != ANIM_TURN_LEFT)
            return;
        const int stance = work->m_request.stance;
        if (stance == HUMAN_STANCE_CROUCH)
            work->Play(ANIM_CROUCH, true);
        else if (stance != HUMAN_STANCE_PRONE)
            work->Play(ANIM_STAND, true);
        else
            work->Play(ANIM_LIE, true);
    };
    if (absAngle < static_cast<float>(time) * 1e-5f) {
        // Facing it.
        work->m_bodyFlags |= 4;
        m_turnTarget.x = MOVE_NO_VALUE;
        endTurnAnim();
        return;
    }
    const int stance = work->m_request.stance;
    float limit = stance == HUMAN_STANCE_STAND ? TURN_LIMIT_STAND : TURN_LIMIT_LOW;
    if (angle <= 0.0f && stance == HUMAN_STANCE_STAND)
        limit *= 0.5f;
    // The time the turn in place may take (60 degrees / s and half as much again).
    const int turnTime = static_cast<int>(static_cast<float>(static_cast<int>(limit / 3.0f * 1000.0f)) * 1.5f);
    const bool small = absAngle < 0.0012f * static_cast<float>(time);
    const bool inPlace = IsInPlace(movement->m_animState.anim.anim);
    m_turning = !inPlace && m_owner->IsLocalPlayer();
    const auto keepTarget = [&]() { m_turnTarget = {dir.x, 0.0f, dir.z, dir.w}; };
    const int carryMode = work->m_request.carryMode;
    const int carry = movement->m_animState.carryAnim;
    if (work->m_request.stance == HUMAN_STANCE_PRONE || work->m_request.unknown5C != 1 || carryMode == 0 || carryMode == 6 ||
        InRange(carry, 0x50, 0x52) || !inPlace) {
        movement->m_unknown950 = 0;
        keepTarget();
        if (!inPlace)
            return;
    } else if (absAngle <= limit) {
        if (movement->m_unknown950 < 1 && m_owner->IsLocalPlayer()) {
            m_turnTarget.x = MOVE_NO_VALUE;
            endTurnAnim();
            return;
        }
        keepTarget();
    } else {
        movement->m_unknown950 = turnTime;
        keepTarget();
    }
    if (small)
        return;
    // The turn in place animation to that side.
    const int asked = work->m_request.anim;
    if (!IsInPlace(asked))
        return;
    const int current = movement->m_animState.anim.anim;
    if (current == ANIM_TURN_LEFT || current == ANIM_TURN_RIGHT)
        return;
    work->Play(angle <= 0.0f ? ANIM_TURN_RIGHT : ANIM_TURN_LEFT, true);
}

bool C_HumanLocomotion::UpdateTurn(int time, bool steady)
{
    if (!IsTurnAllowed())
        return false;
    C_HumanMovement *movement = m_movement;
    C_HumanWork *work = m_owner->GetWork();
    const S_vector4 facing = m_owner->GetDirection4();
    const float level = S_vector(facing.x, 0.0f, facing.z).Magnitude();
    const float limit = work->m_request.stance == HUMAN_STANCE_STAND ? TURN_LIMIT_STAND : TURN_LIMIT_LOW;
    const float target[2] = {m_turnTarget.x, m_turnTarget.z};
    const float from[2] = {facing.x, facing.z};
    const float angle = AngleBetween2D(target, from);
    const float absAngle = std::fabs(angle);
    const int gait = work->m_request.gait;
    const float ms = static_cast<float>(time);
    if (absAngle < ms * 1e-5f)
        return true;
    float step;
    if (!steady && m_steadyTurnTime == MOVE_NO_TIME) {
        if (absAngle < limit * 1.6f && limit * 0.25f < absAngle && work->m_request.unknown5C != 0 &&
            IsInPlace(movement->m_animState.anim.anim)) {
            // With the turn in place animation: faster the larger the angle.
            step = angle <= 0.0f ? 0.0075f : -0.0075f;
            if (absAngle > 2.85f)
                step *= 14.0f;
            else if (absAngle > 2.55f)
                step *= 4.0f;
            else if (absAngle > 2.25f)
                step *= 3.0f;
            else if (absAngle > 1.85f)
                step += step;
            step *= ms;
        } else {
            // At once (the turn counter goes on: += as in the original).
            step = -angle;
            if (movement->m_unknown950 > 0)
                movement->m_unknown950 += time;
        }
    } else {
        // Steady: slower when walking (0.3 for gait 4).
        step = angle > 0.0f ? -m_turnSpeed : m_turnSpeed;
        if (gait == 4)
            step *= 0.3f;
        else if (gait != 2 && gait != 5)
            step *= 0.75f;
        step *= ms;
    }
    // Blocked: the body faces where its frame faces; turns steadily for 500 ms.
    const auto blocked = [&]() {
        I3D_frame *frame = m_owner->GetFrame();
        work->m_forced = true;
        work->SetFacingDir(frame->GetMatrix()(2).Normalized(), true);
        work->m_forced = false;
        movement->m_unknown240 = m_turnTarget;
        m_turnTarget.x = MOVE_NO_VALUE;
        const S_matrix &m = frame->GetMatrix();
        m_owner->SetDirection4(ToVector4(m(2).Normalized(), m.m[2][3]));
        m_steadyTurnTime = 500;
    };
    if (absAngle <= std::fabs(step)) {
        // The last step: facing the target.
        if (!m_specialMoves->CanTurn(angle) && (IsLocalGame() || m_owner->IsLocalPlayer())) {
            blocked();
            return true;
        }
        const S_vector to = Flat(XYZ(m_turnTarget));
        m_owner->SetDirection4({level * to.x, facing.y, level * to.z, m_turnTarget.w});
        m_directionChanged = true;
        work->m_bodyFlags |= 4;
        return true;
    }
    S_vector turned(facing.x, 0.0f, facing.z);
    RotateAboutY(turned, step);
    if (m_specialMoves->CanTurn(-step)) {
        m_owner->SetDirection4({turned.x, facing.y, turned.z, facing.w});
        m_directionChanged = true;
        work->m_bodyFlags |= 4;
        return false;
    }
    blocked();
    return false;
}

// ------------------------------------------------------------------ detail

void C_HumanLocomotion::UpdateDetail()
{
    C_HumanMovement *movement = m_movement;
    if (!IsClient()) {
        movement->m_flags |= MOVE_FLAG_FULL_DETAIL;
        return;
    }
    I3D_scene *scene = GetGame()->GetScene();
    I3D_frame *frame = m_owner->GetFrame();
    I3D_camera *camera = scene->GetActiveCamera();
    // The top of the body against the camera.
    S_vector top = frame->GetWorldPos();
    const int count = movement->m_stanceVolumeCount;
    if (count > 0)
        top.y = movement->m_stanceSpheres[count - 1].y + movement->m_stanceSpheres[count - 1].w;
    const float distance = (top - camera->GetWorldPos()).Magnitude();
    const float fov = camera->GetFOV();
    const float aspect = camera->GetAspectRatio();
    float nearRange, farRange;
    camera->GetRange(nearRange, farRange);
    if (g_detailTime != GetGame()->GetMissionTime()) {
        g_detailTime = GetGame()->GetMissionTime();
        g_detailTan = static_cast<float>(std::tan(fov * 0.5f) / aspect);
    }
    // The height of the view at the body and the share of it a 1.7 m body takes.
    const float extent = g_detailTan * distance + g_detailTan * distance;
    const float size = 1.7f / extent;
    movement->m_flags = (movement->m_flags & 0xFFFF102F) | MOVE_FLAG_FULL_DETAIL | MOVE_FLAG_IN_RANGE;
    float tiny;
    switch (m_owner->GetWork()->m_request.gait) {
    case 0: tiny = 0.035f; break;
    case 1: tiny = 0.025f; break;
    case 2: tiny = 0.018f; break;
    case 5: tiny = 0.0135f; break;
    default: tiny = 0.03f; break;
    }
    const bool drawn = frame->GetRenderStamp() == scene->GetRenderStamp();
    if (m_owner == g_teamManager.GetPlayerTeam()->GetActivePlayer() || movement->m_unknown1AC != 1) {
        if (size >= 0.15f && drawn)
            movement->m_flags |= MOVE_FLAG_LARGE;
    } else {
        if (!drawn || size < 0.0135f)
            movement->m_flags = (movement->m_flags & 0xFFFFF10F) | MOVE_FLAG_HIDDEN;
        if (size < 0.15f) {
            if (size < tiny)
                movement->m_flags = (movement->m_flags & ~MOVE_FLAG_FULL_DETAIL) | MOVE_FLAG_TINY;
            else if (size < 0.048f)
                movement->m_flags = (movement->m_flags & ~MOVE_FLAG_FULL_DETAIL) | MOVE_FLAG_SMALL;
        } else {
            movement->m_flags |= MOVE_FLAG_LARGE;
        }
        if (!drawn)
            movement->m_flags |= MOVE_FLAG_NOT_DRAWN;
    }
    if (extent > 25.0f || farRange < distance)
        movement->m_flags = (movement->m_flags & 0xFFFF2FFF) | MOVE_FLAG_OUT_OF_RANGE;
}

void C_HumanLocomotion::ScaleStanceVolumes(bool enlarged)
{
    C_HumanMovement *movement = m_movement;
    for (int i = 0; i < movement->m_stanceVolumeCount; ++i) {
        S_CollisionVolume *volume = movement->m_stanceVolumes[i];
        const S_vector4 &sphere = movement->m_stanceSpheres[i];
        volume->center = XYZ(sphere);
        volume->centerW = sphere.w;
        volume->radius = sphere.w * (enlarged ? 1.25f : movement->m_stanceVolumeScale);
        g_collisionWorld.MoveVolume(volume);
    }
}

// ------------------------------------------------------------------ the swimming sound

namespace {

// The swimming sound of `kind` (1: under water) is heard: the active player's own always,
// the others' above water only, each while the camera is on the same side of the surface.
bool IsSwimSoundAudible(const C_Human *owner, int kind)
{
    const C_Human *active = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    return (owner == active || kind == 0) && C_HumanMovement::IsCameraUnderWater() != (kind == 0);
}

}  // namespace

void C_HumanLocomotion::StartSwimSound()
{
    if (!IsClient())
        return;
    if (m_swimSound || !IsSwimSoundAudible(m_owner, m_swimSoundKind))
        return;
    const S_vector pos(0.0f, 0.0f, 0.0f);
    m_swimSound = GetMovementSounds()->Play(m_swimSoundKind, m_movement->m_joints[HUMAN_JOINT_F1].frame, &pos);
    if (m_swimSound) {
        m_swimSoundLeft = m_swimSound->GetLength();
        m_swimSound->SetVolume(0.0f);
    }
    // Fading in.
    if (m_swimSoundFade == MOVE_NO_TIME)
        m_swimSoundFade = 0;
    else if (m_swimSoundFade < 0)
        m_swimSoundFade = -m_swimSoundFade;
}

void C_HumanLocomotion::StopSwimSound(bool now)
{
    if (now) {
        if (m_swimSound) {
            m_swimSound->SetVolume(0.0f);
            GetMovementSounds()->Return(m_swimSound);
            m_swimSound = nullptr;
        }
        m_swimSoundFade = MOVE_NO_TIME;
        return;
    }
    // Fading out (over 500 ms; from where a fade in got to).
    if (m_swimSoundFade == MOVE_NO_TIME)
        m_swimSoundFade = -500;
    else if (m_swimSoundFade > 0)
        m_swimSoundFade = -m_swimSoundFade;
}

void C_HumanLocomotion::UpdateSwimSound(int time)
{
    if (!IsClient())
        return;
    bool underWater = false;
    float surface;
    const bool inWater = m_owner->GetMovement()->GetHeadWater(&underWater, &surface);
    const int kind = underWater ? 1 : 0;
    if (!m_swimSound) {
        if (inWater) {
            m_swimSoundKind = kind;
            StartSwimSound();
            return;
        }
        StopSwimSound(false);
        return;
    }
    if (!inWater) {
        StopSwimSound(false);
        return;
    }
    const bool muted = !IsSwimSoundAudible(m_owner, m_swimSoundKind);
    const int fade = m_swimSoundFade;
    if (fade != MOVE_NO_TIME && fade < 0) {
        // Fading out.
        m_swimSoundFade = fade + time;
        if (m_swimSoundFade < 0) {
            m_swimSound->SetVolume(static_cast<float>(m_swimSoundFade) * -0.002f);
            return;
        }
        m_swimSoundFade = MOVE_NO_TIME;
        StopSwimSound(true);
        return;
    }
    if (fade != MOVE_NO_TIME) {
        if (muted) {
            // (The fade out starts, the sound still renewed at full volume this time.)
            m_swimSoundFade = MOVE_NO_TIME;
            StopSwimSound(false);
        } else {
            // Fading in.
            m_swimSoundFade = fade + time;
            if (m_swimSoundFade < 500) {
                m_swimSound->SetVolume(static_cast<float>(m_swimSoundFade) * 0.002f);
                return;
            }
            m_swimSoundFade = MOVE_NO_TIME;
        }
    } else if (muted) {
        m_swimSoundFade = MOVE_NO_TIME;
        StopSwimSound(false);
        return;
    }
    // The next sound when this one ends or the head went in or out of the water.
    m_swimSoundLeft -= time;
    if (m_swimSoundLeft < 1 || kind != m_swimSoundKind) {
        GetMovementSounds()->Return(m_swimSound);
        m_swimSoundKind = kind;
        m_swimSound = nullptr;
        const S_vector pos(0.0f, 0.0f, 0.0f);
        m_swimSound = GetMovementSounds()->Play(kind, m_movement->m_joints[HUMAN_JOINT_C1].frame, &pos);
        if (!m_swimSound)
            return;
        m_swimSoundLeft = m_swimSound->GetLength();
    }
    m_swimSound->SetVolume(1.0f);
}

// ------------------------------------------------------------------ saved games

namespace {

enum : uint16_t {
    CHUNK_LOCOMOTION = 0xDE,
    CHUNK_LOCO_UNKNOWN000 = 0x01,
    CHUNK_LOCO_MOVE = 0x02,
    CHUNK_LOCO_STEADY_TURN_TIME = 0x03,
    CHUNK_LOCO_DEAD_SLIDE_TIME = 0x04,
    CHUNK_LOCO_SLOPE_SLOWDOWN = 0x05,
    CHUNK_LOCO_FALL_START = 0x06,
    CHUNK_LOCO_SWIM_BACK = 0x07,
    CHUNK_LOCO_FALL_GROUND = 0x09,
    CHUNK_LOCO_FALL_HEIGHT = 0x0A,
    CHUNK_LOCO_PUSH_TIME = 0x0B,
    CHUNK_LOCO_TURN_TARGET = 0x0C,
    CHUNK_LOCO_TURNING = 0x0D,
    CHUNK_LOCO_TURN_SPEED = 0x0E,
    CHUNK_LOCO_UNKNOWN030 = 0x0F,
    CHUNK_LOCO_WATER_TRANSITION = 0x11,
    CHUNK_LOCO_WATER_TEST_TIME = 0x12,
    CHUNK_LOCO_IN_WATER = 0x13,
    CHUNK_LOCO_SHIFT_TIME = 0x14,
    CHUNK_LOCO_SHIFT_STANCE = 0x15,
    CHUNK_LOCO_UNKNOWN0DC = 0x16,
    CHUNK_LOCO_UNKNOWN0E0 = 0x17,
    CHUNK_LOCO_UNKNOWN0E4 = 0x18,
    CHUNK_LOCO_UNKNOWN0E8 = 0x19,
    CHUNK_LOCO_VELOCITY = 0x1B,
    CHUNK_LOCO_FALL_SPEED = 0x1C,
    CHUNK_LOCO_FALL_STEP_TIME = 0x1D,
    CHUNK_LOCO_JUMP_TIME = 0x1E,
    CHUNK_LOCO_JUMP_BACK = 0x1F,
    CHUNK_LOCO_UNKNOWN0C0 = 0x20,
    CHUNK_LOCO_WATER_DEPTH = 0x21,
    CHUNK_LOCO_WATER_HEIGHT = 0x22,
    CHUNK_LOCO_UNKNOWN058 = 0x23,
    CHUNK_LOCO_PUSH = 0x24,
    CHUNK_LOCO_UNKNOWN0F0 = 0x25,
    CHUNK_LOCO_EDGE_HEIGHT = 0x29,
    CHUNK_LOCO_EDGE_TIME = 0x2A,
    CHUNK_LOCO_WATER_SURFACE = 0x2B,
    CHUNK_LOCO_UNKNOWN0EC = 0x2C,
    CHUNK_LOCO_VOLUMES_TIME = 0x2D,
    CHUNK_LOCO_GROUND_TEST_TIME = 0x2F,
};

}  // namespace

bool C_HumanLocomotion::SaveGame(C_chunk &chunk)
{
    if (!chunk.IsOpen() || !chunk.Begin(CHUNK_LOCOMOTION))
        return false;
    // (The vectors without their w; the flags as ints.)
    chunk.WriteChunk(CHUNK_LOCO_UNKNOWN000, &m_unknown000, 0x0C);
    chunk.WriteChunk(CHUNK_LOCO_MOVE, &m_move, 0x0C);
    chunk.WriteChunk(CHUNK_LOCO_STEADY_TURN_TIME, m_steadyTurnTime);
    chunk.WriteChunk(CHUNK_LOCO_DEAD_SLIDE_TIME, m_deadSlideTime);
    chunk.WriteChunk(CHUNK_LOCO_SLOPE_SLOWDOWN, m_slopeSlowdown);
    chunk.WriteChunk(CHUNK_LOCO_FALL_START, m_fallStartY);
    chunk.WriteChunk(CHUNK_LOCO_SWIM_BACK, static_cast<int>(m_swimBack));
    chunk.WriteChunk(CHUNK_LOCO_FALL_GROUND, m_fallGround);
    chunk.WriteChunk(CHUNK_LOCO_FALL_HEIGHT, m_fallHeight);
    chunk.WriteChunk(CHUNK_LOCO_PUSH_TIME, m_pushTime);
    chunk.WriteChunk(CHUNK_LOCO_TURN_TARGET, &m_turnTarget, 0x0C);
    chunk.WriteChunk(CHUNK_LOCO_TURNING, static_cast<int>(m_turning));
    chunk.WriteChunk(CHUNK_LOCO_TURN_SPEED, m_turnSpeed);
    chunk.WriteChunk(CHUNK_LOCO_UNKNOWN030, &m_unknown030, 0x0C);
    chunk.WriteChunk(CHUNK_LOCO_WATER_TRANSITION, m_waterTransition);
    chunk.WriteChunk(CHUNK_LOCO_WATER_TEST_TIME, m_waterTestTime);
    chunk.WriteChunk(CHUNK_LOCO_IN_WATER, static_cast<int>(m_inWater));
    chunk.WriteChunk(CHUNK_LOCO_SHIFT_TIME, m_shiftTime);
    chunk.WriteChunk(CHUNK_LOCO_SHIFT_STANCE, m_shiftStance);
    chunk.WriteChunk(CHUNK_LOCO_UNKNOWN0DC, m_unknown0DC);
    chunk.WriteChunk(CHUNK_LOCO_UNKNOWN0E0, m_unknown0E0);
    chunk.WriteChunk(CHUNK_LOCO_UNKNOWN0E4, static_cast<int>(m_unknown0E4));
    chunk.WriteChunk(CHUNK_LOCO_UNKNOWN0E8, m_unknown0E8);
    chunk.WriteChunk(CHUNK_LOCO_VELOCITY, &m_velocity, 0x0C);
    chunk.WriteChunk(CHUNK_LOCO_FALL_SPEED, m_fallSpeed);
    chunk.WriteChunk(CHUNK_LOCO_FALL_STEP_TIME, m_fallStepTime);
    chunk.WriteChunk(CHUNK_LOCO_JUMP_TIME, m_jumpTime);
    chunk.WriteChunk(CHUNK_LOCO_JUMP_BACK, static_cast<int>(m_jumpBack));
    chunk.WriteChunk(CHUNK_LOCO_UNKNOWN0C0, &m_unknown0C0, 0x0C);
    chunk.WriteChunk(CHUNK_LOCO_WATER_DEPTH, m_waterDepth);
    chunk.WriteChunk(CHUNK_LOCO_WATER_HEIGHT, m_waterHeight);
    chunk.WriteChunk(CHUNK_LOCO_UNKNOWN058, m_unknown058);
    chunk.WriteChunk(CHUNK_LOCO_PUSH, &m_push, 0x0C);
    chunk.WriteChunk(CHUNK_LOCO_UNKNOWN0F0, m_unknown0F0);
    chunk.WriteChunk(CHUNK_LOCO_EDGE_HEIGHT, m_edgeHeight);
    chunk.WriteChunk(CHUNK_LOCO_EDGE_TIME, m_edgeTime);
    chunk.WriteChunk(CHUNK_LOCO_WATER_SURFACE, m_waterSurface);
    chunk.WriteChunk(CHUNK_LOCO_UNKNOWN0EC, m_unknown0EC);
    chunk.WriteChunk(CHUNK_LOCO_VOLUMES_TIME, m_volumesTime);
    chunk.WriteChunk(CHUNK_LOCO_GROUND_TEST_TIME, m_groundTestTime);
    chunk.Ascend();
    return true;
}

bool C_HumanLocomotion::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.IsOpen() || !chunk.Descend(id))
        return false;
    if (id != CHUNK_LOCOMOTION) {
        chunk.Ascend();
        return false;
    }
    m_directionChanged = true;
    m_positionChanged = true;
    const auto readFlag = [&chunk](bool &flag) {
        int value = 0;
        chunk.Read(value);
        flag = value != 0;
    };
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_LOCO_UNKNOWN000: chunk.Read(&m_unknown000, 0x0C); break;
        case CHUNK_LOCO_MOVE: chunk.Read(&m_move, 0x0C); break;
        case CHUNK_LOCO_STEADY_TURN_TIME: chunk.Read(m_steadyTurnTime); break;
        case CHUNK_LOCO_DEAD_SLIDE_TIME: chunk.Read(m_deadSlideTime); break;
        case CHUNK_LOCO_SLOPE_SLOWDOWN: chunk.Read(m_slopeSlowdown); break;
        case CHUNK_LOCO_FALL_START: chunk.Read(m_fallStartY); break;
        case CHUNK_LOCO_SWIM_BACK: readFlag(m_swimBack); break;
        case CHUNK_LOCO_FALL_GROUND: chunk.Read(m_fallGround); break;
        case CHUNK_LOCO_FALL_HEIGHT: chunk.Read(m_fallHeight); break;
        case CHUNK_LOCO_PUSH_TIME: chunk.Read(m_pushTime); break;
        case CHUNK_LOCO_TURN_TARGET: chunk.Read(&m_turnTarget, 0x0C); break;
        case CHUNK_LOCO_TURNING: readFlag(m_turning); break;
        case CHUNK_LOCO_TURN_SPEED: chunk.Read(m_turnSpeed); break;
        case CHUNK_LOCO_UNKNOWN030: chunk.Read(&m_unknown030, 0x0C); break;
        case CHUNK_LOCO_WATER_TRANSITION: chunk.Read(m_waterTransition); break;
        case CHUNK_LOCO_WATER_TEST_TIME: chunk.Read(m_waterTestTime); break;
        case CHUNK_LOCO_IN_WATER: readFlag(m_inWater); break;
        case CHUNK_LOCO_SHIFT_TIME: chunk.Read(m_shiftTime); break;
        case CHUNK_LOCO_SHIFT_STANCE: chunk.Read(m_shiftStance); break;
        case CHUNK_LOCO_UNKNOWN0DC: chunk.Read(m_unknown0DC); break;
        case CHUNK_LOCO_UNKNOWN0E0: chunk.Read(m_unknown0E0); break;
        case CHUNK_LOCO_UNKNOWN0E4: readFlag(m_unknown0E4); break;
        case CHUNK_LOCO_UNKNOWN0E8: chunk.Read(m_unknown0E8); break;
        case CHUNK_LOCO_VELOCITY: chunk.Read(&m_velocity, 0x0C); break;
        case CHUNK_LOCO_FALL_SPEED: chunk.Read(m_fallSpeed); break;
        case CHUNK_LOCO_FALL_STEP_TIME: chunk.Read(m_fallStepTime); break;
        case CHUNK_LOCO_JUMP_TIME: chunk.Read(m_jumpTime); break;
        case CHUNK_LOCO_JUMP_BACK: readFlag(m_jumpBack); break;
        case CHUNK_LOCO_UNKNOWN0C0: chunk.Read(&m_unknown0C0, 0x0C); break;
        case CHUNK_LOCO_WATER_DEPTH: chunk.Read(m_waterDepth); break;
        case CHUNK_LOCO_WATER_HEIGHT: chunk.Read(m_waterHeight); break;
        case CHUNK_LOCO_UNKNOWN058: chunk.Read(m_unknown058); break;
        case CHUNK_LOCO_PUSH: chunk.Read(&m_push, 0x0C); break;
        case CHUNK_LOCO_UNKNOWN0F0: chunk.Read(m_unknown0F0); break;
        case CHUNK_LOCO_EDGE_HEIGHT: chunk.Read(m_edgeHeight); break;
        case CHUNK_LOCO_EDGE_TIME: chunk.Read(m_edgeTime); break;
        case CHUNK_LOCO_WATER_SURFACE: chunk.Read(m_waterSurface); break;
        case CHUNK_LOCO_UNKNOWN0EC: chunk.Read(m_unknown0EC); break;
        case CHUNK_LOCO_VOLUMES_TIME: chunk.Read(m_volumesTime); break;
        case CHUNK_LOCO_GROUND_TEST_TIME: chunk.Read(m_groundTestTime); break;
        default: break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    // No swimming sound yet; the water levels and the direction anew.
    m_swimSoundKind = 0x14;
    m_swimSoundFade = MOVE_NO_TIME;
    m_swimSoundLeft = 0;
    m_unknown090 = 0;
    float lowest;
    g_collisionWorld.GetWaterHeightRange(lowest, m_waterLevel);
    m_volumesDir = m_owner->GetDirection4();
    return true;
}
