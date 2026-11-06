# Trial link of the hand port (Part 13a, 2026-11-06)

**Since part 13b the executable links, and since 13c it runs to the main menu: see `_re/RUNNING.md`. What follows is the state at 13a.**

Regenerate: `python _re/tools/build_check.py`, then `python _re/tools/link_check.py`. It writes `_re/link_unresolved.txt`: symbol, decorated name, the objects that want it. Delete `dog*.obj` or any other stale object from `%TEMP%\hd2_build_check` first: build_check does not remove objects whose source is gone (four `dog*` ones added 15 false symbols).

## Result

All 268 hand-ported files, MSVC x86, against `LS3DF.lib`, `rw_data.lib`, `IJoy.lib` (built from the ports; they carry the original DLLs' export names) and the system libraries: **52 unresolved symbols, no duplicate definitions, no exe yet.**

Tier 1 is not in this link, and cannot be: it is g++ (`-fpermissive`), with its own types in `namespace tier1`, and its functions are not callable from the hand port. So "tier 1's stubs" were never an option for a linked build. **The game is the hand port alone**, and tier 1's 74 errors do not block anything: they stop mattering.

## The 52 symbols

| Kind | Count | What |
|---|---|---|
| Globals declared, defined nowhere | 23 | `g_materials`, `g_materialCount`, `g_localPlayerSlot`, `g_carScene`, `g_levelMinY`, `g_netSceneTop`, `g_serverName`, `g_graphFlags`, `g_missionVariant`, `g_netPlayerCount`, `g_netPlayerTable`, `g_netQueryThread`, `g_netDefaultSessionText`, `g_scriptModuleRestarting`, `g_defaultFormationScale`, `g_frameListenersBegin` / `End`, `g_protectionTableA` / `B`, `g_protectionIndexA` / `B`, `g_protectionIndexBreathA` / `B` |
| `C_QuickMessages` | 13 | Cluster `0x48e9f0`-`0x4906f5`, about 6 KB, not ported. The only real piece of work here |
| Listed in `ported.txt`, no definition in the source | 6 | `GetConsole`, `ReadUnicodeInput`, `ShowGameMenuMessage`, `FreeResourceCaches`, `RunExitSequence`, `C_Actor::GetSector`: check each; the addresses found by a name search are not reliable for all of them |
| Small functions not ported | 8 | `RandomFloat` (`0x475190`), `InitUnknown8CA4C8` / `FreeUnknown8CA4C8` (`0x6e8960`, `0x6e8970`), `RenderDebugStats` (`0x6e8860`, debug, off by a flag), `ShowMessage`, `S_MultiplayerSettings` ctor and dtor, `MeleeTrackerBarkTurn` |
| Window and modules | 2 | `GameWindowProc`, `ModuleManager::DispatchMessage` |

The protection tables are read by `human_combat` and `human_movement_base`: with the pass-through (part 11d) they need to read as a valid copy, not as zeroes. Check those reads when they are defined.

## What a link cannot see

The triage still has 886 live functions (74 KB) that nobody asks for by symbol. They are not all gaps. A look at those of 64 bytes or more:

- **Most are MSVC6 container code**: `vector::insert`, `map` nodes, `std::_Lockit`. The hand port has `std::` instead, so they will never be asked for. Only 120 functions (24 KB) of 64 bytes or more are left without those marks.
- **Of those 120**, the largest are `C_QuickMessages`; the melee tracker AI's save and load (`0x5d25b0`, `0x5d28b0`), which `melee_tracker_ai.h` says are unreachable; `RenderDebugStats`' helpers; small chunk helpers the hand port wrote inline.
- **`0x6e8860` and `0x6e8090`** draw the debug statistics, behind the flag `0x00844044`.
- Not read one by one: the other small ones. A function the hand port dropped by mistake would not show up here, only as behaviour in a run.

## Not checked by this link

- Whether the executable starts. Nothing has been run.
- The compiler's float code. `build_check` uses MSVC's default SSE2, while the game ran x87 in single precision. The first run should be built with `/arch:IA32` at least when comparing behaviour (see `float-precision`).
- Memory across the DLL boundary, and whether the original `LS3DF.dll` works with this executable as well as the ported one. Use the original first: fewer unknowns.
- Static initialization order of the globals.
- `/SUBSYSTEM:WINDOWS` and the libraries are a first guess: `shell32.lib` was added to the original's list without checking that it is needed.
