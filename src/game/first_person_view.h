// The first-person view (C_game +0x5C; the original's C_FirstPersonView, 0x00490740..0x00493610,
// game/first_person_view.cpp): the active soldier's hands and weapon in front of the camera
// (clients only). The weapon's first-person model is put into a copy of the hands model (the
// uniform's "FPV_hands") and played with the animations tables\FpvAnims.sav gives each item type
// and event; it sways with the soldier's speed, drops while the weapon is lowered or a belt
// grenade is thrown, flashes at the muzzle, and eases the camera's fields of view. The game's
// "HUD" option (g_hudOption) is this view; the binoculars' view hides it.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "ls3d/math.h"

class C_chunk;
class C_Human;
class I3D_animation_set;
class I3D_frame;
class I3D_model;

// What the first-person model plays (C_FirstPersonView::OnInventoryEvent; the animations of an
// item type are its events 0..12, named in tables\FpvAnims.sav as "#FPV<weapon><event>.I3D").
enum E_FpvEvent : int {
    FPV_EVENT_IDLE = 0,                                 // "Idle1" (variants "Idle1a" ...)
    FPV_EVENT_IDLE_EMPTY = 1,                           // "Idle1LST": FPV_EVENT_IDLE of an empty weapon
    FPV_EVENT_FIRE = 2,                                 // "Shot"; a grenade's throw
    FPV_EVENT_FIRE_LAST = 3,                            // "ShotLST": the last round
    FPV_EVENT_AIM_FIRE = 4,                             // "AimShot": through the sights; a grenade's other throw
    FPV_EVENT_AIM_FIRE_LAST = 5,
    FPV_EVENT_RELOAD = 6,
    FPV_EVENT_JAMMED = 7,                               // "Jammed": the trigger of an empty weapon
    FPV_EVENT_DRAW = 8,                                 // "Arm": the hands item taken (also back after an action)
    FPV_EVENT_PUT_AWAY = 9,                             // "Disarm": put away (also for an action)
    FPV_EVENT_PUT_AWAY_EMPTY = 10,                      // "DisarmLST" (no caller sends it)
    FPV_EVENT_AIM_IN = 11,                              // "Aim": the sights / scope raised
    FPV_EVENT_AIM_OUT = 12,                             // "Daim"
    FPV_EVENT_COUNT = 13,
    FPV_EVENT_CLEARED = 20,                             // the hands are empty
    FPV_EVENT_BELT_GRENADE = 21,                        // a belt grenade thrown
};

// C_FirstPersonView::m_pose: how the weapon is held (FPV_POSE_LOWERED / RAISED follow the
// controller's E_WeaponStance).
enum E_FpvPose : int {
    FPV_POSE_NONE = 0,
    FPV_POSE_LOWERED = 1,                               // drops to -0.2
    FPV_POSE_RAISED = 2,
    FPV_POSE_GRENADE_OUT = 3,                           // the weapon drops away for a belt grenade
    FPV_POSE_GRENADE = 4,                               // the grenade in the hands, thrown
    FPV_POSE_WEAPON_BACK = 5,                           // the weapon comes back up
};

// A stage of the model's animation: the one playing (blending in) and the ones blending out.
struct S_FpvStage {                                     // 0x10 bytes
    I3D_animation_set *anim = nullptr;
    int stage = 0;                                      // +0x04 the model's animation stage 0..2
    float speed = 0.0f;                                 // +0x08 weight change per second (5)
    float weight = 0.0f;                                // +0x0C
};

// The animations of one item type and event: up to 4 variants, picked at random by their
// ratios (cumulative percentages).
struct S_FpvAnim {                                      // 0x30 bytes
    S_FpvAnim() = default;                              // 0x00490800
    ~S_FpvAnim();                                       // 0x00490830
    S_FpvAnim(const S_FpvAnim &) = delete;
    S_FpvAnim &operator=(const S_FpvAnim &) = delete;

    I3D_animation_set *anims[4] = {};                   // loaded on first use (references)
    C_str names[4];                                     // +0x10
    int ratios[4] = {};                                 // +0x20 1..100
};

class alignas(16) C_FirstPersonView {                   // 0x4C370 bytes
public:
    static constexpr int MAX_ITEM_TYPES = 500;

    C_FirstPersonView();                                // 0x00490740
    ~C_FirstPersonView();                               // 0x004908b0
    C_FirstPersonView(const C_FirstPersonView &) = delete;
    C_FirstPersonView &operator=(const C_FirstPersonView &) = delete;

    // Mission start / end / restart (clients).
    void Init();                                        // 0x004909d0
    void Done();                                        // 0x00490b50
    void Reset();                                       // 0x00493360
    void Update(int timeDelta);                         // 0x00491e90
    // The view on / off (the "HUD" option; not in a network game's free camera).
    void Show(bool on);                                 // 0x00491fd0
    bool IsShown() const { return m_on; }               // byte +0x58
    // The binoculars' view (10 degrees; the model hidden).
    void ShowBinoculars(bool on);                       // 0x004921f0
    bool IsBinocularsShown() const { return m_binoculars; }   // byte +0x59
    // Clients: the view through the sights / scope (`on`): the camera's special mode 7, the
    // field of view `fov`, 40 degrees for the second one, the sights' animations and the
    // interface's view mode.
    void SetAimView(bool on, float fov);                // 0x00492130
    bool IsAimViewOn() const { return m_aimView; }      // byte +0x5A
    // Out of the sights: the game's field of view and 65 degrees.
    void ResetAimView();                                // 0x004920b0
    // The active soldier's hands, weapon and pose again.
    void Refresh();                                     // 0x004922a0
    // The weapon in the hands again (the hands stay).
    void RefreshWeapon();                               // 0x00492500
    // The weapon's first-person model (`weapon`, with its `magazine`; null, null, -1: none).
    void SetWeaponView(I3D_model *weapon, I3D_frame *magazine, int itemType);   // 0x00491130
    void SetPose(int pose);                             // 0x00491640 (E_FpvPose)
    // What happened to the weapon in the hands (E_FpvEvent); `nextEvent` plays when that
    // animation ends.
    void OnInventoryEvent(int event, int nextEvent);    // 0x00492930
    // The weapon's states allow using it: not throwing a belt grenade, not reloading, drawing,
    // putting away or raising / lowering the sights.
    bool CanUseWeapon() const;                          // 0x00493500
    // "cardum" / "blsdum" of the first-person model (the shells / the muzzle); null when not a
    // client or without a weapon.
    I3D_frame *GetCartridgeDummy();                     // 0x004928d0
    I3D_frame *GetBlastDummy();                         // 0x00492900
    // Where the local player aims from (a grenade: the first-person weapon's position, else
    // the camera) and along (camera 1's direction).
    S_vector4 GetAimOrigin() const;                     // 0x00492790
    S_vector4 GetAimDir() const;                        // 0x00492880
    bool SaveGame(C_chunk &chunk);                      // 0x00493390 (nothing to save)
    bool LoadGame(C_chunk &chunk);                      // 0x004933a0

private:
    void LoadHands(const char *fileName);               // 0x00490930
    void LoadAnimTable();                               // 0x00490c80
    // The sway, the pose's drop and the fields of view.
    void UpdateView(int timeDelta);                     // 0x00491660
    // The next animation when the current one ends.
    void UpdateAnim(int timeDelta);                     // 0x00491c60
    void UpdateBlend(int timeDelta);                    // 0x00491440
    void UpdateLight();                                 // 0x00491e30
    // The uniform's sleeve texture ("ARM" / "ARS") on the hands.
    void ApplySleeves();                                // 0x00492610
    void PlayAnim(int event);                           // 0x00492a90
    void StopAnims();                                   // 0x004932a0
    void PlayEventSound(int event);                     // 0x004933c0
    // The Panzerfaust's warhead ("ammo"; shown while it is loaded).
    void ShowWarhead(bool on);

    S_vector4 m_unused00 = {};                          // +0x00 (Init clears them; nothing reads them)
    S_quat m_unused10 = {};                             // +0x10
    S_vector4 m_unused20 = {};                          // +0x20
    float m_fov = 0.0f;                                 // +0x30 the camera's field of view it eases to
    float m_fov2 = 0.0f;                                // +0x34 the camera's second one
    C_Human *m_human = nullptr;                         // +0x38 the active soldier
    bool m_initialized = false;                         // +0x3C Init (clients)
    I3D_model *m_model = nullptr;                       // +0x40 the hands with the weapon in them
    I3D_model *m_hands = nullptr;                       // +0x44 "FPV_HANDS"
    I3D_model *m_weapon = nullptr;                      // +0x48 the item's first-person model
    I3D_frame *m_magazine = nullptr;                    // +0x4C
    int m_itemType = 0;                                 // +0x54 -1: none
    bool m_on = false;                                  // +0x58
    bool m_binoculars = false;                          // +0x59
    bool m_aimView = false;                             // +0x5A
    int m_pose = FPV_POSE_NONE;                         // +0x60
    float m_drop = 0.0f;                                // +0x64 the weapon's drop (0 .. -0.8)
    float m_dropSpeed = 0.0f;                           // +0x68
    float m_lightLevel = 1.0f;                          // +0x6C
    S_FpvStage m_current;                               // +0x70
    std::vector<S_FpvStage> m_fading;                   // +0x80
    int m_variant = 0;                                  // +0x90
    int m_event = 0;                                    // +0x94 the animation playing (E_FpvEvent, -1 none)
    int m_nextEvent = 0;                                // +0x98
    float m_swayPhase = 0.0f;                           // +0x9C
    S_FpvAnim m_anims[MAX_ITEM_TYPES][FPV_EVENT_COUNT]; // +0xA0
    float m_flashTime = -1.0f;                          // +0x4C360 ms the muzzle flash stays (-1: none)
    I3D_frame *m_flash = nullptr;                       // +0x4C364 the model's "flash"
};
