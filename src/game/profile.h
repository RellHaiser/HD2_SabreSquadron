// The game profile (g_gameProfile): the hub that owns the campaign tree, the player's profile
// and options, the soldier roster, the mission squad, the item stocks, the multiplayer settings
// and the play mode, and saves / loads them together in the player's saved games (chunk 1000,
// sub-chunks 0x3E9 .. 0x3F2). The campaign tree is in campaign.h, the key bindings in
// key_bindings.h; the multiplayer settings' methods (map list, mpdesc.txt) belong to the
// session menus and are not ported.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "common/c_str.h"
#include "game/key_bindings.h"
#include "game/soldier.h"

class C_Campaign;
class C_chunk;
class C_ItemStock;
struct S_MultiplayerSettings;

// ---------------------------------------------------------------------------
// The soldier roster (profile_roster.cpp, 0x0069f9a0..0x006a1f80)

// The ranks' names in two forms (C_HumanStore::Clear fetches them from the texts; the HUD
// shows them).
struct S_RankName {
    int textId;                                         // +0x00 0x27A6..
    int textId2;                                        // +0x04 0x2792..
    C_str name;                                         // +0x08
    C_str name2;                                        // +0x0C
};
extern S_RankName g_rankNames[8];                       // 0x008ae960 (static init 0x0069f9b0)
// The medals' names and descriptions (two text ids each, 0x2774..0x278B; the debriefing's ShowMedals
// 0x00690020).
extern int g_rosterTexts[24];                           // 0x008ae900 (static init 0x0069fb00)

// The soldiers the campaign knows (tables\HumanStore.def; C_GameProfile +0x10; 0x10 bytes).
class C_HumanStore {
public:
    C_HumanStore() = default;                           // (inline in C_GameProfile's constructor)
    ~C_HumanStore();                                    // 0x006b65f0
    C_HumanStore(const C_HumanStore &) = delete;
    C_HumanStore &operator=(const C_HumanStore &) = delete;

    // Deletes the soldiers and fetches the rank names again.
    void Clear();                                       // 0x0069fc00
    // Clear, then tables\HumanStore.def.
    bool Reload();                                      // 0x006a1b70 (thunk 0x006a1ba0)
    // The roster file: a chunk with the soldiers (chunks 9000).
    bool Load(C_str fileName);                          // 0x006a1bb0
    // Chunk 9000 per soldier (`withDeath`: their death flags too).
    bool SaveGame(C_chunk &chunk, bool withDeath);      // 0x006a1a50
    // Replaces the soldiers, then reads the player's usernames.sav.
    bool LoadGame(C_chunk &chunk);                      // 0x006a1e10
    int GetCount() const { return static_cast<int>(m_profiles.size()); }
    // Null for a bad index.
    S_SoldierProfile *Get(int index) const;             // 0x006a1f50
    // A soldier's stats for a human (C_Human::Setup): health and strength as they are, the
    // others (not 3) scaled by 1 + 0.03 * level and at most 100; `stats` keeps its values when
    // there is no such soldier.
    void GetStats(int *stats, int index, int level) const;   // 0x006a1f80
    // A new soldier "First Last" with the default face; its index.
    int Add();                                          // 0x006a2050
    // PlayersProfiles\<player>\usernames.sav: the names the player gave the soldiers
    // (chunk 0x29A, a chunk per soldier index with chunk 0x23B4).
    bool LoadCustomNames();                             // 0x006a1200
    bool SaveCustomNames();                             // 0x006a1680

private:
    std::vector<S_SoldierProfile *> m_profiles;         // +0x00
};

// ---------------------------------------------------------------------------
// The mission squad (profile_squad.cpp, 0x006a1f80..0x006a5a50)

// The slots S_SquadSoldier::PutItem / RemoveItem / CountItems take (bits).
enum : uint8_t {
    SQUAD_SLOT_HEAD = 0x01,
    SQUAD_SLOT_UNIFORM = 0x02,
    SQUAD_SLOT_HANDS = 0x04,
    SQUAD_SLOT_BACKPACK = 0x08,
    SQUAD_SLOT_LONG_GUN = 0x10,
    SQUAD_SLOT_BELT = 0x20,
    SQUAD_SLOT_BACKPACK_CONTENTS = 0x40,
};

// A soldier of the mission squad (0xAC bytes): the state a human is made from, and the load
// the equipment menus keep within the soldier's limits.
struct S_SquadSoldier : S_SoldierState {
    // The roster soldier: his name and what he can carry (`level` scales the other stats,
    // which are not kept).
    bool SetProfile(int profileIndex, int level);       // 0x006a2610
    // Everything but the health, the progress, +0x34 / +0x38, deployed and the speech set.
    void CopyFrom(const S_SquadSoldier &other);         // 0x006a2740
    // No items; the load is zero but for the capacity.
    void ClearItems();                                  // 0x006a2a90
    // The items go back to the profile's second item stock (C_GameProfile +0x1C), then
    // ClearItems.
    void ReturnItems();                                 // 0x006a2bf0
    void ReturnItemsTo(C_ItemStock *stock);             // 0x006a2c10
    // Equal items in the belt / the backpack go into one stack.
    void MergeItems();                                  // 0x006a2d60
    // The stacks of the belt and the backpack become single items (added at the end).
    void SplitStacks();                                 // 0x006a2f60
    // The load from the items (and the capacity from the profile).
    void UpdateLoad();                                  // 0x006a3120
    // Puts `item` in a slot if the limits allow: `displaced` gets what was there (the slots),
    // what did not fit (belt, backpack contents: the count left; type -1 when all fit), or the
    // item itself when nothing changed.
    void PutItem(S_SoldierItem *displaced, uint8_t slot, const S_SoldierItem &item);   // 0x006a3350
    // How many of the item's type the slot holds.
    int CountItems(uint8_t slot, const S_SoldierItem &item) const;   // 0x006a3cb0
    // Takes up to item.present of the item's type from a slot (item.present is lowered to what
    // there was); the count taken, -1 for an unknown item.
    int RemoveItem(uint8_t slot, S_SoldierItem &item);  // 0x006a3e90
    bool IsEmpty() const;                               // 0x006a42e0
    // Free backpack volume and free weight.
    int GetFreeBeltVolume() const;                      // 0x006a2bb0 (12 less the belt's)
    int GetFreeBackpackVolume() const;                  // 0x006a2bd0
    float GetFreeWeight() const;                        // 0x006a2be0
    // Chunks 0x0B .. 0x17 (0x13: the old load with a float capacity).
    bool SaveGame(C_chunk &chunk);                      // 0x006a49a0
    bool LoadGame(C_chunk &chunk);                      // 0x006a4c10

    // (+0x8C: the load, S_SoldierState's.)
};
// S_SquadSoldier::SplitStacks for any soldier state (the network's: a joining or respawning player's).
void SplitSoldierStacks(S_SoldierState &state);

// A soldier's records outside the soldier structures (__fastcall helpers of the original).
void ClearSoldierItems(S_SquadSoldier *soldier);        // 0x006a2a90
void MergeSoldierItems(S_SquadSoldier *soldier);        // 0x006a2d60
void UpdateSoldierLoad(S_SquadSoldier *soldier);        // 0x006a3120

// The soldiers of the next mission (C_GameProfile +0x14, vtable 0x008159fc; 0x14 bytes): the
// squad chosen in the menus in single player; in multiplayer the local player's state comes
// first.
struct S_MissionSquad {
    S_MissionSquad() = default;                         // (inline in C_GameProfile's constructor)
    virtual ~S_MissionSquad();                          // 0x006b61a0 (deleting 0x006b6180)
    S_MissionSquad(const S_MissionSquad &) = delete;
    S_MissionSquad &operator=(const S_MissionSquad &) = delete;

    // Deletes the soldiers.
    void Clear();
    // A new soldier (no profile, no items, deployed); null never in practice.
    S_SquadSoldier *AddSoldier();                       // 0x00673cf0
    // Chunk 10 per soldier.
    bool SaveGame(C_chunk &chunk);                      // 0x006a22f0
    bool LoadGame(C_chunk &chunk);                      // 0x006a2420

    std::vector<S_SquadSoldier *> states;               // +0x04
};

// ---------------------------------------------------------------------------
// The item stocks (profile_misc.cpp, 0x006a5a80..0x006a6220)

// Counts of items by item type (C_GameProfile +0x18 / +0x1C; 0x10 bytes). The equipment menus
// take from and put back to them; the second gets what the squad soldiers return.
class C_ItemStock {
public:
    C_ItemStock() = default;                            // 0x006a5f30
    ~C_ItemStock() = default;                           // 0x006a5f50
    void Reset();                                       // 0x006a5fa0
    void Set(int type, int count);                      // 0x006a5cf0
    void Add(int type, int count);                      // 0x006a5d70
    // Takes up to `count`; what it took.
    int Take(int type, int count);                      // 0x006a5df0
    int Get(int type) const;                            // 0x006a5f00
    // For every type with a count: chunk 0x4D2 with the type, then the count after the chunk.
    bool SaveGame(C_chunk &chunk);                      // 0x006a5a80
    bool LoadGame(C_chunk &chunk);                      // 0x006a5b60

private:
    // (The original keeps a pointer to an int per type.)
    std::vector<int> m_counts;                          // +0x00
};

// ---------------------------------------------------------------------------
// game.cfg and the HUD option (profile_misc.cpp, 0x006a6250..0x006a6b80)

// game.cfg: the CD key, the parental lock, the last profile (C_GameProfile +0x08; 0x0C bytes).
class C_GameConfig {
public:
    // Reads <game directory>\game.cfg.
    C_GameConfig();                                     // 0x006a6370
    ~C_GameConfig() = default;                          // 0x006b67d0
    const C_str &GetCdKey() const { return m_cdKey; }   // 0x006a67a0
    bool SetCdKey(C_str key);                           // 0x006a67b0
    // A parental lock code is set.
    bool IsParentLockSet() const;                       // 0x006a6780
    bool SetParentLock(C_str code);                     // 0x006a65f0
    // Clears the lock if `code` is its code (case-insensitive).
    bool ClearParentLock(C_str code);                   // 0x006a6670
    // The player profile used last.
    const C_str &GetLastProfile() const { return m_lastProfile; }   // 0x006a6830
    void SetLastProfile(C_str name);                    // 0x006a6840

private:
    // Writes game.cfg (into the working directory).
    bool Save();                                        // 0x006a6250

    C_str m_cdKey;                                      // +0x00 NETCDKEY
    C_str m_parentLock;                                 // +0x04 PARENTLOCK
    C_str m_lastProfile;                                // +0x08 LASTPROFIL
};

// The profile's "show the HUD" option (C_GameProfile +0x0C; one byte).
struct S_HudOption {
    bool shown = false;
    bool SaveGame(C_chunk &chunk);                      // 0x006a68c0 (chunk 0x1642)
    bool LoadGame(C_chunk &chunk);                      // 0x006a69c0
};

// ---------------------------------------------------------------------------
// The debriefing, the movies, the diary (profile_misc.cpp, 0x006a6bb0..0x006a83c0)

// The texts the mission scripts add for the debriefing (C_PlayerProfile +0xDC; 0x10 bytes).
class C_Debriefing {
public:
    void AddText(int textId);                           // 0x006a6c10
    void Clear();                                       // 0x006a6bb0
    const std::vector<int> &GetTexts() const { return m_texts; }

private:
    // (The original keeps a pointer to an int per text.)
    std::vector<int> m_texts;                           // +0x00
};

// The movies the extras menu can play, and which of them the player has seen (C_PlayerProfile
// +0x100; 0x10 bytes; saved in profile.sav).
class C_MovieList {
public:
    struct S_Movie {                                    // 0x0C bytes
        bool seen;
        int textId;                                     // +0x04
        const char *file;                               // +0x08
    };

    C_MovieList() { Reset(); }                          // 0x006b6130
    // The seventeen movies, only Intro.bik seen.
    void Reset();                                       // 0x006a6de0
    // Marks the movie with this file name (searching from the end); false if there is none.
    bool SetSeen(const char *file, bool seen);          // 0x006a70e0
    int GetCount() const { return static_cast<int>(m_movies.size()); }
    bool Get(int index, int *textId, C_str *file) const;   // 0x006a7600
    bool IsSeen(int index) const;                       // 0x006a7660
    // Chunk 0x1EC8: the first ten flags, last first; 0x1EC9: the others, last first.
    void Save(C_chunk &chunk) const;                    // 0x006a7150
    void Load(C_chunk &chunk);                          // 0x006a7370

private:
    std::vector<S_Movie> m_movies;                      // +0x00
};

// The player's diary of a campaign (C_PlayerProfile +0x94 / +0xB8; 0x24 bytes): the texts the
// mission scripts add (the current mission's apart until the mission is won).
class C_Diary {
public:
    // Nothing while locked.
    void AddText(int textId);                           // 0x006a7ad0
    void Clear();                                       // 0x006a7a90
    // Only the current mission's texts.
    void ClearCurrent();                                // 0x006a8390
    // The current mission's texts join the diary.
    void CommitCurrent();                               // 0x006a7c80
    // Chunks 0x6F (the diary) and 0x70 (the current mission), each with chunks 0x71.
    bool SaveGame(C_chunk &chunk);                      // 0x006a7ce0
    // Also the old chunks 0 and 2.
    bool LoadGame(C_chunk &chunk);                      // 0x006a7f30
    const std::vector<int> &GetTexts() const { return m_texts; }
    const std::vector<int> &GetCurrentTexts() const { return m_current; }

    // Set while a saved game is written or read and while a mission restarts.
    bool locked = false;                                // +0x00

private:
    std::vector<int> m_texts;                           // +0x04
    std::vector<int> m_current;                         // +0x14
};

// ---------------------------------------------------------------------------
// The player's profile (player_profile.cpp, 0x006ae7c0..0x006b4960)

// The options (C_PlayerProfile +0x38; 0x3C bytes; Options.bin chunk 0x1201).
struct C_PlayerOptions {
    // The defaults (only +0x14 is left as it is).
    void SetDefaults();                                 // 0x006b3740
    // The detail switch of the current level (DETAIL_*).
    bool IsEnabled(int option) const;                   // 0x006b3700
    void SetSwitch(int option, bool on);                // 0x006b36a0
    bool IsVoiceChatEnabled() const { return voiceChat; }
    void Save(C_chunk &chunk) const;                    // 0x006b3850
    void Load(C_chunk &chunk);                          // 0x006b3a70

    float mouseSensitivity;                             // +0x00 the controls get 1000 - 9 * it
    float gamma;                                        // +0x04 50 = linear
    float soundVolume;                                  // +0x08 percent
    float musicVolume;                                  // +0x0C percent
    float voiceValue;                                   // +0x10 (C_NetGame::SetVoiceValue)
    int unknown14;                                      // +0x14 (saved; never set)
    int level;                                          // +0x18 the detail level 0..3
    bool switches[4][7];                                // +0x1C per level
    bool voiceChat;                                     // +0x38
    bool invertMouse;                                   // +0x39 the controls' +0xB4 (inferred)
    bool subtitles;                                     // +0x3A (the options' game page)
    bool crosshair;                                     // +0x3B (the game page's "parental lock" control, text 0x4EF4)
};
enum : int {
    DETAIL_HARDWARE = 0,                                // the 3D driver's state 0x2B
    DETAIL_VERTEX_ANIMATION = 1,                        // flags and palms moving (C_VertexAnimModule)
    DETAIL_DECALS = 2,                                  // bullet holes, scorch marks and blood
    DETAIL_STAINS = 3,                                  // blood stains on the bodies (C_HumanStains)
    DETAIL_DECORATORS = 4,                              // grass and the like (C_Decorators)
    DETAIL_RAGDOLL = 5,                                 // dead bodies fall as ragdolls
};
// The game's save slots of a player (the quick saves the menus offer per play mode).
enum E_SaveSlot {
    SAVE_SLOT_CAMPAIGN = 0,
    SAVE_SLOT_LONE_WOLF = 1,
    SAVE_SLOT_CARNAGE = 2,
    SAVE_SLOT_SINGLE_MISSION = 3,                       // a campaign mission played on its own
    SAVE_SLOT_SINGLE_CARNAGE = 4,
    SAVE_SLOT_COUNT = 5,
};

// The player's profile (PlayersProfiles\<name>: profile.sav, Options.bin, usernames.sav, the
// saved games; C_GameProfile +0x20; 0x110 bytes).
class C_PlayerProfile {
public:
    C_PlayerProfile();                                  // (inline in C_GameProfile's constructor)
    ~C_PlayerProfile() = default;                       // 0x006b6640

    // --- the profile directory --------------------------------------------------------
    // The directory name: the name itself, or with the menus' unicode font the "Profile_<n>"
    // directory the name maps to.
    C_str GetName() const;                              // 0x006ae7e0
    // The profile's own name (GetName's when no other is given; the in-game menu's presets).
    const C_str &GetProfileName() const { return m_name; }
    // Makes the profile `name` current: its options and profile.sav (false never).
    bool SelectProfile(const char *name);               // 0x006af480
    // Renames the current profile (and its directory, or its name's mapping); 1, or -1 when
    // the name is taken, -2 on other errors.
    int Rename(const char *oldName, const char *newName);   // 0x006aec40
    // profile.sav (chunk 0x1EDC).
    bool Save();                                        // 0x006b0cc0
    bool Load();                                        // 0x006afdb0

    // --- options ----------------------------------------------------------------------
    C_PlayerOptions *GetOptions() { return &m_options; }
    bool IsCrosshairOn() const { return m_options.crosshair; }
    // Options.bin: the options and the key bindings, then applies them.
    bool LoadOptions();                                 // 0x006b3060
    bool SaveOptions();                                 // 0x006b2c70
    // The default options and key layout 0.
    void SetDefaultOptions();                           // 0x006b34e0
    void SetKeyLayout(int layout);                      // 0x006b3520
    // The sound module's volumes, the gamma ramp, the controls.
    void ApplyVolume();                                 // 0x006b35c0
    void ApplyMusicVolume();                            // 0x006b35f0
    void ApplyGamma();                                  // 0x006b3530
    void ApplyHardwareOption();                         // 0x006b3660
    void ApplyVoiceValue();                             // 0x006b3640
    void ApplyMouse();                                  // 0x006b3610
    C_KeyBindings *GetKeyBindings() { return &m_keyBindings; }
    // The key bindings' mode (2 or 3).
    void SetControlMode(int mode) { m_keyBindings.SetMode(mode); }   // (0x006a8940 on +0xEC)

    // --- progress -----------------------------------------------------------------------
    int GetDifficulty() const { return m_difficulty; }
    void SetDifficulty(int difficulty);                 // 0x006b1730
    // The mission reached in a campaign (0 / 1) in the current play mode; it also raises the
    // best of all modes. False for single missions and other modes.
    bool SetProgress(int campaign, int mission);        // 0x006b15f0
    bool HasProgress(int campaign) const;               // 0x006b1660
    int GetBestProgress(int campaign) const;            // 0x006b16c0
    int GetProgress(int campaign) const;                // 0x006b16e0

    // --- diary, debriefing, movies --------------------------------------------------------
    // The diary of the campaign played.
    C_Diary *GetDiary();                                // 0x006ae7c0
    // GetDiary()->ClearCurrent().
    void ClearCurrentDiary();                           // 0x006b4950
    C_Debriefing &GetDebriefing() { return m_debriefing; }
    C_MovieList &GetMovies() { return m_movies; }
    // Marks a movie seen and saves the profile.
    void SetMovieSeen(const char *file, bool seen);     // 0x006b15b0

    // --- saved games ----------------------------------------------------------------------
    // The name of the save made last (any directory part is dropped).
    const C_str *GetLastSave();                         // 0x006b3ec0
    // Also its text and, for a quick save, the play mode's save slot; saves the profile.
    void SetLastSave(const C_str &name);                // 0x006b3fb0
    // The last save's text "mode$mission$date" in three parts.
    bool GetLastSaveText(C_str *mode, C_str *mission, C_str *date) const;   // 0x006b3de0
    // Chunk 1000 with the diary (chunk 0x15B3).
    bool SaveGame(C_chunk &chunk);                      // 0x006b4570
    bool LoadGame(C_chunk &chunk);                      // 0x006b4720
    // A play mode's save slot (SAVE_SLOT_*): its mission (-1: none) and file (the main menu's missions screen).
    int GetSaveSlotMission(int slot) const { return m_saveSlots[slot].mission; }
    const C_str &GetSaveSlotFile(int slot) const { return m_saveSlots[slot].file; }

private:
    struct S_SaveSlotInfo {
        int mission = 0;                                // -1: none
        C_str file;
    };

    // The state before reading profile.sav (the slots' files stay).
    void Reset();                                       // 0x006af6d0
    // Options.bin; the defaults where it fails.
    bool ReadOptions();                                 // 0x006b30a0
    // The old PlayersProfiles\<name>\profile.pr.
    bool LoadOldProfile();                              // 0x006af740
    // Forgets the slots whose files are no longer quick saves; a last quick save that is gone
    // becomes the campaign save of its mission when that exists.
    bool CheckSaves();                                  // 0x006b0660
    bool UseMissionSave(int mission);                   // 0x006b07d0
    void UpdateLastSaveText(const C_str &file);         // 0x006b4020

    C_str m_name;                                       // +0x00
    C_str m_lastSave;                                   // +0x04 LASTSAVE
    C_str m_lastSaveText;                               // +0x08 LASTSAVETEXT "mode$mission$date"
    S_SaveSlotInfo m_saveSlots[SAVE_SLOT_COUNT];        // +0x0C (the ints are not initialized)
    int m_difficulty = 0;                               // +0x34 0..4
    C_PlayerOptions m_options;                          // +0x38
    // Per campaign: the best mission of all modes, then campaign, lone wolf, carnage.
    int m_progress[2][4];                               // +0x74
    C_Diary m_diaries[2];                               // +0x94 one per campaign
    C_Debriefing m_debriefing;                          // +0xDC
    C_KeyBindings m_keyBindings;                        // +0xEC
    C_MovieList m_movies;                               // +0x100
};

// The player profiles. With the menus' unicode font a profile's directory is "Profile_<n>" and
// profile.sav keeps the name (chunk 0x1EDD); this map leads from the names to the directories.
extern std::map<std::string, std::string> g_profileDirectories;   // 0x008ae9e8 (static init 0x006ae670)
// Makes PlayersProfiles\<name> (or the next free Profile_<n>, at most 200): 1, -1 when it
// exists (or there are too many), -2 on errors.
int CreateProfile(const char *name);                    // 0x006ae8e0
// The directory of a name, null if none.
const char *FindProfileDirectory(const char *name);     // 0x006aefe0
// The profile `name` deleted (its directory, read-only files too; its name forgotten). True when the directory
// was removed (an empty name: whether PlayersProfiles itself was). (Not ported yet.)
// A profile's directory deleted with what it holds, its name forgotten (no name: every profile's contents deleted;
// part 9i-v). False when it was not emptied or (unicode font) is not known.
bool DeleteProfile(const char *name);                   // 0x006aef40 (__stdcall)
// A profile name's directory forgotten (none: false).
bool ForgetProfileName(const char *name);               // 0x006af0f0 (__fastcall)
// Finds the profiles in PlayersProfiles (the name map anew) and calls `callback` with each
// directory name (__fastcall in the original).
void ScanProfiles(void (*callback)(const char *name, int context), int context);   // 0x006af200
// Loads text\<language>\*.txt but dialogs.txt (a __thiscall method of the player profile in
// the original, which does not use `this`).
void LoadGameTexts(const char *language);               // 0x006b13b0

// ---------------------------------------------------------------------------
// The play mode (C_GameProfile +0x28, vtable 0x008159f4; 0x14 bytes)

enum E_PlayMode : int {
    PLAY_MODE_CAMPAIGN = 1,                             // saves CAM_*.sav
    PLAY_MODE_LONE_WOLF = 2,                            // LOW_*.sav
    PLAY_MODE_CARNAGE = 3,                              // CAR_*.sav
};

struct S_PlayMode {
    S_PlayMode() = default;                             // 0x006b4990
    virtual ~S_PlayMode() = default;                    // 0x006b49b0
    // Chunk 0x225C with the four ints.
    bool SaveGame(C_chunk &chunk);                      // 0x006b49d0
    bool LoadGame(C_chunk &chunk);                      // 0x006b4ad0

    // A mission of the campaign played on its own (C_Campaign::SetupSingleMission).
    int singleMission = 0;                              // +0x04 0 / 1
    int mode = PLAY_MODE_CAMPAIGN;                      // +0x08 E_PlayMode (single missions: 0 or 3)
    int unknown0C = 0;                                  // +0x0C
    int unknown10 = 0;                                  // +0x10 (saved, not initialized)
};

// ---------------------------------------------------------------------------
// The profile (vtable 0x00815a08, one slot; 0x34 bytes)

class C_GameProfile {
public:
    C_GameProfile();                                    // 0x006b4d80
    ~C_GameProfile();                                   // 0x006b62d0
    C_GameProfile(const C_GameProfile &) = delete;
    C_GameProfile &operator=(const C_GameProfile &) = delete;

    // The text language: the registry's "language" value (english when there is none);
    // also decides whether the menus use the unicode font (Japanese, Russian, or a
    // Text\<language>\FontConfig.hxml).
    virtual C_str GetLanguage();                        // 0x00 0x006b6ad0
    bool IsLanguage(const char *language);              // 0x006b6a70
    // Text\<language>\ (C_GameProfile's, in the menus' code).
    C_str GetTextDirectory();                           // 0x0048fbc0
    bool UsesUnicodeFont() const { return m_unicodeFont; }

    // Frees the objects the profile owns (the destructor calls it too; nothing is saved).
    void FreeObjects();                                 // 0x006b6330
    // Loads the campaigns (the second one is played) and the soldier roster.
    bool Init();                                        // 0x006b68f0
    // A new game: the campaigns again from the files, the roster, the item stocks, the squad
    // and the diary emptied.
    void ResetGame();                                   // 0x006b6850
    // Leaving a session: the diary's current page, the second item stock, the session type.
    void ResetSession();                                // 0x006b7310
    // Selects the player profile used last (rescanning the profiles first when the menus use
    // the unicode font).
    void SelectLastPlayer();                            // 0x006b6940
    // Selects player profile `name` and remembers it in game.cfg.
    bool SelectPlayer(const char *name);                // 0x006b6990

    // --- settings -------------------------------------------------------------------------
    bool IsCooperativeGame() const;                     // 0x006b69d0
    // Single player: the player's; multiplayer: 3, co-op: the session's.
    int GetDifficulty() const;                          // 0x006b6a20
    C_PlayerOptions *GetDetailSettings();               // 0x006b6a60
    // Single player: the player's option; multiplayer: the session's allowcrosshair.
    bool IsCrosshairAllowed() const;                    // 0x006b69f0
    void SetDifficulty(int difficulty);                 // 0x006b6a50
    void SetControlMode(int mode);                      // 0x006b7720
    C_PlayerProfile *GetPlayerProfile() const { return m_player; }
    // Status of objective `objective` of a mission (see C_Campaign::GetObjectiveStatus).
    int GetObjectiveStatus(int campaign, int operation, int mission, int objective);   // 0x006b6820
    // Saved state of the campaign soldier with roster index `profileIndex`, null if none.
    S_SquadSoldier *FindSoldierState(int profileIndex); // 0x006b76b0

    // --- saved games ----------------------------------------------------------------------
    // PlayersProfiles\<player>\ .
    C_str GetProfileDirectory();                        // 0x006b7420
    // <profile dir>CAM_<a><b>.sav / LOW_... / CAR_... by the play mode (empty for single
    // missions and other modes).
    C_str GetSaveFileName(int a, int b);                // 0x006b7a00
    C_str GetQuickSaveFileName();                       // 0x006b7d20
    // The profile directory and the name of the save made last.
    C_str GetLastSavePath();                            // 0x006b8050
    void SetLastSave(const C_str &name);                // 0x006b79f0
    // The profile's part of a saved game (chunks 0x3E9 .. 0x3F2).
    bool SaveGame(C_chunk &chunk);                      // 0x006b6d70
    // While a game runs, the campaign, the roster, the first item stock and the play mode stay.
    bool LoadGame(C_chunk &chunk);                      // 0x006b7100
    // The same inside chunk 1000.
    bool SaveGameChunk(C_chunk &chunk);                 // 0x006b74e0
    bool LoadGameChunk(C_chunk &chunk);                 // 0x006b75d0
    // A saved game's chunk 1000 with everything (a chunk of another id is skipped and the
    // ones after it are read in its place).
    bool LoadSavedGame(C_chunk &chunk);                 // 0x006b7730

    // The owned objects (the original reads several through their addresses, see below).
    C_Campaign *m_campaign = nullptr;                   // +0x04
    C_GameConfig *m_config = nullptr;                   // +0x08
    S_HudOption *m_hudOption = nullptr;                 // +0x0C
    C_HumanStore *m_humanStore = nullptr;               // +0x10
    S_MissionSquad *m_missionSquad = nullptr;           // +0x14
    C_ItemStock *m_itemStock18 = nullptr;               // +0x18 kept while a game runs
    C_ItemStock *m_itemStock1C = nullptr;               // +0x1C the squad's returned items; reset per session
    C_PlayerProfile *m_player = nullptr;                // +0x20
    S_MultiplayerSettings *m_mpSettings = nullptr;      // +0x24
    S_PlayMode *m_playMode = nullptr;                   // +0x28

private:
    C_str m_language;                                   // +0x2C
    bool m_unicodeFont = false;                         // +0x30
};
extern C_GameProfile g_gameProfile;                     // 0x008aea0c (static init 0x006b4d40)
// The game's version (the servers' must match it).
extern int g_versionMajor;                              // 0x00845070
extern int g_versionMinor;                              // 0x00845074

// The profile's objects as the original code reaches them (the members' addresses).
extern C_Campaign *&g_campaign;                         // 0x008aea10 (+0x04)
extern S_HudOption *&g_hudOption;                       // 0x008aea18 (+0x0C)
extern S_MissionSquad *&g_missionSquad;                 // 0x008aea20 (+0x14)
extern S_MultiplayerSettings *&g_mpSettings;            // 0x008aea30 (+0x24)
extern S_PlayMode *&g_playMode;                         // 0x008aea34 (+0x28)

// The version numbers the menus show (1, 12).
void __stdcall GetGameVersion(int *major, int *minor);  // 0x006b6d50
