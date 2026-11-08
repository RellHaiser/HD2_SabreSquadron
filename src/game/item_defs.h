// Item definitions: one per item type, loaded from Tables\items.sav by the items module
// (original source 0x007df330..0x007e0bdf; the record layout is documented at C_ItemDef::Load
// and in _re/types/30_items.spec).
//
// A definition has up to two components that describe what the item does (a weapon, a
// medkit, a backpack ...); the second one is the scope of aimable weapons.
#pragma once

#include <cstdint>

class I3D_frame;
class IImage;

enum E_ItemRecordType {
    ITEM_AMMO = 0,
    ITEM_ANIMATED = 1,              // weapons, mines, grenades: held in hands
    ITEM_STATIC = 2,                // equipment: headgear, uniforms, backpacks, keys...
};

// Kind of an item's component (items.sav stores kind+1, 0 = none).
enum E_ItemComponentKind {
    ICK_MELEE = 0,
    ICK_MINE = 1,
    ICK_PROJECTILE_WEAPON = 2,      // grenades, launchers, cannons, flak
    ICK_FIREARM = 3,
    ICK_SCOPE = 4,                  // aiming/zoom; also used as the secondary component
    ICK_UNIFORM = 5,
    ICK_MEDKIT = 6,
    ICK_BACKPACK = 7,               // also radios and the aqualung
    ICK_HEADGEAR = 8,
    ICK_SPECIAL = 9,                // mission items: wire cutter, documents, explosives bag
    ICK_NONE = 10,                  // what code that finds no component assumes
};

// C_ItemDef::GetBehaviour() values identified so far (examples from Sabre Squadron's items.sav).
enum : int {
    ITEM_BEHAVIOUR_MELEE = 0,       // the knife (the player's stab and its second attack); empty hands
    ITEM_BEHAVIOUR_SIDEARM = 2,     // pistols: drawn from the belt
    ITEM_BEHAVIOUR_SMG = 3,         // submachine guns (MP 40, Thompson, Sten, PPSh)
    ITEM_BEHAVIOUR_RIFLE = 4,       // rifles (Kar 98k, Lee-Enfield, Garand)
    ITEM_BEHAVIOUR_LMG = 5,         // light machine guns firing only from prone (Bren, DP-28, ZB 26)
    ITEM_BEHAVIOUR_LAUNCHER = 6,    // single-shot weapons: one round without an ammo item, the
                                    // round shows as the "mag1" frame, reloads from the backpack
    ITEM_BEHAVIOUR_FIXED = 8,       // fixed weapons (MG 42, Browning 1919, the vehicles' guns and flak)
    ITEM_BEHAVIOUR_THROWN = 9,      // grenades: firing throws the item itself
    ITEM_BEHAVIOUR_MINE = 10,
    ITEM_BEHAVIOUR_STICKY_CHARGE = 11,  // charges stuck on vehicles (only the Clam Mk.III); carried
                                    // like mines and ammo (backpack contents)
    ITEM_BEHAVIOUR_ASSAULT = 12,    // heavy weapons counted as assault weapons (BAR, MP 44)
    ITEM_BEHAVIOUR_MEDKIT = 13,
    ITEM_BEHAVIOUR_BINOCULARS = 14, // on the belt: the player's view mode 1
    ITEM_BEHAVIOUR_UNIFORM = 15,
    ITEM_BEHAVIOUR_HEADGEAR = 16,
    ITEM_BEHAVIOUR_BACKPACK = 17,
    ITEM_BEHAVIOUR_COMPASS_CAMERA = 18, // the compass and the camera
    ITEM_BEHAVIOUR_WIRE_CUTTER = 19,    // on the belt: the wire-cut command
    ITEM_BEHAVIOUR_RESPAWNING = 20, // multiplayer pick-ups (also the documents to take)
    ITEM_BEHAVIOUR_MISSION_EXPLOSIVE = 21,  // explosives for objectives (explosives bag, dynamite);
                                    // dropped on death in multiplayer like the pick-ups
    ITEM_BEHAVIOUR_KEY = 22,        // keys for doors (the base game's items.sav has them as 18)
    ITEM_BEHAVIOUR_AMMO = 23,       // every C_AmmoItemDef
};

// C_ItemDef::GetCarryMode() values: how a human holds and carries the item.
enum : int {
    ITEM_CARRY_FIXED = 0,           // fixed weapons (the guns and flak on vehicles and mounts)
    ITEM_CARRY_SMG = 1,
    ITEM_CARRY_SIDEARM = 2,
    ITEM_CARRY_RIFLE = 3,
    ITEM_CARRY_LMG = 4,             // light and mounted machine guns
    ITEM_CARRY_LAUNCHER = 5,        // bazooka, Panzerfaust, Panzerschreck
    ITEM_CARRY_EQUIPMENT = 6,       // grenades, explosives, helmets, uniforms, other equipment
    ITEM_CARRY_AMMO = 100,          // every C_AmmoItemDef
};

// C_ItemDef::GetAllowedContainers() bits.
enum : uint32_t {
    ITEM_FITS_HEAD = 0x01,
    ITEM_FITS_UNIFORM = 0x02,
    ITEM_FITS_HANDS = 0x04,
    ITEM_FITS_BACKPACK = 0x08,
    ITEM_FITS_LONG_GUN = 0x10,
    ITEM_FITS_BELT = 0x20,
    ITEM_FITS_BACKPACK_CONTENTS = 0x40,
};

// ---------------------------------------------------------------- components
// Components are saved and loaded as an image of their fields after the vtable pointer
// (the "stream" of Save / Load).
class C_ItemComponent {
public:
    explicit C_ItemComponent(E_ItemComponentKind kind) : m_kind(kind) {}
    C_ItemComponent(const C_ItemComponent &) = delete;
    C_ItemComponent &operator=(const C_ItemComponent &) = delete;

    virtual int GetUnknownVf00() { return 0x7FFFFFFF; }      // 0x00 0x007e06b0
    // *ioSize == 0: only reports the stream size; otherwise writes the stream.
    virtual void Save(int *ioSize, void *buffer) = 0;       // 0x04
    virtual void Load(int size, const void *data) = 0;      // 0x08
    virtual int GetProjectileType() { return 3; }           // 0x0C 0x007e06c0
    // (Not in the original vtable: components were freed with a plain operator delete.)
    virtual ~C_ItemComponent() = default;

    E_ItemComponentKind GetKind() const { return m_kind; }

protected:
    // The stream is the object from +0x04 on.
    static constexpr int STREAM_BASE = 4;
    // Field image helpers (offsets are object offsets).
    static void Get(void *field, int size, const void *data, int offset);
    static void Put(void *buffer, int offset, const void *field, int size);
    template <class T> static void Get(T &field, const void *data, int offset) { Get(&field, sizeof(T), data, offset); }
    template <class T> static void Put(void *buffer, int offset, const T &field) { Put(buffer, offset, &field, sizeof(T)); }
    // Image of the base fields (+0x04, +0x08).
    void GetBase(const void *data);
    void PutBase(void *buffer) const;

    int m_reserved04 = 0;                               // +0x04 always 0 in items.sav; set and copied, never read
    E_ItemComponentKind m_kind;                         // +0x08
};

// Common part of melee weapons, mines, projectile weapons and firearms.
class C_WeaponComp : public C_ItemComponent {
public:
    explicit C_WeaponComp(E_ItemComponentKind kind) : C_ItemComponent(kind) {}

    virtual float GetAimedDispersion() { return 0.0f; }     // 0x10 0x007e06d0
    virtual float GetMuzzleVelocity() { return 0.0f; }      // 0x14 0x007e06e0
    // Sound groups: shots in SOUND_CHAPTER_WEAPON_SHOOTING, the others in
    // SOUND_CHAPTER_WEAPON_MANIPULATION; -1 = none.
    virtual int GetShotSound() { return m_shotSound; }      // 0x18 0x007e06f0
    virtual int GetReloadSound() { return -1; }             // 0x1C 0x007e0700
    virtual int GetReloadTimeMs() { return 0; }             // 0x20 0x007e0710
    // Played with the last round.
    virtual int GetLastShotSound() { return -1; }           // 0x24 0x007e0720
    // Played with every shot.
    virtual int GetBoltSound() { return -1; }               // 0x28 0x007e0730
    virtual float GetUnknownVf2C() { return 0.0f; }         // 0x2C 0x007e0740
    // False: the weapon cannot be topped up while rounds are left.
    virtual bool CanReload() { return m_canReload; }        // 0x30 0x007e0750

    // Particle effect: muzzle flash of guns (at the weapon's "blsdum"), launch blast of
    // projectile weapons, explosion of mines.
    int GetEffect() const { return m_effect; }
    // Firearms: a bullet's power when it leaves the muzzle (materials absorb it, humans
    // take what is left). Projectiles and grenades: what they spend to pass thin
    // materials; grenades also damage a destructible part they touch by it.
    float GetDamage() const { return m_damage; }
    // ms between two shots of the controls (C_HumanController::ApplyWeaponControls).
    float GetAttackDelay() const { return m_attackDelay; }

protected:
    void GetWeapon(const void *data);
    void PutWeapon(void *buffer) const;

    float m_unknown0c = 0.0f;                           // +0x0C 0.6 pistols, 0.8 SMGs, 1 rifles
    float m_damage = 0.0f;                              // +0x10 see GetDamage()
    int m_shotSound = 0;                                // +0x14
    float m_attackDelay = 0.0f;                         // +0x18 ms
    int m_effect = 0;                                   // +0x1C
    bool m_canReload = true;                            // +0x20
};

class C_MeleeComp : public C_WeaponComp {               // vtable 0x008173b0
public:
    C_MeleeComp() : C_WeaponComp(ICK_MELEE) {}
    void Save(int *ioSize, void *buffer) override;      // 0x007df330
    void Load(int size, const void *data) override;     // 0x007df370

private:
    float m_idealRange = 0.0f;                          // +0x24 (MeleeAttack uses a fixed 1.2 m reach)
};

class C_MineComp : public C_WeaponComp {                // vtable 0x0081737c
public:
    C_MineComp() : C_WeaponComp(ICK_MINE) {}
    int GetUnknownVf00() override { return m_unknown24; }   // 0x007e0760
    void Save(int *ioSize, void *buffer) override;      // 0x007df3a0
    void Load(int size, const void *data) override;     // 0x007df3e0

    float GetExplosionRadius() const { return m_explosionRadius; }

private:
    int m_unknown24 = 0;                                // +0x24 -1 for every mine
    float m_explosionRadius = 0.0f;                     // +0x28
};

class C_ProjectileWeaponComp : public C_WeaponComp {    // vtable 0x00817348
public:
    C_ProjectileWeaponComp() : C_WeaponComp(ICK_PROJECTILE_WEAPON) {}
    void Save(int *ioSize, void *buffer) override;      // 0x007df410
    void Load(int size, const void *data) override;     // 0x007df490
    int GetProjectileType() override { return m_projectileType; }       // 0x007e07b0
    float GetAimedDispersion() override { return m_aimedDispersion; }   // 0x007e0770
    float GetMuzzleVelocity() override { return m_muzzleVelocity; }     // 0x007e0780
    int GetReloadSound() override { return m_reloadSound; }             // 0x007e0790
    int GetReloadTimeMs() override { return static_cast<int>(m_reloadTime); }   // 0x007e07a0
    float GetUnknownVf2C() override { return m_unknown48; }             // 0x007e07c0

    // Grenades: ms from the throw to the explosion, 0 = on impact.
    int GetFuseTime() const { return m_fuseTime; }
    int GetExplosionEffect() const { return m_explosionEffect; }
    // Sound group of the explosion (chapter SOUND_CHAPTER_EXPLOSION).
    int GetExplosionSound() const { return m_explosionSound; }
    // Also the damage of a direct hit on a vehicle (four times that for the player's shots
    // in single player on difficulty 1).
    float GetExplosionDamage() const { return m_explosionDamage; }
    float GetExplosionRadius() const { return m_explosionRadius; }

private:
    char m_shortName[8] = {};                           // +0x24
    int m_projectileType = 0;                           // +0x2C 0/1/2
    int m_reloadSound = 0;                              // +0x30
    float m_reloadTime = 0.0f;                          // +0x34 ms
    float m_muzzleVelocity = 0.0f;                      // +0x38 m/s; grenades: times the throw strength (at least 5)
    // The firearm's range fields in the same record position (not read for projectiles).
    float m_idealRange = 0.0f;                          // +0x3C
    float m_damageFalloff = 0.0f;                       // +0x40
    float m_aimedDispersion = 0.0f;                     // +0x44
    float m_unknown48 = 0.0f;                           // +0x48
    int m_fuseTime = 0;                                 // +0x4C ms
    int m_unknown50 = 0;                                // +0x50
    int m_explosionEffect = 0;                          // +0x54
    int m_explosionSound = 0;                           // +0x58
    float m_explosionDamage = 0.0f;                     // +0x5C
    float m_explosionRadius = 0.0f;                     // +0x60
};

class C_FirearmComp : public C_WeaponComp {             // vtable 0x00817314
public:
    C_FirearmComp() : C_WeaponComp(ICK_FIREARM) {}
    void Save(int *ioSize, void *buffer) override;      // 0x007df4b0
    void Load(int size, const void *data) override;     // 0x007df540
    int GetProjectileType() override { return m_projectileType; }       // 0x007e0810
    float GetAimedDispersion() override { return m_aimedDispersion; }   // 0x007e07d0
    float GetMuzzleVelocity() override { return m_muzzleVelocity; }     // 0x007e07e0
    int GetReloadSound() override { return m_reloadSound; }             // 0x007e07f0
    int GetReloadTimeMs() override { return static_cast<int>(m_reloadTime); }   // 0x007e0800
    int GetLastShotSound() override { return m_lastShotSound; }         // 0x007e0820
    int GetBoltSound() override { return m_boltSound; }                 // 0x007e0830
    float GetUnknownVf2C() override { return m_unknown44; }             // 0x007e0840

    // Bullets keep their full power up to the ideal range (m) and lose
    // GetDamageFalloff() per metre beyond it.
    float GetIdealRange() const { return m_idealRange; }
    float GetDamageFalloff() const { return m_damageFalloff; }

private:
    int m_unknown24 = 0;                                // +0x24
    int m_projectileType = 0;                           // +0x28 0 MG42/MG15/MG81, 1 Browning/Type 97, 2 others
    int m_reloadSound = 0;                              // +0x2C
    float m_reloadTime = 0.0f;                          // +0x30 ms
    float m_muzzleVelocity = 0.0f;                      // +0x34
    float m_idealRange = 0.0f;                          // +0x38
    float m_damageFalloff = 0.0f;                       // +0x3C
    float m_aimedDispersion = 0.0f;                     // +0x40
    float m_unknown44 = 0.0f;                           // +0x44
    int m_lastShotSound = 0;                            // +0x48
    int m_boltSound = 0;                                // +0x4C
    char m_shortName[8] = {};                           // +0x50
    float m_unknown58 = 0.0f;                           // +0x58 1, ~6 for scoped rifles
};

class C_ScopeComp : public C_ItemComponent {            // vtable 0x00817304
public:
    C_ScopeComp() : C_ItemComponent(ICK_SCOPE) {}
    void Save(int *ioSize, void *buffer) override;      // 0x007df560
    void Load(int size, const void *data) override;     // 0x007df5a0

    int GetAimMode() const { return m_aimMode; }
    float GetAimedFovFactor() const { return m_aimedFovFactor; }

private:
    int m_aimMode = 6;                                  // +0x0C 5 iron sights, 0-3 reticles, 4 binoculars
    float m_aimedFovFactor = 0.0f;                      // +0x10
};

class C_UniformComp : public C_ItemComponent {          // vtable 0x008172f4
public:
    C_UniformComp() : C_ItemComponent(ICK_UNIFORM) {}
    void Save(int *ioSize, void *buffer) override;      // 0x007df5d0
    void Load(int size, const void *data) override;     // 0x007df610

    int GetDisguiseSide() const { return m_disguiseSide; }

private:
    int m_disguiseSide = 0;                             // +0x0C 0 allied, 1 axis, 2 neutral
};

class C_MedkitComp : public C_ItemComponent {           // vtable 0x008172e4
public:
    C_MedkitComp() : C_ItemComponent(ICK_MEDKIT) {}
    void Save(int *ioSize, void *buffer) override;      // 0x007df640
    void Load(int size, const void *data) override;     // 0x007df680

    float GetHealingCapacity() const { return m_healingCapacity; }

private:
    float m_healingCapacity = 0.0f;                     // +0x0C
};

// Backpacks (also radios and the aqualung): an item store for other items.
class C_BackpackComp : public C_ItemComponent {         // vtable 0x008172d4
public:
    C_BackpackComp() : C_ItemComponent(ICK_BACKPACK) {}
    void Save(int *ioSize, void *buffer) override;      // 0x007df6b0
    void Load(int size, const void *data) override;     // 0x007df6f0

    // 0: a real backpack, whose contents are worn as ITEM_CONTAINER_BACKPACK_CONTENTS.
    int GetSubtype() const { return m_subtype; }
    float GetMaxWeight() const { return m_maxWeight; }
    float GetCapacity() const { return m_capacity; }
    int GetMaxItemSize() const { return m_maxItemSize; }

private:
    int m_subtype = 0;                                  // +0x0C 0 backpack, 1 radio, 2 aqualung
    float m_maxWeight = 0.0f;                           // +0x10
    float m_capacity = 0.0f;                            // +0x14 inventory space
    int m_maxItemSize = 0;                              // +0x18
    float m_unknown1c = 0.0f;                           // +0x1C 900 for the aqualung
};

class C_HeadgearComp : public C_ItemComponent {         // vtable 0x008172c4
public:
    C_HeadgearComp() : C_ItemComponent(ICK_HEADGEAR) {}
    void Save(int *ioSize, void *buffer) override;      // 0x007df720
    void Load(int size, const void *data) override;     // 0x007df760

    int GetHeadgearType() const { return m_headgearType; }
    float GetProtection() const { return m_protection; }

private:
    int m_headgearType = 0;                             // +0x0C 0 helmet, 1 field cap, 2 cap/beret
    float m_protection = 0.0f;                          // +0x10 350 steel helmets, 10 caps
};

class C_SpecialItemComp : public C_ItemComponent {      // vtable 0x008172b4
public:
    C_SpecialItemComp() : C_ItemComponent(ICK_SPECIAL) {}
    void Save(int *ioSize, void *) override { *ioSize = 0; }   // 0x007df790
    void Load(int, const void *) override {}           // 0x007df7a0
};

// ---------------------------------------------------------------- definitions
class C_ItemDef {                                       // vtable 0x00817290
public:
    C_ItemDef(E_ItemRecordType recordType, int id);     // 0x007df7e0
    C_ItemDef(const C_ItemDef &) = delete;
    C_ItemDef &operator=(const C_ItemDef &) = delete;
    virtual ~C_ItemDef();                               // 0x00 0x007df890
    // Writes the record stream (layout at Load); *ioSize == 0: only reports its size.
    virtual void Save(int *ioSize, void *buffer);       // 0x04 0x007dff10
    // Reads the record stream; *outSize receives the bytes used by C_ItemDef (the
    // derived classes read their fields after them).
    virtual void Load(int *outSize, const void *data);  // 0x08 0x007e0150
    virtual int GetCarryMode() = 0;                     // 0x0C
    virtual int GetBehaviour() = 0;                     // 0x10 ITEM_BEHAVIOUR_*
    // Rounds in one item of an ammo type (-1 for other definitions).
    virtual float GetAmmoAmount() { return -1.0f; }     // 0x14 0x007df830
    // Ammo item type a weapon loads, -1 if it has none.
    virtual int GetAmmoItemId() { return -1; }          // 0x18 0x007df840
    virtual int GetHeadgearItemId() { return -1; }      // 0x1C 0x007df850
    virtual int GetUnknown64() { return -1; }           // 0x20 0x007df860

    E_ItemRecordType GetRecordType() const { return m_recordType; }
    int GetUnknown0C() const { return m_unknown0c; }
    int GetId() const { return m_id; }
    int GetTextId() const { return m_textId; }
    float GetWeight() const { return m_weight; }
    // Inventory space units (0 counts as 1); 5 = a backpack.
    int GetInventorySize() const { return m_inventorySize; }
    // Also the sound group (chapter 5) of the item bouncing when thrown.
    int GetCategory() const { return m_category; }
    // Sound group (chapter 5) of the weapon's cartridge shells hitting the ground.
    int GetShellSound() const { return m_shellSound; }
    int GetArmedSound() const { return m_armedSound; }
    int GetTeam() const { return m_team; }
    C_ItemComponent *GetComponent() const { return m_component; }
    C_ItemComponent *GetSecondaryComponent() const { return m_secondaryComponent; }
    // Kind of the component, ICK_NONE without one.
    E_ItemComponentKind GetComponentKind() const { return m_component ? m_component->GetKind() : ICK_NONE; }
    // A real backpack (whose contents become the wearer's backpack contents container).
    bool IsBackpack() const;
    const char *GetModelName() const { return m_modelName; }
    // The first-person model's file (uniforms: the hands of the first-person view).
    const char *GetFpvModelName() const { return m_fpvModelName; }
    // (Left null by Load; the inventory check prints it.)
    const char *GetDisplayName() const { return m_displayName; }

    // Model of the item: the frame named like the model file inside it, loaded on first
    // use (through g_modelCache) and kept; null if the file has none.
    I3D_frame *GetModel();                              // 0x007dfbb0
    // First-person model ("fpv_weapon" frame of the fpv model file); weapons also keep
    // its "magazine" frame.
    I3D_frame *GetFpvModel();                           // 0x007df970
    // Inventory picture (<image>.bmp), loaded on first use.
    IImage *GetImage();                                 // 0x007dfab0
    // Item 231 (the diving gear) only: copies of the mask, fins and tank frames of its
    // model (four entries, any may be null); null for other items.
    I3D_frame **GetDivingGear();                        // 0x007dfcb0
    // Containers the item may be put in (ITEM_FITS_*).
    uint32_t GetAllowedContainers();                    // 0x007dfdb0
    // Inventory group 0..7 (sorting / display; meaning of the values not identified).
    int GetInventoryGroup();                            // 0x007dfe70

protected:
    E_ItemRecordType m_recordType;                      // +0x04
    int m_id;                                           // +0x08 index in g_itemsModule.m_items
    int m_unknown0c = 0;                                // +0x0C 0/1/2 (ammo: the pouch, C_HumanEquipment::GetHolsterSlot)
    char *m_displayName = nullptr;                      // +0x10 left null by Load
    int m_textId = 0;                                   // +0x14
    float m_weight = 0.0f;                              // +0x18
    int m_inventorySize = 0;                            // +0x1C
    I3D_frame *m_fpvFrame = nullptr;                    // +0x20
    char *m_fpvModelName = nullptr;                     // +0x24
    IImage *m_image = nullptr;                          // +0x28
    char *m_imageName = nullptr;                        // +0x2C
    I3D_frame *m_modelFrame = nullptr;                  // +0x30
    char *m_modelName = nullptr;                        // +0x34
    int m_category = 0;                                 // +0x38 5 pistol, 6 rifle, 7 SMG/MG, ...
    int m_shellSound = 0;                               // +0x3C
    int m_armedSound = 0;                               // +0x40
    int m_team = 0;                                     // +0x44 0 allied, 1 axis, 2 all, 3 none
    C_ItemComponent *m_component = nullptr;             // +0x48
    C_ItemComponent *m_secondaryComponent = nullptr;    // +0x4C C_ScopeComp on aimable weapons
    I3D_frame **m_divingGear = nullptr;                 // +0x50
};

class C_AmmoItemDef : public C_ItemDef {                // vtable 0x008173e4, 0x58 bytes
public:
    explicit C_AmmoItemDef(int id) : C_ItemDef(ITEM_AMMO, id) {}   // 0x007e0850
    void Save(int *ioSize, void *buffer) override;      // 0x007e08d0
    void Load(int *outSize, const void *data) override; // 0x007e0910
    int GetCarryMode() override { return ITEM_CARRY_AMMO; }   // 0x007e0880
    int GetBehaviour() override { return ITEM_BEHAVIOUR_AMMO; }   // 0x007e0890
    float GetAmmoAmount() override { return m_ammoAmount; }       // 0x007e0870

private:
    float m_ammoAmount = 0.0f;                          // +0x54
};

class C_AnimatedItemDef : public C_ItemDef {            // vtable 0x00817408, 0x68 bytes
public:
    explicit C_AnimatedItemDef(int id) : C_ItemDef(ITEM_ANIMATED, id) {}   // 0x007e0940
    ~C_AnimatedItemDef() override;                      // 0x007e09c0
    void Save(int *ioSize, void *buffer) override;      // 0x007e0a20
    void Load(int *outSize, const void *data) override; // 0x007e0a80
    int GetCarryMode() override { return m_carryMode; } // 0x007e0980
    int GetBehaviour() override { return m_behaviour; } // 0x007e0990
    int GetAmmoItemId() override { return m_ammoItemId; }   // 0x007e0970

    int GetAttachment() const { return m_attachment; }
    // The first-person model's magazine (GetFpvModel keeps it).
    I3D_frame *GetFpvMagazine() const { return m_fpvMagazine; }

private:
    friend class C_ItemDef;                             // GetFpvModel keeps the magazine

    int m_ammoItemId = 0;                               // +0x54 uniforms carried as animated items store headgear here
    int m_carryMode = 0;                                // +0x58
    int m_behaviour = 0;                                // +0x5C
    int m_attachment = 0;                               // +0x60 0 none, 1 scope, 2 silencer
    I3D_frame *m_fpvMagazine = nullptr;                 // +0x64 holds a reference
};

class C_StaticItemDef : public C_ItemDef {              // vtable 0x0081742c, 0x68 bytes
public:
    explicit C_StaticItemDef(int id) : C_ItemDef(ITEM_STATIC, id) {}   // 0x007e0ac0
    void Save(int *ioSize, void *buffer) override;      // 0x007e0b50
    void Load(int *outSize, const void *data) override; // 0x007e0bb0
    int GetCarryMode() override { return m_carryMode; } // 0x007e0ae0
    int GetBehaviour() override { return m_behaviour; } // 0x007e0af0
    int GetHeadgearItemId() override { return m_headgearItemId; }   // 0x007e0b00
    int GetUnknown64() override { return m_unknown64; } // 0x007e0b10

private:
    int m_carryMode = 0;                                // +0x54
    int m_behaviour = 0;                                // +0x58
    int m_unknown5c = 0;                                // +0x5C
    int m_headgearItemId = 0;                           // +0x60 uniforms: default headgear item
    int m_unknown64 = 0;                                // +0x64 uniforms only: -1..3
};

constexpr int MAX_ITEM_TYPES = 500;

// Definition of item type `typeId` (g_itemsModule.m_items), null outside
// 0..MAX_ITEM_TYPES-1 or for an empty slot.
C_ItemDef *GetItemDef(int typeId);                      // inline in the original (0x007df190)
