# The platform (platform)

The operating system services the engine uses besides drawing, sound and input are in
[`src/platform/platform.h`](../src/platform/platform.h). They are the window and its messages,
the mouse cursor, time, threads and locks. The Win32 implementation in
[`src/platform/win32`](../src/platform/win32) is IGraph's own Windows code, moved there unchanged.
The SDL3 implementation in [`src/platform/sdl3`](../src/platform/sdl3) takes over the window, its
messages, the cursor and the clock when LS3DF.ini says `[Platform] SDL3=1`
([below](#the-sdl3-implementation)).

## Design

**Functions, with the window part chosen at start-up.** The engine calls plain functions
(`platform::Milliseconds`, `platform::CreateMainWindow`, ...). The window, message, cursor and clock
functions go to a `platform::Windowing` object
([`windowing.h`](../src/platform/windowing.h)): Win32's, or SDL3's when LS3DF.ini asks for it and
`SDL3.dll` loads. The choice is made on the first window call (IGraph's Init, on the game's
thread), never from `DllMain`, where loading SDL is not allowed. The engine reads the clock from
static initialisers too (IGraph's FPS counter); until the choice, the clock is Win32's. Threads,
locks, events and libraries stay plain Win32 functions for both: DirectSound sets the events
itself, so they have to be Windows events while it is in use.

**IGraph keeps its decisions.** The backend does the operating system's part: it registers the
window class, creates the main and child windows with their frames, runs the window procedure and
the message loop, passes dialog messages to the editors' dialogs, and handles the cursor and the
accessibility keys. Whenever the original decided something, the backend asks IGraph through
`platform::WindowEvents`:

| Event | IGraph's decision |
|---|---|
| `OnClose` (WM_CLOSE) | ignored before Init; otherwise IGraph closes and the process exits |
| `OnDestroy` (WM_DESTROY) | ignored before Init; otherwise the message loop ends |
| `OnActivate` (WM_ACTIVATE) | deactivation: the "..zzZZ" title, suspending the texture thread; reactivation: the title back, device states reapplied, video volumes restored, the thread resumed |
| `WaitWhileIdle` | the message loop waits while the game is in the background (IG_WAIT_INACTIVE) |
| `OnChar` | typed characters for ReadChar |
| `KeyboardFromMessages`, `OnKeyMessage` | the window-message keyboard (IGKEY_WINDOW_MESSAGES) |

The backend's own policy stays with it. It swallows background erases, the F10 and Alt keys that
would open the window menu, and the screen saver and monitor power-off.

**Handles.** Windows, menus and instances are the native handles (`HWND`, `HMENU`, `HINSTANCE`),
because IGraph's interface hands them to the game. Threads, locks and events are opaque
(`platform::Thread`, `platform::Mutex`, `platform::Event`); with Win32 they are thread handles,
critical sections and auto-reset events. IShow's Ogg stream thread waits on an event that
DirectSound sets, so `NativeEvent` gives the sound device the handle.

**Time.** `Milliseconds()` is the engine's clock (`timeGetTime`), and every module reads it through
the platform now. `TickCount()` is `GetTickCount`, which the voice manager reads in one place, as
the original does.

**The boundary is checked.** `python build.py` (and `check`) fails when code outside
`src/platform/win32` calls one of the Windows functions the platform covers: the window, message,
cursor, thread, lock, event and time functions, `Sleep`, and `<mmsystem.h>`. Only the DirectShow
base classes keep theirs, as part of the Windows video backend ([the video device](video_device.md)).

## The SDL3 implementation

Chosen with `[Platform] SDL3=1` in LS3DF.ini ([ls3df_ini.md](ls3df_ini.md#platform)); without
`SDL3.dll` the DLL falls back to Win32 and says so in `log.txt`. It is the window half of phase 2 of
the [cross-platform plan](cross_platform.md); the [SDL3 input device](input_device.md#the-sdl3-backend)
goes with it. SDL3 is loaded at run time (`platform::OpenLibrary`, a table of the functions used in
[`sdl3_api.h`](../src/platform/sdl3/sdl3_api.h)), so the DLL needs it only when it is asked for.

- **The main window is SDL's.** It is captioned when windowed, and borderless and on top when
  fullscreen, as the original's popup was; Direct3D 8 takes it over as before. SDL places and sizes
  its client area. Its native handle (`SDL_PROP_WINDOW_WIN32_HWND_POINTER`) is what IGraph and the
  game get, so the game's own window calls still work. The editors' menu is put on it with
  Windows' `SetMenu`.
- **The message loop is SDL's event loop.** IGraph's decisions come from SDL's events:
  - a close request → `OnClose`;
  - focus gained, lost, or minimised → `OnActivate`;
  - typed text → `OnChar`, in the system's ANSI code page, and for the window-message keyboard
    its key, found with `SDL_GetScancodeFromKey`;
  - Backspace, Tab, Return (both) and Escape → the control characters Windows typed for them;
  - the arrows and Delete → keys of the window-message keyboard;
  - wheel notches → the input device.

  `WaitWhileIdle` waits on `SDL_WaitEvent`.
- **The clock is SDL's** (`SDL_GetTicks`), counted from where `timeGetTime` and `GetTickCount`
  were when SDL started. The game reads `timeGetTime` itself and compares it with the engine's.
- **Still Windows':** the editors' child windows, their menu and modeless dialogs (SDL's message
  hook gives the dialogs their messages first), and the accessibility keys. SDL handles the screen
  saver and the window menu's keys itself. It also reports the mouse buttons as the user set them
  up, so IGraph is told nothing is swapped.

**Building.** `build.py` needs SDL3's headers (3.2 or later, 32-bit) in `%SDL3_DIR%`, by default
`deps\sdl3-x86` next to the checkout. It copies `SDL3.dll` next to the DLL and the tests. To build
it from source with Visual Studio's CMake, run in an x86 developer prompt:

```bat
git clone --depth 1 --branch release-3.4.20 https://github.com/libsdl-org/SDL.git deps\SDL
cmake -S deps\SDL -B deps\SDL\build-x86 -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF ^
  -DCMAKE_INSTALL_PREFIX=deps\sdl3-x86
cmake --build deps\SDL\build-x86
cmake --install deps\SDL\build-x86
```

The 32-bit `SDL3-devel-*-VC.zip` from SDL's releases works too: point `SDL3_DIR` at its folder,
and `build.py` takes `lib\SDL3.dll`.

`python build.py` (and `check`) fails when code outside `src/platform/sdl3` and `src/input/sdl3`
includes SDL's headers or calls an SDL function.

## Not covered yet

Some Windows calls are not part of this seam, because they are small and only matter for another
platform:

- the DirectX 8.1 check (`dpnhpast.dll`) and the OS name in the log;
- COM start-up (DirectShow and EAX need it);
- `RECT` helpers;
- the texture directories' enumeration (`FindFirstFile`);
- the engine's resource lookups, `MessageBox` for a bad engine version, the INI's path from the
  module name, and the command line.

They will move as the other platforms need them.

## Tests

`compare_window` is new. The original DLL's window procedure (0x1006b820) and the port's (the
platform's "LS3D" window class) get the same messages for a hidden test window, which both sides
take as their main window. The same posted messages go through each side's `ProcessWinMessages`.
Everything must match:

- return values, the activation state and the application name;
- the titles each side gives the window and the quit messages;
- the typed characters (`ReadChar`) and the window-message keyboard (`GetKey`, `GetKeyChar`);
- the dialog list.

The test's windows live on a desktop of their own. The activation gives them the keyboard focus,
and on the user's desktop real typing would reach the queue; an early run caught exactly that.

Result: 100,000 operations identical on each of four seeds. Each run covers about 5,200
deactivations and reactivations, 19,000 messages swallowed, 1,400 quit messages, 33,000 typed
characters and 10,000 window-message keys. Four deliberate bugs are caught: two in the backend's
system-key filter and key mapping, one in its typed-character condition, and one in IGraph's
activation decision.

The rest of the suite runs through the platform's clock. `compare_isnd` and `compare_input` patch
`timeGetTime`, and the backend compiled into them calls it through their own import table. It
gives the same results as before.

Not covered: creating the windows (that needs IGraph::Init, and with it a Direct3D device), WM_CLOSE
while initialised (the process exits), the message loop waiting while inactive, the cursor, the
accessibility keys and the texture thread.

### The SDL3 implementation

`test_platform_sdl3` is new. The original has no SDL, so the reference is the Win32
implementation. Both create their main window on a desktop of the test's own. Each window then gets
the same Windows messages:
- focus;
- 16 keys, typed with the window-message keyboard on and off;
- a close;
- focus lost and back.

What IGraph is told must be the same, event for event. It also checks:
- the client origins and the title;
- that SDL's clock is at `timeGetTime`'s count and moves with it;
- the SDL3 input device, chosen with the SDL3 window:
  - all 132 keys of its table, each sent as Windows sends it (scan code, extended flag, virtual
    key), must read as their own DIK code and nothing else;
  - mouse motion, wheel and buttons;
  - key names;
  - focus rules: input lost with the focus, no acquiring without it, acquiring again once it
    is back.

The desktop has no foreground window, no real cursor over the window, and no real key state behind
posted messages. SDL reads all three while it pumps, so the test points SDL3.dll's imports of
`GetForegroundWindow`, `GetFocus`, `GetCursorPos`, `GetKeyState`, `GetAsyncKeyState` and
`TrackMouseEvent` at its own answers.

Result: 26 checks pass. 27 of 28 deliberate bugs are caught, in the event mapping, the clock, the
window, the key table and the input device. The one missed (typed control characters let through) can't
happen: SDL never puts control characters in its text events.

Not covered: fullscreen with Direct3D 8 on the SDL window, the exclusive mouse (SDL's relative mode
reads raw input, which posted messages cannot fake), the editors' menu and dialogs on the SDL
window, and minimising.
