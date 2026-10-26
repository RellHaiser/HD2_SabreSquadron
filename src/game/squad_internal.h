// What the squad controller's sources share (squad_controller.cpp, squad_actions.cpp,
// squad_combat.cpp, squad_movement.cpp): sentences, item types and small helpers.
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "game/controller_internal.h"
#include "game/human_controller.h"
#include "game/items.h"
#include "ls3d/i3d.h"
#include "ls3d/math.h"

class C_Human;

// The sentences the squad says (the speech set's; with `alt` the next one is the whispered
// form; names inferred from where they are said).
enum : int {
    SENTENCE_ENEMY_FAR = 0x5C,
    SENTENCE_ENEMY_NEAR = 0x5E,
    SENTENCE_ENEMY_LOST = 0x60,
    SENTENCE_UNDER_FIRE = 100,
    SENTENCE_CANNOT = 0x68,                             // an order that cannot be carried out (0x69 quietly)
    SENTENCE_ENEMY_DOWN = 0x6A,
    SENTENCE_ENEMY_HIT = 0x6C,                          // (an enemy event of a hit of type 5)
    SENTENCE_LOUD_SOUND = 0x6E,
    SENTENCE_LINE_BLOCKED = 0x70,                       // a friend in the line of fire
    SENTENCE_WEAPON_DROPPED = 0x72,
    SENTENCE_IDLE = 0x7F,                               // nothing to do (the command module's idle reports)
};

// A command of category 2 (C_HumanController::ExecuteCommand's).
inline S_HumanCommand MakeCommand(int type, int param1, int param2)
{
    S_HumanCommand command;
    command.category = HUMAN_COMMAND_VEHICLE;
    command.type = type;
    command.param1 = param1;
    command.param2 = param2;
    return command;
}

// An armoured target: a car with +0x5E0 set, or an animation actor with +0xD8 2 (only
// launchers hurt them).
bool IsArmoured(const C_Actor *target);

// The actor is out of the fight: a dead human, a destroyed car, a stopped animation, a dead
// meleeTracker (0x00538a60 __fastcall).
bool IsActorDead(const C_Actor *actor);
// A car / an animation actor sees `human` (a ray from the actor's bound centre to the body
// within 300 m: nothing or the actor itself or the human's vehicle in the way;
// 0x00538b00 __fastcall).
bool CanSeeVehicle(C_Actor *actor, const C_Human *human);
// A cover point within `radius` of `pos` against `dir` (the walking network's cover points;
// `seed` picks one and turns the offset; none: `pos`). 0x00525260 (__fastcall).
S_vector4 FindCoverPoint(const S_vector4 &pos, const S_vector4 &dir, float radius, uint32_t seed);
// Keeps `pos` on the walking network: within its point's radius, or (`useFrom`) on the way
// from `from` when the two points are far apart on it. 0x0052d720 (__fastcall).
void SnapToNetwork(S_vector4 &pos, bool useFrom, const S_vector4 *from);
