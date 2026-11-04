// Harness stub of game/profile.h: the player's name only (harness.cpp defines it).
#pragma once

#include "common/c_str.h"

class C_PlayerProfile {
public:
    C_str GetName() const;
};
class C_GameProfile {
public:
    C_PlayerProfile *GetPlayerProfile() const { return m_player; }
    C_PlayerProfile *m_player = nullptr;
};
extern C_GameProfile g_gameProfile;
