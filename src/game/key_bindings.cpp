// The player's key bindings (see key_bindings.h).
#include "game/key_bindings.h"

#include <cstring>

#include "common/chunk.h"
#include "game/pending.h"
#include "game/profile.h"
#include "ls3d/igraph.h"

namespace {

enum : uint16_t {
    CHUNK_BINDINGS_VERSION = 0x22B8,
    CHUNK_BINDINGS = 0x22B9,
};
constexpr int BINDINGS_VERSION = 0x42;

// Keys a keyboard binding may not take: F1..F10, F11, F12, Escape, Pause.
const uint8_t RESERVED_KEYS[] = {0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,
                                 0x57, 0x58, 0x01, 0xC5, 0x00};   // 0x00842570

// Two actions that may share a key (their default is P).
constexpr int TEXT_SHARED_KEY_A = 0xB28;
constexpr int TEXT_SHARED_KEY_B = 0xB20;

// The tags the texts use for the keys of actions (action, mode).
struct S_KeyTag {
    const char *tag;
    int action;
    int mode;
};
const S_KeyTag KEY_TAGS[] = {                       // 0x00842580
    {"<Move_forward>", 0, 2},
    {"<Move_back>", 1, 2},
    {"<Strafe_left>", 2, 2},
    {"<Strafe_right>", 3, 2},
    {"<Increase_speed>", 5, 2},
    {"<Decrease_speed>", 4, 2},
    {"<Lean_left>", 10, 2},
    {"<Lean_right>", 11, 2},
    {"<Jump_key>", 12, 2},
    {"<Climb>", 17, 2},
    {"<Crouch>", 8, 2},
    {"<Lie_down>", 9, 2},
    {"<Primary_fire>", 6, 2},
    {"<Secondary_action>", 7, 2},
    {"<Throw_grenade>", 16, 2},
    {"<Reload>", 13, 2},
    {"<Previous_weapon>", 32, 2},
    {"<Next_weapon>", 33, 2},
    {"<Drop_item>", 34, 2},
    {"<Action_button>", 19, 1},
    {"<Action_pull_down_menu>", 20, 1},
    {"<GameInfo>", 21, 1},
    {"<Tactical_map>", 22, 1},
    {"<Interface_mode>", 23, 1},
    {"<Map>", 24, 1},
    {"<Inventory_key>", 25, 1},
    {"<Pause>", 31, 1},
    {"<Binoculars>", 15, 2},
    {"<Camera>", 26, 1},
    {"<Console>", 27, 1},
    {"<Hand_brake>", 18, 2},
    {"<SwitchPlayer>", 30, 1},
    {"<Select_soldier_1>", 35, 1},
    {"<Select_soldier_2>", 36, 1},
    {"<Select_soldier_3>", 37, 1},
    {"<Select_soldier_4>", 38, 1},
    {nullptr, 0, 0},
};

// The default layouts (built one insert at a time in the original; all changed).
// {action, device, key, direction, mode, group, textId, changed}
const S_KeyBinding DEFAULTS_ARROWS[] = {            // 0x006a8eb0
    {0, BINDING_DEVICE_KEYBOARD, 0xC8, 0, 2, 0, 0xAF1, true},
    {1, BINDING_DEVICE_KEYBOARD, 0xD0, 0, 2, 0, 0xAF2, true},
    {2, BINDING_DEVICE_KEYBOARD, 0xCB, 0, 2, 0, 0xAF3, true},
    {3, BINDING_DEVICE_KEYBOARD, 0xCD, 0, 2, 0, 0xAF4, true},
    {5, BINDING_DEVICE_MOUSE, 0x300, 1, 2, 0, 0xAF5, true},
    {4, BINDING_DEVICE_MOUSE, 0x300, 2, 2, 0, 0xAF6, true},
    {10, BINDING_DEVICE_KEYBOARD, 0xD3, 0, 2, 0, 0xAF7, true},
    {11, BINDING_DEVICE_KEYBOARD, 0xD1, 0, 2, 0, 0xAF8, true},
    {12, BINDING_DEVICE_KEYBOARD, 0x24, 0, 2, 0, 0xAF9, true},
    {17, BINDING_DEVICE_KEYBOARD, 0xC7, 0, 2, 0, 0xAFA, true},
    {8, BINDING_DEVICE_KEYBOARD, 0x9D, 0, 2, 0, 0xAFB, true},
    {9, BINDING_DEVICE_KEYBOARD, 0x36, 0, 2, 0, 0xAFC, true},
    {6, BINDING_DEVICE_MOUSE, 0x1, 0, 2, 1, 0xB05, true},
    {7, BINDING_DEVICE_KEYBOARD, 0xCF, 0, 2, 1, 0xB06, true},
    {16, BINDING_DEVICE_KEYBOARD, 0x22, 0, 2, 1, 0xB07, true},
    {13, BINDING_DEVICE_KEYBOARD, 0x1C, 0, 2, 1, 0xB08, true},
    {32, BINDING_DEVICE_KEYBOARD, 0x1A, 0, 2, 1, 0xB09, true},
    {33, BINDING_DEVICE_KEYBOARD, 0x1B, 0, 2, 1, 0xB0A, true},
    {34, BINDING_DEVICE_KEYBOARD, 0xE, 0, 2, 1, 0xB0B, true},
    {19, BINDING_DEVICE_MOUSE, 0x2, 0, 1, 2, 0xB19, true},
    {20, BINDING_DEVICE_MOUSE, 0x4, 0, 1, 2, 0xB1A, true},
    {21, BINDING_DEVICE_KEYBOARD, 0x1F, 0, 1, 2, 0xB1B, true},
    {22, BINDING_DEVICE_KEYBOARD, 0x39, 0, 1, 2, 0xB1C, true},
    {62, BINDING_DEVICE_KEYBOARD, 0x19, 0, 2, 2, 0xB28, true},
    {23, BINDING_DEVICE_KEYBOARD, 0x2F, 0, 2, 2, 0xB1D, true},
    {24, BINDING_DEVICE_KEYBOARD, 0x32, 0, 1, 2, 0xB1E, true},
    {25, BINDING_DEVICE_KEYBOARD, 0x17, 0, 1, 2, 0xB1F, true},
    {31, BINDING_DEVICE_KEYBOARD, 0x19, 0, 1, 2, 0xB20, true},
    {15, BINDING_DEVICE_KEYBOARD, 0x30, 0, 2, 2, 0xB21, true},
    {26, BINDING_DEVICE_KEYBOARD, 0x2E, 0, 2, 2, 0xB23, true},
    {27, BINDING_DEVICE_KEYBOARD, 0x29, 0, 1, 2, 0xB24, true},
    {28, BINDING_DEVICE_KEYBOARD, 0x14, 0, 1, 2, 0xB25, true},
    {29, BINDING_DEVICE_KEYBOARD, 0x15, 0, 1, 2, 0xB26, true},
    {18, BINDING_DEVICE_KEYBOARD, 0x38, 0, 2, 2, 0xB27, true},
    {30, BINDING_DEVICE_KEYBOARD, 0xF, 0, 1, 3, 0xB2D, true},
    {35, BINDING_DEVICE_KEYBOARD, 0x2, 0, 1, 3, 0xB2E, true},
    {36, BINDING_DEVICE_KEYBOARD, 0x3, 0, 1, 3, 0xB2F, true},
    {37, BINDING_DEVICE_KEYBOARD, 0x4, 0, 1, 3, 0xB30, true},
    {38, BINDING_DEVICE_KEYBOARD, 0x5, 0, 1, 3, 0xB31, true},
    {39, BINDING_DEVICE_KEYBOARD, 0x52, 0, 2, 3, 0xB32, true},
    {40, BINDING_DEVICE_KEYBOARD, 0x4F, 0, 2, 3, 0xB33, true},
    {41, BINDING_DEVICE_KEYBOARD, 0x50, 0, 2, 3, 0xB34, true},
    {42, BINDING_DEVICE_KEYBOARD, 0x51, 0, 2, 3, 0xB35, true},
    {43, BINDING_DEVICE_KEYBOARD, 0x4B, 0, 2, 3, 0xB36, true},
    {44, BINDING_DEVICE_KEYBOARD, 0x4C, 0, 2, 3, 0xB37, true},
    {45, BINDING_DEVICE_KEYBOARD, 0x4D, 0, 2, 3, 0xB38, true},
    {47, BINDING_DEVICE_KEYBOARD, 0x3E, 0, 1, 0, 0x0, true},
    {48, BINDING_DEVICE_KEYBOARD, 0x42, 0, 1, 0, 0x0, true},
    {46, BINDING_DEVICE_KEYBOARD, 0x1, 0, 1, 0, 0x0, true},
    {50, BINDING_DEVICE_KEYBOARD, 0x1D, 0, 1, 0, 0x0, true},
    {49, BINDING_DEVICE_KEYBOARD, 0x36, 0, 1, 0, 0x0, true},
    {51, BINDING_DEVICE_KEYBOARD, 0x38, 0, 1, 0, 0x0, true},
    {52, BINDING_DEVICE_MOUSE, 0x100, 0, 1, 0, 0x0, true},
    {53, BINDING_DEVICE_MOUSE, 0x200, 0, 1, 0, 0x0, true},
    {54, BINDING_DEVICE_MOUSE, 0x300, 0, 1, 0, 0x0, true},
    {55, BINDING_DEVICE_MOUSE, 0x1, 0, 1, 0, 0x0, true},
    {56, BINDING_DEVICE_MOUSE, 0x2, 0, 1, 0, 0x0, true},
    {57, BINDING_DEVICE_MOUSE, 0x4, 0, 1, 0, 0x0, true},
    {58, BINDING_DEVICE_KEYBOARD, 0x3B, 0, 1, 0, 0x0, true},
    {59, BINDING_DEVICE_KEYBOARD, 0xD2, 0, 1, 0, 0x0, true},
    {60, BINDING_DEVICE_KEYBOARD, 0xC9, 0, 1, 0, 0x0, true},
    {61, BINDING_DEVICE_KEYBOARD, 0x1D, 0, 1, 0, 0x0, true},
    {0, BINDING_DEVICE_KEYBOARD, 0x22, 0, 3, 4, 0x1CFC, true},
    {1, BINDING_DEVICE_KEYBOARD, 0x23, 0, 3, 4, 0x1CFD, true},
    {2, BINDING_DEVICE_KEYBOARD, 0x24, 0, 3, 4, 0x1CFE, true},
    {3, BINDING_DEVICE_KEYBOARD, 0x20, 0, 3, 4, 0x1CFF, true},
    {4, BINDING_DEVICE_KEYBOARD, 0x1F, 0, 3, 4, 0x1D00, true},
    {5, BINDING_DEVICE_KEYBOARD, 0x1E, 0, 3, 4, 0x1D01, true},
    {6, BINDING_DEVICE_KEYBOARD, 0x10, 0, 3, 4, 0x1D02, true},
    {7, BINDING_DEVICE_KEYBOARD, 0x11, 0, 3, 4, 0x1D03, true},
    {8, BINDING_DEVICE_KEYBOARD, 0x12, 0, 3, 4, 0x1D04, true},
    {9, BINDING_DEVICE_KEYBOARD, 0x13, 0, 3, 4, 0x1D05, true},
    {10, BINDING_DEVICE_KEYBOARD, 0x2C, 0, 3, 4, 0x1D06, true},
    {11, BINDING_DEVICE_KEYBOARD, 0x2D, 0, 3, 4, 0x1D07, true},
    {12, BINDING_DEVICE_KEYBOARD, 0x2E, 0, 3, 4, 0x1D08, true},
    {13, BINDING_DEVICE_KEYBOARD, 0x52, 0, 3, 4, 0x1D09, true},
    {15, BINDING_DEVICE_KEYBOARD, 0x53, 0, 3, 4, 0x1D0A, true},
    {16, BINDING_DEVICE_KEYBOARD, 0xB5, 0, 3, 4, 0x1D0B, true},
    {17, BINDING_DEVICE_KEYBOARD, 0x37, 0, 3, 4, 0x1D0C, true},
    {18, BINDING_DEVICE_KEYBOARD, 0xC8, 0, 3, 4, 0x1D0D, true},
    {32, BINDING_DEVICE_KEYBOARD, 0xD0, 0, 3, 4, 0x1D0E, true},
    {33, BINDING_DEVICE_KEYBOARD, 0xCB, 0, 3, 4, 0x1D0F, true},
    {34, BINDING_DEVICE_KEYBOARD, 0xCD, 0, 3, 4, 0x1D10, true},
    {39, BINDING_DEVICE_KEYBOARD, 0xC9, 0, 3, 4, 0x1D11, true},
    {40, BINDING_DEVICE_KEYBOARD, 0xD1, 0, 3, 4, 0x1D12, true},
    {41, BINDING_DEVICE_KEYBOARD, 0x1D, 0, 3, 4, 0x1D13, true},
    {42, BINDING_DEVICE_KEYBOARD, 0x4B, 0, 3, 4, 0x1D14, true},
    {43, BINDING_DEVICE_KEYBOARD, 0x4D, 0, 3, 4, 0x1D15, true},
    {44, BINDING_DEVICE_KEYBOARD, 0x48, 0, 3, 4, 0x1D16, true},
    {45, BINDING_DEVICE_KEYBOARD, 0x50, 0, 3, 4, 0x1D17, true},
    {62, BINDING_DEVICE_KEYBOARD, 0x4E, 0, 3, 4, 0x1D18, true},
    {63, BINDING_DEVICE_KEYBOARD, 0x4A, 0, 3, 4, 0x1D19, true},
};
const S_KeyBinding DEFAULTS_WASD[] = {              // 0x006aa340
    {0, BINDING_DEVICE_KEYBOARD, 0x11, 0, 2, 0, 0xAF1, true},
    {1, BINDING_DEVICE_KEYBOARD, 0x1F, 0, 2, 0, 0xAF2, true},
    {2, BINDING_DEVICE_KEYBOARD, 0x1E, 0, 2, 0, 0xAF3, true},
    {3, BINDING_DEVICE_KEYBOARD, 0x20, 0, 2, 0, 0xAF4, true},
    {5, BINDING_DEVICE_MOUSE, 0x300, 1, 2, 0, 0xAF5, true},
    {4, BINDING_DEVICE_MOUSE, 0x300, 2, 2, 0, 0xAF6, true},
    {10, BINDING_DEVICE_KEYBOARD, 0x10, 0, 2, 0, 0xAF7, true},
    {11, BINDING_DEVICE_KEYBOARD, 0x12, 0, 2, 0, 0xAF8, true},
    {12, BINDING_DEVICE_MOUSE, 0x4, 0, 2, 0, 0xAF9, true},
    {17, BINDING_DEVICE_KEYBOARD, 0x2D, 0, 2, 0, 0xAFA, true},
    {8, BINDING_DEVICE_KEYBOARD, 0x2A, 0, 2, 0, 0xAFB, true},
    {9, BINDING_DEVICE_KEYBOARD, 0x1D, 0, 2, 0, 0xAFC, true},
    {6, BINDING_DEVICE_MOUSE, 0x1, 0, 2, 1, 0xB05, true},
    {7, BINDING_DEVICE_MOUSE, 0x2, 0, 2, 1, 0xB06, true},
    {16, BINDING_DEVICE_KEYBOARD, 0x22, 0, 2, 1, 0xB07, true},
    {13, BINDING_DEVICE_KEYBOARD, 0x13, 0, 2, 1, 0xB08, true},
    {32, BINDING_DEVICE_KEYBOARD, 0x2C, 0, 2, 1, 0xB09, true},
    {33, BINDING_DEVICE_KEYBOARD, 0x38, 0, 2, 1, 0xB0A, true},
    {34, BINDING_DEVICE_KEYBOARD, 0xE, 0, 2, 1, 0xB0B, true},
    {19, BINDING_DEVICE_KEYBOARD, 0x39, 0, 1, 2, 0xB19, true},
    {20, BINDING_DEVICE_KEYBOARD, 0x28, 0, 1, 2, 0xB1A, true},
    {21, BINDING_DEVICE_KEYBOARD, 0x33, 0, 1, 2, 0xB1B, true},
    {22, BINDING_DEVICE_KEYBOARD, 0x3A, 0, 1, 2, 0xB1C, true},
    {62, BINDING_DEVICE_KEYBOARD, 0x19, 0, 2, 2, 0xB28, true},
    {23, BINDING_DEVICE_KEYBOARD, 0x2F, 0, 2, 2, 0xB1D, true},
    {24, BINDING_DEVICE_KEYBOARD, 0x32, 0, 1, 2, 0xB1E, true},
    {25, BINDING_DEVICE_KEYBOARD, 0x17, 0, 1, 2, 0xB1F, true},
    {31, BINDING_DEVICE_KEYBOARD, 0x19, 0, 1, 2, 0xB20, true},
    {15, BINDING_DEVICE_KEYBOARD, 0x30, 0, 2, 2, 0xB21, true},
    {26, BINDING_DEVICE_KEYBOARD, 0x2E, 0, 2, 2, 0xB23, true},
    {27, BINDING_DEVICE_KEYBOARD, 0x29, 0, 1, 2, 0xB24, true},
    {28, BINDING_DEVICE_KEYBOARD, 0x14, 0, 1, 2, 0xB25, true},
    {29, BINDING_DEVICE_KEYBOARD, 0x15, 0, 1, 2, 0xB26, true},
    {18, BINDING_DEVICE_KEYBOARD, 0xB8, 0, 2, 2, 0xB27, true},
    {30, BINDING_DEVICE_KEYBOARD, 0xF, 0, 1, 3, 0xB2D, true},
    {35, BINDING_DEVICE_KEYBOARD, 0x2, 0, 1, 3, 0xB2E, true},
    {36, BINDING_DEVICE_KEYBOARD, 0x3, 0, 1, 3, 0xB2F, true},
    {37, BINDING_DEVICE_KEYBOARD, 0x4, 0, 1, 3, 0xB30, true},
    {38, BINDING_DEVICE_KEYBOARD, 0x5, 0, 1, 3, 0xB31, true},
    {39, BINDING_DEVICE_KEYBOARD, 0x52, 0, 2, 3, 0xB32, true},
    {40, BINDING_DEVICE_KEYBOARD, 0x4F, 0, 2, 3, 0xB33, true},
    {41, BINDING_DEVICE_KEYBOARD, 0x50, 0, 2, 3, 0xB34, true},
    {42, BINDING_DEVICE_KEYBOARD, 0x51, 0, 2, 3, 0xB35, true},
    {43, BINDING_DEVICE_KEYBOARD, 0x4B, 0, 2, 3, 0xB36, true},
    {44, BINDING_DEVICE_KEYBOARD, 0x4C, 0, 2, 3, 0xB37, true},
    {45, BINDING_DEVICE_KEYBOARD, 0x4D, 0, 2, 3, 0xB38, true},
    {47, BINDING_DEVICE_KEYBOARD, 0x3E, 0, 1, 0, 0x0, true},
    {48, BINDING_DEVICE_KEYBOARD, 0x42, 0, 1, 0, 0x0, true},
    {46, BINDING_DEVICE_KEYBOARD, 0x1, 0, 1, 0, 0x0, true},
    {50, BINDING_DEVICE_KEYBOARD, 0x1D, 0, 1, 0, 0x0, true},
    {49, BINDING_DEVICE_KEYBOARD, 0x36, 0, 1, 0, 0x0, true},
    {51, BINDING_DEVICE_KEYBOARD, 0x38, 0, 1, 0, 0x0, true},
    {52, BINDING_DEVICE_MOUSE, 0x100, 0, 1, 0, 0x0, true},
    {53, BINDING_DEVICE_MOUSE, 0x200, 0, 1, 0, 0x0, true},
    {54, BINDING_DEVICE_MOUSE, 0x300, 0, 1, 0, 0x0, true},
    {55, BINDING_DEVICE_MOUSE, 0x1, 0, 1, 0, 0x0, true},
    {56, BINDING_DEVICE_MOUSE, 0x2, 0, 1, 0, 0x0, true},
    {57, BINDING_DEVICE_MOUSE, 0x4, 0, 1, 0, 0x0, true},
    {58, BINDING_DEVICE_KEYBOARD, 0x3B, 0, 1, 0, 0x0, true},
    {59, BINDING_DEVICE_KEYBOARD, 0xD2, 0, 1, 0, 0x0, true},
    {60, BINDING_DEVICE_KEYBOARD, 0xC9, 0, 1, 0, 0x0, true},
    {61, BINDING_DEVICE_KEYBOARD, 0x9D, 0, 1, 0, 0x0, true},
    {0, BINDING_DEVICE_KEYBOARD, 0x22, 0, 3, 4, 0x1CFC, true},
    {1, BINDING_DEVICE_KEYBOARD, 0x2F, 0, 3, 4, 0x1CFD, true},
    {2, BINDING_DEVICE_KEYBOARD, 0x30, 0, 3, 4, 0x1CFE, true},
    {3, BINDING_DEVICE_KEYBOARD, 0x10, 0, 3, 4, 0x1CFF, true},
    {4, BINDING_DEVICE_KEYBOARD, 0x11, 0, 3, 4, 0x1D00, true},
    {5, BINDING_DEVICE_KEYBOARD, 0x12, 0, 3, 4, 0x1D01, true},
    {6, BINDING_DEVICE_KEYBOARD, 0x1E, 0, 3, 4, 0x1D02, true},
    {7, BINDING_DEVICE_KEYBOARD, 0x1F, 0, 3, 4, 0x1D03, true},
    {8, BINDING_DEVICE_KEYBOARD, 0x20, 0, 3, 4, 0x1D04, true},
    {9, BINDING_DEVICE_KEYBOARD, 0x21, 0, 3, 4, 0x1D05, true},
    {10, BINDING_DEVICE_KEYBOARD, 0x2E, 0, 3, 4, 0x1D06, true},
    {11, BINDING_DEVICE_KEYBOARD, 0x2D, 0, 3, 4, 0x1D07, true},
    {12, BINDING_DEVICE_KEYBOARD, 0x2C, 0, 3, 4, 0x1D08, true},
    {13, BINDING_DEVICE_KEYBOARD, 0xE, 0, 3, 4, 0x1D09, true},
    {15, BINDING_DEVICE_KEYBOARD, 0xD3, 0, 3, 4, 0x1D0A, true},
    {16, BINDING_DEVICE_KEYBOARD, 0xB5, 0, 3, 4, 0x1D0B, true},
    {17, BINDING_DEVICE_KEYBOARD, 0x37, 0, 3, 4, 0x1D0C, true},
    {18, BINDING_DEVICE_KEYBOARD, 0xC8, 0, 3, 4, 0x1D0D, true},
    {32, BINDING_DEVICE_KEYBOARD, 0xD0, 0, 3, 4, 0x1D0E, true},
    {33, BINDING_DEVICE_KEYBOARD, 0xCB, 0, 3, 4, 0x1D0F, true},
    {34, BINDING_DEVICE_KEYBOARD, 0xCD, 0, 3, 4, 0x1D10, true},
    {39, BINDING_DEVICE_KEYBOARD, 0xC9, 0, 3, 4, 0x1D11, true},
    {40, BINDING_DEVICE_KEYBOARD, 0xD1, 0, 3, 4, 0x1D12, true},
    {41, BINDING_DEVICE_KEYBOARD, 0x9D, 0, 3, 4, 0x1D13, true},
    {42, BINDING_DEVICE_KEYBOARD, 0x4B, 0, 3, 4, 0x1D14, true},
    {43, BINDING_DEVICE_KEYBOARD, 0x4D, 0, 3, 4, 0x1D15, true},
    {44, BINDING_DEVICE_KEYBOARD, 0x48, 0, 3, 4, 0x1D16, true},
    {45, BINDING_DEVICE_KEYBOARD, 0x50, 0, 3, 4, 0x1D17, true},
    {62, BINDING_DEVICE_KEYBOARD, 0x4E, 0, 3, 4, 0x1D18, true},
    {63, BINDING_DEVICE_KEYBOARD, 0x4A, 0, 3, 4, 0x1D19, true},
};

// Key names: the first letter upper case, the others lower case.
void CapitalizeKeyName(char *name)                  // 0x006ab7a0
{
    if (name && std::strlen(name) > 1) {
        _strupr(name);
        _strlwr(name + 1);
    }
}

C_str GetText(int id)
{
    return g_texts.Get(id);
}

} // namespace

C_str GetKeyName(uint8_t key)                       // 0x006ab7d0
{
    char name[32] = {};
    WideToAnsi(name, GetIGraph()->GetKeyName(key), 31);
    CapitalizeKeyName(name);
    // Fix: the original passed the name to C_fstr as its format.
    return C_str(name);
}

bool C_KeyBindings::IsReservedKey(uint8_t key)      // 0x006a84e0
{
    for (const uint8_t *k = RESERVED_KEYS; *k; ++k) {
        if (*k == key)
            return true;
    }
    return false;
}

void C_KeyBindings::GetGroupPosition(int index, int *position) const   // 0x006a8540
{
    *position = -1;
    if (index < 0 || index >= GetCount())
        return;
    const int group = m_bindings[index].group;
    int first = index;
    while (first > 0 && m_bindings[first - 1].group == group)
        --first;
    *position = index - first + 1;
}

void C_KeyBindings::GetTextAndGroup(int index, int *textId, int *group) const   // 0x006a85b0
{
    if (index < 0 || index >= GetCount())
        return;
    *textId = m_bindings[index].textId;
    *group = m_bindings[index].group;
}

int C_KeyBindings::Find(int action, int mode) const // 0x006a8610
{
    for (int i = 0; i < GetCount(); ++i) {
        if (m_bindings[i].action == action && m_bindings[i].mode == mode)
            return i;
    }
    return -1;
}

bool C_KeyBindings::Bind(int index, int device, int key, int direction)   // 0x006a8670
{
    if (index < 0 || index >= GetCount())
        return false;
    if (device == BINDING_DEVICE_KEYBOARD) {
        if (IsReservedKey(static_cast<uint8_t>(key)))
            return false;
    } else if (device == BINDING_DEVICE_MOUSE) {
        if (key == MOUSE_AXIS_X || key == MOUSE_AXIS_Y)
            return false;
    }
    S_KeyBinding &binding = m_bindings[index];
    binding.device = static_cast<int16_t>(device);
    binding.key = static_cast<uint16_t>(key);
    binding.direction = direction;
    binding.changed = true;
    ResolveConflicts(index);
    return true;
}

void C_KeyBindings::ResolveConflicts(int index)     // 0x006a8730
{
    const S_KeyBinding &bound = m_bindings[index];
    for (int i = 0; i < GetCount(); ++i) {
        S_KeyBinding &other = m_bindings[i];
        if (i == index || other.group == -1 || other.key != bound.key || other.device != bound.device ||
            other.direction != bound.direction)
            continue;
        if ((other.textId == TEXT_SHARED_KEY_A && bound.textId == TEXT_SHARED_KEY_B) ||
            (bound.textId == TEXT_SHARED_KEY_A && other.textId == TEXT_SHARED_KEY_B))
            continue;
        bool overlap = false;
        if (bound.mode == BINDING_MODE_ALL &&
            (other.mode == BINDING_MODE_ALL || other.mode == BINDING_MODE_GAME || other.mode == BINDING_MODE_COMMAND_MAP))
            overlap = true;
        if (bound.mode == BINDING_MODE_GAME && (other.mode == BINDING_MODE_ALL || other.mode == BINDING_MODE_GAME))
            overlap = true;
        if (bound.mode == BINDING_MODE_COMMAND_MAP && (other.mode == BINDING_MODE_ALL || other.mode == BINDING_MODE_COMMAND_MAP))
            overlap = true;
        if (overlap) {
            other.device = BINDING_DEVICE_NONE;
            other.key = 0;
            other.changed = true;
        }
    }
}

int C_KeyBindings::FindUnbound() const              // 0x006a8860
{
    for (int i = 0; i < GetCount(); ++i) {
        if (m_bindings[i].device == BINDING_DEVICE_NONE && m_bindings[i].key == 0)
            return i;
    }
    return -1;
}

void C_KeyBindings::Apply(S_KeyBinding &binding)    // 0x006a88b0
{
    g_controls.Bind(binding.action, binding.device, binding.key, 0, binding.direction);
    binding.changed = false;
}

void C_KeyBindings::ApplyMode(int mode)             // 0x006a88e0
{
    for (int i = GetCount() - 1; i >= 0; --i) {
        if (m_bindings[i].mode == mode)
            Apply(m_bindings[i]);
    }
}

void C_KeyBindings::SetMode(int mode)               // 0x006a8940
{
    if (mode == BINDING_MODE_GAME || mode == BINDING_MODE_COMMAND_MAP) {
        ApplyMode(mode);
        m_mode = mode;
    }
}

void C_KeyBindings::ApplyChanged()                  // 0x006a8970
{
    if (m_mode != BINDING_MODE_GAME && m_mode != BINDING_MODE_COMMAND_MAP)
        return;
    for (int i = GetCount() - 1; i >= 0; --i) {
        S_KeyBinding &binding = m_bindings[i];
        if (binding.changed && (binding.mode != BINDING_MODE_GAME || m_mode == BINDING_MODE_GAME) &&
            (binding.mode != BINDING_MODE_COMMAND_MAP || m_mode == BINDING_MODE_COMMAND_MAP))
            Apply(binding);
    }
}

bool C_KeyBindings::Save(C_chunk &chunk) const      // 0x006a8a20
{
    chunk.Begin(CHUNK_BINDINGS_VERSION);
    chunk.Write(BINDINGS_VERSION);
    chunk.Ascend();
    chunk.Begin(CHUNK_BINDINGS);
    for (const S_KeyBinding &binding : m_bindings)
        chunk.Write(&binding, sizeof(binding));
    chunk.Ascend();
    return true;
}

bool C_KeyBindings::Load(C_chunk &chunk)            // 0x006a8bb0
{
    m_bindings.clear();
    uint16_t id = 0;
    bool entered = chunk.Descend(id);
    if (entered && id == CHUNK_BINDINGS_VERSION) {
        int version = 0;
        chunk.Read(version);
        chunk.Ascend();
        if (version != BINDINGS_VERSION) {
            SetDefaults(0);
            return false;
        }
        entered = chunk.Descend(id);
        if (entered && id == CHUNK_BINDINGS) {
            S_KeyBinding binding = {};
            while (chunk.RemainingSize() > 0) {
                // Fix: a last record shorter than 0x1C bytes was not read and the previous one
                // was added again and again.
                if (!chunk.Read(&binding, sizeof(binding)))
                    break;
                binding.changed = true;                 // (All to be applied: what the file says, usually false, is not kept.)
                m_bindings.push_back(binding);
            }
            chunk.Ascend();
            ApplyChanged();
            return true;
        }
    }
    SetDefaults(0);
    // Fix: the original also ascended when no chunk had been entered (leaving its parent).
    if (entered)
        chunk.Ascend();
    return false;
}

void C_KeyBindings::SetDefaults(int layout)         // 0x006a8e90
{
    if (layout == 1)
        SetDefaultsWasd();
    else
        SetDefaultsArrows();
}

void C_KeyBindings::AddBindings(const S_KeyBinding *bindings, int count)
{
    m_bindings.clear();
    m_bindings.assign(bindings, bindings + count);
    ApplyChanged();
}

void C_KeyBindings::SetDefaultsArrows()             // 0x006a8eb0
{
    AddBindings(DEFAULTS_ARROWS, static_cast<int>(sizeof(DEFAULTS_ARROWS) / sizeof(DEFAULTS_ARROWS[0])));
}

void C_KeyBindings::SetDefaultsWasd()               // 0x006aa340
{
    AddBindings(DEFAULTS_WASD, static_cast<int>(sizeof(DEFAULTS_WASD) / sizeof(DEFAULTS_WASD[0])));
}

C_str C_KeyBindings::GetBindingName(int index) const    // 0x006ab840
{
    if (index < 0 || index >= GetCount())
        return C_str();
    const S_KeyBinding &binding = m_bindings[index];
    if (binding.device == BINDING_DEVICE_MOUSE) {
        switch (binding.key) {
        case MOUSE_BUTTON_5: return GetText(0xB99);
        case MOUSE_BUTTON_LEFT: return GetText(0xB9A);
        case MOUSE_BUTTON_RIGHT: return GetText(0xB9B);
        case MOUSE_BUTTON_MIDDLE: return GetText(0xB9C);
        case MOUSE_BUTTON_4: return GetText(0xB98);
        case MOUSE_AXIS_X:
        case MOUSE_AXIS_Y: {
            C_str name = GetText(binding.key == MOUSE_AXIS_X ? 0xB9D : 0xB9E);
            if (binding.direction == 1)
                name += " +";
            else if (binding.direction == 2)
                name += " -";
            return name;
        }
        case MOUSE_WHEEL: {
            C_str name = GetText(0xB9F);
            name += " ";
            if (binding.direction == 1)
                name += GetText(0xBAC);
            else if (binding.direction == 2)
                name += GetText(0xBAD);
            return name;
        }
        default:
            return C_str();
        }
    }
    if (binding.device != BINDING_DEVICE_KEYBOARD)
        return C_str();
    int textId = -1;
    switch (binding.key) {
    case 0x0E:                                      // Backspace
        if (!g_gameProfile.IsLanguage("German"))
            return GetKeyName(static_cast<uint8_t>(binding.key));
        textId = 0xB97;
        break;
    case 0x1D: textId = 0xBAB; break;
    case 0x29:                                      // the key left of 1
        if (!g_gameProfile.IsLanguage("Czech"))
            return GetKeyName(static_cast<uint8_t>(binding.key));
        return C_str("~");
    case 0x2A: textId = 0xBA9; break;
    case 0x36: textId = 0xBA8; break;
    case 0x37: textId = 0xBA1; break;
    case 0x38: textId = 0xBA7; break;
    case 0x39:                                      // Space
        if (!g_gameProfile.IsLanguage("Spanish"))
            return GetKeyName(static_cast<uint8_t>(binding.key));
        return C_str("Barra espaciad.");
    case 0x47: textId = 0xBB5; break;
    case 0x48: textId = 0xBB6; break;
    case 0x49: textId = 0xBB7; break;
    case 0x4A: textId = 0xBA2; break;
    case 0x4B: textId = 0xBB2; break;
    case 0x4C: textId = 0xBB3; break;
    case 0x4D: textId = 0xBB4; break;
    case 0x4E: textId = 0xBA3; break;
    case 0x4F: textId = 0xBAF; break;
    case 0x50: textId = 0xBB0; break;
    case 0x51: textId = 0xBB1; break;
    case 0x52: textId = 0xBAE; break;
    case 0x53: textId = 0xBA5; break;
    case 0x9C: textId = 0xBA4; break;
    case 0x9D: textId = 0xBAA; break;
    case 0xB5: textId = 0xBA0; break;
    case 0xB8: textId = 0xBA6; break;
    default:
        return GetKeyName(static_cast<uint8_t>(binding.key));
    }
    return GetText(textId);
}

bool C_KeyBindings::ReplaceTag(C_str *out, const char *text, int tag) const   // 0x006ac130
{
    const char *const tagText = KEY_TAGS[tag].tag;
    const char *found = std::strstr(text, tagText);
    if (!found) {
        *out = C_str(text);
        return false;
    }
    const int binding = Find(KEY_TAGS[tag].action, KEY_TAGS[tag].mode);
    const char *rest = found + std::strlen(tagText);
    int prefixLength = static_cast<int>(found - text);
    if (prefixLength > 0x3FF)
        prefixLength = 0x3FF;
    if (found == text) {
        *out = C_fstr("'%s' %s", GetBindingName(binding).c_str(), rest);
        return true;
    }
    char prefix[0x400];
    std::strncpy(prefix, text, 0x3FF);
    prefix[prefixLength] = '\0';
    if (*rest == '\0')
        *out = C_fstr("%s '%s'", prefix, GetBindingName(binding).c_str());
    else
        *out = C_fstr("%s '%s' %s", prefix, GetBindingName(binding).c_str(), rest);
    return true;
}

bool C_KeyBindings::ReplaceTags(C_str *out, const char *text) const   // 0x006ac470
{
    for (int i = 0; KEY_TAGS[i].tag; ++i) {
        if (std::strstr(text, KEY_TAGS[i].tag))
            return ReplaceTag(out, text, i);
    }
    *out = C_str(text);
    return false;
}
