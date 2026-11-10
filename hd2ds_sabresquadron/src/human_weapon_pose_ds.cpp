// The server's stand-ins for src/game/human_weapon_pose.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/human_pose_internal.h"
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_body.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/human_stains.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "game/world.h"
#include "ls3d/i3d.h"

// 0x0079bd70: not in the server.
C_HumanWeaponPose::C_HumanWeaponPose()
{
}

// 0x0079bf20: not in the server.
C_HumanWeaponPose::~C_HumanWeaponPose()
{
}

// 0x0079f3f0: not in the server.
bool C_HumanWeaponPose::LoadGame(C_chunk &chunk)
{
    return true;
}

// 0x0079d540: not in the server.
bool C_HumanWeaponPose::SaveGame(C_chunk &chunk)
{
    return true;
}

// 0x0079c190: not in the server.
void C_HumanWeaponPose::Attach(I3D_frame *model, C_Human *owner, bool reload)
{
}

// 0x0079c780: not in the server.
void C_HumanWeaponPose::BindSkeleton(int mode)
{
}

// 0x007a8760: not in the server.
void C_HumanWeaponPose::Refresh(int unused)
{
}

// 0x0079d530: not in the server.
void C_HumanWeaponPose::Reset()
{
}

// Not in the server.
void C_HumanWeaponPose::SetBlendLength(int time)
{
}

// Inline in the server (DS 0x007ba9a0).
void C_HumanWeaponPose::SetSeatStance(int stance)
{
    m_stance = stance;   // the server's (inline in UpdateInteraction): the stance only, no prone fit
}

// 0x007b8a00: not in the server.
void C_HumanWeaponPose::SetStainsVisible(bool on)
{
}

// Inline in the server (DS 0x007d3fe0).
void C_HumanWeaponPose::SetStanceChange(int stance)
{
    m_stance = stance;   // the server's (inline in ChangeStance): the stance only, no prone fit
}

// 0x007a78d0: not in the server.
void C_HumanWeaponPose::Update(int time)
{
}
