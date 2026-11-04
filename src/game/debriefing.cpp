// The mission's debriefing (game/debriefing.h, part 9g-v).
#include "game/debriefing.h"

#include <cstdlib>

#include "game/campaign.h"
#include "game/game_app.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "game/soldier.h"

const int g_rankExperience[7] = {10000, 25000, 50000, 80000, 115000, 160000, 210000};

namespace {

// The debriefing's messages (target 0).
enum : uint32_t {
    DEBRIEF_MSG_CONTINUE = 0x2000034,
    DEBRIEF_MSG_RESTART = 0x2000075,
    DEBRIEF_MSG_REPLAY = 0x200007C,
    DEBRIEF_MSG_SAVE_QUIT = 0x20000C8,
};
// The screens (their ids' top 6 bits).
constexpr uint32_t DEBRIEFING_SCREEN = 1, DIARY_SCREEN = 2;
constexpr uint32_t SCREEN_SHIFT = 26, SCREEN_MASK = 0x3F;
// "ARE YOU SURE?", "Do you want to save the game?", "Overwrite current saved game? ..".
constexpr uint32_t TEXT_SURE = 0x7D4, TEXT_SAVE = 0x2533, TEXT_OVERWRITE = 0x2534;
constexpr int MESSAGE_QUESTION = 1;
// The campaign screen kind of the debriefing.
constexpr int KIND_DEBRIEFING = 5;
// The first campaign's operations with their medals; the medals.
constexpr int FIRST_CAMPAIGN = 0;
constexpr int RANKS = 8;
// The roster soldiers looked at (the first 30).
constexpr int ROSTER_LIMIT = 30;
// The squad's progress's random share for the roster: 60 .. 140 %.
constexpr float RAND_SCALE = 3.051851e-05f;

void Post(uint32_t id, uint32_t value)
{
    S_ControlMessage message;
    message.id = id;
    message.value = value;
    PostControlMessage(message);
}

S_SquadSoldier *SquadSoldier(int index)
{
    std::vector<S_SquadSoldier *> &states = g_gameProfile.m_missionSquad->states;
    return index >= 0 && static_cast<size_t>(index) < states.size() ? states[static_cast<size_t>(index)] : nullptr;
}

// The rank at least `rank`.
bool RankAtLeast(const S_SoldierProfile *profile, int rank) { return profile->unknown24 >= rank; }

// A stat's gain added.
void AddStat(S_SoldierProfile *profile, int stat, float gain)
{
    profile->stats[stat] = gain + profile->stats[stat];
}

}   // namespace

bool C_DebriefingMenu::IsCampaign()
{
    return g_gameProfile.m_playMode->singleMission == 0;
}

bool C_DebriefingMenu::OnStart(void *)
{
    // (The original's copy protection is left out.)
    C_Diary *diary = g_gameProfile.GetPlayerProfile()->GetDiary();
    if (IsCampaign()) {
        diary->CommitCurrent();
        if (m_mode == KIND_DEBRIEFING)
            ApplyResults();
    } else {
        diary->ClearCurrent();
    }
    return true;
}

void C_DebriefingMenu::OnMessage(S_ControlMessage *message)
{
    C_Campaign *campaign = g_gameProfile.m_campaign;
    // The campaign's debriefing kept: the results, the progress, the game saved.
    const auto keep = [this, campaign]() {
        const int current = campaign->GetCurrentIndex();
        const int mission = campaign->GetMissionNumber(current);
        if (m_mode == KIND_DEBRIEFING && mission != -1 && current != -1) {
            StoreProgress();
            g_gameProfile.GetPlayerProfile()->SetProgress(current, mission);
            GetGameApp()->SaveGame(g_gameProfile.GetSaveFileName(current, mission), false);
        }
    };
    switch (message->id & CONTROL_MSG_MASK) {
    case MENU_MSG_ESCAPE: {
        if (!g_menuScreens.empty() && g_menuScreens.back() &&
            (g_menuScreens.back()->GetId() >> SCREEN_SHIFT & SCREEN_MASK) == DIARY_SCREEN) {
            Post(MENU_MSG_BACK, 0);
            return;
        }
        if (!IsCampaign()) {
            ShowMessageId(MESSAGE_QUESTION, TEXT_SURE, MENU_MSG_MAIN_MENU, 0);
            return;
        }
        const int current = campaign->GetCurrentIndex();
        const int mission = campaign->GetMissionNumber(current);
        const int progress = g_gameProfile.GetPlayerProfile()->GetProgress(current);
        ShowMessageId(MESSAGE_QUESTION, mission < progress ? TEXT_OVERWRITE : TEXT_SAVE, DEBRIEF_MSG_SAVE_QUIT,
                      MENU_MSG_MAIN_MENU);
        return;
    }
    case DEBRIEF_MSG_CONTINUE:
        if (!IsCampaign())
            return;
        keep();
        Post(MENU_MSG_NEXT, 1);
        return;
    case DEBRIEF_MSG_SAVE_QUIT:
        if (!IsCampaign())
            return;
        keep();
        Post(MENU_MSG_MAIN_MENU, 0);
        return;
    case DEBRIEF_MSG_RESTART: {
        const int current = campaign->GetCurrentIndex();
        const int mission = campaign->GetMissionNumber(current);
        if (mission < 1) {
            g_gameProfile.ResetGame();
            if (campaign->AdvanceToMission())
                m_state = GAME_STATE_ENDED;
            return;
        }
        if (GetGameApp()->LoadGame(g_gameProfile.GetSaveFileName(current, mission - 1), false) &&
            campaign->AdvanceToMission())
            m_state = GAME_STATE_ENDED;
        return;
    }
    case DEBRIEF_MSG_REPLAY: {
        const int current = campaign->GetCurrentIndex();
        const int mission = campaign->GetMissionNumber(current);
        g_gameProfile.ResetGame();
        campaign->SetupSingleMission(current, mission);
        m_state = GAME_STATE_ENDED;
        return;
    }
    default:
        return;
    }
}

bool C_DebriefingMenu::BuildScreens()
{
    auto *menu = new S_MenuDesc();
    menu->AddScreen("debriefing", "debriefing");
    if (IsCampaign())
        menu->AddScreen("DIARY", "diary");
    for (S_ScreenDesc *desc : menu->screens) {
        I3D_frame *model = LoadModel(desc->name.c_str(), desc->file.c_str());
        if (!model)
            continue;
        if ((desc->id & 0xFC000000) == DEBRIEFING_SCREEN << SCREEN_SHIFT) {
            DescribeDebriefing(desc, model);
            ShowMissionResult(desc, model);
        }
        if ((desc->id & 0xFC000000) == DIARY_SCREEN << SCREEN_SHIFT)
            DescribeDiary(desc, model);
        auto *screen = new C_Screen(m_scene, model);
        // (Quirk kept: a screen not built is leaked.)
        if (screen->Build(desc)) {
            screen->HandleMessages(desc->messages);
            m_screens.push_back(screen);
        }
    }
    delete menu;
    S_ControlMessage open;
    open.id = MENU_MSG_OPEN_SCREEN;
    open.value = DEBRIEFING_SCREEN;
    HandleMessage(&open);
    ShowSoldier(nullptr);
    return true;
}

void C_DebriefingMenu::FindMedal8(bool *alive, bool *dead)
{
    *dead = false;
    *alive = false;
    C_HumanStore *store = g_gameProfile.m_humanStore;
    for (int i = 0; i < store->GetCount() && i < ROSTER_LIMIT; ++i) {
        const S_SoldierProfile *profile = store->Get(i);
        if (!profile || !profile->HasUnknown28(8))
            continue;
        if (profile->dead)
            *dead = true;
        else
            *alive = true;
    }
}

void C_DebriefingMenu::AwardMedal(S_SoldierProfile *profile, S_SquadSoldier *soldier)
{
    C_Campaign *campaign = g_gameProfile.m_campaign;
    const int current = campaign->GetCurrentIndex();
    const int operation = campaign->GetOperationIndex(current);
    int &medal = soldier->unknown38;
    if (current == FIRST_CAMPAIGN && !profile->dead) {
        if ((operation == 5 || operation == 6) && !profile->HasUnknown28(9)) {
            medal = 9;
            return;
        }
        if (operation == 4) {
            if (!profile->HasUnknown28(11)) {
                medal = 11;
                return;
            }
        } else if (operation == 2) {
            if (!profile->HasUnknown28(10)) {
                medal = 10;
                return;
            }
            if (!profile->HasUnknown28(3)) {
                bool due = false;
                switch (profile->unknown40) {
                case 2: due = RankAtLeast(profile, 3); break;
                case 3: due = RankAtLeast(profile, 2); break;
                case 4: due = RankAtLeast(profile, 1); break;
                case 5: due = RankAtLeast(profile, 0); break;
                default: break;
                }
                if (due) {
                    medal = 3;
                    return;
                }
            }
        }
    }
    const int experience = soldier->progress.experience;
    if (experience > 14999) {
        // One living holder at a time (a dead soldier's: one dead holder).
        bool alive = false, dead = false;
        FindMedal8(&alive, &dead);
        if (!(profile->dead ? dead : alive))
            medal = 8;
    }
    if (profile->dead)
        return;
    if (experience > 11999 && !profile->HasUnknown28(7)) {
        medal = 7;
        return;
    }
    if (experience > 10999 && !profile->HasUnknown28(2)) {
        medal = 2;
        return;
    }
    if (experience > 9999 && !profile->HasUnknown28(0)) {
        medal = 0;
        return;
    }
    if (experience > 8999 && !profile->HasUnknown28(1)) {
        medal = 1;
        return;
    }
    if (!profile->HasUnknown28(5)) {
        bool due = false;
        switch (profile->unknown40) {
        case 1: due = RankAtLeast(profile, 3); break;
        case 2: due = RankAtLeast(profile, 2); break;
        case 3: due = RankAtLeast(profile, 1); break;
        case 4: due = RankAtLeast(profile, 0); break;
        default: break;
        }
        if (due)
            medal = 5;
    }
    if (operation != 2 && !profile->HasUnknown28(4)) {
        bool due = false;
        switch (profile->unknown3C) {
        case 5: due = RankAtLeast(profile, 3); break;
        case 6: due = RankAtLeast(profile, 2); break;
        case 7: due = RankAtLeast(profile, 1); break;
        case 8: due = RankAtLeast(profile, 0); break;
        default: break;
        }
        if (due)
            medal = 4;
    }
}

void C_DebriefingMenu::ApplyResults()
{
    // (The original's copy protection is left out: the promotions and medals given, the progress kept.)
    C_Campaign *campaign = g_gameProfile.m_campaign;
    const int mission = campaign->GetMissionNumber(campaign->GetCurrentIndex());
    std::vector<S_SquadSoldier *> &states = g_gameProfile.m_missionSquad->states;
    for (size_t i = 0; i < states.size(); ++i) {
        S_SquadSoldier *soldier = SquadSoldier(static_cast<int>(i));
        S_SoldierProfile *profile = g_gameProfile.m_humanStore->Get(soldier->profileIndex);
        if (profile->dead)
            SquadSoldier(static_cast<int>(i))->ReturnItems();
        profile->unknown38 += soldier->progress.experience;
        ++profile->unknown3C;
        ++profile->unknown40;
        const int next = profile->unknown24 + 1;
        if (next < RANKS && g_rankExperience[profile->unknown24] < profile->unknown38)
            soldier->unknown34 = next;
        if (mission > 0)
            AwardMedal(profile, soldier);
    }
}

void C_DebriefingMenu::StoreProgress()
{
    S_SoldierProgress sum;
    sum.unknown00 = sum.unknown04 = sum.endurance = sum.morale = sum.shooting = sum.stealth = sum.firstAid =
        sum.lockpick = 0.0f;
    sum.experience = 0;
    int living = 0;
    C_HumanStore *store = g_gameProfile.m_humanStore;
    std::vector<S_SquadSoldier *> &states = g_gameProfile.m_missionSquad->states;
    const auto cap = [](S_SoldierProfile *profile) {
        for (int stat : {1, 2, 3, 4, 6, 7, 8})
            if (profile->stats[stat] > 100.0f)
                profile->stats[stat] = 100.0f;
    };
    for (size_t i = 0; i < states.size(); ++i) {
        S_SquadSoldier *soldier = SquadSoldier(static_cast<int>(i));
        S_SoldierProfile *profile = store->Get(soldier->profileIndex);
        if (soldier->unknown34 != -1) {
            profile->unknown24 = soldier->unknown34;
            soldier->unknown34 = -1;
        }
        if (soldier->unknown38 != -1) {
            profile->unknown28.push_back(soldier->unknown38);
            soldier->unknown38 = -1;
        }
        const S_SoldierProgress &p = soldier->progress;
        AddStat(profile, 0, p.unknown00);
        AddStat(profile, 1, p.unknown04);
        AddStat(profile, 2, p.endurance);
        AddStat(profile, 3, p.morale);
        AddStat(profile, 4, p.shooting);
        AddStat(profile, 6, p.stealth);
        AddStat(profile, 7, p.firstAid);
        AddStat(profile, 8, p.lockpick);
        cap(profile);
        if (!profile->dead) {
            sum.unknown04 += p.unknown04;
            ++living;
            sum.experience += p.experience;
            sum.endurance += p.endurance;
            sum.morale += p.morale;
            sum.shooting += p.shooting;
            sum.stealth += p.stealth;
            sum.firstAid += p.firstAid;
            sum.lockpick += p.lockpick;
        }
    }
    // The dead taken off the squad (the last first).
    for (size_t i = states.size(); i-- > 0;) {
        if (!store->Get(states[i]->profileIndex)->dead)
            continue;
        delete states[i];
        states.erase(states.begin() + static_cast<long>(i));
    }
    if (living == 0)
        return;
    // The living's average (the first stat's never summed: 0).
    const float share = 1.0f / static_cast<float>(living);
    sum.experience /= living;
    sum.unknown04 *= share;
    sum.endurance *= share;
    sum.morale *= share;
    sum.shooting *= share;
    sum.stealth *= share;
    sum.firstAid *= share;
    sum.lockpick = share * sum.lockpick;
    // A random 60 .. 140 % of it (the factor in the original's FPU precision: a float).
    const auto factor = []() {
        const float r = static_cast<float>(std::rand()) * RAND_SCALE * 0.8f;
        return static_cast<float>(static_cast<double>(r) + 0.6);
    };
    for (int i = 0; i < store->GetCount() && i < ROSTER_LIMIT; ++i) {
        if (store->Get(i)->dead)
            continue;
        S_SoldierProfile *profile = store->Get(i);
        const float gain = factor() * static_cast<float>(sum.experience);
        profile->unknown38 = static_cast<int>(gain + static_cast<float>(profile->unknown38));
        const float averages[8] = {sum.unknown00, sum.unknown04, sum.endurance, sum.morale,
                                   sum.shooting, sum.stealth, sum.firstAid, sum.lockpick};
        const int stats[8] = {0, 1, 2, 3, 4, 6, 7, 8};
        for (int k = 0; k < 8; ++k)
            AddStat(profile, stats[k], factor() * averages[k]);
        cap(profile);
    }
}
