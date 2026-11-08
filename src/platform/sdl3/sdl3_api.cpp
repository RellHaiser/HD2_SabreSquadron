// SDL3 for the platform and the input device (platform/sdl3/sdl3_api.h).
#include "platform/sdl3/sdl3_api.h"

#include "dx/dinput_types.h"
#include "platform/platform.h"

namespace ls3d::sdl3 {

S_api api;
SDL_Window *g_window;
float g_wheel;

namespace {
platform::Library *g_library;
}

bool Start()
{
    static int state;               // 0: not tried, 1: started, -1: failed
    if (state)
        return state > 0;
    state = -1;
    platform::Library *library = platform::OpenLibrary("SDL3.dll");
    if (!library)
        return false;
    bool found = true;
#define LS3D_SDL_LOAD(name) \
    found &= (api.name = reinterpret_cast<decltype(api.name)>(platform::LibrarySymbol(library, #name))) != nullptr;
    LS3D_SDL_FUNCTIONS(LS3D_SDL_LOAD)
#undef LS3D_SDL_LOAD
    if (!found || !api.SDL_Init(SDL_INIT_VIDEO)) {
        platform::CloseLibrary(library);
        return false;
    }
    state = 1;
    g_library = library;
    return true;
}

bool StartGpu()
{
    static int state;
    if (state)
        return state > 0;
    state = -1;
    if (!Start())
        return false;
    bool found = true;
#define LS3D_SDL_LOAD(name) \
    found &= (api.name = reinterpret_cast<decltype(api.name)>(platform::LibrarySymbol(g_library, #name))) != nullptr;
    LS3D_SDL_GPU_FUNCTIONS(LS3D_SDL_LOAD)
#undef LS3D_SDL_LOAD
    state = found ? 1 : -1;
    return found;
}

namespace {

// The keys DirectInput names and SDL's scancode for each (DirectInput's codes are the keys' PC scan
// codes; the media and browser keys SDL3 has no scancode for are left out).
const struct {
    uint8_t dik;
    SDL_Scancode scancode;
} kKeys[] = {
    {DIK_ESCAPE, SDL_SCANCODE_ESCAPE},
    {DIK_1, SDL_SCANCODE_1},
    {DIK_2, SDL_SCANCODE_2},
    {DIK_3, SDL_SCANCODE_3},
    {DIK_4, SDL_SCANCODE_4},
    {DIK_5, SDL_SCANCODE_5},
    {DIK_6, SDL_SCANCODE_6},
    {DIK_7, SDL_SCANCODE_7},
    {DIK_8, SDL_SCANCODE_8},
    {DIK_9, SDL_SCANCODE_9},
    {DIK_0, SDL_SCANCODE_0},
    {DIK_MINUS, SDL_SCANCODE_MINUS},
    {DIK_EQUALS, SDL_SCANCODE_EQUALS},
    {DIK_BACK, SDL_SCANCODE_BACKSPACE},
    {DIK_TAB, SDL_SCANCODE_TAB},
    {DIK_Q, SDL_SCANCODE_Q},
    {DIK_W, SDL_SCANCODE_W},
    {DIK_E, SDL_SCANCODE_E},
    {DIK_R, SDL_SCANCODE_R},
    {DIK_T, SDL_SCANCODE_T},
    {DIK_Y, SDL_SCANCODE_Y},
    {DIK_U, SDL_SCANCODE_U},
    {DIK_I, SDL_SCANCODE_I},
    {DIK_O, SDL_SCANCODE_O},
    {DIK_P, SDL_SCANCODE_P},
    {DIK_LBRACKET, SDL_SCANCODE_LEFTBRACKET},
    {DIK_RBRACKET, SDL_SCANCODE_RIGHTBRACKET},
    {DIK_RETURN, SDL_SCANCODE_RETURN},
    {DIK_LCONTROL, SDL_SCANCODE_LCTRL},
    {DIK_A, SDL_SCANCODE_A},
    {DIK_S, SDL_SCANCODE_S},
    {DIK_D, SDL_SCANCODE_D},
    {DIK_F, SDL_SCANCODE_F},
    {DIK_G, SDL_SCANCODE_G},
    {DIK_H, SDL_SCANCODE_H},
    {DIK_J, SDL_SCANCODE_J},
    {DIK_K, SDL_SCANCODE_K},
    {DIK_L, SDL_SCANCODE_L},
    {DIK_SEMICOLON, SDL_SCANCODE_SEMICOLON},
    {DIK_APOSTROPHE, SDL_SCANCODE_APOSTROPHE},
    {DIK_GRAVE, SDL_SCANCODE_GRAVE},
    {DIK_LSHIFT, SDL_SCANCODE_LSHIFT},
    {DIK_BACKSLASH, SDL_SCANCODE_BACKSLASH},
    {DIK_Z, SDL_SCANCODE_Z},
    {DIK_X, SDL_SCANCODE_X},
    {DIK_C, SDL_SCANCODE_C},
    {DIK_V, SDL_SCANCODE_V},
    {DIK_B, SDL_SCANCODE_B},
    {DIK_N, SDL_SCANCODE_N},
    {DIK_M, SDL_SCANCODE_M},
    {DIK_COMMA, SDL_SCANCODE_COMMA},
    {DIK_PERIOD, SDL_SCANCODE_PERIOD},
    {DIK_SLASH, SDL_SCANCODE_SLASH},
    {DIK_RSHIFT, SDL_SCANCODE_RSHIFT},
    {DIK_MULTIPLY, SDL_SCANCODE_KP_MULTIPLY},
    {DIK_LMENU, SDL_SCANCODE_LALT},
    {DIK_SPACE, SDL_SCANCODE_SPACE},
    {DIK_CAPITAL, SDL_SCANCODE_CAPSLOCK},
    {DIK_F1, SDL_SCANCODE_F1},
    {DIK_F2, SDL_SCANCODE_F2},
    {DIK_F3, SDL_SCANCODE_F3},
    {DIK_F4, SDL_SCANCODE_F4},
    {DIK_F5, SDL_SCANCODE_F5},
    {DIK_F6, SDL_SCANCODE_F6},
    {DIK_F7, SDL_SCANCODE_F7},
    {DIK_F8, SDL_SCANCODE_F8},
    {DIK_F9, SDL_SCANCODE_F9},
    {DIK_F10, SDL_SCANCODE_F10},
    {DIK_NUMLOCK, SDL_SCANCODE_NUMLOCKCLEAR},
    {DIK_SCROLL, SDL_SCANCODE_SCROLLLOCK},
    {DIK_NUMPAD7, SDL_SCANCODE_KP_7},
    {DIK_NUMPAD8, SDL_SCANCODE_KP_8},
    {DIK_NUMPAD9, SDL_SCANCODE_KP_9},
    {DIK_SUBTRACT, SDL_SCANCODE_KP_MINUS},
    {DIK_NUMPAD4, SDL_SCANCODE_KP_4},
    {DIK_NUMPAD5, SDL_SCANCODE_KP_5},
    {DIK_NUMPAD6, SDL_SCANCODE_KP_6},
    {DIK_ADD, SDL_SCANCODE_KP_PLUS},
    {DIK_NUMPAD1, SDL_SCANCODE_KP_1},
    {DIK_NUMPAD2, SDL_SCANCODE_KP_2},
    {DIK_NUMPAD3, SDL_SCANCODE_KP_3},
    {DIK_NUMPAD0, SDL_SCANCODE_KP_0},
    {DIK_DECIMAL, SDL_SCANCODE_KP_PERIOD},
    {DIK_OEM_102, SDL_SCANCODE_NONUSBACKSLASH},
    {DIK_F11, SDL_SCANCODE_F11},
    {DIK_F12, SDL_SCANCODE_F12},
    {DIK_F13, SDL_SCANCODE_F13},
    {DIK_F14, SDL_SCANCODE_F14},
    {DIK_F15, SDL_SCANCODE_F15},
    {DIK_KANA, SDL_SCANCODE_INTERNATIONAL2},
    {DIK_ABNT_C1, SDL_SCANCODE_INTERNATIONAL1},
    {DIK_CONVERT, SDL_SCANCODE_INTERNATIONAL4},
    {DIK_NOCONVERT, SDL_SCANCODE_INTERNATIONAL5},
    {DIK_YEN, SDL_SCANCODE_INTERNATIONAL3},
    {DIK_ABNT_C2, SDL_SCANCODE_KP_COMMA},   // (scan code 0x7e; NEC's is 0xb3)
    {0x59, SDL_SCANCODE_KP_EQUALS},         // (scan code 0x59: DirectInput names only NEC's, 0x8d)
    {DIK_PREVTRACK, SDL_SCANCODE_MEDIA_PREVIOUS_TRACK},
    {DIK_NEXTTRACK, SDL_SCANCODE_MEDIA_NEXT_TRACK},
    {DIK_NUMPADENTER, SDL_SCANCODE_KP_ENTER},
    {DIK_RCONTROL, SDL_SCANCODE_RCTRL},
    {DIK_MUTE, SDL_SCANCODE_MUTE},
    {DIK_PLAYPAUSE, SDL_SCANCODE_MEDIA_PLAY_PAUSE},
    {DIK_MEDIASTOP, SDL_SCANCODE_MEDIA_STOP},
    {DIK_VOLUMEDOWN, SDL_SCANCODE_VOLUMEDOWN},
    {DIK_VOLUMEUP, SDL_SCANCODE_VOLUMEUP},
    {DIK_WEBHOME, SDL_SCANCODE_AC_HOME},
    {DIK_DIVIDE, SDL_SCANCODE_KP_DIVIDE},
    {DIK_SYSRQ, SDL_SCANCODE_PRINTSCREEN},
    {DIK_RMENU, SDL_SCANCODE_RALT},
    {DIK_PAUSE, SDL_SCANCODE_PAUSE},
    {DIK_HOME, SDL_SCANCODE_HOME},
    {DIK_UP, SDL_SCANCODE_UP},
    {DIK_PRIOR, SDL_SCANCODE_PAGEUP},
    {DIK_LEFT, SDL_SCANCODE_LEFT},
    {DIK_RIGHT, SDL_SCANCODE_RIGHT},
    {DIK_END, SDL_SCANCODE_END},
    {DIK_DOWN, SDL_SCANCODE_DOWN},
    {DIK_NEXT, SDL_SCANCODE_PAGEDOWN},
    {DIK_INSERT, SDL_SCANCODE_INSERT},
    {DIK_DELETE, SDL_SCANCODE_DELETE},
    {DIK_LWIN, SDL_SCANCODE_LGUI},
    {DIK_RWIN, SDL_SCANCODE_RGUI},
    {DIK_APPS, SDL_SCANCODE_APPLICATION},
    {DIK_POWER, SDL_SCANCODE_POWER},
    {DIK_SLEEP, SDL_SCANCODE_SLEEP},
    {DIK_WEBSEARCH, SDL_SCANCODE_AC_SEARCH},
    {DIK_WEBFAVORITES, SDL_SCANCODE_AC_BOOKMARKS},
    {DIK_WEBREFRESH, SDL_SCANCODE_AC_REFRESH},
    {DIK_WEBSTOP, SDL_SCANCODE_AC_STOP},
    {DIK_WEBFORWARD, SDL_SCANCODE_AC_FORWARD},
    {DIK_WEBBACK, SDL_SCANCODE_AC_BACK},
    {DIK_MEDIASELECT, SDL_SCANCODE_MEDIA_SELECT},
};

}  // namespace

uint8_t DikOfScancode(SDL_Scancode scancode)
{
    for (const auto &k : kKeys) {
        if (k.scancode == scancode)
            return k.dik;
    }
    return 0;
}

SDL_Scancode ScancodeOfDik(uint8_t dik)
{
    for (const auto &k : kKeys) {
        if (k.dik == dik)
            return k.scancode;
    }
    return SDL_SCANCODE_UNKNOWN;
}

}  // namespace ls3d::sdl3
