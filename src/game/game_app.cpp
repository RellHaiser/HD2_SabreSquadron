#include "game/game_app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "common/chunk.h"
#include "game/campaign.h"
#include "game/console.h"
#include "game/debriefing.h"
#include "game/equipment_screen.h"
#include "game/operation_end.h"
#include "game/session_menu.h"
#include "game/squad_menu.h"
#include "game/video_state.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/game_menu.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/resource_cache.h"
#include "game/sound_module.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/igraph.h"
#include "script/script_module.h"

namespace {

constexpr int SERVER_TICK = 100;                    // ms
// The sound is off this long after a load or a restart.
constexpr int VOLUME_DELAY = 100;                   // ms

// Texts (all_txt).
enum : int {
    // Ten per side (the allied ones first), picked by the player state's +0x54 (not identified;
    // shown before the player's name on the loading screen of a network map).
    TEXT_PLAYER_TITLES = 0x27A6,
    TEXT_JOINED_MAP = 0x92C,                        // the in-game menu of a client entering a map
};

// The campaign's last missions: loading a save made between missions goes on to the next
// one only before them (Sabre Squadron's mission 8 takes one step more).
constexpr int LAST_ADVANCE_MISSION_HD2 = 22;
constexpr int LAST_ADVANCE_MISSION_SS = 7;

// The number saved games keep (chunk 2999): Sabre Squadron's missions count on after the
// first campaign's.
int GetSavedMissionNumber()
{
    const int campaign = g_campaign->GetCurrentIndex();
    int mission = g_campaign->GetMissionNumber(campaign);
    if (campaign == 1)
        mission += g_campaign->CountMissions(0);
    return mission;
}

// Enters chunk 0x4E8C and its mission chunk (2998 or 2999).
bool EnterSavedMission(C_chunk &chunk)
{
    uint16_t id = 0xFFFF;
    if (!chunk.Descend(id) || id != CHUNK_SAVED_GAME || chunk.RemainingSize() <= 0)
        return false;
    return chunk.Descend(id) && (id == CHUNK_SAVED_MISSION_OLD || id == CHUNK_SAVED_MISSION);
}

// Three cleared frames before a load or a restart.
void ClearScreens()
{
    for (int i = 0; i < 3; ++i) {
        GetIGraph()->Clear(0, 1.0f, 3);
        GetIGraph()->Flip();
    }
}

// The diary records nothing while a saved game is written / read or the mission restarts.
C_Diary *GetDiary()
{
    return g_gameProfile.GetPlayerProfile()->GetDiary();
}

// The campaign's next (`back`: previous) step; a Lone Wolf campaign has no squad screens of
// kind 1.
C_CampaignNode *FindNextStep(bool back)
{
    C_CampaignNode *step = back ? g_campaign->PreviousScreen() : g_campaign->NextStep();
    if (g_playMode && g_playMode->mode == PLAY_MODE_LONE_WOLF && step &&
        step->GetType() == CAMPAIGN_NODE_SCREEN && static_cast<C_CampaignScreen *>(step)->GetKind() == 1)
        step = back ? g_campaign->PreviousScreen() : g_campaign->NextStep();
    return step;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction and the start

C_GameApp::C_GameApp()                                  // 0x006c0a70
{
    InitializeCriticalSection(&m_commandLock);
}

C_GameApp::~C_GameApp()                                 // 0x006c0ab0
{
    DeleteCriticalSection(&m_commandLock);
    delete m_state;
    m_state = nullptr;
}

bool C_GameApp::Init(bool resume)                       // 0x006c0b40
{
    m_serverTime = 0;
    m_resume = resume;
    if (IsStateActive())
        return false;
    if (g_gameRole & GAME_ROLE_CLIENT)
        GetIGraph()->UnknownDC(1);
    if (!m_resume) {
        if (!g_gameProfile.Init())
            return false;
        g_gameProfile.SelectLastPlayer();
        StartIntro();
    } else {
        m_state->Start(nullptr);
    }
    const int high = std::rand();
    m_randomId = static_cast<uint32_t>(high << 16 | std::rand());
    return true;
}

bool C_GameApp::StartFromCommandLine()                  // 0x006c0e10
{
    static const char CMD_SWITCH[] = " -cmd ";
    static const char SCRIPT_SWITCH[] = " -c";
    const char *commandLine = GetCommandLineA();
    m_state = nullptr;
    if (const char *command = std::strstr(commandLine, CMD_SWITCH)) {
        // (The console state copies at most 0x204 chars; nothing limits the command here.)
        auto *console = new C_ConsoleState(command + std::strlen(CMD_SWITCH));
        m_state = console;
        console->Start(nullptr);
        return true;
    }
    const char *script = std::strstr(commandLine, SCRIPT_SWITCH);
    if (!script)
        return false;
    // The console alone: the window is hidden.
    GetIGraph()->UnknownDC(0);
    ShowWindow(GetIGraph()->GetHWND(), SW_HIDE);
    script += std::strlen(SCRIPT_SWITCH);
    char command[0x108];
    // Fix: the original's sprintf had no bound (a 264-byte buffer).
    std::snprintf(command, sizeof(command), "exec %s", script);
    // Fix: the original read the word into 32 bytes, whatever its length.
    std::vector<char> token(std::strlen(script) + 1);
    int unknown = 0;
    const bool hasScript = ReadConsoleToken(&script, token.data(), &unknown, nullptr) == 0;
    auto *console = new C_ConsoleState(hasScript ? command : nullptr);
    m_state = console;
    console->Start(nullptr);
    console->ShowBanner();
    return true;
}

bool C_GameApp::StartIntro()                            // 0x006c0fc0
{
    if (StartFromCommandLine())
        return false;
    auto *movie = new C_MovieState();
    m_state = movie;
    movie->Play("Logo1.bik", 1);
    return true;
}

void C_GameApp::Shutdown()                              // 0x006c0bf0
{
    if (!IsLocalGame()) {
        GetGameApp()->SendGameMessage(GAME_MSG_SESSION, 0, 0, 0, true);
        g_netGame.Process(1000, false);
    }
    Close();
}

bool C_GameApp::Close()                                 // 0x006c0c40
{
    m_state->Stop();
    // (Inline in the original.)
    g_soundCache.Clear();
    g_modelCache.Clear();
    g_animationCache.Clear();
    g_shadowCache.Clear();
    g_versionModelCache.Clear();
    if (!m_resume) {
        delete m_state;
        m_state = nullptr;
    }
    if (g_gameRole & GAME_ROLE_CLIENT)
        GetIGraph()->UnknownE4();
    return true;
}

// ---------------------------------------------------------------------------
// The states

bool C_GameApp::StartStep(C_CampaignNode *step, int /*unused*/)   // 0x006c1210
{
    if (!step) {
        if (m_state) {
            m_state->Stop();
            delete m_state;
        }
        auto *menu = new C_MainMenu();
        m_state = menu;
        menu->Start(nullptr);
        return true;
    }
    C_GameState *next = nullptr;
    switch (step->GetType()) {
    case CAMPAIGN_NODE_VIDEO:
        m_state->Stop();
        delete m_state;
        next = new C_VideoState();
        break;
    case CAMPAIGN_NODE_SCREEN:
        m_state->Stop();
        delete m_state;
        switch (static_cast<C_CampaignScreen *>(step)->GetKind()) {
        case 0:
            next = new C_Kind0Menu();
            break;
        case 1:
        case 2:
            next = new C_SquadMenu();
            break;
        case C_CampaignScreen::KIND_EQUIPMENT:
        case 4:
            next = new C_EquipmentMenu();
            break;
        case 5:
            next = new C_DebriefingMenu();
            break;
        case 6:
            next = new C_OperationEndMenu();
            break;
        default:
            // Fix: the original started the state it had just deleted again.
            next = new C_MainMenu();
            m_state = next;
            return next->Start(nullptr);
        }
        break;
    case CAMPAIGN_NODE_PLAY: {
        m_state->Stop();
        delete m_state;
        // Dropped: the copy protection sent a campaign whose check failed back to its start
        // (from its third mission on: the campaign files again, the squad's items gone).
        auto *game = new C_game();
        m_state = game;
        return game->Start(g_campaign->GetCurrentMission());
    }
    default:
        return true;
    }
    m_state = next;
    return next->Start(step);
}

bool C_GameApp::StartNextStep(bool back)
{
    // (The commands queued in this update go out first.)
    m_updatePhase = 0;
    EndClientUpdate(false);
    return StartStep(FindNextStep(back), 0);
}

void C_GameApp::CloseGameMenu()
{
    C_GameState *menu = m_state;
    m_state = static_cast<C_GameMenu *>(menu)->GetGame();
    menu->Stop();
    delete menu;
}

C_game *C_GameApp::GetRunningGame() const
{
    C_GameState *state = m_state;
    if (state->GetTypeId() == GAME_STATE_TYPE_GAME_MENU)
        state = static_cast<C_GameMenu *>(state)->GetGame();
    if (!state || state->GetTypeId() != GAME_STATE_TYPE_GAME)
        return nullptr;
    return static_cast<C_game *>(state);
}

// ---------------------------------------------------------------------------
// The frame

bool C_GameApp::UpdateServer(int timeDelta)             // 0x006c3420
{
    m_serverTime += timeDelta;
    if (m_serverTime < SERVER_TICK)
        return true;
    // (One tick per frame; the remainder catches up in the next frames.)
    m_serverTime -= SERVER_TICK;
    m_updatePhase = 2;
    if (m_state->GetState() == GAME_STATE_QUIT)
        return EndServerUpdate(false);
    if (IsStateActive() && !m_suspended)
        m_state->Tick(SERVER_TICK);
    return EndServerUpdate(true);
}

bool C_GameApp::EndServerUpdate(bool result)            // 0x006c33e0
{
    // The end of the server's update for the clients.
    if ((g_gameRole & GAME_ROLE_SERVER) && !IsLocalGame())
        SendGameMessage(GAME_MSG_SESSION, 0, 0, 0, true);
    m_updatePhase = 0;
    return result;
}

bool C_GameApp::EndClientUpdate(bool result)            // 0x006c23e0
{
    if ((g_gameRole & GAME_ROLE_CLIENT) && !IsLocalGame())
        PostCommand(0, 0, 0, true);
    m_updatePhase = 0;
    return result;
}

void C_GameApp::HandleInput()                           // 0x006c34f0
{
    float *held = nullptr;
    float *pressed = nullptr;
    const int heldCount = g_controls.GetValues(&held, false);
    const int pressedCount = g_controls.GetValues(&pressed, true);
    if (pressedCount >= 0 && heldCount >= 0)
        m_state->HandleInput(held, pressed);
}

void C_GameApp::RenderState()                           // 0x006c34b0
{
    if (IsStateActive())
        m_state->Render();
}

bool C_GameApp::UpdateClient(int timeDelta)             // 0x006c2410
{
    m_updatePhase = 1;
    if (m_volumeDelay > 0) {
        m_volumeDelay -= timeDelta;
        if (m_volumeDelay < 1) {
            m_volumeDelay = 0;
            g_gameProfile.GetPlayerProfile()->ApplyVolume();
        }
    }
    if (m_state->GetState() == GAME_STATE_QUIT)
        return EndClientUpdate(false);
    if (!IsStateActive())
        return EndClientUpdate(true);
    if (m_suspended)
        return EndClientUpdate(true);

    HandleInput();
    m_state->Update(timeDelta);
    m_state->CommitPendingState();

    switch (m_state->GetState()) {
    case GAME_STATE_ENDED:
        if (!m_resume)
            return StartNextStep(false);
        Close();
        return EndClientUpdate(false);

    case GAME_STATE_BACK:
        if (!m_resume)
            return StartNextStep(true);
        Close();
        return EndClientUpdate(false);

    case GAME_STATE_QUIT:
        Close();
        g_netGame.Close(true);
        return EndClientUpdate(false);

    case GAME_STATE_HOST_SESSION:
    case GAME_STATE_JOIN_SESSION: {
        const bool host = m_state->GetState() == GAME_STATE_HOST_SESSION;
        m_state->Stop();
        delete m_state;
        auto *menu = new C_SessionMenu();
        menu->Start(nullptr);
        m_state = menu;
        g_mpSettings->sessionType = host ? SESSION_HOSTING : SESSION_JOINING;
        break;
    }

    case GAME_STATE_NEXT_MAP:
        StartNextMap();
        break;

    case GAME_STATE_MAIN_MENU: {
        m_state->Stop();
        delete m_state;
        auto *menu = new C_MainMenu();
        m_state = menu;
        menu->Start(nullptr);
        break;
    }

    case GAME_STATE_LOBBY:
        if (m_state && m_state->GetTypeId() == GAME_STATE_TYPE_GAME_MENU)
            CloseGameMenu();
        if (m_state && m_state->GetTypeId() == GAME_STATE_TYPE_GAME) {
            // (The mission ends as from the in-game menu.)
            static_cast<C_game *>(m_state)->EndMission(100);
            if (g_teamManager.GetPlayerTeam()->GetActivePlayer())
                GetCameraModule()->SetSpecialMode(2, true, nullptr);
        } else {
            m_state->Stop();
            delete m_state;
            auto *menu = new C_MainMenu();
            m_state = menu;
            menu->Start(nullptr);
        }
        break;

    case GAME_STATE_PLAY_AVI: {
        auto *movie = new C_MovieState();
        movie->SetReturnState(m_state);
        if (m_state->GetTypeId() == GAME_STATE_TYPE_MAIN_MENU) {
            const C_str &name = static_cast<C_MainMenu *>(m_state)->GetMovie();
            movie->Play(name.IsEmpty() ? "credits.bik" : name.c_str(), 0);
        } else if (m_state->GetTypeId() == GAME_STATE_TYPE_GAME) {
            movie->Play(static_cast<C_game *>(m_state)->GetAviName(), 0);
        }
        m_state = movie;
        m_state->SetState(GAME_STATE_RUNNING);
        break;
    }

    case GAME_STATE_MOVIE_FINISHED:
        if (!OnMovieFinished())
            return EndClientUpdate(false);
        break;

    case GAME_STATE_GAME_MENU:
        if (m_state && m_state->GetTypeId() == GAME_STATE_TYPE_GAME) {
            auto *game = static_cast<C_game *>(m_state);
            if (IsLocalGame()) {
                m_gamePaused = game->IsPaused();
                game->SetPaused(true);
            }
            m_state = game->OpenGameMenu(-1);
        }
        break;

    case GAME_STATE_CLOSE_GAME_MENU:
        CloseGameMenu();
        m_state->SetState(GAME_STATE_RUNNING);
        static_cast<C_game *>(m_state)->SetPlayerInputEnabled(true);
        GetCameraModule()->SetSpecialMode(3, true, 1);
        if (m_state && m_state->GetTypeId() == GAME_STATE_TYPE_GAME && IsLocalGame())
            static_cast<C_game *>(m_state)->SetPaused(m_gamePaused);
        break;

    case GAME_STATE_MENU_END_MISSION:
        CloseGameMenu();
        static_cast<C_game *>(m_state)->EndMission(100);
        if (g_teamManager.GetPlayerTeam()->GetActivePlayer())
            GetCameraModule()->SetSpecialMode(2, true, nullptr);
        break;

    case GAME_STATE_MP_GAME_MENU:
        if (m_state && m_state->GetTypeId() == GAME_STATE_TYPE_GAME && !IsLocalGame())
            m_state = static_cast<C_game *>(m_state)->OpenGameMenu(-2);
        break;

    default:
        break;
    }
    return EndClientUpdate(true);
}

bool C_GameApp::OnMovieFinished()
{
    if (m_state->GetTypeId() != GAME_STATE_TYPE_VIDEO)
        return true;
    auto *movie = static_cast<C_MovieState *>(m_state);
    switch (movie->GetSequence()) {
    case 0:
        // Back to the state the movie played over.
        m_state = movie->GetReturnState();
        movie->Stop();
        delete movie;
        m_state->SetState(GAME_STATE_RUNNING);
        break;
    case 1:
        movie->Stop();
        movie->Play("Logo2.bik", 2);
        break;
    case 2:
        movie->Stop();
        movie->Play("Logo3.bik", 3);
        break;
    case 3:
        movie->Stop();
        movie->Play("Intro.bik", 4);
        break;
    case 4:
        StartStep(nullptr, 0);
        break;
    case 5:
        movie->Stop();
        movie->Play("Logo1.bik", 1);
        break;
    case 6:
        Close();
        g_netGame.Close(true);
        return false;
    default:
        break;
    }
    return true;
}

void C_GameApp::StartNextMap()
{
    if (g_mpSettings->sessionType == 0)
        return;
    if (m_state->GetTypeId() == GAME_STATE_TYPE_GAME_MENU)
        CloseGameMenu();
    m_state->Stop();
    delete m_state;
    if (!IsLocalGame()) {
        g_netGame.FlipSessionParity();
        g_netGame.ResetPlayerStats();
        g_netGame.ResetReplication(true);
    }
    auto *game = new C_game();
    m_state = game;
    g_campaign->SetupMultiplayerMission();
    C_MissionDef *mission = g_campaign->GetCurrentMission();

    // The players on the loading screen.
    S_NetPlayerState *players = g_netGame.LockPlayers();
    for (int i = 0; i < g_netGame.GetPlayerCount(); ++i) {
        const S_NetPlayerState &player = players[i];
        if (!player.IsConnected())
            continue;
        const C_str title = g_texts.Get(TEXT_PLAYER_TITLES + (player.team != 1 ? 10 : 0) + player.stats[5]);
        char line[256];
        // Fix: the original's sprintf had no bound.
        std::snprintf(line, sizeof(line), "%s %s", title.c_str(), player.name);
        g_loaderScreen.AddPlayer(line, player.team);
    }
    g_netGame.UnlockPlayers();

    if (!game->Start(mission)) {
        game->Stop();
        delete game;
        auto *menu = new C_MainMenu();
        m_state = menu;
        menu->Start(nullptr);
        return;
    }
    m_gamePaused = game->IsPaused();
    if (!g_mpSettings->dedicated) {
        GetCameraModule()->SetSpecialMode(2, true, 1);
        m_state = game->OpenGameMenu(-2);
        if (!IsLocalGame() && !(g_gameRole & GAME_ROLE_SERVER))
            ShowGameMenuMessage(TEXT_JOINED_MAP);
    }
}

// ---------------------------------------------------------------------------
// The commands and the saved games

void C_GameApp::RestartMission()                        // 0x006c4200
{
    ClearScreens();
    C_game *game = GetRunningGame();
    if (!game)
        return;
    GetSoundModule()->SetMasterVolume(0.0f);
    g_gameProfile.ResetSession();
    GetDiary()->locked = true;
    game->RestartMission(true);
    GetDiary()->locked = false;
    m_volumeDelay = VOLUME_DELAY;
}

bool C_GameApp::SaveCommand()                           // 0x006c3c20
{
    bool ok;
    if (m_pendingCommand == APP_COMMAND_SAVE)
        ok = SaveGame(m_name, true);
    else
        ok = SaveGame(g_gameProfile.GetQuickSaveFileName(), true);
    m_name = "";
    return ok;
}

bool C_GameApp::LoadCommand()                           // 0x006c3550
{
    GetSoundModule()->SetMasterVolume(0.0f);
    bool ok;
    if (m_pendingCommand == APP_COMMAND_LOAD)
        ok = LoadGame(m_name, true);
    else
        ok = LoadGame(g_gameProfile.GetQuickSaveFileName(), true);
    m_volumeDelay = VOLUME_DELAY;
    m_name = "";
    return ok;
}

void C_GameApp::RequestLoad(const C_str &fileName)      // 0x006c42b0
{
    m_name = fileName;
    if (!m_name.IsEmpty())
        m_pendingCommand = APP_COMMAND_LOAD_FROM_MENU;
}

bool C_GameApp::SaveGame(const C_str &fileName, bool inGame)   // 0x006c3d20
{
    if (!inGame) {
        C_chunk chunk;
        if (!chunk.Open(fileName, true))
            return false;
        const bool ok = g_gameProfile.SaveGameChunk(chunk);
        chunk.Close();
        g_gameProfile.SetLastSave(fileName);
        return ok;
    }
    if (g_scriptModule.IsCutsceneRunning())
        return false;
    C_game *game = GetRunningGame();
    if (!game)
        return false;
    C_chunk chunk;
    if (!chunk.Open(fileName, true))
        return false;
    const int campaign = g_campaign->GetCurrentIndex();
    int mission = g_campaign->GetMissionNumber(campaign);
    if (mission < 0)
        return false;
    if (campaign == 1)
        mission += g_campaign->CountMissions(0);
    chunk.Begin(CHUNK_SAVED_GAME);
    chunk.WriteChunk(CHUNK_SAVED_MISSION, mission);
    g_gameProfile.SaveGameChunk(chunk);
    GetDiary()->locked = true;
    const bool ok = game->SaveGame(chunk);
    GetDiary()->locked = false;
    chunk.Ascend();
    chunk.Close();
    g_gameProfile.SetLastSave(fileName);
    return ok;
}

bool C_GameApp::LoadGame(const C_str &fileName, bool inGame)   // 0x006c3660
{
    // Dropped: the copy protection's bookkeeping at the start.
    if (!inGame) {
        C_chunk chunk;
        if (!chunk.Open(fileName, false))
            return false;
        g_gameProfile.ResetSession();
        g_gameProfile.LoadGameChunk(chunk);
        return true;
    }
    ClearScreens();
    if (g_scriptModule.IsCutsceneRunning())
        return false;
    C_game *game = GetRunningGame();
    if (!game)
        return false;
    C_chunk chunk;
    if (!chunk.Open(fileName, false) || !EnterSavedMission(chunk))
        return false;
    const int expected = GetSavedMissionNumber();
    int saved = 0;
    chunk.Read(saved);
    chunk.Ascend();
    // Only a saved game of the mission played.
    if (saved != expected)
        return false;
    g_gameProfile.ResetSession();
    g_gameProfile.LoadGameChunk(chunk);
    GetDiary()->locked = true;
    game->RestartMission(false);
    game->LoadGame(chunk);
    GetDiary()->locked = false;
    return true;
}

bool C_GameApp::LoadFromMenu()                          // 0x006c43a0
{
    // Dropped: the copy protection's bookkeeping at the start.
    if (m_state->GetTypeId() != GAME_STATE_TYPE_MAIN_MENU || m_pendingCommand != APP_COMMAND_LOAD_FROM_MENU)
        return false;
    const C_str fileName = m_name.IsEmpty() ? g_gameProfile.GetLastSavePath() : m_name;
    m_name = "";
    if (fileName.IsEmpty())
        return false;
    const bool quick = std::strstr(fileName, "quick") != nullptr;
    C_chunk chunk;
    if (!chunk.Open(fileName, false))
        return false;
    if (quick) {
        // (The mission number is not checked.)
        if (!EnterSavedMission(chunk))
            return false;
        chunk.Ascend();
    }
    g_gameProfile.ResetSession();
    g_gameProfile.LoadSavedGame(chunk);
    if (quick) {
        // Straight into the mission.
        m_state->Stop();
        delete m_state;
        auto *game = new C_game();
        m_state = game;
        GetDiary()->locked = true;
        game->LoadSavedGame(chunk);
        GetDiary()->locked = false;
        return true;
    }
    // A save between missions: the campaign goes on with the next mission.
    const int campaign = g_campaign->GetCurrentIndex();
    const int mission = g_campaign->GetMissionNumber(campaign);
    if ((campaign == 0 && mission <= LAST_ADVANCE_MISSION_HD2) ||
        (campaign == 1 && mission <= LAST_ADVANCE_MISSION_SS))
        g_campaign->AdvanceToMission();
    else if (campaign == 1 && mission == LAST_ADVANCE_MISSION_SS + 1)
        g_campaign->NextStep();
    return StartStep(FindNextStep(false), 0);
}

bool C_GameApp::HasQuickSave()                          // 0x006c4f10
{
    if (!m_state || m_state->GetTypeId() != GAME_STATE_TYPE_GAME)
        return false;
    C_chunk chunk;
    if (!chunk.Open(g_gameProfile.GetQuickSaveFileName(), false) || !EnterSavedMission(chunk))
        return false;
    int saved = 0;
    chunk.Read(saved);
    chunk.Ascend();
    return saved == GetSavedMissionNumber();
}

bool C_GameApp::GetQuickSaveMission(const C_str &fileName, int *mission)   // 0x006c4b00
{
    if (fileName.IsEmpty() || !std::strstr(fileName, "quick"))
        return false;
    C_str path = g_gameProfile.GetProfileDirectory();
    path += fileName;
    C_chunk chunk;
    if (!chunk.Open(path, false))
        return false;
    uint16_t id = 0xFFFF;
    chunk.Descend(id);
    if (chunk.RemainingSize() <= 0)
        return false;
    // (Any chunk id above 0x4E8B passes.)
    bool ok = id >= CHUNK_SAVED_GAME;
    chunk.Descend(id);
    if (id != CHUNK_SAVED_MISSION_OLD && id != CHUNK_SAVED_MISSION)
        ok = false;
    // (The original counts the first campaign's missions and does not use the count; a short
    // read left part of the chunk header in `*mission`.)
    int saved = 0;
    chunk.Read(saved);
    chunk.Ascend();
    if (mission)
        *mission = saved;
    return ok;
}

// ---------------------------------------------------------------------------

void C_GameApp::PrintMessage(const char *text)          // 0x006c4e80
{
    if (!g_mpSettings->dedicated) {
        const bool console = m_state && m_state->GetTypeId() == GAME_STATE_TYPE_CONSOLE &&
                             static_cast<C_ConsoleState *>(m_state)->IsShown();
        if (!console) {
            if (GetGame()) {
                // Fix: the original passed the message as the format.
                GetGame()->PrintMessage(MESSAGE_WHITE, "%s", text);
            }
            return;
        }
    }
    GetConsole()->AddLine(text);
}
