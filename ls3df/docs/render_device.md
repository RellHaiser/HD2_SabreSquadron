# The render device (gfx)

All of the engine's drawing goes through one interface, `gfx::Device` (`gfx::g_pGfx`), declared
in [`src/gfx/gfx.h`](../src/gfx/gfx.h). It has three backends:
- **Direct3D 8** ([`src/gfx/d3d8`](../src/gfx/d3d8)), the default, which passes every call to
  Direct3D 8 unchanged. The DLL therefore makes exactly the Direct3D calls the original made, and
  the differential tests compare equal.
- **SDL_GPU** ([`src/gfx/sdlgpu`](../src/gfx/sdlgpu)), chosen with `[Render] SDLGPU=1` in
  LS3DF.ini: Direct3D 8's fixed-function model drawn by shaders, on Direct3D 12 or Vulkan
  ([below](#the-sdl_gpu-backend)).
- **OpenGL 3.3** ([`src/gfx/opengl`](../src/gfx/opengl)), chosen with `[Render] OpenGL=1`: the same
  model on GPUs and drivers without those APIs ([below](#the-opengl-33-backend)).

The last two share [`src/gfx/fixed`](../src/gfx/fixed): the device itself (states, resources in
memory, frames recorded), with a small `Renderer` per API that uploads textures, runs a recorded
frame, presents it and reads it back.

This is the first step towards other backends (a modern graphics API, other operating systems).
It moves no pixels and changes no behaviour; it only makes Direct3D replaceable. Sound has the same
kind of seam: [the sound device](sound_device.md).

## Design

**The vocabulary stays Direct3D 8's.** The renderer describes what it draws in Direct3D 8's
fixed-function terms: render states, texture stage combiners, lights, materials, fog, transforms,
vertex formats. The game's look is defined by those settings, so any backend has to reproduce
exactly that model, whatever API it uses. The types, enumerations and constants are in
[`src/dx/d3d8_types.h`](../src/dx/d3d8_types.h). The COM interfaces are in `src/dx/d3d8.h`, which
only the backend includes.

**Resources are opaque handles.** Textures, surfaces, vertex buffers and index buffers are
`gfx::Texture *`, `gfx::Surface *`, `gfx::VertexBuffer *` and `gfx::IndexBuffer *`. These are
incomplete types that only a backend looks inside, and operations on them go through the device
(`g_pGfx->Lock(vb, ...)`, `g_pGfx->Release(texture)`). The game reads some engine structures
directly, so the handles sit exactly where the Direct3D pointers were and have the same size:

| Structure | Member | Before |
|---|---|---|
| `ITexture` | `deviceTexture` (+0x08), `lockedSurface` (+0x0C) | `d3dTexture` (`IDirect3DTexture8 *`), `IDirect3DSurface8 *` |
| `S_IBAlloc`, `C_IndexBuffer` | `ib` | `IDirect3DIndexBuffer8 *` |
| `S_VBAlloc`, `C_VertexBuffer` | `vb` | `IDirect3DVertexBuffer8 *` |

The Direct3D 8 backend's handles are the Direct3D objects themselves
(`gfx::d3d8::D3D()` / `Handle()` convert them), so these members hold the same values as before.

**The device owns the API and the device's life.** IGraph still makes every decision the original
made (display modes, back buffer and depth formats, texture formats, vendor quirks, multisampling,
lost devices). It asks the device for the facts in Direct3D 8's terms (`GetDeviceCaps`,
`CheckDeviceFormat`, ...) and creates, resets and presents the device through it. The `IDirect3D8`
and `IDirect3DDevice8` pointers belong to the backend. They keep the names `ls3d::ig::g_pD3D` and
`g_pDevice`, which the tests use to put their fake devices in.

**The boundary is checked.** `python build.py` (and `check`) fails when a file outside `src/dx`
and `src/gfx/d3d8` includes the Direct3D interfaces or the backend, names an `IDirect3D*8`
interface, or uses `g_pD3D` / `g_pDevice`.

## What a new backend has to provide

Everything in `gfx::Device`: the adapter queries (answered as a Direct3D 8 device that can do what
the backend emulates), the device's life, and the drawing calls. The renderer names these
fixed-function features (the game can also pass values of its own through `IGraph::SetState`):

- Render states: ALPHABLENDENABLE, ALPHAFUNC, ALPHAREF, ALPHATESTENABLE, AMBIENT, CLIPPING,
  COLORVERTEX, COLORWRITEENABLE, CULLMODE, DESTBLEND, DITHERENABLE, FILLMODE, FOGCOLOR, FOGDENSITY,
  FOGENABLE, FOGEND, FOGSTART, FOGTABLEMODE, FOGVERTEXMODE, LIGHTING, LOCALVIEWER,
  MULTISAMPLEANTIALIAS, NORMALIZENORMALS, SHADEMODE, SOFTWAREVERTEXPROCESSING, SPECULARENABLE,
  SRCBLEND, STENCILENABLE, TEXTUREFACTOR, ZBIAS, ZENABLE (including the w-buffer), ZFUNC,
  ZWRITEENABLE.
- Texture stages 0 to 2: ADDRESSU/V, COLOROP / COLORARG1 / COLORARG2, ALPHAOP / ALPHAARG1 /
  ALPHAARG2, BUMPENVMAT00 to 11, MIN / MAG / MIPFILTER, MAXANISOTROPY, MIPMAPLODBIAS,
  TEXCOORDINDEX (including camera-space reflection vectors), TEXTURETRANSFORMFLAGS.
- Combiner operations: DISABLE, SELECTARG1, MODULATE, MODULATE2X, ADD, BLENDTEXTUREALPHA,
  BLENDFACTORALPHA, BLENDCURRENTALPHA, BUMPENVMAP; arguments DIFFUSE, CURRENT, TEXTURE.
- Blend factors: ZERO, ONE, SRCALPHA, INVSRCALPHA, SRCCOLOR, DESTCOLOR.
- Up to 8 per-vertex lights (point, spot, directional) with materials; linear, exponential and
  squared exponential fog; world, view, projection and texture transforms.
- Vertex formats: four declarations (position + normal or colour, one or two texture coordinate
  sets), pre-transformed vertices (XYZRHW with diffuse, specular and one or two texture
  coordinate sets), and one `vs.1.0` shader (the sky's far-plane transform). (`SetVertexFormat`
  also maps formats 4 and 5 to the FVF codes 0x40 and 0x140, which have no position, as the
  original does; they are reachable only through a buffer's stored format.)
- Point, line and triangle lists, strips and fans, indexed (16-bit) and not, from buffers and from
  user memory.

## Tests

- Tests that compile engine sources calling the device also compile the backend
  (`// test-sources: src/gfx/d3d8/gfx_d3d8.cpp`) and put their fake device into `ig::g_pDevice`
  as before. Tests that reach into the port's objects include `gfx/d3d8/d3d8_device.h` to turn
  handles back into their fakes.
- `compare_loader` recognises the handle types by name (`ls3d::gfx::VertexBuffer` and so on) as
  it recognised the Direct3D ones.
- Run before and after the change on the same machine, every test gave the same result:
  `compare_math`, `compare_dxerr8`, `compare_resize`, `compare_s3tc`, `compare_texture` (plain and
  `enhanced`), `compare_isnd`, `compare_buffers`, `compare_mesh`, `compare_material`,
  `compare_frame`, `compare_collision` and `compare_lightmap` identical, `compare_loader` 6,214
  game files with 0 mismatched, `test_enhance` passed.

## The SDL_GPU backend

Phase 3 of the [cross-platform plan](cross_platform.md): the engine draws through SDL_GPU, which
uses Direct3D 12 or Vulkan on Windows (Vulkan on Linux, Metal on macOS). Chosen with
`[Render] SDLGPU=1` ([ls3df_ini.md](ls3df_ini.md#render)); without SDL3.dll (3.2 or later) the DLL
draws with Direct3D 8. `SDLGPUDriver=direct3d12` or `vulkan` picks the API (otherwise SDL's
choice), and `SDLGPUDebug=1` turns on the API's validation. The window is the SDL3 platform's
([platform.md](platform.md#the-sdl3-implementation)), or the engine's own window wrapped by SDL.

**The fixed-function pipeline as shaders.** One vertex shader and one pixel shader
([`shaders/fixed_function.hlsl`](../src/gfx/sdlgpu/shaders/fixed_function.hlsl)), driven by uniforms
the device fills from its states, follow Direct3D's documented rules:
- the transforms, and the pre-transformed vertices' screen positions;
- Direct3D 9's pixel centres, half a pixel from SDL_GPU's;
- the lights in camera space, with attenuation, spot falloff and specular, and the colour sources
  (COLORVERTEX and the material sources);
- vertex fog, linear and exponential (and the specular alpha as a pre-transformed vertex's fog);
- texture coordinate generation and texture transforms;
- three texture stages with their operations, arguments and modifiers, each result clamped;
- BUMPENVMAP, the specular sum, the alpha test on 8-bit alpha and the fog blend.

A stage that reads a texture it does not have is off, as Direct3D's drivers treat it; the frame
captures showed that. Every stage's texture is sampled outside any branch: Vulkan's drivers give a
sample in a branch no reliable mip level. The engine's one vertex shader (the sky's: `dp4` into
`oPos`, `mov oD0`) is read and run as a mode of the same shader. `build.py` compiles the shaders
with `dxc`: DXIL for Direct3D 12 with the Windows SDK's, SPIR-V for Vulkan with the Vulkan SDK's.

**Pipelines.** What an SDL_GPU pipeline fixes makes the pipeline, cached by those settings:
- blending, the colour write mask;
- the depth test, write and function, ZBIAS;
- culling and the fill mode;
- the vertex format and the primitive type.

**ZBIAS** n is drawn as Direct3D 8's runtime draws it, measured on Windows' `d3d8.dll` and
dgVoodoo's (they agree): nearer by 2n depth steps and by (n + 2) / 8 of the triangle's depth slope.
The slope's part keeps a decal on sloping ground in front of it; with only the steps, the snow's
bootprints (ZBIAS 8) were lost. OpenGL keeps n of its own steps, which on its driver already
measure 2n of 2^-24 or more. `test_render_divergence --zbias-probe --backend B` measures a backend.

The missing vertex inputs come from a buffer of Direct3D's defaults (white diffuse). Samplers are
cached by their filters, addressing, mip bias and anisotropy.

**Resources in memory, frames recorded.** A texture keeps its levels in its own format and a buffer
its bytes, as Direct3D's managed resources do: the engine locks and writes them at any time.
- A texture goes to the GPU when a frame first draws with it after a change. The formats with no
  GPU equal (16-bit, luminance, 24-bit) go as 32-bit BGRA, DXT as BC, V8U8 as RG8 signed.
- A frame is recorded and run when its picture is needed (Present, a read-back). Each draw keeps a
  copy of the vertices and indices it uses, taken at the draw, so a buffer may be rewritten mid-frame
  as Direct3D allows.
- At run time, one copy pass uploads the frame's vertices and indices, and one render pass draws it.
  Fans become lists, and clears are quads over the viewport.
- The back buffer is a texture of the device's size; Present scales it into the window.

Direct3D 8's rules kept: a draw from user memory unsets stream 0 (and the indices); Clear without
rectangles clears the viewport; LightEnable on a light never set makes Direct3D's default one.

**In the game** (November 2026): menus and the Africa1 mission, driven across its whole map, on
Direct3D 12 and on Vulkan: everything looks right, and it runs smoothly, about 95 frames a second,
the backend taking 1.5 - 2.7 ms of CPU for a frame of up to 1,700 draws. The first tries crashed or
stuttered, and taught three things:
- **Uploads must not each take memory.** Every texture upload had a transfer buffer of its own,
  mapped into the process until the GPU was done. A mission's hundreds of textures left the 32-bit
  process without address space, and the driver failed (in AMD's shader compiler, in Direct3D 12's
  debug layer). Textures now go through one 16 MB staging buffer in batches, and each frame's
  vertices through one transfer buffer.
- **Uploads must not wait.** The engine's texture thread loads textures all the time. Each upload
  batch used to wait for the GPU to finish all it had been given, which made the game stutter. Now
  only a fifth batch in flight waits (each holds its 16 MB until the GPU is done).
- **SDL 3.4's Direct3D 12 backend writes past its descriptor heap** when a draw's descriptors
  straddle the heap's end: it checks only that the heap is not full yet. With 3 samplers a draw
  that happens; the backend binds 4 (2048 and 65536 are multiples of 4) and re-binds them only when
  they change.

`SDLGPUStats=1` writes what the frames cost every 5 seconds to `LS3DF_render.log` next to the
DLL: the time between frames, building one, waiting for the swapchain and for uploads, the draws,
vertices and uploads. `SDLGPUDebug=1` also writes there: each pipeline and texture made, the
address space left, each line flushed before the call (a crash leaves the culprit last). Vulkan's
validation layer needs a 32-bit build of the layers, which the Vulkan SDK no longer ships: without
one the device starts without validation.

**Full screen** is SDL's, in the desktop's mode: the display mode does not change (both this backend
and OpenGL). SDL's borderless window moved over the display will not do: it keeps a caption in its
style, and on Windows frames then reach the display unevenly (with vsync, one every 33, 3 and 13 ms on
OpenGL, 40 a second on Direct3D 12). The picture is scaled to fit with its shape kept, black bars filling the
rest, so a 4:3 mode is not stretched on a wide display. Losing the focus to another program minimizes it, as
Direct3D's exclusive mode does.

**The gamma ramp** the game sets (its gamma option) changes what is shown, as Direct3D's does: Present
draws the back buffer through the ramp as a table ([`present.hlsl`](../src/gfx/sdlgpu/shaders/present.hlsl))
instead of blitting it. So read-backs and screenshots never see it, nor does the desktop, and it goes with
the window. It applies in full screen only, as Direct3D's, and an identity ramp takes the plain blit. Both this
backend and OpenGL.

**Multisampling** (`[Render] MSAA`, or the game's setup: both backends). A frame draws into a multisampled
colour and depth target, kept from pass to pass since a read-back runs a frame in parts, and each pass
resolves into the back buffer, so what comes after (Present, read-backs) is unchanged. With
`AlphaToCoverage` an alpha-tested 3D draw that is not blended turns its alpha into how much of the pixel
passes the test, an edge one pixel wide at any distance, and the samples cover that much. What is wholly on
the failing side is still discarded, so if a driver ignored the setting the test would only pass a little more.
The game's captures (made through dgVoodoo with 8 samples) are nearer with 4 samples than without: 31.3 - 41.6
dB against 31.0 - 38.4 on the 3D frames, and 95 dB against 45 on the two that are nearly all 2D.

**Ambient occlusion** (`[Render] AmbientOcclusion`: both backends;
[`occlusion.hlsl`](../src/gfx/sdlgpu/shaders/occlusion.hlsl)). A frame's order is the engine's: the sky, the opaque
world, what is alpha-tested, what is blended, a depth clear, the weapon in the hand with its own projection, another
depth clear, the HUD. The device marks the frame's scene as it records it: from the first perspective opaque draw up
to the first alpha-tested or blended draw or depth clear, once in a frame. A multisampled depth buffer cannot be
read, so the scene's depth-writing draws run first into a plain depth texture, whatever the sampling. After the
scene, the render pass ends for the occlusion's passes: the occlusion from that depth, a blur each way that keeps to
depth edges, and a multiply into the picture. The frame then goes on. What is alpha-tested stays out on purpose:
trees are flat cards crossing each other, and occlusion draws dark bands where they cross. It fades with the fog,
and with no scene (menus, movies) nothing of this runs. Samples that fall off the picture are left out and the
others stand for them (reading the edge's depth there made a wall that is not, flickering at the picture's sides in
a hallway); the radius on the picture is not narrowed near the camera (it made the occlusion grow and shrink as the
camera moved); the blur follows a slanted surface's slope (stairs seen at a slant kept their noise).

In the game (November 2026, Africa1, Direct3D 12, Vulkan and OpenGL; 4x MSAA): smooth, no crashes, no halos; the
hallways no longer flicker; a strength of 75 looks right (the default since). Still left: on the main building's
stairs and their ramp, and on a jeep's bonnet near the first-person camera, the occlusion changes with small moves of
the camera, the way screen-space occlusion knows only what the picture shows. Trees need none: their textures
darken at the foot and the lightmaps shade under them. Replays cost 0.2 - 1.1 ms a frame at 1600x1200 (RX 5700 XT).

**In the game** (November 2026, Africa1 with a high-resolution texture pack): the gamma option works in full
screen and the desktop is untouched after Alt-Tab; 4 samples with alpha to coverage stay smooth on both backends
and leave the HUD as it was, with about 2.6 GB of address space free on Direct3D 12. They do not stop the palm
leaves shimmering, as those have no mip levels: `ColorKeyMipmaps=1` does ([ls3df_ini.md](ls3df_ini.md#options)).
`AnisotropicFiltering=16` sharpens ground and walls at shallow angles.

**Not yet:**
- buffers kept on the GPU (each draw's vertices are copied every frame; at about 2 ms a frame it
  has not mattered);
- display mode changes;
- the stencil buffer (the engine does not use it);
- the w-buffer (drawn as the z-buffer).

## The OpenGL 3.3 backend

For GPUs and drivers without Direct3D 12, Vulkan or Metal. Chosen with `[Render] OpenGL=1`
([ls3df_ini.md](ls3df_ini.md#render)); SDLGPU wins when both are set. SDL3 makes the core-profile
context and finds the functions at run time, so nothing is linked. Everything but the `Renderer`
is the SDL_GPU backend's.

**One shader source.** The driver compiles the same
[`fixed_function.hlsl`](../src/gfx/sdlgpu/shaders/fixed_function.hlsl) as GLSL 3.30: a few macros
cover the differences, and the source keeps to what both languages take. `build.py` embeds the
text, so building needs no OpenGL tools.

**Direct3D's conventions.** The GLSL `main` moves clip depth to OpenGL's -w to w and turns y over.
So the back buffer (a framebuffer object) has Direct3D's rows:
- viewports and scissors are as Direct3D gives them, and read-backs are in order;
- Direct3D's clockwise front faces become counter-clockwise;
- an edge exactly on a pixel centre, as a pre-transformed quad's often is, falls as by Direct3D's
  top-left rule. (Keeping OpenGL's upward rows put such quads one row off.)

Present scales the back buffer into the window, turning it back over.

**In the game** (November 2026): the Africa1 mission runs smoothly with full screen and Alt-Tab
working, about 16.6 ms a frame on a 60 Hz display with `PresentMode=vsync`, though with a few short
bursts of frames over 20 ms that Direct3D 12 through SDL_GPU mostly did not have. Left to what the
game asks for (no vsync, its own limit of about 96 frames a second), Direct3D 12 held about 10.5 ms
and felt no different from its vsync, so the default stays what the game asks for.

**Only the render thread touches OpenGL.** The engine's texture thread works on memory alone; a
texture it releases has its GPU copy deleted at the next frame.

Uniforms go up as one buffer a frame, each block a range of it; vertices and indices likewise. The
pipeline key becomes state changes, made only when it differs. `SDLGPUDebug=1` also asks for a debug
context, and OpenGL's errors go to the log.

Result on the 12 game frames: the pictures are SDL_GPU's (31.0 - 45.6 dB; the menus differ from it
in no pixel count, 1,245,333 and 5,142 over 0 apart), no OpenGL errors, 0.5 - 10 ms a frame without
vsync.

## Frame captures

Phase 3 of the [cross-platform plan](cross_platform.md) puts new backends behind the device. Their
pictures are compared with Direct3D 8's on frames captured from the game.

**The recorder** ([`src/gfx/trace`](../src/gfx/trace)) is a device placed in front of the backend
when LS3DF.ini says `[Render] CaptureFrames=1` ([ls3df_ini.md](ls3df_ini.md#render)). It passes
every call on unchanged. Ctrl+Shift+F11 saves the next frame to
`LS3DF_frames\frame_<n>.ls3dframe` next to the DLL ([`trace_format.h`](../src/gfx/trace/trace_format.h)).
The file holds:
- the state the engine had set, and the resources that state names;
- every call up to Present, with each resource defined before its first use, and the bytes the
  frame writes into buffers and textures as it goes;
- the picture the game drew, read from the back buffer as IGraph's screenshots are.

A frame draws with things made long before it, so while it runs the recorder keeps:
- the states set since the device was made or reset;
- a copy of every vertex and index buffer's bytes (the engine's are write-only);
- the levels `UpdateTexture` puts into default-pool textures, which cannot be locked;
- the vertex shaders' declarations.

Managed textures are read back when the frame first uses them. The texture thread's calls go
through the same lock.

**Direct3D 8's rules the capture follows** (and so must every backend): a draw from user memory
(`DrawPrimitiveUP`, `DrawIndexedPrimitiveUP`) leaves stream 0 unset, and the indexed one leaves
the index buffer unset too.

**`test_render_replay`** replays captures on the Direct3D 8 backend in a hidden window. It reads
the back buffer back and compares it with the game's picture; the same driver must draw the same
frame. It writes `<capture>.d3d8.png`, `.game.png` and `.diff.png` (differences x4) next to each
capture.
- **The same driver:** the game's picture was drawn by its folder's `d3d8.dll`, which may be a
  wrapper such as dgVoodoo's. The replay loads `--d3d8 <path>`, else a `d3d8.dll` in the folder
  above the capture's (the game's), else Windows' own. A depth format the driver lacks (dgVoodoo
  offers D32, Windows' does not) falls back to the nearest one it has.
- **Big files:** a capture of a mission can be several hundred MB (its textures, every level), so
  records are read one at a time. With `--selftest` it draws its own two frames through the recorder and captures the second.
The scene covers:
- states and resources from before the capture;
- a write-only vertex buffer;
- a default-pool texture filled by `UpdateTexture`;
- lighting, fog and blending;
- buffer and texel writes during the frame;
- draws from user memory, and a draw refused after them.

Result on the self-test: the replay matches pixel for pixel (0 apart). Six deliberate bugs in the recorder are all
caught: the frame's buffer writes or texel rows dropped, the user-memory draws' reset of stream 0
ignored, a default-pool texture left empty, the earlier states not saved, the buffer copies not
kept.

Result on the game (November 2026): 12 frames captured in the game, through dgVoodoo, all replayed
pixel for pixel (0 apart), among them a mission at night in rain and fog with trees, a character
and the HUD. The captures run from 10 to 600 MB.

The SDL_GPU backend is compared the same way (`--backend sdlgpu`, `--driver direct3d12` or `vulkan`;
`--live sdlgpu` draws the self-test live on it, through Present and the read-back). On the
12 game frames, against dgVoodoo's pictures (Direct3D 11), first version, November 2026:
- 31 to 45.5 dB; the menus 37.6 - 45.5 dB, the missions 31 - 38.4 dB;
- no frame has a bias of more than 0.6 of 255 in any channel. Most pixels that differ are 1 or 2
  apart (the two APIs' filtering), and the rest are along alpha-tested foliage edges.

**`test_render_divergence`** finds the first draw (or clear) at which two backends' pictures of a
capture stop agreeing, and what that draw was:
```
test_render_divergence.exe <capture> [--d3d8 <dll>] --backend sdlgpu --tolerance 8 --min-pixels 64 [--minimize]
```
- The capture is validated first, without a device: each record's size against the file, each
  payload against its operation's layout, resource ids defined before use and not redefined while
  live. A capture with an error is not replayed (`--index <capture>...` only validates).
- Checkpoint k is the capture up to the k-th clear or draw, then END_SCENE and PRESENT, replayed
  from the start on a newly made device on each side (a read-back in the middle would run the fixed
  renderer's frame early). Every ⌈√N⌉-th checkpoint, then one by one in the first window that
  differs. Before a draw is named, the reference replays its prefix again to the same picture, and
  every pixel that newly differs must be one the draw changed on a side.
- `report.md` lists the capture's Direct3D 8 state at the draw and the pipeline the SDL_GPU or
  OpenGL side recorded for it (`S_pipelineKey`, the shader's mode, samplers and stages, read
  through `fixed::SetFrameObserver`), with expected / actual / difference PNGs.
- `repro.ls3dframe` is that prefix with the reference picture as its game's picture, so
  `test_render_replay repro.ls3dframe --backend <test>` fails on its own. `--minimize` adds
  `minimal.ls3dframe`: delta debugging over the draws before it (clears stay), keeping a candidate
  only when a replay still shows at least 90% of the draw's newly differing pixels, and only the
  textures and buffers the kept draws use. Every candidate is replayed, so the result reproduces.
- `--inject OP:rs:STATE:VALUE` (or `ts`, `tex`) puts a state change on the test side before draw OP,
  for testing the tool; `--selftest` does that on its own scene.
- A difference that later draws paint over completely is found only with `--stride 1`.

Exit codes: 0 no divergence, 3 found, 4 found but not localized (the reason is in the report), 2 bad input.

Result on the game frames (November 2026, dgVoodoo against SDL_GPU on Direct3D 12, tolerance 8,
64 pixels): 10 to 53 s a mission frame. It found the bootprints lost to ZBIAS (frame 3, draw 375,
1478 pixels; since fixed, the frame has no divergence) and depth ties along edges (frame 7, draw
60: 112 of its 120 pixels drawn by Direct3D 8 only, all drawn with ZFUNC ALWAYS). `--minimize`
cut those repros to a clear and one earlier draw each, in 11 - 15 candidates (18 - 34 s): frame 7
114 MB to 13.8 MB, frame 4 53 to 10.3 MB, frame 3 (the bug put back with `--inject 375:rs:47:0`)
134 to 10.8 MB, of which 7.7 MB is the picture. Each keeps all of the draw's differing pixels.

Direct3D 12 and Vulkan draw the same pictures (the self-test: identical, 47.2 dB from Windows'
Direct3D 8). `--repeat N` draws a capture N more times and times it (`--no-vsync` for SDL_GPU;
Direct3D 8 windowed always waits): the mission captures take 5 - 7 ms a frame on SDL_GPU.

## Not covered

Like the rest of the renderer, the backend has not run on a real Direct3D device yet. The tests'
fake devices exercise texture, surface and buffer creation, locking and the buffer managers. The
draw path, device creation, resets and presenting only compile.
