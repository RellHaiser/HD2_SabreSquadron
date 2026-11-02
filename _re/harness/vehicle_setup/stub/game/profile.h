// Harness stub of game/profile.h (the real one is too wide): what the car's updates ask of the game
// profile (game/car_update.cpp) and the spawn point's (game/spawn_point.cpp; part 7b), under the
// real names.
#pragma once

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
class C_PlayerProfile {
public:
    C_KeyBindings *GetKeyBindings() { return &m_keyBindings; }
    C_KeyBindings m_keyBindings;
};

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
struct S_SquadSoldier : S_SoldierState {
    void SplitStacks();
    void PutItem(S_SoldierItem *displaced, uint8_t slot, const S_SoldierItem &item);
    uint8_t load[0x20] = {};                            // +0x8C (S_SoldierLoad)
};
