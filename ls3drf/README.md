# LS3DRF.dll — source port

> The source is the `ls3drf/` folder of [HD2_SabreSquadron](https://github.com/RellHaiser/HD2_SabreSquadron):
> make changes there. [readher/LS3DRF](https://github.com/readher/LS3DRF), where it was first published,
> is archived (read-only).

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
commit `aecd644`, with its later fixes carried over: see [Source and LS3DF](#source-and-ls3df)),
cut down to what `LS3DRF.dll` has. Its address comments come in two kinds:

- `// 0x1000xxxx` — inherited from the LS3DF port: the address of the function in **LS3DF.dll**;
- `// LS3DRF 0x1000xxxx (from LS3DF 0x1000xxxx)` — ported from the body in **LS3DRF.dll**, where
  it differs from LS3DF's, followed by a note on what differs.

The original's quirks that the game can observe are kept (and commented); plain memory bugs are
fixed, each with a comment (`Fix:`).

The companion port of `rw_data.dll`, the game's data-file layer, is at
<https://github.com/RellHaiser/rw_data>.

## Status

> **Runs the port's server.** The dedicated server built from HD2_SabreSquadron's `src/`
> ([hd2ds_sabresquadron](../hd2ds_sabresquadron)) loads it and hosts a map. With the original server it has only run
> under a patched copy, not as a plain drop-in. Every one of the original's 722 engine functions (the C
> runtime and static initialisers left out) has a counterpart in the port, 275 of them ported
> from LS3DRF's own body. The DLL links with the original's 59 exports, and its 26 engine classes
> have LS3DRF's virtual tables, sizes and field offsets (the server reads and calls engine objects
> directly); what a few fields mean is still uncertain. Parts of the DLL are verified against the
> original by the differential tests below; the rest has run only as far as the server uses it. Use it in a copy of
> the game folder and keep the original DLL.

| Part | Verified against the original by |
|---|---|
| Math exports (vectors, matrices, quaternions, planes) | `compare_math`: 20,000 rounds, all exact under the game's FPU precision |
| DXGetErrorString8 | `compare_dxerr8`: 2,067,333 result codes |
| Frames, lights, cameras, sectors, portals, occluders, areas, models, animation | `compare_frame`: 2 × 20,000 random operations |
| Collision queries (rays, picking, moving spheres) | `compare_collision`: 2 × 20,000 random queries and scene changes |
| Materials | `compare_material`: 2 × 20,000 operations |
| Vertex / index buffer managers | `compare_buffers`: 2 × 20,000 operations |
| Meshes, mesh levels, face groups | `compare_mesh`: 2 × 20,000 operations |
| The billboard's facing matrix | `compare_billboard`: 2 × 20,000 operations |
| The model / scene / animation loaders, the rest of the scene and of the driver | not compared with the original |

LS3DRF draws nothing and has no render device. The Direct3D 8 declarations the tests' fake
devices need live in [tests/d3d8](tests/d3d8). The tests are described [below](#tests); each
one's header says what it compares and what it leaves out.

## Using the DLL

`python build.py` makes `build\msvc\LS3DRF.dll` (32-bit, the C runtime linked statically: nothing
to install). In a **copy** of the game folder, replace `LS3DRF.dll` with it; the dedicated server
(`HD2DS_SabreSquadron.exe`) loads it. It needs the game's own `rw_data.dll` and nothing else
outside Windows.

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
python build.py test     rem the tests: build\compare_*.exe
python build.py run      rem run the built tests (see Tests)
python build.py check64  rem compile every source and header for x86-64 with Clang (see below)
python build.py --debug  rem a debug build
```

Intermediate files go to `%TEMP%\ls3drf_build` (a synced source folder such as Google Drive or
OneDrive locks freshly written files and breaks the compiler).

### Linux and other POSIX systems

```sh
python3 build.py gcc --m32    # build/gcc32/libls3drf.so: 32-bit, the original's layouts
python3 build.py gcc          # build/gcc/libls3drf.so: the compiler's default target
```

`CXX` picks the compiler (default `g++`; Clang works the same way), `CXXFLAGS` and `LDFLAGS` are
added, and `RW_DATA_LIB` names the [rw_data port](https://github.com/RellHaiser/rw_data) built as a
shared library: it is then linked, and nothing may stay undefined. Without it the `dta*` functions
stay undefined until the library is loaded next to `librw_data.so`.

How the build differs from the Windows one:

- The Win32 types the interfaces are written in (`HRESULT`, `DWORD`, `GUID`, ...) come from
  [platform/win_types.h](src/platform/win_types.h), with their Win32 sizes. `__stdcall` and the
  other calling conventions expand to the compiler's default. No Windows binary is involved, and
  the engine and its client are built with the same headers.
- The operating system calls are in [platform/posix](src/platform/posix): monotonic clocks, no
  cursor, a message on `stderr` instead of the version check's message box. The log goes next to
  the executable on Linux and macOS, and to the current directory elsewhere.
- There is no `DllMain`. A static object in [core/engine.cpp](src/core/engine.cpp) starts and stops
  the engine, and `build.py` links it where it initialises after every other global.
- Every symbol is exported with its Itanium C++ name: there are no ordinals and no `ls3drf.def`.
- `-fno-strict-aliasing`: the code reads floats' bits through other types, as MSVC allows.

**Status: not yet built on Linux.** The POSIX side has only been compiled, linked and loaded on
Windows, with 32-bit MinGW GCC 11 (`set CXX=g++` then `python build.py gcc`, which defines
`LS3D_FORCE_POSIX` for a compiler that targets Windows). That check DLL loads, starts the engine,
runs `IGraph::Init` and unloads. It is not a drop-in replacement for `LS3DRF.dll`. The differential
tests need the original DLLs and run on Windows only.

### 64-bit builds

The engine's classes have the original's sizes and offsets only where a pointer is 4 bytes. Their
checks are `LS3D_LAYOUT32` ([core/layout.h](src/core/layout.h)): a `static_assert` in a 32-bit
build, compiled but not enforced in a 64-bit one, which has its own layouts and no binary to match.
Plain data (the math types, the Direct3D structures, bounding boxes) keeps its `static_assert`s
everywhere.

Where a pointer used to travel in 32 bits, the 64-bit build differs:

- **The animation blob's track table** holds (name, track) pointer pairs where the file has two
  32-bit offsets. A 64-bit loader moves the rest of the blob up by `I3D_animation_set::TableShift`,
  a multiple of 16 so the rotation keys keep their alignment. `Open` then writes the wider pairs.
  The same code writes 8-byte pairs in place in a 32-bit build.
- **32-bit note values** stay offsets in a 64-bit build. `GetNoteKey32` and the `I3DCALLBACK_NOTE32`
  callback resolve them to pointers when they are read. A 32-bit build turns them into pointers on
  load, as the original does.
- **Parameters that can carry a pointer are `uintptr_t`:** `I3D_emitor::SetParam` / `GetParam`
  (the colours), the frame callback's first parameter, `GetNoteKey32`'s value, the error
  callback's context and the driver's `Init` argument (`unknown04`). In a 32-bit build
  `uintptr_t` is the same type as `uint32_t`, so the 32-bit interfaces have not changed.

**Status: compiles, not yet run in a server.** `python build.py check64` compiles every source and
every header on its own for x86-64 with Clang (on Windows for the Win32 platform and for the POSIX
types), and it runs clean. Clang's `-Wshorten-64-to-32` and its pointer/integer cast warnings find
nothing in a 64-bit build. Every animation file the game ships (2,315 `.5ds` files) was loaded
through `I3D_animation_set::Open` in a 32-bit and a 64-bit Clang build, and everything the getters
return was dumped. The two dumps are identical. Nothing else has run as 64-bit code: the
differential tests need the original 32-bit DLLs.

`build.py` checks that every file naming a Win32 type or calling convention includes
`win_types.h` (MSVC knows `__stdcall` without a header, so only another compiler would notice). It
also checks that the Windows and POSIX calls stay in their backends, like the device boundaries.

## Tests

The tests in [tests/](tests) are differential: each loads an **original** DLL from your copy of
the game (it is not part of this repository) and compares it with the port on random operations
with a seed. They run the original on fake Direct3D objects, so they need no graphics hardware.

- `compare_billboard`, `compare_buffers`, `compare_mesh` and `compare_material` compare with
  the original `LS3DRF.dll` (from the dedicated server).
- `compare_frame`, `compare_collision`, `compare_math` and `compare_dxerr8` compare with the
  client's original `LS3DF.dll`, where LS3DRF's code is LS3DF's. Where LS3DRF differs,
  `compare_frame` and `compare_collision` patch LS3DF.dll in memory to behave as LS3DRF.dll does.

The original is found by the test's first argument, else `%HD2_DIR%\<name>.dll`, else the DLL
search path. The game folder must be on `PATH` in any case (the originals import `rw_data.dll`
and others from it). A DLL that is not the original MSVC 6 build is refused.

```bat
set HD2_DIR=C:\Games\Hidden and Dangerous 2
set PATH=%HD2_DIR%;%PATH%
cd build
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
| `src/core` | log and errors, version, CPU detection, engine start-up (`DllMain`, or a static object off Windows), containers |
| `src/math` | the math exports |
| `src/i3d` | the scene graph, collision, loaders, buffer managers |
| `src/igraph` | IGraph and the texture flags |
| `src/platform` | the Win32 types on every system (`win_types.h`); the operating system calls: `win32/` and `posix/` |
| `src/dx` | the Direct3D 8 and DirectInput types the engine's interfaces still name; DXGetErrorString8 |
| `src/external` | the interface of `rw_data.dll` |
| `tests` | the differential tests, their fake Direct3D device and the Direct3D 8 backend |
| `lib` | import definitions for `rw_data.dll` |
| `ls3drf.def` | the exports and their ordinals |

## Source and LS3DF

The source is the `ls3drf/` folder of [HD2_SabreSquadron](https://github.com/RellHaiser/HD2_SabreSquadron):
make changes there. It was published as [readher/LS3DRF](https://github.com/readher/LS3DRF) and pulled in as a
`git subtree`; that repository is now archived, so there is nothing left to pull.

The port itself began as a copy of LS3DF's, not a subtree: its classes have LS3DRF's layouts and
much of the code is gone, so the two are different files from then on. A fix in LS3DF does not reach
it by itself. **`LS3DF_CARRIED` in `build.py` names the LS3DF commit carried over so far**, and in
HD2_SabreSquadron `python ls3drf/build.py check` fails, listing the files, when `ls3df/` has since
changed a file this port still has. (Of the changes after `aecd644`, only `I3D_joint::Uninit`
applied; the others are in the render path, the Direct3D device or the import libraries, which
LS3DRF does not have.)

To carry changes over, in this repository (`NEW` is the LS3DF commit):

```bat
git fetch https://github.com/RellHaiser/LS3DF main
git log --oneline CARRIED..NEW -- src tests build.py
git diff CARRIED NEW -- src/i3d/joint.cpp | git apply --ignore-whitespace
```

Apply what touches a file this port still has, one file at a time as in the last line, run the
tests, and set `LS3DF_CARRIED` to `NEW`.

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
