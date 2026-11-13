// Item instances (original source 0x007d3650..0x007d6a70): C_Item and the ammo,
// container, medkit and weapon items.
#include "game/items.h"

#include <algorithm>
#include <cstdlib>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/game.h"
#include "game/item_defs.h"
#include "game/items_module.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "game/shots.h"
#include "game/sound_table.h"
#include "ls3d/i3d.h"

namespace {

// Saved-game chunks. C_Item writes its own chunk; the derived classes add theirs after
// it, at the level of the caller's chunk.
enum : uint16_t {
    CHUNK_ITEM = 0x2E6B,
    CHUNK_ITEM_UID = 0x64,
    CHUNK_ITEM_TIME = 0x65,
    CHUNK_ITEM_CONTAINER = 0x66,
    CHUNK_ITEM_OWNER = 0x67,            // actor id, -1 = none
    CHUNK_ITEM_IN_WORLD = 0x68,
    // LoadGame expects these two ids for the fields saved as 0x66 / 0x67, so an item's
    // owner and container index never come back from a saved game.
    CHUNK_ITEM_LOAD_CONTAINER = 0xCA,
    CHUNK_ITEM_LOAD_OWNER = 0xCB,

    CHUNK_AMMO_MAX_ROUNDS = 0xC8,
    CHUNK_AMMO_ROUNDS = 0xC9,
    CHUNK_MEDKIT_CHARGES = 0xC8,

    CHUNK_WEAPON_ROUNDS = 0xC8,
    CHUNK_WEAPON_CAPACITY = 0xC9,
    CHUNK_WEAPON_UNKNOWN58 = 0xCA,
    CHUNK_WEAPON_READY_TIME = 0xCB,
    CHUNK_WEAPON_FLAGS = 0xCC,
    CHUNK_WEAPON_LAST_SHOT = 0xCD,
    CHUNK_WEAPON_SHOT_INTERVAL = 0xCE,
    CHUNK_WEAPON_SHOT_COUNT = 0xCF,
    CHUNK_WEAPON_PROJECTILE_KIND = 0xD0,
    CHUNK_WEAPON_UNKNOWN90 = 0xD1,
    CHUNK_WEAPON_UNKNOWN94 = 0xD2,
};

constexpr uint32_t FIND_ANY_FRAME = 0xFFFF;
constexpr float HALF_PI = 1.5707964f;
// Group of SOUND_CHAPTER_WEAPON_MANIPULATION: trigger of an empty weapon.
constexpr int SOUND_GROUP_EMPTY_CLICK = 4;
// AI noise of shots is made at most this often (ms).
constexpr uint32_t SHOT_NOISE_INTERVAL = 1000;
// StopReloadSound() keeps a reload sound that ends within this time (ms).
constexpr int RELOAD_SOUND_GRACE = 200;

// The weapon sounds share one request (0x0098afe0) like in the original, so a field
// one sound sets carries over to the next (e.g. the range scale of silenced shots).
S_SoundRequest g_weaponSound;

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }

uint32_t Now() { return GetGame()->GetMissionTime(); }

// Sounds of the local player's own weapon are played non-positional while the HUD is up.
bool IsOwnWeaponHeard(C_Actor *shooter)
{
    return static_cast<C_Human *>(shooter)->IsLocalPlayer() && GetGame()->IsFirstPersonView();
}

// Reads a 4-byte value chunk and leaves it.
template <typename T> T ReadValue(C_chunk &chunk)
{
    T value{};
    chunk.Read(value);
    chunk.Ascend();
    return value;
}

} // namespace

// ---------------------------------------------------------------------------
// C_Item

C_Item::~C_Item()
{
    if (m_def && m_def->GetBehaviour() == ITEM_BEHAVIOUR_RESPAWNING)
        g_itemsModule.RemoveRespawningItem(this);
    ReleaseFrame(0);
}

void C_Item::Init(int typeId)
{
    m_def = GetItemDef(typeId);
    m_typeId = typeId;
    m_time = 0;
    m_armed = m_def && m_def->GetBehaviour() == ITEM_BEHAVIOUR_MINE;
    if (m_def->GetBehaviour() == ITEM_BEHAVIOUR_RESPAWNING)
        g_itemsModule.AddRespawningItem(this);
}

float C_Item::GetWeight()
{
    return m_def->GetWeight();
}

int C_Item::GetSize()
{
    return m_def->GetInventorySize();
}

float C_Item::GetSpace()
{
    const float size = static_cast<float>(m_def->GetInventorySize());
    return size == 0.0f ? 1.0f : size + size;
}

I3D_frame *C_Item::GetFrame(int index)
{
    if (index != 0)
        return nullptr;
    if (!m_frame)
        CloneFrameTree(m_frame, m_def->GetModel(), nullptr);
    return m_frame;
}

void C_Item::ReleaseFrame(int index)
{
    if (index == 0 && m_frame) {
        ReleaseFrameTree(m_frame);
        m_frame = nullptr;
    }
}

bool C_Item::TimerElapsed()
{
    if (m_time > Now())
        return false;
    m_time = 0x7FFFFFFF;
    return true;
}

void C_Item::StartTimer()
{
    m_time = Now();
}

bool C_Item::SaveGame(C_chunk &chunk)
{
    if (!chunk.Begin(CHUNK_ITEM))
        return false;
    chunk.WriteChunk(CHUNK_ITEM_UID, m_uid);
    chunk.WriteChunk(CHUNK_ITEM_TIME, m_time);
    chunk.WriteChunk(CHUNK_ITEM_CONTAINER, m_container);
    chunk.WriteChunk(CHUNK_ITEM_OWNER, m_owner ? m_owner->GetNetId() : -1);
    chunk.WriteChunk(CHUNK_ITEM_IN_WORLD, static_cast<int>(m_inWorld));
    chunk.Ascend();
    return true;
}

bool C_Item::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    if (!chunk.Descend(id))
        return false;
    if (id != CHUNK_ITEM) {
        chunk.Ascend();
        return false;
    }
    uint16_t sub;
    while (chunk.RemainingSize() > 0 && chunk.Descend(sub)) {
        switch (sub) {
        case CHUNK_ITEM_UID:
            m_uid = ReadValue<int>(chunk);
            break;
        case CHUNK_ITEM_TIME:
            m_time = ReadValue<uint32_t>(chunk);
            break;
        case CHUNK_ITEM_IN_WORLD:
            m_inWorld = ReadValue<int>(chunk) != 0;
            break;
        case CHUNK_ITEM_LOAD_CONTAINER:
            m_container = ReadValue<int>(chunk);
            break;
        case CHUNK_ITEM_LOAD_OWNER: {
            const int owner = ReadValue<int>(chunk);
            // (By index, as the original: GetActor 0x00475dd0 at 0x007d4430. SaveGame writes the owner under
            // CHUNK_ITEM_OWNER, which this switch does not read, so the game's own saves never reach here.)
            m_owner = owner == -1 ? nullptr : g_actorsModule.GetActor(owner);
            break;
        }
        default:
            chunk.Ascend();
            break;
        }
    }
    chunk.Ascend();
    return true;
}

// ---------------------------------------------------------------------------
// C_AmmoItem

void C_AmmoItem::Init(int typeId)
{
    C_Item::Init(typeId);
    // (The original calls through a null definition for ids outside the table.)
    m_maxRounds = m_rounds = m_def->GetAmmoAmount();
}

bool C_AmmoItem::SaveGame(C_chunk &chunk)
{
    C_Item::SaveGame(chunk);
    chunk.WriteChunk(CHUNK_AMMO_MAX_ROUNDS, m_maxRounds);
    chunk.WriteChunk(CHUNK_AMMO_ROUNDS, m_rounds);
    return true;
}

bool C_AmmoItem::LoadGame(C_chunk &chunk)
{
    C_Item::LoadGame(chunk);
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id == CHUNK_AMMO_MAX_ROUNDS)
            m_maxRounds = ReadValue<float>(chunk);
        else if (id == CHUNK_AMMO_ROUNDS)
            m_rounds = ReadValue<float>(chunk);
        else
            chunk.Ascend();
    }
    return true;
}

// ---------------------------------------------------------------------------
// C_ContainerItem

C_ContainerItem::C_ContainerItem() : C_Item(ITEM_KIND_CONTAINER), m_store(new C_ItemStore(2)) {}

C_ContainerItem::~C_ContainerItem()
{
    if (m_store)
        m_store->Clear();
    delete m_store;
    m_store = nullptr;
}

void C_ContainerItem::Init(int typeId)
{
    C_Item::Init(typeId);
    const C_BackpackComp *backpack = static_cast<const C_BackpackComp *>(m_def->GetComponent());
    m_store->SetMaxWeight(backpack->GetMaxWeight());
    m_store->SetMaxSpace(backpack->GetCapacity());
    m_store->SetMaxItemSize(backpack->GetMaxItemSize());
}

// ---------------------------------------------------------------------------
// C_MedkitItem

void C_MedkitItem::Init(int typeId)
{
    C_Item::Init(typeId);
    const C_ItemComponent *component = m_def->GetComponent();
    if (m_def->GetBehaviour() == ITEM_BEHAVIOUR_MEDKIT && component)
        m_charges = static_cast<const C_MedkitComp *>(component)->GetHealingCapacity();
    else
        m_charges = 0.0f;
}

void C_MedkitItem::Consume(float amount)
{
    m_charges -= amount;
    if (m_charges < 0.0f)
        m_charges = 0.0f;
}

bool C_MedkitItem::SaveGame(C_chunk &chunk)
{
    C_Item::SaveGame(chunk);
    chunk.WriteChunk(CHUNK_MEDKIT_CHARGES, m_charges);
    return true;
}

bool C_MedkitItem::LoadGame(C_chunk &chunk)
{
    C_Item::LoadGame(chunk);
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id != CHUNK_MEDKIT_CHARGES) {
            chunk.Ascend();
            return false;
        }
        m_charges = ReadValue<float>(chunk);
    }
    return true;
}

// ---------------------------------------------------------------------------
// C_WeaponItem

C_WeaponItem::C_WeaponItem() : C_Item(ITEM_KIND_WEAPON) {}

C_WeaponItem::~C_WeaponItem()
{
    ReleaseShotSoundFrames();
    if (m_projectileCopy)
        ReleaseFrameTree(m_projectileCopy);
    m_projectileCopy = nullptr;
}

void C_WeaponItem::Init(int typeId)
{
    C_Item::Init(typeId);
    m_attackTime = 0;
    m_flags = WEAPON_UNKNOWN_FLAG1;
    m_capacity = -1.0f;
    if (m_def->GetAmmoItemId() < 0) {
        if (m_def->GetBehaviour() == ITEM_BEHAVIOUR_LAUNCHER)
            m_capacity = 1.0f;
    } else {
        // (The original calls through a null definition for ammo ids outside the table.)
        m_capacity = GetItemDef(m_def->GetAmmoItemId())->GetAmmoAmount();
    }
    m_rounds = m_capacity;
    m_readyTime = 0;
    I3D_frame *model = GetFrame(0);
    m_flash = model ? model->FindChildFrame("flash", FRAME_MASK_VISUAL) : nullptr;
    if (m_flash)
        m_flash->SetOn(false);
    m_cartridgeDummy = nullptr;
    m_blastDummy = nullptr;
    m_shotDummy = nullptr;
    m_lastNoiseTime = 0;
    m_soundId = -1;
}

void C_WeaponItem::Use(float amount)
{
    m_rounds -= amount;
    if (m_rounds < 0.0f)
        m_rounds = 0.0f;
}

float C_WeaponItem::GetAmount()
{
    return Now() < m_readyTime ? 0.0f : m_rounds;
}

I3D_frame *C_WeaponItem::GetShotDummy()
{
    if (!m_shotDummy)
        m_shotDummy = GetFrame(0)->FindChildFrame("shdum", FRAME_MASK_DUMMY);
    return m_shotDummy;
}

I3D_frame *C_WeaponItem::GetBlastDummy()
{
    if (!m_blastDummy)
        m_blastDummy = GetFrame(0)->FindChildFrame("blsdum", FRAME_MASK_DUMMY);
    return m_blastDummy;
}

I3D_frame *C_WeaponItem::GetCartridgeDummy()           // (inline in 0x00451100)
{
    if (!m_cartridgeDummy)
        m_cartridgeDummy = GetFrame(0)->FindChildFrame("cardum", FRAME_MASK_DUMMY);
    return m_cartridgeDummy;
}

void C_WeaponItem::PlayReloadSound(C_Actor *owner, bool ambient)
{
    C_WeaponComp *comp = static_cast<C_WeaponComp *>(m_def->GetComponent());
    g_weaponSound.flags = ambient ? SOUND_AMBIENT : 0;
    g_weaponSound.attachTo = GetShotDummy();
    g_weaponSound.chapter = SOUND_CHAPTER_WEAPON_MANIPULATION;
    g_weaponSound.group = comp->GetReloadSound();
    g_weaponSound.source = owner;
    g_weaponSound.pos = S_vector(0.0f, 0.0f, 0.0f);
    m_soundId = GetSoundModule()->Play(g_weaponSound);
    if (!GetSoundModule()->GetSound(m_soundId))
        m_soundId = -1;
}

bool C_WeaponItem::Reload(C_Actor *owner, int container, int *ammoUid, bool keepLastMagazine)
{
    *ammoUid = -1;
    const bool infiniteAmmo = IsLocalGame() && g_itemsModule.IsInfiniteAmmo();
    C_WeaponComp *comp = static_cast<C_WeaponComp *>(m_def->GetComponent());
    uint32_t readyTime;
    if (!comp->CanReload() && m_rounds != 0.0f) {
        // Cannot be topped up: only a pending reload goes ahead (without new rounds).
        if (!(m_flags & WEAPON_RELOAD_PENDING))
            return false;
        readyTime = Now() + comp->GetReloadTimeMs();
    } else {
        if (!owner || Now() < m_readyTime || (owner->HasSingleContainer() && container != 0) ||
            container > ITEM_CONTAINER_BACKPACK_CONTENTS)
            return false;
        if (m_def->GetBehaviour() == ITEM_BEHAVIOUR_LAUNCHER && container == ITEM_CONTAINER_BELT &&
            !owner->HasSingleContainer() && owner->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS))
            container = ITEM_CONTAINER_BACKPACK_CONTENTS;
        C_ItemContainer *store = owner->GetContainer(container);
        if (!store || store->GetMaxSpace() <= 0.0f)
            return false;

        // The fullest and the emptiest magazine in the first slot of the weapon's ammo.
        C_Item *fullest = nullptr;
        C_Item *emptiest = nullptr;
        int magazines = 0;
        // (The original does not check for containers without slots.)
        const std::vector<C_Item *> *slots = store->GetSlots();
        for (int i = 0; slots && i < store->GetSlotCount(); ++i) {
            const std::vector<C_Item *> &slot = slots[i];
            if (slot.empty() || slot.front()->GetKind() != ITEM_KIND_AMMO ||
                slot.front()->GetTypeId() != m_def->GetAmmoItemId() || m_def->GetAmmoItemId() < 1)
                continue;
            fullest = emptiest = slot.front();
            for (C_Item *magazine : slot) {
                ++magazines;
                if (magazine->GetAmount() > fullest->GetAmount())
                    fullest = magazine;
                else if (magazine->GetAmount() < emptiest->GetAmount())
                    emptiest = magazine;
            }
            break;
        }
        if (!fullest && !infiniteAmmo) {
            if (!(m_flags & WEAPON_RELOAD_PENDING))
                return false;
            m_flags &= ~WEAPON_RELOAD_PENDING;
            return true;
        }

        if (infiniteAmmo || (magazines == 1 && keepLastMagazine)) {
            m_rounds = GetMaxAmount();
        } else {
            bool useUp = true;
            m_rounds += fullest->GetAmount();
            if (m_rounds > GetMaxAmount()) {
                const float rest = m_rounds - GetMaxAmount();
                m_rounds = GetMaxAmount();
                if (rest != 0.0f) {
                    if (emptiest->GetAmount() + rest > emptiest->GetMaxAmount() || emptiest == fullest) {
                        // The magazine keeps what did not fit.
                        static_cast<C_AmmoItem *>(fullest)->SetAmount(rest);
                        useUp = false;
                    } else {
                        static_cast<C_AmmoItem *>(emptiest)->SetAmount(emptiest->GetAmount() + rest);
                    }
                }
            }
            if (useUp) {
                *ammoUid = fullest->GetUid();
                g_itemsModule.DropItem(fullest, owner, container);
                g_itemsModule.DestroyItem(fullest);
            }
        }

        if (owner->GetType() == ACTOR_HUMAN && IsClient()) {
            PlayReloadSound(owner, IsOwnWeaponHeard(owner));
            static_cast<C_Human *>(owner)->SetInventoryChanged(true);
        }
        readyTime = Now() + comp->GetReloadTimeMs();
    }

    if (static_cast<int>(m_time) < static_cast<int>(readyTime))
        m_time = readyTime;
    if (static_cast<int>(m_readyTime) < static_cast<int>(readyTime))
        m_readyTime = readyTime;
    m_flags &= ~WEAPON_RELOAD_PENDING;
    return true;
}

void C_WeaponItem::SetRounds(C_Actor *owner, int ammoUid, int rounds)
{
    if (ammoUid >= 0)
        static_cast<C_Human *>(owner)->RemoveItem(ammoUid, true);
    m_rounds = static_cast<float>(rounds);
    m_flags &= ~WEAPON_RELOAD_PENDING;
    if (IsClient())
        PlayReloadSound(owner, false);
}

void C_WeaponItem::PlayEmptySound(C_Actor *owner)
{
    if (!IsClient())
        return;
    g_weaponSound.pos = S_vector(0.0f, 0.0f, 1.1f);
    g_weaponSound.chapter = SOUND_CHAPTER_WEAPON_MANIPULATION;
    g_weaponSound.group = SOUND_GROUP_EMPTY_CLICK;
    g_weaponSound.source = owner;
    g_weaponSound.attachTo = GetFrame(0);
    g_weaponSound.flags = IsOwnWeaponHeard(owner) ? SOUND_AMBIENT : 0;
    GetSoundModule()->Play(g_weaponSound);
}

void C_WeaponItem::StopReloadSound()
{
    if (m_readyTime != 0 && static_cast<int>(m_readyTime - Now()) > RELOAD_SOUND_GRACE && m_soundId != -1)
        GetSoundModule()->Stop(&m_soundId);
    m_soundId = -1;
}

int C_WeaponItem::Fire(C_Actor *shooter, const S_vector &pos, const S_vector &dir, uint32_t flags)
{
    ++m_shotCount;
    const E_ItemComponentKind kind = m_def->GetComponentKind();
    const bool quiet = (flags & 0xFF) != 0;
    if (m_def->GetBehaviour() == ITEM_BEHAVIOUR_THROWN) {
        g_shotManager.ThrowItem(shooter, this, pos, dir, quiet);
        return FIRE_OK;
    }

    if (kind == ICK_FIREARM) {
        // Submachine and machine guns fire a tracer every third round.
        const int carryMode = m_def->GetCarryMode();
        const bool tracer = (carryMode == ITEM_CARRY_SMG || carryMode == ITEM_CARRY_LMG) && m_shotCount % 3 == 0;
        g_shotManager.FireBullet(shooter, this, pos, dir, tracer, flags, m_def->GetShellSound());
        PlayShotSound(shooter, GetShotDummy(), quiet);
        PlayActionSounds(shooter);
        Use(1.0f);
        m_shotInterval = static_cast<int>(Now() - m_lastShotTime);
        m_lastShotTime = Now();
        return FIRE_OK;
    }

    if (kind != ICK_PROJECTILE_WEAPON) {
        if (kind == ICK_MELEE && IsServer())
            MeleeAttack(shooter, this, false, pos, dir);
        return FIRE_OK;
    }

    I3D_frame *blast = GetBlastDummy();
    if (blast) {
#ifdef HD2_DEDICATED   // no particles on the server; it still reads the matrix
        (void)blast->GetMatrix();
#else
        const int effect = static_cast<C_WeaponComp *>(m_def->GetComponent())->GetEffect();
        g_particlesModule.Create(effect, blast->GetMatrix(), blast, true, false);
#endif
    }
    g_shotManager.FireProjectile(shooter, this, pos, dir, GetProjectileFrame(), 0x5B, false, 0, flags);
    if (I3D_frame *projectile = GetProjectileFrame())
        projectile->SetOn(false);
    PlayShotSound(shooter, blast ? blast : GetFrame(0), quiet);
    m_shotInterval = static_cast<int>(Now() - m_lastShotTime);
    m_lastShotTime = Now();
    Use(1.0f);
    return FIRE_OK;
}

int C_WeaponItem::FireSecondary(C_Actor *shooter, const S_vector &pos, const S_vector &dir, uint8_t flags)
{
    if (m_def->GetBehaviour() == ITEM_BEHAVIOUR_THROWN)
        g_shotManager.ThrowItem(shooter, this, pos, dir, flags != 0);
    return FIRE_OK;
}

I3D_frame *C_WeaponItem::GetProjectileFrame()
{
    if (m_projectileKind == 1)
        FindProjectile();
    return m_projectile;
}

int C_WeaponItem::GetProjectileKind()
{
    if (m_projectileKind == 1)
        FindProjectile();
    return m_projectileKind;
}

void C_WeaponItem::FindProjectile()
{
    I3D_frame *model = GetFrame(0);
    m_projectile = nullptr;
    m_projectileCopy = nullptr;
    m_projectileKind = 0;
    if (!model)
        return;
    m_projectile = model->FindChildFrame("projectile", FIND_ANY_FRAME);
    if (!m_projectile)
        m_projectile = model->FindChildFrame("mag1", FIND_ANY_FRAME);
    if (m_projectile) {
        m_projectileKind = 2;
    } else if ((m_projectile = model->FindChildFrame("mag2", FIND_ANY_FRAME)) != nullptr) {
        m_projectileKind = 3;
    } else if ((m_projectile = model->FindChildFrame("mag3", FIND_ANY_FRAME)) != nullptr) {
        m_projectileKind = 4;
    }
    if (!m_projectile)
        return;
    CloneFrameTree(m_projectileCopy, m_projectile, nullptr);
    const S_vector &pos = m_projectile->GetPos();
    m_projectilePos = {pos.x, pos.y, pos.z, m_projectile->GetPosW()};
    m_projectileRot = m_projectile->GetRot();
}

void C_WeaponItem::AttachProjectile(I3D_frame *hand)
{
    if (m_projectileKind == 1)
        FindProjectile();
    if (!m_projectileCopy || !m_projectile)
        return;
    bool showOriginal;
    if (!hand) {
        // Back onto the weapon, hidden; the weapon's own projectile shows again.
        m_projectileCopy->LinkTo(GetFrame(0), 0);
        m_projectileCopy->SetPosInline(S_vector(m_projectilePos.x, m_projectilePos.y, m_projectilePos.z));
        m_projectileCopy->SetRot(m_projectileRot);
        m_projectileCopy->SetOn(false);
        showOriginal = true;
    } else {
        m_projectileCopy->LinkTo(hand, 0);
        m_projectileCopy->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
        m_projectileCopy->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
        m_projectileCopy->SetOn(true);
        showOriginal = false;
    }
    m_projectile->SetOn(showOriginal);
    m_projectileCopy->Update();
    m_projectile->Update();
}

bool C_WeaponItem::SaveGame(C_chunk &chunk)
{
    C_Item::SaveGame(chunk);
    chunk.WriteChunk(CHUNK_WEAPON_ROUNDS, m_rounds);
    chunk.WriteChunk(CHUNK_WEAPON_CAPACITY, m_capacity);
    chunk.WriteChunk(CHUNK_WEAPON_UNKNOWN58, m_attackTime);
    chunk.WriteChunk(CHUNK_WEAPON_READY_TIME, m_readyTime);
    chunk.WriteChunk(CHUNK_WEAPON_FLAGS, m_flags);
    chunk.WriteChunk(CHUNK_WEAPON_LAST_SHOT, m_lastShotTime);
    chunk.WriteChunk(CHUNK_WEAPON_SHOT_INTERVAL, m_shotInterval);
    chunk.WriteChunk(CHUNK_WEAPON_SHOT_COUNT, m_shotCount);
    chunk.WriteChunk(CHUNK_WEAPON_PROJECTILE_KIND, m_projectileKind);
    chunk.WriteChunk(CHUNK_WEAPON_UNKNOWN90, static_cast<int>(m_unknown90));
    chunk.WriteChunk(CHUNK_WEAPON_UNKNOWN94, m_unknown94);
    return true;
}

bool C_WeaponItem::LoadGame(C_chunk &chunk)
{
    C_Item::LoadGame(chunk);
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_WEAPON_ROUNDS: m_rounds = ReadValue<float>(chunk); break;
        case CHUNK_WEAPON_CAPACITY: m_capacity = ReadValue<float>(chunk); break;
        case CHUNK_WEAPON_UNKNOWN58: m_attackTime = ReadValue<uint32_t>(chunk); break;
        case CHUNK_WEAPON_READY_TIME: m_readyTime = ReadValue<uint32_t>(chunk); break;
        case CHUNK_WEAPON_FLAGS: m_flags = ReadValue<uint32_t>(chunk); break;
        case CHUNK_WEAPON_LAST_SHOT: m_lastShotTime = ReadValue<uint32_t>(chunk); break;
        case CHUNK_WEAPON_SHOT_INTERVAL: m_shotInterval = ReadValue<int>(chunk); break;
        case CHUNK_WEAPON_SHOT_COUNT: m_shotCount = ReadValue<int>(chunk); break;
        case CHUNK_WEAPON_PROJECTILE_KIND: m_projectileKind = ReadValue<int>(chunk); break;
        case CHUNK_WEAPON_UNKNOWN90: m_unknown90 = ReadValue<int>(chunk) != 0; break;
        case CHUNK_WEAPON_UNKNOWN94: m_unknown94 = ReadValue<int>(chunk); break;
        default: chunk.Ascend(); break;
        }
    }
    // An empty launcher does not show its round.
    if (m_def->GetBehaviour() == ITEM_BEHAVIOUR_LAUNCHER && m_rounds == 0.0f) {
        if (I3D_frame *round = GetFrame(0)->FindChildFrame("mag1", FIND_ANY_FRAME))
            round->SetOn(false);
    }
    return true;
}

// ---------------------------------------------------------------------------
// weapon sounds

void C_WeaponItem::PlayShotSound(C_Actor *shooter, I3D_frame *where, bool quiet)
{
    if (m_shotSoundCount == 0)
        return;
#ifndef HD2_DEDICATED   // no sounds on the server: only the AI's noise
    if (IsClient()) {
        if (I3D_frame *frame = GetShotSoundFrame(std::rand() % m_shotSoundCount)) {
            I3D_sound *sound = static_cast<I3D_sound *>(frame);
            sound->LinkTo(where, 0);
            sound->Play(0);
            // The player's own shots are non-positional; others sound a little in front.
            const bool ambient = IsOwnWeaponHeard(shooter);
            sound->SetSoundType(ambient ? I3DSOUND_AMBIENT : I3DSOUND_POINT);
            sound->SetPosInline(S_vector(0.0f, 0.0f, ambient ? 0.0f : 0.7f));
            sound->Update();
            sound->SetOn(true);
        }
    }
#endif
    if (IsServer() && !quiet && Now() - m_lastNoiseTime > SHOT_NOISE_INTERVAL) {
        m_lastNoiseTime = Now();
        C_WeaponComp *comp = static_cast<C_WeaponComp *>(m_def->GetComponent());
        g_weaponSound.chapter = SOUND_CHAPTER_WEAPON_SHOOTING;
        g_weaponSound.group = comp->GetShotSound();
        g_weaponSound.source = shooter;
        g_weaponSound.pos = S_vector(0.0f, 0.0f, 0.0f);
        g_weaponSound.attachTo = where;
        g_weaponSound.flags = SOUND_AI_ONLY | SOUND_AI_OCCLUSION;
        // Item types 7, 22 and 260 (presumably the silenced weapons) carry less far.
        const int id = m_def->GetId();
        g_weaponSound.farScale = id == 22 || id == 7 || id == 260 ? 0.3f : 1.0f;
        GetSoundModule()->Play(g_weaponSound);
    }
}

I3D_frame *C_WeaponItem::GetShotSoundFrame(int index)
{
    if (m_shotSounds[index])
        return m_shotSounds[index];
    C_WeaponComp *comp = static_cast<C_WeaponComp *>(m_def->GetComponent());
    const S_SoundGroup *group = GetSoundModule()->GetGroup(SOUND_CHAPTER_WEAPON_SHOOTING, comp->GetShotSound());
    if (!group)
        return nullptr;
    m_shotSoundCount = std::min(static_cast<int>(group->samples.size()), 2);
    for (int i = 0; i < m_shotSoundCount; ++i) {
        const S_SoundSample *sample = group->samples[i];
        I3D_sound *sound = static_cast<I3D_sound *>(I3DGetDriver()->CreateFrame(FRAME_SOUND));
        if (g_soundCache.OpenInto(sound, sample->fileName) != I3D_OK) {
            sound->Release();
            m_shotSounds[i] = nullptr;
            continue;
        }
        sound->SetSoundType(I3DSOUND_POINT);
        sound->SetCone(HALF_PI, HALF_PI);
        sound->SetVolume(sample->volume);
        sound->SetRange(sample->nearRange, sample->farRange, 0.4f, 0.3f);
        sound->LinkTo(GetFrame(0), 0);
        sound->SetPosInline(S_vector(0.0f, 0.0f, 1.1f));
        m_shotSounds[i] = sound;
    }
    // (Whatever `index` was asked for, the first sound is returned after loading.)
    return m_shotSounds[0];
}

void C_WeaponItem::ReleaseShotSoundFrames()
{
    for (int i = 2; i-- > 0;) {
        if (m_shotSounds[i]) {
            m_shotSounds[i]->Release();
            m_shotSounds[i] = nullptr;
        }
    }
}

void C_WeaponItem::PlayActionSounds(C_Actor *shooter)
{
    if (!IsClient())
        return;
    C_WeaponComp *comp = static_cast<C_WeaponComp *>(m_def->GetComponent());
    // At the shot dummy, non-positional, for the player's own weapon; otherwise a little
    // in front of the model. The AI does not notice these.
    auto place = [&]() {
        if (IsOwnWeaponHeard(shooter)) {
            g_weaponSound.pos = S_vector(0.0f, 0.0f, 0.0f);
            g_weaponSound.attachTo = GetShotDummy();
            g_weaponSound.flags = SOUND_AMBIENT | SOUND_SILENT_TO_AI;
        } else {
            g_weaponSound.pos = S_vector(0.0f, 0.0f, 0.7f);
            g_weaponSound.attachTo = GetFrame(0);
            g_weaponSound.flags = SOUND_SILENT_TO_AI;
        }
        g_weaponSound.source = nullptr;
    };
    if (GetAmount() == 1.0f && comp->GetLastShotSound() >= 0) {
        place();
        g_weaponSound.chapter = SOUND_CHAPTER_WEAPON_SHOOTING;
        g_weaponSound.group = comp->GetLastShotSound();
        GetSoundModule()->Play(g_weaponSound);
    }
    if (comp->GetBoltSound() < 0)
        return;
    place();
    g_weaponSound.chapter = SOUND_CHAPTER_WEAPON_MANIPULATION;
    g_weaponSound.group = comp->GetBoltSound();
    GetSoundModule()->Play(g_weaponSound);
}
