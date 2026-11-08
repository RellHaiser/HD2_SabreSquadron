// The server's stand-ins for src/game/human_pose_seats.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/human_pose_internal.h"
#include <cmath>
#include "game/actor.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/world.h"
#include "ls3d/i3d.h"

// 0x007ae190: not in the server.
void C_HumanWeaponPose::SetSeat(uint32_t seatFlags, C_Actor *vehicle, int seat)
{
}

// 0x007b9070: not in the server.
void C_HumanWeaponPose::SetSeatWeaponPose(I3D_animation_set *anim)
{
}
