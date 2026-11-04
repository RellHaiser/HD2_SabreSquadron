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
// (Part 9h-v: the profile screen's handler is real; its own fakes are its delete and list. Part 9h-ix: the
// missions' handler real too.)
bool C_MainMenu::OnNetworkMessage(S_ControlMessage *m)
{
    GfLog("network message %x", m->id);
    return g_mmTakes[1];
}
bool C_MainMenu::OnConnectMessage(S_ControlMessage *m)
{
    GfLog("connect message %x", m->id);
    return g_mmTakes[3];
}
// (Part 9h-iv: the screens' callbacks of later sub-parts, faked; the network game's close and the last save,
// logged.)
#define MM_CALLBACK(F)                                                                                                bool C_MainMenu::F(void *, void *, C_Control *, uint32_t, uint32_t)                                               {                                                                                                                     GfLog(#F);                                                                                                        return true;                                                                                                  }
void C_NetGame::Close(bool now) { GfLog("net close %d", now); }
bool g_mmHasSave = true;
// (Part 9h-v: the profiles found, the deletes, the difficulty, the save.)
std::vector<std::string> g_mmProfiles;
bool g_mmDeletes = true;
void ScanProfiles(void (*callback)(const char *name, int context), int context)
{
    GfLog("scan");
    for (const std::string &name : g_mmProfiles)
        callback(name.c_str(), context);
}
bool DeleteProfile(const char *name)
{
    GfLog("delete %s", name);
    return g_mmDeletes;
}
void C_GameProfile::SetDifficulty(int difficulty)
{
    GfLog("set difficulty %d", difficulty);
    g_gmDifficulty = difficulty;
}
// (Part 9h-vi: making and renaming a profile, the last one kept.)
int g_mmCreate = 0, g_mmRename = 0;
int CreateProfile(const char *name)
{
    // (Bytes above 0x7F logged as <hex>: the runners read the output as UTF-8.)
    std::string shown;
    for (const char *p = name; *p; ++p)
        shown += static_cast<unsigned char>(*p) < 0x80 ? std::string(1, *p) : CmText("<%02x>", static_cast<unsigned char>(*p));
    GfLog("create '%s'", shown.c_str());
    return g_mmCreate;
}
int C_PlayerProfile::Rename(const char *oldName, const char *newName)
{
    GfLog("rename '%s' '%s'", oldName, newName);
    return g_mmRename;
}
void C_GameConfig::SetLastProfile(C_str name) { GfLog("last profile %s", name.c_str()); }
// (Part 9h-viii: the campaigns' missions (operations: loadout_test.inc's g_lpMissions), their texts and pictures;
// the save slots.)
extern int g_textEmptyFrom;                             // (car_stubs.cpp)
std::vector<int> g_mmMissionCounts;
int g_mmMissionText = 0;
int C_Campaign::CountMissions(int campaign) { return g_mmMissionCounts[static_cast<size_t>(campaign)]; }
int C_Campaign::GetOperationId(int campaign, int operation) { return 0x700 + 0x10 * campaign + operation; }
int C_Campaign::GetMissionId(int campaign, int missionNumber) { return 0x800 + 0x10 * campaign + missionNumber; }
const C_str &C_Campaign::GetMissionImage(int campaign, int missionNumber)
{
    static C_str image;
    image = CmText("img%d_%d", campaign, missionNumber).c_str();
    return image;
}
int C_Campaign::GetMissionText(int campaign, int missionNumber, int index)
{
    GfLog("mission text %d %d %d", campaign, missionNumber, index);
    return g_mmMissionText;
}
int g_mmSlotMissions[5] = {-1, -1, -1, -1, -1};
C_str g_mmSlotFiles[5];
int C_PlayerProfile::GetSaveSlotMission(int slot) const { return g_mmSlotMissions[slot]; }
const C_str &C_PlayerProfile::GetSaveSlotFile(int slot) const { return g_mmSlotFiles[slot]; }
// A mission's line: its index.
struct C_MpLine : C_Control {
    C_MpLine(uint32_t id, int index) : C_Control(nullptr, id, nullptr) { m_index = index; }
};
// (Part 9h-ix: the player's progress, the operations' names.)
bool g_mxHasProgress;
std::vector<int> g_mxBest;
bool C_PlayerProfile::HasProgress(int campaign) const
{
    GfLog("has progress %d", campaign);
    return g_mxHasProgress;
}
int C_PlayerProfile::GetBestProgress(int campaign) const
{
    GfLog("best %d", campaign);
    return g_mxBest[static_cast<size_t>(campaign)];
}
const C_str &C_Campaign::GetOperationName(int campaign, int operation)
{
    static C_str name;
    name = CmText("op%d_%d", campaign, operation).c_str();
    return name;
}
// (Part 9h-ix: the movies: their texts, files and seen flags; Get logged.)
struct S_CiMovie {
    int text;
    const char *file;
    bool seen;
};
std::vector<S_CiMovie> g_ciMovies;
int C_MovieList::GetCount() const { return static_cast<int>(g_ciMovies.size()); }
bool C_MovieList::Get(int index, int *textId, C_str *file) const
{
    GfLog("movie %d", index);
    *textId = g_ciMovies[static_cast<size_t>(index)].text;
    *file = g_ciMovies[static_cast<size_t>(index)].file;
    return true;
}
bool C_MovieList::IsSeen(int index) const
{
    GfLog("seen %d", index);
    return index < static_cast<int>(g_ciMovies.size()) && g_ciMovies[static_cast<size_t>(index)].seen;
}
// (Part 9h-x: the parental lock: set or not, its code logged.)
bool g_pwLocked;
bool C_GameConfig::IsParentLockSet() const { return g_pwLocked; }
bool C_GameConfig::SetParentLock(C_str code)
{
    GfLog("lock set %s", code.c_str());
    g_pwLocked = true;
    return true;
}
bool C_GameConfig::ClearParentLock(C_str code)
{
    GfLog("lock clear %s", code.c_str());
    g_pwLocked = std::string(code.c_str()) != "1234";
    return !g_pwLocked;
}
// The password box: its text asked (logged).
struct C_PwBox : C_Control {
    C_PwBox(uint32_t id, C_Control *parent) : C_Control(nullptr, id, parent) {}
    const char *text = "";
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("box %x %x", m->target, m->id);
        if (m->id == CONTROL_MSG_GET_TEXT)
            m->text = C_str(text);
        return true;
    }
};
extern std::map<std::string, std::vector<uint8_t>> g_files;   // (car_stubs.cpp)
// (Part 9h-xi: a filter screen's control: its kind, its value; its messages logged.)
struct C_FlControl : C_Control {
    C_FlControl(uint32_t id, C_Control *parent, int type, int v) : C_Control(nullptr, id, parent), value(v)
    {
        m_type = type;
    }
    int value;
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("fl %x %x %x %x", m_id, m->id, m->value, static_cast<uint32_t>(m->value2));
        if (m->id == CONTROL_MSG_GET_CHECKED || m->id == CONTROL_MSG_GET_TEXT)
            m->value = static_cast<uint32_t>(value);
        else if (m->id == CONTROL_MSG_GET_NUMBER)
            m->value2 = static_cast<uintptr_t>(value);
        return true;
    }
};
// (Part 9h-xii: the server status asked for: logged, its callback kept.)
void(__fastcall *g_giCallback)(S_ServerStatus *);
int C_NetGame::QueryServerStatus(void(__fastcall *callback)(S_ServerStatus *), S_ServerEntry *server)
{
    GfLog("query %s", server->name);
    g_giCallback = callback;
    return 1;
}
bool C_PlayerProfile::Save()
{
    GfLog("save profile");
    return true;
}
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
MM_DESCRIBE(DescribeBrowserScreen, "browser")
MM_DESCRIBE(DescribeCreateServerScreen, "create")
MM_DESCRIBE(DescribeConnectScreen, "connect")
MM_DESCRIBE(DescribeAddServerScreen, "addserver")

// The found servers' deletes: a global operator delete that notes the watched addresses.
std::vector<void *> g_mmWatch, g_mmDeleted;
void MmDeleteHook(void *p)
{
    if (p && std::find(g_mmWatch.begin(), g_mmWatch.end(), p) != g_mmWatch.end())
        g_mmDeleted.push_back(p);
}

// (Part 9h-v.) A control answering CONTROL_MSG_GET_TEXT with its text and value; the messages logged.
struct C_PfLine : C_Control {
    C_PfLine(uint32_t id, const char *t) : C_Control(nullptr, id, nullptr), text(t) {}
    C_str text;
    uint32_t value = 0;
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("line %x %x %x", m->target, m->id, m->value);
        if (m->id == CONTROL_MSG_GET_TEXT) {
            m->text = text;
            m->value = value;
        }
        return true;
    }
};
// The profile screen as a screen: the same answers (the menu's HandleMessage routes the list's messages to it).
struct C_PfScreen : C_MsScreen {
    C_PfScreen(I3D_scene *scene) : C_MsScreen(scene, "pfscreen", 2u << 26) {}
    C_str text;
    uint32_t value = 0;
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("pfscreen %x %x %x", m->target, m->id, m->value);
        if (m->id == CONTROL_MSG_GET_TEXT) {
            m->text = text;
            m->value = value;
        }
        return true;
    }
};

struct S_MainMenuResults {
    bool mmStart = true, mmStop = true, mmUpdate = true, mmDrain = true, mmMessage = true, mmEscape = true,
         mmSelect = true, mmBuild = true, mmDescribe = true, mmMainShown = true, pfDescribe = true, pfRefresh = true,
         pfShown = true, pfList = true, pfDifficulty = true, pfMessage = true,
         pfButtons = true, pfEscape = true, pfCreate = true, pfRename = true,
         spShown = true, spHover = true, spClick = true,
         mpLists = true, mpDescribe = true, mpSaveSlot = true, mpPick = true,
         mxCampaign = true, mxSingle = true, mxMessages = true, mxSecret = true, mxStart = true, mxOperation = true,
         ciDescribe = true, ciShown = true, ciPick = true,
         pwDescribe = true, pwServer = true, pwLock = true,
         flDescribe = true, flShown = true, flAccept = true, flHelpers = true,
         giDescribe = true, giRefresh = true, giShow = true, giStatus = true;
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
        ok &= m.m_connectState == 5 && m.m_mission == -1 && m.m_progress == -1 && !m.m_mpSettings && !m.m_options;
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
        // (The profile screen's and the missions' handlers, real, take none of these.)
        ok &= MsLogIs({"network message 2000099", "connect message 2000099"}, g_gfLog) && CsQueue() == "";
        const int state = m.m_state;
        for (int k : {1, 3}) {
            g_mmTakes[k] = true;
            send(MAIN_MSG_PLAY_MOVIE);
            ok &= g_gfLog.size() == static_cast<size_t>(k == 1 ? 1 : 2) && m.m_state == state;
            g_mmTakes[k] = false;
        }
        // (The missions' handler taking one: the rest not asked.)
        send(MENU_MSG_OPEN_POPUP);
        ok &= MsLogIs({"network message 2000002"}, g_gfLog) && CsQueue() == "";
        // The movie; the saved game (none: the load command alone).
        send(0x4000000 | MAIN_MSG_PLAY_MOVIE);
        ok &= m.m_state == GAME_STATE_PLAY_AVI;
        m.m_state = state;
        C_GameApp *app = GetGameApp();
        app->m_pendingCommand = 0;
        send(MAIN_MSG_LOAD_GAME);
        ok &= app->m_pendingCommand == APP_COMMAND_LOAD_FROM_MENU && g_gfLog.size() == 2;
        app->m_pendingCommand = 0;
        m.m_saveName = "save7.sav";
        send(MAIN_MSG_LOAD_GAME);
        ok &= app->m_pendingCommand == 0 && g_gfLog.size() == 3 && g_gfLog[2] == "request load prof\\save7.sav";
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
        ok &= escape(0).find("20000c4") != std::string::npos && g_gfLog.size() == 1;
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

    // --- (part 9h-v) the profile screen: its description, refresh, shown, list, difficulty, messages ---
    {
        C_MainMenu m;
        m.OnStart(nullptr);
        CsQueue();
        const int oldDifficulty = g_gmDifficulty;
        // The refresh's messages: no profile; a profile of difficulty d (0: none white).
        const auto refreshed = [](const char *name, int d) {
            if (!name)
                return std::string("8100000 1000015 d 7d2; 8300000 2000076 0 0; 8400000 2000076 0 0; 8500000 2000076 0 0; "
                                   "8600000 2000076 0 0; 8800000 1000001 0 0; 8900000 1000001 0 0; ");
            std::string q = CmText("8100000 1000014 d 0 '%s'; 8200000 2000076 0 0 '%s'; ", name, name);
            if (d >= 1 && d <= 4)
                for (int i = 1; i <= 4; ++i)
                    q += CmText("%x 2000076 %d 0; ", 0x8200000 + i * 0x100000, i == d ? 1 : 0);
            return q + "8800000 1000002 0 0; 8900000 1000002 0 0; ";
        };
        {
            bool ok = true;
            S_ScreenDesc desc("CHANGE PROFIL", "change_profile", 2u << 26);
            desc.AddControl(0x1B, "bcurrent profile");
            S_ControlDesc *list = desc.AddControl(0xF, "LB_Profile*");
            g_mmProfiles = {"Alpha", "Bravo"};
            g_gfLog.clear();
            ok &= m.DescribeProfileScreen(&desc, nullptr) && MsLogIs({"scan"}, g_gfLog) &&
                  CsTree(list) == "f LB_Profile* 8200000: 0 1000016 13 0 'Alpha' 0 1000016 13 0 'Bravo';" &&
                  desc.controls[0]->messages.empty();
            g_mmProfiles.clear();
            ok &= m.DescribeProfileScreen(&desc, nullptr) && list->messages.size() == 2;
            C_MainMenu::PostProfileLine("Charlie", 7);
            ok &= CsQueue() == "8200000 1000016 13 0 'Charlie'; ";
            WM(r.pfDescribe, ok);
        }
        {
            bool ok = true;
            m.m_profileName = "";
            m.RefreshProfile();
            ok &= CsQueue() == refreshed(nullptr, 0);
            m.m_profileName = "Bravo";
            for (int d = 0; d <= 5; ++d) {
                g_gmDifficulty = d;
                m.RefreshProfile();
                ok &= CsQueue() == refreshed("Bravo", d);
            }
            WM(r.pfRefresh, ok);
        }
        {
            bool ok = true;
            g_gmDifficulty = 2;
            m.m_profileName = "";
            ok &= C_MainMenu::OnProfileShown(&m, nullptr, nullptr, CONTROL_NOTIFY_SHOWN, 0) &&
                  CsQueue() == refreshed(nullptr, 0) + "8100000 1000001 0 0; 8200000 200002c 0 0; 8300000 1000001 0 0; "
                                                       "8400000 1000001 0 0; 8500000 1000001 0 0; 8600000 1000001 0 0; "
                                                       "8700000 1000002 0 0; 8800000 1000001 0 0; 8900000 1000001 0 0; "
                                                       "8a00000 1000002 0 0; ";
            m.m_profileName = "Alpha";
            ok &= C_MainMenu::OnProfileShown(&m, nullptr, nullptr, CONTROL_NOTIFY_SHOWN, 0) &&
                  CsQueue() == refreshed("Alpha", 2) + "8100000 1000001 0 0; 8200000 200002c 0 0; 8300000 1000001 0 0; "
                                                       "8400000 1000001 0 0; 8500000 1000001 0 0; 8600000 1000001 0 0; "
                                                       "8700000 1000002 0 0; 8800000 1000002 0 0; 8900000 1000002 0 0; "
                                                       "8a00000 1000002 0 0; ";
            WM(r.pfShown, ok);
        }
        {
            bool ok = true;
            C_PfLine line(0x8200003, "Bravo"), other(0x8300000, "Bravo");
            m.m_profileName = "Alpha";
            g_gfLog.clear();
            // Another control, none: false.
            ok &= !C_MainMenu::OnProfileList(&m, nullptr, &other, CONTROL_NOTIFY_CLICK, 0) &&
                  !C_MainMenu::OnProfileList(&m, nullptr, nullptr, CONTROL_NOTIFY_CLICK, 0) && g_gfLog.empty();
            // A click: the line's profile current.
            ok &= C_MainMenu::OnProfileList(&m, nullptr, &line, CONTROL_NOTIFY_CLICK, 0) && m.m_profileName == "Bravo" &&
                  MsLogIs({"line 0 200002f 0", "select player Bravo", "reset game"}, g_gfLog) &&
                  CsQueue() == refreshed("Bravo", 2);
            // A click on no line: nothing.
            line.text = "";
            g_gfLog.clear();
            ok &= C_MainMenu::OnProfileList(&m, nullptr, &line, CONTROL_NOTIFY_CLICK, 0) && m.m_profileName == "Bravo" &&
                  MsLogIs({"line 0 200002f 0"}, g_gfLog) && CsQueue() == "";
            // The double click on the current one: back; on another: nothing.
            line.text = "Bravo";
            ok &= C_MainMenu::OnProfileList(&m, nullptr, &line, CONTROL_NOTIFY_13, 0) && CsQueue() == "0 2000003 0 0; ";
            line.text = "bravo";
            ok &= C_MainMenu::OnProfileList(&m, nullptr, &line, CONTROL_NOTIFY_13, 0) && CsQueue() == "";
            line.text = "";
            ok &= C_MainMenu::OnProfileList(&m, nullptr, &line, CONTROL_NOTIFY_13, 0) && CsQueue() == "";
            m.m_profileName = "";
            line.text = "x";
            ok &= C_MainMenu::OnProfileList(&m, nullptr, &line, CONTROL_NOTIFY_13, 0) && CsQueue() == "";
            // Another notification: taken, not asked.
            g_gfLog.clear();
            ok &= C_MainMenu::OnProfileList(&m, nullptr, &line, CONTROL_NOTIFY_ENTER, 0) && g_gfLog.empty() && CsQueue() == "";
            WM(r.pfList, ok);
        }
        {
            bool ok = true;
            C_PfLine hard(0x8500000, ""), create(0x8700000, "");
            const std::string posts = "8200000 1000002 0 0; 8300000 1000001 0 0; 8400000 1000001 0 0; 8500000 1000001 0 0; "
                                      "8600000 1000001 0 0; 8700000 1000002 0 0; 8800000 1000002 0 0; 8900000 1000002 0 0; "
                                      "8a00000 1000002 0 0; ";
            m.m_step = 0;
            g_gfLog.clear();
            ok &= !C_MainMenu::OnDifficulty(&m, nullptr, &hard, CONTROL_NOTIFY_CLICK, 0) && CsQueue() == "" && g_gfLog.empty();
            m.m_step = 3;
            ok &= !C_MainMenu::OnDifficulty(&m, nullptr, nullptr, CONTROL_NOTIFY_CLICK, 0) && CsQueue() == posts && m.m_step == 3;
            ok &= !C_MainMenu::OnDifficulty(&m, nullptr, &create, CONTROL_NOTIFY_CLICK, 0) && CsQueue() == posts &&
                  m.m_step == 3 && g_gfLog.empty();
            ok &= C_MainMenu::OnDifficulty(&m, nullptr, &hard, CONTROL_NOTIFY_CLICK, 0) && CsQueue() == posts &&
                  m.m_step == 0 && MsLogIs({"set difficulty 3", "line 0 2000076 1", "save profile"}, g_gfLog);
            for (int d = 1; d <= 4; ++d) {
                C_PfLine button(0x8200000 + d * 0x100000, "");
                m.m_step = 3;
                g_gfLog.clear();
                ok &= C_MainMenu::OnDifficulty(&m, nullptr, &button, CONTROL_NOTIFY_CLICK, 0) && g_gmDifficulty == d;
                CsQueue();
            }
            WM(r.pfDifficulty, ok);
        }
        {
            bool ok = true;
            S_MrWorld world = MrWorld("pfm");
            m.m_scene = As<I3D_scene>(world.scene);
            C_MsScreen box(m.m_scene, "pfbox", 0), main(m.m_scene, "pfmain", 1u << 26), quit(m.m_scene, "pfquit", 4u << 26);
            C_PfScreen screen(m.m_scene);
            m.m_screens = {&box, &main, &screen};
            S_ControlMessage message;
            const auto send = [&](uint32_t id) {
                message.id = id;
                g_gfLog.clear();
                return m.OnProfileMessage(&message);
            };
            // Escape: taken only on the profile screen while a difficulty is picked.
            g_menuScreens = {&main, &screen};
            m.m_step = 3;
            ok &= send(MENU_MSG_ESCAPE) && send(0x4000000 | MENU_MSG_ESCAPE);
            m.m_step = 0;
            ok &= !send(MENU_MSG_ESCAPE);
            m.m_step = 3;
            g_menuScreens = {&main, &quit};
            ok &= !send(MENU_MSG_ESCAPE);
            g_menuScreens.clear();
            ok &= !send(MENU_MSG_ESCAPE);
            m.m_step = 0;
            // The deletion: no profile; failing; deleted (the next line picked; none).
            m.m_profileName = "";
            ok &= send(CONTROL_MSG_REMOVE_SELECTED) && g_gfLog.empty() && CsQueue() == "";
            m.m_profileName = "Alpha";
            g_mmDeletes = false;
            ok &= send(CONTROL_MSG_REMOVE_SELECTED) && MsLogIs({"delete Alpha"}, g_gfLog) && CsQueue() == "" &&
                  m.m_profileName == "Alpha";
            g_mmDeletes = true;
            screen.text = "Bravo";
            screen.value = 1;
            ok &= send(CONTROL_MSG_REMOVE_SELECTED) && m.m_profileName == "Bravo" &&
                  MsLogIs({"delete Alpha", "pfscreen 8200008 2000017 0", "pfscreen 8200008 200002f 0", "select player Bravo",
                           "reset game"}, g_gfLog) && CsQueue() == refreshed("Bravo", 4);
            screen.value = 0xFFFFFFFF;
            ok &= send(CONTROL_MSG_REMOVE_SELECTED) && m.m_profileName.IsEmpty() &&
                  MsLogIs({"delete Bravo", "pfscreen 8200008 2000017 0", "pfscreen 8200008 200002f 0", "select player (null)"},
                          g_gfLog) && CsQueue() == refreshed(nullptr, 0);
            // The name's box edited.
            ok &= send(0x20000C3) && CsQueue() == "8100000 2000016 0 0; ";
            ok &= !send(0x2000099) && !send(MAIN_MSG_PLAY_MOVIE) && CsQueue() == "";
            m.m_screens.clear();
            g_menuScreens.clear();
            WM(r.pfMessage, ok);
        }
        // (Part 9h-vi.) The buttons: delete (asked), rename and create (the name box edited); the box's Escape and
        // Return (a new profile made, a profile renamed; the questions when they fail).
        {
            C_MessageBox box(nullptr, GfFrame(FRAME_DUMMY, "pfmbox"));
            m.m_messageBox = &box;
            C_GameConfig config;
            C_GameConfig *const oldConfig = g_gameProfile.m_config;
            g_gameProfile.m_config = &config;
            C_PfLine nameBox(0x8100000, ""), rename(0x8800000, ""), create(0x8700000, ""), del(0x8900000, ""),
                back(0x8A00000, ""), easy(0x8300000, "");
            const auto press = [&](C_Control *c, uint32_t notification) {
                g_gfLog.clear();
                return C_MainMenu::OnProfileButton(&m, nullptr, c, notification, 0);
            };
            // The question shown: its kind and text; closed again.
            const auto asked = [&](int kind, uint32_t text) {
                const std::string q = CsQueue();
                const bool shown = box.GetKind() == kind && q.find(CmText("100000 1000015 ")) != std::string::npos &&
                                   q.find(CmText(" %x;", text)) != std::string::npos;
                box.Close();
                CsQueue();
                return shown;
            };
            std::string off, on;
            for (uint32_t id = 0x8700000; id <= 0x8A00000; id += 0x100000) {
                off += CmText("%x 1000001 0 0; ", id);
                on += CmText("%x 1000002 0 0; ", id);
            }
            {
                bool ok = true;
                m.m_step = 0;
                ok &= press(&del, CONTROL_NOTIFY_CLICK) && asked(1, 0x7D4) && m.m_step == 0;
                // Its answers: yes deletes (CONTROL_MSG_REMOVE_SELECTED), no (Escape) nothing.
                ok &= press(&del, CONTROL_NOTIFY_CLICK) && box.GetKind() == 1;
                CsQueue();
                box.Unknown20(1);
                const std::string escaped = CsQueue();
                ok &= escaped.find("0 1 ") == std::string::npos && escaped.find(" 2000017") == std::string::npos &&
                      box.GetKind() == -1;
                ok &= press(&rename, CONTROL_NOTIFY_CLICK) && m.m_step == 2 &&
                      CsQueue() == "8100000 1000002 0 0; 8100000 2000016 0 0; 8200000 1000001 0 0; " + off;
                ok &= press(&create, CONTROL_NOTIFY_CLICK) && m.m_step == 1 &&
                      CsQueue() == "8100000 1000015 d 7d2; 8100000 1000002 0 0; 8100000 2000016 0 0; 8200000 1000001 0 0; " + off +
                                   "8300000 2000076 0 0; 8400000 2000076 0 0; 8500000 2000076 0 0; 8600000 2000076 0 0; ";
                ok &= press(&back, CONTROL_NOTIFY_CLICK) && press(&easy, CONTROL_NOTIFY_CLICK) && press(nullptr, CONTROL_NOTIFY_CLICK) &&
                      CsQueue() == "" && g_gfLog.empty() && m.m_step == 1;
                // The box's other notifications: nothing.
                ok &= press(&nameBox, CONTROL_NOTIFY_CLICK) && CsQueue() == "" && g_gfLog.empty();
                WM(r.pfButtons, ok);
            }
            {
                bool ok = true;
                g_gmDifficulty = 2;
                m.m_profileName = "Alpha";
                ok &= press(&nameBox, CONTROL_NOTIFY_ESCAPE) && m.m_step == 1 &&
                      CsQueue() == refreshed("Alpha", 2) + "8100000 1000001 0 0; 8200000 1000002 0 0; " + on;
                m.m_profileName = "";
                ok &= press(&nameBox, CONTROL_NOTIFY_ESCAPE) &&
                      CsQueue() == refreshed(nullptr, 0) + "8100000 1000001 0 0; 8200000 1000002 0 0; 8700000 1000002 0 0; "
                                                           "8a00000 1000002 0 0; ";
                WM(r.pfEscape, ok);
            }
            {
                bool ok = true;
                // Typed with spaces after (cut) and before (kept); a character above 0x7F stops the cut.
                nameBox.text = "  New One \t ";
                m.m_step = 1;
                g_mmCreate = -1;
                ok &= press(&nameBox, CONTROL_NOTIFY_RETURN) && MsLogIs({"line 0 200002f 0", "create '  New One'"}, g_gfLog) &&
                      asked(2, 0x251C) && m.m_step == 1;
                g_mmCreate = -2;
                ok &= press(&nameBox, CONTROL_NOTIFY_RETURN) && asked(2, 0x251D) && m.m_step == 1;
                // (A UTF-8 e acute: its last byte above 0x7F; the output stays UTF-8.)
                nameBox.text = "Abc\xC3\xA9 ";
                ok &= press(&nameBox, CONTROL_NOTIFY_RETURN) && MsLogIs({"line 0 200002f 0", "create 'Abc<c3><a9>'"}, g_gfLog) &&
                      asked(2, 0x251D);
                nameBox.text = "  New One \t ";
                g_mmCreate = 0;
                g_mmProfiles = {"Zed", "  New One"};
                ok &= press(&nameBox, CONTROL_NOTIFY_RETURN) && m.m_step == 3 && m.m_profileName == "  New One" &&
                      MsLogIs({"line 0 200002f 0", "create '  New One'", "select player   New One", "reset game", "save profile",
                               "scan"}, g_gfLog) &&
                      CsQueue() == "8100000 1000001 0 0; 8200000 2000018 0 0; 8300000 1000002 0 0; 8400000 1000002 0 0; "
                                   "8500000 1000002 0 0; 8600000 1000002 0 0; 8200000 1000016 13 0 'Zed'; "
                                   "8200000 1000016 13 0 '  New One'; 8100000 1000014 d 0 '  New One'; "
                                   "8200000 2000076 0 0 '  New One'; ";
                // Return in another step: nothing.
                m.m_step = 0;
                ok &= press(&nameBox, CONTROL_NOTIFY_RETURN) && g_gfLog.empty() && CsQueue() == "";
                WM(r.pfCreate, ok);
            }
            {
                bool ok = true;
                m.m_profileName = "Alpha";
                m.m_step = 2;
                g_gmDifficulty = 1;
                // Nothing typed: the name as it was (still renaming).
                nameBox.text = " \t ";
                ok &= press(&nameBox, CONTROL_NOTIFY_RETURN) && m.m_step == 2 && MsLogIs({"line 0 200002f 0"}, g_gfLog) &&
                      CsQueue() == refreshed("Alpha", 1) + "8100000 1000001 0 0; 8100000 1000014 d 0 'Alpha'; "
                                                           "8200000 1000002 0 0; " + on;
                nameBox.text = "Renamed ";
                g_mmRename = -1;
                ok &= press(&nameBox, CONTROL_NOTIFY_RETURN) && MsLogIs({"line 0 200002f 0", "rename 'Alpha' 'Renamed'"}, g_gfLog) &&
                      asked(2, 0x2526) && m.m_step == 2 && m.m_profileName == "Alpha";
                g_mmRename = -2;
                ok &= press(&nameBox, CONTROL_NOTIFY_RETURN) && asked(2, 0x2527) && m.m_step == 2;
                g_mmRename = 0;
                // A name of one character.
                nameBox.text = "Z";
                g_mmProfiles.clear();
                ok &= press(&nameBox, CONTROL_NOTIFY_RETURN) && m.m_step == 0 && m.m_profileName == "Z" &&
                      MsLogIs({"line 0 200002f 0", "rename 'Alpha' 'Z'", "scan", "last profile Z"}, g_gfLog);
                CsQueue();
                m.m_step = 2;
                m.m_profileName = "Alpha";
                nameBox.text = "Renamed ";
                g_mmProfiles = {"Renamed"};
                ok &= press(&nameBox, CONTROL_NOTIFY_RETURN) && m.m_step == 0 && m.m_profileName == "Renamed" &&
                      MsLogIs({"line 0 200002f 0", "rename 'Alpha' 'Renamed'", "scan", "last profile Renamed"}, g_gfLog) &&
                      CsQueue() == "8100000 1000001 0 0; 8200000 2000018 0 0; 8200000 1000002 0 0; " + on +
                                   "8200000 1000016 13 0 'Renamed'; " + refreshed("Renamed", 1);
                WM(r.pfRename, ok);
            }
            g_mmProfiles.clear();
            g_gameProfile.m_config = oldConfig;
            m.m_messageBox = nullptr;
        }
        g_gmDifficulty = oldDifficulty;
        m.m_mpSettings->m_servers.clear();
        m.OnStop();
    }

    // --- (part 9h-vii) the single-player screen: shown; a mode hovered (its side, its text); clicked (started) ---
    {
        C_MainMenu m;
        alignas(16) static uint8_t spCampaignMemory[sizeof(C_Campaign)];
        std::memset(spCampaignMemory, 0, sizeof spCampaignMemory);
        C_Campaign *const oldCampaign = g_gameProfile.m_campaign;
        auto *campaign = reinterpret_cast<C_Campaign *>(spCampaignMemory);
        g_gameProfile.m_campaign = campaign;
        S_PlayMode playMode;
        S_PlayMode *const oldPlayMode = g_gameProfile.m_playMode;
        g_gameProfile.m_playMode = &playMode;
        const int oldSession = g_mpSettings->sessionType;
        g_controlMessages.clear();
        {
            bool ok = true;
            m.m_saveName = "save2";
            ok &= C_MainMenu::OnSingleplayerShown(&m, nullptr, nullptr, CONTROL_NOTIFY_SHOWN, 0) && m.m_saveName.IsEmpty() &&
                  CsQueue() == "c700000 1000002 0 0; c800000 1000002 0 0; ";
            WM(r.spShown, ok);
        }
        const uint32_t buttons[] = {0xC400000, 0xC500000, 0xC600000, 0xC700000, 0xC800000, 0xC900000, 0xCA00000, 0xCB00000};
        const uint32_t texts[] = {0x8A3, 0x8A4, 0x8A5, 0x8A6, 0x8A7, 0x8A3, 0x8A4, 0x8A5};
        {
            bool ok = true;
            for (int i = 0; i < 8; ++i) {
                C_PfLine button(buttons[i] | 2, "");
                ok &= C_MainMenu::OnSingleplayerButton(&m, nullptr, &button, CONTROL_NOTIFY_ENTER, 0) &&
                      CsQueue() == CmText("c300000 2000018 0 0; c200000 2000019 %d 0; c300000 1000017 2 8a2; c300000 1000017 2 7d2; "
                                          "c300000 1000017 3 %x; ", i, texts[i]);
            }
            C_PfLine back(0xCC00000, ""), other(0xCD00000, "");
            ok &= C_MainMenu::OnSingleplayerButton(&m, nullptr, &back, CONTROL_NOTIFY_ENTER, 0) &&
                  CsQueue() == "c300000 2000018 0 0; c200000 200001a 0 0; ";
            ok &= !C_MainMenu::OnSingleplayerButton(&m, nullptr, &other, CONTROL_NOTIFY_ENTER, 0) &&
                  CsQueue() == "c300000 2000018 0 0; ";
            ok &= !C_MainMenu::OnSingleplayerButton(&m, nullptr, nullptr, CONTROL_NOTIFY_ENTER, 0) && CsQueue() == "";
            ok &= !C_MainMenu::OnSingleplayerButton(&m, nullptr, &back, CONTROL_NOTIFY_13, 0) &&
                  CsQueue() == "c300000 2000018 0 0; " && g_mpSettings->sessionType == oldSession;
            WM(r.spHover, ok);
        }
        {
            bool ok = true;
            // Each mode: the campaign (or a single mission), the play mode; what is posted.
            const int campaigns[] = {0, 0, 0, -1, -1, 1, 1, 1};
            const int modes[] = {1, 2, 3, 0, 3, 1, 2, 3};
            for (int i = 0; i < 8; ++i) {
                C_PfLine button(buttons[i], "");
                g_mpSettings->sessionType = 2;
                campaign->m_current = 7;
                playMode.singleMission = 5;
                playMode.mode = 9;
                ok &= C_MainMenu::OnSingleplayerButton(&m, nullptr, &button, CONTROL_NOTIFY_CLICK, 0) &&
                      g_mpSettings->sessionType == 0 && playMode.mode == modes[i];
                if (campaigns[i] >= 0)
                    ok &= campaign->m_current == campaigns[i] && playMode.singleMission == 0 &&
                          CsQueue() == "c300000 2000018 0 0; c200000 200001a 0 0; 0 6001033 0 0; ";
                else
                    ok &= campaign->m_current == 7 && playMode.singleMission == 1 &&
                          CsQueue() == "c300000 2000018 0 0; c200000 200001a 0 0; 0 6001074 0 0; ";
            }
            C_PfLine back(0xCC00000, ""), other(0xC300000, "");
            ok &= C_MainMenu::OnSingleplayerButton(&m, nullptr, &back, CONTROL_NOTIFY_CLICK, 0) &&
                  CsQueue() == "c300000 2000018 0 0; c200000 200001a 0 0; 0 2000003 0 0; ";
            g_mpSettings->sessionType = 2;
            ok &= !C_MainMenu::OnSingleplayerButton(&m, nullptr, &other, CONTROL_NOTIFY_CLICK, 0) &&
                  CsQueue() == "c300000 2000018 0 0; c200000 200001a 0 0; " && g_mpSettings->sessionType == 0;
            WM(r.spClick, ok);
        }
        g_mpSettings->sessionType = oldSession;
        g_gameProfile.m_playMode = oldPlayMode;
        g_gameProfile.m_campaign = oldCampaign;
    }

    // --- (part 9h-viii) the missions screen: its lists, its description, a save slot shown, a mission picked ---
    {
        C_MainMenu m;
        alignas(16) static uint8_t mpCampaignMemory[sizeof(C_Campaign)];
        std::memset(mpCampaignMemory, 0, sizeof mpCampaignMemory);
        C_Campaign *const oldCampaign = g_gameProfile.m_campaign;
        auto *campaign = reinterpret_cast<C_Campaign *>(mpCampaignMemory);   // (its vector zeroed: empty)
        campaign->m_campaigns = {nullptr, nullptr};
        g_gameProfile.m_campaign = campaign;
        S_PlayMode playMode;
        S_PlayMode *const oldPlayMode = g_gameProfile.m_playMode;
        g_gameProfile.m_playMode = &playMode;
        const std::vector<int> oldOperations = g_lpMissions;
        g_lpMissions = {2, 1};
        g_mmMissionCounts = {3, 3};
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "self";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnMissionPick)] = "pick";
        const auto clones = [](int n) {
            g_mrClones.clear();
            for (int i = 0; i < n; ++i)
                g_mrClones.push_back(As<I3D_frame>(MsFrame(strdup(CmText("mpc%d", i).c_str()))));
        };
        {
            bool ok = true;
            S_MrObj *group = MsFrame("mpgroup"), *opT = MsFrame("mpop"), *misT = MsFrame("mpmis");
            As<I3D_frame>(opT)->m_localMatrix(3) = S_vector(0.5f, -0.25f, 0.125f);
            S_ScreenDesc d("SINGLE MISSION 2", "single mission 2", 5u << 26);
            clones(16);
            g_gfLog.clear();
            m.DescribeCampaignList(&d, As<I3D_frame>(group), As<I3D_frame>(opT), As<I3D_frame>(misT));
            // (The origin: a copy of the operation template's place, shown as ORIGIN.)
            const auto masked = [](const S_ControlDesc *list, const S_vector &want, bool &good) {
                std::string tree = CsTree(list);
                const auto *origin = reinterpret_cast<const S_vector *>(list->messages[3].value2);
                good &= origin && origin->x == want.x && origin->y == want.y && origin->z == want.z;
                const std::string hex = CmText("100006b 0 %x", static_cast<uint32_t>(list->messages[3].value2));
                const size_t at = tree.find(hex);
                if (at != std::string::npos)
                    tree.replace(at, hex.size(), "100006b 0 ORIGIN");
                return tree;
            };
            ok &= d.controls.size() == 1 &&
                  masked(d.controls[0], S_vector(0.5f, -0.25f, 0.125f), ok) ==
                    "23 GB_Campaign 14100000: 0 100000b 14600000 0 0 1000028 11 0 0 100002a 0 3d03126f 0 100006b 0 ORIGIN 0 200002c"
                    " 0 0 { 6 Campaign00 14101000: 0 1000015 d 700 0 1000005 0 0; 6 Mission00 14102000: 0 1000015 c 800 0 1000009 0"
                    " 0 0 1000007 4001000 self pick; 6 Mission01 14103000: 0 1000015 c 801 0 1000009 1 0 0 1000007 4001000 self pic"
                    "k; 6 Campaign01 14104000: 0 1000015 d 701 0 1000005 0 0; 6 Mission02 14105000: 0 1000015 c 802 0 1000009 2 0 0"
                    " 1000007 4001000 self pick; 6 Campaign03 14106000: 0 1000015 d 710 0 1000005 0 0; 6 Mission03 14107000: 0 1000"
                    "015 c 810 0 1000009 3 0 0 1000007 4001000 self pick; 6 Mission04 14108000: 0 1000015 c 811 0 1000009 4 0 0 100"
                    "0007 4001000 self pick; 6 Campaign04 14109000: 0 1000015 d 711 0 1000005 0 0; 6 Mission05 1410a000: 0 1000015 "
                    "c 812 0 1000009 5 0 0 1000007 4001000 self pick; };";
            // Each line a copy of its template under the group; operations by campaign, missions numbered through.
            ok &= GmInOrder(Join(g_gfLog), {"operations 0", "clone tree mpop 0", "mpc0 34 Campaign00", "mpc0 38 mpgroup 0",
                                            "missions 0 0", "clone tree mpmis 0", "mpc1 34 Mission00", "mpc2 34 Mission01",
                                            "clone tree mpop 0", "mpc3 34 Campaign01", "missions 0 1", "mpc4 34 Mission02",
                                            "operations 1", "mpc5 34 Campaign03", "missions 1 0", "mpc6 34 Mission03",
                                            "mpc7 34 Mission04", "mpc8 34 Campaign04", "missions 1 1", "mpc9 34 Mission05"});
            clones(16);
            g_gfLog.clear();
            m.DescribeSingleList(&d, As<I3D_frame>(group), As<I3D_frame>(misT));
            As<I3D_frame>(misT)->m_localMatrix(3) = S_vector(0.0f, 1.0f, 2.0f);
            ok &= d.controls.size() == 2 &&
                  masked(d.controls[1], S_vector(0.0f, 0.0f, 0.0f), ok) ==
                    "23 GB_SingleMission 14200000: 0 100000b 14600000 0 0 1000028 11 0 0 100002a 0 3d03126f 0 100006b 0 ORIGIN 0 20"
                    "0002c 0 0 { 6 SMMission01 14201000: 0 1000015 c 801 0 1000009 1 0 0 1000007 4001000 self pick; 6 SMMission02 1"
                    "4202000: 0 1000015 c 802 0 1000009 2 0 0 1000007 4001000 self pick; 6 SMMission03 14203000: 0 1000015 c 810 0 "
                    "1000009 3 0 0 1000007 4001000 self pick; 6 SMMission04 14204000: 0 1000015 c 811 0 1000009 4 0 0 1000007 40010"
                    "00 self pick; 6 SMMission05 14205000: 0 1000015 c 812 0 1000009 5 0 0 1000007 4001000 self pick; };" &&
                  GmInOrder(Join(g_gfLog), {"clone tree mpmis 0", "mpc0 34 SMMission01", "mpc4 34 SMMission05"});
            m.m_frames.clear();
            WM(r.mpLists, ok);
        }
        {
            // The description: the frames ("video" off; "screen_shot"'s own mesh; the templates off; the groups made),
            // the controls, the lists between them.
            bool ok = true;
            S_MrObj *model = MsFrame("mpmodel"), *video = MsFrame("mpvideo"), *opT = MsFrame("mpdop"), *misT = MsFrame("mpdmis");
            S_MrObj *shot = MrObj("mpshot", g_mrModelVt);
            void *const oldDuplicate = g_mrModelVt[0xA0 / 4], *const oldSetPos = g_msFrameVt[0x08 / 4];
            g_mrModelVt[0xA0 / 4] = reinterpret_cast<void *>(&MrFake<0xA0>);
            g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
            S_MrObj *mesh = MrObj("mpshotmesh", g_mrModelVt);
            mesh->vtable[0x18 / 4] = reinterpret_cast<void *>(&MrFake<0x18>);
            *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(shot) + 0x1E0) = mesh;
            g_msChildren[{model, "video"}] = video;
            g_msChildren[{model, "screen_shot"}] = shot;
            g_msChildren[{model, "blong"}] = opT;
            g_msChildren[{model, "bshort"}] = misT;
            S_MrObj *gc = MsFrame("mpgc"), *gs = MsFrame("mpgs");
            g_mrMade[FRAME_DUMMY] = {gc, gs};
            clones(16);
            S_ScreenDesc d("SINGLE MISSION 2", "single mission 2", 5u << 26);
            g_gfLog.clear();
            ok &= m.DescribeMissionsScreen(&d, As<I3D_frame>(model));
            std::string names;
            for (const S_ControlDesc *c : d.controls)
                names += CmText("%x %s %x|", c->type, c->name.c_str(), c->id);
            ok &= names == "13 text_screen 14100000|4 screen_shot 14200000|12 text00 14300000|14 bload mission 14400000|"
                           "14 bexit 14500000|8 scroll00 14600000|23 GB_Campaign 14700000|23 GB_SingleMission 14800000|"
                           "13 text_list of profiles 14900000|13 text_h&dversion 14a00000|14 bload lastsave 14b00000|"
                           "14 bexit01 14c00000|";
            ok &= d.controls.size() == 12 && CsTree(d.controls[0]) == "13 text_screen 14100000: 0 1000015 a 8cb;" &&
                  CsTree(d.controls[1]) == "4 screen_shot 14200000:;" && CsTree(d.controls[2]) == "12 text00 14300000: 0 2000073 0 0;" &&
                  CsTree(d.controls[3]) == "14 bload mission 14400000: 0 1000015 d 935 0 1000001 0 0 0 600101e 0 0;" &&
                  CsTree(d.controls[4]) == "14 bexit 14500000: 0 1000015 d 8d0 0 6001003 0 0;" &&
                  CsTree(d.controls[5]) == "8 scroll00 14600000:;" &&
                  CsTree(d.controls[8]) == "13 text_list of profiles 14900000: 0 1000015 b 8d2;" &&
                  CsTree(d.controls[9]) == "13 text_h&dversion 14a00000: 0 1000015 28 7d6;" &&
                  CsTree(d.controls[10]) == "14 bload lastsave 14b00000: 0 1000015 d 839 0 1000001 0 0 0 6001075 0 0;" &&
                  CsTree(d.controls[11]) == "14 bexit01 14c00000: 0 1000015 d 8d0 0 1000004 0 0;";
            ok &= GmInOrder(Join(g_gfLog), {"mpvideo on 0", "mpshot a0", "mpshotmesh 18", "mpgc 38 mpmodel 0", "mpgc 34 GB_Campaign",
                                            "mpgs 38 mpmodel 0", "mpgs 34 GB_SingleMission", "mpdop 24", "mpdop on 0",
                                            "mpdmis 24", "mpdmis on 0", "mpgc 24", "mpgs 24", "mpdop 38 mpgc 1",
                                            "mpdmis 38 mpgc 1", "mpc0 34 Campaign00", "mpdop 38 mpgs 1", "SMMission01"}) &&
                  GmInOrder(Join(g_gfLog), {"mpgc 8 (0 0 0)"}) && GmInOrder(Join(g_gfLog), {"mpgs 8 (0 0 0)"});
            // No templates: false (nothing described).
            g_msChildren.erase({model, "bshort"});
            g_mrMade[FRAME_DUMMY] = {MsFrame("mpgc2"), MsFrame("mpgs2")};
            S_ScreenDesc d2("SINGLE MISSION 2", "single mission 2", 5u << 26);
            ok &= !m.DescribeMissionsScreen(&d2, As<I3D_frame>(model)) && d2.controls.empty();
            g_mrMade.erase(FRAME_DUMMY);
            g_mrModelVt[0xA0 / 4] = oldDuplicate;
            g_msFrameVt[0x08 / 4] = oldSetPos;
            m.m_frames.clear();
            m.m_models.clear();
            WM(r.mpDescribe, ok);
        }
        {
            // The play mode's save slot: its file picked; "load the last save" on for its own mission only.
            bool ok = true;
            for (int i = 0; i < 5; ++i) {
                g_mmSlotMissions[i] = 10 + i;
                g_mmSlotFiles[i] = CmText("slot%d.sav", i).c_str();
            }
            const int singles[] = {0, 0, 0, 1, 1};
            const int modes[] = {1, 2, 3, 0, 3};
            for (int i = 0; i < 5; ++i) {
                playMode.singleMission = singles[i];
                playMode.mode = modes[i];
                m.ShowSaveSlot(10 + i);
                ok &= m.m_saveName == CmText("slot%d.sav", i).c_str() && CsQueue() == "14b00000 1000002 0 0; ";
                m.ShowSaveSlot(11 + i);
                ok &= m.m_saveName == CmText("slot%d.sav", i).c_str() && CsQueue() == "14b00000 1000001 0 0; ";
            }
            g_mmSlotMissions[0] = -1;
            playMode.singleMission = 0;
            playMode.mode = 1;
            m.ShowSaveSlot(-1);
            ok &= CsQueue() == "14b00000 1000001 0 0; ";
            // Another mode: the name emptied, nothing posted; another single mission flag: off.
            m.m_saveName = "x";
            playMode.mode = 0;
            m.ShowSaveSlot(10);
            ok &= m.m_saveName.IsEmpty() && CsQueue() == "";
            playMode.singleMission = 1;
            playMode.mode = 1;
            m.m_saveName = "x";
            m.ShowSaveSlot(10);
            ok &= m.m_saveName.IsEmpty() && CsQueue() == "";
            playMode.singleMission = 2;
            m.ShowSaveSlot(10);
            ok &= m.m_saveName.IsEmpty() && CsQueue() == "14b00000 1000001 0 0; ";
            WM(r.mpSaveSlot, ok);
        }
        {
            // A mission picked: kept, its slot, load on, its picture, its description (from its text 0x1194 on).
            bool ok = true;
            const auto PickIs = [](const std::string &expected) {
                const std::string queue = CsQueue();
                if (queue != expected)
                    std::printf("MPPICK %s\nMPPICK expected %s\n",queue.c_str(), expected.c_str());
                return queue == expected;
            };
            playMode.singleMission = 0;
            playMode.mode = 3;
            g_mmSlotMissions[2] = 4;
            C_MpLine line(0x14700005, 4), second(0x14700007, 1);
            g_mmMissionCounts = {3, 4};
            m.m_operationPicked = true;
            g_mmMissionText = 0x1193;
            g_gfLog.clear();
            ok &= C_MainMenu::OnMissionPick(&m, nullptr, &line, CONTROL_NOTIFY_CLICK, 0) && m.m_mission == 4 && !m.m_operationPicked &&
                  MsLogIs({"mission text 1 1 1"}, g_gfLog) &&
                  PickIs("14b00000 1000002 0 0; 14400000 1000002 0 0; 14300000 2000018 0 0; 14200000 100003a 0 0 'img1_1'; ");
            g_mmMissionText = 0x1200;
            g_textEmptyFrom = 0x1206;
            const std::string head = "14b00000 1000001 0 0; 14400000 1000002 0 0; 14300000 2000018 0 0; 14200000 100003a 0 0 'img0_1'; "
                                     "14300000 1000017 2 8d7; 14300000 1000017 d 1201; 14300000 1000017 2 1202; "
                                     "14300000 1000017 2 7d2; 14300000 1000017 3 1203; 14300000 1000017 2 7d2; ";
            ok &= C_MainMenu::OnMissionPick(&m, nullptr, &second, CONTROL_NOTIFY_CLICK, 0) && m.m_mission == 1 &&
                  PickIs(head + "14300000 1000017 2 1204; 14300000 1000017 2 1205; 14300000 1000017 2 189c; ");
            playMode.mode = 1;
            g_textEmptyFrom = 0x1300;
            ok &= C_MainMenu::OnMissionPick(&m, nullptr, &second, CONTROL_NOTIFY_CLICK, 0) &&
                  PickIs(head +
                                   "14300000 1000017 2 1204; 14300000 1000017 2 1205; 14300000 1000017 2 1206; "
                                   "14300000 1000017 2 1207; 14300000 1000017 2 1208; 14300000 1000017 2 1209; ");
            // The second campaign's first mission (at the first's count); a text id of 0x1194 described.
            C_MpLine third(0x14700009, 3);
            g_mmMissionText = 0x1194;
            g_textEmptyFrom = 0x1198;
            g_gfLog.clear();
            ok &= C_MainMenu::OnMissionPick(&m, nullptr, &third, CONTROL_NOTIFY_CLICK, 0) && m.m_mission == 3 &&
                  MsLogIs({"mission text 1 0 1"}, g_gfLog) &&
                  PickIs("14b00000 1000001 0 0; 14400000 1000002 0 0; 14300000 2000018 0 0; 14200000 100003a 0 0 'img1_0'; "
                         "14300000 1000017 2 8d7; 14300000 1000017 d 1195; 14300000 1000017 2 1196; "
                         "14300000 1000017 2 7d2; 14300000 1000017 3 1197; 14300000 1000017 2 7d2; ");
            g_mmMissionCounts = {3, 3};
            g_textEmptyFrom = 0;
            ok &= !C_MainMenu::OnMissionPick(&m, nullptr, nullptr, CONTROL_NOTIFY_CLICK, 0) && PickIs("");
            WM(r.mpPick, ok);
        }
        g_lpMissions = oldOperations;
        std::vector<C_CampaignRoot *>().swap(campaign->m_campaigns);
        g_gameProfile.m_playMode = oldPlayMode;
        g_gameProfile.m_campaign = oldCampaign;
    }

    // --- (part 9h-ix) the missions' messages; the campaign's and the single missions' lists shown; an operation ---
    {
        C_MainMenu m;
        alignas(16) static uint8_t mxCampaignMemory[sizeof(C_Campaign)];
        std::memset(mxCampaignMemory, 0, sizeof mxCampaignMemory);
        C_Campaign *const oldCampaign = g_gameProfile.m_campaign;
        auto *campaign = reinterpret_cast<C_Campaign *>(mxCampaignMemory);   // (its vector zeroed: empty)
        campaign->m_campaigns = {nullptr, nullptr};
        g_gameProfile.m_campaign = campaign;
        S_PlayMode playMode;
        S_PlayMode *const oldPlayMode = g_gameProfile.m_playMode;
        g_gameProfile.m_playMode = &playMode;
        static C_HumanStore store;
        C_HumanStore *const oldStore = g_gameProfile.m_humanStore;
        g_gameProfile.m_humanStore = &store;
        const std::vector<int> oldOperations = g_lpMissions;
        g_lpMissions = {2, 1};
        g_mmMissionCounts = {3, 3};
        const int oldSession = g_mpSettings->sessionType, oldProgress = g_dbProgress;
        const std::string reset = "14700000 2000085 0 0; 14800000 2000085 0 0; ";
        const std::string tail = "14200000 1000039 0 0; 14300000 2000018 0 0; 14400000 1000001 0 0; 14b00000 1000001 0 0; ";
        {
            // The campaign's: its operations' lines and their missions' up to the one after the progress.
            bool ok = true;
            playMode.mode = 1;
            campaign->SetCurrentIndex(0);
            g_dbProgress = 0;
            m.m_saveName = "x";
            g_gfLog.clear();
            m.ShowCampaignMissions();
            ok &= m.m_saveName.IsEmpty() && m.m_progress == 0 &&
                  MsLogIs({"progress 0", "operations 0", "missions 0 0"}, g_gfLog) &&
                  CsQueue() == "14100000 1000015 a 8d3; " + reset + "14600000 100000b 14700000 0; " + tail +
                                   "14700000 2000083 14701000 0; 14700000 2000083 14702000 0; 14700000 2000083 14703000 0; ";
            // The second campaign's lines after the first's (3 missions, 2 operations); all shown; no title.
            playMode.mode = 0;
            campaign->SetCurrentIndex(1);
            g_dbProgress = 5;
            g_gfLog.clear();
            m.ShowCampaignMissions();
            ok &= m.m_progress == 5 &&
                  MsLogIs({"progress 1", "operations 1", "operations 0", "missions 1 0", "missions 1 1"}, g_gfLog) &&
                  CsQueue() == "14100000 1000015 a 0; " + reset + "14600000 100000b 14700000 0; " + tail +
                                   "14700000 2000083 14706000 0; 14700000 2000083 14707000 0; 14700000 2000083 14708000 0; "
                                   "14700000 2000083 14709000 0; 14700000 2000083 1470a000 0; ";
            for (int mode : {2, 3}) {
                playMode.mode = mode;
                m.ShowCampaignMissions();
                ok &= CsQueue().rfind(CmText("14100000 1000015 a %x; ", 0x8D2 + mode), 0) == 0;
            }
            g_dbProgress = 1;
            campaign->SetCurrentIndex(0);
            m.ShowCampaignMissions();
            const std::string queue = CsQueue();
            ok &= queue.substr(queue.find("14700000 2000083")) ==
                  "14700000 2000083 14701000 0; 14700000 2000083 14702000 0; 14700000 2000083 14703000 0; "
                  "14700000 2000083 14704000 0; 14700000 2000083 14705000 0; ";
            // A place past 127 (its byte kept).
            g_mmMissionCounts = {200, 3};
            campaign->SetCurrentIndex(1);
            g_dbProgress = 5;
            m.ShowCampaignMissions();
            ok &= CsQueue().find("14700000 2000083 147cb000 0; ") != std::string::npos;
            g_mmMissionCounts = {3, 3};
            WM(r.mxCampaign, ok);
        }
        {
            // The single missions: every campaign's (the first's first not) up to the best progress there.
            bool ok = true;
            playMode.mode = 0;
            m.m_progress = -1;
            g_mxBest = {1, 0};
            m.m_saveName = "x";
            g_gfLog.clear();
            m.ShowSingleMissions();
            ok &= m.m_saveName.IsEmpty() && m.m_progress == 0 && MsLogIs({"best 0", "best 1"}, g_gfLog) &&
                  CsQueue() == "14100000 1000015 a 8cb; " + reset + "14600000 100000b 14800000 0; " + tail +
                                   "14800000 2000083 14801000 0; 14800000 2000083 14803000 0; ";
            playMode.mode = 3;
            m.m_progress = 100;
            g_gfLog.clear();
            m.ShowSingleMissions();
            ok &= m.m_progress == 100 && g_gfLog.empty() &&
                  CsQueue() == "14100000 1000015 a 8d6; " + reset + "14600000 100000b 14800000 0; " + tail +
                                   "14800000 2000083 14801000 0; 14800000 2000083 14802000 0; 14800000 2000083 14803000 0; "
                                   "14800000 2000083 14804000 0; 14800000 2000083 14805000 0; ";
            playMode.mode = 1;
            m.m_progress = -1;
            g_mxBest = {-1, -1};
            m.ShowSingleMissions();
            ok &= CsQueue() == "14100000 1000015 a 0; " + reset + "14600000 100000b 14800000 0; " + tail;
            WM(r.mxSingle, ok);
        }
        S_ControlMessage message;
        const auto send = [&](uint32_t id, uint32_t value) {
            message.id = id;
            message.value = value;
            g_gfLog.clear();
            return m.OnMissionsMessage(&message);
        };
        {
            // The flags; a popup taken; others not; the campaign started (played: its missions; else the
            // roster, the next step); the single missions opened.
            bool ok = true;
            g_mpSettings->sessionType = 2;
            ok &= send(0x4000000 | 0x2000031, 1) && playMode.singleMission == 1 && g_mpSettings->sessionType == 0;
            g_mpSettings->sessionType = 2;
            ok &= send(0x2000070, 3) && playMode.mode == 3 && g_mpSettings->sessionType == 0;
            ok &= send(MENU_MSG_OPEN_POPUP, 4) && !send(0x2000099, 0) && !send(0x2000032, 0) && g_gfLog.empty() &&
                  CsQueue() == "";
            campaign->SetCurrentIndex(1);
            playMode.mode = 1;
            g_dbProgress = 0;
            m.ShowCampaignMissions();
            const std::string campaignQueue = CsQueue();
            m.m_singleMissions = true;
            g_mpSettings->sessionType = 2;
            g_mxHasProgress = true;
            ok &= send(0x2000033, 0) && !m.m_singleMissions && g_mpSettings->sessionType == 0 &&
                  MsLogIs({"reset game", "has progress 1", "progress 1", "operations 1", "operations 0", "missions 1 0"},
                          g_gfLog) &&
                  CsQueue() == campaignQueue + "0 2000001 5 0; ";
            g_mxHasProgress = false;
            const std::vector<S_SoldierProfile *> oldProfiles = g_epProfiles;
            g_epProfiles.clear();
            ok &= send(0x2000033, 0) &&
                  MsLogIs({"reset game", "has progress 1", "roster load tables\\HumanStore.def", "advance"}, g_gfLog) &&
                  CsQueue() == "0 2000008 0 0; ";
            g_esRosterLoads = false;
            ok &= !send(0x2000033, 0) && MsLogIs({"reset game", "has progress 1", "roster load tables\\HumanStore.def"},
                                                 g_gfLog) && CsQueue() == "";
            g_esRosterLoads = true;
            g_epProfiles = {nullptr};
            ok &= send(0x2000033, 0) && MsLogIs({"reset game", "has progress 1", "advance"}, g_gfLog) &&
                  CsQueue() == "0 2000008 0 0; ";
            g_epProfiles = oldProfiles;
            playMode.mode = 0;
            m.m_progress = -1;
            g_mxBest = {2, 0};
            m.ShowSingleMissions();
            const std::string singleQueue = CsQueue();
            ok &= send(0x2000074, 0) && m.m_singleMissions && MsLogIs({"best 0", "best 1"}, g_gfLog) &&
                  CsQueue() == singleQueue + "0 2000001 5 0; ";
            WM(r.mxMessages, ok);
        }
        {
            // The secret key: on the missions screen showing the single missions, all of them shown / not.
            bool ok = true;
            C_MsScreen missions(nullptr, "mxscreen", 5u << 26 | 0x12), other(nullptr, "mxother", 3u << 26);
            g_menuScreens = {&other, &missions};
            m.m_singleMissions = true;
            m.m_progress = 0;
            ok &= send(MENU_MSG_SECRET_KEY, 0) && m.m_progress == 100 && g_gfLog.empty() &&
                  CsQueue().find("14800000 2000083 14805000 0; ") != std::string::npos;
            ok &= send(MENU_MSG_SECRET_KEY, 0) && m.m_progress == 0 && MsLogIs({"best 0", "best 1"}, g_gfLog) &&
                  CsQueue().find("14800000 2000083 14805000 0; ") == std::string::npos;
            m.m_progress = 100;
            ok &= send(MENU_MSG_SECRET_KEY, 0) && m.m_progress == 0;   // (-1, then the best progress)
            CsQueue();
            m.m_progress = 7;
            m.m_singleMissions = false;
            ok &= send(MENU_MSG_SECRET_KEY, 0) && m.m_progress == 7 && CsQueue() == "";
            m.m_singleMissions = true;
            g_menuScreens = {&missions, &other};
            ok &= send(MENU_MSG_SECRET_KEY, 0) && m.m_progress == 7 && CsQueue() == "";
            g_menuScreens.clear();
            ok &= send(MENU_MSG_SECRET_KEY, 0) && m.m_progress == 7 && CsQueue() == "" && g_gfLog.empty();
            WM(r.mxSecret, ok);
        }
        {
            // A mission started: none picked: nothing; a single mission (its campaign by the first's count);
            // the campaign: its first missions begun, the others' saves before them loaded; advanced.
            bool ok = true;
            const auto start = [&](int single, int mission, const std::vector<std::string> &log) {
                playMode.singleMission = single;
                m.m_mission = mission;
                m.m_state = 0;
                g_mpSettings->sessionType = 2;
                return send(0x2000000 | 0x1E, 0) && m.m_state == GAME_STATE_ENDED && g_mpSettings->sessionType == 0 &&
                       MsLogIs(log, g_gfLog) && CsQueue() == "";
            };
            g_mmMissionCounts = {3, 4};
            m.m_mission = -1;
            m.m_state = 0;
            ok &= send(0x200001E, 0) && m.m_state == 0 && g_gfLog.empty();
            ok &= start(1, 4, {"reset game", "single 1 1"}) && campaign->GetCurrentIndex() == 1;
            ok &= start(1, 2, {"reset game", "single 0 2"}) && campaign->GetCurrentIndex() == 0;
            ok &= start(1, 3, {"reset game", "single 1 0"}) && campaign->GetCurrentIndex() == 1;
            campaign->SetCurrentIndex(0);
            ok &= start(0, 0, {"reset game", "advance"}) && start(2, 3, {"reset game", "advance"});
            ok &= start(0, 2, {"reset game", "load save0_1 0", "advance"}) && campaign->GetCurrentIndex() == 0;
            campaign->SetCurrentIndex(1);
            ok &= start(0, 5, {"reset game", "load save1_1 0", "advance"}) &&
                  start(0, 4, {"reset game", "load save1_0 0", "advance"}) &&
                  start(0, 1, {"reset game", "load save1_0 0", "advance"});
            g_mmMissionCounts = {3, 3};
            WM(r.mxStart, ok);
        }
        {
            // An operation's line picked: its index, an operation picked; load on, the description cleared, its
            // name the picture.
            bool ok = true;
            campaign->SetCurrentIndex(1);
            C_MpLine line(0x14700003, 1);
            m.m_operationPicked = false;
            m.m_mission = -1;
            ok &= C_MainMenu::OnOperationPick(&m, nullptr, &line, CONTROL_NOTIFY_CLICK, 0) && m.m_operationPicked &&
                  m.m_mission == 1 && CsQueue() == "14400000 1000002 0 0; 14300000 2000018 0 0; 14200000 100003a 0 0 'op1_1'; ";
            ok &= !C_MainMenu::OnOperationPick(&m, nullptr, nullptr, CONTROL_NOTIFY_CLICK, 0) && CsQueue() == "";
            WM(r.mxOperation, ok);
        }
        g_dbProgress = oldProgress;
        g_mpSettings->sessionType = oldSession;
        g_lpMissions = oldOperations;
        g_gameProfile.m_humanStore = oldStore;
        std::vector<C_CampaignRoot *>().swap(campaign->m_campaigns);
        g_gameProfile.m_playMode = oldPlayMode;
        g_gameProfile.m_campaign = oldCampaign;
    }

    // --- (part 9h-ix) the cinematics screen: its description, hidden, shown, a movie picked, played ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("CINEMA %s\nCINEMA expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "self";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnCinematicsHidden)] = "hidden";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnCinematicsShown)] = "shown";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnMoviePlay)] = "play";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnMoviePick)] = "movie";
        g_ciMovies = {{0x900, "a.bik", true}, {0x901, "b.bik", false}, {0x902, "c.bik", true}};
        {
            // The frames; the controls; the movies' lines.
            bool ok = true;
            S_MrObj *model = MsFrame("cimodel"), *video = MsFrame("civideo"), *text = MsFrame("citext"),
                    *lineT = MsFrame("ciline"), *shortT = MsFrame("cishort");
            As<I3D_frame>(lineT)->m_localMatrix(3) = S_vector(0.25f, -0.5f, 0.75f);
            S_MrObj *shot = MrObj("cishot", g_mrModelVt);
            void *const oldDuplicate = g_mrModelVt[0xA0 / 4], *const oldSetPos = g_msFrameVt[0x08 / 4];
            g_mrModelVt[0xA0 / 4] = reinterpret_cast<void *>(&MrFake<0xA0>);
            g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
            S_MrObj *mesh = MrObj("cishotmesh", g_mrModelVt);
            mesh->vtable[0x18 / 4] = reinterpret_cast<void *>(&MrFake<0x18>);
            *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(shot) + 0x1E0) = mesh;
            g_msChildren[{model, "video"}] = video;
            g_msChildren[{model, "text00"}] = text;
            g_msChildren[{model, "screen_shot"}] = shot;
            g_msChildren[{model, "blong"}] = lineT;
            g_msChildren[{model, "bshort"}] = shortT;
            g_mrMade[FRAME_DUMMY] = {MsFrame("cigroup")};
            g_mrClones.clear();
            for (int i = 0; i < 3; ++i)
                g_mrClones.push_back(As<I3D_frame>(MsFrame(strdup(CmText("cic%d", i).c_str()))));
            S_ScreenDesc d("SINGLE MISSION 2", "single mission 2", 7u << 26);
            g_gfLog.clear();
            ok &= m.DescribeCinematicsScreen(&d, As<I3D_frame>(model));
            ok &= d.messages.size() == 2 && Is(CsMessage(d.messages[0]), "0 1000007 400c000 self hidden") &&
                  Is(CsMessage(d.messages[1]), "0 1000007 400d000 self shown");
            std::string names;
            for (const S_ControlDesc *c : d.controls)
                names += CmText("%x %s %x|", c->type, c->name.c_str(), c->id);
            ok &= Is(names, "13 text_screen 1c100000|4 screen_shot 1c200000|22 video 1c300000|14 bexit01 1c400000|"
                            "14 bexit 1c500000|8 scroll00 1c600000|23 GB_Cinematics 1c700000|"
                            "13 text_list of profiles 1c800000|13 text_h&dversion 1c900000|14 bload lastsave 1ca00000|"
                            "14 bload mission 1cb00000|");
            if (d.controls.size() == 11) {
                ok &= Is(CsTree(d.controls[0]), "13 text_screen 1c100000: 0 1000015 a 83d;") &&
                      Is(CsTree(d.controls[1]), "4 screen_shot 1c200000:;") && Is(CsTree(d.controls[2]), "22 video 1c300000:;") &&
                      Is(CsTree(d.controls[3]), "14 bexit01 1c400000: 0 1000015 d 8d8 0 1000001 0 0 0 1000007 4001000 self play;") &&
                      Is(CsTree(d.controls[4]), "14 bexit 1c500000: 0 1000015 d 8d0 0 6001003 0 0;") &&
                      Is(CsTree(d.controls[5]), "8 scroll00 1c600000: 0 100000b 1c700000 0;") &&
                      Is(CsTree(d.controls[7]), "13 text_list of profiles 1c800000: 0 1000015 b 9f;") &&
                      Is(CsTree(d.controls[8]), "13 text_h&dversion 1c900000: 0 1000015 28 7d6;") &&
                      Is(CsTree(d.controls[9]), "14 bload lastsave 1ca00000: 0 1000014 d 0 'Load Last save' 0 1000004 0 0;") &&
                      Is(CsTree(d.controls[10]), "14 bload mission 1cb00000: 0 1000015 d 8d0 0 1000004 0 0;");
                // The list: its scroll, rows, step, origin (blong's place); a line per movie.
                const S_ControlDesc *list = d.controls[6];
                const auto *origin = list->messages.size() > 3 ? reinterpret_cast<const S_vector *>(list->messages[3].value2) : nullptr;
                ok &= origin && origin->x == 0.25f && origin->y == -0.5f && origin->z == 0.75f;
                std::string tree = CsTree(list);
                if (origin) {
                    const std::string hex = CmText("100006b 0 %x", static_cast<uint32_t>(list->messages[3].value2));
                    const size_t at = tree.find(hex);
                    if (at != std::string::npos)
                        tree.replace(at, hex.size(), "100006b 0 ORIGIN");
                }
                ok &= Is(tree, "23 GB_Cinematics 1c700000: 0 100000b 1c600000 0 0 1000028 11 0 0 100002a 0 3d03126f 0 100006b 0 ORIGIN"
                               " 0 200002c 0 0 { 6 Anim00 1c701000: 0 1000015 c 900 0 1000009 0 0 0 1000007 4001000 self movie;"
                               " 6 Anim01 1c702000: 0 1000015 c 901 0 1000009 1 0 0 1000007 4001000 self movie;"
                               " 6 Anim02 1c703000: 0 1000015 c 902 0 1000009 2 0 0 1000007 4001000 self movie; };");
            } else {
                ok = false;
            }
            const std::string log = Join(g_gfLog);
            ok &= GmInOrder(log, {"civideo on 0", "citext on 0", "cishot a0", "cishotmesh 18", "cigroup 38 cimodel 0",
                                  "cigroup 34 GB_Cinematics", "ciline 24", "ciline on 0", "cishort 24", "cishort on 0",
                                  "cigroup 24", "ciline 38 cigroup 1", "movie 0", "clone tree ciline 0", "cic0 34 Anim00", "movie 1",
                                  "cic1 34 Anim01", "movie 2", "cic2 34 Anim02"}) &&
                  GmInOrder(log, {"cigroup 8 (0 0 0)"});
            if (!ok)
                std::printf("CINEMA log %s\n", log.c_str());
            // No templates: false (nothing described).
            g_msChildren.erase({model, "bshort"});
            g_mrMade[FRAME_DUMMY] = {MsFrame("cigroup2")};
            S_ScreenDesc d2("SINGLE MISSION 2", "single mission 2", 7u << 26);
            ok &= !m.DescribeCinematicsScreen(&d2, As<I3D_frame>(model)) && d2.controls.empty();
            g_msChildren.erase({model, "blong"});
            g_msChildren[{model, "bshort"}] = shortT;
            g_mrMade[FRAME_DUMMY] = {MsFrame("cigroup3")};
            S_ScreenDesc d3("SINGLE MISSION 2", "single mission 2", 7u << 26);
            ok &= !m.DescribeCinematicsScreen(&d3, As<I3D_frame>(model)) && d3.controls.empty();
            g_mrMade.erase(FRAME_DUMMY);
            g_mrModelVt[0xA0 / 4] = oldDuplicate;
            g_msFrameVt[0x08 / 4] = oldSetPos;
            m.m_frames.clear();
            m.m_models.clear();
            WM(r.ciDescribe, ok);
        }
        {
            // Hidden: the movie closed, none picked; shown: the seen movies' lines only.
            bool ok = true;
            m.m_movie = "x.bik";
            ok &= C_MainMenu::OnCinematicsHidden(&m, nullptr, nullptr, 0, 0) && m.m_movie.IsEmpty() &&
                  Is(CsQueue(), "1c300000 20000a0 0 0; ");
            g_gfLog.clear();
            ok &= C_MainMenu::OnCinematicsShown(&m, nullptr, nullptr, 0, 0) && MsLogIs({"seen 0", "seen 1", "seen 2"}, g_gfLog) &&
                  Is(CsQueue(), "1c700000 2000085 0 0; 1c700000 2000083 1c701000 0; 1c700000 2000083 1c703000 0; ");
            g_ciMovies[1].seen = true;
            g_ciMovies[0].seen = false;
            ok &= C_MainMenu::OnCinematicsShown(&m, nullptr, nullptr, 0, 0) &&
                  Is(CsQueue(), "1c700000 2000085 0 0; 1c700000 2000083 1c702000 0; 1c700000 2000083 1c703000 0; ");
            WM(r.ciShown, ok);
        }
        {
            // A movie picked: its file kept, play on, the movie closed, the file played; play: the movie state.
            bool ok = true;
            C_MpLine line(0x1c702000, 1);
            g_gfLog.clear();
            ok &= C_MainMenu::OnMoviePick(&m, nullptr, &line, CONTROL_NOTIFY_CLICK, 0) && m.m_movie == "b.bik" &&
                  MsLogIs({"movie 1"}, g_gfLog) &&
                  Is(CsQueue(), "1c400000 1000002 0 0; 1c300000 20000a0 0 0; 1c300000 20000a2 0 0 'b.bik'; ");
            ok &= !C_MainMenu::OnMoviePick(&m, nullptr, nullptr, CONTROL_NOTIFY_CLICK, 0) && m.m_movie == "b.bik" &&
                  CsQueue() == "";
            m.m_state = 0;
            ok &= !C_MainMenu::OnMoviePlay(&m, nullptr, nullptr, CONTROL_NOTIFY_CLICK, 0) && m.m_state == 0;
            ok &= C_MainMenu::OnMoviePlay(&m, nullptr, &line, CONTROL_NOTIFY_CLICK, 0) && m.m_state == GAME_STATE_PLAY_AVI &&
                  CsQueue() == "";
            m.m_state = 0;
            WM(r.ciPick, ok);
        }
        g_ciMovies.clear();
    }

    // --- (part 9h-x) the password screen: its description; a server's password; the parental lock's code ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("PASSWORD %s\nPASSWORD expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "self";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnPasswordInput)] = "input";
        {
            // The model placed; the controls.
            bool ok = true;
            S_MrObj *model = MsFrame("pwmodel");
            void *const oldSetPos = g_msFrameVt[0x08 / 4];
            g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
            S_ScreenDesc d("PASSWORD", "password", 10u << 26);
            g_gfLog.clear();
            ok &= m.DescribePasswordScreen(&d, As<I3D_frame>(model)) && MsLogIs({"pwmodel 8 (0.5 -0.375 -0.5)"}, g_gfLog);
            g_msFrameVt[0x08 / 4] = oldSetPos;
            ok &= d.messages.empty() && d.controls.size() == 6;
            if (d.controls.size() == 6) {
                const S_ControlDesc *box = d.controls[1];
                ok &= box->messages.size() == 4 &&
                      std::string(reinterpret_cast<const char *>(box->messages[0].value2)) == " \t>-\"";
                std::string tree = CsTree(box);
                const std::string hex = CmText("100009b 0 %x", static_cast<uint32_t>(box->messages[0].value2));
                if (tree.find(hex) != std::string::npos)
                    tree.replace(tree.find(hex), hex.size(), "100009b 0 CHARS");
                ok &= Is(CsTree(d.controls[0]), "13 text 28100000: 0 1000015 c 7d3;") &&
                      Is(tree, "3 binput 28200000: 0 100009b 0 CHARS 0 1000015 e 7d2 0 1000007 4016000 self input"
                               " 0 1000007 4017000 self input;") &&
                      Is(CsTree(d.controls[2]), "14 byes* 28300000: 0 1000015 d 7d1 0 1000007 4001000 self input;") &&
                      Is(CsTree(d.controls[3]), "14 bno* 28400000: 0 1000015 d 7d0 0 1000007 4001000 self input;") &&
                      Is(CsTree(d.controls[4]), "3 binput01 28500000: 0 1000004 0 0;") &&
                      Is(CsTree(d.controls[5]), "3 binput02 28600000: 0 1000004 0 0;");
            }
            WM(r.pwDescribe, ok);
        }
        // The screens: the password's (10) with its box.
        C_MsScreen screen(nullptr, "pwscreen", 10u << 26);
        C_PwBox *box = new C_PwBox(0x28200000, &screen);
        screen.m_children.push_back(box);
        m.m_screens.assign(11, nullptr);
        m.m_screens[10] = &screen;
        C_PwBox yes(0x28300000, nullptr), no(0x28400000, nullptr), other(0x28500000, nullptr);
        const auto call = [&](C_Control *control, uint32_t notification) {
            g_gfLog.clear();
            return C_MainMenu::OnPasswordInput(&m, nullptr, control, notification, 0);
        };
        {
            // A server's password (step 7): typed / yes: kept, connecting; empty: nothing; Escape / no: dropped, closed.
            bool ok = true;
            const C_str oldPassword = g_mpSettings->password;
            const bool oldSet = g_sessionPasswordSet;
            const auto reset = [&] {
                m.m_step = 7;
                m.m_connectState = 5;
                g_sessionPasswordSet = false;
                g_mpSettings->password = "old";
            };
            reset();
            box->text = "secret";
            ok &= call(box, CONTROL_NOTIFY_RETURN) && m.m_step == 0 && m.m_connectState == 0 && g_sessionPasswordSet &&
                  std::string(g_mpSettings->password.c_str()) == "secret" && MsLogIs({"box 0 200002f"}, g_gfLog) &&
                  Is(CsQueue(), "0 2000005 0 0; 0 2000002 d 0; ");
            reset();
            box->text = "";
            ok &= call(box, CONTROL_NOTIFY_RETURN) && m.m_step == 0 && m.m_connectState == 5 && !g_sessionPasswordSet &&
                  std::string(g_mpSettings->password.c_str()) == "old" && Is(CsQueue(), "");
            reset();
            ok &= call(box, CONTROL_NOTIFY_ESCAPE) && m.m_step == 0 && g_gfLog.empty() &&
                  Is(CsQueue(), "0 10000200 1 0; 0 2000005 0 0; ");
            reset();
            ok &= call(box, CONTROL_NOTIFY_CLICK) && m.m_step == 0 && g_gfLog.empty() && Is(CsQueue(), "");
            reset();
            box->text = "pw2";
            ok &= call(&yes, CONTROL_NOTIFY_CLICK) && m.m_step == 0 && m.m_connectState == 0 && g_sessionPasswordSet &&
                  std::string(g_mpSettings->password.c_str()) == "pw2" && MsLogIs({"box 0 200002f"}, g_gfLog) &&
                  Is(CsQueue(), "0 2000005 0 0; 0 2000002 d 0; ");
            reset();
            box->text = "";
            ok &= call(&yes, CONTROL_NOTIFY_CLICK) && m.m_step == 7 && m.m_connectState == 5 && Is(CsQueue(), "");
            reset();
            ok &= call(&no, CONTROL_NOTIFY_CLICK) && m.m_step == 0 && Is(CsQueue(), "0 10000200 1 0; 0 2000005 0 0; ");
            reset();
            ok &= !call(&other, CONTROL_NOTIFY_CLICK) && m.m_step == 7 && !call(nullptr, CONTROL_NOTIFY_CLICK) &&
                  m.m_step == 7 && Is(CsQueue(), "");
            g_mpSettings->password = oldPassword;
            g_sessionPasswordSet = oldSet;
            WM(r.pwServer, ok);
        }
        {
            // The parental lock (another step): typed / yes: set or cleared, the check box, the kept messages posted,
            // closed; Escape / no: the check box, dropped (0 / 1), closed.
            bool ok = true;
            C_GameConfig config;
            C_GameConfig *const oldConfig = g_gameProfile.m_config;
            g_gameProfile.m_config = &config;
            const std::string applied = "0 10000100 0 0; 0 2000005 0 0; ";
            m.m_step = 3;
            g_pwLocked = false;
            box->text = "1234";
            ok &= call(box, CONTROL_NOTIFY_RETURN) && m.m_step == 0 && g_pwLocked &&
                  MsLogIs({"box 0 200002f", "lock set 1234"}, g_gfLog) && Is(CsQueue(), "18a03000 1000076 1 0; " + applied);
            m.m_step = 3;
            box->text = "999";
            ok &= call(&yes, CONTROL_NOTIFY_CLICK) && m.m_step == 0 && g_pwLocked &&
                  MsLogIs({"box 0 200002f", "lock clear 999"}, g_gfLog) && Is(CsQueue(), "18a03000 1000076 1 0; " + applied);
            m.m_step = 3;
            box->text = "1234";
            ok &= call(box, CONTROL_NOTIFY_RETURN) && m.m_step == 0 && !g_pwLocked &&
                  MsLogIs({"box 0 200002f", "lock clear 1234"}, g_gfLog) && Is(CsQueue(), "18a03000 1000076 0 0; " + applied);
            m.m_step = 3;
            ok &= call(box, CONTROL_NOTIFY_ESCAPE) && m.m_step == 0 && g_gfLog.empty() &&
                  Is(CsQueue(), "18a03000 1000076 0 0; 0 10000200 0 0; 0 2000005 0 0; ");
            m.m_step = 3;
            g_pwLocked = true;
            ok &= call(&no, CONTROL_NOTIFY_CLICK) && m.m_step == 0 && g_gfLog.empty() &&
                  Is(CsQueue(), "18a03000 1000076 1 0; 0 10000200 1 0; 0 2000005 0 0; ");
            m.m_step = 3;
            ok &= call(box, CONTROL_NOTIFY_CLICK) && m.m_step == 0 && Is(CsQueue(), "");
            m.m_step = 3;
            ok &= !call(&other, CONTROL_NOTIFY_CLICK) && m.m_step == 3 && !call(nullptr, CONTROL_NOTIFY_CLICK) &&
                  m.m_step == 3 && Is(CsQueue(), "");
            // No box: an empty code.
            m.m_screens[10] = nullptr;
            m.m_screens.resize(10);
            g_pwLocked = false;
            ok &= call(&yes, CONTROL_NOTIFY_CLICK) && MsLogIs({"lock set "}, g_gfLog) &&
                  Is(CsQueue(), "18a03000 1000076 1 0; " + applied);
            m.m_step = 7;
            ok &= call(&yes, CONTROL_NOTIFY_CLICK) && m.m_step == 7 && Is(CsQueue(), "");
            g_gameProfile.m_config = oldConfig;
            g_pwLocked = false;
            WM(r.pwLock, ok);
        }
        m.m_screens.clear();
        m.m_step = 0;
    }

    // --- (part 9h-xi) the filters screen: its description; shown; accepted; the menu's control helpers ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("FILTERS %s\nFILTERS expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "self";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnFiltersShown)] = "shown";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnFiltersAccept)] = "accept";
        {
            bool ok = true;
            S_ScreenDesc d("Server Filters", "server filters", 11u << 26);
            ok &= m.DescribeFiltersScreen(&d, nullptr) && d.messages.size() == 1 &&
                  Is(CsMessage(d.messages[0]), "0 1000007 400d000 self shown");
            std::string names;
            for (const S_ControlDesc *c : d.controls)
                names += CmText("%x %s %x|", c->type, c->name.c_str(), c->id);
            ok &= Is(names, "10 scan style 2c100000|e bshort00 2c200000|24 responding only 2c300000|24 dedicated only 2c400000|"
                            "24 that are not empty 2c500000|24 that are not full 2c600000|24 without password 2c700000|"
                            "24 current version 2c800000|14 baccept 2c900000|14 bback 2ca00000|13 text_screen 2cb00000|"
                            "13 text_h&dversion 2cc00000|13 text_scan style 2cd00000|13 text_show servers 2ce00000|");
            if (d.controls.size() == 14) {
                ok &= Is(CsTree(d.controls[0]), "10 scan style 2c100000: 0 1000067 1b57 0 0 1000067 1b58 0 0 1000067 1b59 0"
                                                " 0 1000067 1b5a 0 0 1000067 1b5b 0;") &&
                      Is(CsTree(d.controls[1]), "e bshort00 2c200000: 0 1000015 d 9fb 0 10000a0 0 270f 0 100006f 1 0;");
                const uint32_t texts[] = {0xA00, 0xA01, 0xA02, 0xA03, 0xA04, 0xA13};
                for (int i = 0; i < 6; ++i)
                    ok &= Is(CsTree(d.controls[2 + i]),
                             CmText("24 %s %x: 0 1000015 d %x;", d.controls[2 + i]->name.c_str(), 0x2c300000 + 0x100000 * i, texts[i]));
                ok &= Is(CsTree(d.controls[8]), "14 baccept 2c900000: 0 1000015 d 9fc 0 6001003 0 0 0 1000007 4001000 self accept;") &&
                      Is(CsTree(d.controls[9]), "14 bback 2ca00000: 0 1000015 d 9fd 0 6001003 0 0;") &&
                      Is(CsTree(d.controls[10]), "13 text_screen 2cb00000: 0 1000015 a 9f7;") &&
                      Is(CsTree(d.controls[11]), "13 text_h&dversion 2cc00000: 0 1000015 28 7d6;") &&
                      Is(CsTree(d.controls[12]), "13 text_scan style 2cd00000: 0 1000015 b 9fa;") &&
                      Is(CsTree(d.controls[13]), "13 text_show servers 2ce00000: 0 1000015 b 9f8;");
            }
            WM(r.flDescribe, ok);
        }
        // The filters screen (11) with its controls; the browser (8) logging.
        C_MsScreen filters(nullptr, "flscreen", 11u << 26), browser(nullptr, "flbrowser", 8u << 26);
        std::vector<C_FlControl *> controls;
        const int kinds[] = {0x10, 0xE, 0x24, 9, 0x27, 0x24, 0x24, 0x24};
        for (int i = 0; i < 8; ++i) {
            controls.push_back(new C_FlControl(0x2c100000 + 0x100000 * static_cast<uint32_t>(i), &filters, kinds[i], 0));
            filters.m_children.push_back(controls.back());
        }
        m.m_screens.assign(12, nullptr);
        m.m_screens[8] = &browser;
        m.m_screens[11] = &filters;
        C_MpMenuSettings settings;
        m.m_mpSettings = &settings;
        S_ServerFilter &filter = settings.m_filter;
        {
            // Shown: the controls set from the filter (no ping filter: 9999).
            bool ok = true;
            filter.gameType = 2;
            filter.maxPing = 0;
            filter.hideUnreachable = true;
            filter.dedicatedOnly = false;
            filter.hideEmpty = true;
            filter.hideFull = false;
            filter.noPassword = true;
            filter.sameVersion = true;
            g_gfLog.clear();
            ok &= C_MainMenu::OnFiltersShown(&m, nullptr, nullptr, 0, 0) &&
                  MsLogIs({"fl 2c100000 100006d 3 0", "fl 2c200000 1000078 d 270f", "fl 2c300000 100006d 1 0",
                           "fl 2c400000 100006d 0 0", "fl 2c500000 100006d 1 0", "fl 2c600000 100006d 0 0",
                           "fl 2c700000 100006d 1 0", "fl 2c800000 100006d 1 0"}, g_gfLog);
            filter.maxPing = 250;
            filter.gameType = -1;
            g_gfLog.clear();
            ok &= C_MainMenu::OnFiltersShown(&m, nullptr, nullptr, 0, 0) && g_gfLog.size() == 8 &&
                  g_gfLog[0] == "fl 2c100000 100006d 0 0" && g_gfLog[1] == "fl 2c200000 1000078 d fa";
            filter.maxPing = 1;
            g_gfLog.clear();
            ok &= C_MainMenu::OnFiltersShown(&m, nullptr, nullptr, 0, 0) && g_gfLog[1] == "fl 2c200000 1000078 d 1";
            WM(r.flShown, ok);
        }
        {
            // Accept: each control's value kept (a change marks the filter changed); saved; the browser's list
            // cleared, its step 6.
            bool ok = true;
            const int values[] = {3, 400, 0, 1, 1, 0, 1, 1};
            for (int i = 0; i < 8; ++i)
                controls[static_cast<size_t>(i)]->value = values[i];
            filter.gameType = 2;
            filter.maxPing = 400;
            filter.hideUnreachable = false;
            filter.dedicatedOnly = true;
            filter.hideEmpty = true;
            filter.hideFull = false;
            filter.noPassword = true;
            filter.sameVersion = true;
            filter.changed = false;
            m.m_step = 0;
            g_files.clear();
            g_gfLog.clear();
            const auto saved = [] {
                for (const auto &f : g_files)
                    if (f.first.find("MPSetings.sav") != std::string::npos)
                        return true;
                return false;
            };
            ok &= C_MainMenu::OnFiltersAccept(&m, nullptr, nullptr, 0, 0) && !filter.changed && m.m_step == 6 && saved() &&
                  MsLogIs({"fl 2c100000 100006c 0 0", "fl 2c200000 1000079 0 0", "fl 2c300000 100006c 0 0",
                           "fl 2c400000 100006c 0 0", "fl 2c500000 100006c 0 0", "fl 2c600000 100006c 0 0",
                           "fl 2c700000 100006c 0 0", "fl 2c800000 100006c 0 0", "flbrowser message 20900000 2000018 0"},
                          g_gfLog);
            // Each one changed alone: kept, changed.
            for (int i = 0; i < 8; ++i) {
                filter.changed = false;
                const int old = controls[static_cast<size_t>(i)]->value;
                controls[static_cast<size_t>(i)]->value = i == 0 ? 1 : i == 1 ? 25 : old ? 0 : 1;
                ok &= C_MainMenu::OnFiltersAccept(&m, nullptr, nullptr, 0, 0) && filter.changed;
                filter.changed = false;
                ok &= C_MainMenu::OnFiltersAccept(&m, nullptr, nullptr, 0, 0) && !filter.changed;
                controls[static_cast<size_t>(i)]->value = old;
                C_MainMenu::OnFiltersAccept(&m, nullptr, nullptr, 0, 0);
            }
            ok &= filter.gameType == 2 && filter.maxPing == 400 && !filter.hideUnreachable && filter.dedicatedOnly &&
                  filter.hideEmpty && !filter.hideFull && filter.noPassword && filter.sameVersion;
            controls[0]->value = 0;
            controls[1]->value = 9;
            controls[2]->value = 5;
            ok &= C_MainMenu::OnFiltersAccept(&m, nullptr, nullptr, 0, 0) && filter.gameType == -1 && filter.maxPing == 9 &&
                  filter.hideUnreachable;
            g_files.clear();
            WM(r.flAccept, ok);
        }
        {
            // The helpers: a control's value by its kind (none / another kind: -1); a copy sent (none: nothing).
            bool ok = true;
            C_FlControl *combo = new C_FlControl(0x2cf00000, &filters, 0x28, 77);
            C_FlControl *other = new C_FlControl(0x2c050000, &filters, 0x14, 5);
            filters.m_children.push_back(combo);
            filters.m_children.push_back(other);
            controls[1]->value = 321;
            controls[3]->value = 1;
            controls[4]->value = 4;
            g_gfLog.clear();
            ok &= m.GetControlValue(0x2c200000) == 321 && m.GetControlValue(0x2c400000) == 1 &&
                  m.GetControlValue(0x2c500000) == 4 && m.GetControlValue(0x2cf00000) == 77 &&
                  m.GetControlValue(0x2c050000) == -1 && m.GetControlValue(0x2c0a0000) == -1 && m.GetControlValue(0) == -1 &&
                  MsLogIs({"fl 2c200000 1000079 0 0", "fl 2c400000 100006c 0 0", "fl 2c500000 100006c 0 0",
                           "fl 2cf00000 200002f 0 0"}, g_gfLog);
            S_ControlMessage message;
            message.id = 0x1234;
            message.value = 7;
            message.value2 = 8;
            g_gfLog.clear();
            m.SendToControl(0x2cf00000, message);
            m.SendToControl(0x2c0a0000, message);
            ok &= MsLogIs({"fl 2cf00000 1234 7 8"}, g_gfLog);
            WM(r.flHelpers, ok);
        }
        m.m_mpSettings = nullptr;
        m.m_screens.clear();
        m.m_step = 0;
    }

    // --- (part 9h-xii) the game info screen: its description; refreshed; the status kept and shown ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("GAMEINFO %s\nGAMEINFO expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "self";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnGameInfoRefresh)] = "refresh";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnGameInfoTick)] = "tick";
        const int *const st = g_gameInfoStyles;
        {
            // The styles made; the controls.
            bool ok = true;
            for (int &style : g_gameInfoStyles)
                style = -1;
            const auto used = [] {
                int n = 0;
                for (int i = 0; i < NumTextStyles(); ++i)
                    n += GetTextStyle(i) != nullptr;
                return n;
            };
            const int before = used();
            S_ScreenDesc d("gameinfo", "gameinfo", 12u << 26);
            ok &= m.DescribeGameInfoScreen(&d, nullptr) && used() == before + 12 && d.messages.size() == 2 &&
                  Is(CsMessage(d.messages[0]), "0 1000007 400d000 self refresh") &&
                  Is(CsMessage(d.messages[1]), "0 1000007 4014000 self tick");
            // Each style as the original's table has it (font, colour, size, width, x, y, align, valign, joins,
            // wraps, flags).
            const struct {
                int font;
                uint32_t color;
                float size, width, x;
                int align, valign;
                uint8_t joins, wraps;
                uint32_t flags;
            } want[12] = {{4, 0xFFFFFFFF, 0.02f, 0.0f, 0.0f, 1, 1, 1, 1, 0x98}, {4, 0, 0.02f, 0.0f, 0.0f, 1, 1, 1, 1, 0x98},
                          {2, 0xFF000000, 0.04f, 0.18f, 0.06f, 1, 1, 1, 0, 0x10},
                          {2, 0xFF000000, 0.04f, 0.05f, 0.65f, 1, 1, 1, 0, 0x10},
                          {2, 0xFF000000, 0.04f, 0.05f, 0.98f, 3, 1, 1, 0, 0x10},
                          {3, 0xFF000000, 0.02f, 0.18f, 0.0f, 1, 1, 1, 0, 0x10},
                          {3, 0xFF000000, 0.02f, 0.05f, 0.65f, 1, 1, 1, 0, 0x10},
                          {3, 0xFF000000, 0.02f, 0.05f, 0.98f, 3, 1, 1, 0, 0x10},
                          {3, 0xFF000000, 0.019f, 0.0f, 0.0f, 1, 1, 1, 1, 0x18},
                          {3, 0xFF000000, 0.019f, 0.0f, 0.65f, 1, 1, 1, 1, 0x10},
                          {2, 0xFF000000, 0.04f, 0.18f, 0.02f, 1, 2, 1, 0, 0x18},
                          {2, 0xFF000000, 0.04f, 0.05f, 0.98f, 3, 2, 1, 0, 0x10}};
            for (int i = 0; i < 12; ++i) {
                const S_TextStyleDesc *style = st[i] >= 0 ? GetTextStyle(st[i]) : nullptr;
                ok &= style && (i == 0 || st[i] != st[i - 1]) && style->font == want[i].font &&
                      style->color == want[i].color && style->size == want[i].size && style->width == want[i].width &&
                      style->x == want[i].x && style->y == -1.0f && style->below == 0.0f && style->above == 0.0f &&
                      style->unknown20 == 0 && style->align == want[i].align && style->valign == want[i].valign &&
                      style->joins == want[i].joins && style->wraps == want[i].wraps && style->flags30 == want[i].flags;
            }
            std::string names;
            for (const S_ControlDesc *c : d.controls)
                names += CmText("%x %s %x|", c->type, c->name.c_str(), c->id);
            ok &= Is(names, "f LB_Players 30100000|13 text_01 30200000|14 brefresh status 30300000|14 bjoin game 30400000|"
                            "14 breturn 30500000|13 text_screen 30600000|13 text_h&dversion 30700000|"
                            "13 text_team status 30800000|13 text_ 30900000|13 allied 30a00000|13 axis 30b00000|");
            if (d.controls.size() == 11) {
                ok &= Is(CsTree(d.controls[0]), "f LB_Players 30100000: 0 2000073 0 0 0 1000005 0 0;") &&
                      Is(CsTree(d.controls[1]), "13 text_01 30200000:;") &&
                      Is(CsTree(d.controls[2]), "14 brefresh status 30300000: 0 1000015 d a05 0 1000007 4001000 self refresh;") &&
                      Is(CsTree(d.controls[3]), "14 bjoin game 30400000: 0 1000015 d a06 0 600109b 0 0 0 600102a 0 0;") &&
                      Is(CsTree(d.controls[4]), "14 breturn 30500000: 0 1000015 d a07 0 6001003 0 0;") &&
                      Is(CsTree(d.controls[5]), "13 text_screen 30600000: 0 1000015 a a08;") &&
                      Is(CsTree(d.controls[6]), "13 text_h&dversion 30700000: 0 1000015 28 7d6;") &&
                      Is(CsTree(d.controls[7]), "13 text_team status 30800000: 0 1000015 b a09;") &&
                      Is(CsTree(d.controls[8]), CmText("13 text_ 30900000: 0 1000017 %x a0a 0 1000017 %x a21 0 1000017 %x a22;",
                                                       st[5], st[6], st[7])) &&
                      Is(CsTree(d.controls[9]), "13 allied 30a00000: 0 1000099 0 0;") &&
                      Is(CsTree(d.controls[10]), "13 axis 30b00000: 0 1000099 0 0;");
            }
            WM(r.giDescribe, ok);
        }
        S_ServerEntry server = {};
        std::strcpy(server.name, "Srv");
        std::strcpy(server.map, "Maps");
        server.address = 0x0A000102;
        server.ping = 42;
        server.players = 3;
        server.maxPlayers = 8;
        server.dedicated = true;
        server.internet = false;
        server.password = true;
        server.gameType = 1;
        const auto text = [&](uint32_t target, int style, const char *t) {
            return CmText("%x 1000016 %x 0 '%s'; ", target, st[style], t);
        };
        const auto id = [&](uint32_t target, int style, uint32_t t) { return CmText("%x 1000017 %x %x; ", target, st[style], t); };
        {
            // Refreshed: none picked: false; the boxes emptied, reading, the axis' score shown or hidden, asked.
            bool ok = true;
            m.m_server = nullptr;
            g_gfLog.clear();
            ok &= !C_MainMenu::OnGameInfoRefresh(&m, nullptr, nullptr, 0, 0) && g_gfLog.empty() && Is(CsQueue(), "");
            m.m_server = &server;
            g_gameInfoStatus = reinterpret_cast<S_ServerStatus *>(&server);
            g_giCallback = nullptr;
            const std::string head = "30100000 2000018 0 0; 30200000 1000077 0 0; " +
                                     text(0x30200000, 8, "Reading Data from server ...");
            ok &= C_MainMenu::OnGameInfoRefresh(&m, nullptr, nullptr, 0, 0) && !g_gameInfoStatus &&
                  g_giCallback == &C_MainMenu::OnGameInfoStatus && MsLogIs({"query Srv"}, g_gfLog) &&
                  Is(CsQueue(), head + "30b00000 1000003 0 0; ");
            server.gameType = 3;
            ok &= C_MainMenu::OnGameInfoRefresh(&m, nullptr, nullptr, 0, 0) && Is(CsQueue(), head + "30b00000 1000004 0 0; ");
            server.gameType = 1;
            WM(r.giRefresh, ok);
        }
        S_StatusPlayer players[5] = {};
        const struct {
            int team;
            const char *name;
            int kills, points;
        } given[5] = {{0, "Ann", 5, 7}, {1, "Dee", 1, 6}, {0, "", 9, 9}, {2, "Bob", 0, 12}, {5, "C", 2, 3}};
        for (int i = 0; i < 5; ++i) {
            players[i].team = given[i].team;
            std::strcpy(players[i].name, given[i].name);
            players[i].kills = given[i].kills;
            players[i].points = given[i].points;
        }
        S_ServerStatus status = {};
        status.playersBegin = players;
        status.playersEnd = players + 5;
        const int numbers[] = {10, 11, 12, 13, 14, 15};
        status.fragLimit = numbers[0];
        status.roundLimit = numbers[1];
        status.roundCount = numbers[2];
        status.preparationTime = numbers[3];
        status.respawnTime = numbers[4];
        status.spawnProtectionTime = numbers[5];
        status.allowRespawn = 1;
        status.friendlyFire = 0;
        status.autoTeamBalance = 1;
        status.thirdPersonView = 0;
        status.allowVehicles = 1;
        status.allowCrosshair = 0;
        status.inverseDamage = 33;
        status.difficulty = 2;
        status.lives = 4;
        status.teamLives = 1;
        status.axisScore = 70;
        status.alliedScore = 80;
        C_MsScreen info(nullptr, "giscreen", 12u << 26 | 0x5), other(nullptr, "giother", 8u << 26);
        C_MenuState *const oldState = g_menuState;
        const bool oldSet = g_sessionPasswordSet;
        g_sessionPasswordSet = false;
        const auto shown = [&] {
            std::string q = "30100000 2000018 0 0; ";
            q += text(0x30100000, 2, "C") + text(0x30100000, 3, "2") + text(0x30100000, 4, "3");
            q += text(0x30100000, 1, "=") + text(0x30100000, 2, "Bob") + text(0x30100000, 3, "0") + text(0x30100000, 4, "12");
            q += text(0x30100000, 0, "=") + text(0x30100000, 2, "Dee") + text(0x30100000, 3, "1") + text(0x30100000, 4, "6");
            q += text(0x30100000, 0, "!") + text(0x30100000, 2, "Ann") + text(0x30100000, 3, "5") + text(0x30100000, 4, "7");
            q += "30200000 1000077 0 0; 30a00000 2000018 0 0; 30b00000 2000018 0 0; ";
            q += id(0x30a00000, 10, 0xA3C) + text(0x30a00000, 11, "80");
            if (server.gameType != 3)
                q += id(0x30b00000, 10, 0xA3D) + text(0x30b00000, 11, "70");
            const uint32_t T = 0x30200000;
            q += id(T, 8, 0xA0B) + id(T, 8, 0x7D2) + id(T, 8, 0xA0C) + text(T, 9, "Srv") + id(T, 8, 0xA0D) + text(T, 9, "10.0.1.2");
            q += id(T, 8, 0xA0E) + text(T, 9, "42") + id(T, 8, 0xA0F) + text(T, 9, "3 / 8");
            q += id(T, 8, 0xA10) + id(T, 9, 0x7FD) + id(T, 8, 0xA12) + id(T, 9, 0x7FE) + id(T, 8, 0xA11) + id(T, 9, 0x7FD);
            q += id(T, 8, 0x7D2) + id(T, 8, 0xA14) + id(T, 8, 0x7D2) + id(T, 8, 0xA15) +
                 id(T, 9, static_cast<uint32_t>(7000 + server.gameType));
            if (server.gameType == 3)
                q += id(T, 8, 0x501E) + id(T, 9, 0x5016) + id(T, 8, 0x501F) + id(T, 9, 0x7FD) + id(T, 8, 0x5020) + text(T, 9, "4");
            q += id(T, 8, 0xA16) + text(T, 9, "Maps");
            q += id(T, 8, 0xA17) + text(T, 9, "10") + id(T, 8, 0xA18) + text(T, 9, "11") + id(T, 8, 0xA19) + text(T, 9, "12");
            q += id(T, 8, 0xA1A) + text(T, 9, "13") + id(T, 8, 0xA1B) + text(T, 9, "14");
            q += id(T, 8, 0xA1C) + id(T, 9, 0x7FD) + id(T, 8, 0xA1D) + id(T, 9, 0x7FE) + id(T, 8, 0x5023) + text(T, 9, "33");
            q += id(T, 8, 0xA1E) + id(T, 9, 0x7FD) + id(T, 8, 0xA1F) + id(T, 9, 0x7FE) + id(T, 8, 0xA20) + text(T, 9, "15");
            q += id(T, 8, 0x5021) + id(T, 9, 0x7FE) + id(T, 8, 0x5022) + id(T, 9, 0x7FD);
            return q;
        };
        {
            // Shown: only on the main menu's game info screen with a server picked and no password asked.
            bool ok = true;
            g_menuState = &m;
            g_menuScreens = {&other, &info};
            C_MainMenu::ShowGameInfo(&status);
            ok &= Is(CsQueue(), shown());
            server.gameType = 3;
            C_MainMenu::ShowGameInfo(&status);
            ok &= Is(CsQueue(), shown());
            server.gameType = 1;
            // A name the font cannot show.
            std::strcpy(server.name, "S\xC3\xA9");
            C_MainMenu::ShowGameInfo(&status);
            const std::string q = CsQueue();
            ok &= q.find(id(0x30200000, 9, 0x7E9)) != std::string::npos && q.find("S\xC3\xA9") == std::string::npos;
            g_gameProfile.m_unicodeFont = true;
            C_MainMenu::ShowGameInfo(&status);
            ok &= CsQueue().find(text(0x30200000, 9, "S\xC3\xA9")) != std::string::npos;
            g_gameProfile.m_unicodeFont = false;
            std::strcpy(server.name, "Srv");
            // Not: no server; a password asked; another screen on top; no screens; another state; no state.
            m.m_server = nullptr;
            C_MainMenu::ShowGameInfo(&status);
            m.m_server = &server;
            g_sessionPasswordSet = true;
            C_MainMenu::ShowGameInfo(&status);
            g_sessionPasswordSet = false;
            g_menuScreens = {&info, &other};
            C_MainMenu::ShowGameInfo(&status);
            g_menuScreens.clear();
            C_MainMenu::ShowGameInfo(&status);
            g_menuScreens = {&other, &info};
            C_GameState plain;
            g_menuState = reinterpret_cast<C_MenuState *>(&plain);
            C_MainMenu::ShowGameInfo(&status);
            g_menuState = nullptr;
            C_MainMenu::ShowGameInfo(&status);
            ok &= Is(CsQueue(), "");
            // No players: their box only emptied.
            g_menuState = &m;
            status.playersBegin = status.playersEnd = nullptr;
            C_MainMenu::ShowGameInfo(&status);
            ok &= CsQueue().rfind("30100000 2000018 0 0; 30200000 1000077 0 0; ", 0) == 0;
            status.playersBegin = players;
            status.playersEnd = players + 5;
            WM(r.giShow, ok);
        }
        {
            // The status kept (the main menu, no password asked) and shown at the tick, then dropped.
            bool ok = true;
            InitializeCriticalSection(&m.m_foundLock);
            g_gameInfoStatus = nullptr;
            C_MainMenu::OnGameInfoStatus(&status);
            ok &= g_gameInfoStatus == &status;
            ok &= C_MainMenu::OnGameInfoTick(&m, nullptr, nullptr, 0, 0) && !g_gameInfoStatus && Is(CsQueue(), shown());
            ok &= C_MainMenu::OnGameInfoTick(&m, nullptr, nullptr, 0, 0) && Is(CsQueue(), "");
            g_sessionPasswordSet = true;
            C_MainMenu::OnGameInfoStatus(&status);
            ok &= !g_gameInfoStatus;
            g_sessionPasswordSet = false;
            C_GameState plain;
            g_menuState = reinterpret_cast<C_MenuState *>(&plain);
            C_MainMenu::OnGameInfoStatus(&status);
            ok &= !g_gameInfoStatus;
            g_menuState = nullptr;
            C_MainMenu::OnGameInfoStatus(&status);
            ok &= !g_gameInfoStatus;
            DeleteCriticalSection(&m.m_foundLock);
            WM(r.giStatus, ok);
        }
        g_menuScreens.clear();
        g_menuState = oldState;
        g_sessionPasswordSet = oldSet;
        g_gameInfoStatus = nullptr;
        m.m_server = nullptr;
        for (int &style : g_gameInfoStyles) {
            ReleaseTextStyle(style);
            style = -1;
        }
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
        // (The missions screen's two groups (no templates: not described further); the options screen's group frame:
        // the real handler finds no buttons.)
        g_mrMade[FRAME_DUMMY] = {MsFrame("mmgc"), MsFrame("mmgs"), MsFrame("mmgro"), MsFrame("mmgci")};
        g_cacheLoads.clear();
        g_menuScreens.clear();
        g_gfLog.clear();
        ok &= m.BuildScreens() &&
              g_cacheLoads == std::vector<std::string>({"main_menu", "change_profile", "singleplayer", "confirm",
                                                        "single mission 2", "options", "single mission 2",
                                                        "multiplayer menu", "create server", "password", "server filters",
                                                        "gameinfo", "confirm", "password"});
        const std::string log = Join(g_gfLog);
        ok &= GmInOrder(log, {"scan", "describe browser 20000000", "describe create 24000000",
                              "describe connect 34000000",
                              "describe addserver 38000000"}) &&
              log.find("describe ", log.find("describe addserver") + 1) == std::string::npos;
        ok &= m.m_screens.size() == 15 && S_CsPeek::Callbacks(m.m_screens[1]) == 1 && S_CsPeek::Callbacks(m.m_screens[2]) == 1 &&
              S_CsPeek::Callbacks(m.m_screens[3]) == 1 && S_CsPeek::Callbacks(m.m_screens[4]) == 0 &&
              m.m_screens[4]->GetScreenFrame() == As<I3D_frame>(models[3]) &&
              !g_menuScreens.empty() && g_menuScreens.back() == m.m_screens[1];
        // The quit question's model moved.
        // (And the password screen's, part 9h-x.)
        ok &= log.find("mmm04 8 (0.5 -0.375 -0.5)") != std::string::npos && log.find("mmm10 8 (0.5 -0.375 -0.5)") != std::string::npos &&
              log.find(" 8 (0.5", log.find(" 8 (0.5", log.find(" 8 (0.5") + 1) + 1) == std::string::npos &&
              log.find("mmgro 34 GROptions") != std::string::npos && log.find("mmgci 34 GB_Cinematics") != std::string::npos;
        // (The game info screen's styles of this build given back: the next build makes its own.)
        for (int &style : g_gameInfoStyles) {
            ReleaseTextStyle(style);
            style = -1;
        }
        // A model not found: its screen skipped.
        for (size_t i = 1; i < m.m_screens.size(); ++i)
            delete m.m_screens[i];
        m.m_screens = {&box};
        m.m_models.clear();
        g_mrMade[FRAME_MODEL] = std::deque<S_MrObj *>(models.begin(), models.end());
        g_mrMade[FRAME_DUMMY] = {MsFrame("mmgc2"), MsFrame("mmgs2"), MsFrame("mmgro2"), MsFrame("mmgci2")};
        g_cacheFailName = "change_profile";
        g_menuScreens.clear();
        g_gfLog.clear();
        ok &= m.BuildScreens() && m.m_screens.size() == 14 && std::find(g_gfLog.begin(), g_gfLog.end(), "scan") == g_gfLog.end();
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
    std::printf("mainmenu: mmStart %d mmStop %d mmUpdate %d mmDrain %d mmMessage %d mmEscape %d mmSelect %d mmBuild %d mmDescribe %d mmMainShown %d pfDescribe %d pfRefresh %d pfShown %d pfList %d "
                "pfDifficulty %d pfMessage %d pfButtons %d pfEscape %d pfCreate %d pfRename %d "
                "spShown %d spHover %d spClick %d mpLists %d mpDescribe %d mpSaveSlot %d mpPick %d mxCampaign %d mxSingle %d"
                " mxMessages %d mxSecret %d mxStart %d mxOperation %d ciDescribe %d ciShown %d ciPick %d"
                " pwDescribe %d pwServer %d pwLock %d flDescribe %d flShown %d"
                " flAccept %d flHelpers %d giDescribe %d giRefresh %d giShow %d giStatus %d\n",
                r.mmStart, r.mmStop, r.mmUpdate, r.mmDrain, r.mmMessage, r.mmEscape, r.mmSelect, r.mmBuild, r.mmDescribe,
                r.mmMainShown, r.pfDescribe, r.pfRefresh, r.pfShown, r.pfList, r.pfDifficulty, r.pfMessage,
                r.pfButtons, r.pfEscape, r.pfCreate, r.pfRename, r.spShown, r.spHover, r.spClick, r.mpLists, r.mpDescribe,
                r.mpSaveSlot, r.mpPick, r.mxCampaign, r.mxSingle, r.mxMessages, r.mxSecret, r.mxStart, r.mxOperation,
                r.ciDescribe, r.ciShown, r.ciPick, r.pwDescribe, r.pwServer, r.pwLock,
                r.flDescribe, r.flShown, r.flAccept, r.flHelpers,
                r.giDescribe, r.giRefresh, r.giShow, r.giStatus);
}
