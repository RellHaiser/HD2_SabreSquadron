// The base of the in-game menu (C_GameMenu) and the equipment screen (C_EquipmentMenu): a menu state
// with an equipment panel, whose item controls call its slots 0x5C..0x70 (vtable 0x008157d8; its
// destructor is C_MenuState's, inlined: 0x0067f1f0, deleting 0x0067f350). Part 9e-ii-b declares the
// slots the panel's description needs; the rest of the class is 9e-iii.
#pragma once

#include <cstdint>

#include "game/menu_state.h"

class C_Control;

class C_EquipmentMenuBase : public C_MenuState {
public:
    // The item controls' notifications (the panel's callbacks; the arguments a callback's). The
    // base's answer true.
    virtual bool OnItemNotify13(void *, C_Control *, uint32_t, uint32_t) { return true; }   // 0x5C 0x0067f300
    virtual bool OnItemNotify60(void *argument, C_Control *control, uint32_t notification,
                                uint32_t value) = 0;    // 0x60
    virtual bool OnItemNotify0A(void *, C_Control *, uint32_t, uint32_t) { return true; }   // 0x64 0x0067f310
    virtual bool OnItemNotify08(void *, C_Control *, uint32_t, uint32_t) { return true; }   // 0x68 0x0067f320
    virtual bool OnItemNotify09(void *, C_Control *, uint32_t, uint32_t) { return true; }   // 0x6C 0x0067f330
    // The cursor entering or leaving an item control.
    virtual bool OnItemHover(void *, C_Control *, uint32_t, uint32_t) { return true; }      // 0x70 0x0067f340

    // The controls' callbacks: the slots above on the menu (the callbacks' object; thunks 0x00672fb0,
    // 0x00672fc0, 0x00672fd0, 0x00672fe0, 0x00672ff0).
    static bool CallItemNotify13(void *menu, void *argument, C_Control *control, uint32_t notification,
                                 uint32_t value);
    static bool CallItemNotify0A(void *menu, void *argument, C_Control *control, uint32_t notification,
                                 uint32_t value);
    static bool CallItemNotify08(void *menu, void *argument, C_Control *control, uint32_t notification,
                                 uint32_t value);
    static bool CallItemNotify09(void *menu, void *argument, C_Control *control, uint32_t notification,
                                 uint32_t value);
    static bool CallItemHover(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);
};
