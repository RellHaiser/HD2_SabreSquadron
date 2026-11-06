# Running the port (Parts 13c, 13d, 13e, 2026-11-06)

The port starts, plays the videos, reaches the main menu, makes a profile, starts the campaign's Training mission, plays its intro cutscene with subtitles, and the soldier walks and turns. The in-game menu opens and "Exit to main menu" returns to the main menu. Part 13e: past Training to Operation Snowball: the campaign's save and its mission list, weapons and reloading, enemies that close in, shoot and kill, saving and loading in the middle of a fight, the saves also read by the original exe. Windowed: see "Not tested".

## Build

```bash
python _re/tools/build_game.py --copy "H:\HD2_PortRun"
python _re/tools/build_game.py --checked
```

x87 floats (`/arch:IA32`), symbols, a map; objects in `%TEMP%\hd2_game` or `%TEMP%\hd2_game_checked`. Only changed sources are compiled: use `--all` after a header change. A full build takes under a minute.

- Default: `/O2 /Oy- /MT`, the build to play.
- `--checked`: `/Od /MTd /RTC1`. A call with the wrong arguments stops at `_RTC_CheckEsp`, stack overruns at `_RTC_CheckStackVars`, and the debug heap fills memory. Use this first when something is wrong: it found the first and third bugs below immediately. Keep the exe's own PDB next to it (`HD2_PortChk.exe`, `HD2_PortChk.pdb`).

The title (string 3 of the original's table) and the icon go in at link time; the icon is read from `H:\HD2_Unmodified` and never kept in the repository. Without that folder the exe builds with neither.

The original's settings copied: base 0x400000, 1 MB stack, no DEP, not relocatable.

## Run

The run folder is `H:\HD2_PortRun`, a copy of `H:\HD2_Unmodified` with `HD2_Port.exe` added. It uses the **`devel` branch's build of the LS3DF port** (see below), and the original `rw_data.dll`, Bink, Vorbis. The game writes `log.txt` and `window.cfg` there. It only reads the registry (`HKLM\SOFTWARE\WOW6432Node\Illusion Softworks\Hidden & Dangerous 2`: the CD key, `LS3D_setup`), shared with the installed game.

- `-window`: the port's own switch. A window whatever `LS3D_setup` says. **Do not use `-c`**: it is the console mode, and it hides the window.
- Avoid ` -c` or `-cmd` anywhere in the command line and folder names: the game looks for them with `strstr`.

## Which LS3DF.dll

| Exe | DLL | Result |
|---|---|---|
| Original | Original | Clear weather, the reference |
| Port | Original | **Crash loading a mission.** The original's `S_vector::operator*(S_matrix)` and its kin write 16 bytes through a 12-byte result: `_RTC_CheckStackVars` in `C_PhysDoor`'s constructor (checked build), stack cookie failure in `C_Vehicle::Setup` (release build). MSVC6's stack layout hid this from the original exe; a modern compiler's does not |
| Original | `../LS3DF` as it stands | Screen-filling grey "fog": the rain |
| Port | `../LS3DF` as it stands | The same fog |
| Port or original | `LS3DF_Github` `devel` | Clear, rain as thin streaks |

The fog is the rain: `I3D_scene::SetWeather` converted its float parameters 5 and 7 from ints, so a width of 0.06 became about 5e8 and every drop a quad across the screen. Fixed in `LS3DF_Github` commit `586f479` (2026-10-26, `devel`), not in the `../LS3DF` folder or `origin/main`. **The fix is not in any of your branches yet: `_re/ls3df_patches/joint_uninit.patch` must be applied too (loading a saved game crashes without it).** `I3D_joint::Uninit` read its own `mesh` again after the line above it had cleared it (the slot's joint is usually itself); the original keeps the mesh in a register. **`build_game.py` links against `../LS3DF`'s import library; at run time use `devel`'s DLL**: export it (`git -C LS3DF_Github archive devel`) to a scratch folder, `python build.py dll`, copy the DLL and PDB to the run folder. Also kept there: `LS3DF_original.dll`, `LS3DF_ported.dll` (`../LS3DF`'s).

With `devel`'s DLL the sky is a flat dark green with either exe, where the original DLL shows clouds. That is the DLL's, not the game's: a difference for the LS3DF port. Not looked into.

## Tools

- `_re/tools/debugrun.cpp`: a small debugger (build it with MSVC x86, see its head). Logs DLLs, exceptions with registers and a symbolized stack, the exit code. `--timeout N` logs all threads and kills the game after N seconds.
- `_re/tools/to_menu.ps1`: Esc until the main menu shows.
- `_re/tools/shot.ps1`, `send_keys.ps1`: a picture of the game's client area, keys by scan code (0x01 is Esc), mouse by relative moves (the game adds up DirectInput's deltas, so `click:x:y` goes to the corner first; 1:1 at 1600x1200). Both refuse to act unless the game's window has the focus.
- `_re/tools/literal_audit.py`: string literals against the original's strings (Ghidra running); reports only near twins. **Run it after any port of a screen.** Its listing has no strings under 4 characters; those were checked once by scanning the data's bytes.
- `_re/tools/name_audit.py`: names in the source against the game's files.
- `_re/tools/vtable_audit.py`: the game's engine interfaces (`src/ls3d`) against the LS3DF port's headers, slot by slot: bytes of arguments, floats, a float result. **Run it after touching `src/ls3d`.**

## Bugs the first run found

| Bug | Found by |
|---|---|
| `IGraph::InitInput` declared with no argument (the original passes 0): the stack was off by 4, a register was lost in `RunGame`, crash at `C_GameApp::Init` | `--checked`, stack check |
| Same kind, unreached so far: `ISND_source::SetSound` has a flags argument; `CreateJointOverride` has none and `Add` takes the name (the original pushes the name first, then makes the override) | `vtable_audit.py` |
| Main loop: `GetGame()` used without its null test, in the menus. Also a command pending while the player's controls were off was dropped; the original keeps it | crash, then the assembly |
| `NetFirewallAvailable`: `OSVERSIONINFOA` on the stack, `GetVersionExA` told it had the 0x9C bytes of the larger struct: the system wrote past it | `--checked`, stack check |
| `S_IGraphInit` was the wrong layout (flags at +0x28, not +0x2C; bit depth, refresh rate and the error callback missing; flag names guessed): the engine got flags 0, so never full screen, and a window whose viewport failed. `C_TextBox::Render` could not restore the viewport after its first clipped box, so all later menu text was clipped away. Now from the assembly of 0x006db440 and the LS3DF port's header | a menu without text, `SetViewport` failing inside d3d8 |
| Part 13d: collision file header (`tree.klz`): frame count and block end read from the wrong offsets: +0x0C and +0x08, not +0x14 and +0x10 | crash at the first mission load, then the file and the assembly |
| Part 13d: `g_netPlayerTable` and `g_netPlayerCount`, which part 13b said nothing writes: they are members of `g_netQuantizations` (0x00982de8), written through `this`, which an xref search cannot see | null pointer in `WriteQuantized` at the spawn |
| Part 13d: `C_Actor::GetSector`, added in 13b, called itself for ever | stack overflow on entering the level |
| Part 13d: `C_KeyBindings::Load` kept the file's `changed` flag, usually false; the original sets it on every record. So a saved profile's keys were never bound, `GetValues` returned -1, `HandleInput` was never called: no Esc, no control of the soldier. It worked in 13c only because no profile existed yet | Esc stopped skipping the videos after a profile was made |
| Part 13d: the in-game menu's model names: `main_ingame`, `player_screen`, `player_screen_2`; the files are `main ingame`, `player screen`, `player screen 2`. The model not loaded, Esc in a mission opened the options | the wrong menu; all 35 model names of the menus then checked against `MODELS` |
| Part 13d: the client area was 1590x1190 where the original has 1600x1200: the linker's newer subsystem version. `/SUBSYSTEM:WINDOWS,5.01`, as near as the linker allows to the original's | comparing pictures with the original |
| After 13d: the frame limiter's `timeSetEvent` used `TIME_CALLBACK_EVENT_SET` where the original has `TIME_CALLBACK_EVENT_PULSE` (0x20). On the manual-reset event, SET left it signalled for ever, so the wait never waited, yet each frame still reported 10 ms: game speed was frame rate / 100. Measured over 5 s: 16-19 % fast in the mission's cutscene, about 23 times in the menus. After: game time 96 % of real, about 96 frames a second. The original's own ratio was not measured | the user: movement and climbing much too fast |
| After 13d: the supply boxes' animations were named `cratefly`, `crateopen`, `crateclosed`, `cratedrop`; the files and the original's strings have a `#` in front. Every load failed, so the boxes never opened. The harness had copied the same wrong names | the user: no open model at the firing range; then `tools/name_audit.py`, and a trace of every failed `Get` in Training: only the shadows' expected `s` + name probes were left |
| Part 13e: `OP_CSC_DuplicateHuman` linked the weapon to the hand, which is below the holster it was under: a frame linked below its own child, a cycle, and `UpdateChildren` recursed until the stack ran out. The original links it to the holster again, at the hand's place | stack overflow at Snowball's cutscene |
| Part 13e: `C_EquipmentStoreList`: `BuildPage` is virtual, and the equipment screen has its own (0x0067cb60, reached only through vtable 0x00815e8c, so no name or xref asked for it). Without it the page was never built: an empty store | one blank tile on the inventory screen, against the original |
| Part 13e: `GetText` of an unknown id returned "" (my "fix" of 13b); the original's is null, and `SayQuickMessage` says nothing then. Green lines of only a name | looking at the fight |
| Part 13e: 33 names written with `_` where the original has a space: the inventory's four arrows (the controls did not exist: always lit), the player screen's, the presets'. Found with `tools/literal_audit.py`, all at once | the arrows, against the original |
| The version text has three numbers (`%d.%02d.%d`), the port passed two: `1.12.1286592330`. The third is the original's 0x009820f4, which nothing writes: `1.12.0` | looking at the menu |

The early files (`app.cpp` above all) were ported before the later rigour: expect more of this kind there. `InitSound`, `InitRenderer` and `ApplyRenderSettings` were compared with the assembly and agree. Most of the rest of `app.cpp` has not been.

## Tested by the user, playing (2026-11-06)

Movement speed, climbing and ladders; supply boxes opening and emptying; the firing range's targets falling; sound in fights; grenades, time-delay and impact (No.36, No.69); full screen; the Bren, Sten, Springfield and Fairbairn-Sykes dagger; taking items from a dead enemy.

## Not tested

- **The rest of the game:** explosives, the other weapons, vehicles, squad orders and the command map not reported yet; no mission reported played to its end.
- **The player's shots hitting anyone:** the user watched 13e's tests: the scripted shots went too high (the script only turned, it never aimed down), and the kills were the squad's. Sound in those fights was right, by the user's ear.
- **The fight's balance:** enemies acted (they closed in from 90 m, shot, killed), but it was not compared with the original's. Getting near them used a temporary hook and scripted walking, not committed.
- **How it plays.** One walk compared with the original: W held the same time ends in nearly the same place (the gate looks about 8 % larger in the port's picture, one sample, within this test's timing). Climbing, animation and AI not compared.
- `I3D_joint::UnknownA8` (slot 0xA8, past what the LS3DF port's header declares): only behind a debug switch.
