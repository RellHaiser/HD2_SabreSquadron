// The equipment menus' loadout presets and missions' items (original 0x0067a3c0..0x0067e850).
#include "game/loadout_presets.h"

#include <algorithm>
#include <cstring>

#include "common/c_str.h"
#include "game/campaign.h"
#include "game/profile.h"
#include "ls3d/rw_data.h"

namespace {

const char *const PRESETS_FILE = "GameData\\presets.def";
const char *const USER_PRESETS_FILE = "user_presets.def";
const char *const MISSION_ITEMS_FILE = "GameData\\miss_itm.def";
// The record sizes of presets.def's versions (2 and later: the whole record).
constexpr int PRESET_SIZE_V0 = 0x1B0;
constexpr int PRESET_SIZE_V1 = 0x1B4;
constexpr int PRESET_SIZE = 0x1B8;
constexpr int USER_PRESETS_VERSION = 3;
// The user presets: four rows (the games) of six.
constexpr int USER_PRESET_ROWS = 4;
constexpr int USER_PRESETS_PER_ROW = 6;
constexpr int USER_PRESET_COUNT = USER_PRESET_ROWS * USER_PRESETS_PER_ROW;
constexpr std::size_t PRESET_NAME_LENGTH = 0x1D;

// The default user presets: their names, games and texts (0x1982..).
struct S_DefaultPreset {
    const char *name;
    uint32_t flags;
    int textId;
};
const S_DefaultPreset DEFAULT_PRESETS[USER_PRESET_COUNT] = {
    {"Axis user preset1", LOADOUT_AXIS, 0x1982},          {"Axis user preset2", LOADOUT_AXIS, 0x1983},
    {"Axis user preset3", LOADOUT_AXIS, 0x1984},          {"Axis user preset4", LOADOUT_AXIS, 0x1985},
    {"Axis user preset5", LOADOUT_AXIS, 0x1986},          {"Axis user preset6", LOADOUT_AXIS, 0x1987},
    {"Allied user preset1", LOADOUT_ALLIED, 0x1988},      {"Allied user preset2", LOADOUT_ALLIED, 0x1989},
    {"Allied user preset3", LOADOUT_ALLIED, 0x198A},      {"Allied user preset4", LOADOUT_ALLIED, 0x198B},
    {"Allied user preset5", LOADOUT_ALLIED, 0x198C},      {"Allied user preset6", LOADOUT_ALLIED, 0x198D},
    {"Deadmatch user preset1", LOADOUT_DEATHMATCH, 0x198E}, {"Deadmatch user preset2", LOADOUT_DEATHMATCH, 0x198F},
    {"Deadmatch user preset3", LOADOUT_DEATHMATCH, 0x1990}, {"Deadmatch user preset4", LOADOUT_DEATHMATCH, 0x1991},
    {"Deadmatch user preset5", LOADOUT_DEATHMATCH, 0x1992}, {"Deadmatch user preset6", LOADOUT_DEATHMATCH, 0x1993},
    {"User preset1", LOADOUT_USER, -1},                   {"User preset2", LOADOUT_USER, -1},
    {"User preset3", LOADOUT_USER, -1},                   {"User preset4", LOADOUT_USER, -1},
    {"User preset5", LOADOUT_USER, -1},                   {"User preset6", LOADOUT_USER, -1},
};

int PresetSize(int version)
{
    return version == 0 ? PRESET_SIZE_V0 : version == 1 ? PRESET_SIZE_V1 : PRESET_SIZE;
}

C_str UserPresetsPath()
{
    C_str path = g_gameProfile.GetProfileDirectory();
    path += USER_PRESETS_FILE;
    return path;
}

void DeleteAll(std::vector<S_LoadoutPreset *> &presets)
{
    while (!presets.empty()) {
        delete presets.back();
        presets.pop_back();
    }
}

S_LoadoutPreset *Find(const std::vector<S_LoadoutPreset *> &presets, int index, uint32_t flags)
{
    int found = 0;
    for (S_LoadoutPreset *preset : presets) {
        if ((preset->flags & flags) != flags)
            continue;
        if (found == index)
            return preset;
        ++found;
    }
    return nullptr;
}

// The count of the first entry of `typeId` among `count` entries.
int FirstCount(const S_PresetItem *items, int count, int typeId)
{
    for (int i = 0; i < count; ++i) {
        if (items[i].typeId == typeId)
            return items[i].count;
    }
    return 0;
}

} // namespace

// ---- the presets

void S_LoadoutPreset::ClearPlaces()
{
    head = uniform = backpack = longGun = hands = -1;
    beltCount = 0;
    backpackCount = 0;
}

int S_LoadoutPreset::CountItems(int typeId) const
{
    int count = (head == typeId) + (uniform == typeId) + (backpack == typeId) + (longGun == typeId) + (hands == typeId);
    count += FirstCount(belt, beltCount, typeId);
    count += FirstCount(backpackItems, backpackCount, typeId);
    return count;
}

void C_LoadoutPresets::Clear()
{
    DeleteAll(m_presets);
    DeleteAll(m_userPresets);
}

S_LoadoutPreset *C_LoadoutPresets::AddPreset()
{
    m_presets.push_back(new S_LoadoutPreset());
    return m_presets.back();
}

S_LoadoutPreset *C_LoadoutPresets::AddUserPreset()
{
    m_userPresets.push_back(new S_LoadoutPreset());
    return m_userPresets.back();
}

bool C_LoadoutPresets::Load()
{
    const int file = dtaOpen(PRESETS_FILE, false);
    if (file < 0)
        return false;
    Clear();
    int version = 0, count = 0;
    dtaRead(file, &version, 4);
    dtaRead(file, &count, 4);
    for (int i = 0; i < count; ++i)
        dtaRead(file, AddPreset(), PresetSize(version));
    dtaClose(file);

    // The user's: up to 24, the rows of the file's count / 4 into rows of six.
    const C_str path = UserPresetsPath();
    const int user = dtaOpen(path.c_str(), false);
    if (user < 0) {
        SaveUser();
        return false;
    }
    dtaRead(user, &version, 4);
    dtaRead(user, &count, 4);
    count = std::min(count, USER_PRESET_COUNT);
    // Fix: the original divided by zero for fewer than four (now one a row).
    const int perRow = std::max(count / USER_PRESET_ROWS, 1);
    CreateUserPresets();
    for (int i = 0; i < count; ++i) {
        const int at = i % perRow + i / perRow * USER_PRESETS_PER_ROW;
        dtaRead(user, m_userPresets[static_cast<std::size_t>(at)], PresetSize(version));
    }
    dtaClose(user);
    return true;
}

bool C_LoadoutPresets::SaveUser()
{
    const C_str path = UserPresetsPath();
    const int file = dtaOpenWrite(path.c_str(), DTA_WRITE_CREATE_ALWAYS);
    // (The original asks for the current directory here too and leaves it.)
    if (file < 0)
        return false;
    if (m_userPresets.empty())
        CreateUserPresets();
    int version = USER_PRESETS_VERSION;
    dtaWrite(file, &version, 4);
    int count = static_cast<int>(m_userPresets.size());
    dtaWrite(file, &count, 4);
    for (int i = 0; i < count; ++i)
        dtaWrite(file, m_userPresets[static_cast<std::size_t>(i)], PRESET_SIZE);
    dtaClose(file);
    return true;
}

void C_LoadoutPresets::CreateUserPresets()
{
    for (int i = 0; i < USER_PRESET_COUNT; ++i)
        AddUserPreset();
    // (The first 24: the ones just added when the list was empty.)
    for (int i = 0; i < USER_PRESET_COUNT; ++i) {
        S_LoadoutPreset *preset = m_userPresets[static_cast<std::size_t>(i)];
        std::strncpy(preset->name, DEFAULT_PRESETS[i].name, PRESET_NAME_LENGTH);
        preset->name[PRESET_NAME_LENGTH] = '\0';
        preset->flags = DEFAULT_PRESETS[i].flags;
        if (DEFAULT_PRESETS[i].textId != -1)
            preset->textId = DEFAULT_PRESETS[i].textId;
    }
}

S_LoadoutPreset *C_LoadoutPresets::GetPreset(int index, uint32_t flags) const
{
    return Find(m_presets, index, flags);
}

S_LoadoutPreset *C_LoadoutPresets::GetUserPreset(int index, uint32_t flags) const
{
    return Find(m_userPresets, index, flags);
}

// ---- the missions' items

C_MissionItems::C_MissionItems()
{
    std::fill(std::begin(m_first), std::end(m_first), -1);
}

void C_MissionItems::Release()
{
    for (S_MissionItems *&record : m_records) {
        delete record;
        record = nullptr;
    }
}

int S_MissionItems::GetRequiredCount(int type) const
{
    for (const std::pair<int, int> &item : required) {
        if (item.first == type)
            return item.second;
    }
    return 0;
}

S_MissionItems *C_MissionItems::GetMissionRecord(int mission) const
{
    const int index = mission + m_campaign * CAMPAIGN_STRIDE;
    // (Fix: a negative index none; the original read before the records.)
    return index >= 0 && index < MAX_RECORDS ? m_records[index] : nullptr;
}

void C_MissionItems::Setup()
{
    C_Campaign *campaign = g_gameProfile.m_campaign;
    m_campaign = campaign->GetCurrentIndex();
    const int operations = campaign->CountOperations(m_campaign);
    int index = m_campaign * CAMPAIGN_STRIDE;
    int operation = 0;
    for (; operation < operations; ++operation) {
        m_first[operation + m_campaign * CAMPAIGN_STRIDE] = index;
        const int missions = campaign->CountOperationMissions(m_campaign, operation);
        // (A record already there is replaced, not deleted: leaked, as the original.)
        for (int i = 0; i < missions; ++i)
            m_records[index++] = new S_MissionItems();
    }
    m_first[operation + m_campaign * CAMPAIGN_STRIDE] = index;
    Load();
}

bool C_MissionItems::Load()
{
    const int file = dtaOpen(MISSION_ITEMS_FILE, false);
    if (file < 0)
        return false;
    int version = 0, unused = 0;
    dtaRead(file, &version, 4);
    dtaRead(file, &unused, 4);
    // (A header the game skips.)
    uint8_t header[200];
    dtaRead(file, header, version == 1 ? 0x3C : 200);
    int count = 0;
    dtaRead(file, &count, 4);
    // Fix: the original read past the records for more than 120.
    count = std::min(count, MAX_RECORDS);
    for (int i = 0; i < count; ++i) {
        S_MissionItems *&record = m_records[i];
        int present = 0;
        dtaRead(file, &present, 4);
        if (present == 0) {
            if (record) {
                record->required.clear();
                for (S_MissionItems::S_Slot &slot : record->slots)
                    slot = S_MissionItems::S_Slot();
            }
            continue;
        }
        if (!record)
            record = new S_MissionItems();
        dtaRead(file, &record->kind, 4);
        int slots = 0;
        dtaRead(file, &slots, 4);
        // Fix: the original read past the ten slots for more.
        dtaRead(file, record->slots, std::min(slots, static_cast<int>(S_MissionItems::SLOTS)) * 12);
        for (int j = S_MissionItems::SLOTS; j < slots; ++j) {
            S_MissionItems::S_Slot skipped;
            dtaRead(file, &skipped, 12);
        }
        int pairs = 0;
        dtaRead(file, &pairs, 4);
        for (int j = 0; j < pairs; ++j) {
            std::pair<int, int> pair(-1, 0);
            dtaRead(file, &pair, 8);
            record->required.push_back(pair);
        }
    }
    dtaClose(file);
    return true;
}
