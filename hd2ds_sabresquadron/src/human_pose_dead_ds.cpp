// The server's stand-ins for src/game/human_pose_dead.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/human_pose_internal.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include "game/actor.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/human_stains.h"
#include "game/pending.h"
#include "game/sound_module.h"
#include "game/world.h"
#include "ls3d/i3d.h"

// 0x007b3c20: not in the server.
void C_HumanWeaponPose::AddSettledBloodPool()
{
}

// 0x007b88b0: not in the server.
void C_HumanWeaponPose::OnHit(uint32_t flags, const S_vector &pos, const S_vector &force)
{
}

// 0x007b3510: not in the server.
void C_HumanWeaponPose::PickUpBody()
{
}

// 0x007b0d70: not in the server.
void C_HumanWeaponPose::StartDeath(uint32_t hitFlags, const float *force, I3D_animation_set *deathAnim, int kind, C_Human *carrier)
{
}
