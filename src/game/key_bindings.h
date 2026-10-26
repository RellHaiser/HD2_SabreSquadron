// The player's key bindings (C_PlayerProfile +0xEC; saved in Options.bin; 0x006a8470..
// 0x006acb10): which key or mouse button triggers each control action, the two default
// layouts, and the names of the bound keys that the menus and the texts' <Tag>s show.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"

class C_chunk;

// The input device of a binding.
enum E_BindingDevice : int16_t {
    BINDING_DEVICE_NONE = 0,                            // not bound
    BINDING_DEVICE_KEYBOARD = 1,                        // key: a DirectInput key code (DIK_*)
    BINDING_DEVICE_MOUSE = 2,                           // key: MOUSE_* below
};

// Mouse "keys": buttons are bits, the axes 0x100 (x), 0x200 (y) and 0x300 (wheel; the
// binding's direction says which way).
enum : uint16_t {
    MOUSE_BUTTON_LEFT = 0x01,
    MOUSE_BUTTON_RIGHT = 0x02,
    MOUSE_BUTTON_MIDDLE = 0x04,
    MOUSE_BUTTON_4 = 0x08,
    MOUSE_BUTTON_5 = 0x10,
    MOUSE_AXIS_X = 0x100,
    MOUSE_AXIS_Y = 0x200,
    MOUSE_WHEEL = 0x300,
};

// The control modes a binding belongs to. The player profile switches between the game's and
// the command map's (C_KeyBindings::SetMode; the command map's bindings are the 0x1CFC..
// texts); mode 1 bindings work in both.
enum E_BindingMode {
    BINDING_MODE_ALL = 1,
    BINDING_MODE_GAME = 2,
    BINDING_MODE_COMMAND_MAP = 3,                       // while the command map is open
};

// One binding (0x1C bytes, saved as they are).
struct S_KeyBinding {
    int action;                                         // +0x00 C_Controls action
    int16_t device;                                     // +0x04 E_BindingDevice
    uint16_t key;                                       // +0x06
    int direction;                                      // +0x08 axes: 1 + / 2 -
    int mode;                                           // +0x0C E_BindingMode
    int group;                                          // +0x10 the options screen's list
    int textId;                                         // +0x14 the action's name
    bool changed;                                       // +0x18 not yet given to the controls
};
static_assert(sizeof(S_KeyBinding) == 0x1C, "saved as is");

class C_KeyBindings {
public:
    C_KeyBindings() = default;                          // 0x006a8470
    ~C_KeyBindings() = default;                         // 0x006a8490

    int GetCount() const { return static_cast<int>(m_bindings.size()); }   // 0x006a8510
    // The binding's 1-based position among its neighbours of the same group (-1 for a bad
    // index).
    void GetGroupPosition(int index, int *position) const;   // 0x006a8540
    void GetTextAndGroup(int index, int *textId, int *group) const;   // 0x006a85b0
    // The binding of an action in a mode, -1 if none.
    int Find(int action, int mode) const;               // 0x006a8610
    // Binds a key. Keyboard: not the F keys, Escape and Pause; mouse: not the x and y axes.
    // Other bindings of the key in an overlapping mode lose it.
    bool Bind(int index, int device, int key, int direction);   // 0x006a8670
    // The first binding without a key, -1 if none.
    int FindUnbound() const;                            // 0x006a8860
    // Gives the bindings of mode `mode` (2 or 3) to the controls and makes it the mode.
    void SetMode(int mode);                             // 0x006a8940
    // Gives the changed bindings of the current mode to the controls.
    void ApplyChanged();                                // 0x006a8970

    // Chunks 0x22B8 (version 0x42) and 0x22B9 (the bindings).
    bool Save(C_chunk &chunk) const;                    // 0x006a8a20
    // Other versions: the default layout.
    bool Load(C_chunk &chunk);                          // 0x006a8bb0
    // Layout 0: the arrow keys, 1: W A S D.
    void SetDefaults(int layout);                       // 0x006a8e90

    // What the menus show for a binding: the key's name, or the mouse's with "+"/"-".
    C_str GetBindingName(int index) const;              // 0x006ab840
    // Replaces the first of the texts' <Move_forward> .. <Select_soldier_4> tags found in
    // `text` with the quoted name of its key; false (and `text` unchanged) if there is none.
    bool ReplaceTags(C_str *out, const char *text) const;   // 0x006ac470

private:
    // Keys a keyboard binding may not use: F1..F12, Escape, Pause.
    static bool IsReservedKey(uint8_t key);             // 0x006a84e0
    // Unbinds the other bindings of the binding's key in an overlapping mode.
    void ResolveConflicts(int index);                   // 0x006a8730
    static void Apply(S_KeyBinding &binding);           // 0x006a88b0
    void ApplyMode(int mode);                           // 0x006a88e0
    void SetDefaultsArrows();                           // 0x006a8eb0
    void SetDefaultsWasd();                             // 0x006aa340
    void AddBindings(const S_KeyBinding *bindings, int count);
    bool ReplaceTag(C_str *out, const char *text, int tag) const;   // 0x006ac130

    std::vector<S_KeyBinding> m_bindings;               // +0x00
    int m_mode = BINDING_MODE_GAME;                     // +0x10
};

// The name DirectInput gives a key, first letter capitalised.
C_str GetKeyName(uint8_t key);                          // 0x006ab7d0
