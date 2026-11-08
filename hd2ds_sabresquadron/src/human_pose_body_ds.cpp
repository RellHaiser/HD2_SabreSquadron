// The server's stand-ins for src/game/human_pose_body.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/human_pose_internal.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "game/collision.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "ls3d/i3d.h"

// 0x007a8980: not in the server.
void C_HumanWeaponPose::FitProneBody()
{
}

// 0x007acbe0: not in the server.
void C_HumanWeaponPose::SetLook(uint32_t flags, const S_vector4 *pos)
{
}
