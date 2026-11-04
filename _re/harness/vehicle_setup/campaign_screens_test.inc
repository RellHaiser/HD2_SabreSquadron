// Part 9g's campaign screens (game/operation_end.cpp, ..), in the factory build, run by ControlsTest after
// EquipmentScreenTest: the fakes the earlier tests' (the logging frames and models, the screens' factory).
// Prints "campaignscreens:" with 1 per check met.
#define private public
#define protected public
#include "game/operation_end.h"
#include "game/debriefing.h"
#include "game/session_menu.h"
#include "game/squad_menu.h"
#include "game/video_state.h"
#include "game/video_text_box.h"
#undef protected
#undef private
#include <array>

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

// (Part 9g-iv: the video player's. campaign.cpp, game_texts.cpp, the sound module's music and the
// profile's movies are not linked: faked, logged.)
C_CampaignNode::~C_CampaignNode() {}
C_CampaignVideo::~C_CampaignVideo() { GfLog("video gone"); }
bool C_CampaignVideo::LoadDefinition(C_chunk &) { return false; }
bool C_CampaignVideo::Load(C_chunk &) { return false; }
bool C_CampaignVideo::Save(C_chunk &) { return false; }
// The scripts by path: their lines (id, text) added by Load (none: false).
std::map<std::string, std::vector<std::pair<int, std::string>>> g_vsScripts;
template <class Tag, typename Tag::type M> struct S_VsRob {
    friend typename Tag::type VsGet(Tag) { return M; }
};
struct S_VsTextsTag {
    using type = std::vector<S_GameText> C_GameTexts::*;
    friend type VsGet(S_VsTextsTag);
};
template struct S_VsRob<S_VsTextsTag, &C_GameTexts::m_texts>;
struct S_VsFileTag {
    using type = C_str C_CampaignVideo::*;
    friend type VsGet(S_VsFileTag);
};
template struct S_VsRob<S_VsFileTag, &C_CampaignVideo::m_fileName>;
struct S_VsBriefingTag {
    using type = bool C_CampaignVideo::*;
    friend type VsGet(S_VsBriefingTag);
};
template struct S_VsRob<S_VsBriefingTag, &C_CampaignVideo::m_briefing>;
void C_GameTexts::Clear()
{
    GfLog("texts clear");
    (this->*VsGet(S_VsTextsTag())).clear();
}
bool C_GameTexts::Add(int id, const char *text, bool unknown8)
{
    S_GameText t;
    t.id = id;
    t.text = text;
    t.unknown8 = unknown8;
    (this->*VsGet(S_VsTextsTag())).push_back(t);
    return true;
}
bool C_GameTexts::Load(const char *fileName)
{
    GfLog("texts load %s", fileName);
    const auto it = g_vsScripts.find(fileName);
    if (it == g_vsScripts.end())
        return false;
    for (const auto &line : it->second)
        Add(line.first, line.second.c_str(), false);
    return true;
}
float g_vsMusicPosition = 0;
void C_SoundModule::FadeBackgroundMusic(float volume, int time) { GfLog("fade %g %d", volume, time); }
int C_SoundModule::GetBackgroundMusicPosition() const { return static_cast<int>(g_vsMusicPosition); }
void C_PlayerProfile::SetMovieSeen(const char *file, bool seen) { GfLog("seen %s %d", file, seen); }
std::set<std::string> g_vsFiles;
bool FileExists(const char *path)
{
    GfLog("exists %s", path);
    return g_vsFiles.count(path) != 0;
}
const char *GetGameDirectory() { return "G:\\Game"; }

// (Part 9g-v: the debriefing's. The diary, the profiles' progress and medals, the saved games and the
// campaign's steps are not linked: faked, logged; the screens' builders (9g-vi) logged.)
void C_Diary::CommitCurrent() { GfLog("diary commit"); }
void C_Diary::ClearCurrent() { GfLog("diary clear"); }
void S_SquadSoldier::ReturnItems() { GfLog("return all %d", profileIndex); }
int g_dbProgress;
bool C_PlayerProfile::SetProgress(int campaign, int mission)
{
    GfLog("set progress %d %d", campaign, mission);
    return true;
}
int C_PlayerProfile::GetProgress(int campaign) const
{
    GfLog("progress %d", campaign);
    return g_dbProgress;
}
C_str C_GameProfile::GetSaveFileName(int a, int b) { return C_fstr("save%d_%d", a, b); }
bool g_dbLoaded = true, g_dbAdvanced = true;
bool C_GameApp::SaveGame(const C_str &fileName, bool inGame)
{
    GfLog("save %s %d", fileName.c_str(), inGame);
    return true;
}
bool C_GameApp::LoadGame(const C_str &fileName, bool inGame)
{
    GfLog("load %s %d", fileName.c_str(), inGame);
    return g_dbLoaded;
}
bool C_Campaign::AdvanceToMission()
{
    GfLog("advance");
    return g_dbAdvanced;
}
bool C_Campaign::SetupSingleMission(int campaign, int missionNumber)
{
    GfLog("single %d %d", campaign, missionNumber);
    return true;
}
bool S_SoldierProfile::HasUnknown28(int value) const
{
    return std::find(unknown28.begin(), unknown28.end(), value) != unknown28.end();
}
// (Part 9g-viii: the ranks' and medals' texts, profile_roster.cpp's.)
S_RankName g_rankNames[8] = {{0x27A6, 0x2792, C_str(), C_str()}, {0x27A7, 0x2793, C_str(), C_str()}, {0x27A8, 0x2794, C_str(), C_str()},
                             {0x27A9, 0x2795, C_str(), C_str()}, {0x27AA, 0x2796, C_str(), C_str()}, {0x27AB, 0x2797, C_str(), C_str()},
                             {0x27AC, 0x2798, C_str(), C_str()}, {0x27AD, 0x2799, C_str(), C_str()}};
int g_rosterTexts[24];
std::vector<int> g_diaryTexts;
const std::vector<int> &C_Diary::GetTexts() const { return g_diaryTexts; }
C_MissionDef *g_dbMission;
C_MissionDef *C_Campaign::GetCurrentMission() { return g_dbMission; }

// (Part 9g-ix: the squad screen's. The squad's states found, logged; the screens and the soldier's
// control (later parts) faked, logged.)
S_SquadSoldier *C_GameProfile::FindSoldierState(int profileIndex)
{
    GfLog("find state %d", profileIndex);
    for (S_SquadSoldier *s : m_missionSquad->states)
        if (s && s->profileIndex == profileIndex)
            return s;
    return nullptr;
}
bool C_SquadMenu::BuildScreens() { return true; }
bool C_SquadMenu::OnSoldierControl(void *argument, C_Control *control)
{
    GfLog("soldier control %d %x", argument == this, control->GetId());
    return true;
}
namespace {

// (Part 9g-iv.) The video player's engine objects: the stream's rectangle and size, its position; the
// driver's buffers and player, IGraph's stream and screen; a box's private fields; a box logging its
// render; a state of a type.
S_showRect g_vsVideoSize;
int g_vsPosition, g_vsW = 800, g_vsH = 600;
S_MrObj *g_vsStream, *g_vsSource;
std::deque<S_MrObj *> g_vsSounds;
uint32_t __stdcall VsSetRect(void *self, const S_showRect *r)
{
    GfLog("%s 20 %d %d %d %d", MrName(self).c_str(), r->x, r->y, r->width, r->height);
    return 0;
}
uint32_t __stdcall VsVideoSize(void *self, S_showRect *out)
{
    GfLog("%s 28", MrName(self).c_str());
    *out = g_vsVideoSize;
    return 0;
}
uint32_t __stdcall VsPosition(void *self, int *position)
{
    GfLog("%s 38", MrName(self).c_str());
    *position = g_vsPosition;
    return 0;
}
void *__stdcall VsCreateSound(void *)
{
    if (g_vsSounds.empty())
        return nullptr;
    S_MrObj *o = g_vsSounds.front();
    g_vsSounds.pop_front();
    return o;
}
void *__stdcall VsCreateSource(void *) { return g_vsSource; }
void *__stdcall VsCreateStream(void *) { return g_vsStream; }
int __stdcall VsScrnX(void *) { return g_vsW; }
int __stdcall VsScrnY(void *) { return g_vsH; }
struct S_VsBoxPeek : C_TextBox {
    using C_TextBox::m_left;
    using C_TextBox::m_top;
    using C_TextBox::m_right;
    using C_TextBox::m_bottom;
    using C_TextBox::m_width;
    using C_TextBox::m_height;
    using C_TextBox::m_depth;
    using C_TextBox::m_runs;
    using C_TextBox::m_lines;
    using C_TextBox::m_scroll;
    using C_TextBox::m_colour;
};
struct C_VsLogBox : C_TextBox {
    void Render() override { GfLog("box render"); }
    ~C_VsLogBox() override { GfLog("box gone"); }
};
struct C_VsState : C_GameState {
    explicit C_VsState(int t) : type(t) {}
    int GetTypeId() const override { return type; }
    int type;
};

// (Part 9g-vi.) The diary's pages control: its count and page break, the messages asked for logged; the
// mission's fields; frames logging their LinkTo and Update.
struct C_DbPages : C_MsScreen {
    C_DbPages(I3D_scene *scene, const char *n, uint32_t id) : C_MsScreen(scene, n, id) {}
    int count = 0, pageBreak = 0;
    std::string log;
    bool HandleMessage(S_ControlMessage *m) override
    {
        if (m->id == CONTROL_MSG_GET_PAGES) {
            log += "pages ";
            m->value = static_cast<uint32_t>(count);
        } else if (m->id == CONTROL_MSG_GET_PAGE_BREAK) {
            log += "break ";
            m->value = static_cast<uint32_t>(pageBreak);
        }
        return true;
    }
};
struct C_DbObjective : C_CampaignNode {
    using C_CampaignNode::m_id;
};
// (Part 9g-viii.) The soldiers' scroll bar: its value, the times asked.
struct C_DbScroll : C_MsScreen {
    C_DbScroll(I3D_scene *scene, const char *n, uint32_t id) : C_MsScreen(scene, n, id) {}
    int value = 0, asked = 0;
    bool HandleMessage(S_ControlMessage *m) override
    {
        if (m->id == CONTROL_MSG_GET_BAR_VALUE) {
            ++asked;
            m->value = static_cast<uint32_t>(value);
        }
        return true;
    }
};
// (Part 9g-ix.) The squad screen's popup: its deletion logged.
struct C_SqPopup : C_MsScreen {
    C_SqPopup() : C_MsScreen(nullptr, "sqpopup", 0x0C000000) {}
    ~C_SqPopup() override { GfLog("sqpopup deleted"); }
};
struct C_DbMission : C_MissionDef {
    using C_MissionDef::m_objectives;
    using C_MissionDef::m_unknown2C;
};
void **DbPageVt()
{
    static void **vt = nullptr;
    if (!vt) {
        vt = new void *[MR_SLOTS];
        std::copy(g_msFrameVt, g_msFrameVt + MR_SLOTS, vt);
        vt[0x38 / 4] = reinterpret_cast<void *>(&MrFake<0x38, void *, uint32_t>);
        vt[0x24 / 4] = reinterpret_cast<void *>(&MrFake<0x24>);
        vt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
    }
    return vt;
}

// A control's callbacks counted.
struct S_CsPeek : C_Control {
    static size_t Callbacks(C_Control *control) { return static_cast<S_CsPeek *>(control)->m_callbacks.size(); }
};

struct S_CampaignScreensResults {
    bool oeTable = true, oeDescribe = true, oeBuild = true, oeInput = true, oeSlots = true;
    bool smStart = true, smMessage = true, smConsole = true, smPassword = true, smBuild = true, smTick = true;
    bool smJoin = true, smNotify = true;
    bool vsCredits = true, vsMovie = true, vsFit = true, vsUpdate = true, vsScroll = true, vsRender = true, vsInput = true;
    bool vsMovieState = true;
    bool dbMedal = true, dbResults = true, dbStore = true, dbStart = true, dbMessage = true, dbBuild = true;
    bool dbDiary = true, dbDiaryPage = true, dbWon = true, dbScreen = true, dbPanel = true, dbObjectives = true, dbResult = true;
    bool dbShow = true, dbSkills = true, dbRank = true, dbMedals = true, dbScroll = true;
    bool sqSlot = true, sqOneSoldier = true, sqStart = true, sqSlots = true, sqSync = true, sqMessage = true, sqSelect = true;
};

// A message's values ("target id value value2[ value3][ 'text']"; value2 / value3 by name when known).
std::map<uintptr_t, std::string> g_csNames;
std::string CsMessage(const S_ControlMessage &m)
{
    const auto value = [](uintptr_t v) {
        const auto it = g_csNames.find(v);
        return it != g_csNames.end() ? it->second : CmText("%x", static_cast<uint32_t>(v));
    };
    if (m.id == CONTROL_MSG_SET_BAR_VALUE && m.value == 2 && m.value2)
        return CmText("%x %x %x '", m.target, m.id, m.value) + reinterpret_cast<const char *>(m.value2) + "'";
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
// A control description and its children ("type name id: messages { children }").
std::string CsTree(const S_ControlDesc *c)
{
    std::string s = CmText("%x %s %x:", c->type, c->name.c_str(), c->id);
    for (const S_ControlMessage &m : c->messages)
        s += " " + CsMessage(m);
    if (!c->children.empty()) {
        s += " {";
        for (const S_ControlDesc *child : c->children)
            s += " " + CsTree(child);
        s += " }";
    }
    return s + ";";
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
    // ===== part 9g-iv: the video player =====
    {
        // The engine's objects: the stream, the voices' player and buffers (logging), the driver's and
        // IGraph's slots, the sound module's vtable (its update logged) and its music's field.
        static void *streamVt[MR_SLOTS], *sourceVt[MR_SLOTS], *soundVt[MR_SLOTS], *driverVt[16];
        streamVt[0x04 / 4] = reinterpret_cast<void *>(&MrFake<0x04>);
        streamVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08>);
        streamVt[0x0C / 4] = reinterpret_cast<void *>(&MrFake<0x0C, const char *, uint32_t>);
        streamVt[0x10 / 4] = reinterpret_cast<void *>(&MrFake<0x10>);
        streamVt[0x14 / 4] = reinterpret_cast<void *>(&MrFake<0x14>);
        streamVt[0x18 / 4] = reinterpret_cast<void *>(&MrFake<0x18>);
        streamVt[0x20 / 4] = reinterpret_cast<void *>(&VsSetRect);
        streamVt[0x28 / 4] = reinterpret_cast<void *>(&VsVideoSize);
        streamVt[0x2C / 4] = reinterpret_cast<void *>(&MrFake<0x2C>);
        streamVt[0x38 / 4] = reinterpret_cast<void *>(&VsPosition);
        streamVt[0x3C / 4] = reinterpret_cast<void *>(&MrFake<0x3C, float>);
        streamVt[0x50 / 4] = reinterpret_cast<void *>(&MrFake<0x50, bool>);
        sourceVt[0x00 / 4] = reinterpret_cast<void *>(&MrFake<0x00>);
        sourceVt[0x04 / 4] = reinterpret_cast<void *>(&MrFake<0x04, void *>);
        sourceVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08>);
        sourceVt[0x0C / 4] = reinterpret_cast<void *>(&MrFake<0x0C, int, int>);
        sourceVt[0x20 / 4] = reinterpret_cast<void *>(&MrFake<0x20, float>);
        soundVt[0x00 / 4] = reinterpret_cast<void *>(&MrFake<0x00>);
        soundVt[0x04 / 4] = reinterpret_cast<void *>(&MrFake<0x04, const char *, uint32_t>);
        std::copy(g_mrSndVtable, g_mrSndVtable + 8, driverVt);
        driverVt[0x14 / 4] = reinterpret_cast<void *>(&MrFake<0x14, float>);
        driverVt[0x20 / 4] = reinterpret_cast<void *>(&VsCreateSound);
        driverVt[0x28 / 4] = reinterpret_cast<void *>(&VsCreateSource);
        void **const oldDriverVt = g_mrSndDriver.vtable;
        g_mrSndDriver.vtable = driverVt;
        g_mrNames[&g_mrSndDriver] = "snd";
        IGraph *graph = GetIGraph();
        g_mrNames[graph] = "graph";
        const int slots[] = {0x18, 0x1C, 0x38, 0x64, 0x68, 0x6C, 0x70, 0x74, 0x78, 0xA0};
        void *oldGraph[10];
        for (int i = 0; i < 10; ++i)
            oldGraph[i] = g_igraphVtable[slots[i] / 4];
        g_igraphVtable[0x18 / 4] = reinterpret_cast<void *>(&VsScrnX);
        g_igraphVtable[0x1C / 4] = reinterpret_cast<void *>(&VsScrnY);
        g_igraphVtable[0x38 / 4] = reinterpret_cast<void *>(&VsCreateStream);
        g_igraphVtable[0x64 / 4] = reinterpret_cast<void *>(&MrFake<0x64, uint32_t, float, uint32_t>);
        g_igraphVtable[0x68 / 4] = reinterpret_cast<void *>(&MrFake<0x68>);
        g_igraphVtable[0x6C / 4] = reinterpret_cast<void *>(&MrFake<0x6C>);
        g_igraphVtable[0x70 / 4] = reinterpret_cast<void *>(&MrFake<0x70>);
        g_igraphVtable[0x74 / 4] = reinterpret_cast<void *>(&MrFake<0x74, int, int>);
        g_igraphVtable[0x78 / 4] = reinterpret_cast<void *>(&MrFake<0x78, int>);
        g_igraphVtable[0xA0 / 4] = reinterpret_cast<void *>(&MrFake<0xA0>);
        C_MrSound moduleVt;
        void *const oldSoundVt = *reinterpret_cast<void **>(g_soundModuleMemory);
        *reinterpret_cast<void **>(g_soundModuleMemory) = *reinterpret_cast<void **>(&moduleVt);
        int musicOffset = -1;
        {
            alignas(16) uint8_t probe[sizeof g_soundModuleMemory] = {};
            for (int off = 4; off < 0x100 && musicOffset < 0; off += 4) {
                std::memset(probe, 0, sizeof probe);
                *reinterpret_cast<int *>(probe + off) = 12345;
                if (reinterpret_cast<C_SoundModule *>(probe)->GetBackgroundMusic() == 12345)
                    musicOffset = off;
            }
        }
        int &music = *reinterpret_cast<int *>(g_soundModuleMemory + (musicOffset < 0 ? 0x100 : musicOffset));
        const int oldMusic = music;
        C_PlayerProfile player;
        C_PlayerProfile *const oldPlayer = g_gameProfile.m_player;
        g_gameProfile.m_player = &player;
        S_DetailSettings *const details = g_gameProfile.GetDetailSettings();
        const S_DetailSettings oldDetails = *details;
        details->soundVolume = 50.0f;
        const std::string oldLanguage = g_foLanguage;
        g_foLanguage = "English";
        const bool oldUnicode = g_gameProfile.m_unicodeFont;
        g_gameProfile.m_unicodeFont = false;
        const auto node = [](const char *file, bool briefing) {
            auto *video = new C_CampaignVideo();
            video->*VsGet(S_VsFileTag()) = file;
            video->*VsGet(S_VsBriefingTag()) = briefing;
            return video;
        };
        const auto rectOf = [](C_TextBox *box) {
            const S_VsBoxPeek *b = static_cast<S_VsBoxPeek *>(box);
            return CmText("%g %g %g %g %g %g %g", b->m_left, b->m_top, b->m_right, b->m_bottom, b->m_width, b->m_height,
                          b->m_depth);
        };
        const auto runs = [](C_TextBox *box) {
            std::string s;
            const S_VsBoxPeek *b = static_cast<S_VsBoxPeek *>(box);
            for (const S_TextRun &run : b->m_runs)
                s += CmText("%s|%d; ", run.text.c_str(), run.style);
            return s;
        };
        const auto newStream = [&](const char *name) {
            S_MrObj *o = MrObj(name, streamVt);
            g_vsStream = o;
            return o;
        };
        const auto newSource = [&](const char *name) {
            S_MrObj *o = MrObj(name, sourceVt);
            g_vsSource = o;
            return o;
        };
        const auto newSound = [&](const char *name) {
            S_MrObj *o = MrObj(name, soundVt);
            g_vsSounds.push_back(o);
            return o;
        };
        const std::string dir = "G:\\Game\\text\\English\\subtitles\\";

        // --- the credits: the music saved and 37 crossfaded to, their box, styles and script; no stream ---
        {
            bool ok = true;
            C_VideoState v;
            C_CampaignVideo *video = node("credits.bik", false);
            music = 5;
            g_vsMusicPosition = 1234;
            g_vsW = 800;
            g_vsH = 600;
            g_vsFiles = {dir + "credits.txt"};
            g_vsScripts[dir + "credits.txt"] = {
                {1, "<TI>Title"}, {2, "<NA>Name"}, {3, "<SM>small"}, {4, "<SM><C_1>a<C_2>b<C_3>c"}, {5, "<TICZ>t"},
                {6, "<NACZ>n"}, {7, "<SMCZ>s"}, {8, "<SMCZ><C_1>x<C_2>y<C_3>z"}, {9, "<EL>el"}, {10, "<ES>es"},
                {11, "<SPEED>100"}, {12, "<XX>none"}, {13, "no tag"}, {14, "<>empty"}, {15, "<T>t"}, {16, "<TI>100%"},
                {17, "<SM><C_1>only one"}, {18, "<SPEED>x"}, {19, "><TI>skipped"}, {20, "<SM>p<C_2>q<C_3>r"}};
            v.m_time = 5;
            g_gfLog.clear();
            ok &= v.Start(video) && v.m_state == GAME_STATE_RUNNING && v.m_credits && v.m_scrolling && !v.m_outro2 &&
                  !v.m_dataDiskOutro && !v.m_stream && !v.m_voicePlayer && v.m_time == 0 && v.m_musicTrack == 5 &&
                  v.m_musicPosition == 1234 && v.m_textStyle == 8 && v.m_video == video;
            ok &= MsLogIs({"crossfade 37 2000 0", "exists " + dir + "credits.txt", "texts load " + dir + "credits.txt",
                           "texts clear"}, g_gfLog);
            ok &= dynamic_cast<C_VideoTextBox *>(v.m_box) && rectOf(v.m_box) == "16 588 784 12 768 576 0.764" &&
                  static_cast<S_VsBoxPeek *>(v.m_box)->m_scroll == -546 && v.m_scroll == -546.875f &&
                  v.m_scrollSpeed == static_cast<float>(800) * 0.0009765625f * 100.0f * 0.001f;
            std::string want;
            const char *texts[] = {"Title", "Name", "small", "a", "b", "c", "t", "n", "s", "x", "y", "z", "el", "es", "100%", "<C_1>only one", "p<C_2>q<C_3>r"};
            const int styles[] = {0, 1, 2, 11, 12, 13, 5, 6, 7, 8, 9, 10, 3, 4, 0, 2, 2};
            for (int i = 0; i < 17; ++i)
                want += CmText("%s|%d; ", texts[i], g_creditsStyles[styles[i]]);
            ok &= runs(v.m_box) == want;
            for (int i = 0; i < CREDITS_STYLES; ++i) {
                const S_TextStyleDesc *style = GetTextStyle(g_creditsStyles[i]);
                ok &= style && style->color == 0xFFFFFFFF && style->align == 2 && style->valign == 1 && style->y == -1.0f;
            }
            // (Every style as the table has it: the font, the size, the column, joined, the flags.)
            const int fonts[14] = {2, 3, 3, 3, 3, 5, 6, 6, 6, 6, 6, 3, 3, 3};
            const float sizes[14] = {0.06f, 0.04f, 0.022f, 0.04f, 0.025f, 0.06f, 0.04f, 0.022f, 0.022f, 0.022f, 0.022f, 0.022f, 0.022f, 0.022f};
            const float xs[14] = {-1, -1, -1, -1, -1, -1, -1, -1, 0.25f, 0.5f, 0.75f, 0.25f, 0.5f, 0.75f};
            const uint32_t flags[14] = {0, 0, 0, 0, 0, 0, 0, 0, 0x18, 0x10, 0x10, 0x18, 0x10, 0x10};
            for (int i = 0; i < CREDITS_STYLES; ++i) {
                const S_TextStyleDesc *style = GetTextStyle(g_creditsStyles[i]);
                ok &= style->font == fonts[i] && style->size == sizes[i] && style->x == xs[i] &&
                      style->joins == (i >= 8 ? 1 : 0) && style->flags30 == flags[i] && style->width == 0.0f &&
                      style->below == 0.0f && style->above == 0.0f && style->unknown20 == 0 && !style->wraps;
            }
            const S_TextStyleDesc *s0 = GetTextStyle(g_creditsStyles[0]), *s9 = GetTextStyle(g_creditsStyles[9]),
                                  *s11 = GetTextStyle(g_creditsStyles[11]), *s4 = GetTextStyle(g_creditsStyles[4]);
            ok &= s0->font == 2 && s0->size == 0.06f && s0->x == -1.0f && !s0->joins && s0->flags30 == 0 && s9->font == 6 &&
                  s9->x == 0.5f && s9->joins && s9->flags30 == 0x10 && s11->font == 3 && s11->x == 0.25f &&
                  s11->flags30 == 0x18 && s4->size == 0.025f;
            // Stopped: the screen cleared three times, the movie seen, the music back, the styles released.
            g_gfLog.clear();
            const int style0 = g_creditsStyles[0];
            ok &= v.Stop() && !v.m_box &&
                  MsLogIs({"graph 64 0 1 3", "graph 70", "graph 64 0 1 3", "graph 70", "graph 64 0 1 3", "graph 70",
                           "seen credits.bik 1", "crossfade 5 2000 1234"}, g_gfLog) &&
                  !GetTextStyle(style0) && g_creditsStyles[0] == -1 && g_creditsStyles[13] == -1;
            // The music 37 already: not crossfaded; the unicode font's style.
            music = 37;
            g_vsMusicPosition = -1;
            g_gameProfile.m_unicodeFont = true;
            g_vsFiles.clear();
            g_gfLog.clear();
            ok &= v.Start(video) && MsLogIs({"exists " + dir + "credits.txt", "texts clear"}, g_gfLog) &&
                  v.m_textStyle == 0x1A && v.m_box && runs(v.m_box) == "";
            g_gameProfile.m_unicodeFont = false;
            delete v.m_box;
            v.m_box = new C_VsLogBox();
            g_gfLog.clear();
            ok &= v.Stop() && MsLogIs({"graph 64 0 1 3", "graph 70", "graph 64 0 1 3", "graph 70", "graph 64 0 1 3",
                                       "graph 70", "seen credits.bik 1", "fade 1 2000", "box gone"}, g_gfLog);
            v.m_musicTrack = -1;
            v.m_musicPosition = 5;
            g_gfLog.clear();
            ok &= v.Stop() && MrHas("fade 1 2000") && !MrHas("crossfade -1 2000 5");
            // (Not at the name's start: not the credits.)
            C_CampaignVideo *not1 = node("Xcredits.bik", false), *not2 = node("XD_Outro.bik", false), *not3 = node("Outro1.bik", false);
            for (C_CampaignVideo *n : {not1, not2, not3}) {
                newStream("nst");
                newSource("nsrc");
                v.Start(n);
                ok &= !v.m_credits && !v.m_dataDiskOutro && !v.m_outro2;
                v.Stop();
                delete n;
            }
            v.m_scrolling = false;
            delete video;
            WM(r.vsCredits, ok);
        }

        // --- a movie: the voices' player, the stream opened and fitted, the music faded, the script's
        // subtitles (their box) and voices, the volume, played ---
        {
            bool ok = true;
            C_VideoState v;
            C_CampaignVideo *video = node("Brief1.bik", true);
            S_MrObj *stream = newStream("stream");
            S_MrObj *source = newSource("src");
            S_MrObj *s1 = newSound("v1");
            S_MrObj *s2 = newSound("v2");
            newSound("v9");
            g_mrRet[{stream, 0x08}] = 1;
            g_mrRet[{s1, 0x04}] = 0x80000001u;
            g_vsVideoSize = {0, 0, 640, 480};
            details->subtitles = true;
            music = 3;
            g_vsMusicPosition = 77;
            g_vsFiles = {dir + "Brief1.txt"};
            g_vsScripts[dir + "Brief1.txt"] = {{1, "<10,20>Hello"}, {2, "S<5>v1.wav"}, {3, "<30,40>Bye"}, {4, "S<7>v2.wav"},
                                                 {5, "<5 20>no comma"}, {6, "S no time"}, {7, "plain"}, {8, "x,y>z"}};
            g_gfLog.clear();
            ok &= v.Start(video) && v.m_state == GAME_STATE_RUNNING && v.m_stream == As<ISND_stream>(stream) &&
                  v.m_voicePlayer == As<ISND_source>(source) && !v.m_credits && !v.m_scrolling && v.m_musicTrack == -1;
            ok &= MsLogIs({"stream c GameMovie\\Brief1.bik 400", "fade 0.05 2000", "stream 8", "stream 28",
                           "stream 20 5 4 790 592", "exists " + dir + "Brief1.txt", "texts load " + dir + "Brief1.txt",
                           "v1 4 Sounds\\v1.wav 0", "v1 4 sounds_DATADISK\\v1.wav 0", "v2 4 Sounds\\v2.wav 0",
                           "v9 0", "texts clear", "stream 3c 0.55", "stream 14"}, g_gfLog);
            ok &= v.m_subtitles.size() == 2 && v.m_subtitles[0].start == 10 && v.m_subtitles[0].end == 20 &&
                  v.m_subtitles[0].text == "Hello" && v.m_subtitles[1].start == 30 && v.m_subtitles[1].text == "Bye" &&
                  v.m_voices.size() == 2 && v.m_voices[0].time == 5 && v.m_voices[0].sound == As<ISND_sound>(s1) &&
                  v.m_voices[1].time == 7 && v.m_voices[1].sound == As<ISND_sound>(s2) && g_vsSounds.empty();
            ok &= v.m_box && !dynamic_cast<C_VideoTextBox *>(v.m_box) && rectOf(v.m_box) == "100 588 700 492 600 96 0.764";
            // Stopped: the stream closed and released, the player stopped and released, the voices released.
            g_gfLog.clear();
            ok &= v.Stop() && !v.m_stream && !v.m_voicePlayer && v.m_voices.empty() && v.m_subtitles.empty() && !v.m_box &&
                  MsLogIs({"graph 64 0 1 3", "graph 70", "graph 64 0 1 3", "graph 70", "graph 64 0 1 3", "graph 70",
                           "fade 1 2000", "stream 10", "stream 4", "src 8", "src 0", "v2 0", "v1 0"}, g_gfLog);
            delete video;
            // Not a briefing: 1.1 %; the movie seen. The subtitles off: none (the voices kept), no box.
            video = node("Movie.bik", false);
            stream = newStream("stream2");
            source = newSource("src2");
            S_MrObj *s3 = newSound("v3");
            g_mrRet[{stream, 0x08}] = 1;
            details->subtitles = false;
            g_vsFiles = {dir + "Movie.txt"};
            g_vsScripts[dir + "Movie.txt"] = {{1, "<10,20>Hello"}, {2, "S<5>v3.wav"}};
            g_gfLog.clear();
            ok &= v.Start(video) && GmInOrder(Join(g_gfLog), {"fade 0.011 2000", "v3 4 Sounds\\v3.wav 0", "stream2 14"}) &&
                  v.m_subtitles.empty() && v.m_voices.size() == 1 && !v.m_box && v.m_voices[0].sound == As<ISND_sound>(s3);
            g_gfLog.clear();
            ok &= v.Stop() && MrHas("seen Movie.bik 1") && MrHas("v3 0");
            delete video;
            // Not opened: no script, no box, not played.
            video = node("Missing.bik", false);
            stream = newStream("stream3");
            newSource("src3");
            g_mrRet[{stream, 0x08}] = 0;
            g_gfLog.clear();
            ok &= v.Start(video) && MsLogIs({"stream3 c GameMovie\\Missing.bik 400", "fade 0.011 2000", "stream3 8"}, g_gfLog) &&
                  !v.m_box && v.m_state == GAME_STATE_RUNNING;
            v.Stop();
            delete video;
            video = node("a.b.bik", false);
            stream = newStream("stream6");
            newSource("src6");
            g_mrRet[{stream, 0x08}] = 1;
            g_gfLog.clear();
            ok &= v.Start(video) && MrHas("exists " + dir + "a.b.txt");
            v.Stop();
            delete video;
            // No script file: nothing loaded; a name without an extension kept whole.
            video = node("NoExt", false);
            stream = newStream("stream4");
            newSource("src4");
            g_mrRet[{stream, 0x08}] = 1;
            g_vsFiles.clear();
            g_gfLog.clear();
            ok &= v.Start(video) && MrHas("exists " + dir + "NoExt.txt") && !MrHas("texts load " + dir + "NoExt.txt") &&
                  MrHas("texts clear");
            v.Stop();
            delete video;
            // A voice's buffer not made: none (fixed: the original released it).
            video = node("Movie.bik", false);
            stream = newStream("stream5");
            newSource("src5");
            g_mrRet[{stream, 0x08}] = 1;
            g_vsFiles = {dir + "Movie.txt"};
            g_gfLog.clear();
            ok &= v.Start(video) && v.m_voices.empty();
            v.Stop();
            delete video;
            WM(r.vsMovie, ok);
        }

        // --- the stream's rectangle: the movie's shape, 5 pixels in from the nearer edges ---
        {
            bool ok = true;
            C_VideoState v;
            S_MrObj *stream = newStream("fit");
            v.m_stream = As<ISND_stream>(stream);
            const auto fit = [&](int vw, int vh, int w, int h) {
                g_vsVideoSize = {0, 0, vw, vh};
                g_gfLog.clear();
                v.FitVideo(w, h);
                return g_gfLog.size() == 2 ? g_gfLog[1] : std::string("?");
            };
            ok &= fit(640, 480, 800, 600) == "fit 20 5 4 790 592";
            ok &= fit(400, 600, 800, 600) == "fit 20 203 5 393 590";
            ok &= fit(1280, 720, 1024, 768) == "fit 20 5 99 1014 570";
            ok &= fit(320, 480, 640, 480) == "fit 20 163 5 313 470";
            ok &= fit(100, 100, 300, 300) == "fit 20 5 5 290 290";
            ok &= fit(1000, 700, 800, 600) == "fit 20 5 23 790 553" && fit(800, 601, 800, 600) == "fit 20 7 5 785 590";
            v.m_credits = true;
            const auto intro = [&](int turn) {
                g_creditsIntro = turn;
                g_gfLog.clear();
                v.OpenStream("credits.bik");
                return g_gfLog.size() == 1 ? g_gfLog[0] : std::string("?");
            };
            ok &= intro(0) == "fit c GameMovie\\IntroAlps.bik 400" && g_creditsIntro == 1;
            ok &= intro(1) == "fit c GameMovie\\IntroBurma.bik 400" && g_creditsIntro == 2;
            ok &= intro(2) == "fit c GameMovie\\IntroAlps.bik 400" && g_creditsIntro == 1;
            ok &= intro(5) == "fit c GameMovie\\IntroBurma.bik 400" && g_creditsIntro == 2;
            v.m_credits = false;
            g_creditsIntro = 0;
            v.m_stream = nullptr;
            WM(r.vsFit, ok);
        }

        // --- updated: the sound module; the stream's end and position; the subtitles; the voices ---
        {
            bool ok = true;
            C_VideoState v;
            C_CampaignVideo *video = node("Brief2.bik", true);
            v.m_video = video;
            S_MrObj *stream = newStream("ust");
            S_MrObj *source = newSource("usrc");
            S_MrObj *sound = newSound("us1");
            g_vsSounds.clear();
            v.m_stream = As<ISND_stream>(stream);
            v.m_voicePlayer = As<ISND_source>(source);
            v.m_state = GAME_STATE_RUNNING;
            v.m_box = new C_TextBox();
            v.m_subtitles = {{10, 20, C_str("Hello")}, {15, 25, C_str("Over")}, {40, 50, C_str("Late")}};
            v.m_voices = {{5, As<ISND_sound>(sound)}};
            g_mrRet[{stream, 0x2C}] = 0;
            const auto update = [&](int position, int dt) {
                g_vsPosition = position;
                g_gfLog.clear();
                v.Update(dt);
            };
            // 0.4 s: nothing shown, no voice.
            update(400, 16);
            ok &= MsLogIs({"sound update 16", "ust 2c", "ust 38"}, g_gfLog) && v.m_time == 400 && v.m_subtitle == -1 &&
                  v.m_box->NumRuns() == 0 && v.m_nextVoice == 0;
            // 0.6 s: the voice (the driver's volume the options').
            update(600, 16);
            ok &= MsLogIs({"sound update 16", "ust 2c", "ust 38", "snd 14 0.5", "usrc 4 us1", "usrc 20 1", "usrc c 0 0"}, g_gfLog) &&
                  v.m_nextVoice == 1;
            update(700, 16);
            ok &= g_gfLog.size() == 3;
            v.m_nextVoice = 0;
            update(500, 16);
            ok &= v.m_nextVoice == 0;
            update(503, 16);
            ok &= v.m_nextVoice == 1;
            // 1.2 s: the first; 1.6 s: kept (shown already); 2.1 s: gone, the second; 2.6 s: none.
            update(1200, 16);
            ok &= v.m_subtitle == 0 && v.m_box->GetRunText(0) == "Hello" && v.m_box->NumRuns() == 1;
            update(1600, 16);
            ok &= v.m_subtitle == 0 && v.m_box->GetRunText(0) == "Hello";
            update(2100, 16);
            ok &= v.m_subtitle == 1 && v.m_box->GetRunText(0) == "Over" && v.m_box->NumRuns() == 1;
            update(2500, 16);
            ok &= v.m_subtitle == 1;
            update(2600, 16);
            ok &= v.m_subtitle == -1 && v.m_box->NumRuns() == 0;
            // (Its edges: not shown at its start or end exactly.)
            update(4000, 16);
            ok &= v.m_subtitle == -1;
            update(4001, 16);
            ok &= v.m_subtitle == 2;
            update(5000, 16);
            ok &= v.m_subtitle == 2;
            update(5001, 16);
            ok &= v.m_subtitle == -1;
            // Both of a time: the last shown.
            v.m_subtitles = {{10, 30, C_str("A")}, {10, 30, C_str("B")}};
            update(1500, 16);
            ok &= v.m_subtitle == 1 && v.m_box->GetRunText(0) == "B";
            // The stream lost (-1): stopped, the screen cleared, ended; its position still read.
            g_mrRet[{stream, 0x2C}] = static_cast<uint32_t>(-1);
            update(1600, 16);
            ok &= GmInOrder(Join(g_gfLog), {"ust 2c", "ust 18", "graph 64 0 1 3", "graph 70", "ust 38"}) &&
                  v.m_state == GAME_STATE_ENDED;
            // Finished, a briefing: "Press space to continue ..." waited for (the rest not done).
            v.m_state = GAME_STATE_RUNNING;
            g_mrRet[{stream, 0x2C}] = 1;
            v.m_nextVoice = 0;
            v.m_box->AddText(C_str("extra"), 0);
            update(3000, 16);
            ok &= MsLogIs({"sound update 16", "ust 2c"}, g_gfLog) && v.m_ending && v.m_box->NumRuns() == 1 &&
                  v.m_box->GetRunText(0) == g_texts.Get(0x7F8) && v.m_nextVoice == 0 && v.m_time == 1600 &&
                  v.m_state == GAME_STATE_RUNNING;
            // Waiting: the stream no more polled, the subtitles left; the voices still played.
            update(3100, 16);
            ok &= MsLogIs({"sound update 16", "snd 14 0.5", "usrc 4 us1", "usrc 20 1", "usrc c 0 0"}, g_gfLog) &&
                  v.m_box->NumRuns() == 1;
            // No box: nothing waited for.
            v.m_ending = false;
            delete v.m_box;
            v.m_box = nullptr;
            update(3200, 16);
            ok &= MsLogIs({"sound update 16", "ust 2c"}, g_gfLog) && !v.m_ending;
            // Finished, not a briefing: ended.
            video->*VsGet(S_VsBriefingTag()) = false;
            update(3300, 16);
            ok &= GmInOrder(Join(g_gfLog), {"ust 2c", "ust 18", "graph 70", "ust 38"}) && v.m_state == GAME_STATE_ENDED;
            // D_Outro finished: its end reached (not ended), its position read.
            v.m_state = GAME_STATE_RUNNING;
            v.m_dataDiskOutro = true;
            update(3400, 16);
            ok &= MsLogIs({"sound update 16", "ust 2c", "ust 38"}, g_gfLog) && v.m_ending && v.m_state == GAME_STATE_RUNNING;
            // No player: no voice.
            v.m_voicePlayer = nullptr;
            v.m_nextVoice = 0;
            update(3500, 16);
            ok &= v.m_nextVoice == 0 && MsLogIs({"sound update 16"}, g_gfLog);
            v.m_stream = nullptr;
            v.m_voices.clear();
            delete video;
            WM(r.vsUpdate, ok);
        }

        // --- the credits' scroll: by the time; their end (the last line gone): 3 s more; D_Outro's fade
        // out over 1 s, ended after 1.2 s ---
        {
            bool ok = true;
            C_VideoState v;
            v.m_state = GAME_STATE_RUNNING;
            v.m_scrolling = true;
            v.m_credits = true;
            g_creditsIntro = 7;
            v.MakeCreditsBox(800, 600);
            ok &= v.m_scrollSpeed == static_cast<float>(800) * 0.0009765625f * 50.0f * 0.001f && g_creditsIntro == 7;
            auto *box = static_cast<S_VsBoxPeek *>(v.m_box);
            g_gfLog.clear();
            v.Update(100);
            const float scrolled = static_cast<float>(100) * v.m_scrollSpeed + -546.875f;
            ok &= v.m_scroll == scrolled && box->m_scroll == static_cast<int>(scrolled) && MsLogIs({"sound update 100"}, g_gfLog);
            // (No lines: finished at once.) 3 s counted.
            ok &= v.m_fadeTime == 100 && v.m_state == GAME_STATE_RUNNING;
            v.Update(2899);
            ok &= v.m_fadeTime == 2999 && v.m_state == GAME_STATE_RUNNING;
            v.Update(1);
            ok &= v.m_fadeTime == 3000 && v.m_state == GAME_STATE_ENDED;
            // Lines left: not counted.
            v.m_state = GAME_STATE_RUNNING;
            v.m_fadeTime = 0;
            box->m_lines.push_back(nullptr);
            v.m_scroll = -546.875f;
            v.Update(50);
            ok &= v.m_fadeTime == 0 && box->m_scroll == static_cast<int>(static_cast<float>(50) * v.m_scrollSpeed + -546.875f);
            box->m_lines.clear();
            // D_Outro: its fade once its end is reached (the colour's alpha from 255 down).
            v.m_credits = false;
            v.m_dataDiskOutro = true;
            v.Update(500);
            ok &= v.m_fadeTime == 0 && box->m_colour == 0;
            v.m_ending = true;
            v.Update(500);
            ok &= v.m_fadeTime == 500 && box->m_colour == 0x80FFFFFFu && v.m_state == GAME_STATE_RUNNING;
            v.Update(250);
            ok &= v.m_fadeTime == 750 && box->m_colour == 0x40FFFFFFu;
            v.Update(250);
            ok &= v.m_fadeTime == 1000 && box->m_colour == 0x00FFFFFFu && v.m_state == GAME_STATE_RUNNING;
            v.Update(199);
            ok &= box->m_colour == 0x00FFFFFFu && v.m_state == GAME_STATE_RUNNING;
            v.Update(1);
            ok &= v.m_fadeTime == 1200 && v.m_state == GAME_STATE_ENDED;
            for (int &style : g_creditsStyles) {
                ReleaseTextStyle(style);
                style = -1;
            }
            WM(r.vsScroll, ok);
        }

        // --- drawn: the screen cleared, the stream's picture, the box (render state 0 on), the state back ---
        {
            bool ok = true;
            C_VideoState v;
            S_MrObj *stream = newStream("rst");
            g_mrRet[{graph, 0x78}] = 7;
            g_gfLog.clear();
            v.Render();
            ok &= MsLogIs({"graph 78 0", "graph 64 0 1 3", "graph 74 0 7"}, g_gfLog);
            v.m_stream = As<ISND_stream>(stream);
            v.m_box = new C_VsLogBox();
            g_gfLog.clear();
            v.Render();
            ok &= MsLogIs({"graph 78 0", "graph 64 0 1 3", "graph 68", "rst 50 0", "graph 6c", "graph 74 0 1", "graph 68",
                           "box render", "graph 6c", "graph 74 0 7"}, g_gfLog);
            delete v.m_box;
            v.m_box = nullptr;
            v.m_stream = nullptr;
            g_mrRet.erase({graph, 0x78});
            WM(r.vsRender, ok);
        }

        // --- the keys: Escape (Space once the end is reached): the stream stopped, cleared, ended ---
        {
            bool ok = true;
            C_VideoState v;
            S_MrObj *stream = newStream("kst");
            const auto key = [&](int k) {
                v.m_state = GAME_STATE_RUNNING;
                g_mrRet[{graph, 0xA0}] = static_cast<uint32_t>(k);
                g_gfLog.clear();
                float held[4] = {}, pressed[4] = {};
                return v.HandleInput(held, pressed);
            };
            v.m_stream = As<ISND_stream>(stream);
            ok &= key(1) && MsLogIs({"graph a0", "kst 18", "graph 64 0 1 3", "graph 70"}, g_gfLog) && v.m_state == GAME_STATE_ENDED;
            ok &= key(0x39) && MsLogIs({"graph a0"}, g_gfLog) && v.m_state == GAME_STATE_RUNNING;
            ok &= key(0x1C) && MsLogIs({"graph a0"}, g_gfLog) && v.m_state == GAME_STATE_RUNNING;
            v.m_ending = true;
            ok &= key(0x39) && MsLogIs({"graph a0", "kst 18", "graph 64 0 1 3", "graph 70"}, g_gfLog) && v.m_state == GAME_STATE_ENDED;
            ok &= key(0x1C) && v.m_state == GAME_STATE_RUNNING;
            v.m_stream = nullptr;
            ok &= key(1) && MsLogIs({"graph a0", "graph 64 0 1 3", "graph 70"}, g_gfLog) && v.m_state == GAME_STATE_ENDED;
            g_mrRet.erase({graph, 0xA0});
            ok &= v.GetTypeId() == 7;
            WM(r.vsInput, ok);
        }

        // --- the movie state: played (its node, the sequence), ended: finished; stopped: the node deleted;
        // Outro2 over the main menu: its music saved ---
        {
            bool ok = true;
            C_MovieState m;
            S_MrObj *stream = newStream("mst");
            newSource("msrc");
            g_mrRet[{stream, 0x08}] = 0;
            g_gfLog.clear();
            ok &= !m.Play("Logo1.bik", 3) && m.GetSequence() == 3 && m.m_video && std::string(m.m_video->GetFileName()) == "Logo1.bik" &&
                  !m.m_video->IsBriefing() && m.m_state == GAME_STATE_RUNNING && m.GetTypeId() == 10 &&
                  MrHas("mst c GameMovie\\Logo1.bik 400");
            g_mrRet[{graph, 0xA0}] = 1;
            float held[4] = {}, pressed[4] = {};
            ok &= m.HandleInput(held, pressed) && m.m_state == GAME_STATE_MOVIE_FINISHED;
            m.m_state = GAME_STATE_RUNNING;
            g_mrRet[{graph, 0xA0}] = 0;
            ok &= m.HandleInput(held, pressed) && m.m_state == GAME_STATE_RUNNING;
            g_mrRet[{stream, 0x2C}] = static_cast<uint32_t>(-1);
            m.Update(10);
            ok &= m.m_state == GAME_STATE_MOVIE_FINISHED;
            m.m_state = GAME_STATE_RUNNING;
            g_mrRet[{stream, 0x2C}] = 0;
            m.Update(10);
            ok &= m.m_state == GAME_STATE_RUNNING;
            g_gfLog.clear();
            ok &= m.Stop() && !m.m_video && MrHas("seen Logo1.bik 1") && MrHas("video gone");
            // Outro2 over the main menu: its music saved, 37; over another state: not saved.
            C_VsState menu(GAME_STATE_TYPE_MAIN_MENU), other(GAME_STATE_TYPE_SQUAD_MENU);
            m.SetReturnState(&menu);
            music = 9;
            g_vsMusicPosition = 55;
            stream = newStream("ost");
            newSource("osrc");
            g_mrRet[{stream, 0x08}] = 0;
            g_gfLog.clear();
            m.Play("Outro2.bik", 0);
            ok &= m.m_outro2 && m.m_musicTrack == 9 && m.m_musicPosition == 55 && MrHas("crossfade 37 2000 0") &&
                  !MrHas("fade 0.011 2000") && m.GetReturnState() == &menu;
            m.Stop();
            m.m_musicTrack = m.m_musicPosition = -1;
            m.SetReturnState(&other);
            stream = newStream("ost2");
            newSource("osrc2");
            g_mrRet[{stream, 0x08}] = 0;
            m.Play("Outro2.bik", 0);
            ok &= m.m_musicTrack == -1;
            m.Stop();
            // A video state's Outro2 (not a movie state): not saved.
            C_VideoState v;
            C_CampaignVideo *video = node("Outro2.bik", false);
            stream = newStream("ost3");
            newSource("osrc3");
            g_mrRet[{stream, 0x08}] = 0;
            v.Start(video);
            ok &= v.m_musicTrack == -1 && v.m_outro2;
            v.Stop();
            delete video;
            // D_Outro: scrolled with the credits' box, its script parsed as the credits'.
            video = node("D_Outro.bik", false);
            stream = newStream("dst");
            newSource("dsrc");
            g_mrRet[{stream, 0x08}] = 1;
            g_vsFiles = {dir + "D_Outro.txt"};
            g_vsScripts[dir + "D_Outro.txt"] = {{1, "<TI>The end"}, {2, "<1,2>not a subtitle"}};
            details->subtitles = true;
            g_gfLog.clear();
            ok &= v.Start(video) && v.m_dataDiskOutro && v.m_scrolling && dynamic_cast<C_VideoTextBox *>(v.m_box) &&
                  runs(v.m_box) == CmText("The end|%d; ", g_creditsStyles[0]) && v.m_subtitles.empty() &&
                  GmInOrder(Join(g_gfLog), {"dst 20", "texts load", "dst 3c 0.55", "dst 14"});
            v.Stop();
            delete video;
            WM(r.vsMovieState, ok);
        }

        g_vsScripts.clear();
        g_vsFiles.clear();
        g_vsSounds.clear();
        g_mrRet.clear();
        g_gameProfile.m_unicodeFont = oldUnicode;
        g_foLanguage = oldLanguage;
        *details = oldDetails;
        g_gameProfile.m_player = oldPlayer;
        music = oldMusic;
        *reinterpret_cast<void **>(g_soundModuleMemory) = oldSoundVt;
        for (int i = 0; i < 10; ++i)
            g_igraphVtable[slots[i] / 4] = oldGraph[i];
        g_mrSndDriver.vtable = oldDriverVt;
        g_mrNames.erase(&g_mrSndDriver);
        g_mrNames.erase(graph);
    }
    // ===== part 9g-v: the debriefing =====
    {
        S_PlayMode playMode;
        S_PlayMode *const oldPlayMode = g_gameProfile.m_playMode;
        g_gameProfile.m_playMode = &playMode;
        S_MissionSquad squad;
        S_MissionSquad *const oldSquad = g_gameProfile.m_missionSquad;
        g_gameProfile.m_missionSquad = &squad;
        alignas(16) static uint8_t storeMemory[64];
        C_HumanStore *const oldStore = g_gameProfile.m_humanStore;
        g_gameProfile.m_humanStore = reinterpret_cast<C_HumanStore *>(storeMemory);
        C_PlayerProfile player;
        C_PlayerProfile *const oldPlayer = g_gameProfile.m_player;
        g_gameProfile.m_player = &player;
        const std::vector<S_SoldierProfile *> oldProfiles = g_epProfiles;
        // Profiles in zeroed memory (their constructor is not linked): a rank, medals, the counts.
        alignas(16) static uint8_t profileMemory[8][sizeof(S_SoldierProfile)];
        int made = 0;
        const auto profile = [&](int rank, std::vector<int> medals, int missions3C, int missions40, bool dead) {
            auto *p = reinterpret_cast<S_SoldierProfile *>(profileMemory[made++]);
            std::memset(static_cast<void *>(p), 0, sizeof(S_SoldierProfile));
            p->unknown24 = rank;
            for (int m : medals)
                p->unknown28.push_back(m);
            p->unknown3C = missions3C;
            p->unknown40 = missions40;
            p->dead = dead;
            return p;
        };
        const auto soldier = [](int profileIndex, int experience) {
            auto *s = new S_SquadSoldier();
            s->profileIndex = profileIndex;
            s->progress.experience = experience;
            s->unknown34 = -1;
            s->unknown38 = -1;
            return s;
        };
        const auto clearSquad = [&]() {
            for (S_SquadSoldier *s : squad.states)
                delete s;
            squad.states.clear();
        };

        // --- the medal due ---
        {
            bool ok = true;
            const auto medal = [&](int current, int operation, S_SoldierProfile *p, int experience,
                                   std::vector<S_SoldierProfile *> roster = {}) {
                campaign->m_current = current;
                g_csOperation = operation;
                g_epProfiles = roster;
                S_SquadSoldier s;
                s.unknown38 = -1;
                s.progress.experience = experience;
                C_DebriefingMenu::AwardMedal(p, &s);
                return s.unknown38;
            };
            made = 0;
            // The first campaign's operations: 5 and 6 medal 9; 4 medal 11; 2 medal 10, then 3 by the missions
            // and the rank.
            ok &= medal(0, 5, profile(0, {}, 0, 0, false), 0) == 9 && medal(0, 6, profile(0, {}, 0, 0, false), 0) == 9;
            ok &= medal(1, 5, profile(0, {}, 0, 0, false), 0) == -1 && medal(0, 5, profile(0, {9}, 0, 0, false), 0) == -1;
            ok &= medal(0, 4, profile(0, {}, 0, 0, false), 0) == 11 && medal(0, 4, profile(0, {11}, 0, 0, false), 0) == -1;
            made = 0;
            ok &= medal(0, 2, profile(0, {}, 0, 0, false), 0) == 10;
            ok &= medal(0, 2, profile(3, {10}, 0, 2, false), 0) == 3 && medal(0, 2, profile(2, {10}, 0, 2, false), 0) == 5;
            ok &= medal(0, 2, profile(2, {10}, 0, 3, false), 0) == 3 && medal(0, 2, profile(1, {10}, 0, 3, false), 0) == 5;
            made = 0;
            ok &= medal(0, 2, profile(1, {10}, 0, 4, false), 0) == 3 && medal(0, 2, profile(0, {10}, 0, 4, false), 0) == 5;
            ok &= medal(0, 2, profile(0, {10}, 0, 5, false), 0) == 3 && medal(0, 2, profile(9, {10}, 0, 6, false), 0) == -1;
            ok &= medal(0, 2, profile(9, {10, 3}, 0, 2, false), 0) == 5 && medal(0, 2, profile(0, {10}, 9, 9, false), 0) == -1;
            // (A dead soldier: none of the operations'.)
            made = 0;
            ok &= medal(0, 5, profile(0, {}, 0, 0, true), 0) == -1;
            // The experience: more than 14999 medal 8 (none holding it), then 7 (more than 11999), 2, 0, 1.
            ok &= medal(1, 0, profile(0, {}, 0, 0, false), 15000) == 7 && medal(1, 0, profile(0, {7}, 0, 0, false), 15000) == 2;
            ok &= medal(1, 0, profile(0, {7, 2, 0, 1}, 0, 0, false), 15000) == 8;
            made = 0;
            S_SoldierProfile *holder = profile(0, {8}, 0, 0, false), *deadHolder = profile(0, {8}, 0, 0, true);
            ok &= medal(1, 0, profile(0, {7, 2, 0, 1}, 0, 0, false), 15000, {holder}) == -1;
            ok &= medal(1, 0, profile(0, {7, 2, 0, 1}, 0, 0, false), 15000, {deadHolder}) == 8;
            ok &= medal(1, 0, profile(0, {}, 0, 0, true), 15000, {holder}) == 8 && medal(1, 0, profile(0, {}, 0, 0, true), 15000, {deadHolder}) == -1;
            S_SoldierProfile *nonHolder = profile(0, {7}, 0, 0, false);
            ok &= medal(1, 0, profile(0, {7, 2, 0, 1}, 0, 0, false), 15000, {nonHolder, nullptr}) == 8;
            made = 0;
            ok &= medal(1, 0, profile(3, {5}, 0, 1, false), 0) == -1;
            ok &= medal(1, 0, profile(0, {7, 2, 0, 1}, 0, 0, false), 14999) == -1;
            made = 0;
            ok &= medal(1, 0, profile(0, {}, 0, 0, false), 12000) == 7 && medal(1, 0, profile(0, {}, 0, 0, false), 11999) == 2;
            ok &= medal(1, 0, profile(0, {}, 0, 0, false), 11000) == 2 && medal(1, 0, profile(0, {}, 0, 0, false), 10999) == 0;
            ok &= medal(1, 0, profile(0, {}, 0, 0, false), 10000) == 0 && medal(1, 0, profile(0, {}, 0, 0, false), 9999) == 1;
            made = 0;
            ok &= medal(1, 0, profile(0, {}, 0, 0, false), 9000) == 1 && medal(1, 0, profile(0, {}, 0, 0, false), 8999) == -1;
            // (A dead soldier: only medal 8.)
            ok &= medal(1, 0, profile(0, {}, 0, 0, true), 12000) == -1;
            // The missions (+0x40) by the rank: medal 5; the missions (+0x3C) by the rank: medal 4 (not in
            // operation 2; over 5).
            ok &= medal(1, 0, profile(3, {}, 0, 1, false), 0) == 5 && medal(1, 0, profile(2, {}, 0, 1, false), 0) == -1;
            made = 0;
            ok &= medal(1, 0, profile(2, {}, 0, 2, false), 0) == 5 && medal(1, 0, profile(1, {}, 0, 3, false), 0) == 5;
            ok &= medal(1, 0, profile(0, {}, 0, 4, false), 0) == 5 && medal(1, 0, profile(9, {}, 0, 5, false), 0) == -1;
            ok &= medal(1, 0, profile(3, {}, 5, 0, false), 0) == 4 && medal(1, 0, profile(2, {}, 5, 0, false), 0) == -1;
            made = 0;
            ok &= medal(1, 0, profile(2, {}, 6, 0, false), 0) == 4 && medal(1, 0, profile(1, {}, 7, 0, false), 0) == 4;
            ok &= medal(1, 0, profile(0, {}, 8, 0, false), 0) == 4 && medal(1, 0, profile(9, {}, 9, 0, false), 0) == -1;
            ok &= medal(1, 2, profile(3, {}, 5, 0, false), 0) == -1 && medal(1, 0, profile(3, {4}, 5, 0, false), 0) == -1;
            made = 0;
            ok &= medal(1, 0, profile(3, {}, 5, 1, false), 0) == 4 && medal(1, 0, profile(3, {4}, 5, 1, false), 0) == 5;
            ok &= medal(1, 0, profile(3, {5}, 5, 1, false), 0) == 4 && medal(1, 0, profile(3, {}, 8, 1, false), 9000) == 1;
            ok &= medal(1, 0, profile(0, {1}, 0, 4, false), 9000) == 5;
            // (The medal 8 set, then overridden by 5 or 4.)
            made = 0;
            ok &= medal(1, 0, profile(0, {7, 2, 0, 1}, 8, 4, false), 15000) == 4;
            g_epProfiles.clear();
            WM(r.dbMedal, ok);
        }

        // --- the results: a dead soldier's items back; the experience, the missions; a promotion due (not
        // past rank 7); the medal (from the second mission) ---
        {
            bool ok = true;
            made = 0;
            S_SoldierProfile *p0 = profile(0, {}, 3, 4, false), *p1 = profile(2, {}, 0, 0, true), *p2 = profile(7, {}, 0, 0, false);
            S_SoldierProfile *p3 = profile(1, {}, 0, 0, false);
            p0->unknown38 = 6000;
            p2->unknown38 = 900000;
            p3->unknown38 = 20000;
            g_epProfiles = {p0, p1, p2, p3};
            squad.states = {soldier(0, 4001), soldier(1, 100), soldier(2, 5), soldier(3, 5000)};
            campaign->m_current = 1;
            g_csOperation = 0;
            g_esMission = 0;
            g_gfLog.clear();
            C_DebriefingMenu::ApplyResults();
            ok &= MsLogIs({"mission number 1", "return all 1"}, g_gfLog);
            ok &= p0->unknown38 == 10001 && p0->unknown3C == 4 && p0->unknown40 == 5 && squad.states[0]->unknown34 == 1;
            ok &= p1->unknown38 == 100 && squad.states[1]->unknown34 == -1 && p2->unknown38 == 900005 &&
                  squad.states[2]->unknown34 == -1 && p3->unknown38 == 25000 && squad.states[3]->unknown34 == -1;
            ok &= squad.states[0]->unknown38 == -1 && squad.states[3]->unknown38 == -1;
            // (At 10000 exactly: none; past the next rank's too: one rank.)
            p0->unknown38 = 5999;
            p3->unknown38 = 20001;
            squad.states[0]->unknown34 = -1;
            g_esMission = 1;
            C_DebriefingMenu::ApplyResults();
            ok &= p0->unknown38 == 10000 && squad.states[0]->unknown34 == -1 && squad.states[3]->unknown34 == 2;
            // The second mission on: the medals (rank 7, 2 missions: medal 5).
            ok &= squad.states[0]->unknown38 == -1 && squad.states[1]->unknown38 == -1 && squad.states[2]->unknown38 == 5 &&
                  squad.states[3]->unknown38 == -1;
            clearSquad();
            g_epProfiles.clear();
            WM(r.dbResults, ok);
        }

        // --- kept: the promotions and medals given, the progress added (capped at 100), the dead off the
        // squad, the living's average to the roster's living (by a random 60 .. 140 %) ---
        {
            bool ok = true;
            made = 0;
            S_SoldierProfile *p0 = profile(1, {3}, 0, 0, false), *p1 = profile(2, {}, 0, 0, true), *p2 = profile(4, {}, 0, 0, false);
            S_SoldierProfile *p3 = profile(0, {}, 0, 0, false), *p4 = profile(0, {}, 0, 0, true);
            g_epProfiles = {p0, p1, p2, p3, p4};
            for (S_SoldierProfile *p : g_epProfiles) {
                for (int k = 0; k < 9; ++k)
                    p->stats[k] = 10.0f * static_cast<float>(k) + 3.25f;
                p->unknown38 = 500;
            }
            p2->stats[1] = 99.5f;
            p2->stats[6] = 99.9375f;
            S_SquadSoldier *s0 = soldier(0, 3000), *s1 = soldier(1, 1000), *s2 = soldier(2, 2001);
            const float gains[9] = {1.5f, 2.25f, 0.75f, 0.5f, 1.25f, 9.0f, 0.125f, 2.0f, 2.5f};
            for (S_SquadSoldier *s : {s0, s1, s2}) {
                S_SoldierProgress &p = s->progress;
                p.unknown00 = gains[0];
                p.unknown04 = gains[1] * static_cast<float>(s->profileIndex + 1);
                p.endurance = gains[2];
                p.morale = gains[3];
                p.shooting = gains[4];
                p.stealth = gains[6];
                p.firstAid = gains[7];
                p.lockpick = gains[8];
            }
            s0->unknown34 = 2;
            s0->unknown38 = 7;
            s2->unknown38 = 1;
            squad.states = {s0, s1, s2};
            // The original's arithmetic transcribed (the same random numbers).
            std::vector<std::array<float, 9>> want;
            std::vector<int> wantExperience;
            for (S_SoldierProfile *p : g_epProfiles) {
                std::array<float, 9> st;
                for (int k = 0; k < 9; ++k)
                    st[static_cast<size_t>(k)] = p->stats[k];
                want.push_back(st);
                wantExperience.push_back(p->unknown38);
            }
            float sum[9] = {};
            int living = 0, sumExperience = 0;
            for (S_SquadSoldier *s : {s0, s1, s2}) {
                const size_t i = static_cast<size_t>(s->profileIndex);
                const S_SoldierProgress &p = s->progress;
                const float add[9] = {p.unknown00, p.unknown04, p.endurance, p.morale, p.shooting, 0.0f, p.stealth, p.firstAid, p.lockpick};
                for (int k = 0; k < 9; ++k)
                    if (k != 5)
                        want[i][static_cast<size_t>(k)] += add[k];
                for (int k : {1, 2, 3, 4, 6, 7, 8})
                    if (want[i][static_cast<size_t>(k)] > 100.0f)
                        want[i][static_cast<size_t>(k)] = 100.0f;
                if (!g_epProfiles[i]->dead) {
                    ++living;
                    sumExperience += p.experience;
                    for (int k = 1; k < 9; ++k)
                        if (k != 5)
                            sum[k] += add[k];
                }
            }
            const float share = 1.0f / static_cast<float>(living);
            for (int k = 1; k < 9; ++k)
                sum[k] *= share;
            sumExperience /= living;
            std::srand(4321);
            std::vector<int> randoms;
            for (int i = 0; i < 5 * 9; ++i)
                randoms.push_back(std::rand());
            size_t next = 0;
            for (size_t i = 0; i < g_epProfiles.size(); ++i) {
                if (g_epProfiles[i]->dead)
                    continue;
                const auto factor = [&]() {
                    const float f = static_cast<float>(randoms[next++]) * 3.051851e-05f * 0.8f;
                    return static_cast<float>(static_cast<double>(f) + 0.6);
                };
                wantExperience[i] = static_cast<int>(factor() * static_cast<float>(sumExperience) + static_cast<float>(wantExperience[i]));
                for (int k : {0, 1, 2, 3, 4, 6, 7, 8})
                    want[i][static_cast<size_t>(k)] = factor() * sum[k] + want[i][static_cast<size_t>(k)];
                for (int k : {1, 2, 3, 4, 6, 7, 8})
                    if (want[i][static_cast<size_t>(k)] > 100.0f)
                        want[i][static_cast<size_t>(k)] = 100.0f;
            }
            std::srand(4321);
            C_DebriefingMenu::StoreProgress();
            ok &= p0->unknown24 == 2 && p0->unknown28 == std::vector<int>({3, 7}) && p2->unknown28 == std::vector<int>({1}) &&
                  p1->unknown28.empty() && s0->unknown34 == -1 && s0->unknown38 == -1 && s2->unknown38 == -1;
            ok &= squad.states == std::vector<S_SquadSoldier *>({s0, s2});
            for (size_t i = 0; i < g_epProfiles.size(); ++i) {
                ok &= g_epProfiles[i]->unknown38 == wantExperience[i];
                for (int k = 0; k < 9; ++k)
                    ok &= g_epProfiles[i]->stats[k] == want[i][static_cast<size_t>(k)];
            }
            ok &= p2->stats[1] == 100.0f && p2->stats[6] == 100.0f && p1->stats[5] == 53.25f && p0->stats[5] == 53.25f &&
                  p2->unknown24 == 4 && p1->unknown24 == 2;
            // (The roster's first 30 only.)
            {
                alignas(16) static uint8_t bigMemory[31][sizeof(S_SoldierProfile)];
                std::memset(static_cast<void *>(bigMemory), 0, sizeof bigMemory);
                std::vector<S_SoldierProfile *> big;
                for (auto &memory : bigMemory)
                    big.push_back(reinterpret_cast<S_SoldierProfile *>(memory));
                const std::vector<S_SoldierProfile *> kept = g_epProfiles;
                g_epProfiles = big;
                squad.states = {soldier(0, 1000)};
                C_DebriefingMenu::StoreProgress();
                ok &= big[29]->unknown38 > 0 && big[30]->unknown38 == 0 && big[30]->stats[1] == 0.0f;
                clearSquad();
                g_epProfiles = kept;
            }
            // (No living soldier: no averages.)
            p0->dead = true;
            p2->dead = true;
            const float before = p3->stats[2];
            squad.states = {soldier(0, 10)};
            C_DebriefingMenu::StoreProgress();
            ok &= squad.states.empty() && p3->stats[2] == before;
            clearSquad();
            g_epProfiles.clear();
            WM(r.dbStore, ok);
        }

        // --- started: the campaign's diary kept (its results for the debriefing kind), a single mission's dropped ---
        {
            bool ok = true;
            C_DebriefingMenu m;
            playMode.singleMission = 0;
            m.m_mode = 5;
            g_gfLog.clear();
            ok &= m.OnStart(nullptr) && MsLogIs({"diary commit", "mission number 1"}, g_gfLog);
            m.m_mode = 6;
            g_gfLog.clear();
            ok &= m.OnStart(nullptr) && MsLogIs({"diary commit"}, g_gfLog);
            playMode.singleMission = 1;
            m.m_mode = 5;
            g_gfLog.clear();
            ok &= m.OnStart(nullptr) && MsLogIs({"diary clear"}, g_gfLog) && !C_DebriefingMenu::IsCampaign();
            playMode.singleMission = 0;
            ok &= C_DebriefingMenu::IsCampaign() && m.GetTypeId() == 5;
            g_gfLog.clear();
            m.OnStop();
            ok &= g_gfLog.empty();
            WM(r.dbStart, ok);
        }

        // --- the messages: Escape (the diary closed; the campaign's save question; a single mission's), the
        // continue and the save and quit (the results kept, the game saved), the restart, the replay ---
        {
            bool ok = true;
            C_DebriefingMenu m;
            m.m_state = GAME_STATE_RUNNING;
            S_ControlMessage message;
            const auto send = [&](uint32_t id) {
                message.id = id;
                g_gfLog.clear();
                g_controlMessages.clear();
                m.OnMessage(&message);
                return CsQueue();
            };
            C_MsScreen debriefing(nullptr, "dbd", 0x04000000), diary(nullptr, "dbdiary", 0x08000000);
            m.m_messageBox = nullptr;
            playMode.singleMission = 0;
            campaign->m_current = 1;
            g_esMission = 4;
            g_dbProgress = 4;
            // The diary on top: closed.
            g_menuScreens = {&debriefing, &diary};
            ok &= send(MENU_MSG_ESCAPE) == "0 2000003 0 0; " && g_gfLog.empty();
            // The debriefing on top, none, a null top: the campaign's question (the mission played before:
            // overwrite).
            g_menuScreens = {&debriefing};
            ok &= send(MENU_MSG_ESCAPE) == "" && MsLogIs({"mission number 1", "progress 1"}, g_gfLog);
            g_menuScreens.clear();
            g_dbProgress = 5;
            send(CONTROL_NOTIFY_CLICK | MENU_MSG_ESCAPE);
            ok &= MsLogIs({"mission number 1", "progress 1"}, g_gfLog);
            g_menuScreens = {nullptr};
            send(MENU_MSG_ESCAPE);
            ok &= MsLogIs({"mission number 1", "progress 1"}, g_gfLog);
            g_menuScreens.clear();
            // (A single mission: "ARE YOU SURE?".)
            playMode.singleMission = 1;
            ok &= send(MENU_MSG_ESCAPE) == "" && g_gfLog.empty();
            ok &= send(0x2000034) == "" && g_gfLog.empty() && send(0x20000C8) == "" && g_gfLog.empty();
            playMode.singleMission = 0;
            // Continue: the results kept (the debriefing's kind; a mission), the game saved, the next step.
            m.m_mode = 5;
            made = 0;
            g_epProfiles = {profile(0, {}, 0, 0, false)};
            squad.states = {soldier(0, 7)};
            ok &= send(0x2000034) == "0 2000008 1 0; " &&
                  MsLogIs({"mission number 1", "set progress 1 4", "save save1_4 0"}, g_gfLog) && g_epProfiles[0]->stats[1] != 0.0f;
            ok &= send(0x20000C8) == "0 2000009 0 0; " && MsLogIs({"mission number 1", "set progress 1 4", "save save1_4 0"}, g_gfLog);
            // (No mission, not the debriefing's kind: only the step.)
            g_esMission = -1;
            ok &= send(0x2000034) == "0 2000008 1 0; " && MsLogIs({"mission number 1"}, g_gfLog);
            g_esMission = 4;
            m.m_mode = 6;
            ok &= send(0x20000C8) == "0 2000009 0 0; " && MsLogIs({"mission number 1"}, g_gfLog);
            campaign->m_current = -1;
            m.m_mode = 5;
            ok &= send(0x2000034) == "0 2000008 1 0; " && MsLogIs({"mission number -1"}, g_gfLog);
            campaign->m_current = 1;
            // Restart: the save before the mission loaded, the mission's step; the first: the game reset.
            m.m_state = GAME_STATE_RUNNING;
            ok &= send(0x2000075) == "" && MsLogIs({"mission number 1", "load save1_3 0", "advance"}, g_gfLog) &&
                  m.m_state == GAME_STATE_ENDED;
            m.m_state = GAME_STATE_RUNNING;
            g_dbLoaded = false;
            ok &= send(0x2000075) == "" && MsLogIs({"mission number 1", "load save1_3 0"}, g_gfLog) && m.m_state == GAME_STATE_RUNNING;
            g_dbLoaded = true;
            g_dbAdvanced = false;
            send(0x2000075);
            ok &= m.m_state == GAME_STATE_RUNNING;
            g_esMission = 1;
            g_dbAdvanced = true;
            m.m_state = GAME_STATE_RUNNING;
            send(0x2000075);
            ok &= MsLogIs({"mission number 1", "load save1_0 0", "advance"}, g_gfLog) && m.m_state == GAME_STATE_ENDED;
            g_dbAdvanced = false;
            m.m_state = GAME_STATE_RUNNING;
            g_esMission = 0;
            send(0x2000075);
            ok &= MsLogIs({"mission number 1", "reset game", "advance"}, g_gfLog) && m.m_state == GAME_STATE_RUNNING;
            g_dbAdvanced = true;
            send(0x2000075);
            ok &= m.m_state == GAME_STATE_ENDED;
            // Replay: the game reset, the single mission set up, ended.
            m.m_state = GAME_STATE_RUNNING;
            g_esMission = 6;
            ok &= send(0x200007C) == "" && MsLogIs({"mission number 1", "reset game", "single 1 6"}, g_gfLog) &&
                  m.m_state == GAME_STATE_ENDED;
            m.m_state = GAME_STATE_RUNNING;
            ok &= send(0x200000B) == "" && g_gfLog.empty() && m.m_state == GAME_STATE_RUNNING;
            {
                C_MessageBox box(nullptr, GfFrame(FRAME_DUMMY, "dbmsgbox"));
                m.m_messageBox = &box;
                const auto question = [&]() {
                    m.CloseMessage();
                    const std::string q = send(MENU_MSG_ESCAPE);
                    return CmText("%d %x %x ", box.GetKind(), box.m_yesMessage, box.m_noMessage) +
                           (q.find("100000 1000015 18 ") != std::string::npos ? q.substr(q.find("100000 1000015 18 ") + 18, 4) : "?");
                };
                g_menuScreens.clear();
                playMode.singleMission = 1;
                const std::string q1 = question();
                ok &= q1 == "1 2000009 0 7d4;";
                playMode.singleMission = 0;
                g_esMission = 4;
                g_dbProgress = 4;
                const std::string q2 = question();
                ok &= q2 == "1 20000c8 2000009 2533";
                g_dbProgress = 5;
                ok &= question() == "1 20000c8 2000009 2534";
                m.m_messageBox = nullptr;
            }
            clearSquad();
            g_epProfiles.clear();
            g_esMission = 0;
            WM(r.dbMessage, ok);
        }

        // --- built: the debriefing (its builders), the campaign's diary, the first opened, the soldier shown ---
        {
            bool ok = true;
            void (*const oldFill)(I3D_model *) = g_cacheFill;
            g_cacheFill = [](I3D_model *) {};
            C_DebriefingMenu m;
            S_MrWorld world = MrWorld("dbb");
            m.m_scene = As<I3D_scene>(world.scene);
            C_MsScreen box(m.m_scene, "dbbbox", 0);
            m.m_screens = {&box};
            S_MrObj *debriefing = MrObj("dbbd", g_mrModelVt), *diary = MrObj("dbbdi", g_mrModelVt);
            g_mrMade[FRAME_MODEL] = {debriefing, diary};
            S_MrObj *right = MsFrame("dbbr"), *left = MsFrame("dbbl");
            right->vtable = DbPageVt();
            left->vtable = DbPageVt();
            g_msChildren[{diary, "text_02"}] = right;
            g_msChildren[{diary, "text_01"}] = left;
            S_MrObj *madePages = MsFrame("dbbp");
            madePages->vtable = DbPageVt();
            g_mrMade[FRAME_DUMMY] = {madePages};
            playMode.singleMission = 0;
            g_cacheLoads.clear();
            g_menuScreens.clear();
            g_gfLog.clear();
            ok &= m.BuildScreens() && g_cacheLoads == std::vector<std::string>({"debriefing", "diary"}) &&
                  GmInOrder(Join(g_gfLog), {"dbbd 34 debriefing", "dbbd 48 death 1", "dbbd 48 DSO_a 1", "dbbd 48 soldier_rank 20", "music 2 0", "fade 1 2000",
                                            "dbbdi 34 DIARY", "dbbdi 48 text_02 1", "dbbr 38 dbbp 1"}) &&
                  m.m_screens.size() == 3 && m.m_screens[1]->GetScreenFrame() == As<I3D_frame>(debriefing) &&
                  !g_menuScreens.empty() && g_menuScreens.back() == m.m_screens[1];
            for (size_t i = 1; i < m.m_screens.size(); ++i)
                delete m.m_screens[i];
            m.m_screens = {&box};
            m.m_models.clear();
            g_menuScreens.clear();
            // A single mission: no diary.
            playMode.singleMission = 1;
            g_mrMade[FRAME_MODEL] = {MrObj("dbbs", g_mrModelVt)};
            g_cacheLoads.clear();
            g_gfLog.clear();
            ok &= m.BuildScreens() && g_cacheLoads == std::vector<std::string>({"debriefing"}) && m.m_screens.size() == 2 &&
                  !MrHas("dbbs 48 text_02 1");
            g_msChildren.erase({diary, "text_02"});
            g_msChildren.erase({diary, "text_01"});
            g_mrMade.erase(FRAME_DUMMY);
            for (size_t i = 1; i < m.m_screens.size(); ++i)
                delete m.m_screens[i];
            m.m_screens.clear();
            m.m_models.clear();
            g_menuScreens.clear();
            g_mrMade.erase(FRAME_MODEL);
            g_cacheFill = oldFill;
            playMode.singleMission = 0;
            WM(r.dbBuild, ok);
        }

        g_epProfiles = oldProfiles;
        g_gameProfile.m_player = oldPlayer;
        g_gameProfile.m_humanStore = oldStore;
        g_gameProfile.m_missionSquad = oldSquad;
        g_gameProfile.m_playMode = oldPlayMode;
        g_controlMessages.clear();
    }
    // ===== part 9g-vi: the debriefing's diary and screen =====
    {
        S_PlayMode playMode;
        S_PlayMode *const oldPlayMode = g_gameProfile.m_playMode;
        g_gameProfile.m_playMode = &playMode;
        S_MissionSquad squad;
        S_MissionSquad *const oldSquad = g_gameProfile.m_missionSquad;
        g_gameProfile.m_missionSquad = &squad;
        alignas(16) static uint8_t storeMemory[64];
        C_HumanStore *const oldStore = g_gameProfile.m_humanStore;
        g_gameProfile.m_humanStore = reinterpret_cast<C_HumanStore *>(storeMemory);
        C_PlayerProfile player;
        C_PlayerProfile *const oldPlayer = g_gameProfile.m_player;
        g_gameProfile.m_player = &player;
        const std::vector<S_SoldierProfile *> oldProfiles = g_epProfiles;

        // --- the diary's screen: its pages' frames under "multi_page", the controls, the diary's texts ---
        {
            bool ok = true;
            C_DebriefingMenu m;
            g_csNames[reinterpret_cast<uintptr_t>(&m)] = "menu";
            g_csNames[reinterpret_cast<uintptr_t>(&C_DebriefingMenu::OnDiaryPage)] = "page";
            void **const pageVt = DbPageVt();
            S_MrObj *model = MrObj("dym", g_mrModelVt);
            S_MrObj *right = MsFrame("dyr"), *left = MsFrame("dyl");
            right->vtable = pageVt;
            left->vtable = pageVt;
            g_msChildren[{model, "text_02"}] = right;
            g_msChildren[{model, "text_01"}] = left;
            S_MrObj *pages = MsFrame("dyp");
            pages->vtable = pageVt;
            g_mrMade[FRAME_DUMMY] = {pages};
            g_diaryTexts = {5, 1500, 16010, 16019, 16020, 999, 1000, 16009};
            S_ScreenDesc desc("DIARY", "diary", 2u << SCREEN_ID_SHIFT);
            g_gfLog.clear();
            g_controlMessages.clear();
            ok &= m.DescribeDiary(&desc, As<I3D_frame>(model));
            ok &= GmInOrder(Join(g_gfLog), {"dym 48 text_02 1", "dym 48 text_01 1", "create 6", "dyp 8 (0 0 0)", "dyr 38 dyp 1",
                                            "dyl 38 dyp 1", "dyr 24", "dyl 24"});
            ok &= CsDesc(desc) ==
                  "13 text_screen 8100000: 0 1000015 a ac1; 25 multi_page 8200000: 0 100009d 0 0 'text_02' 0 100009d 0 0 "
                  "'text_01'; 14 bprevious page 8300000: 0 1000015 d ac5 0 1000007 4001000 menu page; 14 bnext page 8400000: 0 "
                  "1000015 d ac6 0 1000007 4001000 menu page; 14 bexit 8500000: 0 1000015 d ac7 0 6001003 0 0; 13 "
                  "text_h&dversion 8600000: 0 1000015 28 7d6; ";
            ok &= CsQueue() == "8200000 1000017 4 5; 8200000 1000017 2 7d2; 8200000 1000017 5 5dc; 8200000 1000017 4 3e8a; "
                               "8200000 1000017 4 3e93; 8200000 1000017 2 7d2; 8200000 1000017 5 3e94; 8200000 1000017 4 3e7; "
                               "8200000 1000017 2 7d2; 8200000 1000017 5 3e8; 8200000 1000017 2 7d2; 8200000 1000017 5 3e89; "
                               "8200000 200002c 0 0; ";
            g_msChildren.erase({model, "text_02"});
            g_msChildren.erase({model, "text_01"});
            g_mrMade.erase(FRAME_DUMMY);
            g_diaryTexts.clear();
            g_csNames.erase(reinterpret_cast<uintptr_t>(&m));
            WM(r.dbDiary, ok);
        }

        // --- the diary's pages: back, on, from the start, the page break's; the buttons ---
        {
            bool ok = true;
            C_DebriefingMenu m;
            C_DbPages pagesScreen(nullptr, "dbpages", 0x08200000);
            C_MsScreen box(nullptr, "dbbox", 0), one(nullptr, "dbone", 0x04000000), previous(nullptr, "dbprev", 0x08300000),
                next(nullptr, "dbnext", 0x08400000), other(nullptr, "dbother", 0x04B00000);
            m.m_screens = {&box, &one, &pagesScreen};
            const auto turn = [&](C_Control *control, int page, int count, int pageBreak = 0) {
                m.m_diaryPage = page;
                pagesScreen.count = count;
                pagesScreen.pageBreak = pageBreak;
                g_controlMessages.clear();
                g_gfLog.clear();
                const bool done = C_DebriefingMenu::OnDiaryPage(&m, nullptr, control, CONTROL_NOTIFY_CLICK, 0);
                return done ? CmText("%d: ", m.m_diaryPage) + CsQueue() : std::string("false");
            };
            ok &= turn(nullptr, 6, 10) == "0: 8200000 1000067 0 0; 8300000 1000001 0 0; 8400000 1000002 0 0; ";
            ok &= turn(&previous, 4, 10) == "2: 8200000 1000067 2 0; 8300000 1000002 0 0; 8400000 1000002 0 0; ";
            ok &= turn(&previous, 2, 10) == "0: 8200000 1000067 0 0; 8300000 1000001 0 0; 8400000 1000002 0 0; ";
            ok &= turn(&previous, 1, 10) == "1: 8200000 1000067 1 0; 8300000 1000002 0 0; 8400000 1000002 0 0; ";
            ok &= turn(&next, 2, 6) == "4: 8200000 1000067 4 0; 8300000 1000002 0 0; 8400000 1000001 0 0; ";
            ok &= turn(&next, 4, 6) == "6: 8200000 1000067 6 0; 8300000 1000002 0 0; 8400000 1000001 0 0; ";
            ok &= turn(&next, 5, 6) == "5: 8200000 1000067 5 0; 8300000 1000002 0 0; 8400000 1000001 0 0; ";
            ok &= turn(&next, 0, 3) == "2: 8200000 1000067 2 0; 8300000 1000002 0 0; 8400000 1000001 0 0; ";
            ok &= turn(&next, 0, 2) == "2: 8200000 1000067 2 0; 8300000 1000002 0 0; 8400000 1000001 0 0; ";
            ok &= turn(&next, 0, 1) == "0: 8200000 1000067 0 0; 8300000 1000001 0 0; 8400000 1000001 0 0; ";
            ok &= turn(&next, 0, 5) == "2: 8200000 1000067 2 0; 8300000 1000002 0 0; 8400000 1000002 0 0; ";
            // Another control: no page yet, the page break's (its left page; none: 0); a page kept.
            ok &= turn(&other, -1, 10, 5) == "4: 8200000 1000067 4 0; 8300000 1000002 0 0; 8400000 1000002 0 0; ";
            ok &= turn(&other, -1, 10, 6) == "6: 8200000 1000067 6 0; 8300000 1000002 0 0; 8400000 1000002 0 0; ";
            ok &= turn(&other, -1, 10, -3) == "0: 8200000 1000067 0 0; 8300000 1000001 0 0; 8400000 1000002 0 0; ";
            ok &= turn(&other, -1, 10, 1) == "0: 8200000 1000067 0 0; 8300000 1000001 0 0; 8400000 1000002 0 0; ";
            ok &= turn(&other, 3, 10, 6) == "3: 8200000 1000067 3 0; 8300000 1000002 0 0; 8400000 1000002 0 0; ";
            ok &= turn(&other, 0, 10, 4) == "0: 8200000 1000067 0 0; 8300000 1000001 0 0; 8400000 1000002 0 0; ";
            ok &= turn(&other, -1, 10, 3) == "2: 8200000 1000067 2 0; 8300000 1000002 0 0; 8400000 1000002 0 0; ";
            ok &= turn(&other, -1, 10, 4) == "4: 8200000 1000067 4 0; 8300000 1000002 0 0; 8400000 1000002 0 0; ";
            ok &= pagesScreen.log == "pages pages pages pages pages pages pages pages pages pages pages pages break pages break "
                                     "pages break pages break pages pages pages break pages break ";
            m.m_screens.clear();
            WM(r.dbDiaryPage, ok);
        }

        // --- the mission won: the operation's end's kind; a tutorial; the required objectives neither open nor
        // critically failed, someone alive ---
        {
            bool ok = true;
            C_DebriefingMenu m;
            alignas(16) static uint8_t missionMemory[sizeof(C_MissionDef)];
            std::memset(static_cast<void *>(missionMemory), 0, sizeof missionMemory);
            auto *mission = reinterpret_cast<C_DbMission *>(missionMemory);
            alignas(16) static uint8_t objectiveMemory[3][sizeof(S_MissionObjective)];
            std::memset(static_cast<void *>(objectiveMemory), 0, sizeof objectiveMemory);
            S_MissionObjective *o[3];
            for (int i = 0; i < 3; ++i)
                o[i] = reinterpret_cast<S_MissionObjective *>(objectiveMemory[i]);
            alignas(16) static uint8_t profileMemory[2][sizeof(S_SoldierProfile)];
            std::memset(static_cast<void *>(profileMemory), 0, sizeof profileMemory);
            auto *alive = reinterpret_cast<S_SoldierProfile *>(profileMemory[0]);
            auto *dead = reinterpret_cast<S_SoldierProfile *>(profileMemory[1]);
            dead->dead = true;
            g_epProfiles = {alive, dead};
            S_SquadSoldier s0, s1;
            s0.profileIndex = 0;
            s1.profileIndex = 1;
            m.m_mode = 5;
            g_dbMission = nullptr;
            ok &= !m.IsMissionWon();
            m.m_mode = 6;
            ok &= m.IsMissionWon();
            m.m_mode = 5;
            g_dbMission = mission;
            squad.states = {&s1, &s0};
            ok &= m.IsMissionWon();
            squad.states = {&s0, &s1};
            ok &= m.IsMissionWon();
            squad.states = {&s1, &s0};
            mission->m_objectives = {o[0], o[1], o[2]};
            const auto won = [&](int a, bool ba, int b, bool bb, int c, bool bc) {
                o[0]->status = a;
                o[0]->bonus = ba;
                o[1]->status = b;
                o[1]->bonus = bb;
                o[2]->status = c;
                o[2]->bonus = bc;
                return m.IsMissionWon();
            };
            ok &= won(1, false, 2, false, 4, false) && !won(0, false, 1, false, 1, false) && !won(1, false, 3, false, 1, false);
            ok &= won(0, true, 3, true, 1, false) && won(2, false, 2, false, 2, false) && !won(1, false, 1, false, 0, false);
            // Nobody alive: lost; a tutorial: won.
            squad.states = {&s1};
            ok &= !won(1, false, 1, false, 1, false);
            squad.states.clear();
            ok &= !won(1, false, 1, false, 1, false);
            mission->m_unknown2C = true;
            ok &= won(0, false, 0, false, 0, false);
            mission->m_unknown2C = false;
            mission->m_objectives = std::vector<S_MissionObjective *>();
            g_dbMission = nullptr;
            g_epProfiles.clear();
            WM(r.dbWon, ok);
        }

        // --- the debriefing's screen: its "death" frame off, the titles, the soldiers' panel, the scroll bar, the
        // buttons by the mission's result and the play mode ---
        {
            bool ok = true;
            C_DebriefingMenu m;
            g_csNames[reinterpret_cast<uintptr_t>(&m)] = "menu";
            g_csNames[reinterpret_cast<uintptr_t>(&C_DebriefingMenu::OnDiaryPage)] = "page";
            g_csNames[reinterpret_cast<uintptr_t>(&C_DebriefingMenu::OnSoldierScroll)] = "scroll";
            S_MrObj *model = MrObj("dbm", g_mrModelVt);
            static void **deathVt = nullptr;
            if (!deathVt) {
                deathVt = new void *[MR_SLOTS];
                std::copy(g_msFrameVt, g_msFrameVt + MR_SLOTS, deathVt);
                deathVt[0x30 / 4] = reinterpret_cast<void *>(&MrFake<0x30, bool>);
            }
            S_MrObj *death = MsFrame("dbdeath");
            death->vtable = deathVt;
            g_msChildren[{model, "death"}] = death;
            S_SquadSoldier s0, s1;
            squad.states = {&s0, &s1};
            bool described = true;
            const auto describe = [&](int mode, bool campaign, bool won) {
                m.m_mode = mode;
                playMode.singleMission = campaign ? 0 : 1;
                g_dbMission = nullptr;
                alignas(16) static uint8_t missionMemory[sizeof(C_MissionDef)];
                std::memset(static_cast<void *>(missionMemory), 0, sizeof missionMemory);
                auto *mission = reinterpret_cast<C_DbMission *>(missionMemory);
                mission->m_unknown2C = won;
                g_dbMission = won ? mission : nullptr;
                S_ScreenDesc desc("debriefing", "debriefing", 1u << SCREEN_ID_SHIFT);
                g_gfLog.clear();
                described &= m.DescribeDebriefing(&desc, As<I3D_frame>(model));
                g_dbMission = nullptr;
                return CsDesc(desc);
            };
            const std::string head = "13 text_screen 4100000: 0 1000015 a abf; 13 text_mission status 4200000: 0 1000015 c 7da; "
                                     "13 text_status 4300000: 0 1000015 b ac8; 13 text_team status 4400000: 0 1000015 b ac9; "
                                     "12 text_objectives 4500000:; 5 panel_soldier00 4600000:; 8 scroll00 4700000: 0 1000020 2 0 0 1000007 4010000 menu "
                                     "scroll; ";
            const std::string tail = "14 bload 4a00000: 0 1000015 d ac3 0 1000007 4001000 menu page 0 6001001 2 0; 14 bexit to main "
                                     "menu 4b00000: 0 1000015 d ac4 0 600100a 0 0; 13 text_h&dversion 4c00000: 0 1000015 28 7d6; ";
            std::string got = describe(5, true, true);
            ok &= g_gfLog.size() > 3 && g_gfLog[0] == "dbm 48 death 1" && g_gfLog[1] == "dbdeath 30 0" && g_gfLog[2] == "dbm 48 DSO_a 1";
            ok &= got == head + "14 bsave and continue 4800000: 0 1000015 d ac2 0 6001034 0 0; 14 bretry immediately 4900000: 0 "
                                "1000015 d acc 0 6001075 0 0; " + tail;
            if (got != head + "14 bsave and continue 4800000: 0 1000015 d ac2 0 6001034 0 0; 14 bretry immediately 4900000: 0 "
                               "1000015 d acc 0 6001075 0 0; " + tail)
                std::fprintf(stderr, "DBDESC %s\n", got.c_str());
            // Not won: saving inactive.
            ok &= describe(5, true, false) == head + "14 bsave and continue 4800000: 0 1000015 d ac2 0 6001034 0 0 0 1000001 0 0; "
                                                    "14 bretry immediately 4900000: 0 1000015 d acc 0 6001075 0 0; " + tail;
            // A single mission: saving and the diary inactive, the replay.
            std::string single = tail;
            single.replace(single.find("0 6001001 2 0;"), std::strlen("0 6001001 2 0;"), "0 6001001 2 0 0 1000001 0 0;");
            ok &= describe(5, false, true) == head + "14 bsave and continue 4800000: 0 1000015 d ac2 0 6001034 0 0 0 1000001 0 0; "
                                                    "14 bretry immediately 4900000: 0 1000015 d acc 0 600107c 0 0; " + single;
            // The operation's end's kind: its title, won (saving active), no retry.
            std::string end = head;
            end.replace(end.find("a abf;"), std::strlen("a abf;"), "a ac0;");
            ok &= describe(6, true, false) == end + "14 bsave and continue 4800000: 0 1000015 d ac2 0 6001034 0 0; 14 bretry "
                                                   "immediately 4900000: 0 1000015 d acc 0 1000001 0 0; " + tail;
            // No "death" frame: nothing off.
            g_msChildren.erase({model, "death"});
            describe(5, true, true);
            ok &= g_gfLog.size() > 2 && g_gfLog[0] == "dbm 48 death 1" && g_gfLog[1] == "dbm 48 DSO_a 1";
            squad.states.clear();
            g_csNames.clear();
            WM(r.dbScreen, ok && described);
        }

        // --- the soldiers' panel: its picture, name, titles and skills' bars; the model's medals, places,
        // ranks and the death frame kept ---
        {
            bool ok = true;
            C_DebriefingMenu m;
            S_MrObj *model = MrObj("dpm", g_mrModelVt);
            S_MrObj *dsoA = MsFrame("dsoA"), *burmaB = MsFrame("burmaB"), *slot5 = MsFrame("slot5"), *major = MsFrame("major"),
                    *fresh = MsFrame("fresh"), *rank = MsFrame("rankslot"), *death = MsFrame("deathv");
            g_msChildren[{model, "DSO_a"}] = dsoA;
            g_msChildren[{model, "BURMA_STAR_b"}] = burmaB;
            g_msChildren[{model, "medal05"}] = slot5;
            g_msChildren[{model, "rank_major"}] = major;
            g_msChildren[{model, "new_medal"}] = fresh;
            g_msChildren[{model, "soldier_rank"}] = rank;
            g_msChildren[{model, "death"}] = death;
            S_ScreenDesc desc("debriefing", "debriefing", 1u << SCREEN_ID_SHIFT);
            g_gfLog.clear();
            m.DescribeSoldiers(&desc, As<I3D_frame>(model));
            ok &= desc.controls.size() == 1;
            const std::string tree = desc.controls.empty() ? "" : CsTree(desc.controls[0]);
            const std::string half = "3f000000", seven = "3f333333";
            std::string bars;
            const char *suffixes[7] = {" %", " Kg", "", "", " %", " %", " %"};
            for (int i = 0; i < 7; ++i)
                bars += CmText(" 17 hlcpctcr0%d 41%02x000: 0 1000093 2 0 0 1000015 b %x 0 1000092 0 %s 0 1000092 1 %s 0 1000092 2 '%s';", i,
                               8 + i, 0xA6E + i, half.c_str(), seven.c_str(), suffixes[i]);
            const std::string want = "5 panel_soldier00 4100000: { 4 soldier00 4101000: 0 1000038 0 0; 13 text_name00 4102000:; 13 "
                                     "text_soldierstats00 4103000: 0 1000015 b acd; 13 text_new medal 4104000:; 13 text_selected "
                                     "soldier stats 4105000: 0 1000015 b ad2; 13 text_decorations 4106000: 0 1000015 b ad3; 13 "
                                     "text_new decoration 4107000: 0 1000015 b ad4;" + bars + " };";
            ok &= tree == want;
            if (tree != want)
                std::fprintf(stderr, "DBPANEL\n%s\n%s\n", tree.c_str(), want.c_str());
            ok &= m.m_medalFrames[0][0] == As<I3D_frame>(dsoA) && !m.m_medalFrames[0][1] && m.m_medalFrames[11][1] == As<I3D_frame>(burmaB) &&
                  !m.m_medalFrames[11][0] && m.m_medalSlots[4] == As<I3D_frame>(slot5) && !m.m_medalSlots[0] &&
                  m.m_rankFrames[7] == As<I3D_frame>(major) && !m.m_rankFrames[0] && m.m_newMedalSlot == As<I3D_frame>(fresh) &&
                  m.m_rankSlot == As<I3D_frame>(rank) && m.m_deathFrame == As<I3D_frame>(death);
            // The frames looked for: the medals' pairs (visuals), the places (dummies), the ranks, the rest.
            std::vector<std::string> wantLog;
            const char *medals[12] = {"DSO", "DCM", "MILIT_MEDAL", "GSM_AFRICA", "GSM", "WAR_MEDAL", "DEFENCE_MEDAL",
                                      "MILITARY_CROSS", "VICTORIA_CROSS", "GERM_FRANCE_STAR", "AFRICA_STAR", "BURMA_STAR"};
            for (const char *medal : medals) {
                wantLog.push_back(CmText("dpm 48 %s_a 1", medal));
                wantLog.push_back(CmText("dpm 48 %s_b 1", medal));
            }
            for (int i = 1; i <= 12; ++i)
                wantLog.push_back(CmText("dpm 48 medal%02d 20", i));
            wantLog.push_back("dpm 48 new_medal 20");
            for (const char *r8 : {"rank_private", "rank_corporal", "rank_sergeant", "rank_StaffSergeant", "rank_second_lieutenant",
                                   "rank_lieutenant", "rank_captain", "rank_major"})
                wantLog.push_back(CmText("dpm 48 %s 1", r8));
            wantLog.push_back("dpm 48 soldier_rank 20");
            wantLog.push_back("dpm 48 death 1");
            ok &= MsLogIs(wantLog, g_gfLog);
            for (const char *name : {"DSO_a", "BURMA_STAR_b", "medal05", "rank_major", "new_medal", "soldier_rank", "death"})
                g_msChildren.erase({model, name});
            WM(r.dbPanel, ok);
        }

        // --- the objectives listed (the completed under "Tasks: ", the others but the inactive under
        // "Recommendation:"); won: a tutorial, someone alive ---
        {
            bool ok = true;
            alignas(16) static uint8_t missionMemory[sizeof(C_MissionDef)];
            std::memset(static_cast<void *>(missionMemory), 0, sizeof missionMemory);
            auto *mission = reinterpret_cast<C_DbMission *>(missionMemory);
            alignas(16) static uint8_t objectiveMemory[4][sizeof(S_MissionObjective)];
            std::memset(static_cast<void *>(objectiveMemory), 0, sizeof objectiveMemory);
            S_MissionObjective *o[4];
            for (int i = 0; i < 4; ++i)
                o[i] = reinterpret_cast<S_MissionObjective *>(objectiveMemory[i]);
            alignas(16) static uint8_t profileMemory[2][sizeof(S_SoldierProfile)];
            std::memset(static_cast<void *>(profileMemory), 0, sizeof profileMemory);
            auto *alive = reinterpret_cast<S_SoldierProfile *>(profileMemory[0]);
            auto *dead = reinterpret_cast<S_SoldierProfile *>(profileMemory[1]);
            dead->dead = true;
            g_epProfiles = {alive, dead};
            S_SquadSoldier s0, s1;
            s0.profileIndex = 0;
            s1.profileIndex = 1;
            const auto list = [&](std::initializer_list<int> statuses) {
                std::vector<S_MissionObjective *> objectives;
                int i = 0;
                for (int status : statuses) {
                    o[i]->status = status;
                    static_cast<C_DbObjective *>(static_cast<C_CampaignNode *>(o[i]))->m_id = 100 + i;
                    objectives.push_back(o[i]);
                    ++i;
                }
                mission->m_objectives = objectives;
                g_controlMessages.clear();
                const bool won = C_DebriefingMenu::ListObjectives();
                return CmText("%d ", won) + CsQueue();
            };
            g_dbMission = nullptr;
            ok &= list({}) == "0 ";
            g_dbMission = mission;
            squad.states = {&s0, &s1};
            ok &= list({}) == "1 ";
            const std::string done0 = "4500000 1000017 29 acf; 4500000 1000016 55 0 '='; 4500000 1000016 29 0 ' '; 4500000 1000017 29 64; ";
            ok &= list({1}) == "1 " + done0;
            ok &= list({1, 4, 1}) == "1 " + done0 + "4500000 1000016 55 0 '='; 4500000 1000016 29 0 ' '; 4500000 1000017 29 66; ";
            ok &= list({0, 1, 2, 3}) == "1 4500000 1000017 29 acf; 4500000 1000016 55 0 '='; 4500000 1000016 29 0 ' '; "
                                        "4500000 1000017 29 65; 4500000 1000016 b 0 ' '; 4500000 1000017 29 ad0; 4500000 1000016 55 "
                                        "0 '!'; 4500000 1000016 29 0 ' '; 4500000 1000017 29 64; 4500000 1000016 55 0 '!'; 4500000 "
                                        "1000016 29 0 ' '; 4500000 1000017 29 66; 4500000 1000016 55 0 '!'; 4500000 1000016 29 0 ' '; "
                                        "4500000 1000017 29 67; ";
            ok &= list({4, 2}) == "1 4500000 1000017 29 ad0; 4500000 1000016 55 0 '!'; 4500000 1000016 29 0 ' '; 4500000 1000017 29 65; ";
            // Nobody alive: lost (the objectives listed all the same); a tutorial: won.
            squad.states = {&s1};
            ok &= list({1}) == "0 " + done0;
            squad.states = {&s1, &s0};
            ok &= list({}) == "1 ";
            squad.states = {&s0, &s1};
            ok &= list({}) == "1 ";
            squad.states.clear();
            mission->m_unknown2C = true;
            ok &= list({}) == "1 ";
            mission->m_unknown2C = false;
            ok &= list({}) == "0 ";
            // The result: the text cleared, the status, the music (won, lost; the operation's end's kind
            // won without a list).
            C_DebriefingMenu m;
            m.m_mode = 5;
            squad.states = {&s0};
            mission->m_objectives = std::vector<S_MissionObjective *>();
            g_controlMessages.clear();
            g_gfLog.clear();
            m.ShowMissionResult(nullptr, nullptr);
            bool result = CsQueue() == "4500000 2000018 0 0; 4200000 1000015 c 7da; " && MsLogIs({"music 1 0", "fade 1 2000"}, g_gfLog);
            squad.states = {&s1};
            g_gfLog.clear();
            m.ShowMissionResult(nullptr, nullptr);
            result &= CsQueue() == "4500000 2000018 0 0; 4200000 1000015 c 7dc; " && MsLogIs({"music 2 0", "fade 1 2000"}, g_gfLog);
            m.m_mode = 6;
            mission->m_objectives = {o[0]};
            o[0]->status = 1;
            g_gfLog.clear();
            m.ShowMissionResult(nullptr, nullptr);
            result &= CsQueue() == "4500000 2000018 0 0; 4200000 1000015 c 7da; " && MsLogIs({"music 1 0", "fade 1 2000"}, g_gfLog);
            mission->m_objectives = std::vector<S_MissionObjective *>();
            squad.states.clear();
            g_dbMission = nullptr;
            g_epProfiles.clear();
            WM(r.dbObjectives, ok);
            WM(r.dbResult, result);
        }

        // ===== part 9g-viii: the soldier shown =====
        {
            static void **frameVt = nullptr;
            if (!frameVt) {
                frameVt = new void *[MR_SLOTS];
                std::copy(g_msFrameVt, g_msFrameVt + MR_SLOTS, frameVt);
                frameVt[0x30 / 4] = reinterpret_cast<void *>(&MrFake<0x30, bool>);
                frameVt[0x38 / 4] = reinterpret_cast<void *>(&MrFake<0x38, void *, uint32_t>);
            }
            const auto frame = [](const std::string &name) {
                S_MrObj *o = MsFrame(name.c_str());
                o->vtable = frameVt;
                return As<I3D_frame>(o);
            };
            C_DebriefingMenu m;
            for (int i = 0; i < DEBRIEF_MEDALS; ++i) {
                m.m_medalFrames[i][0] = frame(CmText("m%da", i));
                m.m_medalFrames[i][1] = frame(CmText("m%db", i));
                m.m_medalSlots[i] = frame(CmText("slot%d", i));
            }
            for (int i = 0; i < DEBRIEF_RANKS; ++i)
                m.m_rankFrames[i] = frame(CmText("rank%d", i));
            m.m_newMedalSlot = frame("newslot");
            m.m_rankSlot = frame("rslot");
            m.m_deathFrame = frame("dsdeath");
            for (int i = 0; i < 24; ++i)
                g_rosterTexts[i] = 0x2774 + i;
            alignas(16) static uint8_t profileMemory[3][sizeof(S_SoldierProfile)];
            std::memset(static_cast<void *>(profileMemory), 0, sizeof profileMemory);
            S_SoldierProfile *p[3];
            for (int i = 0; i < 3; ++i) {
                p[i] = reinterpret_cast<S_SoldierProfile *>(profileMemory[i]);
                for (int k = 0; k < 9; ++k)
                    p[i]->stats[k] = 10.0f * static_cast<float>(k) + 1.5f + static_cast<float>(i);
            }
            p[0]->unknown24 = 2;
            p[0]->unknown28.push_back(3);
            p[0]->unknown28.push_back(7);
            p[0]->customName = C_str("Joe");
            p[0]->fullName = C_str("Joseph Bloggs");
            p[0]->faceImage = reinterpret_cast<IImage *>(0x1234);
            p[1]->dead = true;
            p[1]->unknown24 = 4;
            p[1]->fullName = C_str("Dead Man");
            p[2]->unknown24 = 2;
            p[2]->fullName = C_str("Sam");
            g_epProfiles = {p[0], p[1], p[2]};
            S_SquadSoldier s[3];
            for (int i = 0; i < 3; ++i) {
                s[i].profileIndex = i;
                s[i].unknown34 = -1;
                s[i].unknown38 = -1;
                s[i].progress.experience = 1000 + i;
                s[i].progress.unknown00 = 0.25f;
                s[i].progress.unknown04 = 0.5f;
                s[i].progress.endurance = 0.75f;
                s[i].progress.shooting = 1.25f;
                s[i].progress.stealth = 1.5f;
                s[i].progress.firstAid = 1.75f;
                s[i].progress.lockpick = 2.25f;
            }
            s[2].unknown34 = 3;
            s[2].unknown38 = 5;
            squad.states = {&s[0], &s[1], &s[2]};
            const auto hex = [](float f) {
                uint32_t bits;
                std::memcpy(&bits, &f, sizeof bits);
                return CmText("%x", bits);
            };
            const auto skills = [&](const S_SoldierProfile *pr, const S_SquadSoldier &sd) {
                std::string q;
                const auto bar = [&](uint32_t id, float maximum, float value, float gain) {
                    q += CmText("%x 1000091 0 ", id) + hex(maximum) + "; " + CmText("%x 1000092 0 ", id) + hex(value) + "; " +
                         CmText("%x 1000092 1 ", id) + hex(gain) + "; ";
                };
                bar(0x4608000, 100.0f, pr->stats[4], sd.progress.shooting);
                bar(0x4609000, 100.0f, pr->stats[1], sd.progress.unknown04);
                bar(0x460A000, sd.progress.unknown00 + pr->stats[0], pr->stats[0], sd.progress.unknown00);
                bar(0x460B000, 100.0f, pr->stats[2], sd.progress.endurance);
                bar(0x460C000, 100.0f, pr->stats[6], sd.progress.stealth);
                bar(0x460D000, 100.0f, pr->stats[7], sd.progress.firstAid);
                bar(0x460E000, 100.0f, pr->stats[8], sd.progress.lockpick);
                return q;
            };

            // --- the soldier shown: the death frame, the face, the name, the rank or the promotion, the
            // debugging line, the skills, the badge, the medals ---
            {
                bool ok = true;
                g_controlMessages.clear();
                g_gfLog.clear();
                m.ShowSoldier(0);
                const std::string q0 = CsQueue();
                ok &= m.m_shownSoldier == 0 && g_gfLog.size() > 1 && g_gfLog[0] == "dsdeath 30 0";
                const std::string head0 = "4601000 1000038 0 1234; 4602000 1000014 c 0 'Joe'; 4603000 2000018 0 0; 4603000 1000017 b "
                                          "2794; 4603000 1000017 b ace; 4603000 1000017 b 7d2; 4603000 1000016 b 0 'Debug Experience "
                                          ":: 1000'; ";
                ok &= q0 == head0 + skills(p[0], s[0]) + "4604000 2000018 0 0; 4607000 1000004 0 0; ";
                ok &= MrHas("rank2 38 rslot 1") && MrHas("m3b 38 slot0 1") && MrHas("m7b 38 slot1 1");
                // The dead: the frame on, the full name, "Killed in action".
                g_gfLog.clear();
                m.ShowSoldier(1);
                const std::string q1 = CsQueue();
                ok &= m.m_shownSoldier == 1 && g_gfLog[0] == "dsdeath 30 1" &&
                      q1.find("4602000 1000014 c 0 'Dead Man'; 4603000 2000018 0 0; 4603000 1000017 b 2796; 4603000 1000017 b acd; "
                              "4603000 1000017 b 7d2; 4603000 1000016 b 0 'Debug Experience :: 1001'; ") != std::string::npos;
                // Promoted: "Promoted to " the new rank (the badge his); the new medal.
                g_gfLog.clear();
                m.ShowSoldier(2);
                const std::string q2 = CsQueue();
                ok &= q2.find("4603000 2000018 0 0; 4603000 1000017 b 7d2; 4603000 1000017 2b ad1; 4603000 1000017 2b 2795; 4603000 "
                              "1000017 b 7d2; 4603000 1000016 b 0 'Debug Experience :: 1002'; ") != std::string::npos &&
                      MrHas("rank3 38 rslot 1") && MrHas("m5a 38 newslot 1");
                // Past the squad: nothing.
                g_gfLog.clear();
                m.ShowSoldier(3);
                ok &= m.m_shownSoldier == 2 && g_gfLog.empty() && CsQueue() == "";
                // (No death frame: none switched.)
                I3D_frame *const death = m.m_deathFrame;
                m.m_deathFrame = nullptr;
                g_gfLog.clear();
                m.ShowSoldier(0);
                ok &= !MrHas("dsdeath 30 0");
                m.m_deathFrame = death;
                g_controlMessages.clear();
                WM(r.dbShow, ok);
            }

            // --- the skills' bars (the 3rd's maximum its stat and the gain); none: hidden ---
            {
                bool ok = true;
                g_controlMessages.clear();
                C_DebriefingMenu::ShowSkills(p[1], &s[1]);
                ok &= CsQueue() == skills(p[1], s[1]);
                C_DebriefingMenu::ShowSkills(nullptr, &s[1]);
                std::string hidden;
                for (int i = 0; i < 7; ++i)
                    hidden += CmText("%x 1000004 0 0; ", 0x4608000 + i * 0x1000);
                ok &= CsQueue() == hidden;
                C_DebriefingMenu::ShowSkills(p[1], nullptr);
                ok &= CsQueue() == hidden;
                WM(r.dbSkills, ok);
            }

            // --- the rank's badge: the rank's, a higher promotion's; the badges hidden first ---
            {
                bool ok = true;
                std::vector<std::string> hides;
                for (int i = 7; i >= 0; --i)
                    hides.push_back(CmText("rank%d 30 0", i));
                const auto rank = [&](const S_SoldierProfile *pr, const S_SquadSoldier *sd) {
                    g_gfLog.clear();
                    m.ShowRank(pr, sd);
                    return std::vector<std::string>(g_gfLog.begin() + std::min<size_t>(8, g_gfLog.size()), g_gfLog.end());
                };
                // (Placed: its position its place's, the frame changed.)
                m.m_rankFrames[2]->m_localMatrix.m[3][0] = 1.0f;
                m.m_rankFrames[2]->m_localMatrix.m[3][2] = 3.0f;
                m.m_rankFrames[2]->m_frameFlags = 0x120;
                ok &= rank(p[0], &s[0]) == std::vector<std::string>({"rank2 38 rslot 1", "rank2 30 1"}) &&
                      m.m_rankFrames[2]->GetPos() == S_vector(0.0f, 0.0f, 0.0f) && m.m_rankFrames[2]->m_frameFlags == 0x40000000 &&
                      std::vector<std::string>(g_gfLog.begin(), g_gfLog.begin() + 8) == hides;
                ok &= rank(p[2], &s[2]) == std::vector<std::string>({"rank3 38 rslot 1", "rank3 30 1"});
                s[2].unknown34 = 2;
                ok &= rank(p[2], &s[2]).empty();
                s[2].unknown34 = 1;
                ok &= rank(p[2], &s[2]).empty();
                s[2].unknown34 = 3;
                p[0]->unknown24 = 8;
                ok &= rank(p[0], &s[0]).empty();
                p[0]->unknown24 = 2;
                ok &= rank(nullptr, &s[0]).empty() && g_gfLog.size() == 8 && rank(p[0], nullptr).empty();
                // (A missing badge: neither hidden nor placed.)
                I3D_frame *const two = m.m_rankFrames[2];
                m.m_rankFrames[2] = nullptr;
                ok &= rank(p[0], &s[0]).empty() && g_gfLog.size() == 7;
                m.m_rankFrames[2] = two;
                WM(r.dbRank, ok);
            }

            // --- the medals: the held ones' second frames in the places in turn, the new one's first frame
            // and texts; no profile: all hidden ---
            {
                bool ok = true;
                const auto medals = [&](const S_SoldierProfile *pr, const S_SquadSoldier *sd) {
                    g_gfLog.clear();
                    g_controlMessages.clear();
                    m.ShowMedals(pr, sd);
                    return std::vector<std::string>(g_gfLog.begin() + std::min<size_t>(24, g_gfLog.size()), g_gfLog.end());
                };
                std::vector<std::string> hides;
                for (int i = 11; i >= 0; --i) {
                    hides.push_back(CmText("m%da 30 0", i));
                    hides.push_back(CmText("m%db 30 0", i));
                }
                ok &= medals(p[0], &s[0]) == std::vector<std::string>({"m3b 38 slot0 1", "m3b 30 1", "m3a 30 0", "m7b 38 slot1 1",
                                                                         "m7b 30 1", "m7a 30 0"}) &&
                      std::vector<std::string>(g_gfLog.begin(), g_gfLog.begin() + 24) == hides &&
                      CsQueue() == "4604000 2000018 0 0; 4607000 1000004 0 0; ";
                p[2]->unknown28 = {11};
                ok &= medals(p[2], &s[2]) == std::vector<std::string>({"m11b 38 slot0 1", "m11b 30 1", "m11a 30 0", "m5a 38 newslot 1",
                                                                         "m5a 30 1", "m5b 30 0"}) &&
                      CsQueue() == "4604000 2000018 0 0; 4604000 1000017 2 7d2; 4604000 1000017 2 277e; 4604000 1000017 2 7d2; "
                                   "4604000 1000017 3 277f; 4607000 1000003 0 0; ";
                ok &= medals(nullptr, &s[2]).empty() && g_gfLog.size() == 24 && CsQueue() == "";
                WM(r.dbMedals, ok);
            }

            // --- the scroll bar: its soldier shown (not the one shown already); no control: false ---
            {
                bool ok = true;
                C_DbScroll bar(nullptr, "dbscroll", 0x04700000);
                ok &= !C_DebriefingMenu::OnSoldierScroll(&m, nullptr, nullptr, CONTROL_NOTIFY_CHANGED, 0);
                m.m_shownSoldier = 0;
                bar.value = 2;
                g_controlMessages.clear();
                ok &= C_DebriefingMenu::OnSoldierScroll(&m, nullptr, &bar, CONTROL_NOTIFY_CHANGED, 0) && m.m_shownSoldier == 2 &&
                      CsQueue().find("'Sam'") != std::string::npos;
                ok &= C_DebriefingMenu::OnSoldierScroll(&m, nullptr, &bar, CONTROL_NOTIFY_CHANGED, 0) && CsQueue() == "";
                bar.value = 1;
                ok &= C_DebriefingMenu::OnSoldierScroll(&m, nullptr, &bar, CONTROL_NOTIFY_CHANGED, 0) && m.m_shownSoldier == 1 &&
                      bar.asked == 3;
                g_controlMessages.clear();
                WM(r.dbScroll, ok);
            }
            squad.states.clear();
            g_epProfiles.clear();
        }

        g_epProfiles = oldProfiles;
        g_gameProfile.m_player = oldPlayer;
        g_gameProfile.m_humanStore = oldStore;
        g_gameProfile.m_missionSquad = oldSquad;
        g_gameProfile.m_playMode = oldPlayMode;
        g_controlMessages.clear();
    }
    // ===== part 9g-ix: the squad screen =====
    {
        S_PlayMode playMode;
        S_PlayMode *const oldPlayMode = g_gameProfile.m_playMode;
        g_gameProfile.m_playMode = &playMode;
        S_MissionSquad squad;
        S_MissionSquad *const oldSquad = g_gameProfile.m_missionSquad;
        g_gameProfile.m_missionSquad = &squad;
        alignas(16) static uint8_t sqStoreMemory[64];
        C_HumanStore *const oldStore = g_gameProfile.m_humanStore;
        g_gameProfile.m_humanStore = reinterpret_cast<C_HumanStore *>(sqStoreMemory);
        const std::vector<S_SoldierProfile *> oldProfiles = g_epProfiles;
        // Profiles in zeroed memory (their constructor is not linked).
        alignas(16) static uint8_t sqProfileMemory[36][sizeof(S_SoldierProfile)];
        std::memset(static_cast<void *>(sqProfileMemory), 0, sizeof sqProfileMemory);
        S_SoldierProfile *p[36];
        for (int i = 0; i < 36; ++i)
            p[i] = reinterpret_cast<S_SoldierProfile *>(sqProfileMemory[i]);
        const auto roster = [&](int count) { g_epProfiles.assign(p, p + count); };
        const auto soldier = [](int profileIndex, int speechSet) {
            auto *s = new S_SquadSoldier();
            s->profileIndex = profileIndex;
            s->speechSet = speechSet;
            return s;
        };
        const auto clearSquad = [&]() {
            for (S_SquadSoldier *s : squad.states)
                delete s;
            squad.states.clear();
        };

        // --- a slot: the soldier put in (his face, the index, his name and rank), none (blanks); its controls ---
        {
            bool ok = true;
            roster(2);
            p[0]->fullName = C_str("Al Smith");
            p[0]->unknown24 = 2;
            p[0]->faceImage = reinterpret_cast<IImage *>(0x1234);
            p[1]->fullName = C_str("Bob Jones");
            p[1]->customName = C_str("Bo");
            p[1]->unknown24 = 5;
            S_SquadSlot slot;
            ok &= slot.locked && slot.profile == -1 && slot.picture == 0xFFFFFFFF && slot.name == 0xFFFFFFFF &&
                  slot.rank == 0xFFFFFFFF;
            slot.picture = 0x4201000;
            slot.name = 0x4202000;
            slot.rank = 0x4203000;
            g_controlMessages.clear();
            slot.SetSoldier(0, false);
            ok &= !slot.locked && slot.profile == 0 &&
                  CsQueue() == "4201000 1000038 0 1234; 4201000 1000009 0 0; 4202000 1000014 c 0 'Al Smith'; "
                               "4203000 1000015 2 2794; ";
            slot.SetSoldier(1, true);
            ok &= slot.locked && slot.profile == 1 &&
                  CsQueue() == "4201000 1000038 0 0; 4201000 1000009 1 0; 4202000 1000014 c 0 'Bo'; 4203000 1000015 2 2797; ";
            slot.SetSoldier(-1, false);
            ok &= !slot.locked && slot.profile == -1 &&
                  CsQueue() == "4201000 1000038 0 0; 4201000 1000009 ffffffff 0; 4202000 1000015 b 7d2; 4203000 1000015 b 7d2; ";
            slot.profile = 0;
            slot.ShowTexts();
            ok &= CsQueue() == "4202000 1000014 c 0 'Al Smith'; 4203000 1000015 2 2794; ";
            slot.profile = 7;
            slot.ShowTexts();
            ok &= CsQueue() == "4202000 1000015 b 7d2; 4203000 1000015 b 7d2; ";
            ok &= slot.HasControl(0x4201000) && slot.HasControl(0x4202000) && slot.HasControl(0x4203000) &&
                  !slot.HasControl(0x4204000) && !slot.HasControl(0);
            ok &= &GetProfileName(p[0]) == &p[0]->fullName && &GetProfileName(p[1]) == &p[1]->customName;
            p[0]->faceImage = nullptr;
            WM(r.sqSlot, ok);
        }

        // --- one soldier: the lone wolf mode, the first campaign's missions 2 and 15 on their own ---
        {
            bool ok = true;
            const auto one = [&](int mode, int single, int current, int mission) {
                playMode.mode = mode;
                playMode.singleMission = single;
                campaign->m_current = current;
                g_esMission = mission;
                g_gfLog.clear();
                return C_SquadMenu::IsOneSoldier();
            };
            ok &= one(PLAY_MODE_LONE_WOLF, 0, 1, 3) && g_gfLog.empty();
            ok &= !one(PLAY_MODE_CAMPAIGN, 0, 0, 2) && g_gfLog.empty();
            ok &= one(PLAY_MODE_CAMPAIGN, 1, 0, 2) && MsLogIs({"mission number 0"}, g_gfLog);
            ok &= one(PLAY_MODE_CARNAGE, 1, 0, 15) && !one(PLAY_MODE_CAMPAIGN, 1, 0, 3) && !one(PLAY_MODE_CAMPAIGN, 1, 1, 2) &&
                  !one(PLAY_MODE_CAMPAIGN, 1, 0, 14) && !one(PLAY_MODE_CAMPAIGN, 2, 0, 2);
            playMode = S_PlayMode();
            g_esMission = 0;
            WM(r.sqOneSoldier, ok);
        }

        // --- started: the roster loaded when empty (failing: false); a campaign's kind 2 screen: the operation's
        // soldiers given the Defence Medal, the squad's items back ---
        {
            bool ok = true;
            C_SquadMenu m;
            ok &= m.GetTypeId() == 3 && m.m_selectedSlot == 0 && m.m_selectedSoldier == -1 && m.m_rosterSelected &&
                  m.m_unknown1BC == 0 && m.m_unknown1C0 == 0 && m.m_unknown1C4 == 0 && m.m_slots[3].locked &&
                  m.m_slots[3].profile == -1 && m.m_slots[3].rank == 0xFFFFFFFF;
            const auto start = [&](int mode) {
                m.m_mode = mode;
                g_gfLog.clear();
                return m.OnStart(nullptr);
            };
            g_epProfiles.clear();
            ok &= start(1) && MsLogIs({"roster load tables\\HumanStore.def"}, g_gfLog);
            g_esRosterLoads = false;
            ok &= !start(2) && MsLogIs({"roster load tables\\HumanStore.def"}, g_gfLog);
            g_esRosterLoads = true;
            roster(30);
            p[9]->dead = true;
            p[11]->unknown28 = {6};
            p[16]->unknown28 = {3};
            squad.states = {soldier(3, 1), soldier(4, 2)};
            campaign->m_current = 1;
            g_csOperation = 2;
            ok &= start(2) && MsLogIs({"operation index 1", "return all 3", "return all 4"}, g_gfLog);
            for (int i = 0; i < 30; ++i) {
                std::vector<int> want;
                if (i == 7 || i == 11 || i == 17)
                    want = {6};
                else if (i == 16)
                    want = {3, 6};
                ok &= p[i]->unknown28 == want;
            }
            // (The kind 1 screen, a single mission: neither.)
            ok &= start(1) && g_gfLog.empty() && p[7]->unknown28.size() == 1;
            playMode.singleMission = 1;
            ok &= start(2) && g_gfLog.empty();
            playMode.singleMission = 0;
            // (The operations without soldiers; the last's.)
            g_csOperation = 0;
            ok &= start(2) && MsLogIs({"operation index 1", "return all 3", "return all 4"}, g_gfLog);
            g_csOperation = 7;
            ok &= start(2) && p[1]->unknown28 == std::vector<int>({6}) && p[26]->unknown28 == std::vector<int>({6}) &&
                  p[27]->unknown28 == std::vector<int>({6}) && p[28]->unknown28.empty() && p[0]->unknown28.empty();
            g_csOperation = 3;
            ok &= start(2) && p[22]->unknown28 == std::vector<int>({6}) && p[2]->unknown28 == std::vector<int>({6}) &&
                  p[23]->unknown28.empty();
            // (Operations 4 .. 6.)
            for (int op = 4; op <= 6; ++op) {
                g_csOperation = op;
                ok &= start(2);
            }
            for (int i : {4, 10, 23, 24, 28, 29, 3, 6, 13, 21, 25, 14, 15, 18, 20})
                ok &= p[i]->unknown28 == std::vector<int>({6});
            ok &= p[0]->unknown28.empty() && p[9]->unknown28.empty() && p[30]->unknown28.empty();
            for (int i = 0; i < 36; ++i)
                p[i]->unknown28.clear();
            p[9]->dead = false;
            // (The table, as the binary's.)
            ok &= g_operationMedalSoldiers[3][5] == 22 && g_operationMedalSoldiers[3][6] == -1 && g_operationMedalSoldiers[4][5] == 29 &&
                  g_operationMedalSoldiers[0][0] == -1 && g_operationMedalSoldiers[6][4] == -1 && g_operationMedalSoldiers[5][0] == 3;
            clearSquad();
            // Stopped: the popup deleted.
            m.m_popup = new C_SqPopup();
            g_gfLog.clear();
            m.OnStop();
            ok &= m.m_popup == nullptr && !g_gfLog.empty() && g_gfLog[0] == "sqpopup deleted";
            m.OnStop();
            ok &= m.m_popup == nullptr;
            WM(r.sqStart, ok);
        }

        // --- the slots: a roster soldier in one; the first speech set free ---
        {
            bool ok = true;
            C_SquadMenu m;
            m.m_slots[0].profile = 4;
            m.m_slots[2].profile = 0;
            ok &= m.IsInSlots(4) && m.IsInSlots(0) && m.IsInSlots(-1) && !m.IsInSlots(1);
            m.m_slots[1].profile = 1;
            m.m_slots[3].profile = 9;
            ok &= !m.IsInSlots(-1) && m.IsInSlots(9) && m.IsInSlots(1) && !m.IsInSlots(2);
            ok &= C_SquadMenu::FirstFreeSpeechSet() == 1;
            squad.states = {soldier(0, 1), soldier(1, 3), nullptr, soldier(2, 0)};
            ok &= C_SquadMenu::FirstFreeSpeechSet() == 2;
            squad.states[3]->speechSet = 2;
            ok &= C_SquadMenu::FirstFreeSpeechSet() == 4;
            delete squad.states[3];
            squad.states[3] = soldier(5, 4);
            ok &= C_SquadMenu::FirstFreeSpeechSet() == 2;
            squad.states.push_back(soldier(6, 2));
            ok &= C_SquadMenu::FirstFreeSpeechSet() == -1;
            squad.states.erase(squad.states.begin() + 2);
            clearSquad();
            WM(r.sqSlots, ok);
        }

        // --- the squad kept to the slots: freed, given (a freed one, a new one), ordered, deleted, speech sets ---
        {
            bool ok = true;
            C_SquadMenu m;
            m.m_mode = 1;
            roster(12);
            S_SquadSoldier *a = soldier(5, 2), *b = soldier(1, -1), *c = soldier(7, 1);
            squad.states = {a, b, c};
            const int slots1[4] = {1, 9, 5, -1};
            for (int i = 0; i < 4; ++i)
                m.m_slots[i].profile = slots1[i];
            g_gfLog.clear();
            ok &= C_SquadMenu::SyncSquad(&m, nullptr, nullptr, 0, 0) && squad.states == std::vector<S_SquadSoldier *>({b, c, a}) && b->profileIndex == 1 &&
                  c->profileIndex == 9 && a->profileIndex == 5 && c->autoEquip && !b->autoEquip && !a->autoEquip &&
                  b->speechSet == 3 && c->speechSet == 1 && a->speechSet == 2 &&
                  MsLogIs({"find state 1", "find state 9", "set profile 9 0", "find state 5"}, g_gfLog);
            // All freed but one reused (the last first; not equipped on a kind 2 screen), the others deleted.
            m.m_mode = 2;
            const int slots2[4] = {2, -1, 3, -1};
            for (int i = 0; i < 4; ++i)
                m.m_slots[i].profile = slots2[i];
            c->autoEquip = true;
            g_gfLog.clear();
            ok &= C_SquadMenu::SyncSquad(&m, nullptr, nullptr, 0, 0) && squad.states == std::vector<S_SquadSoldier *>({a}) && a->profileIndex == 2 && !a->autoEquip &&
                  a->speechSet == 2 && MsLogIs({"find state 2", "set profile 2 0"}, g_gfLog);
            // A new one; the order kept.
            const int slots3[4] = {2, 4, 6, 8};
            for (int i = 0; i < 4; ++i)
                m.m_slots[i].profile = slots3[i];
            m.m_mode = 1;
            const size_t added = g_gmAdded.size();
            g_gfLog.clear();
            ok &= C_SquadMenu::SyncSquad(&m, nullptr, nullptr, 0, 0) && squad.states.size() == 4 && squad.states[0] == a && g_gmAdded.size() == added + 3 &&
                  squad.states[1]->profileIndex == 4 && squad.states[2]->profileIndex == 6 && squad.states[3]->profileIndex == 8 &&
                  squad.states[3]->autoEquip && squad.states[1]->speechSet == 0 &&
                  MsLogIs({"find state 2", "find state 4", "add soldier", "set profile 4 0", "find state 6", "add soldier",
                           "set profile 6 0", "find state 8", "add soldier", "set profile 8 0"}, g_gfLog);
            // Reordered: the slots' order (the fourth slot's swapped in from behind).
            const int slots4[4] = {8, 2, 6, 4};
            for (int i = 0; i < 4; ++i)
                m.m_slots[i].profile = slots4[i];
            S_SquadSoldier *n4 = squad.states[1], *n6 = squad.states[2], *n8 = squad.states[3];
            ok &= C_SquadMenu::SyncSquad(&m, nullptr, nullptr, 0, 0) && squad.states == std::vector<S_SquadSoldier *>({n8, a, n6, n4});
            // A null soldier kept; a negative slot ends the slots (those after it still keep their soldiers).
            const int slots5[4] = {6, -1, 8, 2};
            for (int i = 0; i < 4; ++i)
                m.m_slots[i].profile = slots5[i];
            squad.states = {n8, nullptr, n6};
            g_gfLog.clear();
            ok &= C_SquadMenu::SyncSquad(&m, nullptr, nullptr, 0, 0) && squad.states == std::vector<S_SquadSoldier *>({n6, nullptr, n8}) && n8->profileIndex == 8 &&
                  MsLogIs({"find state 6"}, g_gfLog);
            squad.states.clear();
            delete a;
            // Roster soldier 0: in a slot (a freed soldier at the squad's start reused), not in one (freed, deleted);
            // ordered.
            const auto slots = [&](int s0, int s1) {
                m.m_slots[0].profile = s0;
                m.m_slots[1].profile = s1;
                m.m_slots[2].profile = -1;
                m.m_slots[3].profile = -1;
            };
            S_SquadSoldier *z = soldier(3, 1);
            squad.states = {z};
            slots(0, -1);
            ok &= C_SquadMenu::SyncSquad(&m, nullptr, nullptr, 0, 0) && squad.states == std::vector<S_SquadSoldier *>({z}) &&
                  z->profileIndex == 0;
            S_SquadSoldier *u = soldier(2, 2);
            squad.states = {z, u};
            slots(2, -1);
            ok &= C_SquadMenu::SyncSquad(&m, nullptr, nullptr, 0, 0) && squad.states == std::vector<S_SquadSoldier *>({u});
            z = soldier(0, 1);
            squad.states = {u, z};
            slots(0, 2);
            ok &= C_SquadMenu::SyncSquad(&m, nullptr, nullptr, 0, 0) && squad.states == std::vector<S_SquadSoldier *>({z, u});
            clearSquad();
            // (Roster soldier 0's squad soldier not reused for another slot; a freed one at the start deleted.)
            z = soldier(0, 1);
            squad.states = {z};
            slots(0, 5);
            ok &= C_SquadMenu::SyncSquad(&m, nullptr, nullptr, 0, 0) && squad.states.size() == 2 && squad.states[0] == z &&
                  z->profileIndex == 0 && squad.states[1]->profileIndex == 5;
            squad.states.clear();
            squad.states = {soldier(3, 1)};
            slots(-1, -1);
            ok &= C_SquadMenu::SyncSquad(&m, nullptr, nullptr, 0, 0) && squad.states.empty();
            WM(r.sqSync, ok);
        }

        // --- the messages: Escape (the selection screen closed; the question), continue (the selected slot's
        // soldier first, the squad kept, the next step), a free soldier selected ---
        {
            bool ok = true;
            C_SquadMenu m;
            m.m_mode = 2;
            S_ControlMessage message;
            const auto send = [&](uint32_t id, uint32_t value = 0) {
                message.id = id;
                message.value = value;
                g_gfLog.clear();
                g_controlMessages.clear();
                m.OnMessage(&message);
                return CsQueue();
            };
            C_MsScreen squadScreen(nullptr, "sqsquad", 0x04000000), selection(nullptr, "sqselection", 0x08000000);
            m.m_messageBox = nullptr;
            g_menuScreens.clear();
            ok &= send(MENU_MSG_ESCAPE) == "" && g_gfLog.empty();
            g_menuScreens = {&squadScreen, &selection};
            ok &= send(CONTROL_NOTIFY_CLICK | MENU_MSG_ESCAPE) == "0 6001005 0 0; " && g_gfLog.empty();
            {
                C_MessageBox box(nullptr, GfFrame(FRAME_DUMMY, "sqmsgbox"));
                m.m_messageBox = &box;
                g_menuScreens = {&selection, &squadScreen};
                const std::string q = send(MENU_MSG_ESCAPE);
                ok &= box.GetKind() == 1 && box.m_yesMessage == MENU_MSG_PREVIOUS && box.m_noMessage == 0 &&
                      q.find("100000 1000015 18 7d4") != std::string::npos && q.find("6001005") == std::string::npos;
                m.CloseMessage();
                m.m_messageBox = nullptr;
            }
            // Continue: slot 2's soldier into the first.
            roster(10);
            const uint32_t pictures[4] = {0x4111000, 0x4112000, 0x4113000, 0x4114000};
            for (int i = 0; i < 4; ++i) {
                m.m_slots[i].picture = pictures[i];
                m.m_slots[i].name = pictures[i] + 0x100;
                m.m_slots[i].rank = pictures[i] + 0x200;
            }
            m.m_slots[0].profile = 0;
            m.m_slots[0].locked = true;
            m.m_slots[1].profile = 5;
            m.m_slots[1].locked = true;
            m.m_slots[2].profile = 3;
            m.m_slots[2].locked = false;
            m.m_slots[3].profile = -1;
            squad.states = {soldier(0, 1), soldier(5, 2), soldier(3, 3)};
            m.m_selectedSlot = 2;
            std::string q = send(0x200006A);
            ok &= m.m_slots[0].profile == 3 && !m.m_slots[0].locked && m.m_slots[2].profile == 0 && m.m_slots[2].locked &&
                  m.m_slots[1].profile == 5 && squad.states.size() == 3 && squad.states[0]->profileIndex == 3 &&
                  squad.states[1]->profileIndex == 5 && squad.states[2]->profileIndex == 0;
            ok &= q.find("4111000 1000009 3 0; ") == 21 && q.find("4113000 1000009 0 0; ") != std::string::npos &&
                  q.find("4113000 1000009 0 0; ") > q.find("4111000 1000009 3 0; ") && q.size() > 15 &&
                  q.substr(q.size() - 15) == "0 2000008 0 0; ";
            // The first selected: itself; out of the slots: nothing.
            m.m_selectedSlot = 0;
            q = send(0x200006A);
            ok &= m.m_slots[0].profile == 3 && !m.m_slots[0].locked && q.substr(q.size() - 15) == "0 2000008 0 0; ";
            m.m_selectedSlot = 4;
            ok &= send(0x200006A) == "" && g_gfLog.empty();
            m.m_selectedSlot = -1;
            ok &= send(0x200006A) == "" && g_gfLog.empty();
            ok &= send(0x2000071) == "" && g_gfLog.empty();
            // A free soldier selected (the 5th: the 4th in a slot).
            C_MsScreen roster4(nullptr, "sqroster", 0x4101050);
            g_menuScreens = {&roster4};
            ok &= send(0x2000072, 3) == "" && MsLogIs({"soldier control 1 4101050"}, g_gfLog);
            clearSquad();
            g_menuScreens.clear();
            WM(r.sqMessage, ok);
        }

        // --- a free soldier selected: the first from the index (at most the 30th), else the last before it ---
        {
            bool ok = true;
            C_SquadMenu m;
            C_MsScreen top(nullptr, "sqtop", 0);
            const auto select = [&](int index, uint32_t id) {
                top.m_id = id;
                g_menuScreens = {&top};
                g_gfLog.clear();
                m.SelectFreeSoldier(index);
                return g_gfLog.empty() ? std::string("none") : g_gfLog[0];
            };
            roster(5);
            m.m_slots[0].profile = 1;
            m.m_slots[1].profile = 2;
            ok &= select(-1, 0x4101000) == "none" && select(1, 0x4101040) == "soldier control 1 4101040";
            ok &= select(0, 0x4101010) == "soldier control 1 4101010" && select(4, 0x4101050) == "soldier control 1 4101050";
            ok &= select(5, 0x4101050) == "soldier control 1 4101050" && select(1, 0x4101030) == "none";
            m.m_slots[0].profile = 3;
            m.m_slots[1].profile = 4;
            ok &= select(3, 0x4101030) == "soldier control 1 4101030" && select(9, 0x4101090) == "soldier control 1 4101090";
            // (Back to the first.)
            roster(3);
            m.m_slots[0].profile = 1;
            m.m_slots[1].profile = 2;
            ok &= select(3, 0x4101010) == "soldier control 1 4101010" && select(2, 0x4101010) == "soldier control 1 4101010";
            roster(2);
            m.m_slots[0].profile = 0;
            m.m_slots[1].profile = 1;
            ok &= select(0, 0x4101010) == "none" && select(2, 0x4101010) == "none" && select(1, 0x4101020) == "none";
            roster(35);
            m.m_slots[0].profile = -1;
            m.m_slots[1].profile = -1;
            ok &= select(31, 0x41011F0) == "soldier control 1 41011f0" && select(29, 0x41011E0) == "soldier control 1 41011e0" &&
                  select(30, 0x41011E0) == "soldier control 1 41011e0";
            m.m_slots[2].profile = 29;
            ok &= select(29, 0x41011D0) == "soldier control 1 41011d0" && select(29, 0x41011E0) == "none";
            g_menuScreens.clear();
            g_gfLog.clear();
            m.SelectFreeSoldier(4);
            ok &= g_gfLog.empty();
            g_menuScreens.clear();
            WM(r.sqSelect, ok);
        }

        g_epProfiles = oldProfiles;
        g_gameProfile.m_humanStore = oldStore;
        g_gameProfile.m_missionSquad = oldSquad;
        g_gameProfile.m_playMode = oldPlayMode;
        g_controlMessages.clear();
    }
    g_driverVtable[0x54 / 4] = oldCreate;
    g_mrModelVt[0x48 / 4] = oldFind;
    g_gameProfile.m_campaign = oldCampaign;
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("campaignscreens: oeTable %d oeDescribe %d oeBuild %d oeInput %d oeSlots %d smStart %d smMessage %d "
                "smConsole %d smPassword %d smBuild %d smTick %d smJoin %d smNotify %d vsCredits %d vsMovie %d vsFit %d "
                "vsUpdate %d vsScroll %d vsRender %d vsInput %d vsMovieState %d dbMedal %d dbResults %d dbStore %d dbStart %d "
                "dbMessage %d dbBuild %d dbDiary %d dbDiaryPage %d dbWon %d dbScreen %d dbPanel %d dbObjectives %d dbResult %d dbShow %d dbSkills %d "
                "dbRank %d dbMedals %d dbScroll %d sqSlot %d sqOneSoldier %d sqStart %d sqSlots %d sqSync %d "
                "sqMessage %d sqSelect %d\n", r.oeTable, r.oeDescribe,
                r.oeBuild, r.oeInput, r.oeSlots, r.smStart, r.smMessage, r.smConsole, r.smPassword, r.smBuild, r.smTick,
                r.smJoin, r.smNotify, r.vsCredits, r.vsMovie, r.vsFit, r.vsUpdate, r.vsScroll, r.vsRender, r.vsInput,
                r.vsMovieState, r.dbMedal, r.dbResults, r.dbStore, r.dbStart, r.dbMessage, r.dbBuild, r.dbDiary, r.dbDiaryPage, r.dbWon,
                r.dbScreen, r.dbPanel, r.dbObjectives, r.dbResult, r.dbShow, r.dbSkills,
                r.dbRank, r.dbMedals, r.dbScroll, r.sqSlot, r.sqOneSoldier, r.sqStart, r.sqSlots, r.sqSync,
                r.sqMessage, r.sqSelect);
}
