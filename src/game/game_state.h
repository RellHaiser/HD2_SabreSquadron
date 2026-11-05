// C_GameState — the states the application switches between (the menus, the video player,
// the game itself; C_GameApp::m_state).
#pragma once

#include <cstdint>

// Values of C_GameState::GetState() / SetPendingState(). The application (C_GameApp::
// UpdateClient) switches to what the state asks for; the menus use further values among
// themselves.
enum E_GameStateValue : int {
    GAME_STATE_NONE = 0,
    GAME_STATE_RUNNING = 1,                         // active (C_game: a mission is played)
    GAME_STATE_ENDED = 2,                           // done: the campaign's next step
    GAME_STATE_NO_PENDING = 2,                      // (pending state) nothing to switch to
    GAME_STATE_BACK = 3,                            // the campaign's previous screen
    GAME_STATE_QUIT = 4,                            // the game ends (fatal load errors, the console closed)
    GAME_STATE_STARTING = 5,                        // C_game: the mission fades in
    GAME_STATE_ENDING = 6,                          // C_game: the mission fades out
    GAME_STATE_HOST_SESSION = 7,                    // the session menu, hosting (session type 2)
    GAME_STATE_JOIN_SESSION = 8,                    // the session menu, joining (session type 1)
    GAME_STATE_NEXT_MAP = 9,                        // multiplayer: load the session's next map
    GAME_STATE_MAIN_MENU = 10,
    GAME_STATE_LOBBY = 11,                          // multiplayer: back to the session menu
    GAME_STATE_PLAY_AVI = 12,                       // a movie over the state (the game's, the menu's)
    GAME_STATE_MOVIE_FINISHED = 13,                 // C_MovieState: what follows
    GAME_STATE_GAME_MENU = 14,                      // the in-game menu (single player: pauses)
    GAME_STATE_CLOSE_GAME_MENU = 15,                // the in-game menu: back to the game
    GAME_STATE_MENU_END_MISSION = 16,               // the in-game menu: the mission ends
    GAME_STATE_MP_GAME_MENU = 17,                   // the in-game menu of a network game
};

// Running, starting or ending: the state is updated and drawn.
inline bool IsActiveState(int value)
{
    return value == GAME_STATE_RUNNING || value == GAME_STATE_ENDING || value == GAME_STATE_STARTING;
}

// C_GameState::GetTypeId() values (game/pending.h has the menus).
enum : int {
    GAME_STATE_TYPE_NONE = 0,                       // the base class
    GAME_STATE_TYPE_GAME = 1,                       // C_game
    GAME_STATE_TYPE_MAIN_MENU = 2,                  // C_MainMenu
    GAME_STATE_TYPE_SQUAD_MENU = 3,                 // C_SquadMenu
    GAME_STATE_TYPE_EQUIPMENT_MENU = 4,             // C_EquipmentMenu
    GAME_STATE_TYPE_DEBRIEFING_MENU = 5,            // C_DebriefingMenu
    GAME_STATE_TYPE_CAMPAIGN_VIDEO = 7,             // C_VideoState
    GAME_STATE_TYPE_SESSION_MENU = 8,               // C_SessionMenu
    GAME_STATE_TYPE_HERO_STATS_MENU = 9,            // C_HeroStatsMenu
    GAME_STATE_TYPE_VIDEO = 10,                     // C_MovieState (0x00815bc8)
    GAME_STATE_TYPE_GAME_MENU = 11,                 // the in-game menu (0x00815750; wraps the game)
    GAME_STATE_TYPE_CONSOLE = 12,                   // C_ConsoleState
    GAME_STATE_TYPE_OPERATION_END_MENU = 14,        // C_OperationEndMenu
};

// (The states are allocated 16-byte aligned.)
class alignas(16) C_GameState {                     // vtable 0x008158ac
public:
    virtual ~C_GameState();                         // 0x0067ef90 (the deleting one)
    // Enters the state; `arg` is what it starts with (C_game: the mission's definition,
    // null in multiplayer).
    virtual bool Start(void *arg);                  // 0x04 (the base: 0x0067ef30, false)
    virtual bool Stop();                            // 0x08 (0x0067ef40, false)
    // The frame's controls: values held per action and those pressed in this frame
    // (C_Controls; null when there are none).
    virtual bool HandleInput(float *held, float *pressed);   // 0x0C (0x0067ef50, false)
    virtual void Tick(int timeDelta);               // 0x10 fixed server tick (100 ms; 0x0067ef60)
    virtual void Update(int timeDelta);             // 0x14 per frame (0x0067ef70)
    virtual void Render();                          // 0x18 (0x0067ef80)
    virtual int GetTypeId() const;                  // 0x1C (0x0067ecf0, 0)
    virtual int GetState() const;                   // 0x20 0x0067ed00
    virtual void SetState(int state);               // 0x24 0x0067ed10
    virtual int GetPendingState() const;            // 0x28 0x0067ed20
    virtual void SetPendingState(int state);        // 0x2C 0x0067ed30
    // Switches to the pending state, if there is one.
    virtual void CommitPendingState();              // 0x30 0x0067ed40

protected:
    int m_state = 0;                                // +0x04 E_GameStateValue
    int m_pendingState = GAME_STATE_NO_PENDING;     // +0x08
};
