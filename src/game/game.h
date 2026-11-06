// C_game — the game state of a running mission (the scene, the mission clock, the HUD
// objects, the multiplayer rounds; original source 0x006987d0..0x0069f3c0, the scene loader
// C_game::LoadMission 0x006caad0 lives with the loader, game/mission_loader.cpp).
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/campaign.h"
#include "game/game_state.h"
#include "ls3d/math.h"

class I3D_scene;
class I3D_frame;
class I3D_model;
class I3D_light;
class I3D_sector;
class C_chunk;
class C_BitStream;
class C_CameraModule;
class C_Checkpoints;
class C_Interface;
class C_FirstPersonView;
class C_Human;
struct S_SoldierState;

// The mission and its objectives (C_MissionDef, S_MissionObjective): see game/campaign.h.

// ---------------------------------------------------------------------------

// A respawn the server sends to the clients at its next tick (C_game::m_pendingSpawns;
// 16-byte aligned).
struct S_PendingSpawn {                             // 0x30 bytes
    S_vector4 pos;
    S_vector4 dir;                                  // +0x10
    S_SoldierState *state;                          // +0x20 owned
    int *stats;                                     // +0x24 owned (12 values, C_Human::Respawn)
    int slot;                                       // +0x28 network slot
};

// Multiplayer rounds (C_game::m_roundState).
enum E_RoundState : int {
    ROUND_PLAYING = 0,
    ROUND_OVER = 1,                                 // the round's result is shown (4 s)
    ROUND_INTERMISSION = 2,                         // the scores are shown (5 s), then the next round
    ROUND_WARMUP = 3,
    ROUND_CLOSING = 4,                              // the session ends (4 s), back to the lobby
};

// C_game::m_mode values.
enum E_GameMode : int {
    GAME_MODE_NORMAL = 0,
    GAME_MODE_LOADING = 1,                          // a saved game is being loaded
    GAME_MODE_QUIT = 2,                             // the in-game menu's quit: the mission ends into the main menu
};

// C_game::m_cameraMode values (multiplayer).
enum : int {
    GAME_CAMERA_OWN = 0,
    GAME_CAMERA_FOLLOW = 1,                         // spectating: follows a player
    GAME_CAMERA_FREE = 2,                           // the free camera (warmup, spectator team)
};

// Message types of C_GameApp::SendGameMessage() that the game sends (others are the net
// code's own).
enum E_GameMessage : int {
    GAME_MSG_SESSION = 0,                           // session state (sent reliably when a round restarts)
    GAME_MSG_GAME_ENDED = 1,
    GAME_MSG_SPAWN = 3,                             // an S_PendingSpawn (the message frees it)
    GAME_MSG_SETTINGS = 6,
    GAME_MSG_TEMP_ACTORS = 7,
    GAME_MSG_SCORES = 8,
    GAME_MSG_OBJECTIVE = 10,                        // objective number (5 bits), status (4 bits)
    GAME_MSG_WARMUP_FINISHED = 11,
    GAME_MSG_WARMUP_STARTED = 12,
    GAME_MSG_ROUND_OVER = 13,                       // the winning team (2 bits)
    GAME_MSG_SPAWN_FAILED = 15,                     // slot (5 bits), unknown bit
    GAME_MSG_RESPAWN_WAVE = 16,
    GAME_MSG_TEMP_ACTORS_START = 20,
};

class C_QuickMessages;

class C_game : public C_GameState {                 // vtable 0x008159ac, 0xEC bytes
    friend class C_GameApp;                             // the game commands (net/net_commands.cpp)
public:
    C_game();                                       // 0x006987e0
    ~C_game() override;                             // 0x00698b00 (0x00698ae0 deleting)
    C_game(const C_game &) = delete;
    C_game &operator=(const C_game &) = delete;

    // --- C_GameState ---------------------------------------------------------------
    // Loads and starts the mission `mission` (a C_MissionDef; null: the multiplayer
    // session's current map). False when the mission did not load.
    bool Start(void *mission) override;             // 0x00698c80
    bool Stop() override;                           // 0x00699250
    bool HandleInput(float *held, float *pressed) override;   // 0x0069a830
    void Tick(int timeDelta) override;              // 0x0069b1e0
    void Update(int timeDelta) override;            // 0x0069b500
    void Render() override;                         // 0x0069bb40
    int GetTypeId() const override { return GAME_STATE_TYPE_GAME; }   // 0x00698ab0

    // States in which a mission is loaded and running.
    bool IsMissionRunning() const
    {
        return m_state == GAME_STATE_RUNNING || m_state == GAME_STATE_STARTING || m_state == GAME_STATE_ENDING;
    }
    I3D_scene *GetScene() const { return m_scene; }
    bool IsMissionLoaded() const { return m_missionLoaded; }
    // The far clipping distance (m; the birds fly off beyond it).
    float GetFarClip() const { return m_farClip; }
    float GetNearClip() const { return m_nearClip; }
    const char *GetMissionName() const { return m_missionName; }
    // Simulation time of the mission in ms; advances every unpaused frame and is
    // still 0 before the first one (scripts wait for it).
    uint32_t GetMissionTime() const { return m_missionTime; }
    bool HasMissionStarted() const { return m_missionTime != 0; }
    // Game time in ms (on a client the server's clock as last synchronised).
    uint32_t GetTime() const { return m_time; }
    // Simulation stopped (the in-game menu, the map); the scene is still drawn.
    bool IsPaused() const { return m_paused; }
    // Pauses the simulation (single player only; never in the first 2.5 s).
    void SetPaused(bool paused);                    // 0x0069bb80
    // The in-game menu opens (the command map's options button sets the state directly).
    void OpenGameMenu() { m_state = GAME_STATE_GAME_MENU; }
    // Brightness (0..1) of the scene lights at `pos`: luminance of the sector's
    // lights weighted by their range and cone; `sector` null = the one containing
    // `pos`. With `occlusion` lights blocked by level geometry are skipped.
    float GetLightIntensity(const S_vector &pos, I3D_sector *sector, bool occlusion);   // 0x0069eca0

    // Loads the mission's scene, actors and data (0 when it loaded, else an error; the modules'
    // LoadMissionData follows either way). game/mission_loader.cpp.
    int LoadMission(const char *name, C_MissionDef *mission);   // 0x006caad0
    // Frees the mission's data (the modules, the scene's frames).
    void FreeMission();                             // 0x0069a6a0
    // Starts the fade out; the mission ends after it (every caller passes 100, which is not
    // used).
    void EndMission(int unused);                    // 0x0069a7d0
    // Starts the mission (single player) or round (multiplayer) over; `newRound` keeps
    // the actors (the next multiplayer round).
    void RestartMission(bool newRound);             // 0x0069c660
    bool SaveGame(C_chunk &chunk);                  // 0x0069cbb0
    bool LoadGame(C_chunk &chunk);                  // 0x0069d290
    // Starts the current campaign mission and loads the saved game into it.
    bool LoadSavedGame(C_chunk &chunk);             // 0x0069c610
    // Quick saving is allowed now (the mission runs; saves left at the difficulty).
    bool CanQuickSave() const;                      // 0x0069c550
    bool HasQuickSave() const { return m_hasQuickSave; }
    bool IsMissionFailed() const { return m_missionFailed; }
    // The screen fade of the mission start / end (the scene's render callback).
    void ApplyFade();                               // 0x0069b0e0
    // Opens the in-game menu over this game; returns the state that is now current.
    C_GameState *OpenGameMenu(int arg);             // 0x0069e8f0

    // --- HUD --------------------------------------------------------------------------
    C_Checkpoints *GetCheckpoints() const { return m_checkpoints; }
    C_Interface *GetInterface() const { return m_interface; }
    // Weapon / inventory part of the HUD.
    C_FirstPersonView *GetFirstPersonView() const { return m_firstPersonView; }
    // Quick chat messages of the interface (+0x60).
    C_QuickMessages *GetQuickMessages() const { return m_quickMessages; }
    C_CameraModule *GetCameraModule();              // 0x0069a620
    bool IsFirstPersonView() const;                        // 0x0069a780
    void ShowFirstPersonView(bool show);                        // 0x0069a770
    bool IsBinocularsView() const;                   // 0x0069a7b0
    void ShowBinocularsView(bool show);                   // 0x0069a7a0
    void Print(const char *format, ...);            // 0x0069c2b0 (empty in the release build; cdecl)
    void PrintColored(const S_vector &color, const char *format, ...);   // 0x0069c2a0 (empty too)
    // A message line (printf format) in `color` (0..255 components): the HUD on clients,
    // the console otherwise.
    void PrintMessage(const S_vector &color, const char *format, ...);   // 0x0069c240
    // A video played over the game (GAME_STATE_PLAY_AVI).
    void PlayAvi(const char *fileName);             // 0x0069e8b0
    const char *GetAviName() const { return m_aviName; }   // 0x0069e8d0
    float GetCutsceneFOV() const { return m_fov; }
    // The player's controls reach the soldier (scripts switch them off for cutscenes); the
    // application's pending commands (saving, loading) wait for it.
    void SetPlayerInputEnabled(bool on) { m_playerInputEnabled = on; }
    bool IsPlayerInputEnabled() const { return m_playerInputEnabled; }

    // --- music --------------------------------------------------------------------------
    // Restarts the mission music (the alarm variant if `alarm`).
    void RestoreMusic(bool alarm);                  // 0x0069ec40
    // An alarm starts / ends (counted; the alarm music plays while any is on).
    void SetAlarm(bool on);                         // 0x0069ebf0
    bool IsAlarmMusic() const { return m_alarmCount != 0; }

    // --- mission script settings ------------------------------------------------------
    bool IsUniformDisguise() const { return m_uniformDisguise; }
    bool IsNoTeamDamage() const { return m_noTeamDamage; }
    void SetUniformDisguise(bool on) { m_uniformDisguise = on; }
    void SetNoTeamDamage(bool on) { m_noTeamDamage = on; }
    void SetTacticalMode(bool on) { m_tacticalMode = on; }
    // The squad's command map may open.
    bool IsTacticalMode() const { return m_tacticalMode; }
    bool WasSignalPistolFired() const { return m_signalPistolFired; }
    void SetSignalPistolFired(bool on) { m_signalPistolFired = on; }
    // Models whose animation the game advances every frame (it holds a reference).
    void AddAnimatedModel(I3D_frame *model);        // 0x0069a5b0
    void RemoveAnimatedModel(I3D_frame *model);     // 0x0069a630 (releases it)

    // --- objectives -------------------------------------------------------------------
    // The mission of the campaign being played (null when there is none).
    C_MissionDef *GetMission() const { return m_mission; }
    // Objective `objective` (1-based) gets `status` (E_ObjectiveStatus): experience for the
    // squad in single player, points for the team / the clients in multiplayer.
    void SetObjectiveStatus(int objective, int status);   // 0x0069bd30
    // The mission is lost (`deadlyArea`: the squad entered a deadly area; else everyone died or
    // a critical objective failed).
    void FailMission(bool deadlyArea);              // 0x0069c1a0
    void WriteObjectiveStates(C_BitStream *msg) const;   // 0x00699480
    void ReadObjectiveStates(C_BitStream *msg);     // 0x006994f0

    // --- multiplayer --------------------------------------------------------------------
    // The VIP of an objectives game (network slot; -1 none).
    void SetVipSlot(int slot) { m_vipSlot = slot; }
    int GetVipSlot() const;                         // 0x0069e340
    // Round clock (ms since the round started) and the time left of it / of the respawn
    // wave (ms).
    int SetRoundTime(int ms);                       // 0x0069b1a0 (returns s)
    int GetRoundTimeLeft() const;                   // 0x0069b150
    int GetRoundTime() const { return m_roundTime; }
    int GetRespawnTimeLeft() const;                 // 0x0069b180
    // (A client at the server's respawn wave, game message 16: the round time's milliseconds.)
    void SetRespawnWaveTime(int ms) { m_respawnWaveTime = ms; }
    // Client side of the server's messages.
    void OnRoundOver(int winner);                   // 0x00699860
    void OnRespawnScreen(bool cancel);              // 0x006998f0
    void OnWarmupStarted();                         // 0x00699ae0
    void OnWarmupFinished();                        // 0x00699bd0
    void OnSessionClosing(int reason);              // 0x00699cf0
    void StartNextRound();                          // 0x00699a80
    // Server side: a client asks to respawn / joins the game (the game owns `state` and
    // `stats`).
    void OnRespawnRequest(int slot, S_SoldierState *state, int *stats);   // 0x0069dc90
    void OnPlayerJoined(int slot, S_SoldierState *state, int *stats);     // 0x0069e610
    void OnPlayerSpawned(int slot, C_Human *human); // 0x0069e560
    void OnPlayerLeft(int connection);              // 0x0069e9f0
    // Puts the human of `slot` at `pos` (client side of a respawn).
    void RespawnPlayer(int slot, const S_vector4 &pos, const S_vector4 &dir, S_SoldierState *state,
                       const int *stats);           // 0x0069da50
    // The spectator camera follows players / the free camera.
    void SetCameraMode(int mode);                   // 0x0069ad20
    int GetCameraMode() const { return m_cameraMode; }
    // The player slot the spectator camera follows (-1: none).
    int GetSpectatedSlot() const { return m_spectatedSlot; }
    int GetRoundState() const { return m_roundState; }
    int GetMode() const { return m_mode; }          // E_GameMode
    void SetMode(int mode) { m_mode = mode; }       // (the in-game menu's quit)
    // Co-op with shared lives: the respawns used.
    int GetTeamLivesUsed() const { return m_teamLivesUsed; }
    void SetTeamLivesUsed(int used) { m_teamLivesUsed = used; }   // (a client: the server's scores message)
    // A client: the mission's clock and the game time set from the server's (the scores message).
    void SetMissionClock(uint32_t time)
    {
        m_missionTime = time;
        m_time = time;
    }
    // The campaign mission's number across the campaigns (-1: not a campaign mission).
    int GetCampaignMissionNumber() const { return m_campaignMissionNumber; }

    // Sends message `type` to each connected player but the local one.
    static void SendToPlayers(int type);            // 0x00699560

private:
    friend class C_MissionLoader;                   // (the mission's name)
    void ReleaseScene();                            // 0x00699540
    // Switches the scene's static lights off for the mission (`restore`: back on).
    void SwitchStaticLights(bool restore);          // 0x0069c2c0
    void UpdateSpectatorCamera(float *held, float *pressed);   // 0x0069ad80
    // Checks the round's end (server, once a second).
    void CheckRoundEnd(int timeDelta);              // 0x00699e20
    // The open objectives go to the winner (1-based objectives, completed / failed).
    void DecideObjectives(int winner);              // 0x006991c0
    void ApplyObjectiveStatus(int objective, int status);   // 0x0069bf10
    void AnnounceRoundEnd(int winner);              // 0x00699630
    void ChooseVip();                               // 0x00699950
    void StartWarmup();                             // 0x00699b60
    // Players in damaging / restricted areas (twice a second).
    void CheckPlayerAreas(int timeDelta);           // 0x0069f1a0
    // Some objective is active and none is still open (inline in the original).
    bool AreObjectivesDecided() const;
    // The first objective's attacking team.
    int GetAttackingTeam() const;

    bool m_hasQuickSave = false;                    // +0x0C the current mission has a quick save
    int m_savesUsed = 0;                            // +0x10 saves made at difficulty 3 (3 allowed)
    bool m_missionLoaded = false;                   // +0x14
    I3D_scene *m_scene = nullptr;                   // +0x18
    int m_mode = GAME_MODE_NORMAL;                  // +0x1C E_GameMode
    int m_fadeTime = 0;                             // +0x20 ms of the fade in / out left
    int m_endDelay = 0;                             // +0x24 ms after the fade out
    uint32_t m_missionTime = 0;                     // +0x28 ms
    uint32_t m_time = 0;                            // +0x2C ms
    int m_roundTime = 0;                            // +0x30 ms since the multiplayer round started
    bool m_uniformDisguise = false;                 // +0x34
    bool m_noTeamDamage = false;                    // +0x35
    bool m_tacticalMode = false;                    // +0x36
    int m_inputDelay = 0;                           // +0x38 ms before the input counts (2.5 s at the start)
    int m_campaignMissionNumber = -1;               // +0x3C
    bool m_signalPistolFired = false;               // +0x40
    std::vector<I3D_model *> m_animatedModels;      // +0x44
    C_Checkpoints *m_checkpoints = nullptr;         // +0x54
    C_Interface *m_interface = nullptr;             // +0x58
    C_FirstPersonView *m_firstPersonView = nullptr;               // +0x5C
    C_QuickMessages *m_quickMessages = nullptr;     // +0x60
    bool m_paused = false;                          // +0x64
    int16_t m_unknown66 = 0;                        // +0x66 (scene chunk 1)
    int16_t m_unknown68 = 0;                        // +0x68 (scene chunk 1)
    C_str m_missionName;                            // +0x6A (unaligned in the original layout)
    bool m_playerInputEnabled = false;              // +0x6E
    bool m_missionFailed = false;                   // +0x6F
    bool m_vipChosen = false;                       // +0x70
    bool m_hasVipObjective = false;                 // +0x71
    int m_round = 0;                                // +0x74 rounds played on the map
    int m_vipSlot = -1;                             // +0x78
    int m_respawnWaveTime = 0;                      // +0x7C ms
    bool m_inRestrictedArea = false;                // +0x80 the local player was told
    int m_teamLivesUsed = 0;                        // +0x84 co-op: respawns of the shared lives
    int m_roundState = ROUND_PLAYING;               // +0x88 E_RoundState
    uint32_t m_roundStateTime = 0;                  // +0x8C mission time it changed
    int m_respawnRequest = 0;                       // +0x90 client: 1 / 2 posts a command when the slot is free
    bool m_unknown94 = false;                       // +0x94
    std::vector<S_PendingSpawn *> m_pendingSpawns;  // +0x98
    C_MissionDef *m_mission = nullptr;              // +0xA8
    C_str m_aviName;                                // +0xAC
    bool m_playAvi = false;                         // +0xB0
    int m_spectatedSlot = -1;                       // +0xB4
    int m_cameraMode = GAME_CAMERA_FOLLOW;          // +0xB8
    float m_fov = 0.0f;                             // +0xBC (scene chunk 0x3010)
    float m_nearClip = 0.0f;                        // +0xC0 0.1 (scene chunk 0x3011)
    float m_farClip = 0.0f;                         // +0xC4
    int m_music = -1;                               // +0xC8 sound module music (scene chunk 0xB840)
    int m_alarmMusic = -1;                          // +0xCC (scene chunk 0xB880)
    int m_musicTimer = 0;                           // +0xD0 ms (5000 when the music changes)
    int m_alarmCount = 0;                           // +0xD4
    int m_unknownD8 = 0;                            // +0xD8 (scene chunk 0xB650)
    std::vector<I3D_light *> m_staticLights;        // +0xDC switched off for the mission
};

// Quick chat messages (C_game +0x60, 0x28 bytes).
class C_QuickMessages {
public:
    C_QuickMessages();                              // (inline in the C_game constructor)
    ~C_QuickMessages();                             // (inline in the C_game destructor)
    // Text of message `id` (ids from 90000000 on), the default text for others.
    const char *GetText(int id) const;              // 0x004900e0
    void Init(const char *missionName);             // 0x0048eb70
    void Done();                                    // 0x0048ef40
    void Reset();                                   // 0x0048f400
    void Update(int timeDelta);                     // 0x0048f2d0
    bool SaveGame(C_chunk &chunk);                  // 0x0048fc50
    bool LoadGame(C_chunk &chunk);                  // 0x0048fdd0
};

C_game *GetGame();                                  // 0x006987d0
// A human or a car within 2 m above `pos` (spawning there would collide).
bool IsSpawnBlocked(const S_vector4 &pos);           // 0x0069d970
// Occupation: a respawning player faces the zone to take from `pos` (a C_game method in the
// original).
void FaceTargetZone(S_vector4 *dir, const S_vector4 &pos, int team);   // 0x0069e370
// Shows text `textId` in the in-game menu when it is open.
void ShowGameMenuMessage(int textId);                // 0x0069e9b0
// The session moves on to its next map (the settings' next; the game's pending state GAME_STATE_NEXT_MAP).
void LoadNextMap();                                 // 0x0069bca0
extern C_game *g_game;                              // 0x008ae8f4
