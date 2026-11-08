// The platform's window, message, cursor and clock functions behind one interface, so that the
// implementation is chosen when the engine starts (platform/platform.cpp):
// - Win32 (platform/win32): IGraph's own Windows code, the default;
// - SDL3 (platform/sdl3): LS3DF.ini [Platform] SDL3=1, when SDL3.dll can be loaded.
// Threads, locks, events, libraries and the accessibility keys are the Win32 implementation's for
// both (platform_win32.cpp): DirectSound sets the events itself, and SDL has no notion of the rest.
//
// Only the platform's sources and the backends that work with one implementation (the SDL3 input
// device) include this.
#pragma once

#include "platform/platform.h"

namespace ls3d::platform {

class Windowing {
public:
    virtual ~Windowing() = default;

    // The functions of platform.h of the same names.
    virtual uint32_t Milliseconds() = 0;
    virtual uint32_t TickCount() = 0;
    virtual void Pause(uint32_t ms) = 0;
    virtual bool RegisterWindowClass(HINSTANCE instance) = 0;
    virtual HWND CreateMainWindow(HINSTANCE instance, bool fullscreen, int x, int y, int width, int height,
                                  bool withMenu, HMENU menu) = 0;
    virtual HWND CreateChildWindow(HINSTANCE instance, HWND parent, int x, int y, int width, int height) = 0;
    virtual HMENU LoadDefaultMenu() = 0;
    virtual void Refresh(HWND window) = 0;
    virtual void Focus(HWND window) = 0;
    virtual bool SetTitle(HWND window, const char *title) = 0;
    virtual bool ForegroundIsOwn(HWND window) = 0;
    virtual POINT ClientOrigin(HWND window, bool framed, bool withMenu) = 0;
    virtual bool AddDialog(HWND dialog) = 0;
    virtual bool RemoveDialog(HWND dialog) = 0;
    virtual void PumpMessages() = 0;
    virtual bool SetCursorPosition(int x, int y) = 0;
    virtual void ReleaseCursorClip() = 0;
    virtual bool MouseButtonsSwapped() = 0;
    virtual void DisableAccessibilityKeys() = 0;
    virtual void RestoreAccessibilityKeys() = 0;
};

// The implementations. Sdl3Windowing is null when SDL3.dll cannot be loaded or SDL's video cannot
// start (it loads and starts SDL on the first call: never from DllMain).
Windowing *Win32Windowing();
Windowing *Sdl3Windowing();

// The implementation in use (chosen on the first window call; until then the clock is Win32's).
Windowing *CurrentWindowing();
// Tests: this implementation from now on.
void UseWindowing(Windowing *windowing);
// The engine's decisions (SetWindowEvents).
WindowEvents *Events();

// Windows' modeless dialogs (AddDialog): true when `msg` was one of theirs, handled (the SDL3
// implementation's message hook).
bool Win32DialogMessage(MSG *msg);

}  // namespace ls3d::platform
