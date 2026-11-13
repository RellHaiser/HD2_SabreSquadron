// Item instances and the containers that hold them.
//
// The items module creates one C_Item per item somebody carries or that lies in the
// world; its type (index into the item definitions) decides the class: weapons
// (C_WeaponItem), backpacks (C_ContainerItem), ammo (C_AmmoItem) and medkits
// (C_MedkitItem). Instances: 0x007d3650..0x007d6a70; containers: 0x007d6af0..0x007d7a90.
//
// Containers: humans carry seven (C_Actor::GetItemStore, ITEM_CONTAINER_*), vehicles one,
// and every backpack has one inside. A C_ItemSlot holds one item, a C_ItemList any number
// in no order, a C_ItemStore groups equal items in slots. Containers only store; the
// weight / space / size limits are checked by the items module before AddItem.
#pragma once

#include <cstdint>
#include <vector>

#include "ls3d/math.h"

class C_Actor;
class C_chunk;
class C_ItemDef;
class C_ItemStore;
class I3D_frame;

// Containers of a human (C_Actor::GetItemStore index).
enum : int {
    ITEM_CONTAINER_HEAD = 0,
    ITEM_CONTAINER_BELT = 1,
    ITEM_CONTAINER_LONG_GUN = 2,
    ITEM_CONTAINER_HANDS = 3,
    ITEM_CONTAINER_UNIFORM = 4,
    ITEM_CONTAINER_BACKPACK = 5,
    ITEM_CONTAINER_BACKPACK_CONTENTS = 6,
    ITEM_CONTAINER_COUNT = 7,                       // also "none" / "any" in requests
};

// Inventory space of a human's belt and long-gun containers (also read by the soldier
// equipment screen).
constexpr float HUMAN_BELT_SPACE = 12.0f;               // 0x00863868
constexpr float HUMAN_LONG_GUN_SPACE = 8.0f;            // 0x0086386c

// Item types of silenced weapons (the squad's _ALT combat orders want one; the member panels
// show a mark).
enum : int {
    ITEM_SILENCED_7 = 7,
    ITEM_SILENCED_16 = 0x16,
    ITEM_SILENCED_104 = 0x104,
};
inline bool IsSilencedType(int typeId)
{
    return typeId == ITEM_SILENCED_16 || typeId == ITEM_SILENCED_7 || typeId == ITEM_SILENCED_104;
}

// Results of C_ItemContainer::AddItem / RemoveItem and the items module's checks.
enum : int {
    ITEM_RESULT_OK = 1,
    ITEM_RESULT_FAILED = -1,        // no free slot / item not in the container
    ITEM_RESULT_NO_ITEM = -2,
    ITEM_RESULT_TOO_HEAVY = -5,
    ITEM_RESULT_NO_SPACE = -6,
    ITEM_RESULT_TOO_BIG = -7,
};

// C_Item::Fire / FireSecondary results.
enum : int {
    FIRE_OK = 0,
    FIRE_NOT_A_WEAPON = 5,
};

// ---------------------------------------------------------------- containers
class C_Item;

class C_ItemContainer {                                 // vtable 0x00817068
public:
    explicit C_ItemContainer(int kind);                 // 0x007d6af0
    C_ItemContainer(const C_ItemContainer &) = delete;
    C_ItemContainer &operator=(const C_ItemContainer &) = delete;
    virtual ~C_ItemContainer() = default;               // 0x00 0x007d6c10

    virtual void SetMaxWeight(float value) { m_maxWeight = value; }       // 0x04 0x007d6b20 (DS 0x008101c0)
    virtual float GetMaxWeight() { return m_maxWeight; }                  // 0x08 0x007d6b30 (DS 0x008101e0)
    virtual void SetMaxSpace(float value) { m_maxSpace = value; }         // 0x0C 0x007d6b40
    virtual float GetMaxSpace() { return m_maxSpace; }                    // 0x10 0x007d6b50 (DS 0x00810220)
    virtual void SetMaxItemSize(int value) { m_maxItemSize = value; }     // 0x14 0x007d6b60
    virtual int GetMaxItemSize() { return m_maxItemSize; }                // 0x18 0x007d6b70 (DS 0x00810260)
    virtual float GetTotalWeight() = 0;                                   // 0x1C
    virtual float GetTotalSpace() = 0;                                    // 0x20
    // Number of items of type `typeId`.
    virtual int CountItems(int typeId) = 0;                               // 0x24
    virtual int GetUsedSlotCount() = 0;                                   // 0x28
    // ITEM_RESULT_*; a single-item slot hands back the item it held in `*replaced`.
    // `index` (-1 from every caller) is not used by any container.
    virtual int AddItem(C_Item *item, C_Item **replaced, int index) = 0;  // 0x2C
    virtual int RemoveItem(C_Item *item, int index) = 0;                  // 0x30
    // Destroys every item.
    virtual void Clear() = 0;                                             // 0x34
    virtual bool IsEmpty() = 0;                                           // 0x38
    // Slots of a C_ItemStore (GetSlotCount() of them), null for the others.
    virtual std::vector<C_Item *> *GetSlots() = 0;                        // 0x3C
    // Item of a C_ItemSlot, null for the others.
    virtual C_Item *GetItem() = 0;                                        // 0x40
    virtual int GetSlotCount() = 0;                                       // 0x44
    // A C_ItemSlot can be locked for a while (by mission time).
    virtual bool IsLocked() { return false; }                             // 0x48 0x007d6b80 (DS 0x00810280)
    virtual void SetLockTime(uint32_t) {}                                 // 0x4C 0x007d6b90
    virtual uint32_t GetLockTime() { return 0; }                          // 0x50 0x007d6ba0 (DS 0x008102c0)
    // (Slots 0x54 / 0x5C / 0x60 are empty in every container class; their arguments are
    // counted from RET n.)
    virtual void Unknown54(int) {}                                        // 0x54 0x007d6bb0
    virtual int Unknown58() { return 0; }                                 // 0x58 0x007d6bc0 (DS 0x00810300)
    virtual bool Unknown5C(int, int) { return false; }                    // 0x5C 0x007d6bd0 (DS 0x00810320)
    virtual bool Unknown60(int, int) { return false; }                    // 0x60 0x007d6be0 (DS 0x00810340)

    int GetKind() const { return m_kind; }

protected:
    int m_kind;                                         // +0x04
    float m_maxWeight = 100.0f;                         // +0x08
    float m_maxSpace = 100.0f;                          // +0x0C (an item takes twice its size, at least 1)
    int m_maxItemSize = 3;                              // +0x10
};

// One item (head, long gun, hands, uniform, backpack).
class C_ItemSlot : public C_ItemContainer {             // vtable 0x008170d0
public:
    explicit C_ItemSlot(int kind) : C_ItemContainer(kind) {}   // 0x007d6c50
    ~C_ItemSlot() override;                             // 0x007d6d70

    float GetTotalWeight() override;                    // 0x007d6dd0
    float GetTotalSpace() override;                     // 0x007d6df0
    int CountItems(int typeId) override;                // 0x007d6c90
    int GetUsedSlotCount() override { return m_item ? 1 : 0; }   // 0x007d6c80
    int AddItem(C_Item *item, C_Item **replaced, int index) override;   // 0x007d6e10
    int RemoveItem(C_Item *item, int index) override;   // 0x007d6e40
    void Clear() override;                              // 0x007d6e60
    bool IsEmpty() override { return m_item == nullptr; }       // 0x007d6cb0
    std::vector<C_Item *> *GetSlots() override { return nullptr; }   // 0x007d6cc0 (DS 0x008104b0)
    C_Item *GetItem() override { return m_item; }       // 0x007d6cd0 (DS 0x008104d0)
    int GetSlotCount() override { return 1; }           // 0x007d6ce0 (DS 0x008104f0)
    bool IsLocked() override;                           // 0x007d6cf0
    void SetLockTime(uint32_t time) override { m_lockTime = time; }   // 0x007d6d10
    uint32_t GetLockTime() override { return m_lockTime; }           // 0x007d6d20 (DS 0x00810560)
    void Unknown54(int) override {}                        // 0x007d6d30
    int Unknown58() override { return 0; }              // 0x007d6d40 (DS 0x008105a0)

private:
    C_Item *m_item = nullptr;                           // +0x14
    uint32_t m_lockTime = 0;                            // +0x18 mission time until which it is locked
};

// Any number of items, in no order (kind 3).
class C_ItemList : public C_ItemContainer {             // vtable 0x00817138
public:
    C_ItemList() : C_ItemContainer(3) {}                // 0x007d6ec0
    ~C_ItemList() override;                             // 0x007d6fc0

    float GetTotalWeight() override;                    // 0x007d7040
    float GetTotalSpace() override;                     // 0x007d7090
    // (Not implemented by the original either.)
    int CountItems(int) override { return 0; }          // 0x007d6f40 (DS 0x00810880)
    int GetUsedSlotCount() override { return 0; }       // 0x007d6f30 (DS 0x00810860)
    int AddItem(C_Item *item, C_Item **replaced, int index) override;   // 0x007d70e0
    int RemoveItem(C_Item *item, int index) override;   // 0x007d72b0
    void Clear() override;                              // 0x007d7340
    bool IsEmpty() override { return m_items.empty(); } // 0x007d73c0
    std::vector<C_Item *> *GetSlots() override { return nullptr; }   // 0x007d6f50 (DS 0x008108a0)
    C_Item *GetItem() override { return nullptr; }      // 0x007d6f60 (DS 0x008108c0)
    int GetSlotCount() override { return 0; }           // 0x007d6f70 (DS 0x008108e0)
    void Unknown54(int) override {}                        // 0x007d6f80
    int Unknown58() override { return 0; }              // 0x007d6f90 (DS 0x00810920)

private:
    std::vector<C_Item *> m_items;                      // +0x14 (heap-allocated in the original)
};

// Items grouped in slots of equal items (same type and instance kind): 14 slots for
// kind 1 (the belt), 32 otherwise. Used slots are kept at the front.
class C_ItemStore : public C_ItemContainer {            // vtable 0x008171a0
public:
    explicit C_ItemStore(int kind);                     // 0x007d7470
    ~C_ItemStore() override;                            // 0x007d75a0

    float GetTotalWeight() override;                    // 0x007d7680
    float GetTotalSpace() override;                     // 0x007d7700
    int CountItems(int typeId) override;                // 0x007d7780
    int GetUsedSlotCount() override { return m_usedSlots; }   // 0x007d7520 (DS 0x008110d0)
    int AddItem(C_Item *item, C_Item **replaced, int index) override;   // 0x007d77f0
    int RemoveItem(C_Item *item, int index) override;   // 0x007d78d0
    void Clear() override;                              // 0x007d7a10
    bool IsEmpty() override { return m_usedSlots == 0; }       // 0x007d7a90
    std::vector<C_Item *> *GetSlots() override { return m_slots.data(); }   // 0x007d7530 (DS 0x008110f0)
    C_Item *GetItem() override { return nullptr; }      // 0x007d7540 (DS 0x00811110)
    int GetSlotCount() override { return static_cast<int>(m_slots.size()); }   // 0x007d7550 (DS 0x00811130)
    void Unknown54(int) override {}                        // 0x007d7560
    int Unknown58() override { return 0; }              // 0x007d7570 (DS 0x00811170)

private:
    // +0x14 slot count, +0x18 array of heap vectors in the original.
    std::vector<std::vector<C_Item *>> m_slots;
    int m_usedSlots = 0;                                // +0x1C
};

// ---------------------------------------------------------------- instances
// C_Item::GetKind() (class of the instance).
enum E_ItemKind {
    ITEM_KIND_WEAPON = 0,
    ITEM_KIND_CONTAINER = 1,
    ITEM_KIND_AMMO = 2,
    ITEM_KIND_MEDKIT = 3,
};

class C_Item {                                          // vtable 0x00816f78, 0x28 bytes (16-aligned heap)
public:
    explicit C_Item(E_ItemKind kind) : m_kind(kind) {}  // 0x007d3e20
    C_Item(const C_Item &) = delete;
    C_Item &operator=(const C_Item &) = delete;
    virtual ~C_Item();                                  // 0x00 0x007d3e70

    // Binds the instance to item type `typeId`.
    virtual void Init(int typeId);                      // 0x04 0x007d3ea0
    // Spends `amount` rounds (weapons).
    virtual void Use(float) {}                          // 0x08 0x007d36a0
    // Rounds left / capacity; -1 where it does not apply.
    virtual float GetAmount() { return -1.0f; }         // 0x0C 0x007d3cd0 (DS 0x0080cdf0)
    virtual float GetMaxAmount() { return -1.0f; }      // 0x10 0x007d3ce0 (DS 0x0080ce10)
    // Network side of a reload: the server's result (`ammoUid` is the magazine used up, -1 none).
    virtual void SetRounds(C_Actor *, int, int) {}      // 0x14 0x007d36c0
    // Reloads from container `container` of `owner`; `*ammoUid` receives the magazine
    // used up (-1 none). See C_WeaponItem::Reload.
    virtual bool Reload(C_Actor *, int, int *, bool) { return false; }   // 0x18 0x007d36b0 (DS 0x0080c560)
    // Uses up `amount` charges (medkits).
    virtual void Consume(float) {}                      // 0x1C 0x007d36d0
    virtual float GetCharges() { return -1.0f; }        // 0x20 0x007d36e0 (DS 0x0080c5c0)
    // Fires from `pos` towards `dir`; FIRE_*. Bits 0-7 of `flags` set: no noise for the AI.
    virtual int Fire(C_Actor *shooter, const S_vector &pos, const S_vector &dir, uint32_t flags) = 0;   // 0x24
    virtual int FireSecondary(C_Actor *shooter, const S_vector &pos, const S_vector &dir, uint8_t flags) = 0;   // 0x28
    virtual float GetWeight();                          // 0x2C 0x007d36f0
    virtual int GetSize();                              // 0x30 0x007d3700
    // Inventory space taken: twice the size, at least 1.
    virtual float GetSpace();                           // 0x34 0x007d3710
    // Frame of the projectile sitting in the weapon (grenade, rocket ...), if any.
    virtual I3D_frame *GetProjectileFrame() { return nullptr; }   // 0x38 0x007d3730 (DS 0x0080c650)
    // Moves a copy of the projectile into `hand` (null: puts it back on the weapon).
    virtual void AttachProjectile(I3D_frame *) {}       // 0x3C 0x007d3740
    virtual int GetProjectileKind() { return 0; }       // 0x40 0x007d3750 (DS 0x0080c690)
    virtual bool SaveGame(C_chunk &chunk);              // 0x44 0x007d3fb0
    virtual bool LoadGame(C_chunk &chunk);              // 0x48 0x007d41f0

    C_ItemDef *GetDef() const { return m_def; }
    E_ItemKind GetKind() const { return m_kind; }
    int GetTypeId() const { return m_typeId; }
    int GetUid() const { return m_uid; }
    void SetUid(int uid) { m_uid = uid; }
    C_Actor *GetOwner() const { return m_owner; }
    void SetOwner(C_Actor *owner) { m_owner = owner; }
    // Container of the owner the item was last put in (ITEM_CONTAINER_*).
    int GetContainerIndex() const { return m_container; }
    void SetContainerIndex(int container) { m_container = container; }
    // Its model is shown in the world (dropped, or an item actor's pick-up).
    bool IsInWorld() const { return m_inWorld; }
    void SetInWorld(bool on) { m_inWorld = on; }
    // Mines are armed from Init on; armed mines placed in the mission are laid.
    bool IsArmed() const { return m_armed; }
    void SetArmed(bool on) { m_armed = on; }
    // Mission time until which the item is busy (after a move or a reload).
    uint32_t GetTime() const { return m_time; }
    void SetTime(uint32_t time) { m_time = time; }

    // The item's model (a copy of its definition's model), created on first use. Only
    // index 0 exists.
    I3D_frame *GetFrame(int index);                     // 0x007d3f00
    void ReleaseFrame(int index);                       // 0x007d3f40
    // One-shot timer in mission time: StartTimer() sets it to now; TimerElapsed() is
    // true once it has passed (and then disarms it).
    bool TimerElapsed();                                // 0x007d3f70
    void StartTimer();                                  // 0x007d3fa0

protected:
    C_ItemDef *m_def = nullptr;                         // +0x04
    E_ItemKind m_kind;                                  // +0x08
    int m_typeId = -1;                                  // +0x0C index into the item definitions
    int m_uid = 0;                                      // +0x10
    uint32_t m_time = 0;                                // +0x14 mission time (timer / busy until)
    I3D_frame *m_frame = nullptr;                       // +0x18
    bool m_inWorld = false;                             // +0x1C
    bool m_armed = false;                               // +0x1D
    C_Actor *m_owner = nullptr;                         // +0x20
    int m_container = 0;                                // +0x24
};

class C_AmmoItem : public C_Item {                      // vtable 0x00816ed8, 0x30 bytes
public:
    C_AmmoItem() : C_Item(ITEM_KIND_AMMO) {}            // 0x007d3680

    void Init(int typeId) override;                     // 0x007d37d0
    float GetAmount() override { return m_rounds; }     // 0x007d3780
    float GetMaxAmount() override { return m_maxRounds; }   // 0x007d3790
    void SetAmount(float rounds) { m_rounds = rounds; }
    int Fire(C_Actor *, const S_vector &, const S_vector &, uint32_t) override { return FIRE_NOT_A_WEAPON; }   // 0x007d3760
    int FireSecondary(C_Actor *, const S_vector &, const S_vector &, uint8_t) override { return FIRE_NOT_A_WEAPON; }   // 0x007d3770
    bool SaveGame(C_chunk &chunk) override;             // 0x007d3820
    bool LoadGame(C_chunk &chunk) override;             // 0x007d3a20

private:
    float m_maxRounds = -1.0f;                          // +0x28
    float m_rounds = 0.0f;                              // +0x2C
};

// An item holding other items (backpacks).
class C_ContainerItem : public C_Item {                 // vtable 0x00816f28, 0x2C bytes
public:
    C_ContainerItem();                                  // 0x007d3c60
    ~C_ContainerItem() override;                        // 0x007d3d30

    void Init(int typeId) override;                     // 0x007d3da0
    int Fire(C_Actor *, const S_vector &, const S_vector &, uint32_t) override { return FIRE_NOT_A_WEAPON; }   // 0x007d3cf0 (DS 0x0080ce30)
    int FireSecondary(C_Actor *, const S_vector &, const S_vector &, uint8_t) override { return FIRE_NOT_A_WEAPON; }   // 0x007d3d00 (DS 0x0080ce50)

    C_ItemStore *GetStore() const { return m_store; }

private:
    C_ItemStore *m_store;                               // +0x28
};

class C_MedkitItem : public C_Item {                    // vtable 0x00816fc8, 0x2C bytes
public:
    C_MedkitItem() : C_Item(ITEM_KIND_MEDKIT) {}        // 0x007d45b0 (DS 0x0080d9b0)

    void Init(int typeId) override;                     // 0x007d4640
    void Consume(float amount) override;                // 0x007d45d0
    float GetCharges() override { return m_charges; }   // 0x007d4600
    int Fire(C_Actor *, const S_vector &, const S_vector &, uint32_t) override { return FIRE_NOT_A_WEAPON; }   // 0x007d4680
    int FireSecondary(C_Actor *, const S_vector &, const S_vector &, uint8_t) override { return FIRE_NOT_A_WEAPON; }   // 0x007d4690
    bool SaveGame(C_chunk &chunk) override;             // 0x007d46a0
    bool LoadGame(C_chunk &chunk) override;             // 0x007d47b0

private:
    float m_charges = 0.0f;                             // +0x28 healing left
};

// C_WeaponItem flags.
enum : uint32_t {
    WEAPON_UNKNOWN_FLAG1 = 0x1,     // set by Init, never tested here
    WEAPON_RELOAD_PENDING = 0x2,    // set elsewhere; a reload request with nothing to load
                                    // (or of a weapon that cannot be topped up) then still succeeds
};

class C_WeaponItem : public C_Item {                    // vtable 0x00817018, 0xB0 bytes
public:
    C_WeaponItem();                                     // 0x007d4a20
    ~C_WeaponItem() override;                           // 0x007d4b70

    void Init(int typeId) override;                     // 0x007d4be0
    void Use(float amount) override;                    // 0x007d4ab0
    // Rounds in the weapon; 0 until a reload / the last shot is finished.
    float GetAmount() override;                         // 0x007d4ae0
    float GetMaxAmount() override { return m_capacity; }   // 0x007d4b00 (DS 0x0080e0f0)
    void SetRounds(C_Actor *owner, int ammoUid, int rounds) override;   // 0x007d51d0
    // Takes the fullest magazine of the right type from container `container` of
    // `owner` (ITEM_BEHAVIOUR_LAUNCHER weapons use the backpack contents instead of the
    // belt). Rounds that do not fit go back into the emptiest magazine if they fit
    // there, otherwise the magazine keeps them. `keepLastMagazine`: the last magazine
    // refills the weapon without being used up (as does the infinite ammo cheat).
    // The weapon is busy for the component's reload time; `*ammoUid` receives the uid
    // of the magazine used up (-1 none).
    bool Reload(C_Actor *owner, int container, int *ammoUid, bool keepLastMagazine) override;   // 0x007d4cb0
    int Fire(C_Actor *shooter, const S_vector &pos, const S_vector &dir, uint32_t flags) override;   // 0x007d5430 (DS 0x0080e8e0)
    int FireSecondary(C_Actor *shooter, const S_vector &pos, const S_vector &dir, uint8_t flags) override;   // 0x007d5670
    I3D_frame *GetProjectileFrame() override;           // 0x007d4b10
    void AttachProjectile(I3D_frame *hand) override;    // 0x007d57e0
    int GetProjectileKind() override;                   // 0x007d4b30
    bool SaveGame(C_chunk &chunk) override;             // 0x007d5950
    bool LoadGame(C_chunk &chunk) override;             // 0x007d5b90

    // "Click" of an empty weapon.
    void PlayEmptySound(C_Actor *owner);                // 0x007d52f0
    // Plays a shot at `where`: a positional sound frame on clients, and on the server
    // (unless `quiet`) at most once a second a noise for the AI (the squad's melee too).
    void PlayShotSound(C_Actor *shooter, I3D_frame *where, bool quiet);   // 0x007d5ee0
    // Stops the reload sound if the reload is interrupted more than 200 ms before its end.
    void StopReloadSound();                             // 0x007d6a70
    // Rounds in the weapon (C_Human::DropAllItems fills dropped weapons).
    void SetLoadedRounds(float rounds) { m_rounds = rounds; }
    uint32_t GetFlags() const { return m_flags; }
    void SetFlags(uint32_t flags) { m_flags = flags; }
    uint32_t GetReadyTime() const { return m_readyTime; }
    // Mission time between the last two shots.
    int GetShotInterval() const { return m_shotInterval; }
    I3D_frame *GetFlashFrame() const { return m_flash; }
    // Speed factor of a thrown weapon (grenades fly at max(5, this * muzzle velocity) m/s).
    float GetThrowStrength() const { return m_throwStrength; }
    void SetThrowStrength(float strength) { m_throwStrength = strength; }
    // Dummy frames of the model, looked up on first use: "cardum" (where shells are
    // ejected) and "blsdum" (muzzle effects).
    I3D_frame *GetCartridgeDummy();
    I3D_frame *GetBlastDummy();
    // "shdum" (where shots sound and the controls fire from), looked up while not found.
    I3D_frame *GetShotDummy();                          // 0x0058ba80 (__fastcall)
    // Rounds in the weapon as stored (GetAmount() hides them until a reload is finished).
    float GetLoadedRounds() const { return m_rounds; }
    bool IsReloadPending() const { return (m_flags & WEAPON_RELOAD_PENDING) != 0; }
    void ClearReloadPending() { m_flags &= ~WEAPON_RELOAD_PENDING; }
    // Mission time when the attack delay of the controls' last shot ends.
    uint32_t GetAttackTime() const { return m_attackTime; }
    void SetAttackTime(uint32_t time) { m_attackTime = time; }

private:
    // Looks up the projectile frame ("projectile"/"mag1" kind 2, "mag2" 3, "mag3" 4) and
    // makes the copy AttachProjectile() moves around.
    void FindProjectile();                              // 0x007d56b0
    // Sound frame `index` of the shot sound group, created on first use (at most two).
    I3D_frame *GetShotSoundFrame(int index);            // 0x007d60d0
    void ReleaseShotSoundFrames();                      // 0x007d6760
    // Last-shot and bolt sounds of a firearm shot.
    void PlayActionSounds(C_Actor *shooter);            // 0x007d6790
    // Reload sound at the shot dummy (`ambient`: heard without positioning).
    void PlayReloadSound(C_Actor *owner, bool ambient);

    // +0x28 unused
    S_vector4 m_projectilePos;                          // +0x30 local placement of the projectile
    S_quat m_projectileRot;                             // +0x40
    float m_rounds = -1.0f;                             // +0x50 in the weapon
    float m_capacity = -1.0f;                           // +0x54
    uint32_t m_attackTime = 0;                          // +0x58 see GetAttackTime() (C_HumanController::ApplyWeaponControls)
    uint32_t m_readyTime = 0;                           // +0x5C mission time when it can fire / reload again
    uint32_t m_flags = WEAPON_UNKNOWN_FLAG1;            // +0x60 WEAPON_*
    uint32_t m_lastShotTime = 0;                        // +0x64
    int m_shotInterval = 0;                             // +0x68
    int m_shotCount = 0;                                // +0x6C
    I3D_frame *m_flash = nullptr;                       // +0x70 "flash" visual (hidden)
    I3D_frame *m_projectile = nullptr;                  // +0x74
    I3D_frame *m_projectileCopy = nullptr;              // +0x78
    int m_projectileKind = 1;                           // +0x7C 1 = not looked up yet
    I3D_frame *m_shotSounds[2] = {};                    // +0x80 I3D_sound frames
    int m_shotSoundCount = 1;                           // +0x88 1 until the sounds are loaded
    uint32_t m_lastNoiseTime = 0;                       // +0x8C last AI noise (mission time)
    bool m_unknown90 = false;                           // +0x90
    int m_unknown94 = 0;                                // +0x94
    float m_throwStrength = 1.0f;                       // +0x98
    I3D_frame *m_cartridgeDummy = nullptr;              // +0x9C "cardum" (used by the shot manager)
    I3D_frame *m_blastDummy = nullptr;                  // +0xA0 "blsdum": muzzle effect
    I3D_frame *m_shotDummy = nullptr;                   // +0xA4 "shdum": where shots sound
    int m_soundId = -1;                                 // +0xA8 reload sound slot
};
