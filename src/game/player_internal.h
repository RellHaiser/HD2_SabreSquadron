// What the player controller's sources share (player_controller.cpp, player_weapons.cpp,
// player_net.cpp; the remote controller's remote_controller.cpp uses some of it too).
#pragma once

#include <cstdint>

#include "game/controller_internal.h"

class C_Human;
struct S_vector;

// How long the weapon stays raised after the player used it (ms; C_PlayerController's
// m_raisedTime).
constexpr int RAISED_TIME_AFTER_USE = 10000;

// The breath the player can hold (ms; HoldBreath).
constexpr int BREATH_FULL = 3500;

// The work animations of the binoculars (taken out at the belt item's frame, put away).
constexpr int WORK_ANIMATION_BINOCULARS_OUT = 0x17;
constexpr int WORK_ANIMATION_BINOCULARS_AWAY = 0x18;

// Body actions of the knife (CONTROLLER_ACTION_8 / CONTROLLER_ACTION_MELEE_2).
constexpr int BODY_ACTION_STAB = 8;
constexpr int BODY_ACTION_STAB_2 = 0xB;

// The dive costs that much stamina (the network's dive message needs more).
constexpr float DIVE_STAMINA_NEEDED = 10.0f;

// Two directions across `dir`: `side` = (dir x y) x dir and `up` = side x dir, each
// normalized (the player's spread; also 0x004c4440).
void BuildBasis(const S_vector &dir, S_vector &side, S_vector &up);   // 0x00544d20 (__fastcall)

// The work animation a squad command's speaker plays (S_ActionCommand::action; false: none).
bool GetSquadCommandGesture(int command, int *gesture);

// A client: the soldier class of a player's human the server sent (CTRL_MSG_SOLDIER_CLASS):
// the net game's record of it, the human's skills by it.
void ApplySoldierClass(C_Human *human, int soldierClass);
