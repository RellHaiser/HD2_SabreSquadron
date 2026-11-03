// The in-game menu (original 0x0067eb40..0x00687f50; part 9e-v: the class and the menu state's slots).
#include "game/game_menu.h"

#include "game/campaign.h"
#include "game/cursor.h"
#include "game/interface.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "game/sound_module.h"
#include "game/team.h"

int g_gameMenuTime;                                     // 0x008ae62c
int g_gameMenuMessages;                                 // 0x008ae628

namespace {

// A screen's index: its id's top 6 bits (the message box's 0).
constexpr int SCREEN_INDEX_SHIFT = 26;
constexpr uint32_t SCREEN_INDEX_MASK = 0x3F;
constexpr uint32_t MESSAGE_BOX_SCREEN = 0;

bool IsLocalGame()
{
    return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL;
}

} // namespace

// ---- the options handler

C_OptionsHandler::~C_OptionsHandler()
{
    if (m_cursor) {
        m_cursor->Release();
        m_cursor = nullptr;
    }
}

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
        UpdateCoopPlayers();
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
