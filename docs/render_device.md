# The render device (gfx)

All of the engine's drawing goes through one interface, `gfx::Device` (`gfx::g_pGfx`), declared
in [`src/gfx/gfx.h`](../src/gfx/gfx.h). Its only implementation so far is the Direct3D 8 backend
in [`src/gfx/d3d8`](../src/gfx/d3d8), which passes every call to Direct3D 8 unchanged. The DLL
therefore makes exactly the Direct3D calls it made before, in the same order with the same
arguments, and the differential tests still compare equal.

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

## Not covered

Like the rest of the renderer, the backend has not run on a real Direct3D device yet. The tests'
fake devices exercise texture, surface and buffer creation, locking and the buffer managers. The
draw path, device creation, resets and presenting only compile.
