// The mission's debriefing (vtable 0x00815cf0, 0x290 bytes; the campaign screen kind 5): the squad's
// results, the medals and promotions, the diary; continuing saves the game. Part 9g-v: the class,
// starting, the messages, the results kept; 9g-vi and on: the screens.
#pragma once

#include <cstdint>

#include "game/menu_state.h"

class C_Control;
class I3D_frame;
struct S_ScreenDesc;
struct S_SoldierProfile;
struct S_SquadSoldier;

// The medals (S_SoldierProfile::unknown28's ids) and the ranks (unknown24) the debriefing shows.
constexpr int DEBRIEF_MEDALS = 12, DEBRIEF_RANKS = 8;
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
    bool OnStart(void *arg) override;                   // 0x40 0x0068cfe0 (DS 0x006cad70)

    // The campaign played, not a single mission.
    static bool IsCampaign();                           // 0x0068c5d0

protected:
    // Each squad soldier: a dead one's items back, the experience and the missions counted, a
    // promotion due, the medal due (from the second mission).
    static void ApplyResults();                         // 0x0068ce60 (DS 0x006cac40)
    // The medal `soldier` earned (S_SquadSoldier +0x38): the first campaign's operations' own, the
    // experience's, the missions' (by the rank).
    static void AwardMedal(S_SoldierProfile *profile, S_SquadSoldier *soldier);   // 0x0068cbf0
    // Whether a living (`alive`) or a dead roster soldier has medal 8.
    static void FindMedal8(bool *alive, bool *dead);    // 0x0068cb80 (__fastcall)
    // The results kept: the promotions and medals given, the progress added to the profiles (capped at
    // 100), the dead taken off the squad, the living's average given to the roster (by a random 60 ..
    // 140 %).
    static void StoreProgress();                        // 0x0068c5f0

    // The debriefing's screen: its "death" frame off, the titles, the soldiers' panel, their scroll
    // bar, the buttons (saving and going on: a won campaign mission; retrying; the diary: the
    // campaign's; the main menu). True. (Part 9g-vi.)
    bool DescribeDebriefing(S_ScreenDesc *desc, I3D_frame *model);   // 0x0068edc0
    // The diary's screen: its two pages' frames under "multi_page", the texts' control, the page
    // buttons, the exit; the diary's texts (headings in their style, the others after a blank line).
    // True. (Part 9g-vi.)
    bool DescribeDiary(S_ScreenDesc *desc, I3D_frame *model);        // 0x0068d8b0
    // The diary's page buttons and the debriefing's diary button: two pages back, on (as far as there
    // are pages), from the start (no control), the page break's (no page yet); the buttons (in)active.
    // True. (Part 9g-vi.)
    static bool OnDiaryPage(void *menu, void *argument, C_Control *control, uint32_t notification,
                            uint32_t value);            // 0x0068e160
    // The mission won: the operation's end's kind; a tutorial; every required objective neither open nor
    // critically failed and someone of the squad alive. (Part 9g-vi.)
    bool IsMissionWon() const;                          // 0x0068f8f0

    // The soldiers' panel: its picture, name, titles, the skills' 7 bars (their suffixes); the model's
    // medals' frames, their places, the new medal's, the ranks' badges, the rank's place, "death"
    // (part 9g-vii).
    void DescribeSoldiers(S_ScreenDesc *desc, I3D_frame *model);     // 0x0068e3e0
    // The mission's result: the objectives listed, "ACCOMPLISHED" (the operation's end's kind, or won) or
    // "FAILED", the won's or the lost's music (faded in). (The arguments are not read.)
    void ShowMissionResult(S_ScreenDesc *desc, I3D_frame *model);    // 0x0068f6c0
    // The objectives into the debriefing's text: the completed under "Tasks: ", the others (not the
    // inactive) under "Recommendation:". Won: a tutorial, or someone of the squad alive.
    static bool ListObjectives();                       // 0x0068f080
    // The squad's soldier `index` shown (past the squad: nothing): his face, name (his own or the full
    // one), the death frame, the rank or the promotion, the shipped game's debugging line with his
    // experience, the skills, the rank's badge, the medals (part 9g-viii).
    void ShowSoldier(int index);                        // 0x0068f9f0 (DS 0x006ce130)
    // The skills' bars: the shooting, the 2nd, the 1st (its maximum the stat and the gain), the
    // endurance, the stealth, the first aid, the lockpick; the mission's gain as the upgrade (no soldier:
    // hidden).
    static void ShowSkills(const S_SoldierProfile *profile, const S_SquadSoldier *soldier);   // 0x006905d0
    // The rank's badge in its place (a promotion's when higher).
    void ShowRank(const S_SoldierProfile *profile, const S_SquadSoldier *soldier);            // 0x006907b0
    // The medals held in their places, the new one's frame, name and description.
    void ShowMedals(const S_SoldierProfile *profile, const S_SquadSoldier *soldier);          // 0x00690020
    // The soldiers' scroll bar moved: its soldier shown (another than the one shown). Without a control:
    // false.
    static bool OnSoldierScroll(void *menu, void *argument, C_Control *control, uint32_t notification,
                                uint32_t value);        // 0x0068ff70

    int m_diaryPage = -1;                               // +0x1B0 the diary's left page (-1: none yet)
    int m_shownSoldier = -1;                            // +0x1B4 the squad's soldier shown
    I3D_frame *m_medalFrames[DEBRIEF_MEDALS][2] = {};   // +0x1B8 the medals' two frames ("DSO_a", "DSO_b", ..)
    I3D_frame *m_medalSlots[DEBRIEF_MEDALS] = {};       // +0x218 "medal01" .. "medal12" (the places)
    I3D_frame *m_rankFrames[DEBRIEF_RANKS] = {};        // +0x248 "rank_private" .. "rank_major"
    uint8_t m_unknown268[0x10] = {};                    // +0x268
    I3D_frame *m_newMedalSlot = nullptr;                // +0x278 "new_medal"
    I3D_frame *m_rankSlot = nullptr;                    // +0x27C "soldier_rank"
    I3D_frame *m_deathFrame = nullptr;                  // +0x280 "death"
    uint8_t m_unknown284[0xC] = {};                     // +0x284
};
