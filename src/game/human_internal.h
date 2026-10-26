// Helpers shared by the C_Human sources (human*.cpp); not part of the human's interface.
#pragma once

#include <cstdint>

#include "game/pending.h"

namespace {

// Which sides of the game this machine runs (g_gameRole).
inline bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
inline bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
// Single player: server and client in one.
inline bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

// Morale factor of enemies on the second difficulty level (0x00810544).
constexpr float DIFFICULTY_NORMAL_MORALE = 0.95f;

// Material id of helmets (hit effects of HIT_ZONE_HELMET).
constexpr int HELMET_MATERIAL = 12;

// Frame type mask of the dummies of human models ("f" body, "z" alternative body, "j"
// hand); type 11 (not identified further).
constexpr uint32_t FRAME_MASK_MODEL_CHILD = 0x400;

// Music track (C_SoundModule::PlayMusic) played when a soldier of the player's squad dies.
constexpr int MUSIC_SOLDIER_DIED = 7;

}  // namespace
