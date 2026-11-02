// The mission squad: the soldiers chosen for the next mission, what they carry and the limits
// the equipment menus keep (original source file 0x006a1f80..0x006a5a50; see profile.h).
#include "game/profile.h"

#include <cmath>
#include <cstring>

#include "common/chunk.h"
#include "game/game_internal.h"
#include "game/item_defs.h"
#include "game/items_module.h"

namespace {

// A squad soldier's chunks (the squad: chunk 10 per soldier).
enum : uint16_t {
    CHUNK_SQUAD_SOLDIER = 10,
    CHUNK_SOLDIER_PROFILE = 0x0B,
    CHUNK_SOLDIER_HEAD = 0x0C,
    CHUNK_SOLDIER_UNIFORM = 0x0D,
    CHUNK_SOLDIER_BACKPACK = 0x0E,
    CHUNK_SOLDIER_LONG_GUN = 0x0F,
    CHUNK_SOLDIER_HANDS = 0x10,
    CHUNK_SOLDIER_BELT_ITEM = 0x11,
    CHUNK_SOLDIER_BACKPACK_ITEM = 0x12,
    CHUNK_SOLDIER_LOAD_OLD = 0x13,                      // the capacity as a float
    CHUNK_SOLDIER_TEAM = 0x14,
    CHUNK_SOLDIER_LOAD = 0x15,
    CHUNK_SOLDIER_DEPLOYED = 0x16,
    CHUNK_SOLDIER_SPEECH_SET = 0x17,
};

// The belt's volume.
constexpr float BELT_VOLUME = 12.0f;                    // 0x00863868

// What a multiplayer soldier's stats are without a profile.
constexpr int DEFAULT_STATS[STAT_COUNT] = {800, 80, 80, 80, 80, 80, 50, 30, 20, 50, 0, 80};

// The volume of one item: twice its inventory size, 1 when that is 0.
int GetItemVolume(const C_ItemDef *def)
{
    const float size = static_cast<float>(def->GetInventorySize());
    return static_cast<int>(size == 0.0f ? 1.0f : size + size);
}

// The weight of a slot's item, 0 for none.
float GetSlotWeight(const S_SoldierItem &item)
{
    if (item.typeId < 0)
        return 0.0f;
    // Fix: an unknown item type was dereferenced as a null definition.
    const C_ItemDef *def = GetItemDef(item.typeId);
    return def ? def->GetWeight() : 0.0f;
}

// The weight and the volume of an item stack; false for none.
bool GetItemLoad(const S_SoldierItem &item, int *volume, float *weight)   // 0x006a3090
{
    *volume = 0;
    *weight = 0.0f;
    if (item.typeId < 0)
        return false;
    const C_ItemDef *def = GetItemDef(item.typeId);
    if (!def)
        return false;
    *weight = def->GetWeight() * static_cast<float>(item.present);
    const float size = static_cast<float>(def->GetInventorySize());
    const float pieceVolume = size == 0.0f ? 1.0f : size + size;
    *volume = static_cast<int>(static_cast<float>(item.present) * pieceVolume);
    return true;
}

// The backpack container of an item definition: its weight and volume limits (0 for none).
void GetBackpackLimits(const C_ItemDef *def, float *maxWeight, int *maxVolume)
{
    *maxWeight = 0.0f;
    *maxVolume = 0;
    const C_ItemComponent *component = def->GetComponent();
    if (component && component->GetKind() == ICK_BACKPACK) {
        const C_BackpackComp *backpack = static_cast<const C_BackpackComp *>(component);
        *maxWeight = backpack->GetMaxWeight();
        *maxVolume = static_cast<int>(backpack->GetCapacity());
    }
}

bool SameType(const S_SoldierItem &a, const S_SoldierItem &b)   // 0x006a3c50
{
    return a.typeId == b.typeId;
}

// Takes up to request.present from `item` (request.present becomes what was taken); an
// emptied item is cleared. Returns the count taken.
int TakeCount(S_SoldierItem &item, S_SoldierItem &request)      // 0x006a3c60
{
    if (item.present < request.present)
        request.present = item.present;
    item.present = static_cast<int16_t>(item.present - request.present);
    if (item.present == 0) {
        item.uid = -1;
        item.typeId = -1;
        item.present = 0;
    }
    return request.present;
}

// The first stack of an item type, -1 if none.
int FindStack(const std::vector<S_SoldierItem> &items, int16_t typeId)
{
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].typeId == typeId)
            return static_cast<int>(i);
    }
    return -1;
}

void MergeStacks(std::vector<S_SoldierItem> &items)
{
    for (size_t i = 0; i < items.size(); ++i) {
        // (From the last item down to the next one; erasing does not move those still to come.)
        int j = static_cast<int>(items.size()) - 1;
        for (int n = j - static_cast<int>(i); n > 0; --n, --j) {
            if (items[i].typeId == items[j].typeId) {
                items[i].present = static_cast<int16_t>(items[i].present + items[j].present);
                items.erase(items.begin() + j);
            }
        }
    }
}

void SplitStacks(std::vector<S_SoldierItem> &items)
{
    for (size_t k = items.size(); k-- > 0;) {
        while (items[k].present > 1) {
            S_SoldierItem single;
            single.uid = -1;
            single.typeId = items[k].typeId;
            single.present = 1;
            items.push_back(single);
            --items[k].present;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// S_SoldierItem

bool S_SoldierItem::WriteChunk(int id, C_chunk &chunk) const   // 0x006a4fd0
{
    chunk.Begin(static_cast<uint16_t>(id));
    chunk.Write(this, sizeof(*this));
    chunk.Ascend();
    return true;
}

bool S_SoldierItem::ReadChunk(C_chunk &chunk)           // 0x006a50d0
{
    if (!chunk.IsOpen())
        return false;
    chunk.Read(this, sizeof(*this));
    chunk.Ascend();
    return true;
}

// ---------------------------------------------------------------------------
// S_SoldierLoad

float S_SoldierLoad::GetBeltFill() const                // 0x006a25e0
{
    return std::fabs(static_cast<float>(beltVolume) / BELT_VOLUME);
}

float S_SoldierLoad::GetBackpackFill() const            // 0x006a25f0
{
    return std::fabs(static_cast<float>(backpackVolume) / static_cast<float>(backpackMaxVolume));
}

float S_SoldierLoad::GetWeightFill() const              // 0x006a2600
{
    return std::fabs(weight / static_cast<float>(capacity));
}

// ---------------------------------------------------------------------------
// S_SquadSoldier

bool S_SquadSoldier::SetProfile(int index, int level)   // 0x006a2610
{
    // Fix: the original only tested the range for negative indexes (where the test cannot
    // fail) and went on with a missing profile.
    const S_SoldierProfile *profile = GetSoldierProfile(index);
    if (!profile)
        return false;
    profileIndex = index;
    name = profile->customName.IsEmpty() ? profile->fullName : profile->customName;
    if (IsLocalGame()) {
        load.capacity = static_cast<int>(profile->stats[STAT_STRENGTH]);
        return true;
    }
    int stats[STAT_COUNT];
    std::memcpy(stats, DEFAULT_STATS, sizeof(stats));
    g_gameProfile.m_humanStore->GetStats(stats, index, level);
    load.capacity = stats[STAT_STRENGTH];
    return true;
}

void S_SquadSoldier::CopyFrom(const S_SquadSoldier &other)   // 0x006a2740
{
    vip = other.vip;
    team = other.team;
    spawnZone = other.spawnZone;
    spectating = other.spectating;
    name = other.name;
    profileIndex = other.profileIndex;
    head = other.head;
    uniform = other.uniform;
    backpack = other.backpack;
    longGun = other.longGun;
    hands = other.hands;
    belt = other.belt;
    backpackContents = other.backpackContents;
    load = other.load;
}

void S_SquadSoldier::ClearItems()                       // 0x006a2a90
{
    head = S_SoldierItem();
    uniform = S_SoldierItem();
    backpack = S_SoldierItem();
    longGun = S_SoldierItem();
    hands = S_SoldierItem();
    belt.clear();
    backpackContents.clear();
    const int capacity = load.capacity;
    load = S_SoldierLoad();
    load.capacity = capacity;
}

void S_SquadSoldier::ReturnItems()                      // 0x006a2bf0
{
    ReturnItemsTo(g_gameProfile.m_itemStock1C);
}

void S_SquadSoldier::ReturnItemsTo(C_ItemStock *stock)  // 0x006a2c10
{
    if (stock) {
        const S_SoldierItem *const slots[] = {&head, &uniform, &backpack, &longGun, &hands};
        for (const S_SoldierItem *item : slots)
            stock->Add(item->typeId, item->present);
        while (!belt.empty()) {
            stock->Add(belt.back().typeId, belt.back().present);
            belt.pop_back();
        }
        while (!backpackContents.empty()) {
            stock->Add(backpackContents.back().typeId, backpackContents.back().present);
            backpackContents.pop_back();
        }
    }
    ClearItems();
}

void S_SquadSoldier::MergeItems()                       // 0x006a2d60
{
    MergeStacks(belt);
    MergeStacks(backpackContents);
}

void S_SquadSoldier::SplitStacks()                      // 0x006a2f60
{
    ::SplitStacks(belt);
    ::SplitStacks(backpackContents);
}

void S_SquadSoldier::UpdateLoad()                       // 0x006a3120
{
    load = S_SoldierLoad();
    if (profileIndex < 0)
        return;
    SetProfile(profileIndex, 0);
    int volume = 0;
    float weight = 0.0f;
    const S_SoldierItem *const slots[] = {&head, &uniform, &backpack, &longGun, &hands};
    for (const S_SoldierItem *item : slots) {
        GetItemLoad(*item, &volume, &weight);
        load.weight += weight;
    }
    for (size_t i = belt.size(); i-- > 0;) {
        GetItemLoad(belt[i], &volume, &weight);
        load.beltVolume += volume;
        load.beltWeight += weight;
        load.weight += weight;
    }
    for (size_t i = backpackContents.size(); i-- > 0;) {
        GetItemLoad(backpackContents[i], &volume, &weight);
        load.backpackVolume += volume;
        load.backpackWeight += weight;
        load.weight += weight;
    }
    const C_ItemDef *def = GetItemDef(backpack.typeId);
    if (def) {
        const C_ItemComponent *component = def->GetComponent();
        if (component && component->GetKind() == ICK_BACKPACK)
            GetBackpackLimits(def, &load.backpackMaxWeight, &load.backpackMaxVolume);
    }
}

void S_SquadSoldier::PutItem(S_SoldierItem *displaced, uint8_t slot, const S_SoldierItem &item)   // 0x006a3350
{
    g_itemsModule.EnsureItemsLoaded();
    const C_ItemDef *def = GetItemDef(item.typeId);
    if (!def) {
        *displaced = item;
        return;
    }
    const float weight = def->GetWeight();
    const int volume = GetItemVolume(def);
    const float capacity = static_cast<float>(load.capacity);

    // A slot of one item: swapped if the weight allows.
    auto swap = [&](S_SoldierItem &slotItem) {
        const float oldWeight = GetSlotWeight(slotItem);
        if (weight + load.weight - oldWeight > capacity)
            return false;
        load.weight += weight - oldWeight;
        const S_SoldierItem old = slotItem;
        slotItem = item;
        *displaced = old;
        return true;
    };

    switch (slot) {
    case SQUAD_SLOT_HEAD:
        if (swap(head))
            return;
        break;
    case SQUAD_SLOT_UNIFORM:
        if (swap(uniform))
            return;
        break;
    case SQUAD_SLOT_HANDS:
        if (swap(hands))
            return;
        break;
    case SQUAD_SLOT_LONG_GUN:
        if (swap(longGun))
            return;
        break;
    case SQUAD_SLOT_BACKPACK: {
        // The contents must fit the new backpack.
        float maxWeight;
        int maxVolume;
        GetBackpackLimits(def, &maxWeight, &maxVolume);
        const float oldWeight = GetSlotWeight(backpack);
        if (weight + load.weight - oldWeight <= capacity && load.backpackWeight <= maxWeight &&
            load.backpackVolume <= maxVolume) {
            load.weight += weight - oldWeight;
            const S_SoldierItem old = backpack;
            backpack = item;
            *displaced = old;
            load.backpackMaxWeight = maxWeight;
            load.backpackMaxVolume = maxVolume;
            return;
        }
        break;
    }
    case SQUAD_SLOT_BELT:
    case SQUAD_SLOT_BACKPACK_CONTENTS: {
        // One at a time onto the item's stack (a new one at the end) while the limits allow.
        const bool toBelt = slot == SQUAD_SLOT_BELT;
        std::vector<S_SoldierItem> &items = toBelt ? belt : backpackContents;
        int stack = FindStack(items, item.typeId);
        S_SoldierItem left = item;
        int count = item.present;
        while (count > 0) {
            if (toBelt) {
                if (static_cast<float>(load.beltVolume + volume) > BELT_VOLUME)
                    break;
            } else if (weight + load.backpackWeight > load.backpackMaxWeight ||
                       load.backpackVolume + volume > load.backpackMaxVolume) {
                break;
            }
            if (weight + load.weight > capacity)
                break;
            if (stack < 0) {
                S_SoldierItem single;
                single.uid = -1;
                single.typeId = item.typeId;
                single.present = 1;
                items.push_back(single);
                stack = static_cast<int>(items.size()) - 1;
            } else {
                ++items[stack].present;
            }
            --count;
            if (toBelt) {
                load.beltWeight += weight;
                load.beltVolume += volume;
            } else {
                load.backpackWeight += weight;
                load.backpackVolume += volume;
            }
            load.weight += weight;
        }
        if (count == 0)
            left.typeId = -1;
        left.present = static_cast<int16_t>(count);
        *displaced = left;
        return;
    }
    default:
        break;
    }
    *displaced = item;
}

int S_SquadSoldier::CountItems(uint8_t slot, const S_SoldierItem &item) const   // 0x006a3cb0
{
    switch (slot) {
    case SQUAD_SLOT_HEAD: return head.typeId == item.typeId ? head.present : 0;
    case SQUAD_SLOT_UNIFORM: return uniform.typeId == item.typeId ? uniform.present : 0;
    case SQUAD_SLOT_HANDS: return hands.typeId == item.typeId ? hands.present : 0;
    case SQUAD_SLOT_BACKPACK: return backpack.typeId == item.typeId ? backpack.present : 0;
    case SQUAD_SLOT_LONG_GUN: return longGun.typeId == item.typeId ? longGun.present : 0;
    case SQUAD_SLOT_BELT: {
        const int stack = FindStack(belt, item.typeId);
        return stack >= 0 ? belt[stack].present : 0;
    }
    case SQUAD_SLOT_BACKPACK_CONTENTS: {
        const int stack = FindStack(backpackContents, item.typeId);
        return stack >= 0 ? backpackContents[stack].present : 0;
    }
    default:
        return 0;
    }
}

int S_SquadSoldier::RemoveItem(uint8_t slot, S_SoldierItem &item)   // 0x006a3e90
{
    g_itemsModule.EnsureItemsLoaded();
    const C_ItemDef *def = GetItemDef(item.typeId);
    if (!def)
        return -1;
    const float weight = def->GetWeight();
    const int volume = GetItemVolume(def);
    int removed = 0;
    switch (slot) {
    case SQUAD_SLOT_HEAD:
    case SQUAD_SLOT_UNIFORM:
    case SQUAD_SLOT_HANDS:
    case SQUAD_SLOT_LONG_GUN: {
        S_SoldierItem &slotItem = slot == SQUAD_SLOT_HEAD      ? head
                                  : slot == SQUAD_SLOT_UNIFORM ? uniform
                                  : slot == SQUAD_SLOT_HANDS   ? hands
                                                               : longGun;
        if (!SameType(slotItem, item))
            return 0;
        removed = TakeCount(slotItem, item);
        break;
    }
    case SQUAD_SLOT_BACKPACK:
        // Only an empty backpack comes off.
        if (!SameType(backpack, item) || !backpackContents.empty())
            return 0;
        removed = TakeCount(backpack, item);
        load.backpackMaxWeight = 0.0f;
        load.backpackMaxVolume = 0;
        break;
    case SQUAD_SLOT_BELT:
    case SQUAD_SLOT_BACKPACK_CONTENTS: {
        const bool fromBelt = slot == SQUAD_SLOT_BELT;
        std::vector<S_SoldierItem> &items = fromBelt ? belt : backpackContents;
        const int stack = FindStack(items, item.typeId);
        if (stack >= 0) {
            removed = TakeCount(items[stack], item);
            if (items[stack].present == 0)
                items.erase(items.begin() + stack);
            if (removed != 0) {
                if (fromBelt) {
                    load.beltWeight -= static_cast<float>(item.present) * weight;
                    load.beltVolume -= item.present * volume;
                } else {
                    load.backpackWeight -= static_cast<float>(item.present) * weight;
                    load.backpackVolume -= item.present * volume;
                }
            }
        }
        if (items.empty()) {
            if (fromBelt) {
                load.beltVolume = 0;
                load.beltWeight = 0.0f;
            } else {
                load.backpackVolume = 0;
                load.backpackWeight = 0.0f;
            }
        }
        break;
    }
    default:
        return 0;
    }
    if (removed != 0) {
        if (IsEmpty()) {
            const int capacity = load.capacity;
            load = S_SoldierLoad();
            load.capacity = capacity;
            return removed;
        }
        load.weight = std::fabs(load.weight - static_cast<float>(item.present) * weight);
    }
    return removed;
}

bool S_SquadSoldier::IsEmpty() const                    // 0x006a42e0
{
    return head.typeId == -1 && uniform.typeId == -1 && backpack.typeId == -1 && longGun.typeId == -1 &&
           hands.typeId == -1 && belt.empty() && backpackContents.empty();
}

int S_SquadSoldier::GetFreeBeltVolume() const           // 0x006a2bb0
{
    return static_cast<int>(BELT_VOLUME - static_cast<float>(load.beltVolume));
}

int S_SquadSoldier::GetFreeBackpackVolume() const       // 0x006a2bd0
{
    return load.backpackMaxVolume - load.backpackVolume;
}

float S_SquadSoldier::GetFreeWeight() const             // 0x006a2be0
{
    return static_cast<float>(load.capacity) - load.weight;
}

bool S_SquadSoldier::SaveGame(C_chunk &chunk)           // 0x006a49a0
{
    chunk.WriteChunk(CHUNK_SOLDIER_PROFILE, profileIndex);
    chunk.WriteChunk(CHUNK_SOLDIER_TEAM, team);
    head.WriteChunk(CHUNK_SOLDIER_HEAD, chunk);
    uniform.WriteChunk(CHUNK_SOLDIER_UNIFORM, chunk);
    backpack.WriteChunk(CHUNK_SOLDIER_BACKPACK, chunk);
    longGun.WriteChunk(CHUNK_SOLDIER_LONG_GUN, chunk);
    hands.WriteChunk(CHUNK_SOLDIER_HANDS, chunk);
    for (const S_SoldierItem &item : belt)
        item.WriteChunk(CHUNK_SOLDIER_BELT_ITEM, chunk);
    for (const S_SoldierItem &item : backpackContents)
        item.WriteChunk(CHUNK_SOLDIER_BACKPACK_ITEM, chunk);
    chunk.WriteChunk(CHUNK_SOLDIER_LOAD, &load, sizeof(load));
    chunk.WriteChunk(CHUNK_SOLDIER_DEPLOYED, &deployed, 1);
    chunk.WriteChunk(CHUNK_SOLDIER_SPEECH_SET, speechSet);
    return true;
}

bool S_SquadSoldier::LoadGame(C_chunk &chunk)           // 0x006a4c10
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id;
        if (!chunk.Descend(id))
            return false;
        switch (id) {
        case CHUNK_SOLDIER_PROFILE: {
            int index = -1;
            chunk.Read(index);
            chunk.Ascend();
            SetProfile(index, 0);
            break;
        }
        case CHUNK_SOLDIER_HEAD: head.ReadChunk(chunk); break;
        case CHUNK_SOLDIER_UNIFORM: uniform.ReadChunk(chunk); break;
        case CHUNK_SOLDIER_BACKPACK: backpack.ReadChunk(chunk); break;
        case CHUNK_SOLDIER_LONG_GUN: longGun.ReadChunk(chunk); break;
        case CHUNK_SOLDIER_HANDS: hands.ReadChunk(chunk); break;
        case CHUNK_SOLDIER_BELT_ITEM:
            belt.push_back(S_SoldierItem());
            belt.back().ReadChunk(chunk);
            break;
        case CHUNK_SOLDIER_BACKPACK_ITEM:
            backpackContents.push_back(S_SoldierItem());
            backpackContents.back().ReadChunk(chunk);
            break;
        case CHUNK_SOLDIER_LOAD_OLD: {
            chunk.Read(&load, sizeof(load));
            // (The old load kept the capacity as a float.)
            float capacity;
            std::memcpy(&capacity, &load.capacity, sizeof(capacity));
            load.capacity = static_cast<int>(capacity);
            chunk.Ascend();
            break;
        }
        case CHUNK_SOLDIER_TEAM:
            chunk.Read(team);
            chunk.Ascend();
            break;
        case CHUNK_SOLDIER_LOAD:
            chunk.Read(&load, sizeof(load));
            chunk.Ascend();
            break;
        case CHUNK_SOLDIER_DEPLOYED:
            chunk.Read(&deployed, 1);
            chunk.Ascend();
            break;
        case CHUNK_SOLDIER_SPEECH_SET:
            chunk.Read(speechSet);
            chunk.Ascend();
            break;
        default:
            // (The chunk is not left.)
            return false;
        }
    }
    return true;
}

void ClearSoldierItems(S_SquadSoldier *soldier)         // 0x006a2a90
{
    soldier->ClearItems();
}

void MergeSoldierItems(S_SquadSoldier *soldier)         // 0x006a2d60
{
    soldier->MergeItems();
}

void UpdateSoldierLoad(S_SquadSoldier *soldier)         // 0x006a3120
{
    soldier->UpdateLoad();
}

// ---------------------------------------------------------------------------
// S_MissionSquad

S_MissionSquad::~S_MissionSquad()                       // 0x006b61a0
{
    Clear();
}

void S_MissionSquad::Clear()
{
    while (!states.empty()) {
        delete states.back();
        states.pop_back();
    }
}

S_SquadSoldier *S_MissionSquad::AddSoldier()            // 0x00673cf0
{
    S_SquadSoldier *soldier = new S_SquadSoldier();
    soldier->spawnZone = 0x3FF;
    soldier->progress = S_SoldierProgress();
    soldier->progress.unknown04 = 0.0f;
    soldier->progress.morale = 0.0f;
    soldier->unknown34 = -1;
    soldier->unknown38 = -1;
    soldier->profileIndex = -1;
    soldier->team = -1;
    soldier->deployed = true;
    soldier->speechSet = -1;
    states.push_back(soldier);
    return states.back();
}

bool S_MissionSquad::SaveGame(C_chunk &chunk)           // 0x006a22f0
{
    for (S_SquadSoldier *soldier : states) {
        chunk.Begin(CHUNK_SQUAD_SOLDIER);
        soldier->SaveGame(chunk);
        chunk.Ascend();
    }
    return true;
}

bool S_MissionSquad::LoadGame(C_chunk &chunk)           // 0x006a2420
{
    Clear();
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0;
        chunk.Descend(id);
        if (id == CHUNK_SQUAD_SOLDIER) {
            if (S_SquadSoldier *soldier = AddSoldier())
                soldier->LoadGame(chunk);
        }
        if (!chunk.IsOpen())
            break;
        chunk.Ascend();
    }
    return true;
}
