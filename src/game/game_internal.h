// Helpers shared by the C_game sources (game*.cpp); not part of the game's interface.
#pragma once

#include <cstdint>

#include "game/game.h"
#include "game/pending.h"

namespace {

// Which sides of the game this machine runs (g_gameRole).
inline bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
inline bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
// Single player: server and client in one.
inline bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

// Multiplayer: the mission is the session map's (C_Campaign::GetCurrentMission), and its
// objectives are the map's.
inline C_MpMissionDef *GetMpMission(C_MissionDef *mission) { return static_cast<C_MpMissionDef *>(mission); }
inline S_MpObjective *GetMpObjective(const C_MissionDef *mission, int index)
{
    return static_cast<S_MpObjective *>(mission->GetObjective(index));
}

// The color of the game's messages (white; components 0..255).
const S_vector MESSAGE_WHITE = {255.0f, 255.0f, 255.0f};

// Texts (all_txt) of the game's messages.
enum : int {
    TEXT_GAME_SAVED = 0x7F9,
    TEXT_GAME_LOADED = 0x7FA,
    TEXT_SAVE_DISABLED = 0x19C7,                    // quick saving is not allowed at this difficulty
    TEXT_SAVES_LEFT = 0x2536,                       // "%d" saves left (difficulty 3)
    TEXT_MISSION_COMPLETED = 0x1CE8,
    TEXT_MISSION_FAILED = 0x1CE9,
    TEXT_WARMUP = 0x1CEA,
    TEXT_TEAM_WINS = 0x1CEB,                        // + the winning team
    TEXT_DEADLY_AREA = 0x1CF0,                      // single player: the squad entered a deadly area
    TEXT_RESTRICTED_AREA = 0x1CF1,
    TEXT_WINS_AXIS = 0x22E5,                        // the console's round result
    TEXT_WINS_ALLIED = 0x22E6,
    TEXT_SCORE_AXIS = 0x22E7,
    TEXT_SCORE_ALLIED = 0x22E8,
    TEXT_SESSION_CLOSING = 0x22A6,                  // OnSessionClosing() reasons 1, 0, 3, 2
    TEXT_CONNECTION_PROBLEM = 0x92B,
    TEXT_SPECTATING = 0x4F57,
};

// C_GameProfile::GetDifficulty() levels that limit saving.
enum : int {
    DIFFICULTY_LIMITED_SAVES = 3,                   // 3 quick saves per mission
    DIFFICULTY_NO_SAVES = 4,
};

// Input actions the game reads from the frame's `pressed` values (C_Controls).
enum : int {
    // (The meanings from the default keys, key_bindings.cpp.)
    INPUT_MOVE_FORWARD = 0,                         // W (the roll menu: the previous entry)
    INPUT_MOVE_BACK = 1,                            // S (the roll menu: the next entry)
    INPUT_ROLL_MENU = 19,                           // right mouse: the actions on an actor / closes the inventory
    INPUT_INFO = 21,                                // S: the mission information / scores while held
    INPUT_COMMAND_MAP = 22,                         // Space
    INPUT_LOWER_PANEL = 23,                         // V: the lower panel's next position
    INPUT_MAP = 24,                                 // M (while held)
    INPUT_INVENTORY = 25,                           // I
    INPUT_TOGGLE_HUD = 26,
    INPUT_MESSAGE_LOG = 27,                         // ~: the messages' history
    INPUT_SAY = 28,                                 // T
    INPUT_SAY_TEAM = 29,                            // Y
    INPUT_SWITCH_SOLDIER = 30,                      // Tab (Shift: the previous one)
    INPUT_PAUSE = 31,                               // P
    INPUT_WEAPON_PREV = 32,                         // [ / ]: the weapon selector
    INPUT_WEAPON_NEXT = 33,
    INPUT_SELECT_SOLDIER_1 = 35,                    // 1..4: control soldier 1..4
    INPUT_SIGNAL_ALL = 39,                          // numpad 0: the signals to the whole squad
    INPUT_SIGNAL_SOLDIER_1 = 40,                    // numpad 1..6: the signals to soldier 1..6
    INPUT_MENU = 46,
    INPUT_QUICK_SAVE = 47,
    INPUT_QUICK_LOAD = 48,
    INPUT_CURSOR_X = 52,                            // the mouse's motion (the maps' cursor)
    INPUT_CURSOR_Y = 53,
    INPUT_WHEEL = 54,                               // the mouse wheel (the menus' entries)
    INPUT_NEXT_PLAYER = 55,                         // spectating: the next player / free camera jump
    INPUT_CAMERA_MODE = 56,
    INPUT_CLICK = 55,                               // the maps' clicks: left / right mouse
    INPUT_CLICK2 = 56,
    INPUT_MP_MENU = 62,
};

// Multiplayer game types (S_MultiplayerSettings::gameType).
inline bool IsTeamGame() { return g_mpSettings->gameType != MP_GAME_DEATHMATCH; }

// Team counters of the network game: the round wins (+0x458, 0x0097d4c8).
inline int &TeamWins(int team) { return g_netGame.GetTeamWins()[team]; }

} // namespace
