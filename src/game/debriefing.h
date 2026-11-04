// The mission's debriefing (vtable 0x00815cf0, 0x290 bytes; the campaign screen kind 5): the squad's
// results, the medals and promotions, the diary; continuing saves the game. Part 9g-v: the class,
// starting, the messages, the results kept; 9g-vi and on: the screens.
#pragma once

#include <cstdint>

#include "game/menu_state.h"

class I3D_frame;
struct S_ScreenDesc;
struct S_SoldierProfile;
struct S_SquadSoldier;

// The experience a rank needs (more than it: the next rank; ranks 0 .. 7).
extern const int g_rankExperience[7];                   // 0x008423c8

class C_DebriefingMenu : public C_MenuState {
public:
    // (Inline in C_GameApp::StartStep; the destructor is the base's: 0x006c2030.)
    C_DebriefingMenu() = default;

    int GetTypeId() const override { return GAME_STATE_TYPE_DEBRIEFING_MENU; }   // 0x1C 0x006c2020

    // Escape (no screen, or not the diary on top: the campaign's question "save and quit" (0x2533; the
    // mission played before: 0x2534), a single mission's "ARE YOU SURE?"; the diary: closed).
    // DEBRIEF_MSG_CONTINUE: the campaign's debriefing kept (the results, the progress, the game saved),
    // the next step. DEBRIEF_MSG_SAVE_QUIT: the same, the main menu. DEBRIEF_MSG_RESTART: the mission
    // again (the save before it loaded; the first: the game reset). DEBRIEF_MSG_REPLAY: a single mission
    // again.
    void OnMessage(S_ControlMessage *message) override; // 0x34 0x0068d0f0
    // The debriefing ("debriefing") and the campaign's diary ("DIARY") described, made; the first
    // opened, its soldier shown. True.
    bool BuildScreens() override;                       // 0x38 0x0068d5e0
    void OnStop() override {}                           // 0x3C 0x0068d0e0
    // The campaign's: the diary's entry kept, the debriefing's results applied (kind 5); a single
    // mission's: the diary's entry dropped. True.
    bool OnStart(void *arg) override;                   // 0x40 0x0068cfe0

    // The campaign played, not a single mission.
    static bool IsCampaign();                           // 0x0068c5d0

protected:
    // Each squad soldier: a dead one's items back, the experience and the missions counted, a
    // promotion due, the medal due (from the second mission).
    static void ApplyResults();                         // 0x0068ce60
    // The medal `soldier` earned (S_SquadSoldier +0x38): the first campaign's operations' own, the
    // experience's, the missions' (by the rank).
    static void AwardMedal(S_SoldierProfile *profile, S_SquadSoldier *soldier);   // 0x0068cbf0
    // Whether a living (`alive`) or a dead roster soldier has medal 8.
    static void FindMedal8(bool *alive, bool *dead);    // 0x0068cb80 (__fastcall)
    // The results kept: the promotions and medals given, the progress added to the profiles (capped at
    // 100), the dead taken off the squad, the living's average given to the roster (by a random 60 ..
    // 140 %).
    static void StoreProgress();                        // 0x0068c5f0

    // The screens (part 9g-vi).
    bool DescribeDebriefing(S_ScreenDesc *desc, I3D_frame *model);   // 0x0068edc0
    void DescribeDebriefing2(S_ScreenDesc *desc, I3D_frame *model);  // 0x0068f6c0
    bool DescribeDiary(S_ScreenDesc *desc, I3D_frame *model);        // 0x0068d8b0
    bool ShowSoldier(int *unknown);                     // 0x0068f9f0

    int m_unknown1B0 = -1;                              // +0x1B0
    int m_unknown1B4 = -1;                              // +0x1B4
    uint8_t m_unknown1B8[0xD8] = {};                    // +0x1B8
};
