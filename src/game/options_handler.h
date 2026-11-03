// The options screen's messages, the main menu's and the in-game menu's (0x0C bytes; C_GameMenu
// +0x1EC): opening it (its controls filled from the player's options the first time), saving the
// options, leaving it (not with a function without a key: the key bindings' page showing it).
// Original 0x0061bf00..0x00620e90 (part 9e-viii).
#pragma once

#include <cstdint>

class C_Cursor;
class C_MenuState;
class I3D_frame;
struct S_ControlMessage;
struct S_ScreenDesc;

class C_OptionsHandler {
public:
    explicit C_OptionsHandler(C_MenuState *menu) : m_menu(menu) {}
    C_OptionsHandler(const C_OptionsHandler &) = delete;
    C_OptionsHandler &operator=(const C_OptionsHandler &) = delete;
    // The cursor released.
    ~C_OptionsHandler();                                // 0x0061cfa0 (__fastcall)

    // True: an options message, handled. Escape on the options screen: leaving it; the options
    // saved and applied (with the key bindings changed); the screen opened (the first time its
    // controls filled); leaving: a function without a key asked about, else the pages hidden, the
    // options saved and back; the unbound function's page, group and place shown.
    bool HandleMessage(S_ControlMessage *message);      // 0x0061bf00
    // The options screen's controls described (its index kept; not ported yet).
    bool BuildScreen(S_ScreenDesc *desc, I3D_frame *model);   // 0x0061cfc0

private:
    // The controls of the screen's pages from the player's options.
    void Fill();
    // The key bindings' groups refreshed, the one the group list chooses last; false without the
    // list.
    bool RefreshKeyGroups();                            // 0x00620be0 (__fastcall)
    // The id of the options screen's control (the screen's index in the top 6 bits).
    uint32_t Id(uint32_t control) const { return static_cast<uint32_t>(m_screen) << 26 | control; }

    C_Cursor *m_cursor = nullptr;                       // +0x00
    C_MenuState *m_menu;                                // +0x04
    bool m_fillOnOpen = true;                           // +0x08
    uint8_t m_screen = 0;                               // +0x09 the options screen's index
};
