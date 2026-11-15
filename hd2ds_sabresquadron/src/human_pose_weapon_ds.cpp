// The server's stand-ins for src/game/human_pose_weapon.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/human_pose_internal.h"
#include <cmath>
#include <cstring>
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/world.h"
#include "ls3d/i3d.h"

// The server's own small copy (DS 0x00805dc0): the raised state and the hold bit only, no blend.
void C_HumanWeaponPose::LowerWeapon(int /*time*/)
{
    if (!m_raised || !m_weapon)
        return;
    m_raised = false;
    m_flags &= ~static_cast<uint32_t>(POSE_FLAG_HOLD);
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

// The server's own small copy (DS 0x00805d90): no blend and no steer / static / seat test; the raise
// bit is kept as it was, not set.
void C_HumanWeaponPose::RaiseWeapon(int /*time*/)
{
    if (m_raised || !m_weapon)
        return;
    m_raised = true;
    m_flags = (m_flags & 0x4FFFFFC2u) | POSE_FLAG_HOLD;
}

// The server's own small copy (DS 0x00805df0): the hand bits and the grip flag only, nothing linked
// (mode 2: the other hand, as on the client).
void C_HumanWeaponPose::SetMode(int mode)
{
    if (!m_weapon)
        return;
    if (mode == 2) {
        const uint32_t side = m_hand & 0xFF;
        if (side == 0)
            mode = 1;
        else if (side == 1)
            mode = 0;
        else
            mode = -1;
    }
    if (mode == 1)
        m_hand = (m_hand & 0xFFFFFFF1u) | 1;
    else if (mode == 0)
        m_hand &= 0x7FFFFFF0u;
    m_flags |= POSE_FLAG_GRIP;
}

// The server's own small copy (DS 0x00805d10): the weapon referenced, no grips, behaviour or kind.
void C_HumanWeaponPose::SetModel(I3D_frame *model, int /*behaviour*/)
{
    if (m_weapon == model)
        return;
    if (m_weapon)
        m_weapon->Release();
    if (!model) {
        m_weapon = nullptr;
        m_flags &= 0x03FFFFFCu;
        m_hand &= 0x7FFFFFFFu;
        m_raised = false;
        return;
    }
    m_weapon = model;
    model->AddRef();
    SetMode(1);
}

// The server's own small copy (DS 0x00805e80): the hand bit and the grip flag only, no grip test or
// timing; off leaves the hand bit set.
void C_HumanWeaponPose::SetSupportHand(bool on)
{
    if (on) {
        if (IsSupportHandOn())
            return;
        m_hand |= 0x80000000u;
    } else if (!IsSupportHandOn()) {
        return;
    }
    m_flags |= POSE_FLAG_GRIP;
}

// 0x007b92e0: not in the server.
void C_HumanWeaponPose::SetViewDir(const S_vector &view)
{
}

// 0x007ae7f0: not in the server.
void C_HumanWeaponPose::StartRecoil(I3D_animation_set * /*unused*/)
{
}
