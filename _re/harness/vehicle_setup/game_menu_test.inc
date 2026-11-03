// Part 9e-v's in-game menu (game/game_menu.cpp: the class and the menu state's slots), in the factory
// build, run by ControlsTest after LoadoutTest: the slots not ported yet, the info screen, the
// sound's volume, the stocks' reset and the team logging fakes; the game behind a state logging
// its slots. Prints "gamemenu:" with 1 per check met.
#define private public
#define protected public
#include "game/game_menu.h"
#undef protected
#undef private
#include "game/interface.h"
#include "game/team.h"

// (Not ported yet: logged.)
void C_GameMenu::OnMessage(S_ControlMessage *m) { GfLog("gm message %x", m->id); }
bool C_GameMenu::BuildScreens() { return true; }
bool C_GameMenu::OnRender() { return false; }
bool C_GameMenu::OnItemNotify13(void *, C_Control *, uint32_t, uint32_t) { return true; }
bool C_GameMenu::OnItemNotify60(void *, C_Control *, uint32_t, uint32_t) { return true; }
bool C_GameMenu::OnItemNotify0A(void *, C_Control *, uint32_t, uint32_t) { return true; }
bool C_GameMenu::OnItemNotify08(void *, C_Control *, uint32_t, uint32_t) { return true; }
bool C_GameMenu::OnItemNotify09(void *, C_Control *, uint32_t, uint32_t) { return true; }
bool C_GameMenu::OnItemHover(void *, C_Control *, uint32_t, uint32_t) { return true; }
void C_GameMenu::UpdateLoadButton() { GfLog("weight"); }
void C_GameMenu::UpdateCoopPlayers() { GfLog("coop"); }

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

namespace {

struct S_GameMenuResults {
    bool gmNew = true, gmNewClient = true, gmStart = true, gmStartNet = true, gmStop = true, gmInput = true;
    bool gmUpdate = true, gmTick = true, gmDraw = true, gmMessage = true, gmObjectives = true, gmBase = true;
};

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
                        !m->m_unknown1D0 && !m->m_unknown1D1 && m->m_unknown1D4 == -1 && m->m_unknown1D8 == -1 &&
                        m->m_unknown1DC == -1 && !m->m_loadButtonOn && !m->m_stock1E4 && !m->m_stock1E8 &&
                        !m->m_options && !m->m_infoScreen && m->m_unknown4C0 == 0 && m->m_unknown4C4 == -1 &&
                        !m->m_unknown4D8 && !m->m_unknown4D9 && !m->m_hasPlayerSlotMember && !m->GetGame());
        m->SetUnknown1D1(true);
        C_GmGame game;
        m->SetGame(&game);
        WM(r.gmNew, m->m_unknown1D1 && m->GetGame() == &game);
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
                  !m.m_options->m_cursor && m.m_options->m_unknown08 && !m.m_options->m_unknown09 && !m.m_infoScreen;
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
        const std::vector<std::vector<std::string>> want = {
            {"game update 7"}, {"weight", "coop", "game update 7"}, {"weight", "coop"}, {"weight", "coop", "game update 7"}};
        for (int i = 0; i < 4; ++i) {
            g_gameRole = GAME_ROLE_CLIENT;
            C_GameMenu m;
            g_gameRole = roles[i];
            m.SetGame(&game);
            g_gfLog.clear();
            m.OnUpdate(7);
            ok &= MsLogIs(want[static_cast<size_t>(i)], g_gfLog);
            m.SetGame(nullptr);
            g_gfLog.clear();
            m.OnUpdate(7);
            ok &= g_gfLog.size() == (i == 0 ? 0u : 2u);
        }
        // The panel's soldier ticked (the network games), then deleted by the stop.
        g_gameRole = GAME_ROLE_CLIENT;
        C_GameMenu m;
        C_MenuSoldier *soldier = new C_MenuSoldier();
        MsWorld(*soldier);
        m.m_panel.m_soldier = soldier;
        g_gfLog.clear();
        m.OnUpdate(9);
        ok &= std::find(g_gfLog.begin(), g_gfLog.end(), "ss 20 9") != g_gfLog.end() && g_gfLog.size() >= 2 &&
              g_gfLog[g_gfLog.size() - 2] == "weight" && g_gfLog.back() == "coop";
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
                "gmTick %d gmDraw %d gmMessage %d gmObjectives %d gmBase %d\n",
                r.gmNew, r.gmNewClient, r.gmStart, r.gmStartNet, r.gmStop, r.gmInput, r.gmUpdate, r.gmTick, r.gmDraw,
                r.gmMessage, r.gmObjectives, r.gmBase);
}
