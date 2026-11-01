# LS3DRF.dll — source port (not started)

`LS3DRF.dll` is the LS3D engine build that the dedicated server (`HD2DS_SabreSquadron.exe`)
loads in place of `LS3DF.dll`. It is the same engine version and build date as `LS3DF.dll`, with
no sound: its 59 exports are LS3DF's 60 minus `ISndGetDriver`
([ls3drf.def](ls3drf.def); the ordinals after it shift down by one).

| | |
|---|---|
| File | `LS3DRF.dll` (405,504 bytes) |
| SHA-1 | `badf85f7410329a30661d318188016596cc35d23` (Steam; same as GOG) |
| Build date | 16 August 2004 (PE header) |
| Linker | 6.00 |
| Image base | `0x10000000` |
| Imports | `WINMM.dll`, `KERNEL32.dll`, `USER32.dll`, `ole32.dll`, `rw_data.dll` |
| Exports | 59 ([ls3drf.def](ls3drf.def)) |

The sources start as a copy of the [LS3DF port](../ls3df) (`src/`, `tests/`, `lib/`, `build.py`), taken
at LS3DF devel `aecd644`; later LS3DF changes are carried over by hand. What differs (no sound, smaller
scene and some visuals) is cut down here. Address comments copied from LS3DF still name LS3DF.dll
addresses, and `tests/` still compare against the original LS3DF.dll.

## Status

Copied, not yet cut down: `python build.py` links `build/msvc/LS3DRF.dll` with the original's 59
exports, but the code is still LS3DF's. Function names and types are being recovered in Ghidra.
