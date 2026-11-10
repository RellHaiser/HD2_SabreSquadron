// The models of the items on a human's body (S_HumanBody::equipment; original source
// 0x0078cd20..0x007965e0, human_equipment.cpp): the helmet, the long gun on the back, the
// backpack, the item in the hands, a "special" object carried in the hands (places 4..9) and
// the holster set (pouches, holsters, grenades) whose frames are shown while the human carries
// the items of their slot.
#pragma once

#include <cstdint>

#include "ls3d/math.h"

class C_Actor;
class C_chunk;
class C_Human;
class C_Item;
class I3D_frame;

// Places of the item models (GetModel / AttachModel / DetachModel).
enum E_EquipmentPlace {
    EQUIPMENT_PLACE_HEAD = 0,
    EQUIPMENT_PLACE_BACKPACK = 1,
    EQUIPMENT_PLACE_LONG_GUN = 2,
    EQUIPMENT_PLACE_HANDS = 3,
    // Special objects in the hands (the model at m_specialModel); 4..9.
    EQUIPMENT_PLACE_SPECIAL_FIRST = 4,
    EQUIPMENT_PLACE_SPECIAL_LAST = 9,
    // Place 9 is a carried body (its model plays the carried animation; C_HumanMovement
    // OnPickUpBody / OnDropBody).
    EQUIPMENT_PLACE_CARRIED = 9,
    EQUIPMENT_PLACE_NONE = 11,
};

// C_HumanEquipment::OnAnimationEvent() events.
enum E_EquipmentEvent {
    EQUIPMENT_EVENT_0 = 0,
    EQUIPMENT_EVENT_1 = 1,
    EQUIPMENT_EVENT_GIVE = 2,             // hand-over: the receiver takes the model
    EQUIPMENT_EVENT_TAKE = 3,             // hand-over done on this side
    // 4 + 2k: DetachModel(4 + k), 5 + 2k: AttachModel(4 + k), k = 0..5.
    EQUIPMENT_EVENT_SPECIAL_FIRST = 4,
    EQUIPMENT_EVENT_SPECIAL_LAST = 0x0F,
    // The round of the hands weapon (launchers) goes to the hand dummy "HW2" / "HW1" / back
    // into the weapon (C_Item::AttachProjectile) while reloading.
    EQUIPMENT_EVENT_PROJECTILE_HAND2 = 0x10,
    EQUIPMENT_EVENT_PROJECTILE_HAND1 = 0x11,
    EQUIPMENT_EVENT_PROJECTILE_WEAPON = 0x12,
};

// Holster slots (GetHolsterSlot): 0 is the sidearm holster.
enum : int {
    HOLSTER_SLOT_SIDEARM = 0,
    HOLSTER_SLOT_COUNT = 14,              // also "none"
};

// C_HumanEquipment::GetFlags().
enum : uint32_t {
    EQUIPMENT_HANDS_PUT_AWAY = 0x1,       // the hands item is put away for an action
    EQUIPMENT_EVENT0_PENDING = 0x2,       // animation event 0 waits for the hands item
};

// Items of a holster slot and the frames of the holster set that show them.
struct S_HolsterSlot {                                  // 0x18 bytes
    int itemCount = 0;
    int frameCount = 0;                                 // at most 4
    I3D_frame *frames[4] = {};
};

class C_HumanEquipment {                                // 0x230 bytes
public:
    explicit C_HumanEquipment(C_Human *owner);          // 0x0078ce60
    ~C_HumanEquipment();                                // 0x0078ce90
    C_HumanEquipment(const C_HumanEquipment &) = delete;
    C_HumanEquipment &operator=(const C_HumanEquipment &) = delete;

    // Builds the models of what the human carries (mission start).
    void Init();                                        // 0x0078cec0
    void Done();                                        // 0x0078e290
    void Reset();                                       // 0x00793d10
    bool SaveGame(C_chunk &chunk);                      // 0x00793d20
    bool LoadGame(C_chunk &chunk);                      // 0x00794890
    void Update(int time);                              // 0x00793140
    void Unknown7938f0(int arg);                        // 0x007938f0
    void Unknown793b40();                               // 0x00793b40

    // Inventory changes (C_ItemsModule).
    // `item` left container `container` (for `to` / `toContainer`, if any).
    void OnItemRemoved(C_Item *item, int container, C_Actor *to, int toContainer);   // 0x00792f10
    // A new item was put into `container`.
    void OnItemCreated(C_Item *item, int container);    // 0x007929f0
    // `item` arrived in `container` from container `fromContainer` of `from`.
    void OnItemAdded(C_Item *item, int container, C_Actor *from, int fromContainer, bool unknown);   // 0x00792b80
    // `count` items of type `typeId` were added to / removed from the belt (holster frames).
    void AddToHolster(int typeId, int count);           // 0x00791e20
    void RemoveFromHolster(int typeId, int count);      // 0x00791d70

    // Animation events of the body (take / put away models at the right frame).
    void OnAnimationEvent(int event);                   // 0x00791830 (DS 0x007ffa20)
    // Links the model of `place` where it belongs / takes it off.
    void AttachModel(int place);                        // 0x0078e3c0 (DS 0x007fb8d0)
    void DetachModel(int place);                        // 0x00790980 (DS 0x007fe7d0)
    void SetModel(I3D_frame *model, int place, int uid, int time, int typeId);   // 0x007916f0
    I3D_frame *GetModel(int place) const;               // 0x007917d0
    // Puts the hands item away for an action (true) / takes it back.
    void PutAwayHandsItem(bool putAway);                // 0x007920c0
    bool ShowHandsItem(bool show, int unused, bool toggle);   // 0x00791f20
    // C_Item::GetProjectileKind() of the hands item, 0 without one.
    int GetHandsProjectileKind();                       // 0x00792070
    void SetHolster4Visible(bool on);                   // 0x00791d20
    // The light of the special object in the hands (single player): a light at its "fire"
    // dummy for about a second and its "lensf" lens flare; off also drops the object.
    void SetFlashlight(bool on);                        // 0x00793610
    // The special objects go to the first hand dummy (the second one otherwise; m_unknown2c).
    void SetSpecialHandDummy1(bool on) { m_unknown2c = on; }
    void UpdateLensFlare(bool on);                      // 0x00793830 (on in DI)
    void Unknown792310();                               // 0x00792310
    // Where a dropped item is put down (EQUIPMENT_PLACE_*; others: the hand).
    S_vector GetDropPosition(int place);                // 0x00792350
    uint32_t GetFlags() const { return m_flags; }
    // Renames the models for the body's shadow refresh and back.
    void UpdateModelNames();                            // 0x00792510
    // Takes the joints of the human's model (after a model change).
    // (The frame argument, the model or the human's frame, is not used.)
    void FindJoints(I3D_frame *unused);                 // 0x0078d0f0
    // 1 while a body is carried, 0 otherwise (C_HumanMovement::OnPickUpBody / OnDropBody).
    void SetUnknownD0(float value) { m_unknownD0 = value; }
    // Forgets the item in the hands (inline in C_ItemsModule 0x007dc1e0).
    void ResetHands()
    {
        m_handsTime = NO_TIME;
        m_handsModel = nullptr;
        m_handsUid = -1;
        m_handsTypeId = -1;
    }
    // Holster slot of an item type; HOLSTER_SLOT_COUNT: not shown on the body.
    static int GetHolsterSlot(int typeId);              // 0x00791b80 __fastcall

    // The long gun on the back lifts while 1 (the movement; 2 at the death).
    void SetLongGunLift(int state) { m_longGunLift = state; }

private:
    static constexpr int NO_TIME = -10000;

    void Clear();                                       // 0x0078cde0
    void HideHolsters();                                // 0x0078d1c0
    void ReleaseHolsters();                             // 0x0078d210
    // Loads the holster set of the human's model and links its frames to the joints.
    void InitHolsters();                                // 0x0078d280
    // Shows the holster frames of the belt's items (inline in Init, LoadGame, Unknown793b40).
    void AddBeltToHolsters();
    // Gives an item model the body's light level (object visuals only).
    void SetLightLevel(I3D_frame *model) const;

    C_Human *m_owner;                                   // +0x00
    C_Human *m_giveTo = nullptr;                        // +0x04 hand-over: gets m_giveToPlace's model
    int m_giveToPlace = 0;                              // +0x08
    C_Human *m_takeFrom = nullptr;                      // +0x0C hand-over: loses / gets m_takeFromPlace's model
    int m_takeFromPlace = 0;                            // +0x10
    bool m_takeFromAttach = false;                      // +0x14
    I3D_frame *m_handsModel = nullptr;                  // +0x18
    I3D_frame *m_specialModel = nullptr;                // +0x1C
    I3D_frame *m_longGunModel = nullptr;                // +0x20
    I3D_frame *m_headModel = nullptr;                   // +0x24
    I3D_frame *m_backpackModel = nullptr;               // +0x28
    bool m_unknown2c = true;                            // +0x2C
    int m_handsUid = -1;                                // +0x30
    int m_handsTypeId = -1;                             // +0x34
    int m_backpackTypeId = -1;                          // +0x38
    int m_longGunTypeId = -1;                           // +0x3C
    int m_longGunUid = 0;                               // +0x40
    int m_headUid = 0;                                  // +0x44
    int m_backpackUid = 0;                              // +0x48
    bool m_handsShown = true;                           // +0x4C
    uint32_t m_flags = 0;                               // +0x50 EQUIPMENT_*
    int m_specialTime = NO_TIME;                        // +0x54
    I3D_frame *m_divingGear[4] = {};                    // +0x58 clones of the backpack item's
    I3D_frame *m_flashlight = nullptr;                  // +0x68 "m_SpecFrameLight"
    int m_flashlightTime = NO_TIME;                     // +0x6C
    int m_lensFlareTime = NO_TIME;                      // +0x70
    I3D_frame *m_jointD = nullptr;                      // +0x74 joint "d": long gun on the back
    I3D_frame *m_jointF = nullptr;                      // +0x78 joint "f": head
    I3D_frame *m_backpackJoint = nullptr;               // +0x7C joint "d" too
    I3D_frame *m_jointU = nullptr;                      // +0x80
    I3D_frame *m_jointQ = nullptr;                      // +0x84
    uint8_t m_unknown88[8] = {};
    S_quat m_specialRot;                                // +0x90
    S_vector m_specialPos;                              // +0xA0
    uint8_t m_unknownAC[4] = {};
    int m_specialPlace = EQUIPMENT_PLACE_NONE;          // +0xB0
    I3D_frame *m_handDummy1 = nullptr;                  // +0xB4 "HW1"
    I3D_frame *m_handDummy2 = nullptr;                  // +0xB8 "HW2"
    // The long gun on the back lifts (tilts and slides) while m_longGunLift is 1 (set by the
    // movement) and returns otherwise (Update).
    float m_longGunTilt = 0.0f;                         // +0xBC rad
    float m_longGunSlide = 0.0f;                        // +0xC0
    float m_longGunMaxTilt = 0.0f;                      // +0xC4
    float m_longGunMaxSlide = 0.0f;                     // +0xC8
    int m_longGunLift = 0;                              // +0xCC
    float m_unknownD0 = 0.0f;                           // +0xD0
    int m_handsTime = NO_TIME;                          // +0xD4 700 once the hands model is set
    S_HolsterSlot m_holsters[HOLSTER_SLOT_COUNT];       // +0xD8
    uint8_t m_unknown228[8] = {};
};
