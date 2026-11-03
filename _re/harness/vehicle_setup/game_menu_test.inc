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
// (Part 9e-xiii: the player screen's panels: their description's name and control count, their
// model; the soldier types' combo box's id; the callbacks true; the chat line and its team.)
std::string MrFrameName(const void *frame);
bool GmPanel(const char *panel, const S_ScreenDesc *desc, I3D_frame *model)
{
    GfLog("panel %s %s %d %s", panel, desc->name.c_str(), static_cast<int>(desc->controls.size()),
          model ? MrFrameName(model).c_str() : "none");
    return true;
}
// (Part 9e-xiv: the soldiers panel's. The store's count equipment_panel_test.inc's profiles; the
// stats logged; the squad soldier's profile set and logged.)
int C_HumanStore::GetCount() const { return static_cast<int>(g_epProfiles.size()); }
bool S_SquadSoldier::SetProfile(int profile, int level)
{
    GfLog("set profile %d %d", profile, level);
    profileIndex = profile;
    return true;
}
// (Part 9e-xviii: the squad's new soldiers (kept for deletion), the copies and the loads logged.)
std::vector<std::unique_ptr<S_SquadSoldier>> g_gmAdded;
S_SquadSoldier *S_MissionSquad::AddSoldier()
{
    GfLog("add soldier");
    g_gmAdded.push_back(std::make_unique<S_SquadSoldier>());
    g_gmAdded.back()->profileIndex = -1;
    g_gmAdded.back()->team = -1;
    states.push_back(g_gmAdded.back().get());
    return states.back();
}
void S_SquadSoldier::CopyFrom(const S_SquadSoldier &other)
{
    GfLog("copy from %d", other.profileIndex);
    profileIndex = other.profileIndex;
    team = other.team;
}
void S_SquadSoldier::UpdateLoad() { GfLog("update load"); }
// (Part 9e-xix: the soldiers' stats (a profile's from g_gmStats; the call logged), the local
// player's (its rank 5th), the stocks' counts set (per stock), the items loaded (logged).)
std::map<int, std::vector<int>> g_gmStats;
void C_HumanStore::GetStats(int *stats, int index, int level) const
{
    GfLog("get stats %d %d", index, level);
    const auto it = g_gmStats.find(index);
    if (it != g_gmStats.end())
        std::copy(it->second.begin(), it->second.end(), stats);
}
int g_gmLocalStats[16];
int *C_NetGame::GetLocalPlayerStats() { return g_gmLocalStats; }
std::map<const C_ItemStock *, std::map<int, int>> g_gmStockSets;
void C_ItemStock::Set(int type, int count) { g_gmStockSets[this][type] = count; }
void C_ItemsModule::EnsureItemsLoaded() { GfLog("items loaded"); }
// (Part 9e-xvii: the profile's name.)
C_str g_gmProfileName("Pvt");
const C_str &C_PlayerProfile::GetProfileName() const { return g_gmProfileName; }
bool C_NetGame::SendChat(const char *text, bool team)
{
    GfLog("chat [%s] %d", text, team);
    return true;
}

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
const C_ItemStock *g_gmResetStock;                      // (part 9f-ii: the stock reset last)
void C_ItemStock::Reset()
{
    GfLog("stock reset");
    g_gmResetStock = this;
}
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
    bool gmMainLocal = true, gmMainNet = true, gmBuild = true, gmBuildNet = true, gmPlayer = true, gmPlayerCoop = true;
    bool gmPlayerCallbacks = true, gmChat = true, gmSoldiers = true, gmSoldiersNone = true, gmSoldierPicked = true;
    bool gmSide = true, gmEquipment = true, gmEquipmentNone = true, gmItems = true, gmItemsNone = true, gmSkills = true;
    bool gmSoldierTypes = true, gmStorePreset = true, gmTypeEdited = true, gmPresetSaved = true;
    bool gmTeam = true, gmTakeItems = true, gmShown = true, gmStats = true, gmStocks = true, gmClicked = true;
    bool gmDragStart = true, gmDragMove = true, gmDrop = true, gmTypePicked = true, gmPresetItem = true;
    bool gmSubstitute = true, gmMatch = true, gmHover = true, gmChatLine = true;
};
// A control answering CONTROL_MSG_GET_TEXT with its picked line (its value).
struct C_GmPicked : C_Control {
    uint32_t line;
    explicit C_GmPicked(uint32_t l) : C_Control(nullptr, 0x0C500000, nullptr), line(l) {}
    bool HandleMessage(S_ControlMessage *m) override
    {
        if (m->id == CONTROL_MSG_GET_TEXT)
            m->value = line;
        return true;
    }
};
// (Part 9e-xx: the dragged copy the driver makes, logged.)
I3D_frame *g_gmVisual;
I3D_frame *__stdcall GmCreateVisual(void *, uint32_t type)
{
    GfLog("create visual %u", type);
    return g_gmVisual;
}
// A float's bits (a message's value2).
uint32_t GmBits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}
// The log's lines as one ("line; ").
std::string Join(const std::vector<std::string> &log)
{
    std::string s;
    for (const std::string &line : log)
        s += line + "; ";
    return s;
}
// Whether the queue's lines (GmQueue's) hold `lines` in their order.
bool GmInOrder(const std::string &queue, const std::vector<std::string> &lines)
{
    size_t at = 0;
    for (const std::string &line : lines) {
        at = queue.find(line, at);
        if (at == std::string::npos)
            return false;
        at += line.size();
    }
    return true;
}

// A control answering CONTROL_MSG_GET_TEXT with its text.
struct C_GmTexted : C_Control {
    C_str text;
    explicit C_GmTexted(const char *t) : C_Control(nullptr, 0x0C200002, nullptr), text(t) {}
    bool HandleMessage(S_ControlMessage *m) override
    {
        if (m->id == CONTROL_MSG_GET_TEXT)
            m->text = text;
        return true;
    }
};
// The queue as "target id value value2 text; " (emptied).
std::string GmQueue()
{
    std::string s;
    for (const S_ControlMessage &m : g_controlMessages)
        s += CmText("%x %x %x %x %s; ", m.target, m.id, m.value, static_cast<uint32_t>(m.value2), m.text.c_str());
    g_controlMessages.clear();
    return s;
}

// (Part 9e-xxiii.) A popup screen logging the messages sent to it as GmQueue does ("popup ...").
struct C_GmPopup : C_Screen {
    explicit C_GmPopup(I3D_scene *scene) : C_Screen(scene, GfFrame(FRAME_DUMMY, "gmhover")) {}
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("popup %x %x %x %x %s", m->target, m->id, m->value, static_cast<uint32_t>(m->value2), m->text.c_str());
        return true;
    }
};
// A definition with its behaviour, ammunition, rounds; a weapon with its muzzle speed.
struct C_GmHoverDef : C_ItemDef {
    int behaviour, ammo;
    float amount;
    C_GmHoverDef(int b, int a = -1, float n = 0.0f) : C_ItemDef(ITEM_ANIMATED, 0), behaviour(b), ammo(a), amount(n) {}
    int GetCarryMode() override { return 0; }
    int GetBehaviour() override { return behaviour; }
    int GetAmmoItemId() override { return ammo; }
    float GetAmmoAmount() override { return amount; }
};
struct C_GmWeapon : C_WeaponComp {
    float speed;
    C_GmWeapon(E_ItemComponentKind kind, float s) : C_WeaponComp(kind), speed(s) {}
    void Save(int *, void *) override {}
    void Load(int, const void *) override {}
    float GetMuzzleVelocity() override { return speed; }
};

// A control answering CONTROL_MSG_GET_INDEX with its index (logged).
struct C_GmIndexed : C_Control {
    int index;
    C_GmIndexed(uint32_t id, int i) : C_Control(nullptr, id, nullptr), index(i) {}
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("index %x", m->id);
        if (m->id == CONTROL_MSG_GET_INDEX)
            m->value = static_cast<uint32_t>(index);
        return true;
    }
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
    static int WeaponsInfo(const C_EquipmentMenuBase &b) { return b.*&C_GmBasePeek::m_weaponsInfo; }
    static void SetWeaponsInfo(C_EquipmentMenuBase &b, int id) { b.*&C_GmBasePeek::m_weaponsInfo = id; }
    static int &DragType(C_EquipmentMenuBase &b) { return b.*&C_GmBasePeek::m_dragType; }
    static S_SoldierItem &DragItem(C_EquipmentMenuBase &b) { return b.*&C_GmBasePeek::m_dragItem; }
    static I3D_frame *&DragVisual(C_EquipmentMenuBase &b) { return b.*&C_GmBasePeek::m_dragVisual; }
    static bool Fresh(const C_EquipmentMenuBase &b)
    {
        const S_SoldierItem &item = b.*&C_GmBasePeek::m_dragItem;
        return b.*&C_GmBasePeek::m_weaponsInfo == 0 && b.*&C_GmBasePeek::m_dragVisual == nullptr && item.uid == -1 &&
               item.typeId == -1 && item.present == 0 && b.*&C_GmBasePeek::m_dragType == -1;
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
                        m->m_soldierTypes == -1 && !m->m_loadButtonOn && !m->m_stock1E4 && !m->m_stock1E8 &&
                        !m->m_options && !m->m_infoScreen && m->m_mapPresets == 0 && m->m_userPreset == -1 &&
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
        // (As big as a network game's mission: part 9e-xix's stocks read its items.)
        alignas(16) static uint8_t missionMemory[sizeof(C_MpMissionDef)];
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
        // (The options' builder: the models' children looked up (none), the group's frames
        // menu_soldier_test.inc's.)
        void *const oldFind = g_mrModelVt[0x48 / 4], *const oldSetPos = g_msFrameVt[0x08 / 4];
        g_mrModelVt[0x48 / 4] = reinterpret_cast<void *>(&MsFind);
        g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
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
            g_mrMade[FRAME_DUMMY] = {MsFrame("gmgroup")};
            g_gfLog.clear();
            g_menuScreens.clear();
            okb &= m.BuildScreens() && MrHas("gmma 34 MAIN_INGAME_MENU") && MrHas("gmmo 34 OPTIONS") &&
                   MrHas("gmmo 48 bvideo setup 20") && MrHas("gmgroup 34 GROptions") && logHas("build player") == 0 &&
                   m.m_screens.size() == 3 && !m.m_screens[1]->m_inactive && !m.m_screens[2]->m_inactive &&
                   m.m_screens[1]->m_scene == m.m_scene && m.m_screens[2]->m_scene == m.m_scene &&
                   m.m_screens[1]->GetId() == 1u << 26 && m.m_screens[2]->GetId() == 2u << 26 &&
                   m.m_screens[1]->GetScreenFrame() == As<I3D_frame>(ma) && m.m_screens[2]->GetScreenFrame() == As<I3D_frame>(mo) &&
                   m.m_models.size() == 3 && !g_menuScreens.empty() && g_menuScreens.back() == m.m_screens[1];
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
            g_mrMade[FRAME_DUMMY] = {MsFrame("gmngroup")};
            g_cacheFailName = "options";
            g_gfLog.clear();
            // (Single player in the warmup's menu: the main screen first still.)
            n.m_startArg = reinterpret_cast<void *>(static_cast<intptr_t>(-2));
            okb &= n.BuildScreens() && logHas("gmno 48") == 0 && n.m_screens.size() == 2 &&
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
        // (Part 9e-xviii: the player screen shown on its opening: a squad.)
        S_MissionSquad netSquad;
        S_MissionSquad *const oldNetSquad = g_gameProfile.m_missionSquad;
        g_gameProfile.m_missionSquad = &netSquad;
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
            g_mrMade[FRAME_DUMMY] = {MsFrame("gmcgroup")};
            g_cacheLoads.clear();
            g_gfLog.clear();
            g_menuScreens.clear();
            // (Co-op: the player screen's description popup's model.)
            S_MrObj *dm = MrObj("gmcd", g_mrModelVt);
            g_mrMade[FRAME_MODEL] = {a, o, p, dm};
            g_epProfiles.clear();
            okb &= m.BuildScreens() && MrHas("gmcp 34 PLAYER_SCREEN") && MrHas("gmcp 48 equippanel 20") &&
                   MrHas("items loaded") && MrHas("gmcp 48 allitemspanel 20") && MrHas("gmco 48 bvideo setup 20") && m.m_screens[3]->m_inactive == false &&
                   m.m_screens.size() == 4 && m.m_screens[3]->GetId() == 3u << 26 && !g_menuScreens.empty() &&
                   g_menuScreens.back() == m.m_screens[coop ? 3 : 1];
            okb &= g_cacheLoads.size() >= 3 &&
                   g_cacheLoads[coop ? g_cacheLoads.size() - 2 : g_cacheLoads.size() - 1] ==
                       (coop ? "player_screen_2" : "player_screen") &&
                   (!coop || (g_cacheLoads.back() == "description" && m.m_popup &&
                              m.m_popup->GetScreenFrame() == As<I3D_frame>(dm)));
            delete m.m_popup;
            m.m_popup = nullptr;
            for (size_t i = 1; i < m.m_screens.size(); ++i)
                delete m.m_screens[i];
            g_menuScreens.clear();
            delete m.m_options;
            m.m_options = nullptr;
            g_coop = oldCoop;
            WM(r.gmBuildNet, okb);
        }

        // --- the player screen's description (part 9e-xiii): the panels, the screen's callbacks, the
        // soldier types (their id kept, filled), the presets' buttons, the warmup's and the time's texts
        // (a cooperative game: the version's and the equipment's), accept, back, the chat input, the
        // texts, the game info; the weapons' information label (else co-op: the player status's text
        // and the description popup); the panel's soldier in "3Dview" ---
        // (No profiles: no soldiers panel.)
        g_epProfiles.clear();
        const auto playerDescribe = [](const S_ScreenDesc &desc, const C_GameMenu *menu) {
            const std::map<uintptr_t, std::string> functions = {
                {reinterpret_cast<uintptr_t>(&C_GameMenu::OnPlayerScreenShown), "shown"},
                {reinterpret_cast<uintptr_t>(&C_GameMenu::OnPlayerScreenHidden), "hidden"},
                {reinterpret_cast<uintptr_t>(&C_EquipmentMenuBase::CallItemNotify60), "notify60"},
                {reinterpret_cast<uintptr_t>(&C_GameMenu::OnSoldierType), "type"},
                {reinterpret_cast<uintptr_t>(&C_GameMenu::OnSavePreset), "save"},
                {reinterpret_cast<uintptr_t>(&C_GameMenu::OnChat), "chat"},
                {reinterpret_cast<uintptr_t>(&C_GameMenu::OnGameInfo), "info"}};
            const auto message = [&](const S_ControlMessage &m) {
                if (m.id == CONTROL_MSG_ADD_CALLBACK && !m.target) {
                    const auto it = functions.find(m.value3);
                    return CmText(" cb %x %s%s", m.value, it == functions.end() ? "?" : it->second.c_str(),
                                  m.value2 == reinterpret_cast<uintptr_t>(menu) ? "" : " other");
                }
                return (m.target ? CmText(" @%x", m.target) : std::string()) +
                       CmText(" %x %x %x", m.id, m.value, static_cast<uint32_t>(m.value2));
            };
            std::string s;
            for (const S_ControlMessage &m : desc.messages)
                s += message(m);
            s += "; ";
            for (const S_ControlDesc *c : desc.controls) {
                s += CmText("%s %x %x:", c->name.c_str(), c->type, c->id);
                for (const S_ControlMessage &m : c->messages)
                    s += message(m);
                s += "; ";
            }
            return s;
        };
        {
            bool okp = true;
            const bool oldCoop = g_coop;
            const int oldType = g_mpSettings->gameType;
            g_coop = false;
            g_mpSettings->gameType = MP_GAME_OBJECTIVES;
            // The model's frames: the weapons' information label and the soldier's area (the bound
            // 0.25 .. 0.75, -0.6 .. -0.15; menu_run_test.inc's IGraph, no game scene: the soldier made).
            const int igraphSlots[] = {0x18 / 4, 0x1C / 4};
            void *oldIgraph[2] = {g_igraphVtable[igraphSlots[0]], g_igraphVtable[igraphSlots[1]]};
            g_igraphVtable[0x18 / 4] = reinterpret_cast<void *>(&MrScrnX);
            g_igraphVtable[0x1C / 4] = reinterpret_cast<void *>(&MrScrnY);
            I3D_scene *const oldGameScene = GetGame()->m_scene;
            GetGame()->m_scene = nullptr;
            S_MrObj *model = MsFrame("gmpm"), *area = MsFrame("gmp3d");
            auto *bound = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(area) + 0x170);
            bound[0] = 0.25f;
            bound[1] = -0.6f;
            bound[4] = 0.75f;
            bound[5] = -0.15f;
            reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(area) + 0x1D0)[0] = 0x40000;
            g_msChildren[{model, "text_weapons_info"}] = MsFrame("gmpw");
            g_msChildren[{model, "3Dview"}] = area;
            // (The skills panel the first control.)
            const std::string head = " cb 400d000 shown cb 400c000 hidden; Panel02 5 c100000: 1000005 0 0; "
                                     "soldier_type 11 c200000: cb 400e000 notify60 cb 400b000 type 1000081 c 1f4 "
                                     "1000017 13 1f4 1000001 0 0; "
                                     "bsave_preset 14 c300000: 1000015 b 9e3 cb 4001000 save 1000001 0 0; "
                                     "brename_preset 14 c400000: 1000015 b 9e4 @c200002 6001016 0 0 1000001 0 0; ";
            const auto accept = [](int i) {
                return CmText("baccept 14 c%x00000: 1000015 d 9c9 600106a 0 0 1000001 0 0; "
                              "bback 14 c%x00000: 1000015 d 9ca 6001077 0 0; console 21 c%x00000: cb 4015000 chat; "
                              "text03 12 c%x00000:; text_playerarrangement 13 c%x00000: 1000015 a 9f7; ",
                              i, i + 1, i + 2, i + 3, i + 4);
            };
            const std::string texts = "text_chat 13 c%x00000: 1000015 b 9e8; "
                                      "text_selected_soldier_stats 13 c%x00000: 1000015 b 9e9; "
                                      "text_draft 13 c%x00000: 1000015 b 9ea; bgame_info 14 c%x00000: 1000015 d 9cc cb "
                                      "4001000 info; ";
            const std::string version = "text_h&dversion 13 c%x00000: 1000015 28 7d6; "
                                        "text_equipment 13 c%x00000: 1000015 b 9e7; ";
            C_GameMenu m;
            S_MrWorld world = MrWorld("gmp");
            // (Nothing to make: the soldier's Setup stops at once.)
            g_mrMade.clear();
            m.m_scene = As<I3D_scene>(world.scene);
            S_ScreenDesc desc("PLAYER_SCREEN", "player_screen", 3u << 26);
            g_gfLog.clear();
            okp &= m.BuildPlayerScreen(&desc, As<I3D_frame>(model));
            std::string want = head + "text_warm_up 13 c500000: 1000015 2d 9e5 1000004 0 0; "
                               "text_etimated_time 13 c600000: 1000015 2d 9e6; " + accept(7) +
                               CmText(version.c_str(), 0xC, 0xD) + CmText(texts.c_str(), 0xE, 0xF, 0x10, 0x11) +
                               "text_weapons_info 12 d200000: 1000015 b 7d2; ";
            // (The ids of the 16th and 17th: 0xD000000 and 0xD100000.)
            want.replace(want.find("c1000000"), 8, "d000000");
            want.replace(want.find("c1100000"), 8, "d100000");
            okp &= playerDescribe(desc, &m) == want;
            okp &= m.m_soldierTypes == 0x0C200000 && C_GmBasePeek::WeaponsInfo(m) == 0x0D200000 && !m.m_popup &&
                   m.m_panel.m_soldier;
            // (No equipment panel: its frames not found; the dummies not made.)
            okp &= g_gfLog.size() >= 11 && g_gfLog[0] == "gmpm 48 equippanel 20" && g_gfLog[1] == "create 6" &&
                   g_gfLog[2] == "create 6" && g_gfLog[3] == "items loaded" && g_gfLog[4] == "stock reset" &&
                   g_gfLog[5] == "gmpm 48 allitemspanel 20" && g_gfLog[6] == "gmpm 48 soldier type01 1" &&
                   g_gfLog[7] == "gmpm 48 bscroll 20" && g_gfLog[8] == "gmpm 48 text_weapons_info 1" &&
                   g_gfLog[9] == "gmpm 48 3Dview 1" && MrHas("gmp3d on 0");
            // (The soldier types filled: their combo box emptied first.)
            okp &= std::any_of(g_controlMessages.begin(), g_controlMessages.end(), [](const S_ControlMessage &q) {
                return q.target == 0x0C200000 && q.id == CONTROL_MSG_CLEAR;
            });
            g_controlMessages.clear();
            delete m.m_panel.m_soldier;
            m.m_panel.m_soldier = nullptr;
            WM(r.gmPlayer, okp);
            // The occupation games: the warmup's text shown; no label, no area: none.
            g_mpSettings->gameType = MP_GAME_OCCUPATION;
            g_msChildren.erase({model, "text_weapons_info"});
            g_msChildren.erase({model, "3Dview"});
            C_GameMenu n;
            n.m_scene = m.m_scene;
            S_ScreenDesc desc2("PLAYER_SCREEN", "player_screen", 3u << 26);
            okp &= n.BuildPlayerScreen(&desc2, As<I3D_frame>(model));
            want = playerDescribe(desc2, &n);
            okp &= want.find("text_warm_up 13 c500000: 1000015 2d 9e5; text_etimated") != std::string::npos &&
                   want.find("text_weapons_info") == std::string::npos && C_GmBasePeek::WeaponsInfo(n) == 0 && !n.m_panel.m_soldier;
            WM(r.gmPlayer, okp);

            // A cooperative game: the version's and the equipment's texts first, no warmup or time;
            // the player status's text; the description popup on its model (none loaded: none).
            okp = true;
            g_coop = true;
            g_mpSettings->gameType = MP_GAME_COOPERATIVE;
            g_msChildren[{model, "text_weapons_info"}] = MsFrame("gmpw2");
            S_MrObj *popupModel = MrObj("gmpd", g_mrModelVt);
            g_mrMade[FRAME_MODEL] = {popupModel};
            C_GameMenu c;
            c.m_scene = m.m_scene;
            S_ScreenDesc desc3("PLAYER_SCREEN", "player_screen_2", 3u << 26);
            g_cacheLoads.clear();
            g_gfLog.clear();
            okp &= c.BuildPlayerScreen(&desc3, As<I3D_frame>(model));
            want = head + CmText(version.c_str(), 5, 6) + accept(7) + CmText(texts.c_str(), 0xC, 0xD, 0xE, 0xF) +
                   "text_player_status 13 d000000: 1000015 b 4f4c; ";
            okp &= playerDescribe(desc3, &c) == want;
            okp &= C_GmBasePeek::WeaponsInfo(c) == 0 && logHas("gmpm 48 text_weapons_info") == 0 && MrHas("gmpm 48 3Dview 1") &&
                   g_cacheLoads.size() == 1 && g_cacheLoads[0] == "description" && c.m_popup &&
                   typeid(*c.m_popup) == typeid(C_DescriptionScreen) && c.m_popup->GetScreenFrame() == As<I3D_frame>(popupModel) &&
                   c.m_popup->m_scene == c.m_scene && MrHas("gmpd 48 description01 1");
            delete c.m_popup;
            c.m_popup = nullptr;
            C_GameMenu c2;
            c2.m_scene = m.m_scene;
            S_ScreenDesc desc4("PLAYER_SCREEN", "player_screen_2", 3u << 26);
            g_mrMade[FRAME_MODEL] = {MrObj("gmpd2", g_mrModelVt)};
            g_cacheFailName = "description";
            okp &= c2.BuildPlayerScreen(&desc4, As<I3D_frame>(model)) && !c2.m_popup;
            g_cacheFailName.clear();
            WM(r.gmPlayerCoop, okp);
            g_msChildren.erase({model, "text_weapons_info"});

            // --- the soldiers panel (part 9e-xiv): the lists' frames made under "soldierspanel", the
            // template and the selectors moved into them, the tables hidden; the panel's group, its
            // scroll bar, the two lists (the first 30 profiles, the next 30) of the template's copies
            // (their indices, faces, clicks); not co-op: the sides' marks. The template hidden ---
            {
                bool oks = true;
                g_coop = false;
                alignas(16) static uint8_t profiles[61][sizeof(S_SoldierProfile)];
                std::memset(profiles, 0, sizeof profiles);
                const auto setProfiles = [&](int n) {
                    g_epProfiles.clear();
                    for (int i = 0; i < n; ++i) {
                        auto *p = reinterpret_cast<S_SoldierProfile *>(profiles[i]);
                        p->faceImage = reinterpret_cast<IImage *>(static_cast<uintptr_t>(0x1000 + i));
                        g_epProfiles.push_back(p);
                    }
                };
                // A visual copy (its mesh duplicated and updated) for each soldier; a copy for the
                // selector first.
                const auto clones = [](int n) {
                    g_mrClones.clear();
                    g_mrClones.push_back(As<I3D_frame>(MsFrame("gmssel2")));
                    for (int i = 0; i < n; ++i) {
                        S_MrObj *copy = MrObj(CmText("gmsv%d", i).c_str(), g_mrModelVt);
                        S_MrObj *mesh = MrObj("gmsmesh", g_mrModelVt);
                        mesh->vtable[0x18 / 4] = reinterpret_cast<void *>(&MrFake<0x18>);
                        *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(copy) + 0x1E0) = mesh;
                        g_mrClones.push_back(As<I3D_frame>(copy));
                    }
                };
                S_MrObj *sm = MsFrame("gmsm"), *photo = MsFrame("gmsphoto"), *sel = MsFrame("gmssel");
                S_MrObj *t13 = MsFrame("gmst13"), *t15 = MsFrame("gmst15");
                As<I3D_frame>(photo)->m_localMatrix(3) = S_vector(0.25f, -0.5f, 0.75f);
                As<I3D_frame>(photo)->m_localMatrix.m[3][3] = 2.0f;
                g_msChildren[{sm, "soldierspanel"}] = MsFrame("gmsarea");
                g_msChildren[{sm, "photo03"}] = photo;
                g_msChildren[{sm, "selector"}] = sel;
                g_msChildren[{sm, "table13"}] = t13;
                g_msChildren[{sm, "table15"}] = t15;
                // The panel's description: "<name> <type> <id>: messages {children}" (the callbacks
                // by name, the origins by their place).
                std::function<std::string(const S_ControlDesc *, const C_GameMenu *)> panelDescribe;
                panelDescribe = [&panelDescribe](const S_ControlDesc *c, const C_GameMenu *menu) {
                    std::string s = CmText("%s %x %x:", c->name.c_str(), c->type, c->id);
                    for (const S_ControlMessage &m : c->messages) {
                        if (m.id == CONTROL_MSG_ADD_CALLBACK) {
                            const char *f = m.value3 == reinterpret_cast<uintptr_t>(&C_GameMenu::OnSoldierPicked) ? "picked"
                                            : m.value3 == reinterpret_cast<uintptr_t>(&C_GameMenu::OnSide) ? "side"
                                                                                                           : "?";
                            s += CmText(" cb %x %s%s", m.value, f, m.value2 == reinterpret_cast<uintptr_t>(menu) ? "" : " other");
                        } else if (m.id == CONTROL_MSG_SET_ORIGIN) {
                            const auto *o = reinterpret_cast<const float *>(m.value2);
                            s += o ? CmText(" origin %g %g %g %g", o[0], o[1], o[2], o[3]) : std::string(" origin none");
                        } else {
                            s += CmText(" %x %x %x", m.id, m.value, static_cast<uint32_t>(m.value2));
                        }
                        if (m.target)
                            s += CmText(" @%x", m.target);
                    }
                    if (!c->children.empty()) {
                        s += " {";
                        for (const S_ControlDesc *child : c->children)
                            s += " " + panelDescribe(child, menu) + ";";
                        s += " }";
                    }
                    return s;
                };
                const std::string listHead = " 1000004 0 0 100000b c101000 0 1000028 4 0 1000029 0 3d7df3b6 origin 0.25 -0.5 0.75 2";
                const auto picture = [](char list, int i) {
                    return CmText(" Soldier_%c%02d 4 c10%x%02x0: 1000009 %x 0 1000038 0 %x cb 4001000 picked;", list, i,
                                  list == 'a' ? 2 : 3, (list == 'a' ? i : i - 30) + 1, i, 0x1000 + i);
                };
                const auto build = [&](int n, C_GameMenu &menu, S_ScreenDesc &d) {
                    setProfiles(n);
                    clones(n);
                    g_mrMade[FRAME_DUMMY] = {MsFrame("gmsa"), MsFrame("gmsx")};
                    menu.m_scene = m.m_scene;
                    g_gfLog.clear();
                    return menu.BuildSoldiersPanel(&d, As<I3D_frame>(sm));
                };
                C_GameMenu s1;
                S_ScreenDesc d1("PLAYER_SCREEN", "player_screen", 3u << 26);
                C_GameMenu s0;
                S_ScreenDesc d0("PLAYER_SCREEN", "player_screen", 3u << 26);
                oks &= build(1, s0, d0) && d0.controls.size() == 1 && d0.controls[0]->children[1]->children.size() == 1;
                oks &= build(2, s1, d1) && d1.controls.size() == 1;
                std::string want = "soldierspanel 5 c100000: { scroll11 8 c101000: 100000b c102000 0; GB_Soldiers_a 23 c102000:" +
                                   listHead + " 200002c 0 0 {" + picture('a', 0) + picture('a', 1) + " };" +
                                   " GB_Soldiers_x 23 c103000:" + listHead + " 200002c 0 0;" +
                                   " allies 1f c104000: cb 400e000 side; axis 1f c105000: cb 400e000 side; }";
                const std::string got = d1.controls.empty() ? std::string() : panelDescribe(d1.controls[0], &s1);
                if (got != want)
                    std::fprintf(stderr, "gmSoldiers:\n%s\n%s\n", got.c_str(), want.c_str());
                oks &= got == want;
                // The frames: the lists' made under the area, the template and the selectors moved,
                // the tables hidden, the copies named under the lists' frames; the template hidden.
                oks &= MsLogIs({"gmsm 48 soldierspanel 20", "create 6", "gmsa 38 gmsarea 0", "gmsa 34 GB_Soldiers_a",
                                "gmsa 8 (0 0 0)", "create 6", "gmsx 38 gmsarea 0", "gmsx 34 GB_Soldiers_x", "gmsx 8 (0 0 0)",
                                "gmsm 48 photo03 1", "gmsm 48 selector 1", "clone tree gmssel 0", "gmssel2 34 selector2",
                                "gmssel2 38 null 0", "gmsm 48 table13 1", "gmst13 on 0", "gmsm 48 table14 1", "gmsm 48 table15 1",
                                "gmst15 on 0", "gmssel 38 gmsa 0", "gmssel2 38 gmsx 0", "gmsphoto 38 gmsa 1",
                                "gmsphoto on 1", "gmsphoto 24", "clone tree gmsphoto 0", "gmsv0 a0", "gmsmesh 18",
                                "gmsv0 34 Soldier_a00", "gmsv0 38 gmsa 0", "clone tree gmsphoto 0", "gmsv1 a0", "gmsmesh 18",
                                "gmsv1 34 Soldier_a01", "gmsv1 38 gmsa 0", "gmsphoto on 0", "gmsphoto 24"},
                               g_gfLog);
                WM(r.gmSoldiers, oks);
                // 33 profiles: 30 in the first list, 3 in the second; 61: 30 in each; co-op: no marks.
                C_GameMenu s2;
                S_ScreenDesc d2("PLAYER_SCREEN", "player_screen", 3u << 26);
                g_coop = true;
                oks &= build(33, s2, d2) && d2.controls.size() == 1 && d2.controls[0]->children.size() == 3;
                g_coop = false;
                if (d2.controls.size() == 1 && d2.controls[0]->children.size() == 3) {
                    const S_ControlDesc *a = d2.controls[0]->children[1], *x = d2.controls[0]->children[2];
                    oks &= a->children.size() == 30 && x->children.size() == 3 &&
                           " " + panelDescribe(a->children[29], &s2) + ";" == picture('a', 29) &&
                           " " + panelDescribe(x->children[0], &s2) + ";" == picture('x', 30) &&
                           " " + panelDescribe(x->children[2], &s2) + ";" == picture('x', 32) &&
                           MrHas("gmsv32 38 gmsx 0") && MrHas("gmsv29 34 Soldier_a29");
                }
                C_GameMenu s3;
                S_ScreenDesc d3("PLAYER_SCREEN", "player_screen", 3u << 26);
                oks &= build(61, s3, d3) && d3.controls.size() == 1 && d3.controls[0]->children.size() == 5 &&
                       d3.controls[0]->children[1]->children.size() == 30 && d3.controls[0]->children[2]->children.size() == 30 &&
                       d3.controls[0]->children[2]->children[29]->name == C_str("Soldier_x59");
                WM(r.gmSoldiers, oks);
                // No profiles: false, nothing; no template, selector or list frame: false, the frames
                // made (no controls).
                bool okn = true;
                C_GameMenu s4;
                S_ScreenDesc d4("PLAYER_SCREEN", "player_screen", 3u << 26);
                okn &= !build(0, s4, d4) && d4.controls.empty() && g_gfLog.empty();
                for (const char *missing : {"photo03", "selector"}) {
                    S_MrObj *const kept = g_msChildren[{sm, missing}];
                    g_msChildren.erase({sm, missing});
                    C_GameMenu s5;
                    S_ScreenDesc d5("PLAYER_SCREEN", "player_screen", 3u << 26);
                    okn &= !build(2, s5, d5) && d5.controls.empty() && MrHas("gmsx 34 GB_Soldiers_x") &&
                           logHas("gmsphoto") == 0 && logHas("gmssel 38") == 0;
                    g_msChildren[{sm, missing}] = kept;
                }
                // (A list frame not made: AddFrame links it first, as the original: not tested.)
                WM(r.gmSoldiersNone, okn);
                for (S_ScreenDesc *d : {&d0, &d1, &d2, &d3})
                    for (const S_ControlDesc *panel : d->controls)
                        for (const S_ControlDesc *list : panel->children)
                            for (const S_ControlMessage &msg : list->messages)
                                if (msg.id == CONTROL_MSG_SET_ORIGIN)
                                    _aligned_free(reinterpret_cast<void *>(msg.value2));
                g_mrClones.clear();
                g_mrMade.erase(FRAME_DUMMY);
                for (const char *name : {"soldierspanel", "photo03", "selector", "table13", "table15"})
                    g_msChildren.erase({sm, name});
            }

            // --- the equipment panel (part 9e-xv): "equippanel" renamed, the holsters' and backpack's
            // dummies made under it (kept in the models), the places' visuals given meshes of their own,
            // the scroll bars and their texts moved (under a missing bar: unlinked), the items (own
            // meshes) under their groups; the panel's description with the menu. Not found, or a dummy
            // not made: false ---
            {
                bool oke = true;
                S_MrObj *em = MsFrame("gmem"), *panel = MsFrame("gmeq");
                g_msChildren[{em, "equippanel"}] = panel;
                // A visual with a mesh of its own (its copy and update logged).
                const auto visual = [&](const char *name, const char *frame) {
                    S_MrObj *v = MrObj(frame, g_mrModelVt);
                    S_MrObj *mesh = MrObj("gmemesh", g_mrModelVt);
                    mesh->vtable[0x18 / 4] = reinterpret_cast<void *>(&MrFake<0x18>);
                    *reinterpret_cast<S_MrObj **>(reinterpret_cast<uint8_t *>(v) + 0x1E0) = mesh;
                    g_msChildren[{panel, name}] = v;
                };
                // (No "drop", no "item02".)
                for (const char *name : {"back", "head", "hands", "uniform", "shoulder", "item00", "item01", "item03", "item04",
                                         "item05", "item06", "item07"})
                    visual(name, (std::string("gmev_") + name).c_str());
                g_msChildren[{panel, "scroll13"}] = MsFrame("gmes13");
                g_msChildren[{panel, "text_hlstrscap"}] = MsFrame("gmet13");
                g_msChildren[{panel, "hlcpct00"}] = MsFrame("gmeh00");
                // (No "scroll14": its texts unlinked.)
                g_msChildren[{panel, "text_bkpckcap"}] = MsFrame("gmet14");
                g_msChildren[{panel, "hlcpct01"}] = MsFrame("gmeh01");
                C_GameMenu e;
                e.m_scene = m.m_scene;
                S_MrObj *holsters = MsFrame("gmeh"), *backpack = MsFrame("gmeb");
                g_mrMade[FRAME_DUMMY] = {holsters, backpack};
                S_ScreenDesc de("PLAYER_SCREEN", "player_screen", 3u << 26);
                g_gfLog.clear();
                oke &= e.BuildEquipmentPanel(&de, As<I3D_frame>(em));
                std::vector<std::string> want = {"gmem 48 equippanel 20", "create 6", "create 6", "gmeq 34 P_Equipment",
                                                 "gmeh 38 gmeq 0", "gmeh 8 (0 0 0)", "gmeh 34 GB_Holsters", "gmeb 38 gmeq 0",
                                                 "gmeb 8 (0 0 0)", "gmeb 34 GR_BackPack"};
                const auto own = [&](const char *name, const char *parent = nullptr) {
                    want.push_back(CmText("gmeq 48 %s 1", name));
                    if (std::string(name) == "drop" || std::string(name) == "item02")
                        return;
                    want.push_back(CmText("gmev_%s a0", name));
                    want.push_back("gmemesh 18");
                    if (parent)
                        want.push_back(CmText("gmev_%s 38 %s 1", name, parent));
                };
                for (const char *name : {"back", "head", "drop", "hands", "uniform", "shoulder"})
                    own(name);
                for (const char *line : {"gmeq 48 scroll13 1", "gmes13 38 gmeq 1", "gmeq 48 text_hlstrscap 1",
                                         "gmet13 38 gmes13 1", "gmeq 48 hlcpct00 1", "gmeh00 38 gmes13 1"})
                    want.push_back(line);
                for (const char *name : {"item00", "item01", "item02", "item03"})
                    own(name, "gmeh");
                for (const char *line : {"gmeq 48 scroll14 1", "gmeq 48 text_bkpckcap 1", "gmet14 38 null 1",
                                         "gmeq 48 hlcpct01 1", "gmeh01 38 null 1"})
                    want.push_back(line);
                for (const char *name : {"item04", "item05", "item06", "item07"})
                    own(name, "gmeb");
                // (Then the panel's description: the model's frames looked up.)
                oke &= g_gfLog.size() > want.size() &&
                       MsLogIs(want, std::vector<std::string>(g_gfLog.begin(), g_gfLog.begin() + static_cast<long>(want.size())));
                oke &= e.m_models == std::vector<I3D_frame *>({As<I3D_frame>(panel), As<I3D_frame>(holsters), As<I3D_frame>(backpack)}) &&
                       e.m_panel.m_menu == &e && !de.controls.empty() && de.controls[0]->name == C_str("P_Equipment");
                // (The places' callbacks the menu's: it is set first.)
                oke &= !de.controls.empty() && !de.controls[0]->children.empty() &&
                       std::any_of(de.controls[0]->children[0]->messages.begin(), de.controls[0]->children[0]->messages.end(),
                                   [&e](const S_ControlMessage &msg) {
                                       return msg.id == CONTROL_MSG_ADD_CALLBACK && msg.value2 == reinterpret_cast<uintptr_t>(&e);
                                   });
                WM(r.gmEquipment, oke);
                // Not found: false, the dummies made (leaked); a dummy not made: false.
                bool okn = true;
                g_msChildren.erase({em, "equippanel"});
                C_GameMenu e2;
                S_ScreenDesc de2("PLAYER_SCREEN", "player_screen", 3u << 26);
                g_mrMade[FRAME_DUMMY] = {MsFrame("gmeh2"), MsFrame("gmeb2")};
                g_gfLog.clear();
                okn &= !e2.BuildEquipmentPanel(&de2, As<I3D_frame>(em)) && e2.m_models.empty() && de2.controls.empty() &&
                       GfLogIs({"gmem 48 equippanel 20", "create 6", "create 6"}) && !e2.m_panel.m_menu;
                g_msChildren[{em, "equippanel"}] = panel;
                for (int made = 0; made < 2; ++made) {
                    C_GameMenu e3;
                    S_ScreenDesc de3("PLAYER_SCREEN", "player_screen", 3u << 26);
                    g_mrMade[FRAME_DUMMY] = {MsFrame("gmeh3")};
                    if (made == 0)
                        g_mrMade[FRAME_DUMMY].clear();
                    g_gfLog.clear();
                    okn &= !e3.BuildEquipmentPanel(&de3, As<I3D_frame>(em)) && e3.m_models.empty() && de3.controls.empty() &&
                           logHas("gmeq") == 0;
                }
                WM(r.gmEquipmentNone, okn);
                g_mrMade.erase(FRAME_DUMMY);
            }

            // --- the items panel (part 9e-xvi): the stocks set up; "allitemspanel" renamed, the filter
            // (renamed) and the scroll's dummy moved under it, the unused entries hidden, the template
            // (renamed) and its count's text (under it, off, renamed, placed); the store list's
            // description with the menu. A frame missing: false ---
            {
                bool oki = true;
                S_MrObj *im = MsFrame("gmim"), *panel = MsFrame("gmip"), *filter = MsFrame("gmif");
                S_MrObj *entry = MsFrame("gmit"), *count = MsFrame("gmic");
                g_msChildren[{im, "allitemspanel"}] = panel;
                g_msChildren[{im, "soldier type01"}] = filter;
                g_msChildren[{im, "bscroll"}] = MsFrame("gmib");
                g_msChildren[{im, "item08"}] = entry;
                g_msChildren[{im, "item09"}] = count;
                // (Of the unused entries: four.)
                for (const char *name : {"item10", "item15", "selitem08", "selitem15"})
                    g_msChildren[{im, name}] = MsFrame((std::string("gmiu_") + name).c_str());
                std::vector<std::string> want = {"items loaded", "stock reset", "gmim 48 allitemspanel 20", "gmim 48 soldier type01 1",
                                                 "gmim 48 bscroll 20", "gmip 34 P_visualstore", "gmif 34 filter",
                                                 "gmif 38 gmip 1", "gmib 38 gmip 1"};
                for (const char *name : {"item10", "item11", "item12", "item13", "item14", "item15", "selitem08", "selitem09",
                                         "selitem10", "selitem11", "selitem12", "selitem13", "selitem14", "selitem15"}) {
                    want.push_back(CmText("gmim 48 %s 1", name));
                    if (g_msChildren.count({im, name}))
                        want.push_back(CmText("gmiu_%s on 0", name));
                }
                for (const char *line : {"gmim 48 item08 1", "gmim 48 item09 1", "gmit 34 vi_template", "gmic 38 gmit 1",
                                         "gmic on 0", "gmic 34 ammo", "gmic 24"})
                    want.push_back(line);
                // The store list's description (part 9e-xxiv): its six pages (no frames: none built),
                // the highlight.
                for (int page = 0; page < 6; ++page)
                    for (const char *line : {"create 6", "gmim 48 P_visualstore 20", "gmim 48 selitem08 1", "gmim 48 selitem08 1",
                                             "gmim 48 vi_template 1"})
                        want.push_back(line);
                want.push_back("gmim 48 selitem08 1");
                want.push_back("gmiu_selitem08 38 null 1");
                C_GameMenu it;
                S_ScreenDesc di("PLAYER_SCREEN", "player_screen", 3u << 26);
                As<I3D_frame>(count)->m_frameFlags |= 1;
                g_gfLog.clear();
                oki &= it.BuildItemsPanel(&di, As<I3D_frame>(im));
                oki &= MsLogIs(want, g_gfLog) && it.m_storeList.m_menu == &it &&
                       di.controls.size() == 1 && di.controls[0]->name == "P_visualstore";
                WM(r.gmItems, oki);
                // A frame missing: false (the stocks set up first; the later ones not looked up).
                bool okn = true;
                for (const char *missing : {"allitemspanel", "soldier type01", "item08", "item09"}) {
                    S_MrObj *const kept = g_msChildren[{im, missing}];
                    g_msChildren.erase({im, missing});
                    C_GameMenu it2;
                    S_ScreenDesc di2("PLAYER_SCREEN", "player_screen", 3u << 26);
                    g_gfLog.clear();
                    okn &= !it2.BuildItemsPanel(&di2, As<I3D_frame>(im)) && !g_gfLog.empty() && g_gfLog[0] == "items loaded" &&
                           logHas("store desc") == 0 && logHas("gmic 34") == 0 && !it2.m_storeList.m_menu;
                    if (missing == std::string("allitemspanel") || missing == std::string("soldier type01"))
                        okn &= logHas("gmip 34") == 0 && logHas("gmim 48 item10") == 0;
                    g_msChildren[{im, missing}] = kept;
                }
                WM(r.gmItemsNone, okn);
            }

            // --- the skills panel (part 9e-xvi): "Panel02" taking no input; per skill its name's title
            // (no margin, text 0xA78 + the skill), its bar (along z, empty), its value's title ---
            {
                S_ScreenDesc dk("PLAYER_SCREEN", "player_screen", 3u << 26);
                std::string got;
                bool okk = C_GameMenu::BuildSkillsPanel(&dk, nullptr) && dk.controls.size() == 1;
                for (const S_ControlDesc *c : dk.controls) {
                    got += CmText("%s %x %x:", c->name.c_str(), c->type, c->id);
                    for (const S_ControlMessage &m2 : c->messages)
                        got += CmText(" %x %x %x", m2.id, m2.value, static_cast<uint32_t>(m2.value2));
                    for (const S_ControlDesc *k : c->children) {
                        got += CmText("; %s %x %x:", k->name.c_str(), k->type, k->id);
                        for (const S_ControlMessage &m2 : k->messages)
                            got += CmText(" %x %x %x%s", m2.id, m2.value, static_cast<uint32_t>(m2.value2), m2.target ? " @" : "");
                    }
                }
                std::string want = "Panel02 5 c100000: 1000005 0 0";
                const char *const rows[][3] = {{"text_shooting", "hlcpct07", "text_shooting01"},
                                               {"text_strenght", "hlcpct08", "text_strenght01"},
                                               {"text_health", "hlcpct09", "text_health01"},
                                               {"text_endurance", "hlcpct03", "text_endurance01"},
                                               {"text_stealth", "hlcpct04", "text_stealth01"},
                                               {"text_first aid", "hlcpct05", "text_first aid01"},
                                               {"text_lockpick", "hlcpct06", "text_lockpick01"}};
                for (int i = 0; i < 7; ++i) {
                    want += CmText("; %s 13 c1%02x000: 2000073 0 0 1000015 b %x", rows[i][0], 3 * i + 1, 0xA78 + i);
                    want += CmText("; %s 16 c1%02x000: 1000093 2 0 1000092 0 0", rows[i][1], 3 * i + 2);
                    want += CmText("; %s 13 c1%02x000: 2000073 0 0 1000015 b 7d2", rows[i][2], 3 * i + 3);
                }
                if (got != want)
                    std::fprintf(stderr, "gmSkills:\n%s\n%s\n", got.c_str(), want.c_str());
                okk &= got == want;
                WM(r.gmSkills, okk);
            }
            GetGame()->m_scene = oldGameScene;
            g_igraphVtable[igraphSlots[0]] = oldIgraph[0];
            g_igraphVtable[igraphSlots[1]] = oldIgraph[1];
            g_coop = oldCoop;
            g_mpSettings->gameType = oldType;
        }

        g_gameProfile.m_missionSquad = oldNetSquad;
        g_gmAdded.clear();
        g_cacheFill = oldFill;
        g_driverVtable[0x54 / 4] = oldCreate;
        g_mrModelVt[0x48 / 4] = oldFind;
        g_msFrameVt[0x08 / 4] = oldSetPos;
        g_gameRole = GAME_ROLE_CLIENT;
        game->m_mission = oldMission;
    }

    // --- the soldier types (part 9e-xvii): emptied, the header and the first line text 500; the
    // squad team's user presets, then the presets the map lists (counted); their texts, else their
    // names. The user preset stored: the squad soldier's profile and places, its belt and backpack
    // items by type (the last first; 13 and 31 types at most). The header edited: the preset
    // renamed (29 characters), stored, saved; empty: edited again. Saved: stored; a preset named by
    // its text renamed for the profile and its number, the types refilled, the header the name; the
    // presets saved ---
    {
        alignas(16) static uint8_t missionMemory[sizeof(C_MpMissionDef)];
        std::memset(missionMemory, 0, sizeof missionMemory);
        // (Its field by its accessor: the harness's layout.)
        auto &allowed = const_cast<C_str &>(reinterpret_cast<C_MpMissionDef *>(missionMemory)->GetPresets());
        allowed = C_str("Allied gunner, Allied medic, Axis gunner");
        C_game *game = GetGame();
        C_MissionDef *const oldMission = game->m_mission;
        game->m_mission = reinterpret_cast<C_MissionDef *>(missionMemory);
        C_GameMenu m;
        m.m_presets.Clear();
        const auto preset = [](S_LoadoutPreset *p, const char *name, uint32_t flags, int textId) {
            std::strncpy(p->name, name, sizeof p->name - 1);
            p->flags = flags;
            p->textId = textId;
            return p;
        };
        S_LoadoutPreset *u0 = preset(m.m_presets.AddUserPreset(), "Allied_user_preset1", LOADOUT_ALLIED, 0x1988);
        preset(m.m_presets.AddUserPreset(), "Axis_user_preset1", LOADOUT_AXIS, 0x1982);
        S_LoadoutPreset *u2 = preset(m.m_presets.AddUserPreset(), "Mine", LOADOUT_ALLIED, -1);
        preset(m.m_presets.AddUserPreset(), "Deathmatch1", LOADOUT_DEATHMATCH, 0x198E);
        preset(m.m_presets.AddUserPreset(), "Nogame", 0, 0x1999);
        preset(m.m_presets.AddPreset(), "Allied gunner", LOADOUT_ALLIED, -1);
        preset(m.m_presets.AddPreset(), "Allied medic", LOADOUT_ALLIED, 0x2000);
        preset(m.m_presets.AddPreset(), "Allied sniper", LOADOUT_ALLIED, -1);
        preset(m.m_presets.AddPreset(), "Axis gunner", LOADOUT_AXIS, 0x2001);
        m.m_soldierTypes = 0x0C200000;
        m.m_team = TEAM_ALLIES;
        m.m_mapPresets = 9;
        g_controlMessages.clear();
        m.UpdateSoldierTypes();
        const std::string head = "c200000 2000018 0 0 ; c200000 1000081 c 1f4 ; c200000 1000017 13 1f4 ; ";
        bool okt = GmQueue() == head + "c200000 1000017 13 1988 ; c200000 1000016 13 0 Mine; "
                                       "c200000 1000016 13 0 Allied gunner; c200000 1000017 13 2000 ; " &&
                   m.m_mapPresets == 2;
        m.m_team = TEAM_AXIS;
        m.UpdateSoldierTypes();
        okt &= GmQueue() == head + "c200000 1000017 13 1982 ; c200000 1000017 13 2001 ; " && m.m_mapPresets == 1;
        // No team: every game's (the flags 2: not the one of no game).
        m.m_team = -1;
        m.UpdateSoldierTypes();
        const std::string all = GmQueue();
        okt &= m.m_mapPresets == 3 && all.find("13 1999") == std::string::npos && all.find("13 198e") != std::string::npos;
        m.m_team = 2;
        m.UpdateSoldierTypes();
        okt &= GmQueue() == head + "c200000 1000017 13 198e ; " && m.m_mapPresets == 0;
        WM(r.gmSoldierTypes, okt);

        // Stored.
        bool oks = true;
        S_SquadSoldier squad;
        squad.profileIndex = 7;
        squad.head = EpMake(1, 0, 1);
        squad.uniform = EpMake(2, 4, 1);
        squad.backpack = EpMake(3, 5, 1);
        squad.longGun = EpMake(4, 6, 1);
        squad.belt = {EpMake(5, 10, 2), EpMake(6, 11, 1), EpMake(7, 10, 3)};
        squad.backpackContents = {EpMake(8, 20, 4)};
        m.m_panel.m_squadSoldier = &squad;
        m.m_team = TEAM_ALLIES;
        u2->head = u2->hands = 99;
        u2->beltCount = 5;
        u2->backpackCount = 9;
        m.StorePreset(1);
        oks &= u2->unknown1b4 == 7 && u2->head == 0 && u2->uniform == 4 && u2->backpack == 5 && u2->longGun == 6 &&
               u2->hands == -1 && u2->beltCount == 2 && u2->belt[0].typeId == 10 && u2->belt[0].count == 5 &&
               u2->belt[1].typeId == 11 && u2->belt[1].count == 1 && u2->backpackCount == 1 &&
               u2->backpackItems[0].typeId == 20 && u2->backpackItems[0].count == 4;
        // (Full: 13 belt types, 31 backpack types; the last first.)
        squad.belt.clear();
        squad.backpackContents.clear();
        for (int i = 0; i < 15; ++i)
            squad.belt.push_back(EpMake(i, 100 + i, 1));
        for (int i = 0; i < 33; ++i)
            squad.backpackContents.push_back(EpMake(i, 200 + i, 2));
        squad.hands = EpMake(9, 8, 1);
        m.StorePreset(1);
        oks &= u2->beltCount == 13 && u2->belt[0].typeId == 114 && u2->belt[12].typeId == 102 && u2->hands == 8 &&
               u2->backpackCount == 31 && u2->backpackItems[0].typeId == 232 && u2->backpackItems[30].typeId == 202 &&
               u2->backpackItems[30].count == 2;
        // (Another team's index: none; the axis': its first.)
        const S_LoadoutPreset kept = *u0;
        m.StorePreset(2);
        oks &= std::memcmp(&kept, u0, sizeof kept) == 0 && u0->unknown1b4 == 0;
        S_LoadoutPreset *axis = m.m_presets.GetUserPreset(0, LOADOUT_AXIS);
        m.m_team = TEAM_AXIS;
        m.StorePreset(0);
        m.m_team = TEAM_ALLIES;
        oks &= axis && axis->unknown1b4 == 7 && axis->hands == 8 && std::memcmp(&kept, u0, sizeof kept) == 0;
        WM(r.gmStorePreset, oks);
        squad.belt.clear();
        squad.backpackContents.clear();
        squad.hands = EpMake(-1, -1, 0);

        // The header edited.
        bool oke = true;
        g_files.erase("prof\\user_presets.def");
        const auto edited = [&](const char *text) {
            C_GmTexted control(text);
            g_controlMessages.clear();
            return C_GameMenu::OnSoldierType(&m, &m, &control, CONTROL_NOTIFY_0B, 0);
        };
        oke &= edited("") && GmQueue() == "c200002 2000016 0 0 ; " && !g_files.count("prof\\user_presets.def");
        m.m_userPreset = -1;
        oke &= edited("Scout") && GmQueue().empty() && !g_files.count("prof\\user_presets.def");
        m.m_userPreset = 0;
        u0->unknown1b4 = 0;
        std::memset(u0->name, 'X', sizeof u0->name - 1);
        oke &= edited("A very long preset name of forty chars") && GmQueue().empty() &&
               std::string(u0->name) == "A very long preset name of fo" && u0->textId == -1 && u0->unknown1b4 == 7 &&
               g_files.count("prof\\user_presets.def");
        // (Saved after it is stored: the file's first preset has the profile.)
        const std::vector<uint8_t> &saved = g_files["prof\\user_presets.def"];
        oke &= saved.size() >= 8 + sizeof(S_LoadoutPreset) &&
               reinterpret_cast<const S_LoadoutPreset *>(saved.data() + 8)->unknown1b4 == 7;
        g_files.erase("prof\\user_presets.def");
        m.m_userPreset = 5;
        oke &= edited("Scout") && !g_files.count("prof\\user_presets.def");
        WM(r.gmTypeEdited, oke);

        // Saved.
        bool okv = true;
        m.m_userPreset = -1;
        okv &= C_GameMenu::OnSavePreset(&m, &m, nullptr, CONTROL_NOTIFY_CLICK, 0) && GmQueue().empty() &&
               g_files.count("prof\\user_presets.def");
        g_files.erase("prof\\user_presets.def");
        u0->textId = 0x1988;
        std::memset(u0->name, 'X', sizeof u0->name - 1);
        u0->unknown1b4 = 0;
        m.m_userPreset = 0;
        okv &= C_GameMenu::OnSavePreset(&m, &m, nullptr, CONTROL_NOTIFY_CLICK, 0) && std::string(u0->name) == "Pvt1" &&
               u0->name[29] == 0 && u0->textId == -1 && u0->unknown1b4 == 7 && g_files.count("prof\\user_presets.def") &&
               GmQueue() == head + "c200000 1000016 13 0 Pvt1; c200000 1000016 13 0 Mine; "
                                   "c200000 1000016 13 0 Allied gunner; c200000 1000017 13 2000 ; "
                                   "c200000 100007a c 0 Pvt1; ";
        // Named already: stored, not renamed; the second (index 1 of the team's).
        m.m_userPreset = 1;
        u2->unknown1b4 = 0;
        okv &= C_GameMenu::OnSavePreset(&m, &m, nullptr, CONTROL_NOTIFY_CLICK, 0) && std::string(u2->name) == "Mine" &&
               u2->unknown1b4 == 7 && GmQueue().empty();
        // (Its number in the name: the preset's index + 1.)
        u2->textId = 0x1989;
        okv &= C_GameMenu::OnSavePreset(&m, &m, nullptr, CONTROL_NOTIFY_CLICK, 0) && std::string(u2->name) == "Pvt2";
        g_controlMessages.clear();
        WM(r.gmPresetSaved, okv);
        m.m_panel.m_squadSoldier = nullptr;
        g_files.erase("prof\\user_presets.def");
        game->m_mission = oldMission;
        allowed = C_str();
    }

    // --- the item slots (part 9e-xx): clicked twice (from the panel: back to the store, the backpack
    // with its contents; from the store: taken, dropped onto the soldier, what it displaced (or the
    // item) back; a large weapon not with another; not in the store: false); dragged: its start (a
    // store's item the store has; one of its type kept, the places shown, the control's visual's
    // copy over it), its move (by the cursor's), its drop (the copy released, the places hidden, the
    // item dropped on the control under the cursor); the drop (from the panel: back, else moved;
    // from the store onto the panel: put, taken; what it displaced back) ---
    {
        C_GameMenu m;
        S_SquadSoldier sq;
        sq.backpackContents = {EpMake(1, 30, 1)};
        sq.backpack = EpMake(2, 31, 1);
        m.m_panel.m_squadSoldier = &sq;
        for (auto &h : m.m_panel.m_highlights)
            h = As<I3D_frame>(MsFrame("gmdh"));
        I3D_frame *const firstHighlight = m.m_panel.m_highlights[0];
        C_ItemStock stock;
        m.m_storeList.SetStock(&stock);
        g_slCounts.clear();
        g_epRemoved.clear();
        g_controlMessages.clear();
        const uint32_t head = 0x0C201000, backpack = 0x0C203000, uniform = 0x0C202000, store = 0x0C301000;
        const auto clicked = [&](uint32_t id, int type) {
            C_GmIndexed control(id, 0);
            control.m_index = type;
            g_gfLog.clear();
            return m.OnItemNotify13(&m, &control, CONTROL_NOTIFY_13, 0);
        };
        bool okc = clicked(head, -1) && g_gfLog.empty();
        // (The store's entry of a returned type updated: its count posted.)
        m.m_storeList.m_entries.resize(500);
        m.m_storeList.m_entries[31] = 0x0C3F1000;
        C_SlDef d31(ITEM_STATIC, 0, -1, -1);
        const std::map<int, C_ItemDef *> defs31 = g_gfItemDefs;
        g_gfItemDefs[31] = &d31;
        g_controlMessages.clear();
        g_epRemoved = {1};
        okc &= clicked(head, 5) && GmInOrder(Join(g_gfLog), {"remove 1 -1 5 1", "stock add 5 1"});
        okc &= clicked(head, 5) && logHas("stock add") == 0;
        okc &= clicked(backpack, 31) && GmInOrder(Join(g_gfLog), {"remove 40 1 30 1", "remove 8 2 31 1"}) && logHas("remove 1 ") == 0 &&
               GmQueue().find("c3f1000") != std::string::npos;
        g_gfItemDefs = defs31;
        // (Type 0 an item too; another screen's control, not the panel's: the store's way.)
        g_epRemoved = {1};
        okc &= clicked(head, 0) && MrHas("remove 1 -1 0 1");
        okc &= !clicked(0x0E201000, 7) && GfLogIs({"stock take 7 1"});
        // From the store: not there, false; there: taken, dropped (no item type: given back).
        okc &= !clicked(store, 7) && GfLogIs({"stock take 7 1"});
        g_slCounts[7] = 1;
        okc &= clicked(store, 7) && GmInOrder(Join(g_gfLog), {"stock take 7 1", "stock add 7 1"});
        // (A type the soldier takes: on the head when free, not forced; what it displaced back; its
        // own type: the item back.)
        C_SlDef d7(ITEM_STATIC, 0, -1, -1);
        const std::map<int, C_ItemDef *> keptDefs = g_gfItemDefs;
        g_gfItemDefs = {{7, &d7}};
        g_epAllowed[&d7] = 1;
        sq.head = EpMake(3, 3, 1);
        g_slCounts[7] = 1;
        okc &= clicked(store, 7) && logHas("put") == 0 && MrHas("stock add 7 1");
        sq.head = EpMake(-1, -1, 0);
        g_slCounts[7] = 1;
        okc &= clicked(store, 7) && GmInOrder(Join(g_gfLog), {"stock take 7 1", "put 1 -1 7 1"}) && logHas("stock add") == 0;
        g_slCounts[7] = 1;
        g_epDisplaced = {EpMake(9, 12, 2)};
        okc &= clicked(store, 7) && GmInOrder(Join(g_gfLog), {"stock take 7 1", "put 1 -1 7 1", "stock add 12 2"});
        g_slCounts[7] = 1;
        g_epDisplaced = {EpMake(9, 7, 4)};
        okc &= clicked(store, 7) && GmInOrder(Join(g_gfLog), {"put 1 -1 7 1", "stock add 7 1"}) && logHas("stock add 7 4") == 0;
        g_epDisplaced.clear();
        g_epAllowed.erase(&d7);
        g_gfItemDefs = keptDefs;
        // A large weapon with another: nothing.
        C_SlDef large(ITEM_ANIMATED, 0, -1, -1);
        large.m_inventorySize = 2;
        const std::map<int, C_ItemDef *> oldDefs = g_gfItemDefs;
        g_gfItemDefs = {{8, &large}};
        sq.hands = EpMake(3, 8, 1);
        g_slCounts[8] = 1;
        okc &= clicked(store, 8) && g_gfLog.empty();
        // (Not large: taken, the large one there or not.)
        g_slCounts[9] = 1;
        okc &= clicked(store, 9) && MrHas("stock take 9 1");
        sq.hands = EpMake(-1, -1, 0);
        WM(r.gmClicked, okc);

        // The drag's start.
        void **const copyVt = new void *[MR_SLOTS];
        std::copy(g_msFrameVt, g_msFrameVt + MR_SLOTS, copyVt);
        copyVt[0x4C / 4] = reinterpret_cast<void *>(&MrFake<0x4C, void *>);
        copyVt[0x04 / 4] = reinterpret_cast<void *>(&MrFake<0x04>);
        copyVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
        S_MrObj *source = MsFrame("gmds"), *copy = MsFrame("gmdc");
        source->vtable = copyVt;
        copy->vtable = copyVt;
        As<I3D_frame>(source)->m_worldMatrix.m[3][0] = 0.25f;
        As<I3D_frame>(source)->m_worldMatrix.m[3][1] = -0.5f;
        As<I3D_frame>(source)->m_worldMatrix.m[3][2] = 0.75f;
        S_MrWorld world = MrWorld("gmd");
        m.m_scene = As<I3D_scene>(world.scene);
        void *const oldVisual = g_driverVtable[0x50 / 4];
        g_driverVtable[0x50 / 4] = reinterpret_cast<void *>(&GmCreateVisual);
        g_gmVisual = As<I3D_frame>(copy);
        const auto start = [&](uint32_t id, int type) {
            C_GmIndexed control(id, type);
            control.m_frame = As<I3D_frame>(source);
            g_gfLog.clear();
            return m.OnItemNotify0A(&m, &control, CONTROL_NOTIFY_0A, 0);
        };
        bool oks = start(store, -1) && C_GmBasePeek::DragType(m) == -1 && GfLogIs({"index 100000a"});
        g_slCounts[7] = 0;
        oks &= start(store, 7) && C_GmBasePeek::DragType(m) == -1 && !C_GmBasePeek::DragVisual(m);
        g_slCounts[7] = 2;
        oks &= start(store, 7) && C_GmBasePeek::DragType(m) == 7 && C_GmBasePeek::DragItem(m).typeId == 7 && C_GmBasePeek::DragItem(m).present == 1 &&
               C_GmBasePeek::DragVisual(m) == As<I3D_frame>(copy);
        oks &= GmInOrder(Join(g_gfLog), {"index 100000a", "gmdh on 0", "gmds 4", "create visual 0", "gmdc 4c gmds",
                                         "gmdc 38 sector 0", "gmdc c (0 0 1) 0", "gmdc 8 (0.25 -0.5 0.85)", "gmdc on 1",
                                         "gmdc 24"});
        // (From the panel: no store check, its places shown ignoring the weight.)
        C_GmBasePeek::DragType(m) = -1;
        g_slCounts.clear();
        oks &= start(head, 9) && C_GmBasePeek::DragType(m) == 9;
        // (A large weapon from the store with another: nothing.)
        C_GmBasePeek::DragType(m) = -1;
        sq.longGun = EpMake(3, 8, 1);
        g_slCounts[8] = 1;
        oks &= start(store, 8) && C_GmBasePeek::DragType(m) == -1;
        g_slCounts[9] = 1;
        oks &= start(store, 9) && C_GmBasePeek::DragType(m) == 9;
        C_GmBasePeek::DragType(m) = -1;
        sq.longGun = EpMake(-1, -1, 0);
        // (Type 0 dragged too.)
        oks &= start(head, 0) && C_GmBasePeek::DragType(m) == 0;
        C_GmBasePeek::DragType(m) = -1;
        // (The places shown: from the panel the weight ignored, from the store not.)
        C_SlDef heavy(ITEM_STATIC, 0, -1, -1);
        heavy.m_weight = 50.0f;
        g_gfItemDefs = {{10, &heavy}, {8, &large}};
        g_epAllowed[&heavy] = 1;
        g_epFreeWeight = 10.0f;
        oks &= start(head, 10) && firstHighlight->IsOn();
        g_slCounts[10] = 1;
        oks &= start(store, 10) && !firstHighlight->IsOn();
        g_epFreeWeight = 100.0f;
        C_GmBasePeek::DragType(m) = -1;
        // (The large weapon's type no more: the store list's update reads only its entries' types.)
        g_gfItemDefs = oldDefs;
        // (No visual made: no copy.)
        g_gmVisual = nullptr;
        oks &= start(head, 9) && !C_GmBasePeek::DragVisual(m) && logHas("gmdc") == 0;
        g_gmVisual = As<I3D_frame>(copy);
        WM(r.gmDragStart, oks);

        // Its move: the copy by the cursor's move (its depth the move's).
        bool okm = true;
        m.m_cursor = C_Cursor::Acquire(m.m_scene);
        C_GmBasePeek::DragVisual(m) = As<I3D_frame>(copy);
        As<I3D_frame>(copy)->m_worldMatrix.m[3][0] = 0.5f;
        As<I3D_frame>(copy)->m_worldMatrix.m[3][1] = 0.25f;
        As<I3D_frame>(copy)->m_worldMatrix.m[3][2] = 0.5f;
        g_csCursor.move = S_vector(0.125f, -0.25f, -1.0f);
        C_GmBasePeek::DragType(m) = 9;
        g_gfLog.clear();
        okm &= m.OnItemNotify08(&m, nullptr, CONTROL_NOTIFY_08, 0) && GfLogIs({"gmdc 8 (0.625 0 -1)", "gmdc 24"});
        C_GmBasePeek::DragType(m) = -1;
        g_gfLog.clear();
        okm &= m.OnItemNotify08(&m, nullptr, CONTROL_NOTIFY_08, 0) && g_gfLog.empty();
        g_csCursor.move = S_vector(0.0f, 0.0f, 0.0f);
        WM(r.gmDragMove, okm);

        // Its drop: the copy released, the places hidden, dropped on what is under the cursor (here
        // nothing: from the store, nothing more); the drag ended.
        bool okd = true;
        C_MsScreen top(m.m_scene, "gmdtop", 0x0C000000);
        g_menuScreens = {&top};
        {
            // (A light type the head takes: its drop hides the places.)
            C_SlDef light(ITEM_STATIC, 0, -1, -1);
            g_gfItemDefs = {{11, &light}};
            g_epAllowed[&light] = 1;
            C_GmBasePeek::DragType(m) = 11;
            C_GmBasePeek::DragItem(m) = EpMake(-1, 11, 1);
            firstHighlight->SetOn(true);
            C_GmIndexed control(store, 11);
            okd &= m.OnItemNotify09(&m, &control, CONTROL_NOTIFY_09, 0) && !firstHighlight->IsOn();
            // (Under the cursor the panel's uniform: put there, taken.)
            S_MrObj *hit = MsFrame("gmdhit");
            As<I3D_frame>(hit)->SetData(reinterpret_cast<S_FrameData *>(static_cast<uintptr_t>(uniform)));
            g_mrRet[{world.scene, 0x104}] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hit));
            g_epAllowed[&light] = 2;
            g_slCounts[11] = 1;
            C_GmBasePeek::DragType(m) = 11;
            C_GmBasePeek::DragItem(m) = EpMake(-1, 11, 1);
            g_gfLog.clear();
            okd &= m.OnItemNotify09(&m, &control, CONTROL_NOTIFY_09, 0) &&
                   GmInOrder(Join(g_gfLog), {"put 2 -1 11 1", "stock take 11 1"});
            g_mrRet.erase({world.scene, 0x104});
            g_epAllowed.erase(&light);
            g_gfItemDefs = oldDefs;
            g_slCounts.clear();
        }
        C_GmBasePeek::DragType(m) = 7;
        C_GmBasePeek::DragItem(m) = EpMake(4, 7, 1);
        C_GmBasePeek::DragVisual(m) = As<I3D_frame>(copy);
        {
            C_GmIndexed control(store, 7);
            g_gfLog.clear();
            okd &= m.OnItemNotify09(&m, &control, CONTROL_NOTIFY_09, 0) && !C_GmBasePeek::DragVisual(m) && C_GmBasePeek::DragType(m) == -1 &&
                   C_GmBasePeek::DragItem(m).typeId == -1 && C_GmBasePeek::DragItem(m).present == 0 && C_GmBasePeek::DragItem(m).uid == 4 &&
                   GmInOrder(Join(g_gfLog), {"gmdc 0", "gmdh on 0"}) && logHas("stock") == 0;
            // No drag: the copy released only.
            C_GmBasePeek::DragVisual(m) = As<I3D_frame>(copy);
            g_gfLog.clear();
            okd &= m.OnItemNotify09(&m, &control, CONTROL_NOTIFY_09, 0) && GfLogIs({"gmdc 0"}) && !C_GmBasePeek::DragVisual(m);
        }
        // The drop: from the panel to nowhere (its count back), the backpack, onto another place;
        // from the store onto a place (taken; what it displaced back unless of the type), to nowhere.
        const auto drop = [&](uint32_t from, uint32_t target, int type) {
            C_GmIndexed control(from, type);
            C_GmBasePeek::DragItem(m) = EpMake(-1, type, 1);
            g_gfLog.clear();
            return m.DropItem(&control, target);
        };
        g_epCount = 3;
        g_epRemoved = {3};
        okd &= drop(head, 0, 5) && GmInOrder(Join(g_gfLog), {"remove 1 -1 5 3", "stock add 5 3"});
        g_epRemoved = {3};
        okd &= drop(head, store, 5) && MrHas("stock add 5 3");
        g_controlMessages.clear();
        g_gfItemDefs[31] = &d31;
        okd &= drop(backpack, 0, 31) && MrHas("remove 8 2 31 1") && logHas("remove 1 ") == 0 &&
               GmQueue().find("c3f1000") != std::string::npos;
        g_gfItemDefs.erase(31);
        g_epCount = 1;
        g_epRemoved = {1};
        okd &= drop(head, 0, 5) && MrHas("stock add 5 1");
        g_epRemoved = {0};
        okd &= drop(head, 0, 5) && logHas("stock add") == 0;
        g_epRemoved = {1};
        // (Types the places take.)
        // (The store's entries sized for the types, as its pages make them.)
        m.m_storeList.m_entries.resize(500);
        C_SlDef d5(ITEM_STATIC, 0, -1, -1), d6(ITEM_STATIC, 0, -1, -1);
        g_gfItemDefs = {{5, &d5}, {6, &d6}};
        g_epAllowed[&d5] = g_epAllowed[&d6] = 3;
        okd &= drop(head, uniform, 5) && GmInOrder(Join(g_gfLog), {"remove 1 -1 5 1", "put 2 -1 5 1"}) && logHas("stock") == 0;
        g_slCounts = {{6, 1}};
        okd &= drop(store, uniform, 6) && GmInOrder(Join(g_gfLog), {"put 2 -1 6 1", "stock take 6 1"}) && logHas("stock add") == 0;
        // (What it displaced: of another type back to the store; of its type: nothing taken.)
        // (None in the stock: taken anyway, not filled.)
        g_slCounts = {{6, 0}};
        g_epDisplaced = {EpMake(9, 12, 1)};
        okd &= drop(store, uniform, 6) && GmInOrder(Join(g_gfLog), {"put 2 -1 6 1", "stock take 6 1", "stock add 12 1"}) &&
               logHas("stock add 6") == 0;
        g_epDisplaced = {EpMake(9, 6, 1)};
        okd &= drop(store, uniform, 6) && MrHas("put 2 -1 6 1") && logHas("stock") == 0;
        g_epDisplaced.clear();
        g_epAllowed.erase(&d5);
        g_epAllowed.erase(&d6);
        g_gfItemDefs = oldDefs;
        okd &= drop(store, 0, 6) && g_gfLog.empty();
        okd &= drop(store, store, 6) && g_gfLog.empty();
        WM(r.gmDrop, okd);
        g_menuScreens.clear();
        m.m_cursor->Release();
        m.m_cursor = nullptr;
        g_driverVtable[0x50 / 4] = oldVisual;
        g_gmVisual = nullptr;
        g_gfItemDefs = oldDefs;
        g_slCounts.clear();
        g_epRemoved.clear();
        g_epCount = 0;
        g_controlMessages.clear();
        m.m_panel.m_squadSoldier = nullptr;
        for (auto &h : m.m_panel.m_highlights)
            h = nullptr;
        m.m_storeList.m_stock = nullptr;
        C_GmBasePeek::DragVisual(m) = nullptr;
    }

    // --- the soldier type picked (part 9e-xxi): the header: the items back, the buttons off, false;
    // a user preset (the edited one): its profile and pick (not empty), the stats, the buttons on;
    // the map's preset of the line: the buttons off; its match's items put on (the old back first,
    // unless an empty user preset; a large long gun not with another); the panel and the store
    // refreshed. A preset's item: taken, put (not forced), what was left back ---
    {
        alignas(16) static uint8_t pickMission[sizeof(C_MpMissionDef)];
        std::memset(pickMission, 0, sizeof pickMission);
        auto &allowed = const_cast<C_str &>(reinterpret_cast<C_MpMissionDef *>(pickMission)->GetPresets());
        allowed = C_str("Allied gunner, Allied medic");
        C_game *game = GetGame();
        C_MissionDef *const oldMission = game->m_mission;
        game->m_mission = reinterpret_cast<C_MissionDef *>(pickMission);
        C_GameMenu m;
        m.m_presets.Clear();
        S_LoadoutPreset *u0 = m.m_presets.AddUserPreset();
        std::strcpy(u0->name, "Mine");
        u0->flags = LOADOUT_ALLIED;
        u0->unknown1b4 = 33;
        u0->head = 3;
        u0->longGun = 8;
        u0->beltCount = 2;
        u0->belt[0].typeId = 10;
        u0->belt[0].count = 2;
        u0->belt[1].typeId = 11;
        u0->belt[1].count = 1;
        u0->backpackCount = 1;
        u0->backpackItems[0].typeId = 12;
        u0->backpackItems[0].count = 3;
        // (Past the counts: not read.)
        u0->belt[2].typeId = 13;
        u0->belt[2].count = 1;
        u0->backpackItems[1].typeId = 14;
        u0->backpackItems[1].count = 1;
        S_LoadoutPreset *u1 = m.m_presets.AddUserPreset();
        std::strcpy(u1->name, "Empty");
        u1->flags = LOADOUT_ALLIED;
        u1->unknown1b4 = 4;
        S_LoadoutPreset *p0 = m.m_presets.AddPreset();
        std::strcpy(p0->name, "Allied medic");
        p0->flags = LOADOUT_ALLIED;
        p0->hands = 5;
        p0->uniform = 6;
        p0->backpack = 7;
        S_LoadoutPreset *p1 = m.m_presets.AddPreset();
        std::strcpy(p1->name, "Allied sniper");
        p1->flags = LOADOUT_ALLIED;
        m.m_team = TEAM_ALLIES;
        S_SquadSoldier sq;
        sq.profileIndex = 2;
        m.m_panel.m_squadSoldier = &sq;
        for (auto &h : m.m_panel.m_highlights)
            h = As<I3D_frame>(MsFrame("gmph"));
        C_ItemStock stock;
        m.m_storeList.SetStock(&stock);
        m.m_storeList.m_entries.resize(500);
        g_slCounts = {{3, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 1}, {10, 2}, {11, 1}, {12, 3}, {13, 1}, {14, 1}};
        // (The types every place takes: the puts show the places; the store's entry of 3 updated.)
        C_SlDef any(ITEM_STATIC, 0, -1, -1);
        const std::map<int, C_ItemDef *> pickDefs = g_gfItemDefs;
        g_gfItemDefs = {{3, &any}, {5, &any}, {6, &any}, {7, &any}, {8, &any}, {10, &any}, {11, &any}, {12, &any},
                        {13, &any}, {14, &any}};
        g_epAllowed[&any] = 0x7F;
        m.m_storeList.m_entries[3] = 0x0C3F3000;
        // (A soldier: its face set.)
        auto *pickSoldier = new C_MenuSoldier();
        MsWorld(*pickSoldier);
        alignas(16) static uint8_t pickFaces[61][sizeof(S_SoldierProfile)];
        std::memset(pickFaces, 0, sizeof pickFaces);
        g_epProfiles.clear();
        for (auto &face : pickFaces)
            g_epProfiles.push_back(reinterpret_cast<S_SoldierProfile *>(face));
        g_epProfiles[33]->faceTexture = "gm_p33.bmp";
        const auto pick = [&](uint32_t value) {
            C_GmPicked control(value);
            g_gfLog.clear();
            g_controlMessages.clear();
            return m.OnItemNotify60(&m, &control, CONTROL_NOTIFY_0E, 0);
        };
        bool okp = m.OnItemNotify60(&m, nullptr, CONTROL_NOTIFY_0E, 0);
        // (The panel's refresh posts first.)
        okp &= !pick(0) && GmInOrder(GmQueue(), {"c600000 1000001 0 0 ; c700000 1000001 0 0 ; "}) && GfLogIs({"return items"});
        g_pickedSoldierA = 0;
        g_pickedSoldierX = 30;
        okp &= pick(1) && m.m_userPreset == 0 && g_pickedSoldierX == 33 && g_pickedSoldierA == 0;
        okp &= GmInOrder(GmQueue(), {"c103000 20000aa 3 0 ; ", "c600000 1000002 0 0 ; ", "c700000 1000002 0 0 ; "});
        // (Clearing the soldier returns its items: none to it, the store refreshed first.)
        okp &= GmInOrder(Join(g_gfLog), {"set profile 33 0", "get stats 33 0", "return items", "stock take 3 1",
                                         "put 1 -1 3 1", "stock take 8 1", "put 10 -1 8 1", "stock take 10 2", "put 20 -1 10 2",
                                         "stock take 11 1", "stock take 12 3", "put 40 -1 12 3"}) &&
               logHas("stock take 13") == 0 && logHas("stock take 14") == 0;
        // (The face updated: an empty user preset's pick keeps the items (no models reset).)
        m.m_panel.m_soldier = pickSoldier;
        sq.profileIndex = 33;
        okp &= pick(2) && pickSoldier->m_face && std::strcmp(pickSoldier->m_face, "gm_p33.bmp") == 0;
        {
            // (No clearing there: the panel's load and the store's entry posted by the pick itself.)
            const std::string q = GmQueue();
            okp &= (q.rfind("0 1000092 ", 0) == 0 || q.find("; 0 1000092 ") != std::string::npos) && q.find("c3f3000") != std::string::npos;
        }
        m.m_panel.m_soldier = nullptr;
        {
            // (The panel refreshed (the load posted), the store's entries updated.)
            g_controlMessages.clear();
            pick(1);
            const std::string q = GmQueue();
            okp &= (q.rfind("0 1000092 ", 0) == 0 || q.find("; 0 1000092 ") != std::string::npos) && q.find("c3f3000") != std::string::npos;
            // (Profile 30: the second list's first.)
            u0->unknown1b4 = 30;
            g_controlMessages.clear();
            okp &= pick(1) && g_pickedSoldierX == 30 && GmQueue().find("c103000 20000aa 0 0 ; ") != std::string::npos;
            u0->unknown1b4 = 33;
            // (The soldier type's clearing refreshes the store: its entry updated.)
            g_controlMessages.clear();
            pick(0);
            okp &= GmQueue().find("c3f3000") != std::string::npos;
        }
        // (A profile of the first list; an empty user preset: no profile, the old items kept.)
        u0->unknown1b4 = 7;
        okp &= pick(1) && g_pickedSoldierA == 7 && GmQueue().find("c102000 20000aa 7 0 ; ") != std::string::npos;
        sq.profileIndex = 9;
        okp &= pick(2) && m.m_userPreset == 1 && g_pickedSoldierA == 4 && logHas("set profile") == 0 &&
               MrHas("get stats 9 0") && logHas("return items") == 0 &&
               GmQueue().find("20000aa") == std::string::npos;
        // (Not empty with a belt item, a backpack item or a backpack only: the old items returned.)
        u1->belt[0].typeId = 10;
        u1->belt[0].count = 1;
        u1->backpackItems[0].typeId = 12;
        u1->backpackItems[0].count = 1;
        for (int kind = 0; kind < 3; ++kind) {
            g_slCounts = {{7, 1}, {10, 1}, {12, 1}};
            u1->beltCount = kind == 0;
            u1->backpackCount = kind == 1;
            u1->backpack = kind == 2 ? 7 : -1;
            okp &= pick(2) && MrHas("return items") && MrHas("set profile 4 0");
        }
        u1->beltCount = u1->backpackCount = 0;
        u1->backpack = -1;
        // The map's preset of the line: the buttons off, not edited; past them: none.
        m.m_userPreset = 1;
        g_slCounts = {{5, 1}, {6, 1}, {7, 1}};
        m.m_userPreset = 1;
        okp &= pick(7) && m.m_userPreset == -1 && GmInOrder(Join(g_gfLog), {"return items", "put 4 -1 5 1",
                                                                            "put 2 -1 6 1", "put 8 -1 7 1"});
        okp &= GmQueue().find("c600000 1000001 0 0 ; ") != std::string::npos;
        okp &= pick(8) && logHas("put") == 0 && logHas("return items") == 0;
        // (A large long gun with another: not put.)
        C_SlDef large(ITEM_ANIMATED, 0, -1, -1);
        large.m_inventorySize = 2;
        const std::map<int, C_ItemDef *> keptDefs = pickDefs;
        g_gfItemDefs = {{8, &large}, {3, &any}, {10, &any}, {11, &any}, {12, &any}, {13, &any}, {14, &any}};
        g_slCounts = {{3, 1}, {8, 1}, {10, 2}, {11, 1}, {12, 3}};
        sq.hands = EpMake(1, 8, 1);
        u0->unknown1b4 = 33;
        okp &= pick(1) && logHas("stock take 8") == 0 && MrHas("stock take 3 1");
        sq.hands = EpMake(-1, -1, 0);
        g_gfItemDefs = keptDefs;
        g_epAllowed.erase(&any);
        g_epProfiles[33]->faceTexture = C_str();
        g_epProfiles.clear();
        m.m_panel.m_soldier = nullptr;
        WM(r.gmTypePicked, okp);

        // A preset's item.
        bool oki = !m.AddPresetItem(ITEM_FITS_HEAD, -1, 1);
        g_slCounts = {{0, 1}};
        g_gfLog.clear();
        oki &= m.AddPresetItem(ITEM_FITS_HEAD, 0, 1) && MrHas("stock take 0 1");
        g_slCounts = {{3, 0}};
        g_gfLog.clear();
        oki &= !m.AddPresetItem(ITEM_FITS_HEAD, 3, 1) && GfLogIs({"stock take 3 1"});
        // (No item type: nothing left.)
        g_slCounts = {{3, 2}};
        oki &= m.AddPresetItem(ITEM_FITS_HEAD, 3, 2);
        C_SlDef d3(ITEM_STATIC, 0, -1, -1);
        g_gfItemDefs = {{3, &d3}};
        g_epAllowed[&d3] = 1;
        g_slCounts = {{3, 2}};
        g_gfLog.clear();
        oki &= m.AddPresetItem(ITEM_FITS_HEAD, 3, 2) && GmInOrder(Join(g_gfLog), {"stock take 3 2", "put 1 -1 3 2"}) &&
               logHas("stock add") == 0;
        g_slCounts = {{3, 2}};
        g_epDisplaced = {EpMake(9, 12, 1)};
        oki &= m.AddPresetItem(ITEM_FITS_HEAD, 3, 2) && MrHas("stock add 12 1");
        g_slCounts = {{3, 2}};
        g_epDisplaced = {EpMake(9, 3, 1)};
        oki &= !m.AddPresetItem(ITEM_FITS_HEAD, 3, 2) && MrHas("stock add 3 1");
        g_slCounts = {{3, 2}};
        g_epDisplaced = {EpMake(9, 12, 0)};
        g_gfLog.clear();
        oki &= m.AddPresetItem(ITEM_FITS_HEAD, 3, 2) && logHas("stock add") == 0;
        // (Not forced: the head taken already, left whole.)
        sq.head = EpMake(1, 4, 1);
        g_slCounts = {{3, 2}};
        g_epDisplaced.clear();
        g_gfLog.clear();
        oki &= !m.AddPresetItem(ITEM_FITS_HEAD, 3, 2) && logHas("put") == 0 && MrHas("stock add 3 2");
        sq.head = EpMake(-1, -1, 0);
        g_epDisplaced.clear();
        g_epAllowed.erase(&d3);
        g_gfItemDefs = keptDefs;
        WM(r.gmPresetItem, oki);
        m.m_panel.m_squadSoldier = nullptr;
        for (auto &h : m.m_panel.m_highlights)
            h = nullptr;
        m.m_storeList.m_stock = nullptr;
        g_slCounts.clear();
        g_controlMessages.clear();
        g_pickedSoldierA = 0;
        g_pickedSoldierX = 30;
        allowed = C_str();
        game->m_mission = oldMission;
    }

    // --- the substitutes (part 9e-xxii): the stock's best by score (the type itself, the same
    // behaviour, scoped reticles, paired ids, the small behaviours' table, a weapon's ammunition);
    // none for ammunition; the type itself best: false. The preset matched: its places swapped (a
    // weapon's ammunition counted, then swapped in the belt and backpack), its items merged ---
    {
        C_GameMenu m;
        C_ItemStock stock;
        m.m_storeList.SetStock(&stock);
        std::vector<std::unique_ptr<C_SlDef>> defs;
        const std::map<int, C_ItemDef *> keptDefs = g_gfItemDefs;
        g_gfItemDefs.clear();
        // A type: its record, behaviour, ammunition, id (its type unless given).
        const auto def = [&](int type, E_ItemRecordType record, int behaviour, int ammo = -1, int id = -1) {
            defs.push_back(std::make_unique<C_SlDef>(record, behaviour, ammo, -1));
            defs.back()->m_id = id < 0 ? type : id;
            g_gfItemDefs[type] = defs.back().get();
            return defs.back().get();
        };
        C_ScopeComp reticle, ironSights;
        reticle.m_aimMode = 2;
        ironSights.m_aimMode = 5;
        const auto best = [&](int type, int ammo) {
            int t = type;
            const bool found = m.FindSubstitute(t, ammo);
            return CmText("%d %d", found, t);
        };
        bool oks = true;
        def(1, ITEM_AMMO, 0);
        g_slCounts = {{1, 1}, {2, 1}};
        def(2, ITEM_STATIC, 0);
        oks &= best(1, 0) == "0 1";
        // The type itself (in the stock) best: false; not there, another of its behaviour.
        def(3, ITEM_STATIC, 0);
        g_slCounts = {{2, 1}, {3, 1}};
        oks &= best(3, 0) == "0 3";
        g_slCounts = {{2, 1}};
        oks &= best(3, 0) == "1 2";
        // None: -1, false.
        g_slCounts = {};
        oks &= best(3, 0) == "0 -1";
        // Behaviour 0x13 never matched; scoped reticles +50 over the same behaviour; paired ids +60.
        def(4, ITEM_STATIC, 0x13);
        def(5, ITEM_STATIC, 0x13);
        g_slCounts = {{5, 1}};
        oks &= best(4, 0) == "0 -1";
        C_SlDef *r10 = def(10, ITEM_ANIMATED, 4), *r11 = def(11, ITEM_ANIMATED, 4), *r12 = def(12, ITEM_ANIMATED, 4);
        r10->m_secondaryComponent = &reticle;
        r12->m_secondaryComponent = &reticle;
        r11->m_secondaryComponent = &ironSights;
        g_slCounts = {{11, 1}, {12, 1}};
        oks &= best(10, 0) == "1 12";
        r12->m_secondaryComponent = &ironSights;
        oks &= best(10, 0) == "1 11";
        // (Paired: the ids 7, 0x16, 0x104.)
        def(20, ITEM_ANIMATED, 2, -1, 7);
        def(21, ITEM_ANIMATED, 2, -1, 0x16);
        def(22, ITEM_ANIMATED, 2, -1, 99);
        g_slCounts = {{22, 1}, {21, 1}};
        oks &= best(20, 0) == "1 21";
        def(23, ITEM_ANIMATED, 2, -1, 0x104);
        g_slCounts = {{22, 1}, {23, 1}};
        oks &= best(20, 0) == "1 23";
        // The small behaviours: by the candidate's (3 and 4: 60 over 5: 50 over 2: 30 over 1: 10).
        def(30, ITEM_ANIMATED, 6);
        def(31, ITEM_ANIMATED, 1);
        def(32, ITEM_ANIMATED, 2);
        def(33, ITEM_ANIMATED, 5);
        def(34, ITEM_ANIMATED, 3);
        def(35, ITEM_ANIMATED, 4);
        def(36, ITEM_ANIMATED, 12);
        def(37, ITEM_ANIMATED, 8);
        g_slCounts = {{31, 1}};
        oks &= best(30, 0) == "1 31";
        g_slCounts = {{31, 1}, {32, 1}};
        oks &= best(30, 0) == "1 32";
        g_slCounts = {{31, 1}, {32, 1}, {33, 1}};
        oks &= best(30, 0) == "1 33";
        g_slCounts = {{33, 1}, {34, 1}};
        oks &= best(30, 0) == "1 34";
        g_slCounts = {{33, 1}, {35, 1}};
        oks &= best(30, 0) == "1 35";
        g_slCounts = {{36, 1}, {32, 1}};
        oks &= best(30, 0) == "1 36";
        // (Not small: nothing; a 12 asking: its 5 +61 more; a small paired with a small +110.)
        g_slCounts = {{37, 1}};
        oks &= best(30, 0) == "0 -1";
        def(38, ITEM_ANIMATED, 12);
        g_slCounts = {{33, 1}, {34, 1}};
        oks &= best(38, 0) == "1 33";
        def(39, ITEM_ANIMATED, 6, -1, 7);
        def(40, ITEM_ANIMATED, 1, -1, 0x104);
        g_slCounts = {{40, 1}, {34, 1}};
        oks &= best(39, 0) == "1 40";
        WM(r.gmSubstitute, oks);
        // A weapon's ammunition: enough +30, short 10 per one, none -100 more.
        def(50, ITEM_ANIMATED, 4, 60);
        def(51, ITEM_ANIMATED, 4, 61);
        def(52, ITEM_ANIMATED, 4, 62);
        def(60, ITEM_AMMO, 0);
        def(61, ITEM_AMMO, 0);
        g_slCounts = {{51, 1}, {52, 1}, {61, 5}};
        oks &= best(50, 4) == "1 51";
        g_slCounts = {{51, 1}, {52, 1}, {61, 2}};
        oks &= best(50, 4) == "1 51";
        g_slCounts = {{51, 1}, {52, 1}, {61, 0}, {62, 4}};
        oks &= best(50, 4) == "1 52";
        // (Neither's ammunition there: both below 0, none.)
        g_slCounts = {{51, 1}, {52, 1}};
        oks &= best(50, 4) == "0 -1";
        // (No ammunition asked: the first of the best.)
        g_slCounts = {{51, 1}, {52, 1}, {61, 0}};
        oks &= best(50, 0) == "1 51";
        // (Above 12 no ammunition counted.)
        def(53, ITEM_ANIMATED, 13, 61);
        def(54, ITEM_ANIMATED, 13, 62);
        g_slCounts = {{54, 1}, {61, 5}};
        oks &= best(53, 4) == "1 54";
        WM(r.gmSubstitute, oks);
        // The scores exactly, by ties: the rival's (the lower type) wins a tie, the candidate's
        // (the higher) a point more. Types 100.. per case (the type asked, the rival, the candidate,
        // their ammunition).
        {
            // A case: the asked type's behaviour (paired: id 7), the rival and the candidate (their
            // behaviours, paired (id 0x16), scoped with reticles (aim 2), ammunition have / asked).
            struct S_Side {
                int behaviour;
                bool paired, scoped;
                int ammoHave;                   // -1: no ammunition
            };
            int base = 100;
            const auto tie = [&](int behaviour, bool paired, bool scoped, S_Side rival, S_Side candidate, int asked) {
                const int t = base, r = base + 1, c = base + 2, ra = base + 3, ca = base + 4;
                base += 10;
                def(t, ITEM_ANIMATED, behaviour, -1, paired ? 7 : t);
                if (scoped)
                    g_gfItemDefs[t]->m_secondaryComponent = &reticle;
                g_slCounts.clear();
                const auto side = [&](int type, int ammoType, const S_Side &s) {
                    C_SlDef *d = def(type, ITEM_ANIMATED, s.behaviour, s.ammoHave >= 0 ? ammoType : -1, s.paired ? 0x16 : type);
                    if (s.scoped)
                        d->m_secondaryComponent = &reticle;
                    g_slCounts[type] = 1;
                    if (s.ammoHave >= 0) {
                        def(ammoType, ITEM_AMMO, 0);
                        g_slCounts[ammoType] = s.ammoHave;
                    }
                };
                side(r, ra, rival);
                side(c, ca, candidate);
                return best(t, asked) == CmText("1 %d", r);
            };
            bool okt = true;
            // Same behaviour +100: the rival a paired small one (10 + 110, 2 short: 100).
            okt &= tie(6, true, false, {1, true, false, 1}, {6, false, false, -1}, 3);
            // Paired small +110: a paired 1 (10 + 110 - 20) against the same behaviour (100).
            okt &= tie(6, true, false, {6, false, false, -1}, {1, true, false, 1}, 3);
            // Scoped reticles +50 (150): a paired 3 (60 + 110 - 20).
            okt &= tie(4, true, true, {3, true, false, 1}, {4, false, true, -1}, 3);
            // Paired same +60 (160): a paired 3, 1 short.
            okt &= tie(4, true, false, {3, true, false, 2}, {4, true, false, -1}, 3);
            // The small table by the candidate's: 2 +30, 3 +60, 4 +60, 5 +50, 12 +50, 1 +10 (the rival
            // the same behaviour, short to match).
            okt &= tie(6, false, false, {6, false, false, 1}, {2, false, false, -1}, 8);
            okt &= tie(6, false, false, {6, false, false, 1}, {3, false, false, -1}, 5);
            okt &= tie(6, false, false, {6, false, false, 1}, {4, false, false, -1}, 5);
            okt &= tie(6, false, false, {6, false, false, 1}, {5, false, false, -1}, 6);
            okt &= tie(6, false, false, {6, false, false, 1}, {12, false, false, -1}, 6);
            okt &= tie(6, false, false, {6, false, false, 1}, {1, false, false, -1}, 10);
            // The ammunition enough +30 (130): a paired 3, 4 short.
            okt &= tie(4, true, false, {3, true, false, 1}, {4, false, false, 5}, 5);
            WM(r.gmSubstitute, okt);
            // A 12 asking: its 5 +61 more (111), the type itself (100 + 51, 4 short) the rival.
            {
                const int t = base, x = base + 2, a = base + 3;
                base += 10;
                def(t, ITEM_ANIMATED, 12, a);
                def(a, ITEM_AMMO, 0);
                def(x, ITEM_ANIMATED, 5);
                g_slCounts = {{t, 1}, {a, 2}, {x, 1}};
                okt &= best(t, 6) == CmText("0 %d", t);
            }
            // The type itself +51 (131, 2 short) over a later same behaviour with enough (130).
            {
                const int x = base, t = base + 2, a = base + 3, xa = base + 4;
                base += 10;
                def(x, ITEM_ANIMATED, 4, xa);
                def(xa, ITEM_AMMO, 0);
                def(t, ITEM_ANIMATED, 4, a);
                def(a, ITEM_AMMO, 0);
                g_slCounts = {{x, 1}, {xa, 4}, {t, 1}, {a, 2}};
                okt &= best(t, 4) == CmText("0 %d", t);
            }
            // Exactly enough: +30 (130 over a paired 3, 5 short: 120).
            {
                const int t = base, r = base + 1, c = base + 2, ra = base + 3, ca = base + 4;
                base += 10;
                def(t, ITEM_ANIMATED, 4, -1, 7);
                def(r, ITEM_ANIMATED, 3, ra, 0x16);
                def(ra, ITEM_AMMO, 0);
                def(c, ITEM_ANIMATED, 4, ca);
                def(ca, ITEM_AMMO, 0);
                g_slCounts = {{r, 1}, {ra, 1}, {c, 1}, {ca, 6}};
                okt &= best(t, 6) == CmText("1 %d", c);
            }
            // None of its ammunition -100 more: a scoped one (150 - 30 - 100: 20) first, tied by a later
            // small 2 (30, 1 short).
            {
                const int t = base, c = base + 1, r = base + 2, ca = base + 3, ra = base + 4;
                base += 10;
                def(t, ITEM_ANIMATED, 4)->m_secondaryComponent = &reticle;
                def(c, ITEM_ANIMATED, 4, ca)->m_secondaryComponent = &reticle;
                def(ca, ITEM_AMMO, 0);
                def(r, ITEM_ANIMATED, 2, ra);
                def(ra, ITEM_AMMO, 0);
                g_slCounts = {{c, 1}, {ca, 0}, {r, 1}, {ra, 2}};
                okt &= best(t, 3) == CmText("1 %d", c);
                // (Aim mode 3 a reticle too.)
                C_ScopeComp three;
                three.m_aimMode = 3;
                g_gfItemDefs[t]->m_secondaryComponent = &three;
                g_gfItemDefs[c]->m_secondaryComponent = &three;
                okt &= best(t, 3) == CmText("1 %d", c);
            }
            // Ammunition asked only above 0; behaviour 12 counted; an ammunition type 0 counted.
            {
                const int t = base, y = base + 1, x = base + 2, xa = base + 3;
                base += 10;
                def(t, ITEM_ANIMATED, 12);
                def(y, ITEM_ANIMATED, 1);
                def(x, ITEM_ANIMATED, 12, xa);
                def(xa, ITEM_AMMO, 0);
                g_slCounts = {{y, 1}, {x, 1}};
                okt &= best(t, 1) == CmText("1 %d", y) && best(t, 0) == CmText("1 %d", x);
                def(0, ITEM_AMMO, 0);
                g_gfItemDefs[x] = defs.back().get();
                C_SlDef *x0 = def(x, ITEM_ANIMATED, 12, 0);
                (void)x0;
                g_slCounts = {{y, 1}, {x, 1}, {0, 0}};
                okt &= best(t, 1) == CmText("1 %d", y);
                g_gfItemDefs.erase(0);
            }
            // A score of exactly 1 chosen (the type itself: 151, 15 short; not small: its ammunition
            // no candidate; an id paired with none).
            {
                const int t = base, a = base + 3;
                base += 10;
                def(t, ITEM_ANIMATED, 8, a, 9999);
                def(a, ITEM_AMMO, 0);
                g_slCounts = {{t, 1}, {a, 1}};
                okt &= best(t, 16) == CmText("0 %d", t);
            }
            // Type 0: a candidate, and asked.
            def(0, ITEM_ANIMATED, 4);
            {
                const int t = base;
                base += 10;
                def(t, ITEM_ANIMATED, 4);
                g_slCounts = {{0, 1}};
                okt &= best(t, 0) == "1 0" && best(0, 0) == "0 0";
            }
            g_gfItemDefs.erase(0);
            // Behaviour 7 not small (as asked or offered).
            {
                const int t = base, x = base + 2, t2 = base + 4, x2 = base + 5;
                base += 10;
                def(t, ITEM_ANIMATED, 7);
                def(x, ITEM_ANIMATED, 3);
                def(t2, ITEM_ANIMATED, 6);
                def(x2, ITEM_ANIMATED, 7);
                g_slCounts = {{x, 1}};
                okt &= best(t, 0) == "0 -1";
                g_slCounts = {{x2, 1}};
                okt &= best(t2, 0) == "0 -1";
            }
            WM(r.gmSubstitute, okt);
        }

        // The match.
        bool okm = true;
        S_LoadoutPreset p;
        p.head = 3;
        p.hands = 50;
        p.longGun = 10;
        p.uniform = 2;
        p.backpack = 1;
        p.beltCount = 3;
        p.belt[0] = {60, 2};
        p.belt[1] = {3, 1};
        p.belt[2] = {60, 1};
        p.backpackCount = 2;
        p.backpackItems[0] = {2, 4};
        p.backpackItems[1] = {1, 1};
        std::strcpy(p.name, "P");
        p.flags = LOADOUT_ALLIED;
        p.textId = 9;
        p.unknown1b4 = 7;
        // (Head 3 kept; weapon 50 swapped for 51 (its ammunition 60 for 61); long gun 10 for 12; the
        // uniform's 2 for 3; the backpack's ammunition none: kept.)
        r12->m_secondaryComponent = &reticle;
        g_slCounts = {{3, 1}, {51, 1}, {61, 9}, {12, 1}};
        S_LoadoutPreset *q = m.MatchPreset(p);
        okm &= q && q->head == 3 && q->hands == 51 && q->longGun == 12 && q->uniform == 3 && q->backpack == 1 &&
               q->name[0] == 0 && q->flags == 0 && q->textId == -1 && q->unknown1b4 == 0;
        // (Quirk kept: the long gun without ammunition keeps the weapon's ammunition type, its swap
        // remapping it to the long gun's none.)
        okm &= q && q->beltCount == 2 && q->belt[0].typeId == -1 && q->belt[0].count == 3 && q->belt[1].typeId == 3 &&
               q->belt[1].count == 1 && q->backpackCount == 2 && q->backpackItems[0].typeId == 3 &&
               q->backpackItems[0].count == 4 && q->backpackItems[1].typeId == 1;
        delete q;
        // (Nothing in the stock: the places none, the items dropped but the unswappable ammunition.)
        g_slCounts = {};
        q = m.MatchPreset(p);
        okm &= q && q->head == -1 && q->hands == -1 && q->longGun == -1 && q->uniform == -1 && q->backpack == 1 &&
               q->beltCount == 1 && q->belt[0].typeId == 60 && q->belt[0].count == 3 && q->backpackCount == 1 &&
               q->backpackItems[0].typeId == 1;
        delete q;
        WM(r.gmMatch, okm);
        // The places' ammunition asked 0 (a weapon candidate short of its own would lose); a type 0
        // kept; the weapons' ammunition counted (its type 0 too: remapped); a weapon without
        // ammunition asks none; 13 belt types at most; the items' substitutes asked no ammunition.
        {
            const int h = 400, w = 401, c = 402, wa = 403;
            def(h, ITEM_STATIC, 6);
            def(w, ITEM_ANIMATED, 3, wa);
            def(wa, ITEM_AMMO, 0);
            def(c, ITEM_ANIMATED, 1);
            g_slCounts = {{w, 9}, {wa, 0}, {c, 9}};
            S_LoadoutPreset places;
            places.head = places.uniform = places.backpack = h;
            places.beltCount = 1;
            places.belt[0] = {h, 1};
            q = m.MatchPreset(places);
            okm &= q->head == w && q->uniform == w && q->backpack == w && q->beltCount == 1 && q->belt[0].typeId == w;
            delete q;
            // (Type 0 kept as a place, a weapon and an item.)
            def(0, ITEM_ANIMATED, 4);
            g_slCounts = {{0, 2}};
            S_LoadoutPreset zero;
            zero.head = zero.hands = 0;
            zero.beltCount = 1;
            zero.belt[0] = {0, 2};
            q = m.MatchPreset(zero);
            okm &= q->head == 0 && q->hands == 0 && q->beltCount == 1 && q->belt[0].typeId == 0 && q->belt[0].count == 2;
            delete q;
            // (The weapon's ammunition counted: 3 of its type 0 in the belt; the substitute's ammunition
            // there (410 short 0: 130 over 411 none) remaps it.)
            const int x2 = 410, x1 = 411, a1 = 412, a2 = 413, hw = 414;
            def(0, ITEM_AMMO, 0);
            def(hw, ITEM_ANIMATED, 4, 0);
            def(x2, ITEM_ANIMATED, 4, a2);
            def(a2, ITEM_AMMO, 0);
            def(x1, ITEM_ANIMATED, 4, a1);
            def(a1, ITEM_AMMO, 0);
            // (409: enough only for 1 of it: ahead of 410 at the count of 1.)
            def(409, ITEM_ANIMATED, 4, 408);
            def(408, ITEM_AMMO, 0);
            g_slCounts = {{409, 1}, {408, 1}, {x1, 1}, {a1, 3}, {x2, 1}, {a2, 0}};
            S_LoadoutPreset armed;
            armed.hands = hw;
            armed.beltCount = 1;
            armed.belt[0] = {0, 3};
            q = m.MatchPreset(armed);
            okm &= q->hands == x1 && q->beltCount == 1 && q->belt[0].typeId == a1;
            delete q;
            // (Weapon type 0 with ammunition 412: counted, 410 chosen.)
            def(0, ITEM_ANIMATED, 4, a1);
            g_slCounts = {{x1, 1}, {a1, 3}, {x2, 1}, {a2, 0}};
            S_LoadoutPreset zeroWeapon;
            zeroWeapon.hands = 0;
            zeroWeapon.beltCount = 1;
            zeroWeapon.belt[0] = {a1, 3};
            q = m.MatchPreset(zeroWeapon);
            okm &= q->hands == x1;
            delete q;
            g_gfItemDefs.erase(0);
            // (A long gun without ammunition after a weapon with it: none asked, the first of two ties.)
            const int lg = 420, k1 = 421, k2 = 422, ka = 423, hh = 424, hha = 425;
            def(hh, ITEM_ANIMATED, 2, hha);
            def(hha, ITEM_AMMO, 0);
            def(lg, ITEM_ANIMATED, 3);
            def(k1, ITEM_ANIMATED, 3, ka);
            def(ka, ITEM_AMMO, 0);
            def(k2, ITEM_ANIMATED, 3);
            g_slCounts = {{hh, 1}, {hha, 3}, {k1, 1}, {ka, 0}, {k2, 1}};
            S_LoadoutPreset guns;
            guns.hands = hh;
            guns.longGun = lg;
            guns.beltCount = 1;
            guns.belt[0] = {hha, 3};
            q = m.MatchPreset(guns);
            okm &= q->hands == hh && q->longGun == k1;
            delete q;
            // (14 ammunition types in the belt: 13 kept.)
            S_LoadoutPreset full;
            full.beltCount = 14;
            for (int i = 0; i < 14; ++i) {
                def(430 + i, ITEM_AMMO, 0);
                full.belt[i] = {430 + i, 1};
            }
            q = m.MatchPreset(full);
            okm &= q->beltCount == 13 && q->belt[12].typeId == 442;
            delete q;
            WM(r.gmMatch, okm);
        }
        r10->m_secondaryComponent = r11->m_secondaryComponent = r12->m_secondaryComponent = nullptr;
        g_slCounts.clear();
        g_gfItemDefs = keptDefs;
        m.m_storeList.m_stock = nullptr;
    }

    // --- the stats (part 9e-xix): the profile's at the local player's rank (none: the defaults); the
    // skills' bars (hundredths; health full) and values (per cent, kg, as they are) ---
    {
        g_gmStats = {{7, {900, 60, 70, 0, 55, 0, 44, 33, 22}}};
        g_gmLocalStats[5] = 3;
        g_controlMessages.clear();
        g_gfLog.clear();
        C_GameMenu::ShowSoldierStats(7);
        const auto row = [](int bar, float fill, const char *text) {
            return CmText("c4%02x000 1000092 0 %x ; c4%02x000 1000014 17 0 %s; ", bar, GmBits(fill), bar + 1, text);
        };
        // (A hundredth of the stat: rounded once, as the original's float product.)
        const auto pc = [](int n) { return static_cast<float>(n) * 0.01f; };
        const std::string want = row(2, pc(55), " 55 %") + row(5, pc(60), " 60 Kg") + row(8, 1.0f, "900") + row(0xB, pc(70), " 70") +
                                 row(0xE, pc(44), " 44 %") + row(0x11, pc(33), " 33 %") + row(0x14, pc(22), " 22 %");
        const std::string got = GmQueue();
        if (got != want)
            std::fprintf(stderr, "gmStats:\n%s\n%s\n", got.c_str(), want.c_str());
        bool okt = got == want && GfLogIs({"get stats 7 3"});
        g_gmLocalStats[5] = 0;
        C_GameMenu::ShowSoldierStats(8);
        okt &= GmQueue() == row(2, pc(80), " 80 %") + row(5, pc(80), " 80 Kg") + row(8, 1.0f, "800") + row(0xB, pc(80), " 80") +
                                row(0xE, pc(50), " 50 %") + row(0x11, pc(30), " 30 %") + row(0x14, pc(20), " 20 %");
        g_gmStats.clear();
        WM(r.gmStats, okt);
    }

    // --- the stocks (part 9e-xix): made, every type unset (-1), the profile's reset; the map's items
    // of a team (known, in range) in the stocks for the others' team: their counts (the list's, else
    // 1; group 5: 10) and their seconds' (a weapon's ammunition 10 but 0x26's, a uniform's head gear
    // 1; the list's count), a type with a count kept ---
    {
        alignas(16) static uint8_t stockMission[sizeof(C_MpMissionDef)];
        std::memset(stockMission, 0, sizeof stockMission);
        auto &items = const_cast<std::vector<S_CampaignItem> &>(reinterpret_cast<C_MpMissionDef *>(stockMission)->GetItems());
        const auto item = [](int id, int count, int ammo) {
            S_CampaignItem i;
            i.itemId = id;
            i.count = count;
            i.ammo = ammo;
            i.ammunition = false;
            return i;
        };
        items = {item(1, 3, -1), item(2, -1, -1), item(0x26, -1, -1), item(4, -1, 7), item(5, -1, -1), item(6, 2, 2),
                 item(7, 1, 1), item(600, 1, 1), item(-1, 1, 1), item(8, -1, -1), item(0, 2, -1), item(500, 1, -1),
                 item(11, -1, -1)};
        C_SlDef d1(ITEM_STATIC, 0, -1, -1), d2(ITEM_ANIMATED, 0, 50, -1), d26(ITEM_ANIMATED, 0, 51, -1);
        C_SlDef d4(ITEM_STATIC, 15, -1, 60), d5(ITEM_STATIC, 0, -1, -1), d6(ITEM_STATIC, 0, -1, -1), d8(ITEM_STATIC, 0, -1, -1);
        C_SlDef d0(ITEM_STATIC, 0, -1, -1), d500(ITEM_STATIC, 0, -1, -1), d11(ITEM_STATIC, 0, -1, -1), d9(ITEM_ANIMATED, 0, 0, -1);
        d0.m_team = d500.m_team = d11.m_team = d9.m_team = 2;
        d1.m_team = 0;
        d2.m_team = 1;
        d26.m_team = d4.m_team = d5.m_team = d8.m_team = 2;
        d6.m_team = 3;
        const std::map<int, C_ItemDef *> oldDefs = g_gfItemDefs;
        g_gfItemDefs = {{1, &d1}, {2, &d2}, {0x26, &d26}, {4, &d4}, {5, &d5}, {6, &d6}, {8, &d8}, {0, &d0}, {500, &d500},
                        {11, &d11}, {9, &d9}};
        g_slGroups[&d5] = 5;
        // (Types 8 and 11 have counts already.)
        g_slCounts = {{8, 2}, {11, 1}};
        C_game *game = GetGame();
        C_MissionDef *const oldMission = game->m_mission;
        game->m_mission = reinterpret_cast<C_MissionDef *>(stockMission);
        C_GameMenu m;
        g_gmStockSets.clear();
        g_gfLog.clear();
        bool oks = m.SetupStocks() && m.m_stock1E4 && m.m_stock1E8 && m.m_stock1E4 != m.m_stock1E8 &&
                   GfLogIs({"items loaded", "stock reset"});
        std::map<int, int> &first = g_gmStockSets[m.m_stock1E4], &second = g_gmStockSets[m.m_stock1E8];
        const auto changed = [](const std::map<int, int> &stock) {
            std::string s;
            for (const auto &e : stock)
                if (e.second != -1)
                    s += CmText("%d:%d ", e.first, e.second);
            return s;
        };
        oks &= first.size() == 500 && second.size() == 500 && first.count(499) && first.count(0) &&
               changed(first) == "0:2 1:3 4:1 5:10 38:1 60:7 " && changed(second) == "0:2 2:1 4:1 5:10 38:1 50:10 60:7 ";
        WM(r.gmStocks, oks);
        // Counts of 0: a weapon's (its ammunition type 0 too).
        delete m.m_stock1E4;
        delete m.m_stock1E8;
        items = {item(9, 0, 0)};
        g_gmStockSets.clear();
        oks &= m.SetupStocks() && changed(g_gmStockSets[m.m_stock1E4]) == "0:0 9:0 " &&
               changed(g_gmStockSets[m.m_stock1E8]) == "0:0 9:0 ";
        WM(r.gmStocks, oks);
        delete m.m_stock1E4;
        delete m.m_stock1E8;
        m.m_stock1E4 = m.m_stock1E8 = nullptr;
        g_gmStockSets.clear();
        g_slCounts.clear();
        g_slGroups.erase(&d5);
        g_gfItemDefs = oldDefs;
        items = std::vector<S_CampaignItem>();
        game->m_mission = oldMission;
    }

    // --- the squad's team (part 9e-xviii): set (2: nothing): the message box closed; the posted
    // change done: the escape; the same team: no more; not co-op: the marks (another team: no more);
    // the store's list and the soldier types on at the first, else the load's button off and the
    // items returned; the pages emptied, the team kept, its stock, its soldiers' list, the items
    // taken, the types. The list: shown, its scroll bar, its pick; the profile, the panel. The items:
    // those the stock has not dropped (the backpack's contents with it), the load ---
    {
        S_MissionSquad squad;
        S_MissionSquad *const oldSquad = g_gameProfile.m_missionSquad;
        g_gameProfile.m_missionSquad = &squad;
        S_SquadSoldier first;
        first.profileIndex = -1;
        first.team = -1;
        squad.states = {&first};
        C_GameMenu m;
        m.m_presets.Clear();
        m.m_soldierTypes = 0x0C200000;
        m.m_panel.m_squadSoldier = &first;
        m.m_stock1E4 = new C_ItemStock();
        m.m_stock1E8 = new C_ItemStock();
        const bool oldCoop = g_coop;
        g_coop = false;
        g_slCounts.clear();
        g_controlMessages.clear();
        g_gfLog.clear();
        g_gameMenuMessages = 3;
        m.SetTeam(2);
        bool okt = GmQueue().empty() && g_gfLog.empty() && g_gameMenuMessages == 3 && m.m_team == -1;
        // (The panel's soldier: dressed (a head of no item type: its type kept) and its face set; the
        // profiles zeroed: no constructor.)
        auto *dressed = new C_MenuSoldier();
        MsWorld(*dressed);
        m.m_panel.m_soldier = dressed;
        alignas(16) static uint8_t teamFaces[61][sizeof(S_SoldierProfile)];
        std::memset(teamFaces, 0, sizeof teamFaces);
        g_epProfiles.clear();
        for (auto &face : teamFaces)
            g_epProfiles.push_back(reinterpret_cast<S_SoldierProfile *>(face));
        g_epProfiles[5]->faceTexture = "gm_t05.bmp";
        g_epProfiles[34]->faceTexture = "gm_t34.bmp";
        first.head = EpMake(1, 499, 1);
        g_pickedSoldierA = 5;
        m.SetTeam(TEAM_ALLIES);
        std::string q = GmQueue();
        okt &= g_gameMenuMessages == 2 && m.m_team == TEAM_ALLIES && first.team == TEAM_ALLIES &&
               m.m_storeList.GetStock() == m.m_stock1E4 && dressed->m_face && std::strcmp(dressed->m_face, "gm_t05.bmp") == 0 &&
               dressed->m_headTypeId == 499 && q.find(" 1000092 ") != std::string::npos && q.find(" 2000085 ") != std::string::npos;
        okt &= GmInOrder(q, {"c104000 100006d 1 0 ; ", "c105000 100006d 0 0 ; ", "1000002 0 0 ; ", "c200000 1000002 0 0 ; ",
                             "c101000 100000b c102000 0 ; ", "c102000 1000003 0 0 ; ", "c103000 1000004 0 0 ; ",
                             "c102000 20000aa 5 0 ; ", "c200000 2000018 0 0 ; "}) &&
               q.find("ca00000") == std::string::npos;
        okt &= GmInOrder(Join(g_gfLog), {"get stats 5 0", "set profile 5 0", "update load"}) && logHas("return items") == 0;
        g_pickedSoldierA = 0;
        // (No soldier for the rest: returning the items would reset its models.)
        m.m_panel.m_soldier = nullptr;
        // The same team: the box closed only.
        g_gfLog.clear();
        m.SetTeam(TEAM_ALLIES);
        okt &= GmQueue().empty() && g_gfLog.empty() && g_gameMenuMessages == 1;
        // The other: the marks, the load's button off, the items returned, the axis' list.
        g_pickedSoldierX = 34;
        m.SetTeam(TEAM_AXIS);
        q = GmQueue();
        okt &= m.m_team == TEAM_AXIS && first.team == TEAM_AXIS && m.m_storeList.GetStock() == m.m_stock1E8 &&
               GmInOrder(q, {"c105000 100006d 1 0 ; ", "c104000 100006d 0 0 ; ", "ca00000 1000001 0 0 ; ",
                             "c101000 100000b c103000 0 ; ", "c102000 1000004 0 0 ; ", "c103000 1000003 0 0 ; ",
                             "c103000 20000aa 4 0 ; ", "c200000 2000018 0 0 ; "}) &&
               q.find("c200000 1000002") == std::string::npos &&
               GmInOrder(Join(g_gfLog), {"return items", "get stats 34 0", "set profile 34 0", "update load"});
        g_pickedSoldierX = 30;
        // The change posted and now the first soldier's: the escape, no more.
        m.m_teamChangePosted = true;
        g_gfLog.clear();
        m.SetTeam(TEAM_AXIS);
        okt &= !m.m_teamChangePosted && GmQueue() == "0 200000a 0 0 ; " && g_gfLog.empty();
        m.m_teamChangePosted = true;
        first.team = TEAM_ALLIES;
        m.SetTeam(TEAM_AXIS);
        okt &= m.m_teamChangePosted && GmQueue().empty();
        first.team = TEAM_AXIS;
        m.m_teamChangePosted = false;
        // Another team (not co-op): nothing more.
        m.SetTeam(3);
        okt &= GmQueue().empty() && g_gfLog.empty() && m.m_team == TEAM_AXIS;
        // Co-op: no marks.
        g_coop = true;
        m.SetTeam(TEAM_ALLIES);
        q = GmQueue();
        okt &= m.m_team == TEAM_ALLIES && q.find("100006d") == std::string::npos && q.find("ca00000 1000001") != std::string::npos;
        g_gfLog.clear();
        m.SetTeam(3);
        okt &= m.m_team == 3 && first.team == 3 && m.m_storeList.GetStock() == m.m_stock1E4 &&
               GmQueue().find("c102000 1000004") == std::string::npos && MrHas("get stats 0 0");
        g_coop = false;
        m.m_panel.m_soldier = nullptr;
        g_epProfiles[5]->faceTexture = C_str();
        g_epProfiles[34]->faceTexture = C_str();
        g_epProfiles.clear();
        first.head = EpMake(-1, -1, 0);
        WM(r.gmTeam, okt);

        // The items taken.
        bool oki = true;
        S_SquadSoldier sq;
        sq.head = EpMake(1, 3, 1);
        sq.uniform = EpMake(2, 4, 1);
        sq.hands = EpMake(3, 6, 1);
        sq.longGun = EpMake(9, 5, 1);
        sq.belt = {EpMake(4, 7, 1), EpMake(5, 8, 1)};
        sq.backpack = EpMake(6, 9, 1);
        sq.backpackContents = {EpMake(7, 10, 1)};
        g_slCounts = {{3, 1}, {5, 1}, {6, 2}, {7, 1}, {10, 1}};
        m.m_panel.m_squadSoldier = &sq;
        m.m_storeList.SetStock(m.m_stock1E4);
        g_epRemoved.clear();
        g_gfLog.clear();
        m.TakeSoldierItems();
        oki &= MsLogIs({"stock take 3 1", "stock take 4 1", "stock take 5 1", "stock take 6 1", "stock take 8 1", "remove 20 5 8 1",
                        "stock take 7 1", "stock take 9 1", "update load"},
                       g_gfLog) &&
               sq.head.typeId == 3 && sq.uniform.typeId == -1 && sq.uniform.uid == -1 && sq.uniform.present == 0 &&
               sq.hands.typeId == 6 && sq.longGun.typeId == 5 && sq.backpack.typeId == -1 && sq.backpack.uid == -1 &&
               sq.backpack.present == 0 && sq.backpackContents.empty();
        // The backpack kept: its contents taken (the last first; those not: removed).
        sq.backpack = EpMake(6, 9, 1);
        sq.backpackContents = {EpMake(7, 10, 1), EpMake(8, 11, 1)};
        sq.belt.clear();
        sq.head = sq.uniform = sq.hands = sq.longGun = EpMake(-1, -1, 0);
        g_slCounts = {{9, 1}, {10, 1}};
        g_gfLog.clear();
        m.TakeSoldierItems();
        oki &= MsLogIs({"stock take 9 1", "stock take 11 1", "remove 40 8 11 1", "stock take 10 1", "update load"}, g_gfLog) &&
               sq.backpack.typeId == 9 && sq.backpackContents.size() == 2;
        WM(r.gmTakeItems, oki);
        m.m_panel.m_squadSoldier = &first;

        // The shown callback: the squad's soldiers, the panel's soldier, the team (none: asked for).
        bool oks = true;
        const auto shown = [&]() {
            g_gfLog.clear();
            return C_GameMenu::OnPlayerScreenShown(&m, &m, nullptr, CONTROL_NOTIFY_SHOWN, 0);
        };
        squad.states.clear();
        m.m_hasPlayerSlotMember = false;
        oks &= shown() && squad.states.size() == 1 && m.m_panel.m_squadSoldier == squad.states[0] &&
               GfLogIs({"add soldier", "command 5 2 0 0"});
        g_coop = true;
        oks &= shown() && squad.states.size() == 1 && GfLogIs({"command 5 1 0 0"});
        g_coop = false;
        m.m_hasPlayerSlotMember = true;
        squad.states = {&first};
        first.profileIndex = 12;
        first.team = TEAM_AXIS;
        m.m_team = TEAM_AXIS;
        oks &= shown() && squad.states.size() == 2 && m.m_panel.m_squadSoldier == squad.states[1] &&
               squad.states[1]->team == TEAM_AXIS && squad.states[1]->profileIndex == 12 &&
               GfLogIs({"add soldier", "copy from 12"}) && GmQueue().find("100006d") == std::string::npos;
        oks &= shown() && squad.states.size() == 2 && m.m_panel.m_squadSoldier == squad.states[1] && g_gfLog.empty();
        squad.states.push_back(&first);
        oks &= shown() && squad.states.size() == 3 && m.m_panel.m_squadSoldier == squad.states[1];
        m.m_hasPlayerSlotMember = false;
        squad.states = {&first};
        m.m_team = -1;
        first.team = TEAM_ALLIES;
        oks &= shown() && m.m_panel.m_squadSoldier == &first && m.m_team == TEAM_ALLIES && MrHas("get stats 0 0");
        squad.states.clear();
        m.m_hasPlayerSlotMember = true;
        oks &= shown() && squad.states.size() == 1 && m.m_panel.m_squadSoldier == squad.states[0];
        WM(r.gmShown, oks);
        g_controlMessages.clear();
        m.m_panel.m_squadSoldier = nullptr;
        g_coop = oldCoop;
        g_slCounts.clear();
        g_gmAdded.clear();
        g_gameMenuMessages = 0;
        g_gameProfile.m_missionSquad = oldSquad;
    }

    // --- the soldiers panel's callbacks (part 9e-xiv): a picture clicked: its index asked, kept for
    // its list (0 .. 29, 30 .. 59), the stats shown, the squad soldier's profile set (level 0), the
    // panel refreshed and its face; no index: nothing; true. A side's mark (not co-op): the team asked
    // for when it is not the squad's (another control: false; none: no team); true ---
    {
        bool okp = true;
        C_GameMenu m;
        S_SquadSoldier squad;
        squad.profileIndex = -1;
        m.m_panel.m_squadSoldier = &squad;
        // (A soldier on its world: its face set; leaked. The profiles zeroed: no constructor.)
        auto *soldier = new C_MenuSoldier();
        MsWorld(*soldier);
        m.m_panel.m_soldier = soldier;
        alignas(16) static uint8_t faces[61][sizeof(S_SoldierProfile)];
        std::memset(faces, 0, sizeof faces);
        g_epProfiles.clear();
        for (auto &face : faces)
            g_epProfiles.push_back(reinterpret_cast<S_SoldierProfile *>(face));
        g_epProfiles[0]->faceTexture = "gm_f000.bmp";
        g_epProfiles[30]->faceTexture = "gm_f030.bmp";
        // (The lists' first picks: 0 and 30.)
        okp &= g_pickedSoldierA == 0 && g_pickedSoldierX == 30;
        const auto pick = [&](int index) {
            C_GmIndexed control(0x0C102100, index);
            g_gfLog.clear();
            g_controlMessages.clear();
            return C_GameMenu::OnSoldierPicked(&m, &m, &control, CONTROL_NOTIFY_CLICK, 0);
        };
        g_pickedSoldierA = 3;
        g_pickedSoldierX = 33;
        // (The face the soldier keeps.)
        const auto face = [soldier](const char *texture) { return soldier->m_face && std::strcmp(soldier->m_face, texture) == 0; };
        soldier->m_face = "none";
        okp &= pick(-1) && GfLogIs({"index 100000a"}) && g_controlMessages.empty() && g_pickedSoldierA == 3 && face("none") &&
               g_pickedSoldierX == 33 && squad.profileIndex == -1;
        okp &= pick(0) && GfLogIs({"index 100000a", "get stats 0 0", "set profile 0 0"}) && face("gm_f000.bmp") &&
               !g_controlMessages.empty() &&
               g_pickedSoldierA == 0 && g_pickedSoldierX == 33 && squad.profileIndex == 0;
        okp &= pick(29) && g_pickedSoldierA == 29 && g_pickedSoldierX == 33;
        okp &= pick(30) && g_pickedSoldierA == 29 && g_pickedSoldierX == 30 && face("gm_f030.bmp");
        okp &= pick(59) && g_pickedSoldierA == 29 && g_pickedSoldierX == 59 && MrHas("set profile 59 0");
        okp &= pick(60) && g_pickedSoldierA == 29 && g_pickedSoldierX == 59 && GfLogIs({"index 100000a", "get stats 60 0", "set profile 60 0"}) && face("");
        m.m_panel.m_squadSoldier = nullptr;
        m.m_panel.m_soldier = nullptr;
        g_epProfiles[0]->faceTexture = C_str();
        g_epProfiles[30]->faceTexture = C_str();
        g_epProfiles.clear();
        g_controlMessages.clear();
        WM(r.gmSoldierPicked, okp);
        g_pickedSoldierA = 0;
        g_pickedSoldierX = 30;

        bool okt = true;
        const bool oldCoop = g_coop;
        g_coop = false;
        m.m_team = 0;
        const auto side = [&](uint32_t id, bool none = false) {
            C_GmIndexed control(id, 0);
            g_gfLog.clear();
            return C_GameMenu::OnSide(&m, &m, none ? nullptr : &control, CONTROL_NOTIFY_0E, 0);
        };
        okt &= side(0x0C104000) && GfLogIs({"command 5 1 0 0"});
        okt &= side(0x0C105000) && g_gfLog.empty();
        m.m_team = 1;
        okt &= side(0x0C105000) && GfLogIs({"command 5 0 0 0"}) && side(0x0C104000) && g_gfLog.empty();
        okt &= !side(0x0C106000) && g_gfLog.empty() && side(0, true) && g_gfLog.empty();
        m.m_team = -1;
        okt &= side(0, true) && g_gfLog.empty();
        g_coop = true;
        okt &= !side(0x0C104000) && g_gfLog.empty();
        g_coop = oldCoop;
        WM(r.gmSide, okt);
    }

    // --- the player screen's callbacks (part 9e-xiii): hidden true; the game info's: the info screen
    // shown, true; the chat line sent ("/t": the team's, "/a": all; either skipped with the spaces after,
    // not past a byte from 0x80; the net game's), true ---
    {
        C_GameMenu m;
        alignas(16) static uint8_t infoMemory[64];
        m.m_infoScreen = reinterpret_cast<C_InfoScreen *>(infoMemory);
        g_gfLog.clear();
        WM(r.gmPlayerCallbacks, C_GameMenu::OnPlayerScreenHidden(&m, nullptr, nullptr, 0, 0) && g_gfLog.empty() &&
                                    C_GameMenu::OnGameInfo(&m, nullptr, nullptr, CONTROL_NOTIFY_CLICK, 0) &&
                                    GfLogIs({"info show 1"}));
        m.m_infoScreen = nullptr;
        bool okc = true;
        const auto chat = [](const char *line) {
            g_gfLog.clear();
            const bool sent = C_GameMenu::OnChat(nullptr, nullptr, nullptr, CONTROL_NOTIFY_SEND,
                                                 static_cast<uint32_t>(reinterpret_cast<uintptr_t>(line)));
            return sent && g_gfLog.size() == 1 ? g_gfLog[0] : std::string("none");
        };
        okc &= chat("/t  \t hello /a") == "chat [hello /a] 1";
        okc &= chat("/aall") == "chat [all] 0";
        okc &= chat("/a/t x") == "chat [/t x] 0";
        okc &= chat(" x /t") == "chat [x /t] 0";
        okc &= chat("x/t") == "chat [x/t] 0";
        okc &= chat("/T up") == "chat [/T up] 0";
        okc &= chat("/t\xE9 x") == "chat [\xE9 x] 1";
        okc &= chat("/t") == "chat [] 1";
        okc &= chat("\n\v\f\r plain") == "chat [plain] 0";
        WM(r.gmChat, okc);
    }

    // --- the hover (part 9e-xxiii): no control false; the label's (posted; none: nothing) or the
    // popup's (sent; the info screen shown: hidden, false): left hidden, entered: the item's lines ---
    {
        C_GameMenu m;
        bool okh = true;
        const bool oldTextIds = g_textIds;
        g_textIds = true;
        std::vector<std::unique_ptr<C_GmHoverDef>> defs;
        const auto def = [&](int type, int behaviour, int ammo = -1, float amount = 0.0f) {
            defs.push_back(std::make_unique<C_GmHoverDef>(behaviour, ammo, amount));
            C_GmHoverDef *d = defs.back().get();
            d->m_textId = 0x300 + type;
            d->m_weight = 1.25f + static_cast<float>(type);
            g_gfItemDefs[type] = d;
            return d;
        };
        C_Control control(nullptr, 0x0C301000, nullptr);
        const auto hover = [&](uint32_t notification, int type = 0) {
            control.m_index = type;
            g_controlMessages.clear();
            g_gfLog.clear();
            const bool answer = m.OnItemHover(nullptr, &control, notification, 0);
            return CmText("%d ", answer) + GmQueue() + Join(g_gfLog);
        };
        g_controlMessages.clear();
        okh &= !m.OnItemHover(nullptr, nullptr, CONTROL_NOTIFY_ENTER, 0) && g_controlMessages.empty();
        // No label, no popup: nothing.
        def(1, 0);
        okh &= hover(CONTROL_NOTIFY_ENTER, 1) == "1 " && hover(CONTROL_NOTIFY_LEAVE, 1) == "1 ";
        C_GmBasePeek::SetWeaponsInfo(m, 0x0C123000);
        okh &= hover(CONTROL_NOTIFY_LEAVE, 1) == "1 c123000 1000004 0 0 ; ";
        okh &= hover(CONTROL_NOTIFY_ENTER | 1, 1) == "1 " && hover(CONTROL_NOTIFY_CLICK, 1) == "1 ";
        // No item: nothing.
        g_gfItemDefs[7] = nullptr;
        okh &= hover(CONTROL_NOTIFY_ENTER, 7) == "1 " && hover(CONTROL_NOTIFY_ENTER, -1) == "1 " &&
               hover(CONTROL_NOTIFY_ENTER, 500) == "1 ";
        const std::string head = "c123000 2000018 0 0 ; c123000 1000017 10 301 ; c123000 1000016 11 0 text 2730 : 2.25 kg; ";
        const std::string show = "c123000 1000003 0 0 ; ";
        okh &= hover(CONTROL_NOTIFY_ENTER, 1) == "1 " + head + show;
        // The weapons (behaviours 2 .. 5, 12): the ammo's rounds (truncated), the muzzle speed (the
        // projectile weapons and firearms), a rifle's scope's zoom (its reticles' modes).
        C_GmHoverDef *ammo = def(2, 0, -1, 30.75f);
        C_GmWeapon projectile(ICK_PROJECTILE_WEAPON, 715.5f), firearm(ICK_FIREARM, 99.3f), other(ICK_SCOPE, 5.0f);
        C_ScopeComp scope;
        scope.m_aimMode = 3;
        scope.m_aimedFovFactor = 0.5f;
        const float zoom = static_cast<float>(std::tan(0.6981317202250164) / std::tan(0.5f * 0.5));
        const std::string zoomLine = CmText("text 2734 : %.2f x", static_cast<double>(zoom));
        const auto lines = [&](int type, int behaviour) {
            C_GmHoverDef *d = def(type, behaviour, 2);
            d->m_component = &projectile;
            d->m_secondaryComponent = &scope;
            const std::string h = CmText("c123000 2000018 0 0 ; c123000 1000017 10 %x ; c123000 1000016 11 0 text 2730 : %.2f kg; ",
                                         0x300 + type, static_cast<double>(d->m_weight));
            const std::string got = hover(CONTROL_NOTIFY_ENTER, type);
            d->m_component = nullptr;
            d->m_secondaryComponent = nullptr;
            if (got == "1 " + h + show)
                return std::string("none");
            if (got == "1 " + h + "c123000 1000016 11 0 text 2731 : 30; c123000 1000016 11 0 text 2732 : 715.50 m/s; " + show)
                return std::string("weapon");
            if (got == "1 " + h + "c123000 1000016 11 0 text 2731 : 30; c123000 1000016 11 0 text 2732 : 715.50 m/s; c123000 1000016 11 0 " +
                           zoomLine + "; " + show)
                return std::string("rifle");
            return got;
        };
        okh &= lines(10, 1) == "none" && lines(11, 2) == "weapon" && lines(12, 3) == "weapon" && lines(13, 4) == "rifle" &&
               lines(14, 5) == "weapon" && lines(15, 6) == "none" && lines(16, 11) == "none" && lines(17, 12) == "weapon" &&
               lines(18, 13) == "none";
        // No ammo item; the firearm's speed; another component: none; the scope's modes, kind.
        C_GmHoverDef *gun = def(20, 4, 2);
        const std::string gunHead = "c123000 2000018 0 0 ; c123000 1000017 10 314 ; c123000 1000016 11 0 text 2730 : 21.25 kg; ";
        okh &= hover(CONTROL_NOTIFY_ENTER, 20) == "1 " + gunHead + "c123000 1000016 11 0 text 2731 : 30; " + show;
        gun->ammo = 7;
        gun->m_component = &firearm;
        okh &= hover(CONTROL_NOTIFY_ENTER, 20) == "1 " + gunHead + "c123000 1000016 11 0 text 2732 : 99.30 m/s; " + show;
        gun->m_component = &other;
        gun->m_secondaryComponent = &scope;
        okh &= hover(CONTROL_NOTIFY_ENTER, 20) == "1 " + gunHead + "c123000 1000016 11 0 " + zoomLine + "; " + show;
        scope.m_aimMode = 4;
        okh &= hover(CONTROL_NOTIFY_ENTER, 20) == "1 " + gunHead + show;
        scope.m_aimMode = 3;
        gun->m_secondaryComponent = &firearm;
        okh &= hover(CONTROL_NOTIFY_ENTER, 20) == "1 " + gunHead + show;
        gun->ammo = 2;
        ammo->amount = 0.99f;
        gun->m_secondaryComponent = nullptr;
        okh &= hover(CONTROL_NOTIFY_ENTER, 20) == "1 " + gunHead + "c123000 1000016 11 0 text 2731 : 0; " + show;
        // The popup: sent to it (target 0), the label's ignored (none: still sent); the info screen
        // shown: hidden, false.
        C_GmPopup popup(m.m_scene);
        m.m_popup = &popup;
        C_GmBasePeek::SetWeaponsInfo(m, 0);
        okh &= hover(CONTROL_NOTIFY_LEAVE, 1) == "1 popup 0 1000004 0 0 ; ";
        okh &= hover(CONTROL_NOTIFY_ENTER, 1) ==
               "1 popup 0 2000018 0 0 ; popup 0 1000017 10 301 ; popup 0 1000016 11 0 text 2730 : 2.25 kg; popup 0 1000003 0 0 ; ";
        okh &= hover(CONTROL_NOTIFY_ENTER, 7) == "1 " && hover(CONTROL_NOTIFY_CLICK, 1) == "1 ";
        alignas(16) static uint8_t infoMemory[64];
        m.m_infoScreen = reinterpret_cast<C_InfoScreen *>(infoMemory);
        g_gmInfoShown = true;
        okh &= hover(CONTROL_NOTIFY_ENTER, 1) == "0 popup 0 1000004 0 0 ; ";
        okh &= hover(CONTROL_NOTIFY_LEAVE, 1) == "1 popup 0 1000004 0 0 ; ";
        g_gmInfoShown = false;
        okh &= hover(CONTROL_NOTIFY_ENTER, 7) == "1 ";
        // (A label too: the popup's still, target 0.)
        C_GmBasePeek::SetWeaponsInfo(m, 0x0C123000);
        okh &= hover(CONTROL_NOTIFY_LEAVE, 1) == "1 popup 0 1000004 0 0 ; ";
        // No popup: the info screen shown does not matter.
        m.m_popup = nullptr;
        g_gmInfoShown = true;
        okh &= hover(CONTROL_NOTIFY_ENTER, 1) == "1 " + head + show;
        g_gmInfoShown = false;
        m.m_infoScreen = nullptr;
        for (int t : {1, 2, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20})
            g_gfItemDefs[t] = nullptr;
        g_textIds = oldTextIds;
        WM(r.gmHover, okh);

        // The chat's lines; the stats toggle (the skills panel or the store shown, the button's text).
        bool okt = true;
        g_controlMessages.clear();
        C_GameMenu::AddChatLine(5, "ann", "hi there");
        okt &= GmQueue() == "cc00000 1000016 26 0 ann: hi there; ";
        g_soldierStatsShown = false;
        okt &= C_GameMenu::OnStatsToggle(nullptr, nullptr, nullptr, 0, 0) && g_soldierStatsShown &&
               GmQueue() == "c400000 1000003 0 0 ; c300000 1000004 0 0 ; c700000 1000015 c 9e1 ; ";
        okt &= C_GameMenu::OnStatsToggle(nullptr, nullptr, nullptr, 0, 0) && !g_soldierStatsShown &&
               GmQueue() == "c300000 1000003 0 0 ; c400000 1000004 0 0 ; c700000 1000015 c 9e2 ; ";
        WM(r.gmChatLine, okt);
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
                "gmMainLocal %d gmMainNet %d gmBuild %d gmBuildNet %d gmPlayer %d gmPlayerCoop %d gmPlayerCallbacks %d "
                "gmChat %d gmSoldiers %d gmSoldiersNone %d gmSoldierPicked %d gmSide %d gmEquipment %d gmEquipmentNone %d "
                "gmItems %d gmItemsNone %d gmSkills %d gmSoldierTypes %d gmStorePreset %d gmTypeEdited %d "
                "gmPresetSaved %d gmTeam %d gmTakeItems %d gmShown %d gmStats %d gmStocks %d gmClicked %d "
                "gmDragStart %d gmDragMove %d gmDrop %d gmTypePicked %d gmPresetItem %d "
                "gmSubstitute %d gmMatch %d gmHover %d gmChatLine %d\n",
                r.gmNew, r.gmNewClient, r.gmStart, r.gmStartNet, r.gmStop, r.gmInput, r.gmUpdate, r.gmTick, r.gmDraw,
                r.gmMessage, r.gmObjectives, r.gmBase, r.gmLoad, r.gmCoop, r.gmNet, r.gmRender, r.gmRenderGame,
                r.gmRenderEquip, r.gmOptions, r.gmEscape, r.gmEscapeNet, r.gmSquad, r.gmQuestions, r.gmMainLocal,
                r.gmMainNet, r.gmBuild, r.gmBuildNet, r.gmPlayer, r.gmPlayerCoop, r.gmPlayerCallbacks, r.gmChat, r.gmSoldiers,
                r.gmSoldiersNone, r.gmSoldierPicked, r.gmSide, r.gmEquipment, r.gmEquipmentNone,
                r.gmItems, r.gmItemsNone, r.gmSkills, r.gmSoldierTypes, r.gmStorePreset, r.gmTypeEdited,
                r.gmPresetSaved, r.gmTeam, r.gmTakeItems, r.gmShown, r.gmStats, r.gmStocks, r.gmClicked,
                r.gmDragStart, r.gmDragMove, r.gmDrop, r.gmTypePicked, r.gmPresetItem,
                r.gmSubstitute, r.gmMatch, r.gmHover, r.gmChatLine);
}
