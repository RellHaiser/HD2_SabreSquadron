// Part 9h's main menu (game/main_menu.cpp, game/mp_game_setup.cpp), in the factory build, run by ControlsTest
// after CampaignScreensTest: the fakes the earlier tests' (the logging frames and models, the screens' factory,
// the message queue's CsQueue). Prints "mainmenu:" with 1 per check met.
#define private public
#define protected public
#include "game/main_menu.h"
#include "game/mp_game_setup.h"
#include "game/options_handler.h"
#undef protected
#undef private

// (Part 9h-iii: not ported yet or not linked: faked, logged.)
const char *g_mmDate = "1.2.2026";
bool GetDateString(const char **text, bool forFileName)
{
    GfLog("date %d", forFileName);
    if (!g_mmDate)
        return false;
    *text = g_mmDate;
    return true;
}
void C_GameApp::RequestLoad(const C_str &fileName) { GfLog("request load %s", fileName.c_str()); }
bool C_GameProfile::SelectPlayer(const char *name)
{
    GfLog("select player %s", name ? name : "(null)");
    return true;
}
// The screens' handlers: each logs and answers its own flag.
bool g_mmTakes[4];
bool C_MainMenu::OnProfileMessage(S_ControlMessage *m)
{
    GfLog("profile message %x", m->id);
    return g_mmTakes[0];
}
bool C_MainMenu::OnNetworkMessage(S_ControlMessage *m)
{
    GfLog("network message %x", m->id);
    return g_mmTakes[1];
}
bool C_MainMenu::OnMissionsMessage(S_ControlMessage *m)
{
    GfLog("missions message %x", m->id);
    return g_mmTakes[2];
}
bool C_MainMenu::OnConnectMessage(S_ControlMessage *m)
{
    GfLog("connect message %x", m->id);
    return g_mmTakes[3];
}
// (Part 9h-iv: the screens' callbacks of later sub-parts, faked; the network game's close and the last save,
// logged.)
#define MM_CALLBACK(F)                                                                                                bool C_MainMenu::F(void *, void *, C_Control *, uint32_t, uint32_t)                                               {                                                                                                                     GfLog(#F);                                                                                                        return true;                                                                                                  }
MM_CALLBACK(OnProfileShown)
MM_CALLBACK(OnProfileButton)
MM_CALLBACK(OnProfileList)
MM_CALLBACK(OnDifficulty)
MM_CALLBACK(OnSingleplayerShown)
MM_CALLBACK(OnSingleplayerButton)
void C_NetGame::Close(bool now) { GfLog("net close %d", now); }
bool g_mmHasSave = true;
bool C_PlayerProfile::GetLastSaveText(C_str *mode, C_str *mission, C_str *date) const
{
    GfLog("last save");
    if (!g_mmHasSave)
        return false;
    *mode = "Campaign";
    *mission = "Mission 3";
    *date = "1.1.2026";
    return true;
}
#define MM_DESCRIBE(F, N)                                                                                         \
    bool C_MainMenu::F(S_ScreenDesc *desc, I3D_frame *)                                                           \
    {                                                                                                             \
        GfLog("describe %s %x", N, desc->id);                                                                     \
        return true;                                                                                              \
    }
MM_DESCRIBE(DescribeProfileScreen, "profile")
MM_DESCRIBE(DescribeMissionsScreen, "missions")
MM_DESCRIBE(DescribeCinematicsScreen, "cinematics")
MM_DESCRIBE(DescribeBrowserScreen, "browser")
MM_DESCRIBE(DescribeCreateServerScreen, "create")
MM_DESCRIBE(DescribePasswordScreen, "password")
MM_DESCRIBE(DescribeFiltersScreen, "filters")
MM_DESCRIBE(DescribeGameInfoScreen, "gameinfo")
MM_DESCRIBE(DescribeConnectScreen, "connect")
MM_DESCRIBE(DescribeAddServerScreen, "addserver")

// The found servers' deletes: a global operator delete that notes the watched addresses.
std::vector<void *> g_mmWatch, g_mmDeleted;
void MmDeleteHook(void *p)
{
    if (p && std::find(g_mmWatch.begin(), g_mmWatch.end(), p) != g_mmWatch.end())
        g_mmDeleted.push_back(p);
}

struct S_MainMenuResults {
    bool mmStart = true, mmStop = true, mmUpdate = true, mmDrain = true, mmMessage = true, mmEscape = true,
         mmSelect = true, mmBuild = true, mmDescribe = true, mmMainShown = true;
};

void MainMenuTest(I3D_scene *scene)
{
    S_MainMenuResults r;
    void *const oldFind = g_mrModelVt[0x48 / 4], *const oldCreate = g_driverVtable[0x54 / 4];
    g_mrModelVt[0x48 / 4] = reinterpret_cast<void *>(&MsFind);
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&MrCreateFrame);
    C_PlayerProfile player;
    C_PlayerProfile *const oldPlayer = g_gameProfile.m_player;
    g_gameProfile.m_player = &player;
    g_controlMessages.clear();
    g_gfLog.clear();

    // --- started: the settings and the options handler made, the date shown; stopped: all deleted ---
    {
        bool ok = true;
        C_MainMenu m;
        ok &= m.unknown1F4 == 5 && m.unknown208 == -1 && m.unknown20C == -1 && !m.m_mpSettings && !m.m_options;
        ok &= m.OnStart(nullptr) && m.m_mpSettings && m.m_options && m.m_options->m_menu == &m &&
              m.m_mpSettings->m_listKind == 1 && m.m_mpSettings->m_filter.maxPing == -1;
        ok &= CsQueue() == "4300000 1000014 c 0 '1.2.2026'; " && MsLogIs({"date 0"}, g_gfLog);
        WM(r.mmStart, ok);

        // The update: the date again, only when known.
        ok = true;
        g_gfLog.clear();
        g_mmDate = "3.4.2026";
        m.OnUpdate(10);
        ok &= CsQueue() == "4300000 1000014 c 0 '3.4.2026'; ";
        g_mmDate = nullptr;
        m.OnUpdate(10);
        ok &= CsQueue() == "" && MsLogIs({"date 0", "date 0"}, g_gfLog);
        g_mmDate = "1.2.2026";
        WM(r.mmUpdate, ok);

        // The servers found: those in neither of the settings' lists deleted, all taken off.
        ok = true;
        auto *a = new S_ServerEntry(), *b = new S_ServerEntry(), *c = new S_ServerEntry();
        // (Deleted ones are seen by the delete's hook below: the entries' addresses.)
        g_mmDeleted.clear();
        g_mmWatch = {a, b, c};
        g_deleteHook = &MmDeleteHook;
        m.m_mpSettings->m_servers = {a};
        m.m_mpSettings->m_servers2 = {b};
        m.m_foundServers = {a, b, c};
        m.DrainFoundServers();
        ok &= m.m_foundServers.empty() && m.m_mpSettings->m_servers.size() == 1 && m.m_mpSettings->m_servers2.size() == 1 &&
              g_mmDeleted == std::vector<void *>({c});
        g_deleteHook = nullptr;
        g_mmWatch.clear();
        m.DrainFoundServers();
        ok &= m.m_foundServers.empty();
        m.m_mpSettings->m_servers2.clear();
        delete b;
        WM(r.mmDrain, ok);

        // Stopped: the game info's styles released, the settings (their first list's servers) and the handler gone.
        ok = true;
        S_TextStyleDesc styleDesc;
        for (int i = 0; i < 3; ++i)
            g_gameInfoStyles[i] = CreateTextStyle(styleDesc);
        m.m_foundServers = {new S_ServerEntry()};
        m.m_mpSettings->m_servers = {a};
        const int made = g_gameInfoStyles[1];
        ok &= GetTextStyle(made) != nullptr;
        m.OnStop();
        ok &= !m.m_mpSettings && !m.m_options && m.m_foundServers.empty() && GetTextStyle(made) == nullptr;
        for (int style : g_gameInfoStyles)
            ok &= style == -1;
        WM(r.mmStop, ok);
    }

    // --- the messages: the handlers in turn, then the menu's own ---
    {
        bool ok = true;
        C_MainMenu m;
        m.OnStart(nullptr);
        CsQueue();
        S_ControlMessage message;
        message.id = 0x2000099;
        const auto send = [&](uint32_t id) {
            g_gfLog.clear();
            message.id = id;
            m.OnMessage(&message);
        };
        send(0x2000099);
        ok &= MsLogIs({"profile message 2000099", "network message 2000099", "missions message 2000099",
                       "connect message 2000099"}, g_gfLog) && CsQueue() == "";
        const int state = m.m_state;
        for (int k = 0; k < 4; ++k) {
            g_mmTakes[k] = true;
            send(MAIN_MSG_PLAY_MOVIE);
            ok &= g_gfLog.size() == static_cast<size_t>(k + 1) && m.m_state == state;
            g_mmTakes[k] = false;
        }
        // The movie; the saved game (none: the load command alone).
        send(0x4000000 | MAIN_MSG_PLAY_MOVIE);
        ok &= m.m_state == GAME_STATE_PLAY_AVI;
        m.m_state = state;
        C_GameApp *app = GetGameApp();
        app->m_pendingCommand = 0;
        send(MAIN_MSG_LOAD_GAME);
        ok &= app->m_pendingCommand == APP_COMMAND_LOAD_FROM_MENU && g_gfLog.size() == 4;
        app->m_pendingCommand = 0;
        m.m_saveName = "save7.sav";
        send(MAIN_MSG_LOAD_GAME);
        ok &= app->m_pendingCommand == 0 && g_gfLog.size() == 5 && g_gfLog[4] == "request load prof\\save7.sav";
        m.m_saveName = "";
        send(MAIN_MSG_LOAD_GAME);
        ok &= app->m_pendingCommand == APP_COMMAND_LOAD_FROM_MENU;
        app->m_pendingCommand = 0;
        ok &= CsQueue() == "";
        WM(r.mmMessage, ok);

        // Escape by the top screen.
        ok = true;
        std::vector<C_MsScreen *> screens;
        for (uint32_t i = 0; i <= 15; ++i)
            screens.push_back(new C_MsScreen(nullptr, "mmscreen", i << 26 | 0x1234));
        const auto escape = [&](int top) {
            g_menuScreens.clear();
            if (top >= 0) {
                g_menuScreens.push_back(screens[1]);
                g_menuScreens.push_back(screens[top]);
            }
            send(MENU_MSG_ESCAPE);
            return CsQueue();
        };
        ok &= escape(-1) == "0 2000003 0 0; ";
        // (Top 0: the options handler's screen index before its screen is built: it takes Escape.)
        ok &= escape(0).find("20000c4") != std::string::npos && g_gfLog.size() == 3;
        for (int top : {1, 3, 5, 6, 7, 8, 9, 11, 12, 13, 15})
            ok &= escape(top) == "0 2000003 0 0; ";
        ok &= escape(14) == "0 10000200 1 0; 0 2000005 0 0; " && escape(10) == "0 10000200 1 0; 0 2000005 0 0; ";
        ok &= escape(4) == "0 14001200 1 0; 0 6001005 0 0; ";
        m.m_profileName = "Tester";
        m.m_options->m_fillOnOpen = false;
        ok &= escape(2) == "0 2000003 0 0; " && GmInOrder(Join(g_gfLog), {"select player Tester", "reset game"}) &&
              m.m_options->m_fillOnOpen;
        g_menuScreens.clear();
        for (C_MsScreen *s : screens)
            delete s;
        WM(r.mmEscape, ok);

        // The profile picked: none: no player (nothing else).
        ok = true;
        m.m_profileName = "";
        m.m_options->m_fillOnOpen = false;
        g_gfLog.clear();
        ok &= C_MainMenu::SelectProfile(&m, nullptr, nullptr, 0, 0) && MsLogIs({"select player (null)"}, g_gfLog) &&
              !m.m_options->m_fillOnOpen;
        m.m_profileName = "Other";
        g_gfLog.clear();
        ok &= C_MainMenu::SelectProfile(&m, nullptr, nullptr, 0, 0) && MsLogIs({"select player Other", "reset game"}, g_gfLog) &&
              m.m_options->m_fillOnOpen;
        WM(r.mmSelect, ok);
        m.m_mpSettings->m_servers.clear();
        m.OnStop();
    }

    // --- (part 9h-iv) described: the main, profile, single-player and quit screens; the others empty ---
    {
        bool ok = true;
        C_MainMenu m;
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "self";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnMainShown)] = "mainShown";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnProfileShown)] = "profileShown";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnProfileButton)] = "profileButton";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnProfileList)] = "profileList";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnDifficulty)] = "difficulty";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnSingleplayerShown)] = "singleShown";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnSingleplayerButton)] = "singleButton";
        // (The messages carrying a string (the forbidden characters, the frames): the string.)
        const auto message = [](const S_ControlMessage &msg) {
            if (msg.id == CONTROL_MSG_SET_FORBIDDEN || msg.id == CONTROL_MSG_ADD_FRAME)
                return CmText("%x %x %x '%s'", msg.target, msg.id, msg.value, reinterpret_cast<const char *>(msg.value2));
            return CsMessage(msg);
        };
        const auto dump = [&]() {
            S_MenuDesc *menu = m.Describe();
            std::string s;
            for (const S_ScreenDesc *d : menu->screens) {
                s += CmText("[%s|%s|%x]", d->name.c_str(), d->file.c_str(), d->id);
                for (const S_ControlMessage &msg : d->messages)
                    s += " " + message(msg);
                for (const S_ControlDesc *c : d->controls) {
                    s += CmText(" %x %s %x:", c->type, c->name.c_str(), c->id);
                    for (const S_ControlMessage &msg : c->messages)
                        s += " " + message(msg);
                    s += c->children.empty() ? ";" : " (children);";
                }
                s += "\n";
            }
            delete menu;
            return s;
        };
        // (As the original's, read from its asm: the screens, their callbacks, the controls' types, names, ids
        // and messages in order.)
        const std::string want =
            "[MAIN MENU|main_menu|4000000] 0 1000007 400d000 self mainShown 13 text_screen 4100000: 0 1000015 a 835; 13 text_date00 4200000: 0 1000015 b 836; 13 text_date01 4300000: 0 1000015 c 7d2; 13 text_profile00 4400000: 0 1000015 b 837; 13 text_profile01 4500000: 0 1000015 c 7d2; 13 text_last save00 4600000: 0 1000015 b 838; 13 text_last save01 4700000: 0 1000015 c 7d2; 14 bresume game 4800000: 0 1000015 d 839 0 6001075 0 0; 14 bchange profile 4900000: 0 1000015 d 83a 0 6001001 2 0; 14 bsingleplayer 4a00000: 0 1000015 d 83b 0 6001001 3 0; 14 bmultiplayerlan 4b00000: 0 1000015 d 843 0 6001099 0 0; 14 bmultiplayernet 4c00000: 0 1000015 d 844 0 600109a 0 0; 14 bcinematics 4d00000: 0 1000015 d 83d 0 6001001 7 0; 14 boptions 4e00000: 0 1000015 d 83e 0 6001071 0 0; 14 bcredits 4f00000: 0 1000015 d 83f 0 600103a 0 0; 14 bexit 5000000: 0 1000015 d 840 0 6001003 4000000 0; 13 text_h&dversion 5100000: 0 1000015 28 7d6;\n"
            "[CHANGE PROFIL|change_profile|8000000] 0 1000007 400d000 self profileShown 1b bcurrent profile 8100000: 0 1000015 d 7d2 0 100009b 0 './\\*?|<>:\"' 0 100006e 11 0 0 1000007 4016000 self profileButton 0 1000007 4017000 self profileButton; f LB_Profile* 8200000: 0 1000007 4001000 self profileList 0 1000007 4013000 self profileList; 14 beasy-* 8300000: 0 1000015 d 86d 0 1000007 4001000 self difficulty; 14 bnormal-* 8400000: 0 1000015 d 86e 0 1000007 4001000 self difficulty; 14 bhard-* 8500000: 0 1000015 d 86f 0 1000007 4001000 self difficulty; 14 bvery hard-* 8600000: 0 1000015 d 870 0 1000007 4001000 self difficulty; 14 bcreate profile 8700000: 0 1000015 d 868 0 1000007 4001000 self profileButton; 14 brename profile 8800000: 0 1000015 d 86a 0 1000007 4001000 self profileButton; 14 Bdelete profile 8900000: 0 1000015 d 86b 0 1000007 4001000 self profileButton; 14 bback 8a00000: 0 1000015 d 86c 0 6001003 0 0; 13 text_h&dversion 8b00000: 0 1000015 28 7d6; 13 text_current profile and save 8c00000: 0 1000015 b 88f; 13 text_date and time 8d00000:; 13 text_list of profiles 8e00000: 0 1000015 b 890; 13 text01 8f00000:; 13 text_screen 9000000: 0 1000015 a 867; 13 text_difficulty 9100000: 0 1000015 b 891;\n"
            "[SINGLEPLAYER|singleplayer|c000000] 0 1000007 400d000 self singleShown 13 text_screen c100000: 0 1000015 a 899; c single side c200000: 0 1000032 0 'ss1' 0 1000032 0 'ss2' 0 1000032 0 'ss3' 0 1000032 0 'ss4' 0 1000032 0 'ss5' 0 1000032 0 'ss6' 0 1000032 0 'ss8' 0 1000032 0 'ss7'; 13 text00 c300000:; 14 bcampaign c400000: 0 1000015 d 89a 0 1000007 4003000 self singleButton 0 1000007 4001000 self singleButton; 14 blone wolf c500000: 0 1000015 d 89b 0 1000007 4003000 self singleButton 0 1000007 4001000 self singleButton; 14 carnage c600000: 0 1000015 d 89c 0 1000007 4003000 self singleButton 0 1000007 4001000 self singleButton; 14 bsingle mission c700000: 0 1000015 d 89d 0 1000007 4003000 self singleButton 0 1000007 4001000 self singleButton; 14 bsingle mission- carnage c800000: 0 1000015 d 89e 0 1000007 4003000 self singleButton 0 1000007 4001000 self singleButton; 14 bcampaign01 c900000: 0 1000015 d 89a 0 1000007 4003000 self singleButton 0 1000007 4001000 self singleButton; 14 blone wolf01 ca00000: 0 1000015 d 89b 0 1000007 4003000 self singleButton 0 1000007 4001000 self singleButton; 14 carnage01 cb00000: 0 1000015 d 89c 0 1000007 4003000 self singleButton 0 1000007 4001000 self singleButton; 14 bback cc00000: 0 1000015 d 8a0 0 1000007 4003000 self singleButton 0 1000007 4001000 self singleButton; 13 text_h&dversion cd00000: 0 1000015 28 7d6; 13 text_HD2original ce00000: 0 1000015 d 4fb0; 13 text_HD2datadisk cf00000: 0 1000015 d 4fb1;\n"
            "[CONFIRM|confirm|10000000] 13 text 10100000: 0 1000015 c 7d4; 14 byes* 10200000: 0 1000015 d 7d1 0 14001100 0 0 0 6001005 0 0; 14 bno* 10300000: 0 1000015 d 7d0 0 14001200 1 0 0 6001005 0 0; 14 bok 10400000: 0 1000004 0 0;\n"
            "[SINGLE MISSION 2|single mission 2|14000000]\n"
            "[OPTIONS|options|18000000]\n"
            "[SINGLE MISSION 2|single mission 2|1c000000]\n"
            "[multiplayer menu|multiplayer menu|20000000]\n"
            "[create server|create server|24000000]\n"
            "[PASSWORD|password|28000000]\n"
            "[Server Filters|server filters|2c000000]\n"
            "[Game info|gameinfo|30000000]\n"
            "[MP Connect console|confirm|34000000]\n"
            "[ADD SERVER|password|38000000]\n";
        g_foLanguage = "English";
        const std::string english = dump();
        g_foLanguage = "japan";
        const std::string japanese = dump();
        g_foLanguage = "English";
        // (In Japanese the name's box has no length limit.)
        std::string wantJapanese = want;
        const size_t limit = wantJapanese.find(" 0 100006e 11 0");
        wantJapanese.erase(limit, std::strlen(" 0 100006e 11 0"));
        ok &= english == want && japanese == wantJapanese && limit != std::string::npos;
        WM(r.mmDescribe, ok);
    }

    // --- (part 9h-iv) the main screen shown: no profile: all off; a profile: its name, its last save (or none),
    // the buttons on ---
    {
        bool ok = true;
        C_MainMenu m;
        const C_str oldName = g_gmProfileName;
        const auto shown = [&]() {
            g_gfLog.clear();
            g_controlMessages.clear();
            m.m_saveName = "save1";
            return C_MainMenu::OnMainShown(&m, nullptr, nullptr, CONTROL_NOTIFY_SHOWN, 0);
        };
        std::string modes;
        for (uint32_t id = 0x4A00000; id <= 0x4F00000; id += 0x100000)
            modes += CmText("%x 1000001 0 0; ", id);
        g_gmProfileName = "";
        ok &= shown() && m.m_saveName.IsEmpty() && m.m_profileName.IsEmpty() &&
              MsLogIs({"net close 1"}, g_gfLog) &&
              CsQueue() == "4500000 1000015 c 7e4; 4700000 1000015 c 7e5; 4800000 1000001 0 0; 4900000 1000015 d 845; " + modes;
        std::string on;
        for (uint32_t id = 0x4A00000; id <= 0x4F00000; id += 0x100000)
            on += CmText("%x 1000002 0 0; ", id);
        g_gmProfileName = "Pvt";
        g_mmHasSave = true;
        ok &= shown() && m.m_profileName == "Pvt" && MsLogIs({"net close 1", "last save"}, g_gfLog) &&
              CsQueue() == "4500000 1000014 c 0 'Pvt'; 4500000 1000014 c 0 'Pvt'; 4700000 2000018 0 0; "
                           "4700000 1000016 7 0 'Campaign'; 4700000 1000016 7 0 'Mission 3'; 4700000 1000016 7 0 '1.1.2026'; "
                           "4800000 1000002 0 0; 4900000 1000015 d 83a; " + on;
        g_mmHasSave = false;
        ok &= shown() && CsQueue() == "4500000 1000014 c 0 'Pvt'; 4500000 1000014 c 0 'Pvt'; 4700000 2000018 0 0; "
                                      "4700000 1000015 c 7e5; 4800000 1000001 0 0; 4900000 1000015 d 83a; " + on;
        g_mmHasSave = true;
        g_gmProfileName = oldName;
        WM(r.mmMainShown, ok);
    }

    // --- built: each screen's model loaded, described by its kind, made; the main screen opened ---
    {
        bool ok = true;
        void (*const oldFill)(I3D_model *) = g_cacheFill;
        g_cacheFill = [](I3D_model *) {};
        void *const oldModelSetPos = g_mrModelVt[0x08 / 4], *const oldFrameSetPos = g_msFrameVt[0x08 / 4];
        g_mrModelVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
        g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
        C_MainMenu m;
        m.OnStart(nullptr);
        CsQueue();
        S_MrWorld world = MrWorld("mmb");
        m.m_scene = As<I3D_scene>(world.scene);
        C_MsScreen box(m.m_scene, "mmbbox", 0);
        std::vector<S_MrObj *> models;
        for (int i = 1; i <= 14; ++i)
            models.push_back(MrObj(strdup(CmText("mmm%02d", i).c_str()), g_mrModelVt));
        m.m_screens = {&box};
        g_mrMade[FRAME_MODEL] = std::deque<S_MrObj *>(models.begin(), models.end());
        // (The options screen's: its group's frame; the real handler finds no buttons.)
        g_mrMade[FRAME_DUMMY] = {MsFrame("mmgro")};
        g_cacheLoads.clear();
        g_menuScreens.clear();
        g_gfLog.clear();
        ok &= m.BuildScreens() &&
              g_cacheLoads == std::vector<std::string>({"main_menu", "change_profile", "singleplayer", "confirm",
                                                        "single mission 2", "options", "single mission 2",
                                                        "multiplayer menu", "create server", "password", "server filters",
                                                        "gameinfo", "confirm", "password"});
        const std::string log = Join(g_gfLog);
        ok &= GmInOrder(log, {"describe profile 8000000", "describe missions 14000000", "describe cinematics 1c000000",
                              "describe browser 20000000", "describe create 24000000", "describe password 28000000",
                              "describe filters 2c000000", "describe gameinfo 30000000", "describe connect 34000000",
                              "describe addserver 38000000"}) &&
              log.find("describe ", log.find("describe addserver") + 1) == std::string::npos;
        ok &= m.m_screens.size() == 15 && S_CsPeek::Callbacks(m.m_screens[1]) == 1 && S_CsPeek::Callbacks(m.m_screens[2]) == 1 &&
              S_CsPeek::Callbacks(m.m_screens[3]) == 1 && S_CsPeek::Callbacks(m.m_screens[4]) == 0 &&
              m.m_screens[4]->GetScreenFrame() == As<I3D_frame>(models[3]) &&
              !g_menuScreens.empty() && g_menuScreens.back() == m.m_screens[1];
        // The quit question's model moved.
        ok &= log.find("mmm04 8 (0.5 -0.375 -0.5)") != std::string::npos && log.find(" 8 (0.5") == log.rfind(" 8 (0.5") &&
              log.find("mmgro 34 GROptions") != std::string::npos;
        // A model not found: its screen skipped.
        for (size_t i = 1; i < m.m_screens.size(); ++i)
            delete m.m_screens[i];
        m.m_screens = {&box};
        m.m_models.clear();
        g_mrMade[FRAME_MODEL] = std::deque<S_MrObj *>(models.begin(), models.end());
        g_mrMade[FRAME_DUMMY] = {MsFrame("mmgro2")};
        g_cacheFailName = "change_profile";
        g_menuScreens.clear();
        g_gfLog.clear();
        ok &= m.BuildScreens() && m.m_screens.size() == 14 && Join(g_gfLog).find("describe profile") == std::string::npos;
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
        g_msFrameVt[0x08 / 4] = oldFrameSetPos;
        m.OnStop();
        WM(r.mmBuild, ok);
    }

    g_gameProfile.m_player = oldPlayer;
    g_driverVtable[0x54 / 4] = oldCreate;
    g_mrModelVt[0x48 / 4] = oldFind;
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("mainmenu: mmStart %d mmStop %d mmUpdate %d mmDrain %d mmMessage %d mmEscape %d mmSelect %d mmBuild %d mmDescribe %d mmMainShown %d\n",
                r.mmStart, r.mmStop, r.mmUpdate, r.mmDrain, r.mmMessage, r.mmEscape, r.mmSelect, r.mmBuild, r.mmDescribe,
                r.mmMainShown);
}
