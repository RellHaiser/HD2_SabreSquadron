// The equipment menus' data: the loadout presets (GameData\presets.def and the profile's
// user_presets.def; C_GameMenu +0x49C) and the items the operations' missions bring along
// (GameData\miss_itm.def; C_GameMenu +0x1F0). Original 0x0067a3c0..0x0067e850 (part 9e-iv).
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

// An item type and its count (a preset's belt and backpack entries).
struct S_PresetItem {                                   // 8 bytes
    // (0x0067a3c0)
    void Reset()
    {
        typeId = -1;
        count = 0;
    }

    int typeId = -1;
    int count = 0;
};

// A loadout (0x1B8 bytes, read and written as it is; presets.def's version 0 has the first 0x1B0
// bytes, version 1 0x1B4).
struct S_LoadoutPreset {
    static constexpr int BELT_ITEMS = 14;
    static constexpr int BACKPACK_ITEMS = 32;
    static constexpr int NAME_SIZE = 0x20;

    // The places emptied (the belt's and the backpack's counts too; their entries kept).
    void ClearPlaces();                                 // 0x0067a3d0
    // How many of an item type the loadout has (its places, belt and backpack).
    int CountItems(int typeId) const;                   // 0x0067d450

    char name[NAME_SIZE] = {};                          // +0x00
    // The places' item types: head, hands, long gun, uniform, backpack (-1: none; the order the
    // in-game menu's StorePreset fills them in from the squad soldier's places).
    int head = -1;                                      // +0x20
    int hands = -1;                                     // +0x24
    int longGun = -1;                                   // +0x28
    int uniform = -1;                                   // +0x2C
    int backpack = -1;                                  // +0x30
    int beltCount = 0;                                  // +0x34
    S_PresetItem belt[BELT_ITEMS];                      // +0x38
    int backpackCount = 0;                              // +0xA8
    S_PresetItem backpackItems[BACKPACK_ITEMS];         // +0xAC
    // The games it is for (LOADOUT_*) and its name's text (-1: the name).
    uint32_t flags = 0;                                 // +0x1AC
    int textId = -1;                                    // +0x1B0
    int unknown1b4 = 0;                                 // +0x1B4
};
static_assert(sizeof(S_LoadoutPreset) == 0x1B8, "read and written as it is");

// S_LoadoutPreset::flags.
enum : uint32_t {
    LOADOUT_USER = 0x04,
    LOADOUT_AXIS = 0x12,
    LOADOUT_ALLIED = 0x22,
    LOADOUT_DEATHMATCH = 0x42,
};

class C_LoadoutPresets {                                // 0x20 bytes
public:
    C_LoadoutPresets() = default;
    C_LoadoutPresets(const C_LoadoutPresets &) = delete;
    C_LoadoutPresets &operator=(const C_LoadoutPresets &) = delete;
    // (The original leaves them to Clear.)
    ~C_LoadoutPresets() { Clear(); }

    // Both lists' presets deleted (the last first).
    void Clear();                                       // 0x0067d4d0
    S_LoadoutPreset *AddPreset();                       // 0x0067d580
    S_LoadoutPreset *AddUserPreset();                   // 0x0067d770
    // presets.def, then the profile's user presets (none: the defaults made and saved; false).
    bool Load();                                        // 0x0067d960
    // The user presets into the profile's user_presets.def (none yet: the defaults; version 3).
    bool SaveUser();                                    // 0x0067dc30
    // The 24 default user presets: six each of the axis, the allied, the deathmatch and the user's.
    void CreateUserPresets();                           // 0x0067def0
    // The `index`th preset (user preset) with all of `flags` (none: null).
    S_LoadoutPreset *GetPreset(int index, uint32_t flags) const;       // 0x0067de40
    S_LoadoutPreset *GetUserPreset(int index, uint32_t flags) const;   // 0x0067de90
    int GetPresetCount() const { return static_cast<int>(m_presets.size()); }
    int GetUserPresetCount() const { return static_cast<int>(m_userPresets.size()); }

private:
    std::vector<S_LoadoutPreset *> m_presets;           // +0x04
    std::vector<S_LoadoutPreset *> m_userPresets;       // +0x14
};

// The items a mission brings along (0x8C bytes).
struct S_MissionItems {
    static constexpr int SLOTS = 10;
    struct S_Slot {                                     // 12 bytes (meanings not identified)
        int a = -1, b = -1, c = -1;
    };

    int kind = 4;                                       // +0x00
    S_Slot slots[SLOTS];                                // +0x04
    // The items the mission needs the deployed soldiers to carry: (type, count).
    std::vector<std::pair<int, int>> required;          // +0x7C
};

class C_MissionItems {                                  // 0x2AC bytes
public:
    static constexpr int MAX_OPERATIONS = 0x32;
    static constexpr int MAX_RECORDS = 0x78;
    static constexpr int CAMPAIGN_STRIDE = 0x1E;        // the records (and operations) per campaign

    C_MissionItems();                                   // 0x0067e460
    C_MissionItems(const C_MissionItems &) = delete;
    C_MissionItems &operator=(const C_MissionItems &) = delete;
    ~C_MissionItems() { Release(); }

    // The records deleted.
    void Release();                                     // 0x0067e490
    // A record per mission of the current campaign's operations (their first records' indices),
    // then miss_itm.def.
    void Setup();                                       // 0x0067e500
    bool Load();                                        // 0x0067e650

    S_MissionItems *GetRecord(int index) const { return m_records[index]; }
    int GetFirstRecord(int campaign, int operation) const { return m_first[operation + campaign * CAMPAIGN_STRIDE]; }
    // The current campaign's record of a mission (its number; none past the records).
    S_MissionItems *GetMissionRecord(int mission) const;   // 0x0067e620

private:
    int m_first[MAX_OPERATIONS];                        // +0x000 (-1)
    S_MissionItems *m_records[MAX_RECORDS] = {};        // +0x0C8
    int m_campaign = 0;                                 // +0x2A8
};
