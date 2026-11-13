// The three objects of a human's body part (S_HumanBody in human.h): movement (animation of
// the model, collision, physics of the body; human_movement.h), work (what the hands do:
// weapons, items, gestures; 0x0077d270..0x007805xx) and equipment (models of the carried items,
// human_equipment.h; the work in human_work.cpp).
#pragma once

#include <cstdint>

#include "game/human_equipment.h"
#include "game/human_movement.h"
#include "ls3d/math.h"

class C_Actor;
class C_chunk;
class C_Human;
class C_Item;
class I3D_frame;
class I3D_sector;

// The requests of C_HumanWork (S_MoveRequest::changed; also the bits of its blocking masks and
// of the refused requests).
enum : uint32_t {
    WORK_REQUEST_ANIM = 0x1,                            // Play
    WORK_REQUEST_ACTIVITY = 0x2,                        // PlayWorkAnimation
    WORK_REQUEST_STANCE = 0x8,                          // SetStance
    WORK_REQUEST_GAIT = 0x10,                           // SetGait
    WORK_REQUEST_LOOK_AT = 0x40,                        // SetLookAt
    WORK_REQUEST_CARRY_MODE = 0x100,                    // SetCarryMode
    WORK_REQUEST_ACTION = 0x200,                        // DoAction
    WORK_REQUEST_UNKNOWN5C = 0x400,                     // SetUnknown5C
    WORK_REQUEST_FACING = 0x800,                        // SetFacingDir
    WORK_REQUEST_VIEW_DIR = 0x1000,                     // SetViewDir
    WORK_REQUEST_HIT = 0x2000,                          // ApplyHit
    WORK_REQUEST_INTERACT = 0x4000,                      // Interact
    WORK_REQUEST_LOOK = 0x8000,                         // SetLook
    WORK_REQUEST_WORKING_FRAME = 0x10000,               // SetWorkingFrame(2)
};

// C_HumanWork::m_mode values identified so far.
enum {
    WORK_MODE_SWIMMING = 5,
};

// What a human's body is asked to do (0xE0 bytes, 16-byte aligned; 0x0077e1e0..0x00780c70,
// human_work.cpp): the controller, the scripts and the items set the requests (the animation,
// the activity, the stance and gait, the carry mode, actions, gestures, directions, hits);
// requests that the human's state blocks are refused (m_refused) unless forced (m_forced);
// Update passes them on to the movement's parts each frame.
class C_HumanWork {
public:
    C_HumanWork();                                      // 0x0077e1e0
    void SetMovement(C_HumanMovement *movement);        // 0x0077e200
    // Back to the defaults for the animation group `group` of the human.
    void SetGroup(int group);                           // 0x0077e220
    void Reset();                                       // 0x0077e280
    bool SaveGame(C_chunk &chunk);                      // 0x00780280
    bool LoadGame(C_chunk &chunk);                      // 0x00780560
    // Passes the requests on to the movement (and gives it a copy of them).
    void Update(int time);                              // 0x0077e290
    // After `time` ms the stance becomes `stance` (time MOVE_NO_TIME: at once). `blocked`
    // (the requests to block meanwhile: the animation player passes 8 / 9, the timer its
    // +0xC0) is not kept.
    void SetStanceTimer(int time, int stance, uint32_t blocked);   // 0x007800d0
    void UpdateStanceTimer(int time);                   // 0x00780140

    // The requests (`on`: marked in S_MoveRequest::changed).
    void Play(int animation, bool on);                  // 0x0077f060
    void PlayWorkAnimation(int animation, bool on);     // 0x0077f4b0
    void SetStance(int stance, bool on);                // 0x0077f500
    // The sub-variant of the moving animations (3: limping).
    void SetGait(int gait, bool on);                    // 0x0077f5f0
    void SetLookAt(const S_vector4 &target, bool on);   // 0x0077f6c0
    // Carry mode of the hands item for the animations (100: empty hands; `notify` flags the
    // change). Ignored while the work is busy unless SetUnknownB0(true) forces it.
    void SetCarryMode(int carryMode, bool notify);      // 0x0077f750
    void DoAction(int action, bool on);                 // 0x0077f7e0
    void SetUnknown5C(bool value, bool on);             // 0x0077f830 (DS 0x007ece60)
    void SetFacingDir(const S_vector &dir, bool on);    // 0x0077f8c0
    void SetViewDir(const S_vector &dir);               // 0x0077faf0 (DS 0x007ed170)
    void SetWorkingFrame(I3D_frame *frame, bool on);    // 0x0077fb70
    // (Takes a reference of the frame.)
    void SetWorkingFrame2(I3D_frame *frame, bool on);   // 0x0077fbc0
    void ApplyHit(uint32_t flags, const S_vector &pos, const S_vector &force);   // 0x0077fc10 (DS 0x007ed2d0)
    void Die(C_Actor *killer) { m_request.killer = killer; }   // 0x00780000
    void Interact(C_Actor *target, uint32_t interaction, int arg, bool on);   // 0x00780010
    // Where the head looks (C_HumanWeaponPose::SetLook; LOOK_* flags, `at` for LOOK_AT).
    void SetLook(uint32_t flags, const S_vector4 *at);  // 0x00780090 (DS 0x007ed830)
    // The movement's view direction (C_HumanMovement +0x940).
    S_vector4 GetViewDir() const;                       // 0x0077f030

    int GetUnknown5C() const { return m_request.unknown5C; }
    int GetUnknown54() const { return m_request.carryMode; }   // (0 / 6: no carry animation)
    // Animation group of the human (C_Human::GetGroup(); the movement table).
    int GetGroup() const { return m_group; }
    const S_vector4 &GetUnknown10() const { return m_request.unknown10; }
    const S_vector4 &GetFacingDir() const { return m_request.facingDir; }
    void SetChanged(uint32_t changed) { m_request.changed = changed; }
    void SetUnknownB8(int mode) { m_mode = mode; }
    // Clears the per-frame flags; ORs bits into the body flags / blocked requests / +0xD8.
    void ClearFlagsC8CCD8()
    {
        m_refused = 0;
        m_bodyFlags = 0;
        m_flagsD8 = 0;
    }
    void OrFlagsCC(uint32_t bits) { m_bodyFlags |= bits; }
    void SetFlagsD0(uint32_t bits, bool on) { m_blocked = on ? m_blocked | bits : m_blocked & ~bits; }
    void OrFlagsD8(uint32_t bits) { m_flagsD8 |= bits; }
    int GetStance() const { return m_request.stance; }
    int GetGait() const { return m_request.gait; }
    int GetUnknownB8() const { return m_mode; }
    uint32_t GetUnknownC8() const { return m_refused; }
    // Body flags; TakeFlags() returns them and clears the one-frame bits (mask 0xFFFD0FEF).
    uint32_t TakeFlags()
    {
        const uint32_t flags = m_bodyFlags;
        m_bodyFlags &= 0xFFFD0FEF;
        return flags;
    }
    void SetUnknownB0(bool on) { m_forced = on; }

    S_MoveRequest m_request;                            // +0x00
    int m_group = 1;                                    // +0xA0
    // The movement, for its parts and for its own members (the same object).
    C_HumanMovement *m_movement = nullptr;              // +0xA4
    C_HumanMovement *m_movementBase = nullptr;          // +0xA8
    C_Human *const *m_owner = nullptr;                  // +0xAC the movement's owner member
    bool m_forced = false;                              // +0xB0 the requests pass even when blocked
    int m_playCount = 0;                                // +0xB4 Play requests since the last update
    // 1 a ladder / window, 2 / 3 lying down, 4, 5 swimming (C_HumanMovement::SetSeatMode).
    int m_mode = 0;                                     // +0xB8
    int m_stanceTimer = MOVE_NO_TIME;                   // +0xBC ms to SetStance(m_timerStance)
    uint32_t m_blockedTimer = 0;                        // +0xC0 requests blocked (cleared with the stance timer)
    int m_timerStance = 0;                              // +0xC4
    uint32_t m_refused = 0;                             // +0xC8 blocked requests since the last update
    uint32_t m_bodyFlags = 0;                           // +0xCC TakeFlags
    uint32_t m_blocked = 0;                             // +0xD0 SetFlagsD0
    uint32_t m_blocked2 = 0;                            // +0xD4
    uint32_t m_flagsD8 = 0;                             // +0xD8
    uint8_t m_unknownDC[4] = {};

private:
    // Whether the request `bit` is blocked (not forced): it is then refused.
    bool IsBlocked(uint32_t bit) const { return ((m_blocked | m_blocked2) & bit) != 0 && !m_forced; }
};

