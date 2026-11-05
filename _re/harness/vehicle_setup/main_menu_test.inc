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
// (Part 9h-xxi: the network handler real; the searches and the firewall logged.)
void C_NetGame::SearchServers(void(__fastcall *callback)(S_ServerEntry *))
{
    GfLog("search %d", callback == &C_MainMenu::OnServerFound);
}
void C_NetGame::PingServers(void(__fastcall *callback)(S_ServerEntry *), std::vector<S_ServerEntry *> &servers)
{
    GfLog("ping servers %d %d", callback == &C_MainMenu::OnServerFound, static_cast<int>(servers.size()));
}
bool g_nmFirewall = false;
bool C_NetGame::FirewallBlocks(uint16_t port)
{
    GfLog("firewall %d", port);
    return g_nmFirewall;
}
// (Part 9h-xviii: the connect screen's dedicated server: logged, its result.)
bool g_cnDedicated = true;
bool C_NetGame::HostDedicated(bool unknown)
{
    GfLog("net dedicated %d", unknown);
    return g_cnDedicated;
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
// (Part 9h-xix: a ping: logged, its callback kept; a control answering every message with its values, logged. Part
// 9h-xx: the search's poll logged.)
void C_NetGame::PollSearch() { GfLog("net poll"); }
void(__fastcall *g_bwPing)(S_ServerEntry *);
void C_NetGame::PingServer(void(__fastcall *callback)(S_ServerEntry *), S_ServerEntry *server)
{
    GfLog("ping %s", server->name);
    g_bwPing = callback;
}
struct C_BwControl : C_Control {
    C_BwControl(uint32_t id, C_Control *parent, uint32_t v) : C_Control(nullptr, id, parent), value(v) {}
    uint32_t value;
    uintptr_t value2 = 0;
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("bw %x %x", m_id, m->id);
        m->value = value;
        m->value2 = value2;
        return true;
    }
};
// (Part 9h-xiii: the create server screen's callbacks (a later sub-part's); the codecs' texts.)
// (Part 9h-xvi: a map line: its id, index, parent, highlight frame.)
struct C_CsLine : C_Control {
    C_CsLine(uint32_t id, int index, C_Control *parent, I3D_frame *highlight)
        : C_Control(nullptr, id, parent), highlight(highlight)
    {
        m_index = index;
    }
    I3D_frame *highlight;
    I3D_frame *GetHighlightFrame() const override { return highlight; }
};
int GetVoiceCodecTextId(int codec) { return 0xB40 + codec; }
// (Part 9i-ii: the session's reset real.)
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
         giDescribe = true, giRefresh = true, giShow = true, giStatus = true,
         csDescribe = true, csSetup = true, csShown = true, csStart = true, csPicked = true, csHost = true,
         csRules = true, csMapList = true, csOptions = true, csStyle = true, csOption = true,
         csMoves = true, csDouble = true, csDragStart = true, csDragMove = true, csDrop = true,
         hsSession = true, cnDescribe = true, cnHidden = true, cnTick = true, cnMessage = true,
         bwDescribe = true, bwList = true, bwShown = true, bwSort = true, bwPicked = true, bwJoin = true, bwCount = true,
         bxLine = true, bxFound = true, bxList = true, bxTick = true, bxRestore = true, bxRefresh = true,
         bxFavorite = true, bxFavorites = true,
         nmLan = true, nmInternet = true, nmLeave = true, nmStart = true, nmJoin = true, nmSearch = true, nmEscape = true,
         slFile = true, slOdd = true, asDescribe = true, asButton = true;
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
        // (The profile screen's, the missions', the connect and the network handlers, real, take none of these.)
        send(0x2000050);
        ok &= g_gfLog.empty() && CsQueue() == "";
        const int state = m.m_state;
        // (Part 9h-xxi: the network handler taking one.)
        send(0x2000069);
        ok &= g_gfLog.empty() && m.m_state == GAME_STATE_NEXT_MAP && CsQueue() == "";
        m.m_state = state;
        // (The missions' handler taking one: the rest not asked.)
        send(MENU_MSG_OPEN_POPUP);
        ok &= g_gfLog.empty() && CsQueue() == "";
        // The movie; the saved game (none: the load command alone).
        send(0x4000000 | MAIN_MSG_PLAY_MOVIE);
        ok &= m.m_state == GAME_STATE_PLAY_AVI;
        m.m_state = state;
        C_GameApp *app = GetGameApp();
        app->m_pendingCommand = 0;
        send(MAIN_MSG_LOAD_GAME);
        ok &= app->m_pendingCommand == APP_COMMAND_LOAD_FROM_MENU && g_gfLog.empty();
        app->m_pendingCommand = 0;
        m.m_saveName = "save7.sav";
        send(MAIN_MSG_LOAD_GAME);
        ok &= app->m_pendingCommand == 0 && g_gfLog.size() == 1 && g_gfLog[0] == "request load prof\\save7.sav";
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
        ok &= escape(0).find("20000c4") != std::string::npos && g_gfLog.empty();
        // (Part 9h-xviii: Escape on the connect screen cancels it.)
        ok &= escape(13) == "0 2000005 0 0; 0 2000028 0 0; 24900000 1000001 0 0; ";
        for (int top : {1, 3, 5, 6, 7, 9, 11, 12, 15})
            ok &= escape(top) == "0 2000003 0 0; ";
        // (Part 9h-xxi: Escape on the browser: the searches stopped, its list emptied, back.)
        ok &= escape(8) == "0 2000028 0 0; 0 200009b 0 0; 20900000 2000018 0 0; 0 2000003 0 0; ";
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
        status.players.assign(players, players + 5);
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
            status.players.clear();
            C_MainMenu::ShowGameInfo(&status);
            ok &= CsQueue().rfind("30100000 2000018 0 0; 30200000 1000077 0 0; ", 0) == 0;
            status.players.assign(players, players + 5);
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

    // --- (part 9h-xiii) the create server screen's description: the maps panel, the options panel ---
    {
        C_MainMenu m;
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "self";
        const char *const callbacks[][2] = {
            {"OnCreateServerShown", "shown"}, {"OnCreateServerHidden", "hidden"}, {"OnHostNameChanged", "host"},
            {"OnServerStyleChanged", "style"}, {"OnServerOptionChanged", "option"}, {"OnMapPresetPicked", "preset"},
            {"OnMapLineDoubleClick", "double"}, {"OnMapLineDragStart", "drag"}, {"OnMapLineDragMove", "move"},
            {"OnMapLineDrop", "drop"}};
        const ControlCallback functions[] = {
            &C_MainMenu::OnCreateServerShown, &C_MainMenu::OnCreateServerHidden, &C_MainMenu::OnHostNameChanged,
            &C_MainMenu::OnServerStyleChanged, &C_MainMenu::OnServerOptionChanged, &C_MainMenu::OnMapPresetPicked,
            &C_MainMenu::OnMapLineDoubleClick, &C_MainMenu::OnMapLineDragStart, &C_MainMenu::OnMapLineDragMove,
            &C_MainMenu::OnMapLineDrop};
        for (int i = 0; i < 10; ++i)
            g_csNames[reinterpret_cast<uintptr_t>(functions[i])] = callbacks[i][1];
        C_MpMenuSettings settings;
        m.m_mpSettings = &settings;
        settings.m_setup.serverName = "Host";
        for (auto &group : settings.m_setup.maps) {
            for (C_MpMap *map : group.maps)
                delete map;
            group.maps.clear();
        }
        settings.m_setup.maps[0].maps = {new C_MpMap(C_str("dm1"), C_str("d1")), new C_MpMap(C_str("dm2"), C_str("d2"))};
        settings.m_setup.maps[3].maps = {new C_MpMap(C_str("coop1"), C_str("c1"))};
        S_MrObj *model = MsFrame("csmodel");
        const char *const children[][2] = {{"bmap_A", "csmapa"},     {"bmap_B", "csmapb"},     {"scroll00", "cssc0"},
                                           {"scroll13", "cssc13"},   {"yes_no_tmpl", "csyn"},  {"bshort00", "csnum"},
                                           {"binput", "csinput"},    {"combo_tmpl", "cscombo"}};
        std::map<std::string, S_MrObj *> frames;
        for (const auto &child : children) {
            frames[child[0]] = MsFrame(child[1]);
            g_msChildren[{model, child[0]}] = frames[child[0]];
        }
        As<I3D_frame>(frames["bmap_A"])->m_localMatrix(3) = S_vector(0.5f, 0.25f, 0.0f);
        As<I3D_frame>(frames["bmap_B"])->m_localMatrix(3) = S_vector(0.75f, 0.25f, 0.0f);
        As<I3D_frame>(frames["bshort00"])->m_localMatrix(3) = S_vector(0.125f, -0.5f, 0.0f);
        void *const oldSetPos = g_msFrameVt[0x08 / 4];
        g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("CREATE %s\nCREATE expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        // A tree with its origins and the password's characters shown as words (their values checked apart).
        const auto masked = [](std::string tree) {
            for (const char *key : {"100006b 0 ", "100009b 0 "}) {
                for (size_t at = tree.find(key); at != std::string::npos; at = tree.find(key, at + 1)) {
                    const size_t from = at + std::strlen(key), to = tree.find(' ', from);
                    tree.replace(from, to - from, key[5] == '6' ? "ORIGIN" : "CHARS");
                }
            }
            return tree;
        };
        const auto origin = [](const S_ControlDesc *list, float x, float y) {
            for (const S_ControlMessage &message : list->messages)
                if (message.id == CONTROL_MSG_SET_ORIGIN) {
                    const auto *v = reinterpret_cast<const S_vector *>(message.value2);
                    return v && v->x == x && v->y == y && v->z == 0.0f;
                }
            return false;
        };
        // The dummies made: the panel, the groups (named: their lists take the names), the options' group.
        const auto dummies = [] {
            const char *const names[] = {"cspanel",        "gr_mp_deathmatch", "gr_mp_zonegame", "gr_mp_hiddenmaps",
                                         "gr_mp_coopmaps", "gr_mp_sel_list",   "gr_setings"};
            g_mrMade[FRAME_DUMMY].clear();
            for (const char *name : names) {
                S_MrObj *frame = MsFrame(name);
                As<I3D_frame>(frame)->m_name = name;
                As<I3D_frame>(frame)->m_nameOffset = 0;
                g_mrMade[FRAME_DUMMY].push_back(frame);
            }
            g_mrClones.clear();
            for (int i = 0; i < 80; ++i)
                g_mrClones.push_back(As<I3D_frame>(MsFrame(strdup(CmText("csc%d", i).c_str()))));
        };
        const std::string cb4 = "0 1000007 4013000 self double 0 1000007 400a000 self drag 0 1000007 4008000 self move"
                                " 0 1000007 4009000 self drop";
        const auto mapList = [&](const char *name, uint32_t id, const std::vector<const char *> &maps) {
            std::string t = CmText("23 %s %x: 0 100000b 24301000 0 0 1000028 b 0 0 100002a 0 3d09a027 0 1000004 0 0 0 100006b 0 "
                                   "ORIGIN 0 200002c 0 0", name, id);
            if (!maps.empty()) {
                t += " {";
                for (size_t i = 0; i < maps.size(); ++i)
                    t += CmText(" 14 MPMap%02d %x: %s 0 1000014 d 0 '%s' 0 1000009 %x 0;", static_cast<int>(i),
                                id + 0x10 * static_cast<uint32_t>(i + 1), cb4.c_str(), maps[i], static_cast<uint32_t>(i));
                t += " }";
            }
            return t + ";";
        };
        std::string picked = "26 gr_mp_sel_list 24307000: 0 100000b 24306000 0 0 1000028 b 0 0 100002a 0 3d09a027 0 1000003 0 0 0"
                             " 100006b 0 ORIGIN 0 2000085 0 0 0 200002c 0 0 {";
        for (int i = 0; i < 50; ++i)
            picked += CmText(" 14 MPMap%02d %x: %s;", i, 0x24307010 + 0x10 * i, cb4.c_str());
        picked += " };";
        const std::string panel = "5 MP_MAPS_PANEL 24300000: { 8 scroll00 24301000: 0 100000b 24302000 0; " +
                                  mapList("gr_mp_deathmatch", 0x24302000, {"dm1", "dm2"}) + " " +
                                  mapList("gr_mp_zonegame", 0x24303000, {}) + " " + mapList("gr_mp_hiddenmaps", 0x24304000, {}) +
                                  " " + mapList("gr_mp_coopmaps", 0x24305000, {"coop1"}) +
                                  " 8 scroll13 24306000: 0 100000b 24307000 0; " + picked + " };";
        const auto number = [](const char *name, uint32_t id, uint32_t text, uint32_t value, uint32_t min, uint32_t max,
                               uint32_t length) {
            return CmText(" e %s %x: 0 1000015 d %x 0 100006f 1 0 0 1000078 d %x 0 10000a0 %x %x 0 100006e %x 0 0 1000007 400b000"
                          " self option;", name, id, text, value, min, max, length);
        };
        const auto yesNo = [](const char *name, uint32_t id, uint32_t text) {
            return CmText(" 27 %s %x: 0 1000015 d %x 0 1000099 0 0 0 2000073 0 0 0 1000007 4010000 self option;", name, id, text);
        };
        const std::string options =
            "23 gr_setings 24700000: 0 100000b 24600000 0 0 1000028 7 0 0 100002a 0 3d23d70a 0 100006b 0 ORIGIN 0 200002c 0 0 {"
            " 28 coop difficulty 24701000: 0 1000015 d 4ef2 0 1000081 c 4efe 0 1000017 13 4efc 0 1000017 13 4efd 0 1000017 13"
            " 4efe 0 1000017 13 4eff 0 1000007 400e000 self option;" +
            number("player num", 0x24702000, 0x93C, 0x20, 2, 0x20, 3) + yesNo("Team Respawn", 0x24703000, 0x4EF3) +
            number("Respawn Num", 0x24704000, 0x4EF5, 0x22B, 0xFFFFFFFF, 0x63, 3) +
            number("Frag Limit", 0x24705000, 0x93D, 0x1F4, 0, 0x3E7, 4) + number("Round Limit", 0x24706000, 0x93E, 0xA, 1, 0x63, 3) +
            number("Round Count", 0x24707000, 0x93F, 5, 1, 0x3E7, 4) + number("WarmUp Time", 0x24708000, 0x940, 0xA, 0, 0x63, 3) +
            yesNo("Allow respawn", 0x24709000, 0x942) + number("Respawn Time", 0x2470a000, 0x941, 0x14, 5, 0x63, 3) +
            number("Spawn Protection", 0x2470b000, 0x948, 5, 0, 0xF, 3) +
            number("Self Damage", 0x2470c000, 0x949, 0x64, 0, 0x3E7, 4) + yesNo("Friendly Fire", 0x2470d000, 0x943) +
            yesNo("Auto Team Balence", 0x2470e000, 0x944) + yesNo("3person view", 0x2470f000, 0x945) +
            yesNo("yn_crosshair", 0x24710000, 0x4EF4) +
            " e bpassword 24711000: 0 100009b 0 CHARS 0 1000015 d 946 0 1000007 400b000 self option;"
            " e bport 24712000: 0 1000015 d 94a 0 100006f 1 0 0 10000a0 400 fffa 0 100006e 5 0 0 1000007 400b000 self option; };";
        // The screen's own controls after the panels, from `first` (their ids by their places).
        const auto tail = [](uint32_t first) {
            const char *const trees[] = {
                "10 voice chat %x: 0 1000007 4010000 self option 0 1000067 b40 0 0 1000067 b41 0 0 1000067 b42 0 0 1000067 b43 0"
                " 0 1000067 b44 0 0 1000067 b45 0 0 1000067 b46 0 0 1000067 b47 0;",
                "14 bstart %x: 0 1000015 d 935 0 1000001 0 0 0 6001029 0 0;", "14 bback %x: 0 1000015 d 936 0 6001003 0 0;",
                "13 text_screen %x: 0 1000015 a 92f;", "13 text_hostname and style %x: 0 1000015 b 930;",
                "13 text_available maps checklist %x: 0 1000015 b 932;", "13 text_server options %x: 0 1000015 b 933;",
                "13 text_voice chat %x: 0 1000015 b 947;", "13 text_h&dversion %x: 0 1000015 28 7d6;",
                "13 text_server type %x: 0 1000015 b 94c;",
                "11 maplist preset %x: 0 1000007 400e000 self preset 0 1000081 c 1f4 0 1000017 13 4ee8 0 1000017 13 4ee9"
                " 0 1000017 13 4eea 0 1000017 13 4eeb 0 1000017 13 4eec;",
                "13 text_maplist preset %x: 0 1000015 b 4eed;", "13 text_maplist %x: 0 1000015 b 4eee;"};
            std::vector<std::string> out;
            for (size_t i = 0; i < 13; ++i)
                out.push_back(CmText(trees[i], first + 0x100000 * static_cast<uint32_t>(i)));
            return out;
        };
        const std::string head0 = "3 bhostname 24100000: 0 1000014 d 0 'Host' 0 1000007 400b000 self host;";
        const std::string head1 = "10 style 24200000: 0 1000007 4010000 self style 0 1000067 1b58 0 0 1000067 1b59 0 0 1000067 1b5a 0"
                                  " 0 1000067 1b5b 0;";
        const auto checks = [](uint32_t first) {
            return std::vector<std::string>{
                CmText("24 internet server %x: 0 1000015 d 94d 0 100006d 0 0 0 1000007 4010000 self option;", first),
                CmText("24 dedicated server %x: 0 1000015 d 94e 0 100006d 0 0 0 1000007 4010000 self option;", first + 0x100000),
                CmText("8 scroll01 %x: 0 100000b 24700000 0;", first + 0x200000)};
        };
        const auto trees = [&](const S_ScreenDesc &d) {
            std::vector<std::string> out;
            for (const S_ControlDesc *c : d.controls)
                out.push_back(masked(CsTree(c)));
            return out;
        };
        const auto joined = [](const std::vector<std::string> &v) {
            std::string s;
            for (const std::string &x : v)
                s += x + "\n";
            return s;
        };
        {
            // All there: the controls, the panels' lists and rows, the frames made, moved and copied.
            bool ok = true;
            dummies();
            m.m_startable = true;
            S_ScreenDesc d("create server", "create server", 9u << 26);
            g_gfLog.clear();
            ok &= m.DescribeCreateServerScreen(&d, As<I3D_frame>(model)) && !m.m_startable && d.messages.size() == 2 &&
                  Is(CsMessage(d.messages[0]), "0 1000007 400d000 self shown") &&
                  Is(CsMessage(d.messages[1]), "0 1000007 400c000 self hidden");
            std::vector<std::string> want = {head0, head1, panel};
            for (const std::string &x : checks(0x24400000))
                want.push_back(x);
            want.push_back(options);
            for (const std::string &x : tail(0x24800000))
                want.push_back(x);
            ok &= Is(joined(trees(d)), joined(want));
            if (d.controls.size() == 20) {
                const S_ControlDesc *mapsPanel = d.controls[2];
                ok &= mapsPanel->children.size() == 7 && origin(mapsPanel->children[1], 0.5f, 0.25f) &&
                      origin(mapsPanel->children[4], 0.5f, 0.25f) && origin(mapsPanel->children[6], 0.75f, 0.25f) &&
                      origin(d.controls[6], 0.125f, -0.5f) && d.controls[6]->children.size() == 18 &&
                      std::string(reinterpret_cast<const char *>(d.controls[6]->children[16]->messages[0].value2)) == " \t>-\"";
            } else {
                ok = false;
            }
            const std::string log = Join(g_gfLog);
            ok &= GmInOrder(log, {"cspanel 38 csmodel 0", "gr_mp_deathmatch 38 cspanel 0", "gr_mp_sel_list 38 cspanel 0",
                                  "csmodel 48 bmap_A 20", "csmodel 48 bmap_B 20", "csmodel 48 scroll00 1", "csmodel 48 scroll13 1",
                                  "cssc0 38 cspanel 1", "cssc0 24", "cssc13 38 cspanel 1", "cssc13 24", "csmapa on 0", "csmapa 24",
                                  "csmapb on 0", "csmapb 24", "csmapa 38 gr_mp_deathmatch 1", "clone tree csmapa 0",
                                  "csc0 34 MPMap00", "csc0 38 gr_mp_deathmatch 0", "csmapa 38 gr_mp_zonegame 1",
                                  "csmapa 38 gr_mp_hiddenmaps 1", "csmapa 38 gr_mp_coopmaps 1", "csc2 38 gr_mp_coopmaps 0",
                                  "csmapb 38 gr_mp_sel_list 1", "clone tree csmapb 0", "csc52 38 gr_mp_sel_list 0",
                                  "gr_setings 38 csmodel 0", "csmodel 48 yes_no_tmpl 20", "csmodel 48 bshort00 20",
                                  "csmodel 48 binput 20", "csmodel 48 combo_tmpl 20", "csyn on 0", "csnum on 0", "csinput on 0",
                                  "cscombo on 0", "clone tree cscombo 0", "csc53 34 coop difficulty",
                                  "clone tree csnum 0", "csc54 34 player num", "clone tree csyn 0", "csc55 34 Team Respawn",
                                  "clone tree csinput 0", "csc69 34 bpassword", "clone tree csinput 0", "csc70 34 bport"});
            // The frames made at the origin.
            for (const char *frame : {"cspanel", "gr_mp_deathmatch", "gr_mp_zonegame", "gr_mp_hiddenmaps", "gr_mp_coopmaps",
                                      "gr_mp_sel_list", "gr_setings"})
                ok &= GmInOrder(log, {CmText("%s 8 (0 0 0)", frame)});
            // Each panel alone: true.
            dummies();
            S_ScreenDesc d5("create server", "create server", 9u << 26);
            ok &= m.DescribeMapsPanel(&d5, As<I3D_frame>(model)) && d5.controls.size() == 1;
            dummies();
            S_ScreenDesc d6("create server", "create server", 9u << 26);
            ok &= m.DescribeOptionsPanel(&d6, As<I3D_frame>(model)) && d6.controls.size() == 4;
            WM(r.csDescribe, ok);
        }
        {
            // A template missing: its panel not described (false), the controls after it take its places.
            bool ok = true;
            dummies();
            g_msChildren.erase({model, "bmap_B"});
            S_ScreenDesc d("create server", "create server", 9u << 26);
            ok &= m.DescribeMapsPanel(&d, As<I3D_frame>(model)) == false && d.controls.empty();
            dummies();
            S_ScreenDesc d2("create server", "create server", 9u << 26);
            ok &= m.DescribeCreateServerScreen(&d2, As<I3D_frame>(model));
            std::vector<std::string> want = {head0, head1};
            for (const std::string &x : checks(0x24300000))
                want.push_back(x);
            want.push_back("23 gr_setings 24600000: 0 100000b 24600000 0 0 1000028 7 0 0 100002a 0 3d23d70a 0 100006b 0 ORIGIN"
                           " 0 200002c 0 0 {" + options.substr(options.find(" 28 coop difficulty 24701000")));
            ok &= trees(d2).size() == 19;
            g_msChildren[{model, "bmap_B"}] = frames["bmap_B"];
            g_msChildren.erase({model, "combo_tmpl"});
            dummies();
            S_ScreenDesc d3("create server", "create server", 9u << 26);
            ok &= m.DescribeOptionsPanel(&d3, As<I3D_frame>(model)) == false && d3.controls.empty();
            dummies();
            S_ScreenDesc d4("create server", "create server", 9u << 26);
            ok &= m.DescribeCreateServerScreen(&d4, As<I3D_frame>(model));
            want = {head0, head1, panel};
            for (const std::string &x : tail(0x24400000))
                want.push_back(x);
            ok &= Is(joined(trees(d4)), joined(want));
            g_msChildren[{model, "combo_tmpl"}] = frames["combo_tmpl"];
            // Each other frame missing alone: false.
            for (const char *missing : {"scroll00", "scroll13", "bmap_A"}) {
                g_msChildren.erase({model, missing});
                dummies();
                S_ScreenDesc d7("create server", "create server", 9u << 26);
                ok &= !m.DescribeMapsPanel(&d7, As<I3D_frame>(model)) && d7.controls.empty();
                g_msChildren[{model, missing}] = frames[missing];
            }
            for (const char *missing : {"yes_no_tmpl", "bshort00", "binput"}) {
                g_msChildren.erase({model, missing});
                dummies();
                S_ScreenDesc d8("create server", "create server", 9u << 26);
                ok &= !m.DescribeOptionsPanel(&d8, As<I3D_frame>(model)) && d8.controls.empty();
                g_msChildren[{model, missing}] = frames[missing];
            }
            WM(r.csDescribe, ok);
        }
        g_msFrameVt[0x08 / 4] = oldSetPos;
        g_mrMade.erase(FRAME_DUMMY);
        m.m_frames.clear();
        m.m_models.clear();
        m.m_mpSettings = nullptr;
    }

    // --- (part 9h-xiv) the create server screen: shown, hidden, the host, a preset, the picked maps, start ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("CSCALL %s\nCSCALL expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        C_MpMenuSettings settings;
        m.m_mpSettings = &settings;
        C_MpGameSetup &setup = settings.m_setup;
        for (auto &group : setup.maps) {
            for (C_MpMap *map : group.maps)
                delete map;
            group.maps.clear();
        }
        C_MpMap a(C_str("ma"), C_str("da")), b(C_str("mb"), C_str("db")), c(C_str("mc"), C_str("dc"));
        setup.maps[0].maps = {&a, &b};
        setup.maps[1].maps = {&c};
        setup.maps[3].maps = {&b, &a, &c};
        {
            // The setup: the current group by rotation and game type (another type: the last); a rotation picked
            // (out of range: nothing; the first: every map in the list's order).
            bool ok = true;
            setup.rotation = 2;
            for (int type : {0, 1, 2, 3, 7}) {
                setup.gameType = type;
                ok &= &setup.GetCurrentGroup() == &setup.rotations[2].types[type > 3 ? 3 : type];
            }
            const C_MpGameSetup &constSetup = setup;
            ok &= &constSetup.GetCurrentGroup() == &setup.rotations[2].types[3];
            setup.SetGameType(1);
            ok &= setup.gameType == 1;
            setup.rotations[0].types[0].maps = {&c};
            setup.rotations[0].types[2].maps = {&c};
            setup.SelectRotation(-1);
            setup.SelectRotation(5);
            ok &= setup.rotation == 2;
            setup.SelectRotation(4);
            ok &= setup.rotation == 4 && setup.rotations[0].types[0].maps == std::vector<C_MpMap *>({&c});
            setup.SelectRotation(0);
            ok &= setup.rotation == 0;
            for (int type = 0; type < 4; ++type)
                ok &= setup.rotations[0].types[type].maps == setup.maps[type].maps;
            WM(r.csSetup, ok);
        }
        {
            // Shown: the settings loaded (the saved ones back); every control set. Hidden: saved.
            bool ok = true;
            setup.serverName = "Srv";
            setup.gameType = 2;
            setup.dedicated = true;
            setup.password = "pw";
            setup.port = 1234;
            setup.voiceCodec = 3;
            setup.rotation = 4;
            g_files.clear();
            settings.Save();
            setup.serverName = "Other";
            setup.gameType = 0;
            setup.port = 1;
            const int oldListing = g_mpSettings->unknown174;
            g_mpSettings->unknown174 = 1;
            CsQueue();
            ok &= C_MainMenu::OnCreateServerShown(&m, nullptr, nullptr, 0, 0) &&
                  Is(CsQueue(), "24100000 1000014 d 0 'Srv'; 24200000 100006d 2 0; 24400000 1000076 1 0; 24500000 1000076 1 0;"
                                " 24711000 1000078 d 0 'pw'; 24712000 1000078 d 4d2; 24800000 100006d 3 0; 25200000 1000076 4 0; ");
            g_mpSettings->unknown174 = 2;
            setup.dedicated = false;
            settings.Save();
            ok &= C_MainMenu::OnCreateServerShown(&m, nullptr, nullptr, 0, 0) &&
                  CsQueue().find("24400000 1000076 0 0; 24500000 1000076 0 0;") != std::string::npos;
            g_mpSettings->unknown174 = oldListing;
            g_files.clear();
            setup.port = 4321;
            ok &= C_MainMenu::OnCreateServerHidden(&m, nullptr, nullptr, 0, 0);
            setup.port = 1;
            settings.Load();
            ok &= setup.port == 4321 && CsQueue() == "";
            WM(r.csShown, ok);
        }
        {
            // Start: on with maps in the current group and a name; else off.
            bool ok = true;
            setup.rotation = 1;
            setup.gameType = 0;
            setup.rotations[1].types[0].maps = {&a};
            setup.serverName = "Srv";
            m.m_startable = false;
            m.RefreshStartButton();
            ok &= m.m_startable && Is(CsQueue(), "24900000 1000002 0 0; ");
            setup.serverName = "";
            m.RefreshStartButton();
            ok &= !m.m_startable && Is(CsQueue(), "24900000 1000001 0 0; ");
            setup.serverName = "Srv";
            setup.rotations[1].types[0].maps.clear();
            m.RefreshStartButton();
            ok &= !m.m_startable && Is(CsQueue(), "24900000 1000001 0 0; ");
            WM(r.csStart, ok);
        }
        {
            // The picked list: hidden, the current group's maps (the dragged one current), wrapped in notifications
            // when nothing is dragged.
            bool ok = true;
            setup.rotations[1].types[0].maps = {&b, &a, &c};
            m.m_hint.m_on = false;
            m.m_hint.m_unknown00 = 1;
            const auto entries = [&](int current) {
                std::string q = "24307000 2000085 0 0; ";
                const C_MpMap *const list[] = {&b, &a, &c};
                for (int i = 0; i < 3; ++i)
                    q += CmText("24307000 %x %x %x; ", i == current ? 0x1000064 : 0x1000067, i,
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list[i])));
                return q;
            };
            m.ShowPickedMaps();
            ok &= Is(CsQueue(), "0 10000a1 4004000 0; " + entries(-1) + "0 10000a1 4005000 0; ");
            m.m_hint.m_on = true;
            m.ShowPickedMaps();
            ok &= Is(CsQueue(), entries(1));
            m.m_hint.m_on = false;
            WM(r.csPicked, ok);
        }
        {
            // The host edited (0x0B): its text the name (none: shown again); start refreshed. A preset picked.
            bool ok = true;
            C_PwBox host(0x24100000, nullptr);
            host.text = "New";
            setup.rotations[1].types[0].maps = {&a};
            g_gfLog.clear();
            ok &= C_MainMenu::OnHostNameChanged(&m, nullptr, &host, CONTROL_NOTIFY_0B, 0) &&
                  std::string(setup.serverName.c_str()) == "New" && MsLogIs({"box 0 200002f"}, g_gfLog) &&
                  Is(CsQueue(), "24900000 1000002 0 0; ");
            host.text = "";
            g_gfLog.clear();
            ok &= C_MainMenu::OnHostNameChanged(&m, nullptr, &host, CONTROL_NOTIFY_0B, 0) &&
                  std::string(setup.serverName.c_str()) == "New" && MsLogIs({"box 0 200002f", "box 0 1000014"}, g_gfLog) &&
                  Is(CsQueue(), "24900000 1000002 0 0; ");
            g_gfLog.clear();
            ok &= C_MainMenu::OnHostNameChanged(&m, nullptr, &host, CONTROL_NOTIFY_CLICK, 0) && g_gfLog.empty() &&
                  CsQueue() == "";
            // (The text restored: the name, in the button's style.)
            struct C_CsHostLog : C_Control {
                C_CsHostLog() : C_Control(nullptr, 0x24100000, nullptr) {}
                std::string seen;
                bool HandleMessage(S_ControlMessage *message) override
                {
                    if (message->id == CONTROL_MSG_SET_TEXT)
                        seen = CmText("%x %s", message->value, message->text.c_str());
                    return true;
                }
            } logged;
            C_MainMenu::OnHostNameChanged(&m, nullptr, &logged, CONTROL_NOTIFY_0B, 0);
            ok &= logged.seen == "d New";
            CsQueue();
            // A preset picked: that rotation; the picked maps; start.
            C_MsScreen screen(nullptr, "csscreen", 9u << 26);
            C_FlControl preset(0x25200000, &screen, 0x11, 0);
            setup.rotation = 1;
            ok &= C_MainMenu::OnMapPresetPicked(&m, nullptr, &preset, 0x400E000, 0) && setup.rotation == 0;
            std::string q = CsQueue();
            ok &= q.find("24307000 2000085 0 0; ") != std::string::npos && q.find("24900000 1000002 0 0; ") != std::string::npos;
            preset.value = 3;
            setup.rotations[3].types[0].maps.clear();
            ok &= C_MainMenu::OnMapPresetPicked(&m, nullptr, &preset, 0x400E000, 0) && setup.rotation == 3;
            q = CsQueue();
            ok &= q.find("24900000 1000001 0 0; ") != std::string::npos;
            ok &= C_MainMenu::OnMapPresetPicked(&m, nullptr, nullptr, 0x400E000, 0) && setup.rotation == 3 && CsQueue() == "";
            WM(r.csHost, ok);
        }
        for (S_MpRotation &rotation : setup.rotations)
            rotation.Clear();
        for (auto &group : setup.maps)
            group.maps.clear();
        m.m_mpSettings = nullptr;
    }

    // --- (part 9h-xv) the create server screen: the game type picked, its maps and options; an option changed ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("CSOPT %s\nCSOPT expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        C_MpMenuSettings settings;
        m.m_mpSettings = &settings;
        C_MpGameSetup &setup = settings.m_setup;
        for (auto &group : setup.maps) {
            for (C_MpMap *map : group.maps)
                delete map;
            group.maps.clear();
        }
        for (S_MpRotation &rotation : setup.rotations)
            rotation.Clear();
        {
            // The current rules by the game type (another: the last).
            bool ok = true;
            for (int type : {0, 1, 2, 3, 9}) {
                setup.gameType = type;
                ok &= &setup.GetCurrentRules() == &setup.rules[type > 3 ? 3 : type];
            }
            const C_MpGameSetup &constSetup = setup;
            ok &= &constSetup.GetCurrentRules() == &setup.rules[3];
            WM(r.csRules, ok);
        }
        {
            // The game type's map list shown, the others hidden.
            bool ok = true;
            CsQueue();
            m.ShowMapList(1);
            ok &= Is(CsQueue(), "24302000 1000004 0 0; 24303000 1000003 0 0; 24301000 100000b 24303000 0; 24303000 200002c 0 0;"
                                " 24304000 1000004 0 0; 24305000 1000004 0 0; ");
            m.ShowMapList(-1);
            ok &= Is(CsQueue(), "24302000 1000004 0 0; 24303000 1000004 0 0; 24304000 1000004 0 0; 24305000 1000004 0 0; ");
            WM(r.csMapList, ok);
        }
        // The rules' values (distinct), the options' posts from them.
        S_MpGameRules &rules = setup.rules[2];
        setup.gameType = 2;
        const int values[] = {11, 12, 13, 14, 15, 16, 1, 0, 1, 1, 17, 18};
        int *const fields[] = {&rules.maxClients,   &rules.pointLimit,   &rules.roundLimit,      &rules.roundCount,
                               &rules.warmup,       &rules.respawnTime,  &rules.allowRespawn,    &rules.friendlyFire,
                               &rules.autoTeamBalance, &rules.thirdPersonView, &rules.spawnProtection, &rules.inverseDamage};
        for (int i = 0; i < 12; ++i)
            *fields[i] = values[i];
        rules.coopDifficulty = 3;
        rules.coopLives = 19;
        rules.allowCrosshair = 1;
        rules.teamLives = 0;
        const std::string base = "24703000 1000076 0 0; 24701000 1000076 2 0; 24704000 1000078 d 13; 24702000 1000078 d b;"
                                 " 24705000 1000078 d c; 24706000 1000078 d d; 24707000 1000078 d e; 24708000 1000078 d f;"
                                 " 2470a000 1000078 d 10; 2470b000 1000078 d 11; 2470c000 1000078 d 12; 24709000 1000076 1 0;"
                                 " 2470d000 1000076 0 0; 2470e000 1000076 1 0; 2470f000 1000076 1 0; 24710000 1000076 1 0;"
                                 " 2470c000 1000001 0 0; 2470a000 1000002 0 0; ";
        const auto en = [](uint32_t row, bool on) { return CmText("%x %x 0 0; ", row, on ? 0x1000002 : 0x1000001); };
        const auto typeRows = [&](int type) {
            std::string q;
            if (type == 3) {
                q += "24709000 1000076 1 0; " + en(0x2470a000, true) + "24702000 10000a0 2 6; " + en(0x24701000, true) +
                     en(0x24704000, true) + en(0x24703000, true) + "2470e000 1000076 0 0; " + en(0x24705000, false) +
                     en(0x24709000, false) + en(0x2470d000, true) + en(0x2470e000, false) + en(0x24706000, false);
            } else if (type >= 0 && type <= 2) {
                q += "24702000 10000a0 2 20; " + en(0x24701000, false) + en(0x24704000, false) + en(0x24703000, false) +
                     en(0x24705000, type != 2) + en(0x24709000, type == 2) + en(0x2470d000, type != 0) +
                     en(0x2470e000, type != 0) + en(0x24706000, true);
            }
            return q;
        };
        {
            // The options: the rules' values, the two dependent rows, the game type's rows.
            bool ok = true;
            for (int type : {0, 1, 2, 3, 5, -1}) {
                m.ShowServerOptions(type);
                ok &= Is(CsQueue(), base + typeRows(type));
            }
            // Friendly fire on, respawning off: inverse damage on, the respawn time off.
            rules.friendlyFire = 1;
            rules.allowRespawn = 0;
            m.ShowServerOptions(7);
            const std::string q = CsQueue();
            ok &= q.find("2470c000 1000002 0 0; 2470a000 1000001 0 0; ") != std::string::npos;
            rules.friendlyFire = 0;
            rules.allowRespawn = 1;
            WM(r.csOptions, ok);
        }
        {
            // The game type picked (GET_SELECTED): set up; the picked maps, start, its list, its options. None: nothing.
            bool ok = true;
            struct C_CsStyle : C_Control {
                C_CsStyle() : C_Control(nullptr, 0x24200000, nullptr) {}
                int picked = 2;
                bool HandleMessage(S_ControlMessage *message) override
                {
                    if (message->id == CONTROL_MSG_GET_SELECTED)
                        message->value = static_cast<uint32_t>(picked);
                    return true;
                }
            } style;
            setup.gameType = 0;
            setup.serverName = "Srv";
            ok &= C_MainMenu::OnServerStyleChanged(&m, nullptr, &style, CONTROL_NOTIFY_CHANGED, 0) && setup.gameType == 2;
            const std::string q = CsQueue();
            const std::string list = "24302000 1000004 0 0; 24303000 1000004 0 0; 24304000 1000003 0 0; 24301000 100000b 24304000 0;"
                                     " 24304000 200002c 0 0; 24305000 1000004 0 0; ";
            ok &= Is(q, "0 10000a1 4004000 0; 24307000 2000085 0 0; 0 10000a1 4005000 0; 24900000 1000001 0 0; " + list + base +
                            typeRows(2));
            style.picked = -1;
            ok &= C_MainMenu::OnServerStyleChanged(&m, nullptr, &style, CONTROL_NOTIFY_CHANGED, 0) && setup.gameType == 2 &&
                  CsQueue() == "";
            style.picked = 0;
            ok &= C_MainMenu::OnServerStyleChanged(&m, nullptr, &style, CONTROL_NOTIFY_CHANGED, 0) && setup.gameType == 0;
            CsQueue();
            setup.gameType = 2;
            WM(r.csStyle, ok);
        }
        {
            // An option changed: into the setup or the current rules; the two dependent rows (rows only).
            bool ok = true;
            struct C_CsEdit : C_Control {
                C_CsEdit(uint32_t id, C_Control *parent, int type, const char *t) : C_Control(nullptr, id, parent), text(t)
                {
                    m_type = type;
                }
                const char *text;
                bool HandleMessage(S_ControlMessage *message) override
                {
                    if (message->id == CONTROL_MSG_GET_TEXT)
                        message->text = C_str(text);
                    return true;
                }
            };
            C_MsScreen screen(nullptr, "csopt", 9u << 26);
            m.m_screens.assign(10, nullptr);
            m.m_screens[9] = &screen;
            const struct {
                uint32_t id;
                int type, value;
                int *field;
            } rows[] = {{0x24701000, 0x28, 1, &rules.coopDifficulty}, {0x24702000, 0xE, 21, &rules.maxClients},
                        {0x24703000, 0x27, 1, &rules.teamLives},      {0x24704000, 0xE, 22, &rules.coopLives},
                        {0x24705000, 0xE, 23, &rules.pointLimit},     {0x24706000, 0xE, 24, &rules.roundLimit},
                        {0x24707000, 0xE, 25, &rules.roundCount},     {0x24708000, 0xE, 26, &rules.warmup},
                        {0x24709000, 0x27, 0, &rules.allowRespawn},   {0x2470a000, 0xE, 27, &rules.respawnTime},
                        {0x2470b000, 0xE, 28, &rules.spawnProtection}, {0x2470c000, 0xE, 29, &rules.inverseDamage},
                        {0x2470d000, 0x27, 1, &rules.friendlyFire},   {0x2470e000, 0x27, 0, &rules.autoTeamBalance},
                        {0x2470f000, 0x27, 0, &rules.thirdPersonView}, {0x24710000, 0x27, 0, &rules.allowCrosshair}};
            // (The rows under the options list, as the screen makes them.)
            C_FlControl *list = new C_FlControl(0x24700000, &screen, 0x23, 0);
            screen.m_children.push_back(list);
            for (const auto &row : rows)
                list->m_children.push_back(new C_FlControl(row.id, list, row.type, row.value));
            C_FlControl *port = new C_FlControl(0x24712000, list, 0xE, 2345);
            C_CsEdit *password = new C_CsEdit(0x24711000, list, 0xE, "secret");
            list->m_children.push_back(port);
            list->m_children.push_back(password);
            C_FlControl *internet = new C_FlControl(0x24400000, &screen, 0x24, 1);
            C_FlControl *dedicated = new C_FlControl(0x24500000, &screen, 0x24, 1);
            C_FlControl *voice = new C_FlControl(0x24800000, &screen, 0x10, 5);
            for (C_Control *c : std::initializer_list<C_Control *>{internet, dedicated, voice})
                screen.m_children.push_back(c);
            // Each row (its id's low part ignored): its rule (the difficulty + 1); then the dependent rows.
            for (size_t i = 0; i < 16; ++i) {
                C_FlControl part(rows[i].id | 0x7, nullptr, rows[i].type, 0);
                *rows[i].field = -5;
                ok &= C_MainMenu::OnServerOptionChanged(&m, nullptr, &part, CONTROL_NOTIFY_CHANGED, 0) &&
                      *rows[i].field == rows[i].value + (i == 0 ? 1 : 0);
                const std::string q = CsQueue();
                ok &= Is(q, en(0x2470c000, rules.friendlyFire != 0) + en(0x2470a000, rules.allowRespawn != 0));
            }
            // The password (a titled edit box's text; another kind: empty), the port.
            C_FlControl part(0x24711003, nullptr, 0xE, 0);
            ok &= C_MainMenu::OnServerOptionChanged(&m, nullptr, &part, 0, 0) && std::string(setup.password.c_str()) == "secret";
            ok &= std::string(m.GetEditText(0x24711000).c_str()) == "secret" && m.GetEditText(0x24712000).IsEmpty() &&
                  m.GetEditText(0x24400000).IsEmpty() &&
                  m.GetEditText(0x24799000).IsEmpty();
            C_FlControl portPart(0x24712000, nullptr, 0xE, 0);
            ok &= C_MainMenu::OnServerOptionChanged(&m, nullptr, &portPart, 0, 0) && setup.port == 2345;
            // An unknown row: only the dependent rows.
            C_FlControl unknownRow(0x24799000, nullptr, 0xE, 0);
            ok &= C_MainMenu::OnServerOptionChanged(&m, nullptr, &unknownRow, 0, 0) && CsQueue().size() > 0;
            CsQueue();
            // The screen's own: internet, dedicated, the voice codec (no dependent rows); another place: nothing.
            setup.internet = false;
            setup.dedicated = false;
            setup.voiceCodec = 0;
            ok &= C_MainMenu::OnServerOptionChanged(&m, nullptr, internet, 0, 0) && setup.internet &&
                  C_MainMenu::OnServerOptionChanged(&m, nullptr, dedicated, 0, 0) && setup.dedicated &&
                  C_MainMenu::OnServerOptionChanged(&m, nullptr, voice, 0, 0) && setup.voiceCodec == 5 && CsQueue() == "";
            internet->value = 0;
            C_FlControl other(0x24200000, nullptr, 0x10, 0);
            ok &= C_MainMenu::OnServerOptionChanged(&m, nullptr, &other, 0, 0) && setup.internet && CsQueue() == "";
            ok &= C_MainMenu::OnServerOptionChanged(&m, nullptr, internet, 0, 0) && !setup.internet;
            ok &= !C_MainMenu::OnServerOptionChanged(&m, nullptr, nullptr, 0, 0) && CsQueue() == "";
            m.m_screens.clear();
            WM(r.csOption, ok);
        }
        m.m_mpSettings = nullptr;
    }

    // --- (part 9h-xvi) the create server screen's map lines: double click, drag and drop; the setup's moves ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("CSDRAG %s\nCSDRAG expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        C_MpMenuSettings settings;
        m.m_mpSettings = &settings;
        C_MpGameSetup &setup = settings.m_setup;
        for (auto &group : setup.maps) {
            for (C_MpMap *map : group.maps)
                delete map;
            group.maps.clear();
        }
        for (S_MpRotation &rotation : setup.rotations)
            rotation.Clear();
        C_MpMap a(C_str("ma"), C_str("da")), b(C_str("mb"), C_str("db")), c(C_str("mc"), C_str("dc")), d(C_str("md"), C_str("dd"));
        setup.maps[1].maps = {&a, &b, &c};
        setup.maps[3].maps = {&d};
        setup.gameType = 1;
        setup.rotation = 2;
        setup.serverName = "Srv";
        std::vector<C_MpMap *> &group = setup.rotations[2].types[1].maps;
        const auto names = [&] {
            std::string s;
            for (C_MpMap *map : group)
                s += map->GetName();
            return s;
        };
        {
            // The setup: the type's list (another type: the last); a map added (from the list), taken off, moved.
            bool ok = true;
            for (int type : {0, 1, 2, 3, 8}) {
                setup.gameType = type;
                ok &= &setup.GetCurrentMaps() == &setup.maps[type > 3 ? 3 : type];
            }
            setup.gameType = 1;
            setup.AddMap(2);
            setup.AddMap(0);
            setup.AddMap(-1);
            setup.AddMap(3);
            ok &= names() == "mcma";
            setup.AddMap(1);
            setup.MoveMap(2, 0);
            ok &= names() == "mbmcma";
            setup.MoveMap(0, 2);
            ok &= names() == "mcmamb";
            setup.RemoveMap(-1);
            setup.RemoveMap(3);
            setup.RemoveMap(1);
            ok &= names() == "mcmb";
            group.clear();
            setup.RemoveMap(0);
            setup.AddMap(0);
            ok &= names() == "ma";
            setup.gameType = 3;
            setup.AddMap(0);
            ok &= setup.rotations[2].types[3].maps == std::vector<C_MpMap *>({&d});
            setup.gameType = 1;
            WM(r.csMoves, ok);
        }
        C_Control pickedList(nullptr, 0x24307000, nullptr), mapsList(nullptr, 0x24303000, nullptr);
        const std::string start = "24900000 1000002 0 0; ", noStart = "24900000 1000001 0 0; ";
        const auto picked = [&] {
            std::string q = "0 10000a1 4004000 0; 24307000 2000085 0 0; ";
            for (size_t i = 0; i < group.size(); ++i)
                q += CmText("24307000 1000067 %x %x; ", static_cast<uint32_t>(i),
                            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(group[i])));
            return q + "0 10000a1 4005000 0; ";
        };
        {
            // Double click: a picked map taken off; a list's added; another notification / no parent: nothing.
            bool ok = true;
            group = {&a, &b};
            CsQueue();
            C_CsLine pickedLine(0x24307020, 1, &pickedList, nullptr), listLine(0x24303030, 2, &mapsList, nullptr),
                orphan(0x24303030, 2, nullptr, nullptr);
            ok &= C_MainMenu::OnMapLineDoubleClick(&m, nullptr, &pickedLine, CONTROL_NOTIFY_13, 0) && names() == "ma" &&
                  Is(CsQueue(), picked() + start);
            ok &= C_MainMenu::OnMapLineDoubleClick(&m, nullptr, &listLine, CONTROL_NOTIFY_13, 0) && names() == "mamc" &&
                  Is(CsQueue(), picked() + start);
            ok &= C_MainMenu::OnMapLineDoubleClick(&m, nullptr, &listLine, CONTROL_NOTIFY_CLICK, 0) && names() == "mamc" &&
                  C_MainMenu::OnMapLineDoubleClick(&m, nullptr, &orphan, CONTROL_NOTIFY_13, 0) && names() == "mamc" &&
                  CsQueue() == "";
            group.clear();
            ok &= C_MainMenu::OnMapLineDoubleClick(&m, nullptr, &pickedLine, CONTROL_NOTIFY_13, 0) &&
                  Is(CsQueue(), picked() + noStart);
            WM(r.csDouble, ok);
        }
        // The drag's frames: the line's (its world place), the copy made.
        m.m_cursor = C_Cursor::Acquire(nullptr);
        const auto cursor = [&](float x, float y, uint32_t hovered) {
            g_csCursor.position = S_vector(x, y, 0.0f);
            std::memcpy(g_csCursor.room + 8, &hovered, sizeof hovered);
        };
        void **const copyVt = new void *[MR_SLOTS];
        std::copy(g_msFrameVt, g_msFrameVt + MR_SLOTS, copyVt);
        copyVt[0x00 / 4] = reinterpret_cast<void *>(&MrFake<0x00>);
        copyVt[0x4C / 4] = reinterpret_cast<void *>(&MrFake<0x4C, void *>);
        copyVt[0x04 / 4] = reinterpret_cast<void *>(&MrFake<0x04>);
        copyVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
        copyVt[0x0C / 4] = reinterpret_cast<void *>(&MrFake<0x0C, const S_vector *, float>);
        S_MrObj *source = MsFrame("csds"), *copy = MsFrame("csdc");
        source->vtable = copyVt;
        copy->vtable = copyVt;
        As<I3D_frame>(source)->m_worldMatrix.m[3][0] = 0.25f;
        As<I3D_frame>(source)->m_worldMatrix.m[3][1] = -0.5f;
        As<I3D_frame>(source)->m_worldMatrix.m[3][2] = 0.75f;
        As<I3D_frame>(source)->m_frameFlags |= 0x20;
        As<I3D_frame>(copy)->m_worldMatrix.m[3][0] = 0.5f;
        As<I3D_frame>(copy)->m_worldMatrix.m[3][1] = 0.25f;
        As<I3D_frame>(copy)->m_worldMatrix.m[3][2] = 2.0f;
        As<I3D_frame>(copy)->m_frameFlags |= 0x20;
        S_MrWorld world = MrWorld("csd");
        m.m_scene = As<I3D_scene>(world.scene);
        void *const oldVisual = g_driverVtable[0x50 / 4];
        g_driverVtable[0x50 / 4] = reinterpret_cast<void *>(&GmCreateVisual);
        g_gmVisual = As<I3D_frame>(copy);
        // (The screen: the picked list's lines found under the cursor.)
        C_MsScreen screen(nullptr, "csdscreen", 9u << 26);
        C_FlControl *pool = new C_FlControl(0x24307000, &screen, 0x26, 0);
        screen.m_children.push_back(pool);
        for (int i = 0; i < 3; ++i)
            pool->m_children.push_back(new C_CsLine(0x24307010 + 0x10 * static_cast<uint32_t>(i), i, pool, nullptr));
        C_FlControl *listControl = new C_FlControl(0x24303000, &screen, 0x23, 0);
        screen.m_children.push_back(listControl);
        listControl->m_children.push_back(new C_CsLine(0x24303010, 0, listControl, nullptr));
        m.m_screens.assign(10, nullptr);
        m.m_screens[9] = &screen;
        {
            // A drag started: a copy of the line's frame (facing up, a little in front, in the scene); the hint the
            // drag (its map's name). None without a highlight frame or a copy; out of range: copied, not dragged.
            bool ok = true;
            C_CsLine listLine(0x24303020, 1, &mapsList, As<I3D_frame>(source)), none(0x24303020, 1, &mapsList, nullptr),
                pickedLine(0x24307020, 0, &pickedList, As<I3D_frame>(source)), outOfRange(0x24303020, 3, &mapsList, As<I3D_frame>(source));
            m.m_hint.Reset();
            g_gfLog.clear();
            ok &= !C_MainMenu::OnMapLineDragStart(&m, nullptr, &none, CONTROL_NOTIFY_0A, 0) && g_gfLog.empty();
            ok &= C_MainMenu::OnMapLineDragStart(&m, nullptr, &listLine, CONTROL_NOTIFY_0A, 0) && m.m_hint.m_on &&
                  m.m_hint.m_unknown00 == 1 && m.m_hint.m_unknown04 == 1 && m.m_hint.m_frame == As<I3D_frame>(copy) &&
                  m.m_hint.m_hasText && m.m_hint.m_box.m_runs.size() == 1 && m.m_hint.m_box.m_runs[0].style == 0xD &&
                  std::string(m.m_hint.m_box.m_runs[0].text.c_str()) == "mb" &&
                  Is(Join(g_gfLog), "csds 4; create visual 0; csdc 4c csds; csdc 38 null 0; csdc c (0 1 0) 0;"
                                    " csdc 8 (0.25 -0.5 0.65); csdc 38 sector 1; csdc on 1; csdc 24; ");
            m.m_hint.Reset();
            group = {&c, &a};
            ok &= C_MainMenu::OnMapLineDragStart(&m, nullptr, &pickedLine, CONTROL_NOTIFY_0A, 0) && m.m_hint.m_on &&
                  m.m_hint.m_unknown00 == 0 && m.m_hint.m_unknown04 == 0;
            m.m_hint.Reset();
            g_gfLog.clear();
            ok &= C_MainMenu::OnMapLineDragStart(&m, nullptr, &outOfRange, CONTROL_NOTIFY_0A, 0) && !m.m_hint.m_on &&
                  !m.m_hint.m_frame && GmInOrder(Join(g_gfLog), {"create visual 0", "csdc 24"});
            group.clear();
            ok &= C_MainMenu::OnMapLineDragStart(&m, nullptr, &pickedLine, CONTROL_NOTIFY_0A, 0) && !m.m_hint.m_on;
            g_gmVisual = nullptr;
            ok &= !C_MainMenu::OnMapLineDragStart(&m, nullptr, &listLine, CONTROL_NOTIFY_0A, 0) && !m.m_hint.m_on;
            g_gmVisual = As<I3D_frame>(copy);
            WM(r.csDragStart, ok);
        }
        {
            // Moved: the copy with the cursor (its depth the move's); a picked map over another picked line moved
            // there (only from the picked list, only in its area; x / y compared as doubles).
            bool ok = true;
            m.m_hint.Reset();
            ok &= !C_MainMenu::OnMapLineDragMove(&m, nullptr, nullptr, 0, 0);
            group = {&a, &b, &c};
            m.m_hint.m_frame = As<I3D_frame>(copy);
            m.m_hint.m_unknown00 = 0;
            m.m_hint.m_unknown04 = 0;
            m.m_hint.m_on = true;
            g_csCursor.move = S_vector(0.125f, -0.25f, 0.5f);
            cursor(0.3f, -0.5f, 0x24307030);
            g_gfLog.clear();
            CsQueue();
            ok &= C_MainMenu::OnMapLineDragMove(&m, nullptr, nullptr, 0, 0) && names() == "mbmcma" &&
                  m.m_hint.m_unknown00 == 2 && GmInOrder(Join(g_gfLog), {"csdc 8 (0.625 0 0.5)", "csdc 24"});
            const std::string q = CsQueue();
            ok &= q.find("24307000 2000085") != std::string::npos && q.find("10000a1") == std::string::npos;
            // The same line: nothing; outside the area (each side); from a list; over another control: nothing.
            const auto still = [&](float x, float y, uint32_t hovered, int from) {
                group = {&a, &b, &c};
                m.m_hint.m_unknown00 = 0;
                m.m_hint.m_unknown04 = from;
                cursor(x, y, hovered);
                return C_MainMenu::OnMapLineDragMove(&m, nullptr, nullptr, 0, 0) && names() == "mambmc" &&
                       m.m_hint.m_unknown00 == 0 && CsQueue() == "";
            };
            ok &= still(0.3f, -0.5f, 0x24307010, 0) && still(0.28f - 0.0001f, -0.5f, 0x24307030, 0) &&
                  still(0.46f, -0.5f, 0x24307030, 0) && still(0.3f, -0.29f, 0x24307030, 0) &&
                  still(0.3f, -0.68f, 0x24307030, 0) && still(0.3f, -0.5f, 0x24307030, 1) &&
                  still(0.3f, -0.5f, 0x24303010, 0) && still(0.3f, -0.5f, 0x24399000, 0);
            // (0.28f is above 0.28 as a double: inside.)
            group = {&a, &b, &c};
            m.m_hint.m_unknown00 = 0;
            m.m_hint.m_unknown04 = 0;
            cursor(0.28f, -0.6799999f, 0x24307020);
            ok &= C_MainMenu::OnMapLineDragMove(&m, nullptr, nullptr, 0, 0) && names() == "mbmamc";
            // (-0.3f is below -0.3 as a double: inside.)
            group = {&a, &b, &c};
            m.m_hint.m_unknown00 = 0;
            cursor(0.3f, -0.3f, 0x24307020);
            ok &= C_MainMenu::OnMapLineDragMove(&m, nullptr, nullptr, 0, 0) && names() == "mbmamc";
            CsQueue();
            WM(r.csDragMove, ok);
        }
        {
            // Dropped: the copy released; a picked map over the lists taken off; a list's over the picked ones
            // added (moved to the line under the cursor); else nothing. The drag ended; the picked maps, start.
            bool ok = true;
            const auto drop = [&](float x, float y, uint32_t hovered, int index, int from) {
                m.m_hint.Reset();
                m.m_hint.m_frame = As<I3D_frame>(copy);
                m.m_hint.m_unknown00 = index;
                m.m_hint.m_unknown04 = from;
                m.m_hint.m_on = true;
                cursor(x, y, hovered);
                g_gfLog.clear();
                const bool result = C_MainMenu::OnMapLineDrop(&m, nullptr, nullptr, 0, 0);
                return result && !m.m_hint.m_on && !m.m_hint.m_frame && MrHas("csdc 0");
            };
            group = {&a, &b, &c};
            ok &= drop(0.1f, -0.5f, 0, 1, 0) && names() == "mamc" && Is(CsQueue(), picked() + start);
            group = {&a, &b, &c};
            ok &= drop(0.1f, -0.5f, 0, 1, 1) && names() == "mambmc" && Is(CsQueue(), picked() + start);
            group = {&a};
            ok &= drop(0.3f, -0.5f, 0x24307010, 2, 1) && names() == "mcma" &&
                  Is(CsQueue(), CmText("24307000 2000085 0 0; 24307000 1000064 0 %x; 24307000 1000067 1 %x; ",
                                       static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&c)),
                                       static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&a))) +
                                    picked() + start);
            group = {&a};
            ok &= drop(0.3f, -0.5f, 0x24307020, 2, 1) && names() == "mamc" && Is(CsQueue(), picked() + start);
            group = {&a};
            ok &= drop(0.3f, -0.5f, 0x24303010, 1, 1) && names() == "mamb" && Is(CsQueue(), picked() + start);
            group = {&a, &b};
            ok &= drop(0.3f, -0.5f, 0x24307010, 0, 0) && names() == "mamb" && Is(CsQueue(), picked() + start);
            // (Each side of the lists' area, compared as doubles: 0.08f and -0.68f outside, 0.26f and -0.3f inside.)
            for (const auto &at : {std::make_pair(0.08f, -0.5f), std::make_pair(0.2601f, -0.5f),
                                   std::make_pair(0.1f, -0.29f), std::make_pair(0.1f, -0.68f)}) {
                group = {&a, &b};
                ok &= drop(at.first, at.second, 0, 0, 0) && names() == "mamb";
            }
            for (const auto &at : {std::make_pair(0.26f, -0.5f), std::make_pair(0.1f, -0.3f), std::make_pair(0.0801f, -0.6799f)}) {
                group = {&a, &b};
                ok &= drop(at.first, at.second, 0, 0, 0) && names() == "mb";
            }
            CsQueue();
            // No copy: nothing released.
            m.m_hint.Reset();
            g_gfLog.clear();
            ok &= C_MainMenu::OnMapLineDrop(&m, nullptr, nullptr, 0, 0) && !MrHas("csdc 0");
            CsQueue();
            WM(r.csDrop, ok);
        }
        g_driverVtable[0x50 / 4] = oldVisual;
        g_gmVisual = nullptr;
        m.m_screens.clear();
        m.m_hint.Reset();
        m.m_cursor->Release();
        m.m_cursor = nullptr;
        m.m_scene = nullptr;
        for (S_MpRotation &rotation : setup.rotations)
            rotation.Clear();
        for (auto &g : setup.maps)
            g.maps.clear();
        m.m_mpSettings = nullptr;
    }

    // --- (part 9h-xvii) the hosted session set up from the create server screen ---
    {
        bool ok = true;
        C_MainMenu m;
        C_MpMenuSettings settings;
        m.m_mpSettings = &settings;
        C_MpGameSetup &setup = settings.m_setup;
        for (auto &group : setup.maps) {
            for (C_MpMap *map : group.maps)
                delete map;
            group.maps.clear();
        }
        for (S_MpRotation &rotation : setup.rotations)
            rotation.Clear();
        C_MpMap a(C_str("ma"), C_str("da")), b(C_str("mb"), C_str("db"));
        a.frontline = 1;
        a.presets = "p1";
        b.description = 77;
        setup.gameType = 3;
        setup.rotation = 1;
        setup.rotations[1].types[3].maps = {&a, &b};
        setup.serverName = "Server";
        m.m_profileName = "Player";
        struct C_HsEdit : C_Control {
            C_HsEdit(uint32_t id, C_Control *parent) : C_Control(nullptr, id, parent) { m_type = 0xE; }
            bool HandleMessage(S_ControlMessage *message) override
            {
                if (message->id == CONTROL_MSG_GET_TEXT)
                    message->text = C_str("pw");
                return true;
            }
        };
        C_MsScreen screen(nullptr, "hsscreen", 9u << 26);
        C_FlControl *list = new C_FlControl(0x24700000, &screen, 0x23, 0);
        screen.m_children.push_back(list);
        C_FlControl *style = new C_FlControl(0x24200000, &screen, 0x10, 3);
        for (C_Control *c : std::initializer_list<C_Control *>{style, new C_FlControl(0x24400000, &screen, 0x24, 1),
                                                                new C_FlControl(0x24500000, &screen, 0x24, 1),
                                                                new C_FlControl(0x24800000, &screen, 0x10, 6)})
            screen.m_children.push_back(c);
        const uint32_t rowIds[] = {0x24701000, 0x24702000, 0x24703000, 0x24704000, 0x24705000, 0x24706000, 0x24707000, 0x24708000,
                                   0x24709000, 0x2470a000, 0x2470b000, 0x2470c000, 0x2470d000, 0x2470e000, 0x2470f000, 0x24710000};
        for (int i = 0; i < 16; ++i)
            list->m_children.push_back(new C_FlControl(rowIds[i], list, i == 0 ? 0x28 : 0xE, 100 + i));
        list->m_children.push_back(new C_HsEdit(0x24711000, list));
        list->m_children.push_back(new C_FlControl(0x24712000, list, 0xE, 4321));
        m.m_screens.assign(10, nullptr);
        m.m_screens[9] = &screen;
        S_MultiplayerSettings *session = g_mpSettings;
        const S_MultiplayerSettings old = *session;
        session->maps.clear();
        session->sessionType = 0;
        g_gfLog.clear();
        m.SetUpHostedSession();
        ok &= session->unknown21C.c_str()[0] == 1 && session->sessionType == 2 &&
              std::string(session->sessionName) == "Server" && std::string(session->sessionName + 100) == "Player" &&
              session->gameType == 3 && session->unknown174 == 1 && session->dedicated && session->coopDifficulty == 101 &&
              session->coopLives == 103 && session->teamLives == 102 && session->maxClients == 101 &&
              session->pointLimit == 104 && session->roundLimit == 105 && session->roundCount == 106 && session->warmup == 107 &&
              session->allowRespawn == 108 && session->respawnTime == 109 && session->spawnProtection == 110 &&
              session->inverseDamage == 111 && session->friendlyFire == 112 && session->autoTeamBalance == 113 &&
              session->thirdPersonView == 114 && session->allowCrosshair == 115 &&
              std::string(session->password.c_str()) == "pw" && session->port == 4321 && session->unknown178 == 6;
        // The rotation's maps copied (every field).
        ok &= session->maps.size() == 2 && session->maps[0] != &a && session->maps[1] != &b &&
              std::string(session->maps[0]->GetName()) == "ma" && std::string(session->maps[0]->GetDirectory()) == "da" &&
              session->maps[0]->frontline == 1 && std::string(session->maps[0]->GetPresets()) == "p1" &&
              std::string(session->maps[1]->GetName()) == "mb" && session->maps[1]->description == 77;
        // Another game type: the cooperative rules' defaults.
        style->value = 1;
        m.SetUpHostedSession();
        ok &= session->gameType == 1 && session->coopDifficulty == 3 && session->coopLives == -1 && session->teamLives == 0 &&
              session->maps.size() == 2;
        // (A name longer than its field cut.)
        setup.serverName = C_str(std::string(150, 'x').c_str());
        m.SetUpHostedSession();
        ok &= std::strlen(session->sessionName) == 99 && std::string(session->sessionName + 100) == "Player";
        m.m_profileName = C_str(std::string(300, 'y').c_str());
        m.SetUpHostedSession();
        ok &= std::strlen(session->sessionName + 100) == 259;
        session->Reset();
        const std::vector<C_MpMap *> none;
        *session = old;
        session->maps = none;
        m.m_screens.clear();
        for (S_MpRotation &rotation : setup.rotations)
            rotation.Clear();
        m.m_mpSettings = nullptr;
        WM(r.hsSession, ok);
    }

    // --- (part 9h-xviii) the connect screen: its description, hidden, its tick's states; OK ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("CONNECT %s\nCONNECT expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "self";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnConnectTick)] = "tick";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnConnectHidden)] = "hidden";
        {
            // The model placed; the callbacks; the controls; the console's style.
            bool ok = true;
            S_MrObj *model = MsFrame("cnmodel");
            void *const oldSetPos = g_msFrameVt[0x08 / 4];
            g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
            S_ScreenDesc d("MP Connect console", "confirm", 13u << 26);
            g_gfLog.clear();
            g_connectStyle = -1;
            ok &= m.DescribeConnectScreen(&d, As<I3D_frame>(model)) && MsLogIs({"cnmodel 8 (0.5 -0.375 -0.5)"}, g_gfLog);
            g_msFrameVt[0x08 / 4] = oldSetPos;
            ok &= d.messages.size() == 2 && Is(CsMessage(d.messages[0]), "0 1000007 4014000 self tick") &&
                  Is(CsMessage(d.messages[1]), "0 1000007 400c000 self hidden") && d.controls.size() == 4;
            if (d.controls.size() == 4)
                ok &= Is(CsTree(d.controls[0]), "12 text 34100000:;") && Is(CsTree(d.controls[1]), "14 byes* 34200000: 0 1000004 0 0;") &&
                      Is(CsTree(d.controls[2]), "14 bno* 34300000: 0 1000004 0 0;") &&
                      Is(CsTree(d.controls[3]), "14 bok 34400000: 0 1000015 d 7d8 0 600100a 0 0;");
            const S_TextStyleDesc *style = g_connectStyle >= 0 ? GetTextStyle(g_connectStyle) : nullptr;
            ok &= style && style->font == 2 && style->color == 0xFF000000 && style->size == 0.035f && style->width == 0.0f &&
                  style->x == -1.0f && style->y == -1.0f && style->align == 1 && style->valign == 3 && style->joins == 0 &&
                  style->wraps == 1 && style->flags30 == 0;
            WM(r.cnDescribe, ok);
        }
        const int s = g_connectStyle;
        const auto line = [&](uint32_t text) { return CmText("34100000 1000017 %x %x; ", static_cast<uint32_t>(s), text); };
        {
            // Hidden: the console emptied, no wait.
            bool ok = true;
            m.m_connectWait = 77;
            ok &= C_MainMenu::OnConnectHidden(&m, nullptr, nullptr, 0, 0) && m.m_connectWait == 0 &&
                  Is(CsQueue(), "34100000 2000018 0 0; ");
            WM(r.cnHidden, ok);
        }
        {
            // The tick: another screen's or hidden: false; waiting; then each state.
            bool ok = true;
            struct C_CnConsole : C_Control {
                C_CnConsole(uint32_t id, bool shown) : C_Control(nullptr, id, nullptr) { m_shown = shown; }
            } console(0x34000000, true), other(0x30000000, true), hidden(0x34000000, false);
            S_MultiplayerSettings *settings = g_mpSettings;
            const int oldType = settings->sessionType;
            const bool oldDedicated = settings->dedicated;
            const auto tick = [&](int state, uint32_t value = 50) {
                m.m_connectState = state;
                g_gfLog.clear();
                return C_MainMenu::OnConnectTick(&m, nullptr, &console, CONTROL_NOTIFY_SCREEN_TICK, value);
            };
            m.m_connectState = 0;
            ok &= !C_MainMenu::OnConnectTick(&m, nullptr, &other, 0, 0) && !C_MainMenu::OnConnectTick(&m, nullptr, &hidden, 0, 0);
            m.m_connectWait = 120;
            ok &= tick(0, 50) && m.m_connectWait == 70 && m.m_connectState == 0 && CsQueue() == "";
            ok &= tick(0, 70) && m.m_connectWait == 0 && m.m_connectState == 0;
            m.m_connectWait = 1;
            ok &= tick(0, 50) && m.m_connectWait == -49 && m.m_connectState == 0 && CsQueue() == "";
            m.m_connectWait = 0;
            ok &= tick(0, 70) && m.m_connectState != 0;
            CsQueue();
            // Start: hosting (a dedicated server's / a server's), joining, else an error.
            settings->sessionType = 2;
            settings->dedicated = false;
            m.m_connectWait = 0;
            ok &= tick(0) && m.m_connectState == 3 && m.m_connectWait == 1500 && MsLogIs({"squad clear", "roster reload"}, g_gfLog) &&
                  Is(CsQueue(), "34100000 2000018 0 0; " + line(0x91B));
            settings->dedicated = true;
            m.m_connectWait = 0;
            ok &= tick(0) && m.m_connectState == 7 && m.m_connectWait == 1500;
            CsQueue();
            settings->sessionType = 1;
            m.m_connectWait = 0;
            ok &= tick(0) && m.m_connectState == 1 && m.m_connectWait == 300 &&
                  Is(CsQueue(), "34100000 2000018 0 0; " + line(0x91C) + line(0x91D));
            settings->sessionType = 0;
            m.m_connectWait = 0;
            ok &= tick(0) && m.m_connectState == 6 && m.m_connectWait == 1000 &&
                  Is(CsQueue(), "34100000 2000018 0 0; " + line(0x91E) + line(0x91F));
            // Connect: to the settings' server; joining.
            m.m_connectWait = 0;
            ok &= tick(1) && m.m_connectState == 2 && MsLogIs({CmText("net connect %d", static_cast<int>(offsetof(S_MultiplayerSettings, unknown17C)))}, g_gfLog) && CsQueue() == "";
            // Joining: connected; still on its way; refused (each).
            const int oldJoin = g_csJoin;
            g_csJoin = 1;
            ok &= tick(2) && m.m_connectState == 5 && m.m_connectWait == 300 && Is(CsQueue(), line(0x923) + line(0x924));
            for (int status : {0, 2, 9}) {
                g_csJoin = status;
                m.m_connectWait = 0;
                ok &= tick(2) && m.m_connectState == 2 && m.m_connectWait == 0 && CsQueue() == "";
            }
            const std::pair<int, uint32_t> refusals[] = {{3, 0x925}, {4, 0x927}, {5, 0x929}, {6, 0x22E9}, {7, 0x92A}, {8, 0x1CD4}};
            for (const auto &refusal : refusals) {
                g_csJoin = refusal.first;
                m.m_connectWait = 0;
                ok &= tick(2) && m.m_connectState == 6 && m.m_connectWait == 2500 &&
                      Is(CsQueue(), line(refusal.second) + line(refusal.first == 3 ? 0x926 : 0x928));
            }
            g_csJoin = oldJoin;
            // Host: the session's name, DirectPlay's fields, the menu's +0x1F0; made / failed.
            std::memset(settings->sessionName, 0, sizeof settings->sessionName);
            std::strcpy(settings->sessionName, "Srv");
            const int oldHost = g_csHost;
            g_csHost = 0;
            m.m_openFirewall = true;
            m.m_connectWait = 0;
            ok &= tick(3) && m.m_connectState == 4 && m.m_connectWait == 300 &&
                  MsLogIs({"net host 1 Srv ebfe7ba0 11d2628d 60000fae 1114b097 100 2000 0 0"}, g_gfLog) &&
                  Is(CsQueue(), line(0x922));
            g_csHost = -1;
            m.m_connectWait = 0;
            ok &= tick(3) && m.m_connectState == 6 && m.m_connectWait == 2500 && Is(CsQueue(), line(0x920) + line(0x921));
            g_csHost = oldHost;
            // The dedicated server: made / failed.
            g_cnDedicated = true;
            m.m_connectWait = 0;
            ok &= tick(7) && m.m_connectState == 8 && m.m_connectWait == 300 && MsLogIs({"net dedicated 1"}, g_gfLog) &&
                  Is(CsQueue(), line(0x922));
            g_cnDedicated = false;
            m.m_openFirewall = false;
            m.m_connectWait = 0;
            ok &= tick(7) && m.m_connectState == 6 && m.m_connectWait == 2500 && MsLogIs({"net dedicated 0"}, g_gfLog) &&
                  Is(CsQueue(), line(0x920) + line(0x921));
            // Hosted: the network handler's start / the menu quits; done. Done, failed: nothing.
            m.m_connectWait = 0;
            ok &= tick(4) && m.m_connectState == 5 && Is(CsQueue(), "0 2000069 0 0; ");
            ok &= tick(8) && m.m_connectState == 5 && Is(CsQueue(), "0 2000004 0 0; ");
            for (int state : {5, 6, 9, -1})
                ok &= tick(state) && m.m_connectState == state && CsQueue() == "" && g_gfLog.empty();
            settings->sessionType = oldType;
            settings->dedicated = oldDedicated;
            WM(r.cnTick, ok);
        }
        {
            // OK on the connect screen: closed, 0x2000028, start; another screen, no screens, another message: not taken.
            bool ok = true;
            C_MpMenuSettings settings;
            m.m_mpSettings = &settings;
            for (auto &group : settings.m_setup.maps) {
                for (C_MpMap *map : group.maps)
                    delete map;
                group.maps.clear();
            }
            C_MsScreen connect(nullptr, "cnscreen", 13u << 26), other(nullptr, "cnother", 8u << 26);
            S_ControlMessage message;
            message.id = 0x4000000 | 0x200000A;
            g_menuScreens = {&other, &connect};
            ok &= m.OnConnectMessage(&message) && Is(CsQueue(), "0 2000005 0 0; 0 2000028 0 0; 24900000 1000001 0 0; ");
            g_menuScreens = {&connect, &other};
            ok &= !m.OnConnectMessage(&message) && CsQueue() == "";
            g_menuScreens.clear();
            ok &= !m.OnConnectMessage(&message) && CsQueue() == "";
            g_menuScreens = {&other, &connect};
            message.id = 0x2000003;
            ok &= !m.OnConnectMessage(&message) && CsQueue() == "";
            g_menuScreens.clear();
            m.m_mpSettings = nullptr;
            WM(r.cnMessage, ok);
        }
        ReleaseTextStyle(g_connectStyle);
        g_connectStyle = -1;
    }

    // --- (part 9h-xix) the browser: its description and list, shown, sorting, a server picked / joined, the count ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("BROWSER %s\nBROWSER expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "self";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnBrowserTick)] = "tick";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnBrowserShown)] = "shown";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnServerSort)] = "sort";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnRestoreList)] = "restore";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnRefreshList)] = "refresh";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnFavoriteToggle)] = "favorite";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnFavorites)] = "favorites";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnServerPicked)] = "picked";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnServerJoin)] = "join";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnServerCountChanged)] = "count";
        S_MrObj *model = MsFrame("bwmodel"), *row = MsFrame("bwrow");
        void *const oldSetPos = g_msFrameVt[0x08 / 4];
        g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
        {
            // The callbacks; the controls (their messages); the list's frames.
            bool ok = true;
            g_msChildren[{model, "sever00"}] = row;
            g_mrMade[FRAME_DUMMY] = {MsFrame("bwgroup")};
            g_mrClones.clear();
            for (int i = 1; i < 12; ++i)
                g_mrClones.push_back(As<I3D_frame>(MsFrame(strdup(CmText("bwc%d", i).c_str()))));
            S_ScreenDesc d("multiplayer menu", "multiplayer menu", 8u << 26);
            g_gfLog.clear();
            ok &= m.DescribeBrowserScreen(&d, As<I3D_frame>(model));
            ok &= d.messages.size() == 2 && Is(CsMessage(d.messages[0]), "0 1000007 4014000 self tick") &&
                  Is(CsMessage(d.messages[1]), "0 1000007 400d000 self shown");
            std::string names;
            for (const S_ControlDesc *c : d.controls)
                names += CmText("%x %s %x|", c->type, c->name.c_str(), c->id);
            ok &= Is(names, "13 text_screen 20100000|a barrows 20200000|14 bhost name 20300000|14 bmap 20400000|"
                            "14 bstyle 20500000|14 bping 20600000|14 bplayers/max 20700000|8 scroll00 20800000|"
                            "18 GB_ServerList 20900000|14 brestore list 20a00000|14 brefresh list 20b00000|"
                            "14 benter server ip 20c00000|14 bserver filters 20d00000|14 bjoin game 20e00000|"
                            "14 bcreate server 20f00000|14 bview game info 21000000|14 bback 21100000|"
                            "13 text_h&dversion 21200000|4 gamespylogo 21300000|4 esrb-US 21400000|13 text_online 21500000|"
                            "14 badd/remove 21600000|14 bfavorites 21700000|");
            if (d.controls.size() == 23) {
                const char *const want[23] = {
                    "13 text_screen 20100000: 0 1000015 a 8fd;",
                    "a barrows 20200000: 0 1000007 4010000 self sort;",
                    "14 bhost name 20300000: 0 1000015 d 8fe 0 1000007 4001000 self sort;",
                    "14 bmap 20400000: 0 1000015 d 8ff 0 1000007 4001000 self sort;",
                    "14 bstyle 20500000: 0 1000015 d 900 0 1000007 4001000 self sort;",
                    "14 bping 20600000: 0 1000015 d 901 0 1000007 4001000 self sort;",
                    "14 bplayers/max 20700000: 0 1000015 d 902 0 1000007 4001000 self sort;",
                    "8 scroll00 20800000: 0 100000b 20900000 0;",
                    "18 GB_ServerList 20900000: 0 100000b 20800000 0 0 1000007 400e000 self picked"
                    " 0 1000007 4013000 self join 0 1000007 4010000 self count;",
                    "14 brestore list 20a00000: 0 1000015 d 915 0 1000007 4001000 self restore;",
                    "14 brefresh list 20b00000: 0 1000015 d 917 0 1000007 4001000 self refresh;",
                    "14 benter server ip 20c00000: 0 1000015 d 904 0 6001002 e 0 0 1000001 0 0;",
                    "14 bserver filters 20d00000: 0 1000015 d 905 0 6001001 b 0 0 6001028 0 0;",
                    "14 bjoin game 20e00000: 0 1000015 d 906 0 6001028 0 0 0 600109b 0 0 0 600102a 0 0 0 1000001 0 0;",
                    "14 bcreate server 20f00000: 0 1000015 d 907 0 6001001 9 0 0 6001028 0 0;",
                    "14 bview game info 21000000: 0 1000015 d 908 0 6001001 c 0 0 1000001 0 0;",
                    "14 bback 21100000: 0 1000015 d 909 0 6001028 0 0 0 600109b 0 0 20900000 6001018 0 0 0 6001003 0 0;",
                    "13 text_h&dversion 21200000: 0 1000015 28 7d6;",
                    "4 gamespylogo 21300000:;",
                    "4 esrb-US 21400000:;",
                    "13 text_online 21500000: 0 1000015 28 7d6;",
                    "14 badd/remove 21600000: 0 1000015 d 4e86 0 1000007 4001000 self favorite;",
                    "14 bfavorites 21700000: 0 1000015 d 4e84 0 1000007 4001000 self favorites;"};
                for (int i = 0; i < 23; ++i)
                    ok &= Is(CsTree(d.controls[i]), want[i]);
            } else {
                ok = false;
            }
            std::vector<std::string> order = {"bwgroup 34 GB_ServerList", "bwrow 38 bwgroup 1", "bwrow on 0", "bwrow 24",
                                              "bwgroup on 1", "bwgroup 24"};
            for (int i = 1; i < 12; ++i) {
                order.push_back("clone tree bwrow 0");
                order.push_back(CmText("bwc%d 34 Server%d", i, i));
                order.push_back(CmText("bwc%d on 0", i));
                order.push_back(CmText("bwc%d 24", i));
            }
            const std::string log = Join(g_gfLog);
            ok &= GmInOrder(log, order) && GmInOrder(log, {"bwgroup 8 (0 0 0)"}) && log.find("bwc12") == std::string::npos;
            if (!ok)
                std::printf("BROWSER log %s\n", log.c_str());
            m.m_frames.clear();
            m.m_models.clear();
            WM(r.bwDescribe, ok);
        }
        {
            // No row: the list not described (the controls after it moved up).
            bool ok = true;
            g_msChildren.erase({model, "sever00"});
            g_mrMade[FRAME_DUMMY] = {MsFrame("bwgroup2"), MsFrame("bwgroup2b")};
            S_ScreenDesc d("multiplayer menu", "multiplayer menu", 8u << 26);
            ok &= !m.DescribeServerList(&d, As<I3D_frame>(model)) && d.controls.empty();
            ok &= m.DescribeBrowserScreen(&d, As<I3D_frame>(model)) && d.controls.size() == 21 &&
                  d.controls[7]->name == "brestore list" && d.controls[7]->id == 0x20800000;
            g_msChildren[{model, "sever00"}] = row;
            // (With its row: true.)
            g_mrMade[FRAME_DUMMY] = {MsFrame("bwgroup3")};
            g_mrClones.clear();
            for (int i = 1; i < 12; ++i)
                g_mrClones.push_back(As<I3D_frame>(MsFrame(strdup(CmText("bwd%d", i).c_str()))));
            S_ScreenDesc d3("multiplayer menu", "multiplayer menu", 8u << 26);
            ok &= m.DescribeServerList(&d3, As<I3D_frame>(model)) && d3.controls.size() == 2;
            g_mrMade.erase(FRAME_DUMMY);
            m.m_frames.clear();
            m.m_models.clear();
            WM(r.bwList, ok);
        }
        g_msFrameVt[0x08 / 4] = oldSetPos;
        {
            // Shown: no single mission.
            S_PlayMode playMode;
            S_PlayMode *const oldPlayMode = g_gameProfile.m_playMode;
            g_gameProfile.m_playMode = &playMode;
            playMode.singleMission = 1;
            const bool ok = C_MainMenu::OnBrowserShown(&m, nullptr, nullptr, 0, 0) && playMode.singleMission == -1 &&
                            CsQueue() == "";
            g_gameProfile.m_playMode = oldPlayMode;
            WM(r.bwShown, ok);
        }
        // The browser (8) with the arrows, the headers, the list.
        struct C_BwScreen : C_MsScreen {
            C_BwScreen() : C_MsScreen(nullptr, "bwscreen", 8u << 26) {}
            bool HandleMessage(S_ControlMessage *message) override
            {
                if (C_Control *control = FindControl(message->target, false))
                    return control->HandleMessage(message);
                return C_MsScreen::HandleMessage(message);
            }
        } browser;
        C_BwControl arrows(0x20200000, &browser, 1), list(0x20900000, &browser, 0x42), other(0x20800000, &browser, 0);
        std::vector<C_BwControl *> headers;
        for (uint32_t i = 0; i < 5; ++i)
            headers.push_back(new C_BwControl(0x20300000 + 0x100000 * i, &browser, 0));
        browser.m_children = {&arrows, &list, &other};
        for (C_BwControl *header : headers)
            browser.m_children.push_back(header);
        m.m_screens.assign(9, nullptr);
        m.m_screens[8] = &browser;
        {
            // The arrows: the direction toggled; a header: its key; the key's own: the arrows flipped; none, another:
            // false.
            bool ok = true;
            g_gfLog.clear();
            CsQueue();
            ok &= C_MainMenu::OnServerSort(&m, nullptr, &arrows, 0, 0) && Is(CsQueue(), "20900000 2000079 c0 0; ") &&
                  MsLogIs({"bw 20900000 20000ad", "bw 20200000 100006c"}, g_gfLog);
            const char *const sorts[5] = {"20900000 2000079 1 0; ", "20200000 100006d 0 0; ", "20900000 2000079 3 0; ",
                                          "20900000 2000079 4 0; ", "20900000 2000079 5 0; "};
            for (int i = 0; i < 5; ++i)
                ok &= C_MainMenu::OnServerSort(&m, nullptr, headers[i], 0, 0) && Is(CsQueue(), sorts[i]);
            arrows.value = 0;
            ok &= C_MainMenu::OnServerSort(&m, nullptr, headers[1], 0, 0) && Is(CsQueue(), "20200000 100006d 1 0; ");
            list.value = 0x85;
            ok &= C_MainMenu::OnServerSort(&m, nullptr, headers[4], 0, 0) && Is(CsQueue(), "20200000 100006d 1 0; ");
            ok &= C_MainMenu::OnServerSort(&m, nullptr, headers[1], 0, 0) && Is(CsQueue(), "20900000 2000079 2 0; ");
            list.value = 0;
            ok &= C_MainMenu::OnServerSort(&m, nullptr, headers[0], 0, 0) && Is(CsQueue(), "20900000 2000079 1 0; ");
            g_gfLog.clear();
            ok &= !C_MainMenu::OnServerSort(&m, nullptr, &other, 0, 0) && CsQueue() == "" && g_gfLog.size() == 2;
            g_gfLog.clear();
            ok &= !C_MainMenu::OnServerSort(&m, nullptr, nullptr, 0, 0) && CsQueue() == "" && g_gfLog.empty();
            WM(r.bwSort, ok);
        }
        {
            // A server picked: kept, pinged, join and the game info on (listing the Internet's: the favourite too); none:
            // nothing.
            bool ok = true;
            S_ServerEntry server = {};
            std::strcpy(server.name, "Srv");
            list.value2 = reinterpret_cast<uintptr_t>(&server);
            const int oldListing = g_mpSettings->unknown174;
            g_mpSettings->unknown174 = 0;
            g_gfLog.clear();
            g_bwPing = nullptr;
            ok &= C_MainMenu::OnServerPicked(&m, nullptr, &list, 0, 0) && m.m_server == &server &&
                  Is(CsQueue(), "20e00000 1000002 0 0; 21000000 1000002 0 0; ") &&
                  MsLogIs({"bw 20900000 1000069", "ping Srv"}, g_gfLog) && g_bwPing == &C_MainMenu::IgnoreServer;
            g_mpSettings->unknown174 = 1;
            ok &= C_MainMenu::OnServerPicked(&m, nullptr, &list, 0, 0) &&
                  Is(CsQueue(), "20e00000 1000002 0 0; 21000000 1000002 0 0; 21600000 1000002 0 0; ");
            g_mpSettings->unknown174 = 2;
            ok &= C_MainMenu::OnServerPicked(&m, nullptr, &list, 0, 0) &&
                  Is(CsQueue(), "20e00000 1000002 0 0; 21000000 1000002 0 0; ");
            list.value2 = 0;
            g_gfLog.clear();
            ok &= C_MainMenu::OnServerPicked(&m, nullptr, &list, 0, 0) && !m.m_server && CsQueue() == "" &&
                  MsLogIs({"bw 20900000 1000069"}, g_gfLog);
            g_mpSettings->unknown174 = oldListing;
            C_MainMenu::IgnoreServer(&server);
            WM(r.bwPicked, ok);
            // Joined: the join button's messages; no server: nothing.
            ok = C_MainMenu::OnServerJoin(&m, nullptr, &list, 0, 0) && CsQueue() == "";
            m.m_server = &server;
            ok &= C_MainMenu::OnServerJoin(&m, nullptr, &list, 0, 0) &&
                  Is(CsQueue(), "0 2000028 0 0; 0 200009b 0 0; 0 200002a 0 0; ");
            m.m_server = nullptr;
            WM(r.bwJoin, ok);
        }
        {
            // The count: the list's entries, the first list's servers and their players, the text.
            bool ok = true;
            C_MpMenuSettings settings;
            m.m_mpSettings = &settings;
            S_ServerEntry a = {}, b = {};
            a.players = 3;
            b.players = 4;
            settings.m_servers = {&a, &b};
            list.value = 7;
            g_gfLog.clear();
            const std::string online = g_texts.Get(0x22AE).c_str();
            ok &= C_MainMenu::OnServerCountChanged(&m, nullptr, &list, 0, 0) &&
                  Is(CsQueue(), "21500000 1000014 9 0 '7/2 ----- 7 " + online + "'; ") &&
                  MsLogIs({"bw 20900000 10000a2"}, g_gfLog) && settings.m_serverCount == 2 && settings.m_playerCount == 7;
            settings.m_servers.clear();
            list.value = 0;
            ok &= C_MainMenu::OnServerCountChanged(&m, nullptr, &list, 0, 0) &&
                  Is(CsQueue(), "21500000 1000014 9 0 '0/0 ----- 0 " + online + "'; ");
            m.m_mpSettings = nullptr;
            WM(r.bwCount, ok);
        }
        for (C_BwControl *header : headers)
            delete header;
        browser.m_children.clear();
        m.m_screens.clear();
        g_gfLog.clear();
    }

    // --- (part 9h-xx) the browser's servers: their lines (filtered), found, listed; the tick; the searches; the
    // favourites ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("BROWSER2 %s\nBROWSER2 expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        InitializeCriticalSection(&m.m_foundLock);
        // (The servers' addresses printed, not names; the deletes watched.)
        const auto oldNames = g_csNames;
        g_csNames.clear();
        g_deleteHook = &MmDeleteHook;
        C_MpMenuSettings settings;
        for (auto &group : settings.m_setup.maps) {
            for (C_MpMap *map : group.maps)
                delete map;
            group.maps.clear();
        }
        m.m_mpSettings = &settings;
        struct C_BxScreen : C_MsScreen {
            C_BxScreen() : C_MsScreen(nullptr, "bxscreen", 8u << 26) {}
            bool HandleMessage(S_ControlMessage *message) override
            {
                if (C_Control *control = FindControl(message->target, false))
                    return control->HandleMessage(message);
                return C_MsScreen::HandleMessage(message);
            }
        } browser;
        C_BwControl list(0x20900000, &browser, 0);
        browser.m_children = {&list};
        m.m_screens.assign(9, nullptr);
        m.m_screens[8] = &browser;
        const int oldListing = g_mpSettings->unknown174;
        const auto line = [](const S_ServerEntry *server) {
            return CmText("20900000 1000067 0 %x; ", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(server)));
        };
        const auto entry = [](uint32_t address, uint32_t ping) {
            S_ServerEntry server = {};
            server.address = address;
            server.ping = ping;
            server.maxPlayers = 8;
            server.players = 4;
            server.gameType = 1;
            server.dedicated = true;
            server.versionMajor = g_versionMajor;
            server.versionMinor = g_versionMinor;
            return server;
        };
        CsQueue();
        {
            // A line: the servers found filtered (each filter alone), the favourites not.
            bool ok = true;
            S_ServerEntry s = entry(1, 100);
            S_ServerFilter &f = settings.m_filter;
            const S_ServerFilter none = f;
            const auto posted = [&]() {
                m.AddServerLine(&s);
                return CsQueue() == line(&s);
            };
            const auto dropped = [&]() {
                m.AddServerLine(&s);
                return CsQueue() == "";
            };
            ok &= posted();
            f.hideUnreachable = true;
            s.ping = 9999;
            ok &= dropped();
            s.ping = 9998;
            ok &= posted();
            f = none;
            s.ping = 9999;
            ok &= posted();
            f.maxPing = 100;
            s.ping = 100;
            ok &= posted();
            s.ping = 101;
            ok &= dropped();
            f.maxPing = 0;
            ok &= posted();
            f.maxPing = 1;
            s.ping = 2;
            ok &= dropped();
            s.ping = 1;
            ok &= posted();
            f = none;
            f.gameType = 2;
            ok &= dropped();
            f.gameType = 1;
            ok &= posted();
            f = none;
            f.hideEmpty = true;
            ok &= posted();
            s.players = 0;
            ok &= dropped();
            f = none;
            f.hideFull = true;
            s.players = 8;
            ok &= dropped();
            s.players = 7;
            ok &= posted();
            f = none;
            f.dedicatedOnly = true;
            ok &= posted();
            s.dedicated = false;
            ok &= dropped();
            f = none;
            f.noPassword = true;
            ok &= posted();
            s.password = true;
            ok &= dropped();
            f = none;
            f.sameVersion = true;
            ok &= posted();
            s.versionMajor = g_versionMajor + 1;
            ok &= dropped();
            s.versionMajor = g_versionMajor;
            s.versionMinor = g_versionMinor + 1;
            ok &= dropped();
            // The favourites: everything filtered, still listed.
            f.hideUnreachable = f.hideEmpty = f.dedicatedOnly = f.noPassword = true;
            f.maxPing = 1;
            f.gameType = 3;
            s.players = 0;
            s.ping = 9999;
            settings.m_listKind = 2;
            ok &= posted();
            settings.m_listKind = 1;
            f = none;
            WM(r.bxLine, ok);
        }
        {
            // Found (the net thread's): kept while the main menu runs and no password was asked.
            bool ok = true;
            C_MenuState *const oldState = g_menuState;
            const bool oldSet = g_sessionPasswordSet;
            S_ServerEntry s = {};
            g_menuState = &m;
            g_sessionPasswordSet = false;
            C_MainMenu::OnServerFound(&s);
            ok &= m.m_foundServers.size() == 1 && m.m_foundServers[0] == &s;
            g_sessionPasswordSet = true;
            C_MainMenu::OnServerFound(&s);
            g_sessionPasswordSet = false;
            C_GameState plain;
            g_menuState = reinterpret_cast<C_MenuState *>(&plain);
            C_MainMenu::OnServerFound(&s);
            g_menuState = nullptr;
            C_MainMenu::OnServerFound(&s);
            ok &= m.m_foundServers.size() == 1;
            m.AddFoundServer(&s);
            ok &= m.m_foundServers.size() == 2 && CsQueue() == "";
            m.m_foundServers.clear();
            g_menuState = oldState;
            g_sessionPasswordSet = oldSet;
            WM(r.bxFound, ok);
        }
        {
            // Listed: searching a LAN (an address's other answers dropped unless listed; the new ones listed);
            // pinging (a listed one answering: the list shown again).
            bool ok = true;
            S_ServerEntry known = entry(1, 10), fresh = entry(2, 20), stranger = entry(3, 30);
            auto *twin = new S_ServerEntry(entry(1, 40));
            g_mmWatch = {twin};
            g_mmDeleted.clear();
            settings.m_servers = {&known};
            g_mpSettings->unknown174 = 0;
            m.m_step = 5;
            m.m_searchTime = 500;
            m.m_foundServers = {twin, &fresh, &known};
            m.ListFoundServers();
            ok &= m.m_foundServers.empty() && g_mmDeleted == std::vector<void *>({twin}) &&
                  settings.m_servers == std::vector<S_ServerEntry *>({&known, &fresh}) && m.m_searchTime == 0 &&
                  Is(CsQueue(), line(&fresh));
            g_mmWatch.clear();
            g_mmDeleted.clear();
            // Internet games: no address check; pinging: listed ones answering.
            g_mpSettings->unknown174 = 1;
            m.m_step = 4;
            m.m_searchTime = 500;
            m.m_foundServers = {&stranger, &fresh};
            m.ListFoundServers();
            ok &= m.m_foundServers.empty() && m.m_searchTime == 500 && settings.m_servers.size() == 2 &&
                  Is(CsQueue(), "20900000 200002c 0 0; ");
            m.m_foundServers = {&stranger};
            m.ListFoundServers();
            ok &= CsQueue() == "";
            // Searching the Internet's: a listed one not again; an address twice (not LAN) listed.
            m.m_step = 5;
            S_ServerEntry twin2 = entry(1, 50);
            m.m_foundServers = {&known, &twin2};
            m.ListFoundServers();
            ok &= settings.m_servers.size() == 3 && settings.m_servers[2] == &twin2 && Is(CsQueue(), line(&twin2));
            settings.m_servers.clear();
            // The favourites shown: their addresses checked, the new ones added to them.
            S_ServerEntry five = entry(5, 5), six = entry(6, 6);
            auto *twin5 = new S_ServerEntry(entry(5, 1));
            g_mmWatch = {twin5};
            g_mmDeleted.clear();
            settings.m_listKind = 2;
            settings.m_servers2 = {&five};
            g_mpSettings->unknown174 = 0;
            m.m_foundServers = {&six, twin5};
            m.ListFoundServers();
            ok &= g_mmDeleted == std::vector<void *>({twin5}) &&
                  settings.m_servers2 == std::vector<S_ServerEntry *>({&five, &six}) && settings.m_servers.empty() &&
                  Is(CsQueue(), line(&six));
            g_mmWatch.clear();
            g_mmDeleted.clear();
            settings.m_servers2.clear();
            settings.m_listKind = 1;
            m.m_step = 0;
            WM(r.bxList, ok);
        }
        {
            // The tick: other states nothing; the list again; the reachable ones listed once; the searches stopped
            // after 15 s quiet; sorted every second; the network polled every 10 ms.
            bool ok = true;
            S_ServerEntry a = entry(1, 100), b = entry(2, 9999), c = entry(3, 50);
            // (The poll's time at first.)
            ok &= g_netPollTime == 15;
            // The favourites shown: theirs (again, the reachable ones).
            S_ServerEntry f1 = entry(7, 10);
            settings.m_listKind = 2;
            settings.m_servers = {&a};
            settings.m_servers2 = {&f1};
            m.m_step = 6;
            g_gfLog.clear();
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 0) && Is(CsQueue(), line(&f1));
            m.m_step = 9;
            m.m_searchTime = 5;
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 0) && f1.listed && !a.listed &&
                  Is(CsQueue(), line(&f1));
            settings.m_listKind = 1;
            settings.m_servers2.clear();
            // Pinging: the servers found listed (a listed one answering: shown again).
            m.m_step = 4;
            m.m_searchTime = 0;
            m.m_sortTime = 0;
            g_mpSettings->unknown174 = 1;
            m.m_foundServers = {&a};
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 0) && m.m_foundServers.empty() &&
                  Is(CsQueue(), "20900000 200002c 0 0; ");
            settings.m_servers = {&a, &b, &c};
            g_gfLog.clear();
            g_netPollTime = 5;
            m.m_step = 0;
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 30) && CsQueue() == "" && g_gfLog.empty() &&
                  g_netPollTime == 5;
            m.m_step = 6;
            m.m_searchTime = 3;
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 30) && m.m_step == 0 && m.m_searchTime == 3 &&
                  Is(CsQueue(), line(&a) + line(&b) + line(&c)) && MsLogIs({"bw 20900000 2000018", "bw 20900000 200002c"}, g_gfLog) &&
                  g_netPollTime == 5;
            // Pinging the reachable: a listed (c already), the search's time again, the list refreshed.
            c.listed = true;
            m.m_step = 9;
            m.m_searchTime = 700;
            m.m_sortTime = 0;
            g_gfLog.clear();
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 4) && a.listed && !b.listed && m.m_step == 9 &&
                  m.m_searchTime == 4 && m.m_sortTime == 4 && g_netPollTime == 9 && Is(CsQueue(), line(&a)) &&
                  MsLogIs({"bw 20900000 200002c"}, g_gfLog);
            g_gfLog.clear();
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 2) && m.m_searchTime == 6 && CsQueue() == "" &&
                  MsLogIs({"net poll"}, g_gfLog) && g_netPollTime == 0;
            // Nothing new with some time: no refresh.
            b.ping = 10;
            b.listed = true;
            g_gfLog.clear();
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 10) && m.m_searchTime == 16 && CsQueue() == "" &&
                  g_gfLog.empty() && g_netPollTime == 10;
            // 15 s quiet: stopped (pinging: its button's; searching: the LAN's); sorted every second.
            g_mpSettings->unknown174 = 0;
            m.m_searchTime = 14990;
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 10) && m.m_searchTime == 15000 && m.m_step == 9 &&
                  CsQueue() == "";
            g_netPollTime = 0;
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 1) && m.m_step == 0 &&
                  Is(CsQueue(), "0 2000028 0 0; 20900000 2000079 0 0; 20a00000 1000015 d 915; 20b00000 1000002 0 0; ");
            m.m_step = 4;
            m.m_searchTime = 15000;
            m.m_sortTime = 1000;
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 1) && m.m_step == 0 && m.m_sortTime == 0 &&
                  Is(CsQueue(), "0 2000028 0 0; 20900000 2000079 0 0; 20a00000 1000015 d 915; 20b00000 1000002 0 0; "
                                "20900000 2000079 0 0; ");
            m.m_step = 5;
            m.m_searchTime = 15000;
            m.m_sortTime = 999;
            ok &= C_MainMenu::OnBrowserTick(&m, nullptr, nullptr, 0, 1) && m.m_step == 0 && m.m_sortTime == 1000 &&
                  Is(CsQueue(), "0 2000028 0 0; 20900000 2000079 0 0; 20b00000 1000015 d 917; ");
            settings.m_servers.clear();
            g_netPollTime = 15;
            WM(r.bxTick, ok);
        }
        {
            // "REFRESH LIST": started (the list's servers reset; the unreachable hidden: emptied), stopped.
            bool ok = true;
            S_ServerEntry a = entry(1, 100), s = entry(2, 20);
            std::strcpy(a.map, "Map");
            a.listed = true;
            settings.m_servers = {&a};
            m.m_server = &s;
            m.m_step = 0;
            m.m_searchTime = 77;
            const std::string start = "20e00000 1000001 0 0; 21000000 1000001 0 0; 21600000 1000001 0 0; 20900000 200002c 0 0; "
                                      "20b00000 1000001 0 0; 20a00000 1000015 d 916; 0 2000098 0 0; ";
            ok &= C_MainMenu::OnRestoreList(&m, nullptr, nullptr, 0, 0) && m.m_step == 4 && !m.m_server &&
                  m.m_searchTime == 0 && Is(CsQueue(), start) && !a.listed && a.ping == 9999 && a.gameType == 4 &&
                  a.players == 0 && a.maxPlayers == 0 && !a.dedicated && std::strcmp(a.map, " ??? ") == 0 &&
                  a.address == 1;
            ok &= C_MainMenu::OnRestoreList(&m, nullptr, nullptr, 0, 0) && m.m_step == 0 &&
                  Is(CsQueue(), "0 2000028 0 0; 20900000 2000079 0 0; 20a00000 1000015 d 915; 20b00000 1000002 0 0; ");
            settings.m_filter.hideUnreachable = true;
            ok &= C_MainMenu::OnRestoreList(&m, nullptr, nullptr, 0, 0) && m.m_step == 9 &&
                  Is(CsQueue(), "20900000 2000018 0 0; " + start);
            settings.m_listKind = 2;
            ok &= C_MainMenu::OnRestoreList(&m, nullptr, nullptr, 0, 0) && m.m_step == 0 &&
                  Is(CsQueue(), "0 2000028 0 0; 20900000 2000079 0 0; 20a00000 1000015 d 915; ");
            settings.m_filter.hideUnreachable = false;
            // The favourites' servers reset with them shown.
            S_ServerEntry f = entry(5, 5);
            settings.m_servers2 = {&f};
            a.ping = 1;
            ok &= C_MainMenu::OnRestoreList(&m, nullptr, nullptr, 0, 0) && f.ping == 9999 && a.ping == 1;
            CsQueue();
            m.m_step = 5;
            ok &= C_MainMenu::OnRestoreList(&m, nullptr, nullptr, 0, 0) && m.m_step == 5 && CsQueue() == "";
            settings.m_listKind = 1;
            settings.m_servers.clear();
            settings.m_servers2.clear();
            m.m_step = 0;
            WM(r.bxRestore, ok);
        }
        {
            // "UPDATE LIST": started (the servers found and listed dropped), stopped (Internet games: the ping's on).
            bool ok = true;
            auto *found = new S_ServerEntry(entry(1, 1)), *listed = new S_ServerEntry(entry(2, 2));
            S_ServerEntry s = entry(3, 3);
            g_mmWatch = {found, listed};
            g_mmDeleted.clear();
            m.m_foundServers = {found};
            settings.m_servers = {listed};
            m.m_server = &s;
            m.m_searchTime = 9;
            ok &= C_MainMenu::OnRefreshList(&m, nullptr, nullptr, 0, 0) && m.m_step == 5 && !m.m_server &&
                  m.m_searchTime == 0 && m.m_foundServers.empty() && settings.m_servers.empty() &&
                  g_mmDeleted == std::vector<void *>({found, listed}) &&
                  Is(CsQueue(), "20e00000 1000001 0 0; 21000000 1000001 0 0; 21600000 1000001 0 0; 20900000 2000018 0 0; "
                                "20a00000 1000001 0 0; 20b00000 1000015 d 918; 0 2000027 0 0; ");
            g_mmWatch.clear();
            g_mmDeleted.clear();
            g_mpSettings->unknown174 = 1;
            ok &= C_MainMenu::OnRefreshList(&m, nullptr, nullptr, 0, 0) && m.m_step == 0 &&
                  Is(CsQueue(), "0 2000028 0 0; 20900000 2000079 0 0; 20b00000 1000015 d 917; 20a00000 1000002 0 0; ");
            m.m_step = 5;
            g_mpSettings->unknown174 = 2;
            ok &= C_MainMenu::OnRefreshList(&m, nullptr, nullptr, 0, 0) && m.m_step == 0 &&
                  Is(CsQueue(), "0 2000028 0 0; 20900000 2000079 0 0; 20b00000 1000015 d 917; ");
            m.m_step = 4;
            ok &= C_MainMenu::OnRefreshList(&m, nullptr, nullptr, 0, 0) && m.m_step == 4 && CsQueue() == "";
            m.m_step = 0;
            WM(r.bxRefresh, ok);
        }
        {
            // A favourite: none picked: false; the servers found: copied; the favourites: removed, the list again.
            bool ok = true;
            m.m_server = nullptr;
            ok &= !C_MainMenu::OnFavoriteToggle(&m, nullptr, nullptr, 0, 0) && CsQueue() == "";
            S_ServerEntry s = entry(7, 70);
            std::strcpy(s.name, "Fav");
            s.listed = true;
            m.m_server = &s;
            ok &= C_MainMenu::OnFavoriteToggle(&m, nullptr, nullptr, 0, 0) && m.m_step == 0 && CsQueue() == "" &&
                  settings.m_servers2.size() == 1 && settings.m_servers2[0] != &s &&
                  std::memcmp(settings.m_servers2[0], &s, sizeof s) == 0 && settings.m_servers.empty();
            S_ServerEntry *copy = settings.m_servers2[0];
            S_ServerEntry other = entry(8, 80);
            settings.m_servers2.push_back(&other);
            settings.m_listKind = 2;
            g_mmWatch = {copy};
            g_mmDeleted.clear();
            m.m_server = copy;
            ok &= C_MainMenu::OnFavoriteToggle(&m, nullptr, nullptr, 0, 0) && m.m_step == 6 && !m.m_server &&
                  g_mmDeleted == std::vector<void *>({copy}) &&
                  settings.m_servers2 == std::vector<S_ServerEntry *>({&other}) && Is(CsQueue(), "21600000 1000001 0 0; ");
            g_mmWatch.clear();
            g_mmDeleted.clear();
            // Not among them: nothing removed (still picked).
            m.m_step = 0;
            m.m_server = &s;
            ok &= C_MainMenu::OnFavoriteToggle(&m, nullptr, nullptr, 0, 0) && m.m_step == 6 && m.m_server == &s &&
                  settings.m_servers2.size() == 1 && Is(CsQueue(), "21600000 1000001 0 0; ");
            // Twice: the last removed.
            auto *twice = new S_ServerEntry(entry(9, 9));
            settings.m_servers2 = {twice, &other, twice};
            m.m_server = twice;
            ok &= C_MainMenu::OnFavoriteToggle(&m, nullptr, nullptr, 0, 0) &&
                  settings.m_servers2 == std::vector<S_ServerEntry *>({twice, &other}) && Is(CsQueue(), "21600000 1000001 0 0; ");
            settings.m_servers2 = {&other};
            m.m_server = &s;
            m.m_step = 0;
            settings.m_listKind = 0;
            ok &= C_MainMenu::OnFavoriteToggle(&m, nullptr, nullptr, 0, 0) && m.m_step == 0 && CsQueue() == "" &&
                  settings.m_servers2.size() == 1;
            settings.m_listKind = 1;
            settings.m_servers2.clear();
            WM(r.bxFavorite, ok);
        }
        {
            // The favourites shown / the servers found again: nothing picked, the searches stopped, the buttons, the
            // texts, the list again; another kind: nothing.
            bool ok = true;
            S_ServerEntry s = entry(1, 1);
            m.m_server = &s;
            g_gfLog.clear();
            ok &= C_MainMenu::OnFavorites(&m, nullptr, nullptr, 0, 0) && settings.m_listKind == 2 && !m.m_server &&
                  m.m_step == 6 && std::find(g_gfLog.begin(), g_gfLog.end(), "net close 1") != g_gfLog.end() &&
                  Is(CsQueue(), "20b00000 1000001 0 0; 20d00000 1000001 0 0; 21600000 1000001 0 0; 21700000 1000015 d 4e85; "
                                "21600000 1000015 d 4e87; ");
            m.m_server = &s;
            m.m_step = 0;
            ok &= C_MainMenu::OnFavorites(&m, nullptr, nullptr, 0, 0) && settings.m_listKind == 1 && !m.m_server &&
                  m.m_step == 6 &&
                  Is(CsQueue(), "20b00000 1000002 0 0; 20d00000 1000002 0 0; 21600000 1000001 0 0; 21700000 1000015 d 4e84; "
                                "21600000 1000015 d 4e86; ");
            m.m_server = &s;
            m.m_step = 0;
            settings.m_listKind = 3;
            g_gfLog.clear();
            ok &= C_MainMenu::OnFavorites(&m, nullptr, nullptr, 0, 0) && settings.m_listKind == 3 && m.m_server == &s &&
                  m.m_step == 0 && CsQueue() == "" && g_gfLog.empty();
            settings.m_listKind = 1;
            m.m_server = nullptr;
            WM(r.bxFavorites, ok);
        }
        g_mpSettings->unknown174 = oldListing;
        m.m_mpSettings = nullptr;
        browser.m_children.clear();
        m.m_screens.clear();
        DeleteCriticalSection(&m.m_foundLock);
        g_deleteHook = nullptr;
        g_csNames = oldNames;
        g_gfLog.clear();
        CsQueue();
    }

    // --- (part 9h-xxi) the network handler: LAN / Internet games, the server started, leaving the browser, start, the
    // firewall, connecting, joining, the searches, stopped; the join's password ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("NETWORK %s\nNETWORK expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        InitializeCriticalSection(&m.m_foundLock);
        C_MpMenuSettings settings;
        for (auto &group : settings.m_setup.maps) {
            for (C_MpMap *map : group.maps)
                delete map;
            group.maps.clear();
        }
        m.m_mpSettings = &settings;
        S_MultiplayerSettings *session = g_mpSettings;
        const int oldListing = session->unknown174, oldType = session->sessionType;
        const bool oldSet = g_sessionPasswordSet;
        S_ControlMessage message;
        const auto send = [&](uint32_t id) {
            message.id = id;
            g_gfLog.clear();
            return m.OnNetworkMessage(&message);
        };
        CsQueue();
        {
            // LAN games: the settings read, the LAN searched, the browser opened; Internet games: the server list read,
            // shown again.
            bool ok = true;
            session->unknown174 = 1;
            m.m_step = 4;
            g_files.clear();
            settings.m_filter.maxPing = 5;
            ok &= send(0x4000000 | 0x2000099) && settings.m_filter.maxPing == -1 && session->unknown174 == 0 && m.m_step == 5 &&
                  Is(CsQueue(), "20e00000 1000001 0 0; 21000000 1000001 0 0; 21600000 1000001 0 0; 20900000 2000018 0 0; "
                                "20a00000 1000001 0 0; 20b00000 1000015 d 918; 0 2000027 0 0; 20100000 1000015 a 910; "
                                "20a00000 1000001 0 0; 21300000 1000004 0 0; 20c00000 1000001 0 0; 21700000 1000001 0 0; "
                                "21600000 1000001 0 0; 0 2000001 8 0; ");
            WM(r.nmLan, ok);
            // (Part 9h-xxii: the server list read: no file, both lists emptied.)
            settings.m_servers = {new S_ServerEntry()};
            g_files.clear();
            settings.m_filter.maxPing = 5;
            ok = send(0x200009A) && session->unknown174 == 1 && m.m_step == 6 && settings.m_servers.empty() &&
                 settings.m_filter.maxPing == -1 &&
                 Is(CsQueue(), "20100000 1000015 a 911; 20a00000 1000002 0 0; 21300000 1000003 0 0; 20c00000 1000002 0 0; "
                               "21700000 1000002 0 0; 21600000 1000001 0 0; 0 2000001 8 0; ");
            m.m_step = 0;
            WM(r.nmInternet, ok);
        }
        {
            // The server started: the next map. Leaving the browser: the Internet's servers saved.
            bool ok = true;
            const int state = m.m_state;
            ok &= send(0x2000069) && m.m_state == GAME_STATE_NEXT_MAP && CsQueue() == "";
            m.m_state = state;
            session->unknown174 = 1;
            g_files.clear();
            ok &= send(0x200009B) && g_files.size() == 1;
            g_files.clear();
            session->unknown174 = 0;
            ok &= send(0x200009B) && g_files.empty() && CsQueue() == "";
            WM(r.nmLeave, ok);
        }
        {
            // Start: not startable: nothing; else the settings saved, the session set up, the firewall asked about
            // (closed: the question; its answers), the connection started.
            bool ok = true;
            const S_MultiplayerSettings old = *session;
            session->maps.clear();
            m.m_screens.clear();
            m.m_startable = false;
            ok &= send(0x2000029) && g_gfLog.empty() && CsQueue() == "" && session->sessionType == old.sessionType;
            m.m_startable = true;
            m.m_openFirewall = true;
            g_nmFirewall = false;
            g_files.clear();
            ok &= send(0x2000029) && !m.m_startable && !m.m_openFirewall && session->sessionType == 2 &&
                  GmInOrder(Join(g_gfLog), {"firewall 65535"}) && Is(CsQueue(), "0 20000c9 0 0; ") &&
                  g_files.size() == 1 && g_files.begin()->first.find("MPSetings.sav") != std::string::npos;
            g_files.clear();
            m.m_startable = true;
            g_nmFirewall = true;
            ok &= send(0x2000029) && !m.m_startable && GmInOrder(Join(g_gfLog), {"firewall 65535"}) && CsQueue() == "";
            g_nmFirewall = false;
            ok &= send(0x20000CA) && m.m_openFirewall && Is(CsQueue(), "0 20000c9 0 0; ");
            ok &= send(0x20000CB) && !m.m_openFirewall && Is(CsQueue(), "0 20000c9 0 0; ");
            m.m_connectState = 5;
            ok &= send(0x20000C9) && m.m_connectState == 0 && Is(CsQueue(), "0 2000002 d 0; ");
            session->Reset();
            const std::vector<C_MpMap *> none;
            *session = old;
            session->maps = none;
            WM(r.nmStart, ok);
        }
        {
            // Joining: none picked: nothing; the server kept as the session's (its password asked for first).
            bool ok = true;
            m.m_server = nullptr;
            session->sessionType = 0;
            ok &= send(0x200002A) && session->sessionType == 0 && CsQueue() == "";
            S_ServerEntry server = {};
            std::strcpy(server.name, "Joined");
            server.address = 0x01020304;
            server.port = 2345;
            m.m_server = &server;
            m.m_connectState = 5;
            g_sessionPasswordSet = false;
            ok &= send(0x200002A) && session->sessionType == 1 && g_sessionPasswordSet && m.m_connectState == 0 &&
                  std::memcmp(session->unknown17C, &server, sizeof server) == 0 && Is(CsQueue(), "0 2000002 d 0; ");
            server.password = true;
            g_sessionPasswordSet = false;
            m.m_connectState = 5;
            m.m_step = 0;
            ok &= send(0x200002A) && !g_sessionPasswordSet && m.m_connectState == 5 && m.m_step == 7 &&
                  std::memcmp(session->unknown17C, &server, sizeof server) == 0 &&
                  Is(CsQueue(), "28200000 1000015 e 7d2; 0 2000002 a 0; 28200000 2000016 0 0; ");
            m.m_server = nullptr;
            m.m_step = 0;
            WM(r.nmJoin, ok);
        }
        {
            // The LAN searched, the list pinged (the servers found dropped first); stopped: the searches' buttons, or
            // the network closed.
            bool ok = true;
            auto *found = new S_ServerEntry();
            g_mmWatch = {found};
            g_mmDeleted.clear();
            g_deleteHook = &MmDeleteHook;
            m.m_foundServers = {found};
            ok &= send(0x2000027) && m.m_foundServers.empty() && g_mmDeleted.size() == 1 && MsLogIs({"search 1"}, g_gfLog);
            S_ServerEntry a = {}, b = {};
            settings.m_servers = {&a, &b};
            auto *found2 = new S_ServerEntry();
            g_mmWatch = {found2};
            g_mmDeleted.clear();
            m.m_foundServers = {found2};
            ok &= send(0x2000098) && MsLogIs({"ping servers 1 2"}, g_gfLog) && g_mmDeleted.size() == 1 &&
                  m.m_foundServers.empty();
            settings.m_listKind = 2;
            settings.m_servers2 = {&a};
            ok &= send(0x2000098) && MsLogIs({"ping servers 1 1"}, g_gfLog);
            settings.m_listKind = 1;
            settings.m_servers2.clear();
            settings.m_servers.clear();
            g_deleteHook = nullptr;
            g_mmWatch.clear();
            g_mmDeleted.clear();
            CsQueue();
            m.m_step = 4;
            ok &= send(0x2000028) && m.m_step == 0 && CsQueue().find("20a00000 1000015 d 915") != std::string::npos;
            m.m_step = 9;
            ok &= send(0x2000028) && m.m_step == 0 && CsQueue().find("20a00000 1000015 d 915") != std::string::npos;
            m.m_step = 5;
            ok &= send(0x2000028) && m.m_step == 0 && CsQueue().find("20b00000 1000015 d 917") != std::string::npos;
            g_sessionPasswordSet = true;
            m.m_step = 6;
            ok &= send(0x2000028) && m.m_step == 6 && !g_sessionPasswordSet && MsLogIs({"net close 1"}, g_gfLog) &&
                  CsQueue() == "";
            m.m_step = 0;
            WM(r.nmSearch, ok);
        }
        {
            // Escape: on the browser only (no screens, another: not taken); another message: not taken.
            bool ok = true;
            C_MsScreen browser(nullptr, "nmbrowser", 8u << 26 | 0x5), other(nullptr, "nmother", 9u << 26);
            g_menuScreens = {&other, &browser};
            ok &= send(MENU_MSG_ESCAPE) &&
                  Is(CsQueue(), "0 2000028 0 0; 0 200009b 0 0; 20900000 2000018 0 0; 0 2000003 0 0; ");
            g_menuScreens = {&browser, &other};
            ok &= !send(MENU_MSG_ESCAPE) && CsQueue() == "";
            g_menuScreens.clear();
            ok &= !send(MENU_MSG_ESCAPE) && CsQueue() == "";
            ok &= !send(0x2000050) && !send(0x2000003) && CsQueue() == "";
            WM(r.nmEscape, ok);
        }
        session->unknown174 = oldListing;
        session->sessionType = oldType;
        g_sessionPasswordSet = oldSet;
        m.m_mpSettings = nullptr;
        DeleteCriticalSection(&m.m_foundLock);
        g_gfLog.clear();
        CsQueue();
    }

    // --- (part 9h-xxii) the server list's file: written, read back (the old servers deleted), its odd chunks ---
    {
        C_MpMenuSettings settings;
        for (auto &group : settings.m_setup.maps) {
            for (C_MpMap *map : group.maps)
                delete map;
            group.maps.clear();
        }
        const auto entry = [](const char *name, uint32_t address) {
            auto *server = new S_ServerEntry();
            std::strcpy(server->name, name);
            std::strcpy(server->map, "Map");
            server->address = address;
            server->port = 1000 + static_cast<int>(address);
            server->gameType = 2;
            server->ping = 77;
            server->maxPlayers = 8;
            server->players = 3;
            server->dedicated = true;
            server->internet = true;
            server->password = address & 1;
            server->unknown84 = 0x1234;
            server->voice = true;
            server->versionMinor = 5;
            server->versionMajor = 6;
            server->listed = true;
            server->unknown94 = 0x5678;
            return server;
        };
        // (Equal but for the bytes past the strings' ends: a read leaves the reset's there.)
        const auto equal = [](const S_ServerEntry *a, const S_ServerEntry *b) {
            S_ServerEntry x = *a, y = *b;
            for (S_ServerEntry *e : {&x, &y}) {
                std::memset(e->name + std::strlen(e->name), 0, sizeof e->name - std::strlen(e->name));
                std::memset(e->map + std::strlen(e->map), 0, sizeof e->map - std::strlen(e->map));
            }
            return std::memcmp(&x, &y, sizeof x) == 0;
        };
        const auto same = [&](const std::vector<S_ServerEntry *> &a, const std::vector<S_ServerEntry> &b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (!equal(a[i], &b[i]))
                    return false;
            }
            return true;
        };
        const auto clearLists = [&]() {
            for (auto *list : {&settings.m_servers, &settings.m_servers2}) {
                for (S_ServerEntry *server : *list)
                    delete server;
                list->clear();
            }
        };
        {
            // Written and read back: both lists, every field; the old servers deleted.
            bool ok = true;
            std::vector<S_ServerEntry *> found = {entry("One", 1), entry("Two", 2)}, favorites = {entry("Fav", 3)};
            settings.m_servers = found;
            settings.m_servers2 = favorites;
            g_files.clear();
            settings.SaveServerList();
            ok &= g_files.size() == 1 && g_files.begin()->first.find("ServerList.bin") != std::string::npos;
            if (!g_files.empty()) {
                const std::vector<uint8_t> &bytes = g_files.begin()->second;
                const auto count = [&](uint8_t lo, uint8_t hi) {
                    int n = 0;
                    for (size_t i = 0; i + 1 < bytes.size(); ++i)
                        n += bytes[i] == lo && bytes[i + 1] == hi;
                    return n;
                };
                ok &= count(0xD0, 0x07) == 3 && count(0x8D, 0x02) == 2 && count(0x96, 0x02) == 3;
            }
            const std::vector<S_ServerEntry> foundCopy = {*found[0], *found[1]}, favoriteCopy = {*favorites[0]};
            g_mmWatch = {found[0], found[1], favorites[0]};
            g_mmDeleted.clear();
            g_deleteHook = &MmDeleteHook;
            settings.LoadServerList();
            g_deleteHook = nullptr;
            ok &= g_mmDeleted.size() == 3 && same(settings.m_servers, foundCopy) && same(settings.m_servers2, favoriteCopy);
            g_mmWatch.clear();
            g_mmDeleted.clear();
            // Empty lists written; read: empty.
            clearLists();
            settings.SaveServerList();
            settings.m_servers = {entry("Old", 9)};
            settings.LoadServerList();
            ok &= settings.m_servers.empty() && settings.m_servers2.empty();
            // No file: both lists emptied.
            g_files.clear();
            settings.m_servers = {entry("Old", 9)};
            settings.m_servers2 = {entry("Old2", 8)};
            settings.LoadServerList();
            ok &= settings.m_servers.empty() && settings.m_servers2.empty();
            WM(r.slFile, ok);
        }
        {
            // Odd files: another root (nothing); a long name cut; the skipped chunks; an unknown chunk ends its list.
            bool ok = true;
            const auto write = [&](const std::function<void(C_chunk &)> &body) {
                g_files.clear();
                C_chunk chunk;
                chunk.Open("PlayersProfiles\\ServerList.bin", true);
                body(chunk);
                chunk.Close();
            };
            S_ServerEntry *a = entry("A", 1), *b = entry("B", 2);
            std::memset(b->name, 'x', sizeof b->name - 1);
            write([&](C_chunk &chunk) {
                chunk.Begin(0x28B);
                chunk.Begin(0x291);
                chunk.Begin(0x296);
                ok &= a->Save(chunk);
                chunk.Ascend();
                chunk.Ascend();
                chunk.Ascend();
            });
            settings.LoadServerList();
            ok &= settings.m_servers.empty() && settings.m_servers2.empty();
            write([&](C_chunk &chunk) {
                chunk.Begin(0x28A);
                chunk.Begin(0x291);
                chunk.WriteChunk(0x28D, 2);
                chunk.Begin(0x28F);
                chunk.Write(7);
                chunk.Ascend();
                chunk.Begin(0x296);
                a->Save(chunk);
                chunk.Ascend();
                chunk.Begin(0x294);
                chunk.Ascend();
                chunk.Begin(0x296);
                b->Save(chunk);
                chunk.Ascend();
                chunk.Begin(0x123);
                chunk.Ascend();
                chunk.Begin(0x296);
                a->Save(chunk);
                chunk.Ascend();
                chunk.Ascend();
                chunk.Begin(0x299);
                chunk.Ascend();
                chunk.Begin(0x295);
                chunk.Begin(0x294);
                chunk.Ascend();
                chunk.Begin(0x296);
                b->Save(chunk);
                chunk.Ascend();
                chunk.Begin(0x28F);
                chunk.Ascend();
                chunk.Begin(0x296);
                a->Save(chunk);
                chunk.Ascend();
                chunk.Ascend();
                chunk.Ascend();
            });
            settings.LoadServerList();
            const std::string cut(0x31, 'x');
            ok &= settings.m_servers.size() == 2 && settings.m_servers2.size() == 1 &&
                  std::string(settings.m_servers[0]->name) == "A" && settings.m_servers[0]->unknown94 == 0x5678 &&
                  std::string(settings.m_servers[1]->name) == cut && std::string(settings.m_servers[1]->map) == "Map" &&
                  settings.m_servers[1]->versionMajor == 6 && std::string(settings.m_servers2[0]->name) == cut;
            // An entry's other chunks skipped.
            write([&](C_chunk &chunk) {
                chunk.Begin(0x28A);
                chunk.Begin(0x295);
                chunk.Begin(0x296);
                chunk.Begin(0x7D1);
                chunk.Write(5);
                chunk.Ascend();
                a->Save(chunk);
                chunk.Ascend();
                chunk.Ascend();
                chunk.Ascend();
            });
            settings.LoadServerList();
            ok &= settings.m_servers.empty() && settings.m_servers2.size() == 1 &&
                  equal(settings.m_servers2[0], a);
            // An entry without its chunk: reset.
            write([&](C_chunk &chunk) {
                chunk.Begin(0x28A);
                chunk.Begin(0x291);
                chunk.Begin(0x296);
                chunk.Begin(0x7D1);
                chunk.Ascend();
                chunk.Ascend();
                chunk.Ascend();
                chunk.Ascend();
            });
            settings.LoadServerList();
            ok &= settings.m_servers.size() == 1 && settings.m_servers[0]->ping == 9999 &&
                  settings.m_servers[0]->gameType == 4 && std::string(settings.m_servers[0]->map) == " ??? ";
            // An entry read alone: true.
            write([&](C_chunk &chunk) {
                chunk.Begin(0x296);
                a->Save(chunk);
                chunk.Ascend();
            });
            {
                C_chunk chunk;
                uint16_t id = 0;
                S_ServerEntry alone = {};
                ok &= chunk.Open("PlayersProfiles\\ServerList.bin", false) && chunk.Descend(id) && id == 0x296 &&
                      alone.Load(chunk) && alone.port == a->port && std::string(alone.name) == "A";
                chunk.Close();
            }
            delete a;
            delete b;
            clearLists();
            g_files.clear();
            WM(r.slOdd, ok);
        }
    }

    // --- (part 9h-xxiii) the add server popup: its description; yes (a server added), no, others ---
    {
        C_MainMenu m;
        const auto Is = [](const std::string &got, const std::string &want) {
            if (got != want)
                std::printf("ADDSERVER %s\nADDSERVER expected %s\n", got.c_str(), want.c_str());
            return got == want;
        };
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "self";
        g_csNames[reinterpret_cast<uintptr_t>(&C_MainMenu::OnAddServerButton)] = "add";
        {
            // The model placed as the quit question's; the controls.
            bool ok = true;
            S_MrObj *model = MsFrame("asmodel");
            void *const oldSetPos = g_msFrameVt[0x08 / 4];
            g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
            S_ScreenDesc d("ADD SERVER", "password", 14u << 26);
            g_gfLog.clear();
            ok &= m.DescribeAddServerScreen(&d, As<I3D_frame>(model)) && MsLogIs({"asmodel 8 (0.5 -0.375 -0.5)"}, g_gfLog);
            g_msFrameVt[0x08 / 4] = oldSetPos;
            ok &= d.messages.empty() && d.controls.size() == 6;
            if (d.controls.size() == 6) {
                ok &= Is(CsTree(d.controls[0]), "13 text 38100000: 0 1000015 c 7e6;") &&
                      Is(CsTree(d.controls[1]), "3 binput01 38200000: 0 1000014 d 0 '255.255.255.255' 0 100006e f 0;") &&
                      Is(CsTree(d.controls[2]), "3 binput02 38300000: 0 1000014 d 0 '11001' 0 100006f 1 0;") &&
                      Is(CsTree(d.controls[3]), "14 byes* 38400000: 0 1000015 d 7d1 0 1000007 4001000 self add;") &&
                      Is(CsTree(d.controls[4]), "14 bno* 38500000: 0 1000015 d 7d0 0 1000007 4001000 self add;") &&
                      Is(CsTree(d.controls[5]), "3 binput 38600000: 0 1000004 0 0;");
            }
            WM(r.asDescribe, ok);
        }
        {
            // Yes: both boxes filled: a server (the address's first number its lowest byte) added to the list shown,
            // its line; closed, idle. A box empty: closed only. No: closed. Others: false.
            bool ok = true;
            C_MpMenuSettings settings;
            for (auto &group : settings.m_setup.maps) {
                for (C_MpMap *map : group.maps)
                    delete map;
                group.maps.clear();
            }
            m.m_mpSettings = &settings;
            struct C_AsScreen : C_MsScreen {
                C_AsScreen() : C_MsScreen(nullptr, "asscreen", 14u << 26) {}
                bool HandleMessage(S_ControlMessage *message) override
                {
                    if (C_Control *control = FindControl(message->target, false))
                        return control->HandleMessage(message);
                    return C_MsScreen::HandleMessage(message);
                }
            } screen;
            C_PfLine address(0x38200000, "1.2.3.4"), port(0x38300000, "2345");
            C_BwControl yes(0x38400000, &screen, 0), no(0x38500000, &screen, 0), other(0x38600000, &screen, 0);
            address.m_parent = &screen;
            port.m_parent = &screen;
            screen.m_children = {&address, &port, &yes, &no, &other};
            m.m_screens.assign(15, nullptr);
            m.m_screens[14] = &screen;
            CsQueue();
            m.m_step = 4;
            g_gfLog.clear();
            ok &= C_MainMenu::OnAddServerButton(&m, nullptr, &yes, 0, 0) && m.m_step == 0 && settings.m_servers.size() == 1 &&
                  MsLogIs({"line 38200000 200002f 0", "line 38300000 200002f 0"}, g_gfLog);
            if (settings.m_servers.size() == 1) {
                const S_ServerEntry *s = settings.m_servers[0];
                ok &= s->address == 0x04030201 && s->port == 2345 && s->gameType == 4 && s->internet && !s->dedicated &&
                      s->ping == 9999 && std::string(s->name) == " ??? " && std::string(s->map) == " ??? " &&
                      s->versionMinor == g_versionMinor && s->versionMajor == g_versionMajor && !s->listed &&
                      Is(CsQueue(), CmText("20900000 1000067 0 %x; 0 2000005 0 0; ",
                                           static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s))));
            }
            // (The last number unmasked, as the original: 1.2.3.260 is 0x04030201 again.)
            address.text = "1.2.3.260";
            settings.m_listKind = 2;
            ok &= C_MainMenu::OnAddServerButton(&m, nullptr, &yes, 0, 0) && settings.m_servers2.size() == 1 &&
                  settings.m_servers2[0]->address == 0x04030201 && CsQueue().find("0 2000005 0 0; ") != std::string::npos;
            settings.m_listKind = 1;
            address.text = "300.2.3.4";
            ok &= C_MainMenu::OnAddServerButton(&m, nullptr, &yes, 0, 0) && settings.m_servers.size() == 2 &&
                  settings.m_servers[1]->address == 0x0403022C;
            // (Not an address: its numbers 0.)
            address.text = "none";
            ok &= C_MainMenu::OnAddServerButton(&m, nullptr, &yes, 0, 0) && settings.m_servers.size() == 3 &&
                  settings.m_servers[2]->address == 0;
            address.text = "1.2.3.4";
            delete settings.m_servers.back();
            settings.m_servers.pop_back();
            CsQueue();
            port.text = "";
            m.m_step = 4;
            ok &= C_MainMenu::OnAddServerButton(&m, nullptr, &yes, 0, 0) && settings.m_servers.size() == 2 && m.m_step == 0 &&
                  Is(CsQueue(), "0 2000005 0 0; ");
            port.text = "1";
            address.text = "";
            ok &= C_MainMenu::OnAddServerButton(&m, nullptr, &yes, 0, 0) && settings.m_servers.size() == 2 &&
                  Is(CsQueue(), "0 2000005 0 0; ");
            m.m_step = 4;
            ok &= C_MainMenu::OnAddServerButton(&m, nullptr, &no, 0, 0) && m.m_step == 0 && Is(CsQueue(), "0 2000005 0 0; ");
            m.m_step = 4;
            ok &= !C_MainMenu::OnAddServerButton(&m, nullptr, &other, 0, 0) && m.m_step == 4 && CsQueue() == "";
            ok &= !C_MainMenu::OnAddServerButton(&m, nullptr, nullptr, 0, 0) && m.m_step == 4 && CsQueue() == "";
            for (auto *list : {&settings.m_servers, &settings.m_servers2}) {
                for (S_ServerEntry *server : *list)
                    delete server;
                list->clear();
            }
            screen.m_children.clear();
            m.m_screens.clear();
            m.m_mpSettings = nullptr;
            m.m_step = 0;
            WM(r.asButton, ok);
        }
        g_gfLog.clear();
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
        // (And the create server screen's seven: its panels' groups (no templates: not described further).)
        g_mrMade[FRAME_DUMMY] = {MsFrame("mmgc"), MsFrame("mmgs"), MsFrame("mmgro"), MsFrame("mmgci"),  MsFrame("mmbw"), MsFrame("mmcs1"),
                                 MsFrame("mmcs2"), MsFrame("mmcs3"), MsFrame("mmcs4"), MsFrame("mmcs5"),   MsFrame("mmcs6"),
                                 MsFrame("mmcs7")};
        g_cacheLoads.clear();
        g_menuScreens.clear();
        g_gfLog.clear();
        ok &= m.BuildScreens() &&
              g_cacheLoads == std::vector<std::string>({"main_menu", "change_profile", "singleplayer", "confirm",
                                                        "single mission 2", "options", "single mission 2",
                                                        "multiplayer menu", "create server", "password", "server filters",
                                                        "gameinfo", "confirm", "password"});
        const std::string log = Join(g_gfLog);
        // (Part 9h-xxiii: every screen's description real.)
        ok &= GmInOrder(log, {"scan"}) && log.find("describe ") == std::string::npos;
        ok &= m.m_screens.size() == 15 && S_CsPeek::Callbacks(m.m_screens[1]) == 1 && S_CsPeek::Callbacks(m.m_screens[2]) == 1 &&
              S_CsPeek::Callbacks(m.m_screens[3]) == 1 && S_CsPeek::Callbacks(m.m_screens[4]) == 0 &&
              m.m_screens[4]->GetScreenFrame() == As<I3D_frame>(models[3]) &&
              !g_menuScreens.empty() && g_menuScreens.back() == m.m_screens[1];
        // The quit question's model moved.
        // (And the password screen's, part 9h-x, the connect screen's, part 9h-xviii, the add server popup's, 9h-xxiii.)
        ok &= log.find("mmm04 8 (0.5 -0.375 -0.5)") != std::string::npos && log.find("mmm10 8 (0.5 -0.375 -0.5)") != std::string::npos &&
              log.find("mmm13 8 (0.5 -0.375 -0.5)") != std::string::npos && log.find("mmm14 8 (0.5 -0.375 -0.5)") != std::string::npos &&
              log.find(" 8 (0.5", log.find(" 8 (0.5", log.find(" 8 (0.5", log.find(" 8 (0.5", log.find(" 8 (0.5") + 1) + 1) + 1) + 1) ==
                  std::string::npos &&
              log.find("mmgro 34 GROptions") != std::string::npos && log.find("mmgci 34 GB_Cinematics") != std::string::npos;
        // (The game info screen's styles and the connect screen's of this build given back: the next build makes its own.)
        ReleaseTextStyle(g_connectStyle);
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
        g_mrMade[FRAME_DUMMY] = {MsFrame("mmgc2"), MsFrame("mmgs2"), MsFrame("mmgro2"), MsFrame("mmgci2"), MsFrame("mmbw2"), MsFrame("mmcs8"),
                                 MsFrame("mmcs9"), MsFrame("mmcs10"), MsFrame("mmcs11"), MsFrame("mmcs12"), MsFrame("mmcs13"),
                                 MsFrame("mmcs14")};
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
        ReleaseTextStyle(g_connectStyle);
        g_connectStyle = -1;
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
                " flAccept %d flHelpers %d giDescribe %d giRefresh %d giShow %d giStatus %d"
                " csDescribe %d csSetup %d csShown %d csStart %d csPicked %d csHost %d"
                " csRules %d csMapList %d csOptions %d csStyle %d csOption %d"
                " csMoves %d csDouble %d csDragStart %d csDragMove %d csDrop %d"
                " hsSession %d cnDescribe %d cnHidden %d cnTick %d cnMessage %d"
                " bwDescribe %d bwList %d bwShown %d bwSort %d bwPicked %d bwJoin %d bwCount %d"
                " bxLine %d bxFound %d bxList %d bxTick %d bxRestore %d bxRefresh %d bxFavorite %d bxFavorites %d"
                " nmLan %d nmInternet %d nmLeave %d nmStart %d nmJoin %d nmSearch %d nmEscape %d slFile %d slOdd %d asDescribe %d asButton %d\n",
                r.mmStart, r.mmStop, r.mmUpdate, r.mmDrain, r.mmMessage, r.mmEscape, r.mmSelect, r.mmBuild, r.mmDescribe,
                r.mmMainShown, r.pfDescribe, r.pfRefresh, r.pfShown, r.pfList, r.pfDifficulty, r.pfMessage,
                r.pfButtons, r.pfEscape, r.pfCreate, r.pfRename, r.spShown, r.spHover, r.spClick, r.mpLists, r.mpDescribe,
                r.mpSaveSlot, r.mpPick, r.mxCampaign, r.mxSingle, r.mxMessages, r.mxSecret, r.mxStart, r.mxOperation,
                r.ciDescribe, r.ciShown, r.ciPick, r.pwDescribe, r.pwServer, r.pwLock,
                r.flDescribe, r.flShown, r.flAccept, r.flHelpers,
                r.giDescribe, r.giRefresh, r.giShow, r.giStatus,
                r.csDescribe, r.csSetup, r.csShown, r.csStart, r.csPicked, r.csHost,
                r.csRules, r.csMapList, r.csOptions, r.csStyle, r.csOption,
                r.csMoves, r.csDouble, r.csDragStart, r.csDragMove, r.csDrop,
                r.hsSession, r.cnDescribe, r.cnHidden, r.cnTick, r.cnMessage,
                r.bwDescribe, r.bwList, r.bwShown, r.bwSort, r.bwPicked, r.bwJoin, r.bwCount,
                r.bxLine, r.bxFound, r.bxList, r.bxTick, r.bxRestore, r.bxRefresh, r.bxFavorite, r.bxFavorites,
                r.nmLan, r.nmInternet, r.nmLeave, r.nmStart, r.nmJoin, r.nmSearch, r.nmEscape, r.slFile, r.slOdd, r.asDescribe, r.asButton);
}
