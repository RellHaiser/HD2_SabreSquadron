// Harness stub of game/world.h: doors with a settable state.
#pragma once

#include "game/actor.h"

class C_Door : public C_Actor {
public:
    int GetMotion() const { return m_state; }
    int m_state = 0;
};
