// The network session's menu (vtable 0x00815e98, 0x1D0 bytes; the application's states 7 and 8): the
// connection console, a box over the scene showing the server made or the client connecting
// (stepped by its screen's ticks; Escape on it quits the game), and the password asked when the
// server refuses it. Part 9g-ii.
#pragma once

#include <cstdint>

#include "game/menu_state.h"

class C_Control;
class I3D_frame;
struct S_ScreenDesc;

// The password entered (the session menu's or the main menu's; the main menu's join asks no more).
extern bool g_sessionPasswordSet;                       // 0x008ae5c2

// The console's steps (C_SessionMenu::m_step).
enum : int {
    SESSION_STEP_START = 0,                             // the squad emptied, the session's kind shown
    SESSION_STEP_CONNECT = 1,                           // the client connecting
    SESSION_STEP_JOINING = 2,                           // the client's joining polled
    SESSION_STEP_HOST = 3,                              // the server made
    SESSION_STEP_HOSTED = 4,                            // the game started (the session's map)
    SESSION_STEP_PASSWORD = 5,                          // the password screen opened
    SESSION_STEP_WAIT = 6,                              // the session takes over
    SESSION_STEP_FAILED = 7,                            // the error shown: Escape returns
};

class C_SessionMenu : public C_MenuState {
public:
    // (Inline in C_GameApp::UpdateClient; the destructor is the base's: 0x006c3200.)
    C_SessionMenu() = default;

    int GetTypeId() const override { return GAME_STATE_TYPE_SESSION_MENU; }   // 0x1C 0x006c31f0

    // Escape (a screen shown): on the console the popup closed and the game quit; on the password
    // the messages kept back dropped and the popup closed. SESSION_MSG_STARTED: the session's map
    // loaded (GAME_STATE_NEXT_MAP).
    void OnMessage(S_ControlMessage *message) override; // 0x34 0x0068aa00
    // The console ("MP Connect console" on "confirm") and the password ("PASSWORD") described, those
    // whose model loaded and built kept; the console opened. True.
    bool BuildScreens() override;                       // 0x38 0x0068a750
    // The console's text style released.
    void OnStop() override;                             // 0x3C 0x0068a720
    // The console's text style made; its first step. True.
    bool OnStart(void *arg) override;                   // 0x40 0x0068a680
    void OnUpdate(int) override {}                      // 0x48 0x0068a730
    bool OnRender() override { return false; }          // 0x50 0x0068a740 (DS 0x006c7a10)

    // The console's screen tick (its own screen shown only, else false): a wait counted down by the
    // tick's time, else the step (the texts added to the console's; true).
    static bool OnConsoleTick(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x0068b1c0
    // The console hidden: true.
    static bool OnConsoleHidden(void *menu, void *argument, C_Control *control, uint32_t notification,
                                uint32_t value);        // 0x0068b1b0
    // The password screen's controls: the input's Return or the OK button: a password entered kept
    // (the console's first step again, the screen closed; none: nothing); the input's Escape, the
    // Cancel button: the screen closed. Another control, none: false.
    static bool OnPasswordNotify(void *menu, void *argument, C_Control *control, uint32_t notification,
                                 uint32_t value);       // 0x0068bce0

private:
    // The console's box at (0.5, -0.375, -0.5): its ticks, the text, the buttons (the yes and no
    // hidden; "Exit Game" quitting). True.
    bool DescribeConsole(S_ScreenDesc *desc, I3D_frame *model);    // 0x0068ac80
    // The password's box: "ENTER PASSWORD", the input, OK and Cancel (the input's Return and Escape,
    // their clicks to OnPasswordNotify), the other inputs hidden. True.
    bool DescribePassword(S_ScreenDesc *desc, I3D_frame *model);   // 0x0068b010
    // A line of the console's text.
    void AddLine(uint32_t textId) const;

    uint8_t m_unknown1B0[8] = {};                       // +0x1B0
    int m_wait = 0;                                     // +0x1B8 ms before the next step
    int m_style = 0;                                    // +0x1BC the console's text style (OnStart's)
    int m_step = SESSION_STEP_START;                    // +0x1C0
    uint8_t m_unknown1C4[0xC] = {};                     // +0x1C4
};
