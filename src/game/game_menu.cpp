// The in-game menu (original 0x0067eb40..0x00687f50; part 9e-v: the class and the menu state's slots;
// 9e-vi: the update's helpers and the drawing; 9e-vii: the messages; 9e-ix: the screens' descriptions;
// 9e-xiii: the player screen's; 9e-xiv..9e-xvi: its soldiers, equipment, items and skills panels;
// 9e-xvii: the soldier types and the user presets; 9e-xviii: the squad's team; 9e-xix: the
// stats and the stocks).
#include "game/game_menu.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <malloc.h>

#include "common/c_str.h"
#include "game/campaign.h"
#include "game/controls.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/human.h"
#include "game/interface.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "game/sound_module.h"
#include "game/team.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

int g_gameMenuTime;                                     // 0x008ae62c
int g_gameMenuMessages;                                 // 0x008ae628
int g_pickedSoldierA;                                   // 0x008ae634
int g_pickedSoldierX = 30;                              // 0x008410ac

namespace {

// A screen's index: its id's top 6 bits (the message box's 0).
constexpr int SCREEN_INDEX_SHIFT = 26;
constexpr uint32_t SCREEN_INDEX_MASK = 0x3F;
constexpr uint32_t MESSAGE_BOX_SCREEN = 0;
// (game_internal.h's, which needs more of the game than this file does.)
constexpr int DIFFICULTY_NO_SAVES = 4;
// The menu's screens (their indices): the main screen (opened first), the options (escape from them
// goes back) and a network game's player screen (the equipment: drawn with the panel's soldier, the
// others over the game behind; escape goes back too).
constexpr uint32_t MAIN_SCREEN = 1;
constexpr uint32_t OPTIONS_SCREEN = 2;
constexpr uint32_t PLAYER_SCREEN = 3;
// The main screen's controls: buttons (style 0xD) and the version's text (style 0x28); the player
// screen's: the combo box (its header style 0xC, its lines 0x13; text 500 the soldier types'), the
// labels, the chat input, the texts (style 0xB; the warmup's and the time's TIME_STYLE).
constexpr int CONTROL_BUTTON = 0x14, CONTROL_TEXT = 0x13;
constexpr int CONTROL_COMBO = 0x11, CONTROL_LABEL = 0x12, CONTROL_CHAT = 0x21;
constexpr uint32_t BUTTON_STYLE = 0xD, VERSION_STYLE = 0x28;
constexpr uint32_t TEXT_STYLE = 0xB, LIST_STYLE = 0xC, LIST_ITEM_STYLE = 0x13;
constexpr uint32_t TEXT_VERSION = 0x7D6, TEXT_SOLDIER_TYPES = 500;
// The soldiers panel (the player screen's first control: 0x0C100000): its group, its scroll bar,
// its two hiding lists (4 pictures shown, 0.062 apart) of 30 profiles each, the soldiers' pictures,
// the sides' marks (ALLIES_COUNT, AXIS_COUNT: they show the teams' counts too).
constexpr int CONTROL_GROUP = 5, CONTROL_SCROLL_BAR = 8, CONTROL_HIDING_LIST = 0x23, CONTROL_PICTURE = 4;
constexpr int CONTROL_MARK = 0x1F;
constexpr uint32_t SOLDIERS_SCROLL = 0x0C101000, SOLDIERS_LIST_A = 0x0C102000, SOLDIERS_LIST_X = 0x0C103000;
constexpr int SOLDIERS_PER_LIST = 30, SOLDIER_LIST_ROWS = 4;
constexpr float SOLDIER_ACROSS_STEP = 0.062f;
// The skills panel: the bars (along z) and the skills' names' texts (0xA78 + the skill), the values'.
constexpr int CONTROL_BAR = 0x16;
constexpr uint32_t BAR_AXIS_Z = 2;
constexpr uint32_t TEXT_SKILLS = 0xA78, TEXT_SKILL_VALUE = 0x7D2;
// The skills panel's controls (the player screen's fourth: 0x0C400000; a skill's name, bar and
// value 1..3 + 3 per skill) and the values' style; the default stats (a profile not found).
constexpr uint32_t SKILLS_PANEL = 0x0C400000;
constexpr uint32_t SKILL_VALUE_STYLE = 0x17;
constexpr int DEFAULT_STATS[12] = {800, 80, 80, 80, 80, 80, 50, 30, 20, 50, 0, 80};
// The stocks: the item types, an unset count, the inventory group counted by ten, the item type a
// weapon's ammunition is not counted for (0x26), the uniforms' behaviour, the items of no team.
constexpr int STOCK_TYPES = 500;
constexpr int STOCK_GROUP_BY_TEN = 5, STOCK_NO_AMMO_TYPE = 0x26, BEHAVIOUR_UNIFORM = 15, ITEM_TEAM_NONE = 3;
// A user preset's name as the menu writes it (29 characters), its belt's and backpack's item types
// as the menu stores them (13 and 31: one less than the preset holds).
constexpr size_t PRESET_NAME_LENGTH = 0x1D;
constexpr int PRESET_BELT_TYPES = 13, PRESET_BACKPACK_TYPES = 31;
// What the buttons post when clicked: their routes (CONTROL_NOTIFY_CLICK | the message).
constexpr uint32_t CLICK = CONTROL_NOTIFY_CLICK;
// The options handler's opening of its screen (options_handler.cpp).
constexpr uint32_t OPTIONS_MSG_OPEN = 0x2000071;

// The in-game menu's own messages (target 0; posted by its screens' controls).
enum : uint32_t {
    GAME_MENU_MSG_APPLY_SOLDIER = 0x200006A,            // the squad's edited soldier kept (inferred)
    GAME_MENU_MSG_DROP_SOLDIER = 0x2000077,             // the squad's edited soldier dropped (inferred)
    GAME_MENU_MSG_QUIT_QUESTION = 0x2000078,
    GAME_MENU_MSG_RESTART_QUESTION = 0x20000B4,
    GAME_MENU_MSG_RESTART = 0x20000B5,
    GAME_MENU_MSG_QUICK_SAVE = 0x20000B6,
    GAME_MENU_MSG_QUICK_LOAD = 0x20000B7,
    GAME_MENU_MSG_QUIT = 0x20000B8,                     // the mission ends into the main menu
    GAME_MENU_MSG_END_MISSION = 0x20000C5,
    GAME_MENU_MSG_END_MISSION_2 = 0x20000C6,
};
constexpr uint32_t TEXT_QUIT_QUESTION = 0x7D9, TEXT_RESTART_QUESTION = 0x7D4;
// The warmup's menu (C_game's OpenGameMenu(-2)).
void *const WARMUP_MENU = reinterpret_cast<void *>(static_cast<intptr_t>(-2));
// The app's command a changed team posts (the team as its argument; inferred).
constexpr int APP_NET_COMMAND_TEAM = 5;

// The presets of a squad's team (its loadouts' flags: the axis', the allied, the deathmatch's).
uint32_t TeamLoadouts(int team)
{
    switch (team) {
    case TEAM_AXIS:
        return LOADOUT_AXIS;
    case TEAM_ALLIES:
        return LOADOUT_ALLIED;
    case 2:
        return LOADOUT_DEATHMATCH;
    default:
        return 2;
    }
}

// A float as a message's value2.
uintptr_t FloatValue(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// The network games' controls (UpdateLoadButton, UpdateNetStatus).
constexpr uint32_t LOAD_BUTTON = 0x0CA00000;
constexpr uint32_t COOP_PLAYERS = 0x0D300000;           // a list: a row per player
constexpr uint32_t ALLIES_COUNT = 0x0C104000;
constexpr uint32_t AXIS_COUNT = 0x0C105000;
constexpr uint32_t RESPAWN_TIME = 0x0C800000;
constexpr uint32_t ROUND_TIME = 0x0C900000;
// The co-op list's styles and texts (its title; a player without a human, dead, alive).
constexpr uint32_t COOP_TITLE_STYLE = 0x56, COOP_NAME_STYLE = 0x57, COOP_STATE_STYLE = 0x58;
constexpr uint32_t TEXT_COOP_TITLE = 0x4F4C, TEXT_COOP_NO_HUMAN = 0x4F4D, TEXT_COOP_DEAD = 0x4F4E,
                   TEXT_COOP_ALIVE = 0x4F4F;
constexpr int TEXT_RANKS = 0x2792;                      // the allied ranks (eight)
constexpr int PLAYER_RANK = 5;                          // S_NetPlayerState::stats
constexpr int MAX_RANK = 7;
constexpr uint32_t TIME_STYLE = 0x2D, TIME_RUN = 1;
// The I3D driver's states the game's drawing changes.
constexpr int DRIVER_STATE_5 = 5;
constexpr int DRIVER_STATE_6 = 6;
// The game's render callback's phases (C_game's).
constexpr uint32_t GAME_RENDER_PHASES = 6;

void Post(uint32_t target, uint32_t id, uint32_t value, uintptr_t value2 = 0, const C_str &text = C_str())
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    message.value2 = value2;
    message.text = text;
    PostControlMessage(message);
}

// " m:ss" of a time in ms.
C_str FormatTime(int ms)
{
    const int seconds = ms / 1000;
    return C_fstr(" %2d:%02d", seconds / 60, seconds % 60);
}

// The deathmatch's players with a human by their uniforms' sides (0 allied, 1 axis).
void CountUniformSides(int *axis, int *allies)          // 0x006801f0 (__fastcall)
{
    *axis = 0;
    *allies = 0;
    S_NetPlayerState *players = g_netGame.LockPlayers();
    for (int i = 0; i < g_netGame.GetPlayerCount(); ++i) {
        C_Human *human = players[i].human;
        if (!human)
            continue;
        const C_Item *uniform = human->GetUniformItem();
        if (!uniform)
            continue;
        const int side = uniform->GetDef()->GetTeam();
        if (side == 0)
            ++*allies;
        else if (side == 1)
            ++*axis;
    }
    g_netGame.UnlockPlayers();
}

// The squad's soldier at `index` deleted (a network game's squad of two: the soldier and his edited
// copy).
void DeleteSquadSoldier(std::vector<S_SquadSoldier *> &squad, std::size_t index)
{
    delete squad[index];
    squad.erase(squad.begin() + static_cast<std::ptrdiff_t>(index));
}

bool IsLocalGame()
{
    return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL;
}

} // namespace

// ---- the mission's objectives

bool C_MissionDef::AreObjectivesDecided() const
{
    for (const S_MissionObjective *objective : m_objectives) {
        if (objective->status == OBJECTIVE_INACTIVE)
            continue;
        // Some objective active: none of them open.
        for (const S_MissionObjective *other : m_objectives) {
            if (other->status == OBJECTIVE_OPEN)
                return false;
        }
        return true;
    }
    return false;
}

// ---- the menu

C_GameMenu::C_GameMenu()
{
    m_presets.Load();
    if (IsLocalGame())
        m_missionItems.Setup();
}

C_GameMenu::~C_GameMenu()
{
    m_game = nullptr;
}

bool C_GameMenu::OnStart(void *)
{
    // Dropped: the copy protection's bookkeeping (the game window's handle and a value of it).
    m_hasPlayerSlotMember = g_teamManager.GetPlayerTeam()->HasMemberInPlayerSlots();
    m_options = new C_OptionsHandler(this);
    GetSoundModule()->SetMasterVolume(0.0f);
    if (!IsLocalGame()) {
        m_infoScreen = new C_InfoScreen();
        // Fix: dropped when it could not be made (the original crashed in Init).
        if (!m_infoScreen->Init(m_scene)) {
            delete m_infoScreen;
            m_infoScreen = nullptr;
        }
    }
    return true;
}

void C_GameMenu::OnStop()
{
    g_gameProfile.m_player->ApplyVolume();
    delete m_options;
    m_options = nullptr;
    if (m_popup) {
        delete m_popup;
        m_popup = nullptr;
    }
    if (!IsLocalGame() && m_infoScreen) {
        m_infoScreen->Done();
        delete m_infoScreen;
        m_infoScreen = nullptr;
    }
    m_panel.DeleteSoldier();
    // Fix: the stocks' pointers cleared (the original left them, deleted again by the next stop).
    for (C_ItemStock **stock : {&m_stock1E4, &m_stock1E8}) {
        if (*stock) {
            (*stock)->Reset();
            delete *stock;
            *stock = nullptr;
        }
    }
}

bool C_GameMenu::OnInput(float *, float *)
{
    if (m_game)
        m_game->HandleInput(nullptr, nullptr);
    return true;
}

void C_GameMenu::OnUpdate(int timeDelta)
{
    if (!IsLocalGame()) {
        m_panel.TickSoldier(timeDelta);
        UpdateLoadButton();
        UpdateNetStatus();
    }
    if ((g_gameRole & GAME_ROLE_CLIENT) && m_game)
        m_game->Update(timeDelta);
}

void C_GameMenu::OnTick(int timeDelta)
{
    g_gameMenuTime += timeDelta;
    if ((g_gameRole & GAME_ROLE_SERVER) && m_game)
        m_game->Tick(timeDelta);
}

void C_GameMenu::UpdateLoadButton()
{
    const S_SquadSoldier *soldier = m_panel.GetSquadSoldier();
    // (Not over: NaN neither.)
    if (soldier && soldier->uniform.typeId != -1 && soldier->load.GetWeightFill() <= 1.0f) {
        if (!m_loadButtonOn) {
            m_loadButtonOn = true;
            Post(LOAD_BUTTON, CONTROL_MSG_ACTIVATE, 0);
        }
    } else if (m_loadButtonOn) {
        m_loadButtonOn = false;
        Post(LOAD_BUTTON, CONTROL_MSG_DEACTIVATE, 0);
    }
}

void C_GameMenu::UpdateNetStatus()
{
    // (The original's messages carry stack leftovers as their value2 / value3 where it sets none.)
    if (g_gameProfile.IsCooperativeGame()) {
        S_NetPlayerState *players = g_netGame.LockPlayers();
        Post(COOP_PLAYERS, CONTROL_MSG_CLEAR, 0);
        Post(COOP_PLAYERS, CONTROL_MSG_ADD_TEXT_ID, COOP_TITLE_STYLE, TEXT_COOP_TITLE);
        for (int i = 0; i < g_netGame.GetPlayerCount(); ++i) {
            const S_NetPlayerState &player = players[i];
            if (!player.IsConnected())
                continue;
            const int rank = std::min(std::max(player.stats[PLAYER_RANK], 0), MAX_RANK);
            const C_str name = C_fstr("%s %s", g_texts.Get(TEXT_RANKS + rank).c_str(), player.name);
            Post(COOP_PLAYERS, CONTROL_MSG_ADD_TEXT, COOP_NAME_STYLE, 0, name);
            const uint32_t state = !player.human           ? TEXT_COOP_NO_HUMAN
                                   : player.human->IsDead() ? TEXT_COOP_DEAD
                                                            : TEXT_COOP_ALIVE;
            Post(COOP_PLAYERS, CONTROL_MSG_ADD_TEXT_ID, COOP_STATE_STYLE, state);
        }
        g_netGame.UnlockPlayers();
        return;
    }
    int axis = 0, allies = 0;
    if (g_mpSettings->gameType == MP_GAME_DEATHMATCH)
        CountUniformSides(&axis, &allies);
    else
        g_netGame.CountTeams(&axis, &allies, false, false);
    Post(ALLIES_COUNT, CONTROL_MSG_SET_NUMBER, static_cast<uint32_t>(allies));
    Post(AXIS_COUNT, CONTROL_MSG_SET_NUMBER, static_cast<uint32_t>(axis));
    if (g_mpSettings->gameType == MP_GAME_OCCUPATION)
        Post(RESPAWN_TIME, CONTROL_MSG_SET_RUN_TEXT, TIME_STYLE, TIME_RUN, FormatTime(::GetGame()->GetRespawnTimeLeft()));
    Post(ROUND_TIME, CONTROL_MSG_SET_RUN_TEXT, TIME_STYLE, TIME_RUN, FormatTime(::GetGame()->GetRoundTimeLeft()));
}

void C_GameMenu::OnMessage(S_ControlMessage *message)
{
    if (m_options->HandleMessage(message))
        return;
    std::vector<S_SquadSoldier *> &squad = g_missionSquad->states;
    switch (message->id & CONTROL_MSG_MASK) {
    case MENU_MSG_ESCAPE: {
        if (m_infoScreen && m_infoScreen->IsShown()) {
            m_infoScreen->Show(false);
            return;
        }
        C_Screen *top = g_menuScreens.empty() ? nullptr : g_menuScreens.back();
        if (top && m_startArg != WARMUP_MENU) {
            const uint32_t index = top->GetId() >> SCREEN_INDEX_SHIFT;
            if (index == OPTIONS_SCREEN || index == PLAYER_SCREEN) {
                Post(0, MENU_MSG_BACK, 0);
                return;
            }
        }
        if (IsLocalGame()) {
            C_MissionDef *mission = ::GetGame()->GetMission();
            if (!mission || !mission->AreObjectivesDecided())
                m_state = GAME_STATE_CLOSE_GAME_MENU;
            return;
        }
        if (!squad.empty()) {
            const int team = squad[0]->team;
            if (m_team != -1 && m_team != team) {
                if (m_teamChangePosted)
                    return;
                m_teamChangePosted = true;
                GetGameApp()->PostCommand(APP_NET_COMMAND_TEAM, team, 0, false);
                return;
            }
        }
        if (squad.size() == 2)
            DeleteSquadSoldier(squad, 1);
        m_state = GAME_STATE_CLOSE_GAME_MENU;
        return;
    }
    case GAME_MENU_MSG_APPLY_SOLDIER:
        if (squad.size() == 2)
            DeleteSquadSoldier(squad, 0);
        if (!m_hasPlayerSlotMember) {
            if (g_mpSettings->sessionType == 1 || g_mpSettings->sessionType == 2)
                GetGameApp()->PostCommand(APP_NET_COMMAND_0, 0, 0, false);
        } else if (m_respawnOnApply) {
            GetGameApp()->PostCommand(APP_NET_COMMAND_RESPAWN, 0, 0, false);
        }
        Post(0, MENU_MSG_ESCAPE, 0);
        return;
    case GAME_MENU_MSG_DROP_SOLDIER:
        if (squad.size() == 2)
            DeleteSquadSoldier(squad, 1);
        Post(0, MENU_MSG_ESCAPE, 0);
        return;
    case GAME_MENU_MSG_QUIT_QUESTION:
        ShowMessageId(MESSAGE_BOX_YES_NO, TEXT_QUIT_QUESTION, GAME_MENU_MSG_QUIT, 0);
        return;
    case GAME_MENU_MSG_RESTART_QUESTION:
        ShowMessageId(MESSAGE_BOX_YES_NO, TEXT_RESTART_QUESTION, GAME_MENU_MSG_RESTART, 0);
        return;
    case GAME_MENU_MSG_RESTART:
        m_state = GAME_STATE_CLOSE_GAME_MENU;
        GetGameApp()->m_pendingCommand = APP_COMMAND_RESTART;
        return;
    case GAME_MENU_MSG_QUICK_SAVE:
        m_state = GAME_STATE_CLOSE_GAME_MENU;
        GetGameApp()->m_pendingCommand = APP_COMMAND_QUICK_SAVE;
        return;
    case GAME_MENU_MSG_QUICK_LOAD:
        m_state = GAME_STATE_CLOSE_GAME_MENU;
        GetGameApp()->m_pendingCommand = APP_COMMAND_QUICK_LOAD;
        return;
    case GAME_MENU_MSG_QUIT:
        static_cast<C_game *>(m_game)->SetMode(GAME_MODE_QUIT);
        m_state = GAME_STATE_MENU_END_MISSION;
        return;
    case GAME_MENU_MSG_END_MISSION:
    case GAME_MENU_MSG_END_MISSION_2:
        m_state = GAME_STATE_MENU_END_MISSION;
        return;
    default:
        return;
    }
}

bool C_GameMenu::BuildScreens()
{
    S_MenuDesc *menu = new S_MenuDesc();
    menu->AddScreen("MAIN_INGAME_MENU", "main_ingame");
    menu->AddScreen("OPTIONS", "options");
    if (!IsLocalGame())
        menu->AddScreen("PLAYER_SCREEN", g_gameProfile.IsCooperativeGame() ? "player_screen_2" : "player_screen");
    for (S_ScreenDesc *desc : menu->screens) {
        I3D_frame *model = LoadModel(desc->name.c_str(), desc->file.c_str());
        if (!model)
            continue;
        const uint32_t index = desc->id >> SCREEN_ID_SHIFT;
        if (index == MAIN_SCREEN)
            BuildMainScreen(desc, model);
        if (index == OPTIONS_SCREEN)
            m_options->BuildScreen(desc, model);
        if (index == PLAYER_SCREEN)
            BuildPlayerScreen(desc, model);
        auto *screen = new C_Screen(m_scene, model);
        // (Quirk kept: a screen not built is leaked.)
        if (screen->Build(desc)) {
            screen->HandleMessages(desc->messages);
            m_screens.push_back(screen);
        }
    }
    delete menu;
    S_ControlMessage open;
    open.id = MENU_MSG_OPEN_SCREEN;
    open.value = !IsLocalGame() && m_startArg == WARMUP_MENU ? PLAYER_SCREEN : MAIN_SCREEN;
    HandleMessage(&open);
    return true;
}

bool C_GameMenu::BuildMainScreen(S_ScreenDesc *desc, I3D_frame *)
{
    const auto button = [desc](const char *name, uint32_t textId, uint32_t click, uint32_t value = 0) {
        S_ControlDesc *control = desc->AddControl(CONTROL_BUTTON, name);
        AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, textId);
        AddDescMessage(control, 0, click, value);
        return control;
    };
    if (IsLocalGame()) {
        // Dropped: the copy protection's message for the missions after the 7th.
        C_game *game = ::GetGame();
        S_ControlDesc *control = button("button06", 0x9DB, CLICK | GAME_MENU_MSG_END_MISSION_2);
        if (!game->GetMission()->IsTutorial())
            AddDescMessage(control, 0, CONTROL_MSG_HIDE, 0);
        // Back to the game, or (the objectives decided, not failed) the mission's end.
        C_MissionDef *mission = game->GetMission();
        if (mission && mission->AreObjectivesDecided() && !game->IsMissionFailed()) {
            button("button00", 0x9E1, CLICK | GAME_MENU_MSG_END_MISSION_2);
        } else {
            control = button("button00", 0x9CF, CLICK | MENU_MSG_ESCAPE);
            if (game->IsMissionFailed())
                AddDescMessage(control, 0, CONTROL_MSG_DEACTIVATE, 0);
        }
        button("button01", 0x9D3, CLICK | GAME_MENU_MSG_RESTART_QUESTION);
        control = button("button02", 0x9D0, CLICK | GAME_MENU_MSG_QUICK_SAVE);
        if (!game->CanQuickSave())
            AddDescMessage(control, 0, CONTROL_MSG_DEACTIVATE, 0);
        control = button("button03", 0x9D1, CLICK | GAME_MENU_MSG_QUICK_LOAD);
        if (!game->HasQuickSave() || g_gameProfile.GetDifficulty() == DIFFICULTY_NO_SAVES)
            AddDescMessage(control, 0, CONTROL_MSG_DEACTIVATE, 0);
        button("button04", 0x9D2, CLICK | OPTIONS_MSG_OPEN);
        button("button05", 0x9D4, CLICK | GAME_MENU_MSG_QUIT_QUESTION);
    } else {
        AddDescMessage(desc->AddControl(CONTROL_BUTTON, "button06"), 0, CONTROL_MSG_HIDE, 0);
        button("button00", 0x9D5, CLICK | MENU_MSG_ESCAPE);
        button("button01", 0x9D6, CONTROL_MSG_DEACTIVATE);
        button("button02", 0x9D7, CONTROL_MSG_DEACTIVATE);
        button("button03", 0x9D8, CLICK | OPTIONS_MSG_OPEN);
        button("button04", 0x9D9, CLICK | MENU_MSG_OPEN_SCREEN, PLAYER_SCREEN);
        button("button05", 0x9DA, CLICK | GAME_MENU_MSG_QUIT_QUESTION);
    }
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_h&dversion"), 0, CONTROL_MSG_SET_TEXT_ID, VERSION_STYLE,
                   TEXT_VERSION);
    return true;
}

bool C_GameMenu::BuildPlayerScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    BuildSoldiersPanel(desc, model);
    BuildEquipmentPanel(desc, model);
    BuildItemsPanel(desc, model);
    BuildSkillsPanel(desc, model);
    const auto self = reinterpret_cast<uintptr_t>(this);
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_SHOWN, self,
                     reinterpret_cast<uintptr_t>(&C_GameMenu::OnPlayerScreenShown));
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_HIDDEN, self,
                     reinterpret_cast<uintptr_t>(&C_GameMenu::OnPlayerScreenHidden));
    const auto addCallback = [self](S_ControlDesc *control, uint32_t notification, ControlCallback function) {
        AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, notification, self, reinterpret_cast<uintptr_t>(function));
    };
    const auto text = [desc](int type, const char *name, uint32_t style, uint32_t textId) {
        S_ControlDesc *control = desc->AddControl(type, name);
        AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, style, textId);
        return control;
    };

    // The soldier types (inactive until filled).
    S_ControlDesc *types = desc->AddControl(CONTROL_COMBO, "soldier_type");
    addCallback(types, CONTROL_NOTIFY_0E, &C_EquipmentMenuBase::CallItemNotify60);
    addCallback(types, CONTROL_NOTIFY_0B, &C_GameMenu::OnSoldierType);
    AddDescMessage(types, 0, CONTROL_MSG_SET_HEADER_TEXT_ID, LIST_STYLE, TEXT_SOLDIER_TYPES);
    AddDescMessage(types, 0, CONTROL_MSG_ADD_TEXT_ID, LIST_ITEM_STYLE, TEXT_SOLDIER_TYPES);
    AddDescMessage(types, 0, CONTROL_MSG_DEACTIVATE, 0);
    m_soldierTypes = static_cast<int>(types->id);
    UpdateSoldierTypes();
    // The presets' save and rename (the combo box's header edited; both inactive).
    S_ControlDesc *control = text(CONTROL_BUTTON, "bsave_preset", TEXT_STYLE, 0x9E3);
    addCallback(control, CONTROL_NOTIFY_CLICK, &C_GameMenu::OnSavePreset);
    AddDescMessage(control, 0, CONTROL_MSG_DEACTIVATE, 0);
    control = text(CONTROL_BUTTON, "brename_preset", TEXT_STYLE, 0x9E4);
    AddDescMessage(control, static_cast<uint32_t>(m_soldierTypes) | 2, CLICK | CONTROL_MSG_EDIT, 0);
    AddDescMessage(control, 0, CONTROL_MSG_DEACTIVATE, 0);
    if (g_gameProfile.IsCooperativeGame()) {
        text(CONTROL_TEXT, "text_h&dversion", VERSION_STYLE, TEXT_VERSION);
        text(CONTROL_TEXT, "text_equipment", TEXT_STYLE, 0x9E7);
    } else {
        // The warmup's text (shown in the occupation games) and the time's.
        control = text(CONTROL_TEXT, "text_warm_up", TIME_STYLE, 0x9E5);
        if (g_mpSettings->gameType != MP_GAME_OCCUPATION)
            AddDescMessage(control, 0, CONTROL_MSG_HIDE, 0);
        text(CONTROL_TEXT, "text_etimated_time", TIME_STYLE, 0x9E6);
    }
    control = text(CONTROL_BUTTON, "baccept", BUTTON_STYLE, 0x9C9);
    AddDescMessage(control, 0, CLICK | GAME_MENU_MSG_APPLY_SOLDIER, 0);
    AddDescMessage(control, 0, CONTROL_MSG_DEACTIVATE, 0);
    control = text(CONTROL_BUTTON, "bback", BUTTON_STYLE, 0x9CA);
    AddDescMessage(control, 0, CLICK | GAME_MENU_MSG_DROP_SOLDIER, 0);
    addCallback(desc->AddControl(CONTROL_CHAT, "console"), CONTROL_NOTIFY_SEND, &C_GameMenu::OnChat);
    desc->AddControl(CONTROL_LABEL, "text03");
    text(CONTROL_TEXT, "text_playerarrangement", 0xA, 0x9F7);
    if (!g_gameProfile.IsCooperativeGame()) {
        text(CONTROL_TEXT, "text_h&dversion", VERSION_STYLE, TEXT_VERSION);
        text(CONTROL_TEXT, "text_equipment", TEXT_STYLE, 0x9E7);
    }
    text(CONTROL_TEXT, "text_chat", TEXT_STYLE, 0x9E8);
    text(CONTROL_TEXT, "text_selected_soldier_stats", TEXT_STYLE, 0x9E9);
    text(CONTROL_TEXT, "text_draft", TEXT_STYLE, 0x9EA);
    control = text(CONTROL_BUTTON, "bgame_info", BUTTON_STYLE, 0x9CC);
    addCallback(control, CONTROL_NOTIFY_CLICK, &C_GameMenu::OnGameInfo);
    if (g_gameProfile.IsCooperativeGame()) {
        text(CONTROL_TEXT, "text_player_status", TEXT_STYLE, 0x4F4C);
        if (I3D_frame *popup = LoadModel("DESCRIPTION", "description")) {
            m_popup = new C_DescriptionScreen(m_scene, popup);
            m_popup->Build(nullptr);
        }
    } else if (model->FindChildFrame("text_weapons_info", FRAME_MASK_VISUAL)) {
        control = text(CONTROL_LABEL, "text_weapons_info", TEXT_STYLE, 0x7D2);
        m_weaponsInfo = static_cast<int>(control->id);
    }
    if (I3D_frame *view = model->FindChildFrame("3Dview", FRAME_MASK_VISUAL))
        m_panel.CreateSoldier(static_cast<I3D_visual *>(view));
    return true;
}

bool C_GameMenu::OnPlayerScreenHidden(void *, void *, C_Control *, uint32_t, uint32_t)
{
    return true;
}

bool C_GameMenu::OnGameInfo(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    static_cast<C_GameMenu *>(menu)->m_infoScreen->Show(true);
    return true;
}

bool C_GameMenu::OnChat(void *, void *, C_Control *, uint32_t, uint32_t value)
{
    const char *line = reinterpret_cast<const char *>(static_cast<uintptr_t>(value));
    bool team = false;
    if (std::strstr(line, "/t") == line) {
        team = true;
        line += 2;
    } else if (std::strstr(line, "/a") == line) {
        line += 2;
    }
    while (static_cast<unsigned char>(*line) < 0x80 && std::isspace(*line))
        ++line;
    g_netGame.SendChat(line, team);
    return true;
}

bool C_GameMenu::BuildSoldiersPanel(S_ScreenDesc *desc, I3D_frame *model)
{
    C_HumanStore *store = g_gameProfile.m_humanStore;
    if (store->GetCount() < 1)
        return false;
    I3D_frame *area = model->FindChildFrame("soldierspanel", FRAME_MASK_DUMMY);
    const S_vector zero(0.0f, 0.0f, 0.0f);
    I3D_frame *frameA = AddFrame(C_fstr("GB_Soldiers_a"), FRAME_DUMMY, area, zero);
    I3D_frame *frameX = AddFrame(C_fstr("GB_Soldiers_x"), FRAME_DUMMY, area, zero);
    I3D_frame *photo = model->FindChildFrame("photo03", FRAME_MASK_VISUAL);
    I3D_frame *selector = model->FindChildFrame("selector", FRAME_MASK_VISUAL);
    // (Quirk kept: the copy is not checked.)
    I3D_frame *selector2 = CloneFrame(C_fstr("selector2"), nullptr, selector);
    for (const char *name : {"table13", "table14", "table15"}) {
        if (I3D_frame *table = model->FindChildFrame(name, FRAME_MASK_VISUAL))
            table->SetOn(false);
    }
    // (The frames made are kept.)
    if (!photo || !frameA || !frameX || !selector)
        return false;
    selector->LinkTo(frameA, 0);
    selector2->LinkTo(frameX, 0);
    photo->LinkTo(frameA, 1);
    photo->SetOn(true);
    photo->Update();

    S_ControlDesc *panel = desc->AddControl(CONTROL_GROUP, "soldierspanel");
    AddDescMessage(panel->AddChild(CONTROL_SCROLL_BAR, "scroll11"), 0, CONTROL_MSG_SET_VALUE, SOLDIERS_LIST_A);
    const auto self = reinterpret_cast<uintptr_t>(this);
    // A list: hidden, its scroll bar, 4 shown across from the template's place (the list frees it).
    const auto list = [&](const char *name) {
        S_ControlDesc *list = panel->AddChild(CONTROL_HIDING_LIST, name);
        AddDescMessage(list, 0, CONTROL_MSG_HIDE, 0);
        AddDescMessage(list, 0, CONTROL_MSG_SET_VALUE, SOLDIERS_SCROLL);
        AddDescMessage(list, 0, CONTROL_MSG_SET_VISIBLE, SOLDIER_LIST_ROWS);
        AddDescMessage(list, 0, CONTROL_MSG_SET_ACROSS_STEP, 0, FloatValue(SOLDIER_ACROSS_STEP));
        auto *origin = static_cast<S_vector4 *>(_aligned_malloc(sizeof(S_vector4), 16));
        if (origin)
            std::memcpy(origin, &photo->GetPos(), sizeof(S_vector4));
        AddDescMessage(list, 0, CONTROL_MSG_SET_ORIGIN, 0, reinterpret_cast<uintptr_t>(origin));
        return list;
    };
    // A profile's picture: the template's copy under the list's frame, its index and face.
    const auto soldier = [&](S_ControlDesc *list, const char *format, int index, I3D_frame *frame) {
        const C_str name = C_fstr(format, index);
        CloneVisual(name, frame, photo);
        S_ControlDesc *picture = list->AddChild(CONTROL_PICTURE, name.c_str());
        AddDescMessage(picture, 0, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(index));
        AddDescMessage(picture, 0, CONTROL_MSG_SET_TEXTURE, 0, reinterpret_cast<uintptr_t>(store->Get(index)->faceImage));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CLICK, self,
                       reinterpret_cast<uintptr_t>(&C_GameMenu::OnSoldierPicked));
    };
    S_ControlDesc *listA = list("GB_Soldiers_a");
    for (int i = 0; i < store->GetCount() && i < SOLDIERS_PER_LIST; ++i)
        soldier(listA, "Soldier_a%02d", i, frameA);
    AddDescMessage(listA, 0, CONTROL_MSG_REFRESH, 0);
    S_ControlDesc *listX = list("GB_Soldiers_x");
    for (int i = SOLDIERS_PER_LIST; i < store->GetCount() && i < 2 * SOLDIERS_PER_LIST; ++i)
        soldier(listX, "Soldier_x%02d", i, frameX);
    AddDescMessage(listX, 0, CONTROL_MSG_REFRESH, 0);
    if (!g_gameProfile.IsCooperativeGame()) {
        for (const char *side : {"allies", "axis"})
            AddDescMessage(panel->AddChild(CONTROL_MARK, side), 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_0E, self,
                           reinterpret_cast<uintptr_t>(&C_GameMenu::OnSide));
    }
    photo->SetOn(false);
    photo->Update();
    return true;
}

bool C_GameMenu::BuildEquipmentPanel(S_ScreenDesc *desc, I3D_frame *model)
{
    I3D_frame *panel = model->FindChildFrame("equippanel", FRAME_MASK_DUMMY);
    I3D_frame *holsters = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    I3D_frame *backpack = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    // (The frames made are leaked without the panel's.)
    if (!panel || !holsters || !backpack)
        return false;
    panel->SetName("P_Equipment");
    const auto group = [panel](I3D_frame *frame, const char *name) {
        frame->LinkTo(panel, 0);
        frame->SetPos(S_vector(0.0f, 0.0f, 0.0f));
        frame->SetName(name);
    };
    group(holsters, "GB_Holsters");
    group(backpack, "GR_BackPack");
    for (I3D_frame *frame : {panel, holsters, backpack})
        m_models.push_back(frame);
    // The visuals the soldier's items are shown on get meshes of their own; the slots' visuals moved
    // under their groups, the scroll bars' and their texts' under the panel.
    const auto own = [panel](const char *name) {
        auto *visual = static_cast<I3D_object_visual *>(panel->FindChildFrame(name, FRAME_MASK_VISUAL));
        if (visual) {
            visual->DuplicateMesh();
            visual->GetVisualMesh()->Unknown18();
        }
        return visual;
    };
    const auto move = [panel](const char *name, I3D_frame *parent) {
        I3D_frame *frame = panel->FindChildFrame(name, FRAME_MASK_VISUAL);
        if (frame)
            frame->LinkTo(parent, 1);
        return frame;
    };
    for (const char *name : {"back", "head", "drop", "hands", "uniform", "shoulder"})
        own(name);
    // (Quirk kept: the texts are moved under a missing scroll bar too, unlinked.)
    I3D_frame *scroll = move("scroll13", panel);
    move("text_hlstrscap", scroll);
    move("hlcpct00", scroll);
    for (const char *name : {"item00", "item01", "item02", "item03"}) {
        if (I3D_frame *item = own(name))
            item->LinkTo(holsters, 1);
    }
    scroll = move("scroll14", panel);
    move("text_bkpckcap", scroll);
    move("hlcpct01", scroll);
    for (const char *name : {"item04", "item05", "item06", "item07"}) {
        if (I3D_frame *item = own(name))
            item->LinkTo(backpack, 1);
    }
    m_panel.SetMenu(this);
    m_panel.BuildDescription(desc, model);
    return true;
}

bool C_GameMenu::BuildItemsPanel(S_ScreenDesc *desc, I3D_frame *model)
{
    SetupStocks();
    I3D_frame *panel = model->FindChildFrame("allitemspanel", FRAME_MASK_DUMMY);
    I3D_frame *filter = model->FindChildFrame("soldier type01", FRAME_MASK_VISUAL);
    I3D_frame *scroll = model->FindChildFrame("bscroll", FRAME_MASK_DUMMY);
    if (!panel || !filter)
        return false;
    panel->SetName("P_visualstore");
    filter->SetName("filter");
    filter->LinkTo(panel, 1);
    // (Quirk kept: the scroll's dummy is not checked.)
    scroll->LinkTo(panel, 1);
    for (const char *name : {"item10", "item11", "item12", "item13", "item14", "item15", "selitem08", "selitem09",
                             "selitem10", "selitem11", "selitem12", "selitem13", "selitem14", "selitem15"}) {
        if (I3D_frame *frame = model->FindChildFrame(name, FRAME_MASK_VISUAL))
            frame->SetOn(false);
    }
    // The store's entries' template and its count's text.
    I3D_frame *entry = model->FindChildFrame("item08", FRAME_MASK_VISUAL);
    I3D_frame *count = model->FindChildFrame("item09", FRAME_MASK_VISUAL);
    if (!entry || !count)
        return false;
    entry->SetName("vi_template");
    count->LinkTo(entry, 1);
    count->SetOn(false);
    count->SetName("ammo");
    count->Update();
    m_storeList.SetMenu(this);
    m_storeList.BuildDescription(desc, model);
    return true;
}

bool C_GameMenu::BuildSkillsPanel(S_ScreenDesc *desc, I3D_frame *)
{
    // The skills' rows: their names' and values' titles, their bars (a skill's name is not used).
    static const char *const SKILLS[][3] = {
        {"text_shooting", "hlcpct07", "text_shooting01"},     {"text_strenght", "hlcpct08", "text_strenght01"},
        {"text_health", "hlcpct09", "text_health01"},         {"text_endurance", "hlcpct03", "text_endurance01"},
        {"text_stealth", "hlcpct04", "text_stealth01"},       {"text_first aid", "hlcpct05", "text_first aid01"},
        {"text_lockpick", "hlcpct06", "text_lockpick01"}};
    S_ControlDesc *panel = desc->AddControl(CONTROL_GROUP, "Panel02");
    AddDescMessage(panel, 0, CONTROL_MSG_IGNORE_INPUT, 0);
    for (int i = 0; i < 7; ++i) {
        S_ControlDesc *name = panel->AddChild(CONTROL_TEXT, SKILLS[i][0]);
        AddDescMessage(name, 0, CONTROL_MSG_SET_MARGIN, 0);
        AddDescMessage(name, 0, CONTROL_MSG_SET_TEXT_ID, TEXT_STYLE, TEXT_SKILLS + static_cast<uint32_t>(i));
        S_ControlDesc *bar = panel->AddChild(CONTROL_BAR, SKILLS[i][1]);
        AddDescMessage(bar, 0, CONTROL_MSG_SET_BAR_AXIS, BAR_AXIS_Z);
        AddDescMessage(bar, 0, CONTROL_MSG_SET_BAR_VALUE, 0, 0);
        S_ControlDesc *value = panel->AddChild(CONTROL_TEXT, SKILLS[i][2]);
        AddDescMessage(value, 0, CONTROL_MSG_SET_MARGIN, 0);
        AddDescMessage(value, 0, CONTROL_MSG_SET_TEXT_ID, TEXT_STYLE, TEXT_SKILL_VALUE);
    }
    return true;
}

void C_GameMenu::UpdateSoldierTypes()
{
    const uint32_t flags = TeamLoadouts(m_team);
    const auto types = static_cast<uint32_t>(m_soldierTypes);
    Post(types, CONTROL_MSG_CLEAR, 0);
    Post(types, CONTROL_MSG_SET_HEADER_TEXT_ID, LIST_STYLE, TEXT_SOLDIER_TYPES);
    Post(types, CONTROL_MSG_ADD_TEXT_ID, LIST_ITEM_STYLE, TEXT_SOLDIER_TYPES);
    // (Quirk kept: a name is the line's format.)
    const auto add = [types](const S_LoadoutPreset *preset) {
        if (preset->textId == -1)
            Post(types, CONTROL_MSG_ADD_TEXT, LIST_ITEM_STYLE, 0, C_fstr(preset->name));
        else
            Post(types, CONTROL_MSG_ADD_TEXT_ID, LIST_ITEM_STYLE, static_cast<uintptr_t>(preset->textId));
    };
    for (int i = 0; S_LoadoutPreset *preset = m_presets.GetUserPreset(i, flags); ++i)
        add(preset);
    m_mapPresets = 0;
    for (int i = 0; S_LoadoutPreset *preset = m_presets.GetPreset(i, flags); ++i) {
        const auto *mission = static_cast<C_MpMissionDef *>(::GetGame()->GetMission());
        if (!std::strstr(mission->GetPresets().c_str(), preset->name))
            continue;
        add(preset);
        ++m_mapPresets;
    }
}

void C_GameMenu::StorePreset(int index)
{
    S_LoadoutPreset *preset = m_presets.GetUserPreset(index, TeamLoadouts(m_team));
    if (!preset)
        return;
    const S_SquadSoldier *soldier = m_panel.GetSquadSoldier();
    preset->unknown1b4 = soldier->profileIndex;
    preset->ClearPlaces();
    const auto place = [](int &to, const S_SoldierItem &item) {
        if (item.typeId >= 0)
            to = item.typeId;
    };
    place(preset->head, soldier->head);
    place(preset->longGun, soldier->longGun);
    place(preset->hands, soldier->hands);
    place(preset->uniform, soldier->uniform);
    // The items counted by type (the last first; full: the others dropped).
    const auto count = [](S_PresetItem *items, int &n, int max, const std::vector<S_SoldierItem> &from) {
        for (auto it = from.rbegin(); it != from.rend(); ++it) {
            if (n >= max)
                continue;
            int i = 0;
            while (i < n && items[i].typeId != it->typeId)
                ++i;
            if (i < n) {
                items[i].count += it->present;
            } else {
                items[n].typeId = it->typeId;
                items[n].count = it->present;
                ++n;
            }
        }
    };
    count(preset->belt, preset->beltCount, PRESET_BELT_TYPES, soldier->belt);
    place(preset->backpack, soldier->backpack);
    count(preset->backpackItems, preset->backpackCount, PRESET_BACKPACK_TYPES, soldier->backpackContents);
}

bool C_GameMenu::OnSoldierType(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_GameMenu *>(menu);
    S_ControlMessage text;
    text.id = CONTROL_MSG_GET_TEXT;
    control->HandleMessage(&text);
    if (text.text.IsEmpty()) {
        Post(static_cast<uint32_t>(self->m_soldierTypes) | 2, CONTROL_MSG_EDIT, 0);
        return true;
    }
    if (self->m_userPreset >= 0) {
        S_LoadoutPreset *preset = self->m_presets.GetUserPreset(self->m_userPreset, TeamLoadouts(self->m_team));
        if (preset) {
            std::strncpy(preset->name, text.text.c_str(), PRESET_NAME_LENGTH);
            preset->name[PRESET_NAME_LENGTH] = 0;
            preset->textId = -1;
            self->StorePreset(self->m_userPreset);
            self->m_presets.SaveUser();
        }
    }
    return true;
}

bool C_GameMenu::OnSavePreset(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_GameMenu *>(menu);
    if (self->m_userPreset >= 0) {
        self->StorePreset(self->m_userPreset);
        S_LoadoutPreset *preset = self->m_presets.GetUserPreset(self->m_userPreset, TeamLoadouts(self->m_team));
        // A preset named by its text gets the profile's name and its number.
        if (preset && preset->textId != -1) {
            C_str name = g_gameProfile.m_player->GetProfileName();
            name += C_fstr("%d", self->m_userPreset + 1);
            preset->textId = -1;
            std::strncpy(preset->name, name.c_str(), PRESET_NAME_LENGTH);
            preset->name[PRESET_NAME_LENGTH] = 0;
            self->UpdateSoldierTypes();
            Post(static_cast<uint32_t>(self->m_soldierTypes), CONTROL_MSG_SET_HEADER_TEXT, LIST_STYLE, 0, name);
        }
    }
    self->m_presets.SaveUser();
    return true;
}

void C_GameMenu::ShowSoldierStats(int profile)
{
    int stats[12];
    std::copy(std::begin(DEFAULT_STATS), std::end(DEFAULT_STATS), stats);
    g_gameProfile.m_humanStore->GetStats(stats, profile, g_netGame.GetLocalPlayerStats()[PLAYER_RANK]);
    // A skill's row (1..7): its bar and its value's text.
    const auto row = [](int skill, float fill, const char *format, int value) {
        const uint32_t bar = SKILLS_PANEL | static_cast<uint32_t>(3 * skill - 1) << 12;
        Post(bar, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(fill));
        Post(bar + 0x1000, CONTROL_MSG_SET_TEXT, SKILL_VALUE_STYLE, 0, C_fstr(format, value));
    };
    const auto percent = [&stats](int stat) { return static_cast<float>(stats[stat]) * 0.01f; };
    row(1, percent(4), "%3d %%", stats[4]);
    row(2, percent(1), "%3d Kg", stats[1]);
    row(3, 1.0f, "%3d", stats[0]);
    row(4, percent(2), "%3d", stats[2]);
    row(5, percent(6), "%3d %%", stats[6]);
    row(6, percent(7), "%3d %%", stats[7]);
    row(7, percent(8), "%3d %%", stats[8]);
}

bool C_GameMenu::SetupStocks()
{
    g_itemsModule.EnsureItemsLoaded();
    m_stock1E4 = new C_ItemStock();
    m_stock1E8 = new C_ItemStock();
    for (int type = 0; type < STOCK_TYPES; ++type) {
        m_stock1E4->Set(type, -1);
        m_stock1E8->Set(type, -1);
    }
    g_gameProfile.m_itemStock18->Reset();
    // (The original copies the list.)
    const auto *mission = static_cast<C_MpMissionDef *>(::GetGame()->GetMission());
    const std::vector<S_CampaignItem> items = mission->GetItems();
    // A type's count in a stock, unless it has one.
    const auto add = [](C_ItemStock *stock, int type, int count) {
        if (stock->Get(type) < 1)
            stock->Set(type, count);
    };
    for (const S_CampaignItem &item : items) {
        const int type = item.itemId;
        C_ItemDef *def = type >= 0 && type < STOCK_TYPES ? GetItemDef(type) : nullptr;
        if (!def || def->GetTeam() == ITEM_TEAM_NONE)
            continue;
        const int team = def->GetTeam();
        int count = 1, second = -1, secondCount = 1;
        if (def->GetInventoryGroup() == STOCK_GROUP_BY_TEN) {
            count = 10;
            secondCount = 0;
        } else if (def->GetRecordType() == ITEM_ANIMATED && type != STOCK_NO_AMMO_TYPE) {
            second = def->GetAmmoItemId();
            secondCount = 10;
        } else if (def->GetBehaviour() == BEHAVIOUR_UNIFORM) {
            second = def->GetHeadgearItemId();
        } else {
            secondCount = 0;
        }
        if (item.count >= 0)
            count = item.count;
        if (item.ammo >= 0)
            secondCount = item.ammo;
        if (team != 0) {
            add(m_stock1E8, type, count);
            if (second >= 0)
                add(m_stock1E8, second, secondCount);
        }
        if (team != 1) {
            add(m_stock1E4, type, count);
            if (second >= 0)
                add(m_stock1E4, second, secondCount);
        }
    }
    return true;
}

void C_GameMenu::SetTeam(int team)
{
    if (team == 2)
        return;
    ShowMessage(-1, -1);
    // (Quirk kept: the first soldier read unchecked.)
    S_MissionSquad *squad = g_gameProfile.m_missionSquad;
    if (m_teamChangePosted) {
        S_SquadSoldier *first = squad->states.empty() ? nullptr : squad->states[0];
        if (team == first->team) {
            m_teamChangePosted = false;
            Post(0, MENU_MSG_ESCAPE, 0);
        }
    }
    if (team == m_team)
        return;
    if (!g_gameProfile.IsCooperativeGame()) {
        if (team == TEAM_ALLIES) {
            Post(ALLIES_COUNT, CONTROL_MSG_SET_CHECKED, 1);
            Post(AXIS_COUNT, CONTROL_MSG_SET_CHECKED, 0);
        } else if (team == TEAM_AXIS) {
            Post(AXIS_COUNT, CONTROL_MSG_SET_CHECKED, 1);
            Post(ALLIES_COUNT, CONTROL_MSG_SET_CHECKED, 0);
        } else {
            return;
        }
    }
    if (m_team < 0) {
        m_storeList.SetActive(true);
        Post(static_cast<uint32_t>(m_soldierTypes), CONTROL_MSG_ACTIVATE, 0);
    } else {
        Post(LOAD_BUTTON, CONTROL_MSG_DEACTIVATE, 0);
        m_panel.ReturnItems(m_storeList.GetStock());
    }
    m_storeList.ClearPages();
    m_team = team;
    m_panel.GetSquadSoldier()->team = team;
    if (m_team == TEAM_ALLIES)
        m_storeList.SetStock(m_stock1E4);
    else if (m_team == TEAM_AXIS)
        m_storeList.SetStock(m_stock1E8);
    ShowTeamSoldiers(m_team);
    TakeSoldierItems();
    UpdateSoldierTypes();
}

void C_GameMenu::ShowTeamSoldiers(int team)
{
    int profile = 0;
    if (team == TEAM_ALLIES) {
        Post(SOLDIERS_SCROLL, CONTROL_MSG_SET_VALUE, SOLDIERS_LIST_A);
        Post(SOLDIERS_LIST_A, CONTROL_MSG_SHOW, 0);
        Post(SOLDIERS_LIST_X, CONTROL_MSG_HIDE, 0);
        Post(SOLDIERS_LIST_A, CONTROL_MSG_SELECT_INDEX, static_cast<uint32_t>(g_pickedSoldierA));
        profile = g_pickedSoldierA;
    } else if (team == TEAM_AXIS) {
        Post(SOLDIERS_SCROLL, CONTROL_MSG_SET_VALUE, SOLDIERS_LIST_X);
        Post(SOLDIERS_LIST_A, CONTROL_MSG_HIDE, 0);
        Post(SOLDIERS_LIST_X, CONTROL_MSG_SHOW, 0);
        Post(SOLDIERS_LIST_X, CONTROL_MSG_SELECT_INDEX, static_cast<uint32_t>(g_pickedSoldierX - SOLDIERS_PER_LIST));
        profile = g_pickedSoldierX;
    }
    ShowSoldierStats(profile);
    m_panel.GetSquadSoldier()->SetProfile(profile, 0);
    m_panel.Refresh();
    m_panel.UpdateFace();
    m_panel.DressSoldier();
}

void C_GameMenu::TakeSoldierItems()
{
    S_SquadSoldier *soldier = m_panel.GetSquadSoldier();
    // A place's item the stock has not: dropped.
    const auto take = [this](S_SoldierItem &item) {
        if (item.typeId == -1 || m_storeList.TakeItem(item, false))
            return true;
        item.uid = -1;
        item.typeId = -1;
        item.present = 0;
        return false;
    };
    const auto takeAll = [this, soldier](std::vector<S_SoldierItem> &items, uint8_t slot) {
        for (size_t i = items.size(); i-- > 0;) {
            if (!m_storeList.TakeItem(items[i], false))
                soldier->RemoveItem(slot, items[i]);
        }
    };
    take(soldier->head);
    take(soldier->uniform);
    take(soldier->longGun);
    take(soldier->hands);
    takeAll(soldier->belt, SQUAD_SLOT_BELT);
    if (!take(soldier->backpack))
        soldier->backpackContents.clear();
    takeAll(soldier->backpackContents, SQUAD_SLOT_BACKPACK_CONTENTS);
    soldier->UpdateLoad();
}

bool C_GameMenu::OnPlayerScreenShown(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_GameMenu *>(menu);
    S_MissionSquad *squad = g_gameProfile.m_missionSquad;
    if (squad->states.empty()) {
        squad->AddSoldier();
        self->m_panel.SelectSoldier(0);
    } else if (!self->m_hasPlayerSlotMember) {
        self->m_panel.SelectSoldier(0);
    } else {
        // A player slot member edits a second soldier, at first the first's copy.
        if (squad->states.size() == 1) {
            squad->AddSoldier();
            if (squad->states.size() == 2)
                squad->states[1]->CopyFrom(*squad->states[0]);
        }
        self->m_panel.SelectSoldier(1);
    }
    const int team = self->m_panel.GetSquadSoldier()->team;
    if (team >= 0) {
        self->SetTeam(team);
        return true;
    }
    GetGameApp()->PostCommand(APP_NET_COMMAND_TEAM, g_gameProfile.IsCooperativeGame() ? TEAM_ALLIES : 2, 0, false);
    return true;
}

bool C_GameMenu::OnSoldierPicked(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_GameMenu *>(menu);
    S_ControlMessage index;
    index.id = CONTROL_MSG_GET_INDEX;
    control->HandleMessage(&index);
    const int profile = static_cast<int>(index.value);
    if (profile >= 0) {
        if (profile < SOLDIERS_PER_LIST)
            g_pickedSoldierA = profile;
        else if (profile < 2 * SOLDIERS_PER_LIST)
            g_pickedSoldierX = profile;
        ShowSoldierStats(profile);
        self->m_panel.GetSquadSoldier()->SetProfile(profile, 0);
        self->m_panel.Refresh();
        self->m_panel.UpdateFace();
    }
    return true;
}

bool C_GameMenu::OnSide(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    if (g_gameProfile.IsCooperativeGame())
        return false;
    int team = -1;
    if (control) {
        if (control->GetId() == ALLIES_COUNT)
            team = TEAM_ALLIES;
        else if (control->GetId() == AXIS_COUNT)
            team = TEAM_AXIS;
        else
            return false;
    }
    if (team != static_cast<C_GameMenu *>(menu)->m_team && team >= 0)
        GetGameApp()->PostCommand(APP_NET_COMMAND_TEAM, team, 0, false);
    return true;
}

bool C_GameMenu::OnRender()
{
    C_Screen *top = g_menuScreens.empty() ? nullptr : g_menuScreens.back();
    if (top) {
        const uint32_t index = top->GetId() >> SCREEN_INDEX_SHIFT;
        if (index == PLAYER_SCREEN)
            return RenderEquipment();
        // The message box over the player screen.
        if (index == MESSAGE_BOX_SCREEN && m_underPopup &&
            (m_underPopup->GetId() >> SCREEN_INDEX_SHIFT) == PLAYER_SCREEN)
            return RenderEquipment();
        return RenderGame();
    }
    if (m_game && m_game->GetTypeId() == GAME_STATE_TYPE_GAME) {
        static_cast<C_game *>(m_game)->GetScene()->Render(0);
        return true;
    }
    return false;
}

bool C_GameMenu::RenderGame()
{
    const int state5 = I3DGetDriver()->GetState(DRIVER_STATE_5);
    const int state6 = I3DGetDriver()->GetState(DRIVER_STATE_6);
    GetIGraph()->Clear(0, 1.0f, 3);
    if (!m_game || m_game->GetTypeId() != GAME_STATE_TYPE_GAME)
        return false;
    // The game's scene without its callback (the interface).
    I3D_scene *scene = static_cast<C_game *>(m_game)->GetScene();
    void *callback = scene->SetRenderCallback(nullptr, 0);
    scene->Render(0);
    scene->SetRenderCallback(callback, GAME_RENDER_PHASES);
    I3DGetDriver()->SetState(DRIVER_STATE_5, 0);
    I3DGetDriver()->SetState(DRIVER_STATE_6, 1);
    C_Screen *top = g_menuScreens.empty() ? nullptr : g_menuScreens.back();
    if (m_underPopup && top) {
        top->GetScreenFrame()->SetOn(false);
        m_underPopup->GetScreenFrame()->SetOn(true);
        m_scene->Render(0);
        top->GetScreenFrame()->SetOn(true);
        m_underPopup->GetScreenFrame()->SetOn(false);
        m_scene->Render(0);
        m_underPopup->GetScreenFrame()->SetOn(true);
    } else if (m_scene) {
        m_scene->Render(0);
    }
    I3DGetDriver()->SetState(DRIVER_STATE_5, state5);
    I3DGetDriver()->SetState(DRIVER_STATE_6, state6);
    return true;
}

bool C_GameMenu::RenderEquipment()
{
    if (m_scene)
        m_scene->Render(0);
    if (m_infoScreen && m_infoScreen->IsShown())
        return true;
    m_panel.RenderSoldier();
    if (m_popup && m_popup->HasPopup()) {
        // (Quirk kept: the top read unchecked when the stack is empty.)
        C_Screen *top = g_menuScreens.empty() ? nullptr : g_menuScreens.back();
        const int state5 = I3DGetDriver()->GetState(DRIVER_STATE_5);
        I3DGetDriver()->SetState(DRIVER_STATE_5, 0);
        top->GetScreenFrame()->SetOn(false);
        m_popup->GetScreenFrame()->SetOn(true);
        m_scene->Render(0);
        I3DGetDriver()->SetState(DRIVER_STATE_5, state5);
        top->GetScreenFrame()->SetOn(true);
        m_popup->GetScreenFrame()->SetOn(false);
    }
    return true;
}

bool C_GameMenu::OnDrawScreens()
{
    if (m_infoScreen && m_infoScreen->IsShown())
        m_infoScreen->Render();
    return false;
}

void C_GameMenu::ShowMessage(int textId, int)
{
    if (textId < 1) {
        CloseMessage();
        --g_gameMenuMessages;
        return;
    }
    if (!g_menuScreens.empty() &&
        (g_menuScreens.back()->GetId() >> SCREEN_INDEX_SHIFT & SCREEN_INDEX_MASK) == MESSAGE_BOX_SCREEN)
        return;
    ShowMessageId(0, static_cast<uint32_t>(textId), 0, 0);
    ++g_gameMenuMessages;
}
