// What the enemy AI's sources share (enemy_controller.cpp, enemy_tasks.cpp, enemy_combat.cpp,
// sniper_controller.cpp).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "game/controller_internal.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "ls3d/math.h"

struct S_Checkpoint;

// A copy of `event` from the pool, with 10 s to live (the AI's inline copies).
inline S_HumanEvent *CopyEvent(const S_HumanEvent &event)
{
    S_HumanEvent *copy = AllocHumanEvent();
    copy->type = event.type;
    copy->age = event.age;
    copy->timeLeft = 10000;
    copy->actorId = event.actorId;
    copy->sourceId = event.sourceId;
    copy->pos = event.pos;
    copy->unknown30 = event.unknown30;
    return copy;
}

// The two events are the same (actor, type, source, position and +0x30).
inline bool SameEvent(const S_HumanEvent &a, const S_HumanEvent &b)
{
    return a.actorId == b.actorId && a.type == b.type && a.sourceId == b.sourceId && a.pos.x == b.pos.x
           && a.pos.y == b.pos.y && a.pos.z == b.pos.z && a.unknown30 == b.unknown30;
}

// rand() as 0..1 (the original's rand() * (1 / 32767)).
inline float Rand01()
{
    return static_cast<float>(std::rand()) * (1.0f / 32767.0f);
}

// (NormalizeBy and MagnitudeBy: game/controller_internal.h.)

// `human` sits hidden inside a car or a static weapon (the seat's SEAT_FLAG_HIDDEN: a tank's
// crew, inferred), where small arms do not reach him; the AI takes a gun or a launcher to
// him, or hides. 0x00559610.
bool IsShieldedInVehicle(const C_Human *human);
// The stances (of `stances`: 1 lying, 2 crouching, 4 standing) point `cp` gives cover in
// against `pos`. 0x005534b0 (__thiscall on the point).
uint32_t CoverAgainst(const S_Checkpoint *cp, const S_vector &pos, uint32_t stances);
// Where to aim at `human` (its body, lower unless it lies, led by an eighth of a second).
// 0x0054d6b0.
S_vector4 GetLeadPoint(const C_Human *human);
