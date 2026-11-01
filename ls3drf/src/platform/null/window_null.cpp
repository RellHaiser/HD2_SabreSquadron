// The DLL's window (platform/platform.h): there is none. LS3DRF.dll has no window class, window,
// message loop or accessibility-key calls (of USER32 it imports ClipCursor and MessageBoxA only),
// so the class is never registered: IGraph::Init stops where LS3DF's stops when RegisterClassExA
// fails ("Error register window class !!").
//
// Each call answers as a window system that is not there: no class, window or menu (false, null),
// no title and no dialog list (false), no messages to pump. The accessibility keys are left alone.
// (The compare tests link the Win32 window, tests/win32, instead.)
#include "platform/platform.h"

namespace ls3d::platform {

// ---------------------------------------------------------------- the window

// (No window procedure or message loop asks the engine.)
void SetWindowEvents(WindowEvents *events) {}
bool RegisterWindowClass(HINSTANCE instance) { return false; }
HWND CreateMainWindow(HINSTANCE instance, bool fullscreen, int x, int y, int width, int height, bool withMenu,
                      HMENU menu)
{
    return nullptr;
}
HWND CreateChildWindow(HINSTANCE instance, HWND parent, int x, int y, int width, int height) { return nullptr; }
HMENU LoadDefaultMenu() { return nullptr; }
void Refresh(HWND window) {}
void Focus(HWND window) {}
bool SetTitle(HWND window, const char *title) { return false; }
bool ForegroundIsOwn(HWND window) { return false; }
bool AddDialog(HWND dialog) { return false; }
bool RemoveDialog(HWND dialog) { return false; }
void PumpMessages() {}

// ---------------------------------------------------------------- the mouse and keyboard

void DisableAccessibilityKeys() {}
void RestoreAccessibilityKeys() {}

}  // namespace ls3d::platform
