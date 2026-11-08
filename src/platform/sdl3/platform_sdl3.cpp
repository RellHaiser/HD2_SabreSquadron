// The SDL3 implementation of the platform's window, cursor and clock functions
// (platform/windowing.h), chosen with LS3DF.ini [Platform] SDL3=1. SDL3.dll is loaded, and SDL's
// video started, on the first window call (on the game's thread, never from DllMain).
//
// - The main window is SDL's, captioned (windowed) or borderless and on top (fullscreen: Direct3D
//   takes it over as it did the original's popup). Its native handle is what IGraph and the game
//   get. SDL places and sizes its client area.
// - The message loop is SDL's event loop. IGraph's decisions come from SDL's events: a close
//   request, focus gained, lost or minimised (activation), typed text (characters, the
//   window-message keyboard), Backspace, Tab, Return and Escape (the characters Windows typed for
//   them) and the arrows and Delete (the window-message keyboard's keys). Wheel notches go to the
//   input device.
// - The clock is SDL's, counted from where Windows' timeGetTime was when SDL took over: the game
//   reads timeGetTime itself and may compare.
// - What SDL has no notion of stays Windows': the editors' child windows, their menu and modeless
//   dialogs (SDL's message hook gives the dialogs their messages first), the accessibility keys.
//   SDL handles the screen saver and the window menu's keys itself, and reports the mouse buttons
//   as the user set them up (so no swapping is asked of IGraph).
#include "platform/windowing.h"

#include <cstring>
#include <string>

#include "dx/dinput_types.h"
#include "platform/sdl3/sdl3_api.h"

namespace ls3d::platform {

namespace {

using sdl3::api;

// The window's title in UTF-8 (the engine's strings are in the system's ANSI code page).
std::string Utf8(const char *text)
{
#ifdef _WIN32
    wchar_t wide[512];
    const int n = MultiByteToWideChar(CP_ACP, 0, text, -1, wide, 512);
    if (n <= 0)
        return text;
    char utf8[1536];
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof utf8, nullptr, nullptr) > 0 ? utf8 : text;
#else
    return text;
#endif
}

// A typed character in the system's ANSI code page (0: none), as Windows' WM_CHAR gave it.
int Ansi(uint32_t codepoint)
{
#ifdef _WIN32
    if (codepoint > 0xffff)
        return 0;
    const wchar_t wide = static_cast<wchar_t>(codepoint);
    char ch = 0;
    BOOL lossy = FALSE;
    if (WideCharToMultiByte(CP_ACP, 0, &wide, 1, &ch, 1, nullptr, &lossy) != 1 || lossy)
        return 0;
    return static_cast<unsigned char>(ch);
#else
    return codepoint < 256 ? static_cast<int>(codepoint) : 0;
#endif
}

// The next code point of UTF-8 text (advances `p`).
uint32_t NextCodepoint(const char *&p)
{
    const auto *u = reinterpret_cast<const unsigned char *>(p);
    uint32_t c = *u++;
    int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (more)
        c &= 0x3f >> more;
    while (more-- && (*u & 0xc0) == 0x80)
        c = c << 6 | (*u++ & 0x3f);
    p = reinterpret_cast<const char *>(u);
    return c;
}

#ifdef _WIN32
// Windows' modeless dialogs get their messages first (IsDialogMessage).
bool SDLCALL MessageHook(void *, MSG *msg) { return !Win32DialogMessage(msg); }
#endif

class WindowingSdl3 final : public Windowing {
public:
    void StartClock()
    {
#ifdef _WIN32
        clockOffset = Win32Windowing()->Milliseconds() - static_cast<uint32_t>(api.SDL_GetTicks());
        tickOffset = Win32Windowing()->TickCount() - static_cast<uint32_t>(api.SDL_GetTicks());
        api.SDL_SetWindowsMessageHook(MessageHook, nullptr);
#endif
    }

    // ---- time
    uint32_t Milliseconds() override { return static_cast<uint32_t>(api.SDL_GetTicks()) + clockOffset; }
    uint32_t TickCount() override { return static_cast<uint32_t>(api.SDL_GetTicks()) + tickOffset; }
    void Pause(uint32_t ms) override { api.SDL_Delay(ms); }

    // ---- the window
    // (The engine's window class is still registered: the editors' child windows are Windows'.)
    bool RegisterWindowClass(HINSTANCE instance) override { return Win32Windowing()->RegisterWindowClass(instance); }
    HWND CreateMainWindow(HINSTANCE, bool fullscreen, int x, int y, int width, int height, bool, HMENU menu) override
    {
        const SDL_WindowFlags flags =
            SDL_WINDOW_HIDDEN | (fullscreen ? SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP : 0);
        SDL_Window *window = api.SDL_CreateWindow("LS3D engine", width, height, flags);
        if (!window)
            return nullptr;
        api.SDL_SetWindowPosition(window, x, y);
        main = static_cast<HWND>(
            api.SDL_GetPointerProperty(api.SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#ifdef _WIN32
        if (menu && main) {
            SetMenu(main, menu);
            api.SDL_SetWindowSize(window, width, height);     // (the client area keeps its size)
        }
#endif
        api.SDL_ShowWindow(window);
        api.SDL_StartTextInput(window);
        sdl3::g_window = window;
        return main;
    }
    HWND CreateChildWindow(HINSTANCE instance, HWND parent, int x, int y, int width, int height) override
    {
        return Win32Windowing()->CreateChildWindow(instance, parent, x, y, width, height);
    }
    HMENU LoadDefaultMenu() override { return Win32Windowing()->LoadDefaultMenu(); }
    void Refresh(HWND window) override
    {
        if (window != main)
            Win32Windowing()->Refresh(window);
    }
    void Focus(HWND window) override
    {
        if (window == main && main)
            api.SDL_RaiseWindow(sdl3::g_window);
        else
            Win32Windowing()->Focus(window);
    }
    bool SetTitle(HWND window, const char *title) override
    {
        if (window != main || !main)
            return Win32Windowing()->SetTitle(window, title);
        return api.SDL_SetWindowTitle(sdl3::g_window, Utf8(title).c_str());
    }
    bool ForegroundIsOwn(HWND window) override
    {
        if (window != main || !main)
            return Win32Windowing()->ForegroundIsOwn(window);
        return api.SDL_GetKeyboardFocus() == sdl3::g_window;
    }
    // (SDL's window position is its client area's.)
    POINT ClientOrigin(HWND window, bool framed, bool withMenu) override
    {
        if (window != main || !main)
            return Win32Windowing()->ClientOrigin(window, framed, withMenu);
        int x = 0, y = 0;
        api.SDL_GetWindowPosition(sdl3::g_window, &x, &y);
        return {x, y};
    }
    bool AddDialog(HWND dialog) override { return Win32Windowing()->AddDialog(dialog); }
    bool RemoveDialog(HWND dialog) override { return Win32Windowing()->RemoveDialog(dialog); }
    void PumpMessages() override
    {
        SDL_Event e;
        for (;;) {
            while (api.SDL_PollEvent(&e))
                Handle(e);
            if (!Events()->WaitWhileIdle())
                return;
            if (api.SDL_WaitEvent(&e))
                Handle(e);
        }
    }

    // ---- the mouse and keyboard
    bool SetCursorPosition(int x, int y) override
    {
        return api.SDL_WarpMouseGlobal(static_cast<float>(x), static_cast<float>(y));
    }
    void ReleaseCursorClip() override
    {
        if (sdl3::g_window)
            api.SDL_SetWindowMouseRect(sdl3::g_window, nullptr);
    }
    bool MouseButtonsSwapped() override { return false; }
    void DisableAccessibilityKeys() override { Win32Windowing()->DisableAccessibilityKeys(); }
    void RestoreAccessibilityKeys() override { Win32Windowing()->RestoreAccessibilityKeys(); }

private:
    void Handle(const SDL_Event &e)
    {
        WindowEvents *events = Events();
        switch (e.type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            // (Ignored while the engine is not initialised; otherwise it closes and the process exits.)
            events->OnClose(0);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            events->OnActivate(true, false);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            events->OnActivate(false, (api.SDL_GetWindowFlags(sdl3::g_window) & SDL_WINDOW_MINIMIZED) != 0);
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            events->OnActivate(false, true);
            break;
        case SDL_EVENT_KEY_DOWN:
            Key(e.key);
            break;
        case SDL_EVENT_TEXT_INPUT:
            Text(e.text.text);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            sdl3::g_wheel += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
            break;
        default:
            break;
        }
    }
    // A typed character, and for the window-message keyboard its key.
    void Typed(int ch, int dik)
    {
        Events()->OnChar(ch);
        if (Events()->KeyboardFromMessages())
            Events()->OnKeyMessage(dik, ch);
    }
    // The keys Windows typed a control character for, and the window-message keyboard's own keys.
    void Key(const SDL_KeyboardEvent &k)
    {
        switch (k.key) {
        case SDLK_BACKSPACE: Typed(8, DIK_BACK); return;
        case SDLK_TAB: Typed(9, DIK_TAB); return;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: Typed(13, DIK_RETURN); return;
        case SDLK_ESCAPE: Typed(0x1b, DIK_ESCAPE); return;
        default: break;
        }
        if (!Events()->KeyboardFromMessages())
            return;
        switch (k.key) {
        case SDLK_LEFT: Events()->OnKeyMessage(DIK_LEFT, 0); break;
        case SDLK_UP: Events()->OnKeyMessage(DIK_UP, 0); break;
        case SDLK_RIGHT: Events()->OnKeyMessage(DIK_RIGHT, 0); break;
        case SDLK_DOWN: Events()->OnKeyMessage(DIK_DOWN, 0); break;
        case SDLK_DELETE: Events()->OnKeyMessage(DIK_DELETE, 0); break;
        default: break;
        }
    }
    // Typed text: each character, with the key that types it (none: 0, as Windows' VkKeyScan).
    void Text(const char *utf8)
    {
        for (const char *p = utf8; *p;) {
            const uint32_t c = NextCodepoint(p);
            const int ch = Ansi(c);
            if (ch < 0x20)
                continue;
            int dik = 0;
            if (Events()->KeyboardFromMessages()) {
                SDL_Keymod mod = SDL_KMOD_NONE;
                SDL_Scancode sc = api.SDL_GetScancodeFromKey(c, &mod);
                if (sc == SDL_SCANCODE_UNKNOWN && c < 0x80)
                    sc = api.SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(c >= 'A' && c <= 'Z' ? c + 0x20 : c), &mod);
                dik = sdl3::DikOfScancode(sc);
            }
            Events()->OnChar(ch);
            if (Events()->KeyboardFromMessages())
                Events()->OnKeyMessage(dik, dik ? ch : 0);
        }
    }

    HWND main = nullptr;
    uint32_t clockOffset = 0, tickOffset = 0;
};

}  // namespace

Windowing *Sdl3Windowing()
{
    static WindowingSdl3 *windowing;
    static bool tried;
    if (!tried) {
        tried = true;
        if (sdl3::Start()) {
            windowing = new WindowingSdl3;
            windowing->StartClock();
        }
    }
    return windowing;
}

}  // namespace ls3d::platform
