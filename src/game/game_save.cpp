// C_game, part 3 (original source 0x0069c550..0x0069d970): restarting the mission and the
// saved games (chunk 0x29A).
#include <cstring>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/first_person_view.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/human_stains.h"
#include "game/interface.h"
#include "game/vehicle_spawn.h"
#include "game/world.h"

namespace {

// Chunks of the game's part of a saved game.
enum : uint16_t {
    CHUNK_GAME = 0x29A,
    CHUNK_GAME_VERSION = 1000,                      // int 2
    CHUNK_GAME_MISSION = 2000,                      // the mission's name (with the terminator)
    CHUNK_GAME_HUD = 3000,
    CHUNK_GAME_INTERFACE = 4000,
    CHUNK_GAME_MODULES = 5000,
    CHUNK_GAME_TIMED_EVENTS = 6000,
    CHUNK_GAME_CHECKPOINTS = 8000,
    CHUNK_GAME_STATE = 9000,                        // S_SavedGameState
    CHUNK_GAME_QUICK_MESSAGES = 0x20D0,
    CHUNK_GAME_MISSION_DEF = 0x2134,
    CHUNK_GAME_SAVES_USED = 0x238C,
    CHUNK_GAME_SIGNAL_PISTOL = 0x23F0,
};
constexpr int SAVE_VERSION = 2;

// The game's clock and switches in a saved game (0x18 bytes).
struct S_SavedGameState {
    int fadeTime;
    int endDelay;
    uint32_t missionTime;
    uint32_t time;
    bool playerInputEnabled;                        // +0x10
    bool missionFailed;
    bool uniformDisguise;
    bool tacticalMode;
    bool noTeamDamage;                              // +0x14
    uint8_t padding[3];
};
static_assert(sizeof(S_SavedGameState) == 0x18, "S_SavedGameState layout");

// Kept between saving and loading (0x008ae69c): a too short chunk 9000 loads the values
// last saved / loaded.
S_SavedGameState g_savedGameState;

// The squad chosen in the menus appears at the spawn points.
void SpawnMissionSquad()
{
    std::vector<S_SquadSoldier *> &squad = g_missionSquad->states;
    for (size_t i = 0; i < squad.size(); ++i) {
        if (!squad[i]->deployed)
            continue;
        C_Actor *spawnPoint = g_actorsModule.FindSpawnPoint(0);
        if (!spawnPoint)
            break;
        static_cast<C_SpawnPoint *>(spawnPoint)->Spawn(0, squad[i], nullptr);
    }
}

} // namespace

bool C_game::CanQuickSave() const                   // 0x0069c550
{
    if (AreObjectivesDecided())
        return false;
    const int difficulty = g_gameProfile.GetDifficulty();
    if (difficulty == DIFFICULTY_NO_SAVES)
        return false;
    if (difficulty == DIFFICULTY_LIMITED_SAVES && m_savesUsed > 2)
        return false;
    // Not in the very first mission.
    const int campaign = g_campaign->GetCurrentIndex();
    const int number = g_campaign->GetMissionNumber(campaign);
    return campaign != 0 || number != 0;
}

bool C_game::LoadSavedGame(C_chunk &chunk)          // 0x0069c610
{
    C_MissionDef *mission = g_campaign->GetCurrentMission();
    m_mode = GAME_MODE_LOADING;
    Start(mission);
    RestartMission(false);
    LoadGame(chunk);
    m_mode = GAME_MODE_NORMAL;
    m_hasQuickSave = true;
    return true;
}

void C_game::RestartMission(bool newRound)          // 0x0069c660
{
    if (m_state != GAME_STATE_RUNNING && m_mode != GAME_MODE_LOADING)
        return;
    m_savesUsed = 0;
    m_time = 0;
    m_missionTime = 0;
    m_roundTime = 0;
    m_missionFailed = false;
    m_vipChosen = false;
    m_respawnRequest = 0;
    m_spectatedSlot = -1;
    m_noTeamDamage = false;
    m_roundState = IsLocalGame() ? ROUND_PLAYING : ROUND_WARMUP;
    m_cameraMode = GAME_CAMERA_FOLLOW;
    m_uniformDisguise = true;
    m_tacticalMode = true;
    m_paused = false;
    m_unknown94 = false;
    m_signalPistolFired = false;
    if (g_mpSettings->teamLives == 0) {
        S_NetPlayerState *players = g_netGame.LockPlayers();
        const int count = g_netGame.GetPlayerCount();
        for (int slot = 0; slot < count; ++slot)
            players[slot].livesUsed = players[slot].human ? -1 : 0;
        g_netGame.UnlockPlayers();
    } else {
        int axis = 0;
        int allied = 0;
        g_netGame.CountTeamPlayers(&axis, &allied);
        m_teamLivesUsed = -(axis + allied);
    }
    if (!IsServer())
        m_vipSlot = -1;
    while (!m_pendingSpawns.empty()) {
        S_PendingSpawn *spawn = m_pendingSpawns.back();
        // Fix: the original frees only the record, leaking its soldier state and stats.
        delete spawn->state;
        delete[] spawn->stats;
        _aligned_free(spawn);
        m_pendingSpawns.pop_back();
    }
    if (IsClient()) {
        m_interface->Reset();
        FreeSpeechSlots();
        ClearSpeechSlots();
    }
    m_checkpoints->ResetState();

    const bool restartActors = !newRound && IsLocalGame() && m_mode != GAME_MODE_LOADING;
    if (restartActors) {
        // The players' soldiers (the first 32 ids) go; the spawn points start over.
        for (int id = 0; id < 32; ++id) {
            C_Actor *actor = g_actorsModule.GetActorById(id);
            if (!actor)
                continue;
            if (IsClient())
                actor->StopMission(false);
            if (IsServer())
                actor->StopMission(true);
            g_actorsModule.DestroyActor(actor, true);
        }
        for (int id = g_actorsModule.GetActorCount(); id-- > 0;) {
            C_Actor *actor = g_actorsModule.GetActorById(id);
            if (actor && actor->GetType() == ACTOR_SPAWN_POINT) {
                actor->StopMission(false);
                actor->StopMission(true);
                actor->StartMission(true);
                actor->StartMission(false);
            }
        }
    }
    ModuleManager::BroadcastRestartMission(newRound);
    if (restartActors)
        SpawnMissionSquad();
    if (IsClient()) {
        m_firstPersonView->Reset();
        m_quickMessages->Reset();
    }
    TimedEvents_Clear();
    if (m_mission)
        m_mission->DeactivateObjectives();
    if (!IsLocalGame() && !IsServer()) {
        g_netGame.FlipSessionParity();
        g_netGame.ResetReplication(true);
    }
    if (newRound) {
        m_fadeTime = 500;
        m_state = GAME_STATE_STARTING;
    }
    if (!IsLocalGame() && IsClient() && !g_netGame.GetLocalHuman()) {
        // A client without a soldier asks for one.
        if (GetGameApp()->m_state == GetGame())
            GetGameApp()->PostCommand(APP_NET_COMMAND_0, 0, 0, false);
    }
    m_musicTimer = 0;
    m_alarmCount = 0;
    m_inputDelay = 2500;
    RestoreMusic(false);
    C_Human::GetSpeechSets()->ResetDelays();
    if (IsLocalGame() && g_gameProfile.GetDetailSettings()->IsEnabled(DETAIL_STAINS))
        g_stainManager.Reset();
}

bool C_game::SaveGame(C_chunk &chunk)               // 0x0069cbb0
{
    // (Counted also when nothing is saved.)
    if (g_gameProfile.GetDifficulty() == DIFFICULTY_LIMITED_SAVES)
        ++m_savesUsed;
    if (m_state != GAME_STATE_RUNNING || !IsServer())
        return false;
    chunk.Begin(CHUNK_GAME);
    chunk.WriteChunk(CHUNK_GAME_VERSION, SAVE_VERSION);
    const char *name = m_missionName;
    chunk.WriteChunk(CHUNK_GAME_MISSION, name, static_cast<int>(std::strlen(name) + 1));
    g_savedGameState.fadeTime = m_fadeTime;
    g_savedGameState.endDelay = m_endDelay;
    g_savedGameState.missionTime = m_missionTime;
    g_savedGameState.time = m_time;
    g_savedGameState.playerInputEnabled = m_playerInputEnabled;
    g_savedGameState.missionFailed = m_missionFailed;
    g_savedGameState.uniformDisguise = m_uniformDisguise;
    g_savedGameState.tacticalMode = m_tacticalMode;
    g_savedGameState.noTeamDamage = m_noTeamDamage;
    chunk.Begin(CHUNK_GAME_STATE);
    chunk.Write(&g_savedGameState, sizeof(g_savedGameState));
    chunk.Ascend();
    chunk.WriteChunk(CHUNK_GAME_SAVES_USED, m_savesUsed);
    const int pistolFired = m_signalPistolFired ? 1 : 0;
    chunk.WriteChunk(CHUNK_GAME_SIGNAL_PISTOL, pistolFired);
    SaveSpawnedVehicles(chunk);                     // (before the modules: see the header)
    chunk.Begin(CHUNK_GAME_MODULES);
    ModuleManager::SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_GAME_INTERFACE);
    m_interface->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_GAME_TIMED_EVENTS);
    TimedEvents_Save(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_GAME_CHECKPOINTS);
    m_checkpoints->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_GAME_QUICK_MESSAGES);
    m_quickMessages->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_GAME_HUD);
    m_firstPersonView->SaveGame(chunk);
    chunk.Ascend();
    if (m_mission) {
        chunk.Begin(CHUNK_GAME_MISSION_DEF);
        m_mission->SaveProgress(chunk);
        chunk.Ascend();
    }
    chunk.Ascend();
    PrintMessage(MESSAGE_WHITE, g_texts.Get(TEXT_GAME_SAVED));
    if (g_gameProfile.GetDifficulty() == DIFFICULTY_LIMITED_SAVES) {
        const C_fstr text(g_texts.Get(TEXT_SAVES_LEFT), 3 - m_savesUsed);
        PrintMessage(MESSAGE_WHITE, text);
    }
    m_hasQuickSave = true;
    return true;
}

bool C_game::LoadGame(C_chunk &chunk)               // 0x0069d290
{
    if ((m_state != GAME_STATE_RUNNING && m_mode != GAME_MODE_LOADING) || !IsServer() || !chunk.IsOpen())
        return false;
    uint16_t id = 0;
    if (!chunk.Descend(id) || id != CHUNK_GAME) {
        // (The original stays inside a chunk of another kind.)
        return false;
    }
    m_paused = false;
    bool spawnedVehiclesLoaded = false;
    while (chunk.RemainingSize() > 0) {
        chunk.Descend(id);
        switch (id) {
        case CHUNK_GAME_VERSION: {
            int version = 0;
            chunk.Read(version);
            chunk.Ascend();
            if (version != SAVE_VERSION)
                return false;
            break;
        }
        case CHUNK_GAME_MISSION: {
            // A saved game of another mission is refused.
            char name[1000];
            const int remaining = chunk.RemainingSize();
            chunk.Read(name, remaining < 1000 ? remaining : 999);
            name[999] = '\0';
            chunk.Ascend();
            if (std::strcmp(m_missionName, name) != 0)
                return false;
            break;
        }
        case CHUNK_GAME_HUD:
            m_firstPersonView->LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_GAME_INTERFACE:
            m_interface->LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_GAME_SPAWNED_VEHICLES:
            LoadSpawnedVehicles(chunk);
            spawnedVehiclesLoaded = true;
            chunk.Ascend();
            break;
        case CHUNK_GAME_MODULES:
            if (!spawnedVehiclesLoaded)
                RemoveSpawnedVehicles();
            ModuleManager::LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_GAME_TIMED_EVENTS:
            TimedEvents_Load(chunk);
            chunk.Ascend();
            break;
        case CHUNK_GAME_CHECKPOINTS:
            m_checkpoints->LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_GAME_STATE:
            if (chunk.RemainingSize() >= static_cast<int>(sizeof(g_savedGameState)))
                chunk.Read(&g_savedGameState, sizeof(g_savedGameState));
            m_fadeTime = g_savedGameState.fadeTime;
            m_endDelay = g_savedGameState.endDelay;
            m_missionTime = g_savedGameState.missionTime;
            m_time = g_savedGameState.time;
            m_playerInputEnabled = g_savedGameState.playerInputEnabled;
            m_missionFailed = g_savedGameState.missionFailed;
            m_uniformDisguise = g_savedGameState.uniformDisguise;
            m_tacticalMode = g_savedGameState.tacticalMode;
            m_noTeamDamage = g_savedGameState.noTeamDamage;
            chunk.Ascend();
            break;
        case CHUNK_GAME_QUICK_MESSAGES:
            m_quickMessages->LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_GAME_MISSION_DEF:
            m_mission->LoadProgress(chunk);
            chunk.Ascend();
            break;
        case CHUNK_GAME_SAVES_USED:
            chunk.Read(m_savesUsed);
            chunk.Ascend();
            break;
        case CHUNK_GAME_SIGNAL_PISTOL: {
            int fired = 0;
            chunk.Read(fired);
            chunk.Ascend();
            m_signalPistolFired = fired != 0;
            break;
        }
        default:
            chunk.Ascend();
            break;
        }
    }
    chunk.Ascend();
    PrintMessage(MESSAGE_WHITE, g_texts.Get(TEXT_GAME_LOADED));
    m_inputDelay = 500;
    return true;
}
