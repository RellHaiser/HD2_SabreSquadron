// Helpers shared by the C_Car sources (car*.cpp); not part of the car's interface.
#pragma once

#include <cstdint>

#include "game/pending.h"                               // g_gameRole

namespace {

// Single player: server and client in one.
inline bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }
// A network game's server / client.
inline bool IsNetServer() { return !IsLocalGame() && (g_gameRole & GAME_ROLE_SERVER) != 0; }
inline bool IsNetClient() { return !IsLocalGame() && (g_gameRole & GAME_ROLE_CLIENT) != 0; }

}  // namespace
