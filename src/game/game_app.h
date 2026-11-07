// The application's state machine (C_GameApp; g_gameApp 0x008aee24, created by RunGame).
//
// The application holds one C_GameState at a time: the logo / intro movies, the menus, the
// campaign's videos and screens, the mission itself (C_game), the in-game menu wrapped around
// it, or the console. Each frame RunGame calls UpdateServer (the 100 ms server ticks) and
// UpdateClient (input, the state's Update, then the state's state value decides what follows:
// the next campaign step, the previous screen, a network session, the next map, a movie, the
// in-game menu ...). The campaign's steps (C_Campaign::NextStep) become states in StartStep.
// The commands the game queues (m_pendingCommand) restart the mission and write / read the
// saved games (chunk 0x4E8C: 2999 the mission number, then the profile and the game).
#pragma once

#include <cstdint>

#include <windows.h>

#include "common/c_str.h"
#include "game/game_state.h"

class C_BitStream;
class C_CampaignNode;
class C_chunk;
class C_game;

// Commands queued on the application (m_pendingCommand), executed between frames.
enum E_AppCommand : int {
    APP_COMMAND_NONE = 0,
    APP_COMMAND_RESTART = 1,                        // the mission starts again
    APP_COMMAND_QUICK_SAVE = 2,
    APP_COMMAND_QUICK_LOAD = 3,
    APP_COMMAND_SAVE = 4,                           // into m_name
    APP_COMMAND_LOAD = 5,                           // from m_name
    APP_COMMAND_LOAD_FROM_MENU = 6,                 // the main menu loads m_name (RequestLoad)
};

// PostCommand() commands.
enum : int {
    APP_NET_COMMAND_0 = 0,                          // (not identified; a client without a soldier sends it)
    APP_NET_COMMAND_RESPAWN = 1,                    // the local player asks to respawn
};

// The saved games' chunks.
enum : uint16_t {
    CHUNK_SAVED_GAME = 0x4E8C,
    CHUNK_SAVED_MISSION_OLD = 0x0BB6,               // accepted on load
    CHUNK_SAVED_MISSION = 0x0BB7,                   // (2999) the mission number
};

class C_GameApp {                                   // 0x44 bytes
public:
    C_GameApp();                                    // 0x006c0a70
    ~C_GameApp();                                   // 0x006c0ab0
    C_GameApp(const C_GameApp &) = delete;
    C_GameApp &operator=(const C_GameApp &) = delete;

    // Starts the application: the profile, the last player and the logos (or the console of
    // the command line); `resume` restarts the state the application already holds instead.
    // False while a state is active.
    bool Init(bool resume);                         // 0x006c0b40
    // Leaving the game: the network session is told, then Close().
    void Shutdown();                                // 0x006c0bf0
    // Stops the state and empties the resource caches (the state is deleted unless resumed).
    bool Close();                                   // 0x006c0c40 (thunk 0x006c0c30)

    // The fixed 100 ms server ticks of the active state (false: the application ends).
    bool UpdateServer(int timeDelta);               // 0x006c3420
    // Input, the state's frame update and the switch to what the state asks for (false: the
    // application ends).
    bool UpdateClient(int timeDelta);               // 0x006c2410
    void RenderState();                             // 0x006c34b0

    // --- the queued commands ---------------------------------------------------------------
    // APP_COMMAND_RESTART: the mission again (the sound is off for 100 ms).
    void RestartMission();                          // 0x006c4200
    // APP_COMMAND_QUICK_SAVE / SAVE.
    bool SaveCommand();                             // 0x006c3c20
    // APP_COMMAND_QUICK_LOAD / LOAD (the sound is off for 100 ms).
    bool LoadCommand();                             // 0x006c3550
    // APP_COMMAND_LOAD_FROM_MENU: the main menu loads m_name (empty: the profile's last save):
    // a quick save goes straight into its mission, any other save continues the campaign.
    bool LoadFromMenu();                            // 0x006c43a0
    // Queues APP_COMMAND_LOAD_FROM_MENU for `fileName` (nothing for an empty name).
    void RequestLoad(const C_str &fileName);        // 0x006c42b0

    // A saved game: `inGame` the running mission (chunk 0x4E8C), otherwise the profile only.
    bool SaveGame(const C_str &fileName, bool inGame);   // 0x006c3d20
    bool LoadGame(const C_str &fileName, bool inGame);   // 0x006c3660
    // The profile's quick save is of the mission played.
    bool HasQuickSave();                            // 0x006c4f10
    // A quick save `fileName` (in the profile directory) holds a mission (chunks 0x4E8C and
    // 2999); its mission number goes to `mission` (may be null).
    bool GetQuickSaveMission(const C_str &fileName, int *mission);   // 0x006c4b00

    // A message of the network code: the game's message lines, or the console (dedicated
    // server, the console state).
    void PrintMessage(const char *text);            // 0x006c4e80
    // Queues a command for the network game (critical section +0x2C; `flush`: sends the
    // queued ones instead, the other arguments are not used).
    void PostCommand(int command, intptr_t a, intptr_t b, bool flush);   // 0x0072b590
    // Sends game message `type` (E_GameMessage; `to`: a player's connection, 0 all) with two
    // values (`reliable`: the session messages).
    void SendGameMessage(int type, int to, intptr_t a, int b, bool reliable);   // 0x0072a480
    // A game command (NET_MESSAGE_GAME's id and data) of a received stream: the server's handling of a client's
    // and a client's of the server's (part 10d).
    void HandleServerCommand(int id, C_BitStream *stream);  // 0x00727bd0
    void HandleClientCommand(int id, C_BitStream *stream);  // 0x00728820

    // The state is active (its state value is running, starting or ending).
    bool IsStateActive() const { return m_state && IsActiveState(m_state->GetState()); }

    C_str m_name;                                   // +0x00 the file of APP_COMMAND_SAVE / LOAD
    int m_pendingCommand = APP_COMMAND_NONE;        // +0x04 E_AppCommand
    C_GameState *m_state = nullptr;                 // +0x14

private:
    // Game command 0x15 (the whole game; part of HandleClientCommand 0x00728820).
    void ReadFullState(C_BitStream *stream);
    // `-cmd <command>` / `-c <script>` on the command line: the console state.
    bool StartFromCommandLine();                    // 0x006c0e10
    // The console of the command line, else the first logo.
    bool StartIntro();                              // 0x006c0fc0
    // Replaces the state with the one campaign step `step` needs (null: the main menu).
    // (`unused`: every caller passes 0.)
    bool StartStep(C_CampaignNode *step, int unused);   // 0x006c1210
    // The next (`back`: previous) campaign step; a Lone Wolf campaign skips the squad screens.
    bool StartNextStep(bool back);
    // Stops and deletes the state and puts `next` in its place.
    void ReplaceState(C_GameState *next);
    // The in-game menu gives the game back (the menu is deleted).
    void CloseGameMenu();
    // The game the state is (the in-game menu: the game behind it), null if none.
    C_game *GetRunningGame() const;
    // The clients' end of an update (the queued commands go out), the server's (the session
    // message); both return `result`.
    bool EndClientUpdate(bool result);              // 0x006c23e0
    bool EndServerUpdate(bool result);              // 0x006c33e0
    void HandleInput();                             // 0x006c34f0
    // A movie of the intro sequence ended (GAME_STATE_MOVIE_FINISHED).
    bool OnMovieFinished();
    // The session's next map (GAME_STATE_NEXT_MAP).
    void StartNextMap();

    uint8_t m_unknown08 = 0;                        // +0x08 (only the constructor sets them)
    int m_unknown0C = 0;                            // +0x0C
    uint8_t m_unknown10 = 0;                        // +0x10
    bool m_resume = false;                          // +0x11 Init(resume)
    // While set, the state is neither updated nor ticked (nothing sets it).
    bool m_suspended = false;                       // +0x12
    int m_serverTime = 0;                           // +0x18 ms not yet ticked
    uint32_t m_randomId = 0;                        // +0x1C two rand() values (not read here)
    bool m_gamePaused = false;                      // +0x20 the game's pause before its menu opened
    int m_volumeDelay = 0;                          // +0x24 ms until the profile's volume returns
    int m_updatePhase = 0;                          // +0x28 1 client, 2 server, 0 outside an update
    CRITICAL_SECTION m_commandLock;                 // +0x2C PostCommand()
};

C_GameApp *GetGameApp();                            // 0x006dcd10
