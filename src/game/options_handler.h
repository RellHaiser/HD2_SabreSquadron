// The options screen's messages, the main menu's and the in-game menu's (0x0C bytes; C_GameMenu
// +0x1EC): opening it (its controls filled from the player's options the first time), saving the
// options, leaving it (not with a function without a key: the key bindings' page showing it).
// Original 0x0061bf00..0x00620e90 (part 9e-viii; 9e-x: the screen's description; 9e-xi: the video,
// sound, game and multiplayer pages; 9e-xii: the controls and keys pages).
#pragma once

#include <cstdint>

#include "common/c_str.h"

class C_Control;
class C_Cursor;
class C_MenuState;
class I3D_frame;
struct S_ControlDesc;
struct S_ControlMessage;
struct S_ScreenDesc;

class C_OptionsHandler {
public:
    explicit C_OptionsHandler(C_MenuState *menu) : m_menu(menu) {}
    C_OptionsHandler(const C_OptionsHandler &) = delete;
    C_OptionsHandler &operator=(const C_OptionsHandler &) = delete;
    // The cursor released.
    ~C_OptionsHandler();                                // 0x0061cfa0 (DS 0x00649ce0) (__fastcall)

    // True: an options message, handled. Escape on the options screen: leaving it; the options
    // saved and applied (with the key bindings changed); the screen opened (the first time its
    // controls filled); leaving: a function without a key asked about, else the pages hidden, the
    // options saved and back; the unbound function's page, group and place shown.
    bool HandleMessage(S_ControlMessage *message);      // 0x0061bf00
    // The options screen's description: its index kept, the player's name shown when the screen is,
    // the screen's texts, a group (a dummy frame "GROptions" under the model, the pages' buttons
    // moved under it) of the pages' buttons (each showing its page; back leaving), the pages'
    // controls, the version's text. False (nothing described) without the group's frame or one of
    // the buttons' frames.
    bool BuildScreen(S_ScreenDesc *desc, I3D_frame *model);   // 0x0061cfc0
    // The controls filled again the next time the screen opens (the main menu's profile picked).
    void RequestFill() { m_fillOnOpen = true; }
    // The screen shown: the player's name (none: its text) on the screen's third control.
    static bool OnShown(void *handler, void *argument, C_Control *control, uint32_t notification,
                        uint32_t value);                // 0x00620530 (__thiscall)

private:
    // The controls of the screen's pages from the player's options.
    void Fill();
    // The key bindings' groups refreshed, the one the group list chooses last; false without the
    // list.
    bool RefreshKeyGroups();                            // 0x00620be0 (__fastcall)
    // The pages: each a group (hidden; shown, the others hidden) of its texts and controls. True.
    // The video page: the model's detail switches' frames named GD00..GD06 first; the gamma, the
    // detail level, the switches (the morphing's inactive in a game; the high models' hidden).
    bool BuildVideoPage(S_ScreenDesc *desc, I3D_frame *model);         // 0x0061dc20 (page 0x600000)
    // The sound and music volumes.
    bool BuildSoundPage(S_ScreenDesc *desc, I3D_frame *model);         // 0x0061e1a0 (0x700000)
    // The controls page (the mouse; the joystick's hidden) and the keys page (0x900000; shown by the
    // controls page's button): the groups' list, its scroll bar, the five groups' key lists (false
    // without the "hkinput" dummy or the "hktext" visual: none), the layouts' buttons.
    bool BuildControlsPage(S_ScreenDesc *desc, I3D_frame *model);      // 0x0061e3f0 (0x800000)
    // A key group's list under `page`: a dummy named `name` under "keysscreen01" (the menu's model)
    // holding a copy of "hkinput" per binding of the group (by the bindings' order, 0.036 apart); the
    // list's bindings (each refreshing the groups when changed); the first group shown. False
    // without the frames.
    bool BuildKeyGroup(S_ControlDesc *page, I3D_frame *model, C_str name, int group);   // 0x0061f600
    // The subtitles and the crosshair (inactive in a game).
    bool BuildGamePage(S_ScreenDesc *desc, I3D_frame *model);          // 0x0061f230 (0xA00000)
    // The connection (its group hidden twice: the original's), the voice chat and its volume.
    bool BuildMultiplayerPage(S_ScreenDesc *desc, I3D_frame *model);   // 0x0061f3b0 (0xB00000)
    // The pages' callbacks (their controls' CONTROL_NOTIFY_CHANGED; false: not theirs). The sliders:
    // the gamma, the volumes (the sound's with a click; the music's below 1 off), the mouse, the voice.
    static bool OnSlider(void *handler, void *argument, C_Control *control, uint32_t notification,
                         uint32_t value);               // 0x00620300 (__thiscall)
    // The detail level (the switches shown by it, active at level 3 only) and the switches.
    static bool OnDetail(void *handler, void *argument, C_Control *control, uint32_t notification,
                         uint32_t value);               // 0x0061fd00 (__thiscall)
    // The check boxes: the mouse's inversion, the subtitles, the crosshair.
    static bool OnCheck(void *handler, void *argument, C_Control *control, uint32_t notification,
                        uint32_t value);                // 0x006204a0 (__thiscall)
    // The voice chat (its volume active with it).
    static bool OnVoiceChat(void *handler, void *argument, C_Control *control, uint32_t notification,
                            uint32_t value);            // 0x00620780 (__thiscall)
    // The groups' list: the chosen group's list shown, the scroll bar's, refreshed; the others hidden.
    static bool OnKeyGroup(void *handler, void *argument, C_Control *control, uint32_t notification,
                           uint32_t value);             // 0x006209c0 (__thiscall)
    // A binding changed: the groups refreshed.
    static bool OnKeyChanged(void *handler, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x006209b0 (__thiscall)
    // The layouts' buttons: the player's key layout (0 the defaults', 1 the other's), the groups refreshed.
    static bool OnKeyLayout(void *handler, void *argument, C_Control *control, uint32_t notification,
                            uint32_t value);            // 0x0061fcb0 (__thiscall)
    // A page's group: hidden; when shown, the other pages hidden.
    S_ControlDesc *AddPage(S_ScreenDesc *desc, const char *name, uint32_t page);
    // The id of the options screen's control (the screen's index in the top 6 bits).
    uint32_t Id(uint32_t control) const { return static_cast<uint32_t>(m_screen) << 26 | control; }

    C_Cursor *m_cursor = nullptr;                       // +0x00
    C_MenuState *m_menu;                                // +0x04
    bool m_fillOnOpen = true;                           // +0x08
    uint8_t m_screen = 0;                               // +0x09 the options screen's index
};
