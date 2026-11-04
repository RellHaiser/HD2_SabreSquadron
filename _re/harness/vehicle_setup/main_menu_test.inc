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
// The description: the screens' names (each AddScreen's id its index + 1).
std::vector<std::string> g_mmScreens;
S_MenuDesc *C_MainMenu::Describe()
{
    GfLog("describe");
    auto *menu = new S_MenuDesc();
    for (const std::string &name : g_mmScreens)
        menu->AddScreen(name.c_str(), name.c_str());
    // (The first screen gets a callback of its own, made by the screen's messages.)
    if (!menu->screens.empty())
        AddScreenMessage(menu->screens[0], 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_SHOWN, reinterpret_cast<uintptr_t>(this),
                         reinterpret_cast<uintptr_t>(&C_MainMenu::SelectProfile));
    return menu;
}
#define MM_DESCRIBE(F, N)                                                                                         \
    bool C_MainMenu::F(S_ScreenDesc *desc, I3D_frame *)                                                           \
    {                                                                                                             \
        GfLog("describe %s %x", N, desc->id);                                                                     \
        return true;                                                                                              \
    }
MM_DESCRIBE(DescribeProfileScreen, "profile")
MM_DESCRIBE(DescribeMissionsScreen, "missions")
MM_DESCRIBE(DescribeSavesScreen, "saves")
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
         mmSelect = true, mmBuild = true;
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
        g_mmScreens.clear();
        for (int i = 1; i <= 15; ++i) {
            g_mmScreens.push_back(CmText("mms%02d", i));
            models.push_back(MrObj(strdup(CmText("mmm%02d", i).c_str()), g_mrModelVt));
        }
        m.m_screens = {&box};
        g_mrMade[FRAME_MODEL] = std::deque<S_MrObj *>(models.begin(), models.end());
        // (The options screen's: its group's frame; the real handler finds no buttons.)
        g_mrMade[FRAME_DUMMY] = {MsFrame("mmgro")};
        g_cacheLoads.clear();
        g_menuScreens.clear();
        g_gfLog.clear();
        ok &= m.BuildScreens() && g_cacheLoads.size() == 15 && g_cacheLoads[0] == "mms01" && g_cacheLoads[14] == "mms15";
        const std::string log = Join(g_gfLog);
        ok &= GmInOrder(log, {"describe", "describe profile 8000000", "describe missions 14000000", "describe saves 1c000000",
                              "describe browser 20000000", "describe create 24000000", "describe password 28000000",
                              "describe filters 2c000000", "describe gameinfo 30000000", "describe connect 34000000",
                              "describe addserver 38000000"}) &&
              log.find("describe ", log.find("describe addserver") + 1) == std::string::npos;
        ok &= m.m_screens.size() == 16 && S_CsPeek::Callbacks(m.m_screens[1]) == 1 && S_CsPeek::Callbacks(m.m_screens[2]) == 0 &&
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
        g_mmScreens = {"mms01", "mms02"};
        g_mrMade[FRAME_MODEL] = std::deque<S_MrObj *>(models.begin(), models.begin() + 2);
        g_cacheFailName = "mms02";
        g_menuScreens.clear();
        g_gfLog.clear();
        ok &= m.BuildScreens() && m.m_screens.size() == 2 && Join(g_gfLog).find("describe profile") == std::string::npos;
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
    std::printf("mainmenu: mmStart %d mmStop %d mmUpdate %d mmDrain %d mmMessage %d mmEscape %d mmSelect %d mmBuild %d\n",
                r.mmStart, r.mmStop, r.mmUpdate, r.mmDrain, r.mmMessage, r.mmEscape, r.mmSelect, r.mmBuild);
}
