// The server's stand-ins for src/game/human_pose_weapon.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/human_pose_internal.h"
#include <cmath>
#include <cstring>
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/world.h"
#include "ls3d/i3d.h"

// 0x007b91c0: not in the server.
void C_HumanWeaponPose::LowerWeapon(int time)
{
}

// 0x007b9220: not in the server.
void C_HumanWeaponPose::PauseHands(int time)
{
}

// 0x007b9270: not in the server.
void C_HumanWeaponPose::PauseWeapon(int time, bool free)
{
}

// 0x007ae780: not in the server.
void C_HumanWeaponPose::PlayActivity(I3D_animation_set *anim)
{
}

// 0x007b9160: not in the server.
void C_HumanWeaponPose::RaiseWeapon(int time)
{
}

// 0x007b96c0: not in the server.
void C_HumanWeaponPose::SetMode(int mode)
{
}

// 0x007b9510: not in the server.
void C_HumanWeaponPose::SetModel(I3D_frame *model, int behaviour)
{
}

// 0x007b9b20: not in the server.
void C_HumanWeaponPose::SetSupportHand(bool on)
{
}

// 0x007b92e0: not in the server.
void C_HumanWeaponPose::SetViewDir(const S_vector &view)
{
}

// 0x007ae7f0: not in the server.
void C_HumanWeaponPose::StartRecoil(I3D_animation_set * /*unused*/)
{
}
