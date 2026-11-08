// Item definitions and their components (original source 0x007df330..0x007e0bdf).
#include "game/item_defs.h"

#include <cstring>

#include "game/items_module.h"
#include "game/resource_cache.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// Fixed-size strings of the record stream (with terminator).
constexpr int RECORD_NAME_SIZE = 20;
// Size of the C_ItemDef part of the stream without its components.
constexpr int RECORD_BASE_SIZE = 0xA8;
// Stream offsets of the C_ItemDef fields.
enum : int {
    REC_FPV_MODEL = 0x00,
    REC_IMAGE = 0x14,
    REC_MODEL = 0x28,
    REC_UNKNOWN0C = 0x3C,
    // 0x40: 16 unused bytes
    REC_DISPLAY_NAME = 0x50,    // not loaded
    REC_TEXT_ID = 0x64,
    REC_WEIGHT = 0x68,
    REC_INVENTORY_SIZE = 0x6C,
    REC_CATEGORY = 0x70,
    REC_SHELL_SOUND = 0x74,
    REC_ARMED_SOUND = 0x78,
    REC_TEAM = 0x7C,
    REC_COMPONENT = 0x80,       // kind+1 (0 = none), then the component's stream, then the
                                // secondary component the same way, then 0x20 reserved bytes
};

// Item 231 is the diving gear; its model has these parts.
constexpr int ITEM_DIVING_GEAR = 231;
const char *const DIVING_GEAR_PARTS[4] = {"mask", "finL", "finR", "tank"};

// Driver states switched off while an inventory picture loads (not identified).
constexpr int DRIVER_STATE_15 = 0x15;
constexpr int DRIVER_STATE_19 = 0x19;

constexpr uint32_t FRAME_MASK_VISUAL_OR_DUMMY = FRAME_MASK_VISUAL | FRAME_MASK_DUMMY;

template <class T> T ReadAt(const void *data, int offset)
{
    T value;
    std::memcpy(&value, static_cast<const uint8_t *>(data) + offset, sizeof(T));
    return value;
}

template <class T> void WriteAt(void *buffer, int offset, const T &value)
{
    std::memcpy(static_cast<uint8_t *>(buffer) + offset, &value, sizeof(T));
}

// A record name: null for an empty string.
char *ReadName(const void *data, int offset)
{
    char *name = new char[RECORD_NAME_SIZE];
    std::memcpy(name, static_cast<const uint8_t *>(data) + offset, RECORD_NAME_SIZE);
    if (name[0] == '\0') {
        delete[] name;
        return nullptr;
    }
    return name;
}

// Writes a name (with its terminator; the rest of the field is left as it is) or, for
// none, a zero-filled field.
void WriteName(void *buffer, int offset, const char *name)
{
    uint8_t *dst = static_cast<uint8_t *>(buffer) + offset;
    if (name)
        std::memcpy(dst, name, std::strlen(name) + 1);
    else
        std::memset(dst, 0, RECORD_NAME_SIZE);
}

// New component of kind `kind` (items.sav value minus one), null for unknown kinds.
C_ItemComponent *CreateComponent(int kind)
{
    switch (kind) {
    case ICK_MELEE: return new C_MeleeComp;
    case ICK_MINE: return new C_MineComp;
    case ICK_PROJECTILE_WEAPON: return new C_ProjectileWeaponComp;
    case ICK_FIREARM: return new C_FirearmComp;
    case ICK_SCOPE: return new C_ScopeComp;
    case ICK_UNIFORM: return new C_UniformComp;
    case ICK_MEDKIT: return new C_MedkitComp;
    case ICK_BACKPACK: return new C_BackpackComp;
    case ICK_HEADGEAR: return new C_HeadgearComp;
    case ICK_SPECIAL: return new C_SpecialItemComp;
    default: return nullptr;
    }
}

// Reads a component (kind+1 at `cursor`, then its stream) into `component`; returns
// the bytes of the component stream (0 without a component).
int LoadComponent(C_ItemComponent *&component, const uint8_t *&cursor)
{
    const int kind = ReadAt<int>(cursor, 0);
    cursor += sizeof(int);
    if (kind == 0)
        return 0;
    // (The original goes on with a null component for unknown kinds and crashes.)
    component = CreateComponent(kind - 1);
    if (!component)
        return 0;
    int size = 0;
    component->Save(&size, nullptr);
    component->Load(size, cursor);
    cursor += size;
    return size;
}

// Writes kind+1 and the stream of `component` (0 without one) at `cursor` and moves it on.
void SaveComponent(C_ItemComponent *component, uint8_t *&cursor)
{
    if (!component) {
        WriteAt(cursor, 0, 0);
        cursor += sizeof(int);
        return;
    }
    WriteAt(cursor, 0, static_cast<int>(component->GetKind()) + 1);
    cursor += sizeof(int);
    int size = 0;
    component->Save(&size, cursor);     // the size
    component->Save(&size, cursor);     // the data
    cursor += size;
}

// Loads a model file through the model cache; null if it does not load.
I3D_frame *OpenModelFile(const char *fileName)
{
    I3D_frame *model = I3DGetDriver()->CreateFrame(FRAME_MODEL);
    // (The original appends an empty extension string to the name.)
    if (g_modelCache.OpenInto(static_cast<I3D_model *>(model), fileName) != I3D_OK) {
        model->Release();
        return nullptr;
    }
    return model;
}

} // namespace

// ---------------------------------------------------------------------------
// components

void C_ItemComponent::Get(void *field, int size, const void *data, int offset)
{
    std::memcpy(field, static_cast<const uint8_t *>(data) + offset - STREAM_BASE, size);
}

void C_ItemComponent::Put(void *buffer, int offset, const void *field, int size)
{
    std::memcpy(static_cast<uint8_t *>(buffer) + offset - STREAM_BASE, field, size);
}

void C_ItemComponent::GetBase(const void *data)
{
    Get(m_reserved04, data, 0x04);
    Get(m_kind, data, 0x08);
}

void C_ItemComponent::PutBase(void *buffer) const
{
    Put(buffer, 0x04, m_reserved04);
    Put(buffer, 0x08, m_kind);
}

void C_WeaponComp::GetWeapon(const void *data)
{
    GetBase(data);
    Get(m_unknown0c, data, 0x0C);
    Get(m_damage, data, 0x10);
    Get(m_shotSound, data, 0x14);
    Get(m_attackDelay, data, 0x18);
    Get(m_effect, data, 0x1C);
    Get(m_canReload, data, 0x20);
}

void C_WeaponComp::PutWeapon(void *buffer) const
{
    PutBase(buffer);
    Put(buffer, 0x0C, m_unknown0c);
    Put(buffer, 0x10, m_damage);
    Put(buffer, 0x14, m_shotSound);
    Put(buffer, 0x18, m_attackDelay);
    Put(buffer, 0x1C, m_effect);
    Put(buffer, 0x20, static_cast<uint32_t>(m_canReload));   // (with its padding)
}

void C_MeleeComp::Save(int *ioSize, void *buffer)
{
    if (*ioSize == 0) {
        *ioSize = 0x24;
        return;
    }
    PutWeapon(buffer);
    Put(buffer, 0x24, m_idealRange);
}

void C_MeleeComp::Load(int, const void *data)
{
    GetWeapon(data);
    Get(m_idealRange, data, 0x24);
}

void C_MineComp::Save(int *ioSize, void *buffer)
{
    if (*ioSize == 0) {
        *ioSize = 0x28;
        return;
    }
    PutWeapon(buffer);
    Put(buffer, 0x24, m_unknown24);
    Put(buffer, 0x28, m_explosionRadius);
}

void C_MineComp::Load(int, const void *data)
{
    GetWeapon(data);
    Get(m_unknown24, data, 0x24);
    Get(m_explosionRadius, data, 0x28);
}

// Projectile weapons and firearms are followed in the stream by two 20-character
// strings that are always written empty and never read.
void C_ProjectileWeaponComp::Save(int *ioSize, void *buffer)
{
    if (*ioSize == 0) {
        *ioSize = 0x60 + 2 * RECORD_NAME_SIZE;
        return;
    }
    PutWeapon(buffer);
    Put(buffer, 0x24, m_shortName);
    Put(buffer, 0x2C, m_projectileType);
    Put(buffer, 0x30, m_reloadSound);
    Put(buffer, 0x34, m_reloadTime);
    Put(buffer, 0x38, m_muzzleVelocity);
    Put(buffer, 0x3C, m_idealRange);
    Put(buffer, 0x40, m_damageFalloff);
    Put(buffer, 0x44, m_aimedDispersion);
    Put(buffer, 0x48, m_unknown48);
    Put(buffer, 0x4C, m_fuseTime);
    Put(buffer, 0x50, m_unknown50);
    Put(buffer, 0x54, m_explosionEffect);
    Put(buffer, 0x58, m_explosionSound);
    Put(buffer, 0x5C, m_explosionDamage);
    Put(buffer, 0x60, m_explosionRadius);
    *ioSize = 0x60 + 2 * RECORD_NAME_SIZE;
    std::memset(static_cast<uint8_t *>(buffer) + 0x60, 0, 2 * RECORD_NAME_SIZE);
}

void C_ProjectileWeaponComp::Load(int, const void *data)
{
    GetWeapon(data);
    Get(m_shortName, data, 0x24);
    Get(m_projectileType, data, 0x2C);
    Get(m_reloadSound, data, 0x30);
    Get(m_reloadTime, data, 0x34);
    Get(m_muzzleVelocity, data, 0x38);
    Get(m_idealRange, data, 0x3C);
    Get(m_damageFalloff, data, 0x40);
    Get(m_aimedDispersion, data, 0x44);
    Get(m_unknown48, data, 0x48);
    Get(m_fuseTime, data, 0x4C);
    Get(m_unknown50, data, 0x50);
    Get(m_explosionEffect, data, 0x54);
    Get(m_explosionSound, data, 0x58);
    Get(m_explosionDamage, data, 0x5C);
    Get(m_explosionRadius, data, 0x60);
}

void C_FirearmComp::Save(int *ioSize, void *buffer)
{
    if (*ioSize == 0) {
        *ioSize = 0x58 + 2 * RECORD_NAME_SIZE;
        return;
    }
    PutWeapon(buffer);
    Put(buffer, 0x24, m_unknown24);
    Put(buffer, 0x28, m_projectileType);
    Put(buffer, 0x2C, m_reloadSound);
    Put(buffer, 0x30, m_reloadTime);
    Put(buffer, 0x34, m_muzzleVelocity);
    Put(buffer, 0x38, m_idealRange);
    Put(buffer, 0x3C, m_damageFalloff);
    Put(buffer, 0x40, m_aimedDispersion);
    Put(buffer, 0x44, m_unknown44);
    Put(buffer, 0x48, m_lastShotSound);
    Put(buffer, 0x4C, m_boltSound);
    Put(buffer, 0x50, m_shortName);
    Put(buffer, 0x58, m_unknown58);
    *ioSize = 0x58 + 2 * RECORD_NAME_SIZE;
    // (Only blanked for m_unknown24 == 0; otherwise the bytes are left as they are.)
    if (m_unknown24 == 0)
        std::memset(static_cast<uint8_t *>(buffer) + 0x58, 0, 2 * RECORD_NAME_SIZE);
}

void C_FirearmComp::Load(int, const void *data)
{
    GetWeapon(data);
    Get(m_unknown24, data, 0x24);
    Get(m_projectileType, data, 0x28);
    Get(m_reloadSound, data, 0x2C);
    Get(m_reloadTime, data, 0x30);
    Get(m_muzzleVelocity, data, 0x34);
    Get(m_idealRange, data, 0x38);
    Get(m_damageFalloff, data, 0x3C);
    Get(m_aimedDispersion, data, 0x40);
    Get(m_unknown44, data, 0x44);
    Get(m_lastShotSound, data, 0x48);
    Get(m_boltSound, data, 0x4C);
    Get(m_shortName, data, 0x50);
    Get(m_unknown58, data, 0x58);
}

void C_ScopeComp::Save(int *ioSize, void *buffer)
{
    if (*ioSize == 0) {
        *ioSize = 0x10;
        return;
    }
    PutBase(buffer);
    Put(buffer, 0x0C, m_aimMode);
    Put(buffer, 0x10, m_aimedFovFactor);
}

void C_ScopeComp::Load(int, const void *data)
{
    GetBase(data);
    Get(m_aimMode, data, 0x0C);
    Get(m_aimedFovFactor, data, 0x10);
}

void C_UniformComp::Save(int *ioSize, void *buffer)
{
    if (*ioSize == 0) {
        *ioSize = 0x0C;
        return;
    }
    PutBase(buffer);
    Put(buffer, 0x0C, m_disguiseSide);
}

void C_UniformComp::Load(int, const void *data)
{
    GetBase(data);
    Get(m_disguiseSide, data, 0x0C);
}

void C_MedkitComp::Save(int *ioSize, void *buffer)
{
    if (*ioSize == 0) {
        *ioSize = 0x0C;
        return;
    }
    PutBase(buffer);
    Put(buffer, 0x0C, m_healingCapacity);
}

void C_MedkitComp::Load(int, const void *data)
{
    GetBase(data);
    Get(m_healingCapacity, data, 0x0C);
}

void C_BackpackComp::Save(int *ioSize, void *buffer)
{
    if (*ioSize == 0) {
        *ioSize = 0x1C;
        return;
    }
    PutBase(buffer);
    Put(buffer, 0x0C, m_subtype);
    Put(buffer, 0x10, m_maxWeight);
    Put(buffer, 0x14, m_capacity);
    Put(buffer, 0x18, m_maxItemSize);
    Put(buffer, 0x1C, m_unknown1c);
}

void C_BackpackComp::Load(int, const void *data)
{
    GetBase(data);
    Get(m_subtype, data, 0x0C);
    Get(m_maxWeight, data, 0x10);
    Get(m_capacity, data, 0x14);
    Get(m_maxItemSize, data, 0x18);
    Get(m_unknown1c, data, 0x1C);
}

void C_HeadgearComp::Save(int *ioSize, void *buffer)
{
    if (*ioSize == 0) {
        *ioSize = 0x10;
        return;
    }
    PutBase(buffer);
    Put(buffer, 0x0C, m_headgearType);
    Put(buffer, 0x10, m_protection);
}

void C_HeadgearComp::Load(int, const void *data)
{
    GetBase(data);
    Get(m_headgearType, data, 0x0C);
    Get(m_protection, data, 0x10);
}

// ---------------------------------------------------------------------------
// C_ItemDef

C_ItemDef::C_ItemDef(E_ItemRecordType recordType, int id) : m_recordType(recordType), m_id(id) {}

C_ItemDef::~C_ItemDef()
{
    if (m_fpvFrame) {
        ReleaseFrameTree(m_fpvFrame);
        m_fpvFrame = nullptr;
    }
    delete m_component;
    m_component = nullptr;
    delete m_secondaryComponent;
    m_secondaryComponent = nullptr;
    delete[] m_fpvModelName;
    m_fpvModelName = nullptr;
    if (m_image) {
        m_image->Release();
        m_image = nullptr;
    }
    delete[] m_imageName;
    m_imageName = nullptr;
    if (m_modelFrame) {
        ReleaseFrameTree(m_modelFrame);
        m_modelFrame = nullptr;
    }
    delete[] m_modelName;
    m_modelName = nullptr;
    if (m_divingGear) {
        for (int i = 0; i < 4; ++i) {
            if (m_divingGear[i])
                m_divingGear[i]->Release();
        }
        delete[] m_divingGear;
        m_divingGear = nullptr;
    }
    delete[] m_displayName;
    m_displayName = nullptr;
}

bool C_ItemDef::IsBackpack() const
{
    return m_component && m_component->GetKind() == ICK_BACKPACK &&
           static_cast<const C_BackpackComp *>(m_component)->GetSubtype() == 0;
}

void C_ItemDef::Save(int *ioSize, void *buffer)
{
    const bool write = *ioSize != 0;
    int componentSize = 0;
    if (m_component)
        m_component->Save(&componentSize, buffer);
    int secondarySize = 0;
    if (m_secondaryComponent)
        m_secondaryComponent->Save(&secondarySize, buffer);
    *ioSize = RECORD_BASE_SIZE + componentSize + secondarySize;
    if (!write)
        return;

    WriteName(buffer, REC_FPV_MODEL, m_fpvModelName);
    WriteName(buffer, REC_IMAGE, m_imageName);
    WriteName(buffer, REC_MODEL, m_modelName);
    WriteAt(buffer, REC_UNKNOWN0C, m_unknown0c);
    WriteName(buffer, REC_DISPLAY_NAME, m_displayName);
    WriteAt(buffer, REC_TEXT_ID, m_textId);
    WriteAt(buffer, REC_WEIGHT, m_weight);
    WriteAt(buffer, REC_INVENTORY_SIZE, m_inventorySize);
    WriteAt(buffer, REC_CATEGORY, m_category);
    WriteAt(buffer, REC_SHELL_SOUND, m_shellSound);
    WriteAt(buffer, REC_ARMED_SOUND, m_armedSound);
    WriteAt(buffer, REC_TEAM, m_team);

    uint8_t *cursor = static_cast<uint8_t *>(buffer) + REC_COMPONENT;
    SaveComponent(m_component, cursor);
    SaveComponent(m_secondaryComponent, cursor);
}

void C_ItemDef::Load(int *outSize, const void *data)
{
    *outSize = 0;
    C_ItemDef::Save(outSize, nullptr);   // the size without components: RECORD_BASE_SIZE

    m_fpvModelName = ReadName(data, REC_FPV_MODEL);
    m_imageName = ReadName(data, REC_IMAGE);
    m_modelName = ReadName(data, REC_MODEL);
    m_displayName = nullptr;
    m_unknown0c = ReadAt<int>(data, REC_UNKNOWN0C);
    m_textId = ReadAt<int>(data, REC_TEXT_ID);
    m_weight = ReadAt<float>(data, REC_WEIGHT);
    m_inventorySize = ReadAt<int>(data, REC_INVENTORY_SIZE);
    m_category = ReadAt<int>(data, REC_CATEGORY);
    m_shellSound = ReadAt<int>(data, REC_SHELL_SOUND);
    m_armedSound = ReadAt<int>(data, REC_ARMED_SOUND);
    m_team = ReadAt<int>(data, REC_TEAM);

    const uint8_t *cursor = static_cast<const uint8_t *>(data) + REC_COMPONENT;
    *outSize += LoadComponent(m_component, cursor);
    *outSize += LoadComponent(m_secondaryComponent, cursor);
}

I3D_frame *C_ItemDef::GetModel()
{
    if (m_modelFrame || !m_modelName)
        return m_modelFrame;
    I3D_frame *model = OpenModelFile(m_modelName);
    if (!model)
        return m_modelFrame;
    I3D_frame *source = model->FindChildFrame(m_modelName, FRAME_MASK_VISUAL);
    if (!source || !CloneFrameTree(m_modelFrame, source, nullptr))
        m_modelFrame = nullptr;
    model->Release();
    return m_modelFrame;
}

I3D_frame *C_ItemDef::GetFpvModel()
{
    if (m_fpvFrame || !m_fpvModelName)
        return m_fpvFrame;
    I3D_frame *model = OpenModelFile(m_fpvModelName);
    if (!model)
        return m_fpvFrame;
    I3D_frame *source = model->FindChildFrame("fpv_weapon", FRAME_MASK_VISUAL);
    if (m_recordType == ITEM_ANIMATED) {
        if (I3D_frame *magazine = model->FindChildFrame("magazine", FRAME_MASK_VISUAL_OR_DUMMY)) {
            magazine->AddRef();
            static_cast<C_AnimatedItemDef *>(this)->m_fpvMagazine = magazine;
        }
    }
    if (!source || !CloneFrameTree(m_fpvFrame, source, nullptr))
        m_fpvFrame = nullptr;
    model->Release();
    return m_fpvFrame;
}

IImage *C_ItemDef::GetImage()
{
    if (m_image || !m_imageName)
        return m_image;
    I3D_driver *driver = I3DGetDriver();
    const bool state15 = driver->GetState(DRIVER_STATE_15) != 0;
    const bool state19 = driver->GetState(DRIVER_STATE_19) != 0;
    driver->SetState(DRIVER_STATE_15, 0);
    driver->SetState(DRIVER_STATE_19, 0);
    char path[128];
    std::strcpy(path, m_imageName);
    std::strcat(path, ".bmp");
#ifndef HD2_DEDICATED   // no textures in LS3DRF.dll
    if (GetIGraph()->LoadTexture(path, 0, 0, &m_image) != 0)
        m_image = nullptr;
#else
    m_image = nullptr;
#endif
    driver->SetState(DRIVER_STATE_15, state15);
    driver->SetState(DRIVER_STATE_19, state19);
    return m_image;
}

I3D_frame **C_ItemDef::GetDivingGear()
{
    if (m_divingGear)
        return m_divingGear;
    if (m_id != ITEM_DIVING_GEAR || !m_modelName)
        return nullptr;
    I3D_frame *model = OpenModelFile(m_modelName);
    if (!model)
        return nullptr;
    m_divingGear = new I3D_frame *[4];
    for (int i = 0; i < 4; ++i) {
        m_divingGear[i] = nullptr;
        if (I3D_frame *source = model->FindChildFrame(DIVING_GEAR_PARTS[i], FRAME_MASK_VISUAL))
            CloneFrameTree(m_divingGear[i], source, nullptr);
    }
    model->Release();
    return m_divingGear;
}

uint32_t C_ItemDef::GetAllowedContainers()
{
    const bool ammo = m_recordType == ITEM_AMMO;
    const int behaviour = GetBehaviour();
    if (behaviour == ITEM_BEHAVIOUR_HEADGEAR)
        return ITEM_FITS_HEAD;
    if (behaviour == ITEM_BEHAVIOUR_UNIFORM)
        return ITEM_FITS_UNIFORM;
    const int size = m_inventorySize;
    if (size == 5)
        return ITEM_FITS_BACKPACK;
    uint32_t fits = 0;
    // Long weapons.
    if ((size == 2 || size == 3) && !ammo &&
        (behaviour == ITEM_BEHAVIOUR_SMG || behaviour == ITEM_BEHAVIOUR_RIFLE || behaviour == ITEM_BEHAVIOUR_LMG || behaviour == ITEM_BEHAVIOUR_LAUNCHER || behaviour == ITEM_BEHAVIOUR_ASSAULT))
        fits = ITEM_FITS_LONG_GUN;
    // Anything usable.
    if (size != 4 && !ammo && (behaviour < ITEM_BEHAVIOUR_MINE || behaviour == ITEM_BEHAVIOUR_ASSAULT))
        fits |= ITEM_FITS_HANDS;
    if (size == 0 || size == 1)
        fits |= ITEM_FITS_BELT | ITEM_FITS_BACKPACK_CONTENTS;
    if (size == 2) {
        if (ammo || behaviour == ITEM_BEHAVIOUR_STICKY_CHARGE || behaviour == ITEM_BEHAVIOUR_MINE)
            fits |= ITEM_FITS_BACKPACK_CONTENTS;
        if (behaviour == ITEM_BEHAVIOUR_MEDKIT)
            fits |= ITEM_FITS_BELT | ITEM_FITS_BACKPACK_CONTENTS;
    }
    return fits;
}

int C_ItemDef::GetInventoryGroup()
{
    const int behaviour = GetBehaviour();
    if (behaviour == ITEM_BEHAVIOUR_FIXED)
        return 0;
    if (m_recordType == ITEM_AMMO)
        return 4;
    switch (behaviour) {
    case ITEM_BEHAVIOUR_HEADGEAR:
    case ITEM_BEHAVIOUR_UNIFORM:
        return 6;
    case ITEM_BEHAVIOUR_MELEE:
    case 1:
    case ITEM_BEHAVIOUR_SIDEARM:
        return 1;
    case ITEM_BEHAVIOUR_SMG:
    case ITEM_BEHAVIOUR_RIFLE:
        return 2;
    case ITEM_BEHAVIOUR_LMG:
    case ITEM_BEHAVIOUR_LAUNCHER:
    case 7:
    case ITEM_BEHAVIOUR_ASSAULT:
        return 3;
    case ITEM_BEHAVIOUR_THROWN:
    case ITEM_BEHAVIOUR_MINE:
    case ITEM_BEHAVIOUR_STICKY_CHARGE:
    case ITEM_BEHAVIOUR_MISSION_EXPLOSIVE:
        return 5;
    default:
        return 7;
    }
}

// ---------------------------------------------------------------------------
// derived definitions: their fields follow the C_ItemDef part of the stream

void C_AmmoItemDef::Save(int *ioSize, void *buffer)
{
    const bool write = *ioSize != 0;
    C_ItemDef::Save(ioSize, buffer);
    if (!write) {
        *ioSize += 4;
        return;
    }
    // (The size reported after writing leaves this field out.)
    WriteAt(buffer, *ioSize, m_ammoAmount);
}

void C_AmmoItemDef::Load(int *outSize, const void *data)
{
    C_ItemDef::Load(outSize, data);
    m_ammoAmount = ReadAt<float>(data, *outSize);
}

C_AnimatedItemDef::~C_AnimatedItemDef()
{
    if (m_fpvMagazine)
        m_fpvMagazine->Release();
}

void C_AnimatedItemDef::Save(int *ioSize, void *buffer)
{
    const bool write = *ioSize != 0;
    C_ItemDef::Save(ioSize, buffer);
    if (!write) {
        *ioSize += 0x10;
        return;
    }
    WriteAt(buffer, *ioSize + 0x0, m_carryMode);
    WriteAt(buffer, *ioSize + 0x4, m_behaviour);
    WriteAt(buffer, *ioSize + 0x8, m_attachment);
    WriteAt(buffer, *ioSize + 0xC, m_ammoItemId);
}

void C_AnimatedItemDef::Load(int *outSize, const void *data)
{
    C_ItemDef::Load(outSize, data);
    m_carryMode = ReadAt<int>(data, *outSize + 0x0);
    m_behaviour = ReadAt<int>(data, *outSize + 0x4);
    m_attachment = ReadAt<int>(data, *outSize + 0x8);
    m_ammoItemId = ReadAt<int>(data, *outSize + 0xC);
}

void C_StaticItemDef::Save(int *ioSize, void *buffer)
{
    const bool write = *ioSize != 0;
    C_ItemDef::Save(ioSize, buffer);
    if (!write) {
        *ioSize += 0x14;
        return;
    }
    WriteAt(buffer, *ioSize + 0x00, m_carryMode);
    WriteAt(buffer, *ioSize + 0x04, m_behaviour);
    WriteAt(buffer, *ioSize + 0x08, m_unknown5c);
    WriteAt(buffer, *ioSize + 0x0C, m_headgearItemId);
    WriteAt(buffer, *ioSize + 0x10, m_unknown64);
}

void C_StaticItemDef::Load(int *outSize, const void *data)
{
    C_ItemDef::Load(outSize, data);
    m_carryMode = ReadAt<int>(data, *outSize + 0x00);
    m_behaviour = ReadAt<int>(data, *outSize + 0x04);
    m_unknown5c = ReadAt<int>(data, *outSize + 0x08);
    m_headgearItemId = ReadAt<int>(data, *outSize + 0x0C);
    m_unknown64 = ReadAt<int>(data, *outSize + 0x10);
}
