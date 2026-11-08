// SDL3 for the platform's SDL3 implementation (platform/sdl3) and the SDL3 input device
// (input/sdl3): SDL's functions, loaded at run time from SDL3.dll, and what the two share.
#pragma once

#include <cstdint>

#include <SDL3/SDL.h>

namespace ls3d::sdl3 {

// The functions the backends call.
#define LS3D_SDL_FUNCTIONS(X)                                                                                \
    X(SDL_Init)                                                                                          \
    X(SDL_SetHint)                                                                                       \
    X(SDL_CreateWindow)                                                                                  \
    X(SDL_GetWindowProperties)                                                                           \
    X(SDL_GetPointerProperty)                                                                            \
    X(SDL_GetWindowID)                                                                                   \
    X(SDL_SetWindowPosition)                                                                             \
    X(SDL_GetWindowPosition)                                                                             \
    X(SDL_SetWindowSize)                                                                                 \
    X(SDL_ShowWindow)                                                                                    \
    X(SDL_SetWindowTitle)                                                                                \
    X(SDL_RaiseWindow)                                                                                   \
    X(SDL_GetWindowFlags)                                                                                \
    X(SDL_PollEvent)                                                                                     \
    X(SDL_WaitEvent)                                                                                     \
    X(SDL_StartTextInput)                                                                                \
    X(SDL_WarpMouseGlobal)                                                                               \
    X(SDL_SetWindowMouseRect)                                                                            \
    X(SDL_SetWindowRelativeMouseMode)                                                                    \
    X(SDL_GetKeyboardFocus)                                                                              \
    X(SDL_GetKeyboardState)                                                                              \
    X(SDL_GetMouseState)                                                                                 \
    X(SDL_GetRelativeMouseState)                                                                         \
    X(SDL_GetScancodeFromKey)                                                                            \
    X(SDL_GetKeyFromScancode)                                                                            \
    X(SDL_GetKeyName)                                                                                    \
    X(SDL_GetTicks)                                                                                      \
    X(SDL_Delay)                                                                                         \
    X(SDL_SetWindowsMessageHook)

struct S_api {
#define LS3D_SDL_MEMBER(name) decltype(&::name) name;
    LS3D_SDL_FUNCTIONS(LS3D_SDL_MEMBER)
#undef LS3D_SDL_MEMBER
};
extern S_api api;

// Loads SDL3.dll and starts SDL's video (once; on the thread that will own the window). False when
// either fails.
bool Start();

// The engine's main window (null before it is created).
extern SDL_Window *g_window;
// Wheel notches since the input device last read them (the platform's message loop adds them).
extern float g_wheel;

// DirectInput's key code (DIK_*: the PC set-1 scan code, 0x80 for the E0 keys) of an SDL scancode,
// 0 when DirectInput has none; and back (SDL_SCANCODE_UNKNOWN for none).
uint8_t DikOfScancode(SDL_Scancode scancode);
SDL_Scancode ScancodeOfDik(uint8_t dik);

}  // namespace ls3d::sdl3
