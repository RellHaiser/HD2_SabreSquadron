// Part 9e-v's in-game menu (game/game_menu.cpp: the class and the menu state's slots), in the factory
// build, run by ControlsTest after LoadoutTest: the slots not ported yet, the info screen, the
// sound's volume, the stocks' reset and the team logging fakes; the game behind a state logging
// its slots. Prints "gamemenu:" with 1 per check met.
#define private public
#define protected public
#include "game/game_menu.h"
#undef protected
#undef private
#include "game/game_app.h"
#include "game/interface.h"
#include "game/team.h"

// (Not ported yet: logged.)
// (Part 9e-ix: the difficulty; the screens' builders not ported yet: logged.)
int g_gmDifficulty;
int C_GameProfile::GetDifficulty() const { return g_gmDifficulty; }
bool g_gmCanQuickSave = true;
bool C_game::CanQuickSave() const { return g_gmCanQuickSave; }
// (Their descriptions' names and control counts, their models; the options' made inactive by a
// screen message.)
std::string MrFrameName(const void *frame);
bool C_GameMenu::BuildPlayerScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    GfLog("build player %s %d %s", desc->name.c_str(), static_cast<int>(desc->controls.size()),
          model ? MrFrameName(model).c_str() : "none");
    return true;
}
bool C_OptionsHandler::BuildScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    GfLog("build options %s %d %s", desc->name.c_str(), static_cast<int>(desc->controls.size()),
          model ? MrFrameName(model).c_str() : "none");
    S_ControlMessage inactive;
    inactive.id = CONTROL_MSG_DEACTIVATE;
    desc->messages.push_back(inactive);
    return true;
}
bool C_GameMenu::OnItemNotify13(void *, C_Control *, uint32_t, uint32_t) { return true; }
bool C_GameMenu::OnItemNotify60(void *, C_Control *, uint32_t, uint32_t) { return true; }
bool C_GameMenu::OnItemNotify0A(void *, C_Control *, uint32_t, uint32_t) { return true; }
bool C_GameMenu::OnItemNotify08(void *, C_Control *, uint32_t, uint32_t) { return true; }
bool C_GameMenu::OnItemNotify09(void *, C_Control *, uint32_t, uint32_t) { return true; }
bool C_GameMenu::OnItemHover(void *, C_Control *, uint32_t, uint32_t) { return true; }

bool g_gmInfoInit = true, g_gmInfoShown;
I3D_scene *g_gmInfoScene;
bool C_InfoScreen::Init(I3D_scene *scene)
{
    GfLog("info init");
    g_gmInfoScene = scene;
    return g_gmInfoInit;
}
void C_InfoScreen::Done() { GfLog("info done"); }
bool C_InfoScreen::IsShown() const { return g_gmInfoShown; }
void C_InfoScreen::Render() { GfLog("info render"); }
void C_ItemStock::Reset() { GfLog("stock reset"); }
void C_PlayerProfile::ApplyVolume() { GfLog("volume"); }
float C_SoundModule::SetMasterVolume(float volume)
{
    GfLog("master %g", volume);
    return 1.0f;
}
bool g_gmSlots;
bool C_Team::HasMemberInPlayerSlots() const { return g_gmSlots; }
// (Part 9e-vi: the network status.) The humans' uniforms, the teams' counts, the times left.
std::map<const C_Human *, C_Item *> g_gmUniforms;
C_Item *C_Human::GetUniformItem() const
{
    const auto it = g_gmUniforms.find(this);
    return it == g_gmUniforms.end() ? nullptr : it->second;
}
int g_gmAxis, g_gmAllies, g_gmRespawnLeft, g_gmRoundLeft;
void C_NetGame::CountTeams(int *axis, int *allies, bool pending, bool inGame)
{
    GfLog("count teams %d %d", pending, inGame);
    *axis = g_gmAxis;
    *allies = g_gmAllies;
}
int C_game::GetRespawnTimeLeft() const { return g_gmRespawnLeft; }
int C_game::GetRoundTimeLeft() const { return g_gmRoundLeft; }
// (Part 9e-vii: the messages.) The squad, the app (zeroed memory; its commands logged), the info
// screen hidden. (The options handler is options_test.inc's.)
S_MissionSquad *&g_missionSquad = g_gameProfile.m_missionSquad;
alignas(16) uint8_t g_gmAppMemory[sizeof(C_GameApp)];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_gmAppMemory); }
void C_GameApp::PostCommand(int command, intptr_t a, intptr_t b, bool flush)
{
    GfLog("command %d %d %d %d", command, static_cast<int>(a), static_cast<int>(b), flush);
}
void C_InfoScreen::Show(bool show) { GfLog("info show %d", show); }

namespace {

struct S_GameMenuResults {
    bool gmNew = true, gmNewClient = true, gmStart = true, gmStartNet = true, gmStop = true, gmInput = true;
    bool gmUpdate = true, gmTick = true, gmDraw = true, gmMessage = true, gmObjectives = true, gmBase = true;
    bool gmLoad = true, gmCoop = true, gmNet = true, gmRender = true, gmRenderGame = true, gmRenderEquip = true;
    bool gmOptions = true, gmEscape = true, gmEscapeNet = true, gmSquad = true, gmQuestions = true;
    bool gmMainLocal = true, gmMainNet = true, gmBuild = true, gmBuildNet = true;
};

// A uniform of a side (its type's team).
struct C_GmDef : C_SlDef {
    explicit C_GmDef(int team) : C_SlDef(ITEM_STATIC, 0, -1, -1) { m_team = team; }
};
struct C_GmItem : C_Item {
    explicit C_GmItem(C_ItemDef *def) : C_Item(static_cast<E_ItemKind>(0)) { m_def = def; }
    int Fire(C_Actor *, const S_vector &, const S_vector &, uint32_t) override { return 0; }
    int FireSecondary(C_Actor *, const S_vector &, const S_vector &, uint8_t) override { return 0; }
};

// A game state of the game's type, its scene where C_game keeps it.
struct C_GmGameOne : C_GameState {
    int GetTypeId() const override { return GAME_STATE_TYPE_GAME; }
};
// The game scene's callback: the one set logged, the previous returned (first 0x1234).
void *g_gmCallback = reinterpret_cast<void *>(0x1234);
void *__stdcall GmSetRenderCallback(void *, void *callback, uint32_t flags)
{
    GfLog("gscene callback %x %x", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(callback)), flags);
    void *const old = g_gmCallback;
    g_gmCallback = callback;
    return old;
}
uint32_t __stdcall GmClear(void *, uint32_t color, float z, uint32_t flags)
{
    GfLog("clear %u %g %u", color, z, flags);
    return 0;
}

// The game behind the menu: its slots logged.
struct C_GmGame : C_GameState {
    bool HandleInput(float *held, float *pressed) override
    {
        GfLog("game input %d %d", held != nullptr, pressed != nullptr);
        return false;
    }
    void Tick(int timeDelta) override { GfLog("game tick %d", timeDelta); }
    void Update(int timeDelta) override { GfLog("game update %d", timeDelta); }
};

// The mission's objectives (the definition zeroed memory; its vector reached).
struct C_GmMission : C_MissionDef {
    using C_MissionDef::m_objectives;
};

// The base's fields (protected; equipment_menu.h included before): their first values.
struct C_GmBasePeek : C_EquipmentMenuBase {
    static bool Fresh(const C_EquipmentMenuBase &b)
    {
        const S_SoldierItem &item = b.*&C_GmBasePeek::m_unknown1B8;
        return b.*&C_GmBasePeek::m_unknown1B0 == 0 && b.*&C_GmBasePeek::m_unknown1B4 == 0 && item.uid == -1 &&
               item.typeId == -1 && item.present == 0 && b.*&C_GmBasePeek::m_unknown1C0 == -1;
    }
};

} // namespace

void GameMenuTest(I3D_scene *scene)
{
    S_GameMenuResults r;
    g_gfLog.clear();
    const uint32_t oldRole = g_gameRole;
    const std::map<std::string, std::vector<uint8_t>> oldFiles = g_files;
    C_PlayerProfile player;
    C_PlayerProfile *const oldPlayer = g_gameProfile.m_player;
    g_gameProfile.m_player = &player;
    alignas(16) static uint8_t campaignMemory[sizeof(C_Campaign)];
    std::memset(campaignMemory, 0, sizeof campaignMemory);
    C_Campaign *const oldCampaign = g_gameProfile.m_campaign;
    g_gameProfile.m_campaign = reinterpret_cast<C_Campaign *>(campaignMemory);
    g_lpMissions = {2};
    // presets.def: one preset; no user file (the defaults made).
    {
        std::vector<uint8_t> presets(8 + sizeof(S_LoadoutPreset));
        reinterpret_cast<int *>(presets.data())[0] = 2;
        reinterpret_cast<int *>(presets.data())[1] = 1;
        g_files["GameData\\presets.def"] = presets;
    }
    g_files.erase("prof\\user_presets.def");
    g_files.erase("GameData\\miss_itm.def");

    // --- made: the menu state's, the presets loaded, single player the missions' items set up; the
    // fields' first values ---
    {
        g_gameRole = GAME_ROLE_LOCAL;
        g_gfLog.clear();
        auto *m = new C_GameMenu();
        WM(r.gmNew, MsLogIs({"operations 0", "missions 0 0"}, g_gfLog) && g_menuState == m &&
                        m->GetTypeId() == GAME_STATE_TYPE_GAME_MENU && m->m_presets.GetPresetCount() == 1 &&
                        m->m_presets.GetUserPresetCount() == 24 && m->m_missionItems.m_records[0] &&
                        m->m_missionItems.m_records[1] && !m->m_missionItems.m_records[2]);
        WM(r.gmNew, C_GmBasePeek::Fresh(*m) &&
                        !m->m_teamChangePosted && !m->m_respawnOnApply && m->m_team == -1 && m->m_unknown1D8 == -1 &&
                        m->m_unknown1DC == -1 && !m->m_loadButtonOn && !m->m_stock1E4 && !m->m_stock1E8 &&
                        !m->m_options && !m->m_infoScreen && m->m_unknown4C0 == 0 && m->m_unknown4C4 == -1 &&
                        !m->m_unknown4D8 && !m->m_unknown4D9 && !m->m_hasPlayerSlotMember && !m->GetGame());
        m->SetRespawnOnApply(true);
        C_GmGame game;
        m->SetGame(&game);
        WM(r.gmNew, m->m_respawnOnApply && m->GetGame() == &game);
        // Deleted: the game behind kept, the menu state's cleared.
        g_gfLog.clear();
        delete m;
        WM(r.gmNew, !g_menuState && g_gfLog.empty());
    }

    // --- a network game's: the missions' items not set up ---
    {
        g_gameRole = GAME_ROLE_CLIENT;
        g_gfLog.clear();
        C_GameMenu m;
        WM(r.gmNewClient, g_gfLog.empty() && !m.m_missionItems.m_records[0] && m.m_presets.GetPresetCount() == 1);
    }

    // --- started (single player): the player slots' member, the options handler (the menu's), the
    // sound off; no info screen ---
    {
        g_gameRole = GAME_ROLE_LOCAL;
        C_GameMenu m;
        g_gmSlots = true;
        g_gfLog.clear();
        bool ok = m.OnStart(nullptr) && m.m_hasPlayerSlotMember && m.m_options && m.m_options->m_menu == &m &&
                  !m.m_options->m_cursor && m.m_options->m_fillOnOpen && m.m_options->m_screen == 0 && !m.m_infoScreen;
        ok &= MsLogIs({"master 0"}, g_gfLog);
        g_gmSlots = false;
        m.OnStop();
        ok &= !m.m_options;
        WM(r.gmStart, ok);
    }

    // --- started (a network game): the info screen on the menu's scene; not made: none ---
    {
        g_gameRole = GAME_ROLE_SERVER;
        C_GameMenu m;
        m.m_scene = scene;
        g_gfLog.clear();
        bool ok = m.OnStart(nullptr) && !m.m_hasPlayerSlotMember && m.m_infoScreen && g_gmInfoScene == scene;
        ok &= MsLogIs({"master 0", "info init"}, g_gfLog);
        m.OnStop();
        C_GameMenu n;
        g_gmInfoInit = false;
        ok &= n.OnStart(nullptr) && !n.m_infoScreen;
        g_gmInfoInit = true;
        n.OnStop();
        WM(r.gmStartNet, ok);
    }

    // --- stopped: the volume, the options handler (its cursor released), the popup, the network game's
    // info screen, the stocks emptied and deleted (not again) ---
    {
        g_gameRole = GAME_ROLE_CLIENT;
        C_GameMenu m;
        m.OnStart(nullptr);
        m.m_options->m_cursor = C_Cursor::Acquire(scene);
        m.m_popup = new C_MsScreen(scene, "gmpopup", 0x04000000);
        m.m_stock1E4 = new C_ItemStock();
        m.m_stock1E8 = new C_ItemStock();
        g_gfLog.clear();
        m.OnStop();
        bool ok = MsLogIs({"volume", "cursor release", "cursor release", "info done", "stock reset", "stock reset"}, g_gfLog) &&
                  !m.m_options && !m.m_popup && !m.m_infoScreen && !m.m_stock1E4 && !m.m_stock1E8;
        g_gfLog.clear();
        m.OnStop();
        ok &= MsLogIs({"volume"}, g_gfLog);
        // Single player: an info screen there not stopped.
        g_gameRole = GAME_ROLE_LOCAL;
        C_GameMenu n;
        n.m_infoScreen = new C_InfoScreen();
        C_InfoScreen *const info = n.m_infoScreen;
        g_gfLog.clear();
        n.OnStop();
        ok &= MsLogIs({"volume"}, g_gfLog) && n.m_infoScreen == info;
        delete info;
        n.m_infoScreen = nullptr;
        WM(r.gmStop, ok);
    }

    // --- the input: the game's (none); true ---
    {
        C_GameMenu m;
        float held[4] = {}, pressed[4] = {};
        g_gfLog.clear();
        bool ok = m.OnInput(held, pressed) && g_gfLog.empty();
        C_GmGame game;
        m.SetGame(&game);
        ok &= m.OnInput(held, pressed) && MsLogIs({"game input 0 0"}, g_gfLog);
        WM(r.gmInput, ok);
    }

    // --- the update: the network games' soldier, weight message and players; a client: the game's ---
    {
        bool ok = true;
        C_GmGame game;
        const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_CLIENT, GAME_ROLE_SERVER, GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
        // (The objectives game: the teams counted.)
        const int oldType = g_mpSettings->gameType;
        g_mpSettings->gameType = MP_GAME_OBJECTIVES;
        const std::vector<std::vector<std::string>> want = {
            {"game update 7"}, {"count teams 0 0", "game update 7"}, {"count teams 0 0"}, {"count teams 0 0", "game update 7"}};
        for (int i = 0; i < 4; ++i) {
            g_gameRole = GAME_ROLE_CLIENT;
            C_GameMenu m;
            g_gameRole = roles[i];
            m.SetGame(&game);
            g_gfLog.clear();
            g_controlMessages.clear();
            m.OnUpdate(7);
            // (The network games: the status posted, the round's time last.)
            ok &= MsLogIs(want[static_cast<size_t>(i)], g_gfLog) &&
                  (i == 0 ? g_controlMessages.empty() : g_controlMessages.back().target == 0x0C900000);
            m.SetGame(nullptr);
            g_gfLog.clear();
            g_controlMessages.clear();
            m.OnUpdate(7);
            ok &= g_gfLog.size() == (i == 0 ? 0u : 1u) && g_controlMessages.empty() == (i == 0);
            g_controlMessages.clear();
        }
        // The panel's soldier ticked (the network games), then deleted by the stop.
        g_gameRole = GAME_ROLE_CLIENT;
        C_GameMenu m;
        C_MenuSoldier *soldier = new C_MenuSoldier();
        MsWorld(*soldier);
        m.m_panel.m_soldier = soldier;
        g_gfLog.clear();
        m.OnUpdate(9);
        ok &= std::find(g_gfLog.begin(), g_gfLog.end(), "ss 20 9") != g_gfLog.end();
        g_controlMessages.clear();
        g_mpSettings->gameType = oldType;
        // (As equipment_panel_test.inc deletes it.)
        delete soldier->m_pose;
        soldier->m_pose = nullptr;
        soldier->m_model = nullptr;
        for (auto &anim : soldier->m_anims)
            anim = nullptr;
        m.OnStop();
        ok &= !m.m_panel.m_soldier;
        WM(r.gmUpdate, ok);
    }

    // --- the tick: the menu time; the server: the game's ---
    {
        bool ok = true;
        C_GmGame game;
        g_gameRole = GAME_ROLE_CLIENT;
        C_GameMenu m;
        m.SetGame(&game);
        const int time = g_gameMenuTime;
        g_gfLog.clear();
        m.OnTick(100);
        ok &= g_gfLog.empty() && g_gameMenuTime == time + 100;
        g_gameRole = GAME_ROLE_SERVER;
        m.OnTick(30);
        ok &= MsLogIs({"game tick 30"}, g_gfLog) && g_gameMenuTime == time + 130;
        g_gameRole = GAME_ROLE_LOCAL;
        g_gfLog.clear();
        m.OnTick(5);
        ok &= MsLogIs({"game tick 5"}, g_gfLog) && g_gameMenuTime == time + 135;
        m.SetGame(nullptr);
        g_gfLog.clear();
        m.OnTick(5);
        ok &= g_gfLog.empty() && g_gameMenuTime == time + 140;
        WM(r.gmTick, ok);
    }

    // --- the screens drawn: the info screen when shown; false ---
    {
        g_gameRole = GAME_ROLE_CLIENT;
        C_GameMenu m;
        g_gfLog.clear();
        bool ok = !m.OnDrawScreens() && g_gfLog.empty();
        m.m_infoScreen = new C_InfoScreen();
        g_gmInfoShown = false;
        ok &= !m.OnDrawScreens() && g_gfLog.empty();
        g_gmInfoShown = true;
        ok &= !m.OnDrawScreens() && MsLogIs({"info render"}, g_gfLog);
        g_gmInfoShown = false;
        delete m.m_infoScreen;
        m.m_infoScreen = nullptr;
        WM(r.gmDraw, ok);
    }

    // --- a message: the box (counted), not over the box on top; below 1: closed (counted down) ---
    {
        bool ok = true;
        C_GameMenu m;
        const int count = g_gameMenuMessages;
        m.ShowMessage(0x55, -1);
        ok &= g_gameMenuMessages == count + 1;
        C_MsScreen box(scene, "gmbox", 0x00000005), other(scene, "gmother", 0x08000005);
        g_menuScreens.push_back(&box);
        m.ShowMessage(0x56, -1);
        ok &= g_gameMenuMessages == count + 1;
        g_menuScreens.back() = &other;
        m.ShowMessage(0x57, -1);
        ok &= g_gameMenuMessages == count + 2;
        m.ShowMessage(0, -1);
        m.ShowMessage(-3, -1);
        ok &= g_gameMenuMessages == count;
        // (Screen index 1, not the box's.)
        C_MsScreen one(scene, "gmone", 0x04000005);
        g_menuScreens.back() = &one;
        m.ShowMessage(1, -1);
        ok &= g_gameMenuMessages == count + 1;
        g_menuScreens.pop_back();
        // The menu's box: shown (kind 0, the text id), closed.
        C_MessageBox real(scene, GfFrame(FRAME_DUMMY, "gmrealbox"));
        m.m_messageBox = &real;
        g_controlMessages.clear();
        m.ShowMessage(0x4D, -1);
        ok &= real.m_kind == 0 && ScQueue().find("100000 1000015 18 4d ; ") != std::string::npos;
        m.ShowMessage(0, -1);
        ok &= real.m_kind == MESSAGE_BOX_NONE && g_gameMenuMessages == count + 1;
        m.m_messageBox = nullptr;
        g_controlMessages.clear();
        m.ShowMessage(-1, -1);
        WM(r.gmMessage, ok && g_gameMenuMessages == count);
    }

    // --- the mission's objectives decided: some active, none open ---
    {
        bool ok = true;
        alignas(16) static uint8_t missionMemory[sizeof(C_MissionDef)];
        alignas(16) static uint8_t objectiveMemory[3][sizeof(S_MissionObjective)];
        std::memset(missionMemory, 0, sizeof missionMemory);
        std::memset(objectiveMemory, 0, sizeof objectiveMemory);
        auto *mission = reinterpret_cast<C_GmMission *>(missionMemory);
        S_MissionObjective *o[3];
        for (int i = 0; i < 3; ++i)
            o[i] = reinterpret_cast<S_MissionObjective *>(objectiveMemory[i]);
        const auto decided = [&](int a, int b, int c) {
            o[0]->status = a;
            o[1]->status = b;
            o[2]->status = c;
            return mission->AreObjectivesDecided();
        };
        ok &= !mission->AreObjectivesDecided();
        mission->m_objectives = {o[0], o[1], o[2]};
        ok &= !decided(4, 4, 4) && decided(4, 1, 2) && decided(1, 4, 3) && !decided(4, 1, 0) && !decided(0, 4, 4) &&
              !decided(1, 0, 4) && decided(4, 4, 1);
        mission->m_objectives = std::vector<S_MissionObjective *>();
        WM(r.gmObjectives, ok);
    }

    // --- the load's button: on (posted once) with a uniform and the load within the strength (1 too);
    // else off (posted once; NaN, no uniform, no soldier) ---
    {
        bool ok = true;
        C_GameMenu m;
        S_SquadSoldier s;
        s.uniform.typeId = 21;
        g_controlMessages.clear();
        const auto run = [&](float fill) {
            g_epWeightFill = fill;
            m.UpdateLoadButton();
            return ScQueue();
        };
        ok &= run(0.5f).empty() && !m.m_loadButtonOn;
        m.m_panel.m_squadSoldier = &s;
        ok &= run(0.5f) == "ca00000 1000002 0 0 ; " && m.m_loadButtonOn && run(1.0f).empty();
        ok &= run(1.01f) == "ca00000 1000001 0 0 ; " && !m.m_loadButtonOn && run(2.0f).empty();
        ok &= run(1.0f) == "ca00000 1000002 0 0 ; " && run(std::nanf("")) == "ca00000 1000001 0 0 ; ";
        ok &= run(0.0f) == "ca00000 1000002 0 0 ; ";
        s.uniform.typeId = -1;
        ok &= run(0.0f) == "ca00000 1000001 0 0 ; ";
        s.uniform.typeId = 21;
        ok &= run(0.0f) == "ca00000 1000002 0 0 ; ";
        m.m_panel.m_squadSoldier = nullptr;
        ok &= run(0.0f) == "ca00000 1000001 0 0 ; " && run(0.0f).empty();
        g_epWeightFill = 0.0f;
        WM(r.gmLoad, ok);
    }

    // --- the co-op game's status: the list cleared, its title, per connected player its rank (0 .. 7)
    // and name, its human's state (none, dead, alive) ---
    {
        bool ok = true;
        const bool oldCoop = g_coop, oldTextIds = g_textIds;
        g_coop = true;
        g_textIds = true;
        C_Human dead, alive;
        dead.health = 0;
        alive.health = 5;
        S_NetPlayerState *p = g_grPlayers;
        for (int i = 0; i < 5; ++i)
            p[i] = S_NetPlayerState();
        std::strcpy(p[0].name, "ann");
        p[0].stats[5] = 9;
        std::strcpy(p[2].name, "bob");
        p[2].stats[5] = -2;
        p[2].human = &dead;
        std::strcpy(p[3].name, "cy");
        p[3].stats[5] = 3;
        p[3].human = &alive;
        std::strcpy(p[4].name, "dee");
        g_grPlayerCount = 4;
        g_controlMessages.clear();
        g_gfLog.clear();
        C_GameMenu::UpdateNetStatus();
        ok &= MsLogIs({"lock", "unlock"}, g_gfLog);
        ok &= ScQueue() == "d300000 2000018 0 0 ; d300000 1000017 56 4f4c ; "
                           "d300000 1000016 57 0 text 10137 ann; d300000 1000017 58 4f4d ; "
                           "d300000 1000016 57 0 text 10130 bob; d300000 1000017 58 4f4e ; "
                           "d300000 1000016 57 0 text 10133 cy; d300000 1000017 58 4f4f ; ";
        g_grPlayerCount = 0;
        for (int i = 0; i < 5; ++i)
            p[i] = S_NetPlayerState();
        g_coop = oldCoop;
        g_textIds = oldTextIds;
        WM(r.gmCoop, ok);
    }

    // --- the other games' status: the teams' counts (the deathmatch's by the uniforms' sides), the
    // occupation's respawn time, the round's time (" m:ss") ---
    {
        bool ok = true;
        const int oldType = g_mpSettings->gameType;
        g_gmAxis = 3;
        g_gmAllies = 4;
        g_gmRespawnLeft = 65999;
        g_gmRoundLeft = 600000;
        g_mpSettings->gameType = MP_GAME_OCCUPATION;
        g_controlMessages.clear();
        g_gfLog.clear();
        C_GameMenu::UpdateNetStatus();
        ok &= MsLogIs({"count teams 0 0"}, g_gfLog) &&
              ScQueue() == "c104000 1000078 4 0 ; c105000 1000078 3 0 ; c800000 1000018 2d 1   1:05; "
                           "c900000 1000018 2d 1  10:00; ";
        g_mpSettings->gameType = MP_GAME_OBJECTIVES;
        g_gmRoundLeft = 59000;
        C_GameMenu::UpdateNetStatus();
        ok &= ScQueue() == "c104000 1000078 4 0 ; c105000 1000078 3 0 ; c900000 1000018 2d 1   0:59; ";
        // The deathmatch: the players with a human wearing a uniform of side 0 (allied) or 1 (axis).
        C_GmDef allied(0), axis(1), all(2);
        C_GmItem u0(&allied), u1(&axis), u2(&allied), u3(&all), u4(&axis);
        C_Human h[5];
        g_gmUniforms = {{&h[0], &u0}, {&h[1], &u1}, {&h[2], &u2}, {&h[3], &u3}, {&h[4], &u4}};
        S_NetPlayerState *p = g_grPlayers;
        for (int i = 0; i < 7; ++i) {
            p[i] = S_NetPlayerState();
            p[i].human = i < 5 ? &h[i] : nullptr;
        }
        // (Past the count: not counted.)
        p[6].human = &h[0];
        g_grPlayerCount = 6;
        g_mpSettings->gameType = MP_GAME_DEATHMATCH;
        g_gfLog.clear();
        C_GameMenu::UpdateNetStatus();
        ok &= MsLogIs({"lock", "unlock"}, g_gfLog) &&
              ScQueue() == "c104000 1000078 2 0 ; c105000 1000078 2 0 ; c900000 1000018 2d 1   0:59; ";
        g_grPlayerCount = 0;
        for (int i = 0; i < 7; ++i)
            p[i] = S_NetPlayerState();
        g_gmUniforms.clear();
        g_mpSettings->gameType = oldType;
        WM(r.gmNet, ok);
    }

    // --- drawn: by the top screen (the equipment's, index 3, or the message box over it: the equipment's
    // drawing; else the game's); no screen: the game's scene (the game's type; else false) ---
    {
        const int slots[] = {0x1C / 4, 0x18 / 4};
        void *oldDriver[2] = {g_driverVtable[slots[0]], g_driverVtable[slots[1]]};
        void *const oldClear = g_igraphVtable[0x64 / 4];
        g_driverVtable[0x1C / 4] = reinterpret_cast<void *>(&MrGetState);
        g_driverVtable[0x18 / 4] = reinterpret_cast<void *>(&MrSetState);
        g_igraphVtable[0x64 / 4] = reinterpret_cast<void *>(&GmClear);
        g_mrStates[5] = 3;
        g_mrStates[6] = 4;
        g_gameRole = GAME_ROLE_CLIENT;
        C_GameMenu m;
        S_MrWorld world = MrWorld("gm");
        m.m_scene = As<I3D_scene>(world.scene);
        alignas(16) static uint8_t gameMemory[sizeof(C_game)];
        std::memset(gameMemory, 0, sizeof gameMemory);
        C_GameState *one = new (gameMemory) C_GmGameOne();
        static void *gameSceneVt[MR_SLOTS];
        std::copy(std::begin(g_mrSceneVt), std::end(g_mrSceneVt), gameSceneVt);
        gameSceneVt[0x74 / 4] = reinterpret_cast<void *>(&GmSetRenderCallback);
        reinterpret_cast<C_game *>(gameMemory)->m_scene = As<I3D_scene>(MrObj("gscene", gameSceneVt));
        C_GmGame other;
        C_MrScreen game(m.m_scene, "ggame", 0x04000000), two(m.m_scene, "gtwo", 0x08000000),
            equip(m.m_scene, "gequip", 0x0C000000), box(m.m_scene, "gbox", 0x00000000);
        const std::vector<std::string> gameDraw = {"clear 0 1 3", "gscene callback 0 0", "gscene 78 0", "gscene callback 1234 6",
                                                   "state 5 0", "state 6 1", "scene 78 0", "state 5 3", "state 6 4"};
        const std::vector<std::string> equipDraw = {"scene 78 0"};
        {
            bool ok = true;
            m.SetGame(one);
            const auto draws = [&](std::initializer_list<C_Screen *> screens, const std::vector<std::string> &want) {
                g_menuScreens = screens;
                g_gfLog.clear();
                return m.OnRender() && MsLogIs(want, g_gfLog);
            };
            ok &= draws({&game}, gameDraw) && draws({&two}, gameDraw) && draws({&equip}, equipDraw) &&
                  draws({&box}, gameDraw);
            m.m_underPopup = &equip;
            ok &= draws({&box}, equipDraw);
            m.m_underPopup = &two;
            const std::vector<std::string> underDraw = {
                "clear 0 1 3", "gscene callback 0 0", "gscene 78 0", "gscene callback 1234 6", "state 5 0", "state 6 1",
                "gbox on 0", "gtwo on 1", "scene 78 0", "gbox on 1", "gtwo on 0", "scene 78 0", "gtwo on 1",
                "state 5 3", "state 6 4"};
            ok &= draws({&box}, underDraw);
            m.m_underPopup = nullptr;
            // No screen: the game's scene alone; another state or none: false.
            ok &= draws({}, {"gscene 78 0"});
            m.SetGame(&other);
            g_gfLog.clear();
            ok &= !m.OnRender() && g_gfLog.empty();
            m.SetGame(nullptr);
            ok &= !m.OnRender() && g_gfLog.empty();
            WM(r.gmRender, ok);
        }

        // The game's drawing: no game (or not the game's type): the clear, false; no scene of the menu's.
        {
            bool ok = true;
            g_menuScreens = {&game};
            g_gfLog.clear();
            ok &= !m.RenderGame() && MsLogIs({"clear 0 1 3"}, g_gfLog);
            m.SetGame(&other);
            g_gfLog.clear();
            ok &= !m.RenderGame() && MsLogIs({"clear 0 1 3"}, g_gfLog);
            m.SetGame(one);
            I3D_scene *const scene = m.m_scene;
            m.m_scene = nullptr;
            g_gfLog.clear();
            ok &= m.RenderGame() && MsLogIs({"clear 0 1 3", "gscene callback 0 0", "gscene 78 0", "gscene callback 1234 6",
                                             "state 5 0", "state 6 1", "state 5 3", "state 6 4"},
                                            g_gfLog);
            m.m_scene = scene;
            // (Under a popup without a top: the scene once.)
            m.m_underPopup = &two;
            g_menuScreens.clear();
            g_gfLog.clear();
            ok &= m.RenderGame() && MsLogIs(gameDraw, g_gfLog);
            m.m_underPopup = nullptr;
            WM(r.gmRenderGame, ok);
        }

        // The equipment's drawing: the scene; the info screen shown: no more; the popup's own popup over
        // the top (driver state 5 cleared); no scene: none drawn.
        {
            bool ok = true;
            C_MrScreen pop(m.m_scene, "gpop", 0x10000000);
            g_menuScreens = {&equip};
            m.m_popup = &pop;
            g_gfLog.clear();
            ok &= m.RenderEquipment() && MsLogIs(equipDraw, g_gfLog);
            pop.popup = true;
            g_gfLog.clear();
            ok &= m.RenderEquipment() && MsLogIs({"scene 78 0", "state 5 0", "gequip on 0", "gpop on 1", "scene 78 0",
                                                  "state 5 3", "gequip on 1", "gpop on 0"},
                                                 g_gfLog);
            m.m_infoScreen = new C_InfoScreen();
            g_gmInfoShown = true;
            g_gfLog.clear();
            ok &= m.RenderEquipment() && MsLogIs(equipDraw, g_gfLog);
            g_gmInfoShown = false;
            g_gfLog.clear();
            ok &= m.RenderEquipment() && g_gfLog.size() == 8;
            // The panel's soldier drawn.
            C_MenuSoldier *soldier = new C_MenuSoldier();
            MsWorld(*soldier);
            m.m_panel.m_soldier = soldier;
            g_gfLog.clear();
            ok &= m.RenderEquipment() && g_gfLog.size() > 8 && MrHas("ss 78 0");
            delete soldier->m_pose;
            soldier->m_pose = nullptr;
            soldier->m_model = nullptr;
            for (auto &anim : soldier->m_anims)
                anim = nullptr;
            m.m_panel.DeleteSoldier();
            delete m.m_infoScreen;
            m.m_infoScreen = nullptr;
            pop.popup = false;
            I3D_scene *const scene = m.m_scene;
            m.m_scene = nullptr;
            g_gfLog.clear();
            ok &= m.RenderEquipment() && g_gfLog.empty();
            m.m_scene = scene;
            m.m_popup = nullptr;
            WM(r.gmRenderEquip, ok);
        }
        g_menuScreens.clear();
        m.SetGame(nullptr);
        one->~C_GameState();
        g_driverVtable[slots[0]] = oldDriver[0];
        g_driverVtable[slots[1]] = oldDriver[1];
        g_igraphVtable[0x64 / 4] = oldClear;
    }

    // --- the messages (part 9e-vii) ---
    {
        S_MissionSquad squad;
        S_MissionSquad *const oldSquad = g_gameProfile.m_missionSquad;
        g_gameProfile.m_missionSquad = &squad;
        g_gameRole = GAME_ROLE_CLIENT;
        C_GameMenu m;
        m.m_options = new C_OptionsHandler(&m);
        const auto message = [&](uint32_t id) {
            S_ControlMessage msg;
            msg.id = id;
            g_gfLog.clear();
            g_controlMessages.clear();
            m.SetState(GAME_STATE_RUNNING);
            m.OnMessage(&msg);
        };
        const auto soldier = [](int team) {
            auto *s = new S_SquadSoldier();
            s->team = team;
            return s;
        };

        // The options handler's first (taken: nothing more).
        {
            message(0x200006F);
            bool ok = MsLogIs({"save options", "hardware option", "keys applied"}, g_gfLog) && g_controlMessages.empty() &&
                      m.GetState() == GAME_STATE_RUNNING;
            message(0x2000099);
            ok &= g_gfLog.empty() && g_controlMessages.empty() && m.GetState() == GAME_STATE_RUNNING;
            // (The code's flag bits masked.)
            message(0x200000A | 0x0C000000);
            ok &= m.GetState() == GAME_STATE_CLOSE_GAME_MENU;
            WM(r.gmOptions, ok);
        }

        // Escape: the info screen shown hidden; screens 2 / 3 back (not in the warmup's menu); single
        // player: closed unless the objectives are decided.
        {
            bool ok = true;
            m.m_infoScreen = new C_InfoScreen();
            g_gmInfoShown = true;
            message(MENU_MSG_ESCAPE);
            ok &= MsLogIs({"info show 0"}, g_gfLog) && m.GetState() == GAME_STATE_RUNNING;
            // (Not shown: on to the screens.)
            g_gmInfoShown = false;
            message(MENU_MSG_ESCAPE);
            ok &= g_gfLog.empty() && m.GetState() == GAME_STATE_CLOSE_GAME_MENU;
            delete m.m_infoScreen;
            m.m_infoScreen = nullptr;
            C_MsScreen one(scene, "gmesc1", 0x04000000), two(scene, "gmesc2", 0x08000000), three(scene, "gmesc3", 0x0C000000);
            for (C_MsScreen *s : {&two, &three}) {
                g_menuScreens = {s};
                message(MENU_MSG_ESCAPE);
                ok &= ScQueue() == "0 2000003 0 0 ; " && m.GetState() == GAME_STATE_RUNNING;
            }
            g_menuScreens = {&one};
            message(MENU_MSG_ESCAPE);
            ok &= g_controlMessages.empty() && m.GetState() == GAME_STATE_CLOSE_GAME_MENU;
            m.m_startArg = reinterpret_cast<void *>(static_cast<intptr_t>(-2));
            g_menuScreens = {&two};
            message(MENU_MSG_ESCAPE);
            ok &= g_controlMessages.empty() && m.GetState() == GAME_STATE_CLOSE_GAME_MENU;
            m.m_startArg = nullptr;
            g_menuScreens.clear();
            // Single player: no mission, undecided: closed; decided: kept open.
            g_gameRole = GAME_ROLE_LOCAL;
            C_MissionDef *const oldMission = GetGame()->m_mission;
            GetGame()->m_mission = nullptr;
            message(MENU_MSG_ESCAPE);
            ok &= m.GetState() == GAME_STATE_CLOSE_GAME_MENU;
            alignas(16) static uint8_t missionMemory[sizeof(C_MissionDef)];
            alignas(16) static uint8_t objectiveMemory[sizeof(S_MissionObjective)];
            std::memset(missionMemory, 0, sizeof missionMemory);
            std::memset(objectiveMemory, 0, sizeof objectiveMemory);
            auto *mission = reinterpret_cast<C_GmMission *>(missionMemory);
            auto *objective = reinterpret_cast<S_MissionObjective *>(objectiveMemory);
            mission->m_objectives = {objective};
            GetGame()->m_mission = mission;
            objective->status = OBJECTIVE_OPEN;
            message(MENU_MSG_ESCAPE);
            ok &= m.GetState() == GAME_STATE_CLOSE_GAME_MENU;
            objective->status = OBJECTIVE_COMPLETED;
            message(MENU_MSG_ESCAPE);
            ok &= m.GetState() == GAME_STATE_RUNNING;
            mission->m_objectives = std::vector<S_MissionObjective *>();
            GetGame()->m_mission = oldMission;
            g_gameRole = GAME_ROLE_CLIENT;
            WM(r.gmEscape, ok);
        }

        // Escape in a network game: the team changed (posted once, kept open); else the edited soldier
        // (the second of two) dropped, closed.
        {
            bool ok = true;
            // (The first's team asked: the edited one's another.)
            S_SquadSoldier *a = soldier(1), *b = soldier(0);
            squad.states = {a, b};
            m.m_team = 0;
            message(MENU_MSG_ESCAPE);
            ok &= MsLogIs({"command 5 1 0 0"}, g_gfLog) && m.m_teamChangePosted &&
                  m.GetState() == GAME_STATE_RUNNING && squad.states.size() == 2;
            message(MENU_MSG_ESCAPE);
            ok &= g_gfLog.empty() && m.GetState() == GAME_STATE_RUNNING;
            m.m_team = 1;
            message(MENU_MSG_ESCAPE);
            ok &= squad.states == std::vector<S_SquadSoldier *>({a}) && m.GetState() == GAME_STATE_CLOSE_GAME_MENU;
            m.m_team = -1;
            S_SquadSoldier *c = soldier(0), *d = soldier(0);
            squad.states.push_back(c);
            squad.states.push_back(d);
            message(MENU_MSG_ESCAPE);
            ok &= squad.states.size() == 3 && m.GetState() == GAME_STATE_CLOSE_GAME_MENU;
            for (S_SquadSoldier *s : squad.states)
                delete s;
            squad.states.clear();
            message(MENU_MSG_ESCAPE);
            ok &= g_gfLog.empty() && m.GetState() == GAME_STATE_CLOSE_GAME_MENU;
            m.m_teamChangePosted = false;
            WM(r.gmEscapeNet, ok);
        }

        // The edited soldier applied (the first of two dropped; no player slot member: hosting or
        // joining asks for command 0; else after the warmup the spawn) or dropped; then escape.
        {
            bool ok = true;
            S_SquadSoldier *a = soldier(0), *b = soldier(1);
            squad.states = {a, b};
            g_mpSettings->sessionType = 1;
            message(0x200006A);
            ok &= squad.states == std::vector<S_SquadSoldier *>({b}) && MsLogIs({"command 0 0 0 0"}, g_gfLog) &&
                  ScQueue() == "0 200000a 0 0 ; ";
            g_mpSettings->sessionType = 2;
            message(0x200006A);
            ok &= squad.states.size() == 1 && MsLogIs({"command 0 0 0 0"}, g_gfLog);
            g_mpSettings->sessionType = 0;
            message(0x200006A);
            ok &= g_gfLog.empty() && ScQueue() == "0 200000a 0 0 ; ";
            m.m_hasPlayerSlotMember = true;
            g_mpSettings->sessionType = 1;
            message(0x200006A);
            ok &= g_gfLog.empty();
            m.SetRespawnOnApply(true);
            message(0x200006A);
            ok &= MsLogIs({"command 1 0 0 0"}, g_gfLog);
            m.SetRespawnOnApply(false);
            m.m_hasPlayerSlotMember = false;
            g_mpSettings->sessionType = 0;
            S_SquadSoldier *c = soldier(0);
            squad.states.push_back(c);
            message(0x2000077);
            ok &= squad.states == std::vector<S_SquadSoldier *>({b}) && ScQueue() == "0 200000a 0 0 ; ";
            message(0x2000077);
            ok &= squad.states.size() == 1 && ScQueue() == "0 200000a 0 0 ; ";
            // Three: neither drops one.
            S_SquadSoldier *e = soldier(0), *f = soldier(0);
            squad.states = {b, e, f};
            message(0x200006A);
            message(0x2000077);
            ok &= squad.states.size() == 3;
            for (S_SquadSoldier *s : squad.states)
                delete s;
            squad.states.clear();
            WM(r.gmSquad, ok);
        }

        // The questions (yes: their answers); the app's commands; the quit (the game's mode) and the
        // mission's end; others nothing.
        {
            bool ok = true;
            C_MessageBox box(scene, GfFrame(FRAME_DUMMY, "gmqbox"));
            m.m_messageBox = &box;
            message(0x2000078);
            ok &= box.m_kind == MESSAGE_BOX_YES_NO && box.m_yesMessage == 0x20000B8 && box.m_noMessage == 0 &&
                  ScQueue().find("100000 1000015 18 7d9 ; ") != std::string::npos;
            m.CloseMessage();
            message(0x20000B4);
            ok &= box.m_kind == MESSAGE_BOX_YES_NO && box.m_yesMessage == 0x20000B5 && box.m_noMessage == 0 &&
                  ScQueue().find("100000 1000015 18 7d4 ; ") != std::string::npos;
            m.CloseMessage();
            m.m_messageBox = nullptr;
            g_controlMessages.clear();
            C_GameApp *app = GetGameApp();
            const uint32_t commands[] = {0x20000B5, 0x20000B6, 0x20000B7};
            for (int i = 0; i < 3; ++i) {
                app->m_pendingCommand = 0;
                message(commands[i]);
                ok &= m.GetState() == GAME_STATE_CLOSE_GAME_MENU && app->m_pendingCommand == i + 1;
            }
            app->m_pendingCommand = 0;
            alignas(16) static uint8_t gameMemory2[sizeof(C_game)];
            std::memset(gameMemory2, 0, sizeof gameMemory2);
            C_GameState *one = new (gameMemory2) C_GmGameOne();
            m.SetGame(one);
            message(0x20000B8);
            ok &= m.GetState() == GAME_STATE_MENU_END_MISSION && reinterpret_cast<C_game *>(gameMemory2)->GetMode() == GAME_MODE_QUIT;
            reinterpret_cast<C_game *>(gameMemory2)->SetMode(GAME_MODE_NORMAL);
            for (uint32_t id : {0x20000C5u, 0x20000C6u}) {
                message(id);
                ok &= m.GetState() == GAME_STATE_MENU_END_MISSION &&
                      reinterpret_cast<C_game *>(gameMemory2)->GetMode() == GAME_MODE_NORMAL;
            }
            for (uint32_t id : {0x2000079u, 0x20000B3u, 0x20000B9u, 0x20000C3u, 0x20000C8u, 0x200006Bu}) {
                message(id);
                ok &= m.GetState() == GAME_STATE_RUNNING && g_controlMessages.empty() && app->m_pendingCommand == 0 &&
                      g_gfLog.empty();
            }
            m.SetGame(nullptr);
            one->~C_GameState();
            WM(r.gmQuestions, ok);
        }
        delete m.m_options;
        m.m_options = nullptr;
        g_gameProfile.m_missionSquad = oldSquad;
        g_controlMessages.clear();
    }

    // --- the main screen's description (part 9e-ix) ---
    // Its controls as "<name> <type>: <id> <value> <value2>, ...; ".
    // The log's lines starting with `prefix`.
    const auto logHas = [](const char *prefix) {
        return std::count_if(g_gfLog.begin(), g_gfLog.end(),
                             [prefix](const std::string &line) { return line.rfind(prefix, 0) == 0; });
    };
    const auto describe = [](const S_ScreenDesc &desc) {
        std::string s;
        for (const S_ControlDesc *c : desc.controls) {
            s += CmText("%s %x:", c->name.c_str(), c->type);
            for (const S_ControlMessage &m : c->messages)
                s += (m.target ? CmText(" @%x", m.target) : std::string()) +
                     CmText(" %x %x %x", m.id, m.value, static_cast<uint32_t>(m.value2));
            s += "; ";
        }
        return s;
    };
    {
        bool ok = true;
        g_gameRole = GAME_ROLE_LOCAL;
        alignas(16) static uint8_t missionMemory[sizeof(C_MissionDef)];
        alignas(16) static uint8_t objectiveMemory[sizeof(S_MissionObjective)];
        std::memset(missionMemory, 0, sizeof missionMemory);
        std::memset(objectiveMemory, 0, sizeof objectiveMemory);
        auto *mission = reinterpret_cast<C_GmMission *>(missionMemory);
        auto *objective = reinterpret_cast<S_MissionObjective *>(objectiveMemory);
        C_game *game = GetGame();
        C_MissionDef *const oldMission = game->m_mission;
        game->m_mission = mission;
        game->m_hasQuickSave = true;
        game->m_missionFailed = false;
        g_gmCanQuickSave = true;
        g_gmDifficulty = 1;
        C_GameMenu builder;
        const auto main = [&]() {
            S_ScreenDesc desc("MAIN_INGAME_MENU", "main_ingame", 1 << 26);
            return builder.BuildMainScreen(&desc, nullptr) ? describe(desc) : std::string("false");
        };
        const std::string version = "text_h&dversion 13: 1000015 28 7d6; ";
        const std::string rest = "button01 14: 1000015 d 9d3 60010b4 0 0; button02 14: 1000015 d 9d0 60010b6 0 0; "
                                 "button03 14: 1000015 d 9d1 60010b7 0 0; button04 14: 1000015 d 9d2 6001071 0 0; "
                                 "button05 14: 1000015 d 9d4 6001078 0 0; " + version;
        // No objectives: back to the game; not the tutorial: button 6 hidden.
        ok &= main() == "button06 14: 1000015 d 9db 60010c6 0 0 1000004 0 0; button00 14: 1000015 d 9cf 600100a 0 0; " + rest;
        mission->m_unknown2C = true;
        mission->m_objectives = {objective};
        objective->status = OBJECTIVE_COMPLETED;
        ok &= main() == "button06 14: 1000015 d 9db 60010c6 0 0; button00 14: 1000015 d 9e1 60010c6 0 0; " + rest;
        objective->status = OBJECTIVE_OPEN;
        ok &= main().find("button00 14: 1000015 d 9cf 600100a 0 0; ") != std::string::npos;
        // Failed: back to the game, inactive.
        objective->status = OBJECTIVE_COMPLETED;
        game->m_missionFailed = true;
        ok &= main().find("button00 14: 1000015 d 9cf 600100a 0 0 1000001 0 0; ") != std::string::npos;
        game->m_missionFailed = false;
        // No quick save possible; none to load; the difficulty without saves.
        g_gmCanQuickSave = false;
        game->m_hasQuickSave = false;
        std::string s = main();
        ok &= s.find("button02 14: 1000015 d 9d0 60010b6 0 0 1000001 0 0; button03 14: 1000015 d 9d1 60010b7 0 0 1000001 0 0; ") !=
              std::string::npos;
        g_gmCanQuickSave = true;
        game->m_hasQuickSave = true;
        g_gmDifficulty = 4;
        s = main();
        ok &= s.find("button02 14: 1000015 d 9d0 60010b6 0 0; button03 14: 1000015 d 9d1 60010b7 0 0 1000001 0 0; ") !=
              std::string::npos;
        g_gmDifficulty = 3;
        ok &= main().find("button03 14: 1000015 d 9d1 60010b7 0 0; ") != std::string::npos;
        g_gmDifficulty = 1;
        mission->m_unknown2C = false;
        mission->m_objectives = std::vector<S_MissionObjective *>();
        // No mission: the button 6 test reads it (as the original): with one only.
        WM(r.gmMainLocal, ok);

        // A network game's: button 6 hidden, back, two inactive, the options, the player screen, the quit.
        g_gameRole = GAME_ROLE_CLIENT;
        WM(r.gmMainNet, main() == "button06 14: 1000004 0 0; button00 14: 1000015 d 9d5 600100a 0 0; "
                                  "button01 14: 1000015 d 9d6 1000001 0 0; button02 14: 1000015 d 9d7 1000001 0 0; "
                                  "button03 14: 1000015 d 9d8 6001071 0 0; button04 14: 1000015 d 9d9 6001001 3 0; "
                                  "button05 14: 1000015 d 9da 6001078 0 0; " + version);

        // --- the screens built: their models loaded (named), the builders, the screens made (their
        // descriptions' messages handled), the first opened; a model not loaded: no screen ---
        // (menu_run_test.inc's frames made, the cache filling nothing.)
        void *const oldCreate = g_driverVtable[0x54 / 4];
        g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&MrCreateFrame);
        void (*const oldFill)(I3D_model *) = g_cacheFill;
        g_cacheFill = [](I3D_model *) {};
        g_gameRole = GAME_ROLE_LOCAL;
        {
            bool okb = true;
            C_GameMenu m;
            m.m_options = new C_OptionsHandler(&m);
            S_MrWorld world = MrWorld("gmb");
            m.m_scene = As<I3D_scene>(world.scene);
            C_MsScreen box(m.m_scene, "gmbbox", 0);
            m.m_screens = {&box};
            S_MrObj *ma = MrObj("gmma", g_mrModelVt), *mo = MrObj("gmmo", g_mrModelVt);
            g_mrMade[FRAME_MODEL] = {ma, mo};
            g_gfLog.clear();
            g_menuScreens.clear();
            okb &= m.BuildScreens() && MrHas("gmma 34 MAIN_INGAME_MENU") && MrHas("gmmo 34 OPTIONS") &&
                   MrHas("build options OPTIONS 0 gmmo") && logHas("build player") == 0 && logHas("build options") == 1 &&
                   m.m_screens.size() == 3 && m.m_screens[2]->m_inactive && !m.m_screens[1]->m_inactive &&
                   m.m_screens[1]->m_scene == m.m_scene && m.m_screens[2]->m_scene == m.m_scene &&
                   m.m_screens[1]->GetId() == 1u << 26 && m.m_screens[2]->GetId() == 2u << 26 &&
                   m.m_screens[1]->GetScreenFrame() == As<I3D_frame>(ma) && m.m_screens[2]->GetScreenFrame() == As<I3D_frame>(mo) &&
                   m.m_models.size() == 2 && !g_menuScreens.empty() && g_menuScreens.back() == m.m_screens[1];
            // (The main screen's controls made from its description: the version's text among them.)
            okb &= static_cast<C_ControlGroup *>(m.m_screens[1])->m_children.size() <= 8;
            for (size_t i = 1; i < m.m_screens.size(); ++i)
                delete m.m_screens[i];
            g_menuScreens.clear();
            // The options' model not loaded: no options screen.
            C_GameMenu n;
            n.m_options = new C_OptionsHandler(&n);
            n.m_scene = m.m_scene;
            n.m_screens = {&box};
            S_MrObj *na = MrObj("gmna", g_mrModelVt), *no = MrObj("gmno", g_mrModelVt);
            g_mrMade[FRAME_MODEL] = {na, no};
            g_cacheFailName = "options";
            g_gfLog.clear();
            // (Single player in the warmup's menu: the main screen first still.)
            n.m_startArg = reinterpret_cast<void *>(static_cast<intptr_t>(-2));
            okb &= n.BuildScreens() && logHas("build options") == 0 && n.m_screens.size() == 2 &&
                   n.m_screens[1]->GetId() == 1u << 26 && !g_menuScreens.empty() && g_menuScreens.back() == n.m_screens[1];
            n.m_startArg = nullptr;
            g_cacheFailName.clear();
            for (size_t i = 1; i < n.m_screens.size(); ++i)
                delete n.m_screens[i];
            g_menuScreens.clear();
            delete m.m_options;
            delete n.m_options;
            m.m_options = n.m_options = nullptr;
            WM(r.gmBuild, okb);
        }

        // A network game's: the player screen too (co-op: "player_screen_2"); the warmup's menu opens it.
        g_gameRole = GAME_ROLE_CLIENT;
        for (bool coop : {false, true}) {
            bool okb = true;
            const bool oldCoop = g_coop;
            g_coop = coop;
            C_GameMenu m;
            m.m_options = new C_OptionsHandler(&m);
            m.m_startArg = coop ? reinterpret_cast<void *>(static_cast<intptr_t>(-2)) : nullptr;
            S_MrWorld world = MrWorld("gmc");
            m.m_scene = As<I3D_scene>(world.scene);
            C_MsScreen box(m.m_scene, "gmcbox", 0);
            m.m_screens = {&box};
            S_MrObj *a = MrObj("gmca", g_mrModelVt), *o = MrObj("gmco", g_mrModelVt), *p = MrObj("gmcp", g_mrModelVt);
            g_mrMade[FRAME_MODEL] = {a, o, p};
            g_cacheLoads.clear();
            g_gfLog.clear();
            g_menuScreens.clear();
            okb &= m.BuildScreens() && MrHas("gmcp 34 PLAYER_SCREEN") && MrHas("build player PLAYER_SCREEN 0 gmcp") &&
                   MrHas("build options OPTIONS 0 gmco") && logHas("build") == 2 &&
                   m.m_screens.size() == 4 && m.m_screens[3]->GetId() == 3u << 26 && !g_menuScreens.empty() &&
                   g_menuScreens.back() == m.m_screens[coop ? 3 : 1];
            okb &= !g_cacheLoads.empty() && g_cacheLoads.back() == (coop ? "player_screen_2" : "player_screen");
            for (size_t i = 1; i < m.m_screens.size(); ++i)
                delete m.m_screens[i];
            g_menuScreens.clear();
            delete m.m_options;
            m.m_options = nullptr;
            g_coop = oldCoop;
            WM(r.gmBuildNet, okb);
        }
        g_cacheFill = oldFill;
        g_driverVtable[0x54 / 4] = oldCreate;
        g_gameRole = GAME_ROLE_CLIENT;
        game->m_mission = oldMission;
    }

    // --- the base: C_EquipmentMenuBase's slots true ---
    {
        C_GameMenu m;
        C_EquipmentMenuBase &b = m;
        WM(r.gmBase, b.C_EquipmentMenuBase::OnItemNotify13(nullptr, nullptr, 0, 0) &&
                         b.C_EquipmentMenuBase::OnItemHover(nullptr, nullptr, 0, 0));
    }

    g_gameRole = oldRole;
    g_gameProfile.m_player = oldPlayer;
    g_gameProfile.m_campaign = oldCampaign;
    g_lpMissions.clear();
    g_files = oldFiles;
    g_gfLog.clear();
    std::printf("gamemenu: gmNew %d gmNewClient %d gmStart %d gmStartNet %d gmStop %d gmInput %d gmUpdate %d "
                "gmTick %d gmDraw %d gmMessage %d gmObjectives %d gmBase %d gmLoad %d gmCoop %d gmNet %d gmRender %d "
                "gmRenderGame %d gmRenderEquip %d gmOptions %d gmEscape %d gmEscapeNet %d gmSquad %d gmQuestions %d "
                "gmMainLocal %d gmMainNet %d gmBuild %d gmBuildNet %d\n",
                r.gmNew, r.gmNewClient, r.gmStart, r.gmStartNet, r.gmStop, r.gmInput, r.gmUpdate, r.gmTick, r.gmDraw,
                r.gmMessage, r.gmObjectives, r.gmBase, r.gmLoad, r.gmCoop, r.gmNet, r.gmRender, r.gmRenderGame,
                r.gmRenderEquip, r.gmOptions, r.gmEscape, r.gmEscapeNet, r.gmSquad, r.gmQuestions, r.gmMainLocal,
                r.gmMainNet, r.gmBuild, r.gmBuildNet);
}
