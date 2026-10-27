// Harness stub of game/actor.h.
#pragma once

#include "ls3d/i3d.h"

enum E_ActorType {
    ACTOR_OTHER = 1,
    ACTOR_DOOR = 6,
};

class C_Actor {
public:
    E_ActorType GetType() const { return m_type; }
    E_ActorType m_type = ACTOR_OTHER;
};

inline C_Actor *GetFrameActor(const I3D_frame *frame)
{
    return frame ? frame->actor : nullptr;
}
