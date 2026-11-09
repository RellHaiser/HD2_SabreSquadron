// The platform's window, message, cursor and clock functions (platform/platform.h): they go to the
// implementation chosen on the first window call (platform/windowing.h).
#include "platform/windowing.h"

#include "core/enhance.h"

namespace ls3d::platform {

namespace {

Windowing *s_current;
WindowEvents *s_events;

// The implementation, chosen now if it was not yet: LS3DF.ini [Platform] SDL3, Win32 otherwise.
Windowing &Chosen()
{
    if (!s_current) {
        s_current = Win32Windowing();
        if (Enhancements().sdl3) {
            if (Windowing *sdl3 = Sdl3Windowing()) {
                s_current = sdl3;
                EnhancementLog("LS3DF.ini SDL3: the window, its input and the clock through SDL3");
            } else {
                EnhancementLog("LS3DF.ini SDL3: SDL3.dll not found (or SDL's video did not start), "
                               "the window through Win32");
            }
        }
    }
    return *s_current;
}

}  // namespace

Windowing *CurrentWindowing() { return s_current; }
void UseWindowing(Windowing *windowing) { s_current = windowing; }
WindowEvents *Events() { return s_events; }

// (The clock is read from static initialisers too: Win32's until a window call chose.)
uint32_t Milliseconds() { return (s_current ? s_current : Win32Windowing())->Milliseconds(); }
uint32_t TickCount() { return (s_current ? s_current : Win32Windowing())->TickCount(); }
void Pause(uint32_t ms) { (s_current ? s_current : Win32Windowing())->Pause(ms); }

void SetWindowEvents(WindowEvents *events) { s_events = events; }
bool RegisterWindowClass(HINSTANCE instance) { return Chosen().RegisterWindowClass(instance); }
HWND CreateMainWindow(HINSTANCE instance, bool fullscreen, int x, int y, int width, int height, bool withMenu,
                      HMENU menu)
{
    return Chosen().CreateMainWindow(instance, fullscreen, x, y, width, height, withMenu, menu);
}
HWND CreateChildWindow(HINSTANCE instance, HWND parent, int x, int y, int width, int height)
{
    return Chosen().CreateChildWindow(instance, parent, x, y, width, height);
}
HMENU LoadDefaultMenu() { return Chosen().LoadDefaultMenu(); }
void Refresh(HWND window) { Chosen().Refresh(window); }
void Focus(HWND window) { Chosen().Focus(window); }
void Minimize(HWND window) { Chosen().Minimize(window); }
bool SetTitle(HWND window, const char *title) { return Chosen().SetTitle(window, title); }
bool ForegroundIsOwn(HWND window) { return Chosen().ForegroundIsOwn(window); }
POINT ClientOrigin(HWND window, bool framed, bool withMenu) { return Chosen().ClientOrigin(window, framed, withMenu); }
bool AddDialog(HWND dialog) { return Chosen().AddDialog(dialog); }
bool RemoveDialog(HWND dialog) { return Chosen().RemoveDialog(dialog); }
void PumpMessages() { Chosen().PumpMessages(); }
bool SetCursorPosition(int x, int y) { return Chosen().SetCursorPosition(x, y); }
// (IGraph's destructor calls it, possibly from DllMain: it does not choose.)
void ReleaseCursorClip() { (s_current ? s_current : Win32Windowing())->ReleaseCursorClip(); }
bool MouseButtonsSwapped() { return Chosen().MouseButtonsSwapped(); }
void DisableAccessibilityKeys() { Chosen().DisableAccessibilityKeys(); }
void RestoreAccessibilityKeys() { Chosen().RestoreAccessibilityKeys(); }

}  // namespace ls3d::platform
