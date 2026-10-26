// What the controllers' sources share (game/human_controller.cpp and the AI controllers):
// the body commands' arguments, the body's flags and small helpers.
#pragma once

#include <cmath>
#include <cstdint>

#include "game/actor.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "ls3d/i3d.h"

// Work animations the controls play (BODY_COMMAND_PLAY; directions as in the move flags).
enum : int {
    ANIM_DIVE = 0x12,
    ANIM_STOP = 0x2D,
    ANIM_DOOR_KEY = 0x67,                               // locking / unlocking a door
    ANIM_LEAN_LEFT = 0xA2,
    ANIM_LEAN_RIGHT = 0xA3,
    ANIM_WALK_FORWARD = 0xBE,
    ANIM_WALK_BACK = 0xBF,
    ANIM_WALK_LEFT = 0xC0,
    ANIM_WALK_RIGHT = 0xC1,
    ANIM_WALK_FORWARD_LEFT = 0xC2,
    ANIM_WALK_FORWARD_RIGHT = 0xC3,
    ANIM_WALK_BACK_LEFT = 0xC4,
    ANIM_WALK_BACK_RIGHT = 0xC5,
};
// The disguise a noisy action costs (C_Human::SetDisguise).
constexpr float DISGUISE_LOSS_SHOT = -30.0f;
constexpr float DISGUISE_LOSS_ACTION = -10.0f;

// BODY_COMMAND_WORK_ANIMATION of the photo camera.
constexpr int WORK_ANIMATION_PHOTO = 0x49;

// Body actions (BODY_COMMAND_ACTION, S_HumanBody::DoAction; names inferred from the callers).
enum : int {
    BODY_ACTION_FIRE = 1,
    BODY_ACTION_RELOAD = 3,
    BODY_ACTION_THROW = 6,                              // a grenade in the hands
    BODY_ACTION_THROW_2 = 7,
    BODY_ACTION_CARRY_BODY = 10,                        // picks a body up / puts it down
    BODY_ACTION_THROW_BELT_GRENADE = 13,                // the frame: the belt grenade
    BODY_ACTION_RAISE_WEAPON = 20,
    BODY_ACTION_LOWER_WEAPON = 21,
    BODY_ACTION_CLIMB = 30,
    BODY_ACTION_USE_OBJECT = 31,                        // the ladder in arg2
    // Rising from cover to fire and going back (the AI; inferred).
    BODY_ACTION_RISE_FROM_COVER = 2,
    BODY_ACTION_BACK_TO_COVER = 22,
};
// RequestAction()'s actions for them.
constexpr int CONTROLLER_ACTION_RISE_FROM_COVER = 0x11;
constexpr int CONTROLLER_ACTION_BACK_TO_COVER = 0x12;

// The body's stances (BODY_COMMAND_3) and gaits (BODY_COMMAND_STANCE).
enum : int {
    STANCE_STAND = 0,
    STANCE_CROUCH = 1,
    STANCE_LIE = 2,
};
enum : int {
    GAIT_PACE_1 = 0,
    GAIT_PACE_3 = 1,
    GAIT_PACE_4 = 2,
    GAIT_PACE_2 = 4,
    GAIT_MARCH = 5,
};

// C_HumanController::OnBodyFlags() flags of the work object (names inferred).
enum : uint32_t {
    BODY_FLAG_WEAPON_RAISED = 0x00000040,
    BODY_FLAG_UNKNOWN_100 = 0x00000100,
    BODY_FLAG_THROWN = 0x00001000,                      // the hands item left the hand
    BODY_FLAG_THROWN_2 = 0x00002000,
    BODY_FLAG_BELT_GRENADE_THROWN = 0x00004000,
    BODY_FLAG_UNKNOWN_10000 = 0x00010000,               // move flag 0x8000000
    BODY_FLAG_UNKNOWN_20000 = 0x00020000,
};
constexpr uint32_t MOVE_UNKNOWN_8000000 = 0x08000000;
// SetAIMode() mode 0x100, which a new controller starts with.
constexpr uint32_t MOVE_AI_AGGRESSIVE = 0x00000100;
// Move flags that keep with the controller that takes over (Attach).
constexpr uint32_t MOVE_KEPT_ON_ATTACH = 0x3FFFFFFF;
// The moving flags whose end stops the walk (ANIM_STOP; MOVE_MOVING_MASK and the leaning).
constexpr uint32_t MOVE_WALKING_MASK = MOVE_MOVING_MASK | MOVE_LEAN_MASK;
// The dive: the directions go, flag 0x80 comes.
constexpr uint32_t MOVE_DIVE = 0x00000080;
constexpr uint32_t MOVE_DIRECTION_BITS = 0x00000078;
// Move flags of MOVE_MEDIUM_MASK: on a ladder (inferred from C_AIController::Attach), and
// 0x80000 (not identified: the AI then only walks straight ahead).
constexpr uint32_t MOVE_ON_LADDER = 0x00100000;
constexpr uint32_t MOVE_MEDIUM_80000 = 0x00080000;

// The AI's move flags (C_AIController): firing a burst (StartFiring / StopFiring), looking
// around (LookAround ways 3 and 4), SetAIMode's defensive and zombie modes.
constexpr uint32_t MOVE_FIRING = 0x00200000;
constexpr uint32_t MOVE_LOOKING_AROUND = 0x00400000;
constexpr uint32_t MOVE_AI_DEFENSIVE = 0x00000200;
constexpr uint32_t MOVE_AI_ZOMBIE = 0x00000400;
// The throwing animation (S_HumanBody::GetCarryAnimLength).
constexpr int ANIM_THROW = 0x4F;

// The fire order's results (UpdateFireOrder; names inferred).
enum : uint32_t {
    AI_FIRE_NOT_READY = 0x0001,                         // the weapon's ready time is ahead
    AI_FIRE_BLOCKED = 0x0002,                           // the line of fire is not clear (Unknown114)
    AI_FIRE_PAUSE = 0x0004,                             // between bursts
    AI_FIRE_NOTHING = 0x0008,                           // no target and no direction
    AI_FIRE_NO_WEAPON = 0x0010,
    AI_FIRE_STARTED = 0x0020,
    AI_FIRE_BUSY = 0x0040,                              // the item is in use
    AI_FIRE_NOT_AIMED = 0x0080,                         // the weapon does not point along the view yet / the target changed
    AI_FIRE_MUST_TURN = 0x0100,                         // the body has to turn first (its facing is locked)
    AI_FIRE_RELOADING = 0x0200,
    AI_FIRE_0400 = 0x0400,                              // (not identified; the squad shows mode icon 0xC)
    AI_FIRE_NO_AMMO = 0x0800,
    AI_FIRE_TARGET_DEAD = 0x1000,
    AI_FIRE_STOP_MASK = 0x1B19,                         // results that end the burst
    AI_FIRE_WAIT_MASK = 0x00C6,                         // results that keep it but do not fire
};

// Event types (S_HumanEvent::type) the AI's senses raise (ai_senses.cpp); names inferred
// from the handlers.
enum : uint32_t {
    AI_EVENT_ENEMY_LOST = 0x10000002,
    AI_EVENT_KILLED_ENEMY = 0x10000003,
    AI_EVENT_HIT = HUMAN_EVENT_HIT,                     // tracked as a stimulus (the attacker)
    AI_EVENT_BODY_SEEN = 0x20000004,
    AI_EVENT_BODY_HEARD = 0x20000012,
    AI_EVENT_FRIEND_HEARD = 0x20000013,
    AI_EVENT_ENEMY_SEEN = 0x30000001,
    AI_EVENT_SOUND_0 = 0x3000000B,                      // by the sound's chapter (E_SoundChapter)
    AI_EVENT_SOUND_2 = 0x3000000C,
    AI_EVENT_SOUND_1 = 0x3000000D,
    AI_EVENT_SOUND_6 = 0x3000000E,
    AI_EVENT_SOUND_8 = 0x3000000F,
    AI_EVENT_SHOT_AT = 0x40000010,
    AI_EVENT_LOUD_SOUND = 0x40000011,
    AI_EVENT_HIT_5 = 0x40000015,                        // a hit of type 5
};

inline S_BodyCommand MakeBodyCommand(int kind, int arg0, int arg1 = 0, void *arg2 = nullptr)
{
    S_BodyCommand command;
    command.unknown04 = 1;
    command.kind = kind;
    command.arg0 = arg0;
    command.arg1 = arg1;
    command.arg2 = arg2;
    return command;
}

inline int FrameArg(I3D_frame *frame)
{
    return static_cast<int>(reinterpret_cast<intptr_t>(frame));
}

inline const S_vector &XYZ(const S_vector4 &v)
{
    return reinterpret_cast<const S_vector &>(v);
}

inline S_vector4 ToVector4(const S_vector &v, float w)
{
    return {v.x, v.y, v.z, w};
}

// The item in the human's hands. (Fix: the original calls through a null container when
// the human has none.)
inline C_Item *HandsItemOf(const C_Human *human)
{
    C_ItemContainer *hands = human->GetContainer(ITEM_CONTAINER_HANDS);
    return hands ? hands->GetItem() : nullptr;
}

inline int BehaviourOf(const C_Item *item)
{
    return item->GetDef()->GetBehaviour();
}

// The frame the controls fire from: the weapon's shot dummy, else its model. (Hands items are
// weapons; Fix: the original reads the dummy past the end of other items.)
inline I3D_frame *ShotFrameOf(C_Item *item)
{
    if (item->GetKind() == ITEM_KIND_WEAPON) {
        if (I3D_frame *dummy = static_cast<C_WeaponItem *>(item)->GetShotDummy())
            return dummy;
    }
    return item->GetFrame(0);
}

// The 4th float after a direction the controllers hand out (they are S_vector4s).
inline const S_vector4 &Dir4(const S_vector &dir)
{
    return reinterpret_cast<const S_vector4 &>(dir);
}

inline S_vector4 WorldPos4Of(const I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
}

// The frame's z axis, normalized (w: the matrix's).
inline S_vector4 FrameDir4(const I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    S_vector dir(m.m[2][0], m.m[2][1], m.m[2][2]);
    dir.Normalize();
    return {dir.x, dir.y, dir.z, m.m[2][3]};
}

inline float FireDirSquare(const S_vector4 &dir)
{
    return dir.z * dir.z + dir.y * dir.y + dir.x * dir.x;
}

// The AI controllers' controls (the original's static arrays at 0x008ab058 / 0x008aaf58,
// shared by the squad, the enemy and the sniper AI; every Tick clears them).
extern float g_aiAxes[CONTROL_COUNT];
extern float g_aiButtons[CONTROL_COUNT];

// Work animations (BODY_COMMAND_WORK_ANIMATION) of the AI.
constexpr int WORK_ANIMATION_SURRENDER = 0x34;          // hands up ("zajatec", prisoner)
constexpr int WORK_ANIMATION_END_ACTIVITY = 0x4C;       // the activity fades out (standing up again)

inline bool InVehicleSeat(const C_Actor *vehicle)
{
    if (!vehicle)
        return false;
    switch (vehicle->GetType()) {
    case ACTOR_CAR:
    case ACTOR_SUBMARINE:
    case ACTOR_STATIC_WEAPON:
    case ACTOR_JUNKERS88:
        return true;
    default:
        return false;
    }
}

// S_vector::Normalize with the squared length `square` the caller summed (the original sums
// the components in varying orders, which the last bit of the result follows).
inline void NormalizeBy(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-8f)
        return;
    if (square >= 1e-8f) {
        const float k = 1.0f / std::sqrt(square);
        v.x *= k;
        v.y *= k;
        v.z *= k;
    } else if (v.x != 0.0f) {
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    } else if (v.z != 0.0f) {
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    } else {
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
    }
}

// S_vector::Magnitude of a squared length the caller summed.
inline float MagnitudeBy(float square)
{
    if (square < 1e-8f)
        return 0.0f;
    if (std::fabs(square - 1.0f) < 1e-8f)
        return 1.0f;
    return std::sqrt(square);
}
