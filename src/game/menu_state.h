// The menu states' base (game/menu_state.cpp, part 9d): a state of the application showing an
// LS3D scene of screens (C_Screen, game/screen.h) with the menus' cursor. The screens shown are a
// stack (the last on top, the one taking the input); the messages posted to target 0 are the
// menu's (opening and closing screens, the message box, the editing, the states asked for),
// the others go to the screen of the target's top 6 bits. Part 9d-ii-a: the class, the stack and
// the messages; 9d-ii-b: starting, stopping, the input, the update and the drawing.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "game/control.h"
#include "game/game_state.h"
#include "game/sound_module.h"
#include "game/text_box.h"

class C_Cursor;
class C_MessageBox;
class C_Screen;
class I3D_camera;
class I3D_frame;
class I3D_light;
class I3D_scene;
class I3D_sound;

// A text the menus show where they ask (the main menu's hints): its box on a frame of theirs.
struct S_MenuHint {                                     // 0xB0 bytes (C_MenuState +0x40)
    S_MenuHint() { Reset(); }
    // Not shown, no text, no frame, the box emptied.
    void Reset();                                       // 0x0066e480 (__fastcall)
    // The text set (shown when on).
    void SetText(const C_str &text, uint8_t style);     // 0x0066e430
    // On and with a text: the box put on the frame and drawn.
    void Render(I3D_scene *scene);                      // 0x0066e450

    int m_unknown00 = -1;                               // +0x00 (the main menu's)
    int m_unknown04 = -1;                               // +0x04
    int m_unknown08 = -1;                               // +0x08
    bool m_on = false;                                  // +0x0C
    bool m_hasText = false;                             // +0x0D
    C_TextBox m_box;                                    // +0x10
    I3D_frame *m_frame = nullptr;                       // +0xAC
};

// The menu's messages (target 0; CONTROL_MSG_MASK of the code). A code with MENU_MSG_DEFER is kept
// back; one with MENU_MSG_DEFERRED posts (MENU_DEFERRED_POST, the defer bit cleared) or drops
// (MENU_DEFERRED_DROP) those kept.
constexpr uint32_t MENU_MSG_DEFER = 0x20000000;
constexpr uint32_t MENU_MSG_DEFERRED = 0x10000000;
constexpr uint32_t MENU_DEFERRED_POST = 0x100, MENU_DEFERRED_DROP = 0x200;
enum : uint32_t {
    MENU_MSG_OPEN_SCREEN = 0x2000001,                   // the top hidden, the screen `value` opened
    MENU_MSG_OPEN_POPUP = 0x2000002,                    // the screen `value` opened over the top
    MENU_MSG_BACK = 0x2000003,                          // the top closed (the last: the quit question)
    MENU_MSG_QUIT = 0x2000004,                          // the sounds stopped, the game ends
    MENU_MSG_CLOSE_POPUP = 0x2000005,
    MENU_MSG_PREVIOUS = 0x2000007,                      // every screen closed, the campaign's previous screen
    MENU_MSG_NEXT = 0x2000008,                          // every screen closed, the campaign's next step
    MENU_MSG_MAIN_MENU = 0x2000009,                     // the game reset, the main menu
    MENU_MSG_RESET_GAME = 0x2000068,
    MENU_MSG_RELEASE_FRAME = 0x1000068,                 // one of the menu's frames (value2) released
    MENU_MSG_ADD_TICKED = 0x200007A,                    // a control (value2) ticked by the menu
    MENU_MSG_REMOVE_TICKED = 0x200007B,
    MENU_MSG_KEYBOARD_OFF = 0x20000C2,                  // value != 0: the keys and the clicks off
};
// The screen asked whether to quit (MENU_MSG_BACK on the last screen opens it; back from it closes it).
constexpr uint32_t QUIT_SCREEN = 4;

// The screens shown, the last on top (all the menus'; one menu state at a time).
extern std::deque<C_Screen *> g_menuScreens;            // 0x008ae5c4
// The messages kept back (MENU_MSG_DEFER; a static of HandleMessage in the original).
extern std::deque<S_ControlMessage> g_deferredMessages;   // 0x008ae55c
// The menu state made last (cleared when it is deleted).
extern class C_MenuState *g_menuState;                  // 0x008ae5f8

class C_MenuState : public C_GameState {                // 0x1B0 bytes, vtable 0x00815850
public:
    C_MenuState();                                      // 0x0067edd0
    ~C_MenuState() override;                            // 0x0067f080 (0x0067f060)
    C_MenuState(const C_MenuState &) = delete;
    C_MenuState &operator=(const C_MenuState &) = delete;

    // (Part 9d-ii-b.)
    bool Start(void *arg) override;                     // 0x04 0x0066b010
    bool Stop() override;                               // 0x08 0x0066b440
    bool HandleInput(float *held, float *pressed) override;   // 0x0C 0x0066b6e0
    // The menu's OnTick.
    void Tick(int timeDelta) override { OnTick(timeDelta); }   // 0x10 0x0066c190
    void Update(int timeDelta) override;                // 0x14 0x0066c1a0
    void Render() override;                             // 0x18 0x0066e100
    // (0x1C: the base's, 0.)

    // The menus' own: a message to target 0 not the base's; their screens' descriptions made.
    virtual void OnMessage(S_ControlMessage *message) = 0;   // 0x34
    virtual bool BuildScreens() = 0;                    // 0x38
    // Called by Stop, Start (the argument; its result dropped), HandleInput, Update (twice),
    // Tick, Render (true: the menu drew) and DrawScreens (its result dropped).
    virtual void OnStop() {}                            // 0x3C 0x0067ed60
    virtual bool OnStart(void *) { return true; }       // 0x40 0x0067ed70
    virtual bool OnInput(float *, float *) { return true; }   // 0x44 0x0067ed80
    virtual void OnUpdate(int) {}                       // 0x48 0x0067ed90
    virtual void OnTick(int) {}                         // 0x4C 0x0067eda0
    virtual bool OnRender() { return false; }           // 0x50 0x0067edb0
    virtual bool OnDrawScreens() { return false; }      // 0x54 0x0067edc0
    // The top screen and the one under its popup drawn, the menu's, the popup, the hint (9d-ii-b).
    virtual void DrawScreens();                         // 0x58 0x0066e300

    // A message: kept back or the kept ones posted / dropped (MENU_MSG_DEFER, MENU_MSG_DEFERRED);
    // to a screen by the target's top 6 bits; else the menu's (MENU_MSG_*, the message box's,
    // CONTROL_MSG_NOTIFY's cursor input to the top, the editing), the rest to OnMessage.
    void HandleMessage(S_ControlMessage *message);      // 0x0066cc50
    // The queued messages handled, the first first (those posted meanwhile too).
    void HandleMessages();                              // 0x0066ca70
    // The control with the id in its screen (by its top 6 bits; none, id 0: null).
    C_Control *FindControl(uint32_t id);                // 0x0066da30
    // The message box's (none made: nothing).
    void ShowMessage(int kind, const C_str &text, uint32_t yesMessage, uint32_t noMessage);   // 0x0066da80
    void ShowMessageId(int kind, uint32_t textId, uint32_t yesMessage, uint32_t noMessage);   // 0x0066daa0
    void CloseMessage();                                // 0x0066dac0 (__fastcall)

protected:
    uint32_t m_hoveredId = 0xFFFFFFFF;                  // +0x0C the id under the cursor last update
    C_Control *m_editing = nullptr;                     // +0x10 the control the keys go to
    bool m_timing = false;                              // +0x14
    int m_time = 0;                                     // +0x18 (timed while m_timing)
    int m_unknown1C = 250;                              // +0x1C
    int m_mode = 0;                                     // +0x20 (Start's)
    int m_inputLock = -1;                               // +0x24 the cursor's input only at -1
    int m_unknown28 = 0;                                // +0x28
    bool m_dragging = false;                            // +0x2C
    bool m_keyboardOff = false;                         // +0x2D
    I3D_scene *m_scene = nullptr;                       // +0x30
    I3D_camera *m_camera = nullptr;                     // +0x34
    I3D_light *m_light = nullptr;                       // +0x38
    I3D_sound *m_clickSound = nullptr;                  // +0x3C
    S_MenuHint m_hint;                                  // +0x40
    void *m_startArg = nullptr;                         // +0xF0 Start's (a campaign node, or 0 / -1 / -2)
    C_Cursor *m_cursor = nullptr;                       // +0xF4
    C_Screen *m_underPopup = nullptr;                   // +0xF8 the top when the popup was opened
    C_Screen *m_popup = nullptr;                        // +0xFC drawn over the screens when it has one
    C_MessageBox *m_messageBox = nullptr;               // +0x100
    std::vector<I3D_frame *> m_models;                  // +0x104 the screens' models (released by Stop)
    std::vector<I3D_frame *> m_frames;                  // +0x118 frames the menus made (their trees released)
    std::vector<C_Control *> m_tickedControls;          // +0x12C
    std::vector<C_Screen *> m_screens;                  // +0x140 by the ids' top 6 bits
    S_SoundRequest m_sound;                             // +0x160
};
