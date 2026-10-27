# Item definitions loaded from Tables\items.sav (source file 0x007dc8c0-0x007e0bdf)
#
# items.sav is a fixed 252000-byte image of 500 slots. Each slot starts with an int:
#   0 = empty (slot is 4 bytes), 1 = present (slot is 508 bytes: the int, then 504 bytes below).
# A present slot holds E_ItemRecordType, then the C_ItemDef::Load stream (500 bytes):
#   +0x00 char[20] FPV model   +0x14 char[20] image   +0x28 char[20] model
#   +0x3c int m_unknown0c      +0x40 16 unused bytes  +0x50 char[20] display name (not loaded)
#   +0x64 textId, weight, inventorySize, category, unknown3c, armedSound, team
#   +0x80 int component kind+1 (0 = none), component data (C_ItemComponent::Save size)
#   then int secondary component kind+1, its data, 0x20 reserved bytes, then the derived
#   class fields (C_AmmoItemDef 4 bytes, C_AnimatedItemDef 16, C_StaticItemDef 20).
# Offsets in "Items Table/knownfields.json" are relative to the record type int, i.e.
# json offset = stream offset + 4 ("Field Structure Type" = component kind+1 at json 132,
# "Aim Mode Specifier" = secondary component kind+1).

enum E_ItemRecordType
  ITEM_AMMO = 0
  ITEM_ANIMATED = 1                   # weapons, mines, grenades: held in hands
  ITEM_STATIC = 2                     # equipment: headgear, uniforms, backpacks, keys...
end

# C_ItemComponent::m_kind. items.sav stores kind+1 (0 = no component); code that finds no
# component uses 10.
enum E_ItemComponentKind
  ICK_MELEE = 0
  ICK_MINE = 1
  ICK_PROJECTILE_WEAPON = 2           # grenades, launchers, cannons, flak
  ICK_FIREARM = 3
  ICK_SCOPE = 4                       # aiming/zoom; also used as the secondary component
  ICK_UNIFORM = 5
  ICK_MEDKIT = 6
  ICK_BACKPACK = 7                    # also radios and the aqualung
  ICK_HEADGEAR = 8
  ICK_SPECIAL = 9                     # mission items: wire cutter, documents, explosives bag
  ICK_NONE = 10
end

# ---------------------------------------------------------------- components
# Component objects are Save/Load'ed as raw copies of everything after the vtable pointer.
# Save(size, buf): *size == 0 -> only report the stream size.

class C_ItemComponent size=0xc vtable=0x0
  0x04 int m_unknown04                # always 0 in items.sav
  0x08 E_ItemComponentKind m_kind
end

class C_WeaponComp : C_ItemComponent size=0x24 vtable=0x0
  0x0c float m_unknown0c              # 0.6 pistols, 0.8 SMGs, 1 rifles, 0.15 knives
  0x10 float m_damage
  0x14 int m_shotSound                # sound table index
  0x18 float m_attackDelay            # ms
  0x1c int m_effect                   # muzzle flash (guns) / explosion (mines) effect id
  0x20 bool m_canReload
end

class C_MeleeComp : C_WeaponComp size=0x28 vtable=0x8173b0:13
  0x24 float m_idealRange
end

class C_MineComp : C_WeaponComp size=0x2c vtable=0x81737c:13
  0x24 int m_unknown24                # -1 for every mine
  0x28 float m_explosionRadius
end

class C_ProjectileWeaponComp : C_WeaponComp size=0x64 vtable=0x817348:13
  0x24 char m_shortName[8]
  0x2c int m_projectileType           # 0/1/2 (C_ItemComponent default 3)
  0x30 int m_reloadSound
  0x34 float m_reloadTime             # ms
  0x38 float m_muzzleVelocity
  0x3c float m_idealRange
  0x40 float m_damageFalloff          # as the firearm's; not read for projectiles
  0x44 float m_aimedDispersion
  0x48 float m_unknown48
  0x4c int m_fuseTime                 # ms
  0x50 int m_unknown50
  0x54 int m_explosionEffect
  0x58 int m_explosionSound           # sound group (chapter 6)
  0x5c float m_explosionDamage
  0x60 float m_explosionRadius
end

class C_FirearmComp : C_WeaponComp size=0x5c vtable=0x817314:13
  0x24 int m_unknown24
  0x28 int m_projectileType           # 0 MG42/MG15/MG81, 1 Browning/Type 97, 2 others
  0x2c int m_reloadSound
  0x30 float m_reloadTime             # ms
  0x34 float m_muzzleVelocity
  0x38 float m_idealRange
  0x3c float m_damageFalloff          # bullet power lost per metre beyond m_idealRange
  0x40 float m_aimedDispersion
  0x44 float m_unknown44
  0x48 int m_lastShotSound
  0x4c int m_boltSound
  0x50 char m_shortName[8]
  0x58 float m_unknown58              # 1, ~6 for scoped rifles
end

class C_ScopeComp : C_ItemComponent size=0x14 vtable=0x817304:4
  0x0c int m_aimMode                  # 5 iron sights, 0-3 scope reticles, 4 binoculars
  0x10 float m_aimedFovFactor
end

class C_UniformComp : C_ItemComponent size=0x10 vtable=0x8172f4:4
  0x0c int m_disguiseSide             # 0 allied, 1 axis, 2 neutral
end

class C_MedkitComp : C_ItemComponent size=0x10 vtable=0x8172e4:4
  0x0c float m_healingCapacity
end

class C_BackpackComp : C_ItemComponent size=0x20 vtable=0x8172d4:4
  0x0c int m_subtype                  # 0 backpack, 1 radio, 2 aqualung
  0x10 float m_maxWeight              # limits of the backpack's item store
  0x14 float m_capacity
  0x18 int m_maxItemSize
  0x1c float m_unknown1c              # 900 for the aqualung
end

class C_HeadgearComp : C_ItemComponent size=0x14 vtable=0x8172c4:4
  0x0c int m_headgearType             # 0 helmet, 1 field cap, 2 cap/beret
  0x10 float m_protection             # 350 steel helmets, 10 caps
end

class C_SpecialItemComp : C_ItemComponent size=0xc vtable=0x8172b4:4
end

# ---------------------------------------------------------------- item definitions

class C_ItemDef size=0x54 vtable=0x817290:9
  0x04 E_ItemRecordType m_recordType
  0x08 int m_id                       # index in C_ItemsModule::m_items
  0x0c int m_unknown0c                # 0/1/2
  0x10 char* m_displayName            # Load leaves it NULL
  0x14 int m_textId
  0x18 float m_weight
  0x1c int m_inventorySize            # inventory space units (0 counts as 1)
  0x20 I3D_frame* m_fpvFrame
  0x24 char* m_fpvModelName
  0x28 void* m_image
  0x2c char* m_imageName
  0x30 I3D_frame* m_modelFrame
  0x34 char* m_modelName
  0x38 int m_category                 # 5 pistol, 6 rifle, 7 SMG/MG, 8 launcher, 10 headgear...
  0x3c int m_shellSound               # sound group (chapter 5) of the weapon's shells
  0x40 int m_armedSound
  0x44 int m_team                     # 0 allied, 1 axis, 2 all, 3 none
  0x48 C_ItemComponent* m_component
  0x4c C_ItemComponent* m_secondaryComponent # C_ScopeComp on aimable weapons
  0x50 I3D_frame** m_divingGear        # item 231 only: mask, finL, finR, tank frames (4)
  vslot 0x03 GetCarryMode | int __thiscall GetCarryMode(C_ItemDef *this)
  vslot 0x04 GetBehaviour | int __thiscall GetBehaviour(C_ItemDef *this)
end

class C_AmmoItemDef : C_ItemDef size=0x58 vtable=0x8173e4:9
  0x54 float m_ammoAmount
end

class C_AnimatedItemDef : C_ItemDef size=0x68 vtable=0x817408:9
  0x54 int m_ammoItemId               # uniforms carried as animated items store headgear here
  0x58 int m_carryMode
  0x5c int m_behaviour
  0x60 int m_attachment               # 0 none, 1 scope, 2 silencer
  0x64 I3D_frame* m_fpvMagazine       # "magazine" frame of the first-person model
end

class C_StaticItemDef : C_ItemDef size=0x68 vtable=0x81742c:9
  0x54 int m_carryMode
  0x58 int m_behaviour
  0x5c int m_unknown5c
  0x60 int m_headgearItemId           # uniforms: default headgear item
  0x64 int m_unknown64                # uniforms only: -1..3
end

# ---------------------------------------------------------------- module

class C_ItemsModule : C_Module size=0x848 vtable=0x817208:16
  0x00c bool m_infiniteAmmo           # console cheat (single player)
  0x00d bool m_itemsLoaded            # set by EnsureItemsLoaded
  0x010 C_ItemDef* m_items[500]
  0x7e0 int m_lastUid                 # uids are handed out counting up from 0
  0x7e4 bool m_missionActive          # StartMission..StopMission: moves update models / HUD
  0x7e5 bool m_7e5
  # 0x7e8-0x807 are also labelled as globals g_modelPool / g_modelPoolCount / g_respawningItems
  # (names/script.txt), which is why g_itemsModule itself is left untyped in Ghidra.
  0x7e8 void* m_placements            # S_ItemPlacement[] (0x30 bytes each), _aligned_malloc'd
  0x7ec int m_placementCapacity
  0x7f0 int m_placementCount
  0x7f4 int m_placementGrowBy         # 2
  0x7f8 std::vector<void*> m_respawningItems  # C_Item*
  # 0x808-0x837: std::deque of {time (us), uid} pairs: uids released for reuse after 10 ms
  0x808 uint8_t m_808
  0x80c int m_80c
  0x810 int m_810
  0x814 int m_814
  0x818 void* m_818
  0x81c uint32_t m_81c
  0x820 uint32_t m_820
  0x824 uint32_t m_824
  0x828 uint32_t m_828
  0x82c void* m_82c
  0x830 uint32_t m_830
  0x834 int m_freedUidCount
  0x838 uint8_t m_838
  0x83c void* m_instances             # std::vector<C_Item*> of every item instance
  0x840 void* m_840
  0x844 uint32_t m_844
end
