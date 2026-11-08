// The Win32 platform (platform/platform.h): the window class and procedure, the message loop and
// the other Windows calls IGraph made itself (IGraph.cpp), moved here unchanged. The window, cursor
// and clock functions are the Win32 implementation of platform/windowing.h; the threads, locks,
// events and libraries are every implementation's.
#include "platform/windowing.h"

#include <mmsystem.h>

#include "core/c_vector.h"
#include "dx/dinput_types.h"

namespace ls3d::platform {

namespace {

const char kWindowClass[] = "LS3D";
const char kWindowTitle[] = "LS3D engine";

C_vector<HWND> s_dialogs(0x20);                 // 0x100d19b0

// Accessibility keys switched off while the engine runs (SPI_GET/SETSTICKYKEYS ...). Only the
// sticky keys structure gets its size: the filter and toggle keys calls fail as in the original.
STICKYKEYS s_stickyKeys;                        // 0x100d1e3c
FILTERKEYS s_filterKeys;                        // 0x100d1ddc
TOGGLEKEYS s_toggleKeys;                        // 0x100d2164

// 0x1006b820: IGraph's window procedure.
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 0;
    case WM_DESTROY:
        if (!Events()->OnDestroy())
            return 1;
        PostQuitMessage(0);
        break;
    case WM_CLOSE:
        // (When the engine is initialised, OnClose closes it and exits.)
        if (!Events()->OnClose(static_cast<int>(wParam)))
            return 1;
        break;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        // F10 and Alt combinations would open the window menu.
        if (wParam == VK_F10 || (lParam & 0x20000000))
            return 1;
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_SCREENSAVE || (wParam & 0xfff0) == SC_MONITORPOWER)
            return 1;
        break;
    case WM_ACTIVATE:
        Events()->OnActivate(LOWORD(wParam) != WA_INACTIVE, HIWORD(wParam) != 0);
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// The window-message keyboard (IGKEY_WINDOW_MESSAGES): arrows and delete from WM_KEYDOWN, the rest
// from WM_CHAR, as DirectInput codes.
void KeyFromMessage(const MSG &msg)
{
    if (msg.message == WM_KEYDOWN) {
        switch (msg.wParam) {
        case VK_LEFT: Events()->OnKeyMessage(DIK_LEFT, 0); break;
        case VK_UP: Events()->OnKeyMessage(DIK_UP, 0); break;
        case VK_RIGHT: Events()->OnKeyMessage(DIK_RIGHT, 0); break;
        case VK_DOWN: Events()->OnKeyMessage(DIK_DOWN, 0); break;
        case VK_DELETE: Events()->OnKeyMessage(DIK_DELETE, 0); break;
        }
    } else if (msg.message == WM_CHAR && static_cast<LONG>(msg.lParam) >= 0) {
        switch (msg.wParam) {
        case 8:
            Events()->OnKeyMessage(DIK_BACK, 8);
            break;
        case 9:
            Events()->OnKeyMessage(DIK_TAB, 9);
            break;
        case 0x1b:
            Events()->OnKeyMessage(DIK_ESCAPE, 0x1b);
            break;
        default: {
            const SHORT vk = VkKeyScanA(static_cast<CHAR>(msg.wParam));
            if (vk == -1)
                Events()->OnKeyMessage(0, 0);
            else
                Events()->OnKeyMessage(static_cast<int>(MapVirtualKeyA(vk & 0xff, 0)), static_cast<int>(msg.wParam));
        }
        }
    }
}

}  // namespace

// ---------------------------------------------------------------- threads and locks

// The handles are the Windows thread handles and critical sections.
Thread *StartThread(ThreadProc proc, void *param)
{
    DWORD threadId;
    return reinterpret_cast<Thread *>(CreateThread(nullptr, 0, proc, param, 0, &threadId));
}
void SetPriority(Thread *thread, int priority) { SetThreadPriority(reinterpret_cast<HANDLE>(thread), priority); }
void Suspend(Thread *thread) { SuspendThread(reinterpret_cast<HANDLE>(thread)); }
void Resume(Thread *thread) { ResumeThread(reinterpret_cast<HANDLE>(thread)); }
void Terminate(Thread *thread, uint32_t exitCode) { TerminateThread(reinterpret_cast<HANDLE>(thread), exitCode); }
void Join(Thread *thread) { WaitForSingleObject(reinterpret_cast<HANDLE>(thread), INFINITE); }
void Release(Thread *thread) { CloseHandle(reinterpret_cast<HANDLE>(thread)); }

Mutex *NewMutex()
{
    auto *section = new CRITICAL_SECTION;
    InitializeCriticalSection(section);
    return reinterpret_cast<Mutex *>(section);
}
void DeleteMutex(Mutex *mutex)
{
    auto *section = reinterpret_cast<CRITICAL_SECTION *>(mutex);
    DeleteCriticalSection(section);
    delete section;
}
void Lock(Mutex *mutex) { EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(mutex)); }
void Unlock(Mutex *mutex) { LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(mutex)); }

// (Event handles are the Windows event handles.)
Event *NewEvent() { return reinterpret_cast<Event *>(CreateEventA(nullptr, FALSE, FALSE, nullptr)); }
void DeleteEvent(Event *event) { CloseHandle(reinterpret_cast<HANDLE>(event)); }
void SetEvent(Event *event) { ::SetEvent(reinterpret_cast<HANDLE>(event)); }
bool WaitEvent(Event *event)
{
    // 0xBF: QS_ALLINPUT of the original's SDK (no QS_SENDMESSAGE / QS_RAWINPUT).
    HANDLE handle = reinterpret_cast<HANDLE>(event);
    return MsgWaitForMultipleObjects(1, &handle, FALSE, INFINITE, 0xbf) == WAIT_OBJECT_0;
}
HANDLE NativeEvent(Event *event) { return reinterpret_cast<HANDLE>(event); }
Event *EventFromNative(HANDLE handle) { return reinterpret_cast<Event *>(handle); }

Library *OpenLibrary(const char *name) { return reinterpret_cast<Library *>(LoadLibraryA(name)); }
void *LibrarySymbol(Library *library, const char *name)
{
    return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
}
void CloseLibrary(Library *library) { FreeLibrary(reinterpret_cast<HMODULE>(library)); }

namespace {

// The Win32 implementation of the window, cursor and clock functions.
class WindowingWin32 final : public Windowing {
public:
    // ---- time

    uint32_t Milliseconds() override { return timeGetTime(); }
    uint32_t TickCount() override { return GetTickCount(); }
    void Pause(uint32_t ms) override { Sleep(ms); }

    // ---- the window

    bool RegisterWindowClass(HINSTANCE instance) override
    {
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof wc;
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kWindowClass;
        return RegisterClassExA(&wc) != 0;
    }

    HWND CreateMainWindow(HINSTANCE instance, bool fullscreen, int x, int y, int width, int height, bool withMenu,
                          HMENU menu) override
    {
        if (fullscreen)
            return CreateWindowExA(WS_EX_TOPMOST, kWindowClass, kWindowTitle, WS_VISIBLE, x, y, width, height,
                                   GetDesktopWindow(), menu, instance, nullptr);
        const int frameX = GetSystemMetrics(SM_CXFIXEDFRAME);
        const int frameY = GetSystemMetrics(SM_CYFIXEDFRAME);
        const int caption = GetSystemMetrics(SM_CYCAPTION);
        const int menuHeight = withMenu ? GetSystemMetrics(SM_CYMENU) : 0;
        const DWORD style = WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        return CreateWindowExA(0, kWindowClass, kWindowTitle, style, x - frameX, y - menuHeight - caption - frameY,
                               width + frameX * 2, height + frameY * 2 + menuHeight + caption, GetDesktopWindow(), menu,
                               instance, nullptr);
    }

    HWND CreateChildWindow(HINSTANCE instance, HWND parent, int x, int y, int width, int height) override
    {
        return CreateWindowExA(0, kWindowClass, kWindowTitle, WS_CHILD | WS_VISIBLE, x, y, width, height, parent, nullptr,
                               instance, nullptr);
    }

    HMENU LoadDefaultMenu() override
    {
        return LoadMenuA(GetModuleHandleA("LS3D.DLL"), MAKEINTRESOURCEA(123));
    }

    void Refresh(HWND window) override { UpdateWindow(window); }
    void Focus(HWND window) override { SetFocus(window); }
    void Minimize(HWND window) override { ShowWindow(window, SW_MINIMIZE); }
    bool SetTitle(HWND window, const char *title) override { return SetWindowTextA(window, title) != FALSE; }

    bool ForegroundIsOwn(HWND window) override
    {
        const HWND foreground = GetForegroundWindow();
        return IsChild(window, foreground) || window == foreground;
    }

    POINT ClientOrigin(HWND window, bool framed, bool withMenu) override
    {
        RECT rc;
        GetWindowRect(window, &rc);
        if (framed) {
            const int frameX = GetSystemMetrics(SM_CXFIXEDFRAME);
            const int frameY = GetSystemMetrics(SM_CYFIXEDFRAME);
            const int caption = GetSystemMetrics(SM_CYCAPTION);
            const int menu = withMenu ? GetSystemMetrics(SM_CYMENU) : 0;
            rc.left += frameX;
            rc.top += menu + caption + frameY;
        }
        return {rc.left, rc.top};
    }

    bool AddDialog(HWND dialog) override
    {
        if (!dialog)
            return false;
        s_dialogs.PushBack(dialog);
        return true;
    }

    bool RemoveDialog(HWND dialog) override
    {
        return s_dialogs.RemoveSwap(dialog);
    }

    // 0x1006a470 (IGraph::ProcessWinMessages)
    void PumpMessages() override
    {
        for (;;) {
            MSG msg;
            while (!PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE | PM_NOYIELD)) {
                if (!Events()->WaitWhileIdle())
                    return;
                WaitMessage();
            }
            int dialog = 0;
            for (; dialog < s_dialogs.Size(); ++dialog) {
                if (IsDialogMessageA(s_dialogs[dialog], &msg))
                    break;
            }
            // Typed characters (not repeats of a held key) for ReadChar.
            if (msg.message == WM_CHAR && static_cast<LONG>(msg.lParam) >= 0)
                Events()->OnChar(static_cast<int>(msg.wParam));
            if (Events()->KeyboardFromMessages())
                KeyFromMessage(msg);
            if (dialog == s_dialogs.Size()) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }
    }

    // ---- the mouse and keyboard

    bool SetCursorPosition(int x, int y) override { return SetCursorPos(x, y) != FALSE; }
    void ReleaseCursorClip() override { ClipCursor(nullptr); }
    bool MouseButtonsSwapped() override { return GetSystemMetrics(SM_SWAPBUTTON) != 0; }

    void DisableAccessibilityKeys() override
    {
        // The original switches filter and toggle keys "off" only when sticky keys had flags, and never
        // sets their structure sizes, so those calls fail.
        s_stickyKeys.cbSize = sizeof s_stickyKeys;
        SystemParametersInfoA(SPI_GETSTICKYKEYS, sizeof s_stickyKeys, &s_stickyKeys, 0);
        const DWORD stickyFlags = s_stickyKeys.dwFlags;
        if (stickyFlags) {
            s_stickyKeys.dwFlags = 0;
            SystemParametersInfoA(SPI_SETSTICKYKEYS, s_stickyKeys.cbSize, &s_stickyKeys, 0);
            s_stickyKeys.dwFlags = stickyFlags;
        }
        SystemParametersInfoA(SPI_GETFILTERKEYS, s_filterKeys.cbSize, &s_filterKeys, 0);
        if (s_stickyKeys.dwFlags) {
            const DWORD saved = s_filterKeys.dwFlags;
            s_filterKeys.dwFlags = 0;
            SystemParametersInfoA(SPI_SETFILTERKEYS, s_filterKeys.cbSize, &s_filterKeys, 0);
            s_filterKeys.dwFlags = saved;
        }
        SystemParametersInfoA(SPI_GETTOGGLEKEYS, s_toggleKeys.cbSize, &s_toggleKeys, 0);
        if (s_stickyKeys.dwFlags) {
            const DWORD saved = s_toggleKeys.dwFlags;
            s_toggleKeys.dwFlags = 0;
            SystemParametersInfoA(SPI_SETTOGGLEKEYS, s_toggleKeys.cbSize, &s_toggleKeys, 0);
            s_toggleKeys.dwFlags = saved;
        }
    }

    void RestoreAccessibilityKeys() override
    {
        SystemParametersInfoA(SPI_SETSTICKYKEYS, s_stickyKeys.cbSize, &s_stickyKeys, 0);
        SystemParametersInfoA(SPI_SETFILTERKEYS, s_filterKeys.cbSize, &s_filterKeys, 0);
        SystemParametersInfoA(SPI_SETTOGGLEKEYS, s_toggleKeys.cbSize, &s_toggleKeys, 0);
    }
};

WindowingWin32 s_windowing;

}  // namespace

Windowing *Win32Windowing() { return &s_windowing; }

bool Win32DialogMessage(MSG *msg)
{
    for (int i = 0; i < s_dialogs.Size(); ++i) {
        if (IsDialogMessageA(s_dialogs[i], msg))
            return true;
    }
    return false;
}

}  // namespace ls3d::platform
