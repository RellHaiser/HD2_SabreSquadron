# LS3DRF.dll — source port

Clean C++17 rewrite of `LS3DRF.dll`, the build of the LS3D engine (version 5.59.5, build of
16 August 2004) that the Hidden & Dangerous 2 / Sabre Squadron dedicated server
(`HD2DS_SabreSquadron.exe`) loads in place of `LS3DF.dll`. It is the same engine without what a
server does not need: no sound (ISND), no video (IShow), no input, no window, no Direct3D
device and no textures. What is left is the scene graph (frames, visuals, sectors and portals,
lights, models and animation), collision queries, the model / scene / animation loaders, the
vertex / index buffers and the math exports.

| | |
|---|---|
| File | `LS3DRF.dll` (405,504 bytes) |
| SHA-1 | `badf85f7410329a30661d318188016596cc35d23` (Steam; same as GOG) |
| Build date | 16 August 2004 (PE header) |
| Image base | `0x10000000` |
| Exports | 59 ([ls3drf.def](ls3drf.def)): LS3DF's 60 minus `ISndGetDriver`, the ordinals after it shifted down by one |

The port starts from the [LS3DF.dll port](https://github.com/RellHaiser/LS3DF) (taken at its
commit `aecd644`), cut down to what `LS3DRF.dll` has. Its address comments come in two kinds:

- `// 0x1000xxxx` — inherited from the LS3DF port: the address of the function in **LS3DF.dll**;
- `// LS3DRF 0x1000xxxx (from LS3DF 0x1000xxxx)` — ported from the body in **LS3DRF.dll**, where
  it differs from LS3DF's, followed by a note on what differs.

The original's quirks that the game can observe are kept (and commented); plain memory bugs are
fixed, each with a comment (`Fix:`).

The companion port of `rw_data.dll`, the game's data-file layer, is at
<https://github.com/RellHaiser/rw_data>.

## Status

> **Not tried with the server yet.** Every one of the original's 722 engine functions (the C
> runtime and static initialisers left out) has a counterpart in the port, 275 of them ported
> from LS3DRF's own body. The DLL links with the original's 59 exports. It is not a drop-in
> replacement yet: some classes (emitors and others) still have LS3DF's
> larger layouts, and the server reads engine objects directly.

- `src/gfx` keeps the render device interface with a null device: LS3DRF draws nothing. The
  Direct3D 8 backend lives only in [tests/d3d8](tests/d3d8), for the tests.
- Fields that LS3DRF lacks are kept at their LS3DF offsets until a whole class moves to LS3DRF's
  layout; each says so in a comment.

## Building

Requirements:

- Windows with Visual Studio 2019 or later and its C++ x86 tools ("Desktop development with
  C++"); `build.py` finds them with `vswhere`;
- Python 3;
- MinGW's `dlltool` (32-bit; MSYS2's mingw32 package, for instance), used once to make the import
  library of `rw_data` from [lib/](lib) — set `DLLTOOL` to its path if it is not on `PATH`.

```bat
python build.py          rem the DLL: build\msvc\LS3DRF.dll (+ .pdb)
python build.py check    rem compile every file, no link (and check the device boundaries)
python build.py test     rem the tests: compare_*.exe
python build.py run      rem run the built tests (see Tests)
python build.py --debug  rem a debug build
```

Intermediate files go to `%TEMP%\ls3drf_build` (a synced source folder such as Google Drive or
OneDrive locks freshly written files and breaks the compiler).

## Tests

The tests in [tests/](tests) are differential: each loads an **original** DLL from your copy of
the game (it is not part of this repository) and compares it with the port on random operations
with a seed. They run the original on fake Direct3D objects, so they need no graphics hardware.

- `compare_buffers`, `compare_mesh` and `compare_material` compare with the original
  `LS3DRF.dll` (from the dedicated server).
- `compare_frame`, `compare_collision`, `compare_math` and `compare_dxerr8` compare with the
  client's original `LS3DF.dll`, where LS3DRF's code is LS3DF's. Where LS3DRF differs,
  `compare_frame` and `compare_collision` patch LS3DF.dll in memory to behave as LS3DRF.dll does.

The original is found by the test's first argument, else `%HD2_DIR%\<name>.dll`, else the DLL
search path. The game folder must be on `PATH` in any case (the originals import `rw_data.dll`
and others from it). A DLL that is not the original MSVC 6 build is refused.

```bat
set HD2_DIR=C:\Games\Hidden and Dangerous 2
set PATH=%HD2_DIR%;%PATH%
compare_frame.exe "" 20000 1
compare_mesh.exe "" 20000 1
```

`python build.py test run` builds the tests and runs each with these arguments (20000 operations,
seed 1; `--ops N`, `--seeds 1,2`), one line per run; the full output goes to
`%TEMP%\ls3drf_build\logs`. It puts `HD2_DIR` on `PATH` itself.

Each test prints what it covered and ends with "identical" / "0 differ" (exit code 0) or the
first difference (exit code 1).

## Layout

| Path | What |
|---|---|
| `src/core` | log and errors, version, CPU detection, engine start-up (`DllMain`), containers |
| `src/math` | the math exports |
| `src/i3d` | the scene graph, collision, loaders, buffer managers |
| `src/igraph` | IGraph and the texture flags |
| `src/gfx` | the render device interface and its null device |
| `src/platform` | time and the few Win32 calls left |
| `src/dx` | the Direct3D 8 and DirectInput types the engine's interfaces still name; DXGetErrorString8 |
| `src/external` | the interface of `rw_data.dll` |
| `tests` | the differential tests, their fake Direct3D device and the Direct3D 8 backend |
| `lib` | import definitions for `rw_data.dll` |
| `ls3drf.def` | the exports and their ordinals |

## Third-party code

The original statically links code it did not write; the port rewrites it from the binary as
well:

- `DXGetErrorString8` (`dxerr8.lib` of the DirectX 8.1 SDK, © Microsoft): its table of result
  code names.

`src/dx` declares Direct3D 8 types as Microsoft's SDK did; `src/external` declares the interface
of `rw_data.dll`.

## Disclaimer

This is an unofficial fan project, not affiliated with or endorsed by Illusion Softworks,
2K / Take-Two Interactive or any other holder of rights to Hidden & Dangerous 2. The source was
written by analysing the behaviour of the original DLL; the repository contains no files of the
game (no binaries, data or assets). You need your own copy of the game to use or test it.
Trademarks belong to their owners.
