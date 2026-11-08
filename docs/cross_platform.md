# Going cross-platform

The long-term goal: LS3DF (and with it the game) building and running on operating systems other
than Windows. Direct3D 8, DirectSound, DirectInput, DirectShow, Bink and Win32 give way to
portable libraries:

| Today | Replacement |
|---|---|
| Win32 window, messages, clock, threads | SDL3 |
| DirectInput 8 | SDL3 |
| DirectSound 8 + EAX 3 | OpenAL Soft (EAX reverb through EFX) |
| Bink 1, DirectShow | FFmpeg |
| Direct3D 8 | SDL_GPU (see below), and OpenGL 3.3 beside it |

SDL_GPU draws through each system's own graphics API:

| System | SDL_GPU's API |
|---|---|
| Windows | Direct3D 12 or Vulkan |
| Linux | Vulkan |
| macOS | Metal |

OpenGL 3.3 (core profile, its context made by SDL3) is a second render backend beside SDL_GPU, on
the same three systems, for GPUs and drivers that have none of those three APIs.

The 32-bit Windows build on Direct3D 8 stays as the reference: the differential tests compare it
with the original DLL, and each new backend is checked against it before the old one goes.

## The phases

| Phase | What | Status |
|---|---|---|
| 1 | Seams: every API behind an engine-owned interface, still on Direct3D 8 | **done** |
| 2 | SDL3 (window, input, time), OpenAL Soft (sound) and FFmpeg (video) backends, still rendering with Direct3D 8 | **done**: OpenAL Soft, FFmpeg and SDL3 each work in the game, chosen in LS3DF.ini |
| 3 | SDL_GPU and OpenGL 3.3 render backends on Windows, their output compared pixel by pixel with the Direct3D 8 backend's | **in progress**: frame captures and their replay done; SDL_GPU works in the game on Direct3D 12 and Vulkan (looks right, smooth); OpenGL 3.3 next |
| 4 | A 64-bit build, together with the game's port | |
| 5 | Linux, then macOS | |

### Phase 1: the seams (done)

Each seam keeps the original API's vocabulary, holds opaque handles where the API pointers were
(the game reads the engine's structures, so layouts stay), puts the API calls in one Windows
backend, and is enforced by `python build.py`. The DLL makes the same API calls as before; the
differential tests showed no change.

| Seam | Interface | Windows backend | Commit | Details |
|---|---|---|---|---|
| Rendering | `gfx::Device` | `src/gfx/d3d8` | d2c3b93 | [render_device.md](render_device.md) |
| Sound | `audio::Device` | `src/audio/dsound` | 80c81d1 | [sound_device.md](sound_device.md) |
| Input | `input::Device` | `src/input/dinput` | aefd06e | [input_device.md](input_device.md) |
| Window, messages, time, threads, events | `platform::` functions | `src/platform/win32` | 47159f2 | [platform.md](platform.md) |
| Video | `video::Device` | `src/video/win32` | 0a59157 | [video_device.md](video_device.md) |

New differential tests came with the seams: `compare_input`, `compare_window` and
`compare_show`. They found two bugs of the port, both fixed: IGraph's held-key repeat started at the
wrong value, and the port marked the end of an Ogg stream too early; see [porting_notes.md](porting_notes.md).

The built DLL then ran in the game for the first time (October 2026): menus, two cooperative
multiplayer missions, music, the intro and credits movies.

Left on Win32 for now, small and only needed for another platform
([platform.md](platform.md#not-covered-yet)): the DirectX 8.1 check, the OS name in the log, COM
start-up, `RECT` helpers, the texture directories' enumeration, resource lookups and `MessageBox`,
the INI path from the module name, the command line.

Known escape hatch: Bink plays its own sound through DirectSound, handed the sound device's object
(`audio::Device::Native`); an FFmpeg backend plays its sound through the sound device instead.

### Phase 2: SDL3, OpenAL Soft, FFmpeg (done)

Each new backend sits beside the Windows one and is chosen at start-up in LS3DF.ini, so the two can
be compared in the same tests:

| Part | Status |
|---|---|
| **Sound on OpenAL Soft** (`src/audio/openal`) | Written and tested on OpenAL Soft's loopback device (`test_audio_openal`); chosen with `[Sound] OpenAL=1` in LS3DF.ini; tried in the game, where it works (November 2026). Details in [sound_device.md](sound_device.md#the-openal-soft-backend). |
| **Platform and input on SDL3** (`src/platform/sdl3`, `src/input/sdl3`) | Written and tested against the Win32 implementation (`test_platform_sdl3`); chosen with `[Platform] SDL3=1` in LS3DF.ini; tried in the game, where it works (November 2026: menus, fullscreen with Direct3D 8, Alt-Tab, mouse look, the controls menu's key names). Details in [platform.md](platform.md#the-sdl3-implementation) and [input_device.md](input_device.md#the-sdl3-backend). |
| **Video on FFmpeg** (`src/video/ffmpeg`) | Written and tested against Bink and DirectShow (`test_video_ffmpeg`); chosen with `[Video] FFmpeg=1` in LS3DF.ini; tried in the game, where it works (November 2026: the intro, a briefing, the credits, skipping a movie). Details in [video_device.md](video_device.md#the-ffmpeg-backend). |

- **Sound on OpenAL Soft:** a buffer is an OpenAL source reading the buffer's bytes through a
  callback, which keeps DirectSound's ring-buffer model (locks while playing, the play cursor,
  position notifications). Pan and volume follow DirectSound's rules exactly. 3D uses
  DirectSound's distance model with the axes flipped. EAX 3 goes to OpenAL Soft's own EAX
  emulation, which answers the open question about EFX: OpenAL Soft maps EAX 3 to its reverb
  itself. The library (OpenAL Soft 1.24.3, built 32-bit with the static C runtime) is loaded at run
  time, so the DLL still works without it.
- **Platform and input on SDL3:** the main window is SDL's, and Direct3D 8 draws on its native
  handle. SDL's events drive the decisions IGraph makes on the window's messages
  (`platform::WindowEvents`), and the clock is SDL's, counted on from `timeGetTime`. The keyboard
  and mouse are SDL's state, with DirectInput's key codes (DIK) kept as the engine's vocabulary.
  The window functions became a `platform::Windowing` object chosen at start-up rather than at
  build time. Left on Win32 for now: threads, locks and events (DirectSound sets the events
  itself, so they have to be Windows events while it can be in use), and the editors' child
  windows, menu and dialogs, which SDL has no notion of. The library (SDL 3.4.20, built 32-bit
  with the static C runtime) is loaded at run time, so the DLL still works without it.
- **Video on FFmpeg:** Bink movies stepped frame by frame (the `video::Device` movie model), their
  sound through the sound device, so the escape hatch above is gone with this backend; the
  DirectShow graphs (AVI and others) on FFmpeg too, with a thread handing frames to IShow's tiles.
  This answers the open question about Bink: FFmpeg decodes all 68 of the game's movies (101,158
  frames) to within 49.7 dB of binkw32's pictures on average, none under 47.7 dB (most pixels within
  2 of 255; a few movies have single blocks further off). The
  libraries (FFmpeg 7.1.1, a 32-bit LGPL build of just the Bink, AVI and WAV parts) are loaded at
  run time, so the DLL still works without them.

### Phase 3: SDL_GPU and OpenGL 3.3 (in progress)

Two render backends behind `gfx::Device`, each turning the fixed-function Direct3D 8 states into
shaders, their frames compared pixel by pixel with the Direct3D 8 backend's.

The frames come from the game: with `[Render] CaptureFrames=1` in LS3DF.ini, Ctrl+Shift+F11 saves
one frame's drawing calls, state, resources and picture. `test_render_replay` replays a capture on
a backend and compares its picture with the game's ([render_device.md](render_device.md#frame-captures)).
On Direct3D 8 the replay matches the game's picture exactly, so a capture holds everything its
frame needs.

The backends:

- **SDL_GPU:** Direct3D 12 or Vulkan on Windows, Vulkan on Linux, Metal on macOS. On Windows both
  of its APIs can be compared with Direct3D 8.
- **OpenGL 3.3** (core profile), its context made by SDL3: for GPUs and drivers without Direct3D 12,
  Vulkan or Metal.

### Phase 4: 64 bits

The engine and the game's port built as 64-bit. The x87 bit-exactness the tests rely on ends here,
which is why the 32-bit build stays as the reference.

### Phase 5: other systems

Linux, then macOS: SDL_GPU on Vulkan (Linux) and Metal (macOS), OpenGL 3.3 on both.
