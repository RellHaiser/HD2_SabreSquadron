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
    bool voiceChat = false;
    bool switches[8] = {};
    bool invertMouse = false;                           // (the camera module, part 8e-iii)
    // (The options handler, part 9e-viii: options_test.inc.)
    float mouseSensitivity = 0, gamma = 0, soundVolume = 0, musicVolume = 0, voiceValue = 0;
    int level = 0;
    bool unknown3A = false, crosshair = false;
};
enum : int {
    DETAIL_VERTEX_ANIMATION = 1,
    DETAIL_DECORATORS = 4,
};

// (The Junkers 88, part 7h: the HUD option and the play mode, as the real header has them.)
struct S_HudOption {
    bool shown = false;
};
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
};

class C_PlayerProfile {
public:
    C_Diary *GetDiary();
    C_KeyBindings *GetKeyBindings() { return &m_keyBindings; }
    // (The in-game menu, part 9e-v: game_menu_test.inc.)
    void ApplyVolume();
    // (The options handler, part 9e-viii: options_test.inc.)
    bool SaveOptions();
    void ApplyHardwareOption();
    C_KeyBindings m_keyBindings;
};

class C_Campaign;
class C_HumanStore;
struct S_MissionSquad;
class C_GameProfile {
public:
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
    // (The loadout presets and missions' items, part 9e-iv: loadout_test.inc.)
    C_str GetProfileDirectory();
    C_Campaign *m_campaign = nullptr;
    // (The in-game menu's main screen, part 9e-ix: game_menu_test.inc.)
    int GetDifficulty() const;
};
extern C_GameProfile g_gameProfile;
extern int g_versionMajor;
extern int g_versionMinor;
void __stdcall GetGameVersion(int *major, int *minor);
extern S_MultiplayerSettings *g_mpSettings;             // (a reference into g_gameProfile in the real header)

// The squad soldier's slots (S_SquadSoldier::PutItem).
enum : uint8_t {
    SQUAD_SLOT_UNIFORM = 0x02,
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
    int CountItems(uint8_t slot, const S_SoldierItem &item) const;
    int RemoveItem(uint8_t slot, S_SoldierItem &item);
    int GetFreeBeltVolume() const;
    int GetFreeBackpackVolume() const;
    float GetFreeWeight() const;
    S_SoldierLoad load;                                 // +0x8C
};
struct S_MissionSquad {
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
    // (The in-game menu, part 9e-v: game_menu_test.inc.)
    void Reset();
};
class C_HumanStore {
public:
    S_SoldierProfile *Get(int index) const;
};
