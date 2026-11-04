// The debriefing's screens (game/debriefing.h): part 9g-vi the diary; 9g-vii the debriefing's.
#include "game/debriefing.h"

#include <cstring>

#include "game/campaign.h"
#include "game/controls.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "game/sound_module.h"
#include "ls3d/i3d.h"
#include "ls3d/math.h"

namespace {

// The diary's controls: its pages (the multi page text), the previous and next page buttons.
constexpr uint32_t DIARY_PAGES = 0x8200000, DIARY_PREVIOUS = 0x8300000, DIARY_NEXT = 0x8400000;
constexpr int CONTROL_SCROLL = 8, CONTROL_LABEL = 0x12, CONTROL_TEXT = 0x13, CONTROL_BUTTON = 0x14, CONTROL_MULTI_PAGE = 0x25;
// The menu's modes: the debriefing's campaign screen kind; 6 (the operation's end's kind: no retry).
constexpr int KIND_DEBRIEFING_SCREEN = 5, KIND_OPERATION_END_SCREEN = 6;
// The debriefing's messages (debriefing.cpp's), the diary's screen.
constexpr uint32_t DEBRIEF_MSG_CONTINUE = 0x2000034, DEBRIEF_MSG_RESTART = 0x2000075, DEBRIEF_MSG_REPLAY = 0x200007C;
constexpr uint32_t DIARY_SCREEN_INDEX = 2;
// "MISSION DEBRIEFING", "CAMPAIGN DEBRIEFING", the mission's status (2010), "Status:", "Team status:",
// "SAVE AND CONTINUE", "RETRY", "SHOW DIARY", "EXIT TO MAIN MENU"; their styles.
constexpr uint32_t TEXT_DEBRIEFING = 0xABF, TEXT_OPERATION_END = 0xAC0, TEXT_MISSION_STATUS = 0x7DA, TEXT_STATUS = 0xAC8,
                   TEXT_TEAM_STATUS = 0xAC9, TEXT_SAVE_CONTINUE = 0xAC2, TEXT_RETRY = 0xACC, TEXT_DIARY = 0xAC3,
                   TEXT_EXIT_MAIN = 0xAC4;
constexpr uint32_t MISSION_STATUS_STYLE = 0xC, STATUS_STYLE = 0xB;
constexpr int FRAME_TYPE_DUMMY = 6;
// "DIARY" (the title), "Previous page", "Next page", "Exit", the version, blank; the styles.
constexpr uint32_t TEXT_DIARY_TITLE = 0xAC1, TEXT_PREVIOUS = 0xAC5, TEXT_NEXT = 0xAC6, TEXT_EXIT = 0xAC7, TEXT_VERSION = 0x7D6,
                   TEXT_BLANK = 0x7D2;
constexpr uint32_t TITLE_STYLE = 0xA, BUTTON_STYLE = 0xD, VERSION_STYLE = 0x28;
// The diary's lines: headings (ids under 1000 and 16010 .. 16019) in style 4, the others after a blank
// line (style 2) in style 5.
constexpr uint32_t HEADING_STYLE = 4, BLANK_STYLE = 2, ENTRY_STYLE = 5;

void Post(uint32_t target, uint32_t id, uint32_t value, uintptr_t value2 = 0)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    message.value2 = value2;
    PostControlMessage(message);
}

}   // namespace

bool C_DebriefingMenu::DescribeDiary(S_ScreenDesc *desc, I3D_frame *model)
{
    // The two pages' frames under a frame of their own (the original looks at neither).
    I3D_frame *right = model->FindChildFrame("text_02", FRAME_MASK_VISUAL);
    I3D_frame *left = model->FindChildFrame("text_01", FRAME_MASK_VISUAL);
    I3D_frame *pages = AddFrame(C_fstr("multi_page"), FRAME_TYPE_DUMMY, model, S_vector(0.0f, 0.0f, 0.0f));
    right->LinkTo(pages, 1);
    left->LinkTo(pages, 1);
    right->Update();
    left->Update();
    const auto self = reinterpret_cast<uintptr_t>(this);
    const auto page = reinterpret_cast<uintptr_t>(&C_DebriefingMenu::OnDiaryPage);
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_screen"), 0, CONTROL_MSG_SET_TEXT_ID, TITLE_STYLE, TEXT_DIARY_TITLE);
    S_ControlDesc *text = desc->AddControl(CONTROL_MULTI_PAGE, "multi_page");
    AddDescMessage(text, 0, CONTROL_MSG_ADD_AREA, 0, C_fstr("text_02"));
    AddDescMessage(text, 0, CONTROL_MSG_ADD_AREA, 0, C_fstr("text_01"));
    S_ControlDesc *previous = desc->AddControl(CONTROL_BUTTON, "bprevious page");
    AddDescMessage(previous, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_PREVIOUS);
    AddDescMessage(previous, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, page);
    S_ControlDesc *next = desc->AddControl(CONTROL_BUTTON, "bnext page");
    AddDescMessage(next, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_NEXT);
    AddDescMessage(next, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, page);
    S_ControlDesc *exit = desc->AddControl(CONTROL_BUTTON, "bexit");
    AddDescMessage(exit, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_EXIT);
    AddDescMessage(exit, 0, CONTROL_NOTIFY_CLICK | MENU_MSG_BACK, 0);
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_h&dversion"), 0, CONTROL_MSG_SET_TEXT_ID, VERSION_STYLE, TEXT_VERSION);
    // The diary's texts.
    for (int id : g_gameProfile.GetPlayerProfile()->GetDiary()->GetTexts()) {
        if (id < 1000 || (id >= 0x3E8A && id < 0x3E94)) {
            Post(DIARY_PAGES, CONTROL_MSG_ADD_TEXT_ID, HEADING_STYLE, static_cast<uint32_t>(id));
        } else {
            Post(DIARY_PAGES, CONTROL_MSG_ADD_TEXT_ID, BLANK_STYLE, TEXT_BLANK);
            Post(DIARY_PAGES, CONTROL_MSG_ADD_TEXT_ID, ENTRY_STYLE, static_cast<uint32_t>(id));
        }
    }
    Post(DIARY_PAGES, CONTROL_MSG_REFRESH, 0);
    return true;
}

bool C_DebriefingMenu::OnDiaryPage(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_DebriefingMenu *>(menu);
    C_Control *pages = self->FindControl(DIARY_PAGES);
    S_ControlMessage get;
    get.id = CONTROL_MSG_GET_PAGES;
    pages->HandleMessage(&get);
    const int count = static_cast<int>(get.value);
    int &page = self->m_diaryPage;
    // Two pages at a time: back, on, from the start; another control (the screen shown) with no page
    // yet: the page break's (its left page).
    if (!control) {
        page = 0;
    } else if (control->GetId() == DIARY_PREVIOUS) {
        if (page - 2 >= 0)
            page -= 2;
    } else if (control->GetId() == DIARY_NEXT) {
        if (page + 2 <= count)
            page += 2;
    } else if (page < 0) {
        S_ControlMessage pageBreak;
        pageBreak.id = CONTROL_MSG_GET_PAGE_BREAK;
        pages->HandleMessage(&pageBreak);
        page = static_cast<int>(pageBreak.value);
        if (page < 0)
            page = 0;
        if (page % 2 != 0)
            --page;
    }
    Post(DIARY_PAGES, CONTROL_MSG_SET_PAGE, static_cast<uint32_t>(page));
    Post(DIARY_PREVIOUS, page == 0 ? CONTROL_MSG_DEACTIVATE : CONTROL_MSG_ACTIVATE, 0);
    Post(DIARY_NEXT, page + 2 < count ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE, 0);
    return true;
}

bool C_DebriefingMenu::IsMissionWon() const
{
    // (The original's copy protection is left out.)
    if (m_mode == KIND_OPERATION_END_SCREEN)
        return true;
    C_MissionDef *mission = g_gameProfile.m_campaign->GetCurrentMission();
    if (!mission)
        return false;
    if (mission->IsTutorial())
        return true;
    for (int i = 0; i < mission->GetObjectiveCount(); ++i) {
        const S_MissionObjective *objective = mission->GetObjective(i);
        if (!objective->bonus && (objective->status == OBJECTIVE_OPEN || objective->status == OBJECTIVE_FAILED_CRITICAL))
            return false;
    }
    // Someone of the squad alive (the last first).
    const std::vector<S_SquadSoldier *> &states = g_gameProfile.m_missionSquad->states;
    for (size_t i = states.size(); i-- > 0;)
        if (!g_gameProfile.m_humanStore->Get(states[i]->profileIndex)->dead)
            return true;
    return false;
}

bool C_DebriefingMenu::DescribeDebriefing(S_ScreenDesc *desc, I3D_frame *model)
{
    if (I3D_frame *death = model->FindChildFrame("death", FRAME_MASK_VISUAL))
        death->SetOn(false);
    const auto self = reinterpret_cast<uintptr_t>(this);
    const auto text = [desc](int type, const char *name, uint32_t style, uint32_t textId) {
        S_ControlDesc *control = desc->AddControl(type, name);
        AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, style, textId);
        return control;
    };
    text(CONTROL_TEXT, "text_screen", TITLE_STYLE, m_mode == KIND_DEBRIEFING_SCREEN ? TEXT_DEBRIEFING : TEXT_OPERATION_END);
    text(CONTROL_TEXT, "text_mission status", MISSION_STATUS_STYLE, TEXT_MISSION_STATUS);
    text(CONTROL_TEXT, "text_status", STATUS_STYLE, TEXT_STATUS);
    text(CONTROL_TEXT, "text_team status", STATUS_STYLE, TEXT_TEAM_STATUS);
    desc->AddControl(CONTROL_LABEL, "text_objectives");
    DescribeSoldiers(desc, model);
    S_ControlDesc *scroll = desc->AddControl(CONTROL_SCROLL, "scroll00");
    AddDescMessage(scroll, 0, CONTROL_MSG_SET_COUNT, static_cast<uint32_t>(g_gameProfile.m_missionSquad->states.size()));
    AddDescMessage(scroll, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CHANGED, self,
                   reinterpret_cast<uintptr_t>(&C_DebriefingMenu::OnSoldierScroll));
    // Saving and going on: the mission won, the campaign's.
    S_ControlDesc *save = text(CONTROL_BUTTON, "bsave and continue", BUTTON_STYLE, TEXT_SAVE_CONTINUE);
    AddDescMessage(save, 0, CONTROL_NOTIFY_CLICK | DEBRIEF_MSG_CONTINUE, 0);
    if (!IsMissionWon() || !IsCampaign())
        AddDescMessage(save, 0, CONTROL_MSG_DEACTIVATE, 0);
    // Retrying: the campaign's mission restarted, a single mission played again (not at an operation's end).
    S_ControlDesc *retry = text(CONTROL_BUTTON, "bretry immediately", BUTTON_STYLE, TEXT_RETRY);
    AddDescMessage(retry, 0,
                   m_mode == KIND_OPERATION_END_SCREEN ? CONTROL_MSG_DEACTIVATE
                   : IsCampaign()                     ? CONTROL_NOTIFY_CLICK | DEBRIEF_MSG_RESTART
                                                      : CONTROL_NOTIFY_CLICK | DEBRIEF_MSG_REPLAY,
                   0);
    // The diary (the campaign's): its page, its screen opened.
    S_ControlDesc *diary = text(CONTROL_BUTTON, "bload", BUTTON_STYLE, TEXT_DIARY);
    AddDescMessage(diary, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,
                   reinterpret_cast<uintptr_t>(&C_DebriefingMenu::OnDiaryPage));
    AddDescMessage(diary, 0, CONTROL_NOTIFY_CLICK | MENU_MSG_OPEN_SCREEN, DIARY_SCREEN_INDEX);
    if (!IsCampaign())
        AddDescMessage(diary, 0, CONTROL_MSG_DEACTIVATE, 0);
    AddDescMessage(text(CONTROL_BUTTON, "bexit to main menu", BUTTON_STYLE, TEXT_EXIT_MAIN), 0,
                   CONTROL_NOTIFY_CLICK | MENU_MSG_ESCAPE, 0);
    text(CONTROL_TEXT, "text_h&dversion", VERSION_STYLE, TEXT_VERSION);
    return true;
}

// ---------------------------------------------------------------------------
// Part 9g-vii: the soldiers' panel

namespace {

// The medals (S_SoldierProfile::unknown28's ids 0 .. 11): their two frames' names (_a, _b).
const char *const MEDALS[DEBRIEF_MEDALS] = {"DSO",           "DCM",           "MILIT_MEDAL",    "GSM_AFRICA",
                                            "GSM",           "WAR_MEDAL",     "DEFENCE_MEDAL",  "MILITARY_CROSS",
                                            "VICTORIA_CROSS", "GERM_FRANCE_STAR", "AFRICA_STAR", "BURMA_STAR"};
// The ranks (S_SoldierProfile::unknown24, 0 .. 7): their badges.
const char *const RANKS[DEBRIEF_RANKS] = {"rank_private",           "rank_corporal",   "rank_sergeant", "rank_StaffSergeant",
                                          "rank_second_lieutenant", "rank_lieutenant", "rank_captain",  "rank_major"};
// The skills' bars: their suffixes (the strength's in kg).
const char *const SKILL_BARS[7][2] = {{"hlcpctcr00", " %"}, {"hlcpctcr01", " Kg"}, {"hlcpctcr02", ""}, {"hlcpctcr03", ""},
                                      {"hlcpctcr04", " %"}, {"hlcpctcr05", " %"}, {"hlcpctcr06", " %"}};
constexpr int CONTROL_PICTURE = 4, CONTROL_GROUP = 5, CONTROL_SKILL_BAR = 0x17;
// "Soldier stats :"-like titles: the soldier's stats, the selected soldier's, the decorations, the new one;
// the skills' names (0xA6E + the skill).
constexpr uint32_t TEXT_SOLDIER_STATS = 0xACD, TEXT_SELECTED_STATS = 0xAD2, TEXT_DECORATIONS = 0xAD3,
                   TEXT_NEW_DECORATION = 0xAD4, TEXT_SKILLS = 0xA6E;
constexpr uint32_t BAR_AXIS_Z = 2;

uintptr_t FloatValue(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

}   // namespace

void C_DebriefingMenu::DescribeSoldiers(S_ScreenDesc *desc, I3D_frame *model)
{
    S_ControlDesc *panel = desc->AddControl(CONTROL_GROUP, "panel_soldier00");
    AddDescMessage(panel->AddChild(CONTROL_PICTURE, "soldier00"), 0, CONTROL_MSG_SET_TEXTURE, 0, 0);
    panel->AddChild(CONTROL_TEXT, "text_name00");
    AddDescMessage(panel->AddChild(CONTROL_TEXT, "text_soldierstats00"), 0, CONTROL_MSG_SET_TEXT_ID, STATUS_STYLE,
                   TEXT_SOLDIER_STATS);
    panel->AddChild(CONTROL_TEXT, "text_new medal");
    AddDescMessage(panel->AddChild(CONTROL_TEXT, "text_selected soldier stats"), 0, CONTROL_MSG_SET_TEXT_ID, STATUS_STYLE,
                   TEXT_SELECTED_STATS);
    AddDescMessage(panel->AddChild(CONTROL_TEXT, "text_decorations"), 0, CONTROL_MSG_SET_TEXT_ID, STATUS_STYLE,
                   TEXT_DECORATIONS);
    AddDescMessage(panel->AddChild(CONTROL_TEXT, "text_new decoration"), 0, CONTROL_MSG_SET_TEXT_ID, STATUS_STYLE,
                   TEXT_NEW_DECORATION);
    for (int i = 0; i < 7; ++i) {
        S_ControlDesc *bar = panel->AddChild(CONTROL_SKILL_BAR, SKILL_BARS[i][0]);
        AddDescMessage(bar, 0, CONTROL_MSG_SET_BAR_AXIS, BAR_AXIS_Z);
        AddDescMessage(bar, 0, CONTROL_MSG_SET_TEXT_ID, STATUS_STYLE, TEXT_SKILLS + static_cast<uint32_t>(i));
        AddDescMessage(bar, 0, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(0.5f));
        AddDescMessage(bar, 0, CONTROL_MSG_SET_BAR_VALUE, 1, FloatValue(0.7f));
        AddDescMessage(bar, 0, CONTROL_MSG_SET_BAR_VALUE, 2, reinterpret_cast<uintptr_t>(SKILL_BARS[i][1]));
    }
    // The model's frames the soldier shown uses.
    for (int i = 0; i < DEBRIEF_MEDALS; ++i) {
        m_medalFrames[i][0] = model->FindChildFrame(C_fstr("%s_a", MEDALS[i]), FRAME_MASK_VISUAL);
        m_medalFrames[i][1] = model->FindChildFrame(C_fstr("%s_b", MEDALS[i]), FRAME_MASK_VISUAL);
    }
    for (int i = 0; i < DEBRIEF_MEDALS; ++i)
        m_medalSlots[i] = model->FindChildFrame(C_fstr("medal%02d", i + 1), FRAME_MASK_DUMMY);
    m_newMedalSlot = model->FindChildFrame("new_medal", FRAME_MASK_DUMMY);
    for (int i = 0; i < DEBRIEF_RANKS; ++i)
        m_rankFrames[i] = model->FindChildFrame(RANKS[i], FRAME_MASK_VISUAL);
    m_rankSlot = model->FindChildFrame("soldier_rank", FRAME_MASK_DUMMY);
    m_deathFrame = model->FindChildFrame("death", FRAME_MASK_VISUAL);
}

namespace {

// The objectives' text (the debriefing's "text_objectives") and the mission's status.
constexpr uint32_t OBJECTIVES_TEXT = 0x4500000, MISSION_STATUS_TEXT = 0x4200000;
// "Tasks: ", "Recommendation:", "ACCOMPLISHED", "FAILED"; the lines' styles (the marks', the texts').
constexpr uint32_t TEXT_TASKS = 0xACF, TEXT_RECOMMENDATION = 0xAD0, TEXT_ACCOMPLISHED = 0x7DA, TEXT_FAILED = 0x7DC;
constexpr uint32_t MARK_STYLE = 0x55, OBJECTIVE_STYLE = 0x29, GAP_STYLE = 0xB;
// The music after the mission: won, lost.
constexpr int MUSIC_WON = 1, MUSIC_LOST = 2;

void PostText(uint32_t target, uint32_t style, const char *text)
{
    S_ControlMessage message;
    message.target = target;
    message.id = CONTROL_MSG_ADD_TEXT;
    message.value = style;
    message.text = C_fstr("%s", text);
    PostControlMessage(message);
}

}   // namespace

bool C_DebriefingMenu::ListObjectives()
{
    C_MissionDef *mission = g_gameProfile.m_campaign->GetCurrentMission();
    if (!mission)
        return false;
    // The completed ones under "Tasks: " ("= "), the others but the inactive under "Recommendation:"
    // ("! ", a blank line between).
    bool listed = false, headed = false;
    for (int i = 0; i < mission->GetObjectiveCount(); ++i) {
        const S_MissionObjective *objective = mission->GetObjective(i);
        if (objective->status != OBJECTIVE_COMPLETED)
            continue;
        if (!headed) {
            Post(OBJECTIVES_TEXT, CONTROL_MSG_ADD_TEXT_ID, OBJECTIVE_STYLE, TEXT_TASKS);
            headed = true;
        }
        PostText(OBJECTIVES_TEXT, MARK_STYLE, "=");
        PostText(OBJECTIVES_TEXT, OBJECTIVE_STYLE, " ");
        Post(OBJECTIVES_TEXT, CONTROL_MSG_ADD_TEXT_ID, OBJECTIVE_STYLE, static_cast<uint32_t>(objective->GetId()));
        listed = true;
    }
    headed = false;
    for (int i = 0; i < mission->GetObjectiveCount(); ++i) {
        const S_MissionObjective *objective = mission->GetObjective(i);
        if (objective->status == OBJECTIVE_COMPLETED || objective->status == OBJECTIVE_INACTIVE)
            continue;
        if (!headed) {
            if (listed)
                PostText(OBJECTIVES_TEXT, GAP_STYLE, " ");
            Post(OBJECTIVES_TEXT, CONTROL_MSG_ADD_TEXT_ID, OBJECTIVE_STYLE, TEXT_RECOMMENDATION);
            headed = true;
        }
        PostText(OBJECTIVES_TEXT, MARK_STYLE, "!");
        PostText(OBJECTIVES_TEXT, OBJECTIVE_STYLE, " ");
        Post(OBJECTIVES_TEXT, CONTROL_MSG_ADD_TEXT_ID, OBJECTIVE_STYLE, static_cast<uint32_t>(objective->GetId()));
    }
    // Won: a tutorial, or someone of the squad alive (the last first).
    if (mission->IsTutorial())
        return true;
    const std::vector<S_SquadSoldier *> &states = g_gameProfile.m_missionSquad->states;
    for (size_t i = states.size(); i-- > 0;)
        if (!g_gameProfile.m_humanStore->Get(states[i]->profileIndex)->dead)
            return true;
    return false;
}

void C_DebriefingMenu::ShowMissionResult(S_ScreenDesc *, I3D_frame *)
{
    Post(OBJECTIVES_TEXT, CONTROL_MSG_CLEAR, 0);
    // (The original's copy protection is left out: "FAILED" for a cracked copy from the sixteenth mission.)
    const bool won = m_mode == KIND_OPERATION_END_SCREEN || ListObjectives();
    Post(MISSION_STATUS_TEXT, CONTROL_MSG_SET_TEXT_ID, MISSION_STATUS_STYLE, won ? TEXT_ACCOMPLISHED : TEXT_FAILED);
    GetSoundModule()->SetBackgroundMusic(won ? MUSIC_WON : MUSIC_LOST, 0);
    GetSoundModule()->FadeBackgroundMusic(1.0f, 2000);
}
