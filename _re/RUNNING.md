# Running the port (Part 13c, 2026-11-06)

The port starts, plays the logo and intro videos, and reaches a fully drawn main menu. The mouse works, the "Are you sure?" box works, and END exits cleanly. **No mission has been loaded.**

## Build

```bash
python _re/tools/build_game.py --copy "H:\HD2_PortRun"
python _re/tools/build_game.py --checked
```

x87 floats (`/arch:IA32`), symbols, a map; objects in `%TEMP%\hd2_game` or `%TEMP%\hd2_game_checked`. Only changed sources are compiled: use `--all` after a header change. A full build takes under a minute.

- Default: `/O2 /Oy- /MT`, the build to play.
- `--checked`: `/Od /MTd /RTC1`. A call with the wrong arguments stops at `_RTC_CheckEsp`, stack overruns at `_RTC_CheckStackVars`, and the debug heap fills memory. Use this first when something is wrong: it found the first and third bugs below immediately. Keep the exe's own PDB next to it (`HD2_PortChk.exe`, `HD2_PortChk.pdb`).

The original's settings copied: base 0x400000, 1 MB stack, no DEP, not relocatable.

## Run

The run folder is `H:\HD2_PortRun`, a copy of `H:\HD2_Unmodified` with `HD2_Port.exe` added. It uses the **original** `LS3DF.dll`, `rw_data.dll`, Bink, Vorbis. The game writes `log.txt` and `window.cfg` there. It only reads the registry (`HKLM\SOFTWARE\WOW6432Node\Illusion Softworks\Hidden & Dangerous 2`: the CD key, `LS3D_setup`), shared with the installed game.

- `-window`: the port's own switch. A window whatever `LS3D_setup` says. **Do not use `-c`**: it is the console mode, and it hides the window.
- Avoid ` -c` or `-cmd` anywhere in the command line and folder names: the game looks for them with `strstr`.

## Tools

- `_re/tools/debugrun.cpp`: a small debugger (build it with MSVC x86, see its head). Logs DLLs, exceptions with registers and a symbolized stack, the exit code. `--timeout N` logs all threads and kills the game after N seconds.
- `_re/tools/shot.ps1`, `send_keys.ps1`: a picture of the game's client area, keys by scan code (0x01 is Esc), mouse by relative moves (the game adds up DirectInput's deltas, so `click:x:y` goes to the corner first; 1:1 at 1600x1200). Both refuse to act unless the game's window has the focus.
- `_re/tools/vtable_audit.py`: the game's engine interfaces (`src/ls3d`) against the LS3DF port's headers, slot by slot: bytes of arguments, floats, a float result. **Run it after touching `src/ls3d`.**

## Bugs the first run found

| Bug | Found by |
|---|---|
| `IGraph::InitInput` declared with no argument (the original passes 0): the stack was off by 4, a register was lost in `RunGame`, crash at `C_GameApp::Init` | `--checked`, stack check |
| Same kind, unreached so far: `ISND_source::SetSound` has a flags argument; `CreateJointOverride` has none and `Add` takes the name (the original pushes the name first, then makes the override) | `vtable_audit.py` |
| Main loop: `GetGame()` used without its null test, in the menus. Also a command pending while the player's controls were off was dropped; the original keeps it | crash, then the assembly |
| `NetFirewallAvailable`: `OSVERSIONINFOA` on the stack, `GetVersionExA` told it had the 0x9C bytes of the larger struct: the system wrote past it | `--checked`, stack check |
| `S_IGraphInit` was the wrong layout (flags at +0x28, not +0x2C; bit depth, refresh rate and the error callback missing; flag names guessed): the engine got flags 0, so never full screen, and a window whose viewport failed. `C_TextBox::Render` could not restore the viewport after its first clipped box, so all later menu text was clipped away. Now from the assembly of 0x006db440 and the LS3DF port's header | a menu without text, `SetViewport` failing inside d3d8 |
| The version text has three numbers (`%d.%02d.%d`), the port passed two: `1.12.1286592330`. The third is the original's 0x009820f4, which nothing writes: `1.12.0` | looking at the menu |

The early files (`app.cpp` above all) were ported before the later rigour: expect more of this kind there. `InitSound`, `InitRenderer` and `ApplyRenderSettings` were compared with the assembly and agree. Most of the rest of `app.cpp` has not been.

## Not tested

- **Full screen.** Every run used `-window`, because full screen changes the display mode. The flags are now right, but it has not been seen.
- **Any mission**, the menus behind the main one, saving, sound in play. Sound started (see `log.txt`) but nobody listened to it.
- The window's client area is 1590x1190 for a 1600x1200 back buffer (windowed only; the picture is scaled slightly). Not compared with the original in a window.
- The title bar is empty and there is no icon: the port has no resources yet. The string resource could not be read from the file on disk (the packed exe's), and the title is not worth guessing.
- `I3D_joint::UnknownA8` (slot 0xA8, past what the LS3DF port's header declares): only behind a debug switch.
