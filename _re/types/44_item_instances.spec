# Item instances (0x007d3650..0x007d6a70) and item containers (0x007d6ac0..0x007d7a90).
# An item instance is created by the items module for one item of a type (index into
# g_itemDefs); containers hold them: humans carry seven (head, belt, long gun, hands,
# uniform, backpack, backpack contents), vehicles one, backpacks one inside.

# ---- containers -------------------------------------------------------------------------
class C_ItemContainer size=0x14 vtable=0x817068:25
  0x04 int m_kind
  0x08 float m_maxWeight          # 100
  0x0C float m_maxSpace           # 100 (an item takes twice its size, at least 1)
  0x10 int m_maxItemSize          # 3
  vslot 0x00 ScalarDeletingDestructor | C_ItemContainer * __thiscall ScalarDeletingDestructor(C_ItemContainer *this, uchar flags)
  vslot 0x01 SetMaxWeight | void __thiscall SetMaxWeight(C_ItemContainer *this, float value)
  vslot 0x02 GetMaxWeight | float __thiscall GetMaxWeight(C_ItemContainer *this)
  vslot 0x03 SetMaxSpace | void __thiscall SetMaxSpace(C_ItemContainer *this, float value)
  vslot 0x04 GetMaxSpace | float __thiscall GetMaxSpace(C_ItemContainer *this)
  vslot 0x05 SetMaxItemSize | void __thiscall SetMaxItemSize(C_ItemContainer *this, int value)
  vslot 0x06 GetMaxItemSize | int __thiscall GetMaxItemSize(C_ItemContainer *this)
  vslot 0x07 GetTotalWeight | float __thiscall GetTotalWeight(C_ItemContainer *this)
  vslot 0x08 GetTotalSpace | float __thiscall GetTotalSpace(C_ItemContainer *this)
  vslot 0x09 CountItems | int __thiscall CountItems(C_ItemContainer *this, int typeId)
  vslot 0x0a GetUsedSlotCount | int __thiscall GetUsedSlotCount(C_ItemContainer *this)
  vslot 0x0b AddItem | int __thiscall AddItem(C_ItemContainer *this, C_Item *item, C_Item **replaced, int index)
  vslot 0x0c RemoveItem | int __thiscall RemoveItem(C_ItemContainer *this, C_Item *item, int index)
  vslot 0x0d Clear | void __thiscall Clear(C_ItemContainer *this)
  vslot 0x0e IsEmpty | bool __thiscall IsEmpty(C_ItemContainer *this)
  vslot 0x0f GetSlots | void * __thiscall GetSlots(C_ItemContainer *this)
  vslot 0x10 GetItem | C_Item * __thiscall GetItem(C_ItemContainer *this)
  vslot 0x11 GetSlotCount | int __thiscall GetSlotCount(C_ItemContainer *this)
  vslot 0x12 IsLocked | bool __thiscall IsLocked(C_ItemContainer *this)
  vslot 0x13 SetLockTime | void __thiscall SetLockTime(C_ItemContainer *this, uint time)
  vslot 0x14 GetLockTime | uint __thiscall GetLockTime(C_ItemContainer *this)
  vslot 0x15 Unknown54 | void __thiscall Unknown54(C_ItemContainer *this, int arg)
  vslot 0x16 Unknown58 | int __thiscall Unknown58(C_ItemContainer *this)
  vslot 0x17 Unknown5C | bool __thiscall Unknown5C(C_ItemContainer *this, int a1, int a2)
  vslot 0x18 Unknown60 | bool __thiscall Unknown60(C_ItemContainer *this, int a1, int a2)
end

# One item (head, long gun, hands, uniform, backpack).
class C_ItemSlot : C_ItemContainer size=0x1c vtable=0x8170d0:25
  0x14 C_Item* m_item
  0x18 uint32_t m_lockTime            # game time until which the slot is locked
end

# Any number of items, unsorted (kind 3).
class C_ItemList : C_ItemContainer size=0x18 vtable=0x817138:25
  0x14 void* m_items              # heap std::vector<C_Item*>
end

# Items grouped in slots of equal items (14 slots for kind 1, 32 otherwise).
class C_ItemStore : C_ItemContainer size=0x20 vtable=0x8171a0:25
  0x14 int m_slotCount
  0x18 void** m_slots             # m_slotCount heap std::vector<C_Item*>
  0x1C int m_usedSlots
end

# ---- instances --------------------------------------------------------------------------
class C_Item size=0x28 vtable=0x816f78:19
  0x04 C_ItemDef* m_def
  0x08 int m_kind                 # 0 weapon, 1 container, 2 ammo, 3 medkit
  0x0C int m_typeId               # index into g_itemDefs
  0x10 int m_uid
  0x14 uint32_t m_time           # mission time: one-shot timer / busy until
  0x18 I3D_frame* m_frame         # model, loaded on demand (GetFrame)
  0x1C bool m_inWorld             # model shown in the world (dropped / pick-up)
  0x1D bool m_armed               # set for mines by Init; placed armed mines are laid
  0x20 C_Actor* m_owner
  0x24 int m_container            # container of the owner (ITEM_CONTAINER_*)
  vslot 0x00 ScalarDeletingDestructor | C_Item * __thiscall ScalarDeletingDestructor(C_Item *this, uchar flags)
  vslot 0x01 Init | void __thiscall Init(C_Item *this, int typeId)
  vslot 0x02 Use | void __thiscall Use(C_Item *this, float amount)
  vslot 0x03 GetAmount | float __thiscall GetAmount(C_Item *this)
  vslot 0x04 GetMaxAmount | float __thiscall GetMaxAmount(C_Item *this)
  vslot 0x05 SetRounds | void __thiscall SetRounds(C_Item *this, C_Actor *owner, int ammoUid, int rounds)
  vslot 0x06 Reload | bool __thiscall Reload(C_Item *this, C_Actor *owner, int container, int *ammoUid, bool keepLastMagazine)
  vslot 0x07 Consume | void __thiscall Consume(C_Item *this, float amount)
  vslot 0x08 GetCharges | float __thiscall GetCharges(C_Item *this)
  vslot 0x09 Fire | int __thiscall Fire(C_Item *this, C_Actor *shooter, S_vector *pos, S_vector *dir, uint flags)
  vslot 0x0a FireSecondary | int __thiscall FireSecondary(C_Item *this, C_Actor *shooter, S_vector *pos, S_vector *dir, uchar flags)
  vslot 0x0b GetWeight | float __thiscall GetWeight(C_Item *this)
  vslot 0x0c GetSize | int __thiscall GetSize(C_Item *this)
  vslot 0x0d GetSpace | float __thiscall GetSpace(C_Item *this)
  vslot 0x0e GetProjectileFrame | I3D_frame * __thiscall GetProjectileFrame(C_Item *this)
  vslot 0x0f AttachProjectile | void __thiscall AttachProjectile(C_Item *this, I3D_frame *hand)
  vslot 0x10 GetProjectileKind | int __thiscall GetProjectileKind(C_Item *this)
  vslot 0x11 SaveGame | bool __thiscall SaveGame(C_Item *this, C_chunk *chunk)
  vslot 0x12 LoadGame | bool __thiscall LoadGame(C_Item *this, C_chunk *chunk)
end

class C_AmmoItem : C_Item size=0x30 vtable=0x816ed8:19
  0x28 float m_maxRounds
  0x2C float m_rounds
end

# Item holding other items (backpacks).
class C_ContainerItem : C_Item size=0x2c vtable=0x816f28:19
  0x28 C_ItemStore* m_store
end

class C_MedkitItem : C_Item size=0x2c vtable=0x816fc8:19
  0x28 float m_charges
end

class C_WeaponItem : C_Item size=0xb0 vtable=0x817018:19
  0x30 S_vector4 m_projectilePos  # local placement of the projectile frame
  0x40 S_quat m_projectileRot
  0x50 float m_rounds             # in the magazine
  0x54 float m_capacity
  0x58 int m_unknown58
  0x5C uint32_t m_readyTime           # game time when it can fire again
  0x60 uint32_t m_flags
  0x64 uint32_t m_lastShotTime
  0x68 int m_shotInterval
  0x6C int m_shotCount
  0x70 I3D_frame* m_flash         # "flash"
  0x74 I3D_frame* m_projectile    # "projectile" (or other dummies), m_projectileKind 2..4
  0x78 I3D_frame* m_projectileCopy
  0x7C int m_projectileKind       # 1 = not looked up yet
  0x80 I3D_frame* m_shotSounds[2] # I3D_sound frames of the shot sound group
  0x88 int m_shotSoundCount       # 1 until the sounds are loaded
  0x8C uint32_t m_lastNoiseTime   # last AI noise of a shot (mission time)
  0x90 bool m_unknown90
  0x94 int m_unknown94
  0x98 float m_unknown98
  0x9C I3D_frame* m_cartridgeDummy # "cardum" (shot manager)
  0xA0 I3D_frame* m_blastDummy    # "blsdum": muzzle effect
  0xA4 I3D_frame* m_shotDummy     # "shdum": shot sound position
  0xA8 int m_soundId              # reload sound slot
end
