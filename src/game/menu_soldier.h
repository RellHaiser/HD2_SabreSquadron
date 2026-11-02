// The soldier the equipment menus show (original 0x007d0200..0x007d3650; 0x210 bytes, aligned to
// 16; made by the menus' base 0x00670c00, freed through 0x00670e40): a model of its own scene
// that turns slowly in front of the game's camera, idles, holds the item in its hands and wears
// the uniform, head gear, backpack, long gun and the holsters' items chosen in the menu.
#pragma once

#include <cstdint>

#include "game/human_equipment.h"
#include "ls3d/math.h"

class C_HumanWeaponPose;
class I3D_animation_set;
class I3D_frame;
class I3D_model;
class I3D_scene;

// Places of the soldier's items (AddItem / RemoveItem).
enum E_MenuSoldierPlace {
    MENU_SOLDIER_HEAD = 0x1,
    MENU_SOLDIER_BODY = 0x2,                            // the uniform: the model "mp_<model name>"
    MENU_SOLDIER_HANDS = 0x4,
    MENU_SOLDIER_BACKPACK = 0x8,
    MENU_SOLDIER_LONG_GUN = 0x10,
    MENU_SOLDIER_HOLSTERS = 0x20,
    MENU_SOLDIER_BACKPACK_CONTENTS = 0x40,              // (nothing shown)
};

class alignas(16) C_MenuSoldier {
public:
    C_MenuSoldier() = default;
    C_MenuSoldier(const C_MenuSoldier &) = delete;
    C_MenuSoldier &operator=(const C_MenuSoldier &) = delete;

    // The scene in the screen rectangle (pixels), its lights and camera before the game's camera,
    // the animations and the naked model "mp_Na01".
    void Setup(float left, float top, float right, float bottom);   // 0x007d02c0
    // The model (with a fresh pose, the joints, the face, the holsters and the items again);
    // null: the items, the holsters and the model released.
    void SetModel(I3D_model *model);                    // 0x007d1bf0
    // Releases the model, the scene and the animations.
    void Release();                                     // 0x007d0ee0
    // An item of type `typeId` at a place (E_MenuSoldierPlace; holsters: one more item).
    void AddItem(uint32_t place, int typeId);           // 0x007d1fc0
    // The item at a place away (holsters: one of `typeId`, all: every holster hidden).
    void RemoveItem(uint32_t place, int typeId, bool all);   // 0x007d3080
    // The diving gear's frames released (inline in the menus' 0x00670e40, after Release).
    void ReleaseDivingGear();
    // Every item away and the naked model again.
    void ResetItems();                                  // 0x007d3220
    // The face texture of the model (kept for the next model).
    void SetFace(const char *texture);                  // 0x007d3360
    // Turns the soldier, blends the animations, updates the pose and the scene.
    void Tick(int time);                                // 0x007d3510
    void Render();                                      // 0x007d35d0

    // The holsters.
    void HideHolsters();                                // 0x007d0f30
    void ReleaseHolsters();                             // 0x007d0f80
    void InitHolsters();                                // 0x007d0fe0
    void RemoveFromHolster(int typeId, int count);      // 0x007d1a70
    void AddToHolster(int typeId, int count);           // 0x007d1b20
    void AddToHolsterSlot(int slot, int count);         // 0x007d1b40

    // The animations: an idle one (-1: the first one at full weight) and a pose (0..8).
    void PlayIdle(int index);                           // 0x007d32a0
    void PlayPose(int index);                           // 0x007d3310
    // (The original returns a leftover of its last division.)
    void UpdateAnims(int time);                         // 0x007d3390

    I3D_scene *GetScene() const { return m_scene; }
    I3D_model *GetModel() const { return m_model; }

private:
    static constexpr int ANIM_COUNT = 9;
    static constexpr int DIVING_GEAR_COUNT = 4;

    S_vector m_pos = {0.0f, 0.0f, 0.0f};                // +0x00 the game camera's position
    float m_posW = 0.0f;                                // +0x0C (its matrix's w, copied along)
    S_vector m_axis = {0.0f, 1.0f, 0.0f};               // +0x10 the axis it turns about
    float m_axisW = 0.0f;                               // +0x1C
    float m_angle = 0.0f;                               // +0x20
    I3D_scene *m_scene = nullptr;                       // +0x24
    I3D_model *m_model = nullptr;                       // +0x28
    C_HumanWeaponPose *m_pose = nullptr;                // +0x2C
    int m_idle = -1;                                    // +0x30 the idle animation playing
    int m_poseAnim = -1;                                // +0x34 the pose playing
    int m_idleStage = 0;                                // +0x38 0 / 1
    int m_poseStage = 2;                                // +0x3C 2 / 3
    int m_idleFade = 0;                                 // +0x40 ms
    int m_poseFade = 0;                                 // +0x44 ms
    int m_idleTime = 6000;                              // +0x48 ms to the next idle animation
    I3D_animation_set *m_anims[ANIM_COUNT] = {};        // +0x4C
    bool m_supportHand = false;                         // +0x70
    I3D_frame *m_handDummy = nullptr;                   // +0x74 "HW1"
    I3D_frame *m_jointD = nullptr;                      // +0x78 the back
    I3D_frame *m_jointF = nullptr;                      // +0x7C the head
    I3D_frame *m_backpackJoint = nullptr;               // +0x80 ("d" again)
    I3D_frame *m_jointU = nullptr;                      // +0x84
    I3D_frame *m_jointQ = nullptr;                      // +0x88
    I3D_frame *m_handsModel = nullptr;                  // +0x8C
    I3D_frame *m_longGunModel = nullptr;                // +0x90
    I3D_frame *m_headModel = nullptr;                   // +0x94
    I3D_frame *m_backpackModel = nullptr;               // +0x98
    int m_handsTypeId = -1;                             // +0x9C
    int m_longGunTypeId = -1;                           // +0xA0
    int m_headTypeId = -1;                              // +0xA4
    int m_backpackTypeId = -1;                          // +0xA8
    I3D_frame *m_divingGear[DIVING_GEAR_COUNT] = {};    // +0xAC
    S_HolsterSlot m_holsters[HOLSTER_SLOT_COUNT];       // +0xBC
    const char *m_face = nullptr;                       // +0x20C
};
