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
- Both are large address aware (since 2026-11-07; the original is not): up to 4 GB on 64-bit Windows, as the community LAA patchers give the original. `--no-laa` links without it (2 GB), to rule it out for a crash. The script prints the flag it finds in the linked exe.

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

Every LS3DF branch so far draws no sky: `I3D_driver::Render` tests `backdropSector`'s visible flag, which is cleared every frame and never set; the original (0x1005fce2) tests `primarySector`'s (+0x1F8). The screen's clear colour shows instead: flat green, navy or grey by mission. `_re/ls3df_patches/backdrop_visible.patch` is the fix. It is in the run folder's DLL, and in the LS3DF repositories only once applied there.

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
| After 13e (the user: the gamma slider is grey): one variable of the original ported as two. `C_NetGame` is a global at 0x0097d070, so its fields also appear as absolute addresses, and those got names of their own: `g_graphFlags` is +0x4DFC, `g_netReplication` +0x14, `g_serverName` +0x4E00, `g_netMaxPing`/`Freq`/`Inactivity` +0x5064 on. `g_levelMinY` and `g_netSceneTop` are `g_scanMin[1]` and `g_scanMax[1]`. Each half was set on one side and read on the other. | `m_graphFlags` stayed 0, and `Close`, on showing the main menu, cleared all of IGraph's flags, the full screen one too, which the slider tests. `g_netReplication` was never set: a null pointer in any multiplayer game. `g_levelMinY` stayed 0: a human below y = -5 was killed. The server's name on the info screen was empty, and the console's limits never reached the ping and inactivity checks. |
| After 13e (the user: joining a server stays at "Connecting"): the same kind of split. `C_NetGame`'s transport is a pointer in the port and by value in the original (+0x33C), so `+0x414` is the transport's own `m_callback` (+0xD8), which the port kept as a second field. `Init` set that one, the transport's stayed null, and every datagram was dropped. | The server's first message, which ends "Connecting", never reached the game, as a client. A server hosted by the port would have lost its clients' messages the same way (not run). |
| After 13e (the user: the tactical map crashed on a double right-click): `S_HumanOrder`'s path was made with `std::malloc` and freed with `std::free` in `squad_order.cpp`, while `BuildPathPoints` makes the same array with `_aligned_malloc`, as the original does throughout (0x0058c8e0). | `free` of an aligned block: the second click hits the new order's marker, deletes the order, and the heap fails in `ReleaseHumanOrder` (the user's `HD2_Port.RPT`). |
| Also then, found by the checked build: `IScene::UnmapScreenPoint` writes four floats each, into the port's three-float `S_vector`s. | `_RTC_CheckStackVars` in `C_CommandPanel::PickTarget` on opening the map; in the release build, two floats written over neighbouring locals. |
| After 13e (the user, cooperative with other people: magazines now and then invisible, the Sten's, the MP 40's): `SetWeaponView(def->GetFpvModel(), def->GetFpvMagazine(), ...)`. MSVC evaluates the arguments right to left, so the magazine pointer was read before `GetFpvModel` first loaded it (seen in the old file's `/FAs` listing; the original calls `GetFpvModel` first, 0x00492548). | The first time a weapon type is drawn, its first-person model has no magazine, until the next draw. Fix confirmed by the user (2026-11-07): a Sten drawn and put away three times, several reloads, the magazine shown every time. |
| Also then: a teammate's rank in a network game (their crosshair name, the inventory, the member panel) kept the rank of an earlier match. `GetProfileUnknown24` (0x005a8380) returns `stats[NET_STAT_RANK]` in a network game; the port took that copy for unused and read the profile. | The local profile's rank instead of the round's. |

## Tested by the user, playing (2026-11-06)

Movement speed, climbing and ladders; supply boxes opening and emptying; the firing range's targets falling; sound in fights; grenades, time-delay and impact (No.36, No.69); full screen; the Bren, Sten, Springfield and Fairbairn-Sykes dagger; taking items from a dead enemy.

Africa1, played to its end, through the finishing screen and the soldiers' stat increases: the sky (with `backdrop_visible.patch`); a Panzerfaust destroying the Panzer III; dynamite on the fuel tanks; both fixed MG 42s; the SAS Jeep, FlaK truck with its 20 mm cannon, and BMW R75; bullet stains on both sides; a locked door opened with a key; picking up the mission's documents.

Multiplayer, as a client: an RpR internet deathmatch server (joined, spawned, moved; the server was empty), and the user's LAN cooperative server on Co_Sicily2 (defusing all the bridge's dynamite, a clam mine on a Panzer III). Once an item would not move from backpack to pouches (a clam mine): by the user, the original exe sometimes does this in multiplayer too, with any item.

On the user's LAN server, as a client: occupation on Normandy3; objectives on Czech3, spawned as Axis, with the VIP role and its defined uniform assigned.

Africa5, part of it: the stealth and disguise meter; saving and loading in a mission; every squad order tried (some need gear not at hand, a launcher for "Use RPG"). The tactical map's crash: above; the user confirmed the double right-click works since the fix. (Scripted right clicks, `send_keys` `rclick`, `rclick2`, pan the camera and place no known order; the user's bind is Caps Lock, and an odd number of presses leaves the keyboard's Caps Lock on.)

Every single player mission loads without a crash. The aqualung and submarines on Norway and Normandy1. Africa2 and Africa6 played to their ends.

Cooperative with other people, on the original's dedicated server, two of them on the port. Open from that session:

- **A crash while healing a teammate**, on the other player's port client, partway through the heal; other heals of human players worked. Not explained: reading the heal's code paths found nothing, and the crash report (`HD2_Port.RPT` on that computer) has not been seen.
- **Dropped enemy weapons floating above the ground**, seen by the other player. A client puts a rested item where the server's placement message says, and a body started without speed stays at rest (`C_ItemActor::OnPlacement`, `ResetBody`, `C_RigidBody::Start`, matching the original's assembly), so the position looks like the server's. Not compared with the original client on the same server.

## Not tested

- **The rest of the game:** the other missions past loading, and the weapons not used yet.
- **Multiplayer:** hosting with the port, listen or dedicated. To join a local server of the original's: `HD2DS_SabreSquadron.exe -cmd -exec` a file with `domain internet`, `port 11001`, `style deathmatch`, `mapname Africa1`, `server`; the client: `-window -cmd domain internet port 11001 join <address>`.
- **The fight's balance:** the user's impression from playing is that combat against the AI is identical to the original's; not measured side by side. Getting near them used a temporary hook and scripted walking, not committed.
- **How it plays.** One walk compared with the original: W held the same time ends in nearly the same place (the gate looks about 8 % larger in the port's picture, one sample, within this test's timing). Climbing, animation and AI not compared.
- `I3D_joint::UnknownA8` (slot 0xA8, past what the LS3DF port's header declares): only behind a debug switch.
