// Harness stub of game/profile.h (the real one is too wide): what the car's updates ask of the game
// profile (game/car_update.cpp) and the spawn point's (game/spawn_point.cpp; part 7b), under the
// real names.
#pragma once

#include <vector>

#include "common/c_str.h"
#include "game/pending.h"                               // S_MultiplayerSettings
#include "game/key_bindings.h"
#include "game/soldier.h"

// The detail settings (the spawn point asks for the voice chat; part 8's vertex animations for
// their switch).
struct S_DetailSettings {
    bool IsVoiceChatEnabled() const { return voiceChat; }
    bool IsEnabled(int option) const { return option >= 0 && option < 8 && switches[option]; }
    // (The options' video page, part 9e-xi: options_test.inc.)
    void SetSwitch(int option, bool on);
    bool voiceChat = false;
    bool switches[8] = {};
    bool invertMouse = false;                           // (the camera module, part 8e-iii)
    // (The options handler, part 9e-viii: options_test.inc.)
    float mouseSensitivity = 0, gamma = 0, soundVolume = 0, musicVolume = 0, voiceValue = 0;
    int level = 0;
    bool subtitles = false, crosshair = false;
};
enum : int {
    DETAIL_VERTEX_ANIMATION = 1,
    DETAIL_DECORATORS = 4,
};

// (The Junkers 88, part 7h: the HUD option and the play mode, as the real header has them.)
struct S_HudOption {
    bool shown = false;
};
// (The play modes, part 9g-ix: as the real header.)
enum E_PlayMode { PLAY_MODE_CAMPAIGN = 1, PLAY_MODE_LONE_WOLF = 2, PLAY_MODE_CARNAGE = 3 };
// (The save slots, part 9h-viii: as the real header.)
enum { SAVE_SLOT_CAMPAIGN = 0, SAVE_SLOT_LONE_WOLF = 1, SAVE_SLOT_CARNAGE = 2, SAVE_SLOT_SINGLE_MISSION = 3, SAVE_SLOT_SINGLE_CARNAGE = 4 };
struct S_PlayMode {
    int singleMission = 0;
    int mode = 0;
    int unknown0C = 0;
    int unknown10 = 0;
};

// (The hot key control's bindings, part 9c-v-d.)
// (The menus' diary texts, part 9d-ii-b: menu_run_test.inc.)
class C_Diary {
public:
    void AddText(int textId);
    // (The debriefing, part 9g-v: campaign_screens_test.inc.)
    void CommitCurrent();
    void ClearCurrent();
    const std::vector<int> &GetTexts() const;   // (the diary, part 9g-vi)
};

class C_PlayerProfile {
public:
    C_Diary *GetDiary();
    C_KeyBindings *GetKeyBindings() { return &m_keyBindings; }
    // (The video player, part 9g-iv: campaign_screens_test.inc.)
    void SetMovieSeen(const char *file, bool seen);
    // (The debriefing, part 9g-v.)
    bool SetProgress(int campaign, int mission);
    int GetProgress(int campaign) const;
    bool HasProgress(int campaign) const;               // (the missions' messages, part 9h-ix)
    int GetBestProgress(int campaign) const;
    // (The in-game menu, part 9e-v: game_menu_test.inc.)
    void ApplyVolume();
    // (The options handler, part 9e-viii: options_test.inc.)
    C_str GetName() const;
    // (The main menu, part 9h-iv: main_menu_test.inc.)
    bool GetLastSaveText(C_str *mode, C_str *mission, C_str *date) const;
    int GetSaveSlotMission(int slot) const;             // (the missions screen, part 9h-viii)
    const C_str &GetSaveSlotFile(int slot) const;
    bool Save();                                        // (the profile screen, part 9h-v)
    int Rename(const char *oldName, const char *newName);   // (its buttons, part 9h-vi)
    // (The in-game menu's presets, part 9e-xvii: game_menu_test.inc.)
    const C_str &GetProfileName() const;
    bool SaveOptions();
    // (The options' pages, part 9e-xi: options_test.inc; the options world_modules_test.inc's.)
    S_DetailSettings *GetOptions();
    void SetKeyLayout(int layout);                      // (the keys page, part 9e-xii)
    void ApplyGamma();
    void ApplyMusicVolume();
    void ApplyMouse();
    void ApplyVoiceValue();
    void ApplyHardwareOption();
    C_KeyBindings m_keyBindings;
};

class C_Campaign;
class C_HumanStore;
struct S_MissionSquad;
class C_ItemStock;
// (The main menu's profile screen, part 9h-vi: main_menu_test.inc.)
class C_GameConfig {
public:
    void SetLastProfile(C_str name);
};
class C_GameProfile {
public:
    C_GameConfig *m_config = nullptr;                   // (part 9h-vi)
    C_PlayerProfile *GetPlayerProfile() const { return m_player; }
    C_PlayerProfile *m_player = nullptr;
    bool IsCooperativeGame() const;
    S_DetailSettings *GetDetailSettings();
    // (The fonts' language, part 9a: fonts_test.inc.)
    C_str GetLanguage();
    bool IsLanguage(const char *language);
    C_str GetTextDirectory();
    // (The text boxes' layout, part 9b-ii: textbox_test.inc.)
    bool UsesUnicodeFont() const { return m_unicodeFont; }
    bool m_unicodeFont = false;
    S_HudOption *m_hudOption = nullptr;
    S_PlayMode *m_playMode = nullptr;
    // (The menus' reset, part 9d-ii-a: menu_test.inc.)
    void ResetGame();
    // (The equipment panel, part 9e-ii-a.)
    C_HumanStore *m_humanStore = nullptr;
    S_MissionSquad *m_missionSquad = nullptr;
    // (The in-game menu's stocks, part 9e-xix: game_menu_test.inc.)
    C_ItemStock *m_itemStock18 = nullptr;
    // (The equipment screen's second stock, part 9f-i: equipment_screen_test.inc.)
    C_ItemStock *m_itemStock1C = nullptr;
    // (The loadout presets and missions' items, part 9e-iv: loadout_test.inc.)
    C_str GetProfileDirectory();
    C_Campaign *m_campaign = nullptr;
    // (The in-game menu's main screen, part 9e-ix: game_menu_test.inc.)
    int GetDifficulty() const;
    void SetDifficulty(int difficulty);    // (the main menu's profile screen, part 9h-v)
    // (The debriefing, part 9g-v: campaign_screens_test.inc.)
    C_str GetSaveFileName(int a, int b);
    // (The squad screen, part 9g-ix.)
    struct S_SquadSoldier *FindSoldierState(int profileIndex);
    // (The main menu, part 9h-iii: main_menu_test.inc.)
    bool SelectPlayer(const char *name);
};
extern C_GameProfile g_gameProfile;
// (The debriefing's soldier, part 9g-viii: as the real header.)
struct S_RankName {
    int textId;
    int textId2;
    C_str name;
    C_str name2;
};
extern S_RankName g_rankNames[8];
extern int g_rosterTexts[24];
extern int g_versionMajor;
extern int g_versionMinor;
void __stdcall GetGameVersion(int *major, int *minor);
// (The main menu's profile screen, part 9h-v: main_menu_test.inc.)
void ScanProfiles(void (*callback)(const char *name, int context), int context);
bool DeleteProfile(const char *name);
int CreateProfile(const char *name);
extern S_MultiplayerSettings *g_mpSettings;             // (a reference into g_gameProfile in the real header)

// The squad soldier's slots (S_SquadSoldier::PutItem).
enum : uint8_t {
    SQUAD_SLOT_UNIFORM = 0x02,
    SQUAD_SLOT_BELT = 0x20,                             // (part 9e-xviii)
    SQUAD_SLOT_BACKPACK_CONTENTS = 0x40,
    // (Part 9f-iv: the equipment screen's.)
    SQUAD_SLOT_HEAD = 0x01,
    SQUAD_SLOT_HANDS = 0x04,
    SQUAD_SLOT_BACKPACK = 0x08,
    SQUAD_SLOT_LONG_GUN = 0x10,
};
// (The equipment panel, part 9e-ii-a: equipment_panel_test.inc fakes the methods.)
struct S_SoldierLoad {
    int capacity = 0;                                   // +0x00 (+0x8C)
    float weight = 0.0f;                                // +0x04 (+0x90)
    float backpackMaxWeight = 0.0f;                     // +0x08 (+0x94)
    int backpackMaxVolume = 0;                          // +0x0C (+0x98)
    float backpackWeight = 0.0f;                        // +0x10 (+0x9C)
    int backpackVolume = 0;                             // +0x14 (+0xA0)
    float beltWeight = 0.0f;                            // +0x18 (+0xA4)
    int beltVolume = 0;                                 // +0x1C (+0xA8)
    float GetBeltFill() const;
    float GetBackpackFill() const;
    float GetWeightFill() const;
};
class C_ItemStock;
struct S_SquadSoldier : S_SoldierState {
    void SplitStacks();
    void PutItem(S_SoldierItem *displaced, uint8_t slot, const S_SoldierItem &item);
    void ReturnItemsTo(C_ItemStock *stock);
    void ReturnItems();                                 // (the debriefing, part 9g-v)
    int CountItems(uint8_t slot, const S_SoldierItem &item) const;
    int RemoveItem(uint8_t slot, S_SoldierItem &item);
    int GetFreeBeltVolume() const;
    int GetFreeBackpackVolume() const;
    float GetFreeWeight() const;
    // (The in-game menu's soldiers panel, part 9e-xiv: game_menu_test.inc.)
    bool SetProfile(int profileIndex, int level);
    // (The player screen and the squad's team, part 9e-xviii: game_menu_test.inc.)
    void CopyFrom(const S_SquadSoldier &other);
    void UpdateLoad();
    S_SoldierLoad load;                                 // +0x8C
};
struct S_MissionSquad {
    // (The in-game menu's player screen, part 9e-xviii: game_menu_test.inc.)
    S_SquadSoldier *AddSoldier();
    // (The session menu, part 9g-ii: campaign_screens_test.inc.)
    void Clear();
    std::vector<S_SquadSoldier *> states;               // +0x04
};
// (The in-game menu's messages, part 9e-vii: game_menu_test.inc defines it.)
extern S_MissionSquad *&g_missionSquad;
class C_ItemStock {
public:
    void Add(int type, int count);
    // (The store list, part 9e-iii-a: store_list_test.inc.)
    int Take(int type, int count);
    int Get(int type) const;
    // (The in-game menu, part 9e-v: game_menu_test.inc; the stocks' setup, 9e-xix.)
    void Reset();
    void Set(int type, int count);
};
class C_HumanStore {
public:
    S_SoldierProfile *Get(int index) const;
    // (The squad screen's names, part 9g-xvi.)
    bool SaveCustomNames();
    // (The in-game menu's soldiers panel, part 9e-xiv: game_menu_test.inc; its stats, 9e-xix.)
    int GetCount() const;
    void GetStats(int *stats, int index, int level) const;
    // (The equipment screen's roster, part 9f-iii: equipment_screen_test.inc.)
    bool Load(C_str fileName);
    // (The session menu, part 9g-ii: campaign_screens_test.inc.)
    bool Reload();
};
