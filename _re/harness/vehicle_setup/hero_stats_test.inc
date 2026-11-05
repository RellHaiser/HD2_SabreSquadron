// Part 9i-i's hero stats menu (game/hero_stats_menu.cpp), in the factory build, run by ControlsTest after
// MainMenuTest (its controls answering every message, C_BwControl). Prints "herostats:" with 1 per check met.
#define private public
#define protected public
#include "game/hero_stats_menu.h"
#undef protected
#undef private

struct S_HeroStatsResults {
    bool hsType = true, hsValue = true, hsEscape = true, hsAccept = true, hsQuit = true, hsHero = true, hsSpec = true,
         hsSkills = true, hsRow = true, hsBuild = true;
};

void HeroStatsTest(I3D_scene *)
{
    S_HeroStatsResults r;
    void *const oldFind = g_mrModelVt[0x48 / 4], *const oldCreate = g_driverVtable[0x54 / 4];
    g_mrModelVt[0x48 / 4] = reinterpret_cast<void *>(&MsFind);
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&MrCreateFrame);
    // (A model's AddModelFrame logged.)
    void *const oldAddFrame = g_mrModelVt[0x7C / 4];
    g_mrModelVt[0x7C / 4] = reinterpret_cast<void *>(&MrFake<0x7C, void *>);
    const auto Is = [](const std::string &got, const std::string &want) {
        if (got != want)
            std::printf("HEROSTATS %s\nHEROSTATS expected %s\n", got.c_str(), want.c_str());
        return got == want;
    };
    g_controlMessages.clear();
    g_gfLog.clear();
    C_HeroStatsMenu m;
    WM(r.hsType, m.GetTypeId() == GAME_STATE_TYPE_HERO_STATS_MENU && GAME_STATE_TYPE_HERO_STATS_MENU == 9);

    // The hero screen with the skill rows' controls (their numbers 10 + the row).
    C_MsScreen hero(nullptr, "hsscreen", 1u << 26), quit(nullptr, "hsquit", 4u << 26);
    // (The rows under their list, as FindControl walks the ids.)
    C_BwControl skillList(0x4500000, &hero, 0);
    hero.m_children.push_back(&skillList);
    std::vector<C_BwControl *> rows;
    for (uint32_t i = 1; i <= 18; ++i) {
        rows.push_back(new C_BwControl(0x4500000 | i << 12, &skillList, 10 + i));
        skillList.m_children.push_back(rows.back());
    }
    {
        // A top screen's control's number; none, no screen: -1.
        bool ok = true;
        g_menuScreens = {&hero};
        g_gfLog.clear();
        ok &= C_HeroStatsMenu::GetSkillValue(0x4503000) == 13 && MsLogIs({"bw 4503000 1000079"}, g_gfLog);
        ok &= C_HeroStatsMenu::GetSkillValue(0x4513000) == -1;
        g_menuScreens = {&hero, &quit};
        ok &= C_HeroStatsMenu::GetSkillValue(0x4503000) == -1;
        g_menuScreens.clear();
        ok &= C_HeroStatsMenu::GetSkillValue(0x4503000) == -1;
        WM(r.hsValue, ok);
    }
    {
        // Escape: on the quit question: no, closed; else (none, another) the question, the previous screen kept back.
        bool ok = true;
        S_ControlMessage message;
        message.id = 0x4000000 | MENU_MSG_ESCAPE;
        g_menuScreens = {&hero, &quit};
        m.OnMessage(&message);
        ok &= Is(CsQueue(), "0 14001200 1 0; 0 6001005 0 0; ");
        g_menuScreens = {&quit, &hero};
        m.OnMessage(&message);
        ok &= Is(CsQueue(), "0 2000002 4 0; 0 22000007 0 0; ");
        g_menuScreens.clear();
        m.OnMessage(&message);
        ok &= Is(CsQueue(), "0 2000002 4 0; 0 22000007 0 0; ");
        message.id = 0x2000050;
        m.OnMessage(&message);
        ok &= CsQueue() == "";
        WM(r.hsEscape, ok);
    }
    {
        // Accept: the rows' numbers into the first soldier's stats (rows 1, 2, 5, 6, 7, 10, 11, 14, 15), the next
        // step; an empty roster loaded first (failing: nothing).
        bool ok = true;
        alignas(16) static uint8_t hsStoreMemory[64];
        C_HumanStore *const oldStore = g_gameProfile.m_humanStore;
        g_gameProfile.m_humanStore = reinterpret_cast<C_HumanStore *>(hsStoreMemory);
        const std::vector<S_SoldierProfile *> oldProfiles = g_epProfiles;
        alignas(16) static uint8_t hsProfileMemory[sizeof(S_SoldierProfile)];
        std::memset(static_cast<void *>(hsProfileMemory), 0, sizeof hsProfileMemory);
        auto *soldier = reinterpret_cast<S_SoldierProfile *>(hsProfileMemory);
        for (float &stat : soldier->stats)
            stat = -5.0f;
        g_epProfiles = {soldier};
        g_menuScreens = {&hero};
        S_ControlMessage message;
        message.id = 0x4000000 | 0x200006A;
        g_gfLog.clear();
        m.OnMessage(&message);
        const float want[9] = {11, 12, 15, 21, 17, 20, 16, 25, 24};
        for (int i = 0; i < 9; ++i)
            ok &= soldier->stats[i] == want[i];
        ok &= Is(CsQueue(), "0 2000008 0 0; ") && g_gfLog.size() == 9 && g_gfLog[0] == "bw 4501000 1000079";
        // (A row missing: -1.)
        skillList.m_children.erase(skillList.m_children.begin());
        m.OnMessage(&message);
        ok &= soldier->stats[0] == -1.0f && soldier->stats[1] == 12.0f && CsQueue() == "0 2000008 0 0; ";
        skillList.m_children.insert(skillList.m_children.begin(), rows[0]);
        // Empty: loaded (the load's roster still empty here: Get(0) none), failing: nothing.
        g_epProfiles.clear();
        g_esRosterLoads = false;
        g_gfLog.clear();
        m.OnMessage(&message);
        ok &= MsLogIs({"roster load tables\\HumanStore.def"}, g_gfLog) && CsQueue() == "";
        g_esRosterLoads = true;
        g_epProfiles = oldProfiles;
        g_gameProfile.m_humanStore = oldStore;
        g_menuScreens.clear();
        WM(r.hsAccept, ok);
    }
    void *const oldSetPos = g_msFrameVt[0x08 / 4];
    g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
    {
        // The quit question: the model placed; the text, yes (posted), no (dropped), both closing.
        bool ok = true;
        S_MrObj *model = MsFrame("hsqmodel");
        S_ScreenDesc d("CONFIRM", "confirm", 4u << 26);
        g_gfLog.clear();
        ok &= C_HeroStatsMenu::DescribeQuitQuestion(&d, As<I3D_frame>(model)) &&
              MsLogIs({"hsqmodel 8 (0.5 -0.375 -0.5)"}, g_gfLog) && d.controls.size() == 3 && d.messages.empty();
        if (d.controls.size() == 3)
            ok &= Is(CsTree(d.controls[0]), "13 text 10100000: 0 1000015 c 7d4;") &&
                  Is(CsTree(d.controls[1]), "14 byes* 10200000: 0 1000015 d 7d1 0 14001100 0 0 0 6001005 0 0;") &&
                  Is(CsTree(d.controls[2]), "14 bno* 10300000: 0 1000015 d 7d0 0 14001200 1 0 0 6001005 0 0;");
        WM(r.hsQuit, ok);
    }
    // The hero screen's model and its frames.
    S_MrObj *model = MrObj("hsmodel", g_mrModelVt);
    const char *const specNames[] = {"bsoldier", "bheavysoldier", "bsniper", "bmedic", "bengineer"};
    S_MrObj *spec[5];
    for (int i = 0; i < 5; ++i) {
        spec[i] = MsFrame(strdup(CmText("hss%d", i).c_str()));
        g_msChildren[{model, specNames[i]}] = spec[i];
    }
    S_MrObj *skillRow = MsFrame("hsrow");
    As<I3D_frame>(skillRow)->m_localMatrix(3) = S_vector(0.25f, -0.5f, 0.75f);
    g_msChildren[{model, "skill01"}] = skillRow;
    {
        // The specialization: its buttons into a new group under the model; its list.
        bool ok = true;
        S_MrObj *group = MsFrame("hsspec");
        g_mrMade[FRAME_DUMMY] = {group};
        S_ScreenDesc d("HERO STATS", "heros_screen", 1u << 26);
        g_gfLog.clear();
        ok &= m.DescribeSpecialization(&d, As<I3D_frame>(model)) && m.m_models.size() == 1 &&
              m.m_models[0] == As<I3D_frame>(group) && d.controls.size() == 1;
        if (d.controls.size() == 1)
            ok &= Is(CsTree(d.controls[0]), "b GRSkillSpec 4100000: 0 1000028 14 0;");
        const std::string log = Join(g_gfLog);
        ok &= GmInOrder(log, {"hsspec 38 hsmodel 0", "hsmodel 7c hsspec", "hsspec 8 (0 0 0)", "hsspec 34 GRSkillSpec",
                              "hss0 38 hsspec 1", "hss1 38 hsspec 1", "hss2 38 hsspec 1", "hss3 38 hsspec 1",
                              "hss4 38 hsspec 1", "hsspec on 1", "hsspec 24"});
        if (!ok)
            std::printf("HEROSTATS spec log %s\n", log.c_str());
        // A button missing (or no group): false, nothing more.
        g_msChildren.erase({model, "bmedic"});
        g_mrMade[FRAME_DUMMY] = {MsFrame("hsspec2")};
        S_ScreenDesc d2("HERO STATS", "heros_screen", 1u << 26);
        ok &= !m.DescribeSpecialization(&d2, As<I3D_frame>(model)) && d2.controls.empty() && m.m_models.size() == 1;
        g_msChildren[{model, "bmedic"}] = spec[3];
        g_msChildren.erase({model, "bsoldier"});
        g_mrMade[FRAME_DUMMY] = {MsFrame("hsspec4")};
        S_ScreenDesc d4("HERO STATS", "heros_screen", 1u << 26);
        ok &= !m.DescribeSpecialization(&d4, As<I3D_frame>(model)) && d4.controls.empty() && m.m_models.size() == 1;
        g_msChildren[{model, "bsoldier"}] = spec[0];
        g_mrMade[FRAME_DUMMY].clear();
        S_ScreenDesc d3("HERO STATS", "heros_screen", 1u << 26);
        ok &= !m.DescribeSpecialization(&d3, As<I3D_frame>(model)) && d3.controls.empty();
        m.m_models.clear();
        WM(r.hsSpec, ok);
    }
    {
        // A skill row: "skill01" copied (named, `index` steps down) into GRSkills; its control.
        bool ok = true;
        S_MrObj *group = MsFrame("hsgroup"), *copy = MsFrame("hscopy");
        g_msChildren[{model, "GRSkills"}] = group;
        g_mrClones = {As<I3D_frame>(copy)};
        S_ControlDesc list(0xB, "GRSkills", 0x4500000);
        g_gfLog.clear();
        m.AddSkillRow(&list, As<I3D_frame>(model), C_str("Might"), 3, 0.02f);
        const S_vector &pos = As<I3D_frame>(copy)->GetPos();
        ok &= m.m_frames.size() == 1 && m.m_frames[0] == As<I3D_frame>(copy) && pos.x == 0.25f &&
              pos.y == -0.5f - 3.0f * 0.02f && pos.z == 0.75f && list.children.size() == 1;
        if (list.children.size() == 1)
            ok &= Is(CsTree(list.children[0]), "19 Might 4501000: 0 1000014 b 0 'Might' 0 100000b 4300000 0;");
        const std::string log = Join(g_gfLog);
        ok &= GmInOrder(log, {"clone tree hsrow 0", "hscopy 34 Might", "hscopy 38 hsgroup 0", "hscopy on 1", "hscopy 24"});
        if (!ok)
            std::printf("HEROSTATS row log %s\n", log.c_str());
        // No group or no row: nothing.
        g_msChildren.erase({model, "GRSkills"});
        m.AddSkillRow(&list, As<I3D_frame>(model), C_str("X"), 0, 0.02f);
        g_msChildren[{model, "GRSkills"}] = group;
        g_msChildren.erase({model, "skill01"});
        m.AddSkillRow(&list, As<I3D_frame>(model), C_str("X"), 0, 0.02f);
        g_msChildren[{model, "skill01"}] = skillRow;
        ok &= m.m_frames.size() == 1 && list.children.size() == 1;
        g_msChildren.erase({model, "GRSkills"});
        m.m_frames.clear();
        WM(r.hsRow, ok);
    }
    {
        // The skills: the row into a new group GRSkills, off; the list (20 shown, 0.02 apart, the row's place);
        // a row a skill; the total's 20 and the skills' bases posted.
        bool ok = true;
        S_MrObj *group = MsFrame("hsskills");
        g_mrMade[FRAME_DUMMY] = {group};
        g_msChildren[{model, "GRSkills"}] = group;
        g_mrClones.clear();
        std::vector<S_MrObj *> copies;
        for (int i = 0; i < 18; ++i) {
            copies.push_back(MsFrame(strdup(CmText("hsc%d", i).c_str())));
            g_mrClones.push_back(As<I3D_frame>(copies.back()));
        }
        S_ScreenDesc d("HERO STATS", "heros_screen", 1u << 26);
        g_gfLog.clear();
        ok &= m.DescribeSkills(&d, As<I3D_frame>(model)) && m.m_models.size() == 1 && m.m_frames.size() == 18 &&
              d.controls.size() == 1;
        if (d.controls.size() == 1) {
            const S_ControlDesc *list = d.controls[0];
            const auto *origin = list->messages.size() == 3 ? reinterpret_cast<const S_vector *>(list->messages[2].value2) : nullptr;
            ok &= list->messages.size() == 3 && list->messages[2].value == 0 && list->messages[2].target == 0 && list->messages[2].id == 0x100006B;
            ok &= origin && origin->x == 0.25f && origin->y == -0.5f && origin->z == 0.75f && list->children.size() == 18;
            ok &= list->messages.size() == 3 && Is(CsMessage(list->messages[0]), "0 1000028 14 0") &&
                  Is(CsMessage(list->messages[1]), "0 100002a 0 3ca3d70a");
            const char *const names[18] = {"Health", "Might", "Reactions", "Inteligence", "Endurance", "Stealth",
                                           "Light Weapons", "Heavy Weapons", "Shooting Rifle", "Throwing", "Morale",
                                           "Driving", "Swimming", "Traps", "First Aid", "Pick Lock", "Pilot", "German"};
            for (size_t i = 0; i < list->children.size() && i < 18; ++i)
                ok &= list->children[i]->name == names[i] && list->children[i]->id == (0x4100000u | (i + 1) << 12);
            ok &= As<I3D_frame>(copies[17])->GetPos().y == -0.5f - 17.0f * 0.02f;
        }
        std::string want = "4300000 1000073 14 0; ";
        const uint32_t bases[18] = {80, 75, 60, 50, 75, 30, 80, 40, 75, 70, 80, 50, 80, 30, 70, 40, 50, 60};
        for (uint32_t i = 1; i <= 18; ++i)
            want += CmText("%x 1000072 %x 0; ", 0x4500000 | i << 12, bases[i - 1]);
        ok &= Is(CsQueue(), want);
        const std::string log = Join(g_gfLog);
        ok &= GmInOrder(log, {"hsskills 38 hsmodel 0", "hsmodel 7c hsskills", "hsskills 8 (0 0 0)", "hsskills 34 GRSkills",
                              "hsrow 38 hsskills 1", "hsrow on 0", "hsskills on 1", "hsrow 24", "hsskills 24",
                              "clone tree hsrow 0", "hsc0 34 Health", "hsc17 34 German"});
        if (!ok)
            std::printf("HEROSTATS skills log %s\n", log.c_str());
        // No row: false.
        g_msChildren.erase({model, "skill01"});
        g_mrMade[FRAME_DUMMY] = {MsFrame("hsskills2")};
        S_ScreenDesc d2("HERO STATS", "heros_screen", 1u << 26);
        ok &= !m.DescribeSkills(&d2, As<I3D_frame>(model)) && d2.controls.empty() && CsQueue() == "";
        g_msChildren[{model, "skill01"}] = skillRow;
        g_mrMade.erase(FRAME_DUMMY);
        m.m_models.clear();
        m.m_frames.clear();
        WM(r.hsSkills, ok);
    }
    {
        // The hero screen: the texts, the specialization, the skills, accept, back.
        bool ok = true;
        g_mrMade[FRAME_DUMMY] = {MsFrame("hshs1"), MsFrame("hshs2")};
        g_mrClones.clear();
        for (int i = 0; i < 18; ++i)
            g_mrClones.push_back(As<I3D_frame>(MsFrame(strdup(CmText("hsd%d", i).c_str()))));
        S_ScreenDesc d("HERO STATS", "heros_screen", 1u << 26);
        ok &= m.DescribeHeroScreen(&d, As<I3D_frame>(model)) && d.controls.size() == 7;
        std::string names;
        for (const S_ControlDesc *c : d.controls)
            names += CmText("%x %s %x|", c->type, c->name.c_str(), c->id);
        ok &= Is(names, "13 text_screen 4100000|13 text_specialization 4200000|19 skill00 4300000|b GRSkillSpec 4400000|"
                        "b GRSkills 4500000|14 baccept 4600000|14 bback 4700000|");
        if (d.controls.size() == 7)
            ok &= Is(CsTree(d.controls[0]), "13 text_screen 4100000: 0 1000015 a a29;") &&
                  Is(CsTree(d.controls[1]), "13 text_specialization 4200000: 0 1000015 b a2d;") &&
                  Is(CsTree(d.controls[2]), "19 skill00 4300000: 0 1000015 b a2e;") &&
                  Is(CsTree(d.controls[5]), "14 baccept 4600000: 0 1000015 d a2a 0 600106a 0 0;") &&
                  Is(CsTree(d.controls[6]), "14 bback 4700000: 0 1000015 d a2b 0 600100a 0 0;");
        CsQueue();
        g_mrMade.erase(FRAME_DUMMY);
        m.m_models.clear();
        m.m_frames.clear();
        WM(r.hsHero, ok);
    }
    g_msFrameVt[0x08 / 4] = oldSetPos;
    {
        // Built: the hero screen and three questions (their models; the hero's and the last described), made; the
        // hero screen opened.
        bool ok = true;
        void (*const oldFill)(I3D_model *) = g_cacheFill;
        g_cacheFill = [](I3D_model *) {};
        void *const oldModelSetPos = g_mrModelVt[0x08 / 4];
        g_mrModelVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
        S_MrWorld world = MrWorld("hsb");
        m.m_scene = As<I3D_scene>(world.scene);
        C_MsScreen box(m.m_scene, "hsbbox", 0);
        std::vector<S_MrObj *> models;
        for (int i = 1; i <= 4; ++i)
            models.push_back(MrObj(strdup(CmText("hsbm%d", i).c_str()), g_mrModelVt));
        m.m_screens = {&box};
        g_mrMade[FRAME_MODEL] = std::deque<S_MrObj *>(models.begin(), models.end());
        g_mrMade[FRAME_DUMMY] = {MsFrame("hsbg1"), MsFrame("hsbg2")};
        g_cacheLoads.clear();
        g_menuScreens.clear();
        g_gfLog.clear();
        ok &= m.BuildScreens() &&
              g_cacheLoads == std::vector<std::string>({"heros_screen", "confirm", "confirm", "confirm"}) &&
              m.m_screens.size() == 5 && !g_menuScreens.empty() && g_menuScreens.back() == m.m_screens[1] &&
              m.m_screens[1]->GetScreenFrame() == As<I3D_frame>(models[0]) &&
              m.m_screens[4]->GetScreenFrame() == As<I3D_frame>(models[3]);
        const std::string log = Join(g_gfLog);
        // (Only the hero's model described as the hero's: its two groups asked for.)
        size_t creates = 0;
        for (size_t at = log.find("create 6"); at != std::string::npos; at = log.find("create 6", at + 1))
            ++creates;
        ok &= creates == 2;
        // (Only the last model placed: the quit question.)
        ok &= log.find("hsbm4 8 (0.5 -0.375 -0.5)") != std::string::npos && log.find("hsbm2 8") == std::string::npos &&
              log.find("hsbm3 8") == std::string::npos && log.find("hsbm1 8") == std::string::npos;
        if (!ok)
            std::printf("HEROSTATS build log %s\n", log.c_str());
        for (size_t i = 1; i < m.m_screens.size(); ++i)
            delete m.m_screens[i];
        // A model not loaded: its screen skipped (the next ones made).
        m.m_screens = {&box};
        m.m_models.clear();
        g_mrMade[FRAME_MODEL] = std::deque<S_MrObj *>(models.begin(), models.end());
        g_cacheFailName = "heros_screen";
        g_menuScreens.clear();
        ok &= m.BuildScreens() && m.m_screens.size() == 4 && m.m_screens[1]->GetScreenFrame() != As<I3D_frame>(models[0]);
        g_cacheFailName.clear();
        for (size_t i = 1; i < m.m_screens.size(); ++i)
            delete m.m_screens[i];
        m.m_screens.clear();
        m.m_models.clear();
        g_menuScreens.clear();
        g_mrMade.erase(FRAME_MODEL);
        g_mrMade.erase(FRAME_DUMMY);
        g_cacheFill = oldFill;
        g_mrModelVt[0x08 / 4] = oldModelSetPos;
        m.m_scene = nullptr;
        WM(r.hsBuild, ok);
    }
    for (C_BwControl *row : rows)
        delete row;
    skillList.m_children.clear();
    hero.m_children.clear();
    g_driverVtable[0x54 / 4] = oldCreate;
    g_mrModelVt[0x48 / 4] = oldFind;
    g_mrModelVt[0x7C / 4] = oldAddFrame;
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("herostats: hsType %d hsValue %d hsEscape %d hsAccept %d hsQuit %d hsHero %d hsSpec %d hsSkills %d hsRow %d"
                " hsBuild %d\n",
                r.hsType, r.hsValue, r.hsEscape, r.hsAccept, r.hsQuit, r.hsHero, r.hsSpec, r.hsSkills, r.hsRow, r.hsBuild);
}
