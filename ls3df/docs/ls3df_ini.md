# LS3DF.ini — optional graphics improvements

The port can do a few things the original engine cannot. They are all **off by default**: without
an `LS3DF.ini` the DLL behaves exactly like the original (the differential tests run this way). To
turn them on, put an `LS3DF.ini` in the same folder as `LS3DF.dll`:

```ini
[Graphics]
AnisotropicFiltering=16
TrilinearFiltering=1
ImprovedMipmaps=1
ColorKeyMipmaps=1
TrueColorTextures=1
TrueColorLightmaps=1
NearPortalDistance=100
```

A missing key is off. The DLL reads the file once, at start-up. The `[Sound]`, `[Video]` and
`[Platform]` sections ([below](#sound)) choose the new cross-platform backends instead of
Windows' own APIs.

## Options

| Key | Values | What it does |
|---|---|---|
| `AnisotropicFiltering` | 0, 2–16 | Anisotropic texture filtering, for sharper ground and walls at shallow angles. Capped at what the graphics card supports and reapplied after a device reset. On `SDLGPU` and `OpenGL` it also blends mip levels, as Direct3D 12 requires. |
| `TrilinearFiltering` | 0 / 1 | Mip levels are always blended, even if the game asks for point or no mip filtering. |
| `ImprovedMipmaps` | 0 / 1 | A new mip-map filter for BMP / TGA textures (details below). |
| `ColorKeyMipmaps` | 0 / 1 | With `ImprovedMipmaps`: colour-keyed textures whose material has no mip maps, such as the palm leaves of Africa1, get them, which stops much of their shimmer at a distance (details below). |
| `TrueColorTextures` | 0 / 1 | Textures loaded from image files are 32-bit (A8R8G8B8) instead of 16-bit or DXT. DDS files keep their own format, and textures the engine fills itself (shadow maps, video, lightmaps) are left alone. |
| `TrueColorLightmaps` | 0 / 1 | Lightmaps are always 32-bit. |
| `NearPortalDistance` | 0–1000 (cm) | A camera standing just outside a room's opening (portal), looking away from it, still draws the room. Needed by gun cameras placed past an embrasure (details below). 100 is enough for the game's bunkers. |

**NearPortalDistance** fixes cameras that sit just outside a sector (room). The engine draws the
camera's own sector and enters a neighbouring sector only through a portal at least partly in
front of the camera. Anything of that sector that sticks out past the opening is therefore lost
when the camera is just past it and looking away:

- The TK vz. 37 bunker gun of the Czech6 missions (`la_m_Bunkr01`) has its first-person camera
  0.5–0.7 m outside the embrasure, in the outdoor sector. The gun belongs to the bunker sector, so
  neither the gun nor a sight added to the model (`VIEW00`, the C++ repo's
  `tools/build_bunker_sight.py`) was ever drawn.
- With the option, a portal of the camera's own sector whose corners are all behind the camera is
  still looked through when the camera is within the distance of its opening. The sector beyond
  is then entered with the whole view: its frames are culled by the view frustum as usual (the
  room itself is behind the camera), and the depth buffer keeps whatever the camera's own sector
  hides.
- The cost is a little extra culling work when a camera stands right in front of a door or window
  looking away. Around the Czech6 bunkers that happens for 0.7% of random camera positions.

**ImprovedMipmaps** replaces the original's filter, which weighs each 2 × 2 block unevenly, rounds
down at every step and averages gamma-encoded values:

- Brightness is kept. The original's mips of random-noise textures lose 24% of their light at the
  first level and 47% at 1 × 1; the new filter stays within half an 8-bit step.
- Colours are averaged in linear light (gamma-correct) and weighted by alpha, so colour-key
  edges no longer pick up the transparent colour (black fringes around foliage).
- A colour-keyed texture with mip levels is drawn blended, so its levels keep the averaged alpha, which is how
  opaque they are drawn. (Until November 2026 they were scaled to keep the coverage of an alpha test, and
  such textures, such as a flag or camouflage netting, faded as the camera went away.) Colour keys drawn
  alpha-tested get on/off levels with `ColorKeyMipmaps` (below).
- These textures get their own cache files (a `G` before the extension; `AG` for blended colour keys), so old
  cache files are never used for them.

**ColorKeyMipmaps** is for leaves, fences and the like whose material does not ask for mip maps. Each pixel of
such a texture far away samples a tiny, nearly random spot of it, leaf or gap, that changes as the camera moves.

- They keep their way of drawing, alpha-tested with depth written, where a colour key with mip maps would
  otherwise be blended, which draws in another order.
- Each level's alpha stays 0 or 255 with level 0's coverage. A mission sets its alpha reference after its textures
  load, and missions differ (32 and 128 are both seen), so the levels must not depend on it. Filtering between
  texels still gives `[Render] AlphaToCoverage` the smooth alpha it needs.
- Each such texture takes about a third more memory, and DXT1 ones become uncompressed, as any colour key with
  mip maps is. They get their own cache files (a `K` after the `G`).
- In the game (November 2026, Africa1 with a high-resolution texture pack, on Direct3D 12 with
  `AnisotropicFiltering=16`, `MSAA=4` and `AlphaToCoverage=1`): the palms no longer shimmer, keep their density near
  and far, and draw in the right order, with about 2.6 GB of address space free.

## Sound

```ini
[Sound]
OpenAL=1
```

| Key | Values | What it does |
|---|---|---|
| `OpenAL` | 0 / 1 | Sound plays through OpenAL Soft instead of DirectSound (the cross-platform plan's phase 2, [cross_platform.md](cross_platform.md)). Needs `soft_oal.dll` (built with the DLL) in the game folder; without it the DLL falls back to DirectSound. The game's EAX reverb works again, as on the sound cards of its time: OpenAL Soft emulates EAX, while DirectSound on today's Windows has none. |

How it works and what it covers: [sound_device.md](sound_device.md#the-openal-soft-backend).
`log.txt` says which one is in use.

## Video

```ini
[Video]
FFmpeg=1
```

| Key | Values | What it does |
|---|---|---|
| `FFmpeg` | 0 / 1 | Movies (Bink) and other videos are decoded by FFmpeg instead of Bink and DirectShow (the cross-platform plan's phase 2, [cross_platform.md](cross_platform.md)). Needs FFmpeg's five DLLs (`avutil-59.dll`, `avcodec-61.dll`, `avformat-61.dll`, `swscale-8.dll`, `swresample-5.dll`, built with the DLL) in the game folder; without them the DLL falls back to Bink and DirectShow. A movie's sound then plays through the game's sound device, so with `[Sound] OpenAL=1` too, nothing uses DirectSound. |

How it works and what it covers: [video_device.md](video_device.md#the-ffmpeg-backend). `log.txt`
says which one is in use.

## Platform

```ini
[Platform]
SDL3=1
```

| Key | Values | What it does |
|---|---|---|
| `SDL3` | 0 / 1 | The window, its messages, the keyboard and mouse, and the clock go through SDL3 instead of Windows' own functions and DirectInput (the cross-platform plan's phase 2, [cross_platform.md](cross_platform.md)). Needs `SDL3.dll` (built with the DLL) in the game folder; without it the DLL falls back to Windows and DirectInput. Drawing is still Direct3D 8, on SDL's window. |

How it works and what it covers: [platform.md](platform.md#the-sdl3-implementation) and
[input_device.md](input_device.md#the-sdl3-backend). `log.txt` says which one is in use.

## Render

```ini
[Render]
SDLGPU=1
SDLGPUDriver=vulkan
MSAA=4
AlphaToCoverage=1
AmbientOcclusion=1
CaptureFrames=1
```

| Key | Values | What it does |
|---|---|---|
| `SDLGPU` | 0 / 1 | Drawing through SDL_GPU (Direct3D 12 or Vulkan) instead of Direct3D 8 (the cross-platform plan's phase 3, [render_device.md](render_device.md#the-sdl_gpu-backend)). Needs `SDL3.dll` (3.2 or later) in the game folder; without it the DLL draws with Direct3D 8. Best with `[Platform] SDL3=1`. Fullscreen is SDL's, in the desktop's mode (no mode change), the picture scaled with its shape kept. |
| `OpenGL` | 0 / 1 | Drawing through OpenGL 3.3 instead of Direct3D 8, for GPUs and drivers without Direct3D 12 or Vulkan ([render_device.md](render_device.md#the-opengl-33-backend)). Needs `SDL3.dll` like `SDLGPU`, which wins when both are set. Best with `[Platform] SDL3=1`. |
| `SDLGPUDriver` | `direct3d12` / `vulkan` | The API SDL_GPU draws with; empty or missing: SDL's choice. |
| `PresentMode` | `vsync` / `mailbox` / `immediate` | How `SDLGPU` and `OpenGL` show frames: one per refresh with the game waiting for it; the newest whole frame at each refresh, without waiting (on OpenGL, the same as `immediate`); or at once, tearing if need be. Empty or missing: what the game asks for. |
| `MSAA` | 0, 2, 4, 8 | Antialiasing for `SDLGPU` and `OpenGL`: that many samples a pixel, or as many as the GPU can do. 0 or missing: what the game's setup asks for. 4 is a good choice: on the game's captures 8 is hardly different. Direct3D 8 ignores it (there, the setup or a wrapper such as dgVoodoo decides). |
| `AlphaToCoverage` | 0 / 1 | With `MSAA`: the edges of 3D alpha-tested textures (leaves, fences, wire) are smoothed too, which MSAA alone leaves stepped. 2D drawing is left as the game drew it. |
| `AmbientOcclusion` | 0 / 1 | For `SDLGPU` and `OpenGL`: soft darkening where the opaque 3D scene's surfaces meet, such as the folds of a uniform, under a hat's brim, or where a wall meets the ground. Leaves and the like, what is blended, the weapon in the hand and the HUD are left as they were. Direct3D 8 ignores it. |
| `AmbientOcclusionStrength` | percent | How dark it gets. Missing: 75. |
| `AmbientOcclusionRadius` | centimetres | How far it reaches. Missing: 60. |
| `SDLGPUStats` | 0 / 1 | Every 5 seconds, what the frames cost (time apart, building, waiting, draws, uploads) in `LS3DF_render.log` next to the DLL. Also for `OpenGL`. |
| `SDLGPUDebug` | 0 / 1 | The API's validation (Direct3D 12's debug layer, the Vulkan SDK's layers, OpenGL's debug output), and what is asked of it in `LS3DF_render.log`; slow, for finding faults. Also for `OpenGL`. |
| `CaptureFrames` | 0 / 1 | **Ctrl+Shift+F11** saves the next frame the game draws to `LS3DF_frames\frame_<n>.ls3dframe` next to the DLL: every drawing call, the state and resources it uses, and the picture the game drew. The render tests replay these files on each backend and compare the pictures (the cross-platform plan's phase 3, [render_device.md](render_device.md#frame-captures)). A file can be large (tens of MB), and keeping copies of the vertex buffers costs some memory while the option is on. |

## Diagnostics

```ini
[Diagnostics]
Events=1
EventBuffer=65536
```

| Key | Values | What it does |
|---|---|---|
| `Events` | 0 / 1 | Timestamped markers of each frame's stages, in two files: the engine's in `LS3DF_events.csv` next to the DLL (flips, presents, the fixed renderer's runs and read-backs, texture uploads, SDL_GPU's swapchain acquire, submit, upload waits and pipeline creation, OpenGL's swap, and with OpenGL the GPU's own time for each frame and how far behind the CPU it finished, from timer queries that never wait), written when the engine closes; and `HD2_Port.exe`'s main loop in `HD2_events.csv` next to the EXE (input, the frame floor, the engine tick, drawing, network, the server's and the client's updates, the flip), written when the loop ends. Both stamp `QueryPerformanceCounter`, so the two files line up by time. Only the main thread's markers are recorded. Off: nothing is recorded and nothing is written. |
| `EventBuffer` | events | How many of the latest events each file keeps (24 bytes each; 1,024 - 4,194,304, rounded up to a power of two). Missing: 65,536, about 20 seconds of play at 100 frames a second. |
| `Hitches` | 0 / 1 | With `Events`: the hitch recorder. When frames are `HitchMs` apart, or a stage of a frame takes that long, or a wait takes `HitchWaitMs`, it keeps the events around that moment and writes them as one line to `LS3DF_hitches.jsonl` (the engine) and `HD2_hitches.jsonl` (the game loop): the trigger, the slow frame's stages by the time each took on its own, the largest of them as the cause, and the events. The files are emptied when the game starts. Missing: 1. |
| `HitchMs` | ms | Frames this far apart, or a stage (drawing, update, flip, present …) this long, is a hitch. Missing: 50. |
| `HitchWaitMs` | ms | A wait or creation this long is a hitch: texture uploads, SDL_GPU's upload wait, pipeline creation, swapchain acquire and submit, OpenGL's swap. Missing: 20 (above a 60 Hz refresh, so vsync alone does not count). |
| `HitchBeforeMs`, `HitchAfterMs` | ms | Events kept before a hitch (and always from the slow frame's start) and after it. Missing: 2,000 and 500. |
| `HitchFiles` | count | Hitches written at most in a session; the rest are counted in the file's last line. Missing: 50. |
| `HitchCapture` | count | With `[Render] CaptureFrames=1`: that many of the engine's hitches also save the next frame to `LS3DF_frames\`, for the render tests. That frame's own slowness is not recorded as a hitch. Missing: 0. |

The schema is `src/diag/events.h`. A mark costs about 35 ns with the recorder on (most of it the clock); off, one test.
The recorder writes nothing on the game's thread: a hitch's events are copied into one of four buffers made at the
start, and a thread of its own writes them. With all four still being written, a hitch is counted, not written.

## Things to know

- **Trilinear filtering and 32-bit lightmaps are already the engine's defaults.** Those two
  options only matter if the game turns them down. If it does, a line in `log.txt` says so.
- **`log.txt` lists what is in effect** under "Port enhancements (LS3DF.ini)", including the
  anisotropy the card actually allows. These lines are written even when the game's log is off.
- **`TrueColorTextures` uses much more memory**: about 8× the texture memory of the default
  compressed setting (all of the game's textures: 1.9 GB at 32-bit against 233 MB as DXT1, of which
  a mission loads a part). The game is a 32-bit process, so if a large map runs out of memory, turn
  this option off first.
- Very fine alpha detail, such as grass blades one pixel wide and a few pixels apart, still
  disappears at a distance; no per-level filter can keep it. It no longer turns into a solid wall.
- **Tried in the game on `SDLGPU` only.** `AnisotropicFiltering` and `ImprovedMipmaps` ran on Direct3D 12
  through SDL_GPU. The texture and loading paths are verified (below); the filter settings on a real
  Direct3D 8 device are not.

## The changes

- New: [`src/core/enhance.h`](../src/core/enhance.h) / `enhance.cpp` (reads the INI; `Enhancements()`,
  `SetEnhancements()` for tests, `EnhancementLog()`), and
  [`src/igraph/mip_filter.h`](../src/igraph/mip_filter.h) / `mip_filter.cpp` (`DownsampleMip`,
  `ScaleAlphaToCoverage`, `AlphaCoverage`).
- `igraph/itexture.cpp`: the 32-bit override, the new mip chain (each level made from the unscaled
  level above; only the uploaded copy of a colour-keyed level has its alpha rescaled), the `G` cache
  file tag.
- `igraph/igraph.cpp`, `igraph_state.h`: the anisotropy limit from the device caps (`g_anisotropy`,
  `LinearMinFilter()`), the log block.
- `i3d/driver_core.cpp`: `SetState(0)` uses the anisotropic minification filter and sets the
  maximum anisotropy on stages 0–2, `SetState(2)` the trilinear floor, `LightmapsTrueColor()`
  (also used by `lightmap.cpp` and `litobject.cpp`); `driver_draw.cpp` restores the filter after
  drawing text.
- `i3d/portal_render.cpp`: `NearPortal()` (the camera's distance to a portal's polygon) and the
  look-through in `ParsePortal` for a camera-sector portal that is all behind the camera.
- Every change is marked `(Port enhancement, LS3DF.ini <key>)` in the source.

## Tests

- `test_enhance`: the INI reader and the filter's properties (uniform colours unchanged, the
  gamma-correct average, no colour from transparent texels, brightness across a chain, coverage on
  several shapes, every size down to 1 × 1 without writing past the level), with the original's
  filter printed alongside.
- `compare_texture [original LS3DF.dll] [rounds] enhanced | mipmaps`: the original is asked for
  `ITC_TRUECOLOR` where the option forces it; level 0 must match, and every further level must be
  exactly the new filter's chain (16-bit levels too, through `WriteSurface`); also the port's own
  cache files and their names. 8,000 cases each, and three deliberately introduced bugs are caught.
  Without a mode, it still matches the original on all 8,000 cases.
- `compare_loader` without an INI: all 6,214 game files identical. With every option on (INI next
  to `%TEMP%\ls3df_loader\LS3DF_port.dll`), run it in chunks
  (`compare_loader.exe "" "" <first> <count> "" all`): in one process the test's fake devices keep
  every texture, and at 32 bits they fill the 4 GB address space. All files load; the differences
  are texture formats and levels, plus texture ids one lower from `d_lamp_05` on. That is because
  `2DE_RAC1.TGA` is asked for with and without `ITC_TRUECOLOR`, and with the option both requests
  are identical, so the port shares one texture.
- `test_near_portal <la_m_Bunkr01.4ds with VIEW00>`: loads the Czech6 scenes on a fake device,
  places the bunker gun where each of the five bunker frames of CZECH6 and CZECH6_MP_ZONE puts
  it, turns it over its range (15 poses each) and runs the sector pass from its camera. With the
  option off (the original's behaviour) the sight is drawn in 0 of 75 poses; at 1 m it is drawn in
  all 75. 20,000 random cameras around the bunkers check that the option only ever adds sectors;
  two deliberately introduced bugs are caught.
