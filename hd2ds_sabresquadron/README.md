# HD2DS_SabreSquadron.exe — source port (started: it builds, not yet a server)

The Sabre Squadron dedicated server. It links the engine without sound
([LS3DRF.dll](../ls3drf)) and the data layer [rw_data.dll](../rw_data).

| | |
|---|---|
| File | `HD2DS_SabreSquadron.exe` (5,214,242 bytes) |
| SHA-1 | `8974c911016edc7e8e491cdbe322924ae178b4c9` (Steam; same as GOG) |
| Build date | 18 October 2004 (PE header) |
| Linker | 6.00 |
| Image base | `0x00400000` |
| Imports | `USER32.dll`, `ole32.dll`, `ADVAPI32.dll`, `GDI32.dll`, `OLEAUT32.dll`, `WINMM.dll`, `LS3DRF.dll`, `rw_data.dll`, `MPR.dll`, `imagehlp.dll`, `WSOCK32.dll`, `KERNEL32.dll` |

## How it is built

The server is the client's game code compiled again with server branches, so it is built from the game's
[src/](../src) with `HD2_DEDICATED` defined. Server code goes into `src/` in `#ifdef HD2_DEDICATED` blocks; this
folder holds only the build script and the stand-ins for the client files the server does not have.

```
python hd2ds_sabresquadron/build.py [--all] [--copy DIR]
```

It uses `_re/tools/build_game.py`'s compiler and linker flags, puts its objects in `%TEMP%\hd2ds_game` (the client
build's objects stay) and links `HD2DS_Port.exe` against `ls3drf/build/msvc/LS3DRF.lib` (build it first with
`python ls3drf/build.py dll`) and `rw_data.lib`. `--keep-client` builds every `src/` file and no stand-ins, to see
what the link needs without them; `--unresolved FILE` lists a failed link's missing symbols.

## Client-only files

`build.py`'s `CLIENT_ONLY` lists 19 `src/game` files that are not built: human poses (body, dead, hold, ragdoll,
seats, weapon), the weapon pose, stains, the car effects, the vehicle sounds / lamps / effects / damage visuals and the
in-game interface. None of their mapped functions matches a server function, and none pairs with a changed server
function (MEDIUM: only about half of the client's functions are mapped to a `src/` file). Three more files with no
matched function stay built because server code calls their other functions: `human_pose_ik.cpp` (pose math),
`input_controls.cpp` (`g_controls`, `WideToAnsi`, the render callback) and `speech_slots.cpp` (`LoadMaterialTables`).

[src/](src) has their stand-ins, a `*_ds.cpp` for each of the 13 files the rest of the game still links to:
122 functions and variables, with the original signature, the client's address where `src/` gives one and an empty
body (a default return; `true` for `Init` / `Done` / `LoadGame` / `SaveGame`). `isnd_ds.cpp` stands in for
`ISndGetDriver()`, which LS3DRF.dll does not export: a sound driver that reports no device.

## Status

- Builds: 265 sources, 0 messages; 0 unresolved symbols. Without the 19 files the link misses 122 symbols, with them
  only `ISndGetDriver`.
- Imports from LS3DRF.dll: the original server's 38 plus three `S_matrix` methods (`RotateByMatrix`, `SetDir` with
  two vectors, `SetRot`), by the same ordinals as the original DLL's.
- With `HD2_DEDICATED`, `src/ls3d/i3d.h` and `igraph.h` declare LS3DRF.dll's interfaces: no render, draw, input
  or window methods (IGraph keeps LS3DRF's 13 slots), and the scene's and light's inline accessors read LS3DRF's
  offsets. The game's calls to the removed methods are guarded in `src/`: the server gets no-ops, zero input and
  null textures, streams and windows. Without the define the client's code is unchanged.
- Not a server yet: beyond those guards `src/` has no server code, and some client paths (fonts, movie controls,
  faces and decals with null textures, a message box with no window) may fail or block at run time. Not run.
