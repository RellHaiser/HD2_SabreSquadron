# Hidden & Dangerous 2: Sabre Squadron — C++ source port

A reconstruction of the game client of *Hidden & Dangerous 2: Sabre Squadron* (Illusion Softworks)
as readable, compilable C++, written from the decompiled 32-bit executable. The engine is LS3D; the
port keeps its structure, names and data formats so that it can stand in for the original code.

This repository holds no files of the game. Running anything built from it needs an installed copy
of the game and its data.

## Status

**Runnable (2026-11-06).** The port builds into `HD2_Port.exe`, a drop-in replacement for the original
executable, entirely from the hand-ported sources. It has been played from the main menu to the end of
missions, in single player and as a multiplayer client. In play, its combat against the AI has so far
seemed identical to the original's.

No live game logic is known to be left unported: 10,777 of the executable's functions are ported, and what
remains is compiler library code, small helpers the port keeps inline, or code the original cannot reach.
That rests on a sweep of the remaining functions, not on a proof (see `_re/TRIAGE.md`).

Tested by playing:

- **Single player:** every mission loads. Africa1, Africa2 and Africa6 played to the end, with the
  end-of-mission screen and stat increases. Part of the Tutorial's training, Africa5 and Operation Snowball.
- **Gameplay:** infantry weapons, grenades, launchers, explosives, fixed machine guns, vehicles and their
  weapons, the aqualung and submarines, squad orders, the tactical map, stealth and disguise, the
  inventory, saving and loading in a mission.
- **Multiplayer, as a client:** an internet server and a LAN server, in deathmatch, cooperative, occupation
  and objectives; a cooperative game with other people on the original's dedicated server.
- **Other:** full screen and windowed, sound, the video settings' gamma slider.

Not tested yet:

- Hosting from the game with another player joined. (LAN and internet listen servers have been started and spawned in;
  no report yet of another player joining.) The port's dedicated server has hosted a client: see
  [hd2ds_sabresquadron](hd2ds_sabresquadron/README.md).
- The other missions past loading, the few squad orders that need particular gear, and a side-by-side
  measurement against the original of how the fights balance.

Known problem: one crash while healing a teammate in a cooperative game, not explained yet.

`_re/RUNNING.md` keeps the details: the bugs found by running, the tools and what was exercised.

## Running

You need an installed copy of the game, *Hidden & Dangerous 2: Sabre Squadron* version 1.12, and a build of
[LS3DF](https://github.com/RellHaiser/LS3DF), the engine port (its `main` branch is in `ls3df/`). Work in a
copy of the game's folder.

1. Build `LS3DF.dll` (`python ls3df/build.py`, below) and put it in the folder. **The game's original
   `LS3DF.dll` does not work with the port**: it writes 16 bytes through 12-byte results, which a modern
   compiler's stack layout does not survive, so it crashes loading a mission. An older LS3DF build lacks
   the fixes for rain, loading a saved game and the sky.
2. Build the port (below) and put `HD2_Port.exe` in the folder, with `HD2_Port.pdb` if you want crash
   reports with names.
3. Run `HD2_Port.exe`. It uses the folder's `rw_data.dll`, `IJoy.dll`, Bink and Vorbis libraries, and the
   installed game's settings and data as they are.

- `-window` runs in a window whatever the game's setup says. This is the port's own switch.
- Do not use `-c`: it is the console mode, and hides the window. The game looks for ` -c` and `-cmd` with a
  plain text search, so avoid them in folder names too.
- A crash writes `HD2_Port.RPT` next to the executable, with the call stack. The next crash overwrites it.

## Building

Needs Visual Studio with the C++ x86 tools, and Python; the engine DLLs also need MinGW's 32-bit `dlltool`
(MSYS2's mingw32 package, for instance). From the repository root, the DLLs the executable links against
first, then the executable:

```bash
python ls3df/build.py
```

```bash
python _re/tools/build_rw_data.py
```

```bash
python _re/tools/build_game.py
```

`ls3df/build.py` makes `ls3df\build\msvc\LS3DF.dll` and `build_rw_data.py` makes `rw_data\build\msvc\rw_data.dll`,
each with its import library; `build_game.py` links against those, or against sibling clones of the two
repositories (`..\LS3DF`, `..\RW_DATA`) when they are not built here. `python ls3drf/build.py` makes
`LS3DRF.dll`, the dedicated server's engine (not needed by the game).

The executable and its symbols land in `%TEMP%\hd2_game`; add `--copy DIR` to copy them to the game's
folder. Only changed sources are compiled, so use `--all` after a header change. A full build takes about a
minute.

- The default build, `/O2 /MT` with x87 floats (`/arch:IA32`), is the one to play: the game's math depends
  on the x87 floats.
- `--checked` builds `/Od /MTd /RTC1` in `%TEMP%\hd2_game_checked`. It stops at wrong call conventions and
  stack overruns, and is the first thing to try when something goes wrong.
- The window title and the icon are added at link time, the icon read from an installed copy of the game
  (`ICON_SOURCE` in `build_game.py`, a path on the author's machine). Without it the executable builds with
  neither, and works.

`python _re/tools/build_check.py` and `python _re/tools/link_check.py` are the quick checks: every source
compiled with diagnostics, then the link.

## Approach

- **Hand ports (`src/`, except `src/tier1`).** Each subsystem is rewritten from the decompilation and the
  assembly into C++ that follows the original's naming style: `C_Class`, `S_Struct`, PascalCase methods,
  `m_` members. Declarations carry the original address as a `// 0x00XXXXXX` comment.
- **Tier 1 (`src/tier1`).** A generated, mechanical translation of the whole program, which kept every
  function present while the hand port proceeded. It is not part of the executable, and is kept for
  reference.
- **Behaviour.** The original's behaviour is kept, including its oddities (commented `Quirk kept`). Plain
  crash and memory bugs are fixed (commented `Fix:`). Copy protection is dropped.
- **Validation.** Ported code is checked against the original's assembly and, for engine-heavy parts, run in
  test harnesses against stub engines and the game's own data (vehicle models, scripts, tables), with
  mutation testing of the tests. Since it became runnable, the port has also been tested by playing it,
  comparing with the original where something looks wrong.

Related ports, each a drop-in for the game's own DLL: [LS3DF](https://github.com/RellHaiser/LS3DF) (the
engine), [rw_data](https://github.com/RellHaiser/rw_data) (the archives), [IJOY](https://github.com/RellHaiser/IJOY)
(joystick input). The port needs LS3DF; the other two are optional. All three are included here as
subtrees (`ls3df/`, `rw_data/`, `ijoy/`). `ls3drf/` holds LS3DRF, the engine without sound or rendering that the
dedicated server loads; it is maintained here (its old repository, readher/LS3DRF, is archived).

## Layout

| Path | Contents |
|---|---|
| `src/common` | Strings (`C_str`), chunked data files (`C_chunk`), configuration files, file wrappers |
| `src/ls3d` | The LS3D engine interfaces the game uses: math, `I3D_*` (scene, frames, visuals), `IGraph`, `rw_data` |
| `src/game` | The game: app core and modules, actors (humans, vehicles, items, ...), AI, physics, interface, sound |
| `src/net` | Multiplayer: the network game, transport, replication, console commands, GameSpy, the firewall |
| `src/script` | The mission script language |
| `src/tier1` | Generated code (see above), not built into the executable |
| `_re` | The reverse-engineering workspace: build and analysis tools, test harnesses, notes (`RUNNING.md`, `TRIAGE.md`, `PROGRESS.md`), patches for LS3DF |
| `tools` | Model and animation (`.4ds`, `.5ds`) tools and statistics |
| `ls3df` | The LS3D engine DLL, `LS3DF.dll` ([RellHaiser/LS3DF](https://github.com/RellHaiser/LS3DF), kept in sync with `git subtree --prefix=ls3df`); built and tested with its own `build.py` |
| `rw_data` | The virtual file system DLL, `rw_data.dll` ([RellHaiser/rw_data](https://github.com/RellHaiser/rw_data), kept in sync with `git subtree --prefix=rw_data`); `src/ls3d/rw_data.h` forwards to its header; built by `_re/tools/build_rw_data.py` |
| `ijoy` | The joystick DLL, `IJoy.dll` ([RellHaiser/IJOY](https://github.com/RellHaiser/IJOY), kept in sync with `git subtree --prefix=ijoy`); built and tested against the original with its own `build.py` (MSVC only) |
| `ls3drf` | The dedicated server's engine DLL, `LS3DRF.dll`: LS3DF's port without sound, video, input or the render device (began as [readher/LS3DRF](https://github.com/readher/LS3DRF), now archived: this folder is its source); built and tested with its own `build.py`. Its code began as a copy of LS3DF's, so LS3DF's fixes are carried over by hand ([how](ls3drf/README.md#source-and-ls3df)) |
| `hd2ds_sabresquadron` | The dedicated server, `HD2DS_SabreSquadron.exe`: built from `src/` with `HD2_DEDICATED`; hosts maps and a client has played on it ([status](hd2ds_sabresquadron/README.md#status)) |
| `docs` | [Mission scripting language reference](docs/HD2_Mission_Scripting_Language.md) |

## Legal

This is an unofficial, non-commercial reconstruction for preservation and study, not affiliated
with the game's developers or publishers. *Hidden & Dangerous 2* and its content belong to their
respective owners.
