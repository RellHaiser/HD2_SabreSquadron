// The in-game menu (original 0x0067eb40..0x00687f50; part 9e-v: the class and the menu state's slots;
// 9e-vi: the update's helpers and the drawing; 9e-vii: the messages; 9e-ix: the screens' descriptions).
#include "game/game_menu.h"

#include <algorithm>

#include "common/c_str.h"
#include "game/campaign.h"
#include "game/controls.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/human.h"
#include "game/interface.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "game/sound_module.h"
#include "game/team.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

int g_gameMenuTime;                                     // 0x008ae62c
int g_gameMenuMessages;                                 // 0x008ae628

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
// The main screen's controls: buttons (style 0xD) and the version's text (style 0x28).
constexpr int CONTROL_BUTTON = 0x14, CONTROL_TEXT = 0x13;
constexpr uint32_t BUTTON_STYLE = 0xD, VERSION_STYLE = 0x28;
constexpr uint32_t TEXT_VERSION = 0x7D6;
// What the buttons post (the menu's messages with bits 0x04001000).
constexpr uint32_t CLICK = 0x4001000;
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
