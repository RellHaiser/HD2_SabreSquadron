// IGraph: the keyboard and mouse, typed characters, timing (0x1006f040 - 0x1006fbd0).
// No DirectInput in this build: the keyboard and the mouse are never created, so each method takes
// LS3DF's path without its device (no key down, no mouse state, mouse mode -1). The window-message
// keyboard (IGKEY_WINDOW_MESSAGES), the typed characters and the timer work as before.
#include <cstring>

#include "core/i3d_result.h"
#include "dx/dinput_types.h"
#include "igraph/igraph.h"
#include "igraph/igraph_state.h"
#include "platform/platform.h"

using namespace ls3d;
using namespace ls3d::ig;

namespace {

uint32_t g_keyboardFlags = 0;                   // 0x100d173c IGKEY_*

// Typed characters: [0] the last one ReadChar returned, then the queue.
int g_chars[0x20];                              // 0x100d1e48

// IGKEY_WINDOW_MESSAGES: the key and character of the last WM_KEYDOWN / WM_CHAR.
int g_messageKey = 0;                           // 0x100d1a8c
int g_messageChar = 0;                          // 0x100d212c
int g_lastKeyChar = 0;                          // 0x100d1a80 GetKeyChar

int g_mouseX = 0;                               // 0x100d2178
int g_mouseY = 0;                               // 0x100d217c

DWORD g_lastTime = 0;                           // 0x100d1738 ReadTimer

// Key, character, shifted character (0x100a7868).
struct S_keyChar {
    uint8_t key, normal, shifted;
};
const S_keyChar kKeyChars[] = {
    {DIK_SPACE, ' ', ' '}, {DIK_0, '0', ')'}, {DIK_1, '1', '!'}, {DIK_2, '2', '@'}, {DIK_3, '3', '#'},
    {DIK_4, '4', '$'}, {DIK_5, '5', '%'}, {DIK_6, '6', '^'}, {DIK_7, '7', '&'}, {DIK_8, '8', '*'},
    {DIK_9, '9', '('}, {DIK_A, 'a', 'A'}, {DIK_B, 'b', 'B'}, {DIK_C, 'c', 'C'}, {DIK_D, 'd', 'D'},
    {DIK_E, 'e', 'E'}, {DIK_F, 'f', 'F'}, {DIK_G, 'g', 'G'}, {DIK_H, 'h', 'H'}, {DIK_I, 'i', 'I'},
    {DIK_J, 'j', 'J'}, {DIK_K, 'k', 'K'}, {DIK_L, 'l', 'L'}, {DIK_M, 'm', 'M'}, {DIK_N, 'n', 'N'},
    {DIK_O, 'o', 'O'}, {DIK_P, 'p', 'P'}, {DIK_Q, 'q', 'Q'}, {DIK_R, 'r', 'R'}, {DIK_S, 's', 'S'},
    {DIK_T, 't', 'T'}, {DIK_U, 'u', 'U'}, {DIK_V, 'v', 'V'}, {DIK_W, 'w', 'W'}, {DIK_X, 'x', 'X'},
    {DIK_Y, 'y', 'Y'}, {DIK_Z, 'z', 'Z'}, {DIK_NUMPAD0, '0', '0'}, {DIK_NUMPAD1, '1', '1'},
    {DIK_NUMPAD2, '2', '2'}, {DIK_NUMPAD3, '3', '3'}, {DIK_NUMPAD4, '4', '4'}, {DIK_NUMPAD5, '5', '5'},
    {DIK_NUMPAD6, '6', '6'}, {DIK_NUMPAD7, '7', '7'}, {DIK_NUMPAD8, '8', '8'}, {DIK_NUMPAD9, '9', '9'},
    {DIK_MULTIPLY, '*', '*'}, {DIK_ADD, '+', '+'}, {DIK_SUBTRACT, '-', '-'}, {DIK_DECIMAL, '.', '.'},
    {DIK_DIVIDE, '/', '/'}, {DIK_SEMICOLON, ';', ':'}, {DIK_EQUALS, '=', '+'}, {DIK_COMMA, ',', '<'},
    {DIK_MINUS, '-', '_'}, {DIK_PERIOD, '.', '>'}, {DIK_SLASH, '/', '?'}, {DIK_GRAVE, '`', '~'},
    {DIK_LBRACKET, '[', '{'}, {DIK_BACKSLASH, '\\', '|'}, {DIK_RBRACKET, ']', '}'}, {DIK_APOSTROPHE, '\'', '"'},
};

}  // namespace

namespace ls3d::ig {

// The input part of the IGraph constructor (0x10069a00): the mouse position is all that is left.
void ResetInputState()
{
    g_mouseX = 0;
    g_mouseY = 0;
}

bool KeyboardUsesMessages()
{
    return (g_keyboardFlags & IGKEY_WINDOW_MESSAGES) != 0;
}

// ProcessWinMessages: a WM_CHAR into the first free queue slot.
void QueueChar(int ch)
{
    for (int i = 1; i < 0x20; ++i) {
        if (!g_chars[i]) {
            g_chars[i] = ch;
            break;
        }
    }
}

// ProcessWinMessages with IGKEY_WINDOW_MESSAGES: the platform turns the window's key messages into
// DirectInput codes (platform/win32).
void KeyMessage(int key, int ch)
{
    g_messageKey = key;
    g_messageChar = ch;
}

}  // namespace ls3d::ig

// 0x1006f040: the flags are kept (they choose the window-message keyboard), then creating the
// keyboard fails as in LS3DF when CreateDevice fails, without a DirectInput error to log.
HRESULT __stdcall IGraph::InitKeyboard(uint32_t flags)
{
    CloseKeyboard();
    g_keyboardFlags = flags;
    return I3DERR_GENERIC;
}

// 0x1006f150
bool __stdcall IGraph::IsKeyboardInit()
{
    return false;
}

// 0x1006f160
HRESULT __stdcall IGraph::CloseKeyboard()
{
    return 0;
}

// 0x1006f190 (the Ctrl+Alt+Shift+F12 / F11 / F10 debug keys went with the keyboard)
HRESULT __stdcall IGraph::ReadKeyboard()
{
    return I3DERR_NOTINITIALIZED;
}

// 0x1006f2d0
int __stdcall IGraph::ReadChar()
{
    std::memmove(&g_chars[0], &g_chars[1], sizeof g_chars - sizeof g_chars[0]);
    g_chars[0x1f] = 0;
    return g_chars[0];
}

// 0x1006f300
HRESULT __stdcall IGraph::ClearChars()
{
    std::memset(g_chars, 0, sizeof g_chars);
    return 0;
}

// 0x1006f320
int __stdcall IGraph::GetKey()
{
    if (g_keyboardFlags & IGKEY_WINDOW_MESSAGES) {
        const int key = g_messageKey;
        g_messageKey = 0;
        g_lastKeyChar = g_messageChar;
        g_messageChar = 0;
        return key;
    }
    return 0;
}

// 0x1006f470
int __stdcall IGraph::GetKeyChar()
{
    const int ch = g_lastKeyChar;
    g_lastKeyChar = 0;
    return ch;
}

// 0x1006f490
bool __stdcall IGraph::IsKeyDown(int key)
{
    return false;
}

// 0x1006f4b0 (the map is checked, not kept: no mapped key is ever down)
HRESULT __stdcall IGraph::SetKeyMap(const uint8_t *keys, uint32_t count)
{
    if (!keys || count > 0x40)
        return I3DERR_INVALIDPARAMS;
    return 0;
}

// 0x1006f500
uint64_t __stdcall IGraph::GetMappedKeys()
{
    return 0;
}

// 0x1006f550
bool __stdcall IGraph::IsShiftDown()
{
    return false;
}

// 0x1006f570
bool __stdcall IGraph::IsCtrlDown()
{
    return false;
}

// 0x1006f590
bool __stdcall IGraph::IsAltDown()
{
    return false;
}

// 0x1006f5b0
uint32_t __stdcall IGraph::GetShiftKeys(bool read)
{
    return 0;
}

// 0x1006f660
uint8_t __stdcall IGraph::KeyToChar(int key, uint32_t shiftKeys)
{
    for (const S_keyChar &k : kKeyChars) {
        if (k.key == static_cast<uint8_t>(key))
            return (shiftKeys & IGSHIFT_SHIFT) ? k.shifted : k.normal;
    }
    return 0;
}

// 0x1006f6a0
const wchar_t *__stdcall IGraph::GetKeyName(int key)
{
    return nullptr;
}

// 0x1006f710
int __stdcall IGraph::InitMouse(int mode)
{
    if (mode == -1)
        return -1;
    CloseMouse();
    return I3DERR_GENERIC;
}

// 0x1006f840
bool __stdcall IGraph::IsMouseInit()
{
    return false;
}

// 0x1006f850
HRESULT __stdcall IGraph::CloseMouse()
{
    return 0;
}

// 0x1006f8a0
HRESULT __stdcall IGraph::ReadMouse()
{
    return I3DERR_NOTINITIALIZED;
}

// 0x1006fa10
int __stdcall IGraph::GetMouseDX()
{
    return 0;
}

// 0x1006fa20
int __stdcall IGraph::GetMouseDY()
{
    return 0;
}

// 0x1006fa30
int __stdcall IGraph::GetMouseDZ()
{
    return 0;
}

// 0x1006fa40 (mouse mode -1: the position is only stored, the cursor stays)
int __stdcall IGraph::SetMousePos(int x, int y)
{
    g_mouseX = x;
    g_mouseY = y;
    return -1;
}

// 0x1006fb10
int __stdcall IGraph::GetMouseX()
{
    return g_mouseX;
}

// 0x1006fb20
int __stdcall IGraph::GetMouseY()
{
    return g_mouseY;
}

// 0x1006fb30
uint32_t __stdcall IGraph::GetMouseButtons()
{
    return 0;
}

// LS3DRF 0x100313e0 (from LS3DF 0x1006fb70)
DWORD __stdcall IGraph::ReadTimer()
{
    g_lastTime = platform::Milliseconds();
    return g_lastTime;
}

// LS3DRF 0x100313f0 (from LS3DF 0x1006fb80)
DWORD __stdcall IGraph::GetTimer(DWORD minMs, DWORD maxMs)
{
    const DWORD before = g_lastTime;
    ReadTimer();
    const DWORD elapsed = g_lastTime - before;
    if (elapsed < minMs) {
        platform::Pause(minMs - elapsed);
        g_lastTime += minMs - elapsed;
        return minMs;
    }
    if (maxMs && elapsed > maxMs)
        return maxMs;
    return elapsed;
}
