// Part 9g's campaign screens (game/operation_end.cpp, ..), in the factory build, run by ControlsTest after
// EquipmentScreenTest: the fakes the earlier tests' (the logging frames and models, the screens' factory).
// Prints "campaignscreens:" with 1 per check met.
#define private public
#define protected public
#include "game/operation_end.h"
#include "game/session_menu.h"
#undef protected
#undef private

// (campaign.cpp is not linked: the operation index set by the test, logged.)
int g_csOperation;
int C_Campaign::GetOperationIndex(int campaign)
{
    GfLog("operation index %d", campaign);
    return g_csOperation;
}

// (Part 9g-ii: the network game's, the squad's and the roster's, logged.)
int g_csHost, g_csJoin;
int C_NetGame::Host(const S_NetSessionDesc &desc, bool unknown)
{
    GfLog("net host %d %s %x %x %x %x %d %d %x %x", unknown, desc.name, desc.application[0], desc.application[1],
          desc.application[2], desc.application[3], desc.unknown15C, desc.unknown15E, desc.unknownD8[0],
          desc.unknown160[7]);
    return g_csHost;
}
int C_NetGame::Connect(void *address)
{
    GfLog("net connect %d", static_cast<int>(static_cast<uint8_t *>(address) - reinterpret_cast<uint8_t *>(g_mpSettings)));
    return 0;
}
int C_NetGame::GetJoinStatus() const
{
    GfLog("net join");
    return g_csJoin;
}
void S_MissionSquad::Clear() { GfLog("squad clear"); }
bool C_HumanStore::Reload()
{
    GfLog("roster reload");
    return true;
}

namespace {

// A control's callbacks counted.
struct S_CsPeek : C_Control {
    static size_t Callbacks(C_Control *control) { return static_cast<S_CsPeek *>(control)->m_callbacks.size(); }
};

struct S_CampaignScreensResults {
    bool oeTable = true, oeDescribe = true, oeBuild = true, oeInput = true, oeSlots = true;
    bool smStart = true, smMessage = true, smConsole = true, smPassword = true, smBuild = true, smTick = true;
    bool smJoin = true, smNotify = true;
};

// A message's values ("target id value value2[ value3][ 'text']"; value2 / value3 by name when known).
std::map<uintptr_t, std::string> g_csNames;
std::string CsMessage(const S_ControlMessage &m)
{
    const auto value = [](uintptr_t v) {
        const auto it = g_csNames.find(v);
        return it != g_csNames.end() ? it->second : CmText("%x", static_cast<uint32_t>(v));
    };
    std::string s = CmText("%x %x %x ", m.target, m.id, m.value) + value(m.value2);
    if (m.value3)
        s += " " + value(m.value3);
    if (!m.text.IsEmpty())
        s += std::string(" '") + m.text.c_str() + "'";
    return s;
}
// A description's controls and their messages ("type name: id value value2, ..; ").
std::string CsDesc(const S_ScreenDesc &desc)
{
    std::string s;
    for (const S_ControlDesc *c : desc.controls) {
        s += CmText("%x %s %x:", c->type, c->name.c_str(), c->id);
        for (const S_ControlMessage &m : c->messages)
            s += " " + CsMessage(m);
        s += "; ";
    }
    return s;
}
// The queue's messages (emptied).
std::string CsQueue()
{
    std::string s;
    for (const S_ControlMessage &m : g_controlMessages)
        s += CsMessage(m) + "; ";
    g_controlMessages.clear();
    return s;
}
// A screen shown or not; answering the text asked for with its own.
struct C_CsScreen : C_MsScreen {
    C_CsScreen(I3D_scene *scene, const char *n, uint32_t id) : C_MsScreen(scene, n, id) {}
    void SetShown(bool shown) { m_shown = shown; }
    std::string text;
    bool HandleMessage(S_ControlMessage *m) override
    {
        if (m->id == CONTROL_MSG_GET_TEXT) {
            GfLog("%s get text", name);
            m->text = C_str(text.c_str());
            return true;
        }
        return C_MsScreen::HandleMessage(m);
    }
};

} // namespace

void CampaignScreensTest(I3D_scene *)
{
    S_CampaignScreensResults r;
    g_gfLog.clear();
    alignas(16) static uint8_t campaignMemory[sizeof(C_Campaign)];
    std::memset(campaignMemory, 0, sizeof campaignMemory);
    C_Campaign *const oldCampaign = g_gameProfile.m_campaign;
    auto *campaign = reinterpret_cast<C_Campaign *>(campaignMemory);
    g_gameProfile.m_campaign = campaign;
    // (The models' frames found by name, the driver's frames the test's.)
    void *const oldFind = g_mrModelVt[0x48 / 4], *const oldCreate = g_driverVtable[0x54 / 4];
    g_mrModelVt[0x48 / 4] = reinterpret_cast<void *>(&MsFind);
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&MrCreateFrame);

    // --- the operation's row: the first two campaigns' operations 0 .. 7, else the first row ---
    {
        bool ok = true;
        const auto row = [&](int current, int operation) {
            campaign->m_current = current;
            g_csOperation = operation;
            g_gfLog.clear();
            const S_OperationEnd *end = C_OperationEndMenu::GetOperationEnd();
            // (The index asked for the first two campaigns only.)
            ok &= (current == 0 || current == 1) ? MsLogIs({CmText("operation index %d", current)}, g_gfLog)
                                                 : g_gfLog.empty();
            return end;
        };
        const char *first[8] = {"f_s_tutorial", "f_s_arctic", "f_s_africa", "f_s_norway",
                                "f_s_burma", "f_s_alps", "f_s_norman", ""};
        for (int i = 0; i < 8; ++i) {
            const S_OperationEnd *end = row(0, i);
            ok &= std::string(end->model) == first[i] && end->colour == 0xFF000000 && end->bigTitle &&
                  end->unknown14 == -1;
            ok &= i == 7 ? end->titleText == 0xFFFFFFFF && end->historyText == 0xFFFFFFFF
                         : end->titleText == 17850 && end->historyText == (i == 0 ? 17760u : 17749u + i);
        }
        const char *second[4] = {"f_s_LaRochel", "f_s_libye", "f_s_sicily", "f_s_burgundy"};
        for (int i = 0; i < 4; ++i) {
            const S_OperationEnd *end = row(1, i);
            ok &= std::string(end->model) == second[i] && end->historyText == 17756u + i && end->titleText == 17850;
        }
        // (The second campaign's unused rows zero: no model.)
        for (int i = 4; i < 8; ++i) {
            const S_OperationEnd *end = row(1, i);
            ok &= !end->model && !end->bigTitle && end->titleText == 0 && end->colour == 0;
        }
        for (const auto &co : {std::pair<int, int>{0, 8}, {0, -1}, {1, 8}, {1, -2}, {2, 3}, {-1, 0}, {5, 1}}) {
            const S_OperationEnd *end = row(co.first, co.second);
            ok &= std::string(end->model) == "f_s_tutorial" && end->historyText == 17760;
        }
        ok &= std::string(C_OperationEndMenu::GetModelFile()) == "f_s_tutorial";
        campaign->m_current = 1;
        g_csOperation = 2;
        ok &= std::string(C_OperationEndMenu::GetModelFile()) == "f_s_sicily";
        WM(r.oeTable, ok);
    }

    // --- the screen's texts: where the model has their frames (looked up as visuals) ---
    {
        bool ok = true;
        C_OperationEndMenu m;
        S_MrObj *model = MrObj("oem", g_mrModelVt);
        const auto describe = [&](std::initializer_list<const char *> frames) {
            for (const char *name : {"title", "textbar", "text01"})
                g_msChildren.erase({model, name});
            for (const char *name : frames)
                g_msChildren[{model, name}] = MsFrame(name);
            S_ScreenDesc desc("Campaign_debrief", "f", 1u << SCREEN_ID_SHIFT);
            g_gfLog.clear();
            ok &= m.DescribeScreen(&desc, As<I3D_frame>(model));
            std::vector<std::string> finds;
            for (const std::string &line : g_gfLog)
                if (line.compare(0, 4, "oem ") == 0)
                    finds.push_back(line);
            ok &= MsLogIs({"oem 48 title 1", "oem 48 textbar 1", "oem 48 text01 1"}, finds);
            return CsDesc(desc);
        };
        campaign->m_current = 0;
        g_csOperation = 3;
        const std::string all = describe({"title", "textbar", "text01"});
        ok &= all == "13 title 4100000: 0 1000015 59 45ba 0 10000a3 ff000000 0; "
                     "13 textbar 4200000: 0 1000015 d 7f8; "
                     "12 text01 4300000: 0 1000015 3 4558 0 10000a3 ff000000 0; ";
        ok &= describe({}) == "";
        ok &= describe({"textbar"}) == "13 textbar 4100000: 0 1000015 d 7f8; ";
        ok &= describe({"text01", "title"}) == "13 title 4100000: 0 1000015 59 45ba 0 10000a3 ff000000 0; "
                                                "12 text01 4200000: 0 1000015 3 4558 0 10000a3 ff000000 0; ";
        // (The second campaign's unused rows: the title's style 0xA, the zero texts.)
        campaign->m_current = 1;
        g_csOperation = 6;
        ok &= describe({"title", "text01"}) == "13 title 4100000: 0 1000015 a 0 0 10000a3 0 0; "
                                               "12 text01 4200000: 0 1000015 3 0 0 10000a3 0 0; ";
        for (const char *name : {"title", "textbar", "text01"})
            g_msChildren.erase({model, name});
        WM(r.oeDescribe, ok);
    }

    // --- built: the model of the operation's row loaded ("Campaign_debrief"), its screen made and
    // opened; no model in the row, the model not loaded: false ---
    {
        bool ok = true;
        void (*const oldFill)(I3D_model *) = g_cacheFill;
        g_cacheFill = [](I3D_model *) {};
        {
            C_OperationEndMenu m;
            S_MrWorld world = MrWorld("oeb");
            m.m_scene = As<I3D_scene>(world.scene);
            C_MsScreen box(m.m_scene, "oebbox", 0);
            m.m_screens = {&box};
            S_MrObj *model = MrObj("oebm", g_mrModelVt);
            g_msChildren[{model, "textbar"}] = MsFrame("oebbar");
            g_mrMade[FRAME_MODEL] = {model};
            campaign->m_current = 0;
            g_csOperation = 5;
            g_cacheLoads.clear();
            // (A screen shown already: hidden, the new one opened as a screen, not a popup.)
            C_MsScreen top(m.m_scene, "oebtop", 0);
            g_menuScreens = {&top};
            g_gfLog.clear();
            ok &= m.BuildScreens() && g_cacheLoads == std::vector<std::string>({"f_s_alps"}) && MrHas("oebtop hide") &&
                  !m.m_underPopup && g_menuScreens.size() == 2 &&
                  MrHas("oebm 34 Campaign_debrief") && MrHas("oebm 48 title 1") && MrHas("oebm 48 textbar 1") &&
                  MrHas("oebm 48 text01 1") && m.m_screens.size() == 2 && m.m_screens[1]->GetId() == 1u << SCREEN_ID_SHIFT &&
                  m.m_screens[1]->GetScreenFrame() == As<I3D_frame>(model) && m.m_models.size() == 1 &&
                  !g_menuScreens.empty() && g_menuScreens.back() == m.m_screens[1] &&
                  static_cast<C_ControlGroup *>(m.m_screens[1])->m_children.size() == 1;
            delete m.m_screens[1];
            m.m_screens = {&box};
            m.m_models.clear();
            g_menuScreens.clear();
            // No model in the row: nothing loaded.
            campaign->m_current = 1;
            g_csOperation = 4;
            g_cacheLoads.clear();
            g_gfLog.clear();
            ok &= !m.BuildScreens() && g_cacheLoads.empty() && m.m_screens.size() == 1 && g_menuScreens.empty();
            // The model not loaded: false, no screen.
            campaign->m_current = 1;
            g_csOperation = 0;
            g_cacheFailName = "f_s_LaRochel";
            g_mrMade[FRAME_MODEL] = {MrObj("oebf", g_mrModelVt)};
            g_cacheLoads.clear();
            g_gfLog.clear();
            ok &= !m.BuildScreens() && g_cacheLoads == std::vector<std::string>({"f_s_LaRochel"}) &&
                  m.m_screens.size() == 1 && m.m_models.empty() && g_menuScreens.empty() &&
                  !MrHas("oebf 48 title 1");
            g_cacheFailName.clear();
            m.m_screens.clear();
            g_msChildren.erase({model, "textbar"});
            g_mrMade.erase(FRAME_MODEL);
        }
        g_cacheFill = oldFill;
        WM(r.oeBuild, ok);
    }

    // --- the input: Escape or Space: the screen cleared, flipped, ended; other keys nothing; true ---
    {
        bool ok = true;
        IGraph *graph = GetIGraph();
        void *const old[3] = {g_igraphVtable[0xA0 / 4], g_igraphVtable[0x64 / 4], g_igraphVtable[0x70 / 4]};
        g_igraphVtable[0xA0 / 4] = reinterpret_cast<void *>(&MrFake<0xA0>);
        g_igraphVtable[0x64 / 4] = reinterpret_cast<void *>(&MrFake<0x64, uint32_t, float, uint32_t>);
        g_igraphVtable[0x70 / 4] = reinterpret_cast<void *>(&MrFake<0x70>);
        g_mrNames[graph] = "graph";
        for (int key : {1, 0x39, 0, 0x1C, 0x10, 0x139, 0x101, 2}) {
            C_OperationEndMenu m;
            m.m_state = GAME_STATE_RUNNING;
            g_mrRet[{graph, 0xA0}] = static_cast<uint32_t>(key);
            g_gfLog.clear();
            float held[4] = {}, pressed[4] = {};
            ok &= m.OnInput(held, pressed);
            if (key == 1 || key == 0x39)
                ok &= MsLogIs({"graph a0", "graph 64 0 1 3", "graph 70"}, g_gfLog) && m.m_state == GAME_STATE_ENDED;
            else
                ok &= MsLogIs({"graph a0"}, g_gfLog) && m.m_state == GAME_STATE_RUNNING;
        }
        g_mrRet.erase({graph, 0xA0});
        g_mrNames.erase(graph);
        g_igraphVtable[0xA0 / 4] = old[0];
        g_igraphVtable[0x64 / 4] = old[1];
        g_igraphVtable[0x70 / 4] = old[2];
        WM(r.oeInput, ok);
    }

    // --- the other slots: the type, the start true, the message and the stop nothing (the state the
    // menu's first, quit) ---
    {
        C_OperationEndMenu m;
        S_ControlMessage message;
        message.id = 0x200000B;
        g_gfLog.clear();
        g_controlMessages.clear();
        m.OnMessage(&message);
        m.OnStop();
        WM(r.oeSlots, m.GetTypeId() == 14 && m.OnStart(nullptr) && m.OnStart(&message) && g_gfLog.empty() &&
                          g_controlMessages.empty() && m.m_state == GAME_STATE_QUIT);
    }

    // ===== part 9g-ii: the session menu =====
    S_MultiplayerSettings *const settings = g_mpSettings;
    const int oldSessionType = settings->sessionType;
    void *const oldModelSetPos = g_mrModelVt[0x08 / 4];
    g_mrModelVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
    g_csNames[reinterpret_cast<uintptr_t>(&C_SessionMenu::OnConsoleTick)] = "tick";
    g_csNames[reinterpret_cast<uintptr_t>(&C_SessionMenu::OnConsoleHidden)] = "hidden";
    g_csNames[reinterpret_cast<uintptr_t>(&C_SessionMenu::OnPasswordNotify)] = "notify";

    // --- started: the console's style (font 2, black, 0.035 high, left, bottom), the first step;
    // stopped: the style released ---
    {
        C_SessionMenu m;
        m.m_step = SESSION_STEP_FAILED;
        bool ok = m.OnStart(nullptr) && m.m_step == SESSION_STEP_START;
        const S_TextStyleDesc *style = GetTextStyle(m.m_style);
        ok &= style && style->font == 2 && style->color == 0xFF000000 && style->size == 0.035f && style->width == 0.0f &&
              style->x == -1.0f && style->y == -1.0f && style->below == 0.0f && style->above == 0.0f &&
              style->unknown20 == 0 && style->align == 1 && style->valign == 3 && !style->joins && !style->wraps &&
              style->flags30 == 0;
        const int made = m.m_style;
        m.OnStop();
        ok &= GetTextStyle(made) == nullptr;
        ok &= m.GetTypeId() == GAME_STATE_TYPE_SESSION_MENU && m.GetTypeId() == 8 && !m.OnRender() && m.m_wait == 0;
        g_gfLog.clear();
        m.OnUpdate(5);
        ok &= g_gfLog.empty() && m.m_step == SESSION_STEP_START && m.m_wait == 0;
        WM(r.smStart, ok);
    }

    // --- the messages: Escape on the console (the popup closed, the game quit), on the password (the
    // kept messages dropped, the popup closed), elsewhere or no screen nothing; the game started: the
    // session's map ---
    {
        bool ok = true;
        C_SessionMenu m;
        S_ControlMessage message;
        g_controlMessages.clear();
        g_menuScreens.clear();
        message.id = MENU_MSG_ESCAPE;
        m.OnMessage(&message);
        ok &= CsQueue() == "";
        C_MsScreen console(nullptr, "csmc", 0x04000000), password(nullptr, "csmp", 0x08000000),
            other(nullptr, "csmo", 0x0C000000), box(nullptr, "csmb", 0);
        const auto escape = [&](C_Screen *top, uint32_t id) {
            g_menuScreens = {&box, top};
            message.id = id;
            m.OnMessage(&message);
            return CsQueue();
        };
        ok &= escape(&console, MENU_MSG_ESCAPE) == "0 2000005 0 0; 0 2000004 0 0; ";
        ok &= escape(&password, MENU_MSG_ESCAPE) == "0 10000200 1 0; 0 2000005 0 0; ";
        ok &= escape(&other, MENU_MSG_ESCAPE) == "" && escape(&box, MENU_MSG_ESCAPE) == "";
        // (The id's notification bits ignored.)
        ok &= escape(&console, CONTROL_NOTIFY_CLICK | MENU_MSG_ESCAPE) == "0 2000005 0 0; 0 2000004 0 0; ";
        ok &= escape(&console, 0x200000B) == "" && escape(&console, 0x2000068) == "";
        ok &= m.m_state == GAME_STATE_QUIT;
        message.id = 0x2000069;
        m.OnMessage(&message);
        ok &= m.m_state == GAME_STATE_NEXT_MAP && CsQueue() == "";
        m.m_state = GAME_STATE_QUIT;
        message.id = CONTROL_NOTIFY_CLICK | 0x2000069;
        m.OnMessage(&message);
        ok &= m.m_state == GAME_STATE_NEXT_MAP;
        g_menuScreens.clear();
        WM(r.smMessage, ok);
    }

    // --- the console's description: the box placed, its ticks and hiding, the text, the yes and no
    // hidden, "Exit Game" quitting ---
    {
        C_SessionMenu m;
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "menu";
        S_MrObj *model = MrObj("csc", g_mrModelVt);
        S_ScreenDesc desc("MP Connect console", "confirm", 1u << SCREEN_ID_SHIFT);
        g_gfLog.clear();
        bool ok = m.DescribeConsole(&desc, As<I3D_frame>(model)) && MsLogIs({"csc 8 (0.5 -0.375 -0.5)"}, g_gfLog);
        std::string screen;
        for (const S_ControlMessage &msg : desc.messages)
            screen += CsMessage(msg) + "; ";
        ok &= screen == "0 1000007 4014000 menu tick; 0 1000007 400c000 menu hidden; ";
        ok &= CsDesc(desc) == "12 text 4100000:; 14 byes* 4200000: 0 1000004 0 0; 14 bno* 4300000: 0 1000004 0 0; "
                              "14 bok 4400000: 0 1000014 d 0 'Exit Game' 0 6001004 0 0; ";
        g_csNames.erase(reinterpret_cast<uintptr_t>(&m));
        WM(r.smConsole, ok);
    }

    // --- the password's description: the box placed, "ENTER PASSWORD", the input (its Return and
    // Escape), OK and Cancel ("YES" / "NO", their clicks), the other inputs hidden ---
    {
        C_SessionMenu m;
        g_csNames[reinterpret_cast<uintptr_t>(&m)] = "menu";
        S_MrObj *model = MrObj("csp", g_mrModelVt);
        S_ScreenDesc desc("PASSWORD", "password", 2u << SCREEN_ID_SHIFT);
        g_gfLog.clear();
        bool ok = m.DescribePassword(&desc, As<I3D_frame>(model)) && MsLogIs({"csp 8 (0.5 -0.375 -0.5)"}, g_gfLog) &&
                  desc.messages.empty();
        const std::string got = CsDesc(desc);
        ok &= got == "13 text 8100000: 0 1000015 c 7d3; "
                     "3 binput 8200000: 0 1000015 e 7d2 0 1000007 4016000 menu notify 0 1000007 4017000 menu notify; "
                     "14 byes* 8300000: 0 1000015 d 7d1 0 1000007 4001000 menu notify; "
                     "14 bno* 8400000: 0 1000015 d 7d0 0 1000007 4001000 menu notify; "
                     "3 binput01 8500000: 0 1000004 0 0; 3 binput02 8600000: 0 1000004 0 0; ";
        if (!ok)
            std::fprintf(stderr, "CSPASSWORD %s\n", got.c_str());
        g_csNames.erase(reinterpret_cast<uintptr_t>(&m));
        WM(r.smPassword, ok);
    }

    // --- built: the two models ("MP Connect console" on "confirm", "PASSWORD" on "password"), their
    // descriptions, the screens kept, the console opened; a model not loaded: its screen skipped ---
    {
        bool ok = true;
        void (*const oldFill)(I3D_model *) = g_cacheFill;
        g_cacheFill = [](I3D_model *) {};
        C_SessionMenu m;
        S_MrWorld world = MrWorld("csb");
        m.m_scene = As<I3D_scene>(world.scene);
        C_MsScreen box(m.m_scene, "csbbox", 0);
        m.m_screens = {&box};
        S_MrObj *console = MrObj("csbc", g_mrModelVt), *password = MrObj("csbp", g_mrModelVt);
        g_mrMade[FRAME_MODEL] = {console, password};
        g_cacheLoads.clear();
        // (A screen shown already: hidden, the console opened as a screen, not a popup.)
        C_MsScreen top(m.m_scene, "csbtop", 0);
        g_menuScreens = {&top};
        g_gfLog.clear();
        ok &= m.BuildScreens() && MrHas("csbtop hide") && !m.m_underPopup && g_menuScreens.size() == 2 &&
              S_CsPeek::Callbacks(m.m_screens[1]) == 2 && S_CsPeek::Callbacks(m.m_screens[2]) == 0 && g_cacheLoads == std::vector<std::string>({"confirm", "password"}) &&
              MrHas("csbc 34 MP Connect console") && MrHas("csbp 34 PASSWORD") && MrHas("csbc 8 (0.5 -0.375 -0.5)") &&
              MrHas("csbp 8 (0.5 -0.375 -0.5)") && m.m_screens.size() == 3 &&
              m.m_screens[1]->GetId() == 1u << SCREEN_ID_SHIFT && m.m_screens[2]->GetId() == 2u << SCREEN_ID_SHIFT &&
              m.m_screens[1]->GetScreenFrame() == As<I3D_frame>(console) &&
              m.m_screens[2]->GetScreenFrame() == As<I3D_frame>(password) && m.m_models.size() == 2 &&
              !g_menuScreens.empty() && g_menuScreens.back() == m.m_screens[1];
        for (size_t i = 1; i < m.m_screens.size(); ++i)
            delete m.m_screens[i];
        m.m_screens = {&box};
        m.m_models.clear();
        g_menuScreens.clear();
        // The console's model not loaded: the password's screen only (its id its own).
        g_cacheFailName = "confirm";
        g_mrMade[FRAME_MODEL] = {MrObj("csbf", g_mrModelVt), MrObj("csbq", g_mrModelVt)};
        g_cacheLoads.clear();
        g_gfLog.clear();
        ok &= m.BuildScreens() && g_cacheLoads == std::vector<std::string>({"confirm", "password"}) &&
              !MrHas("csbf 8 (0.5 -0.375 -0.5)") && MrHas("csbq 8 (0.5 -0.375 -0.5)") && m.m_screens.size() == 2 &&
              m.m_screens[1]->GetId() == 2u << SCREEN_ID_SHIFT && m.m_models.size() == 1;
        g_cacheFailName.clear();
        for (size_t i = 1; i < m.m_screens.size(); ++i)
            delete m.m_screens[i];
        m.m_screens.clear();
        m.m_models.clear();
        g_menuScreens.clear();
        g_mrMade.erase(FRAME_MODEL);
        g_cacheFill = oldFill;
        WM(r.smBuild, ok);
    }

    // --- the console's ticks: its own screen shown only; a wait counted down; the steps (hosting:
    // the server made or not, the game started; neither kind: the error) ---
    {
        bool ok = true;
        C_SessionMenu m;
        m.m_style = 0x2A;
        C_CsScreen console(nullptr, "cst", 0x04000000), password(nullptr, "cstp", 0x08000000);
        const auto tick = [&](C_Control *control, uint32_t value) {
            g_gfLog.clear();
            g_controlMessages.clear();
            return C_SessionMenu::OnConsoleTick(&m, nullptr, control, CONTROL_NOTIFY_SCREEN_TICK, value);
        };
        // Another screen's, the console hidden: false, nothing.
        ok &= !tick(&password, 10) && g_gfLog.empty() && CsQueue() == "" && m.m_step == SESSION_STEP_START;
        console.SetShown(false);
        ok &= !tick(&console, 10) && g_gfLog.empty() && CsQueue() == "" && m.m_step == SESSION_STEP_START;
        console.SetShown(true);
        // Hosting: the squad emptied, the roster again, the text cleared, "Creating server ...".
        settings->sessionType = SESSION_HOSTING;
        std::memset(settings->sessionName, 0, sizeof settings->sessionName);
        std::strcpy(settings->sessionName, "Sabre game");
        settings->sessionName[0xC8] = 0x55;
        settings->sessionName[0x15C] = 0x55;
        settings->sessionName[0x167] = 0x66;
        settings->sessionName[0xD8] = 0x77;
        ok &= tick(&console, 10) && MsLogIs({"squad clear", "roster reload"}, g_gfLog) &&
              CsQueue() == "4100000 2000018 0 0; 4100000 1000017 2a 91b; " && m.m_wait == 300 &&
              m.m_step == SESSION_STEP_HOST;
        // The wait: counted down by the ticks' time (to 0 or below), nothing else.
        ok &= tick(&console, 100) && m.m_wait == 200 && g_gfLog.empty() && CsQueue() == "";
        // (A wait of 1 is a wait too.)
        m.m_wait = 1;
        ok &= tick(&console, 1) && m.m_wait == 0 && g_gfLog.empty() && CsQueue() == "" && m.m_step == SESSION_STEP_HOST;
        m.m_wait = 300;
        ok &= tick(&console, 100) && m.m_wait == 200;
        ok &= tick(&console, 250) && m.m_wait == -50 && g_gfLog.empty() && m.m_step == SESSION_STEP_HOST;
        // The server made: the session's name, DirectPlay's fields; "Server created.".
        g_csHost = 0;
        ok &= tick(&console, 10) &&
              MsLogIs({"net host 1 Sabre game ebfe7ba0 11d2628d 60000fae 1114b097 100 2000 77 66"}, g_gfLog) &&
              CsQueue() == "4100000 1000017 2a 922; " && m.m_wait == 300 && m.m_step == SESSION_STEP_HOSTED;
        m.m_wait = 0;
        ok &= tick(&console, 10) && g_gfLog.empty() && CsQueue() == "0 2000069 0 0; " && m.m_step == SESSION_STEP_WAIT;
        ok &= tick(&console, 10) && g_gfLog.empty() && CsQueue() == "" && m.m_step == SESSION_STEP_WAIT && m.m_wait == 0;
        // Not made: "Unable to create server.", "Press <ESC> to return.".
        m.m_step = SESSION_STEP_HOST;
        g_csHost = -1;
        ok &= tick(&console, 10) && CsQueue() == "4100000 1000017 2a 920; 4100000 1000017 2a 921; " &&
              m.m_wait == 2500 && m.m_step == SESSION_STEP_FAILED;
        m.m_wait = 0;
        g_csHost = 5;
        m.m_step = SESSION_STEP_HOST;
        ok &= tick(&console, 10) && m.m_step == SESSION_STEP_HOSTED;
        ok &= tick(&console, 10) && m.m_step == SESSION_STEP_HOSTED && m.m_wait == 290;
        m.m_wait = 0;
        m.m_step = SESSION_STEP_FAILED;
        ok &= tick(&console, 10) && g_gfLog.empty() && CsQueue() == "" && m.m_step == SESSION_STEP_FAILED;
        m.m_step = 9;
        ok &= tick(&console, 10) && g_gfLog.empty() && CsQueue() == "" && m.m_step == 9;
        // Neither kind: "ERROR -- !!!", "Connection lost.".
        settings->sessionType = SESSION_NONE;
        m.m_step = SESSION_STEP_START;
        ok &= tick(&console, 10) && MsLogIs({"squad clear", "roster reload"}, g_gfLog) &&
              CsQueue() == "4100000 2000018 0 0; 4100000 1000017 2a 91e; 4100000 1000017 2a 91f; " && m.m_wait == 1000 &&
              m.m_step == SESSION_STEP_FAILED;
        settings->sessionType = 3;
        m.m_wait = 0;
        m.m_step = SESSION_STEP_START;
        ok &= tick(&console, 10) && m.m_step == SESSION_STEP_FAILED && m.m_wait == 1000;
        WM(r.smTick, ok);
    }

    // --- joining: "Creating client ...", "Connecting to server ...", the connection (the settings'
    // +0x17C), its status polled (on its way: again; connected; refused: why, the password asked) ---
    {
        bool ok = true;
        C_SessionMenu m;
        m.m_style = 7;
        C_CsScreen console(nullptr, "csj", 0x04000000);
        const auto tick = [&](uint32_t value) {
            g_gfLog.clear();
            g_controlMessages.clear();
            return C_SessionMenu::OnConsoleTick(&m, nullptr, &console, CONTROL_NOTIFY_SCREEN_TICK, value);
        };
        settings->sessionType = SESSION_JOINING;
        ok &= tick(1) && MsLogIs({"squad clear", "roster reload"}, g_gfLog) &&
              CsQueue() == "4100000 2000018 0 0; 4100000 1000017 7 91c; 4100000 1000017 7 91d; " && m.m_wait == 300 &&
              m.m_step == SESSION_STEP_CONNECT;
        m.m_wait = 0;
        const int address = static_cast<int>(reinterpret_cast<uint8_t *>(settings->unknown17C) -
                                             reinterpret_cast<uint8_t *>(settings));
        ok &= tick(1) && MsLogIs({CmText("net connect %d", address)}, g_gfLog) && CsQueue() == "" &&
              m.m_step == SESSION_STEP_JOINING && m.m_wait == 0;
        for (int status : {0, 2, 6, 8, -1}) {
            g_csJoin = status;
            ok &= tick(1) && MsLogIs({"net join"}, g_gfLog) && CsQueue() == "" && m.m_step == SESSION_STEP_JOINING &&
                  m.m_wait == 0;
        }
        const auto status = [&](int join) {
            m.m_step = SESSION_STEP_JOINING;
            m.m_wait = 0;
            g_csJoin = join;
            ok &= tick(1);
            return CsQueue();
        };
        ok &= status(1) == "4100000 1000017 7 923; 4100000 1000017 7 924; " && m.m_step == SESSION_STEP_WAIT && m.m_wait == 0;
        ok &= status(3) == "4100000 1000017 7 925; 4100000 1000017 7 926; " && m.m_step == SESSION_STEP_FAILED && m.m_wait == 0;
        ok &= status(4) == "4100000 1000017 7 927; " && m.m_step == SESSION_STEP_PASSWORD && m.m_wait == 500;
        ok &= status(5) == "4100000 1000017 7 929; 4100000 1000017 7 928; " && m.m_step == SESSION_STEP_FAILED && m.m_wait == 0;
        ok &= status(7) == "4100000 1000017 7 92a; 4100000 1000017 7 928; " && m.m_step == SESSION_STEP_FAILED && m.m_wait == 0;
        // The password asked: its input emptied, its screen opened, the input edited.
        m.m_step = SESSION_STEP_PASSWORD;
        m.m_wait = 0;
        ok &= tick(1) && g_gfLog.empty() &&
              CsQueue() == "8200000 1000015 e 7d2; 0 2000001 2 0; 8200000 2000016 0 0; " && m.m_step == SESSION_STEP_WAIT &&
              m.m_wait == 0;
        m.m_step = SESSION_STEP_PASSWORD;
        m.m_wait = -3;
        ok &= tick(1) && m.m_wait == 0 && m.m_step == SESSION_STEP_WAIT;
        WM(r.smJoin, ok);
    }

    // --- the password's controls: the input's Return or OK with a text (kept, the first step again,
    // the screen closed), without one nothing; Escape, Cancel: closed; others: false ---
    {
        bool ok = true;
        C_SessionMenu m;
        C_CsScreen input(nullptr, "csin", 0x08200000), okButton(nullptr, "csok", 0x08300000),
            cancel(nullptr, "csno", 0x08400000), other(nullptr, "csot", 0x08500000), screen1(nullptr, "cs1", 0x04000000),
            box(nullptr, "csbx", 0);
        const auto notify = [&](C_Control *control, uint32_t notification) {
            g_gfLog.clear();
            g_controlMessages.clear();
            return C_SessionMenu::OnPasswordNotify(&m, nullptr, control, notification, 0);
        };
        const C_str oldPassword = settings->password;
        settings->password = C_str("old");
        g_sessionPasswordSet = false;
        ok &= !notify(nullptr, CONTROL_NOTIFY_RETURN) && CsQueue() == "";
        m.m_step = SESSION_STEP_WAIT;
        input.text = "";
        ok &= notify(&input, CONTROL_NOTIFY_RETURN) && MsLogIs({"csin get text"}, g_gfLog) && CsQueue() == "" &&
              m.m_step == SESSION_STEP_WAIT && !g_sessionPasswordSet && std::string(settings->password.c_str()) == "old";
        input.text = "secret";
        ok &= notify(&input, CONTROL_NOTIFY_RETURN) && CsQueue() == "0 2000003 0 0; " && m.m_step == SESSION_STEP_START &&
              g_sessionPasswordSet && std::string(settings->password.c_str()) == "secret";
        m.m_step = SESSION_STEP_WAIT;
        g_sessionPasswordSet = false;
        ok &= notify(&input, CONTROL_NOTIFY_ESCAPE) && g_gfLog.empty() && CsQueue() == "0 2000003 0 0; " &&
              m.m_step == SESSION_STEP_WAIT && !g_sessionPasswordSet;
        ok &= notify(&input, CONTROL_NOTIFY_CLICK) && g_gfLog.empty() && CsQueue() == "" && m.m_step == SESSION_STEP_WAIT;
        // OK: the input found in its screen (the menu's third).
        m.m_screens = {&box, &screen1, &input};
        input.text = "";
        ok &= notify(&okButton, CONTROL_NOTIFY_CLICK) && MsLogIs({"csin get text"}, g_gfLog) && CsQueue() == "" &&
              m.m_step == SESSION_STEP_WAIT;
        input.text = "pass2";
        ok &= notify(&okButton, CONTROL_NOTIFY_CLICK) && CsQueue() == "0 2000003 0 0; " && m.m_step == SESSION_STEP_START &&
              g_sessionPasswordSet && std::string(settings->password.c_str()) == "pass2";
        // (Any notification of OK, Cancel.)
        m.m_step = SESSION_STEP_WAIT;
        ok &= notify(&okButton, CONTROL_NOTIFY_ENTER) && CsQueue() == "0 2000003 0 0; " && m.m_step == SESSION_STEP_START;
        m.m_step = SESSION_STEP_WAIT;
        ok &= notify(&cancel, CONTROL_NOTIFY_CLICK) && g_gfLog.empty() && CsQueue() == "0 2000003 0 0; " &&
              m.m_step == SESSION_STEP_WAIT;
        ok &= notify(&cancel, CONTROL_NOTIFY_ENTER) && CsQueue() == "0 2000003 0 0; ";
        // No input: nothing (fixed).
        m.m_screens = {&box, &screen1};
        ok &= notify(&okButton, CONTROL_NOTIFY_CLICK) && g_gfLog.empty() && CsQueue() == "" && m.m_step == SESSION_STEP_WAIT;
        ok &= !notify(&other, CONTROL_NOTIFY_CLICK) && !notify(&screen1, CONTROL_NOTIFY_CLICK) && CsQueue() == "";
        ok &= C_SessionMenu::OnConsoleHidden(&m, nullptr, &screen1, CONTROL_NOTIFY_HIDDEN, 0);
        m.m_screens.clear();
        settings->password = oldPassword;
        g_sessionPasswordSet = false;
        WM(r.smNotify, ok);
    }

    g_mrModelVt[0x08 / 4] = oldModelSetPos;
    settings->sessionType = oldSessionType;
    g_csNames.clear();
    g_controlMessages.clear();
    g_driverVtable[0x54 / 4] = oldCreate;
    g_mrModelVt[0x48 / 4] = oldFind;
    g_gameProfile.m_campaign = oldCampaign;
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("campaignscreens: oeTable %d oeDescribe %d oeBuild %d oeInput %d oeSlots %d smStart %d smMessage %d "
                "smConsole %d smPassword %d smBuild %d smTick %d smJoin %d smNotify %d\n", r.oeTable, r.oeDescribe,
                r.oeBuild, r.oeInput, r.oeSlots, r.smStart, r.smMessage, r.smConsole, r.smPassword, r.smBuild, r.smTick,
                r.smJoin, r.smNotify);
}
