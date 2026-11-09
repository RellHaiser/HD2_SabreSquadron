// Test of the platform's and the input device's SDL3 backends (src/platform/sdl3, src/input/sdl3)
// against the Win32 platform (src/platform/win32), which is IGraph's own code (compare_window
// compares it with the original). Both implementations get a main window on a desktop of the
// test's own (real typing stays out) and the same posted messages; what they tell IGraph
// (WindowEvents) must be the same:
// - typed characters and the window-message keyboard: letters, digits and symbols, Backspace,
//   Tab, Return, Escape, the arrows and Delete;
// - activation (focus lost and gained) and close requests;
// - the title and the client area's position.
// The SDL3 input device on its own:
// - every key DirectInput names, sent as Windows sends it (scan code, extended flag), must show in
//   the keyboard state under its own DIK code (the key table checked against SDL's own
//   translation of Windows' keys) and be released again;
// - mouse buttons, wheel (120 a notch) and motion; key names; acquiring only with the focus, the
//   input lost with it;
// - the clock: SDL's, at timeGetTime's count.
//
// SDL takes the focus only when its window is Windows' foreground window, and a desktop nobody looks
// at has none: SDL3.dll's GetForegroundWindow and GetFocus are pointed at the test, which answers
// the SDL window while it has the focus (the posted WM_SETFOCUS / WM_KILLFOCUS say so). Its
// GetCursorPos, GetKeyState, GetAsyncKeyState and TrackMouseEvent are the test's too: SDL reads
// them while it pumps, and posted messages do not change the real ones.
//
// Usage: test_platform_sdl3.exe (SDL3.dll next to it: build.py copies it)
//
// test-sources: src/platform/*.cpp src/platform/win32/*.cpp src/platform/sdl3/*.cpp
// test-sources: src/input/input.cpp src/input/sdl3/input_sdl3.cpp src/input/dinput/input_dinput.cpp
// test-sources: src/core/enhance.cpp src/core/debug.cpp
#include <windows.h>

#include <mmsystem.h>

#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/core/debug.h"
#include "../src/input/backends.h"
#include "../src/platform/sdl3/sdl3_api.h"
#include "../src/platform/windowing.h"

using namespace ls3d;

namespace {

int g_checks, g_failures;

// The window SDL is told is in the foreground (null: none).
HWND g_foreground;
HWND WINAPI FakeForeground() { return g_foreground; }

// Points `module`'s import of dll!function at `replacement`.
bool PatchImport(HMODULE module, const char *dll, const char *function, void *replacement)
{
    uint8_t *base = reinterpret_cast<uint8_t *>(module);
    const IMAGE_NT_HEADERS *nt =
        reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto *d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name; ++d) {
        if (_stricmp(reinterpret_cast<const char *>(base + d->Name), dll) != 0)
            continue;
        auto *names = reinterpret_cast<const IMAGE_THUNK_DATA *>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk
                                                                                                : d->FirstThunk));
        auto *slots = reinterpret_cast<IMAGE_THUNK_DATA *>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            auto *byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char *>(byName->Name), function) != 0)
                continue;
            DWORD old;
            VirtualProtect(&slots->u1.Function, sizeof slots->u1.Function, PAGE_READWRITE, &old);
            slots->u1.Function = reinterpret_cast<uintptr_t>(replacement);
            VirtualProtect(&slots->u1.Function, sizeof slots->u1.Function, old, &old);
            return true;
        }
    }
    return false;
}

// The cursor SDL is told about (screen coordinates; SDL also reads the cursor while it pumps).
POINT g_cursor = {-1000, -1000};
BOOL WINAPI FakeCursorPos(LPPOINT p)
{
    *p = g_cursor;
    return TRUE;
}

// The mouse moved to (x, y) of the window's client area, as Windows tells it.
void MoveTo(HWND window, int x, int y)
{
    POINT p = {x, y};
    ClientToScreen(window, &p);
    g_cursor = p;
    PostMessageA(window, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
}

// The window gets / loses the focus, as Windows tells it.
void Focus(HWND window, bool on)
{
    g_foreground = on ? window : nullptr;
    PostMessageA(window, WM_ACTIVATE, on ? WA_ACTIVE : WA_INACTIVE, 0);
    PostMessageA(window, on ? WM_SETFOCUS : WM_KILLFOCUS, 0, 0);
}

void Check(bool ok, const char *fmt, ...)
{
    ++g_checks;
    if (ok)
        return;
    ++g_failures;
    if (g_failures > 40)
        return;
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    std::printf("FAIL: %s\n", text);
}

// IGraph's side, recorded.
class S_events final : public platform::WindowEvents {
public:
    bool OnClose(int code) override
    {
        Log("close(%d)", code);
        return false;                       // (as before IGraph is initialised: ignored)
    }
    bool OnDestroy() override { return false; }
    void OnActivate(bool active, bool minimized) override { Log("activate(%d,%d)", active, minimized); }
    bool WaitWhileIdle() override { return false; }
    void OnChar(int ch) override { Log("char(%d)", ch); }
    bool KeyboardFromMessages() override { return messageKeyboard; }
    void OnKeyMessage(int key, int ch) override { Log("key(%02x,%d)", key, ch); }

    std::vector<std::string> log;
    bool messageKeyboard = true;

private:
    void Log(const char *fmt, ...)
    {
        char text[64];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(text, sizeof text, fmt, args);
        va_end(args);
        log.push_back(text);
    }
} g_events;

std::string Join(const std::vector<std::string> &v)
{
    std::string s;
    for (const std::string &e : v)
        s += e + " ";
    return s;
}

// A key as Windows sends it: the virtual key, and the scan code (bit 24: an extended key).
LPARAM KeyParam(UINT scan, bool extended, bool up)
{
    return 1 | (static_cast<LPARAM>(scan) << 16) | (extended ? 1 << 24 : 0) | (up ? 0xC0000000 : 0);
}
// The keys held, as GetKeyState tells them to SDL (posted messages do not change the real state;
// SDL releases a Shift or Windows key that GetKeyState says is up).
SHORT g_keyState[256];
SHORT WINAPI FakeKeyState(int vk) { return g_keyState[vk & 0xff]; }
// The mouse is over the window as far as SDL can tell (the real cursor is not: SDL would be told it
// left).
BOOL WINAPI FakeTrackMouseEvent(LPTRACKMOUSEEVENT) { return TRUE; }

// A mouse button pressed or released at (x, y) of the client area.
void PostButton(HWND w, UINT message, int vk, int x, int y)
{
    const bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN;
    g_keyState[vk] = down ? static_cast<SHORT>(0x8000) : 0;
    WPARAM held = 0;
    held |= g_keyState[VK_LBUTTON] ? MK_LBUTTON : 0;
    held |= g_keyState[VK_RBUTTON] ? MK_RBUTTON : 0;
    PostMessageA(w, message, held, MAKELPARAM(x, y));
}

void PostKey(HWND w, UINT vk, UINT scan, bool extended, bool up)
{
    g_keyState[vk & 0xff] = up ? 0 : static_cast<SHORT>(0x8000);
    PostMessageA(w, up ? WM_KEYUP : WM_KEYDOWN, vk, KeyParam(scan, extended, up));
}
// A key pressed and released (the window's TranslateMessage types its character).
void PostTyped(HWND w, UINT vk, UINT scan, bool extended)
{
    PostKey(w, vk, scan, extended, false);
    PostKey(w, vk, scan, extended, true);
}

// What the implementation tells IGraph for the same messages.
std::string Scenario(platform::Windowing *w, HWND window)
{
    g_events.log.clear();
    auto pump = [&] {
        for (int i = 0; i < 5; ++i) {
            w->PumpMessages();
            Sleep(5);
        }
    };
    // Focus first (as the window gets it when shown), then the keys.
    Focus(window, true);
    pump();
    const std::string activation = Join(g_events.log);
    g_events.log.clear();
    const struct {
        UINT vk, scan;
        bool extended;
    } keys[] = {
        {'A', 0x1e, false},      {'B', 0x30, false},     {'1', 0x02, false},   {VK_OEM_MINUS, 0x0c, false},
        {VK_SPACE, 0x39, false}, {VK_BACK, 0x0e, false}, {VK_TAB, 0x0f, false}, {VK_RETURN, 0x1c, false},
        {VK_RETURN, 0x1c, true}, {VK_ESCAPE, 0x01, false}, {VK_LEFT, 0x4b, true}, {VK_UP, 0x48, true},
        {VK_RIGHT, 0x4d, true},  {VK_DOWN, 0x50, true},  {VK_DELETE, 0x53, true}, {VK_F5, 0x3f, false},
    };
    for (bool messageKeyboard : {true, false}) {
        g_events.messageKeyboard = messageKeyboard;
        for (const auto &k : keys) {
            PostTyped(window, k.vk, k.scan, k.extended);
            pump();
        }
    }
    g_events.messageKeyboard = true;
    const std::string typing = Join(g_events.log);
    g_events.log.clear();
    PostMessageA(window, WM_CLOSE, 0, 0);
    pump();
    // The focus lost and back.
    Focus(window, false);
    pump();
    Focus(window, true);
    pump();
    const std::string closing = Join(g_events.log);
    return activation + "| " + typing + "| " + closing;
}

// The SDL3 input device on the SDL3 window.
void TestInput(HWND window, platform::Windowing *w)
{
    auto pump = [&] {
        for (int i = 0; i < 3; ++i) {
            w->PumpMessages();
            Sleep(5);
        }
    };
    input::Device *d = input::Sdl3InputDevice();
    input::System *system = nullptr;
    d->Create(GetModuleHandleA(nullptr), &system);
    input::Peripheral *keyboard = nullptr, *mouse = nullptr;
    Check(d->CreateDevice(system, GUID_SysKeyboard, &keyboard) == DI_OK && keyboard, "keyboard");
    Check(d->CreateDevice(system, GUID_SysMouse, &mouse) == DI_OK && mouse, "mouse");
    d->SetCooperativeLevel(keyboard, window, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND);
    d->SetCooperativeLevel(mouse, window, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND);
    Focus(window, true);
    pump();
    Check(d->Acquire(keyboard) == DI_OK && d->Acquire(mouse) == DI_OK, "acquired with the focus");

    // Every key: down, in the state under its own code; up, gone.
    BYTE state[256];
    int keys = 0, wrong = 0;
    for (int dik = 1; dik < 256; ++dik) {
        if (sdl3::ScancodeOfDik(static_cast<uint8_t>(dik)) == SDL_SCANCODE_UNKNOWN)
            continue;
        ++keys;
        // Windows' scan codes are DirectInput's, but for NumLock (extended) and Pause (not).
        const UINT scan = dik & 0x7f;
        const bool extended = dik == DIK_NUMLOCK || (dik != DIK_PAUSE && (dik & 0x80));
        const UINT vk = MapVirtualKeyA(extended ? 0xe000 | scan : scan, MAPVK_VSC_TO_VK_EX);
        PostKey(window, vk, scan, extended, false);
        pump();
        d->GetDeviceState(keyboard, sizeof state, state);
        int down = 0;
        for (int i = 0; i < 256; ++i)
            down += (state[i] & 0x80) != 0;
        const bool ok = (state[dik] & 0x80) && down == 1;
        PostKey(window, vk, scan, extended, true);
        pump();
        d->GetDeviceState(keyboard, sizeof state, state);
        int after = 0;
        for (int i = 0; i < 256; ++i)
            after += (state[i] & 0x80) != 0;
        if (!ok || after) {
            if (++wrong <= 10)
                std::printf("  key %02x: down %d (%d keys), %d keys after release\n", dik, state[dik] != 0, down, after);
        }
    }
    std::printf("keyboard: %d keys sent as Windows sends them\n", keys);
    Check(!wrong, "%d of %d keys not in the state as their own code", wrong, keys);

    // The mouse: buttons, wheel, motion.
    DIMOUSESTATE2 m;
    d->GetDeviceState(mouse, sizeof m, &m);         // (what came before)
    MoveTo(window, 10, 10);
    pump();
    d->GetDeviceState(mouse, sizeof m, &m);
    MoveTo(window, 25, 7);
    PostButton(window, WM_LBUTTONDOWN, VK_LBUTTON, 25, 7);
    PostMessageA(window, WM_MOUSEWHEEL, MAKEWPARAM(MK_LBUTTON, 240), MAKELPARAM(g_cursor.x, g_cursor.y));
    pump();
    d->GetDeviceState(mouse, sizeof m, &m);
    Check(m.lX == 15 && m.lY == -3, "mouse motion %ld,%ld, expected 15,-3", m.lX, m.lY);
    Check(m.lZ == 240, "wheel %ld, expected 240 (two notches)", m.lZ);
    Check(m.rgbButtons[0] == 0x80 && !m.rgbButtons[1] && !m.rgbButtons[2], "left button down");
    PostButton(window, WM_LBUTTONUP, VK_LBUTTON, 25, 7);
    PostButton(window, WM_RBUTTONDOWN, VK_RBUTTON, 25, 7);
    pump();
    d->GetDeviceState(mouse, sizeof m, &m);
    Check(!m.rgbButtons[0] && m.rgbButtons[1] == 0x80 && m.lX == 0 && m.lY == 0 && m.lZ == 0,
          "right button down, no motion or wheel since");
    PostButton(window, WM_RBUTTONUP, VK_RBUTTON, 25, 7);
    pump();

    // Key names.
    DIPROPSTRING name = {};
    name.diph.dwSize = sizeof name;
    name.diph.dwHeaderSize = sizeof name.diph;
    name.diph.dwHow = DIPH_BYOFFSET;
    for (const auto &k : {std::make_pair(DIK_A, L"A"), std::make_pair(DIK_ESCAPE, L"Escape"),
                          std::make_pair(DIK_LEFT, L"Left"), std::make_pair(DIK_F5, L"F5")}) {
        name.diph.dwObj = k.first;
        const HRESULT hr = d->GetProperty(keyboard, DIPROP_KEYNAME, &name.diph);
        Check(hr == DI_OK && !std::wcscmp(name.wsz, k.second), "key name of %02x: %ls", k.first, name.wsz);
    }

    // The focus lost: the input with it; acquiring needs it back.
    Focus(window, false);
    pump();
    Check(d->GetDeviceState(keyboard, sizeof state, state) == DIERR_INPUTLOST, "input lost with the focus");
    Check(d->GetDeviceState(keyboard, sizeof state, state) == DIERR_NOTACQUIRED, "then not acquired");
    Check(d->Acquire(keyboard) == DIERR_OTHERAPPHASPRIO, "no acquiring without the focus");
    Focus(window, true);
    pump();
    Check(d->Acquire(keyboard) == DI_OK && d->GetDeviceState(keyboard, sizeof state, state) == DI_OK,
          "acquired again with the focus");
    d->Release(keyboard);
    d->Release(mouse);
    d->Release(system);
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    ls3d::g_logDisabled = true;
    // A desktop of the test's own: real typing does not reach its windows.
    HDESK desktop = CreateDesktopA("LS3DFSdlTestDesktop", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop)) {
        std::printf("cannot use a desktop of the test's own\n");
        return 2;
    }
    platform::SetWindowEvents(&g_events);
    HINSTANCE instance = GetModuleHandleA(nullptr);
    platform::Windowing *win32 = platform::Win32Windowing();
    win32->RegisterWindowClass(instance);
    HWND w32 = win32->CreateMainWindow(instance, false, 100, 120, 320, 200, false, nullptr);
    Check(w32 != nullptr, "the Win32 window");
    const POINT o32 = win32->ClientOrigin(w32, true, false);
    const std::string expected = Scenario(win32, w32);
    DestroyWindow(w32);

    platform::Windowing *sdl = platform::Sdl3Windowing();
    if (!sdl) {
        std::printf("no SDL3 (SDL3.dll next to the test)\n");
        return 2;
    }
    platform::UseWindowing(sdl);
    HMODULE sdlLibrary = GetModuleHandleA("SDL3.dll");
    if (!PatchImport(sdlLibrary, "USER32.dll", "GetForegroundWindow", reinterpret_cast<void *>(&FakeForeground)) ||
        !PatchImport(sdlLibrary, "USER32.dll", "GetFocus", reinterpret_cast<void *>(&FakeForeground)) ||
        !PatchImport(sdlLibrary, "USER32.dll", "GetCursorPos", reinterpret_cast<void *>(&FakeCursorPos)) ||
        !PatchImport(sdlLibrary, "USER32.dll", "GetKeyState", reinterpret_cast<void *>(&FakeKeyState)) ||
        !PatchImport(sdlLibrary, "USER32.dll", "GetAsyncKeyState", reinterpret_cast<void *>(&FakeKeyState)) ||
        !PatchImport(sdlLibrary, "USER32.dll", "TrackMouseEvent", reinterpret_cast<void *>(&FakeTrackMouseEvent))) {
        std::printf("cannot patch SDL3.dll's user32 imports\n");
        return 2;
    }
    HWND hs = sdl->CreateMainWindow(instance, false, 140, 160, 320, 200, false, nullptr);
    Check(hs != nullptr && IsWindow(hs), "the SDL3 window and its native handle");
    const POINT os = sdl->ClientOrigin(hs, true, false);
    Check(o32.x == 100 && o32.y == 120 && os.x == 140 && os.y == 160, "client origins %ld,%ld / %ld,%ld", o32.x, o32.y,
          os.x, os.y);
    const std::string got = Scenario(sdl, hs);
    std::printf("Win32: %s\nSDL3:  %s\n", expected.c_str(), got.c_str());
    Check(got == expected, "what IGraph is told differs");
    Check(sdl->SetTitle(hs, "LS3D test ..zzZZ"), "SetTitle");
    char title[64] = {};
    GetWindowTextA(hs, title, sizeof title);
    Check(!std::strcmp(title, "LS3D test ..zzZZ"), "title: %s", title);
    Check(!sdl->MouseButtonsSwapped(), "SDL reports the buttons as set up: none swapped for IGraph");

    // The platform's input device goes with it.
    input::ChooseDevice();
    Check(input::g_pInput == input::Sdl3InputDevice(), "the SDL3 input device with the SDL3 platform");
    TestInput(hs, sdl);

    // The clock: at timeGetTime's count, moving with it.
    const DWORD t0 = timeGetTime();
    const uint32_t s0 = sdl->Milliseconds();
    Sleep(300);
    const DWORD t1 = timeGetTime();
    const uint32_t s1 = sdl->Milliseconds();
    Check(static_cast<int>(s0 - t0) >= -2 && static_cast<int>(s0 - t0) <= 20, "clock at timeGetTime's: %d ms apart",
          static_cast<int>(s0 - t0));
    Check(std::abs(static_cast<int>((s1 - s0) - (t1 - t0))) <= 20, "clock moves as timeGetTime: %u / %u ms", s1 - s0,
          t1 - t0);

    std::printf("test_platform_sdl3: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
