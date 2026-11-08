# LS3DF.ini — optional graphics improvements

The port can do a few things the original engine cannot. They are all **off by default**: without
an `LS3DF.ini` the DLL behaves exactly like the original (the differential tests run this way). To
turn them on, put an `LS3DF.ini` in the same folder as `LS3DF.dll`:

```ini
[Graphics]
AnisotropicFiltering=16
TrilinearFiltering=1
ImprovedMipmaps=1
TrueColorTextures=1
TrueColorLightmaps=1
NearPortalDistance=100
```

A missing key is off. The DLL reads the file once, at start-up.

## Options

| Key | Values | What it does |
|---|---|---|
| `AnisotropicFiltering` | 0, 2–16 | Anisotropic texture filtering, for sharper ground and walls at shallow angles. Capped at what the graphics card supports and reapplied after a device reset. |
| `TrilinearFiltering` | 0 / 1 | Mip levels are always blended, even if the game asks for point or no mip filtering. |
| `ImprovedMipmaps` | 0 / 1 | A new mip-map filter for BMP / TGA textures (details below). |
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
- Colour-keyed (alpha-tested) textures keep their coverage at each level. With the original's filter
  and the engine's alpha reference (0x20), foliage fattens with distance: 30% leaf coverage became
  100%.
- These textures get their own cache files (a `G` before the extension), so old cache files are
  never used for them.

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
- **Not tested in the game yet.** The texture and loading paths are verified (below); the filter
  settings on a real Direct3D device are not.

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
